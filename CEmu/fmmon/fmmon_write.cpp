#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "fmmon_write.h"
#include "sasami_fmmon_map.h"

/* %TEMP%\ogg_cemu を用意。KPI の ogg_kbsasami とは別 */
static void FmMonEnsureDir(wchar_t* dir, int n)
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	/* KPI/SASAMI は %TEMP%\ogg_kbsasami。CEmu は混ぜない */
	_snwprintf_s(dir, n, _TRUNCATE, L"%sogg_cemu", tmp);
	CreateDirectoryW(dir, NULL);
}

/* 毎回 CreateFile/Close すると描画ごと極端に重い → ハンドル＋MapView 常駐 */
static CRITICAL_SECTION s_ioCs;
static LONG s_ioOnce = 0;
static HANDLE s_hLive = INVALID_HANDLE_VALUE;
static HANDLE s_hRing = INVALID_HANDLE_VALUE;
static HANDLE s_hLiveMap = NULL;
static HANDLE s_hRingMap = NULL;
static SasamiFmMonDump* s_liveView = NULL;
static SasamiFmMonRing* s_ringView = NULL;
static uint32_t s_gen = 0;
static int s_ringReady = 0;

static void FmMonIoInit()
{
	if (InterlockedCompareExchange(&s_ioOnce, 1, 0) == 0)
		InitializeCriticalSection(&s_ioCs);
}

static BOOL FmMonEnsureRingSized(HANDLE h)
{
	LARGE_INTEGER sz;
	sz.QuadPart = 0;
	if (!GetFileSizeEx(h, &sz)) return FALSE;
	const ULONGLONG need = (ULONGLONG)sizeof(SasamiFmMonRing);
	if ((ULONGLONG)sz.QuadPart == need)
		return TRUE;

	SasamiFmMonRingHdr hdr;
	memset(&hdr, 0, sizeof(hdr));
	hdr.magic[0] = 'O'; hdr.magic[1] = 'P'; hdr.magic[2] = 'N'; hdr.magic[3] = 'R';
	hdr.version = SASAMI_FMMON_RING_VERSION;
	DWORD wr = 0;
	SetFilePointer(h, 0, NULL, FILE_BEGIN);
	if (!WriteFile(h, &hdr, sizeof(hdr), &wr, NULL) || wr != sizeof(hdr))
		return FALSE;

	LARGE_INTEGER end;
	end.QuadPart = (LONGLONG)need;
	if (!SetFilePointerEx(h, end, NULL, FILE_BEGIN))
		return FALSE;
	return SetEndOfFile(h) ? TRUE : FALSE;
}

static BOOL FmMonEnsureLiveSized(HANDLE h)
{
	LARGE_INTEGER sz;
	sz.QuadPart = 0;
	if (!GetFileSizeEx(h, &sz)) return FALSE;
	const ULONGLONG need = (ULONGLONG)sizeof(SasamiFmMonDump);
	if ((ULONGLONG)sz.QuadPart == need)
		return TRUE;
	LARGE_INTEGER end;
	end.QuadPart = (LONGLONG)need;
	if (!SetFilePointerEx(h, end, NULL, FILE_BEGIN))
		return FALSE;
	return SetEndOfFile(h) ? TRUE : FALSE;
}

static int FmMonMapLive()
{
	if (s_liveView) return 1;
	if (s_hLive == INVALID_HANDLE_VALUE) return 0;
	if (!FmMonEnsureLiveSized(s_hLive)) return 0;
	return SasamiFmMonMapLive(s_hLive, 1, &s_hLiveMap, &s_liveView);
}

static int FmMonMapRing()
{
	if (s_ringView) return 1;
	if (s_hRing == INVALID_HANDLE_VALUE || !s_ringReady) return 0;
	if (!SasamiFmMonMapRing(s_hRing, 1, &s_hRingMap, &s_ringView))
		return 0;
	if (s_ringView->magic[0] != 'O' || s_ringView->magic[1] != 'P'
		|| s_ringView->magic[2] != 'N' || s_ringView->magic[3] != 'R') {
		s_ringView->magic[0] = 'O';
		s_ringView->magic[1] = 'P';
		s_ringView->magic[2] = 'N';
		s_ringView->magic[3] = 'R';
		s_ringView->version = SASAMI_FMMON_RING_VERSION;
		s_ringView->reserved = 0;
		s_ringView->gen = 0;
	}
	if (s_ringView->gen > s_gen)
		s_gen = s_ringView->gen;
	return 1;
}

static HANDLE FmMonOpenLive()
{
	if (s_hLive != INVALID_HANDLE_VALUE)
		return s_hLive;
	wchar_t dir[MAX_PATH], path[MAX_PATH];
	FmMonEnsureDir(dir, MAX_PATH);
	_snwprintf_s(path, _TRUNCATE, L"%s\\fmmon_live.opna", dir);
	s_hLive = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (s_hLive != INVALID_HANDLE_VALUE)
		FmMonMapLive();
	return s_hLive;
}

static HANDLE FmMonOpenRing()
{
	if (s_hRing != INVALID_HANDLE_VALUE)
		return s_hRing;
	wchar_t dir[MAX_PATH], path[MAX_PATH];
	FmMonEnsureDir(dir, MAX_PATH);
	_snwprintf_s(path, _TRUNCATE, L"%s\\fmmon_ring.opna", dir);
	s_hRing = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (s_hRing != INVALID_HANDLE_VALUE) {
		s_ringReady = FmMonEnsureRingSized(s_hRing) ? 1 : 0;
		if (!s_ringReady) {
			CloseHandle(s_hRing);
			s_hRing = INVALID_HANDLE_VALUE;
		} else {
			FmMonMapRing();
		}
	}
	return s_hRing;
}

void FmMonInitDump(SasamiFmMonDump* d)
{
	if (!d) return;
	memset(d, 0, sizeof(*d));
	d->magic[0] = 'O'; d->magic[1] = 'P'; d->magic[2] = 'N'; d->magic[3] = 'A';
	d->version = SASAMI_FMMON_VERSION_V7;
	memset(d->keyMidi, 0xFF, sizeof(d->keyMidi));
	memset(d->exMidi, 0xFF, sizeof(d->exMidi));
	memset(d->ssgMidi, 0xFF, sizeof(d->ssgMidi));
	d->pad6[2] = (uint8_t)(SASAMI_FMMON_VIEW_KEYS
		| SASAMI_FMMON_VIEW_REGS | SASAMI_FMMON_VIEW_PANELS
		| SASAMI_FMMON_CLOCK_DUMP);
}

void FmMonWriteRingReset(void)
{
	FmMonIoInit();
	EnterCriticalSection(&s_ioCs);
	s_gen = 0;
	FmMonOpenRing();
	FmMonOpenLive();
	/* inode は握ったまま gen だけ 0。UI の常駐 MapView が旧ファイルに残らない */
	if (s_ringView) {
		s_ringView->gen = 0;
		s_ringView->magic[0] = 'O';
		s_ringView->magic[1] = 'P';
		s_ringView->magic[2] = 'N';
		s_ringView->magic[3] = 'R';
		s_ringView->version = SASAMI_FMMON_RING_VERSION;
		MemoryBarrier();
	} else if (s_hRing != INVALID_HANDLE_VALUE) {
		SasamiFmMonRingHdr hdr;
		memset(&hdr, 0, sizeof(hdr));
		hdr.magic[0] = 'O'; hdr.magic[1] = 'P'; hdr.magic[2] = 'N'; hdr.magic[3] = 'R';
		hdr.version = SASAMI_FMMON_RING_VERSION;
		hdr.gen = 0;
		DWORD wr = 0;
		SetFilePointer(s_hRing, 0, NULL, FILE_BEGIN);
		WriteFile(s_hRing, &hdr, sizeof(hdr), &wr, NULL);
	}
	if (s_liveView) {
		memset(s_liveView, 0, sizeof(*s_liveView));
	} else if (s_hLive != INVALID_HANDLE_VALUE) {
		SasamiFmMonDump z;
		memset(&z, 0, sizeof(z));
		DWORD wr = 0;
		SetFilePointer(s_hLive, 0, NULL, FILE_BEGIN);
		WriteFile(s_hLive, &z, sizeof(z), &wr, NULL);
	}
	LeaveCriticalSection(&s_ioCs);
}

void FmMonWriteDump(const SasamiFmMonDump* d)
{
	if (!d) return;
	SasamiFmMonDump stamped = *d;
	stamped.pad6[2] = (uint8_t)(stamped.pad6[2] | SASAMI_FMMON_CLOCK_DUMP);
	d = &stamped;
	FmMonIoInit();
	EnterCriticalSection(&s_ioCs);

	FmMonOpenRing();
	FmMonOpenLive();
	if (s_ringView) {
		if (s_ringView->gen > s_gen)
			s_gen = s_ringView->gen;
		SasamiFmMonPublishDump(s_ringView, &s_gen, s_liveView, d);
	} else {
		s_gen++;
		HANDLE hl = s_hLive;
		if (hl != INVALID_HANDLE_VALUE) {
			DWORD wr = 0;
			SetFilePointer(hl, 0, NULL, FILE_BEGIN);
			WriteFile(hl, d, sizeof(*d), &wr, NULL);
		}
	}

	LeaveCriticalSection(&s_ioCs);
}
