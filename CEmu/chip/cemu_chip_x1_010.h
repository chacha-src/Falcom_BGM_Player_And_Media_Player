#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Seta X1-010。16ch の 8bit PCM または 128バイト波形+エンベロープ。
   チップ全体が 8KB RAM窓: $0000-$007F がチャンネル制御、残りは波形/エンベロープ表
   （チャンネルレジスタが128バイトページで索引）。 */
CChip* CEmuChipX1010Create(uint32_t clockHz, int sampleRate);
void CEmuChipX1010Destroy(CChip* c);

/* サウンドRAM窓アクセス（$0000-$1FFF）。 */
uint8_t CEmuChipX1010Read(CChip* c, unsigned offset);
void CEmuChipX1010Write(CChip* c, unsigned offset, uint8_t data);
