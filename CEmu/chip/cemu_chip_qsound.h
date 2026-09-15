#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Capcom QSound、16ch 符号付き8bit PCM。 */
CChip* CEmuChipQSoundCreate(uint32_t clockHz, int sampleRate);
void CEmuChipQSoundDestroy(CChip* c);
void CEmuChipQSoundWriteCommand(CChip* c, uint8_t data);
