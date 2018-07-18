/**
* Amtasukaze AVX Compute Kernel
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/

// このファイルはAVXでコンパイル
#include <intrin.h>
#include <immintrin.h>
#include <stdio.h>

struct CPUInfo {
	bool initialized, avx, avx2;
};

static CPUInfo g_cpuinfo;

static inline void InitCPUInfo() {
	if (g_cpuinfo.initialized == false) {
		int cpuinfo[4];
		__cpuid(cpuinfo, 1);
		g_cpuinfo.avx = cpuinfo[2] & (1 << 28) || false;
		bool osxsaveSupported = cpuinfo[2] & (1 << 27) || false;
		g_cpuinfo.avx2 = false;
		if (osxsaveSupported && g_cpuinfo.avx)
		{
			// _XCR_XFEATURE_ENABLED_MASK = 0
			unsigned long long xcrFeatureMask = _xgetbv(0);
			g_cpuinfo.avx = (xcrFeatureMask & 0x6) == 0x6;
			if (g_cpuinfo.avx) {
				__cpuid(cpuinfo, 7);
				g_cpuinfo.avx2 = cpuinfo[1] & (1 << 5) || false;
			}
		}
		g_cpuinfo.initialized = true;
	}
}

bool IsAVXAvailable() {
	InitCPUInfo();
	return g_cpuinfo.avx;
}

bool IsAVX2Available() {
	InitCPUInfo();
	return g_cpuinfo.avx2;
}

// https://qiita.com/beru/items/fff00c19968685dada68
// in  : ( x7, x6, x5, x4, x3, x2, x1, x0 )
// out : ( -,  -,  -, xsum )
inline __m128 hsum256_ps(__m256 x) {
	// hiQuad = ( x7, x6, x5, x4 )
	const __m128 hiQuad = _mm256_extractf128_ps(x, 1);
	// loQuad = ( x3, x2, x1, x0 )
	const __m128 loQuad = _mm256_castps256_ps128(x);
	// sumQuad = ( x3+x7, x2+x6, x1+x5, x0+x4 )
	const __m128 sumQuad = _mm_add_ps(loQuad, hiQuad);
	// loDual = ( -, -, x1+x5, x0+x4 )
	const __m128 loDual = sumQuad;
	// hiDual = ( -, -, x3+x7, x2+x6 )
	const __m128 hiDual = _mm_movehl_ps(sumQuad, sumQuad);
	// sumDual = ( -, -, x1+x3 + x5+x7, x0+x2 + x4+x6 )
	const __m128 sumDual = _mm_add_ps(loDual, hiDual);
	// lo = ( -, -, -, x0+x2 + x4+x6 )
	const __m128 lo = sumDual;
	// hi = ( -, -, -, x1+x3 + x5+x7 )
	const __m128 hi = _mm_shuffle_ps(sumDual, sumDual, 0x1);
	// sum = ( -, -, -, x0+x1+x2+x3 + x4+x5+x6+x7 )
	const __m128 sum = _mm_add_ss(lo, hi);
	return sum;
}

// k,Yは後ろに3要素はみ出して読む
float CalcCorrelation5x5_AVX(const float* k, const float* Y, int x, int y, int w, float* pavg)
{
	// 後ろのゴミを消すためのマスク
	const auto mask = _mm256_castsi256_ps(_mm256_set_epi32(0, 0, 0, -1, -1, -1, -1, -1));

	const auto y0 = _mm256_loadu_ps(Y + (x - 2) + w * (y - 2));
	const auto y1 = _mm256_loadu_ps(Y + (x - 2) + w * (y - 1));
	const auto y2 = _mm256_loadu_ps(Y + (x - 2) + w * (y + 0));
	const auto y3 = _mm256_loadu_ps(Y + (x - 2) + w * (y + 1));
	const auto y4 = _mm256_loadu_ps(Y + (x - 2) + w * (y + 2));

	auto vysum = _mm256_and_ps(
		_mm256_add_ps(
			_mm256_add_ps(
				_mm256_add_ps(y0, y1),
				_mm256_add_ps(y2, y3)),
			y4),
		mask);

	float avg;
	_mm_store_ss(&avg, hsum256_ps(vysum));
	avg /= 25;

	auto vavg = _mm256_broadcast_ss(&avg);

	const auto k0 = _mm256_loadu_ps(k + 0);
	const auto k1 = _mm256_loadu_ps(k + 5);
	const auto k2 = _mm256_loadu_ps(k + 10);
	const auto k3 = _mm256_loadu_ps(k + 15);
	const auto k4 = _mm256_loadu_ps(k + 20);

	auto vsum = _mm256_and_ps(
		_mm256_add_ps(
			_mm256_add_ps(
				_mm256_add_ps(_mm256_mul_ps(k0, _mm256_sub_ps(y0, vavg)), _mm256_mul_ps(k1, _mm256_sub_ps(y1, vavg))),
				_mm256_add_ps(_mm256_mul_ps(k2, _mm256_sub_ps(y2, vavg)), _mm256_mul_ps(k3, _mm256_sub_ps(y3, vavg)))),
			_mm256_mul_ps(k4, _mm256_sub_ps(y4, vavg))),
		mask);

	float sum;
	_mm_store_ss(&sum, hsum256_ps(vsum));

	if (pavg) *pavg = avg;
	return sum;
};
