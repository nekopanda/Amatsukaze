/**
* Amatsukaze core utility
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "common.h"

#include <string>

struct Exception {
	virtual ~Exception() { }
	virtual const char* message() const {
		return "No Message ...";
	};
	virtual void raise() const { throw *this;	}
};

#define DEFINE_EXCEPTION(name) \
	struct name : public Exception { \
		name(const char* fmt, ...) { \
			va_list arg; va_start(arg, fmt); \
			size_t length = _vscprintf(fmt, arg); \
			char* buf = new char[length + 1]; \
			vsnprintf_s(buf, length + 1, _TRUNCATE, fmt, arg); \
			mes = buf; delete[] buf; \
			va_end(arg); \
		} \
		virtual const char* message() const { return mes.c_str(); } \
		virtual void raise() const { throw *this;	} \
	private: \
		std::string mes; \
	};

DEFINE_EXCEPTION(EOFException)
DEFINE_EXCEPTION(FormatException)
DEFINE_EXCEPTION(InvalidOperationException)
DEFINE_EXCEPTION(ArgumentException)
DEFINE_EXCEPTION(IOException)
DEFINE_EXCEPTION(RuntimeException)
DEFINE_EXCEPTION(NoLogoException)
DEFINE_EXCEPTION(NoDrcsMapException)
DEFINE_EXCEPTION(AviSynthException)
DEFINE_EXCEPTION(TestException)

#undef DEFINE_EXCEPTION

#define THROW(exception, message) \
	throw_exception_(exception("Exception thrown at %s:%d\r\nMessage: " message, __FILE__, __LINE__))

#define THROWF(exception, fmt, ...) \
	throw_exception_(exception("Exception thrown at %s:%d\r\nMessage: " fmt, __FILE__, __LINE__, __VA_ARGS__))

static void throw_exception_(const Exception& exc)
{
	PRINTF("AMT [error] %s\n", exc.message());
	//MessageBox(NULL, exc.message(), "Amatsukaze Error", MB_OK);
	exc.raise();
}

// コピー禁止オブジェクト
class NonCopyable
{
protected:
	NonCopyable() {}
	~NonCopyable() {} /// protected な非仮想デストラクタ
private:
	NonCopyable(const NonCopyable &);
	NonCopyable& operator=(const NonCopyable &) { }
};

static void DebugPrint(const char* fmt, ...)
{
	va_list argp;
	char buf[1000];
	va_start(argp, fmt);
	_vsnprintf_s(buf, sizeof(buf), fmt, argp);
	va_end(argp);
	OutputDebugString(buf);
}

/** @brief ポインタとサイズのセット */
struct MemoryChunk {

	MemoryChunk() : data(NULL), length(0) { }
	MemoryChunk(uint8_t* data, size_t length) : data(data), length(length) { }

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

/** @brief リングバッファではないがtrimHeadとtrimTailが同じくらい高速なバッファ */
class AutoBuffer {
public:
	AutoBuffer()
		: data_(NULL)
		, capacity_(0)
		, head_(0)
		, tail_(0)
	{ }

	~AutoBuffer() {
		release();
	}

	void add(MemoryChunk mc) {
		ensure(mc.length);
		memcpy(data_ + tail_, mc.data, mc.length);
		tail_ += mc.length;
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

	uint8_t* ptr() const {
		return &data_[head_];
	}

	/** @brief データへ */
	MemoryChunk get() const {
		return MemoryChunk(&data_[head_], size());
	}
	
	/** @brief 追加スペース取得 */
	MemoryChunk space(int at_least = 0) {
		if (at_least > 0) {
			ensure(at_least);
		}
		return MemoryChunk(&data_[tail_], capacity_ - tail_);
	}

	/** @brief 尻をsizeだけ後ろにずらす（その分サイズも増える） */
	void extend(int size) {
		ensure(size);
		tail_ += size;
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

std::string GetFullPath(const std::string& path)
{
	char buf[MAX_PATH];
	int sz = GetFullPathName(path.c_str(), sizeof(buf), buf, nullptr);
	if (sz >= sizeof(buf)) {
		THROWF(IOException, "パスが長すぎます: %s", path.c_str());
	}
	if (sz == 0) {
		THROWF(IOException, "GetFullPathName()に失敗: %s", path.c_str());
	}
	return buf;
}

class File : NonCopyable
{
public:
	File(const std::string& path, const char* mode) {
		fp_ = _fsopen(path.c_str(), mode, _SH_DENYNO);
		if (fp_ == NULL) {
			THROWF(IOException, "failed to open file %s", GetFullPath(path).c_str());
		}
	}
	~File() {
		fclose(fp_);
	}
	void write(MemoryChunk mc) const {
		if (mc.length == 0) return;
		if (fwrite(mc.data, mc.length, 1, fp_) != 1) {
			THROWF(IOException, "failed to write to file");
		}
	}
	template <typename T>
	void writeValue(T v) const {
		write(MemoryChunk((uint8_t*)&v, sizeof(T)));
	}
	template <typename T>
	void writeArray(const std::vector<T>& arr) const {
		writeValue((int64_t)arr.size());
		auto dataptr = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(arr.data()));
		write(MemoryChunk(dataptr, sizeof(T)*arr.size()));
	}
	void writeString(const std::string& str) const {
		writeValue((int64_t)str.size());
		auto dataptr = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(str.data()));
		write(MemoryChunk(dataptr, sizeof(str[0])*str.size()));
	}
	size_t read(MemoryChunk mc) const {
		if (mc.length == 0) return 0;
		size_t ret = fread(mc.data, 1, mc.length, fp_);
		if (ret == 0 && feof(fp_)) {
			// ファイル終端
			return 0;
		}
		if (ret <= 0) {
			THROWF(IOException, "failed to read from file");
		}
		return ret;
	}
	template <typename T>
	T readValue() const {
		T v;
		if (read(MemoryChunk((uint8_t*)&v, sizeof(T))) != sizeof(T)) {
			THROWF(IOException, "failed to read value from file");
		}
		return v;
	}
	template <typename T>
	std::vector<T> readArray() const {
		size_t len = (size_t)readValue<int64_t>();
		std::vector<T> arr(len);
		if (read(MemoryChunk((uint8_t*)arr.data(), sizeof(T)*len)) != sizeof(T)*len) {
			THROWF(IOException, "failed to read array from file");
		}
		return arr;
	}
	std::string readString() const {
		auto v = readArray<char>();
		return std::string(v.begin(), v.end());
	}
	void flush() const {
		fflush(fp_);
	}
	void seek(int64_t offset, int origin) const {
		if (_fseeki64(fp_, offset, origin) != 0) {
			THROWF(IOException, "failed to seek file");
		}
	}
	int64_t pos() const {
		return _ftelli64(fp_);
	}
	int64_t size() const {
		int64_t cur = _ftelli64(fp_);
		if (cur < 0) {
			THROWF(IOException, "_ftelli64 failed");
		}
		if (_fseeki64(fp_, 0L, SEEK_END) != 0) {
			THROWF(IOException, "failed to seek to end");
		}
		int64_t last = _ftelli64(fp_);
		if (last < 0) {
			THROWF(IOException, "_ftelli64 failed");
		}
		_fseeki64(fp_, cur, SEEK_SET);
		if (_fseeki64(fp_, cur, SEEK_SET) != 0) {
			THROWF(IOException, "failed to seek back to current");
		}
		return last;
	}
	bool getline(std::string& line) {
		enum { BUF_SIZE = 200 };
		char buf[BUF_SIZE];
		line.clear();
		while(1) {
			buf[BUF_SIZE - 2] = 0;
			if (fgets(buf, BUF_SIZE, fp_) == nullptr) {
				return line.size() > 0;
			}
			if (buf[BUF_SIZE - 2] != 0 && buf[BUF_SIZE - 2] != '\n') {
				// まだある
				line.append(buf);
				continue;
			}
			else {
				// 改行文字を取り除く
				size_t len = strlen(buf);
				if (buf[len - 1] == '\n') buf[--len] = 0;
				if (buf[len - 1] == '\r') buf[--len] = 0;
				line.append(buf);
				break;
			}
		}
		return true;
	}
	void writeline(std::string& line) {
		fputs(line.c_str(), fp_);
		fputs("\n", fp_);
	}
	static bool exists(const std::string& path) {
		FILE* fp_ = _fsopen(path.c_str(), "rb", _SH_DENYNO);
		if (fp_) {
			fclose(fp_);
			return true;
		}
		return false;
	}
	static void copy(const std::string& srcpath, const std::string& dstpath) {
		CopyFileA(srcpath.c_str(), dstpath.c_str(), FALSE);
	}
private:
	FILE* fp_;
};

template <typename T>
void WriteArray(const File& file, const std::vector<T>& arr) {
	file.writeValue((int)arr.size());
	for (int i = 0; i < (int)arr.size(); ++i) {
		arr[i].Write(file);
	}
}

template <typename T>
std::vector<T> ReadArray(const File& file) {
	int num = file.readValue<int>();
	std::vector<T> ret(num);
	for (int i = 0; i < num; ++i) {
		ret[i] = T::Read(file);
	}
	return ret;
}

template <typename F>
void WriteGrayBitmap(const std::string& path, int w, int h, F pixels) {

	int stride = (3 * w + 3) & ~3;
	auto buf = std::unique_ptr<uint8_t[]>(new uint8_t[h * stride]);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			uint8_t* ptr = &buf[3 * x + (h - y - 1) * stride];
			ptr[0] = ptr[1] = ptr[2] = pixels(x, y);
		}
	}

	BITMAPINFOHEADER bmiHeader = { 0 };
	bmiHeader.biSize = sizeof(bmiHeader);
	bmiHeader.biWidth = w;
	bmiHeader.biHeight = h;
	bmiHeader.biPlanes = 1;
	bmiHeader.biBitCount = 24;
	bmiHeader.biCompression = BI_RGB;
	bmiHeader.biSizeImage = 0;
	bmiHeader.biXPelsPerMeter = 1;
	bmiHeader.biYPelsPerMeter = 1;
	bmiHeader.biClrUsed = 0;
	bmiHeader.biClrImportant = 0;

	BITMAPFILEHEADER bmfHeader = { 0 };
	bmfHeader.bfType = 0x4D42;
	bmfHeader.bfOffBits = sizeof(bmfHeader) + sizeof(bmiHeader);
	bmfHeader.bfSize = bmfHeader.bfOffBits + bmiHeader.biSizeImage;

	File file(path, "wb");
	file.write(MemoryChunk((uint8_t*)&bmfHeader, sizeof(bmfHeader)));
	file.write(MemoryChunk((uint8_t*)&bmiHeader, sizeof(bmiHeader)));
	file.write(MemoryChunk(buf.get(), h * stride));
}
