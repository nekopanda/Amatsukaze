#pragma once

#include "common.h"

struct Exception {
  virtual ~Exception() { }
  virtual const char* message() const {
    return "No Message ...";
  };
};

#define DEFINE_EXCEPTION(name) \
	struct name : public Exception { \
		name() { buf[0] = 0; } \
		name(const char* fmt, ...) { \
			va_list arg; va_start(arg, fmt); \
			vsnprintf_s(buf, sizeof(buf), fmt, arg); \
			va_end(arg); \
		} \
		virtual const char* message() const { return buf; } \
	private: \
		char buf[300]; \
	};

DEFINE_EXCEPTION(EOFException)
DEFINE_EXCEPTION(FormatException)
DEFINE_EXCEPTION(InvalidOperationException)
DEFINE_EXCEPTION(IOException)
DEFINE_EXCEPTION(RuntimeException)

#undef DEFINE_EXCEPTION

#define THROW(exception, message) \
  throw_exception_(exception("Exception thrown at %s:%d\r\nMessage: " message, __FILE__, __LINE__))

#define THROWF(exception, fmt, ...) \
  throw_exception_(exception("Exception thrown at %s:%d\r\nMessage: " fmt, __FILE__, __LINE__, __VA_ARGS__))

static void throw_exception_(const Exception& exc)
{
  printf("%s\n", exc.message());
  throw exc;
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
  CONDITION_VARIABLE cond_val_ = CONDITION_VARIABLE_INIT;
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
