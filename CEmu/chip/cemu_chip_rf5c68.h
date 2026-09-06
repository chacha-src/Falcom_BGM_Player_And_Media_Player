#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Ricoh RF5C68, 8ch PCM. */
CChip* CEmuChipRf5c68Create(uint32_t clockHz, int sampleRate);
void CEmuChipRf5c68Destroy(CChip* c);
