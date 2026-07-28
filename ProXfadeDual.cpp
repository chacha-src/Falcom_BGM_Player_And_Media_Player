#include "stdafx.h"
#include "ProXfadeDual.h"
#include <dsound.h>

/* 二重 DS クロスフェードは撤去。呼び出し互換のためスタブのみ残す。
   ProAudio_XfadeMs() は常に 0。連続再生はレガシー timer9000 経路。 */

volatile LONG g_xfadeKeepDsb = 0;
volatile LONG g_xfadeSkipFrames = 0;
volatile LONG g_xfadeSkipMs = 0;
volatile LONG g_xfadePrefetchPosted = 0;
volatile LONG g_xfadeNoWrite = 0;
volatile LONG g_xfadeReuseContinue = 0;
volatile LONG g_xfadeBSlot = -1;
volatile LONG g_xfadeDeferCcWrite = 0;
volatile LONG g_xfadeHoldCcFile = 0;

static ProXfadeInfo g_xf;
static DWORD g_xfSongStartTick = 0;

int ProXfade_ModeFromPath(LPCTSTR path)
{
	(void)path;
	return 0;
}

bool ProXfade_IsSupportedMode(int mode)
{
	(void)mode;
	return false;
}

void ProXfade_Reset()
{
	ZeroMemory(&g_xf, sizeof(g_xf));
	g_xf.phase = PRO_XF_IDLE;
	g_xf.nextPlcnt = -1;
	g_xf.bits = 16;
	g_xfSongStartTick = 0;
	InterlockedExchange(&g_xfadeKeepDsb, 0);
	InterlockedExchange(&g_xfadeSkipFrames, 0);
	InterlockedExchange(&g_xfadeSkipMs, 0);
	InterlockedExchange(&g_xfadePrefetchPosted, 0);
	InterlockedExchange(&g_xfadeNoWrite, 0);
	InterlockedExchange(&g_xfadeReuseContinue, 0);
	InterlockedExchange(&g_xfadeBSlot, -1);
	InterlockedExchange(&g_xfadeDeferCcWrite, 0);
}

int ProXfade_Phase() { return PRO_XF_IDLE; }
bool ProXfade_IsDualActive() { return false; }
bool ProXfade_ShouldSuppressApplyIn() { return false; }
bool ProXfade_HasFailed() { return false; }
void ProXfade_MarkFailed() {}
const ProXfadeInfo* ProXfade_Get() { return &g_xf; }

LONG ProXfade_GainToDsVolume(float gain01)
{
	(void)gain01;
	return DSBVOLUME_MIN;
}

bool ProXfade_Prefetch(LPCTSTR path, int mode, int nextPlcnt, int xfadeMs)
{
	(void)path; (void)mode; (void)nextPlcnt; (void)xfadeMs;
	return false;
}

bool ProXfade_RequestPrefetchAsync(LPCTSTR path, int mode, int nextPlcnt, int xfadeMs)
{
	(void)path; (void)mode; (void)nextPlcnt; (void)xfadeMs;
	return false;
}

void ProXfade_ArmSlotReady(int nextPlcnt, int mode, LPCTSTR path, int bSlot)
{
	(void)nextPlcnt; (void)mode; (void)path; (void)bSlot;
}

bool ProXfade_EnsureBFromHead(int bSlot)
{
	(void)bSlot;
	return false;
}

bool ProXfade_BRingReadyForOverlap(int outRate, int outBpf)
{
	(void)outRate; (void)outBpf;
	return false;
}

void ProXfade_BeginOverlap(int xfadeMs) { (void)xfadeMs; }
void ProXfade_BeginOverlapEx(int xfadeMs, int outRate, int outBpf, int armQueuedMs)
{
	(void)xfadeMs; (void)outRate; (void)outBpf; (void)armQueuedMs;
}

void ProXfade_NoteOverlapWrite(int outBytes) { (void)outBytes; }
__int64 ProXfade_OverlapBytesDone() { return 0; }
__int64 ProXfade_OverlapBytesTarget() { return 0; }

bool ProXfade_GetMixGainsForBytes(int outBytes, float& gA0, float& gA1, float& gB0, float& gB1)
{
	(void)outBytes;
	gA0 = gA1 = 1.f;
	gB0 = gB1 = 0.f;
	return false;
}

void ProXfade_ApplyPcmGainRamp(BYTE* pcm, int bytes, int bits, float gain0, float gain1)
{
	(void)pcm; (void)bytes; (void)bits; (void)gain0; (void)gain1;
}

bool ProXfade_PrepChunkGain(int outBytes, bool isB, float& g0, float& g1)
{
	(void)outBytes; (void)isB;
	g0 = g1 = 1.f;
	return false;
}

void ProXfade_TickOverlapVolumes(LPDIRECTSOUNDBUFFER8 dsbA, LPDIRECTSOUNDBUFFER8 dsbB)
{
	(void)dsbA; (void)dsbB;
}

bool ProXfade_XfWriteBudget(ULONG playCursor, ULONG oldw, ULONG ring,
	int bpf, int rate, int wantLeadMs, int maxChunkMs, int& outBytes)
{
	(void)playCursor; (void)oldw; (void)ring; (void)bpf; (void)rate;
	(void)wantLeadMs; (void)maxChunkMs;
	outBytes = 0;
	return false;
}

bool ProXfade_OverlapFinished() { return false; }
bool ProXfade_ReadyToPromote(__int64 dsQueuedBytes)
{
	(void)dsQueuedBytes;
	return false;
}

int ProXfade_CurrentSkipFrames() { return 0; }

void ProXfade_OnSongPlaybackStarted()
{
	g_xfSongStartTick = GetTickCount();
}

bool ProXfade_DualArmOk(int xfadeMs)
{
	/* 曲開始直後の偽 EOF で次曲へ飛ばないための猶予。
	   クロス撤去後も timer9000 / endflg 保護に必要（常時 false だと連続再生が死ぬ）。 */
	(void)xfadeMs;
	if (g_xfSongStartTick == 0)
		return true;
	return (GetTickCount() - g_xfSongStartTick) >= 3000u;
}

void ProXfade_RequestPromote(int skipFrames) { (void)skipFrames; }

int ProXfade_CancelPendingCrossfade()
{
	ProXfade_Reset();
	return -1;
}

bool ProXfade_ConsumePromote(int& outPlcnt, int& outMode, CString& outPath, int& outSkipFrames, int& outSkipMs)
{
	outPlcnt = -1;
	outMode = 0;
	outPath.Empty();
	outSkipFrames = 0;
	outSkipMs = 0;
	return false;
}

void ProXfade_ReleaseSecondary(LPDIRECTSOUNDBUFFER8* dsb8, LPDIRECTSOUNDBUFFER* dsb1)
{
	if (dsb8 && *dsb8) { (*dsb8)->Release(); *dsb8 = NULL; }
	if (dsb1 && *dsb1) { (*dsb1)->Release(); *dsb1 = NULL; }
}

bool ProXfade_CreateSecondaryBuffer(LPDIRECTSOUND8 ds, LPDIRECTSOUNDBUFFER8* outDsb8,
	LPDIRECTSOUNDBUFFER* outDsb1, const ProXfadeInfo* info, ULONG minBytes)
{
	(void)ds; (void)info; (void)minBytes;
	if (outDsb8) *outDsb8 = NULL;
	if (outDsb1) *outDsb1 = NULL;
	return false;
}
