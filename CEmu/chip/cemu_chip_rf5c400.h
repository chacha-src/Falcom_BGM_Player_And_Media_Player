#pragma once
#include "cemu_chip.h"
#include <stdint.h>

/* Ricoh RF5C400（Konami Hornet/Firebeat）: 32ch PCM、チャンネル毎 AR/DR/RR。
   MAME sound/rf5c400.cpp を参考。Write() はバイト番地ではなくレジスタオフセット。
   0x400未満がグローバル、それ以上はチャンネル=(offset>>5)&0x1f。 */
CChip* CEmuChipRf5c400Create(uint32_t clockHz, int sampleRate);
void CEmuChipRf5c400Destroy(CChip* c);

/* グローバルレジスタ読み出し（ステータス、ストリーム位置、外部メモリ）。 */
unsigned CEmuChipRf5c400ReadReg(CChip* c, unsigned offset);
