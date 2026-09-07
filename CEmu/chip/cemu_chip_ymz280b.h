#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Yamaha YMZ280B (PCMD8), 8 channels of 4-bit ADPCM / 8-bit / 16-bit PCM
   streamed from an external sample ROM. The host interface is two ports:
   write the register number, then the value. */
CChip* CEmuChipYmz280bCreate(uint32_t clockHz, int sampleRate);
void CEmuChipYmz280bDestroy(CChip* c);

/* Port 0 = register select, port 1 = data. */
void CEmuChipYmz280bWritePort(CChip* c, unsigned port, uint8_t data);
uint8_t CEmuChipYmz280bReadStatus(CChip* c);
