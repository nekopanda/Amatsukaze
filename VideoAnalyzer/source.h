// 入力クラス

#pragma once

#include "stdafx.h"

interface Source {
public:
	virtual int add_ref() = 0;
	virtual int release() = 0;

	virtual void init(char *infile) = 0;

	virtual bool has_video() = 0;
	virtual bool has_audio() = 0;
	virtual INPUT_INFO &get_input_info() = 0;
	virtual void set_rate(int rate, int scale) = 0;

	virtual bool read_video_y8(int frame, unsigned char *luma) = 0;
	virtual int read_audio(int frame, short *buf) = 0;
};

// 空のソース
class NullSource : public Source {
protected:
	NullSource() : _ref(1) { ZeroMemory(&_ip, sizeof(_ip)); }
	virtual ~NullSource() { }

	INPUT_INFO _ip;
	int _ref;
public:

	int add_ref() { return ++_ref; }
	int release() { int r = --_ref; if (r <= 0) delete this; return r; }

	bool has_video() { return (_ip.flag & INPUT_INFO_FLAG_VIDEO) != 0; };
	bool has_audio() { return (_ip.flag & INPUT_INFO_FLAG_AUDIO) != 0; };
	INPUT_INFO &get_input_info() { return _ip; };
	void set_rate(int rate, int scale) {
		_ip.rate = rate;
		_ip.scale = scale;
	}

	// must implement
	void init(char *infile) { };
	bool read_video_y8(int frame, unsigned char *luma) { return false; };
	int read_audio(int frame, short *buf) { return 0; };
};

// auiを使ったソース
class AuiSource : public NullSource {
protected:
	string _in, _plugin;

	HMODULE _dll;

	INPUT_PLUGIN_TABLE *_ipt;
	INPUT_HANDLE _ih;
	//INPUT_INFO _ip;

public:
	AuiSource(void) : NullSource(), _dll(NULL) { }
	virtual ~AuiSource() {
		if (_dll) {
			FreeLibrary(_dll);
		}
	}

	virtual void init(char *infile) {
		_in = infile;
		_plugin = "avsinp.aui";

		int p = _in.find("://");
		if (p != _in.npos) {
			_plugin = _in.substr(0, p);
			_in = _in.substr(p+3);
		}

		printf(" -%s\n", _plugin.c_str());

		_dll = LoadLibrary(_plugin.c_str());
		if (_dll == NULL) {
			throw "   plugin loading failed.";
		}

		FARPROC f = GetProcAddress(_dll, _T("GetInputPluginTable"));
		if (f == NULL) {
			throw "   not Aviutl input plugin error.";
		}
		_ipt = (INPUT_PLUGIN_TABLE*)f();
		if (_ipt == NULL) {
			throw "   not Aviutl input plugin error.";
		}
		if (_ipt->func_init) {
			if (_ipt->func_init() == FALSE) {
				throw "   func_init() failed.";
			}
		}

		_ih = _ipt->func_open((LPSTR)_in.c_str());
		if (_ih == NULL) {
			throw "   func_open() failed.";
		}

		if (_ipt->func_info_get(_ih, &_ip) == FALSE) {
			throw "   func_info_get() failed...";
		}
	}

	bool has_video() {
		return (_ip.flag & INPUT_INFO_FLAG_VIDEO) != 0;
	}
	bool has_audio() {
		return (_ip.flag & INPUT_INFO_FLAG_AUDIO) != 0;
	}

	INPUT_INFO &get_input_info() {
		return _ip;
	}

	bool read_video_y8(int frame, unsigned char *luma) {
		int h = _ip.format->biHeight;
		int w = _ip.format->biWidth;
		unsigned char *buf = (unsigned char *)malloc(2 * h * w);

		int ret = _ipt->func_read_video(_ih, frame, buf);
		if (ret == 0) {
			return false;
		}

		int skip_w = w & 0x0F;
		w = w - skip_w;

		unsigned char *p = buf;
		for (int i=0; i<w; i++) {
			for (int j=0; j<h; j++) {
				*luma = *p;

				luma++;
				p += 2;
			}
			p += skip_w * 2;
		}
		free(buf);
		return true;
	}

	int read_audio(int frame, short *buf) {
		int start = (int)((double)frame * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
		int end = (int)((double)(frame + 1) * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
		return _ipt->func_read_audio(_ih, start, end - start, buf);
	}
};

// *.wavソース
class WavSource : public NullSource {
	string _in;

	FILE *_f;
	__int64 _start;
	__int64 _end;
	WAVEFORMATEX _fmt;

public:
	WavSource() : NullSource(), _f(NULL), _start(0) { }
	~WavSource() {
		if (_f) {
			fclose(_f);
		}
	}

	void init(char *infile) {
		printf(" -WavSource\n");
		_f = fopen(infile, "rb");
		if (_f == NULL) {
			throw "   wav open failed.";
		}

		char buf[1000];
		if (fread(buf, 1, 4, _f) != 4 || strncmp(buf, "RIFF", 4) != 0) {
			throw "   no RIFF header.";
		}
		fseek(_f, 4, SEEK_CUR);
		if (fread(buf, 1, 4, _f) != 4 || strncmp(buf, "WAVE", 4) != 0) {
			throw "   no WAVE header.";
		}

		// chunk
		while(fread(buf, 1, 4, _f) == 4) {
			if (ftell(_f) > 1000000) {
				break;				
			}

			int size = 0;
			fread(&size, 4, 1, _f);
			if (strncmp(buf, "fmt ", 4) == 0) {
				if (fread(&_fmt, min(size, sizeof(_fmt)), 1, _f) != 1) {
					throw "   illegal WAVE file.";
				}
				if (_fmt.wFormatTag != WAVE_FORMAT_PCM) {
					throw "   only PCM supported.";
				}
				int diff = size - sizeof(_fmt);
				if (diff > 0) {
					fseek(_f, size - sizeof(_fmt), SEEK_CUR);
				}
			} else if (strncmp(buf, "data", 4) == 0){
				fseek(_f, 4, SEEK_CUR);
				_start = _ftelli64(_f);
				break;
			} else {
				fseek(_f, size, SEEK_CUR);
			}
		}
		if (_start == 0) {
			fclose(_f);
			throw "   maybe not wav file.";
		}

		ZeroMemory(&_ip, sizeof(_ip));
		_ip.flag |= INPUT_INFO_FLAG_AUDIO;
		_ip.audio_format = &_fmt;
		_ip.audio_format_size = sizeof(_fmt);
		_ip.audio_n = -1;
	}

	int read_audio(int frame, short *buf) {
		__int64 start = (int)((double)frame * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
		__int64 end = (int)((double)(frame + 1) * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);

		_fseeki64(_f, _start + start * _fmt.nBlockAlign, SEEK_SET);

		return fread(buf, _fmt.nBlockAlign, (size_t)(end - start), _f);
	}
};
