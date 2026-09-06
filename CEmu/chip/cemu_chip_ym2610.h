#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Yamaha YM2610/OPNB via ymfm. */
CChip* CEmuChipYm2610Create(uint32_t clockHz, int sampleRate);
void CEmuChipYm2610Destroy(CChip* c);
