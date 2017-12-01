/**
* Transcode manager
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
#include <memory>
#include <limits>
#include <smmintrin.h>

#include "StreamUtils.hpp"
#include "OSUtil.hpp"
#include "TsSplitter.hpp"
#include "Transcode.hpp"
#include "TranscodeSetting.hpp"
#include "StreamReform.hpp"
#include "PacketCache.hpp"
#include "AMTSource.hpp"
#include "LogoScan.hpp"
#include "CMAnalyze.hpp"
#include "InterProcessComm.hpp"
#include "CaptionData.hpp"
#include "CaptionFormatter.hpp"
#include "EncoderOptionParser.hpp"

class AMTSplitter : public TsSplitter {
public:
	AMTSplitter(AMTContext& ctx, const ConfigWrapper& setting)
		: TsSplitter(ctx, setting.isSubtitlesEnabled())
		, setting_(setting)
		, psWriter(ctx)
		, writeHandler(*this)
		, audioFile_(setting.getAudioFilePath(), "wb")
		, waveFile_(setting.getWaveFilePath(), "wb")
		, videoFileCount_(0)
		, videoStreamType_(-1)
		, audioStreamType_(-1)
		, audioFileSize_(0)
		, waveFileSize_(0)
		, srcFileSize_(0)
	{
		psWriter.setHandler(&writeHandler);
	}

	StreamReformInfo split()
	{
		readAll();

		// for debug
		printInteraceCount();

		return StreamReformInfo(ctx, videoFileCount_,
			videoFrameList_, audioFrameList_, captionTextList_, streamEventList_);
	}

	int64_t getSrcFileSize() const {
		return srcFileSize_;
	}

	int64_t getTotalIntVideoSize() const {
		return writeHandler.getTotalSize();
	}

protected:
	class StreamFileWriteHandler : public PsStreamWriter::EventHandler {
		TsSplitter& this_;
		std::unique_ptr<File> file_;
		int64_t totalIntVideoSize_;
	public:
		StreamFileWriteHandler(TsSplitter& this_)
			: this_(this_), totalIntVideoSize_() { }
		virtual void onStreamData(MemoryChunk mc) {
			if (file_ != NULL) {
				file_->write(mc);
				totalIntVideoSize_ += mc.length;
			}
		}
		void open(const std::string& path) {
			totalIntVideoSize_ = 0;
			file_ = std::unique_ptr<File>(new File(path, "wb"));
		}
		void close() {
			file_ = nullptr;
		}
		int64_t getTotalSize() const {
			return totalIntVideoSize_;
		}
	};

	const ConfigWrapper& setting_;
	PsStreamWriter psWriter;
	StreamFileWriteHandler writeHandler;
	File audioFile_;
	File waveFile_;

	int videoFileCount_;
	int videoStreamType_;
	int audioStreamType_;
	int64_t audioFileSize_;
	int64_t waveFileSize_;
	int64_t srcFileSize_;

	// データ
	std::vector<FileVideoFrameInfo> videoFrameList_;
	std::vector<FileAudioFrameInfo> audioFrameList_;
	std::vector<StreamEvent> streamEventList_;
	std::vector<CaptionItem> captionTextList_;

	void readAll() {
		enum { BUFSIZE = 4 * 1024 * 1024 };
		auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
		MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
		File srcfile(setting_.getSrcFilePath(), "rb");
		srcFileSize_ = srcfile.size();
		size_t readBytes;
		do {
			readBytes = srcfile.read(buffer);
			inputTsData(MemoryChunk(buffer.data, readBytes));
		} while (readBytes == buffer.length);
	}

	static bool CheckPullDown(PICTURE_TYPE p0, PICTURE_TYPE p1) {
		switch (p0) {
		case PIC_TFF:
		case PIC_BFF_RFF:
			return (p1 == PIC_TFF || p1 == PIC_TFF_RFF);
		case PIC_BFF:
		case PIC_TFF_RFF:
			return (p1 == PIC_BFF || p1 == PIC_BFF_RFF);
		default: // それ以外はチェック対象外
			return true;
		}
	}

	void printInteraceCount() {

		if (videoFrameList_.size() == 0) {
			ctx.error("フレームがありません");
			return;
		}

		// ラップアラウンドしないPTSを生成
		std::vector<std::pair<int64_t, int>> modifiedPTS;
		int64_t videoBasePTS = videoFrameList_[0].PTS;
		int64_t prevPTS = videoFrameList_[0].PTS;
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			int64_t PTS = videoFrameList_[i].PTS;
			int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
			modifiedPTS.emplace_back(modPTS, i);
			prevPTS = modPTS;
		}

		// PTSでソート
		std::sort(modifiedPTS.begin(), modifiedPTS.end());

#if 0
		// フレームリストを出力
		FILE* framesfp = fopen("frames.txt", "w");
		fprintf(framesfp, "FrameNumber,DecodeFrameNumber,PTS,Duration,FRAME_TYPE,PIC_TYPE,IsGOPStart\n");
		for (int i = 0; i < (int)modifiedPTS.size(); ++i) {
			int64_t PTS = modifiedPTS[i].first;
			int decodeIndex = modifiedPTS[i].second;
			const VideoFrameInfo& frame = videoFrameList_[decodeIndex];
			int PTSdiff = -1;
			if (i < (int)modifiedPTS.size() - 1) {
				int64_t nextPTS = modifiedPTS[i + 1].first;
				const VideoFrameInfo& nextFrame = videoFrameList_[modifiedPTS[i + 1].second];
				PTSdiff = int(nextPTS - PTS);
				if (CheckPullDown(frame.pic, nextFrame.pic) == false) {
					ctx.warn("Flag Check Error: PTS=%lld %s -> %s",
						PTS, PictureTypeString(frame.pic), PictureTypeString(nextFrame.pic));
				}
			}
			fprintf(framesfp, "%d,%d,%lld,%d,%s,%s,%d\n",
				i, decodeIndex, PTS, PTSdiff, FrameTypeString(frame.type), PictureTypeString(frame.pic), frame.isGopStart ? 1 : 0);
		}
		fclose(framesfp);
#endif

		// PTS間隔を出力
		struct Integer {
			int v;
			Integer() : v(0) { }
		};

		std::array<int, MAX_PIC_TYPE> interaceCounter = { 0 };
		std::map<int, Integer> PTSdiffMap;
		prevPTS = -1;
		for (const auto& ptsIndex : modifiedPTS) {
			int64_t PTS = ptsIndex.first;
			const VideoFrameInfo& frame = videoFrameList_[ptsIndex.second];
			interaceCounter[(int)frame.pic]++;
			if (prevPTS != -1) {
				int PTSdiff = int(PTS - prevPTS);
				PTSdiffMap[PTSdiff].v++;
			}
			prevPTS = PTS;
		}

		ctx.info("[映像フレーム統計情報]");

		int64_t totalTime = modifiedPTS.back().first - videoBasePTS;
		ctx.info("時間: %f 秒", totalTime / 90000.0);

		ctx.info("FRAME=%d DBL=%d TLP=%d TFF=%d BFF=%d TFF_RFF=%d BFF_RFF=%d",
			interaceCounter[0], interaceCounter[1], interaceCounter[2], interaceCounter[3], interaceCounter[4], interaceCounter[5], interaceCounter[6]);

		for (const auto& pair : PTSdiffMap) {
			ctx.info("(PTS_Diff,Cnt)=(%d,%d)", pair.first, pair.second.v);
		}
	}

	// TsSplitter仮想関数 //

	virtual void onVideoPesPacket(
		int64_t clock,
		const std::vector<VideoFrameInfo>& frames,
		PESPacket packet)
	{
		for (const VideoFrameInfo& frame : frames) {
			videoFrameList_.push_back(frame);
			videoFrameList_.back().fileOffset = writeHandler.getTotalSize();
		}
		psWriter.outVideoPesPacket(clock, frames, packet);
	}

	virtual void onVideoFormatChanged(VideoFormat fmt) {
		ctx.info("[映像フォーマット変更]");
		if (fmt.fixedFrameRate) {
			ctx.info("サイズ: %dx%d FPS: %d/%d", fmt.width, fmt.height, fmt.frameRateNum, fmt.frameRateDenom);
		}
		else {
			ctx.info("サイズ: %dx%d FPS: VFR", fmt.width, fmt.height);
		}

		// 出力ファイルを変更
		writeHandler.open(setting_.getIntVideoFilePath(videoFileCount_++));
		psWriter.outHeader(videoStreamType_, audioStreamType_);

		StreamEvent ev = StreamEvent();
		ev.type = VIDEO_FORMAT_CHANGED;
		ev.frameIdx = (int)videoFrameList_.size();
		streamEventList_.push_back(ev);
	}

	virtual void onAudioPesPacket(
		int audioIdx, 
		int64_t clock, 
		const std::vector<AudioFrameData>& frames, 
		PESPacket packet)
	{
		for (const AudioFrameData& frame : frames) {
			FileAudioFrameInfo info = frame;
			info.audioIdx = audioIdx;
			info.codedDataSize = frame.codedDataSize;
			info.waveDataSize = frame.decodedDataSize;
			info.fileOffset = audioFileSize_;
			info.waveOffset = waveFileSize_;
			audioFile_.write(MemoryChunk(frame.codedData, frame.codedDataSize));
			if (frame.decodedDataSize > 0) {
				waveFile_.write(MemoryChunk((uint8_t*)frame.decodedData, frame.decodedDataSize));
			}
			audioFileSize_ += frame.codedDataSize;
			waveFileSize_ += frame.decodedDataSize;
			audioFrameList_.push_back(info);
		}
		if (videoFileCount_ > 0) {
			psWriter.outAudioPesPacket(audioIdx, clock, frames, packet);
		}
	}

	virtual void onAudioFormatChanged(int audioIdx, AudioFormat fmt) {
		ctx.info("[音声%dフォーマット変更]", audioIdx);
		ctx.info("チャンネル: %s サンプルレート: %d",
			getAudioChannelString(fmt.channels), fmt.sampleRate);

		StreamEvent ev = StreamEvent();
		ev.type = AUDIO_FORMAT_CHANGED;
		ev.audioIdx = audioIdx;
		ev.frameIdx = (int)audioFrameList_.size();
		streamEventList_.push_back(ev);
	}
	
	virtual void onCaptionPesPacket(
		int64_t clock,
		std::vector<CaptionItem>& captions,
		PESPacket packet)
	{
		for (auto& caption : captions) {
			captionTextList_.emplace_back(std::move(caption));
		}
	}

	virtual std::string getDRCSOutPath(const std::string& md5) {
		return setting_.getDRCSOutPath(md5);
	}

	// TsPacketSelectorHandler仮想関数 //

	virtual void onPidTableChanged(const PMTESInfo video, const std::vector<PMTESInfo>& audio, const PMTESInfo caption) {
		// ベースクラスの処理
		TsSplitter::onPidTableChanged(video, audio, caption);

		ASSERT(audio.size() > 0);
		videoStreamType_ = video.stype;
		audioStreamType_ = audio[0].stype;

		StreamEvent ev = StreamEvent();
		ev.type = PID_TABLE_CHANGED;
		ev.numAudio = (int)audio.size();
		ev.frameIdx = (int)videoFrameList_.size();
		streamEventList_.push_back(ev);
	}
};

class RFFExtractor
{
public:
	void clear() {
		prevFrame_ = nullptr;
	}

	void inputFrame(av::EncodeWriter& encoder, std::unique_ptr<av::Frame>&& frame, PICTURE_TYPE pic) {

		// PTSはinputFrameで再定義されるので修正しないでそのまま渡す
		switch (pic) {
		case PIC_FRAME:
		case PIC_TFF:
		case PIC_TFF_RFF:
			encoder.inputFrame(*frame);
			break;
		case PIC_FRAME_DOUBLING:
			encoder.inputFrame(*frame);
			encoder.inputFrame(*frame);
			break;
		case PIC_FRAME_TRIPLING:
			encoder.inputFrame(*frame);
			encoder.inputFrame(*frame);
			encoder.inputFrame(*frame);
			break;
		case PIC_BFF:
			encoder.inputFrame(*mixFields(
				(prevFrame_ != nullptr) ? *prevFrame_ : *frame, *frame));
			break;
		case PIC_BFF_RFF:
			encoder.inputFrame(*mixFields(
				(prevFrame_ != nullptr) ? *prevFrame_ : *frame, *frame));
			encoder.inputFrame(*frame);
			break;
		}

		prevFrame_ = std::move(frame);
	}

private:
	std::unique_ptr<av::Frame> prevFrame_;

	// 2つのフレームのトップフィールド、ボトムフィールドを合成
	static std::unique_ptr<av::Frame> mixFields(av::Frame& topframe, av::Frame& bottomframe)
	{
		auto dstframe = std::unique_ptr<av::Frame>(new av::Frame());

		AVFrame* top = topframe();
		AVFrame* bottom = bottomframe();
		AVFrame* dst = (*dstframe)();

		// フレームのプロパティをコピー
		av_frame_copy_props(dst, top);

		// メモリサイズに関する情報をコピー
		dst->format = top->format;
		dst->width = top->width;
		dst->height = top->height;

		// メモリ確保
		if (av_frame_get_buffer(dst, 64) != 0) {
			THROW(RuntimeException, "failed to allocate frame buffer");
		}

		const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(dst->format));
		int pixel_shift = (desc->comp[0].depth > 8) ? 1 : 0;
		int nplanes = (dst->format != AV_PIX_FMT_NV12) ? 3 : 2;

		for (int i = 0; i < nplanes; ++i) {
			int hshift = (i > 0 && dst->format != AV_PIX_FMT_NV12) ? desc->log2_chroma_w : 0;
			int vshift = (i > 0) ? desc->log2_chroma_h : 0;
			int wbytes = (dst->width >> hshift) << pixel_shift;
			int height = dst->height >> vshift;

			for (int y = 0; y < height; y += 2) {
				uint8_t* dst0 = dst->data[i] + dst->linesize[i] * (y + 0);
				uint8_t* dst1 = dst->data[i] + dst->linesize[i] * (y + 1);
				uint8_t* src0 = top->data[i] + top->linesize[i] * (y + 0);
				uint8_t* src1 = bottom->data[i] + bottom->linesize[i] * (y + 1);
				memcpy(dst0, src0, wbytes);
				memcpy(dst1, src1, wbytes);
			}
		}

		return std::move(dstframe);
	}
};

static PICTURE_TYPE getPictureTypeFromAVFrame(AVFrame* frame)
{
	bool interlaced = frame->interlaced_frame != 0;
	bool tff = frame->top_field_first != 0;
	int repeat = frame->repeat_pict;
	if (interlaced == false) {
		switch (repeat) {
		case 0: return PIC_FRAME;
		case 1: return tff ? PIC_TFF_RFF : PIC_BFF_RFF;
		case 2: return PIC_FRAME_DOUBLING;
		case 4: return PIC_FRAME_TRIPLING;
		default: THROWF(FormatException, "Unknown repeat count: %d", repeat);
		}
		return PIC_FRAME;
	}
	else {
		if (repeat) {
			THROW(FormatException, "interlaced and repeat ???");
		}
		return tff ? PIC_TFF : PIC_BFF;
	}
}

class AMTFilterSource : public AMTObject {
public:
	// Main (+ Post)
	AMTFilterSource(AMTContext&ctx,
		const ConfigWrapper& setting,
		const StreamReformInfo& reformInfo,
		const std::vector<EncoderZone>& zones,
		const std::string& logopath,
		int fileId, int encoderId, CMType cmtype)
		: AMTObject(ctx)
		, setting_(setting)
		, env_(make_unique_ptr(CreateScriptEnvironment2()))
	{
		try {
			env_->Invoke("Eval", AVSValue(makePreamble().c_str()));

			AVSValue avsv;
			env_->LoadPlugin(GetModulePath().c_str(), true, &avsv);

			std::vector<int> outFrames;
			filter_ = makeMainFilterSource(fileId, encoderId, cmtype, outFrames, reformInfo, logopath);

			std::string mainpath = setting.getFilterScriptPath();
			if (mainpath.size()) {
				env_->SetVar("AMT_SOURCE", filter_);
				filter_ = env_->Invoke("Import", mainpath.c_str(), 0).AsClip();
			}
			
			std::string postpath = setting.getPostFilterScriptPath();
			if (postpath.size()) {
				env_->SetVar("AMT_SOURCE", filter_);
				filter_ = env_->Invoke("Import", postpath.c_str(), 0).AsClip();
			}

			MakeZones(filter_, fileId, encoderId, outFrames, zones, reformInfo);

			MakeOutFormat(reformInfo.getFormat(encoderId, fileId).videoFormat);
		}
		catch (const AvisynthError& avserror) {
			// AvisynthErrorはScriptEnvironmentに依存しているので
			// AviSyntExceptioに変換する
			THROWF(AviSynthException, "%s", avserror.msg);
		}
	}

	~AMTFilterSource() {
		filter_ = nullptr;
		env_ = nullptr;
	}

	const PClip& getClip() const {
		return filter_;
	}

	const VideoFormat& getFormat() const {
		return outfmt_;
	}

	const std::vector<EncoderZone> getZones() const {
		return outZones_;
	}

	IScriptEnvironment2* getEnv() const {
		return env_.get();
	}

private:
	const ConfigWrapper& setting_;
	ScriptEnvironmentPointer env_;
	PClip filter_;
	VideoFormat outfmt_;
	std::vector<EncoderZone> outZones_;

	std::string makePreamble() {
		StringBuilder sb;
		// システムのプラグインフォルダを無効化
		if (setting_.isSystemAvsPlugin() == false) {
			sb.append("ClearAutoloadDirs()\n");
		}
		// Amatsukaze用オートロードフォルダを追加
		sb.append("AddAutoloadDir(\"%s\\plugins64\")\n", GetModuleDirectory());
		// メモリ節約オプションを有効にする
		sb.append("SetCacheMode(CACHE_OPTIMAL_SIZE)\n");
		return sb.str();
	}

	PClip prefetch(PClip clip, int threads) {
		AVSValue args[] = { clip, threads };
		return env_->Invoke("Prefetch", AVSValue(args, 2)).AsClip();
	}

	PClip makeMainFilterSource(
		int fileId, int encoderId, CMType cmtype,
		std::vector<int>& outFrames,
		const StreamReformInfo& reformInfo,
		const std::string& logopath)
	{
		auto& fmt = reformInfo.getFormat(encoderId, fileId);
		PClip clip = new av::AMTSource(ctx,
			setting_.getIntVideoFilePath(fileId),
			setting_.getWaveFilePath(),
			fmt.videoFormat, fmt.audioFormat[0],
			reformInfo.getFilterSourceFrames(fileId),
			reformInfo.getFilterSourceAudioFrames(fileId),
			setting_.getDecoderSetting(),
			env_.get());
		
		clip = prefetch(clip, 1);

		if (logopath.size() > 0) {
			// キャッシュを間に入れるためにInvokeでフィルタをインスタンス化
			AVSValue args_a[] = { clip, logopath.c_str() };
			PClip analyzeclip = prefetch(env_->Invoke("AMTAnalyzeLogo", AVSValue(args_a, 2)).AsClip(), 1);
			AVSValue args_e[] = { clip, analyzeclip, logopath.c_str() };
			clip = env_->Invoke("AMTEraseLogo2", AVSValue(args_e, 3)).AsClip();
		}

		return trimInput(clip, fileId, encoderId, cmtype, outFrames, reformInfo);
	}

	PClip trimInput(PClip clip, int fileId, int encoderId, CMType cmtype,
		std::vector<int>& outFrames,
		const StreamReformInfo& reformInfo)
	{
		// このencoderIndex+cmtype用の出力フレームリスト作成
		auto& srcFrames = reformInfo.getFilterSourceFrames(fileId);
		outFrames.clear();
		for (int i = 0; i < (int)srcFrames.size(); ++i) {
			int frameEncoderIndex = reformInfo.getEncoderIndex(srcFrames[i].frameIndex);
			if (encoderId == frameEncoderIndex) {
				if (cmtype == CMTYPE_BOTH || cmtype == srcFrames[i].cmType) {
					outFrames.push_back(i);
				}
			}
		}
		int numSrcFrames = (int)outFrames.size();

		// 不連続点で区切る
		std::vector<EncoderZone> trimZones;
		EncoderZone zone;
		zone.startFrame = outFrames.front();
		for (int i = 1; i < (int)outFrames.size(); ++i) {
			if (outFrames[i] != outFrames[i - 1] + 1) {
				zone.endFrame = outFrames[i - 1];
				trimZones.push_back(zone);
				zone.startFrame = outFrames[i];
			}
		}
		zone.endFrame = outFrames.back();
		trimZones.push_back(zone);

		if (trimZones.size() > 1 ||
			trimZones[0].startFrame != 0 ||
			trimZones[0].endFrame != (srcFrames.size() - 1))
		{
			// Trimが必要
			std::vector<AVSValue> trimClips(trimZones.size());
			for (int i = 0; i < (int)trimZones.size(); ++i) {
				AVSValue arg[] = { clip, trimZones[i].startFrame, trimZones[i].endFrame };
				trimClips[i] = env_->Invoke("Trim", AVSValue(arg, 3));
			}
			if (trimClips.size() == 1) {
				clip = trimClips[0].AsClip();
			}
			else {
				clip = env_->Invoke("AlignedSplice", AVSValue(trimClips.data(), (int)trimClips.size())).AsClip();
			}
		}

		return clip;
	}

	PClip makePostFilterSource(const std::string& intfile, const VideoFormat& infmt) {
		return new av::AVSLosslessSource(ctx, intfile, infmt, env_.get());
	}

	void MakeZones(
		PClip postClip,
		int fileId, int encoderId,
		const std::vector<int>& outFrames,
		const std::vector<EncoderZone>& zones,
		const StreamReformInfo& reformInfo)
	{
		int numSrcFrames = (int)outFrames.size();

		// このencoderIndex用のゾーンを作成
		outZones_.clear();
		for (int i = 0; i < (int)zones.size(); ++i) {
			EncoderZone newZone = {
				(int)(std::lower_bound(outFrames.begin(), outFrames.end(), zones[i].startFrame) - outFrames.begin()),
				(int)(std::lower_bound(outFrames.begin(), outFrames.end(), zones[i].endFrame) - outFrames.begin())
			};
			// 短すぎる場合はゾーンを捨てる
			if (newZone.endFrame - newZone.startFrame > 30) {
				outZones_.push_back(newZone);
			}
		}

		const VideoFormat& infmt = reformInfo.getFormat(encoderId, fileId).videoFormat;
		VideoInfo outvi = postClip->GetVideoInfo();
		double srcDuration = (double)numSrcFrames * infmt.frameRateDenom / infmt.frameRateNum;
		double clipDuration = (double)outvi.num_frames * outvi.fps_denominator / outvi.fps_numerator;
		bool outParity = postClip->GetParity(0);

		ctx.info("フィルタ入力: %dフレーム %d/%dfps (%s)",
			numSrcFrames, infmt.frameRateNum, infmt.frameRateDenom,
			infmt.progressive ? "プログレッシブ" : "インターレース");

		ctx.info("フィルタ出力: %dフレーム %d/%dfps (%s)",
			outvi.num_frames, outvi.fps_numerator, outvi.fps_denominator,
			outParity ? "インターレース" : "プログレッシブ");

		if (std::abs(srcDuration - clipDuration) > 0.1f) {
			THROWF(RuntimeException, "フィルタ出力映像の時間が入力と一致しません（入力: %.3f秒 出力: %.3f秒）", srcDuration, clipDuration);
		}

		if (numSrcFrames != outvi.num_frames && outParity) {
			ctx.warn("フレーム数が変わっていますがインターレースのままです。プログレッシブ出力が目的ならAssumeBFF()をavsファイルの最後に追加してください。");
		}

		// フレーム数が変わっている場合はゾーンを引き伸ばす
		if (numSrcFrames != outvi.num_frames) {
			double scale = (double)outvi.num_frames / numSrcFrames;
			for (int i = 0; i < (int)outZones_.size(); ++i) {
				outZones_[i].startFrame = std::max(0, std::min(outvi.num_frames, (int)std::round(outZones_[i].startFrame * scale)));
				outZones_[i].endFrame = std::max(0, std::min(outvi.num_frames, (int)std::round(outZones_[i].endFrame * scale)));
			}
		}
	}

	void MakeOutFormat(const VideoFormat& infmt)
	{
		auto vi = filter_->GetVideoInfo();
		// vi_からエンコーダ入力用VideoFormatを生成する
		outfmt_ = infmt;
		if (outfmt_.width != vi.width || outfmt_.height != vi.height) {
			// リサイズされた
			outfmt_.width = vi.width;
			outfmt_.height = vi.height;
			// リサイズされた場合はアスペクト比を1:1にする
			outfmt_.sarHeight = outfmt_.sarWidth = 1;
		}
		outfmt_.frameRateDenom = vi.fps_denominator;
		outfmt_.frameRateNum = vi.fps_numerator;
		// インターレースかどうかは取得できないのでパリティがfalse(BFF?)だったらプログレッシブと仮定
		outfmt_.progressive = (filter_->GetParity(0) == false);
	}
};

struct EncodeFileInfo {
	std::string outPath;
	int64_t fileSize;
	double srcBitrate;
	double targetBitrate;
};

class EncoderArgumentGenerator
{
public:
	EncoderArgumentGenerator(
		const ConfigWrapper& setting,
		StreamReformInfo& reformInfo)
		: setting_(setting)
		, reformInfo_(reformInfo)
	{ }

	std::string GenEncoderOptions(
		VideoFormat outfmt,
		std::vector<EncoderZone> zones,
		int videoFileIndex, int encoderIndex, CMType cmtype, int pass)
	{
		VIDEO_STREAM_FORMAT srcFormat = reformInfo_.getVideoStreamFormat();
		double srcBitrate = getSourceBitrate(videoFileIndex);
		return makeEncoderArgs(
			setting_.getEncoder(),
			setting_.getEncoderPath(),
			setting_.getOptions(
				srcFormat, srcBitrate, false, pass, zones,
				videoFileIndex, encoderIndex, cmtype),
			outfmt,
			setting_.getEncVideoFilePath(videoFileIndex, encoderIndex, cmtype));
	}

	double getSourceBitrate(int fileId) const
	{
		// ビットレート計算
		int64_t srcBytes = 0;
		double srcDuration = 0;
		int numEncoders = reformInfo_.getNumEncoders(fileId);
		for (int i = 0; i < numEncoders; ++i) {
			const auto& info = reformInfo_.getSrcVideoInfo(i, fileId);
			srcBytes += info.first;
			srcDuration += info.second;
		}
		return ((double)srcBytes * 8 / 1000) / ((double)srcDuration / MPEG_CLOCK_HZ);
	}

	EncodeFileInfo printBitrate(AMTContext& ctx, int videoFileIndex, CMType cmtype) const
	{
		double srcBitrate = getSourceBitrate(videoFileIndex);
		ctx.info("入力映像ビットレート: %d kbps", (int)srcBitrate);
		VIDEO_STREAM_FORMAT srcFormat = reformInfo_.getVideoStreamFormat();
		double targetBitrate = std::numeric_limits<float>::quiet_NaN();
		if (setting_.isAutoBitrate()) {
			targetBitrate = setting_.getBitrate().getTargetBitrate(srcFormat, srcBitrate);
			if (cmtype == CMTYPE_CM) {
				targetBitrate *= setting_.getBitrateCM();
			}
			ctx.info("目標映像ビットレート: %d kbps", (int)targetBitrate);
		}
		EncodeFileInfo info;
		info.srcBitrate = srcBitrate;
		info.targetBitrate = targetBitrate;
		return info;
	}

private:
	const ConfigWrapper& setting_;
	const StreamReformInfo& reformInfo_;
};

class AMTFilterOutTmp : public AMTObject {
public:
	AMTFilterOutTmp(
		AMTContext&ctx,
		const ConfigWrapper& setting)
		: AMTObject(ctx)
		, thread_(this, 8)
		, codec_(make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze")))
	{
	}

	void filter(
		PClip source, VideoFormat outfmt,
		int start, int num_frames,
		const std::string& outpath,
		IScriptEnvironment* env)
	{
		Stopwatch sw;

		// エンコードスレッド開始
		thread_.start();
		sw.start();

		file_ = std::unique_ptr<LosslessVideoFile>(new LosslessVideoFile(ctx, outpath, "wb"));
		vi_ = source->GetVideoInfo();

		if (vi_.BitsPerComponent() != 8) {
			THROW(FormatException, "メインフィルタ出力が8bitではありません");
		}
		if (vi_.Is420() == false) {
			THROW(FormatException, "メインフィルタ出力がYUV420ではありません");
		}

		size_t rawSize = vi_.width * vi_.height * 3 / 2;
		size_t outSize = codec_->EncodeGetOutputSize(UTVF_YV12, vi_.width, vi_.height);
		size_t extraSize = codec_->EncodeGetExtraDataSize();
		memIn_ = std::unique_ptr<uint8_t[]>(new uint8_t[rawSize]);
		memOut_ = std::unique_ptr<uint8_t[]>(new uint8_t[outSize]);
		std::vector<uint8_t> extra(extraSize);

		if (codec_->EncodeGetExtraData(extra.data(), extraSize, UTVF_YV12, vi_.width, vi_.height)) {
			THROW(RuntimeException, "failed to EncodeGetExtraData (UtVideo)");
		}
		if (codec_->EncodeBegin(UTVF_YV12, vi_.width, vi_.height, CBGROSSWIDTH_WINDOWS)) {
			THROW(RuntimeException, "failed to EncodeBegin (UtVideo)");
		}
		file_->writeHeader(vi_.width, vi_.height, num_frames, extra);

		FpsPrinter fpsPrinter(ctx, 4);
		fpsPrinter.start(num_frames);
		for (int i = 0; i < num_frames; ++i) {
			thread_.put(source->GetFrame(i + start, env), 1);
			fpsPrinter.update(1);
		}

		// エンコードスレッドを終了して自分に引き継ぐ
		thread_.join();

		codec_->EncodeEnd();

		sw.stop();
		fpsPrinter.stop();

		double prod, cons; thread_.getTotalWait(prod, cons);
		ctx.info("Total: %.2fs, FilterWait: %.2fs, EncoderWait: %.2fs", sw.getTotal(), prod, cons);
	}

private:
	class SpDataPumpThread : public DataPumpThread<PVideoFrame, true> {
	public:
		SpDataPumpThread(AMTFilterOutTmp* this_, int bufferingFrames)
			: DataPumpThread(bufferingFrames)
			, this_(this_)
		{ }
	protected:
		virtual void OnDataReceived(PVideoFrame&& data) {
			this_->onFrameReceived(std::move(data));
		}
	private:
		AMTFilterOutTmp* this_;
	};

	SpDataPumpThread thread_;
	std::unique_ptr<LosslessVideoFile> file_;
	VideoInfo vi_;
	CCodecPointer codec_;
	std::unique_ptr<uint8_t[]> memIn_;
	std::unique_ptr<uint8_t[]> memOut_;

	void onFrameReceived(PVideoFrame frame)
	{
		CopyYV12(memIn_.get(), frame, vi_.width, vi_.height);
		bool keyFrame = false;
		size_t codedSize = codec_->EncodeFrame(memOut_.get(), &keyFrame, memIn_.get());
		file_->writeFrame(memOut_.get(), (int)codedSize);
	}
};


class AMTFilterVideoEncoder : public AMTObject {
public:
	AMTFilterVideoEncoder(
		AMTContext&ctx)
		: AMTObject(ctx)
		, thread_(this, 16)
	{ }

	void encode(
		PClip source, VideoFormat outfmt,
		const std::vector<std::string>& encoderOptions,
		IScriptEnvironment* env)
	{
		vi_ = source->GetVideoInfo();
		outfmt_ = outfmt;

		int bufsize = outfmt_.width * outfmt_.height * 3;

		int npass = (int)encoderOptions.size();
		for (int i = 0; i < npass; ++i) {
			ctx.info("%d/%dパス エンコード開始 予定フレーム数: %d", i + 1, npass, vi_.num_frames);

			const std::string& args = encoderOptions[i];
			
			// 初期化
			encoder_ = std::unique_ptr<av::EncodeWriter>(new av::EncodeWriter());

			ctx.info("[エンコーダ起動]");
			ctx.info(args.c_str());
			encoder_->start(args, outfmt_, false, bufsize);

			Stopwatch sw;
			// エンコードスレッド開始
			thread_.start();
			sw.start();

			// エンコード
			for (int i = 0; i < vi_.num_frames; ++i) {
				thread_.put(std::unique_ptr<OutFrame>(new OutFrame(source->GetFrame(i, env), i)), 1);
			}

			// エンコードスレッドを終了して自分に引き継ぐ
			thread_.join();

			// 残ったフレームを処理
			encoder_->finish();
			encoder_ = nullptr;
			sw.stop();

			double prod, cons; thread_.getTotalWait(prod, cons);
			ctx.info("Total: %.2fs, FilterWait: %.2fs, EncoderWait: %.2fs", sw.getTotal(), prod, cons);
		}
	}

private:
	struct OutFrame {
		PVideoFrame frame;
		int n;

		OutFrame(const PVideoFrame& frame, int n)
			: frame(frame)
			, n(n) { }
	};

	class SpDataPumpThread : public DataPumpThread<std::unique_ptr<OutFrame>, true> {
	public:
		SpDataPumpThread(AMTFilterVideoEncoder* this_, int bufferingFrames)
			: DataPumpThread(bufferingFrames)
			, this_(this_)
		{ }
	protected:
		virtual void OnDataReceived(std::unique_ptr<OutFrame>&& data) {
			this_->onFrameReceived(std::move(data));
		}
	private:
		AMTFilterVideoEncoder* this_;
	};

	VideoInfo vi_;
	VideoFormat outfmt_;
	std::unique_ptr<av::EncodeWriter> encoder_;

	SpDataPumpThread thread_;

	int getFFformat() {
		switch (vi_.BitsPerComponent()) {
		case 8: return AV_PIX_FMT_YUV420P;
		case 10: return AV_PIX_FMT_YUV420P10;
		case 12: return AV_PIX_FMT_YUV420P12;
		case 14: return AV_PIX_FMT_YUV420P14;
		case 16: return AV_PIX_FMT_YUV420P16;
		default: THROW(FormatException, "サポートされていないフィルタ出力形式です");
		}
		return 0;
	}

	void onFrameReceived(std::unique_ptr<OutFrame>&& frame) {
		
		// PVideoFrameをav::Frameに変換
		const PVideoFrame& in = frame->frame;
		av::Frame out;

		out()->width = outfmt_.width;
		out()->height = outfmt_.height;
		out()->format = getFFformat();
		out()->sample_aspect_ratio.num = outfmt_.sarWidth;
		out()->sample_aspect_ratio.den = outfmt_.sarHeight;
		out()->color_primaries = (AVColorPrimaries)outfmt_.colorPrimaries;
		out()->color_trc = (AVColorTransferCharacteristic)outfmt_.transferCharacteristics;
		out()->colorspace = (AVColorSpace)outfmt_.colorSpace;

		// AVFrame.dataは16バイトまで超えたアクセスがあり得るので、
		// そのままポインタをセットすることはできない

		if(av_frame_get_buffer(out(), 64) != 0) {
			THROW(RuntimeException, "failed to allocate frame buffer");
		}

		const uint8_t *src_data[4] = {
			in->GetReadPtr(PLANAR_Y),
			in->GetReadPtr(PLANAR_U),
			in->GetReadPtr(PLANAR_V),
			nullptr
		};
		int src_linesize[4] = {
			in->GetPitch(PLANAR_Y),
			in->GetPitch(PLANAR_U),
			in->GetPitch(PLANAR_V),
			0
		};

		av_image_copy(
			out()->data, out()->linesize, src_data, src_linesize, 
			(AVPixelFormat)out()->format, out()->width, out()->height);

		encoder_->inputFrame(out);
	}

};

class AMTSimpleVideoEncoder : public AMTObject {
public:
	AMTSimpleVideoEncoder(
		AMTContext& ctx,
		const ConfigWrapper& setting)
		: AMTObject(ctx)
		, setting_(setting)
		, reader_(this)
		, thread_(this, 8)
	{
		//
	}

	void encode()
	{
		if (setting_.isTwoPass()) {
			ctx.info("1/2パス エンコード開始");
			processAllData(1);
			ctx.info("2/2パス エンコード開始");
			processAllData(2);
		}
		else {
			processAllData(-1);
		}
	}

	int getAudioCount() const {
		return audioCount_;
	}

	int64_t getSrcFileSize() const {
		return srcFileSize_;
	}

	VideoFormat getVideoFormat() const {
		return videoFormat_;
	}

private:
	class SpVideoReader : public av::VideoReader {
	public:
		SpVideoReader(AMTSimpleVideoEncoder* this_)
			: VideoReader(this_->ctx)
			, this_(this_)
		{ }
	protected:
		virtual void onFileOpen(AVFormatContext *fmt) {
			this_->onFileOpen(fmt);
		}
		virtual void onVideoFormat(AVStream *stream, VideoFormat fmt) {
			this_->onVideoFormat(stream, fmt);
		}
		virtual void onFrameDecoded(av::Frame& frame) {
			this_->onFrameDecoded(frame);
		}
		virtual void onAudioPacket(AVPacket& packet) {
			this_->onAudioPacket(packet);
		}
	private:
		AMTSimpleVideoEncoder* this_;
	};

	class SpDataPumpThread : public DataPumpThread<std::unique_ptr<av::Frame>> {
	public:
		SpDataPumpThread(AMTSimpleVideoEncoder* this_, int bufferingFrames)
			: DataPumpThread(bufferingFrames)
			, this_(this_)
		{ }
	protected:
		virtual void OnDataReceived(std::unique_ptr<av::Frame>&& data) {
			this_->onFrameReceived(std::move(data));
		}
	private:
		AMTSimpleVideoEncoder* this_;
	};

	class AudioFileWriter : public av::AudioWriter {
	public:
		AudioFileWriter(AVStream* stream, const std::string& filename, int bufsize)
			: AudioWriter(stream, bufsize)
			, file_(filename, "wb")
		{ }
	protected:
		virtual void onWrite(MemoryChunk mc) {
			file_.write(mc);
		}
	private:
		File file_;
	};

	const ConfigWrapper& setting_;
	SpVideoReader reader_;
	av::EncodeWriter* encoder_;
	SpDataPumpThread thread_;

	int audioCount_;
	std::vector<std::unique_ptr<AudioFileWriter>> audioFiles_;
	std::vector<int> audioMap_;

	int64_t srcFileSize_;
	VideoFormat videoFormat_;
	RFFExtractor rffExtractor_;

	int pass_;

	void onFileOpen(AVFormatContext *fmt)
	{
		audioMap_ = std::vector<int>(fmt->nb_streams, -1);
		if (pass_ <= 1) { // 2パス目は出力しない
			audioCount_ = 0;
			for (int i = 0; i < (int)fmt->nb_streams; ++i) {
				if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
					audioFiles_.emplace_back(new AudioFileWriter(
						fmt->streams[i], setting_.getIntAudioFilePath(0, 0, audioCount_, CMTYPE_BOTH), 8 * 1024));
					audioMap_[i] = audioCount_++;
				}
			}
		}
	}

	void processAllData(int pass)
	{
		pass_ = pass;

		encoder_ = new av::EncodeWriter();

		// エンコードスレッド開始
		thread_.start();

		// エンコード
		reader_.readAll(setting_.getSrcFilePath(), setting_.getDecoderSetting());

		// エンコードスレッドを終了して自分に引き継ぐ
		thread_.join();

		// 残ったフレームを処理
		encoder_->finish();

		if (pass_ <= 1) { // 2パス目は出力しない
			for (int i = 0; i < audioCount_; ++i) {
				audioFiles_[i]->flush();
			}
			audioFiles_.clear();
		}

		rffExtractor_.clear();
		audioMap_.clear();
		delete encoder_; encoder_ = NULL;
	}

	void onVideoFormat(AVStream *stream, VideoFormat fmt)
	{
		videoFormat_ = fmt;

		// ビットレート計算
		File file(setting_.getSrcFilePath(), "rb");
		srcFileSize_ = file.size();
		double srcBitrate = ((double)srcFileSize_ * 8 / 1000) / (stream->duration * av_q2d(stream->time_base));
		ctx.info("入力映像ビットレート: %d kbps", (int)srcBitrate);

		if (setting_.isAutoBitrate()) {
			ctx.info("目標映像ビットレート: %d kbps",
				(int)setting_.getBitrate().getTargetBitrate(fmt.format, srcBitrate));
		}

		// 初期化
		std::string args = makeEncoderArgs(
			setting_.getEncoder(),
			setting_.getEncoderPath(),
			setting_.getOptions(
				fmt.format, srcBitrate, false, pass_, std::vector<EncoderZone>(), 0, 0, CMTYPE_BOTH),
			fmt,
			setting_.getEncVideoFilePath(0, 0, CMTYPE_BOTH));

		ctx.info("[エンコーダ開始]");
		ctx.info(args.c_str());

		// x265でインタレースの場合はフィールドモード
		bool dstFieldMode =
			(setting_.getEncoder() == ENCODER_X265 && fmt.progressive == false);

		int bufsize = fmt.width * fmt.height * 3;
		encoder_->start(args, fmt, dstFieldMode, bufsize);
	}

	void onFrameDecoded(av::Frame& frame__) {
		// フレームをコピーしてスレッドに渡す
		thread_.put(std::unique_ptr<av::Frame>(new av::Frame(frame__)), 1);
	}

	void onFrameReceived(std::unique_ptr<av::Frame>&& frame)
	{
		// RFFフラグ処理
		// PTSはinputFrameで再定義されるので修正しないでそのまま渡す
		PICTURE_TYPE pic = getPictureTypeFromAVFrame((*frame)());
		//fprintf(stderr, "%s\n", PictureTypeString(pic));
		rffExtractor_.inputFrame(*encoder_, std::move(frame), pic);

		//encoder_.inputFrame(*frame);
	}

	void onAudioPacket(AVPacket& packet)
	{
		if (pass_ <= 1) { // 2パス目は出力しない
			int audioIdx = audioMap_[packet.stream_index];
			if (audioIdx >= 0) {
				audioFiles_[audioIdx]->inputFrame(packet);
			}
		}
	}
};

class AMTMuxder : public AMTObject {
public:
	AMTMuxder(
		AMTContext&ctx,
		const ConfigWrapper& setting,
		const StreamReformInfo& reformInfo)
		: AMTObject(ctx)
		, setting_(setting)
		, reformInfo_(reformInfo)
		, audioCache_(ctx, setting.getAudioFilePath(), reformInfo.getAudioFileOffsets(), 12, 4)
	{ }

	int64_t mux(int videoFileIndex, int encoderIndex, CMType cmtype,
		const VideoFormat& fvfmt, // フィルタ出力フォーマット
		const EncoderOptionInfo& eoInfo, // エンコーダオプション情報
		const std::string& outFilePath)
	{
		auto fmt = reformInfo_.getFormat(encoderIndex, videoFileIndex);
		auto vfmt = fvfmt;

		if (vfmt.progressive == false) {
			// エンコーダでインタレ解除している場合があるので、それを反映する
			switch (eoInfo.deint) {
			case ENCODER_DEINT_24P:
				vfmt.mulDivFps(4, 5);
				vfmt.progressive = true;
				break;
			case ENCODER_DEINT_30P:
			case ENCODER_DEINT_VFR:
				vfmt.progressive = true;
				break;
			case ENCODER_DEINT_60P:
				vfmt.mulDivFps(2, 1);
				vfmt.progressive = true;
				break;
			}
		}
		else {
			if (eoInfo.deint != ENCODER_DEINT_NONE) {
				// 一応警告を出す
				ctx.warn("エンコーダへの入力はプログレッシブですが、"
					"エンコーダオプションでインタレ解除指定がされています。");
				ctx.warn("エンコーダでこのオプションが無視される場合は問題ありません。");
			}
		}

		// 音声ファイルを作成
		std::vector<std::string> audioFiles;
		const FileAudioFrameList& fileFrameList =
			reformInfo_.getFileAudioFrameList(encoderIndex, videoFileIndex, cmtype);
		for (int asrc = 0, adst = 0; asrc < (int)fileFrameList.size(); ++asrc) {
			const std::vector<int>& frameList = fileFrameList[asrc];
			if (frameList.size() > 0) {
				if (fmt.audioFormat[asrc].channels == AUDIO_2LANG) {
					// デュアルモノは2つのAACに分離
					ctx.info("音声%d-%dはデュアルモノなので2つのAACファイルに分離します", encoderIndex, asrc);
					SpDualMonoSplitter splitter(ctx);
					std::string filepath0 = setting_.getIntAudioFilePath(videoFileIndex, encoderIndex, adst++, cmtype);
					std::string filepath1 = setting_.getIntAudioFilePath(videoFileIndex, encoderIndex, adst++, cmtype);
					splitter.open(0, filepath0);
					splitter.open(1, filepath1);
					for (int frameIndex : frameList) {
						splitter.inputPacket(audioCache_[frameIndex]);
					}
					audioFiles.push_back(filepath0);
					audioFiles.push_back(filepath1);
				}
				else {
					std::string filepath = setting_.getIntAudioFilePath(videoFileIndex, encoderIndex, adst++, cmtype);
					File file(filepath, "wb");
					for (int frameIndex : frameList) {
						file.write(audioCache_[frameIndex]);
					}
					audioFiles.push_back(filepath);
				}
			}
		}

		// 映像ファイル
		std::string encVideoFile;
		encVideoFile = setting_.getEncVideoFilePath(videoFileIndex, encoderIndex, cmtype);

		// チャプターファイル
		std::string chapterFile;
		if (setting_.isChapterEnabled()) {
			auto path = setting_.getTmpChapterPath(videoFileIndex, encoderIndex, cmtype);
			if (File::exists(path)) {
				chapterFile = path;
			}
		}

		// 字幕ファイル
		std::vector<std::string> subsFiles;
		std::vector<std::string> subsTitles;
		auto& capList = reformInfo_.getOutCaptionList(encoderIndex, videoFileIndex, (CMType)cmtype);
		for (int lang = 0; lang < capList.size(); ++lang) {
			if (setting_.getFormat() == FORMAT_MKV) {
				subsFiles.push_back(setting_.getTmpASSFilePath(
					videoFileIndex, encoderIndex, lang, (CMType)cmtype));
				subsTitles.push_back("ASS");
			}
			subsFiles.push_back(setting_.getTmpSRTFilePath(
				videoFileIndex, encoderIndex, lang, (CMType)cmtype));
			subsTitles.push_back("SRT");
		}

		// タイムコードファイル
		std::string timecodeFile;
		std::pair<int, int> timebase;
		if (eoInfo.afsTimecode) {
			timecodeFile = setting_.getTimecodeFilePath(videoFileIndex, encoderIndex, cmtype);
			// 自動フィールドシフトは120fpsタイミングで出力するので4倍する
			timebase = std::make_pair(vfmt.frameRateNum * 4, vfmt.frameRateDenom);
		}

		std::string tmpOutPath = setting_.getVfrTmpFilePath(videoFileIndex, encoderIndex, cmtype);

		auto args = makeMuxerArgs(
			setting_.getFormat(),
			setting_.getMuxerPath(), setting_.getTimelineEditorPath(), setting_.getMp4BoxPath(),
			encVideoFile, vfmt, audioFiles, 
			outFilePath, tmpOutPath, chapterFile,
			timecodeFile, timebase, subsFiles, subsTitles);

		for (int i = 0; i < (int)args.size(); ++i) {
			ctx.info(args[i].c_str());
			MySubProcess muxer(args[i]);
			int ret = muxer.join();
			if (ret != 0) {
				THROWF(RuntimeException, "mux failed (exit code: %d)", ret);
			}
			// mp4boxがコンソール出力のコードページを変えてしまうので戻す
			ctx.setDefaultCP();
		}

		File outfile(outFilePath, "rb");
		return outfile.size();
	}

private:
	class MySubProcess : public EventBaseSubProcess {
	public:
		MySubProcess(const std::string& args) : EventBaseSubProcess(args) { }
	protected:
		virtual void onOut(bool isErr, MemoryChunk mc) {
			// これはマルチスレッドで呼ばれるの注意
			fwrite(mc.data, mc.length, 1, isErr ? stderr : stdout);
			fflush(isErr ? stderr : stdout);
		}
	};

	class SpDualMonoSplitter : public DualMonoSplitter
	{
		std::unique_ptr<File> file[2];
	public:
		SpDualMonoSplitter(AMTContext& ctx) : DualMonoSplitter(ctx) { }
		void open(int index, const std::string& filename) {
			file[index] = std::unique_ptr<File>(new File(filename, "wb"));
		}
		virtual void OnOutFrame(int index, MemoryChunk mc) {
			file[index]->write(mc);
		}
	};

	const ConfigWrapper& setting_;
	const StreamReformInfo& reformInfo_;

	PacketCache audioCache_;
};

class AMTSimpleMuxder : public AMTObject {
public:
	AMTSimpleMuxder(
		AMTContext&ctx,
		const ConfigWrapper& setting)
		: AMTObject(ctx)
		, setting_(setting)
		, totalOutSize_(0)
	{ }

	void mux(VideoFormat videoFormat, int audioCount) {
			// Mux
		std::vector<std::string> audioFiles;
		for (int i = 0; i < audioCount; ++i) {
			audioFiles.push_back(setting_.getIntAudioFilePath(0, 0, i, CMTYPE_BOTH));
		}
		std::string encVideoFile = setting_.getEncVideoFilePath(0, 0, CMTYPE_BOTH);
		std::string outFilePath = setting_.getOutFilePath(0, CMTYPE_BOTH);
		auto args = makeMuxerArgs(
			FORMAT_MP4,
			setting_.getMuxerPath(), setting_.getTimelineEditorPath(), setting_.getMp4BoxPath(),
			encVideoFile, videoFormat, audioFiles, outFilePath, 
			std::string(), std::string(), std::string(), std::pair<int, int>(), 
			std::vector<std::string>(), std::vector<std::string>());
		ctx.info("[Mux開始]");
		ctx.info(args[0].c_str());

		{
			MySubProcess muxer(args[0]);
			int ret = muxer.join();
			if (ret != 0) {
				THROWF(RuntimeException, "mux failed (muxer exit code: %d)", ret);
			}
		}

		{ // 出力サイズ取得
			File outfile(setting_.getOutFilePath(0, CMTYPE_BOTH), "rb");
			totalOutSize_ += outfile.size();
		}
	}

	int64_t getTotalOutSize() const {
		return totalOutSize_;
	}

private:
	class MySubProcess : public EventBaseSubProcess {
	public:
		MySubProcess(const std::string& args) : EventBaseSubProcess(args) { }
	protected:
		virtual void onOut(bool isErr, MemoryChunk mc) {
			// これはマルチスレッドで呼ばれるの注意
			fwrite(mc.data, mc.length, 1, isErr ? stderr : stdout);
			fflush(isErr ? stderr : stdout);
		}
	};

	const ConfigWrapper& setting_;
	int64_t totalOutSize_;
};

static void transcodeMain(AMTContext& ctx, const ConfigWrapper& setting)
{
	setting.dump();

	auto eoInfo = ParseEncoderOption(setting.getEncoder(), setting.getEncoderOptions());
	PrintEncoderInfo(ctx, eoInfo);

	Stopwatch sw;
	sw.start();
	auto splitter = std::unique_ptr<AMTSplitter>(new AMTSplitter(ctx, setting));
	if (setting.getServiceId() > 0) {
		splitter->setServiceId(setting.getServiceId());
	}
	StreamReformInfo reformInfo = splitter->split();
	ctx.info("TS解析完了: %.2f秒", sw.getAndReset());
	int64_t numTotalPackets = splitter->getNumTotalPackets();
	int64_t numScramblePackets = splitter->getNumScramblePackets();
	int64_t totalIntVideoSize = splitter->getTotalIntVideoSize();
	int64_t srcFileSize = splitter->getSrcFileSize();
	splitter = nullptr;

	if (setting.isDumpStreamInfo()) {
		reformInfo.serialize(setting.getStreamInfoPath());
	}

	// スクランブルパケットチェック
	double scrambleRatio = (double)numScramblePackets / (double)numTotalPackets;
	if (scrambleRatio > 0.01) {
		ctx.error("%.2f%%のパケットがスクランブル状態です。", scrambleRatio * 100);
		if (scrambleRatio > 0.3) {
			THROW(FormatException, "スクランブルパケットが多すぎます");
		}
	}

	if(setting.isIgnoreNoDrcsMap() == false) {
		// DRCSマッピングチェック
		auto& counter = ctx.getCounter();
		auto it = counter.find("drcsnomap");
		if (it != counter.end() && it->second > 0) {
			THROW(NoDrcsMapException, "マッピングにないDRCS外字あり正常に字幕処理できなかったため終了します");
		}
	}

	reformInfo.prepare(setting.isSplitSub());

	int numVideoFiles = reformInfo.getNumVideoFile();
	int numOutFiles = reformInfo.getNumOutFiles();
	std::vector<std::unique_ptr<CMAnalyze>> cmanalyze;

	// ロゴ・CM解析
	sw.start();
	for (int videoFileIndex = 0; videoFileIndex < numVideoFiles; ++videoFileIndex) {
		if (setting.isChapterEnabled()) {
			// ファイル読み込み情報を保存
			auto& fmt = reformInfo.getFormat(0, videoFileIndex);
			auto amtsPath = setting.getTmpAMTSourcePath(videoFileIndex);
			av::SaveAMTSource(amtsPath,
				setting.getIntVideoFilePath(videoFileIndex),
				setting.getWaveFilePath(),
				fmt.videoFormat, fmt.audioFormat[0],
				reformInfo.getFilterSourceFrames(videoFileIndex),
				reformInfo.getFilterSourceAudioFrames(videoFileIndex),
				setting.getDecoderSetting());

			int numFrames = (int)reformInfo.getFilterSourceFrames(videoFileIndex).size();
			cmanalyze.emplace_back(std::unique_ptr<CMAnalyze>(
				new CMAnalyze(ctx, setting, videoFileIndex, numFrames)));

			reformInfo.applyCMZones(videoFileIndex, cmanalyze.back()->getZones());

			// チャプター推定
			ctx.info("[チャプター生成]");
			MakeChapter makechapter(ctx, setting, reformInfo, videoFileIndex);
			int numEncoders = reformInfo.getNumEncoders(videoFileIndex);
			for (int i = 0; i < numEncoders; ++i) {
				for (CMType cmtype : setting.getCMTypes()) {
					makechapter.exec(videoFileIndex, i, cmtype);
				}
			}
		}
		else {
			// チャプターCM解析無効
			cmanalyze.emplace_back(std::unique_ptr<CMAnalyze>(new CMAnalyze(ctx, setting)));
		}
	}
	if (setting.isChapterEnabled()) {
		ctx.info("ロゴ・CM解析完了: %.2f秒", sw.getAndReset());
	}

	auto audioDiffInfo = reformInfo.genAudio();
	audioDiffInfo.printAudioPtsDiff(ctx);

	// 字幕ファイルを生成
	for (int videoFileIndex = 0, currentEncoderFile = 0;
		videoFileIndex < numVideoFiles; ++videoFileIndex) {
		CaptionASSFormatter formatterASS(ctx);
		CaptionSRTFormatter formatterSRT(ctx);
		int numEncoders = reformInfo.getNumEncoders(videoFileIndex);
		for (int encoderIndex = 0; encoderIndex < numEncoders; ++encoderIndex, ++currentEncoderFile) {
			for (CMType cmtype : setting.getCMTypes()) {
				auto& capList = reformInfo.getOutCaptionList(encoderIndex, videoFileIndex, (CMType)cmtype);
				for (int lang = 0; lang < capList.size(); ++lang) {
					WriteUTF8File(
						setting.getTmpASSFilePath(videoFileIndex, encoderIndex, lang, (CMType)cmtype),
						formatterASS.generate(capList[lang]));
					WriteUTF8File(
						setting.getTmpSRTFilePath(videoFileIndex, encoderIndex, lang, (CMType)cmtype),
						formatterSRT.generate(capList[lang]));
				}
			}
		}
	}

	auto argGen = std::unique_ptr<EncoderArgumentGenerator>(new EncoderArgumentGenerator(setting, reformInfo));
	std::vector<EncodeFileInfo> outFileInfo;
	std::vector<VideoFormat> outfiles;

	sw.start();
	for (int videoFileIndex = 0, currentEncoderFile = 0;
		videoFileIndex < numVideoFiles; ++videoFileIndex) {
		int numEncoders = reformInfo.getNumEncoders(videoFileIndex);
		if (numEncoders == 0) {
			ctx.warn("numEncoders == 0 ...");
		}
		else {
			const CMAnalyze* cma = cmanalyze[videoFileIndex].get();

			for (int encoderIndex = 0; encoderIndex < numEncoders; ++encoderIndex, ++currentEncoderFile) {
				for (CMType cmtype : setting.getCMTypes()) {

					// 出力が1秒以下ならスキップ
					if (reformInfo.getFileDuration(encoderIndex, videoFileIndex, cmtype) < MPEG_CLOCK_HZ)
						continue;

					AMTFilterSource filterSource(ctx, setting, reformInfo,
						cma->getZones(), cma->getLogoPath(), videoFileIndex, encoderIndex, cmtype);

					try {
						PClip filterClip = filterSource.getClip();
						IScriptEnvironment2* env = filterSource.getEnv();
						auto& encoderZones = filterSource.getZones();
						auto& outfmt = filterSource.getFormat();
						auto& outvi = filterClip->GetVideoInfo();

						std::vector<int> pass;
						if (setting.isTwoPass()) {
							pass.push_back(1);
							pass.push_back(2);
						}
						else {
							pass.push_back(-1);
						}

						ctx.info("[エンコード開始] %d/%d %s", currentEncoderFile + 1, numOutFiles, CMTypeToString(cmtype));

						outFileInfo.push_back(argGen->printBitrate(ctx, videoFileIndex, cmtype));
						outfiles.push_back(outfmt);

						std::vector<std::string> encoderArgs;
						for (int i = 0; i < (int)pass.size(); ++i) {
							encoderArgs.push_back(
								argGen->GenEncoderOptions(
									outfmt, encoderZones, videoFileIndex, encoderIndex, cmtype, pass[i]));
						}
						AMTFilterVideoEncoder encoder(ctx);
						encoder.encode(filterClip, outfmt, encoderArgs, env);
					}
					catch (const AvisynthError& avserror) {
						THROWF(AviSynthException, "%s", avserror.msg);
					}
				}
			}
		}
	}
	ctx.info("エンコード完了: %.2f秒", sw.getAndReset());

	argGen = nullptr;

	sw.start();
	int64_t totalOutSize = 0;
	auto muxer = std::unique_ptr<AMTMuxder>(new AMTMuxder(ctx, setting, reformInfo));
	for (int videoFileIndex = 0, currentOutFile = 0;
		videoFileIndex < numVideoFiles; ++videoFileIndex)
	{
		int numEncoders = reformInfo.getNumEncoders(videoFileIndex);
		auto& cmtypes = setting.getCMTypes();
		auto& outFileMapping = reformInfo.getOutFileMapping();

		for (int encoderIndex = 0; encoderIndex < numEncoders; ++encoderIndex) {
			int outIndex = reformInfo.getOutFileIndex(encoderIndex, videoFileIndex);

			for (int i = 0; i < (int)cmtypes.size(); ++i) {
				CMType cmtype = cmtypes[i];

				// 出力が1秒以下ならスキップ
				if (reformInfo.getFileDuration(encoderIndex, videoFileIndex, cmtype) < MPEG_CLOCK_HZ)
					continue;

				auto& info = outFileInfo[currentOutFile++];
				info.outPath = setting.getOutFilePath(
					outFileMapping[outIndex], (i == 0) ? CMTYPE_BOTH : cmtype);

				ctx.info("[Mux開始] %d/%d %s", outIndex + 1, reformInfo.getNumOutFiles(), CMTypeToString(cmtype));
				info.fileSize = muxer->mux(
					videoFileIndex, encoderIndex, cmtype,
					outfiles[outIndex], eoInfo, info.outPath);

				totalOutSize += info.fileSize;
			}
		}
	}
	ctx.info("Mux完了: %.2f秒", sw.getAndReset());

	muxer = nullptr;

	// 出力結果を表示
	reformInfo.printOutputMapping([&](int index) { return setting.getOutFilePath(index, CMTYPE_BOTH); });

	// 出力結果JSON出力
	if (setting.getOutInfoJsonPath().size() > 0) {
		StringBuilder sb;
		sb.append("{ ")
			.append("\"srcpath\": \"%s\", ", toJsonString(setting.getSrcFilePath()))
			.append("\"outfiles\": [");
		for (int i = 0; i < (int)outFileInfo.size(); ++i) {
			if (i > 0) sb.append(", ");
			const auto& info = outFileInfo[i];
			sb.append("{ \"path\": \"%s\", \"srcbitrate\": %d, \"outbitrate\": %d, \"outfilesize\": %lld }",
				toJsonString(info.outPath), (int)info.srcBitrate, (int)info.targetBitrate, info.fileSize);
		}
		sb.append("]")
			.append(", \"logofiles\": [");
		for (int i = 0; i < reformInfo.getNumVideoFile(); ++i) {
			if (i > 0) sb.append(", ");
			sb.append("\"%s\"", toJsonString(cmanalyze[i]->getLogoPath()));
		}
		sb.append("]")
			.append(", \"srcfilesize\": %lld, \"intvideofilesize\": %lld, \"outfilesize\": %lld",
				srcFileSize, totalIntVideoSize, totalOutSize);
		auto duration = reformInfo.getInOutDuration();
		sb.append(", \"srcduration\": %.3f, \"outduration\": %.3f",
			(double)duration.first / MPEG_CLOCK_HZ, (double)duration.second / MPEG_CLOCK_HZ);
		sb.append(", \"audiodiff\": ");
		audioDiffInfo.printToJson(sb);
		for (const auto& pair : ctx.getCounter()) {
			sb.append(", \"%s\": %d", pair.first, pair.second);
		}
		sb.append(", \"cmanalyze\": %s", (setting.isChapterEnabled() ? "true" : "false"))
			.append(" }");

		std::string str = sb.str();
		MemoryChunk mc(reinterpret_cast<uint8_t*>(const_cast<char*>(str.data())), str.size());
		File file(setting.getOutInfoJsonPath(), "w");
		file.write(mc);
	}
}

static void transcodeSimpleMain(AMTContext& ctx, const ConfigWrapper& setting)
{
	if (ends_with(setting.getSrcFilePath(), ".ts")) {
		ctx.warn("一般ファイルモードでのTSファイルの処理は非推奨です");
	}

	auto encoder = std::unique_ptr<AMTSimpleVideoEncoder>(new AMTSimpleVideoEncoder(ctx, setting));
	encoder->encode();
	int audioCount = encoder->getAudioCount();
	int64_t srcFileSize = encoder->getSrcFileSize();
	VideoFormat videoFormat = encoder->getVideoFormat();
	encoder = nullptr;

	auto muxer = std::unique_ptr<AMTSimpleMuxder>(new AMTSimpleMuxder(ctx, setting));
	muxer->mux(videoFormat, audioCount);
	int64_t totalOutSize = muxer->getTotalOutSize();
	muxer = nullptr;

	// 出力結果を表示
	ctx.info("完了");
	if (setting.getOutInfoJsonPath().size() > 0) {
		StringBuilder sb;
		sb.append("{ \"srcpath\": \"%s\"", toJsonString(setting.getSrcFilePath()))
			.append(", \"outpath\": \"%s\"", toJsonString(setting.getOutFilePath(0, CMTYPE_BOTH)))
			.append(", \"srcfilesize\": %lld", srcFileSize)
			.append(", \"outfilesize\": %lld", totalOutSize)
			.append(" }");

		std::string str = sb.str();
		MemoryChunk mc(reinterpret_cast<uint8_t*>(const_cast<char*>(str.data())), str.size());
		File file(setting.getOutInfoJsonPath(), "w");
		file.write(mc);
	}
}
