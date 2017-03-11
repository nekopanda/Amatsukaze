/**
* Amtasukaze CLI Entry point
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
/*
コンソールから立ち上がったときにコンソールにアタッチするコードは
以下のライセンスで公開されていたのを使っています。

Copyright (c) 2013, 2016 Daniel Tillett
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/
#include "AmatsukazeCLI.hpp"

#ifdef _MSC_VER
#include <shellapi.h>

// Attach output of application to parent console
static bool attachOutputToConsole(void) {
	HANDLE consoleHandleOut, consoleHandleError;

	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		// Redirect unbuffered STDOUT to the console
		consoleHandleOut = GetStdHandle(STD_OUTPUT_HANDLE);
		if (consoleHandleOut != INVALID_HANDLE_VALUE) {
			freopen("CONOUT$", "w", stdout);
			setvbuf(stdout, NULL, _IONBF, 0);
		}
		else {
			return false;
		}
		// Redirect unbuffered STDERR to the console
		consoleHandleError = GetStdHandle(STD_ERROR_HANDLE);
		if (consoleHandleError != INVALID_HANDLE_VALUE) {
			freopen("CONOUT$", "w", stderr);
			setvbuf(stderr, NULL, _IONBF, 0);
		}
		else {
			return false;
		}
		return true;
	}
	//Not a console application
	return false;
}

// Send the "enter" to the console to release the command prompt 
// on the parent console
static void sendEnterKey(void) {
	INPUT ip;
	// Set up a generic keyboard event.
	ip.type = INPUT_KEYBOARD;
	ip.ki.wScan = 0; // hardware scan code for key
	ip.ki.time = 0;
	ip.ki.dwExtraInfo = 0;

	// Send the "Enter" key
	ip.ki.wVk = 0x0D; // virtual-key code for the "Enter" key
	ip.ki.dwFlags = 0; // 0 for key press
	SendInput(1, &ip, sizeof(INPUT));

	// Release the "Enter" key
	ip.ki.dwFlags = KEYEVENTF_KEYUP; // KEYEVENTF_KEYUP for key release
	SendInput(1, &ip, sizeof(INPUT));
}

int windowsMain()
{
	int argc;
	LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == NULL) {
		printf("CommandLineToArgvW failed\n");
		return 1;
	}
	try {
		printCopyright();
		return amatsukazeTranscodeMain(parseArgs(argc, argv));
	}
	catch (Exception e) {
		// parseArgsでエラー
		printHelp(argv[0]);
		return 1;
	}
	// 本当は使い終わったらargvは解放しないといけない
	// LocalFree(argv);
}

// コンソールなしで立ち上げるにはmainCRTStartupは使えないのでWinMainCRTStartupを使う
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	bool console = attachOutputToConsole();
	int ret = windowsMain();
	if (console) {
		sendEnterKey();
	}
	return ret;
}
#else
int main(int argc, char* argv[]) {
	try {
		printCopyright();
		return amatsukazeTranscodeMain(parseArgs(argc, argv));
	}
	catch (Exception e) {
		// parseArgsでエラー
		printHelp(argv[0]);
		return 1;
	}
}
#endif
