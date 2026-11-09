#pragma once
#include <stddef.h>
#include "cpu_isa.h"

// ビルドは AVX2。AVX512 / AVX10 と、AVX2 では出せない拡張は専用 TU に置き、
// 初回 Get() で CPU に合う実装へ固定する。呼び出し側は ISA を見なくてよい。

class SimdKernels {
public:
	typedef void (*FnCopy)(void* dst, const void* src, size_t n);
	typedef void (*FnZero)(void* dst, size_t n);
	typedef void (*FnAddF32)(float* dst, const float* src, size_t n);
	typedef void (*FnMulF32)(float* dst, const float* src, size_t n);
	typedef void (*FnMaddF32)(float* dst, const float* a, const float* b, size_t n);

	FnCopy Copy;
	FnZero Zero;
	FnAddF32 AddF32;
	FnMulF32 MulF32;
	FnMaddF32 MaddF32;
	const char* name;

	static const SimdKernels& Get();
};

// AVX512 / AVX10 専用 TU からリンクする（未対応 CPU では呼ばない）
void SimdKernels_Copy_AVX512(void* dst, const void* src, size_t n);
void SimdKernels_Zero_AVX512(void* dst, size_t n);
void SimdKernels_AddF32_AVX512(float* dst, const float* src, size_t n);
void SimdKernels_MulF32_AVX512(float* dst, const float* src, size_t n);
void SimdKernels_MaddF32_AVX512(float* dst, const float* a, const float* b, size_t n);

void SimdKernels_Copy_AVX10(void* dst, const void* src, size_t n);
void SimdKernels_Zero_AVX10(void* dst, size_t n);
void SimdKernels_AddF32_AVX10(float* dst, const float* src, size_t n);
void SimdKernels_MulF32_AVX10(float* dst, const float* src, size_t n);
void SimdKernels_MaddF32_AVX10(float* dst, const float* a, const float* b, size_t n);
