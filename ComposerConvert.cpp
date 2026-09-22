#include "stdafx.h"
#include "ComposerConvert.h"
#include "MidiPack.h"
#include <vector>
#include <algorithm>
#include <string>

static int CiEqW(wchar_t a, wchar_t b)
{
	if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
	if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
	return a == b;
}

int ComposerEqExt(const wchar_t* path, const wchar_t* ext)
{
	if (!path || !ext) return 0;
	const wchar_t* dot = wcsrchr(path, L'.');
	if (!dot) return 0;
	while (*dot && *ext) {
		if (!CiEqW(*dot++, *ext++)) return 0;
	}
	return *dot == 0 && *ext == 0;
}

int ComposerIsSeqExt(const wchar_t* path)
{
	return ComposerEqExt(path, L".rcp") || ComposerEqExt(path, L".r36")
		|| ComposerEqExt(path, L".g36") || ComposerEqExt(path, L".g18")
		|| ComposerEqExt(path, L".mcp") || ComposerEqExt(path, L".mtd")
		|| ComposerEqExt(path, L".mff") || ComposerEqExt(path, L".seq")
		|| ComposerEqExt(path, L".smf") || ComposerEqExt(path, L".sng")
		|| ComposerEqExt(path, L".zms");
}

static int StartsWith(const unsigned char* d, unsigned n, const char* s)
{
	const size_t sl = strlen(s);
	if (n < sl) return 0;
	return memcmp(d, s, sl) == 0;
}

int ComposerKindOfMem(const unsigned char* data, unsigned size)
{
	if (!data || size < 16) return COMPOSER_KIND_NONE;
	if (size >= 8 && memcmp(data, "MThd", 4) == 0) return COMPOSER_KIND_SMF;
	if (size >= 20 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "RMID", 4) == 0)
		return COMPOSER_KIND_SMF;
	if (StartsWith(data, size, "RCM-PC98V2.0(C)COME ON MUSIC"))
		return COMPOSER_KIND_RCP;
	if (StartsWith(data, size, "COME ON MUSIC RECOMPOSER RCP3.0"))
		return COMPOSER_KIND_G36;
	if (StartsWith(data, size, "COME ON MUSIC")) {
		if (size > 0x1C && memcmp(data + 0x0E, "GS CONTROL 1.0", 14) == 0)
			return COMPOSER_KIND_GSD;
		if (size > 0x14 && (memcmp(data + 0x0E, "\0\0R ", 4) == 0
			|| memcmp(data + 0x10, "R CM-64", 7) == 0
			|| memcmp(data + 0x10, "R MT-32", 7) == 0))
			return COMPOSER_KIND_CM6;
		return COMPOSER_KIND_MCP;
	}
	if (size >= 2 && data[0] == 'S' && data[1] == 'O')
		return COMPOSER_KIND_EUP;
	if (StartsWith(data, size, "RCM-PC98V1.0"))
		return COMPOSER_KIND_MCP;
	if (size >= 16 && memcmp(data, "BALLADE SONG", 12) == 0)
		return COMPOSER_KIND_SNG;
	/* Z-MUSIC ソース。先頭が .COMMENT / (t / (i) */
	{
		unsigned i = 0;
		while (i < size && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n'))
			i++;
		if (i + 8 <= size && (memcmp(data + i, ".COMMENT", 8) == 0 || memcmp(data + i, ".comment", 8) == 0))
			return COMPOSER_KIND_ZMS;
		if (i + 3 <= size && data[i] == '(' && (data[i + 1] == 't' || data[i + 1] == 'T'
			|| data[i + 1] == 'i' || data[i + 1] == 'I'))
			return COMPOSER_KIND_ZMS;
	}
	return COMPOSER_KIND_NONE;
}

static int ReadAll(const wchar_t* path, std::vector<unsigned char>& out)
{
	out.clear();
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD size = GetFileSize(f, NULL), got = 0;
	if (size < 16 || size > 32 * 1024 * 1024) {
		CloseHandle(f);
		return 0;
	}
	out.resize(size);
	const BOOL ok = ReadFile(f, out.data(), size, &got, NULL);
	CloseHandle(f);
	if (!ok || got != size) {
		out.clear();
		return 0;
	}
	return 1;
}

int ComposerKindOf(const wchar_t* path)
{
	if (ComposerEqExt(path, L".rcp") || ComposerEqExt(path, L".r36"))
		return COMPOSER_KIND_RCP;
	if (ComposerEqExt(path, L".g36") || ComposerEqExt(path, L".g18"))
		return COMPOSER_KIND_G36;
	if (ComposerEqExt(path, L".mcp") || ComposerEqExt(path, L".mtd"))
		return COMPOSER_KIND_MCP;
	if (ComposerEqExt(path, L".eup"))
		return COMPOSER_KIND_EUP;
	if (ComposerEqExt(path, L".mff") || ComposerEqExt(path, L".smf"))
		return COMPOSER_KIND_SMF;
	if (ComposerEqExt(path, L".sng"))
		return COMPOSER_KIND_SNG;
	if (ComposerEqExt(path, L".zms"))
		return COMPOSER_KIND_ZMS;
	std::vector<unsigned char> buf;
	if (!ReadAll(path, buf)) return COMPOSER_KIND_NONE;
	return ComposerKindOfMem(buf.data(), (unsigned)buf.size());
}

void ComposerTempMidiPath(const wchar_t* src, wchar_t* dest, int destChars)
{
	if (!dest || destChars < 8) return;
	dest[0] = 0;
	wchar_t tmp[MAX_PATH];
	if (!GetTempPathW(MAX_PATH, tmp) || !tmp[0])
		wcsncpy_s(tmp, L".\\", _TRUNCATE);
	wchar_t dir[MAX_PATH];
	_snwprintf_s(dir, _TRUNCATE, L"%sogg_composer", tmp);
	CreateDirectoryW(dir, NULL);
	const wchar_t* name = src ? src : L"";
	for (const wchar_t* p = name; *p; ++p) {
		if (*p == L'\\' || *p == L'/' || *p == L'>')
			name = p + 1;
		else if (p[0] == L':' && p > src && p[-1] == L':')
			name = p + 1;
	}
	wchar_t stem[MAX_PATH];
	wcsncpy_s(stem, name, _TRUNCATE);
	wchar_t* dot = wcsrchr(stem, L'.');
	if (dot && dot != stem)
		*dot = 0;
	if (!stem[0])
		wcsncpy_s(stem, L"composer", _TRUNCATE);
	ULONGLONG h = 14695981039346656037ULL;
	if (src) {
		for (const wchar_t* p = src; *p; ++p) {
			wchar_t c = *p;
			if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
			if (c == L'/') c = L'\\';
			h ^= (ULONGLONG)(unsigned)c;
			h *= 1099511628211ULL;
		}
	}
	_snwprintf_s(dest, destChars, _TRUNCATE, L"%s\\%016I64X_%s.mid", dir, h, stem);
}

int ComposerFindSidecar(const wchar_t* src, const wchar_t* ext, wchar_t* out, int outChars)
{
	if (!src || !ext || !out || outChars < 8) return 0;
	out[0] = 0;
	wchar_t buf[MAX_PATH];
	wcsncpy_s(buf, src, _TRUNCATE);
	wchar_t* dot = wcsrchr(buf, L'.');
	if (dot && dot != buf)
		*dot = 0;
	wchar_t tryPath[MAX_PATH];
	_snwprintf_s(tryPath, _TRUNCATE, L"%s%s", buf, ext);
	if (GetFileAttributesW(tryPath) != INVALID_FILE_ATTRIBUTES) {
		wcsncpy_s(out, outChars, tryPath, _TRUNCATE);
		return 1;
	}
	wchar_t ext2[16];
	wcsncpy_s(ext2, ext, _TRUNCATE);
	for (wchar_t* p = ext2; *p; ++p) {
		if (*p >= L'a' && *p <= L'z') *p = (wchar_t)(*p - L'a' + L'A');
	}
	_snwprintf_s(tryPath, _TRUNCATE, L"%s%s", buf, ext2);
	if (GetFileAttributesW(tryPath) != INVALID_FILE_ATTRIBUTES) {
		wcsncpy_s(out, outChars, tryPath, _TRUNCATE);
		return 1;
	}
	return 0;
}

int ComposerFindSidecarWrd(const wchar_t* src, wchar_t* out, int outChars)
{
	if (MidiPackIsVirtualPath(src))
		return MidiPackFindSidecarWrd(src, out, outChars);
	return ComposerFindSidecar(src, L".wrd", out, outChars)
		|| ComposerFindSidecar(src, L".WRD", out, outChars)
		|| ComposerFindSidecar(src, L".kok", out, outChars)
		|| ComposerFindSidecar(src, L".KOK", out, outChars);
}

int ComposerHasSidecarWrd(const wchar_t* src)
{
	if (MidiPackIsVirtualPath(src))
		return MidiPackHasSidecarWrd(src);
	wchar_t tmp[MAX_PATH];
	return ComposerFindSidecarWrd(src, tmp, MAX_PATH);
}

static int SmfHasNoteOn(const wchar_t* path)
{
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD sz = GetFileSize(f, NULL), got = 0;
	if (sz == INVALID_FILE_SIZE || sz < 22 || sz > 8 * 1024 * 1024) {
		CloseHandle(f);
		return 0;
	}
	std::vector<unsigned char> d(sz);
	const BOOL ok = ReadFile(f, d.data(), sz, &got, NULL);
	CloseHandle(f);
	if (!ok || got != sz || memcmp(d.data(), "MThd", 4) != 0) return 0;
	for (DWORD i = 0; i + 3 < got; ++i) {
		const unsigned char st = d[i];
		if ((st & 0xf0) == 0x90 && d[i + 2] > 0 && d[i + 2] < 128)
			return 1;
	}
	return 0;
}

static int CacheValid(const wchar_t* src, const wchar_t* dest)
{
	HANDLE hs = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	HANDLE hd = CreateFileW(dest, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hs == INVALID_HANDLE_VALUE || hd == INVALID_HANDLE_VALUE) {
		if (hs != INVALID_HANDLE_VALUE) CloseHandle(hs);
		if (hd != INVALID_HANDLE_VALUE) CloseHandle(hd);
		return 0;
	}
	FILETIME swt, dwt;
	GetFileTime(hs, NULL, NULL, &swt);
	GetFileTime(hd, NULL, NULL, &dwt);
	const DWORD ss = GetFileSize(hs, NULL);
	const DWORD ds = GetFileSize(hd, NULL);
	CloseHandle(hs);
	CloseHandle(hd);
	if (ds < 22 || ds > 4 * 1024 * 1024) return 0;
	ULARGE_INTEGER su, du;
	su.LowPart = swt.dwLowDateTime; su.HighPart = swt.dwHighDateTime;
	du.LowPart = dwt.dwLowDateTime; du.HighPart = dwt.dwHighDateTime;
	if (!(du.QuadPart >= su.QuadPart && ss > 0)) return 0;
	return SmfHasNoteOn(dest);
}

static int WriteAll(const wchar_t* path, const unsigned char* data, unsigned size)
{
	HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD w = 0;
	const BOOL ok = WriteFile(f, data, size, &w, NULL);
	CloseHandle(f);
	return (ok && w == size) ? 1 : 0;
}

/* 変換後 SMF の隣へ DOC/HED 等をコピー（マップ判定が ogg_composer 側を見るため） */
static void CopyComposerSidecars(const wchar_t* src, const wchar_t* destMid)
{
	if (!src || !destMid) return;
	wchar_t srcStem[MAX_PATH], dstStem[MAX_PATH];
	wcsncpy_s(srcStem, src, _TRUNCATE);
	wcsncpy_s(dstStem, destMid, _TRUNCATE);
	wchar_t* d1 = wcsrchr(srcStem, L'.');
	wchar_t* s1 = wcsrchr(srcStem, L'\\');
	if (d1 && (!s1 || d1 > s1)) *d1 = 0;
	wchar_t* d2 = wcsrchr(dstStem, L'.');
	wchar_t* s2 = wcsrchr(dstStem, L'\\');
	if (d2 && (!s2 || d2 > s2)) *d2 = 0;
	static const wchar_t* kExt[] = {
		L".doc", L".txt", L".hed", L".tdf", L".wrd", L".kok",
		L".DOC", L".TXT", L".HED", L".TDF", L".WRD", L".KOK"
	};
	for (int i = 0; i < 12; ++i) {
		wchar_t from[MAX_PATH], to[MAX_PATH];
		_snwprintf_s(from, _TRUNCATE, L"%s%s", srcStem, kExt[i]);
		if (GetFileAttributesW(from) == INVALID_FILE_ATTRIBUTES) continue;
		_snwprintf_s(to, _TRUNCATE, L"%s%s", dstStem, kExt[i]);
		CopyFileW(from, to, FALSE);
	}
}

/* ---- SMF writer ---- */
struct SmfEv {
	unsigned tick;
	unsigned char a, b, c, n;
	unsigned char port;
	std::vector<unsigned char> extra;
};

struct SmfTrk {
	std::vector<SmfEv> ev;
};

static void SmfPutBe16(std::vector<unsigned char>& o, unsigned v)
{
	o.push_back((unsigned char)((v >> 8) & 0xff));
	o.push_back((unsigned char)(v & 0xff));
}
static void SmfPutBe32(std::vector<unsigned char>& o, unsigned v)
{
	o.push_back((unsigned char)((v >> 24) & 0xff));
	o.push_back((unsigned char)((v >> 16) & 0xff));
	o.push_back((unsigned char)((v >> 8) & 0xff));
	o.push_back((unsigned char)(v & 0xff));
}
static void SmfPutVar(std::vector<unsigned char>& o, unsigned v)
{
	unsigned char buf[4];
	int n = 0;
	buf[n++] = (unsigned char)(v & 0x7f);
	v >>= 7;
	while (v) {
		buf[n++] = (unsigned char)((v & 0x7f) | 0x80);
		v >>= 7;
	}
	while (n--)
		o.push_back(buf[n]);
}

static void SmfPush(SmfTrk& t, unsigned tick, unsigned char a, unsigned char b = 0,
	unsigned char c = 0, unsigned char n = 3, unsigned char port = 0)
{
	SmfEv e;
	e.tick = tick;
	e.a = a; e.b = b; e.c = c; e.n = n; e.port = port;
	t.ev.push_back(e);
}
static void SmfMeta(SmfTrk& t, unsigned tick, unsigned char type, const unsigned char* d, unsigned n)
{
	SmfEv e;
	e.tick = tick;
	e.a = 0xff; e.b = type; e.c = 0; e.n = 0; e.port = 0;
	if (d && n)
		e.extra.assign(d, d + n);
	t.ev.push_back(e);
}
static void SmfTempo(SmfTrk& t, unsigned tick, unsigned usec)
{
	unsigned char d[3] = {
		(unsigned char)((usec >> 16) & 0xff),
		(unsigned char)((usec >> 8) & 0xff),
		(unsigned char)(usec & 0xff)
	};
	SmfMeta(t, tick, 0x51, d, 3);
}
static void SmfPort(SmfTrk& t, unsigned tick, unsigned char port)
{
	unsigned char d[1] = { port };
	SmfMeta(t, tick, 0x21, d, 1);
}
static void SmfSysex(SmfTrk& t, unsigned tick, const unsigned char* d, unsigned n, unsigned char port)
{
	SmfEv e;
	e.tick = tick;
	e.a = 0xf0; e.b = 0; e.c = 0; e.n = 0; e.port = port;
	if (d && n)
		e.extra.assign(d, d + n);
	t.ev.push_back(e);
}

static void SmfBuild(const std::vector<SmfTrk>& trks, unsigned div, std::vector<unsigned char>& out)
{
	out.clear();
	unsigned ntr = 0;
	for (size_t i = 0; i < trks.size(); ++i) {
		if (!trks[i].ev.empty())
			ntr++;
	}
	if (!ntr) ntr = 1;
	out.insert(out.end(), { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1 });
	SmfPutBe16(out, ntr);
	SmfPutBe16(out, div);
	for (size_t ti = 0; ti < trks.size(); ++ti) {
		std::vector<SmfEv> ev = trks[ti].ev;
		if (ev.empty() && ti != 0)
			continue;
		std::stable_sort(ev.begin(), ev.end(), [](const SmfEv& a, const SmfEv& b) {
			if (a.tick != b.tick) return a.tick < b.tick;
			const int ao = (a.a == 0xff) ? -1 : ((a.a & 0xf0) == 0x80 ? 1 : 0);
			const int bo = (b.a == 0xff) ? -1 : ((b.a & 0xf0) == 0x80 ? 1 : 0);
			return ao < bo;
		});
		std::vector<unsigned char> body;
		unsigned last = 0;
		unsigned char running = 0;
		for (size_t i = 0; i < ev.size(); ++i) {
			const SmfEv& e = ev[i];
			unsigned dt = (e.tick >= last) ? (e.tick - last) : 0;
			last = e.tick;
			SmfPutVar(body, dt);
			if (e.a == 0xff) {
				body.push_back(0xff);
				body.push_back(e.b);
				SmfPutVar(body, (unsigned)e.extra.size());
				body.insert(body.end(), e.extra.begin(), e.extra.end());
				running = 0;
			} else if (e.a == 0xf0) {
				body.push_back(0xf0);
				std::vector<unsigned char> sx = e.extra;
				if (sx.empty() || sx.back() != 0xf7)
					sx.push_back(0xf7);
				if (!sx.empty() && sx[0] == 0xf0)
					sx.erase(sx.begin());
				if (!sx.empty() && sx.back() == 0xf7)
					sx.pop_back();
				SmfPutVar(body, (unsigned)sx.size() + 1);
				body.insert(body.end(), sx.begin(), sx.end());
				body.push_back(0xf7);
				running = 0;
			} else {
				if (e.a != running) {
					body.push_back(e.a);
					running = e.a;
				}
				if (e.n >= 2) body.push_back(e.b);
				if (e.n >= 3) body.push_back(e.c);
			}
		}
		SmfPutVar(body, 0);
		body.push_back(0xff);
		body.push_back(0x2f);
		body.push_back(0x00);
		out.insert(out.end(), { 'M', 'T', 'r', 'k' });
		SmfPutBe32(out, (unsigned)body.size());
		out.insert(out.end(), body.begin(), body.end());
	}
}

static unsigned RcpU16(const unsigned char* p) { return p[0] | ((unsigned)p[1] << 8); }
static unsigned RcpU32(const unsigned char* p)
{
	return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

struct RcpNoteOff {
	unsigned tick;
	unsigned char ch, note, port;
};

static void FlushOffs(SmfTrk& t, std::vector<RcpNoteOff>& offs, unsigned now)
{
	std::vector<RcpNoteOff> keep;
	for (size_t i = 0; i < offs.size(); ++i) {
		if (offs[i].tick <= now)
			SmfPush(t, offs[i].tick, (unsigned char)(0x80 | offs[i].ch), offs[i].note, 0x40, 3, offs[i].port);
		else
			keep.push_back(offs[i]);
	}
	offs.swap(keep);
}

static int RolandSum(const unsigned char* d, int n)
{
	unsigned s = 0;
	for (int i = 0; i < n; ++i) s += d[i];
	return (int)((0x80 - (s & 0x7f)) & 0x7f);
}

static void SendUserSysex(SmfTrk& t, unsigned tick, const unsigned char* usr, unsigned char p1,
	unsigned char p2, unsigned char ch, unsigned char port,
	unsigned char rDev, unsigned char rMod, unsigned char yDev, unsigned char yMod)
{
	unsigned char out[256];
	int n = 0;
	out[n++] = 0xf0;
	unsigned sum = 0;
	int summing = 0;
	for (int i = 0; i < 24 && usr; ++i) {
		unsigned char b = usr[i];
		if (b == 0xf7) break;
		if (b == 0x80) b = p1;
		else if (b == 0x81) b = p2;
		else if (b == 0x82) b = (unsigned char)(ch & 15);
		else if (b == 0x83) { summing = 1; sum = 0; continue; }
		else if (b == 0x84) {
			b = (unsigned char)((0x80 - (sum & 0x7f)) & 0x7f);
			summing = 0;
		}
		if (n >= 250) break;
		out[n++] = (unsigned char)(b & 0x7f);
		if (summing) sum += out[n - 1];
		(void)rDev; (void)rMod; (void)yDev; (void)yMod;
	}
	out[n++] = 0xf7;
	SmfSysex(t, tick, out, (unsigned)n, port);
}

static int ConvertRcpMem(const unsigned char* d, unsigned size, int kind, std::vector<unsigned char>& mid)
{
	if (!d || size < 0x586) return 0;
	const int isV3 = (kind == COMPOSER_KIND_G36) ? 1 : 0;
	unsigned inPos = 0;
	unsigned tickQ = 48, tempoBpm = 120, beatN = 4, beatD = 4;
	int gTrans = 0;
	unsigned trkN = 18;
	unsigned userOff, trkOff;
	if (isV3) {
		tickQ = RcpU16(d + 0x20A);
		tempoBpm = RcpU16(d + 0x20C);
		beatN = d[0x20E];
		beatD = d[0x20F];
		gTrans = (signed char)d[0x211];
		trkN = RcpU16(d + 0x208);
		userOff = 0xB18;
		trkOff = 0xC98;
	} else {
		tickQ = d[0x1C0];
		if (size > 0x1E7)
			tickQ |= ((unsigned)d[0x1E7] << 8);
		tempoBpm = d[0x1C1];
		beatN = d[0x1C2];
		beatD = d[0x1C3];
		gTrans = (signed char)d[0x1C5];
		trkN = d[0x1E6];
		if (trkN == 0) trkN = 18;
		if (trkN > 36) trkN = 36;
		userOff = 0x406;
		trkOff = 0x586;
	}
	if (tickQ < 1) tickQ = 48;
	if (tickQ > 960) tickQ = 48;
	if (tempoBpm < 8 || tempoBpm > 250) tempoBpm = 120;
	if (beatN < 1) beatN = 4;
	if (beatD < 1) beatD = 4;

	unsigned char userSx[8][24];
	memset(userSx, 0xf7, sizeof(userSx));
	if (userOff + 8 * 0x30 <= size) {
		for (int u = 0; u < 8; ++u)
			memcpy(userSx[u], d + userOff + u * 0x30 + 0x18, 24);
	}

	const unsigned hdrSize = isV3 ? 0x2Eu : 0x2Cu;
	const unsigned evSize = isV3 ? 6u : 4u;
	int noteN = 0;
	std::vector<SmfTrk> trks;
	trks.resize(trkN + 1);
	SmfTempo(trks[0], 0, 60000000u / tempoBpm);
	unsigned char ts[4] = { (unsigned char)beatN, 2, 24, 8 };
	{
		int denLog = 0;
		unsigned den = beatD;
		while (den > 1) { den >>= 1; denLog++; }
		ts[1] = (unsigned char)denLog;
	}
	SmfMeta(trks[0], 0, 0x58, ts, 4);
	{
		char title[80];
		memset(title, 0, sizeof(title));
		const unsigned tOff = 0x20;
		const unsigned tLen = isV3 ? 0x80u : 0x40u;
		unsigned n = 0;
		for (unsigned i = 0; i < tLen && tOff + i < size && n + 1 < sizeof(title); ++i) {
			unsigned char c = d[tOff + i];
			if (c == 0) break;
			if (c >= 0x20) title[n++] = (char)c;
		}
		if (n)
			SmfMeta(trks[0], 0, 0x03, (const unsigned char*)title, n);
	}

	unsigned pos = trkOff;
	for (unsigned ti = 0; ti < trkN && pos + hdrSize <= size; ++ti) {
		const unsigned char* th = d + pos;
		unsigned trkLen = isV3 ? RcpU32(th) : RcpU16(th);
		if (trkLen < hdrSize) trkLen = hdrSize;
		if (pos + trkLen > size) trkLen = size - pos;
		const int rhythm = th[3];
		int midiCh = th[4];
		int key = (signed char)(th[5] & 0x7f);
		if (th[5] & 0x80) key = 0;
		const int stPlus = (signed char)th[6];
		const int mute = th[7];
		unsigned char port = 0;
		int ch = 0;
		/* Recomposer は 01 だけミュート。02 などは演奏する */
		int skipNotes = (mute == 1) ? 1 : 0;
		if (midiCh == 0xff || midiCh < 0) {
			skipNotes = 1;
			ch = 0;
		} else {
			port = (unsigned char)((midiCh >> 4) & 1);
			ch = midiCh & 15;
		}
		SmfTrk& tr = trks[ti + 1];
		SmfPort(tr, 0, port);
		if (th[5] & 0x80)
			SmfPush(tr, 0, (unsigned char)(0xb0 | ch), 0, 0, 3, port); // bank 0; drum often ch10

		std::vector<RcpNoteOff> offs;
		struct Loop {
			unsigned startOff;
			int remain;
			unsigned startTick;
		};
		Loop loops[12];
		int loopN = 0;
		unsigned sameRet[8];
		int sameN = 0;
		unsigned tick = (stPlus > 0) ? (unsigned)stPlus : 0;
		if (stPlus < 0 && (unsigned)(-stPlus) < tickQ)
			tick = 0;
		unsigned char rDev = 0x10, rMod = 0x42, yDev = 0x10, yMod = 0x4c;
		unsigned char raH = 0, raM = 0, yaH = 0, yaM = 0;
		int tempoMul = 0x40;
		int cmdI = 0;
		const unsigned dataBeg = pos + hdrSize;
		unsigned ip = dataBeg;
		const unsigned dataEnd = pos + trkLen;
		int bomb = 0;
		std::vector<unsigned> measOff;
		measOff.push_back(dataBeg);

		auto readEv = [&](unsigned at, unsigned char& cmd, unsigned& delay, unsigned& p1, unsigned& p2) -> int {
			if (at + evSize > dataEnd) return 0;
			cmd = d[at];
			if (isV3) {
				p2 = d[at + 1];
				delay = RcpU16(d + at + 2);
				p1 = RcpU16(d + at + 4);
			} else {
				delay = d[at + 1];
				p1 = d[at + 2];
				p2 = d[at + 3];
			}
			return 1;
		};

		while (ip + evSize <= dataEnd && bomb < 80000 && tr.ev.size() < 60000) {
			bomb++;
			unsigned char cmd;
			unsigned delay = 0, p1 = 0, p2 = 0;
			if (!readEv(ip, cmd, delay, p1, p2)) break;
			const unsigned thisOff = ip;
			ip += evSize;
			cmdI++;

			if (cmd == 0xFD) {
				measOff.push_back(ip);
				if (sameN > 0)
					ip = sameRet[--sameN];
				continue;
			}
			if (cmd == 0xFE)
				break;
			if (cmd == 0xF9) {
				if (loopN < 12) {
					loops[loopN].startOff = ip;
					loops[loopN].remain = -1;
					loops[loopN].startTick = tick;
					loopN++;
				}
				continue;
			}
			if (cmd == 0xF8) {
				int cnt = (int)delay;
				if (loopN > 0) {
					Loop& L = loops[loopN - 1];
					if (L.remain < 0) {
						if (cnt == 0) cnt = 2;
						if (cnt > 16) cnt = 16;
						L.remain = cnt;
					}
					L.remain--;
					if (L.remain > 0) {
						ip = L.startOff;
					} else {
						loopN--;
					}
				}
				continue;
			}
			if (cmd == 0xFC) {
				unsigned tgt = 0;
				if (isV3) {
					unsigned idx = p1;
					if (idx >= 0x30)
						tgt = pos + hdrSize + (idx - 0x30) * evSize;
				} else {
					/* v2: オフセットは (p1&~3)|(p2<<8)。下位2bit は小節番号の上位 */
					unsigned repeatPos = (p1 & ~3u) | (p2 << 8);
					tgt = pos + repeatPos;
				}
				if (tgt >= dataBeg && tgt < dataEnd && tgt != thisOff && sameN < 8) {
					sameRet[sameN++] = ip;
					ip = tgt;
				}
				continue;
			}
			if (cmd == 0xF6 || cmd == 0xF7 || cmd == 0xF5)
				continue;

			FlushOffs(tr, offs, tick);

			if (cmd <= 0x7F) {
				if (!skipNotes && p2 > 0) {
					int note = (int)cmd + key + ((th[5] & 0x80) ? 0 : gTrans);
					while (note < 0) note += 12;
					while (note > 127) note -= 12;
					unsigned char nn = (unsigned char)note;
					unsigned char vel = (unsigned char)(p2 > 127 ? 127 : p2);
					SmfPush(tr, tick, (unsigned char)(0x90 | ch), nn, vel, 3, port);
					noteN++;
					RcpNoteOff of;
					of.tick = tick + (p1 > 0 ? p1 : 1);
					of.ch = (unsigned char)ch;
					of.note = nn;
					of.port = port;
					offs.push_back(of);
				}
			} else if (cmd >= 0x90 && cmd <= 0x97) {
				SendUserSysex(tr, tick, userSx[cmd - 0x90], (unsigned char)p1, (unsigned char)p2,
					(unsigned char)ch, port, rDev, rMod, yDev, yMod);
			} else if (cmd == 0x98) {
				unsigned char buf[256];
				int n = 0;
				buf[n++] = 0xf0;
				buf[n++] = (unsigned char)(p1 & 0x7f);
				buf[n++] = (unsigned char)(p2 & 0x7f);
				while (ip + evSize <= dataEnd && n < 240) {
					unsigned char c2;
					unsigned dly2 = 0, a1 = 0, a2 = 0;
					if (!readEv(ip, c2, dly2, a1, a2)) break;
					if (c2 != 0xF7) break;
					ip += evSize;
					buf[n++] = (unsigned char)(a1 & 0x7f);
					buf[n++] = (unsigned char)(a2 & 0x7f);
				}
				buf[n++] = 0xf7;
				SmfSysex(tr, tick, buf, (unsigned)n, port);
			} else if (cmd == 0xE2) {
				SmfPush(tr, tick, (unsigned char)(0xb0 | ch), 0, (unsigned char)(p2 & 0x7f), 3, port);
				SmfPush(tr, tick, (unsigned char)(0xc0 | ch), (unsigned char)(p1 & 0x7f), 0, 2, port);
			} else if (cmd == 0xE6) {
				int v = (int)p1;
				if (v == 0) skipNotes = 1;
				else {
					skipNotes = mute ? 1 : 0;
					int mc = v - 1;
					port = (unsigned char)((mc >> 4) & 1);
					ch = mc & 15;
				}
			} else if (cmd == 0xE7) {
				tempoMul = (int)p1;
				if (tempoMul < 1) tempoMul = 0x40;
				unsigned bpm = (unsigned)tempoBpm * (unsigned)tempoMul / 0x40u;
				if (bpm < 8) bpm = 8;
				if (bpm > 400) bpm = 400;
				SmfTempo(tr, tick, 60000000u / bpm);
			} else if (cmd == 0xEA)
				SmfPush(tr, tick, (unsigned char)(0xd0 | ch), (unsigned char)(p1 & 0x7f), 0, 2, port);
			else if (cmd == 0xEB)
				SmfPush(tr, tick, (unsigned char)(0xb0 | ch), (unsigned char)(p1 & 0x7f), (unsigned char)(p2 & 0x7f), 3, port);
			else if (cmd == 0xEC)
				SmfPush(tr, tick, (unsigned char)(0xc0 | ch), (unsigned char)(p1 & 0x7f), 0, 2, port);
			else if (cmd == 0xED)
				SmfPush(tr, tick, (unsigned char)(0xa0 | ch), (unsigned char)(p1 & 0x7f), (unsigned char)(p2 & 0x7f), 3, port);
			else if (cmd == 0xEE) {
				SmfPush(tr, tick, (unsigned char)(0xe0 | ch), (unsigned char)(p1 & 0x7f), (unsigned char)(p2 & 0x7f), 3, port);
			} else if (cmd == 0xDD) {
				raH = (unsigned char)(p1 & 0x7f);
				raM = (unsigned char)(p2 & 0x7f);
			} else if (cmd == 0xDF) {
				rDev = (unsigned char)(p1 & 0x7f);
				rMod = (unsigned char)(p2 & 0x7f);
			} else if (cmd == 0xDE) {
				unsigned char sx[12];
				sx[0] = 0xf0; sx[1] = 0x41; sx[2] = rDev; sx[3] = rMod; sx[4] = 0x12;
				sx[5] = raH; sx[6] = raM;
				sx[7] = (unsigned char)(p1 & 0x7f);
				sx[8] = (unsigned char)(p2 & 0x7f);
				sx[9] = (unsigned char)RolandSum(sx + 5, 4);
				sx[10] = 0xf7;
				SmfSysex(tr, tick, sx, 11, port);
			} else if (cmd == 0xD0) {
				yaH = (unsigned char)(p1 & 0x7f);
				yaM = (unsigned char)(p2 & 0x7f);
			} else if (cmd == 0xD1) {
				yDev = (unsigned char)(p1 & 0x7f);
				yMod = (unsigned char)(p2 & 0x7f);
			} else if (cmd == 0xD2 || cmd == 0xD3) {
				unsigned char sx[12];
				int n = 0;
				sx[n++] = 0xf0; sx[n++] = 0x43;
				if (cmd == 0xD3) {
					sx[n++] = 0x10; sx[n++] = 0x4c;
				} else {
					sx[n++] = yDev; sx[n++] = yMod;
				}
				sx[n++] = yaH; sx[n++] = yaM;
				sx[n++] = (unsigned char)(p1 & 0x7f);
				sx[n++] = (unsigned char)(p2 & 0x7f);
				sx[n++] = 0xf7;
				SmfSysex(tr, tick, sx, (unsigned)n, port);
			} else if (cmd == 0xC0) {
				unsigned char sx[8] = { 0xf0, 0x43, (unsigned char)(0x10 | (ch & 15)), 0x08,
					(unsigned char)(p1 & 0x7f), (unsigned char)(p2 & 0x7f), 0xf7 };
				SmfSysex(tr, tick, sx, 7, port);
			} else if (cmd == 0xDC) {
				unsigned char sx[8] = { 0xf0, 0x41, 0x32, (unsigned char)(ch & 15),
					(unsigned char)(p1 & 0x7f), (unsigned char)(p2 & 0x7f), 0xf7 };
				SmfSysex(tr, tick, sx, 7, port);
			}

			if (cmd < 0xF0)
				tick += delay;
			(void)thisOff; (void)rhythm;
		}
		FlushOffs(tr, offs, tick + tickQ * 4);
		pos += trkLen;
	}

	if (noteN <= 0) return 0;
	SmfBuild(trks, tickQ, mid);
	return mid.size() > 22 ? 1 : 0;
}

static int ConvertEupMem(const unsigned char* d, unsigned size, std::vector<unsigned char>& mid)
{
	if (!d || size < 200 || d[0] != 'S' || d[1] != 'O') return 0;
	unsigned pos = 16;
	if (pos >= size) return 0;
	unsigned nInst = d[pos];
	pos = 16 + 2 + nInst * 48;
	if (pos + 32 * 4 + 20 >= size) return 0;
	unsigned char mute[32], ch[32], vol[32], trn[32];
	memcpy(mute, d + pos, 32);
	memcpy(ch, d + pos + 32, 32);
	memcpy(vol, d + pos + 64, 32);
	memcpy(trn, d + pos + 96, 32);
	pos += 32 * 4 + 8 + 6 + 4 + 1;
	if (pos + 4 > size) return 0;
	unsigned tempo = d[pos++];
	pos += 2;
	if (tempo < 8 || tempo > 250) tempo = 120;
	const unsigned ppqn = 120;
	std::vector<SmfTrk> trks;
	trks.resize(2);
	SmfTempo(trks[0], 0, 60000000u / tempo);
	unsigned tick = 0;
	unsigned ip = pos;
	while (ip + 6 <= size) {
		unsigned char cmd = d[ip];
		if (cmd == 0) { ip++; continue; }
		/* Towns EUP channel event: often 6 bytes. Fallback: note stream. */
		unsigned char trk = d[ip];
		unsigned char note = (ip + 1 < size) ? d[ip + 1] : 0;
		unsigned char vel = (ip + 2 < size) ? d[ip + 2] : 64;
		unsigned char gate = (ip + 3 < size) ? d[ip + 3] : 12;
		unsigned step = (ip + 5 < size) ? (unsigned)d[ip + 4] | ((unsigned)d[ip + 5] << 8) : 12;
		ip += 6;
		if (trk >= 32) {
			if (trk == 0xff) break;
			continue;
		}
		if (mute[trk] == 0) continue;
		int midiCh = ch[trk] & 15;
		int nn = (int)(note & 0x7f) + (signed char)trn[trk];
		if (nn < 0) nn = 0;
		if (nn > 127) nn = 127;
		unsigned char vv = vel & 0x7f;
		if (!vv) vv = (unsigned char)(vol[trk] & 0x7f);
		if (vv > 127) vv = 127;
		if (note && vel) {
			SmfPush(trks[1], tick, (unsigned char)(0x90 | midiCh), (unsigned char)nn, vv, 3, 0);
			SmfPush(trks[1], tick + (gate ? gate : 1), (unsigned char)(0x80 | midiCh), (unsigned char)nn, 0x40, 3, 0);
		}
		tick += step ? step : 1;
		if (tick > 120u * 600u) break;
	}
	SmfBuild(trks, ppqn, mid);
	return mid.size() > 22 ? 1 : 0;
}

static int CopySmfMem(const unsigned char* d, unsigned size, std::vector<unsigned char>& mid)
{
	if (!d || size < 14) return 0;
	if (memcmp(d, "MThd", 4) == 0) {
		mid.assign(d, d + size);
		return 1;
	}
	if (size >= 20 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "RMID", 4) == 0) {
		unsigned off = 12;
		while (off + 8 <= size) {
			unsigned cksz = d[off + 4] | ((unsigned)d[off + 5] << 8)
				| ((unsigned)d[off + 6] << 16) | ((unsigned)d[off + 7] << 24);
			if (memcmp(d + off, "data", 4) == 0) {
				unsigned ds = off + 8;
				if (ds + cksz <= size && cksz >= 14 && memcmp(d + ds, "MThd", 4) == 0) {
					mid.assign(d + ds, d + ds + cksz);
					return 1;
				}
			}
			off += 8 + cksz + (cksz & 1);
		}
	}
	return 0;
}

static int ZmsHexByte(const char*& p)
{
	while (*p == ' ' || *p == '\t' || *p == '$' || *p == ',' || *p == '{' || *p == '}')
		p++;
	if (!*p) return -1;
	auto nibble = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	int hi = nibble(*p);
	if (hi < 0) {
		if (*p == '/') return -2;
		return -1;
	}
	p++;
	int lo = nibble(*p);
	if (lo >= 0) p++;
	else { lo = hi; hi = 0; }
	return (hi << 4) | lo;
}

static unsigned ZmsLenTicks(int defLen, int num, unsigned ppqn)
{
	int L = (num > 0) ? num : defLen;
	if (L <= 0) L = 4;
	/* l4 = 四分。ppqn が四分のティック */
	unsigned t = (ppqn * 4u) / (unsigned)L;
	if (!t) t = 1;
	return t;
}

static int ZmsNoteVal(int noteBase, int oct, int acc)
{
	int n = (oct + 1) * 12 + noteBase + acc;
	if (n < 0) n = 0;
	if (n > 127) n = 127;
	return n;
}

/* Z-MUSIC MIDI MML → SMF。OPM 専用コマンドは飛ばして MIDI トラックを鳴らす */
static int ConvertZmsMem(const unsigned char* d, unsigned size, std::vector<unsigned char>& mid)
{
	if (!d || size < 8 || size > 8 * 1024 * 1024) return 0;
	std::string text((const char*)d, (const char*)d + size);
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\r') text[i] = '\n';
		if (text[i] == '\t') text[i] = ' ';
	}
	const unsigned ppqn = 48;
	std::vector<SmfTrk> trks;
	trks.resize(17);
	unsigned tempoBpm = 120;
	wchar_t titleW[256] = {};
	int assignCh[32];
	for (int i = 0; i < 32; i++) assignCh[i] = (i < 16) ? i : 0;

	struct St {
		int ch, oct, defLen, vel, vol, q, pan, prog;
		unsigned tick;
		int lastNote, lastLen;
		int tie;
	};
	St st[32];
	memset(st, 0, sizeof(st));
	for (int i = 0; i < 32; i++) {
		st[i].ch = i % 16;
		st[i].oct = 4;
		st[i].defLen = 4;
		st[i].vel = 80;
		st[i].vol = 100;
		st[i].q = 8;
		st[i].pan = 64;
		st[i].prog = -1;
	}
	int curTrk = 0;

	auto emitSyx = [&](const std::vector<unsigned char>& sx) {
		if (sx.size() >= 2)
			SmfSysex(trks[0], 0, sx.data(), (unsigned)sx.size(), 0);
	};

	size_t pos = 0;
	while (pos < text.size()) {
		while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n')) pos++;
		if (pos >= text.size()) break;
		if (text[pos] == '/') {
			while (pos < text.size() && text[pos] != '\n') pos++;
			continue;
		}
		if (text[pos] == '.' ) {
			const char* line = text.c_str() + pos;
			if (_strnicmp(line, ".COMMENT", 8) == 0) {
				const char* p = line + 8;
				while (*p == ' ') p++;
				char tmp[200];
				int n = 0;
				while (*p && *p != '\n' && n < 199) tmp[n++] = *p++;
				tmp[n] = 0;
				MultiByteToWideChar(932, 0, tmp, -1, titleW, 256);
				unsigned char raw[200];
				memcpy(raw, tmp, (size_t)n);
				SmfMeta(trks[0], 0, 0x03, raw, (unsigned)n);
				pos += (size_t)(p - line);
				continue;
			}
			if (_strnicmp(line, ".ROLAND_EXCLUSIVE", 17) == 0) {
				const char* p = line + 17;
				std::vector<unsigned char> sx;
				sx.push_back(0xf0);
				sx.push_back(0x41);
				int v;
				while ((v = ZmsHexByte(p)) >= 0)
					sx.push_back((unsigned char)(v & 0x7f));
				if (sx.size() >= 4) {
					unsigned sum = 0;
					for (size_t i = 4; i < sx.size(); ++i) sum += sx[i];
					sx.push_back((unsigned char)((0x80 - (sum & 0x7f)) & 0x7f));
					sx.push_back(0xf7);
					emitSyx(sx);
				}
				while (pos < text.size() && text[pos] != '\n') pos++;
				continue;
			}
			while (pos < text.size() && text[pos] != '\n') pos++;
			continue;
		}
		if (text[pos] == '(') {
			pos++;
			char tag[16] = {};
			int ti = 0;
			while (pos < text.size() && text[pos] != ')' && text[pos] != ',' && ti < 14) {
				tag[ti++] = (char)text[pos++];
			}
			int arg = 0, arg2 = 0;
			if (pos < text.size() && text[pos] == ',') {
				pos++;
				while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
					arg = arg * 10 + (text[pos++] - '0');
			}
			if (pos < text.size() && text[pos] == ',') {
				pos++;
				while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
					arg2 = arg2 * 10 + (text[pos++] - '0');
			}
			while (pos < text.size() && text[pos] != ')') pos++;
			if (pos < text.size() && text[pos] == ')') pos++;
			if ((tag[0] == 't' || tag[0] == 'T') && tag[1] >= '0') {
				curTrk = atoi(tag + 1);
				if (curTrk < 1) curTrk = 1;
				if (curTrk > 16) curTrk = 16;
			} else if ((tag[0] == 'a' || tag[0] == 'A') && tag[1] >= '0') {
				int tr = atoi(tag + 1);
				int ch = arg ? arg : arg2;
				if (tr >= 1 && tr <= 16 && ch >= 1 && ch <= 16)
					assignCh[tr - 1] = ch - 1;
			} else if ((tag[0] == 'o' || tag[0] == 'O') && tag[1] >= '0') {
				int v = atoi(tag + 1);
				if (v >= 32 && v <= 300) {
					tempoBpm = (unsigned)v;
					SmfTempo(trks[0], 0, 60000000u / tempoBpm);
				}
			}
			continue;
		}
		/* MML for current track */
		if (curTrk < 1 || curTrk > 16) {
			pos++;
			continue;
		}
		St& s = st[curTrk - 1];
		s.ch = assignCh[curTrk - 1] & 15;
		SmfTrk& tr = trks[curTrk];
		char c = text[pos];
		if (c == '\n') { pos++; continue; }
		if (c == '[') {
			while (pos < text.size() && text[pos] != ']') pos++;
			if (pos < text.size()) pos++;
			continue;
		}
		if (c == '|' && pos + 1 < text.size() && text[pos + 1] == ':') {
			pos += 2;
			int times = 0;
			while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
				times = times * 10 + (text[pos++] - '0');
			(void)times;
			continue;
		}
		if (c == ':' && pos + 1 < text.size() && text[pos + 1] == '|') {
			pos += 2;
			continue;
		}
		if (c == 'n' || c == 'N') {
			pos++;
			int chn = 0;
			while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
				chn = chn * 10 + (text[pos++] - '0');
			if (chn >= 1 && chn <= 16) s.ch = chn - 1;
			continue;
		}
		if (c == '@') {
			pos++;
			char k = (pos < text.size()) ? text[pos] : 0;
			if (k == 'v' || k == 'V' || k == 'p' || k == 'P' || k == 'u' || k == 'U') {
				pos++;
				int v = 0;
				while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
					v = v * 10 + (text[pos++] - '0');
				if (k == 'v') s.vel = v;
				else if (k == 'V') {
					s.vol = v;
					SmfPush(tr, s.tick, (unsigned char)(0xb0 | s.ch), 7, (unsigned char)(v > 127 ? 127 : v), 3, 0);
				} else if (k == 'p' || k == 'P') {
					s.pan = v;
					SmfPush(tr, s.tick, (unsigned char)(0xb0 | s.ch), 10, (unsigned char)(v > 127 ? 127 : v), 3, 0);
				}
				continue;
			}
			int v = 0, digits = 0;
			while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
				v = v * 10 + (text[pos++] - '0');
				digits++;
			}
			if (digits) {
				s.prog = v;
				SmfPush(tr, s.tick, (unsigned char)(0xc0 | s.ch), (unsigned char)(v & 0x7f), 0, 2, 0);
			}
			continue;
		}
		if (c == 'o' || c == 'O') {
			pos++;
			int v = 0;
			while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
				v = v * 10 + (text[pos++] - '0');
			if (v >= 0 && v <= 9) s.oct = v;
			continue;
		}
		if (c == 'l' || c == 'L') {
			pos++;
			int v = 0;
			while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
				v = v * 10 + (text[pos++] - '0');
			if (v > 0) s.defLen = v;
			continue;
		}
		if (c == 'q' || c == 'Q') {
			pos++;
			int v = 0;
			while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
				v = v * 10 + (text[pos++] - '0');
			if (v > 0) s.q = v;
			continue;
		}
		if (c == '>') { s.oct++; if (s.oct > 9) s.oct = 9; pos++; continue; }
		if (c == '<') { s.oct--; if (s.oct < 0) s.oct = 0; pos++; continue; }
		if (c == '&') { s.tie = 1; pos++; continue; }
		int noteBase = -1;
		if (c == 'c' || c == 'C') noteBase = 0;
		else if (c == 'd' || c == 'D') noteBase = 2;
		else if (c == 'e' || c == 'E') noteBase = 4;
		else if (c == 'f' || c == 'F') noteBase = 5;
		else if (c == 'g' || c == 'G') noteBase = 7;
		else if (c == 'a' || c == 'A') noteBase = 9;
		else if (c == 'b' || c == 'B') noteBase = 11;
		else if (c == 'r' || c == 'R') noteBase = -2;
		if (noteBase == -1) { pos++; continue; }
		pos++;
		int acc = 0;
		while (pos < text.size() && (text[pos] == '+' || text[pos] == '#' || text[pos] == '-')) {
			if (text[pos] == '-') acc--;
			else acc++;
			pos++;
		}
		int lenNum = 0;
		while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
			lenNum = lenNum * 10 + (text[pos++] - '0');
		int dots = 0;
		while (pos < text.size() && text[pos] == '.') { dots++; pos++; }
		unsigned dur = ZmsLenTicks(s.defLen, lenNum, ppqn);
		unsigned add = dur / 2;
		for (int i = 0; i < dots; i++) { dur += add; add /= 2; }
		if (noteBase == -2) {
			s.tick += dur;
			s.tie = 0;
			continue;
		}
		int nn = ZmsNoteVal(noteBase, s.oct, acc);
		unsigned gate = dur;
		if (s.q > 0 && s.q < 8) {
			gate = dur * (unsigned)s.q / 8u;
			if (!gate) gate = 1;
		}
		unsigned char vv = (unsigned char)(s.vel > 127 ? 127 : (s.vel < 1 ? 1 : s.vel));
		if (s.tie && s.lastNote == nn) {
			/* タイ: 前のノートオフを延ばすので、ここでは on を重ねず tick だけ進める */
			s.tick += dur;
			s.lastLen += (int)dur;
			continue;
		}
		SmfPush(tr, s.tick, (unsigned char)(0x90 | s.ch), (unsigned char)nn, vv, 3, 0);
		SmfPush(tr, s.tick + gate, (unsigned char)(0x80 | s.ch), (unsigned char)nn, 0x40, 3, 0);
		s.tick += dur;
		s.lastNote = nn;
		s.lastLen = (int)dur;
		s.tie = 0;
	}
	if (trks[0].ev.empty())
		SmfTempo(trks[0], 0, 60000000u / (tempoBpm ? tempoBpm : 120));
	int any = 0;
	for (size_t i = 1; i < trks.size(); ++i)
		if (!trks[i].ev.empty()) any = 1;
	if (!any) return 0;
	SmfBuild(trks, ppqn, mid);
	return mid.size() > 22 ? 1 : 0;
}

/* BALLADE / ミュージクン SNG。ヘッダの後は RCP に近い 4 バイトイベント */
static int ConvertSngMem(const unsigned char* d, unsigned size, std::vector<unsigned char>& mid)
{
	if (!d || size < 64) return 0;
	unsigned i = 0;
	while (i + 12 < size && memcmp(d + i, "BALLADE SONG", 12) != 0)
		i++;
	if (i + 12 >= size) return 0;
	unsigned eofMark = i;
	while (eofMark < size && d[eofMark] != 0x1a)
		eofMark++;
	if (eofMark >= size) return 0;
	unsigned nowAt = 0;
	for (unsigned p = eofMark; p + 4 < size && p < eofMark + 64; p++) {
		if (d[p] == 'n' && d[p + 1] == 'o' && d[p + 2] == 'w' && d[p + 3] == ' ') {
			nowAt = p;
			break;
		}
	}
	if (!nowAt) nowAt = eofMark + 1;
	unsigned titleAt = nowAt + 4;
	unsigned tableAt = titleAt + 6;
	if (tableAt + 8 > size) return 0;
	unsigned lens[16] = {};
	int nTrk = 0;
	unsigned sum = 0;
	for (int t = 0; t < 16; t++) {
		unsigned v = RcpU32(d + tableAt + (unsigned)t * 4);
		if (v == 0) continue;
		if (v >= size) break;
		if (sum + v > size) break;
		lens[nTrk++] = v;
		sum += v;
	}
	if (nTrk <= 0) return 0;
	unsigned dataAt = size - sum;
	if (dataAt < tableAt || dataAt >= size) {
		dataAt = tableAt + 16 * 4 + 32;
		if (dataAt + sum > size)
			dataAt = tableAt + (unsigned)nTrk * 4;
	}
	unsigned tempoBpm = 120;
	for (unsigned p = tableAt; p + 2 < dataAt && p + 2 < size; p++) {
		if (d[p] == 0x4b && d[p + 2] >= 32 && d[p + 2] <= 240) {
			tempoBpm = d[p + 2];
			break;
		}
	}
	const unsigned ppqn = 48;
	std::vector<SmfTrk> trks;
	trks.resize((size_t)nTrk + 1);
	SmfTempo(trks[0], 0, 60000000u / tempoBpm);
	unsigned off = dataAt;
	for (int t = 0; t < nTrk; t++) {
		unsigned ts = lens[t];
		if (off + ts > size) break;
		const unsigned char* trd = d + off;
		unsigned ip = 0;
		unsigned tick = 0;
		int ch = t % 16;
		std::vector<RcpNoteOff> offs;
		SmfTrk& tr = trks[t + 1];
		while (ip + 4 <= ts) {
			unsigned char st = trd[ip];
			unsigned char n1 = trd[ip + 1];
			unsigned char gt = trd[ip + 2];
			unsigned char vel = trd[ip + 3];
			ip += 4;
			FlushOffs(tr, offs, tick);
			if (n1 < 0x80) {
				if (n1 > 0 && vel > 0) {
					unsigned gate = gt ? gt : 1;
					SmfPush(tr, tick, (unsigned char)(0x90 | ch), n1, vel, 3, 0);
					RcpNoteOff no;
					no.tick = tick + gate;
					no.ch = (unsigned char)ch;
					no.note = n1;
					no.port = 0;
					offs.push_back(no);
				}
				tick += st ? st : 0;
			} else if (n1 == 0xfc || n1 == 0xfd) {
				break;
			} else if (n1 == 0xe7) {
				/* テンポ */
				if (gt >= 32 && gt <= 240)
					SmfTempo(trks[0], tick, 60000000u / gt);
				tick += st;
			} else if (n1 == 0xe2) {
				SmfPush(tr, tick, (unsigned char)(0xc0 | ch), gt, 0, 2, 0);
				tick += st;
			} else if (n1 == 0xeb) {
				SmfPush(tr, tick, (unsigned char)(0xb0 | ch), gt, vel, 3, 0);
				tick += st;
			} else {
				tick += st;
			}
			if (tick > ppqn * 600u * 8u) break;
		}
		FlushOffs(tr, offs, tick + ppqn * 4);
		off += ts;
	}
	int any = 0;
	for (size_t t = 1; t < trks.size(); ++t)
		if (!trks[t].ev.empty()) any = 1;
	if (!any) return 0;
	SmfBuild(trks, ppqn, mid);
	return mid.size() > 22 ? 1 : 0;
}

int ComposerConvertToMidi(const wchar_t* src, wchar_t* dest, int destChars)
{
	if (!src || !dest || destChars < 8) return 0;
	dest[0] = 0;
	wchar_t pack[MIDIPACK_PATH];
	if (MidiPackMaterialize(src, pack, MIDIPACK_PATH))
		src = pack;
	ComposerTempMidiPath(src, dest, destChars);
	if (!dest[0]) return 0;
	if (CacheValid(src, dest))
		return 1;

	std::vector<unsigned char> buf;
	if (!ReadAll(src, buf)) return 0;
	int kind = ComposerKindOfMem(buf.data(), (unsigned)buf.size());
	if (kind == COMPOSER_KIND_NONE) {
		if (ComposerEqExt(src, L".rcp") || ComposerEqExt(src, L".r36"))
			kind = COMPOSER_KIND_RCP;
		else if (ComposerEqExt(src, L".g36") || ComposerEqExt(src, L".g18"))
			kind = COMPOSER_KIND_G36;
		else if (ComposerEqExt(src, L".mcp") || ComposerEqExt(src, L".mtd"))
			kind = COMPOSER_KIND_MCP;
		else if (ComposerEqExt(src, L".eup"))
			kind = COMPOSER_KIND_EUP;
		else if (ComposerEqExt(src, L".mff") || ComposerEqExt(src, L".seq")
			|| ComposerEqExt(src, L".smf"))
			kind = COMPOSER_KIND_SMF;
		else if (ComposerEqExt(src, L".sng"))
			kind = COMPOSER_KIND_SNG;
		else if (ComposerEqExt(src, L".zms"))
			kind = COMPOSER_KIND_ZMS;
	}
	if (kind == COMPOSER_KIND_GSD || kind == COMPOSER_KIND_CM6)
		return 0;

	std::vector<unsigned char> mid;
	int ok = 0;
	if (kind == COMPOSER_KIND_SMF)
		ok = CopySmfMem(buf.data(), (unsigned)buf.size(), mid);
	else if (kind == COMPOSER_KIND_EUP)
		ok = ConvertEupMem(buf.data(), (unsigned)buf.size(), mid);
	else if (kind == COMPOSER_KIND_RCP || kind == COMPOSER_KIND_G36 || kind == COMPOSER_KIND_MCP)
		ok = ConvertRcpMem(buf.data(), (unsigned)buf.size(),
			(kind == COMPOSER_KIND_G36) ? COMPOSER_KIND_G36 : COMPOSER_KIND_RCP, mid);
	else if (kind == COMPOSER_KIND_SNG)
		ok = ConvertSngMem(buf.data(), (unsigned)buf.size(), mid);
	else if (kind == COMPOSER_KIND_ZMS)
		ok = ConvertZmsMem(buf.data(), (unsigned)buf.size(), mid);
	if (!ok || mid.size() < 22) return 0;
	if (!WriteAll(dest, mid.data(), (unsigned)mid.size())) return 0;
	CopyComposerSidecars(src, dest);
	return 1;
}
