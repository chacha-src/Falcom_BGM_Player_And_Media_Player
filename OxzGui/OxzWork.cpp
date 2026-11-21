#include "OxzWork.h"
#include "OxzLang.h"
#include "OxzFilt.h"
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
	if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 2 || sz.QuadPart > 2ll * 1024 * 1024 * 1024) {
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
		else
			OxzRipFile(child, encoding);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
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
			CopyFileW(from.c_str(), to.c_str(), FALSE);
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
		CopyFileW(src.c_str(), to.c_str(), FALSE);
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

static int OxzRun7za(const std::wstring& args, const wchar_t* cwd)
{
	std::wstring cmd = OxzQuote(OxzBeside(L"7za.exe")) + L" " + args;
	std::vector<wchar_t> buf(cmd.begin(), cmd.end());
	buf.push_back(0);
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	memset(&pi, 0, sizeof(pi));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	if (!CreateProcessW(NULL, &buf[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi))
		return 2;
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return code == 0 ? 0 : (int)code;
}

int OxzExtractTo(const wchar_t* arc, const wchar_t* dest, std::wstring& err)
{
	if (!OxzHas7za()) { err = OxzTxt(OS_NO7ZA); return 2; }
	CreateDirectoryW(dest, NULL);
	std::wstring args = L"x -y -bso0 -bsp0 " + OxzQuote(std::wstring(L"-o") + dest) + L" " + OxzQuote(arc);
	int rc = OxzRun7za(args, NULL);
	if (rc != 0) { err = OxzTxt(OS_OPEN_FAIL); return rc; }
	OxzRipTree(dest, 0);
	return 0;
}

int OxzTest(const wchar_t* arc, std::wstring& err)
{
	std::wstring tmp = OxzNewWork();
	int rc = OxzExtractTo(arc, tmp.c_str(), err);
	OxzRmTree(tmp);
	if (rc != 0 && err.empty()) err = OxzTxt(OS_TEST_FAIL);
	return rc;
}

int OxzPackWork(const std::wstring& work, const std::wstring& archive, unsigned long long& bytes, std::wstring& err)
{
	bytes = 0;
	if (!OxzHas7za()) { err = OxzTxt(OS_NO7ZA); return 2; }
	std::wstring stage = OxzNewWork();
	std::wstring tmp = archive + L".building";
	std::wstring args = L"a -bso0 -bsp0 -mx9 -md=64m " + OxzQuote(tmp);
	int files = 0;
	WIN32_FIND_DATAW fd;
	HANDLE h;
	DeleteFileW(tmp.c_str());
	OxzCopyTree(work, stage);
	OxzRipTree(stage, 1);
	h = FindFirstFileW((stage + L"\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
				continue;
			args += L" ";
			args += OxzQuote(fd.cFileName);
			files++;
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
	int rc = 2;
	if (files <= 0) err = OxzTxt(OS_EMPTY);
	else rc = OxzRun7za(args, stage.c_str());
	OxzRmTree(stage);
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (err.empty() && rc <= 1 && GetFileAttributesExW(tmp.c_str(), GetFileExInfoStandard, &fad)) {
		ULARGE_INTEGER u;
		u.LowPart = fad.nFileSizeLow;
		u.HighPart = fad.nFileSizeHigh;
		bytes = u.QuadPart;
		if (!MoveFileExW(tmp.c_str(), archive.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			DeleteFileW(tmp.c_str());
			err = OxzTxt(OS_MOVE_FAIL);
			return 2;
		}
		return 0;
	}
	DeleteFileW(tmp.c_str());
	if (err.empty()) err = OxzTxt(OS_SAVE_FAIL);
	return 2;
}

int OxzMergePack(const std::wstring& existing, const std::vector<std::wstring>& inputs, const std::wstring& dest, unsigned long long& bytes, std::wstring& err)
{
	std::wstring work = OxzNewWork();
	int rc = 2;
	int added = 0;
	if (!existing.empty() && GetFileAttributesW(existing.c_str()) != INVALID_FILE_ATTRIBUTES) {
		rc = OxzExtractTo(existing.c_str(), work.c_str(), err);
		if (rc != 0) {
			OxzRmTree(work);
			return rc;
		}
	}
	for (size_t i = 0; i < inputs.size(); ++i) {
		DWORD attr = GetFileAttributesW(inputs[i].c_str());
		if (OxzSamePath(inputs[i], dest)) continue;
		if (attr == INVALID_FILE_ATTRIBUTES) continue;
		if ((attr & FILE_ATTRIBUTE_DIRECTORY) && OxzIsInside(inputs[i], dest)) continue;
		OxzImportOne(inputs[i], work);
		added++;
	}
	if (added <= 0) {
		err = OxzTxt(OS_NOTHING);
		OxzRmTree(work);
		return 2;
	}
	rc = OxzPackWork(work, dest, bytes, err);
	OxzRmTree(work);
	return rc;
}
