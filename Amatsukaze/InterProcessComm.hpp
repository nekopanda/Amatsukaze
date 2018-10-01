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

static std::vector<char> toUTF8String(const std::wstring& str) {
	if (str.size() == 0) {
		return std::vector<char>();
	}
	int dstlen = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0, NULL, NULL);
	if (dstlen == 0) {
		THROW(RuntimeException, "MultiByteToWideChar failed");
	}
	std::vector<char> ret(dstlen);
	WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), ret.data(), (int)ret.size(), NULL, NULL);
	return ret;
}

static std::string toJsonString(const tstring& str) {
	if (str.size() == 0) {
		return std::string();
	}
	std::vector<char> utf8 = toUTF8String(to_wstring(str));
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

struct ResourceAllocation {
	int32_t gpuIndex;
	int32_t group;
	uint64_t mask;

	bool IsFailed() const {
		return gpuIndex == -1;
	}
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

	static ResourceAllocation DefaultAllocation() {
		ResourceAllocation res = { 0, -1, 0 };
		return res;
	}

	ResourceAllocation readCommand(int expected) const {
		DWORD bytesRead = 0;
		int32_t cmd;
		ResourceAllocation res;
		read(MemoryChunk((uint8_t*)&cmd, sizeof(cmd)));
		if (cmd != expected) {
			THROW(RuntimeException, "invalid return command");
		}
		read(MemoryChunk((uint8_t*)&res, sizeof(res)));
		return res;
	}

public:
	ResourceManger(AMTContext& ctx, HANDLE inPipe, HANDLE outPipe)
		: AMTObject(ctx)
		, inPipe(inPipe)
		, outPipe(outPipe)
	{ }

	ResourceAllocation request(PipeCommand phase) const {
		if (inPipe == INVALID_HANDLE_VALUE) {
			return DefaultAllocation();
		}
		writeCommand(phase | HOST_CMD_NoWait);
		return readCommand(phase);
	}

	// リソース確保できるまで待つ
	ResourceAllocation wait(PipeCommand phase) const {
		if (inPipe == INVALID_HANDLE_VALUE) {
			return DefaultAllocation();
		}
		ResourceAllocation ret = request(phase);
		if (ret.IsFailed()) {
			writeCommand(phase);
			ctx.progress("リソース待ち ...");
			Stopwatch sw; sw.start();
			ret = readCommand(phase);
			ctx.infoF("リソース待ち %.2f秒", sw.getAndReset());
		}
		return ret;
	}
};
