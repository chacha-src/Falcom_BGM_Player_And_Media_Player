#pragma once

#include <time.h>
#include "lzma_sdk/7z.h"
#include "lzma_sdk/7zFile.h"

/* LZMA SDK C decoder (public domain). 7z を開いてメンバをメモリへ出す。 */
struct SevenZipArc {
	CFileInStream archiveStream;
	CLookToRead2 lookStream;
	CSzArEx db;
	ISzAlloc allocImp;
	ISzAlloc allocTempImp;
	UInt32 blockIndex;
	Byte* outBuffer;
	size_t outBufferSize;
	int opened;
};

int SevenZipPathIs7zW(const wchar_t* path);
int SevenZipOpenW(SevenZipArc* a, const wchar_t* path);
void SevenZipClose(SevenZipArc* a);
int SevenZipExtractIndex(SevenZipArc* a, UInt32 i, const Byte** data, size_t* size);
time_t SevenZipMTimeUtc(const SevenZipArc* a, UInt32 i);
int SevenZipMTimeUtcFileTime(const SevenZipArc* a, UInt32 i, FILETIME* outUtc);
int SevenZipFileNameW(const SevenZipArc* a, UInt32 i, wchar_t* name, int nameChars);
