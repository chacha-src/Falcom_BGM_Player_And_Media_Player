#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* YM3812 (OPL2 / AdLib). Write(addr): even=address latch, odd=data.
   Clock typically 3579545 Hz. */
CChip* CEmuChipYm3812Create(uint32_t clockHz, int sampleRate);
void CEmuChipYm3812Destroy(CChip* c);
unsigned CEmuChipYm3812WriteCount(const CChip* c);
unsigned CEmuChipYm3812KeyOnCount(const CChip* c);
const unsigned* CEmuChipYm3812RegHist(const CChip* c);
