#pragma once

#include "common.h"

#include <vector>
#include <memory>
#include <algorithm>

#include "openssl/sha.h"

#include "kexception.hpp"
#include "int64.hpp"
#include "fileutils.hpp"

#define SHA_CTX SHA512_CTX
#define SHA_Init SHA512_Init
#define SHA_Update SHA512_Update
#define SHA_Final SHA512_Final
#define HASH_LENGTH SHA512_DIGEST_LENGTH

namespace hashchecker {

using namespace utl;

const BYTE HexCharToNum[] = {
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 255, 255, 255, 255, 255, 255,
	255, 10, 11, 12, 13, 14, 15, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 10, 11, 12, 13, 14, 15, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
};

struct IORequest {
	DWORD dwErrorCode;
	DWORD dwNumberOfBytesTransfered;
};

static void CALLBACK ReadComplete(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
{
	IORequest* ioreq = (IORequest*)lpOverlapped->hEvent;
	ioreq->dwErrorCode = dwErrorCode;
	ioreq->dwNumberOfBytesTransfered = dwNumberOfBytesTransfered;
}

class Handle
{
	HANDLE handle;
public:
	Handle(HANDLE handle)
	{
		this->handle = handle;
	}

	~Handle()
	{
		if (handle != NULL && handle != INVALID_HANDLE_VALUE) {
			CloseHandle(handle);
		}
	}

	operator HANDLE() { return handle; }
	HANDLE* operator&() { return &handle; }
	HANDLE get() { return handle; }
};

class StackBuffer
{
public:
	StackBuffer()
		: allocSize(64 * 1024)
		, bufferEnd(0)
		, bufferPos(0)
	{ }

	~StackBuffer()
	{
		Clear();
	}

	void Clear()
	{
		for (int i = 0, end = (int)list.size(); i < end; i++) {
			free(list[i]);
			list[i] = NULL;
		}
		list.clear();
	}

	template<typename PTR_TYPE>
	PTR_TYPE Add(PTR_TYPE p, size_t len)
	{
		if (bufferPos + len >= bufferEnd) {
			size_t allocSize = this->allocSize;
			if (len > allocSize) {
				allocSize = len;
			}
			buffer = (BYTE*)malloc(allocSize);
			if (buffer == NULL) {
				throw KException(MES("リソースエラー"));
			}
			bufferEnd = allocSize;
			list.push_back(buffer);
			bufferPos = 0;
		}

		BYTE* retptr = buffer + bufferPos;
		memcpy(retptr, p, len);
		bufferPos += len;

		return (PTR_TYPE)retptr;
	}

protected:
	std::vector<BYTE*> list;

	BYTE* buffer;
	size_t allocSize;
	size_t bufferEnd;
	size_t bufferPos;
};

class FileHashList
{
public:

	enum CHECK_RESULT { CHECK_OK, HASH_ERROR, FILE_NOT_FOUND, IO_ERROR, WARNING_INVALID_PATH };
	enum HASH_FILE_ERROR { HF_NO_ERROR, HF_NOT_MATCH, HF_NO_HASH };

	class HashCheckHandler
	{
	public:
		virtual void OnResult(LPCWSTR filename, CHECK_RESULT result) { }
		virtual void BufferAllocated(size_t buflen, DWORD sectorSize) { }
		virtual void TotalFileSize(utl::int64 totalFileSize) { }
		virtual void ProgressUpdate(utl::int64 readByte) { }

		virtual void WrongHashFile(HASH_FILE_ERROR he) { }
	};

	FileHashList()
		: DefaultMemSize(32 * 1024 * 1024) // 32 MB
		, PageSize(0)
	{
	}

	void FileHashList::WriteToFile(LPCWSTR path)
	{
		Handle fd = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
		if (fd.get() == INVALID_HANDLE_VALUE) throw IOException(MES("ハッシュファイルを開けません"));

		size_t bufSize = 64 * 1024;
		size_t bufPos = 0;
		std::unique_ptr<BYTE[]> hashfile(new BYTE[bufSize]);
		PBYTE bufptr = hashfile.get();
		BYTE hashStr[HASH_LENGTH * 2];

		SHA_CTX ctx;
		SHA_Init(&ctx);

		for (int64 i = 0, end = filelist.size(); i < end; i++) {

			WriteHex(hashStr, filelist[i].hash, HASH_LENGTH);

			if (bufPos + sizeof(hashStr) + 2 + filelist[i].flen * 4 + 2 >= bufSize) {
				DWORD actualWrite;
				// compute hash
				SHA_Update(&ctx, hashfile.get(), bufPos);
				// write to file
				if (WriteFile(fd, hashfile.get(), (DWORD)bufPos, &actualWrite, NULL) == FALSE)
					throw IOException(MES("ハッシュファイル書き込み失敗"));
				bufPos = 0;
			}

			// write to buffer
			memcpy(bufptr + bufPos, hashStr, sizeof(hashStr));
			bufPos += sizeof(hashStr);
			bufptr[bufPos++] = ' ';
			bufptr[bufPos++] = ' ';
			bufPos += utf16toutf8(bufptr + bufPos, filelist[i].filename, filelist[i].flen);
			bufptr[bufPos++] = '\r';
			bufptr[bufPos++] = '\n';
		}

		DWORD actualWrite;
		// compute hash
		SHA_Update(&ctx, hashfile.get(), bufPos);
		// write to file
		if (WriteFile(fd, hashfile.get(), (DWORD)bufPos, &actualWrite, NULL) == FALSE)
			throw IOException(MES("ハッシュファイル書き込み失敗"));

		// write hash
		SHA_Final(hashfile.get(), &ctx); // reuse buffer
		WriteHex(hashStr, hashfile.get(), HASH_LENGTH);
		if (WriteFile(fd, hashStr, sizeof(hashStr), &actualWrite, NULL) == FALSE)
			throw IOException(MES("ハッシュファイル書き込み失敗"));

	}

	void FileHashList::ReadFromFile(LPCWSTR path, HashCheckHandler* handler)
	{
		Handle fd = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (fd.get() == INVALID_HANDLE_VALUE) {
			if (GetLastError() == ERROR_FILE_NOT_FOUND) {
				throw IOException(MES("ハッシュファイルが見つかりません"));
			}
			throw IOException(MES("ハッシュファイルがファイルを開けません"));
		}

		int64 filesize;
		if (GetFileSizeEx(fd, &filesize) == FALSE) throw IOException(MES("ファイルサイズを取得できません"));

		std::unique_ptr<BYTE[]> hashfile(new BYTE[filesize]);

		DWORD actualRead;
		if (ReadFile(fd, hashfile.get(), (DWORD)filesize, &actualRead, NULL) == FALSE)
			throw IOException(MES("ハッシュファイルの読み込み失敗"));

		BYTE* hfptr = hashfile.get();
		BYTE* hashfileEnd = hfptr + filesize;
		HashData tmpHD = { 0 };
		BYTE tmpHash[HASH_LENGTH];

		// skip BOM
		if (filesize >= 3 && hfptr[0] == 0xEF && hfptr[1] == 0xBB && hfptr[2] == 0xBF) {
			hfptr += 3;
		}

		{
			BYTE* fileHashStr = PreviousRow(hashfile.get() + actualRead, hashfile.get());
			if (fileHashStr + HASH_LENGTH * 2 + 2 < hashfileEnd || fileHashStr + HASH_LENGTH * 2 > hashfileEnd) {
				handler->WrongHashFile(HF_NO_HASH);
			}
			else {
				// chech hash
				BYTE fileHash[HASH_LENGTH];
				SHA_CTX ctx;
				SHA_Init(&ctx);
				// compute hash
				SHA_Update(&ctx, hashfile.get(), fileHashStr - hashfile.get());
				SHA_Final(fileHash, &ctx);
				// read from file
				ReadHex(fileHashStr, tmpHash, HASH_LENGTH);
				// compare
				if (memcmp(tmpHash, fileHash, HASH_LENGTH) != 0)
					handler->WrongHashFile(HF_NOT_MATCH);
			}
		}

		filelist.clear();
		flistb.Clear();

		const int max_path = 32767;
		wchar_t *objname = new wchar_t[max_path];

		while (hfptr < hashfileEnd) {
			if (hfptr + HASH_LENGTH * 2 + 2 >= hashfileEnd) {
				if (hfptr + HASH_LENGTH * 2 > hashfileEnd)
					throw IOException(MES("ハッシュファイルが壊れています"));

				break;
			}

			hfptr = ReadHex(hfptr, tmpHash, HASH_LENGTH);
			tmpHD.hash = flistb.Add(tmpHash, HASH_LENGTH);

			// skip two spaces
			hfptr += 2;

			BYTE* fnameEnd;
			BYTE* next = NextRow(hfptr, hashfileEnd, &fnameEnd);
			tmpHD.flen = (int)utf8toutf16(objname, hfptr, fnameEnd - hfptr);
			tmpHD.filename = (LPCWSTR)flistb.Add(objname, tmpHD.flen * sizeof(wchar_t));
			hfptr = next;

			filelist.push_back(tmpHD);
		}

		delete[] objname;
	}

	void FileHashList::CheckHash(LPCWSTR _path, HashCheckHandler* handler, DWORD blocksize, bool isBenchmark)
	{
		FastHashParam prm = { 0 };
		prm.handler = handler;
		prm.isBenchmark = isBenchmark;

		const int max_path = 32767;
		wchar_t path[MAX_PATH];
		wchar_t *filepath = new wchar_t[max_path];
		int pathLen = (int)wcslen(_path);
		memcpy(path, _path, pathLen * sizeof(wchar_t));
		if (path[pathLen - 1] != L'\\') {
			path[pathLen++] = L'\\';
		}
		path[pathLen] = 0x0000;
		if (pathLen >= 2 && path[0] == L'\\' && path[1] == L'\\') {
			wcscpy_s(filepath, max_path, path);
		}
		else {
			wcscpy_s(filepath, max_path, L"\\\\?\\");
			wcscat_s(filepath, max_path, path);
			pathLen = (int)wcslen(filepath);
		}

		prm.sectorSize = GetSectorSize(path);
		prm.buflen = DefaultMemSize;
		std::unique_ptr<VirtualAllocMemory> actbuf_ = AllocateBuffer(&prm.buffer, &prm.buflen, prm.sectorSize, blocksize);

		handler->BufferAllocated(prm.buflen, prm.sectorSize);

		Handle ev = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (ev.get() == NULL) throw KException(MES("リソースエラー"));
		prm.ev = ev.get();

		// calc total data size
		std::vector<HashData*> validlist;
		int64 totalFileSize = 0;
		for (int64 i = 0, end = filelist.size(); i < end; i++) {
			MakePath(filepath + pathLen, filelist[i]);
			WIN32_FILE_ATTRIBUTE_DATA fileAtt;
			if (GetFileAttributesEx(filepath, GetFileExInfoStandard, &fileAtt) == FALSE) {
				handler->OnResult(filepath + pathLen, FILE_NOT_FOUND);
			}
			else if (fileAtt.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				// 
			}
			else {
				filelist[i].filesize = make64(fileAtt.nFileSizeHigh, fileAtt.nFileSizeLow);
				totalFileSize += filelist[i].filesize;

				validlist.push_back(&filelist[i]);
			}
		}

		handler->TotalFileSize(totalFileSize);

		for (int64 i = 0, end = filelist.size(), v = 0, vend = validlist.size(); i < end; i++) {
			BYTE hash[HASH_LENGTH];

			if (v < vend && (&filelist[i] == validlist[v])) {
				try {
					MakePath(filepath + pathLen, *validlist[v]);
					if (isBenchmark) {
						Benchmark(prm, filepath, validlist[v]->filesize);
					}
					else {
						CalcHash(prm, filepath, validlist[v]->filesize, hash);
					}

					if (isBenchmark) {
						handler->OnResult(filepath + pathLen, CHECK_OK);
					}
					else if (memcmp(validlist[v]->hash, hash, HASH_LENGTH) == 0) {
						handler->OnResult(filepath + pathLen, CHECK_OK);
					}
					else {
						handler->OnResult(filepath + pathLen, HASH_ERROR);
					}
				}
				catch (IOException&) {
					handler->OnResult(filepath + pathLen, IO_ERROR);
				}
				v++;
			}
			else {
				//MakePath(filepath + pathLen, filelist[i]);
				//handler->OnResult(filepath + pathLen, FILE_NOT_FOUND);
			}
		}

		delete[] filepath;
	}

	void FileHashList::MakeFromFiles(LPCWSTR _path, HashCheckHandler* handler, DWORD blocksize)
	{
		FastHashParam prm = { 0 };
		prm.handler = handler;

		const int max_path = 32767;
		wchar_t path[MAX_PATH];
		wchar_t *filepath = new wchar_t[max_path];
		int pathLen = (int)wcslen(_path);
		memcpy(path, _path, pathLen * sizeof(wchar_t));
		if (path[pathLen - 1] != L'\\') {
			path[pathLen++] = L'\\';
		}
		path[pathLen] = 0x0000;
		if (pathLen >= 2 && path[0] == L'\\' && path[1] == L'\\') {
			wcscpy_s(filepath, max_path, path);
		}
		else {
			wcscpy_s(filepath, max_path, L"\\\\?\\");
			wcscat_s(filepath, max_path, path);
			pathLen = (int)wcslen(filepath);
		}

		prm.sectorSize = GetSectorSize(path);
		prm.buflen = DefaultMemSize;
		std::unique_ptr<VirtualAllocMemory> actbuf_ = AllocateBuffer(&prm.buffer, &prm.buflen, prm.sectorSize, blocksize);

		handler->BufferAllocated(prm.buflen, prm.sectorSize);

		filelist.clear();
		flistb.Clear();

		Handle ev = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (ev.get() == NULL) throw KException(MES("リソースエラー"));
		prm.ev = ev.get();

		CalcFileSize calcFileSize;
		calcFileSize.totalFileSize = 0;
		calcFileSize.filepath = filepath;
		calcFileSize.filepathLen = pathLen;

		// calc total data size
		EnumDirectoryFile(path, calcFileSize);

		handler->TotalFileSize(calcFileSize.totalFileSize);

		EnumCallback enumCallback;
		enumCallback.p = this;
		enumCallback.prm = &prm;
		enumCallback.path = path;
		enumCallback.pathLen = pathLen;
		enumCallback.filepath = filepath;
		enumCallback.filepathLen = pathLen;

		// compute hash
		EnumDirectoryFile(path, enumCallback);

		delete[] filepath;
	}

	static LPCWSTR GetErrorString(CHECK_RESULT errorCode)
	{
		switch (errorCode) {
		case CHECK_OK:
			return L"CHECK_OK";
		case HASH_ERROR:
			return L"HASH_ERROR";
		case FILE_NOT_FOUND:
			return L"FILE_NOT_FOUND";
		case IO_ERROR:
			return L"IO_ERROR";
		case WARNING_INVALID_PATH:
			return L"WARNING_INVALID_PATH";
		}
		return L"Unknown Code";
	}
protected:

	struct HashData {
		PBYTE hash;
		__int64 filesize;
		LPCWSTR filename;
		int flen;
	};

	struct VirtualAllocMemory {
		BYTE* ptr;
		VirtualAllocMemory(BYTE* ptr) : ptr(ptr) { }
		~VirtualAllocMemory() { if (ptr) VirtualFree(ptr, 0, MEM_RELEASE); }
		BYTE* get() { return ptr; }
	};

	std::vector<HashData> filelist;
	StackBuffer flistb;

	size_t DefaultMemSize;
	DWORD PageSize;

	std::unique_ptr<FileHashList::VirtualAllocMemory> AllocateBuffer(BYTE** ppBuffer, size_t* memSize, DWORD SectorSize, DWORD blocksize)
	{
		if (PageSize == 0) {
			SYSTEM_INFO info;
			GetSystemInfo(&info);
			PageSize = info.dwPageSize;
		}
		size_t sizeAlign = std::max(PageSize, SectorSize * 2);
		size_t alMemSize = (*memSize + sizeAlign - 1) & ~(sizeAlign - 1);
		if (PageSize < SectorSize) {
			alMemSize += SectorSize - PageSize;
		}
		if (blocksize > 0) {
			alMemSize = blocksize * 2;
		}
		std::unique_ptr<VirtualAllocMemory> buffer(new VirtualAllocMemory((BYTE*)VirtualAlloc(NULL, alMemSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)));
		if (buffer->get() == NULL) throw KException(MES("メモリを確保できません"));
		if (PageSize < SectorSize) {
			*ppBuffer = (BYTE*)(((ULONG_PTR)buffer->get() + SectorSize - PageSize) & ~(ULONG_PTR)(SectorSize - PageSize));
		}
		else {
			*ppBuffer = buffer->get();
		}
		*memSize = alMemSize;
		return buffer;
	}

	typedef struct FastHashParam
	{
		BYTE* buffer;
		size_t buflen;
		HANDLE ev;		// This member is not used.
		DWORD sectorSize;
		bool isBenchmark;
		HashCheckHandler* handler;
	} FastHashParam;


	struct CalcFileSize {
		utl::int64 totalFileSize;
		LPWSTR filepath;
		int filepathLen;

		bool FileHashList::CalcFileSize::operator()(WIN32_FIND_DATA& FindData)
		{

			if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				int prevfilepathlen = filepathLen;
				int filenamelen = (int)wcslen(FindData.cFileName);
				memcpy(filepath + filepathLen, FindData.cFileName, (filenamelen + 1) * sizeof(wchar_t));
				filepathLen += filenamelen;
				filepath[filepathLen++] = L'\\';
				filepath[filepathLen] = 0x0000;

				EnumDirectoryFile(filepath, *this);

				filepathLen = prevfilepathlen;
			}
			else {
				totalFileSize += make64(FindData.nFileSizeHigh, FindData.nFileSizeLow);
			}

			return true;
		}
	};

	struct EnumCallback {
		FileHashList* p;
		FastHashParam* prm;
		LPCWSTR path;
		int pathLen;
		wchar_t* filepath;
		int filepathLen;

		bool FileHashList::EnumCallback::operator()(WIN32_FIND_DATA& FindData)
		{
			int prevfilepathlen = filepathLen;
			int filenamelen = (int)wcslen(FindData.cFileName);
			memcpy(filepath + filepathLen, FindData.cFileName, (filenamelen + 1) * sizeof(wchar_t));
			filepathLen += filenamelen;

			if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				filepath[filepathLen++] = L'\\';
				filepath[filepathLen] = 0x0000;

				EnumDirectoryFile((const wchar_t*)filepath, *this);
			}
			else {
				filepath[filepathLen] = 0x0000;

				BYTE hash[HASH_LENGTH];
				HashData hd = { 0 };

				try {
					hd.filesize = make64(FindData.nFileSizeHigh, FindData.nFileSizeLow);

					CalcHash(*prm, filepath, hd.filesize, hash);

					hd.hash = p->flistb.Add(hash, HASH_LENGTH);

					hd.filename = (LPCWSTR)p->flistb.Add(filepath + pathLen, (filepathLen - pathLen) * sizeof(wchar_t));
					hd.flen = filepathLen - pathLen;

					p->filelist.push_back(hd);
					prm->handler->OnResult(filepath + pathLen, CHECK_OK);
				}
				catch (IOException&) {
					prm->handler->OnResult(filepath + pathLen, IO_ERROR);
				}
			}

			filepathLen = prevfilepathlen;
			return true;
		}
	};


	static BYTE* ReadHex(BYTE* str, BYTE* dst, int len)
	{
		for (int i = 0; i < len; i++) {
			BYTE c1 = HexCharToNum[str[2 * i]];
			BYTE c2 = HexCharToNum[str[2 * i + 1]];
			if (c1 == 0xFF || c2 == 0xFF) {
				throw IOException(MES("16進数ではありません"));
			}
			dst[i] = (c1 << 4) | c2;
		}

		return str += len * 2;
	}

	static void WriteHex(BYTE* dst, BYTE* bin, int len)
	{
		static const BYTE NumToHexChar[0x10] = {
			'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

		for (int i = 0; i < len; i++) {
			dst[2 * i] = NumToHexChar[bin[i] >> 4];
			dst[2 * i + 1] = NumToHexChar[bin[i] & 0xF];
		}
	}

	static BYTE* NextRow(BYTE* str, BYTE* end, BYTE** ppRowEnd)
	{
		for (; str < end; str++) {
			if (*str == '\r' || *str == '\n') {
				*ppRowEnd = str;
				if (str + 1 < end && *(str + 1) == '\n') {
					str++;
				}
				return str + 1;
			}
		}
		*ppRowEnd = end;
		return end;
	}

	static BYTE* PreviousRow(BYTE* str, BYTE* start)
	{
		for (str = str - 1; str >= start; str--) {
			if (*str == '\r' || *str == '\n') {
				return str + 1;
			}
		}
		return start;
	}

	static DWORD GetSectorSize(LPCWSTR path)
	{
		wchar_t rootPath[MAX_PATH];
		wcscpy_s(rootPath, MAX_PATH, path);

		if (memcmp(rootPath, L"\\\\", 4) == 0) {
			// UNC path
			LPWSTR ptr = wcschr(rootPath + 2, L'\\');
			if (ptr == NULL) {
				throw IOException(MES("UNCパスが正しくありません"));
			}
			LPWSTR ptr2 = wcschr(rootPath, L'\\');
			if (ptr2 == NULL) {
				LPWSTR tail = ptr2 + wcslen(ptr2);
				*(tail++) = '\\';
				*(tail) = 0x0000;
			}
			else {
				ptr2[1] = 0x0000;
			}
		}
		else {
			rootPath[3] = 0x0000;
		}

		DWORD lpSectorsPerCluster;
		DWORD lpBytesPerSector;
		DWORD lpNumberOfFreeClusters;
		DWORD lpTotalNumberOfClusters;

		if (GetDiskFreeSpace(rootPath, &lpSectorsPerCluster, &lpBytesPerSector, &lpNumberOfFreeClusters, &lpTotalNumberOfClusters) == FALSE) {
			throw IOException(MES("ディスク情報取得に失敗"));
		}

		return lpBytesPerSector;
	}

	static __int64 make64(int high, unsigned int low)
	{
		return ((__int64)high << 32LL) | low;
	}

	static size_t utf8toutf16(wchar_t* utf16, const BYTE* urf8, size_t utf8len)
	{
		size_t utf16len = 0;
		for (size_t crnt = 0; crnt < utf8len; utf16len++) {
			int ch0 = urf8[crnt++];
			if ((ch0 & 0x80) == 0x00) {
				utf16[utf16len] = (wchar_t)ch0;
			}
			else if ((ch0 & 0xe0) == 0xc0) {
				int ch1 = urf8[crnt++];
				utf16[utf16len] = (wchar_t)(((ch0 & 0x1f) << 6)
					| ((ch1 & 0x3f)));
			}
			else {
				int ch1 = urf8[crnt++];
				int ch2 = urf8[crnt++];
				utf16[utf16len] = (wchar_t)(((ch0 & 0x0f) << 12)
					| ((ch1 & 0x3f) << 6)
					| ((ch2 & 0x3f)));
			}
		}
		return utf16len;
	}

	static size_t utf16toutf8(BYTE* urf8, const wchar_t* utf16, size_t utf16len)
	{
		size_t utf8len = 0;
		for (int i = 0; i < utf16len; i++) {
			wchar_t c = utf16[i];
			if (c <= 0x007F) {
				urf8[utf8len++] = (BYTE)c;
			}
			else if (c <= 0x07FF) {
				urf8[utf8len++] = (BYTE)((c >> 6) | 0xc0);
				urf8[utf8len++] = (BYTE)(((c >> 0) & 0x3f) | 0x80);
			}
			else {
				urf8[utf8len++] = (BYTE)((c >> 12) | 0xe0);
				urf8[utf8len++] = (BYTE)(((c >> 6) & 0x3f) | 0x80);
				urf8[utf8len++] = (BYTE)(((c >> 0) & 0x3f) | 0x80);
			}
		}
		return utf8len;
	}

	static void FileHashList::CalcHash(FastHashParam& p, LPWSTR filename, __int64 filesize, BYTE* hash)
	{
		DWORD openFlag = FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING;
		Handle fd = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, openFlag, NULL);
		if (fd.get() == INVALID_HANDLE_VALUE) throw IOException(MES("ファイルを開けません"));

		size_t splitBufLen = p.buflen / 2;
		BYTE* bufR = p.buffer;
		BYTE* bufC = p.buffer + splitBufLen;

		IORequest ioreq;

		int64 offset = 0;
		DWORD dwReadByte;
		DWORD dwActualReadByte = 0;
		OVERLAPPED overlapped;

		SHA_CTX ctx;
		SHA_Init(&ctx);

		int64 remainSize;

		if (filesize == 0) {
			SHA_Final(hash, &ctx);
			return;
		}

		do {

			memset(&overlapped, 0x00, sizeof(overlapped));
			overlapped.Offset = (DWORD)offset;
			overlapped.OffsetHigh = (DWORD)(offset >> 32);
			overlapped.hEvent = (HANDLE)&ioreq;

			remainSize = filesize - offset;
			if (remainSize > (__int64)splitBufLen) {
				dwReadByte = (DWORD)splitBufLen;
			}
			else {
				dwReadByte = ((DWORD)remainSize + p.sectorSize - 1) & ~(p.sectorSize - 1);
			}

			if (ReadFileEx(fd, bufR, dwReadByte, &overlapped, ReadComplete) == FALSE)
				throw IOException(MES("ファイル読み取りエラー"));

			// compute hash
			SHA_Update(&ctx, bufC, dwActualReadByte);

			// update progress
			p.handler->ProgressUpdate(dwActualReadByte);

			// wait read completion
			if (SleepEx(INFINITE, TRUE) != WAIT_IO_COMPLETION) throw IOException(MES("不明なエラー"));
			// check error code
			if (ioreq.dwErrorCode != 0) throw IOException(MES("ファイル読み取りエラー"));
			dwActualReadByte = ioreq.dwNumberOfBytesTransfered;

			// dwNumberOfBytesTransfered must be a multiple of the sector size
			offset += dwActualReadByte;

			std::swap(bufR, bufC);
		} while (offset < filesize);

		// compute hash
		SHA_Update(&ctx, bufC, remainSize);

		// update progress
		p.handler->ProgressUpdate(remainSize);

		SHA_Final(hash, &ctx);
	}

	static void FileHashList::Benchmark(FastHashParam& p, LPWSTR filename, __int64 filesize)
	{
		//const DWORD openFlag = FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN;
		const DWORD openFlag = FILE_FLAG_NO_BUFFERING | FILE_FLAG_NO_BUFFERING;
		Handle fd = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, openFlag, NULL);
		if (fd.get() == INVALID_HANDLE_VALUE) throw IOException(MES("ファイルを開けません"));

		size_t splitBufLen = p.buflen / 2;
		BYTE* bufR = p.buffer;
		BYTE* bufC = p.buffer + splitBufLen;

		IORequest ioreq;

		int64 offset = 0;
		DWORD dwReadByte;
		DWORD dwActualReadByte = 0;
		OVERLAPPED overlapped;

		int64 remainSize;

		if (filesize == 0) {
			return;
		}

		do {
			memset(&overlapped, 0x00, sizeof(overlapped));
			overlapped.Offset = (DWORD)offset;
			overlapped.OffsetHigh = (DWORD)(offset >> 32);
			overlapped.hEvent = (HANDLE)&ioreq;

			remainSize = filesize - offset;
			if (remainSize > (__int64)splitBufLen) {
				dwReadByte = (DWORD)splitBufLen;
			}
			else {
				dwReadByte = ((DWORD)remainSize + p.sectorSize - 1) & ~(p.sectorSize - 1);
			}

			if (openFlag & FILE_FLAG_OVERLAPPED) {
				if (ReadFileEx(fd, bufR, dwReadByte, &overlapped, ReadComplete) == FALSE)
					throw IOException(MES("ファイル読み取りエラー"));

				// update progress
				p.handler->ProgressUpdate(dwActualReadByte);

				// wait read completion
				if (SleepEx(INFINITE, TRUE) != WAIT_IO_COMPLETION) throw IOException(MES("不明なエラー"));
				// check error code
				if (ioreq.dwErrorCode != 0) throw IOException(MES("ファイル読み取りエラー"));
				dwActualReadByte = ioreq.dwNumberOfBytesTransfered;
			}
			else {
				// update progress
				p.handler->ProgressUpdate(dwActualReadByte);

				if (ReadFile(fd, bufR, dwReadByte, &dwActualReadByte, NULL) == FALSE)
					throw IOException(MES("ファイル読み取りエラー"));
			}

			// dwNumberOfBytesTransfered must be a multiple of the sector size
			offset += dwActualReadByte;

			std::swap(bufR, bufC);
		} while (offset < filesize);

		// update progress
		p.handler->ProgressUpdate(remainSize);
	}

	static void FileHashList::MakePath(wchar_t* path, const HashData& hd)
	{
		memcpy(path, hd.filename, hd.flen * sizeof(wchar_t));
		path[hd.flen] = 0x0000;
	}
};


void FileWriteAccessCheck(LPCWSTR path, bool createNew) {
	if (createNew) {
		Handle fd = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
		if (fd.get() == INVALID_HANDLE_VALUE) {
			if (GetLastError() == ERROR_FILE_EXISTS) {
				throw IOException(MES("ハッシュファイルが既に存在しています"));
			}
			throw IOException(MES("ハッシュファイルに書き込めません"));
		}
	}
	else {
		Handle fd = CreateFile(path, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (fd.get() == INVALID_HANDLE_VALUE) {
			throw IOException(MES("ハッシュファイルに書き込めません"));
		}
	}
}

void FileReadAccessCheck(LPCWSTR path) {
	Handle fd = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fd.get() == INVALID_HANDLE_VALUE) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND) {
			throw IOException(MES("ハッシュファイルが見つかりません"));
		}
		throw IOException(MES("ハッシュファイルがファイルを開けません"));
	}
}

}
