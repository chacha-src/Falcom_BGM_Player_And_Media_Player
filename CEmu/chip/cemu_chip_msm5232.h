#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* OKI MSM5232 — 8ch 矩形/ノイズ音源（flstory メロディ）。 */
CChip* CEmuChipMsm5232Create(uint32_t clockHz, int sampleRate);
void CEmuChipMsm5232Destroy(CChip* c);
