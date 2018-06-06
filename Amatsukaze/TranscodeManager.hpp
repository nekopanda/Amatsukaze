/**
* Transcode manager
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
#include <memory>
#include <limits>
#include <smmintrin.h>

#include "TsSplitter.hpp"
#include "Encoder.hpp"
#include "Muxer.hpp"
#include "StreamReform.hpp"
#include "LogoScan.hpp"
#include "CMAnalyze.hpp"
#include "InterProcessComm.hpp"
#include "CaptionData.hpp"
#include "CaptionFormatter.hpp"
#include "EncoderOptionParser.hpp"
#include "NicoJK.hpp"

class AMTSplitter : public TsSplitter {
public:
	AMTSplitter(AMTContext& ctx, const ConfigWrapper& setting)
		: TsSplitter(ctx, true, true, setting.isSubtitlesEnabled())
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
			videoFrameList_, audioFrameList_, captionTextList_, streamEventList_, timeList_);
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
	std::vector<std::pair<int64_t, JSTTime>> timeList_;

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
		double sec = (double)totalTime / MPEG_CLOCK_HZ;
		int minutes = (int)(sec / 60);
		sec -= minutes * 60;
		ctx.info("時間: %d分%.3f秒", minutes, sec);

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

    StringBuilder sb;
    sb.append("サイズ: %dx%d", fmt.width, fmt.height);
    if (fmt.width != fmt.displayWidth || fmt.height != fmt.displayHeight) {
      sb.append(" 表示領域: %dx%d", fmt.displayWidth, fmt.displayHeight);
    }
    int darW, darH; fmt.getDAR(darW, darH);
    sb.append(" (%d:%d)", darW, darH);
		if (fmt.fixedFrameRate) {
      sb.append(" FPS: %d/%d", fmt.frameRateNum, fmt.frameRateDenom);
		}
		else {
      sb.append(" FPS: VFR");
		}
    ctx.info(sb.str().c_str());

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

	virtual DRCSOutInfo getDRCSOutPath(int64_t PTS, const std::string& md5) {
		DRCSOutInfo info;
		info.elapsed = (videoFrameList_.size() > 0) ? (double)(PTS - videoFrameList_[0].PTS) : -1.0;
		info.filename = setting_.getDRCSOutPath(md5);
		return info;
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

	virtual void onTime(int64_t clock, JSTTime time) {
		timeList_.push_back(std::make_pair(clock, time));
	}
};

class DrcsSearchSplitter : public TsSplitter {
public:
	DrcsSearchSplitter(AMTContext& ctx, const ConfigWrapper& setting)
		: TsSplitter(ctx, true, false, true)
		, setting_(setting)
	{ }

	void readAll()
	{
		enum { BUFSIZE = 4 * 1024 * 1024 };
		auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
		MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
		File srcfile(setting_.getSrcFilePath(), "rb");
		size_t readBytes;
		do {
			readBytes = srcfile.read(buffer);
			inputTsData(MemoryChunk(buffer.data, readBytes));
		} while (readBytes == buffer.length);
	}

protected:
	const ConfigWrapper& setting_;
  std::vector<VideoFrameInfo> videoFrameList_;

	// TsSplitter仮想関数 //

	virtual void onVideoPesPacket(
		int64_t clock,
		const std::vector<VideoFrameInfo>& frames,
		PESPacket packet)
	{
    // 今の所最初のフレームしか必要ないけど
    for (const VideoFrameInfo& frame : frames) {
      videoFrameList_.push_back(frame);
    }
  }

	virtual void onVideoFormatChanged(VideoFormat fmt) { }

	virtual void onAudioPesPacket(
		int audioIdx,
		int64_t clock,
		const std::vector<AudioFrameData>& frames,
		PESPacket packet)
	{ }

	virtual void onAudioFormatChanged(int audioIdx, AudioFormat fmt) { }

	virtual void onCaptionPesPacket(
		int64_t clock,
		std::vector<CaptionItem>& captions,
		PESPacket packet)
	{ }

	virtual DRCSOutInfo getDRCSOutPath(int64_t PTS, const std::string& md5) {
		DRCSOutInfo info;
    info.elapsed = (videoFrameList_.size() > 0) ? (double)(PTS - videoFrameList_[0].PTS) : -1.0;
		info.filename = setting_.getDRCSOutPath(md5);
		return info;
	}

	virtual void onTime(int64_t clock, JSTTime time) {
		//
	}
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
		std::vector<BitrateZone> zones,
    double vfrBitrateScale,
    std::string timecodepath,
    bool is120fps,
		int videoFileIndex, int encoderIndex, CMType cmtype, int pass)
	{
		VIDEO_STREAM_FORMAT srcFormat = reformInfo_.getVideoStreamFormat();
		double srcBitrate = getSourceBitrate(videoFileIndex);
		return makeEncoderArgs(
			setting_.getEncoder(),
			setting_.getEncoderPath(),
			setting_.getOptions(
				srcFormat, srcBitrate, false, pass, zones, vfrBitrateScale,
				videoFileIndex, encoderIndex, cmtype),
			outfmt,
      timecodepath,
      is120fps,
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

static std::vector<BitrateZone> MakeBitrateZones(
  const std::vector<int>& frameDurations,
  const std::vector<EncoderZone>& cmzones,
  const ConfigWrapper& setting,
  VideoInfo outvi)
{
  std::vector<BitrateZone> bitrateZones;
  if (frameDurations.size() == 0 || setting.isEncoderSupportVFR()) {
    // VFRでない、または、エンコーダがVFRをサポートしている -> VFR用に調整する必要がない
    for (int i = 0; i < (int)cmzones.size(); ++i) {
      bitrateZones.emplace_back(cmzones[i], setting.getBitrateCM());
    }
  }
  else {
    if (setting.isZoneAvailable()) {
      // VFR非対応エンコーダでゾーンに対応していればビットレートゾーン生成
      return MakeVFRBitrateZones(
        frameDurations, cmzones, setting.getBitrateCM(),
        outvi.fps_numerator, outvi.fps_denominator,
        0.05); // 全体で5%までの差なら許容する
    }
  }
  return bitrateZones;
}

static void transcodeMain(AMTContext& ctx, const ConfigWrapper& setting)
{
	setting.dump();

  bool isNoEncode = (setting.getMode() == "cm");

	auto eoInfo = ParseEncoderOption(setting.getEncoder(), setting.getEncoderOptions());
	PrintEncoderInfo(ctx, eoInfo);

	// チェック
	if (!isNoEncode && setting.getFormat() == FORMAT_M2TS && eoInfo.afsTimecode) {
		THROW(FormatException, "M2TS出力はVFRをサポートしていません");
	}

	Stopwatch sw;
	sw.start();
	auto splitter = std::unique_ptr<AMTSplitter>(new AMTSplitter(ctx, setting));
	if (setting.getServiceId() > 0) {
		splitter->setServiceId(setting.getServiceId());
	}
	StreamReformInfo reformInfo = splitter->split();
	ctx.info("TS解析完了: %.2f秒", sw.getAndReset());
	int serviceId = splitter->getActualServiceId();
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

	if(!isNoEncode && setting.isIgnoreNoDrcsMap() == false) {
		// DRCSマッピングチェック
		if (ctx.getErrorCount(AMT_ERR_NO_DRCS_MAP) > 0) {
			THROW(NoDrcsMapException, "マッピングにないDRCS外字あり正常に字幕処理できなかったため終了します");
		}
	}

	reformInfo.prepare(setting.isSplitSub());

	time_t startTime = reformInfo.getFirstFrameTime();

	NicoJK nicoJK(ctx, setting);
	bool nicoOK = false;
	if (!isNoEncode && setting.isNicoJKEnabled()) {
		ctx.info("[ニコニコ実況コメント取得]");
		auto srcDuration = reformInfo.getInDuration() / MPEG_CLOCK_HZ;
		nicoOK = nicoJK.makeASS(serviceId, startTime, (int)srcDuration);
		if (nicoOK) {
			reformInfo.SetNicoJKList(nicoJK.getDialogues());
		}
		else {
			if (nicoJK.isFail() == false) {
				ctx.info("対応チャンネルがありません");
			}
			else if (setting.isIgnoreNicoJKError() == false) {
				THROW(RuntimeException, "ニコニコ実況コメント取得に失敗");
			}
		}
	}

	int numVideoFiles = reformInfo.getNumVideoFile();
	int numOutFiles = reformInfo.getNumOutFiles();
	std::vector<std::unique_ptr<CMAnalyze>> cmanalyze;

	// ロゴ・CM解析
	sw.start();
	for (int videoFileIndex = 0; videoFileIndex < numVideoFiles; ++videoFileIndex) {
		// チャプター解析は300フレーム（約10秒）以上ある場合だけ
		//（短すぎるとエラーになることがあるので）
		if (setting.isChapterEnabled() &&
			reformInfo.getFilterSourceFrames(videoFileIndex).size() >= 300)
		{
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

  if (isNoEncode) {
    // CM解析のみならここで終了
    return;
  }

	auto audioDiffInfo = reformInfo.genAudio();
	audioDiffInfo.printAudioPtsDiff(ctx);

	ctx.info("[字幕ファイル生成]");
	for (int videoFileIndex = 0, currentEncoderFile = 0;
		videoFileIndex < numVideoFiles; ++videoFileIndex) {
		CaptionASSFormatter formatterASS(ctx);
		CaptionSRTFormatter formatterSRT(ctx);
		NicoJKFormatter formatterNicoJK(ctx);
		int numEncoders = reformInfo.getNumEncoders(videoFileIndex);
		for (int encoderIndex = 0; encoderIndex < numEncoders; ++encoderIndex, ++currentEncoderFile) {
			for (CMType cmtype : setting.getCMTypes()) {
				auto& capList = reformInfo.getOutCaptionList(encoderIndex, videoFileIndex, cmtype);
				for (int lang = 0; lang < capList.size(); ++lang) {
					WriteUTF8File(
						setting.getTmpASSFilePath(videoFileIndex, encoderIndex, lang, cmtype),
						formatterASS.generate(capList[lang]));
					WriteUTF8File(
						setting.getTmpSRTFilePath(videoFileIndex, encoderIndex, lang, cmtype),
						formatterSRT.generate(capList[lang]));
				}
				if (nicoOK) {
					const auto& headerLines = nicoJK.getHeaderLines();
					const auto& dialogues = reformInfo.getOutNicoJKList(encoderIndex, videoFileIndex, cmtype);
					for (NicoJKType jktype : setting.getNicoJKTypes()) {
						File file(setting.getTmpNicoJKASSPath(videoFileIndex, encoderIndex, cmtype, jktype), "w");
						auto text = formatterNicoJK.generate(headerLines[(int)jktype], dialogues[(int)jktype]);
						file.write(MemoryChunk((uint8_t*)text.data(), text.size()));
					}
				}
			}
		}
	}
	ctx.info("字幕ファイル生成完了: %.2f秒", sw.getAndReset());

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

          auto getTcPath = [&]() {
            return setting.getTimecodeFilePath(videoFileIndex, encoderIndex, cmtype);
          };

					try {
						PClip filterClip = filterSource.getClip();
						IScriptEnvironment2* env = filterSource.getEnv();
						auto encoderZones = filterSource.getZones();
						auto& outfmt = filterSource.getFormat();
						auto& outvi = filterClip->GetVideoInfo();
            auto& frameDurations = filterSource.getFrameDurations();
            FilterVFRProc vfrProc(ctx, frameDurations, outvi, setting.isVFR120fps());

						ctx.info("[エンコード開始] %d/%d %s", currentEncoderFile + 1, numOutFiles, CMTypeToString(cmtype));

						outFileInfo.push_back(argGen->printBitrate(ctx, videoFileIndex, cmtype));
						outfiles.push_back(outfmt);

            bool vfrEnabled = eoInfo.afsTimecode;

            if (vfrProc.isEnabled()) {
              // フィルタによるVFRが有効
              if (eoInfo.afsTimecode) {
                THROW(ArgumentException, "エンコーダとフィルタの両方でVFRタイムコードが出力されています。");
              }
              vfrEnabled = true;
              // ゾーンをVFRフレーム番号に修正
              vfrProc.toVFRZones(encoderZones);
              // タイムコード生成
              vfrProc.makeTimecode(getTcPath());
              outFileInfo.back().tcPath = getTcPath();
            }

            std::vector<int> pass;
            if (setting.isTwoPass()) {
              pass.push_back(1);
              pass.push_back(2);
            }
            else {
              pass.push_back(-1);
            }

            auto bitrateZones = MakeBitrateZones(frameDurations, encoderZones, setting, outvi);
            auto vfrBitrateScale = AdjustVFRBitrate(frameDurations);
            // VFRフレームタイミングが120fpsか
            bool is120fps = (eoInfo.afsTimecode || setting.isVFR120fps());
						std::vector<std::string> encoderArgs;
						for (int i = 0; i < (int)pass.size(); ++i) {
							encoderArgs.push_back(
								argGen->GenEncoderOptions(
									outfmt, bitrateZones, vfrBitrateScale, outFileInfo.back().tcPath, is120fps,
                  videoFileIndex, encoderIndex, cmtype, pass[i]));
						}
						AMTFilterVideoEncoder encoder(ctx);
						encoder.encode(filterClip, outfmt, 
              frameDurations, encoderArgs, env);
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

				auto& info = outFileInfo[currentOutFile];
				const auto& vfmt = outfiles[currentOutFile];
				++currentOutFile;

				ctx.info("[Mux開始] %d/%d %s", outIndex + 1, reformInfo.getNumOutFiles(), CMTypeToString(cmtype));
				muxer->mux(
					videoFileIndex, encoderIndex, cmtype,
					vfmt, eoInfo, OutPathGenerator(setting,
						outFileMapping[outIndex], (i == 0) ? CMTYPE_BOTH : cmtype), nicoOK, info);

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
			sb.append("{ \"path\": \"%s\", \"srcbitrate\": %d, \"outbitrate\": %d, \"outfilesize\": %lld, ",
				toJsonString(info.outPath), (int)info.srcBitrate, 
				std::isnan(info.targetBitrate) ? -1 : (int)info.targetBitrate, info.fileSize);
			sb.append("\"subs\": [");
			for (int s = 0; s < (int)info.outSubs.size(); ++s) {
				if (s > 0) sb.append(", ");
				sb.append("\"%s\"", toJsonString(info.outSubs[s]));
			}
			sb.append("] }");
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
      sb.append(", \"error\": {");
      for (int i = 0; i < AMT_ERR_MAX; ++i) {
         if (i > 0) sb.append(", ");
         sb.append("\"%s\": %d", AMT_ERROR_NAMES[i], ctx.getErrorCount((AMT_ERROR_COUNTER)i));
      }
      sb.append(" }");
		sb.append(", \"cmanalyze\": %s", (setting.isChapterEnabled() ? "true" : "false"))
			.append(", \"nicojk\": %s", (nicoOK ? "true" : "false"))
			.append(" }");

		std::string str = sb.str();
		MemoryChunk mc(reinterpret_cast<uint8_t*>(const_cast<char*>(str.data())), str.size());
		File file(setting.getOutInfoJsonPath(), "w");
		file.write(mc);
	}
}

static void searchDrcsMain(AMTContext& ctx, const ConfigWrapper& setting)
{
	Stopwatch sw;
	sw.start();
	auto splitter = std::unique_ptr<DrcsSearchSplitter>(new DrcsSearchSplitter(ctx, setting));
	if (setting.getServiceId() > 0) {
		splitter->setServiceId(setting.getServiceId());
	}
	splitter->readAll();
	ctx.info("完了: %.2f秒", sw.getAndReset());
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
