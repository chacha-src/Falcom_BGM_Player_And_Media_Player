#if (defined(_M_X64) || defined(__x86_64__)) && (defined(__AVX512F__) || defined(__AVX512__))
#include <immintrin.h>
#include <math.h>

/* CAnalyzerDlg の FftRadix2 と同じ回転子の漸化式。16 本だけまとめてバタフライする。 */

static void Butterfly16(float* re, float* im, int base, int half,
	float* wRe, float* wIm, float wlenRe, float wlenIm)
{
	alignas(64) float twR[16], twI[16];
	float wr = *wRe;
	float wi = *wIm;
	for (int t = 0; t < 16; ++t) {
		twR[t] = wr;
		twI[t] = wi;
		const float nwr = wr * wlenRe - wi * wlenIm;
		wi = wr * wlenIm + wi * wlenRe;
		wr = nwr;
	}
	*wRe = wr;
	*wIm = wi;

	const __m512 twRv = _mm512_load_ps(twR);
	const __m512 twIv = _mm512_load_ps(twI);
	const __m512 reU = _mm512_loadu_ps(re + base);
	const __m512 imU = _mm512_loadu_ps(im + base);
	const __m512 reV = _mm512_loadu_ps(re + base + half);
	const __m512 imV = _mm512_loadu_ps(im + base + half);
	const __m512 vRe = _mm512_sub_ps(_mm512_mul_ps(reV, twRv), _mm512_mul_ps(imV, twIv));
	const __m512 vIm = _mm512_add_ps(_mm512_mul_ps(reV, twIv), _mm512_mul_ps(imV, twRv));
	_mm512_storeu_ps(re + base, _mm512_add_ps(reU, vRe));
	_mm512_storeu_ps(im + base, _mm512_add_ps(imU, vIm));
	_mm512_storeu_ps(re + base + half, _mm512_sub_ps(reU, vRe));
	_mm512_storeu_ps(im + base + half, _mm512_sub_ps(imU, vIm));
}

void AnalyzerFftRadix2_AVX512(float* re, float* im, int n)
{
	int j = 0;
	for (int i = 1; i < n; ++i) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) {
			float tr = re[i]; re[i] = re[j]; re[j] = tr;
			float ti = im[i]; im[i] = im[j]; im[j] = ti;
		}
	}
	for (int len = 2; len <= n; len <<= 1) {
		const float ang = -6.28318530718f / (float)len;
		const float wlenRe = cosf(ang);
		const float wlenIm = sinf(ang);
		const int half = len >> 1;
		for (int i = 0; i < n; i += len) {
			float wRe = 1.0f, wIm = 0.0f;
			int k = 0;
			if (half >= 16) {
				for (; k + 16 <= half; k += 16)
					Butterfly16(re, im, i + k, half, &wRe, &wIm, wlenRe, wlenIm);
			}
			for (; k < half; ++k) {
				const float uRe = re[i + k];
				const float uIm = im[i + k];
				const float vRe = re[i + k + half] * wRe - im[i + k + half] * wIm;
				const float vIm = re[i + k + half] * wIm + im[i + k + half] * wRe;
				re[i + k] = uRe + vRe;
				im[i + k] = uIm + vIm;
				re[i + k + half] = uRe - vRe;
				im[i + k + half] = uIm - vIm;
				const float nWRe = wRe * wlenRe - wIm * wlenIm;
				wIm = wRe * wlenIm + wIm * wlenRe;
				wRe = nWRe;
			}
		}
	}
	_mm256_zeroupper();
}

#else

void AnalyzerFftRadix2_AVX512(float* re, float* im, int n)
{
	(void)re;
	(void)im;
	(void)n;
}

#endif
