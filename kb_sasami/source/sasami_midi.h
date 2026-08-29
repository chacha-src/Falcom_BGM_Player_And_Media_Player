#pragma once
#include "sasami_file.h"
#include <stdint.h>

static const int SASAMI_PPQN = 48;
static const unsigned SASAMI_DEFAULT_T = 13000;
static const uint32_t SASAMI_MAX_TICKS = 200000;
enum { SASAMI_MAX_SMF = 2 * 1024 * 1024 };

// gsBankLsb: GS/88 系の初期 CC32 (1=55, 2=88, 3=88Pro, 4=8820)。0=付けない。
// out must have room for SASAMI_MAX_SMF (or outCap). *outSize set on success.
bool SasamiConvertToSmf(const SasamiSong& song, SasamiMidiMap map, int gsBankLsb, uint8_t* out, int outCap, int* outSize);

void SasamiMapForceToSel(int mapForce, SasamiMidiMap* map, int* gsBankLsb);
int SasamiReadMidMapForceW(const wchar_t* fol, int* outForce);
int SasamiResolveMapForceW(const wchar_t* fol, int globalDefault);
int SasamiReadFmForceW(const wchar_t* fol, int* outForce); // 1 if b[5] valid 0..2
int SasamiResolveFmModeW(const wchar_t* fol, int globalDefault); // 0..2
void SasamiInvalidateTempMidi(const wchar_t* src);

#ifdef __cplusplus
extern "C" {
#endif
int SasamiPathIsMidi(const wchar_t* path);
int SasamiPathIsFm(const wchar_t* path);
int SasamiConvertPathToMidiFile(const wchar_t* src, wchar_t* dest, int destChars);
#ifdef __cplusplus
}
#endif
