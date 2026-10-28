#pragma once

// 8鍵並列。AVX512 / AVX10-512 のときだけ呼ぶ。
void PianoRollGoertzel8_AVX512(
    const double* samples, int numSamples, const double* coeffs8, double* out8);

// 4鍵並列 Goertzel（AVX2）。coeffs[keyBegin..keyEnd) を rawOut[0..] に書く。
void PianoRollGoertzelBatchAvx2(
    const double* samples,
    int numSamples,
    const double* coeffs,
    int keyBegin,
    int keyEnd,
    double* rawOut);
