#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Ricoh RF5C68、8ch PCM。 */
CChip* CEmuChipRf5c68Create(uint32_t clockHz, int sampleRate);
void CEmuChipRf5c68Destroy(CChip* c);
/* MAME rf5c68_mem_r/w: 4KB 窓。addr は 0..0xFFF。 */
uint8_t CEmuChipRf5c68MemR(CChip* c, unsigned addr);
void CEmuChipRf5c68MemW(CChip* c, unsigned addr, uint8_t v);
