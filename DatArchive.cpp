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
		++g_n;
	}
	return TRUE;
}

static BOOL DatArc_ExtractOne(int i)
{
	if (i < 0 || i >= g_n) return FALSE;
	DatArcMemEnt& e = g_ent[i];
	const CString path = DatArc_StagePath(e.nameW);
	if (::PathFileExists(path))
		return TRUE;
	if (!g_arcMap || e.cmpSize == 0) {
		// 空メンバー
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
	const BOOL ok = DatArc_WriteWholeFile(path, dst, e.uncSize);
	free(dst);
	return ok;
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

static BOOL DatArc_RebuildFromStage()
{
	if (!DatArc_EnsureStageDir()) return FALSE;

	// ステージ上の packable を列挙
	TCHAR names[DATARC_MAX][DATARC_NAME];
	int nNames = 0;
	CString pattern = g_stageDir;
	pattern += _T("*.dat");
	WIN32_FIND_DATA fd;
	HANDLE h = ::FindFirstFile(pattern, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			if (!DatArc_IsPackableLeaf(fd.cFileName)) continue;
			if (nNames >= DATARC_MAX) break;
			_tcsncpy(names[nNames], fd.cFileName, DATARC_NAME - 1);
			names[nNames][DATARC_NAME - 1] = 0;
			++nNames;
		} while (::FindNextFile(h, &fd));
		::FindClose(h);
	}

	// 変更が無ければアーカイブ再生成を省略
	if (nNames == g_n && g_arcMap && ::PathFileExists(g_arcPath)) {
		BOOL allSame = TRUE;
		for (int i = 0; i < nNames; ++i) {
			const int oi = DatArc_FindIndex(names[i]);
			if (oi < 0) { allSame = FALSE; break; }
			WIN32_FILE_ATTRIBUTE_DATA fad = {};
			if (!::GetFileAttributesEx(DatArc_StagePath(names[i]), GetFileExInfoStandard, &fad)
				|| !DatArc_StageMatchesEnt(oi, fad)) {
				allSame = FALSE;
				break;
			}
		}
		if (allSame)
			return TRUE;
	}

	// 圧縮バッファと生データ (未変更分は旧マップの圧縮塊を流用)
	BYTE* unc[DATARC_MAX];
	BYTE* cmp[DATARC_MAX];
	ULONGLONG uncSz[DATARC_MAX];
	ULONGLONG cmpSz[DATARC_MAX];
	BYTE cmpOwned[DATARC_MAX]; // 1=malloc、0=旧マップ参照
	ZeroMemory(unc, sizeof(unc));
	ZeroMemory(cmp, sizeof(cmp));
	ZeroMemory(uncSz, sizeof(uncSz));
	ZeroMemory(cmpSz, sizeof(cmpSz));
	ZeroMemory(cmpOwned, sizeof(cmpOwned));

	BOOL ok = TRUE;
	for (int i = 0; i < nNames && ok; ++i) {
		const CString path = DatArc_StagePath(names[i]);
		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		const BOOL gotFad = ::GetFileAttributesEx(path, GetFileExInfoStandard, &fad) != 0;
		const int oi = DatArc_FindIndex(names[i]);
		if (gotFad && oi >= 0 && DatArc_StageMatchesEnt(oi, fad)) {
			uncSz[i] = g_ent[oi].uncSize;
			cmpSz[i] = g_ent[oi].cmpSize;
			if (cmpSz[i] > 0)
				cmp[i] = g_arcMap + (size_t)g_ent[oi].offset;
			cmpOwned[i] = 0;
			continue;
		}
		if (!DatArc_ReadWholeFile(path, &unc[i], &uncSz[i])) {
			ok = FALSE;
			break;
		}
		const size_t bound = ZSTD_compressBound((size_t)uncSz[i]);
		cmp[i] = (BYTE*)malloc(bound ? bound : 1);
		if (!cmp[i]) { ok = FALSE; break; }
		cmpOwned[i] = 1;
		const size_t csz = ZSTD_compress(cmp[i], bound, unc[i], (size_t)uncSz[i], DATARC_ZSTD_LEVEL);
		if (ZSTD_isError(csz)) { ok = FALSE; break; }
		cmpSz[i] = (ULONGLONG)csz;
	}

	if (ok) {
		const ULONGLONG hdrBytes = sizeof(DatArcFileHdr) + (ULONGLONG)nNames * sizeof(DatArcIdxEnt);
		ULONGLONG total = hdrBytes;
		for (int i = 0; i < nNames; ++i)
			total += cmpSz[i];

		BYTE* out = (BYTE*)malloc((size_t)total);
		if (!out) {
			ok = FALSE;
		} else {
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
				off += cmpSz[i];
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
				}
			} else {
				::DeleteFile(tmpPath);
				ok = FALSE;
			}
			free(out);
		}
	}

	for (int i = 0; i < nNames; ++i) {
		if (unc[i]) free(unc[i]);
		if (cmpOwned[i] && cmp[i]) free(cmp[i]);
	}

	if (!ok) return FALSE;

	// メモリ上のインデックスを再読込
	if (!DatArc_LoadArcIntoMemory() || !DatArc_ParseIndexFromMap())
		return FALSE;
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
		if (!DatArc_ExtractAll())
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
	if (g_stageDir[0])
		_tchdir(g_stageDir);
}

CString DatArc_Path(LPCTSTR leaf)
{
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
	if (!g_ready || !leaf || !leaf[0]) return FALSE;
	if (!DatArc_IsPackableLeaf(leaf)) return FALSE;
	DatArc_AddIndex(leaf);
	if (g_flushSuspend > 0) {
		g_flushDirty = TRUE;
		return TRUE;
	}
	return DatArc_RebuildFromStage();
}

BOOL DatArc_Delete(LPCTSTR leaf)
{
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
	if (!g_ready) return FALSE;
	if (g_flushSuspend > 0) {
		g_flushDirty = TRUE;
		return TRUE;
	}
	return DatArc_RebuildFromStage();
}

void DatArc_FlushSuspend(BOOL suspend)
{
	if (suspend) {
		++g_flushSuspend;
		return;
	}
	if (g_flushSuspend > 0)
		--g_flushSuspend;
	if (g_flushSuspend == 0 && g_flushDirty && g_ready)
		DatArc_RebuildFromStage();
}
