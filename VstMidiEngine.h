#pragma once

#include <Windows.h>
#include "PluginKinds.h"

enum {
	VST_MAX_PLUGINS = 512,
	VST_PATH_CHARS = 520,
	VST_NAME_CHARS = 128
};

struct VstPluginInfo {
	wchar_t path[VST_PATH_CHARS];
	wchar_t name[VST_NAME_CHARS];
	int arch;          // 32 or 64
	int isVst3;        // 0=VST2 DLL, 1=VST3 bundle
	int isInstrument;
	int isMultiTimbral; // 1=SC-VA / SGP2 / GS・XG 系マルチ等（1インスタンスで16ch）
};

/*
 * VST2 instruments: same-arch plugs load in-process.
 * Cross-arch (x86 app + x64 SC-VA) uses KpiHost64 IPC (VstOpen/Render/…).
 */
#ifdef __cplusplus
extern "C" {
#endif

int VstScanEnsure(HWND parentForWait);
int VstScanGetCount(void);
const VstPluginInfo* VstScanGet(int i);
void VstScanInvalidate(void);
int VstDetectMultiTimbral(const wchar_t* nameOrPath); // 0/1
int VstScanGetMultiCount(void); // instruments with isMultiTimbral (host arch)
const VstPluginInfo* VstScanGetMulti(int multiIndex); // 0..GetMultiCount-1
int VstPluginPeArch(const wchar_t* path); // 32/64/0
// Explicit savedata.vstMultiDll only. Empty DLL => 0 (local MIDI out / Mapper).
// Returns PE arch (32/64) or 0 if none. outPath filled when return!=0.
int VstPickPreferredPlugin(wchar_t* outPath, int outChars);
// 1=open via KpiHost64. Only when explicit DLL is PE32+. Empty DLL never remotes.
int VstShouldOpenRemote64(wchar_t* outDll, int outChars);
int VstHasX64Instruments(void);

int VstIsMidiExt(const wchar_t* path);
int VstIsProjectExt(const wchar_t* path);
int VstResolvePlayPath(const wchar_t* inPath, wchar_t* outMid, int outMidChars,
	wchar_t hints[][128], int maxHints, int* outHintCount);

int VstMidiOpen(const wchar_t* midPath, const wchar_t hints[][128],
	int hintCount, HWND parentForWait);
void VstMidiClose(void);
int VstMidiRead(BYTE* dst, int bytesWanted);
// 1 if a real plug or ensemble is active (not builtin-only).
int VstMidiHasPluginAudio(void);
void VstMidiLog(const wchar_t* msg);
int VstMidiSeekSamples(__int64 samplePos);
int VstMidiGetRate(void);
int VstMidiGetChannels(void);
int VstMidiGetBits(void);
__int64 VstMidiGetLengthSamples(void);

int VstLiveLoadPart(int part1to32, const wchar_t* pluginPath, int isVst3);
void VstLiveUnloadPart(int part1to32);
void VstLiveMidiShort(int portIndex0to2, DWORD shortMsg);
int VstLiveRender(float* L, float* R, int frames);

#ifdef __cplusplus
}
#endif
