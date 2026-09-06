#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

struct CEmuFm7 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

int CEmuFm7Open(CEmuFm7* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
void CEmuFm7Close(CEmuFm7* m);
int CEmuFm7Render(CEmuFm7* m, int16_t* stereo, int frames);
int CEmuFm7Seek(CEmuFm7* m, uint64_t sample);
