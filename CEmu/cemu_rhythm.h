#pragma once
#include "chip/cemu_chip.h"

/* 2608_BD.WAV 等が入ったディレクトリ（末尾 '\\'）。無ければ空 */
void GetRhythmPath(wchar_t* pszPath, int nSize);

/* 必要なら exe 配下から ym2608_adpcm_rom.bin を探し、
   常に ADPCM-A へ載せる（上書き。zip の adpcm は ADPCM-B） */
void CEmuLoadExternalYm2608Adpcm(CChip* chip);
