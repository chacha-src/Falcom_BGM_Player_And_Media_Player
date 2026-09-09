#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* OKI MSM6295, 4ch ADPCM. */
CChip* CEmuChipOki6295Create(uint32_t clockHz, int sampleRate);
void CEmuChipOki6295Destroy(CChip* c);

/* Paged sample ROM (NMK112, and the GAL that stands in for it on Battle
   Garegga / Batrider). The chip's 0x40000 address space is split into eight
   windows — four 0x100 phrase-table pages, then 0x400-0xFFFF and three 64K
   pages — each of which selects a 64K bank of the sample ROM. Points at
   eight live entries owned by the caller; NULL restores a flat ROM. */
void CEmuChipOki6295SetBankTable(CChip* c, const unsigned* entries);
