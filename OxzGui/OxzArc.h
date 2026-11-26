#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#ifndef _WINDOWS_
#include <windows.h>
#endif

/* 0 none, 1 7z/oxz, 2 zip, 3 gzip, 4 tar, 5 rar, 6 zstd, 7 bzip2, 8 xz, 9 cab, 10 iso, 11 wim, 12 lzh, 13 arj */
int OxzArchiveKind(const wchar_t* path);
int OxzExtractNative(const wchar_t* arc, const wchar_t* dest, int kind, int rip, std::wstring& err, int base, int span);

struct OxzSpan {
	uint64_t off;
	uint64_t n;
};

struct OxzEnt {
	std::wstring rel;
	std::wstring origin;
	std::wstring codec;
	int dir;
	int index;
	int kind;
	int packedKnown;
	int crcBits;
	uint32_t crc;
	uint64_t size;
	uint64_t packed;
	uint64_t offset;
	uint64_t mtime;
	std::vector<OxzSpan> spans;
};

struct OxzTestRow {
	std::wstring name;
	int ok;
	std::wstring detail;
};

int OxzListArchive(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err);
int OxzExtractInProc(const wchar_t* arc, const wchar_t* dest, std::wstring& err, int base, int span);
int OxzMaterialize(const wchar_t* arc, const wchar_t* work, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span);
int OxzTestArchive(const wchar_t* arc, std::vector<OxzTestRow>& rows, std::wstring& err, int base, int span);
int OxzWriteZip(const std::wstring& work, const std::wstring& archive, unsigned long long& bytes, std::wstring& err, int base, int span);
int OxzWriteTar(const std::wstring& work, const std::wstring& archive, unsigned long long& bytes, std::wstring& err, int base, int span);
/* One uncompressed file, written as an lz4 frame. Returns 3 when the folder is not a single file. */
int OxzWriteOneLz4(const std::wstring& work, const std::wstring& archive, unsigned long long& bytes, std::wstring& err, int base, int span);
/* 0 plain tar, 1 gz, 2 bz2, 3 xz, 4 zst, 5 lz4, -1 otherwise */
int OxzTarWrap(const wchar_t* path);
void OxzApplyTarExt(std::wstring& path, int wrap);

struct OxzMapNode {
	std::wstring name;
	unsigned long long size;
	unsigned long long packed;
};
struct OxzMapLink {
	int from;
	int to;
	unsigned long long packed;
};
void OxzShowMap(HWND owner, HFONT font, const wchar_t* title, unsigned long long size, unsigned long long packed, int known, const std::vector<OxzMapNode>& folders, const std::vector<OxzMapNode>& codecs, const std::vector<OxzMapLink>& links);
void OxzShowTest(HWND owner, HFONT font, const wchar_t* title, const std::vector<OxzTestRow>& rows);
