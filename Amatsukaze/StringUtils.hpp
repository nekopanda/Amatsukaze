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

#include "CoreUtils.hpp"

namespace string_internal {
	template <typename T> T MakeArg(T value) { return value; }
	template <typename T> T MakeArgW(T value) { return value; }

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

	const char* MakeArg(const char* value) { return value; }
	const char* MakeArg(const wchar_t* value) { return to_string(value).data(); }
	const char* MakeArg(const std::string& value) { return value.c_str(); }
	const char* MakeArg(const std::wstring& value) { return to_string(value).data(); }

	const wchar_t* MakeArgW(const char* value) { return to_wstring(value).data(); }
	const wchar_t* MakeArgW(const wchar_t* value) { return value; }
	const wchar_t* MakeArgW(const std::string& value) { return to_wstring(value).data(); }
	const wchar_t* MakeArgW(const std::wstring& value) { return value.c_str(); }

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

template <typename ... Args>
std::string StringFormat(const char* fmt, const Args& ... args)
{
	std::string str;
	size_t size = _scprintf(fmt, string_internal::MakeArg(args) ...);
	if (size > 0)
	{
		str.reserve(size + 1); // null終端を足す
		str.resize(size);
		snprintf(&str[0], str.size() + 1, fmt, string_internal::MakeArg(args) ...);
	}
	return str;
}

template <typename ... Args>
std::wstring StringFormat(const wchar_t* fmt, const Args& ... args)
{
	std::wstring str;
	size_t size = _scwprintf(fmt, string_internal::MakeArgW(args) ...);
	if (size > 0)
	{
		str.reserve(size + 1); // null終端を足す
		str.resize(size);
		swprintf(&str[0], str.size() + 1, fmt, string_internal::MakeArgW(args) ...);
	}
	return str;
}

class StringBuilder : public string_internal::StringBuilderBase
{
public:
	template <typename ... Args>
	StringBuilder& append(const char* const fmt, Args const & ... args)
	{
		size_t size = _scprintf(fmt, string_internal::MakeArg(args) ...);
		if (size > 0)
		{
			auto mc = buffer.space((int)((size + 1) * sizeof(char))); // null終端を足す
			snprintf(reinterpret_cast<char*>(mc.data), mc.length / sizeof(char), 
				fmt, string_internal::MakeArg(args) ...);
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
		size_t size = _scwprintf(fmt, string_internal::MakeArgW(args) ...);
		if (size > 0)
		{
			auto mc = buffer.space((int)((size + 1) * sizeof(wchar_t))); // null終端を足す
			swprintf(reinterpret_cast<wchar_t*>(mc.data), mc.length / sizeof(wchar_t),
				fmt, string_internal::MakeArgW(args) ...);
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

class UTF8Converter
{
public:
	UTF8Converter() : searchIdx(0) { }

	void AddBytes(MemoryChunk utf8) {
		buffer.add(utf8);
		while (SearchLineBreak());
	}

	void Flush() {
		if (buffer.size() > 0) {
			OnTextLine(Utf8ToWstring(buffer.ptr(), (int)buffer.size()));
			buffer.clear();
		}
	}

protected:
	AutoBuffer buffer;
	int searchIdx;

	virtual void OnTextLine(const std::vector<char>& line) = 0;

	std::vector<char> Utf8ToWstring(const uint8_t* ptr, int sz) {
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

	bool SearchLineBreak() {
		const uint8_t* ptr = buffer.ptr();
		for (int i = searchIdx; i < buffer.size(); ++i) {
			if (ptr[i] == '\n') {
				int len = i;
				if (len > 0 && ptr[len - 1] == '\r') --len;
				OnTextLine(Utf8ToWstring(ptr, len));
				buffer.trimHead(i + 1);
				searchIdx = 0;
				return true;
			}
		}
		searchIdx = (int)buffer.size();
		return false;
	}
};

std::vector<std::string> split(const std::string& text, const char* delimiters)
{
	std::vector<std::string> ret;
	std::vector<char> text_(text.begin(), text.end());
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
