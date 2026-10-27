#pragma once
#include <stddef.h>

// PCM ホットパス。ビルドは AVX2。AVX512 / AVX10-512 の CPU だけ専用実装へディスパッチ。
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

// AVX10/512 専用 TU。レガシー AVX512 CPUID が無くても leaf 24 が 512bit ならこちら。
void SimdFloatPeakMean_AVX10(const float* src, size_t n, double* maxAbs, double* sumAbs, size_t* validCount);
void SimdFloatToInt16_AVX10(const float* src, short* dst, size_t n, double scale);
void SimdDoubleToInt16Kbpsf2_AVX10(const double* src, short* dst, size_t n);
void SimdI16CopySatFromI32_AVX10(const int* src, short* dst, size_t n);
void SimdMixI16Sat_AVX10(short* acc, const short* add, size_t n);
