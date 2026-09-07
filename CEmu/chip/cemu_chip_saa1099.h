#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Philips SAA1099 (CMS / Game Blaster). Write(addr): even=data, odd=control.
   Clock typically 7159090 Hz (14.31818MHz/2). Ported from MAME saa1099. */

CChip* CEmuChipSaa1099Create(uint32_t clockHz, int sampleRate);
void CEmuChipSaa1099Destroy(CChip* c);
unsigned CEmuChipSaa1099WriteCount(const CChip* c);
unsigned CEmuChipSaa1099ToneOnCount(const CChip* c);
