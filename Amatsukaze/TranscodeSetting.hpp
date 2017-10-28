#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <direct.h>

#include "StreamUtils.hpp"

// カラースペース定義を使うため
#include "libavutil/pixfmt.h"

struct EncoderZone {
	int startFrame;
	int endFrame;
};

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
	const std::string& outpath,
	const std::string& chapterpath)
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
	if (chapterpath.size() > 0) {
		ss << " --chapter \"" << chapterpath << "\"";
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
		std::string postFilterScriptPath,
		ENUM_ENCODER encoder,
		std::string encoderPath,
		std::string encoderOptions,
		std::string muxerPath,
		std::string timelineditorPath,
		bool twoPass,
		bool autoBitrate,
		bool pulldown,
		BitrateSetting bitrate,
		double bitrateCM,
		int serviceId,
		DECODER_TYPE mpeg2decoder,
		DECODER_TYPE h264decoder,
		std::vector<std::string> logoPath,
		bool errorOnNoLogo,
		std::string amt32bitPath,
		std::string chapterExePath,
		std::string joinLogoScpPath,
		std::string joinLogoScpCmdPath,
		HANDLE inPipe,
		HANDLE outPipe,
		bool dumpStreamInfo)
		: AMTObject(ctx)
		, tmpDir(ctx, workDir)
		, mode(mode)
		, modeArgs(modeArgs)
		, srcFilePath(srcFilePath)
		, outVideoPath(outVideoPath)
		, outInfoJsonPath(outInfoJsonPath)
		, filterScriptPath(filterScriptPath)
		, postFilterScriptPath(postFilterScriptPath)
		, encoder(encoder)
		, encoderPath(encoderPath)
		, encoderOptions(encoderOptions)
		, muxerPath(muxerPath)
		, timelineditorPath(timelineditorPath)
		, twoPass(twoPass)
		, autoBitrate(autoBitrate)
		, pulldown(pulldown)
		, bitrate(bitrate)
		, bitrateCM(bitrateCM)
		, serviceId(serviceId)
		, mpeg2decoder(mpeg2decoder)
		, h264decoder(h264decoder)
		, logoPath(logoPath)
		, errorOnNoLogo(errorOnNoLogo)
		, amt32bitPath(amt32bitPath)
		, chapterExePath(chapterExePath)
		, joinLogoScpPath(joinLogoScpPath)
		, joinLogoScpCmdPath(joinLogoScpCmdPath)
		, inPipe(inPipe)
		, outPipe(outPipe)
		, dumpStreamInfo(dumpStreamInfo)
	{
		//
	}

	~TranscoderSetting()
	{
		if (inPipe != INVALID_HANDLE_VALUE) CloseHandle(inPipe);
		if (outPipe != INVALID_HANDLE_VALUE) CloseHandle(outPipe);
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

	std::string getPostFilterScriptPath() const {
		return postFilterScriptPath;
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

	double getBitrateCM() const {
		return bitrateCM;
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

	const std::vector<std::string>& getLogoPath() const {
		return logoPath;
	}

	bool getErrorOnNoLogo() const {
		return errorOnNoLogo;
	}

	std::string get32bitPath() const {
		return amt32bitPath;
	}

	std::string getChapterExePath() const {
		return chapterExePath;
	}

	std::string getJoinLogoScpPath() const {
		return joinLogoScpPath;
	}

	std::string getJoinLogoScpCmdPath() const {
		return joinLogoScpCmdPath;
	}

	HANDLE getInPipe() const {
		return inPipe;
	}

	HANDLE getOutPipe() const {
		return outPipe;
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

	std::string getWaveFilePath() const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/audio.wav";
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

	std::string getTmpAMTSourcePath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/amts" << vindex << ".dat";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpSourceAVSPath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/amts" << vindex << ".avs";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpLogoFramePath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/logof" << vindex << ".txt";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpChapterExePath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/chapter_exe" << vindex << ".txt";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpChapterExeOutPath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/chapter_exe_o" << vindex << ".txt";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpTrimAVSPath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/trim" << vindex << ".avs";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpJlsPath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/jls" << vindex << ".txt";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpChapterPath(int vindex, int index) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/chapter" << vindex << "-" << index << ".txt";
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
		int pass, const std::vector<EncoderZone>& zones, int vindex, int index) const
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
				ss << " --vbv-bufsize " << (int)(targetBitrate * 2);
			}
		}
		if (pulldown) {
			ss << " --pdfile-in \"" << getEncPulldownFilePath(vindex, index) << "\"";
		}
		if (pass >= 0) {
			ss << " --pass " << pass;
			ss << " --stats \"" << getEncStatsFilePath(vindex, index) << "\"";
		}
		if (zones.size() && bitrateCM != 1.0 && encoder != ENCODER_QSVENC && encoder != ENCODER_NVENC) {
			ss << " --zones ";
			ss << std::setprecision(3);
			for (int i = 0; i < (int)zones.size(); ++i) {
				auto zone = zones[i];
				if (i > 0) ss << "/";
				ss << zone.startFrame << "," << zone.endFrame << ",b=" << bitrateCM;
			}
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
		ctx.info("errorOnNoLogo: %s", errorOnNoLogo ? "yes" : "no");
		ctx.info("フィルタ中間ファイル: %s", (inPipe != INVALID_HANDLE_VALUE) ? "yes" : "no");
		for (int i = 0; i < (int)logoPath.size(); ++i) {
			ctx.info("logo%d: %s", (i + 1), logoPath[i].c_str());
		}
		ctx.info("amt32bitPath: %s", amt32bitPath.c_str());
		ctx.info("chapterExePath: %s", chapterExePath.c_str());
		ctx.info("joinLogoScpPath: %s", joinLogoScpPath.c_str());
		ctx.info("joinLogoScpCmdPath: %s", joinLogoScpCmdPath.c_str());
		if (serviceId > 0) {
			ctx.info("ServiceId: %d", serviceId);
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
	std::string modeArgs; // テスト用
	// 入力ファイルパス（拡張子を含む）
	std::string srcFilePath;
	// 出力ファイルパス（拡張子を除く）
	std::string outVideoPath;
	// 結果情報JSON出力パス
	std::string outInfoJsonPath;
	std::string filterScriptPath;
	std::string postFilterScriptPath;
	// エンコーダ設定
	ENUM_ENCODER encoder;
	std::string encoderPath;
	std::string encoderOptions;
	std::string muxerPath;
	std::string timelineditorPath;
	bool twoPass;
	bool autoBitrate;
	bool pulldown; // 新バージョンではサポートしない
	BitrateSetting bitrate;
	double bitrateCM;
	int serviceId;
	DECODER_TYPE mpeg2decoder;
	DECODER_TYPE h264decoder;
	// CM解析用設定
	std::vector<std::string> logoPath;
	bool errorOnNoLogo;
	std::string amt32bitPath;
	std::string chapterExePath;
	std::string joinLogoScpPath;
	std::string joinLogoScpCmdPath;
	// フィルタ処理分離用
	HANDLE inPipe;
	HANDLE outPipe;
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

