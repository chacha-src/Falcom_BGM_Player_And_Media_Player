#include "OxzCore.h"

#define NOMINMAX
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>
#include <immintrin.h>

#include "zstd.h"
#include "lz4.h"
#include "lz4hc.h"
#include "libdeflate.h"
#include <brotli/encode.h>
#include <brotli/decode.h>
#include "lizard_compress.h"
#include "lizard_decompress.h"
#include "lzfse.h"
#include "bzlib.h"
#include "Lzma2Dec.h"
#include "Ppmd7.h"
#include "OxzFilt.h"
#include "OxzFast.h"

enum {
	M_STORE = 0,
	M_ZSTD = 1,
	M_LZ4 = 2,
	M_DEFLATE = 3,
	M_LZMA2 = 4,
	M_BROTLI = 5,
	M_TOMB = 6,
	M_DIR = 7,
	M_RAWDEFLATE = 8,
	M_LIZARD = 9,
	M_LZFSE = 10,
	M_BZIP2 = 11,
	M_PPMD = 12
};

enum { F_RIP = 1, F_DELTA1 = 2, F_DELTA4 = 4 };

enum { ORIGIN_NEW = 0, ORIGIN_BASE = 1, ORIGIN_OVERLAY = 2, ORIGIN_TOMB = 3 };

static const uint8_t kMagic[8] = { 'O', 'X', 'Z', 'P', 'K', 'G', 0, 0 };
static const size_t kLzBlockMax = 0x7E000000u;
static const size_t kProbeBytes = 512u * 1024u;
static const size_t kPpmdMax = 8ull * 1024ull * 1024ull;
static thread_local size_t gLevelBytes = 0;
static int gUserLevel = 10;
static int gUsePpmd = 1;
static thread_local int tZstdLdm = 0;
static thread_local int tInSurvey = 0;
static thread_local int tInFilePool = 0;

static int Scale(int lo, int hi)
{
	if (hi < lo) return lo;
	if (gUserLevel <= 1) return lo;
	if (gUserLevel >= 10) return hi;
	return lo + (hi - lo) * (gUserLevel - 1) / 9;
}

static size_t LevelN(size_t n)
{
	return gLevelBytes ? gLevelBytes : n;
}

static unsigned HwCap(void)
{
	unsigned n = std::thread::hardware_concurrency();
	if (n < 1) n = 4;
	if (n > 12) n = 12;
	return n;
}

static unsigned InnerThreads(size_t n)
{
	unsigned threads;
	if (tInSurvey || n < 2ull * 1024ull * 1024ull) return 1;
	threads = HwCap();
	if (tInFilePool && threads > 2) threads = 2;
	return threads;
}

static void BoostThread(std::thread& th)
{
	SetThreadPriority((HANDLE)th.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
}

extern "C" int OxzLzma2Compress(const void* in, size_t inSize, void* out, size_t outCap, size_t* outSize, unsigned char* prop, size_t profile, int userLevel);
extern "C" int OxzLzma2EncodeChunk(const void* in, size_t inSize, void* out, size_t outCap, size_t* outSize, unsigned char* prop, size_t profile, int userLevel);
extern "C" int OxzPpmdCompress(const void* in, size_t inSize, void* out, size_t outCap, size_t* outSize, size_t profile, int userLevel);
extern "C" void* OxzLzmaAlloc(const ISzAlloc* p, size_t n)
{
	(void)p;
	return n ? malloc(n) : NULL;
}
extern "C" void OxzLzmaFree(const ISzAlloc* p, void* a)
{
	(void)p;
	free(a);
}

struct ILess {
	bool operator()(const std::wstring& a, const std::wstring& b) const
	{
		return _wcsicmp(a.c_str(), b.c_str()) < 0;
	}
};

struct SnapFile {
	std::wstring rel;
	uint64_t size;
	uint64_t mtime;
	uint32_t attr;
	int isDir;
	int origin;
	uint16_t method;
	uint16_t flags;
	uint8_t prop;
	uint32_t crc;
	uint64_t unpacked;
	uint64_t packed;
	uint64_t dataOff;
	int dropped;
};

struct Snap {
	std::wstring work;
	std::wstring archive;
	int kind; /* 0 none, 1 raw 7z, 2 pkg */
	uint64_t baseOff;
	uint64_t baseSize;
	std::map<std::wstring, SnapFile, ILess> files;
};

static Snap gSnap;

struct DirEnt {
	uint16_t method;
	uint16_t flags;
	uint8_t prop;
	uint32_t crc;
	uint32_t attr;
	uint64_t mtime;
	uint64_t unpacked;
	uint64_t packed;
	uint64_t dataOff;
	std::wstring name;
};

struct WorkItem {
	std::wstring rel;
	std::wstring disk;
	int isDir;
	uint64_t size;
	uint64_t mtime;
	uint32_t attr;
};

enum { OP_COMP = 1, OP_COPY = 2, OP_TOMB = 3, OP_DIR = 4, OP_SKIP = 5, OP_STREAM = 6 };

struct OutItem {
	int op;
	std::wstring rel;
	std::wstring disk;
	uint64_t size;
	uint64_t mtime;
	uint32_t attr;
	uint32_t crc;
	uint16_t method;
	uint16_t flags;
	uint8_t prop;
	uint64_t unpacked;
	uint64_t packed;
	uint64_t srcOff;
	std::vector<uint8_t> payload;
};

static uint32_t gCrcTab[8][256];
static int gCrcReady;

static void CrcInit(void)
{
	int i, b, k;
	if (gCrcReady) return;
	for (i = 0; i < 256; ++i) {
		uint32_t r = (uint32_t)i;
		for (b = 0; b < 8; ++b) r = (r >> 1) ^ (0xEDB88320u & (0u - (r & 1u)));
		gCrcTab[0][i] = r;
	}
	for (k = 1; k < 8; ++k) {
		for (i = 0; i < 256; ++i) {
			uint32_t r = gCrcTab[k - 1][i];
			gCrcTab[k][i] = gCrcTab[0][r & 255] ^ (r >> 8);
		}
	}
	gCrcReady = 1;
}

static uint32_t CrcBlock(const uint8_t* p, size_t n, uint32_t c)
{
	CrcInit();
	while (n >= 8) {
		uint32_t one, two;
		memcpy(&one, p, 4);
		memcpy(&two, p + 4, 4);
		one ^= c;
		c = gCrcTab[7][one & 255] ^ gCrcTab[6][(one >> 8) & 255] ^ gCrcTab[5][(one >> 16) & 255] ^ gCrcTab[4][(one >> 24) & 255]
			^ gCrcTab[3][two & 255] ^ gCrcTab[2][(two >> 8) & 255] ^ gCrcTab[1][(two >> 16) & 255] ^ gCrcTab[0][(two >> 24) & 255];
		p += 8;
		n -= 8;
	}
	while (n--) c = gCrcTab[0][(c ^ *p++) & 255] ^ (c >> 8);
	return c;
}

static uint32_t CrcFinish(const uint8_t* p, size_t n)
{
	return CrcBlock(p, n, 0xFFFFFFFFu) ^ 0xFFFFFFFFu;
}

static std::wstring FullPath(const wchar_t* p)
{
	wchar_t buf[32768];
	DWORD n;
	if (!p || !p[0]) return L"";
	n = GetFullPathNameW(p, 32768, buf, NULL);
	if (!n || n >= 32768) return p;
	return buf;
}

static int SamePath(const std::wstring& a, const std::wstring& b)
{
	return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

static int ReadAt(HANDLE h, uint64_t off, void* dst, size_t n)
{
	uint8_t* p = (uint8_t*)dst;
	LARGE_INTEGER li;
	li.QuadPart = (LONGLONG)off;
	if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN)) return 0;
	while (n) {
		DWORD chunk = n > (1 << 20) ? (1 << 20) : (DWORD)n;
		DWORD rd = 0;
		if (!ReadFile(h, p, chunk, &rd, NULL) || rd != chunk) return 0;
		p += rd;
		n -= rd;
	}
	return 1;
}

static int WriteAll(HANDLE h, const void* src, size_t n)
{
	const uint8_t* p = (const uint8_t*)src;
	while (n) {
		DWORD chunk = n > (1 << 20) ? (1 << 20) : (DWORD)n;
		DWORD wr = 0;
		if (!WriteFile(h, p, chunk, &wr, NULL) || wr != chunk) return 0;
		p += wr;
		n -= wr;
	}
	return 1;
}

static uint64_t FileLen(HANDLE h)
{
	LARGE_INTEGER li;
	if (!GetFileSizeEx(h, &li) || li.QuadPart < 0) return 0;
	return (uint64_t)li.QuadPart;
}

static HANDLE OpenRead(const wchar_t* path)
{
	return CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static int Is7zHead(const uint8_t* b, size_t n)
{
	return n >= 6 && b[0] == 0x37 && b[1] == 0x7A && b[2] == 0xBC && b[3] == 0xAF && b[4] == 0x27 && b[5] == 0x1C;
}

static void Put16(std::vector<uint8_t>& b, uint16_t v)
{
	b.push_back((uint8_t)v);
	b.push_back((uint8_t)(v >> 8));
}
static void Put32(std::vector<uint8_t>& b, uint32_t v)
{
	int i;
	for (i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i)));
}
static void Put64(std::vector<uint8_t>& b, uint64_t v)
{
	int i;
	for (i = 0; i < 8; ++i) b.push_back((uint8_t)(v >> (8 * i)));
}

struct Rdr {
	const uint8_t* p;
	const uint8_t* end;
	int ok;
};

static uint16_t Get16(Rdr* r)
{
	uint16_t v;
	if (!r->ok || r->end - r->p < 2) { r->ok = 0; return 0; }
	v = (uint16_t)(r->p[0] | (r->p[1] << 8));
	r->p += 2;
	return v;
}
static uint32_t Get32(Rdr* r)
{
	uint32_t v;
	int i;
	if (!r->ok || r->end - r->p < 4) { r->ok = 0; return 0; }
	v = 0;
	for (i = 0; i < 4; ++i) v |= (uint32_t)r->p[i] << (8 * i);
	r->p += 4;
	return v;
}
static uint64_t Get64(Rdr* r)
{
	uint64_t v = 0;
	int i;
	if (!r->ok || r->end - r->p < 8) { r->ok = 0; return 0; }
	for (i = 0; i < 8; ++i) v |= (uint64_t)r->p[i] << (8 * i);
	r->p += 8;
	return v;
}

static std::string ToUtf8(const std::wstring& w)
{
	std::string s;
	int n;
	if (w.empty()) return s;
	n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	if (n <= 0) return s;
	s.resize(n);
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}

static std::wstring FromUtf8(const uint8_t* p, size_t n)
{
	int c;
	std::wstring w;
	if (!n) return w;
	c = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char*)p, (int)n, NULL, 0);
	if (c <= 0) return w;
	w.resize(c);
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char*)p, (int)n, &w[0], c);
	return w;
}

static int RelOk(const std::wstring& rel)
{
	size_t i = 0;
	if (rel.empty() || rel[0] == L'\\' || rel[0] == L'/') return 0;
	if (rel.size() >= 2 && rel[1] == L':') return 0;
	while (i < rel.size()) {
		size_t j = i;
		std::wstring part;
		while (j < rel.size() && rel[j] != L'\\' && rel[j] != L'/') ++j;
		part = rel.substr(i, j - i);
		if (part.empty() || part == L"." || part == L"..") return 0;
		if (j >= rel.size()) break;
		i = j + 1;
		if (i >= rel.size()) return 0;
	}
	return 1;
}

static std::wstring SlashToBack(std::wstring s)
{
	size_t i;
	for (i = 0; i < s.size(); ++i) if (s[i] == L'/') s[i] = L'\\';
	return s;
}

static int LooksPe(const uint8_t* p, size_t n)
{
	uint32_t off;
	if (n < 0x40 || p[0] != 'M' || p[1] != 'Z') return 0;
	off = (uint32_t)p[0x3C] | ((uint32_t)p[0x3D] << 8) | ((uint32_t)p[0x3E] << 16) | ((uint32_t)p[0x3F] << 24);
	if ((size_t)off + 4 > n || off < 0x40) return 0;
	return p[off] == 'P' && p[off + 1] == 'E' && p[off + 2] == 0 && p[off + 3] == 0;
}

static int TryZstd(const uint8_t* in, size_t n, std::vector<uint8_t>& out, int mode)
{
	size_t ln = LevelN(n);
	int hi = 19;
	int level;
	int wlog = 23;
	int pass;
	int bestLdm = 0;
	size_t best = n;
	std::vector<uint8_t> buf;
	if (n == 0) return 0;
	if (ln <= 256u * 1024u) hi = 22;
	else if (ln <= 64ull * 1024ull * 1024ull) hi = 19;
	else if (ln <= 256ull * 1024ull * 1024ull) hi = 16;
	else if (ln <= 1024ull * 1024ull * 1024ull) hi = 12;
	else hi = 9;
	level = Scale(1, hi);
	if (ln >= 32ull * 1024ull * 1024ull) wlog = 25;
	else if (ln >= 8ull * 1024ull * 1024ull) wlog = 24;
	else if (ln < 1u * 1024u * 1024u) wlog = 21;
	for (pass = 0; pass < 2; ++pass) {
		size_t bound, got;
		int useLog;
		ZSTD_CCtx* c;
		if (mode == 0 && pass == 1) break;
		if (mode == 1 && pass == 0) continue;
		if (pass == 1 && ln > 256ull * 1024ull * 1024ull) break;
		bound = ZSTD_compressBound(n);
		if (ZSTD_isError(bound) || bound == 0) break;
		try { buf.resize(bound); } catch (...) { break; }
		c = ZSTD_createCCtx();
		if (!c) break;
		useLog = wlog;
		if (pass == 1 && useLog < 24) useLog = 24;
		ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, level);
		ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, 0);
		ZSTD_CCtx_setParameter(c, ZSTD_c_windowLog, useLog);
		if (pass == 1) ZSTD_CCtx_setParameter(c, ZSTD_c_enableLongDistanceMatching, 1);
		{
			unsigned zt = InnerThreads(n);
			if (level >= 19 && zt > 2) zt = 2;
			if (zt > 1 && ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_nbWorkers, (int)zt))) zt = 1;
			got = ZSTD_compress2(c, buf.data(), buf.size(), in, n);
			if (ZSTD_isError(got) && zt > 1) {
				ZSTD_CCtx_setParameter(c, ZSTD_c_nbWorkers, 0);
				got = ZSTD_compress2(c, buf.data(), buf.size(), in, n);
			}
		}
		ZSTD_freeCCtx(c);
		if (ZSTD_isError(got) || got == 0 || got >= best) continue;
		best = got;
		bestLdm = pass;
		buf.resize(got);
		out.swap(buf);
	}
	tZstdLdm = bestLdm;
	return best < n;
}

static int TryLz4(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	int cap, got;
	if (n == 0 || n > kLzBlockMax) return 0;
	cap = LZ4_compressBound((int)n);
	if (cap <= 0) return 0;
	try { out.resize((size_t)cap); } catch (...) { return 0; }
	if (gUserLevel <= 2)
		got = LZ4_compress_default((const char*)in, (char*)out.data(), (int)n, cap);
	else
		got = LZ4_compress_HC((const char*)in, (char*)out.data(), (int)n, cap, Scale(4, LZ4HC_CLEVEL_MAX));
	if (got <= 0 || (size_t)got >= n) return 0;
	out.resize((size_t)got);
	return 1;
}

static int DeflateOnce(const uint8_t* in, size_t n, std::vector<uint8_t>& out, int raw)
{
	size_t ln = LevelN(n);
	int hi = 9;
	int level;
	struct libdeflate_compressor* c;
	size_t bound, got;
	if (n == 0) return 0;
	if (ln <= 64ull * 1024ull * 1024ull) hi = 12;
	else if (ln <= 512ull * 1024ull * 1024ull) hi = 9;
	else hi = 6;
	level = Scale(1, hi);
	c = libdeflate_alloc_compressor(level);
	if (!c) return 0;
	bound = raw ? libdeflate_deflate_compress_bound(c, n) : libdeflate_zlib_compress_bound(c, n);
	try { out.resize(bound ? bound : n + 64); } catch (...) { libdeflate_free_compressor(c); return 0; }
	got = raw ? libdeflate_deflate_compress(c, in, n, out.data(), out.size())
		: libdeflate_zlib_compress(c, in, n, out.data(), out.size());
	libdeflate_free_compressor(c);
	if (got == 0 || got >= n) return 0;
	out.resize(got);
	return 1;
}

static int TryDeflate(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	return DeflateOnce(in, n, out, 0);
}

static int TryRawDeflate(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	return DeflateOnce(in, n, out, 1);
}

static int BrotliParallel(const uint8_t* in, size_t n, int q, int win, std::vector<uint8_t>& out, unsigned threads)
{
	size_t chunk = 4ull * 1024ull * 1024ull;
	size_t parts, total = 0, pi;
	unsigned workers, t;
	if (chunk > n / 2) chunk = n / 2;
	if (chunk < 256u * 1024u) return 0;
	parts = (n + chunk - 1) / chunk;
	if (parts < 2) return 0;
	{
		std::vector<std::vector<uint8_t>> piece(parts);
		std::vector<int> ok(parts, 0);
		std::vector<std::thread> pool;
		std::atomic<size_t> cursor(0);
		workers = threads;
		if (workers > parts) workers = (unsigned)parts;
		for (t = 0; t < workers; ++t) {
			pool.push_back(std::thread([&, n, chunk, q, win, parts]() {
				for (;;) {
					size_t i = cursor.fetch_add(1);
					size_t off, len, cap, got;
					if (i >= parts) break;
					off = chunk * i;
					if (off >= n) break;
					len = chunk;
					if (len > n - off) len = n - off;
					cap = BrotliEncoderMaxCompressedSize(len);
					if (cap == 0) continue;
					try { piece[i].resize(cap); }
					catch (...) { continue; }
					got = cap;
					if (!BrotliEncoderCompress(q, win, BROTLI_MODE_GENERIC, len, in + off, &got, piece[i].data()) || got == 0) continue;
					piece[i].resize(got);
					ok[i] = 1;
				}
			}));
			BoostThread(pool.back());
		}
		for (t = 0; t < pool.size(); ++t) pool[t].join();
		for (pi = 0; pi < parts; ++pi) {
			if (!ok[pi]) return 0;
			total += piece[pi].size();
		}
		if (total == 0 || total >= n) return 0;
		try { out.resize(total); }
		catch (...) { return 0; }
		total = 0;
		for (pi = 0; pi < parts; ++pi) {
			memcpy(out.data() + total, piece[pi].data(), piece[pi].size());
			total += piece[pi].size();
		}
	}
	return 1;
}

static int TryBrotli(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	int q, win, mode, modes;
	size_t cap, best, ln;
	unsigned threads;
	if (n == 0) return 0;
	cap = BrotliEncoderMaxCompressedSize(n);
	if (cap == 0) return 0;
	ln = LevelN(n);
	if (ln <= 8u * 1024u * 1024u) q = BROTLI_MAX_QUALITY;
	else if (ln <= 64ull * 1024ull * 1024ull) q = 9;
	else if (ln <= 256ull * 1024ull * 1024ull) q = 6;
	else q = 4;
	q = Scale(1, q);
	win = n < (1u << 16) ? 18 : BROTLI_MAX_WINDOW_BITS;
	best = n;
	modes = (n <= 8u * 1024u * 1024u && ln <= 8u * 1024u * 1024u) ? 2 : 1;
	threads = InnerThreads(n);
	if (threads > 1 && n > 8ull * 1024ull * 1024ull && BrotliParallel(in, n, q, win, out, threads)) return 1;
	for (mode = 0; mode < modes; ++mode) {
		std::vector<uint8_t> buf;
		size_t got = cap;
		BrotliEncoderMode bm = mode ? BROTLI_MODE_TEXT : BROTLI_MODE_GENERIC;
		try { buf.resize(cap); } catch (...) { break; }
		if (!BrotliEncoderCompress(q, win, bm, n, in, &got, buf.data())) continue;
		if (got == 0 || got >= best) continue;
		best = got;
		buf.resize(got);
		out.swap(buf);
	}
	return best < n;
}

static int TryLzma(const uint8_t* in, size_t n, std::vector<uint8_t>& out, uint8_t* prop)
{
	size_t cap, got = 0;
	unsigned char pr = 0;
	unsigned th;
	if (n == 0) return 0;
	th = InnerThreads(n);
	if (th >= 2 && n >= 8ull * 1024ull * 1024ull) {
		size_t unit = n / th;
		if (unit < 4ull * 1024ull * 1024ull) {
			th = (unsigned)(n / (4ull * 1024ull * 1024ull));
			if (th < 2) th = 2;
			unit = n / th;
		}
		if (th >= 2 && unit > 0) {
			struct Part { std::vector<uint8_t> data; unsigned char prop; int ok; };
			std::vector<Part> parts(th);
			std::vector<std::thread> pool;
			std::atomic<unsigned> cursor(0);
			unsigned t;
			int propsOk = 1;
			size_t total = 0;
			for (t = 0; t < th; ++t) {
				pool.push_back(std::thread([&, n, th]() {
					for (;;) {
						unsigned c = cursor.fetch_add(1);
						size_t off, len, capc, gotc = 0;
						unsigned char prc = 0;
						if (c >= th) break;
						off = (n / th) * c;
						len = (c + 1 == th) ? (n - off) : (n / th);
						capc = len + (len / 4) + 65536;
						try { parts[c].data.resize(capc); }
						catch (...) { parts[c].ok = 0; continue; }
						if (OxzLzma2EncodeChunk(in + off, len, parts[c].data.data(), capc, &gotc, &prc, LevelN(n), gUserLevel) != 0 || gotc == 0 || parts[c].data[gotc - 1] != 0) {
							parts[c].ok = 0;
							continue;
						}
						parts[c].data.resize(gotc);
						parts[c].prop = prc;
						parts[c].ok = 1;
					}
				}));
				BoostThread(pool.back());
			}
			for (t = 0; t < pool.size(); ++t) pool[t].join();
			for (t = 0; t < th; ++t) {
				if (!parts[t].ok) { propsOk = 0; break; }
				if (t > 0 && parts[t].prop != parts[0].prop) { propsOk = 0; break; }
				total += parts[t].data.size() - (t + 1 == th ? 0 : 1);
			}
			if (propsOk && total > 0 && total < n) {
				size_t at = 0;
				try { out.resize(total); }
				catch (...) { propsOk = 0; }
				if (propsOk) {
					for (t = 0; t < th; ++t) {
						size_t take = parts[t].data.size();
						if (t + 1 != th) take--;
						memcpy(out.data() + at, parts[t].data.data(), take);
						at += take;
					}
					*prop = parts[0].prop;
					return 1;
				}
			}
		}
	}
	cap = n + (n / 4) + 65536;
	try { out.resize(cap); } catch (...) { return 0; }
	if (OxzLzma2Compress(in, n, out.data(), cap, &got, &pr, LevelN(n), gUserLevel) != 0) return 0;
	if (got == 0 || got >= n) return 0;
	out.resize(got);
	*prop = pr;
	return 1;
}

static int TryLizard(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	int level, cap, got;
	size_t ln;
	if (n == 0 || n > kLzBlockMax) return 0;
	ln = LevelN(n);
	if (ln <= 4u * 1024u * 1024u) level = LIZARD_MAX_CLEVEL;
	else if (ln <= 64ull * 1024ull * 1024ull) level = 22;
	else level = LIZARD_DEFAULT_CLEVEL;
	level = Scale(LIZARD_MIN_CLEVEL, level);
	cap = Lizard_compressBound((int)n);
	if (cap <= 0) return 0;
	try { out.resize((size_t)cap); } catch (...) { return 0; }
	got = Lizard_compress((const char*)in, (char*)out.data(), (int)n, cap, level);
	if (got <= 0 || (size_t)got >= n) return 0;
	out.resize((size_t)got);
	return 1;
}

static int TryLzfse(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	size_t scratchNeed, got;
	void* scratch;
	if (n == 0) return 0;
	scratchNeed = lzfse_encode_scratch_size();
	scratch = scratchNeed ? malloc(scratchNeed) : NULL;
	if (scratchNeed && !scratch) return 0;
	try { out.resize(n + 4096); } catch (...) { free(scratch); return 0; }
	got = lzfse_encode_buffer(out.data(), out.size(), in, n, scratch);
	free(scratch);
	if (got == 0 || got >= n) return 0;
	out.resize(got);
	return 1;
}

static int TryBzip2(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	if (n == 0 || n > 0xFFFFFFFFull) return 0;
	return OxzBzipCompress(in, n, out, Scale(1, 9), InnerThreads(n));
}

static int TryPpmd(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	size_t got = 0;
	if (n == 0) return 0;
	try { out.resize(n); } catch (...) { return 0; }
	if (OxzPpmdCompress(in, n, out.data(), out.size(), &got, LevelN(n), gUserLevel) != 0) return 0;
	if (got == 0 || got >= n) return 0;
	out.resize(got);
	return 1;
}

struct Choice {
	int method;
	int flags;
	int zstdLdm;
	uint8_t prop;
	std::vector<uint8_t> data;
};

static void Consider(Choice* best, int method, int flags, uint8_t prop, std::vector<uint8_t>& packed)
{
	if (packed.size() >= best->data.size()) return;
	best->method = method;
	best->flags = flags;
	best->prop = prop;
	best->zstdLdm = (method == M_ZSTD) ? tZstdLdm : 0;
	best->data.swap(packed);
}

static void Delta1Enc(uint8_t* p, size_t n)
{
	size_t i;
	if (n < 2) return;
	i = n;
	while (i > 32) {
		size_t at = i - 32;
		__m256i cur, prev;
		if (at < 1) break;
		cur = _mm256_loadu_si256((const __m256i*)(p + at));
		prev = _mm256_loadu_si256((const __m256i*)(p + at - 1));
		_mm256_storeu_si256((__m256i*)(p + at), _mm256_sub_epi8(cur, prev));
		i = at;
	}
	while (i > 1) {
		--i;
		p[i] = (uint8_t)(p[i] - p[i - 1]);
	}
}

static void Delta1Dec(uint8_t* p, size_t n)
{
	size_t i = 1;
	if (n < 2) return;
	while (i + 16 <= n) {
		__m128i v = _mm_loadu_si128((const __m128i*)(p + i));
		__m128i carry;
		v = _mm_add_epi8(v, _mm_slli_si128(v, 1));
		v = _mm_add_epi8(v, _mm_slli_si128(v, 2));
		v = _mm_add_epi8(v, _mm_slli_si128(v, 4));
		v = _mm_add_epi8(v, _mm_slli_si128(v, 8));
		carry = _mm_set1_epi8((char)p[i - 1]);
		v = _mm_add_epi8(v, carry);
		_mm_storeu_si128((__m128i*)(p + i), v);
		i += 16;
	}
	for (; i < n; ++i) p[i] = (uint8_t)(p[i] + p[i - 1]);
}

static void Delta4Enc(uint8_t* p, size_t n)
{
	size_t i;
	if (n < 8) return;
	i = n;
	while (i > 32) {
		size_t at = i - 32;
		__m256i cur, prev;
		if (at < 4) break;
		cur = _mm256_loadu_si256((const __m256i*)(p + at));
		prev = _mm256_loadu_si256((const __m256i*)(p + at - 4));
		_mm256_storeu_si256((__m256i*)(p + at), _mm256_sub_epi8(cur, prev));
		i = at;
	}
	while (i > 4) {
		--i;
		p[i] = (uint8_t)(p[i] - p[i - 4]);
	}
}

static void Delta4Dec(uint8_t* p, size_t n)
{
	size_t i = 4;
	while (i + 4 <= n) {
		int k, s, d;
		memcpy(&s, p + i - 4, 4);
		memcpy(&d, p + i, 4);
		{
			__m128i a = _mm_add_epi8(_mm_cvtsi32_si128(s), _mm_cvtsi32_si128(d));
			k = _mm_cvtsi128_si32(a);
		}
		memcpy(p + i, &k, 4);
		i += 4;
	}
	for (; i < n; ++i) p[i] = (uint8_t)(p[i] + p[i - 4]);
}

static void RunCodecs(Choice* best, const uint8_t* p, size_t n, int flags)
{
	std::vector<uint8_t> tmp;
	uint8_t prop = 0;
	if (n == 0) return;
	if (TryZstd(p, n, tmp, -1)) Consider(best, M_ZSTD, flags, 0, tmp);
	if (TryLz4(p, n, tmp)) Consider(best, M_LZ4, flags, 0, tmp);
	if (TryLizard(p, n, tmp)) Consider(best, M_LIZARD, flags, 0, tmp);
	if (TryLzfse(p, n, tmp)) Consider(best, M_LZFSE, flags, 0, tmp);
	if (TryDeflate(p, n, tmp)) Consider(best, M_DEFLATE, flags, 0, tmp);
	if (TryRawDeflate(p, n, tmp)) Consider(best, M_RAWDEFLATE, flags, 0, tmp);
	if (TryBzip2(p, n, tmp)) Consider(best, M_BZIP2, flags, 0, tmp);
	if (TryBrotli(p, n, tmp)) Consider(best, M_BROTLI, flags, 0, tmp);
	if (TryPpmd(p, n, tmp)) Consider(best, M_PPMD, flags, 0, tmp);
	if (TryLzma(p, n, tmp, &prop)) Consider(best, M_LZMA2, flags, prop, tmp);
}

static void Trial(Choice* best, const uint8_t* raw, size_t n, int flags)
{
	std::vector<uint8_t> b;
	try { b.assign(raw, raw + n); } catch (...) { return; }
	if (flags & F_RIP) OxzRipConvert(b.data(), b.size(), 1);
	if (flags & F_DELTA4) Delta4Enc(b.data(), b.size());
	else if (flags & F_DELTA1) Delta1Enc(b.data(), b.size());
	RunCodecs(best, b.data(), b.size(), flags);
}

static int OneCodec(const uint8_t* in, size_t n, int method, int zstdPass, std::vector<uint8_t>& tmp, uint8_t* prop)
{
	switch (method) {
	case M_ZSTD: return TryZstd(in, n, tmp, zstdPass);
	case M_LZ4: return TryLz4(in, n, tmp);
	case M_LIZARD: return TryLizard(in, n, tmp);
	case M_LZFSE: return TryLzfse(in, n, tmp);
	case M_DEFLATE: return TryDeflate(in, n, tmp);
	case M_RAWDEFLATE: return TryRawDeflate(in, n, tmp);
	case M_BZIP2: return TryBzip2(in, n, tmp);
	case M_BROTLI: return TryBrotli(in, n, tmp);
	case M_PPMD: return TryPpmd(in, n, tmp);
	case M_LZMA2: return TryLzma(in, n, tmp, prop);
	default: return 0;
	}
}

static void WarmCodecs()
{
	static std::once_flag once;
	std::call_once(once, []() {
		uint8_t sample[64];
		std::vector<uint8_t> tmp;
		uint8_t prop = 0;
		int i;
		for (i = 0; i < 64; ++i) sample[i] = (uint8_t)(i * 17 + 3);
		OneCodec(sample, 64, M_ZSTD, 0, tmp, &prop);
		OneCodec(sample, 64, M_LZ4, 0, tmp, &prop);
		OneCodec(sample, 64, M_LIZARD, 0, tmp, &prop);
		OneCodec(sample, 64, M_LZFSE, 0, tmp, &prop);
		OneCodec(sample, 64, M_DEFLATE, 0, tmp, &prop);
		OneCodec(sample, 64, M_RAWDEFLATE, 0, tmp, &prop);
		OneCodec(sample, 64, M_BZIP2, 0, tmp, &prop);
		OneCodec(sample, 64, M_BROTLI, 0, tmp, &prop);
		OneCodec(sample, 64, M_PPMD, 0, tmp, &prop);
		OneCodec(sample, 64, M_LZMA2, 0, tmp, &prop);
	});
}

struct FilePulse {
	const wchar_t* name;
	uint64_t size;
	int pct;
	int seen;
};

struct SavePulse {
	OxzCoreProgFn fn;
	void* user;
	unsigned total;
	unsigned filesDone;
	uint64_t bytesAll;
	uint64_t bytesDone;
	DWORD lastTick;
	std::mutex mu;
	std::vector<FilePulse*> active;
	SavePulse() : fn(NULL), user(NULL), total(0), filesDone(0), bytesAll(0), bytesDone(0), lastTick(0) {}
};

static SavePulse* gPulse = NULL;
static thread_local FilePulse* tFilePulse = NULL;
static const unsigned kPulseMagic = 0x50585A4Fu;

struct PulseWeight {
	unsigned magic;
	int base;
	int span;
	int usedBase;
	unsigned long long numer;
	unsigned long long denom;
};

static void Publish(FilePulse* fp, int pct, int finish)
{
	SavePulse* p;
	unsigned display;
	uint64_t numer;
	size_t i;
	DWORD now;
	if (!fp || !fp->name) return;
	p = gPulse;
	if (!p || !p->fn) return;
	std::lock_guard<std::mutex> lock(p->mu);
	if (!fp->seen) {
		fp->seen = 1;
		p->active.push_back(fp);
	}
	now = GetTickCount();
	if (!finish && pct > 0 && p->lastTick && (DWORD)(now - p->lastTick) < 80) {
		if (pct > fp->pct && pct < 100) fp->pct = pct;
		return;
	}
	if (!finish) p->lastTick = now;
	if (finish) {
		if (fp->pct < 100) {
			p->bytesDone += fp->size;
			p->filesDone++;
		}
		fp->pct = 100;
		for (i = 0; i < p->active.size(); ++i) {
			if (p->active[i] == fp) {
				p->active.erase(p->active.begin() + (ptrdiff_t)i);
				break;
			}
		}
	} else {
		if (pct < fp->pct) pct = fp->pct;
		if (pct > 99) pct = 99;
		fp->pct = pct;
	}
	numer = p->bytesDone;
	for (i = 0; i < p->active.size(); ++i) {
		FilePulse* o = p->active[i];
		if (o->pct > 0 && o->pct < 100) numer += o->size * (uint64_t)o->pct / 100ull;
	}
	if (p->bytesAll && numer > p->bytesAll) numer = p->bytesAll;
	if (p->user) {
		PulseWeight* w = (PulseWeight*)p->user;
		if (w->magic == kPulseMagic) {
			w->numer = numer;
			w->denom = p->bytesAll ? p->bytesAll : 1;
		}
	}
	display = p->filesDone;
	if (!finish && p->total && display < p->total) display++;
	if (p->total && display > p->total) display = p->total;
	if (!display) display = 1;
	p->fn(fp->name, display, p->total ? p->total : 1, finish ? 100 : fp->pct, p->user);
}

struct PulseGuard {
	FilePulse fp;
	explicit PulseGuard(const std::wstring& rel, uint64_t size)
	{
		memset(&fp, 0, sizeof(fp));
		fp.name = rel.c_str();
		fp.size = size ? size : 1;
		tFilePulse = &fp;
		Publish(&fp, 0, 0);
	}
	~PulseGuard()
	{
		Publish(&fp, 100, 1);
		if (tFilePulse == &fp) tFilePulse = NULL;
	}
};

struct Survey {
	int method;
	int flags;
	int zstdPass;
	int ok;
	int zstdLdm;
	uint8_t prop;
	std::vector<uint8_t> data;
};

static Choice Search(const uint8_t* raw, size_t n, int isPe)
{
	static const int kMethod[] = {
		M_ZSTD, M_ZSTD, M_LZ4, M_LIZARD, M_LZFSE, M_DEFLATE, M_RAWDEFLATE, M_BZIP2, M_BROTLI, M_PPMD, M_LZMA2
	};
	static const int kPass[] = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	int flags[6];
	int nflags = 0;
	int f, m;
	unsigned threads, t;
	std::vector<Survey> jobs;
	std::vector<std::thread> pool;
	std::atomic<size_t> cursor(0);
	Choice best;
	best.method = M_STORE;
	best.flags = 0;
	best.prop = 0;
	best.zstdLdm = 0;
	best.data.assign(raw, raw + n);
	if (n == 0) return best;
	WarmCodecs();
	flags[nflags++] = 0;
	if (isPe) flags[nflags++] = F_RIP;
	if (n >= 32) {
		flags[nflags++] = F_DELTA1;
		flags[nflags++] = F_DELTA4;
		if (isPe) {
			flags[nflags++] = F_RIP | F_DELTA1;
			flags[nflags++] = F_RIP | F_DELTA4;
		}
	}
	for (f = 0; f < nflags; ++f) {
		for (m = 0; m < (int)(sizeof(kMethod) / sizeof(kMethod[0])); ++m) {
			Survey s;
			if (kMethod[m] == M_PPMD && (!gUsePpmd || LevelN(n) > kPpmdMax)) continue;
			s.method = kMethod[m];
			s.flags = flags[f];
			s.zstdPass = kPass[m];
			s.ok = 0;
			s.zstdLdm = 0;
			s.prop = 0;
			jobs.push_back(std::move(s));
		}
	}
	threads = HwCap();
	if (tInFilePool) {
		unsigned capThreads = HwCap() / 3;
		if (capThreads < 1) capThreads = 1;
		if (capThreads > 4) capThreads = 4;
		threads = capThreads;
	}
	if (threads > jobs.size()) threads = (unsigned)jobs.size();
	{
		size_t levelSnap = gLevelBytes;
		FilePulse* pulseFile = tFilePulse;
		unsigned pulseCap = (levelSnap > n && levelSnap - n > 4096) ? 25u : 95u;
		std::atomic<unsigned> pulseStep(0);
		size_t pulseJobs = jobs.size() ? jobs.size() : 1;
		for (t = 0; t < threads; ++t) {
			pool.push_back(std::thread([&, n, levelSnap, pulseFile, pulseCap, pulseJobs]() {
				gLevelBytes = levelSnap;
				tInSurvey = 1;
				for (;;) {
					size_t i = cursor.fetch_add(1);
					std::vector<uint8_t> b, tmp;
					uint8_t prop = 0;
					Survey* job;
					unsigned step;
					if (i >= jobs.size()) break;
					job = &jobs[i];
					try {
						b.assign(raw, raw + n);
						if (job->flags & F_RIP) OxzRipConvert(b.data(), b.size(), 1);
						if (job->flags & F_DELTA4) Delta4Enc(b.data(), b.size());
						else if (job->flags & F_DELTA1) Delta1Enc(b.data(), b.size());
						if (OneCodec(b.data(), b.size(), job->method, job->zstdPass, tmp, &prop)) {
							job->ok = 1;
							job->prop = prop;
							job->zstdLdm = (job->method == M_ZSTD) ? job->zstdPass : 0;
							job->data.swap(tmp);
						}
					} catch (...) {
					}
					if (pulseFile) {
						step = pulseStep.fetch_add(1) + 1;
						Publish(pulseFile, (int)((unsigned long long)step * pulseCap / pulseJobs), 0);
					}
				}
				tInSurvey = 0;
			}));
			BoostThread(pool.back());
		}
	}
	for (t = 0; t < pool.size(); ++t) pool[t].join();
	{
		Choice fast = best;
		Choice ppmd;
		int havePpmd = 0;
		for (size_t i = 0; i < jobs.size(); ++i) {
			Choice* slot;
			if (!jobs[i].ok) continue;
			if (jobs[i].method == M_PPMD) {
				if (!havePpmd) {
					ppmd.method = M_STORE;
					ppmd.data.assign(raw, raw + n);
					havePpmd = 1;
				}
				slot = &ppmd;
			} else {
				slot = &fast;
			}
			if (jobs[i].data.size() >= slot->data.size()) continue;
			slot->method = jobs[i].method;
			slot->flags = jobs[i].flags;
			slot->prop = jobs[i].prop;
			slot->zstdLdm = jobs[i].zstdLdm;
			slot->data.swap(jobs[i].data);
		}
		if (havePpmd && ppmd.method == M_PPMD && ppmd.data.size() * 20 <= fast.data.size() * 19)
			best = std::move(ppmd);
		else
			best = std::move(fast);
	}
	return best;
}

static int EmitMethod(const uint8_t* raw, size_t n, int method, int flags, int zstdLdm, Choice* out)
{
	std::vector<uint8_t> b, tmp;
	uint8_t prop = 0;
	int ok = 0;
	try { b.assign(raw, raw + n); } catch (...) { return 0; }
	if (flags & F_RIP) OxzRipConvert(b.data(), b.size(), 1);
	if (flags & F_DELTA4) Delta4Enc(b.data(), b.size());
	else if (flags & F_DELTA1) Delta1Enc(b.data(), b.size());
	ok = OneCodec(b.data(), b.size(), method, method == M_ZSTD ? (zstdLdm ? 1 : 0) : 0, tmp, &prop);
	if (!ok || tmp.size() >= n) return 0;
	out->method = method;
	out->flags = flags;
	out->prop = prop;
	out->zstdLdm = (method == M_ZSTD) ? zstdLdm : 0;
	out->data.swap(tmp);
	return 1;
}

static Choice Pick(const uint8_t* raw, size_t n, int isPe)
{
	Choice best;
	gLevelBytes = n;
	if (n <= kProbeBytes) return Search(raw, n, isPe);
	{
		Choice probe = Search(raw, kProbeBytes, isPe);
		best.method = M_STORE;
		best.flags = 0;
		best.prop = 0;
		best.zstdLdm = 0;
		best.data.assign(raw, raw + n);
		if (probe.method != M_STORE && EmitMethod(raw, n, probe.method, probe.flags, probe.zstdLdm, &best))
			return best;
		if (probe.method != M_ZSTD && EmitMethod(raw, n, M_ZSTD, probe.flags, 1, &best))
			return best;
	}
	return best;
}

static int DecLzma(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen, uint8_t prop)
{
	ISzAlloc alloc = { OxzLzmaAlloc, OxzLzmaFree };
	CLzma2Dec dec;
	SizeT dest = dstLen, slen = srcLen;
	ELzmaStatus st = LZMA_STATUS_NOT_SPECIFIED;
	SRes r;
	Lzma2Dec_Construct(&dec);
	if (Lzma2Dec_Allocate(&dec, prop, &alloc) != SZ_OK) return 0;
	Lzma2Dec_Init(&dec);
	r = Lzma2Dec_DecodeToBuf(&dec, dst, &dest, src, &slen, LZMA_FINISH_END, &st);
	Lzma2Dec_Free(&dec, &alloc);
	return r == SZ_OK && dest == dstLen;
}

static int DecBrotli(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
	size_t ip = 0, op = 0;
	while (op < dstLen && ip < srcLen) {
		BrotliDecoderState* s = BrotliDecoderCreateInstance(NULL, NULL, NULL);
		size_t availIn, availOut, produced, consumed, total = 0;
		const uint8_t* nextIn;
		uint8_t* nextOut;
		BrotliDecoderResult r;
		if (!s) return 0;
		availIn = srcLen - ip;
		nextIn = src + ip;
		availOut = dstLen - op;
		nextOut = dst + op;
		r = BrotliDecoderDecompressStream(s, &availIn, &nextIn, &availOut, &nextOut, &total);
		produced = (dstLen - op) - availOut;
		consumed = (srcLen - ip) - availIn;
		BrotliDecoderDestroyInstance(s);
		if (r != BROTLI_DECODER_RESULT_SUCCESS || (produced == 0 && consumed == 0)) return 0;
		op += produced;
		ip += consumed;
		if (op == dstLen) return 1;
	}
	return op == dstLen;
}

static int DecPpmd(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
	ISzAlloc alloc = { OxzLzmaAlloc, OxzLzmaFree };
	CPpmd7* ppmd;
	unsigned order, mem;
	int ok = 0;
	if (srcLen < 5) return 0;
	order = src[0];
	mem = (unsigned)src[1] | ((unsigned)src[2] << 8) | ((unsigned)src[3] << 16) | ((unsigned)src[4] << 24);
	if (order < PPMD7_MIN_ORDER || order > PPMD7_MAX_ORDER) return 0;
	if (mem < PPMD7_MIN_MEM_SIZE || mem > PPMD7_MAX_MEM_SIZE) return 0;
	ppmd = (CPpmd7*)malloc(sizeof(CPpmd7));
	if (!ppmd) return 0;
	Ppmd7_Construct(ppmd);
	if (!Ppmd7_Alloc(ppmd, mem, &alloc)) { free(ppmd); return 0; }
	Ppmd7_Init(ppmd, order);
	Ppmd7z_RangeDec_Bind(src + 5, src + srcLen);
	if (Ppmd7z_RangeDec_Init(&ppmd->rc.dec) && !Ppmd7z_RangeDec_Over()) {
		Byte* got = Ppmd7z_DecodeSymbols(ppmd, dst, dst + dstLen);
		if (got == dst + dstLen && !Ppmd7z_RangeDec_Over() && Ppmd7z_RangeDec_IsFinishedOK(&ppmd->rc.dec)) ok = 1;
	}
	Ppmd7z_RangeDec_Bind(0, 0);
	Ppmd7_Free(ppmd, &alloc);
	free(ppmd);
	return ok;
}

static int DecodeRaw(const DirEnt& e, const uint8_t* src, std::vector<uint8_t>& plain)
{
	size_t n = (size_t)e.unpacked;
	struct libdeflate_decompressor* d;
	size_t got = 0;
	enum libdeflate_result rr;
	if (e.unpacked != (uint64_t)n) return -1;
	try { plain.assign(n, 0); } catch (...) { return -1; }
	if (n == 0) return e.crc == 0 && e.packed == 0;
	if (e.method == M_STORE) {
		if (e.packed != e.unpacked) return 0;
		memcpy(plain.data(), src, n);
	} else if (e.method == M_ZSTD) {
		got = ZSTD_decompress(plain.data(), n, src, (size_t)e.packed);
		if (ZSTD_isError(got) || got != n) return 0;
	} else if (e.method == M_LZ4) {
		int g;
		if (n > kLzBlockMax || e.packed > kLzBlockMax) return 0;
		g = LZ4_decompress_safe((const char*)src, (char*)plain.data(), (int)e.packed, (int)n);
		if (g < 0 || (size_t)g != n) return 0;
	} else if (e.method == M_LIZARD) {
		int g;
		if (n > kLzBlockMax || e.packed > kLzBlockMax) return 0;
		g = Lizard_decompress_safe((const char*)src, (char*)plain.data(), (int)e.packed, (int)n);
		if (g < 0 || (size_t)g != n) return 0;
	} else if (e.method == M_LZFSE) {
		size_t scratchNeed = lzfse_decode_scratch_size();
		void* scratch = scratchNeed ? malloc(scratchNeed) : NULL;
		if (scratchNeed && !scratch) return 0;
		got = lzfse_decode_buffer(plain.data(), n, src, (size_t)e.packed, scratch);
		free(scratch);
		if (got != n) return 0;
	} else if (e.method == M_DEFLATE || e.method == M_RAWDEFLATE) {
		d = libdeflate_alloc_decompressor();
		if (!d) return 0;
		rr = e.method == M_RAWDEFLATE
			? libdeflate_deflate_decompress(d, src, (size_t)e.packed, plain.data(), n, &got)
			: libdeflate_zlib_decompress(d, src, (size_t)e.packed, plain.data(), n, &got);
		libdeflate_free_decompressor(d);
		if (rr != LIBDEFLATE_SUCCESS || got != n) return 0;
	} else if (e.method == M_BZIP2) {
		if (n > 0xFFFFFFFFull || e.packed > 0xFFFFFFFFull) return 0;
		if (!OxzBzipDecompress(src, (size_t)e.packed, plain.data(), n, InnerThreads(n))) return 0;
	} else if (e.method == M_LZMA2) {
		if (!DecLzma(src, (size_t)e.packed, plain.data(), n, e.prop)) return 0;
	} else if (e.method == M_BROTLI) {
		if (!DecBrotli(src, (size_t)e.packed, plain.data(), n)) return 0;
	} else if (e.method == M_PPMD) {
		if (!DecPpmd(src, (size_t)e.packed, plain.data(), n)) return 0;
	} else {
		return 0;
	}
	if (e.flags & F_DELTA4) Delta4Dec(plain.data(), plain.size());
	else if (e.flags & F_DELTA1) Delta1Dec(plain.data(), plain.size());
	if (e.flags & F_RIP) OxzRipConvert(plain.data(), plain.size(), 0);
	if (e.crc == CrcFinish(plain.data(), plain.size())) return 1;
	if (e.method == M_BZIP2 && n <= 0xFFFFFFFFull && e.packed <= 0xFFFFFFFFull) {
		try { plain.assign(n, 0); } catch (...) { return 0; }
		if (!OxzBzipDecompress(src, (size_t)e.packed, plain.data(), n, 0)) return 0;
		if (e.flags & F_DELTA4) Delta4Dec(plain.data(), plain.size());
		else if (e.flags & F_DELTA1) Delta1Dec(plain.data(), plain.size());
		if (e.flags & F_RIP) OxzRipConvert(plain.data(), plain.size(), 0);
		if (e.crc == CrcFinish(plain.data(), plain.size())) return 1;
	}
	return 0;
}

static int MethodHasPayload(uint16_t m)
{
	return m == M_STORE || m == M_ZSTD || m == M_LZ4 || m == M_DEFLATE || m == M_LZMA2
		|| m == M_BROTLI || m == M_RAWDEFLATE || m == M_LIZARD || m == M_LZFSE
		|| m == M_BZIP2 || m == M_PPMD;
}

static int LoadDir(HANDLE h, uint64_t fileSize, std::vector<DirEnt>& ents, uint64_t* baseOff, uint64_t* baseSize)
{
	uint8_t hdr[64];
	uint16_t ver, hdrSize;
	uint32_t count;
	uint64_t dirOff;
	std::vector<uint8_t> dir;
	Rdr r;
	uint32_t i;
	if (!ReadAt(h, 0, hdr, 64)) return 0;
	if (memcmp(hdr, kMagic, 8) != 0) return 0;
	ver = (uint16_t)(hdr[8] | (hdr[9] << 8));
	hdrSize = (uint16_t)(hdr[10] | (hdr[11] << 8));
	if (ver != 1 || hdrSize < 64 || hdrSize > 4096) return 0;
	*baseOff = 0;
	*baseSize = 0;
	{
		int k;
		for (k = 0; k < 8; ++k) *baseOff |= (uint64_t)hdr[16 + k] << (8 * k);
		for (k = 0; k < 8; ++k) *baseSize |= (uint64_t)hdr[24 + k] << (8 * k);
		dirOff = 0;
		for (k = 0; k < 8; ++k) dirOff |= (uint64_t)hdr[32 + k] << (8 * k);
		count = (uint32_t)hdr[40] | ((uint32_t)hdr[41] << 8) | ((uint32_t)hdr[42] << 16) | ((uint32_t)hdr[43] << 24);
	}
	if (*baseSize && (*baseOff > fileSize || *baseSize > fileSize - *baseOff)) return 0;
	if (count > 100000000u) return 0;
	if (dirOff > fileSize || dirOff < 64) return 0;
	try { dir.resize((size_t)(fileSize - dirOff)); } catch (...) { return 0; }
	if (!dir.empty() && !ReadAt(h, dirOff, dir.data(), dir.size())) return 0;
	r.p = dir.data();
	r.end = dir.data() + dir.size();
	r.ok = 1;
	ents.clear();
	ents.reserve(count);
	for (i = 0; i < count; ++i) {
		DirEnt e;
		uint16_t nameLen;
		const uint8_t* name;
		std::wstring w;
		e.method = Get16(&r);
		e.flags = Get16(&r);
		e.prop = (uint8_t)Get16(&r);
		e.crc = Get32(&r);
		e.attr = Get32(&r);
		e.mtime = Get64(&r);
		e.unpacked = Get64(&r);
		e.packed = Get64(&r);
		e.dataOff = Get64(&r);
		nameLen = Get16(&r);
		if (!r.ok || nameLen > 4096 || (size_t)(r.end - r.p) < nameLen) return 0;
		name = r.p;
		r.p += nameLen;
		w = SlashToBack(FromUtf8(name, nameLen));
		if (!RelOk(w)) return 0;
		e.name = w;
		if (e.method != M_TOMB && e.method != M_DIR && !MethodHasPayload(e.method)) return 0;
		if (MethodHasPayload(e.method)) {
			if (e.packed > fileSize || e.dataOff > fileSize || e.packed > fileSize - e.dataOff) return 0;
		}
		ents.push_back(e);
	}
	return r.ok;
}

static int Seek(HANDLE h, uint64_t off)
{
	LARGE_INTEGER li;
	li.QuadPart = (LONGLONG)off;
	return SetFilePointerEx(h, li, NULL, FILE_BEGIN);
}

static int CopyRange(HANDLE in, uint64_t off, uint64_t n, HANDLE out)
{
	std::vector<uint8_t> buf;
	if (!Seek(in, off)) return 0;
	try { buf.resize(1u << 20); } catch (...) { return 0; }
	while (n) {
		DWORD chunk = n > buf.size() ? (DWORD)buf.size() : (DWORD)n;
		DWORD rd = 0;
		if (!ReadFile(in, buf.data(), chunk, &rd, NULL) || rd != chunk) return 0;
		if (!WriteAll(out, buf.data(), rd)) return 0;
		n -= rd;
	}
	return 1;
}

static uint64_t FtToU(const FILETIME& ft)
{
	return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static FILETIME UToFt(uint64_t v)
{
	FILETIME ft;
	ft.dwLowDateTime = (DWORD)v;
	ft.dwHighDateTime = (DWORD)(v >> 32);
	return ft;
}

static std::wstring JoinPath(const std::wstring& dir, const std::wstring& rel)
{
	if (dir.empty()) return rel;
	if (dir[dir.size() - 1] == L'\\') return dir + rel;
	return dir + L"\\" + rel;
}

static int UnderRoot(const std::wstring& root, const std::wstring& full)
{
	std::wstring r = root;
	if (r.empty()) return 0;
	if (r[r.size() - 1] != L'\\') r += L'\\';
	return _wcsnicmp(full.c_str(), r.c_str(), r.size()) == 0;
}

static void EnsureParents(const std::wstring& full)
{
	size_t i;
	for (i = 0; i < full.size(); ++i) {
		if (full[i] != L'\\') continue;
		if (i >= 4 && full.compare(0, 4, L"\\\\?\\") == 0 && i < 8) continue;
		if (i == 2 && full[1] == L':') continue;
		CreateDirectoryW(full.substr(0, i).c_str(), NULL);
	}
}

static void Walk(const std::wstring& dir, const std::wstring& rel, std::vector<WorkItem>& out)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		WorkItem it;
		std::wstring name = fd.cFileName;
		if (name == L"." || name == L"..") continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
		it.rel = rel.empty() ? name : rel + L"\\" + name;
		it.disk = dir + L"\\" + name;
		it.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
		it.attr = fd.dwFileAttributes;
		it.mtime = FtToU(fd.ftLastWriteTime);
		it.size = it.isDir ? 0 : (((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
		if (!RelOk(it.rel)) continue;
		out.push_back(it);
		if (it.isDir) Walk(it.disk, it.rel, out);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static std::string RelUtf8(const std::wstring& rel)
{
	std::wstring s = rel;
	size_t i;
	for (i = 0; i < s.size(); ++i) if (s[i] == L'\\') s[i] = L'/';
	return ToUtf8(s);
}

static int LoadFile(const std::wstring& path, uint64_t size, std::vector<uint8_t>& buf)
{
	HANDLE h;
	if (size != (uint64_t)(size_t)size) return 0;
	h = OpenRead(path.c_str());
	if (h == INVALID_HANDLE_VALUE) return 0;
	try { buf.resize((size_t)size); }
	catch (...) { CloseHandle(h); return 0; }
	if (size && !ReadAt(h, 0, buf.data(), (size_t)size)) { CloseHandle(h); return 0; }
	CloseHandle(h);
	return 1;
}

static int FillPicked(const std::wstring& path, uint64_t size, OutItem* it)
{
	std::vector<uint8_t> raw;
	Choice c;
	if (!LoadFile(path, size, raw)) return 0;
	c = Pick(raw.data(), raw.size(), LooksPe(raw.data(), raw.size()));
	it->crc = size ? CrcFinish(raw.data(), raw.size()) : 0;
	it->method = (uint16_t)c.method;
	it->flags = (uint16_t)c.flags;
	it->prop = c.prop;
	it->unpacked = size;
	it->packed = c.data.size();
	it->payload.swap(c.data);
	it->op = OP_COMP;
	return 1;
}

static int StreamStore(HANDLE in, HANDLE out, uint64_t size, uint32_t* crc)
{
	std::vector<uint8_t> buf;
	uint32_t c = 0xFFFFFFFFu;
	uint64_t left = size;
	try { buf.resize(1u << 20); } catch (...) { return 0; }
	if (!Seek(in, 0)) return 0;
	while (left) {
		DWORD chunk = left > buf.size() ? (DWORD)buf.size() : (DWORD)left;
		DWORD rd = 0;
		if (!ReadFile(in, buf.data(), chunk, &rd, NULL) || rd != chunk) return 0;
		c = CrcBlock(buf.data(), rd, c);
		if (!WriteAll(out, buf.data(), rd)) return 0;
		left -= rd;
	}
	*crc = c ^ 0xFFFFFFFFu;
	return 1;
}

static int StreamZstdOrStore(const std::wstring& path, uint64_t size, HANDLE out, uint64_t dataOff, OutItem* it)
{
	HANDLE in = OpenRead(path.c_str());
	ZSTD_CCtx* c;
	std::vector<uint8_t> ibuf, obuf;
	uint32_t crc = 0xFFFFFFFFu;
	uint64_t left, produced = 0;
	int level, ended = 0, ok = 0;
	if (in == INVALID_HANDLE_VALUE) return 0;
	if (!Seek(out, dataOff)) { CloseHandle(in); return 0; }
	level = 6;
	if (size <= 8ull * 1024ull * 1024ull) level = 19;
	else if (size <= 256ull * 1024ull * 1024ull) level = 12;
	else if (size <= 1024ull * 1024ull * 1024ull) level = 9;
	c = ZSTD_createCCtx();
	if (!c) { CloseHandle(in); return 0; }
	ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, level);
	ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, 0);
	try { ibuf.resize(1u << 20); obuf.resize(1u << 20); }
	catch (...) { ZSTD_freeCCtx(c); CloseHandle(in); return 0; }
	left = size;
	while (!ended) {
		size_t chunk = 0;
		ZSTD_inBuffer input;
		int last;
		if (left) {
			DWORD want = left > ibuf.size() ? (DWORD)ibuf.size() : (DWORD)left;
			DWORD rd = 0;
			if (!ReadFile(in, ibuf.data(), want, &rd, NULL) || rd != want) break;
			crc = CrcBlock(ibuf.data(), rd, crc);
			left -= rd;
			chunk = rd;
		}
		input.src = ibuf.data();
		input.size = chunk;
		input.pos = 0;
		last = left == 0;
		for (;;) {
			ZSTD_outBuffer output;
			size_t rem;
			output.dst = obuf.data();
			output.size = obuf.size();
			output.pos = 0;
			rem = ZSTD_compressStream2(c, &output, &input, last ? ZSTD_e_end : ZSTD_e_continue);
			if (ZSTD_isError(rem)) { ended = -1; break; }
			if (output.pos && !WriteAll(out, obuf.data(), output.pos)) { ended = -1; break; }
			produced += output.pos;
			if (last) {
				if (rem == 0) { ended = 1; break; }
			} else if (input.pos == input.size) {
				break;
			}
		}
		if (ended < 0) break;
	}
	ZSTD_freeCCtx(c);
	crc ^= 0xFFFFFFFFu;
	if (ended == 1 && produced > 0 && produced < size) {
		it->method = M_ZSTD;
		it->flags = 0;
		it->prop = 0;
		it->crc = crc;
		it->unpacked = size;
		it->packed = produced;
		it->op = OP_STREAM;
		CloseHandle(in);
		return 1;
	}
	if (!Seek(out, dataOff) || !StreamStore(in, out, size, &crc)) { CloseHandle(in); return 0; }
	if (!SetEndOfFile(out)) { CloseHandle(in); return 0; }
	it->method = M_STORE;
	it->flags = 0;
	it->prop = 0;
	it->crc = crc;
	it->unpacked = size;
	it->packed = size;
	it->op = OP_STREAM;
	ok = 1;
	CloseHandle(in);
	return ok;
}

static void PutEntry(std::vector<uint8_t>& dir, uint16_t method, uint16_t flags, uint8_t prop, uint32_t crc, uint32_t attr, uint64_t mtime, uint64_t unpacked, uint64_t packed, uint64_t dataOff, const std::wstring& rel)
{
	std::string name = RelUtf8(rel);
	Put16(dir, method);
	Put16(dir, flags);
	Put16(dir, prop);
	Put32(dir, crc);
	Put32(dir, attr);
	Put64(dir, mtime);
	Put64(dir, unpacked);
	Put64(dir, packed);
	Put64(dir, dataOff);
	Put16(dir, (uint16_t)name.size());
	dir.insert(dir.end(), name.begin(), name.end());
}

static void WriteHeader(HANDLE h, uint64_t baseOff, uint64_t baseSize, uint64_t dirOff, uint32_t count, uint32_t dirBytes)
{
	uint8_t hdr[64];
	int i;
	memset(hdr, 0, sizeof(hdr));
	memcpy(hdr, kMagic, 8);
	hdr[8] = 1;
	hdr[10] = 64;
	for (i = 0; i < 8; ++i) hdr[16 + i] = (uint8_t)(baseOff >> (8 * i));
	for (i = 0; i < 8; ++i) hdr[24 + i] = (uint8_t)(baseSize >> (8 * i));
	for (i = 0; i < 8; ++i) hdr[32 + i] = (uint8_t)(dirOff >> (8 * i));
	for (i = 0; i < 4; ++i) hdr[40 + i] = (uint8_t)(count >> (8 * i));
	for (i = 0; i < 4; ++i) hdr[44 + i] = (uint8_t)(dirBytes >> (8 * i));
	Seek(h, 0);
	WriteAll(h, hdr, 64);
}

static int MoveReplace(const std::wstring& src, const std::wstring& dst)
{
	int i;
	for (i = 0; i < 5; ++i) {
		if (MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			return 1;
		Sleep(50);
	}
	return 0;
}

static SnapFile* FindSnap(Snap* s, const std::wstring& rel)
{
	std::map<std::wstring, SnapFile, ILess>::iterator it = s->files.find(rel);
	if (it == s->files.end()) return NULL;
	return &it->second;
}

extern "C" int OxzCoreIsPkg(const wchar_t* path)
{
	HANDLE h;
	uint8_t b[8];
	int ok;
	h = OpenRead(path);
	if (h == INVALID_HANDLE_VALUE) return 0;
	ok = ReadAt(h, 0, b, 8) && memcmp(b, kMagic, 8) == 0;
	CloseHandle(h);
	return ok;
}

extern "C" void OxzCoreForget(void)
{
	gSnap = Snap();
}

extern "C" int OxzCoreRemember(const wchar_t* workDir, const wchar_t* archiveOrNull)
{
	Snap next;
	std::vector<WorkItem> items;
	std::vector<DirEnt> ents;
	HANDLE h = INVALID_HANDLE_VALUE;
	uint8_t mag[8];
	size_t i;
	DWORD attr;
	next.work = FullPath(workDir);
	next.archive = archiveOrNull ? FullPath(archiveOrNull) : L"";
	next.kind = 0;
	next.baseOff = 0;
	next.baseSize = 0;
	attr = GetFileAttributesW(next.work.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return OXZ_CORE_ERR;
	if (!next.archive.empty()) {
		h = OpenRead(next.archive.c_str());
		if (h != INVALID_HANDLE_VALUE && ReadAt(h, 0, mag, 8)) {
			if (memcmp(mag, kMagic, 8) == 0) {
				uint64_t len = FileLen(h);
				next.kind = 2;
				if (!LoadDir(h, len, ents, &next.baseOff, &next.baseSize)) {
					CloseHandle(h);
					return OXZ_CORE_ERR;
				}
			} else if (Is7zHead(mag, 8)) {
				next.kind = 1;
				next.baseOff = 0;
				next.baseSize = FileLen(h);
			}
		}
		if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	}
	for (i = 0; i < ents.size(); ++i) {
		SnapFile sf;
		sf.rel = ents[i].name;
		sf.size = ents[i].unpacked;
		sf.mtime = ents[i].mtime;
		sf.attr = ents[i].attr;
		sf.isDir = ents[i].method == M_DIR;
		sf.origin = ents[i].method == M_TOMB ? ORIGIN_TOMB : ORIGIN_OVERLAY;
		sf.method = ents[i].method;
		sf.flags = ents[i].flags;
		sf.prop = ents[i].prop;
		sf.crc = ents[i].crc;
		sf.unpacked = ents[i].unpacked;
		sf.packed = ents[i].packed;
		sf.dataOff = ents[i].dataOff;
		sf.dropped = 0;
		if (sf.origin == ORIGIN_TOMB) {
			if (next.files.find(sf.rel) == next.files.end()) next.files[sf.rel] = sf;
		} else {
			next.files[sf.rel] = sf;
		}
	}
	Walk(next.work, L"", items);
	for (i = 0; i < items.size(); ++i) {
		SnapFile sf;
		SnapFile* prev = FindSnap(&next, items[i].rel);
		sf.rel = items[i].rel;
		sf.size = items[i].size;
		sf.mtime = items[i].mtime;
		sf.attr = items[i].attr;
		sf.isDir = items[i].isDir;
		sf.origin = ORIGIN_NEW;
		sf.method = 0;
		sf.flags = 0;
		sf.prop = 0;
		sf.crc = 0;
		sf.unpacked = items[i].size;
		sf.packed = 0;
		sf.dataOff = 0;
		sf.dropped = 0;
		if (prev && prev->origin == ORIGIN_OVERLAY && !sf.isDir && !prev->isDir && prev->unpacked == sf.size && MethodHasPayload(prev->method)) {
			sf.origin = ORIGIN_OVERLAY;
			sf.method = prev->method;
			sf.flags = prev->flags;
			sf.prop = prev->prop;
			sf.crc = prev->crc;
			sf.unpacked = prev->unpacked;
			sf.packed = prev->packed;
			sf.dataOff = prev->dataOff;
		} else if (prev && prev->origin == ORIGIN_OVERLAY && sf.isDir && prev->isDir) {
			sf.origin = ORIGIN_OVERLAY;
			sf.method = M_DIR;
		} else if (prev && prev->origin == ORIGIN_OVERLAY) {
			sf.origin = ORIGIN_NEW;
		} else if (next.kind == 1 || (next.kind == 2 && next.baseSize > 0)) {
			sf.origin = ORIGIN_BASE;
		}
		next.files[sf.rel] = sf;
	}
	gSnap = next;
	return OXZ_CORE_OK;
}

extern "C" int OxzCoreSave(const wchar_t* workDir, const wchar_t* archive, unsigned long long* outBytes, OxzCoreProgFn prog, void* progUser)
{
	std::wstring work = FullPath(workDir);
	std::wstring dest = FullPath(archive);
	std::wstring building;
	std::vector<WorkItem> items;
	std::vector<std::wstring> tombs;
	HANDLE out = INVALID_HANDLE_VALUE;
	HANDLE baseFile = INVALID_HANDLE_VALUE;
	std::vector<uint8_t> dir;
	uint64_t pos = 64;
	uint64_t baseOff = 0, baseSize = 0;
	uint32_t count = 0;
	int snapOk, baseKept = 0;
	size_t i, nfiles = 0;
	unsigned index = 0;
	DWORD attr;
	if (outBytes) *outBytes = 0;
	if (work.empty() || dest.empty()) return OXZ_CORE_ERR;
	attr = GetFileAttributesW(work.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return OXZ_CORE_ERR;
	Walk(work, L"", items);
	snapOk = SamePath(gSnap.work, work) && !gSnap.archive.empty();
	if (items.empty() && !snapOk) return OXZ_CORE_EMPTY;
	if (snapOk && (gSnap.kind == 1 || (gSnap.kind == 2 && gSnap.baseSize > 0))) {
		baseFile = OpenRead(gSnap.archive.c_str());
		if (baseFile != INVALID_HANDLE_VALUE) {
			uint64_t len = FileLen(baseFile);
			int spanOk = gSnap.kind == 1 ? (gSnap.baseOff == 0 && len == gSnap.baseSize && len > 0)
				: (gSnap.baseSize > 0 && gSnap.baseOff <= len && gSnap.baseSize <= len - gSnap.baseOff);
			if (spanOk) {
				for (std::map<std::wstring, SnapFile, ILess>::iterator it = gSnap.files.begin(); it != gSnap.files.end(); ++it) {
					SnapFile& sf = it->second;
					size_t k;
					if (sf.origin != ORIGIN_BASE || sf.isDir) continue;
					for (k = 0; k < items.size(); ++k) {
						if (!SamePath(items[k].rel, sf.rel)) continue;
						if (!items[k].isDir && items[k].size == sf.size && items[k].mtime == sf.mtime) baseKept = 1;
						break;
					}
					if (baseKept) break;
				}
			}
			if (!baseKept) { CloseHandle(baseFile); baseFile = INVALID_HANDLE_VALUE; }
		}
	}
	building = dest + L".building";
	DeleteFileW(building.c_str());
	out = CreateFileW(building.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (out == INVALID_HANDLE_VALUE) {
		if (baseFile != INVALID_HANDLE_VALUE) CloseHandle(baseFile);
		return OXZ_CORE_ERR;
	}
	{
		uint8_t zeros[64];
		memset(zeros, 0, sizeof(zeros));
		if (!WriteAll(out, zeros, 64)) goto fail;
	}
	if (baseKept) {
		baseOff = 64;
		baseSize = gSnap.baseSize;
		if (!CopyRange(baseFile, gSnap.baseOff, baseSize, out)) goto fail;
		pos = 64 + baseSize;
	}
	if (baseFile != INVALID_HANDLE_VALUE) { CloseHandle(baseFile); baseFile = INVALID_HANDLE_VALUE; }
	for (i = 0; i < items.size(); ++i) if (!items[i].isDir) nfiles++;
	{
		std::map<std::wstring, int, ILess> live;
		unsigned total;
		for (i = 0; i < items.size(); ++i) live[items[i].rel] = 1;
		total = (unsigned)items.size();
		if (snapOk) {
			for (std::map<std::wstring, SnapFile, ILess>::iterator it = gSnap.files.begin(); it != gSnap.files.end(); ++it) {
				if (it->second.origin != ORIGIN_OVERLAY || it->second.dropped) continue;
				if (live.find(it->second.rel) == live.end()) total++;
			}
		}
		if (!total) total = 1;
		{
			struct Prepared { int ready; OutItem it; Prepared() : ready(0) {} };
			std::vector<Prepared> prepared;
			std::vector<size_t> smallPick, largePick;
			int pulsed = 0;
			auto must = [&](size_t ix) -> int {
				WorkItem& w = items[ix];
				SnapFile* sf = snapOk ? FindSnap(&gSnap, w.rel) : NULL;
				if (w.isDir) return 0;
				if (sf && !sf->dropped && sf->origin == ORIGIN_OVERLAY && !sf->isDir && sf->size == w.size && sf->mtime == w.mtime && MethodHasPayload(sf->method)) return 0;
				if (baseKept && sf && sf->origin == ORIGIN_BASE && !sf->isDir && sf->size == w.size && sf->mtime == w.mtime) return 0;
				return 1;
			};
			auto runPick = [&](size_t ix) {
				Prepared& pr = prepared[ix];
				PulseGuard guard(items[ix].rel, items[ix].size);
				pr.it.rel = items[ix].rel;
				pr.it.attr = items[ix].attr;
				pr.it.mtime = items[ix].mtime;
				try {
					if (FillPicked(items[ix].disk, items[ix].size, &pr.it)) pr.ready = 1;
					else pr.ready = -1;
				} catch (...) { pr.ready = -1; }
			};
			try { prepared.resize(items.size()); }
			catch (...) { goto fail; }
			for (i = 0; i < items.size(); ++i) {
				if (!must(i)) continue;
				if (items[i].size >= 8ull * 1024ull * 1024ull) largePick.push_back(i);
				else smallPick.push_back(i);
			}
			{
				SavePulse pulse;
				struct PulseArm {
					int on;
					PulseArm() : on(0) {}
					void arm(SavePulse* p) { gPulse = p; on = 1; }
					~PulseArm() { if (on) gPulse = NULL; }
				} arm;
				pulse.fn = prog;
				pulse.user = progUser;
				pulse.total = (unsigned)(largePick.size() + smallPick.size());
				for (i = 0; i < largePick.size(); ++i) pulse.bytesAll += items[largePick[i]].size ? items[largePick[i]].size : 1;
				for (i = 0; i < smallPick.size(); ++i) pulse.bytesAll += items[smallPick[i]].size ? items[smallPick[i]].size : 1;
				if (prog && pulse.total) { arm.arm(&pulse); pulsed = 1; }
			for (i = 0; i < largePick.size(); ++i) runPick(largePick[i]);
			if (smallPick.size() == 1) runPick(smallPick[0]);
			else if (!smallPick.empty()) {
				unsigned ft = HwCap() / 3;
				unsigned t;
				std::atomic<size_t> cursor(0);
				std::vector<std::thread> pool;
				if (ft < 1) ft = 1;
				if (ft > smallPick.size()) ft = (unsigned)smallPick.size();
				for (t = 0; t < ft; ++t) {
					pool.push_back(std::thread([&]() {
						tInFilePool = 1;
						for (;;) {
							size_t c = cursor.fetch_add(1);
							if (c >= smallPick.size()) break;
							runPick(smallPick[c]);
						}
						tInFilePool = 0;
					}));
					BoostThread(pool.back());
				}
				for (t = 0; t < pool.size(); ++t) pool[t].join();
			}
			}
	for (i = 0; i < items.size(); ++i) {
		WorkItem& w = items[i];
		SnapFile* sf = snapOk ? FindSnap(&gSnap, w.rel) : NULL;
		std::string utf;
		int pct0 = 0;
		utf = RelUtf8(w.rel);
		if (utf.empty() || utf.size() > 4096) goto fail;
		if (prog && !pulsed) prog(w.rel.c_str(), index, total, pct0, progUser);
		if (w.isDir) {
			PutEntry(dir, M_DIR, 0, 0, 0, w.attr, w.mtime, 0, 0, 0, w.rel);
			count++;
		} else if (sf && !sf->dropped && sf->origin == ORIGIN_OVERLAY && !sf->isDir && sf->size == w.size && sf->mtime == w.mtime && MethodHasPayload(sf->method)) {
			HANDLE src = OpenRead(gSnap.archive.c_str());
			uint64_t len;
			int copied = 0;
			if (src != INVALID_HANDLE_VALUE) {
				len = FileLen(src);
				if (sf->dataOff <= len && sf->packed <= len - sf->dataOff && Seek(out, pos))
					copied = CopyRange(src, sf->dataOff, sf->packed, out);
				CloseHandle(src);
			}
			if (!copied) {
				OutItem it;
				it.rel = w.rel;
				it.attr = w.attr;
				it.mtime = w.mtime;
				if (!FillPicked(w.disk, w.size, &it)) {
					if (!StreamZstdOrStore(w.disk, w.size, out, pos, &it)) goto fail;
				} else {
					if (!Seek(out, pos) || !WriteAll(out, it.payload.data(), it.payload.size())) goto fail;
				}
				PutEntry(dir, it.method, it.flags, it.prop, it.crc, w.attr, w.mtime, it.unpacked, it.packed, pos, w.rel);
				pos += it.packed;
			} else {
				PutEntry(dir, sf->method, sf->flags, sf->prop, sf->crc, w.attr, w.mtime, sf->unpacked, sf->packed, pos, w.rel);
				pos += sf->packed;
			}
			count++;
		} else if (baseKept && sf && sf->origin == ORIGIN_BASE && !sf->isDir && sf->size == w.size && sf->mtime == w.mtime) {
			/* 変わっていない base の中身。書庫へは足さない。 */
		} else if (i < prepared.size() && prepared[i].ready == 1) {
			OutItem& it = prepared[i].it;
			if (!Seek(out, pos) || (it.packed && !WriteAll(out, it.payload.data(), it.payload.size()))) goto fail;
			PutEntry(dir, it.method, it.flags, it.prop, it.crc, w.attr, w.mtime, it.unpacked, it.packed, pos, w.rel);
			pos += it.packed;
			count++;
		} else {
			OutItem it;
			it.rel = w.rel;
			it.attr = w.attr;
			it.mtime = w.mtime;
			if (!FillPicked(w.disk, w.size, &it)) {
				if (!StreamZstdOrStore(w.disk, w.size, out, pos, &it)) goto fail;
			} else {
				if (!Seek(out, pos) || (it.packed && !WriteAll(out, it.payload.data(), it.payload.size()))) goto fail;
			}
			PutEntry(dir, it.method, it.flags, it.prop, it.crc, w.attr, w.mtime, it.unpacked, it.packed, pos, w.rel);
			pos += it.packed;
			count++;
		}
		if (prog && !pulsed) prog(w.rel.c_str(), index, total, 100, progUser);
		index++;
	}
		}
	if (snapOk) {
		for (std::map<std::wstring, SnapFile, ILess>::iterator it = gSnap.files.begin(); it != gSnap.files.end(); ++it) {
			SnapFile& sf = it->second;
			HANDLE src;
			uint64_t len;
			int copied = 0;
			if (sf.origin != ORIGIN_OVERLAY || sf.dropped) continue;
			if (live.find(sf.rel) != live.end()) continue;
			if (prog) prog(sf.rel.c_str(), index, total, 0, progUser);
			if (sf.isDir) {
				PutEntry(dir, M_DIR, 0, 0, 0, sf.attr, sf.mtime, 0, 0, 0, sf.rel);
				count++;
			} else if (MethodHasPayload(sf.method)) {
				src = OpenRead(gSnap.archive.c_str());
				if (src != INVALID_HANDLE_VALUE) {
					len = FileLen(src);
					if (sf.dataOff <= len && sf.packed <= len - sf.dataOff && Seek(out, pos))
						copied = CopyRange(src, sf.dataOff, sf.packed, out);
					CloseHandle(src);
				}
				if (!copied) goto fail;
				PutEntry(dir, sf.method, sf.flags, sf.prop, sf.crc, sf.attr, sf.mtime, sf.unpacked, sf.packed, pos, sf.rel);
				pos += sf.packed;
				count++;
			}
			if (prog) prog(sf.rel.c_str(), index, total, 100, progUser);
			index++;
		}
	}
	}
	if (baseKept) {
		for (std::map<std::wstring, SnapFile, ILess>::iterator it = gSnap.files.begin(); it != gSnap.files.end(); ++it) {
			size_t k;
			int onDisk = 0;
			if (it->second.origin == ORIGIN_NEW) continue;
			if (it->second.origin == ORIGIN_OVERLAY && !it->second.dropped) continue;
			for (k = 0; k < items.size(); ++k) if (SamePath(items[k].rel, it->second.rel)) { onDisk = 1; break; }
			if (!onDisk) tombs.push_back(it->second.rel);
		}
		std::sort(tombs.begin(), tombs.end(), [](const std::wstring& a, const std::wstring& b) { return a.size() > b.size(); });
		for (i = 0; i < tombs.size(); ++i) {
			std::string utf = RelUtf8(tombs[i]);
			if (utf.empty() || utf.size() > 4096) goto fail;
			PutEntry(dir, M_TOMB, 0, 0, 0, 0, 0, 0, 0, 0, tombs[i]);
			count++;
		}
	}
	if (dir.size() > 0xFFFFFFFFull) goto fail;
	if (!Seek(out, pos) || (!dir.empty() && !WriteAll(out, dir.data(), dir.size()))) goto fail;
	WriteHeader(out, baseKept ? baseOff : 0, baseKept ? baseSize : 0, pos, count, (uint32_t)dir.size());
	FlushFileBuffers(out);
	CloseHandle(out);
	out = INVALID_HANDLE_VALUE;
	if (!MoveReplace(building, dest)) { DeleteFileW(building.c_str()); return OXZ_CORE_ERR; }
	if (outBytes) {
		HANDLE rh = OpenRead(dest.c_str());
		if (rh != INVALID_HANDLE_VALUE) { *outBytes = FileLen(rh); CloseHandle(rh); }
	}
	OxzCoreRemember(workDir, archive);
	(void)nfiles;
	return OXZ_CORE_OK;
fail:
	if (baseFile != INVALID_HANDLE_VALUE) CloseHandle(baseFile);
	if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
	DeleteFileW(building.c_str());
	return OXZ_CORE_ERR;
}

static int WriteMember(const std::wstring& path, const uint8_t* p, size_t n, uint64_t mtime, uint32_t attr)
{
	HANDLE h;
	FILETIME ft;
	EnsureParents(path);
	h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	if (n && !WriteAll(h, p, n)) { CloseHandle(h); DeleteFileW(path.c_str()); return 0; }
	ft = UToFt(mtime);
	SetFileTime(h, &ft, &ft, &ft);
	CloseHandle(h);
	if (attr) SetFileAttributesW(path.c_str(), attr);
	return 1;
}

static int StreamDecode(HANDLE arc, const DirEnt& e, const std::wstring& path)
{
	HANDLE out;
	std::vector<uint8_t> ibuf, obuf;
	uint32_t crc = 0xFFFFFFFFu;
	uint64_t left = e.packed;
	uint64_t produced = 0;
	FILETIME ft;
	EnsureParents(path);
	out = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (out == INVALID_HANDLE_VALUE) return 0;
	try { ibuf.resize(1u << 20); obuf.resize(1u << 20); }
	catch (...) { CloseHandle(out); return 0; }
	if (!Seek(arc, e.dataOff)) { CloseHandle(out); return 0; }
	if (e.method == M_STORE) {
		while (left) {
			DWORD chunk = left > ibuf.size() ? (DWORD)ibuf.size() : (DWORD)left;
			DWORD rd = 0;
			if (!ReadFile(arc, ibuf.data(), chunk, &rd, NULL) || rd != chunk) { CloseHandle(out); return 0; }
			crc = CrcBlock(ibuf.data(), rd, crc);
			if (!WriteAll(out, ibuf.data(), rd)) { CloseHandle(out); return 0; }
			left -= rd;
			produced += rd;
		}
	} else if (e.method == M_ZSTD) {
		ZSTD_DCtx* d = ZSTD_createDCtx();
		if (!d) { CloseHandle(out); return 0; }
		while (left || produced < e.unpacked) {
			DWORD chunk = 0;
			ZSTD_inBuffer input;
			if (left) {
				DWORD want = left > ibuf.size() ? (DWORD)ibuf.size() : (DWORD)left;
				DWORD rd = 0;
				if (!ReadFile(arc, ibuf.data(), want, &rd, NULL) || rd != want) { ZSTD_freeDCtx(d); CloseHandle(out); return 0; }
				left -= rd;
				chunk = rd;
			}
			input.src = ibuf.data();
			input.size = chunk;
			input.pos = 0;
			for (;;) {
				ZSTD_outBuffer output;
				size_t rem;
				size_t before = input.pos;
				output.dst = obuf.data();
				output.size = obuf.size();
				output.pos = 0;
				rem = ZSTD_decompressStream(d, &output, &input);
				if (ZSTD_isError(rem)) { ZSTD_freeDCtx(d); CloseHandle(out); return 0; }
				if (output.pos) {
					crc = CrcBlock(obuf.data(), output.pos, crc);
					if (!WriteAll(out, obuf.data(), output.pos)) { ZSTD_freeDCtx(d); CloseHandle(out); return 0; }
					produced += output.pos;
				}
				if (rem == 0 && input.pos == input.size) break;
				if (input.pos == input.size && output.pos == 0) {
					if (left == 0) { ZSTD_freeDCtx(d); CloseHandle(out); return 0; }
					break;
				}
				if (input.pos == before && output.pos == 0) { ZSTD_freeDCtx(d); CloseHandle(out); return 0; }
			}
			if (produced > e.unpacked) { ZSTD_freeDCtx(d); CloseHandle(out); return 0; }
			if (left == 0) break;
		}
		ZSTD_freeDCtx(d);
	} else {
		CloseHandle(out);
		return 0;
	}
	crc ^= 0xFFFFFFFFu;
	ft = UToFt(e.mtime);
	SetFileTime(out, &ft, &ft, &ft);
	CloseHandle(out);
	if (produced != e.unpacked || crc != e.crc) { DeleteFileW(path.c_str()); return 0; }
	if (e.attr) SetFileAttributesW(path.c_str(), e.attr);
	return 1;
}

static void DeleteAny(const std::wstring& path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	if (a == INVALID_FILE_ATTRIBUTES) return;
	SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
	if (a & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(path.c_str());
	else DeleteFileW(path.c_str());
}

extern "C" int OxzCoreExtract(const wchar_t* archive, const wchar_t* dest, OxzCoreBaseFn baseFn, void* baseUser, OxzCoreProgFn prog, void* progUser)
{
	std::wstring root = FullPath(dest);
	std::wstring arc = FullPath(archive);
	std::vector<DirEnt> ents;
	uint64_t baseOff = 0, baseSize = 0, len;
	HANDLE h;
	std::wstring tempBase;
	unsigned overlays = 0, seen = 0;
	size_t i;
	std::vector<std::wstring> tombs;
	h = OpenRead(arc.c_str());
	if (h == INVALID_HANDLE_VALUE) return OXZ_CORE_ERR;
	len = FileLen(h);
	if (!LoadDir(h, len, ents, &baseOff, &baseSize)) { CloseHandle(h); return OXZ_CORE_ERR; }
	CreateDirectoryW(root.c_str(), NULL);
	if (baseSize) {
		wchar_t tmp[MAX_PATH];
		wchar_t name[MAX_PATH];
		HANDLE th;
		if (!baseFn) { CloseHandle(h); return OXZ_CORE_ERR; }
		if (!GetTempPathW(MAX_PATH, tmp)) { CloseHandle(h); return OXZ_CORE_ERR; }
		_snwprintf_s(name, _TRUNCATE, L"%soxzb%08X.oxz", tmp, GetTickCount());
		tempBase = name;
		th = CreateFileW(tempBase.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (th == INVALID_HANDLE_VALUE) { CloseHandle(h); return OXZ_CORE_ERR; }
		if (!CopyRange(h, baseOff, baseSize, th)) { CloseHandle(th); DeleteFileW(tempBase.c_str()); CloseHandle(h); return OXZ_CORE_ERR; }
		CloseHandle(th);
		if (baseFn(tempBase.c_str(), root.c_str(), baseUser) != 0) {
			DeleteFileW(tempBase.c_str());
			CloseHandle(h);
			return OXZ_CORE_ERR;
		}
		DeleteFileW(tempBase.c_str());
	}
	for (i = 0; i < ents.size(); ++i) {
		if (ents[i].method == M_TOMB) tombs.push_back(ents[i].name);
		else if (MethodHasPayload(ents[i].method)) overlays++;
	}
	std::sort(tombs.begin(), tombs.end(), [](const std::wstring& a, const std::wstring& b) { return a.size() > b.size(); });
	for (i = 0; i < tombs.size(); ++i) {
		std::wstring full = FullPath(JoinPath(root, tombs[i]).c_str());
		if (!UnderRoot(root, full)) { CloseHandle(h); return OXZ_CORE_ERR; }
		DeleteAny(full);
	}
	for (i = 0; i < ents.size(); ++i) {
		std::wstring full;
		if (ents[i].method != M_DIR) continue;
		full = FullPath(JoinPath(root, ents[i].name).c_str());
		if (!UnderRoot(root, full)) { CloseHandle(h); return OXZ_CORE_ERR; }
		EnsureParents(full);
		CreateDirectoryW(full.c_str(), NULL);
	}
	{
		struct ExJob { size_t ei; std::wstring full; int large; };
		std::vector<ExJob> small, big;
		unsigned total = overlays ? overlays : 1;
		std::atomic<unsigned> done(0);
		std::atomic<int> failed(0);
		auto runOne = [&](const ExJob& job, HANDLE rh) -> int {
			std::vector<uint8_t> packed, plain;
			int dr;
			const DirEnt& ent = ents[job.ei];
			if (failed.load()) return 0;
			if (prog) prog(ent.name.c_str(), done.load(), total, 0, progUser);
			if (ent.packed != (uint64_t)(size_t)ent.packed) dr = -1;
			else {
				try { packed.resize((size_t)ent.packed); dr = 1; }
				catch (...) { dr = -1; }
				if (dr == 1) {
					if (ent.packed && !ReadAt(rh, ent.dataOff, packed.data(), packed.size())) return 0;
					dr = DecodeRaw(ent, packed.data(), plain);
				}
			}
			if (dr == 1) {
				if (!WriteMember(job.full, plain.data(), plain.size(), ent.mtime, ent.attr)) return 0;
			} else if (dr < 0 && ent.flags == 0 && (ent.method == M_STORE || ent.method == M_ZSTD)) {
				if (rh != h) return 2;
				if (!StreamDecode(h, ent, job.full)) return 0;
			} else {
				return 0;
			}
			done.fetch_add(1);
			if (prog) prog(ent.name.c_str(), done.load(), total, 100, progUser);
			return 1;
		};
		for (i = 0; i < ents.size(); ++i) {
			ExJob job;
			if (!MethodHasPayload(ents[i].method)) continue;
			job.ei = i;
			job.full = FullPath(JoinPath(root, ents[i].name).c_str());
			job.large = (ents[i].unpacked >= 8ull * 1024ull * 1024ull || ents[i].packed >= 8ull * 1024ull * 1024ull) ? 1 : 0;
			if (!UnderRoot(root, job.full)) { CloseHandle(h); return OXZ_CORE_ERR; }
			if (job.large || ents[i].packed != (uint64_t)(size_t)ents[i].packed) big.push_back(job);
			else small.push_back(job);
		}
		for (i = 0; i < big.size(); ++i) {
			int rc = runOne(big[i], h);
			if (rc == 2) {
				if (!StreamDecode(h, ents[big[i].ei], big[i].full)) rc = 0;
				else {
					done.fetch_add(1);
					if (prog) prog(ents[big[i].ei].name.c_str(), done.load(), total, 100, progUser);
					rc = 1;
				}
			}
			if (rc != 1) { CloseHandle(h); return OXZ_CORE_ERR; }
		}
		if (small.size() <= 1) {
			for (i = 0; i < small.size(); ++i) {
				if (runOne(small[i], h) != 1) { CloseHandle(h); return OXZ_CORE_ERR; }
			}
		} else {
			unsigned ft = HwCap();
			unsigned t;
			std::atomic<size_t> cursor(0);
			std::vector<std::thread> pool;
			if (ft > small.size()) ft = (unsigned)small.size();
			for (t = 0; t < ft; ++t) {
				pool.push_back(std::thread([&]() {
					HANDLE rh = OpenRead(arc.c_str());
					tInFilePool = 1;
					if (rh == INVALID_HANDLE_VALUE) { failed.store(1); return; }
					for (;;) {
						size_t c = cursor.fetch_add(1);
						int rc;
						if (c >= small.size() || failed.load()) break;
						rc = runOne(small[c], rh);
						if (rc != 1) failed.store(1);
					}
					CloseHandle(rh);
					tInFilePool = 0;
				}));
				BoostThread(pool.back());
			}
			for (t = 0; t < pool.size(); ++t) pool[t].join();
			if (failed.load()) { CloseHandle(h); return OXZ_CORE_ERR; }
		}
		seen = done.load();
	}
	CloseHandle(h);
	if (prog && overlays == 0) prog(L"", 0, 1, 100, progUser);
	return OXZ_CORE_OK;
}

static int RelUnder(const std::wstring& rel, const std::wstring& root)
{
	if (_wcsicmp(rel.c_str(), root.c_str()) == 0) return 1;
	return rel.size() > root.size() && _wcsnicmp(rel.c_str(), root.c_str(), root.size()) == 0 && rel[root.size()] == L'\\';
}

extern "C" int OxzCoreSetLevel(int level)
{
	if (level < 1) level = 1;
	if (level > 10) level = 10;
	gUserLevel = level;
	return gUserLevel;
}

extern "C" int OxzCoreGetLevel(void)
{
	return gUserLevel;
}

extern "C" void OxzCoreSetPpmd(int on)
{
	gUsePpmd = on ? 1 : 0;
}

extern "C" int OxzCoreGetPpmd(void)
{
	return gUsePpmd;
}

extern "C" int OxzCoreDrop(const wchar_t* rel)
{
	std::wstring key;
	int n = 0;
	if (!rel || !rel[0]) return OXZ_CORE_ERR;
	key = SlashToBack(rel);
	for (std::map<std::wstring, SnapFile, ILess>::iterator it = gSnap.files.begin(); it != gSnap.files.end(); ++it) {
		if (!RelUnder(it->second.rel, key)) continue;
		it->second.dropped = 1;
		n++;
	}
	return n ? OXZ_CORE_OK : OXZ_CORE_ERR;
}

extern "C" int OxzCoreRename(const wchar_t* oldRel, const wchar_t* newRel)
{
	std::wstring oldKey, newKey;
	std::vector<std::pair<std::wstring, SnapFile>> add;
	std::vector<std::wstring> del;
	size_t i;
	if (!oldRel || !oldRel[0] || !newRel || !newRel[0]) return OXZ_CORE_ERR;
	oldKey = SlashToBack(oldRel);
	newKey = SlashToBack(newRel);
	if (SamePath(oldKey, newKey)) return OXZ_CORE_OK;
	for (std::map<std::wstring, SnapFile, ILess>::iterator it = gSnap.files.begin(); it != gSnap.files.end(); ++it) {
		SnapFile sf;
		std::wstring rest;
		if (!RelUnder(it->second.rel, oldKey)) continue;
		sf = it->second;
		rest = sf.rel.size() > oldKey.size() ? sf.rel.substr(oldKey.size()) : L"";
		sf.rel = newKey + rest;
		sf.dropped = 0;
		add.push_back(std::make_pair(sf.rel, sf));
		del.push_back(it->first);
	}
	if (add.empty()) return OXZ_CORE_ERR;
	for (i = 0; i < del.size(); ++i) gSnap.files.erase(del[i]);
	for (i = 0; i < add.size(); ++i) gSnap.files[add[i].first] = add[i].second;
	return OXZ_CORE_OK;
}

extern "C" int OxzCoreMaterialize(const wchar_t* rel, const wchar_t* destPath)
{
	SnapFile* sf;
	HANDLE h;
	std::vector<uint8_t> packed, plain;
	DirEnt e;
	uint64_t len;
	std::wstring key;
	if (!rel || !destPath) return OXZ_CORE_ERR;
	key = SlashToBack(rel);
	sf = FindSnap(&gSnap, key);
	if (!sf || sf->dropped || sf->isDir || !MethodHasPayload(sf->method)) return OXZ_CORE_ERR;
	if (sf->unpacked != (uint64_t)(size_t)sf->unpacked) return OXZ_CORE_ERR;
	h = OpenRead(gSnap.archive.c_str());
	if (h == INVALID_HANDLE_VALUE) return OXZ_CORE_ERR;
	len = FileLen(h);
	if (sf->dataOff > len || sf->packed > len - sf->dataOff) { CloseHandle(h); return OXZ_CORE_ERR; }
	try { packed.resize((size_t)sf->packed); }
	catch (...) { CloseHandle(h); return OXZ_CORE_ERR; }
	if (sf->packed && !ReadAt(h, sf->dataOff, packed.data(), packed.size())) { CloseHandle(h); return OXZ_CORE_ERR; }
	CloseHandle(h);
	e.method = sf->method;
	e.flags = sf->flags;
	e.prop = sf->prop;
	e.crc = sf->crc;
	e.attr = sf->attr;
	e.mtime = sf->mtime;
	e.unpacked = sf->unpacked;
	e.packed = sf->packed;
	e.dataOff = sf->dataOff;
	e.name = sf->rel;
	if (DecodeRaw(e, packed.data(), plain) != 1) return OXZ_CORE_ERR;
	if (!WriteMember(destPath, plain.empty() ? NULL : plain.data(), plain.size(), sf->mtime, sf->attr)) return OXZ_CORE_ERR;
	return OXZ_CORE_OK;
}

static void FillItem(OxzCoreItem* dst, const std::wstring& name, int isDir, int tomb, int overlay, uint64_t unpacked, uint64_t mtime, uint16_t method, uint16_t flags, uint64_t packed, uint32_t attr)
{
	dst->name = _wcsdup(name.c_str());
	dst->isDir = isDir;
	dst->tomb = tomb;
	dst->overlay = overlay;
	dst->unpacked = unpacked;
	dst->mtime = mtime;
	dst->method = method;
	dst->flags = flags;
	dst->packed = packed;
	dst->attr = attr;
}

extern "C" int OxzCoreList(OxzCoreItem** items, int* count)
{
	std::vector<SnapFile*> keep;
	OxzCoreItem* arr;
	size_t i;
	if (!items || !count) return OXZ_CORE_ERR;
	*items = NULL;
	*count = 0;
	for (std::map<std::wstring, SnapFile, ILess>::iterator it = gSnap.files.begin(); it != gSnap.files.end(); ++it) {
		if (it->second.dropped || it->second.origin == ORIGIN_TOMB || it->second.method == M_TOMB) continue;
		keep.push_back(&it->second);
	}
	arr = (OxzCoreItem*)calloc(keep.size() ? keep.size() : 1, sizeof(OxzCoreItem));
	if (!arr) return OXZ_CORE_ERR;
	for (i = 0; i < keep.size(); ++i) {
		SnapFile* sf = keep[i];
		FillItem(&arr[i], sf->rel, sf->isDir, 0, MethodHasPayload(sf->method) ? 0 : -1, sf->unpacked, sf->mtime, sf->method, sf->flags, sf->packed, sf->attr);
		if (!arr[i].name) { OxzCoreFreeItems(arr, (int)i); return OXZ_CORE_ERR; }
	}
	*items = arr;
	*count = (int)keep.size();
	return OXZ_CORE_OK;
}

extern "C" int OxzCoreReadDir(const wchar_t* archive, OxzCoreItem** items, int* count, uint64_t* baseOff, uint64_t* baseSize)
{
	HANDLE h;
	std::vector<DirEnt> ents;
	uint64_t off = 0, sz = 0, len;
	OxzCoreItem* arr;
	uint32_t i;
	if (!items || !count) return OXZ_CORE_ERR;
	*items = NULL;
	*count = 0;
	h = OpenRead(archive);
	if (h == INVALID_HANDLE_VALUE) return OXZ_CORE_ERR;
	len = FileLen(h);
	if (!LoadDir(h, len, ents, &off, &sz)) { CloseHandle(h); return OXZ_CORE_ERR; }
	CloseHandle(h);
	if (baseOff) *baseOff = off;
	if (baseSize) *baseSize = sz;
	if (ents.size() > 0x7FFFFFFF) return OXZ_CORE_ERR;
	arr = (OxzCoreItem*)calloc(ents.size() ? ents.size() : 1, sizeof(OxzCoreItem));
	if (!arr) return OXZ_CORE_ERR;
	for (i = 0; i < ents.size(); ++i) {
		FillItem(&arr[i], ents[i].name, ents[i].method == M_DIR, ents[i].method == M_TOMB,
			MethodHasPayload(ents[i].method) ? (int)i : -1, ents[i].unpacked, ents[i].mtime,
			ents[i].method, ents[i].flags, ents[i].packed, ents[i].attr);
		if (!arr[i].name) { OxzCoreFreeItems(arr, (int)i); return OXZ_CORE_ERR; }
	}
	*items = arr;
	*count = (int)ents.size();
	return OXZ_CORE_OK;
}

extern "C" void OxzCoreFreeItems(OxzCoreItem* items, int count)
{
	int i;
	if (!items) return;
	for (i = 0; i < count; ++i) free(items[i].name);
	free(items);
}

extern "C" int OxzCoreReadOverlay(const wchar_t* archive, int overlay, void** data, size_t* size)
{
	HANDLE h;
	std::vector<DirEnt> ents;
	std::vector<uint8_t> packed, plain;
	uint64_t off = 0, sz = 0, len;
	void* block;
	if (!data || !size || overlay < 0) return OXZ_CORE_ERR;
	*data = NULL;
	*size = 0;
	h = OpenRead(archive);
	if (h == INVALID_HANDLE_VALUE) return OXZ_CORE_ERR;
	len = FileLen(h);
	if (!LoadDir(h, len, ents, &off, &sz) || (size_t)overlay >= ents.size() || !MethodHasPayload(ents[overlay].method)) {
		CloseHandle(h);
		return OXZ_CORE_ERR;
	}
	try { packed.resize((size_t)ents[overlay].packed); }
	catch (...) { CloseHandle(h); return OXZ_CORE_ERR; }
	if (ents[overlay].packed && !ReadAt(h, ents[overlay].dataOff, packed.data(), packed.size())) { CloseHandle(h); return OXZ_CORE_ERR; }
	CloseHandle(h);
	if (DecodeRaw(ents[overlay], packed.data(), plain) != 1) return OXZ_CORE_ERR;
	block = malloc(plain.size() ? plain.size() : 1);
	if (!block) return OXZ_CORE_ERR;
	if (!plain.empty()) memcpy(block, plain.data(), plain.size());
	*data = block;
	*size = plain.size();
	return OXZ_CORE_OK;
}

extern "C" void OxzCoreFree(void* p)
{
	free(p);
}

extern "C" int OxzBzipLastParallel(void);

extern "C" int OxzSpeedSelfTest(void)
{
	int saved = gUserLevel;
	int code = 0;
	std::vector<uint8_t> raw, a, b, packed;
	size_t i;
	gUserLevel = 1;
	gLevelBytes = 0;
	raw.resize(4096);
	for (i = 0; i < raw.size(); ++i) raw[i] = (uint8_t)(i * 17 + 3);
	{
		uint32_t tab[256];
		uint32_t c = 0xFFFFFFFFu;
		int n, bit;
		for (n = 0; n < 256; ++n) {
			uint32_t r = (uint32_t)n;
			for (bit = 0; bit < 8; ++bit) r = (r >> 1) ^ (0xEDB88320u & (0u - (r & 1u)));
			tab[n] = r;
		}
		for (i = 0; i < raw.size(); ++i) c = tab[(c ^ raw[i]) & 255] ^ (c >> 8);
		if ((c ^ 0xFFFFFFFFu) != CrcFinish(raw.data(), raw.size())) code = 1;
	}
	if (!code) {
		a = raw;
		b = raw;
		for (i = a.size() - 1; i > 0; --i) a[i] = (uint8_t)(a[i] - a[i - 1]);
		Delta1Enc(b.data(), b.size());
		if (a != b) code = 2;
		else {
			Delta1Dec(b.data(), b.size());
			if (b != raw) code = 2;
		}
	}
	if (!code) {
		a = raw;
		b = raw;
		for (i = a.size() - 1; i >= 4; --i) {
			a[i] = (uint8_t)(a[i] - a[i - 4]);
			if (i == 4) break;
		}
		Delta4Enc(b.data(), b.size());
		if (a != b) code = 3;
		else {
			Delta4Dec(b.data(), b.size());
			if (b != raw) code = 3;
		}
	}
	if (!code) {
		raw.resize(3u * 1024u * 1024u);
		for (i = 0; i < raw.size(); ++i) raw[i] = (uint8_t)((i * 17) ^ (i >> 4) ^ ((i / 4096) * 13));
		{
			unsigned int dest = (unsigned int)(raw.size() + raw.size() / 50 + 600);
			std::vector<uint8_t> stock(dest), plain(raw.size());
			int rc = BZ2_bzBuffToBuffCompress((char*)stock.data(), &dest, (char*)raw.data(), (unsigned int)raw.size(), 1, 0, 30);
			int dec = 0;
			if (rc != BZ_OK) code = 41;
			else {
				dec = OxzBzipDecompress(stock.data(), dest, plain.data(), plain.size(), 4);
				if (!dec) code = 42;
				else if (plain != raw) code = 43;
				else if (!OxzBzipLastParallel()) code = 44;
			}
			if (code) fprintf(stderr, "bz rc=%d dec=%d dest=%u par=%d\n", rc, dec, dest, OxzBzipLastParallel());
		}
	}
	if (!code) {
		const char* phrase = "the quick brown fox jumps over the lazy dog. ";
		size_t plen = strlen(phrase);
		std::vector<uint8_t> plain;
		raw.resize(4u * 1024u * 1024u);
		for (i = 0; i < raw.size(); ++i) raw[i] = (uint8_t)phrase[i % plen];
		plain.resize(raw.size());
		if (!OxzBzipCompress(raw.data(), raw.size(), packed, 9, 4)) code = 5;
		else if (!OxzBzipDecompress(packed.data(), packed.size(), plain.data(), plain.size(), 4) || plain != raw || !OxzBzipLastParallel()) code = 5;
		else if (!OxzBzipDecompress(packed.data(), packed.size(), plain.data(), plain.size(), 0) || plain != raw) code = 5;
	}
	if (!code) {
		uint8_t prop = 0;
		std::vector<uint8_t> plain;
		raw.resize(9ull * 1024ull * 1024ull);
		for (i = 0; i < raw.size(); ++i) raw[i] = (uint8_t)((i * 3) ^ (i >> 4));
		gLevelBytes = raw.size();
		if (!TryLzma(raw.data(), raw.size(), packed, &prop)) code = 6;
		else {
			plain.resize(raw.size());
			if (!DecLzma(packed.data(), packed.size(), plain.data(), plain.size(), prop) || plain != raw) code = 6;
		}
		gLevelBytes = 0;
	}
	if (!code) {
		std::vector<uint8_t> plain;
		raw.resize(4ull * 1024ull * 1024ull);
		for (i = 0; i < raw.size(); ++i) raw[i] = (uint8_t)(i & 255);
		gLevelBytes = raw.size();
		if (!TryZstd(raw.data(), raw.size(), packed, 0)) code = 7;
		else {
			plain.resize(raw.size());
			size_t got = ZSTD_decompress(plain.data(), plain.size(), packed.data(), packed.size());
			if (ZSTD_isError(got) || got != plain.size() || plain != raw) code = 7;
		}
		gLevelBytes = 0;
	}
	if (!code) {
		std::vector<uint8_t> plain;
		raw.assign(9ull * 1024ull * 1024ull, 7);
		for (i = 0; i < raw.size(); i += 64) raw[i] = (uint8_t)(i / 64);
		gLevelBytes = raw.size();
		if (!TryBrotli(raw.data(), raw.size(), packed)) code = 8;
		else {
			plain.resize(raw.size());
			if (!DecBrotli(packed.data(), packed.size(), plain.data(), plain.size()) || plain != raw) code = 8;
		}
		gLevelBytes = 0;
	}
	if (!code) {
		size_t got = 0;
		std::vector<uint8_t> plain;
		raw.resize(32 * 1024);
		for (i = 0; i < raw.size(); ++i) raw[i] = (uint8_t)("oxz ppmd speed test "[i % 20]);
		packed.resize(raw.size());
		if (OxzPpmdCompress(raw.data(), raw.size(), packed.data(), packed.size(), &got, raw.size(), 1) != 0 || got == 0) code = 9;
		else {
			plain.resize(raw.size());
			if (!DecPpmd(packed.data(), got, plain.data(), plain.size()) || plain != raw) code = 9;
		}
	}
	gUserLevel = saved;
	gLevelBytes = 0;
	return code;
}
