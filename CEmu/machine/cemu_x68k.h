#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

struct CEmuX68k {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

int CEmuX68kOpen(CEmuX68k* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
void CEmuX68kClose(CEmuX68k* m);
int CEmuX68kRender(CEmuX68k* m, int16_t* stereo, int frames);
int CEmuX68kSeek(CEmuX68k* m, uint64_t sample);
