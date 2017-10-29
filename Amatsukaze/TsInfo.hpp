#pragma once

#include "Mpeg2TsParser.hpp"
#include "AribString.hpp"

#include <stdint.h>

#include <memory>

struct ProgramInfo {
	int programId;
	bool hasVideo;
	int videoPid; // 同じ映像の別サービスの注意
	VideoFormat videoFormat;
};

struct ServiceInfo {
	int serviceId;
	std::wstring provider;
	std::wstring name;
};

class TsInfoParser : public AMTObject {
public:
	TsInfoParser(AMTContext& ctx)
		: AMTObject(ctx)
		, patOK(false)
		, serviceOK(false)
		, timeOK(false)
	{
		handlerTable.addConstant(0x0000, newPsiHandler()); // PAT
		handlerTable.addConstant(0x0011, newPsiHandler()); // SDT/BAT
		handlerTable.addConstant(0x0014, newPsiHandler()); // TDT/TOT
	}

	void inputTsPacket(int64_t clock, TsPacket packet) {
		TsPacketHandler* handler = handlerTable.get(packet.PID());
		if (handler != NULL) {
			handler->onTsPacket(clock, packet);
		}
	}

	bool isOK(bool programOnly) const {
		for (int i = 0; i < numPrograms; ++i) {
			if (programList[i].programOK == false) return false;
		}
		if (programOnly) {
			return patOK;
		}
		return patOK && serviceOK && timeOK;
	}

	int getNumPrograms() const {
		return (int)programList.size();
	}

	const ProgramInfo& getProgram(int i) const {
		return programList[i];
	}

	const std::vector<ServiceInfo>& getServiceList() const {
		return serviceList;
	}

	JSTTime getTime() const {
		return time;
	}

	std::vector<int> getSetPids() const {
		return handlerTable.getSetPids();
	}

private:
	class PSIDelegator : public PsiUpdatedDetector {
		TsInfoParser& this_;
	public:
		PSIDelegator(AMTContext&ctx, TsInfoParser& this_) : PsiUpdatedDetector(ctx), this_(this_) { }
		virtual void onTableUpdated(PsiSection section) {
			this_.onPsiUpdated(section);
		}
	};
	struct ProgramItem : ProgramInfo {
		bool programOK;
		std::unique_ptr<PSIDelegator> pmtHandler;
		std::unique_ptr<VideoFrameParser> videoHandler;

		ProgramItem()
			: programOK(false)
		{
			programId = -1;
			hasVideo = false;
			videoPid = -1;
			videoFormat = VideoFormat();
		}
	};
	class SpVideoFrameParser : public VideoFrameParser {
		TsInfoParser& this_;
		ProgramItem* item;
	public:
		SpVideoFrameParser(AMTContext&ctx, TsInfoParser& this_, ProgramItem* item)
			: VideoFrameParser(ctx), this_(this_), item(item) { }
	protected:
		virtual void onVideoPesPacket(int64_t clock, const std::vector<VideoFrameInfo>& frames, PESPacket packet) { }
		virtual void onVideoFormatChanged(VideoFormat fmt) {
			// 同じPIDのプログラムは全て上書き
			for (int i = 0; i < (int)this_.programList.size(); ++i) {
				if (this_.programList[i].videoPid = item->videoPid) {
					this_.programList[i].videoFormat = fmt;
					this_.programList[i].programOK = true;
				}
			}
		}
	};

	std::vector<std::unique_ptr<PSIDelegator>> psiHandlers;
	PidHandlerTable handlerTable;

	bool patOK;
	bool serviceOK;
	bool timeOK;

	int numPrograms;
	std::vector<ProgramItem> programList;
	std::vector<ServiceInfo> serviceList;
	JSTTime time;

	PSIDelegator* newPsiHandler() {
		auto p = new PSIDelegator(ctx, *this);
		psiHandlers.emplace_back(p);
		return p;
	}

	void onPsiUpdated(PsiSection section)
	{
		switch (section.table_id()) {
		case 0x00: // PAT
			onPAT(section);
			break;
		case 0x02: // PMT
			onPMT(section);
			break;
		case 0x42: // SDT（自ストリーム）
			onSDT(section);
			break;
		case 0x70: // TDT
			onTDT(section);
			break;
		case 0x73: // TOT
			onTOT(section);
			break;
		}
	}

	void onPAT(PsiSection section)
	{
		PAT pat = section;
		if (pat.parse() && pat.check()) {
			std::vector<PATElement> programs;
			for (int i = 0; i < pat.numElems(); ++i) {
				auto elem = pat.get(i);
				int pn = elem.program_number();
				if (pn != 0) {
					programs.push_back(elem);
				}
			}
			numPrograms = (int)programs.size();
			// 少ないときだけ増やす（逆に減らすとhandlerTableにダングリングポインタが残るので注意）
			if ((int)programList.size() < numPrograms) {
				programList.resize(programs.size());
			}
			for (int i = 0; i < (int)programs.size(); ++i) {
				if (programList[i].pmtHandler == nullptr) {
					programList[i].pmtHandler =
						std::unique_ptr<PSIDelegator>(new PSIDelegator(ctx, *this));
				}
				programList[i].programId = programs[i].program_number();
				handlerTable.add(programs[i].PID(), programList[i].pmtHandler.get());
			}
			patOK = true;
		}
	}

	void onPMT(PsiSection section)
	{
		PMT pmt = section;
		if (pmt.parse() && pmt.check()) {
			// 該当プログラムを探す
			ProgramItem* item = nullptr;
			int programId = pmt.program_number();
			for (int i = 0; i < (int)numPrograms; ++i) {
				if (programList[i].programId == programId) {
					item = &programList[i];
					break;
				}
			}
			if (item != nullptr) {
				// 映像をみつける
				item->hasVideo = false;
				for (int i = 0; i < pmt.numElems(); ++i) {
					PMTElement elem = pmt.get(i);
					uint8_t stream_type = elem.stream_type();
					VIDEO_STREAM_FORMAT type = VS_UNKNOWN;
					switch (stream_type) {
					case 0x02:
						type = VS_MPEG2;
						break;
					case 0x1B:
						type = VS_H264;
						break;
					}
					if (type != VS_UNKNOWN) {
						item->hasVideo = true;
						item->videoPid = elem.elementary_PID();
						if (programList[i].videoHandler == nullptr) {
							programList[i].videoHandler =
								std::unique_ptr<SpVideoFrameParser>(new SpVideoFrameParser(ctx, *this, item));
						}
						programList[i].videoHandler->setStreamFormat(type);
						handlerTable.add(elem.elementary_PID(), programList[i].videoHandler.get());
						break;
					}
				}
			}
		}
	}

	void onSDT(PsiSection section)
	{
		SDT sdt(section);
		if (sdt.parse() && sdt.check()) {
			serviceList.clear();
			for (int i = 0; i < sdt.numElems(); ++i) {
				ServiceInfo info;
				auto elem = sdt.get(i);
				info.serviceId = elem.service_id();
				auto descs = ParseDescriptors(elem.descriptor());
				for (int i = 0; i < sdt.numElems(); ++i) {
					if (descs[i].tag() == 0x48) { // サービス記述子
						ServiceDescriptor servicedesc(descs[i]);
						if (servicedesc.parse()) {
							info.provider = GetAribString(servicedesc.service_provider_name());
							info.name = GetAribString(servicedesc.service_name());
							break;
						}
					}
				}
				serviceList.push_back(info);
			}
			serviceOK = true;
		}
	}

	void onTDT(PsiSection section)
	{
		TDT tdt(section);
		if (tdt.parse() && tdt.check()) {
			time = tdt.JST_time();
			timeOK = true;
		}
	}

	void onTOT(PsiSection section)
	{
		TOT tot(section);
		if (tot.parse() && tot.check()) {
			time = tot.JST_time();
			timeOK = true;
		}
	}
};

class TsInfo : public AMTObject {
public:
	TsInfo(AMTContext& ctx)
		: AMTObject(ctx)
		, parser(ctx)
	{ }

	void ReadFile(const char* filepath) {
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
				if (parser.isOK(false)) return;
				totalRead += readBytes;
			} while (readBytes == buffer.length && totalRead < MAX_BYTES);
			if (parser.isOK(true)) return;
			THROW(FormatException, "TSファイルに情報がありません");
	}

	bool ReadFileFromC(const char* filepath) {
		try {
			ReadFile(filepath);
			return true;
		}
		catch (const Exception& exception) {
			ctx.setError(exception);
		}
		return false;
	}

	bool HasServiceInfo() {
		return parser.isOK(false);
	}

	// ref intで受け取る
	void GetDay(int* y, int* m, int* d) {
		parser.getTime().getDay(*y, *m, *d);
	}

	void GetTime(int* h, int* m, int* s) {
		parser.getTime().getTime(*h, *m, *s);
	}

	int GetNumProgram() {
		return parser.getNumPrograms();
	}

	void GetProgramInfo(int i, int* progId, int* hasVideo, int* videoPid) {
		auto& prog = parser.getProgram(i);
		*progId = prog.programId;
		*hasVideo = prog.hasVideo;
		*videoPid = prog.videoPid;
	}

	void GetVideoFormat(int i, int* stream, int* width, int* height, int* sarW, int* sarH) {
		auto& fmt = parser.getProgram(i).videoFormat;
		*stream = fmt.format;
		*width = fmt.width;
		*height = fmt.height;
		*sarW = fmt.sarWidth;
		*sarH = fmt.sarHeight;
	}

	int GetNumService() {
		return (int)parser.getServiceList().size();
	}

	int GetServiceId(int i) {
		return parser.getServiceList()[i].serviceId;
	}

	// IntPtrで受け取ってMarshal.PtrToStringUniで変換
	const wchar_t* GetProviderName(int i) {
		return parser.getServiceList()[i].provider.c_str();
	}

	const wchar_t* GetServiceName(int i) {
		return parser.getServiceList()[i].name.c_str();
	}

	std::vector<int> getSetPids() const {
		return parser.getSetPids();
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
extern "C" __declspec(dllexport) bool TsInfo_ReadFile(TsInfo* ptr, const char* filepath) { return ptr->ReadFileFromC(filepath); }
extern "C" __declspec(dllexport) bool TsInfo_HasServiceInfo(TsInfo* ptr) { return ptr->HasServiceInfo(); }
extern "C" __declspec(dllexport) void TsInfo_GetDay(TsInfo* ptr, int* y, int* m, int* d) { ptr->GetDay(y, m, d); }
extern "C" __declspec(dllexport) void TsInfo_GetTime(TsInfo* ptr, int* h, int* m, int* s) { return ptr->GetTime(h, m, s); }
extern "C" __declspec(dllexport) int TsInfo_GetNumProgram(TsInfo* ptr) { return ptr->GetNumProgram(); }
extern "C" __declspec(dllexport) void TsInfo_GetProgramInfo(TsInfo* ptr, int i, int* progId, int* hasVideo, int* videoPid)
{ return ptr->GetProgramInfo(i, progId, hasVideo, videoPid); }
extern "C" __declspec(dllexport) void TsInfo_GetVideoFormat(TsInfo* ptr, int i, int* stream, int* width, int* height, int* sarW, int* sarH)
{ return ptr->GetVideoFormat(i, stream, width, height, sarW, sarH); }
extern "C" __declspec(dllexport) int TsInfo_GetNumService(TsInfo* ptr) { return ptr->GetNumService(); }
extern "C" __declspec(dllexport) int TsInfo_GetServiceId(TsInfo* ptr, int i) { return ptr->GetServiceId(i); }
extern "C" __declspec(dllexport) const wchar_t* TsInfo_GetProviderName(TsInfo* ptr, int i) { return ptr->GetProviderName(i); }
extern "C" __declspec(dllexport) const wchar_t* TsInfo_GetServiceName(TsInfo* ptr, int i) { return ptr->GetServiceName(i); }

typedef bool(*TS_SLIM_CALLBACK)();

class TsSlimFilter : AMTObject
{
public:
	TsSlimFilter(AMTContext& ctx, int videoPid)
		: AMTObject(ctx)
		, videoPid(videoPid)
	{ }

	bool exec(const char* srcpath, const char* dstpath, TS_SLIM_CALLBACK cb)
	{
		try {
			File srcfile(srcpath, "rb");
			File dstfile(dstpath, "wb");
			pfile = &dstfile;
			videoOk = false;
			enum {
				BUFSIZE = 4 * 1024 * 1024
			};
			auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
			MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
			size_t readBytes;
			SpTsPacketParser packetParser(*this);
			do {
				readBytes = srcfile.read(buffer);
				packetParser.inputTS(MemoryChunk(buffer.data, readBytes));
				if (cb() == false) return false;
			} while (readBytes == buffer.length);
			packetParser.flush();
			return true;
		}
		catch (const Exception& exception) {
			ctx.setError(exception);
		}
		return false;
	}

private:
	class SpTsPacketParser : public TsPacketParser {
		TsSlimFilter& this_;
	public:
		SpTsPacketParser(TsSlimFilter& this_)
			: TsPacketParser(this_.ctx)
			, this_(this_) { }

		virtual void onTsPacket(TsPacket packet) {
			if (this_.videoOk == false && packet.PID() == this_.videoPid) {
				this_.videoOk = true;
			}
			if (this_.videoOk) {
				this_.pfile->write(packet);
			}
		}
	};

	File* pfile;
	int videoPid;
	bool videoOk;
};

extern "C" __declspec(dllexport) void* TsSlimFilter_Create(AMTContext* ctx, int videoPid) { return new TsSlimFilter(*ctx, videoPid); }
extern "C" __declspec(dllexport) void TsSlimFilter_Delete(TsSlimFilter* ptr) { delete ptr; }
extern "C" __declspec(dllexport) bool TsSlimFilter_Exec(TsSlimFilter* ptr, const char* srcpath, const char* dstpath, TS_SLIM_CALLBACK cb) { return ptr->exec(srcpath, dstpath, cb); }
