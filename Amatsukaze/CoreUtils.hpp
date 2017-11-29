/**
* Amatsukaze core utility
* Copyright (c) 2017 Nekopanda
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

#undef DEFINE_EXCEPTION

#define THROW(exception, message) \
  throw_exception_(exception("Exception thrown at %s:%d\r\nMessage: " message, __FILE__, __LINE__))

#define THROWF(exception, fmt, ...) \
  throw_exception_(exception("Exception thrown at %s:%d\r\nMessage: " fmt, __FILE__, __LINE__, __VA_ARGS__))

static void throw_exception_(const Exception& exc)
{
	PRINTF("%s\n", exc.message());
	MessageBox(NULL, exc.message(), "Amatsukaze Error", MB_OK);
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

// with idiom サポート //

template <typename T>
class WithHolder
{
public:
  WithHolder(T& obj) : obj_(obj) {
    obj_.enter();
  }
  ~WithHolder() {
    obj_.exit();
  }
private:
  T& obj_;
};

template <typename T>
WithHolder<T> with(T& obj) {
  return WithHolder<T>(obj);
}

class CondWait;

class CriticalSection : NonCopyable
{
public:
  CriticalSection() {
    InitializeCriticalSection(&critical_section_);
  }
  ~CriticalSection() {
    DeleteCriticalSection(&critical_section_);
  }
  void enter() {
    EnterCriticalSection(&critical_section_);
  }
  void exit() {
    LeaveCriticalSection(&critical_section_);
  }
private:
  CRITICAL_SECTION critical_section_;

  friend CondWait;
};

class CondWait : NonCopyable
{
public:
  CondWait()
    : cond_val_(CONDITION_VARIABLE_INIT)
  { }
  void wait(CriticalSection& cs) {
    SleepConditionVariableCS(&cond_val_, &cs.critical_section_, INFINITE);
  }
  void signal() {
    WakeConditionVariable(&cond_val_);
  }
  void broadcast() {
    WakeAllConditionVariable(&cond_val_);
  }
private:
  CONDITION_VARIABLE cond_val_;
};

class Semaphore : NonCopyable
{
public:
  Semaphore(int initial_count)
  {
    handle = CreateSemaphore(NULL, initial_count, INT_MAX, NULL);
    if (handle == NULL) {
      THROW(RuntimeException, "failed to create semaphore");
    }
  }
  ~Semaphore()
  {
    CloseHandle(handle);
    handle = NULL;
  }
  void enter() {
    if (WaitForSingleObject(handle, INFINITE) == WAIT_FAILED) {
      THROW(RuntimeException, "failed to wait semaphore");
    }
  }
  void exit() {
    if (ReleaseSemaphore(handle, 1, NULL) == 0) {
      THROW(RuntimeException, "failed to release semaphore");
    }
  }
private:
  HANDLE handle;
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
	static bool exists(const std::string& path) {
		FILE* fp_ = _fsopen(path.c_str(), "rb", _SH_DENYNO);
		if (fp_) {
			fclose(fp_);
			return true;
		}
		return false;
	}
private:
	FILE* fp_;
};
