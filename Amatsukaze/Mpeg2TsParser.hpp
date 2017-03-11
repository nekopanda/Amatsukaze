/**
* MPEG2-TS parser
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "StreamUtils.hpp"

/** @brief TSパケットのアダプテーションフィールド */
struct AdapdationField : public MemoryChunk {
	AdapdationField(uint8_t* data, int length) : MemoryChunk(data, length) { }

	uint8_t adapdation_field_length() const { return data[0]; }
	// ARIB TR-14 8.2.3に送出規定がある
	// TSパケットの連続性、PCR連続性が切れる場合に送られる
	uint8_t discontinuity_indicator() const { return bsm(data[1], 7, 1); }
	uint8_t randam_access_indicator() const { return bsm(data[1], 6, 1); }
	uint8_t elementary_stream_priority_indicator() const { return bsm(data[1], 5, 1); }
	uint8_t PCR_flag() const { return bsm(data[1], 4, 1); }
	uint8_t OPCR_flag() const { return bsm(data[1], 3, 1); }
	uint8_t splicing_point_flag() const { return bsm(data[1], 2, 1); }
	uint8_t transport_private_data_flag() const { return bsm(data[1], 1, 1); }
	uint8_t adaptation_field_extension_flag() const { return bsm(data[1], 0, 1); }

	int64_t program_clock_reference;
	int64_t original_program_clock_reference;

	bool parse() {
		int consumed = 2;
		if (PCR_flag()) {
			if (consumed + 6 > length) return false;
			program_clock_reference = read_pcr(&data[consumed]);
			consumed += 6;
		}
		if (OPCR_flag()) {
			if (consumed + 6 > length) return false;
			original_program_clock_reference = read_pcr(&data[consumed]);
			consumed += 6;
		}
		return true;
	}

	bool check() {
		// do nothing
		return true;
	}

private:
	int64_t read_pcr(uint8_t* ptr) {
		int64_t raw = read48(ptr);
		return bsm(raw, 15, 33) * 300 + bsm(raw, 0, 9);
	}
};

/** @brief TSパケット */
struct TsPacket : public MemoryChunk {

	TsPacket(uint8_t* data)
		: MemoryChunk(data, TS_PACKET_LENGTH), payload_offset(0) { }

	uint8_t sync_byte() const { return data[0]; }
	uint8_t transport_error_indicator() const { return bsm(data[1], 7, 1); }
	uint8_t payload_unit_start_indicator() const { return bsm(data[1], 6, 1); }
	uint8_t transport_priority() const { return bsm(data[1], 6, 1); }
	uint16_t PID() const { return bsm(read16(&data[1]), 0, 13); }
	uint8_t transport_scrambling_control() const { return bsm(data[3], 6, 2); }
	uint8_t adaptation_field_control() const { return bsm(data[3], 4, 2); }
	uint8_t continuity_counter() const { return bsm(data[3], 0, 4); }

	bool has_adaptation_field() { return (adaptation_field_control() & 0x02) != 0; }
	bool has_payload() { return (adaptation_field_control() & 0x01) != 0; }

	bool parse() {
		if (adaptation_field_control() & 0x01) {
			if (adaptation_field_control() & 0x2) {
				// exists adapdation field
				int adaptation_field_length = data[4];
				// TSパケットヘッダ+アダプテーションフィールド長はadaptation_field_lengthに含まれていない
				payload_offset = 4 + 1 + adaptation_field_length;
			}
			else {
				payload_offset = 4;
			}
		}
		return true;
	}

	bool check() {
		if (sync_byte() != TS_SYNC_BYTE) return false; // 同期コードが合っていない
		if (PID() >= 0x0002U && PID() <= 0x000FU) return false; // 未定義PID
		if (transport_scrambling_control() == 0x01) return false; // 未定義スクランブル制御
		if (adaptation_field_control() == 0x00) return false; // 未定義アダプテーションフィールド制御
		if (has_payload() && payload_offset >= TS_PACKET_LENGTH) {
			// アダプテーションフィールドが長すぎる
			return false;
		}
		return true;
	}

	MemoryChunk adapdation_field() {
		if (has_payload()) {
			return MemoryChunk(&data[4], payload_offset - 4);
		}
		return MemoryChunk(&data[4], TS_PACKET_LENGTH - 4);
	}

	MemoryChunk payload() {
		// ペイロードの長さ調整はアダプテーションフィールドでなされるよ～
		return MemoryChunk(
			&data[payload_offset], TS_PACKET_LENGTH - payload_offset);
	}

private:
	int payload_offset;
};

/** @brief PESパケットの先頭9バイト */
struct PESConstantHeader : public MemoryChunk {

	PESConstantHeader(uint8_t* data, int length) : MemoryChunk(data, length) { }

	int32_t packet_start_code_prefix() const { return read24(data); }
	uint8_t stream_id() const { return data[3]; }
	uint16_t PES_packet_length() const { return read16(&data[4]); }

	uint8_t PES_scrambling_control() const { return bsm(data[6], 4, 2); }
	uint8_t PES_priority() const { return bsm(data[6], 3, 1); }
	uint8_t data_alignment_indicator() const { return bsm(data[6], 2, 1); }
	uint8_t copyright() const { return bsm(data[6], 1, 1); }
	uint8_t original_or_copy() const { return bsm(data[6], 0, 1); }
	uint8_t PTS_DTS_flags() const { return bsm(data[7], 6, 2); }
	uint8_t ESCR_flags() const { return bsm(data[7], 5, 1); }
	uint8_t ES_rate_flags() const { return bsm(data[7], 4, 1); }
	uint8_t DSM_trick_mode_flags() const { return bsm(data[7], 3, 1); }
	uint8_t additional_copy_info_flags() const { return bsm(data[7], 2, 1); }
	uint8_t PES_CRC_flags() const { return bsm(data[7], 1, 1); }
	uint8_t PES_extension_flags() const { return bsm(data[7], 0, 1); }
	uint8_t PES_header_data_length() const { return data[8]; }

	/** @brief 十分な長さがない場合 false を返す */
	bool parse() {
		if (length < 9) return false;
		return true;
	}

	bool check() {
		if (packet_start_code_prefix() != 0x000001) return false; // スタートコード
		if ((data[6] & 0xC0) != 0x80U) return false; // 固定ビット
		if (PTS_DTS_flags() == 0x01)return false; // forbidden
		return true;
	}
};

struct PESPacket : public PESConstantHeader {

	PESPacket(uint8_t* data, int length) : PESConstantHeader(data, length) { }

	bool has_PTS() { return (PTS_DTS_flags() & 0x02) != 0; }
	bool has_DTS() { return (PTS_DTS_flags() & 0x01) != 0; }

	bool parse() {
		if (!PESConstantHeader::parse()) {
			return false;
		}
		// ヘッダ長チェック
		int headerLength = PES_header_data_length();
		int calculatedLength = 0;
		if (has_PTS()) calculatedLength += 5;
		if (has_DTS()) calculatedLength += 5;
		if (ESCR_flags()) calculatedLength += 6;
		if (ES_rate_flags()) calculatedLength += 3;
		if (DSM_trick_mode_flags()) calculatedLength += 1;
		if (additional_copy_info_flags()) calculatedLength += 1;
		if (PES_CRC_flags()) calculatedLength += 2;
		if (PES_extension_flags()) calculatedLength += 1;

		if (headerLength < calculatedLength) {
			// ヘッダ長が足りない
			return false;
		}

		int consumed = 9;
		if (has_PTS()) {
			PTS = readTimeStamp(&data[consumed]);
			consumed += 5;
		}
		if (has_DTS()) {
			DTS = readTimeStamp(&data[consumed]);
			consumed += 5;
		}

		payload_offset = PES_header_data_length() + 9;

		return true;
	}

	bool check() {
		if (!PESConstantHeader::check()) {
			return false;
		}
		// 長さチェック
		int packetLength = PES_packet_length();
		if (payload_offset >= length) return false;
		if (packetLength != 0 && packetLength + 6 != length) return false;
		return true;
	}

	MemoryChunk paylod() {
		return MemoryChunk(data + payload_offset, length - payload_offset);
	}

	// PTS, DTSがあるときだけ書き換える
	void changeTimestamp(int64_t PTS, int64_t DTS) {
		int pos = 9;
		if (has_PTS()) {
			writeTimeStamp(&data[pos], PTS);
			pos += 5;
		}
		if (has_DTS()) {
			writeTimeStamp(&data[pos], DTS);
			pos += 5;
		}

		this->PTS = PTS;
		this->DTS = DTS;
	}

	void changeStreamId(uint8_t stream_id_) {
		data[3] = stream_id_;
		ASSERT(stream_id() == stream_id_);
	}

	void writePacketLength() {
		write16(&data[4], uint16_t(length - 6));
		ASSERT(PES_packet_length() == length - 6);
	}

	int64_t PTS, DTS;

private:
	int64_t readTimeStamp(uint8_t* ptr) {
		int64_t raw = read40(ptr);
		return (bsm(raw, 33, 3) << 30) |
			(bsm(raw, 17, 15) << 15) |
			bsm(raw, 1, 15);
	}

	void writeTimeStamp(uint8_t* ptr, int64_t TS) {
		int64_t raw = 0;
		bms(raw, 3, 36, 4); // '0011'
		bms(raw, TS >> 30, 33,	3);
		bms(raw, 1, 32, 1); // marker_bit
		bms(raw, TS >> 15, 17, 15);
		bms(raw, 1, 16, 1); // marker_bit
		bms(raw, TS >>	0,	1, 15);
		bms(raw, 1, 0, 1); // marker_bit
		write40(ptr, raw);
	}

	int payload_offset;
};

/** @brief TSパケットを切り出す
* inputTS()を必要回数呼び出して最後にflush()を必ず呼び出すこと。
* flush()を呼び出さないと内部のバッファに残ったデータが処理されない。
*/
class TsPacketParser : public AMTObject {
	enum {
		// 同期コードを探すときにチェックするパケット数
		CHECK_PACKET_NUM = 8,
	};
public:
	TsPacketParser(AMTContext& ctx)
		: AMTObject(ctx)
		, syncOK(false)
	{ }

	/** @brief TSデータを入力 */
	void inputTS(MemoryChunk data) {

		buffer.add(data.data, data.length);

		if (syncOK) {
			outPackets();
		}
		while (buffer.size() >= CHECK_PACKET_NUM*TS_PACKET_LENGTH) {
			// チェックするのに十分な量がある
			if (checkSyncByte(buffer.get(), CHECK_PACKET_NUM)) {
				syncOK = true;
				outPackets();
			}
			else {
				// ダメだったので1バイトスキップ
				syncOK = false;
				buffer.trimHead(1);
			}
		}
	}

	/** @brief 内部バッファをフラッシュ */
	void flush() {
		while (buffer.size() >= TS_PACKET_LENGTH) {
			// 先頭パケットの同期コードが合っていれば出力する
			if (checkSyncByte(buffer.get(), 1))
			{
				checkAndOutPacket(MemoryChunk(buffer.get(), TS_PACKET_LENGTH));
				buffer.trimHead(TS_PACKET_LENGTH);
			}
			else {
				buffer.trimHead(1);
			}
		}
	}

	/** @brief 残っているデータを全てクリア */
	void reset() {
		buffer.clear();
		syncOK = false;
	}

protected:
	/** @brief 切りだされたTSパケットを処理 */
	virtual void onTsPacket(TsPacket packet) = 0;

private:
	AutoBuffer buffer;
	bool syncOK;

	// numPacket個分のパケットの同期バイトが合っているかチェック
	bool checkSyncByte(uint8_t* ptr, int numPacket) {
		for (int i = 0; i < numPacket; ++i) {
			if (ptr[TS_PACKET_LENGTH*i] != TS_SYNC_BYTE) {
				return false;
			}
		}
		return true;
	}

	// 「先頭と次のパケットの同期バイトを見て合っていれば出力」を繰り返す
	void outPackets() {
		while (buffer.size() >= 2 * TS_PACKET_LENGTH &&
			checkSyncByte(buffer.get(), 2))
		{
			checkAndOutPacket(MemoryChunk(buffer.get(), TS_PACKET_LENGTH));
			// onTsPacketでresetが呼ばれるかもしれないので注意
			buffer.trimHead(TS_PACKET_LENGTH);
		}
	}

	// パケットをチェックして出力
	void checkAndOutPacket(MemoryChunk data) {
		TsPacket packet(data.data);
		if (packet.parse() && packet.check()) {
			onTsPacket(packet);
		}
	}
};

class TsPacketHandler {
public:
	virtual void onTsPacket(int64_t clock, TsPacket packet) = 0;
};

class PesParser : public TsPacketHandler {
public:
	PesParser() : packetClock(-1) { }

	/** @brief TSパケット(チェック済み)を入力 */
	virtual void onTsPacket(int64_t clock, TsPacket packet) {

		if (packet.has_payload()) {
			// データあり

			if (packet.payload_unit_start_indicator()) {
				// パケットスタート
				// ペイロード最初から始まるんだよね

				if (buffer.size() > 0) {
					// 前のパケットデータがある場合は出力
					checkAndOutPacket(packetClock, buffer);
					buffer.clear();
				}

				packetClock = clock;
			}

			MemoryChunk payload = packet.payload();
			buffer.add(payload.data, payload.length);

			// 完了チェック
			PESConstantHeader header(buffer.get(), (int)buffer.size());
			if (header.parse()) {
				int PES_packet_length = header.PES_packet_length();
				int lengthIncludeHeader = PES_packet_length + 6;
				if (PES_packet_length != 0 && buffer.size() >= lengthIncludeHeader) {
					// パケットのストア完了
					checkAndOutPacket(packetClock, MemoryChunk(buffer.get(), lengthIncludeHeader));
					buffer.trimHead(lengthIncludeHeader);
				}
			}
		}
	}

protected:
	virtual void onPesPacket(int64_t clock, PESPacket packet) = 0;

private:
	AutoBuffer buffer;
	int64_t packetClock;

	// パケットをチェックして出力
	void checkAndOutPacket(int64_t clock, MemoryChunk data) {
		PESPacket packet(data.data, (int)data.length);
		// フォーマットチェック
		if (packet.parse() && packet.check()) {
			// OK
			onPesPacket(packetClock, packet);
		}
	}
};

struct PsiConstantHeader : public MemoryChunk {

	PsiConstantHeader(uint8_t* data, int length) : MemoryChunk(data, length) { }

	uint8_t table_id() const { return data[0]; }
	uint8_t section_syntax_indicator() const { return bsm(data[1], 7, 1); }
	uint16_t section_length() const { return bsm(read16(&data[1]), 0, 12); }

	bool parse() {
		if (length < 3) return false;
		return true;
	}

	bool check() const {
		return true; // 最後にCRCでチェックするので何もしない
	}

};

struct PsiSection : public AMTObject, public PsiConstantHeader {

	PsiSection(AMTContext& ctx, uint8_t* data, int length)
		: AMTObject(ctx), PsiConstantHeader(data, length) { }

	uint16_t id() { return read16(&data[3]); }
	uint8_t version_number() { return bsm(data[5], 1, 5); }
	uint8_t current_next_indicator() { return bsm(data[5], 0, 1); }
	uint8_t section_number() { return data[6]; }
	uint8_t last_section_number() { return data[7]; }

	bool parse() {
		if (!PsiConstantHeader::parse()) return false;
		return true;
	}

	bool check() const {
		if (!PsiConstantHeader::check()) return false;
		if (length != section_length() + 3) return false;
		if (ctx.getCRC()->calc(data, (int)length, 0xFFFFFFFFUL)) return false;
		return true;
	}

	MemoryChunk payload() {
		int payload_offset = section_syntax_indicator() ? 8 : 3;
		// CRC_32の4bytes引く
		return MemoryChunk(data + payload_offset, length - payload_offset - 4);
	}
};

struct PATElement {

	PATElement(uint8_t* ptr) : ptr(ptr) { }

	uint16_t program_number() { return read16(ptr); }
	uint16_t PID() { return bsm(read16(&ptr[2]), 0, 13); }

	bool is_network_PID() { return (program_number() == 0); }

private:
	uint8_t* ptr;
};

struct PAT {

	PAT(PsiSection section) : section(section) { }

	uint16_t TSID() { return section.id(); }

	bool parse() {
		payload_ = section.payload();
		return true;
	}

	bool check() const {
		return (payload_.length % 4) == 0;
	}

	int numElems() const {
		return (int)payload_.length / 4;
	}

	PATElement get(int i) const {
		ASSERT(i < numElems());
		return PATElement(payload_.data + i * 4);
	}

private:
	PsiSection section;
	MemoryChunk payload_;
};

struct PMTElement {

	PMTElement(uint8_t* ptr) : ptr(ptr) { }

	uint8_t stream_type() { return *ptr; }
	uint16_t elementary_PID() { return bsm(read16(&ptr[1]), 0, 13); }
	uint16_t ES_info_length() { return bsm(read16(&ptr[3]), 0, 12); }
	int size() { return ES_info_length() + 5; }

private:
	uint8_t* ptr;
};

struct PMT {

	PMT(PsiSection section) : section(section) { }

	uint16_t program_number() { return section.id(); }
	uint16_t PCR_PID() { return bsm(read16(&payload_.data[0]), 0, 13); }
	uint16_t program_info_length() { return bsm(read16(&payload_.data[2]), 0, 12); }

	bool parse() {
		payload_ = section.payload();
		int offset = program_info_length() + 4;
		while (offset < payload_.length) {
			elems.emplace_back(&payload_.data[offset]);
			offset += elems.back().size();
		}
		return true;
	}

	bool check() const {
		return true;
	}

	int numElems() const {
		return int(elems.size());
	}

	PMTElement get(int i) const {
		return elems[i];
	}

private:
	PsiSection section;
	MemoryChunk payload_;
	std::vector<PMTElement> elems;
};

class PsiParser : public AMTObject, public TsPacketHandler {
public:
	PsiParser(AMTContext& ctx)
		: AMTObject(ctx)
	{}

	/** 状態リセット */
	void clear() {
		buffer.clear();
	}

	virtual void onTsPacket(int64_t clock, TsPacket packet) {

		if (packet.has_payload()) {
			// データあり

			MemoryChunk payload = packet.payload();

			if (packet.payload_unit_start_indicator()) {
				// セクション開始がある
				int startPos = payload.data[0] + 1; // pointer field
													// 長さチェック
				if (startPos >= payload.length) {
					return;
				}
				if (startPos > 1) {
					// 前のセクションの続きがある
					buffer.add(&payload.data[1], startPos - 1);
					checkAndOutSection();
				}
				buffer.clear();

				buffer.add(&payload.data[startPos], payload.length - startPos);
				checkAndOutSection();
			}
			else {
				buffer.add(&payload.data[0], payload.length);
				checkAndOutSection();
			}
		}
	}

protected:
	virtual void onPsiSection(PsiSection section) = 0;

private:
	AutoBuffer buffer;

	// パケットをチェックして出力
	void checkAndOutSection() {
		// 完了チェック
		PsiConstantHeader header(buffer.get(), (int)buffer.size());
		if (header.parse()) {
			int lengthIncludeHeader = header.section_length() + 3;
			if (buffer.size() >= lengthIncludeHeader) {
				// パケットのストア完了
				PsiSection section(ctx, buffer.get(), lengthIncludeHeader);
				// フォーマットチェック
				if (section.parse() && section.check()) {
					// OK
					onPsiSection(section);
				}
				buffer.trimHead(lengthIncludeHeader);
			}
		}
	}
};

class PsiUpdatedDetector : public PsiParser {
public:

	PsiUpdatedDetector(AMTContext&ctx)
		: PsiParser(ctx)
	{ }

protected:
	virtual void onPsiSection(PsiSection section) {
		if (section != (MemoryChunk)curSection) {
			curSection.clear();
			curSection.add(section.data, section.length);
			onTableUpdated(section);
		}
	}

	virtual void onTableUpdated(PsiSection section) = 0;

private:
	AutoBuffer curSection;
};

class PidHandlerTable {
public:
	PidHandlerTable(TsPacketHandler* patHandler)
		: table()
	{
		table[0] = patHandler;
	}

	/** @brief 番号pid位置にhandlerをセット
	* 同じテーブル中で同じハンドラは１つのpidにしか紐付けできない
	* ハンドラが別のpidにひも付けされている場合解除されてから新しくセットされる
	*/
	bool add(int pid, TsPacketHandler* handler) {
		// PATは変更不可
		if (pid == 0 || pid > MAX_PID) {
			return false;
		}
		if (table[pid] == handler) {
			// すでにセットされている
			return true;
		}
		auto it = handlers.find(handler);
		if (it != handlers.end()) {
			table[it->second] = NULL;
		}
		if (table[pid] != NULL) {
			handlers.erase(table[pid]);
		}
		table[pid] = handler;
		handlers[handler] = pid;
		return true;
	}

	TsPacketHandler* get(int pid) {
		return table[pid];
	}

	void clear() {
		for (auto pair : handlers) {
			table[pair.second] = NULL;
		}
		handlers.clear();
	}

private:
	TsPacketHandler *table[MAX_PID + 1];
	std::map<TsPacketHandler*, int> handlers;
};

struct PMTESInfo {
	int stype;
	int pid;

	PMTESInfo() { }
	PMTESInfo(int stype, int pid)
		: stype(stype), pid(pid) { }
};

class TsPacketSelectorHandler {
public:
	// サービスを設定する場合はサービスのpids上でのインデックス
	// なにもしない場合は負の値の返す
	virtual int onPidSelect(int TSID, const std::vector<int>& pids) = 0;

	virtual void onPmtUpdated(int PcrPid) = 0;

	// TsPacketSelectorでPID Tableが変更された時変更後の情報が送られる
	virtual void onPidTableChanged(const PMTESInfo video, const std::vector<PMTESInfo>& audio) = 0;

	virtual void onVideoPacket(int64_t clock, TsPacket packet) = 0;

	virtual void onAudioPacket(int64_t clock, TsPacket packet, int audioIdx) = 0;
};

class TsPacketSelector : public AMTObject {
public:
	TsPacketSelector(AMTContext& ctx)
		: AMTObject(ctx)
		, waitingNewVideo(false)
		, TSID_(-1)
		, SID_(-1)
		, videoEs(-1, -1)
		, PsiParserPAT(ctx, *this)
		, PsiParserPMT(ctx, *this)
		, videoDelegator(*this)
	{
		curHandlerTable = new PidHandlerTable(&PsiParserPAT);
		nextHandlerTable = new PidHandlerTable(&PsiParserPAT);
	}

	~TsPacketSelector() {
		delete curHandlerTable;
		delete nextHandlerTable;
		for (AudioDelegator* ad : audioDelegators) {
			delete ad;
		}
	}

	void setHandler(TsPacketSelectorHandler* handler) {
		selectorHandler = handler;
	}

	// PSIパーサ内部バッファをクリア
	void resetParser() {
		PsiParserPAT.clear();
		PsiParserPMT.clear();
	}

	void inputTsPacket(int64_t clock, TsPacket packet) {

		if (waitingNewVideo && packet.PID() == videoEs.pid) {
			// 待っていた映像パケット来たのでテーブル変更
			waitingNewVideo = false;
			swapHandlerTable();
			selectorHandler->onPidTableChanged(videoEs, audioEs);
		}
		TsPacketHandler* handler = curHandlerTable->get(packet.PID());
		if (handler != NULL) {
			handler->onTsPacket(clock, packet);
		}

	}

private:
	class PATDelegator : public PsiUpdatedDetector {
		TsPacketSelector& this_;
	public:
		PATDelegator(AMTContext&ctx, TsPacketSelector& this_) : PsiUpdatedDetector(ctx), this_(this_) { }
		virtual void onTableUpdated(PsiSection section) {
			this_.onPatUpdated(section);
		}
	};
	class PMTDelegator : public PsiUpdatedDetector {
		TsPacketSelector& this_;
	public:
		PMTDelegator(AMTContext&ctx, TsPacketSelector& this_) : PsiUpdatedDetector(ctx), this_(this_) { }
		virtual void onTableUpdated(PsiSection section) {
			this_.onPmtUpdated(section);
		}
	};
	class VideoDelegator : public TsPacketHandler {
		TsPacketSelector& this_;
	public:
		VideoDelegator(TsPacketSelector& this_) : this_(this_) { }
		virtual void onTsPacket(int64_t clock, TsPacket packet) {
			this_.onVideoPacket(clock, packet);
		}
	};
	class AudioDelegator : public TsPacketHandler {
		TsPacketSelector& this_;
		int audioIdx;
	public:
		AudioDelegator(TsPacketSelector& this_, int audioIdx)
			: this_(this_), audioIdx(audioIdx) { }
		virtual void onTsPacket(int64_t clock, TsPacket packet) {
			this_.onAudioPacket(clock, packet, audioIdx);
		}
	};

	AutoBuffer buffer;

	bool waitingNewVideo;
	int TSID_;
	int SID_;
	PMTESInfo videoEs;
	std::vector<PMTESInfo> audioEs;

	PATDelegator PsiParserPAT;
	PMTDelegator PsiParserPMT;
	int pmtPid;
	VideoDelegator videoDelegator;
	std::vector<AudioDelegator*> audioDelegators;

	PidHandlerTable *curHandlerTable;
	PidHandlerTable *nextHandlerTable;

	TsPacketSelectorHandler *selectorHandler;

	void onPatUpdated(PsiSection section) {
		if (selectorHandler == NULL) {
			return;
		}
		PAT pat(section);
		if (pat.parse() && pat.check()) {
			int patTSID = pat.TSID();
			std::vector<int> sids;
			std::vector<int> pids;
			for (int i = 0; i < pat.numElems(); ++i) {
				PATElement elem = pat.get(i);
				if (!elem.is_network_PID()) {
					sids.push_back(elem.program_number());
					pids.push_back(elem.PID());
				}
			}
			if (TSID_ != patTSID) {
				// TSが変わった
				curHandlerTable->clear();
				PsiParserPMT.clear();
				TSID_ = patTSID;
			}
			int progidx = selectorHandler->onPidSelect(patTSID, sids);
			if (progidx >= sids.size()) {
				throw InvalidOperationException("選択したサービスインデックスは範囲外です");
			}
			if (progidx >= 0) {
				int sid = sids[progidx];
				int pid = pids[progidx];
				if (SID_ != sid) {
					// サービス選択が変わった
					curHandlerTable->clear();
					PsiParserPMT.clear();
					SID_ = sid;
				}
				pmtPid = pid;
				curHandlerTable->add(pmtPid, &PsiParserPMT);
			}
		}
	}

	void onPmtUpdated(PsiSection section) {
		if (selectorHandler == NULL) {
			return;
		}
		PMT pmt(section);
		if (pmt.parse() && pmt.check()) {

			printPMT(pmt);

			// 映像、オーディオストリームを探す
			PMTESInfo videoEs(-1, -1);
			std::vector<PMTESInfo> audioEs;
			for (int i = 0; i < pmt.numElems(); ++i) {
				PMTElement elem = pmt.get(i);
				uint8_t stream_type = elem.stream_type();
				if (isVideo(stream_type) && videoEs.stype == -1) {
					videoEs.stype = stream_type;
					videoEs.pid = elem.elementary_PID();
				}
				else if (isAudio(stream_type)) {
					audioEs.emplace_back(stream_type, elem.elementary_PID());
				}
			}
			if (videoEs.pid == -1) {
				// 映像ストリームがない
				ctx.warn("PMT 映像ストリームがありません");
				return;
			}
			if (audioEs.size() == 0) {
				ctx.warn("PMT オーディオストリームがありません");
			}

			// 
			PidHandlerTable *table = curHandlerTable;
			if (videoEs.pid != this->videoEs.pid) {
				// 映像ストリームが変わる場合
				waitingNewVideo = true;
				table = nextHandlerTable;
				if (this->videoEs.pid != -1) {
					ctx.info("PMT 映像ストリームの変更を検知");
				}
			}

			this->videoEs = videoEs;
			this->audioEs = audioEs;

			table->add(videoEs.pid, &videoDelegator);
			ensureAudioDelegators(int(audioEs.size()));
			for (int i = 0; i < int(audioEs.size()); ++i) {
				table->add(audioEs[i].pid, audioDelegators[i]);
			}

			selectorHandler->onPmtUpdated(pmt.PCR_PID());
			if (table == curHandlerTable) {
				selectorHandler->onPidTableChanged(videoEs, audioEs);
			}
		}
	}

	void onVideoPacket(int64_t clock, TsPacket packet) {
		if (selectorHandler != NULL) {
			selectorHandler->onVideoPacket(clock, packet);
		}
	}

	void onAudioPacket(int64_t clock, TsPacket packet, int audioIdx) {
		if (selectorHandler != NULL) {
			selectorHandler->onAudioPacket(clock, packet, audioIdx);
		}
	}

	void swapHandlerTable() {
		std::swap(curHandlerTable, nextHandlerTable);
		nextHandlerTable->clear();

		// PMTを引き継ぐ
		curHandlerTable->add(pmtPid, &PsiParserPMT);
	}

	void ensureAudioDelegators(int numAudios) {
		while (audioDelegators.size() < numAudios) {
			int audioIdx = int(audioDelegators.size());
			audioDelegators.push_back(new AudioDelegator(*this, audioIdx));
		}
	}

	bool isVideo(uint8_t stream_type) {
		switch (stream_type) {
		case 0x02: // MPEG2-VIDEO
		case 0x1B: // H.264/AVC
					 //	case 0x24: // H.265/HEVC
			return true;
		}
		return false;
	}

	bool isAudio(uint8_t stream_type) {
		// AAC以外未対応
		return (stream_type == 0x0F);
	}

	void printPMT(const PMT& pmt) {
		ctx.info("[PMT更新]");
		const char* content = NULL;
		for (int i = 0; i < pmt.numElems(); ++i) {
			PMTElement elem = pmt.get(i);
			switch (elem.stream_type()) {
			case 0x00:
				content = "ECM";
				break;
			case 0x02: // ITU-T Rec. H.262 and ISO/IEC 13818-2 (MPEG-2 higher rate interlaced video) in a packetized stream
				content = "MPEG2-VIDEO";
				break;
			case 0x04: // ISO/IEC 13818-3 (MPEG-2 halved sample rate audio) in a packetized stream
				content = "MPEG2-AUDIO";
				break;
			case 0x06: // ITU-T Rec. H.222 and ISO/IEC 13818-1 (MPEG-2 packetized data) privately defined (i.e., DVB subtitles / VBI and AC - 3)
				content = "字幕";
				break;
			case 0x0D: // ISO/IEC 13818-6 DSM CC tabled data
				content = "データカルーセル";
				break;
			case 0x0F: // ISO/IEC 13818-7 ADTS AAC (MPEG-2 lower bit-rate audio) in a packetized stream
				content = "ADTS AAC";
				break;
			case 0x1B: // ITU-T Rec. H.264 and ISO/IEC 14496-10 (lower bit-rate video) in a packetized stream
				content = "H.264/AVC";
				break;
			case 0x24: // ITU-T Rec. H.265 and ISO/IEC 23008-2 (Ultra HD video) in a packetized stream
				content = "H.265/HEVC";
				break;
			}

			if (content != NULL) {
				ctx.info("PID: 0x%04x TYPE: %s", elem.elementary_PID(), content);
			}
			else {
				ctx.info("PID: 0x%04x TYPE: Unknown (0x%04x)", elem.elementary_PID(), elem.stream_type());
			}
		}
	}
};
