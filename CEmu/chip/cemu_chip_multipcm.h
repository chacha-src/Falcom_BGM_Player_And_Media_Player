#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Sega/Yamaha 315-5560 MultiPCM（YMW-258-F）。 */
CChip* CEmuChipMultiPcmCreate(uint32_t clockHz, int sampleRate, int chipId);
void CEmuChipMultiPcmDestroy(CChip* c);
void CEmuChipMultiPcmSetBank(CChip* c, unsigned bankMb);
/* MAME multi32: 512KiB 窓 2 本（lo=bits0-2、hi=bits3-5。scross は同じ値）。 */
void CEmuChipMultiPcmSetBankPair(CChip* c, unsigned lo512, unsigned hi512);
