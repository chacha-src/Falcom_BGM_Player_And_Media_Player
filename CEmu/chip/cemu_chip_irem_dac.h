#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Irem M72 8-bit sample DAC (m72_audio_device). The sound CPU pumps one
   unsigned byte per NMI; Write(0, level) sets the current DAC level. */
CChip* CEmuChipIremDacCreate(int sampleRate);
void CEmuChipIremDacDestroy(CChip* c);
