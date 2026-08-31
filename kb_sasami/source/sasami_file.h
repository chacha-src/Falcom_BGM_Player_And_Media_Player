#pragma once

#include <stdint.h>
#include <stddef.h>

enum SasamiKind {
	SASAMI_KIND_UNKNOWN = 0,
	SASAMI_KIND_FPY = 1,
	SASAMI_KIND_MPY = 2,
	SASAMI_KIND_MPW2 = 3,
	SASAMI_KIND_MPW3 = 4 /* .mpsmv — MPW2 body + variable MPW3 VST trailer */
};

enum SasamiMidiMap {
	SASAMI_MAP_GS55 = 0,
	SASAMI_MAP_GS88 = 1,
	SASAMI_MAP_XG = 2,
	SASAMI_MAP_GM = 3
};

/* Max file / build buffer (MIDI body + VST state trailer). */
enum { SASAMI_MAX_FILE = 64 * 1024 * 1024 };

enum { SASAMI_MISAO_MAX_CH = 16 };

struct SasamiTrack {
	uint32_t fileOff;
	int part;
	int unused;
};

struct SasamiSong {
	SasamiKind kind;
	int mpyVersion;
	int dualPort;
	int wideTracks;
	unsigned versionWord;
	int trackCount;
	SasamiTrack tracks[64];
	uint8_t* data;       /* malloc'd MIDI/FM body (not including external state copies) */
	uint32_t dataSize;
	uint32_t dataCap;
	char titleSjis[65];
	int fmOpna10ch;
	int misaoEnabled;
	int misaoChCount;
	SasamiTrack misaoTracks[SASAMI_MISAO_MAX_CH];
	/* MPW3 / .mpsmv VST trailer */
	int hasMpw3Trailer;
	wchar_t vstPath[32][260];
	int vstProg[32];
	int vstBankMsb[32];
	int vstBankLsb[32];
	int vstForceCh[32];
	uint8_t* vstComp[32];
	uint32_t vstCompLen[32];
	uint8_t* vstCtrl[32];
	uint32_t vstCtrlLen[32];
};

struct SasamiTags {
	char titleSjis[65];
	char composerSjis[65];
	char commentSjis[65];
};

void SasamiSongInit(SasamiSong* out);
void SasamiSongFree(SasamiSong* out);
/* Replace slot blob (copies bytes). len=0 clears. */
void SasamiVstBlobSet(uint8_t** slot, uint32_t* slotLen, const uint8_t* bytes, uint32_t len);

bool SasamiExtIsMidi(const wchar_t* path);
bool SasamiExtIsFm(const wchar_t* path);
bool SasamiExtIsAny(const wchar_t* path);
bool SasamiPeekTagsW(const wchar_t* path, SasamiTags* out);
SasamiKind SasamiKindFromPath(const wchar_t* path);

bool SasamiLoadMemory(const uint8_t* bytes, size_t size, SasamiKind hint, SasamiSong* out);
bool SasamiLoadFileW(const wchar_t* path, SasamiSong* out);

uint8_t SasamiGet(const SasamiSong& s, uint32_t off);
uint16_t SasamiGet16(const SasamiSong& s, uint32_t off);
uint32_t SasamiGet24(const SasamiSong& s, uint32_t off);

bool SasamiOffOk(const SasamiSong& s, uint32_t off, uint32_t need);
