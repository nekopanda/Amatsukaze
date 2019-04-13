/**
* Amtasukaze OS Utility
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <Windows.h>
#include "Shlwapi.h"

#include <string>

#include "CoreUtils.hpp"

extern HMODULE g_DllHandle;

std::wstring GetModulePath() {
	wchar_t buf[AMT_MAX_PATH] = { 0 };
	GetModuleFileNameW(g_DllHandle, buf, AMT_MAX_PATH);
	return buf;
}

std::wstring GetModuleDirectory() {
	wchar_t buf[AMT_MAX_PATH] = { 0 };
	GetModuleFileNameW(g_DllHandle, buf, AMT_MAX_PATH);
	PathRemoveFileSpecW(buf);
	return buf;
}

std::wstring SearchExe(const std::wstring& name) {
	wchar_t buf[AMT_MAX_PATH] = { 0 };
	if (!SearchPathW(0, name.c_str(), 0, AMT_MAX_PATH, buf, 0)) {
		return name;
	}
	return buf;
}

//std::wstring GetDirectoryPath(const std::wstring& name) {
//	wchar_t buf[AMT_MAX_PATH] = { 0 };
//	std::copy(name.begin(), name.end(), buf);
//	PathRemoveFileSpecW(buf);
//	return buf;
//}

bool DirectoryExists(const std::wstring& dirName_in)
{
	DWORD ftyp = GetFileAttributesW(dirName_in.c_str());
	if (ftyp == INVALID_FILE_ATTRIBUTES)
		return false;
	if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
		return true;
	return false;
}

// dirpathは 終端\\なし
// patternは "*.*" とか
// ディレクトリ名を含まないファイル名リストが返る
std::vector<std::wstring> GetDirectoryFiles(const std::wstring& dirpath, const std::wstring& pattern)
{
	std::wstring search = dirpath + _T("/") + pattern;
	std::vector<std::wstring> result;
	WIN32_FIND_DATAW findData;
	HANDLE hFind = FindFirstFileW(search.c_str(), &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		THROWF(IOException, "ファイル列挙に失敗: %s", search);
	}
	do {
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			// ディレクトリ
		}
		else {
			// ファイル
			result.push_back(findData.cFileName);
		}
	} while (FindNextFileW(hFind, &findData));
	FindClose(hFind);
	return result;
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
