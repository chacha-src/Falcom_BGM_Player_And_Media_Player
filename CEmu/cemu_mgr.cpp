#include "StdAfx.h"
#include "cemu_mgr.h"
#include "cemu_modepref.h"
#include <string.h>
#include <stdlib.h>

static CEmuMgr s_cemuMgr;
static int s_cemuOnce = 0;
static CEmuGameEntry s_cemuFallbackGe;
static CRITICAL_SECTION s_cemuCatCs;
static int s_cemuCatCsReady = 0;

/* カタログ CS を一度だけ初期化 */
static void CEmuMgrCatCsInit()
{
	if (s_cemuCatCsReady) return;
	InitializeCriticalSection(&s_cemuCatCs);
	s_cemuCatCsReady = 1;
}

/* プロセス内シングルトン */
CEmuMgr* CEmuMgrGet()
{
	return &s_cemuMgr;
}

void CEmuMgrShutdown()
{
	CEmuMgrCatCsInit();
	EnterCriticalSection(&s_cemuCatCs);
	CEmuCatalogClear(&s_cemuMgr.catalog);
	s_cemuMgr.dataRoot[0] = 0;
	s_cemuMgr.ready = 0;
	s_cemuOnce = 0;
	LeaveCriticalSection(&s_cemuCatCs);
}

/* platform 文字列 → data サブフォルダ名 */
void CEmuMgrPlatformToDataDir(const char* platform, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!platform) return;
	if (_stricmp(platform, "pc98dos") == 0 || _stricmp(platform, "pc9821") == 0 || _stricmp(platform, "pc98vx") == 0) {
		strncpy_s(out, (size_t)outCap, "pc98", _TRUNCATE);
		return;
	}
	if (_stricmp(platform, "pc88va") == 0 || _stricmp(platform, "pc88vados") == 0
		|| _stricmp(platform, "pc80sr") == 0) {
		strncpy_s(out, (size_t)outCap, "pc88", _TRUNCATE);
		return;
	}
	strncpy_s(out, (size_t)outCap, platform, _TRUNCATE);
}

/* path::0001 — 物理 zip と 1 始まり曲番号を分離 */
int CEmuParseVirtualPath(const wchar_t* in, wchar_t* outPhysical, int outPhysChars,
	unsigned* outTitleIndex1)
{
	unsigned idx = 1;
	if (outTitleIndex1) *outTitleIndex1 = 1;
	if (outPhysical && outPhysChars > 0) outPhysical[0] = 0;
	if (!in || !in[0]) {
		if (outPhysical && outPhysChars > 0) outPhysical[0] = 0;
		return 0;
	}
	const int len = (int)wcslen(in);
	if (len >= 7 && in[len - 5] == L':') {
		int allDigits = 1;
		for (int i = len - 4; i < len; i++) {
			if (in[i] < L'0' || in[i] > L'9') { allDigits = 0; break; }
		}
		if (allDigits && in[len - 6] == L':') {
			if (outPhysical && outPhysChars > 0)
				wcsncpy_s(outPhysical, (size_t)outPhysChars, in, (size_t)(len - 6));
			idx = (unsigned)_wtoi(in + len - 4);
			if (idx == 0) idx = 1;
			if (outTitleIndex1) *outTitleIndex1 = idx;
			return 1;
		}
	}
	if (outPhysical && outPhysChars > 0)
		wcsncpy_s(outPhysical, (size_t)outPhysChars, in, _TRUNCATE);
	return 0;
}

/* フルパス zip + 曲番 → …\\artofwar.zip::0001 */
void CEmuFormatVirtualPath(const wchar_t* zipPhysical, unsigned titleIndex1,
	wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return;
	out[0] = 0;
	if (!zipPhysical || !zipPhysical[0]) return;
	if (titleIndex1 == 0) titleIndex1 = 1;
	/* ::NNNN を必ず残す。バッファが短いときは物理パス側を切る（曲が同一 Fol に潰れるのを防ぐ）。 */
	wchar_t suf[8];
	_snwprintf_s(suf, _TRUNCATE, L"::%04u", titleIndex1);
	const int sufLen = (int)wcslen(suf);
	if (outChars <= sufLen + 1) {
		wcsncpy_s(out, (size_t)outChars, suf, _TRUNCATE);
		return;
	}
	const int zipMax = outChars - 1 - sufLen;
	_snwprintf_s(out, (size_t)outChars, _TRUNCATE, L"%.*s%s", zipMax, zipPhysical, suf);
}

/* 表示用 basename::0001 */
void CEmuFormatVirtualBasename(const wchar_t* zipPhysical, unsigned titleIndex1,
	wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return;
	out[0] = 0;
	if (!zipPhysical || !zipPhysical[0]) return;
	if (titleIndex1 == 0) titleIndex1 = 1;
	const wchar_t* base = zipPhysical;
	const wchar_t* slash = wcsrchr(zipPhysical, L'\\');
	if (!slash) slash = wcsrchr(zipPhysical, L'/');
	if (slash) base = slash + 1;
	_snwprintf_s(out, (size_t)outChars, _TRUNCATE, L"%s::%04u", base, titleIndex1);
}

/* exe 隣 data\ を既定ルートにする */
static void CEmuMgrDefaultDataRoot(wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return;
	out[0] = 0;
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(NULL, exe, MAX_PATH);
	wchar_t* slash = wcsrchr(exe, L'\\');
	if (slash) *(slash + 1) = 0;
	_snwprintf_s(out, outChars, _TRUNCATE, L"%sdata", exe);
}

/* 既定ルート（exe\\data）を解決。savedPath は互換のため残すが通常は無視 */
void CEmuMgrGetEffectiveDataRoot(const wchar_t* savedPath, wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return;
	out[0] = 0;
	if (savedPath && savedPath[0]) {
		DWORD att = GetFileAttributesW(savedPath);
		if (att != INVALID_FILE_ATTRIBUTES && (att & FILE_ATTRIBUTE_DIRECTORY)) {
			wcsncpy_s(out, (size_t)outChars, savedPath, _TRUNCATE);
			return;
		}
	}
	CEmuMgrDefaultDataRoot(out, outChars);
}

/* dataRoot を決め ready を立てる。カタログはまだ読まない */
static void CEmuMgrSetRoot(CEmuMgr* m, const wchar_t* dataRootOverride)
{
	if (!m) return;
	if (dataRootOverride && dataRootOverride[0])
		wcsncpy_s(m->dataRoot, dataRootOverride, _TRUNCATE);
	else
		CEmuMgrDefaultDataRoot(m->dataRoot, MAX_PATH);

	if (GetFileAttributesW(m->dataRoot) == INVALID_FILE_ATTRIBUTES) {
		wchar_t exe[MAX_PATH];
		GetModuleFileNameW(NULL, exe, MAX_PATH);
		wchar_t* slash = wcsrchr(exe, L'\\');
		if (slash) *(slash + 1) = 0;
		wchar_t alt[MAX_PATH];
		_snwprintf_s(alt, _TRUNCATE, L"%shoot", exe);
		if (GetFileAttributesW(alt) != INVALID_FILE_ATTRIBUTES)
			wcsncpy_s(m->dataRoot, alt, _TRUNCATE);
	}

	CEmuCatalogClear(&m->catalog);
	/* data\ が無くても exe 隣 arcdata.zip があれば ready。ROM zip は場所不問（stem） */
	m->ready = 0;
	if (GetFileAttributesW(m->dataRoot) != INVALID_FILE_ATTRIBUTES)
		m->ready = 1;
	else {
		wchar_t arc[MAX_PATH] = {};
		CEmuCatalogGetExeArcdataPath(arc, MAX_PATH);
		if (arc[0] && GetFileAttributesW(arc) != INVALID_FILE_ATTRIBUTES)
			m->ready = 1;
	}
}

/* 初回利用時にカタログを読込。既に読込済みなら no-op */
int CEmuMgrEnsureCatalog(CEmuMgr* m)
{
	return CEmuMgrEnsureCatalogEx(m, NULL, NULL);
}

/* 進捗付き。起動時 KPI 読込画面などから呼ぶ */
int CEmuMgrEnsureCatalogEx(CEmuMgr* m, CEmuCatalogProgressFn progress, void* progressUser)
{
	if (!m) return 0;
	CEmuMgrCatCsInit();
	EnterCriticalSection(&s_cemuCatCs);
	if (!m->catalog.loaded && m->dataRoot[0])
		CEmuCatalogLoadEx(&m->catalog, m->dataRoot, progress, progressUser);
	const int n = m->catalog.count;
	LeaveCriticalSection(&s_cemuCatCs);
	return n;
}

/* exe 隣 data\ を既定。Init は起動時 1 回（ルート設定のみ・カタログは遅延） */
int CEmuMgrInit(CEmuMgr* m, const wchar_t* dataRootOverride)
{
	if (!m) return 0;
	if (s_cemuOnce) return m->ready;
	s_cemuOnce = 1;
	CEmuMgrCatCsInit();
	m->dataRoot[0] = 0;
	m->ready = 0;
	CEmuCatalogInit(&m->catalog);
	CEmuMgrSetRoot(m, dataRootOverride);
	/* カタログは初回 Resolve / Ensure まで読まない（起動をブロックしない） */
	return m->ready;
}

/* データパス変更後にカタログを再読込（ディスクキャッシュは維持。更新時は Invalidate） */
int CEmuMgrReload(CEmuMgr* m, const wchar_t* dataRootOverride)
{
	if (!m) return 0;
	if (!s_cemuOnce)
		return CEmuMgrInit(m, dataRootOverride);
	CEmuMgrCatCsInit();
	EnterCriticalSection(&s_cemuCatCs);
	CEmuMgrSetRoot(m, dataRootOverride);
	/* ディスクキャッシュは消さない。arcdata 更新時は呼び出し側で Invalidate。 */
	if (m->dataRoot[0])
		CEmuCatalogLoadEx(&m->catalog, m->dataRoot, NULL, NULL);
	LeaveCriticalSection(&s_cemuCatCs);
	return m->ready;
}

/* フォルダはヒントだけ（物理検索 / 最終フォールバック）。
   同一性は zip stem + 中身 — "roms" はダンプフォルダでカタログ dataDir ではない。 */
static int CEmuPathIsAbsolute(const wchar_t* path)
{
	if (!path || !path[0]) return 0;
	if (path[0] == L'\\' && path[1] == L'\\') return 1; /* UNC */
	if (((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z'))
		&& path[1] == L':' && (path[2] == L'\\' || path[2] == L'/' || path[2] == 0))
		return 1;
	return 0;
}

/* zip パスのフォルダ名から dataDir ヒントを取る。\\roms\\ は無視 */
static void CEmuMgrStemDataDirFromPath(const wchar_t* zipPath, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!zipPath) return;
	wchar_t low[MAX_PATH];
	wcsncpy_s(low, zipPath, _TRUNCATE);
	for (wchar_t* p = low; *p; p++) {
		if (*p >= L'A' && *p <= L'Z') *p = (wchar_t)(*p + (L'a' - L'A'));
	}
	if (wcsstr(low, L"\\pc88\\") || wcsstr(low, L"/pc88/"))
		strncpy_s(out, (size_t)outCap, "pc88", _TRUNCATE);
	else if (wcsstr(low, L"\\pc98\\") || wcsstr(low, L"/pc98/"))
		strncpy_s(out, (size_t)outCap, "pc98", _TRUNCATE);
	else if (wcsstr(low, L"\\x68k\\") || wcsstr(low, L"/x68k/"))
		strncpy_s(out, (size_t)outCap, "x68k", _TRUNCATE);
	else if (wcsstr(low, L"\\x1\\") || wcsstr(low, L"/x1/"))
		strncpy_s(out, (size_t)outCap, "x1", _TRUNCATE);
	else if (wcsstr(low, L"\\pc\\") || wcsstr(low, L"/pc/"))
		strncpy_s(out, (size_t)outCap, "pc", _TRUNCATE);
	else if (wcsstr(low, L"\\fmtowns\\") || wcsstr(low, L"/fmtowns/"))
		strncpy_s(out, (size_t)outCap, "fmtowns", _TRUNCATE);
	else if (wcsstr(low, L"\\fm7\\") || wcsstr(low, L"/fm7/"))
		strncpy_s(out, (size_t)outCap, "fm7", _TRUNCATE);
	else if (wcsstr(low, L"\\msx\\") || wcsstr(low, L"/msx/"))
		strncpy_s(out, (size_t)outCap, "msx", _TRUNCATE);
	else if (wcsstr(low, L"\\ac\\") || wcsstr(low, L"/ac/"))
		strncpy_s(out, (size_t)outCap, "ac", _TRUNCATE);
	else if (wcsstr(low, L"\\capcom\\") || wcsstr(low, L"/capcom/"))
		strncpy_s(out, (size_t)outCap, "ac", _TRUNCATE);
	else if (wcsstr(low, L"\\sc3000\\") || wcsstr(low, L"/sc3000/"))
		strncpy_s(out, (size_t)outCap, "sc3000", _TRUNCATE);
	else if (wcsstr(low, L"\\pico\\") || wcsstr(low, L"/pico/"))
		strncpy_s(out, (size_t)outCap, "pico", _TRUNCATE);
	/* \\roms\\ は意図的に無視 — カタログの同一性ではない */
}

/* カタログ欠落時の仮エントリ。stem 誤タグを避けるため既知名だけ細かく振る */
static const CEmuGameEntry* CEmuMgrFallbackEntry(const char* stem, const char* dataDir)
{
	if (!stem || !stem[0] || !dataDir || !dataDir[0]) return NULL;
	memset(&s_cemuFallbackGe, 0, sizeof(s_cemuFallbackGe));
	strncpy_s(s_cemuFallbackGe.archive, stem, _TRUNCATE);
	strncpy_s(s_cemuFallbackGe.dataDir, dataDir, _TRUNCATE);
	if (_stricmp(dataDir, "pc88") == 0) {
		strncpy_s(s_cemuFallbackGe.platform, "pc88", _TRUNCATE);
		strncpy_s(s_cemuFallbackGe.subtype, "opna", _TRUNCATE);
	} else if (_stricmp(dataDir, "pc98") == 0) {
		strncpy_s(s_cemuFallbackGe.platform, "pc98dos", _TRUNCATE);
		strncpy_s(s_cemuFallbackGe.subtype, "opna", _TRUNCATE);
	} else if (_stricmp(dataDir, "ac") == 0) {
		/* 既知 stem だけ — Capcom GNG 等を System16 に誤タグしない */
		strncpy_s(s_cemuFallbackGe.platform, "sega", _TRUNCATE);
		strncpy_s(s_cemuFallbackGe.subtype, "unknown", _TRUNCATE);
		if (_strnicmp(stem, "sfa", 3) == 0 || _strnicmp(stem, "sfz", 3) == 0
			|| _stricmp(stem, "vsav") == 0 || _stricmp(stem, "vhunt") == 0
			|| _stricmp(stem, "msh") == 0 || _stricmp(stem, "mshvsf") == 0
			|| _stricmp(stem, "mvsc") == 0 || _stricmp(stem, "xmcota") == 0
			|| _stricmp(stem, "xmvsf") == 0 || _stricmp(stem, "dstlk") == 0
			|| _stricmp(stem, "nwarr") == 0 || _stricmp(stem, "spf2t") == 0
			|| _stricmp(stem, "ssf2") == 0 || _stricmp(stem, "ssf2t") == 0
			|| _stricmp(stem, "hsf2") == 0) {
			/* CPS-2 / QSound — CPS1 OKI ではない */
			strncpy_s(s_cemuFallbackGe.platform, "capcom", _TRUNCATE);
			strncpy_s(s_cemuFallbackGe.subtype, "cps2", _TRUNCATE);
		} else if (_stricmp(stem, "ghouls") == 0 || _stricmp(stem, "sf2ce") == 0
			|| _strnicmp(stem, "sf2", 3) == 0) {
			strncpy_s(s_cemuFallbackGe.platform, "capcom", _TRUNCATE);
			strncpy_s(s_cemuFallbackGe.subtype, "cps1", _TRUNCATE);
		} else if (_stricmp(stem, "shinobi") == 0 || _stricmp(stem, "cotton") == 0) {
			strncpy_s(s_cemuFallbackGe.subtype, "system16b", _TRUNCATE);
		} else if (_stricmp(stem, "fantzone") == 0) {
			strncpy_s(s_cemuFallbackGe.subtype, "system16a", _TRUNCATE);
		} else if (_stricmp(stem, "gngjap") == 0 || _strnicmp(stem, "gng", 3) == 0) {
			/* Capcom Ghosts'n Goblins: Z80+YM2203 — System16 YM2151 ではない */
			strncpy_s(s_cemuFallbackGe.platform, "capcom", _TRUNCATE);
			strncpy_s(s_cemuFallbackGe.subtype, "gng", _TRUNCATE);
		} else if (_stricmp(stem, "outrunm") == 0 || _stricmp(stem, "outrun") == 0
			|| _stricmp(stem, "toutrun") == 0 || _stricmp(stem, "shangon") == 0) {
			strncpy_s(s_cemuFallbackGe.subtype, "outrun", _TRUNCATE);
		} else if (_stricmp(stem, "aburner2") == 0 || _stricmp(stem, "aburner") == 0) {
			strncpy_s(s_cemuFallbackGe.subtype, "aburner", _TRUNCATE);
		} else if (_stricmp(stem, "sharrier") == 0 || _stricmp(stem, "hangon") == 0
			|| _stricmp(stem, "hangon1") == 0) {
			strncpy_s(s_cemuFallbackGe.subtype,
				_stricmp(stem, "hangon") == 0 || _stricmp(stem, "hangon1") == 0 ? "hangon" : "sharrier",
				_TRUNCATE);
		}
	} else if (_stricmp(dataDir, "x68k") == 0) {
		strncpy_s(s_cemuFallbackGe.platform, "x68k", _TRUNCATE);
		strncpy_s(s_cemuFallbackGe.subtype, "mxdrv", _TRUNCATE);
	} else if (_stricmp(dataDir, "msx") == 0) {
		strncpy_s(s_cemuFallbackGe.platform, "msx", _TRUNCATE);
		strncpy_s(s_cemuFallbackGe.subtype, "kss", _TRUNCATE);
	} else if (_stricmp(dataDir, "x1") == 0) {
		strncpy_s(s_cemuFallbackGe.platform, "x1", _TRUNCATE);
		strncpy_s(s_cemuFallbackGe.subtype, "opm", _TRUNCATE);
	} else if (_stricmp(dataDir, "pc") == 0) {
		strncpy_s(s_cemuFallbackGe.platform, "pcatdos", _TRUNCATE);
		strncpy_s(s_cemuFallbackGe.subtype, "adlib", _TRUNCATE);
	} else if (_stricmp(dataDir, "sc3000") == 0) {
		strncpy_s(s_cemuFallbackGe.platform, "sega", _TRUNCATE);
		strncpy_s(s_cemuFallbackGe.subtype, "sg1000", _TRUNCATE);
	} else if (_stricmp(dataDir, "pico") == 0) {
		strncpy_s(s_cemuFallbackGe.platform, "sega", _TRUNCATE);
		strncpy_s(s_cemuFallbackGe.subtype, "pico", _TRUNCATE);
	} else if (_stricmp(dataDir, "fm7") == 0) {
		/* *_fmav は FM77AV/OPN。素の *_fm7 は PSG */
		const size_t n = strlen(stem);
		if (n >= 5 && _stricmp(stem + n - 5, "_fmav") == 0)
			strncpy_s(s_cemuFallbackGe.platform, "fm77av", _TRUNCATE);
		else
			strncpy_s(s_cemuFallbackGe.platform, "fm7", _TRUNCATE);
		strncpy_s(s_cemuFallbackGe.subtype, "generic", _TRUNCATE);
	} else {
		strncpy_s(s_cemuFallbackGe.platform, dataDir, _TRUNCATE);
	}
	CEmuCatalogAssignHwIds(&s_cemuFallbackGe);
	return &s_cemuFallbackGe;
}

/* カタログ欠落時: zip メンバを嗅ぐ (Neo M1/V* など) */
static const CEmuGameEntry* CEmuMgrSniffFallback(const char* stem, const CEmuZipFs* zipFs)
{
	if (!stem || !stem[0] || !zipFs) return NULL;
	int hasM1 = 0, hasV = 0;
	for (int i = 0; i < zipFs->fileCount; i++) {
		const wchar_t* p = zipFs->files[i].path;
		if (!p || !p[0]) continue;
		const wchar_t* base = p;
		for (const wchar_t* q = p; *q; q++) {
			if (*q == L'\\' || *q == L'/') base = q + 1;
		}
		size_t n = wcslen(base);
		if (n >= 3) {
			const wchar_t* ext = base + n - 3;
			if (_wcsicmp(ext, L".m1") == 0) hasM1 = 1;
			else if (_wcsicmp(ext, L".v1") == 0 || _wcsicmp(ext, L".v2") == 0
				|| _wcsicmp(ext, L".v3") == 0 || _wcsicmp(ext, L".v4") == 0)
				hasV = 1;
		}
	}
	if (hasM1 && hasV) {
		const CEmuGameEntry* ge = CEmuMgrFallbackEntry(stem, "ac");
		if (ge) {
			strncpy_s(s_cemuFallbackGe.platform, "neogeo", _TRUNCATE);
			strncpy_s(s_cemuFallbackGe.subtype, "generic", _TRUNCATE);
			strncpy_s(s_cemuFallbackGe.dataDir, "ac", _TRUNCATE);
			MultiByteToWideChar(CP_ACP, 0, stem, -1, s_cemuFallbackGe.name, CEMU_GAME_NAME);
			CEmuCatalogAssignHwIds(&s_cemuFallbackGe);
		}
		return ge;
	}
	return NULL;
}

/* D&D / 再生: zip パスからゲーム特定。outZipPath=実 zip フルパス */
const CEmuGameEntry* CEmuMgrResolveZip(CEmuMgr* m, const wchar_t* droppedZip,
	wchar_t* outZipPath, int outZipChars, char* outDataDir, int outDataDirCap)
{
	if (!m || !droppedZip || !outZipPath || outZipChars <= 0) return NULL;
	outZipPath[0] = 0;
	if (outDataDir && outDataDirCap > 0) outDataDir[0] = 0;

	wchar_t physical[CEMU_ZIP_PATH];
	unsigned titleIdx = 1;
	CEmuParseVirtualPath(droppedZip, physical, (int)_countof(physical), &titleIdx);
	(void)titleIdx;

	/* Reload で catalog を毎回 Clear しない（再生中 s->game がダングリングになる）。
	   data\ 無しでも EnsureCatalog → arcdata.zip で stem 解決する。 */
	CEmuMgrEnsureCatalog(m);

	/* 実 zip の場所はドロップ／指定パス優先。カタログ用 dataRoot とは独立。 */
	wchar_t full[CEMU_ZIP_PATH];
	full[0] = 0;
	const int absPath = CEmuPathIsAbsolute(physical);
	if (GetFileAttributesW(physical) != INVALID_FILE_ATTRIBUTES) {
		wcsncpy_s(full, physical, _TRUNCATE);
	} else if (absPath) {
		/* 絶対パスなのに無い → data へ差し替えない（D&D デグレ／クラッシュ源） */
		return NULL;
	} else {
		/* 相対・ファイル名のみ: data 配下を便宜検索（絶対パスは上で打ち切り済み） */
		char stemTry[CEMU_ARCHIVE_NAME];
		if (!CEmuArchiveStemFromPath(physical, stemTry, (int)sizeof(stemTry)))
			return NULL;
		static const char* kTry[] = {
			"pc98", "pc88", "pc88va", "ac", "x68k", "msx", "x1", "fm7", "fmtowns",
			"sc3000", "pico", "roms", NULL
		};
		int found = 0;
		for (int i = 0; kTry[i] && !found; i++) {
			_snwprintf_s(full, _TRUNCATE, L"%s\\%hs\\%hs.zip", m->dataRoot, kTry[i], stemTry);
			if (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES)
				found = 1;
		}
		if (!found) return NULL;
	}

	if (!full[0] || GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES)
		return NULL;

	char stem[CEMU_ARCHIVE_NAME];
	if (!CEmuArchiveStemFromPath(full, stem, (int)sizeof(stem)))
		return NULL;

	char dirHint[CEMU_DATA_DIR];
	CEmuMgrStemDataDirFromPath(full, dirHint, (int)sizeof(dirHint));

	/* 重複カタログ行 (xml vs xml2) を実 zip メンバで採点する。
	   FC88 DATA* パックが MUS* 専用リストに張り付かないように。
	   ヒープ — CEmuZipFs は約 0.5MB でスタックに置かない。 */
	CEmuZipFs* zipProbe = (CEmuZipFs*)calloc(1, sizeof(CEmuZipFs));
	const CEmuZipFs* zipFs = NULL;
	/* 名前のみ: 順位付けで毎回数 MB の ADPCM を展開しない
	   (プレイリスト描画 / DnD が凍って DS が飢えていた)。 */
	if (zipProbe && CEmuZipFsOpenNames(zipProbe, full))
		zipFs = zipProbe;

	char preferTag[CEMU_MODE_TAG] = {};
	CEmuModePrefGet(full, preferTag, (int)sizeof(preferTag));

	/* 同一性 = zip stem + メンバ一致。フォルダは通常フィルタにしない
	   (pc88/ac/roms が衝突する) が、x1/fm7 はプラットフォーム横断で
	   stem が被る (rebirth→pc88 vs x1) — それらのフォルダは dataDir に結ぶ。
	   fmtowns のローカル zip はカタログ上 pc98vx (Falcom RX+2608) なので
	   dataDir=fmtowns に結ばない (空フォールバックしか出ない)。 */
	const char* bindDir = NULL;
	if (dirHint[0]
		&& (_stricmp(dirHint, "x1") == 0 || _stricmp(dirHint, "fm7") == 0))
		bindDir = dirHint;
	const CEmuGameEntry* ge = CEmuCatalogFindArchiveForZipMode(&m->catalog, stem,
		bindDir, zipFs, preferTag[0] ? preferTag : NULL);
	/* ローカル zip は gngjap.zip。カタログ archive は "gng" */
	if (!ge && (_stricmp(stem, "gngjap") == 0 || _stricmp(stem, "gngj") == 0)) {
		ge = CEmuCatalogFindArchiveForZipMode(&m->catalog, "gng", NULL, zipFs,
			preferTag[0] ? preferTag : NULL);
	}
	/* ローカル dezeniw.zip は hudsonsoft dezeniwsr (PATCH+MAIN@1000) */
	if (!ge && _stricmp(stem, "dezeniw") == 0) {
		static const char* kDezeniw[] = { "dezeniwsr", "dezeniw88", NULL };
		for (int a = 0; kDezeniw[a] && !ge; a++) {
			ge = CEmuCatalogFindArchiveForZipMode(&m->catalog, kDezeniw[a],
				NULL, zipFs, preferTag[0] ? preferTag : NULL);
		}
	}
	/* ローカル bpoint.zip の romlist はカタログ archive bpoint88 */
	if (!ge && _stricmp(stem, "bpoint") == 0) {
		ge = CEmuCatalogFindArchiveForZipMode(&m->catalog, "bpoint88", NULL, zipFs,
			preferTag[0] ? preferTag : NULL);
	}
	/* ローカル aspicsp.zip はカタログ archive x1aspicsp (既に一致する) */
	if (!ge && _stricmp(stem, "aspicsp") == 0) {
		ge = CEmuCatalogFindArchiveForZipMode(&m->catalog, "x1aspicsp", bindDir, zipFs,
			preferTag[0] ? preferTag : NULL);
	}
	/* ローカル rebirthx1.zip はカタログ archive "rebirth" を共有 (X1 OPM 双子) */
	if (!ge && _stricmp(stem, "rebirthx1") == 0) {
		ge = CEmuCatalogFindArchiveForZipMode(&m->catalog, "rebirth",
			bindDir ? bindDir : "x1", zipFs, preferTag[0] ? preferTag : NULL);
	}
	if (!ge && zipFs)
		ge = CEmuMgrSniffFallback(stem, zipFs);
	if (!ge && dirHint[0])
		ge = CEmuMgrFallbackEntry(stem, dirHint);
	if (zipProbe) {
		if (zipFs)
			CEmuZipFsClose(zipProbe);
		free(zipProbe);
	}

	wcsncpy_s(outZipPath, (size_t)outZipChars, full, _TRUNCATE);
	if (outDataDir && outDataDirCap > 0) {
		if (ge && ge->dataDir[0])
			strncpy_s(outDataDir, (size_t)outDataDirCap, ge->dataDir, _TRUNCATE);
		else if (ge && ge->platform[0])
			CEmuMgrPlatformToDataDir(ge->platform, outDataDir, outDataDirCap);
		else if (dirHint[0])
			strncpy_s(outDataDir, (size_t)outDataDirCap, dirHint, _TRUNCATE);
	}
	return ge;
}
