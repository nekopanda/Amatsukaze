#pragma once

#include <Windows.h>
#include <process.h>

#include <vector>

#include "common.h"

class CriticalSection
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
};

class Semaphore
{
public:
  // TODO:
};

template <typename T>
class DataPumpThread
{
public:
  DataPumpThread(size_t maximum) {
    semaphore_ = CreateSemaphore(NULL, 0, 0x10000, NULL);
    if (semaphore_ == NULL) {
      throw "failed to create semaphore ...";
    }
    if (_beginthread(pump_thread_, 0, this) == -1) {
      CloseHandle(semaphore_);
      semaphore_ = NULL;
      throw "failed to begin pump thread ...";
    }

    maximum_ = maximum;
  }

  ~DataPumpThread() {
    CloseHandle(semaphore_);
  }

  void put(const T& data, size_t amount)
  {
    auto& lock = with(critical_section_);
    while (current_ >= maximum_) {
      critical_section_.exit();
      WaitForSingleObject(semaphore_, INFINITE);
      critical_section_.enter();
    }
    if (data_.size() == 0) {
      ReleaseSemaphore(semaphore_, 1, NULL);
    }
    data_.push_back(std::make_pair(amount, data));
    current_ += amount;
  }

private:
  CriticalSection critical_section_;
  HANDLE semaphore_;

  std::vector<std::pair<size_t, T>> data_;

  size_t maximum_;
  size_t current_;

  static void pump_thread_(void* arg)
  {
    DataPumpThread* this_ = (DataPumpThread*)arg;
    this_->pump_thread();
  }
  void pump_thread()
  {
    while (true) {
      auto& lock = with(critical_section_);
      while (data_.size() == 0) {
        critical_section_.exit();
        WaitForSingleObject(semaphore_, INFINITE);
        critical_section_.enter();
      }
    }
  }
};


