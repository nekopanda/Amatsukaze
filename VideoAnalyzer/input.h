//----------------------------------------------------------------------------------
//	入力プラグイン ヘッダーファイル for AviUtl version 0.99k 以降
//	By ＫＥＮくん
//----------------------------------------------------------------------------------

//	入力ファイル情報構造体
typedef struct {
	int					flag;				//	フラグ
											//	INPUT_INFO_FLAG_VIDEO	: 画像データあり
											//	INPUT_INFO_FLAG_AUDIO	: 音声データあり
											//	INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS	: キーフレームを気にせずにfunc_read_video()を呼び出します
											//	※標準ではキーフレームからシーケンシャルにfunc_read_video()が呼ばれるように制御されます
	int					rate,scale;			//	フレームレート
	int					n;					//	フレーム数
	BITMAPINFOHEADER	*format;			//	画像フォーマットへのポインタ(次に関数が呼ばれるまで内容を有効にしておく)
	int					format_size;		//	画像フォーマットのサイズ
	int					audio_n;			//	音声サンプル数
	WAVEFORMATEX		*audio_format;		//	音声フォーマットへのポインタ(次に関数が呼ばれるまで内容を有効にしておく)
	int					audio_format_size;	//	音声フォーマットのサイズ
	DWORD				handler;			//	画像codecハンドラ
	int					reserve[7];
} INPUT_INFO;
#define	INPUT_INFO_FLAG_VIDEO				1
#define	INPUT_INFO_FLAG_AUDIO				2
#define	INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS	8
//	※画像フォーマットにはRGB,YUY2とインストールされているcodecのものが使えます。
//	また、'Y''C''4''8'(biBitCountは48)でPIXEL_YC形式フォーマットで扱えます。(YUY2フィルタモードでは使用出来ません)
//	音声フォーマットにはPCMとインストールされているcodecのものが使えます。

//	入力ファイルハンドル
typedef void*	INPUT_HANDLE;

//	入力プラグイン構造体
typedef struct {
	int		flag;				//	フラグ
								//	INPUT_PLUGIN_FLAG_VIDEO	: 画像をサポートする
								//	INPUT_PLUGIN_FLAG_AUDIO	: 音声をサポートする
	LPSTR	name;				//	プラグインの名前
	LPSTR	filefilter;			//	入力ファイルフィルタ
	LPSTR	information;		//	プラグインの情報
	BOOL 	(*func_init)( void );
								//	DLL開始時に呼ばれる関数へのポインタ (NULLなら呼ばれません)
	BOOL 	(*func_exit)( void );
								//	DLL終了時に呼ばれる関数へのポインタ (NULLなら呼ばれません)
	INPUT_HANDLE (*func_open)( LPSTR file );
								//	入力ファイルをオープンする関数へのポインタ
								//	file	: ファイル名
								//	戻り値	: TRUEなら入力ファイルハンドル
	BOOL 	(*func_close)( INPUT_HANDLE ih );
								//	入力ファイルをクローズする関数へのポインタ
								//	ih		: 入力ファイルハンドル
								//	戻り値	: TRUEなら成功
	BOOL 	(*func_info_get)( INPUT_HANDLE ih,INPUT_INFO *iip );
								//	入力ファイルの情報を取得する関数へのポインタ
								//	ih		: 入力ファイルハンドル
								//	iip		: 入力ファイル情報構造体へのポインタ
								//	戻り値	: TRUEなら成功
	int 	(*func_read_video)( INPUT_HANDLE ih,int frame,void *buf );
								//	画像データを読み込む関数へのポインタ
								//	ih		: 入力ファイルハンドル
								//	frame	: 読み込むフレーム番号
								//	buf		: データを読み込むバッファへのポインタ
								//	戻り値	: 読み込んだデータサイズ
	int 	(*func_read_audio)( INPUT_HANDLE ih,int start,int length,void *buf );
								//	音声データを読み込む関数へのポインタ
								//	ih		: 入力ファイルハンドル
								//	start	: 読み込み開始サンプル番号
								//	length	: 読み込むサンプル数
								//	buf		: データを読み込むバッファへのポインタ
								//	戻り値	: 読み込んだサンプル数
	BOOL 	(*func_is_keyframe)( INPUT_HANDLE ih,int frame );
								//	キーフレームか調べる関数へのポインタ (NULLなら全てキーフレーム)
								//	ih		: 入力ファイルハンドル
								//	frame	: フレーム番号
								//	戻り値	: キーフレームなら成功
	BOOL	(*func_config)( HWND hwnd,HINSTANCE dll_hinst );
								//	入力設定のダイアログを要求された時に呼ばれる関数へのポインタ (NULLなら呼ばれません)
								//	hwnd		: ウィンドウハンドル
								//	dll_hinst	: インスタンスハンドル
								//	戻り値		: TRUEなら成功
	int		reserve[16];
} INPUT_PLUGIN_TABLE;
#define	INPUT_PLUGIN_FLAG_VIDEO		1
#define	INPUT_PLUGIN_FLAG_AUDIO		2

BOOL func_init( void );
BOOL func_exit( void );
INPUT_HANDLE func_open( LPSTR file );
BOOL func_close( INPUT_HANDLE ih );
BOOL func_info_get( INPUT_HANDLE ih,INPUT_INFO *iip );
int func_read_video( INPUT_HANDLE ih,int frame,void *buf );
int func_read_audio( INPUT_HANDLE ih,int start,int length,void *buf );
BOOL func_is_keyframe( INPUT_HANDLE ih,int frame );
BOOL func_config( HWND hwnd,HINSTANCE dll_hinst );
