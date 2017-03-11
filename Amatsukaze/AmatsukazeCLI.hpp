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
#define PRITSTR "ls"
#define _T(s) L ## s
#else
namespace std { typedef string tstring; }
typedef char tchar;
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
	printf(
		"Amatsukaze - Automated MPEG2-TS Transcode Utility\n"
		"Built on %s %s\n"
		"Copyright (c) 2017 Nekopanda\n", __DATE__, __TIME__);
}

static void printHelp(const tchar* bin) {
	printf(
		"%" PRITSTR " <オプション> -i <input.ts> -o <output.mp4>\n"
		"オプション []はデフォルト値 \n"
		"  -i|--input  <パス>  入力TSファイルパス\n"
		"  -o|--output <パス>  出力MP4ファイルパス\n"
		"  -s|--serviceid <数値> 処理するサービスIDを指定[]\n"
		"  -w|--work   <パス>  一時ファイルパス[./]\n"
		"  -et|--encoder-type <タイプ>  使用エンコーダタイプ[x264]\n"
		"                      対応エンコーダ: x264,x265,QSVEnc\n"
		"  -e|--encoder <パス> エンコーダパス[x264.exe]\n"
		"  -eo|--encoder-opotion <オプション> エンコーダへ渡すオプション[--crf 23]\n"
		"                      入力ファイルの解像度、アスペクト比、インタレースフラグ、\n"
		"                      フレームレート、カラーマトリクス等は自動で追加されるので不要\n"
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

static std::tstring pathRemoveExtension(const std::tstring& path) {
	size_t lastsplit = path.rfind(_T('/'));
	size_t namebegin = (lastsplit == std::string::npos)
		? 0
		: lastsplit + 1;
	size_t dotpos = path.find(_T('.'), namebegin);
	size_t len = (dotpos == std::tstring::npos)
		? path.size()
		: dotpos;
	return path.substr(0, len);
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

static TranscoderSetting parseArgs(int argc, tchar* argv[])
{
	std::tstring tsFilePath;
	std::tstring outVideoPath;
	std::tstring workDir = _T("./");
	std::tstring outInfoJsonPath;
	ENUM_ENCODER encoder = ENUM_ENCODER();
	std::tstring encoderPath = _T("x264.exe");
	std::tstring encoderOptions = _T("--crf 23");
	std::tstring muxerPath = _T("muxer.exe");
	std::tstring timelineditorPath = _T("timelineeditor.exe");
	int serviceId = -1;
	bool dumpStreamInfo = bool();

	for (int i = 1; i < argc; ++i) {
		std::tstring key = argv[i];
		if (key == _T("-i") || key == _T("--input")) {
			tsFilePath = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("-o") || key == _T("--output")) {
			outVideoPath =
				pathRemoveExtension(pathNormalize(getParam(argc, argv, i++)));
		}
		else if (key == _T("-w") || key == _T("--work")) {
			workDir = pathNormalize(getParam(argc, argv, i++));
		}
		else if (key == _T("-et") || key == _T("--encoder-type")) {
			std::tstring arg = getParam(argc, argv, i++);
			encoder = encoderFtomString(arg);
			if (encoder == (ENUM_ENCODER)-1) {
				printf("--encoder-typeの指定が間違っています: %" PRITSTR "\n", arg.c_str());
			}
		}
		else if (key == _T("-e") || key == _T("--encoder")) {
			encoderPath = getParam(argc, argv, i++);
		}
		else if (key == _T("-eo") || key == _T("--encoder-option")) {
			encoderOptions = getParam(argc, argv, i++);
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

	if (tsFilePath.size() == 0) {
		THROWF(ArgumentException, "入力ファイルを指定してください");
	}
	if (outVideoPath.size() == 0) {
		THROWF(ArgumentException, "出力ファイルを指定してください");
	}

	TranscoderSetting setting = TranscoderSetting();
	setting.tsFilePath = to_string(tsFilePath);
	setting.outVideoPath = to_string(outVideoPath);
	setting.intFileBasePath = to_string(workDir) + "/amt" + std::to_string(time(NULL));
	setting.audioFilePath = setting.intFileBasePath + "-audio.dat";
	setting.outInfoJsonPath = to_string(outInfoJsonPath);
	setting.encoder = encoder;
	setting.encoderPath = to_string(encoderPath);
	setting.encoderOptions = to_string(encoderOptions);
	setting.muxerPath = to_string(muxerPath);
	setting.timelineditorPath = to_string(timelineditorPath);
	setting.serviceId = serviceId;
	setting.dumpStreamInfo = dumpStreamInfo;

	return setting;
}

static int amatsukazeTranscodeMain(const TranscoderSetting& setting) {
	try {
		AMTContext ctx;
		transcodeMain(ctx, setting);
		return 0;
	}
	catch (Exception e) {
		return 1;
	}
}
