#include "stdafx.h"
#include "SevenZipDec.h"
#include "OxzFilt.h"
#include "lzma_sdk/7zAlloc.h"
#include "lzma_sdk/7zCrc.h"
#include <stdlib.h>
#include <string.h>
#if defined(_M_X64)
#include "OxzCore/OxzCore.h"
#include <map>
#include <string>
#include <vector>
static int OxzPkgFile(const wchar_t* path);
static int OpenOxzPkg(SevenZipArc* a, const wchar_t* path);
static int ExtractOxzPkg(SevenZipArc* a, UInt32 i, const Byte** data, size_t* size);
static void FreeOxzPkg(SevenZipArc* a);
#endif

#define kInputBufSize ((size_t)1 << 18)

int SevenZipPathIs7zW(const wchar_t* path)
{
	if (!path || !path[0])
		return 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		unsigned char sig[8] = {};
		DWORD rd = 0;
		const BOOL ok = ReadFile(h, sig, 8, &rd, NULL);
		CloseHandle(h);
		if (ok && rd >= 8 && sig[0] == 'O' && sig[1] == 'X' && sig[2] == 'Z' && sig[3] == 'P' && sig[4] == 'K' && sig[5] == 'G' && sig[6] == 0 && sig[7] == 0)
			return 1;
		if (ok && rd >= 6
			&& sig[0] == k7zSignature[0] && sig[1] == k7zSignature[1]
			&& sig[2] == k7zSignature[2] && sig[3] == k7zSignature[3]
			&& sig[4] == k7zSignature[4] && sig[5] == k7zSignature[5])
			return 1;
		if (ok && rd >= 2 && sig[0] == 'P' && sig[1] == 'K')
			return 0;
	}
	const size_t n = wcslen(path);
	return (n >= 3 && _wcsicmp(path + n - 3, L".7z") == 0) ? 1 : 0;
}

int SevenZipOpenW(SevenZipArc* a, const wchar_t* path)
{
	if (!a || !path || !path[0])
		return 0;
	memset(a, 0, sizeof(*a));
	File_Construct(&a->archiveStream.file);
	a->allocImp.Alloc = SzAlloc;
	a->allocImp.Free = SzFree;
	a->allocTempImp.Alloc = SzAllocTemp;
	a->allocTempImp.Free = SzFreeTemp;
	a->blockIndex = 0xFFFFFFFF;
#if defined(_M_X64)
	if (OxzPkgFile(path))
		return OpenOxzPkg(a, path);
#endif
	FileInStream_CreateVTable(&a->archiveStream);
	a->archiveStream.wres = 0;
	if (InFile_OpenW(&a->archiveStream.file, path) != 0)
		return 0;
	LookToRead2_CreateVTable(&a->lookStream, False);
	a->lookStream.buf = (Byte*)ISzAlloc_Alloc(&a->allocImp, kInputBufSize);
	if (!a->lookStream.buf) {
		File_Close(&a->archiveStream.file);
		return 0;
	}
	a->lookStream.bufSize = kInputBufSize;
	a->lookStream.realStream = &a->archiveStream.vt;
	LookToRead2_INIT(&a->lookStream);
	CrcGenerateTable();
	SzArEx_Init(&a->db);
	if (SzArEx_Open(&a->db, &a->lookStream.vt, &a->allocImp, &a->allocTempImp) != SZ_OK) {
		SevenZipClose(a);
		return 0;
	}
	{
		const size_t n = wcslen(path);
		a->ripPe = (n >= 4 && _wcsicmp(path + n - 4, L".oxz") == 0) ? 1 : 0;
	}
	a->opened = 1;
	return 1;
}

void SevenZipClose(SevenZipArc* a)
{
	if (!a)
		return;
#if defined(_M_X64)
	FreeOxzPkg(a);
#endif
	if (a->outBuffer) {
		ISzAlloc_Free(&a->allocImp, a->outBuffer);
		a->outBuffer = NULL;
		a->outBufferSize = 0;
	}
	free(a->ripCopy);
	a->ripCopy = NULL;
	a->ripCopyCap = 0;
	SzArEx_Free(&a->db, &a->allocImp);
	if (a->lookStream.buf) {
		ISzAlloc_Free(&a->allocImp, a->lookStream.buf);
		a->lookStream.buf = NULL;
	}
	File_Close(&a->archiveStream.file);
	a->opened = 0;
}

int SevenZipExtractIndex(SevenZipArc* a, UInt32 i, const Byte** data, size_t* size)
{
	if (data)
		*data = NULL;
	if (size)
		*size = 0;
	if (!a || !a->opened || i >= a->db.NumFiles)
		return 0;
	if (SzArEx_IsDir(&a->db, i))
		return 0;
#if defined(_M_X64)
	if (a->oxzPkg)
		return ExtractOxzPkg(a, i, data, size);
#endif
	size_t offset = 0;
	size_t outSizeProcessed = 0;
	if (SzArEx_Extract(&a->db, &a->lookStream.vt, i,
		&a->blockIndex, &a->outBuffer, &a->outBufferSize,
		&offset, &outSizeProcessed, &a->allocImp, &a->allocTempImp) != SZ_OK)
		return 0;
	if (a->ripPe && outSizeProcessed >= 2
		&& a->outBuffer[offset] == 'M' && a->outBuffer[offset + 1] == 'Z') {
		if (a->ripCopyCap < outSizeProcessed) {
			Byte* nbuf = (Byte*)realloc(a->ripCopy, outSizeProcessed);
			if (!nbuf) return 0;
			a->ripCopy = nbuf;
			a->ripCopyCap = outSizeProcessed;
		}
		memcpy(a->ripCopy, a->outBuffer + offset, outSizeProcessed);
		OxzRipConvert(a->ripCopy, outSizeProcessed, 0);
		if (data)
			*data = a->ripCopy;
	} else if (data) {
		*data = a->outBuffer + offset;
	}
	if (size)
		*size = outSizeProcessed;
	return 1;
}

int SevenZipMTimeUtcFileTime(const SevenZipArc* a, UInt32 i, FILETIME* outUtc)
{
	if (outUtc) {
		outUtc->dwLowDateTime = 0;
		outUtc->dwHighDateTime = 0;
	}
	if (!a || !a->opened || i >= a->db.NumFiles || !outUtc)
		return 0;
	if (!SzBitWithVals_Check(&a->db.MTime, i))
		return 0;
	const CNtfsFileTime nt = a->db.MTime.Vals[i];
	ULARGE_INTEGER ull;
	ull.LowPart = nt.Low;
	ull.HighPart = nt.High;
	if (ull.QuadPart < 116444736000000000ULL)
		return 0;
	outUtc->dwLowDateTime = nt.Low;
	outUtc->dwHighDateTime = nt.High;
	return 1;
}

time_t SevenZipMTimeUtc(const SevenZipArc* a, UInt32 i)
{
	FILETIME ft = {};
	if (!SevenZipMTimeUtcFileTime(a, i, &ft))
		return 0;
	ULARGE_INTEGER ull;
	ull.LowPart = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;
	return (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

int SevenZipFileNameW(const SevenZipArc* a, UInt32 i, wchar_t* name, int nameChars)
{
	if (!name || nameChars <= 0)
		return 0;
	name[0] = 0;
	if (!a || !a->opened || i >= a->db.NumFiles)
		return 0;
	const size_t len = SzArEx_GetFileNameUtf16(&a->db, i, NULL);
	if (len == 0 || len > (size_t)nameChars)
		return 0;
	SzArEx_GetFileNameUtf16(&a->db, i, (UInt16*)name);
	for (wchar_t* p = name; *p; ++p) {
		if (*p == L'/')
			*p = L'\\';
	}
	return 1;
}

#if defined(_M_X64)

struct OxzMerged {
	wchar_t* arc;
	wchar_t temp[MAX_PATH];
	SevenZipArc* base;
	int* kind;
	int* src;
};

static int OxzPkgFile(const wchar_t* path)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	unsigned char b[8];
	DWORD rd = 0;
	int ok;
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	ok = ReadFile(h, b, 8, &rd, NULL) && rd == 8
		&& b[0] == 'O' && b[1] == 'X' && b[2] == 'Z' && b[3] == 'P' && b[4] == 'K' && b[5] == 'G' && b[6] == 0 && b[7] == 0;
	CloseHandle(h);
	return ok;
}

static void FreeOxzPkg(SevenZipArc* a)
{
	OxzMerged* m;
	if (!a || !a->oxzPkg)
		return;
	m = (OxzMerged*)a->oxzPkg;
	a->oxzPkg = NULL;
	if (m->base) {
		SevenZipClose(m->base);
		free(m->base);
	}
	if (m->temp[0])
		DeleteFileW(m->temp);
	free(m->arc);
	free(m->kind);
	free(m->src);
	free(m);
}

static void OxzSetBit(Byte* p, UInt32 i)
{
	p[i >> 3] |= (Byte)(0x80u >> (i & 7));
}

static int OxzHold(SevenZipArc* a, const void* p, size_t n, const Byte** data, size_t* size)
{
	size_t cap = n ? n : 1;
	if (a->outBufferSize < cap) {
		Byte* b = (Byte*)ISzAlloc_Alloc(&a->allocImp, cap);
		if (!b)
			return 0;
		ISzAlloc_Free(&a->allocImp, a->outBuffer);
		a->outBuffer = b;
		a->outBufferSize = cap;
	}
	if (n && p)
		memcpy(a->outBuffer, p, n);
	if (data)
		*data = a->outBuffer;
	if (size)
		*size = n;
	return 1;
}

static int ExtractOxzPkg(SevenZipArc* a, UInt32 i, const Byte** data, size_t* size)
{
	OxzMerged* m = (OxzMerged*)a->oxzPkg;
	if (!m || !m->kind || i >= a->db.NumFiles)
		return 0;
	if (m->kind[i] == 1) {
		void* p = NULL;
		size_t n = 0;
		int ok;
		if (!m->arc || m->src[i] < 0)
			return 0;
		if (OxzCoreReadOverlay(m->arc, m->src[i], &p, &n) != OXZ_CORE_OK)
			return 0;
		ok = OxzHold(a, p, n, data, size);
		OxzCoreFree(p);
		return ok;
	}
	if (m->kind[i] == 0 && m->base && m->src[i] >= 0) {
		const Byte* p = NULL;
		size_t n = 0;
		if (!SevenZipExtractIndex(m->base, (UInt32)m->src[i], &p, &n))
			return 0;
		return OxzHold(a, p, n, data, size);
	}
	return 0;
}

struct OxzLess {
	bool operator()(const std::wstring& a, const std::wstring& b) const
	{
		return _wcsicmp(a.c_str(), b.c_str()) < 0;
	}
};

struct OxzRow {
	std::wstring name;
	int isDir;
	int kind;
	int src;
	uint64_t size;
	uint64_t mtime;
};

static int WriteSlice(const wchar_t* src, uint64_t off, uint64_t n, const wchar_t* dst)
{
	HANDLE in = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	HANDLE out;
	std::vector<Byte> buf;
	LARGE_INTEGER li;
	if (in == INVALID_HANDLE_VALUE)
		return 0;
	out = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (out == INVALID_HANDLE_VALUE) {
		CloseHandle(in);
		return 0;
	}
	li.QuadPart = (LONGLONG)off;
	if (!SetFilePointerEx(in, li, NULL, FILE_BEGIN)) {
		CloseHandle(in);
		CloseHandle(out);
		return 0;
	}
	try { buf.resize(1u << 20); }
	catch (...) {
		CloseHandle(in);
		CloseHandle(out);
		return 0;
	}
	while (n) {
		DWORD chunk = n > buf.size() ? (DWORD)buf.size() : (DWORD)n;
		DWORD rd = 0, wr = 0;
		if (!ReadFile(in, buf.data(), chunk, &rd, NULL) || rd != chunk || !WriteFile(out, buf.data(), rd, &wr, NULL) || wr != rd) {
			CloseHandle(in);
			CloseHandle(out);
			return 0;
		}
		n -= rd;
	}
	CloseHandle(in);
	CloseHandle(out);
	return 1;
}

static int OpenOxzPkg(SevenZipArc* a, const wchar_t* path)
{
	OxzMerged* m = (OxzMerged*)calloc(1, sizeof(OxzMerged));
	OxzCoreItem* items = NULL;
	int count = 0, k;
	uint64_t baseOff = 0, baseSize = 0;
	std::map<std::wstring, OxzRow, OxzLess> rows;
	std::vector<OxzRow> list;
	std::vector<Byte> blob;
	std::vector<size_t> offs;
	UInt32 n, i;
	wchar_t tmpDir[MAX_PATH];
	if (!m)
		return 0;
	a->oxzPkg = m;
	SzArEx_Init(&a->db);
	m->arc = _wcsdup(path);
	if (!m->arc) {
		SevenZipClose(a);
		return 0;
	}
	if (OxzCoreReadDir(path, &items, &count, &baseOff, &baseSize) != OXZ_CORE_OK) {
		SevenZipClose(a);
		return 0;
	}
	if (baseSize) {
		if (!GetTempPathW(MAX_PATH, tmpDir)) {
			OxzCoreFreeItems(items, count);
			SevenZipClose(a);
			return 0;
		}
		_snwprintf_s(m->temp, _TRUNCATE, L"%soxzk%08X.oxz", tmpDir, GetTickCount());
		if (!WriteSlice(path, baseOff, baseSize, m->temp)) {
			OxzCoreFreeItems(items, count);
			SevenZipClose(a);
			return 0;
		}
		m->base = (SevenZipArc*)calloc(1, sizeof(SevenZipArc));
		if (!m->base || !SevenZipOpenW(m->base, m->temp)) {
			free(m->base);
			m->base = NULL;
			OxzCoreFreeItems(items, count);
			SevenZipClose(a);
			return 0;
		}
		for (i = 0; i < m->base->db.NumFiles; ++i) {
			wchar_t name[2048];
			OxzRow r;
			FILETIME ft;
			if (!SevenZipFileNameW(m->base, i, name, 2048) || !name[0])
				continue;
			r.name = name;
			r.isDir = SzArEx_IsDir(&m->base->db, i) ? 1 : 0;
			r.kind = r.isDir ? 2 : 0;
			r.src = (int)i;
			r.size = r.isDir ? 0 : (uint64_t)SzArEx_GetFileSize(&m->base->db, i);
			r.mtime = 0;
			if (SevenZipMTimeUtcFileTime(m->base, i, &ft))
				r.mtime = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
			rows[r.name] = r;
		}
	}
	for (k = 0; k < count; ++k) {
		if (items[k].tomb && items[k].name)
			rows.erase(items[k].name);
	}
	for (k = 0; k < count; ++k) {
		OxzRow r;
		if (!items[k].name || items[k].tomb)
			continue;
		if (items[k].isDir) {
			if (rows.find(items[k].name) != rows.end())
				continue;
			r.name = items[k].name;
			r.isDir = 1;
			r.kind = 2;
			r.src = -1;
			r.size = 0;
			r.mtime = items[k].mtime;
			rows[r.name] = r;
			continue;
		}
		if (items[k].overlay < 0)
			continue;
		r.name = items[k].name;
		r.isDir = 0;
		r.kind = 1;
		r.src = items[k].overlay;
		r.size = items[k].unpacked;
		r.mtime = items[k].mtime;
		rows[r.name] = r;
	}
	OxzCoreFreeItems(items, count);
	for (std::map<std::wstring, OxzRow, OxzLess>::iterator it = rows.begin(); it != rows.end(); ++it)
		list.push_back(it->second);
	n = (UInt32)list.size();
	a->db.NumFiles = n;
	if (n) {
		a->db.UnpackPositions = (UInt64*)ISzAlloc_Alloc(&a->allocImp, (size_t)(n + 1) * sizeof(UInt64));
		a->db.IsDirs = (Byte*)ISzAlloc_Alloc(&a->allocImp, (size_t)(n + 7) / 8);
		a->db.FileNameOffsets = (size_t*)ISzAlloc_Alloc(&a->allocImp, (size_t)(n + 1) * sizeof(size_t));
		a->db.MTime.Defs = (Byte*)ISzAlloc_Alloc(&a->allocImp, (size_t)(n + 7) / 8);
		a->db.MTime.Vals = (CNtfsFileTime*)ISzAlloc_Alloc(&a->allocImp, (size_t)n * sizeof(CNtfsFileTime));
		m->kind = (int*)calloc(n, sizeof(int));
		m->src = (int*)calloc(n, sizeof(int));
		if (!a->db.UnpackPositions || !a->db.IsDirs || !a->db.FileNameOffsets || !a->db.MTime.Defs || !a->db.MTime.Vals || !m->kind || !m->src) {
			SevenZipClose(a);
			return 0;
		}
		memset(a->db.IsDirs, 0, (size_t)(n + 7) / 8);
		memset(a->db.MTime.Defs, 0, (size_t)(n + 7) / 8);
		memset(a->db.MTime.Vals, 0, (size_t)n * sizeof(CNtfsFileTime));
		a->db.UnpackPositions[0] = 0;
		offs.push_back(0);
		for (i = 0; i < n; ++i) {
			const OxzRow& r = list[i];
			size_t chars = r.name.size() + 1;
			const wchar_t* s = r.name.c_str();
			a->db.UnpackPositions[i + 1] = a->db.UnpackPositions[i] + (r.isDir ? 0 : r.size);
			if (r.isDir)
				OxzSetBit(a->db.IsDirs, i);
			if (r.mtime) {
				OxzSetBit(a->db.MTime.Defs, i);
				a->db.MTime.Vals[i].Low = (UInt32)r.mtime;
				a->db.MTime.Vals[i].High = (UInt32)(r.mtime >> 32);
			}
			m->kind[i] = r.kind;
			m->src[i] = r.src;
			blob.insert(blob.end(), (const Byte*)s, (const Byte*)s + chars * sizeof(wchar_t));
			offs.push_back(offs.back() + chars);
		}
		a->db.FileNames = (Byte*)ISzAlloc_Alloc(&a->allocImp, blob.empty() ? 1 : blob.size());
		if (!a->db.FileNames) {
			SevenZipClose(a);
			return 0;
		}
		if (!blob.empty())
			memcpy(a->db.FileNames, &blob[0], blob.size());
		for (i = 0; i <= n; ++i)
			a->db.FileNameOffsets[i] = offs[i];
	}
	a->ripPe = 0;
	a->opened = 1;
	return 1;
}

#endif
