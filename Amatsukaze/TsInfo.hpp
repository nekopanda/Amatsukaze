/**
* Amtasukaze TS Info
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "Mpeg2TsParser.hpp"
#include "AribString.hpp"

#include <stdint.h>

#include <memory>

struct ProgramInfo {
	int programId;
	bool hasVideo;
	int videoPid; // 同じ映像の別サービスに注意
	VideoFormat videoFormat;
};

struct ServiceInfo {
	int serviceId;
	std::wstring provider;
	std::wstring name;
};

struct ContentNibbles {
	uint8_t content_nibble_level_1;
	uint8_t content_nibble_level_2;
	uint8_t user_nibble_1;
	uint8_t user_nibble_2;
};

struct ContentInfo {
	int serviceId;
  std::wstring eventName;
  std::wstring text;
	std::vector<ContentNibbles> nibbles;
};

class TsInfoParser : public AMTObject {
public:
	TsInfoParser(AMTContext& ctx)
		: AMTObject(ctx)
		, patOK(false)
		, serviceOK(false)
		, timeOK(false)
		, numPrograms(0)
	{
		handlerTable.addConstant(0x0000, newPsiHandler()); // PAT
		handlerTable.addConstant(0x0011, newPsiHandler()); // SDT/BAT
		handlerTable.addConstant(0x0012, newPsiHandler()); // H-EIT
		handlerTable.addConstant(0x0026, newPsiHandler()); // M-EIT
		handlerTable.addConstant(0x0027, newPsiHandler()); // L-EIT
		handlerTable.addConstant(0x0014, newPsiHandler()); // TDT/TOT
	}

	void inputTsPacket(int64_t clock, TsPacket packet) {
		TsPacketHandler* handler = handlerTable.get(packet.PID());
		if (handler != NULL) {
			handler->onTsPacket(clock, packet);
		}
	}

	bool isProgramOK() const {
    if (!patOK) return false;
		for (int i = 0; i < numPrograms; ++i) {
      // 全プログラムのパケットがあるとは限らないので
      // 1つでも映像のあるプログラムが取得できればOKとする
			if (programList[i].programOK && programList[i].hasVideo) return true;
		}
		return false;
	}

  bool isScrampbled() const {
    bool hasScrambleNG = false;
    bool hasOKVideo = false;
    for (int i = 0; i < numPrograms; ++i) {
      if (!programList[i].programOK && programList[i].hasVideo && programList[i].hasScramble) {
        // スクランブルで映像情報が取得できなかった
        hasScrambleNG = true;
      }
      if (programList[i].programOK && programList[i].hasVideo) {
        // 映像情報が取得できたプログラム
        hasOKVideo = true;
      }
    }
    // 映像情報が1つも取得できなくて、スクランブル映像があった場合
    return !hasOKVideo && hasScrambleNG;
  }

	bool isOK() const {
		for (int i = 0; i < numPrograms; ++i) {
			if (programList[i].programOK == false) return false;
		}
		for (int i = 0; i < numPrograms; ++i) {
			// 映像ありサービスでイベント情報がないのはダメ
			if (programList[i].hasVideo && programList[i].eventOK == false) return false;
		}
		return patOK && serviceOK && timeOK;
	}

	bool hasServiceInfo() {
		return serviceOK && timeOK;
	}

	int getNumPrograms() const {
		return (int)programList.size();
	}

	const ProgramInfo& getProgram(int i) const {
		return programList[i];
	}

	const ContentInfo& getContent(int i) const {
		return programList[i].contentInfo;
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
		virtual void onTableUpdated(int64_t clock, PsiSection section) {
			this_.onPsiUpdated(section);
		}
	};
	struct ProgramItem : ProgramInfo {
		bool programOK;
    bool hasScramble;
		std::unique_ptr<PSIDelegator> pmtHandler;
		std::unique_ptr<VideoFrameParser> videoHandler;

		bool eventOK;
		ContentInfo contentInfo;

		ProgramItem()
			: programOK(false)
      , hasScramble(false)
			, eventOK(false)
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
    virtual void onTsPacket(int64_t clock, TsPacket packet) {
      if (packet.transport_scrambling_control()) {
        // スクランブルパケット
        if (item->hasScramble == false) {
          for (int i = 0; i < (int)this_.programList.size(); ++i) {
            if (this_.programList[i].videoPid == item->videoPid) {
              this_.programList[i].hasScramble = true;
            }
          }
        }
        return;
      }
      PesParser::onTsPacket(clock, packet);
    }
	protected:
		virtual void onVideoPesPacket(int64_t clock, const std::vector<VideoFrameInfo>& frames, PESPacket packet) { }
		virtual void onVideoFormatChanged(VideoFormat fmt) {
			// 同じPIDのプログラムは全て上書き
			for (int i = 0; i < (int)this_.programList.size(); ++i) {
				if (this_.programList[i].videoPid == item->videoPid) {
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

	// イベント情報から
	JSTTime startTime;

	PSIDelegator* newPsiHandler() {
		auto p = new PSIDelegator(ctx, *this);
		psiHandlers.emplace_back(p);
		return p;
	}

	ProgramItem* getProgramItem(int programId) {
		for (int i = 0; i < (int)numPrograms; ++i) {
			if (programList[i].programId == programId) {
				return &programList[i];
			}
		}
		return nullptr;
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
		case 0x4E: // EIT（自ストリームの現在と次）
			onEIT(section);
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
#if 1
      if (section.current_next_indicator() == 0) {
        printf("IS NEXT PAT\n");
      }
#endif
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
		if (section.current_next_indicator() && pmt.parse() && pmt.check()) {
			// 該当プログラムを探す
			ProgramItem* item = getProgramItem(pmt.program_number());
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
						int videoPid = elem.elementary_PID();

						// 同じvideoPidのプログラムを探す
						ProgramItem* top = nullptr;
						for (int i = 0; i < (int)numPrograms; ++i) {
							if (programList[i].videoPid == videoPid &&
								programList[i].videoHandler != nullptr) {
								top = &programList[i];
								break;
							}
						}

						item->hasVideo = true;
						item->videoPid = videoPid;

						if (top == nullptr) {
							// 自分が最初なので取得する
							if (item->videoHandler == nullptr) {
								item->videoHandler =
									std::unique_ptr<SpVideoFrameParser>(new SpVideoFrameParser(ctx, *this, item));
							}
							item->videoHandler->setStreamFormat(type);
							handlerTable.add(item->videoPid, item->videoHandler.get());
						}
						else {
							// 自分は先頭ではないので、先頭のフォーマットをもらう
							if (top->programOK) {
								item->videoFormat = top->videoFormat;
								item->programOK = true;
							}
						}
						break;
					}
				}
				if (item->hasVideo == false) {
					// このプログラムには対応する映像がない
					item->programOK = true;
				}
			}
		}
	}

	void onSDT(PsiSection section)
	{
		SDT sdt(section);
		if (section.current_next_indicator() && sdt.parse() && sdt.check()) {
			serviceList.clear();
			for (int i = 0; i < sdt.numElems(); ++i) {
				ServiceInfo info;
				auto elem = sdt.get(i);
				info.serviceId = elem.service_id();
				auto descs = ParseDescriptors(elem.descriptor());
				for (int i = 0; i < (int)descs.size(); ++i) {
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

	void onEIT(PsiSection section)
	{
		EIT eit(section);
		if (section.current_next_indicator() && eit.parse() && eit.check() &&
      section.section_number() == 0) // 現在の番組のみ
    {
			ProgramItem* item = getProgramItem(eit.service_id());
			if (item != nullptr) {
				if (eit.numElems() > 0) {
					auto elem = eit.get(0);
					startTime = elem.start_time();
					auto descs = ParseDescriptors(elem.descriptor());
          ContentInfo info;
					for (int i = 0; i < (int)descs.size(); ++i) {
            if (descs[i].tag() == 0x4D) { // 短形式イベント記述子
              ShortEventDescriptor seventdesc(descs[i]);
              if (seventdesc.parse()) {
                info.eventName = GetAribString(seventdesc.event_name());
                info.text = GetAribString(seventdesc.text());
              }
            }
						else if (descs[i].tag() == 0x54) { // コンテント記述子
							ContentDescriptor contentdesc(descs[i]);
							if (contentdesc.parse()) {
								int num = contentdesc.numElems();
								for (int i = 0; i < num; ++i) {
									auto data_i = contentdesc.get(i);
									ContentNibbles data;
									data.content_nibble_level_1 = data_i.content_nibble_level_1();
									data.content_nibble_level_2 = data_i.content_nibble_level_2();
									data.user_nibble_1 = data_i.user_nibble_1();
									data.user_nibble_2 = data_i.user_nibble_2();
                  info.nibbles.push_back(data);
								}
							}
						}
					}
					// 同じPIDのプログラムは全て上書き
					for (int i = 0; i < (int)programList.size(); ++i) {
						if (programList[i].videoPid == item->videoPid) {
              programList[i].contentInfo = info;
              programList[i].contentInfo.serviceId = programList[i].programId;
							programList[i].eventOK = true;
						}
					}
				}
			}
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
			File srcfile(filepath, "rb");
			// ファイルの真ん中を読む
			srcfile.seek(srcfile.size() / 2, SEEK_SET);
      int ret = ReadTS(srcfile);
			if (ret == 0) {
				return;
			}
      bool isScrampbled = (ret == 2);
			// ダメだったらファイルの先頭付近を読む
			srcfile.seek(srcfile.size() / 30, SEEK_SET);
      ret = ReadTS(srcfile);
      if (ret == 0) {
        return;
      }
      if (isScrampbled) {
        THROW(FormatException, "すべてのプログラムがスクランブルされています");
      }
      else {
        THROW(FormatException, "TSファイルに情報がありません");
      }
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
		return parser.hasServiceInfo();
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

	void GetProgramInfo(int i, int* progId, int* hasVideo, int* videoPid, int* numContent) {
		auto& prog = parser.getProgram(i);
		*progId = prog.programId;
		*hasVideo = prog.hasVideo;
		*videoPid = prog.videoPid;
		*numContent = (int)parser.getContent(i).nibbles.size();
	}

	void GetVideoFormat(int i, int* stream, int* width, int* height, int* sarW, int* sarH) {
		auto& fmt = parser.getProgram(i).videoFormat;
		*stream = fmt.format;
		*width = fmt.width;
		*height = fmt.height;
		*sarW = fmt.sarWidth;
		*sarH = fmt.sarHeight;
	}

	void GetContentNibbles(int i, int ci, int *level1, int *level2, int* user1, int* user2) {
		auto& data = parser.getContent(i).nibbles[ci];
		*level1 = data.content_nibble_level_1;
		*level2 = data.content_nibble_level_2;
		*user1 = data.user_nibble_1;
		*user2 = data.user_nibble_2;
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

  const wchar_t* GetEventName(int i) {
    return parser.getContent(i).eventName.c_str();
  }

  const wchar_t* GetEventText(int i) {
    return parser.getContent(i).text.c_str();
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

	int ReadTS(File& srcfile) {
		enum {
			BUFSIZE = 4 * 1024 * 1024,
			MAX_BYTES = 100 * 1024 * 1024
		};
		auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
		MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
		size_t totalRead = 0;
		size_t readBytes;
		SpTsPacketParser packetParser(*this);
		do {
			readBytes = srcfile.read(buffer);
			packetParser.inputTS(MemoryChunk(buffer.data, readBytes));
			if (parser.isOK()) return 0;
			totalRead += readBytes;
		} while (readBytes == buffer.length && totalRead < MAX_BYTES);
    if (parser.isProgramOK()) return 0;
    if (parser.isScrampbled()) return 2;
		return 1;
	}
};

// C API for P/Invoke
extern "C" __declspec(dllexport) void* TsInfo_Create(AMTContext* ctx) { return new TsInfo(*ctx); }
extern "C" __declspec(dllexport) void TsInfo_Delete(TsInfo* ptr) { delete ptr; }
extern "C" __declspec(dllexport) int TsInfo_ReadFile(TsInfo* ptr, const char* filepath) { return ptr->ReadFileFromC(filepath); }
extern "C" __declspec(dllexport) int TsInfo_HasServiceInfo(TsInfo* ptr) { return ptr->HasServiceInfo(); }
extern "C" __declspec(dllexport) void TsInfo_GetDay(TsInfo* ptr, int* y, int* m, int* d) { ptr->GetDay(y, m, d); }
extern "C" __declspec(dllexport) void TsInfo_GetTime(TsInfo* ptr, int* h, int* m, int* s) { return ptr->GetTime(h, m, s); }
extern "C" __declspec(dllexport) int TsInfo_GetNumProgram(TsInfo* ptr) { return ptr->GetNumProgram(); }
extern "C" __declspec(dllexport) void TsInfo_GetProgramInfo(TsInfo* ptr, int i, int* progId, int* hasVideo, int* videoPid, int* numContent)
{ return ptr->GetProgramInfo(i, progId, hasVideo, videoPid, numContent); }
extern "C" __declspec(dllexport) void TsInfo_GetVideoFormat(TsInfo* ptr, int i, int* stream, int* width, int* height, int* sarW, int* sarH)
{ return ptr->GetVideoFormat(i, stream, width, height, sarW, sarH); }
extern "C" __declspec(dllexport) void TsInfo_GetContentNibbles(TsInfo* ptr, int i, int ci, int *level1, int *level2, int* user1, int* user2)
{ return ptr->GetContentNibbles(i, ci, level1, level2, user1, user2); }
extern "C" __declspec(dllexport) int TsInfo_GetNumService(TsInfo* ptr) { return ptr->GetNumService(); }
extern "C" __declspec(dllexport) int TsInfo_GetServiceId(TsInfo* ptr, int i) { return ptr->GetServiceId(i); }
extern "C" __declspec(dllexport) const wchar_t* TsInfo_GetProviderName(TsInfo* ptr, int i) { return ptr->GetProviderName(i); }
extern "C" __declspec(dllexport) const wchar_t* TsInfo_GetServiceName(TsInfo* ptr, int i) { return ptr->GetServiceName(i); }
extern "C" __declspec(dllexport) const wchar_t* TsInfo_GetEventName(TsInfo* ptr, int i) { return ptr->GetEventName(i); }
extern "C" __declspec(dllexport) const wchar_t* TsInfo_GetEventText(TsInfo* ptr, int i) { return ptr->GetEventText(i); }

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
