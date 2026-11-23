/* WIM inside OxzGui. Uncompressed, XPRESS, and LZX chunks. No external tool. */
#include "OxzArc.h"
#include "OxzIn.h"
#include "OxzWork.h"
#include "OxzLang.h"
#include <string.h>
#include <vector>
#include <algorithm>

static int Rd(HANDLE h, void* p, DWORD n)
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

static int Sk(HANDLE h, uint64_t off)
{
	LARGE_INTEGER li;
	li.QuadPart = (LONGLONG)off;
	return SetFilePointerEx(h, li, NULL, FILE_BEGIN) ? 1 : 0;
}

static uint16_t U16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t U32(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t U64(const unsigned char* p) { return (uint64_t)U32(p) | ((uint64_t)U32(p + 4) << 32); }

struct WRes {
	uint64_t off;
	uint64_t zsize;
	uint64_t size;
	int flags;
	unsigned char hash[20];
	int hasHash;
};

static int ReadRes(const unsigned char* p, WRes& r)
{
	uint64_t sz = 0;
	int i;
	for (i = 0; i < 7; ++i) sz |= (uint64_t)p[i] << (8 * i);
	r.flags = p[7];
	r.zsize = sz;
	r.off = U64(p + 8);
	r.size = U64(p + 16);
	r.hasHash = 0;
	memset(r.hash, 0, 20);
	return 1;
}

enum { WIM_COMPRESSED = 4, WIM_METADATA = 2, WIM_XPRESS = 0x20000, WIM_LZX = 0x40000, WIM_LZMS = 0x80000 };

struct BitR {
	const unsigned char* p;
	const unsigned char* end;
	uint32_t buf;
	int n;
	void pull()
	{
		while (n <= 16 && p + 1 < end) {
			uint32_t w = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
			p += 2;
			buf |= w << n;
			n += 16;
		}
	}
	uint32_t get(int k)
	{
		if (k <= 0) return 0;
		pull();
		uint32_t v = buf & ((k == 32) ? 0xffffffffu : ((1u << k) - 1));
		buf >>= k;
		n -= k;
		if (n < 0) n = 0;
		return v;
	}
};

static int BuildHuf(const unsigned char* lens, int nsym, std::vector<uint16_t>& sym, std::vector<unsigned char>& ln, int bits)
{
	int count[17];
	uint16_t nextc[17];
	int i, L;
	uint32_t code, size;
	memset(count, 0, sizeof(count));
	for (i = 0; i < nsym; ++i) {
		if (lens[i] > 16) return 0;
		if (lens[i]) count[lens[i]]++;
	}
	code = 0;
	for (L = 1; L <= 16; ++L) {
		code = (code + (uint32_t)count[L - 1]) << 1;
		nextc[L] = (uint16_t)code;
	}
	size = 1u << bits;
	sym.assign(size, 0xffff);
	ln.assign(size, 0);
	for (i = 0; i < nsym; ++i) {
		int L = lens[i];
		uint32_t c, step, base, k;
		if (!L || L > bits) continue;
		c = nextc[L]++;
		step = 1u << (bits - L);
		base = c << (bits - L);
		if (base >= size) return 0;
		for (k = 0; k < step && base + k < size; ++k) {
			sym[base + k] = (uint16_t)i;
			ln[base + k] = (unsigned char)L;
		}
	}
	return 1;
}

static int HufGet(BitR& b, const std::vector<uint16_t>& sym, const std::vector<unsigned char>& ln, int bits, int* out)
{
	uint32_t peek, idx;
	int L;
	b.pull();
	if ((int)b.n < 1) return 0;
	peek = b.buf;
	idx = peek & ((1u << bits) - 1);
	if (idx >= sym.size()) return 0;
	L = ln[idx];
	if (!L || sym[idx] == 0xffff) return 0;
	b.buf >>= L;
	b.n -= L;
	*out = sym[idx];
	return 1;
}

static int XpressNorm(const unsigned char*& in, const unsigned char* lim, uint32_t& value, unsigned& bitPos)
{
	if (bitPos > 16) {
		if (in >= lim) return 0;
		bitPos -= 16;
		value |= (uint32_t)U16(in) << bitPos;
		in += 2;
	}
	return 1;
}

static int Xpress(const unsigned char* in0, size_t inN, unsigned char* out, size_t outN)
{
	unsigned char lens[512];
	std::vector<uint16_t> sym;
	std::vector<unsigned char> ln;
	const unsigned char* in;
	const unsigned char* lim;
	uint32_t value;
	unsigned bitPos;
	size_t o = 0;
	int i;
	if (inN < 260) return 0;
	for (i = 0; i < 256; ++i) {
		lens[i * 2] = (unsigned char)(in0[i] & 15);
		lens[i * 2 + 1] = (unsigned char)(in0[i] >> 4);
	}
	if (!BuildHuf(lens, 512, sym, ln, 15)) return 0;
	in = in0 + 256;
	lim = in0 + inN - 1;
	value = U32(in);
	value = (value << 16) | (value >> 16);
	in += 4;
	bitPos = 0;
	while (o < outN) {
		uint32_t peek;
		int L, s, offBits, len;
		uint32_t offset;
		size_t from, k;
		if (bitPos > 16 && !XpressNorm(in, lim, value, bitPos)) return 0;
		peek = value >> (32 - 15);
		if (peek >= ln.size() || !ln[peek] || sym[peek] == 0xffff) return 0;
		L = ln[peek];
		s = sym[peek];
		bitPos += (unsigned)L;
		value <<= L;
		if (!XpressNorm(in, lim, value, bitPos)) return 0;
		if (s < 256) {
			out[o++] = (unsigned char)s;
			continue;
		}
		offBits = (s - 256) >> 4;
		len = (s - 256) & 15;
		if (len == 15) {
			int add;
			if (in > lim) return 0;
			add = *in++;
			if (add == 255) {
				if (in >= lim) return 0;
				add = (int)U16(in);
				in += 2;
			} else add += 15;
			len += add;
		}
		len += 3;
		if (o + (size_t)len > outN) return 0;
		if (offBits == 0) offset = 1;
		else {
			uint32_t extra = value >> (32 - offBits);
			bitPos += (unsigned)offBits;
			value <<= offBits;
			if (!XpressNorm(in, lim, value, bitPos)) return 0;
			offset = (1u << offBits) + extra;
		}
		if (!offset || offset > o) return 0;
		from = o - offset;
		for (k = 0; k < (size_t)len; ++k) out[o++] = out[from + k];
	}
	return 1;
}

static const uint32_t kSlotBase[51] = {
	0, 0, 0, 1, 2, 4, 6, 10, 14, 22, 30, 46, 62, 94, 126, 190, 254, 382, 510,
	766, 1022, 1534, 2046, 3070, 4094, 6142, 8190, 12286, 16382, 24574, 32766,
	49150, 65534, 98302, 131070, 196606, 262142, 393214, 524286, 655358, 786430,
	917502, 1048574, 1179646, 1310718, 1441790, 1572862, 1703934, 1835006, 1966078, 0
};
static const unsigned char kSlotBits[51] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
	14, 14, 15, 15, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 0
};

struct LzxH {
	std::vector<uint16_t> sym;
	std::vector<unsigned char> ln;
};

static int LzxBuild(const unsigned char* lens, int n, LzxH& h)
{
	return BuildHuf(lens, n, h.sym, h.ln, 16);
}

/* 7-Zip's WIM LZX reader takes the next bit from bit 15 of each little-endian uint16. */
struct LzxBit {
	const unsigned char* base;
	size_t len;
	int bitpos;
	uint32_t get(int k)
	{
		uint32_t v = 0;
		int i;
		for (i = 0; i < k; ++i) {
			int boff = (bitpos >> 4) * 2;
			unsigned w;
			int bit;
			if (boff < 0 || (size_t)boff + 1 >= len) { bitpos++; v <<= 1; continue; }
			w = (unsigned)base[boff] | ((unsigned)base[boff + 1] << 8);
			bit = 15 - (bitpos & 15);
			v = (v << 1) | ((w >> bit) & 1u);
			bitpos++;
		}
		return v;
	}
	uint32_t peek16()
	{
		int save = bitpos;
		uint32_t v = get(16);
		bitpos = save;
		return v;
	}
	const unsigned char* aligned()
	{
		int a = (bitpos + 15) & ~15;
		return base + (a >> 3);
	}
};

static int LzxSym(LzxBit& b, const LzxH& h, int* o)
{
	uint32_t peek;
	int L;
	if (h.sym.empty() || h.ln.empty()) return 0;
	peek = b.peek16();
	if (peek >= h.sym.size()) return 0;
	L = h.ln[peek];
	if (!L) return 0;
	b.get(L);
	*o = h.sym[peek];
	return 1;
}

static int LzxDelta(int prev, int s)
{
	int L = prev - s;
	if (L < 0) L += 17;
	return L;
}

static int LzxReadLens(LzxBit& b, unsigned char* lens, int n)
{
	unsigned char pre[20];
	LzxH ph;
	int i;
	memset(lens, 0, (size_t)n);
	for (i = 0; i < 20; ++i) pre[i] = (unsigned char)b.get(4);
	if (!LzxBuild(pre, 20, ph)) return 0;
	i = 0;
	while (i < n) {
		int s, L, run, k;
		if (!LzxSym(b, ph, &s)) return 0;
		if (s <= 16) {
			lens[i] = (unsigned char)LzxDelta(lens[i], s);
			i++;
		} else if (s == 17) {
			run = (int)b.get(4) + 4;
			if (i + run > n) return 0;
			for (k = 0; k < run; ++k) lens[i++] = 0;
		} else if (s == 18) {
			run = (int)b.get(5) + 20;
			if (i + run > n) return 0;
			for (k = 0; k < run; ++k) lens[i++] = 0;
		} else if (s == 19) {
			run = (int)b.get(1) + 4;
			if (!LzxSym(b, ph, &s) || s > 16) return 0;
			L = LzxDelta(lens[i], s);
			if (i + run > n) return 0;
			for (k = 0; k < run; ++k) lens[i++] = (unsigned char)L;
		} else return 0;
	}
	return 1;
}

static void LzxE8(unsigned char* data, size_t size);

static int LzxChunk(const unsigned char* in, size_t inN, unsigned char* out, size_t outN, uint32_t window)
{
	LzxBit b;
	uint32_t R0 = 1, R1 = 1, R2 = 1;
	size_t o = 0;
	int slots = 30;
	int mainN;
	unsigned dict = 15;
	uint32_t win = window ? window : 32768;
	b.base = in;
	b.len = inN;
	b.bitpos = 0;
	while ((1u << dict) < win && dict < 21) dict++;
	if (win <= 32768) slots = 30;
	else if (win <= 65536) slots = 32;
	else if (win <= 131072) slots = 34;
	else if (win <= 262144) slots = 36;
	else if (win <= 524288) slots = 38;
	else if (win <= 1048576) slots = 42;
	else slots = 50;
	mainN = 256 + slots * 8;
	while (o < outN) {
		int type = (int)b.get(3);
		int sizedef = (int)b.get(1);
		uint32_t block;
		if (sizedef) block = 32768;
		else {
			block = b.get(16);
			if (dict >= 16) block = (block << 8) | b.get(8);
		}
		unsigned char* mlens = NULL;
		unsigned char* llens = NULL;
		LzxH mainH, lenH, alH;
		int haveAl = 0;
		size_t blockEnd;
		if (type == 3) {
			const unsigned char* p;
			uint32_t n0, n1, n2;
			size_t left;
			if (b.bitpos & 15) b.bitpos += 16 - (b.bitpos & 15);
			p = b.base + (b.bitpos >> 3);
			if (p + 12 > b.base + b.len) return 0;
			n0 = U32(p); n1 = U32(p + 4); n2 = U32(p + 8);
			p += 12;
			b.bitpos += 12 * 8;
			R0 = n0 ? n0 : 1; R1 = n1 ? n1 : 1; R2 = n2 ? n2 : 1;
			left = block;
			if (o + left > outN) left = outN - o;
			if (p + left > b.base + b.len) return 0;
			memcpy(out + o, p, left);
			b.bitpos += (int)block * 8;
			o += left;
			continue;
		}
		if (type != 1 && type != 2) return 0;
		mlens = (unsigned char*)malloc((size_t)mainN);
		llens = (unsigned char*)malloc(249);
		if (!mlens || !llens) { free(mlens); free(llens); return 0; }
		memset(mlens, 0, (size_t)mainN);
		memset(llens, 0, 249);
		if (type == 2) {
			unsigned char al[8];
			int i;
			for (i = 0; i < 8; ++i) al[i] = (unsigned char)b.get(3);
			if (!LzxBuild(al, 8, alH)) { free(mlens); free(llens); return 0; }
			haveAl = 1;
		}
		if (!LzxReadLens(b, mlens, 256) || !LzxReadLens(b, mlens + 256, slots * 8) || !LzxReadLens(b, llens, 249) || !LzxBuild(mlens, mainN, mainH) || !LzxBuild(llens, 249, lenH)) {
			free(mlens); free(llens); return 0;
		}
		free(mlens); free(llens);
		blockEnd = o + block;
		if (blockEnd > outN) blockEnd = outN;
		while (o < blockEnd) {
			int sym;
			if (!LzxSym(b, mainH, &sym)) return 0;
			if (sym < 256) {
				out[o++] = (unsigned char)sym;
				continue;
			}
			{
				int lenHead = (sym - 256) & 7;
				int slot = (sym - 256) >> 3;
				int length = lenHead + 2;
				uint32_t offset;
				size_t from, k;
				if (lenHead == 7) {
					int extra;
					if (!LzxSym(b, lenH, &extra)) return 0;
					length += extra;
				}
				if (slot < 0 || slot >= slots) return 0;
				if (slot == 0) offset = R0;
				else if (slot == 1) { offset = R1; R1 = R0; R0 = offset; }
				else if (slot == 2) { offset = R2; R2 = R0; R0 = offset; }
				else {
					int eb = kSlotBits[slot];
					uint32_t add = 0;
					if (haveAl && eb >= 3) {
						int al;
						add = b.get(eb - 3) << 3;
						if (!LzxSym(b, alH, &al)) return 0;
						add |= (uint32_t)al;
					} else if (eb) add = b.get(eb);
					offset = kSlotBase[slot] + add;
					if (offset == 0) return 0;
					R2 = R1; R1 = R0; R0 = offset;
				}
				if (!offset || offset > o || o + (size_t)length > outN) return 0;
				from = o - offset;
				for (k = 0; k < (size_t)length; ++k) out[o++] = out[from + k];
			}
		}
	}
	LzxE8(out, outN);
	return 1;
}

static void LzxE8(unsigned char* data, size_t size)
{
	const uint32_t translation = 12000000u;
	size_t i;
	if (size <= 10) return;
	for (i = 0; i + 10 < size; ) {
		uint32_t v, pos;
		if (data[i] != 0xE8) { i++; continue; }
		v = (uint32_t)data[i + 1] | ((uint32_t)data[i + 2] << 8) | ((uint32_t)data[i + 3] << 16) | ((uint32_t)data[i + 4] << 24);
		pos = 0u - 1u - (uint32_t)i;
		if (v < translation) v += pos + 1;
		else if (v > pos) v += translation;
		data[i + 1] = (unsigned char)v;
		data[i + 2] = (unsigned char)(v >> 8);
		data[i + 3] = (unsigned char)(v >> 16);
		data[i + 4] = (unsigned char)(v >> 24);
		i += 5;
	}
}

static int DecompChunk(const unsigned char* in, size_t inN, unsigned char* out, size_t outN, int codec, uint32_t window)
{
	if (codec == 0) {
		if (inN < outN) return 0;
		memcpy(out, in, outN);
		return 1;
	}
	if (codec == 1) return Xpress(in, inN, out, outN);
	if (codec == 2) return LzxChunk(in, inN, out, outN, window);
	return 0;
}

static int DecompRes(HANDLE h, const WRes& r, std::vector<unsigned char>& out, int codec, uint32_t chunk)
{
	out.clear();
	if (!r.size) return 1;
	if (r.size > (uint64_t)512 << 20 && r.size > (uint64_t)chunk * 8) {
		/* still allowed; allocate the real size when it fits */
	}
	if (r.size > ((uint64_t)1 << 32)) return 0;
	try { out.resize((size_t)r.size); }
	catch (...) { return 0; }
	if (!(r.flags & WIM_COMPRESSED) || codec == 0) {
		if (!Sk(h, r.off) || !Rd(h, out.data(), (DWORD)((r.zsize < r.size) ? r.zsize : r.size))) return 0;
		if (r.zsize < r.size) memset(out.data() + (size_t)r.zsize, 0, (size_t)(r.size - r.zsize));
		OxzPulseAdd(r.size);
		return 1;
	}
	{
		uint32_t cs = chunk ? chunk : 32768;
		uint64_t nch = (r.size + cs - 1) / cs;
		std::vector<unsigned char> packed;
		std::vector<uint32_t> ends;
		size_t table = (nch > 1) ? (size_t)(nch - 1) * 4 : 0;
		uint64_t prev = 0;
		uint64_t i;
		if (r.zsize < table) return 0;
		try { packed.resize((size_t)r.zsize); }
		catch (...) { return 0; }
		if (!Sk(h, r.off) || !Rd(h, packed.data(), (DWORD)r.zsize)) return 0;
		ends.resize((size_t)nch);
		for (i = 0; i + 1 < nch; ++i) ends[(size_t)i] = U32(packed.data() + i * 4);
		ends[(size_t)nch - 1] = (uint32_t)(r.zsize - table);
		for (i = 0; i < nch; ++i) {
			uint32_t end = ends[(size_t)i];
			uint32_t sz = (uint32_t)(end - prev);
			size_t outOff = (size_t)i * cs;
			size_t want = (size_t)std::min<uint64_t>(cs, r.size - outOff);
			const unsigned char* src = packed.data() + table + prev;
			if ((uint64_t)table + end > r.zsize) return 0;
			if (!DecompChunk(src, sz, out.data() + outOff, want, codec, cs)) return 0;
			OxzPulseAdd(want);
			prev = end;
		}
	}
	return 1;
}

static std::wstring WName(const unsigned char* p, int nbytes)
{
	int n = nbytes / 2;
	std::wstring w;
	int i;
	if (n < 0) n = 0;
	w.resize(n);
	for (i = 0; i < n; ++i) w[i] = (wchar_t)(p[i * 2] | (p[i * 2 + 1] << 8));
	while (!w.empty() && (w.back() == 0 || w.back() == L'\\' || w.back() == L'/')) w.pop_back();
	for (i = 0; i < (int)w.size(); ++i) if (w[i] == L'/') w[i] = L'\\';
	return w;
}

static int SafeRelW(const wchar_t* name, std::wstring& rel)
{
	rel.clear();
	if (!name || !name[0]) return 0;
	if (name[0] == L'\\' || name[0] == L'/') return 0;
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

struct WFile {
	std::wstring rel;
	int dir;
	unsigned char hash[20];
	int hasHash;
	uint64_t mtime;
};

static void WalkDentry(const std::vector<unsigned char>& meta, size_t pos, const std::wstring& parent, std::vector<WFile>& files, int depth)
{
	if (depth > 64 || pos + 102 > meta.size()) return;
	for (;;) {
		if (pos + 102 > meta.size()) return;
		const unsigned char* e = meta.data() + pos;
		uint64_t len = U64(e);
		uint32_t attr;
		uint64_t sub;
		uint16_t nameN, shortN;
		std::wstring name, full;
		WFile f;
		size_t nameAt;
		if (len == 0) return;
		if (len < 102 || pos + len > meta.size()) return;
		attr = U32(e + 8);
		sub = U64(e + 16);
		nameN = U16(e + 100);
		shortN = U16(e + 98);
		nameAt = 102;
		if (nameAt + nameN + shortN > (size_t)len) {
			nameN = U16(e + 94);
			shortN = U16(e + 92);
			nameAt = 96;
			if (nameAt + nameN + shortN > (size_t)len) { pos += (size_t)((len + 7) & ~7ull); continue; }
		}
		name = WName(e + nameAt, nameN);
		full = parent.empty() || name.empty() ? (name.empty() ? parent : name) : (parent + L"\\" + name);
		f.rel.clear();
		f.dir = 0;
		f.hasHash = 0;
		f.mtime = U64(e + 56);
		f.dir = (attr & 0x10) ? 1 : 0;
		memcpy(f.hash, e + 64, 20);
		f.hasHash = 0;
		for (int i = 0; i < 20; ++i) if (f.hash[i]) f.hasHash = 1;
		if (!name.empty()) {
			if (SafeRelW(full.c_str(), f.rel)) files.push_back(f);
		}
		if (f.dir && sub && sub != pos) WalkDentry(meta, (size_t)sub, name.empty() ? parent : full, files, depth + 1);
		pos += (size_t)((len + 7) & ~7ull);
		if (pos >= meta.size()) return;
	}
}

static const WRes* ByHash(const std::vector<WRes>& tab, const unsigned char* hash)
{
	for (size_t i = 0; i < tab.size(); ++i)
		if (tab[i].hasHash && memcmp(tab[i].hash, hash, 20) == 0) return &tab[i];
	return NULL;
}

static void CodecName(int codec, std::wstring& s)
{
	if (codec == 0) s = L"Store";
	else if (codec == 1) s = L"XPRESS";
	else if (codec == 2) s = L"LZX";
	else if (codec == 3) s = L"LZMS";
	else s = L"WIM";
}

int OxzWimList(const wchar_t* path, std::vector<OxzEnt>& out, std::wstring& err)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	unsigned char hdr[208];
	uint32_t flags, chunk, images;
	WRes tableR, xmlR, bootR;
	int codec = 0;
	std::vector<WRes> table;
	std::vector<WFile> files;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (!Rd(h, hdr, 208) || memcmp(hdr, "MSWIM", 5) != 0) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	flags = U32(hdr + 16);
	chunk = U32(hdr + 20);
	images = U32(hdr + 44);
	ReadRes(hdr + 48, tableR);
	ReadRes(hdr + 72, xmlR);
	ReadRes(hdr + 96, bootR);
	(void)images;
	(void)xmlR;
	if (flags & WIM_LZMS) codec = 3;
	else if (flags & WIM_LZX) codec = 2;
	else if (flags & WIM_XPRESS) codec = 1;
	else if (flags & 2) codec = 1;
	if (codec == 3) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	{
		std::vector<unsigned char> raw;
		if (!DecompRes(h, tableR, raw, codec, chunk ? chunk : 32768)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		size_t n = raw.size() / 50;
		table.resize(n);
		for (size_t i = 0; i < n; ++i) {
			ReadRes(raw.data() + i * 50, table[i]);
			memcpy(table[i].hash, raw.data() + i * 50 + 30, 20);
			table[i].hasHash = 1;
		}
	}
	{
		int saw = 0;
		for (size_t i = 0; i < table.size(); ++i) {
			std::vector<unsigned char> meta;
			if (!(table[i].flags & WIM_METADATA)) continue;
			if (!DecompRes(h, table[i], meta, codec, chunk ? chunk : 32768)) continue;
			size_t pos = 0;
			if (meta.size() >= 4) {
				uint32_t sl = U32(meta.data());
				if (sl >= 4 && sl < meta.size()) pos = (sl + 7u) & ~7u;
			}
			WalkDentry(meta, pos, L"", files, 0);
			saw = 1;
		}
		if (!saw && (bootR.flags & WIM_METADATA || bootR.size)) {
			std::vector<unsigned char> meta;
			if (DecompRes(h, bootR, meta, codec, chunk ? chunk : 32768) && meta.size() >= 8) {
				uint32_t sl = U32(meta.data());
				size_t pos = (sl >= 4 && sl < meta.size()) ? ((sl + 7u) & ~7u) : 0;
				WalkDentry(meta, pos, L"", files, 0);
			}
		}
	}
	CloseHandle(h);
	for (size_t i = 0; i < files.size(); ++i) {
		OxzEnt e;
		const WRes* rr;
		e.rel = files[i].rel;
		e.origin = files[i].rel;
		e.dir = files[i].dir;
		e.index = (int)i;
		e.kind = 11;
		e.crcBits = 0;
		e.crc = 0;
		e.mtime = files[i].mtime;
		e.offset = 0;
		e.size = 0;
		e.packed = 0;
		e.packedKnown = 0;
		CodecName(codec, e.codec);
		rr = files[i].hasHash ? ByHash(table, files[i].hash) : NULL;
		if (rr) {
			e.size = rr->size;
			e.packed = rr->zsize;
			e.packedKnown = (rr->flags & WIM_COMPRESSED) ? 1 : 1;
			e.offset = rr->off;
		}
		out.push_back(e);
	}
	if (out.empty()) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	return 0;
}

static void EnsureParentW(const std::wstring& path)
{
	for (size_t i = 3; i < path.size(); ++i) {
		if (path[i] == L'\\' || path[i] == L'/') CreateDirectoryW(path.substr(0, i).c_str(), NULL);
	}
	size_t s = path.find_last_of(L"\\/");
	if (s != std::wstring::npos) CreateDirectoryW(path.substr(0, s).c_str(), NULL);
}

int OxzWimTake(const wchar_t* arc, const wchar_t* dest, const std::vector<OxzEnt>& ents, std::wstring& err, int base, int span)
{
	HANDLE h = CreateFileW(arc, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	unsigned char hdr[208];
	uint32_t flags, chunk;
	WRes tableR;
	int codec = 0;
	std::vector<WRes> table;
	std::vector<WFile> files;
	int nfile = 0, done = 0;
	if (h == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); return 2; }
	if (!Rd(h, hdr, 208)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	flags = U32(hdr + 16);
	chunk = U32(hdr + 20);
	ReadRes(hdr + 48, tableR);
	if (flags & WIM_LZX) codec = 2;
	else if (flags & WIM_XPRESS || (flags & 2)) codec = 1;
	if (flags & WIM_LZMS) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
	{
		std::vector<unsigned char> raw;
		if (!DecompRes(h, tableR, raw, codec, chunk ? chunk : 32768)) { CloseHandle(h); err = OxzTxt(OS_OPEN_FAIL); return 2; }
		size_t n = raw.size() / 50;
		table.resize(n);
		for (size_t i = 0; i < n; ++i) {
			ReadRes(raw.data() + i * 50, table[i]);
			memcpy(table[i].hash, raw.data() + i * 50 + 30, 20);
			table[i].hasHash = 1;
		}
	}
	for (size_t i = 0; i < table.size(); ++i) {
		std::vector<unsigned char> meta;
		if (!(table[i].flags & WIM_METADATA)) continue;
		if (!DecompRes(h, table[i], meta, codec, chunk ? chunk : 32768)) continue;
		uint32_t sl = meta.size() >= 4 ? U32(meta.data()) : 0;
		size_t pos = (sl >= 4 && sl < meta.size()) ? ((sl + 7u) & ~7u) : 0;
		WalkDentry(meta, pos, L"", files, 0);
	}
	for (size_t i = 0; i < ents.size(); ++i) if (!ents[i].dir) nfile++;
	if (nfile < 1) nfile = 1;
	CreateDirectoryW(dest, NULL);
	for (size_t i = 0; i < ents.size(); ++i) {
		std::wstring full = std::wstring(dest) + L"\\" + ents[i].rel;
		const WFile* wf = NULL;
		if (ents[i].dir) { EnsureParentW(full + L"\\x"); CreateDirectoryW(full.c_str(), NULL); continue; }
		for (size_t k = 0; k < files.size(); ++k)
			if (_wcsicmp(files[k].rel.c_str(), ents[i].origin.c_str()) == 0) { wf = &files[k]; break; }
		EnsureParentW(full);
		{
			std::vector<unsigned char> data;
			const WRes* rr = (wf && wf->hasHash) ? ByHash(table, wf->hash) : NULL;
			HANDLE o;
			DWORD wr = 0;
			OxzNoteA(done, nfile, ents[i].rel.c_str(), base, span);
			OxzPulseSize(rr ? rr->size : 0);
			done++;
			if (!rr) { err = OxzTxt(OS_OPEN_FAIL); CloseHandle(h); return 2; }
			if (!DecompRes(h, *rr, data, codec, chunk ? chunk : 32768)) { err = OxzTxt(OS_OPEN_FAIL); CloseHandle(h); return 2; }
			o = CreateFileW(full.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (o == INVALID_HANDLE_VALUE) { err = OxzTxt(OS_OPEN_FAIL); CloseHandle(h); return 2; }
			if (!data.empty() && (!WriteFile(o, data.data(), (DWORD)data.size(), &wr, NULL) || wr != data.size())) {
				CloseHandle(o); err = OxzTxt(OS_OPEN_FAIL); CloseHandle(h); return 2;
			}
			CloseHandle(o);
		}
	}
	CloseHandle(h);
	return 0;
}
