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
#include "AmatsukazeTestImpl.hpp"

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
		"  -s|--serviceid <数値> 処理するサービスIDを指定[]\n"
		"  -w|--work   <パス>  一時ファイルパス[./]\n"
		"  -et|--encoder-type <タイプ>  使用エンコーダタイプ[x264]\n"
		"                      対応エンコーダ: x264,x265,QSVEnc,NVEnc\n"
		"  -e|--encoder <パス> エンコーダパス[x264.exe]\n"
		"  -eo|--encoder-opotion <オプション> エンコーダへ渡すオプション[]\n"
		"                      入力ファイルの解像度、アスペクト比、インタレースフラグ、\n"
		"                      フレームレート、カラーマトリクス等は自動で追加されるので不要\n"
    "  -b|--bitrate a:b:f  ビットレート計算式 映像ビットレートkbps = f*(a*s+b)\n"
    "                      sは入力映像ビットレート、fは入力がH264の場合は入力されたfだが、\n"
    "                      入力がMPEG2の場合はf=1とする\n"
    "                      指定がない場合はビットレートオプションを追加しない\n"
		"  -bcm|--bitrate-cm <float>   CM判定されたところのビットレート倍率\n"
		"  --2pass             2passエンコード\n"
		"  -m|--muxer  <パス>  L-SMASHのmuxerへのパス[muxer.exe]\n"
		"  -t|--timelineeditor <パス> L-SMASHのtimelineeditorへのパス[timelineeditor.exe]\n"
		"  -f|--filter <パス>  フィルタAvisynthスクリプトへのパス[]"
		"  -pf|--postfilter <パス>  ポストフィルタAvisynthスクリプトへのパス[]"
		"  --mpeg2decoder <デコーダ>  MPEG2用デコーダ[default]\n"
		"                      使用可能デコーダ: default,QSV,CUVID"
		"  --h264decoder <デコーダ>  H264用デコーダ[default]\n"
		"                      使用可能デコーダ: default,QSV,CUVID"
		"  --error-on-no-logo  ロゴが見つからない場合はエラーとする\n"
		"  --logo <パス>       ロゴファイルを指定（いくつでも指定可能）\n"
		"  --32bitlib <パス>   32bitのAmatsukaze.dllへのパス\n"
		"  --chapter-exe <パス> chapter_exe.exeへのパス\n"
		"  --jls <パス>         join_logo_scp.exeへのパス\n"
		"  --jls-cmd <パス>    join_logo_scpのコマンドファイルへのパス\n"
		"  --chapter-jls <パス> chapter_????へのパス\n"
		"  --inpipe <ハンドル> Amatsukazeホストへの送信パイプ\n"
		"  --outpipe <ハンドル> Amatsukzeホストからの受信パイ\n"
		"  -j|--json   <パス>  出力結果情報をJSON出力する場合は出力ファイルパスを指定[]\n"
    "  --mode <モード>     処理モード[ts]\n"
    "                      ts : MPGE2-TSを入力する詳細解析モード\n"
    "                      g  : MPEG2-TS以外の入力ファイルを処理する一般ファイルモード\n"
		"                           一般ファイルモードはFFMPEGでデコードするため音ズレ補正\n"
		"                           などの処理は一切ないので、MPEG2-TSには使用しないように\n"
		"  --dump              処理途中のデータをダンプ（デバッグ用）\n",
		bin);
}

static std::tstring getParam(int argc, const tchar* argv[], int ikey) {
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
	else if (str == _T("nvenc") || str == _T("NVEnc")) {
		return ENCODER_NVENC;
	}
	return (ENUM_ENCODER)-1;
}

static DECODER_TYPE decoderFromString(const std::tstring& str) {
	if (str == _T("default")) {
		return DECODER_DEFAULT;
	}
	else if (str == _T("qsv") || str == _T("QSV")) {
		return DECODER_QSV;
	}
	else if (str == _T("cuvid") || str == _T("CUVID") || str == _T("nvdec") || str == _T("NVDec")) {
		return DECODER_CUVID;
	}
	return (DECODER_TYPE)-1;
}

static std::unique_ptr<TranscoderSetting> parseArgs(AMTContext& ctx, int argc, const tchar* argv[])
{
	std::tstring srcFilePath;
	std::tstring outVideoPath;
	std::tstring workDir = _T("./");
	std::tstring outInfoJsonPath;
	std::tstring filterScriptPath;
	std::tstring postFilterScriptPath;
	ENUM_ENCODER encoder = ENUM_ENCODER();
	std::tstring encoderPath = _T("x264.exe");
  std::tstring encoderOptions = _T("");
	std::tstring muxerPath = _T("muxer.exe");
	std::tstring timelineditorPath = _T("timelineeditor.exe");
	std::tstring modeStr = _T("ts");
  std::tstring modeArgs = _T("");
  bool autoBitrate = bool();
  BitrateSetting bitrate = BitrateSetting();
	double bitrateCM = 1.0;
  bool twoPass = bool();
	bool pulldown = bool();
	int serviceId = -1;
	DECODER_TYPE mpeg2decoder = DECODER_DEFAULT;
	DECODER_TYPE h264decoder = DECODER_DEFAULT;
	std::vector<std::string> logoPath;
	bool errorOnNoLogo = false;
	std::tstring amt32bitPath;
	std::tstring chapterExePath;
	std::tstring joinLogoScpPath;
	std::tstring joinLogoScpCmdPath;
	HANDLE inPipe = INVALID_HANDLE_VALUE;
	HANDLE outPipe = INVALID_HANDLE_VALUE;
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
			modeStr = getParam(argc, argv, i++);
    }
    else if (key == _T("-a") || key == _T("--args")) {
      modeArgs = getParam(argc, argv, i++);
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
		else if (key == _T("-bcm") || key == _T("--bitrate-cm")) {
			const auto arg = getParam(argc, argv, i++);
			int ret = stscanf(arg.c_str(), _T("%lf"), &bitrateCM);
			if (ret == 0) {
				THROWF(ArgumentException, "--bitrate-cmの指定が間違っています");
			}
		}
    else if (key == _T("--2pass")) {
      twoPass = true;
    }
		else if (key == _T("--pulldown")) {
			pulldown = true;
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
    else if (key == _T("-f") || key == _T("--filter")) {
      filterScriptPath = getParam(argc, argv, i++);
    }
		else if (key == _T("-pf") || key == _T("--postfilter")) {
			postFilterScriptPath = getParam(argc, argv, i++);
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
		else if (key == _T("--mpeg2decoder")) {
			std::tstring arg = getParam(argc, argv, i++);
			mpeg2decoder = decoderFromString(arg);
			if (mpeg2decoder == (DECODER_TYPE)-1) {
				PRINTF("--mpeg2decoderの指定が間違っています: %" PRITSTR "\n", arg.c_str());
			}
		}
		else if (key == _T("--h264decoder")) {
			std::tstring arg = getParam(argc, argv, i++);
			h264decoder = decoderFromString(arg);
			if (h264decoder == (DECODER_TYPE)-1) {
				PRINTF("--h264decoderの指定が間違っています: %" PRITSTR "\n", arg.c_str());
			}
		}
		else if (key == _T("--error-on-no-logo")) {
			errorOnNoLogo = true;
		}
		else if (key == _T("--logo")) {
			logoPath.push_back(to_string(getParam(argc, argv, i++)));
		}
		else if (key == _T("--32bitlib")) {
			amt32bitPath = getParam(argc, argv, i++);
		}
		else if (key == _T("--chapter-exe")) {
			chapterExePath = getParam(argc, argv, i++);
		}
		else if (key == _T("--jls")) {
			joinLogoScpPath = getParam(argc, argv, i++);
		}
		else if (key == _T("--jls-cmd")) {
			joinLogoScpCmdPath = getParam(argc, argv, i++);
		}
		else if (key == _T("--inpipe")) {
			inPipe = (HANDLE)std::stoll(getParam(argc, argv, i++));
		}
		else if (key == _T("--outpipe")) {
			outPipe = (HANDLE)std::stoll(getParam(argc, argv, i++));
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

	if (modeStr == L"ts" || modeStr == L"g") {
		if (srcFilePath.size() == 0) {
			THROWF(ArgumentException, "入力ファイルを指定してください");
		}
		if (outVideoPath.size() == 0) {
			THROWF(ArgumentException, "出力ファイルを指定してください");
		}
	}

	if (errorOnNoLogo) {
		if (logoPath.size() == 0) {
			THROW(ArgumentException, "ロゴが指定されていません");
		}
	}

	// CM解析は４つ揃える必要がある
	if (chapterExePath.size() > 0 || joinLogoScpPath.size() > 0) {
		if (chapterExePath.size() == 0) {
			THROW(ArgumentException, "chapter_exe.exeへのパスが設定されていません");
		}
		if (joinLogoScpPath.size() == 0) {
			THROW(ArgumentException, "join_logo_scp.exeへのパスが設定されていません");
		}
	}

	return std::unique_ptr<TranscoderSetting>(new TranscoderSetting(
		ctx,
		to_string(workDir),
		to_string(modeStr),
    to_string(modeArgs),
		to_string(srcFilePath),
		to_string(outVideoPath),
		to_string(outInfoJsonPath),
    to_string(filterScriptPath),
		to_string(postFilterScriptPath),
		encoder,
		to_string(encoderPath),
		to_string(encoderOptions),
		to_string(muxerPath),
		to_string(timelineditorPath),
		twoPass,
		autoBitrate,
		pulldown,
		bitrate,
		bitrateCM,
		serviceId,
		mpeg2decoder,
		h264decoder,
		logoPath,
		errorOnNoLogo,
		to_string(amt32bitPath),
		to_string(chapterExePath),
		to_string(joinLogoScpPath),
		to_string(joinLogoScpCmdPath),
		inPipe,
		outPipe,
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
    fprintf(stderr, "FFMPEG [%s] %s", level_str, buf);
  }
  else {
    fprintf(stderr, buf);
  }
  if (print_prefix) {
    fflush(stdout);
  }

  LeaveCriticalSection(&g_log_crisec);
}

static int amatsukazeTranscodeMain(AMTContext& ctx, const TranscoderSetting& setting) {
	try {
		std::string mode = setting.getMode();
    if(mode == "ts")
      transcodeMain(ctx, setting);
		else if (mode == "g")
      transcodeSimpleMain(ctx, setting);

		else if (mode == "test_print_crc")
			test::PrintCRCTable(ctx, setting);
		else if (mode == "test_crc")
			test::CheckCRC(ctx, setting);
		else if (mode == "test_read_bits")
			test::ReadBits(ctx, setting);
		else if (mode == "test_auto_buffer")
			test::CheckAutoBuffer(ctx, setting);
		else if (mode == "test_verifympeg2ps")
			test::VerifyMpeg2Ps(ctx, setting);
		else if (mode == "test_readts")
			test::ReadTS(ctx, setting);
		else if (mode == "test_aacdec")
			test::AacDecode(ctx, setting);
		else if (mode == "test_wavewrite")
			test::WaveWriteHeader(ctx, setting);
		else if (mode == "test_process")
			test::ProcessTest(ctx, setting);
		else if (mode == "test_streamreform")
			test::FileStreamInfo(ctx, setting);
		else if (mode == "test_parseargs")
			test::ParseArgs(ctx, setting);
		else if (mode == "test_lossless")
			test::LosslessFileTest(ctx, setting);
		else if (mode == "test_logoframe")
			test::LogoFrameTest(ctx, setting);

		else
			PRINTF("--modeの指定が間違っています: %s\n", mode.c_str());

		return 0;
	}
	catch (const Exception&) {
		return 1;
	}
}

__declspec(dllexport) int AmatsukazeCLI(int argc, const wchar_t* argv[]) {
  try {
    printCopyright();

    AMTContext ctx;

    auto setting = parseArgs(ctx, argc, argv);

    // FFMPEGライブラリ初期化
    InitializeCriticalSection(&g_log_crisec);
    av_log_set_callback(amatsukaze_av_log_callback);
    av_register_all();

    return amatsukazeTranscodeMain(ctx, *setting);
  }
  catch (const Exception&) {
    // parseArgsでエラー
    printHelp(argv[0]);
    return 1;
  }
}
