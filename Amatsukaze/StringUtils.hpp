#pragma once

#include <string>
#include <cassert>
#include <vector>

#include "CoreUtils.hpp"

namespace string_internal {
	template <typename T> T MakeArg(T value) { return value; }
	template <typename T> T MakeArgW(T value) { return value; }

	static std::vector<char> to_string(std::wstring str) {
		if (str.size() == 0) {
			return std::vector<char>();
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
			return std::vector<wchar_t>();
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

	template <typename ... Args>
	int SPrintF(char * const buffer, size_t const bufferCount,
		char const * const fmt, Args const & ... args) noexcept
	{
		int const result = snprintf(
			buffer, bufferCount, fmt, MakeArg(args) ...);
		assert(-1 != result);
		return result;
	}

	template <typename ... Args>
	int SPrintF(wchar_t * const buffer, size_t const bufferCount,
		wchar_t const * const fmt, Args const & ... args) noexcept
	{
		int const result = swprintf(
			buffer, bufferCount, fmt, MakeArgW(args) ...);
		assert(-1 != result);
		return result;
	}

	template <typename T>
	class StringBuilderBase {
	public:
		StringBuilderBase() { }

		template <typename ... Args>
		StringBuilderBase& append(const T* const fmt, Args const & ... args)
		{
			auto mc = buffer.space();
			size_t size = SPrintF(
				reinterpret_cast<T*>(mc.data), mc.length / sizeof(T) + 1, fmt, args ...);
			if (size > mc.length / sizeof(T))
			{
				mc = buffer.space((int)(size * sizeof(T)));
				size = SPrintF(
					reinterpret_cast<T*>(mc.data), mc.length / sizeof(T) + 1, fmt, args ...);
			}
			buffer.extend((int)(size * sizeof(T)));
			return *this;
		}

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

template <typename T, typename ... Args>
std::basic_string<T> StringFormat(const T* fmt, const Args& ... args)
{
	std::basic_string<T> str;
	size_t size = string_internal::SPrintF(nullptr, 0, fmt, args ...);
	if (size > 0)
	{
		str.resize(size);
		string_internal::SPrintF(
			&str[0], str.size() + 1, fmt, args ...);
	}
	return str;
}

class StringBuilder : public string_internal::StringBuilderBase<char>
{
public:
	std::string str() const {
		auto mc = buffer.get();
		return std::string(
			reinterpret_cast<const char*>(mc.data),
			reinterpret_cast<const char*>(mc.data + mc.length));
	}
};

class StringBuilderW : public string_internal::StringBuilderBase<wchar_t>
{
public:
	std::wstring str() const {
		auto mc = buffer.get();
		return std::wstring(
			reinterpret_cast<const wchar_t*>(mc.data),
			reinterpret_cast<const wchar_t*>(mc.data + mc.length));
	}
};
