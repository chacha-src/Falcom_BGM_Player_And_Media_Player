#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

struct CEmuAc {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

int CEmuAcOpen(CEmuAc* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
void CEmuAcClose(CEmuAc* m);
int CEmuAcRender(CEmuAc* m, int16_t* stereo, int frames);
int CEmuAcSeek(CEmuAc* m, uint64_t sample);
