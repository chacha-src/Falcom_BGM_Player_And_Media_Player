#include "stdafx.h"
#include "SevenZipDec.h"
#include "lzma_sdk/7zAlloc.h"
#include "lzma_sdk/7zCrc.h"

#define kInputBufSize ((size_t)1 << 18)

int SevenZipPathIs7zW(const wchar_t* path)
{
	if (!path || !path[0])
		return 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h != INVALID_HANDLE_VALUE) {
		unsigned char sig[6] = {};
		DWORD rd = 0;
		const BOOL ok = ReadFile(h, sig, 6, &rd, NULL);
		CloseHandle(h);
		if (ok && rd == 6
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
	a->opened = 1;
	return 1;
}

void SevenZipClose(SevenZipArc* a)
{
	if (!a)
		return;
	if (a->outBuffer) {
		ISzAlloc_Free(&a->allocImp, a->outBuffer);
		a->outBuffer = NULL;
		a->outBufferSize = 0;
	}
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
	size_t offset = 0;
	size_t outSizeProcessed = 0;
	if (SzArEx_Extract(&a->db, &a->lookStream.vt, i,
		&a->blockIndex, &a->outBuffer, &a->outBufferSize,
		&offset, &outSizeProcessed, &a->allocImp, &a->allocTempImp) != SZ_OK)
		return 0;
	if (data)
		*data = a->outBuffer + offset;
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
