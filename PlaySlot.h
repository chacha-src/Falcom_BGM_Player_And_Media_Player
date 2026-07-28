#pragma once
// ============================================================================
// PlaySlot: スロット再生（クロスフェード二重DSは撤去。Init/Stop/Seek 互換用）
// 各スロット: 再生スレッド + DS バッファ + デコーダインスタンス
// ============================================================================

#include "stdafx.h"
#include <dsound.h>

enum { PLAY_SLOT_COUNT = 2 };

// デコーダ実体は PlaySlot.cpp 内（mp3.h / flac.h の前提を他 TU に漏らさない）
struct PlaySlotDecoders;

struct PlaySlot {
	int idx;
	CWinThread* thread;
	LPDIRECTSOUNDBUFFER8 dsb;
	LPDIRECTSOUNDBUFFER dsb1;
	ULONG oldw;
	ULONG ringBytes;           // 実バッファ長（g_ds_buffer_bytes と食い違うと半速/ループの元）
	__int64 dsWritten;
	__int64 heard;
	__int64 playb;
	int openMode;              // -10/-8/-1/-6/-9/999 / INT_MIN=閉
	volatile LONG stopReq;     // 1=停止要求
	volatile LONG running;     // 1=スレッド稼働中
	volatile LONG endflg;      // 1=EOF
	volatile LONG seekParkReq; // 1=フィードに一時停止要求
	volatile LONG seekParked;  // 1=フィードが安全点で停止中
	volatile LONG seekDoReq;   // 1=パーク中にシーク実行
	volatile LONG seekDone;    // 1=シーク完了
	__int64 seekToFrames;

	PlaySlotDecoders* dec;

	BYTE* pcmExtra;
	int pcmExtraBytes;
	int pcmExtraPos;

	TCHAR path[1024];
	int rate;                  // ソース
	int ch;
	int bits;
	int outRate;               // DS/bufwav3 出力（曲1再生中の g_ds_pcm_* に合わせる）
	int outCh;
	int outBits;
	int oggsize;
	int flacMode;              // スロット固有（グローバル flacmode を触ると曲1と衝突）

	PlaySlot()
		: idx(0), thread(NULL), dsb(NULL), dsb1(NULL)
		, oldw(0), ringBytes(0), dsWritten(0), heard(0), playb(0)
		, openMode(INT_MIN), stopReq(0), running(0), endflg(0)
		, seekParkReq(0), seekParked(0), seekDoReq(0), seekDone(0), seekToFrames(0)
		, dec(NULL)
		, pcmExtra(NULL), pcmExtraBytes(0), pcmExtraPos(0)
		, rate(44100), ch(2), bits(16)
		, outRate(44100), outCh(2), outBits(16), oggsize(0), flacMode(0)
	{
		path[0] = 0;
	}
};

extern PlaySlot g_playSlots[PLAY_SLOT_COUNT];
extern volatile LONG g_activeSlot;     // timerp / UI が見るスロット
extern volatile LONG g_stoppingSlot;   // -1=ユーザ停止で両方, 0/1=そのスロットのみ
extern volatile LONG g_slotDualEnabled; // 1=二重スロット運用中
extern volatile LONG g_eqResetNext;     // 1=次回 equaliser を reset

void PlaySlot_InitAll();
void PlaySlot_Reset(int slot);
bool PlaySlot_CloseDecoder(int slot);
bool PlaySlot_ReleaseBuffer(int slot);

bool PlaySlot_OpenFile(int slot, LPCTSTR path, int mode);
bool PlaySlot_CreateBuffer(int slot, LPDIRECTSOUND8 ds, ULONG bytes);

bool PlaySlot_BeginFeed(int slot);
// オーバーラップ直前: 頭から prefills → フィード開始（prepare 中は Open のみ）
bool PlaySlot_StartFromHead(int slot);
bool PlaySlot_StopFeed(int slot, DWORD joinTimeoutMs);
bool PlaySlot_StopFeedAndClose(int slot, DWORD joinTimeoutMs);

bool PlaySlot_Handoff(int oldSlot, int newSlot);
bool PlaySlot_HandoffFromLegacy(int newSlot, LPDIRECTSOUNDBUFFER8 oldDsb, LPDIRECTSOUNDBUFFER oldDsb1);
/* 昇格時: フェード終了位置へ B をシークし直して継続 */
void PlaySlot_ApplyPromotePosition(int slot);

void PlaySlot_StopAll();
bool PlaySlot_Seek(int slot, __int64 playbFrames);
extern volatile LONG g_seekUiFreshTick;
// 二重スロット再生中か（シークは必ずこっち。グローバルデコーダは閉じ済み）
bool PlaySlot_IsDualSeekTarget(int* outSlot);
void PlaySlot_ZeroRing(int slot);

int  PlaySlot_IdleSlot();
PlaySlot& PlaySlot_Active();
LPDIRECTSOUNDBUFFER8 PlaySlot_ActiveDsb();

UINT PlaySlot_FeedThread(LPVOID pSlotIndex);
