/**
* Amtasukaze CLI Entry point
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#include "AmatsukazeCLI.hpp"

int wmain(int argc, wchar_t* argv[]) {
	try {
		printCopyright();

		TranscoderSetting setting = parseArgs(argc, argv);

		// FFMPEGライブラリ初期化
		av_register_all();

		return amatsukazeTranscodeMain(setting);
	}
	catch (Exception e) {
		// parseArgsでエラー
		printHelp(argv[0]);
		return 1;
	}
}
