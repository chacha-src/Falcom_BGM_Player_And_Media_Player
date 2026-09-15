#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Sega 315-5218 PCM、16ch 8bit。
   bankShift/bankMask: MAME set_bank — AB/OutRun は BANK_512 → shift=12, mask=0x70。 */
CChip* CEmuChipSegaPcmCreate(uint32_t clockHz, int sampleRate, unsigned bankShift, unsigned bankMask);
/* Hang-On / Space Harrier ディスクリート論理PCM（8ch、バンクなし）。 */
CChip* CEmuChipSegaPcmCreateDiscrete(uint32_t clockHz, int sampleRate);
void CEmuChipSegaPcmDestroy(CChip* c);
