/* AVX10/512 用 FFT。レガシー AVX512F が無くても同じ 16 本バタフライを使う。 */
#define AnalyzerFftRadix2_AVX512 AnalyzerFftRadix2_AVX10
#include "AnalyzerFftAvx512.cpp"
#undef AnalyzerFftRadix2_AVX512
