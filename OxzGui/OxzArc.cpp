#include "OxzArc.h"
#include "OxzIn.h"
#include "OxzWork.h"
#include "OxzLang.h"
#include "OxzFilt.h"
#include "lzma_sdk/7z.h"
#include "lzma_sdk/7zFile.h"
#include "lzma_sdk/7zAlloc.h"
#include "lzma_sdk/7zCrc.h"
#include "unzip.h"
#include "iowin32.h"
#include "zlib.h"
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int EndsWithI(const wchar_t* path, const wchar_t* ext)
{
	size_t n = path ? wcslen(path) : 0;
	size_t e = ext ? wcslen(ext) : 0;
	if (!n || e > n) return 0;
	return _wcsicmp(path + n - e, ext) == 0;
}

static int ReadHead(const wchar_t* path, unsigned char* buf, DWORD n, DWORD* got)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD rd = 0;
	BOOL ok = ReadFile(h, buf, n, &rd, NULL);
	CloseHandle(h);
	if (got) *got = rd;
	return ok ? 1 : 0;
}

int OxzArchiveKind(const wchar_t* path)
{
	unsigned char b[512];
	DWORD n = 0;
	if (!path || !path[0]) return 0;
	if (!ReadHead(path, b, sizeof(b), &n)) {
		if (EndsWithI(path, L".oxz") || EndsWithI(path, L".7z")) return 1;
		if (EndsWithI(path, L".zip")) return 2;
		if (EndsWithI(path, L".gz") || EndsWithI(path, L".tgz")) return 3;
		if (EndsWithI(path, L".tar")) return 4;
		if (EndsWithI(path, L".rar")) return 5;
		if (EndsWithI(path, L".zst") || EndsWithI(path, L".tzst") || EndsWithI(path, L".tar.zst")) return 6;
		if (EndsWithI(path, L".bz2") || EndsWithI(path, L".tbz") || EndsWithI(path, L".tbz2") || EndsWithI(path, L".tar.bz2")) return 7;
		if (EndsWithI(path, L".xz") || EndsWithI(path, L".txz") || EndsWithI(path, L".tar.xz")) return 8;
		if (EndsWithI(path, L".cab")) return 9;
		if (EndsWithI(path, L".iso")) return 10;
		if (EndsWithI(path, L".wim")) return 11;
		if (EndsWithI(path, L".lzh") || EndsWithI(path, L".lha")) return 12;
		if (EndsWithI(path, L".arj")) return 13;
		return 0;
	}
	if (n >= 6 && b[0] == 0x37 && b[1] == 0x7A && b[2] == 0xBC && b[3] == 0xAF && b[4] == 0x27 && b[5] == 0x1C) return 1;
	if (n >= 4 && b[0] == 'P' && b[1] == 'K' && (b[2] == 3 || b[2] == 5 || b[2] == 7) && (b[3] == 4 || b[3] == 6 || b[3] == 8)) return 2;
	if (n >= 2 && b[0] == 0x1F && b[1] == 0x8B) return 3;
	if (n >= 262 && memcmp(b + 257, "ustar", 5) == 0) return 4;
	if (n >= 4 && memcmp(b, "Rar!", 4) == 0) return 5;
	if (n >= 3 && memcmp(b, "BZh", 3) == 0) return 7;
	if (n >= 6 && b[0] == 0xFD && memcmp(b + 1, "7zXZ", 4) == 0) return 8;
	if (n >= 4 && b[0] == 0x28 && b[1] == 0xB5 && b[2] == 0x2F && b[3] == 0xFD) return 6;
	if (n >= 4 && memcmp(b, "MSCF", 4) == 0) return 9;
	if (n >= 5 && memcmp(b, "MSWIM", 5) == 0) return 11;
	if (n >= 2 && b[0] == 0x60 && b[1] == 0xEA) return 13;
	if (EndsWithI(path, L".oxz") || EndsWithI(path, L".7z")) return 1;
	if (EndsWithI(path, L".zip")) return 2;
	if (EndsWithI(path, L".gz") || EndsWithI(path, L".tgz") || EndsWithI(path, L".tar.gz")) return 3;
	if (EndsWithI(path, L".tar")) return 4;
	if (EndsWithI(path, L".rar")) return 5;
	if (EndsWithI(path, L".zst") || EndsWithI(path, L".tzst") || EndsWithI(path, L".tar.zst")) return 6;
	if (EndsWithI(path, L".bz2") || EndsWithI(path, L".tbz") || EndsWithI(path, L".tbz2") || EndsWithI(path, L".tar.bz2")) return 7;
	if (EndsWithI(path, L".xz") || EndsWithI(path, L".txz") || EndsWithI(path, L".tar.xz")) return 8;
	if (EndsWithI(path, L".cab")) return 9;
	if (EndsWithI(path, L".iso")) return 10;
	if (EndsWithI(path, L".wim")) return 11;
	if (EndsWithI(path, L".lzh") || EndsWithI(path, L".lha")) return 12;
	if (EndsWithI(path, L".arj")) return 13;
	return 0;
}

static int SafeRel(const wchar_t* name, std::wstring& rel)
{
	rel.clear();
	if (!name || !name[0]) return 0;
	if (name[0] == L'\\' || name[0] == L'/') return 0;
	if (((name[0] >= L'A' && name[0] <= L'Z') || (name[0] >= L'a' && name[0] <= L'z')) && name[1] == L':') return 0;
	std::wstring cur;
	const wchar_t* p = name;
	while (*p) {
		while (*p == L'\\' || *p == L'/') p++;
		if (!*p) break;
		const wchar_t* s = p;
		while (*p && *p != L'\\' && *p != L'/') p++;
		std::wstring part(s, p - s);
		if (part.empty() || part == L".") continue;
		if (part == L"..") return 0;
		if (!cur.empty()) cur += L"\\";
		cur += part;
	}
	if (cur.empty()) return 0;
	rel.swap(cur);
	return 1;
}

static void EnsureParent(const std::wstring& full)
{
	size_t s = full.find_last_of(L'\\');
	if (s == std::wstring::npos) return;
	for (size_t i = 3; i < s; ++i) {
		if (full[i] == L'\\') CreateDirectoryW(full.substr(0, i).c_str(), NULL);
	}
	CreateDirectoryW(full.substr(0, s).c_str(), NULL);
}

static int WriteBytes(const std::wstring& path, const void* data, size_t size, const FILETIME* when)
{
	EnsureParent(path);
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	const unsigned char* p = (const unsigned char*)data;
	size_t left = size;
	OxzPulseSize(size);
	while (left) {
		DWORD n = left > 1 << 20 ? 1 << 20 : (DWORD)left;
		DWORD wr = 0;
		if (!WriteFile(h, p, n, &wr, NULL) || wr != n) { CloseHandle(h); return 0; }
		OxzPulseAdd(n);
		p += n;
		left -= n;
	}
	if (when && (when->dwLowDateTime || when->dwHighDateTime)) SetFileTime(h, when, when, when);
	CloseHandle(h);
	return 1;
}

static void NoteStep(int i, int n, const wchar_t* name, int base, int span)
{
	OxzNoteA(i, n, name, base, span);
}

static int MTimeOf(const CSzArEx* db, UInt32 i, FILETIME* out)
{
	out->dwLowDateTime = 0;
	out->dwHighDateTime = 0;
	if (!SzBitWithVals_Check(&db->MTime, i)) return 0;
	CNtfsFileTime nt = db->MTime.Vals[i];
	ULARGE_INTEGER ull;
	ull.LowPart = nt.Low;
	ull.HighPart = nt.High;
	if (ull.QuadPart < 116444736000000000ULL) return 0;
	out->dwLowDateTime = nt.Low;
	out->dwHighDateTime = nt.High;
	return 1;
}

static int Extract7z(const wchar_t* arc, const wchar_t* dest, int rip, std::wstring& err, int base, int span)
{
	CFileInStream archiveStream;
	CLookToRead2 lookStream;
	CSzArEx db;
	ISzAlloc allocImp;
	ISzAlloc allocTempImp;
	UInt32 blockIndex = 0xFFFFFFFF;
	Byte* outBuffer = NULL;
	size_t outBufferSize = 0;
	int ok = 0;
	memset(&archiveStream, 0, sizeof(archiveStream));
	memset(&lookStream, 0, sizeof(lookStream));
	memset(&db, 0, sizeof(db));
	allocImp.Alloc = SzAlloc;
	allocImp.Free = SzFree;
	allocTempImp.Alloc = SzAllocTemp;
	allocTempImp.Free = SzFreeTemp;
	File_Construct(&archiveStream.file);
	FileInStream_CreateVTable(&archiveStream);
	archiveStream.wres = 0;
	if (InFile_OpenW(&archiveStream.file, arc) != 0) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	LookToRead2_CreateVTable(&lookStream, False);
	lookStream.buf = (Byte*)ISzAlloc_Alloc(&allocImp, (size_t)1 << 18);
	if (!lookStream.buf) { File_Close(&archiveStream.file); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	lookStream.bufSize = (size_t)1 << 18;
	lookStream.realStream = &archiveStream.vt;
	LookToRead2_INIT(&lookStream);
	CrcGenerateTable();
	SzArEx_Init(&db);
	if (SzArEx_Open(&db, &lookStream.vt, &allocImp, &allocTempImp) != SZ_OK) {
		err = OxzTxt(OS_OPEN_FAIL);
		goto done;
	}
	CreateDirectoryW(dest, NULL);
	for (UInt32 i = 0; i < db.NumFiles; ++i) {
		wchar_t name[4096];
		std::wstring rel, full;
		size_t len = SzArEx_GetFileNameUtf16(&db, i, NULL);
		FILETIME when;
		int haveTime = MTimeOf(&db, i, &when);
		if (len == 0 || len > 4096) { err = OxzTxt(OS_OPEN_FAIL); goto done; }
		SzArEx_GetFileNameUtf16(&db, i, (UInt16*)name);
		for (wchar_t* p = name; *p; ++p) if (*p == L'/') *p = L'\\';
		if (!SafeRel(name, rel)) { err = OxzTxt(OS_OPEN_FAIL); goto done; }
		full = std::wstring(dest) + L"\\" + rel;
		NoteStep((int)i, (int)db.NumFiles, rel.c_str(), base, span);
		if (SzArEx_IsDir(&db, i)) {
			EnsureParent(full + L"\\x");
			CreateDirectoryW(full.c_str(), NULL);
			continue;
		}
		{
			size_t offset = 0, outSize = 0;
			const Byte* src;
			Byte* owned = NULL;
			if (SzArEx_Extract(&db, &lookStream.vt, i, &blockIndex, &outBuffer, &outBufferSize, &offset, &outSize, &allocImp, &allocTempImp) != SZ_OK) {
				err = OxzTxt(OS_OPEN_FAIL);
				goto done;
			}
			src = outBuffer + offset;
			if (rip && outSize >= 2 && outBuffer[offset] == 'M' && outBuffer[offset + 1] == 'Z') {
				owned = (Byte*)malloc(outSize);
				if (!owned) { err = OxzTxt(OS_OPEN_FAIL); goto done; }
				memcpy(owned, outBuffer + offset, outSize);
				OxzRipConvert(owned, outSize, 0);
				src = owned;
			}
			if (!WriteBytes(full, src, outSize, haveTime ? &when : NULL)) {
				free(owned);
				err = OxzTxt(OS_OPEN_FAIL);
				goto done;
			}
			free(owned);
		}
	}
	ok = 1;
done:
	if (outBuffer) ISzAlloc_Free(&allocImp, outBuffer);
	SzArEx_Free(&db, &allocImp);
	if (lookStream.buf) ISzAlloc_Free(&allocImp, lookStream.buf);
	File_Close(&archiveStream.file);
	if (!ok && err.empty()) err = OxzTxt(OS_OPEN_FAIL);
	return ok ? 0 : 2;
}

static std::wstring FromBytes(const char* s, int n, UINT cp)
{
	if (n < 0) n = (int)strlen(s);
	if (n == 0) return L"";
	int c = MultiByteToWideChar(cp, 0, s, n, NULL, 0);
	if (c <= 0) return L"";
	std::wstring w(c, L'\0');
	MultiByteToWideChar(cp, 0, s, n, &w[0], c);
	return w;
}

static int ExtractZip(const wchar_t* arc, const wchar_t* dest, std::wstring& err, int base, int span)
{
	zlib_filefunc64_def ff;
	unzFile uf;
	unz_global_info64 gi;
	unsigned i = 0;
	fill_win32_filefunc64W(&ff);
	uf = unzOpen2_64(arc, &ff);
	if (!uf) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (unzGetGlobalInfo64(uf, &gi) != UNZ_OK) { unzClose(uf); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	CreateDirectoryW(dest, NULL);
	if (gi.number_entry == 0) { unzClose(uf); NoteStep(0, 1, L"", base, span); return 0; }
	if (unzGoToFirstFile(uf) != UNZ_OK) { unzClose(uf); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	do {
		unz_file_info64 fi;
		char raw[2048];
		std::wstring name, rel, full;
		FILETIME loc, utc;
		int haveTime = 0;
		unsigned char buf[1 << 16];
		HANDLE h = INVALID_HANDLE_VALUE;
		if (unzGetCurrentFileInfo64(uf, &fi, raw, sizeof(raw) - 1, NULL, 0, NULL, 0) != UNZ_OK) { err = OxzTxt(OS_OPEN_FAIL); break; }
		raw[sizeof(raw) - 1] = 0;
		if (fi.compression_method != 0 && fi.compression_method != 8) { err = OxzTxt(OS_OPEN_FAIL); break; }
		name = FromBytes(raw, (int)strlen(raw), (fi.flag & 0x800) ? CP_UTF8 : CP_ACP);
		for (size_t k = 0; k < name.size(); ++k) if (name[k] == L'/') name[k] = L'\\';
		if (!SafeRel(name.c_str(), rel)) { err = OxzTxt(OS_OPEN_FAIL); break; }
		full = std::wstring(dest) + L"\\" + rel;
		NoteStep((int)i, (int)gi.number_entry, rel.c_str(), base, span);
		OxzPulseSize(fi.uncompressed_size);
		i++;
		if (!name.empty() && (name.back() == L'\\' || name.back() == L'/')) {
			CreateDirectoryW(full.c_str(), NULL);
			continue;
		}
		if ((fi.external_fa >> 16) == 0 && (fi.external_fa & 0x10)) {
			CreateDirectoryW(full.c_str(), NULL);
			continue;
		}
		if (unzOpenCurrentFile(uf) != UNZ_OK) { err = OxzTxt(OS_OPEN_FAIL); break; }
		EnsureParent(full);
		h = CreateFileW(full.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE) { unzCloseCurrentFile(uf); err = OxzTxt(OS_OPEN_FAIL); break; }
		for (;;) {
			int rd = unzReadCurrentFile(uf, buf, sizeof(buf));
			DWORD wr = 0;
			if (rd < 0) { err = OxzTxt(OS_OPEN_FAIL); break; }
			if (rd == 0) break;
			if (!WriteFile(h, buf, (DWORD)rd, &wr, NULL) || wr != (DWORD)rd) { err = OxzTxt(OS_OPEN_FAIL); break; }
			OxzPulseAdd((uint64_t)rd);
		}
		if (DosDateTimeToFileTime((WORD)(fi.dosDate >> 16), (WORD)(fi.dosDate & 0xFFFF), &loc) && LocalFileTimeToFileTime(&loc, &utc)) {
			SetFileTime(h, &utc, &utc, &utc);
			haveTime = 1;
		}
		(void)haveTime;
		CloseHandle(h);
		unzCloseCurrentFile(uf);
		if (!err.empty()) break;
	} while (unzGoToNextFile(uf) == UNZ_OK);
	unzClose(uf);
	if (!err.empty()) return 2;
	return 0;
}

static unsigned long long Octal(const char* s, int n)
{
	unsigned long long v = 0;
	int i;
	for (i = 0; i < n; ++i) {
		if (s[i] == 0 || s[i] == ' ') continue;
		if (s[i] < '0' || s[i] > '7') break;
		v = (v << 3) + (unsigned)(s[i] - '0');
	}
	return v;
}

static int TarChecksum(const unsigned char* h)
{
	unsigned sum = 0, field = 0;
	int i;
	for (i = 0; i < 512; ++i) sum += h[i];
	for (i = 148; i < 156; ++i) field += h[i];
	sum = sum - field + 8 * (unsigned)' ';
	return sum == (unsigned)Octal((const char*)h + 148, 8);
}

static int ExtractTarHandle(HANDLE h, const wchar_t* dest, std::wstring& err, int base, int span)
{
	unsigned char block[512];
	std::wstring longName;
	LARGE_INTEGER total;
	int index = 0, guess = 1;
	total.QuadPart = 0;
	GetFileSizeEx(h, &total);
	if (total.QuadPart > 512) guess = (int)(total.QuadPart / 512);
	if (guess < 1) guess = 1;
	CreateDirectoryW(dest, NULL);
	for (;;) {
		DWORD rd = 0;
		int zero = 1, i;
		unsigned long long size, pad;
		char type;
		std::wstring name, rel, full;
		if (!ReadFile(h, block, 512, &rd, NULL) || rd == 0) break;
		if (rd != 512) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
		for (i = 0; i < 512; ++i) if (block[i]) { zero = 0; break; }
		if (zero) break;
		if (!TarChecksum(block)) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
		size = Octal((const char*)block + 124, 12);
		type = (char)block[156];
		if (type == 'L') {
			std::string raw;
			unsigned long long left = size;
			while (left) {
				unsigned char tmp[512];
				DWORD n = 0;
				if (!ReadFile(h, tmp, 512, &n, NULL) || n != 512) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
				unsigned take = left > 512 ? 512 : (unsigned)left;
				raw.append((char*)tmp, take);
				left -= take;
			}
			longName = FromBytes(raw.c_str(), (int)raw.size(), CP_UTF8);
			continue;
		}
		if (type == 'x' || type == 'g') {
			unsigned long long left = (size + 511) & ~511ull;
			LARGE_INTEGER mv;
			mv.QuadPart = (LONGLONG)left;
			SetFilePointerEx(h, mv, NULL, FILE_CURRENT);
			continue;
		}
		if (!longName.empty()) name.swap(longName);
		else {
			std::string raw((char*)block, strnlen((char*)block, 100));
			if (memcmp(block + 257, "ustar", 5) == 0) {
				std::string pre((char*)block + 345, strnlen((char*)block + 345, 155));
				if (!pre.empty()) raw = pre + "/" + raw;
			}
			name = FromBytes(raw.c_str(), (int)raw.size(), CP_UTF8);
			if (name.empty()) name = FromBytes(raw.c_str(), (int)raw.size(), CP_ACP);
		}
		for (size_t k = 0; k < name.size(); ++k) if (name[k] == L'/') name[k] = L'\\';
		NoteStep(index++, guess, name.c_str(), base, span);
		if (!SafeRel(name.c_str(), rel)) {
			int onlyDot = !name.empty();
			for (size_t k = 0; onlyDot && k < name.size(); ++k) if (name[k] != L'.' && name[k] != L'\\') onlyDot = 0;
			if (!onlyDot) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
			unsigned long long skip = (size + 511) & ~511ull;
			if (skip) {
				LARGE_INTEGER mv;
				mv.QuadPart = (LONGLONG)skip;
				SetFilePointerEx(h, mv, NULL, FILE_CURRENT);
			}
			continue;
		}
		full = std::wstring(dest) + L"\\" + rel;
		if (type == '5' || (!name.empty() && name.back() == L'\\')) {
			CreateDirectoryW(full.c_str(), NULL);
			pad = (size + 511) & ~511ull;
			if (pad) {
				LARGE_INTEGER mv;
				mv.QuadPart = (LONGLONG)pad;
				SetFilePointerEx(h, mv, NULL, FILE_CURRENT);
			}
			continue;
		}
		if (type != '0' && type != '\0') {
			unsigned long long skip = (size + 511) & ~511ull;
			LARGE_INTEGER mv;
			mv.QuadPart = (LONGLONG)skip;
			SetFilePointerEx(h, mv, NULL, FILE_CURRENT);
			continue;
		}
		{
			unsigned long long left = size;
			HANDLE out;
			unsigned long long unix = Octal((const char*)block + 136, 12);
			FILETIME when;
			ULARGE_INTEGER u;
			EnsureParent(full);
			out = CreateFileW(full.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (out == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
			while (left) {
				unsigned char tmp[1 << 16];
				DWORD want = left > sizeof(tmp) ? (DWORD)sizeof(tmp) : (DWORD)left;
				DWORD n = 0, wr = 0;
				if (!ReadFile(h, tmp, want, &n, NULL) || n != want) { CloseHandle(out); err = OxzTxt(OS_OPEN_FAIL); return 2; }
				if (!WriteFile(out, tmp, n, &wr, NULL) || wr != n) { CloseHandle(out); err = OxzTxt(OS_OPEN_FAIL); return 2; }
				left -= n;
			}
			u.QuadPart = (unix + 11644473600ull) * 10000000ull;
			when.dwLowDateTime = u.LowPart;
			when.dwHighDateTime = u.HighPart;
			SetFileTime(out, &when, &when, &when);
			CloseHandle(out);
			pad = (512 - (size & 511)) & 511;
			if (pad) {
				LARGE_INTEGER mv;
				mv.QuadPart = (LONGLONG)pad;
				SetFilePointerEx(h, mv, NULL, FILE_CURRENT);
			}
		}
	}
	NoteStep(guess, guess, L"", base, span);
	return 0;
}

static int ExtractTarFile(const wchar_t* path, const wchar_t* dest, std::wstring& err, int base, int span)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	int rc;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	rc = ExtractTarHandle(h, dest, err, base, span);
	CloseHandle(h);
	return rc;
}

static int ExtractGz(const wchar_t* arc, const wchar_t* dest, std::wstring& err, int base, int span)
{
	gzFile g = gzopen_w(arc, "rb");
	std::wstring stem = OxzBaseName(arc);
	std::wstring outPath;
	HANDLE out;
	unsigned char buf[1 << 16];
	int looks = 0;
	if (!g) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (stem.size() >= 4 && _wcsicmp(stem.c_str() + stem.size() - 4, L".tgz") == 0) stem.resize(stem.size() - 4), stem += L".tar";
	else if (stem.size() >= 3 && _wcsicmp(stem.c_str() + stem.size() - 3, L".gz") == 0) stem.resize(stem.size() - 3);
	if (stem.empty()) stem = L"file";
	CreateDirectoryW(dest, NULL);
	outPath = std::wstring(dest) + L"\\" + stem;
	out = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (out == INVALID_HANDLE_VALUE) { gzclose(g); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	NoteStep(0, 2, stem.c_str(), base, span);
	for (;;) {
		int n = gzread(g, buf, sizeof(buf));
		DWORD wr = 0;
		if (n < 0) { CloseHandle(out); gzclose(g); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		if (n == 0) break;
		if (!WriteFile(out, buf, (DWORD)n, &wr, NULL) || wr != (DWORD)n) { CloseHandle(out); gzclose(g); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	}
	CloseHandle(out);
	gzclose(g);
	{
		unsigned char mag[8];
		DWORD got = 0;
		HANDLE h = CreateFileW(outPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (h != INVALID_HANDLE_VALUE) {
			LARGE_INTEGER sz;
			sz.QuadPart = 0;
			GetFileSizeEx(h, &sz);
			if (sz.QuadPart > 262) {
				LARGE_INTEGER mv;
				mv.QuadPart = 257;
				SetFilePointerEx(h, mv, NULL, FILE_BEGIN);
				ReadFile(h, mag, 5, &got, NULL);
				if (got == 5 && memcmp(mag, "ustar", 5) == 0) looks = 1;
			}
			if (!looks && sz.QuadPart >= 512 && (EndsWithI(stem.c_str(), L".tar"))) looks = 1;
			CloseHandle(h);
		}
	}
	if (!looks) { NoteStep(1, 2, stem.c_str(), base, span); return 0; }
	{
		int rc = ExtractTarFile(outPath.c_str(), dest, err, base + span / 2, span - span / 2);
		DeleteFileW(outPath.c_str());
		return rc;
	}
}

int OxzExtractNative(const wchar_t* arc, const wchar_t* dest, int kind, int rip, std::wstring& err, int base, int span)
{
	if (!arc || !dest || span < 1) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (kind == 1) return Extract7z(arc, dest, rip, err, base, span);
	if (kind == 2) return ExtractZip(arc, dest, err, base, span);
	if (kind == 3) return ExtractGz(arc, dest, err, base, span);
	if (kind == 4) return ExtractTarFile(arc, dest, err, base, span);
	err = OxzTxt(OS_OPEN_FAIL);
	return 2;
}
