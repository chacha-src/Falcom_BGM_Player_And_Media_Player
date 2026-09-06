#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

struct CEmuPc98 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

int CEmuPc98Open(CEmuPc98* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
void CEmuPc98Close(CEmuPc98* m);
int CEmuPc98Render(CEmuPc98* m, int16_t* stereo, int frames);
int CEmuPc98Seek(CEmuPc98* m, uint64_t sample);
