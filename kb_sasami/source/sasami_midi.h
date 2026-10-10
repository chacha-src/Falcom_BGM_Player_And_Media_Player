#pragma once
#include "sasami_file.h"
#include <stdint.h>

static const int SASAMI_PPQN = 48;
static const unsigned SASAMI_DEFAULT_T = 13000;
static const uint32_t SASAMI_MAX_TICKS = 200000;
enum { SASAMI_MAX_SMF = 2 * 1024 * 1024 };

// gsBankLsb: GS/88 系の初期 CC32 (1=55, 2=88, 3=88Pro, 4=8820)。0=付けない。
// out must have room for SASAMI_MAX_SMF (or outCap). *outSize set on success.
// exportLoops: 0=再生（1周＋loopStart/CC111）。1以上=標準MIDI書き出し。
//   最長トラックに戻る J が無いときはマーカーも切り詰めもせず1回。
//   最長に J があるときはその周を exportLoops 回展開し、ループマーカーは付けない。
// outLongestJump: 非NULLなら、最長トラックが戻る J を持つか 0/1 を書く。
bool SasamiConvertToSmf(const SasamiSong& song, SasamiMidiMap map, int gsBankLsb, uint8_t* out, int outCap, int* outSize, int laBankMsb = 0, int exportLoops = 0, int* outLongestJump = 0);

void SasamiMapForceToSel(int mapForce, SasamiMidiMap* map, int* gsBankLsb, int* laBankMsb = 0);
int SasamiReadMidMapForceW(const wchar_t* fol, int* outForce);
int SasamiResolveMapForceW(const wchar_t* fol, int globalDefault);
/* mapForce 0=自動。SysEx / CC32 / 曲名・パス / cmd40 の4モードから。
   自動の既定は .mpy=88、.mpw2/.mpsmv=88Pro。CRender の GS VST が空なら XG。不明は 0。 */
int SasamiDetectMapForceFromMem(const uint8_t* data, int n, const wchar_t* path, const char* titleSjis);
int SasamiDetectMapForceFromSong(const SasamiSong& song, const wchar_t* path);
int SasamiAutoMapForce(int resolved, const SasamiSong* song, const uint8_t* smf, int smfN, const wchar_t* path, const char* titleSjis);
int SasamiReadFmForceW(const wchar_t* fol, int* outForce); // 1 if b[5] valid 0..2
int SasamiResolveFmModeW(const wchar_t* fol, int globalDefault); // 0..2
void SasamiInvalidateTempMidi(const wchar_t* src);

#ifdef __cplusplus
extern "C" {
#endif
int SasamiPathIsMidi(const wchar_t* path);
int SasamiPathIsFm(const wchar_t* path);
/* exportLoops 0=再生用テンポラリ。1以上=書き出し（J の有無は変換側）。
   負=ディスクに書かず、最長に J があれば 2、無ければ 1、失敗は 0。dest は不要。 */
int SasamiConvertPathToMidiFile(const wchar_t* src, wchar_t* dest, int destChars, int exportLoops = 0);
#ifdef __cplusplus
}
#endif
