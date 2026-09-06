#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Namco C352, 32ch 16-bit PCM. */
CChip* CEmuChipC352Create(uint32_t clockHz, int sampleRate);
void CEmuChipC352Destroy(CChip* c);
