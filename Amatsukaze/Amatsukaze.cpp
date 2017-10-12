/**
* Amtasukaze Compile Target
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#define _USE_MATH_DEFINES
// avisynth‚ÉƒŠƒ“ƒN‚µ‚Ä‚¢‚é‚Ì‚Å
#define AVS_LINKAGE_DLLIMPORT
#include "AmatsukazeCLI.hpp"

HMODULE g_DllHandle;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
	if (dwReason == DLL_PROCESS_ATTACH) g_DllHandle = hModule;
	return TRUE;
}
