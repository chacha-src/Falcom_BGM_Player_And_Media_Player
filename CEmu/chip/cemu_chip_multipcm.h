#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Sega/Yamaha 315-5560 MultiPCM (YMW-258-F). */
CChip* CEmuChipMultiPcmCreate(uint32_t clockHz, int sampleRate, int chipId);
void CEmuChipMultiPcmDestroy(CChip* c);
void CEmuChipMultiPcmSetBank(CChip* c, unsigned bankMb);
