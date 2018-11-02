/**
* Amtasukaze Transcode Setting
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>

#include "StreamUtils.hpp"

// カラースペース定義を使うため
#include "libavutil/pixfmt.h"

struct EncoderZone {
	int startFrame;
	int endFrame;
};

struct BitrateZone : EncoderZone {
	double bitrate;

	BitrateZone()
		: EncoderZone()
		, bitrate()
	{ }
	BitrateZone(EncoderZone zone)
		: EncoderZone(zone)
		, bitrate()
	{ }
	BitrateZone(EncoderZone zone, double bitrate)
		: EncoderZone(zone)
		, bitrate(bitrate)
	{ }
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

enum ENUM_FORMAT {
	FORMAT_MP4,
	FORMAT_MKV,
	FORMAT_M2TS,
	FORMAT_TS,
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

static tstring makeEncoderArgs(
	ENUM_ENCODER encoder,
	const tstring& binpath,
	const tstring& options,
	const VideoFormat& fmt,
	const tstring& timecodepath,
	bool is120fps,
	const tstring& outpath)
{
	StringBuilderT sb;

	sb.append(_T("\"%s\""), binpath);

	// y4mヘッダにあるので必要ない
	//ss << " --fps " << fmt.frameRateNum << "/" << fmt.frameRateDenom;
	//ss << " --input-res " << fmt.width << "x" << fmt.height;
	//ss << " --sar " << fmt.sarWidth << ":" << fmt.sarHeight;

	if (fmt.colorPrimaries != AVCOL_PRI_UNSPECIFIED) {
		sb.append(_T(" --colorprim %s"), av::getColorPrimStr(fmt.colorPrimaries));
	}
	if (fmt.transferCharacteristics != AVCOL_TRC_UNSPECIFIED) {
		sb.append(_T(" --transfer %s"), av::getTransferCharacteristicsStr(fmt.transferCharacteristics));
	}
	if (fmt.colorSpace != AVCOL_TRC_UNSPECIFIED) {
		sb.append(_T(" --colormatrix %s"), av::getColorSpaceStr(fmt.colorSpace));
	}

	// インターレース
	switch (encoder) {
	case ENCODER_X264:
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		sb.append(fmt.progressive ? _T("") : _T(" --tff"));
		break;
	case ENCODER_X265:
		//sb.append(fmt.progressive ? " --no-interlace" : " --interlace tff");
		if (fmt.progressive == false) {
			THROW(ArgumentException, "HEVCのインターレース出力には対応していません");
		}
		break;
	}

	sb.append(_T(" %s -o \"%s\""), options, outpath);

	// 入力形式
	switch (encoder) {
	case ENCODER_X264:
		sb.append(_T(" --stitchable"))
			.append(_T(" --demuxer y4m -"));
		break;
	case ENCODER_X265:
		sb.append(_T(" --no-opt-qp-pps --no-opt-ref-list-length-pps"))
			.append(_T(" --y4m --input -"));
		break;
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		sb.append(_T(" --format raw --y4m -i -"));
		break;
	}

	if (timecodepath.size() > 0 && encoder == ENCODER_X264) {
		std::pair<int, int> timebase = std::make_pair(fmt.frameRateNum * (is120fps ? 4 : 2), fmt.frameRateDenom);
		sb.append(_T(" --tcfile-in \"%s\" --timebase %d/%d"), timecodepath, timebase.second, timebase.first);
	}

	return sb.str();
}

static std::vector<std::pair<tstring, bool>> makeMuxerArgs(
	ENUM_FORMAT format,
	const tstring& binpath,
	const tstring& timelineeditorpath,
	const tstring& mp4boxpath,
	const tstring& inVideo,
	const VideoFormat& videoFormat,
	const std::vector<tstring>& inAudios,
	const tstring& outpath,
	const tstring& tmpoutpath,
	const tstring& chapterpath,
	const tstring& timecodepath,
	std::pair<int, int> timebase,
	const std::vector<tstring>& inSubs,
	const std::vector<tstring>& subsTitles,
	const tstring& metapath)
{
	std::vector<std::pair<tstring, bool>> ret;

	StringBuilderT sb;
	sb.append(_T("\"%s\""), binpath);

	if (format == FORMAT_MP4) {
		bool needChapter = (chapterpath.size() > 0);
		bool needTimecode = (timecodepath.size() > 0);
		bool needSubs = (inSubs.size() > 0);

		// まずはmuxerで映像、音声、チャプターをmux
		if (videoFormat.fixedFrameRate) {
			sb.append(_T(" -i \"%s?fps=%d/%d\""), inVideo,
				videoFormat.frameRateNum, videoFormat.frameRateDenom);
		}
		else {
			sb.append(_T(" -i \"%s\""), inVideo);
		}
		for (const auto& inAudio : inAudios) {
			sb.append(_T(" -i \"%s\""), inAudio);
		}
		// timelineeditorがチャプターを消すのでtimecodeがある時はmp4boxで入れる
		if (needChapter && !needTimecode) {
			sb.append(_T(" --chapter \"%s\""), chapterpath);
			needChapter = false;
		}
		sb.append(_T(" --optimize-pd"));

		tstring dst = needTimecode ? tmpoutpath : outpath;
		sb.append(_T(" -o \"%s\""), dst);

		ret.push_back(std::make_pair(sb.str(), false));
		sb.clear();

		if (needTimecode) {
			// 必要ならtimelineeditorでtimecodeを埋め込む
			sb.append(_T("\"%s\""), timelineeditorpath)
				.append(_T(" --track 1"))
				.append(_T(" --timecode \"%s\""), timecodepath)
				.append(_T(" --media-timescale %d"), timebase.first)
				.append(_T(" --media-timebase %d"), timebase.second)
				.append(_T(" \"%s\""), dst)
				.append(_T(" \"%s\""), outpath);
			ret.push_back(std::make_pair(sb.str(), false));
			sb.clear();
			needTimecode = false;
		}

		if (needChapter || needSubs) {
			// 字幕とチャプターを埋め込む
			sb.append(_T("\"%s\""), mp4boxpath);
			for (int i = 0; i < (int)inSubs.size(); ++i) {
				if (subsTitles[i] == _T("SRT")) { // mp4はSRTのみ
					sb.append(_T(" -add \"%s#:name=%s\""), inSubs[i], subsTitles[i]);
				}
			}
			needSubs = false;
			// timecodeがある場合はこっちでチャプターを入れる
			if (needChapter) {
				sb.append(_T(" -chap \"%s\""), chapterpath);
				needChapter = false;
			}
			sb.append(_T(" \"%s\""), outpath);
			ret.push_back(std::make_pair(sb.str(), true));
			sb.clear();
		}
	}
	else if (format == FORMAT_MKV) {

		if (chapterpath.size() > 0) {
			sb.append(_T(" --chapters \"%s\""), chapterpath);
		}

		sb.append(_T(" -o \"%s\""), outpath);

		if (timecodepath.size()) {
			sb.append(_T(" --timestamps \"0:%s\""), timecodepath);
		}
		sb.append(_T(" \"%s\""), inVideo);

		for (const auto& inAudio : inAudios) {
			sb.append(_T(" \"%s\""), inAudio);
		}
		for (int i = 0; i < (int)inSubs.size(); ++i) {
			sb.append(_T(" --track-name \"0:%s\" \"%s\""), subsTitles[i], inSubs[i]);
		}

		ret.push_back(std::make_pair(sb.str(), true));
		sb.clear();
	}
	else { // M2TS or TS
		sb.append(_T(" \"%s\" \"%s\""), metapath, outpath);
		ret.push_back(std::make_pair(sb.str(), true));
		sb.clear();
	}

	return ret;
}

static tstring makeTimelineEditorArgs(
	const tstring& binpath,
	const tstring& inpath,
	const tstring& outpath,
	const tstring& timecodepath)
{
	StringBuilderT sb;
	sb.append(_T("\"%s\""), binpath)
		.append(_T(" --track 1"))
		.append(_T(" --timecode \"%s\""), timecodepath)
		.append(_T(" \"%s\""), inpath)
		.append(_T(" \"%s\""), outpath);
	return sb.str();
}

static const char* cmOutMaskToString(int outmask) {
	switch (outmask)
	{
	case 1: return "通常";
	case 2: return "CMをカット";
	case 3: return "通常出力とCMカット出力";
	case 4: return "CMのみ";
	case 5: return "通常出力とCM出力";
	case 6: return "本編とCMを分離";
	case 7: return "通常,本編,CM全出力";
	}
	return "不明";
}

enum AMT_CLI_MODE {
	AMT_CLI_TS,
	AMT_CLI_GENERIC,
};

class TempDirectory : AMTObject, NonCopyable
{
public:
	TempDirectory(AMTContext& ctx, const tstring& tmpdir, bool noRemoveTmp)
		: AMTObject(ctx)
		, path_(tmpdir)
		, initialized_(false)
		, noRemoveTmp_(noRemoveTmp)
	{ }
	~TempDirectory() {
		if (!initialized_ || noRemoveTmp_) {
			return;
		}
		// 一時ファイルを削除
		ctx.clearTmpFiles();
		// ディレクトリ削除
		if (rmdirT(path_.c_str()) != 0) {
			ctx.warnF("一時ディレクトリ削除に失敗: ", path_);
		}
	}

	void Initialize() {
		if (initialized_) return;

		for (int code = (int)time(NULL) & 0xFFFFFF; code > 0; ++code) {
			auto path = genPath(path_, code);
			if (mkdirT(path.c_str()) == 0) {
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

		tstring abolutePath;
		int sz = GetFullPathNameW(path_.c_str(), 0, 0, 0);
		abolutePath.resize(sz);
		GetFullPathNameW(path_.c_str(), sz, &abolutePath[0], 0);
		abolutePath.resize(sz - 1);
		path_ = abolutePath;
		initialized_ = true;
	}

	tstring path() const {
		if (!initialized_) {
			THROW(InvalidOperationException, "一時ディレクトリを作成していません");
		}
		return path_;
	}

private:
	tstring path_;
	bool initialized_;
	bool noRemoveTmp_;

	tstring genPath(const tstring& base, int code)
	{
		return StringFormat(_T("%s/amt%d"), base, code);
	}
};

static const char* GetCMSuffix(CMType cmtype) {
	switch (cmtype) {
	case CMTYPE_CM: return "-cm";
	case CMTYPE_NONCM: return "-main";
	case CMTYPE_BOTH: return "";
	}
	return "";
}

static const char* GetNicoJKSuffix(NicoJKType type) {
	switch (type) {
	case NICOJK_720S: return "-720S";
	case NICOJK_720T: return "-720T";
	case NICOJK_1080S: return "-1080S";
	case NICOJK_1080T: return "-1080T";
	}
	return "";
}

struct Config {
	// 一時フォルダ
	tstring workDir;
	tstring mode;
	tstring modeArgs; // テスト用
	// 入力ファイルパス（拡張子を含む）
	tstring srcFilePath;
	// 出力ファイルパス（拡張子を除く）
	tstring outVideoPath;
	// 結果情報JSON出力パス
	tstring outInfoJsonPath;
	// DRCSマッピングファイルパス
	tstring drcsMapPath;
	tstring drcsOutPath;
	// フィルタパス
	tstring filterScriptPath;
	tstring postFilterScriptPath;
	// エンコーダ設定
	ENUM_ENCODER encoder;
	tstring encoderPath;
	tstring encoderOptions;
	tstring muxerPath;
	tstring timelineditorPath;
	tstring mp4boxPath;
	tstring nicoConvAssPath;
	tstring nicoConvChSidPath;
	ENUM_FORMAT format;
	bool splitSub;
	bool twoPass;
	bool autoBitrate;
	bool chapter;
	bool subtitles;
	int nicojkmask;
	bool nicojk18;
	bool useNicoJKLog;
	BitrateSetting bitrate;
	double bitrateCM;
	double x265TimeFactor;
	int serviceId;
	DecoderSetting decoderSetting;
	// CM解析用設定
	std::vector<tstring> logoPath;
	bool ignoreNoLogo;
	bool ignoreNoDrcsMap;
	bool ignoreNicoJKError;
	double pmtCutSideRate[2];
	bool looseLogoDetection;
	bool noDelogo;
	bool vfr120fps;
	tstring chapterExePath;
	tstring chapterExeOptions;
	tstring joinLogoScpPath;
	tstring joinLogoScpCmdPath;
	tstring joinLogoScpOptions;
	int cmoutmask;
	tstring trimavsPath;
	// 検出モード用
	int maxframes;
	// ホストプロセスとの通信用
	HANDLE inPipe;
	HANDLE outPipe;
	int affinityGroup;
	uint64_t affinityMask;
	// デバッグ用設定
	bool dumpStreamInfo;
	bool systemAvsPlugin;
	bool noRemoveTmp;
	bool dumpFilter;
};

class ConfigWrapper : public AMTObject
{
public:
	ConfigWrapper(
		AMTContext& ctx,
		const Config& conf)
		: AMTObject(ctx)
		, conf(conf)
		, tmpDir(ctx, conf.workDir, conf.noRemoveTmp)
	{
		for (int cmtypei = 0; cmtypei < CMTYPE_MAX; ++cmtypei) {
			if (conf.cmoutmask & (1 << cmtypei)) {
				cmtypes.push_back((CMType)cmtypei);
			}
		}
		for (int nicotypei = 0; nicotypei < NICOJK_MAX; ++nicotypei) {
			if (conf.nicojkmask & (1 << nicotypei)) {
				nicojktypes.push_back((NicoJKType)nicotypei);
			}
		}
	}

	tstring getMode() const {
		return conf.mode;
	}

	tstring getModeArgs() const {
		return conf.modeArgs;
	}

	tstring getSrcFilePath() const {
		return conf.srcFilePath;
	}

	tstring getOutInfoJsonPath() const {
		return conf.outInfoJsonPath;
	}

	tstring getFilterScriptPath() const {
		return conf.filterScriptPath;
	}

	tstring getPostFilterScriptPath() const {
		return conf.postFilterScriptPath;
	}

	ENUM_ENCODER getEncoder() const {
		return conf.encoder;
	}

	tstring getEncoderPath() const {
		return conf.encoderPath;
	}

	tstring getEncoderOptions() const {
		return conf.encoderOptions;
	}

	ENUM_FORMAT getFormat() const {
		return conf.format;
	}

	bool isFormatVFRSupported() const {
		return conf.format != FORMAT_M2TS && conf.format != FORMAT_TS;
	}

	tstring getMuxerPath() const {
		return conf.muxerPath;
	}

	tstring getTimelineEditorPath() const {
		return conf.timelineditorPath;
	}

	tstring getMp4BoxPath() const {
		return conf.mp4boxPath;
	}

	tstring getNicoConvAssPath() const {
		return conf.nicoConvAssPath;
	}

	tstring getNicoConvChSidPath() const {
		return conf.nicoConvChSidPath;
	}

	bool isSplitSub() const {
		return conf.splitSub;
	}

	bool isTwoPass() const {
		return conf.twoPass;
	}

	bool isAutoBitrate() const {
		return conf.autoBitrate;
	}

	bool isChapterEnabled() const {
		return conf.chapter;
	}

	bool isSubtitlesEnabled() const {
		return conf.subtitles;
	}

	bool isNicoJKEnabled() const {
		return conf.nicojkmask != 0;
	}

	bool isNicoJK18Enabled() const {
		return conf.nicojk18;
	}

	bool isUseNicoJKLog() const {
		return conf.useNicoJKLog;
	}

	int getNicoJKMask() const {
		return conf.nicojkmask;
	}

	BitrateSetting getBitrate() const {
		return conf.bitrate;
	}

	double getBitrateCM() const {
		return conf.bitrateCM;
	}

	double getX265TimeFactor() const {
		return conf.x265TimeFactor;
	}

	int getServiceId() const {
		return conf.serviceId;
	}

	DecoderSetting getDecoderSetting() const {
		return conf.decoderSetting;
	}

	const std::vector<tstring>& getLogoPath() const {
		return conf.logoPath;
	}

	bool isIgnoreNoLogo() const {
		return conf.ignoreNoLogo;
	}

	bool isIgnoreNoDrcsMap() const {
		return conf.ignoreNoDrcsMap;
	}

	bool isIgnoreNicoJKError() const {
		return conf.ignoreNicoJKError;
	}

	bool isPmtCutEnabled() const {
		return conf.pmtCutSideRate[0] > 0 || conf.pmtCutSideRate[1] > 0;
	}

	const double* getPmtCutSideRate() const {
		return conf.pmtCutSideRate;
	}

	bool isLooseLogoDetection() const {
		return conf.looseLogoDetection;
	}

	bool isNoDelogo() const {
		return conf.noDelogo;
	}

	bool isVFR120fps() const {
		return conf.vfr120fps;
	}

	tstring getChapterExePath() const {
		return conf.chapterExePath;
	}

	tstring getChapterExeOptions() const {
		return conf.chapterExeOptions;
	}

	tstring getJoinLogoScpPath() const {
		return conf.joinLogoScpPath;
	}

	tstring getJoinLogoScpCmdPath() const {
		return conf.joinLogoScpCmdPath;
	}

	tstring getJoinLogoScpOptions() const {
		return conf.joinLogoScpOptions;
	}

	tstring getTrimAVSPath() const {
		return conf.trimavsPath;
	}

	const std::vector<CMType>& getCMTypes() const {
		return cmtypes;
	}

	const std::vector<NicoJKType>& getNicoJKTypes() const {
		return nicojktypes;
	}

	int getMaxFrames() const {
		return conf.maxframes;
	}

	HANDLE getInPipe() const {
		return conf.inPipe;
	}

	HANDLE getOutPipe() const {
		return conf.outPipe;
	}

	int getAffinityGroup() const {
		return conf.affinityGroup;
	}

	uint64_t getAffinityMask() const {
		return conf.affinityMask;
	}

	bool isDumpStreamInfo() const {
		return conf.dumpStreamInfo;
	}

	bool isSystemAvsPlugin() const {
		return conf.systemAvsPlugin;
	}

	tstring getAudioFilePath() const {
		return regtmp(StringFormat(_T("%s/audio.dat"), tmpDir.path()));
	}

	tstring getWaveFilePath() const {
		return regtmp(StringFormat(_T("%s/audio.wav"), tmpDir.path()));
	}

	tstring getIntVideoFilePath(int index) const {
		return regtmp(StringFormat(_T("%s/i%d.mpg"), tmpDir.path(), index));
	}

	tstring getStreamInfoPath() const {
		return conf.outVideoPath + _T("-streaminfo.dat");
	}

	tstring getEncVideoFilePath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat(_T("%s/v%d-%d%s.raw"), tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	tstring getTimecodeFilePath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat(_T("%s/v%d-%d%s.timecode.txt"), tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	tstring getAvsTmpPath(int vindex, int index, CMType cmtype) const {
		auto str = StringFormat(_T("%s/v%d-%d%s.avstmp"), tmpDir.path(), vindex, index, GetCMSuffix(cmtype));
		ctx.registerTmpFile(str);
		// KFMCycleAnalyzeのデバッグダンプファイルも追加
		ctx.registerTmpFile(str + _T(".debug"));
		return str;
	}

	tstring getFilterAvsPath(int vindex, int index, CMType cmtype) const {
		auto str = StringFormat(_T("%s/vfilter%d-%d%s.avs"), tmpDir.path(), vindex, index, GetCMSuffix(cmtype));
		ctx.registerTmpFile(str);
		return str;
	}

	tstring getEncStatsFilePath(int vindex, int index, CMType cmtype) const
	{
		auto str = StringFormat(_T("%s/s%d-%d%s.log"), tmpDir.path(), vindex, index, GetCMSuffix(cmtype));
		ctx.registerTmpFile(str);
		// x264は.mbtreeも生成するので
		ctx.registerTmpFile(str + _T(".mbtree"));
		// x265は.cutreeも生成するので
		ctx.registerTmpFile(str + _T(".cutree"));
		return str;
	}

	tstring getIntAudioFilePath(int vindex, int index, int aindex, CMType cmtype) const {
		return regtmp(StringFormat(_T("%s/a%d-%d-%d%s.aac"),
			tmpDir.path(), vindex, index, aindex, GetCMSuffix(cmtype)));
	}

	tstring getTmpASSFilePath(int vindex, int index, int langindex, CMType cmtype) const {
		return regtmp(StringFormat(_T("%s/c%d-%d-%d%s.ass"),
			tmpDir.path(), vindex, index, langindex, GetCMSuffix(cmtype)));
	}

	tstring getTmpSRTFilePath(int vindex, int index, int langindex, CMType cmtype) const {
		return regtmp(StringFormat(_T("%s/c%d-%d-%d%s.srt"),
			tmpDir.path(), vindex, index, langindex, GetCMSuffix(cmtype)));
	}

	tstring getTmpAMTSourcePath(int vindex) const {
		return regtmp(StringFormat(_T("%s/amts%d.dat"), tmpDir.path(), vindex));
	}

	tstring getTmpSourceAVSPath(int vindex) const {
		return regtmp(StringFormat(_T("%s/amts%d.avs"), tmpDir.path(), vindex));
	}

	tstring getTmpLogoFramePath(int vindex) const {
		return regtmp(StringFormat(_T("%s/logof%d.txt"), tmpDir.path(), vindex));
	}

	tstring getTmpChapterExePath(int vindex) const {
		return regtmp(StringFormat(_T("%s/chapter_exe%d.txt"), tmpDir.path(), vindex));
	}

	tstring getTmpChapterExeOutPath(int vindex) const {
		return regtmp(StringFormat(_T("%s/chapter_exe_o%d.txt"), tmpDir.path(), vindex));
	}

	tstring getTmpTrimAVSPath(int vindex) const {
		return regtmp(StringFormat(_T("%s/trim%d.avs"), tmpDir.path(), vindex));
	}

	tstring getTmpJlsPath(int vindex) const {
		return regtmp(StringFormat(_T("%s/jls%d.txt"), tmpDir.path(), vindex));
	}

	tstring getTmpChapterPath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat(_T("%s/chapter%d-%d%s.txt"),
			tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	tstring getTmpNicoJKXMLPath() const {
		return regtmp(StringFormat(_T("%s/nicojk.xml"), tmpDir.path()));
	}

	tstring getTmpNicoJKASSPath(NicoJKType type) const {
		return regtmp(StringFormat(_T("%s/nicojk%s.ass"), tmpDir.path(), GetNicoJKSuffix(type)));
	}

	tstring getTmpNicoJKASSPath(int vindex, int index, CMType cmtype, NicoJKType type) const {
		return regtmp(StringFormat(_T("%s/nicojk%d-%d%s%s.ass"),
			tmpDir.path(), vindex, index, GetCMSuffix(cmtype), GetNicoJKSuffix(type)));
	}

	tstring getVfrTmpFilePath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat(_T("%s/t%d-%d%s.%s"),
			tmpDir.path(), vindex, index, GetCMSuffix(cmtype), getOutputExtention()));
	}

	tstring getM2tsMetaFilePath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat(_T("%s/t%d-%d%s.meta"), tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	const char* getOutputExtention() const {
		switch (conf.format) {
		case FORMAT_MP4: return "mp4";
		case FORMAT_MKV: return "mkv";
		case FORMAT_M2TS: return "m2ts";
		case FORMAT_TS: return "ts";
		}
		return "amatsukze";
	}

	tstring getOutFilePath(int index, CMType cmtype) const {
		StringBuilderT sb;
		sb.append(_T("%s"), conf.outVideoPath);
		if (index != 0) {
			sb.append(_T("-%d"), index);
		}
		sb.append(_T("%s.%s"), GetCMSuffix(cmtype), getOutputExtention());
		return sb.str();
	}

	tstring getOutASSPath(int index, int langidx, CMType cmtype, NicoJKType jktype) const {
		StringBuilderT sb;
		sb.append(_T("%s"), conf.outVideoPath);
		if (index != 0) {
			sb.append(_T("-%d"), index);
		}
		sb.append(_T("%s"), GetCMSuffix(cmtype));
		if (langidx < 0) {
			sb.append(_T("-nicojk%s"), GetNicoJKSuffix(jktype));
		}
		else if (langidx > 0) {
			sb.append(_T("-%d"), langidx);
		}
		sb.append(_T(".ass"));
		return sb.str();
	}

	tstring getOutSummaryPath() const {
		return StringFormat(_T("%s.txt"), conf.outVideoPath);
	}

	tstring getDRCSMapPath() const {
		return conf.drcsMapPath;
	}

	tstring getDRCSOutPath(const std::string& md5) const {
		return StringFormat(_T("%s\\%s.bmp"), conf.drcsOutPath, md5);
	}

	bool isDumpFilter() const {
		return conf.dumpFilter;
	}

	tstring getFilterGraphDumpPath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat(_T("%s/graph%d-%d%s.txt"), tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	bool isZoneAvailable() const {
		return conf.encoder != ENCODER_QSVENC && conf.encoder != ENCODER_NVENC;
	}

	bool isEncoderSupportVFR() const {
		return conf.encoder == ENCODER_X264;
	}

	bool isBitrateCMEnabled() const {
		return conf.bitrateCM != 1.0 && isZoneAvailable();
	}

	bool isAdjustBitrateVFR() const {
		return true;
	}

	tstring getOptions(
		int numFrames,
		VIDEO_STREAM_FORMAT srcFormat, double srcBitrate, bool pulldown,
		int pass, const std::vector<BitrateZone>& zones, double vfrBitrateScale,
		int vindex, int index, CMType cmtype) const
	{
		StringBuilderT sb;
		sb.append(_T("%s"), conf.encoderOptions);
		if (conf.autoBitrate) {
			double targetBitrate = conf.bitrate.getTargetBitrate(srcFormat, srcBitrate);
			if (isEncoderSupportVFR() == false && isAdjustBitrateVFR()) {
				// タイムコード非対応エンコーダにおけるビットレートのVFR調整
				targetBitrate *= vfrBitrateScale;
			}
			double maxBitrate = std::max(targetBitrate * 2, srcBitrate);
			if (cmtype == CMTYPE_CM) {
				targetBitrate *= conf.bitrateCM;
			}
			if (conf.encoder == ENCODER_QSVENC) {
				sb.append(_T(" --la %d --maxbitrate %d"), (int)targetBitrate, (int)maxBitrate);
			}
			else if (conf.encoder == ENCODER_NVENC) {
				sb.append(_T(" --vbrhq %d --maxbitrate %d"), (int)targetBitrate, (int)maxBitrate);
			}
			else {
				sb.append(_T(" --bitrate %d --vbv-maxrate %d --vbv-bufsize %d"),
					(int)targetBitrate, (int)maxBitrate, (int)maxBitrate);
			}
		}
		if (pass >= 0) {
			sb.append(_T(" --pass %d --stats \"%s\""),
				pass, getEncStatsFilePath(vindex, index, cmtype));
		}
		if (zones.size() &&
			isZoneAvailable() &&
			((isEncoderSupportVFR() == false && isAdjustBitrateVFR()) || isBitrateCMEnabled()))
		{
			sb.append(_T(" --zones "));
			for (int i = 0; i < (int)zones.size(); ++i) {
				auto zone = zones[i];
				sb.append(_T("%s%d,%d,b=%.3g"), (i > 0) ? "/" : "",
					zone.startFrame, zone.endFrame - 1, zone.bitrate);
			}
		}
		if (conf.encoder == ENCODER_X264 || conf.encoder == ENCODER_X265) {
			if (numFrames > 0) {
				sb.append(_T(" --frames %d"), numFrames);
			}
		}
		return sb.str();
	}

	void dump() const {
		ctx.info("[設定]");
		if (conf.mode != _T("ts")) {
			ctx.infoF("Mode: %s", conf.mode);
		}
		ctx.infoF("入力: %s", conf.srcFilePath);
		ctx.infoF("出力: %s", conf.outVideoPath);
		ctx.infoF("一時フォルダ: %s", tmpDir.path());
		ctx.infoF("出力フォーマット: %s", formatToString(conf.format));
		ctx.infoF("エンコーダ: %s (%s)", conf.encoderPath, encoderToString(conf.encoder));
		ctx.infoF("エンコーダオプション: %s", conf.encoderOptions);
		if (conf.autoBitrate) {
			ctx.infoF("自動ビットレート: 有効 (%g:%g:%g)",
				conf.bitrate.a, conf.bitrate.b, conf.bitrate.h264);
		}
		else {
			ctx.info("自動ビットレート: 無効");
		}
		ctx.infoF("エンコード/出力: %s/%s",
			conf.twoPass ? "2パス" : "1パス",
			cmOutMaskToString(conf.cmoutmask));
		ctx.infoF("チャプター解析: %s%s",
			conf.chapter ? "有効" : "無効",
			(conf.chapter && conf.ignoreNoLogo) ? "" : "（ロゴ必須）");
		if (conf.chapter) {
			for (int i = 0; i < (int)conf.logoPath.size(); ++i) {
				ctx.infoF("logo%d: %s", (i + 1), conf.logoPath[i]);
			}
			ctx.infoF("ロゴ消し: %s", conf.noDelogo ? "しない" : "する");
		}
		ctx.infoF("字幕: %s", conf.subtitles ? "有効" : "無効");
		if (conf.subtitles) {
			ctx.infoF("DRCSマッピング: %s", conf.drcsMapPath);
		}
		if (conf.serviceId > 0) {
			ctx.infoF("サービスID: %d", conf.serviceId);
		}
		else {
			ctx.info("サービスID: 指定なし");
		}
		ctx.infoF("デコーダ: MPEG2:%s H264:%s",
			decoderToString(conf.decoderSetting.mpeg2),
			decoderToString(conf.decoderSetting.h264));
	}

	void CreateTempDir() {
		tmpDir.Initialize();
	}

private:
	Config conf;
	TempDirectory tmpDir;
	std::vector<CMType> cmtypes;
	std::vector<NicoJKType> nicojktypes;

	const char* decoderToString(DECODER_TYPE decoder) const {
		switch (decoder) {
		case DECODER_QSV: return "QSV";
		case DECODER_CUVID: return "CUVID";
		}
		return "default";
	}

	const char* formatToString(ENUM_FORMAT fmt) const {
		switch (fmt) {
		case FORMAT_MP4: return "MP4";
		case FORMAT_MKV: return "Matroska";
		case FORMAT_M2TS: return "M2TS";
		case FORMAT_TS: return "TS";
		}
		return "unknown";
	}

	tstring regtmp(tstring str) const {
		ctx.registerTmpFile(str);
		return str;
	}
};

class OutPathGenerator {
public:
	OutPathGenerator(const ConfigWrapper& setting, int index, CMType cmtype)
		: setting_(setting)
		, index_(index)
		, cmtype_(cmtype)
	{ }
	tstring getOutFilePath() const {
		return setting_.getOutFilePath(index_, cmtype_);
	}
	tstring getOutASSPath(int langidx, NicoJKType jktype) const {
		return setting_.getOutASSPath(index_, langidx, cmtype_, jktype);
	}
private:
	const ConfigWrapper& setting_;
	int index_;
	CMType cmtype_;
};
