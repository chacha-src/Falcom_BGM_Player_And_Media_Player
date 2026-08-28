#pragma once

#include <stdint.h>
#include <stddef.h>

enum SasamiKind {
	SASAMI_KIND_UNKNOWN = 0,
	SASAMI_KIND_FPY = 1,
	SASAMI_KIND_MPY = 2,
	SASAMI_KIND_MPW2 = 3
};

enum SasamiMidiMap {
	SASAMI_MAP_GS55 = 0,
	SASAMI_MAP_GS88 = 1,
	SASAMI_MAP_XG = 2,
	SASAMI_MAP_GM = 3
};

enum { SASAMI_MAX_FILE = 2 * 1024 * 1024 };

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
	uint8_t data[SASAMI_MAX_FILE];
	uint32_t dataSize;
	char titleSjis[65];
	int fmOpna10ch;
};

struct SasamiTags {
	char titleSjis[65];
	char composerSjis[65];
	char commentSjis[65];
};

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
