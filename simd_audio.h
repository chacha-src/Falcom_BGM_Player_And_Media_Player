#pragma once
#include <stddef.h>

// PCM ホットパス。AVX2 既定、AVX512 対応 CPU なら専用実装へディスパッチ。
// 呼び出し側は ISA を意識しなくてよい（初回で関数ポインタを固定する）。

void SimdFloatPeakMean(const float* src, size_t n, double* maxAbs, double* sumAbs, size_t* validCount);
void SimdFloatToInt16(const float* src, short* dst, size_t n, double scale);
void SimdDoubleToInt16Kbpsf2(const double* src, short* dst, size_t n);
void SimdI16CopySatFromI32(const int* src, short* dst, size_t n);
void SimdMixI16Sat(short* acc, const short* add, size_t n);

// AVX512 専用 TU からリンクする（未対応 CPU では呼ばない）
void SimdFloatPeakMean_AVX512(const float* src, size_t n, double* maxAbs, double* sumAbs, size_t* validCount);
void SimdFloatToInt16_AVX512(const float* src, short* dst, size_t n, double scale);
void SimdDoubleToInt16Kbpsf2_AVX512(const double* src, short* dst, size_t n);
void SimdI16CopySatFromI32_AVX512(const int* src, short* dst, size_t n);
void SimdMixI16Sat_AVX512(short* acc, const short* add, size_t n);
