#pragma once
#include "cemu_chip.h"
#include <stdint.h>

/* Konami SCC / SCC-compatible: 5 channels, 32-byte waves (ch4 shares ch3's
   wave on the original chip). Register file is 0x00-0x8F as seen through the
   0x9800/0xB800 windows. */
CChip* CEmuChipSccCreate(uint32_t clockHz, int sampleRate);
void CEmuChipSccDestroy(CChip* c);

/* Write one SCC register (0x00-0x8F). Also updates the FM monitor. */
void CEmuChipSccWriteReg(CChip* c, unsigned reg, uint8_t data);
