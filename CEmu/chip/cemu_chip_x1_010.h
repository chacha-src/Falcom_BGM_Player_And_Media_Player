#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Seta X1-010, 16 channels of 8-bit PCM or 128-byte wavetable with envelope.
   The whole chip is an 8 KB RAM window: $0000-$007F are the channel control
   registers, the rest holds waveform and envelope tables that the channel
   registers index by 128-byte page. */
CChip* CEmuChipX1010Create(uint32_t clockHz, int sampleRate);
void CEmuChipX1010Destroy(CChip* c);

/* Sound RAM window access ($0000-$1FFF). */
uint8_t CEmuChipX1010Read(CChip* c, unsigned offset);
void CEmuChipX1010Write(CChip* c, unsigned offset, uint8_t data);
