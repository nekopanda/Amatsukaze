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

class HostProcessComm : AMTObject
{
	enum PipeCommand {
		FinishAnalyze = 1,
		StartFilter = 2,
		FinishFilter = 3,
		FinishEncode = 4,
		Error = 5
	};

	HANDLE inPipe;
	HANDLE outPipe;

	void write(MemoryChunk mc) {
		DWORD bytesWritten = 0;
		if (WriteFile(outPipe, mc.data, (DWORD)mc.length, &bytesWritten, NULL) == 0) {
			THROW(RuntimeException, "failed to write to stdin pipe");
		}
		if (bytesWritten != mc.length) {
			THROW(RuntimeException, "failed to write to stdin pipe (bytes written mismatch)");
		}
	}

	void writeCommand(int cmd) {
		write(MemoryChunk((uint8_t*)&cmd, 4));
	}

	void write(int cmd, const std::string& json) {
		write(MemoryChunk((uint8_t*)&cmd, 4));
		int sz = (int)json.size();
		write(MemoryChunk((uint8_t*)&sz, 4));
		write(MemoryChunk((uint8_t*)json.data(), sz));
	}

	int readCommand() {
		DWORD bytesRead = 0;
		int32_t cmd;
		if (ReadFile(inPipe, &cmd, 4, &bytesRead, NULL) == 0) {
			THROW(RuntimeException, "failed to read from pipe");
		}
		if (bytesRead != 4) {
			THROW(RuntimeException, "failed to read from pipe");
		}
		return cmd;
	}

public:
	HostProcessComm(AMTContext& ctx, HANDLE inPipe, HANDLE outPipe)
		: AMTObject(ctx)
		, inPipe(inPipe)
		, outPipe(outPipe)
	{ }

	void postAnalyzeFinished(const std::string& json) {
		write(FinishAnalyze, json);
	}
	void waitStartFilter() {
		int cmd = readCommand();
		if (cmd != StartFilter) {
			if(cmd == Error) THROW(RuntimeException, "Canceled");
			THROWF(RuntimeException, "Unexpected command %d", cmd);
		}
	}
	void postFilterFinished() {
		writeCommand(FinishFilter);
	}
	void waitFinish() {
		int cmd = readCommand();
		if (cmd != FinishEncode) {
			if (cmd == Error) THROW(RuntimeException, "Canceled");
			THROWF(RuntimeException, "Unexpected command %d", cmd);
		}
	}
};

struct EncodeTaskInfo
{
	std::vector<std::string> encoderArgs;
	VideoFormat infmt;
};

void SaveEncodeTaskInfo(const std::string& path,
	const std::vector<std::string>& encoderArgs,
	const VideoFormat& infmt)
{
	File file(path, "wb");
	file.writeValue(infmt);
	file.writeValue((int)encoderArgs.size());
	for (int i = 0; i < (int)encoderArgs.size(); ++i) {
		file.writeString(encoderArgs[i]);
	}
}

std::unique_ptr<EncodeTaskInfo> LoadEncodeTaskInfo(const std::string& path)
{
	File file(path, "rb");
	auto info = std::unique_ptr<EncodeTaskInfo>(new EncodeTaskInfo());
	info->infmt = file.readValue<VideoFormat>();
	info->encoderArgs.resize(file.readValue<int>());
	for (int i = 0; i < (int)info->encoderArgs.size(); ++i) {
		info->encoderArgs[i] = file.readString();
	}
	return info;
}
