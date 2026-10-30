#include "stdafx.h"
#include "DatArchive.h"
#include <shlwapi.h>
#include <zstd.h>

#pragma comment(lib, "libzstd_static.lib")

enum {
	DATARC_MAX = 256,
	DATARC_NAME = 96,
	DATARC_ZSTD_LEVEL = 5
};

static const DWORD DATARC_MAGIC = 0x4144594Fu; // 'OYDA'
static const DWORD DATARC_VER = 1;
static const TCHAR DATARC_NAME_W[] = _T("oggYSEDbgm_uni_avx2.dat");
static const TCHAR DATARC_STAGE_SUB[] = _T("oggYSED_uni_avx2_stage");

#pragma pack(push, 1)
struct DatArcFileHdr {
	DWORD magic;
	DWORD version;
	DWORD count;
	DWORD reserved;
};
struct DatArcIdxEnt {
	char name[DATARC_NAME];
	ULONGLONG uncSize;
	ULONGLONG cmpSize;
	ULONGLONG offset;
};
#pragma pack(pop)

struct DatArcMemEnt {
	TCHAR nameW[DATARC_NAME];
	char nameU8[DATARC_NAME];
	ULONGLONG uncSize;
	ULONGLONG cmpSize;
	ULONGLONG offset;
	BYTE inIndex;
	FILETIME stageMtime;
	ULONGLONG stageSize;
	BYTE stageFpValid;
	/* 非圧縮バイトの指紋。mtime が動かない上書きでも、一致なら再圧縮しない */
	ULONGLONG contentHash;
	BYTE hashValid;
	/* offset から次メンバまでの空き。cmpSize より大きくてもよい(差し替え用の余り) */
	ULONGLONG slotCap;
};

static TCHAR g_exeDir[MAX_PATH] = { 0 };
static TCHAR g_arcPath[MAX_PATH] = { 0 };
static TCHAR g_stageDir[MAX_PATH] = { 0 };
static DatArcMemEnt g_ent[DATARC_MAX];
static int g_n = 0;
static BOOL g_ready = FALSE;
static BYTE* g_arcMap = NULL;
static ULONGLONG g_arcMapSize = 0;
static int g_flushSuspend = 0;
static BOOL g_flushDirty = FALSE;
/* FlushAll のときだけ、mtime が同じでも内容ハッシュを見て古い塊を戻さない */
static int g_verifyContent = 0;
static CRITICAL_SECTION g_arcCs;
static volatile LONG g_arcCsInit = 0;

struct DatArcLock {
	BOOL on;
	DatArcLock() : on(FALSE) {
		if (g_arcCsInit) { EnterCriticalSection(&g_arcCs); on = TRUE; }
	}
	~DatArcLock() { if (on) LeaveCriticalSection(&g_arcCs); }
};

static void DatArc_FreeMap()
{
	if (g_arcMap) {
		free(g_arcMap);
		g_arcMap = NULL;
	}
	g_arcMapSize = 0;
}

static BOOL DatArc_NameToUtf8(LPCTSTR w, char* out, int outMax)
{
	if (!w || !out || outMax <= 0) return FALSE;
	ZeroMemory(out, outMax);
#ifdef _UNICODE
	const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, out, outMax, NULL, NULL);
	return n > 0;
#else
	strncpy(out, w, outMax - 1);
	return TRUE;
#endif
}

static BOOL DatArc_Utf8ToName(const char* u8, TCHAR* out, int outMax)
{
	if (!u8 || !out || outMax <= 0) return FALSE;
	ZeroMemory(out, outMax * sizeof(TCHAR));
#ifdef _UNICODE
	const int n = ::MultiByteToWideChar(CP_UTF8, 0, u8, -1, out, outMax);
	return n > 0;
#else
	strncpy(out, u8, outMax - 1);
	return TRUE;
#endif
}

static BOOL DatArc_IsArchiveLeaf(LPCTSTR leaf)
{
	return leaf && _tcsicmp(leaf, DATARC_NAME_W) == 0;
}

// パック対象: 本体設定・sidecar・playlist。ゲーム資産 wav.dat 等は除外。
static BOOL DatArc_IsPackableLeaf(LPCTSTR leaf)
{
	if (!leaf || !leaf[0]) return FALSE;
	if (DatArc_IsArchiveLeaf(leaf)) return FALSE;
	const size_t len = _tcslen(leaf);
	if (len < 5) return FALSE;
	if (_tcsicmp(leaf + len - 4, _T(".dat")) != 0) return FALSE;

	if (_tcsicmp(leaf, _T("oggYSEDbgmu.dat")) == 0) return TRUE;
	if (_tcsicmp(leaf, _T("oggYSEDbgm.dat")) == 0) return TRUE;
	if (_tcsicmp(leaf, _T("fmmon_geom.dat")) == 0) return TRUE;
	if (_tcsnicmp(leaf, _T("oggYSEDbgmu_"), 12) == 0) return TRUE;
	if (_tcsnicmp(leaf, _T("oggYSEDbgm_"), 11) == 0) return TRUE;
	if (_tcsnicmp(leaf, _T("playlistu"), 9) == 0) return TRUE;
	if (_tcsnicmp(leaf, _T("playlist"), 8) == 0) {
		// playlist.dat / playlistN.dat (ANSI 系) のみ。他ゲーム用は触らない。
		if (_tcsicmp(leaf, _T("playlist.dat")) == 0) return TRUE;
		if (leaf[8] >= _T('0') && leaf[8] <= _T('9')) return TRUE;
	}
	return FALSE;
}

static int DatArc_FindIndex(LPCTSTR leaf)
{
	if (!leaf) return -1;
	for (int i = 0; i < g_n; ++i) {
		if (_tcsicmp(g_ent[i].nameW, leaf) == 0)
			return i;
	}
	return -1;
}

static int DatArc_AddIndex(LPCTSTR leaf)
{
	int i = DatArc_FindIndex(leaf);
	if (i >= 0) return i;
	if (g_n >= DATARC_MAX) return -1;
	i = g_n++;
	ZeroMemory(&g_ent[i], sizeof(g_ent[i]));
	_tcsncpy(g_ent[i].nameW, leaf, DATARC_NAME - 1);
	DatArc_NameToUtf8(leaf, g_ent[i].nameU8, DATARC_NAME);
	g_ent[i].inIndex = 1;
	return i;
}

static CString DatArc_StagePath(LPCTSTR leaf)
{
	CString s = g_stageDir;
	s += leaf;
	return s;
}

static BOOL DatArc_EnsureStageDir()
{
	if (!g_stageDir[0]) return FALSE;
	if (::PathFileExists(g_stageDir)) return TRUE;
	return ::CreateDirectory(g_stageDir, NULL) != 0 || ::GetLastError() == ERROR_ALREADY_EXISTS;
}

static BOOL DatArc_ReadWholeFile(LPCTSTR path, BYTE** outBuf, ULONGLONG* outSize)
{
	*outBuf = NULL;
	*outSize = 0;
	CFile f;
	if (!f.Open(path, CFile::modeRead | CFile::shareDenyWrite, NULL))
		return FALSE;
	const ULONGLONG sz = f.GetLength();
	if (sz > 0x40000000ULL) { // 1GB cap
		f.Close();
		return FALSE;
	}
	BYTE* buf = (BYTE*)malloc((size_t)sz + 1);
	if (!buf) { f.Close(); return FALSE; }
	if (sz > 0 && f.Read(buf, (UINT)sz) != (UINT)sz) {
		free(buf);
		f.Close();
		return FALSE;
	}
	f.Close();
	*outBuf = buf;
	*outSize = sz;
	return TRUE;
}

static BOOL DatArc_WriteWholeFile(LPCTSTR path, const void* data, ULONGLONG size)
{
	CFile f;
	if (!f.Open(path, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL))
		return FALSE;
	if (size > 0)
		f.Write(data, (UINT)size);
	f.Close();
	return TRUE;
}

static ULONGLONG DatArc_Fnv64(const void* data, size_t size)
{
	const BYTE* p = (const BYTE*)data;
	ULONGLONG h = 14695981039346656037ull;
	size_t i = 0;
	if (!p) size = 0;
	for (; i + 8 <= size; i += 8) {
		ULONGLONG v;
		memcpy(&v, p + i, 8);
		h ^= v;
		h *= 1099511628211ull;
	}
	for (; i < size; ++i) {
		h ^= (ULONGLONG)p[i];
		h *= 1099511628211ull;
	}
	return h;
}

/* 後からの数バイト増減でアーカイブ全体を書き直さないための余り */
static ULONGLONG DatArc_SlotStore(ULONGLONG cmpSz)
{
	ULONGLONG slack;
	if (cmpSz == 0) return 0;
	slack = cmpSz / 8;
	if (slack < 4096ull) slack = 4096ull;
	if (slack > (1024ull * 1024ull)) slack = 1024ull * 1024ull;
	return cmpSz + slack;
}

static BOOL DatArc_LoadArcIntoMemory()
{
	DatArc_FreeMap();
	if (!::PathFileExists(g_arcPath))
		return FALSE;
	return DatArc_ReadWholeFile(g_arcPath, &g_arcMap, &g_arcMapSize);
}

static BOOL DatArc_ParseIndexFromMap()
{
	g_n = 0;
	if (!g_arcMap || g_arcMapSize < sizeof(DatArcFileHdr))
		return FALSE;
	const DatArcFileHdr* hdr = (const DatArcFileHdr*)g_arcMap;
	if (hdr->magic != DATARC_MAGIC || hdr->version != DATARC_VER)
		return FALSE;
	if (hdr->count > (DWORD)DATARC_MAX)
		return FALSE;
	const ULONGLONG need = sizeof(DatArcFileHdr) + (ULONGLONG)hdr->count * sizeof(DatArcIdxEnt);
	if (g_arcMapSize < need)
		return FALSE;
	const DatArcIdxEnt* idx = (const DatArcIdxEnt*)(g_arcMap + sizeof(DatArcFileHdr));
	for (DWORD i = 0; i < hdr->count; ++i) {
		DatArcMemEnt& e = g_ent[g_n];
		ZeroMemory(&e, sizeof(e));
		memcpy(e.nameU8, idx[i].name, DATARC_NAME);
		e.nameU8[DATARC_NAME - 1] = 0;
		DatArc_Utf8ToName(e.nameU8, e.nameW, DATARC_NAME);
		e.uncSize = idx[i].uncSize;
		e.cmpSize = idx[i].cmpSize;
		e.offset = idx[i].offset;
		e.inIndex = 1;
		if (e.offset + e.cmpSize > g_arcMapSize)
			return FALSE;
		if (i + 1 < hdr->count) {
			if (idx[i + 1].offset < idx[i].offset)
				return FALSE;
			e.slotCap = idx[i + 1].offset - idx[i].offset;
		} else {
			if (g_arcMapSize < idx[i].offset)
				return FALSE;
			e.slotCap = g_arcMapSize - idx[i].offset;
		}
		if (e.slotCap < e.cmpSize)
			e.slotCap = e.cmpSize;
		++g_n;
	}
	return TRUE;
}

static BOOL DatArc_ExtractOne(int i)
{
	if (i < 0 || i >= g_n) return FALSE;
	DatArcMemEnt& e = g_ent[i];
	const CString path = DatArc_StagePath(e.nameW);
	if (::PathFileExists(path)) {
		/* 既にあるステージは「今のアーカイブ塊と一致」とは限らない。
		 * ここで指紋を取ると、直後の FlushAll が古い圧縮塊を再利用する。 */
		return TRUE;
	}
	if (!g_arcMap || e.cmpSize == 0) {
		// 空メンバー
		e.contentHash = DatArc_Fnv64(NULL, 0);
		e.hashValid = 1;
		return DatArc_WriteWholeFile(path, "", 0);
	}
	if (e.offset + e.cmpSize > g_arcMapSize)
		return FALSE;
	const BYTE* src = g_arcMap + (size_t)e.offset;
	BYTE* dst = (BYTE*)malloc((size_t)e.uncSize + 1);
	if (!dst) return FALSE;
	const size_t got = ZSTD_decompress(dst, (size_t)e.uncSize, src, (size_t)e.cmpSize);
	if (ZSTD_isError(got) || got != (size_t)e.uncSize) {
		free(dst);
		return FALSE;
	}
	e.contentHash = DatArc_Fnv64(dst, (size_t)e.uncSize);
	e.hashValid = 1;
	const BOOL ok = DatArc_WriteWholeFile(path, dst, e.uncSize);
	free(dst);
	if (!ok)
		return FALSE;
	{
		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		if (::GetFileAttributesEx(path, GetFileExInfoStandard, &fad)) {
			ULARGE_INTEGER sz;
			sz.LowPart = fad.nFileSizeLow;
			sz.HighPart = fad.nFileSizeHigh;
			e.stageSize = sz.QuadPart;
			e.stageMtime = fad.ftLastWriteTime;
			e.stageFpValid = 1;
		}
	}
	return TRUE;
}

static BOOL DatArc_IsPlaylistLeaf(LPCTSTR leaf)
{
	if (!leaf || !leaf[0]) return FALSE;
	if (_tcsnicmp(leaf, _T("playlistu"), 9) == 0) return TRUE;
	if (_tcsicmp(leaf, _T("playlist.dat")) == 0) return TRUE;
	if (_tcsnicmp(leaf, _T("playlist"), 8) == 0 && leaf[8] >= _T('0') && leaf[8] <= _T('9'))
		return TRUE;
	return FALSE;
}

static BOOL DatArc_ExtractStartupLeaves()
{
	if (!DatArc_EnsureStageDir()) return FALSE;
	for (int i = 0; i < g_n; ++i) {
		if (DatArc_IsPlaylistLeaf(g_ent[i].nameW))
			continue;
		if (!DatArc_ExtractOne(i))
			return FALSE;
	}
	return TRUE;
}

static BOOL DatArc_ExtractAll()
{
	if (!DatArc_EnsureStageDir()) return FALSE;
	for (int i = 0; i < g_n; ++i) {
		if (!DatArc_ExtractOne(i))
			return FALSE;
	}
	return TRUE;
}

static void DatArc_RefreshStageFingerprints()
{
	for (int i = 0; i < g_n; ++i) {
		g_ent[i].stageFpValid = 0;
		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		const CString path = DatArc_StagePath(g_ent[i].nameW);
		if (!::GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
			continue;
		ULARGE_INTEGER sz;
		sz.LowPart = fad.nFileSizeLow;
		sz.HighPart = fad.nFileSizeHigh;
		g_ent[i].stageSize = sz.QuadPart;
		g_ent[i].stageMtime = fad.ftLastWriteTime;
		g_ent[i].stageFpValid = 1;
	}
}

static BOOL DatArc_StageMatchesEnt(int ei, const WIN32_FILE_ATTRIBUTE_DATA& fad)
{
	if (ei < 0 || ei >= g_n || !g_ent[ei].stageFpValid)
		return FALSE;
	ULARGE_INTEGER sz;
	sz.LowPart = fad.nFileSizeLow;
	sz.HighPart = fad.nFileSizeHigh;
	if (sz.QuadPart != g_ent[ei].stageSize)
		return FALSE;
	if (::CompareFileTime(&fad.ftLastWriteTime, &g_ent[ei].stageMtime) != 0)
		return FALSE;
	if (!g_arcMap || g_ent[ei].cmpSize == 0) {
		// 空ファイルはサイズ0一致で再利用可
		return sz.QuadPart == 0 && g_ent[ei].uncSize == 0;
	}
	if (g_ent[ei].offset + g_ent[ei].cmpSize > g_arcMapSize)
		return FALSE;
	return TRUE;
}

static void DatArc_NoteStageFp(int ei, LPCTSTR path)
{
	WIN32_FILE_ATTRIBUTE_DATA fad = {};
	if (ei < 0 || ei >= g_n) return;
	if (!path || !::GetFileAttributesEx(path, GetFileExInfoStandard, &fad)) {
		g_ent[ei].stageFpValid = 0;
		return;
	}
	ULARGE_INTEGER sz;
	sz.LowPart = fad.nFileSizeLow;
	sz.HighPart = fad.nFileSizeHigh;
	g_ent[ei].stageSize = sz.QuadPart;
	g_ent[ei].stageMtime = fad.ftLastWriteTime;
	g_ent[ei].stageFpValid = 1;
}

static int DatArc_FileSlot(const char* nameU8)
{
	if (!nameU8 || !nameU8[0] || !g_arcMap || g_arcMapSize < sizeof(DatArcFileHdr))
		return -1;
	const DatArcFileHdr* hdr = (const DatArcFileHdr*)g_arcMap;
	if (hdr->magic != DATARC_MAGIC || hdr->version != DATARC_VER)
		return -1;
	if (hdr->count > (DWORD)DATARC_MAX)
		return -1;
	const ULONGLONG need = sizeof(DatArcFileHdr) + (ULONGLONG)hdr->count * sizeof(DatArcIdxEnt);
	if (g_arcMapSize < need)
		return -1;
	const DatArcIdxEnt* idx = (const DatArcIdxEnt*)(g_arcMap + sizeof(DatArcFileHdr));
	for (DWORD s = 0; s < hdr->count; ++s) {
		if (strncmp(idx[s].name, nameU8, DATARC_NAME) == 0)
			return (int)s;
	}
	return -1;
}

/* 変わったメンバだけ、既存の空きに収まるならアーカイブ全体は書き直さない */
static BOOL DatArc_TryPatchMembers(
	TCHAR names[][DATARC_NAME],
	int nNames,
	BYTE* cmp[],
	ULONGLONG uncSz[],
	ULONGLONG cmpSz[],
	BYTE cmpOwned[],
	ULONGLONG hashV[],
	BYTE hashOk[])
{
	if (!g_arcMap || !g_arcPath[0] || nNames != g_n || nNames <= 0)
		return FALSE;
	int dirty[DATARC_MAX];
	int nDirty = 0;
	for (int i = 0; i < nNames; ++i) {
		const int oi = DatArc_FindIndex(names[i]);
		if (oi < 0)
			return FALSE;
		if (!cmpOwned[i])
			continue;
		if (cmpSz[i] > 0x7fffffffULL || (cmpSz[i] > 0 && !cmp[i]))
			return FALSE;
		const int slot = DatArc_FileSlot(g_ent[oi].nameU8);
		if (slot < 0)
			return FALSE;
		ULONGLONG cap = g_ent[oi].slotCap;
		if (cap < g_ent[oi].cmpSize)
			cap = g_ent[oi].cmpSize;
		if (cmpSz[i] > cap)
			return FALSE;
		if (g_ent[oi].offset + cap > g_arcMapSize)
			return FALSE;
		dirty[nDirty++] = i;
	}
	if (nDirty <= 0)
		return FALSE;

	::SetFileAttributes(g_arcPath, FILE_ATTRIBUTE_NORMAL);
	CFile f;
	if (!f.Open(g_arcPath, CFile::modeReadWrite | CFile::shareExclusive, NULL))
		return FALSE;

	BOOL ok = TRUE;
	try {
		DatArcIdxEnt* idx = (DatArcIdxEnt*)(g_arcMap + sizeof(DatArcFileHdr));
		for (int d = 0; d < nDirty && ok; ++d) {
			const int i = dirty[d];
			const int oi = DatArc_FindIndex(names[i]);
			const int slot = DatArc_FileSlot(g_ent[oi].nameU8);
			if (oi < 0 || slot < 0) { ok = FALSE; break; }
			if (cmpSz[i] > 0) {
				f.Seek((LONGLONG)g_ent[oi].offset, CFile::begin);
				f.Write(cmp[i], (UINT)cmpSz[i]);
				memcpy(g_arcMap + (size_t)g_ent[oi].offset, cmp[i], (size_t)cmpSz[i]);
			}
			idx[slot].uncSize = uncSz[i];
			idx[slot].cmpSize = cmpSz[i];
			f.Seek((LONGLONG)(sizeof(DatArcFileHdr) + (size_t)slot * sizeof(DatArcIdxEnt)), CFile::begin);
			f.Write(&idx[slot], sizeof(DatArcIdxEnt));
			g_ent[oi].uncSize = uncSz[i];
			g_ent[oi].cmpSize = cmpSz[i];
			g_ent[oi].contentHash = hashV[i];
			g_ent[oi].hashValid = hashOk[i] ? 1 : 0;
			DatArc_NoteStageFp(oi, DatArc_StagePath(names[i]));
		}
		f.Close();
	}
	catch (...) {
		try { f.Close(); } catch (...) {}
		ok = FALSE;
	}
	return ok;
}

static BOOL DatArc_RebuildFromStage()
{
	if (!DatArc_EnsureStageDir()) return FALSE;

	// 既存アーカイブメンバはステージ未展開でも落とさない。
	// その後ステージ上の新規 .dat を足す。
	TCHAR names[DATARC_MAX][DATARC_NAME];
	int nNames = 0;
	for (int i = 0; i < g_n && nNames < DATARC_MAX; ++i) {
		if (!g_ent[i].nameW[0]) continue;
		if (!DatArc_IsPackableLeaf(g_ent[i].nameW)) continue;
		_tcsncpy(names[nNames], g_ent[i].nameW, DATARC_NAME - 1);
		names[nNames][DATARC_NAME - 1] = 0;
		++nNames;
	}
	CString pattern = g_stageDir;
	pattern += _T("*.dat");
	WIN32_FIND_DATA fd;
	HANDLE h = ::FindFirstFile(pattern, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			if (!DatArc_IsPackableLeaf(fd.cFileName)) continue;
			BOOL already = FALSE;
			for (int k = 0; k < nNames; ++k) {
				if (_tcsicmp(names[k], fd.cFileName) == 0) { already = TRUE; break; }
			}
			if (already) continue;
			if (nNames >= DATARC_MAX) break;
			_tcsncpy(names[nNames], fd.cFileName, DATARC_NAME - 1);
			names[nNames][DATARC_NAME - 1] = 0;
			++nNames;
		} while (::FindNextFile(h, &fd));
		::FindClose(h);
	}

	// 変更が無ければアーカイブ再生成を省略（FlushAll の照合中はハッシュまで見る）
	if (!g_verifyContent && nNames == g_n && g_arcMap && ::PathFileExists(g_arcPath)) {
		BOOL allSame = TRUE;
		for (int i = 0; i < nNames; ++i) {
			const int oi = DatArc_FindIndex(names[i]);
			if (oi < 0) { allSame = FALSE; break; }
			WIN32_FILE_ATTRIBUTE_DATA fad = {};
			if (!::GetFileAttributesEx(DatArc_StagePath(names[i]), GetFileExInfoStandard, &fad))
				continue; /* 未展開: 既存塊を維持 */
			if (!DatArc_StageMatchesEnt(oi, fad)) {
				allSame = FALSE;
				break;
			}
		}
		if (allSame) {
			g_flushDirty = FALSE;
			return TRUE;
		}
	}

	// 圧縮バッファと生データ (未変更分は旧マップの圧縮塊を流用)
	BYTE* unc[DATARC_MAX];
	BYTE* cmp[DATARC_MAX];
	ULONGLONG uncSz[DATARC_MAX];
	ULONGLONG cmpSz[DATARC_MAX];
	ULONGLONG hashV[DATARC_MAX];
	BYTE cmpOwned[DATARC_MAX]; // 1=malloc、0=旧マップ参照
	BYTE hashOk[DATARC_MAX];
	ZeroMemory(unc, sizeof(unc));
	ZeroMemory(cmp, sizeof(cmp));
	ZeroMemory(uncSz, sizeof(uncSz));
	ZeroMemory(cmpSz, sizeof(cmpSz));
	ZeroMemory(hashV, sizeof(hashV));
	ZeroMemory(cmpOwned, sizeof(cmpOwned));
	ZeroMemory(hashOk, sizeof(hashOk));

	BOOL ok = TRUE;
	for (int i = 0; i < nNames && ok; ++i) {
		const CString path = DatArc_StagePath(names[i]);
		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		const BOOL gotFad = ::GetFileAttributesEx(path, GetFileExInfoStandard, &fad) != 0;
		const int oi = DatArc_FindIndex(names[i]);
		if (gotFad && oi >= 0 && DatArc_StageMatchesEnt(oi, fad)
			&& !(g_verifyContent && g_ent[oi].hashValid)) {
			uncSz[i] = g_ent[oi].uncSize;
			cmpSz[i] = g_ent[oi].cmpSize;
			hashV[i] = g_ent[oi].contentHash;
			hashOk[i] = g_ent[oi].hashValid;
			if (cmpSz[i] > 0 && g_arcMap)
				cmp[i] = g_arcMap + (size_t)g_ent[oi].offset;
			cmpOwned[i] = 0;
			continue;
		}
		if (!gotFad) {
			if (oi >= 0 && g_arcMap &&
				g_ent[oi].offset + g_ent[oi].cmpSize <= g_arcMapSize) {
				uncSz[i] = g_ent[oi].uncSize;
				cmpSz[i] = g_ent[oi].cmpSize;
				hashV[i] = g_ent[oi].contentHash;
				hashOk[i] = g_ent[oi].hashValid;
				if (cmpSz[i] > 0)
					cmp[i] = g_arcMap + (size_t)g_ent[oi].offset;
				cmpOwned[i] = 0;
				continue;
			}
			ok = FALSE;
			break;
		}
		if (!DatArc_ReadWholeFile(path, &unc[i], &uncSz[i])) {
			ok = FALSE;
			break;
		}
		{
			const ULONGLONG h = DatArc_Fnv64(unc[i], (size_t)uncSz[i]);
			BOOL reused = FALSE;
			if (oi >= 0 && g_ent[oi].hashValid && h == g_ent[oi].contentHash
				&& uncSz[i] == g_ent[oi].uncSize) {
				const BOOL blobOk = (g_ent[oi].cmpSize == 0) ||
					(g_arcMap && g_ent[oi].offset + g_ent[oi].cmpSize <= g_arcMapSize);
				if (blobOk) {
					cmpSz[i] = g_ent[oi].cmpSize;
					hashV[i] = h;
					hashOk[i] = 1;
					if (cmpSz[i] > 0)
						cmp[i] = g_arcMap + (size_t)g_ent[oi].offset;
					cmpOwned[i] = 0;
					free(unc[i]);
					unc[i] = NULL;
					DatArc_NoteStageFp(oi, path);
					reused = TRUE;
				}
			}
			if (reused)
				continue;
			hashV[i] = h;
			hashOk[i] = 1;
		}
		const size_t bound = ZSTD_compressBound((size_t)uncSz[i]);
		cmp[i] = (BYTE*)malloc(bound ? bound : 1);
		if (!cmp[i]) { ok = FALSE; break; }
		cmpOwned[i] = 1;
		int zlev = DATARC_ZSTD_LEVEL;
		/* プレイリストや曲パラメータは大きい。対話中の保存は速度優先 */
		if (uncSz[i] > (64ull * 1024ull))
			zlev = 1;
		const size_t csz = ZSTD_compress(cmp[i], bound, unc[i], (size_t)uncSz[i], zlev);
		if (ZSTD_isError(csz)) { ok = FALSE; break; }
		cmpSz[i] = (ULONGLONG)csz;
	}

	BYTE* out = NULL;
	ULONGLONG total = 0;
	int done = 0; /* 0=失敗 1=全体書き直し不要 2=tmp へ書き出した */
	if (ok) {
		BOOL anyOwned = FALSE;
		BOOL sameSet = (nNames == g_n && g_arcMap != NULL);
		for (int i = 0; i < nNames; ++i) {
			if (cmpOwned[i]) anyOwned = TRUE;
			if (sameSet && DatArc_FindIndex(names[i]) < 0)
				sameSet = FALSE;
		}
		if (!anyOwned && sameSet) {
			done = 1;
		} else if (anyOwned && sameSet &&
			DatArc_TryPatchMembers(names, nNames, cmp, uncSz, cmpSz, cmpOwned, hashV, hashOk)) {
			done = 1;
		} else {
			const ULONGLONG hdrBytes = sizeof(DatArcFileHdr) + (ULONGLONG)nNames * sizeof(DatArcIdxEnt);
			total = hdrBytes;
			for (int i = 0; i < nNames; ++i)
				total += DatArc_SlotStore(cmpSz[i]);
			if (total == 0 || total > 0xffffffffULL) {
				ok = FALSE;
			} else {
				out = (BYTE*)malloc((size_t)total);
				if (!out) {
					ok = FALSE;
				} else {
					ZeroMemory(out, (size_t)total);
					DatArcFileHdr* hdr = (DatArcFileHdr*)out;
					hdr->magic = DATARC_MAGIC;
					hdr->version = DATARC_VER;
					hdr->count = (DWORD)nNames;
					hdr->reserved = 0;
					DatArcIdxEnt* idx = (DatArcIdxEnt*)(out + sizeof(DatArcFileHdr));
					ULONGLONG off = hdrBytes;
					for (int i = 0; i < nNames; ++i) {
						ZeroMemory(&idx[i], sizeof(idx[i]));
						DatArc_NameToUtf8(names[i], idx[i].name, DATARC_NAME);
						idx[i].uncSize = uncSz[i];
						idx[i].cmpSize = cmpSz[i];
						idx[i].offset = off;
						if (cmpSz[i] > 0 && cmp[i])
							memcpy(out + (size_t)off, cmp[i], (size_t)cmpSz[i]);
						off += DatArc_SlotStore(cmpSz[i]);
					}

					TCHAR tmpPath[MAX_PATH];
					_tcsncpy(tmpPath, g_arcPath, MAX_PATH - 1);
					tmpPath[MAX_PATH - 1] = 0;
					size_t tlen = _tcslen(tmpPath);
					if (tlen + 5 < MAX_PATH)
						_tcscat(tmpPath, _T(".tmp"));
					else
						ok = FALSE;

					if (ok && DatArc_WriteWholeFile(tmpPath, out, total)) {
						::SetFileAttributes(g_arcPath, FILE_ATTRIBUTE_NORMAL);
						::DeleteFile(g_arcPath);
						if (!::MoveFile(tmpPath, g_arcPath)) {
							::DeleteFile(tmpPath);
							ok = FALSE;
						} else {
							done = 2;
						}
					} else {
						::DeleteFile(tmpPath);
						ok = FALSE;
					}
				}
			}
		}
	}

	for (int i = 0; i < nNames; ++i) {
		if (unc[i]) free(unc[i]);
		if (cmpOwned[i] && cmp[i]) free(cmp[i]);
	}

	if (!ok || done == 0) {
		if (out) free(out);
		return FALSE;
	}
	if (done == 1) {
		g_flushDirty = FALSE;
		return TRUE;
	}

	/* 書き出したバッファをそのままマップにする。ディスクから読み直さない */
	DatArc_FreeMap();
	g_arcMap = out;
	g_arcMapSize = total;
	if (!DatArc_ParseIndexFromMap())
		return FALSE;
	for (int i = 0; i < nNames; ++i) {
		const int ei = DatArc_FindIndex(names[i]);
		if (ei < 0 || !hashOk[i]) continue;
		g_ent[ei].contentHash = hashV[i];
		g_ent[ei].hashValid = 1;
	}
	DatArc_RefreshStageFingerprints();
	g_flushDirty = FALSE;
	return TRUE;
}

static BOOL DatArc_MigrateLooseFromExeDir()
{
	if (!DatArc_EnsureStageDir()) return FALSE;

	CString pattern = g_exeDir;
	pattern += _T("*.dat");
	WIN32_FIND_DATA fd;
	HANDLE h = ::FindFirstFile(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return TRUE; // 何も無い

	TCHAR packed[DATARC_MAX][DATARC_NAME];
	int nPacked = 0;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		if (!DatArc_IsPackableLeaf(fd.cFileName)) continue;
		CString src = g_exeDir;
		src += fd.cFileName;
		CString dst = DatArc_StagePath(fd.cFileName);
		::SetFileAttributes(dst, FILE_ATTRIBUTE_NORMAL);
		::DeleteFile(dst);
		if (!::CopyFile(src, dst, FALSE))
			continue;
		if (nPacked < DATARC_MAX) {
			_tcsncpy(packed[nPacked], fd.cFileName, DATARC_NAME - 1);
			packed[nPacked][DATARC_NAME - 1] = 0;
			++nPacked;
		}
	} while (::FindNextFile(h, &fd));
	::FindClose(h);

	if (nPacked <= 0)
		return TRUE;

	if (!DatArc_RebuildFromStage())
		return FALSE;

	// 古いバラ .dat を削除（アーカイブ本体は残す）
	for (int i = 0; i < nPacked; ++i) {
		CString old = g_exeDir;
		old += packed[i];
		::SetFileAttributes(old, FILE_ATTRIBUTE_NORMAL);
		::DeleteFile(old);
	}
	return TRUE;
}

BOOL DatArc_Init(LPCTSTR exeDirWithSlash)
{
	if (!g_arcCsInit) {
		InitializeCriticalSection(&g_arcCs);
		g_arcCsInit = 1;
	}
	g_ready = FALSE;
	g_n = 0;
	DatArc_FreeMap();
	ZeroMemory(g_exeDir, sizeof(g_exeDir));
	ZeroMemory(g_arcPath, sizeof(g_arcPath));
	ZeroMemory(g_stageDir, sizeof(g_stageDir));
	ZeroMemory(g_ent, sizeof(g_ent));

	if (!exeDirWithSlash || !exeDirWithSlash[0])
		return FALSE;
	_tcsncpy(g_exeDir, exeDirWithSlash, MAX_PATH - 1);

	_tcsncpy(g_arcPath, g_exeDir, MAX_PATH - 1);
	if (_tcslen(g_arcPath) + _tcslen(DATARC_NAME_W) + 1 < MAX_PATH)
		_tcscat(g_arcPath, DATARC_NAME_W);
	else
		return FALSE;

	TCHAR temp[MAX_PATH];
	const DWORD nTemp = ::GetTempPath(MAX_PATH, temp);
	if (nTemp == 0 || nTemp >= MAX_PATH)
		return FALSE;
	_sntprintf(g_stageDir, MAX_PATH - 1, _T("%s%s\\"), temp, DATARC_STAGE_SUB);
	g_stageDir[MAX_PATH - 1] = 0;

	if (!DatArc_EnsureStageDir())
		return FALSE;

	// 毎回クリーンなステージから開始（前回残骸を消す）
	{
		CString pattern = g_stageDir;
		pattern += _T("*.*");
		WIN32_FIND_DATA fd;
		HANDLE h = ::FindFirstFile(pattern, &fd);
		if (h != INVALID_HANDLE_VALUE) {
			do {
				if (fd.cFileName[0] == _T('.') && (fd.cFileName[1] == 0 || (fd.cFileName[1] == _T('.') && fd.cFileName[2] == 0)))
					continue;
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
				CString p = g_stageDir;
				p += fd.cFileName;
				::SetFileAttributes(p, FILE_ATTRIBUTE_NORMAL);
				::DeleteFile(p);
			} while (::FindNextFile(h, &fd));
			::FindClose(h);
		}
	}

	const BOOL hasArc = ::PathFileExists(g_arcPath);
	if (hasArc) {
		if (!DatArc_LoadArcIntoMemory() || !DatArc_ParseIndexFromMap())
			return FALSE;
		if (!DatArc_ExtractStartupLeaves())
			return FALSE;
		DatArc_RefreshStageFingerprints();
	} else {
		// 無ければバラ .dat を集めて作成し、古いのは削除
		if (!DatArc_MigrateLooseFromExeDir())
			return FALSE;
		// 移行後もアーカイブが無ければ空で作る
		if (!::PathFileExists(g_arcPath)) {
			if (!DatArc_RebuildFromStage())
				return FALSE;
		} else {
			if (!DatArc_LoadArcIntoMemory() || !DatArc_ParseIndexFromMap())
				return FALSE;
			DatArc_RefreshStageFingerprints();
		}
	}

	g_ready = TRUE;
	return TRUE;
}

void DatArc_Shutdown()
{
	DatArcLock lk;
	g_flushSuspend = 0;
	if (g_ready)
		DatArc_FlushAll();
	DatArc_FreeMap();
	g_ready = FALSE;
	g_n = 0;
}

LPCTSTR DatArc_StageDir()
{
	return g_stageDir;
}

void DatArc_Chdir()
{
	DatArcLock lk;
	if (g_stageDir[0])
		_tchdir(g_stageDir);
}

CString DatArc_Path(LPCTSTR leaf)
{
	DatArcLock lk;
	if (!leaf || !leaf[0])
		return CString();
	if (g_ready) {
		const int i = DatArc_FindIndex(leaf);
		if (i >= 0)
			DatArc_ExtractOne(i);
		else
			DatArc_EnsureStageDir();
		return DatArc_StagePath(leaf);
	}
	// 未初期化時は従来どおり exe 隣
	CString s = g_exeDir[0] ? g_exeDir : CString();
	s += leaf;
	return s;
}

BOOL DatArc_Exists(LPCTSTR leaf)
{
	DatArcLock lk;
	if (!leaf || !leaf[0]) return FALSE;
	if (g_ready) {
		if (DatArc_FindIndex(leaf) >= 0) return TRUE;
		return ::PathFileExists(DatArc_StagePath(leaf));
	}
	CString s = g_exeDir;
	s += leaf;
	return ::PathFileExists(s);
}

BOOL DatArc_Commit(LPCTSTR leaf)
{
	DatArcLock lk;
	if (!g_ready || !leaf || !leaf[0]) return FALSE;
	if (!DatArc_IsPackableLeaf(leaf)) return FALSE;
	DatArc_AddIndex(leaf);
	/* 同サイズ上書きだと指紋が残って古い塊が再利用されることがある */
	{
		const int i = DatArc_FindIndex(leaf);
		if (i >= 0) g_ent[i].stageFpValid = 0;
	}
	if (g_flushSuspend > 0) {
		g_flushDirty = TRUE;
		return TRUE;
	}
	return DatArc_RebuildFromStage();
}

void DatArc_InvalidateLeaf(LPCTSTR leaf)
{
	DatArcLock lk;
	if (!g_ready || !leaf || !leaf[0]) return;
	const int i = DatArc_FindIndex(leaf);
	if (i >= 0) g_ent[i].stageFpValid = 0;
}

BOOL DatArc_Delete(LPCTSTR leaf)
{
	DatArcLock lk;
	if (!g_ready || !leaf || !leaf[0]) return FALSE;
	CString path = DatArc_StagePath(leaf);
	::SetFileAttributes(path, FILE_ATTRIBUTE_NORMAL);
	::DeleteFile(path);
	const int i = DatArc_FindIndex(leaf);
	if (i >= 0) {
		for (int j = i; j < g_n - 1; ++j)
			g_ent[j] = g_ent[j + 1];
		--g_n;
	}
	if (g_flushSuspend > 0) {
		g_flushDirty = TRUE;
		return TRUE;
	}
	return DatArc_RebuildFromStage();
}

BOOL DatArc_Rename(LPCTSTR fromLeaf, LPCTSTR toLeaf)
{
	DatArcLock lk;
	if (!g_ready || !fromLeaf || !toLeaf) return FALSE;
	CString from = DatArc_StagePath(fromLeaf);
	CString to = DatArc_StagePath(toLeaf);
	::SetFileAttributes(to, FILE_ATTRIBUTE_NORMAL);
	::DeleteFile(to);
	if (!::MoveFile(from, to)) {
		if (::PathFileExists(from))
			return FALSE;
	}
	const int i = DatArc_FindIndex(fromLeaf);
	if (i >= 0) {
		_tcsncpy(g_ent[i].nameW, toLeaf, DATARC_NAME - 1);
		g_ent[i].nameW[DATARC_NAME - 1] = 0;
		DatArc_NameToUtf8(toLeaf, g_ent[i].nameU8, DATARC_NAME);
		g_ent[i].stageFpValid = 0;
	} else {
		DatArc_AddIndex(toLeaf);
	}
	if (g_flushSuspend > 0) {
		g_flushDirty = TRUE;
		return TRUE;
	}
	return DatArc_RebuildFromStage();
}

BOOL DatArc_FlushAll()
{
	DatArcLock lk;
	if (!g_ready) return FALSE;
	if (g_flushSuspend > 0) {
		g_flushDirty = TRUE;
		return TRUE;
	}
	/* 全メンバを再圧縮しない。ステージ上のファイルは内容ハッシュが違うときだけ圧縮し直す。 */
	struct VerifyScope {
		VerifyScope() { g_verifyContent = 1; }
		~VerifyScope() { g_verifyContent = 0; }
	} verifyScope;
	return DatArc_RebuildFromStage();
}

void DatArc_FlushSuspend(BOOL suspend)
{
	DatArcLock lk;
	if (suspend) {
		++g_flushSuspend;
		return;
	}
	if (g_flushSuspend > 0)
		--g_flushSuspend;
	if (g_flushSuspend == 0 && g_flushDirty && g_ready)
		DatArc_RebuildFromStage();
}
