#include "OxzFast.h"

#define NOMINMAX
#include <windows.h>
#include <string.h>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include "bzlib.h"

static void Boost(std::thread& th)
{
	SetThreadPriority((HANDLE)th.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
}

static void BzWarm(void)
{
	static std::once_flag once;
	std::call_once(once, []() {
		unsigned char in[32], out[128];
		unsigned int dest = 128;
		int i;
		for (i = 0; i < 32; ++i) in[i] = (unsigned char)i;
		BZ2_bzBuffToBuffCompress((char*)out, &dest, (char*)in, 32, 1, 0, 0);
	});
}

static int BzOne(const uint8_t* in, unsigned n, int level, std::vector<uint8_t>& out)
{
	unsigned int destLen;
	unsigned long long bound;
	int rc;
	if (n == 0) return 0;
	bound = (unsigned long long)n + (unsigned long long)n / 100ull + 600ull;
	if (bound > 0xFFFFFFFFull) return 0;
	destLen = (unsigned int)bound;
	try { out.resize(destLen); }
	catch (...) { return 0; }
	rc = BZ2_bzBuffToBuffCompress((char*)out.data(), &destLen, (char*)in, n, level, 0, 30);
	if (rc != BZ_OK || destLen == 0) return 0;
	out.resize(destLen);
	return 1;
}

static int ReadBits(const uint8_t* p, size_t nBytes, size_t bit, int nb, uint32_t* out)
{
	size_t byte;
	int sh, i;
	uint64_t w = 0;
	uint32_t mask;
	if (nb <= 0 || nb > 32) return 0;
	if (bit + (size_t)nb > nBytes * 8) return 0;
	byte = bit >> 3;
	sh = (int)(bit & 7);
	for (i = 0; i < 8; ++i) {
		w <<= 8;
		if (byte + (size_t)i < nBytes) w |= p[byte + i];
	}
	mask = (nb == 32) ? 0xFFFFFFFFu : ((1u << nb) - 1u);
	*out = (uint32_t)((w >> (64 - sh - nb)) & mask);
	return 1;
}

struct BitW {
	std::vector<uint8_t> b;
	uint64_t acc;
	int live;
	BitW() : acc(0), live(0) {}
	int put(uint32_t v, int n)
	{
		if (n <= 0) return 1;
		if (live + n > 60) return 0;
		acc = (acc << n) | (uint64_t)(n == 32 ? v : (v & ((1u << n) - 1u)));
		live += n;
		while (live >= 8) {
			live -= 8;
			b.push_back((uint8_t)((acc >> live) & 0xFF));
		}
		return 1;
	}
	void finish()
	{
		if (live > 0) {
			b.push_back((uint8_t)((acc << (8 - live)) & 0xFF));
			live = 0;
		}
	}
};

static int CopyBits(BitW& w, const uint8_t* p, size_t nBytes, size_t a, size_t b)
{
	if (a > b) return 0;
	if (w.live == 0 && (a & 7) == 0) {
		size_t whole = (b - a) >> 3;
		size_t src = a >> 3;
		if (src + whole <= nBytes) {
			try { w.b.insert(w.b.end(), p + src, p + src + whole); }
			catch (...) { return 0; }
			a += whole * 8;
		}
	}
	while (a + 24 <= b) {
		uint32_t v = 0;
		if (!ReadBits(p, nBytes, a, 24, &v) || !w.put(v, 24)) return 0;
		a += 24;
	}
	if (a < b) {
		uint32_t v = 0;
		if (!ReadBits(p, nBytes, a, (int)(b - a), &v) || !w.put(v, (int)(b - a))) return 0;
	}
	return 1;
}

static int IsBzHdr(const uint8_t* p, size_t n, size_t i)
{
	static const uint8_t mag[6] = { 0x31, 0x41, 0x59, 0x26, 0x53, 0x59 };
	if (i + 10 > n) return 0;
	if (p[i] != 'B' || p[i + 1] != 'Z' || p[i + 2] != 'h') return 0;
	if (p[i + 3] < '1' || p[i + 3] > '9') return 0;
	return memcmp(p + i + 4, mag, 6) == 0;
}

static void ScanMagics(const uint8_t* p, size_t n, std::vector<size_t>& blocks, std::vector<size_t>& eoss)
{
	size_t i;
	const uint64_t kBlk = 0x314159265359ULL;
	const uint64_t kEos = 0x177245385090ULL;
	if (n < 16) return;
	for (i = 0; i + 8 <= n; ++i) {
		uint64_t w = 0;
		int sh;
		int k;
		for (k = 0; k < 8; ++k) w = (w << 8) | p[i + k];
		for (sh = 0; sh < 8; ++sh) {
			size_t bit = i * 8 + (size_t)sh;
			uint64_t v;
			if (bit + 48 > n * 8) continue;
			v = (w >> (16 - sh)) & 0xFFFFFFFFFFFFULL;
			if (v == kBlk) blocks.push_back(bit);
			else if (v == kEos) eoss.push_back(bit);
		}
	}
}

struct BzStream {
	size_t begin;
	size_t end;
	size_t eosBit;
	int level;
	std::vector<size_t> blocks;
};

static int FindStreams(const uint8_t* p, size_t n, std::vector<BzStream>& out)
{
	std::vector<size_t> blocks, eoss, heads;
	size_t i, h;
	ScanMagics(p, n, blocks, eoss);
	for (i = 0; i + 10 <= n; ++i) if (IsBzHdr(p, n, i)) heads.push_back(i);
	if (heads.empty() || heads[0] != 0) return 0;
	for (h = 0; h < heads.size(); ++h) {
		BzStream s;
		size_t e;
		size_t limit = (h + 1 < heads.size()) ? heads[h + 1] : n;
		int found = 0;
		s.begin = heads[h];
		s.level = p[s.begin + 3] - '0';
		s.end = 0;
		s.eosBit = 0;
		for (e = 0; e < eoss.size(); ++e) {
			size_t endByte;
			if (eoss[e] < s.begin * 8 + 48) continue;
			endByte = (eoss[e] + 80 + 7) / 8;
			if (endByte <= s.begin || endByte > n) continue;
			if (endByte != limit && endByte != n) continue;
			if (endByte < n && !IsBzHdr(p, n, endByte) && endByte != n) continue;
			s.eosBit = eoss[e];
			s.end = endByte;
			found = 1;
			break;
		}
		if (!found) return 0;
		for (e = 0; e < blocks.size(); ++e) {
			if (blocks[e] >= s.begin * 8 + 32 && blocks[e] < s.eosBit) s.blocks.push_back(blocks[e]);
		}
		if (s.blocks.empty() || s.blocks[0] != s.begin * 8 + 32) return 0;
		out.push_back(std::move(s));
	}
	if (out.empty() || out.back().end != n) return 0;
	return 1;
}

static int DecBlock(const uint8_t* src, size_t srcLen, const BzStream& s, size_t blockIndex, std::vector<uint8_t>& plain)
{
	size_t from = s.blocks[blockIndex];
	size_t to = (blockIndex + 1 < s.blocks.size()) ? s.blocks[blockIndex + 1] : s.eosBit;
	uint32_t crc = 0;
	BitW w;
	size_t cap;
	unsigned int destLen;
	int rc;
	if (to <= from) return 0;
	if (!ReadBits(src, srcLen, from + 48, 32, &crc)) return 0;
	if (!w.put('B', 8) || !w.put('Z', 8) || !w.put('h', 8) || !w.put((uint32_t)('0' + s.level), 8)) return 0;
	if (!CopyBits(w, src, srcLen, from, to)) return 0;
	if (!w.put(0x17, 8) || !w.put(0x72, 8) || !w.put(0x45, 8) || !w.put(0x38, 8) || !w.put(0x50, 8) || !w.put(0x90, 8)) return 0;
	if (!w.put(crc, 32)) return 0;
	w.finish();
	if (w.b.empty() || w.b.size() > 0xFFFFFFFFu) return 0;
	cap = (size_t)s.level * 100000u * 2u;
	if (cap < 256u * 1024u) cap = 256u * 1024u;
	for (;;) {
		try { plain.resize(cap); }
		catch (...) { return 0; }
		destLen = (unsigned int)cap;
		rc = BZ2_bzBuffToBuffDecompress((char*)plain.data(), &destLen, (char*)w.b.data(), (unsigned int)w.b.size(), 0, 0);
		if (rc == BZ_OK && destLen > 0) {
			plain.resize(destLen);
			return 1;
		}
		if (rc != BZ_OUTBUFF_FULL || cap >= 32u * 1024u * 1024u) return 0;
		cap *= 2;
	}
}

static int BzConcat(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
	size_t ip = 0, op = 0;
	if (!src || !dst || srcLen == 0 || srcLen > 0xFFFFFFFFull || dstLen > 0xFFFFFFFFull) return 0;
	BzWarm();
	while (ip < srcLen && op < dstLen) {
		bz_stream strm;
		unsigned in0, out0, consumed, produced;
		int rc;
		memset(&strm, 0, sizeof(strm));
		rc = BZ2_bzDecompressInit(&strm, 0, 0);
		if (rc != BZ_OK) return 0;
		strm.next_in = (char*)(src + ip);
		strm.avail_in = (unsigned int)(srcLen - ip);
		strm.next_out = (char*)(dst + op);
		strm.avail_out = (unsigned int)(dstLen - op);
		in0 = strm.avail_in;
		out0 = strm.avail_out;
		do { rc = BZ2_bzDecompress(&strm); } while (rc == BZ_OK);
		consumed = in0 - strm.avail_in;
		produced = out0 - strm.avail_out;
		BZ2_bzDecompressEnd(&strm);
		if (rc != BZ_STREAM_END || consumed == 0) return 0;
		ip += consumed;
		op += produced;
		if (op == dstLen) return 1;
	}
	return op == dstLen && ip == srcLen;
}

static int gBzPar;

extern "C" int OxzBzipLastParallel(void)
{
	return gBzPar;
}

static int DecOneStream(const uint8_t* src, const BzStream& s, std::vector<uint8_t>& plain)
{
	size_t cap = (size_t)s.level * 100000u;
	unsigned int destLen;
	int rc;
	if (s.end <= s.begin || s.end - s.begin > 0xFFFFFFFFu) return 0;
	if (s.blocks.size() > 1) cap = cap * s.blocks.size();
	if (cap < 64) cap = 64;
	try { plain.resize(cap); }
	catch (...) { return 0; }
	destLen = (unsigned int)plain.size();
	rc = BZ2_bzBuffToBuffDecompress((char*)plain.data(), &destLen, (char*)(src + s.begin), (unsigned int)(s.end - s.begin), 0, 0);
	if (rc != BZ_OK || destLen == 0) return 0;
	plain.resize(destLen);
	return 1;
}

static int DecStreamBlocks(const uint8_t* src, size_t srcLen, const BzStream& s, unsigned threads, std::vector<uint8_t>& plain)
{
	std::vector<std::vector<uint8_t>> parts(s.blocks.size());
	std::vector<int> ok(s.blocks.size(), 0);
	std::vector<std::thread> pool;
	std::atomic<size_t> cursor(0);
	unsigned workers = threads;
	unsigned t;
	size_t b, total = 0;
	if (workers > s.blocks.size()) workers = (unsigned)s.blocks.size();
	if (workers < 2) return DecOneStream(src, s, plain);
	for (t = 0; t < workers; ++t) {
		pool.push_back(std::thread([&, srcLen]() {
			for (;;) {
				size_t i = cursor.fetch_add(1);
				if (i >= s.blocks.size()) break;
				try {
					if (DecBlock(src, srcLen, s, i, parts[i])) ok[i] = 1;
				} catch (...) {
					ok[i] = 0;
				}
			}
		}));
		Boost(pool.back());
	}
	for (t = 0; t < pool.size(); ++t) pool[t].join();
	for (b = 0; b < s.blocks.size(); ++b) {
		if (!ok[b]) return 0;
		total += parts[b].size();
	}
	try { plain.resize(total); }
	catch (...) { return 0; }
	total = 0;
	for (b = 0; b < s.blocks.size(); ++b) {
		memcpy(plain.data() + total, parts[b].data(), parts[b].size());
		total += parts[b].size();
	}
	return 1;
}

static int DecParallel(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen, unsigned threads)
{
	std::vector<BzStream> streams;
	size_t outPos = 0;
	if (threads < 2 || !FindStreams(src, srcLen, streams)) return 0;
	BzWarm();
	if (streams.size() == 1 && streams[0].blocks.size() >= 2 && (streams[0].end - streams[0].begin) >= 200u * 1024u) {
		std::vector<uint8_t> plain;
		if (!DecStreamBlocks(src, srcLen, streams[0], threads, plain)) return 0;
		if (plain.size() != dstLen) return 0;
		memcpy(dst, plain.data(), dstLen);
		return 1;
	}
	if (streams.size() >= 2) {
		std::vector<std::vector<uint8_t>> parts(streams.size());
		std::vector<int> ok(streams.size(), 0);
		std::vector<std::thread> pool;
		std::atomic<size_t> cursor(0);
		unsigned workers = threads;
		unsigned t;
		size_t i;
		if (workers > streams.size()) workers = (unsigned)streams.size();
		if (workers < 2) return 0;
		for (t = 0; t < workers; ++t) {
			pool.push_back(std::thread([&]() {
				for (;;) {
					size_t n = cursor.fetch_add(1);
					if (n >= streams.size()) break;
					try {
						if (DecOneStream(src, streams[n], parts[n])) ok[n] = 1;
					} catch (...) {
						ok[n] = 0;
					}
				}
			}));
			Boost(pool.back());
		}
		for (t = 0; t < pool.size(); ++t) pool[t].join();
		for (i = 0; i < streams.size(); ++i) {
			if (!ok[i]) return 0;
			if (outPos + parts[i].size() > dstLen) return 0;
			memcpy(dst + outPos, parts[i].data(), parts[i].size());
			outPos += parts[i].size();
		}
		return outPos == dstLen;
	}
	return 0;
}

extern "C" int OxzBzipDecompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen, unsigned threads)
{
	unsigned int destLen;
	int rc;
	gBzPar = 0;
	if (!src || !dst || srcLen == 0 || dstLen == 0) return 0;
	if (srcLen > 0xFFFFFFFFull || dstLen > 0xFFFFFFFFull) return 0;
	if (threads >= 2 && DecParallel(src, srcLen, dst, dstLen, threads)) {
		gBzPar = 1;
		return 1;
	}
	destLen = (unsigned int)dstLen;
	rc = BZ2_bzBuffToBuffDecompress((char*)dst, &destLen, (char*)src, (unsigned int)srcLen, 0, 0);
	if (rc == BZ_OK && destLen == (unsigned int)dstLen) return 1;
	return BzConcat(src, srcLen, dst, dstLen);
}

int OxzBzipCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out, int level, unsigned threads, OxzPulseFn pulse, void* pulseUser)
{
	unsigned block;
	size_t parts, pi;
	if (!in || n == 0 || n > 0xFFFFFFFFull) return 0;
	if (level < 1) level = 1;
	if (level > 9) level = 9;
	BzWarm();
	block = 100000u * (unsigned)level - 19u;
	if (threads < 2 || n <= block) {
		if (!BzOne(in, (unsigned)n, level, out)) return 0;
		return out.size() < n;
	}
	parts = n / block;
	if (n % block) parts++;
	if (parts < 2) {
		if (!BzOne(in, (unsigned)n, level, out)) return 0;
		return out.size() < n;
	}
	{
		std::vector<std::vector<uint8_t>> chunk(parts);
		std::vector<int> ok(parts, 0);
		std::vector<std::thread> pool;
		std::atomic<size_t> cursor(0);
		std::atomic<unsigned long long> doneBytes(0);
		unsigned workers = threads;
		unsigned t;
		size_t total = 0;
		if (workers > parts) workers = (unsigned)parts;
		for (t = 0; t < workers; ++t) {
			pool.push_back(std::thread([&, n, block, level, parts, pulse, pulseUser]() {
				for (;;) {
					size_t i = cursor.fetch_add(1);
					size_t off, len;
					if (i >= parts) break;
					off = (size_t)block * i;
					len = block;
					if (off >= n) { ok[i] = 0; break; }
					if (len > n - off) len = n - off;
					try {
						if (BzOne(in + off, (unsigned)len, level, chunk[i])) {
							ok[i] = 1;
							if (pulse) pulse(pulseUser, doneBytes.fetch_add(len) + len, n);
						}
					} catch (...) {
						ok[i] = 0;
					}
				}
			}));
			Boost(pool.back());
		}
		for (t = 0; t < pool.size(); ++t) pool[t].join();
		for (pi = 0; pi < parts; ++pi) {
			if (!ok[pi] || chunk[pi].empty()) return 0;
			total += chunk[pi].size();
		}
		if (total == 0 || total >= n || total > 0xFFFFFFFFull) return 0;
		try { out.resize(total); }
		catch (...) { return 0; }
		total = 0;
		for (pi = 0; pi < parts; ++pi) {
			memcpy(out.data() + total, chunk[pi].data(), chunk[pi].size());
			total += chunk[pi].size();
		}
	}
	return 1;
}
