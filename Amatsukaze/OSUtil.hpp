/**
* Amtasukaze OS Utility
* Copyright (c) 2017 Nekopanda
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
  PathRemoveFileSpec(buf);
  return buf;
}

// プロセスに設定されているコア数を取得
int GetProcessorCount()
{
  DWORD_PTR procMask, sysMask;
  if (GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask)) {
    int cnt = 0;
    for (int i = 0; i < 64; ++i) {
      if (procMask & (DWORD_PTR(1) << i)) cnt++;
    }
    return cnt;
  }
  return 8; // 失敗したら適当な値にしておく
}
