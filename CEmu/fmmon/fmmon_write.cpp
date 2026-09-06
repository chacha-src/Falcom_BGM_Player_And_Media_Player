#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "fmmon_write.h"

static void FmMonEnsureDir(wchar_t* dir, int n)
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	_snwprintf_s(dir, n, _TRUNCATE, L"%sogg_kbsasami", tmp);
	CreateDirectoryW(dir, NULL);
}

/* 毎回 CreateFile/Close すると描画ごと極端に重い → ハンドル常駐 */
static CRITICAL_SECTION s_ioCs;
static LONG s_ioOnce = 0;
static HANDLE s_hLive = INVALID_HANDLE_VALUE;
static HANDLE s_hRing = INVALID_HANDLE_VALUE;
static uint32_t s_gen = 0;
static int s_ringReady = 0;
static uint32_t s_liveEvery = 0; /* live は間引き書き（I/O 負荷） */

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
	if ((ULONGLONG)sz.QuadPart >= need)
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

static HANDLE FmMonOpenLive()
{
	if (s_hLive != INVALID_HANDLE_VALUE)
		return s_hLive;
	wchar_t dir[MAX_PATH], path[MAX_PATH];
	FmMonEnsureDir(dir, MAX_PATH);
	_snwprintf_s(path, _TRUNCATE, L"%s\\fmmon_live.opna", dir);
	s_hLive = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
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
		}
	}
	return s_hRing;
}

void FmMonInitDump(SasamiFmMonDump* d)
{
	if (!d) return;
	memset(d, 0, sizeof(*d));
	d->magic[0] = 'O'; d->magic[1] = 'P'; d->magic[2] = 'N'; d->magic[3] = 'A';
	d->version = SASAMI_FMMON_VERSION_V6;
	memset(d->keyMidi, 0xFF, sizeof(d->keyMidi));
	memset(d->exMidi, 0xFF, sizeof(d->exMidi));
	memset(d->ssgMidi, 0xFF, sizeof(d->ssgMidi));
}

void FmMonWriteDump(const SasamiFmMonDump* d)
{
	if (!d) return;
	FmMonIoInit();
	EnterCriticalSection(&s_ioCs);

	HANDLE hr = FmMonOpenRing();
	if (hr != INVALID_HANDLE_VALUE) {
		/* Sync gen from file so multi-module writers don't regress "latest". */
		{
			SasamiFmMonRingHdr rhdr;
			DWORD rd = 0;
			LARGE_INTEGER z; z.QuadPart = 0;
			SetFilePointerEx(hr, z, NULL, FILE_BEGIN);
			if (ReadFile(hr, &rhdr, sizeof(rhdr), &rd, NULL) && rd == sizeof(rhdr)
				&& rhdr.magic[0] == 'O' && rhdr.magic[1] == 'P'
				&& rhdr.magic[2] == 'N' && rhdr.magic[3] == 'R'
				&& rhdr.gen > s_gen)
				s_gen = rhdr.gen;
		}
		const uint32_t idx = s_gen % SASAMI_FMMON_RING;
		s_gen++;
		const uint32_t gen = s_gen;
		DWORD wr = 0;
		LARGE_INTEGER off;
		off.QuadPart = (LONGLONG)offsetof(SasamiFmMonRing, slot)
			+ (LONGLONG)idx * (LONGLONG)sizeof(SasamiFmMonDump);
		SetFilePointerEx(hr, off, NULL, FILE_BEGIN);
		WriteFile(hr, d, sizeof(*d), &wr, NULL);

		SasamiFmMonRingHdr hdr;
		memset(&hdr, 0, sizeof(hdr));
		hdr.magic[0] = 'O'; hdr.magic[1] = 'P'; hdr.magic[2] = 'N'; hdr.magic[3] = 'R';
		hdr.version = SASAMI_FMMON_RING_VERSION;
		hdr.gen = gen;
		off.QuadPart = 0;
		SetFilePointerEx(hr, off, NULL, FILE_BEGIN);
		WriteFile(hr, &hdr, sizeof(hdr), &wr, NULL);

		/* live は 8 回に 1 回（フォールバック用。毎書きは KSS 96k で重い） */
		if ((++s_liveEvery % 8u) == 1u) {
			HANDLE hl = FmMonOpenLive();
			if (hl != INVALID_HANDLE_VALUE) {
				wr = 0;
				SetFilePointer(hl, 0, NULL, FILE_BEGIN);
				WriteFile(hl, d, sizeof(*d), &wr, NULL);
			}
		}
	} else {
		s_gen++;
		HANDLE hl = FmMonOpenLive();
		if (hl != INVALID_HANDLE_VALUE) {
			DWORD wr = 0;
			SetFilePointer(hl, 0, NULL, FILE_BEGIN);
			WriteFile(hl, d, sizeof(*d), &wr, NULL);
		}
	}

	LeaveCriticalSection(&s_ioCs);
}
