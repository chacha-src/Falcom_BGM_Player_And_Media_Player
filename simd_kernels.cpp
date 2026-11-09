#include "simd_kernels.h"
#include <string.h>

#if defined(_MSC_VER) || defined(__INTEL_LLVM_COMPILER)
#include <immintrin.h>
#endif

static void Copy_Scalar(void* dst, const void* src, size_t n)
{
	if (n) memcpy(dst, src, n);
}

static void Zero_Scalar(void* dst, size_t n)
{
	if (n) memset(dst, 0, n);
}

static void AddF32_Scalar(float* dst, const float* src, size_t n)
{
	for (size_t i = 0; i < n; ++i) dst[i] += src[i];
}

static void MulF32_Scalar(float* dst, const float* src, size_t n)
{
	for (size_t i = 0; i < n; ++i) dst[i] *= src[i];
}

static void MaddF32_Scalar(float* dst, const float* a, const float* b, size_t n)
{
	for (size_t i = 0; i < n; ++i) dst[i] += a[i] * b[i];
}

#if defined(__AVX2__) || defined(_M_X64) || defined(__AVX__)
static void Copy_AVX2(void* dst, const void* src, size_t n)
{
	unsigned char* d = (unsigned char*)dst;
	const unsigned char* s = (const unsigned char*)src;
	size_t i = 0;
	for (; i + 32 <= n; i += 32) {
		__m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
		_mm256_storeu_si256((__m256i*)(d + i), v);
	}
	if (i < n) memcpy(d + i, s + i, n - i);
	_mm256_zeroupper();
}

static void Zero_AVX2(void* dst, size_t n)
{
	unsigned char* d = (unsigned char*)dst;
	size_t i = 0;
	const __m256i z = _mm256_setzero_si256();
	for (; i + 32 <= n; i += 32)
		_mm256_storeu_si256((__m256i*)(d + i), z);
	if (i < n) memset(d + i, 0, n - i);
	_mm256_zeroupper();
}

static void AddF32_AVX2(float* dst, const float* src, size_t n)
{
	size_t i = 0;
	for (; i + 8 <= n; i += 8) {
		__m256 a = _mm256_loadu_ps(dst + i);
		__m256 b = _mm256_loadu_ps(src + i);
		_mm256_storeu_ps(dst + i, _mm256_add_ps(a, b));
	}
	for (; i < n; ++i) dst[i] += src[i];
	_mm256_zeroupper();
}

static void MulF32_AVX2(float* dst, const float* src, size_t n)
{
	size_t i = 0;
	for (; i + 8 <= n; i += 8) {
		__m256 a = _mm256_loadu_ps(dst + i);
		__m256 b = _mm256_loadu_ps(src + i);
		_mm256_storeu_ps(dst + i, _mm256_mul_ps(a, b));
	}
	for (; i < n; ++i) dst[i] *= src[i];
	_mm256_zeroupper();
}

static void MaddF32_AVX2(float* dst, const float* a, const float* b, size_t n)
{
	size_t i = 0;
	for (; i + 8 <= n; i += 8) {
		__m256 d = _mm256_loadu_ps(dst + i);
		__m256 x = _mm256_mul_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i));
		_mm256_storeu_ps(dst + i, _mm256_add_ps(d, x));
	}
	for (; i < n; ++i) dst[i] += a[i] * b[i];
	_mm256_zeroupper();
}

static void MaddF32_FMA(float* dst, const float* a, const float* b, size_t n)
{
	size_t i = 0;
	for (; i + 8 <= n; i += 8) {
		__m256 d = _mm256_loadu_ps(dst + i);
		__m256 x = _mm256_loadu_ps(a + i);
		__m256 y = _mm256_loadu_ps(b + i);
		_mm256_storeu_ps(dst + i, _mm256_fmadd_ps(x, y, d));
	}
	for (; i < n; ++i) dst[i] += a[i] * b[i];
	_mm256_zeroupper();
}
#else
static void Copy_AVX2(void* dst, const void* src, size_t n) { Copy_Scalar(dst, src, n); }
static void Zero_AVX2(void* dst, size_t n) { Zero_Scalar(dst, n); }
static void AddF32_AVX2(float* dst, const float* src, size_t n) { AddF32_Scalar(dst, src, n); }
static void MulF32_AVX2(float* dst, const float* src, size_t n) { MulF32_Scalar(dst, src, n); }
static void MaddF32_AVX2(float* dst, const float* a, const float* b, size_t n) { MaddF32_Scalar(dst, a, b, n); }
#endif

static SimdKernels g_k;
static int g_bound;

static void BindOnce()
{
	if (g_bound) return;
	const CpuIsa& isa = SimdCpu::Get();
	if (isa.avx10_512) {
		g_k.Copy = SimdKernels_Copy_AVX10;
		g_k.Zero = SimdKernels_Zero_AVX10;
		g_k.AddF32 = SimdKernels_AddF32_AVX10;
		g_k.MulF32 = SimdKernels_MulF32_AVX10;
		g_k.MaddF32 = SimdKernels_MaddF32_AVX10;
		g_k.name = "AVX10/512";
	} else if (isa.avx512) {
		g_k.Copy = SimdKernels_Copy_AVX512;
		g_k.Zero = SimdKernels_Zero_AVX512;
		g_k.AddF32 = SimdKernels_AddF32_AVX512;
		g_k.MulF32 = SimdKernels_MulF32_AVX512;
		g_k.MaddF32 = SimdKernels_MaddF32_AVX512;
		g_k.name = "AVX512";
	} else if (isa.avx2 || isa.avx10) {
		g_k.Copy = Copy_AVX2;
		g_k.Zero = Zero_AVX2;
		g_k.AddF32 = AddF32_AVX2;
		g_k.MulF32 = MulF32_AVX2;
		g_k.MaddF32 = isa.fma ? MaddF32_FMA : MaddF32_AVX2;
		g_k.name = isa.fma ? "AVX2+FMA" : "AVX2";
	} else {
		g_k.Copy = Copy_Scalar;
		g_k.Zero = Zero_Scalar;
		g_k.AddF32 = AddF32_Scalar;
		g_k.MulF32 = MulF32_Scalar;
		g_k.MaddF32 = MaddF32_Scalar;
		g_k.name = "scalar";
	}
	g_bound = 1;
}

const SimdKernels& SimdKernels::Get()
{
	BindOnce();
	return g_k;
}
