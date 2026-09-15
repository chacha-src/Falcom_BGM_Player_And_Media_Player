#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Philips SAA1099（CMS / Game Blaster）。Write(addr): 偶数=データ、奇数=制御。
   クロックは通常 7159090 Hz（14.31818MHz/2）。MAME saa1099 からの移植。 */
CChip* CEmuChipSaa1099Create(uint32_t clockHz, int sampleRate);
void CEmuChipSaa1099Destroy(CChip* c);
unsigned CEmuChipSaa1099WriteCount(const CChip* c);
unsigned CEmuChipSaa1099ToneOnCount(const CChip* c);
