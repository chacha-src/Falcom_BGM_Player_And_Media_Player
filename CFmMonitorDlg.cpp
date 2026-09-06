#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "CFmMonitorDlg.h"
#include "CMediaPlayerDlg.h"
#include "PlayList.h"
#include "PluginKinds.h"
#include "resource.h"
#include "DatArchive.h"
#include <algorithm>
#include <math.h>
#include <string.h>

extern save savedata;
extern CString filen;
extern CString fnn;
extern int mode;
extern CPlayList* pl;
extern int plcnt;

namespace {

static void FmPathStem(const wchar_t* path, wchar_t* stem, int n)
{
	if (!stem || n < 2) return;
	stem[0] = 0;
	if (!path || !path[0]) return;
	const wchar_t* base = path;
	for (const wchar_t* p = path; *p; p++)
		if (*p == L'\\' || *p == L'/') base = p + 1;
	wcsncpy_s(stem, n, base, _TRUNCATE);
	wchar_t* dot = wcsrchr(stem, L'.');
	if (dot && dot != stem) *dot = 0;
}

/* KPI dump stem match: .fpy / PMD / FMP / MSX */
static int FmExtBoundaryOk(wchar_t c)
{
	/* ".kss::0001" / ".kss)" / 終端を許す（旧実装は :: で拡張子判定が死んでいた） */
	return (c == 0 || c == L')' || c == L' ' || c == L':' || c == L'/' || c == L'\\') ? 1 : 0;
}

static int FmIsMonExtSuffix(const wchar_t* p)
{
	if (!p || !*p) return 0;
	static const wchar_t* kExt[] = {
		L".fpy2", L".fpy", L".m2", L".mz", L".mp", L".ms", L".m",
		L".opi", L".ovi", L".ozi",
		L".s98", L".vgm", L".vgz", L".gym", L".ssl", L".dro", L".cym", L".mym", L".x1f",
		L".mdx", L".mdc", L".cmf", L".laa",
		L".sc68", L".sndh",
		/* 長い拡張子を先に（.psf が .psf2/.minipsf を食わない） */
		L".minipsf2", L".psf2", L".minipsf", L".psf",
		L".minigsf", L".gsf", L".nsfe", L".nsf", L".ncsf", L".spc", L".sid",
		L".midi", L".mid", L".rmi",
		L".kss", L".hes", L".mgs", L".bgm", L".opx", L".mpk", L".mbm"
	};
	for (int i = 0; i < (int)(sizeof(kExt)/sizeof(kExt[0])); i++) {
		const size_t el = wcslen(kExt[i]);
		if (_wcsnicmp(p, kExt[i], el) != 0) continue;
		if (FmExtBoundaryOk(p[el])) return 1;
	}
	const size_t len = wcslen(p);
	for (int i = 0; i < (int)(sizeof(kExt)/sizeof(kExt[0])); i++) {
		const size_t el = wcslen(kExt[i]);
		if (len >= el && _wcsnicmp(p + len - el, kExt[i], el) == 0)
			return 1;
	}
	return 0;
}

static void FmExtractBestMonStem(const wchar_t* path, wchar_t* stem, int n)
{
	if (!stem || n < 2) return;
	stem[0] = 0;
	if (!path || !path[0]) return;
	const wchar_t* best = NULL;
	int bestLen = 0;
	static const wchar_t* kExt[] = {
		L".fpy2", L".fpy", L".m2", L".mz", L".mp", L".ms", L".m",
		L".opi", L".ovi", L".ozi",
		L".s98", L".vgm", L".vgz", L".gym", L".ssl",
		L".mdx", L".mdc", L".cmf", L".laa",
		L".sc68", L".sndh",
		/* 長い拡張子を先に（.psf が .psf2/.minipsf を食わない） */
		L".minipsf2", L".psf2", L".minipsf", L".psf",
		L".minigsf", L".gsf", L".nsfe", L".nsf", L".ncsf", L".spc", L".sid",
		L".midi", L".mid", L".rmi",
		L".kss", L".hes", L".mgs", L".bgm", L".opx", L".mpk", L".mbm"
	};
	for (const wchar_t* p = path; *p; p++) {
		if (*p != L'(') continue;
		const wchar_t* start = p + 1;
		const wchar_t* close = wcschr(start, L')');
		if (!close || close <= start + 2) continue;
		for (int e = 0; e < (int)(sizeof(kExt)/sizeof(kExt[0])); e++) {
			const size_t el = wcslen(kExt[e]);
			if ((size_t)(close - start) > el && _wcsnicmp(close - (int)el, kExt[e], el) == 0) {
				best = start;
				bestLen = (int)(close - start - (int)el);
				break;
			}
		}
	}
	if (best && bestLen > 0 && bestLen < n) {
		for (int i = 0; i < bestLen; i++) stem[i] = best[i];
		stem[bestLen] = 0;
		return;
	}
	FmPathStem(path, stem, n);
}

static int FmIsKpiAliasPath(const wchar_t* p)
{
	if (!p || !p[0]) return 1;
	return (_wcsicmp(p, L"C:\\MUSIC_DIR\\MUSIC_FILE.xxx") == 0
		|| _wcsicmp(p, L"MUSIC_FILE.xxx") == 0) ? 1 : 0;
}

static int FmPlayHasMonHint()
{
	const wchar_t* hints[4] = { (LPCWSTR)filen, (LPCWSTR)fnn, NULL, NULL };
	if (pl && pl->pc && plcnt >= 0 && plcnt < pl->playcnt) {
		hints[2] = pl->pc[plcnt].fol;
		hints[3] = pl->pc[plcnt].name;
	}
	for (int i = 0; i < 4; i++) {
		const wchar_t* h = hints[i];
		if (!h || !h[0]) continue;
		if (FmIsMonExtSuffix(h)) return 1;
		for (const wchar_t* q = h; *q; q++) {
			if (*q == L'.' && FmIsMonExtSuffix(q)) return 1;
		}
	}
	return 0;
}

/* KPI 再生パスに拡張子があるか */
static int FmPlayPathHasExt(const wchar_t* ext)
{
	if (!ext || !ext[0]) return 0;
	const size_t el = wcslen(ext);
	const wchar_t* hints[4] = { (LPCWSTR)filen, (LPCWSTR)fnn, NULL, NULL };
	if (pl && pl->pc && plcnt >= 0 && plcnt < pl->playcnt) {
		hints[2] = pl->pc[plcnt].fol;
		hints[3] = pl->pc[plcnt].name;
	}
	for (int i = 0; i < 4; i++) {
		const wchar_t* h = hints[i];
		if (!h || !h[0]) continue;
		const size_t len = wcslen(h);
		/* 末尾一致（"song.psf"）。".psf2" の末尾4文字は "psf2" なので ".psf" には当たらない */
		if (len >= el && _wcsnicmp(h + len - (int)el, ext, el) == 0) return 1;
		for (const wchar_t* q = h; *q; q++) {
			if (*q != L'.') continue;
			if (_wcsnicmp(q, ext, el) != 0) continue;
			if (FmExtBoundaryOk(q[el])) return 1;
		}
	}
	return 0;
}

static int FmPlayLooksLikeMsx()
{
	return (FmPlayPathHasExt(L".kss") || FmPlayPathHasExt(L".hes")
		|| FmPlayPathHasExt(L".mgs") || FmPlayPathHasExt(L".bgm")
		|| FmPlayPathHasExt(L".opx") || FmPlayPathHasExt(L".mpk")
		|| FmPlayPathHasExt(L".mbm")) ? 1 : 0;
}

static int FmDumpMatchesPlay(const SasamiFmMonDump& d)
{
	if (mode != -3)
		return 1;
	/* MSX KPI dump は path 表記ゆれが激しいので KPI 再生中は常に受け入れる */
	if (d.version >= 6 && (d.dumpFlags & SASAMI_FMMON_FLAG_MSX))
		return 1;
	/* keys-only（PSF/SID/GSF/NCSF 等）も path 表記ゆれで捨てない */
	if (d.version >= 6 && (d.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY)
		&& FmPlayHasMonHint())
		return 1;
	/* PMDWin 直書き / KPI エイリアス path は実曲名と一致しないので、
	   再生中が PMD/FMP/FPY なら受け入れる。 */
	if (FmIsKpiAliasPath(d.sourcePath))
		return FmPlayHasMonHint();

	wchar_t dumpStem[260];
	FmPathStem(d.sourcePath, dumpStem, 260);
	if (!dumpStem[0])
		return FmPlayHasMonHint();

	auto tryRaw = [&](const wchar_t* raw) -> int {
		if (!raw || !raw[0]) return 0;
		wchar_t playStem[260];
		FmExtractBestMonStem(raw, playStem, 260);
		if (playStem[0] && _wcsicmp(playStem, dumpStem) == 0)
			return 1;
		const size_t sl = wcslen(dumpStem);
		for (const wchar_t* p = raw; *p; p++) {
			if (_wcsnicmp(p, dumpStem, sl) != 0) continue;
			const wchar_t* after = p + sl;
			if (*after == L'.' || *after == L')' || *after == 0 || *after == L':')
				return 1;
		}
		return 0;
	};

	if (tryRaw((LPCWSTR)filen)) return 1;
	if (tryRaw((LPCWSTR)fnn)) return 1;
	if (pl && pl->pc && plcnt >= 0 && plcnt < pl->playcnt) {
		if (tryRaw(pl->pc[plcnt].fol)) return 1;
		if (tryRaw(pl->pc[plcnt].name)) return 1;
	}

	/* SASAMI v5: path 表記ゆれでも .fpy 再生中なら受け入れる（無描画防止） */
	if (d.version >= 2 && d.version <= 5 && FmPlayPathHasExt(L".fpy2"))
		return 1;
	if (d.version >= 2 && d.version <= 5 && FmPlayPathHasExt(L".fpy"))
		return 1;

	if (FmPlayLooksLikeMsx())
		return 1;

	return FmPlayHasMonHint() ? 0 : 1;
}

/* 再生中ファイルの stem。曲切替検知用 */
static void FmPlayIdentity(wchar_t* out, int n)
{
	if (!out || n < 2) return;
	out[0] = 0;
	const wchar_t* hints[4] = { (LPCWSTR)filen, (LPCWSTR)fnn, NULL, NULL };
	if (pl && pl->pc && plcnt >= 0 && plcnt < pl->playcnt) {
		hints[2] = pl->pc[plcnt].fol;
		hints[3] = pl->pc[plcnt].name;
	}
	for (int i = 0; i < 4; i++) {
		if (!hints[i] || !hints[i][0]) continue;
		FmExtractBestMonStem(hints[i], out, n);
		if (out[0]) return;
		FmPathStem(hints[i], out, n);
		if (out[0]) return;
	}
}

#pragma pack(push, 1)
struct FmMonGeomFile {
	char magic[4]; // "FMMG"
	int version;   // 1
	int open;      // 0/1
	int x, y, w, h;
};
#pragma pack(pop)

/* DatArc ステージ上の fmmon_geom.dat（アーカイブに同梱・起動時も残る） */
static void FmMonGeomPath(wchar_t* out, int n)
{
	LPCTSTR stage = DatArc_StageDir();
	if (stage && stage[0]) {
		_snwprintf_s(out, n, _TRUNCATE, L"%sfmmon_geom.dat", stage);
		return;
	}
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	_snwprintf_s(out, n, _TRUNCATE, L"%sogg_kbsasami\\fmmon_geom.dat", tmp);
}

static void FmMonGeomSave(int open, int x, int y, int w, int h)
{
	FmMonGeomFile g = {};
	g.magic[0] = 'F'; g.magic[1] = 'M'; g.magic[2] = 'M'; g.magic[3] = 'G';
	g.version = 1;
	g.open = open ? 1 : 0;
	g.x = x; g.y = y; g.w = w; g.h = h;
	wchar_t path[MAX_PATH];
	FmMonGeomPath(path, MAX_PATH);
	/* 親ディレクトリ保証（temp fallback 時） */
	{
		wchar_t dir[MAX_PATH];
		wcsncpy_s(dir, path, _TRUNCATE);
		wchar_t* sl = wcsrchr(dir, L'\\');
		if (sl) {
			*sl = 0;
			CreateDirectoryW(dir, NULL);
		}
	}
	HANDLE hf = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hf == INVALID_HANDLE_VALUE) return;
	DWORD wr = 0;
	WriteFile(hf, &g, sizeof(g), &wr, NULL);
	FlushFileBuffers(hf);
	CloseHandle(hf);
	DatArc_InvalidateLeaf(L"fmmon_geom.dat");
	DatArc_Commit(L"fmmon_geom.dat");
}

static int FmMonGeomLoad(FmMonGeomFile* out)
{
	/* アーカイブから展開済みのステージを優先 */
	CString staged = DatArc_Path(L"fmmon_geom.dat");
	wchar_t path[MAX_PATH];
	if (!staged.IsEmpty())
		wcsncpy_s(path, staged, _TRUNCATE);
	else
		FmMonGeomPath(path, MAX_PATH);
	HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hf == INVALID_HANDLE_VALUE) {
		/* 旧 exe隣 .bin 互換 */
		wchar_t mod[MAX_PATH];
		GetModuleFileNameW(NULL, mod, MAX_PATH);
		wchar_t* slash = wcsrchr(mod, L'\\');
		if (slash) slash[1] = 0;
		else mod[0] = 0;
		_snwprintf_s(path, _TRUNCATE, L"%sfmmon_geom.bin", mod);
		hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hf == INVALID_HANDLE_VALUE) return 0;
	}
	DWORD rd = 0;
	FmMonGeomFile g = {};
	BOOL ok = ReadFile(hf, &g, sizeof(g), &rd, NULL);
	CloseHandle(hf);
	if (!ok || rd != sizeof(g)) return 0;
	if (g.magic[0] != 'F' || g.magic[1] != 'M' || g.magic[2] != 'M' || g.magic[3] != 'G') return 0;
	if (g.version != 1) return 0;
	*out = g;
	return 1;
}

static UINT FmUiDpi(HWND hwnd)
{
	typedef UINT(WINAPI* PFN)(HWND);
	static PFN s_fn = nullptr;
	static BOOL s_got = FALSE;
	if (!s_got) {
		HMODULE u = ::GetModuleHandleW(L"user32.dll");
		if (u) s_fn = (PFN)::GetProcAddress(u, "GetDpiForWindow");
		s_got = TRUE;
	}
	if (s_fn && hwnd) {
		const UINT d = s_fn(hwnd);
		if (d) return d;
	}
	return 96;
}

static int FmScale(int v, UINT dpi) { return MulDiv(v, (int)dpi, 96); }

static void FmBump(BYTE& g) { g = 255; }

static void FmTickGlow(BYTE& g)
{
	if (g == 0) return;
	g = (BYTE)((g * 7) / 8);
	if (g < 8) g = 0;
}

static COLORREF FmMixFade(COLORREF base, COLORREF hi, BYTE fade)
{
	if (fade == 0) return base;
	const int a = fade;
	const int r = (GetRValue(base) * (255 - a) + GetRValue(hi) * a) / 255;
	const int g = (GetGValue(base) * (255 - a) + GetGValue(hi) * a) / 255;
	const int b = (GetBValue(base) * (255 - a) + GetBValue(hi) * a) / 255;
	return RGB(r, g, b);
}

static void FmFillFade(CDC& dc, int x, int y, int w, int h, COLORREF base, COLORREF hi, BYTE fade)
{
	if (w <= 0 || h <= 0) return;
	dc.FillSolidRect(x, y, w, h, FmMixFade(base, hi, fade));
}

static void FmMonLivePath(wchar_t* out, int n)
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	_snwprintf_s(out, n, _TRUNCATE, L"%sogg_kbsasami\\fmmon_live.opna", tmp);
}

static void FmMonRingPath(wchar_t* out, int n)
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	_snwprintf_s(out, n, _TRUNCATE, L"%sogg_kbsasami\\fmmon_ring.opna", tmp);
}

static HANDLE s_hLiveRd = INVALID_HANDLE_VALUE;
static HANDLE s_hRingRd = INVALID_HANDLE_VALUE;

static HANDLE FmOpenLiveRd()
{
	if (s_hLiveRd != INVALID_HANDLE_VALUE)
		return s_hLiveRd;
	wchar_t path[MAX_PATH];
	FmMonLivePath(path, MAX_PATH);
	s_hLiveRd = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	return s_hLiveRd;
}

static HANDLE FmOpenRingRd()
{
	if (s_hRingRd != INVALID_HANDLE_VALUE)
		return s_hRingRd;
	wchar_t path[MAX_PATH];
	FmMonRingPath(path, MAX_PATH);
	s_hRingRd = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	return s_hRingRd;
}

static void FmInvalidateRdHandles()
{
	if (s_hLiveRd != INVALID_HANDLE_VALUE) {
		CloseHandle(s_hLiveRd);
		s_hLiveRd = INVALID_HANDLE_VALUE;
	}
	if (s_hRingRd != INVALID_HANDLE_VALUE) {
		CloseHandle(s_hRingRd);
		s_hRingRd = INVALID_HANDLE_VALUE;
	}
}

static int FmReadDump(SasamiFmMonDump* out)
{
	for (int attempt = 0; attempt < 2; attempt++) {
		HANDLE h = FmOpenLiveRd();
		if (h == INVALID_HANDLE_VALUE) {
			Sleep(0);
			continue;
		}
		SasamiFmMonDump tmp;
		memset(&tmp, 0, sizeof(tmp));
		DWORD rd = 0;
		SetFilePointer(h, 0, NULL, FILE_BEGIN);
		BOOL ok = ReadFile(h, &tmp, sizeof(tmp), &rd, NULL);
		if (!ok || rd == 0) {
			FmInvalidateRdHandles();
			continue;
		}
		if (SasamiFmMonMagicOk(tmp)) {
			const DWORD minPreV5 = (DWORD)offsetof(SasamiFmMonDump, regWriteBits);
			const DWORD minV5 = (DWORD)offsetof(SasamiFmMonDump, keyOnEx);
			const DWORD minV6 = (DWORD)sizeof(SasamiFmMonDump);
			DWORD need = minPreV5;
			if (tmp.version >= 6) need = minV6;
			else if (tmp.version >= 5) need = minV5;
			if (rd < need)
				continue;
			*out = tmp;
			return 1;
		}
	}
	return 0;
}

/* ring の最新 1 スロットを直接読む（genLast 取りこぼし／live 古いときの救済） */
static int FmReadLatestRingDump(SasamiFmMonDump* out)
{
	if (!out) return 0;
	HANDLE h = FmOpenRingRd();
	if (h == INVALID_HANDLE_VALUE) return 0;
	SasamiFmMonRingHdr hdr;
	memset(&hdr, 0, sizeof(hdr));
	DWORD rd = 0;
	SetFilePointer(h, 0, NULL, FILE_BEGIN);
	if (!ReadFile(h, &hdr, sizeof(hdr), &rd, NULL) || rd != sizeof(hdr)
		|| !SasamiFmMonRingMagicOk(hdr) || hdr.gen == 0) {
		FmInvalidateRdHandles();
		return 0;
	}
	const uint32_t idx = (hdr.gen - 1u) % SASAMI_FMMON_RING;
	LARGE_INTEGER off;
	off.QuadPart = (LONGLONG)offsetof(SasamiFmMonRing, slot)
		+ (LONGLONG)idx * (LONGLONG)sizeof(SasamiFmMonDump);
	if (!SetFilePointerEx(h, off, NULL, FILE_BEGIN))
		return 0;
	SasamiFmMonDump d;
	rd = 0;
	if (!ReadFile(h, &d, sizeof(d), &rd, NULL) || rd != sizeof(d) || !SasamiFmMonMagicOk(d))
		return 0;
	*out = d;
	return 1;
}

/* keys-only: PCM hit を keyOnHitCnt/ssg/ex/rhythm にパッキング（shadow と対） */
static uint8_t FmKeysOnlyPackedHit(const SasamiFmMonDump& d, int pcm)
{
	if (pcm < 0 || pcm >= SASAMI_FMMON_PCM_MAX) return 0;
	if (pcm < 6) return d.keyOnHitCnt[pcm];
	if (pcm < 9) return d.ssgHitCnt[pcm - 6];
	if (pcm < 12) return d.keyOnExHitCnt[pcm - 9];
	return d.rhythmHitCnt[pcm - 12];
}
static void FmKeysOnlySetPackedHit(SasamiFmMonDump& d, int pcm, uint8_t v)
{
	if (pcm < 0 || pcm >= SASAMI_FMMON_PCM_MAX) return;
	if (pcm < 6) d.keyOnHitCnt[pcm] = v;
	else if (pcm < 9) d.ssgHitCnt[pcm - 6] = v;
	else if (pcm < 12) d.keyOnExHitCnt[pcm - 9] = v;
	else d.rhythmHitCnt[pcm - 12] = v;
}
static void FmKeysOnlyMergePackedHits(SasamiFmMonDump& dst, const SasamiFmMonDump& src)
{
	for (int i = 0; i < SASAMI_FMMON_PCM_MAX; i++) {
		const uint8_t a = FmKeysOnlyPackedHit(dst, i);
		const uint8_t b = FmKeysOnlyPackedHit(src, i);
		if (b > a) FmKeysOnlySetPackedHit(dst, i, b);
		if (src.pcmOn[i] && src.pcmNote[i] != 0xFF)
			dst.pcmNote[i] = src.pcmNote[i];
	}
}

/* リング全体(~320KB)を毎回読まず、未消費スロットだけ読む */
static int FmDrainRingSlots(uint32_t* genLast,
	void (*onSlot)(const SasamiFmMonDump&, void*), void* ctx)
{
	if (!genLast || !onSlot) return 0;
	HANDLE h = FmOpenRingRd();
	if (h == INVALID_HANDLE_VALUE) return 0;

	SasamiFmMonRingHdr hdr;
	memset(&hdr, 0, sizeof(hdr));
	DWORD rd = 0;
	const DWORD hdrNeed = (DWORD)sizeof(hdr);
	SetFilePointer(h, 0, NULL, FILE_BEGIN);
	if (!ReadFile(h, &hdr, hdrNeed, &rd, NULL) || rd != hdrNeed
		|| !SasamiFmMonRingMagicOk(hdr)) {
		FmInvalidateRdHandles();
		return 0;
	}
	uint32_t from = *genLast;
	uint32_t to = hdr.gen;
	if (to == from)
		return 1;
	/* gen 後退 = 曲切替で ring 再作成。開いたハンドルは削除済み inode のまま
	   新ファイルを読めず、SASAMI(.fpy) が無描画になる */
	if (to < from) {
		FmInvalidateRdHandles();
		*genLast = 0;
		from = 0;
		h = FmOpenRingRd();
		if (h == INVALID_HANDLE_VALUE) return 0;
		memset(&hdr, 0, sizeof(hdr));
		rd = 0;
		SetFilePointer(h, 0, NULL, FILE_BEGIN);
		if (!ReadFile(h, &hdr, hdrNeed, &rd, NULL) || rd != hdrNeed
			|| !SasamiFmMonRingMagicOk(hdr)) {
			FmInvalidateRdHandles();
			return 0;
		}
		to = hdr.gen;
		if (to == 0)
			return 1;
	}
	if (to - from > SASAMI_FMMON_RING)
		from = to - SASAMI_FMMON_RING;
	/* 遅れ時は中間を OR マージしてから最新帯を読む（飛ばすと 16 分パルスが消える） */
	enum { kMaxDrain = 24 };
	SasamiFmMonDump skipMerged;
	int haveSkip = 0;
	if ((to - from) > (uint32_t)kMaxDrain) {
		const uint32_t skipTo = to - (uint32_t)kMaxDrain;
		memset(&skipMerged, 0, sizeof(skipMerged));
		for (uint32_t g = from; g < skipTo; g++) {
			const uint32_t idx = g % SASAMI_FMMON_RING;
			LARGE_INTEGER off;
			off.QuadPart = (LONGLONG)offsetof(SasamiFmMonRing, slot)
				+ (LONGLONG)idx * (LONGLONG)sizeof(SasamiFmMonDump);
			if (!SetFilePointerEx(h, off, NULL, FILE_BEGIN))
				continue;
			SasamiFmMonDump d;
			rd = 0;
			if (!ReadFile(h, &d, sizeof(d), &rd, NULL) || rd != sizeof(d))
				continue;
			if (!SasamiFmMonMagicOk(d)) continue;
			if (!haveSkip) {
				skipMerged = d;
				haveSkip = 1;
			} else {
				for (int i = 0; i < 6; i++) {
					skipMerged.keyOnFm[i] = (uint8_t)(skipMerged.keyOnFm[i] | d.keyOnFm[i]);
					if (d.keyOnHitCnt[i] > skipMerged.keyOnHitCnt[i])
						skipMerged.keyOnHitCnt[i] = d.keyOnHitCnt[i];
					if (d.keyMidi[i] != 0xFF && d.keyOnFm[i])
						skipMerged.keyMidi[i] = d.keyMidi[i];
					skipMerged.rhythmHitCnt[i] = (d.rhythmHitCnt[i] > skipMerged.rhythmHitCnt[i])
						? d.rhythmHitCnt[i] : skipMerged.rhythmHitCnt[i];
				}
				for (int i = 0; i < 3; i++) {
					skipMerged.ssgOn[i] = (uint8_t)(skipMerged.ssgOn[i] | d.ssgOn[i]);
					if (d.ssgHitCnt[i] > skipMerged.ssgHitCnt[i])
						skipMerged.ssgHitCnt[i] = d.ssgHitCnt[i];
					if (d.ssgMidi[i] != 0xFF && d.ssgOn[i])
						skipMerged.ssgMidi[i] = d.ssgMidi[i];
					skipMerged.keyOnEx[i] = (uint8_t)(skipMerged.keyOnEx[i] | d.keyOnEx[i]);
					if (d.keyOnExHitCnt[i] > skipMerged.keyOnExHitCnt[i])
						skipMerged.keyOnExHitCnt[i] = d.keyOnExHitCnt[i];
					if (d.exMidi[i] != 0xFF && d.keyOnEx[i])
						skipMerged.exMidi[i] = d.exMidi[i];
				}
				skipMerged.rhythmPulse = (uint8_t)(skipMerged.rhythmPulse | d.rhythmPulse);
				skipMerged.rhythmKey = (uint8_t)(skipMerged.rhythmKey | d.rhythmKey);
				/* keys-only PCM: gate は OR しない（張り付き）。hit/note だけ畳む */
				if (d.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY)
					FmKeysOnlyMergePackedHits(skipMerged, d);
				else {
					for (int i = 0; i < SASAMI_FMMON_PCM_MAX; i++) {
						if (d.pcmOn[i] && d.pcmNote[i] != 0xFF)
							skipMerged.pcmNote[i] = d.pcmNote[i];
					}
				}
				if (d.curSample >= skipMerged.curSample) {
					skipMerged.curSample = d.curSample;
					skipMerged.seq = d.seq;
					memcpy(skipMerged.regs, d.regs, sizeof(d.regs));
				}
			}
		}
		from = skipTo;
	}
	int any = 0;
	for (uint32_t g = from; g < to; g++) {
		const uint32_t idx = g % SASAMI_FMMON_RING;
		LARGE_INTEGER off;
		off.QuadPart = (LONGLONG)offsetof(SasamiFmMonRing, slot)
			+ (LONGLONG)idx * (LONGLONG)sizeof(SasamiFmMonDump);
		if (!SetFilePointerEx(h, off, NULL, FILE_BEGIN))
			continue;
		SasamiFmMonDump d;
		rd = 0;
		if (!ReadFile(h, &d, sizeof(d), &rd, NULL) || rd != sizeof(d))
			continue;
		if (!SasamiFmMonMagicOk(d)) continue;
		if (haveSkip) {
			for (int i = 0; i < 6; i++) {
				d.keyOnFm[i] = (uint8_t)(d.keyOnFm[i] | skipMerged.keyOnFm[i]);
				if (skipMerged.keyOnHitCnt[i] > d.keyOnHitCnt[i])
					d.keyOnHitCnt[i] = skipMerged.keyOnHitCnt[i];
				if (!d.keyOnFm[i] && skipMerged.keyOnFm[i] && skipMerged.keyMidi[i] != 0xFF)
					d.keyMidi[i] = skipMerged.keyMidi[i];
				d.rhythmHitCnt[i] = (skipMerged.rhythmHitCnt[i] > d.rhythmHitCnt[i])
					? skipMerged.rhythmHitCnt[i] : d.rhythmHitCnt[i];
			}
			for (int i = 0; i < 3; i++) {
				d.ssgOn[i] = (uint8_t)(d.ssgOn[i] | skipMerged.ssgOn[i]);
				if (skipMerged.ssgHitCnt[i] > d.ssgHitCnt[i])
					d.ssgHitCnt[i] = skipMerged.ssgHitCnt[i];
				d.keyOnEx[i] = (uint8_t)(d.keyOnEx[i] | skipMerged.keyOnEx[i]);
				if (skipMerged.keyOnExHitCnt[i] > d.keyOnExHitCnt[i])
					d.keyOnExHitCnt[i] = skipMerged.keyOnExHitCnt[i];
			}
			d.rhythmPulse = (uint8_t)(d.rhythmPulse | skipMerged.rhythmPulse);
			d.rhythmKey = (uint8_t)(d.rhythmKey | skipMerged.rhythmKey);
			if (skipMerged.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY)
				FmKeysOnlyMergePackedHits(d, skipMerged);
			else {
				for (int i = 0; i < SASAMI_FMMON_PCM_MAX; i++) {
					if (skipMerged.pcmNote[i] != 0xFF && skipMerged.pcmNote[i] != 0)
						d.pcmNote[i] = skipMerged.pcmNote[i];
				}
			}
			haveSkip = 0;
		}
		onSlot(d, ctx);
		any = 1;
	}
	/* ハンドル常駐（毎回 Close しない） */
	*genLast = to;
	(void)any;
	return 1; /* ファイルあり（新規スロット無しでも OK） */
}

static const wchar_t* kRzmName[6] = { L"BD", L"SD", L"TOP", L"HH", L"TOM", L"RIM" };

static constexpr COLORREF FM_BG = RGB(28, 32, 40);
/* クロマキーは使わない（白抜け防止）。描画は常に不透明 Stretch/BitBlt */

static int FmIsArcadePcmProfile(unsigned p)
{
	return (p == SASAMI_FMMON_KEYS_QSOUND
		|| p == SASAMI_FMMON_KEYS_RF5C
		|| p == SASAMI_FMMON_KEYS_C352
		|| p == SASAMI_FMMON_KEYS_SEGAPCM
		|| p == SASAMI_FMMON_KEYS_OKI) ? 1 : 0;
}

static int FmArcadePcmChannels(unsigned p)
{
	if (p == SASAMI_FMMON_KEYS_OKI) return 4;
	if (p == SASAMI_FMMON_KEYS_RF5C) return 8;
	return 16;
}

static const wchar_t* FmArcadePcmName(unsigned p)
{
	switch (p) {
	case SASAMI_FMMON_KEYS_QSOUND: return L"QSound";
	case SASAMI_FMMON_KEYS_RF5C: return L"RF5C/K053260";
	case SASAMI_FMMON_KEYS_C352: return L"C352";
	case SASAMI_FMMON_KEYS_SEGAPCM: return L"SegaPCM";
	case SASAMI_FMMON_KEYS_OKI: return L"OKI6295";
	default: return L"ArcadePCM";
	}
}

static const wchar_t* FmArcadePcmShort(unsigned p)
{
	switch (p) {
	case SASAMI_FMMON_KEYS_QSOUND: return L"QS";
	case SASAMI_FMMON_KEYS_RF5C: return L"RF";
	case SASAMI_FMMON_KEYS_C352: return L"C352";
	case SASAMI_FMMON_KEYS_SEGAPCM: return L"SPCM";
	case SASAMI_FMMON_KEYS_OKI: return L"OKI";
	default: return L"PCM";
	}
}

} // namespace

static void FmReleaseFontCache();

IMPLEMENT_DYNAMIC(CFmMonitorDlg, CCustomBlurDialogExBase)

CFmMonitorDlg::CFmMonitorDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_FMMONITOR, pParent)
	, m_histN(0), m_histHead(0)
	, m_lastSeq(0), m_lastCurSample(0), m_lastHeardSamp(0), m_heardAnchor(0)
	, m_heardQpc(0), m_heardFreq(0), m_ringGenLast(0), m_haveDump(0)
	, m_dirtyHead(1), m_dirtyHex(1), m_dirtyPanels(1), m_dirtyKeys(1), m_fullDraw(1)
	, m_panelDirtyMask(0x3F)
	, m_readFail(0), m_persistAge(-1), m_userClosing(0), m_lastPollMs(0)
	, m_lastPlayy(-1)
	, m_layOk(0)
	, m_frameOld(nullptr), m_frameW(0), m_frameH(0)
#if CCUSTOM_AERO_SUPPORT
	, m_chromaW(0), m_chromaH(0), m_chromaReady(false)
#endif
{
	memset(&m_dump, 0, sizeof(m_dump));
	memset(&m_prev, 0, sizeof(m_prev));
	memset(m_hist, 0, sizeof(m_hist));
	memset(m_histSamp, 0, sizeof(m_histSamp));
	memset(m_fade, 0, sizeof(m_fade));
	LARGE_INTEGER f;
	if (QueryPerformanceFrequency(&f) && f.QuadPart > 0)
		m_heardFreq = f.QuadPart;
	else
		m_heardFreq = 1;
	memset(m_touched, 0, sizeof(m_touched));
	memset(m_fadeKey, 0, sizeof(m_fadeKey));
	memset(m_fadeEx, 0, sizeof(m_fadeEx));
	memset(m_fadeSsg, 0, sizeof(m_fadeSsg));
	memset(m_fadePcm, 0, sizeof(m_fadePcm));
	memset(m_fadeRzmPad, 0, sizeof(m_fadeRzmPad));
	memset(&m_lay, 0, sizeof(m_lay));
	m_lastSong[0] = 0;
	m_playIdent[0] = 0;
}

CFmMonitorDlg::~CFmMonitorDlg()
{
	ReleasePaintBuffers();
	FmReleaseFontCache();
}

void CFmMonitorDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_FM_HELP, m_help);
}

BEGIN_MESSAGE_MAP(CFmMonitorDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_MOVE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_SYSCOMMAND()
	ON_BN_CLICKED(IDC_FM_HELP, &CFmMonitorDlg::OnBnClickedHelp)
END_MESSAGE_MAP()

BOOL CFmMonitorDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(LL14(
		L"FMモニタ (.fpy/PMD/FMP)",
		L"FM Monitor (.fpy/PMD/FMP)",
		L"Moniteur FM (.fpy/PMD/FMP)",
		L"Monitor FM (.fpy/PMD/FMP)",
		L"Monitor FM (.fpy/PMD/FMP)",
		L"FM 모니터 (.fpy/PMD/FMP)",
		L"FM监视器 (.fpy/PMD/FMP)",
		L"مراقب FM (.fpy/PMD/FMP)",
		L"FM-монитор (.fpy/PMD/FMP)",
		L"FM-Monitor (.fpy/PMD/FMP)",
		L"Monitor FM (.fpy/PMD/FMP)",
		L"FM-monitor (.fpy/PMD/FMP)",
		L"Monitor FM (.fpy/PMD/FMP)",
		L"FM izleyici (.fpy/PMD/FMP)"));
	ModifyStyle(WS_MINIMIZEBOX, 0);
	RestoreGeom();
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	SetTimer(1, 16, NULL);
	m_fullDraw = 1;
	m_dirtyHead = m_dirtyHex = m_dirtyPanels = m_dirtyKeys = 1;
	return TRUE;
}

void CFmMonitorDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CFmMonitorDlg::ReleasePaintBuffers()
{
	if (m_frameDC.GetSafeHdc()) {
		if (m_frameOld) m_frameDC.SelectObject(m_frameOld);
		m_frameOld = nullptr;
		m_frameBmp.DeleteObject();
		m_frameDC.DeleteDC();
	}
	m_frameW = m_frameH = 0;
	m_layOk = 0;
	m_fullDraw = 1;
#if CCUSTOM_AERO_SUPPORT
	m_chromaCache.Release();
	m_chromaReady = false;
	m_chromaW = m_chromaH = 0;
#endif
}

bool CFmMonitorDlg::EnsureFrameBuffer(CDC& refDC, int w, int h)
{
	if (w <= 0 || h <= 0) return false;
	if (m_frameDC.GetSafeHdc() && m_frameW == w && m_frameH == h)
		return true;
	ReleasePaintBuffers();
	if (!m_frameDC.CreateCompatibleDC(&refDC)) return false;
	if (!m_frameBmp.CreateCompatibleBitmap(&refDC, w, h)) {
		m_frameDC.DeleteDC();
		return false;
	}
	m_frameOld = m_frameDC.SelectObject(&m_frameBmp);
	m_frameW = w;
	m_frameH = h;
	return true;
}

void CFmMonitorDlg::OnDestroy()
{
	PersistGeom();
	ReleasePaintBuffers();
	CCustomBlurDialogExBase::OnDestroy();
}

void CFmMonitorDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED) return;
	if (CCC_IsAeroEnabled())
		CCC_RefreshDwmBlur(m_hWnd);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	/* 初期化中の誤保存を避け、ユーザー操作後だけ位置を書く（タイマーで間引き） */
	m_persistAge = 0;
	m_layOk = 0;
	m_fullDraw = 1;
	m_dirtyHead = m_dirtyHex = m_dirtyPanels = m_dirtyKeys = 1;
	Invalidate(FALSE);
}

void CFmMonitorDlg::OnMove(int x, int y)
{
	CCustomBlurDialogExBase::OnMove(x, y);
	m_persistAge = 0;
}

void CFmMonitorDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	CCustomBlurDialogExBase::OnSysCommand(nID, lParam);
}

void CFmMonitorDlg::OnBnClickedHelp()
{
	AfxMessageBox(LL14(
		L"SASAMI .fpy / PMD / FMP の OPNA 鍵盤（とレジスタ）を表示します。\n"
		L"PMD / FMP ともフル dump（レジスタ＋鍵盤）。取得失敗時のみ FMP は鍵盤のみ。\n"
		L"旧 fmpmd.kpi のみでは PMD/FMP は動きません。起動時に Plugins.zip が新しければ\n"
		L"サイレント更新されます（Render の KPI プラグイン DL でも取得可）。\n"
		L"kbsasami.raira=1 時、%TEMP%\\ogg_kbsasami\\ に dump を書き出します。",
		L"Shows OPNA keys (and registers) for SASAMI .fpy / PMD / FMP.\n"
		L"PMD/FMP: full dump; FMP falls back to keys-only if regs unavailable.\n"
		L"Old fmpmd.kpi alone will not drive PMD/FMP.\n"
		L"At startup, Plugins.zip is silently updated when newer\n"
		L"(or use KPI plugin download in Render).\n"
		L"With kbsasami.raira=1, dumps go to %TEMP%\\ogg_kbsasami\\.",
		L"Affiche les touches OPNA (et registres) pour SASAMI .fpy / PMD / FMP.\n"
		L"PMD: dump complet. FMP: touches + PPZ (pas de registres).\n"
		L"L'ancien fmpmd.kpi seul ne pilote pas PMD/FMP.\n"
		L"Au demarrage, Plugins.zip est mis a jour en silence s'il est plus recent\n"
		L"(ou telechargez les plugins KPI dans Render).\n"
		L"Avec kbsasami.raira=1, dump dans %TEMP%\\ogg_kbsasami\\.",
		L"Mostra tasti OPNA (e registri) per SASAMI .fpy / PMD / FMP.\n"
		L"PMD: dump completo. FMP: tasti + PPZ (senza registri).\n"
		L"Il solo vecchio fmpmd.kpi non guida PMD/FMP.\n"
		L"All'avvio, Plugins.zip viene aggiornato in silenzio se piu recente\n"
		L"(oppure scarica i plugin KPI in Render).\n"
		L"Con kbsasami.raira=1, dump in %TEMP%\\ogg_kbsasami\\.",
		L"Muestra teclas OPNA (y registros) para SASAMI .fpy / PMD / FMP.\n"
		L"PMD: dump completo. FMP: teclas + PPZ (sin registros).\n"
		L"Solo el viejo fmpmd.kpi no mueve PMD/FMP.\n"
		L"Al iniciar, Plugins.zip se actualiza en silencio si es mas nuevo\n"
		L"(o descargue plugins KPI en Render).\n"
		L"Con kbsasami.raira=1, dump en %TEMP%\\ogg_kbsasami\\.",
		L"SASAMI .fpy / PMD / FMP의 OPNA 건반(및 레지스터)을 표시합니다.\n"
		L"PMD는 전체 dump, FMP는 건반+PPZ(레지스터 비대응).\n"
		L"옛 fmpmd.kpi만으로는 PMD/FMP가 동작하지 않습니다.\n"
		L"시작 시 Plugins.zip이 더 새롭면 조용히 갱신됩니다\n"
		L"(Render의 KPI 플러그인 DL로도 받을 수 있음).\n"
		L"kbsasami.raira=1일 때 %TEMP%\\ogg_kbsasami\\에 dump를 씁니다.",
		L"显示 SASAMI .fpy / PMD / FMP 的 OPNA 键盘（及寄存器）。\n"
		L"PMD 为完整 dump，FMP 为键盘+PPZ（不支持寄存器）。\n"
		L"仅有旧版 fmpmd.kpi 时 PMD/FMP 不会驱动监视器。\n"
		L"启动时若 Plugins.zip 更新则会静默更新\n"
		L"（也可在 Render 中下载 KPI 插件）。\n"
		L"kbsasami.raira=1 时写入 %TEMP%\\ogg_kbsasami\\。",
		L"يعرض مفاتيح OPNA (والسجلات) لـ SASAMI .fpy / PMD / FMP.\n"
		L"PMD: تفريغ كامل. FMP: مفاتيح + PPZ (بدون سجلات).\n"
		L"ملف fmpmd.kpi القديم وحده لا يشغّل PMD/FMP.\n"
		L"عند التشغيل يُحدَّث Plugins.zip بصمت إن كان أحدث\n"
		L"(أو نزّل إضافات KPI من Render).\n"
		L"مع kbsasami.raira=1 يُكتب التفريغ إلى %TEMP%\\ogg_kbsasami\\.",
		L"Показывает клавиши OPNA (и регистры) для SASAMI .fpy / PMD / FMP.\n"
		L"PMD: полный dump. FMP: клавиши + PPZ (без регистров).\n"
		L"Один старый fmpmd.kpi не управляет PMD/FMP.\n"
		L"При запуске Plugins.zip тихо обновляется, если новее\n"
		L"(или скачайте KPI-плагины в Render).\n"
		L"При kbsasami.raira=1 dump пишется в %TEMP%\\ogg_kbsasami\\.",
		L"Zeigt OPNA-Tasten (und Register) fuer SASAMI .fpy / PMD / FMP.\n"
		L"PMD: voller Dump. FMP: Tasten + PPZ (keine Register).\n"
		L"Altes fmpmd.kpi allein steuert PMD/FMP nicht.\n"
		L"Beim Start wird Plugins.zip still aktualisiert, wenn neuer\n"
		L"(oder KPI-Plugins in Render laden).\n"
		L"Mit kbsasami.raira=1 Dump nach %TEMP%\\ogg_kbsasami\\.",
		L"Mostra teclas OPNA (e registradores) para SASAMI .fpy / PMD / FMP.\n"
		L"PMD: dump completo. FMP: teclas + PPZ (sem registradores).\n"
		L"So o antigo fmpmd.kpi nao aciona PMD/FMP.\n"
		L"Na inicializacao, Plugins.zip e atualizado em silencio se mais novo\n"
		L"(ou baixe plugins KPI no Render).\n"
		L"Com kbsasami.raira=1, dump em %TEMP%\\ogg_kbsasami\\.",
		L"Toont OPNA-toetsen (en registers) voor SASAMI .fpy / PMD / FMP.\n"
		L"PMD: volledige dump. FMP: toetsen + PPZ (geen registers).\n"
		L"Alleen oude fmpmd.kpi stuurt PMD/FMP niet aan.\n"
		L"Bij starten wordt Plugins.zip stil bijgewerkt als nieuwer\n"
		L"(of download KPI-plugins in Render).\n"
		L"Met kbsasami.raira=1 dump naar %TEMP%\\ogg_kbsasami\\.",
		L"Pokazuje klawisze OPNA (i rejestry) dla SASAMI .fpy / PMD / FMP.\n"
		L"PMD: pelny dump. FMP: klawisze + PPZ (bez rejestrow).\n"
		L"Sam stary fmpmd.kpi nie steruje PMD/FMP.\n"
		L"Przy starcie Plugins.zip jest cicho aktualizowany, jesli nowszy\n"
		L"(lub pobierz wtyczki KPI w Render).\n"
		L"Przy kbsasami.raira=1 dump do %TEMP%\\ogg_kbsasami\\.",
		L"SASAMI .fpy / PMD / FMP icin OPNA tuslarini (ve kayitlari) gosterir.\n"
		L"PMD: tam dump. FMP: tuslar + PPZ (kayit yok).\n"
		L"Yalniz eski fmpmd.kpi PMD/FMP'yi surmez.\n"
		L"Baslangicta Plugins.zip daha yeniyse sessizce guncellenir\n"
		L"(veya Render'dan KPI eklentilerini indirin).\n"
		L"kbsasami.raira=1 iken dump %TEMP%\\ogg_kbsasami\\ konumuna yazilir."));
}

BOOL CFmMonitorDlg::OnEraseBkgnd(CDC* pDC)
{
	(void)pDC;
	return TRUE;
}

void CFmMonitorDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1) {
		/* 移動/リサイズ後だけ間引いて保存（常時 Commit は避ける） */
		if (m_persistAge >= 0 && ++m_persistAge >= 12) {
			if (!m_userClosing && IsWindowVisible())
				savedata.fmmonwindow = 1;
			PersistGeom();
			m_persistAge = -1; /* 次の OnSize/OnMove まで休止 */
		}
		IdlePulse();
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

double CFmMonitorDlg::ApproxHzFromFnum(uint8_t a4, uint8_t a0)
{
	/* YM2608/OPNA: fo = (clock/144) * Fnum * 2^(Block-1) / 2^20
	   （= Fnum * clock / (144 * 2^(21-Block))）。
	   2^(20-Block) だと1オクターブ高く、普通の曲でも O9C などになる。 */
	int block = (a4 >> 3) & 7;
	int fnum = ((a4 & 7) << 8) | a0;
	if (fnum <= 0) return 0.0;
	/* PMD fm_block_calc 相当: fnum が [0x26a,0x4d4) を外れたら block±1 してやり直す。
	   範囲外のまま Hz 換算すると、シ↓がミ／レ帯に再マップされる。 */
	{
		int cx = block << 11;
		int ax = fnum;
		while (ax >= 0x26a) {
			if (ax < (0x26a * 2))
				break;
			cx += 0x800;
			if (cx != 0x4000) {
				ax -= 0x26a;
			} else {
				cx = 0x3800;
				if (ax >= 0x800)
					ax = 0x7ff;
				break;
			}
		}
		while (ax < 0x26a) {
			cx -= 0x800;
			if (cx >= 0) {
				ax += 0x26a;
			} else {
				cx = 0;
				if (ax < 8)
					ax = 8;
				break;
			}
		}
		block = (cx >> 11) & 7;
		fnum = ax & 0x7ff;
	}
	if (block <= 0)
		return (double)fnum * 7987200.0 / (144.0 * (double)(1u << 21));
	return (double)fnum * 7987200.0 / (144.0 * (double)(1u << (21 - block)));
}

int CFmMonitorDlg::ApproxMidiFromFnum(uint8_t a4, uint8_t a0)
{
	const double freq = ApproxHzFromFnum(a4, a0);
	if (freq < 8.0) return -1;
	int n = (int)(69.0 + 12.0 * log(freq / 440.0) / log(2.0) + 0.5);
	if (n < 0) n = 0;
	if (n > 127) n = 127;
	return n;
}

int CFmMonitorDlg::ApproxMidiFromSsg(uint16_t period)
{
	period &= 0x0FFF;
	if (period == 0) return -1;
	const double freq = 7987200.0 / (32.0 * (double)period);
	if (freq < 8.0) return -1;
	int n = (int)(69.0 + 12.0 * log(freq / 440.0) / log(2.0) + 0.5);
	if (n < 0) n = 0;
	if (n > 127) n = 127;
	return n;
}

/* 再生中のみ鍵盤・音名を出す。playy は停止後も 1 のままなので plf/ps/playf を見る */
static int FmMonIsLive()
{
	extern int plf;
	extern int playf;
	extern int ps;
	if (ps != 0) return 0;
	if (plf == 0 && playf == 0) return 0;
	return 1;
}

/* YM/SASAMI 流: MIDI60=O5C。音名は常に4文字（O5C / O5C#）で # 有無でも桁がずれない */
static void FmFormatNoteName(int midi, wchar_t* out, int outChars)
{
	if (!out || outChars < 5) return;
	if (midi < 0 || midi > 127) {
		wcsncpy_s(out, outChars, L"----", _TRUNCATE);
		return;
	}
	static const wchar_t* kName[12] = {
		L"C ", L"C#", L"D ", L"D#", L"E ", L"F ",
		L"F#", L"G ", L"G#", L"A ", L"A#", L"B "
	};
	_snwprintf_s(out, outChars, _TRUNCATE, L"O%d%s", midi / 12, kName[midi % 12]);
}

static int FmSsgNoisePeriod(const SasamiFmMonDump& d)
{
	return (int)(d.regs[6] & 0x1F);
}

static int FmSsgNoiseOn(const SasamiFmMonDump& d, int ch)
{
	if (ch < 0 || ch > 2) return 0;
	/* R7 bit3-5: 0=noise enable */
	return (((d.regs[7] >> (3 + ch)) & 1) == 0) ? 1 : 0;
}

int CFmMonitorDlg::PcmRows() const
{
	if (!m_haveDump) return 0;
	if (IsMsxDump()) {
		if (MsxDevMask() & SASAMI_FMMON_DEV_HES) {
			int n = m_dump.pcmCount;
			if (n < 3) n = 3;
			if (n > SASAMI_FMMON_PCM_MAX) n = SASAMI_FMMON_PCM_MAX;
			return n;
		}
		if (MsxDevMask() & SASAMI_FMMON_DEV_SCC) {
			int n = m_dump.pcmCount;
			if (n < 5) n = 5;
			if (n > SASAMI_FMMON_PCM_MAX) n = SASAMI_FMMON_PCM_MAX;
			return n;
		}
		return 0;
	}
	int n = m_dump.pcmCount;
	if (n < 0) n = 0;
	if (n > SASAMI_FMMON_PCM_MAX) n = SASAMI_FMMON_PCM_MAX;
	if (m_dump.version >= 6 && (m_dump.dumpFlags & SASAMI_FMMON_FLAG_PPZ) && n < 8)
		n = 8;
	if (m_dump.version >= 6
		&& (m_dump.dumpFlags & (SASAMI_FMMON_FLAG_ADPCM | SASAMI_FMMON_FLAG_PCM86))) {
		/* PPZ+ADPCM: ADPCM at pcm[8] → need 9 rows. Alone: just pcm[0]=ADPCM. */
		if (m_dump.dumpFlags & SASAMI_FMMON_FLAG_PPZ) {
			if (n < 9) n = 9;
		} else if (n < 1) {
			n = 1;
		}
	}
	/* MDX OPM + PDX */
	if (IsOpmDump() && n > 0)
		return n;
	if (IsOpmDump())
		return 0;
	/* OPL3 / DualOPL2: ch10-18 in pcm slots */
	if (IsOplDump() && ChipProfile() == SASAMI_FMMON_KEYS_OPL3)
		return 9;
	if (IsOplDump())
		return 0;
	return n;
}

int CFmMonitorDlg::ExRows() const
{
	if (!m_haveDump || m_dump.version < 6) return 0;
	if (IsYm2610Dump()) return 0; /* no FM3-EX split on YM2610 monitor */
	if (IsOpmDump() || ChipProfile() == SASAMI_FMMON_KEYS_MDX)
		return 2; /* OPM7-8 */
	if (IsOplDump())
		return 3; /* OPL7-9 (with FmRows 6 → 9ch) */
	if (IsMsxDump())
		return (MsxDevMask() & SASAMI_FMMON_DEV_OPLL) ? 3 : 0;
	if (KeysOnly()) return 0;
	for (int i = 0; i < 3; i++)
		if (m_dump.keyOnEx[i])
			return 3;
	/*
	 * FMP: WinFMP often leaves regs[0x27]=0x9F (multi-freq bits) even without
	 * G/H/I parts — trust KPI FLAG_FM3EX (extend / real GHI / sticky EX gate).
	 * PMD: FLAG or 0x27 CH3 effect/multi-freq (bits7-6).
	 */
	if (m_dump.dumpFlags & SASAMI_FMMON_FLAG_FMP)
		return (m_dump.dumpFlags & SASAMI_FMMON_FLAG_FM3EX) ? 3 : 0;
	/* Non-FMP: FLAG or CH3 multi-freq (0x27 bits7-6). Never use 0x27 for FMP. */
	if (m_dump.dumpFlags & SASAMI_FMMON_FLAG_FM3EX)
		return 3;
	if ((m_dump.regs[0x27] & 0xC0) != 0)
		return 3;
	return 0;
}

int CFmMonitorDlg::FmRows() const
{
	if (!m_haveDump) return 6;
	if (IsOpmDump() || ChipProfile() == SASAMI_FMMON_KEYS_MDX)
		return 6; /* OPM1-6; +ExRows=2 → 8 */
	if (IsOplDump())
		return 6; /* OPL1-6; +ExRows=3 → 9 */
	if (KeysOnly()) return 0;
	if (IsMsxDump())
		return (MsxDevMask() & SASAMI_FMMON_DEV_OPLL) ? 6 : 0;
	if (m_dump.padHit == 5) return 0; /* SN76489 */
	if (IsYm2610Dump()) return 4; /* YM2610 FM×4 (ch 1,2,4,5) */
	if (!m_dump.fm10 && m_dump.padHit == 1) return 3; /* OPN */
	return 6;
}

int CFmMonitorDlg::SsgRows() const
{
	if (!m_haveDump) return 3;
	if (IsOplDump() || KeysOnly()) return 0;
	if (IsOpmDump()) {
		/* X1 OPM+AY / dual: show SSG when gated or labeled AY */
		for (int i = 0; i < 3; i++)
			if (m_dump.ssgOn[i]) return 3;
		if (m_dump.titleSjis[0] && strstr(m_dump.titleSjis, "AY"))
			return 3;
		return 0;
	}
	if (IsMsxDump())
		return (MsxDevMask() & SASAMI_FMMON_DEV_PSG) ? 3 : 0;
	if (m_dump.padHit == 5) return 3; /* SN76489 tones */
	return 3;
}

int CFmMonitorDlg::KeysOnly() const
{
	return (m_haveDump && m_dump.version >= 6
		&& (m_dump.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY)) ? 1 : 0;
}

int CFmMonitorDlg::IsMsxDump() const
{
	return (m_haveDump && m_dump.version >= 6
		&& (m_dump.dumpFlags & SASAMI_FMMON_FLAG_MSX)) ? 1 : 0;
}

int CFmMonitorDlg::IsOpmDump() const
{
	return (m_haveDump && m_dump.version >= 6
		&& (m_dump.dumpFlags & SASAMI_FMMON_FLAG_OPM)) ? 1 : 0;
}

int CFmMonitorDlg::IsOplDump() const
{
	if (!m_haveDump || m_dump.version < 6) return 0;
	const unsigned p = (unsigned)m_dump.pad6[1];
	return (p == SASAMI_FMMON_KEYS_OPL2 || p == SASAMI_FMMON_KEYS_OPL3) ? 1 : 0;
}

int CFmMonitorDlg::IsYm2610Dump() const
{
	return (m_haveDump && m_dump.padHit == 6) ? 1 : 0;
}

int CFmMonitorDlg::IsArcadePcmDump() const
{
	if (!m_haveDump || m_dump.version < 6) return 0;
	return FmIsArcadePcmProfile((unsigned)m_dump.pad6[1]);
}

unsigned CFmMonitorDlg::MsxDevMask() const
{
	if (!IsMsxDump()) return 0;
	return (unsigned)m_dump.pad6[0];
}

unsigned CFmMonitorDlg::ChipProfile() const
{
	if (!m_haveDump || m_dump.version < 6) return SASAMI_FMMON_KEYS_GENERIC;
	if (IsOpmDump()) return SASAMI_FMMON_KEYS_MDX;
	if (IsOplDump()) return (unsigned)m_dump.pad6[1];
	if (KeysOnly() || IsMsxDump())
		return (unsigned)m_dump.pad6[1];
	/* Hybrid OPN + arcade PCM (e.g. SegaOut YM2203+SegaPCM). */
	if (FmIsArcadePcmProfile((unsigned)m_dump.pad6[1]))
		return (unsigned)m_dump.pad6[1];
	return SASAMI_FMMON_KEYS_GENERIC;
}

unsigned CFmMonitorDlg::ViewCaps() const
{
	if (!m_haveDump || m_dump.version < 6) return 0;
	unsigned c = (unsigned)m_dump.pad6[2];
	if (c) {
		/* UI can draw MSX regs/panels even if older KPI omitted caps */
		if (IsMsxDump()) {
			const unsigned m = MsxDevMask();
			if (m & (SASAMI_FMMON_DEV_PSG | SASAMI_FMMON_DEV_OPLL
				| SASAMI_FMMON_DEV_SCC | SASAMI_FMMON_DEV_HES))
				c |= SASAMI_FMMON_VIEW_REGS;
			if (m & SASAMI_FMMON_DEV_OPLL)
				c |= SASAMI_FMMON_VIEW_PANELS;
		}
		return c;
	}
	/* Legacy dumps without pad6[2] */
	if (IsOpmDump() && !(m_dump.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY))
		return SASAMI_FMMON_VIEW_KEYS | SASAMI_FMMON_VIEW_REGS | SASAMI_FMMON_VIEW_PANELS;
	if (IsOplDump())
		return SASAMI_FMMON_VIEW_KEYS | SASAMI_FMMON_VIEW_REGS | SASAMI_FMMON_VIEW_PANELS;
	if (IsArcadePcmDump()) {
		for (int i = 0; i < 0x200; i++)
			if (m_dump.regs[i] != 0)
				return SASAMI_FMMON_VIEW_KEYS | SASAMI_FMMON_VIEW_REGS | SASAMI_FMMON_VIEW_PANELS;
		return SASAMI_FMMON_VIEW_KEYS;
	}
	if (KeysOnly()) return SASAMI_FMMON_VIEW_KEYS;
	if (IsMsxDump()) {
		unsigned v = SASAMI_FMMON_VIEW_KEYS;
		const unsigned m = MsxDevMask();
		if (m & (SASAMI_FMMON_DEV_PSG | SASAMI_FMMON_DEV_OPLL
			| SASAMI_FMMON_DEV_SCC | SASAMI_FMMON_DEV_HES))
			v |= SASAMI_FMMON_VIEW_REGS;
		if (m & SASAMI_FMMON_DEV_OPLL)
			v |= SASAMI_FMMON_VIEW_PANELS;
		return v;
	}
	return SASAMI_FMMON_VIEW_KEYS | SASAMI_FMMON_VIEW_REGS | SASAMI_FMMON_VIEW_PANELS;
}

int CFmMonitorDlg::HideRhythm() const
{
	/* MSX FMPAC / OPLL: show rhythm keys when OPLL is present (reg $0E). */
	if (IsMsxDump()) {
		if (MsxDevMask() & SASAMI_FMMON_DEV_OPLL)
			return 0;
		return 1;
	}
	if (KeysOnly() || IsOpmDump() || IsYm2610Dump()) return 1;
	/* OPN / SN / non-OPNA: no ADPCM-A rhythm row */
	if (m_haveDump && !m_dump.fm10) return 1;
	return 0;
}

int CFmMonitorDlg::HasViewRegs() const
{
	return (ViewCaps() & SASAMI_FMMON_VIEW_REGS) ? 1 : 0;
}

int CFmMonitorDlg::HasViewPanels() const
{
	return (ViewCaps() & SASAMI_FMMON_VIEW_PANELS) ? 1 : 0;
}

int CFmMonitorDlg::PreferOpnaShell() const
{
	/* 停止中に最後の OPNA/OPM 等ダンプが残っている場合はそのまま */
	if (!m_haveDump)
		return 1;
	if (FmIsArcadePcmProfile(ChipProfile()))
		return 0;
	/* PC/AT BEEP/CMS/MPU: real aux regs — do not force empty OPNA shell. */
	if (KeysOnly() && HasViewRegs() && m_dump.titleSjis[0]
		&& strstr(m_dump.titleSjis, "PC/AT"))
		return 0;
	/* MIDI / SPC 等 keys-only で専用 hex・パネルが無い → OPNA をデフォルト殻に */
	if (KeysOnly() && !IsOpmDump() && !IsOplDump() && !IsMsxDump())
		return 1;
	return 0;
}

/* KPI(FPY) は MIDI モニタと同じ可聴補正を使う。
   OggGetHeardPcmFrames / GDI 時刻だけだとデコード側に近く、常に最新 dump を即表示 →
   スタンプやリングを直しても見た目が一切変わらない。mode==-3 は遅延補正が本体の正解。
   SASAMI/FMP 700 / PMD 550。 */
static int FmPlayLooksLikeFpy()
{
	return FmPlayPathHasExt(L".fpy2") || FmPlayPathHasExt(L".fpy") ? 1 : 0;
}

static int FmPlayLooksLikeFmp()
{
	return (FmPlayPathHasExt(L".opi") || FmPlayPathHasExt(L".ovi")
		|| FmPlayPathHasExt(L".ozi")) ? 1 : 0;
}

static int FmPlayLooksLikeKeysOnlyXsf()
{
	/* PSF/SPC/GSF/NCSF/SID/NSF keys-only。細分化後にモニタが僅かに先行しやすい */
	return (FmPlayPathHasExt(L".psf2") || FmPlayPathHasExt(L".minipsf2")
		|| FmPlayPathHasExt(L".psf") || FmPlayPathHasExt(L".minipsf")
		|| FmPlayPathHasExt(L".spc")
		|| FmPlayPathHasExt(L".gsf") || FmPlayPathHasExt(L".minigsf")
		|| FmPlayPathHasExt(L".ncsf") || FmPlayPathHasExt(L".sid")
		|| FmPlayPathHasExt(L".nsf") || FmPlayPathHasExt(L".nsfe")) ? 1 : 0;
}

static int FmPlayHeardLagMs()
{
	if (FmPlayLooksLikeFpy() || FmPlayLooksLikeFmp()) return 700;
	/* keys-only XSF 系: 600 だと表示が少し先行 → +150ms */
	if (FmPlayLooksLikeKeysOnlyXsf()) return 750;
	/* KSS: KPI/DS が大きな塊で playb が進むため GDI 追従だとかたまる。
	   dump 最新−ラグで追う前提の遅延（ホスト先行分）。 */
	if (FmPlayPathHasExt(L".kss")) return 600; /* 500 でも僅かに先行 → +100ms */
	if (FmPlayPathHasExt(L".zip")) return 600; /* CEmu hoot archive */
	if (FmPlayPathHasExt(L".s98") || FmPlayPathHasExt(L".vgm") || FmPlayPathHasExt(L".vgz")
		|| FmPlayPathHasExt(L".hes") || FmPlayPathHasExt(L".gym") || FmPlayPathHasExt(L".ssl")
		|| FmPlayPathHasExt(L".dro") || FmPlayPathHasExt(L".cym") || FmPlayPathHasExt(L".mym")
		|| FmPlayPathHasExt(L".mdx") || FmPlayPathHasExt(L".mdc")
		|| FmPlayPathHasExt(L".cmf") || FmPlayPathHasExt(L".laa")
		|| FmPlayPathHasExt(L".sc68") || FmPlayPathHasExt(L".sndh")
		|| FmPlayPathHasExt(L".mid") || FmPlayPathHasExt(L".midi")
		|| FmPlayLooksLikeMsx())
		return 600;
	return 550; /* PMD ほか */
}

/* dump 側 curSample で追う形式（巨大 KPI バッファ / keys-only 含む） */
static int FmPlayPreferDumpClock()
{
	if (FmPlayLooksLikeMsx()) return 1;
	/* CEmu hoot zip（arcus2.zip::0001 等）も dump.curSample で追う */
	if (FmPlayPathHasExt(L".zip")) return 1;
	if (FmPlayPathHasExt(L".kss") || FmPlayPathHasExt(L".hes")
		|| FmPlayPathHasExt(L".vgm") || FmPlayPathHasExt(L".vgz")
		|| FmPlayPathHasExt(L".gym") || FmPlayPathHasExt(L".ssl")
		|| FmPlayPathHasExt(L".s98")
		|| FmPlayPathHasExt(L".mdx") || FmPlayPathHasExt(L".mdc")
		|| FmPlayPathHasExt(L".cmf") || FmPlayPathHasExt(L".laa")
		|| FmPlayPathHasExt(L".sc68") || FmPlayPathHasExt(L".sndh")
		|| FmPlayPathHasExt(L".nsf") || FmPlayPathHasExt(L".nsfe")
		|| FmPlayPathHasExt(L".spc") || FmPlayPathHasExt(L".psf") || FmPlayPathHasExt(L".minipsf")
		|| FmPlayPathHasExt(L".psf2") || FmPlayPathHasExt(L".minipsf2")
		|| FmPlayPathHasExt(L".gsf") || FmPlayPathHasExt(L".minigsf")
		|| FmPlayPathHasExt(L".ncsf") || FmPlayPathHasExt(L".sid")
		|| FmPlayPathHasExt(L".mid") || FmPlayPathHasExt(L".midi"))
		return 1;
	return 0;
}

uint64_t CFmMonitorDlg::HeardSample(uint32_t sampleRate)
{
	extern int playy;
	extern int mode;
	extern int wavbit_sample_Hz;
	const uint32_t srDump = sampleRate > 0 ? sampleRate : 44100;

	__int64 frames = 0;
	/* 巨大 KPI バッファ / keys-only: GDI 跳びを避け dump curSample−ラグで追う。
	   CEmu (MODE_CEMU=-1000) の hoot zip も同様。 */
	const unsigned lastFlags = (m_histN > 0)
		? m_hist[(m_histHead + m_histN - 1) % HIST_MAX].dumpFlags : 0u;
	const int isKpiOrCemu = (mode == -3 || IsCemuMode(mode)) ? 1 : 0;
	const int useDumpClock = (isKpiOrCemu && m_histN > 0
		&& (FmPlayPreferDumpClock()
			|| (lastFlags & (SASAMI_FMMON_FLAG_MSX | SASAMI_FMMON_FLAG_KEYSONLY)))) ? 1 : 0;
	if (useDumpClock) {
		const int li = (m_histHead + m_histN - 1) % HIST_MAX;
		const uint64_t latest = m_histSamp[li];
		unsigned lagMs = (unsigned)FmPlayHeardLagMs();
		/* 拡張子判定漏れでも keys-only dump は同系の遅れを使う */
		if ((lastFlags & SASAMI_FMMON_FLAG_KEYSONLY) && lagMs < 750u)
			lagMs = 750u;
		const uint64_t lag = (uint64_t)srDump * lagMs / 1000u;
		frames = (latest > lag) ? (__int64)(latest - lag) : 0;
	} else if (mode == -3) {
		const double sec = OggGetGdiPlaybackTimeSec();
		frames = (__int64)(sec * (double)srDump + 0.5);
		const int lagMs = FmPlayHeardLagMs();
		frames -= (__int64)srDump * lagMs / 1000;
	} else {
		const int srSrc = (wavbit_sample_Hz > 0) ? wavbit_sample_Hz : (int)srDump;
		frames = OggGetHeardPcmFrames();
		if (frames < 0) frames = 0;
		if (srSrc != (int)srDump)
			frames = frames * (__int64)srDump / (__int64)srSrc;
	}
	if (frames < 0) frames = 0;
	uint64_t heard = (uint64_t)frames;

	if (playy == 0) {
		m_heardAnchor = heard;
		m_lastHeardSamp = heard;
		return heard;
	}
	/* シーク巻き戻し（大きく戻ったときだけ） */
	if (m_lastHeardSamp > 0 && heard + (uint64_t)(srDump / 5) < m_lastHeardSamp) {
		m_heardAnchor = heard;
		m_lastHeardSamp = heard;
		return heard;
	}
	/* GDI 時刻の微小揺らぎで dump が前後するとノートが交互に見える */
	if (heard < m_lastHeardSamp)
		heard = m_lastHeardSamp;
	m_heardAnchor = heard;
	m_lastHeardSamp = heard;
	return heard;
}

int CFmMonitorDlg::ContentHeight(int dpi, int pcmRows) const
{
	const int pad = FmScale(4, dpi);
	const int head = pad + FmScale(14, dpi);
	const int cellH = FmScale(9, dpi);
	const int bankGap = FmScale(10, dpi);
	const int hexH = 2 * (FmScale(18, dpi) + 16 * cellH) + bankGap;
	const int fmPanelH = (std::max)(hexH, FmScale(320, dpi));
	const int rowH = FmScale(14, dpi);
	const int chRows = FmRows() + ExRows() + SsgRows() + pcmRows + (HideRhythm() ? 0 : 1);
	const int keys = FmScale(4, dpi) + chRows * rowH;
	return head + fmPanelH + keys + pad;
}

int CFmMonitorDlg::PreferredWidth(int dpi) const
{
	const int pad = FmScale(4, dpi);
	const int cellW = FmScale(18, dpi); /* Consolas "00" + margin */
	const int gapExtra = FmScale(4, dpi);
	const int hexW = cellW + 16 * cellW + 4 * gapExtra + FmScale(8, dpi);
	const int fmW = FmScale(560, dpi);
	const int labelW = FmScale(58, dpi);
	const int pianoMin = FmScale(360, dpi);
	const int top = hexW + FmScale(6, dpi) + fmW;
	const int bot = labelW + pianoMin;
	return pad * 2 + (std::max)(top, bot);
}

void CFmMonitorDlg::RestoreGeom()
{
	const UINT dpi = FmUiDpi(m_hWnd ? m_hWnd : nullptr);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int clientW = PreferredWidth((int)dpi);
	const int clientH = ContentHeight((int)dpi, 0);
	/* クライアント→外枠 */
	CRect rc(0, 0, clientW, clientH + capH);
	AdjustWindowRectEx(&rc, GetStyle(), FALSE, GetExStyle());
	int prefW = rc.Width();
	int prefH = rc.Height();

	FmMonGeomFile side = {};
	const int hasSide = FmMonGeomLoad(&side);
	if (hasSide) {
		/* 位置・サイズのみ。開閉フラグは呼び出し側 / 起動時ロードが正 */
		if (side.w >= 280 && side.w <= 4000) savedata.fmmonw = side.w;
		if (side.h >= 180 && side.h <= 3000) savedata.fmmonh = side.h;
		if (side.x > -30000 && side.y > -30000) {
			savedata.fmmonx = side.x;
			savedata.fmmony = side.y;
		}
	}

	int x = savedata.fmmonx, y = savedata.fmmony;
	int w = savedata.fmmonw, h = savedata.fmmonh;
	if (w < 280 || w > 4000) w = prefW;
	if (h < 180 || h > 3000) h = prefH;
	if (x < -30000 || y < -30000) { x = 80; y = 80; }
	SetWindowPos(nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void CFmMonitorDlg::PersistGeom()
{
	if (!::IsWindow(GetSafeHwnd()) || IsIconic()) return;
	CRect rc;
	GetWindowRect(&rc);
	if (rc.Width() < 160 || rc.Height() < 120) return;
	savedata.fmmonx = rc.left;
	savedata.fmmony = rc.top;
	savedata.fmmonw = rc.Width();
	savedata.fmmonh = rc.Height();
	/* IsWindowVisible に頼らない（メニュー閉鎖時に開いたまま保存されるバグを防ぐ） */
	const int open = m_userClosing ? 0 : (savedata.fmmonwindow ? 1 : 0);
	savedata.fmmonwindow = open;
	FmMonGeomSave(open, savedata.fmmonx, savedata.fmmony, savedata.fmmonw, savedata.fmmonh);
}

void CFmMonitorDlg::DetachForDestroy()
{
	m_userClosing = 0;
	savedata.fmmonwindow = 1;
	PersistGeom();
	KillTimer(1);
	DatArc_InvalidateLeaf(L"oggYSEDbgmu.dat");
	OggPersistSaveDatNow();
}

void CFmMonitorDlg::OnClose()
{
	m_userClosing = 1;
	savedata.fmmonwindow = 0;
	PersistGeom();
	DatArc_InvalidateLeaf(L"oggYSEDbgmu.dat");
	OggPersistSaveDatNow();
	ShowWindow(SW_HIDE);
	DestroyWindow();
	extern CMediaPlayerDlg* mp;
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->SyncPushToggleButtons();
}

static int FmHexColX(int col, int cellW, int gapExtra)
{
	return col * cellW + (col / 4) * gapExtra;
}

static int FmHexRowIsFmOps(int row)
{
	/* 0x30..0x9F: 4-op パラメータ（3ch x 4op の並び） */
	return row >= 0x3 && row <= 0x9;
}

static int FmHexColInOpGroup(int col)
{
	return (col % 4) != 3;
}

static void FmFrameRect(CDC& dc, const CRect& rc, COLORREF penColor)
{
	/* Rectangle は現行ブラシで塗り潰す → 白抜けの元凶。枠のみ描く */
	CPen pen(PS_SOLID, 1, penColor);
	CPen* oldp = dc.SelectObject(&pen);
	HGDIOBJ oldb = ::SelectObject(dc.GetSafeHdc(), ::GetStockObject(NULL_BRUSH));
	dc.Rectangle(rc.left, rc.top, rc.right, rc.bottom);
	::SelectObject(dc.GetSafeHdc(), oldb);
	dc.SelectObject(oldp);
}

static HFONT s_fmFontCache[21]; /* px 8..20 */

static HFONT FmMakeFont(int px)
{
	if (px < 8) px = 8;
	if (px > 20) px = 20;
	if (!s_fmFontCache[px]) {
		s_fmFontCache[px] = ::CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
	}
	if (s_fmFontCache[px])
		return s_fmFontCache[px];
	return (HFONT)::GetStockObject(ANSI_FIXED_FONT);
}

static void FmReleaseFontCache()
{
	for (int i = 0; i < 21; i++) {
		if (s_fmFontCache[i]) {
			::DeleteObject(s_fmFontCache[i]);
			s_fmFontCache[i] = nullptr;
		}
	}
}

void CFmMonitorDlg::DrawHexBank(CDC& dc, int x, int y, int cellW, int cellH, int gapExtra, int bankBase, const wchar_t* title, int rowCount)
{
	if (rowCount < 1) rowCount = 1;
	if (rowCount > 16) rowCount = 16;
	const int fontPx = (std::max)(8, (std::min)(cellH - 2, cellW * 2 / 3));
	HFONT font = FmMakeFont(fontPx);
	HFONT oldf = (HFONT)dc.SelectObject(font);
	/* タイトルは col ヘッダの上・bankTitle 帯の中。cellH*2 上だと rcHead に食い込む */
	const int titleY = y - cellH - fontPx - 2;
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(200, 220, 210));
	dc.TextOut(x, titleY, title);

	wchar_t hdr[4];
	for (int col = 0; col < 16; col++) {
		_snwprintf_s(hdr, _TRUNCATE, L"%X", col);
		dc.SetTextColor(RGB(150, 175, 160));
		CSize hs = dc.GetTextExtent(hdr);
		dc.TextOut(x + FmHexColX(col, cellW, gapExtra) + (cellW - hs.cx) / 2, y - cellH, hdr);
	}

	const COLORREF baseDark = RGB(24, 28, 32);
	const COLORREF baseGroup = RGB(36, 52, 44);   /* 薄緑系グループ */
	const COLORREF baseTouched = RGB(48, 72, 58);
	const COLORREF hi = RGB(80, 200, 120);

	for (int row = 0; row < rowCount; row++) {
		_snwprintf_s(hdr, _TRUNCATE, L"%X", row);
		dc.SetTextColor(RGB(160, 180, 170));
		dc.SetBkMode(TRANSPARENT);
		dc.TextOut(x - cellW + 1, y + row * cellH + (cellH - fontPx) / 2, hdr);

		const int opsRow = FmHexRowIsFmOps(row);
		for (int col = 0; col < 16; col++) {
			const int idx = bankBase + row * 16 + col;
			if (idx < 0 || idx >= 0x200) continue;
			const int px = x + FmHexColX(col, cellW, gapExtra);
			const int py = y + row * cellH;
			const int inGroup = FmHexColInOpGroup(col);
			COLORREF base = baseDark;
			if (inGroup && opsRow)
				base = m_touched[idx] ? baseTouched : baseGroup;
			else if (inGroup && (m_touched[idx] || (m_haveDump && m_dump.regs[idx] != 0)))
				base = m_touched[idx] ? baseTouched : baseGroup;
			else if (m_touched[idx])
				base = baseTouched;

			const COLORREF cellBg = FmMixFade(base, hi, m_fade[idx]);
			dc.FillSolidRect(px, py, cellW - 1, cellH - 1, cellBg);
			wchar_t t[4];
			if (m_haveDump)
				_snwprintf_s(t, _TRUNCATE, L"%02X", m_dump.regs[idx]);
			else
				wcscpy_s(t, L"--");
			/* OPAQUE+base だとフェード塗りを文字背景で潰し縁だけ緑に見える */
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(m_touched[idx] ? RGB(240, 250, 245) : RGB(130, 145, 138));
			CSize ts = dc.GetTextExtent(t);
			dc.TextOut(px + (cellW - ts.cx) / 2, py + (cellH - ts.cy) / 2, t);
		}
	}
	dc.SelectObject(oldf);
}

static void FmDeleteFont(HFONT)
{
	/* フォントはキャッシュ — 破棄しない */
}

/* セル内ノブ。背景は呼び出し側が塗る。ラベルは円の下。 */
static void FmDrawKnob(CDC& dc, int cx, int cy, int r, int val, int vmax, const wchar_t* name, COLORREF back)
{
	if (r < 5) r = 5;
	if (vmax < 1) vmax = 1;
	if (val < 0) val = 0;
	if (val > vmax) val = vmax;

	CBrush fill(RGB(36, 52, 44));
	CBrush* oldBr = dc.SelectObject(&fill);
	CPen pen(PS_SOLID, 1, RGB(140, 190, 160));
	CPen* oldp = dc.SelectObject(&pen);
	dc.Ellipse(cx - r, cy - r, cx + r + 1, cy + r + 1);
	dc.SelectObject(oldBr);

	const double a0 = 3.1415926535 * 0.75;
	const double a1 = 3.1415926535 * 2.25;
	const double t = (double)val / (double)vmax;
	const double ang = a0 + (a1 - a0) * t;
	const int x1 = cx + (int)(cos(ang) * (r - 2));
	const int y1 = cy - (int)(sin(ang) * (r - 2));
	CPen needle(PS_SOLID, (std::max)(1, r / 5), RGB(255, 255, 255));
	dc.SelectObject(&needle);
	dc.MoveTo(cx, cy);
	dc.LineTo(x1, y1);
	dc.SelectObject(oldp);

	const int fontPx = (std::max)(8, (std::min)(r - 1, 14));
	HFONT font = FmMakeFont(fontPx);
	HFONT oldf = (HFONT)dc.SelectObject(font);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(245, 250, 245));
	wchar_t vs[8];
	_snwprintf_s(vs, _TRUNCATE, L"%d", val);
	CSize vsZ = dc.GetTextExtent(vs);
	dc.TextOut(cx - vsZ.cx / 2, cy - vsZ.cy / 2, vs);

	const int namePx = (std::max)(8, (std::min)(fontPx, r * 2 / 3 + 2));
	HFONT nf = FmMakeFont(namePx);
	dc.SelectObject(nf);
	dc.SetTextColor(RGB(190, 220, 200));
	CSize ns = dc.GetTextExtent(name);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(back);
	dc.TextOut(cx - ns.cx / 2, cy + r + 1, name);
	dc.SelectObject(oldf);
	FmDeleteFont(font);
	FmDeleteFont(nf);
}

/* 狭いとき用: 上に名前、下に値の矩形セル（重ならないよう cell 内に収める） */
static void FmDrawParamCell(CDC& dc, const CRect& cell, int val, int /*vmax*/, const wchar_t* name, COLORREF back)
{
	if (cell.Width() < 8 || cell.Height() < 12) return;
	dc.FillSolidRect(cell, back);
	FmFrameRect(dc, cell, RGB(60, 90, 72));

	const int namePx = (std::max)(8, (std::min)(11, cell.Height() / 3));
	const int valPx = (std::max)(9, (std::min)(16, cell.Height() * 2 / 5));
	HFONT nf = FmMakeFont(namePx);
	HFONT vf = FmMakeFont(valPx);
	HFONT oldf = (HFONT)dc.SelectObject(nf);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(back);
	dc.SetTextColor(RGB(160, 200, 175));
	CSize ns = dc.GetTextExtent(name);
	dc.TextOut(cell.left + (cell.Width() - ns.cx) / 2, cell.top + 1, name);

	dc.SelectObject(vf);
	dc.SetTextColor(RGB(235, 250, 240));
	wchar_t vs[8];
	_snwprintf_s(vs, _TRUNCATE, L"%d", val);
	CSize vsZ = dc.GetTextExtent(vs);
	dc.TextOut(cell.left + (cell.Width() - vsZ.cx) / 2,
		cell.top + namePx + (cell.Height() - namePx - vsZ.cy) / 2, vs);
	dc.SelectObject(oldf);
	FmDeleteFont(nf);
	FmDeleteFont(vf);
}

/* セル幅に応じてノブ or 矩形。必ず cell 内に描く */
static void FmDrawParamInCell(CDC& dc, const CRect& cell, int val, int vmax, const wchar_t* name, COLORREF back)
{
	if (cell.Width() < 10 || cell.Height() < 14) return;
	const int labelReserve = (std::max)(9, cell.Height() / 4);
	const int r = (std::min)((cell.Width() - 2) / 2, (cell.Height() - labelReserve - 2) / 2);
	if (r >= 7) {
		dc.FillSolidRect(cell, back);
		const int cx = cell.left + cell.Width() / 2;
		const int cy = cell.top + r + 1;
		FmDrawKnob(dc, cx, cy, r, val, vmax, name, back);
	} else {
		FmDrawParamCell(dc, cell, val, vmax, name, back);
	}
}

/* YM2608 / OPN 系 ALGO 0..7 */
static void FmDrawAlgo(CDC& dc, const CRect& rc, int alg, int fontPx)
{
	const COLORREF bg = RGB(28, 44, 36);
	dc.FillSolidRect(rc, bg);
	FmFrameRect(dc, rc, RGB(90, 150, 120));
	HFONT font = FmMakeFont(fontPx);
	HFONT oldf = (HFONT)dc.SelectObject(font);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(bg);
	dc.SetTextColor(RGB(180, 230, 200));
	wchar_t at[16];
	_snwprintf_s(at, _TRUNCATE, L"ALGO %d", alg & 7);
	dc.TextOut(rc.left + 3, rc.top + 1, at);

	const int margin = 6;
	const int top = rc.top + fontPx + 3;
	const int bot = rc.bottom - 4;
	const int left = rc.left + margin;
	const int right = rc.right - margin;
	const int bw = (std::max)(14, (right - left - 20) / 4);
	const int bh = (std::max)(10, (bot - top - 10) / 3);
	CRect box[4];

	auto place = [&](int i, int col, int row, int cols, int rows) {
		const int cellW = (right - left) / (std::max)(1, cols);
		const int cellH = (bot - top) / (std::max)(1, rows);
		const int cx = left + col * cellW + cellW / 2;
		const int cy = top + row * cellH + cellH / 2;
		box[i].SetRect(cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2);
	};

	alg &= 7;
	switch (alg) {
	case 0: place(0, 0, 1, 4, 3); place(1, 1, 1, 4, 3); place(2, 2, 1, 4, 3); place(3, 3, 1, 4, 3); break;
	case 1: place(0, 0, 0, 3, 3); place(1, 0, 2, 3, 3); place(2, 1, 1, 3, 3); place(3, 2, 1, 3, 3); break;
	case 2: place(0, 0, 0, 3, 3); place(1, 0, 2, 3, 3); place(2, 1, 2, 3, 3); place(3, 2, 1, 3, 3); break;
	case 3: place(0, 0, 0, 3, 3); place(1, 1, 0, 3, 3); place(2, 1, 2, 3, 3); place(3, 2, 1, 3, 3); break;
	case 4: place(0, 0, 0, 2, 2); place(1, 1, 0, 2, 2); place(2, 0, 1, 2, 2); place(3, 1, 1, 2, 2); break;
	case 5: place(0, 0, 1, 2, 3); place(1, 1, 0, 2, 3); place(2, 1, 1, 2, 3); place(3, 1, 2, 2, 3); break;
	case 6: place(0, 0, 0, 2, 3); place(1, 1, 0, 2, 3); place(2, 1, 1, 2, 3); place(3, 1, 2, 2, 3); break;
	default: /* 7: 全部キャリア */ place(0, 0, 1, 4, 3); place(1, 1, 1, 4, 3); place(2, 2, 1, 4, 3); place(3, 3, 1, 4, 3); break;
	}

	CPen wire(PS_SOLID, 1, RGB(140, 210, 170));
	CPen* oldp = dc.SelectObject(&wire);
	auto wireTo = [&](int a, int b) {
		const POINT pa = { box[a].CenterPoint().x, box[a].CenterPoint().y };
		const POINT pb = { box[b].CenterPoint().x, box[b].CenterPoint().y };
		const int ax = (pa.x < pb.x) ? box[a].right : ((pa.x > pb.x) ? box[a].left : pa.x);
		const int ay = (pa.y < pb.y) ? box[a].bottom : ((pa.y > pb.y) ? box[a].top : pa.y);
		const int bx = (pb.x < pa.x) ? box[b].right : ((pb.x > pa.x) ? box[b].left : pb.x);
		const int by = (pb.y < pa.y) ? box[b].bottom : ((pb.y > pa.y) ? box[b].top : pb.y);
		dc.MoveTo(ax, ay);
		if (ax != bx && ay != by) { dc.LineTo(bx, ay); dc.LineTo(bx, by); }
		else dc.LineTo(bx, by);
	};
	switch (alg) {
	case 0: wireTo(0, 1); wireTo(1, 2); wireTo(2, 3); break;
	case 1: wireTo(0, 2); wireTo(1, 2); wireTo(2, 3); break;
	case 2: wireTo(1, 2); wireTo(2, 3); wireTo(0, 3); break;
	case 3: wireTo(0, 1); wireTo(1, 3); wireTo(2, 3); break;
	case 4: wireTo(0, 1); wireTo(2, 3); break;
	case 5: wireTo(0, 1); wireTo(0, 2); wireTo(0, 3); break;
	case 6: wireTo(0, 1); break;
	default: break;
	}
	dc.SelectObject(oldp);

	for (int i = 0; i < 4; i++) {
		dc.FillSolidRect(box[i], RGB(44, 68, 54));
		FmFrameRect(dc, box[i], RGB(120, 180, 140));
		wchar_t s[8];
		_snwprintf_s(s, _TRUNCATE, L"S%d", i + 1);
		dc.SetBkColor(RGB(44, 68, 54));
		dc.SetTextColor(RGB(240, 250, 245));
		CSize sz = dc.GetTextExtent(s);
		dc.TextOut(box[i].left + (box[i].Width() - sz.cx) / 2,
			box[i].top + (box[i].Height() - sz.cy) / 2, s);
	}
	{
		CPen fb(PS_SOLID, 1, RGB(255, 190, 100));
		CPen* op = dc.SelectObject(&fb);
		const int cx = box[0].left;
		dc.Arc(cx - 10, box[0].top - 7, cx + 8, box[0].bottom + 7,
			cx, box[0].top, cx, box[0].bottom);
		dc.MoveTo(cx - 1, box[0].top);
		dc.LineTo(cx - 4, box[0].top - 4);
		dc.SelectObject(op);
	}
	dc.SelectObject(oldf);
	FmDeleteFont(font);
}

static void FmDrawEnvelope(CDC& dc, const CRect& rc, int ar, int dr, int sr, int rr, int sl, int tl)
{
	dc.FillSolidRect(rc, RGB(18, 26, 22));
	FmFrameRect(dc, rc, RGB(70, 100, 80));
	const int x0 = rc.left + 2;
	const int x1 = rc.right - 2;
	const int y0 = rc.top + 2;
	const int y1 = rc.bottom - 2;
	const int w = (std::max)(8, x1 - x0);
	const int h = (std::max)(8, y1 - y0);
	/* TL=減衰。表示は上が大音量 */
	auto Y = [&](int atten) {
		if (atten < 0) atten = 0;
		if (atten > 127) atten = 127;
		return y0 + atten * h / 127;
	};
	const int peak = tl & 127;
	const int sus = peak + (127 - peak) * (sl & 15) / 15;
	int px = x0;
	POINT pts[5];
	pts[0] = { px, y1 };
	px += 2 + (31 - (ar & 31)) * w / 80; if (px > x1) px = x1;
	pts[1] = { px, Y(peak) };
	px += 2 + (31 - (dr & 31)) * w / 90; if (px > x1) px = x1;
	pts[2] = { px, Y(sus) };
	px += 2 + (31 - (sr & 31)) * w / 70; if (px > x1) px = x1;
	pts[3] = { px, Y(sus) };
	px += 2 + (15 - (rr & 15)) * w / 40; if (px > x1) px = x1;
	pts[4] = { px, y1 };
	CPen env(PS_SOLID, 1, RGB(120, 220, 170));
	CPen* oldp = dc.SelectObject(&env);
	dc.MoveTo(pts[0]);
	for (int i = 1; i < 5; i++) dc.LineTo(pts[i]);
	dc.SelectObject(oldp);
}

void CFmMonitorDlg::DrawFmChPanel(CDC& dc, const CRect& rc, int ch)
{
	if (rc.Width() < 100 || rc.Height() < 80) return;
	int bank, slot;
	if (IsYm2610Dump() && ch >= 0 && ch < 4) {
		static const int kB[4] = { 0, 0, 0x100, 0x100 };
		static const int kS[4] = { 1, 2, 0, 1 };
		bank = kB[ch];
		slot = kS[ch];
	} else {
		bank = (ch < 3) ? 0 : 0x100;
		slot = (ch < 3) ? ch : (ch - 3);
	}
	auto reg = [&](int r) -> uint8_t {
		return m_haveDump ? m_dump.regs[bank + r] : 0;
	};

	const int savedDC = dc.SaveDC();
	dc.IntersectClipRect(rc);

	const COLORREF headBg = RGB(40, 64, 52);
	const COLORREF bodyBg = RGB(28, 36, 40);
	const COLORREF rowBg = RGB(32, 42, 38);
	const int pad = (std::max)(3, rc.Width() / 90);
	/* ヘッダは高さの 30%、最低 72 */
	const int headH = (std::max)(72, rc.Height() * 30 / 100);
	dc.FillSolidRect(rc.left, rc.top, rc.Width(), headH, headBg);
	dc.FillSolidRect(rc.left, rc.top + headH, rc.Width(), rc.Height() - headH, bodyBg);
	FmFrameRect(dc, rc, RGB(100, 160, 130));

	const uint8_t b0 = reg(0xB0 + slot);
	const uint8_t b4 = reg(0xB4 + slot);
	const uint8_t a4 = reg(0xA4 + slot);
	const uint8_t a0 = reg(0xA0 + slot);
	const int alg = b0 & 7;
	const int fb = (b0 >> 3) & 7;
	const int panL = (b4 >> 7) & 1;
	const int panR = (b4 >> 6) & 1;
	const int ams = (b4 >> 4) & 3;
	const int pms = b4 & 7;
	const int pan = (panL && panR) ? 3 : (panL ? 1 : (panR ? 2 : 0));
	const int midi = ApproxMidiFromFnum(a4, a0);
	const double hz = ApproxHzFromFnum(a4, a0);
	extern int playy;
	const int keyed = FmMonIsLive() && m_haveDump && m_dump.keyOnFm[ch];

	/* ---- ヘッダ: 左 ALGO / 中央ノブ / 右 NOTE ---- */
	const int titlePx = (std::max)(11, headH / 8);
	HFONT titleFont = FmMakeFont(titlePx);
	HFONT oldf = (HFONT)dc.SelectObject(titleFont);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(headBg);
	dc.SetTextColor(RGB(220, 245, 230));
	wchar_t title[24];
	_snwprintf_s(title, _TRUNCATE, L"FM CH%d", ch + 1);
	dc.TextOut(rc.left + pad, rc.top + 2, title);

	const int headInnerTop = rc.top + titlePx + 4;
	const int headInnerBot = rc.top + headH - pad;
	const int headInnerH = (std::max)(40, headInnerBot - headInnerTop);
	const int innerW = rc.Width() - pad * 2;
	/* 比率: algo 36% / knobs 34% / info 30% */
	const int algoW = innerW * 36 / 100;
	const int infoW = innerW * 30 / 100;
	const int knobBandW = innerW - algoW - infoW - pad * 2;

	CRect algo(rc.left + pad, headInnerTop, rc.left + pad + algoW, headInnerBot);
	FmDrawAlgo(dc, algo, alg, (std::max)(9, titlePx - 1));

	CRect knobsRc(algo.right + pad, headInnerTop, algo.right + pad + knobBandW, headInnerBot);
	{
		const int cellW = knobsRc.Width() / 2;
		const int cellH = knobsRc.Height() / 2;
		const struct { int v; int vmax; const wchar_t* n; } k4[4] = {
			{ ams, 3, L"AMS" }, { pms, 7, L"PMS" }, { fb, 7, L"FB" }, { pan, 3, L"PAN" }
		};
		for (int i = 0; i < 4; i++) {
			CRect c(
				knobsRc.left + (i % 2) * cellW + 1,
				knobsRc.top + (i / 2) * cellH + 1,
				knobsRc.left + (i % 2) * cellW + cellW - 1,
				knobsRc.top + (i / 2) * cellH + cellH - 1);
			FmDrawParamInCell(dc, c, k4[i].v, k4[i].vmax, k4[i].n, headBg);
		}
	}

	CRect infoRc(rc.right - pad - infoW, headInnerTop, rc.right - pad, headInnerBot);
	{
		const int infoPx = (std::max)(10, (std::min)(14, infoRc.Height() / 5));
		HFONT infoFont = FmMakeFont(infoPx);
		dc.SelectObject(infoFont);
		dc.SetBkColor(headBg);
		dc.SetTextColor(RGB(230, 245, 235));
		wchar_t line[64];
		int iy = infoRc.top + 2;
		if (midi >= 0) _snwprintf_s(line, _TRUNCATE, L"NOTE: %d", midi);
		else wcscpy_s(line, L"NOTE: --");
		dc.TextOut(infoRc.left + 2, iy, line);
		iy += infoPx + 2;
		_snwprintf_s(line, _TRUNCATE, L"%.0f Hz", hz);
		dc.TextOut(infoRc.left + 2, iy, line);
		iy += infoPx + 4;
		dc.TextOut(infoRc.left + 2, iy, L"SLOT");
		iy += infoPx + 2;
		const int box = (std::max)(12, (std::min)(infoPx + 2, (infoRc.Width() - 8) / 4 - 2));
		for (int s = 0; s < 4; s++) {
			const int sx = infoRc.left + 2 + s * (box + 2);
			dc.FillSolidRect(sx, iy, box, box, keyed ? RGB(70, 200, 120) : RGB(40, 50, 44));
			FmFrameRect(dc, CRect(sx, iy, sx + box, iy + box), RGB(90, 140, 110));
			wchar_t sn[4];
			_snwprintf_s(sn, _TRUNCATE, L"%d", s + 1);
			dc.SetTextColor(RGB(245, 250, 245));
			CSize sz = dc.GetTextExtent(sn);
			dc.TextOut(sx + (box - sz.cx) / 2, iy + (box - sz.cy) / 2, sn);
		}
		FmDeleteFont(infoFont);
	}

	/* ---- 本体: 4オペレータ行 ---- */
	const int bodyTop = rc.top + headH + pad;
	const int bodyBot = rc.bottom - pad;
	const int slotH = (bodyBot - bodyTop) / 4;
	if (slotH < 24) {
		dc.SelectObject(oldf);
		FmDeleteFont(titleFont);
		dc.RestoreDC(savedDC);
		return;
	}

	for (int op = 0; op < 4; op++) {
		const int yy = bodyTop + op * slotH;
		CRect row(rc.left + pad, yy, rc.right - pad, yy + slotH - 2);
		dc.FillSolidRect(row, rowBg);

		const uint8_t dtMul = reg(0x30 + op * 4 + slot);
		const uint8_t tl = reg(0x40 + op * 4 + slot);
		const uint8_t ksAr = reg(0x50 + op * 4 + slot);
		const uint8_t amDr = reg(0x60 + op * 4 + slot);
		const uint8_t srV = reg(0x70 + op * 4 + slot);
		const uint8_t slRr = reg(0x80 + op * 4 + slot);
		const uint8_t ssg = reg(0x90 + op * 4 + slot);
		const int mul = dtMul & 0x0F;
		const int dt1 = (dtMul >> 4) & 0x07;
		const int ar = ksAr & 0x1F;
		const int dr = amDr & 0x1F;
		const int am = (amDr >> 7) & 1;
		const int srate = srV & 0x1F;
		const int sl = (slRr >> 4) & 0x0F;
		const int rr = slRr & 0x0F;
		const int tl7 = tl & 0x7F;

		/* 列: [S#][env][params 6列][AM/EG] — 幅から逆算 */
		const int labW = (std::max)(16, (std::min)(28, row.Width() / 18));
		const int flagW = (std::max)(28, (std::min)(44, row.Width() / 10));
		const int envW = (std::max)(36, (std::min)(72, row.Width() / 6));
		const int paramLeft = row.left + labW + 2;
		const int envRight = paramLeft + envW;
		const int flagLeft = row.right - flagW;
		const int paramRight = flagLeft - 2;
		const int paramW = (std::max)(48, paramRight - envRight - 2);

		const int labPx = (std::max)(10, (std::min)(14, row.Height() / 3));
		HFONT labFont = FmMakeFont(labPx);
		dc.SelectObject(labFont);
		dc.SetBkMode(OPAQUE);
		dc.SetBkColor(rowBg);
		dc.SetTextColor(RGB(170, 220, 190));
		wchar_t sn[8];
		_snwprintf_s(sn, _TRUNCATE, L"S%d", op + 1);
		CSize snZ = dc.GetTextExtent(sn);
		dc.TextOut(row.left + (labW - snZ.cx) / 2, row.top + (row.Height() - snZ.cy) / 2, sn);

		CRect env(paramLeft, row.top + 2, envRight, row.bottom - 2);
		FmDrawEnvelope(dc, env, ar, dr, srate, rr, sl, tl7);

		/* パラメータ格子: 上段6 (AR..TL)、下段2 (MUL DT)。セル幅=paramW/6 */
		const int cols = 6;
		const int cellW = paramW / cols;
		const int gapY = 1;
		const int topRowH = row.Height() * 58 / 100;
		const int botRowH = row.Height() - topRowH - gapY;
		CRect band(envRight + 2, row.top + 1, envRight + 2 + cellW * cols, row.bottom - 1);

		const struct { int v; int vmax; const wchar_t* n; } topP[6] = {
			{ ar, 31, L"AR" }, { dr, 31, L"DR" }, { srate, 31, L"SR" },
			{ rr, 15, L"RR" }, { sl, 15, L"SL" }, { tl7, 127, L"TL" }
		};
		for (int i = 0; i < 6; i++) {
			CRect c(band.left + i * cellW, band.top, band.left + (i + 1) * cellW - 1, band.top + topRowH);
			FmDrawParamInCell(dc, c, topP[i].v, topP[i].vmax, topP[i].n, rowBg);
		}
		if (botRowH >= 16) {
			const struct { int v; int vmax; const wchar_t* n; } botP[2] = {
				{ mul, 15, L"MUL" }, { dt1, 7, L"DT" }
			};
			for (int i = 0; i < 2; i++) {
				CRect c(band.left + i * cellW, band.top + topRowH + gapY,
					band.left + (i + 1) * cellW - 1, band.bottom);
				FmDrawParamInCell(dc, c, botP[i].v, botP[i].vmax, botP[i].n, rowBg);
			}
		}

		/* AM / EG ランプ（専用列・他と非重複） */
		{
			const int half = flagW / 2;
			const int lampPx = (std::max)(8, (std::min)(11, row.Height() / 4));
			HFONT lf = FmMakeFont(lampPx);
			dc.SelectObject(lf);
			dc.SetBkColor(rowBg);
			dc.SetTextColor(RGB(190, 220, 200));
			dc.TextOut(flagLeft + 1, row.top + 1, L"AM");
			const int lamp = (std::max)(8, (std::min)(half - 4, row.Height() / 3));
			dc.FillSolidRect(flagLeft + 2, row.top + lampPx + 2, lamp, lamp,
				am ? RGB(80, 200, 140) : RGB(40, 48, 44));
			FmFrameRect(dc, CRect(flagLeft + 2, row.top + lampPx + 2, flagLeft + 2 + lamp, row.top + lampPx + 2 + lamp),
				RGB(90, 130, 100));

			dc.TextOut(flagLeft + half, row.top + 1, L"EG");
			dc.FillSolidRect(flagLeft + half, row.top + lampPx + 2, lamp + 2, (std::max)(6, lamp / 2),
				(ssg & 0x08) ? RGB(200, 170, 80) : RGB(40, 48, 44));
			FmDeleteFont(lf);
		}

		FmDeleteFont(labFont);
	}

	dc.SelectObject(oldf);
	FmDeleteFont(titleFont);
	dc.RestoreDC(savedDC);
}
void CFmMonitorDlg::DrawPiano108(CDC& dc, const CRect& rc, int midiNote, int lit)
{
	if (rc.Width() < 40 || rc.Height() < 8) return;
	const int k0 = 21, k1 = 108;
	int note = midiNote;
	if (note < k0 || note > k1) note = -1;
	int whites = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m != 1 && m != 3 && m != 6 && m != 8 && m != 10) whites++;
	}
	if (whites < 1) return;
	const int ww = rc.Width();
	const int hh = rc.Height();
	const COLORREF keyW = RGB(228, 228, 232);
	const COLORREF keyB = RGB(22, 24, 28);
	const COLORREF litW = RGB(220, 40, 40);
	const COLORREF litB = RGB(255, 70, 70);
	const COLORREF gap = RGB(12, 14, 18);

	dc.FillSolidRect(rc, gap);

	int wi = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m == 1 || m == 3 || m == 6 || m == 8 || m == 10) continue;
		const int x0 = rc.left + wi * ww / whites;
		const int x1 = rc.left + (wi + 1) * ww / whites;
		const int kw = (std::max)(1, x1 - x0 - 1);
		COLORREF c = keyW;
		if (lit && note == n) c = litW;
		dc.FillSolidRect(x0, rc.top, kw, hh, c);
		wi++;
	}
	wi = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m != 1 && m != 3 && m != 6 && m != 8 && m != 10) { wi++; continue; }
		const int xw = rc.left + (wi * ww / whites);
		const int bw = (std::max)(2, ww / whites * 55 / 100);
		const int x0 = xw - bw / 2;
		COLORREF c = keyB;
		if (lit && note == n) c = litB;
		dc.FillSolidRect(x0, rc.top, bw, hh * 62 / 100, c);
	}
}

void CFmMonitorDlg::DrawChannelKeys(CDC& dc, int x, int y, int w, int rowH, int keyH, int labelW)
{
	const int live = FmMonIsLive();
	const int fontPx = (std::max)(10, (std::min)(14, keyH - 1));
	HFONT labFont = FmMakeFont(fontPx);
	HFONT oldf = (HFONT)dc.SelectObject(labFont);
	dc.SetBkMode(TRANSPARENT);
	int row = 0;
	/* 等幅: "SSG1 O5C# N031" — # と N 桁で揺れないよう固定幅名 */
	const int lampProbe = (keyH > 4) ? (keyH * 3 / 4) : 8;
	const int needW = lampProbe + 4
		+ dc.GetTextExtent(L"SSG1 O5C# N031").cx
		+ FmScale(10, FmUiDpi(GetSafeHwnd()));
	if (labelW < needW) labelW = needW;
	int pianoW = w - labelW;
	if (pianoW < 80) pianoW = (w > labelW) ? (w - labelW) : 80;

	auto drawLabel = [&](int yy, const wchar_t* text, BYTE fade, COLORREF hi) {
		const int lamp = (keyH > 4) ? (keyH * 3 / 4) : 8;
		FmFillFade(dc, x, yy + (rowH - lamp) / 2, lamp, lamp,
			RGB(40, 44, 50), hi, fade);
		dc.SetTextColor(RGB(210, 215, 220));
		dc.TextOut(x + lamp + 4, yy + (rowH - keyH) / 2, text);
	};

	static const wchar_t* kFmName[6] = { L"FM1", L"FM2", L"FM3", L"FM4", L"FM5", L"FM6" };
	static const wchar_t* kOplName[6] = { L"OPL1", L"OPL2", L"OPL3", L"OPL4", L"OPL5", L"OPL6" };
	static const wchar_t* kOpmName[8] = {
		L"OPM1", L"OPM2", L"OPM3", L"OPM4", L"OPM5", L"OPM6", L"OPM7", L"OPM8"
	};
	static const wchar_t* kExName[3] = { L"EX1", L"EX2", L"EX3" };
	static const wchar_t* kOplEx[3] = { L"OPL7", L"OPL8", L"OPL9" };
	const int fmN = FmRows();
	const int exN = ExRows();
	const int msx = IsMsxDump();
	const int opm = IsOpmDump() || (ChipProfile() == SASAMI_FMMON_KEYS_MDX);
	const int opl = IsOplDump();
	/* OPNA: EX is FM3 split → nest between FM3 and FM4. OPM/MSX/OPL keep ch7+ after. */
	const int nestEx = (!opm && !msx && !opl && exN > 0 && fmN >= 6) ? 1 : 0;

	auto drawFmCh = [&](int ch) {
		const int yy = y + row * rowH;
		int bank, slot;
		if (IsYm2610Dump() && ch >= 0 && ch < 4) {
			static const int kB[4] = { 0, 0, 0x100, 0x100 };
			static const int kS[4] = { 1, 2, 0, 1 };
			bank = kB[ch];
			slot = kS[ch];
		} else {
			bank = (ch < 3) ? 0 : 0x100;
			slot = (ch < 3) ? ch : (ch - 3);
		}
		const uint8_t a4 = m_haveDump ? m_dump.regs[bank + 0xA4 + slot] : 0;
		const uint8_t a0 = m_haveDump ? m_dump.regs[bank + 0xA0 + slot] : 0;
		const BYTE fade = live ? m_fadeKey[ch] : (BYTE)0;
		/* 短音符は gate=off で届くことが多い → フェード中も鍵盤を点灯 */
		const int gate = live && m_haveDump && m_dump.keyOnFm[ch];
		const int keyLit = gate || (live && fade >= 40);
		int midi = -1;
		if (keyLit && m_haveDump) {
			if (m_dump.version >= 6 && m_dump.keyMidi[ch] != 0xFF)
				midi = (int)m_dump.keyMidi[ch];
			else if (!KeysOnly() && !msx && !opm && gate)
				/* FMP 鍵盤のみは regs が空/ゴミ。休符中の fnum フォールバックで偽ノートが出る */
				midi = ApproxMidiFromFnum(a4, a0);
		}

		wchar_t note[16];
		FmFormatNoteName(midi, note, 16);
		wchar_t lab[40];
		const wchar_t* nm;
		if (opl) {
			static const wchar_t* kOplName[6] = {
				L"OPL1", L"OPL2", L"OPL3", L"OPL4", L"OPL5", L"OPL6"
			};
			nm = kOplName[ch];
		} else {
			nm = opm ? kOpmName[ch] : (msx ? kOplName[ch] : kFmName[ch]);
		}
		_snwprintf_s(lab, _TRUNCATE, L"%s %s", nm, note);
		drawLabel(yy, lab, fade, RGB(80, 220, 120));

		CRect krc(x + labelW, yy + (rowH - keyH) / 2, x + labelW + pianoW, yy + (rowH - keyH) / 2 + keyH);
		DrawPiano108(dc, krc, midi, keyLit);
		row++;
	};

	auto drawExCh = [&](int i) {
		const int yy = y + row * rowH;
		const BYTE fade = live ? m_fadeEx[i] : (BYTE)0;
		const int gate = live && m_haveDump && m_dump.keyOnEx[i];
		const int keyLit = gate || (live && fade >= 40);
		int midi = -1;
		if (keyLit && m_haveDump) {
			if (m_dump.exMidi[i] != 0xFF) midi = (int)m_dump.exMidi[i];
			else if (!KeysOnly() && !msx && !opm && gate) {
				/* EX fnum: EX1 AC/A8, EX2 AE/AA, EX3 AD/A9 */
				static const int kA4[3] = { 0xAC, 0xAE, 0xAD };
				static const int kA0[3] = { 0xA8, 0xAA, 0xA9 };
				midi = ApproxMidiFromFnum(m_dump.regs[kA4[i]], m_dump.regs[kA0[i]]);
			}
		}
		wchar_t note[16];
		FmFormatNoteName(midi, note, 16);
		wchar_t lab[40];
		const wchar_t* nm;
		if (opl) {
			static const wchar_t* kOplEx[3] = { L"OPL7", L"OPL8", L"OPL9" };
			nm = kOplEx[i];
		} else {
			nm = opm ? kOpmName[6 + i] : (msx ? kOplEx[i] : kExName[i]);
		}
		_snwprintf_s(lab, _TRUNCATE, L"%s %s", nm, note);
		drawLabel(yy, lab, fade, RGB(180, 120, 255));
		CRect krc(x + labelW, yy + (rowH - keyH) / 2, x + labelW + pianoW, yy + (rowH - keyH) / 2 + keyH);
		DrawPiano108(dc, krc, midi, keyLit);
		row++;
	};

	if (nestEx) {
		for (int ch = 0; ch < 3; ch++) drawFmCh(ch);
		for (int i = 0; i < exN; i++) drawExCh(i);
		for (int ch = 3; ch < fmN; ch++) drawFmCh(ch);
	} else {
		for (int ch = 0; ch < fmN; ch++) drawFmCh(ch);
		for (int i = 0; i < exN; i++) drawExCh(i);
	}

	static const wchar_t* kSsg[3] = { L"SSG1", L"SSG2", L"SSG3" };
	static const wchar_t* kPsg[3] = { L"PSG1", L"PSG2", L"PSG3" };
	const int ssgN = SsgRows();
	for (int i = 0; i < ssgN; i++, row++) {
		const int yy = y + row * rowH;
		const uint16_t period = m_haveDump
			? (uint16_t)(m_dump.regs[i * 2] | ((m_dump.regs[i * 2 + 1] & 0x0F) << 8))
			: 0;
		const BYTE fade = live ? m_fadeSsg[i] : (BYTE)0;
		const int gate = live && m_haveDump && m_dump.ssgOn[i];
		const int keyLit = gate || (live && fade >= 40);
		int midi = -1;
		if (keyLit && m_haveDump) {
			if (m_dump.version >= 6 && m_dump.ssgMidi[i] != 0xFF)
				midi = (int)m_dump.ssgMidi[i];
			else if (!KeysOnly() && gate)
				midi = ApproxMidiFromSsg(period);
		}

		wchar_t note[16];
		FmFormatNoteName(midi, note, 16);
		wchar_t noise[16];
		/* 常に4文字: N031 / N--- （# 有無で N が横ずれしない） */
		if (live && m_haveDump && FmSsgNoiseOn(m_dump, i))
			_snwprintf_s(noise, _TRUNCATE, L"N%03d", FmSsgNoisePeriod(m_dump));
		else
			wcscpy_s(noise, L"N---");
		wchar_t lab[48];
		_snwprintf_s(lab, _TRUNCATE, L"%s %s %s",
			(msx && !(MsxDevMask() & SASAMI_FMMON_DEV_HES)) ? kPsg[i] : kSsg[i],
			note, noise);
		drawLabel(yy, lab, fade, RGB(100, 180, 255));

		CRect krc(x + labelW, yy + (rowH - keyH) / 2, x + labelW + pianoW, yy + (rowH - keyH) / 2 + keyH);
		DrawPiano108(dc, krc, midi, keyLit);
	}

	const int pcmN = PcmRows();
	for (int i = 0; i < pcmN; i++, row++) {
		const int yy = y + row * rowH;
		const BYTE fade = live ? m_fadePcm[i] : (BYTE)0;
		const int gate = live && m_haveDump && m_dump.pcmOn[i];
		const int keyLit = gate || (live && fade >= 40);
		int midi = -1;
		if (keyLit && m_haveDump && m_dump.pcmNote[i] != 0xFF)
			midi = (int)m_dump.pcmNote[i];

		wchar_t note[16];
		FmFormatNoteName(midi, note, 16);
		wchar_t lab[40];
		const int ppz = (m_haveDump && m_dump.version >= 6
			&& (m_dump.dumpFlags & SASAMI_FMMON_FLAG_PPZ));
		const int hes = msx && (MsxDevMask() & SASAMI_FMMON_DEV_HES);
		const unsigned prof = ChipProfile();
		const int adpcmRow = (m_haveDump && m_dump.version >= 6
			&& (m_dump.dumpFlags & (SASAMI_FMMON_FLAG_ADPCM | SASAMI_FMMON_FLAG_PCM86))
			&& (((m_dump.dumpFlags & SASAMI_FMMON_FLAG_PPZ) && i == 8)
				|| (!(m_dump.dumpFlags & SASAMI_FMMON_FLAG_PPZ) && i == 0)));
		const wchar_t* pref = nullptr;
		int num = i + 1;
		if (IsYm2610Dump()) {
			/* ADA1-6 = ADPCM-A, ADB = ADPCM-B (when present at index 6) */
			if (i < 6) {
				pref = L"ADA";
				num = i + 1;
			} else {
				pref = L"ADB";
				num = 1;
			}
		} else if (adpcmRow) {
			pref = (m_dump.dumpFlags & SASAMI_FMMON_FLAG_PCM86) ? L"86PCM" : L"ADPCM";
			num = 0;
		}
		else if (hes) { pref = L"SSG"; num = i + 4; }
		else if (msx) { pref = L"SCC"; }
		else if (opl) { pref = L"OPL"; num = i + 10; }
		else if (opm || prof == SASAMI_FMMON_KEYS_MDX) {
			pref = (m_haveDump && strstr(m_dump.titleSjis, "GA20")) ? L"GA20" : L"PDX";
		}
		else if (ppz && i < 8) { pref = L"PPZ"; }
		else {
			switch (prof) {
			case SASAMI_FMMON_KEYS_SPC: pref = L"DSP"; break;
			case SASAMI_FMMON_KEYS_NSF:
				if (i == 0) { pref = L"PU1"; num = 0; }
				else if (i == 1) { pref = L"PU2"; num = 0; }
				else if (i == 2) { pref = L"TRI"; num = 0; }
				else if (i == 3) { pref = L"NOI"; num = 0; }
				else if (i == 4) { pref = L"DMC"; num = 0; }
				else { pref = L"NSF"; num = i + 1; }
				break;
			case SASAMI_FMMON_KEYS_SID: pref = L"SID"; break;
			case SASAMI_FMMON_KEYS_PSF: pref = L"SPU"; break;
			case SASAMI_FMMON_KEYS_GSF: {
				static const wchar_t* kGb[4] = { L"SQ1", L"SQ2", L"WAV", L"NOI" };
				pref = (i < 4) ? kGb[i] : L"GB";
				num = (i < 4) ? 0 : (i + 1);
				break;
			}
			case SASAMI_FMMON_KEYS_NCSF: pref = L"NDS"; break;
			case SASAMI_FMMON_KEYS_MIDI: pref = L"CH"; break;
			case SASAMI_FMMON_KEYS_QSOUND: pref = L"QS"; break;
			case SASAMI_FMMON_KEYS_RF5C: pref = L"RF"; break;
			case SASAMI_FMMON_KEYS_C352: pref = L"C352"; break;
			case SASAMI_FMMON_KEYS_SEGAPCM: pref = L"SPCM"; break;
			case SASAMI_FMMON_KEYS_OKI: pref = L"OKI"; break;
			default: pref = L"CH"; break;
			}
		}
		if (num <= 0)
			_snwprintf_s(lab, _TRUNCATE, L"%s %s", pref, note);
		else
			_snwprintf_s(lab, _TRUNCATE, L"%s%d %s", pref, num, note);
		drawLabel(yy, lab, fade, RGB(220, 160, 80));

		CRect krc(x + labelW, yy + (rowH - keyH) / 2, x + labelW + pianoW, yy + (rowH - keyH) / 2 + keyH);
		DrawPiano108(dc, krc, midi, keyLit);
	}

	if (!HideRhythm()) {
	const int rzmY = y + row * rowH + (rowH / 5);
	dc.SetTextColor(RGB(200, 210, 220));
	dc.TextOut(x, rzmY + 2, L"RHY");
	const int padW = (keyH > 10) ? (keyH + 20) : 40;
	const int padH = (keyH > 10) ? (keyH + 2) : 16;
	for (int i = 0; i < 6; i++) {
		const BYTE fade = live ? m_fadeRzmPad[i] : (BYTE)0;
		const int px = x + labelW + i * (padW + 3);
		FmFillFade(dc, px, rzmY, padW, padH,
			RGB(40, 44, 50), RGB(255, 140, 80), fade);
		dc.SetTextColor(fade > 40 ? RGB(240, 240, 245) : RGB(140, 145, 155));
		dc.TextOut(px + 4, rzmY + 1, kRzmName[i]);
	}
	}

	dc.SelectObject(oldf);
}

void CFmMonitorDlg::ComputeLayout(int w, int h)
{
	memset(&m_lay, 0, sizeof(m_lay));
	m_lay.w = w;
	m_lay.h = h;
	m_lay.dpi = (int)FmUiDpi(GetSafeHwnd());
	const int pcmRows = PcmRows();
	const int exRows = ExRows();
	const int fmRows = FmRows();
	const int ssgRows = SsgRows();
	m_lay.pcmRows = pcmRows;
	m_lay.exRows = exRows;
	m_lay.fmRows = fmRows;
	m_lay.ssgRows = ssgRows;
	m_lay.pad = FmScale(4, m_lay.dpi);
	m_lay.headH = FmScale(18, m_lay.dpi);
	m_lay.gapHexKeys = FmScale(4, m_lay.dpi);
	const int chRows = fmRows + exRows + ssgRows + pcmRows;
	const int keyBlockRows = chRows + (HideRhythm() ? 0 : 1);
	m_lay.bankGap = FmScale(8, m_lay.dpi);

	int avail = h - m_lay.pad - m_lay.headH - m_lay.gapHexKeys - m_lay.pad;
	if (avail < 120) avail = 120;
	const int topShare = 18;
	const int botShare = keyBlockRows;
	m_lay.topH = avail * topShare / (topShare + botShare);
	m_lay.rowH = (avail - m_lay.topH) / keyBlockRows;
	if (m_lay.rowH < 11) m_lay.rowH = 11;
	if (m_lay.topH < 120) m_lay.topH = 120;
	{
		const int usedBot = keyBlockRows * m_lay.rowH;
		int rem = avail - m_lay.topH - usedBot;
		if (rem > 0) m_lay.topH += rem;
	}
	m_lay.keyH = (m_lay.rowH > 3) ? (m_lay.rowH - 2) : m_lay.rowH;
	m_lay.labelW = FmScale(120, m_lay.dpi);
	m_lay.topY = m_lay.pad + m_lay.headH;

	/* bankTitle = タイトル行 + col ヘッダ行。DrawHexBank と一致させる */
	const int titleLine = FmScale(14, m_lay.dpi);
	m_lay.cellH = (m_lay.topH - 2 * titleLine - m_lay.bankGap) / 34; /* 仮: 2*(title+cellH)+32*cellH */
	if (m_lay.cellH < 12) m_lay.cellH = 12;
	if (m_lay.cellH > 22) m_lay.cellH = 22;
	m_lay.bankTitle = m_lay.cellH + titleLine + 2;
	{
		const int need = 2 * (m_lay.bankTitle + 16 * m_lay.cellH) + m_lay.bankGap;
		if (need > m_lay.topH) {
			const int room = m_lay.topH - m_lay.bankGap - 2 * (titleLine + 2);
			m_lay.cellH = room / 34;
			if (m_lay.cellH < 10) m_lay.cellH = 10;
			if (m_lay.cellH > 22) m_lay.cellH = 22;
			m_lay.bankTitle = m_lay.cellH + titleLine + 2;
		}
	}
	m_lay.cellW = (std::max)(16, m_lay.cellH + 6);
	{
		HFONT measureFont = FmMakeFont((std::max)(8, m_lay.cellH - 2));
		HDC screen = ::GetDC(nullptr);
		if (screen) {
			HGDIOBJ old = ::SelectObject(screen, measureFont);
			SIZE sz = {};
			::GetTextExtentPoint32W(screen, L"00", 2, &sz);
			::SelectObject(screen, old);
			::ReleaseDC(nullptr, screen);
			m_lay.cellW = (std::max)(16, (int)sz.cx + 6);
		}
	}
	m_lay.gapExtra = (std::max)(FmScale(4, m_lay.dpi), 4);
	const int hexInnerW = m_lay.cellW + 16 * m_lay.cellW + 4 * m_lay.gapExtra;
	m_lay.hexColW = hexInnerW + FmScale(8, m_lay.dpi);
	const int minFm = FmScale(480, m_lay.dpi);
	if (m_lay.hexColW + minFm + m_lay.pad * 2 > w)
		m_lay.hexColW = (std::max)(hexInnerW, w - m_lay.pad * 2 - minFm);
	m_lay.hexX = m_lay.pad + m_lay.cellW;
	m_lay.gridY0 = m_lay.topY + m_lay.bankTitle;
	m_lay.gridY1 = m_lay.gridY0 + 16 * m_lay.cellH + m_lay.bankGap + m_lay.bankTitle;
	m_lay.fmX = m_lay.pad + m_lay.hexColW + FmScale(4, m_lay.dpi);
	m_lay.fmW = (std::max)(100, w - m_lay.pad - m_lay.fmX);
	m_lay.gap = FmScale(3, m_lay.dpi);
	{
		const int fmN = FmRows();
		const int cols = (fmN <= 2) ? (std::max)(1, fmN) : ((fmN <= 4) ? 2 : 3);
		const int rows = (fmN <= 0) ? 1 : ((fmN + cols - 1) / cols);
		m_lay.pw = (m_lay.fmW - m_lay.gap * (cols - 1)) / (std::max)(1, cols);
		m_lay.ph = (m_lay.topH - m_lay.gap * (rows - 1)) / (std::max)(1, rows);
	}
	m_lay.keysY = m_lay.topY + m_lay.topH + m_lay.gapHexKeys;
	m_lay.keysW = (std::max)(120, w - m_lay.pad * 2);

	m_lay.rcHead.SetRect(0, 0, w, m_lay.topY);
	m_lay.rcHex.SetRect(0, m_lay.topY, m_lay.fmX, m_lay.topY + m_lay.topH);
	m_lay.rcPanels.SetRect(m_lay.fmX, m_lay.topY, w, m_lay.topY + m_lay.topH);
	m_lay.rcKeys.SetRect(0, m_lay.keysY, w, h);
	m_layOk = 1;
}

void CFmMonitorDlg::DrawHead(CDC& dc)
{
	if (!m_layOk) return;
	dc.FillSolidRect(m_lay.rcHead, FM_BG);
	CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(230, 235, 240));
	wchar_t head[512];
	if (m_haveDump) {
		const wchar_t* stem = m_dump.sourcePath;
		for (const wchar_t* p = m_dump.sourcePath; *p; p++)
			if (*p == L'\\' || *p == L'/') stem = p + 1;
		const wchar_t* chip;
		if (m_dump.titleSjis[0]
			&& (IsOpmDump() || ChipProfile() == SASAMI_FMMON_KEYS_MDX
				|| (!(m_dump.dumpFlags & SASAMI_FMMON_FLAG_FMP)
					&& !(m_dump.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY)
					&& !IsOplDump() && !IsMsxDump()))) {
			/* CEmu identity in titleSjis ("PC-88  OPNA+ADPCM", "X1  OPM+AY", …) */
			static wchar_t idChip[96];
			MultiByteToWideChar(CP_ACP, 0, m_dump.titleSjis, -1, idChip, 96);
			chip = idChip;
		}
		else if (IsOpmDump() || ChipProfile() == SASAMI_FMMON_KEYS_MDX)
			chip = HasViewRegs()
				? L"MDX  OPM×8 (+regs)"
				: L"MDX  OPM×8 (keys)";
		else if (IsOplDump()) {
			static wchar_t oplChip[96];
			if (m_dump.titleSjis[0]) {
				MultiByteToWideChar(CP_ACP, 0, m_dump.titleSjis, -1, oplChip, 96);
				chip = oplChip;
			} else {
				const unsigned hw = (unsigned)m_dump.pad6[0];
				const wchar_t* kind = (hw == 1) ? L"OPL2×9" :
					(hw == 3) ? L"DualOPL2×18" : L"OPL3×18";
				_snwprintf_s(oplChip, _TRUNCATE, L"OPL  %s", kind);
				chip = oplChip;
			}
		}
		else if (KeysOnly()) {
			switch (ChipProfile()) {
			case SASAMI_FMMON_KEYS_SPC: chip = L"SPC  S-DSP×8"; break;
			case SASAMI_FMMON_KEYS_NSF: chip = L"NSF  APU(+exp)"; break;
			case SASAMI_FMMON_KEYS_SID: chip = L"SID  ×3"; break;
			case SASAMI_FMMON_KEYS_PSF: chip = L"PSF  SPU"; break;
			case SASAMI_FMMON_KEYS_GSF: chip = L"GSF  GB APU×4"; break;
			case SASAMI_FMMON_KEYS_NCSF: chip = L"NCSF  Nitro"; break;
			case SASAMI_FMMON_KEYS_MIDI: {
				static wchar_t midChip[96];
				if (m_dump.titleSjis[0]) {
					MultiByteToWideChar(CP_ACP, 0, m_dump.titleSjis, -1, midChip, 96);
					chip = midChip;
				} else {
					chip = L"MIDI";
				}
				break;
			}
			case SASAMI_FMMON_KEYS_QSOUND: chip = L"AC  QSound×16"; break;
			case SASAMI_FMMON_KEYS_RF5C: chip = L"AC  RF5C68×8"; break;
			case SASAMI_FMMON_KEYS_C352: chip = L"AC  C352×16"; break;
			case SASAMI_FMMON_KEYS_SEGAPCM: chip = L"Sega  SegaPCM×8/16"; break;
			case SASAMI_FMMON_KEYS_OKI: chip = L"AC  OKI×4"; break;
			default: chip = L"Keys  CH×n"; break;
			}
		}
		else if (IsMsxDump()) {
			static wchar_t msxChip[96];
			const unsigned m = MsxDevMask();
			if (m & SASAMI_FMMON_DEV_HES)
				_snwprintf_s(msxChip, _TRUNCATE, L"HES  SSG1-6 (HuC6280)");
			else
				_snwprintf_s(msxChip, _TRUNCATE, L"MSX  %s%s%s",
					(m & SASAMI_FMMON_DEV_PSG) ? L"PSG " : L"",
					(m & SASAMI_FMMON_DEV_OPLL) ? L"OPLL/FMPAC " : L"",
					(m & SASAMI_FMMON_DEV_SCC) ? L"SCC" : L"");
			chip = msxChip;
		}
		else if (m_dump.version >= 6 && ExRows() > 0
			&& !(m_dump.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY)
			&& !IsOpmDump() && !IsMsxDump()) {
			const int isFmp = (m_dump.dumpFlags & SASAMI_FMMON_FLAG_FMP) ? 1 : 0;
			static wchar_t opnaChip[96];
			const wchar_t* base = isFmp ? L"FMP  OPNA+EX" : L"PMD  OPNA+EX";
			const wchar_t* pcm =
				(m_dump.dumpFlags & SASAMI_FMMON_FLAG_PCM86) ? L"+86PCM" :
				(m_dump.dumpFlags & SASAMI_FMMON_FLAG_ADPCM) ? L"+ADPCM" : L"";
			const wchar_t* ppz = (m_dump.dumpFlags & SASAMI_FMMON_FLAG_PPZ) ? L"/PPZ" : L"";
			_snwprintf_s(opnaChip, _TRUNCATE, L"%s%s%s", base, pcm, ppz);
			chip = opnaChip;
		}
		else if (m_dump.padHit == 5)
			chip = L"SN76489  Tone×3+Noise";
		else if (m_dump.padHit == 6)
			chip = L"YM2610  FM×4+SSG×3+ADPCM";
		else if (m_dump.padHit == 1)
			chip = L"OPN   FM×3+SSG×3";
		else if (m_dump.padHit == 0)
			chip = L"BEEP";
		else
			chip = L"OPNA  FM×6";
		_snwprintf_s(head, _TRUNCATE,
			L"%s   seq=%u  %uHz  %s%s",
			chip, m_dump.seq, m_dump.sampleRate,
			stem, (m_dump.fm10 && !IsYm2610Dump()) ? L"  [10ch]" : L"");
	} else {
		_snwprintf_s(head, _TRUNCATE, L"%s",
			LL14(L"OPN/OPNA dump 待機中（PMD/FMPは新Plugins要）",
				L"Waiting for OPN/OPNA dump (PMD/FMP need updated Plugins)",
				L"En attente du dump OPN/OPNA (PMD/FMP: Plugins a jour requis)",
				L"In attesa del dump OPN/OPNA (PMD/FMP: servono Plugins aggiornati)",
				L"Esperando dump OPN/OPNA (PMD/FMP: se necesitan Plugins actualizados)",
				L"OPN/OPNA dump 대기 중(PMD/FMP는 새 Plugins 필요)",
				L"等待 OPN/OPNA dump（PMD/FMP 需要新版 Plugins）",
				L"بانتظار تفريغ OPN/OPNA (PMD/FMP يحتاج Plugins محدّث)",
				L"Ожидание dump OPN/OPNA (PMD/FMP нужны обновлённые Plugins)",
				L"Warte auf OPN/OPNA-Dump (PMD/FMP braucht aktuelle Plugins)",
				L"Aguardando dump OPN/OPNA (PMD/FMP precisa de Plugins atualizados)",
				L"Wachten op OPN/OPNA-dump (PMD/FMP heeft bijgewerkte Plugins nodig)",
				L"Oczekiwanie na dump OPN/OPNA (PMD/FMP wymaga aktualnych Plugins)",
				L"OPN/OPNA dump bekleniyor (PMD/FMP icin guncel Plugins gerekir)"));
	}
	dc.TextOut(m_lay.pad, m_lay.pad, head);
	dc.SelectObject(old);
}

void CFmMonitorDlg::DrawHexArea(CDC& dc)
{
	if (!m_layOk) return;
	dc.FillSolidRect(m_lay.rcHex, FM_BG);
	if (IsOpmDump() && HasViewRegs()) {
		DrawHexBank(dc, m_lay.hexX, m_lay.gridY0, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x000, L"OPM $00-$FF");
		CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(120, 130, 140));
		dc.TextOut(m_lay.hexX, m_lay.gridY1, L"(YM2151 single map)");
		dc.SelectObject(old);
		return;
	}
		if (IsOplDump() && HasViewRegs()) {
		const int opl3 = (ChipProfile() == SASAMI_FMMON_KEYS_OPL3) ? 1 : 0;
		const int pcat = (m_dump.titleSjis[0]
			&& strstr(m_dump.titleSjis, "PC/AT")) ? 1 : 0;
		DrawHexBank(dc, m_lay.hexX, m_lay.gridY0, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x000,
			opl3 ? L"OPL port0 / chip0"
			: (pcat ? L"AdLib/SB OPL2 $00-$FF" : L"OPL2 $00-$FF"));
		if (opl3) {
			DrawHexBank(dc, m_lay.hexX, m_lay.gridY1, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x100,
				L"OPL port1 / chip1");
		} else {
			CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(RGB(120, 130, 140));
			dc.TextOut(m_lay.hexX, m_lay.gridY1,
				pcat ? L"(YM3812 · PC/AT AdLib/Sound Blaster)" : L"(YM3812 single map)");
			dc.SelectObject(old);
		}
		return;
	}
	/* PC/AT BEEP / GameBlaster / MPU — aux regs in keys-only dumps */
	if (KeysOnly() && HasViewRegs() && m_dump.titleSjis[0]
		&& strstr(m_dump.titleSjis, "PC/AT")) {
		const char* t = m_dump.titleSjis;
		if (strstr(t, "BEEP")) {
			DrawHexBank(dc, m_lay.hexX, m_lay.gridY0, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x000,
				L"BEEP PIT2/61 $00-$0F", 1);
			CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(RGB(120, 130, 140));
			dc.TextOut(m_lay.hexX, m_lay.gridY1,
				L"(00-01=PIT2 reload  02=port61  03=gate  04=MIDI)");
			dc.SelectObject(old);
			return;
		}
		if (strstr(t, "GameBlaster") || strstr(t, "SAA")) {
			DrawHexBank(dc, m_lay.hexX, m_lay.gridY0, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x000,
				L"SAA0 $00-$1F", 2);
			DrawHexBank(dc, m_lay.hexX, m_lay.gridY1, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x020,
				L"SAA1 $20-$3F", 2);
			return;
		}
		if (strstr(t, "MPU") || strstr(t, "MIDI")) {
			DrawHexBank(dc, m_lay.hexX, m_lay.gridY0, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x010,
				L"MPU note/ch $10-$1F", 1);
			CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(RGB(120, 130, 140));
			dc.TextOut(m_lay.hexX, m_lay.gridY1,
				L"(per-channel last note; 0=off)");
			dc.SelectObject(old);
			return;
		}
	}
	if (IsMsxDump() && HasViewRegs()) {
		const unsigned m = MsxDevMask();
		const int cellH = m_lay.cellH;
		const int gapY = (std::max)(cellH + 4, cellH * 2); /* title+colhdr overhead */
		int y = m_lay.gridY0;
		CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(120, 130, 140));

		if (m & (SASAMI_FMMON_DEV_PSG | SASAMI_FMMON_DEV_HES)) {
			DrawHexBank(dc, m_lay.hexX, y, m_lay.cellW, cellH, m_lay.gapExtra, 0x000,
				(m & SASAMI_FMMON_DEV_HES) ? L"HuC/PSG $00-$0F (ch1-3)" : L"PSG/AY $00-$0F", 1);
			y += gapY + cellH;
		}
		if (m & SASAMI_FMMON_DEV_OPLL) {
			DrawHexBank(dc, m_lay.hexX, y, m_lay.cellW, cellH, m_lay.gapExtra, 0x040,
				L"OPLL/FMPAC $00-$3F (@+$40)", 4);
			y += gapY + 4 * cellH;
		}
		if (m & SASAMI_FMMON_DEV_SCC) {
			DrawHexBank(dc, m_lay.hexX, y, m_lay.cellW, cellH, m_lay.gapExtra, 0x080,
				L"SCC ctrl $80-$8F (freq/vol/on; waves N/A)", 1);
			y += gapY + cellH;
		}
		if (m & SASAMI_FMMON_DEV_HES) {
			DrawHexBank(dc, m_lay.hexX, y, m_lay.cellW, cellH, m_lay.gapExtra, 0x090,
				L"HuC ch4-6 @+$90 (per/vol/ctl)", 1);
		}
		dc.SelectObject(old);
		return;
	}
	if (IsArcadePcmDump() && HasViewRegs()) {
		const unsigned prof = ChipProfile();
		wchar_t title[64];
		_snwprintf_s(title, _TRUNCATE, L"%s regs $00-$FF", FmArcadePcmName(prof));
		DrawHexBank(dc, m_lay.hexX, m_lay.gridY0, m_lay.cellW, m_lay.cellH, m_lay.gapExtra,
			0x000, title);
		if (prof == SASAMI_FMMON_KEYS_QSOUND || prof == SASAMI_FMMON_KEYS_C352) {
			_snwprintf_s(title, _TRUNCATE, L"%s regs $100-$1FF", FmArcadePcmName(prof));
			DrawHexBank(dc, m_lay.hexX, m_lay.gridY1, m_lay.cellW, m_lay.cellH, m_lay.gapExtra,
				0x100, title);
		} else {
			CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(RGB(120, 130, 140));
			dc.TextOut(m_lay.hexX, m_lay.gridY1, L"(single arcade PCM register bank)");
			dc.SelectObject(old);
		}
		return;
	}
	/* 起動直後・MIDI等 keys-only・通常 OPNA: Bank0/1 */
	if (PreferOpnaShell() || HasViewRegs()) {
		const wchar_t* b0 = IsYm2610Dump() ? L"Bank0 SSG/FM/ADPCM-B" : L"Bank0";
		const wchar_t* b1 = IsYm2610Dump() ? L"Bank1 ADPCM-A/FM" : L"Bank1";
		DrawHexBank(dc, m_lay.hexX, m_lay.gridY0, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x000, b0);
		DrawHexBank(dc, m_lay.hexX, m_lay.gridY1, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x100, b1);
		return;
	}
	if (IsMsxDump()) {
		CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(140, 145, 155));
		dc.TextOut(m_lay.pad + 8, m_lay.topY + 8, L"MSX: no chip regs in dump");
		dc.SelectObject(old);
		return;
	}
	DrawHexBank(dc, m_lay.hexX, m_lay.gridY0, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x000, L"Bank0");
	DrawHexBank(dc, m_lay.hexX, m_lay.gridY1, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x100, L"Bank1");
}

void CFmMonitorDlg::DrawOpmChPanel(CDC& dc, const CRect& rc, int ch)
{
	/* YM2151: 8ch×4op。レジスタは ch + op*8。表示順 S1..S4 = OP1,OP2,OP3,OP4
	   (= M1,C1,M2,C2) で OPN ALGO 図と対応。 */
	if (rc.Width() < 100 || rc.Height() < 80 || ch < 0 || ch > 7) return;
	static const int kSOff[4] = { 0, 16, 8, 24 };
	auto reg = [&](int r) -> uint8_t {
		return m_haveDump ? m_dump.regs[r & 0xFF] : 0;
	};

	const int savedDC = dc.SaveDC();
	dc.IntersectClipRect(rc);

	const COLORREF headBg = RGB(40, 52, 72);
	const COLORREF bodyBg = RGB(28, 34, 44);
	const COLORREF rowBg = RGB(32, 40, 52);
	const int pad = (std::max)(3, rc.Width() / 90);
	const int headH = (std::max)(72, rc.Height() * 30 / 100);
	dc.FillSolidRect(rc.left, rc.top, rc.Width(), headH, headBg);
	dc.FillSolidRect(rc.left, rc.top + headH, rc.Width(), rc.Height() - headH, bodyBg);
	FmFrameRect(dc, rc, RGB(120, 150, 190));

	const uint8_t rl = reg(0x20 + ch);
	const uint8_t kc = reg(0x28 + ch);
	const uint8_t kf = reg(0x30 + ch);
	const uint8_t pams = reg(0x38 + ch);
	const int alg = rl & 7;
	const int fb = (rl >> 3) & 7;
	const int panL = (rl >> 6) & 1;
	const int panR = (rl >> 7) & 1;
	const int pan = (panL && panR) ? 3 : (panL ? 1 : (panR ? 2 : 0));
	const int pms = (pams >> 4) & 7;
	const int ams = pams & 3;
	int midi = -1;
	int keyed = 0;
	if (m_haveDump) {
		if (ch < 6) {
			keyed = m_dump.keyOnFm[ch] ? 1 : 0;
			if (m_dump.keyMidi[ch] != 0xFF) midi = (int)m_dump.keyMidi[ch];
		} else {
			keyed = m_dump.keyOnEx[ch - 6] ? 1 : 0;
			if (m_dump.exMidi[ch - 6] != 0xFF) midi = (int)m_dump.exMidi[ch - 6];
		}
	}

	const int titlePx = (std::max)(11, headH / 8);
	HFONT titleFont = FmMakeFont(titlePx);
	HFONT oldf = (HFONT)dc.SelectObject(titleFont);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(headBg);
	dc.SetTextColor(RGB(220, 235, 255));
	wchar_t title[24];
	_snwprintf_s(title, _TRUNCATE, L"OPM%d", ch + 1);
	dc.TextOut(rc.left + pad, rc.top + 2, title);

	const int headInnerTop = rc.top + titlePx + 4;
	const int headInnerBot = rc.top + headH - pad;
	const int innerW = rc.Width() - pad * 2;
	const int algoW = innerW * 36 / 100;
	const int infoW = innerW * 30 / 100;
	const int knobBandW = innerW - algoW - infoW - pad * 2;

	CRect algo(rc.left + pad, headInnerTop, rc.left + pad + algoW, headInnerBot);
	FmDrawAlgo(dc, algo, alg, (std::max)(9, titlePx - 1));

	CRect knobsRc(algo.right + pad, headInnerTop, algo.right + pad + knobBandW, headInnerBot);
	{
		const int cellW = knobsRc.Width() / 2;
		const int cellH = knobsRc.Height() / 2;
		const struct { int v; int vmax; const wchar_t* n; } k4[4] = {
			{ ams, 3, L"AMS" }, { pms, 7, L"PMS" }, { fb, 7, L"FB" }, { pan, 3, L"PAN" }
		};
		for (int i = 0; i < 4; i++) {
			CRect c(
				knobsRc.left + (i % 2) * cellW + 1,
				knobsRc.top + (i / 2) * cellH + 1,
				knobsRc.left + (i % 2) * cellW + cellW - 1,
				knobsRc.top + (i / 2) * cellH + cellH - 1);
			FmDrawParamInCell(dc, c, k4[i].v, k4[i].vmax, k4[i].n, headBg);
		}
	}

	CRect infoRc(rc.right - pad - infoW, headInnerTop, rc.right - pad, headInnerBot);
	{
		const int infoPx = (std::max)(10, (std::min)(14, infoRc.Height() / 5));
		HFONT infoFont = FmMakeFont(infoPx);
		dc.SelectObject(infoFont);
		dc.SetBkColor(headBg);
		dc.SetTextColor(RGB(230, 240, 255));
		wchar_t line[64];
		int iy = infoRc.top + 2;
		wchar_t note[16];
		FmFormatNoteName(midi, note, 16);
		_snwprintf_s(line, _TRUNCATE, L"%s %s", keyed ? L"KEY" : L"---", note);
		dc.TextOut(infoRc.left + 2, iy, line);
		iy += infoPx + 2;
		_snwprintf_s(line, _TRUNCATE, L"KC:%02X KF:%02X", kc, kf);
		dc.TextOut(infoRc.left + 2, iy, line);
		iy += infoPx + 4;
		dc.TextOut(infoRc.left + 2, iy, L"SLOT");
		iy += infoPx + 2;
		const int box = (std::max)(12, (std::min)(infoPx + 2, (infoRc.Width() - 8) / 4 - 2));
		for (int s = 0; s < 4; s++) {
			const int sx = infoRc.left + 2 + s * (box + 2);
			dc.FillSolidRect(sx, iy, box, box, keyed ? RGB(90, 160, 220) : RGB(40, 48, 58));
			FmFrameRect(dc, CRect(sx, iy, sx + box, iy + box), RGB(100, 140, 180));
			wchar_t sn[4];
			_snwprintf_s(sn, _TRUNCATE, L"%d", s + 1);
			dc.SetTextColor(RGB(245, 250, 255));
			CSize sz = dc.GetTextExtent(sn);
			dc.TextOut(sx + (box - sz.cx) / 2, iy + (box - sz.cy) / 2, sn);
		}
		FmDeleteFont(infoFont);
	}

	const int bodyTop = rc.top + headH + pad;
	const int bodyBot = rc.bottom - pad;
	const int slotH = (bodyBot - bodyTop) / 4;
	if (slotH < 24) {
		dc.SelectObject(oldf);
		FmDeleteFont(titleFont);
		dc.RestoreDC(savedDC);
		return;
	}

	for (int op = 0; op < 4; op++) {
		const int yy = bodyTop + op * slotH;
		CRect row(rc.left + pad, yy, rc.right - pad, yy + slotH - 2);
		dc.FillSolidRect(row, rowBg);
		const int off = kSOff[op] + ch;
		const uint8_t dtMul = reg(0x40 + off);
		const uint8_t tl = reg(0x60 + off);
		const uint8_t ksAr = reg(0x80 + off);
		const uint8_t amDr = reg(0xA0 + off);
		const uint8_t dt2D2 = reg(0xC0 + off);
		const uint8_t slRr = reg(0xE0 + off);
		const int mul = dtMul & 0x0F;
		const int dt1 = (dtMul >> 4) & 0x07;
		const int ar = ksAr & 0x1F;
		const int ks = (ksAr >> 6) & 3;
		const int d1r = amDr & 0x1F;
		const int ame = (amDr >> 7) & 1;
		const int d2r = dt2D2 & 0x1F;
		const int dt2 = (dt2D2 >> 6) & 3;
		const int sl = (slRr >> 4) & 0x0F;
		const int rr = slRr & 0x0F;
		const int tl7 = tl & 0x7F;

		const int labW = (std::max)(16, (std::min)(28, row.Width() / 18));
		const int flagW = (std::max)(28, (std::min)(44, row.Width() / 10));
		const int envW = (std::max)(36, (std::min)(72, row.Width() / 6));
		const int paramLeft = row.left + labW + 2;
		const int envRight = paramLeft + envW;
		const int flagLeft = row.right - flagW;
		const int paramW = (std::max)(48, flagLeft - 2 - envRight - 2);

		const int labPx = (std::max)(10, (std::min)(14, row.Height() / 3));
		HFONT labFont = FmMakeFont(labPx);
		dc.SelectObject(labFont);
		dc.SetBkMode(OPAQUE);
		dc.SetBkColor(rowBg);
		dc.SetTextColor(RGB(170, 200, 230));
		wchar_t sn[8];
		_snwprintf_s(sn, _TRUNCATE, L"S%d", op + 1);
		CSize snZ = dc.GetTextExtent(sn);
		dc.TextOut(row.left + (labW - snZ.cx) / 2, row.top + (row.Height() - snZ.cy) / 2, sn);

		CRect env(paramLeft, row.top + 2, envRight, row.bottom - 2);
		FmDrawEnvelope(dc, env, ar, d1r, d2r, rr, sl, tl7);

		const int cols = 6;
		const int cellW = paramW / cols;
		const int gapY = 1;
		const int topRowH = row.Height() * 58 / 100;
		const int botRowH = row.Height() - topRowH - gapY;
		CRect band(envRight + 2, row.top + 1, envRight + 2 + cellW * cols, row.bottom - 1);
		const struct { int v; int vmax; const wchar_t* n; } topP[6] = {
			{ ar, 31, L"AR" }, { d1r, 31, L"D1R" }, { d2r, 31, L"D2R" },
			{ rr, 15, L"RR" }, { sl, 15, L"D1L" }, { tl7, 127, L"TL" }
		};
		for (int i = 0; i < 6; i++) {
			CRect c(band.left + i * cellW, band.top, band.left + (i + 1) * cellW - 1, band.top + topRowH);
			FmDrawParamInCell(dc, c, topP[i].v, topP[i].vmax, topP[i].n, rowBg);
		}
		if (botRowH >= 16) {
			const struct { int v; int vmax; const wchar_t* n; } botP[3] = {
				{ mul, 15, L"MUL" }, { dt1, 7, L"DT1" }, { dt2, 3, L"DT2" }
			};
			for (int i = 0; i < 3; i++) {
				CRect c(band.left + i * cellW, band.top + topRowH + gapY,
					band.left + (i + 1) * cellW - 1, band.bottom);
				FmDrawParamInCell(dc, c, botP[i].v, botP[i].vmax, botP[i].n, rowBg);
			}
		}

		{
			const int half = flagW / 2;
			const int lampPx = (std::max)(8, (std::min)(11, row.Height() / 4));
			HFONT lf = FmMakeFont(lampPx);
			dc.SelectObject(lf);
			dc.SetBkColor(rowBg);
			dc.SetTextColor(RGB(190, 210, 230));
			dc.TextOut(flagLeft + 1, row.top + 1, L"AM");
			const int lamp = (std::max)(8, (std::min)(half - 4, row.Height() / 3));
			dc.FillSolidRect(flagLeft + 2, row.top + lampPx + 2, lamp, lamp,
				ame ? RGB(80, 180, 220) : RGB(40, 48, 56));
			FmFrameRect(dc, CRect(flagLeft + 2, row.top + lampPx + 2, flagLeft + 2 + lamp, row.top + lampPx + 2 + lamp),
				RGB(90, 130, 160));
			dc.TextOut(flagLeft + half, row.top + 1, L"KS");
			wchar_t ksS[4];
			_snwprintf_s(ksS, _TRUNCATE, L"%d", ks);
			dc.TextOut(flagLeft + half, row.top + lampPx + 2, ksS);
			FmDeleteFont(lf);
		}
		FmDeleteFont(labFont);
	}

	dc.SelectObject(oldf);
	FmDeleteFont(titleFont);
	dc.RestoreDC(savedDC);
}

void CFmMonitorDlg::DrawOplChPanel(CDC& dc, const CRect& rc, int ch)
{
	/* YM3812/YMF262: 9 or 18 × 2op. Operator slot table per chip half. */
	if (rc.Width() < 80 || rc.Height() < 56 || ch < 0 || ch > 17) return;
	static const int kOp1[9] = { 0, 1, 2, 6, 7, 8, 12, 13, 14 };
	static const int kOp2[9] = { 3, 4, 5, 9, 10, 11, 15, 16, 17 };
	const int bank = (ch >= 9) ? 0x100 : 0;
	const int loc = ch % 9;
	auto reg = [&](int r) -> uint8_t {
		return m_haveDump ? m_dump.regs[bank + (r & 0xFF)] : 0;
	};

	const int savedDC = dc.SaveDC();
	dc.IntersectClipRect(rc);
	const COLORREF headBg = RGB(36, 56, 52);
	const COLORREF bodyBg = RGB(28, 34, 36);
	const COLORREF rowBg = RGB(32, 42, 40);
	const int pad = (std::max)(3, rc.Width() / 90);
	const int headH = (std::max)(56, rc.Height() * 34 / 100);
	dc.FillSolidRect(rc.left, rc.top, rc.Width(), headH, headBg);
	dc.FillSolidRect(rc.left, rc.top + headH, rc.Width(), rc.Height() - headH, bodyBg);
	FmFrameRect(dc, rc, RGB(100, 170, 150));

	int gate = 0;
	int midi = -1;
	if (m_haveDump) {
		if (ch < 6) {
			gate = m_dump.keyOnFm[ch] ? 1 : 0;
			if (m_dump.keyMidi[ch] != 0xFF) midi = (int)m_dump.keyMidi[ch];
		} else if (ch < 9) {
			gate = m_dump.keyOnEx[ch - 6] ? 1 : 0;
			if (m_dump.exMidi[ch - 6] != 0xFF) midi = (int)m_dump.exMidi[ch - 6];
		} else {
			const int p = ch - 9;
			gate = m_dump.pcmOn[p] ? 1 : 0;
			if (m_dump.pcmNote[p] != 0xFF) midi = (int)m_dump.pcmNote[p];
		}
	}
	const uint8_t fbCon = reg(0xC0 + loc);
	const int fb = (fbCon >> 1) & 7;
	const int conn = fbCon & 1; /* 0=FM, 1=AM */
	const uint8_t b0 = reg(0xB0 + loc);
	const uint8_t a0 = reg(0xA0 + loc);
	const int fnum = a0 | ((b0 & 3) << 8);
	const int blk = (b0 >> 2) & 7;

	const int titlePx = (std::max)(10, headH / 7);
	HFONT titleFont = FmMakeFont(titlePx);
	HFONT oldf = (HFONT)dc.SelectObject(titleFont);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(headBg);
	dc.SetTextColor(RGB(210, 255, 230));
	wchar_t title[24];
	_snwprintf_s(title, _TRUNCATE, L"OPL%d", ch + 1);
	dc.TextOut(rc.left + pad, rc.top + 2, title);

	const int headInnerTop = rc.top + titlePx + 4;
	const int headInnerBot = rc.top + headH - pad;
	const int innerW = rc.Width() - pad * 2;
	const int algoW = innerW * 34 / 100;
	const int infoW = innerW * 34 / 100;
	const int knobBandW = innerW - algoW - infoW - pad * 2;

	/* 2-op connection diagram */
	{
		CRect algo(rc.left + pad, headInnerTop, rc.left + pad + algoW, headInnerBot);
		dc.FillSolidRect(algo, RGB(24, 40, 36));
		FmFrameRect(dc, algo, RGB(80, 140, 120));
		HFONT af = FmMakeFont((std::max)(9, titlePx - 1));
		dc.SelectObject(af);
		dc.SetBkColor(RGB(24, 40, 36));
		dc.SetTextColor(RGB(170, 230, 200));
		dc.TextOut(algo.left + 3, algo.top + 1, conn ? L"AM" : L"FM");
		const int bw = (std::max)(18, algo.Width() / 3);
		const int bh = (std::max)(12, algo.Height() / 4);
		const int cy = algo.CenterPoint().y + 2;
		CRect mBox(algo.left + 6, cy - bh / 2, algo.left + 6 + bw, cy + bh / 2);
		CRect cBox = conn
			? CRect(algo.right - 6 - bw, cy - bh / 2, algo.right - 6, cy + bh / 2)
			: CRect(algo.right - 6 - bw, cy - bh / 2, algo.right - 6, cy + bh / 2);
		if (!conn) {
			/* Mod → Car */
			cBox = CRect(algo.right - 6 - bw, cy - bh / 2, algo.right - 6, cy + bh / 2);
			CPen wire(PS_SOLID, 1, RGB(140, 210, 170));
			CPen* op = dc.SelectObject(&wire);
			dc.MoveTo(mBox.right, cy);
			dc.LineTo(cBox.left, cy);
			dc.SelectObject(op);
		} else {
			/* Mod || Car (additive) */
			mBox = CRect(algo.left + 8, algo.top + titlePx + 6, algo.left + 8 + bw, algo.top + titlePx + 6 + bh);
			cBox = CRect(algo.left + 8, algo.bottom - 6 - bh, algo.left + 8 + bw, algo.bottom - 6);
			CPen wire(PS_SOLID, 1, RGB(140, 210, 170));
			CPen* op = dc.SelectObject(&wire);
			const int mx = algo.right - 10;
			dc.MoveTo(mBox.right, mBox.CenterPoint().y);
			dc.LineTo(mx, mBox.CenterPoint().y);
			dc.LineTo(mx, cBox.CenterPoint().y);
			dc.LineTo(cBox.right, cBox.CenterPoint().y);
			dc.SelectObject(op);
		}
		dc.FillSolidRect(mBox, RGB(44, 68, 54));
		FmFrameRect(dc, mBox, RGB(120, 180, 140));
		dc.FillSolidRect(cBox, RGB(44, 68, 54));
		FmFrameRect(dc, cBox, RGB(120, 180, 140));
		dc.SetTextColor(RGB(240, 250, 245));
		dc.SetBkColor(RGB(44, 68, 54));
		CSize zs = dc.GetTextExtent(L"M");
		dc.TextOut(mBox.left + (mBox.Width() - zs.cx) / 2, mBox.top + (mBox.Height() - zs.cy) / 2, L"M");
		zs = dc.GetTextExtent(L"C");
		dc.TextOut(cBox.left + (cBox.Width() - zs.cx) / 2, cBox.top + (cBox.Height() - zs.cy) / 2, L"C");
		FmDeleteFont(af);
	}

	CRect knobsRc(rc.left + pad + algoW + pad, headInnerTop,
		rc.left + pad + algoW + pad + knobBandW, headInnerBot);
	{
		const int cellW = knobsRc.Width() / 2;
		const int cellH = knobsRc.Height() / 2;
		const struct { int v; int vmax; const wchar_t* n; } k4[4] = {
			{ fb, 7, L"FB" }, { conn, 1, L"CON" }, { blk, 7, L"BLK" }, { fnum & 0x3FF, 1023, L"FN" }
		};
		for (int i = 0; i < 4; i++) {
			CRect c(
				knobsRc.left + (i % 2) * cellW + 1,
				knobsRc.top + (i / 2) * cellH + 1,
				knobsRc.left + (i % 2) * cellW + cellW - 1,
				knobsRc.top + (i / 2) * cellH + cellH - 1);
			FmDrawParamInCell(dc, c, k4[i].v, k4[i].vmax, k4[i].n, headBg);
		}
	}

	CRect infoRc(rc.right - pad - infoW, headInnerTop, rc.right - pad, headInnerBot);
	{
		const int infoPx = (std::max)(10, (std::min)(14, infoRc.Height() / 4));
		HFONT infoFont = FmMakeFont(infoPx);
		dc.SelectObject(infoFont);
		dc.SetBkColor(headBg);
		dc.SetTextColor(RGB(230, 245, 235));
		wchar_t line[64];
		wchar_t note[16];
		FmFormatNoteName(midi, note, 16);
		_snwprintf_s(line, _TRUNCATE, L"%s %s", gate ? L"KEY" : L"---", note);
		dc.TextOut(infoRc.left + 2, infoRc.top + 2, line);
		_snwprintf_s(line, _TRUNCATE, L"F#%03X B%d", fnum & 0x3FF, blk);
		dc.TextOut(infoRc.left + 2, infoRc.top + 2 + infoPx + 2, line);
		FmDeleteFont(infoFont);
	}

	const int bodyTop = rc.top + headH + pad;
	const int bodyBot = rc.bottom - pad;
	const int slotH = (bodyBot - bodyTop) / 2;
	if (slotH < 22) {
		dc.SelectObject(oldf);
		FmDeleteFont(titleFont);
		dc.RestoreDC(savedDC);
		return;
	}

	const int ops[2] = { kOp1[loc], kOp2[loc] };
	for (int oi = 0; oi < 2; oi++) {
		const int yy = bodyTop + oi * slotH;
		CRect row(rc.left + pad, yy, rc.right - pad, yy + slotH - 2);
		dc.FillSolidRect(row, rowBg);
		const int op = ops[oi];
		const uint8_t av = reg(0x20 + op);
		const uint8_t tl = reg(0x40 + op);
		const uint8_t adr = reg(0x60 + op);
		const uint8_t srr = reg(0x80 + op);
		const uint8_t ws = reg(0xE0 + op);
		const int mul = av & 0x0F;
		const int ksr = (av >> 4) & 1;
		const int egT = (av >> 5) & 1;
		const int vib = (av >> 6) & 1;
		const int am = (av >> 7) & 1;
		const int tl6 = tl & 0x3F;
		const int ksl = (tl >> 6) & 3;
		const int ar = (adr >> 4) & 0x0F;
		const int dr = adr & 0x0F;
		const int sl = (srr >> 4) & 0x0F;
		const int rr = srr & 0x0F;
		const int wave = ws & 7;

		const int labW = (std::max)(16, (std::min)(28, row.Width() / 16));
		const int flagW = (std::max)(36, (std::min)(52, row.Width() / 8));
		const int envW = (std::max)(36, (std::min)(72, row.Width() / 6));
		const int paramLeft = row.left + labW + 2;
		const int envRight = paramLeft + envW;
		const int flagLeft = row.right - flagW;
		const int paramW = (std::max)(48, flagLeft - 2 - envRight - 2);

		HFONT labFont = FmMakeFont((std::max)(10, (std::min)(14, row.Height() / 3)));
		dc.SelectObject(labFont);
		dc.SetBkMode(OPAQUE);
		dc.SetBkColor(rowBg);
		dc.SetTextColor(RGB(170, 220, 190));
		wchar_t sn[8];
		_snwprintf_s(sn, _TRUNCATE, L"%c", oi == 0 ? L'M' : L'C');
		CSize snZ = dc.GetTextExtent(sn);
		dc.TextOut(row.left + (labW - snZ.cx) / 2, row.top + (row.Height() - snZ.cy) / 2, sn);

		/* OPL AR/DR are 0..15 — scale for envelope viz like OPN 0..31 */
		CRect env(paramLeft, row.top + 2, envRight, row.bottom - 2);
		FmDrawEnvelope(dc, env, ar * 2, dr * 2, 0, rr * 2, sl, tl6 * 2);

		const int cols = 6;
		const int cellW = paramW / cols;
		const int gapY = 1;
		const int topRowH = row.Height() * 58 / 100;
		const int botRowH = row.Height() - topRowH - gapY;
		CRect band(envRight + 2, row.top + 1, envRight + 2 + cellW * cols, row.bottom - 1);
		const struct { int v; int vmax; const wchar_t* n; } topP[6] = {
			{ ar, 15, L"AR" }, { dr, 15, L"DR" }, { sl, 15, L"SL" },
			{ rr, 15, L"RR" }, { tl6, 63, L"TL" }, { mul, 15, L"MUL" }
		};
		for (int i = 0; i < 6; i++) {
			CRect c(band.left + i * cellW, band.top, band.left + (i + 1) * cellW - 1, band.top + topRowH);
			FmDrawParamInCell(dc, c, topP[i].v, topP[i].vmax, topP[i].n, rowBg);
		}
		if (botRowH >= 14) {
			const struct { int v; int vmax; const wchar_t* n; } botP[3] = {
				{ ksl, 3, L"KSL" }, { wave, 7, L"WS" }, { egT, 1, L"EG" }
			};
			for (int i = 0; i < 3; i++) {
				CRect c(band.left + i * cellW, band.top + topRowH + gapY,
					band.left + (i + 1) * cellW - 1, band.bottom);
				FmDrawParamInCell(dc, c, botP[i].v, botP[i].vmax, botP[i].n, rowBg);
			}
		}

		{
			const int lampPx = (std::max)(8, (std::min)(11, row.Height() / 4));
			HFONT lf = FmMakeFont(lampPx);
			dc.SelectObject(lf);
			dc.SetBkColor(rowBg);
			dc.SetTextColor(RGB(190, 220, 200));
			const int lamp = (std::max)(7, (std::min)(flagW / 3 - 2, row.Height() / 3));
			dc.TextOut(flagLeft + 1, row.top + 1, L"AM");
			dc.FillSolidRect(flagLeft + 2, row.top + lampPx + 2, lamp, lamp,
				am ? RGB(80, 200, 140) : RGB(40, 48, 44));
			dc.TextOut(flagLeft + flagW / 3, row.top + 1, L"VB");
			dc.FillSolidRect(flagLeft + flagW / 3 + 1, row.top + lampPx + 2, lamp, lamp,
				vib ? RGB(80, 180, 220) : RGB(40, 48, 44));
			dc.TextOut(flagLeft + 2 * flagW / 3, row.top + 1, L"KS");
			wchar_t ksS[4];
			_snwprintf_s(ksS, _TRUNCATE, L"%d", ksr);
			dc.TextOut(flagLeft + 2 * flagW / 3, row.top + lampPx + 2, ksS);
			FmDeleteFont(lf);
		}
		FmDeleteFont(labFont);
	}

	dc.SelectObject(oldf);
	FmDeleteFont(titleFont);
	dc.RestoreDC(savedDC);
}

void CFmMonitorDlg::DrawOpllChPanel(CDC& dc, const CRect& rc, int ch)
{
	/* YM2413: 9ch。USR=regs $00-$07、preset=チップ内蔵音色 ROM（emu2413 YM2413 set）。 */
	if (rc.Width() < 80 || rc.Height() < 56 || ch < 0 || ch > 8) return;
	static const wchar_t* kInst[16] = {
		L"USR", L"Vln", L"Gtr", L"Pno", L"Flt", L"Clr", L"Obo", L"Trp",
		L"Org", L"Hrn", L"Syn", L"Hps", L"Vib", L"SyB", L"AcB", L"EGt"
	};
	/* default_inst[0] melodic 1..15（0 は USR なので未使用）— emu2413 YM2413 ROM */
	static const uint8_t kRom[16][8] = {
		{ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
		{ 0x71,0x61,0x1e,0x17,0xd0,0x78,0x00,0x17 },
		{ 0x13,0x41,0x1a,0x0d,0xd8,0xf7,0x23,0x13 },
		{ 0x13,0x01,0x99,0x00,0xf2,0xc4,0x21,0x23 },
		{ 0x11,0x61,0x0e,0x07,0x8d,0x64,0x70,0x27 },
		{ 0x32,0x21,0x1e,0x06,0xe1,0x76,0x01,0x28 },
		{ 0x31,0x22,0x16,0x05,0xe0,0x71,0x00,0x18 },
		{ 0x21,0x61,0x1d,0x07,0x82,0x81,0x11,0x07 },
		{ 0x33,0x21,0x2d,0x13,0xb0,0x70,0x00,0x07 },
		{ 0x61,0x61,0x1b,0x06,0x64,0x65,0x10,0x17 },
		{ 0x41,0x61,0x0b,0x18,0x85,0xf0,0x81,0x07 },
		{ 0x33,0x01,0x83,0x11,0xea,0xef,0x10,0x04 },
		{ 0x17,0xc1,0x24,0x07,0xf8,0xf8,0x22,0x12 },
		{ 0x61,0x50,0x0c,0x05,0xd2,0xf5,0x40,0x42 },
		{ 0x01,0x01,0x55,0x03,0xe9,0x90,0x03,0x02 },
		{ 0x41,0x41,0x89,0x03,0xf1,0xe4,0xc0,0x13 },
	};
	auto reg = [&](int r) -> uint8_t {
		return m_haveDump ? m_dump.regs[0x40 + (r & 0x3F)] : 0;
	};

	const int savedDC = dc.SaveDC();
	dc.IntersectClipRect(rc);
	const COLORREF headBg = RGB(48, 44, 64);
	const COLORREF bodyBg = RGB(30, 28, 40);
	const COLORREF rowBg = RGB(36, 34, 48);
	const int pad = (std::max)(3, rc.Width() / 90);
	const int headH = (std::max)(56, rc.Height() * 34 / 100);
	dc.FillSolidRect(rc.left, rc.top, rc.Width(), headH, headBg);
	dc.FillSolidRect(rc.left, rc.top + headH, rc.Width(), rc.Height() - headH, bodyBg);
	FmFrameRect(dc, rc, RGB(150, 130, 190));

	int gate = 0;
	int midi = -1;
	if (m_haveDump) {
		if (ch < 6) {
			gate = m_dump.keyOnFm[ch] ? 1 : 0;
			if (m_dump.keyMidi[ch] != 0xFF) midi = (int)m_dump.keyMidi[ch];
		} else {
			gate = m_dump.keyOnEx[ch - 6] ? 1 : 0;
			if (m_dump.exMidi[ch - 6] != 0xFF) midi = (int)m_dump.exMidi[ch - 6];
		}
	}
	const uint8_t r30 = reg(0x30 + ch);
	const int inst = (r30 >> 4) & 0x0F;
	const int vol = r30 & 0x0F;
	const uint8_t r20 = reg(0x20 + ch);
	const uint8_t r10 = reg(0x10 + ch);
	const int fnum = r10 | ((r20 & 1) << 8);
	const int blk = (r20 >> 1) & 7;
	const int sus = (r20 >> 5) & 1;
	const int custom = (inst == 0) ? 1 : 0;

	/* 8-byte patch: USR=shadow regs, else YM2413 ROM */
	uint8_t patch[8];
	if (custom) {
		for (int i = 0; i < 8; i++) patch[i] = reg(i);
	} else {
		memcpy(patch, kRom[inst], 8);
	}
	const int fb = patch[3] & 7;

	const int titlePx = (std::max)(10, headH / 7);
	HFONT titleFont = FmMakeFont(titlePx);
	HFONT oldf = (HFONT)dc.SelectObject(titleFont);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(headBg);
	dc.SetTextColor(RGB(230, 220, 255));
	wchar_t title[24];
	_snwprintf_s(title, _TRUNCATE, L"OPLL%d", ch + 1);
	dc.TextOut(rc.left + pad, rc.top + 2, title);

	const int headInnerTop = rc.top + titlePx + 4;
	const int headInnerBot = rc.top + headH - pad;
	const int innerW = rc.Width() - pad * 2;
	const int algoW = innerW * 32 / 100;
	const int infoW = innerW * 36 / 100;
	const int knobBandW = innerW - algoW - infoW - pad * 2;

	{
		CRect algo(rc.left + pad, headInnerTop, rc.left + pad + algoW, headInnerBot);
		dc.FillSolidRect(algo, RGB(32, 28, 48));
		FmFrameRect(dc, algo, RGB(120, 100, 160));
		HFONT af = FmMakeFont((std::max)(9, titlePx - 1));
		dc.SelectObject(af);
		dc.SetBkColor(RGB(32, 28, 48));
		dc.SetTextColor(RGB(210, 190, 255));
		dc.TextOut(algo.left + 3, algo.top + 1, kInst[inst]);
		wchar_t sub[32];
		_snwprintf_s(sub, _TRUNCATE, L"FM FB%d%s", fb, custom ? L"" : L" ROM");
		dc.TextOut(algo.left + 3, algo.top + titlePx + 2, sub);
		/* M → C 簡易図 */
		{
			const int bw = (std::max)(16, algo.Width() / 4);
			const int bh = (std::max)(10, (algo.Height() - titlePx * 2) / 3);
			const int cy = algo.bottom - bh - 4;
			CRect mBox(algo.left + 6, cy, algo.left + 6 + bw, cy + bh);
			CRect cBox(algo.right - 6 - bw, cy, algo.right - 6, cy + bh);
			CPen wire(PS_SOLID, 1, RGB(170, 150, 220));
			CPen* op = dc.SelectObject(&wire);
			dc.MoveTo(mBox.right, mBox.CenterPoint().y);
			dc.LineTo(cBox.left, cBox.CenterPoint().y);
			dc.SelectObject(op);
			dc.FillSolidRect(mBox, RGB(56, 48, 72));
			FmFrameRect(dc, mBox, RGB(150, 130, 190));
			dc.FillSolidRect(cBox, RGB(56, 48, 72));
			FmFrameRect(dc, cBox, RGB(150, 130, 190));
			dc.SetBkColor(RGB(56, 48, 72));
			dc.SetTextColor(RGB(240, 235, 255));
			CSize zs = dc.GetTextExtent(L"M");
			dc.TextOut(mBox.left + (mBox.Width() - zs.cx) / 2, mBox.top + (mBox.Height() - zs.cy) / 2, L"M");
			zs = dc.GetTextExtent(L"C");
			dc.TextOut(cBox.left + (cBox.Width() - zs.cx) / 2, cBox.top + (cBox.Height() - zs.cy) / 2, L"C");
		}
		FmDeleteFont(af);
	}

	CRect knobsRc(rc.left + pad + algoW + pad, headInnerTop,
		rc.left + pad + algoW + pad + knobBandW, headInnerBot);
	{
		const int cellW = knobsRc.Width() / 2;
		const int cellH = knobsRc.Height() / 2;
		const struct { int v; int vmax; const wchar_t* n; } k4[4] = {
			{ inst, 15, L"INS" }, { vol, 15, L"VOL" }, { fb, 7, L"FB" }, { sus, 1, L"SUS" }
		};
		for (int i = 0; i < 4; i++) {
			CRect c(
				knobsRc.left + (i % 2) * cellW + 1,
				knobsRc.top + (i / 2) * cellH + 1,
				knobsRc.left + (i % 2) * cellW + cellW - 1,
				knobsRc.top + (i / 2) * cellH + cellH - 1);
			FmDrawParamInCell(dc, c, k4[i].v, k4[i].vmax, k4[i].n, headBg);
		}
	}

	CRect infoRc(rc.right - pad - infoW, headInnerTop, rc.right - pad, headInnerBot);
	{
		const int infoPx = (std::max)(10, (std::min)(14, infoRc.Height() / 4));
		HFONT infoFont = FmMakeFont(infoPx);
		dc.SelectObject(infoFont);
		dc.SetBkColor(headBg);
		dc.SetTextColor(RGB(235, 230, 255));
		wchar_t line[64];
		wchar_t note[16];
		FmFormatNoteName(midi, note, 16);
		_snwprintf_s(line, _TRUNCATE, L"%s %s", gate ? L"KEY" : L"---", note);
		dc.TextOut(infoRc.left + 2, infoRc.top + 2, line);
		_snwprintf_s(line, _TRUNCATE, L"F#%03X B%d", fnum & 0x1FF, blk);
		dc.TextOut(infoRc.left + 2, infoRc.top + 2 + infoPx + 2, line);
		FmDeleteFont(infoFont);
	}

	const int bodyTop = rc.top + headH + pad;
	const int bodyBot = rc.bottom - pad;
	const int slotH = (bodyBot - bodyTop) / 2;
	if (slotH < 22) {
		dc.SelectObject(oldf);
		FmDeleteFont(titleFont);
		dc.RestoreDC(savedDC);
		return;
	}

	for (int oi = 0; oi < 2; oi++) {
		const int yy = bodyTop + oi * slotH;
		CRect row(rc.left + pad, yy, rc.right - pad, yy + slotH - 2);
		dc.FillSolidRect(row, rowBg);

		const uint8_t r0 = patch[oi];
		const uint8_t r2 = patch[2 + oi];
		const uint8_t r4 = patch[4 + oi];
		const uint8_t r6 = patch[6 + oi];
		const int mul = r0 & 0x0F;
		const int ksr = (r0 >> 4) & 1;
		const int egT = (r0 >> 5) & 1;
		const int vib = (r0 >> 6) & 1;
		const int am = (r0 >> 7) & 1;
		const int ksl = (r2 >> 6) & 3;
		/* Carrier TL はチャンネル VOL（ROM 側 TL は常に0相当） */
		int tl = (oi == 0) ? (r2 & 0x3F) : (vol * 4);
		int tlMax = (oi == 0) ? 63 : 60;
		const wchar_t* tlName = (oi == 0) ? L"TL" : L"VOL";
		if (oi == 1) { tl = vol; tlMax = 15; }
		const int ar = (r4 >> 4) & 0x0F;
		const int dr = r4 & 0x0F;
		const int sl = (r6 >> 4) & 0x0F;
		const int rr = r6 & 0x0F;
		const int wave = (oi == 0) ? ((patch[3] >> 3) & 1) : ((patch[3] >> 4) & 1);

		const int labW = (std::max)(16, (std::min)(28, row.Width() / 16));
		const int flagW = (std::max)(36, (std::min)(52, row.Width() / 8));
		const int envW = (std::max)(36, (std::min)(72, row.Width() / 6));
		const int paramLeft = row.left + labW + 2;
		const int envRight = paramLeft + envW;
		const int flagLeft = row.right - flagW;
		const int paramW = (std::max)(48, flagLeft - 2 - envRight - 2);

		HFONT labFont = FmMakeFont((std::max)(10, (std::min)(14, row.Height() / 3)));
		dc.SelectObject(labFont);
		dc.SetBkMode(OPAQUE);
		dc.SetBkColor(rowBg);
		dc.SetTextColor(RGB(190, 180, 230));
		wchar_t sn[8];
		_snwprintf_s(sn, _TRUNCATE, L"%c", oi == 0 ? L'M' : L'C');
		CSize snZ = dc.GetTextExtent(sn);
		dc.TextOut(row.left + (labW - snZ.cx) / 2, row.top + (row.Height() - snZ.cy) / 2, sn);

		CRect env(paramLeft, row.top + 2, envRight, row.bottom - 2);
		const int tlEnv = (oi == 0) ? tl : (vol * 4);
		FmDrawEnvelope(dc, env, ar * 2, dr * 2, 0, rr * 2, sl, tlEnv);

		const int cols = 6;
		const int cellW = paramW / cols;
		const int gapY = 1;
		const int topRowH = row.Height() * 58 / 100;
		const int botRowH = row.Height() - topRowH - gapY;
		CRect band(envRight + 2, row.top + 1, envRight + 2 + cellW * cols, row.bottom - 1);
		const struct { int v; int vmax; const wchar_t* n; } topP[6] = {
			{ ar, 15, L"AR" }, { dr, 15, L"DR" }, { sl, 15, L"SL" },
			{ rr, 15, L"RR" }, { tl, tlMax, tlName }, { mul, 15, L"MUL" }
		};
		for (int i = 0; i < 6; i++) {
			CRect c(band.left + i * cellW, band.top, band.left + (i + 1) * cellW - 1, band.top + topRowH);
			FmDrawParamInCell(dc, c, topP[i].v, topP[i].vmax, topP[i].n, rowBg);
		}
		if (botRowH >= 14) {
			const struct { int v; int vmax; const wchar_t* n; } botP[3] = {
				{ ksl, 3, L"KSL" }, { wave, 1, L"WS" }, { egT, 1, L"EG" }
			};
			for (int i = 0; i < 3; i++) {
				CRect c(band.left + i * cellW, band.top + topRowH + gapY,
					band.left + (i + 1) * cellW - 1, band.bottom);
				FmDrawParamInCell(dc, c, botP[i].v, botP[i].vmax, botP[i].n, rowBg);
			}
		}
		{
			const int lampPx = (std::max)(8, (std::min)(11, row.Height() / 4));
			HFONT lf = FmMakeFont(lampPx);
			dc.SelectObject(lf);
			dc.SetBkColor(rowBg);
			dc.SetTextColor(RGB(200, 190, 230));
			const int lamp = (std::max)(7, (std::min)(flagW / 3 - 2, row.Height() / 3));
			dc.TextOut(flagLeft + 1, row.top + 1, L"AM");
			dc.FillSolidRect(flagLeft + 2, row.top + lampPx + 2, lamp, lamp,
				am ? RGB(150, 120, 220) : RGB(40, 36, 48));
			dc.TextOut(flagLeft + flagW / 3, row.top + 1, L"VB");
			dc.FillSolidRect(flagLeft + flagW / 3 + 1, row.top + lampPx + 2, lamp, lamp,
				vib ? RGB(80, 180, 220) : RGB(40, 36, 48));
			dc.TextOut(flagLeft + 2 * flagW / 3, row.top + 1, L"KS");
			wchar_t ksS[4];
			_snwprintf_s(ksS, _TRUNCATE, L"%d", ksr);
			dc.TextOut(flagLeft + 2 * flagW / 3, row.top + lampPx + 2, ksS);
			FmDeleteFont(lf);
		}
		FmDeleteFont(labFont);
	}

	dc.SelectObject(oldf);
	FmDeleteFont(titleFont);
	dc.RestoreDC(savedDC);
}

void CFmMonitorDlg::DrawArcadePcmChPanel(CDC& dc, const CRect& rc, int ch, unsigned profile)
{
	if (rc.Width() < 70 || rc.Height() < 46 || ch < 0) return;
	const int savedDC = dc.SaveDC();
	dc.IntersectClipRect(rc);

	const COLORREF headBg = RGB(62, 44, 34);
	const COLORREF bodyBg = RGB(34, 34, 42);
	const COLORREF barBg = RGB(24, 26, 30);
	const int pad = (std::max)(3, rc.Width() / 80);
	const int headH = (std::max)(22, rc.Height() / 3);
	dc.FillSolidRect(rc.left, rc.top, rc.Width(), headH, headBg);
	dc.FillSolidRect(rc.left, rc.top + headH, rc.Width(), rc.Height() - headH, bodyBg);
	FmFrameRect(dc, rc, RGB(170, 120, 85));

	auto b = [&](int idx) -> uint8_t {
		return (m_haveDump && idx >= 0 && idx < 0x200) ? m_dump.regs[idx] : 0;
	};
	auto wHiLo = [&](int idx) -> unsigned {
		return ((unsigned)b(idx) << 8) | (unsigned)b(idx + 1);
	};
	auto wLoHi = [&](int idx) -> unsigned {
		return (unsigned)b(idx) | ((unsigned)b(idx + 1) << 8);
	};

	int pitch = 0;
	int vol = 0;
	int pan = 0;
	int ctl = 0;
	if (profile == SASAMI_FMMON_KEYS_QSOUND) {
		const int base = ch * 16;
		pitch = (int)wHiLo(base + 4);
		vol = (int)wHiLo(base + 12) & 0xFFFF;
		pan = (int)wHiLo(base + 8) & 0xFF;
		ctl = (int)wHiLo(base + 6);
	} else if (profile == SASAMI_FMMON_KEYS_C352) {
		const int base = ch * 16;
		ctl = (int)wLoHi(base + 0);
		vol = (int)wLoHi(base + 2) & 0xFFFF;
		pitch = ((int)(wLoHi(base + 6) & 0xFF) << 8) | (int)(wLoHi(base + 4) & 0xFF);
		pan = ((int)wLoHi(base + 8) + (int)wLoHi(base + 10)) & 0xFF;
	} else if (profile == SASAMI_FMMON_KEYS_SEGAPCM) {
		/* Discrete: 0x42+8*ch … / 0xC6+8*ch. 315-5218: 0x02+8*ch / 0x86+8*ch. */
		const int dLo = 0x40 + ch * 8;
		const int dHi = 0xc0 + ch * 8;
		if (b(dLo + 2) | b(dLo + 3) | b(dLo + 7) | b(dHi + 6)) {
			vol = b(dLo + 2);
			pan = b(dLo + 3);
			pitch = b(dLo + 7);
			ctl = b(dHi + 6);
		} else {
			const int base = ch * 8;
			vol = b(base + 2);
			pan = b(base + 3);
			pitch = b(base + 7);
			ctl = b(base + 0x86);
		}
	} else if (profile == SASAMI_FMMON_KEYS_RF5C) {
		vol = b(0x00);
		pan = b(0x01);
		pitch = b(0x02) | (b(0x03) << 8);
		ctl = b(0x08);
	} else if (profile == SASAMI_FMMON_KEYS_OKI) {
		ctl = b(0);
		vol = b(0x10 + ch) ? 255 : 0;
		pan = ch;
		pitch = b(1);
	}

	const int live = FmMonIsLive();
	const int gate = live && m_haveDump && ch < SASAMI_FMMON_PCM_MAX && m_dump.pcmOn[ch];
	const BYTE fade = (live && ch < SASAMI_FMMON_PCM_MAX) ? m_fadePcm[ch] : (BYTE)0;
	const int lit = gate || fade >= 40;
	int midi = -1;
	if (lit && m_haveDump && ch < SASAMI_FMMON_PCM_MAX && m_dump.pcmNote[ch] != 0xFF)
		midi = (int)m_dump.pcmNote[ch];
	wchar_t note[16];
	FmFormatNoteName(midi, note, 16);

	const int titlePx = (std::max)(9, (std::min)(13, headH - 6));
	HFONT titleFont = FmMakeFont(titlePx);
	HFONT oldf = (HFONT)dc.SelectObject(titleFont);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(headBg);
	dc.SetTextColor(RGB(250, 230, 210));
	wchar_t title[32];
	_snwprintf_s(title, _TRUNCATE, L"%s%d", FmArcadePcmShort(profile), ch + 1);
	dc.TextOut(rc.left + pad, rc.top + 2, title);

	const int lamp = (std::max)(9, headH - 8);
	FmFillFade(dc, rc.right - pad - lamp, rc.top + 4, lamp, lamp,
		RGB(42, 44, 48), RGB(255, 150, 70), lit ? (BYTE)255 : fade);

	const int infoPx = (std::max)(8, (std::min)(12, (rc.Height() - headH) / 4));
	HFONT infoFont = FmMakeFont(infoPx);
	dc.SelectObject(infoFont);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(bodyBg);
	dc.SetTextColor(RGB(220, 225, 230));
	int y = rc.top + headH + pad;
	wchar_t line[64];
	_snwprintf_s(line, _TRUNCATE, L"%s  P:%04X", note, pitch & 0xFFFF);
	dc.TextOut(rc.left + pad, y, line);
	y += infoPx + 2;
	_snwprintf_s(line, _TRUNCATE, L"V:%04X  Pan:%02X", vol & 0xFFFF, pan & 0xFF);
	dc.TextOut(rc.left + pad, y, line);
	y += infoPx + 2;
	_snwprintf_s(line, _TRUNCATE, L"Ctl:%04X", ctl & 0xFFFF);
	dc.TextOut(rc.left + pad, y, line);

	CRect bar(rc.left + pad, rc.bottom - pad - (std::max)(6, infoPx / 2),
		rc.right - pad, rc.bottom - pad);
	dc.FillSolidRect(bar, barBg);
	FmFrameRect(dc, bar, RGB(80, 80, 88));
	int level = vol;
	if (profile == SASAMI_FMMON_KEYS_QSOUND || profile == SASAMI_FMMON_KEYS_C352)
		level = (std::min)(255, vol >> 8);
	if (level < 0) level = 0;
	if (level > 255) level = 255;
	CRect fill(bar.left + 1, bar.top + 1,
		bar.left + 1 + (bar.Width() - 2) * level / 255, bar.bottom - 1);
	dc.FillSolidRect(fill, lit ? RGB(255, 150, 70) : RGB(120, 95, 70));

	dc.SelectObject(oldf);
	FmDeleteFont(infoFont);
	FmDeleteFont(titleFont);
	dc.RestoreDC(savedDC);
}

void CFmMonitorDlg::DrawPanelsArea(CDC& dc)
{
	if (!m_layOk) return;
	if (IsArcadePcmDump() && HasViewPanels()) {
		dc.FillSolidRect(m_lay.rcPanels, FM_BG);
		const unsigned prof = ChipProfile();
		const int nCh = FmArcadePcmChannels(prof);
		const int cols = (nCh >= 16) ? 4 : (nCh >= 8 ? 4 : 2);
		const int rows = (nCh + cols - 1) / cols;
		const int gap = m_lay.gap;
		const int pw = (m_lay.rcPanels.Width() - gap * (cols - 1) - 8) / cols;
		const int ph = (m_lay.rcPanels.Height() - gap * (rows - 1) - 8) / rows;
		for (int i = 0; i < nCh; i++) {
			const int c = i % cols;
			const int r = i / cols;
			CRect pr(
				m_lay.fmX + 4 + c * (pw + gap),
				m_lay.topY + 4 + r * (ph + gap),
				m_lay.fmX + 4 + c * (pw + gap) + pw,
				m_lay.topY + 4 + r * (ph + gap) + ph);
			DrawArcadePcmChPanel(dc, pr, i, prof);
		}
		m_panelDirtyMask = 0;
		return;
	}
	if (IsOplDump() && HasViewPanels()) {
		dc.FillSolidRect(m_lay.rcPanels, FM_BG);
		const int nCh = (ChipProfile() == SASAMI_FMMON_KEYS_OPL3) ? 18 : 9;
		const int cols = (nCh > 9) ? 6 : 3;
		const int rows = (nCh + cols - 1) / cols;
		const int gap = m_lay.gap;
		const int pw = (m_lay.rcPanels.Width() - gap * (cols - 1) - 8) / cols;
		const int ph = (m_lay.rcPanels.Height() - gap * (rows - 1) - 8) / rows;
		for (int i = 0; i < nCh; i++) {
			const int c = i % cols;
			const int r = i / cols;
			CRect pr(
				m_lay.fmX + 4 + c * (pw + gap),
				m_lay.topY + 4 + r * (ph + gap),
				m_lay.fmX + 4 + c * (pw + gap) + pw,
				m_lay.topY + 4 + r * (ph + gap) + ph);
			DrawOplChPanel(dc, pr, i);
		}
		m_panelDirtyMask = 0;
		return;
	}
	if (IsOpmDump() && HasViewPanels()) {
		dc.FillSolidRect(m_lay.rcPanels, FM_BG);
		const int cols = 4;
		const int rows = 2;
		const int gap = m_lay.gap;
		const int pw = (m_lay.rcPanels.Width() - gap * (cols - 1) - 8) / cols;
		const int ph = (m_lay.rcPanels.Height() - gap * (rows - 1) - 8) / rows;
		for (int i = 0; i < 8; i++) {
			const int c = i % cols;
			const int r = i / cols;
			CRect pr(
				m_lay.fmX + 4 + c * (pw + gap),
				m_lay.topY + 4 + r * (ph + gap),
				m_lay.fmX + 4 + c * (pw + gap) + pw,
				m_lay.topY + 4 + r * (ph + gap) + ph);
			DrawOpmChPanel(dc, pr, i);
		}
		m_panelDirtyMask = 0;
		return;
	}
	if (IsMsxDump() && HasViewPanels()
		&& (MsxDevMask() & SASAMI_FMMON_DEV_OPLL)) {
		dc.FillSolidRect(m_lay.rcPanels, FM_BG);
		const int cols = 3;
		const int rows = 3;
		const int gap = m_lay.gap;
		const int pw = (m_lay.rcPanels.Width() - gap * (cols - 1) - 8) / cols;
		const int ph = (m_lay.rcPanels.Height() - gap * (rows - 1) - 8) / rows;
		for (int i = 0; i < 9; i++) {
			const int c = i % cols;
			const int r = i / cols;
			CRect pr(
				m_lay.fmX + 4 + c * (pw + gap),
				m_lay.topY + 4 + r * (ph + gap),
				m_lay.fmX + 4 + c * (pw + gap) + pw,
				m_lay.topY + 4 + r * (ph + gap) + ph);
			DrawOpllChPanel(dc, pr, i);
		}
		m_panelDirtyMask = 0;
		return;
	}
	if (IsMsxDump() && !(MsxDevMask() & SASAMI_FMMON_DEV_OPLL)
		&& !PreferOpnaShell()) {
		dc.FillSolidRect(m_lay.rcPanels, FM_BG);
		CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(140, 145, 155));
		dc.TextOut(m_lay.fmX + 8, m_lay.topY + 8,
			L"MSX: no OPLL - panels N/A (PSG/SCC keys)");
		dc.SelectObject(old);
		m_panelDirtyMask = 0;
		return;
	}
	/* OPNA/OPN パネル（起動直後・MIDI keys-only・通常 OPN(A)） */
	BYTE mask = m_panelDirtyMask;
	const int fmN = FmRows();
	const int fmMaskBits = (fmN >= 6) ? 0x3F : ((1 << fmN) - 1);
	if (PreferOpnaShell() || !HasViewPanels() || KeysOnly())
		mask = (BYTE)fmMaskBits;
	if (mask == 0) mask = (BYTE)fmMaskBits;
	if (mask == (BYTE)fmMaskBits)
		dc.FillSolidRect(m_lay.rcPanels, FM_BG);
	const int cols = (fmN <= 2) ? (std::max)(1, fmN) : ((fmN <= 4) ? 2 : 3);
	for (int i = 0; i < fmN && i < 6; i++) {
		if (!(mask & (1 << i))) continue;
		const int c = i % cols;
		const int r = i / cols;
		CRect pr(m_lay.fmX + c * (m_lay.pw + m_lay.gap), m_lay.topY + r * (m_lay.ph + m_lay.gap),
			m_lay.fmX + c * (m_lay.pw + m_lay.gap) + m_lay.pw,
			m_lay.topY + r * (m_lay.ph + m_lay.gap) + m_lay.ph);
		if (mask != (BYTE)fmMaskBits)
			dc.FillSolidRect(pr, FM_BG);
		DrawFmChPanel(dc, pr, i);
	}
	m_panelDirtyMask = 0;
}

void CFmMonitorDlg::DrawKeysArea(CDC& dc)
{
	if (!m_layOk) return;
	dc.FillSolidRect(m_lay.rcKeys, FM_BG);
	DrawChannelKeys(dc, m_lay.pad, m_lay.keysY, m_lay.keysW, m_lay.rowH, m_lay.keyH, m_lay.labelW);
}

void CFmMonitorDlg::ComposeFrame(CDC& dc, int w, int h)
{
	if (w < 80 || h < 80) {
		dc.FillSolidRect(0, 0, w, h, FM_BG);
		return;
	}
	const int pcmNow = PcmRows();
	const int exNow = ExRows();
	const int fmNow = FmRows();
	const int ssgNow = SsgRows();
	const int needLay = !m_layOk || m_lay.w != w || m_lay.h != h || m_fullDraw
		|| m_lay.pcmRows != pcmNow || m_lay.exRows != exNow
		|| m_lay.fmRows != fmNow || m_lay.ssgRows != ssgNow;
	if (needLay)
		ComputeLayout(w, h);

	if (m_fullDraw || needLay) {
		dc.FillSolidRect(0, 0, w, h, FM_BG);
		m_panelDirtyMask = 0x3F;
		DrawHead(dc);
		DrawHexArea(dc);
		DrawPanelsArea(dc);
		DrawKeysArea(dc);
		m_fullDraw = 0;
		m_dirtyHead = m_dirtyHex = m_dirtyPanels = m_dirtyKeys = 0;
		return;
	}
	if (m_dirtyHead) { DrawHead(dc); m_dirtyHead = 0; }
	if (m_dirtyHex) { DrawHexArea(dc); m_dirtyHex = 0; }
	if (m_dirtyPanels) { DrawPanelsArea(dc); m_dirtyPanels = 0; }
	if (m_dirtyKeys) { DrawKeysArea(dc); m_dirtyKeys = 0; }
}

void CFmMonitorDlg::ApplyDump(const SasamiFmMonDump& d)
{
	const int songChanged = (m_lastSong[0] == 0)
		|| (wcscmp(m_lastSong, d.sourcePath) != 0)
		|| (m_haveDump && d.curSample + 10000 < m_lastCurSample);
	if (songChanged) {
		memset(m_touched, 0, sizeof(m_touched));
		memset(m_fadeKey, 0, sizeof(m_fadeKey));
		memset(m_fadeEx, 0, sizeof(m_fadeEx));
		memset(m_fadeSsg, 0, sizeof(m_fadeSsg));
		memset(m_fadePcm, 0, sizeof(m_fadePcm));
		memset(m_fadeRzmPad, 0, sizeof(m_fadeRzmPad));
		wcsncpy_s(m_lastSong, d.sourcePath, _TRUNCATE);
		m_layOk = 0;
		m_fullDraw = 1;
		m_dirtyHead = 1;
		m_dirtyKeys = 1;
		m_panelDirtyMask = 0x3F;
		m_dirtyPanels = 1;
	}

	int chgHex = 0, chgKeys = 0;
	BYTE panelMask = 0;
	const int keysOnly = (d.version >= 6 && (d.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY)) ? 1 : 0;
	const int arcadeRegs = (keysOnly && d.version >= 6
		&& FmIsArcadePcmProfile((unsigned)d.pad6[1])
		&& ((unsigned)d.pad6[2] & SASAMI_FMMON_VIEW_REGS)) ? 1 : 0;
	const int pcatAuxRegs = (keysOnly && d.version >= 6
		&& ((unsigned)d.pad6[2] & SASAMI_FMMON_VIEW_REGS)
		&& d.titleSjis[0] && strstr(d.titleSjis, "PC/AT")) ? 1 : 0;
	const int softRegs = (arcadeRegs || pcatAuxRegs) ? 1 : 0;
	if (m_haveDump) {
		if (!keysOnly || softRegs) for (int i = 0; i < 0x200; i++) {
			/* 値変化した番地だけフェード（同一値の sticky writeBits では光らせない） */
			if (d.regs[i] != m_dump.regs[i]) {
				FmBump(m_fade[i]);
				m_touched[i] = 1;
				chgHex = 1;
			}
			if (d.regs[i] != 0)
				m_touched[i] = 1;
		}
		/* チャンネル単位: ALG(B0) / AMS·PMS·PAN(B4) / Fnum / オペレータ / keyOn */
		const int ym2610 = (d.padHit == 6) ? 1 : 0;
		const int fmChN = ym2610 ? 4 : 6;
		for (int ch = 0; ch < fmChN; ch++) {
			int bank, slot;
			if (ym2610) {
				static const int kB[4] = { 0, 0, 0x100, 0x100 };
				static const int kS[4] = { 1, 2, 0, 1 };
				bank = kB[ch];
				slot = kS[ch];
			} else {
				bank = (ch < 3) ? 0 : 0x100;
				slot = (ch < 3) ? ch : (ch - 3);
			}
			int dirty = 0;
			if (d.keyOnFm[ch] != m_dump.keyOnFm[ch]) {
				dirty = 1;
				chgKeys = 1;
			}
			if (d.version >= 6 && d.keyMidi[ch] != m_dump.keyMidi[ch]) {
				dirty = 1;
				chgKeys = 1;
				if (d.keyOnFm[ch])
					FmBump(m_fadeKey[ch]);
			}
			if (d.keyOnFm[ch] && !m_dump.keyOnFm[ch])
				FmBump(m_fadeKey[ch]);
			/* v4: key-on 書き込み累積（同一状態の再トリガも拾う） */
			if (d.version >= 4 && d.keyOnHitCnt[ch] != m_dump.keyOnHitCnt[ch]) {
				FmBump(m_fadeKey[ch]);
				dirty = 1;
				chgKeys = 1;
			}
			const uint8_t b0 = d.regs[bank + 0xB0 + slot];
			const uint8_t pb0 = m_dump.regs[bank + 0xB0 + slot];
			const uint8_t b4 = d.regs[bank + 0xB4 + slot];
			const uint8_t pb4 = m_dump.regs[bank + 0xB4 + slot];
			if (b0 != pb0 || b4 != pb4)
				dirty = 1; /* ALG 図・FB・AMS/PMS/PAN */
			if (d.regs[bank + 0xA4 + slot] != m_dump.regs[bank + 0xA4 + slot]
				|| d.regs[bank + 0xA0 + slot] != m_dump.regs[bank + 0xA0 + slot]) {
				FmBump(m_fadeKey[ch]);
				chgKeys = 1;
				dirty = 1;
			}
			for (int op = 0; op < 4 && !dirty; op++) {
				const int o = op * 4 + slot;
				if (d.regs[bank + 0x30 + o] != m_dump.regs[bank + 0x30 + o]
					|| d.regs[bank + 0x40 + o] != m_dump.regs[bank + 0x40 + o]
					|| d.regs[bank + 0x50 + o] != m_dump.regs[bank + 0x50 + o]
					|| d.regs[bank + 0x60 + o] != m_dump.regs[bank + 0x60 + o]
					|| d.regs[bank + 0x70 + o] != m_dump.regs[bank + 0x70 + o]
					|| d.regs[bank + 0x80 + o] != m_dump.regs[bank + 0x80 + o]
					|| d.regs[bank + 0x90 + o] != m_dump.regs[bank + 0x90 + o])
					dirty = 1;
			}
			if (dirty)
				panelMask = (BYTE)(panelMask | (1 << ch));
		}
		/* 部分パネル描画の取り残しで CH ごとに「合ってる／ずれてる」に見えるのを防ぐ */
		if (panelMask != 0)
			panelMask = 0x3F;
		for (int i = 0; i < 3; i++) {
			if (d.keyOnEx[i] != m_dump.keyOnEx[i]) chgKeys = 1;
			if (d.keyOnEx[i] && !m_dump.keyOnEx[i]) FmBump(m_fadeEx[i]);
			if (d.version >= 6 && d.keyOnExHitCnt[i] != m_dump.keyOnExHitCnt[i]) {
				FmBump(m_fadeEx[i]);
				chgKeys = 1;
			}
		}
		for (int i = 0; i < 3; i++) {
			if (d.ssgOn[i] != m_dump.ssgOn[i])
				chgKeys = 1;
			if (d.ssgOn[i] && !m_dump.ssgOn[i])
				FmBump(m_fadeSsg[i]);
			if (d.version >= 4 && d.ssgHitCnt[i] != m_dump.ssgHitCnt[i]) {
				FmBump(m_fadeSsg[i]);
				chgKeys = 1;
			}
			if (d.version >= 6 && d.ssgMidi[i] != m_dump.ssgMidi[i]) {
				FmBump(m_fadeSsg[i]);
				chgKeys = 1;
			}
			if (d.regs[i * 2] != m_dump.regs[i * 2]
				|| d.regs[i * 2 + 1] != m_dump.regs[i * 2 + 1]) {
				FmBump(m_fadeSsg[i]);
				chgKeys = 1;
			}
		}
		const int pcmN = (d.pcmCount < SASAMI_FMMON_PCM_MAX) ? d.pcmCount : SASAMI_FMMON_PCM_MAX;
		if (d.pcmCount != m_dump.pcmCount) {
			/* PCM 行の増減は top/keys 分割が変わる → リサイズ相当の全再レイアウト */
			chgKeys = 1;
			m_layOk = 0;
			m_fullDraw = 1;
		}
		for (int i = 0; i < pcmN; i++) {
			if (d.pcmOn[i] && !m_dump.pcmOn[i])
				FmBump(m_fadePcm[i]);
			else if (d.pcmOn[i] && d.pcmNote[i] != m_dump.pcmNote[i])
				FmBump(m_fadePcm[i]);
			/* keys-only: パック済み hit（短い on→off で gate が最終 off でも点灯） */
			if ((d.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY)
				&& FmKeysOnlyPackedHit(d, i) != FmKeysOnlyPackedHit(m_dump, i))
				FmBump(m_fadePcm[i]);
			if (d.pcmOn[i] != m_dump.pcmOn[i] || d.pcmNote[i] != m_dump.pcmNote[i]
				|| ((d.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY)
					&& FmKeysOnlyPackedHit(d, i) != FmKeysOnlyPackedHit(m_dump, i)))
				chgKeys = 1;
		}
		for (int i = 0; i < 6; i++) {
			const int was = (m_dump.rhythmKey >> i) & 1;
			const int nowR = (d.rhythmKey >> i) & 1;
			if (nowR != was) chgKeys = 1;
			if (nowR && !was)
				FmBump(m_fadeRzmPad[i]);
		}
		/* v3: 累積ヒット差分 — ファイル上書きで pulse を見失っても拾える */
		int hitBump = 0;
		if (d.version >= 3) {
			for (int i = 0; i < 6; i++) {
				if (d.rhythmHitCnt[i] != m_dump.rhythmHitCnt[i]) {
					FmBump(m_fadeRzmPad[i]);
					hitBump = 1;
				}
			}
		}
		/* 同一 0x10 値の連打 / 区間 pulse */
		if (hitBump || (d.rhythmPulse & 0x3F)) {
			FmBump(m_fade[0x10]);
			m_touched[0x10] = 1;
			chgHex = 1;
			chgKeys = 1;
			if (!hitBump) {
				for (int i = 0; i < 6; i++) {
					if (d.rhythmPulse & (1 << i))
						FmBump(m_fadeRzmPad[i]);
				}
			}
		} else if (d.regs[0x10] != m_dump.regs[0x10] && !(d.regs[0x10] & 0x80)) {
			for (int i = 0; i < 6; i++) {
				if (d.regs[0x10] & (1 << i))
					FmBump(m_fadeRzmPad[i]);
			}
			chgKeys = 1;
			chgHex = 1;
		}
		if (d.seq != m_dump.seq || d.sampleRate != m_dump.sampleRate)
			m_dirtyHead = 1;
	} else {
		/* 初回 dump: hex は触るが、停止中／開始直後のキー点灯は出さない */
		memset(m_fade, 0, sizeof(m_fade));
		memset(m_fadeKey, 0, sizeof(m_fadeKey));
		memset(m_fadeEx, 0, sizeof(m_fadeEx));
		memset(m_fadeSsg, 0, sizeof(m_fadeSsg));
		memset(m_fadePcm, 0, sizeof(m_fadePcm));
		memset(m_fadeRzmPad, 0, sizeof(m_fadeRzmPad));
		for (int i = 0; i < 0x200; i++) {
			if (d.regs[i] != 0)
				m_touched[i] = 1;
		}
		if (FmMonIsLive()) {
			for (int i = 0; i < 6; i++)
				if (d.keyOnFm[i]) FmBump(m_fadeKey[i]);
			for (int i = 0; i < 3; i++)
				if (d.ssgOn[i]) FmBump(m_fadeSsg[i]);
			for (int i = 0; i < 6; i++)
				if ((d.rhythmKey >> i) & 1) FmBump(m_fadeRzmPad[i]);
			if (d.rhythmPulse & 0x3F) {
				FmBump(m_fade[0x10]);
				for (int i = 0; i < 6; i++) {
					if (d.rhythmPulse & (1 << i))
						FmBump(m_fadeRzmPad[i]);
				}
			}
			const int pcmN = (d.pcmCount < SASAMI_FMMON_PCM_MAX) ? d.pcmCount : SASAMI_FMMON_PCM_MAX;
			for (int i = 0; i < pcmN; i++)
				if (d.pcmOn[i]) FmBump(m_fadePcm[i]);
		}
		chgHex = chgKeys = 1;
		panelMask = 0x3F;
		m_dirtyHead = 1;
		/* 直前曲の PCM 行数レイアウトが残っていることがある */
		m_layOk = 0;
		m_fullDraw = 1;
	}

	m_prev = m_dump;
	m_dump = d;
	m_lastSeq = d.seq;
	m_lastCurSample = d.curSample;
	m_haveDump = 1;
	if ((!keysOnly || softRegs) && chgHex) m_dirtyHex = 1;
	if (!keysOnly && panelMask) {
		m_panelDirtyMask = (BYTE)(m_panelDirtyMask | panelMask);
		m_dirtyPanels = 1;
	}
	if (softRegs && (chgHex || chgKeys || panelMask)) {
		m_panelDirtyMask = 0x3F;
		m_dirtyPanels = 1;
	}
	if (keysOnly && PreferOpnaShell()) {
		/* MIDI等: OPNA 殻を出す。初回/曲切替は fullDraw、通常更新は hex/panels 据え置き */
		if (m_fullDraw || chgHex) {
			m_dirtyHex = 1;
			m_panelDirtyMask = 0x3F;
			m_dirtyPanels = 1;
		}
	}
	if (chgKeys) m_dirtyKeys = 1;
}

void CFmMonitorDlg::ResetDumpSync()
{
	/* 曲切替で writer が live/ring を作り直すと、常駐ハンドルは旧 inode のまま */
	FmInvalidateRdHandles();
	m_histN = 0;
	m_histHead = 0;
	m_ringGenLast = 0;
	m_lastCurSample = 0;
	m_lastHeardSamp = 0;
	m_heardAnchor = 0;
	m_lastSeq = 0;
	m_readFail = 0;
	m_haveDump = 0;
	m_lastSong[0] = 0;
	/* m_playIdent は残す（同じ曲で毎フレ Reset し続けない） */
	memset(&m_dump, 0, sizeof(m_dump));
	memset(&m_prev, 0, sizeof(m_prev));
	m_dirtyHead = m_dirtyHex = m_dirtyPanels = m_dirtyKeys = 1;
	m_panelDirtyMask = 0x3F;
}

void CFmMonitorDlg::PushHistDump(const SasamiFmMonDump& d)
{
	if (FmMonIsLive() && !FmDumpMatchesPlay(d))
		return;
	if (d.sourcePath[0]) {
		if ((m_lastSong[0] && wcscmp(m_lastSong, d.sourcePath) != 0)
			|| (m_haveDump && m_dump.sourcePath[0]
				&& wcscmp(m_dump.sourcePath, d.sourcePath) != 0)) {
			ResetDumpSync();
		}
	}
	if (m_haveDump && d.curSample + 10000 < m_lastCurSample)
		ResetDumpSync();

	if (m_histN > 0) {
		const int lastI = (m_histHead + m_histN - 1) % HIST_MAX;
		if (d.seq == m_hist[lastI].seq && d.curSample == m_hist[lastI].curSample
			&& d.rhythmPulse == m_hist[lastI].rhythmPulse
			&& memcmp(d.keyOnFm, m_hist[lastI].keyOnFm, 6) == 0
			&& memcmp(d.ssgOn, m_hist[lastI].ssgOn, 3) == 0
			&& (d.version < 6 || (memcmp(d.keyMidi, m_hist[lastI].keyMidi, 6) == 0
				&& memcmp(d.exMidi, m_hist[lastI].exMidi, 3) == 0
				&& memcmp(d.ssgMidi, m_hist[lastI].ssgMidi, 3) == 0
				&& memcmp(d.keyOnEx, m_hist[lastI].keyOnEx, 3) == 0))
			&& (d.version < 3 || memcmp(d.rhythmHitCnt, m_hist[lastI].rhythmHitCnt, 6) == 0)
			&& (d.version < 4 || (memcmp(d.keyOnHitCnt, m_hist[lastI].keyOnHitCnt, 6) == 0
				&& memcmp(d.ssgHitCnt, m_hist[lastI].ssgHitCnt, 3) == 0))
			&& (d.version < 5 || memcmp(d.regWriteBits, m_hist[lastI].regWriteBits, 64) == 0)
			&& (d.version < 6 || memcmp(d.keyOnExHitCnt, m_hist[lastI].keyOnExHitCnt, 3) == 0)
			&& (d.pcmCount == m_hist[lastI].pcmCount
				&& memcmp(d.pcmOn, m_hist[lastI].pcmOn, SASAMI_FMMON_PCM_MAX) == 0
				&& memcmp(d.pcmNote, m_hist[lastI].pcmNote, SASAMI_FMMON_PCM_MAX) == 0))
			return;
	}
	/* 満杯で最古＝表示中を潰さない。SOFT 超えたら最古を捨ててから追加 */
	while (m_histN >= HIST_SOFT) {
		m_histHead = (m_histHead + 1) % HIST_MAX;
		m_histN--;
	}
	const int i = (m_histHead + m_histN) % HIST_MAX;
	m_hist[i] = d;
	m_histSamp[i] = d.curSample;
	m_histN++;
}

/* 可聴より十分古い履歴だけ捨てる（新しい dump は捨てない） */
void CFmMonitorDlg::TrimHistForHeard(uint64_t heard, uint32_t rate)
{
	if (m_histN <= 0 || rate == 0) return;
	const uint64_t pastPad = (uint64_t)rate / 5; /* ~200ms */
	while (m_histN > 1) {
		if (m_histSamp[m_histHead] + pastPad >= heard)
			break;
		const int isCur = (m_haveDump
			&& m_hist[m_histHead].seq == m_lastSeq
			&& m_hist[m_histHead].curSample == m_lastCurSample) ? 1 : 0;
		if (isCur)
			break;
		m_histHead = (m_histHead + 1) % HIST_MAX;
		m_histN--;
	}
}

int CFmMonitorDlg::PollDump()
{
	struct Cb { CFmMonitorDlg* self; int got; int seen; } cb = { this, 0, 0 };
	auto thunk = [](const SasamiFmMonDump& d, void* p) {
		Cb* c = (Cb*)p;
		c->seen++;
		const int nBefore = c->self->m_histN;
		c->self->PushHistDump(d);
		if (c->self->m_histN > nBefore)
			c->got = 1;
	};

	const uint32_t genBefore = m_ringGenLast;
	const int ringOk = FmDrainRingSlots(&m_ringGenLast, thunk, &cb) ? 1 : 0;
	int got = (cb.got || m_histN > 0) ? 1 : 0;

	/* ring は進んだが照合で全部落ちた → genLast だけ先行して永久に前曲 live へ落ちるのを防ぐ */
	if (ringOk && cb.seen > 0 && !cb.got && m_histN <= 0 && m_ringGenLast != genBefore) {
		m_ringGenLast = (m_ringGenLast > (uint32_t)SASAMI_FMMON_RING)
			? (m_ringGenLast - (uint32_t)SASAMI_FMMON_RING) : 0;
		FmInvalidateRdHandles();
	}

	/* リングファイルだけ残って live を読まない／gen リセット後に取りこぼすのを防ぐ */
	if (!cb.got) {
		SasamiFmMonDump d;
		int gotOne = 0;
		if (FmReadLatestRingDump(&d) && FmDumpMatchesPlay(d)) {
			gotOne = 1;
		} else if (FmReadDump(&d) && FmDumpMatchesPlay(d)) {
			gotOne = 1;
		}
		if (gotOne) {
			m_readFail = 0;
			const int nBefore = m_histN;
			PushHistDump(d);
			if (m_histN > nBefore)
				got = 1;
		} else if (!ringOk) {
			if (m_haveDump) {
				if (++m_readFail > 45) {
					m_haveDump = 0;
					ResetDumpSync();
					m_fullDraw = 1;
					m_panelDirtyMask = 0x3F;
					m_dirtyHead = m_dirtyHex = m_dirtyPanels = m_dirtyKeys = 1;
				}
			}
			return 0;
		}
	} else {
		m_readFail = 0;
	}
	if (!got || m_histN <= 0) return 0;

	uint32_t rate = 44100;
	{
		const int li = (m_histHead + m_histN - 1) % HIST_MAX;
		if (m_hist[li].sampleRate > 0)
			rate = m_hist[li].sampleRate;
	}
	const uint64_t heard = HeardSample(rate);
	TrimHistForHeard(heard, rate);
	if (m_histN <= 0) return 0;

	/* dump.curSample = その tick の PCM 開始位置 / heard = 可聴位置 */
	int bestN = -1;
	for (int n = 0; n < m_histN; n++) {
		const int i = (m_histHead + n) % HIST_MAX;
		if (m_histSamp[i] <= heard)
			bestN = n;
	}
	if (bestN < 0) {
		if (!FmMonIsLive())
			return m_haveDump ? 1 : 0;
		/* 可聴より先だけ（起動直後・ラグ中）→ 最古を出して始動。出さないと .fpy が無描画 */
		uint64_t minS = UINT64_MAX;
		for (int n = 0; n < m_histN; n++) {
			const int i = (m_histHead + n) % HIST_MAX;
			if (m_histSamp[i] < minS) {
				minS = m_histSamp[i];
				bestN = n;
			}
		}
		if (bestN < 0)
			return m_haveDump ? 1 : 0;
	}

	int curN = -1;
	for (int n = 0; n < m_histN; n++) {
		const int i = (m_histHead + n) % HIST_MAX;
		if (m_haveDump
			&& m_hist[i].seq == m_lastSeq
			&& m_hist[i].curSample == m_lastCurSample) {
			curN = n;
			break;
		}
	}
	/* 表示中が hist から落ちた場合は、可聴位置の dump へ単調に進める */
	if (curN < 0 && m_haveDump) {
		for (int n = 0; n <= bestN; n++) {
			const int i = (m_histHead + n) % HIST_MAX;
			if (m_histSamp[i] >= m_lastCurSample) {
				curN = n - 1; /* fromN = curN+1 が n になる */
				break;
			}
		}
	}

	int nextN = bestN;
	if (curN >= 0 && bestN <= curN)
		return 1;
	const int fromN = (curN < 0) ? nextN : (curN + 1);
	if (fromN > nextN) return 1;

	int applied = 0;
	const int pending = nextN - fromN + 1;
	/* 2枚以上は最新の gate を残し、中間の hit だけ畳む。
	   gate を OR すると張り付き、連打 Apply は最終 off で短音符が消える */
	if (pending > 1) {
		SasamiFmMonDump merged = m_hist[(m_histHead + nextN) % HIST_MAX];
		for (int n = fromN; n < nextN; n++) {
			const SasamiFmMonDump& s = m_hist[(m_histHead + n) % HIST_MAX];
			for (int i = 0; i < 6; i++) {
				if (s.keyOnHitCnt[i] > merged.keyOnHitCnt[i])
					merged.keyOnHitCnt[i] = s.keyOnHitCnt[i];
				if (s.keyOnFm[i] && s.keyMidi[i] != 0xFF)
					merged.keyMidi[i] = s.keyMidi[i];
				/* hitCnt が増えていない短パルスでもフェードを起こす */
				if (s.keyOnFm[i] && !merged.keyOnFm[i]
					&& s.keyOnHitCnt[i] <= m_dump.keyOnHitCnt[i]
					&& merged.keyOnHitCnt[i] <= m_dump.keyOnHitCnt[i])
					merged.keyOnHitCnt[i] = (uint8_t)(m_dump.keyOnHitCnt[i] + 1);
				merged.rhythmHitCnt[i] = (s.rhythmHitCnt[i] > merged.rhythmHitCnt[i])
					? s.rhythmHitCnt[i] : merged.rhythmHitCnt[i];
			}
			for (int i = 0; i < 3; i++) {
				if (s.ssgHitCnt[i] > merged.ssgHitCnt[i])
					merged.ssgHitCnt[i] = s.ssgHitCnt[i];
				if (s.ssgOn[i] && s.ssgMidi[i] != 0xFF)
					merged.ssgMidi[i] = s.ssgMidi[i];
				if (s.ssgOn[i] && !merged.ssgOn[i]
					&& s.ssgHitCnt[i] <= m_dump.ssgHitCnt[i]
					&& merged.ssgHitCnt[i] <= m_dump.ssgHitCnt[i])
					merged.ssgHitCnt[i] = (uint8_t)(m_dump.ssgHitCnt[i] + 1);
				if (s.keyOnExHitCnt[i] > merged.keyOnExHitCnt[i])
					merged.keyOnExHitCnt[i] = s.keyOnExHitCnt[i];
				if (s.keyOnEx[i] && s.exMidi[i] != 0xFF)
					merged.exMidi[i] = s.exMidi[i];
				if (s.keyOnEx[i] && !merged.keyOnEx[i]
					&& s.keyOnExHitCnt[i] <= m_dump.keyOnExHitCnt[i]
					&& merged.keyOnExHitCnt[i] <= m_dump.keyOnExHitCnt[i])
					merged.keyOnExHitCnt[i] = (uint8_t)(m_dump.keyOnExHitCnt[i] + 1);
			}
			for (int i = 0; i < SASAMI_FMMON_PCM_MAX; i++) {
				if (s.pcmOn[i])
					merged.pcmNote[i] = s.pcmNote[i];
				/* 短い on→最終 off: hit を合成して fadePcm が点く */
				if (s.pcmOn[i] && !merged.pcmOn[i]
					&& (merged.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY)) {
					const uint8_t sh = FmKeysOnlyPackedHit(s, i);
					const uint8_t mh = FmKeysOnlyPackedHit(merged, i);
					const uint8_t dh = FmKeysOnlyPackedHit(m_dump, i);
					if (sh > mh)
						FmKeysOnlySetPackedHit(merged, i, sh);
					else if (sh <= dh && mh <= dh)
						FmKeysOnlySetPackedHit(merged, i, (uint8_t)(dh + 1));
				} else if (s.pcmOn[i] && (merged.dumpFlags & SASAMI_FMMON_FLAG_KEYSONLY)) {
					const uint8_t sh = FmKeysOnlyPackedHit(s, i);
					if (sh > FmKeysOnlyPackedHit(merged, i))
						FmKeysOnlySetPackedHit(merged, i, sh);
				}
			}
			merged.rhythmPulse = (uint8_t)(merged.rhythmPulse | s.rhythmPulse);
			merged.rhythmKey = (uint8_t)(merged.rhythmKey | s.rhythmKey);
		}
		ApplyDump(merged);
		applied = 1;
		if (nextN > 0 && nextN < m_histN) {
			m_histHead = (m_histHead + nextN) % HIST_MAX;
			m_histN -= nextN;
		}
		return 1;
	}

	{
		const SasamiFmMonDump& show = m_hist[(m_histHead + nextN) % HIST_MAX];
		if (!(m_haveDump && show.seq == m_lastSeq
			&& show.curSample == m_lastCurSample)) {
			ApplyDump(show);
			applied = 1;
		}
	}

	if (nextN > 0 && nextN < m_histN) {
		m_histHead = (m_histHead + nextN) % HIST_MAX;
		m_histN -= nextN;
	}
	return applied || m_haveDump;
}

void CFmMonitorDlg::TickFades()
{
	/* フェードは hex/鍵盤のみ。パネルはフェードを使わないので触らない */
	auto tickQ = [](BYTE& g) -> int {
		if (!g) return 0;
		const BYTE before = (BYTE)(g >> 4);
		FmTickGlow(g);
		const BYTE after = (BYTE)(g >> 4);
		return before != after || g == 0;
	};
	int hex = 0, keys = 0;
	for (int i = 0; i < 0x200; i++)
		if (tickQ(m_fade[i])) hex = 1;
	for (int i = 0; i < 6; i++)
		if (tickQ(m_fadeKey[i])) keys = 1;
	for (int i = 0; i < 3; i++)
		if (tickQ(m_fadeEx[i])) keys = 1;
	for (int i = 0; i < 3; i++)
		if (tickQ(m_fadeSsg[i])) keys = 1;
	for (int i = 0; i < SASAMI_FMMON_PCM_MAX; i++)
		if (tickQ(m_fadePcm[i])) keys = 1;
	for (int i = 0; i < 6; i++)
		if (tickQ(m_fadeRzmPad[i])) keys = 1;
	if (hex) m_dirtyHex = 1;
	if (keys) m_dirtyKeys = 1;
}

void CFmMonitorDlg::InvalidateDirtyRegions()
{
	if (!(m_fullDraw || m_dirtyHead || m_dirtyHex || m_dirtyPanels || m_dirtyKeys))
		return;
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (m_fullDraw || !m_layOk) {
		Invalidate(FALSE);
		return;
	}
	CRect acc(0, 0, 0, 0);
	auto add = [&](const CRect& r) {
		CRect c = r;
		c.OffsetRect(0, capH);
		if (acc.IsRectEmpty()) acc = c;
		else acc.UnionRect(&acc, &c);
	};
	if (m_dirtyHead) add(m_lay.rcHead);
	if (m_dirtyHex) add(m_lay.rcHex);
	if (m_dirtyPanels) add(m_lay.rcPanels);
	if (m_dirtyKeys) add(m_lay.rcKeys);
	if (!acc.IsRectEmpty())
		InvalidateRect(&acc, FALSE);
	else
		Invalidate(FALSE);
}

void CFmMonitorDlg::PumpSyncNow()
{
	if (!::IsWindow(GetSafeHwnd()) || !IsWindowVisible() || IsIconic())
		return;
	const int live = FmMonIsLive();
	if (m_lastPlayy != 0 && live == 0) {
		memset(m_fadeKey, 0, sizeof(m_fadeKey));
		memset(m_fadeEx, 0, sizeof(m_fadeEx));
		memset(m_fadeSsg, 0, sizeof(m_fadeSsg));
		memset(m_fadePcm, 0, sizeof(m_fadePcm));
		memset(m_fadeRzmPad, 0, sizeof(m_fadeRzmPad));
		/* 表示上の gate も落とす（描画側でも live 判定するが残骸を残さない） */
		memset(m_dump.keyOnFm, 0, sizeof(m_dump.keyOnFm));
		memset(m_dump.keyOnEx, 0, sizeof(m_dump.keyOnEx));
		memset(m_dump.ssgOn, 0, sizeof(m_dump.ssgOn));
		memset(m_dump.pcmOn, 0, sizeof(m_dump.pcmOn));
		m_dump.rhythmKey = 0;
		m_dump.rhythmPulse = 0;
		m_dirtyKeys = 1;
		m_panelDirtyMask = 0x3F;
		m_dirtyPanels = 1;
		m_fullDraw = 1;
	}
	if (m_lastPlayy == 0 && live == 1)
		ResetDumpSync();
	m_lastPlayy = live;
	if (live) {
		/* 再生曲切替（filen）で必ず hist/ring 位置を捨てる。前曲 dump が残るのを防ぐ */
		wchar_t playId[260];
		FmPlayIdentity(playId, 260);
		if (playId[0] && (m_playIdent[0] == 0 || _wcsicmp(m_playIdent, playId) != 0)) {
			wcsncpy_s(m_playIdent, playId, _TRUNCATE);
			ResetDumpSync();
		}
		if (m_haveDump && !FmDumpMatchesPlay(m_dump))
			ResetDumpSync();
		PollDump();
	} else {
		m_playIdent[0] = 0;
	}
	TickFades();
	InvalidateDirtyRegions();
}

void CFmMonitorDlg::IdlePulse()
{
	if (!::IsWindow(GetSafeHwnd()) || !IsWindowVisible() || IsIconic())
		return;
	const ULONGLONG now = GetTickCount64();
	if (now - m_lastPollMs < 8)
		return;
	m_lastPollMs = now;
	PumpSyncNow();
	CRect ur;
	if (GetUpdateRect(&ur, FALSE))
		UpdateWindow();
}

void CFmMonitorDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(&rect);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int w = rect.Width();
	const int h = rect.Height() - capH;
	if (w <= 0 || h <= 0) {
		CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}

	if (!EnsureFrameBuffer(dc, w, h) || !m_frameDC.GetSafeHdc()) {
		dc.FillSolidRect(0, capH, w, h, FM_BG);
		CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}

	ComposeFrame(m_frameDC, w, h);

	CRect pr = dc.m_ps.rcPaint;
	if (pr.IsRectEmpty())
		pr.SetRect(0, capH, w, capH + h);
	const int paintCap = (pr.top < capH) ? 1 : 0;

#if CCUSTOM_AERO_SUPPORT
	const bool needOpaque = CCC_IsWin11()
		&& (savedata.aero == 1 || CCC_AcrylicCaption(m_hWnd));
	if (needOpaque) {
		if (m_chromaW != w || m_chromaH != h) {
			m_chromaCache.Release();
			m_chromaReady = false;
			m_chromaW = w;
			m_chromaH = h;
		}
		if (m_chromaCache.Ensure(dc.GetSafeHdc(), w, h)) {
			m_chromaCache.UpdateOpaqueRect(m_frameDC.GetSafeHdc(), 0, 0, 0, 0, w, h);
			m_chromaReady = true;
			m_chromaCache.BlitFull(dc.GetSafeHdc(), 0, capH, w, h);
			if (paintCap)
				CCC_CaptionPaintGdi(dc, m_hWnd);
			return;
		}
		CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, capH, w, h,
			m_frameDC.GetSafeHdc(), 0, 0, w, h);
		if (paintCap)
			CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}
#endif
	int sx = pr.left;
	int sy = pr.top - capH;
	int sw = pr.Width();
	int sh = pr.Height();
	if (sy < 0) { sh += sy; sy = 0; }
	if (sx < 0) { sw += sx; sx = 0; }
	if (sx + sw > w) sw = w - sx;
	if (sy + sh > h) sh = h - sy;
	if (sw > 0 && sh > 0)
		dc.BitBlt(sx, capH + sy, sw, sh, &m_frameDC, sx, sy, SRCCOPY);
	if (paintCap)
		CCC_CaptionPaintGdi(dc, m_hWnd);
}
