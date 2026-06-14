#pragma once

// 4鍵並列 Goertzel（AVX2）。coeffs[keyBegin..keyEnd) を rawOut[0..] に書く。
void PianoRollGoertzelBatchAvx2(
    const double* samples,
    int numSamples,
    const double* coeffs,
    int keyBegin,
    int keyEnd,
    double* rawOut);
