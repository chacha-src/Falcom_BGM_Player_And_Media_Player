#include "simd_audio.h"
#include "cpu_isa.h"
#include <math.h>
#include <float.h>
#include <stdint.h>

#if defined(_MSC_VER) || defined(__INTEL_LLVM_COMPILER)
#include <immintrin.h>
#endif

static int finite_d(double v)
{
#if defined(_MSC_VER)
	return _finite(v) ? 1 : 0;
#else
	return isfinite(v) ? 1 : 0;
#endif
}

static short sat_i16_from_d(double s)
{
	if (!finite_d(s)) return 0;
	if (s > 32767.0) s = 32767.0;
	else if (s < -32768.0) s = -32768.0;
	return (short)s;
}

static void PeakMean_Scalar(const float* src, size_t n, double* maxAbs, double* sumAbs, size_t* validCount)
{
	double mx = 0.0, sm = 0.0;
	size_t v = 0;
	for (size_t i = 0; i < n; ++i) {
		double d = (double)src[i];
		if (!finite_d(d)) continue;
		double a = fabs(d);
		if (a > mx) mx = a;
		sm += a;
		++v;
	}
	*maxAbs = mx;
	*sumAbs = sm;
	*validCount = v;
}

static void FloatToI16_Scalar(const float* src, short* dst, size_t n, double scale)
{
	for (size_t i = 0; i < n; ++i) {
		dst[i] = sat_i16_from_d((double)src[i] * scale);
	}
}

static void DoubleToI16_Scalar(const double* src, short* dst, size_t n)
{
	for (size_t i = 0; i < n; ++i) {
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

static void I32ToI16_Scalar(const int* src, short* dst, size_t n)
{
	for (size_t i = 0; i < n; ++i) {
		int x = src[i];
		if (x > 32767) x = 32767;
		else if (x < -32767) x = -32767;
		dst[i] = (short)x;
	}
}

static void MixI16_Scalar(short* acc, const short* add, size_t n)
{
	for (size_t i = 0; i < n; ++i) {
		int x = (int)acc[i] + (int)add[i];
		if (x > 32767) x = 32767;
		else if (x < -32768) x = -32768;
		acc[i] = (short)x;
	}
}

#if defined(__AVX2__) || defined(_M_X64) || defined(__AVX__)
static void PeakMean_AVX2(const float* src, size_t n, double* maxAbs, double* sumAbs, size_t* validCount)
{
	size_t i = 0;
	__m256 vmax = _mm256_setzero_ps();
	__m256 vsum = _mm256_setzero_ps();
	const __m256 inf = _mm256_set1_ps(HUGE_VALF);
	const __m256 ninf = _mm256_set1_ps(-HUGE_VALF);
	const __m256 absmask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
	size_t valid = 0;
	for (; i + 8 <= n; i += 8) {
		__m256 x = _mm256_loadu_ps(src + i);
		__m256 finite = _mm256_and_ps(_mm256_cmp_ps(x, inf, _CMP_NGE_UQ), _mm256_cmp_ps(x, ninf, _CMP_NLE_UQ));
		__m256 a = _mm256_and_ps(x, absmask);
		a = _mm256_and_ps(a, finite);
		vmax = _mm256_max_ps(vmax, a);
		vsum = _mm256_add_ps(vsum, a);
		valid += (size_t)_mm_popcnt_u32((unsigned)_mm256_movemask_ps(finite));
	}
	alignas(32) float tmp[8];
	double mx = 0.0, sm = 0.0;
	_mm256_storeu_ps(tmp, vmax);
	for (int k = 0; k < 8; ++k) if ((double)tmp[k] > mx) mx = (double)tmp[k];
	_mm256_storeu_ps(tmp, vsum);
	for (int k = 0; k < 8; ++k) sm += (double)tmp[k];
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

static void FloatToI16_AVX2(const float* src, short* dst, size_t n, double scale)
{
	size_t i = 0;
	const __m256 vscale = _mm256_set1_ps((float)scale);
	const __m256 vlo = _mm256_set1_ps(-32768.0f);
	const __m256 vhi = _mm256_set1_ps(32767.0f);
	for (; i + 8 <= n; i += 8) {
		__m256 x = _mm256_mul_ps(_mm256_loadu_ps(src + i), vscale);
		x = _mm256_min_ps(_mm256_max_ps(x, vlo), vhi);
		__m256i i32 = _mm256_cvtps_epi32(x);
		__m128i lo = _mm256_castsi256_si128(i32);
		__m128i hi = _mm256_extracti128_si256(i32, 1);
		__m128i packed = _mm_packs_epi32(lo, hi);
		_mm_storeu_si128((__m128i*)(dst + i), packed);
	}
	for (; i < n; ++i) dst[i] = sat_i16_from_d((double)src[i] * scale);
}

static void MixI16_AVX2(short* acc, const short* add, size_t n)
{
	size_t i = 0;
	for (; i + 16 <= n; i += 16) {
		__m256i a = _mm256_loadu_si256((const __m256i*)(acc + i));
		__m256i b = _mm256_loadu_si256((const __m256i*)(add + i));
		_mm256_storeu_si256((__m256i*)(acc + i), _mm256_adds_epi16(a, b));
	}
	for (; i < n; ++i) {
		int x = (int)acc[i] + (int)add[i];
		if (x > 32767) x = 32767;
		else if (x < -32768) x = -32768;
		acc[i] = (short)x;
	}
}

static void I32ToI16_AVX2(const int* src, short* dst, size_t n)
{
	size_t i = 0;
	const __m256i lo = _mm256_set1_epi32(-32767);
	const __m256i hi = _mm256_set1_epi32(32767);
	for (; i + 8 <= n; i += 8) {
		__m256i x = _mm256_loadu_si256((const __m256i*)(src + i));
		x = _mm256_min_epi32(_mm256_max_epi32(x, lo), hi);
		__m128i a = _mm256_castsi256_si128(x);
		__m128i b = _mm256_extracti128_si256(x, 1);
		_mm_storeu_si128((__m128i*)(dst + i), _mm_packs_epi32(a, b));
	}
	for (; i < n; ++i) {
		int x = src[i];
		if (x > 32767) x = 32767;
		else if (x < -32767) x = -32767;
		dst[i] = (short)x;
	}
}
#else
static void PeakMean_AVX2(const float* src, size_t n, double* maxAbs, double* sumAbs, size_t* validCount)
{
	PeakMean_Scalar(src, n, maxAbs, sumAbs, validCount);
}
static void FloatToI16_AVX2(const float* src, short* dst, size_t n, double scale)
{
	FloatToI16_Scalar(src, dst, n, scale);
}
static void MixI16_AVX2(short* acc, const short* add, size_t n)
{
	MixI16_Scalar(acc, add, n);
}
static void I32ToI16_AVX2(const int* src, short* dst, size_t n)
{
	I32ToI16_Scalar(src, dst, n);
}
#endif

typedef void (*FnPeak)(const float*, size_t, double*, double*, size_t*);
typedef void (*FnF2I)(const float*, short*, size_t, double);
typedef void (*FnD2I)(const double*, short*, size_t);
typedef void (*FnI32)(const int*, short*, size_t);
typedef void (*FnMix)(short*, const short*, size_t);

static FnPeak g_peak;
static FnF2I g_f2i;
static FnD2I g_d2i;
static FnI32 g_i32;
static FnMix g_mix;
static int g_bound;

static void BindOnce()
{
	if (g_bound) return;
	const CpuIsa& isa = CpuIsaGet();
	if (isa.avx512) {
		g_peak = SimdFloatPeakMean_AVX512;
		g_f2i = SimdFloatToInt16_AVX512;
		g_d2i = SimdDoubleToInt16Kbpsf2_AVX512;
		g_i32 = SimdI16CopySatFromI32_AVX512;
		g_mix = SimdMixI16Sat_AVX512;
	} else if (isa.avx2) {
		g_peak = PeakMean_AVX2;
		g_f2i = FloatToI16_AVX2;
		g_i32 = I32ToI16_AVX2;
		g_mix = MixI16_AVX2;
		g_d2i = DoubleToI16_Scalar;
	} else {
		g_peak = PeakMean_Scalar;
		g_f2i = FloatToI16_Scalar;
		g_d2i = DoubleToI16_Scalar;
		g_i32 = I32ToI16_Scalar;
		g_mix = MixI16_Scalar;
	}
	g_bound = 1;
}

void SimdFloatPeakMean(const float* src, size_t n, double* maxAbs, double* sumAbs, size_t* validCount)
{
	BindOnce();
	g_peak(src, n, maxAbs, sumAbs, validCount);
}

void SimdFloatToInt16(const float* src, short* dst, size_t n, double scale)
{
	BindOnce();
	g_f2i(src, dst, n, scale);
}

void SimdDoubleToInt16Kbpsf2(const double* src, short* dst, size_t n)
{
	BindOnce();
	g_d2i(src, dst, n);
}

void SimdI16CopySatFromI32(const int* src, short* dst, size_t n)
{
	BindOnce();
	g_i32(src, dst, n);
}

void SimdMixI16Sat(short* acc, const short* add, size_t n)
{
	BindOnce();
	g_mix(acc, add, n);
}
