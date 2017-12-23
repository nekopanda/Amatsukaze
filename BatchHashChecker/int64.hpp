#pragma once

#include "common.h"

namespace utl {

class _Int64Ptr;

class int64
{
private:
	LARGE_INTEGER data;
public:
	int64() { }
	int64(const __int64 qw) { data.QuadPart = qw; }
	int64(const DWORD high, const DWORD low)
	{
		data.HighPart = high;
		data.LowPart = low;
	}
	int64(const LARGE_INTEGER li) { data.QuadPart = li.QuadPart; }
	// キャスト
	operator __int64() { return data.QuadPart; }
	operator LARGE_INTEGER() { return data; }
	_Int64Ptr operator&();
	// 単項演算子
	int64& operator=(const __int64 qw)
	{
		data.QuadPart = qw;
		return *this;
	}
	int64 operator~() { return int64(~data.QuadPart); }
	int64 operator!() { return int64(!data.QuadPart); }
	int64& operator++()
	{
		++data.QuadPart;
		return *this;
	}
	int64& operator--()
	{
		--data.QuadPart;
		return *this;
	}
	int64 operator++(int)
	{
		int64 old(data.QuadPart++);
		return old;
	}
	int64 operator--(int)
	{
		int64 old(data.QuadPart--);
		return old;
	}
	void operator+=(const __int64 other) { data.QuadPart += other; }
	void operator-=(const __int64 other) { data.QuadPart -= other; }
	void operator*=(const __int64 other) { data.QuadPart *= other; }
	void operator/=(const __int64 other) { data.QuadPart /= other; }
	// ２項演算子
	/*
	__int64 operator + (const __int64 other) const { return data.QuadPart + other; }
	__int64 operator - (const __int64 other) const { return data.QuadPart - other; }
	__int64 operator * (const __int64 other) const { return data.QuadPart * other; }
	__int64 operator / (const __int64 other) const { return data.QuadPart / other; }
	*/
	// メソッド
	DWORD HighPart() { return data.HighPart; }
	const DWORD HighPart() const { return data.HighPart; }
	DWORD LowPart() { return data.LowPart; }
	const DWORD LowPart() const { return data.LowPart; }

	friend _Int64Ptr;
};

// ポインタも処理したいので定義
class _Int64Ptr
{
private:
	int64* ptr;
public:
	_Int64Ptr(int64* p) : ptr(p) { }
	// キャスト
	operator __int64*() { return &ptr->data.QuadPart; }
	operator LARGE_INTEGER*() { return &ptr->data; }
	operator int64*() { return ptr; }
};

inline _Int64Ptr int64::operator&() { return _Int64Ptr(this); }

BOOL ReadFile64(HANDLE hFile, LPVOID lpBuffer, __int64 nNumberOfBytesToRead, __int64* lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
	__int64 totalRead = 0;
	DWORD dwRead;
	BYTE* bufPtr = (BYTE*)lpBuffer;
	for (; totalRead < nNumberOfBytesToRead; ) {
		DWORD nowRead = (DWORD)std::min(nNumberOfBytesToRead - totalRead, 0x80000000LL);
		if (!ReadFile(hFile, &bufPtr[totalRead], nowRead, &dwRead, lpOverlapped)) {
			return false;
		}
		totalRead += dwRead;
		if (dwRead < nowRead) {
			break;
		}
	}
	*lpNumberOfBytesRead = totalRead;
	return true;
}
BOOL WriteFile64(HANDLE hFile, LPVOID lpBuffer, __int64 nNumberOfBytesToWrite, __int64* lpNumberOfBytesWrite, LPOVERLAPPED lpOverlapped)
{
	__int64 totalWrite = 0;
	DWORD dwWrite;
	BYTE* bufPtr = (BYTE*)lpBuffer;
	for (; totalWrite < nNumberOfBytesToWrite; ) {
		DWORD nowWrite = (DWORD)std::min(nNumberOfBytesToWrite - totalWrite, 0x80000000LL);
		if (!WriteFile(hFile, &bufPtr[totalWrite], nowWrite, &dwWrite, lpOverlapped)) {
			return false;
		}
		totalWrite += dwWrite;
		if (dwWrite < nowWrite) {
			break;
		}
	}
	*lpNumberOfBytesWrite = totalWrite;
	return true;
}

}
