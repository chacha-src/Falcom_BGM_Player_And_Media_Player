#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* OKI MSM6295, 4ch ADPCM. */
CChip* CEmuChipOki6295Create(uint32_t clockHz, int sampleRate);
void CEmuChipOki6295Destroy(CChip* c);
