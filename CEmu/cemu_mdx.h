#pragma once
#include <stdint.h>
#ifndef _WINDOWS_
#include <windows.h>
#endif

/* Portable MDX (MXDRV + X68Sound) player for CEmu x68k zip path. */

struct CEmuMdxPlayer {
	void* impl;
	DWORD sampleRate;
	UINT64 curSample;
	UINT64 endSample;
	int open;
	wchar_t sourcePath[520];
};

void CEmuMdxInit(CEmuMdxPlayer* p);
void CEmuMdxClose(CEmuMdxPlayer* p);
/* mdx/pdx are raw file images (pdx may be NULL).
   pdxFileName: optional name baked into headerless MDD wraps (e.g. PCM.DAT). */
int CEmuMdxOpenBuffer(CEmuMdxPlayer* p,
	const BYTE* mdx, DWORD mdxSize,
	const BYTE* pdx, DWORD pdxSize,
	DWORD sampleRate, const wchar_t* srcPath,
	const char* pdxFileName);

/* MDD PCM.DAT (64× start/end @512) → heap PDX (96-slot). Caller frees. */
BYTE* CEmuMdxConvertPcmDatToPdx(const BYTE* pcm, DWORD pcmSize, DWORD* outSize);
int CEmuMdxSeek(CEmuMdxPlayer* p, UINT64 sample, DWORD flags);
int CEmuMdxRender(CEmuMdxPlayer* p, short* outStereo, int sampleFrames);
UINT64 CEmuMdxLengthSamples(const CEmuMdxPlayer* p);
