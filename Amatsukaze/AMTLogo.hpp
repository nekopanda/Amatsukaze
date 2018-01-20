/**
* Amtasukaze Logo File
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*
* ただし、ToOutLGP()の中身の処理は
* MakKi氏の透過性ロゴ フィルタプラグインより拝借
* https://github.com/makiuchi-d/delogo-aviutl
*/
#pragma once

#include "CoreUtils.hpp"
#include "logo.h"

namespace logo {

struct LogoHeader {
	int magic;
	int version;
	int w, h;
	int logUVx, logUVy;
	int imgw, imgh, imgx, imgy;
	char name[255];
	int serviceId;
	int reserved[60];

	LogoHeader() { }

	LogoHeader(int w, int h, int logUVx, int logUVy, int imgw, int imgh, int imgx, int imgy, const std::string& name)
		: magic(0x12345)
		, version(1)
		, w(w)
		, h(h)
		, logUVx(logUVx)
		, logUVy(logUVy)
		, imgw(imgw)
		, imgh(imgh)
		, imgx(imgx)
		, imgy(imgy)
		, name()
		, reserved()
	{
		strncpy_s(this->name, name.c_str(), sizeof(name) - 1);
	}
};

class LogoData
{
protected:
	int w, h;
	int logUVx, logUVy;
	std::unique_ptr<float[]> data;
	float *aY, *aU, *aV;
	float *bY, *bU, *bV;

	static void ToYC48Y(float& y) {
		y = float(((int(y * 255) * 1197) >> 6) - 299);
	}

	static void ToYC48C(float& u) {
		u = float(((int(u * 255) - 128) * 4681 + 164) >> 8);
	}

	static void ToYV12Y(float& y) {
		y = float(((((int)y * 219 + 383) >> 12) + 16) / 255.0f);
	}

	static void ToYV12C(float& u) {
		u = float((((((int)u + 2048) * 7 + 66) >> 7) + 16) / 255.0f);
	}

	static void ToYC48ABY(float& A, float& B) {
		float x0 = 0, x1 = 2048;
		ToYV12Y(x0); ToYV12Y(x1);
		float y0 = (x0 - B) / A;
		float y1 = (x1 - B) / A;
		ToYC48Y(y0); ToYC48Y(y1);
		// (0,y0),(2048,y1)を通る直線
		B = y0;
		A = (y1 - y0) / 2048.0f;
	}

	static void ToYC48ABC(float& A, float& B) {
		float x0 = 0, x1 = 2048;
		ToYV12C(x0); ToYV12C(x1);
		float y0 = (x0 - B) / A;
		float y1 = (x1 - B) / A;
		ToYC48C(y0); ToYC48C(y1);
		// (0,y0),(2048,y1)を通る直線
		B = y0;
		A = (y1 - y0) / 2048.0f;
	}

	static void ToOutLGP(LOGO_PIXEL& lgp, float aY, float bY, float aU, float bU, float aV, float bV)
	{
		float A;
		float B;
		float temp;

		// 輝度
		A = aY;
		B = bY;
		ToYC48ABY(A, B);
		if (A == 1) {	// 0での除算回避
			lgp.y = lgp.dp_y = 0;
		}
		else {
			temp = B / (1 - A) + 0.5f;
			if (std::abs(temp)<0x7FFF) {
				// shortの範囲内
				lgp.y = (short)temp;
				temp = (1 - A) * LOGO_MAX_DP + 0.5f;
				if (std::abs(temp)>0x3FFF || short(temp) == 0)
					lgp.y = lgp.dp_y = 0;
				else
					lgp.dp_y = (short)temp;
			}
			else
				lgp.y = lgp.dp_y = 0;
		}

		// 色差(青)
		A = aU;
		B = bU;
		ToYC48ABC(A, B);
		if (A == 1) {
			lgp.cb = lgp.dp_cb = 0;
		}
		else {
			temp = B / (1 - A) + 0.5f;
			if (std::abs(temp)<0x7FFF) {
				// short範囲内
				lgp.cb = (short)temp;
				temp = (1 - A) * LOGO_MAX_DP + 0.5f;
				if (std::abs(temp)>0x3FFF || short(temp) == 0)
					lgp.cb = lgp.dp_cb = 0;
				else
					lgp.dp_cb = (short)temp;
			}
			else
				lgp.cb = lgp.dp_cb = 0;
		}

		// 色差(赤)
		A = aV;
		B = bV;
		ToYC48ABC(A, B);
		if (A == 1) {
			lgp.cr = lgp.dp_cr = 0;
		}
		else {
			temp = B / (1 - A) + 0.5f;
			if (std::abs(temp)<0x7FFF) {
				// short範囲内
				lgp.cr = (short)temp;
				temp = (1 - A) * LOGO_MAX_DP + 0.5f;
				if (std::abs(temp)>0x3FFF || short(temp) == 0)
					lgp.cr = lgp.dp_cr = 0;
				else
					lgp.dp_cr = (short)temp;
			}
			else
				lgp.cr = lgp.dp_cr = 0;
		}
	}

	void WriteBaseLogo(File& file, const LogoHeader* header, const LOGO_PIXEL* data)
	{
		LOGO_FILE_HEADER fh = { 0 };
		strcpy_s(fh.str, LOGO_FILE_HEADER_STR);
		fh.logonum.l = SWAP_ENDIAN(1);
		file.writeValue(fh);

		LOGO_HEADER h = { 0 };
		strncpy_s(h.name, header->name, sizeof(h.name) - 1);
		h.x = header->imgx;
		h.y = header->imgy;
		h.w = header->w;
		h.h = header->h;
		file.writeValue(h);

		size_t sz = header->w * header->h;
		file.write(MemoryChunk((uint8_t*)data, sz * sizeof(data[0])));
	}

	void WriteExtendedLogo(File& file, const LogoHeader* header)
	{
		int wUV = w >> logUVx;
		int hUV = h >> logUVy;
		size_t sz = (header->w * header->h + wUV * hUV * 2) * 2;

		file.writeValue(*header);
		file.write(MemoryChunk((uint8_t*)data.get(), sz * sizeof(float)));
	}

public:
	LogoData() { }

	LogoData(int w, int h, int logUVx, int logUVy)
		: w(w), h(h), logUVx(logUVx), logUVy(logUVy)
	{
		int wUV = w >> logUVx;
		int hUV = h >> logUVy;
		data = std::unique_ptr<float[]>(new float[(w*h + wUV*hUV * 2) * 2]);
		aY = data.get();
		bY = aY + w * h;
		aU = bY + w * h;
		bU = aU + wUV * hUV;
		aV = bU + wUV * hUV;
		bV = aV + wUV * hUV;
	}

	bool isValid() const { return (data != nullptr); }
	int getWidth() const { return w; }
	int getHeight() const { return h; }
	int getLogUVx() const { return logUVx; }
	int getLogUVy() const { return logUVy; }

	float* GetA(int plane) {
		switch (plane) {
		case PLANAR_Y: return aY;
		case PLANAR_U: return aU;
		case PLANAR_V: return aV;
		}
		return nullptr;
	}

	float* GetB(int plane) {
		switch (plane) {
		case PLANAR_Y: return bY;
		case PLANAR_U: return bU;
		case PLANAR_V: return bV;
		}
		return nullptr;
	}

	void Save(const std::string& filepath, const LogoHeader* header)
	{
		// ベース部分作成
		int wUV = w >> logUVx;
		std::vector<LOGO_PIXEL> basedata(header->w * header->h);
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				int off = x + y * w;
				int offUV = (x >> logUVx) + (y >> logUVy) * wUV;
				ToOutLGP(basedata[off], aY[off], bY[off], aU[offUV], bU[offUV], aV[offUV], bV[offUV]);
			}
		}

		File file(filepath, "wb");
		WriteBaseLogo(file, header, basedata.data());
		WriteExtendedLogo(file, header);
	}

	static LogoData Load(const std::string& filepath, LogoHeader* header)
	{
		File file(filepath, "rb");

		// ベース部分をスキップ
		file.readValue<LOGO_FILE_HEADER>();
		LOGO_HEADER h = file.readValue<LOGO_HEADER>();
		file.seek(LOGO_PIXELSIZE(&h), SEEK_CUR);

		*header = file.readValue<LogoHeader>();

		// TODO: magic,versionチェック

		LogoData logo(header->w, header->h, header->logUVx, header->logUVy);

		int wUV = header->w >> header->logUVx;
		int hUV = header->h >> header->logUVy;
		size_t sz = (header->w * header->h + wUV * hUV * 2) * 2;

		file.read(MemoryChunk((uint8_t*)logo.data.get(), sz * sizeof(float)));

		return logo;
	}
};

} // namespace logo
