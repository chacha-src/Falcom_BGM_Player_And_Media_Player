#pragma once
#include "sasami_file.h"
#include <stdint.h>

static const int SASAMI_PPQN = 48;
static const unsigned SASAMI_DEFAULT_T = 13000;
static const uint32_t SASAMI_MAX_TICKS = 200000;
enum { SASAMI_MAX_SMF = 2 * 1024 * 1024 };

// out must have room for SASAMI_MAX_SMF (or outCap). *outSize set on success.
bool SasamiConvertToSmf(const SasamiSong& song, SasamiMidiMap map, uint8_t* out, int outCap, int* outSize);

#ifdef __cplusplus
extern "C" {
#endif
int SasamiPathIsMidi(const wchar_t* path);
int SasamiPathIsFm(const wchar_t* path);
int SasamiConvertPathToMidiFile(const wchar_t* src, wchar_t* dest, int destChars);
#ifdef __cplusplus
}
#endif
