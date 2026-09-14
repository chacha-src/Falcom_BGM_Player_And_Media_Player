#include "stdafx.h"
#include "ComposerConvert.h"
#include "MidiPack.h"
#include <vector>
#include <algorithm>

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
		|| ComposerEqExt(path, L".mff") || ComposerEqExt(path, L".seq");
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
	if (ComposerEqExt(path, L".mff"))
		return COMPOSER_KIND_SMF;
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
		if (*p == L'\\' || *p == L'/')
			name = p + 1;
	}
	wchar_t stem[MAX_PATH];
	wcsncpy_s(stem, name, _TRUNCATE);
	wchar_t* dot = wcsrchr(stem, L'.');
	if (dot && dot != stem)
		*dot = 0;
	if (!stem[0])
		wcsncpy_s(stem, L"composer", _TRUNCATE);
	_snwprintf_s(dest, destChars, _TRUNCATE, L"%s\\%s.mid", dir, stem);
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
		|| ComposerFindSidecar(src, L".WRD", out, outChars);
}

int ComposerHasSidecarWrd(const wchar_t* src)
{
	if (MidiPackIsVirtualPath(src))
		return MidiPackHasSidecarWrd(src);
	wchar_t tmp[MAX_PATH];
	return ComposerFindSidecarWrd(src, tmp, MAX_PATH);
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
	return (du.QuadPart >= su.QuadPart && ss > 0) ? 1 : 0;
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
		int skipNotes = mute ? 1 : 0;
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
					tgt = pos + (p1 | (p2 << 8));
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

int ComposerConvertToMidi(const wchar_t* src, wchar_t* dest, int destChars)
{
	if (!src || !dest || destChars < 8) return 0;
	dest[0] = 0;
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
		else if (ComposerEqExt(src, L".mff") || ComposerEqExt(src, L".seq"))
			kind = COMPOSER_KIND_SMF;
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
	if (!ok || mid.size() < 22) return 0;
	if (!WriteAll(dest, mid.data(), (unsigned)mid.size())) return 0;
	return 1;
}
