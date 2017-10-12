/**
* Transcode manager
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <memory>
#include <limits>
#include <direct.h>
#include <smmintrin.h>

#include "StreamUtils.hpp"
#include "TsSplitter.hpp"
#include "Transcode.hpp"
#include "StreamReform.hpp"
#include "PacketCache.hpp"
#include "AMTSource.hpp"

// カラースペース定義を使うため
#include "libavutil/pixfmt.h"

namespace av {

// カラースペース3セット
// x265は数値そのままでもOKだが、x264はhelpを見る限りstringでなければ
// ならないようなので変換を定義
// とりあえずARIB STD-B32 v3.7に書いてあるのだけ

// 3原色
static const char* getColorPrimStr(int color_prim) {
	switch (color_prim) {
	case AVCOL_PRI_BT709: return "bt709";
	case AVCOL_PRI_BT2020: return "bt2020";
	default:
		THROWF(FormatException,
			"Unsupported color primaries (%d)", color_prim);
	}
	return NULL;
}

// ガンマ
static const char* getTransferCharacteristicsStr(int transfer_characteritics) {
	switch (transfer_characteritics) {
	case AVCOL_TRC_BT709: return "bt709";
	case AVCOL_TRC_IEC61966_2_4: return "iec61966-2-4";
	case AVCOL_TRC_BT2020_10: return "bt2020-10";
	case AVCOL_TRC_SMPTEST2084: return "smpte-st-2084";
	case AVCOL_TRC_ARIB_STD_B67: return "arib-std-b67";
	default:
		THROWF(FormatException,
			"Unsupported color transfer characteritics (%d)", transfer_characteritics);
	}
	return NULL;
}

// 変換係数
static const char* getColorSpaceStr(int color_space) {
	switch (color_space) {
	case AVCOL_SPC_BT709: return "bt709";
	case AVCOL_SPC_BT2020_NCL: return "bt2020nc";
	default:
		THROWF(FormatException,
			"Unsupported color color space (%d)", color_space);
	}
	return NULL;
}

} // namespace av {

enum ENUM_ENCODER {
	ENCODER_X264,
	ENCODER_X265,
	ENCODER_QSVENC,
	ENCODER_NVENC,
};

struct BitrateSetting {
  double a, b;
  double h264;
  double h265;

  double getTargetBitrate(VIDEO_STREAM_FORMAT format, double srcBitrate) const {
    double base = a * srcBitrate + b;
    if (format == VS_H264) {
      return base * h264;
    }
    else if (format == VS_H265) {
      return base * h265;
    }
    return base;
  }
};

static const char* encoderToString(ENUM_ENCODER encoder) {
	switch (encoder) {
	case ENCODER_X264: return "x264";
	case ENCODER_X265: return "x265";
	case ENCODER_QSVENC: return "QSVEnc";
	case ENCODER_NVENC: return "NVEnc";
	}
	return "Unknown";
}

static std::string makeEncoderArgs(
	ENUM_ENCODER encoder,
	const std::string& binpath,
	const std::string& options,
	const VideoFormat& fmt,
	const std::string& outpath)
{
	std::ostringstream ss;

	ss << "\"" << binpath << "\"";

	// y4mヘッダにあるので必要ない
	//ss << " --fps " << fmt.frameRateNum << "/" << fmt.frameRateDenom;
	//ss << " --input-res " << fmt.width << "x" << fmt.height;
	//ss << " --sar " << fmt.sarWidth << ":" << fmt.sarHeight;

	if (fmt.colorPrimaries != AVCOL_PRI_UNSPECIFIED) {
		ss << " --colorprim " << av::getColorPrimStr(fmt.colorPrimaries);
	}
	if (fmt.transferCharacteristics != AVCOL_TRC_UNSPECIFIED) {
		ss << " --transfer " << av::getTransferCharacteristicsStr(fmt.transferCharacteristics);
	}
	if (fmt.colorSpace != AVCOL_TRC_UNSPECIFIED) {
		ss << " --colormatrix " << av::getColorSpaceStr(fmt.colorSpace);
	}

	// インターレース
	switch (encoder) {
	case ENCODER_X264:
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		ss << (fmt.progressive ? "" : " --tff");
		break;
	case ENCODER_X265:
		ss << (fmt.progressive ? " --no-interlace" : " --interlace tff");
		break;
	}

	ss << " " << options << " -o \"" << outpath << "\"";

	// 入力形式
	switch (encoder) {
	case ENCODER_X264:
		ss << " --demuxer y4m -";
		break;
	case ENCODER_X265:
		ss << " --y4m --input -";
		break;
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		ss << " --format raw --y4m -i -";
		break;
	}
	
	return ss.str();
}

static std::string makeMuxerArgs(
	const std::string& binpath,
	const std::string& inVideo,
	const VideoFormat& videoFormat,
	const std::vector<std::string>& inAudios,
	const std::string& outpath)
{
	std::ostringstream ss;

	ss << "\"" << binpath << "\"";
	if (videoFormat.fixedFrameRate) {
		ss << " -i \"" << inVideo << "?fps="
			 << videoFormat.frameRateNum << "/"
			 << videoFormat.frameRateDenom << "\"";
	}
	else {
		ss << " -i \"" << inVideo << "\"";
	}
	for (const auto& inAudio : inAudios) {
		ss << " -i \"" << inAudio << "\"";
	}
	ss << " --optimize-pd";
	ss << " -o \"" << outpath << "\"";

	return ss.str();
}

static std::string makeTimelineEditorArgs(
	const std::string& binpath,
	const std::string& inpath,
	const std::string& outpath,
	const std::string& timecodepath,
	std::pair<int, int> timebase)
{
	std::ostringstream ss;
	ss << "\"" << binpath << "\"";
	ss << " --track 1";
	ss << " --timecode \"" << timecodepath << "\"";
	ss << " --media-timescale " << timebase.first;
	ss << " --media-timebase " << timebase.second;
	ss << " \"" << inpath << "\"";
	ss << " \"" << outpath << "\"";
	return ss.str();
}

inline bool ends_with(std::string const & value, std::string const & ending)
{
	if (ending.size() > value.size()) return false;
	return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

enum AMT_CLI_MODE {
  AMT_CLI_TS,
  AMT_CLI_GENERIC,
};

class TempDirectory : AMTObject, NonCopyable
{
public:
	TempDirectory(AMTContext& ctx, const std::string& tmpdir)
		: AMTObject(ctx)
	{
		for (int code = (int)time(NULL) & 0xFFFFFF; code > 0; ++code) {
			auto path = genPath(tmpdir, code);
			if (_mkdir(path.c_str()) == 0) {
				path_ = path;
				break;
			}
			if (errno != EEXIST) {
				break;
			}
		}
		if (path_.size() == 0) {
			THROW(IOException, "一時ディレクトリ作成失敗");
		}
	}
	~TempDirectory() {
		// 一時ファイルを削除
		ctx.clearTmpFiles();
		// ディレクトリ削除
		if (_rmdir(path_.c_str()) != 0) {
			ctx.warn("一時ディレクトリ削除に失敗: ", path_.c_str());
		}
	}

	std::string path() const {
		return path_;
	}

private:
	std::string path_;

	std::string genPath(const std::string& base, int code)
	{
		std::ostringstream ss;
		ss << base << "/amt" << code;
		return ss.str();
	}
};

class TranscoderSetting : public AMTObject
{
public:
	TranscoderSetting(
			AMTContext& ctx,
			std::string workDir,
			std::string mode,
      std::string modeArgs,
			std::string srcFilePath,
			std::string outVideoPath,
			std::string outInfoJsonPath,
			std::string filterScriptPath,
			ENUM_ENCODER encoder,
			std::string encoderPath,
			std::string encoderOptions,
			std::string muxerPath,
			std::string timelineditorPath,
			bool twoPass,
			bool autoBitrate,
			bool pulldown,
			BitrateSetting bitrate,
			int serviceId,
			DECODER_TYPE mpeg2decoder,
			DECODER_TYPE h264decoder,
			bool dumpStreamInfo)
		: AMTObject(ctx)
		, tmpDir(ctx, workDir)
		, mode(mode)
    , modeArgs(modeArgs)
		, srcFilePath(srcFilePath)
		, outVideoPath(outVideoPath)
		, outInfoJsonPath(outInfoJsonPath)
    , filterScriptPath(filterScriptPath)
		, encoder(encoder)
		, encoderPath(encoderPath)
		, encoderOptions(encoderOptions)
		, muxerPath(muxerPath)
		, timelineditorPath(timelineditorPath)
		, twoPass(twoPass)
		, autoBitrate(autoBitrate)
		, pulldown(pulldown)
		, bitrate(bitrate)
		, serviceId(serviceId)
		, mpeg2decoder(mpeg2decoder)
		, h264decoder(h264decoder)
		, dumpStreamInfo(dumpStreamInfo)
	{
		//
	}

	std::string getMode() const {
		return mode;
	}

  std::string getModeArgs() const {
    return modeArgs;
  }

	std::string getSrcFilePath() const {
		return srcFilePath;
	}

	std::string getOutInfoJsonPath() const {
		return outInfoJsonPath;
	}

  std::string getFilterScriptPath() const {
    return filterScriptPath;
  }

	ENUM_ENCODER getEncoder() const {
		return encoder;
	}

	std::string getEncoderPath() const {
		return encoderPath;
	}

	std::string getMuxerPath() const {
		return muxerPath;
	}

	std::string getTimelineEditorPath() const {
		return timelineditorPath;
	}

	bool isTwoPass() const {
		return twoPass;
	}

	bool isAutoBitrate() const {
		return autoBitrate;
	}

	bool isPulldownEnabled() const {
		return pulldown;
	}

	BitrateSetting getBitrate() const {
		return bitrate;
	}

	int getServiceId() const {
		return serviceId;
	}

	DECODER_TYPE getMpeg2Decoder() const {
		return mpeg2decoder;
	}

	DECODER_TYPE getH264Decoder() const {
		return h264decoder;
	}

	bool isDumpStreamInfo() const {
		return dumpStreamInfo;
	}

	std::string getAudioFilePath() const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/audio.dat";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getIntVideoFilePath(int index) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/i" << index << ".mpg";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getStreamInfoPath() const
	{
		return outVideoPath + "-streaminfo.dat";
	}

	std::string getEncVideoFilePath(int vindex, int index) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/v" << vindex << "-" << index << ".raw";
		ctx.registerTmpFile(ss.str());
		return ss.str();
  }

  std::string getEncStatsFilePath(int vindex, int index) const
  {
    std::ostringstream ss;
    ss << tmpDir.path() << "/s" << vindex << "-" << index << ".log";
		ctx.registerTmpFile(ss.str());
		// x264は.mbtreeも生成するので
		ctx.registerTmpFile(ss.str() + ".mbtree");
    return ss.str();
  }

	std::string getEncTimecodeFilePath(int vindex, int index) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/tc" << vindex << "-" << index << ".txt";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getEncPulldownFilePath(int vindex, int index) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/pd" << vindex << "-" << index << ".txt";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getIntAudioFilePath(int vindex, int index, int aindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/a" << vindex << "-" << index << "-" << aindex << ".aac";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getVfrTmpFilePath(int index) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/t" << index << ".mp4";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getLogoTmpFilePath() const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/logotmp.dat";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getOutFilePath(int index) const
	{
		std::ostringstream ss;
		ss << outVideoPath;
		if (index != 0) {
			ss << "-" << index;
		}
		ss << ".mp4";
		return ss.str();
	}

	std::string getOutSummaryPath() const
	{
		std::ostringstream ss;
		ss << outVideoPath;
		ss << ".txt";
		return ss.str();
	}

  std::string getOptions(
    VIDEO_STREAM_FORMAT srcFormat, double srcBitrate, bool pulldown,
    int pass, int vindex, int index) const 
  {
    std::ostringstream ss;
    ss << encoderOptions;
    if (autoBitrate) {
      double targetBitrate = bitrate.getTargetBitrate(srcFormat, srcBitrate);
			if (encoder == ENCODER_QSVENC) {
				ss << " --la " << (int)targetBitrate;
				ss << " --maxbitrate " << (int)(targetBitrate * 2);
			}
			else if (encoder == ENCODER_NVENC) {
				ss << " --vbrhq " << (int)targetBitrate;
				ss << " --maxbitrate " << (int)(targetBitrate * 2);
			}
			else {
				ss << " --bitrate " << (int)targetBitrate;
				ss << " --vbv-maxrate " << (int)(targetBitrate * 2);
				ss << " --vbv-bufsize 31250"; // high profile level 4.1
			}
    }
		if (pulldown) {
			ss << " --pdfile-in \"" << getEncPulldownFilePath(vindex, index) << "\"";
		}
    if (pass >= 0) {
      ss << " --pass " << pass;
      ss << " --stats \"" << getEncStatsFilePath(vindex, index) << "\"";
    }
    return ss.str();
  }

	void dump() const {
		ctx.info("[設定]");
    ctx.info("Mode: %s", mode.c_str());
    ctx.info("Input: %s", srcFilePath.c_str());
		ctx.info("Output: %s", outVideoPath.c_str());
		ctx.info("WorkDir: %s", tmpDir.path().c_str());
		ctx.info("OutJson: %s", outInfoJsonPath.c_str());
		ctx.info("Encoder: %s", encoderToString(encoder));
		ctx.info("EncoderPath: %s", encoderPath.c_str());
		ctx.info("EncoderOptions: %s", encoderOptions.c_str());
		ctx.info("MuxerPath: %s", muxerPath.c_str());
    ctx.info("TimelineeditorPath: %s", timelineditorPath.c_str());
    ctx.info("autoBitrate: %s", autoBitrate ? "yes" : "no");
    ctx.info("Bitrate: %f:%f:%f", bitrate.a, bitrate.b, bitrate.h264);
    ctx.info("twoPass: %s", twoPass ? "yes" : "no");
		if (serviceId > 0) {
			ctx.info("ServiceId: 0x%04x", serviceId);
		}
		else {
			ctx.info("ServiceId: 指定なし");
		}
		ctx.info("mpeg2decoder: %s", decoderToString(mpeg2decoder));
		ctx.info("h264decoder: %s", decoderToString(h264decoder));
	}

private:
	TempDirectory tmpDir;

	std::string mode;
  std::string modeArgs;
	// 入力ファイルパス（拡張子を含む）
	std::string srcFilePath;
	// 出力ファイルパス（拡張子を除く）
	std::string outVideoPath;
	// 結果情報JSON出力パス
	std::string outInfoJsonPath;
  std::string filterScriptPath;
	// エンコーダ設定
	ENUM_ENCODER encoder;
	std::string encoderPath;
	std::string encoderOptions;
	std::string muxerPath;
	std::string timelineditorPath;
	bool twoPass;
	bool autoBitrate;
	bool pulldown;
	BitrateSetting bitrate;
	int serviceId;
	DECODER_TYPE mpeg2decoder;
	DECODER_TYPE h264decoder;
	// デバッグ用設定
	bool dumpStreamInfo;

	const char* decoderToString(DECODER_TYPE decoder) const {
		switch (decoder) {
		case DECODER_QSV: return "QSV";
		case DECODER_CUVID: return "CUVID";
		}
		return "default";
	}
};

class AMTSplitter : public TsSplitter {
public:
	AMTSplitter(AMTContext& ctx, const TranscoderSetting& setting)
		: TsSplitter(ctx)
		, setting_(setting)
		, psWriter(ctx)
		, writeHandler(*this)
		, audioFile_(setting.getAudioFilePath(), "wb")
		, videoFileCount_(0)
		, videoStreamType_(-1)
		, audioStreamType_(-1)
		, audioFileSize_(0)
		, srcFileSize_(0)
	{
		psWriter.setHandler(&writeHandler);
	}

	StreamReformInfo split()
	{
		writeHandler.resetSize();

		readAll();

		// for debug
		printInteraceCount();

		return StreamReformInfo(ctx, videoFileCount_,
			videoFrameList_, audioFrameList_, streamEventList_);
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
			file_ = std::unique_ptr<File>(new File(path, "wb"));
		}
		void close() {
			file_ = nullptr;
		}
		void resetSize() {
			totalIntVideoSize_ = 0;
		}
		int64_t getTotalSize() const {
			return totalIntVideoSize_;
		}
	};

	const TranscoderSetting& setting_;
	PsStreamWriter psWriter;
	StreamFileWriteHandler writeHandler;
	File audioFile_;

	int videoFileCount_;
	int videoStreamType_;
	int audioStreamType_;
	int64_t audioFileSize_;
	int64_t srcFileSize_;

	// データ
  std::vector<FileVideoFrameInfo> videoFrameList_;
	std::vector<FileAudioFrameInfo> audioFrameList_;
	std::vector<StreamEvent> streamEventList_;

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
			info.fileOffset = audioFileSize_;
			audioFile_.write(MemoryChunk(frame.codedData, frame.codedDataSize));
			audioFileSize_ += frame.codedDataSize;
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

	// TsPacketSelectorHandler仮想関数 //

	virtual void onPidTableChanged(const PMTESInfo video, const std::vector<PMTESInfo>& audio) {
		// ベースクラスの処理
		TsSplitter::onPidTableChanged(video, audio);

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

struct EncodeFileInfo {
  double srcBitrate;
  double targetBitrate;
};

class AMTVideoEncoder : public AMTObject {
public:
	AMTVideoEncoder(
		AMTContext&ctx,
		const TranscoderSetting& setting,
		StreamReformInfo& reformInfo)
		: AMTObject(ctx)
		, setting_(setting)
		, reformInfo_(reformInfo)
		, thread_(this, 8)
		, prevFrameIndex_()
		, pd_data_(NULL)
	{
		//
	}

	~AMTVideoEncoder() {
		delete[] encoders_; encoders_ = NULL;
	}

  std::vector<EncodeFileInfo> peform(int videoFileIndex)
  {
		videoFileIndex_ = videoFileIndex;
    numEncoders_ = reformInfo_.getNumEncoders(videoFileIndex);
    efi_ = std::vector<EncodeFileInfo>(numEncoders_, EncodeFileInfo());

		const auto& format0 = reformInfo_.getFormat(0, videoFileIndex_);
		int bufsize = format0.videoFormat.width * format0.videoFormat.height * 3;

		// pulldownファイル生成
		bool pulldown = (setting_.isPulldownEnabled() && reformInfo_.hasRFF());
		ctx.setCounter("pulldown", (int)pulldown);
		if (pulldown) {
			generatePulldownFile(bufsize);
		}

		// x265でインタレースの場合はフィールドモード
		bool fieldMode = 
			(setting_.getEncoder() == ENCODER_X265 &&
			 format0.videoFormat.progressive == false);
		ctx.setCounter("fieldmode", (int)fieldMode);

    // ビットレート計算
    double srcBitrate = getSourceBitrate();
    ctx.info("入力映像ビットレート: %d kbps", (int)srcBitrate);

    VIDEO_STREAM_FORMAT srcFormat = reformInfo_.getVideoStreamFormat();
    double targetBitrate = std::numeric_limits<float>::quiet_NaN();
    if (setting_.isAutoBitrate()) {
      targetBitrate = setting_.getBitrate().getTargetBitrate(srcFormat, srcBitrate);
      ctx.info("目標映像ビットレート: %d kbps", (int)targetBitrate);
    }
    for (int i = 0; i < numEncoders_; ++i) {
      efi_[i].srcBitrate = srcBitrate;
      efi_[i].targetBitrate = targetBitrate;
    }

		auto getOptions = [&](int pass, int index) {
			return setting_.getOptions(
				srcFormat, srcBitrate, pulldown, pass, videoFileIndex_, index);
		};

    if (setting_.isTwoPass()) {
      ctx.info("1/2パス エンコード開始");
      processAllData(fieldMode, bufsize, getOptions, 1);
      ctx.info("2/2パス エンコード開始");
      processAllData(fieldMode, bufsize, getOptions, 2);
    }
    else {
      processAllData(fieldMode, bufsize, getOptions, -1);
    }

    return efi_;
	}

private:
	class SpVideoReader : public av::VideoReader {
	public:
		SpVideoReader(AMTVideoEncoder* this_)
			: VideoReader(this_->ctx)
			, this_(this_)
		{ }
  protected:
    virtual void onVideoFormat(AVStream *stream, VideoFormat fmt) { }
    virtual void onFrameDecoded(av::Frame& frame) {
      this_->onFrameDecoded(frame);
    }
    virtual void onAudioPacket(AVPacket& packet) { }
	private:
		AMTVideoEncoder* this_;
	};

	class SpDataPumpThread : public DataPumpThread<std::unique_ptr<av::Frame>> {
	public:
		SpDataPumpThread(AMTVideoEncoder* this_, int bufferingFrames)
			: DataPumpThread(bufferingFrames)
			, this_(this_)
		{ }
	protected:
		virtual void OnDataReceived(std::unique_ptr<av::Frame>&& data) {
			this_->onFrameReceived(std::move(data));
		}
	private:
		AMTVideoEncoder* this_;
	};

	const TranscoderSetting& setting_;
	StreamReformInfo& reformInfo_;

	int videoFileIndex_;
  int numEncoders_;
	av::EncodeWriter* encoders_;
	std::stringstream* pd_data_;
  std::vector<EncodeFileInfo> efi_;
	
	SpDataPumpThread thread_;
	int prevFrameIndex_;

	RFFExtractor rffExtractor_;


  void processAllData(bool fieldMode, int bufsize, std::function<std::string(int,int)> getOptions, int pass)
  {
    // 初期化
    encoders_ = new av::EncodeWriter[numEncoders_];
    SpVideoReader reader(this);

    for (int i = 0; i < numEncoders_; ++i) {
      const auto& format = reformInfo_.getFormat(i, videoFileIndex_);
      std::string args = makeEncoderArgs(
        setting_.getEncoder(),
        setting_.getEncoderPath(),
        getOptions(pass, i),
        format.videoFormat,
				setting_.getEncVideoFilePath(videoFileIndex_, i));
      ctx.info("[エンコーダ開始]");
      ctx.info(args.c_str());
      encoders_[i].start(args, format.videoFormat, fieldMode, bufsize);
    }

    // エンコードスレッド開始
    thread_.start();

    // エンコード
    std::string intVideoFilePath = setting_.getIntVideoFilePath(videoFileIndex_);
    reader.readAll(intVideoFilePath,
			setting_.getMpeg2Decoder(), setting_.getH264Decoder(), DECODER_DEFAULT);

    // エンコードスレッドを終了して自分に引き継ぐ
    thread_.join();

    // 残ったフレームを処理
    for (int i = 0; i < numEncoders_; ++i) {
      encoders_[i].finish();
    }

    // 終了
		rffExtractor_.clear();
    delete[] encoders_; encoders_ = NULL;
  }

	void generatePulldownFile(int bufsize)
	{
		// 初期化
		pd_data_ = new std::stringstream[numEncoders_];
		SpVideoReader reader(this);

		// エンコードスレッド開始
		thread_.start();

		// エンコード
		std::string intVideoFilePath = setting_.getIntVideoFilePath(videoFileIndex_);
		reader.readAll(intVideoFilePath,
			setting_.getMpeg2Decoder(), setting_.getH264Decoder(), DECODER_DEFAULT);

		// エンコードスレッドを終了して自分に引き継ぐ
		thread_.join();

		// ファイル出力
		for (int i = 0; i < numEncoders_; ++i) {
			std::string str = pd_data_[i].str();
			MemoryChunk mc(reinterpret_cast<uint8_t*>(const_cast<char*>(str.data())), str.size());
			File file(setting_.getEncPulldownFilePath(videoFileIndex_, i), "w");
			file.write(mc);
		}

		// 終了
		delete[] pd_data_; pd_data_ = NULL;
	}

  double getSourceBitrate()
  {
    // ビットレート計算
    VIDEO_STREAM_FORMAT srcFormat = reformInfo_.getVideoStreamFormat();
    int64_t srcBytes = 0;
    double srcDuration = 0;
    for (int i = 0; i < numEncoders_; ++i) {
      const auto& info = reformInfo_.getSrcVideoInfo(i, videoFileIndex_);
      srcBytes += info.first;
      srcDuration += info.second;
    }
    return ((double)srcBytes * 8 / 1000) / ((double)srcDuration / MPEG_CLOCK_HZ);
  }

	void onFrameDecoded(av::Frame& frame__) {
		// フレームをコピーしてスレッドに渡す
		thread_.put(std::unique_ptr<av::Frame>(new av::Frame(frame__)), 1);
	}

	const char* toPulldownFlag(PICTURE_TYPE pic, bool progressive) {
		switch (pic) {
		case PIC_FRAME: return "SGL";
		case PIC_FRAME_DOUBLING: return "DBL";
		case PIC_FRAME_TRIPLING: return "TPL";
		case PIC_TFF: return progressive ? "PTB" : "TB";
		case PIC_BFF: return progressive ? "PBT" : "BT";
		case PIC_TFF_RFF: return "TBT";
		case PIC_BFF_RFF: return "BTB";
		default: THROWF(FormatException, "Unknown PICTURE_TYPE %d", pic);
		}
		return NULL;
	}

	static std::unique_ptr<av::Frame> toYV12(std::unique_ptr<av::Frame>&& frame)
	{
		if ((*frame)()->format != AV_PIX_FMT_NV12) {
			return std::move(frame);
		}

		//fprintf(stderr, "f");

		auto dstframe = std::unique_ptr<av::Frame>(new av::Frame());

		AVFrame* src = (*frame)();
		AVFrame* dst = (*dstframe)();

		// フレームのプロパティをコピー
		av_frame_copy_props(dst, src);

		// メモリサイズに関する情報をコピー
		dst->format = AV_PIX_FMT_YUV420P;
		dst->width = src->width;
		dst->height = src->height;

		// メモリ確保
		if (av_frame_get_buffer(dst, 64) != 0) {
			THROW(RuntimeException, "failed to allocate frame buffer");
		}

		// copy luna
		for (int y = 0; y < dst->height; ++y) {
			uint8_t* dst0 = dst->data[0] + dst->linesize[0] * y;
			uint8_t* src0 = src->data[0] + src->linesize[0] * y;
			memcpy(dst0, src0, dst->width);
		}

		// extract chroma
		__m128i shuffle_i = _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15);
		int chroma_h = dst->height / 2;
		for (int y = 0; y < chroma_h; ++y) {
			uint8_t* dstU = dst->data[1] + dst->linesize[1] * y;
			uint8_t* dstV = dst->data[2] + dst->linesize[2] * y;
			uint8_t* src0 = src->data[1] + src->linesize[1] * y;

			int x = 0;
			int chroma_w = dst->width / 2;
			for (; x <= (chroma_w - 8); x += 8) {
				__m128i s = _mm_loadu_si128((__m128i*)&src0[x * 2]);
				__m128i ss = _mm_shuffle_epi8(s, shuffle_i);
				*(int64_t*)&dstU[x] = _mm_extract_epi64(ss, 0);
				*(int64_t*)&dstV[x] = _mm_extract_epi64(ss, 1);
			}
			for (; x < chroma_w; ++x) {
				dstU[x] = src0[x * 2 + 0];
				dstV[x] = src0[x * 2 + 1];
			}
		}

		return std::move(dstframe);
	}

	void onFrameReceived(std::unique_ptr<av::Frame>&& frame) {

		// ffmpegがどうptsをwrapするか分からないので入力データの
		// 下位33bitのみを見る
		//（26時間以上ある動画だと重複する可能性はあるが無視）
		int64_t pts = (*frame)()->pts & ((int64_t(1) << 33) - 1);

		int frameIndex = reformInfo_.getVideoFrameIndex(pts, videoFileIndex_);
		if (frameIndex == -1) {
			frameIndex = prevFrameIndex_;
			ctx.incrementCounter("incident");
			ctx.warn("Unknown PTS frame %lld", pts);
		}
		else {
			prevFrameIndex_ = frameIndex;
		}

		VideoFrameInfo info = reformInfo_.getVideoFrameInfo(frameIndex);
		int encoderIndex = reformInfo_.getEncoderIndex(frameIndex);

		if (pd_data_ != NULL) {
			// pulldownファイル生成中
			auto& ss = pd_data_[encoderIndex];
			ss << toPulldownFlag(info.pic, info.progressive) << std::endl;
			return;
		}

		auto& encoder = encoders_[encoderIndex];

		// NV12 -> YV12変換
		auto frameYV12 = toYV12(std::move(frame));

		if (reformInfo_.isVFR() || setting_.isPulldownEnabled()) {
			// VFRの場合は必ず１枚だけ出力
			// プルダウンが有効な場合はフラグで処理するので１枚だけ出力
			encoder.inputFrame(*frameYV12);
		}
		else {
			// RFFフラグ処理
			rffExtractor_.inputFrame(encoder, std::move(frameYV12), info.pic);
		}

		reformInfo_.frameEncoded(frameIndex);
	}

};

class AMTFilterVideoEncoder : public AMTObject {
public:
  AMTFilterVideoEncoder(
    AMTContext&ctx,
    const TranscoderSetting& setting,
    StreamReformInfo& reformInfo)
    : AMTObject(ctx)
    , setting_(setting)
    , reformInfo_(reformInfo)
		, videoFileIndex_()
		, env_()
		, filter_()
		, vi_()
		, outfmt_()
		, numEncoders_()
		, encoders_()
    , pd_data_()
		, thread_(this, 8)
		, prevFrameIndex_()
  {
  }

  ~AMTFilterVideoEncoder() {
    // エラー落ちした場合用の解放処理
    delete[] encoders_; encoders_ = nullptr;
    DeleteFilter();
  }

  std::vector<FilterOutVideoInfo> genOutFrameList() {
    // Fake音声データからエンコードフレームのタイムスタンプを取得
    int numVideoFile = reformInfo_.getNumVideoFile();
    std::vector<FilterOutVideoInfo> infos(numVideoFile);
    for (int i = 0; i < numVideoFile; ++i) {
      videoFileIndex_ = i;
      CreateFilter();

      FilterOutVideoInfo& info = infos[i];
      info.numFrames = vi_.num_frames;
      info.frameRateNum = vi_.fps_numerator;
      info.frameRateDenom = vi_.fps_denominator;
      info.fakeAudioSampleRate = vi_.audio_samples_per_second;
      info.fakeAudioSamples.resize(vi_.num_audio_samples);
      
      const int SAMPLES = 1000;
      std::unique_ptr<int32_t[]> buf =
        std::unique_ptr<int32_t[]>(new int32_t[vi_.nchannels * SAMPLES]);

      for (int idx = 0; idx < vi_.num_audio_samples; idx += SAMPLES) {
        int count = std::min<int>((int)vi_.num_audio_samples, idx + SAMPLES) - idx;
        filter_->GetAudio(buf.get(), idx, count, env_);

        for (int i = 0; i < count; ++i) {
          info.fakeAudioSamples[idx + i] =
            (int)reinterpret_cast<av::FakeAudioSample*>(&buf[vi_.nchannels * i])->index;
        }
      }

      DeleteFilter();
    }
    return infos;
  }

  std::vector<EncodeFileInfo> peform(int videoFileIndex)
  {
    videoFileIndex_ = videoFileIndex;
    CreateFilter();

    // フレーム数チェック
    if (vi_.num_frames != reformInfo_.getNumFilterOutFrames(videoFileIndex_)) {
      THROW(RuntimeException, "フィルタ出力のフレーム数が変化しています！！");
    }
    
    numEncoders_ = reformInfo_.getNumEncoders(videoFileIndex);
    efi_ = std::vector<EncodeFileInfo>(numEncoders_, EncodeFileInfo());

    const auto& format0 = reformInfo_.getFormat(0, videoFileIndex_);
    int bufsize = format0.videoFormat.width * format0.videoFormat.height * 3;

    // ビットレート計算
    double srcBitrate = getSourceBitrate();
    ctx.info("入力映像ビットレート: %d kbps", (int)srcBitrate);

    VIDEO_STREAM_FORMAT srcFormat = reformInfo_.getVideoStreamFormat();
    double targetBitrate = std::numeric_limits<float>::quiet_NaN();
    if (setting_.isAutoBitrate()) {
      targetBitrate = setting_.getBitrate().getTargetBitrate(srcFormat, srcBitrate);
      ctx.info("目標映像ビットレート: %d kbps", (int)targetBitrate);
    }
    for (int i = 0; i < numEncoders_; ++i) {
      efi_[i].srcBitrate = srcBitrate;
      efi_[i].targetBitrate = targetBitrate;
    }

    auto getOptions = [&](int pass, int index) {
      return setting_.getOptions(
        srcFormat, srcBitrate, false, pass, videoFileIndex_, index);
    };

    if (setting_.isTwoPass()) {
      ctx.info("1/2パス エンコード開始");
      processAllData(bufsize, getOptions, 1);
      ctx.info("2/2パス エンコード開始");
      processAllData(bufsize, getOptions, 2);
    }
    else {
      processAllData(bufsize, getOptions, -1);
    }

    DeleteFilter();

    return efi_;
  }

  static AVSValue CreateInternalAMTSource(AVSValue args, void* user_data, IScriptEnvironment* env) {
    return static_cast<AMTFilterVideoEncoder*>(user_data)->makeFilterSource(env);
  }

private:
  struct OutFrame {
    PVideoFrame frame;
    int n;

    OutFrame(const PVideoFrame& frame, int n)
      : frame(frame)
      , n(n) { }
  };

  class SpDataPumpThread : public DataPumpThread<std::unique_ptr<OutFrame>> {
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

  const TranscoderSetting& setting_;
  StreamReformInfo& reformInfo_;


  int videoFileIndex_;
  IScriptEnvironment2* env_;
  PClip filter_;
  VideoInfo vi_;
  VideoFormat outfmt_;
  int numEncoders_;
  av::EncodeWriter* encoders_;
  std::stringstream* pd_data_;
  std::vector<EncodeFileInfo> efi_;

  SpDataPumpThread thread_;
  int prevFrameIndex_;

  void CreateFilter() {
		std::string modulepath = GetModulePath();
    env_ = CreateScriptEnvironment2();

		// TODO: 本来これはやってはいけないようなので、グローバル変数を使う
    env_->AddFunction(av::GetInternalAMTSouceName(), "", CreateInternalAMTSource, this);
		
		AVSValue result;
		if (env_->LoadPlugin(modulepath.c_str(), false, &result) == false) {
			THROW(RuntimeException, "Failed to LoadPlugin ...");
		}

		result = env_->Invoke("Import", setting_.getFilterScriptPath().c_str(), 0);
    filter_ = result.AsClip();
    vi_ = filter_->GetVideoInfo();

    // vi_からエンコーダ入力用VideoFormatを生成する
    outfmt_ = reformInfo_.getFormat(0, videoFileIndex_).videoFormat;
    if (outfmt_.width != vi_.width || outfmt_.height != vi_.height) {
      // リサイズされた
      outfmt_.width = vi_.width;
      outfmt_.height = vi_.height;
      // リサイズされた場合はアスペクト比を1:1にする
      outfmt_.sarHeight = outfmt_.sarWidth = 1;
    }
    outfmt_.frameRateDenom = vi_.fps_denominator;
    outfmt_.frameRateNum = vi_.fps_numerator;
    // インターレースかどうかは取得できないのでパリティが0だったらインターレースと仮定
    outfmt_.progressive = (filter_->GetParity(0) == false);
  }

  void DeleteFilter() {
    filter_ = nullptr;
    if (env_) {
      env_->DeleteScriptEnvironment();
      env_ = nullptr;
    }
  }

  PClip makeFilterSource(IScriptEnvironment* env) {
    return new av::AMTSource(ctx,
      setting_.getIntVideoFilePath(videoFileIndex_),
      reformInfo_.getFormat(0, videoFileIndex_).videoFormat,
      reformInfo_.getFilterSourceFrames(videoFileIndex_),
      env);
  }

  void processAllData(int bufsize, std::function<std::string(int, int)> getOptions, int pass)
  {
    // 初期化
    encoders_ = new av::EncodeWriter[numEncoders_];

    for (int i = 0; i < numEncoders_; ++i) {
      const auto& format = reformInfo_.getFormat(i, videoFileIndex_);
      std::string args = makeEncoderArgs(
        setting_.getEncoder(),
        setting_.getEncoderPath(),
        getOptions(pass, i),
        outfmt_,
        setting_.getEncVideoFilePath(videoFileIndex_, i));
      ctx.info("[エンコーダ開始]");
      ctx.info(args.c_str());
      encoders_[i].start(args, outfmt_, false, bufsize);
    }

    // エンコードスレッド開始
    thread_.start();

    // エンコード
    for (int i = 0; i < vi_.num_frames; ++i) {
      thread_.put(std::unique_ptr<OutFrame>(new OutFrame(filter_->GetFrame(i, env_), i)), 1);
    }

    // エンコードスレッドを終了して自分に引き継ぐ
    thread_.join();

    // 残ったフレームを処理
    for (int i = 0; i < numEncoders_; ++i) {
      encoders_[i].finish();
    }

    // 終了
    delete[] encoders_; encoders_ = NULL;
  }

  double getSourceBitrate()
  {
    // ビットレート計算
    VIDEO_STREAM_FORMAT srcFormat = reformInfo_.getVideoStreamFormat();
    int64_t srcBytes = 0;
    double srcDuration = 0;
    for (int i = 0; i < numEncoders_; ++i) {
      const auto& info = reformInfo_.getSrcVideoInfo(i, videoFileIndex_);
      srcBytes += info.first;
      srcDuration += info.second;
    }
    return ((double)srcBytes * 8 / 1000) / ((double)srcDuration / MPEG_CLOCK_HZ);
  }

  int getFFformat() {
    switch (vi_.BitsPerComponent()) {
    case 8: return AV_PIX_FMT_YUV420P;
    case 10: return AV_PIX_FMT_YUV420P10;
    case 12: return AV_PIX_FMT_YUV420P12;
    case 16: return AV_PIX_FMT_YUV420P16;
    default: THROW(FormatException, "サポートされていないフィルタ出力形式です");
    }
    return 0;
  }

  void onFrameReceived(std::unique_ptr<OutFrame>&& frame) {
    
    int encoderIndex = reformInfo_.getFilterOutEncoderIndex(videoFileIndex_, frame->n);
    auto& encoder = encoders_[encoderIndex];

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

    encoder.inputFrame(out);
  }

};

class AMTSimpleVideoEncoder : public AMTObject {
public:
  AMTSimpleVideoEncoder(
    AMTContext& ctx,
    const TranscoderSetting& setting)
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

  const TranscoderSetting& setting_;
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
						fmt->streams[i], setting_.getIntAudioFilePath(0, 0, audioCount_), 8 * 1024));
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
    reader_.readAll(setting_.getSrcFilePath(),
			setting_.getMpeg2Decoder(), setting_.getH264Decoder(), DECODER_DEFAULT);

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
				fmt.format, srcBitrate, false, pass_, 0, 0),
      fmt,
      setting_.getEncVideoFilePath(0, 0));

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
		const TranscoderSetting& setting,
		const StreamReformInfo& reformInfo)
		: AMTObject(ctx)
		, setting_(setting)
		, reformInfo_(reformInfo)
		, audioCache_(ctx, setting.getAudioFilePath(), reformInfo.getAudioFileOffsets(), 12, 4)
		, totalOutSize_(0)
	{ }

	void mux(int videoFileIndex) {
		int numEncoders = reformInfo_.getNumEncoders(videoFileIndex);
		if (numEncoders == 0) {
			return;
		}

		for (int i = 0; i < numEncoders; ++i) {
			// 音声ファイルを作成
			std::vector<std::string> audioFiles;
			const FileAudioFrameList& fileFrameList =
				reformInfo_.getFileAudioFrameList(i, videoFileIndex);
			for (int a = 0; a < (int)fileFrameList.size(); ++a) {
				const std::vector<int>& frameList = fileFrameList[a];
				if (frameList.size() > 0) {
					std::string filepath = setting_.getIntAudioFilePath(videoFileIndex, i, a);
					File file(filepath, "wb");
					for (int frameIndex : frameList) {
						file.write(audioCache_[frameIndex]);
					}
					audioFiles.push_back(filepath);
				}
			}

			// Mux
			int outFileIndex = reformInfo_.getOutFileIndex(i, videoFileIndex);
			std::string encVideoFile = setting_.getEncVideoFilePath(videoFileIndex, i);
			std::string outFilePath = setting_.getOutFilePath(outFileIndex);
			std::string args = makeMuxerArgs(
				setting_.getMuxerPath(), encVideoFile,
				reformInfo_.getFormat(i, videoFileIndex).videoFormat,
				audioFiles, outFilePath);
			ctx.info("[Mux開始]");
			ctx.info(args.c_str());

			{
				MySubProcess muxer(args);
				int ret = muxer.join();
				if (ret != 0) {
					THROWF(RuntimeException, "mux failed (muxer exit code: %d)", ret);
				}
			}

			{ // 出力サイズ取得
				File outfile(setting_.getOutFilePath(outFileIndex), "rb");
				totalOutSize_ += outfile.size();
			}
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

	const TranscoderSetting& setting_;
	const StreamReformInfo& reformInfo_;

	PacketCache audioCache_;
	int64_t totalOutSize_;
};

class AMTSimpleMuxder : public AMTObject {
public:
	AMTSimpleMuxder(
		AMTContext&ctx,
		const TranscoderSetting& setting)
		: AMTObject(ctx)
		, setting_(setting)
		, totalOutSize_(0)
	{ }

	void mux(VideoFormat videoFormat, int audioCount) {
			// Mux
		std::vector<std::string> audioFiles;
		for (int i = 0; i < audioCount; ++i) {
			audioFiles.push_back(setting_.getIntAudioFilePath(0, 0, i));
		}
		std::string encVideoFile = setting_.getEncVideoFilePath(0, 0);
		std::string outFilePath = setting_.getOutFilePath(0);
		std::string args = makeMuxerArgs(
			setting_.getMuxerPath(), encVideoFile, videoFormat, audioFiles, outFilePath);
		ctx.info("[Mux開始]");
		ctx.info(args.c_str());

		{
			MySubProcess muxer(args);
			int ret = muxer.join();
			if (ret != 0) {
				THROWF(RuntimeException, "mux failed (muxer exit code: %d)", ret);
			}
		}

		{ // 出力サイズ取得
			File outfile(setting_.getOutFilePath(0), "rb");
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

	const TranscoderSetting& setting_;
	int64_t totalOutSize_;
};

static std::vector<char> toUTF8String(const std::string str) {
	if (str.size() == 0) {
		return std::vector<char>();
	}
	int intlen = (int)str.size() * 2;
	auto wc = std::unique_ptr<wchar_t[]>(new wchar_t[intlen]);
	intlen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), wc.get(), intlen);
	if (intlen == 0) {
		THROW(RuntimeException, "MultiByteToWideChar failed");
	}
	int dstlen = WideCharToMultiByte(CP_UTF8, 0, wc.get(), intlen, NULL, 0, NULL, NULL);
	if (dstlen == 0) {
		THROW(RuntimeException, "MultiByteToWideChar failed");
	}
	std::vector<char> ret(dstlen);
	WideCharToMultiByte(CP_UTF8, 0, wc.get(), intlen, ret.data(), (int)ret.size(), NULL, NULL);
	return ret;
}

static std::string toJsonString(const std::string str) {
	if (str.size() == 0) {
		return str;
	}
	std::vector<char> utf8 = toUTF8String(str);
	std::vector<char> ret;
	for (char c : utf8) {
		switch (c) {
		case '\"':
			ret.push_back('\\');
			ret.push_back('\"');
			break;
		case '\\':
			ret.push_back('\\');
			ret.push_back('\\');
			break;
		case '/':
			ret.push_back('\\');
			ret.push_back('/');
			break;
		case '\b':
			ret.push_back('\\');
			ret.push_back('b');
			break;
		case '\f':
			ret.push_back('\\');
			ret.push_back('f');
			break;
		case '\n':
			ret.push_back('\\');
			ret.push_back('n');
			break;
		case '\r':
			ret.push_back('\\');
			ret.push_back('r');
			break;
		case '\t':
			ret.push_back('\\');
			ret.push_back('t');
			break;
		default:
			ret.push_back(c);
		}
	}
	return std::string(ret.begin(), ret.end());
}

static void transcodeMain(AMTContext& ctx, const TranscoderSetting& setting)
{
	setting.dump();

	auto splitter = std::unique_ptr<AMTSplitter>(new AMTSplitter(ctx, setting));
	if (setting.getServiceId() > 0) {
		splitter->setServiceId(setting.getServiceId());
	}
	StreamReformInfo reformInfo = splitter->split();
	int64_t totalIntVideoSize = splitter->getTotalIntVideoSize();
	int64_t srcFileSize = splitter->getSrcFileSize();
	splitter = nullptr;

	if (setting.isDumpStreamInfo()) {
		reformInfo.serialize(setting.getStreamInfoPath());
	}

  reformInfo.prepareEncode();

	auto encoder = std::unique_ptr<AMTFilterVideoEncoder>(new AMTFilterVideoEncoder(ctx, setting, reformInfo));

  auto audioDiffInfo = reformInfo.prepareFilterOut(encoder->genOutFrameList());
  audioDiffInfo.printAudioPtsDiff(ctx);

  std::vector<EncodeFileInfo> bitrateInfo;
	for (int i = 0; i < reformInfo.getNumVideoFile(); ++i) {
    if (reformInfo.getNumEncoders(i) == 0) {
      ctx.warn("numEncoders == 0 ...");
    }
    else {
      auto efi = encoder->peform(i);
      bitrateInfo.insert(bitrateInfo.end(), efi.begin(), efi.end());
    }
	}
	encoder = nullptr;

  auto muxer = std::unique_ptr<AMTMuxder>(new AMTMuxder(ctx, setting, reformInfo));
  for (int i = 0; i < reformInfo.getNumVideoFile(); ++i) {
    muxer->mux(i);
  }
	int64_t totalOutSize = muxer->getTotalOutSize();
  muxer = nullptr;

	// 出力結果を表示
	ctx.info("完了");
	reformInfo.printOutputMapping([&](int index) { return setting.getOutFilePath(index); });

	// 出力結果JSON出力
	if (setting.getOutInfoJsonPath().size() > 0) {
		std::ostringstream ss;
		ss << "{ \"srcpath\": \"" << toJsonString(setting.getSrcFilePath()) << "\", ";
		ss << "\"outpath\": [";
		for (int i = 0; i < reformInfo.getNumOutFiles(); ++i) {
			if (i > 0) {
				ss << ", ";
			}
			ss << "\"" << toJsonString(setting.getOutFilePath(i)) << "\"";
		}
    ss << "], ";
    ss << "\"bitrate\": [";
    for (int i = 0; i < (int)bitrateInfo.size(); ++i) {
      auto info = bitrateInfo[i];
      if (i > 0) {
        ss << ", ";
      }
      ss << "{ \"src\": " << (int)info.srcBitrate
        << ", \"tgt1st\": " << (int)info.targetBitrate << "}";
    }
    ss << "], ";
		ss << "\"srcfilesize\": " << srcFileSize << ", ";
		ss << "\"intvideofilesize\": " << totalIntVideoSize << ", ";
		ss << "\"outfilesize\": " << totalOutSize << ", ";
		auto duration = reformInfo.getInOutDuration();
		ss << "\"srcduration\": " << std::fixed << std::setprecision(3)
			 << ((double)duration.first / MPEG_CLOCK_HZ) << ", ";
		ss << "\"outduration\": " << std::fixed << std::setprecision(3)
			 << ((double)duration.second / MPEG_CLOCK_HZ) << ", ";
		ss << "\"audiodiff\": ";
		audioDiffInfo.printToJson(ss);
		for (const auto& pair : ctx.getCounter()) {
			ss << ", \"" << pair.first << "\": " << pair.second;
		}
		ss << " }";

		std::string str = ss.str();
		MemoryChunk mc(reinterpret_cast<uint8_t*>(const_cast<char*>(str.data())), str.size());
		File file(setting.getOutInfoJsonPath(), "w");
		file.write(mc);
	}
}

static void transcodeSimpleMain(AMTContext& ctx, const TranscoderSetting& setting)
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
		std::ostringstream ss;
		ss << "{ \"srcpath\": \"" << toJsonString(setting.getSrcFilePath()) << "\", ";
		ss << "\"outpath\": [";
		ss << "\"" << toJsonString(setting.getOutFilePath(0)) << "\"";
		ss << "], ";
		ss << "\"srcfilesize\": " << srcFileSize << ", ";
		ss << "\"outfilesize\": " << totalOutSize;
		ss << " }";

		std::string str = ss.str();
		MemoryChunk mc(reinterpret_cast<uint8_t*>(const_cast<char*>(str.data())), str.size());
		File file(setting.getOutInfoJsonPath(), "w");
		file.write(mc);
	}
}

