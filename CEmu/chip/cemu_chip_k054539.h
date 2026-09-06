#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Konami K054539, 8ch PCM. */
CChip* CEmuChipK054539Create(uint32_t clockHz, int sampleRate);
void CEmuChipK054539Destroy(CChip* c);
void CEmuChipK054539SetFmMonBase(CChip* c, int baseChannel);
uint8_t CEmuChipK054539PeekReg(CChip* c, unsigned off);
void CEmuChipK054539GetMetrics(CChip* c, unsigned* timerFires, unsigned* keyOns);
