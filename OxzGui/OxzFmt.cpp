/* UnRAR 7.0.9 sources are compiled into this program only to open RAR archives.
   This program does not create RAR archives.
   UnRAR source code may be used in any software to handle
   RAR archives without limitations free of charge, but cannot be
   used to develop RAR (WinRAR) compatible archiver and to
   re-create RAR compression algorithm, which is proprietary.
   Distribution of modified UnRAR source code in separate form
   or as a part of other software is permitted, provided that
   full text of this paragraph, starting from "UnRAR source code"
   words, is included in license, or in documentation if license
   is not available, and in source code comments of resulting package. */
#include "OxzIn.h"
#include "OxzLang.h"
#include "../../kpi_sources_20240712/kpi_unarc/kbunarc/source/unrar/unrarsrc/dll.hpp"
#include "../../kpi_sources_20240712/kpi_unarc/kbunarc/source/unlha/unlzh.h"
#include <fdi.h>
#include <string.h>
#include <stdlib.h>

#pragma comment(lib, "cabinet.lib")

static int CALLBACK RarCb(UINT msg, LPARAM, LPARAM p2, LPARAM)
{
	if (msg == UCM_LARGEDICT) return 1;
	if (msg == UCM_PROCESSDATA) { OxzPulseAdd((uint64_t)(size_t)p2); return 1; }
	return 0;
}

static std::wstring RarName(RARHeaderDataEx& hd, wchar_t* extra)
{
	if (extra && extra[0]) return extra;
	if (hd.FileNameW[0]) return hd.FileNameW;
	return OxzFromA(hd.FileName, -1, CP_ACP);
}

int OxzListRar(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err)
{
	RAROpenArchiveDataEx od;
	RARHeaderDataEx hd;
	wchar_t extra[32768];
	HANDLE h;
	int idx = 0;
	memset(&od, 0, sizeof(od));
	od.ArcNameW = (wchar_t*)path;
	od.OpenMode = RAR_OM_LIST;
	od.Callback = RarCb;
	h = RAROpenArchiveEx(&od);
	if (!h || od.OpenResult != 0) {
		if (h) RARCloseArchive(h);
		err = (od.OpenResult == ERAR_MISSING_PASSWORD || od.OpenResult == ERAR_BAD_PASSWORD) ? OxzTxt(OS_NEED_PW) : OxzTxt(OS_OPEN_FAIL);
		return 2;
	}
	for (;;) {
		int rc;
		std::wstring name, rel;
		OxzEnt e;
		memset(&hd, 0, sizeof(hd));
		extra[0] = 0;
		hd.FileNameEx = extra;
		hd.FileNameExSize = 32768;
		rc = RARReadHeaderEx(h, &hd);
		if (rc == ERAR_END_ARCHIVE) break;
		if (rc == ERAR_MISSING_PASSWORD || rc == ERAR_BAD_PASSWORD) { RARCloseArchive(h); err = OxzTxt(OS_NEED_PW); return 2; }
		if (rc != 0) { RARCloseArchive(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		name = RarName(hd, extra);
		for (size_t i = 0; i < name.size(); ++i) if (name[i] == L'/') name[i] = L'\\';
		e.dir = (hd.Flags & RHDF_DIRECTORY) || (!name.empty() && (name.back() == L'\\' || name.back() == L'/')) ? 1 : 0;
		if (!OxzSafeRelA(name.c_str(), rel)) {
			if (RARProcessFileW(h, RAR_SKIP, NULL, NULL) != 0) { RARCloseArchive(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			continue;
		}
		e.rel = rel;
		e.origin = rel;
		e.index = idx++;
		e.kind = EK_RAR;
		e.size = (uint64_t)hd.UnpSize | ((uint64_t)hd.UnpSizeHigh << 32);
		e.packed = (uint64_t)hd.PackSize | ((uint64_t)hd.PackSizeHigh << 32);
		e.packedKnown = (hd.Flags & RHDF_SOLID) ? 0 : 1;
		e.crcBits = (hd.HashType == RAR_HASH_CRC32 || hd.HashType == RAR_HASH_NONE) ? 32 : 0;
		e.crc = hd.FileCRC;
		if (hd.MtimeLow || hd.MtimeHigh) e.mtime = (uint64_t)hd.MtimeLow | ((uint64_t)hd.MtimeHigh << 32);
		else e.mtime = OxzDosFt((uint16_t)(hd.FileTime >> 16), (uint16_t)hd.FileTime);
		if (hd.Method == 0x30) e.codec = L"Store";
		else if (hd.Method >= 0x31 && hd.Method <= 0x35) { wchar_t b[16]; _snwprintf_s(b, _TRUNCATE, L"RAR%u", hd.Method - 0x30); e.codec = b; }
		else e.codec = L"RAR";
		out.push_back(e);
		rc = RARProcessFileW(h, RAR_SKIP, NULL, NULL);
		if (rc == ERAR_MISSING_PASSWORD || rc == ERAR_BAD_PASSWORD) { RARCloseArchive(h); err = OxzTxt(OS_NEED_PW); return 2; }
		if (rc != 0) { RARCloseArchive(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	}
	RARCloseArchive(h);
	return 0;
}

int OxzTakeRar(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span)
{
	RAROpenArchiveDataEx od;
	RARHeaderDataEx hd;
	wchar_t extra[32768];
	HANDLE h;
	int nfile = 0, done = 0;
	for (size_t i = 0; i < ents.size(); ++i) if (!ents[i].dir) nfile++;
	if (nfile < 1) nfile = 1;
	memset(&od, 0, sizeof(od));
	od.ArcNameW = (wchar_t*)arc;
	od.OpenMode = RAR_OM_EXTRACT;
	od.Callback = RarCb;
	h = RAROpenArchiveEx(&od);
	if (!h || od.OpenResult != 0) {
		if (h) RARCloseArchive(h);
		err = (od.OpenResult == ERAR_MISSING_PASSWORD) ? OxzTxt(OS_NEED_PW) : OxzTxt(OS_OPEN_FAIL);
		return 2;
	}
	CreateDirectoryW(dest, NULL);
	for (size_t i = 0; i < ents.size(); ++i) if (ents[i].dir) OxzEnsureParentA(std::wstring(dest) + L"\\" + ents[i].rel + L"\\x");
	for (;;) {
		int rc;
		std::wstring name, rel, full;
		const OxzEnt* hit = NULL;
		memset(&hd, 0, sizeof(hd));
		extra[0] = 0;
		hd.FileNameEx = extra;
		hd.FileNameExSize = 32768;
		rc = RARReadHeaderEx(h, &hd);
		if (rc == ERAR_END_ARCHIVE) break;
		if (rc != 0) { RARCloseArchive(h); err = (rc == ERAR_MISSING_PASSWORD || rc == ERAR_BAD_PASSWORD) ? OxzTxt(OS_NEED_PW) : OxzTxt(OS_OPEN_FAIL); return 2; }
		name = RarName(hd, extra);
		for (size_t i = 0; i < name.size(); ++i) if (name[i] == L'/') name[i] = L'\\';
		if (!OxzSafeRelA(name.c_str(), rel)) { RARProcessFileW(h, RAR_SKIP, NULL, NULL); continue; }
		for (size_t i = 0; i < ents.size(); ++i)
			if (!ents[i].dir && _wcsicmp(ents[i].origin.c_str(), rel.c_str()) == 0) { hit = &ents[i]; break; }
		if (!hit || (hd.Flags & RHDF_DIRECTORY)) {
			if (RARProcessFileW(h, RAR_SKIP, NULL, NULL) != 0) { RARCloseArchive(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			continue;
		}
		full = std::wstring(dest) + L"\\" + hit->rel;
		OxzEnsureParentA(full);
		OxzNoteA(done, nfile, hit->rel.c_str(), base, span);
		OxzPulseSize(((uint64_t)hd.UnpSizeHigh << 32) | hd.UnpSize);
		done++;
		rc = RARProcessFileW(h, RAR_EXTRACT, NULL, (wchar_t*)full.c_str());
		if (rc != 0) { RARCloseArchive(h); err = (rc == ERAR_MISSING_PASSWORD || rc == ERAR_BAD_PASSWORD) ? OxzTxt(OS_NEED_PW) : OxzTxt(OS_OPEN_FAIL); return 2; }
	}
	RARCloseArchive(h);
	return 0;
}

static uint16_t Ru16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t Ru32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int OxzListCab(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err)
{
	HANDLE h = OxzOpenReadA(path);
	unsigned char hd[64];
	uint32_t coffFiles, reserved = 0;
	uint16_t folders, files, flags, cbHead = 0, cbFolder = 0;
	uint64_t folderAt;
	std::vector<uint16_t> comp;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (!OxzReadExactA(h, hd, 36) || memcmp(hd, "MSCF", 4) != 0) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	coffFiles = Ru32(hd + 16);
	folders = Ru16(hd + 26);
	files = Ru16(hd + 28);
	flags = Ru16(hd + 30);
	folderAt = 36;
	if (flags & 4) {
		unsigned char rz[4];
		if (!OxzReadExactA(h, rz, 4)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		cbHead = Ru16(rz);
		cbFolder = Ru16(rz + 2);
		folderAt = 40 + cbHead;
		if (cbHead && !OxzSeekA(h, folderAt)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	}
	comp.resize(folders ? folders : 1);
	for (uint16_t i = 0; i < folders; ++i) {
		unsigned char fb[8];
		if (!OxzSeekA(h, folderAt + (uint64_t)i * (8 + cbFolder)) || !OxzReadExactA(h, fb, 8)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		comp[i] = Ru16(fb + 6);
	}
	if (!OxzSeekA(h, coffFiles)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	for (uint16_t i = 0; i < files; ++i) {
		unsigned char fh[16];
		OxzEnt e;
		std::string raw;
		std::wstring name, rel;
		uint16_t folder, attr;
		int utf;
		char ch;
		DWORD got = 0;
		if (!OxzReadExactA(h, fh, 16)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		attr = Ru16(fh + 14);
		folder = Ru16(fh + 10);
		utf = (attr & 0x80) ? 1 : 0;
		for (;;) {
			if (!ReadFile(h, &ch, 1, &got, NULL) || got != 1) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			if (!ch) break;
			raw.push_back(ch);
		}
		name = OxzFromA(raw.c_str(), (int)raw.size(), utf ? CP_UTF8 : CP_ACP);
		for (size_t k = 0; k < name.size(); ++k) if (name[k] == L'/') name[k] = L'\\';
		if (!OxzSafeRelA(name.c_str(), rel)) continue;
		e.rel = rel;
		e.origin = rel;
		e.index = (int)i;
		e.kind = EK_CAB;
		e.dir = (attr & 0x10) ? 1 : 0;
		e.size = Ru32(fh);
		e.packed = 0;
		e.packedKnown = 0;
		e.crcBits = 0;
		e.crc = 0;
		e.offset = Ru32(fh + 4);
		e.mtime = OxzDosFt(Ru16(fh + 12), Ru16(fh + 8));
		{
			uint16_t c = (folder < comp.size()) ? (uint16_t)(comp[folder] & 0x000f) : 0;
			if (c == 0) e.codec = L"Store";
			else if (c == 1) e.codec = L"MSZIP";
			else if (c == 2) e.codec = L"Quantum";
			else if (c == 3) e.codec = L"LZX";
			else e.codec = L"CAB";
		}
		(void)reserved;
		out.push_back(e);
	}
	CloseHandle(h);
	return 0;
}

struct CabJob {
	const wchar_t* arc;
	const wchar_t* dest;
	const std::vector<OxzEnt>* ents;
	std::wstring err;
	int bad;
	int done;
	int total;
};

static thread_local CabJob* tCab;

static void HUGE * DIAMONDAPI CabAlloc(ULONG cb) { return malloc(cb ? cb : 1); }
static void DIAMONDAPI CabFree(void HUGE * p) { free(p); }
static INT_PTR DIAMONDAPI CabOpen(char FAR *, int, int)
{
	HANDLE h = OxzOpenReadA(tCab->arc);
	if (h == INVALID_HANDLE_VALUE) return -1;
	return (INT_PTR)h;
}
static UINT DIAMONDAPI CabRead(INT_PTR hf, void FAR * pv, UINT cb)
{
	DWORD g = 0;
	if (!ReadFile((HANDLE)hf, pv, cb, &g, NULL)) return 0;
	return g;
}
static UINT DIAMONDAPI CabWrite(INT_PTR hf, void FAR * pv, UINT cb)
{
	DWORD g = 0;
	if (!WriteFile((HANDLE)hf, pv, cb, &g, NULL)) return 0;
	return g;
}
static int DIAMONDAPI CabClose(INT_PTR hf) { CloseHandle((HANDLE)hf); return 0; }
static long DIAMONDAPI CabSeek(INT_PTR hf, long dist, int seektype)
{
	DWORD m = seektype == 0 ? FILE_BEGIN : (seektype == 1 ? FILE_CURRENT : FILE_END);
	DWORD n = SetFilePointer((HANDLE)hf, dist, NULL, m);
	return n == INVALID_SET_FILE_POINTER ? -1 : (long)n;
}
static INT_PTR DIAMONDAPI CabNotify(FDINOTIFICATIONTYPE type, PFDINOTIFICATION p)
{
	if (type == fdintCABINET_INFO || type == fdintPARTIAL_FILE || type == fdintENUMERATE) return 0;
	if (type == fdintNEXT_CABINET) { tCab->bad = 1; tCab->err = OxzTxt(OS_OPEN_FAIL); return -1; }
	if (type == fdintCOPY_FILE) {
		std::wstring name = OxzFromA(p->psz1, -1, CP_ACP);
		std::wstring rel, full;
		const OxzEnt* hit = NULL;
		for (size_t i = 0; i < name.size(); ++i) if (name[i] == L'/') name[i] = L'\\';
		if (!OxzSafeRelA(name.c_str(), rel)) return 0;
		for (size_t i = 0; i < tCab->ents->size(); ++i)
			if (!(*tCab->ents)[i].dir && _wcsicmp((*tCab->ents)[i].origin.c_str(), rel.c_str()) == 0) { hit = &(*tCab->ents)[i]; break; }
		if (!hit) return 0;
		full = std::wstring(tCab->dest) + L"\\" + hit->rel;
		OxzEnsureParentA(full);
		OxzNoteA(tCab->done, tCab->total, hit->rel.c_str(), 0, 100);
		tCab->done++;
		return (INT_PTR)OxzCreateA(full);
	}
	if (type == fdintCLOSE_FILE_INFO) {
		FILETIME loc, utc;
		if (DosDateTimeToFileTime(p->date, p->time, &loc) && LocalFileTimeToFileTime(&loc, &utc))
			SetFileTime((HANDLE)p->hf, &utc, &utc, &utc);
		CloseHandle((HANDLE)p->hf);
		return TRUE;
	}
	return 0;
}

int OxzTakeCab(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span)
{
	ERF erf;
	HFDI hf;
	CabJob job;
	char cabdir[MAX_PATH];
	BOOL ok;
	(void)base; (void)span;
	job.arc = arc;
	job.dest = dest;
	job.ents = &ents;
	job.bad = 0;
	job.done = 0;
	job.total = 1;
	for (size_t i = 0; i < ents.size(); ++i) if (!ents[i].dir) job.total++;
	CreateDirectoryW(dest, NULL);
	GetTempPathA(MAX_PATH, cabdir);
	tCab = &job;
	hf = FDICreate(CabAlloc, CabFree, CabOpen, CabRead, CabWrite, CabClose, CabSeek, cpuUNKNOWN, &erf);
	if (!hf) { tCab = NULL; err = OxzTxt(OS_OPEN_FAIL); return 2; }
	ok = FDICopy(hf, "oxz.cab", cabdir, 0, CabNotify, NULL, NULL);
	FDIDestroy(hf);
	tCab = NULL;
	if (job.bad || !ok) { err = job.err.empty() ? OxzTxt(OS_OPEN_FAIL) : job.err; return 2; }
	return 0;
}

struct LzhIO { HANDLE h; };

static long __fastcall LzhRead(char* b, long n, void* u)
{
	DWORD g = 0;
	if (n <= 0) return 0;
	if (!ReadFile(((LzhIO*)u)->h, b, (DWORD)n, &g, NULL)) return 0;
	return (long)g;
}
static long __fastcall LzhSeek(int pos, int whence, void* u)
{
	LARGE_INTEGER m, o;
	m.QuadPart = pos;
	o.QuadPart = 0;
	if (!SetFilePointerEx(((LzhIO*)u)->h, m, &o, (DWORD)whence)) return -1;
	return (long)o.QuadPart;
}

struct LzhItem {
	std::wstring rel;
	std::wstring method;
	uint64_t off;
	uint64_t comp;
	uint64_t size;
	uint64_t mtime;
	uint16_t crc;
	int dir;
};

static int LzhScan(HANDLE h, std::vector<LzhItem>& items)
{
	uint64_t pos = 0;
	uint64_t len = OxzLenA(h);
	while (pos + 22 < len) {
		unsigned char b[22];
		LzhItem it;
		unsigned char level;
		char method[6];
		std::string nameA, pathA;
		std::wstring nameW, pathW;
		uint64_t comp = 0, orig = 0, next;
		if (!OxzSeekA(h, pos) || !OxzReadExactA(h, b, 21)) break;
		if (b[0] == 0 && b[1] == 0) break;
		level = b[20];
		memcpy(method, b + 2, 5);
		method[5] = 0;
		it.method = OxzFromA(method, 5, CP_ACP);
		it.crc = 0;
		it.dir = (it.method == L"-lhd-") ? 1 : 0;
		it.mtime = 0;
		if (level == 0 || level == 1) {
			unsigned char nl = 0;
			if (!OxzReadExactA(h, &nl, 1)) return 0;
			if (nl) {
				nameA.resize(nl);
				if (!OxzReadExactA(h, &nameA[0], nl)) return 0;
			}
			it.mtime = OxzDosFt(Ru16(b + 16), Ru16(b + 14));
			orig = Ru32(b + 11);
			if (level == 0) {
				comp = Ru32(b + 7);
				it.off = pos + 2 + b[0];
				next = it.off + comp;
			} else {
				uint64_t exAt = pos + b[0];
				uint32_t ex = 0;
				for (;;) {
					unsigned char eh[3];
					if (!OxzSeekA(h, exAt + ex) || !OxzReadExactA(h, eh, 2)) return 0;
					uint16_t hs = Ru16(eh);
					if (!hs) break;
					if (!OxzReadExactA(h, eh + 2, 1)) return 0;
					if (eh[2] == 0x01) {
						int n = hs - 3;
						if (n < 0 || n > 4096) return 0;
						nameA.resize(n);
						if (n && !OxzReadExactA(h, &nameA[0], n)) return 0;
					} else if (eh[2] == 0x02) {
						int n = hs - 3;
						if (n < 0 || n > 4096) return 0;
						pathA.resize(n);
						if (n && !OxzReadExactA(h, &pathA[0], n)) return 0;
						for (int i = 0; i < n; ++i) if ((unsigned char)pathA[i] == 0xFF) pathA[i] = '/';
					} else if (eh[2] == 0x44 || eh[2] == 0x45) {
						int n = hs - 3;
						std::wstring* dst = eh[2] == 0x44 ? &nameW : &pathW;
						if (n < 0 || (n & 1) || n > 8192) return 0;
						dst->resize(n / 2);
						if (n && !OxzReadExactA(h, &(*dst)[0], n)) return 0;
						for (size_t i = 0; i < dst->size(); ++i) if ((*dst)[i] == 0xFFFF) (*dst)[i] = L'/';
					}
					ex += hs;
				}
				comp = Ru32(b + 7);
				if (comp >= ex) comp -= ex;
				it.off = exAt + ex + 2;
				next = it.off + comp;
			}
		} else if (level == 2) {
			unsigned char h2[26];
			uint32_t ex = 0;
			uint64_t exAt;
			if (!OxzSeekA(h, pos) || !OxzReadExactA(h, h2, 24)) return 0;
			memcpy(method, h2 + 2, 5);
			method[5] = 0;
			it.method = OxzFromA(method, 5, CP_ACP);
			it.dir = (it.method == L"-lhd-") ? 1 : 0;
			orig = Ru32(h2 + 11);
			comp = Ru32(h2 + 7);
			it.crc = Ru16(h2 + 21);
			{
				uint64_t ft = ((uint64_t)Ru32(h2 + 15)) * 10000000ull + 0x19DB1DED53E8000ull;
				it.mtime = ft;
			}
			exAt = pos + 24;
			for (;;) {
				unsigned char eh[3];
				if (!OxzSeekA(h, exAt + ex) || !OxzReadExactA(h, eh, 2)) return 0;
				uint16_t hs = Ru16(eh);
				if (!hs) break;
				if (!OxzReadExactA(h, eh + 2, 1)) return 0;
				if (eh[2] == 0x01) {
					int n = hs - 3;
					if (n < 0 || n > 4096) return 0;
					nameA.resize(n);
					if (n && !OxzReadExactA(h, &nameA[0], n)) return 0;
				} else if (eh[2] == 0x02) {
					int n = hs - 3;
					if (n < 0 || n > 4096) return 0;
					pathA.resize(n);
					if (n && !OxzReadExactA(h, &pathA[0], n)) return 0;
					for (int i = 0; i < n; ++i) if ((unsigned char)pathA[i] == 0xFF) pathA[i] = '/';
				} else if (eh[2] == 0x44 || eh[2] == 0x45) {
					int n = hs - 3;
					std::wstring* dst = eh[2] == 0x44 ? &nameW : &pathW;
					if (n < 0 || (n & 1) || n > 8192) return 0;
					dst->resize(n / 2);
					if (n && !OxzReadExactA(h, &(*dst)[0], n)) return 0;
					for (size_t i = 0; i < dst->size(); ++i) if ((*dst)[i] == 0xFFFF) (*dst)[i] = L'/';
				}
				ex += hs;
			}
			it.off = exAt + ex + 2;
			next = it.off + comp;
		} else return 0;
		it.comp = comp;
		it.size = orig;
		if (!nameW.empty()) it.rel = pathW.empty() ? nameW : (pathW + nameW);
		else {
			std::string both = pathA + nameA;
			it.rel = OxzFromA(both.c_str(), (int)both.size(), CP_ACP);
		}
		for (size_t i = 0; i < it.rel.size(); ++i) if (it.rel[i] == L'/') it.rel[i] = L'\\';
		{
			std::wstring rel;
			if (OxzSafeRelA(it.rel.c_str(), rel)) { it.rel.swap(rel); items.push_back(it); }
		}
		pos = next;
	}
	return 1;
}

static int LzhOne(HANDLE h, const LzhItem& it, const std::wstring& full, std::wstring& err)
{
	HANDLE o;
	uint64_t left;
	OxzEnsureParentA(full);
	if (it.dir) { CreateDirectoryW(full.c_str(), NULL); return 1; }
	o = OxzCreateA(full);
	if (o == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
	if (it.method == L"-lh0-" || it.method == L"-lz4-") {
		unsigned char buf[1 << 16];
		if (!OxzSeekA(h, it.off)) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 0; }
		left = it.comp;
		while (left) {
			DWORD n = left > sizeof(buf) ? (DWORD)sizeof(buf) : (DWORD)left;
			DWORD g = 0, w = 0;
			if (!ReadFile(h, buf, n, &g, NULL) || g != n || !WriteFile(o, buf, n, &w, NULL) || w != n) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 0; }
			OxzPulseAdd(n);
			left -= n;
		}
		OxzStampA(o, it.mtime);
		CloseHandle(o);
		return 1;
	}
	{
		LzhIO io;
		UNLZHHandler dec;
		std::vector<unsigned char> buf;
		long got;
		io.h = h;
		if (!OxzSeekA(h, it.off)) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 0; }
		dec = open_unlzh_handler(LzhRead, LzhSeek, it.method.c_str(), (long)it.comp, (long)it.size, &io);
		if (!dec) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 0; }
		buf.resize(1 << 16);
		for (;;) {
			DWORD w = 0;
			got = unlzh(dec, (char*)buf.data(), (long)buf.size());
			if (got < 0) { close_unlzh_handler(dec); CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 0; }
			if (got == 0) break;
			if (!WriteFile(o, buf.data(), (DWORD)got, &w, NULL) || w != (DWORD)got) { close_unlzh_handler(dec); CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 0; }
			OxzPulseAdd((uint64_t)got);
		}
		close_unlzh_handler(dec);
		OxzStampA(o, it.mtime);
		CloseHandle(o);
		return 1;
	}
}

static int LzhLoad(const wchar_t* path, std::vector<OxzEnt>& out, std::vector<LzhItem>* raw, std::wstring& err)
{
	HANDLE h = OxzOpenReadA(path);
	std::vector<LzhItem> items;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (!LzhScan(h, items)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	CloseHandle(h);
	for (size_t i = 0; i < items.size(); ++i) {
		OxzEnt e;
		e.rel = items[i].rel;
		e.origin = items[i].rel;
		e.codec = items[i].method;
		e.dir = items[i].dir;
		e.index = (int)i;
		e.kind = EK_LZH;
		e.packedKnown = 1;
		e.crcBits = items[i].crc ? 16 : 0;
		e.crc = items[i].crc;
		e.size = items[i].size;
		e.packed = items[i].comp;
		e.offset = items[i].off;
		e.mtime = items[i].mtime;
		out.push_back(e);
	}
	if (raw) raw->swap(items);
	return 0;
}

int OxzListLzh(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err)
{
	return LzhLoad(path, out, NULL, err);
}

int OxzTakeLzh(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span)
{
	HANDLE h = OxzOpenReadA(arc);
	std::vector<LzhItem> items;
	int nfile = 0, done = 0;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (!LzhScan(h, items)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	CreateDirectoryW(dest, NULL);
	for (size_t i = 0; i < ents.size(); ++i) if (!ents[i].dir) nfile++;
	if (nfile < 1) nfile = 1;
	for (size_t i = 0; i < ents.size(); ++i) {
		const LzhItem* it = NULL;
		if (ents[i].dir) { OxzEnsureParentA(std::wstring(dest) + L"\\" + ents[i].rel + L"\\x"); continue; }
		for (size_t k = 0; k < items.size(); ++k)
			if (_wcsicmp(items[k].rel.c_str(), ents[i].origin.c_str()) == 0) { it = &items[k]; break; }
		if (!it) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		OxzNoteA(done, nfile, ents[i].rel.c_str(), base, span);
		OxzPulseSize(it->size);
		done++;
		if (!LzhOne(h, *it, std::wstring(dest) + L"\\" + ents[i].rel, err)) { CloseHandle(h); return 2; }
	}
	CloseHandle(h);
	return 0;
}

struct IsoFile { std::wstring rel; int dir; uint64_t off; uint64_t size; uint64_t mtime; std::vector<OxzSpan> spans; };

static uint64_t IsoTime(const unsigned char* b)
{
	SYSTEMTIME st;
	FILETIME ft;
	memset(&st, 0, sizeof(st));
	st.wYear = (WORD)(1900 + b[0]);
	st.wMonth = b[1] ? b[1] : 1;
	st.wDay = b[2] ? b[2] : 1;
	st.wHour = b[3];
	st.wMinute = b[4];
	st.wSecond = b[5];
	if (!SystemTimeToFileTime(&st, &ft)) return 0;
	return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static void IsoWalk(HANDLE h, uint32_t block, uint64_t extent, uint64_t bytes, const std::wstring& parent, int joliet, std::vector<IsoFile>& files, int depth)
{
	std::vector<unsigned char> buf;
	size_t i;
	if (depth > 40 || !bytes || bytes > (64ull << 20)) return;
	buf.resize((size_t)bytes);
	if (!OxzSeekA(h, extent * block) || !OxzReadExactA(h, buf.data(), (DWORD)bytes)) return;
	i = 0;
	while (i + 34 < buf.size()) {
		unsigned rec = buf[i];
		unsigned nl, flags;
		uint32_t ext, sz;
		std::wstring name, rel, full;
		IsoFile f;
		size_t sys;
		if (rec == 0) { i = (i + block) & ~(size_t)(block - 1); if (!i) break; continue; }
		if (i + rec > buf.size()) break;
		nl = buf[i + 32];
		flags = buf[i + 25];
		ext = (uint32_t)buf[i + 2] | ((uint32_t)buf[i + 3] << 8) | ((uint32_t)buf[i + 4] << 16) | ((uint32_t)buf[i + 5] << 24);
		sz = (uint32_t)buf[i + 10] | ((uint32_t)buf[i + 11] << 8) | ((uint32_t)buf[i + 12] << 16) | ((uint32_t)buf[i + 13] << 24);
		if (nl == 1 && (buf[i + 33] == 0 || buf[i + 33] == 1)) { i += rec; continue; }
		if (joliet) {
			int chars = nl / 2;
			name.resize(chars);
			for (int c = 0; c < chars; ++c) name[c] = (wchar_t)((buf[i + 33 + c * 2] << 8) | buf[i + 34 + c * 2]);
		} else {
			std::string a((char*)buf.data() + i + 33, nl);
			name = OxzFromA(a.c_str(), (int)a.size(), CP_ACP);
		}
		{
			size_t sc = name.find(L';');
			if (sc != std::wstring::npos) name.resize(sc);
			while (!name.empty() && name.back() == L'.') name.pop_back();
		}
		sys = i + 33 + nl;
		if (nl & 1) sys++;
		while (sys + 4 < i + rec) {
			unsigned sl = buf[sys + 2];
			if (sl < 4 || sys + sl > i + rec) break;
			if (buf[sys] == 'N' && buf[sys + 1] == 'M' && sl > 5) {
				std::string a((char*)buf.data() + sys + 5, sl - 5);
				name = OxzFromA(a.c_str(), (int)a.size(), CP_UTF8);
				if (name.empty()) name = OxzFromA(a.c_str(), (int)a.size(), CP_ACP);
			}
			sys += sl;
		}
		for (size_t k = 0; k < name.size(); ++k) if (name[k] == L'/') name[k] = L'\\';
		full = parent.empty() ? name : (parent + L"\\" + name);
		if (OxzSafeRelA(full.c_str(), rel)) {
			f.rel = rel;
			f.dir = (flags & 2) ? 1 : 0;
			f.off = (uint64_t)ext * block;
			f.size = sz;
			f.mtime = IsoTime(buf.data() + i + 18);
			if (!f.dir) { OxzSpan sp; sp.off = f.off; sp.n = f.size; f.spans.push_back(sp); }
			if (!f.dir && (flags & 0x80) == 0 && !files.empty() && !files.back().dir && files.back().rel == rel && !files.back().spans.empty() && (files.back().spans.size() > 1 || files.back().size != files.back().spans[0].n)) {
				files.back().spans.push_back(f.spans[0]);
				files.back().size += f.size;
			} else if (!f.dir && !files.empty() && !files.back().dir && files.back().rel == rel) {
				files.back().spans.push_back(f.spans[0]);
				files.back().size += f.size;
			} else files.push_back(f);
		}
		if (flags & 2) IsoWalk(h, block, ext, sz, full, joliet, files, depth + 1);
		i += rec;
	}
}

int OxzListIso(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err)
{
	HANDLE h = OxzOpenReadA(path);
	unsigned char vd[2048];
	uint32_t block = 2048;
	int joliet = 0;
	uint64_t rootExt = 0, rootSz = 0;
	uint32_t sector;
	std::vector<IsoFile> files;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	for (sector = 16; sector < 64; ++sector) {
		if (!OxzSeekA(h, (uint64_t)sector * 2048) || !OxzReadExactA(h, vd, 2048)) break;
		if (vd[0] == 255) break;
		if (memcmp(vd + 1, "CD001", 5) != 0) continue;
		if (vd[0] == 1 && !rootExt) {
			block = (uint32_t)vd[128] | ((uint32_t)vd[129] << 8);
			if (block != 512 && block != 1024 && block != 2048) block = 2048;
			rootExt = (uint32_t)vd[158] | ((uint32_t)vd[159] << 8) | ((uint32_t)vd[160] << 16) | ((uint32_t)vd[161] << 24);
			rootSz = (uint32_t)vd[166] | ((uint32_t)vd[167] << 8) | ((uint32_t)vd[168] << 16) | ((uint32_t)vd[169] << 24);
		}
		if (vd[0] == 2 && vd[88] == 0x25 && vd[89] == 0x2F) {
			joliet = 1;
			rootExt = (uint32_t)vd[158] | ((uint32_t)vd[159] << 8) | ((uint32_t)vd[160] << 16) | ((uint32_t)vd[161] << 24);
			rootSz = (uint32_t)vd[166] | ((uint32_t)vd[167] << 8) | ((uint32_t)vd[168] << 16) | ((uint32_t)vd[169] << 24);
		}
	}
	if (!rootExt || !rootSz) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	IsoWalk(h, block, rootExt, rootSz, L"", joliet, files, 0);
	CloseHandle(h);
	for (size_t i = 0; i < files.size(); ++i) {
		OxzEnt e;
		e.rel = files[i].rel;
		e.origin = files[i].rel;
		e.codec = joliet ? L"Joliet" : L"ISO9660";
		e.dir = files[i].dir;
		e.index = (int)i;
		e.kind = EK_ISO;
		e.packedKnown = 1;
		e.crcBits = 0;
		e.size = files[i].size;
		e.packed = files[i].size;
		e.offset = files[i].off;
		e.mtime = files[i].mtime;
		e.spans = files[i].spans;
		out.push_back(e);
	}
	return 0;
}

int OxzTakeIso(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span)
{
	HANDLE h = OxzOpenReadA(arc);
	int nfile = 0, done = 0;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	CreateDirectoryW(dest, NULL);
	for (size_t i = 0; i < ents.size(); ++i) if (!ents[i].dir) nfile++;
	if (nfile < 1) nfile = 1;
	for (size_t i = 0; i < ents.size(); ++i) {
		std::wstring full = std::wstring(dest) + L"\\" + ents[i].rel;
		HANDLE o;
		uint64_t left, off;
		if (ents[i].dir) { OxzEnsureParentA(full + L"\\x"); continue; }
		OxzNoteA(done, nfile, ents[i].rel.c_str(), base, span);
		OxzPulseSize(ents[i].size);
		done++;
		o = OxzCreateA(full);
		if (o == INVALID_HANDLE_VALUE) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		if (!ents[i].spans.empty()) {
			for (size_t s = 0; s < ents[i].spans.size(); ++s) {
				unsigned char buf[1 << 16];
				left = ents[i].spans[s].n;
				off = ents[i].spans[s].off;
				if (!OxzSeekA(h, off)) { CloseHandle(o); CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
				while (left) {
					DWORD n = left > sizeof(buf) ? (DWORD)sizeof(buf) : (DWORD)left;
					DWORD g = 0, w = 0;
					if (!ReadFile(h, buf, n, &g, NULL) || g != n || !WriteFile(o, buf, n, &w, NULL) || w != n) { CloseHandle(o); CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
					OxzPulseAdd(n);
					left -= n;
				}
			}
		} else {
			unsigned char buf[1 << 16];
			left = ents[i].size;
			if (!OxzSeekA(h, ents[i].offset)) { CloseHandle(o); CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			while (left) {
				DWORD n = left > sizeof(buf) ? (DWORD)sizeof(buf) : (DWORD)left;
				DWORD g = 0, w = 0;
				if (!ReadFile(h, buf, n, &g, NULL) || g != n || !WriteFile(o, buf, n, &w, NULL) || w != n) { CloseHandle(o); CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
				OxzPulseAdd(n);
				left -= n;
			}
		}
		OxzStampA(o, ents[i].mtime);
		CloseHandle(o);
	}
	CloseHandle(h);
	return 0;
}

struct ArjItem { std::wstring rel; int method; int dir; uint64_t off; uint64_t comp; uint64_t size; uint64_t mtime; uint32_t crc; };

static int ArjScan(HANDLE h, std::vector<ArjItem>& items)
{
	unsigned char mag[4];
	uint64_t pos = 0;
	int mainHdr = 1;
	if (!OxzReadExactA(h, mag, 2)) return 0;
	if (mag[0] != 0x60 || mag[1] != 0xEA) return 0;
	for (;;) {
		unsigned char hs[2];
		uint16_t size;
		std::vector<unsigned char> hdr;
		ArjItem it;
		uint8_t method, flags, namelen;
		uint32_t comp, orig;
		std::string name;
		if (!OxzSeekA(h, pos) || !OxzReadExactA(h, mag, 2)) return 1;
		if (mag[0] != 0x60 || mag[1] != 0xEA) return pos == 0 ? 0 : 1;
		if (!OxzReadExactA(h, hs, 2)) return 0;
		size = Ru16(hs);
		if (size == 0) return 1;
		if (size < 30 || size > 65535) return 0;
		hdr.resize(size);
		if (!OxzReadExactA(h, hdr.data(), size)) return 0;
		{
			unsigned char exb[2];
			uint64_t after = pos + 4ull + size + 4ull;
			if (!OxzSeekA(h, after) || !OxzReadExactA(h, exb, 2)) return 0;
			after += 2ull + Ru16(exb);
			if (hdr[0] < 1 || hdr[0] >= size) { pos = after + Ru32(hdr.data() + 12); continue; }
			method = hdr[5];
			flags = hdr[4];
			comp = Ru32(hdr.data() + 12);
			orig = Ru32(hdr.data() + 16);
			it.crc = Ru32(hdr.data() + 20);
			it.mtime = OxzDosFt((uint16_t)(Ru32(hdr.data() + 8) >> 16), (uint16_t)Ru32(hdr.data() + 8));
			it.method = method;
			it.comp = comp;
			it.size = orig;
			it.dir = (hdr[6] == 3) ? 1 : 0;
			if (mainHdr || hdr[6] == 2) { mainHdr = 0; pos = after + comp; continue; }
			namelen = 0;
			name.assign((char*)hdr.data() + hdr[0], strnlen((char*)hdr.data() + hdr[0], size - hdr[0]));
			{
				std::wstring w = OxzFromA(name.c_str(), (int)name.size(), CP_ACP);
				std::wstring rel;
				for (size_t i = 0; i < w.size(); ++i) if (w[i] == L'/') w[i] = L'\\';
				if (!OxzSafeRelA(w.c_str(), rel)) { pos = after + comp; continue; }
				it.rel = rel;
			}
			it.off = after;
			items.push_back(it);
			pos = it.off + comp;
		}
		(void)namelen;
	}
}

static int ArjCopy(HANDLE h, const ArjItem& it, HANDLE o)
{
	unsigned char buf[1 << 16];
	uint64_t left = it.method == 0 ? it.size : it.comp;
	if (it.method != 0) return 0;
	if (!OxzSeekA(h, it.off)) return 0;
	while (left) {
		DWORD n = left > sizeof(buf) ? (DWORD)sizeof(buf) : (DWORD)left;
		DWORD g = 0, w = 0;
		if (!ReadFile(h, buf, n, &g, NULL) || g != n || !WriteFile(o, buf, n, &w, NULL) || w != n) return 0;
		OxzPulseAdd(n);
		left -= n;
	}
	return 1;
}

/* ARJ methods 1-4. 16-bit MSB bit buffer and the 26,624-byte window used by unarj. */
enum { ARJ_DIC = 26624, ARJ_NC = 510, ARJ_NP = 17, ARJ_NT = 19, ARJ_CBIT = 9, ARJ_PBIT = 5, ARJ_TBIT = 5 };

struct ArjDec {
	HANDLE h;
	uint64_t left;
	uint16_t bitbuf;
	unsigned char sub;
	int bitcount;
	uint16_t blocksize;
	uint16_t leftT[ARJ_NC * 2];
	uint16_t rightT[ARJ_NC * 2];
	unsigned char cLen[ARJ_NC];
	unsigned char ptLen[ARJ_NT];
	uint16_t cTable[4096];
	uint16_t ptTable[256];
	int getbyte()
	{
		unsigned char c;
		DWORD g = 0;
		if (!left) return 0;
		if (!ReadFile(h, &c, 1, &g, NULL) || g != 1) { left = 0; return 0; }
		left--;
		return c;
	}
	void fillbuf(int n)
	{
		bitbuf = (uint16_t)(bitbuf << n);
		while (n > bitcount) {
			bitbuf = (uint16_t)(bitbuf | (uint16_t)((uint16_t)sub << (n -= bitcount)));
			sub = (unsigned char)getbyte();
			bitcount = 8;
		}
		bitbuf = (uint16_t)(bitbuf | (uint16_t)(sub >> (bitcount -= n)));
	}
	int getbits(int n)
	{
		int x = bitbuf >> (16 - n);
		fillbuf(n);
		return x;
	}
	int make(int nchar, const unsigned char* bitlen, int tablebits, uint16_t* table)
	{
		uint16_t count[17], weight[17], start[18];
		int i, ch, jutbits, avail;
		memset(count, 0, sizeof(count));
		for (i = 0; i < nchar; ++i) {
			if (bitlen[i] > 16) return 0;
			count[bitlen[i]]++;
		}
		start[1] = 0;
		for (i = 1; i <= 16; ++i) start[i + 1] = (uint16_t)(start[i] + (uint16_t)(count[i] << (16 - i)));
		if (start[17] != 0) return 0;
		jutbits = 16 - tablebits;
		for (i = 1; i <= tablebits; ++i) {
			start[i] = (uint16_t)(start[i] >> jutbits);
			weight[i] = (uint16_t)(1u << (tablebits - i));
		}
		while (i <= 16) { weight[i] = (uint16_t)(1u << (16 - i)); i++; }
		i = start[tablebits + 1] >> jutbits;
		if (i != 0) {
			int k = 1 << tablebits;
			while (i != k) table[i++] = 0;
		}
		avail = nchar;
		{
			unsigned mask0 = 1u << (15 - tablebits);
			for (ch = 0; ch < nchar; ++ch) {
				int len = bitlen[ch];
				unsigned nextcode, k;
				uint16_t* p;
				if (!len) continue;
				nextcode = (unsigned)start[len] + weight[len];
				if (len <= tablebits) {
					for (k = start[len]; k < nextcode; ++k) table[k] = (uint16_t)ch;
				} else {
					k = start[len];
					p = &table[k >> jutbits];
					i = len - tablebits;
					while (i != 0) {
						if (*p == 0) {
							rightT[avail] = leftT[avail] = 0;
							*p = (uint16_t)avail++;
						}
						if (k & mask0) p = &rightT[*p];
						else p = &leftT[*p];
						k <<= 1;
						i--;
					}
					*p = (uint16_t)ch;
				}
				start[len] = (uint16_t)nextcode;
			}
		}
		return 1;
	}
	int readPt(int nn, int nbit, int special)
	{
		int i, n, c;
		n = getbits(nbit);
		if (n == 0) {
			c = getbits(nbit);
			for (i = 0; i < nn; ++i) ptLen[i] = 0;
			for (i = 0; i < 256; ++i) ptTable[i] = (uint16_t)c;
			return 1;
		}
		if (n > nn) return 0;
		i = 0;
		while (i < n) {
			c = getbits(3);
			if (c == 7) while (getbits(1)) c++;
			ptLen[i++] = (unsigned char)c;
			if (i == special) {
				c = getbits(2);
				while (--c >= 0 && i < nn) ptLen[i++] = 0;
			}
		}
		while (i < nn) ptLen[i++] = 0;
		return make(nn, ptLen, 8, ptTable);
	}
	int readC()
	{
		int i, n, c;
		n = getbits(ARJ_CBIT);
		if (n == 0) {
			c = getbits(ARJ_CBIT);
			for (i = 0; i < ARJ_NC; ++i) cLen[i] = 0;
			for (i = 0; i < 4096; ++i) cTable[i] = (uint16_t)c;
			return 1;
		}
		if (n > ARJ_NC) return 0;
		i = 0;
		while (i < n) {
			unsigned mask;
			c = ptTable[bitbuf >> 8];
			if (c >= ARJ_NT) {
				mask = 1u << 7;
				do {
					if (bitbuf & mask) c = rightT[c];
					else c = leftT[c];
					mask >>= 1;
				} while (c >= ARJ_NT);
			}
			fillbuf(ptLen[c]);
			if (c <= 2) {
				if (c == 0) c = 1;
				else if (c == 1) c = getbits(4) + 3;
				else c = getbits(ARJ_CBIT) + 20;
				while (--c >= 0 && i < ARJ_NC) cLen[i++] = 0;
			} else if (i < ARJ_NC) cLen[i++] = (unsigned char)(c - 2);
		}
		while (i < ARJ_NC) cLen[i++] = 0;
		return make(ARJ_NC, cLen, 12, cTable);
	}
	int decodeC()
	{
		unsigned mask;
		int j;
		if (blocksize == 0) {
			blocksize = (uint16_t)getbits(16);
			if (!readPt(ARJ_NT, ARJ_TBIT, 3) || !readC() || !readPt(ARJ_NP, ARJ_PBIT, -1)) return -1;
		}
		blocksize--;
		j = cTable[bitbuf >> 4];
		if (j >= ARJ_NC) {
			mask = 1u << 3;
			do {
				if (bitbuf & mask) j = rightT[j];
				else j = leftT[j];
				mask >>= 1;
			} while (j >= ARJ_NC);
		}
		fillbuf(cLen[j]);
		return j;
	}
	int decodeP()
	{
		unsigned mask;
		int j = ptTable[bitbuf >> 8];
		if (j >= ARJ_NP) {
			mask = 1u << 7;
			do {
				if (bitbuf & mask) j = rightT[j];
				else j = leftT[j];
				mask >>= 1;
			} while (j >= ARJ_NP);
		}
		fillbuf(ptLen[j]);
		if (j != 0) {
			j--;
			j = (1 << j) + getbits(j);
		}
		return j;
	}
};

static int ArjExpand(HANDLE h, const ArjItem& it, HANDLE o, std::wstring& err)
{
	ArjDec d;
	unsigned char dic[ARJ_DIC];
	unsigned char outb[1 << 15];
	int outn = 0, r = 0;
	uint64_t produced = 0;
	if (it.method < 1 || it.method > 4) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
	memset(&d, 0, sizeof(d));
	memset(dic, 0, sizeof(dic));
	d.h = h;
	d.left = it.comp;
	if (!OxzSeekA(h, it.off)) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
	d.bitcount = 0;
	d.fillbuf(16);
	while (produced < it.size) {
		int c = d.decodeC();
		int j, pos, n;
		if (c < 0) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
		if (c <= 255) {
			dic[r] = (unsigned char)c;
			if (++r == ARJ_DIC) r = 0;
			outb[outn++] = (unsigned char)c;
			produced++;
		} else {
			j = c - (255 + 1 - 3);
			pos = d.decodeP();
			if (j < 3 || (uint64_t)j > it.size - produced) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
			n = r - pos - 1;
			if (n < 0) n += ARJ_DIC;
			while (j-- > 0) {
				unsigned char b = dic[n];
				if (++n == ARJ_DIC) n = 0;
				dic[r] = b;
				if (++r == ARJ_DIC) r = 0;
				outb[outn++] = b;
				produced++;
				if (outn == (int)sizeof(outb)) {
					DWORD w = 0;
					if (!WriteFile(o, outb, outn, &w, NULL) || w != (DWORD)outn) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
					OxzPulseAdd((uint64_t)outn);
					outn = 0;
				}
			}
		}
		if (outn == (int)sizeof(outb)) {
			DWORD w = 0;
			if (!WriteFile(o, outb, outn, &w, NULL) || w != (DWORD)outn) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
			OxzPulseAdd((uint64_t)outn);
			outn = 0;
		}
	}
	if (outn) {
		DWORD w = 0;
		if (!WriteFile(o, outb, outn, &w, NULL) || w != (DWORD)outn) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
		OxzPulseAdd((uint64_t)outn);
	}
	return 1;
}

int OxzListArj(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err)
{
	HANDLE h = OxzOpenReadA(path);
	std::vector<ArjItem> items;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (!ArjScan(h, items)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	CloseHandle(h);
	for (size_t i = 0; i < items.size(); ++i) {
		OxzEnt e;
		wchar_t b[16];
		e.rel = items[i].rel;
		e.origin = items[i].rel;
		e.dir = items[i].dir;
		e.index = (int)i;
		e.kind = EK_ARJ;
		e.packedKnown = 1;
		e.crcBits = 32;
		e.crc = items[i].crc;
		e.size = items[i].size;
		e.packed = items[i].comp;
		e.offset = items[i].off;
		e.mtime = items[i].mtime;
		if (items[i].method == 0) e.codec = L"Store";
		else { _snwprintf_s(b, _TRUNCATE, L"ARJ%u", items[i].method); e.codec = b; }
		out.push_back(e);
	}
	return 0;
}

int OxzTakeArj(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span)
{
	HANDLE h = OxzOpenReadA(arc);
	std::vector<ArjItem> items;
	int nfile = 0, done = 0;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (!ArjScan(h, items)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	CreateDirectoryW(dest, NULL);
	for (size_t i = 0; i < ents.size(); ++i) if (!ents[i].dir) nfile++;
	if (nfile < 1) nfile = 1;
	for (size_t i = 0; i < ents.size(); ++i) {
		const ArjItem* it = NULL;
		std::wstring full;
		HANDLE o;
		if (ents[i].dir) { OxzEnsureParentA(std::wstring(dest) + L"\\" + ents[i].rel + L"\\x"); continue; }
		for (size_t k = 0; k < items.size(); ++k)
			if (_wcsicmp(items[k].rel.c_str(), ents[i].origin.c_str()) == 0) { it = &items[k]; break; }
		if (!it) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		full = std::wstring(dest) + L"\\" + ents[i].rel;
		OxzNoteA(done, nfile, ents[i].rel.c_str(), base, span);
		OxzPulseSize(it->size);
		done++;
		o = OxzCreateA(full);
		if (o == INVALID_HANDLE_VALUE) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		if (it->method == 0) {
			if (!ArjCopy(h, *it, o)) { CloseHandle(o); CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		} else if (!ArjExpand(h, *it, o, err)) { CloseHandle(o); CloseHandle(h); return 2; }
		OxzStampA(o, it->mtime);
		CloseHandle(o);
	}
	CloseHandle(h);
	return 0;
}
