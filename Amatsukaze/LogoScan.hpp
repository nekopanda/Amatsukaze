/**
* Amtasukaze Logo Analyze
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*
* ただし、approxim_line(),GetAB(),med_average()は
* MakKi氏の透過性ロゴ フィルタプラグインより拝借
* https://github.com/makiuchi-d/delogo-aviutl
*/
#pragma once

#include "TranscodeSetting.hpp"
#include "logo.h"
#include "AMTLogo.hpp"
#include "TsInfo.hpp"
#include "TextOut.h"

#include <cmath>
#include <numeric>
#include <fstream>

float CalcCorrelation5x5(const float* k, const float* Y, int x, int y, int w, float* pavg)
{
	float avg = 0.0f;
	for (int ky = -2; ky <= 2; ++ky) {
		for (int kx = -2; kx <= 2; ++kx) {
			avg += Y[(x + kx) + (y + ky) * w];
		}
	}
	avg /= 25;
	float sum = 0.0f;
	for (int ky = -2; ky <= 2; ++ky) {
		for (int kx = -2; kx <= 2; ++kx) {
			sum += k[(kx + 2) + (ky + 2) * 5] * (Y[(x + kx) + (y + ky) * w] - avg);
		}
	}
	if (pavg) *pavg = avg;
	return sum;
};

// ComputeKernel.cpp
bool IsAVXAvailable();
float CalcCorrelation5x5_AVX(const float* k, const float* Y, int x, int y, int w, float* pavg);

#if 0
float CalcCorrelation5x5_Debug(const float* k, const float* Y, int x, int y, int w, float* pavg)
{
	float f0 = CalcCorrelation5x5(k, Y, x, y, w, pavg);
	float f1 = CalcCorrelation5x5_AVX(k, Y, x, y, w, pavg);
	if (f0 != f1) {
		printf("Error!!!\n");
	}
	return f1;
}
#endif

namespace logo {

class LogoDataParam : public LogoData
{
  enum {
		KSIZE = 5,
		KLEN = KSIZE * KSIZE,
		CSHIFT = 3,
		CLEN = 256 >> CSHIFT
	};
	int imgw, imgh, imgx, imgy; // この4つはすべて2の倍数
	std::unique_ptr<uint8_t[]> mask;
	std::unique_ptr<float[]> kernels;
	struct ScaleLimit {
		float scale;   // 正規化用スケール（想定される相関が1になるようにするため）
		float scale2;  // キャップ用スケール（想定される相関が小さすぎる場合に値を小さくするため）
	};
	std::unique_ptr<ScaleLimit[]> scales;
	float thresh;
	int maskpixels;
	float blackScore;

	float(*pCalcCorrelation5x5)(const float* k, const float* Y, int x, int y, int w, float* pavg);
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

  const uint8_t* GetMask() { return mask.get(); }
  const float* GetKernels() { return kernels.get(); }
	float getThresh() const { return thresh; }
	int getMaskPixels() const { return maskpixels; }

	// 評価準備
	void CreateLogoMask(float maskratio)
	{
		// ロゴカーネルの考え方
		// ロゴとの相関を取りたい
		// これだけならロゴと画像を単に画素ごとに掛け合わせて足せばいいが
		// それだと、画像背景の濃淡に大きく影響を受けてしまう
		// なので、ロゴのエッジだけ画素ごとに相関を見ていく

		// 相関下限パラメータ
		const float corrLowerLimit = 0.2f;

		pCalcCorrelation5x5 = IsAVXAvailable() ? CalcCorrelation5x5_AVX : CalcCorrelation5x5;

		int YSize = w * h;
		auto memWork = std::unique_ptr<float[]>(new float[YSize * CLEN + 8]);

		// 各単色背景にロゴを乗せる
		for (int c = 0; c < CLEN; ++c) {
			float *slice = &memWork[c * YSize];
			std::fill_n(slice, YSize, (float)(c << CSHIFT));
			AddLogo(slice, 255);
		}

		auto makeKernel = [](float* k, float* Y, int x, int y, int w) {
			// コピー
			for (int ky = -2; ky <= 2; ++ky) {
				for (int kx = -2; kx <= 2; ++kx) {
					k[(kx + 2) + (ky + 2) * KSIZE] = Y[(x + kx) + (y + ky) * w];
				}
			}
			// 平均値
			float avg = std::accumulate(k, k + KLEN, 0.0f) / KLEN;
			// 平均値をゼロにする
			std::transform(k, k + KLEN, 
				stdext::checked_array_iterator<float*>(k, KLEN), [=](float p) { return p - avg; });
		};

    // 特徴点の抽出 //
		// 単色背景にロゴを乗せた画像の各ピクセルを中心とする5x5ウィンドウの
		// 画素値の分散の大きい順にmaskratio割合のピクセルを着目点とする
		std::vector<std::pair<float, int>> variance(YSize);
		// 各ピクセルの分散を計算（計算されていないところはゼロ初期化されてる）
		for (int y = 2; y < h - 2; ++y) {
			for (int x = 2; x < w - 2; ++x) {
				// 真ん中の色を取る
				float *slice = &memWork[(CLEN >> 1) * YSize];
				float k[KLEN];
				makeKernel(k, slice, x, y, w);
				variance[x + y * w].first = std::accumulate(k, k + KLEN, 0.0f,
					[](float sum, float val) { return sum + val * val; });
			}
		}
		// ピクセルインデックスを生成
		for (int i = 0; i < YSize; ++i) {
			variance[i].second = i;
		}
		// 降順ソート
		std::sort(variance.begin(), variance.end(), std::greater<std::pair<float, int>>());
		// 計算結果からmask生成
		mask = std::unique_ptr<uint8_t[]>(new uint8_t[YSize]());
		maskpixels = std::min(YSize, (int)(YSize * maskratio));
		for (int i = 0; i < maskpixels; ++i) {
			mask[variance[i].second] = 1;
		}
#if 0
		WriteGrayBitmap("hoge.bmp", w, h, [&](int x, int y) {
			return mask[x + y * w] ? 255 : 0;
		});
#endif

    // ピクセル周辺の特徴
    kernels = std::unique_ptr<float[]>(new float[maskpixels * KLEN + 8]);
		// 各ピクセルx各単色背景での相関値スケール
		scales = std::unique_ptr<ScaleLimit[]>(new ScaleLimit[maskpixels * CLEN]);
    int count = 0;
		float avgCorr = 0.0f;
    for (int y = 2; y < h - 2; ++y) {
      for (int x = 2; x < w - 2; ++x) {
        if (mask[x + y * w]) {
					float* k = &kernels[count * KLEN];
					ScaleLimit* s = &scales[count * CLEN];
					makeKernel(k, memWork.get(), x, y, w);
					for (int i = 0; i < CLEN; ++i) {
						float *slice = &memWork[i * YSize];
						avgCorr += s[i].scale = std::abs(pCalcCorrelation5x5(k, slice, x, y, w, nullptr));
					}
					++count;
        }
      }
    }
		avgCorr /= maskpixels * CLEN;
		// 相関下限（これより小さい相関のピクセルはスケールしない）
		float limitCorr = avgCorr * corrLowerLimit;
		for (int i = 0; i < maskpixels * CLEN; ++i) {
			float corr = scales[i].scale;
			scales[i].scale = (corr > 0) ? (1.0f / corr) : 0.0f;
			scales[i].scale2 = std::min(1.0f, corr / limitCorr);
		}

#if 0
    // ロゴカーネルをチェック
    for (int idx = 0; idx < count; ++idx) {
      float* k = &kernels[idx++ * KLEN];
      float sum = std::accumulate(k, k + KLEN, 0.0f);
      //float abssum = std::accumulate(k, k + KLEN, 0.0f,
      //  [](float sum, float val) { return sum + std::abs(val); });
      // チェック
      if (std::abs(sum) > 0.00001f /*&& std::abs(abssum - 1.0f) > 0.00001f*/) {
        //printf("Error: %d => sum: %f, abssum: %f\n", idx, sum, abssum);
				printf("Error: %d => sum: %f\n", idx, sum);
      }
    }
#endif

		// 黒背景の評価値（これがはっきり出たときの基準）
		float *slice = &memWork[(16 >> CSHIFT) * YSize];
		blackScore = CorrelationScore(slice, 255);
	}

	float EvaluateLogo(const float *src, float maxv, float fade, float* work, int stride = -1)
	{
		// ロゴを評価 //
		const float *logoAY = GetA(PLANAR_Y);
		const float *logoBY = GetB(PLANAR_Y);

		if (stride == -1) {
			stride = w;
		}

		// ロゴを除去
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				float srcv = src[x + y * stride];
				float a = logoAY[x + y * w];
				float b = logoBY[x + y * w];
				float bg = a * srcv + b * maxv;
				float dstv = fade * bg + (1 - fade) * srcv;
				work[x + y * w] = dstv;
			}
		}

		// 正規化
		return CorrelationScore(work, maxv) / blackScore;
	}

	std::unique_ptr<LogoDataParam> MakeFieldLogo(bool bottom)
	{
		auto logo = std::unique_ptr<LogoDataParam>(
			new LogoDataParam(LogoData(w, h / 2, logUVx, logUVy), imgw, imgh / 2, imgx, imgy / 2));
		
		for (int y = 0; y < logo->h; ++y) {
			for (int x = 0; x < logo->w; ++x) {
				logo->aY[x + y * w] = aY[x + (bottom + y * 2) * w];
				logo->bY[x + y * w] = bY[x + (bottom + y * 2) * w];
			}
		}

		int UVoffset = ((int)bottom ^ (logo->imgy % 2));
		int wUV = logo->w >> logUVx;
		int hUV = logo->h >> logUVy;

		for (int y = 0; y < hUV; ++y) {
			for (int x = 0; x < wUV; ++x) {
				logo->aU[x + y * wUV] = aU[x + (UVoffset + y * 2) * wUV];
				logo->bU[x + y * wUV] = bU[x + (UVoffset + y * 2) * wUV];
				logo->aV[x + y * wUV] = aV[x + (UVoffset + y * 2) * wUV];
				logo->bV[x + y * wUV] = bV[x + (UVoffset + y * 2) * wUV];
			}
		}

		return logo;
	}

private:

  // 画素ごとにロゴとの相関を計算
  float CorrelationScore(const float *work, float maxv)
  {
    const uint8_t* mask = GetMask();
    const float* kernels = GetKernels();

    // ロゴとの相関を評価
    int count = 0;
    float result = 0;
    for (int y = 2; y < h - 2; ++y) {
      for (int x = 2; x < w - 2; ++x) {
        if (mask[x + y * w]) {
          const float* k = &kernels[count * KLEN];

					float avg;
					float sum = pCalcCorrelation5x5(k, work, x, y, w, &avg);
					// avg単色の場合の相関値が1になるように正規化
					ScaleLimit s = scales[count * CLEN + (std::max(0, std::min(255, (int)avg)) >> CSHIFT)];
					// 1を超える部分は捨てる（ロゴによる相関ではない部分なので）
					float normalized = std::max(-1.0f, std::min(1.0f, sum * s.scale));
					// 相関が下限値以下の場合は一部元に戻す
					float score = normalized * s.scale2;

					result += score;

					++count;
        }
      }
    }

    return result;
  }

	void AddLogo(float* Y, int maxv)
	{
		const float *logoAY = GetA(PLANAR_Y);
		const float *logoBY = GetB(PLANAR_Y);
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				float a = logoAY[x + y * w];
				float b = logoBY[x + y * w];
				if (a > 0) {
					Y[x + y * w] = (Y[x + y * w] - b * maxv) / a;
				}
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
			
			// ロゴ周辺を消す
			// メリット
			//   ロゴを消したときにロゴを含む長方形がうっすら出るのが防げる
			// デメリット
			//   ロゴ周辺のノイズっぽい成分はソース画像の圧縮により発生したノイズである
			//   ロゴの圧縮により発生した平均値が現れているので、
			//   これを残すことによりロゴ除去すると周辺のノイズも低減できるが、
			//   消してしまうとノイズは残ることになる

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

	int64_t currentPos;

	void readAll(const tstring& src, int serviceid)
	{
		using namespace av;

		InputContext inputCtx(src);
		if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
			THROW(FormatException, "avformat_find_stream_info failed");
		}
		AVStream *videoStream = av::GetVideoStream(inputCtx(), serviceid);
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
		codecCtx()->thread_count = GetFFmpegThreads(GetProcessorCount() - 2);
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
					currentPos = packet.pos;
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

template <typename pixel_t>
void CopyY(float* dst, const pixel_t* src, int srcPitch, int w, int h)
{
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			dst[x + y * w] = src[x + y * srcPitch];
		}
	}
}

typedef bool(*LOGO_ANALYZE_CB)(float progress, int nread, int total, int ngather);

class LogoAnalyzer : AMTObject
{
  tstring srcpath;
	int serviceid;

  tstring workfile;
  tstring dstpath;
	LOGO_ANALYZE_CB cb;

	int scanx, scany;
	int scanw, scanh, thy;
	int numMaxFrames;
	int logUVx, logUVy;
	int imgw, imgh;
	int numFrames;
	std::unique_ptr<LogoData> logodata;

	float progressbase;

	// 今の所可逆圧縮が8bitのみなので対応は8bitのみ
	class InitialLogoCreator : SimpleVideoReader
	{
		LogoAnalyzer* pThis;
		CCodecPointer codec;
		size_t scanDataSize;
		size_t codedSize;
		int readCount;
		int64_t filesize;
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
		void readAll(const tstring& src, int serviceid)
		{
			{ File file(src, _T("rb")); filesize = file.size(); }

			SimpleVideoReader::readAll(src, serviceid);

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
				new LosslessVideoFile(pThis->ctx, pThis->workfile, _T("wb")));
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

			if ((readCount % 200) == 0) {
				float progress = (float)currentPos / filesize * 50;
				if (pThis->cb(progress, readCount, 0, pThis->numFrames) == false) {
					THROW(RuntimeException, "Cancel requested");
				}
			}

			return true;
		};
	};

	void MakeInitialLogo()
	{
		InitialLogoCreator creator(this);
		creator.readAll(srcpath, serviceid);
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

		auto memDeint = std::unique_ptr<float[]>(new float[YSize + 8]);
		auto memWork = std::unique_ptr<float[]>(new float[YSize + 8]);

		const int numFade = 20;
		auto minFades = std::unique_ptr<int[]>(new int[numFrames]);
		{
			LosslessVideoFile file(ctx, workfile, _T("rb"));
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
					float result = std::abs(deintLogo.EvaluateLogo(memDeint.get(), 255.0f, fade, memWork.get()));
					if (result < minResult) {
						minResult = result;
						minFadeIndex = fi;
					}
				}
				minFades[i] = minFadeIndex;

				if ((i % 100) == 0) {
					float progress = (float)i / numFrames * 25 + progressbase;
					if (cb(progress, i, numFrames, numFrames) == false) {
						THROW(RuntimeException, "Cancel requested");
					}
				}
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
			LosslessVideoFile file(ctx, workfile, _T("rb"));
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

public:
	LogoAnalyzer(AMTContext& ctx, const tchar* srcpath, int serviceid, const tchar* workfile, const tchar* dstpath,
			int imgx, int imgy, int w, int h, int thy, int numMaxFrames,
			LOGO_ANALYZE_CB cb)
		: AMTObject(ctx)
		, srcpath(srcpath)
		, serviceid(serviceid)
		, workfile(workfile)
		, dstpath(dstpath)
		, scanx(imgx)
		, scany(imgy)
		, scanw(w)
		, scanh(h)
		, thy(thy)
		, numMaxFrames(numMaxFrames)
		, cb(cb)
	{
		//
	}

	void ScanLogo()
	{
		// 有効フレームデータと初期ロゴの取得
		progressbase = 0;
		MakeInitialLogo();

		// データ解析とロゴの作り直し
		progressbase = 50;
		//MultiCandidate();
		ReMakeLogo();
		progressbase = 75;
		ReMakeLogo();
		//ReMakeLogo();

		if (cb(1, numFrames, numFrames, numFrames) == false) {
			THROW(RuntimeException, "Cancel requested");
		}

		LogoHeader header(scanw, scanh, logUVx, logUVy, imgw, imgh, scanx, scany, "No Name");
		header.serviceId = serviceid;
		logodata->Save(dstpath, &header);
	}
};

// C API for P/Invoke
extern "C" __declspec(dllexport) int ScanLogo(AMTContext* ctx,
	const tchar* srcpath, int serviceid, const tchar* workfile, const tchar* dstpath,
	int imgx, int imgy, int w, int h, int thy, int numMaxFrames,
	LOGO_ANALYZE_CB cb)
{
	try {
		LogoAnalyzer analyzer(*ctx,
			srcpath, serviceid, workfile, dstpath, imgx, imgy, w, h, thy, numMaxFrames, cb);
		analyzer.ScanLogo();
		return true;
	}
	catch (const Exception& exception) {
		ctx->setError(exception);
	}
	return false;
}

struct LogoAnalyzeFrame
{
	float p[11], t[11], b[11];
};

// ロゴ除去用解析フィルタ
class AMTAnalyzeLogo : public GenericVideoFilter
{
	VideoInfo srcvi;

	std::unique_ptr<LogoDataParam> logo;
	std::unique_ptr<LogoDataParam> deintLogo;
	std::unique_ptr<LogoDataParam> fieldLogoT;
	std::unique_ptr<LogoDataParam> fieldLogoB;
	LogoHeader header;
	float maskratio;

	float logothresh;

	template <typename pixel_t>
	PVideoFrame GetFrameT(int n, IScriptEnvironment2* env)
	{
		size_t YSize = header.w * header.h;
		auto memCopy = std::unique_ptr<float[]>(new float[YSize + 8]);
		auto memDeint = std::unique_ptr<float[]>(new float[YSize + 8]);
		auto memWork = std::unique_ptr<float[]>(new float[YSize + 8]);

		PVideoFrame dst = env->NewVideoFrame(vi);
		LogoAnalyzeFrame* pDst = reinterpret_cast<LogoAnalyzeFrame*>(dst->GetWritePtr());

		float maxv = (float)((1 << srcvi.BitsPerComponent()) - 1);

		for (int i = 0; i < 8; ++i) {
			int nsrc = std::max(0, std::min(srcvi.num_frames - 1, n * 8 + i));
			PVideoFrame frame = child->GetFrame(nsrc, env);

			const pixel_t* srcY = reinterpret_cast<const pixel_t*>(frame->GetReadPtr(PLANAR_Y));
			const pixel_t* srcU = reinterpret_cast<const pixel_t*>(frame->GetReadPtr(PLANAR_U));
			const pixel_t* srcV = reinterpret_cast<const pixel_t*>(frame->GetReadPtr(PLANAR_V));

			int pitchY = frame->GetPitch(PLANAR_Y) / sizeof(pixel_t);
			int pitchUV = frame->GetPitch(PLANAR_U) / sizeof(pixel_t);
			int off = header.imgx + header.imgy * pitchY;
			int offUV = (header.imgx >> header.logUVx) + (header.imgy >> header.logUVy) * pitchUV;

			CopyY(memCopy.get(), srcY + off, pitchY, header.w, header.h);

			// フレームをインタレ解除
			DeintY(memDeint.get(), srcY + off, pitchY, header.w, header.h);

			LogoAnalyzeFrame info;
			for (int f = 0; f <= 10; ++f) {
				info.p[f] = std::abs(deintLogo->EvaluateLogo(memDeint.get(), maxv, (float)f / 10.0f, memWork.get()));
				info.t[f] = std::abs(fieldLogoT->EvaluateLogo(memCopy.get(), maxv, (float)f / 10.0f, memWork.get(), header.w * 2));
				info.b[f] = std::abs(fieldLogoB->EvaluateLogo(memCopy.get() + header.w, maxv, (float)f / 10.0f, memWork.get(), header.w * 2));
			}

			pDst[i] = info;
		}

		return dst;
	}

public:
	AMTAnalyzeLogo(PClip clip, const tstring& logoPath, float maskratio, IScriptEnvironment* env)
		: GenericVideoFilter(clip)
		, srcvi(vi)
		, maskratio(maskratio)
	{
		try {
			logo = std::unique_ptr<LogoDataParam>(
				new LogoDataParam(LogoData::Load(logoPath, &header), &header));
		}
		catch (const IOException&) {
			env->ThrowError("Failed to read logo file (%s)", logoPath.c_str());
		}

		deintLogo = std::unique_ptr<LogoDataParam>(
			new LogoDataParam(LogoData(header.w, header.h, header.logUVx, header.logUVy), &header));
		DeintLogo(*deintLogo, *logo, header.w, header.h);
		deintLogo->CreateLogoMask(maskratio);

		fieldLogoT = logo->MakeFieldLogo(false);
		fieldLogoT->CreateLogoMask(maskratio);
		fieldLogoB = logo->MakeFieldLogo(true);
		fieldLogoB->CreateLogoMask(maskratio);

		// for debug
		//LogoHeader hT = header;
		//hT.h /= 2;
		//hT.imgh /= 2;
		//hT.imgy /= 2;
		//fieldLogoT->Save("logoT.lgd", &hT);
		//fieldLogoB->Save("logoB.lgd", &hT);

		int out_bytes = sizeof(LogoAnalyzeFrame) * 8;
		vi.pixel_type = VideoInfo::CS_BGR32;
		vi.width = 64;
		vi.height = nblocks(out_bytes, vi.width * 4);

		vi.num_frames = nblocks(vi.num_frames, 8);
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env_)
	{
		IScriptEnvironment2* env = static_cast<IScriptEnvironment2*>(env_);

		int pixelSize = srcvi.ComponentSize();
		switch (pixelSize) {
		case 1:
			return GetFrameT<uint8_t>(n, env);
		case 2:
			return GetFrameT<uint16_t>(n, env);
		default:
			env->ThrowError("[AMTAnalyzeLogo] Unsupported pixel format");
		}

		return PVideoFrame();
	}

	int __stdcall SetCacheHints(int cachehints, int frame_range) {
		if (cachehints == CACHE_GET_MTMODE) {
			return MT_NICE_FILTER;
		}
		return 0;
	}

	static AVSValue __cdecl Create(AVSValue args, void* user_data, IScriptEnvironment* env)
	{
		return new AMTAnalyzeLogo(
			args[0].AsClip(),       // source
			to_tstring(args[1].AsString()),			// logopath
			(float)args[2].AsFloat(35) / 100.0f, // maskratio
			env
		);
	}
};

class AMTEraseLogo : public GenericVideoFilter
{
	PClip analyzeclip;

	std::vector<int> frameResult;
	std::unique_ptr<LogoDataParam> logo;
	LogoHeader header;
	int mode;

	template <typename pixel_t>
	void Delogo(pixel_t* dst, int w, int h, int logopitch, int imgpitch, float maxv, const float* A, const float* B, float fade)
	{
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				float srcv = dst[x + y * imgpitch];
				float a = A[x + y * logopitch];
				float b = B[x + y * logopitch];
				float bg = a * srcv + b * maxv;
				float tmp = fade * bg + (1 - fade) * srcv;
				dst[x + y * imgpitch] = (pixel_t)std::min(std::max(tmp + 0.5f, 0.0f), maxv);
			}
		}
	}

	void CalcFade2(int n, float& fadeT, float& fadeB, IScriptEnvironment2* env)
	{
		enum {
			DIST = 4,
		};
		LogoAnalyzeFrame frames[DIST * 2 + 1];

		int prev_n = INT_MAX;
		PVideoFrame frame;
		for (int i = -DIST; i <= DIST; ++i) {
			int nsrc = std::max(0, std::min(vi.num_frames - 1, n + i));
			int analyze_n = (nsrc + i) >> 3;
			int idx = (nsrc + i) & 7;

			if (analyze_n != prev_n) {
				frame = analyzeclip->GetFrame(analyze_n, env);
				prev_n = analyze_n;
			}

			const LogoAnalyzeFrame* pInfo =
				reinterpret_cast<const LogoAnalyzeFrame*>(frame->GetReadPtr());
			frames[i + DIST] = pInfo[idx];
		}
		frame = nullptr;

		int minfades[DIST * 2 + 1];
		for (int i = 0; i < DIST * 2 + 1; ++i) {
			minfades[i] = (int)(std::min_element(frames[i].p, frames[i].p + 11) - frames[i].p);
		}
		int minT = (int)(std::min_element(frames[DIST].t, frames[DIST].t + 11) - frames[DIST].t);
		int minB = (int)(std::min_element(frames[DIST].b, frames[DIST].b + 11) - frames[DIST].b);
		// 前後4フレームを見てフェードしてるか突然消えてるか判断
		float before_fades = 0;
		float after_fades = 0;
		for (int i = 1; i <= 4; ++i) {
			before_fades += minfades[DIST - i];
			after_fades += minfades[DIST + i];
		}
		before_fades /= 4 * 10;
		after_fades /= 4 * 10;
		// しきい値は適当
		if ((before_fades < 0.3 && after_fades > 0.7) ||
			(before_fades > 0.7 && after_fades < 0.3))
		{
			// 急な変化 -> フィールドごとに見る
			fadeT = minT / 10.0f;
			fadeB = minB / 10.0f;
		}
		else {
			// 緩やかな変化 -> フレームごとに見る
			fadeT = fadeB = (minfades[DIST] / 10.0f);
		}
	}

	void CalcFade(int n, float& fadeT, float& fadeB, IScriptEnvironment2* env)
	{
		if (frameResult.size() == 0) {
			// ロゴ解析結果がない場合は常にリアルタイム解析
			CalcFade2(n, fadeT, fadeB, env);
		}
		else {
			// ロゴ解析結果を大局的に使って、
			// 切り替わり周辺だけリアルタイム解析結果を使う
			enum {
				DIST = 8,
			};
			int frames[DIST * 2 + 1];
			for (int i = -DIST; i <= DIST; ++i) {
				int nsrc = std::max(0, std::min(vi.num_frames - 1, n + i));
				frames[i + DIST] = frameResult[nsrc];
			}
			if (std::all_of(frames, frames + DIST * 2 + 1, [&](int p) { return p == frames[0]; })) {
				// ON or OFF
				fadeT = fadeB = ((frames[DIST] == 2) ? 1.0f : 0.0f);
			}
			else {
				// 切り替わりを含む
				CalcFade2(n, fadeT, fadeB, env);
			}
		}
	}

	template <typename pixel_t>
	PVideoFrame GetFrameT(int n, IScriptEnvironment2* env)
	{
		PVideoFrame frame = child->GetFrame(n, env);
		env->MakeWritable(&frame);

		float maxv = (float)((1 << vi.BitsPerComponent()) - 1);
		pixel_t* dstY = reinterpret_cast<pixel_t*>(frame->GetWritePtr(PLANAR_Y));
		pixel_t* dstU = reinterpret_cast<pixel_t*>(frame->GetWritePtr(PLANAR_U));
		pixel_t* dstV = reinterpret_cast<pixel_t*>(frame->GetWritePtr(PLANAR_V));

		int pitchY = frame->GetPitch(PLANAR_Y) / sizeof(pixel_t);
		int pitchUV = frame->GetPitch(PLANAR_U) / sizeof(pixel_t);
		int off = header.imgx + header.imgy * pitchY;
		int offUV = (header.imgx >> header.logUVx) + (header.imgy >> header.logUVy) * pitchUV;

		if (mode == 0) { // 通常
			float fadeT, fadeB;
			CalcFade(n, fadeT, fadeB, env);

			// 最適Fade値でロゴ除去
			const float *logoAY = logo->GetA(PLANAR_Y);
			const float *logoBY = logo->GetB(PLANAR_Y);
			const float *logoAU = logo->GetA(PLANAR_U);
			const float *logoBU = logo->GetB(PLANAR_U);
			const float *logoAV = logo->GetA(PLANAR_V);
			const float *logoBV = logo->GetB(PLANAR_V);

			int wUV = (header.w >> header.logUVx);
			int hUV = (header.h >> header.logUVy);

			if (fadeT == fadeB) {
				// フレーム処理
				Delogo(dstY + off, header.w, header.h, header.w, pitchY, maxv, logoAY, logoBY, fadeT);
				Delogo(dstU + offUV, wUV, hUV, wUV, pitchUV, maxv, logoAU, logoBU, fadeT);
				Delogo(dstV + offUV, wUV, hUV, wUV, pitchUV, maxv, logoAV, logoBV, fadeT);
			}
			else {
				// フィールド処理

				Delogo(dstY + off, header.w, header.h / 2, header.w * 2, pitchY * 2, maxv, logoAY, logoBY, fadeT);
				Delogo(dstY + off + pitchY, header.w, header.h / 2, header.w * 2, pitchY * 2, maxv, logoAY + header.w, logoBY + header.w, fadeB);

				int uvparity = ((header.imgy / 2) % 2);
				int tuvoff = uvparity * pitchUV;
				int buvoff = !uvparity * pitchUV;
				int tuvoffl = uvparity * wUV;
				int buvoffl = !uvparity * wUV;

				Delogo(dstU + offUV + tuvoff, wUV, hUV / 2, wUV * 2, pitchUV * 2, maxv, logoAU + tuvoffl, logoBU + tuvoffl, fadeT);
				Delogo(dstV + offUV + tuvoff, wUV, hUV / 2, wUV * 2, pitchUV * 2, maxv, logoAV + tuvoffl, logoBV + tuvoffl, fadeT);

				Delogo(dstU + offUV + buvoff, wUV, hUV / 2, wUV * 2, pitchUV * 2, maxv, logoAU + buvoffl, logoBU + buvoffl, fadeB);
				Delogo(dstV + offUV + buvoff, wUV, hUV / 2, wUV * 2, pitchUV * 2, maxv, logoAV + buvoffl, logoBV + buvoffl, fadeB);
			}

			return frame;
		}

		// ロゴフレームデバッグ用
		float fadeT, fadeB;
		CalcFade(n, fadeT, fadeB, env);

		const char* str = "X";
		if (fadeT == fadeB) {
			str = (fadeT < 0.5) ? "X" : "O";
		}
		else {
			str = (fadeT < fadeB) ? "BTM" : "TOP";
		}

		char buf[200];
		sprintf_s(buf, "%s %.1f vs %.1f", str, fadeT, fadeB);
		DrawText(frame, true, 0, 0, buf);

		return frame;
	}

	void ReadLogoFrameFile(const tstring& logofPath, IScriptEnvironment* env)
	{
		struct LogoFrameElement {
			bool isStart;
			int best, start, end;
		};
		std::vector<LogoFrameElement> elements;
		try {
			File file(logofPath, _T("r"));
			std::regex re("^\\s*(\\d+)\\s+(\\S)\\s+(\\d+)\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)");
			std::string str;
			while (file.getline(str)) {
				std::smatch m;
				if (std::regex_search(str, m, re)) {
					LogoFrameElement elem = {
						std::tolower(m[2].str()[0]) == 's', // isStart
						std::stoi(m[1].str()), // best
						std::stoi(m[5].str()), // start
						std::stoi(m[6].str())  // end
					};
					elements.push_back(elem);
				}
			}
		}
		catch (const IOException&) {
			env->ThrowError("Failed to read dat file (%s)", logofPath.c_str());
		}
		frameResult.resize(vi.num_frames);
		std::fill(frameResult.begin(), frameResult.end(), 0);
		for (int i = 0; i < (int)elements.size(); i += 2) {
			if (elements[i].isStart == false || elements[i + 1].isStart) {
				env->ThrowError("Invalid logoframe file. Start and End must be cyclic.");
			}
			std::fill(frameResult.begin() + std::min(vi.num_frames, elements[i].start),
				frameResult.begin() + std::min(vi.num_frames, elements[i].end + 1), 1);
			std::fill(frameResult.begin() + std::min(vi.num_frames, elements[i].end),
				frameResult.begin() + std::min(vi.num_frames, elements[i + 1].start + 1), 2);
			std::fill(frameResult.begin() + std::min(vi.num_frames, elements[i + 1].start + 1),
				frameResult.begin() + std::min(vi.num_frames, elements[i + 1].end + 1), 1);
		}
	}

public:
	AMTEraseLogo(PClip clip, PClip analyzeclip, const tstring& logoPath, const tstring& logofPath, int mode, IScriptEnvironment* env)
		: GenericVideoFilter(clip)
		, analyzeclip(analyzeclip)
		, mode(mode)
	{
		try {
			logo = std::unique_ptr<LogoDataParam>(
				new LogoDataParam(LogoData::Load(logoPath, &header), &header));
		}
		catch (const IOException&) {
			env->ThrowError("Failed to read logo file (%s)", logoPath.c_str());
		}
		
		if (logofPath.size() > 0) {
			ReadLogoFrameFile(logofPath, env);
		}
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

	int __stdcall SetCacheHints(int cachehints, int frame_range) {
		if (cachehints == CACHE_GET_MTMODE) {
			return MT_NICE_FILTER;
		}
		return 0;
	}

	static AVSValue __cdecl Create(AVSValue args, void* user_data, IScriptEnvironment* env)
	{
		return new AMTEraseLogo(
			args[0].AsClip(),       // source
			args[1].AsClip(),       // analyzeclip
			to_tstring(args[2].AsString()),			// logopath
			to_tstring(args[3].AsString("")),		// logofpath
			args[4].AsInt(0),       // mode
			env
		);
	}
};

class LogoFrame : AMTObject
{
	int numLogos;
	std::unique_ptr<LogoDataParam[]> logoArr;
	std::unique_ptr<LogoDataParam[]> deintArr;

	int maxYSize;
	int numFrames;
	int framesPerSec;
	VideoInfo vi;

	struct EvalResult {
		float corr0, corr1;
	};
	std::unique_ptr<EvalResult[]> evalResults;

	int bestLogo;
	float logoRatio;

	template <typename pixel_t>
	void ScanFrame(PVideoFrame& frame, float* memDeint, float* memWork, float maxv, EvalResult* outResult)
	{
		const pixel_t* srcY = reinterpret_cast<const pixel_t*>(frame->GetReadPtr(PLANAR_Y));
		int pitchY = frame->GetPitch(PLANAR_Y);

		for (int i = 0; i < numLogos; ++i) {
			LogoDataParam& logo = deintArr[i];
			if (logo.isValid() == false ||
				logo.getImgWidth() != vi.width ||
				logo.getImgHeight() != vi.height)
			{
				outResult[i].corr0 = 0;
				outResult[i].corr1 = -1;
				continue;
			}

			// フレームをインタレ解除
			int off = logo.getImgX() + logo.getImgY() * pitchY;
			DeintY(memDeint, srcY + off, pitchY, logo.getWidth(), logo.getHeight());

			// ロゴ評価
			outResult[i].corr0 = logo.EvaluateLogo(memDeint, maxv, 0, memWork);
			outResult[i].corr1 = logo.EvaluateLogo(memDeint, maxv, 1, memWork);
		}
	}

	template <typename pixel_t>
	void IterateFrames(PClip clip, IScriptEnvironment2* env)
	{
		auto memDeint = std::unique_ptr<float[]>(new float[maxYSize + 8]);
		auto memWork = std::unique_ptr<float[]>(new float[maxYSize + 8]);
		float maxv = (float)((1 << vi.BitsPerComponent()) - 1);
		evalResults = std::unique_ptr<EvalResult[]>(new EvalResult[vi.num_frames * numLogos]);
		for (int n = 0; n < vi.num_frames; ++n) {
			PVideoFrame frame = clip->GetFrame(n, env);
			ScanFrame<pixel_t>(frame, memDeint.get(), memWork.get(), maxv, &evalResults[n * numLogos]);

			if ((n % 5000) == 0) {
				ctx.infoF("%6d/%d", n, vi.num_frames);
			}
		}
		numFrames = vi.num_frames;
		framesPerSec = (int)std::round((float)vi.fps_numerator / vi.fps_denominator);

		ctx.info("Finished");
	}

public:
	LogoFrame(AMTContext& ctx, const std::vector<tstring>& logofiles, float maskratio)
		: AMTObject(ctx)
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
			catch (const IOException&) {
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

	void dumpResult(const tstring& basepath)
	{
		for (int i = 0; i < numLogos; ++i) {
			StringBuilder sb;
			for (int n = 0; n < numFrames; ++n) {
				auto& r = evalResults[n * numLogos + i];
				sb.append("%f,%f\n", r.corr0, r.corr1);
			}
			File file(StringFormat(_T("%s%d"), basepath, i), _T("w"));
			file.write(sb.getMC());
		}
	}

	void writeResult(const tstring& outpath)
	{
		// 絶対値<0.2fは不明とみなす
		const float thresh = 0.2f;
		const float threshL = 0.5f; // MinMax評価用
		// MinMax幅
		const float avgDur = 1.0f;
		const float medianDur = 0.5f;

		// ロゴを選択 //
    struct Summary {
      float cost;    // 消した後のゴミの量
      int numFrames;  // 検出したフレーム数
    };
		std::vector<Summary> logoSummary(numLogos);
		for (int n = 0; n < numFrames; ++n) {
			for (int i = 0; i < numLogos; ++i) {
				auto& r = evalResults[n * numLogos + i];
				// ロゴを検出 かつ 消せてる
        if (r.corr0 > thresh && std::abs(r.corr1) < thresh) {
          logoSummary[i].numFrames++;
          logoSummary[i].cost += std::abs(r.corr1);
        }
			}
		}
    std::vector<float> logoScore(numLogos);
    for (int i = 0; i < numLogos; ++i) {
      auto& s = logoSummary[i];
      // (消した後のゴミの量の平均) * (検出したフレーム割合の逆数)
      logoScore[i] = (s.numFrames == 0) ? INFINITY :
        (s.cost / s.numFrames) * (numFrames / (float)s.numFrames);
#if 1
      ctx.debugF("logo%d: %f * %f = %f", i + 1,
        (s.cost / s.numFrames),
        (numFrames / (float)s.numFrames),
        logoScore[i]);
#endif
    }
		bestLogo = (int)(std::min_element(logoScore.begin(), logoScore.end()) - logoScore.begin());
		logoRatio = (float)logoSummary[bestLogo].numFrames / numFrames;

		// スコアに変換
		int halfAvgFrames = int(framesPerSec * avgDur / 2 + 0.5f);
		int aveFrames = halfAvgFrames * 2 + 1;
		int halfMedianFrames = int(framesPerSec * medianDur / 2 + 0.5f);
		int medianFrames = halfMedianFrames * 2 + 1;
		int winFrames = std::max(aveFrames, medianFrames);
		int halfWinFrames = winFrames / 2;
		std::vector<float> rawScores_(numFrames + winFrames);
		auto rawScores = rawScores_.begin() + halfWinFrames;
		for (int n = 0; n < numFrames; ++n) {
			auto& r = evalResults[n * numLogos + bestLogo];
			// corr0のマイナスとcorr1のプラスはノイズなので消す
			rawScores[n] = std::max(0.0f, r.corr0) + std::min(0.0f, r.corr1);
		}
		// 両端を端の値で埋める
		std::fill(rawScores_.begin(), rawScores, rawScores[0]);
		std::fill(rawScores + numFrames, rawScores_.end(), rawScores[numFrames - 1]);
		
		struct FrameResult {
			int result;
			float score;
		};

		// フィルタで均す
		std::vector<FrameResult> frameResult(numFrames);
		std::vector<float> medianBuf(medianFrames);
		for (int i = 0; i < numFrames; ++i) {
			// MinMax
			// 前の最大値と後ろの最大値の小さい方を取る
			// 動きの多い映像でロゴがかき消されることがあるので、それを救済する
			float beforeMax = *std::max_element(rawScores + i - halfAvgFrames, rawScores + i);
			float afterMax = *std::max_element(rawScores + i + 1, rawScores + i + 1 + halfAvgFrames);
			float minMax = std::min(beforeMax, afterMax);
			int minMaxResult = (std::abs(minMax) < threshL) ? 1 : (minMax < 0.0f) ? 0 : 2;

			// 移動平均
			// MinMaxだけだと薄くても安定して表示されてるとかが識別できないので
			// これも必要
			float avg = std::accumulate(rawScores + i - halfAvgFrames,
				rawScores + i + halfAvgFrames + 1, 0.0f) / aveFrames;
			int avgResult = (std::abs(avg) < thresh) ? 1 : (avg < 0.0f) ? 0 : 2;

			// 両者が違ってたら不明とする
			frameResult[i].result = (minMaxResult != avgResult) ? 1 : minMaxResult;

			// 生の値は動きが激しいので少しメディアンフィルタをかけておく
			std::copy(rawScores + i - halfMedianFrames,
				rawScores + i + halfMedianFrames + 1, medianBuf.begin());
			std::sort(medianBuf.begin(), medianBuf.end());
			frameResult[i].score = medianBuf[halfMedianFrames];
		}

		// 不明部分を推測
		// 両側がロゴありとなっていたらロゴありとする
		for(auto it = frameResult.begin(); it != frameResult.end();) {
			auto first1 = std::find_if(it, frameResult.end(), [](FrameResult r) { return r.result == 1; });
			it = std::find_if_not(first1, frameResult.end(), [](FrameResult r) { return r.result == 1; });
			int prevResult = (first1 == frameResult.begin()) ? 0 : (first1 - 1)->result;
			int nextResult = (it == frameResult.end()) ? 0 : it->result;
			if (prevResult == nextResult) {
				std::transform(first1, it, first1, [=](FrameResult r) {
					r.result = prevResult;
					return r;
				});
			}
		}
		
		// ロゴ区間を出力
		StringBuilder sb;
		for (auto it = frameResult.begin(); it != frameResult.end();) {
			auto sEnd_ = std::find_if(it, frameResult.end(), [](FrameResult r) { return r.result == 2; });
			auto eEnd_ = std::find_if(sEnd_, frameResult.end(), [](FrameResult r) { return r.result == 0; });

			// 移動平均なので実際の値とは時間差がある場合があるので、フレーム時刻を精緻化
			auto sEnd = sEnd_;
			auto eEnd = eEnd_;
			if (sEnd != frameResult.end()) {
				if (sEnd->score >= thresh) {
					// すでに始まっているので戻ってみる
					sEnd = std::find_if(std::make_reverse_iterator(sEnd), frameResult.rend(),
						[=](FrameResult r) { return r.score < thresh; }).base();
				}
				else {
					// まだ始まっていないので進んでみる
					sEnd = std::find_if(sEnd, frameResult.end(),
						[=](FrameResult r) { return r.score >= thresh; });
				}
			}
			if (eEnd != frameResult.end()) {
				if (eEnd->score <= -thresh) {
					// すでに終わっているので戻ってみる
					eEnd = std::find_if(std::make_reverse_iterator(eEnd), std::make_reverse_iterator(sEnd),
						[=](FrameResult r) { return r.score > -thresh; }).base();
				}
				else {
					// まだ終わっていないので進んでみる
					eEnd = std::find_if(eEnd, frameResult.end(),
						[=](FrameResult r) { return r.score <= -thresh; });
				}
			}

			auto sStart = std::find_if(std::make_reverse_iterator(sEnd),
				std::make_reverse_iterator(it), [=](FrameResult r) { return r.score <= -thresh; }).base();
			auto eStart = std::find_if(std::make_reverse_iterator(eEnd),
				std::make_reverse_iterator(sEnd), [=](FrameResult r) { return r.score >= thresh; }).base();

			auto sBest = std::find_if(sStart, sEnd, [](FrameResult r) { return r.score > 0; });
			auto eBest = std::find_if(std::make_reverse_iterator(eEnd),
				std::make_reverse_iterator(eStart), [](FrameResult r) { return r.score > 0; }).base();

			// 区間がある場合だけ
			if (sEnd != eEnd) {
				int sStarti = int(sStart - frameResult.begin());
				int sBesti = int(sBest - frameResult.begin());
				int sEndi = int(sEnd - frameResult.begin());
				int eStarti = int(eStart - frameResult.begin()) - 1;
				int eBesti = int(eBest - frameResult.begin()) - 1;
				int eEndi = int(eEnd - frameResult.begin()) - 1;
				sb.append("%6d S 0 ALL %6d %6d\n", sBesti, sStarti, sEndi);
				sb.append("%6d E 0 ALL %6d %6d\n", eBesti, eStarti, eEndi);
			}

			it = eEnd_;
		}

		File file(outpath, _T("w"));
		file.write(sb.getMC());
	}

	int getBestLogo() const {
		return bestLogo;
	}

	float getLogoRatio() const {
		return logoRatio;
	}
};

} // namespace logo

