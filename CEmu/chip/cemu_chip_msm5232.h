#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* OKI MSM5232 — 8-channel square/noise tone generator (flstory melody). */
CChip* CEmuChipMsm5232Create(uint32_t clockHz, int sampleRate);
void CEmuChipMsm5232Destroy(CChip* c);
