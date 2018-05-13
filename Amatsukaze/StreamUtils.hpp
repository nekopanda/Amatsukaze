/**
* Memory and Stream utility
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <algorithm>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <fstream>
#include <cctype>
#include <locale>
#include <codecvt>

#include "CoreUtils.hpp"
#include "OSUtil.hpp"
#include "StringUtils.hpp"

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
		return (filled + ((int)data.length - offset) * 8) >= bits;
	}

	// bitsビット読んで進める
	// bits <= 32
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

	// bitsビット読むだけ
	// bits <= 32
	template <int bits>
	uint32_t next() {
		return nextn(bits);
	}

	uint32_t nextn(int bits) {
		if (bits <= filled) {
			return next_(bits);
		}
		fill();
		if (bits > filled) {
			throw EOFException("BitReader.nextでオーバーラン");
		}
		return next_(bits);
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
			if (offset > (int)data.length) {
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
		while (filled + 8 <= 64 && offset < (int)data.length) readByte();
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

	uint32_t next_(int bits) {
		int shift = filled - bits;
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
			THROW(FormatException, "バイトアラインしていません");
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

enum AMT_LOG_LEVEL {
	AMT_LOG_DEBUG,
	AMT_LOG_INFO,
	AMT_LOG_WARN,
	AMT_LOG_ERROR
};

enum AMT_ERROR_COUNTER {
   // 不明なPTSのフレーム
   AMT_ERR_UNKNOWN_PTS = 0,
   // PESパケットデコードエラー
   AMT_ERR_DECODE_PACKET_FAILED,
   // H264におけるPTSミスマッチ
   AMT_ERR_H264_PTS_MISMATCH,
   // H264におけるフィールド配置エラー
   AMT_ERR_H264_UNEXPECTED_FIELD,
   // PTSが戻っている
   AMT_ERR_NON_CONTINUOUS_PTS,
   // DRCSマッピングがない
   AMT_ERR_NO_DRCS_MAP,
   // エラーの個数
   AMT_ERR_MAX,
};

const char* AMT_ERROR_NAMES[] = {
   "unknown-pts",
   "decode-packet-failed",
   "h264-pts-mismatch",
   "h264-unexpected-field",
   "non-continuous-pts",
   "no-drcs-map",
};

class AMTContext {
public:
	AMTContext()
		: debugEnabled(true)
		, acp(GetACP())
      , errCounter()
	{ }

	const CRC32* getCRC() const {
		return &crc;
	}

	void debug(const char *fmt, ...) const {
		if (!debugEnabled) return;
		va_list arg; va_start(arg, fmt);
		print(fmt, arg, AMT_LOG_DEBUG);
		va_end(arg);
	}
	void info(const char *fmt, ...) const {
		va_list arg; va_start(arg, fmt);
		print(fmt, arg, AMT_LOG_INFO);
		va_end(arg);
	}
	void warn(const char *fmt, ...) const {
		va_list arg; va_start(arg, fmt);
		print(fmt, arg, AMT_LOG_WARN);
		va_end(arg);
	}
	void error(const char *fmt, ...) const {
		va_list arg; va_start(arg, fmt);
		print(fmt, arg, AMT_LOG_ERROR);
		va_end(arg);
	}
	void progress(const char *fmt, ...) const {
		va_list arg; va_start(arg, fmt);
		printProgress(fmt, arg);
		va_end(arg);
	}

	void registerTmpFile(const std::string& path) {
		tmpFiles.insert(path);
	}

	void clearTmpFiles() {
		for (auto& path : tmpFiles) {
			remove(path.c_str());
		}
		tmpFiles.clear();
	}

	void incrementCounter(AMT_ERROR_COUNTER err) {
      errCounter[err]++;
	}

	int getErrorCount(AMT_ERROR_COUNTER err) const {
		return errCounter[err];
	}

	void setError(const Exception& exception) {
		errMessage = exception.message();
	}

	const std::string& getError() const {
		return errMessage;
	}

	const std::map<std::string, std::wstring>& getDRCSMapping() const {
		return drcsMap;
	}

	void loadDRCSMapping(const std::string& mapPath)
	{
		if (File::exists(mapPath) == false) {
			THROWF(ArgumentException, "DRCSマッピングファイルが見つかりません: %s",
				mapPath.c_str());
		}
		else {
			// BOMありUTF-8で読み込む
			std::wifstream input(mapPath);
			// codecvt_utf8 はUCS-2にしか対応していないので使わないように！
			// 必ず codecvt_utf8_utf16 を使うこと
			input.imbue(std::locale(input.getloc(), new std::codecvt_utf8_utf16<wchar_t, 0x10ffff, std::consume_header>));
			for (std::wstring line; getline(input, line);)
			{
				if (line.size() >= 34) {
					std::string key(line.begin(), line.begin() + 32);
					std::transform(key.begin(), key.end(), key.begin(), ::toupper);
					bool ok = (line[32] == '=');
					for (auto c : key) if (!isxdigit(c)) ok = false;
					if (ok) {
						drcsMap[key] = std::wstring(line.begin() + 33, line.end());
					}
				}
			}
		}
	}

	// コンソール出力をデフォルトコードページに設定
	void setDefaultCP() {
		SetConsoleCP(acp);
		SetConsoleOutputCP(acp);
	}

private:
	bool debugEnabled;
	CRC32 crc;
	int acp;

	std::set<std::string> tmpFiles;
   std::array<int, AMT_ERR_MAX> errCounter;
	std::string errMessage;

	std::map<std::string, std::wstring> drcsMap;

	void print(const char* fmt, va_list arg, AMT_LOG_LEVEL level) const {
		static const char* log_levels[] = { "debug", "info", "warn", "error" };
		char buf[1024];
		vsnprintf_s(buf, sizeof(buf), fmt, arg);
		PRINTF("AMT [%s] %s\n", log_levels[level], buf);
	}

	void printProgress(const char* fmt, va_list arg) const {
		char buf[1024];
		vsnprintf_s(buf, sizeof(buf), fmt, arg);
		PRINTF("AMT %s\r", buf);
	}
};

class AMTObject {
public:
	AMTObject(AMTContext& ctx) : ctx(ctx) { }
	AMTContext& ctx;
};

enum DECODER_TYPE {
	DECODER_DEFAULT = 0,
	DECODER_QSV,
	DECODER_CUVID,
};

struct DecoderSetting {
	DECODER_TYPE mpeg2;
	DECODER_TYPE h264;
	DECODER_TYPE hevc;

	DecoderSetting()
		: mpeg2(DECODER_DEFAULT)
		, h264(DECODER_DEFAULT)
		, hevc(DECODER_DEFAULT)
	{ }
};

enum CMType {
	CMTYPE_BOTH = 0,
	CMTYPE_NONCM = 1,
	CMTYPE_CM = 2,
	CMTYPE_MAX
};

static const char* CMTypeToString(CMType cmtype) {
	if (cmtype == CMTYPE_CM) return "CM";
	if (cmtype == CMTYPE_NONCM) return "本編";
	return "";
}

enum VIDEO_STREAM_FORMAT {
	VS_UNKNOWN,
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
	VIDEO_STREAM_FORMAT format;
	int width, height; // フレームの横縦
  int displayWidth, displayHeight; // フレームの内表示領域の縦横（表示領域中心オフセットはゼロと仮定）
	int sarWidth, sarHeight; // アスペクト比
	int frameRateNum, frameRateDenom; // フレームレート
	uint8_t colorPrimaries, transferCharacteristics, colorSpace; // カラースペース
	bool progressive, fixedFrameRate;

	bool isEmpty() const {
		return width == 0;
	}

	bool isSARUnspecified() const {
		return sarWidth == 0 && sarHeight == 1;
	}

	void mulDivFps(int mul, int div) {
		frameRateNum *= mul;
		frameRateDenom *= div;
		int g = gcd(frameRateNum, frameRateDenom);
		frameRateNum /= g;
		frameRateDenom /= g;
	}

  void getDAR(int& darWidth, int& darHeight) const {
    darWidth = displayWidth * sarWidth;
    darHeight = displayHeight * sarHeight;
    int denom = gcd(darWidth, darHeight);
    darWidth /= denom;
    darHeight /= denom;
  }

	bool operator==(const VideoFormat& o) const {
		return (width == o.width && height == o.height
      && displayWidth == o.displayWidth && displayHeight == o.displayHeight
      && sarWidth == o.sarWidth && sarHeight == o.sarHeight
			&& frameRateNum == o.frameRateNum && frameRateDenom == o.frameRateDenom
			&& progressive == o.progressive);
	}
	bool operator!=(const VideoFormat& o) const {
		return !(*this == o);
	}

private:
	static int gcd(int u, int v) {
		int r;
		while (0 != v) {
			r = u % v;
			u = v;
			v = r;
		}
		return u;
	}
};

struct VideoFrameInfo {
	int64_t PTS, DTS;
	// MPEG2の場合 sequence header がある
	// H264の場合 SPS がある
	bool isGopStart;
	bool progressive;
	PICTURE_TYPE pic;
	FRAME_TYPE type; // 使わないけど参考情報
	int codedDataSize; // 映像ビットストリームにおけるバイト数
	VideoFormat format;
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
	case AUDIO_2LANG: return "デュアルモノ";
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

enum NicoJKType {
	NICOJK_720S = 0,
	NICOJK_720T = 1,
	NICOJK_1080S = 2,
	NICOJK_1080T = 3,
	NICOJK_MAX
};

#include "avisynth.h"
#include "utvideo/utvideo.h"
#include "utvideo/Codec.h"

static void DeleteScriptEnvironment(IScriptEnvironment2* env) {
	if (env) env->DeleteScriptEnvironment();
}

typedef std::unique_ptr<IScriptEnvironment2, decltype(&DeleteScriptEnvironment)> ScriptEnvironmentPointer;

static ScriptEnvironmentPointer make_unique_ptr(IScriptEnvironment2* env) {
	return ScriptEnvironmentPointer(env, DeleteScriptEnvironment);
}

static void DeleteUtVideoCodec(CCodec* codec) {
	if (codec) CCodec::DeleteInstance(codec);
}

typedef std::unique_ptr<CCodec, decltype(&DeleteUtVideoCodec)> CCodecPointer;

static CCodecPointer make_unique_ptr(CCodec* codec) {
	return CCodecPointer(codec, DeleteUtVideoCodec);
}

// 1インスタンスは書き込みor読み込みのどちらか一方しか使えない
class LosslessVideoFile : AMTObject
{
	struct LosslessFileHeader {
		int magic;
		int version;
		int width;
		int height;
	};

	File file;
	LosslessFileHeader fh;
	std::vector<uint8_t> extra;
	std::vector<int> framesizes;
	std::vector<int64_t> offsets;

	int current;

public:
	LosslessVideoFile(AMTContext& ctx, const std::string& filepath, const char* mode)
		: AMTObject(ctx)
		, file(filepath, mode)
		, current()
	{
	}

	void writeHeader(int width, int height, int numframes, const std::vector<uint8_t>& extra)
	{
		fh.magic = 0x012345;
		fh.version = 1;
		fh.width = width;
		fh.height = height;
		framesizes.resize(numframes);
		offsets.resize(numframes);

		file.writeValue(fh);
		file.writeArray(extra);
		file.writeArray(framesizes);
		offsets[0] = file.pos();
	}

	void readHeader()
	{
		fh = file.readValue<LosslessFileHeader>();
		extra = file.readArray<uint8_t>();
		framesizes = file.readArray<int>();
		offsets.resize(framesizes.size());
		offsets[0] = file.pos();

		for (int i = 1; i < (int)framesizes.size(); ++i) {
			offsets[i] = offsets[i - 1] + framesizes[i - 1];
		}
	}

	int getWidth() const { return fh.width; }
	int getHeight() const { return fh.height; }
	int getNumFrames() const { return (int)framesizes.size(); }
	const std::vector<uint8_t>& getExtra() const { return extra; }

	void writeFrame(const uint8_t* data, int len)
	{
		int numframes = (int)framesizes.size();
		int n = current++;
		if (n >= numframes) {
			THROWF(InvalidOperationException, "[LosslessVideoFile] attempt to write frame more than specified num frames");
		}

		if (n > 0) {
			offsets[n] = offsets[n - 1] + framesizes[n - 1];
		}
		framesizes[n] = len;

		// データを書き込む
		file.seek(offsets[n], SEEK_SET);
		file.write(MemoryChunk((uint8_t*)data, len));

		// 長さを書き込む
		file.seek(offsets[0] - sizeof(int) * (numframes - n), SEEK_SET);
		file.writeValue(len);
	}

	int64_t readFrame(int n, uint8_t* data)
	{
		file.seek(offsets[n], SEEK_SET);
		file.read(MemoryChunk(data, framesizes[n]));
		return framesizes[n];
	}
};

static void CopyYV12(uint8_t* dst, PVideoFrame& frame, int width, int height)
{
	const uint8_t* srcY = frame->GetReadPtr(PLANAR_Y);
	const uint8_t* srcU = frame->GetReadPtr(PLANAR_U);
	const uint8_t* srcV = frame->GetReadPtr(PLANAR_V);
	int pitchY = frame->GetPitch(PLANAR_Y);
	int pitchUV = frame->GetPitch(PLANAR_U);
	int widthUV = width >> 1;
	int heightUV = height >> 1;

	uint8_t* dstp = dst;
	for (int y = 0; y < height; ++y) {
		memcpy(dstp, &srcY[y * pitchY], width);
		dstp += width;
	}
	for (int y = 0; y < heightUV; ++y) {
		memcpy(dstp, &srcU[y * pitchUV], widthUV);
		dstp += widthUV;
	}
	for (int y = 0; y < heightUV; ++y) {
		memcpy(dstp, &srcV[y * pitchUV], widthUV);
		dstp += widthUV;
	}
}

static void CopyYV12(PVideoFrame& dst, uint8_t* frame, int width, int height)
{
	uint8_t* dstY = dst->GetWritePtr(PLANAR_Y);
	uint8_t* dstU = dst->GetWritePtr(PLANAR_U);
	uint8_t* dstV = dst->GetWritePtr(PLANAR_V);
	int pitchY = dst->GetPitch(PLANAR_Y);
	int pitchUV = dst->GetPitch(PLANAR_U);
	int widthUV = width >> 1;
	int heightUV = height >> 1;

	uint8_t* srcp = frame;
	for (int y = 0; y < height; ++y) {
		memcpy(&dstY[y * pitchY], srcp, width);
		srcp += width;
	}
	for (int y = 0; y < heightUV; ++y) {
		memcpy(&dstU[y * pitchUV], srcp, widthUV);
		srcp += widthUV;
	}
	for (int y = 0; y < heightUV; ++y) {
		memcpy(&dstV[y * pitchUV], srcp, widthUV);
		srcp += widthUV;
	}
}

static void CopyYV12(uint8_t* dst,
	const uint8_t* srcY, const uint8_t* srcU, const uint8_t* srcV,
	int pitchY, int pitchUV, int width, int height)
{
	int widthUV = width >> 1;
	int heightUV = height >> 1;

	uint8_t* dstp = dst;
	for (int y = 0; y < height; ++y) {
		memcpy(dstp, &srcY[y * pitchY], width);
		dstp += width;
	}
	for (int y = 0; y < heightUV; ++y) {
		memcpy(dstp, &srcU[y * pitchUV], widthUV);
		dstp += widthUV;
	}
	for (int y = 0; y < heightUV; ++y) {
		memcpy(dstp, &srcV[y * pitchUV], widthUV);
		dstp += widthUV;
	}
}

void ConcatFiles(const std::vector<std::string>& srcpaths, const std::string& dstpath)
{
	enum { BUF_SIZE = 16 * 1024 * 1024 };
	auto buf = std::unique_ptr<uint8_t[]>(new uint8_t[BUF_SIZE]);
	File dstfile(dstpath, "wb");
	for (int i = 0; i < (int)srcpaths.size(); ++i) {
		File srcfile(srcpaths[i], "rb");
		while (true) {
			size_t readBytes = srcfile.read(MemoryChunk(buf.get(), BUF_SIZE));
			dstfile.write(MemoryChunk(buf.get(), readBytes));
			if (readBytes != BUF_SIZE) break;
		}
	}
}

// BOMありUTF8で書き込む
void WriteUTF8File(const std::string& filename, const std::string& utf8text)
{
	File file(filename, "w");
	uint8_t bom[] = { 0xEF, 0xBB, 0xBF };
	file.write(MemoryChunk(bom, sizeof(bom)));
	file.write(MemoryChunk((uint8_t*)utf8text.data(), utf8text.size()));
}

void WriteUTF8File(const std::string& filename, const std::wstring& text)
{
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
	WriteUTF8File(filename, converter.to_bytes(text));
}

// C API for P/Invoke
extern "C" __declspec(dllexport) AMTContext* AMTContext_Create() { return new AMTContext(); }
extern "C" __declspec(dllexport) void ATMContext_Delete(AMTContext* ptr) { delete ptr; }
extern "C" __declspec(dllexport) const char* AMTContext_GetError(AMTContext* ptr) { return ptr->getError().c_str(); }
