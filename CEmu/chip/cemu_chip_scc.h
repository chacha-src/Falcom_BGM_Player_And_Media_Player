#pragma once
#include "cemu_chip.h"
#include <stdint.h>

/* Konami SCC / SCC互換: 5ch、32バイト波形（原チップではch4がch3波形を共有）。
   レジスタファイルは 0x9800/0xB800 窓から見える 0x00-0x8F。 */
CChip* CEmuChipSccCreate(uint32_t clockHz, int sampleRate);
void CEmuChipSccDestroy(CChip* c);

/* SCCレジスタ1本書き込み（0x00-0xBF）。FMモニタも更新する。 */
void CEmuChipSccWriteReg(CChip* c, unsigned reg, uint8_t data);
uint8_t CEmuChipSccReadReg(CChip* c, unsigned reg);
/* SCC-I plus モード: 波形 $B800、freq/vol/on は $B8A0-$B8AF。 */
void CEmuChipSccSetPlusMode(CChip* c, int plus);
