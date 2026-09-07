// KpiEnumCache.cpp — KPI/外部プラグイン列挙結果を LocalAppData にキャッシュ
// 指紋: 全 .kpi + (名前 or PEエクスポート候補) .dll の path/size/mtime（LoadLibrary なし）
// エントリ: path, size, mtime, arch(32/64), kind(KPI/Winamp/…), 拡張子ごとの kvar(v2/v5/0)
// ※対応拡張子そのものはフル列挙で得た台帳のコピー。PE だけでは取れない。
#include "stdafx.h"
#include "KpiEnumCache.h"
#include "PluginKinds.h"
#include "PluginForeignEnum.h"
#include <shlobj.h>
#include <vector>
#include <string>
#include <algorithm>

extern BYTE plugkind[];
extern int kpicnt;
extern CString kpif[];
extern CString ext[][300];
extern BYTE kvar[][300];
extern BYTE kpiarch[];

enum { KE_MAX_PLUG = 150, KE_MAX_EXT = 299, KE_CACHE_VER = 3 };

struct KeFileFp {
	ULONGLONG size;
	FILETIME mtime;
	std::wstring path;
};

static void KeNormPath(std::wstring& p)
{
	for (size_t i = 0; i < p.size(); i++) {
		if (p[i] == L'/') p[i] = L'\\';
		if (p[i] >= L'A' && p[i] <= L'Z') p[i] = (wchar_t)(p[i] - L'A' + L'a');
	}
	while (!p.empty() && (p.back() == L'\\' || p.back() == L' ')) p.pop_back();
}

static int KeGetFp(const wchar_t* path, KeFileFp* out)
{
	if (!path || !out) return 0;
	WIN32_FILE_ATTRIBUTE_DATA fad = {};
	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) return 0;
	ULARGE_INTEGER sz;
	sz.LowPart = fad.nFileSizeLow;
	sz.HighPart = fad.nFileSizeHigh;
	out->size = sz.QuadPart;
	out->mtime = fad.ftLastWriteTime;
	out->path = path;
	KeNormPath(out->path);
	return 1;
}

static int KeSameMtimeSize(const KeFileFp& a, ULONGLONG size, FILETIME mt)
{
	return a.size == size
		&& a.mtime.dwLowDateTime == mt.dwLowDateTime
		&& a.mtime.dwHighDateTime == mt.dwHighDateTime;
}

static ULONGLONG KeHashMix(ULONGLONG h, ULONGLONG v)
{
	h ^= v;
	h *= 1099511628211ULL;
	return h;
}

static ULONGLONG KeHashPath(const std::wstring& p)
{
	ULONGLONG h = 14695981039346656037ULL;
	for (size_t i = 0; i < p.size(); i++)
		h = KeHashMix(h, (ULONGLONG)p[i]);
	return h;
}

static ULONGLONG KeHashFpList(std::vector<KeFileFp>& files)
{
	std::sort(files.begin(), files.end(),
		[](const KeFileFp& a, const KeFileFp& b) { return a.path < b.path; });
	ULONGLONG h = 14695981039346656037ULL;
	h = KeHashMix(h, (ULONGLONG)files.size());
	for (size_t i = 0; i < files.size(); i++) {
		h = KeHashMix(h, KeHashPath(files[i].path));
		h = KeHashMix(h, files[i].size);
		h = KeHashMix(h, ((ULONGLONG)files[i].mtime.dwHighDateTime << 32)
			| (ULONGLONG)files[i].mtime.dwLowDateTime);
	}
	return h;
}

static int KeIsHiddenOggDir(const wchar_t* name)
{
	return (name && _wcsnicmp(name, L".ogg_", 5) == 0) ? 1 : 0;
}

static void KeCollectPluginFilesRecursive(const std::wstring& dir, std::vector<KeFileFp>& out)
{
	std::wstring pat = dir;
	if (!pat.empty() && pat.back() != L'\\') pat += L'\\';
	pat += L"*";
	WIN32_FIND_DATAW fd{};
	HANDLE h = FindFirstFileW(pat.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
		std::wstring full = dir;
		if (!full.empty() && full.back() != L'\\') full += L'\\';
		full += fd.cFileName;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (KeIsHiddenOggDir(fd.cFileName)) continue;
			KeCollectPluginFilesRecursive(full, out);
			continue;
		}
		const size_t n = wcslen(fd.cFileName);
		if (n < 5) continue;
		const int isKpi = (_wcsicmp(fd.cFileName + n - 4, L".kpi") == 0);
		const int isDll = (_wcsicmp(fd.cFileName + n - 4, L".dll") == 0);
		if (!isKpi && !isDll) continue;
		if (isDll) {
			/* LoadLibrary せず名前+PEエクスポートで候補判定（Winamp/XMPlay/AIMP） */
			if (!PluginForeign_IsCandidatePath(CString(fd.cFileName), CString(full.c_str())))
				continue;
		}
		KeFileFp fp;
		if (KeGetFp(full.c_str(), &fp))
			out.push_back(fp);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static int KeCacheDir(wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return 0;
	out[0] = 0;
	wchar_t base[MAX_PATH] = {};
	if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base)) || !base[0]) {
		DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) {
			if (!GetTempPathW(MAX_PATH, base) || !base[0]) return 0;
		}
	}
	wchar_t app[MAX_PATH];
	_snwprintf_s(app, _TRUNCATE, L"%s\\oggYSED", base);
	CreateDirectoryW(app, NULL);
	_snwprintf_s(out, (size_t)outChars, _TRUNCATE, L"%s\\oggYSED\\kpicache", base);
	CreateDirectoryW(out, NULL);
	return 1;
}

static int KeCachePath(wchar_t* out, int outChars)
{
	wchar_t dir[MAX_PATH];
	if (!KeCacheDir(dir, MAX_PATH)) return 0;
	_snwprintf_s(out, (size_t)outChars, _TRUNCATE, L"%s\\plugins.cache", dir);
	return 1;
}

void KpiEnumCache_Invalidate(void)
{
	wchar_t path[MAX_PATH];
	if (!KeCachePath(path, MAX_PATH)) return;
	DeleteFileW(path);
}

static int KeWrite(HANDLE h, const void* p, DWORD n)
{
	DWORD wr = 0;
	return WriteFile(h, p, n, &wr, NULL) && wr == n;
}

static int KeRead(HANDLE h, void* p, DWORD n)
{
	DWORD rd = 0;
	return ReadFile(h, p, n, &rd, NULL) && rd == n;
}

static int KeWriteWStr(HANDLE h, const wchar_t* s)
{
	DWORD n = s ? (DWORD)wcslen(s) : 0;
	if (n > 2000) n = 2000;
	if (!KeWrite(h, &n, 4)) return 0;
	if (n && !KeWrite(h, s, n * sizeof(wchar_t))) return 0;
	return 1;
}

static int KeReadWStr(HANDLE h, CString& out)
{
	DWORD n = 0;
	if (!KeRead(h, &n, 4) || n > 2000) return 0;
	out.Empty();
	if (!n) return 1;
	std::wstring buf;
	buf.resize(n);
	if (!KeRead(h, &buf[0], n * sizeof(wchar_t))) return 0;
	out = buf.c_str();
	return 1;
}

static ULONGLONG KeQuickFingerprint(LPCTSTR rootDir)
{
	std::wstring root = rootDir ? rootDir : L"";
	KeNormPath(root);
	std::vector<KeFileFp> files;
	KeCollectPluginFilesRecursive(root, files);
	return KeHashFpList(files);
}

static int KeValidKindArchVer(BYTE kind, BYTE arch, BYTE ver)
{
	if (arch != 32 && arch != 64) return 0;
	if (kind == PLUGKIND_KPI)
		return (ver == 2 || ver == 5) ? 1 : 0;
	if (kind == PLUGKIND_WINAMP || kind == PLUGKIND_XMPLAY || kind == PLUGKIND_AIMP)
		return 1; /* kvar は 0 */
	return 0;
}

static int KeCountExts(int plugIdx)
{
	int n = 0;
	while (n < KE_MAX_EXT && !ext[plugIdx][n].IsEmpty())
		n++;
	return n;
}

static int KeEntrySavable(int i)
{
	if (i < 0 || i >= kpicnt) return 0;
	if (kpif[i].IsEmpty()) return 0;
	const BYTE kind = plugkind[i];
	const BYTE arch = kpiarch[i];
	const int extN = KeCountExts(i);
	if (extN <= 0) return 0; /* 拡張子無しは再生マッチ不可＝保存しない */
	const BYTE ver = (kind == PLUGKIND_KPI) ? kvar[i][0] : (BYTE)0;
	return KeValidKindArchVer(kind, arch, ver);
}

BOOL KpiEnumCache_TryApply(LPCTSTR rootDir)
{
	if (!rootDir || !rootDir[0]) return FALSE;
	wchar_t path[MAX_PATH];
	if (!KeCachePath(path, MAX_PATH)) return FALSE;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return FALSE;

	char magic[8];
	ULONGLONG fp = 0;
	DWORD count = 0;
	int ok = KeRead(h, magic, 8)
		&& magic[0] == 'K' && magic[1] == 'P' && magic[2] == 'I' && magic[3] == 'C'
		&& magic[4] == (char)KE_CACHE_VER
		&& KeRead(h, &fp, 8)
		&& KeRead(h, &count, 4)
		&& count > 0 && count <= (DWORD)KE_MAX_PLUG;
	const ULONGLONG nowFp = KeQuickFingerprint(rootDir);
	if (!ok || fp != nowFp) {
		CloseHandle(h);
		return FALSE;
	}

	kpicnt = 0;
	for (DWORD i = 0; ok && i < count; i++) {
		CString filePath;
		ULONGLONG size = 0;
		FILETIME mt = {};
		BYTE arch = 0, kind = 0;
		DWORD extN = 0;
		ok = KeReadWStr(h, filePath)
			&& KeRead(h, &size, 8)
			&& KeRead(h, &mt, sizeof(mt))
			&& KeRead(h, &arch, 1)
			&& KeRead(h, &kind, 1)
			&& KeRead(h, &extN, 4)
			&& extN > 0 && extN <= (DWORD)KE_MAX_EXT;
		if (!ok) break;

		KeFileFp disk;
		if (!KeGetFp(filePath, &disk) || !KeSameMtimeSize(disk, size, mt)) {
			ok = 0;
			break;
		}

		for (DWORD e = 0; e < 300; e++) {
			ext[kpicnt][e].Empty();
			kvar[kpicnt][e] = 0;
		}

		BYTE firstVer = 0;
		for (DWORD e = 0; ok && e < extN; e++) {
			CString ex;
			BYTE kv = 0;
			ok = KeReadWStr(h, ex) && KeRead(h, &kv, 1);
			if (!ok) break;
			if (ex.IsEmpty()) { ok = 0; break; }
			if (e == 0) firstVer = kv;
			if (e < (DWORD)KE_MAX_EXT) {
				ext[kpicnt][e] = ex;
				kvar[kpicnt][e] = kv;
			}
		}
		if (!ok) break;
		if (!KeValidKindArchVer(kind, arch, (kind == PLUGKIND_KPI) ? firstVer : (BYTE)0)) {
			ok = 0;
			break;
		}

		kpif[kpicnt] = filePath;
		kpiarch[kpicnt] = arch;
		plugkind[kpicnt] = kind;
		ext[kpicnt][299] = L"";
		kpicnt++;
	}
	CloseHandle(h);
	if (!ok || kpicnt <= 0 || (DWORD)kpicnt != count) {
		kpicnt = 0;
		return FALSE;
	}
	return TRUE;
}

void KpiEnumCache_Save(LPCTSTR rootDir)
{
	if (!rootDir || !rootDir[0] || kpicnt <= 0) return;
	wchar_t path[MAX_PATH], tmp[MAX_PATH];
	if (!KeCachePath(path, MAX_PATH)) return;

	DWORD count = 0;
	for (int i = 0; i < kpicnt && i < KE_MAX_PLUG; i++) {
		if (KeEntrySavable(i)) count++;
	}
	if (count == 0) return;

	_snwprintf_s(tmp, _TRUNCATE, L"%s.part", path);
	DeleteFileW(tmp);
	HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;

	char kMagic[8] = { 'K','P','I','C', (char)KE_CACHE_VER, 0, 0, 0 };
	const ULONGLONG fp = KeQuickFingerprint(rootDir);
	int ok = KeWrite(h, kMagic, 8) && KeWrite(h, &fp, 8) && KeWrite(h, &count, 4);
	for (int i = 0; ok && i < kpicnt && i < KE_MAX_PLUG; i++) {
		if (!KeEntrySavable(i)) continue;
		KeFileFp disk;
		if (!KeGetFp(kpif[i], &disk)) { ok = 0; break; }
		BYTE arch = kpiarch[i];
		BYTE kind = plugkind[i];
		DWORD extN = (DWORD)KeCountExts(i);
		ok = KeWriteWStr(h, kpif[i])
			&& KeWrite(h, &disk.size, 8)
			&& KeWrite(h, &disk.mtime, sizeof(disk.mtime))
			&& KeWrite(h, &arch, 1)
			&& KeWrite(h, &kind, 1)
			&& KeWrite(h, &extN, 4);
		for (DWORD e = 0; ok && e < extN; e++) {
			BYTE kv = kvar[i][e];
			ok = KeWriteWStr(h, ext[i][e]) && KeWrite(h, &kv, 1);
		}
	}
	CloseHandle(h);
	if (!ok) {
		DeleteFileW(tmp);
		return;
	}
	DeleteFileW(path);
	if (!MoveFileW(tmp, path)) {
		CopyFileW(tmp, path, FALSE);
		DeleteFileW(tmp);
	}
}
