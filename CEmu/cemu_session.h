#pragma once
#include "cemu_types.h"
#include "cemu_s98.h"
#include "cemu_mdx.h"
#include "machine/cemu_pc88.h"
#include "machine/cemu_pc98.h"
#include "machine/cemu_ac.h"
#include "machine/cemu_x68k.h"
#include "machine/cemu_sg1000.h"
#include "machine/cemu_x1.h"
#include "machine/cemu_pcat.h"
#include "machine/cemu_f3.h"
#include "machine/cemu_msx.h"
#include "machine/cemu_fm7.h"

enum {
	CEMU_KIND_NONE = 0,
	CEMU_KIND_S98 = 1,
	CEMU_KIND_PC88 = 2,
	CEMU_KIND_PC98 = 3,
	CEMU_KIND_MDX = 4,
	CEMU_KIND_AC = 5,
	CEMU_KIND_X68K = 6,
	CEMU_KIND_SG1000 = 7,
	CEMU_KIND_X1 = 8,
	CEMU_KIND_PCAT = 9,
	CEMU_KIND_F3 = 10,
	CEMU_KIND_MSX = 11,
	CEMU_KIND_FM7 = 12
};

struct CEmuSession {
	int kind;
	int sampleRate;
	int channels;
	UINT64 curSample;
	UINT64 lengthSamples;
	/* hard 系 (length 不明): 無音連続で終端。S98/MDX は使わない */
	uint32_t silenceFrames;   /* 再生開始からの累積フレーム */
	uint32_t silenceRun;      /* 連続無音フレーム */
	uint8_t silenceHeard;     /* 一度でも音が出た */
	uint8_t endedBySilence;   /* 無音判定で lengthSamples を確定した */
	wchar_t path[CEMU_ZIP_PATH];
	const CEmuGameEntry* game;
	CEmuS98Player s98;
	CEmuMdxPlayer mdx;
	CEmuPc88 pc88;
	CEmuPc98 pc98;
	CEmuAc ac;
	CEmuX68k x68k;
	CEmuSg1000 sg1000;
	CEmuX1 x1;
	CEmuPcat pcat;
	CEmuF3 f3;
	CEmuMsx msx;
	CEmuFm7 fm7;
};

void CEmuSessionInit(CEmuSession* s);
void CEmuSessionClose(CEmuSession* s);
int CEmuSessionOpen(CEmuSession* s, const wchar_t* path, unsigned titleCode, DWORD sampleRate);
int CEmuSessionRender(CEmuSession* s, short* stereo, int frames);
int CEmuSessionSeek(CEmuSession* s, UINT64 sample);
