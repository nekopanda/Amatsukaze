#include "common.h"
#include <tchar.h>
#include <iostream>

#include "hash.hpp"

namespace hashchecker {

wchar_t* NumConmmaSeparatedFormat(wchar_t* str, __int64 num,  int width = 0, wchar_t conmma = L',', wchar_t space = L' ')
{
	int k = 0;

	// 桁数をカウント
	for(__int64 j = num; j; j /= 10, k++);

	// 数値部分を書く
	int k_len = k + (k-1)/3 + ((num < 0) ? 1 : 0);
	if( num == 0 ) k_len++;
	int len = std::max(k_len, width);
	wchar_t *ptr = str + len - 1;
	for(__int64 j = num, i = 0; j; j /= 10, ptr--, i++) {
		if( (i % 3) == 0 && i ){
			*ptr-- = conmma;
		}
		*ptr = L'0' + (wchar_t)abs(j % 10);
	}
	if( num == 0 ) *ptr-- = L'0';

	if( space == L' ' ){
		if( num < 0 ) *ptr-- = L'-';
		for( ; ptr >= str; ptr--) *ptr = L' ';
	}
	else {
		for(int i = k; ptr >= str; ptr--, i++){
			if( (i % 3) == 0 ){
				if( ptr == str ) break; 
				*ptr-- = conmma;
			}
			*ptr = space;
		}
		if( num < 0 ) *str = L'-';
	}

	return str + len;
}

LPCWSTR FillSpaceString = L"                                                  "
			L"                       \r";

class ProgressPrinter : public FileHashList::HashCheckHandler
{
protected:
	int64 totalFileSize;
	int64 completeSize;
	DWORD speed; // (KBps)
	DWORD prevTick;
	int64 prevCompSize;
public:

	virtual void TotalFileSize(utl::int64 totalFileSize)
	{
		this->totalFileSize = totalFileSize;
		this->completeSize = 0;

		wchar_t sizebuf[32];
		*NumConmmaSeparatedFormat(sizebuf, totalFileSize) = L'\0';
		wprintf(L"ハッシュチェック対象ファイルは合計 %s バイトです\n", sizebuf);

		speed = 0;
		prevTick = GetTickCount();
		prevCompSize = 0;
	}
	virtual void BufferAllocated(size_t buflen, DWORD sectorSize)
	{
		wprintf(L"読み取りサイズ: %zd bytes, セクターサイズ: %d bytes \n", buflen/2, sectorSize);
	}
	virtual void ProgressUpdate(utl::int64 readByte)
	{
		completeSize += readByte;

		DWORD nowTick = GetTickCount();
		if( nowTick - prevTick > 1000 ){
			int prog1000 = (int)(completeSize * 10000 / totalFileSize);

			wchar_t bar[42];
			int prog40 = (int)(completeSize * (sizeof(bar)/sizeof(wchar_t)-2) / totalFileSize);
			for(int i = 0, end = prog40; i < end; i++) bar[i] = L'=';
			bar[prog40] = L'>';
			for(int i = prog40+1, end = sizeof(bar)/sizeof(wchar_t)-1; i < end; i++) bar[i] = L' ';
			bar[sizeof(bar)/sizeof(wchar_t)-1] = L'\0';

			DWORD time = nowTick - prevTick;
			int64 newSize = completeSize - prevCompSize;

			speed = (int)(newSize / time);
			
			prevTick = nowTick;
			prevCompSize = completeSize;
		
			wchar_t sizebuf[32];
			*NumConmmaSeparatedFormat(sizebuf, completeSize/1000000, 11) = L'\0';

			wprintf(L"\r%3d%%[%s]%sMB%4d.%02dMB/s\r", prog1000 / 100, bar, sizebuf, speed/1000, (speed/10) % 100);
		}
	}
};

class MakeHandler : public ProgressPrinter
{
public:
	int64 filecnt;
	int64 errorcnt;

	MakeHandler()
		: filecnt(0)
		, errorcnt(0)
	{
	}

	virtual void OnResult(LPCWSTR filename, FileHashList::CHECK_RESULT result)
	{
		if( result != FileHashList::CHECK_OK ) {
			wprintf(FillSpaceString);
			wprintf(L"%s: %s\n", FileHashList::GetErrorString(result), filename);
			errorcnt++;
		}
		else {
			filecnt++;
		}
	}
};

class CheckHandler : public MakeHandler
{
public:
	int64 hashError;
	int64 notFound;
	int64 ioError;
	int64 otherError;

	CheckHandler()
		: hashError(0)
		, notFound(0)
		, ioError(0)
		, otherError(0)
	{
	}

	virtual void OnResult(LPCWSTR filename, FileHashList::CHECK_RESULT result)
	{
		if( result != FileHashList::CHECK_OK ) {
			wprintf(FillSpaceString);
			wprintf(L"%s: %s\n", FileHashList::GetErrorString(result), filename);

			if( result == FileHashList::HASH_ERROR ){
				hashError++;
			}
			else if( result == FileHashList::FILE_NOT_FOUND ) {
				notFound++;
			}
			else if( result == FileHashList::IO_ERROR ) {
				ioError++;
			}
			else {
				otherError++;
			}

			errorcnt++;
		}
		filecnt++;
	}

	virtual void WrongHashFile(FileHashList::HASH_FILE_ERROR he)
	{
		char c;
		printf("ハッシュファイルが壊れています。続行しますか？[Y/n]:");
		scanf_s("%c", &c, 1);
		if(c != 'Y'){
			throw IOException(MES("アボートしました"));
		}
	}
};

} // namespace hashchecker

using namespace hashchecker;

void PrintHelp()
{
	wprintf(L"Batch Hash Checker version 1.1.0\n"
		L"Command: bhc [m|c|hc|hu] [オプション] path\n"
		L"m : ハッシュリストを作成・更新します\n"
		L"c : ファイルをチェックします\n"
		L"b : Readベンチマークを走らせます\n"
		L"hc: ハッシュリストのハッシュをチェックします\n"
		L"hu: ハッシュリストのハッシュを更新します\n"
		L"オプション\n"
		L"-hl (filepath) : ハッシュリストのファイルを選択\n"
		L"-rb ブロックサイズ : １回のReadFileで読み取るデータ量（G,M,Kサフィックス対応）\n"
		);
}

typedef struct InputCommand
{
	enum { MAKE_HASH, CHECK_HASH, BENCHMARK, HASH_FILE_CHECK, HASH_FILE_UPDATE } mode;
	DWORD blocksize;
	wchar_t path[MAX_PATH];
	wchar_t hashpath[MAX_PATH];
} InputCommand;

wchar_t* ConverToAbsolutePath(wchar_t* path)
{
	wchar_t buf[MAX_PATH];
	int pathlen = (int)wcslen(path);
	/*
	if( pathlen >= 3 && path[1] == L':' && path[2] == L'\\' ){
		// "C:\..." or "D:\..." and so on
		return;
	}
	if( pathlen >= 2 && path[0] == L'\\' && path[1] == L'\\' ){
		// network path
		return;
	}
	*/
	memcpy(buf, path, (pathlen+1)*sizeof(wchar_t));
	wchar_t *file = NULL;
	if( GetFullPathName(buf, MAX_PATH, path, &file) == 0 ) throw IOException(DEFMES);

	return file;
}

void ParseCommand(int argc, _TCHAR* argv[], InputCommand* cmd)
{
	memset(cmd, 0x00, sizeof(InputCommand));

	if( argc < 3 ){
		throw L"コマンド引数が少なすぎます";
	}

	LPCWSTR modeStr = argv[1];
	if( *modeStr == L'm' ){
		cmd->mode = InputCommand::MAKE_HASH;
	}
	else if( *modeStr == L'c' ){
		cmd->mode = InputCommand::CHECK_HASH;
	}
	else if( *modeStr == L'b' ){
		cmd->mode = InputCommand::BENCHMARK;
	}
	else if( *modeStr == L'h' ){
		if( modeStr[1] == L'c' ){
			cmd->mode = InputCommand::HASH_FILE_CHECK;
		}
		else if( modeStr[1] == L'u' ){
			cmd->mode = InputCommand::HASH_FILE_UPDATE;
		}
		else{
			throw L"モードは'u'か'c'です";
		}
	}
	else{
		throw L"モードは'm'か'c'です";
	}

	int argidx = 2;
	// is there option string ?
	while( argidx + 3 <= argc ){
		LPCWSTR optionStr1 = argv[argidx++];
		LPCWSTR optionStr2 = argv[argidx++];

		if( wcscmp(optionStr1, L"-hl") == 0 ){
			wcscpy_s(cmd->hashpath, optionStr2);
		}
		else if( wcscmp(optionStr1, L"-rb") == 0 ){
			DWORD unit = 1;
			int len = (int)wcslen(optionStr2);
			wchar_t suffix = optionStr2[len-1];
			if(suffix >= '0' && suffix <= '9') {
			}
			else {
				switch(suffix) {
				case 'm':
				case 'M':
					unit = 1024*1024;
					break;
				case 'k':
				case 'K':
					unit = 1024;
					break;
				case 'g':
				case 'G':
					unit = 1024*1024*1024;
					break;
				default:
					throw L"不明なサイズ指定です";
				}
			}
			cmd->blocksize = _wtoi(optionStr2) * unit;
		}
		else{
			throw L"不明なオプションです";
		}
	}
	LPCWSTR pathStr = argv[argidx++];
	
	int pathLen = (int)wcslen(pathStr);
	memcpy(cmd->path, pathStr, pathLen*sizeof(wchar_t));
	// remove '\\'
	if( cmd->path[pathLen-1] == L'\\' ){
		cmd->path[pathLen-1] = L'\0';
		pathLen--;
	}

	// conver to absolute path if they are relative path
	ConverToAbsolutePath(cmd->path);

	wchar_t *filepart = wcsrchr(cmd->path, L'\\') + 1;

	if( cmd->hashpath[0] == L'\0' ){
		size_t filepartpos = filepart - cmd->path;
		memcpy(cmd->hashpath, cmd->path, filepartpos*sizeof(filepart[0]));
		wsprintf(cmd->hashpath + filepartpos, L"%s.hash", filepart);
	}

	ConverToAbsolutePath(cmd->hashpath);
}

// Main
int T01Main(int argc, _TCHAR* argv[])
{
	/*
		コマンド
		bhc [m|c] [オプション] path
		オプション
		-hl (filepath) : ハッシュリストのファイルを選択
	*/
	InputCommand cmd;
	int ret = 0;

	try {
		ParseCommand(argc, argv, &cmd);
	}
	catch(LPCWSTR mes) {
		wprintf(mes);
		wprintf(L"\n");
		PrintHelp();
		return 0;
	}
	
	try {
		if( cmd.mode == InputCommand::MAKE_HASH ){
			FileHashList fhl;
			MakeHandler handler;

			wprintf(L"ハッシュリストを作成します\n");

			try {
				FileWriteAccessCheck(cmd.hashpath, true);
			}
			catch (KException& exc) {
				printf("ハッシュファイル書き込みチェックエラーが発生しました(%s)\n", exc.getMessage());
				throw;
			}

			fhl.MakeFromFiles(cmd.path, &handler, cmd.blocksize);
			wprintf(FillSpaceString);
			
			try {
				fhl.WriteToFile(cmd.hashpath);
			}
			catch(KException& exc) {
				printf("ハッシュファイル書き込み時にエラーが発生しました(%s)\n", exc.getMessage());
				throw;
			}

			if( handler.errorcnt == 0 ){
				wprintf(L"データベース作成:すべて正常に終了しました\n");
			}
			wprintf(L" %I64d個のファイルをデータベースに追加しました\n", (int64_t)handler.filecnt);
			if( handler.errorcnt != 0 ){
				wprintf(L"エラーが発生しています！！\n %I64d個のファイルがエラーで追加されませんでした\n",
					(int64_t)handler.errorcnt);
			}

			wprintf(L"出力ファイル:%s\n", cmd.hashpath);
		}
		else if( cmd.mode == InputCommand::CHECK_HASH ){
			FileHashList fhl;
			CheckHandler handler;

			wprintf(L"データをハッシュと照合します\n");

			try {
				fhl.ReadFromFile(cmd.hashpath, &handler);
			}
			catch(KException& exc) {
				printf("ハッシュファイル読み込み時にエラーが発生しました(%s)\n", exc.getMessage());
				throw;
			}

			fhl.CheckHash(cmd.path, &handler, cmd.blocksize, false);
			wprintf(FillSpaceString);

			if( handler.errorcnt == 0 ){
				wprintf(L"ファイルチェック:すべて正常に終了しました\n");
			}
			wprintf(L" %I64d個のファイルをチェックしました\n", (int64_t)handler.filecnt);
			if( handler.errorcnt != 0 ){
				wprintf(L" %I64d個のファイルでエラーが発生しました！！\n",
					(int64_t)handler.errorcnt);
				wprintf(L"内訳:ハッシュエラー:%I64d, ファイルがない:%I64d, 読み取り失敗:%I64d, その他:%I64d\n",
					(int64_t)handler.hashError, (int64_t)handler.notFound, (int64_t)handler.ioError, (int64_t)handler.otherError);
			}
		}
		else if( cmd.mode == InputCommand::BENCHMARK ){
			FileHashList fhl;
			CheckHandler handler;

			wprintf(L"データ読み取りベンチマーク\n");

			try {
				fhl.ReadFromFile(cmd.hashpath, &handler);
			}
			catch(KException& exc) {
				printf("ハッシュファイル読み込み時にエラーが発生しました(%s)\n", exc.getMessage());
				throw;
			}

			fhl.CheckHash(cmd.path, &handler, cmd.blocksize, true);
			wprintf(FillSpaceString);
		}
		else if( cmd.mode == InputCommand::HASH_FILE_CHECK || cmd.mode == InputCommand::HASH_FILE_UPDATE ){
			FileHashList fhl;
			CheckHandler handler;

			class CheckHandler : public FileHashList::HashCheckHandler
			{
			public:
				FileHashList::HASH_FILE_ERROR result;
				CheckHandler() : result(FileHashList::HF_NO_ERROR) { }
				virtual void WrongHashFile(FileHashList::HASH_FILE_ERROR he) {
					result = he;
				}
			} cHandler;

			if(cmd.mode == InputCommand::HASH_FILE_UPDATE){
		//		wprintf(L"ハッシュファイルを更新します\n");

				try {
					FileWriteAccessCheck(cmd.hashpath, false);

					fhl.ReadFromFile(cmd.hashpath, &cHandler);
					if(cHandler.result == FileHashList::HF_NO_ERROR){
						printf("更新の必要はありません\n");
					}
					else {
						fhl.WriteToFile(cmd.hashpath);
						printf("更新完了\n");
					}
				}
				catch(KException& exc) {
					printf("ハッシュファイル読み書き時にエラーが発生しました(%s)\n", exc.getMessage());
					throw;
				}
			}
			else if(cmd.mode == InputCommand::HASH_FILE_CHECK) {
		//		wprintf(L"ハッシュファイルをチェックします\n");

				try {
					fhl.ReadFromFile(cmd.hashpath, &cHandler);
					switch(cHandler.result){
					case FileHashList::HF_NO_ERROR:
						printf("ハッシュファイルは正常です\n");
						break;
					case FileHashList::HF_NOT_MATCH:
						printf("ハッシュファイルは壊れています\n");
						break;
					case FileHashList::HF_NO_HASH:
						printf("ハッシュデータがありません\n");
						break;
					}
				}
				catch(KException& exc) {
					printf("ハッシュファイル読み込み時にエラーが発生しました(%s)\n", exc.getMessage());
					throw;
				}
			}
			else {
				throw "???";
			}
		}
		else{
			throw "???";
		}
	}
	catch(...) {
		//
	}

	return ret;
}

int _tmain(int argc, _TCHAR* argv[])
{
	// グローバル変数の初期化でヒープから確保して、それがレポートされてしまうので
	// 差分だけ表示するように変更
#ifdef _DEBUG
	_CrtMemState MemState;
#endif
	_CrtMemCheckpoint(&MemState);
	//_CrtDumpMemoryLeaks();
	_ASSERT(_CrtCheckMemory()); // 忘れたときのために書いておく
	setlocale( LC_ALL, "jpn" );

	// ここでテスト項目切り替え
	int re_code;
	re_code = T01Main(argc, argv);

#ifdef _DEBUG
	getchar();
#endif

	//_CrtDumpMemoryLeaks();
	_CrtMemDumpAllObjectsSince(&MemState);
	return re_code;
}

