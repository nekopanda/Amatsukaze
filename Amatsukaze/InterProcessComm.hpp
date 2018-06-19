/**
* Amtasukaze Communication to Host Process
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <memory>

#include "StreamUtils.hpp"

static std::vector<char> toUTF8String(const std::string str) {
	if (str.size() == 0) {
		return std::vector<char>();
	}
	int intlen = (int)str.size() * 2;
	auto wc = std::unique_ptr<wchar_t[]>(new wchar_t[intlen]);
	intlen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)str.size(), wc.get(), intlen);
	if (intlen == 0) {
		THROW(RuntimeException, "MultiByteToWideChar failed");
	}
	int dstlen = WideCharToMultiByte(CP_UTF8, 0, wc.get(), intlen, NULL, 0, NULL, NULL);
	if (dstlen == 0) {
		THROW(RuntimeException, "MultiByteToWideChar failed");
	}
	std::vector<char> ret(dstlen);
	WideCharToMultiByte(CP_UTF8, 0, wc.get(), intlen, ret.data(), (int)ret.size(), NULL, NULL);
	return ret;
}

static std::string toJsonString(const std::string str) {
	if (str.size() == 0) {
		return str;
	}
	std::vector<char> utf8 = toUTF8String(str);
	std::vector<char> ret;
	for (char c : utf8) {
		switch (c) {
		case '\"':
			ret.push_back('\\');
			ret.push_back('\"');
			break;
		case '\\':
			ret.push_back('\\');
			ret.push_back('\\');
			break;
		case '/':
			ret.push_back('\\');
			ret.push_back('/');
			break;
		case '\b':
			ret.push_back('\\');
			ret.push_back('b');
			break;
		case '\f':
			ret.push_back('\\');
			ret.push_back('f');
			break;
		case '\n':
			ret.push_back('\\');
			ret.push_back('n');
			break;
		case '\r':
			ret.push_back('\\');
			ret.push_back('r');
			break;
		case '\t':
			ret.push_back('\\');
			ret.push_back('t');
			break;
		default:
			ret.push_back(c);
		}
	}
	return std::string(ret.begin(), ret.end());
}

enum PipeCommand {
  HOST_CMD_TSAnalyze = 0,
  HOST_CMD_CMAnalyze,
  HOST_CMD_Filter,
  HOST_CMD_Encode,
  HOST_CMD_Mux,

	HOST_CMD_NoWait = 0x100,
};

class ResourceManger : AMTObject
{
	HANDLE inPipe;
	HANDLE outPipe;

	void write(MemoryChunk mc) const {
		DWORD bytesWritten = 0;
		if (WriteFile(outPipe, mc.data, (DWORD)mc.length, &bytesWritten, NULL) == 0) {
			THROW(RuntimeException, "failed to write to stdin pipe");
		}
		if (bytesWritten != mc.length) {
			THROW(RuntimeException, "failed to write to stdin pipe (bytes written mismatch)");
		}
	}

	void read(MemoryChunk mc) const {
		int offset = 0;
		while (offset < mc.length) {
			DWORD bytesRead = 0;
			if (ReadFile(inPipe, mc.data + offset, (int)mc.length - offset, &bytesRead, NULL) == 0) {
				THROW(RuntimeException, "failed to read from pipe");
			}
			offset += bytesRead;
		}
	}

	void writeCommand(int cmd) const {
		write(MemoryChunk((uint8_t*)&cmd, 4));
	}

  /*
	void write(int cmd, const std::string& json) {
		write(MemoryChunk((uint8_t*)&cmd, 4));
		int sz = (int)json.size();
		write(MemoryChunk((uint8_t*)&sz, 4));
		write(MemoryChunk((uint8_t*)json.data(), sz));
	}
  */

	int readCommand(int expected) const {
		DWORD bytesRead = 0;
		int32_t cmd[2];
		read(MemoryChunk((uint8_t*)cmd, sizeof(cmd)));
    if (cmd[0] != expected) {
      THROW(RuntimeException, "invalid return command");
    }
		return cmd[1];
	}

public:
	ResourceManger(AMTContext& ctx, HANDLE inPipe, HANDLE outPipe)
		: AMTObject(ctx)
		, inPipe(inPipe)
		, outPipe(outPipe)
	{ }

	// リソース確保できなかったら-1
	int request(PipeCommand phase) const {
		if (inPipe == INVALID_HANDLE_VALUE) {
			return 0;
		}
		writeCommand(phase | HOST_CMD_NoWait);
		return readCommand(phase);
	}

	// リソース確保できるまで待つ
  int wait(PipeCommand phase) const {
    if (inPipe == INVALID_HANDLE_VALUE) {
      return 0;
    }
    int ret = request(phase);
    if (ret == -1) {
      writeCommand(phase);
      ctx.progress("リソース待ち ...");
      Stopwatch sw; sw.start();
      ret = readCommand(phase);
      ctx.info("リソース待ち %.2f秒", sw.getAndReset());
    }
		return ret;
	}
};
