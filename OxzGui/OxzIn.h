#pragma once
#include "OxzArc.h"

enum {
	EK_7Z = 1, EK_ZIP = 2, EK_GZ = 3, EK_TAR = 4, EK_RAR = 5, EK_ZST = 6, EK_BZ = 7, EK_XZ = 8,
	EK_CAB = 9, EK_ISO = 10, EK_WIM = 11, EK_LZH = 12, EK_ARJ = 13,
	EK_GZTAR = 14, EK_BZTAR = 15, EK_ZSTTAR = 16, EK_XZTAR = 17
};

int OxzEndsI(const wchar_t* path, const wchar_t* ext);
int OxzNameIsTar(const wchar_t* path);
int OxzSafeRelA(const wchar_t* name, std::wstring& rel);
void OxzEnsureParentA(const std::wstring& path);
HANDLE OxzOpenReadA(const wchar_t* path);
int OxzReadExactA(HANDLE h, void* p, DWORD n);
int OxzSeekA(HANDLE h, uint64_t off);
uint64_t OxzLenA(HANDLE h);
std::wstring OxzFromA(const char* s, int n, UINT cp);
uint64_t OxzDosFt(uint16_t date, uint16_t time);
void OxzAddDirsA(std::vector<OxzEnt>& v);
void OxzNoteA(int i, int n, const wchar_t* name, int base, int span);
void OxzPulseSize(uint64_t n);
void OxzPulseAdd(uint64_t n);
void OxzPulseReset();
HANDLE OxzCreateA(const std::wstring& path);
int OxzWriteA(HANDLE h, const void* p, DWORD n, uint32_t* crc, uint64_t* total);
void OxzStampA(HANDLE h, uint64_t ft);
uint32_t OxzCrcAdd(uint32_t c, const void* p, size_t n);
uint32_t OxzCrcFile(const std::wstring& path, int* ok);

int OxzListRar(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err);
int OxzTakeRar(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span);
int OxzListCab(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err);
int OxzTakeCab(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span);
int OxzListLzh(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err);
int OxzTakeLzh(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span);
int OxzListIso(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err);
int OxzTakeIso(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span);
int OxzListArj(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err);
int OxzTakeArj(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span);
int OxzWimList(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err);
int OxzWimTake(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span);
