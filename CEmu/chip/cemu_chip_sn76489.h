#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* TI SN76489 / SN76489A (SG-1000 / SC-3000 PSG). */
CChip* CEmuChipSn76489Create(uint32_t clockHz, int sampleRate);
void CEmuChipSn76489Destroy(CChip* c);
unsigned CEmuChipSn76489WriteCount(const CChip* c);
