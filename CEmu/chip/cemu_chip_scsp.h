#pragma once
#include "cemu_chip.h"
#include <stdint.h>

/* Model 2A/3 SCSP（YMF292-F）、32スロット。実合成は CEmu/vendor/scsp。
   再生にはサウンド68000の実行が必要: チップはキーオン済みを鳴らすが、
   ホストがCPUを回すまで誰もキーオンしない。 */
CChip* CEmuChipScspCreate(uint32_t clockHz, int sampleRate);
void CEmuChipScspDestroy(CChip* c);

/* 512KB サウンドRAMは共有: サウンド68000の下位メモリであり、SCSPが波形を
   取る唯一の空間でもある。ホストバスはここにサンプルを直書きする。
   ワードはホスト順（ベンダーコアはバイトスワップRAM規約）。8bitアクセスは
   addr^1。SCSP未生成なら NULL。 */
uint8_t* CEmuChipScspRam();
unsigned CEmuChipScspRamSize();

/* Model 2/3 の 0x100000 レジスタ窓 — ワードインデックス（バイトオフセットではない）。 */
unsigned CEmuChipScspReadReg(CChip* c, unsigned wordIndex);

/* 曲選択は実機同様 SCSP MIDI 入力へ入る。 */
void CEmuChipScspMidiIn(CChip* c, uint8_t data);
int CEmuChipScspMidiPending();

/* サウンド68000向け保留IPL（タイマA/B/CとMIDI）。アイドル時は0。 */
int CEmuChipScspIrqLevel();
