#include "OxzIn.h"
#include "OxzLang.h"
#include "OxzWork.h"
#include "OxzFilt.h"
#include "../OxzCore/OxzCore.h"
#include "OxzVol.h"
#include "7z.h"
#include "7zFile.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#include "LzmaDec.h"
#include "Xz.h"
#include "XzEnc.h"
#include "zlib.h"
#include "../OxzCore/third/lz4/lib/lz4.h"
#include "zip.h"
#include "iowin32.h"
#include "../OxzCore/third/bzip2/bzlib.h"
#include "zstd/zstd.h"
#include "../../zlib-develop/contrib/infback9/infback9.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <memory>

int OxzEndsI(const wchar_t* path, const wchar_t* ext)
{
	size_t n = path ? wcslen(path) : 0, e = ext ? wcslen(ext) : 0;
	if (!n || e > n) return 0;
	return _wcsicmp(path + n - e, ext) == 0;
}

int OxzNameIsTar(const wchar_t* path)
{
	return OxzEndsI(path, L".tar") || OxzEndsI(path, L".tgz") || OxzEndsI(path, L".tar.gz")
		|| OxzEndsI(path, L".tbz") || OxzEndsI(path, L".tbz2") || OxzEndsI(path, L".tar.bz2")
		|| OxzEndsI(path, L".txz") || OxzEndsI(path, L".tar.xz")
		|| OxzEndsI(path, L".tzst") || OxzEndsI(path, L".tar.zst")
		|| OxzEndsI(path, L".tar.lz4") || OxzEndsI(path, L".tlz4");
}

int OxzTarWrap(const wchar_t* path)
{
	if (!path) return -1;
	if (OxzEndsI(path, L".tar.gz") || OxzEndsI(path, L".tgz")) return 1;
	if (OxzEndsI(path, L".tar.bz2") || OxzEndsI(path, L".tbz2") || OxzEndsI(path, L".tbz")) return 2;
	if (OxzEndsI(path, L".tar.xz") || OxzEndsI(path, L".txz")) return 3;
	if (OxzEndsI(path, L".tar.zst") || OxzEndsI(path, L".tzst")) return 4;
	if (OxzEndsI(path, L".tar.lz4") || OxzEndsI(path, L".tlz4") || OxzEndsI(path, L".lz4")) return 5;
	if (OxzEndsI(path, L".tar")) return 0;
	return -1;
}

void OxzApplyTarExt(std::wstring& path, int wrap)
{
	static const wchar_t* cut[] = {
		L".tar.gz", L".tar.bz2", L".tar.xz", L".tar.zst", L".tar.lz4",
		L".tbz2", L".tgz", L".tbz", L".txz", L".tzst", L".tlz4", L".lz4", L".tar", NULL
	};
	static const wchar_t* put[] = { L".tar", L".tgz", L".tbz2", L".txz", L".tzst", L".lz4" };
	int i;
	if (wrap < 0 || wrap > 5) wrap = 0;
	for (i = 0; cut[i]; ++i) {
		if (OxzEndsI(path.c_str(), cut[i])) {
			path.resize(path.size() - wcslen(cut[i]));
			break;
		}
	}
	path += put[wrap];
}

int OxzSafeRelA(const wchar_t* name, std::wstring& rel)
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

void OxzEnsureParentA(const std::wstring& path)
{
	for (size_t i = 0; i < path.size(); ++i) {
		if ((path[i] == L'\\' || path[i] == L'/') && i > 2) CreateDirectoryW(path.substr(0, i).c_str(), NULL);
	}
}

HANDLE OxzOpenReadA(const wchar_t* path)
{
	return CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

int OxzReadExactA(HANDLE h, void* p, DWORD n)
{
	unsigned char* b = (unsigned char*)p;
	DWORD o = 0;
	while (o < n) {
		DWORD g = 0;
		if (!ReadFile(h, b + o, n - o, &g, NULL) || g == 0) return 0;
		o += g;
	}
	return 1;
}

int OxzSeekA(HANDLE h, uint64_t off)
{
	LARGE_INTEGER li;
	li.QuadPart = (LONGLONG)off;
	return SetFilePointerEx(h, li, NULL, FILE_BEGIN) ? 1 : 0;
}

uint64_t OxzLenA(HANDLE h)
{
	LARGE_INTEGER n;
	n.QuadPart = 0;
	if (!GetFileSizeEx(h, &n)) return 0;
	return (uint64_t)n.QuadPart;
}

std::wstring OxzFromA(const char* s, int n, UINT cp)
{
	if (!s) return L"";
	if (n < 0) n = (int)strlen(s);
	if (n <= 0) return L"";
	int c = MultiByteToWideChar(cp, 0, s, n, NULL, 0);
	if (c <= 0) return L"";
	std::wstring w(c, L'\0');
	MultiByteToWideChar(cp, 0, s, n, &w[0], c);
	while (!w.empty() && w.back() == 0) w.pop_back();
	return w;
}

uint64_t OxzDosFt(uint16_t date, uint16_t time)
{
	FILETIME loc, utc;
	if (!DosDateTimeToFileTime(date, time, &loc)) return 0;
	if (!LocalFileTimeToFileTime(&loc, &utc)) return 0;
	return ((uint64_t)utc.dwHighDateTime << 32) | utc.dwLowDateTime;
}

static OxzEnt Blank()
{
	OxzEnt e;
	e.dir = 0;
	e.index = 0;
	e.kind = 0;
	e.packedKnown = 0;
	e.crcBits = 0;
	e.crc = 0;
	e.size = e.packed = e.offset = e.mtime = 0;
	return e;
}

void OxzAddDirsA(std::vector<OxzEnt>& v)
{
	std::vector<std::wstring> have;
	std::vector<OxzEnt> add;
	for (size_t i = 0; i < v.size(); ++i) if (v[i].dir) have.push_back(v[i].rel);
	for (size_t i = 0; i < v.size(); ++i) {
		std::wstring p = v[i].rel;
		for (;;) {
			size_t s = p.find_last_of(L'\\');
			int found = 0;
			if (s == std::wstring::npos) break;
			p.resize(s);
			if (p.empty()) break;
			for (size_t k = 0; k < have.size(); ++k) if (_wcsicmp(have[k].c_str(), p.c_str()) == 0) { found = 1; break; }
			for (size_t k = 0; !found && k < add.size(); ++k) if (_wcsicmp(add[k].rel.c_str(), p.c_str()) == 0) found = 1;
			if (found) continue;
			OxzEnt e = Blank();
			e.rel = p;
			e.origin = p;
			e.dir = 1;
			e.kind = v[i].kind;
			e.codec = v[i].codec;
			add.push_back(e);
			have.push_back(p);
		}
	}
	for (size_t i = 0; i < add.size(); ++i) v.push_back(add[i]);
}

struct FileWatch {
	int on;
	int i;
	int n;
	int base;
	int span;
	uint64_t size;
	uint64_t got;
	DWORD tick;
	wchar_t name[1024];
};

static thread_local FileWatch tWatch = { 0, 0, 1, 0, 100, 0, 0, 0, L"" };

static int WatchOverall(int fileTen)
{
	int v = tWatch.base * 10;
	int cap = (tWatch.base + tWatch.span) * 10;
	if (fileTen < 0) fileTen = 0;
	if (fileTen > 1000) fileTen = 1000;
	if (tWatch.n > 0) {
		unsigned long long num = (unsigned long long)tWatch.i * 1000ull + (unsigned)fileTen;
		v += (int)(((unsigned long long)tWatch.span * 10ull * num) / ((unsigned long long)tWatch.n * 1000ull));
	}
	if (v > cap) v = cap;
	if (v > 1000) v = 1000;
	if (v < 0) v = 0;
	return v;
}

void OxzNoteA(int i, int n, const wchar_t* name, int base, int span)
{
	if (i < 0) i = 0;
	if (n < 1) n = 1;
	tWatch.on = 1;
	tWatch.i = i;
	tWatch.n = n;
	tWatch.base = base;
	tWatch.span = span;
	tWatch.size = 0;
	tWatch.got = 0;
	tWatch.tick = 0;
	tWatch.name[0] = 0;
	if (name) wcsncpy_s(tWatch.name, name, _TRUNCATE);
	OxzProgDetail(tWatch.name, (unsigned)(i + 1), (unsigned)n, 0, WatchOverall(0));
}

void OxzPulseSize(uint64_t n)
{
	tWatch.size = n;
	tWatch.got = 0;
}

void OxzPulseReset()
{
	tWatch.on = 0;
	tWatch.size = 0;
	tWatch.got = 0;
	tWatch.tick = 0;
}

void OxzPulseAdd(uint64_t n)
{
	int ten;
	DWORD now;
	if (!tWatch.on || !tWatch.size || !n) return;
	tWatch.got += n;
	if (tWatch.got > tWatch.size) tWatch.got = tWatch.size;
	ten = (int)(tWatch.got * 1000ull / tWatch.size);
	if (ten > 999) ten = 999;
	now = GetTickCount();
	if (ten > 0 && ten < 990 && tWatch.tick && (DWORD)(now - tWatch.tick) < 80) return;
	tWatch.tick = now ? now : 1;
	OxzProgDetail(tWatch.name, (unsigned)(tWatch.i + 1), (unsigned)tWatch.n, ten, WatchOverall(ten));
}

HANDLE OxzCreateA(const std::wstring& path)
{
	OxzEnsureParentA(path);
	return CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

int OxzWriteA(HANDLE h, const void* p, DWORD n, uint32_t* crc, uint64_t* total)
{
	DWORD w = 0;
	if (n && (!WriteFile(h, p, n, &w, NULL) || w != n)) return 0;
	if (crc && n) *crc = crc32(*crc, (const Bytef*)p, n);
	if (total) *total += n;
	if (n) OxzPulseAdd(n);
	return 1;
}

void OxzStampA(HANDLE h, uint64_t ft)
{
	FILETIME t;
	if (!ft) return;
	t.dwLowDateTime = (DWORD)ft;
	t.dwHighDateTime = (DWORD)(ft >> 32);
	SetFileTime(h, &t, &t, &t);
}

uint32_t OxzCrcAdd(uint32_t c, const void* p, size_t n)
{
	return crc32(c, (const Bytef*)p, (uInt)n);
}

uint32_t OxzCrcFile(const std::wstring& path, int* ok)
{
	HANDLE h = OxzOpenReadA(path.c_str());
	unsigned char buf[1 << 16];
	uint32_t c = crc32(0, Z_NULL, 0);
	if (ok) *ok = 0;
	if (h == INVALID_HANDLE_VALUE) return 0;
	for (;;) {
		DWORD g = 0;
		if (!ReadFile(h, buf, sizeof(buf), &g, NULL)) { CloseHandle(h); return 0; }
		if (!g) break;
		c = crc32(c, buf, g);
	}
	CloseHandle(h);
	if (ok) *ok = 1;
	return c;
}

static uint64_t Oct(const char* s, int n)
{
	uint64_t v = 0;
	int i;
	for (i = 0; i < n; ++i) {
		if (s[i] == 0 || s[i] == ' ') continue;
		if (s[i] < '0' || s[i] > '7') break;
		v = (v << 3) + (unsigned)(s[i] - '0');
	}
	return v;
}

static int TarSum(const unsigned char* h)
{
	unsigned sum = 0, field = 0;
	int i;
	for (i = 0; i < 512; ++i) sum += h[i];
	for (i = 148; i < 156; ++i) field += h[i];
	sum = sum - field + 8 * (unsigned)' ';
	return sum == (unsigned)Oct((const char*)h + 148, 8);
}

struct Src {
	uint64_t pos;
	Src() : pos(0) {}
	virtual ~Src() {}
	virtual int Read(void* p, int n) = 0;
	virtual int Skip(uint64_t n)
	{
		unsigned char b[1 << 15];
		while (n) {
			int c = n > sizeof(b) ? (int)sizeof(b) : (int)n;
			int r = Read(b, c);
			if (r <= 0) return 0;
			n -= (uint64_t)r;
		}
		return 1;
	}
};

struct FileSrc : Src {
	HANDLE h;
	explicit FileSrc(HANDLE x) : h(x) {}
	int Read(void* p, int n) override
	{
		DWORD g = 0;
		if (n <= 0) return 0;
		if (!ReadFile(h, p, (DWORD)n, &g, NULL)) return -1;
		pos += g;
		return (int)g;
	}
	int Skip(uint64_t n) override
	{
		LARGE_INTEGER m, o;
		m.QuadPart = (LONGLONG)n;
		o.QuadPart = 0;
		if (!SetFilePointerEx(h, m, &o, FILE_CURRENT)) return 0;
		pos += n;
		return 1;
	}
};

struct GzSrc : Src {
	gzFile g;
	explicit GzSrc(const wchar_t* path) { g = gzopen_w(path, "rb"); }
	~GzSrc() override { if (g) gzclose(g); }
	int Read(void* p, int n) override
	{
		int r;
		if (!g || n <= 0) return 0;
		r = gzread(g, p, (unsigned)n);
		if (r < 0) return -1;
		pos += r;
		return r;
	}
};

struct BzSrc : Src {
	FILE* f;
	BZFILE* b;
	int ended;
	explicit BzSrc(const wchar_t* path) : ended(0)
	{
		int err = 0;
		f = _wfopen(path, L"rb");
		b = f ? BZ2_bzReadOpen(&err, f, 0, 0, NULL, 0) : NULL;
	}
	~BzSrc() override
	{
		int err = 0;
		if (b) BZ2_bzReadClose(&err, b);
		if (f) fclose(f);
	}
	int Read(void* p, int n) override
	{
		int err = 0, r;
		if (!b || n <= 0 || ended) return 0;
		r = BZ2_bzRead(&err, b, p, n);
		if (err == BZ_STREAM_END) ended = 1;
		else if (err != BZ_OK) return -1;
		if (r < 0) r = 0;
		pos += r;
		return r;
	}
};

struct ZstSrc : Src {
	HANDLE h;
	ZSTD_DCtx* d;
	std::vector<unsigned char> in;
	size_t have, used;
	int eof;
	explicit ZstSrc(const wchar_t* path) : h(OxzOpenReadA(path)), d(ZSTD_createDCtx()), have(0), used(0), eof(0)
	{
		in.resize(1 << 16);
		if (!d) eof = 1;
	}
	~ZstSrc() override
	{
		if (d) ZSTD_freeDCtx(d);
		if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	}
	int Read(void* p, int n) override
	{
		ZSTD_outBuffer o;
		int produced = 0;
		if (!d || h == INVALID_HANDLE_VALUE || n <= 0) return 0;
		o.dst = p;
		o.size = (size_t)n;
		o.pos = 0;
		while ((int)o.pos < n) {
			ZSTD_inBuffer ib;
			size_t r;
			if (used >= have && !eof) {
				DWORD g = 0;
				if (!ReadFile(h, in.data(), (DWORD)in.size(), &g, NULL)) return -1;
				have = g;
				used = 0;
				if (g < in.size()) eof = 1;
			}
			ib.src = in.data() + used;
			ib.size = have - used;
			ib.pos = 0;
			r = ZSTD_decompressStream(d, &o, &ib);
			used += ib.pos;
			if (ZSTD_isError(r)) return -1;
			if (o.pos == 0 && eof) break;
			if ((int)o.pos >= n) break;
			if (eof && ib.pos == ib.size && o.pos == (size_t)produced) break;
			produced = (int)o.pos;
		}
		pos += o.pos;
		return (int)o.pos;
	}
};

struct XzSrc : Src {
	HANDLE h;
	CXzUnpacker dec;
	ISzAlloc alloc;
	std::vector<unsigned char> in;
	size_t have, used;
	int eof, bad, finished;
	explicit XzSrc(const wchar_t* path) : h(OxzOpenReadA(path)), have(0), used(0), eof(0), bad(0), finished(0)
	{
		alloc.Alloc = SzAlloc;
		alloc.Free = SzFree;
		in.resize(1 << 16);
		XzUnpacker_Construct(&dec, &alloc);
		CrcGenerateTable();
		XzUnpacker_Init(&dec);
		if (h == INVALID_HANDLE_VALUE) bad = 1;
	}
	~XzSrc() override
	{
		XzUnpacker_Free(&dec);
		if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	}
	int Read(void* p, int n) override
	{
		int produced = 0;
		if (bad || n <= 0 || finished) return 0;
		while (produced < n && !finished) {
			SizeT outLen, inLen;
			ECoderStatus st;
			SRes r;
			size_t before;
			if (used >= have && !eof) {
				DWORD g = 0;
				DWORD want = (DWORD)in.size();
				if (!ReadFile(h, in.data(), want, &g, NULL)) return -1;
				have = g;
				used = 0;
				if (g < want) eof = 1;
			}
			before = used;
			outLen = (SizeT)(n - produced);
			inLen = (SizeT)(have - used);
			st = CODER_STATUS_NOT_SPECIFIED;
			r = XzUnpacker_Code(&dec, (Byte*)p + produced, &outLen, in.data() + used, &inLen, eof ? True : False, CODER_FINISH_ANY, &st);
			used += inLen;
			if (XzUnpacker_IsStreamWasFinished(&dec)) finished = 1;
			if (r != SZ_OK) {
				if (produced) break;
				return -1;
			}
			produced += (int)outLen;
			if (finished && outLen == 0) break;
			if (outLen == 0 && used == before) {
				if (finished) break;
				if (eof) {
					if (!produced) return -1;
					break;
				}
				if (used < have) break;
			}
		}
		pos += produced;
		return produced;
	}
};

static uint32_t Rotl32(uint32_t x, int r)
{
	return (x << r) | (x >> (32 - r));
}

static uint32_t Xx32(const void* data, size_t n, uint32_t seed)
{
	const unsigned char* p = (const unsigned char*)data;
	const unsigned char* end = p + n;
	uint32_t h = seed + 0x165667B1u + (uint32_t)n;
	while (p + 4 <= end) {
		uint32_t v;
		memcpy(&v, p, 4);
		h += v * 0xC2B2AE3Du;
		h = Rotl32(h, 17) * 0x27D4EB2Fu;
		p += 4;
	}
	while (p < end) {
		h += (uint32_t)(*p++) * 0x165667B1u;
		h = Rotl32(h, 11) * 0x9E3779B1u;
	}
	h ^= h >> 15;
	h *= 0x85EBCA77u;
	h ^= h >> 13;
	h *= 0xC2B2AE3Du;
	h ^= h >> 16;
	return h;
}

struct Lz4Src : Src {
	HANDLE h;
	std::vector<unsigned char> pending;
	std::vector<unsigned char> hist;
	size_t at;
	int bad, ended, blockChk, contentChk, linked;
	size_t blockMax;
	explicit Lz4Src(const wchar_t* path) : h(OxzOpenReadA(path)), at(0), bad(0), ended(0), blockChk(0), contentChk(0), linked(0), blockMax(0)
	{
		unsigned char mag[4], flg, bd, hc, desc[16];
		DWORD g = 0;
		size_t dn = 2;
		if (h == INVALID_HANDLE_VALUE) { bad = 1; return; }
		if (!ReadFile(h, mag, 4, &g, NULL) || g != 4 || mag[0] != 0x04 || mag[1] != 0x22 || mag[2] != 0x4D || mag[3] != 0x18) { bad = 1; return; }
		if (!ReadFile(h, &flg, 1, &g, NULL) || g != 1 || !ReadFile(h, &bd, 1, &g, NULL) || g != 1) { bad = 1; return; }
		if ((flg >> 6) != 1 || (flg & 0x02)) { bad = 1; return; }
		desc[0] = flg;
		desc[1] = bd;
		{
			int bsz = (bd >> 4) & 7;
			if (bsz < 4 || bsz > 7) { bad = 1; return; }
			blockMax = 1u << (8 + bsz * 2);
		}
		linked = (flg & 0x20) ? 0 : 1;
		blockChk = (flg & 0x10) ? 1 : 0;
		contentChk = (flg & 0x04) ? 1 : 0;
		if (flg & 0x08) {
			if (!ReadFile(h, desc + dn, 8, &g, NULL) || g != 8) { bad = 1; return; }
			dn += 8;
		}
		if (flg & 0x01) {
			if (!ReadFile(h, desc + dn, 4, &g, NULL) || g != 4) { bad = 1; return; }
			dn += 4;
		}
		if (!ReadFile(h, &hc, 1, &g, NULL) || g != 1 || hc != (unsigned char)(Xx32(desc, dn, 0) >> 8)) { bad = 1; return; }
	}
	~Lz4Src() override { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
	void Remember(const unsigned char* p, size_t n)
	{
		if (!linked || !n) return;
		if (n >= 65536) {
			hist.assign(p + n - 65536, p + n);
			return;
		}
		if (hist.size() + n > 65536) hist.erase(hist.begin(), hist.begin() + (hist.size() + n - 65536));
		hist.insert(hist.end(), p, p + n);
	}
	int PullBlock()
	{
		unsigned char szb[4];
		DWORD g = 0;
		uint32_t sz;
		int raw;
		if (ended || bad || h == INVALID_HANDLE_VALUE) return 0;
		if (!ReadFile(h, szb, 4, &g, NULL) || g != 4) { bad = 1; return -1; }
		sz = (uint32_t)szb[0] | ((uint32_t)szb[1] << 8) | ((uint32_t)szb[2] << 16) | ((uint32_t)szb[3] << 24);
		if (sz == 0) {
			if (contentChk) {
				unsigned char c[4];
				ReadFile(h, c, 4, &g, NULL);
			}
			ended = 1;
			return 0;
		}
		raw = (sz & 0x80000000u) ? 1 : 0;
		sz &= 0x7FFFFFFFu;
		if (sz == 0 || sz > blockMax) { bad = 1; return -1; }
		{
			std::vector<unsigned char> comp(sz), plain(blockMax);
			if (!ReadFile(h, comp.data(), sz, &g, NULL) || g != sz) { bad = 1; return -1; }
			if (blockChk) {
				unsigned char c[4];
				if (!ReadFile(h, c, 4, &g, NULL) || g != 4) { bad = 1; return -1; }
			}
			if (raw) {
				Remember(comp.data(), comp.size());
				pending.swap(comp);
			} else {
				int n = hist.empty()
					? LZ4_decompress_safe((const char*)comp.data(), (char*)plain.data(), (int)sz, (int)blockMax)
					: LZ4_decompress_safe_usingDict((const char*)comp.data(), (char*)plain.data(), (int)sz, (int)blockMax, (const char*)hist.data(), (int)hist.size());
				if (n < 0) { bad = 1; return -1; }
				plain.resize((size_t)n);
				Remember(plain.data(), plain.size());
				pending.swap(plain);
			}
			at = 0;
		}
		return 1;
	}
	int Read(void* p, int n) override
	{
		int got = 0;
		unsigned char* d = (unsigned char*)p;
		if (bad || n <= 0) return bad ? -1 : 0;
		while (got < n) {
			if (at >= pending.size()) {
				int pr = PullBlock();
				if (pr < 0) return -1;
				if (pr == 0) break;
			}
			{
				size_t c = pending.size() - at;
				if (c > (size_t)(n - got)) c = (size_t)(n - got);
				memcpy(d + got, pending.data() + at, c);
				at += c;
				got += (int)c;
			}
		}
		pos += (uint64_t)got;
		return got;
	}
};

static int ReadFull(Src& s, void* p, int n)
{
	int g = 0;
	unsigned char* b = (unsigned char*)p;
	while (g < n) {
		int r = s.Read(b + g, n - g);
		if (r < 0) return -1;
		if (r == 0) break;
		g += r;
	}
	return g;
}

static int TarStream(Src& s, int kind, const wchar_t* codec, const wchar_t* dest, const std::vector<OxzEnt>* only, std::vector<OxzEnt>* list, std::wstring& err, int base, int span)
{
	std::string longName;
	int index = 0, nfile = 1, done = 0;
	if (only) {
		nfile = 0;
		for (size_t i = 0; i < only->size(); ++i) if (!(*only)[i].dir) nfile++;
		if (nfile < 1) nfile = 1;
	}
	for (;;) {
		unsigned char block[512];
		int rd, zero = 1, i;
		uint64_t size, dataAt;
		char type;
		std::wstring name, rel;
		rd = ReadFull(s, block, 512);
		if (rd == 0) break;
		if (rd != 512) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
		for (i = 0; i < 512; ++i) if (block[i]) { zero = 0; break; }
		if (zero) break;
		if (!TarSum(block)) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
		size = Oct((const char*)block + 124, 12);
		type = (char)block[156];
		dataAt = s.pos;
		if (type == 'L' || type == 'K') {
			std::string raw;
			uint64_t left = size;
			while (left) {
				unsigned char tmp[512];
				int n = ReadFull(s, tmp, 512);
				unsigned take;
				if (n != 512) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
				take = left > 512 ? 512 : (unsigned)left;
				raw.append((char*)tmp, take);
				left -= take;
			}
			longName = raw.c_str();
			continue;
		}
		if (type == 'x' || type == 'g') {
			uint64_t skip = (size + 511) & ~511ull;
			if (skip && !s.Skip(skip)) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
			continue;
		}
		if (!longName.empty()) {
			name = OxzFromA(longName.c_str(), (int)longName.size(), CP_UTF8);
			longName.clear();
		} else {
			std::string raw((char*)block, strnlen((char*)block, 100));
			if (memcmp(block + 257, "ustar", 5) == 0) {
				std::string pre((char*)block + 345, strnlen((char*)block + 345, 155));
				if (!pre.empty()) raw = pre + "/" + raw;
			}
			name = OxzFromA(raw.c_str(), (int)raw.size(), CP_UTF8);
			if (name.empty()) name = OxzFromA(raw.c_str(), (int)raw.size(), CP_ACP);
		}
		for (size_t k = 0; k < name.size(); ++k) if (name[k] == L'/') name[k] = L'\\';
		if (!OxzSafeRelA(name.c_str(), rel)) {
			uint64_t skip = (size + 511) & ~511ull;
			if (skip && !s.Skip(skip)) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
			continue;
		}
		if (type == '5' || (!name.empty() && name.back() == L'\\')) {
			if (list) {
				OxzEnt e = Blank();
				e.rel = rel; e.origin = rel; e.dir = 1; e.kind = kind; e.codec = codec ? codec : L"";
				e.index = index++;
				list->push_back(e);
			}
			if (dest) OxzEnsureParentA(std::wstring(dest) + L"\\" + rel + L"\\x");
			{
				uint64_t skip = (size + 511) & ~511ull;
				if (skip && !s.Skip(skip)) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
			}
			continue;
		}
		if (type != '0' && type != '\0') {
			uint64_t skip = (size + 511) & ~511ull;
			if (skip && !s.Skip(skip)) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
			continue;
		}
		{
			const OxzEnt* hit = NULL;
			uint64_t unix = Oct((const char*)block + 136, 12);
			if (only) {
				for (size_t k = 0; k < only->size(); ++k)
					if (!(*only)[k].dir && (*only)[k].offset == dataAt) { hit = &(*only)[k]; break; }
			}
			if (list) {
				OxzEnt e = Blank();
				e.rel = rel; e.origin = rel; e.kind = kind; e.codec = codec ? codec : L"ustar";
				e.index = index++; e.size = size; e.packed = size; e.packedKnown = 1; e.offset = dataAt;
				if (unix) e.mtime = (unix * 10000000ull) + 116444736000000000ull;
				list->push_back(e);
			}
			if (dest && (!only || hit)) {
				std::wstring relOut = hit ? hit->rel : rel;
				std::wstring full = std::wstring(dest) + L"\\" + relOut;
				HANDLE o;
				uint64_t left = size;
				OxzNoteA(done, nfile, relOut.c_str(), base, span);
				OxzPulseSize(size);
				done++;
				o = OxzCreateA(full);
				if (o == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
				while (left) {
					unsigned char tmp[1 << 16];
					int want = left > sizeof(tmp) ? (int)sizeof(tmp) : (int)left;
					int n = ReadFull(s, tmp, want);
					if (n != want || !OxzWriteA(o, tmp, (DWORD)n, NULL, NULL)) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
					left -= (uint64_t)n;
				}
				if (unix) OxzStampA(o, (unix * 10000000ull) + 116444736000000000ull);
				CloseHandle(o);
				{
					uint64_t pad = (512 - (size & 511)) & 511;
					if (pad && !s.Skip(pad)) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
				}
			} else {
				uint64_t skip = (size + 511) & ~511ull;
				if (skip && !s.Skip(skip)) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
			}
		}
	}
	return 0;
}

static std::wstring StripOne(const wchar_t* path)
{
	std::wstring n = OxzBaseName(path);
	size_t d = n.find_last_of(L'.');
	if (d != std::wstring::npos && d > 0) n.resize(d);
	if (n.empty()) n = L"file";
	return n;
}

static int UseTar(Src& s, const wchar_t* path)
{
	unsigned char b[512];
	int n, tar;
	if (OxzNameIsTar(path)) return 1;
	n = ReadFull(s, b, 512);
	if (n < 0) return -1;
	tar = (n >= 262 && memcmp(b + 257, "ustar", 5) == 0);
	return tar;
}

static uint32_t GzISize(const wchar_t* path)
{
	HANDLE h = OxzOpenReadA(path);
	unsigned char b[4];
	uint64_t n;
	if (h == INVALID_HANDLE_VALUE) return 0;
	n = OxzLenA(h);
	if (n < 8 || !OxzSeekA(h, n - 4) || !OxzReadExactA(h, b, 4)) { CloseHandle(h); return 0; }
	CloseHandle(h);
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static int ListCompressed(const wchar_t* path, int plainKind, int tarKind, const wchar_t* codec, std::vector<OxzEnt>& out, std::wstring& err)
{
	int tar = OxzNameIsTar(path);
	if (!tar) {
		if (plainKind == EK_GZ) { GzSrc s(path); if (!s.g) { err = OxzTxt(OS_OPEN_FAIL); return 2; } tar = UseTar(s, path); }
		else if (plainKind == EK_BZ) { BzSrc s(path); if (!s.b) { err = OxzTxt(OS_OPEN_FAIL); return 2; } tar = UseTar(s, path); }
		else if (plainKind == EK_ZST) { ZstSrc s(path); if (s.h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; } tar = UseTar(s, path); }
		else if (plainKind == EK_LZ4) { Lz4Src s(path); if (s.bad) { err = OxzTxt(OS_OPEN_FAIL); return 2; } tar = UseTar(s, path); }
		else { XzSrc s(path); if (s.h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; } tar = UseTar(s, path); }
		if (tar < 0) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	}
	if (tar) {
		int rc;
		if (plainKind == EK_GZ) { GzSrc s(path); if (!s.g) { err = OxzTxt(OS_OPEN_FAIL); return 2; } rc = TarStream(s, tarKind, codec, NULL, NULL, &out, err, 0, 100); }
		else if (plainKind == EK_BZ) { BzSrc s(path); if (!s.b) { err = OxzTxt(OS_OPEN_FAIL); return 2; } rc = TarStream(s, tarKind, codec, NULL, NULL, &out, err, 0, 100); }
		else if (plainKind == EK_ZST) { ZstSrc s(path); if (s.h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; } rc = TarStream(s, tarKind, codec, NULL, NULL, &out, err, 0, 100); }
		else if (plainKind == EK_LZ4) { Lz4Src s(path); if (s.bad) { err = OxzTxt(OS_OPEN_FAIL); return 2; } rc = TarStream(s, tarKind, codec, NULL, NULL, &out, err, 0, 100); }
		else { XzSrc s(path); if (s.h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; } rc = TarStream(s, tarKind, codec, NULL, NULL, &out, err, 0, 100); }
		return rc;
	}
	{
		OxzEnt e = Blank();
		std::wstring rel;
		if (!OxzSafeRelA(StripOne(path).c_str(), rel)) rel = L"file";
		e.rel = rel;
		e.origin = rel;
		e.kind = plainKind;
		e.codec = codec;
		e.packedKnown = 1;
		e.packed = 0;
		{
			HANDLE h = OxzOpenReadA(path);
			if (h != INVALID_HANDLE_VALUE) { e.packed = OxzLenA(h); CloseHandle(h); }
		}
		if (plainKind == EK_GZ) e.size = GzISize(path);
		else if (plainKind == EK_ZST) {
			HANDLE h = OxzOpenReadA(path);
			unsigned char b[64];
			DWORD g = 0;
			if (h != INVALID_HANDLE_VALUE && ReadFile(h, b, 64, &g, NULL)) {
				unsigned long long sz = ZSTD_getFrameContentSize(b, g);
				if (sz != ZSTD_CONTENTSIZE_UNKNOWN && sz != ZSTD_CONTENTSIZE_ERROR) e.size = sz;
			}
			if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
		}
		out.push_back(e);
	}
	return 0;
}

static int TakeCompressed(const wchar_t* path, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span)
{
	if (ents.empty()) return 0;
	int k = ents[0].kind;
	if (k == EK_GZ || k == EK_BZ || k == EK_ZST || k == EK_XZ || k == EK_LZ4) {
		std::wstring full = std::wstring(dest) + L"\\" + ents[0].rel;
		HANDLE o = OxzCreateA(full);
		unsigned char buf[1 << 16];
		if (o == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
		OxzNoteA(0, 1, ents[0].rel.c_str(), base, span);
		OxzPulseSize(ents[0].size);
		if (k == EK_GZ) {
			GzSrc s(path);
			if (!s.g) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			for (;;) {
				int n = s.Read(buf, sizeof(buf));
				if (n < 0) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
				if (!n) break;
				if (!OxzWriteA(o, buf, (DWORD)n, NULL, NULL)) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			}
		} else if (k == EK_BZ) {
			BzSrc s(path);
			if (!s.b) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			for (;;) {
				int n = s.Read(buf, sizeof(buf));
				if (n < 0) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
				if (!n) break;
				if (!OxzWriteA(o, buf, (DWORD)n, NULL, NULL)) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			}
		} else if (k == EK_ZST) {
			ZstSrc s(path);
			if (s.h == INVALID_HANDLE_VALUE) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			for (;;) {
				int n = s.Read(buf, sizeof(buf));
				if (n < 0) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
				if (!n) break;
				if (!OxzWriteA(o, buf, (DWORD)n, NULL, NULL)) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			}
		} else if (k == EK_LZ4) {
			Lz4Src s(path);
			if (s.bad) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			for (;;) {
				int n = s.Read(buf, sizeof(buf));
				if (n < 0) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
				if (!n) break;
				if (!OxzWriteA(o, buf, (DWORD)n, NULL, NULL)) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			}
		} else {
			XzSrc s(path);
			if (s.h == INVALID_HANDLE_VALUE) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			for (;;) {
				int n = s.Read(buf, sizeof(buf));
				if (n < 0) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
				if (!n) break;
				if (!OxzWriteA(o, buf, (DWORD)n, NULL, NULL)) { CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			}
		}
		CloseHandle(o);
		return 0;
	}
	if (k == EK_GZTAR) { GzSrc s(path); if (!s.g) { err = OxzTxt(OS_OPEN_FAIL); return 2; } return TarStream(s, k, L"gzip", dest, &ents, NULL, err, base, span); }
	if (k == EK_BZTAR) { BzSrc s(path); if (!s.b) { err = OxzTxt(OS_OPEN_FAIL); return 2; } return TarStream(s, k, L"bzip2", dest, &ents, NULL, err, base, span); }
	if (k == EK_ZSTTAR) { ZstSrc s(path); if (s.h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; } return TarStream(s, k, L"zstd", dest, &ents, NULL, err, base, span); }
	if (k == EK_XZTAR) { XzSrc s(path); if (s.h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; } return TarStream(s, k, L"xz", dest, &ents, NULL, err, base, span); }
	if (k == EK_LZ4TAR) { Lz4Src s(path); if (s.bad) { err = OxzTxt(OS_OPEN_FAIL); return 2; } return TarStream(s, k, L"lz4", dest, &ents, NULL, err, base, span); }
	if (k == EK_TAR) {
		HANDLE h = OxzOpenReadA(path);
		int rc;
		if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
		FileSrc s(h);
		rc = TarStream(s, EK_TAR, L"ustar", dest, &ents, NULL, err, base, span);
		CloseHandle(h);
		return rc;
	}
	err = OxzTxt(OS_OPEN_FAIL);
	return 2;
}

static std::wstring IdName(const unsigned char* id, int n)
{
	if (n == 1 && id[0] == 0x21) return L"LZMA2";
	if (n == 3 && id[0] == 0x03 && id[1] == 0x01 && id[2] == 0x01) return L"LZMA";
	if (n == 3 && id[0] == 0x04 && id[1] == 0x01 && id[2] == 0x08) return L"Deflate";
	if (n == 3 && id[0] == 0x04 && id[1] == 0x02 && id[2] == 0x02) return L"bzip2";
	if (n == 3 && id[0] == 0x03 && id[1] == 0x04 && id[2] == 0x01) return L"PPMd";
	if (n == 1 && id[0] == 0x00) return L"Store";
	return L"7z";
}

static std::wstring FolderCodec(const CSzArEx& db, UInt32 folder)
{
	const Byte* p;
	const Byte* end;
	UInt32 num = 0, ci;
	std::wstring last = L"7z";
	if (folder == (UInt32)-1 || !db.db.CodersData || !db.db.FoCodersOffsets) return last;
	p = db.db.CodersData + db.db.FoCodersOffsets[folder];
	end = db.db.CodersData + db.db.FoCodersOffsets[folder + 1];
	while (p < end) {
		Byte c = *p++;
		if (c & 0x80) { num |= (UInt32)(c & 0x7f); if (!(c & 0x80)) break; num <<= 7; }
		else { num |= c; break; }
	}
	for (ci = 0; ci < num && p < end; ++ci) {
		Byte main;
		unsigned idSize, j;
		unsigned char id[8];
		if (p >= end) break;
		main = *p++;
		idSize = main & 0xF;
		if (p + idSize > end || idSize > 8) break;
		for (j = 0; j < idSize; ++j) id[j] = *p++;
		last = IdName(id, (int)idSize);
		if (main & 0x10) {
			/* skip two numbers */
			int t;
			for (t = 0; t < 2; ++t) {
				while (p < end) { Byte c = *p++; if (!(c & 0x80)) break; }
			}
		}
		if (main & 0x20) {
			while (p < end) { Byte c = *p++; if (!(c & 0x80)) break; }
			if (p < end) p++;
		}
	}
	return last;
}

static int Open7z(const wchar_t* arc, CFileInStream& archiveStream, CLookToRead2& look, CSzArEx& db, ISzAlloc& a, ISzAlloc& t, std::wstring& err)
{
	memset(&archiveStream, 0, sizeof(archiveStream));
	memset(&look, 0, sizeof(look));
	memset(&db, 0, sizeof(db));
	a.Alloc = SzAlloc; a.Free = SzFree; t.Alloc = SzAllocTemp; t.Free = SzFreeTemp;
	File_Construct(&archiveStream.file);
	FileInStream_CreateVTable(&archiveStream);
	archiveStream.wres = 0;
	if (InFile_OpenW(&archiveStream.file, arc) != 0) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	LookToRead2_CreateVTable(&look, False);
	look.buf = (Byte*)ISzAlloc_Alloc(&a, (size_t)1 << 18);
	if (!look.buf) { File_Close(&archiveStream.file); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	look.bufSize = (size_t)1 << 18;
	look.realStream = &archiveStream.vt;
	LookToRead2_INIT(&look);
	CrcGenerateTable();
	SzArEx_Init(&db);
	if (SzArEx_Open(&db, &look.vt, &a, &t) != SZ_OK) {
		SzArEx_Free(&db, &a);
		ISzAlloc_Free(&a, look.buf);
		File_Close(&archiveStream.file);
		err = OxzTxt(OS_OPEN_FAIL);
		return 2;
	}
	return 0;
}

static void Close7z(CFileInStream& archiveStream, CLookToRead2& look, CSzArEx& db, ISzAlloc& a)
{
	SzArEx_Free(&db, &a);
	if (look.buf) ISzAlloc_Free(&a, look.buf);
	File_Close(&archiveStream.file);
}

static int List7z(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err)
{
	CFileInStream as; CLookToRead2 look; CSzArEx db; ISzAlloc a, t;
	UInt32 i;
	if (Open7z(path, as, look, db, a, t, err)) return 2;
	for (i = 0; i < db.NumFiles; ++i) {
		size_t len = SzArEx_GetFileNameUtf16(&db, i, NULL);
		std::vector<wchar_t> name(len ? len : 1);
		std::wstring rel;
		OxzEnt e;
		FILETIME when;
		if (!len || len > 8192) continue;
		SzArEx_GetFileNameUtf16(&db, i, (UInt16*)name.data());
		for (wchar_t* p = name.data(); *p; ++p) if (*p == L'/') *p = L'\\';
		if (!OxzSafeRelA(name.data(), rel)) continue;
		e = Blank();
		e.rel = rel; e.origin = rel; e.index = (int)i; e.kind = EK_7Z;
		e.dir = SzArEx_IsDir(&db, i) ? 1 : 0;
		e.size = SzArEx_GetFileSize(&db, i);
		e.codec = L"7z";
		if (!e.dir && db.FileToFolder && db.FileToFolder[i] != (UInt32)-1) {
			UInt32 f = db.FileToFolder[i];
			e.codec = FolderCodec(db, f);
			if (db.FolderToFile && db.FolderToFile[f + 1] == db.FolderToFile[f] + 1 && db.db.FoStartPackStreamIndex && db.db.PackPositions) {
				UInt32 a = db.db.FoStartPackStreamIndex[f];
				UInt32 b = db.db.FoStartPackStreamIndex[f + 1];
				if (b >= a) { e.packed = db.db.PackPositions[b] - db.db.PackPositions[a]; e.packedKnown = 1; }
			}
		}
		if (SzBitWithVals_Check(&db.CRCs, i)) { e.crcBits = 32; e.crc = db.CRCs.Vals[i]; }
		if (SzBitWithVals_Check(&db.MTime, i)) e.mtime = ((uint64_t)db.MTime.Vals[i].High << 32) | db.MTime.Vals[i].Low;
		(void)when;
		out.push_back(e);
	}
	Close7z(as, look, db, a);
	return 0;
}

static void MaybeRip(const std::wstring& path)
{
	HANDLE h = OxzOpenReadA(path.c_str());
	uint64_t n;
	std::vector<unsigned char> b;
	if (h == INVALID_HANDLE_VALUE) return;
	n = OxzLenA(h);
	if (n < 2 || n > (uint64_t)SIZE_MAX) { CloseHandle(h); return; }
	b.resize((size_t)n);
	{
		size_t got = 0;
		while (got < b.size()) {
			DWORD chunk = (DWORD)((b.size() - got) > (1u << 20) ? (1u << 20) : (b.size() - got));
			DWORD g = 0;
			if (!ReadFile(h, b.data() + got, chunk, &g, NULL) || g == 0) { CloseHandle(h); return; }
			got += g;
		}
	}
	CloseHandle(h);
	if (b[0] != 'M' || b[1] != 'Z') return;
	OxzRipConvert(b.data(), b.size(), 0);
	h = OxzCreateA(path);
	if (h == INVALID_HANDLE_VALUE) return;
	{
		size_t off = 0;
		while (off < b.size()) {
			DWORD chunk = (DWORD)((b.size() - off) > (1u << 20) ? (1u << 20) : (b.size() - off));
			if (!OxzWriteA(h, b.data() + off, chunk, NULL, NULL)) break;
			off += chunk;
		}
	}
	CloseHandle(h);
}

static int Take7z(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span)
{
	CFileInStream as; CLookToRead2 look; CSzArEx db; ISzAlloc a, t;
	UInt32 block = 0xFFFFFFFF;
	Byte* buf = NULL;
	size_t bufSize = 0;
	int rip = OxzIsOxz(arc) ? 1 : 0;
	int nfile = 0, done = 0;
	std::vector<int> want(ents.size() ? 1 : 0);
	if (Open7z(arc, as, look, db, a, t, err)) return 2;
	want.assign(db.NumFiles, -1);
	for (size_t i = 0; i < ents.size(); ++i) {
		if (ents[i].dir) { OxzEnsureParentA(std::wstring(dest) + L"\\" + ents[i].rel + L"\\x"); continue; }
		if (ents[i].index >= 0 && (UInt32)ents[i].index < db.NumFiles) want[ents[i].index] = (int)i;
		nfile++;
	}
	if (nfile < 1) nfile = 1;
	CreateDirectoryW(dest, NULL);
	for (UInt32 i = 0; i < db.NumFiles; ++i) {
		size_t off = 0, outSize = 0;
		const OxzEnt* e;
		std::wstring full;
		HANDLE o;
		if (want[i] < 0) continue;
		e = &ents[want[i]];
		OxzNoteA(done, nfile, ents[want[i]].rel.c_str(), base, span);
		if (SzArEx_Extract(&db, &look.vt, i, &block, &buf, &bufSize, &off, &outSize, &a, &t) != SZ_OK) {
			if (buf) ISzAlloc_Free(&a, buf);
			Close7z(as, look, db, a);
			err = OxzTxt(OS_OPEN_FAIL);
			return 2;
		}
		full = std::wstring(dest) + L"\\" + e->rel;
		done++;
		OxzPulseSize(outSize);
		o = OxzCreateA(full);
		if (o == INVALID_HANDLE_VALUE) { if (buf) ISzAlloc_Free(&a, buf); Close7z(as, look, db, a); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		{
			size_t left = outSize;
			const Byte* src = buf + off;
			while (left) {
				DWORD chunk = (DWORD)(left > (1u << 20) ? (1u << 20) : left);
				if (!OxzWriteA(o, src, chunk, NULL, NULL)) { CloseHandle(o); if (buf) ISzAlloc_Free(&a, buf); Close7z(as, look, db, a); err = OxzTxt(OS_OPEN_FAIL); return 2; }
				src += chunk;
				left -= chunk;
			}
		}
		OxzStampA(o, e->mtime);
		CloseHandle(o);
		if (rip) MaybeRip(full);
	}
	if (buf) ISzAlloc_Free(&a, buf);
	Close7z(as, look, db, a);
	return 0;
}

static uint16_t Zu16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t Zu32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t Zu64(const unsigned char* p) { return (uint64_t)Zu32(p) | ((uint64_t)Zu32(p + 4) << 32); }

struct ZipItem {
	std::wstring rel;
	int method;
	int flags;
	uint32_t crc;
	uint64_t comp, size, local;
	uint64_t mtime;
	int dir;
};

static int ZipFind(HANDLE h, uint64_t& cdOff, uint64_t& cdSize, uint64_t& count)
{
	uint64_t len = OxzLenA(h);
	uint64_t win = len > 66000 ? 66000 : len;
	std::vector<unsigned char> b((size_t)win);
	int64_t i;
	if (!len || !OxzSeekA(h, len - win) || !OxzReadExactA(h, b.data(), (DWORD)win)) return 0;
	for (i = (int64_t)win - 22; i >= 0; --i) {
		if (b[(size_t)i] == 'P' && b[(size_t)i + 1] == 'K' && b[(size_t)i + 2] == 5 && b[(size_t)i + 3] == 6) {
			cdOff = Zu32(b.data() + i + 16);
			cdSize = Zu32(b.data() + i + 12);
			count = Zu16(b.data() + i + 10);
			if (cdOff == 0xffffffffu || count == 0xffffu) {
				if (i >= 20 && b[(size_t)i - 20] == 'P' && b[(size_t)i - 19] == 'K' && b[(size_t)i - 18] == 6 && b[(size_t)i - 17] == 7) {
					uint64_t z = Zu64(b.data() + i - 12);
					unsigned char zh[56];
					if (OxzSeekA(h, z) && OxzReadExactA(h, zh, 56) && zh[0] == 'P' && zh[1] == 'K' && zh[2] == 6 && zh[3] == 6) {
						count = Zu64(zh + 32);
						cdSize = Zu64(zh + 40);
						cdOff = Zu64(zh + 48);
					}
				}
			}
			return 1;
		}
	}
	return 0;
}

static const wchar_t* ZipMethod(int m)
{
	if (m == 0) return L"Store";
	if (m == 8) return L"Deflate";
	if (m == 9) return L"Deflate64";
	if (m == 12) return L"bzip2";
	if (m == 14) return L"LZMA";
	if (m == 93) return L"zstd";
	if (m == 95) return L"xz";
	return L"ZIP";
}

static int ZipListRaw(const wchar_t* path, std::vector<ZipItem>& items, std::wstring& err)
{
	HANDLE h = OxzOpenReadA(path);
	uint64_t cdOff = 0, cdSize = 0, count = 0, pos, end;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (!ZipFind(h, cdOff, cdSize, count)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	pos = cdOff;
	end = cdOff + cdSize;
	while (pos + 46 <= end) {
		unsigned char hd[46];
		ZipItem it;
		uint16_t nam, ext, cmt, flags, method;
		uint64_t comp, un, local;
		std::vector<char> name;
		std::wstring w, rel;
		if (!OxzSeekA(h, pos) || !OxzReadExactA(h, hd, 46)) break;
		if (hd[0] != 'P' || hd[1] != 'K' || hd[2] != 1 || hd[3] != 2) break;
		flags = Zu16(hd + 8);
		method = Zu16(hd + 10);
		comp = Zu32(hd + 20);
		un = Zu32(hd + 24);
		nam = Zu16(hd + 28);
		ext = Zu16(hd + 30);
		cmt = Zu16(hd + 32);
		local = Zu32(hd + 42);
		name.resize(nam ? nam : 1);
		if (nam && !OxzReadExactA(h, name.data(), nam)) break;
		if (ext) {
			std::vector<unsigned char> ex(ext);
			size_t p = 0;
			if (!OxzReadExactA(h, ex.data(), ext)) break;
			while (p + 4 <= ex.size()) {
				uint16_t id = Zu16(ex.data() + p), sz = Zu16(ex.data() + p + 2);
				p += 4;
				if (p + sz > ex.size()) break;
				if (id == 1) {
					size_t q = p;
					if (un == 0xffffffffu && q + 8 <= p + sz) { un = Zu64(ex.data() + q); q += 8; }
					if (comp == 0xffffffffu && q + 8 <= p + sz) { comp = Zu64(ex.data() + q); q += 8; }
					if (local == 0xffffffffu && q + 8 <= p + sz) local = Zu64(ex.data() + q);
				}
				p += sz;
			}
		}
		w = OxzFromA(name.data(), nam, (flags & 0x800) ? CP_UTF8 : CP_ACP);
		for (size_t i = 0; i < w.size(); ++i) if (w[i] == L'/') w[i] = L'\\';
		it.dir = (!w.empty() && w.back() == L'\\') || ((Zu32(hd + 38) & 0x10) != 0);
		if (OxzSafeRelA(w.c_str(), rel)) {
			it.rel = rel;
			it.method = method;
			it.flags = flags;
			it.crc = Zu32(hd + 16);
			it.comp = comp;
			it.size = un;
			it.local = local;
			it.mtime = OxzDosFt(Zu16(hd + 14), Zu16(hd + 12));
			items.push_back(it);
		}
		pos += 46ull + nam + ext + cmt;
	}
	CloseHandle(h);
	(void)count;
	return 0;
}

struct InflateJob {
	HANDLE in, out;
	uint64_t left;
	unsigned char* inbuf;
	size_t incap;
	uint32_t* crc;
	uint64_t* total;
	int err;
};

static unsigned BackIn(void* p, unsigned char** buf)
{
	InflateJob* j = (InflateJob*)p;
	DWORD n, g = 0;
	if (!j->left || j->err) return 0;
	n = j->left > j->incap ? (DWORD)j->incap : (DWORD)j->left;
	if (!ReadFile(j->in, j->inbuf, n, &g, NULL) || !g) return 0;
	j->left -= g;
	*buf = j->inbuf;
	return g;
}
static int BackOut(void* p, unsigned char* buf, unsigned len)
{
	InflateJob* j = (InflateJob*)p;
	if (!OxzWriteA(j->out, buf, len, j->crc, j->total)) { j->err = 1; return 1; }
	return 0;
}

static int InflateRaw(HANDLE in, uint64_t comp, HANDLE out, int window, uint32_t* crc, uint64_t* total)
{
	z_stream zs;
	std::vector<unsigned char> ib(1 << 16), ob(1 << 16);
	int ret;
	memset(&zs, 0, sizeof(zs));
	if (inflateInit2(&zs, window) != Z_OK) return 0;
	ret = Z_OK;
	while (ret != Z_STREAM_END) {
		if (!zs.avail_in) {
			DWORD n = comp > ib.size() ? (DWORD)ib.size() : (DWORD)comp;
			DWORD g = 0;
			if (!n) { ret = inflate(&zs, Z_FINISH); break; }
			if (!ReadFile(in, ib.data(), n, &g, NULL) || !g) { inflateEnd(&zs); return 0; }
			comp -= g;
			zs.next_in = ib.data();
			zs.avail_in = g;
		}
		zs.next_out = ob.data();
		zs.avail_out = (uInt)ob.size();
		ret = inflate(&zs, comp ? Z_NO_FLUSH : Z_NO_FLUSH);
		if (ret != Z_OK && ret != Z_STREAM_END) { inflateEnd(&zs); return 0; }
		if (!OxzWriteA(out, ob.data(), (DWORD)(ob.size() - zs.avail_out), crc, total)) { inflateEnd(&zs); return 0; }
		if (ret == Z_BUF_ERROR && !comp && !zs.avail_in) break;
	}
	inflateEnd(&zs);
	return 1;
}

static int Inflate64(HANDLE in, uint64_t comp, HANDLE out, uint32_t* crc, uint64_t* total)
{
	z_stream zs;
	InflateJob job;
	unsigned char* win;
	int r;
	memset(&zs, 0, sizeof(zs));
	win = (unsigned char*)malloc(65536);
	job.in = in; job.out = out; job.left = comp; job.incap = 1 << 16; job.crc = crc; job.total = total; job.err = 0;
	job.inbuf = (unsigned char*)malloc(job.incap);
	if (!win || !job.inbuf || inflateBack9Init(&zs, win) != Z_OK) { free(win); free(job.inbuf); return 0; }
	r = inflateBack9(&zs, BackIn, &job, BackOut, &job);
	inflateBack9End(&zs);
	free(win);
	free(job.inbuf);
	return r == Z_STREAM_END && !job.err;
}

static int CopySpan(HANDLE in, uint64_t n, HANDLE out, uint32_t* crc, uint64_t* total)
{
	unsigned char b[1 << 16];
	while (n) {
		DWORD want = n > sizeof(b) ? (DWORD)sizeof(b) : (DWORD)n;
		DWORD g = 0;
		if (!ReadFile(in, b, want, &g, NULL) || g != want) return 0;
		if (!OxzWriteA(out, b, g, crc, total)) return 0;
		n -= g;
	}
	return 1;
}

static int BzipSpan(HANDLE in, uint64_t n, HANDLE out, uint32_t* crc, uint64_t* total)
{
	bz_stream bs;
	std::vector<unsigned char> ib(1 << 16), ob(1 << 16);
	int ret;
	memset(&bs, 0, sizeof(bs));
	if (BZ2_bzDecompressInit(&bs, 0, 0) != BZ_OK) return 0;
	ret = BZ_OK;
	while (ret != BZ_STREAM_END) {
		if (!bs.avail_in) {
			DWORD want = n > ib.size() ? (DWORD)ib.size() : (DWORD)n;
			DWORD g = 0;
			if (!want) break;
			if (!ReadFile(in, ib.data(), want, &g, NULL) || !g) { BZ2_bzDecompressEnd(&bs); return 0; }
			n -= g;
			bs.next_in = (char*)ib.data();
			bs.avail_in = g;
		}
		bs.next_out = (char*)ob.data();
		bs.avail_out = (unsigned)ob.size();
		ret = BZ2_bzDecompress(&bs);
		if (ret != BZ_OK && ret != BZ_STREAM_END) { BZ2_bzDecompressEnd(&bs); return 0; }
		if (!OxzWriteA(out, ob.data(), (DWORD)(ob.size() - bs.avail_out), crc, total)) { BZ2_bzDecompressEnd(&bs); return 0; }
	}
	BZ2_bzDecompressEnd(&bs);
	return 1;
}

static int ZstdSpan(HANDLE in, uint64_t n, HANDLE out, uint32_t* crc, uint64_t* total)
{
	ZSTD_DCtx* d = ZSTD_createDCtx();
	std::vector<unsigned char> ib(1 << 16), ob(1 << 16);
	if (!d) return 0;
	while (n) {
		DWORD want = n > ib.size() ? (DWORD)ib.size() : (DWORD)n;
		DWORD g = 0;
		ZSTD_inBuffer inn;
		if (!ReadFile(in, ib.data(), want, &g, NULL) || !g) { ZSTD_freeDCtx(d); return 0; }
		n -= g;
		inn.src = ib.data(); inn.size = g; inn.pos = 0;
		while (inn.pos < inn.size) {
			ZSTD_outBuffer o;
			size_t r;
			o.dst = ob.data(); o.size = ob.size(); o.pos = 0;
			r = ZSTD_decompressStream(d, &o, &inn);
			if (ZSTD_isError(r)) { ZSTD_freeDCtx(d); return 0; }
			if (o.pos && !OxzWriteA(out, ob.data(), (DWORD)o.pos, crc, total)) { ZSTD_freeDCtx(d); return 0; }
			if (!o.pos && inn.pos < inn.size && r == 0) break;
		}
	}
	ZSTD_freeDCtx(d);
	return 1;
}

static int XzSpan(HANDLE in, uint64_t n, HANDLE out, uint32_t* crc, uint64_t* total)
{
	CXzUnpacker dec;
	ISzAlloc al;
	std::vector<unsigned char> ib(1 << 16), ob(1 << 16);
	al.Alloc = SzAlloc; al.Free = SzFree;
	XzUnpacker_Construct(&dec, &al);
	CrcGenerateTable();
	XzUnpacker_Init(&dec);
	while (n || 1) {
		DWORD want = n > ib.size() ? (DWORD)ib.size() : (DWORD)n;
		DWORD g = 0;
		SizeT inLen, outLen;
		ECoderStatus st;
		SRes r;
		int eof = 0;
		if (want) {
			if (!ReadFile(in, ib.data(), want, &g, NULL)) { XzUnpacker_Free(&dec); return 0; }
			n -= g;
			if (!g) eof = 1;
		} else eof = 1;
		inLen = g;
		outLen = ob.size();
		r = XzUnpacker_Code(&dec, ob.data(), &outLen, ib.data(), &inLen, eof, CODER_FINISH_END, &st);
		if (r != SZ_OK) { XzUnpacker_Free(&dec); return 0; }
		if (outLen && !OxzWriteA(out, ob.data(), (DWORD)outLen, crc, total)) { XzUnpacker_Free(&dec); return 0; }
		if (XzUnpacker_IsStreamWasFinished(&dec)) break;
		if (eof && !outLen) break;
	}
	XzUnpacker_Free(&dec);
	return 1;
}

static int LzmaSpan(HANDLE in, uint64_t n, HANDLE out, uint32_t* crc, uint64_t* total)
{
	unsigned char head[9];
	unsigned char props[5];
	uint64_t skip, left;
	CLzmaDec dec;
	ISzAlloc al;
	std::vector<unsigned char> ib(1 << 16), ob(1 << 16);
	if (n < 5) return 0;
	if (!OxzReadExactA(in, head, n >= 9 ? 9 : 5)) return 0;
	if (n >= 9 && Zu16(head + 2) == 5) {
		memcpy(props, head + 4, 5);
		skip = 9;
	} else {
		memcpy(props, head, 5);
		skip = 5;
		if (n >= 9) {
			LARGE_INTEGER back;
			back.QuadPart = -4;
			if (!SetFilePointerEx(in, back, NULL, FILE_CURRENT)) return 0;
		}
	}
	left = n - skip;
	al.Alloc = SzAlloc;
	al.Free = SzFree;
	LzmaDec_Construct(&dec);
	if (LzmaDec_Allocate(&dec, props, 5, &al) != SZ_OK) return 0;
	LzmaDec_Init(&dec);
	for (;;) {
		DWORD want = left > ib.size() ? (DWORD)ib.size() : (DWORD)left;
		DWORD g = 0;
		SizeT inSz, outSz;
		ELzmaStatus st;
		SRes r;
		if (want) {
			if (!ReadFile(in, ib.data(), want, &g, NULL)) { LzmaDec_Free(&dec, &al); return 0; }
			left -= g;
		}
		inSz = g;
		outSz = ob.size();
		r = LzmaDec_DecodeToBuf(&dec, ob.data(), &outSz, ib.data(), &inSz, left ? LZMA_FINISH_ANY : LZMA_FINISH_END, &st);
		if (outSz && !OxzWriteA(out, ob.data(), (DWORD)outSz, crc, total)) { LzmaDec_Free(&dec, &al); return 0; }
		if (r != SZ_OK) { LzmaDec_Free(&dec, &al); return 0; }
		if (st == LZMA_STATUS_FINISHED_WITH_MARK || (!left && st == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)) break;
		if (!left && !outSz) break;
		if (!g && !outSz) { LzmaDec_Free(&dec, &al); return 0; }
	}
	LzmaDec_Free(&dec, &al);
	return 1;
}

static int ZipOne(HANDLE h, const ZipItem& it, const std::wstring& full, uint32_t* crcOut, std::wstring& err)
{
	unsigned char lh[30];
	uint16_t nam, ext;
	uint64_t data;
	HANDLE o;
	uint32_t crc = 0;
	uint64_t total = 0;
	int ok = 0;
	if (it.flags & 1) { err = OxzTxt(OS_NEED_PW); return 0; }
	if (!OxzSeekA(h, it.local) || !OxzReadExactA(h, lh, 30)) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
	nam = Zu16(lh + 26);
	ext = Zu16(lh + 28);
	data = it.local + 30 + nam + ext;
	if (!OxzSeekA(h, data)) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
	o = OxzCreateA(full);
	if (o == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
	if (it.method == 0) ok = CopySpan(h, it.comp, o, &crc, &total);
	else if (it.method == 8) ok = InflateRaw(h, it.comp, o, -15, &crc, &total);
	else if (it.method == 9) ok = Inflate64(h, it.comp, o, &crc, &total);
	else if (it.method == 12) ok = BzipSpan(h, it.comp, o, &crc, &total);
	else if (it.method == 14) ok = LzmaSpan(h, it.comp, o, &crc, &total);
	else if (it.method == 93) ok = ZstdSpan(h, it.comp, o, &crc, &total);
	else if (it.method == 95) ok = XzSpan(h, it.comp, o, &crc, &total);
	else { wchar_t b[64]; _snwprintf_s(b, _TRUNCATE, L"ZIP %d", it.method); err = b; CloseHandle(o); return 0; }
	if (ok) OxzStampA(o, it.mtime);
	CloseHandle(o);
	if (!ok) { err = err.empty() ? OxzTxt(OS_OPEN_FAIL) : err; return 0; }
	if (crc != it.crc) { err = OxzTxt(OS_OPEN_FAIL); return 0; }
	if (crcOut) *crcOut = crc;
	return 1;
}

static int ListZip(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err)
{
	std::vector<ZipItem> items;
	int rc = ZipListRaw(path, items, err);
	wchar_t unk[32];
	if (rc) return rc;
	for (size_t i = 0; i < items.size(); ++i) {
		OxzEnt e = Blank();
		e.rel = items[i].rel;
		e.origin = items[i].rel;
		e.dir = items[i].dir;
		e.index = (int)i;
		e.kind = EK_ZIP;
		e.size = items[i].size;
		e.packed = items[i].comp;
		e.packedKnown = 1;
		e.crcBits = 32;
		e.crc = items[i].crc;
		e.offset = items[i].local;
		e.mtime = items[i].mtime;
		if (items[i].method == 0 || items[i].method == 8 || items[i].method == 9 || items[i].method == 12 || items[i].method == 14 || items[i].method == 93 || items[i].method == 95)
			e.codec = ZipMethod(items[i].method);
		else { _snwprintf_s(unk, _TRUNCATE, L"ZIP %d", items[i].method); e.codec = unk; }
		out.push_back(e);
	}
	return 0;
}

static int TakeZip(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span)
{
	std::vector<ZipItem> items;
	int nfile = 0, done = 0;
	if (ZipListRaw(arc, items, err)) return 2;
	CreateDirectoryW(dest, NULL);
	for (size_t i = 0; i < ents.size(); ++i) if (!ents[i].dir) nfile++;
	if (nfile < 1) nfile = 1;
	{
		HANDLE h = OxzOpenReadA(arc);
		if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
		for (size_t i = 0; i < ents.size(); ++i) {
			const ZipItem* it = NULL;
			if (ents[i].dir) { OxzEnsureParentA(std::wstring(dest) + L"\\" + ents[i].rel + L"\\x"); continue; }
			if (ents[i].index >= 0 && (size_t)ents[i].index < items.size() && _wcsicmp(items[ents[i].index].rel.c_str(), ents[i].origin.c_str()) == 0)
				it = &items[ents[i].index];
			if (!it) {
				for (size_t k = 0; k < items.size(); ++k)
					if (_wcsicmp(items[k].rel.c_str(), ents[i].origin.c_str()) == 0) { it = &items[k]; break; }
			}
			if (!it) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			OxzNoteA(done, nfile, ents[i].rel.c_str(), base, span);
			OxzPulseSize(it->size);
			done++;
			if (!ZipOne(h, *it, std::wstring(dest) + L"\\" + ents[i].rel, NULL, err)) { CloseHandle(h); return 2; }
		}
		CloseHandle(h);
	}
	return 0;
}

static int ListTarFile(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err)
{
	HANDLE h = OxzOpenReadA(path);
	int rc;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	FileSrc s(h);
	rc = TarStream(s, EK_TAR, L"ustar", NULL, NULL, &out, err, 0, 100);
	CloseHandle(h);
	return rc;
}

static int Detect(const wchar_t* path, unsigned char* b, DWORD cap, DWORD* n)
{
	HANDLE h = OxzOpenReadA(path);
	DWORD g = 0;
	*n = 0;
	if (h == INVALID_HANDLE_VALUE) return 0;
	if (!ReadFile(h, b, cap, &g, NULL)) { CloseHandle(h); return 0; }
	CloseHandle(h);
	*n = g;
	return 1;
}

int OxzListArchive(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err)
{
	unsigned char b[560];
	DWORD n = 0;
	int rc = 2;
	out.clear();
	if (!path) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	{
		std::wstring use;
		int temp = 0;
		if (OxzMaterializeOxz(path, use, &temp)) {
		OxzCoreItem* items = NULL;
		int count = 0;
		uint64_t bo = 0, bs = 0;
		int rd = OxzCoreReadDir(use.c_str(), &items, &count, &bo, &bs);
		OxzReleaseOxz(use, temp);
		if (rd != OXZ_CORE_OK) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
		for (int i = 0; i < count; ++i) {
			OxzEnt e;
			if (!items[i].name || items[i].tomb) continue;
			e = Blank();
			e.rel = items[i].name;
			e.origin = e.rel;
			e.dir = items[i].isDir;
			e.index = i;
			e.kind = EK_7Z;
			e.size = items[i].unpacked;
			e.packed = items[i].packed;
			e.packedKnown = items[i].isDir ? 0 : 1;
			e.mtime = items[i].mtime;
			e.codec = L"oxz";
			out.push_back(e);
		}
		OxzCoreFreeItems(items, count);
		OxzAddDirsA(out);
		return 0;
		}
	}
	if (!Detect(path, b, sizeof(b), &n)) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (n >= 6 && b[0] == 0x37 && b[1] == 0x7A && b[2] == 0xBC && b[3] == 0xAF && b[4] == 0x27 && b[5] == 0x1C) rc = List7z(path, out, err);
	else if (n >= 4 && b[0] == 'P' && b[1] == 'K') rc = ListZip(path, out, err);
	else if (n >= 2 && b[0] == 0x1F && b[1] == 0x8B) rc = ListCompressed(path, EK_GZ, EK_GZTAR, L"gzip", out, err);
	else if (n >= 4 && memcmp(b, "Rar!", 4) == 0) rc = OxzListRar(path, out, err);
	else if (n >= 262 && memcmp(b + 257, "ustar", 5) == 0) rc = ListTarFile(path, out, err);
	else if (n >= 3 && memcmp(b, "BZh", 3) == 0) rc = ListCompressed(path, EK_BZ, EK_BZTAR, L"bzip2", out, err);
	else if (n >= 6 && b[0] == 0xFD && memcmp(b + 1, "7zXZ", 4) == 0) rc = ListCompressed(path, EK_XZ, EK_XZTAR, L"xz", out, err);
	else if (n >= 4 && b[0] == 0x28 && b[1] == 0xB5 && b[2] == 0x2F && b[3] == 0xFD) rc = ListCompressed(path, EK_ZST, EK_ZSTTAR, L"zstd", out, err);
	else if (n >= 4 && b[0] == 0x04 && b[1] == 0x22 && b[2] == 0x4D && b[3] == 0x18) rc = ListCompressed(path, EK_LZ4, EK_LZ4TAR, L"lz4", out, err);
	else if (n >= 4 && memcmp(b, "MSCF", 4) == 0) rc = OxzListCab(path, out, err);
	else if (n >= 8 && memcmp(b, "MSWIM", 5) == 0) rc = OxzWimList(path, out, err);
	else if (n >= 2 && b[0] == 0x60 && b[1] == 0xEA) rc = OxzListArj(path, out, err);
	else if (OxzEndsI(path, L".lzh") || OxzEndsI(path, L".lha")) rc = OxzListLzh(path, out, err);
	else if (OxzEndsI(path, L".iso")) rc = OxzListIso(path, out, err);
	else if (OxzEndsI(path, L".tar")) rc = ListTarFile(path, out, err);
	else { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (rc == 0) OxzAddDirsA(out);
	return rc;
}

static int TakeAll(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span)
{
	int k;
	if (ents.empty()) { CreateDirectoryW(dest, NULL); return 0; }
	k = ents[0].kind;
	CreateDirectoryW(dest, NULL);
	if (k == EK_7Z) return Take7z(arc, dest, ents, err, base, span);
	if (k == EK_ZIP) return TakeZip(arc, dest, ents, err, base, span);
	if (k == EK_RAR) return OxzTakeRar(arc, dest, ents, err, base, span);
	if (k == EK_CAB) return OxzTakeCab(arc, dest, ents, err, base, span);
	if (k == EK_LZH) return OxzTakeLzh(arc, dest, ents, err, base, span);
	if (k == EK_ISO) return OxzTakeIso(arc, dest, ents, err, base, span);
	if (k == EK_WIM) return OxzWimTake(arc, dest, ents, err, base, span);
	if (k == EK_ARJ) return OxzTakeArj(arc, dest, ents, err, base, span);
	if (k == EK_TAR || k == EK_GZ || k == EK_BZ || k == EK_ZST || k == EK_XZ || k == EK_LZ4 || k == EK_GZTAR || k == EK_BZTAR || k == EK_ZSTTAR || k == EK_XZTAR || k == EK_LZ4TAR)
		return TakeCompressed(arc, dest, ents, err, base, span);
	err = OxzTxt(OS_OPEN_FAIL);
	return 2;
}

int OxzExtractInProc(const wchar_t* arc, const wchar_t* dest, std::wstring& err, int base, int span)
{
	std::vector<OxzEnt> ents;
	int rc;
	if (OxzCoreIsPkg(arc)) return 3;
	rc = OxzListArchive(arc, ents, err);
	if (rc) return rc;
	return TakeAll(arc, dest, ents, err, base, span);
}

int OxzMaterialize(const wchar_t* arc, const wchar_t* work, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span)
{
	std::vector<OxzEnt> need;
	if (!arc || !work) return 0;
	if (!OxzCoreIsPkg(arc) && OxzIsOxzFamily(arc)) return OxzExtractTo(arc, work, err, base, span);
	CreateDirectoryW(work, NULL);
	for (size_t i = 0; i < ents.size(); ++i) {
		std::wstring full = std::wstring(work) + L"\\" + ents[i].rel;
		DWORD a = GetFileAttributesW(full.c_str());
		if (ents[i].dir) { OxzEnsureParentA(full + L"\\x"); continue; }
		if (a != INVALID_FILE_ATTRIBUTES) continue;
		need.push_back(ents[i]);
	}
	if (need.empty()) return 0;
	return TakeAll(arc, work, need, err, base, span);
}

int OxzTestArchive(const wchar_t* arc, std::vector<OxzTestRow>& rows, std::wstring& err, int base, int span)
{
	std::wstring tmp;
	std::vector<OxzEnt> ents;
	int rc, bad = 0;
	rows.clear();
	if (OxzIsOxzFamily(arc)) {
		tmp = OxzNewWork();
		rc = OxzExtractTo(arc, tmp.c_str(), err, base, span);
		{
			std::vector<OxzEnt> ents;
			std::wstring e2;
			if (OxzListArchive(arc, ents, e2) == 0) {
				for (size_t i = 0; i < ents.size(); ++i) {
					OxzTestRow row;
					if (ents[i].dir) continue;
					row.name = ents[i].rel;
					row.ok = rc == 0;
					row.detail = rc == 0 ? L"OK" : err;
					rows.push_back(row);
				}
			}
		}
		OxzRmTree(tmp);
		if (rc && rows.empty()) { OxzTestRow row; row.name = arc; row.ok = 0; row.detail = err; rows.push_back(row); }
		return rc ? 2 : 0;
	}
	rc = OxzListArchive(arc, ents, err);
	if (rc) { OxzTestRow row; row.name = arc ? arc : L""; row.ok = 0; row.detail = err.empty() ? OxzTxt(OS_OPEN_FAIL) : err; rows.push_back(row); return rc; }
	tmp = OxzNewWork();
	rc = TakeAll(arc, tmp.c_str(), ents, err, base, span);
	for (size_t i = 0; i < ents.size(); ++i) {
		OxzTestRow row;
		std::wstring full;
		if (ents[i].dir) continue;
		row.name = ents[i].rel;
		full = tmp + L"\\" + ents[i].rel;
		if (rc) { row.ok = 0; row.detail = err.empty() ? OxzTxt(OS_TEST_FAIL) : err; bad++; }
		else if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) { row.ok = 0; row.detail = OxzTxt(OS_TEST_FAIL); bad++; }
		else if (ents[i].crcBits == 32) {
			int okf = 0;
			uint32_t c = OxzCrcFile(full, &okf);
			if (!okf || c != ents[i].crc) { row.ok = 0; row.detail = OxzTxt(OS_TEST_FAIL); bad++; }
			else { row.ok = 1; row.detail = L"OK"; }
		} else { row.ok = 1; row.detail = L"OK"; }
		rows.push_back(row);
	}
	OxzRmTree(tmp);
	if (bad) { err = OxzTxt(OS_TEST_FAIL); return 2; }
	err.clear();
	return rc ? 2 : 0;
}

struct DiskFile { std::wstring rel; int dir; uint64_t size; FILETIME ft; };

static void WalkDisk(const std::wstring& root, const std::wstring& rel, std::vector<DiskFile>& out)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((root + L"\\" + rel + (rel.empty() ? L"*" : L"\\*")).c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		DiskFile f;
		if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
		f.rel = rel.empty() ? fd.cFileName : (rel + L"\\" + fd.cFileName);
		f.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
		f.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
		f.ft = fd.ftLastWriteTime;
		out.push_back(f);
		if (f.dir) WalkDisk(root, f.rel, out);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static std::string Utf8(const std::wstring& w)
{
	if (w.empty()) return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	std::string s(n, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}

int OxzWriteZip(const std::wstring& work, const std::wstring& archive, unsigned long long& bytes, std::wstring& err, int base, int span)
{
	std::vector<DiskFile> files;
	zlib_filefunc64_def ff;
	zipFile z;
	int n = 0, i = 0;
	bytes = 0;
	WalkDisk(work, L"", files);
	for (size_t k = 0; k < files.size(); ++k) if (!files[k].dir) n++;
	if (n < 1) n = 1;
	fill_win32_filefunc64W(&ff);
	z = zipOpen2_64(archive.c_str(), APPEND_STATUS_CREATE, NULL, &ff);
	if (!z) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	for (size_t k = 0; k < files.size(); ++k) {
		zip_fileinfo zi;
		std::string name = Utf8(files[k].rel);
		FILETIME loc;
		WORD d = 0, t = 0;
		HANDLE h;
		std::vector<unsigned char> buf(1 << 16);
		memset(&zi, 0, sizeof(zi));
		if (FileTimeToLocalFileTime(&files[k].ft, &loc)) FileTimeToDosDateTime(&loc, &d, &t);
		zi.dosDate = ((uLong)d << 16) | t;
		for (size_t c = 0; c < name.size(); ++c) if (name[c] == '\\') name[c] = '/';
		if (files[k].dir) {
			if (!name.empty() && name.back() != '/') name.push_back('/');
			if (zipOpenNewFileInZip4_64(z, name.c_str(), &zi, NULL, 0, NULL, 0, NULL, 0, 0, 0, -15, 8, Z_DEFAULT_STRATEGY, NULL, 0, 45, 0x800, 1) != ZIP_OK) { zipClose(z, NULL); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			zipCloseFileInZip(z);
			continue;
		}
		OxzNoteA(i, n, files[k].rel.c_str(), base, span);
		OxzPulseSize(files[k].size);
		i++;
		if (zipOpenNewFileInZip4_64(z, name.c_str(), &zi, NULL, 0, NULL, 0, NULL, Z_DEFLATED, 6, 0, -15, 8, Z_DEFAULT_STRATEGY, NULL, 0, 45, 0x800, 1) != ZIP_OK) { zipClose(z, NULL); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		h = OxzOpenReadA((work + L"\\" + files[k].rel).c_str());
		if (h == INVALID_HANDLE_VALUE) { zipClose(z, NULL); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		for (;;) {
			DWORD g = 0;
			if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &g, NULL)) { CloseHandle(h); zipClose(z, NULL); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			if (!g) break;
			if (zipWriteInFileInZip(z, buf.data(), g) != ZIP_OK) { CloseHandle(h); zipClose(z, NULL); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			OxzPulseAdd(g);
		}
		CloseHandle(h);
		if (zipCloseFileInZip(z) != ZIP_OK) { zipClose(z, NULL); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	}
	if (zipClose(z, NULL) != ZIP_OK) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	{
		HANDLE h = OxzOpenReadA(archive.c_str());
		if (h != INVALID_HANDLE_VALUE) { bytes = OxzLenA(h); CloseHandle(h); }
	}
	return 0;
}

static void TarOct(char* d, int n, uint64_t v)
{
	char tmp[32];
	memset(d, '0', n);
	_snprintf_s(tmp, _TRUNCATE, "%0*llo", n - 1, (unsigned long long)v);
	if ((int)strlen(tmp) <= n - 1) memcpy(d, tmp, strlen(tmp));
	d[n - 1] = 0;
}

struct TarOut {
	virtual ~TarOut() {}
	virtual int Write(const void* p, unsigned n) = 0;
	virtual int Finish(unsigned long long& bytes) = 0;
};

struct FileOut : TarOut {
	HANDLE h;
	explicit FileOut(const wchar_t* path) : h(OxzCreateA(path)) {}
	~FileOut() override { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
	int Write(const void* p, unsigned n) override { return h != INVALID_HANDLE_VALUE && OxzWriteA(h, p, n, NULL, NULL); }
	int Finish(unsigned long long& bytes) override
	{
		if (h == INVALID_HANDLE_VALUE) return 0;
		bytes = OxzLenA(h);
		CloseHandle(h);
		h = INVALID_HANDLE_VALUE;
		return 1;
	}
};

struct GzOut : TarOut {
	gzFile g;
	std::wstring path;
	explicit GzOut(const std::wstring& p) : g(gzopen_w(p.c_str(), "wb6")), path(p) {}
	~GzOut() override { if (g) gzclose(g); }
	int Write(const void* p, unsigned n) override
	{
		const unsigned char* b = (const unsigned char*)p;
		if (!g) return 0;
		if (n) OxzPulseAdd(n);
		while (n) {
			unsigned c = n > (1u << 20) ? (1u << 20) : n;
			int w = gzwrite(g, b, c);
			if (w <= 0) return 0;
			b += (unsigned)w;
			n -= (unsigned)w;
		}
		return 1;
	}
	int Finish(unsigned long long& bytes) override
	{
		int rc;
		HANDLE h;
		if (!g) return 0;
		rc = gzclose(g);
		g = NULL;
		if (rc != Z_OK) return 0;
		h = OxzOpenReadA(path.c_str());
		if (h == INVALID_HANDLE_VALUE) return 0;
		bytes = OxzLenA(h);
		CloseHandle(h);
		return 1;
	}
};

struct BzOut : TarOut {
	FILE* f;
	BZFILE* b;
	int bz;
	std::wstring path;
	explicit BzOut(const std::wstring& p) : f(_wfopen(p.c_str(), L"wb")), b(NULL), bz(0), path(p)
	{
		if (f) b = BZ2_bzWriteOpen(&bz, f, 6, 0, 0);
	}
	~BzOut() override
	{
		if (b) BZ2_bzWriteClose(&bz, b, 1, NULL, NULL);
		if (f) fclose(f);
	}
	int Write(const void* p, unsigned n) override
	{
		if (!b) return 0;
		if (n) OxzPulseAdd(n);
		BZ2_bzWrite(&bz, b, (void*)p, (int)n);
		return bz == BZ_OK;
	}
	int Finish(unsigned long long& bytes) override
	{
		unsigned int nin = 0, nout = 0;
		HANDLE h;
		if (!b) return 0;
		BZ2_bzWriteClose(&bz, b, 0, &nin, &nout);
		b = NULL;
		if (f) { fclose(f); f = NULL; }
		if (bz != BZ_OK) return 0;
		h = OxzOpenReadA(path.c_str());
		if (h == INVALID_HANDLE_VALUE) return 0;
		bytes = OxzLenA(h);
		CloseHandle(h);
		return 1;
	}
};

struct ZstOut : TarOut {
	HANDLE h;
	ZSTD_CCtx* c;
	std::vector<unsigned char> out;
	int ok;
	explicit ZstOut(const wchar_t* path) : h(OxzCreateA(path)), c(ZSTD_createCCtx()), out(1 << 16), ok(0)
	{
		if (h != INVALID_HANDLE_VALUE && c && !ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, 6))) ok = 1;
	}
	~ZstOut() override
	{
		if (c) ZSTD_freeCCtx(c);
		if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	}
	int Pump(ZSTD_inBuffer* in, ZSTD_EndDirective end)
	{
		for (;;) {
			ZSTD_outBuffer o;
			size_t r;
			if (!ok) return 0;
			o.dst = out.data();
			o.size = out.size();
			o.pos = 0;
			r = ZSTD_compressStream2(c, &o, in, end);
			if (ZSTD_isError(r)) { ok = 0; return 0; }
			if (o.pos && !OxzWriteA(h, out.data(), (DWORD)o.pos, NULL, NULL)) { ok = 0; return 0; }
			if (end == ZSTD_e_end) { if (r == 0) return 1; }
			else if (in->pos == in->size) return 1;
		}
	}
	int Write(const void* p, unsigned n) override
	{
		ZSTD_inBuffer in;
		if (n) OxzPulseAdd(n);
		in.src = p;
		in.size = n;
		in.pos = 0;
		return Pump(&in, ZSTD_e_continue);
	}
	int Finish(unsigned long long& bytes) override
	{
		ZSTD_inBuffer in;
		in.src = NULL;
		in.size = 0;
		in.pos = 0;
		if (!Pump(&in, ZSTD_e_end)) return 0;
		bytes = OxzLenA(h);
		CloseHandle(h);
		h = INVALID_HANDLE_VALUE;
		return 1;
	}
};

struct Lz4Out : TarOut {
	HANDLE h;
	std::vector<unsigned char> buf;
	int ok;
	explicit Lz4Out(const wchar_t* path) : h(OxzCreateA(path)), ok(h != INVALID_HANDLE_VALUE)
	{
		unsigned char hdr[7], body[2];
		body[0] = 0x60;
		body[1] = 0x50;
		hdr[0] = 0x04; hdr[1] = 0x22; hdr[2] = 0x4D; hdr[3] = 0x18;
		hdr[4] = body[0]; hdr[5] = body[1];
		hdr[6] = (unsigned char)(Xx32(body, 2, 0) >> 8);
		if (ok && !OxzWriteA(h, hdr, 7, NULL, NULL)) ok = 0;
	}
	~Lz4Out() override { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
	int FlushBlock()
	{
		unsigned char szb[4];
		std::vector<unsigned char> comp;
		const unsigned char* src;
		uint32_t sz;
		int n, bound;
		if (buf.empty()) return 1;
		bound = LZ4_compressBound((int)buf.size());
		comp.resize((size_t)bound);
		n = LZ4_compress_default((const char*)buf.data(), (char*)comp.data(), (int)buf.size(), bound);
		if (n <= 0 || n >= (int)buf.size()) {
			sz = (uint32_t)buf.size() | 0x80000000u;
			src = buf.data();
			n = (int)buf.size();
		} else {
			sz = (uint32_t)n;
			src = comp.data();
		}
		szb[0] = (unsigned char)sz;
		szb[1] = (unsigned char)(sz >> 8);
		szb[2] = (unsigned char)(sz >> 16);
		szb[3] = (unsigned char)(sz >> 24);
		if (!OxzWriteA(h, szb, 4, NULL, NULL) || !OxzWriteA(h, src, (DWORD)n, NULL, NULL)) return 0;
		buf.clear();
		return 1;
	}
	int Write(const void* p, unsigned n) override
	{
		const unsigned char* b = (const unsigned char*)p;
		if (!ok) return 0;
		if (n) OxzPulseAdd(n);
		while (n) {
			unsigned room = (unsigned)((256 * 1024) - buf.size());
			unsigned c = n < room ? n : room;
			buf.insert(buf.end(), b, b + c);
			b += c;
			n -= c;
			if (buf.size() == 256 * 1024 && !FlushBlock()) { ok = 0; return 0; }
		}
		return 1;
	}
	int Finish(unsigned long long& bytes) override
	{
		unsigned char z[4] = { 0, 0, 0, 0 };
		if (!ok || !FlushBlock() || !OxzWriteA(h, z, 4, NULL, NULL)) return 0;
		bytes = OxzLenA(h);
		CloseHandle(h);
		h = INVALID_HANDLE_VALUE;
		return 1;
	}
};

struct SeqIn {
	ISeqInStream vt;
	HANDLE h;
};
struct SeqOut {
	ISeqOutStream vt;
	HANDLE h;
};
static SRes SeqInRead(const ISeqInStream* pp, void* buf, size_t* size)
{
	SeqIn* s = (SeqIn*)pp;
	DWORD g = 0;
	DWORD want;
	if (!size) return SZ_ERROR_PARAM;
	if (*size == 0) return SZ_OK;
	want = *size > 0x100000u ? 0x100000u : (DWORD)*size;
	if (!ReadFile(s->h, buf, want, &g, NULL)) return SZ_ERROR_READ;
	*size = g;
	return SZ_OK;
}
static size_t SeqOutWrite(const ISeqOutStream* pp, const void* buf, size_t size)
{
	SeqOut* s = (SeqOut*)pp;
	const unsigned char* b = (const unsigned char*)buf;
	size_t left = size;
	while (left) {
		DWORD c = left > 0x100000u ? 0x100000u : (DWORD)left;
		DWORD g = 0;
		if (!WriteFile(s->h, b, c, &g, NULL) || g != c) return size - left;
		b += c;
		left -= c;
	}
	return size;
}

static int WriteXzFile(const wchar_t* src, const wchar_t* dest)
{
	SeqIn in;
	SeqOut out;
	CXzProps props;
	SRes r;
	in.vt.Read = SeqInRead;
	out.vt.Write = SeqOutWrite;
	in.h = OxzOpenReadA(src);
	out.h = OxzCreateA(dest);
	if (in.h == INVALID_HANDLE_VALUE || out.h == INVALID_HANDLE_VALUE) {
		if (in.h != INVALID_HANDLE_VALUE) CloseHandle(in.h);
		if (out.h != INVALID_HANDLE_VALUE) CloseHandle(out.h);
		return 0;
	}
	XzProps_Init(&props);
	props.lzma2Props.lzmaProps.level = 6;
	CrcGenerateTable();
	r = Xz_Encode(&out.vt, &in.vt, &props, NULL);
	CloseHandle(in.h);
	CloseHandle(out.h);
	return r == SZ_OK;
}

static int PutOut(TarOut& o, const void* p, size_t n)
{
	const unsigned char* b = (const unsigned char*)p;
	while (n) {
		unsigned c = n > 0x100000u ? 0x100000u : (unsigned)n;
		if (!o.Write(b, c)) return 0;
		b += c;
		n -= c;
	}
	return 1;
}

int OxzWriteTar(const std::wstring& work, const std::wstring& archive, unsigned long long& bytes, std::wstring& err, int base, int span)
{
	std::vector<DiskFile> files;
	std::unique_ptr<TarOut> out;
	std::wstring temp;
	int wrap, n = 0, i = 0;
	bytes = 0;
	wrap = OxzTarWrap(archive.c_str());
	if (wrap < 0) wrap = 0;
	WalkDisk(work, L"", files);
	for (size_t k = 0; k < files.size(); ++k) if (!files[k].dir) n++;
	if (n < 1) n = 1;
	if (wrap == 3) {
		temp = archive + L".plain";
		out.reset(new FileOut(temp.c_str()));
	} else if (wrap == 1) out.reset(new GzOut(archive));
	else if (wrap == 2) out.reset(new BzOut(archive));
	else if (wrap == 4) out.reset(new ZstOut(archive.c_str()));
	else if (wrap == 5) out.reset(new Lz4Out(archive.c_str()));
	else out.reset(new FileOut(archive.c_str()));
	for (size_t k = 0; k < files.size(); ++k) {
		unsigned char block[512];
		std::string name = Utf8(files[k].rel);
		unsigned sum;
		uint64_t unix;
		for (size_t c = 0; c < name.size(); ++c) if (name[c] == '\\') name[c] = '/';
		if (files[k].dir && (name.empty() || name.back() != '/')) name.push_back('/');
		if (name.size() > 100) {
			unsigned char lhdr[512];
			std::string raw = name;
			uint64_t left;
			memset(lhdr, 0, 512);
			memcpy(lhdr, "././@LongLink", 13);
			TarOct((char*)lhdr + 124, 12, raw.size() + 1);
			lhdr[156] = 'L';
			memset(lhdr + 148, ' ', 8);
			sum = 0;
			for (int b = 0; b < 512; ++b) sum += lhdr[b];
			_snprintf_s((char*)lhdr + 148, 8, _TRUNCATE, "%06o", sum);
			if (!PutOut(*out, lhdr, 512)) { out.reset(); DeleteFileW(archive.c_str()); if (!temp.empty()) DeleteFileW(temp.c_str()); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			left = (raw.size() + 1 + 511) & ~511ull;
			{
				std::vector<unsigned char> pad((size_t)left);
				memset(pad.data(), 0, pad.size());
				memcpy(pad.data(), raw.c_str(), raw.size());
				if (!PutOut(*out, pad.data(), pad.size())) { out.reset(); DeleteFileW(archive.c_str()); if (!temp.empty()) DeleteFileW(temp.c_str()); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			}
		}
		memset(block, 0, 512);
		memcpy(block, name.c_str(), name.size() > 100 ? 100 : name.size());
		memcpy(block + 100, "0000644", 7);
		memcpy(block + 108, "0000000", 7);
		memcpy(block + 116, "0000000", 7);
		TarOct((char*)block + 124, 12, files[k].dir ? 0 : files[k].size);
		unix = 0;
		if (files[k].ft.dwLowDateTime || files[k].ft.dwHighDateTime) {
			uint64_t ft = ((uint64_t)files[k].ft.dwHighDateTime << 32) | files[k].ft.dwLowDateTime;
			if (ft > 116444736000000000ull) unix = (ft - 116444736000000000ull) / 10000000ull;
		}
		TarOct((char*)block + 136, 12, unix);
		block[156] = files[k].dir ? '5' : '0';
		memcpy(block + 257, "ustar", 5);
		memcpy(block + 263, "00", 2);
		memset(block + 148, ' ', 8);
		sum = 0;
		for (int b = 0; b < 512; ++b) sum += block[b];
		_snprintf_s((char*)block + 148, 8, _TRUNCATE, "%06o", sum);
		block[155] = 0;
		if (!PutOut(*out, block, 512)) { out.reset(); DeleteFileW(archive.c_str()); if (!temp.empty()) DeleteFileW(temp.c_str()); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		if (!files[k].dir) {
			HANDLE in = OxzOpenReadA((work + L"\\" + files[k].rel).c_str());
			uint64_t left = files[k].size;
			unsigned char buf[1 << 16];
			OxzNoteA(i, n, files[k].rel.c_str(), base, span);
			OxzPulseSize(files[k].size);
			i++;
			if (in == INVALID_HANDLE_VALUE) { out.reset(); DeleteFileW(archive.c_str()); if (!temp.empty()) DeleteFileW(temp.c_str()); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			while (left) {
				DWORD want = left > sizeof(buf) ? (DWORD)sizeof(buf) : (DWORD)left;
				DWORD g = 0;
				if (!ReadFile(in, buf, want, &g, NULL) || g != want || !PutOut(*out, buf, g)) { CloseHandle(in); out.reset(); DeleteFileW(archive.c_str()); if (!temp.empty()) DeleteFileW(temp.c_str()); err = OxzTxt(OS_OPEN_FAIL); return 2; }
				left -= g;
			}
			CloseHandle(in);
			if (files[k].size & 511) {
				unsigned char pad[512];
				DWORD pn = (DWORD)(512 - (files[k].size & 511));
				memset(pad, 0, pn);
				if (!PutOut(*out, pad, pn)) { out.reset(); DeleteFileW(archive.c_str()); if (!temp.empty()) DeleteFileW(temp.c_str()); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			}
		}
	}
	{
		unsigned char z[1024];
		memset(z, 0, sizeof(z));
		if (!PutOut(*out, z, 1024) || !out->Finish(bytes)) { out.reset(); DeleteFileW(archive.c_str()); if (!temp.empty()) DeleteFileW(temp.c_str()); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	}
	out.reset();
	if (wrap == 3) {
		if (!WriteXzFile(temp.c_str(), archive.c_str())) { DeleteFileW(archive.c_str()); DeleteFileW(temp.c_str()); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		DeleteFileW(temp.c_str());
		{
			HANDLE h = OxzOpenReadA(archive.c_str());
			if (h != INVALID_HANDLE_VALUE) { bytes = OxzLenA(h); CloseHandle(h); }
		}
	}
	return 0;
}

int OxzWriteOneLz4(const std::wstring& work, const std::wstring& archive, unsigned long long& bytes, std::wstring& err, int base, int span)
{
	std::vector<DiskFile> files;
	int n = 0, which = -1;
	bytes = 0;
	WalkDisk(work, L"", files);
	for (size_t k = 0; k < files.size(); ++k) if (!files[k].dir) { n++; which = (int)k; }
	if (n != 1) return 3;
	{
		Lz4Out out(archive.c_str());
		HANDLE in = OxzOpenReadA((work + L"\\" + files[which].rel).c_str());
		uint64_t left = files[which].size;
		unsigned char buf[1 << 16];
		OxzNoteA(0, 1, files[which].rel.c_str(), base, span);
		OxzPulseSize(files[which].size);
		if (in == INVALID_HANDLE_VALUE) { DeleteFileW(archive.c_str()); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		while (left) {
			DWORD want = left > sizeof(buf) ? (DWORD)sizeof(buf) : (DWORD)left;
			DWORD g = 0;
			if (!ReadFile(in, buf, want, &g, NULL) || g != want || !out.Write(buf, g)) { CloseHandle(in); DeleteFileW(archive.c_str()); err = OxzTxt(OS_OPEN_FAIL); return 2; }
			left -= g;
		}
		CloseHandle(in);
		if (!out.Finish(bytes)) { DeleteFileW(archive.c_str()); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	}
	return 0;
}
