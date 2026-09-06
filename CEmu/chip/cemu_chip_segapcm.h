#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Sega 315-5218 PCM, 16ch 8-bit.
   bankShift/bankMask: MAME set_bank — AB/OutRun use BANK_512 → shift=12, mask=0x70. */
CChip* CEmuChipSegaPcmCreate(uint32_t clockHz, int sampleRate, unsigned bankShift, unsigned bankMask);
/* Hang-On / Space Harrier discrete logic PCM (8ch, no bank). */
CChip* CEmuChipSegaPcmCreateDiscrete(uint32_t clockHz, int sampleRate);
void CEmuChipSegaPcmDestroy(CChip* c);
