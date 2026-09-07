#pragma once
#include "chip/cemu_chip.h"

/* Directory (trailing '\\') containing 2608_BD.WAV etc., or empty. */
void GetRhythmPath(wchar_t* pszPath, int nSize);

/* If needed, search exe dir + subfolders for ym2608_adpcm_rom.bin and
   always load it into ADPCM-A (overwrites; zip adpcm is ADPCM-B). */
void CEmuLoadExternalYm2608Adpcm(CChip* chip);
