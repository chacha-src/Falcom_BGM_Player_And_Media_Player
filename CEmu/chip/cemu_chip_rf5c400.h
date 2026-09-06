#pragma once
#include "cemu_chip.h"
#include <stdint.h>

/* Stub: Hornet RF5C400 — holds wave ROM; MixAdd/Render stay silent until a
   real RF5C400 core + 68000 sound map are wired. */
CChip* CEmuChipRf5c400Create(uint32_t clockHz, int sampleRate);
void CEmuChipRf5c400Destroy(CChip* c);
