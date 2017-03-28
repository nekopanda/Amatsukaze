/**
* Dicrete Cosine Transform (DCT)
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <cmath>

#include <intrin.h>
#include <immintrin.h> // AVX

extern "C" {
#include <libavformat/avformat.h>
}

// SIMD: 0=no simd, 1=sse, 2=avx
template <int N, int SIMD, bool debug>
class Dct2d
{
public:
  Dct2d()
  {
    // テーブル作成 //

    // 2/N * C(u) * C(v) を計算
    for (int v = 0; v < N; ++v) {
      for (int u = 0; u < N; ++u) {
        cc[v][u] = (u*v == 0) ? (1.0f / std::sqrt(2.0f)) : 1.0f;
      }
    }
    cc[0][0] = 0.5f;
    for (int v = 0; v < N; ++v) {
      for (int u = 0; u < N; ++u) {
        cc[v][u] *= 2.0f / N;
      }
    }

    for (int u = 0; u < N; ++u) {
      for (int x = 0; x < N; ++x) {
        cs_xu[x][u] = cs[u][x] = (float)std::cos(M_PI * (2 * x + 1) * u / (2.0f * N));
      }
    }
  }

  void transform(const float orig[][N], float dct[][N]) const
  {
    if (SIMD == 2 && N == 8) {
      transform_AVX_8x8(orig, dct);
    }
    else if (SIMD == 2) {
      transform_AVX(orig, dct);
    }
    else {
      transform_scalar(orig, dct);
    }
    if (debug) {
      checkDct(orig, dct);
    }
  }

  void transform_scalar(const float orig[][N], float dct[][N]) const
	{
    for (int v = 0; v < N; ++v) {
      for (int u = 0; u < N; ++u) {
        float s = 0;
        for (int y = 0; y < N; ++y) {
          for (int x = 0; x < N; ++x) {
            s += orig[y][x] * cs[u][x] * cs[v][y];
          }
        }
        dct[v][u] = cc[v][u] * s;
      }
    }
  }

  void transform_AVX(const float orig[][N], float dct[][N]) const
  {
    __declspec(align(32)) __m256 tmp[N][N];

    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        tmp[y][x] = _mm256_mul_ps(
          _mm256_set1_ps(orig[y][x]),
          _mm256_loadu_ps(cs_xu[x]));
      }
    }

    for (int v = 0; v < N; ++v) {
			__m256 s = _mm256_setzero_ps();
      for (int y = 0; y < N; ++y) {
        __m256 csvy = _mm256_set1_ps(cs[v][y]);
				for (int x = 0; x < N; ++x) {
					s = _mm256_add_ps(
						s, _mm256_mul_ps(tmp[y][x], csvy));
				}
      }
      _mm256_storeu_ps(dct[v], 
        _mm256_mul_ps(_mm256_loadu_ps(cc[v]), s));
    }
  }

	void transform_AVX_8x8(const float orig[][N], float dct[][N]) const
	{
		__declspec(align(32)) __m256 tmp[N][N];

		for (int y = 0; y < N; ++y) {
			for (int x = 0; x < N; ++x) {
				tmp[y][x] = _mm256_mul_ps(
					_mm256_set1_ps(orig[y][x]),
					_mm256_loadu_ps(cs_xu[x]));
			}
		}

		for (int v = 0; v < N; ++v) {
			__m256 s = _mm256_setzero_ps();

#define PROC_Y(y)  {                                \
			__m256 csvy = _mm256_set1_ps(cs[v][y]);       \
			__m256 s_0 = _mm256_mul_ps(tmp[y][0], csvy);  \
			__m256 s_1 = _mm256_mul_ps(tmp[y][1], csvy);  \
			__m256 s_2 = _mm256_mul_ps(tmp[y][2], csvy);  \
			__m256 s_3 = _mm256_mul_ps(tmp[y][3], csvy);  \
			__m256 s_4 = _mm256_mul_ps(tmp[y][4], csvy);  \
			__m256 s_5 = _mm256_mul_ps(tmp[y][5], csvy);  \
			__m256 s_6 = _mm256_mul_ps(tmp[y][6], csvy);  \
			__m256 s_7 = _mm256_mul_ps(tmp[y][7], csvy);  \
																										\
			__m256 s_x = _mm256_add_ps(                   \
				_mm256_add_ps(                              \
					_mm256_add_ps(s_0, s_1),                  \
					_mm256_add_ps(s_2, s_3)),                 \
				_mm256_add_ps(                              \
					_mm256_add_ps(s_4, s_5),                  \
					_mm256_add_ps(s_6, s_7)));                \
																										\
			s = _mm256_add_ps(s, s_x); }

			PROC_Y(0)
			PROC_Y(1)
			PROC_Y(2)
			PROC_Y(3)
			PROC_Y(4)
			PROC_Y(5)
			PROC_Y(6)
			PROC_Y(7)

#undef PROC_Y

			_mm256_storeu_ps(dct[v],
				_mm256_mul_ps(_mm256_loadu_ps(cc[v]), s));
		}
		//checkDct(orig, dct);
	}

  void inverse(const float dct[][N], float orig[][N]) const {
    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        float s = 0;
        for (int v = 0; v < N; ++v) {
          for (int u = 0; u < N; ++u) {
            s += cc[v][u] * dct[v][u] * cs[u][x] * cs[v][y];
          }
        }
        orig[y][x] = s;
      }
    }
  }

  void checkDct(const float orig[][N], const float dct[][N]) const {
    float check[N][N];
    inverse(dct, check);
    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
        if (std::abs(check[y][x] - orig[y][x]) >= 0.1) {
          printf("!!!");
        }
      }
    }
  }

private:
  float cc[N][N];
  float cs[N][N];
  float cs_xu[N][N];
};

template <int N>
struct DctSummary {
  enum { LEN = 4 };
  int summary[LEN];
	int pixels;

  void print(FILE* fp) const {
    for (int i = 0; i < LEN; ++i) {
      fprintf(fp, (i == LEN - 1) ? "%d\n" : "%d,", summary[i]);
    }
  }

  void add(const DctSummary& o) {
    for (int i = 0; i < LEN; ++i) {
      summary[i] += o.summary[i];
    }
		pixels += o.pixels;
  }
};

class FrameAnalyzer
{
public:
  enum { N = 8 };
  virtual DctSummary<N> analyzeFrame(AVFrame* cur, AVFrame* prev) = 0;
};

template <int SIMD>
class FrameAnalyzerImpl : public FrameAnalyzer
{
public:
  virtual DctSummary<N> analyzeFrame(AVFrame* cur, AVFrame* prev)
  {
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(cur->format));
    const int pixel_shift = (desc->comp[0].depth > 8) ? 1 : 0;

    int nb = 0;
    DctSummary<N> s = DctSummary<N>();
    for (int i = 0; i < 3; ++i) {
      const int hshift = (i > 0) ? desc->log2_chroma_w : 0;
      const int vshift = (i > 0) ? desc->log2_chroma_h : 0;
      const int width = cur->width >> hshift;
      const int height = cur->height >> vshift;

      for (int y = 0; y <= (height - N * 2); y += N * 2) {
        int cline = cur->linesize[i];
        int pline = prev->linesize[i];
        uint8_t* cptr = cur->data[i] + cline * y;
        uint8_t* pptr = prev->data[i] + pline * y;

        for (int x = 0; x <= (width - N * 2); x += N * 2) {
          float src[N][N] = { 0 };
          float dct[N][N];

          if (pixel_shift == 0) {
            if (SIMD == 2) {
              fill_block_uint8_AVX(src, cptr, cline, pptr, pline);
            }
            else {
              fill_block<uint8_t>(src, cptr, cline, pptr, pline);
            }
          }
          else {
            if (SIMD == 2) {
              fill_block_uint16_AVX(src, cptr, cline, pptr, pline);
            }
            else {
              fill_block<uint16_t>(src, cptr, cline, pptr, pline);
            }
          }

          dct_.transform(src, dct);

          if (SIMD == 2) {
            addToSummary_AVX(dct, s);
          }
          else {
            addToSummary(dct, s);
          }
          ++nb;
        }
      }
    }
		s.pixels = nb * N * N;

    return s;
  }

private:
  Dct2d<N, SIMD, false> dct_;

  template <typename T>
  void fill_block(float block[][N], uint8_t* cptr, int cline, uint8_t* pptr, int pline)
  {
    for (int sy = 0, dy = 0; dy < N; sy += 2, dy += 1) {
      T* cptr_line = (T*)(cptr + sy * cline);
      T* pptr_line = (T*)(pptr + sy * pline);

      for (int sx = 0, dx = 0; dx < N; sx += 2, dx += 1) {
        block[dy][dx] = std::abs((float)cptr_line[sx] - (float)pptr_line[sx]);
      }
    }
  }

  void fill_block_uint8_AVX(float block[][N], uint8_t* cptr, int cline, uint8_t* pptr, int pline)
  {
    __declspec(align(32)) const __m128 signmask = _mm_set1_ps(-0.0f);
    __declspec(align(32)) const __m128i mask = _mm_set_epi32(0xFF, 0xFF, 0xFF, 0xFF);

    // AVX2はまだ使いたくないのでSSEで実装
    for (int sy = 0, dy = 0; dy < N; sy += 2, dy += 1) {
      uint8_t* cptr_line = cptr + sy * cline;
      uint8_t* pptr_line = pptr + sy * pline;

      _mm_storeu_ps(&block[dy][0], _mm_andnot_ps(signmask, _mm_sub_ps(
        _mm_cvtepi32_ps(_mm_and_si128(mask, _mm_cvtepu16_epi32(_mm_loadu_si128((__m128i*)(cptr_line + 0))))),
        _mm_cvtepi32_ps(_mm_and_si128(mask, _mm_cvtepu16_epi32(_mm_loadu_si128((__m128i*)(pptr_line + 0))))))));

      _mm_storeu_ps(&block[dy][4], _mm_andnot_ps(signmask, _mm_sub_ps(
        _mm_cvtepi32_ps(_mm_and_si128(mask, _mm_cvtepu16_epi32(_mm_loadu_si128((__m128i*)(cptr_line + 8))))),
        _mm_cvtepi32_ps(_mm_and_si128(mask, _mm_cvtepu16_epi32(_mm_loadu_si128((__m128i*)(pptr_line + 8))))))));

    }
  }

  void fill_block_uint16_AVX(float block[][N], uint8_t* cptr, int cline, uint8_t* pptr, int pline)
  {
    __declspec(align(32)) const __m256 signmask = _mm256_set1_ps(-0.0f);
    __declspec(align(32)) const __m256i mask = _mm256_set_epi32(0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF);

    for (int sy = 0, dy = 0; dy < N; sy += 2, dy += 1) {
      uint8_t* cptr_line = cptr + sy * cline;
      uint8_t* pptr_line = pptr + sy * pline;

      _mm256_storeu_ps(block[dy], _mm256_andnot_ps(signmask, _mm256_sub_ps(
        _mm256_cvtepi32_ps(_mm256_and_si256(mask, _mm256_loadu_si256((__m256i*)cptr_line))),
        _mm256_cvtepi32_ps(_mm256_and_si256(mask, _mm256_loadu_si256((__m256i*)pptr_line))))));

    }
  }

  void addToSummary(float block[][N], DctSummary<N>& s)
  {
		const float T0 = 0.5;
		const float T1 = 1;
		const float T2 = 2;
		const float T3 = 4;

    for (int y = 0; y < N; ++y) {
      for (int x = 0; x < N; ++x) {
				s.summary[0] += (std::abs(block[y][x]) >= T0);
				s.summary[1] += (std::abs(block[y][x]) >= T1);
				s.summary[2] += (std::abs(block[y][x]) >= T2);
				s.summary[3] += (std::abs(block[y][x]) >= T3);
      }
    }
  }

  void addToSummary_AVX(float block[][N], DctSummary<N>& s)
  {
		const __m256 signmask = _mm256_set1_ps(-0.0f);
		const __m256 T0 = _mm256_set1_ps(0.5);
		const __m256 T1 = _mm256_set1_ps(1);
		const __m256 T2 = _mm256_set1_ps(2);
		const __m256 T3 = _mm256_set1_ps(4);

		__m128i sum = _mm_loadu_si128((__m128i*)s.summary);

#define PROC_Y(y) \
		auto t ## y = _mm256_loadu_ps(block[y]); \
		sum = _mm_add_epi32(sum, _mm_setr_epi32( \
			__popcnt(_mm256_movemask_ps(_mm256_cmp_ps(_mm256_andnot_ps(signmask, t ## y), T0, _CMP_GE_OS))), \
			__popcnt(_mm256_movemask_ps(_mm256_cmp_ps(_mm256_andnot_ps(signmask, t ## y), T1, _CMP_GE_OS))), \
			__popcnt(_mm256_movemask_ps(_mm256_cmp_ps(_mm256_andnot_ps(signmask, t ## y), T2, _CMP_GE_OS))), \
			__popcnt(_mm256_movemask_ps(_mm256_cmp_ps(_mm256_andnot_ps(signmask, t ## y), T3, _CMP_GE_OS)))))

		PROC_Y(0);
		PROC_Y(1);
		PROC_Y(2);
		PROC_Y(3);
		PROC_Y(4);
		PROC_Y(5);
		PROC_Y(6);
		PROC_Y(7);

#undef PROC_Y

			_mm_storeu_si128((__m128i*)s.summary, sum);
  }
};