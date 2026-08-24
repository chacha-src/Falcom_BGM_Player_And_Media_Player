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
	// 1 = opened through the same path a slot drop uses. The host palette only
	// lists plugs that passed; 0 means not checked yet or a failed probe.
	int isLiveOk;
	// Result of playing a probe note and measuring the output, because loading
	// successfully proves nothing: an unauthorised or half-installed instrument
	// opens, accepts MIDI and returns digital silence.
	//   0 = stayed silent although it should be ready to play (bad install)
	//   1 = made a sound
	//   2 = silent, but it is a sampler/kit player with nothing loaded yet, so
	//       silence is the expected answer until the user picks a patch
	int isAudible;
	int probePeakMilli; // measured peak x1000, kept for the log / tooltip
};

/*
 * VST2 instruments: same-arch plugs load in-process.
 * Cross-arch (x86 app + x64 SC-VA) uses KpiHost64 IPC (VstOpen/Render/…).
 */
#ifdef __cplusplus
extern "C" {
#endif

int VstScanEnsure(HWND parentForWait);
// After Ensure: try each instrument the way a part-slot drop does, and keep
// only the ones that open. Call from the VST host UI on first open / rescan.
void VstScanVerifyLiveList(HWND parentForWait);
int VstScanGetCount(void);
const VstPluginInfo* VstScanGet(int i);
void VstScanInvalidate(void);
int VstDetectMultiTimbral(const wchar_t* nameOrPath); // 0/1
int VstScanGetMultiCount(void); // instruments with isMultiTimbral (host arch)
const VstPluginInfo* VstScanGetMulti(int multiIndex); // 0..GetMultiCount-1
int VstPluginPeArch(const wchar_t* path); // 32/64/0
// Explicit GS (vstMultiDll) / XG (vstExtraPath). Empty both => 0 (MIDI Mapper).
// XG System On in the SMF picks XG; otherwise GS. One slot empty uses the other.
// Returns PE arch (32/64) or 0 if none. outPath filled when return!=0.
int VstPickPreferredPlugin(wchar_t* outPath, int outChars);
// 1=open via KpiHost64. midPath is peeked for XG reset. Empty both DLLs never remotes.
int VstShouldOpenRemote64(const wchar_t* midPath, wchar_t* outDll, int outChars);
int VstHasX64Instruments(void);

int VstIsMidiExt(const wchar_t* path);
int VstIsProjectExt(const wchar_t* path);
int VstResolvePlayPath(const wchar_t* inPath, wchar_t* outMid, int outMidChars,
	wchar_t hints[][128], int maxHints, int* outHintCount);

int VstMidiOpen(const wchar_t* midPath, const wchar_t hints[][128],
	int hintCount, HWND parentForWait);
void VstMidiClose(void);
int VstMidiRead(BYTE* dst, int bytesWanted);
// 1 = VST/ensemble が PCM を出す。マッパーのみは 0（PCM は無音でも曲は続く）。
int VstMidiHasPluginAudio(void);
void VstMidiLog(const wchar_t* msg);
int VstMidiSeekSamples(__int64 samplePos);
int VstMidiGetRate(void);
int VstMidiGetChannels(void);
int VstMidiGetBits(void);
__int64 VstMidiGetLengthSamples(void);
// 曲長に含まれる残響用の余白（秒）。最終イベント以降は音楽としては終わっている。
#define VST_TAIL_PAD_SEC 2
double VstMidiTailPadSec(void);
// Plugin delay (VST2 initialDelay / VST3 getLatencySamples). 0 = mapper / unknown.
int VstMidiGetLatencySamples(void);
// x86 app: KpiHost64 Open の戻りを覚えさせる（ローカルにプラグが無いとき用）。
void VstMidiSetReportedLatencySamples(int samples);

// ライブ交差用に曲エンジンを2本。スロットはスレッドローカル（A再生中にBを開ける）。
enum { VST_SONG_SLOTS = 2 };
void VstMidiSetIoSlot(int slot);
int VstMidiGetIoSlot(void);
void VstMidiCloseSlot(int slot);

// MIDI メタ/ファイル名から GS マップ種別。
// 0=なし 1=55 2=88 3=88Pro 4=8820/50 5=GM 6=SD-90 7=XG(タイトル) 8=LA(MT-32) 9=GM2 10..=ETC。
int VstMidiGuessGsMapKind(const wchar_t* title, const wchar_t* path);
// 複数メタを畳む。8820>88Pro>88>LA>55 が GM/SD/XG タイトルより強い。
int VstMidiFoldGsMapHint(int cur, int kind);
int VstMidiSysexIsGmOn(const unsigned char* d, int n);
int VstMidiSysexIsGsReset(const unsigned char* d, int n);
int VstMidiSysexIsXgOn(const unsigned char* d, int n);
// 1 = GS Port B (50 xx) / 32-byte Voice Reserve / XG Multi Part 17-32.
// 16-byte Voice Reserve at 00 01 10 is ordinary 16-part GS, not 32ch.
int VstMidiSysexMarksGs32(const unsigned char* d, int n);
int VstMidiBankMsbIsSdNative(int msb);
// 使われている (bankMSB<<8|PC) が SASAMI_GS.DAT のどのマップに収まるか。
// 8820 から 88Pro→88→55 へ落とす。0=判定不能。
int VstMidiGsMapDropFromUsed(const unsigned short* pairs, int nPairs);

// Live MIDI を曲エンジンへ差し込む（モニタの CC / 鍵盤）。キューして次のブロックで送る。
void VstMidiInjectShort(int portIndex0to2, DWORD shortMsg, int sampleOfs);
void VstMidiInjectSysex(int portIndex0to2, const unsigned char* data, int bytes);
// x86→KpiHost64: 曲レンダーに乗せるためキューを奪う。ローカル再生では呼ばない。
int VstMidiStealInjects(BYTE* ports, DWORD* msgs, int* sampleOfs, int maxCount);

int VstLiveLoadPart(int part1to32, const wchar_t* pluginPath, int isVst3);
void VstLiveUnloadPart(int part1to32);
void VstLiveAllNotesOff();
void VstLiveMidiShort(int portIndex0to2, DWORD shortMsg);
void VstLiveMidiSysex(int portIndex0to2, const unsigned char* data, int bytes);
void VstLiveThruSet(int enable);
int VstLiveThruIsOn(void);
void VstLiveThruBind(const wchar_t* midPath);
void VstLiveThruPoll(__int64 playSample);
void VstLiveThruPcmPush(const BYTE* pcm, int bytes, int rate, int ch, int bits);
void VstLiveThruPcmMix(float* L, float* R, int frames);
int VstLiveRender(float* L, float* R, int frames);
int VstLiveEditorOpen(int part1to32);
void VstLiveEditorClose(int part1to32);

// Which MIDI channel the part hands to its plug-in. -1 keeps the channel the
// note arrived on; 0..15 forces one, so a drum kit that only listens on its
// own channel still plays whichever slot it sits in.
int VstLiveSendChannel(int part1to32);
void VstLiveSetSendChannel(int part1to32, int sendCh);
// Programs (HALion/SampleTank/Groove Agent kits, VST2 patches).
int VstLiveProgramCount(int part1to32);
int VstLiveProgramCurrent(int part1to32);
int VstLiveProgramName(int part1to32, int index, wchar_t* out, int outChars);
// Batched form for the slot menu: one call, so a remote part costs a single
// pipe round trip instead of one per name. Returns the names written.
int VstLiveProgramNames(int part1to32, int first, int count, wchar_t* out,
	int stride);
int VstLiveSetProgram(int part1to32, int index);

// What the part is currently hearing, so the UI can light up the channels.
struct VstLiveActInfo {
	int held;            // notes currently down
	int note;            // last note number
	int vel;             // last velocity
	int ageMs;           // since the last message on this channel, -1 when never
	unsigned mask[4];    // bit n of mask[n/32] = note n is down
};
int VstLiveActivity(int part1to32, struct VstLiveActInfo* out);
// Last system exclusive seen, already summarised for display.
int VstLiveSysexInfo(wchar_t* out, int chars, int* ageMs);

// Hardware / PC-keyboard MIDI that the VST host actually sent (not SMF thru).
// The MIDI monitor drains these on its UI timer.
void VstLiveTapPushShort(int portIndex0to2, DWORD shortMsg);
void VstLiveTapPushSysex(int portIndex0to2, const unsigned char* data, int bytes);
int VstLiveTapStealShorts(BYTE* ports, DWORD* msgs, int maxCount);
int VstLiveTapStealSysex(int* portIndex0to2, unsigned char* data, int maxBytes);

#ifdef __cplusplus
}
#endif
