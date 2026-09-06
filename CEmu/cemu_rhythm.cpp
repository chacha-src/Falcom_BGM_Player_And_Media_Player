#include "StdAfx.h"
#include "cemu_rhythm.h"
#include "cemu_mgr.h"
#include <wchar.h>
#include <stdlib.h>

enum { kRhythmMaxDepth = 8, kRhythmQueueMax = 512 };

static int RhythmSkipDir(const wchar_t* name)
{
	if (!name || !name[0] || name[0] == L'.') return 1;
	if (_wcsicmp(name, L"System Volume Information") == 0) return 1;
	if (_wcsicmp(name, L"$Recycle.Bin") == 0) return 1;
	if (_wcsicmp(name, L"node_modules") == 0) return 1;
	if (_wcsicmp(name, L".git") == 0) return 1;
	return 0;
}

static void RhythmStripSlash(wchar_t* p)
{
	if (!p) return;
	size_t n = wcslen(p);
	while (n > 0 && (p[n - 1] == L'\\' || p[n - 1] == L'/'))
		p[--n] = 0;
}

static void RhythmExeDir(wchar_t* out, int outCch)
{
	if (!out || outCch <= 0) return;
	out[0] = 0;
	if (!GetModuleFileNameW(NULL, out, (DWORD)outCch))
		return;
	wchar_t* slash = wcsrchr(out, L'\\');
	if (slash) *slash = 0;
	else out[0] = 0;
}

static int RhythmFileExists(const wchar_t* path)
{
	return path && path[0]
		&& GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static unsigned RhythmFileSize(const wchar_t* path)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!path || !path[0] || !GetFileAttributesExW(path, GetFileExInfoStandard, &fad))
		return 0;
	if (fad.nFileSizeHigh) return 0xffffffffu;
	return fad.nFileSizeLow;
}

/* Visit root and every subfolder up to kRhythmMaxDepth.
   onDir returns 1 to stop. */
static int RhythmWalkUnder(const wchar_t* root,
	int (*onDir)(const wchar_t* dir, void* ctx), void* ctx)
{
	if (!root || !root[0] || !onDir) return 0;
	wchar_t q[kRhythmQueueMax][MAX_PATH];
	int depth[kRhythmQueueMax];
	int head = 0, tail = 0;
	wcsncpy_s(q[tail], root, _TRUNCATE);
	RhythmStripSlash(q[tail]);
	if (!q[tail][0]) return 0;
	depth[tail] = 0;
	tail++;
	while (head < tail) {
		const wchar_t* cur = q[head];
		const int d = depth[head];
		head++;
		if (onDir(cur, ctx))
			return 1;
		if (d >= kRhythmMaxDepth) continue;
		wchar_t pattern[MAX_PATH];
		_snwprintf_s(pattern, _TRUNCATE, L"%s\\*", cur);
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW(pattern, &fd);
		if (h == INVALID_HANDLE_VALUE) continue;
		do {
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
			if (RhythmSkipDir(fd.cFileName)) continue;
			if (tail >= kRhythmQueueMax) break;
			_snwprintf_s(q[tail], _TRUNCATE, L"%s\\%s", cur, fd.cFileName);
			depth[tail] = d + 1;
			tail++;
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
	return 0;
}

struct RhythmWavCtx {
	wchar_t* out;
	int outCch;
};

static int RhythmOnDirWav(const wchar_t* dir, void* ctx)
{
	RhythmWavCtx* c = (RhythmWavCtx*)ctx;
	wchar_t probe[MAX_PATH];
	_snwprintf_s(probe, _TRUNCATE, L"%s\\2608_BD.WAV", dir);
	if (!RhythmFileExists(probe)) return 0;
	_snwprintf_s(c->out, (size_t)c->outCch, _TRUNCATE, L"%s\\", dir);
	return 1;
}

struct RhythmRomCtx {
	wchar_t* out;
	int outCch;
	unsigned bestSize;
};

static int RhythmTryRomInDir(const wchar_t* dir, RhythmRomCtx* c)
{
	static const wchar_t* kNames[] = {
		L"ym2608_adpcm_rom.bin",
		L"2608_adpcm_rom.bin",
		NULL
	};
	for (int i = 0; kNames[i]; i++) {
		wchar_t probe[MAX_PATH];
		_snwprintf_s(probe, _TRUNCATE, L"%s\\%s", dir, kNames[i]);
		const unsigned sz = RhythmFileSize(probe);
		/* Prefer the real 8KiB ADPCM-A ROM; skip huge misnamed blobs. */
		if (sz < 0x1000 || sz > 0x8000) continue;
		if (!c->out[0] || sz == 0x2000 || (c->bestSize != 0x2000 && sz < c->bestSize)) {
			wcsncpy_s(c->out, (size_t)c->outCch, probe, _TRUNCATE);
			c->bestSize = sz;
			if (sz == 0x2000) return 1;
		}
	}
	return 0;
}

static int RhythmOnDirRom(const wchar_t* dir, void* ctx)
{
	return RhythmTryRomInDir(dir, (RhythmRomCtx*)ctx);
}

static int RhythmFindWavDir(wchar_t* out, int outCch)
{
	if (!out || outCch <= 0) return 0;
	out[0] = 0;
	RhythmWavCtx ctx = { out, outCch };

	wchar_t roots[4][MAX_PATH];
	int nRoots = 0;
	auto addRoot = [&](const wchar_t* r) {
		if (!r || !r[0] || nRoots >= 4) return;
		wcsncpy_s(roots[nRoots], r, _TRUNCATE);
		RhythmStripSlash(roots[nRoots]);
		if (!roots[nRoots][0]) return;
		for (int i = 0; i < nRoots; i++)
			if (_wcsicmp(roots[i], roots[nRoots]) == 0) return;
		nRoots++;
	};

	CEmuMgr* m = CEmuMgrGet();
	if (m && m->dataRoot[0]) {
		wchar_t dataRoot[MAX_PATH], parent[MAX_PATH];
		wcsncpy_s(dataRoot, m->dataRoot, _TRUNCATE);
		RhythmStripSlash(dataRoot);
		addRoot(dataRoot);
		wcsncpy_s(parent, dataRoot, _TRUNCATE);
		wchar_t* slash = wcsrchr(parent, L'\\');
		if (slash) { *slash = 0; addRoot(parent); }
		static const wchar_t* kDataTry[] = {
			L"rhythm", L"Rhythm", L"wav", L"data\\rhythm", L"data\\Rhythm", NULL
		};
		for (int i = 0; kDataTry[i]; i++) {
			wchar_t p[MAX_PATH];
			_snwprintf_s(p, _TRUNCATE, L"%s\\%s", dataRoot, kDataTry[i]);
			if (RhythmOnDirWav(p, &ctx)) return 1;
		}
	}

	wchar_t exeDir[MAX_PATH];
	RhythmExeDir(exeDir, (int)_countof(exeDir));
	addRoot(exeDir);

	static const wchar_t* kPrefer[] = {
		L"Plugins\\kbsasami",
		L"Plugins\\Kobarin\\fmpmd\\Rhythm",
		L"Plugins\\Kobarin\\fmpmd",
		L"Plugins\\Mamiya\\kpis98",
		L"Rhythm",
		L"wav",
		NULL
	};
	for (int r = 0; r < nRoots; r++) {
		for (int i = 0; kPrefer[i]; i++) {
			wchar_t p[MAX_PATH];
			_snwprintf_s(p, _TRUNCATE, L"%s\\%s", roots[r], kPrefer[i]);
			if (RhythmOnDirWav(p, &ctx)) return 1;
		}
	}
	for (int r = 0; r < nRoots; r++) {
		if (RhythmWalkUnder(roots[r], RhythmOnDirWav, &ctx))
			return 1;
	}
	return out[0] ? 1 : 0;
}

static int RhythmFindAdpcmRom(wchar_t* out, int outCch)
{
	if (!out || outCch <= 0) return 0;
	out[0] = 0;
	RhythmRomCtx ctx = { out, outCch, 0 };
	wchar_t roots[4][MAX_PATH];
	int nRoots = 0;
	auto addRoot = [&](const wchar_t* r) {
		if (!r || !r[0] || nRoots >= 4) return;
		wcsncpy_s(roots[nRoots], r, _TRUNCATE);
		RhythmStripSlash(roots[nRoots]);
		if (!roots[nRoots][0]) return;
		for (int i = 0; i < nRoots; i++)
			if (_wcsicmp(roots[i], roots[nRoots]) == 0) return;
		nRoots++;
	};

	wchar_t exeDir[MAX_PATH];
	RhythmExeDir(exeDir, (int)_countof(exeDir));
	addRoot(exeDir);

	/* CEmu data lives under .../ogg_binary/data — Plugins is the sibling. */
	CEmuMgr* m = CEmuMgrGet();
	if (m && m->dataRoot[0]) {
		wchar_t dataRoot[MAX_PATH], parent[MAX_PATH];
		wcsncpy_s(dataRoot, m->dataRoot, _TRUNCATE);
		RhythmStripSlash(dataRoot);
		addRoot(dataRoot);
		wcsncpy_s(parent, dataRoot, _TRUNCATE);
		wchar_t* slash = wcsrchr(parent, L'\\');
		if (slash) {
			*slash = 0;
			addRoot(parent);
		}
	}

	static const wchar_t* kPrefer[] = {
		L"Plugins\\kbsasami",
		L"Plugins\\kbsasami\\x64",
		L"Plugins\\Kobarin\\fmpmd",
		L"Plugins\\Kobarin\\fmpmd\\Rhythm",
		L"Plugins\\Mamiya\\kpis98",
		L"",
		NULL
	};
	for (int r = 0; r < nRoots; r++) {
		for (int i = 0; kPrefer[i]; i++) {
			wchar_t p[MAX_PATH];
			if (kPrefer[i][0])
				_snwprintf_s(p, _TRUNCATE, L"%s\\%s", roots[r], kPrefer[i]);
			else
				wcsncpy_s(p, roots[r], _TRUNCATE);
			if (RhythmTryRomInDir(p, &ctx) && ctx.bestSize == 0x2000)
				return 1;
		}
	}
	for (int r = 0; r < nRoots; r++) {
		RhythmWalkUnder(roots[r], RhythmOnDirRom, &ctx);
		if (ctx.bestSize == 0x2000) return 1;
	}
	return out[0] ? 1 : 0;
}

void GetRhythmPath(wchar_t* pszPath, int nSize)
{
	if (!pszPath || nSize <= 0) return;
	pszPath[0] = 0;
	RhythmFindWavDir(pszPath, nSize);
}

void CEmuLoadExternalYm2608Adpcm(CChip* chip)
{
	/* Always load/overwrite ADPCM-A. Real YM2608 rhythm ROM is fixed silicon;
	   zip "adpcm" is ADPCM-B and must not block this. */
	if (!chip) return;
	if (getenv("CEMU_SKIP_RHYTHM")) return;
	wchar_t path[MAX_PATH];
	if (!RhythmFindAdpcmRom(path, (int)_countof(path))) return;

	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	LARGE_INTEGER li;
	if (!GetFileSizeEx(h, &li) || li.QuadPart < 0x1000 || li.QuadPart > 0x8000) {
		CloseHandle(h);
		return;
	}
	const unsigned n = (unsigned)li.QuadPart;
	uint8_t* buf = (uint8_t*)malloc(n);
	if (!buf) { CloseHandle(h); return; }
	DWORD got = 0;
	const BOOL ok = ReadFile(h, buf, n, &got, NULL);
	CloseHandle(h);
	if (ok && got >= 0x1000)
		chip->SetAdpcmRom(buf, got > 0x2000 ? 0x2000u : got, 0);
	free(buf);
}
