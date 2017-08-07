/**
* Amtasukaze CLI Entry point
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#define _USE_MATH_DEFINES
#include "AmatsukazeCLI.hpp"
#include "AMTSource.hpp"

int wmain(int argc, wchar_t* argv[]) {
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
	catch (Exception e) {
		// parseArgsでエラー
		printHelp(argv[0]);
		return 1;
	}
}
