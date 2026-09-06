#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

struct CEmuPcat {
	CHard* hard;
	CDriver* driver;
	int ready;
	int sampleRate;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

int CEmuPcatOpen(CEmuPcat* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
void CEmuPcatClose(CEmuPcat* m);
int CEmuPcatRender(CEmuPcat* m, int16_t* stereo, int frames);
int CEmuPcatSeek(CEmuPcat* m, uint64_t sample);
