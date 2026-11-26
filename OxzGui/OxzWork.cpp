#include "OxzWork.h"
#include "OxzIn.h"
#include "OxzArc.h"
#include "OxzLang.h"
#include "OxzFilt.h"
#include "../OxzCore/OxzCore.h"
#include "OxzVol.h"
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
	unsigned magic;
	int base;
	int span;
	int usedBase;
	unsigned long long weightNumer;
	unsigned long long weightDenom;
	int fileTen;
	std::wstring err;
	OxzSpanProg() : magic(0x50585A4Fu), base(0), span(0), usedBase(0), weightNumer(0), weightDenom(0), fileTen(0) {}
};

extern "C" static void OxzSaveProg(const wchar_t* name, unsigned index, unsigned total, int pct, void* user)
{
	OxzSpanProg* p = (OxzSpanProg*)user;
	int cap = (p->base + p->span) * 10;
	int overallTen = p->base * 10;
	unsigned shown = index;
	int fileTen = pct >= 100 ? 1000 : pct * 10;
	if (pct < 100 && p->fileTen > fileTen && p->fileTen < 1000) fileTen = p->fileTen;
	p->fileTen = 0;
	if (fileTen < 0) fileTen = 0;
	if (fileTen > 1000) fileTen = 1000;
	int holdPlace = 0;
	if (p->magic == 0x50585A4Fu && p->weightDenom) {
		unsigned long long n = p->weightNumer;
		if (n > p->weightDenom) n = p->weightDenom;
		overallTen += (int)(((unsigned long long)p->span * 10ull * n) / p->weightDenom);
		if (!index) holdPlace = 1;
		else if (shown < 1) shown = 1;
	} else if (total) {
		unsigned long long done = index;
		if (pct >= 100 && done < total) done++;
		overallTen += (int)(((unsigned long long)p->span * 10ull * done) / total);
		shown = (unsigned)done;
		if (pct < 100 && shown < total) shown++;
		if (!shown) shown = 1;
	}
	if (overallTen > cap) overallTen = cap;
	if (pct >= 100 && p->weightDenom && p->weightNumer >= p->weightDenom) overallTen = cap;
	if (overallTen > 1000) overallTen = 1000;
	if (holdPlace) OxzProgDetail(name, 0, 0, fileTen, overallTen);
	else OxzProgDetail(name, shown, total, fileTen, overallTen);
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
	int cap = (p->base + p->span) * 10;
	int fileTen = pct >= 100 ? 1000 : pct * 10;
	unsigned shown;
	int overallTen = base * 10;
	if (p->fileTen > fileTen && pct < 100) fileTen = p->fileTen;
	p->fileTen = 0;
	if (fileTen < 0) fileTen = 0;
	if (fileTen > 1000) fileTen = 1000;
	if (pct >= 100) {
		if (total) overallTen += (int)(((unsigned long long)span * 10ull * index) / total);
		shown = index ? index : 1;
	} else if (total) {
		unsigned long long num = (unsigned long long)index * 1000ull + (unsigned)fileTen;
		overallTen += (int)(((unsigned long long)span * 10ull * num) / ((unsigned long long)total * 1000ull));
		shown = index + 1;
	} else {
		shown = 1;
	}
	if (total && shown > total) shown = total;
	if (!shown) shown = 1;
	if (pct >= 100 && total && index >= total) overallTen = cap;
	if (overallTen > cap) overallTen = cap;
	if (overallTen > 1000) overallTen = 1000;
	OxzProgDetail(name, shown, total ? total : 1, fileTen, overallTen);
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
	OxzPulseReset();
}

void OxzProgArchive(const wchar_t* archive)
{
	OxzProgLock();
	OxzCopySnap(gProg.archive, 520, archive);
	OxzProgUnlock();
}

void OxzProgDetail(const wchar_t* file, unsigned index, unsigned total, int fileTen, int overallTen)
{
	OxzProgLock();
	if (fileTen < 0) fileTen = 0;
	if (fileTen > 1000) fileTen = 1000;
	if (overallTen < 0) overallTen = 0;
	if (overallTen > 1000) overallTen = 1000;
	if (total && index > total) index = total;
	if (overallTen < gProg.overallTen) overallTen = gProg.overallTen;
	if (file && file[0]) OxzCopySnap(gProg.file, 1024, file);
	if (index > gProg.index) gProg.index = index;
	if (total) gProg.total = total;
	gProg.fileTen = fileTen;
	gProg.overallTen = overallTen;
	gProg.filePct = fileTen / 10;
	gProg.overall = overallTen / 10;
	OxzProgUnlock();
}

void OxzProgFile(const wchar_t* file, unsigned index, unsigned total, int filePct, int overall)
{
	if (filePct < 0) filePct = 0;
	if (filePct > 100) filePct = 100;
	if (overall < 0) overall = 0;
	if (overall > 100) overall = 100;
	OxzProgDetail(file, index, total, filePct * 10, overall * 10);
}

void OxzProgRead(OxzSnap* out)
{
	if (!out) return;
	OxzProgLock();
	*out = gProg;
	OxzProgUnlock();
}

void OxzFormatGrouped(unsigned long long n, wchar_t* out, size_t cch)
{
	wchar_t tmp[32];
	wchar_t buf[48];
	int i, len, dst;
	if (!out || !cch) return;
	_snwprintf_s(tmp, _TRUNCATE, L"%llu", n);
	len = (int)wcslen(tmp);
	dst = 0;
	for (i = 0; i < len && dst + 2 < (int)sizeof(buf) / (int)sizeof(buf[0]); ++i) {
		if (i > 0 && (len - i) % 3 == 0) buf[dst++] = L',';
		buf[dst++] = tmp[i];
	}
	buf[dst] = 0;
	wcsncpy_s(out, cch, buf, _TRUNCATE);
}

void OxzFormatRatio(unsigned long long packed, unsigned long long size, wchar_t* out, size_t cch)
{
	unsigned long long t;
	if (!out || !cch) return;
	if (!size) { out[0] = 0; return; }
	t = (packed * 1000ull + size / 2ull) / size;
	_snwprintf_s(out, cch, _TRUNCATE, L"%llu.%llu%%", t / 10ull, t % 10ull);
}

void OxzFormatTenth(int ten, wchar_t* out, size_t cch)
{
	if (!out || !cch) return;
	if (ten < 0) ten = 0;
	if (ten > 1000) ten = 1000;
	_snwprintf_s(out, cch, _TRUNCATE, L"%d.%d%%", ten / 10, ten % 10);
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

static int OxzOverallTen()
{
	if (!gScope) return 0;
	int add = 0;
	unsigned long long num = gScope->done + gScope->partial;
	if (gScope->bytes) {
		if (num > gScope->bytes) num = gScope->bytes;
		add = (int)((num * (unsigned long long)gScope->span * 10ull) / gScope->bytes);
	} else if (gScope->total) {
		unsigned idx = gScope->index;
		if (idx > gScope->total) idx = gScope->total;
		add = (int)((idx * (unsigned)gScope->span * 10u) / gScope->total);
	}
	int v = gScope->base * 10 + add;
	int cap = (gScope->base + gScope->span) * 10;
	if (v > cap) v = cap;
	if (v > 1000) v = 1000;
	if (v < 0) v = 0;
	return v;
}

static void OxzNote(const wchar_t* name, unsigned long long finishedBytes, int fileTen)
{
	if (!gScope) return;
	if (fileTen >= 1000) {
		if (gScope->bytes) gScope->done += finishedBytes;
		gScope->partial = 0;
		gScope->index++;
		fileTen = 1000;
	}
	unsigned show = gScope->index;
	if (fileTen < 1000 && show < gScope->total) show++;
	if (!show) show = 1;
	if (fileTen < 0) fileTen = 0;
	if (fileTen > 1000) fileTen = 1000;
	OxzProgDetail(name, show, gScope->total, fileTen, OxzOverallTen());
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
			OxzNote(fd.cFileName, 0, 1000);
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
		OxzNote(name, total, 1000);
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
		int fileTen = total ? (int)(got * 1000ull / total) : 1000;
		if (fileTen > 999) fileTen = 999;
		OxzNote(name, 0, fileTen);
	}
	CloseHandle(in);
	CloseHandle(out);
	gScope->partial = 0;
	OxzNote(name, total ? total : got, 1000);
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

int OxzExtractTo(const wchar_t* arc, const wchar_t* dest, std::wstring& err, int base, int span)
{
	if (span < 1) span = 1;
	OxzProgArchive(arc);
	CreateDirectoryW(dest, NULL);
	{
		std::wstring use;
		int temp = 0;
		if (OxzMaterializeOxz(arc, use, &temp)) {
			OxzSpanProg prog;
			int rc;
			prog.base = base;
			prog.span = span;
			prog.usedBase = 0;
			rc = OxzCoreExtract(use.c_str(), dest, OxzPkgBase, &prog, OxzPkgProg, &prog);
			OxzReleaseOxz(use, temp);
			if (rc != OXZ_CORE_OK) {
				err = prog.err.empty() ? OxzTxt(OS_OPEN_FAIL) : prog.err;
				return 2;
			}
			return 0;
		}
	}
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
	int rc = OxzExtractInProc(arc, dest, err, base, span);
	if (rc == 3) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	return rc;
}

int OxzExtractPlain(const wchar_t* arc, const wchar_t* dest, std::wstring& err, int base, int span)
{
	if (span < 1) span = 1;
	OxzProgArchive(arc);
	CreateDirectoryW(dest, NULL);
	if (OxzIsOxzFamily(arc)) return OxzExtractTo(arc, dest, err, base, span);
	return OxzExtractInProc(arc, dest, err, base, span);
}

int OxzTest(const wchar_t* arc, std::wstring& err, int base, int span)
{
	std::vector<OxzTestRow> rows;
	int rc = OxzTestArchive(arc, rows, err, base, span);
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
	if (OxzIsOxz(archive) && OxzOptSfx()) {
		std::wstring stub = OxzBeside(L"OxzSfx.exe");
		if (GetFileAttributesW(stub.c_str()) == INVALID_FILE_ATTRIBUTES) { err = OxzTxt(OS_SFX_MISSING); return 2; }
	}
	prog.base = base;
	prog.span = span;
	prog.usedBase = 0;
	rc = OxzCoreSave(work.c_str(), archive.c_str(), &n, OxzSaveProg, &prog);
	bytes = n;
	if (rc == OXZ_CORE_EMPTY) { err = OxzTxt(OS_EMPTY); return 2; }
	if (rc != OXZ_CORE_OK) { err = OxzTxt(OS_SAVE_FAIL); return 2; }
	if (OxzIsOxz(archive) && !OxzSealOutput(archive, &bytes, err)) return 2;
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
	{
		int wrap = OxzTarWrap(dest.c_str());
		if (wrap >= 0) rc = OxzWriteTar(work, dest, bytes, err, packBase, packSpan);
		else if (OxzEndsI(dest.c_str(), L".zip")) rc = OxzWriteZip(work, dest, bytes, err, packBase, packSpan);
		else rc = OxzPackWork(work, dest, bytes, err, packBase, packSpan);
	}
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
	{
		int wrap = OxzTarWrap(archive.c_str());
		if (wrap >= 0) return OxzWriteTar(work, archive, bytes, err, 15, 85);
		if (OxzEndsI(archive.c_str(), L".zip")) return OxzWriteZip(work, archive, bytes, err, 15, 85);
	}
	return OxzPackWork(work, archive, bytes, err, 15, 85);
}
