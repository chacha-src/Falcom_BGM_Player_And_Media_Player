#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Irem/Nanao GA20 4-channel PCM (M92 / M107). Write(addr 0x00-0x1f, data). */
CChip* CEmuChipGa20Create(uint32_t clockHz, int sampleRate);
void CEmuChipGa20Destroy(CChip* c);
