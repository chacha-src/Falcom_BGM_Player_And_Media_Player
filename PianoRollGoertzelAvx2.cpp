#include "stdafx.h"
#include "PianoRollGoertzelAvx2.h"
#include <immintrin.h>
#include <cmath>

namespace
{
    static inline double GoertzelMagnitudeScalar(
        const double* samples, int numSamples, double coefficient)
    {
        double s_prev = 0.0, s_prev2 = 0.0;
        for (int n = 0; n < numSamples; ++n) {
            const double x = samples[n];
            const double s = x + coefficient * s_prev - s_prev2;
            s_prev2 = s_prev;
            s_prev = s;
        }
        const double power = s_prev2 * s_prev2 + s_prev * s_prev - coefficient * s_prev * s_prev2;
        return sqrt(power > 0.0 ? power : 0.0) * 2.5 / (double)numSamples;
    }

    static void GoertzelBatch4Avx2(
        const double* samples, int numSamples,
        const double* coeffs4, double* out4)
    {
        __m256d s_prev = _mm256_setzero_pd();
        __m256d s_prev2 = _mm256_setzero_pd();
        const __m256d coeff = _mm256_loadu_pd(coeffs4);

        for (int n = 0; n < numSamples; ++n) {
            const __m256d x = _mm256_set1_pd(samples[n]);
            const __m256d s = _mm256_sub_pd(
                _mm256_add_pd(x, _mm256_mul_pd(coeff, s_prev)),
                s_prev2);
            s_prev2 = s_prev;
            s_prev = s;
        }

        const __m256d prod = _mm256_mul_pd(s_prev, s_prev2);
        __m256d power = _mm256_sub_pd(
            _mm256_add_pd(_mm256_mul_pd(s_prev2, s_prev2), _mm256_mul_pd(s_prev, s_prev)),
            _mm256_mul_pd(coeff, prod));
        power = _mm256_max_pd(power, _mm256_setzero_pd());
        power = _mm256_sqrt_pd(power);

        const __m256d scale = _mm256_set1_pd(2.5 / (double)numSamples);
        power = _mm256_mul_pd(power, scale);
        _mm256_storeu_pd(out4, power);
    }
}

void PianoRollGoertzelBatchAvx2(
    const double* samples,
    int numSamples,
    const double* coeffs,
    int keyBegin,
    int keyEnd,
    double* rawOut)
{
    if (!samples || !coeffs || !rawOut || numSamples <= 0 || keyBegin >= keyEnd)
        return;

    int key = keyBegin;
    const int end4 = keyEnd - ((keyEnd - keyBegin) & 3);
    alignas(32) double coeffs4[4];
    alignas(32) double out4[4];

    for (; key < end4; key += 4) {
        coeffs4[0] = coeffs[key];
        coeffs4[1] = coeffs[key + 1];
        coeffs4[2] = coeffs[key + 2];
        coeffs4[3] = coeffs[key + 3];
        GoertzelBatch4Avx2(samples, numSamples, coeffs4, out4);
        rawOut[key - keyBegin] = out4[0];
        rawOut[key - keyBegin + 1] = out4[1];
        rawOut[key - keyBegin + 2] = out4[2];
        rawOut[key - keyBegin + 3] = out4[3];
    }

    for (; key < keyEnd; ++key) {
        rawOut[key - keyBegin] = GoertzelMagnitudeScalar(
            samples, numSamples, coeffs[key]);
    }
}
