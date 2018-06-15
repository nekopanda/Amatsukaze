/**
* Amtasukaze OS Utility
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <Windows.h>
#include "Shlwapi.h"

#include <string>

extern HMODULE g_DllHandle;

std::string GetModulePath() {
	char buf[MAX_PATH] = { 0 };
	GetModuleFileName(g_DllHandle, buf, MAX_PATH);
	return buf;
}

std::string GetModuleDirectory() {
	char buf[MAX_PATH] = { 0 };
	GetModuleFileName(g_DllHandle, buf, MAX_PATH);
	PathRemoveFileSpecA(buf);
	return buf;
}

std::string SearchExe(const std::string& name) {
	char buf[MAX_PATH] = { 0 };
	if (!SearchPath(0, name.c_str(), 0, MAX_PATH, buf, 0)) {
		return name;
	}
	return buf;
}

std::string GetDirectoryPath(const std::string& name) {
	char buf[MAX_PATH] = { 0 };
	std::copy(name.begin(), name.end(), buf);
	PathRemoveFileSpecA(buf);
	return buf;
}

// 現在のスレッドに設定されているコア数を取得
int GetProcessorCount()
{
	GROUP_AFFINITY gaffinity;
	if (GetThreadGroupAffinity(GetCurrentThread(), &gaffinity)) {
		int cnt = 0;
		for (int i = 0; i < 64; ++i) {
			if (gaffinity.Mask & (DWORD_PTR(1) << i)) cnt++;
		}
		return cnt;
	}
	return 8; // 失敗したら適当な値にしておく
}
