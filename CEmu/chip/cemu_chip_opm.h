#pragma once
#include <stdint.h>
#include "cemu_chip.h"

CChip* CEmuChipYm2151Create(uint32_t clockHz, int sampleRate);
void CEmuChipYm2151Destroy(CChip* c);

/* 診断: create/reset 以降の SetReg 回数。 */
unsigned CEmuChipYm2151WriteCount(const CChip* c);
/* OPMレジスタ256バイトのスナップショット（addrラッチ側）。nullなら0。 */
int CEmuChipYm2151PeekRegs(const CChip* c, unsigned char* out256);
/* キーオン書き込み回数（reg 0x08 でスロットビットあり）。 */
unsigned CEmuChipYm2151KeyOnCount(const CChip* c);
/* X68k/X1互換の空API — TL/KeyOn は書き換えない。 */
void CEmuChipYm2151SetAudibleAssist(CChip* c, int enable);
/* X1: fmgen Mix は pan==0（ibuf[0]）を捨てる。KOEI/KSK の FB は RL=0 のまま
   モニタだけキーオンしピーク0になることが多いので、RL=0 を L+R に読み替える。 */
void CEmuChipYm2151SetRlZeroAsLr(CChip* c, int enable);
