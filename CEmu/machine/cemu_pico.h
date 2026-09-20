#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

/* Sega Pico セッション: ハード＋ドライバ＋ zip パス */
struct CEmuPico {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

int CEmuPicoOpen(CEmuPico* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
void CEmuPicoClose(CEmuPico* m);
int CEmuPicoRender(CEmuPico* m, int16_t* stereo, int frames);
int CEmuPicoSeek(CEmuPico* m, uint64_t sample);
