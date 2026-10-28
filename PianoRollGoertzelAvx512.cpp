#include "PianoRollGoertzelAvx2.h"

#if (defined(_M_X64) || defined(__x86_64__)) && (defined(__AVX512F__) || defined(__AVX512__))
#include <immintrin.h>

void PianoRollGoertzel8_AVX512(
    const double* samples, int numSamples, const double* coeffs8, double* out8)
{
	__m512d s_prev = _mm512_setzero_pd();
	__m512d s_prev2 = _mm512_setzero_pd();
	const __m512d coeff = _mm512_loadu_pd(coeffs8);

	for (int n = 0; n < numSamples; ++n) {
		const __m512d x = _mm512_set1_pd(samples[n]);
		const __m512d s = _mm512_sub_pd(
			_mm512_add_pd(x, _mm512_mul_pd(coeff, s_prev)),
			s_prev2);
		s_prev2 = s_prev;
		s_prev = s;
	}

	const __m512d prod = _mm512_mul_pd(s_prev, s_prev2);
	__m512d power = _mm512_sub_pd(
		_mm512_add_pd(_mm512_mul_pd(s_prev2, s_prev2), _mm512_mul_pd(s_prev, s_prev)),
		_mm512_mul_pd(coeff, prod));
	power = _mm512_max_pd(power, _mm512_setzero_pd());
	power = _mm512_sqrt_pd(power);
	const __m512d scale = _mm512_set1_pd(2.5 / (double)numSamples);
	_mm512_storeu_pd(out8, _mm512_mul_pd(power, scale));
	_mm256_zeroupper();
}

#else

void PianoRollGoertzel8_AVX512(
    const double* samples, int numSamples, const double* coeffs8, double* out8)
{
	(void)samples;
	(void)numSamples;
	(void)coeffs8;
	(void)out8;
}

#endif
