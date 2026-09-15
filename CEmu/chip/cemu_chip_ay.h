#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* AY-3-8910 / YM2149（fmgen PSG）。Write(addr): 偶数=ラッチ、奇数=データ。 */
CChip* CEmuChipAyCreate(uint32_t clockHz, int sampleRate);
void CEmuChipAySetPortA(CChip* c, uint8_t v);
void CEmuChipAyDestroy(CChip* c);
unsigned CEmuChipAyWriteCount(const CChip* c);
/* AYレジスタ 0..15 をコピー。nullなら0。 */
int CEmuChipAyPeekRegs(const CChip* c, unsigned char* out16);
void CEmuChipAyPeekRegWrites(const CChip* c, unsigned out16[16]);
