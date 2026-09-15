#pragma once
#include <stdint.h>
#ifndef _WINDOWS_
#include <windows.h>
#endif

/* CEmu x68k zip 経路用のポータブル MDX (MXDRV + X68Sound) プレーヤ */

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
/* mdx/pdx は生ファイルイメージ (pdx は NULL 可)。
   pdxFileName: ヘッダ無し MDD ラップに焼き込む任意名 (例 PCM.DAT) */
int CEmuMdxOpenBuffer(CEmuMdxPlayer* p,
	const BYTE* mdx, DWORD mdxSize,
	const BYTE* pdx, DWORD pdxSize,
	DWORD sampleRate, const wchar_t* srcPath,
	const char* pdxFileName);

/* MDD PCM.DAT (64× start/end @512) → ヒープ上 PDX (96 スロット)。呼び出し側が free */
BYTE* CEmuMdxConvertPcmDatToPdx(const BYTE* pcm, DWORD pcmSize, DWORD* outSize);
int CEmuMdxSeek(CEmuMdxPlayer* p, UINT64 sample, DWORD flags);
int CEmuMdxRender(CEmuMdxPlayer* p, short* outStereo, int sampleFrames);
UINT64 CEmuMdxLengthSamples(const CEmuMdxPlayer* p);
