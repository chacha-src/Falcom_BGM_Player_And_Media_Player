#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* OKI MSM6295、4ch ADPCM。 */
CChip* CEmuChipOki6295Create(uint32_t clockHz, int sampleRate);
void CEmuChipOki6295Destroy(CChip* c);

/* ページ付きサンプルROM（NMK112、および Battle Garegga / Batrider の代替GAL）。
   チップの 0x40000 空間は8窓 — フレーズ表4ページ(0x100)のあと 0x400-0xFFFF と
   64K×3 — 各窓がサンプルROMの64Kバンクを選ぶ。呼び出し元所有の8エントリを指す。
   NULL でフラットROMに戻す。 */
void CEmuChipOki6295SetBankTable(CChip* c, const unsigned* entries);
