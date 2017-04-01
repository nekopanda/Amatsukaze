/**
* Amtasukaze Command Line Interface
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <time.h>

#include "TranscodeManager.hpp"

// MSVCのマルチバイトはUnicodeでないので文字列操作に適さないのでwchar_tで文字列操作をする
#ifdef _MSC_VER
namespace std { typedef wstring tstring; }
typedef wchar_t tchar;
#define stscanf swscanf_s
#define PRITSTR "ls"
#define _T(s) L ## s
#else
namespace std { typedef string tstring; }
typedef char tchar;
#define stscanf sscanf
#define PRITSTR "s"
#define _T(s) s
#endif

static std::string to_string(std::wstring str) {
	if (str.size() == 0) {
		return std::string();
	}
	int dstlen = WideCharToMultiByte(
		CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0, NULL, NULL);
	std::vector<char> ret(dstlen);
	WideCharToMultiByte(CP_ACP, 0,
		str.c_str(), (int)str.size(), ret.data(), (int)ret.size(), NULL, NULL);
	return std::string(ret.begin(), ret.end());
}

static std::string to_string(std::string str) {
	return str;
}

static void printCopyright() {
	PRINTF(
		"Amatsukaze - Automated MPEG2-TS Transcode Utility\n"
		"Built on %s %s\n"
		"Copyright (c) 2017 Nekopanda\n", __DATE__, __TIME__);
}

static void printHelp(const tchar* bin) {
  PRINTF(
    "%" PRITSTR " <オプション> -i <input.ts> -o <output.mp4>\n"
    "オプション []はデフォルト値 \n"
    "  -i|--input  <パス>  入力ファイルパス\n"
    "  -o|--output <パス>  出力ファイルパス\n"
    "  --mode <モード>     処理モード[ts]\n"
    "                      ts : MPGE2-TSを入力する詳細解析モード\n"
    "                      g  : FFMPEGを利用した一般ファイルモード\n"
		"  -s|--serviceid <数値> 処理するサービスIDを指定[]\n"
		"  -w|--work   <パス>  一時ファイルパス[./]\n"
		"  -et|--encoder-type <タイプ>  使用エンコーダタイプ[x264]\n"
		"                      対応エンコーダ: x264,x265,QSVEnc\n"
		"  -e|--encoder <パス> エンコーダパス[x264.exe]\n"
		"  -eo|--encoder-opotion <オプション> エンコーダへ渡すオプション[]\n"
		"                      入力ファイルの解像度、アスペクト比、インタレースフラグ、\n"
		"                      フレームレート、カラーマトリクス等は自動で追加されるので不要\n"
    "  -b|--bitrate a:b:f  ビットレート計算式 映像ビットレートkbps = f*(a*s+b)\n"
    "                      sは入力映像ビットレート、fは入力がH264の場合は入力されたfだが、\n"
    "                      入力がMPEG2の場合はf=1とする\n"
    "                      指定がない場合はビットレートオプションを追加しない\n"
    "  --2pass              2passエンコード\n"
		"  -m|--muxer  <パス>  L-SMASHのmuxerへのパス[muxer.exe]\n"
		"  -t|--timelineeditor <パス> L-SMASHのtimelineeditorへのパス[timelineeditor.exe]\n"
		"  -j|--json   <パス>  出力結果情報をJSON出力する場合は出力ファイルパスを指定[]\n"
		"  --dump              処理途中のデータをダンプ（デバッグ用）\n",
		bin);
}

static std::tstring getParam(int argc, tchar* argv[], int ikey) {
	if (ikey + 1 >= argc) {
		THROWF(FormatException,
			"%" PRITSTR "オプションはパラメータが必要です", argv[ikey]);
	}
	return argv[ikey + 1];
}

static std::tstring pathNormalize(std::tstring path) {
	if (path.size() != 0) {
		// バックスラッシュはスラッシュに変換
		std::replace(path.begin(), path.end(), _T('\\'), _T('/'));
		// 最後のスラッシュは取る
		if (path.back() == _T('/')) {
			path.pop_back();
		}
	}
	return path;
}

template <typename STR>
static size_t pathGetExtensionSplitPos(const STR& path) {
	size_t lastsplit = path.rfind(_T('/'));
	size_t namebegin = (lastsplit == STR::npos)
		? 0
		: lastsplit + 1;
	size_t dotpos = path.find(_T('.'), namebegin);
	size_t len = (dotpos == STR::npos)
		? path.size()
		: dotpos;
	return len;
}

static std::tstring pathRemoveExtension(const std::tstring& path) {
	return path.substr(0, pathGetExtensionSplitPos(path));
}

static std::string pathGetExtension(const std::string& path) {
	auto ext = path.substr(pathGetExtensionSplitPos(path));
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	return ext;
}

static ENUM_ENCODER encoderFtomString(const std::tstring& str) {
	if (str == _T("x264")) {
		return ENCODER_X264;
	}
	else if (str == _T("x265")) {
		return ENCODER_X265;
	}
	else if (str == _T("qsv") || str == _T("QSVEnc")) {
		return ENCODER_QSVENC;
	}
	return (ENUM_ENCODER)-1;
}

static std::unique_ptr<TranscoderSetting> parseArgs(AMTContext& ctx, int argc, tchar* argv[])
{
	std::tstring srcFilePath;
	std::tstring outVideoPath;
	std::tstring workDir = _T("./");
	std::tstring outInfoJsonPath;
	ENUM_ENCODER encoder = ENUM_ENCODER();
	std::tstring encoderPath = _T("x264.exe");
  std::tstring encoderOptions = _T("");
	std::tstring muxerPath = _T("muxer.exe");
	std::tstring timelineditorPath = _T("timelineeditor.exe");
  AMT_CLI_MODE mode = AMT_CLI_TS;
  bool autoBitrate = bool();
  BitrateSetting bitrate = BitrateSetting();
  bool twoPass = bool();
	int serviceId = -1;
	bool dumpStreamInfo = bool();

	for (int i = 1; i < argc; ++i) {
		std::tstring key = argv[i];
		if (key == _T("-i") || key == _T("--input")) {
			srcFilePath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("-o") || key == _T("--output")) {
			outVideoPath =
				pathRemoveExtension(pathNormalize(getParam(argc, argv, i++)));
		}
    else if (key == _T("--mode")) {
      std::tstring modeStr = getParam(argc, argv, i++);
      if (modeStr == _T("ts")) {
        mode = AMT_CLI_TS;
      }
      else if (modeStr == _T("g")) {
        mode = AMT_CLI_GENERIC;
      }
      else {
        PRINTF("--modeの指定が間違っています: %" PRITSTR "\n", modeStr.c_str());
      }
    }
		else if (key == _T("-w") || key == _T("--work")) {
			workDir = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("-et") || key == _T("--encoder-type")) {
			std::tstring arg = getParam(argc, argv, i++);
			encoder = encoderFtomString(arg);
			if (encoder == (ENUM_ENCODER)-1) {
				PRINTF("--encoder-typeの指定が間違っています: %" PRITSTR "\n", arg.c_str());
			}
		}
		else if (key == _T("-e") || key == _T("--encoder")) {
			encoderPath = getParam(argc, argv, i++);
		}
		else if (key == _T("-eo") || key == _T("--encoder-option")) {
			encoderOptions = getParam(argc, argv, i++);
    }
    else if (key == _T("-b") || key == _T("--bitrate")) {
      const auto arg = getParam(argc, argv, i++);
      int ret = stscanf(arg.c_str(), _T("%lf:%lf:%lf:%lf"),
        &bitrate.a, &bitrate.b, &bitrate.h264, &bitrate.h265);
      if (ret < 3) {
        THROWF(ArgumentException, "--bitrateの指定が間違っています");
      }
      if (ret <= 3) {
        bitrate.h265 = 2;
      }
      autoBitrate = true;
    }
    else if (key == _T("--2pass")) {
      twoPass = true;
    }
		else if (key == _T("-m") || key == _T("--muxer")) {
			muxerPath = getParam(argc, argv, i++);
		}
		else if (key == _T("-t") || key == _T("--timelineeditor")) {
			timelineditorPath = getParam(argc, argv, i++);
		}
		else if (key == _T("-j") || key == _T("--json")) {
			outInfoJsonPath = getParam(argc, argv, i++);
		}
		else if (key == _T("-s") || key == _T("--serivceid")) {
			std::tstring sidstr = getParam(argc, argv, i++);
			if (sidstr.size() > 2 && sidstr.substr(0, 2) == _T("0x")) {
				// 16進
				serviceId = std::stoi(sidstr.substr(2), NULL, 16);;
			}
			else {
				// 10進
				serviceId = std::stoi(sidstr);
			}
		}
		else if (key == _T("--dump")) {
			dumpStreamInfo = true;
		}
		else if (key.size() == 0) {
			continue;
		}
		else {
			THROWF(FormatException, "不明なオプション: %" PRITSTR, argv[i]);
		}
	}

	if (srcFilePath.size() == 0) {
		THROWF(ArgumentException, "入力ファイルを指定してください");
	}
	if (outVideoPath.size() == 0) {
		THROWF(ArgumentException, "出力ファイルを指定してください");
	}

	return std::unique_ptr<TranscoderSetting>(new TranscoderSetting(
		ctx,
		to_string(workDir),
		mode,
		to_string(srcFilePath),
		to_string(outVideoPath),
		to_string(outInfoJsonPath),
		encoder,
		to_string(encoderPath),
		to_string(encoderOptions),
		to_string(muxerPath),
		to_string(timelineditorPath),
		twoPass,
		autoBitrate,
		bitrate,
		serviceId,
		dumpStreamInfo));
}

static CRITICAL_SECTION g_log_crisec;
static void amatsukaze_av_log_callback(
  void* ptr, int level, const char* fmt, va_list vl)
{
  level &= 0xff;

  if (level > av_log_get_level()) {
    return;
  }

  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, vl);
  int len = (int)strlen(buf);
  if (len == 0) {
    return;
  }

  static char* log_levels[] = {
    "panic", "fatal", "error", "warn", "info", "verb", "debug", "trace"
  };

  EnterCriticalSection(&g_log_crisec);
  
  static bool print_prefix = true;
  bool tmp_pp = print_prefix;
  print_prefix = (buf[len - 1] == '\r' || buf[len - 1] == '\n');
  if (tmp_pp) {
    int logtype = level / 8;
    const char* level_str =
      (logtype >= sizeof(log_levels) / sizeof(log_levels[0]))
      ? "unk" : log_levels[logtype];
    printf("FFMPEG [%s] %s", level_str, buf);
  }
  else {
    printf(buf);
  }
  if (print_prefix) {
    fflush(stdout);
  }

  LeaveCriticalSection(&g_log_crisec);
}

static int amatsukazeTranscodeMain(AMTContext& ctx, const TranscoderSetting& setting) {
	try {
    switch (setting.getMode()) {
    case AMT_CLI_TS:
      transcodeMain(ctx, setting);
      break;
    case AMT_CLI_GENERIC:
      transcodeSimpleMain(ctx, setting);
      break;
    }

		return 0;
	}
	catch (Exception e) {
		return 1;
	}
}
