#include "OxzWork.h"
#include "OxzArc.h"
#include "OxzLang.h"
#include "OxzFilt.h"
#include "../OxzCore/OxzCore.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

std::wstring OxzExeDir()
{
	wchar_t exe[32768];
	GetModuleFileNameW(NULL, exe, 32768);
	wchar_t* s = wcsrchr(exe, L'\\');
	if (s) *s = 0;
	return exe;
}

std::wstring OxzBeside(const wchar_t* name)
{
	return OxzExeDir() + L"\\" + name;
}

bool OxzHas7za()
{
	return GetFileAttributesW(OxzBeside(L"7za.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool OxzIsOxz(const std::wstring& path)
{
	size_t n = path.size();
	return n >= 4 && _wcsicmp(path.c_str() + n - 4, L".oxz") == 0;
}

std::wstring OxzBaseName(const std::wstring& path)
{
	size_t s = path.find_last_of(L"\\/");
	return s == std::wstring::npos ? path : path.substr(s + 1);
}

std::wstring OxzParent(const std::wstring& path)
{
	size_t s = path.find_last_of(L"\\/");
	if (s == std::wstring::npos) return L"";
	if (s == 2 && path[1] == L':') return path.substr(0, 3);
	return path.substr(0, s);
}

std::wstring OxzQuote(const std::wstring& s)
{
	return L"\"" + s + L"\"";
}

static bool OxzReadAll(HANDLE h, unsigned char* buf, size_t n)
{
	size_t off = 0;
	while (off < n) {
		DWORD chunk = (DWORD)((n - off) > (1u << 20) ? (1u << 20) : (n - off));
		DWORD rd = 0;
		if (!ReadFile(h, buf + off, chunk, &rd, NULL) || rd == 0) return false;
		off += rd;
	}
	return true;
}

struct OxzSpanProg {
	int base;
	int span;
	int usedBase;
	std::wstring err;
};

extern "C" static void OxzSaveProg(const wchar_t* name, unsigned index, unsigned total, int pct, void* user)
{
	OxzSpanProg* p = (OxzSpanProg*)user;
	int overall = p->base;
	if (total) {
		overall += (int)(((unsigned long long)p->span * index) / total);
		if (pct > 0 && pct < 100)
			overall += (int)(((unsigned long long)p->span * (unsigned)pct) / (100ull * total));
	}
	if (overall > p->base + p->span) overall = p->base + p->span;
	if (pct >= 100 && total && index + 1 >= total) overall = p->base + p->span;
	OxzProgFile(name, index + 1, total, pct, overall);
}

extern "C" static void OxzPkgProg(const wchar_t* name, unsigned index, unsigned total, int pct, void* user)
{
	OxzSpanProg* p = (OxzSpanProg*)user;
	int base = p->base;
	int span = p->span;
	if (p->usedBase) {
		int used = p->span * 4 / 5;
		if (used < 1) used = 1;
		if (used >= p->span) used = p->span > 1 ? p->span - 1 : 0;
		base += used;
		span = p->span - used;
		if (span < 1) span = 1;
	}
	int overall = base;
	if (total) overall += (int)(((unsigned long long)span * index) / total);
	if (pct >= 100 && total && index + 1 >= total) overall = p->base + p->span;
	if (overall > p->base + p->span) overall = p->base + p->span;
	OxzProgFile(name, index + 1, total ? total : 1, pct, overall);
}

extern "C" static int OxzPkgBase(const wchar_t* sevenPath, const wchar_t* destDir, void* user)
{
	OxzSpanProg* p = (OxzSpanProg*)user;
	int sub = p->span * 4 / 5;
	std::wstring e;
	int rc;
	p->usedBase = 1;
	if (sub < 1) sub = 1;
	rc = OxzExtractNative(sevenPath, destDir, 1, 1, e, p->base, sub);
	if (rc) p->err = e.empty() ? OxzTxt(OS_OPEN_FAIL) : e;
	return rc == 0 ? 0 : 1;
}

static bool OxzWriteAll(HANDLE h, const unsigned char* buf, size_t n)
{
	size_t off = 0;
	while (off < n) {
		DWORD chunk = (DWORD)((n - off) > (1u << 20) ? (1u << 20) : (n - off));
		DWORD wr = 0;
		if (!WriteFile(h, buf + off, chunk, &wr, NULL) || wr == 0) return false;
		off += wr;
	}
	return true;
}

static void OxzRipFile(const std::wstring& path, int encoding)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	LARGE_INTEGER sz;
	unsigned char mag[2];
	DWORD rd = 0;
	unsigned char* buf;
	if (h == INVALID_HANDLE_VALUE) return;
	if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 2) {
		CloseHandle(h);
		return;
	}
	if (!ReadFile(h, mag, 2, &rd, NULL) || rd != 2 || mag[0] != 'M' || mag[1] != 'Z') {
		CloseHandle(h);
		return;
	}
	buf = (unsigned char*)malloc((size_t)sz.QuadPart);
	if (!buf) { CloseHandle(h); return; }
	SetFilePointer(h, 0, NULL, FILE_BEGIN);
	if (!OxzReadAll(h, buf, (size_t)sz.QuadPart)) {
		free(buf);
		CloseHandle(h);
		return;
	}
	OxzRipConvert(buf, (size_t)sz.QuadPart, encoding);
	SetFilePointer(h, 0, NULL, FILE_BEGIN);
	OxzWriteAll(h, buf, (size_t)sz.QuadPart);
	CloseHandle(h);
	free(buf);
}

static CRITICAL_SECTION gProgCs;
static INIT_ONCE gProgOnce = INIT_ONCE_STATIC_INIT;
static OxzSnap gProg;

static BOOL CALLBACK OxzProgInit(PINIT_ONCE, PVOID, PVOID*)
{
	InitializeCriticalSection(&gProgCs);
	return TRUE;
}

static void OxzProgLock()
{
	InitOnceExecuteOnce(&gProgOnce, OxzProgInit, NULL, NULL);
	EnterCriticalSection(&gProgCs);
}

static void OxzProgUnlock()
{
	LeaveCriticalSection(&gProgCs);
}

static void OxzCopySnap(wchar_t* dst, size_t cap, const wchar_t* src)
{
	if (!src) return;
	wcsncpy_s(dst, cap, src, _TRUNCATE);
}

void OxzProgBegin(const wchar_t* archive)
{
	OxzProgLock();
	memset(&gProg, 0, sizeof(gProg));
	gProg.startTick = GetTickCount();
	if (!gProg.startTick) gProg.startTick = 1;
	OxzCopySnap(gProg.archive, 520, archive);
	OxzProgUnlock();
}

void OxzProgArchive(const wchar_t* archive)
{
	OxzProgLock();
	OxzCopySnap(gProg.archive, 520, archive);
	OxzProgUnlock();
}

void OxzProgFile(const wchar_t* file, unsigned index, unsigned total, int filePct, int overall)
{
	OxzProgLock();
	if (file && file[0]) OxzCopySnap(gProg.file, 1024, file);
	if (index) gProg.index = index;
	if (total) gProg.total = total;
	if (filePct < 0) filePct = 0;
	if (filePct > 100) filePct = 100;
	if (overall < 0) overall = 0;
	if (overall > 100) overall = 100;
	gProg.filePct = filePct;
	gProg.overall = overall;
	OxzProgUnlock();
}

void OxzProgRead(OxzSnap* out)
{
	if (!out) return;
	OxzProgLock();
	*out = gProg;
	OxzProgUnlock();
}

struct OxzScope {
	int base;
	int span;
	unsigned index;
	unsigned total;
	unsigned long long done;
	unsigned long long partial;
	unsigned long long bytes;
	OxzScope* prev;
};

static OxzScope* gScope = NULL;

struct OxzScopeHold {
	OxzScope sc;
	OxzScopeHold(int base, int span, unsigned files, unsigned long long bytes)
	{
		memset(&sc, 0, sizeof(sc));
		sc.base = base;
		sc.span = span < 0 ? 0 : span;
		sc.total = files ? files : 1;
		sc.bytes = bytes;
		sc.prev = gScope;
		gScope = &sc;
	}
	~OxzScopeHold() { gScope = sc.prev; }
};

static int OxzOverallNow()
{
	if (!gScope) return 0;
	int add = 0;
	unsigned long long num = gScope->done + gScope->partial;
	if (gScope->bytes) {
		if (num > gScope->bytes) num = gScope->bytes;
		add = (int)((num * (unsigned long long)gScope->span) / gScope->bytes);
	} else if (gScope->total) {
		unsigned idx = gScope->index;
		if (idx > gScope->total) idx = gScope->total;
		add = (int)((idx * (unsigned)gScope->span) / gScope->total);
	}
	int v = gScope->base + add;
	int cap = gScope->base + gScope->span;
	if (v > cap) v = cap;
	if (v > 100) v = 100;
	if (v < 0) v = 0;
	return v;
}

static void OxzNote(const wchar_t* name, unsigned long long finishedBytes, int filePct)
{
	if (!gScope) return;
	if (filePct >= 100) {
		if (gScope->bytes) gScope->done += finishedBytes;
		gScope->partial = 0;
		gScope->index++;
	}
	unsigned show = gScope->index;
	if (filePct < 100 && show < gScope->total) show++;
	if (!show) show = 1;
	int fp = filePct < 0 ? 0 : (filePct > 100 ? 100 : filePct);
	OxzProgFile(name, show, gScope->total, fp, OxzOverallNow());
}

static void OxzRipTree(const std::wstring& dir, int encoding)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
		std::wstring child = dir + L"\\" + fd.cFileName;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			OxzRipTree(child, encoding);
		else {
			OxzNote(fd.cFileName, 0, 0);
			OxzRipFile(child, encoding);
			OxzNote(fd.cFileName, 0, 100);
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static unsigned long long OxzFindBytes(const WIN32_FIND_DATAW& fd)
{
	ULARGE_INTEGER u;
	u.LowPart = fd.nFileSizeLow;
	u.HighPart = fd.nFileSizeHigh;
	return u.QuadPart;
}

static void OxzCountDir(const std::wstring& dir, unsigned& files, unsigned long long& bytes)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			OxzCountDir(dir + L"\\" + fd.cFileName, files, bytes);
		else {
			files++;
			bytes += OxzFindBytes(fd);
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static void OxzCountPath(const std::wstring& path, unsigned& files, unsigned long long& bytes)
{
	DWORD a = GetFileAttributesW(path.c_str());
	if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT)) return;
	if (a & FILE_ATTRIBUTE_DIRECTORY) {
		OxzCountDir(path, files, bytes);
		return;
	}
	WIN32_FILE_ATTRIBUTE_DATA fa;
	files++;
	if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) {
		ULARGE_INTEGER u;
		u.LowPart = fa.nFileSizeLow;
		u.HighPart = fa.nFileSizeHigh;
		bytes += u.QuadPart;
	}
}

std::wstring OxzTempList()
{
	wchar_t temp[MAX_PATH];
	wchar_t file[MAX_PATH];
	GetTempPathW(MAX_PATH, temp);
	_snwprintf_s(file, _TRUNCATE, L"%soxzdnd_%u_%u.txt", temp, GetCurrentProcessId(), GetTickCount());
	return file;
}

bool OxzWriteList(const std::wstring& file, const std::vector<std::wstring>& paths)
{
	HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD wr = 0;
	wchar_t bom = 0xFEFF;
	if (h == INVALID_HANDLE_VALUE) return false;
	WriteFile(h, &bom, sizeof(bom), &wr, NULL);
	for (size_t i = 0; i < paths.size(); ++i) {
		std::wstring line = paths[i] + L"\r\n";
		if (!OxzWriteAll(h, (const unsigned char*)line.data(), line.size() * sizeof(wchar_t))) {
			CloseHandle(h);
			return false;
		}
	}
	CloseHandle(h);
	return true;
}

bool OxzReadList(const std::wstring& file, std::vector<std::wstring>& out)
{
	HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	LARGE_INTEGER sz;
	unsigned char* buf;
	if (h == INVALID_HANDLE_VALUE) return false;
	if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 2 || sz.QuadPart > 32ll * 1024 * 1024) {
		CloseHandle(h);
		return false;
	}
	buf = (unsigned char*)malloc((size_t)sz.QuadPart);
	if (!buf || !OxzReadAll(h, buf, (size_t)sz.QuadPart)) {
		free(buf);
		CloseHandle(h);
		return false;
	}
	CloseHandle(h);
	const wchar_t* p = (const wchar_t*)buf;
	size_t n = (size_t)sz.QuadPart / sizeof(wchar_t);
	if (n && p[0] == 0xFEFF) { p++; n--; }
	std::wstring cur;
	for (size_t i = 0; i < n; ++i) {
		if (p[i] == L'\r') continue;
		if (p[i] == L'\n') {
			if (!cur.empty()) out.push_back(cur);
			cur.clear();
		} else {
			cur.push_back(p[i]);
		}
	}
	if (!cur.empty()) out.push_back(cur);
	free(buf);
	return true;
}

std::wstring OxzNewWork()
{
	wchar_t temp[MAX_PATH];
	wchar_t dir[MAX_PATH];
	static unsigned seq = 1;
	GetTempPathW(MAX_PATH, temp);
	_snwprintf_s(dir, _TRUNCATE, L"%soxzgui_%u_%u", temp, GetTickCount(), seq++);
	CreateDirectoryW(dir, NULL);
	return dir;
}

void OxzRmTree(const std::wstring& dir)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) {
		RemoveDirectoryW(dir.c_str());
		return;
	}
	do {
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		std::wstring child = dir + L"\\" + fd.cFileName;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			OxzRmTree(child);
		else
			DeleteFileW(child.c_str());
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	RemoveDirectoryW(dir.c_str());
}

static int OxzCopyFileP(const std::wstring& from, const std::wstring& to, const wchar_t* name)
{
	WIN32_FILE_ATTRIBUTE_DATA fa;
	unsigned long long total = 0;
	if (GetFileAttributesExW(from.c_str(), GetFileExInfoStandard, &fa)) {
		ULARGE_INTEGER u;
		u.LowPart = fa.nFileSizeLow;
		u.HighPart = fa.nFileSizeHigh;
		total = u.QuadPart;
	}
	if (!gScope || total < 1024ull * 1024ull) {
		if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) return 0;
		OxzNote(name, total, 100);
		return 1;
	}
	HANDLE in = CreateFileW(from.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (in == INVALID_HANDLE_VALUE) return 0;
	HANDLE out = CreateFileW(to.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (out == INVALID_HANDLE_VALUE) {
		CloseHandle(in);
		return 0;
	}
	std::vector<char> buf(1024 * 1024);
	unsigned long long got = 0;
	OxzNote(name, 0, 0);
	for (;;) {
		DWORD n = 0, w = 0;
		if (!ReadFile(in, &buf[0], (DWORD)buf.size(), &n, NULL)) {
			CloseHandle(in);
			CloseHandle(out);
			return 0;
		}
		if (!n) break;
		if (!WriteFile(out, &buf[0], n, &w, NULL) || w != n) {
			CloseHandle(in);
			CloseHandle(out);
			return 0;
		}
		got += n;
		gScope->partial = got;
		int fp = total ? (int)(got * 100ull / total) : 100;
		OxzNote(name, 0, fp);
	}
	CloseHandle(in);
	CloseHandle(out);
	gScope->partial = 0;
	OxzNote(name, total ? total : got, 100);
	return 1;
}

void OxzCopyTree(const std::wstring& src, const std::wstring& dest)
{
	CreateDirectoryW(dest.c_str(), NULL);
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
		std::wstring from = src + L"\\" + fd.cFileName;
		std::wstring to = dest + L"\\" + fd.cFileName;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			OxzCopyTree(from, to);
		else
			OxzCopyFileP(from, to, fd.cFileName);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

void OxzImportOne(const std::wstring& src, const std::wstring& destDir)
{
	DWORD srcAttr = GetFileAttributesW(src.c_str());
	if (srcAttr == INVALID_FILE_ATTRIBUTES) return;
	if (srcAttr & FILE_ATTRIBUTE_REPARSE_POINT) return;
	std::wstring to = destDir + L"\\" + OxzBaseName(src);
	DWORD destAttr = GetFileAttributesW(to.c_str());
	if (destAttr != INVALID_FILE_ATTRIBUTES && ((destAttr ^ srcAttr) & FILE_ATTRIBUTE_DIRECTORY)) {
		if (destAttr & FILE_ATTRIBUTE_DIRECTORY) OxzRmTree(to);
		else DeleteFileW(to.c_str());
	}
	CreateDirectoryW(destDir.c_str(), NULL);
	if (srcAttr & FILE_ATTRIBUTE_DIRECTORY)
		OxzCopyTree(src, to);
	else
		OxzCopyFileP(src, to, OxzBaseName(src).c_str());
}

bool OxzSamePath(const std::wstring& a, const std::wstring& b)
{
	return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

static int OxzIsInside(const std::wstring& dir, const std::wstring& path)
{
	size_t n = dir.size();
	if (n == 0 || path.size() <= n) return 0;
	if (_wcsnicmp(path.c_str(), dir.c_str(), n) != 0) return 0;
	if (dir.back() == L'\\' || dir.back() == L'/') return 1;
	return path[n] == L'\\' || path[n] == L'/';
}

std::wstring OxzDestFor(const std::vector<std::wstring>& paths)
{
	if (paths.empty()) return L"";
	if (paths.size() == 1) {
		std::wstring p = paths[0];
		DWORD a = GetFileAttributesW(p.c_str());
		std::wstring parent = OxzParent(p);
		std::wstring name = OxzBaseName(p);
		if (!(a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))) {
			size_t dot = name.find_last_of(L'.');
			if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
		}
		if (name.empty()) name = L"oxz";
		return parent + (parent.size() && parent.back() != L'\\' ? L"\\" : L"") + name + L".oxz";
	}
	std::wstring parent = OxzParent(paths[0]);
	std::wstring leaf = OxzBaseName(parent);
	if (leaf.empty() || leaf.back() == L':') leaf = L"oxz";
	while (!leaf.empty() && (leaf.back() == L'\\' || leaf.back() == L'/')) leaf.pop_back();
	if (leaf.empty()) leaf = L"oxz";
	return parent + (parent.size() && parent.back() != L'\\' ? L"\\" : L"") + leaf + L".oxz";
}

static int OxzPctOf(const std::string& s)
{
	size_t i = 0;
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
	if (i >= s.size() || s[i] < '0' || s[i] > '9') return -1;
	int v = 0;
	size_t start = i;
	while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
		v = v * 10 + (s[i] - '0');
		i++;
		if (i - start > 3) return -1;
	}
	if (i >= s.size() || s[i] != '%') return -1;
	if (v > 100) v = 100;
	return v;
}

static std::wstring OxzFromUtf8(const std::string& s)
{
	if (s.empty()) return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), NULL, 0);
	UINT cp = CP_UTF8;
	if (n <= 0) {
		cp = CP_ACP;
		n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), NULL, 0);
	}
	if (n <= 0) return std::wstring();
	std::wstring w((size_t)n, 0);
	MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, s.data(), (int)s.size(), &w[0], n);
	return w;
}

static int OxzMapped(int base, int span, int pct)
{
	if (span < 0) span = 0;
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	int v = base + (span * pct) / 100;
	if (v > base + span) v = base + span;
	if (v > 100) v = 100;
	if (v < 0) v = 0;
	return v;
}

static void OxzFeed7za(std::string& cur, int& pct, int base, int span, char c)
{
	if (c != '\r' && c != '\n') {
		if (c == '\b') {
			if (!cur.empty()) cur.pop_back();
		} else {
			cur.push_back(c);
		}
		return;
	}
	std::string s = cur;
	cur.clear();
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
	size_t b = 0;
	while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) b++;
	if (b) s.erase(0, b);
	if (s.empty()) return;
	int p = OxzPctOf(s);
	if (p >= 0) {
		pct = p;
		OxzProgFile(NULL, 0, 0, p, OxzMapped(base, span, p));
		return;
	}
	if (s.size() >= 2 && (s[0] == '+' || s[0] == '-') && s[1] == ' ') {
		std::string name = s.substr(2);
		while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
		std::wstring w = OxzFromUtf8(name);
		OxzProgFile(w.c_str(), 0, 0, pct < 0 ? 0 : pct, OxzMapped(base, span, pct < 0 ? 0 : pct));
	}
}

static int OxzRun7za(const std::wstring& args, const wchar_t* cwd, int base, int span)
{
	SECURITY_ATTRIBUTES sa;
	HANDLE rd = NULL, wr = NULL, inR = NULL, inW = NULL;
	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	if (!CreatePipe(&rd, &wr, &sa, 0)) return 2;
	if (!CreatePipe(&inR, &inW, &sa, 0)) {
		CloseHandle(rd);
		CloseHandle(wr);
		return 2;
	}
	SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
	std::wstring cmd = OxzQuote(OxzBeside(L"7za.exe")) + L" " + args;
	std::vector<wchar_t> buf(cmd.begin(), cmd.end());
	buf.push_back(0);
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = inR;
	si.hStdOutput = wr;
	si.hStdError = wr;
	if (!CreateProcessW(NULL, &buf[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi)) {
		CloseHandle(rd);
		CloseHandle(wr);
		CloseHandle(inR);
		CloseHandle(inW);
		return 2;
	}
	CloseHandle(wr);
	CloseHandle(inR);
	CloseHandle(inW);
	std::string cur;
	int pct = -1;
	char tmp[4096];
	for (;;) {
		DWORD n = 0;
		if (!ReadFile(rd, tmp, sizeof(tmp), &n, NULL) || n == 0) break;
		for (DWORD i = 0; i < n; ++i) OxzFeed7za(cur, pct, base, span, tmp[i]);
	}
	if (!cur.empty()) OxzFeed7za(cur, pct, base, span, '\n');
	CloseHandle(rd);
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	if (code == 0) OxzProgFile(NULL, 0, 0, 100, OxzMapped(base, span, 100));
	return code == 0 ? 0 : (int)code;
}

static int OxzExtract7za(const wchar_t* arc, const wchar_t* dest, std::wstring& err, int base, int span, int rip)
{
	if (!OxzHas7za()) { err = OxzTxt(OS_NO7ZA); return 2; }
	if (span < 1) span = 1;
	int zSpan = rip ? span * 85 / 100 : span;
	if (zSpan < 1) zSpan = span;
	int ripSpan = span - zSpan;
	std::wstring args = L"x -y -sccUTF-8 -bb1 -bsp1 -bso1 " + OxzQuote(std::wstring(L"-o") + dest) + L" " + OxzQuote(arc);
	int rc = OxzRun7za(args, NULL, base, zSpan);
	if (rc != 0) { err = OxzTxt(OS_OPEN_FAIL); return rc; }
	if (rip) {
		unsigned files = 0;
		unsigned long long nbytes = 0;
		OxzCountDir(dest, files, nbytes);
		OxzScopeHold hold(base + zSpan, ripSpan, files, 0);
		OxzRipTree(dest, 0);
	}
	OxzProgFile(NULL, 0, 0, 100, OxzMapped(base, span, 100));
	return 0;
}

int OxzExtractTo(const wchar_t* arc, const wchar_t* dest, std::wstring& err, int base, int span)
{
	if (span < 1) span = 1;
	OxzProgArchive(arc);
	CreateDirectoryW(dest, NULL);
	if (OxzCoreIsPkg(arc)) {
		OxzSpanProg prog;
		int rc;
		prog.base = base;
		prog.span = span;
		prog.usedBase = 0;
		rc = OxzCoreExtract(arc, dest, OxzPkgBase, &prog, OxzPkgProg, &prog);
		if (rc != OXZ_CORE_OK) {
			err = prog.err.empty() ? OxzTxt(OS_OPEN_FAIL) : prog.err;
			return 2;
		}
		return 0;
	}
	int kind = OxzArchiveKind(arc);
	int rip = OxzIsOxz(arc) ? 1 : 0;
	if (kind >= 1 && kind <= 4) {
		std::wstring nativeErr;
		int rc = OxzExtractNative(arc, dest, kind, rip, nativeErr, base, span);
		if (rc == 0) return 0;
		if (OxzHas7za()) return OxzExtract7za(arc, dest, err, base, span, rip);
		err = nativeErr.empty() ? OxzTxt(OS_OPEN_FAIL) : nativeErr;
		return rc;
	}
	return OxzExtract7za(arc, dest, err, base, span, rip);
}

int OxzExtractPlain(const wchar_t* arc, const wchar_t* dest, std::wstring& err, int base, int span)
{
	if (!OxzHas7za()) { err = OxzTxt(OS_NO7ZA); return 2; }
	if (span < 1) span = 1;
	OxzProgArchive(arc);
	CreateDirectoryW(dest, NULL);
	std::wstring args = L"x -y -sccUTF-8 -bb1 -bsp1 -bso1 " + OxzQuote(std::wstring(L"-o") + dest) + L" " + OxzQuote(arc);
	int rc = OxzRun7za(args, NULL, base, span);
	if (rc != 0) { err = OxzTxt(OS_OPEN_FAIL); return rc; }
	return 0;
}

int OxzTest(const wchar_t* arc, std::wstring& err, int base, int span)
{
	std::wstring tmp = OxzNewWork();
	int rc = OxzExtractTo(arc, tmp.c_str(), err, base, span);
	OxzRmTree(tmp);
	if (rc != 0 && err.empty()) err = OxzTxt(OS_TEST_FAIL);
	return rc;
}

int OxzPackWork(const std::wstring& work, const std::wstring& archive, unsigned long long& bytes, std::wstring& err, int base, int span)
{
	OxzSpanProg prog;
	unsigned long long n = 0;
	int rc;
	bytes = 0;
	if (span < 1) span = 1;
	OxzProgArchive(archive.c_str());
	prog.base = base;
	prog.span = span;
	prog.usedBase = 0;
	rc = OxzCoreSave(work.c_str(), archive.c_str(), &n, OxzSaveProg, &prog);
	bytes = n;
	if (rc == OXZ_CORE_EMPTY) { err = OxzTxt(OS_EMPTY); return 2; }
	if (rc != OXZ_CORE_OK) { err = OxzTxt(OS_SAVE_FAIL); return 2; }
	return 0;
}

static int OxzTakeInput(const std::wstring& path, const std::wstring& dest)
{
	DWORD attr = GetFileAttributesW(path.c_str());
	if (OxzSamePath(path, dest)) return 0;
	if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
	if ((attr & FILE_ATTRIBUTE_DIRECTORY) && OxzIsInside(path, dest)) return 0;
	return 1;
}

int OxzMergePack(const std::wstring& existing, const std::vector<std::wstring>& inputs, const std::wstring& dest, unsigned long long& bytes, std::wstring& err)
{
	std::wstring work = OxzNewWork();
	int rc = 2;
	int added = 0;
	int has = !existing.empty() && GetFileAttributesW(existing.c_str()) != INVALID_FILE_ATTRIBUTES;
	int extSpan = has ? 35 : 0;
	int impSpan = 15;
	int packBase = extSpan + impSpan;
	int packSpan = 100 - packBase;
	OxzProgArchive(dest.c_str());
	if (has) {
		rc = OxzExtractTo(existing.c_str(), work.c_str(), err, 0, extSpan);
		if (rc != 0) {
			OxzRmTree(work);
			return rc;
		}
		OxzCoreRemember(work.c_str(), existing.c_str());
	}
	unsigned files = 0;
	unsigned long long nbytes = 0;
	for (size_t i = 0; i < inputs.size(); ++i) {
		if (!OxzTakeInput(inputs[i], dest)) continue;
		OxzCountPath(inputs[i], files, nbytes);
	}
	{
		OxzScopeHold hold(extSpan, impSpan, files, nbytes);
		for (size_t i = 0; i < inputs.size(); ++i) {
			if (!OxzTakeInput(inputs[i], dest)) continue;
			OxzImportOne(inputs[i], work);
			added++;
		}
	}
	if (added <= 0) {
		err = OxzTxt(OS_NOTHING);
		OxzRmTree(work);
		return 2;
	}
	rc = OxzPackWork(work, dest, bytes, err, packBase, packSpan);
	OxzRmTree(work);
	return rc;
}

int OxzSaveAdded(const std::wstring& work, const std::wstring& destDir, const std::vector<std::wstring>& inputs, const std::wstring& archive, unsigned long long& bytes, std::wstring& err)
{
	std::wstring into = destDir.empty() ? work : destDir;
	std::vector<std::wstring> use;
	unsigned files = 0;
	unsigned long long nbytes = 0;
	OxzProgBegin(archive.c_str());
	for (size_t i = 0; i < inputs.size(); ++i) {
		if (OxzSamePath(inputs[i], archive) || OxzSamePath(inputs[i], work)) continue;
		if (OxzIsInside(work, inputs[i])) continue;
		DWORD attr = GetFileAttributesW(inputs[i].c_str());
		if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
		use.push_back(inputs[i]);
		OxzCountPath(inputs[i], files, nbytes);
	}
	if (use.empty()) {
		err = OxzTxt(OS_NOTHING);
		return 2;
	}
	{
		OxzScopeHold hold(0, 15, files, nbytes);
		for (size_t i = 0; i < use.size(); ++i) OxzImportOne(use[i], into);
	}
	return OxzPackWork(work, archive, bytes, err, 15, 85);
}
