#pragma once
// ============================================================================
// 二重 DS クロスフェードは撤去済み。API 互換のためのスタブ宣言のみ。
// ============================================================================

#include "ProAudio.h"
#include <dsound.h>

enum {
	PRO_XF_IDLE = 0,
	PRO_XF_PREFETCHING = 1,
	PRO_XF_READY = 2,
	PRO_XF_OVERLAP = 3,
	PRO_XF_PROMOTE = 4
};

// 先読み PCM 上限: 5s@48k stereo 16bit + 余裕。高SRは秒数が短くなる
enum {
	PRO_XF_PCM_MAX_BYTES = 48000 * 6 * 4, // ~6s stereo 16bit @48k
	// バックグラウンド先読み開始用（UI/再生スレッドを止めない）
	PRO_XF_PREPARE_MS = 8000,
	// StartFromHead 所要ぶん。remain<=xms ちょうどだと起動中に A が足りずクロスが途中切れする
	PRO_XF_START_LEAD_MS = 450
};

struct ProXfadeInfo {
	int phase;                 // PRO_XF_*
	int nextPlcnt;
	int nextMode;
	TCHAR nextPath[1024];
	int sampleRate;
	int channels;
	int bits;                  // 16
	int pcmBytes;
	int overlapMs;
	int armQueuedMs;           // BeginOverlap 時点の DS 未再生キュー(ms)
	DWORD overlapStartTick;
	__int64 overlapBytesDone;  // OVERLAP 中に書いた出力バイト（ゲイン進行）
	__int64 overlapBytesTarget;// = outRate*bpf*overlapMs/1000（設定msと一致）
	int skipFrames;            // 昇格後にシークするフレーム数
	int applyInSuppress;       // 1=疑似 ApplyXfadeIn を抑止
	int failed;                // 1=この曲では二重DSを諦めてレガシーへ
	int pcmFullB;              // 1=昇格直前〜 PCM ゲインを B=1,A=0 に固定
	int bHeadStarted;          // 1=StartFromHead 済み（A スレッドを Sleep 待ちしない）
	DWORD bHeadStartTick;      // StartFromHead 時刻（リング準備タイムアウト用）
	BYTE pcm[PRO_XF_PCM_MAX_BYTES];
};

// パスから共通形式 mode を返す。非対応は 0
int  ProXfade_ModeFromPath(LPCTSTR path);
bool ProXfade_IsSupportedMode(int mode);

void ProXfade_Reset();
int  ProXfade_Phase();
bool ProXfade_IsDualActive(); // READY/OVERLAP/PROMOTE
bool ProXfade_ShouldSuppressApplyIn();
bool ProXfade_HasFailed();
void ProXfade_MarkFailed();

// 先読み開始（同期・短寿命デコーダ）。成功で READY
bool ProXfade_Prefetch(LPCTSTR path, int mode, int nextPlcnt, int xfadeMs);
// 専用スレッドで Prefetch（UI / DS リアルタイムをブロックしない）
bool ProXfade_RequestPrefetchAsync(LPCTSTR path, int mode, int nextPlcnt, int xfadeMs);

const ProXfadeInfo* ProXfade_Get();

void ProXfade_BeginOverlap(int xfadeMs);
// outRate/outBpf: フェード目標バイト。armQueuedMs: 開始時点の未再生キュー
//   可聴フェードは「曲末 xms」。開始は remain<=xms+queued で行い、キュー分の純Aのあとに xms の Mix が来る。
void ProXfade_BeginOverlapEx(int xfadeMs, int outRate, int outBpf, int armQueuedMs = 0);
void ProXfade_NoteOverlapWrite(int outBytes);
__int64 ProXfade_OverlapBytesDone();
__int64 ProXfade_OverlapBytesTarget();
void ProXfade_TickOverlapVolumes(LPDIRECTSOUNDBUFFER8 dsbA, LPDIRECTSOUNDBUFFER8 dsbB);
/* Mix 用: overlapBytesDone 基準の連続ゲイン（壁時計を使わない） */
bool ProXfade_GetMixGainsForBytes(int outBytes, float& gA0, float& gA1, float& gB0, float& gB1);
void ProXfade_ApplyPcmGainRamp(BYTE* pcm, int bytes, int bits, float gain0, float gain1);
/* Mix 失敗時フォールバック。バイト進行の A 側ゲイン */
bool ProXfade_PrepChunkGain(int outBytes, bool isB, float& g0, float& g1);
bool ProXfade_OverlapFinished();
/* dsQueuedBytes = A の DS 未再生バイト。可聴フェード完了で true（途中昇格の被り防止） */
bool ProXfade_ReadyToPromote(__int64 dsQueuedBytes);
int  ProXfade_CurrentSkipFrames();

// 二重スロット: B を本デコーダで用意できたとき READY にする（PCM 先読み不要）
void ProXfade_ArmSlotReady(int nextPlcnt, int mode, LPCTSTR path, int bSlot);
/* READY→OVERLAP: B 頭出しは1回だけ。リング準備中も A の供給を止めない（旧 Sleep 待ちは先頭二重の原因） */
bool ProXfade_EnsureBFromHead(int bSlot);
bool ProXfade_BRingReadyForOverlap(int outRate, int outBpf);
extern volatile LONG g_xfadeBSlot; // クロス中の B スロット index

void ProXfade_OnSongPlaybackStarted();
bool ProXfade_DualArmOk(int xfadeMs);

// UI: 昇格要求を立てる / 消費する（スロット handoff 用。Restart しない）
void ProXfade_RequestPromote(int skipFrames);
bool ProXfade_ConsumePromote(int& outPlcnt, int& outMode, CString& outPath, int& outSkipFrames, int& outSkipMs);
// シーク時に進行中クロスを破棄。戻り値=閉じるべき B スロット（無ければ -1）
int  ProXfade_CancelPendingCrossfade();

// play()/stop() 連携
extern volatile LONG g_xfadeKeepDsb;   // 1= Closeds せず m_dsb を維持
extern volatile LONG g_xfadeSkipFrames;
extern volatile LONG g_xfadeSkipMs;
extern volatile LONG g_xfadePrefetchPosted; // 1= UI に先読み要求済み
extern volatile LONG g_xfadeNoWrite;       // 1= 昇格後は notify が DS に書かない
extern volatile LONG g_xfadeReuseContinue; // 1= 昇格継続: SetCurrentPosition(0) しない
extern volatile LONG g_xfadeDeferCcWrite;  // 1= OVERLAP 中 playwav の cc.Write を止め、合成側だけ書く
extern volatile LONG g_xfadeHoldCcFile;    // 1= 連続クロス中は cc を閉じず song1 ファイルへ追記

// cc.Write 用: 先頭で開いた形式（例 24bit/192k）に以降を合わせる
void PlaybackCcLockFormat(int rate, int ch, int bits);
void PlaybackCcClearFormat();
bool PlaybackCcFormatLocked();
void PlaybackCcGetFormat(int& rate, int& ch, int& bits);

/* 廃止: OVERLAP は通常と同じ WriteCursor ギャップのみ。互換のため残置。 */
bool ProXfade_XfWriteBudget(ULONG playCursor, ULONG oldw, ULONG ring,
	int bpf, int rate, int wantLeadMs, int maxChunkMs, int& outBytes);

// OVERLAP 用: B PCM をリングから消費して A に合成
bool PlaySlot_MixIncomingS16(BYTE* inout, int bytes);
bool PlaySlot_MixIncomingEx(BYTE* inout, int bytes, int bits, int ch, int* outMixed = nullptr);
void PlaySlot_StoreIncomingS16(const BYTE* p, int bytes);
void PlaySlot_ClearIncomingMix();
int  PlaySlot_MixRingBytes();

// 再生 WAV 保存（クロス中は Defer を尊重）。形式は Lock 済みならそれに変換。
void PlaybackCcWrite(const void* p, UINT n);
void PlaybackCcWriteForced(const void* p, UINT n);
void PlaybackCcWriteFromFormat(const void* p, UINT n, int srcRate, int srcCh, int srcBits, bool forced);

// DS ヘルパ（oggDlg_ds / oggDlg から）
bool ProXfade_CreateSecondaryBuffer(LPDIRECTSOUND8 ds, LPDIRECTSOUNDBUFFER8* outDsb8,
	LPDIRECTSOUNDBUFFER* outDsb1, const ProXfadeInfo* info, ULONG minBytes);
void ProXfade_ReleaseSecondary(LPDIRECTSOUNDBUFFER8* dsb8, LPDIRECTSOUNDBUFFER* dsb1);

LONG ProXfade_GainToDsVolume(float gain01);

// UI スレッド用メッセージ（oggDlg）
// ※ WM_APP+70/71 は TIMERP / REFRESH_AERO で使用中。衝突禁止。
enum { WM_OGG_XFADE_PREFETCH = WM_APP + 102, WM_OGG_XFADE_PROMOTE = WM_APP + 103 };
