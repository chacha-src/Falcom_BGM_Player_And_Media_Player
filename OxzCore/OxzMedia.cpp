#include "OxzMedia.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include <string.h>
#include <stdlib.h>
#include <vector>

static uint32_t U32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t I32(const uint8_t* p)
{
	return (int32_t)U32(p);
}

static uint16_t U16(const uint8_t* p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static void P32(std::vector<uint8_t>& o, uint32_t v)
{
	o.push_back((uint8_t)v);
	o.push_back((uint8_t)(v >> 8));
	o.push_back((uint8_t)(v >> 16));
	o.push_back((uint8_t)(v >> 24));
}

static void P64(std::vector<uint8_t>& o, uint64_t v)
{
	for (int i = 0; i < 8; ++i) o.push_back((uint8_t)(v >> (8 * i)));
}

static int Four(const uint8_t* p, const char* s)
{
	return p[0] == (uint8_t)s[0] && p[1] == (uint8_t)s[1] && p[2] == (uint8_t)s[2] && p[3] == (uint8_t)s[3];
}

static int MediaLevel(void)
{
	int lv = OxzEncodeLevel();
	if (OxzEncodeScout() || lv < 1) return 1;
	if (lv > 6) return 6;
	return lv;
}

static int ZstdPack(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	ZSTD_CCtx* c;
	ZSTD_inBuffer ib;
	ZSTD_outBuffer ob;
	size_t bound, r;
	if (!n) return 0;
	c = ZSTD_createCCtx();
	if (!c) return 0;
	ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, MediaLevel());
	ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, 0);
	bound = ZSTD_compressBound(n);
	try { out.resize(bound); }
	catch (...) { ZSTD_freeCCtx(c); return 0; }
	ib.src = in;
	ib.size = n;
	ib.pos = 0;
	ob.dst = out.data();
	ob.size = out.size();
	ob.pos = 0;
	while (ib.pos < ib.size) {
		r = ZSTD_compressStream2(c, &ob, &ib, ZSTD_e_continue);
		if (ZSTD_isError(r)) { ZSTD_freeCCtx(c); return 0; }
		OxzPulseEncode(ib.pos, n);
	}
	do {
		r = ZSTD_compressStream2(c, &ob, &ib, ZSTD_e_end);
		if (ZSTD_isError(r)) { ZSTD_freeCCtx(c); return 0; }
	} while (r != 0);
	ZSTD_freeCCtx(c);
	try { out.resize(ob.pos); }
	catch (...) { return 0; }
	return out.empty() ? 0 : 1;
}

static int ZstdUnpack(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
	ZSTD_DCtx* d;
	ZSTD_inBuffer in;
	ZSTD_outBuffer out;
	size_t r;
	if (!dstLen) return srcLen == 0;
	d = ZSTD_createDCtx();
	if (!d) return 0;
	in.src = src;
	in.size = srcLen;
	in.pos = 0;
	out.dst = dst;
	out.size = dstLen;
	out.pos = 0;
	do {
		r = ZSTD_decompressStream(d, &out, &in);
		if (ZSTD_isError(r)) { ZSTD_freeDCtx(d); return 0; }
		OxzPulseDecode(out.pos);
	} while (r != 0);
	ZSTD_freeDCtx(d);
	return out.pos == dstLen;
}

struct PictInfo {
	int kind;
	uint32_t pixOff;
	uint32_t stride;
	uint32_t rows;
	uint32_t bpp;
	uint32_t tail;
};

static int PictInfoOf(const uint8_t* p, size_t n, PictInfo* info)
{
	memset(info, 0, sizeof(*info));
	if (n >= 54 && p[0] == 'B' && p[1] == 'M') {
		uint32_t pix = U32(p + 10);
		uint32_t dib = U32(p + 14);
		int32_t w, h;
		uint32_t rows, bpp, stride, pixels;
		if (dib < 40 || (uint64_t)14 + dib > n || pix >= n) return 0;
		w = I32(p + 18);
		h = I32(p + 22);
		if (U16(p + 26) != 1 || U32(p + 30) != 0) return 0;
		if (U16(p + 28) != 24 && U16(p + 28) != 32) return 0;
		if (w <= 0 || w > 20000 || h == 0 || h > 20000 || h < -20000) return 0;
		rows = (uint32_t)(h < 0 ? -h : h);
		bpp = (uint32_t)U16(p + 28) / 8u;
		stride = ((uint32_t)w * (uint32_t)U16(p + 28) + 31u) / 32u * 4u;
		if (stride < (uint32_t)w * bpp) return 0;
		pixels = rows * stride;
		if ((uint64_t)pix + pixels > n) return 0;
		info->kind = 1;
		info->pixOff = pix;
		info->stride = stride;
		info->rows = rows;
		info->bpp = bpp;
		info->tail = (uint32_t)(n - pix - pixels);
		return 1;
	}
	if (n >= 18 && p[1] == 0 && p[2] == 2) {
		uint32_t off, w, h, bpp, stride, pixels;
		w = U16(p + 12);
		h = U16(p + 14);
		bpp = p[16];
		if ((bpp != 24 && bpp != 32) || !w || !h || w > 20000 || h > 20000) return 0;
		off = 18u + p[0];
		if (U16(p + 5) && p[1] == 0) return 0;
		bpp /= 8;
		stride = w * bpp;
		if ((uint64_t)off + (uint64_t)h * stride > n) return 0;
		pixels = h * stride;
		info->kind = 2;
		info->pixOff = off;
		info->stride = stride;
		info->rows = h;
		info->bpp = bpp;
		info->tail = (uint32_t)(n - off - pixels);
		return 1;
	}
	return 0;
}

int OxzPictMaybe(const uint8_t* p, size_t n)
{
	PictInfo info;
	return p && PictInfoOf(p, n, &info);
}

static void FilterSub(uint8_t* row, uint32_t stride, uint32_t bpp)
{
	uint32_t i;
	if (bpp == 0 || bpp > stride) return;
	i = stride;
	while (i > bpp) {
		--i;
		row[i] = (uint8_t)(row[i] - row[i - bpp]);
	}
}

static void UnfilterSub(uint8_t* row, uint32_t stride, uint32_t bpp)
{
	uint32_t i;
	if (bpp == 0 || bpp > stride) return;
	for (i = bpp; i < stride; ++i) row[i] = (uint8_t)(row[i] + row[i - bpp]);
}

int OxzPictCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	PictInfo info;
	std::vector<uint8_t> pix, packed, body;
	uint32_t r, pixels;
	if (!in || !PictInfoOf(in, n, &info)) return 0;
	pixels = info.rows * info.stride;
	try { pix.assign(in + info.pixOff, in + info.pixOff + pixels); }
	catch (...) { return 0; }
	for (r = 0; r < info.rows; ++r) {
		FilterSub(pix.data() + (size_t)r * info.stride, info.stride, info.bpp);
		if ((r & 63u) == 0) OxzPulseEncode(r, info.rows ? info.rows : 1);
	}
	if (!ZstdPack(pix.data(), pix.size(), packed)) return 0;
	try {
		body.clear();
		body.insert(body.end(), (const uint8_t*)"OXZPICT1", (const uint8_t*)"OXZPICT1" + 8);
		P32(body, (uint32_t)info.kind);
		P32(body, info.pixOff);
		P32(body, info.stride);
		P32(body, info.rows);
		P32(body, info.bpp);
		P32(body, info.tail);
		P64(body, pix.size());
		P32(body, (uint32_t)packed.size());
		if (info.pixOff) body.insert(body.end(), in, in + info.pixOff);
		if (info.tail) body.insert(body.end(), in + info.pixOff + pixels, in + n);
		body.insert(body.end(), packed.begin(), packed.end());
	} catch (...) { return 0; }
	if (body.size() >= n) return 0;
	out.swap(body);
	OxzPulseEncode(n, n);
	return 1;
}

int OxzPictDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen)
{
	PictInfo info;
	uint64_t rawN = 0;
	uint32_t comp = 0, head, tail, pixels, r;
	size_t at, compAt;
	if (!in || n < 48 || !dst || memcmp(in, "OXZPICT1", 8) != 0) return 0;
	info.kind = (int)U32(in + 8);
	info.pixOff = U32(in + 12);
	info.stride = U32(in + 16);
	info.rows = U32(in + 20);
	info.bpp = U32(in + 24);
	tail = U32(in + 28);
	for (int i = 0; i < 8; ++i) rawN |= (uint64_t)in[32 + i] << (8 * i);
	comp = U32(in + 40);
	head = info.pixOff;
	if (!info.rows || !info.stride || info.bpp == 0 || info.bpp > info.stride) return 0;
	if ((uint64_t)info.rows * info.stride != rawN) return 0;
	pixels = info.rows * info.stride;
	if ((uint64_t)head + pixels + tail != dstLen) return 0;
	at = 44;
	if (at + head + tail + comp != n) return 0;
	if (head) memcpy(dst, in + at, head);
	at += head;
	if (tail) memcpy(dst + head + pixels, in + at, tail);
	at += tail;
	compAt = at;
	if (!ZstdUnpack(in + compAt, comp, dst + head, pixels)) return 0;
	for (r = 0; r < info.rows; ++r) UnfilterSub(dst + head + (size_t)r * info.stride, info.stride, info.bpp);
	return 1;
}

struct VidHit {
	uint32_t width, height, bits, stride, frameBytes;
	std::vector<uint64_t> off;
};

static void WalkAvi(const uint8_t* p, size_t begin, size_t end, VidHit* hit, int depth)
{
	size_t pos = begin;
	if (depth > 6) return;
	while (pos + 8 <= end) {
		uint32_t sz = U32(p + pos + 4);
		size_t data = pos + 8;
		size_t next;
		if ((uint64_t)sz > end - data) break;
		next = data + sz;
		if ((next & 1) && next < end) next++;
		if (Four(p + pos, "LIST") && sz >= 4) {
			if (!Four(p + data, "movi")) WalkAvi(p, data + 4, data + sz, hit, depth + 1);
		} else if (Four(p + pos, "strf") && sz >= 40 && hit->frameBytes == 0) {
			int32_t w = I32(p + data + 4);
			int32_t h = I32(p + data + 8);
			uint16_t bits = U16(p + data + 14);
			uint32_t comp = U32(p + data + 16);
			uint32_t rows, stride, bytes;
			if (comp == 0 && w > 0 && w <= 8192 && h != 0 && h < 8192 && h > -8192 && (bits == 16 || bits == 24 || bits == 32)) {
				rows = (uint32_t)(h < 0 ? -h : h);
				stride = ((uint32_t)w * bits + 31u) / 32u * 4u;
				bytes = stride * rows;
				if (bytes) {
					hit->width = (uint32_t)w;
					hit->height = rows;
					hit->bits = bits;
					hit->stride = stride;
					hit->frameBytes = bytes;
				}
			}
		}
		pos = next;
	}
}

static int VidFrames(const uint8_t* p, size_t n, VidHit* hit)
{
	size_t pos = 12;
	memset(hit, 0, sizeof(*hit));
	if (n < 32 || !Four(p, "RIFF") || !Four(p + 8, "AVI ")) return 0;
	WalkAvi(p, 12, n, hit, 0);
	if (!hit->frameBytes) return 0;
	while (pos + 8 <= n) {
		uint32_t sz = U32(p + pos + 4);
		size_t data = pos + 8;
		size_t next;
		if ((uint64_t)sz > n - data) break;
		next = data + sz;
		if ((next & 1) && next < n) next++;
		if (Four(p + pos, "LIST") && sz >= 4 && Four(p + data, "movi")) {
			size_t m = data + 4;
			size_t mend = data + sz;
			while (m + 8 <= mend) {
				uint32_t fsz = U32(p + m + 4);
				size_t fd = m + 8;
				size_t fn;
				int id = p[m] >= '0' && p[m] <= '9' && p[m + 1] >= '0' && p[m + 1] <= '9' && p[m + 2] == 'd' && (p[m + 3] == 'b' || p[m + 3] == 'c');
				if ((uint64_t)fsz > mend - fd) return 0;
				fn = fd + fsz;
				if (id) {
					if (fsz != hit->frameBytes) return 0;
					hit->off.push_back(fd);
				}
				if (fn & 1) fn++;
				if (fn <= m) return 0;
				m = fn;
			}
			return hit->off.size() >= 2 ? 1 : 0;
		}
		if (next <= pos) break;
		pos = next;
	}
	return 0;
}

int OxzVidMaybe(const uint8_t* p, size_t n)
{
	VidHit hit;
	return p && VidFrames(p, n, &hit);
}

int OxzVidCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	VidHit hit;
	std::vector<uint8_t> skel, delta, skelC, deltaC, body;
	size_t i, f;
	if (!in || !VidFrames(in, n, &hit)) return 0;
	try {
		skel.assign(in, in + n);
		delta.resize(hit.off.size() * (size_t)hit.frameBytes);
	} catch (...) { return 0; }
	for (f = 0; f < hit.off.size(); ++f) {
		uint64_t at = hit.off[f];
		if (at + hit.frameBytes > n) return 0;
		memcpy(delta.data() + f * hit.frameBytes, in + at, hit.frameBytes);
		memset(skel.data() + at, 0, hit.frameBytes);
		if ((f & 7u) == 0) OxzPulseEncode(f, hit.off.size());
	}
	for (f = hit.off.size(); f-- > 1; ) {
		uint8_t* cur = delta.data() + f * hit.frameBytes;
		uint8_t* prev = cur - hit.frameBytes;
		for (i = 0; i < hit.frameBytes; ++i) cur[i] = (uint8_t)(cur[i] - prev[i]);
	}
	if (!ZstdPack(skel.data(), skel.size(), skelC)) return 0;
	if (!ZstdPack(delta.data(), delta.size(), deltaC)) return 0;
	try {
		body.clear();
		body.insert(body.end(), (const uint8_t*)"OXZVID01", (const uint8_t*)"OXZVID01" + 8);
		P32(body, (uint32_t)hit.off.size());
		P32(body, hit.frameBytes);
		P32(body, (uint32_t)hit.off.size());
		P64(body, skel.size());
		P64(body, delta.size());
		P32(body, (uint32_t)skelC.size());
		P32(body, (uint32_t)deltaC.size());
		for (f = 0; f < hit.off.size(); ++f) {
			P64(body, hit.off[f]);
			P32(body, hit.frameBytes);
			P32(body, 0);
		}
		body.insert(body.end(), skelC.begin(), skelC.end());
		body.insert(body.end(), deltaC.begin(), deltaC.end());
	} catch (...) { return 0; }
	if (body.size() >= n) return 0;
	out.swap(body);
	OxzPulseEncode(n, n);
	return 1;
}

int OxzVidDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen)
{
	uint32_t frames, frameBytes, spans, skelC, deltaC;
	uint64_t skelRaw, deltaRaw;
	size_t at, i, f;
	std::vector<uint64_t> off;
	if (!in || n < 44 || !dst || memcmp(in, "OXZVID01", 8) != 0) return 0;
	frames = U32(in + 8);
	frameBytes = U32(in + 12);
	spans = U32(in + 16);
	skelRaw = 0;
	deltaRaw = 0;
	for (i = 0; i < 8; ++i) skelRaw |= (uint64_t)in[20 + i] << (8 * i);
	for (i = 0; i < 8; ++i) deltaRaw |= (uint64_t)in[28 + i] << (8 * i);
	skelC = U32(in + 36);
	deltaC = U32(in + 40);
	if (!frames || frames != spans || !frameBytes || skelRaw != dstLen) return 0;
	if (deltaRaw != (uint64_t)frames * frameBytes) return 0;
	at = 44;
	if (at + (size_t)spans * 16 + skelC + deltaC != n) return 0;
	try { off.resize(spans); }
	catch (...) { return 0; }
	for (f = 0; f < spans; ++f) {
		uint64_t o = 0;
		for (i = 0; i < 8; ++i) o |= (uint64_t)in[at + i] << (8 * i);
		if (U32(in + at + 8) != frameBytes) return 0;
		if (o + frameBytes > dstLen) return 0;
		off[f] = o;
		at += 16;
	}
	if (!ZstdUnpack(in + at, skelC, dst, dstLen)) return 0;
	at += skelC;
	{
		std::vector<uint8_t> delta;
		try { delta.resize((size_t)deltaRaw); }
		catch (...) { return 0; }
		if (!ZstdUnpack(in + at, deltaC, delta.data(), delta.size())) return 0;
		for (f = 1; f < frames; ++f) {
			uint8_t* cur = delta.data() + f * frameBytes;
			uint8_t* prev = cur - frameBytes;
			for (i = 0; i < frameBytes; ++i) cur[i] = (uint8_t)(cur[i] + prev[i]);
		}
		for (f = 0; f < frames; ++f) memcpy(dst + off[f], delta.data() + f * frameBytes, frameBytes);
	}
	return 1;
}

int OxzLzssCompress(const uint8_t* in, size_t n, std::vector<uint8_t>& out)
{
	std::vector<int> head, prev;
	std::vector<uint8_t> body;
	size_t pos = 0;
	if (!in || n < 32 || n > 64u * 1024u) return 0;
	try {
		head.assign(8192, -1);
		prev.assign(n, -1);
		body.reserve(n);
	} catch (...) { return 0; }
	while (pos < n) {
		int bestLen = 0, bestOff = 0;
		size_t flagAt, bit, take;
		uint8_t flag = 0;
		if (pos + 3 <= n) {
			unsigned h = (((unsigned)in[pos] << 5) ^ ((unsigned)in[pos + 1] << 2) ^ in[pos + 2]) & 8191u;
			int at = head[h];
			int hops = 0;
			while (at >= 0 && hops < 8) {
				int dist = (int)pos - at;
				if (dist > 0 && dist <= 4095) {
					int maxl = (int)(n - pos);
					int len = 0;
					if (maxl > 18) maxl = 18;
					while (len < maxl && in[pos + len] == in[at + len]) len++;
					if (len > bestLen) { bestLen = len; bestOff = dist; }
				}
				at = prev[at];
				hops++;
			}
			prev[pos] = head[h];
			head[h] = (int)pos;
		}
		flagAt = body.size();
		body.push_back(0);
		for (bit = 0; bit < 8 && pos < n; ++bit) {
			if (bit > 0 && pos + 3 <= n) {
				unsigned h = (((unsigned)in[pos] << 5) ^ ((unsigned)in[pos + 1] << 2) ^ in[pos + 2]) & 8191u;
				int at = head[h];
				int hops = 0;
				bestLen = 0;
				bestOff = 0;
				while (at >= 0 && hops < 8) {
					int dist = (int)pos - at;
					if (dist > 0 && dist <= 4095) {
						int maxl = (int)(n - pos);
						int len = 0;
						if (maxl > 18) maxl = 18;
						while (len < maxl && in[pos + len] == in[at + len]) len++;
						if (len > bestLen) { bestLen = len; bestOff = dist; }
					}
					at = prev[at];
					hops++;
				}
				prev[pos] = head[h];
				head[h] = (int)pos;
			} else if (bit > 0) {
				bestLen = 0;
			}
			if (bestLen >= 3) {
				unsigned pack = (unsigned)bestOff | ((unsigned)(bestLen - 3) << 12);
				body.push_back((uint8_t)pack);
				body.push_back((uint8_t)(pack >> 8));
				take = (size_t)bestLen;
			} else {
				flag |= (uint8_t)(1u << bit);
				body.push_back(in[pos]);
				take = 1;
				bestLen = 0;
			}
			pos += take;
			bestLen = 0;
		}
		body[flagAt] = flag;
		if ((pos & 0x3fffu) == 0) OxzPulseEncode(pos, n);
	}
	if (body.size() >= n) return 0;
	out.swap(body);
	OxzPulseEncode(n, n);
	return 1;
}

int OxzLzssDecompress(const uint8_t* in, size_t n, uint8_t* dst, size_t dstLen)
{
	size_t ip = 0, op = 0;
	if (!in || !dst) return 0;
	while (op < dstLen) {
		uint8_t flag;
		int bit;
		if (ip >= n) return 0;
		flag = in[ip++];
		for (bit = 0; bit < 8 && op < dstLen; ++bit) {
			if (flag & (uint8_t)(1u << bit)) {
				if (ip >= n) return 0;
				dst[op++] = in[ip++];
			} else {
				unsigned pack, dist, len, k;
				if (ip + 2 > n) return 0;
				pack = (unsigned)in[ip] | ((unsigned)in[ip + 1] << 8);
				ip += 2;
				dist = pack & 4095u;
				len = (pack >> 12) + 3u;
				if (!dist || dist > op || op + len > dstLen) return 0;
				for (k = 0; k < len; ++k) dst[op + k] = dst[op + k - dist];
				op += len;
			}
		}
	}
	return 1;
}
