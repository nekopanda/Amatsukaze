#pragma once

#include "CoreUtils.hpp"

/** @brief リングバッファではないがtrimHeadとtrimTailが同じくらい高速なバッファ */
class AutoBuffer {
public:
	AutoBuffer()
		: data_(NULL)
		, capacity_(0)
		, head_(0)
		, tail_(0)
	{ }

	void add(uint8_t* data, size_t size) {
		ensure(size);
		memcpy(data_ + tail_, data, size);
		tail_ += size;
	}

	void add(uint8_t byte) {
		if (tail_ >= capacity_) {
			ensure(1);
		}
		data_[tail_++] = byte;
	}

	/** @brief 有効なデータサイズ */
	size_t size() const {
		return tail_ - head_;
	}

	/** @brief データへのポインタ */
	uint8_t* get() const {
		return &data_[head_];
	}

	/** @brief size分だけ頭を削る */
	void trimHead(size_t size) {
		head_ = std::min(head_ + size, tail_);
		if (head_ == tail_) { // 中身がなくなったら位置を初期化しておく
			head_ = tail_ = 0;
		}
	}

	/** @brief size分だけ尻を削る */
	void trimTail(size_t size) {
		if (this->size() < size) {
			tail_ = head_;
		}
		else {
			tail_ -= size;
		}
	}

	/** @brief メモリは開放しないがデータサイズをゼロにする */
	void clear() {
		head_ = tail_ = 0;
	}

	/** @brief メモリを開放して初期状態にする（再使用可能）*/
	void release() {
		clear();
		if (data_ != NULL) {
			delete[] data_;
			data_ = NULL;
			capacity_ = 0;
		}
	}

private:
	uint8_t* data_;
	size_t capacity_;
	size_t head_;
	size_t tail_;

	size_t nextSize(size_t current) {
		if (current < 256) {
			return 256;
		}
		return current * 3 / 2;
	}

	void ensure(size_t extra) {
		if (tail_ + extra > capacity_) {
			// 足りない
			size_t next = nextSize(tail_ - head_ + extra);
			if (next <= capacity_) {
				// 容量は十分なのでデータを移動してスペースを作る
				memmove(data_, data_ + head_, tail_ - head_);
			}
			else {
				uint8_t* new_ = new uint8_t[next];
				if (data_ != NULL) {
					memcpy(new_, data_ + head_, tail_ - head_);
					delete[] data_;
				}
				data_ = new_;
				capacity_ = next;
			}
			tail_ -= head_;
			head_ = 0;
		}
	}
};

/** @brief ポインタとサイズのセット */
struct MemoryChunk {

	MemoryChunk() : data(NULL), length(0) { }
	MemoryChunk(uint8_t* data, size_t length) : data(data), length(length) { }
	/** @brief AutoBufferのget()とsize()から作成 */
	MemoryChunk(const AutoBuffer& buffer) : data(buffer.get()), length(buffer.size()) { }

	// データの中身を比較
	bool operator==(MemoryChunk o) const {
		if (o.length != length) return false;
		return memcmp(data, o.data, length) == 0;
	}
	bool operator!=(MemoryChunk o) const {
		return !operator==(o);
	}

	uint8_t* data;
	size_t length;
};

enum {
	TS_SYNC_BYTE = 0x47,

	TS_PACKET_LENGTH = 188,
	TS_PACKET_LENGTH2 = 192,

	MAX_PID = 0x1FFF,

  MPEG_CLOCK_HZ = 90000, // MPEG2,H264,H265はPTSが90kHz単位となっている
};

/** @brief shiftだけ右シフトしてmask数bitだけ返す(bit shift mask) */
template <typename T>
T bsm(T v, int shift, int mask) {
	return (v >> shift) & ((T(1) << mask) - 1);
}

/** @brief mask数bitだけshiftだけ左シフトして書き込む(bit mask shift) */
template <typename T, typename U>
void bms(T& v, U data, int shift, int mask) {
	v |= (data & ((T(1) << mask) - 1)) << shift;
}

template<int bytes, typename T>
T readN(const uint8_t* ptr) {
	T r = 0;
	for (int i = 0; i < bytes; ++i) {
		r = r | (T(ptr[i]) << ((bytes - i - 1) * 8));
	}
	return r;
}
uint16_t read16(const uint8_t* ptr) { return readN<2, uint16_t>(ptr); }
uint32_t read24(const uint8_t* ptr) { return readN<3, uint32_t>(ptr); }
uint32_t read32(const uint8_t* ptr) { return readN<4, uint32_t>(ptr); }
uint64_t read40(const uint8_t* ptr) { return readN<5, uint64_t>(ptr); }
uint64_t read48(const uint8_t* ptr) { return readN<6, uint64_t>(ptr); }

template<int bytes, typename T>
void writeN(uint8_t* ptr, T w) {
	for (int i = 0; i < bytes; ++i) {
		ptr[i] = uint8_t(w >> ((bytes - i - 1) * 8));
	}
}
void write16(uint8_t* ptr, uint16_t w) { writeN<2, uint16_t>(ptr, w); }
void write24(uint8_t* ptr, uint32_t w) { writeN<3, uint32_t>(ptr, w); }
void write32(uint8_t* ptr, uint32_t w) { writeN<4, uint32_t>(ptr, w); }
void write40(uint8_t* ptr, uint64_t w) { writeN<5, uint64_t>(ptr, w); }
void write48(uint8_t* ptr, uint64_t w) { writeN<6, uint64_t>(ptr, w); }


class BitReader {
public:
	BitReader(MemoryChunk data)
		: data(data)
		, offset(0)
		, filled(0)
	{
		fill();
	}

	bool canRead(int bits) {
		return (filled + (data.length - offset) * 8) >= bits;
	}

	template <int bits>
	uint32_t read() {
		return readn(bits);
	}

	uint32_t readn(int bits) {
		if (bits <= filled) {
			return read_(bits);
		}
		fill();
		if (bits > filled) {
			throw EOFException("BitReader.readでオーバーラン");
		}
		return read_(bits);
	}

	int32_t readExpGolomSigned() {
		static int table[] = { 1, -1 };
		uint32_t v = readExpGolom() + 1;
		return (v >> 1) * table[v & 1];
	}

	uint32_t readExpGolom() {
		uint64_t masked = bsm(current, 0, filled);
		if (masked == 0) {
			fill();
			masked = bsm(current, 0, filled);
			if (masked == 0) {
				throw EOFException("BitReader.readExpGolomでオーバーラン");
			}
		}
		int bodyLen = filled - __builtin_clzl(masked);
		filled -= bodyLen - 1;
		if (bodyLen > filled) {
			fill();
			if (bodyLen > filled) {
				throw EOFException("BitReader.readExpGolomでオーバーラン");
			}
		}
		int shift = filled - bodyLen;
		filled -= bodyLen;
		return (uint32_t)bsm(current, shift, bodyLen) - 1;
	}

	void skip(int bits) {
		if (filled > bits) {
			filled -= bits;
		}
		else {
			// 今fillされている分を引く
			bits -= filled;
			filled = 0;
			// これでバイトアラインされるので残りバイト数スキップ
			int skipBytes = bits / 8;
			offset += skipBytes;
			if (offset > data.length) {
				throw EOFException("BitReader.skipでオーバーラン");
			}
			bits -= skipBytes * 8;
			// もう1回fillして残ったビット数を引く
			fill();
			if (bits > filled) {
				throw EOFException("BitReader.skipでオーバーラン");
			}
			filled -= bits;
		}
	}

	// 次のバイト境界までのビットを捨てる
	void byteAlign() {
		fill();
		filled &= ~7;
	}

	// 読んだバイト数（中途半端な部分まで読んだ場合も１バイトとして計算）
	int numReadBytes() {
		return offset - (filled / 8);
	}

private:
	MemoryChunk data;
	int offset;
	uint64_t current;
	int filled;

	void fill() {
		while (filled + 8 <= 64 && offset < data.length) readByte();
	}

	void readByte() {
		current = (current << 8) | data.data[offset++];
		filled += 8;
	}

	uint32_t read_(int bits) {
		int shift = filled - bits;
		filled -= bits;
		return (uint32_t)bsm(current, shift, bits);
	}
};

class BitWriter {
public:
	BitWriter(AutoBuffer& dst)
		: dst(dst)
		, current(0)
		, filled(0)
	{
	}

	void writen(uint32_t data, int bits) {
		if (filled + bits > 64) {
			store();
		}
		current |= (((uint64_t)data & ((uint64_t(1)<<bits)-1)) << (64 - filled - bits));
		filled += bits;
	}

	template <int bits>
	void write(uint32_t data) {
		writen(data, bits);
	}

	template <bool bits>
	void byteAlign() {
		int pad = ((filled + 7) & ~7) - filled;
		filled += pad;
		if (bits) {
			current |= ((uint64_t(1) << pad) - 1) << (64 - filled);
		}
	}

	void flush() {
		if (filled & 7) {
			throw FormatException("バイトアラインしていません");
		}
		store();
	}

private:
	AutoBuffer& dst;
	uint64_t current;
	int filled;

	void storeByte() {
		dst.add(uint8_t(current >> 56));
		current <<= 8;
		filled -= 8;
	}

	void store() {
		while (filled >= 8) storeByte();
	}
};

class CRC32 {
public:
	CRC32() {
		createTable(table, 0x04C11DB7UL);
	}

	uint32_t calc(const uint8_t* data, int length, uint32_t crc) const {
		for (int i = 0; i < length; ++i) {
			crc = (crc << 8) ^ table[(crc >> 24) ^ data[i]];
		}
		return crc;
	}

	const uint32_t* getTable() const { return table; }

private:
	uint32_t table[256];

	static void createTable(uint32_t* table, uint32_t exp) {
		for (int i = 0; i < 256; ++i) {
			uint32_t crc = i << 24;
			for (int j = 0; j < 8; ++j) {
				if (crc & 0x80000000UL) {
					crc = (crc << 1) ^ exp;
				}
				else {
					crc = crc << 1;
				}
			}
			table[i] = crc;
		}
	}
};

class File : NonCopyable
{
public:
  File(const std::string& path, const char* mode) {
    fp_ = _fsopen(path.c_str(), mode, _SH_DENYNO);
    if (fp_ == NULL) {
      THROWF(IOException, "failed to open file %s", path.c_str());
    }
  }
  ~File() {
    fclose(fp_);
  }
  void write(MemoryChunk mc) {
    if (fwrite(mc.data, mc.length, 1, fp_) != 1) {
      THROWF(IOException, "failed to write to file");
    }
  }
  size_t read(MemoryChunk mc) {
    size_t ret = fread(mc.data, 1, mc.length, fp_);
    if (ret <= 0) {
      THROWF(IOException, "failed to read from file");
    }
    return ret;
  }
  void flush() {
    fflush(fp_);
  }
  void seek(int64_t offset, int origin) {
    if (_fseeki64(fp_, offset, origin) != 0) {
      THROWF(IOException, "failed to seek file");
    }
  }
private:
  FILE* fp_;
};

enum TS_SPLITTER_LOG_LEVEL {
	TS_SPLITTER_DEBUG,
	TS_SPLITTER_INFO,
	TS_SPLITTER_WARN,
	TS_SPLITTER_ERROR
};

class AMTContext {
public:
	AMTContext()
		: debugEnabled(true)
	{ }

	const CRC32* getCRC() const {
		return &crc;
	}

	void debug(const char *fmt, ...) const {
		if (!debugEnabled) return;
		va_list arg; va_start(arg, fmt);
		print(fmt, arg, TS_SPLITTER_INFO);
		va_end(arg);
	}
	void info(const char *fmt, ...) const {
		va_list arg; va_start(arg, fmt);
		print(fmt, arg, TS_SPLITTER_INFO);
		va_end(arg);
	}
	void warn(const char *fmt, ...) const {
		va_list arg; va_start(arg, fmt);
		print(fmt, arg, TS_SPLITTER_WARN);
		va_end(arg);
	}
	void error(const char *fmt, ...) const {
		va_list arg; va_start(arg, fmt);
		print(fmt, arg, TS_SPLITTER_ERROR);
		va_end(arg);
	}

private:
	bool debugEnabled;
	CRC32 crc;

	void print(const char* fmt, va_list arg, TS_SPLITTER_LOG_LEVEL level) const {
		// TODO:
		char buf[300];
		vsnprintf_s(buf, sizeof(buf), fmt, arg);
		printf("%s\n", buf);
	}
};

class AMTObject {
public:
	AMTObject(AMTContext* ctx) : ctx(ctx) { }
protected:
	AMTContext* ctx;
};

enum VIDEO_STREAM_FORMAT {
	VS_MPEG2,
	VS_H264,
	VS_H265
};

enum PICTURE_TYPE {
	PIC_FRAME = 0, // progressive frame
	PIC_FRAME_DOUBLING, // frame doubling
	PIC_FRAME_TRIPLING, // frame tripling
	PIC_TFF, // top field first
	PIC_BFF, // bottom field first
	PIC_TFF_RFF, // tff かつ repeat first field
	PIC_BFF_RFF, // bff かつ repeat first field
	MAX_PIC_TYPE,
};

const char* PictureTypeString(PICTURE_TYPE pic) {
	switch (pic) {
	case PIC_FRAME: return "FRAME";
	case PIC_FRAME_DOUBLING: return "DBL";
	case PIC_FRAME_TRIPLING: return "TLP";
	case PIC_TFF: return "TFF";
	case PIC_BFF: return "BFF";
	case PIC_TFF_RFF: return "TFF_RFF";
	case PIC_BFF_RFF: return "BFF_RFF";
	default: return "UNK";
	}
}

enum FRAME_TYPE {
	FRAME_NO_INFO = 0,
	FRAME_I,
	FRAME_P,
	FRAME_B,
	FRAME_OTHER,
	MAX_FRAME_TYPE,
};

const char* FrameTypeString(FRAME_TYPE frame) {
	switch (frame) {
	case FRAME_I: return "I";
	case FRAME_P: return "P";
	case FRAME_B: return "B";
	default: return "UNK";
	}
}

double presenting_time(PICTURE_TYPE picType, double frameRate) {
	switch (picType) {
	case PIC_FRAME: return 1.0 / frameRate;
	case PIC_FRAME_DOUBLING: return 2.0 / frameRate;
	case PIC_FRAME_TRIPLING: return 3.0 / frameRate;
	case PIC_TFF: return 1.0 / frameRate;
	case PIC_BFF: return 1.0 / frameRate;
	case PIC_TFF_RFF: return 1.5 / frameRate;
	case PIC_BFF_RFF: return 1.5 / frameRate;
	}
	// 不明
	return 1.0 / frameRate;
}

struct VideoFormat {
	int width, height; // 横縦
	int sarWidth, sarHeight; // アスペクト比
	int frameRateNum, frameRateDenom; // フレームレート
  uint8_t colorPrimaries, transferCharacteristics, colorSpace; // カラースペース
  bool progressive;

	VideoFormat()
		: width(0)
		, height(0)
		, sarWidth(0)
		, sarHeight(0)
    , frameRateNum(0)
    , frameRateDenom(0)
    , colorPrimaries(0)
    , transferCharacteristics(0)
    , colorSpace(0)
    , progressive(false)
	{ }

	bool isEmpty() const {
		return width == 0;
	}

	bool operator==(const VideoFormat& o) const {
		return (width == o.width && height == o.height
			&& frameRateNum == o.frameRateNum && frameRateDenom == o.frameRateDenom
      && progressive == o.progressive);
	}
	bool operator!=(const VideoFormat& o) const {
		return !(*this == o);
	}
};

struct VideoFrameInfo {
	int64_t PTS, DTS;
	// MPEG2の場合 sequence header がある
	// H264の場合 SPS がある
	bool isGopStart;
	PICTURE_TYPE pic;
	FRAME_TYPE type; // 使わないけど参考情報
	VideoFormat format;

	VideoFrameInfo()
		: PTS(0)
		, pic(PIC_FRAME)
		, type(FRAME_NO_INFO)
	{ }
};

enum AUDIO_CHANNELS {
	AUDIO_NONE,

	AUDIO_MONO,
	AUDIO_STEREO,
	AUDIO_30, // 3/0
	AUDIO_31, // 3/1
	AUDIO_32, // 3/2
	AUDIO_32_LFE, // 5.1ch

	AUDIO_21, // 2/1
	AUDIO_22, // 2/2
	AUDIO_2LANG, // 2 音声 (1/ 0 + 1 / 0)

				 // 以下4K向け
	AUDIO_52_LFE, // 7.1ch
	AUDIO_33_LFE, // 3/3.1
	AUDIO_2_22_LFE, // 2/0/0-2/0/2-0.1
	AUDIO_322_LFE, // 3/2/2.1
	AUDIO_2_32_LFE, // 2/0/0-3/0/2-0.1
	AUDIO_020_32_LFE, // 0/2/0-3/0/2-0.1 // AUDIO_2_32_LFEと区別できなくね？
	AUDIO_2_323_2LFE, // 2/0/0-3/2/3-0.2
	AUDIO_333_523_3_2LFE, // 22.2ch
};

const char* getAudioChannelString(AUDIO_CHANNELS channels) {
	switch (channels) {
	case AUDIO_MONO: return "モノラル";
	case AUDIO_STEREO: return "ステレオ";
	case AUDIO_30: return "3/0";
	case AUDIO_31: return "3/1";
	case AUDIO_32: return "3/2";
	case AUDIO_32_LFE: return "5.1ch";
	case AUDIO_21: return "2/1";
	case AUDIO_22: return "2/2";
	case AUDIO_2LANG: return "2音声";
	case AUDIO_52_LFE: return "7.1ch";
	case AUDIO_33_LFE: return "3/3.1";
	case AUDIO_2_22_LFE: return "2/0/0-2/0/2-0.1";
	case AUDIO_322_LFE: return "3/2/2.1";
	case AUDIO_2_32_LFE: return "2/0/0-3/0/2-0.1";
	case AUDIO_020_32_LFE: return "0/2/0-3/0/2-0.1";
	case AUDIO_2_323_2LFE: return "2/0/0-3/2/3-0.2";
	case AUDIO_333_523_3_2LFE: return "22.2ch";
	}
	return "エラー";
}

int getNumAudioChannels(AUDIO_CHANNELS channels) {
	switch (channels) {
	case AUDIO_MONO: return 1;
	case AUDIO_STEREO: return 2;
	case AUDIO_30: return 3;
	case AUDIO_31: return 4;
	case AUDIO_32: return 5;
	case AUDIO_32_LFE: return 6;
	case AUDIO_21: return 3;
	case AUDIO_22: return 4;
	case AUDIO_2LANG: return 2;
	case AUDIO_52_LFE: return 8;
	case AUDIO_33_LFE: return 7;
	case AUDIO_2_22_LFE: return 7;
	case AUDIO_322_LFE: return 8;
	case AUDIO_2_32_LFE: return 8;
	case AUDIO_020_32_LFE: return 8;
	case AUDIO_2_323_2LFE: return 12;
	case AUDIO_333_523_3_2LFE: return 24;
	}
	return 2; // 不明
}

struct AudioFormat {
	AUDIO_CHANNELS channels;
	int sampleRate;

	bool operator==(const AudioFormat& o) const {
		return (channels == o.channels && sampleRate == o.sampleRate);
	}
	bool operator!=(const AudioFormat& o) const {
		return !(*this == o);
	}
};

struct AudioFrameInfo {
	int64_t PTS;
	int numSamples; // 1チャンネルあたりのサンプル数
	AudioFormat format;
};

struct AudioFrameData : public AudioFrameInfo {
	int codedDataSize;
	uint8_t* codedData;
	int numDecodedSamples;
	int decodedDataSize;
	uint16_t* decodedData;
};

class IVideoParser {
public:
	// とりあえず必要な物だけ
	virtual void reset() = 0;

	// PTS, DTS: 90kHzタイムスタンプ 情報がない場合は-1
	virtual bool inputFrame(MemoryChunk frame, std::vector<VideoFrameInfo>& info, int64_t PTS, int64_t DTS) = 0;
};

