#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

typedef void (*OxzPulseFn)(void* user, unsigned long long done, unsigned long long total);
/* threads==0 は逐次。戻り値 1 で、out は入力より小さい。pulse は完了した入力バイト。 */
int OxzBzipCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out, int level, unsigned threads, OxzPulseFn pulse = 0, void* pulseUser = 0);
/* 単一ストリームも、ブロックを繋いだ複数ストリームも展開する。threads==0 は逐次。 */
extern "C" int OxzBzipDecompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen, unsigned threads);
