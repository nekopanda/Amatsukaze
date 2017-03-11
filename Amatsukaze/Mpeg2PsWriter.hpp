/**
* MPEG2-PS writer
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "StreamUtils.hpp"
#include "Mpeg2TsParser.hpp"

#include <deque>
#include <vector>

enum {
	PACK_START_CODE = 0x01BA,
	PSM_START_CODE = 0x01BC,
	SYSTEM_HEADER_START_CODE = 0x01BB,
	MPEG_PROGRAM_END_CODE = 0x01B9,
};

struct PsPackHeader {

	bool parse(MemoryChunk data) {
		BitReader reader(data);
		pack_start_code = reader.read<32>();
		system_clock_reference = readSCR(reader);
		if (system_clock_reference == -1) return false;
		program_mux_rate = reader.read<22>();
		if (reader.read<2>() != 3) return false;
		reader.skip(5);
		uint8_t pack_stuffing_length = reader.read<3>();
		for (int i = 0; i < pack_stuffing_length; ++i) {
			if (reader.read<8>() != 0xFF) return false;
		}

		nReadBytes = reader.numReadBytes();

		// system header
		uint32_t code = reader.read<32>();
		if (code == SYSTEM_HEADER_START_CODE) {
			uint16_t header_length = reader.read<16>();
			reader.skip(header_length * 8);
			nReadBytes = reader.numReadBytes();
		}

		return true;
	}

	uint32_t pack_start_code;
	int64_t system_clock_reference;
	uint32_t program_mux_rate;

	int nReadBytes;

private:
	int64_t readSCR(BitReader& reader) {
		int64_t base = 0, ext;
		if (reader.read<2>() != 1) return -1;
		base = reader.read<3>();
		base <<= 15;
		if (reader.read<1>() != 1) return -1;
		base |= reader.read<15>();
		base <<= 15;
		if (reader.read<1>() != 1) return -1;
		base |= reader.read<15>();
		if (reader.read<1>() != 1) return -1;
		ext = reader.read<9>();
		if (reader.read<1>() != 1) return -1;
		return base * 300 + ext;
	}
};

struct PSMESInfo {
	int stype;
	int stream_id;

	PSMESInfo() { }
	PSMESInfo(int stype, int stream_id)
		: stype(stype), stream_id(stream_id) { }
};

struct PsProgramStreamMap : public AMTObject {

	PsProgramStreamMap(AMTContext&ctx)
		: AMTObject(ctx) { }

	bool parse(MemoryChunk data) {
		BitReader reader(data);
		reader.skip(32); // start code

		uint32_t program_stream_map_length = reader.read<16>();
		current_next_indicator = reader.read<1>();
		reader.skip(2);
		program_stream_map_version = reader.read<5>();
		reader.skip(7);
		if (reader.read<1>() != 1) return false;
		uint16_t program_stream_info_length = reader.read<16>();
		reader.skip(program_stream_info_length * 8);
		uint16_t elementary_stream_map_length = reader.read<16>();
		for (int remain = elementary_stream_map_length; remain > 0; ) {
			if (remain < 4) {
				// 長さ不正
				return false;
			}
			PSMESInfo info;
			info.stype = reader.read<8>();
			info.stream_id = reader.read<8>();
			uint16_t elementary_stream_info_length = reader.read<16>();
			reader.skip(elementary_stream_info_length * 8);
			remain -= 4 + elementary_stream_info_length;
			if (remain < 0) {
				// 長さ不正
				return false;
			}
		}
		reader.read<32>(); // CRC

		nReadBytes = reader.numReadBytes();

		uint32_t crc = ctx.getCRC()->calc(data.data, nReadBytes, uint32_t(-1));
		if (crc != 0) {
			// CRC不正
			return false;
		}

		return true;
	}

	uint8_t current_next_indicator;
	uint8_t program_stream_map_version;
	std::vector<PSMESInfo> streams;

	int nReadBytes;

};

// デバッグ用
class PsStreamVerifier {
public:
	PsStreamVerifier(AMTContext&ctx)
		: psm(ctx)
	{ }

	void verify(MemoryChunk mc) {
		nVideoPackets = nAudioPackets = 0;

		int pos = 0;
		uint32_t code;
		do {
			pos += pack(MemoryChunk(mc.data + pos, mc.length - pos));
			if (mc.length - pos < 4) {
				printf("WARNING: 終了コードがありませんでした\n");
				goto eof;
			}
			code = read32(mc.data + pos);
		} while (code == PACK_START_CODE);
		if (code != MPEG_PROGRAM_END_CODE) {
			printf("WARNING: 終了コードがありませんでした\n");
		}

	eof:
		printf("読み取り終了 VideoPackets: %d AudioPackets: %d\n", nVideoPackets, nAudioPackets);
	}
private:
	PsProgramStreamMap psm;

	int nVideoPackets;
	int nAudioPackets;

	int pack(MemoryChunk pack) {
		PsPackHeader header;
		if (!header.parse(pack)) {
			error();
		}
		int pos = header.nReadBytes;
		for (;;) {
			int len = pes(MemoryChunk(pack.data + pos, pack.length - pos));
			if (len == 0) return pos;
			pos += len;
		}
	}

	int pes(MemoryChunk packet) {
		if (packet.length < 4) {
			return 0;
		}
		if (read24(packet.data) != 0x01) {
			// スタートコード不正
			error();
		}
		uint8_t stream_id = packet.data[3];
		switch (stream_id) {
		case 0xBC: // program_stream_map
			if (!psm.parse(packet)) {
				error();
			}
			return psm.nReadBytes;
		case 0xFF: // dictionary
			return skipPesPacket(packet);
		case 0xB9: // STREAM END
		case 0xBA: // PACK START
			return 0;
		default:
			for (PSMESInfo& info : psm.streams) {
				if (stream_id == info.stream_id) {
					// ESストリーム
					if (isVideoStream(stream_id)) {
						++nVideoPackets;
					}
					if (isAudioStream(stream_id)) {
						++nAudioPackets;
					}
					return skipPesPacket(packet);
				}
			}
			if (isVideoStream(stream_id)) {
				++nVideoPackets;
			}
			if (isAudioStream(stream_id)) {
				++nAudioPackets;
			}
			else {
				// printf("不明stream: 0x%x\n", stream_id);
			}
			return skipPesPacket(packet);
		}
	}

	bool isVideoStream(int stream_id) {
		return (stream_id >> 4) == 0xE;
	}

	bool isAudioStream(int stream_id) {
		return (stream_id >> 5) == 0x6;
	}

	int skipPesPacket(MemoryChunk packet) {
		if (packet.length < 6) {
			error();
		}
		return 6 + read16(packet.data + 4);
	}

	void error() {
		printf("STREAM ERROR\n");
		throw FormatException();
	}
};

#define REDEFINE_PTS 0

struct PsSystemClock {
#if REDEFINE_PTS
	int64_t clockOffset;
#endif
	int maxBitsPerSecond;

	int64_t currentClock;

	PsSystemClock() :
#if REDEFINE_PTS
		clockOffset(-1),
#endif
		maxBitsPerSecond(0),
		currentClock(-1)
	{ }
};

struct PsAccessUnit {
	int sizeInBytes;
	int64_t DTS;
};

struct PsEsBuffer {
	int bufferSize;
	int filled;
	std::deque<PsAccessUnit> accessUnits;

	PsEsBuffer()
		: bufferSize(0)
		, filled(0)
	{ }

	int64_t makeSpace(int sizeInBytes) {
		int64_t time = -1;
		if (sizeInBytes > bufferSize) {
			printf("WARNING: VBV Buffer Underflow !!!\n");
			if (accessUnits.size() > 0) {
				time = accessUnits.back().DTS;
				filled = 0;
				accessUnits.clear();
			}
			return time;
		}
		while (bufferSize - filled < sizeInBytes) {
			PsAccessUnit au = accessUnits.front();
			accessUnits.pop_front();
			filled -= au.sizeInBytes;
			time = au.DTS;
		}
		return time;
	}
};

// PSは映像エンコード用なので、音声はおまけ
// 音声は複数チャネルあったとしても、最初の１チャンネルだけ出力する
class PsStreamWriter : public AMTObject {
	enum {
		BITRATE = 80 * 1000 * 1000, // MP@HLの最大ビットレート
		VBV_SIZE = 9781248 / 8, // MP@HLのVBV Buffer Size
		SYSTEM_CLOCK = 27 * 1000 * 1000,

		VIDEO_STREAM_ID = 0xE0,
		AUDIO_STREAM_ID = 0xC0,
	};
public:
	class EventHandler {
	public:
		virtual void onStreamData(MemoryChunk mc) = 0;
	};

	PsStreamWriter(AMTContext& ctx)
		: AMTObject(ctx)
	{
		systemClock.maxBitsPerSecond = BITRATE;
		videoBuffer.bufferSize = VBV_SIZE;
		audioBuffer.bufferSize = 3584; // とりあえずデフォルト値
		audioChannels = AUDIO_NONE;
		psmVersion = 0;
		videoStreamType = 0;
		audioStreamType = 0;
		nextIsPSM = true;
	}

	void setHandler(EventHandler* handler) {
		this->handler = handler;
	}

	// ファイルの先頭で必ず呼び出すこと
	void outHeader(int videoStreamType, int audioStreamType) {
		if (this->videoStreamType != videoStreamType ||
			this->audioStreamType != audioStreamType)
		{
			this->videoStreamType = videoStreamType;
			this->audioStreamType = audioStreamType;
			++psmVersion;
		}
		nextIsPSM = true;
	}

	void outVideoPesPacket(int64_t clock, const std::vector<VideoFrameInfo>& frames, PESPacket packet) {
		if (frames.size() == 0) return;

		initWhenNeeded(clock);
#if REDEFINE_PTS
    int64_t PTS = frames.front().PTS - systemClock.clockOffset / 300;
    int64_t DTS = frames.front().DTS - systemClock.clockOffset / 300;
    int64_t lastDTS = frames.back().DTS - systemClock.clockOffset / 300;
#else
    int64_t PTS = frames.front().PTS;
    int64_t DTS = frames.front().DTS;
    int64_t lastDTS = frames.back().DTS;
#endif

		// デコーダバッファが空くまでクロックを進める
		putAccessUnit(lastDTS, (int)packet.length, videoBuffer);

		// パケットデータを書き込む
		writePesPacket(packet, VIDEO_STREAM_ID, PTS, DTS);

		// 出力
		outPack();
	}

	void outAudioPesPacket(int audioIdx, int64_t clock, const std::vector<AudioFrameData>& frames, PESPacket packet) {
		if (audioIdx != 0) return;
		if (frames.size() == 0) return;

		initWhenNeeded(clock);
#if REDEFINE_PTS
		int64_t PTS = frames.front().PTS - systemClock.clockOffset / 300;
		int64_t lastDTS = frames.back().PTS - systemClock.clockOffset / 300;
#else
    int64_t PTS = frames.front().PTS;
    int64_t lastDTS = frames.back().PTS;
#endif

		// バッファサイズ調整
		if (audioChannels != frames.front().format.channels) {
			audioChannels = frames.front().format.channels;
			audioBuffer.bufferSize = audioBufferSize(getNumAudioChannels(audioChannels));
		}

		// デコーダバッファが空くまでクロックを進める
		putAccessUnit(lastDTS, (int)packet.length, audioBuffer);

		// パケットデータを書き込む
		writePesPacket(packet, AUDIO_STREAM_ID, PTS, PTS);

		// 出力
		outPack();
	}
private:
	EventHandler* handler;
	PsSystemClock systemClock;
	PsEsBuffer videoBuffer;
	PsEsBuffer audioBuffer;

	AUDIO_CHANNELS audioChannels;

	int videoStreamType;
	int audioStreamType;
	int psmVersion;
	bool nextIsPSM;

	// outVideoPesPacket, outAudioPesPacketの最後で必ずクリアされること
	AutoBuffer buffer;
	
	void initWhenNeeded(int64_t clock) {
		if (systemClock.currentClock == -1) {
#if REDEFINE_PTS
			systemClock.clockOffset = clock - SYSTEM_CLOCK;
      ctx.info("[PsWriter] ClockOffset = %lld", systemClock.clockOffset);
#endif
			systemClock.currentClock = clock;
		}
		if (nextIsPSM) {
			nextIsPSM = false;

			// Pack header
			BitWriter writer(buffer);
			writePackHeader(writer);

			// Program Stream Map
			int psmStart = (int)buffer.size();
			writer.write<32>(PSM_START_CODE);

			int psm_length = 2 + 2 + 2 + 4 * 2 + 4;
			writer.write<16>(psm_length);
			writer.write<1>(1); // current_next_indicator
			writer.write<2>(0xFF); // reserved
			writer.write<5>(psmVersion); // program_stream_map_version
			writer.write<7>(0xFF); // reserved
			writer.write<1>(0xFF); // marker bit
			writer.write<16>(0); // program_stream_info_length
			writer.write<16>(4 * 2); // elementary_stream_map_length

			writer.write<8>(videoStreamType);
			writer.write<8>(VIDEO_STREAM_ID);
			writer.write<16>(0);

			writer.write<8>(audioStreamType);
			writer.write<8>(AUDIO_STREAM_ID);
			writer.write<16>(0);
			writer.flush();

			uint32_t crc = ctx.getCRC()->calc(
				buffer.get() + psmStart, (int)buffer.size() - psmStart, uint32_t(-1));
			writer.write<32>(crc);
			writer.flush();

			outPack();
		}
	}

	void writeSCR(BitWriter& writer, int64_t scr) {
		int64_t base = scr / 300;
		int ext = int(scr % 300);

		writer.write<2>(1); // '01'
		writer.write<3>(uint32_t(base >> 30));
		writer.write<1>(1); // marker_bit
		writer.write<15>(uint32_t(base >> 15));
		writer.write<1>(1); // marker_bit
		writer.write<15>(uint32_t(base));
		writer.write<1>(1); // marker_bit
		writer.write<9>(ext);
		writer.write<1>(1); // marker_bit

		// 48bits
	}

	void writePTS(BitWriter& writer, uint8_t prefix, int64_t pts) {
		writer.write<4>(prefix);
		writer.write<3>(uint32_t(pts >> 30));
		writer.write<1>(1); // marker_bit
		writer.write<15>(uint32_t(pts >> 15));
		writer.write<1>(1); // marker_bit
		writer.write<15>(uint32_t(pts));
		writer.write<1>(1); // marker_bit
	}

	void writePackHeader(BitWriter& writer) {
		writer.write<32>(PACK_START_CODE);
		writeSCR(writer, systemClock.currentClock);
		writer.write<22>(BITRATE / (50 * 8)); // program mux rate
		writer.write<2>(0xFF); // marker bit x2
		writer.write<5>(0xFF); // reserved
		writer.write<3>(0); // pack stuffing length
		writer.flush();
	}

	void writePesPacketHeader(BitWriter& writer,
		uint8_t stream_id, uint16_t payload_length, uint8_t PTS_DTS_flag, int64_t PTS, int64_t DTS)
	{
		int header_length = 0;
		if (PTS_DTS_flag & 1) header_length += 5;
		if (PTS_DTS_flag & 2) header_length += 5;

		writer.write<24>(1); // start code
		writer.write<8>(stream_id);
		writer.write<16>(3 + header_length + payload_length); // PES_packet_length

		writer.write<2>(2); // '10'
		writer.write<2>(0); // PES_scrambling_control
		writer.write<1>(0); // PES_priority
		writer.write<1>(0); // data_alignment_indicator
		writer.write<1>(0); // copyright
		writer.write<1>(1); // original_or_copy
		writer.write<2>(PTS_DTS_flag);
		writer.write<6>(0); // 他のフラグまとめて

		writer.write<8>(header_length);
		if (PTS_DTS_flag == 2) {
			writePTS(writer, 2, PTS);
		}
		else if (PTS_DTS_flag == 3) {
			writePTS(writer, 3, PTS);
			writePTS(writer, 1, DTS);
		}

		writer.flush();
	}

	void writePesPacket(PESPacket packet, uint8_t stream_id, int64_t PTS, int64_t DTS) {
		MemoryChunk payload = packet.paylod();
		int offset = 0;

		// パケット長が PES_packet_length で表せる16bitを超えている場合があるので分割する必要がある
		do {
			int length = std::min<int>(32 * 1000, (int)payload.length - offset);
			BitWriter writer(buffer);
			if (offset == 0) {
				// 先頭
				writePackHeader(writer);
				writePesPacketHeader(writer, stream_id, length, packet.PTS_DTS_flags(), PTS, DTS);
			}
			else {
				// 先頭以外
				writePesPacketHeader(writer, stream_id, length, 0, 0, 0);
			}
			buffer.add(payload.data + offset, length);

			offset += length;

		} while (offset < payload.length);
	}

	// 指定バイト数を入力するのにかかる時間だけクロックを進める
	void proceedClock(int streamBytes) {
		int64_t clockDiff = int64_t(streamBytes) * 8 * SYSTEM_CLOCK / BITRATE;
		systemClock.currentClock += clockDiff;
	}

	void putAccessUnit(int64_t DTS, int sizeInBytes, PsEsBuffer& esBuffer) {
		// 本当はpacketには複数のアクセスユニットが含まれるが面倒なので1つのアクセスユニットとみなす
		PsAccessUnit au;
		au.DTS = DTS;
		au.sizeInBytes = sizeInBytes;

		// 空きができる時間（十分な空きがすでにある場合は-1）
		int64_t time = esBuffer.makeSpace(au.sizeInBytes);
		if (time > systemClock.currentClock) {
			// バッファに空きがないのでtimeまで待つ
			systemClock.currentClock = time;
		}

		// アクセスユニットを追加
		esBuffer.accessUnits.push_back(au);
	}

	// bufferに書き込んだpackを出力
	void outPack() {
		// データを出力
		if (handler != NULL) {
			handler->onStreamData(buffer);
		}
		// クロックを進める
		proceedClock((int)buffer.size());
		// バッファをクリアしておく
		buffer.clear();
	}

	static int audioBufferSize(int nChannel) {
		if (nChannel <= 2) return 3584;
		else if (nChannel <= 8) return 8976;
		else if (nChannel <= 12) return 12804;
		else return 51216;
	}
};
