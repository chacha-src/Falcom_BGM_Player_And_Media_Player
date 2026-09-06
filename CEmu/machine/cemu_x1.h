#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

struct CEmuX1 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

int CEmuX1Open(CEmuX1* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
void CEmuX1Close(CEmuX1* m);
int CEmuX1Render(CEmuX1* m, int16_t* stereo, int frames);
int CEmuX1Seek(CEmuX1* m, uint64_t sample);
