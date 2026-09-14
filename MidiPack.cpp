#include "stdafx.h"
#include "MidiPack.h"
#include "ComposerConvert.h"
#include "CEmu/cemu_mgr.h"
#include <vector>
#include <wininet.h>
#include <shlobj.h>

#define NOUNCRYPT
#define USEWIN32IOAPI
#include "minizip/unzip.h"
#include "minizip/iowin32.h"

#pragma comment(lib, "wininet.lib")

namespace {

enum { kMaxFile = 32 * 1024 * 1024, kMaxMembers = 2048 };

static int CiEq(wchar_t a, wchar_t b)
{
	if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
	if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
	if (a == L'/') a = L'\\';
	if (b == L'/') b = L'\\';
	return a == b;
}

static int EqExtW(const wchar_t* path, const wchar_t* ext)
{
	if (!path || !ext) return 0;
	const wchar_t* dot = wcsrchr(path, L'.');
	if (!dot) return 0;
	while (*dot && *ext) {
		if (!CiEq(*dot++, *ext++)) return 0;
	}
	return *dot == 0 && *ext == 0;
}

static const wchar_t* BaseNameW(const wchar_t* p)
{
	const wchar_t* b = p ? p : L"";
	for (const wchar_t* s = b; *s; ++s) {
		if (*s == L'\\' || *s == L'/' || *s == L'>')
			b = s + 1;
	}
	return b;
}

static void SlashNorm(wchar_t* s)
{
	if (!s) return;
	for (; *s; ++s) {
		if (*s == L'/') *s = L'\\';
	}
}

static void StemCopy(const wchar_t* name, wchar_t* stem, int stemChars)
{
	if (!stem || stemChars <= 0) return;
	stem[0] = 0;
	const wchar_t* b = BaseNameW(name);
	wcsncpy_s(stem, stemChars, b, _TRUNCATE);
	wchar_t* d = wcsrchr(stem, L'.');
	if (d && d != stem) *d = 0;
}

static int SameStem(const wchar_t* a, const wchar_t* b)
{
	wchar_t sa[260], sb[260];
	StemCopy(a, sa, 260);
	StemCopy(b, sb, 260);
	if (!sa[0] || !sb[0]) return 0;
	const wchar_t* pa = sa;
	const wchar_t* pb = sb;
	while (*pa && *pb) {
		if (!CiEq(*pa++, *pb++)) return 0;
	}
	return *pa == 0 && *pb == 0;
}

static int IsWantedSidecar(const wchar_t* path)
{
	return EqExtW(path, L".wrd") || EqExtW(path, L".mag") || EqExtW(path, L".lrc")
		|| EqExtW(path, L".gsd") || EqExtW(path, L".cm6") || EqExtW(path, L".mki");
}

static int IsSeqPath(const wchar_t* path)
{
	return EqExtW(path, L".mid") || EqExtW(path, L".midi") || EqExtW(path, L".kar")
		|| EqExtW(path, L".rmi") || EqExtW(path, L".rcp") || EqExtW(path, L".r36")
		|| EqExtW(path, L".g36") || EqExtW(path, L".g18") || EqExtW(path, L".mcp")
		|| EqExtW(path, L".mtd") || EqExtW(path, L".mff") || EqExtW(path, L".seq");
}

static int IsJunkName(const wchar_t* n)
{
	if (!n || !n[0]) return 1;
	if (n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0)))
		return 1;
	const wchar_t* b = BaseNameW(n);
	if (_wcsicmp(b, L"thumbs.db") == 0 || _wcsicmp(b, L"desktop.ini") == 0)
		return 1;
	if (wcsstr(n, L"__MACOSX")) return 1;
	return 0;
}

static int ArcKind(const wchar_t* path)
{
	if (EqExtW(path, L".zip")) return 1;
	if (EqExtW(path, L".lzh") || EqExtW(path, L".lha") || EqExtW(path, L".lzs")
		|| EqExtW(path, L".pma")) return 2;
	if (EqExtW(path, L".7z") || EqExtW(path, L".cab")) return 3;
	return 0;
}

static unsigned Le16(const unsigned char* p) { return p[0] | ((unsigned)p[1] << 8); }
static unsigned Le32(const unsigned char* p)
{
	return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int MbToW(const char* src, int srclen, UINT cp, wchar_t* dst, int dstChars)
{
	if (!dst || dstChars <= 0) return 0;
	dst[0] = 0;
	if (!src) return 0;
	int n = srclen;
	if (n < 0) n = (int)strlen(src);
	if (n <= 0) return 0;
	int w = MultiByteToWideChar(cp, 0, src, n, dst, dstChars - 1);
	if (w <= 0)
		w = MultiByteToWideChar(CP_ACP, 0, src, n, dst, dstChars - 1);
	if (w < 0) w = 0;
	dst[w] = 0;
	SlashNorm(dst);
	return w > 0 ? 1 : 0;
}

static int ReadAllFile(const wchar_t* path, std::vector<unsigned char>& buf)
{
	buf.clear();
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	LARGE_INTEGER sz = {};
	if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 80ll * 1024 * 1024) {
		CloseHandle(h);
		return 0;
	}
	buf.resize((size_t)sz.QuadPart);
	DWORD rd = 0;
	const BOOL ok = ReadFile(h, buf.data(), (DWORD)buf.size(), &rd, NULL);
	CloseHandle(h);
	if (!ok || rd != buf.size()) {
		buf.clear();
		return 0;
	}
	return 1;
}

static int WriteAllFile(const wchar_t* path, const unsigned char* data, unsigned size)
{
	if (!path || !path[0]) return 0;
	wchar_t dir[MIDIPACK_PATH];
	wcsncpy_s(dir, path, _TRUNCATE);
	wchar_t* sl = wcsrchr(dir, L'\\');
	if (sl) {
		*sl = 0;
		if (dir[0])
			SHCreateDirectoryExW(NULL, dir, NULL);
	}
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD wr = 0;
	const BOOL ok = (size == 0) || WriteFile(h, data, size, &wr, NULL);
	CloseHandle(h);
	return (ok && wr == size) ? 1 : 0;
}

static ULONGLONG HashPathMeta(const wchar_t* path)
{
	ULONGLONG h = 14695981039346656037ULL;
	if (!path) return h;
	for (const wchar_t* p = path; *p; ++p) {
		wchar_t c = *p;
		if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
		if (c == L'/') c = L'\\';
		h ^= (ULONGLONG)(unsigned)c;
		h *= 1099511628211ULL;
	}
	WIN32_FILE_ATTRIBUTE_DATA fad = {};
	if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
		h ^= ((ULONGLONG)fad.nFileSizeLow) | ((ULONGLONG)fad.nFileSizeHigh << 32);
		h *= 1099511628211ULL;
		h ^= ((ULONGLONG)fad.ftLastWriteTime.dwLowDateTime)
			| ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32);
		h *= 1099511628211ULL;
	}
	return h;
}

static int CacheDirFor(const wchar_t* arc, wchar_t* out, int outChars)
{
	if (!out || outChars < 16) return 0;
	out[0] = 0;
	wchar_t tmp[MAX_PATH];
	if (!GetTempPathW(MAX_PATH, tmp) || !tmp[0])
		return 0;
	wchar_t root[MAX_PATH];
	_snwprintf_s(root, _TRUNCATE, L"%sogg_midpack", tmp);
	CreateDirectoryW(root, NULL);
	_snwprintf_s(out, outChars, _TRUNCATE, L"%s\\%016I64X", root, HashPathMeta(arc));
	CreateDirectoryW(out, NULL);
	return 1;
}

struct PackName {
	wchar_t inner[MIDIPACK_INNER];
};

/* ---- zip (minizip) ---- */

static int ZipNameToW(const char* src, unsigned flags, wchar_t* dst, int dstChars)
{
	const UINT cp = (flags & (1u << 11)) ? CP_UTF8 : CP_ACP;
	return MbToW(src, -1, cp, dst, dstChars);
}

static int ZipList(const wchar_t* zip, std::vector<PackName>& names)
{
	names.clear();
	zlib_filefunc64_def ffunc = {};
	fill_win32_filefunc64W(&ffunc);
	unzFile uf = unzOpen2_64(zip, &ffunc);
	if (!uf) return 0;
	unz_global_info64 gi = {};
	if (unzGetGlobalInfo64(uf, &gi) != UNZ_OK) {
		unzClose(uf);
		return 0;
	}
	for (ZPOS64_T i = 0; i < gi.number_entry && names.size() < kMaxMembers; i++) {
		char fn[1024] = {};
		unz_file_info64 fi = {};
		if (unzGetCurrentFileInfo64(uf, &fi, fn, sizeof(fn), NULL, 0, NULL, 0) != UNZ_OK)
			break;
		PackName pn = {};
		ZipNameToW(fn, (unsigned)fi.flag, pn.inner, MIDIPACK_INNER);
		SlashNorm(pn.inner);
		const int isDir = (pn.inner[0] && (pn.inner[wcsnlen(pn.inner, MIDIPACK_INNER) - 1] == L'\\'
			|| (fi.external_fa & FILE_ATTRIBUTE_DIRECTORY) != 0));
		if (!isDir && pn.inner[0] && !IsJunkName(pn.inner))
			names.push_back(pn);
		if ((ZPOS64_T)(i + 1) < gi.number_entry)
			unzGoToNextFile(uf);
	}
	unzClose(uf);
	return 1;
}

static int ZipExtractOne(const wchar_t* zip, const wchar_t* inner, const wchar_t* destFile)
{
	zlib_filefunc64_def ffunc = {};
	fill_win32_filefunc64W(&ffunc);
	unzFile uf = unzOpen2_64(zip, &ffunc);
	if (!uf) return 0;
	unz_global_info64 gi = {};
	if (unzGetGlobalInfo64(uf, &gi) != UNZ_OK) {
		unzClose(uf);
		return 0;
	}
	for (ZPOS64_T i = 0; i < gi.number_entry; i++) {
		char fn[1024] = {};
		unz_file_info64 fi = {};
		if (unzGetCurrentFileInfo64(uf, &fi, fn, sizeof(fn), NULL, 0, NULL, 0) != UNZ_OK)
			break;
		wchar_t wfn[MIDIPACK_INNER];
		ZipNameToW(fn, (unsigned)fi.flag, wfn, MIDIPACK_INNER);
		SlashNorm(wfn);
		int match = 1;
		const wchar_t* a = wfn;
		const wchar_t* b = inner;
		while (*a && *b) {
			if (!CiEq(*a++, *b++)) { match = 0; break; }
		}
		if (match && *a == 0 && *b == 0) {
			if (fi.uncompressed_size > kMaxFile) break;
			if (unzOpenCurrentFile(uf) != UNZ_OK) break;
			std::vector<unsigned char> buf((size_t)fi.uncompressed_size);
			size_t got = 0;
			while (got < buf.size()) {
				int n = unzReadCurrentFile(uf, buf.data() + got, (unsigned)(buf.size() - got));
				if (n <= 0) break;
				got += (size_t)n;
			}
			unzCloseCurrentFile(uf);
			unzClose(uf);
			if (got != buf.size() && fi.uncompressed_size != 0) return 0;
			return WriteAllFile(destFile, buf.empty() ? NULL : buf.data(), (unsigned)buf.size());
		}
		if ((ZPOS64_T)(i + 1) < gi.number_entry)
			unzGoToNextFile(uf);
	}
	unzClose(uf);
	return 0;
}

/* ---- LZH list + lh0/lh4/lh5/lh6/lh7 ---- */

struct LzhBit {
	const unsigned char* p;
	unsigned n, i;
	unsigned acc;
	int accbits;
};

static void LzhBitInit(LzhBit* b, const unsigned char* p, unsigned n)
{
	b->p = p; b->n = n; b->i = 0; b->acc = 0; b->accbits = 0;
}

static unsigned LzhGetBits(LzhBit* b, int need)
{
	if (need <= 0) return 0;
	while (b->accbits < need) {
		unsigned v = (b->i < b->n) ? b->p[b->i++] : 0;
		b->acc = (b->acc << 8) | v;
		b->accbits += 8;
	}
	b->accbits -= need;
	return (b->acc >> b->accbits) & ((1u << need) - 1u);
}

static unsigned LzhPeek16(LzhBit* b)
{
	while (b->accbits < 16) {
		unsigned v = (b->i < b->n) ? b->p[b->i++] : 0;
		b->acc = (b->acc << 8) | v;
		b->accbits += 8;
	}
	return (b->acc >> (b->accbits - 16)) & 0xFFFFu;
}

enum { LZH_MAXMATCH = 256, LZH_THRESHOLD = 3 };
enum { LZH_NC = 255 + LZH_MAXMATCH + 2 - LZH_THRESHOLD }; /* 510 */
enum { LZH_NT = 19, LZH_NPT = 128 };

struct LzhHuff {
	unsigned short c_left[2 * LZH_NC];
	unsigned short c_right[2 * LZH_NC];
	unsigned short pt_left[2 * LZH_NC];
	unsigned short pt_right[2 * LZH_NC];
	unsigned char c_len[LZH_NC];
	unsigned char pt_len[LZH_NPT];
	unsigned short c_table[4096];
	unsigned short pt_table[256];
	int np;
};

static void LzhMakeTable(const unsigned char* bitlen, int nchar, unsigned short* table, int tablebits,
	unsigned short* left, unsigned short* right)
{
	unsigned count[17] = {};
	unsigned start[18];
	unsigned weight[17];
	for (int i = 0; i < nchar; i++) {
		int l = bitlen[i];
		if (l > 16) l = 16;
		if (l > 0) count[l]++;
	}
	start[1] = 0;
	for (int i = 1; i <= 16; i++)
		start[i + 1] = start[i] + (count[i] << (16 - i));
	const int jut = 16 - tablebits;
	for (int i = 1; i <= tablebits; i++) {
		start[i] >>= jut;
		weight[i] = 1u << (tablebits - i);
	}
	for (int i = tablebits + 1; i <= 16; i++)
		weight[i] = 1u << (16 - i);
	const unsigned tablesize = 1u << tablebits;
	memset(table, 0, tablesize * sizeof(unsigned short));
	int avail = nchar;
	for (int ch = 0; ch < nchar; ch++) {
		int l = bitlen[ch];
		if (l == 0) continue;
		unsigned k = start[l];
		unsigned nx = k + weight[l];
		if (l <= tablebits) {
			if (nx > tablesize) nx = tablesize;
			for (unsigned j = k; j < nx; j++)
				table[j] = (unsigned short)ch;
		} else {
			unsigned short* p = &table[(k >> jut) & (tablesize - 1)];
			int rest = l - tablebits;
			unsigned code = k << jut;
			while (rest > 0) {
				if (*p == 0) {
					if (avail >= 2 * LZH_NC) return;
					left[avail] = right[avail] = 0;
					*p = (unsigned short)avail++;
				}
				if (code & 0x8000u)
					p = &right[*p];
				else
					p = &left[*p];
				code <<= 1;
				rest--;
			}
			*p = (unsigned short)ch;
		}
		start[l] = nx;
	}
}

static void LzhReadPtLen(LzhBit* b, LzhHuff* h, int nn, int nbit, int i_special)
{
	int n = (int)LzhGetBits(b, nbit);
	if (n == 0) {
		int c = (int)LzhGetBits(b, nbit);
		for (int i = 0; i < nn && i < LZH_NPT; i++) h->pt_len[i] = 0;
		for (int i = 0; i < 256; i++) h->pt_table[i] = (unsigned short)c;
		return;
	}
	if (n > nn) n = nn;
	int i = 0;
	while (i < n) {
		int c = (int)LzhGetBits(b, 3);
		if (c == 7) {
			while (LzhGetBits(b, 1)) {
				c++;
				if (c > 16) break;
			}
		}
		h->pt_len[i++] = (unsigned char)c;
		if (i == i_special) {
			int z = (int)LzhGetBits(b, 2);
			while (--z >= 0 && i < nn)
				h->pt_len[i++] = 0;
		}
	}
	while (i < nn) h->pt_len[i++] = 0;
	LzhMakeTable(h->pt_len, nn, h->pt_table, 8, h->pt_left, h->pt_right);
}

static unsigned LzhDecodePtNt(LzhBit* b, LzhHuff* h)
{
	unsigned j = h->pt_table[LzhPeek16(b) >> 8];
	if (j >= LZH_NT) {
		unsigned mask = 1u << 7;
		do {
			j = (LzhPeek16(b) & mask) ? h->pt_right[j] : h->pt_left[j];
			mask >>= 1;
		} while (j >= LZH_NT && mask);
	}
	if (j >= LZH_NPT) j = 0;
	LzhGetBits(b, h->pt_len[j]);
	return j;
}

static void LzhReadCLenReal(LzhBit* b, LzhHuff* h)
{
	int n = (int)LzhGetBits(b, 9);
	if (n == 0) {
		int c = (int)LzhGetBits(b, 9);
		for (int i = 0; i < LZH_NC; i++) h->c_len[i] = 0;
		for (int i = 0; i < 4096; i++) h->c_table[i] = (unsigned short)c;
		return;
	}
	if (n > LZH_NC) n = LZH_NC;
	int i = 0;
	while (i < n) {
		unsigned c = LzhDecodePtNt(b, h);
		if (c <= 2) {
			int z;
			if (c == 0) z = 1;
			else if (c == 1) z = (int)LzhGetBits(b, 4) + 3;
			else z = (int)LzhGetBits(b, 9) + 20;
			while (z-- > 0 && i < LZH_NC)
				h->c_len[i++] = 0;
		} else {
			h->c_len[i++] = (unsigned char)(c - 2);
		}
	}
	while (i < LZH_NC) h->c_len[i++] = 0;
	LzhMakeTable(h->c_len, LZH_NC, h->c_table, 12, h->c_left, h->c_right);
}

static unsigned LzhDecodeC(LzhBit* b, LzhHuff* h)
{
	unsigned j = h->c_table[LzhPeek16(b) >> 4];
	if (j >= LZH_NC) {
		unsigned mask = 1u << 3;
		do {
			j = (LzhPeek16(b) & mask) ? h->c_right[j] : h->c_left[j];
			mask >>= 1;
		} while (j >= LZH_NC && mask);
	}
	if (j >= LZH_NC) j = 0;
	LzhGetBits(b, h->c_len[j]);
	return j;
}

static unsigned LzhDecodeP(LzhBit* b, LzhHuff* h)
{
	unsigned j = h->pt_table[LzhPeek16(b) >> 8];
	if (j >= (unsigned)h->np) {
		unsigned mask = 1u << 7;
		do {
			j = (LzhPeek16(b) & mask) ? h->pt_right[j] : h->pt_left[j];
			mask >>= 1;
		} while (j >= (unsigned)h->np && mask);
	}
	if (j >= LZH_NPT) j = 0;
	LzhGetBits(b, h->pt_len[j]);
	if (j != 0)
		j = (1u << (j - 1)) + LzhGetBits(b, (int)j - 1);
	return j;
}

static int LzhDecodeLh(const unsigned char* src, unsigned srcn, unsigned orig, int dicbit, std::vector<unsigned char>& out)
{
	if (orig > kMaxFile || dicbit < 12 || dicbit > 16) return 0;
	out.assign(orig, 0);
	if (orig == 0) return 1;
	LzhBit br;
	LzhBitInit(&br, src, srcn);
	LzhHuff h;
	memset(&h, 0, sizeof(h));
	h.np = dicbit + 1;
	const unsigned dicsiz = 1u << dicbit;
	std::vector<unsigned char> win(dicsiz, 0x20);
	unsigned loc = 0;
	unsigned written = 0;
	const int pbit = (dicbit <= 13) ? 4 : 5;
	int safety = 0;
	while (written < orig) {
		if (++safety > 1 + (int)(orig / 16) + 8) return 0;
		LzhReadPtLen(&br, &h, LZH_NT, 5, 3);
		LzhReadCLenReal(&br, &h);
		LzhReadPtLen(&br, &h, h.np, pbit, -1);
		for (int ccc = 0; ccc < 256 && written < orig; ccc++) {
			unsigned c = LzhDecodeC(&br, &h);
			if (c <= 255) {
				out[written++] = (unsigned char)c;
				win[loc++] = (unsigned char)c;
				loc &= (dicsiz - 1);
			} else {
				unsigned len = c - (256 - LZH_THRESHOLD);
				unsigned pos = LzhDecodeP(&br, &h);
				if (pos >= dicsiz) return 0;
				for (unsigned k = 0; k < len && written < orig; k++) {
					unsigned char v = win[(loc - pos - 1) & (dicsiz - 1)];
					out[written++] = v;
					win[loc++] = v;
					loc &= (dicsiz - 1);
				}
			}
		}
	}
	return 1;
}

static int LzhMethodDic(const char* m, int* dicbit)
{
	if (!m || m[0] != '-' || m[4] != '-') return -1;
	if (memcmp(m, "-lh0-", 5) == 0 || memcmp(m, "-lz4-", 5) == 0) { *dicbit = 0; return 0; }
	if (memcmp(m, "-lhd-", 5) == 0) { *dicbit = -1; return 0; }
	if (memcmp(m, "-lh4-", 5) == 0) { *dicbit = 12; return 1; }
	if (memcmp(m, "-lh5-", 5) == 0) { *dicbit = 13; return 1; }
	if (memcmp(m, "-lh6-", 5) == 0) { *dicbit = 15; return 1; }
	if (memcmp(m, "-lh7-", 5) == 0) { *dicbit = 16; return 1; }
	return -1;
}

struct LzhEnt {
	wchar_t name[MIDIPACK_INNER];
	unsigned packedOff;
	unsigned packed;
	unsigned orig;
	int dicbit; /* 0=store, >0 decode, -1 skip dir, -2 unknown */
};

static int LzhSkipExt(const unsigned char* d, unsigned n, unsigned* poff)
{
	unsigned off = *poff;
	if (off + 2 > n) return 0;
	unsigned next = Le16(d + off);
	off += 2;
	while (next) {
		if (off + next > n) return 0;
		unsigned nxt2 = 0;
		if (next >= 2)
			nxt2 = Le16(d + off + next - 2);
		off += next - 2;
		next = nxt2;
		if (off > n) return 0;
	}
	*poff = off;
	return 1;
}

static int LzhCollect(const unsigned char* d, unsigned n, std::vector<LzhEnt>& ents)
{
	ents.clear();
	unsigned off = 0;
	int safety = 0;
	while (off + 21 < n && safety++ < kMaxMembers) {
		unsigned hdrRemain = d[off];
		unsigned packed = 0, orig = 0, nameOff = 0, nameLen = 0;
		unsigned hdrTotal = 0;
		int level = 0;
		char method[6] = {};
		if (d[off] == 0 && d[off + 1] == 0)
			break;
		/* level 2: header size is 16-bit at 0, level at 20 */
		if (off + 21 <= n && d[off + 20] == 2) {
			hdrTotal = Le16(d + off);
			if (hdrTotal < 24 || off + hdrTotal > n) break;
			memcpy(method, d + off + 2, 5);
			packed = Le32(d + off + 7);
			orig = Le32(d + off + 11);
			level = 2;
			unsigned ex = off + 24;
			LzhEnt e = {};
			e.packed = packed;
			e.orig = orig;
			e.packedOff = off + hdrTotal;
			int dic = -2;
			LzhMethodDic(method, &dic);
			e.dicbit = dic;
			while (ex + 3 < off + hdrTotal) {
				unsigned sz = Le16(d + ex);
				if (sz < 3) break;
				unsigned char typ = d[ex + 2];
				if (typ == 0x01 && sz > 3) {
					MbToW((const char*)d + ex + 3, (int)sz - 5, 932, e.name, MIDIPACK_INNER);
				}
				ex += sz;
			}
			if (!e.name[0])
				_snwprintf_s(e.name, _TRUNCATE, L"file%03d", (int)ents.size());
			SlashNorm(e.name);
			if (e.dicbit != -1 && e.name[0] && !IsJunkName(e.name))
				ents.push_back(e);
			off = e.packedOff + packed;
			continue;
		}
		if (hdrRemain == 0) break;
		hdrTotal = 2 + hdrRemain;
		if (off + hdrTotal > n) break;
		memcpy(method, d + off + 2, 5);
		packed = Le32(d + off + 7);
		orig = Le32(d + off + 11);
		level = d[off + 20];
		nameLen = d[off + 21];
		nameOff = off + 22;
		unsigned dataOff = off + hdrTotal;
		if (level == 1) {
			unsigned ex = off + hdrTotal;
			if (!LzhSkipExt(d, n, &ex)) break;
			dataOff = ex;
		}
		LzhEnt e = {};
		e.packed = packed;
		e.orig = orig;
		e.packedOff = dataOff;
		int dic = -2;
		LzhMethodDic(method, &dic);
		e.dicbit = dic;
		if (nameOff + nameLen <= n)
			MbToW((const char*)d + nameOff, (int)nameLen, 932, e.name, MIDIPACK_INNER);
		if (!e.name[0])
			_snwprintf_s(e.name, _TRUNCATE, L"file%03d", (int)ents.size());
		SlashNorm(e.name);
		if (e.dicbit != -1 && e.name[0] && !IsJunkName(e.name))
			ents.push_back(e);
		if (dataOff + packed > n) break;
		off = dataOff + packed;
	}
	return ents.empty() ? 0 : 1;
}

static int LzhList(const wchar_t* path, std::vector<PackName>& names)
{
	names.clear();
	std::vector<unsigned char> buf;
	if (!ReadAllFile(path, buf)) return 0;
	std::vector<LzhEnt> ents;
	if (!LzhCollect(buf.data(), (unsigned)buf.size(), ents)) return 0;
	for (size_t i = 0; i < ents.size(); ++i) {
		PackName pn = {};
		wcsncpy_s(pn.inner, ents[i].name, _TRUNCATE);
		names.push_back(pn);
	}
	return 1;
}

static int LzhExtractOne(const wchar_t* path, const wchar_t* inner, const wchar_t* destFile)
{
	std::vector<unsigned char> buf;
	if (!ReadAllFile(path, buf)) return 0;
	std::vector<LzhEnt> ents;
	if (!LzhCollect(buf.data(), (unsigned)buf.size(), ents)) return 0;
	for (size_t i = 0; i < ents.size(); ++i) {
		int match = 1;
		const wchar_t* a = ents[i].name;
		const wchar_t* b = inner;
		while (*a && *b) {
			if (!CiEq(*a++, *b++)) { match = 0; break; }
		}
		if (!(match && *a == 0 && *b == 0)) continue;
		if (ents[i].orig > kMaxFile) return 0;
		if (ents[i].packedOff + ents[i].packed > buf.size()) return 0;
		const unsigned char* src = buf.data() + ents[i].packedOff;
		if (ents[i].dicbit == 0) {
			if (ents[i].packed != ents[i].orig) return 0;
			return WriteAllFile(destFile, src, ents[i].orig);
		}
		if (ents[i].dicbit < 0) return 0;
		std::vector<unsigned char> out;
		if (!LzhDecodeLh(src, ents[i].packed, ents[i].orig, ents[i].dicbit, out))
			return 0;
		return WriteAllFile(destFile, out.data(), (unsigned)out.size());
	}
	return 0;
}

/* ---- 7za ---- */

static int FileExistsW(const wchar_t* p)
{
	return p && p[0] && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

static int ToolsDir(wchar_t* out, int outChars)
{
	wchar_t base[MAX_PATH] = {};
	DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) return 0;
	_snwprintf_s(out, outChars, _TRUNCATE, L"%s\\oggYSED\\tools", base);
	SHCreateDirectoryExW(NULL, out, NULL);
	return 1;
}

static int Find7za(wchar_t* out, int outChars)
{
	if (!out || outChars < 8) return 0;
	out[0] = 0;
	wchar_t tryPath[MAX_PATH];
	if (ToolsDir(tryPath, MAX_PATH)) {
		wchar_t a[MAX_PATH];
		_snwprintf_s(a, _TRUNCATE, L"%s\\7za.exe", tryPath);
		if (FileExistsW(a)) { wcsncpy_s(out, outChars, a, _TRUNCATE); return 1; }
		_snwprintf_s(a, _TRUNCATE, L"%s\\7z.exe", tryPath);
		if (FileExistsW(a)) { wcsncpy_s(out, outChars, a, _TRUNCATE); return 1; }
	}
	const wchar_t* pf = _wgetenv(L"ProgramFiles");
	if (pf) {
		_snwprintf_s(tryPath, _TRUNCATE, L"%s\\7-Zip\\7z.exe", pf);
		if (FileExistsW(tryPath)) { wcsncpy_s(out, outChars, tryPath, _TRUNCATE); return 1; }
		_snwprintf_s(tryPath, _TRUNCATE, L"%s\\7-Zip\\7za.exe", pf);
		if (FileExistsW(tryPath)) { wcsncpy_s(out, outChars, tryPath, _TRUNCATE); return 1; }
	}
	const wchar_t* pf86 = _wgetenv(L"ProgramFiles(x86)");
	if (pf86) {
		_snwprintf_s(tryPath, _TRUNCATE, L"%s\\7-Zip\\7z.exe", pf86);
		if (FileExistsW(tryPath)) { wcsncpy_s(out, outChars, tryPath, _TRUNCATE); return 1; }
	}
	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(NULL, exe, MAX_PATH);
	wchar_t* sl = wcsrchr(exe, L'\\');
	if (sl) sl[1] = 0;
	_snwprintf_s(tryPath, _TRUNCATE, L"%stools\\7za.exe", exe);
	if (FileExistsW(tryPath)) { wcsncpy_s(out, outChars, tryPath, _TRUNCATE); return 1; }
	return 0;
}

static int RunHidden(const wchar_t* cmd)
{
	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};
	wchar_t buf[2048];
	wcsncpy_s(buf, cmd, _TRUNCATE);
	if (!CreateProcessW(NULL, buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
		return 0;
	WaitForSingleObject(pi.hProcess, 120000);
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return code == 0 ? 1 : 0;
}

static int DownloadUrl(const wchar_t* url, const wchar_t* dest)
{
	HINTERNET hInet = InternetOpenW(L"oggMidiPack/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!hInet) return 0;
	DWORD timeout = 120000;
	InternetSetOption(hInet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI
		| INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_IGNORE_CERT_CN_INVALID;
	HINTERNET hUrl = InternetOpenUrlW(hInet, url, NULL, 0, flags, 0);
	if (!hUrl) {
		InternetCloseHandle(hInet);
		return 0;
	}
	DWORD status = 0, slen = sizeof(status);
	if (!HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &slen, NULL)
		|| status < 200 || status >= 300) {
		InternetCloseHandle(hUrl);
		InternetCloseHandle(hInet);
		return 0;
	}
	HANDLE hf = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hf == INVALID_HANDLE_VALUE) {
		InternetCloseHandle(hUrl);
		InternetCloseHandle(hInet);
		return 0;
	}
	char tmp[8192];
	DWORD rd = 0, wr = 0;
	int ok = 1;
	while (InternetReadFile(hUrl, tmp, sizeof(tmp), &rd) && rd) {
		if (!WriteFile(hf, tmp, rd, &wr, NULL) || wr != rd) { ok = 0; break; }
	}
	CloseHandle(hf);
	InternetCloseHandle(hUrl);
	InternetCloseHandle(hInet);
	return ok;
}

static int Ensure7za(wchar_t* out, int outChars)
{
	if (Find7za(out, outChars)) return 1;
	wchar_t dir[MAX_PATH];
	if (!ToolsDir(dir, MAX_PATH)) return 0;
	wchar_t zip[MAX_PATH];
	_snwprintf_s(zip, _TRUNCATE, L"%s\\7za920.zip", dir);
	if (!FileExistsW(zip)) {
		if (!DownloadUrl(L"https://www.7-zip.org/a/7za920.zip", zip))
			return 0;
	}
	wchar_t exe[MAX_PATH];
	_snwprintf_s(exe, _TRUNCATE, L"%s\\7za.exe", dir);
	if (ZipExtractOne(zip, L"7za.exe", exe) && FileExistsW(exe)) {
		wcsncpy_s(out, outChars, exe, _TRUNCATE);
		return 1;
	}
	return 0;
}

static int SevenList(const wchar_t* arc, std::vector<PackName>& names)
{
	names.clear();
	wchar_t za[MAX_PATH];
	if (!Ensure7za(za, MAX_PATH)) return 0;
	wchar_t tmpDir[MAX_PATH];
	GetTempPathW(MAX_PATH, tmpDir);
	wchar_t listf[MAX_PATH];
	_snwprintf_s(listf, _TRUNCATE, L"%sogg_midpack_l.txt", tmpDir);
	DeleteFileW(listf);
	wchar_t cmd[2048];
	_snwprintf_s(cmd, _TRUNCATE, L"cmd /c \"\"%s\" l -slt -- \"%s\" > \"%s\"\"", za, arc, listf);
	if (!RunHidden(cmd) && !FileExistsW(listf))
		return 0;
	HANDLE h = CreateFileW(listf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD sz = GetFileSize(h, NULL);
	if (sz == INVALID_FILE_SIZE || sz > 8 * 1024 * 1024) { CloseHandle(h); return 0; }
	std::vector<char> t(sz + 1);
	DWORD rd = 0;
	ReadFile(h, t.data(), sz, &rd, NULL);
	CloseHandle(h);
	DeleteFileW(listf);
	t[rd] = 0;
	const char* p = t.data();
	while (p && *p) {
		const char* nl = strchr(p, '\n');
		size_t ln = nl ? (size_t)(nl - p) : strlen(p);
		if (ln >= 7 && _strnicmp(p, "Path = ", 7) == 0) {
			char raw[1024];
			size_t cl = ln - 7;
			if (cl >= sizeof(raw)) cl = sizeof(raw) - 1;
			memcpy(raw, p + 7, cl);
			raw[cl] = 0;
			while (cl && (raw[cl - 1] == '\r' || raw[cl - 1] == ' ')) raw[--cl] = 0;
			if (_stricmp(raw, "Path") != 0) {
				PackName pn = {};
				MbToW(raw, -1, CP_ACP, pn.inner, MIDIPACK_INNER);
				SlashNorm(pn.inner);
				if (pn.inner[0] && wcschr(pn.inner, L'\\') == NULL && wcschr(pn.inner, L'.') == NULL
					&& wcsstr(pn.inner, L":\\")) {
					/* archive path line — skip if it equals the archive itself */
				} else if (pn.inner[0] && !IsJunkName(pn.inner)
					&& pn.inner[wcsnlen(pn.inner, MIDIPACK_INNER) - 1] != L'\\')
					names.push_back(pn);
			}
		}
		p = nl ? nl + 1 : NULL;
	}
	return names.empty() ? 0 : 1;
}

static int SevenExtractOne(const wchar_t* arc, const wchar_t* inner, const wchar_t* destFile)
{
	wchar_t za[MAX_PATH];
	if (!Ensure7za(za, MAX_PATH)) return 0;
	wchar_t destDir[MIDIPACK_PATH];
	wcsncpy_s(destDir, destFile, _TRUNCATE);
	wchar_t* sl = wcsrchr(destDir, L'\\');
	if (sl) *sl = 0;
	SHCreateDirectoryExW(NULL, destDir, NULL);
	wchar_t innerN[MIDIPACK_INNER];
	wcsncpy_s(innerN, inner, _TRUNCATE);
	for (wchar_t* p = innerN; *p; ++p)
		if (*p == L'\\') *p = L'/';
	wchar_t cmd[2048];
	_snwprintf_s(cmd, _TRUNCATE, L"\"%s\" e -y -o\"%s\" -- \"%s\" \"%s\"", za, destDir, arc, innerN);
	if (!RunHidden(cmd)) {
		_snwprintf_s(cmd, _TRUNCATE, L"\"%s\" e -y -o\"%s\" -- \"%s\" \"%s\"", za, destDir, arc, inner);
		if (!RunHidden(cmd))
			return 0;
	}
	wchar_t produced[MIDIPACK_PATH];
	_snwprintf_s(produced, _TRUNCATE, L"%s\\%s", destDir, BaseNameW(inner));
	if (_wcsicmp(produced, destFile) != 0 && FileExistsW(produced)) {
		DeleteFileW(destFile);
		MoveFileW(produced, destFile);
	}
	return FileExistsW(destFile) ? 1 : 0;
}

static int ListArchive(const wchar_t* arc, std::vector<PackName>& names)
{
	const int k = ArcKind(arc);
	if (k == 1) return ZipList(arc, names);
	if (k == 2) {
		if (LzhList(arc, names) && !names.empty()) return 1;
		return SevenList(arc, names);
	}
	if (k == 3) return SevenList(arc, names);
	return 0;
}

static int ExtractOne(const wchar_t* arc, const wchar_t* inner, const wchar_t* dest)
{
	const int k = ArcKind(arc);
	if (k == 1) {
		if (ZipExtractOne(arc, inner, dest)) return 1;
		return SevenExtractOne(arc, inner, dest);
	}
	if (k == 2) {
		if (LzhExtractOne(arc, inner, dest)) return 1;
		return SevenExtractOne(arc, inner, dest);
	}
	if (k == 3) return SevenExtractOne(arc, inner, dest);
	return 0;
}

static int WantExtractName(const wchar_t* inner, const wchar_t* seqInner)
{
	if (IsSeqPath(inner) && SameStem(inner, seqInner)) return 1;
	if (IsWantedSidecar(inner)) return 1;
	if (seqInner && seqInner[0] && SameStem(inner, seqInner)) return 1;
	return 0;
}

static int MaterializeArc(const wchar_t* arc, const wchar_t* inner, wchar_t* out, int outChars)
{
	wchar_t dir[MIDIPACK_PATH];
	if (!CacheDirFor(arc, dir, MIDIPACK_PATH)) return 0;
	std::vector<PackName> names;
	if (!ListArchive(arc, names)) return 0;
	int any = 0;
	for (size_t i = 0; i < names.size(); ++i) {
		if (!WantExtractName(names[i].inner, inner)) continue;
		wchar_t dest[MIDIPACK_PATH];
		_snwprintf_s(dest, _TRUNCATE, L"%s\\%s", dir, names[i].inner);
		if (!FileExistsW(dest)) {
			if (!ExtractOne(arc, names[i].inner, dest))
				continue;
		}
		any = 1;
	}
	_snwprintf_s(out, outChars, _TRUNCATE, L"%s\\%s", dir, inner);
	SlashNorm(out);
	if (FileExistsW(out)) return 1;
	/* basename fallback (7za e) */
	_snwprintf_s(out, outChars, _TRUNCATE, L"%s\\%s", dir, BaseNameW(inner));
	return FileExistsW(out) ? 1 : (any ? 0 : 0);
}

} // namespace

int MidiPackIsSeqExt(const wchar_t* path)
{
	return IsSeqPath(path);
}

int MidiPackIsArchiveExt(const wchar_t* path)
{
	wchar_t arc[MIDIPACK_PATH], inner[MIDIPACK_INNER];
	if (MidiPackParseVirtual(path, arc, MIDIPACK_PATH, inner, MIDIPACK_INNER) && inner[0])
		path = arc;
	return ArcKind(path) != 0;
}

int MidiPackIsVirtualPath(const wchar_t* path)
{
	if (!path) return 0;
	const wchar_t* gt = wcschr(path, L'>');
	return (gt && gt[1]) ? 1 : 0;
}

int MidiPackParseVirtual(const wchar_t* path, wchar_t* arc, int arcChars, wchar_t* inner, int innerChars)
{
	if (arc && arcChars > 0) arc[0] = 0;
	if (inner && innerChars > 0) inner[0] = 0;
	if (!path || !path[0]) return 0;
	const wchar_t* gt = wcschr(path, L'>');
	if (!gt || !gt[1]) {
		if (arc) wcsncpy_s(arc, arcChars, path, _TRUNCATE);
		return 0;
	}
	if (arc) {
		size_t n = (size_t)(gt - path);
		if (n >= (size_t)arcChars) n = (size_t)arcChars - 1;
		wcsncpy_s(arc, arcChars, path, n);
	}
	if (inner) {
		wcsncpy_s(inner, innerChars, gt + 1, _TRUNCATE);
		SlashNorm(inner);
	}
	return 1;
}

int MidiPackFormatVirtual(const wchar_t* arc, const wchar_t* inner, wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return 0;
	out[0] = 0;
	if (!arc || !inner || !inner[0]) return 0;
	_snwprintf_s(out, outChars, _TRUNCATE, L"%s>%s", arc, inner);
	return 1;
}

int MidiPackIsCemuZip(const wchar_t* zipPath)
{
	if (!zipPath || !EqExtW(zipPath, L".zip")) return 0;
	wchar_t zipOut[CEMU_ZIP_PATH];
	char dataDir[CEMU_DATA_DIR];
	const CEmuGameEntry* ge = CEmuMgrResolveZip(CEmuMgrGet(), zipPath, zipOut, (int)_countof(zipOut),
		dataDir, (int)sizeof(dataDir));
	return ge ? 1 : 0;
}

int MidiPackListSeq(const wchar_t* arc, MidiPackSeq* out, int maxOut)
{
	if (!arc || !out || maxOut <= 0) return 0;
	if (MidiPackIsCemuZip(arc)) return 0;
	std::vector<PackName> names;
	if (!ListArchive(arc, names)) return 0;
	int n = 0;
	for (size_t i = 0; i < names.size() && n < maxOut; ++i) {
		if (!(IsSeqPath(names[i].inner) || EqExtW(names[i].inner, L".eup")) || IsJunkName(names[i].inner)) continue;
		wcsncpy_s(out[n].inner, names[i].inner, _TRUNCATE);
		wcsncpy_s(out[n].name, BaseNameW(names[i].inner), _TRUNCATE);
		out[n].hasWrd = 0;
		for (size_t j = 0; j < names.size(); ++j) {
			if (EqExtW(names[j].inner, L".wrd") && SameStem(names[j].inner, names[i].inner)) {
				out[n].hasWrd = 1;
				break;
			}
		}
		n++;
	}
	return n;
}

int MidiPackHasSidecarWrd(const wchar_t* src)
{
	wchar_t arc[MIDIPACK_PATH], inner[MIDIPACK_INNER];
	if (MidiPackParseVirtual(src, arc, MIDIPACK_PATH, inner, MIDIPACK_INNER) && inner[0]) {
		MidiPackSeq seqs[MIDIPACK_MAX_SEQ];
		const int n = MidiPackListSeq(arc, seqs, MIDIPACK_MAX_SEQ);
		for (int i = 0; i < n; ++i) {
			int match = 1;
			const wchar_t* a = seqs[i].inner;
			const wchar_t* b = inner;
			while (*a && *b) {
				if (!CiEq(*a++, *b++)) { match = 0; break; }
			}
			if (match && *a == 0 && *b == 0)
				return seqs[i].hasWrd;
		}
		return 0;
	}
	wchar_t tmp[MAX_PATH];
	return ComposerFindSidecar(src, L".wrd", tmp, MAX_PATH)
		|| ComposerFindSidecar(src, L".WRD", tmp, MAX_PATH);
}

int MidiPackMaterialize(const wchar_t* src, wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return 0;
	out[0] = 0;
	wchar_t arc[MIDIPACK_PATH], inner[MIDIPACK_INNER];
	if (!MidiPackParseVirtual(src, arc, MIDIPACK_PATH, inner, MIDIPACK_INNER) || !inner[0])
		return 0;
	return MaterializeArc(arc, inner, out, outChars);
}

int MidiPackFindSidecarWrd(const wchar_t* src, wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return 0;
	out[0] = 0;
	wchar_t phys[MIDIPACK_PATH];
	phys[0] = 0;
	const wchar_t* seqPath = src;
	if (MidiPackMaterialize(src, phys, MIDIPACK_PATH))
		seqPath = phys;
	return ComposerFindSidecar(seqPath, L".wrd", out, outChars)
		|| ComposerFindSidecar(seqPath, L".WRD", out, outChars);
}
