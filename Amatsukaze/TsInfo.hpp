#pragma once

#include "Mpeg2TsParser.hpp"

#include <stdint.h>

#include <memory>

class TsInfo : public AMTObject {
public:
	TsInfo(AMTContext& ctx)
		: AMTObject(ctx)
		, parser(ctx)
	{ }

	bool ReadFile(const char* filepath) {
		try {
			enum {
				BUFSIZE = 4 * 1024 * 1024,
				MAX_BYTES = 100 * 1024 * 1024
			};
			auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
			MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
			File srcfile(filepath, "rb");
			// ファイルの真ん中を読む
			srcfile.seek(srcfile.size() / 2, SEEK_SET);
			size_t totalRead = 0;
			size_t readBytes;
			SpTsPacketParser packetParser(*this);
			do {
				readBytes = srcfile.read(buffer);
				packetParser.inputTS(MemoryChunk(buffer.data, readBytes));
				if (parser.isOK()) return true;
				totalRead += readBytes;
			} while (readBytes == buffer.length && totalRead < MAX_BYTES);
			THROW(FormatException, "TSファイルに情報がありません");
		}
		catch (Exception& exception) {
			ctx.setError(exception);
		}
		return false;
	}

	// ref intで受け取る
	void GetDay(int* y, int* m, int* d) {
		parser.getTime().getDay(*y, *m, *d);
	}

	void GetTime(int* h, int* m, int* s) {
		parser.getTime().getTime(*h, *m, *s);
	}

	int GetNumProgram() {
		return (int)parser.getProgramList().size();
	}

	int GetProgramNumber(int i) {
		return parser.getProgramList()[i];
	}

	int GetNumService() {
		return (int)parser.getServiceList().size();
	}

	int GetServiceId(int i) {
		return parser.getServiceList()[i].serviceId;
	}

	// IntPtrで受け取ってMarshal.PtrToStringAnsiで変換
	const char* GetProviderName(int i) {
		return parser.getServiceList()[i].provider.c_str();
	}

	const char* GetServiceName(int i) {
		return parser.getServiceList()[i].name.c_str();
	}

private:
	class SpTsPacketParser : public TsPacketParser {
		TsInfo& this_;
	public:
		SpTsPacketParser(TsInfo& this_)
			: TsPacketParser(this_.ctx)
			, this_(this_) { }

		virtual void onTsPacket(TsPacket packet) {
			this_.parser.inputTsPacket(-1, packet);
		}
	};

	TsInfoParser parser;
};

// C API for P/Invoke
extern "C" __declspec(dllexport) void* TsInfo_Create(AMTContext* ctx) { return new TsInfo(*ctx); }
extern "C" __declspec(dllexport) void TsInfo_Delete(TsInfo* ptr) { delete ptr; }
extern "C" __declspec(dllexport) bool TsInfo_ReadFile(TsInfo* ptr, const char* filepath) { return ptr->ReadFile(filepath); }
extern "C" __declspec(dllexport) void TsInfo_GetDay(TsInfo* ptr, int* y, int* m, int* d) { ptr->GetDay(y, m, d); }
extern "C" __declspec(dllexport) void TsInfo_GetTime(TsInfo* ptr, int* h, int* m, int* s) { return ptr->GetTime(h, m, s); }
extern "C" __declspec(dllexport) int TsInfo_GetNumProgram(TsInfo* ptr) { return ptr->GetNumProgram(); }
extern "C" __declspec(dllexport) int TsInfo_GetProgramNumber(TsInfo* ptr, int i) { return ptr->GetProgramNumber(i); }
extern "C" __declspec(dllexport) int TsInfo_GetNumService(TsInfo* ptr) { return ptr->GetNumService(); }
extern "C" __declspec(dllexport) int TsInfo_GetServiceId(TsInfo* ptr, int i) { return ptr->GetServiceId(i); }
extern "C" __declspec(dllexport) const char* TsInfo_GetProviderName(TsInfo* ptr, int i) { return ptr->GetProviderName(i); }
extern "C" __declspec(dllexport) const char* TsInfo_GetServiceName(TsInfo* ptr, int i) { return ptr->GetServiceName(i); }
