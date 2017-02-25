#pragma once

#include "stdafx.h"

// FAWチェックと、FAWPreview.aufを使っての1フレームデコード
class CFAW {
	bool is_half;

	bool load_failed;
	HMODULE _h;

	typedef int (__stdcall *ExtractDecode1FAW)(const short *in, int samples, short *out, bool is_half);
	ExtractDecode1FAW _ExtractDecode1FAW;

	bool load() {
		if (_ExtractDecode1FAW == NULL && load_failed == false) {
			_h = LoadLibrary("FAWPreview.auf");
			if (_h == NULL) {
				load_failed = true;
				return false;
			}
			_ExtractDecode1FAW = (ExtractDecode1FAW)GetProcAddress(_h, "ExtractDecode1FAW");
			if (_ExtractDecode1FAW == NULL) {
				FreeLibrary(_h);
				_h = NULL;
				load_failed = true;
				return false;
			}
			return true;
		}
		return _ExtractDecode1FAW != NULL;
	}
public:
	CFAW() : _h(NULL), _ExtractDecode1FAW(NULL), load_failed(false), is_half(false) { }

	~CFAW() {
		if (_h) {
			FreeLibrary(_h);
		}
	}

	bool isLoadFailed(void) {
		return load_failed;
	}

	// FAW開始地点を探す。1/2なFAWが見つかれば、以降はそれしか探さない。
	// in: get_audio()で得た音声データ
	// samples: get_audio() * ch数
	// 戻り値：FAW開始位置のインデックス。なければ-1
	int findFAW(short *in, int samples) {
		// search for 72 F8 1F 4E 07 01 00 00
		static unsigned char faw11[] = {0x72, 0xF8, 0x1F, 0x4E, 0x07, 0x01, 0x00, 0x00};
		if (is_half == false) {
			for (int j=0; j<samples - 30; ++j) {
				if (memcmp(in+j, faw11, sizeof(faw11)) == 0) {
					return j;
				}
			}
		}

		// search for 00 F2 00 78 00 9F 00 CE 00 87 00 81 00 80 00 80
		static unsigned char faw12[] = {0x00, 0xF2, 0x00, 0x78, 0x00, 0x9F, 0x00, 0xCE,
										0x00, 0x87, 0x00, 0x81, 0x00, 0x80, 0x00, 0x80};

		for (int j=0; j<samples - 30; ++j) {
			if (memcmp(in+j, faw12, sizeof(faw12)) == 0) {
				is_half = true;
				return j;
			}
		}

		return -1;
	}

	// FAWPreview.aufを使ってFAWデータ1つを抽出＆デコードする
	// in: FAW開始位置のポインタ。findFAWに渡したin + findFAWの戻り値
	// samples: inにあるデータのshort換算でのサイズ
	// out: デコード結果を入れるバッファ(16bit, 2chで1024サンプル)
	//     （1024sample * 2byte * 2ch = 4096バイト必要）
	int decodeFAW(const short *in, int samples, short *out){
		if (load()) {
			return _ExtractDecode1FAW(in, samples, out, is_half);
		}
		return 0;
	}
};

// FAWデコードフィルタ
class FAWDecoder : public NullSource {
	CFAW _cfaw;
	Source *_src;
	WAVEFORMATEX fmt;
public:
	FAWDecoder(Source *src) : NullSource(), _src(src){
		ZeroMemory(&fmt, sizeof(fmt));
		fmt.wFormatTag = WAVE_FORMAT_PCM;		
		fmt.nChannels = 2;
		fmt.nSamplesPerSec = 48000;
		fmt.wBitsPerSample = 16;
		fmt.nBlockAlign = fmt.wBitsPerSample / 8 * fmt.nChannels;
		fmt.nAvgBytesPerSec = fmt.nBlockAlign * fmt.nSamplesPerSec;
		fmt.cbSize = 0;
		_ip.audio_format = &fmt;
		_ip.audio_format_size = sizeof(fmt);
		_ip.audio_n = -1;

		_ip.flag = INPUT_INFO_FLAG_AUDIO;
	}

	int release() {
		_src->release();
		return NullSource::release();
	}

	int read_audio(int frame, short *buf) {
		int nsamples = _src->read_audio(frame, buf);
		nsamples *= _src->get_input_info().audio_format->nChannels;

		int j = _cfaw.findFAW(buf, nsamples);
		if (j == -1) {
			return 0;
		}

		// 2chなので2で割る
		return _cfaw.decodeFAW(buf+j, nsamples-j, buf) / 2;
	}
};
