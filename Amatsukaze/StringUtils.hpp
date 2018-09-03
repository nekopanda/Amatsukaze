/**
* String Utility
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
#include <cassert>
#include <vector>
#include <direct.h>

#ifdef _MSC_VER
typedef std::wstring tstring;
typedef wchar_t tchar;
#define PRITSTR "ls"
#define _T(s) L ## s
#else
typedef std::string tstring;
typedef char tchar;
#define PRITSTR "s"
#define _T(s) s
#endif

template <typename ... Args>
int sscanfT(const wchar_t* buffer, const wchar_t* format, const Args& ... args) {
  return swscanf_s(buffer, format, args ...);
}
template <typename ... Args>
int sscanfT(const char* buffer, const char* format, const Args& ... args) {
  return sscanf(buffer, format, args ...);
}

size_t strlenT(const wchar_t* string) {
  return wcslen(string);
}
size_t strlenT(const char* string) {
  return strlen(string);
}

int stricmpT(const wchar_t* string1, const wchar_t* string2) {
  return _wcsicmp(string1, string2);
}
int stricmpT(const char* string1, const char* string2) {
  return _stricmp(string1, string2);
}

int rmdirT(const wchar_t* dirname) {
  return _wrmdir(dirname);
}
int rmdirT(const char* dirname) {
  return _rmdir(dirname);
}

int mkdirT(const wchar_t* dirname) {
  return _wmkdir(dirname);
}
int mkdirT(const char* dirname) {
  return _mkdir(dirname);
}

int removeT(const wchar_t* dirname) {
  return _wremove(dirname);
}
int removeT(const char* dirname) {
  return remove(dirname);
}

FILE* fsopenT(const wchar_t* FileName, const wchar_t* Mode, int ShFlag) {
  return _wfsopen(FileName, Mode, ShFlag);
}

FILE* fsopenT(const char* FileName, const char* Mode, int ShFlag) {
  return _fsopen(FileName, Mode, ShFlag);
}


namespace string_internal {

	// null終端があるので
	static std::vector<char> to_string(std::wstring str) {
		if (str.size() == 0) {
			return std::vector<char>(1);
		}
		int dstlen = WideCharToMultiByte(
			CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0, NULL, NULL);
		std::vector<char> ret(dstlen + 1);
		WideCharToMultiByte(CP_ACP, 0,
			str.c_str(), (int)str.size(), ret.data(), (int)ret.size(), NULL, NULL);
		ret.back() = 0; // null terminate
		return ret;
	}
	static std::vector<wchar_t> to_wstring(std::string str) {
		if (str.size() == 0) {
			return std::vector<wchar_t>(1);
		}
		int dstlen = MultiByteToWideChar(
			CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0);
		std::vector<wchar_t> ret(dstlen + 1);
		MultiByteToWideChar(CP_ACP, 0,
			str.c_str(), (int)str.size(), ret.data(), (int)ret.size());
		ret.back() = 0; // null terminate
		return ret;
	}

  class MakeArgContext {
    std::vector<std::vector<char>> args;
  public:
    template <typename T> const char* arg(const T& value) {
      args.push_back(to_string(value));
      return args.back().data();
    }
  };

  class MakeArgWContext {
    std::vector<std::vector<wchar_t>> args;
  public:
    template <typename T> const wchar_t* arg(const T& value) {
      args.push_back(to_wstring(value));
      return args.back().data();
    }
  };

  template <typename T> T MakeArg(MakeArgContext& ctx, T value) { return value; }
  template <typename T> T MakeArgW(MakeArgWContext& ctx, T value) { return value; }

	const char* MakeArg(MakeArgContext& ctx, const char* value) { return value; }
	const char* MakeArg(MakeArgContext& ctx, const wchar_t* value) { return ctx.arg(value); }
	const char* MakeArg(MakeArgContext& ctx, const std::string& value) { return value.c_str(); }
	const char* MakeArg(MakeArgContext& ctx, const std::wstring& value) { return ctx.arg(value); }

	const wchar_t* MakeArgW(MakeArgWContext& ctx, const char* value) { return ctx.arg(value); }
	const wchar_t* MakeArgW(MakeArgWContext& ctx, const wchar_t* value) { return value; }
	const wchar_t* MakeArgW(MakeArgWContext& ctx, const std::string& value) { return ctx.arg(value); }
	const wchar_t* MakeArgW(MakeArgWContext& ctx, const std::wstring& value) { return value.c_str(); }

	class StringBuilderBase {
	public:
		StringBuilderBase() { }

		MemoryChunk getMC() {
			return buffer.get();
		}

		void clear() {
			buffer.clear();
		}

	protected:
		AutoBuffer buffer;
	};
}

static std::string to_string(const std::wstring& str) {
  std::vector<char> ret = string_internal::to_string(str);
  return std::string(ret.begin(), ret.end());
}

static std::string to_string(const std::string& str) {
  return str;
}

static std::wstring to_wstring(const std::wstring& str) {
  return str;
}

static std::wstring to_wstring(const std::string& str) {
  std::vector<wchar_t> ret = string_internal::to_wstring(str);
  return std::wstring(ret.begin(), ret.end());
}

#ifdef _MSC_VER
static std::wstring to_tstring(const std::wstring& str) {
  return str;
}

static std::wstring to_tstring(const std::string& str) {
  return to_wstring(str);
}
#else
static std::string to_tstring(const std::wstring& str) {
  return to_string(str);
}

static std::string to_tstring(const std::string& str) {
  return str;
}
#endif

template <typename ... Args>
std::string StringFormat(const char* fmt, const Args& ... args)
{
	std::string str;
  string_internal::MakeArgContext ctx;
	size_t size = _scprintf(fmt, string_internal::MakeArg(ctx, args) ...);
	if (size > 0)
	{
		str.reserve(size + 1); // null終端を足す
		str.resize(size);
		snprintf(&str[0], str.size() + 1, fmt, string_internal::MakeArg(ctx, args) ...);
	}
	return str;
}

template <typename ... Args>
std::wstring StringFormat(const wchar_t* fmt, const Args& ... args)
{
	std::wstring str;
  string_internal::MakeArgWContext ctx;
	size_t size = _scwprintf(fmt, string_internal::MakeArgW(ctx, args) ...);
	if (size > 0)
	{
		str.reserve(size + 1); // null終端を足す
		str.resize(size);
		swprintf(&str[0], str.size() + 1, fmt, string_internal::MakeArgW(ctx, args) ...);
	}
	return str;
}

class StringBuilder : public string_internal::StringBuilderBase
{
public:
	template <typename ... Args>
	StringBuilder& append(const char* const fmt, Args const & ... args)
	{
    string_internal::MakeArgContext ctx;
		size_t size = _scprintf(fmt, string_internal::MakeArg(ctx, args) ...);
		if (size > 0)
		{
			auto mc = buffer.space((int)((size + 1) * sizeof(char))); // null終端を足す
			snprintf(reinterpret_cast<char*>(mc.data), mc.length / sizeof(char), 
				fmt, string_internal::MakeArg(ctx, args) ...);
		}
		buffer.extend((int)(size * sizeof(char)));
		return *this;
	}

	std::string str() const {
		auto mc = buffer.get();
		return std::string(
			reinterpret_cast<const char*>(mc.data),
			reinterpret_cast<const char*>(mc.data + mc.length));
	}
};

class StringBuilderW : public string_internal::StringBuilderBase
{
public:
	template <typename ... Args>
	StringBuilderW& append(const wchar_t* const fmt, Args const & ... args)
	{
    string_internal::MakeArgWContext ctx;
		size_t size = _scwprintf(fmt, string_internal::MakeArgW(ctx, args) ...);
		if (size > 0)
		{
			auto mc = buffer.space((int)((size + 1) * sizeof(wchar_t))); // null終端を足す
			swprintf(reinterpret_cast<wchar_t*>(mc.data), mc.length / sizeof(wchar_t),
				fmt, string_internal::MakeArgW(ctx, args) ...);
		}
		buffer.extend((int)(size * sizeof(wchar_t)));
		return *this;
	}

	std::wstring str() const {
		auto mc = buffer.get();
		return std::wstring(
			reinterpret_cast<const wchar_t*>(mc.data),
			reinterpret_cast<const wchar_t*>(mc.data + mc.length));
	}
};

#ifdef _MSC_VER
typedef StringBuilderW StringBuilderT;
#else
typedef StringBuilder StringBuilderT;
#endif

class StringLiner
{
public:
  StringLiner() : searchIdx(0) { }

  void AddBytes(MemoryChunk utf8) {
    buffer.add(utf8);
    while (SearchLineBreak());
  }

  void Flush() {
    if (buffer.size() > 0) {
      OnTextLine(buffer.ptr(), (int)buffer.size(), 0);
      buffer.clear();
    }
  }

protected:
  AutoBuffer buffer;
  int searchIdx;

  virtual void OnTextLine(const uint8_t* ptr, int len, int brlen) = 0;

  bool SearchLineBreak() {
    const uint8_t* ptr = buffer.ptr();
    for (int i = searchIdx; i < buffer.size(); ++i) {
      if (ptr[i] == '\n') {
        int len = i;
        int brlen = 1;
        if (len > 0 && ptr[len - 1] == '\r') {
          --len; ++brlen;
        }
        OnTextLine(ptr, len, brlen);
        buffer.trimHead(i + 1);
        searchIdx = 0;
        return true;
      }
    }
    searchIdx = (int)buffer.size();
    return false;
  }
};

std::vector<char> utf8ToString(const uint8_t* ptr, int sz) {
  int dstlen = MultiByteToWideChar(
    CP_UTF8, 0, (const char*)ptr, sz, nullptr, 0);
  std::vector<wchar_t> w(dstlen);
  MultiByteToWideChar(
    CP_UTF8, 0, (const char*)ptr, sz, w.data(), (int)w.size());
  dstlen = WideCharToMultiByte(
    CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::vector<char> ret(dstlen);
  WideCharToMultiByte(CP_ACP, 0,
    w.data(), (int)w.size(), ret.data(), (int)ret.size(), nullptr, nullptr);
  return ret;
}

template <typename tchar>
std::vector<std::basic_string<tchar>> split(const std::basic_string<tchar>& text, const tchar* delimiters)
{
	std::vector<std::basic_string<tchar>> ret;
	std::vector<tchar> text_(text.begin(), text.end());
	text_.push_back(0); // null terminate
	char* ctx;
	ret.emplace_back(strtok_s(text_.data(), delimiters, &ctx));
	while(1) {
		const char* tp = strtok_s(NULL, delimiters, &ctx);
		if (tp == nullptr) break;
		ret.emplace_back(tp);
	}
	return ret;
}

bool starts_with(const std::wstring& str, const std::wstring& test) {
	return str.compare(0, test.size(), test) == 0;
}
bool starts_with(const std::string& str, const std::string& test) {
  return str.compare(0, test.size(), test) == 0;
}

bool ends_with(const tstring & value, const tstring & ending)
{
  if (ending.size() > value.size()) return false;
  return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

