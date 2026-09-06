#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Yamaha YM2612/OPN2 via ymfm. */
CChip* CEmuChipYm2612Create(uint32_t clockHz, int sampleRate);
void CEmuChipYm2612Destroy(CChip* c);
