#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Yamaha YMZ280B（PCMD8）。外部サンプルROMから 4bit ADPCM / 8bit / 16bit PCM
   を8chストリーム。ホストIFは2ポート: レジスタ番号のあと値を書く。 */
CChip* CEmuChipYmz280bCreate(uint32_t clockHz, int sampleRate);
void CEmuChipYmz280bDestroy(CChip* c);

/* ポート0=レジスタ選択、ポート1=データ。 */
void CEmuChipYmz280bWritePort(CChip* c, unsigned port, uint8_t data);
uint8_t CEmuChipYmz280bReadStatus(CChip* c);

/* 両出力を1アンプへ結ぶ基板（Battle Bakraid は MAME add_route(ALL_OUTPUTS,"mono")）
   は L+R を両chで聞く。片側ハードパンのまま残すプログラムがあるため重要。 */
void CEmuChipYmz280bSetMono(CChip* c, int mono);

/* 現在デコード中のボイス数。ステータスポート読みはIRQビットを落とすので、
   「何か鳴っているか」だけ知りたい基板はこちらを使う。 */
unsigned CEmuChipYmz280bPlayingCount(const CChip* c);
