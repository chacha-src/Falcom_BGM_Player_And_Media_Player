#include "OxzVol.h"
#include "OxzWork.h"
#include "OxzLang.h"
#include "../OxzCore/OxzCore.h"
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <vector>

static const wchar_t* kKey = L"Software\\OxzGui";
static int gAsk = 0;
static int gSfx = 0;
static int gThreads = 0;
static unsigned long long gSplit = 0;
static std::wstring gSplitText;
static const wchar_t* kPreset[] = { L"10 MB", L"50 MB", L"100 MB", L"700 MB", L"1 GB", L"2 GB", L"4.7 GB" };

static void Put32(uint8_t* p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void Put64(uint8_t* p, uint64_t v)
{
	for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t Get32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t Get64(const uint8_t* p)
{
	uint64_t v = 0;
	for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
	return v;
}

struct VolHead {
	uint32_t index, digits, flags, parts;
	uint64_t payload, total, slice;
	uint64_t payOff;
};

unsigned long long OxzParseBytes(const wchar_t* text)
{
	const wchar_t* s;
	wchar_t* end = NULL;
	double v;
	unsigned long long mul = 1;
	if (!text) return 0;
	s = text;
	while (*s == L' ' || *s == L'\t') s++;
	if (!*s) return 0;
	if (!_wcsicmp(s, L"なし") || !_wcsicmp(s, L"none") || !_wcsicmp(s, L"0")) return 0;
	if (OxzTxt(OS_SPLIT_NONE)[0] && !_wcsicmp(s, OxzTxt(OS_SPLIT_NONE))) return 0;
	v = wcstod(s, &end);
	if (end == s || v <= 0) return 0;
	while (*end == L' ') end++;
	if (!_wcsnicmp(end, L"GB", 2) || !_wcsnicmp(end, L"G", 1)) mul = 1024ull * 1024ull * 1024ull;
	else if (!_wcsnicmp(end, L"MB", 2) || !_wcsnicmp(end, L"M", 1)) mul = 1024ull * 1024ull;
	else if (!_wcsnicmp(end, L"KB", 2) || !_wcsnicmp(end, L"K", 1)) mul = 1024ull;
	else if (*end) return 0;
	if (v > 1.0e15) return 0;
	return (unsigned long long)(v * (double)mul);
}

static void RememberSplit(const wchar_t* text)
{
	unsigned long long n;
	gSplitText = text ? text : L"";
	n = OxzParseBytes(gSplitText.c_str());
	if (n && n < 64ull * 1024ull) n = 64ull * 1024ull;
	gSplit = n;
	RegSetKeyValueW(HKEY_CURRENT_USER, kKey, L"split", REG_SZ, gSplitText.c_str(), (DWORD)((gSplitText.size() + 1) * sizeof(wchar_t)));
}

void OxzOptApply()
{
	DWORD ask = 0, sfx = 0, threads = 0, cb = sizeof(DWORD);
	wchar_t text[64];
	DWORD n = sizeof(text);
	if (RegGetValueW(HKEY_CURRENT_USER, kKey, L"ask", RRF_RT_REG_DWORD, NULL, &ask, &cb) != ERROR_SUCCESS) ask = 0;
	cb = sizeof(DWORD);
	if (RegGetValueW(HKEY_CURRENT_USER, kKey, L"sfx", RRF_RT_REG_DWORD, NULL, &sfx, &cb) != ERROR_SUCCESS) sfx = 0;
	cb = sizeof(DWORD);
	if (RegGetValueW(HKEY_CURRENT_USER, kKey, L"threads", RRF_RT_REG_DWORD, NULL, &threads, &cb) != ERROR_SUCCESS) threads = 0;
	text[0] = 0;
	if (RegGetValueW(HKEY_CURRENT_USER, kKey, L"split", RRF_RT_REG_SZ, NULL, text, &n) != ERROR_SUCCESS) text[0] = 0;
	gAsk = ask ? 1 : 0;
	gSfx = sfx ? 1 : 0;
	if ((int)threads < 0) threads = 0;
	if (threads > 12) threads = 12;
	gThreads = (int)threads;
	gSplitText = text;
	gSplit = OxzParseBytes(text);
	if (gSplit && gSplit < 64ull * 1024ull) gSplit = 64ull * 1024ull;
	OxzCoreSetThreads(gThreads);
}

int OxzOptAsk() { return gAsk; }
int OxzOptSfx() { return gSfx; }
int OxzOptThreads() { return gThreads; }
unsigned long long OxzOptSplit() { return gSplit; }
const wchar_t* OxzOptSplitText() { return gSplitText.c_str(); }

void OxzOptSetAsk(int on)
{
	DWORD v = on ? 1 : 0;
	gAsk = (int)v;
	RegSetKeyValueW(HKEY_CURRENT_USER, kKey, L"ask", REG_DWORD, &v, sizeof(v));
}

void OxzOptSetSfx(int on)
{
	DWORD v = on ? 1 : 0;
	gSfx = (int)v;
	RegSetKeyValueW(HKEY_CURRENT_USER, kKey, L"sfx", REG_DWORD, &v, sizeof(v));
}

void OxzOptSetThreads(int n)
{
	DWORD v;
	if (n < 0) n = 0;
	if (n > 12) n = 12;
	gThreads = n;
	v = (DWORD)n;
	OxzCoreSetThreads(n);
	RegSetKeyValueW(HKEY_CURRENT_USER, kKey, L"threads", REG_DWORD, &v, sizeof(v));
}

void OxzOptSetSplitText(const wchar_t* text)
{
	RememberSplit(text);
}

int OxzSplitPresetCount() { return (int)(sizeof(kPreset) / sizeof(kPreset[0])); }
const wchar_t* OxzSplitPreset(int index) { return (index >= 0 && index < OxzSplitPresetCount()) ? kPreset[index] : L""; }

static HANDLE OpenRead(const wchar_t* path)
{
	return CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static int ReadAt(HANDLE h, uint64_t off, void* buf, DWORD n)
{
	LARGE_INTEGER li;
	DWORD got = 0;
	li.QuadPart = (LONGLONG)off;
	if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN)) return 0;
	if (!ReadFile(h, buf, n, &got, NULL) || got != n) return 0;
	return 1;
}

static int WriteAll(HANDLE h, const void* buf, DWORD n)
{
	const uint8_t* p = (const uint8_t*)buf;
	DWORD done = 0;
	while (done < n) {
		DWORD w = 0;
		if (!WriteFile(h, p + done, n - done, &w, NULL) || !w) return 0;
		done += w;
	}
	return 1;
}

static int FillHead(const uint8_t* b, VolHead* v)
{
	if (memcmp(b, "OXZVOL1", 7) != 0 || b[7] != 0) return 0;
	if (Get32(b + 8) != 1) return 0;
	v->index = Get32(b + 12);
	v->digits = Get32(b + 16);
	v->flags = Get32(b + 20);
	v->payload = Get64(b + 24);
	v->total = Get64(b + 32);
	v->slice = Get64(b + 40);
	v->parts = Get32(b + 48);
	if (!v->parts || v->parts > 100000u || v->index >= v->parts) return 0;
	if (v->digits < 2 || v->digits > 6) return 0;
	return 1;
}

static int ReadVol(const std::wstring& path, VolHead* v)
{
	HANDLE h = OpenRead(path.c_str());
	uint8_t b[64];
	LARGE_INTEGER sz;
	if (h == INVALID_HANDLE_VALUE) return 0;
	if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 16) { CloseHandle(h); return 0; }
	if (!ReadAt(h, 0, b, 8)) { CloseHandle(h); return 0; }
	if (memcmp(b, "OXZVOL1", 7) == 0) {
		if (!ReadAt(h, 0, b, 64) || !FillHead(b, v)) { CloseHandle(h); return 0; }
		v->payOff = 64;
		CloseHandle(h);
		return 1;
	}
	if (sz.QuadPart >= 80 && b[0] == 'M' && b[1] == 'Z') {
		uint8_t foot[16];
		uint64_t at;
		if (!ReadAt(h, (uint64_t)sz.QuadPart - 16, foot, 16)) { CloseHandle(h); return 0; }
		if (memcmp(foot, "OXZSFX1", 7) != 0 || foot[7] != 0) { CloseHandle(h); return 0; }
		at = Get64(foot + 8);
		if (at + 64 > (uint64_t)sz.QuadPart) { CloseHandle(h); return 0; }
		if (!ReadAt(h, at, b, 64) || !FillHead(b, v)) { CloseHandle(h); return 0; }
		v->payOff = at + 64;
		CloseHandle(h);
		return 1;
	}
	CloseHandle(h);
	return 0;
}

static std::wstring StemOf(const std::wstring& path)
{
	std::wstring s = path;
	size_t dot = s.find_last_of(L'.');
	size_t n;
	if (dot == std::wstring::npos) return s;
	n = s.size() - dot;
	if (n == 4 && (!_wcsicmp(s.c_str() + dot, L".oxz") || !_wcsicmp(s.c_str() + dot, L".exe"))) {
		s.resize(dot);
		return s;
	}
	if (n >= 4 && n <= 8 && (s[dot + 1] == L'o' || s[dot + 1] == L'O')) {
		size_t i;
		for (i = dot + 2; i < s.size(); ++i) if (s[i] < L'0' || s[i] > L'9') return s;
		if (s.size() - (dot + 2) >= 2) s.resize(dot);
	}
	return s;
}

static std::wstring PartPath(const std::wstring& stem, uint32_t index, uint32_t digits, int sfx)
{
	wchar_t num[16], ext[24];
	if (index == 0) return stem + (sfx ? L".exe" : L".oxz");
	_snwprintf_s(num, _TRUNCATE, L"%u", index);
	if (wcslen(num) > digits) digits = (uint32_t)wcslen(num);
	_snwprintf_s(ext, _TRUNCATE, L".o%0*u", digits, index);
	return stem + ext;
}

int OxzIsOxzFamily(const wchar_t* path)
{
	VolHead v;
	if (!path || !path[0]) return 0;
	if (OxzCoreIsPkg(path)) return 1;
	return ReadVol(path, &v);
}

int OxzMaterializeOxz(const wchar_t* path, std::wstring& out, int* temp)
{
	VolHead first, base;
	std::wstring stem, tempPath;
	HANDLE outH;
	wchar_t dir[MAX_PATH], file[MAX_PATH];
	uint32_t i;
	if (temp) *temp = 0;
	out.clear();
	if (!path) return 0;
	if (OxzCoreIsPkg(path)) { out = path; return 1; }
	if (!ReadVol(path, &first)) return 0;
	stem = StemOf(path);
	if (!ReadVol(PartPath(stem, 0, first.digits, first.flags & 1), &base)) return 0;
	if (!GetTempPathW(MAX_PATH, dir)) return 0;
	if (!GetTempFileNameW(dir, L"oxj", 0, file)) return 0;
	tempPath = file;
	outH = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (outH == INVALID_HANDLE_VALUE) { DeleteFileW(tempPath.c_str()); return 0; }
	uint8_t* buf = (uint8_t*)malloc(1 << 20);
	if (!buf) { CloseHandle(outH); DeleteFileW(tempPath.c_str()); return 0; }
	for (i = 0; i < base.parts; ++i) {
		VolHead part;
		std::wstring name = PartPath(stem, i, base.digits, base.flags & 1);
		HANDLE in;
		uint64_t left;
		if (!ReadVol(name, &part) || part.index != i) { free(buf); CloseHandle(outH); DeleteFileW(tempPath.c_str()); return 0; }
		in = OpenRead(name.c_str());
		if (in == INVALID_HANDLE_VALUE) { free(buf); CloseHandle(outH); DeleteFileW(tempPath.c_str()); return 0; }
		left = part.payload;
		{
			LARGE_INTEGER li;
			li.QuadPart = (LONGLONG)part.payOff;
			if (!SetFilePointerEx(in, li, NULL, FILE_BEGIN)) { free(buf); CloseHandle(in); CloseHandle(outH); DeleteFileW(tempPath.c_str()); return 0; }
		}
		while (left) {
			DWORD want = left > (1u << 20) ? (DWORD)(1u << 20) : (DWORD)left;
			DWORD got = 0;
			if (!ReadFile(in, buf, want, &got, NULL) || got != want || !WriteAll(outH, buf, got)) {
				free(buf); CloseHandle(in); CloseHandle(outH); DeleteFileW(tempPath.c_str()); return 0;
			}
			left -= got;
		}
		CloseHandle(in);
	}
	free(buf);
	CloseHandle(outH);
	out = tempPath;
	if (temp) *temp = 1;
	return 1;
}

void OxzReleaseOxz(const std::wstring& path, int temp)
{
	if (temp && !path.empty()) DeleteFileW(path.c_str());
}

static int CopySlice(HANDLE in, HANDLE out, unsigned long long n, uint8_t* buf, DWORD bufN)
{
	while (n) {
		DWORD want = n > bufN ? bufN : (DWORD)n;
		DWORD got = 0;
		if (!ReadFile(in, buf, want, &got, NULL) || got != want) return 0;
		if (!WriteAll(out, buf, got)) return 0;
		n -= got;
	}
	return 1;
}

static int CopyFileTo(HANDLE out, const std::wstring& path, unsigned long long* size, uint8_t* buf, DWORD bufN)
{
	HANDLE in = OpenRead(path.c_str());
	LARGE_INTEGER sz;
	if (in == INVALID_HANDLE_VALUE) return 0;
	if (!GetFileSizeEx(in, &sz)) { CloseHandle(in); return 0; }
	if (!CopySlice(in, out, (unsigned long long)sz.QuadPart, buf, bufN)) { CloseHandle(in); return 0; }
	CloseHandle(in);
	if (size) *size = (unsigned long long)sz.QuadPart;
	return 1;
}

static void WriteHead(uint8_t* b, uint32_t index, uint32_t digits, uint32_t flags, uint64_t payload, uint64_t total, uint64_t slice, uint32_t parts)
{
	memset(b, 0, 64);
	memcpy(b, "OXZVOL1", 7);
	Put32(b + 8, 1);
	Put32(b + 12, index);
	Put32(b + 16, digits);
	Put32(b + 20, flags);
	Put64(b + 24, payload);
	Put64(b + 32, total);
	Put64(b + 40, slice);
	Put32(b + 48, parts);
}

int OxzSealOutput(const std::wstring& archive, unsigned long long* bytes, std::wstring& err)
{
	unsigned long long total = 0, slice, left, written = 0;
	unsigned parts, digits, last, i;
	unsigned long long lim;
	int sfx = OxzOptSfx();
	uint32_t flags;
	std::wstring stem, stub;
	HANDLE in;
	LARGE_INTEGER sz;
	std::vector<std::wstring> temps, finals;
	if (!OxzOptSplit() && !sfx) return 1;
	if (archive.size() < 4 || _wcsicmp(archive.c_str() + archive.size() - 4, L".oxz") != 0) return 1;
	stub = OxzBeside(L"OxzSfx.exe");
	if (sfx && GetFileAttributesW(stub.c_str()) == INVALID_FILE_ATTRIBUTES) {
		err = OxzTxt(OS_SFX_MISSING);
		return 0;
	}
	in = OpenRead(archive.c_str());
	if (in == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_SAVE_FAIL); return 0; }
	if (!GetFileSizeEx(in, &sz) || sz.QuadPart < 16) { CloseHandle(in); err = OxzTxt(OS_SAVE_FAIL); return 0; }
	total = (unsigned long long)sz.QuadPart;
	slice = OxzOptSplit() ? OxzOptSplit() : total;
	if (!slice) slice = total;
	if (OxzOptSplit() && slice < 64ull * 1024ull) slice = 64ull * 1024ull;
	{
		unsigned long long stubN = 0;
		unsigned long long over0, overN, cap0, capN;
		const unsigned long long kHead = 64;
		const unsigned long long kFoot = 16;
		if (sfx) {
			WIN32_FILE_ATTRIBUTE_DATA fa;
			if (!GetFileAttributesExW(stub.c_str(), GetFileExInfoStandard, &fa)) { CloseHandle(in); err = OxzTxt(OS_SFX_MISSING); return 0; }
			stubN = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
		}
		over0 = kHead + (sfx ? stubN + kFoot : 0);
		overN = kHead;
		if (OxzOptSplit()) {
			if (slice <= over0 || slice <= overN) { CloseHandle(in); err = OxzTxt(OS_SPLIT_SMALL); return 0; }
			cap0 = slice - over0;
			capN = slice - overN;
			if (total <= cap0) parts = 1;
			else parts = 1 + (unsigned)((total - cap0 + capN - 1) / capN);
		} else {
			cap0 = total;
			capN = total;
			parts = 1;
		}
		if (!parts) parts = 1;
		if (parts > 100000u) { CloseHandle(in); err = OxzTxt(OS_SAVE_FAIL); return 0; }
		last = parts - 1;
		digits = 2;
		lim = 100;
		while (last >= lim && digits < 6) { digits++; lim *= 10; }
		flags = sfx ? 1u : 0u;
		if (parts < 2 && !sfx) {
			CloseHandle(in);
			if (bytes) *bytes = total;
			return 1;
		}
		stem = archive;
		stem.resize(stem.size() - 4);
		left = total;
		{
			uint8_t* buf = (uint8_t*)malloc(1 << 20);
			if (!buf) { CloseHandle(in); err = OxzTxt(OS_SAVE_FAIL); return 0; }
			for (i = 0; i < parts; ++i) {
				unsigned long long cap = (i == 0) ? cap0 : capN;
				unsigned long long n = left > cap ? cap : left;
				std::wstring dest = PartPath(stem, i, digits, sfx);
				std::wstring tmp = dest + L".part";
				HANDLE out;
				uint8_t head[64];
				if (!n) { free(buf); CloseHandle(in); err = OxzTxt(OS_SAVE_FAIL); return 0; }
				out = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (out == INVALID_HANDLE_VALUE) { free(buf); CloseHandle(in); in = INVALID_HANDLE_VALUE; err = OxzTxt(OS_SAVE_FAIL); goto fail; }
				if (i == 0 && sfx) {
					if (!CopyFileTo(out, stub, &stubN, buf, 1 << 20)) { CloseHandle(out); DeleteFileW(tmp.c_str()); free(buf); CloseHandle(in); in = INVALID_HANDLE_VALUE; err = OxzTxt(OS_SFX_MISSING); goto fail; }
				}
				WriteHead(head, i, digits, flags, n, total, OxzOptSplit(), parts);
				if (!WriteAll(out, head, 64) || !CopySlice(in, out, n, buf, 1 << 20)) { CloseHandle(out); DeleteFileW(tmp.c_str()); free(buf); CloseHandle(in); in = INVALID_HANDLE_VALUE; err = OxzTxt(OS_SAVE_FAIL); goto fail; }
		if (i == 0 && sfx) {
			uint8_t foot[16];
			memset(foot, 0, sizeof(foot));
			memcpy(foot, "OXZSFX1", 7);
			Put64(foot + 8, stubN);
			if (!WriteAll(out, foot, 16)) { CloseHandle(out); DeleteFileW(tmp.c_str()); free(buf); CloseHandle(in); in = INVALID_HANDLE_VALUE; err = OxzTxt(OS_SAVE_FAIL); goto fail; }
		}
		CloseHandle(out);
		temps.push_back(tmp);
		finals.push_back(dest);
		written += n + 64;
		if (i == 0 && sfx) written += stubN + 16;
		left -= n;
			}
			free(buf);
		}
	}
	CloseHandle(in);
	in = INVALID_HANDLE_VALUE;
	for (i = 0; i < temps.size(); ++i) {
		if (!MoveFileExW(temps[i].c_str(), finals[i].c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			err = OxzTxt(OS_SAVE_FAIL);
			goto fail;
		}
		temps[i].clear();
	}
	if (sfx) DeleteFileW(archive.c_str());
	{
		unsigned width;
		for (width = 2; width <= 4; ++width) {
			unsigned gap = 0;
			unsigned k;
			for (k = parts; k < 100000u && gap < 8; ++k) {
				wchar_t num[16];
				std::wstring old;
				_snwprintf_s(num, _TRUNCATE, L"%u", k);
				if (wcslen(num) > width) break;
				old = PartPath(stem, k, width, 0);
				if (GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES) { DeleteFileW(old.c_str()); gap = 0; }
				else gap++;
			}
		}
	}
	if (bytes) *bytes = written;
	return 1;
fail:
	if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
	for (size_t t = 0; t < temps.size(); ++t) if (!temps[t].empty()) DeleteFileW(temps[t].c_str());
	return 0;
}
