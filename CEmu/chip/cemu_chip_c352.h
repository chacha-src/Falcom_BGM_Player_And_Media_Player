#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Namco C352, 32ch 16-bit PCM. */
CChip* CEmuChipC352Create(uint32_t clockHz, int sampleRate);
void CEmuChipC352Destroy(CChip* c);
/* Word register read back, as the real chip and MAME's c352_device::read do.
   Voice registers live at reg 0x00-0xFF, the control register at 0x200. */
uint16_t CEmuChipC352Read(CChip* c, unsigned reg);
