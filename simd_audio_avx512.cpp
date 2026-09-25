#include "simd_audio.h"
#include <math.h>
#include <float.h>

#if (defined(_M_X64) || defined(__x86_64__)) && (defined(__AVX512F__) || defined(__AVX512__))
#include <immintrin.h>

static int finite_d(double v)
{
#if defined(_MSC_VER)
	return _finite(v) ? 1 : 0;
#else
	return isfinite(v) ? 1 : 0;
#endif
}

void SimdFloatPeakMean_AVX512(const float* src, size_t n, double* maxAbs, double* sumAbs, size_t* validCount)
{
	size_t i = 0;
	__m512 vmax = _mm512_setzero_ps();
	__m512 vsum = _mm512_setzero_ps();
	size_t valid = 0;
	const __m512 absmask = _mm512_castsi512_ps(_mm512_set1_epi32(0x7fffffff));
	for (; i + 16 <= n; i += 16) {
		__m512 x = _mm512_loadu_ps(src + i);
		__mmask16 fin = _mm512_fpclass_ps_mask(x, 0x99); // snan/qnan/+inf/-inf
		fin = (__mmask16)~fin;
		__m512 a = _mm512_and_ps(x, absmask);
		a = _mm512_maskz_mov_ps(fin, a);
		vmax = _mm512_max_ps(vmax, a);
		vsum = _mm512_add_ps(vsum, a);
		valid += (size_t)_mm_popcnt_u32((unsigned)fin);
	}
	alignas(64) float tmp[16];
	_mm512_storeu_ps(tmp, vmax);
	double mx = 0.0, sm = 0.0;
	for (int k = 0; k < 16; ++k) if ((double)tmp[k] > mx) mx = (double)tmp[k];
	_mm512_storeu_ps(tmp, vsum);
	for (int k = 0; k < 16; ++k) sm += (double)tmp[k];
	for (; i < n; ++i) {
		double d = (double)src[i];
		if (!finite_d(d)) continue;
		double a = fabs(d);
		if (a > mx) mx = a;
		sm += a;
		++valid;
	}
	*maxAbs = mx;
	*sumAbs = sm;
	*validCount = valid;
}

void SimdFloatToInt16_AVX512(const float* src, short* dst, size_t n, double scale)
{
	size_t i = 0;
	const __m512 vscale = _mm512_set1_ps((float)scale);
	for (; i + 16 <= n; i += 16) {
		__m512 x = _mm512_mul_ps(_mm512_loadu_ps(src + i), vscale);
		x = _mm512_max_ps(x, _mm512_set1_ps(-32768.0f));
		x = _mm512_min_ps(x, _mm512_set1_ps(32767.0f));
		__m512i i32 = _mm512_cvtps_epi32(x);
		__m256i packed = _mm512_cvtsepi32_epi16(i32);
		_mm256_storeu_si256((__m256i*)(dst + i), packed);
	}
	for (; i < n; ++i) {
		double s = (double)src[i] * scale;
		if (!finite_d(s)) {
			dst[i] = 0;
			continue;
		}
		if (s > 32767.0) s = 32767.0;
		else if (s < -32768.0) s = -32768.0;
		dst[i] = (short)s;
	}
}

void SimdDoubleToInt16Kbpsf2_AVX512(const double* src, short* dst, size_t n)
{
	size_t i = 0;
	const __m512d vscale = _mm512_set1_pd(32768.0);
	for (; i + 8 <= n; i += 8) {
		__m512d x = _mm512_mul_pd(_mm512_loadu_pd(src + i), vscale);
		x = _mm512_max_pd(x, _mm512_set1_pd(-32768.0));
		x = _mm512_min_pd(x, _mm512_set1_pd(32767.0));
		__m256i i32 = _mm512_cvtpd_epi32(x);
		__m128i lo = _mm256_castsi256_si128(i32);
		__m128i hi = _mm256_extracti128_si256(i32, 1);
		_mm_storeu_si128((__m128i*)(dst + i), _mm_packs_epi32(lo, hi));
	}
	for (; i < n; ++i) {
		double s = src[i] * 32768.0;
		if (!finite_d(s)) {
			dst[i] = 0;
			continue;
		}
		if (s > 32767.0) s = 32767.0;
		else if (s < -32768.0) s = -32768.0;
		dst[i] = (short)(s >= 0.0 ? (s + 0.5) : (s - 0.5));
	}
}

void SimdI16CopySatFromI32_AVX512(const int* src, short* dst, size_t n)
{
	size_t i = 0;
	for (; i + 16 <= n; i += 16) {
		__m512i x = _mm512_loadu_si512((const void*)(src + i));
		x = _mm512_min_epi32(_mm512_max_epi32(x, _mm512_set1_epi32(-32767)), _mm512_set1_epi32(32767));
		__m256i packed = _mm512_cvtsepi32_epi16(x);
		_mm256_storeu_si256((__m256i*)(dst + i), packed);
	}
	for (; i < n; ++i) {
		int x = src[i];
		if (x > 32767) x = 32767;
		else if (x < -32767) x = -32767;
		dst[i] = (short)x;
	}
}

void SimdMixI16Sat_AVX512(short* acc, const short* add, size_t n)
{
	size_t i = 0;
	for (; i + 32 <= n; i += 32) {
		__m512i a = _mm512_loadu_si512((const void*)(acc + i));
		__m512i b = _mm512_loadu_si512((const void*)(add + i));
		_mm512_storeu_si512((void*)(acc + i), _mm512_adds_epi16(a, b));
	}
	for (; i < n; ++i) {
		int x = (int)acc[i] + (int)add[i];
		if (x > 32767) x = 32767;
		else if (x < -32768) x = -32768;
		acc[i] = (short)x;
	}
}

#else
// Win32 や AVX512 無しでこの TU をコンパイルしたときのスタブ。
void SimdFloatPeakMean_AVX512(const float* src, size_t n, double* maxAbs, double* sumAbs, size_t* validCount)
{
	double mx = 0.0, sm = 0.0;
	size_t v = 0;
	for (size_t i = 0; i < n; ++i) {
		double d = (double)src[i];
		if (!_finite(d)) continue;
		double a = fabs(d);
		if (a > mx) mx = a;
		sm += a;
		++v;
	}
	*maxAbs = mx;
	*sumAbs = sm;
	*validCount = v;
}
void SimdFloatToInt16_AVX512(const float* src, short* dst, size_t n, double scale)
{
	for (size_t i = 0; i < n; ++i) {
		double s = (double)src[i] * scale;
		if (!_finite(s)) { dst[i] = 0; continue; }
		if (s > 32767.0) s = 32767.0;
		else if (s < -32768.0) s = -32768.0;
		dst[i] = (short)s;
	}
}
void SimdDoubleToInt16Kbpsf2_AVX512(const double* src, short* dst, size_t n)
{
	for (size_t i = 0; i < n; ++i) {
		double s = src[i] * 32768.0;
		if (!_finite(s)) { dst[i] = 0; continue; }
		if (s > 32767.0) s = 32767.0;
		else if (s < -32768.0) s = -32768.0;
		dst[i] = (short)(s >= 0.0 ? (s + 0.5) : (s - 0.5));
	}
}
void SimdI16CopySatFromI32_AVX512(const int* src, short* dst, size_t n)
{
	for (size_t i = 0; i < n; ++i) {
		int x = src[i];
		if (x > 32767) x = 32767;
		else if (x < -32767) x = -32767;
		dst[i] = (short)x;
	}
}
void SimdMixI16Sat_AVX512(short* acc, const short* add, size_t n)
{
	for (size_t i = 0; i < n; ++i) {
		int x = (int)acc[i] + (int)add[i];
		if (x > 32767) x = 32767;
		else if (x < -32768) x = -32768;
		acc[i] = (short)x;
	}
}
#endif
