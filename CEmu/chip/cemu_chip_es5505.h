#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Ensoniq ES5505（OTIS）— MAME es5506.cpp からの移植（BSD-3-Clause, Aaron Giles）。
   レジスタR/W、バンクPCM ROM、ステレオ Render。 */

CChip* CEmuChipEs5505Create(uint32_t clockHz, int sampleRate);
void CEmuChipEs5505Destroy(CChip* c);
uint16_t CEmuChipEs5505Read(CChip* c, uint32_t addr);
uint16_t CEmuChipEs5505PeekCr(CChip* c, int voice);
void CEmuChipEs5505SetSlowLpe(CChip* c, int on);

/* 任意: ホストがボイス毎のバンク基底を設定（ワード索引。Taitoは (n&mask)<<20）。 */
void CEmuChipEs5505SetVoiceBank(CChip* c, int voice, uint32_t wordBase);
uint32_t CEmuChipEs5505GetVoiceIndex(CChip* c);
