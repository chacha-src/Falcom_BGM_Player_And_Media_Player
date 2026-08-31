#pragma once

#include <Windows.h>
#include "PluginKinds.h"

enum {
	VST_MAX_PLUGINS = 512,
	VST_PATH_CHARS = 520,
	VST_NAME_CHARS = 128
};

struct VstPluginInfo {
	wchar_t path[VST_PATH_CHARS];  // DLL または .vst3 バンドルのフルパス
	wchar_t name[VST_NAME_CHARS];  // 表示名（ファイル名や effGetEffectName）
	int arch;          // 32 or 64。読めなければ 0
	int isVst3;        // 0=VST2 DLL、1=VST3 バンドル
	int isInstrument;  // 1=音源（エフェクトではない）
	int isMultiTimbral; // 1=SC-VA / SGP2 / GS・XG 系マルチ等（1インスタンスで16ch）
	// 1 = パートスロットへドロップしたときと同じ経路で開けた。
	// ホストのパレットはこれだけ載せる。0=未検査または失敗。
	int isLiveOk;
	// プローブ音を鳴らして出力を測った結果。Load 成功だけでは足りない
	// （未認証・インストール途中の音源は MIDI を受けても無音のまま）。
	//   0 = 鳴るはずなのに無音（インストール不良）
	//   1 = 音が出た
	//   2 = 無音だがサンプラー／キットでパッチ未選択。ユーザーが選べば鳴る
	int isAudible;
	int probePeakMilli; // 測定ピーク ×1000。ログ／ツールチップ用
};

/*
 * VST2 音源: 同じアーキテクチャならプロセス内で Load。
 * 異アーキ（x86 本体 + x64 SC-VA）は KpiHost64 の IPC（VstOpen/Render/…）。
 */
#ifdef __cplusplus
extern "C" {
#endif

int VstScanEnsure(HWND parentForWait);
// Ensure のあと、パートスロットへ落とすのと同じ開き方で検査し、開けたものだけ残す。
// VST ホスト UI の初回オープン／再スキャンから呼ぶ。
void VstScanVerifyLiveList(HWND parentForWait);
void VstWaitShowLoad(HWND owner, const wchar_t* pluginName);
void VstWaitHide(void);
int VstScanGetCount(void);
const VstPluginInfo* VstScanGet(int i);
void VstScanInvalidate(void);
int VstDetectMultiTimbral(const wchar_t* nameOrPath); // 0/1
int VstScanGetMultiCount(void); // isMultiTimbral の件数（ホストと同じアーキ）
const VstPluginInfo* VstScanGetMulti(int multiIndex); // 0..GetMultiCount-1
int VstPluginPeArch(const wchar_t* path); // 32/64/0
// 明示 GS（vstMultiDll）/ XG（vstExtraPath）。両方空なら 0（MIDI マッパー）。
// SMF 内の XG System On なら XG、それ以外は GS。片方空ならもう片方。
// 戻りは PE アーキ（32/64）。非 0 のとき outPath を埋める。
int VstPickPreferredPlugin(wchar_t* outPath, int outChars);
// 1=KpiHost64 経由で開く。midPath は XG リセット探索用。両方 DLL 空ならリモートしない。
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
// 1 = まだ曲の MIDI イベントが残っている（初期化 SysEx/CC のあいだ無音停止しない）。
int VstMidiEventsPending(void);
// 直近の再生ブロックで SysEx か CC を送った。呼ぶとクリア。
int VstMidiTakeKeepAlive(void);
// 1 = VST/ensemble が PCM を出す。マッパーのみは 0（PCM は無音でも曲は続く）。
int VstMidiHasPluginAudio(void);
void VstMidiLog(const wchar_t* msg);
int VstMidiSeekSamples(__int64 samplePos);
int VstMidiGetRate(void);
int VstMidiGetChannels(void);
int VstMidiGetBits(void);
__int64 VstMidiGetLengthSamples(void);
/* Song engine sample → tick (for score playhead). 1 if events loaded. */
int VstMidiTickAtSample(__int64 sample, unsigned* outTick);
__int64 VstMidiGetPlaySample(void);
// 曲長に含まれる残響用の余白（秒）。最終イベント以降は音楽としては終わっている。
#define VST_TAIL_PAD_SEC 2
double VstMidiTailPadSec(void);
// プラグイン遅延（VST2 initialDelay / VST3 getLatencySamples）。0=マッパー／不明。
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
// 1 = GS Port B（50 xx）/ 32 バイト Voice Reserve / XG Multi Part 17-32。
// 00 01 10 の 16 バイト Voice Reserve は普通の 16 パート GS なので 32ch ではない。
int VstMidiSysexMarksGs32(const unsigned char* d, int n);
int VstMidiBankMsbIsSdNative(int msb);
// 使われている (bankMSB<<8|PC) が SASAMI_GS.DAT のどのマップに収まるか。
// 8820 から 88Pro→88→55 へ落とす。0=判定不能。
int VstMidiGsMapDropFromUsed(const unsigned short* pairs, int nPairs);
// プレイリスト印用。ファイルを走査して 16/32ch と GM/GS/XG・マップ種別を返す。
struct VstMidiListPeek {
	int ch32;    // 0=16ch 1=32ch
	int mapKind; // 0=なし 1=55 2=88 3=88Pro 4=8820 5=GM 6=SD 7=XG 8=LA 9=GM2 10..=ETC
	int sysMode; // 0=GM 1=GS 2=XG
};
int VstMidiPeekListMarks(const wchar_t* path, struct VstMidiListPeek* out);

// Live MIDI を曲エンジンへ差し込む（モニタの CC / 鍵盤）。キューして次のブロックで送る。
void VstMidiInjectShort(int portIndex0to2, DWORD shortMsg, int sampleOfs);
void VstMidiInjectSysex(int portIndex0to2, const unsigned char* data, int bytes);
// x86→KpiHost64: 曲レンダーに乗せるためキューを奪う。ローカル再生では呼ばない。
int VstMidiStealInjects(BYTE* ports, DWORD* msgs, int* sampleOfs, int maxCount);

int VstLiveLoadPart(int part1to32, const wchar_t* pluginPath, int isVst3);
int VstLiveLoadFx(int part1to32, int slot0to1, const wchar_t* pluginPath, int isVst3);
void VstLiveUnloadFx(int part1to32, int slot0to1);
int VstLiveFxIsLoaded(int part1to32, int slot0to1);
int VstLiveFxGetPath(int part1to32, int slot0to1, wchar_t* outPath, int outCch);
int VstLiveFxParamCount(int part1to32, int slot0to1);
float VstLiveFxGetParam(int part1to32, int slot0to1, int paramIndex);
int VstLiveFxSetParam(int part1to32, int slot0to1, int paramIndex, float value01);
int VstLiveFxParamName(int part1to32, int slot0to1, int paramIndex, wchar_t* out, int outChars);
int VstLiveFxParamDisplay(int part1to32, int slot0to1, int paramIndex, wchar_t* out, int outChars);
int VstLiveFxEditorOpen(int part1to32, int slot0to1);
void VstLiveFxSetBypass(int part1to32, int slot0to1, int bypass);
int VstLiveFxGetBypass(int part1to32, int slot0to1);
int VstLiveFxCaptureState(int part1to32, int slot0to1, unsigned char** outBytes, int* outLen);
int VstLiveFxApplyState(int part1to32, int slot0to1, const unsigned char* bytes, int len);
/* MPW3 トレイラの VST パスをパートへロード。戻り=ロードした数（0=トレイラ無し）
   openEditor: 1=初回ロード時にエディタ（HALion Home）を開く。再生時は 0。 */
int VstApplyMpw3Binds(const wchar_t* mpw3Path, int openEditor);
/* .mpsmv preview: route SMF notes to live VST parts (HALion) instead of GM mapper. */
void VstSongUseLiveBindsSet(int enable);
int VstSongUseLiveBinds(void);
void VstLiveUnloadPart(int part1to32);
// ホスト窓が閉じるとき: 先にリモート音声を止め、全部のパートを降ろす。
void VstLiveShutdown(void);
// KpiHost64 専用: 固まったプラグインがパイプを掴んだままにしないよう effClose/FreeLibrary を飛ばす。
void VstLiveAbandonHostPlugins(int on);
void VstLiveAllNotesOff();
void VstLiveMidiShort(int portIndex0to2, DWORD shortMsg);
void VstLiveMidiToPart(int part1to32, DWORD shortMsg);
void VstLiveMidiSongShort(int portIndex0to2, DWORD shortMsg);
void VstLiveMidiSysex(int portIndex0to2, const unsigned char* data, int bytes);
void VstLiveThruSet(int enable);
int VstLiveThruIsOn(void);
void VstLiveThruBind(const wchar_t* midPath);
void VstLiveThruPoll(__int64 playSample);
void VstLiveThruPcmPush(const BYTE* pcm, int bytes, int rate, int ch, int bits);
void VstLiveThruPcmMix(float* L, float* R, int frames);
int VstLiveRender(float* L, float* R, int frames);
int VstLiveEditorOpen(int part1to32);
/* Queue editor open without blocking the caller (local + remote). */
int VstLiveEditorOpenAsync(int part1to32);
/* Drop pending async opens (call when closing score / exiting). */
void VstLiveEditorOpenCancelPending(void);
void VstLiveEditorClose(int part1to32);
/* ogg 終了時: KpiHost64 上の VST 設定画面をすべて閉じる（プラグインは載せたまま）。 */
void VstLiveEditorCloseAllRemote(void);
/* Score / note-props HWND receives WM_VST_LIVE_EDITOR_CLOSED (w=part, l=prog). */
#ifndef WM_VST_LIVE_EDITOR_CLOSED
#define WM_VST_LIVE_EDITOR_CLOSED (WM_APP + 9120)
#endif
/* Deferred UI refresh after editor close (no pipe during teardown). */
#ifndef WM_VST_LIVE_EDITOR_CLOSED_UI
#define WM_VST_LIVE_EDITOR_CLOSED_UI (WM_VST_LIVE_EDITOR_CLOSED + 1)
#endif
void VstLiveEditorSetNotifyHwnd(HWND hwnd);
/* Optional second listener (VST Host UI) — same WM_VST_LIVE_EDITOR_CLOSED. */
void VstLiveEditorSetNotifyHwnd2(HWND hwnd);
/* Clear "editor closing" quiet flag (score calls after deferred UI refresh). */
void VstLiveEditorClearClosingQuiet(void);
/* Keep VST process() + waveOut running (Host AudioThread equivalent).
   Required for HALion MediaBay keyboard / editor notes when score is open. */
void VstLiveMonitorEnsure(void);
void VstLiveMonitorStop(void);
/* Poll Host64 editorClosedSeq without needing the audio mix thread. */
void VstLivePollRemoteEditorClosed(void);
/* 1 = part loaded. outPath may be NULL. */
int VstLivePartIsLoaded(int part1to32);
/* 1 = any live part is hosted in KpiHost64 (x64). */
int VstLiveAnyRemotePart(void);
/* 1 = editor HWND alive (Host64: HALion Home/MediaBay up — do not PROG/STATE IPC). */
int VstLivePartEditorIsOpen(int part1to32);
/* Copy live plugin path for part; returns 1 if non-empty. */
int VstLivePartGetPath(int part1to32, wchar_t* outPath, int outCch);

// パートがプラグインへ渡す MIDI チャンネル。-1=届いたチャンネルのまま。
// 0..15=強制。自チャンネルしか聴かないドラムキットでも、どのスロットでも鳴る。
int VstLiveSendChannel(int part1to32);
void VstLiveSetSendChannel(int part1to32, int sendCh);
// プログラム（HALion / SampleTank / Groove Agent のキット、VST2 パッチ）。
int VstLiveProgramCount(int part1to32);
int VstLiveProgramCurrent(int part1to32);
int VstLiveProgramName(int part1to32, int index, wchar_t* out, int outChars);
// スロットメニュー用の一括取得。リモートパートならパイプ往復が 1 回で済む。
// 戻りは書き込んだ名前の数。
int VstLiveProgramNames(int part1to32, int first, int count, wchar_t* out,
	int stride);
int VstLiveSetProgram(int part1to32, int index);
/* VST3 state chunk: which 0=component, 1=controller. Get*: malloc; caller free. */
int VstLiveGetState(int part1to32, int which, unsigned char** outBytes, int* outLen);
int VstLiveSetState(int part1to32, int which, const unsigned char* bytes, int len);
int VstLiveApplyStates(int part1to32,
	const unsigned char* comp, int compLen,
	const unsigned char* ctrl, int ctrlLen);
/* Snapshot live VST3 states into caller buffers (malloc). Returns 1 if any blob. */
int VstLiveCaptureStates(int part1to32,
	unsigned char** outComp, int* outCompLen,
	unsigned char** outCtrl, int* outCtrlLen);
/* 1 if part is multi-timbral (SC-VA etc.). */
int VstLivePartIsMulti(int part1to32);
/* GS/XG bank+PC to the live part (MIDI), and store as current prog. */
void VstLiveSendBankProgram(int part1to32, int bankMsb, int bankLsb, int prog0to127);
/* Fire a short preview note (MIDI only; auto note-off). */
void VstLiveAuditionNote(int part1to32, int noteMidi, int velocity, int durMs);
/* Cut any pending audition (call before closing tone map). */
void VstLiveAuditionStop(void);

// パートがいま聴いている内容。UI がチャンネルを点灯するのに使う。
struct VstLiveActInfo {
	int held;            // いま押されているノート数
	int note;            // 最後のノート番号
	int vel;             // 最後のベロシティ
	int ageMs;           // このチャンネルの最終メッセージからの ms。未受信は -1
	unsigned mask[4];    // mask[n/32] の bit n = ノート n が押されている
};
int VstLiveActivity(int part1to32, struct VstLiveActInfo* out);
// 最後に見た SysEx。表示用に要約済み。
int VstLiveSysexInfo(wchar_t* out, int chars, int* ageMs);

// ハードウェア／PC 鍵盤から VST ホストが実際に送った MIDI（SMF thru ではない）。
// MIDI モニタが UI タイマーで Drain する。
void VstLiveTapPushShort(int portIndex0to2, DWORD shortMsg);
void VstLiveTapPushSysex(int portIndex0to2, const unsigned char* data, int bytes);
int VstLiveTapStealShorts(BYTE* ports, DWORD* msgs, int maxCount);
int VstLiveTapStealSysex(int* portIndex0to2, unsigned char* data, int maxBytes);

#ifdef __cplusplus
}
#endif
