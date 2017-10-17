﻿#pragma once

#include "TranscodeSetting.hpp"
#include "logo.h"
#include "AMTLogo.hpp"
#include "TsInfo.hpp"
#include "TextOut.h"

#include <cmath>
#include <numeric>
#include <fstream>

namespace logo {

class LogoDataParam : public LogoData
{
	int imgw, imgh, imgx, imgy;
	std::unique_ptr<uint8_t[]> mask;
	float thresh;
	int maskpixels;
public:
	LogoDataParam() { }

	LogoDataParam(LogoData&& logo, const LogoHeader* header)
		: LogoData(std::move(logo))
		, imgw(header->imgw)
		, imgh(header->imgh)
		, imgx(header->imgx)
		, imgy(header->imgy)
	{ }

	LogoDataParam(LogoData&& logo, int imgw, int imgh, int imgx, int imgy)
		: LogoData(std::move(logo))
		, imgw(imgw)
		, imgh(imgh)
		, imgx(imgx)
		, imgy(imgy)
	{ }

	int getImgWidth() const { return imgw; }
	int getImgHeight() const { return imgh; }
	int getImgX() const { return imgx; }
	int getImgY() const { return imgy; }

	uint8_t* GetMask() { return mask.get(); }
	float getThresh() const { return thresh; }
	int getMaskPixels() const { return maskpixels; }

	void CreateLogoMask(float maskratio)
	{
		size_t YSize = w * h;
		mask = std::unique_ptr<uint8_t[]>(new uint8_t[YSize]());
		auto memWork = std::unique_ptr<float[]>(new float[YSize]);

		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				// x切片、つまり、背景の輝度値ゼロのときのロゴの輝度値を取得
				float a = aY[x + y * w];
				float b = bY[x + y * w];
				float c = memWork[x + y * w] = -b / a;
			}
		}

		// マスクされた部分が少なすぎたらしきい値を下げて作り直す
		thresh = 0.25f;
		for (; thresh > 0.01f; thresh = thresh * 0.8f) {
			maskpixels = 0;
			// 3x3 Prewitt
			for (int y = 1; y < h - 1; ++y) {
				for (int x = 1; x < w - 1; ++x) {
					const float* ptr = &memWork[x + y * w];
					float y_sum_h = 0, y_sum_v = 0;

					y_sum_h -= ptr[-1 + w * -1];
					y_sum_h -= ptr[-1];
					y_sum_h -= ptr[-1 + w * 1];
					y_sum_h += ptr[1 + w * -1];
					y_sum_h += ptr[1];
					y_sum_h += ptr[1 + w * 1];
					y_sum_v -= ptr[-1 + w * -1];
					y_sum_v -= ptr[0 + w * -1];
					y_sum_v -= ptr[1 + w * -1];
					y_sum_v += ptr[-1 + w * 1];
					y_sum_v += ptr[0 + w * 1];
					y_sum_v += ptr[1 + w * 1];

					float val = std::sqrtf(y_sum_h * y_sum_h + y_sum_v * y_sum_v);
					maskpixels += mask[x + y * w] = (val > thresh);
				}
			}
			if (maskpixels >= (h - 1)*(w - 1)*maskratio) {
				break;
			}
		}
	}
};

static void approxim_line(int n, double sum_x, double sum_y, double sum_x2, double sum_xy, double& a, double& b)
{
  // doubleやfloatにはNaNが定義されているのでゼロ除算で例外は発生しない
	double temp = (double)n * sum_x2 - sum_x * sum_x;
	a = ((double)n * sum_xy - sum_x * sum_y) / temp;
	b = (sum_x2 * sum_y - sum_x * sum_xy) / temp;
}

class LogoColor
{
	double sumF, sumB, sumF2, sumB2, sumFB;
public:
	LogoColor()
		: sumF()
		, sumB()
		, sumF2()
		, sumB2()
		, sumFB()
	{ }

	// ピクセルの色を追加 f:前景 b:背景
	void Add(int f, int b)
	{
		sumF += f;
		sumB += b;
		sumF2 += f * f;
		sumB2 += b * b;
		sumFB += f * b;
	}

  // 値を0～1に正規化
  void Normalize(int maxv)
  {
    sumF /= (double)maxv;
    sumB /= (double)maxv;
    sumF2 /= (double)maxv*maxv;
    sumB2 /= (double)maxv*maxv;
    sumFB /= (double)maxv*maxv;
  }

	/*====================================================================
	* 	GetAB_?()
	* 		回帰直線の傾きと切片を返す X軸:前景 Y軸:背景
	*===================================================================*/
	bool GetAB(float& A, float& B, int data_count) const
  {
		double A1, A2;
		double B1, B2;
    approxim_line(data_count, sumF, sumB, sumF2, sumFB, A1, B1);
    approxim_line(data_count, sumB, sumF, sumB2, sumFB, A2, B2);

    // XY入れ替えたもの両方で平均を取る
		A = (float)((A1 + (1 / A2)) / 2);   // 傾きを平均
		B = (float)((B1 + (-B2 / A2)) / 2); // 切片も平均

    if (std::isnan(A) || std::isnan(B) || std::isinf(A) || std::isinf(B) || A == 0)
      return false;

		return true;
	}
};

class LogoScan
{
	int scanw;
	int scanh;
	int logUVx;
	int logUVy;
	int thy;

	std::vector<short> tmpY, tmpU, tmpV;

	int nframes;
	std::unique_ptr<LogoColor[]> logoY, logoU, logoV;

	/*--------------------------------------------------------------------
	*	真中らへんを平均
	*-------------------------------------------------------------------*/
	int med_average(const std::vector<short>& s)
	{
		double t = 0;
		int nn = 0;

		int n = (int)s.size();

		// 真中らへんを平均
		for (int i = n / 4; i < n - (n / 4); i++, nn++)
			t += s[i];

		t = (t + nn / 2) / nn;

		return ((int)t);
	}

	static float calcDist(float a, float b) {
		return (1.0f / 3.0f) * (a - 1) * (a - 1) + (a - 1) * b + b * b;
	}

	static void maxfilter(float *data, float *work, int w, int h)
	{
		for (int y = 0; y < h; ++y) {
			work[0 + y * w] = data[0 + y * w];
			for (int x = 1; x < w - 1; ++x) {
				float a = data[x - 1 + y * w];
				float b = data[x + y * w];
				float c = data[x + 1 + y * w];
				work[x + y * w] = std::max(a, std::max(b, c));
			}
			work[w - 1 + y * w] = data[w - 1 + y * w];
		}
		for (int y = 1; y < h - 1; ++y) {
			for (int x = 0; x < w; ++x) {
				float a = data[x + (y - 1) * w];
				float b = data[x + y * w];
				float c = data[x + (y + 1) * w];
				work[x + y * w] = std::max(a, std::max(b, c));
			}
		}
	}

public:
	// thy: オリジナルだとデフォルト30*8=240（8bitだと12くらい？）
	LogoScan(int scanw, int scanh, int logUVx, int logUVy, int thy)
		: scanw(scanw)
		, scanh(scanh)
		, logUVx(logUVx)
		, logUVy(logUVy)
		, thy(thy)
		, nframes()
		, logoY(new LogoColor[scanw*scanh])
		, logoU(new LogoColor[scanw*scanh >> (logUVx + logUVy)])
		, logoV(new LogoColor[scanw*scanh >> (logUVx + logUVy)])
	{
	}

	void Normalize(int mavx)
	{
		int scanUVw = scanw >> logUVx;
		int scanUVh = scanh >> logUVy;

		// 8bitなので255
		for (int y = 0; y < scanh; ++y) {
			for (int x = 0; x < scanw; ++x) {
				logoY[x + y * scanw].Normalize(mavx);
			}
		}
		for (int y = 0; y < scanUVh; ++y) {
			for (int x = 0; x < scanUVw; ++x) {
				logoU[x + y * scanUVw].Normalize(mavx);
				logoV[x + y * scanUVw].Normalize(mavx);
			}
		}
	}

	std::unique_ptr<LogoData> GetLogo(bool clean) const
	{
		int scanUVw = scanw >> logUVx;
		int scanUVh = scanh >> logUVy;
    auto data = std::unique_ptr<LogoData>(new LogoData(scanw, scanh, logUVx, logUVy));
    float *aY = data->GetA(PLANAR_Y);
    float *aU = data->GetA(PLANAR_U);
    float *aV = data->GetA(PLANAR_V);
    float *bY = data->GetB(PLANAR_Y);
    float *bU = data->GetB(PLANAR_U);
    float *bV = data->GetB(PLANAR_V);

		for (int y = 0; y < scanh; ++y) {
			for (int x = 0; x < scanw; ++x) {
				int off = x + y * scanw;
        if (!logoY[off].GetAB(aY[off], bY[off], nframes)) return nullptr;
			}
		}
    for (int y = 0; y < scanUVh; ++y) {
      for (int x = 0; x < scanUVw; ++x) {
        int off = x + y * scanUVw;
        if (!logoU[off].GetAB(aU[off], bU[off], nframes)) return nullptr;
        if (!logoV[off].GetAB(aV[off], bV[off], nframes)) return nullptr;
      }
    }

		if (clean) {
			// ロゴを綺麗にする
			int sizeY = scanw * scanh;
			auto dist = std::unique_ptr<float[]>(new float[sizeY]());
			for (int y = 0; y < scanh; ++y) {
				for (int x = 0; x < scanw; ++x) {
					int off = x + y * scanw;
					int offUV = (x >> logUVx) + (y >> logUVy) * scanUVw;
					dist[off] = calcDist(aY[off], bY[off]) +
						calcDist(aU[offUV], bU[offUV]) +
						calcDist(aV[offUV], bV[offUV]);

					// 値が小さすぎて分かりにくいので大きくしてあげる
					dist[off] *= 1000;
				}
			}

			// maxフィルタを掛ける
			auto work = std::unique_ptr<float[]>(new float[sizeY]);
			maxfilter(dist.get(), work.get(), scanw, scanh);
			maxfilter(dist.get(), work.get(), scanw, scanh);
			maxfilter(dist.get(), work.get(), scanw, scanh);

			// 小さいところはゼロにする
			for (int y = 0; y < scanh; ++y) {
				for (int x = 0; x < scanw; ++x) {
					int off = x + y * scanw;
					int offUV = (x >> logUVx) + (y >> logUVy) * scanUVw;
					if (dist[off] < 0.3f) {
						aY[off] = 1;
						bY[off] = 0;
						aU[offUV] = 1;
						bU[offUV] = 0;
						aV[offUV] = 1;
						bV[offUV] = 0;
					}
				}
			}
		}

		return  data;
	}

	template <typename pixel_t>
	void AddScanFrame(
		const pixel_t* srcY,
		const pixel_t* srcU,
		const pixel_t* srcV,
		int pitchY, int pitchUV,
		int bgY, int bgU, int bgV)
	{
		int scanUVw = scanw >> logUVx;
		int scanUVh = scanh >> logUVy;

		for (int y = 0; y < scanh; ++y) {
			for (int x = 0; x < scanw; ++x) {
				logoY[x + y * scanw].Add(srcY[x + y * pitchY], bgY);
			}
		}
		for (int y = 0; y < scanUVh; ++y) {
			for (int x = 0; x < scanUVw; ++x) {
				logoU[x + y * scanUVw].Add(srcU[x + y * pitchUV], bgU);
				logoV[x + y * scanUVw].Add(srcV[x + y * pitchUV], bgV);
			}
		}

		++nframes;
	}

	template <typename pixel_t>
	bool AddFrame(
		const pixel_t* srcY,
		const pixel_t* srcU,
		const pixel_t* srcV,
		int pitchY, int pitchUV)
	{
		int scanUVw = scanw >> logUVx;
		int scanUVh = scanh >> logUVy;

		tmpY.clear();
		tmpU.clear();
		tmpV.clear();

		tmpY.reserve((scanw + scanh - 1) * 2);
		tmpU.reserve((scanUVw + scanUVh - 1) * 2);
		tmpV.reserve((scanUVw + scanUVh - 1) * 2);

		/*--------------------------------------------------------------------
		*	背景色計算
		*-------------------------------------------------------------------*/

		for (int x = 0; x < scanw; ++x) {
			tmpY.push_back(srcY[x]);
			tmpY.push_back(srcY[x + (scanh - 1) * pitchY]);
		}
		for (int y = 1; y < scanh - 1; ++y) {
			tmpY.push_back(srcY[y * pitchY]);
			tmpY.push_back(srcY[scanw - 1 + y * pitchY]);
		}
		for (int x = 0; x < scanUVw; ++x) {
			tmpU.push_back(srcU[x]);
			tmpU.push_back(srcU[x + (scanUVh - 1) * pitchUV]);
			tmpV.push_back(srcV[x]);
			tmpV.push_back(srcV[x + (scanUVh - 1) * pitchUV]);
		}
		for (int y = 1; y < scanUVh - 1; ++y) {
			tmpU.push_back(srcU[y * pitchUV]);
			tmpU.push_back(srcU[scanUVw - 1 + y * pitchUV]);
			tmpV.push_back(srcV[y * pitchUV]);
			tmpV.push_back(srcV[scanUVw - 1 + y * pitchUV]);
		}

		// 最小と最大が閾値以上離れている場合、単一色でないと判断
		std::sort(tmpY.begin(), tmpY.end());
		if (abs(tmpY.front() - tmpY.back()) > thy) { // オリジナルだと thy * 8
			return false;
		}
		std::sort(tmpU.begin(), tmpU.end());
		if (abs(tmpU.front() - tmpU.back()) > thy) { // オリジナルだと thy * 8
			return false;
		}
		std::sort(tmpV.begin(), tmpV.end());
		if (abs(tmpV.front() - tmpV.back()) > thy) { // オリジナルだと thy * 8
			return false;
		}

		int bgY = med_average(tmpY);
		int bgU = med_average(tmpU);
		int bgV = med_average(tmpV);

		// 有効フレームを追加
		AddScanFrame(srcY, srcU, srcV, pitchY, pitchUV, bgY, bgU, bgV);

		return true;
	}
};

class SimpleVideoReader : AMTObject
{
public:
	SimpleVideoReader(AMTContext& ctx)
		: AMTObject(ctx)
	{ }

	void readAll(const std::string& src)
	{
		using namespace av;

		InputContext inputCtx(src);
		if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
			THROW(FormatException, "avformat_find_stream_info failed");
		}
		AVStream *videoStream = GetVideoStream(inputCtx());
		if (videoStream == NULL) {
			THROW(FormatException, "Could not find video stream ...");
		}
		AVCodecID vcodecId = videoStream->codecpar->codec_id;
		AVCodec *pCodec = avcodec_find_decoder(vcodecId);
		if (pCodec == NULL) {
			THROW(FormatException, "Could not find decoder ...");
		}
		CodecContext codecCtx(pCodec);
		if (avcodec_parameters_to_context(codecCtx(), videoStream->codecpar) != 0) {
			THROW(FormatException, "avcodec_parameters_to_context failed");
		}
		codecCtx()->thread_count = 4;
		if (avcodec_open2(codecCtx(), pCodec, NULL) != 0) {
			THROW(FormatException, "avcodec_open2 failed");
		}

		bool first = true;
		Frame frame;
		AVPacket packet = AVPacket();
		while (av_read_frame(inputCtx(), &packet) == 0) {
			if (packet.stream_index == videoStream->index) {
				if (avcodec_send_packet(codecCtx(), &packet) != 0) {
					THROW(FormatException, "avcodec_send_packet failed");
				}
				while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
					if (first) {
						onFirstFrame(videoStream, frame());
						first = false;
					}
					if (!onFrame(frame())) {
						av_packet_unref(&packet);
						return;
					}
				}
			}
			av_packet_unref(&packet);
		}

		// flush decoder
		if (avcodec_send_packet(codecCtx(), NULL) != 0) {
			THROW(FormatException, "avcodec_send_packet failed");
		}
		while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
			onFrame(frame());
		}
	}

protected:
	virtual void onFirstFrame(AVStream *videoStream, AVFrame* frame) { };
	virtual bool onFrame(AVFrame* frame) { return true; };
};

static void DeintLogo(LogoData& dst, LogoData& src, int w, int h)
{
	const float *srcAY = src.GetA(PLANAR_Y);
	float *dstAY = dst.GetA(PLANAR_Y);
	const float *srcBY = src.GetB(PLANAR_Y);
	float *dstBY = dst.GetB(PLANAR_Y);

	auto merge = [](float a, float b, float c) { return (a + 2 * b + c) / 4.0f; };

	for (int x = 0; x < w; ++x) {
		dstAY[x] = srcAY[x];
		dstBY[x] = srcBY[x];
		dstAY[x + (h - 1) * w] = srcAY[x + (h - 1) * w];
		dstBY[x + (h - 1) * w] = srcBY[x + (h - 1) * w];
	}
	for (int y = 1; y < h - 1; ++y) {
		for (int x = 0; x < w; ++x) {
			dstAY[x + y * w] = merge(
				srcAY[x + (y - 1) * w],
				srcAY[x + y * w],
				srcAY[x + (y + 1) * w]);
			dstBY[x + y * w] = merge(
				srcBY[x + (y - 1) * w],
				srcBY[x + y * w],
				srcBY[x + (y + 1) * w]);
		}
	}
}

template <typename pixel_t>
void DeintY(float* dst, const pixel_t* src, int srcPitch, int w, int h)
{
	auto merge = [](int a, int b, int c) { return (a + 2 * b + c + 2) / 4.0f; };

	for (int x = 0; x < w; ++x) {
		dst[x] = src[x];
		dst[x + (h - 1) * w] = src[x + (h - 1) * srcPitch];
	}
	for (int y = 1; y < h - 1; ++y) {
		for (int x = 0; x < w; ++x) {
			dst[x + y * w] = merge(
				src[x + (y - 1) * srcPitch],
				src[x + y * srcPitch],
				src[x + (y + 1) * srcPitch]);
		}
	}
}

static float EvaluateLogo(const float *src,float maxv, LogoDataParam& logo, float fade, float* work, int w, int h)
{
	// ロゴを評価 //
	const float *logoAY = logo.GetA(PLANAR_Y);
	const float *logoBY = logo.GetB(PLANAR_Y);
	const uint8_t* mask = logo.GetMask();

	// ロゴを除去
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			float srcv = src[x + y * w];
			float a = logoAY[x + y * w];
			float b = logoBY[x + y * w];
			float bg = a * srcv + b * maxv;
			work[x + y * w] = fade * bg + (1 - fade) * srcv;
		}
	}

	// エッジ評価
	float limit = logo.getThresh() * maxv * (25.0f / 9.0f);
	float result = 0;
	for (int y = 2; y < h - 2; ++y) {
		for (int x = 2; x < w - 2; ++x) {
			if (mask[x + y * w]) { // ロゴ輪郭部のみ
				float y_sum_h = 0, y_sum_v = 0;

				// 5x5 Prewitt filter
				// +----------------+  +----------------+
				// | -1 -1 -1 -1 -1 |  | -1 -1  0  1  1 |
				// | -1 -1 -1 -1 -1 |  | -1 -1  0  1  1 |
				// |  0  0  0  0  0 |  | -1 -1  0  1  1 |
				// |  1  1  1  1  1 |  | -1 -1  0  1  1 |
				// |  1  1  1  1  1 |  | -1 -1  0  1  1 |
				// +----------------+  +----------------+
				y_sum_h -= work[x - 2 + (y - 2) * w];
				y_sum_h -= work[x - 2 + (y - 1) * w];
				y_sum_h -= work[x - 2 + (y)* w];
				y_sum_h -= work[x - 2 + (y + 1) * w];
				y_sum_h -= work[x - 2 + (y + 2) * w];
				y_sum_h -= work[x - 1 + (y - 2) * w];
				y_sum_h -= work[x - 1 + (y - 1) * w];
				y_sum_h -= work[x - 1 + (y)* w];
				y_sum_h -= work[x - 1 + (y + 1) * w];
				y_sum_h -= work[x - 1 + (y + 2) * w];
				y_sum_h += work[x + 1 + (y - 2) * w];
				y_sum_h += work[x + 1 + (y - 1) * w];
				y_sum_h += work[x + 1 + (y)* w];
				y_sum_h += work[x + 1 + (y + 1) * w];
				y_sum_h += work[x + 1 + (y + 2) * w];
				y_sum_h += work[x + 2 + (y - 2) * w];
				y_sum_h += work[x + 2 + (y - 1) * w];
				y_sum_h += work[x + 2 + (y)* w];
				y_sum_h += work[x + 2 + (y + 1) * w];
				y_sum_h += work[x + 2 + (y + 2) * w];
				y_sum_v -= work[x - 2 + (y - 1) * w];
				y_sum_v -= work[x - 1 + (y - 1) * w];
				y_sum_v -= work[x + (y - 1) * w];
				y_sum_v -= work[x + 1 + (y - 1) * w];
				y_sum_v -= work[x + 2 + (y - 1) * w];
				y_sum_v -= work[x - 2 + (y - 2) * w];
				y_sum_v -= work[x - 1 + (y - 2) * w];
				y_sum_v -= work[x + (y - 2) * w];
				y_sum_v -= work[x + 1 + (y - 2) * w];
				y_sum_v -= work[x + 2 + (y - 2) * w];
				y_sum_v += work[x - 2 + (y + 1) * w];
				y_sum_v += work[x - 1 + (y + 1) * w];
				y_sum_v += work[x + (y + 1) * w];
				y_sum_v += work[x + 1 + (y + 1) * w];
				y_sum_v += work[x + 2 + (y + 1) * w];
				y_sum_v += work[x - 2 + (y + 2) * w];
				y_sum_v += work[x - 1 + (y + 2) * w];
				y_sum_v += work[x + (y + 2) * w];
				y_sum_v += work[x + 1 + (y + 2) * w];
				y_sum_v += work[x + 2 + (y + 2) * w];

				float val = std::sqrt(y_sum_h * y_sum_h + y_sum_v * y_sum_v);
				//result += val;
				result += std::min(limit, val);
			}
		}
	}

	// 0～1000の値に正規化
	return result / (limit * logo.getMaskPixels()) * 1000.0f;
}

class LogoAnalyzer : AMTObject
{
  const TranscoderSetting& setting_;

	int scanx, scany;
  int scanw, scanh, thy;
	int numMaxFrames;
  int logUVx, logUVy;
	int imgw, imgh;
  int numFrames;
  std::unique_ptr<LogoData> logodata;

	// 今の所可逆圧縮が8bitのみなので対応は8bitのみ
	class InitialLogoCreator : SimpleVideoReader
	{
		LogoAnalyzer* pThis;
		CCodecPointer codec;
		size_t scanDataSize;
		size_t codedSize;
		int readCount;
		std::unique_ptr<uint8_t[]> memScanData;
		std::unique_ptr<uint8_t[]> memCoded;
		std::unique_ptr<LosslessVideoFile> file;
		std::unique_ptr<LogoScan> logoscan;
	public:
		InitialLogoCreator(LogoAnalyzer* pThis)
			: SimpleVideoReader(pThis->ctx)
			, pThis(pThis)
			, codec(make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze")))
			, scanDataSize(pThis->scanw * pThis->scanh * 3 / 2)
			, codedSize(codec->EncodeGetOutputSize(UTVF_YV12, pThis->scanw, pThis->scanh))
			, readCount()
			, memScanData(new uint8_t[scanDataSize])
			, memCoded(new uint8_t[codedSize])
		{ }
		void readAll(const std::string& src)
		{
			SimpleVideoReader::readAll(src);

			codec->EncodeEnd();

			logoscan->Normalize(255);
			pThis->logodata = logoscan->GetLogo(false);
			if (pThis->logodata == nullptr) {
				THROW(RuntimeException, "Insufficient logo frames");
			}
		}
	protected:
		virtual void onFirstFrame(AVStream *videoStream, AVFrame* frame)
		{
			size_t extraSize = codec->EncodeGetExtraDataSize();
			std::vector<uint8_t> extra(extraSize);

			if (codec->EncodeGetExtraData(extra.data(), extraSize, UTVF_YV12, pThis->scanw, pThis->scanh)) {
				THROW(RuntimeException, "failed to EncodeGetExtraData (UtVideo)");
			}
			if (codec->EncodeBegin(UTVF_YV12, pThis->scanw, pThis->scanh, CBGROSSWIDTH_WINDOWS)) {
				THROW(RuntimeException, "failed to EncodeBegin (UtVideo)");
			}

			const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(frame->format));
			
			pThis->logUVx = desc->log2_chroma_w;
			pThis->logUVy = desc->log2_chroma_h;
			pThis->imgw = frame->width;
			pThis->imgh = frame->height;

			file = std::unique_ptr<LosslessVideoFile>(
				new LosslessVideoFile(pThis->ctx, pThis->setting_.getLogoTmpFilePath(), "wb"));
			logoscan = std::unique_ptr<LogoScan>(
				new LogoScan(pThis->scanw, pThis->scanh, pThis->logUVx, pThis->logUVy, pThis->thy));

			// フレーム数は最大フレーム数（実際はそこまで書き込まないこともある）
			file->writeHeader(pThis->scanw, pThis->scanh, pThis->numMaxFrames, extra);

			pThis->numFrames = 0;
		};
		virtual bool onFrame(AVFrame* frame)
		{
			readCount++;

			if (pThis->numFrames >= pThis->numMaxFrames) return false;

			// スキャン部分だけ
			int pitchY = frame->linesize[0];
			int pitchUV = frame->linesize[1];
			int offY = pThis->scanx + pThis->scany * pitchY;
			int offUV = (pThis->scanx >> pThis->logUVx) + (pThis->scany >> pThis->logUVy) * pitchUV;
			const uint8_t* scanY = frame->data[0] + offY;
			const uint8_t* scanU = frame->data[1] + offUV;
			const uint8_t* scanV = frame->data[2] + offUV;

			if (logoscan->AddFrame(scanY, scanU, scanV, pitchY, pitchUV)) {
				++pThis->numFrames;

				// 有効なフレームは保存しておく
				CopyYV12(memScanData.get(), scanY, scanU, scanV, pitchY, pitchUV, pThis->scanw, pThis->scanh);
				bool keyFrame = false;
				size_t codedSize = codec->EncodeFrame(memCoded.get(), &keyFrame, memScanData.get());
				file->writeFrame(memCoded.get(), (int)codedSize);
			}

			if ((readCount % 2000) == 0) printf("%d frames\n", readCount);

			return true;
		};
	};

  void MakeInitialLogo()
  {
		InitialLogoCreator creator(this);
		creator.readAll(setting_.getSrcFilePath());
  }

  void ReMakeLogo()
  {
    // 複数fade値でロゴを評価 //
		auto codec = make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze"));

    // ロゴを評価用にインタレ解除
		LogoDataParam deintLogo(LogoData(scanw, scanh, logUVx, logUVy), scanw, scanh, scanx, scany);
    DeintLogo(deintLogo, *logodata, scanw, scanh);
		deintLogo.CreateLogoMask(0.1f);

		size_t scanDataSize = scanw * scanh * 3 / 2;
		size_t YSize = scanw * scanh;
		size_t codedSize = codec->EncodeGetOutputSize(UTVF_YV12, scanw, scanh);
		size_t extraSize = codec->EncodeGetExtraDataSize();
		auto memScanData = std::unique_ptr<uint8_t[]>(new uint8_t[scanDataSize]);
		auto memCoded = std::unique_ptr<uint8_t[]>(new uint8_t[codedSize]);

		auto memDeint = std::unique_ptr<float[]>(new float[YSize]);
		auto memWork = std::unique_ptr<float[]>(new float[YSize]);

		const int numFade = 20;
		auto minFades = std::unique_ptr<int[]>(new int[numFrames]);
		{
			LosslessVideoFile file(ctx, setting_.getLogoTmpFilePath(), "rb");
			file.readHeader();
			auto extra = file.getExtra();

			if (codec->DecodeBegin(UTVF_YV12, scanw, scanh, CBGROSSWIDTH_WINDOWS, extra.data(), (int)extra.size())) {
				THROW(RuntimeException, "failed to DecodeBegin (UtVideo)");
			}

			// 全フレームループ
			for (int i = 0; i < numFrames; ++i) {
				int64_t codedSize = file.readFrame(i, memCoded.get());
				if (codec->DecodeFrame(memScanData.get(), memCoded.get()) != scanDataSize) {
					THROW(RuntimeException, "failed to DecodeFrame (UtVideo)");
				}
				// フレームをインタレ解除
				DeintY(memDeint.get(), memScanData.get(), scanw, scanw, scanh);
				// fade値ループ
				float minResult = FLT_MAX;
				int minFadeIndex = 0;
				for (int fi = 0; fi < numFade; ++fi) {
					float fade = 0.1f * fi;
					// ロゴを評価
					float result = EvaluateLogo(memDeint.get(), 255.0f, deintLogo, fade, memWork.get(), scanw, scanh);
					if (result < minResult) {
						minResult = result;
						minFadeIndex = fi;
					}
				}
				minFades[i] = minFadeIndex;

				if ((i % 2000) == 0) printf("%d frames\n", i);
			}

			codec->DecodeEnd();
		}

    // 評価値を集約
		// とりあえず出してみる
		std::vector<int> numMinFades(numFade);
		for (int i = 0; i < numFrames; ++i) {
			numMinFades[minFades[i]]++;
		}
		int maxi = (int)(std::max_element(numMinFades.begin(), numMinFades.end()) - numMinFades.begin());
		printf("maxi = %d (%.1f%%)\n", maxi, numMinFades[maxi] / (float)numFrames * 100.0f);

		LogoScan logoscan(scanw, scanh, logUVx, logUVy, thy);
		{
			LosslessVideoFile file(ctx, setting_.getLogoTmpFilePath(), "rb");
			file.readHeader();
			auto extra = file.getExtra();

			if (codec->DecodeBegin(UTVF_YV12, scanw, scanh, CBGROSSWIDTH_WINDOWS, extra.data(), (int)extra.size())) {
				THROW(RuntimeException, "failed to DecodeBegin (UtVideo)");
			}

			int scanUVw = scanw >> logUVx;
			int scanUVh = scanh >> logUVy;
			int offU = scanw * scanh;
			int offV = offU + scanUVw * scanUVh;

			// 全フレームループ
			for (int i = 0; i < numFrames; ++i) {
				int64_t codedSize = file.readFrame(i, memCoded.get());
				if (codec->DecodeFrame(memScanData.get(), memCoded.get()) != scanDataSize) {
					THROW(RuntimeException, "failed to DecodeFrame (UtVideo)");
				}
				// ロゴのあるフレームだけAddFrame
				if (minFades[i] > 8) { // TODO: 調整
					const uint8_t* ptr = memScanData.get();
					logoscan.AddFrame(ptr, ptr + offU, ptr + offV, scanw, scanUVw);
				}

				if ((i % 2000) == 0) printf("%d frames\n", i);
			}

			codec->DecodeEnd();
		}

    // ロゴ作成
		logoscan.Normalize(255);
		logodata = logoscan.GetLogo(true);
		if (logodata == nullptr) {
			THROW(RuntimeException, "Insufficient logo frames");
		}
  }

	int GetServiceId() {
		TsInfo tsinfo(ctx);
		if (tsinfo.ReadFile(setting_.getSrcFilePath().c_str())) {
			if (tsinfo.GetNumProgram() > 0) {
				return tsinfo.GetProgramNumber(0);
			}
		}
		return -1;
	}

public:
  LogoAnalyzer(AMTContext& ctx, const TranscoderSetting& setting)
    : AMTObject(ctx)
    , setting_(setting)
  {
    //
  }

  int ScanLogo()
  {
    sscanf(setting_.getModeArgs().c_str(), "%d,%d,%d,%d,%d,%d",
      &scanx, &scany, &scanw, &scanh, &thy, &numMaxFrames);

		// サービスID取得
		int serviceId = GetServiceId();

		// 有効フレームデータと初期ロゴの取得
    MakeInitialLogo();

    // データ解析とロゴの作り直し
		//MultiCandidate();
		ReMakeLogo();
		ReMakeLogo();
		//ReMakeLogo();

		LogoHeader header(scanw, scanh, logUVx, logUVy, imgw, imgh, scanx, scany, "No Name");
		header.serviceId = serviceId;
		logodata->Save(setting_.getOutFilePath(0) + ".lgd", &header);

    return 0;
  }
};

int ScanLogo(AMTContext& ctx, const TranscoderSetting& setting)
{
  LogoAnalyzer analyzer(ctx, setting);
  return analyzer.ScanLogo();
}


// 黄金比探索 //

template <typename Op, bool shortHead>
float GoldenRatioSearch(float x0, float x1, float x2, float v0, float v1, float v2, Op& op)
{
	assert(x0 < x1);
	assert(x1 < x2);

	if (op.end(x0, x1, x2, v0, v1, v2)) {
		return x1;
	}

	float x3 = x0 + (x2 - x1);
	float v3 = op.f(x3);

	if (shortHead) {
		// 後ろの区間のほうが長い
		if (v3 < v1) {
			// 後ろの区間
			return GoldenRatioSearch<Op, true>(x1, x3, x2, v1, v3, v2, op);
		}
		else {
			// 前の区間
			return GoldenRatioSearch<Op, false>(x0, x1, x3, v0, v1, v3, op);
		}
	}
	else {
		// 前の区間のほうが長い
		if (v3 > v1) {
			// 後ろの区間
			return GoldenRatioSearch<Op, true>(x3, x1, x2, v3, v1, v2, op);
		}
		else {
			// 前の区間
			return GoldenRatioSearch<Op, false>(x0, x3, x1, v0, v3, v1, op);
		}
	}
}

template <typename Op>
float GoldenRatioSearch(float x0, float x1, Op& op)
{
	assert(x0 < x1);

	// 両端
	float v0 = op.f(x0);
	float v1 = op.f(x1);

	// 分割点（黄金比で分割）
	float x2 = x0 + (x1 - x0) * 2 / (3 + std::sqrtf(5.0f));
	float v2 = op.f(x2);

	return GoldenRatioSearch<Op, true>(x0, x2, x1, v0, v2, v1, op);
}

class AMTEraseLogo : public GenericVideoFilter
{
	std::unique_ptr<LogoDataParam> logo;
	std::unique_ptr<LogoDataParam> deintLogo;
	LogoHeader header;
	int mode;
	float maskratio;

	float logothresh;

	template <typename pixel_t>
	void Delogo(pixel_t* dst, int w, int h, int pitch, float maxv, const float* A, const float* B, float fade)
	{
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				float srcv = dst[x + y * pitch];
				float a = A[x + y * w];
				float b = B[x + y * w];
				float bg = a * srcv + b * maxv;
				float tmp = fade * bg + (1 - fade) * srcv;
				dst[x + y * pitch] = (pixel_t)std::min(std::max(tmp + 0.5f, 0.0f), maxv);
			}
		}
	}

	template <typename pixel_t>
	PVideoFrame GetFrameT(int n, IScriptEnvironment2* env)
	{
		size_t YSize = header.w * header.h;
		auto memDeint = std::unique_ptr<float[]>(new float[YSize]);
		auto memWork = std::unique_ptr<float[]>(new float[YSize]);

		PVideoFrame frame = child->GetFrame(n, env);
		env->MakeWritable(&frame);

		float maxv = (float)((1 << vi.BitsPerComponent()) - 1);
		pixel_t* dstY = reinterpret_cast<pixel_t*>(frame->GetWritePtr(PLANAR_Y));
		pixel_t* dstU = reinterpret_cast<pixel_t*>(frame->GetWritePtr(PLANAR_U));
		pixel_t* dstV = reinterpret_cast<pixel_t*>(frame->GetWritePtr(PLANAR_V));

		int pitchY = frame->GetPitch(PLANAR_Y);
		int pitchUV = frame->GetPitch(PLANAR_U);
		int off = header.imgx + header.imgy * pitchY;
		int offUV = (header.imgx >> header.logUVx) + (header.imgy >> header.logUVy) * pitchUV;

		// フレームをインタレ解除
		DeintY(memDeint.get(), dstY + off, pitchY, header.w, header.h);

		if (mode == 0) { // 通常
			// Fade値探索
			struct SearchOp {
				float* img;
				float* work;
				int w, h;
				float maxv;
				float thresh;
				LogoDataParam& logo;
				SearchOp(float* img, float* work, float maxv, float thresh, LogoDataParam& logo, int w, int h)
					: img(img), work(work), maxv(maxv), thresh(thresh), logo(logo), w(w), h(h) { }
				bool end(float x0, float x1, float x2, float v0, float v1, float v2) {
					return (x2 - x0) < 0.01f;
				}
				float f(float x) {
					return EvaluateLogo(img, maxv, logo, x, work, w, h);
				}
			} op(memDeint.get(), memWork.get(), maxv, logothresh, *deintLogo, header.w, header.h);

			// まず、全体を見る
			float minResult = FLT_MAX;
			float minFade = 0;
			for (int fi = 0; fi < 13; ++fi) {
				float fade = 0.1f * fi;
				// ロゴを評価
				float result = op.f(fade);
				if (result < minResult) {
					minResult = result;
					minFade = fade;
				}
			}

			// 最小値付近を細かく見る
			float optimalFade = GoldenRatioSearch(minFade - 0.1f, minFade + 0.1f, op);

			DebugPrint("Fade: %.1f\n", optimalFade * 100.0f);

			// 最適Fade値でロゴ除去
			const float *logoAY = logo->GetA(PLANAR_Y);
			const float *logoBY = logo->GetB(PLANAR_Y);
			const float *logoAU = logo->GetA(PLANAR_U);
			const float *logoBU = logo->GetB(PLANAR_U);
			const float *logoAV = logo->GetA(PLANAR_V);
			const float *logoBV = logo->GetB(PLANAR_V);

			int wUV = (header.w >> header.logUVx);
			int hUV = (header.h >> header.logUVy);

			Delogo(dstY + off, header.w, header.h, pitchY, maxv, logoAY, logoBY, optimalFade);
			Delogo(dstU + offUV, wUV, hUV, pitchUV, maxv, logoAU, logoBU, optimalFade);
			Delogo(dstV + offUV, wUV, hUV, pitchUV, maxv, logoAV, logoBV, optimalFade);

			return frame;
		}

		// ロゴフレームデバッグ用
		float cost0 = EvaluateLogo(memDeint.get(), maxv, *deintLogo, 0, memWork.get(), header.w, header.h);
		float cost1 = EvaluateLogo(memDeint.get(), maxv, *deintLogo, 1, memWork.get(), header.w, header.h);

		char buf[200];
		sprintf_s(buf, "%s %d vs %d", (cost0 <= cost1) ? "X" : "O", (int)cost0, (int)cost1);
		DrawText(frame, true, 0, 0, buf);

		if (mode == 2) {
			const float *logoAY = logo->GetA(PLANAR_Y);
			const float *logoBY = logo->GetB(PLANAR_Y);
			const float *logoAU = logo->GetA(PLANAR_U);
			const float *logoBU = logo->GetB(PLANAR_U);
			const float *logoAV = logo->GetA(PLANAR_V);
			const float *logoBV = logo->GetB(PLANAR_V);

			int wUV = (header.w >> header.logUVx);
			int hUV = (header.h >> header.logUVy);

			Delogo(dstY + off, header.w, header.h, pitchY, maxv, logoAY, logoBY, 1);
			Delogo(dstU + offUV, wUV, hUV, pitchUV, maxv, logoAU, logoBU, 1);
			Delogo(dstV + offUV, wUV, hUV, pitchUV, maxv, logoAV, logoBV, 1);
		}

		return frame;
	}

public:
	AMTEraseLogo(PClip clip, const std::string& logoPath, int mode, float maskratio, IScriptEnvironment* env)
		: GenericVideoFilter(clip)
		, mode(mode)
		, maskratio(maskratio)
	{
		try {
			logo = std::unique_ptr<LogoDataParam>(
				new LogoDataParam(LogoData::Load(logoPath, &header), &header));
		}
		catch (IOException&) {
			env->ThrowError("Failed to read logo file (%s)", logoPath.c_str());
		}

		deintLogo = std::unique_ptr<LogoDataParam>(
			new LogoDataParam(LogoData(header.w, header.h, header.logUVx, header.logUVy), &header));
		DeintLogo(*deintLogo, *logo, header.w, header.h);
		deintLogo->CreateLogoMask(maskratio);
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env_)
	{
		IScriptEnvironment2* env = static_cast<IScriptEnvironment2*>(env_);

		int pixelSize = vi.ComponentSize();
		switch (pixelSize) {
		case 1:
			return GetFrameT<uint8_t>(n, env);
		case 2:
			return GetFrameT<uint16_t>(n, env);
		default:
			env->ThrowError("[AMTEraseLogo] Unsupported pixel format");
		}

		return PVideoFrame();
	}

	static AVSValue __cdecl Create(AVSValue args, void* user_data, IScriptEnvironment* env)
	{
		return new AMTEraseLogo(
			args[0].AsClip(),       // source
			args[1].AsString(),			// logopath
			args[2].AsInt(0),       // mode
			(float)args[3].AsFloat(10), // maskratio
			env
		);
	}
};

class LogoFrame
{
	int numLogos;
	std::unique_ptr<LogoDataParam[]> logoArr;
	std::unique_ptr<LogoDataParam[]> deintArr;

	int maxYSize;
	int numFrames;
	int framesPerSec;
	VideoInfo vi;

	struct EvalResult {
		float cost0, cost1;
	};
	std::unique_ptr<EvalResult[]> evalResults;

	int bestLogo;
	float logoRatio;

	template <typename pixel_t>
	void ScanFrame(PVideoFrame& frame, float* memDeint, float* memWork, float maxv, EvalResult* outResult)
	{
		const pixel_t* dstY = reinterpret_cast<const pixel_t*>(frame->GetReadPtr(PLANAR_Y));
		const pixel_t* dstU = reinterpret_cast<const pixel_t*>(frame->GetReadPtr(PLANAR_U));
		const pixel_t* dstV = reinterpret_cast<const pixel_t*>(frame->GetReadPtr(PLANAR_V));

		int pitchY = frame->GetPitch(PLANAR_Y);
		int pitchUV = frame->GetPitch(PLANAR_U);

		for (int i = 0; i < numLogos; ++i) {
			LogoDataParam& logo = deintArr[i];
			if (logo.isValid() == false ||
				logo.getImgWidth() != vi.width ||
				logo.getImgHeight() != vi.height)
			{
				outResult[i].cost0 = 0;
				outResult[i].cost1 = 1;
				continue;
			}

			int off = logo.getImgX() + logo.getImgY() * pitchY;
			int offUV = (logo.getImgX() >> logo.getLogUVx()) + (logo.getImgY() >> logo.getLogUVy()) * pitchUV;

			// フレームをインタレ解除
			DeintY(memDeint, dstY + off, pitchY, logo.getWidth(), logo.getHeight());

			// ロゴ評価
			outResult[i].cost0 = EvaluateLogo(memDeint, maxv, logo, 0, memWork, logo.getWidth(), logo.getHeight());
			outResult[i].cost1 = EvaluateLogo(memDeint, maxv, logo, 1, memWork, logo.getWidth(), logo.getHeight());
		}
	}

	template <typename pixel_t>
	void IterateFrames(PClip clip, IScriptEnvironment2* env)
	{
		auto memDeint = std::unique_ptr<float[]>(new float[maxYSize]);
		auto memWork = std::unique_ptr<float[]>(new float[maxYSize]);
		float maxv = (float)((1 << vi.BitsPerComponent()) - 1);
		evalResults = std::unique_ptr<EvalResult[]>(new EvalResult[vi.num_frames]);
		for (int n = 0; n < vi.num_frames; ++n) {
			PVideoFrame frame = clip->GetFrame(n, env);
			ScanFrame<pixel_t>(frame, memDeint.get(), memWork.get(), maxv, &evalResults[n * numLogos]);
		}
		numFrames = vi.num_frames;
		framesPerSec = (int)std::round((float)vi.fps_numerator / vi.fps_denominator);
	}

public:
	LogoFrame(const std::vector<std::string>& logofiles, float maskratio)
	{
		numLogos = (int)logofiles.size();
		logoArr = std::unique_ptr<LogoDataParam[]>(new LogoDataParam[logofiles.size()]);
		deintArr = std::unique_ptr<LogoDataParam[]>(new LogoDataParam[logofiles.size()]);

		maxYSize = 0;
		for (int i = 0; i < (int)logofiles.size(); ++i) {
			try {
				LogoHeader header;
				logoArr[i] = LogoDataParam(LogoData::Load(logofiles[i], &header), &header);
				deintArr[i] = LogoDataParam(LogoData(header.w, header.h, header.logUVx, header.logUVy), &header);
				DeintLogo(deintArr[i], logoArr[i], header.w, header.h);
				deintArr[i].CreateLogoMask(maskratio);

				int YSize = header.w * header.h;
				maxYSize = std::max(maxYSize, YSize);
			}
			catch (IOException&) {
				// 読み込みエラーは無視
			}
		}
	}

	void scanFrames(PClip clip, IScriptEnvironment2* env)
	{
		vi = clip->GetVideoInfo();
		int pixelSize = vi.ComponentSize();
		switch (pixelSize) {
		case 1:
			return IterateFrames<uint8_t>(clip, env);
		case 2:
			return IterateFrames<uint16_t>(clip, env);
		default:
			env->ThrowError("[LogoFrame] Unsupported pixel format");
		}
	}

	void writeResult(const std::string& outpath)
	{
		const int radius = 5; // 10秒ウィンドウで移動平均
		int windowFrames = framesPerSec * radius;
		// 検出フレーム数の合計
		std::vector<int> logoFrames(numLogos);
		for (int n = 0; n < numFrames; ++n) {
			for (int i = 0; i < numLogos; ++i) {
				auto& r = evalResults[n * numLogos + i];
				logoFrames[i] += (r.cost1 < r.cost0);
			}
		}
		bestLogo = (int)(std::max_element(logoFrames.begin(), logoFrames.end()) - logoFrames.begin());
		auto calcScore = [](EvalResult r) {
			return (r.cost1 < r.cost0) * 2;
		};
		std::vector<int> frameScores(numFrames + windowFrames * 2);
		// 両端を1にする
		std::fill_n(frameScores.begin(), windowFrames, 1);
		std::fill_n(frameScores.end() - windowFrames, windowFrames, 1);
		// スコアを入れる
		for (int n = 0; n < numFrames; ++n) {
			frameScores[windowFrames + n] = calcScore(evalResults[n * numLogos + bestLogo]);
		}
		// 初期和を計算
		int sum = std::accumulate(frameScores.begin(), frameScores.begin() + windowFrames * 2, 0);
		// 移動平均を計算
		std::vector<bool> avgScores(numFrames);
		for (int n = 0; n < numFrames; ++n) {
			sum += frameScores[windowFrames * 2 + n];
			avgScores[n] = (sum >= windowFrames * 2);
			sum -= frameScores[n];
		}
		// ロゴ区間を出力
		std::vector<int> logosec;
		bool prevLogo = false;
		for (int n = 0; n < numFrames; ++n) {
			int idx = n;
			if (prevLogo == false && avgScores[n]) {
				// ロゴ開始
				if (frameScores[windowFrames + n]) {
					// 既にロゴがあるのでないところまで遡る
					for (; frameScores[windowFrames + idx]; --idx);
					logosec.push_back(idx + 1);
				}
				else {
					// まだロゴがないのであるところまで辿る
					for (; !frameScores[windowFrames + idx]; ++idx);
					logosec.push_back(idx);
				}
				prevLogo = true;
				n += windowFrames;
			}
			else if (prevLogo && avgScores[n] == false) {
				// ロゴ終了
				if (frameScores[windowFrames + n]) {
					// まだロゴがあるのでないところまで辿る
					for (; frameScores[windowFrames + idx]; ++idx);
					logosec.push_back(idx);
				}
				else {
					// 既にロゴがないのであるところまで遡る
					for (; !frameScores[windowFrames + idx]; --idx);
					logosec.push_back(idx + 1);
				}
				prevLogo = false;
				n += windowFrames;
			}
		}
		if (prevLogo) {
			// ありで終わったら最後の区間を出力
			logosec.push_back(numFrames);
		}

		std::ofstream os(outpath, std::ios::out);
		int numLogoFrames = 0;
		os << std::setw(5) << std::right;
		for (int i = 0; i < (int)logosec.size(); i += 2) {
			int start = logosec[i];
			int end = logosec[i + 1];
			numLogoFrames += end - start;
			os << start << " S 0 ALL " << start << " " << start << std::endl;
			os << end << " E 0 ALL " << end << " " << end << std::endl;
		}
		os.close();
		logoRatio = (float)numLogoFrames / numFrames;
	}

	int getBestLogo() const {
		return bestLogo;
	}

	float getLogoRatio() const {
		return logoRatio;
	}
};

} // namespace logo
