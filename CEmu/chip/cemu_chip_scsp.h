#pragma once
#include "cemu_chip.h"
#include <stdint.h>

/* Stub: Model 2A/3 SCSP — holds wave ROM; MixAdd/Render stay silent until a
   real SCSP synthesizer is ported (68K host alone is not enough). */
CChip* CEmuChipScspCreate(uint32_t clockHz, int sampleRate);
void CEmuChipScspDestroy(CChip* c);
