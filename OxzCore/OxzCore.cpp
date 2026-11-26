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
#include <condition_variable>
#include <immintrin.h>

#define ZSTD_STATIC_LINKING_ONLY
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
#include "OxzMedia.h"
#include "OxzLossless.h"

struct FilePulse;
static thread_local FilePulse* tFilePulse = NULL;
static thread_local int tPulseEmit = 0;
static thread_local int tEmitLo = 0;
static void PulseEncode(FilePulse* fp, unsigned long long done, unsigned long long total);

struct EncHook {
	void (*fn)(void* user, unsigned long long done, unsigned long long total);
	void* user;
};

struct LzWatch {
	EncHook hook;
	unsigned long long base;
	unsigned long long total;
	std::atomic<unsigned long long>* slot;
	std::atomic<unsigned long long>* sum;
};

static void LzmaPulse(void* user, unsigned long long done, unsigned long long span)
{
	LzWatch* w = (LzWatch*)user;
	unsigned long long at;
	(void)span;
	if (!w || !w->hook.fn || !w->total) return;
	if (w->slot && w->sum) {
		unsigned long long prev = w->slot->exchange(done);
		if (done > prev) w->sum->fetch_add(done - prev);
		at = w->sum->load();
	} else {
		at = w->base + done;
	}
	if (at > w->total) at = w->total;
	w->hook.fn(w->hook.user, at, w->total);
}
static thread_local EncHook gEncHook = { 0, 0 };

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
	M_PPMD = 12,
	M_FLAC = 13,
	M_ALAC = 14,
	M_TTA = 15,
	M_PICT = 16,
	M_VID = 17,
	M_LZSS = 18
};

enum { F_RIP = 1, F_DELTA1 = 2, F_DELTA4 = 4 };

enum { ORIGIN_NEW = 0, ORIGIN_BASE = 1, ORIGIN_OVERLAY = 2, ORIGIN_TOMB = 3 };

static const uint8_t kMagic[8] = { 'O', 'X', 'Z', 'P', 'K', 'G', 0, 0 };
static const size_t kLzBlockMax = 0x7E000000u;
static const size_t kFullPick = 256u * 1024u;
static const size_t kSampleSlice = 16u * 1024u;
static const unsigned kSampleCount = 8;
static const size_t kPpmdMax = 8ull * 1024ull * 1024ull;
static thread_local size_t gLevelBytes = 0;
static int gUserLevel = 10;
static int gThreadCap = 0;
static int gUsePpmd = 1;
static thread_local int tZstdLdm = 0;
static thread_local int tInSurvey = 0;
static thread_local int tInFilePool = 0;
static thread_local int tRunInline = 0;
static thread_local int tScout = 0;
static thread_local int tZstdCeiling = 0;
static const size_t kFastFile = 512ull * 1024ull;

static int EffortCap(int level, int cap)
{
	if (tScout && level > cap) return cap;
	return level;
}

static int Scale(int lo, int hi)
{
	if (hi < lo) return lo;
	if (gUserLevel <= 1) return lo;
	if (gUserLevel >= 10) return hi;
	return lo + (hi - lo) * (gUserLevel - 1) / 9;
}

static int ZstdLevel(int hi)
{
	int level = EffortCap(Scale(1, hi), 3);
	if (tZstdCeiling > 0 && level > tZstdCeiling) level = tZstdCeiling;
	return level;
}

static size_t LevelN(size_t n)
{
	return gLevelBytes ? gLevelBytes : n;
}

static unsigned HwCap(void)
{
	unsigned n = std::thread::hardware_concurrency();
	if (n < 1) n = 4;
	if (gThreadCap >= 1 && n > (unsigned)gThreadCap) n = (unsigned)gThreadCap;
	if (n > 12) n = 12;
	if (n < 1) n = 1;
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

static int ZstdArm(ZSTD_CCtx* c, size_t n, int level, int wlog)
{
	unsigned zt;
	int overlap;
	size_t window, reload;
	if (!c || tInSurvey || tInFilePool || tZstdCeiling == 1 || n < 2ull * 1024ull * 1024ull) return 1;
	zt = InnerThreads(n);
	if (zt < 2) return 1;
	if (level >= 19 && zt > 4) zt = 4;
	else if (zt > 8) zt = 8;
	if (ZSTD_isError(ZSTD_CCtx_setParameter(c, ZSTD_c_nbWorkers, (int)zt))) return 1;
	if (wlog < 10) wlog = 10;
	if (wlog > 27) wlog = 27;
	window = (size_t)1 << wlog;
	overlap = 9;
	reload = window;
	while (overlap > 3) {
		unsigned shift = (unsigned)(9 - overlap);
		reload = shift >= 20 ? (512u << 10) : (window >> shift);
		if (reload < (512u << 10)) reload = 512u << 10;
		if (reload > n) reload = n;
		if (n / reload >= zt) break;
		overlap--;
	}
	if (reload < (512u << 10)) reload = 512u << 10;
	if (reload > n) reload = n;
	if (n / reload < 2) {
		ZSTD_CCtx_setParameter(c, ZSTD_c_nbWorkers, 0);
		return 1;
	}
	ZSTD_CCtx_setParameter(c, ZSTD_c_overlapLog, overlap);
	ZSTD_CCtx_setParameter(c, ZSTD_c_jobSize, (int)reload);
	return (int)zt;
}

static void PulseZstdDone(ZSTD_CCtx* c, unsigned long long n, int doneFrame)
{
	ZSTD_frameProgression pr;
	unsigned long long done;
	if (!n) return;
	if (doneFrame) {
		PulseEncode(tFilePulse, n, n);
		return;
	}
	pr = ZSTD_getFrameProgression(c);
	done = pr.consumed;
	if (done > n) done = n;
	PulseEncode(tFilePulse, done, n);
}

static size_t ZstdCompressWatch(ZSTD_CCtx* c, void* dst, size_t dstCap, const void* src, size_t n)
{
	std::atomic<int> run(1);
	int lo = tEmitLo;
	int emit = tPulseEmit;
	FilePulse* fp = tFilePulse;
	std::thread watch;
	size_t got;
	if (emit && fp) {
		watch = std::thread([c, n, lo, fp, &run]() {
			tPulseEmit = 1;
			tEmitLo = lo;
			while (run.load()) {
				ZSTD_frameProgression pr = ZSTD_getFrameProgression(c);
				unsigned long long done = pr.consumed;
				if (done > n) done = n;
				PulseEncode(fp, done, n);
				Sleep(40);
			}
		});
	}
	got = ZSTD_compress2(c, dst, dstCap, src, n);
	run.store(0);
	if (watch.joinable()) watch.join();
	if (emit && fp && !ZSTD_isError(got)) PulseEncode(fp, n, n);
	return got;
}

static void BoostThread(std::thread& th)
{
	SetThreadPriority((HANDLE)th.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
}

extern "C" int OxzLzma2Compress(const void* in, size_t inSize, void* out, size_t outCap, size_t* outSize, unsigned char* prop, size_t profile, int userLevel, void (*pulse)(void*, unsigned long long, unsigned long long), void* pulseUser);
extern "C" int OxzLzma2EncodeChunk(const void* in, size_t inSize, void* out, size_t outCap, size_t* outSize, unsigned char* prop, size_t profile, int userLevel, void (*pulse)(void*, unsigned long long, unsigned long long), void* pulseUser);
extern "C" int OxzPpmdCompress(const void* in, size_t inSize, void* out, size_t outCap, size_t* outSize, size_t profile, int userLevel);

struct TlsBigBlocks {
	enum { kSlots = 12 };
	void* ptr[kSlots];
	size_t cap[kSlots];
	int used[kSlots];
	ZSTD_CCtx* zc;
	ZSTD_DCtx* zd;
	struct libdeflate_compressor* defComp[13];
	struct libdeflate_decompressor* defDec;
	void* lzfseScratch;
	size_t lzfseCap;
	uint8_t* scratch;
	size_t scratchCap;
	uint8_t* fileBuf;
	size_t fileCap;
	TlsBigBlocks()
		: zc(NULL), zd(NULL), defDec(NULL), lzfseScratch(NULL), lzfseCap(0),
		  scratch(NULL), scratchCap(0), fileBuf(NULL), fileCap(0)
	{
		memset(ptr, 0, sizeof(ptr));
		memset(cap, 0, sizeof(cap));
		memset(used, 0, sizeof(used));
		memset(defComp, 0, sizeof(defComp));
	}
	~TlsBigBlocks() {
		int i;
		for (i = 0; i < kSlots; ++i) free(ptr[i]);
		if (zc) ZSTD_freeCCtx(zc);
		if (zd) ZSTD_freeDCtx(zd);
		for (i = 0; i < 13; ++i) if (defComp[i]) libdeflate_free_compressor(defComp[i]);
		if (defDec) libdeflate_free_decompressor(defDec);
		free(lzfseScratch);
		free(scratch);
		free(fileBuf);
	}
};

static TlsBigBlocks& TlsPool(void)
{
	static thread_local TlsBigBlocks tls;
	return tls;
}

extern "C" void* OxzLzmaAlloc(const ISzAlloc* p, size_t n)
{
	(void)p;
	if (!n) return NULL;
	if (n >= (32u << 10) && n <= (192u << 20)) {
		TlsBigBlocks& t = TlsPool();
		int k, bestFit = -1, victim = -1;
		for (k = 0; k < TlsBigBlocks::kSlots; ++k) {
			if (!t.used[k] && t.cap[k] >= n) {
				if (bestFit < 0 || t.cap[k] < t.cap[bestFit]) bestFit = k;
			}
		}
		if (bestFit >= 0) {
			t.used[bestFit] = 1;
			return t.ptr[bestFit];
		}
		for (k = 0; k < TlsBigBlocks::kSlots; ++k) {
			if (!t.used[k]) {
				if (t.cap[k] == 0) { victim = k; break; }
				if (victim < 0 || t.cap[k] < t.cap[victim]) victim = k;
			}
		}
		if (victim >= 0) {
			void* nb = realloc(t.ptr[victim], n);
			if (nb) {
				t.ptr[victim] = nb;
				t.cap[victim] = n;
				t.used[victim] = 1;
				return nb;
			}
		}
	}
	return malloc(n);
}

extern "C" void OxzLzmaFree(const ISzAlloc* p, void* a)
{
	(void)p;
	if (!a) return;
	{
		TlsBigBlocks& t = TlsPool();
		int k;
		for (k = 0; k < TlsBigBlocks::kSlots; ++k) {
			if (a == t.ptr[k]) { t.used[k] = 0; return; }
		}
	}
	free(a);
}

static void* BrotliTlsAlloc(void* opaque, size_t size)
{
	(void)opaque;
	return OxzLzmaAlloc(NULL, size);
}

static void BrotliTlsFree(void* opaque, void* address)
{
	(void)opaque;
	OxzLzmaFree(NULL, address);
}

static uint8_t* TlsScratch(size_t need)
{
	TlsBigBlocks& t = TlsPool();
	if (!need) need = 1;
	if (t.scratchCap < need) {
		size_t want = need < 65536 ? 65536 : need;
		void* nb = realloc(t.scratch, want);
		if (!nb) return NULL;
		t.scratch = (uint8_t*)nb;
		t.scratchCap = want;
	}
	return t.scratch;
}

static uint8_t* TlsFileBuf(size_t need)
{
	TlsBigBlocks& t = TlsPool();
	if (!need) need = 1;
	if (t.fileCap < need) {
		size_t want = need < 65536 ? 65536 : need;
		void* nb = realloc(t.fileBuf, want);
		if (!nb) return NULL;
		t.fileBuf = (uint8_t*)nb;
		t.fileCap = want;
	}
	return t.fileBuf;
}

static ZSTD_CCtx* TlsZstdCCtx(void)
{
	TlsBigBlocks& t = TlsPool();
	if (!t.zc) t.zc = ZSTD_createCCtx();
	if (t.zc) ZSTD_CCtx_reset(t.zc, ZSTD_reset_session_and_parameters);
	return t.zc;
}

static ZSTD_DCtx* TlsZstdDCtx(void)
{
	TlsBigBlocks& t = TlsPool();
	if (!t.zd) t.zd = ZSTD_createDCtx();
	if (t.zd) ZSTD_DCtx_reset(t.zd, ZSTD_reset_session_only);
	return t.zd;
}

static struct libdeflate_compressor* TlsDeflateComp(int level)
{
	TlsBigBlocks& t = TlsPool();
	if (level < 0) level = 0;
	if (level > 12) level = 12;
	if (!t.defComp[level]) t.defComp[level] = libdeflate_alloc_compressor(level);
	return t.defComp[level];
}

static struct libdeflate_decompressor* TlsDeflateDecomp(void)
{
	TlsBigBlocks& t = TlsPool();
	if (!t.defDec) t.defDec = libdeflate_alloc_decompressor();
	return t.defDec;
}

static void* TlsLzfseScratch(size_t need)
{
	TlsBigBlocks& t = TlsPool();
	if (!need) return NULL;
	if (t.lzfseCap < need) {
		void* nb = realloc(t.lzfseScratch, need);
		if (!nb) return NULL;
		t.lzfseScratch = nb;
		t.lzfseCap = need;
	}
	return t.lzfseScratch;
}

struct SaveWorkerPool {
	std::mutex mu;
	std::condition_variable cvStart;
	std::condition_variable cvDone;
	std::vector<std::thread> workers;
	void (*taskFn)(void*, size_t);
	void* taskCtx;
	size_t taskCount;
	std::atomic<size_t> cursor;
	unsigned activeWorkers;
	unsigned epoch;
	int stop;
	std::atomic<int> claimed;

	explicit SaveWorkerPool(unsigned n)
		: taskFn(NULL), taskCtx(NULL), taskCount(0), cursor(0), activeWorkers(0), epoch(0), stop(0), claimed(0)
	{
		unsigned t;
		if (n < 1) n = 1;
		workers.reserve(n);
		for (t = 0; t < n; ++t) {
			workers.push_back(std::thread([this]() {
				unsigned seenEpoch = 0;
				for (;;) {
					void (*fn)(void*, size_t) = NULL;
					void* ctx = NULL;
					size_t count = 0;
					{
						std::unique_lock<std::mutex> lk(mu);
						cvStart.wait(lk, [&]() { return stop || epoch != seenEpoch; });
						if (stop) return;
						seenEpoch = epoch;
						fn = taskFn;
						ctx = taskCtx;
						count = taskCount;
					}
					for (;;) {
						size_t i = cursor.fetch_add(1);
						if (i >= count) break;
						fn(ctx, i);
					}
					{
						std::unique_lock<std::mutex> lk(mu);
						if (--activeWorkers == 0) cvDone.notify_one();
					}
				}
			}));
			BoostThread(workers.back());
		}
	}

	~SaveWorkerPool()
	{
		size_t t;
		{
			std::lock_guard<std::mutex> lk(mu);
			stop = 1;
		}
		cvStart.notify_all();
		for (t = 0; t < workers.size(); ++t) {
			if (workers[t].joinable()) workers[t].join();
		}
	}

	int Run(size_t count, void (*fn)(void*, size_t), void* ctx)
	{
		int expected = 0;
		if (count == 0) return 1;
		if (count == 1 || workers.empty()) {
			size_t i;
			for (i = 0; i < count; ++i) fn(ctx, i);
			return 1;
		}
		if (!claimed.compare_exchange_strong(expected, 1)) return 0;
		{
			std::lock_guard<std::mutex> lk(mu);
			taskFn = fn;
			taskCtx = ctx;
			taskCount = count;
			cursor.store(0);
			activeWorkers = (unsigned)workers.size();
			epoch++;
		}
		cvStart.notify_all();
		for (;;) {
			size_t i = cursor.fetch_add(1);
			if (i >= count) break;
			fn(ctx, i);
		}
		{
			std::unique_lock<std::mutex> lk(mu);
			cvDone.wait(lk, [&]() { return activeWorkers == 0; });
		}
		claimed.store(0);
		return 1;
	}
};

static SaveWorkerPool* gSavePool = NULL;

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
static int gPcl = 0;
static std::atomic<int> gCrcDone(0);
static std::once_flag gCrcOnce;

static void CrcBuildTables(void)
{
	int i, b, k;
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
}

static void CrcEnsure(void);

static uint32_t CrcTable(const uint8_t* p, size_t n, uint32_t c)
{
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

static uint32_t CrcTableExt(uint32_t ext, const uint8_t* p, size_t n)
{
	return CrcTable(p, n, ext ^ 0xFFFFFFFFu) ^ 0xFFFFFFFFu;
}

static __m128i CrcFold(__m128i a, __m128i b, __m128i keys)
{
	__m128i t1 = _mm_clmulepi64_si128(a, keys, 0x00);
	__m128i t2 = _mm_clmulepi64_si128(a, keys, 0x11);
	return _mm_xor_si128(_mm_xor_si128(b, t1), t2);
}

static __m128i CrcLoad16(const uint8_t*& p)
{
	__m128i r = _mm_loadu_si128((const __m128i*)p);
	p += 16;
	return r;
}

static uint32_t CrcFoldTail(__m128i x, const uint8_t* input, const uint8_t* rest, const uint8_t* end)
{
	const long long k3 = 0x1751997d0LL;
	const long long k4 = 0x0ccaa009eLL;
	__m128i k3k4 = _mm_set_epi64x(k4, k3);
	(void)input;
	while ((size_t)(end - rest) >= 16) x = CrcFold(x, CrcLoad16(rest), k3k4);
	if (end != rest) {
		size_t n = (size_t)(end - rest);
		__m128i seq = _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
		__m128i shl = _mm_add_epi8(seq, _mm_set1_epi8((char)((int)n - 16)));
		__m128i shr = _mm_xor_si128(shl, _mm_set1_epi8((char)0x80));
		__m128i first = _mm_shuffle_epi8(x, shl);
		__m128i tail = _mm_loadu_si128((const __m128i*)(end - 16));
		__m128i last = _mm_blendv_epi8(_mm_shuffle_epi8(x, shr), tail, shr);
		x = CrcFold(first, last, k3k4);
	}
	x = _mm_xor_si128(_mm_clmulepi64_si128(x, k3k4, 0x10), _mm_srli_si128(x, 8));
	{
		__m128i barrett = _mm_set_epi64x(0x1db710641LL, (long long)0xb4e5b025f7011641ULL);
		__m128i t = _mm_clmulepi64_si128(x, barrett, 0x00);
		t = _mm_clmulepi64_si128(t, barrett, 0x10);
		return ~((uint32_t)_mm_extract_epi32(_mm_xor_si128(x, t), 2));
	}
}

static uint32_t CrcPcl(uint32_t crc, const uint8_t* data, size_t len)
{
	const uint8_t* end;
	const uint8_t* input;
	if (len < 16) return CrcTableExt(crc, data, len);
	input = data;
	end = data + len;
	if (len < 128) {
		__m128i x = CrcLoad16(data);
		x = _mm_xor_si128(x, _mm_cvtsi32_si128((int)(~crc)));
		return CrcFoldTail(x, input, data, end);
	}
	{
		const long long k1 = 0x154442bd4LL;
		const long long k2 = 0x1c6e41596LL;
		__m128i k1k2 = _mm_set_epi64x(k2, k1);
		__m128i k3k4 = _mm_set_epi64x(0x0ccaa009eLL, 0x1751997d0LL);
		__m128i x3 = CrcLoad16(data);
		__m128i x2 = CrcLoad16(data);
		__m128i x1 = CrcLoad16(data);
		__m128i x0 = CrcLoad16(data);
		__m128i x;
		x3 = _mm_xor_si128(x3, _mm_cvtsi32_si128((int)(~crc)));
		while ((size_t)(end - data) >= 64) {
			x3 = CrcFold(x3, CrcLoad16(data), k1k2);
			x2 = CrcFold(x2, CrcLoad16(data), k1k2);
			x1 = CrcFold(x1, CrcLoad16(data), k1k2);
			x0 = CrcFold(x0, CrcLoad16(data), k1k2);
		}
		x = CrcFold(x3, x2, k3k4);
		x = CrcFold(x, x1, k3k4);
		x = CrcFold(x, x0, k3k4);
		return CrcFoldTail(x, input, data, end);
	}
}

static void CrcEnsure(void)
{
	if (gCrcDone.load(std::memory_order_acquire)) return;
	std::call_once(gCrcOnce, []() {
		int info[4];
		int ok, off;
		size_t li;
		uint8_t b[4128];
		static const size_t lens[] = { 0, 1, 15, 16, 17, 31, 63, 64, 65, 127, 128, 129, 200, 511, 512, 1000, 4096, 4097 };
		CrcBuildTables();
		gPcl = 0;
		__cpuid(info, 1);
		if ((info[2] & (1 << 1)) != 0 && (info[2] & (1 << 9)) != 0 && (info[2] & (1 << 19)) != 0) {
			for (li = 0; li < sizeof(b); ++li) b[li] = (uint8_t)(li * 17u + 3u);
			ok = 1;
			for (li = 0; li < sizeof(lens) / sizeof(lens[0]); ++li) {
				for (off = 0; off <= 7; off += 3) {
					uint32_t table = CrcTable(b + off, lens[li], 0xFFFFFFFFu) ^ 0xFFFFFFFFu;
					uint32_t fast = CrcPcl(0, b + off, lens[li]);
					if (table != fast) ok = 0;
				}
			}
			{
				uint32_t whole = CrcTable(b, 1000, 0xFFFFFFFFu) ^ 0xFFFFFFFFu;
				uint32_t mid = CrcPcl(0, b, 300);
				uint32_t rest = CrcPcl(mid, b + 300, 700);
				if (rest != whole) ok = 0;
			}
			if (ok) gPcl = 1;
		}
		gCrcDone.store(1, std::memory_order_release);
	});
}

static uint32_t CrcBlock(const uint8_t* p, size_t n, uint32_t c)
{
	CrcEnsure();
	if (gPcl && n >= 16) return CrcPcl(c ^ 0xFFFFFFFFu, p, n) ^ 0xFFFFFFFFu;
	return CrcTable(p, n, c);
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
	while (n) {
		DWORD chunk = n > (4u << 20) ? (4u << 20) : (DWORD)n;
		DWORD rd = 0;
		OVERLAPPED ov;
		memset(&ov, 0, sizeof(ov));
		ov.Offset = (DWORD)off;
		ov.OffsetHigh = (DWORD)(off >> 32);
		if (!ReadFile(h, p, chunk, &rd, &ov) || rd != chunk) return 0;
		p += rd;
		off += rd;
		n -= rd;
	}
	return 1;
}

static int WriteAll(HANDLE h, const void* src, size_t n)
{
	const uint8_t* p = (const uint8_t*)src;
	while (n) {
		DWORD chunk = n > (4u << 20) ? (4u << 20) : (DWORD)n;
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
	return CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
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

static int TryZstdStream(const uint8_t* in, size_t n, std::vector<uint8_t>& out, int pass)
{
	size_t ln = LevelN(n);
	int hi = 19;
	int level;
	int wlog = 23;
	size_t bound, off = 0;
	std::vector<uint8_t> buf;
	ZSTD_CCtx* c;
	if (ln <= 256u * 1024u) hi = 22;
	else if (ln <= 64ull * 1024ull * 1024ull) hi = 19;
	else if (ln <= 256ull * 1024ull * 1024ull) hi = 16;
	else if (ln <= 1024ull * 1024ull * 1024ull) hi = 12;
	else hi = 9;
	level = ZstdLevel(hi);
	if (ln >= 32ull * 1024ull * 1024ull) wlog = 25;
	else if (ln >= 8ull * 1024ull * 1024ull) wlog = 24;
	else if (ln < 1u * 1024u * 1024u) wlog = 21;
	if (pass == 1 && wlog < 24) wlog = 24;
	if (tZstdCeiling == 1 && wlog > 19) wlog = 19;
	if (pass == 1 && ln > 256ull * 1024ull * 1024ull) return 0;
	bound = ZSTD_compressBound(n);
	if (ZSTD_isError(bound) || bound == 0) return 0;
	try { buf.resize(bound); } catch (...) { return 0; }
	c = ZSTD_createCCtx();
	if (!c) return 0;
	ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, level);
	ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, 0);
	ZSTD_CCtx_setParameter(c, ZSTD_c_windowLog, wlog);
	if (pass == 1) ZSTD_CCtx_setParameter(c, ZSTD_c_enableLongDistanceMatching, 1);
	{
		int workers = ZstdArm(c, n, level, wlog);
		size_t produced = 0;
		const size_t slice = 4u << 20;
		ZSTD_CCtx_setPledgedSrcSize(c, n);
		if (workers > 1) {
			size_t got = ZstdCompressWatch(c, buf.data(), buf.size(), in, n);
			ZSTD_freeCCtx(c);
			if (ZSTD_isError(got)) return 0;
			if (got == 0 || got >= n) return -1;
			buf.resize(got);
			out.swap(buf);
			tZstdLdm = pass == 1;
			return 1;
		}
		while (off <= n) {
			size_t chunk = 0;
			ZSTD_inBuffer input;
			int last, stepDone = 0, spins = 0;
			if (off < n) {
				chunk = n - off;
				if (chunk > slice) chunk = slice;
			}
			input.src = in + off;
			input.size = chunk;
			input.pos = 0;
			off += chunk;
			last = off >= n;
			while (!stepDone) {
				ZSTD_outBuffer output;
				size_t inBefore, rem;
				if (produced >= buf.size()) { ZSTD_freeCCtx(c); return 0; }
				output.dst = buf.data() + produced;
				output.size = buf.size() - produced;
				if (output.size > slice) output.size = slice;
				output.pos = 0;
				inBefore = input.pos;
				rem = ZSTD_compressStream2(c, &output, &input, last ? ZSTD_e_end : ZSTD_e_continue);
				if (ZSTD_isError(rem)) { ZSTD_freeCCtx(c); return 0; }
				produced += output.pos;
				if (output.pos) PulseZstdDone(c, n, 0);
				if (last) stepDone = rem == 0;
				else stepDone = input.pos == input.size;
				if (!stepDone && input.pos == inBefore && output.pos == 0) {
					if (workers < 2 || ++spins > 20000) { ZSTD_freeCCtx(c); return 0; }
					PulseZstdDone(c, n, 0);
					Sleep(1);
					continue;
				}
				spins = 0;
			}
			PulseZstdDone(c, n, last);
			if (last) break;
		}
		ZSTD_freeCCtx(c);
		if (produced == 0 || produced >= n) return 0;
		buf.resize(produced);
		out.swap(buf);
		tZstdLdm = pass == 1;
		return 1;
	}
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
	if (tPulseEmit && n >= 256u * 1024u && (mode == 0 || mode == 1)) {
		int sr = TryZstdStream(in, n, out, mode);
		if (sr > 0) return 1;
		if (sr < 0) return 0;
	}
	if (ln <= 256u * 1024u) hi = 22;
	else if (ln <= 64ull * 1024ull * 1024ull) hi = 19;
	else if (ln <= 256ull * 1024ull * 1024ull) hi = 16;
	else if (ln <= 1024ull * 1024ull * 1024ull) hi = 12;
	else hi = 9;
	level = ZstdLevel(hi);
	if (ln >= 32ull * 1024ull * 1024ull) wlog = 25;
	else if (ln >= 8ull * 1024ull * 1024ull) wlog = 24;
	else if (ln < 1u * 1024u * 1024u) wlog = 21;
	if (tZstdCeiling == 1 && wlog > 19) wlog = 19;
	for (pass = 0; pass < 2; ++pass) {
		size_t bound, got;
		int useLog;
		int ownCtx = 0;
		uint8_t* sb;
		ZSTD_CCtx* c;
		if (mode == 0 && pass == 1) break;
		if (mode == 1 && pass == 0) continue;
		if (pass == 1 && ln > 256ull * 1024ull * 1024ull) break;
		bound = ZSTD_compressBound(n);
		if (ZSTD_isError(bound) || bound == 0) break;
		sb = TlsScratch(bound);
		if (!sb) break;
		useLog = wlog;
		if (pass == 1 && useLog < 24) useLog = 24;
		if (!tInSurvey && !tInFilePool && tZstdCeiling != 1 && n >= 2ull * 1024ull * 1024ull && InnerThreads(n) >= 2) {
			c = ZSTD_createCCtx();
			ownCtx = 1;
		} else {
			c = TlsZstdCCtx();
		}
		if (!c) break;
		ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, level);
		ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, 0);
		ZSTD_CCtx_setParameter(c, ZSTD_c_windowLog, useLog);
		if (pass == 1) ZSTD_CCtx_setParameter(c, ZSTD_c_enableLongDistanceMatching, 1);
		{
			int zt = ownCtx ? ZstdArm(c, n, level, useLog) : 1;
			got = ZSTD_compress2(c, sb, bound, in, n);
			if (ZSTD_isError(got) && zt > 1) {
				ZSTD_CCtx_setParameter(c, ZSTD_c_nbWorkers, 0);
				got = ZSTD_compress2(c, sb, bound, in, n);
			}
		}
		if (ownCtx) ZSTD_freeCCtx(c);
		if (ZSTD_isError(got) || got == 0 || got >= best) continue;
		try { out.assign(sb, sb + got); } catch (...) { break; }
		best = got;
		bestLdm = pass;
	}
	tZstdLdm = bestLdm;
	return best < n;
}

static int TryLz4(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	int cap, got;
	uint8_t* sb;
	if (n == 0 || n > kLzBlockMax) return 0;
	cap = LZ4_compressBound((int)n);
	if (cap <= 0) return 0;
	sb = TlsScratch((size_t)cap);
	if (!sb) return 0;
	if (tScout || gUserLevel <= 2)
		got = LZ4_compress_default((const char*)in, (char*)sb, (int)n, cap);
	else
		got = LZ4_compress_HC((const char*)in, (char*)sb, (int)n, cap, Scale(4, LZ4HC_CLEVEL_MAX));
	if (got <= 0 || (size_t)got >= n) return 0;
	try { out.assign(sb, sb + (size_t)got); } catch (...) { return 0; }
	return 1;
}

static int DeflateOnce(const uint8_t* in, size_t n, std::vector<uint8_t>& out, int raw)
{
	size_t ln = LevelN(n);
	int hi = 9;
	int level;
	struct libdeflate_compressor* c;
	size_t bound, cap, got;
	uint8_t* sb;
	if (n == 0) return 0;
	if (ln <= 64ull * 1024ull * 1024ull) hi = 12;
	else if (ln <= 512ull * 1024ull * 1024ull) hi = 9;
	else hi = 6;
	level = EffortCap(Scale(1, hi), 1);
	c = TlsDeflateComp(level);
	if (!c) return 0;
	bound = raw ? libdeflate_deflate_compress_bound(c, n) : libdeflate_zlib_compress_bound(c, n);
	cap = bound ? bound : n + 64;
	sb = TlsScratch(cap);
	if (!sb) return 0;
	got = raw ? libdeflate_deflate_compress(c, in, n, sb, cap)
		: libdeflate_zlib_compress(c, in, n, sb, cap);
	if (got == 0 || got >= n) return 0;
	try { out.assign(sb, sb + got); } catch (...) { return 0; }
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
		std::atomic<unsigned long long> doneBytes(0);
		EncHook hook = gEncHook;
		workers = threads;
		if (workers > parts) workers = (unsigned)parts;
		for (t = 0; t < workers; ++t) {
			pool.push_back(std::thread([&, n, chunk, q, win, parts, hook]() {
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
					if (hook.fn && n) hook.fn(hook.user, doneBytes.fetch_add(len) + len, n);
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

static int MostlyText(const uint8_t* p, size_t n)
{
	size_t step, i, good = 0, samples = 0;
	if (n < 64) return 0;
	step = n / 2048;
	if (step < 1) step = 1;
	for (i = 0; i < n; i += step) {
		unsigned char c = p[i];
		samples++;
		if (c == 9 || c == 10 || c == 13 || (c >= 32 && c < 127)) good++;
	}
	return samples && good * 5 >= samples * 4;
}

static int BrotliCompressFast(int q, int win, BrotliEncoderMode bm, size_t n, const uint8_t* in, size_t* encodedSize, uint8_t* outBuf)
{
	BrotliEncoderState* s;
	size_t availIn = n;
	const uint8_t* nextIn = in;
	size_t availOut = *encodedSize;
	uint8_t* nextOut = outBuf;
	size_t totalOut = 0;
	BROTLI_BOOL ok;
	if (availOut == 0 || n == 0) return 0;
	s = BrotliEncoderCreateInstance(BrotliTlsAlloc, BrotliTlsFree, NULL);
	if (!s) return 0;
	BrotliEncoderSetParameter(s, BROTLI_PARAM_QUALITY, (uint32_t)q);
	BrotliEncoderSetParameter(s, BROTLI_PARAM_LGWIN, (uint32_t)win);
	BrotliEncoderSetParameter(s, BROTLI_PARAM_MODE, (uint32_t)bm);
	BrotliEncoderSetParameter(s, BROTLI_PARAM_SIZE_HINT, n > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)n);
	if (win > BROTLI_MAX_WINDOW_BITS)
		BrotliEncoderSetParameter(s, BROTLI_PARAM_LARGE_WINDOW, BROTLI_TRUE);
	ok = BrotliEncoderCompressStream(s, BROTLI_OPERATION_FINISH, &availIn, &nextIn, &availOut, &nextOut, &totalOut);
	if (!BrotliEncoderIsFinished(s)) ok = BROTLI_FALSE;
	BrotliEncoderDestroyInstance(s);
	if (!ok) return 0;
	*encodedSize = totalOut;
	return 1;
}

static int TryBrotli(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	int q, win, mode, modes;
	size_t cap, best, ln;
	unsigned threads;
	uint8_t* sb;
	if (n == 0) return 0;
	cap = BrotliEncoderMaxCompressedSize(n);
	if (cap == 0) return 0;
	ln = LevelN(n);
	if (ln <= 8u * 1024u * 1024u) q = BROTLI_MAX_QUALITY;
	else if (ln <= 64ull * 1024ull * 1024ull) q = 9;
	else if (ln <= 256ull * 1024ull * 1024ull) q = 6;
	else q = 4;
	q = EffortCap(Scale(1, q), 4);
	win = n < (1u << 16) ? 18 : BROTLI_MAX_WINDOW_BITS;
	best = n;
	modes = (!tScout && n <= 8u * 1024u * 1024u && ln <= 8u * 1024u * 1024u && MostlyText(in, n)) ? 2 : 1;
	threads = InnerThreads(n);
	if (!tScout && threads > 1 && n >= 2ull * 1024ull * 1024ull && BrotliParallel(in, n, q, win, out, threads)) return 1;
	sb = TlsScratch(cap);
	if (!sb) return 0;
	for (mode = 0; mode < modes; ++mode) {
		size_t got = cap;
		BrotliEncoderMode bm = mode ? BROTLI_MODE_TEXT : BROTLI_MODE_GENERIC;
		if (!BrotliCompressFast(q, win, bm, n, in, &got, sb)) continue;
		if (got == 0 || got >= best) continue;
		try { out.assign(sb, sb + got); } catch (...) { break; }
		best = got;
	}
	return best < n;
}

static int TryLzma(const uint8_t* in, size_t n, std::vector<uint8_t>& out, uint8_t* prop)
{
	size_t cap, got = 0;
	unsigned char pr = 0;
	unsigned th;
	int ul;
	if (n == 0) return 0;
	ul = tScout ? (gUserLevel > 5 ? 5 : gUserLevel) : gUserLevel;
	th = InnerThreads(n);
	if (th > 4) th = 4;
	if (ul >= 3 && th >= 2 && n >= 4ull * 1024ull * 1024ull) {
		size_t unitMin = 384ull * 1024ull;
		if (n / th < unitMin) {
			th = (unsigned)(n / unitMin);
			if (th < 2) th = 1;
		}
		if (th >= 2) {
			size_t chunkBasis = (n / th) + (n % th);
			struct Part { std::vector<uint8_t> data; unsigned char prop; int ok; };
			std::vector<Part> parts(th);
			std::vector<std::thread> pool;
			std::atomic<unsigned> cursor(0);
			std::vector<std::atomic<unsigned long long>> slots(th);
			std::atomic<unsigned long long> sum(0);
			EncHook hook = gEncHook;
			unsigned t;
			int propsOk = 1;
			size_t total = 0;
			for (t = 0; t < th; ++t) {
				pool.push_back(std::thread([&, n, th, hook]() {
					for (;;) {
						unsigned c = cursor.fetch_add(1);
						size_t off, len, capc, gotc = 0;
						unsigned char prc = 0;
						LzWatch watch;
						if (c >= th) break;
						off = (n / th) * c;
						len = (c + 1 == th) ? (n - off) : (n / th);
						capc = len + (len / 4) + 65536;
						try { parts[c].data.resize(capc); }
						catch (...) { parts[c].ok = 0; continue; }
						watch.hook = hook;
						watch.base = off;
						watch.total = n;
						watch.slot = &slots[c];
						watch.sum = &sum;
						if (OxzLzma2EncodeChunk(in + off, len, parts[c].data.data(), capc, &gotc, &prc, chunkBasis, ul, hook.fn ? LzmaPulse : NULL, hook.fn ? &watch : NULL) != 0 || gotc == 0 || parts[c].data[gotc - 1] != 0) {
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
	{
		uint8_t* sb = TlsScratch(cap);
		LzWatch watch;
		if (!sb) return 0;
		watch.hook = gEncHook;
		watch.base = 0;
		watch.total = n;
		watch.slot = NULL;
		watch.sum = NULL;
		if (OxzLzma2Compress(in, n, sb, cap, &got, &pr, LevelN(n), ul, watch.hook.fn ? LzmaPulse : NULL, watch.hook.fn ? &watch : NULL) != 0) return 0;
		if (got == 0 || got >= n) return 0;
		try { out.assign(sb, sb + got); } catch (...) { return 0; }
	}
	*prop = pr;
	return 1;
}

static int TryLizard(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	int level, cap, got;
	size_t ln;
	uint8_t* sb;
	if (n == 0 || n > kLzBlockMax) return 0;
	ln = LevelN(n);
	if (ln <= 4u * 1024u * 1024u) level = LIZARD_MAX_CLEVEL;
	else if (ln <= 64ull * 1024ull * 1024ull) level = 22;
	else level = LIZARD_DEFAULT_CLEVEL;
	level = EffortCap(Scale(LIZARD_MIN_CLEVEL, level), 10);
	cap = Lizard_compressBound((int)n);
	if (cap <= 0) return 0;
	sb = TlsScratch((size_t)cap);
	if (!sb) return 0;
	got = Lizard_compress((const char*)in, (char*)sb, (int)n, cap, level);
	if (got <= 0 || (size_t)got >= n) return 0;
	try { out.assign(sb, sb + (size_t)got); } catch (...) { return 0; }
	return 1;
}

static int TryLzfse(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	size_t scratchNeed, cap, got;
	void* scratch;
	uint8_t* sb;
	if (n == 0) return 0;
	scratchNeed = lzfse_encode_scratch_size();
	scratch = scratchNeed ? TlsLzfseScratch(scratchNeed) : NULL;
	if (scratchNeed && !scratch) return 0;
	cap = n + 4096;
	sb = TlsScratch(cap);
	if (!sb) return 0;
	got = lzfse_encode_buffer(sb, cap, in, n, scratch);
	if (got == 0 || got >= n) return 0;
	try { out.assign(sb, sb + got); } catch (...) { return 0; }
	return 1;
}

static int TryBzip2(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	if (n == 0 || n > 0xFFFFFFFFull) return 0;
	return OxzBzipCompress(in, n, out, EffortCap(Scale(1, 9), 1), InnerThreads(n), gEncHook.fn, gEncHook.user);
}

static int TryPpmd(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	size_t got = 0;
	uint8_t* sb;
	if (n == 0) return 0;
	sb = TlsScratch(n);
	if (!sb) return 0;
	if (OxzPpmdCompress(in, n, sb, n, &got, LevelN(n), tScout ? 1 : gUserLevel) != 0) return 0;
	if (got == 0 || got >= n) return 0;
	try { out.assign(sb, sb + got); } catch (...) { return 0; }
	return 1;
}

struct Choice {
	int method;
	int flags;
	int zstdLdm;
	uint8_t prop;
	std::vector<uint8_t> data;
};

struct FastAlt {
	int method;
	int flags;
	int zstdLdm;
	size_t packed;
};

static int CodecIsFast(int method)
{
	return method == M_ZSTD || method == M_LZ4 || method == M_LIZARD || method == M_LZFSE;
}

static int CodecIsSlow(int method)
{
	return method == M_LZMA2 || method == M_BROTLI || method == M_BZIP2 || method == M_PPMD
		|| method == M_DEFLATE || method == M_RAWDEFLATE;
}

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
	if (n <= 4) return;
	while (i + 16 <= n) {
		int prev;
		__m128i v = _mm_loadu_si128((const __m128i*)(p + i));
		memcpy(&prev, p + i - 4, 4);
		v = _mm_add_epi8(v, _mm_slli_si128(v, 4));
		v = _mm_add_epi8(v, _mm_slli_si128(v, 8));
		v = _mm_add_epi8(v, _mm_set1_epi32(prev));
		_mm_storeu_si128((__m128i*)(p + i), v);
		i += 16;
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
	case M_FLAC: return OxzFlacCompress(in, n, tmp);
	case M_ALAC: return OxzAlacCompress(in, n, tmp);
	case M_TTA: return OxzTtaCompress(in, n, tmp);
	case M_PICT: return OxzPictCompress(in, n, tmp);
	case M_VID: return OxzVidCompress(in, n, tmp);
	case M_LZSS: return OxzLzssCompress(in, n, tmp);
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
	int tenth;
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
static const unsigned kPulseMagic = 0x50585A4Fu;

struct PulseWeight {
	unsigned magic;
	int base;
	int span;
	int usedBase;
	unsigned long long numer;
	unsigned long long denom;
	int fileTen;
};

static void Publish(FilePulse* fp, int tenth, int finish)
{
	SavePulse* p;
	unsigned display;
	uint64_t numer;
	size_t i;
	DWORD now;
	int pct;
	if (!fp || !fp->name) return;
	p = gPulse;
	if (!p || !p->fn) return;
	if (tenth < 0) tenth = 0;
	if (tenth > 1000) tenth = 1000;
	std::lock_guard<std::mutex> lock(p->mu);
	if (finish == 2) {
		if (fp->seen) {
			for (i = 0; i < p->active.size(); ++i) {
				if (p->active[i] == fp) {
					p->active.erase(p->active.begin() + (ptrdiff_t)i);
					break;
				}
			}
		}
		fp->tenth = 0;
		fp->seen = 0;
		return;
	}
	if (!fp->seen) {
		fp->seen = 1;
		p->active.push_back(fp);
	}
	now = GetTickCount();
	if (finish == 3) {
		if (tenth > 990) tenth = 990;
		fp->tenth = tenth;
	} else if (!finish && tenth > 0 && tenth < 1000 && p->lastTick && (DWORD)(now - p->lastTick) < 80) {
		if (tenth > fp->tenth) fp->tenth = tenth;
		return;
	}
	if (!finish) p->lastTick = now ? now : 1;
	if (finish == 1) {
		if (fp->tenth < 1000) {
			p->bytesDone += fp->size;
			p->filesDone++;
		}
		fp->tenth = 1000;
		for (i = 0; i < p->active.size(); ++i) {
			if (p->active[i] == fp) {
				p->active.erase(p->active.begin() + (ptrdiff_t)i);
				break;
			}
		}
	} else if (finish != 3) {
		if (tenth < fp->tenth) tenth = fp->tenth;
		if (tenth > 990) tenth = 990;
		fp->tenth = tenth;
	}
	numer = p->bytesDone;
	for (i = 0; i < p->active.size(); ++i) {
		FilePulse* o = p->active[i];
		if (o->tenth > 0 && o->tenth < 1000) numer += o->size * (uint64_t)o->tenth / 1000ull;
	}
	if (p->bytesAll && numer > p->bytesAll) numer = p->bytesAll;
	if (p->user) {
		PulseWeight* w = (PulseWeight*)p->user;
		if (w->magic == kPulseMagic) {
			w->numer = numer;
			w->denom = p->bytesAll ? p->bytesAll : 1;
			w->fileTen = fp->tenth;
		}
	}
	display = p->filesDone;
	if (finish != 1 && p->total && display < p->total) display++;
	if (p->total && display > p->total) display = p->total;
	if (!display) display = 1;
	pct = finish == 1 ? 100 : fp->tenth / 10;
	p->fn(fp->name, display, p->total ? p->total : 1, pct, p->user);
}

static int EmitTenth(unsigned long long done, unsigned long long total)
{
	int lo = tEmitLo;
	if (lo < 0) lo = 0;
	if (lo > 990) lo = 990;
	if (!total || done == 0) return lo;
	if (done > total) done = total;
	return lo + (int)(done * (unsigned long long)(990 - lo) / total);
}

static void PulseEncode(FilePulse* fp, unsigned long long done, unsigned long long total)
{
	if (!fp || !tPulseEmit) return;
	Publish(fp, EmitTenth(done, total), 0);
}

struct EncPulseUser {
	FilePulse* fp;
	int lo;
};

static void OnEncodePulse(void* user, unsigned long long done, unsigned long long total)
{
	EncPulseUser* w = (EncPulseUser*)user;
	int lo, ten;
	if (!w || !w->fp || !total) return;
	lo = w->lo;
	if (lo < 0) lo = 0;
	if (lo > 990) lo = 990;
	if (done > total) done = total;
	ten = lo + (int)(done * (unsigned long long)(990 - lo) / total);
	Publish(w->fp, ten, 0);
}

struct PulseGuard {
	FilePulse fp;
	int commit;
	explicit PulseGuard(const std::wstring& rel, uint64_t size) : commit(1)
	{
		memset(&fp, 0, sizeof(fp));
		fp.name = rel.c_str();
		fp.size = size ? size : 1;
		tFilePulse = &fp;
		Publish(&fp, 0, 0);
	}
	void cancel() { commit = 0; }
	~PulseGuard()
	{
		if (commit) Publish(&fp, 1000, 1);
		else Publish(&fp, 0, 2);
		if (tFilePulse == &fp) tFilePulse = NULL;
	}
};

struct Survey {
	int method;
	int flags;
	int flagIdx;
	int zstdPass;
	int ok;
	int zstdLdm;
	uint8_t prop;
	std::vector<uint8_t> data;
};

static size_t Lz4ScoreBuf(const uint8_t* buf, size_t n, std::vector<uint8_t>& lz4Out)
{
	int cap, got;
	if (n == 0 || n > 16u * 1024u * 1024u) return n;
	cap = LZ4_compressBound((int)n);
	if (cap <= 0) return n;
	try {
		if (lz4Out.size() < (size_t)cap) lz4Out.resize((size_t)cap);
	} catch (...) { return n; }
	got = LZ4_compress_fast((const char*)buf, (char*)lz4Out.data(), (int)n, cap, 2);
	if (got <= 0) return n;
	return (size_t)got;
}

static void ExecOneSurvey(Survey* job, const uint8_t* const* srcPtr, size_t n, FilePulse* pulseFile, unsigned pulseCap, size_t pulseJobs, std::atomic<unsigned>* pulseStep)
{
	std::vector<uint8_t> tmp;
	uint8_t prop = 0;
	const uint8_t* src;
	if (job->ok != 0) return;
	src = srcPtr[job->flagIdx];
	if (src) {
		try {
			if (OneCodec(src, n, job->method, job->zstdPass, tmp, &prop)) {
				job->ok = 1;
				job->prop = prop;
				job->zstdLdm = (job->method == M_ZSTD) ? job->zstdPass : 0;
				job->data.swap(tmp);
			}
		} catch (...) {
		}
	}
	if (job->ok == 0) job->ok = -1;
	if (pulseFile && pulseStep) {
		unsigned step = pulseStep->fetch_add(1) + 1;
		int ten = (int)((unsigned long long)step * (unsigned)pulseCap * 10ull / pulseJobs);
		if (ten > (int)(pulseCap * 10)) ten = (int)(pulseCap * 10);
		Publish(pulseFile, ten, 0);
	}
}

struct SurveyBatch {
	std::vector<Survey>* jobs;
	const uint8_t* const* srcPtr;
	size_t n;
	size_t levelSnap;
	FilePulse* pulseFile;
	unsigned pulseCap;
	size_t pulseJobs;
	int scout;
	std::atomic<unsigned> pulseStep;
	SurveyBatch() : jobs(NULL), srcPtr(NULL), n(0), levelSnap(0), pulseFile(NULL), pulseCap(95), pulseJobs(1), scout(0), pulseStep(0) {}
};

static void SurveyPoolStep(void* ctx, size_t idx)
{
	SurveyBatch* b = (SurveyBatch*)ctx;
	size_t prevLev = gLevelBytes;
	int prevSurv = tInSurvey;
	int prevScout = tScout;
	gLevelBytes = b->levelSnap;
	tInSurvey = 1;
	tScout = b->scout;
	ExecOneSurvey(&(*b->jobs)[idx], b->srcPtr, b->n, b->pulseFile, b->pulseCap, b->pulseJobs, &b->pulseStep);
	tScout = prevScout;
	tInSurvey = prevSurv;
	gLevelBytes = prevLev;
}

static void RunJobs(std::vector<Survey>& jobs, const uint8_t* const* srcPtr, size_t n, int scout)
{
	size_t levelSnap;
	FilePulse* pulseFile;
	unsigned pulseCap;
	size_t pulseJobs;
	unsigned pending = 0;
	size_t i;
	if (jobs.empty()) return;
	for (i = 0; i < jobs.size(); ++i) if (jobs[i].ok == 0) pending++;
	if (!pending) return;
	levelSnap = gLevelBytes;
	pulseFile = tFilePulse;
	pulseCap = (levelSnap > n && levelSnap - n > 4096) ? 25u : 95u;
	pulseJobs = jobs.size() ? jobs.size() : 1;
	if (tRunInline || pending == 1 || HwCap() <= 1) {
		std::atomic<unsigned> pulseStep(0);
		int prevSurv = tInSurvey;
		int prevScout = tScout;
		tInSurvey = 1;
		tScout = scout;
		for (i = 0; i < jobs.size(); ++i) {
			if (jobs[i].ok != 0) continue;
			ExecOneSurvey(&jobs[i], srcPtr, n, pulseFile, pulseCap, pulseJobs, &pulseStep);
		}
		tScout = prevScout;
		tInSurvey = prevSurv;
		return;
	}
	if (gSavePool) {
		SurveyBatch batch;
		batch.jobs = &jobs;
		batch.srcPtr = srcPtr;
		batch.n = n;
		batch.levelSnap = levelSnap;
		batch.pulseFile = pulseFile;
		batch.pulseCap = pulseCap;
		batch.pulseJobs = pulseJobs;
		batch.scout = scout;
		if (gSavePool->Run(jobs.size(), SurveyPoolStep, &batch))
			return;
	}
	{
		unsigned threads = HwCap();
		unsigned t;
		std::vector<std::thread> pool;
		std::atomic<size_t> cursor(0);
		std::atomic<unsigned> pulseStep(0);
		if (tInFilePool) {
			unsigned capThreads = HwCap() / 3;
			if (capThreads < 1) capThreads = 1;
			if (capThreads > 4) capThreads = 4;
			threads = capThreads;
		}
		if (threads > pending) threads = pending;
		for (t = 0; t < threads; ++t) {
			pool.push_back(std::thread([&, n, levelSnap, pulseFile, pulseCap, pulseJobs, scout]() {
				gLevelBytes = levelSnap;
				tInSurvey = 1;
				tScout = scout;
				for (;;) {
					size_t idx = cursor.fetch_add(1);
					if (idx >= jobs.size()) break;
					if (jobs[idx].ok != 0) continue;
					ExecOneSurvey(&jobs[idx], srcPtr, n, pulseFile, pulseCap, pulseJobs, &pulseStep);
				}
				tScout = 0;
				tInSurvey = 0;
			}));
			BoostThread(pool.back());
		}
		for (t = 0; t < pool.size(); ++t) pool[t].join();
	}
}

static Choice Search(const uint8_t* raw, size_t n, int isPe, FastAlt* alt)
{
	static const int kMethod[] = {
		M_ZSTD, M_ZSTD, M_LZ4, M_LIZARD, M_LZFSE, M_RAWDEFLATE, M_BZIP2, M_BROTLI, M_PPMD, M_LZMA2
	};
	static const int kPass[] = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
	int flags[6];
	int keepFlag[6];
	size_t score[6];
	std::vector<uint8_t> filtBuf[6];
	const uint8_t* srcPtr[6];
	std::vector<uint8_t> lz4Out;
	size_t baseScore;
	size_t ln;
	int nflags = 0;
	int f, m;
	int probing;
	std::vector<Survey> jobs;
	Choice best;
	if (alt) {
		alt->method = M_STORE;
		alt->flags = 0;
		alt->zstdLdm = 0;
		alt->packed = n ? n : 1;
	}
	best.method = M_STORE;
	best.flags = 0;
	best.prop = 0;
	best.zstdLdm = 0;
	if (n == 0) return best;
	CrcEnsure();
	WarmCodecs();
	ln = LevelN(n);
	for (f = 0; f < 6; ++f) srcPtr[f] = NULL;
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
	baseScore = 0;
	for (f = 0; f < nflags; ++f) {
		keepFlag[f] = 0;
		if (flags[f] == 0) {
			srcPtr[f] = raw;
		} else {
			try {
				if ((flags[f] & F_RIP) && flags[f] != F_RIP && nflags > 1 && flags[1] == F_RIP && srcPtr[1]) {
					filtBuf[f] = filtBuf[1];
				} else {
					filtBuf[f].assign(raw, raw + n);
					if (flags[f] & F_RIP) OxzRipConvert(filtBuf[f].data(), n, 1);
				}
				if (flags[f] & F_DELTA4) Delta4Enc(filtBuf[f].data(), n);
				else if (flags[f] & F_DELTA1) Delta1Enc(filtBuf[f].data(), n);
				srcPtr[f] = filtBuf[f].data();
			} catch (...) {
				srcPtr[f] = NULL;
				score[f] = n;
				continue;
			}
		}
		score[f] = Lz4ScoreBuf(srcPtr[f], n, lz4Out);
		if (flags[f] == 0) baseScore = score[f];
	}
	{ std::vector<uint8_t>().swap(lz4Out); }
	for (f = 0; f < nflags; ++f) if (flags[f] == F_RIP && score[f] < baseScore) baseScore = score[f];
	if (baseScore == 0) baseScore = n;
	for (f = 0; f < nflags; ++f) {
		if (!srcPtr[f]) continue;
		if (flags[f] == 0 || flags[f] == F_RIP) keepFlag[f] = 1;
		else if (score[f] + baseScore / 32 < baseScore) keepFlag[f] = 1;
		else {
			std::vector<uint8_t>().swap(filtBuf[f]);
			srcPtr[f] = NULL;
		}
	}
	jobs.reserve((size_t)nflags * 10 + 2);
	for (f = 0; f < nflags; ++f) {
		if (!keepFlag[f]) continue;
		for (m = 0; m < (int)(sizeof(kMethod) / sizeof(kMethod[0])); ++m) {
			Survey s;
			if (kMethod[m] == M_PPMD && (!gUsePpmd || ln > kPpmdMax)) continue;
			if (kMethod[m] == M_ZSTD && kPass[m] == 1 && ln <= 512u * 1024u) continue;
			s.method = kMethod[m];
			s.flags = flags[f];
			s.flagIdx = f;
			s.zstdPass = kPass[m];
			s.ok = 0;
			s.zstdLdm = 0;
			s.prop = 0;
			jobs.push_back(std::move(s));
		}
	}
	probing = gLevelBytes > n + 4096;
	if (!isPe && n >= 512 && ln <= 64ull * 1024ull * 1024ull) {
		Survey s;
		s.method = M_FLAC;
		s.flags = 0;
		s.flagIdx = 0;
		s.zstdPass = 0;
		s.ok = 0;
		s.zstdLdm = 0;
		s.prop = 0;
		jobs.push_back(std::move(s));
	}
	if (!isPe && !probing && n <= 64u * 1024u) {
		Survey s;
		s.method = M_LZSS;
		s.flags = 0;
		s.flagIdx = 0;
		s.zstdPass = 0;
		s.ok = 0;
		s.zstdLdm = 0;
		s.prop = 0;
		jobs.push_back(std::move(s));
	}
	RunJobs(jobs, srcPtr, n, gUserLevel >= 4 ? 1 : 0);
	if (gUserLevel >= 4) {
		size_t bestSz = (size_t)-1;
		int bestI = -1;
		int promo[6];
		int np = 0, p;
		size_t i;
		static const int kSlow[] = { M_LZMA2, M_BROTLI, M_BZIP2 };
		for (i = 0; i < jobs.size(); ++i) {
			if (jobs[i].ok != 1 || jobs[i].method == M_PPMD) continue;
			if (jobs[i].data.size() < bestSz) {
				bestSz = jobs[i].data.size();
				bestI = (int)i;
			}
		}
		if (bestI >= 0) {
			int s;
			size_t nearSz = (size_t)-1;
			int nearI = -1;
			promo[np++] = bestI;
			for (s = 0; s < 3 && np < 4; ++s) {
				size_t sz = (size_t)-1;
				int idx = -1;
				int already = 0;
				for (p = 0; p < np; ++p) if (jobs[promo[p]].method == kSlow[s]) already = 1;
				if (already) continue;
				for (i = 0; i < jobs.size(); ++i) {
					if (jobs[i].ok != 1 || jobs[i].method != kSlow[s]) continue;
					if (jobs[i].data.size() < sz) { sz = jobs[i].data.size(); idx = (int)i; }
				}
				if (idx < 0 || sz > bestSz * 2) continue;
				promo[np++] = idx;
			}
			for (i = 0; i < jobs.size(); ++i) {
				int already = 0;
				if (jobs[i].ok != 1 || jobs[i].method == M_PPMD || jobs[i].method == M_FLAC) continue;
				if (jobs[i].data.size() * 100 > bestSz * 115) continue;
				for (p = 0; p < np; ++p) if (promo[p] == (int)i) already = 1;
				if (already) continue;
				if (jobs[i].data.size() < nearSz) { nearSz = jobs[i].data.size(); nearI = (int)i; }
			}
			if (nearI >= 0 && np < 5) promo[np++] = nearI;
			for (i = 0; i < jobs.size() && np < 6; ++i) {
				int already = 0;
				if (jobs[i].ok != 1 || jobs[i].method != M_FLAC) continue;
				if (jobs[i].data.size() * 100 > bestSz * 120) continue;
				for (p = 0; p < np; ++p) if (promo[p] == (int)i) already = 1;
				if (!already) promo[np++] = (int)i;
			}
			for (p = 0; p < np; ++p) {
				jobs[promo[p]].ok = 0;
				jobs[promo[p]].data.clear();
			}
			RunJobs(jobs, srcPtr, n, 0);
		}
	}
	{
		Choice fast;
		Choice ppmd;
		size_t fastSz = n;
		size_t ppmdSz = n;
		int havePpmd = 0;
		fast.method = M_STORE;
		fast.flags = 0;
		fast.prop = 0;
		fast.zstdLdm = 0;
		ppmd.method = M_STORE;
		ppmd.flags = 0;
		ppmd.prop = 0;
		ppmd.zstdLdm = 0;
		for (size_t i = 0; i < jobs.size(); ++i) {
			if (jobs[i].ok != 1) continue;
			if (alt && CodecIsFast(jobs[i].method) && jobs[i].data.size() < alt->packed) {
				alt->method = jobs[i].method;
				alt->flags = jobs[i].flags;
				alt->zstdLdm = jobs[i].zstdLdm;
				alt->packed = jobs[i].data.size();
			}
			if (jobs[i].method == M_PPMD) {
				if (jobs[i].data.size() >= ppmdSz) continue;
				havePpmd = 1;
				ppmdSz = jobs[i].data.size();
				ppmd.method = M_PPMD;
				ppmd.flags = jobs[i].flags;
				ppmd.prop = jobs[i].prop;
				ppmd.zstdLdm = 0;
				ppmd.data.swap(jobs[i].data);
			} else {
				if (jobs[i].data.size() >= fastSz) continue;
				fastSz = jobs[i].data.size();
				fast.method = jobs[i].method;
				fast.flags = jobs[i].flags;
				fast.prop = jobs[i].prop;
				fast.zstdLdm = jobs[i].zstdLdm;
				fast.data.swap(jobs[i].data);
			}
		}
		if (havePpmd && ppmd.method == M_PPMD && ppmdSz * 20 <= fastSz * 19)
			best = std::move(ppmd);
		else
			best = std::move(fast);
	}
	if (best.method == M_STORE) {
		try { best.data.assign(raw, raw + n); } catch (...) {}
	}
	return best;
}

static int EmitMethod(const uint8_t* raw, size_t n, int method, int flags, int zstdLdm, Choice* out)
{
	std::vector<uint8_t> b, tmp;
	const uint8_t* src = raw;
	uint8_t prop = 0;
	int ok = 0;
	EncPulseUser watch;
	if (flags != 0) {
		try { b.assign(raw, raw + n); } catch (...) { return 0; }
		if (flags & F_RIP) OxzRipConvert(b.data(), b.size(), 1);
		if (flags & F_DELTA4) Delta4Enc(b.data(), b.size());
		else if (flags & F_DELTA1) Delta1Enc(b.data(), b.size());
		src = b.data();
	}
	watch.fp = tFilePulse;
	watch.lo = tEmitLo;
	if (tFilePulse && tEmitLo > 0) Publish(tFilePulse, tEmitLo, 3);
	gEncHook.fn = OnEncodePulse;
	gEncHook.user = &watch;
	struct EmitHold {
		EmitHold() { tPulseEmit = 1; }
		~EmitHold() { tPulseEmit = 0; gEncHook.fn = NULL; gEncHook.user = NULL; }
	} hold;
	ok = OneCodec(src, n, method, method == M_ZSTD ? (zstdLdm ? 1 : 0) : 0, tmp, &prop);
	(void)hold;
	if (!ok || tmp.size() >= n) return 0;
	out->method = method;
	out->flags = flags;
	out->prop = prop;
	out->zstdLdm = (method == M_ZSTD) ? zstdLdm : 0;
	out->data.swap(tmp);
	return 1;
}

static int BuildSample(const uint8_t* raw, size_t n, std::vector<uint8_t>& out)
{
	size_t count = kSampleCount;
	size_t slice = kSampleSlice;
	size_t span;
	size_t i;
	if (slice > n) slice = n;
	if (slice == 0) return 0;
	if (count < 2) count = 2;
	if (count * slice > n) {
		count = n / slice;
		if (count < 2) count = 2;
		if (count * slice > n) slice = n / count;
	}
	if (slice == 0) return 0;
	span = n - slice;
	try {
		out.clear();
		out.reserve(count * slice);
	} catch (...) { return 0; }
	for (i = 0; i < count; ++i) {
		size_t off = (span * i) / (count - 1);
		out.insert(out.end(), raw + off, raw + off + slice);
	}
	return out.empty() ? 0 : 1;
}

static int KeepSmaller(Choice* best, int method, std::vector<uint8_t>& packed)
{
	if (packed.empty() || packed.size() >= best->data.size()) return 0;
	best->method = method;
	best->flags = 0;
	best->prop = 0;
	best->zstdLdm = 0;
	best->data.swap(packed);
	return 1;
}

static int TrySpecial(const uint8_t* raw, size_t n, Choice* best)
{
	int pcm, pict, vid, any = 0;
	Choice got;
	EncPulseUser watch;
	std::vector<uint8_t> packed;
	pcm = OxzPcmMaybe(raw, n);
	pict = OxzPictMaybe(raw, n);
	vid = OxzVidMaybe(raw, n);
	if (!pcm && !pict && !vid) return 0;
	got.method = M_STORE;
	got.flags = 0;
	got.prop = 0;
	got.zstdLdm = 0;
	try { got.data.assign(raw, raw + n); }
	catch (...) { return 0; }
	watch.fp = tFilePulse;
	watch.lo = 250;
	if (tFilePulse) Publish(tFilePulse, 250, 3);
	gEncHook.fn = OnEncodePulse;
	gEncHook.user = &watch;
	tPulseEmit = 1;
	if (pcm) {
		if (OxzFlacCompress(raw, n, packed)) any |= KeepSmaller(&got, M_FLAC, packed);
		packed.clear();
		if (OxzAlacCompress(raw, n, packed)) any |= KeepSmaller(&got, M_ALAC, packed);
		packed.clear();
		if (OxzTtaCompress(raw, n, packed)) any |= KeepSmaller(&got, M_TTA, packed);
		packed.clear();
	}
	if (pict) {
		if (OxzPictCompress(raw, n, packed)) any |= KeepSmaller(&got, M_PICT, packed);
		packed.clear();
	}
	if (vid) {
		if (OxzVidCompress(raw, n, packed)) any |= KeepSmaller(&got, M_VID, packed);
		packed.clear();
	}
	if (TryZstd(raw, n, packed, 0)) any |= KeepSmaller(&got, M_ZSTD, packed);
	tPulseEmit = 0;
	gEncHook.fn = NULL;
	gEncHook.user = NULL;
	if (!any || got.method == M_STORE || got.data.size() >= n) return 0;
	*best = got;
	return 1;
}

static Choice Pick(const uint8_t* raw, size_t n, int isPe)
{
	Choice best;
	gLevelBytes = n;
	if (!isPe && n >= 4096 && TrySpecial(raw, n, &best)) return best;
	if (n <= kFullPick) return Search(raw, n, isPe, 0);
	{
		std::vector<uint8_t> sample;
		Choice probe;
		FastAlt alt;
		int got = 0;
		int method = M_STORE;
		int flags = 0;
		int ldm = 0;
		int cap = 0;
		int ranked = 0;
		int scoutSave;
		int ceilSave;
		size_t bound;
		if (!BuildSample(raw, n, sample)) return Search(raw, n, isPe, 0);
		bound = ZSTD_compressBound(sample.size());
		if (!isPe && n >= kFastFile && !ZSTD_isError(bound) && bound > 0) {
			std::vector<uint8_t> quick;
			size_t zgot = 0;
			try { quick.resize(bound); } catch (...) { quick.clear(); }
			if (!quick.empty())
				zgot = ZSTD_compress(quick.data(), quick.size(), sample.data(), sample.size(), 1);
			if (!quick.empty() && !ZSTD_isError(zgot) && zgot > 0 && zgot * 25 < sample.size()) {
				method = M_ZSTD;
				cap = 1;
			}
		}
		if (!cap) {
			probe = Search(sample.data(), sample.size(), isPe, &alt);
			ranked = 1;
			method = probe.method;
			flags = probe.flags;
			ldm = probe.zstdLdm;
		}
		if (ranked && !isPe && n >= kFastFile && !sample.empty() && probe.method != M_STORE
			&& probe.data.size() > 0 && probe.data.size() * 25 < sample.size()) {
			cap = 1;
			ldm = 0;
			if (CodecIsSlow(probe.method) && alt.method != M_STORE && alt.packed > 0 && alt.packed < sample.size()) {
				method = alt.method;
				flags = alt.flags;
			}
		}
		best.method = M_STORE;
		best.flags = 0;
		best.prop = 0;
		best.zstdLdm = 0;
		tEmitLo = 250;
		scoutSave = tScout;
		ceilSave = tZstdCeiling;
		if (cap) {
			tScout = 1;
			tZstdCeiling = 1;
		}
		if (method != M_STORE && EmitMethod(raw, n, method, flags, ldm, &best))
			got = 1;
		else if (method != M_ZSTD && EmitMethod(raw, n, M_ZSTD, flags, cap ? 0 : 1, &best))
			got = 1;
		tScout = scoutSave;
		tZstdCeiling = ceilSave;
		tEmitLo = 0;
		if (got) return best;
		try { best.data.assign(raw, raw + n); } catch (...) {}
	}
	return best;
}

struct ExWatch {
	OxzCoreProgFn fn;
	void* user;
	const wchar_t* name;
	unsigned index;
	unsigned total;
	uint64_t unpacked;
	DWORD tick;
};

static thread_local ExWatch tExWatch = { NULL, NULL, NULL, 0, 0, 0, 0 };
static thread_local int tExOn = 0;

static void PulseExtract(uint64_t produced);

static int DecLzma(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen, uint8_t prop)
{
	ISzAlloc alloc = { OxzLzmaAlloc, OxzLzmaFree };
	CLzma2Dec dec;
	size_t produced = 0, consumed = 0;
	ELzmaStatus st = LZMA_STATUS_NOT_SPECIFIED;
	SRes r = SZ_OK;
	Lzma2Dec_Construct(&dec);
	if (Lzma2Dec_Allocate(&dec, prop, &alloc) != SZ_OK) return 0;
	Lzma2Dec_Init(&dec);
	if (!tExOn) {
		SizeT dest = dstLen;
		SizeT slen = srcLen;
		r = Lzma2Dec_DecodeToBuf(&dec, dst, &dest, src, &slen, LZMA_FINISH_END, &st);
		Lzma2Dec_Free(&dec, &alloc);
		return r == SZ_OK && dest == dstLen;
	}
	while (produced < dstLen) {
		SizeT dest = dstLen - produced;
		SizeT slen = srcLen - consumed;
		ELzmaFinishMode mode;
		int last = dest <= (1u << 20);
		if (!last) dest = 1u << 20;
		mode = last ? LZMA_FINISH_END : LZMA_FINISH_ANY;
		r = Lzma2Dec_DecodeToBuf(&dec, dst + produced, &dest, src + consumed, &slen, mode, &st);
		if (r != SZ_OK) break;
		produced += dest;
		consumed += slen;
		PulseExtract(produced);
		if (dest == 0 && slen == 0) { r = SZ_ERROR_DATA; break; }
	}
	Lzma2Dec_Free(&dec, &alloc);
	return r == SZ_OK && produced == dstLen;
}

static int DecBrotli(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
	size_t ip = 0, op = 0;
	while (op < dstLen && ip < srcLen) {
		BrotliDecoderState* s = BrotliDecoderCreateInstance(BrotliTlsAlloc, BrotliTlsFree, NULL);
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
		if (tExOn) PulseExtract(op);
		if (op == dstLen) return 1;
	}
	return op == dstLen;
}

static int DecPpmd(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
	ISzAlloc alloc = { OxzLzmaAlloc, OxzLzmaFree };
	CPpmd7 ppmdObj;
	CPpmd7* ppmd = &ppmdObj;
	unsigned order, mem;
	int ok = 0;
	if (srcLen < 5) return 0;
	order = src[0];
	mem = (unsigned)src[1] | ((unsigned)src[2] << 8) | ((unsigned)src[3] << 16) | ((unsigned)src[4] << 24);
	if (order < PPMD7_MIN_ORDER || order > PPMD7_MAX_ORDER) return 0;
	if (mem < PPMD7_MIN_MEM_SIZE || mem > PPMD7_MAX_MEM_SIZE) return 0;
	Ppmd7_Construct(ppmd);
	if (!Ppmd7_Alloc(ppmd, mem, &alloc)) return 0;
	Ppmd7_Init(ppmd, order);
	Ppmd7z_RangeDec_Bind(src + 5, src + srcLen);
	if (Ppmd7z_RangeDec_Init(&ppmd->rc.dec) && !Ppmd7z_RangeDec_Over()) {
		Byte* got = Ppmd7z_DecodeSymbols(ppmd, dst, dst + dstLen);
		if (got == dst + dstLen && !Ppmd7z_RangeDec_Over() && Ppmd7z_RangeDec_IsFinishedOK(&ppmd->rc.dec)) ok = 1;
	}
	Ppmd7z_RangeDec_Bind(0, 0);
	Ppmd7_Free(ppmd, &alloc);
	return ok;
}

struct RawBuf {
	uint8_t* data;
	size_t cap;
	RawBuf() : data(NULL), cap(0) {}
	~RawBuf() { free(data); }
	int ensure(size_t n) {
		if (n <= cap) return 1;
		size_t want = n < 65536 ? 65536 : n;
		void* nb = realloc(data, want);
		if (!nb) return 0;
		data = (uint8_t*)nb;
		cap = want;
		return 1;
	}
};

static int DecodeToBuf(const DirEnt& e, const uint8_t* src, uint8_t* dst, size_t n)
{
	struct libdeflate_decompressor* d;
	size_t got = 0;
	enum libdeflate_result rr;
	if (e.unpacked != (uint64_t)n) return -1;
	if (n == 0) return e.crc == 0 && e.packed == 0;
	if (!dst) return -1;
	if (e.method == M_STORE) {
		if (e.packed != e.unpacked) return 0;
		memcpy(dst, src, n);
	} else if (e.method == M_ZSTD) {
		ZSTD_DCtx* zdc = TlsZstdDCtx();
		if (!zdc) return 0;
		got = ZSTD_decompressDCtx(zdc, dst, n, src, (size_t)e.packed);
		if (ZSTD_isError(got) || got != n) return 0;
	} else if (e.method == M_LZ4) {
		int g;
		if (n > kLzBlockMax || e.packed > kLzBlockMax) return 0;
		g = LZ4_decompress_safe((const char*)src, (char*)dst, (int)e.packed, (int)n);
		if (g < 0 || (size_t)g != n) return 0;
	} else if (e.method == M_LIZARD) {
		int g;
		if (n > kLzBlockMax || e.packed > kLzBlockMax) return 0;
		g = Lizard_decompress_safe((const char*)src, (char*)dst, (int)e.packed, (int)n);
		if (g < 0 || (size_t)g != n) return 0;
	} else if (e.method == M_LZFSE) {
		size_t scratchNeed = lzfse_decode_scratch_size();
		void* scratch = scratchNeed ? TlsLzfseScratch(scratchNeed) : NULL;
		if (scratchNeed && !scratch) return 0;
		got = lzfse_decode_buffer(dst, n, src, (size_t)e.packed, scratch);
		if (got != n) return 0;
	} else if (e.method == M_DEFLATE || e.method == M_RAWDEFLATE) {
		d = TlsDeflateDecomp();
		if (!d) return 0;
		rr = e.method == M_RAWDEFLATE
			? libdeflate_deflate_decompress(d, src, (size_t)e.packed, dst, n, &got)
			: libdeflate_zlib_decompress(d, src, (size_t)e.packed, dst, n, &got);
		if (rr != LIBDEFLATE_SUCCESS || got != n) return 0;
	} else if (e.method == M_BZIP2) {
		if (n > 0xFFFFFFFFull || e.packed > 0xFFFFFFFFull) return 0;
		if (!OxzBzipDecompress(src, (size_t)e.packed, dst, n, InnerThreads(n))) return 0;
	} else if (e.method == M_LZMA2) {
		if (!DecLzma(src, (size_t)e.packed, dst, n, e.prop)) return 0;
	} else if (e.method == M_BROTLI) {
		if (!DecBrotli(src, (size_t)e.packed, dst, n)) return 0;
	} else if (e.method == M_PPMD) {
		if (!DecPpmd(src, (size_t)e.packed, dst, n)) return 0;
	} else if (e.method == M_FLAC) {
		if (!OxzFlacDecompress(src, (size_t)e.packed, dst, n)) return 0;
	} else if (e.method == M_ALAC) {
		if (!OxzAlacDecompress(src, (size_t)e.packed, dst, n)) return 0;
	} else if (e.method == M_TTA) {
		if (!OxzTtaDecompress(src, (size_t)e.packed, dst, n)) return 0;
	} else if (e.method == M_PICT) {
		if (!OxzPictDecompress(src, (size_t)e.packed, dst, n)) return 0;
	} else if (e.method == M_VID) {
		if (!OxzVidDecompress(src, (size_t)e.packed, dst, n)) return 0;
	} else if (e.method == M_LZSS) {
		if (!OxzLzssDecompress(src, (size_t)e.packed, dst, n)) return 0;
	} else {
		return 0;
	}
	if (e.flags & F_DELTA4) Delta4Dec(dst, n);
	else if (e.flags & F_DELTA1) Delta1Dec(dst, n);
	if (e.flags & F_RIP) OxzRipConvert(dst, n, 0);
	if (e.crc == CrcFinish(dst, n)) return 1;
	if (e.method == M_BZIP2 && n <= 0xFFFFFFFFull && e.packed <= 0xFFFFFFFFull) {
		if (!OxzBzipDecompress(src, (size_t)e.packed, dst, n, 0)) return 0;
		if (e.flags & F_DELTA4) Delta4Dec(dst, n);
		else if (e.flags & F_DELTA1) Delta1Dec(dst, n);
		if (e.flags & F_RIP) OxzRipConvert(dst, n, 0);
		if (e.crc == CrcFinish(dst, n)) return 1;
	}
	return 0;
}

static int DecodeRaw(const DirEnt& e, const uint8_t* src, std::vector<uint8_t>& plain)
{
	size_t n = (size_t)e.unpacked;
	if (e.unpacked != (uint64_t)n) return -1;
	try { plain.resize(n); } catch (...) { return -1; }
	if (n == 0) return e.crc == 0 && e.packed == 0;
	return DecodeToBuf(e, src, plain.data(), n);
}

static int MethodHasPayload(uint16_t m)
{
	return m == M_STORE || m == M_ZSTD || m == M_LZ4 || m == M_DEFLATE || m == M_LZMA2
		|| m == M_BROTLI || m == M_RAWDEFLATE || m == M_LIZARD || m == M_LZFSE
		|| m == M_BZIP2 || m == M_PPMD || m == M_FLAC || m == M_ALAC || m == M_TTA
		|| m == M_PICT || m == M_VID || m == M_LZSS;
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

static int CopyRangeBuf(HANDLE in, uint64_t off, uint64_t n, HANDLE out, std::vector<uint8_t>& buf)
{
	if (!Seek(in, off)) return 0;
	if (buf.empty()) {
		try { buf.resize(4u << 20); } catch (...) { return 0; }
	}
	while (n) {
		DWORD chunk = n > buf.size() ? (DWORD)buf.size() : (DWORD)n;
		DWORD rd = 0;
		if (!ReadFile(in, buf.data(), chunk, &rd, NULL) || rd != chunk) return 0;
		if (!WriteAll(out, buf.data(), rd)) return 0;
		n -= rd;
	}
	return 1;
}

static int CopyRange(HANDLE in, uint64_t off, uint64_t n, HANDLE out)
{
	std::vector<uint8_t> buf;
	return CopyRangeBuf(in, off, n, out, buf);
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

static int LoadFileRaw(const std::wstring& path, uint64_t size, const uint8_t** outPtr)
{
	HANDLE h;
	uint8_t* dst;
	size_t got = 0;
	const size_t step = 8u << 20;
	if (size != (uint64_t)(size_t)size) return 0;
	dst = TlsFileBuf((size_t)size ? (size_t)size : 1);
	if (!dst) return 0;
	h = OpenRead(path.c_str());
	if (h == INVALID_HANDLE_VALUE) return 0;
	while (got < (size_t)size) {
		size_t n = (size_t)size - got;
		DWORD rd = 0;
		int ten;
		if (n > step) n = step;
		if (!ReadFile(h, dst + got, (DWORD)n, &rd, NULL) || rd != (DWORD)n) { CloseHandle(h); return 0; }
		got += n;
		if (tFilePulse && size) {
			ten = (int)(got * 240ull / (size_t)size);
			if (ten > 240) ten = 240;
			Publish(tFilePulse, ten, 0);
		}
	}
	CloseHandle(h);
	*outPtr = dst;
	return 1;
}

static int FillPicked(const std::wstring& path, uint64_t size, OutItem* it)
{
	const uint8_t* raw = NULL;
	size_t n = (size_t)size;
	Choice c;
	if (!LoadFileRaw(path, size, &raw)) return 0;
	c = Pick(raw, n, LooksPe(raw, n));
	it->crc = n ? CrcFinish(raw, n) : 0;
	it->method = (uint16_t)c.method;
	it->flags = (uint16_t)c.flags;
	it->prop = c.prop;
	it->unpacked = size;
	it->packed = c.data.size();
	it->payload.swap(c.data);
	it->op = OP_COMP;
	return 1;
}

struct StreamProg {
	OxzCoreProgFn fn;
	void* user;
	const wchar_t* name;
	uint64_t size;
	unsigned long long baseNumer;
	int useWeight;
	DWORD tick;
};

static void StreamNote(StreamProg* sp, uint64_t done, int finish)
{
	int ten;
	DWORD now;
	PulseWeight* w;
	if (!sp || !sp->fn || !sp->size) return;
	if (done > sp->size) done = sp->size;
	ten = finish ? 1000 : (int)(done * 1000ull / sp->size);
	if (!finish && ten > 999) ten = 999;
	if (ten < 0) ten = 0;
	now = GetTickCount();
	if (!finish && ten > 0 && ten < 999 && sp->tick && (DWORD)(now - sp->tick) < 80) return;
	sp->tick = now ? now : 1;
	w = (PulseWeight*)sp->user;
	if (sp->useWeight && w && w->magic == kPulseMagic) {
		unsigned long long n = sp->baseNumer + (finish ? sp->size : done);
		if (w->denom && n > w->denom) n = w->denom;
		w->numer = n;
		w->fileTen = ten;
	}
	sp->fn(sp->name, 0, 0, finish ? 100 : ten / 10, sp->user);
}

static void NoteZstd(ZSTD_CCtx* c, StreamProg* sp, uint64_t size, int finish)
{
	ZSTD_frameProgression pr;
	uint64_t done;
	if (!sp || !sp->fn) return;
	if (finish) {
		StreamNote(sp, size, 1);
		return;
	}
	pr = ZSTD_getFrameProgression(c);
	done = pr.consumed;
	if (done > size) done = size;
	StreamNote(sp, done, 0);
}

static void ArmStream(StreamProg* sp, OxzCoreProgFn prog, void* user, const wchar_t* name, uint64_t size)
{
	PulseWeight* w;
	memset(sp, 0, sizeof(*sp));
	if (!prog) return;
	sp->fn = prog;
	sp->user = user;
	sp->name = name;
	sp->size = size ? size : 1;
	w = (PulseWeight*)user;
	if (w && w->magic == kPulseMagic && w->denom) {
		sp->useWeight = 1;
		sp->baseNumer = w->numer;
	}
}

static int StreamStore(HANDLE in, HANDLE out, uint64_t size, uint32_t* crc, StreamProg* sp)
{
	std::vector<uint8_t> buf;
	uint32_t c = 0xFFFFFFFFu;
	uint64_t left = size;
	uint64_t got = 0;
	try { buf.resize(4u << 20); } catch (...) { return 0; }
	if (!Seek(in, 0)) return 0;
	while (left) {
		DWORD chunk = left > buf.size() ? (DWORD)buf.size() : (DWORD)left;
		DWORD rd = 0;
		if (!ReadFile(in, buf.data(), chunk, &rd, NULL) || rd != chunk) return 0;
		c = CrcBlock(buf.data(), rd, c);
		if (!WriteAll(out, buf.data(), rd)) return 0;
		left -= rd;
		got += rd;
		StreamNote(sp, got, 0);
	}
	*crc = c ^ 0xFFFFFFFFu;
	return 1;
}

static int StreamZstdOrStore(const std::wstring& path, uint64_t size, HANDLE out, uint64_t dataOff, OutItem* it, StreamProg* sp)
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
	ZSTD_CCtx_setPledgedSrcSize(c, size);
	{
		int wlog = 23;
		size_t zn = size >= 2ull * 1024ull * 1024ull ? (size_t)(2ull * 1024ull * 1024ull) : (size_t)size;
		if (size >= 32ull * 1024ull * 1024ull) wlog = 25;
		else if (size >= 8ull * 1024ull * 1024ull) wlog = 24;
		else if (size < 1ull * 1024ull * 1024ull) wlog = 21;
		if (size >= 2ull * 1024ull * 1024ull && size <= (uint64_t)~(size_t)0) zn = (size_t)size;
		ZSTD_CCtx_setParameter(c, ZSTD_c_windowLog, wlog);
		int workers = ZstdArm(c, zn, level, wlog);
		try { ibuf.resize(4u << 20); obuf.resize(4u << 20); }
		catch (...) { ZSTD_freeCCtx(c); CloseHandle(in); return 0; }
		left = size;
		while (!ended) {
			size_t chunk = 0;
			ZSTD_inBuffer input;
			int last, spins = 0;
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
				size_t inBefore = input.pos;
				output.dst = obuf.data();
				output.size = obuf.size();
				output.pos = 0;
				rem = ZSTD_compressStream2(c, &output, &input, last ? ZSTD_e_end : ZSTD_e_continue);
				if (ZSTD_isError(rem)) { ended = -1; break; }
				if (output.pos && !WriteAll(out, obuf.data(), output.pos)) { ended = -1; break; }
				produced += output.pos;
				NoteZstd(c, sp, size, 0);
				if (last) {
					if (rem == 0) { ended = 1; break; }
				} else if (input.pos == input.size) {
					break;
				}
				if (input.pos == inBefore && output.pos == 0) {
					if (workers < 2 || ++spins > 20000) { ended = -1; break; }
					Sleep(1);
					continue;
				}
				spins = 0;
			}
			if (ended < 0) break;
		}
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
		StreamNote(sp, size, 1);
		CloseHandle(in);
		return 1;
	}
	if (!Seek(out, dataOff) || !StreamStore(in, out, size, &crc, sp)) { CloseHandle(in); return 0; }
	if (!SetEndOfFile(out)) { CloseHandle(in); return 0; }
	it->method = M_STORE;
	it->flags = 0;
	it->prop = 0;
	it->crc = crc;
	it->unpacked = size;
	it->packed = size;
	it->op = OP_STREAM;
	StreamNote(sp, size, 1);
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
	out = CreateFileW(building.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
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
		HANDLE snapSrc = INVALID_HANDLE_VALUE;
		uint64_t snapLen = 0;
		int snapTried = 0;
		std::vector<uint8_t> copyBuf;
		auto ensureSnapSrc = [&]() -> HANDLE {
			if (!snapTried) {
				snapTried = 1;
				if (snapOk && !gSnap.archive.empty()) {
					snapSrc = OpenRead(gSnap.archive.c_str());
					if (snapSrc != INVALID_HANDLE_VALUE) snapLen = FileLen(snapSrc);
				}
			}
			return snapSrc;
		};
		struct SnapClose {
			HANDLE* ph;
			explicit SnapClose(HANDLE* p) : ph(p) {}
			~SnapClose() { if (*ph != INVALID_HANDLE_VALUE) { CloseHandle(*ph); *ph = INVALID_HANDLE_VALUE; } }
		} snapClose(&snapSrc);
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
					else { pr.ready = -1; guard.cancel(); }
				} catch (...) { pr.ready = -1; guard.cancel(); }
			};
			try { prepared.resize(items.size()); }
			catch (...) { goto fail; }
			for (i = 0; i < items.size(); ++i) {
				if (!must(i)) continue;
				if (items[i].size >= 8ull * 1024ull * 1024ull) largePick.push_back(i);
				else smallPick.push_back(i);
			}
			{
				unsigned poolCap = HwCap();
				SaveWorkerPool workerPool(poolCap > 1 ? poolCap - 1 : 0);
				struct PoolArm {
					explicit PoolArm(SaveWorkerPool* p) { gSavePool = p; }
					~PoolArm() { gSavePool = NULL; }
				} poolArm(poolCap > 1 ? &workerPool : NULL);
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
				if (smallPick.size() < poolCap || poolCap <= 1) {
					for (i = 0; i < smallPick.size(); ++i) runPick(smallPick[i]);
				} else {
					std::stable_sort(smallPick.begin(), smallPick.end(), [&](size_t a, size_t b) {
						return items[a].size > items[b].size;
					});
					struct SmallCtx {
						const std::vector<size_t>* list;
						const decltype(runPick)* fn;
					} sctx = { &smallPick, &runPick };
					auto step = [](void* p, size_t idx) {
						SmallCtx* sc = (SmallCtx*)p;
						int prevPool = tInFilePool;
						int prevInline = tRunInline;
						tInFilePool = 1;
						tRunInline = 1;
						(*sc->fn)((*sc->list)[idx]);
						tRunInline = prevInline;
						tInFilePool = prevPool;
					};
					workerPool.Run(smallPick.size(), step, &sctx);
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
			HANDLE src = ensureSnapSrc();
			int copied = 0;
			if (src != INVALID_HANDLE_VALUE) {
				if (sf->dataOff <= snapLen && sf->packed <= snapLen - sf->dataOff && Seek(out, pos))
					copied = CopyRangeBuf(src, sf->dataOff, sf->packed, out, copyBuf);
			}
			if (!copied) {
				OutItem it;
				it.rel = w.rel;
				it.attr = w.attr;
				it.mtime = w.mtime;
				if (!FillPicked(w.disk, w.size, &it)) {
					StreamProg sp;
					ArmStream(&sp, prog, progUser, w.rel.c_str(), w.size);
					if (!StreamZstdOrStore(w.disk, w.size, out, pos, &it, prog ? &sp : NULL)) goto fail;
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
				StreamProg sp;
				ArmStream(&sp, prog, progUser, w.rel.c_str(), w.size);
				if (!StreamZstdOrStore(w.disk, w.size, out, pos, &it, prog ? &sp : NULL)) goto fail;
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
			int copied = 0;
			if (sf.origin != ORIGIN_OVERLAY || sf.dropped) continue;
			if (live.find(sf.rel) != live.end()) continue;
			if (prog) prog(sf.rel.c_str(), index, total, 0, progUser);
			if (sf.isDir) {
				PutEntry(dir, M_DIR, 0, 0, 0, sf.attr, sf.mtime, 0, 0, 0, sf.rel);
				count++;
			} else if (MethodHasPayload(sf.method)) {
				src = ensureSnapSrc();
				if (src != INVALID_HANDLE_VALUE) {
					if (sf.dataOff <= snapLen && sf.packed <= snapLen - sf.dataOff && Seek(out, pos))
						copied = CopyRangeBuf(src, sf.dataOff, sf.packed, out, copyBuf);
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
	DWORD safeAttr = attr & (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
	DWORD flags = (safeAttr ? safeAttr : FILE_ATTRIBUTE_NORMAL) | FILE_FLAG_SEQUENTIAL_SCAN;
	h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, flags, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		if (err == ERROR_PATH_NOT_FOUND) {
			EnsureParents(path);
			h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, flags, NULL);
			if (h == INVALID_HANDLE_VALUE) err = GetLastError();
		}
		if (h == INVALID_HANDLE_VALUE && err == ERROR_ACCESS_DENIED) {
			SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
			h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, flags, NULL);
		}
		if (h == INVALID_HANDLE_VALUE) return 0;
	}
	if (n && !WriteAll(h, p, n)) { CloseHandle(h); DeleteFileW(path.c_str()); return 0; }
	ft = UToFt(mtime);
	SetFileTime(h, &ft, &ft, &ft);
	CloseHandle(h);
	if (attr & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path.c_str(), safeAttr | FILE_ATTRIBUTE_READONLY);
	return 1;
}

static void PulseExtract(uint64_t produced)
{
	int ten;
	DWORD now;
	if (!tExOn || !tExWatch.fn) return;
	ten = 0;
	if (tExWatch.unpacked) {
		if (produced > tExWatch.unpacked) produced = tExWatch.unpacked;
		ten = (int)(produced * 1000ull / tExWatch.unpacked);
	}
	if (ten > 999) ten = 999;
	now = GetTickCount();
	if (ten > 0 && tExWatch.tick && (DWORD)(now - tExWatch.tick) < 80) return;
	tExWatch.tick = now ? now : 1;
	if (tExWatch.user) {
		PulseWeight* w = (PulseWeight*)tExWatch.user;
		if (w->magic == kPulseMagic) w->fileTen = ten;
	}
	tExWatch.fn(tExWatch.name, tExWatch.index, tExWatch.total, ten / 10, tExWatch.user);
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
	out = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (out == INVALID_HANDLE_VALUE) return 0;
	try { ibuf.resize(4u << 20); obuf.resize(4u << 20); }
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
			PulseExtract(produced);
		}
	} else if (e.method == M_ZSTD) {
		ZSTD_DCtx* d = TlsZstdDCtx();
		if (!d) { CloseHandle(out); return 0; }
		while (left || produced < e.unpacked) {
			DWORD chunk = 0;
			ZSTD_inBuffer input;
			if (left) {
				DWORD want = left > ibuf.size() ? (DWORD)ibuf.size() : (DWORD)left;
				DWORD rd = 0;
				if (!ReadFile(arc, ibuf.data(), want, &rd, NULL) || rd != want) { CloseHandle(out); return 0; }
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
				if (ZSTD_isError(rem)) { CloseHandle(out); return 0; }
				if (output.pos) {
					crc = CrcBlock(obuf.data(), output.pos, crc);
					if (!WriteAll(out, obuf.data(), output.pos)) { CloseHandle(out); return 0; }
					produced += output.pos;
					PulseExtract(produced);
				}
				if (rem == 0 && input.pos == input.size) break;
				if (input.pos == input.size && output.pos == 0) {
					if (left == 0) { CloseHandle(out); return 0; }
					break;
				}
				if (input.pos == before && output.pos == 0) { CloseHandle(out); return 0; }
			}
			if (produced > e.unpacked) { CloseHandle(out); return 0; }
			if (left == 0) break;
		}
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
		th = CreateFileW(tempBase.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
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
		std::set<std::wstring, ILess> parents;
		unsigned total = overlays ? overlays : 1;
		std::atomic<unsigned> done(0);
		std::atomic<int> failed(0);
		CrcEnsure();
		auto cleanRel = [](const std::wstring& rel) -> int {
			size_t k;
			if (rel.empty()) return 0;
			for (k = 0; k < rel.size(); ++k) {
				wchar_t ch = rel[k];
				if (ch == L'/') return 0;
				if ((ch == L'.' || ch == L' ') && (k + 1 == rel.size() || rel[k + 1] == L'\\')) return 0;
			}
			return 1;
		};
		auto runOne = [&](const ExJob& job, HANDLE rh, const uint8_t* spanPtr, uint64_t spanOff, uint64_t spanEnd, RawBuf& packed, RawBuf& plain) -> int {
			int dr;
			size_t upk = 0;
			const DirEnt& ent = ents[job.ei];
			if (failed.load()) return 0;
			if (prog) prog(ent.name.c_str(), done.load(), total, 0, progUser);
			if (!spanPtr && ent.flags == 0 && (ent.method == M_STORE || ent.method == M_ZSTD) && ent.unpacked >= 1024ull * 1024ull) {
				int sok;
				tExWatch.fn = prog;
				tExWatch.user = progUser;
				tExWatch.name = ent.name.c_str();
				tExWatch.index = done.load();
				tExWatch.total = total;
				tExWatch.unpacked = ent.unpacked;
				tExWatch.tick = 0;
				tExOn = 1;
				sok = StreamDecode(rh, ent, job.full);
				tExOn = 0;
				if (!sok) return 0;
				done.fetch_add(1);
				if (prog) prog(ent.name.c_str(), done.load(), total, 100, progUser);
				return 1;
			}
			if (ent.packed != (uint64_t)(size_t)ent.packed || ent.unpacked != (uint64_t)(size_t)ent.unpacked) dr = -1;
			else {
				size_t pk = (size_t)ent.packed;
				const uint8_t* pkSrc = NULL;
				upk = (size_t)ent.unpacked;
				if (spanPtr && ent.dataOff >= spanOff && ent.packed <= spanEnd - ent.dataOff) {
					pkSrc = spanPtr + (size_t)(ent.dataOff - spanOff);
					dr = (upk == 0 || plain.ensure(upk)) ? 1 : -1;
				} else {
					dr = ((pk == 0 || packed.ensure(pk)) && (upk == 0 || plain.ensure(upk))) ? 1 : -1;
					if (dr == 1) {
						if (pk && !ReadAt(rh, ent.dataOff, packed.data, pk)) return 0;
						pkSrc = pk ? packed.data : (const uint8_t*)"";
					}
				}
				if (dr == 1) {
					if (prog && ent.unpacked >= 1024ull * 1024ull) {
						tExWatch.fn = prog;
						tExWatch.user = progUser;
						tExWatch.name = ent.name.c_str();
						tExWatch.index = done.load();
						tExWatch.total = total;
						tExWatch.unpacked = ent.unpacked;
						tExWatch.tick = 0;
						tExOn = 1;
					}
					dr = DecodeToBuf(ent, pk ? pkSrc : (const uint8_t*)"", upk ? plain.data : (uint8_t*)"", upk);
					tExOn = 0;
				}
			}
			if (dr == 1) {
				if (!WriteMember(job.full, upk ? plain.data : NULL, upk, ent.mtime, ent.attr)) return 0;
			} else if (dr < 0 && rh != INVALID_HANDLE_VALUE && ent.flags == 0 && (ent.method == M_STORE || ent.method == M_ZSTD)) {
				int sok;
				tExWatch.fn = prog;
				tExWatch.user = progUser;
				tExWatch.name = ent.name.c_str();
				tExWatch.index = done.load();
				tExWatch.total = total;
				tExWatch.unpacked = ent.unpacked;
				tExWatch.tick = 0;
				tExOn = 1;
				sok = StreamDecode(rh, ent, job.full);
				tExOn = 0;
				if (!sok) return 0;
			} else {
				return 0;
			}
			done.fetch_add(1);
			if (prog) prog(ent.name.c_str(), done.load(), total, 100, progUser);
			return 1;
		};
		for (i = 0; i < ents.size(); ++i) {
			ExJob job;
			size_t sl;
			if (!MethodHasPayload(ents[i].method)) continue;
			job.ei = i;
			if (!root.empty() && root.size() + 1 + ents[i].name.size() < MAX_PATH - 4 && cleanRel(ents[i].name)) {
				job.full = JoinPath(root, ents[i].name);
			} else {
				job.full = FullPath(JoinPath(root, ents[i].name).c_str());
				if (!UnderRoot(root, job.full)) { CloseHandle(h); return OXZ_CORE_ERR; }
			}
			job.large = (ents[i].unpacked >= 8ull * 1024ull * 1024ull || ents[i].packed >= 8ull * 1024ull * 1024ull) ? 1 : 0;
			sl = job.full.find_last_of(L'\\');
			if (sl != std::wstring::npos && sl > root.size()) parents.insert(job.full.substr(0, sl));
			if (job.large || ents[i].packed != (uint64_t)(size_t)ents[i].packed) big.push_back(job);
			else small.push_back(job);
		}
		for (std::set<std::wstring, ILess>::iterator pit = parents.begin(); pit != parents.end(); ++pit) {
			EnsureParents(*pit);
			CreateDirectoryW(pit->c_str(), NULL);
		}
		if (big.size() <= 1) {
			RawBuf packed, plain;
			for (i = 0; i < big.size(); ++i) {
				int rc = runOne(big[i], h, NULL, 0, 0, packed, plain);
				if (rc != 1) { CloseHandle(h); return OXZ_CORE_ERR; }
			}
		} else {
			unsigned ft = HwCap();
			unsigned t;
			int heavy = 0;
			std::atomic<size_t> bcursor(0);
			std::vector<std::thread> bpool;
			for (i = 0; i < big.size(); ++i) {
				const DirEnt& be = ents[big[i].ei];
				if (!(be.flags == 0 && (be.method == M_STORE || be.method == M_ZSTD))) heavy = 1;
			}
			if (ft > big.size()) ft = (unsigned)big.size();
			if (heavy && ft > 2) ft = 2;
			else if (ft > 4) ft = 4;
			for (t = 0; t < ft; ++t) {
				bpool.push_back(std::thread([&]() {
					HANDLE rh = OpenRead(arc.c_str());
					RawBuf packed, plain;
					if (rh == INVALID_HANDLE_VALUE) { failed.store(1); return; }
					for (;;) {
						size_t c = bcursor.fetch_add(1);
						int rc;
						if (c >= big.size() || failed.load()) break;
						rc = runOne(big[c], rh, NULL, 0, 0, packed, plain);
						if (rc != 1) failed.store(1);
					}
					CloseHandle(rh);
				}));
				BoostThread(bpool.back());
			}
			for (t = 0; t < bpool.size(); ++t) bpool[t].join();
			if (failed.load()) { CloseHandle(h); return OXZ_CORE_ERR; }
		}
		if (small.size() <= 1) {
			RawBuf packed, plain;
			for (i = 0; i < small.size(); ++i) {
				if (runOne(small[i], h, NULL, 0, 0, packed, plain) != 1) { CloseHandle(h); return OXZ_CORE_ERR; }
			}
		} else {
			unsigned ft = HwCap();
			unsigned t;
			RawBuf arcSpan;
			uint64_t spanOff = (uint64_t)-1, spanEnd = 0;
			int haveSpan = 0;
			std::stable_sort(small.begin(), small.end(), [&](const ExJob& a, const ExJob& b) {
				return ents[a.ei].unpacked > ents[b.ei].unpacked;
			});
			for (i = 0; i < small.size(); ++i) {
				const DirEnt& se = ents[small[i].ei];
				if (!se.packed) continue;
				if (se.dataOff < spanOff) spanOff = se.dataOff;
				if (se.dataOff + se.packed > spanEnd) spanEnd = se.dataOff + se.packed;
			}
			if (spanEnd >= spanOff && spanEnd - spanOff <= 128ull * 1024ull * 1024ull) {
				size_t spanLen = (size_t)(spanEnd - spanOff);
				if (spanLen == 0) {
					haveSpan = 1;
				} else if (arcSpan.ensure(spanLen) && ReadAt(h, spanOff, arcSpan.data, spanLen)) {
					haveSpan = 1;
				}
			}
			if (ft > small.size()) ft = (unsigned)small.size();
			if (haveSpan) {
				SaveWorkerPool exPool(ft > 1 ? ft - 1 : 0);
				struct ExSpanCtx {
					const std::vector<ExJob>* list;
					const decltype(runOne)* fn;
					const uint8_t* ptr;
					uint64_t off;
					uint64_t end;
					std::atomic<int>* fail;
				} ectx = { &small, &runOne, spanEnd > spanOff ? arcSpan.data : (const uint8_t*)"", spanOff, spanEnd, &failed };
				auto step = [](void* p, size_t idx) {
					ExSpanCtx* ec = (ExSpanCtx*)p;
					static thread_local RawBuf tlPacked, tlPlain;
					int prevPool = tInFilePool;
					if (ec->fail->load()) return;
					tInFilePool = 1;
					if ((*ec->fn)((*ec->list)[idx], INVALID_HANDLE_VALUE, ec->ptr, ec->off, ec->end, tlPacked, tlPlain) != 1)
						ec->fail->store(1);
					tInFilePool = prevPool;
				};
				exPool.Run(small.size(), step, &ectx);
			} else {
				std::atomic<size_t> cursor(0);
				std::vector<std::thread> pool;
				for (t = 0; t < ft; ++t) {
					pool.push_back(std::thread([&]() {
						HANDLE rh = OpenRead(arc.c_str());
						RawBuf packed, plain;
						tInFilePool = 1;
						if (rh == INVALID_HANDLE_VALUE) { failed.store(1); return; }
						for (;;) {
							size_t c = cursor.fetch_add(1);
							int rc;
							if (c >= small.size() || failed.load()) break;
							rc = runOne(small[c], rh, NULL, 0, 0, packed, plain);
							if (rc != 1) failed.store(1);
						}
						CloseHandle(rh);
						tInFilePool = 0;
					}));
					BoostThread(pool.back());
				}
				for (t = 0; t < pool.size(); ++t) pool[t].join();
			}
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

extern "C" void OxzCoreSetThreads(int n)
{
	if (n < 0) n = 0;
	if (n > 12) n = 12;
	gThreadCap = n;
}

extern "C" int OxzCoreGetThreads(void)
{
	return gThreadCap;
}

extern "C" void OxzPulseEncode(unsigned long long done, unsigned long long span)
{
	if (gEncHook.fn && span) gEncHook.fn(gEncHook.user, done, span);
}

extern "C" void OxzPulseDecode(unsigned long long produced)
{
	PulseExtract((uint64_t)produced);
}

extern "C" int OxzEncodeLevel(void)
{
	return gUserLevel;
}

extern "C" int OxzEncodeScout(void)
{
	return tInSurvey || tScout;
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
	if (oldKey == newKey) return OXZ_CORE_OK;
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
	RawBuf plain;
	uint8_t* pkBuf;
	DirEnt e;
	uint64_t len;
	size_t pk, upk;
	std::wstring key;
	if (!rel || !destPath) return OXZ_CORE_ERR;
	key = SlashToBack(rel);
	sf = FindSnap(&gSnap, key);
	if (!sf || sf->dropped || sf->isDir || !MethodHasPayload(sf->method)) return OXZ_CORE_ERR;
	if (sf->unpacked != (uint64_t)(size_t)sf->unpacked || sf->packed != (uint64_t)(size_t)sf->packed) return OXZ_CORE_ERR;
	pk = (size_t)sf->packed;
	upk = (size_t)sf->unpacked;
	pkBuf = TlsFileBuf(pk ? pk : 1);
	if (!pkBuf || (upk && !plain.ensure(upk))) return OXZ_CORE_ERR;
	h = OpenRead(gSnap.archive.c_str());
	if (h == INVALID_HANDLE_VALUE) return OXZ_CORE_ERR;
	len = FileLen(h);
	if (sf->dataOff > len || sf->packed > len - sf->dataOff) { CloseHandle(h); return OXZ_CORE_ERR; }
	if (pk && !ReadAt(h, sf->dataOff, pkBuf, pk)) { CloseHandle(h); return OXZ_CORE_ERR; }
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
	CrcEnsure();
	if (DecodeToBuf(e, pk ? pkBuf : (const uint8_t*)"", upk ? plain.data : (uint8_t*)"", upk) != 1) return OXZ_CORE_ERR;
	if (!WriteMember(destPath, upk ? plain.data : NULL, upk, sf->mtime, sf->attr)) return OXZ_CORE_ERR;
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

struct OverlayDirCache {
	std::mutex mu;
	int valid;
	DWORD volSerial;
	DWORD idxHi;
	DWORD idxLo;
	uint64_t fileLen;
	uint64_t mtime;
	std::wstring path;
	std::vector<DirEnt> ents;
	OverlayDirCache() : valid(0), volSerial(0), idxHi(0), idxLo(0), fileLen(0), mtime(0) {}
};

static OverlayDirCache gOvCache;

extern "C" int OxzCoreReadOverlay(const wchar_t* archive, int overlay, void** data, size_t* size)
{
	HANDLE h;
	BY_HANDLE_FILE_INFORMATION fi;
	int haveFi;
	DirEnt ent;
	uint8_t* pkBuf;
	uint64_t len;
	size_t pk, n;
	void* block;
	if (!archive || !data || !size || overlay < 0) return OXZ_CORE_ERR;
	*data = NULL;
	*size = 0;
	CrcEnsure();
	h = OpenRead(archive);
	if (h == INVALID_HANDLE_VALUE) return OXZ_CORE_ERR;
	haveFi = GetFileInformationByHandle(h, &fi) ? 1 : 0;
	len = haveFi ? (((uint64_t)fi.nFileSizeHigh << 32) | fi.nFileSizeLow) : FileLen(h);
	if (haveFi) {
		uint64_t mt = FtToU(fi.ftLastWriteTime);
		int hit = 0;
		{
			std::lock_guard<std::mutex> lk(gOvCache.mu);
			if (gOvCache.valid && gOvCache.fileLen == len && gOvCache.mtime == mt
				&& gOvCache.volSerial == fi.dwVolumeSerialNumber
				&& gOvCache.idxHi == fi.nFileIndexHigh && gOvCache.idxLo == fi.nFileIndexLow
				&& ((fi.nFileIndexHigh | fi.nFileIndexLow) != 0 || SamePath(gOvCache.path, archive))) {
				if ((size_t)overlay >= gOvCache.ents.size() || !MethodHasPayload(gOvCache.ents[overlay].method)) {
					CloseHandle(h);
					return OXZ_CORE_ERR;
				}
				const DirEnt& ce = gOvCache.ents[overlay];
				ent.method = ce.method;
				ent.flags = ce.flags;
				ent.prop = ce.prop;
				ent.crc = ce.crc;
				ent.attr = ce.attr;
				ent.mtime = ce.mtime;
				ent.unpacked = ce.unpacked;
				ent.packed = ce.packed;
				ent.dataOff = ce.dataOff;
				hit = 1;
			}
		}
		if (!hit) {
			std::vector<DirEnt> ents;
			uint64_t off = 0, sz = 0;
			if (!LoadDir(h, len, ents, &off, &sz) || (size_t)overlay >= ents.size() || !MethodHasPayload(ents[overlay].method)) {
				CloseHandle(h);
				return OXZ_CORE_ERR;
			}
			const DirEnt& ce = ents[overlay];
			ent.method = ce.method;
			ent.flags = ce.flags;
			ent.prop = ce.prop;
			ent.crc = ce.crc;
			ent.attr = ce.attr;
			ent.mtime = ce.mtime;
			ent.unpacked = ce.unpacked;
			ent.packed = ce.packed;
			ent.dataOff = ce.dataOff;
			{
				std::lock_guard<std::mutex> lk(gOvCache.mu);
				gOvCache.valid = 1;
				gOvCache.volSerial = fi.dwVolumeSerialNumber;
				gOvCache.idxHi = fi.nFileIndexHigh;
				gOvCache.idxLo = fi.nFileIndexLow;
				gOvCache.fileLen = len;
				gOvCache.mtime = mt;
				gOvCache.path = archive;
				gOvCache.ents.swap(ents);
			}
		}
	} else {
		std::vector<DirEnt> ents;
		uint64_t off = 0, sz = 0;
		if (!LoadDir(h, len, ents, &off, &sz) || (size_t)overlay >= ents.size() || !MethodHasPayload(ents[overlay].method)) {
			CloseHandle(h);
			return OXZ_CORE_ERR;
		}
		ent = ents[overlay];
	}
	if (ent.packed != (uint64_t)(size_t)ent.packed || ent.unpacked != (uint64_t)(size_t)ent.unpacked) {
		CloseHandle(h);
		return OXZ_CORE_ERR;
	}
	pk = (size_t)ent.packed;
	pkBuf = TlsFileBuf(pk ? pk : 1);
	if (!pkBuf) { CloseHandle(h); return OXZ_CORE_ERR; }
	if (pk && !ReadAt(h, ent.dataOff, pkBuf, pk)) { CloseHandle(h); return OXZ_CORE_ERR; }
	CloseHandle(h);
	n = (size_t)ent.unpacked;
	block = malloc(n ? n : 1);
	if (!block) return OXZ_CORE_ERR;
	if (DecodeToBuf(ent, pk ? pkBuf : (const uint8_t*)"", (uint8_t*)block, n) != 1) {
		free(block);
		return OXZ_CORE_ERR;
	}
	*data = block;
	*size = n;
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
	if (!code) {
		std::vector<uint8_t> pcm(8192), back(8192);
		for (i = 0; i < 4096; ++i) {
			int v = (int)((i % 180) * 40 - 3600);
			pcm[i * 2] = (uint8_t)v;
			pcm[i * 2 + 1] = (uint8_t)(v >> 8);
		}
		if (!OxzPcmMaybe(pcm.data(), pcm.size()) || !OxzFlacCompress(pcm.data(), pcm.size(), packed) || !OxzFlacDecompress(packed.data(), packed.size(), back.data(), back.size()) || back != pcm) code = 50;
		else if (!OxzAlacCompress(pcm.data(), pcm.size(), packed) || !OxzAlacDecompress(packed.data(), packed.size(), back.data(), back.size()) || back != pcm) code = 51;
		else if (!OxzTtaCompress(pcm.data(), pcm.size(), packed) || !OxzTtaDecompress(packed.data(), packed.size(), back.data(), back.size()) || back != pcm) code = 52;
	}
	if (!code) {
		const uint32_t stride = 192, rows = 32;
		std::vector<uint8_t> bmp(54 + stride * rows), back;
		memset(bmp.data(), 0, bmp.size());
		bmp[0] = 'B'; bmp[1] = 'M';
		bmp[2] = (uint8_t)bmp.size(); bmp[3] = (uint8_t)(bmp.size() >> 8);
		bmp[10] = 54;
		bmp[14] = 40;
		bmp[18] = 64;
		bmp[22] = (uint8_t)rows;
		bmp[26] = 1;
		bmp[28] = 24;
		for (i = 0; i < stride * rows; ++i) bmp[54 + i] = 40;
		bmp[54] = 41;
		bmp[400] = 9;
		bmp[4000] = 200;
		back.resize(bmp.size());
		if (!OxzPictMaybe(bmp.data(), bmp.size()) || !OxzPictCompress(bmp.data(), bmp.size(), packed) || !OxzPictDecompress(packed.data(), packed.size(), back.data(), back.size()) || back != bmp) code = 53;
	}
	if (!code) {
		const uint32_t frameBytes = 384;
		const int frames = 12;
		std::vector<uint8_t> avi, back, frame(frameBytes);
		auto put = [&](uint32_t v) { avi.push_back((uint8_t)v); avi.push_back((uint8_t)(v >> 8)); avi.push_back((uint8_t)(v >> 16)); avi.push_back((uint8_t)(v >> 24)); };
		auto four = [&](const char* s) { avi.insert(avi.end(), s, s + 4); };
		four("RIFF"); put(0); four("AVI ");
		four("LIST"); put(4 + 8 + 40); four("hdrl");
		four("strf"); put(40);
		put(40); put(16); put(8); avi.push_back(1); avi.push_back(0); avi.push_back(24); avi.push_back(0); put(0); put(frameBytes);
		for (i = 0; i < 16; ++i) avi.push_back(0);
		four("LIST"); put(4 + (uint32_t)frames * (8 + frameBytes)); four("movi");
		for (int f = 0; f < frames; ++f) {
			for (i = 0; i < frame.size(); ++i) frame[i] = 30;
			frame[0] = (uint8_t)(30 + f);
			frame[100] = (uint8_t)(80 + f);
			four("00db"); put(frameBytes); avi.insert(avi.end(), frame.begin(), frame.end());
		}
		{
			uint32_t sz = (uint32_t)(avi.size() - 8);
			avi[4] = (uint8_t)sz; avi[5] = (uint8_t)(sz >> 8); avi[6] = (uint8_t)(sz >> 16); avi[7] = (uint8_t)(sz >> 24);
		}
		back.resize(avi.size());
		if (!OxzVidMaybe(avi.data(), avi.size()) || !OxzVidCompress(avi.data(), avi.size(), packed) || !OxzVidDecompress(packed.data(), packed.size(), back.data(), back.size()) || back != avi) code = 54;
	}
	if (!code) {
		std::vector<uint8_t> raw2(4096), back(4096);
		for (i = 0; i < raw2.size(); ++i) raw2[i] = (uint8_t)((i / 17) * 3);
		if (!OxzLzssCompress(raw2.data(), raw2.size(), packed) || !OxzLzssDecompress(packed.data(), packed.size(), back.data(), back.size()) || back != raw2) code = 55;
	}
	if (!code) {
		/* Ordinary non-audio file (custom binary header + odd alignment + odd length + stereo 16-bit correlated data) */
		std::vector<uint8_t> gen(16385), back(16385);
		uint32_t rng = 0x12345678u;
		int l = 0, r = 0;
		memcpy(gen.data(), "DATBIN\x01\x99\xFE", 9);
		for (i = 0; i + 4 <= gen.size() - 10; i += 4) {
			rng = rng * 1664525u + 1013904223u;
			l = (l * 31) / 32 + (int)((int32_t)rng >> 25);
			rng = rng * 1664525u + 1013904223u;
			r = (r * 31) / 32 + (int)((int32_t)rng >> 25);
			int16_t sl = (int16_t)l;
			int16_t sr = (int16_t)r;
			memcpy(gen.data() + 9 + i, &sl, 2);
			memcpy(gen.data() + 9 + i + 2, &sr, 2);
		}
		gen.back() = 0xA5;
		if (OxzPcmMaybe(gen.data(), gen.size())) code = 56;
		else if (!OxzFlacCompress(gen.data(), gen.size(), packed) || !OxzFlacDecompress(packed.data(), packed.size(), back.data(), back.size()) || back != gen) code = 56;
		else {
			gUserLevel = 6;
			Choice picked = Pick(gen.data(), gen.size(), 0);
			gUserLevel = 1;
			if (picked.method != M_FLAC) code = 57;
			else {
				DirEnt de;
				memset(&de, 0, sizeof(de));
				de.method = (uint16_t)picked.method;
				de.flags = (uint16_t)picked.flags;
				de.prop = picked.prop;
				de.crc = CrcFinish(gen.data(), gen.size());
				de.unpacked = gen.size();
				de.packed = picked.data.size();
				if (DecodeToBuf(de, picked.data.data(), back.data(), back.size()) != 1 || back != gen) code = 58;
			}
		}
	}
	gUserLevel = saved;
	gLevelBytes = 0;
	return code;
}
