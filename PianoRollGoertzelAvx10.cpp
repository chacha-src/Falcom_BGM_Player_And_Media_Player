/* AVX10/512 用 Goertzel。レガシー AVX512F が無くても同じ 8 鍵並列を使う。 */
#define PianoRollGoertzel8_AVX512 PianoRollGoertzel8_AVX10
#include "PianoRollGoertzelAvx512.cpp"
#undef PianoRollGoertzel8_AVX512
