/**
* Dicrete Cosine Transform (DCT)
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <cmath>

#include <immintrin.h> // AVX

template <int N>
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

  void transform(const float orig[][N], float dct[][N]) {
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
    //checkDct(orig, dct);
  }

  void transform_AVX(const float orig[][N], float dct[][N])
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
    //checkDct(orig, dct);
  }

  void inverse(const float dct[][N], float orig[][N]) {
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

  void checkDct(const float orig[][N], const float dct[][N]) {
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
