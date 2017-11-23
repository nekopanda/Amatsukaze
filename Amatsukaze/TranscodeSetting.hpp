/**
* Amtasukaze Transcode Setting
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
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
	StringBuilder sb;

	sb.append("\"%s\"", binpath);

	// y4mヘッダにあるので必要ない
	//ss << " --fps " << fmt.frameRateNum << "/" << fmt.frameRateDenom;
	//ss << " --input-res " << fmt.width << "x" << fmt.height;
	//ss << " --sar " << fmt.sarWidth << ":" << fmt.sarHeight;

	if (fmt.colorPrimaries != AVCOL_PRI_UNSPECIFIED) {
		sb.append(" --colorprim %s", av::getColorPrimStr(fmt.colorPrimaries));
	}
	if (fmt.transferCharacteristics != AVCOL_TRC_UNSPECIFIED) {
		sb.append(" --transfer %s", av::getTransferCharacteristicsStr(fmt.transferCharacteristics));
	}
	if (fmt.colorSpace != AVCOL_TRC_UNSPECIFIED) {
		sb.append(" --colormatrix %s", av::getColorSpaceStr(fmt.colorSpace));
	}

	// インターレース
	switch (encoder) {
	case ENCODER_X264:
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		sb.append(fmt.progressive ? "" : " --tff");
		break;
	case ENCODER_X265:
		sb.append(fmt.progressive ? " --no-interlace" : " --interlace tff");
		break;
	}

	sb.append(" %s -o \"%s\"", options, outpath);

	// 入力形式
	switch (encoder) {
	case ENCODER_X264:
		sb.append(" --stitchable")
			.append(" --demuxer y4m -");
		break;
	case ENCODER_X265:
		sb.append(" --no-opt-qp-pps --no-opt-ref-list-length-pps")
			.append(" --y4m --input -");
		break;
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		sb.append(" --format raw --y4m -i -");
		break;
	}

	return sb.str();
}

static std::string makeMuxerArgs(
	const std::string& binpath,
	const std::string& inVideo,
	const VideoFormat& videoFormat,
	const std::vector<std::string>& inAudios,
	const std::string& outpath,
	const std::string& chapterpath)
{
	StringBuilder sb;

	sb.append("\"%s\"", binpath);
	if (videoFormat.fixedFrameRate) {
		sb.append(" -i \"%s?fps=%d/%d\"", inVideo,
			videoFormat.frameRateNum, videoFormat.frameRateDenom);
	}
	else {
		sb.append(" -i \"%s\"", inVideo);
	}
	for (const auto& inAudio : inAudios) {
		sb.append(" -i \"%s\"", inAudio);
	}
	if (chapterpath.size() > 0) {
		sb.append(" --chapter \"%s\"", chapterpath);
	}
	sb.append(" --optimize-pd");
	sb.append(" -o \"%s\"", outpath);

	return sb.str();
}

static std::string makeTimelineEditorArgs(
	const std::string& binpath,
	const std::string& inpath,
	const std::string& outpath,
	const std::string& timecodepath,
	std::pair<int, int> timebase)
{
	StringBuilder sb;
	sb.append("\"%s\"", binpath)
		.append(" --track 1")
		.append(" --timecode \"%s\"", timecodepath)
		.append(" --media-timescale %d", timebase.first)
		.append(" --media-timebase %d", timebase.second)
		.append(" \"%s\"", inpath)
		.append(" \"%s\"", outpath);
	return sb.str();
}

static const char* cmOutMaskToString(int outmask) {
	switch (outmask)
	{
	case 1: return "通常";
	case 2: return "CMをカット";
	case 3: return "通常+CMカット";
	case 4: return "CMのみ";
	case 5: return "通常+CM";
	case 6: return "本編とCMを分離";
	case 7: return "通常+本編+CM";
	}
	return "不明";
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
		if (tmpdir.size() == 0) {
			// 指定がなければ作らない
			return;
		}
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
		if (path_.size() == 0) {
			return;
		}
		// 一時ファイルを削除
		ctx.clearTmpFiles();
		// ディレクトリ削除
		if (_rmdir(path_.c_str()) != 0) {
			ctx.warn("一時ディレクトリ削除に失敗: ", path_.c_str());
		}
	}

	std::string path() const {
		if (path_.size() == 0) {
			THROW(RuntimeException, "一時フォルダの指定がありません");
		}
		return path_;
	}

private:
	std::string path_;

	std::string genPath(const std::string& base, int code)
	{
		return StringFormat("%s/amt%d", base, code);
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
		std::string drcsOutPath,
		std::string filterScriptPath,
		std::string postFilterScriptPath,
		ENUM_ENCODER encoder,
		std::string encoderPath,
		std::string encoderOptions,
		std::string muxerPath,
		std::string timelineditorPath,
		bool twoPass,
		bool autoBitrate,
		bool chapter,
		BitrateSetting bitrate,
		double bitrateCM,
		int serviceId,
		DecoderSetting decoderSetting,
		std::vector<std::string> logoPath,
		bool errorOnNoLogo,
		std::string chapterExePath,
		std::string joinLogoScpPath,
		std::string joinLogoScpCmdPath,
    int cmoutmask,
		bool dumpStreamInfo,
		bool systemAvsPlugin)
		: AMTObject(ctx)
		, tmpDir(ctx, workDir)
		, mode(mode)
		, modeArgs(modeArgs)
		, srcFilePath(srcFilePath)
		, outVideoPath(outVideoPath)
		, outInfoJsonPath(outInfoJsonPath)
		, drcsOutPath(drcsOutPath)
		, filterScriptPath(filterScriptPath)
		, postFilterScriptPath(postFilterScriptPath)
		, encoder(encoder)
		, encoderPath(encoderPath)
		, encoderOptions(encoderOptions)
		, muxerPath(muxerPath)
		, timelineditorPath(timelineditorPath)
		, twoPass(twoPass)
		, autoBitrate(autoBitrate)
		, chapter(chapter)
		, bitrate(bitrate)
		, bitrateCM(bitrateCM)
		, serviceId(serviceId)
		, decoderSetting(decoderSetting)
		, logoPath(logoPath)
		, errorOnNoLogo(errorOnNoLogo)
		, chapterExePath(chapterExePath)
		, joinLogoScpPath(joinLogoScpPath)
		, joinLogoScpCmdPath(joinLogoScpCmdPath)
    , cmoutmask(cmoutmask)
		, dumpStreamInfo(dumpStreamInfo)
		, systemAvsPlugin(systemAvsPlugin)
	{
		for (int cmtypei = 0; cmtypei < CMTYPE_MAX; ++cmtypei) {
			if (cmoutmask & (1 << cmtypei)) {
				cmtypes.push_back((CMType)cmtypei);
			}
		}
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

	bool isChapterEnabled() const {
		return chapter;
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

	DecoderSetting getDecoderSetting() const {
		return decoderSetting;
	}

	const std::vector<std::string>& getLogoPath() const {
		return logoPath;
	}

	bool getErrorOnNoLogo() const {
		return errorOnNoLogo;
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

  const std::vector<CMType>& getCMTypes() const {
    return cmtypes;
  }

	bool isDumpStreamInfo() const {
		return dumpStreamInfo;
	}

	bool isSystemAvsPlugin() const {
		return systemAvsPlugin;
	}

	std::string getAudioFilePath() const {
		return regtmp(StringFormat("%s/audio.dat", tmpDir.path()));
	}

	std::string getWaveFilePath() const {
		return regtmp(StringFormat("%s/audio.wav", tmpDir.path()));
	}

	std::string getIntVideoFilePath(int index) const {
		return regtmp(StringFormat("%s/i%d.mpg", tmpDir.path(), index));
	}

	std::string getStreamInfoPath() const {
		return outVideoPath + "-streaminfo.dat";
	}

	std::string getEncVideoFilePath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat("%s/v%d-%d%s.raw", tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	std::string getEncStatsFilePath(int vindex, int index, CMType cmtype) const
	{
		auto str = StringFormat("%s/s%d-%d%s.log", tmpDir.path(), vindex, index, GetCMSuffix(cmtype));
		ctx.registerTmpFile(str);
		// x264は.mbtreeも生成するので
		ctx.registerTmpFile(str + ".mbtree");
		// x265は.cutreeも生成するので
		ctx.registerTmpFile(str + ".cutree");
		return str;
	}

	std::string getIntAudioFilePath(int vindex, int index, int aindex, CMType cmtype) const {
		return regtmp(StringFormat("%s/a%d-%d-%d%s.aac",
			tmpDir.path(), vindex, index, aindex, GetCMSuffix(cmtype)));
	}

	std::string getTmpASSFilePath(int vindex, int index, int langindex, CMType cmtype) const {
		return regtmp(StringFormat("%s/c%d-%d-%d%s.ass",
			tmpDir.path(), vindex, index, langindex, GetCMSuffix(cmtype)));
	}

	std::string getLogoTmpFilePath() const {
		return regtmp(StringFormat("%s/logotmp.dat", tmpDir.path()));
	}

	std::string getTmpAMTSourcePath(int vindex) const {
		return regtmp(StringFormat("%s/amts%d.dat", tmpDir.path(), vindex));
	}

	std::string getTmpSourceAVSPath(int vindex) const {
		return regtmp(StringFormat("%s/amts%d.avs", tmpDir.path(), vindex));
	}

	std::string getTmpLogoFramePath(int vindex) const {
		return regtmp(StringFormat("%s/logof%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpChapterExePath(int vindex) const {
		return regtmp(StringFormat("%s/chapter_exe%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpChapterExeOutPath(int vindex) const {
		return regtmp(StringFormat("%s/chapter_exe_o%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpTrimAVSPath(int vindex) const {
		return regtmp(StringFormat("%s/trim%d.avs", tmpDir.path(), vindex));
	}

	std::string getTmpJlsPath(int vindex) const {
		return regtmp(StringFormat("%s/jls%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpChapterPath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat("%s/chapter%d-%d%s.txt",
			tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	std::string getOutFilePath(int index, CMType cmtype) const {
		StringBuilder sb;
		sb.append("%s", outVideoPath);
		if (index != 0) {
			sb.append("-%d", index);
		}
		sb.append("%s.mp4", GetCMSuffix(cmtype));
		return sb.str();
	}

	std::string getOutSummaryPath() const {
		return StringFormat("%s.txt", outVideoPath);
	}

	std::string getDRCSOutPath(const std::string& md5) const {
		return StringFormat("%s%s.bmp", drcsOutPath, md5);
	}

	std::string getOptions(
		VIDEO_STREAM_FORMAT srcFormat, double srcBitrate, bool pulldown,
		int pass, const std::vector<EncoderZone>& zones, int vindex, int index, CMType cmtype) const
	{
		StringBuilder sb;
		sb.append("%s", encoderOptions);
		if (autoBitrate) {
			double targetBitrate = bitrate.getTargetBitrate(srcFormat, srcBitrate);
      if (cmtype == CMTYPE_CM) {
        targetBitrate *= bitrateCM;
      }
			if (encoder == ENCODER_QSVENC) {
				sb.append(" --la %d --maxbitrate %d", (int)targetBitrate, (int)(targetBitrate * 2));
			}
			else if (encoder == ENCODER_NVENC) {
				sb.append(" --vbrhq %d --maxbitrate %d", (int)targetBitrate, (int)(targetBitrate * 2));
			}
			else {
				sb.append(" --bitrate %d --vbv-maxrate %d --vbv-bufsize %d", 
					(int)targetBitrate, (int)(targetBitrate * 2), (int)(targetBitrate * 2));
			}
		}
		if (pass >= 0) {
			sb.append(" --pass %d --stats \"%s\"",
				pass, getEncStatsFilePath(vindex, index, cmtype));
		}
		if (zones.size() && bitrateCM != 1.0 && encoder != ENCODER_QSVENC && encoder != ENCODER_NVENC) {
			sb.append(" --zones ");
			for (int i = 0; i < (int)zones.size(); ++i) {
				auto zone = zones[i];
				sb.append("%s%d,%d,b=%3g", (i > 0) ? "/" : "", zone.startFrame, zone.endFrame, bitrateCM);
			}
		}
		return sb.str();
	}

	void dump() const {
		ctx.info("[設定]");
		if (mode != "ts") {
			ctx.info("Mode: %s", mode.c_str());
		}
		ctx.info("入力: %s", srcFilePath.c_str());
		ctx.info("出力: %s", outVideoPath.c_str());
		ctx.info("一時フォルダ: %s", tmpDir.path().c_str());
		ctx.info("エンコーダ: %s (%s)", encoderPath.c_str(), encoderToString(encoder));
		ctx.info("エンコーダオプション: %s", encoderOptions.c_str());
		if (autoBitrate) {
			ctx.info("自動ビットレート: 有効 (%g:%g:%g)", bitrate.a, bitrate.b, bitrate.h264);
		}
		else {
			ctx.info("自動ビットレート: 無効");
		}
		ctx.info("エンコード/出力: %s/%s",
			twoPass ? "2パス" : "1パス",
			cmOutMaskToString(cmoutmask));
    ctx.info("チャプター解析: %s%s",
			chapter ? "有効" : "無効",
			(chapter && errorOnNoLogo) ? "（ロゴ必須）" : "");
    if (chapter) {
      for (int i = 0; i < (int)logoPath.size(); ++i) {
        ctx.info("logo%d: %s", (i + 1), logoPath[i].c_str());
      }
    }
		if (serviceId > 0) {
			ctx.info("ServiceId: %d", serviceId);
		}
		else {
			ctx.info("ServiceId: 指定なし");
		}
		ctx.info("デコーダ: MPEG2:%s H264:%s",
			decoderToString(decoderSetting.mpeg2),
			decoderToString(decoderSetting.h264));
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
	// マッピングなしDRCS画像出力ディレクトリパス
	std::string drcsOutPath;
	// フィルタパス
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
	bool chapter;
	BitrateSetting bitrate;
	double bitrateCM;
	int serviceId;
	DecoderSetting decoderSetting;
	// CM解析用設定
	std::vector<std::string> logoPath;
	bool errorOnNoLogo;
	std::string chapterExePath;
	std::string joinLogoScpPath;
	std::string joinLogoScpCmdPath;
  int cmoutmask;
	std::vector<CMType> cmtypes;
	// デバッグ用設定
	bool dumpStreamInfo;
	bool systemAvsPlugin;

	const char* decoderToString(DECODER_TYPE decoder) const {
		switch (decoder) {
		case DECODER_QSV: return "QSV";
		case DECODER_CUVID: return "CUVID";
		}
		return "default";
	}

	std::string regtmp(std::string str) const {
		ctx.registerTmpFile(str);
		return str;
	}
};

