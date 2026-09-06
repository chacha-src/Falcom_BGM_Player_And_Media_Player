#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

struct CEmuF3 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

int CEmuF3Open(CEmuF3* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
void CEmuF3Close(CEmuF3* m);
int CEmuF3Render(CEmuF3* m, int16_t* stereo, int frames);
int CEmuF3Seek(CEmuF3* m, uint64_t sample);
