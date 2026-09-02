#pragma once
/* SASAMI binary writers: MPY / MPW2 / MPW3(.mpsmv) / FPY from packed track streams. */
#include "sasami_file.h"
#include <stdint.h>

enum { SASAMI_WRITE_MAX = SASAMI_MAX_FILE };
enum { SASAMI_WRITE_TR = 64 };
enum { SASAMI_WRITE_STREAM = 256 * 1024 };

struct SasamiTrackStream {
	uint8_t bytes[SASAMI_WRITE_STREAM];
	uint32_t size;
	int part; /* MIDI 0..15, FM unused = -1 */
	int used;
};

struct SasamiWriteMidi {
	SasamiTrackStream tr[SASAMI_WRITE_TR];
	int trackCount; /* 32 or 64 */
	int dualPort;
	int wideTracks;
	char titleSjis[65];
	char composerSjis[65];
	char commentSjis[65];
	/* MPW3 / .mpsmv VST bind */
	int isMpw3;
	int mpw3Ver; /* 1=classic trailer, 2=+macro footer */
	wchar_t vstPath[32][260];
	int vstProg[32];
	int vstBankMsb[32];
	int vstBankLsb[32];
	int vstForceCh[32]; /* -1 = none, 0..15 */
	uint8_t* vstComp[32];
	uint32_t vstCompLen[32];
	uint8_t* vstCtrl[32];
	uint32_t vstCtrlLen[32];
	/* Wave3 macros (ver≥2). UTF-16LE packed; optional. */
	wchar_t macroName[32][32];
	wchar_t macroBody[32][2048];
	int macroCount;
};

struct SasamiWriteFm {
	SasamiTrackStream tr[10];
	SasamiTrackStream misao[SASAMI_MISAO_MAX_CH];
	int misaoChCount; /* 0 = no Misao region */
	int opna10;
	int fpy2; /* 1 = nest loops (versionWord@0x1C=2), prefer .fpy2 */
	char titleSjis[65];
	char composerSjis[65];
	char commentSjis[65];
	uint8_t voices[64][25];
	uint16_t voiceAddr[64];
	int voiceCount;
	/* Wave3: FPY2 footer macros */
	wchar_t macroName[32][32];
	wchar_t macroBody[32][2048];
	int macroCount;
};

void SasamiWriteMidiClear(SasamiWriteMidi* w);
void SasamiWriteFmClear(SasamiWriteFm* w);

int SasamiStreamPut3(SasamiTrackStream* s, uint8_t a, uint8_t b, uint8_t c);
int SasamiStreamPut(SasamiTrackStream* s, const uint8_t* p, uint32_t n);

uint32_t SasamiBuildMpy(const SasamiWriteMidi* w, uint8_t* out, uint32_t outCap);
uint32_t SasamiBuildMpw2(const SasamiWriteMidi* w, uint8_t* out, uint32_t outCap);
/* .mpsmv: MPW2 + "MPW3" + ver + 32 slots with optional state blobs */
uint32_t SasamiBuildMpw3(const SasamiWriteMidi* w, uint8_t* out, uint32_t outCap);
uint32_t SasamiBuildFpy(const SasamiWriteFm* w, uint8_t* out, uint32_t outCap);

int SasamiWriteFileW(const wchar_t* path, const uint8_t* data, uint32_t size);
