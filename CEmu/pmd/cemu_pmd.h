#pragma once
#include "../cemu_types.h"
#ifndef _WINDOWS_
#include <windows.h>
#endif

/* In-process PMDWin (CEmu/pmd/pmdwin) for CEmu PMD zips. No DLL. */

struct CEmuZipFs;

struct CEmuPmdPlayer {
	void* impl;
	DWORD sampleRate;
	UINT64 curSample;
	UINT64 endSample;
	int open;
	wchar_t tempDir[520];
};

void CEmuPmdInit(CEmuPmdPlayer* p);
void CEmuPmdClose(CEmuPmdPlayer* p);
int CEmuPmdOpen(CEmuPmdPlayer* p, const CEmuGameEntry* ge, const wchar_t* zipPath,
	unsigned titleCode, DWORD sampleRate, CEmuZipFs* fs);
int CEmuPmdSeek(CEmuPmdPlayer* p, UINT64 sample, DWORD flags);
int CEmuPmdRender(CEmuPmdPlayer* p, short* outStereo, int sampleFrames);
UINT64 CEmuPmdLengthSamples(const CEmuPmdPlayer* p);
int CEmuPmdGeLooks(const CEmuGameEntry* ge);
