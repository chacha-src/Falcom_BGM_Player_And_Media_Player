#include "simd_kernels.h"
#include <string.h>

#if (defined(_M_X64) || defined(__x86_64__)) && (defined(__AVX512F__) || defined(__AVX512__))
#include <immintrin.h>

void SimdKernels_Copy_AVX512(void* dst, const void* src, size_t n)
{
	unsigned char* d = (unsigned char*)dst;
	const unsigned char* s = (const unsigned char*)src;
	size_t i = 0;
	for (; i + 64 <= n; i += 64) {
		__m512i v = _mm512_loadu_si512((const void*)(s + i));
		_mm512_storeu_si512((void*)(d + i), v);
	}
	for (; i + 32 <= n; i += 32) {
		__m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
		_mm256_storeu_si256((__m256i*)(d + i), v);
	}
	if (i < n) memcpy(d + i, s + i, n - i);
	_mm256_zeroupper();
}

void SimdKernels_Zero_AVX512(void* dst, size_t n)
{
	unsigned char* d = (unsigned char*)dst;
	size_t i = 0;
	const __m512i z = _mm512_setzero_si512();
	for (; i + 64 <= n; i += 64)
		_mm512_storeu_si512((void*)(d + i), z);
	for (; i + 32 <= n; i += 32)
		_mm256_storeu_si256((__m256i*)(d + i), _mm256_setzero_si256());
	if (i < n) memset(d + i, 0, n - i);
	_mm256_zeroupper();
}

void SimdKernels_AddF32_AVX512(float* dst, const float* src, size_t n)
{
	size_t i = 0;
	for (; i + 16 <= n; i += 16) {
		__m512 a = _mm512_loadu_ps(dst + i);
		__m512 b = _mm512_loadu_ps(src + i);
		_mm512_storeu_ps(dst + i, _mm512_add_ps(a, b));
	}
	for (; i < n; ++i) dst[i] += src[i];
	_mm256_zeroupper();
}

void SimdKernels_MulF32_AVX512(float* dst, const float* src, size_t n)
{
	size_t i = 0;
	for (; i + 16 <= n; i += 16) {
		__m512 a = _mm512_loadu_ps(dst + i);
		__m512 b = _mm512_loadu_ps(src + i);
		_mm512_storeu_ps(dst + i, _mm512_mul_ps(a, b));
	}
	for (; i < n; ++i) dst[i] *= src[i];
	_mm256_zeroupper();
}

void SimdKernels_MaddF32_AVX512(float* dst, const float* a, const float* b, size_t n)
{
	size_t i = 0;
	for (; i + 16 <= n; i += 16) {
		__m512 d = _mm512_loadu_ps(dst + i);
		__m512 x = _mm512_loadu_ps(a + i);
		__m512 y = _mm512_loadu_ps(b + i);
		_mm512_storeu_ps(dst + i, _mm512_fmadd_ps(x, y, d));
	}
	for (; i < n; ++i) dst[i] += a[i] * b[i];
	_mm256_zeroupper();
}

#else

void SimdKernels_Copy_AVX512(void* dst, const void* src, size_t n)
{
	if (n) memcpy(dst, src, n);
}
void SimdKernels_Zero_AVX512(void* dst, size_t n)
{
	if (n) memset(dst, 0, n);
}
void SimdKernels_AddF32_AVX512(float* dst, const float* src, size_t n)
{
	for (size_t i = 0; i < n; ++i) dst[i] += src[i];
}
void SimdKernels_MulF32_AVX512(float* dst, const float* src, size_t n)
{
	for (size_t i = 0; i < n; ++i) dst[i] *= src[i];
}
void SimdKernels_MaddF32_AVX512(float* dst, const float* a, const float* b, size_t n)
{
	for (size_t i = 0; i < n; ++i) dst[i] += a[i] * b[i];
}

#endif
