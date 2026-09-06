#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

struct CEmuMsx {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

int CEmuMsxOpen(CEmuMsx* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
void CEmuMsxClose(CEmuMsx* m);
int CEmuMsxRender(CEmuMsx* m, int16_t* stereo, int frames);
int CEmuMsxSeek(CEmuMsx* m, uint64_t sample);
