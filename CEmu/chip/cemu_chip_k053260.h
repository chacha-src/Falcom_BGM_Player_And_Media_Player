#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Konami K053260、4ch PCM + メイン/サウンド通信ポート。 */
CChip* CEmuChipK053260Create(uint32_t clockHz, int sampleRate);
void CEmuChipK053260Destroy(CChip* c);

/* サウンドCPU側レジスタ読み（ポート0/1 = メイン→サウンドラッチ）。 */
uint8_t CEmuChipK053260Read(CChip* c, unsigned offset);
/* メインCPU側通信書き込み（曲バイトをポート0/1へ積む）。 */
void CEmuChipK053260MainWrite(CChip* c, unsigned offset, uint8_t data);
uint8_t CEmuChipK053260MainRead(CChip* c, unsigned offset);
