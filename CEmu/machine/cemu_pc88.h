#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

struct CEmuPc88 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

int CEmuPc88Open(CEmuPc88* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
void CEmuPc88Close(CEmuPc88* m);
int CEmuPc88Render(CEmuPc88* m, int16_t* stereo, int frames);
int CEmuPc88Seek(CEmuPc88* m, uint64_t sample);
