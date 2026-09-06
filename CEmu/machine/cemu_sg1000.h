#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

struct CEmuSg1000 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

int CEmuSg1000Open(CEmuSg1000* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
void CEmuSg1000Close(CEmuSg1000* m);
int CEmuSg1000Render(CEmuSg1000* m, int16_t* stereo, int frames);
int CEmuSg1000Seek(CEmuSg1000* m, uint64_t sample);
