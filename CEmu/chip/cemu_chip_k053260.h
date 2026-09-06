#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Konami K053260, 4ch PCM + main/sound communication ports. */
CChip* CEmuChipK053260Create(uint32_t clockHz, int sampleRate);
void CEmuChipK053260Destroy(CChip* c);

/* Sound-CPU side register read (ports 0/1 = main→sound latch). */
uint8_t CEmuChipK053260Read(CChip* c, unsigned offset);
/* Main-CPU side communication write (posts song bytes into ports 0/1). */
void CEmuChipK053260MainWrite(CChip* c, unsigned offset, uint8_t data);
uint8_t CEmuChipK053260MainRead(CChip* c, unsigned offset);
