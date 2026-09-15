#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Irem M72 8bit サンプルDAC（m72_audio_device）。サウンドCPUがNMI毎に
   符号なし1バイトを送る。Write(0, level) が現在のDACレベル。 */
CChip* CEmuChipIremDacCreate(int sampleRate);
void CEmuChipIremDacDestroy(CChip* c);
