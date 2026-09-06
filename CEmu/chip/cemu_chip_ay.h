#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* AY-3-8910 / YM2149 (fmgen PSG). Write(addr): even=latch, odd=data.
   unmuteAssist: MSX/X1 audible floor for muted mixer/vol. Off elsewhere. */
CChip* CEmuChipAyCreate(uint32_t clockHz, int sampleRate);
void CEmuChipAySetUnmuteAssist(CChip* c, int enable);
void CEmuChipAySetPortA(CChip* c, uint8_t v);
void CEmuChipAyDestroy(CChip* c);
unsigned CEmuChipAyWriteCount(const CChip* c);
/* Copy AY regs 0..15. Returns 0 if null. */
int CEmuChipAyPeekRegs(const CChip* c, unsigned char* out16);
void CEmuChipAyPeekRegWrites(const CChip* c, unsigned out16[16]);
