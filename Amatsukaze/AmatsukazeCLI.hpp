/**
* Amtasukaze Command Line Interface
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <time.h>

#include "TranscodeManager.hpp"
#include "AmatsukazeTestImpl.hpp"
#include "Version.h"

// MSVCのマルチバイトはUnicodeでないので文字列操作に適さないのでwchar_tで文字列操作をする


static void printCopyright() {
	PRINTF(
		"Amatsukaze - Automated MPEG2-TS Transcoder %s (%s %s)\n"
		"Copyright (c) 2017-2019 Nekopanda\n", AMATSUKAZE_VERSION, __DATE__, __TIME__);
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
		"  -eo|--encoder-option <オプション> エンコーダへ渡すオプション[]\n"
		"                      入力ファイルの解像度、アスペクト比、インタレースフラグ、\n"
		"                      フレームレート、カラーマトリクス等は自動で追加されるので不要\n"
		"  -b|--bitrate a:b:f  ビットレート計算式 映像ビットレートkbps = f*(a*s+b)\n"
		"                      sは入力映像ビットレート、fは入力がH264の場合は入力されたfだが、\n"
		"                      入力がMPEG2の場合はf=1とする\n"
		"                      指定がない場合はビットレートオプションを追加しない\n"
		"  -bcm|--bitrate-cm <float>   CM判定されたところのビットレート倍率\n"
		"  --2pass             2passエンコード\n"
		"  --splitsub          メイン以外のフォーマットは結合しない\n"
		"  -fmt|--format <フォーマット> 出力フォーマット[mp4]\n"
		"                      対応フォーマット: mp4,mkv,m2ts,ts\n"
		"  -m|--muxer  <パス>  L-SMASHのmuxerまたはmkvmergeまたはtsMuxeRへのパス[muxer.exe]\n"
		"  -t|--timelineeditor  <パス>  timelineeditorへのパス（MP4でVFR出力する場合に必要）[timelineeditor.exe]\n"
		"  --mp4box <パス>     mp4boxへのパス（MP4で字幕処理する場合に必要）[mp4box.exe]\n"
		"  -f|--filter <パス>  フィルタAvisynthスクリプトへのパス[]\n"
		"  -pf|--postfilter <パス>  ポストフィルタAvisynthスクリプトへのパス[]\n"
		"  --mpeg2decoder <デコーダ>  MPEG2用デコーダ[default]\n"
		"                      使用可能デコーダ: default,QSV,CUVID\n"
		"  --h264decoder <デコーダ>  H264用デコーダ[default]\n"
		"                      使用可能デコーダ: default,QSV,CUVID\n"
		"  --chapter           チャプター・CM解析を行う\n"
		"  --subtitles         字幕を処理する\n"
		"  --nicojk            ニコニコ実況コメントを追加する\n"
		"  --logo <パス>       ロゴファイルを指定（いくつでも指定可能）\n"
		"  --drcs <パス>       DRCSマッピングファイルパス\n"
		"  --ignore-no-drcsmap マッピングにないDRCS外字があっても処理を続行する\n"
		"  --ignore-no-logo    ロゴが見つからなくても処理を続行する\n"
		"  --ignore-nicojk-error ニコニコ実況取得でエラーが発生しても処理を続行する\n"
		"  --no-delogo         ロゴ消しをしない（デフォルトはロゴがある場合は消します）\n"
		"  --loose-logo-detection ロゴ検出判定しきい値を低くします\n"
		"  --max-fade-length <数値> ロゴの最大フェードフレーム数[16]\n"
		"  --chapter-exe <パス> chapter_exe.exeへのパス\n"
		"  --jls <パス>         join_logo_scp.exeへのパス\n"
		"  --jls-cmd <パス>    join_logo_scpのコマンドファイルへのパス\n"
		"  --jls-option <オプション>    join_logo_scpのコマンドファイルへのパス\n"
		"  --trimavs <パス>    CMカット用Trim AVSファイルへのパス。メインファイルのCMカット出力でのみ使用される。\n"
		"  --nicoass <パス>     NicoConvASSへのパス\n"
		"  -om|--cmoutmask <数値> 出力マスク[1]\n"
		"                      1 : 通常\n"
		"                      2 : CMをカット\n"
		"                      4 : CMのみ出力\n"
		"                      ORも可 例) 6: 本編とCMを分離\n"
		"  --nicojk18          ニコニコ実況コメントをnicojk18サーバから取得\n"
		"  --nicojklog         ニコニコ実況コメントをNicoJKログフォルダから取得\n"
		"                      (NicoConvASSを -nicojk 1 で呼び出します)\n"
		"  --nicojkmask <数値> ニコニコ実況コメントマスク[1]\n"
		"                      1 : 1280x720不透明\n"
		"                      2 : 1280x720半透明\n"
		"                      4 : 1920x1080不透明\n"
		"                      8 : 1920x1080半透明\n"
		"                      ORも可 例) 15: すべて出力\n"
		"  --no-remove-tmp     一時ファイルを削除せずに残す\n"
		"                      デフォルトは60fpsタイミングで生成\n"
		"  --x265-timefactor <数値>  x265で疑似VFRレートコントロールするときの時間レートファクター[0.25]\n"
		"  --pmt-cut <数値>:<数値>  PMT変更でCM認識するときの最大CM認識時間割合。全再生時間に対する割合で指定する。\n"
		"                      例えば 0.1:0.2 とすると開始10%%までにPMT変更があった場合はそのPMT変更までをCM認識する。\n"
		"                      また終わりから20%%までにPMT変更があった場合も同様にCM認識する。[0:0]\n"
		"  -j|--json   <パス>  出力結果情報をJSON出力する場合は出力ファイルパスを指定[]\n"
		"  --mode <モード>     処理モード[ts]\n"
		"                      ts : MPGE2-TSを入力する通常エンコードモード\n"
		"                      cm : エンコードまで行わず、CM解析までで終了するモード\n"
		"                      drcs : マッピングのないDRCS外字画像だけ出力するモード\n"
		"                      probe_subtitles : 字幕があるか判定\n"
		"                      probe_audio : 音声フォーマットを出力\n"
		"  --resource-manager <入力パイプ>:<出力パイプ> リソース管理ホストとの通信パイプ\n"
		"  --affinity <グループ>:<マスク> CPUアフィニティ\n"
		"                      グループはプロセッサグループ（64論理コア以下のシステムでは0のみ）\n"
		"  --max-frames        probe_*モード時のみ有効。TSを見る時間を映像フレーム数で指定[9000]\n"
		"  --dump              処理途中のデータをダンプ（デバッグ用）\n",
		bin);
}

static tstring getParam(int argc, const tchar* argv[], int ikey) {
	if (ikey + 1 >= argc) {
		THROWF(FormatException,
			"%" PRITSTR "オプションはパラメータが必要です", argv[ikey]);
	}
	return argv[ikey + 1];
}

static ENUM_ENCODER encoderFtomString(const tstring& str) {
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

static DECODER_TYPE decoderFromString(const tstring& str) {
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

static std::unique_ptr<ConfigWrapper> parseArgs(AMTContext& ctx, int argc, const tchar* argv[])
{
	tstring moduleDir = pathNormalize(GetModuleDirectory());
	Config conf = Config();
	conf.workDir = _T("./");
	conf.encoderPath = _T("x264.exe");
	conf.encoderOptions = _T("");
	conf.timelineditorPath = _T("timelineeditor.exe");
	conf.mp4boxPath = _T("mp4box.exe");
	conf.chapterExePath = _T("chapter_exe.exe");
	conf.joinLogoScpPath = _T("join_logo_scp.exe");
	conf.nicoConvAssPath = _T("NicoConvASS.exe");
	conf.nicoConvChSidPath = _T("ch_sid.txt");
	conf.drcsOutPath = moduleDir + _T("/../drcs");
	conf.drcsMapPath = conf.drcsOutPath + _T("/drcs_map.txt");
	conf.joinLogoScpCmdPath = moduleDir + _T("/../JL/JL_標準.txt");
	conf.mode = _T("ts");
	conf.modeArgs = _T("");
	conf.bitrateCM = 1.0;
	conf.x265TimeFactor = 0.25;
	conf.serviceId = -1;
	conf.cmoutmask = 1;
	conf.nicojkmask = 1;
	conf.maxframes = 30 * 300;
	conf.inPipe = INVALID_HANDLE_VALUE;
	conf.outPipe = INVALID_HANDLE_VALUE;
	conf.maxFadeLength = 16;
	conf.numEncodeBufferFrames = 16;
	bool nicojk = false;

	for (int i = 1; i < argc; ++i) {
		tstring key = argv[i];
		if (key == _T("-i") || key == _T("--input")) {
			conf.srcFilePath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("-o") || key == _T("--output")) {
			conf.outVideoPath =
				pathRemoveExtension(pathNormalize(getParam(argc, argv, i++)));
		}
		else if (key == _T("--mode")) {
			conf.mode = getParam(argc, argv, i++);
		}
		else if (key == _T("-a") || key == _T("--args")) {
			conf.modeArgs = getParam(argc, argv, i++);
		}
		else if (key == _T("-w") || key == _T("--work")) {
			conf.workDir = pathNormalize(getParam(argc, argv, i++));
			if (conf.workDir.size() == 0) {
				conf.workDir = _T("./");
			}
		}
		else if (key == _T("-et") || key == _T("--encoder-type")) {
			tstring arg = getParam(argc, argv, i++);
			conf.encoder = encoderFtomString(arg);
			if (conf.encoder == (ENUM_ENCODER)-1) {
				PRINTF("--encoder-typeの指定が間違っています: %" PRITSTR "\n", arg.c_str());
			}
		}
		else if (key == _T("-e") || key == _T("--encoder")) {
			conf.encoderPath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("-eo") || key == _T("--encoder-option")) {
			conf.encoderOptions = getParam(argc, argv, i++);
		}
		else if (key == _T("-b") || key == _T("--bitrate")) {
			const auto arg = getParam(argc, argv, i++);
			int ret = sscanfT(arg.c_str(), _T("%lf:%lf:%lf:%lf"),
				&conf.bitrate.a, &conf.bitrate.b, &conf.bitrate.h264, &conf.bitrate.h265);
			if (ret < 3) {
				THROWF(ArgumentException, "--bitrateの指定が間違っています");
			}
			if (ret <= 3) {
				conf.bitrate.h265 = 2;
			}
			conf.autoBitrate = true;
		}
		else if (key == _T("-bcm") || key == _T("--bitrate-cm")) {
			const auto arg = getParam(argc, argv, i++);
			int ret = sscanfT(arg.c_str(), _T("%lf"), &conf.bitrateCM);
			if (ret == 0) {
				THROWF(ArgumentException, "--bitrate-cmの指定が間違っています");
			}
		}
		else if (key == _T("--2pass")) {
			conf.twoPass = true;
		}
		else if (key == _T("--splitsub")) {
			conf.splitSub = true;
		}
		else if (key == _T("-fmt") || key == _T("--format")) {
			const auto arg = getParam(argc, argv, i++);
			if (arg == _T("mp4")) {
				conf.format = FORMAT_MP4;
			}
			else if (arg == _T("mkv")) {
				conf.format = FORMAT_MKV;
			}
			else if (arg == _T("m2ts")) {
				conf.format = FORMAT_M2TS;
			}
			else if (arg == _T("ts")) {
				conf.format = FORMAT_TS;
			}
			else {
				THROWF(ArgumentException, "--formatの指定が間違っています: %" PRITSTR "", arg);
			}
		}
		else if (key == _T("--chapter")) {
			conf.chapter = true;
		}
		else if (key == _T("--subtitles")) {
			conf.subtitles = true;
		}
		else if (key == _T("--nicojk")) {
			nicojk = true;
		}
		else if (key == _T("-m") || key == _T("--muxer")) {
			conf.muxerPath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("-t") || key == _T("--timelineeditor")) {
			conf.timelineditorPath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("--mp4box")) {
			conf.mp4boxPath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("-j") || key == _T("--json")) {
			conf.outInfoJsonPath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("-f") || key == _T("--filter")) {
			conf.filterScriptPath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("-pf") || key == _T("--postfilter")) {
			conf.postFilterScriptPath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("-s") || key == _T("--serivceid")) {
			tstring sidstr = getParam(argc, argv, i++);
			if (sidstr.size() > 2 && sidstr.substr(0, 2) == _T("0x")) {
				// 16進
				conf.serviceId = std::stoi(sidstr.substr(2), NULL, 16);;
			}
			else {
				// 10進
				conf.serviceId = std::stoi(sidstr);
			}
		}
		else if (key == _T("--mpeg2decoder")) {
			tstring arg = getParam(argc, argv, i++);
			conf.decoderSetting.mpeg2 = decoderFromString(arg);
			if (conf.decoderSetting.mpeg2 == (DECODER_TYPE)-1) {
				PRINTF("--mpeg2decoderの指定が間違っています: %" PRITSTR "\n", arg.c_str());
			}
		}
		else if (key == _T("--h264decoder")) {
			tstring arg = getParam(argc, argv, i++);
			conf.decoderSetting.h264 = decoderFromString(arg);
			if (conf.decoderSetting.h264 == (DECODER_TYPE)-1) {
				PRINTF("--h264decoderの指定が間違っています: %" PRITSTR "\n", arg.c_str());
			}
		}
		else if (key == _T("-eb") || key == _T("--encode-buffer")) {
			conf.numEncodeBufferFrames = std::stoi(getParam(argc, argv, i++));
		}
		else if (key == _T("--ignore-no-logo")) {
			conf.ignoreNoLogo = true;
		}
		else if (key == _T("--ignore-no-drcsmap")) {
			conf.ignoreNoDrcsMap = true;
		}
		else if (key == _T("--ignore-nicojk-error")) {
			conf.ignoreNicoJKError = true;
		}
		else if (key == _T("--loose-logo-detection")) {
			conf.looseLogoDetection = true;
		}
		else if (key == _T("--max-fade-length")) {
			conf.maxFadeLength = std::stoi(getParam(argc, argv, i++));
		}
		else if (key == _T("--no-delogo")) {
			conf.noDelogo = true;
		}
		else if (key == _T("--x265-timefactor")) {
			const auto arg = getParam(argc, argv, i++);
			int ret = sscanfT(arg.c_str(), _T("%lf"), &conf.x265TimeFactor);
			if (ret == 0) {
				THROWF(ArgumentException, "--x265-timefactorの指定が間違っています");
			}
		}
		else if (key == _T("--logo")) {
			conf.logoPath.push_back(pathNormalize(getParam(argc, argv, i++)));
		}
		else if (key == _T("--drcs")) {
			auto path = pathNormalize(getParam(argc, argv, i++));
			conf.drcsMapPath = path;
			conf.drcsOutPath = pathGetDirectory(path);
			if (conf.drcsOutPath.size() == 0) {
				conf.drcsOutPath = _T(".");
			}
		}
		else if (key == _T("--chapter-exe")) {
			conf.chapterExePath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("--chapter-exe-options")) {
			conf.chapterExeOptions = getParam(argc, argv, i++);
		}
		else if (key == _T("--jls")) {
			conf.joinLogoScpPath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("--jls-cmd")) {
			conf.joinLogoScpCmdPath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("--jls-option")) {
			conf.joinLogoScpOptions = getParam(argc, argv, i++);
		}
		else if (key == _T("--trimavs")) {
			conf.trimavsPath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("--nicoass")) {
			conf.nicoConvAssPath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("--nicojk18")) {
			conf.nicojk18 = true;
		}
		else if (key == _T("--nicojklog")) {
			conf.useNicoJKLog = true;
		}
		else if (key == _T("-om") || key == _T("--cmoutmask")) {
			conf.cmoutmask = std::stol(getParam(argc, argv, i++));
		}
		else if (key == _T("--nicojkmask")) {
			conf.nicojkmask = std::stol(getParam(argc, argv, i++));
		}
		else if (key == _T("--dump")) {
			conf.dumpStreamInfo = true;
		}
		else if (key == _T("--systemavsplugin")) {
			conf.systemAvsPlugin = true;
		}
		else if (key == _T("--no-remove-tmp")) {
			conf.noRemoveTmp = true;
		}
		else if (key == _T("--dump-filter")) {
			conf.dumpFilter = true;
		}
		else if (key == _T("--resource-manager")) {
			const auto arg = getParam(argc, argv, i++);
			size_t inPipe, outPipe;
			int ret = sscanfT(arg.c_str(), _T("%zu:%zu"), &inPipe, &outPipe);
			if (ret < 2) {
				THROWF(ArgumentException, "--resource-managerの指定が間違っています");
			}
			conf.inPipe = (HANDLE)inPipe;
			conf.outPipe = (HANDLE)outPipe;
		}
		else if (key == _T("--affinity")) {
			const auto arg = getParam(argc, argv, i++);
			int ret = sscanfT(arg.c_str(), _T("%d:%lld"), &conf.affinityGroup, &conf.affinityMask);
			if (ret < 2) {
				THROWF(ArgumentException, "--affinityの指定が間違っています");
			}
		}
		else if (key == _T("--max-frames")) {
			conf.maxframes = std::stoi(getParam(argc, argv, i++));
		}
		else if (key == _T("--pmt-cut")) {
			const auto arg = getParam(argc, argv, i++);
			int ret = sscanfT(arg.c_str(), _T("%lf:%lf"),
				&conf.pmtCutSideRate[0], &conf.pmtCutSideRate[1]);
			if (ret < 2) {
				THROWF(ArgumentException, "--pmt-cutの指定が間違っています");
			}
		}
    else if (key == _T("--print-prefix")) {
      const auto arg = getParam(argc, argv, i++);
      if (arg == _T("default")) {
        conf.printPrefix = AMT_PREFIX_DEFAULT;
      }
      else if (arg == _T("time")) {
        conf.printPrefix = AMT_PREFIX_TIME;
      }
      else {
        THROWF(ArgumentException, "--print-prefixの指定が間違っています");
      }
    }
		else if (key.size() == 0) {
			continue;
		}
		else {
			// なぜか%lsで長い文字列食わすと落ちるので%sで表示
			THROWF(FormatException, "不明なオプション: %s", to_string(argv[i]));
		}
	}

	if (!nicojk) {
		conf.nicojkmask = 0;
	}

	// muxerのデフォルト値
	if (conf.muxerPath.size() == 0) {
		if (conf.format == FORMAT_MP4) {
			conf.muxerPath = _T("muxer.exe");
		}
		else if (conf.format == FORMAT_MKV) {
			conf.muxerPath = _T("mkvmerge.exe");
		}
		else {
			conf.muxerPath = _T("tsmuxer.exe");
		}
	}

	if (conf.mode == _T("ts") || conf.mode == _T("g")) {
		if (conf.srcFilePath.size() == 0) {
			THROWF(ArgumentException, "入力ファイルを指定してください");
		}
		if (conf.outVideoPath.size() == 0) {
			THROWF(ArgumentException, "出力ファイルを指定してください");
		}
	}

	if (conf.mode == _T("drcs") || conf.mode == _T("cm") || starts_with(conf.mode, _T("probe_"))) {
		if (conf.srcFilePath.size() == 0) {
			THROWF(ArgumentException, "入力ファイルを指定してください");
		}
	}

	if (conf.chapter && !conf.ignoreNoLogo) {
		if (conf.logoPath.size() == 0) {
			THROW(ArgumentException, "ロゴが指定されていません");
		}
	}

	// CM解析は４つ揃える必要がある
	if (conf.chapterExePath.size() > 0 || conf.joinLogoScpPath.size() > 0) {
		if (conf.chapterExePath.size() == 0) {
			THROW(ArgumentException, "chapter_exe.exeへのパスが設定されていません");
		}
		if (conf.joinLogoScpPath.size() == 0) {
			THROW(ArgumentException, "join_logo_scp.exeへのパスが設定されていません");
		}
	}

	if (conf.maxFadeLength < 0) {
		THROW(ArgumentException, "max-fade-lengthが不正");
	}

	if (conf.mode == _T("enctask")) {
		// 必要ない
		conf.workDir = _T("");
	}

	// exeを探す
	if (conf.mode != _T("drcs") && !starts_with(conf.mode, _T("probe_"))) {
		auto search = [](const tstring& path) {
			return pathNormalize(SearchExe(path));
		};
		conf.chapterExePath = search(conf.chapterExePath);
		conf.encoderPath = search(conf.encoderPath);
		conf.joinLogoScpPath = search(conf.joinLogoScpPath);
		conf.nicoConvAssPath = search(conf.nicoConvAssPath);
		conf.nicoConvChSidPath = pathGetDirectory(conf.nicoConvAssPath) + _T("/ch_sid.txt");
		conf.mp4boxPath = search(conf.mp4boxPath);
		conf.muxerPath = search(conf.muxerPath);
		conf.timelineditorPath = search(conf.timelineditorPath);
	}

	return std::unique_ptr<ConfigWrapper>(new ConfigWrapper(ctx, conf));
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

static int amatsukazeTranscodeMain(AMTContext& ctx, const ConfigWrapper& setting) {
	try {

		if (setting.isSubtitlesEnabled()) {
			// DRCSマッピングをロード
			ctx.loadDRCSMapping(setting.getDRCSMapPath());
		}

		tstring mode = setting.getMode();
		if (mode == _T("ts") || mode == _T("cm"))
			transcodeMain(ctx, setting);
		else if (mode == _T("g"))
			transcodeSimpleMain(ctx, setting);
		else if (mode == _T("drcs"))
			searchDrcsMain(ctx, setting);
		else if (mode == _T("probe_subtitles"))
			detectSubtitleMain(ctx, setting);
		else if (mode == _T("probe_audio"))
			detectAudioMain(ctx, setting);

		else if (mode == _T("test_print_crc"))
			test::PrintCRCTable(ctx, setting);
		else if (mode == _T("test_crc"))
			test::CheckCRC(ctx, setting);
		else if (mode == _T("test_read_bits"))
			test::ReadBits(ctx, setting);
		else if (mode == _T("test_auto_buffer"))
			test::CheckAutoBuffer(ctx, setting);
		else if (mode == _T("test_verifympeg2ps"))
			test::VerifyMpeg2Ps(ctx, setting);
		else if (mode == _T("test_readts"))
			test::ReadTS(ctx, setting);
		else if (mode == _T("test_aacdec"))
			test::AacDecode(ctx, setting);
		else if (mode == _T("test_wavewrite"))
			test::WaveWriteHeader(ctx, setting);
		else if (mode == _T("test_process"))
			test::ProcessTest(ctx, setting);
		else if (mode == _T("test_streamreform"))
			test::FileStreamInfo(ctx, setting);
		else if (mode == _T("test_parseargs"))
			test::ParseArgs(ctx, setting);
		else if (mode == _T("test_lossless"))
			test::LosslessFileTest(ctx, setting);
		else if (mode == _T("test_logoframe"))
			test::LogoFrameTest(ctx, setting);
		else if (mode == _T("test_dualmono"))
			test::SplitDualMonoAAC(ctx, setting);
		else if (mode == _T("test_aacdecode"))
			test::AACDecodeTest(ctx, setting);
		else if (mode == _T("test_ass"))
			test::CaptionASS(ctx, setting);
		else if (mode == _T("test_eo"))
			test::EncoderOptionParse(ctx, setting);
		else if (mode == _T("test_perf"))
			test::DecodePerformance(ctx, setting);
		else if (mode == _T("test_zone"))
			test::BitrateZones(ctx, setting);
		else if (mode == _T("test_zone2"))
			test::BitrateZonesBug(ctx, setting);
		else if (mode == _T("test_printf"))
			test::PrintfBug(ctx, setting);
		else if (mode == _T("test_resource"))
			test::ResourceTest(ctx, setting);

		else
			ctx.errorF("--modeの指定が間違っています: %s\n", mode.c_str());

		return 0;
	}
	catch (const NoLogoException&) {
		// ロゴ無しは100とする
		return 100;
	}
	catch (const NoDrcsMapException&) {
		// DRCSマッピングなしは101とする
		return 101;
	}
	catch (const AvisynthError& avserror) {
		ctx.error("AviSynth Error");
		ctx.error(avserror.msg);
		return 2;
	}
	catch (const Exception&) {
		return 1;
	}
}

__declspec(dllexport) int AmatsukazeCLI(int argc, const wchar_t* argv[]) {
	try {
		printCopyright();

		AMTContext ctx;

		ctx.setDefaultCP();

		auto setting = parseArgs(ctx, argc, argv);

    ctx.setTimePrefix(setting->getPrintPrefix() == AMT_PREFIX_TIME);

		// CPUアフィニティを設定
		if (!SetCPUAffinity(setting->getAffinityGroup(), setting->getAffinityMask())) {
			ctx.error("CPUアフィニティを設定できませんでした");
		}

		// FFMPEGライブラリ初期化
		InitializeCriticalSection(&g_log_crisec);
		av_log_set_callback(amatsukaze_av_log_callback);
		av_register_all();

		// キャプションDLL初期化
		InitializeCPW();

		return amatsukazeTranscodeMain(ctx, *setting);
	}
	catch (const Exception&) {
		// parseArgsでエラー
		printHelp(argv[0]);
		return 1;
	}
}
