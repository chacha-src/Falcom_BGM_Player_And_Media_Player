#include "sasami_midi.h"
#include "sasami_misao.h"

#include <windows.h>
#include <algorithm>
#include <string.h>
#include <stdio.h>

/* kpi 本体は kbsasami_module.cpp が持つ。ogg / KpiHost32 はここ。NULL ならリソースを見ずファイルを探す。 */
#ifdef KBSASAMI_PLUGIN
extern HINSTANCE g_hKpi;
#else
HINSTANCE g_hKpi = NULL;
#endif

namespace {

struct MidiEv {
	uint32_t tick;
	int port;
	int seq;
	uint8_t len;
	uint8_t trk;
	uint8_t pass;
	uint8_t bytes[128];
};

struct MidiTrackState {
	uint32_t addr;
	int count;
	int part;
	int port;
	int note;
	int vel;
	enum { MIDI_LOOP_NEST = 16 };
	int loopStack[MIDI_LOOP_NEST];
	int loopSp;
	int drum;
	int backJumps;
	int loopSafety;
	int alive;
	int everJump;
	int pedal;
	int pass;
	uint32_t loopStartTick;
	uint32_t loopEndTick;
	uint32_t loopDest; /* 直近の戻り先。変わったら変異ループ */
	uint32_t period;   /* その戻り先の 1 周 */
	/* Wave3 soft FX (cmd 46/47) — expand into SMF curves while note is held */
	int softMode;   /* -1 off, 0=vib, 1=trem */
	int softDelay;
	int softDepth;
	int softPhase;
	int portaSemi;  /* signed, 0=off */
	int portaDelay;
	int portaGlide;
	int portaLeft;
};

enum { SASAMI_MAX_EV = 49152, SASAMI_MAX_FIRST = 16384 };
enum { TRK0_CAP = 256 * 1024, TRK1_CAP = 512 * 1024, TRK2_CAP = 512 * 1024 };

static MidiEv* s_evs;
static int s_evCount;
static struct { uint64_t key; uint32_t tick; } s_first[SASAMI_MAX_FIRST];
static int s_firstCount;
static uint8_t* s_trk0;
static uint8_t* s_trk1;
static uint8_t* s_trk2;
static int s_trkLen[3];
static uint8_t* s_smfWork;
static int s_evSeq;
static int s_midiReady;
static int s_pushTrk;
static int s_pushPass;

static int EnsureMidiWork()
{
	if (s_midiReady) return 1;
	s_evs = (MidiEv*)VirtualAlloc(NULL, sizeof(MidiEv) * (SIZE_T)SASAMI_MAX_EV, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	s_trk0 = (uint8_t*)VirtualAlloc(NULL, TRK0_CAP, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	s_trk1 = (uint8_t*)VirtualAlloc(NULL, TRK1_CAP, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	s_trk2 = (uint8_t*)VirtualAlloc(NULL, TRK2_CAP, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	s_smfWork = (uint8_t*)VirtualAlloc(NULL, SASAMI_MAX_SMF, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!s_evs || !s_trk0 || !s_trk1 || !s_trk2 || !s_smfWork)
		return 0;
	s_midiReady = 1;
	return 1;
}

static void PutBe32(uint8_t* buf, int* len, int cap, uint32_t v)
{
	if (*len + 4 > cap) return;
	buf[(*len)++] = (uint8_t)(v >> 24);
	buf[(*len)++] = (uint8_t)(v >> 16);
	buf[(*len)++] = (uint8_t)(v >> 8);
	buf[(*len)++] = (uint8_t)v;
}

static void PutBe16(uint8_t* buf, int* len, int cap, uint16_t v)
{
	if (*len + 2 > cap) return;
	buf[(*len)++] = (uint8_t)(v >> 8);
	buf[(*len)++] = (uint8_t)v;
}

static void PutVlq(uint8_t* buf, int* len, int cap, uint32_t v)
{
	uint8_t b[5];
	int n = 0;
	b[n++] = (uint8_t)(v & 0x7F);
	v >>= 7;
	while (v) {
		b[n++] = (uint8_t)((v & 0x7F) | 0x80);
		v >>= 7;
	}
	while (n--) {
		if (*len >= cap) return;
		buf[(*len)++] = b[n];
	}
}

static void PushEv(uint32_t tick, int port, const uint8_t* d, int n)
{
	if (!d || n <= 0 || n > 128) return;
	if (s_evCount >= SASAMI_MAX_EV) return;
	MidiEv& e = s_evs[s_evCount++];
	e.tick = tick;
	e.port = port;
	e.seq = s_evSeq++;
	e.len = (uint8_t)n;
	e.trk = (uint8_t)((s_pushTrk >= 0 && s_pushTrk < 64) ? s_pushTrk : 255);
	e.pass = (uint8_t)((s_pushPass > 0) ? s_pushPass : 0);
	memcpy(e.bytes, d, (size_t)n);
}

static void PushShort(uint32_t tick, int port, uint8_t st, uint8_t a, uint8_t b)
{
	uint8_t d[3] = { st, a, b };
	const int n = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 2 : 3;
	PushEv(tick, port, d, n);
}

// MMODE1J / MMJ4: per-channel init (pitch bend range ±2 octaves, GS bank, volumes).
static void PushMmodeChannelInit(uint32_t tick, int port, int ch, SasamiMidiMap map, int gsBankLsb, int flg88, int laBankMsb)
{
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x65, 0);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x64, 0);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x06, 0x18);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x01, 0);
	/* LA / MT-32 を SC-VA 等で鳴らすときは Capital（MSB 127, LSB 0）。
	   チャンネル 10 はリズムのまま。実機 MT-32 へバンクを出さない判断は
	   変換後、MIDI 出力側で行う。 */
	if (laBankMsb == 127 && ch != 9) {
		PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x00, 127);
		PushShort(tick, port, (uint8_t)(0xB0 | ch), 32, 0);
	} else {
		PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x00, 0);
		if (flg88 != 2 && gsBankLsb >= 1 && gsBankLsb <= 4) {
			/* 判定できたときだけ。未判定を SC-88 の CC32=2 にしない。 */
			PushShort(tick, port, (uint8_t)(0xB0 | ch), 32, (uint8_t)gsBankLsb);
		}
	}
	(void)map;
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 7, 100);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 10, 64);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 11, 127);
	PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x40, 0);
	PushShort(tick, port, (uint8_t)(0xE0 | ch), 0x00, 0x40);
}

static void PushGs(uint32_t tick, int port, uint8_t a, uint8_t b, uint8_t c, uint8_t v)
{
	uint8_t sx[11] = { 0xF0, 0x41, 0x10, 0x42, 0x12, a, b, c, v, 0, 0xF7 };
	int s = (int)a + (int)b + (int)c + (int)v;
	sx[9] = (uint8_t)((128 - (s % 128)) & 0x7F);
	PushEv(tick, port, sx, 11);
}

static void PushExclBody(uint32_t tick, int port, int xg, uint8_t devId, const uint8_t* body, int bodyLen)
{
	if (!body || bodyLen <= 0 || bodyLen > 110) return;
	uint8_t sx[128];
	int n = 0;
	int sum = 0;
	if (xg) {
		sx[n++] = 0xF0;
		sx[n++] = 0x43;
		sx[n++] = 0x10;
		sx[n++] = devId;
	} else {
		sx[n++] = 0xF0;
		sx[n++] = 0x41;
		sx[n++] = 0x10;
		sx[n++] = devId;
		sx[n++] = 0x12;
	}
	for (int i = 0; i < bodyLen; i++) {
		sx[n] = body[i];
		sum += sx[n];
		n++;
	}
	if (!xg)
		sx[n++] = (uint8_t)((128 - (sum % 128)) & 0x7F);
	sx[n++] = 0xF7;
	PushEv(tick, port, sx, n);
}

static int GsPartIdx(int ch)
{
	if (ch == 9) return 0;
	if (ch < 9) return ch + 1;
	return ch;
}

/* SC-55/SC-88 に無い 88Pro EFX（40 03 / 40 4n 22）。55map の排他に混ざる。 */
static int MidiIsGsEfxDt1(const uint8_t* d, int n)
{
	int i = 0;
	if (!d || n < 8) return 0;
	if (d[0] == 0xF0) i = 1;
	if (n - i < 7) return 0;
	if (d[i] != 0x41) return 0;
	if (d[i + 2] != 0x42 || d[i + 3] != 0x12) return 0;
	if (d[i + 4] != 0x40) return 0;
	const unsigned bb = d[i + 5];
	const unsigned cc = (n - i > 6) ? d[i + 6] : 0;
	if (bb == 0x03) return 1;
	if ((bb & 0xF0) == 0x40 && cc == 0x22) return 1;
	return 0;
}

static uint32_t ReadJump(const SasamiSong& s, uint32_t addr, int ver, uint32_t* nextOff)
{
	/* Composer の .mpw2 はストリーム内 J/:| が 16bit のまま。
	   本家 SASAMI11 はヘッダ EE EE EE のとき 24bit（cmd + 3byte、:| 抜けは +4）。
	   常に 24bit にすると次命令の 1byte を上位に読んで全部の J が外れる。
	   16bit 先が命令境界でなく、24bit 先が命令境界のときだけ 24bit。 */
	const uint16_t a16 = SasamiGet16(s, addr + 1);
	uint32_t d16 = (a16 >= 0x1000) ? (uint32_t)(a16 - 0x1000) : (uint32_t)a16;
	*nextOff = addr + 3;
	if (d16 == 0xF0 || d16 == 0)
		return d16;
	if (ver >= 2 && SasamiOffOk(s, addr, 4)) {
		const uint32_t a24 = SasamiGet24(s, addr + 1);
		const uint32_t d24 = (a24 >= 0x1000) ? (a24 - 0x1000) : a24;
		if (d24 != d16 && SasamiOffOk(s, d24, 1)) {
			const int ok16 = SasamiOffOk(s, d16, 1) && SasamiGet(s, d16) <= 47;
			const int ok24 = SasamiGet(s, d24) <= 47;
			if (ok24 && !ok16) {
				*nextOff = addr + 4;
				return d24;
			}
		}
	}
	return d16;
}

/* Q の tick は、そのアドレスの命令。±16 で次の音符へ寄せると J の戻りが Q より後ろになる。 */
static uint32_t DestFirstTick(int track, uint32_t dest, uint32_t fileOff)
{
	uint32_t exact = 0xFFFFFFFFu;
	uint32_t nearA = 0xFFFFFFFFu;
	uint32_t nearT = 0xFFFFFFFFu;
	uint32_t nearD = 4;
	for (int fi = 0; fi < s_firstCount; fi++) {
		if ((s_first[fi].key >> 32) != (uint64_t)(unsigned)track) continue;
		const uint32_t a = (uint32_t)s_first[fi].key;
		if (a == dest) {
			exact = s_first[fi].tick;
			break;
		}
		const uint32_t d = (a > dest) ? (a - dest) : (dest - a);
		if (d < nearD || (d == nearD && a < nearA)) {
			nearD = d;
			nearA = a;
			nearT = s_first[fi].tick;
		}
	}
	if (exact != 0xFFFFFFFFu) return exact;
	if (nearD <= 3 && nearT != 0xFFFFFFFFu) return nearT;
	if (dest == 0 || dest == fileOff) return 0;
	return 0xFFFFFFFFu;
}

static uint32_t GcdU32(uint32_t a, uint32_t b)
{
	while (b) {
		const uint32_t t = a % b;
		a = b;
		b = t;
	}
	return a;
}

/* 各トラックの Q/J は長さが揃わない。一番長い1本ではなく、周期の公倍数で
   全員が同じ位相に戻る長さにする。戻り先が変わる変異は、その後に取り直す。 */
static uint32_t LcmU32(uint32_t a, uint32_t b)
{
	if (!a) return b;
	if (!b) return a;
	const uint32_t g = GcdU32(a, b);
	if (!g) return 0;
	if (a / g > 0xFFFFFFFFu / b) return 0xFFFFFFFFu;
	return (a / g) * b;
}

/* 最長周期の J が 1 小節（192tick）以内に散らばるとき、多数派の J で曲ループを切る。
   いちばん遅い Q に合わせると、先に戻ったトラックがその 1 小節だけ Q を再演奏し、
   遅い J と重なる。 */
static uint32_t MajorityLongEnd(const MidiTrackState* tr, int ntr, uint32_t lcm)
{
	uint32_t jmin = 0xFFFFFFFFu, jmax = 0, best = 0;
	int bestN = 0;
	if (!tr || lcm == 0 || lcm == 0xFFFFFFFFu) return 0;
	for (int i = 0; i < ntr && i < 64; i++) {
		if (tr[i].period != lcm || tr[i].loopEndTick == 0) continue;
		if (tr[i].loopEndTick < jmin) jmin = tr[i].loopEndTick;
		if (tr[i].loopEndTick > jmax) jmax = tr[i].loopEndTick;
	}
	if (jmin == 0xFFFFFFFFu || jmax < jmin || jmax - jmin > 192) return 0;
	for (int i = 0; i < ntr && i < 64; i++) {
		if (tr[i].period != lcm || tr[i].loopEndTick == 0) continue;
		const uint32_t t = tr[i].loopEndTick;
		int c = 0;
		for (int k = 0; k < ntr && k < 64; k++) {
			if (tr[k].period == lcm && tr[k].loopEndTick == t) c++;
		}
		if (c > bestN || (c == bestN && (best == 0 || t < best))) {
			bestN = c;
			best = t;
		}
	}
	if (best < lcm) return 0;
	return best;
}

static uint8_t* TrkPtr(int t)
{
	if (t == 0) return s_trk0;
	if (t == 1) return s_trk1;
	return s_trk2;
}

static int TrkCap(int t)
{
	if (t == 0) return TRK0_CAP;
	if (t == 1) return TRK1_CAP;
	return TRK2_CAP;
}

static void WriteSmf(int nports, uint8_t* out, int outCap, int* outSize)
{
	std::stable_sort(s_evs, s_evs + s_evCount, [](const MidiEv& a, const MidiEv& b) {
		if (a.tick != b.tick) return a.tick < b.tick;
		return a.seq < b.seq;
	});

	if (nports < 1) nports = 1;
	if (nports > 2) nports = 2;
	const int ntr = 1 + nports;
	s_trkLen[0] = s_trkLen[1] = s_trkLen[2] = 0;
	uint32_t last[3] = { 0, 0, 0 };

	for (int p = 0; p < nports; p++) {
		const int tr = 1 + p;
		uint8_t* tb = TrkPtr(tr);
		int* tl = &s_trkLen[tr];
		const int tc = TrkCap(tr);
		PutVlq(tb, tl, tc, 0);
		if (*tl + 4 <= tc) {
			tb[(*tl)++] = 0xFF;
			tb[(*tl)++] = 0x21;
			tb[(*tl)++] = 0x01;
			tb[(*tl)++] = (uint8_t)p;
		}
	}

	for (int i = 0; i < s_evCount; i++) {
		const MidiEv& e = s_evs[i];
		if (e.len == 0) continue;
		int tr;
		if (e.len >= 2 && e.bytes[0] == 0xFF)
			tr = 0;
		else
			tr = 1 + (e.port ? 1 : 0);
		if (tr < 0 || tr >= ntr) continue;
		uint8_t* tb = TrkPtr(tr);
		int* tl = &s_trkLen[tr];
		const int tc = TrkCap(tr);
		PutVlq(tb, tl, tc, e.tick - last[tr]);
		last[tr] = e.tick;
		if (e.bytes[0] == 0xF0) {
			if (*tl + 1 > tc) continue;
			tb[(*tl)++] = 0xF0;
			PutVlq(tb, tl, tc, (uint32_t)(e.len - 1));
			const int rest = e.len - 1;
			if (*tl + rest > tc) continue;
			memcpy(tb + *tl, e.bytes + 1, (size_t)rest);
			*tl += rest;
		} else {
			if (*tl + e.len > tc) continue;
			memcpy(tb + *tl, e.bytes, e.len);
			*tl += e.len;
		}
	}
	for (int t = 0; t < ntr; t++) {
		uint8_t* tb = TrkPtr(t);
		int* tl = &s_trkLen[t];
		const int tc = TrkCap(t);
		PutVlq(tb, tl, tc, 0);
		if (*tl + 3 <= tc) {
			tb[(*tl)++] = 0xFF;
			tb[(*tl)++] = 0x2F;
			tb[(*tl)++] = 0x00;
		}
	}

	int olen = 0;
	if (outCap < 14) {
		if (outSize) *outSize = 0;
		return;
	}
	out[olen++] = 'M'; out[olen++] = 'T'; out[olen++] = 'h'; out[olen++] = 'd';
	PutBe32(out, &olen, outCap, 6);
	PutBe16(out, &olen, outCap, 1);
	PutBe16(out, &olen, outCap, (uint16_t)ntr);
	PutBe16(out, &olen, outCap, (uint16_t)SASAMI_PPQN);
	for (int t = 0; t < ntr; t++) {
		if (olen + 8 + s_trkLen[t] > outCap) {
			if (outSize) *outSize = 0;
			return;
		}
		out[olen++] = 'M'; out[olen++] = 'T'; out[olen++] = 'r'; out[olen++] = 'k';
		PutBe32(out, &olen, outCap, (uint32_t)s_trkLen[t]);
		memcpy(out + olen, TrkPtr(t), (size_t)s_trkLen[t]);
		olen += s_trkLen[t];
	}
	if (outSize) *outSize = olen;
}

} // namespace

static uint64_t SasamiCacheHashPathW(const wchar_t* path)
{
	uint64_t h = 14695981039346656037ULL;
	if (!path) return h;
	for (const wchar_t* p = path; *p; ++p) {
		wchar_t c = *p;
		if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
		if (c == L'/') c = L'\\';
		h ^= (uint64_t)(unsigned short)c;
		h *= 1099511628211ULL;
	}
	return h;
}

namespace {

static int DetIsAlnumW(wchar_t c)
{
	return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

static void DetCompactKey(const wchar_t* s, wchar_t* out, int outN)
{
	int j = 0;
	if (!out || outN <= 0) return;
	out[0] = 0;
	if (!s) return;
	for (; *s && j < outN - 1; ++s) {
		wchar_t c = *s;
		if (c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
		if (c == L'-' || c == L'_' || c == L' ' || c == L'\t' || c == L'.')
			continue;
		out[j++] = c;
	}
	out[j] = 0;
}

static int DetHasToken(const wchar_t* s, const wchar_t* tok)
{
	if (!s || !tok || !tok[0]) return 0;
	const int tn = (int)wcslen(tok);
	for (int i = 0; s[i]; ++i) {
		int j = 0;
		for (; tok[j] && s[i + j]; ++j) {
			wchar_t a = s[i + j];
			wchar_t b = tok[j];
			if (a >= L'a' && a <= L'z') a = (wchar_t)(a - 32);
			if (b >= L'a' && b <= L'z') b = (wchar_t)(b - 32);
			if (a != b) break;
		}
		if (tok[j]) continue;
		const wchar_t prev = (i > 0) ? s[i - 1] : 0;
		const wchar_t next = s[i + tn];
		if (!DetIsAlnumW(prev) && !DetIsAlnumW(next)) return 1;
		i += tn - 1;
	}
	return 0;
}

static int DetIsolated55(const wchar_t* s)
{
	if (!s || !s[0]) return 0;
	for (int i = 0; s[i]; ++i) {
		if (s[i] != L'5' || s[i + 1] != L'5') continue;
		const wchar_t prev = (i > 0) ? s[i - 1] : 0;
		const wchar_t next = s[i + 2];
		const int prevDig = (prev >= L'0' && prev <= L'9');
		const int nextDig = (next >= L'0' && next <= L'9');
		if (!prevDig && !nextDig) return 1;
		++i;
	}
	return 0;
}

static int DetKindFromCompact(const wchar_t* k)
{
	if (!k || !k[0]) return 0;
	if (wcsstr(k, L"8850") || wcsstr(k, L"SC8850") ||
		wcsstr(k, L"8820") || wcsstr(k, L"SC8820"))
		return 4;
	if (wcsstr(k, L"88PRO") || wcsstr(k, L"SC88PRO") || wcsstr(k, L"88PMAP"))
		return 3;
	if (wcsstr(k, L"88P"))
		return 3;
	if (wcsstr(k, L"88VL") || wcsstr(k, L"SC88VL") || wcsstr(k, L"88VALUE"))
		return 2;
	if (wcsstr(k, L"SC88") || wcsstr(k, L"88MAP"))
		return 2;
	if (wcsstr(k, L"LAMAP") || wcsstr(k, L"MT32") || wcsstr(k, L"CM32L") ||
		wcsstr(k, L"CM64") || wcsstr(k, L"LAPC") || wcsstr(k, L"LASYNTH") ||
		wcsstr(k, L"ROLANDLA") || wcsstr(k, L"CM32"))
		return 8;
	if (wcsstr(k, L"GM2") || wcsstr(k, L"GENERALMIDI2") || wcsstr(k, L"GMLEVEL2"))
		return 9;
	if (wcsstr(k, L"SC55") || wcsstr(k, L"55MAP") || wcsstr(k, L"SC55MK") ||
		wcsstr(k, L"GS55"))
		return 1;
	if (wcsstr(k, L"SD90") || wcsstr(k, L"SD80") || wcsstr(k, L"SD20") ||
		wcsstr(k, L"SDMAP") || wcsstr(k, L"STUDIOCANVAS"))
		return 6;
	if (wcsstr(k, L"GENERALMIDI") || wcsstr(k, L"GMMAP") || wcsstr(k, L"GM1"))
		return 5;
	if (wcsstr(k, L"XGMAP") || wcsstr(k, L"SOFTXG") || wcsstr(k, L"SYXG") ||
		wcsstr(k, L"MU50") || wcsstr(k, L"MU10") || wcsstr(k, L"MU15") ||
		wcsstr(k, L"MU80") || wcsstr(k, L"MU90") || wcsstr(k, L"MU100") ||
		wcsstr(k, L"MU128") || wcsstr(k, L"MU500") || wcsstr(k, L"MU1000") ||
		wcsstr(k, L"MU2000") || wcsstr(k, L"YAMAHAXG") || wcsstr(k, L"XG50"))
		return 7;
	return 0;
}

static int DetKindFromText(const wchar_t* s)
{
	if (!s || !s[0]) return 0;
	wchar_t k[280];
	DetCompactKey(s, k, 280);
	int kind = DetKindFromCompact(k);
	if (!kind && DetHasToken(s, L"VL") && !wcsstr(k, L"VL1") && !wcsstr(k, L"VL70"))
		kind = 2;
	if (!kind && DetIsolated55(s)) kind = 1;
	if (!kind && DetHasToken(s, L"GM2")) kind = 9;
	if (!kind && DetHasToken(s, L"GM")) kind = 5;
	if (!kind && DetHasToken(s, L"XG")) kind = 7;
	if (!kind && (DetHasToken(s, L"LA") || DetHasToken(s, L"MT32") || DetHasToken(s, L"MT-32")))
		kind = 8;
	return kind;
}

static int DetFoldHint(int cur, int kind)
{
	if (kind == 4) return 4;
	if (kind == 3 && cur != 4) return 3;
	if (kind == 2 && cur != 4 && cur != 3) return 2;
	if (kind == 8 && cur != 4 && cur != 3 && cur != 2) return 8;
	if (kind >= 9 && kind <= 18 && cur == 0) return kind;
	if (kind == 1 && cur == 0) return 1;
	if ((kind == 5 || kind == 6 || kind == 7) && cur == 0) return kind;
	return cur;
}

static int DetGuessPathTitle(const wchar_t* title, const wchar_t* path)
{
	int kind = DetKindFromText(title);
	const wchar_t* base = path;
	if (base && base[0]) {
		const wchar_t* sl = wcsrchr(base, L'\\');
		if (sl) base = sl + 1;
		const wchar_t* sl2 = wcsrchr(base, L'/');
		if (sl2) base = sl2 + 1;
		kind = DetFoldHint(kind, DetKindFromText(base));
		kind = DetFoldHint(kind, DetKindFromText(path));
	}
	return kind;
}

static int DetKindToForce(int kind)
{
	switch (kind) {
	case 1: return 3;
	case 2: return 4;
	case 3: return 5;
	case 4: return 6;
	case 5: return 7;
	case 6: return 8;
	case 7: return 2;
	case 8: return 9;
	default:
		if (kind >= 9 && kind <= 18) return kind + 1;
		return 0;
	}
}

static int DetSysexXgOn(const uint8_t* d, int n)
{
	if (!d || n < 7) return 0;
	int i = (n > 0 && d[0] == 0xf0) ? 1 : 0;
	if (i + 6 > n) return 0;
	return (d[i] == 0x43 && d[i + 2] == 0x4c && d[i + 3] == 0x00 && d[i + 4] == 0x00 && d[i + 5] == 0x7e) ? 1 : 0;
}

static int DetSysexGmOn(const uint8_t* d, int n)
{
	if (!d || n < 5) return 0;
	int i = (n > 0 && d[0] == 0xf0) ? 1 : 0;
	if (i + 4 > n) return 0;
	if (d[i] != 0x7e || d[i + 2] != 0x09) return 0;
	return (d[i + 3] == 0x01 || d[i + 3] == 0x03) ? 1 : 0;
}

static int DetSysexGm2(const uint8_t* d, int n)
{
	if (!d || n < 5) return 0;
	int i = (n > 0 && d[0] == 0xf0) ? 1 : 0;
	if (i + 4 > n) return 0;
	return (d[i] == 0x7e && d[i + 2] == 0x09 && d[i + 3] == 0x03) ? 1 : 0;
}

static int DetSysexGsReset(const uint8_t* d, int n)
{
	if (!d || n < 8) return 0;
	int i = (n > 0 && d[0] == 0xf0) ? 1 : 0;
	if (i + 7 > n) return 0;
	if (d[i] != 0x41 || d[i + 2] != 0x42 || d[i + 3] != 0x12) return 0;
	return (d[i + 4] == 0x40 && d[i + 5] == 0x00 && d[i + 6] == 0x7f) ? 1 : 0;
}

static int DetSysexGsSysMode(const uint8_t* d, int n)
{
	if (!d || n < 8) return 0;
	int i = (n > 0 && d[0] == 0xf0) ? 1 : 0;
	if (i + 7 > n) return 0;
	if (d[i] != 0x41 || d[i + 2] != 0x42 || d[i + 3] != 0x12) return 0;
	return (d[i + 4] == 0x00 && d[i + 5] == 0x00 && d[i + 6] == 0x7f) ? 1 : 0;
}

static int DetSysexGsSysModeKind(const uint8_t* d, int n)
{
	if (!DetSysexGsSysMode(d, n)) return 0;
	int i = (n > 0 && d[0] == 0xf0) ? 1 : 0;
	const int data = (i + 7 < n) ? (int)(d[i + 7] & 0x7f) : 0;
	if (data <= 0) return 1;
	if (data == 1) return 2;
	if (data == 2) return 3;
	return 4;
}

static int DetSysexMt32(const uint8_t* d, int n)
{
	if (!d || n < 4) return 0;
	int i = (n > 0 && d[0] == 0xf0) ? 1 : 0;
	if (i + 3 > n) return 0;
	return (d[i] == 0x41 && d[i + 2] == 0x16) ? 1 : 0;
}

static int DetBankMsbSd(int msb)
{
	return (msb == 80 || msb == 81 ||
		msb == 96 || msb == 97 || msb == 98 || msb == 99 ||
		msb == 104 || msb == 105 || msb == 106 || msb == 107) ? 1 : 0;
}

static void DetEatSysex(const uint8_t* d, int n, int* hasXg, int* hasGs, int* hasGm, int* hasGm2, int* hasSd, int* mapHint, int* lastSys)
{
	if (DetSysexXgOn(d, n)) *hasXg = 1;
	if (DetSysexGsReset(d, n)) *hasGs = 1;
	if (DetSysexGmOn(d, n)) *hasGm = 1;
	if (DetSysexGm2(d, n)) *hasGm2 = 1;
	if (DetSysexMt32(d, n)) *mapHint = DetFoldHint(*mapHint, 8);
	const int sys = DetSysexGsSysModeKind(d, n);
	if (sys) {
		*mapHint = DetFoldHint(*mapHint, sys);
		if (lastSys) *lastSys = sys;
	}
	(void)hasSd;
}

static int DetResolveKind(int mapHint, int hasXg, int hasGs, int hasGm, int hasGm2, int hasSd, int cc32Max)
{
	if (mapHint == 7) hasXg = 1;
	int resolved = 0;
	if (hasXg) resolved = 7;
	else if (mapHint == 8) resolved = 8;
	else if (mapHint >= 9 && mapHint <= 18) resolved = mapHint;
	else if (mapHint >= 1 && mapHint <= 4) resolved = mapHint;
	else if (hasGm2 && !hasGs) resolved = 9;
	else if ((mapHint == 5 || hasGm) && !hasGs) resolved = 5;
	else if (mapHint == 6 || hasSd) resolved = 6;
	else if (cc32Max >= 1 && cc32Max <= 4) resolved = cc32Max;
	return resolved;
}

static int DetReadBE(const uint8_t* p, int n)
{
	unsigned v = 0;
	for (int i = 0; i < n; i++) v = (v << 8) | p[i];
	return (int)v;
}

static int DetReadVar(const uint8_t*& q, const uint8_t* end, unsigned& v)
{
	v = 0;
	for (int i = 0; i < 4; i++) {
		if (q >= end) return 0;
		unsigned b = *q++;
		v = (v << 7) | (b & 0x7f);
		if (!(b & 0x80)) return 1;
	}
	return 0;
}

static const uint8_t* DetSmfStart(const uint8_t* data, int n, int* outN)
{
	if (!data || n < 14) return NULL;
	if (data[0] == 'M' && data[1] == 'T' && data[2] == 'h' && data[3] == 'd') {
		*outN = n;
		return data;
	}
	const int lim = (n < 4096) ? n : 4096;
	for (int i = 0; i + 14 <= lim; i++) {
		if (data[i] == 'M' && data[i + 1] == 'T' && data[i + 2] == 'h' && data[i + 3] == 'd') {
			*outN = n - i;
			return data + i;
		}
	}
	return NULL;
}

static int DetReadDocWide(const wchar_t* path, wchar_t* out, int outChars)
{
	if (!path || !out || outChars < 8) return 0;
	out[0] = 0;
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD sz = GetFileSize(f, NULL), got = 0;
	if (sz == INVALID_FILE_SIZE || sz < 8 || sz > 256 * 1024) {
		CloseHandle(f);
		return 0;
	}
	char* raw = new char[(size_t)sz + 1];
	if (!ReadFile(f, raw, sz, &got, NULL)) {
		CloseHandle(f);
		delete[] raw;
		return 0;
	}
	CloseHandle(f);
	raw[got] = 0;
	if (MultiByteToWideChar(932, 0, raw, -1, out, outChars) <= 0)
		MultiByteToWideChar(CP_ACP, 0, raw, -1, out, outChars);
	delete[] raw;
	out[outChars - 1] = 0;
	return out[0] ? 1 : 0;
}

static int DetKindFromDocText(const wchar_t* w, const wchar_t* leaf, const wchar_t* leafStem)
{
	if (!w || !w[0]) return 0;
	if (leaf && leaf[0] && wcschr(w, L'[')) {
		wchar_t tag[280];
		const wchar_t* sec = NULL;
		_snwprintf_s(tag, _TRUNCATE, L"[%s]", leaf);
		sec = wcsstr(w, tag);
		if (!sec && leafStem && leafStem[0]) {
			_snwprintf_s(tag, _TRUNCATE, L"[%s]", leafStem);
			sec = wcsstr(w, tag);
		}
		if (sec) {
			wchar_t chunk[1200];
			const wchar_t* end = wcschr(sec + 1, L'[');
			size_t n = end ? (size_t)(end - sec) : wcslen(sec);
			if (n > 1199) n = 1199;
			wcsncpy_s(chunk, sec, n);
			chunk[n] = 0;
			const int sk = DetKindFromDocText(chunk, NULL, NULL);
			if (sk) return sk;
		}
	}
	if (wcsstr(w, L"55MAP") || wcsstr(w, L"55map") || wcsstr(w, L"55Map"))
		return 1;
	if (wcsstr(w, L"88PROMAP") || wcsstr(w, L"88Promap") || wcsstr(w, L"88ProMAP"))
		return 3;
	if (wcsstr(w, L"8820MAP") || wcsstr(w, L"8820map"))
		return 4;
	if (wcsstr(w, L"88MAP") || wcsstr(w, L"88map"))
		return 2;
	int lineKind = 0;
	const wchar_t* p = w;
	while (*p) {
		const wchar_t* nl = wcschr(p, L'\n');
		wchar_t line[512];
		size_t n = nl ? (size_t)(nl - p) : wcslen(p);
		if (n >= 511) n = 511;
		wcsncpy_s(line, p, n);
		line[n] = 0;
		if (wcsstr(line, L"音源") || wcsstr(line, L"対応") || wcsstr(line, L"使用ハード")
			|| wcsstr(line, L"対応ハード") || wcsstr(line, L"MAP") || wcsstr(line, L"map")
			|| wcsstr(line, L"ModuleName")) {
			const int k = DetKindFromText(line);
			if (k) lineKind = DetFoldHint(lineKind, k);
		}
		p = nl ? nl + 1 : p + n;
		if (!nl) break;
	}
	if (lineKind) return lineKind;
	return DetKindFromText(w);
}

/* 隣の .doc/.txt/.hed/.tdf。ogg 本体の VstMidiGuessGsMapFromSidecar と同じ。 */
static int DetGuessSidecar(const wchar_t* midPath)
{
	if (!midPath || !midPath[0]) return 0;
	wchar_t stem[MAX_PATH];
	wcsncpy_s(stem, midPath, _TRUNCATE);
	wchar_t* colon = wcsstr(stem, L"::");
	if (colon) *colon = 0;
	wchar_t* gt = wcschr(stem, L'>');
	if (gt) *gt = 0;
	wchar_t* dot = wcsrchr(stem, L'.');
	wchar_t* sl = wcsrchr(stem, L'\\');
	if (!sl) sl = wcsrchr(stem, L'/');
	if (dot && (!sl || dot > sl)) *dot = 0;
	const wchar_t* leaf = midPath;
	for (const wchar_t* s = midPath; *s; ++s) {
		if (*s == L'\\' || *s == L'/' || *s == L'>')
			leaf = s + 1;
	}
	wchar_t leafStem[260];
	wcsncpy_s(leafStem, leaf, _TRUNCATE);
	wchar_t* ld = wcsrchr(leafStem, L'.');
	if (ld) *ld = 0;
	static const wchar_t* kExt[] = {
		L".doc", L".txt", L".hed", L".tdf",
		L".DOC", L".TXT", L".HED", L".TDF"
	};
	wchar_t* text = new wchar_t[8192];
	int kind = 0;
	for (int i = 0; i < 8; i++) {
		wchar_t pth[MAX_PATH];
		_snwprintf_s(pth, _TRUNCATE, L"%s%s", stem, kExt[i]);
		if (!DetReadDocWide(pth, text, 8192)) continue;
		const int k = DetKindFromDocText(text, leaf, leafStem);
		if (k) kind = DetFoldHint(kind, k);
	}
	if (kind) { delete[] text; return kind; }
	wchar_t dir[MAX_PATH];
	wcsncpy_s(dir, stem, _TRUNCATE);
	wchar_t* dslash = wcsrchr(dir, L'\\');
	if (!dslash) dslash = wcsrchr(dir, L'/');
	if (!dslash) { delete[] text; return 0; }
	dslash[1] = 0;
	wchar_t pat[MAX_PATH];
	_snwprintf_s(pat, _TRUNCATE, L"%s*.*", dir);
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) { delete[] text; return 0; }
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		const wchar_t* ext = wcsrchr(fd.cFileName, L'.');
		if (!ext) continue;
		if (_wcsicmp(ext, L".doc") != 0 && _wcsicmp(ext, L".txt") != 0
			&& _wcsicmp(ext, L".hed") != 0 && _wcsicmp(ext, L".tdf") != 0) continue;
		wchar_t pth[MAX_PATH];
		_snwprintf_s(pth, _TRUNCATE, L"%s%s", dir, fd.cFileName);
		if (!DetReadDocWide(pth, text, 8192)) continue;
		if (leafStem[0] && !wcsstr(text, leafStem) && !wcsstr(text, leaf))
			continue;
		const int k = DetKindFromDocText(text, leaf, leafStem);
		if (k) kind = DetFoldHint(kind, k);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	delete[] text;
	return kind;
}

static int DetTitleJunk(const wchar_t* w)
{
	if (!w || !w[0]) return 1;
	int junk = 1;
	for (const wchar_t* p = w; *p; ++p) {
		const wchar_t c = *p;
		if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') continue;
		if (c != L'?' && c != L'*' && c != L'.' && c != L'-' && c != L'_' && c != L'!') {
			junk = 0;
			break;
		}
	}
	if (!junk && (wcsstr(w, L"GM版") || wcscmp(w, L"GM曲") == 0))
		junk = 1;
	return junk;
}

static unsigned char g_gsBits[5][2048];
static int g_gsBitsReady;

static int DetGsHas(int map, int bank, int pc)
{
	if (map < 1 || map > 4 || bank < 0 || bank > 127 || pc < 0 || pc > 127) return 0;
	const int bit = bank * 128 + pc;
	return (g_gsBits[map][bit >> 3] >> (bit & 7)) & 1;
}

static void DetGsAddBuf(const unsigned char* d, int n)
{
	if (!d || n < 20) return;
	const int rec = n / 20;
	for (int i = 0; i < rec; i++) {
		const unsigned char* r = d + i * 20;
		const int map = r[0], bank = r[1], pc = r[2];
		if (map < 1 || map > 4 || bank > 127 || pc > 127) continue;
		const int bit = bank * 128 + pc;
		g_gsBits[map][bit >> 3] = (unsigned char)(g_gsBits[map][bit >> 3] | (unsigned char)(1u << (bit & 7)));
	}
}

static int DetGsLoadFile(const wchar_t* path)
{
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD sz = GetFileSize(f, NULL), got = 0;
	if (sz < 20 || sz > 8 * 1024 * 1024) { CloseHandle(f); return 0; }
	unsigned char* buf = new unsigned char[sz];
	if (!ReadFile(f, buf, sz, &got, NULL) || got != sz) {
		CloseHandle(f);
		delete[] buf;
		return 0;
	}
	CloseHandle(f);
	DetGsAddBuf(buf, (int)sz);
	delete[] buf;
	return 1;
}

static int DetGsEnsure()
{
	if (g_gsBitsReady) return g_gsBitsReady > 0;
	g_gsBitsReady = -1;
	if (g_hKpi) {
		HRSRC hr = FindResourceW(g_hKpi, MAKEINTRESOURCEW(101), RT_RCDATA);
		if (hr) {
			HGLOBAL hg = LoadResource(g_hKpi, hr);
			DWORD sz = SizeofResource(g_hKpi, hr);
			const unsigned char* p = (const unsigned char*)LockResource(hg);
			if (p && sz >= 20) {
				DetGsAddBuf(p, (int)sz);
				g_gsBitsReady = 1;
				return 1;
			}
		}
	}
	if (DetGsLoadFile(L"C:\\Windows\\SASAMI_GS.DAT")) {
		g_gsBitsReady = 1;
		return 1;
	}
	wchar_t mods[2][MAX_PATH];
	mods[0][0] = mods[1][0] = 0;
	if (g_hKpi) GetModuleFileNameW(g_hKpi, mods[0], MAX_PATH);
	GetModuleFileNameW(NULL, mods[1], MAX_PATH);
	for (int m = 0; m < 2; m++) {
		wchar_t dir[MAX_PATH];
		wcsncpy_s(dir, mods[m], _TRUNCATE);
		for (int up = 0; up < 8 && dir[0]; up++) {
			wchar_t* sl = wcsrchr(dir, L'\\');
			if (!sl) break;
			*sl = 0;
			wchar_t cand[MAX_PATH];
			_snwprintf_s(cand, _TRUNCATE, L"%s\\SASAMI_GS.DAT", dir);
			if (DetGsLoadFile(cand)) { g_gsBitsReady = 1; return 1; }
			_snwprintf_s(cand, _TRUNCATE, L"%s\\res\\SASAMI_GS.DAT", dir);
			if (DetGsLoadFile(cand)) { g_gsBitsReady = 1; return 1; }
		}
	}
	return 0;
}

/* 8850 から落とす。CC32=4 または 8850 にしか無い CC0+PC なら 8820。
   使っていなければ 88Pro → 88 → 55。何も無ければ 55。 */
static int DetDropFromUsed(const unsigned short* pairs, int nPairs, int cc32Max)
{
	int kind = 1;
	if (cc32Max >= 1 && cc32Max <= 4)
		kind = cc32Max;
	if (!pairs || nPairs <= 0 || !DetGsEnsure())
		return kind;
	for (int i = 0; i < nPairs; i++) {
		const int bank = (pairs[i] >> 8) & 0x7f;
		const int pc = pairs[i] & 0x7f;
		int tm = 1;
		if (!DetGsHas(1, bank, pc)) tm = 2;
		if (!DetGsHas(2, bank, pc)) tm = 3;
		if (!DetGsHas(3, bank, pc)) tm = 4;
		if (tm > kind) kind = tm;
	}
	return kind;
}

static int DetFinish(int mapHint, int hasXg, int hasGs, int hasGm, int hasGm2, int hasSd,
	int cc32Max, int lastSys, const wchar_t* path, const wchar_t* titleBuf,
	const unsigned short* pairs, int nPairs)
{
	/* タイトルタグ（FF 03 優先）とファイル名。タイトルが音源名ならファイル名より先に畳む。 */
	if (titleBuf && titleBuf[0])
		mapHint = DetFoldHint(mapHint, DetKindFromText(titleBuf));
	if (!mapHint)
		mapHint = DetGuessPathTitle(titleBuf, path);
	else
		mapHint = DetFoldHint(mapHint, DetGuessPathTitle(NULL, path));
	const int side = DetGuessSidecar(path);
	if (side) {
		if (!titleBuf || !titleBuf[0] || !DetKindFromText(titleBuf))
			mapHint = side;
		else
			mapHint = DetFoldHint(mapHint, side);
	}
	if (mapHint == 7) hasXg = 1;
	int resolved = 0;
	if (hasXg) resolved = 7;
	else if (lastSys >= 1 && lastSys <= 4) resolved = lastSys;
	else if (mapHint == 8) resolved = 8;
	else if (mapHint >= 9 && mapHint <= 18) resolved = mapHint;
	else if (mapHint >= 1 && mapHint <= 4) resolved = mapHint;
	else if (hasGm2 && !hasGs) resolved = 9;
	else if ((mapHint == 5 || hasGm) && !hasGs) resolved = 5;
	else if (mapHint == 6 || hasSd) resolved = 6;
	else resolved = DetDropFromUsed(pairs, nPairs, cc32Max);
	return DetKindToForce(resolved);
}

static int DetFromSmf(const uint8_t* data, int n, const wchar_t* path, const wchar_t* titleW)
{
	int smfN = 0;
	const uint8_t* smf = DetSmfStart(data, n, &smfN);
	if (!smf || smfN < 14) return 0;
	if (DetReadBE(smf + 4, 4) < 6) return 0;
	const int tracks = DetReadBE(smf + 10, 2);
	const uint8_t* p = smf + 8 + DetReadBE(smf + 4, 4);
	const uint8_t* fileEnd = smf + smfN;
	int hasXg = 0, hasGs = 0, hasGm = 0, hasGm2 = 0, hasSd = 0;
	int mapHint = 0, cc32Max = 0, lastSys = 0;
	unsigned short pairs[256];
	int nPairs = 0;
	unsigned char have[2048];
	uint8_t msb[32];
	memset(have, 0, sizeof(have));
	memset(msb, 0, sizeof(msb));
	wchar_t titleBuf[280];
	titleBuf[0] = 0;
	if (titleW && titleW[0] && !DetTitleJunk(titleW))
		wcsncpy_s(titleBuf, titleW, _TRUNCATE);
	for (int tr = 0; tr < tracks && p + 8 <= fileEnd; ++tr) {
		if (memcmp(p, "MTrk", 4)) break;
		const int len = DetReadBE(p + 4, 4);
		const uint8_t* q = p + 8;
		const uint8_t* end = (q + len <= fileEnd) ? q + len : fileEnd;
		uint8_t running = 0;
		int curPort = 0;
		while (q < end) {
			unsigned delta = 0;
			if (!DetReadVar(q, end, delta)) break;
			if (q >= end) break;
			uint8_t st = *q;
			if (st & 0x80) { ++q; if (st < 0xf0) running = st; }
			else if (running) st = running;
			else break;
			if (st == 0xff) {
				if (q >= end) break;
				const uint8_t type = *q++;
				unsigned ml = 0;
				if (!DetReadVar(q, end, ml) || q + ml > end) break;
				if (type == 0x21 && ml >= 1)
					curPort = (q[0] > 1) ? 1 : (int)q[0];
				else if ((type == 0x01 || type == 0x02 || type == 0x03) && ml > 0) {
					char tmp[256];
					unsigned tn = ml;
					if (tn > 255) tn = 255;
					memcpy(tmp, q, tn);
					tmp[tn] = 0;
					wchar_t w[256];
					w[0] = 0;
					if (!MultiByteToWideChar(932, 0, tmp, -1, w, 256))
						MultiByteToWideChar(CP_ACP, 0, tmp, -1, w, 256);
					w[255] = 0;
					if (w[0]) {
						mapHint = DetFoldHint(mapHint, DetKindFromText(w));
						/* FF 03 Sequence Name をタイトルにする。無いときだけ Text。 */
						if (!DetTitleJunk(w) && (type == 0x03 || !titleBuf[0]))
							wcsncpy_s(titleBuf, w, _TRUNCATE);
					}
				}
				q += ml;
			} else if (st == 0xf0 || st == 0xf7) {
				unsigned sl = 0;
				if (!DetReadVar(q, end, sl) || q + sl > end) break;
				const int need = (st == 0xf0) ? (1 + (int)sl) : (int)sl;
				if (need >= 6 && need <= 1024) {
					uint8_t sx[1024];
					int off = 0;
					if (st == 0xf0) sx[off++] = 0xf0;
					memcpy(sx + off, q, sl);
					off += (int)sl;
					DetEatSysex(sx, off, &hasXg, &hasGs, &hasGm, &hasGm2, &hasSd, &mapHint, &lastSys);
				}
				q += sl;
			} else {
				const int kind = st & 0xf0;
				const int need = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
				if (q + need > end) break;
				const uint8_t d1 = q[0], d2 = (need == 2) ? q[1] : 0;
				q += need;
				if (kind >= 0x80 && kind <= 0xe0) {
					const int ch = st & 0x0f;
					int idx = curPort * 16 + ch;
					if (idx < 0) idx = ch;
					if (idx > 31) idx = 31;
					const int drum = (ch == 9) ? 1 : 0;
					if (kind == 0xb0 && d1 == 0) {
						msb[idx] = (uint8_t)(d2 & 0x7f);
						if (DetBankMsbSd(d2 & 0x7f)) hasSd = 1;
						if ((d2 & 0x7f) == 121) hasGm2 = 1;
					} else if (kind == 0xb0 && d1 == 32) {
						const int v = d2 & 0x7f;
						if (!drum && v >= 1 && v <= 4 && v > cc32Max) cc32Max = v;
					} else if (kind == 0xc0 && !drum && nPairs < 256) {
						const int bank = (int)msb[idx];
						const int pc = d1 & 0x7f;
						const int bit = bank * 128 + pc;
						if (bit >= 0 && bit < 16384) {
							const int bi = bit >> 3;
							const unsigned char mask = (unsigned char)(1u << (bit & 7));
							if (!(have[bi] & mask)) {
								have[bi] = (unsigned char)(have[bi] | mask);
								pairs[nPairs++] = (unsigned short)((bank << 8) | pc);
							}
						}
					}
				}
			}
		}
		p = end;
	}
	return DetFinish(mapHint, hasXg, hasGs, hasGm, hasGm2, hasSd, cc32Max, lastSys, path, titleBuf, pairs, nPairs);
}

static uint32_t DetSkipToFf(const SasamiSong& song, uint32_t addr)
{
	uint32_t p = addr + 1;
	int guard = 0;
	while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF && guard++ < 256)
		p++;
	if (SasamiOffOk(song, p, 1) && SasamiGet(song, p) == 0xFF) p++;
	return p;
}

/* cmd40/39 のタグ。0=そのモードは曲に無い。kind は 1=55 2=88 3=88Pro 5=GM 7=XG 8=LA。 */
static int DetBestTagKind(int lv2, int tag0, int tag1, int tag2, int tag3)
{
	if (!tag0 && !tag1 && !tag2 && !tag3) return 0;
	if (!lv2) {
		/* mpy: 88 > 55 > XG > LA */
		if (tag1) return 2;
		if (tag0) return 1;
		if (tag2) return 7;
		return 8;
	}
	if (tag0) return 3;
	if (tag1) return 2;
	if (tag2) return 7;
	return 5;
}

static void DetNoteModeTag(uint8_t tag, int* tag0, int* tag1, int* tag2, int* tag3)
{
	if (tag == 0) *tag0 = 1;
	else if (tag == 1) *tag1 = 1;
	else if (tag == 2) *tag2 = 1;
	else if (tag == 3) *tag3 = 1;
}

static int DetFromSong(const SasamiSong& song, const wchar_t* path)
{
	int hasXg = 0, hasGs = 0, hasGm = 0, hasGm2 = 0, hasSd = 0;
	int mapHint = 0, cc32Max = 0, lastSys = 0;
	int tag0 = 0, tag1 = 0, tag2 = 0, tag3 = 0;
	int prog88 = 0, progAlt = 0;
	wchar_t titleW[280];
	titleW[0] = 0;
	if (song.titleSjis[0]) {
		if (!MultiByteToWideChar(932, 0, song.titleSjis, -1, titleW, 280))
			MultiByteToWideChar(CP_ACP, 0, song.titleSjis, -1, titleW, 280);
		titleW[279] = 0;
	}
	mapHint = DetGuessPathTitle(titleW, path);
	for (int i = 0; i < song.trackCount && i < 64; i++) {
		if (song.tracks[i].unused) continue;
		uint32_t addr = song.tracks[i].fileOff;
		int guard = 0;
		while (SasamiOffOk(song, addr, 1) && addr != 0xF0 && guard++ < 8192) {
			const int cmd = SasamiGet(song, addr);
			if (cmd == 0xF0) break;
			if (cmd == 0) {
				addr += 1;
				continue;
			}
			if (cmd == 2 && SasamiOffOk(song, addr, 3)) {
				/* cmd2: b1=88 列、b2=55（mpy）または 88Pro（mpw2）。0 は未記入。 */
				if (SasamiGet(song, addr + 1)) prog88 = 1;
				if (SasamiGet(song, addr + 2)) progAlt = 1;
			} else if (cmd == 39 && SasamiOffOk(song, addr, 2)) {
				DetNoteModeTag(SasamiGet(song, addr + 1), &tag0, &tag1, &tag2, &tag3);
			}
			if (cmd == 40) {
				uint32_t p = addr + 1;
				while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFE) {
					DetNoteModeTag(SasamiGet(song, p), &tag0, &tag1, &tag2, &tag3);
					p++;
					if (p - addr > 64) break;
				}
				if (SasamiOffOk(song, p, 1) && SasamiGet(song, p) == 0xFE) p++;
				uint8_t body[128];
				int nb = 0;
				while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF && nb < 120) {
					body[nb++] = SasamiGet(song, p);
					p++;
				}
				if (SasamiOffOk(song, p, 1) && SasamiGet(song, p) == 0xFF) p++;
				if (nb > 0)
					DetEatSysex(body, nb, &hasXg, &hasGs, &hasGm, &hasGm2, &hasSd, &mapHint, &lastSys);
				addr = p;
				continue;
			}
			if (cmd == 36) {
				uint8_t body[128];
				int nb = 0;
				uint32_t p = addr + 1;
				if (SasamiOffOk(song, p, 1) && SasamiGet(song, p) == 0xF0) {
					while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF && nb < 127) {
						body[nb++] = SasamiGet(song, p++);
						if (body[nb - 1] == 0xF7) break;
					}
				} else {
					body[nb++] = 0xF0; body[nb++] = 0x41; body[nb++] = 0x10;
					body[nb++] = 0x42; body[nb++] = 0x12;
					while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF && nb < 120)
						body[nb++] = SasamiGet(song, p++);
				}
				if (nb > 0)
					DetEatSysex(body, nb, &hasXg, &hasGs, &hasGm, &hasGm2, &hasSd, &mapHint, &lastSys);
				addr = DetSkipToFf(song, addr);
				continue;
			}
			if (cmd == 13 || cmd == 14 || cmd == 15 || cmd == 37 || cmd == 46 || cmd == 47)
				addr += 4;
			else
				addr += 3;
		}
	}
	/* 1 ファイルに最大 4 モード。入っているうち一番上。
	   mpy: 1=88 > 0=55 > 2=XG > 3=LA。上限は 88。
	   mpw2 (SASAMI2): 0=88Pro > 1=88 > 2=XG > 3=GM。 */
	const int lv2 = (song.kind == SASAMI_KIND_MPW2 || song.kind == SASAMI_KIND_MPW3) ? 1 : 0;
	const int tagKind = DetBestTagKind(lv2, tag0, tag1, tag2, tag3);
	if (tagKind)
		return DetKindToForce(tagKind);
	if (!hasXg && !hasGs && !hasGm && !hasGm2 && !hasSd && !lastSys
		&& !DetGuessPathTitle(titleW, path)) {
		if (lv2) {
			if (progAlt) return DetKindToForce(3);
			if (prog88) return DetKindToForce(2);
		} else if (song.kind == SASAMI_KIND_MPY) {
			if (prog88) return DetKindToForce(2);
			if (progAlt) return DetKindToForce(1);
		}
		return 0;
	}
	return DetFinish(mapHint, hasXg, hasGs, hasGm, hasGm2, hasSd, cc32Max, lastSys, path, titleW, NULL, 0);
}

} // namespace

void SasamiMapForceToSel(int mapForce, SasamiMidiMap* map, int* gsBankLsb, int* laBankMsb)
{
	SasamiMidiMap m = SASAMI_MAP_GS88;
	int lsb = 2;
	int la = 0;
	switch (mapForce) {
	case 0: lsb = 0; break;
	case 1: m = SASAMI_MAP_GS88; lsb = 2; break;
	case 2: m = SASAMI_MAP_XG; lsb = 0; break;
	case 3: m = SASAMI_MAP_GS55; lsb = 1; break;
	case 4: m = SASAMI_MAP_GS88; lsb = 2; break;
	case 5: m = SASAMI_MAP_GS88; lsb = 3; break;
	case 6: m = SASAMI_MAP_GS88; lsb = 4; break;
	case 7: m = SASAMI_MAP_GM; lsb = 0; break;
	case 8: m = SASAMI_MAP_GM; lsb = 0; break;
	case 9: m = SASAMI_MAP_GS55; lsb = 1; la = 127; break;
	default:
		if (mapForce >= 10) { m = SASAMI_MAP_GM; lsb = 0; }
		break;
	}
	if (map) *map = m;
	if (gsBankLsb) *gsBankLsb = lsb;
	if (laBankMsb) *laBankMsb = la;
}

int SasamiReadMidMapForceW(const wchar_t* fol, int* outForce)
{
	if (!fol || !fol[0] || !outForce) return 0;
	*outForce = 0;
	wchar_t base[MAX_PATH] = {};
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) || !base[0])
		return 0;
	wchar_t path[MAX_PATH] = {};
	_snwprintf_s(path, _TRUNCATE, L"%s\\oggYSED\\midflag\\%016I64X",
		base, (unsigned long long)SasamiCacheHashPathW(fol));
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	BYTE b[8] = {};
	DWORD rd = 0;
	const BOOL ok = ReadFile(h, b, 8, &rd, NULL);
	CloseHandle(h);
	if (!ok || rd < 5 || b[0] != 1) return 0;
	*outForce = (int)b[4];
	return 1;
}

int SasamiReadFmForceW(const wchar_t* fol, int* outForce)
{
	if (!fol || !fol[0] || !outForce) return 0;
	*outForce = -1;
	wchar_t base[MAX_PATH] = {};
	if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) || !base[0])
		return 0;
	wchar_t path[MAX_PATH] = {};
	_snwprintf_s(path, _TRUNCATE, L"%s\\oggYSED\\midflag\\%016I64X",
		base, (unsigned long long)SasamiCacheHashPathW(fol));
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	BYTE b[8] = {};
	DWORD rd = 0;
	const BOOL ok = ReadFile(h, b, 8, &rd, NULL);
	CloseHandle(h);
	if (!ok || rd < 6 || b[0] != 1) return 0;
	if (b[5] > 2) return 0;
	*outForce = (int)b[5];
	return 1;
}

int SasamiResolveMapForceW(const wchar_t* fol, int globalDefault)
{
	int pf = 0;
	if (SasamiReadMidMapForceW(fol, &pf))
		return pf;
	if (globalDefault >= 0 && globalDefault <= 19)
		return globalDefault;
	return 0;
}

int SasamiDetectMapForceFromMem(const uint8_t* data, int n, const wchar_t* path, const char* titleSjis)
{
	wchar_t titleW[280];
	titleW[0] = 0;
	if (titleSjis && titleSjis[0]) {
		if (!MultiByteToWideChar(932, 0, titleSjis, -1, titleW, 280))
			MultiByteToWideChar(CP_ACP, 0, titleSjis, -1, titleW, 280);
		titleW[279] = 0;
	}
	int force = 0;
	if (data && n >= 14)
		force = DetFromSmf(data, n, path, titleW[0] ? titleW : NULL);
	if (force > 0) return force;
	return DetKindToForce(DetGuessPathTitle(titleW[0] ? titleW : NULL, path));
}

int SasamiDetectMapForceFromSong(const SasamiSong& song, const wchar_t* path)
{
	return DetFromSong(song, path);
}

#ifndef KBSASAMI_PLUGIN
extern "C" int SasamiHostGsVstReady(void);
#endif

static int SasamiGsVstReady()
{
#ifdef KBSASAMI_PLUGIN
	/* fmmidi は GS バンクを持つ。CRender の欄はここでは見ない。 */
	return 1;
#else
	return SasamiHostGsVstReady() ? 1 : 0;
#endif
}

static int SasamiPathIsMpyFamily(const wchar_t* path)
{
	const SasamiKind k = SasamiKindFromPath(path);
	return (k == SASAMI_KIND_MPY || k == SASAMI_KIND_MPW2 || k == SASAMI_KIND_MPW3) ? 1 : 0;
}

static int SasamiSongIsMpyFamily(const SasamiSong* song, const wchar_t* path)
{
	if (song && (song->kind == SASAMI_KIND_MPY || song->kind == SASAMI_KIND_MPW2
		|| song->kind == SASAMI_KIND_MPW3))
		return 1;
	return SasamiPathIsMpyFamily(path);
}

static int SasamiSongLv2(const SasamiSong* song, const wchar_t* path)
{
	if (song && (song->kind == SASAMI_KIND_MPW2 || song->kind == SASAMI_KIND_MPW3))
		return 1;
	if (song && song->kind == SASAMI_KIND_MPY)
		return 0;
	const SasamiKind k = SasamiKindFromPath(path);
	return (k == SASAMI_KIND_MPW2 || k == SASAMI_KIND_MPW3) ? 1 : 0;
}

int SasamiAutoMapForce(int resolved, const SasamiSong* song, const uint8_t* smf, int smfN, const wchar_t* path, const char* titleSjis)
{
	if (resolved > 0) return resolved;
	int d = 0;
	if (song)
		d = SasamiDetectMapForceFromSong(*song, path);
	else
		d = SasamiDetectMapForceFromMem(smf, smfN, path, titleSjis);
	if (!SasamiSongIsMpyFamily(song, path))
		return d > 0 ? d : 0;
	/* CRender の GS VST が空なら GS マップは鳴らないので XG。明示の midimode は上で返す。 */
	if (!SasamiGsVstReady())
		return 2;
	if (d == 5 || d == 6) {
		const int mpy = (song && song->kind == SASAMI_KIND_MPY)
			|| (!song && SasamiKindFromPath(path) == SASAMI_KIND_MPY);
		if (mpy) d = 4;
	}
	if (d > 0) return d;
	return SasamiSongLv2(song, path) ? 5 : 4;
}

int SasamiResolveFmModeW(const wchar_t* fol, int globalDefault)
{
	int pf = -1;
	if (SasamiReadFmForceW(fol, &pf) && pf >= 0 && pf <= 2)
		return pf;
	if (globalDefault >= 0 && globalDefault <= 2)
		return globalDefault;
	return 2;
}

static int SasamiRegistryMapDefault()
{
	HKEY hKey = NULL;
	if (RegOpenKeyExW(HKEY_CURRENT_USER,
		L"Software\\Kobarin's Soft\\oggYSEDbgm\\KpiV5Config\\kbsasami\\kbsasami",
		0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return 0;
	wchar_t buf[32] = {};
	DWORD sz = sizeof(buf);
	DWORD type = 0;
	int v = -1;
	if (RegQueryValueExW(hKey, L"midimode", NULL, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
		v = _wtoi(buf);
	else {
		sz = sizeof(buf);
		type = 0;
		if (RegQueryValueExW(hKey, L"map", NULL, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
			v = _wtoi(buf);
	}
	RegCloseKey(hKey);
	return (v >= 0 && v <= 19) ? v : 0;
}

static void SasamiTempMidiPath(const wchar_t* src, wchar_t* dest, int destChars);

struct SasamiTempCache {
	wchar_t src[MAX_PATH];
	FILETIME srcWrite;
	DWORD srcSize;
	int mapForce;
	int convVer;
};
static SasamiTempCache s_tempCache;
enum { SASAMI_SMF_CACHE_VER = 16 };

static int SasamiReadSourceStamp(const wchar_t* src, FILETIME* writeTime, DWORD* size)
{
	if (!src || !src[0] || !writeTime || !size) return 0;
	HANDLE h = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	LARGE_INTEGER li;
	if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 || li.QuadPart > 0x7FFFFFFF) {
		CloseHandle(h);
		return 0;
	}
	FILETIME c, a, w;
	if (!GetFileTime(h, &c, &a, &w)) {
		CloseHandle(h);
		return 0;
	}
	CloseHandle(h);
	*writeTime = w;
	*size = (DWORD)li.QuadPart;
	return 1;
}

static int SasamiTempCacheValid(const wchar_t* src, int mapForce, const wchar_t* dest)
{
	if (!src || !src[0] || !dest || !dest[0] || !s_tempCache.src[0]) return 0;
	if (s_tempCache.convVer != SASAMI_SMF_CACHE_VER) return 0;
	if (_wcsicmp(s_tempCache.src, src) != 0 || s_tempCache.mapForce != mapForce) return 0;
	FILETIME wt;
	DWORD sz = 0;
	if (!SasamiReadSourceStamp(src, &wt, &sz)) return 0;
	if (CompareFileTime(&s_tempCache.srcWrite, &wt) != 0 || s_tempCache.srcSize != sz) return 0;
	const DWORD attr = GetFileAttributesW(dest);
	return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
}

void SasamiInvalidateTempMidi(const wchar_t* src)
{
	if (!src || !src[0] || !SasamiExtIsMidi(src)) return;
	wchar_t dest[MAX_PATH] = {};
	SasamiTempMidiPath(src, dest, MAX_PATH);
	DeleteFileW(dest);
	if (s_tempCache.src[0] && _wcsicmp(s_tempCache.src, src) == 0)
		memset(&s_tempCache, 0, sizeof(s_tempCache));
}

bool SasamiConvertToSmf(const SasamiSong& song, SasamiMidiMap map, int gsBankLsb, uint8_t* out, int outCap, int* outSize, int laBankMsb)
{
	if (!out || !outSize || outCap <= 0) return false;
	*outSize = 0;
	if (!EnsureMidiWork()) return false;
	if (song.kind != SASAMI_KIND_MPY && song.kind != SASAMI_KIND_MPW2 && song.kind != SASAMI_KIND_MPW3) return false;
	if (song.trackCount <= 0) return false;

	const int ver = song.mpyVersion;
	const int lv2 = (song.kind == SASAMI_KIND_MPW2 || song.kind == SASAMI_KIND_MPW3) ? 1 : 0;
	/* SASAMI_MAIN: flg 0=55, 1=88, 2=XG。
	   SASAMI2 (mpw2): flg 0=88Pro（音色 b2、EFX）、1=88、2=XG。CC32=3 が Pro。 */
	int flg88;
	if (map == SASAMI_MAP_XG)
		flg88 = 2;
	else if (map == SASAMI_MAP_GM)
		flg88 = 0;
	else if (lv2 && gsBankLsb >= 3)
		flg88 = 0;
	else if (map == SASAMI_MAP_GS88)
		flg88 = 1;
	else
		flg88 = 0;
	const int isGm = (map == SASAMI_MAP_GM) ? 1 : 0;
	const int allowGsEfx = (gsBankLsb >= 3 && gsBankLsb <= 4) ? 1 : 0;

	s_evSeq = 0;
	s_evCount = 0;
	s_firstCount = 0;
	s_pushTrk = 255;
	s_pushPass = 0;
	MidiTrackState tr[64];
	memset(tr, 0, sizeof(tr));
	int nAlive = 0;
	for (int i = 0; i < song.trackCount && i < 64; i++) {
		tr[i].addr = song.tracks[i].fileOff;
		tr[i].count = 0;
		tr[i].part = song.tracks[i].part & 0x0F;
		tr[i].port = 0;
		tr[i].note = 0;
		tr[i].vel = 100;
		tr[i].loopSp = 0;
		tr[i].drum = (tr[i].part == 9) ? 1 : 0;
		tr[i].backJumps = 0;
		tr[i].loopSafety = 0;
		tr[i].alive = song.tracks[i].unused ? 0 : 1;
		tr[i].everJump = 0;
		tr[i].pedal = 0;
		tr[i].loopStartTick = 0xFFFFFFFFu;
		tr[i].loopEndTick = 0;
		tr[i].loopDest = 0xFFFFFFFFu;
		tr[i].period = 0;
		tr[i].softMode = -1;
		tr[i].softDelay = tr[i].softDepth = tr[i].softPhase = 0;
		tr[i].portaSemi = tr[i].portaDelay = tr[i].portaGlide = tr[i].portaLeft = 0;
		if (tr[i].alive && tr[i].addr == 0xF0) tr[i].alive = 0;
		if (tr[i].alive) nAlive++;
	}
	/* Empty score / all-unused tracks: still emit a minimal SMF so VST preview
	   (.mpsmv with binds only) can open instead of "MIDI not found". */
	if (nAlive == 0) {
		const unsigned T = SASAMI_DEFAULT_T;
		const uint32_t mpqn = (uint32_t)((500ull * T) / 13ull);
		uint8_t d[6] = { 0xFF, 0x51, 0x03, (uint8_t)(mpqn >> 16), (uint8_t)(mpqn >> 8), (uint8_t)mpqn };
		PushEv(0, 0, d, 6);
		WriteSmf(1, out, outCap, outSize);
		return *outSize > 22;
	}

	int nports = song.dualPort ? 2 : 1;
	int port1Ready = (nports >= 2) ? 1 : 0;
	uint8_t chOn[2][16][128];
	memset(chOn, 0, sizeof(chOn));

	{
		const unsigned T = SASAMI_DEFAULT_T;
		const uint32_t mpqn = (uint32_t)((500ull * T) / 13ull);
		uint8_t d[6] = { 0xFF, 0x51, 0x03, (uint8_t)(mpqn >> 16), (uint8_t)(mpqn >> 8), (uint8_t)mpqn };
		PushEv(0, 0, d, 6);
	}
	if (song.titleSjis[0]) {
		size_t n = strlen(song.titleSjis);
		const int m = (n > 120) ? 120 : (int)n;
		uint8_t d[128];
		d[0] = 0xFF; d[1] = 0x03; d[2] = (uint8_t)m;
		memcpy(d + 3, song.titleSjis, (size_t)m);
		PushEv(0, 0, d, 3 + m);
	}

	static const uint8_t kGsReset[11] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
	static const uint8_t kXgOn[9] = { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7 };
	static const uint8_t kGmOn[6] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
	for (int p = 0; p < nports; p++) {
		if (map == SASAMI_MAP_XG) PushEv(0, p, kXgOn, 9);
		else if (map == SASAMI_MAP_GM) PushEv(0, p, kGmOn, 6);
		else PushEv(0, p, kGsReset, 11);
		for (int ch = 0; ch < 16; ch++)
			PushMmodeChannelInit(0, p, ch, map, gsBankLsb, flg88, laBankMsb);
	}

	uint32_t tick = 0;
	/* 同じ MIDI ch の複数トラックが同じ音高を持つ。先に終わった方の
	   NoteOff で残っている方まで切ると、その ch が途中で無音になる。
	   最後の 1 本が離すときだけ NoteOff を出す。 */
	auto releasePitch = [&](int port, int ch, int note) {
		if (note <= 0 || note >= 128 || ch < 0 || ch > 15) return;
		if (port < 0) port = 0;
		uint8_t& c = chOn[port ? 1 : 0][ch][note];
		if (c > 0) c--;
		if (c == 0)
			PushShort(tick, port, (uint8_t)(0x80 | ch), (uint8_t)note, 0);
	};
	unsigned curT = SASAMI_DEFAULT_T;
	(void)curT;
	uint32_t gLoopStart = 0xFFFFFFFFu;
	uint32_t gLoopEnd = 0;

	auto releaseTrack = [&](int i) {
		const int ch = tr[i].part;
		const int port = tr[i].port;
		if (tr[i].note)
			releasePitch(port, ch, tr[i].note);
		tr[i].note = 0;
		if (tr[i].pedal) {
			PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x40, 0);
			tr[i].pedal = 0;
		}
	};

	auto killTrack = [&](int i) {
		if (!tr[i].alive) return;
		releaseTrack(i);
		tr[i].alive = 0;
	};

	auto ensurePort1 = [&]() {
		if (port1Ready) return;
		port1Ready = 1;
		nports = 2;
		if (map == SASAMI_MAP_XG) PushEv(tick, 1, kXgOn, 9);
		else if (map == SASAMI_MAP_GM) PushEv(tick, 1, kGmOn, 6);
		else PushEv(tick, 1, kGsReset, 11);
		for (int ch = 0; ch < 16; ch++)
			PushMmodeChannelInit(tick, 1, ch, map, gsBankLsb, flg88, laBankMsb);
	};

	while (tick < SASAMI_MAX_TICKS) {
		int any = 0;
		for (int i = 0; i < song.trackCount && i < 64; i++) {
			if (!tr[i].alive) continue;
			any = 1;
			s_pushTrk = i;
			s_pushPass = tr[i].pass;
			int guard = 0;
			while (tr[i].alive && tr[i].count < 1 && guard++ < 4096) {
				const uint32_t addr = tr[i].addr;
				if (!SasamiOffOk(song, addr, 1) || addr == 0xF0) {
					killTrack(i);
					break;
				}
				const int cmd = SasamiGet(song, addr);
				const int ch = tr[i].part;
				const int port = tr[i].port;
				const uint8_t b1 = SasamiGet(song, addr + 1);
				const uint8_t b2 = SasamiGet(song, addr + 2);
				const uint8_t b3 = SasamiGet(song, addr + 3);
				const uint64_t akey = ((uint64_t)i << 32) | addr;
				{
					int found = 0;
					for (int fi = 0; fi < s_firstCount; fi++) {
						if (s_first[fi].key == akey) { found = 1; break; }
					}
					if (!found && s_firstCount < SASAMI_MAX_FIRST) {
						s_first[s_firstCount].key = akey;
						s_first[s_firstCount].tick = tick;
						s_firstCount++;
					}
				}
				int again = 1;

				switch (cmd) {
				case 1: { // note: original always note-off previous then note-on
					const int note = (int)(b1 & 0x7F);
					if (tr[i].note)
						releasePitch(port, ch, tr[i].note);
					PushShort(tick, port, (uint8_t)(0x90 | ch), (uint8_t)note, (uint8_t)tr[i].vel);
					if (note > 0 && note < 128 && ch >= 0 && ch <= 15) {
						uint8_t& c = chOn[port ? 1 : 0][ch][note];
						if (c < 255) c++;
					}
					tr[i].note = note;
					tr[i].count = b2;
					tr[i].addr = addr + 3;
					if (tr[i].count != 0) again = 0;
					break;
				}
				case 2: { // program 88 / 55
					if (!(flg88 == 2 && song.versionWord >= 36000)) {
						if (flg88 == 1)
							PushShort(tick, port, (uint8_t)(0xC0 | ch), b1, 0);
						else if (!isGm || song.versionWord < 50000)
							PushShort(tick, port, (uint8_t)(0xC0 | ch), b2, 0);
					}
					tr[i].addr = addr + 3;
					break;
				}
				case 3: // tie
					tr[i].count = b2;
					tr[i].addr = addr + 3;
					if (tr[i].count != 0) again = 0;
					break;
				case 4: { // master vol: LCD 10 00 16 + GS 40 00 04 + GM master
					const uint8_t lcd[4] = { 0x10, 0x00, 0x16, b1 };
					PushExclBody(tick, port, (flg88 == 2) ? 1 : 0, 0x42, lcd, 4);
					if (!isGm && flg88 != 2)
						PushGs(tick, port, 0x40, 0x00, 0x04, b1);
					{
						uint8_t sx[8] = { 0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, b1, 0xF7 };
						PushEv(tick, port, sx, 8);
					}
					tr[i].addr = addr + 3;
					break;
				}
				case 5: { // track vol: M58CHK 88=b1, 55=b2
					if (!(flg88 == 2 && song.versionWord >= 36000)) {
						const uint8_t vol = (flg88 == 1) ? b1 : b2;
						if (flg88 == 1)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 7, vol);
						else if (!isGm || song.versionWord < 50000)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 7, vol);
					}
					tr[i].addr = addr + 3;
					break;
				}
				case 6:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 1, b1);
					tr[i].addr = addr + 3;
					break;
				case 7: // bank
					if (!(flg88 == 2 && song.versionWord >= 36000)) {
						if (laBankMsb == 127 && ch != 9)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, 127);
						else if (flg88 == 0 && (!isGm || song.versionWord < 50000))
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, b2);
						else if (flg88 == 1)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, b1);
						if (flg88 == 2)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 32, b1);
					}
					tr[i].addr = addr + 3;
					break;
				case 8: // rest
					if (tr[i].note)
						releasePitch(port, ch, tr[i].note);
					tr[i].note = 0;
					tr[i].count = b2;
					tr[i].addr = addr + 3;
					if (tr[i].count != 0) again = 0;
					break;
				case 9: {
					unsigned T = (unsigned)b1 + ((unsigned)b2 << 8);
					if (T == 0) T = SASAMI_DEFAULT_T;
					curT = T;
					{
						const uint32_t mpqn = (uint32_t)((500ull * T) / 13ull);
						uint8_t d[6] = { 0xFF, 0x51, 0x03, (uint8_t)(mpqn >> 16), (uint8_t)(mpqn >> 8), (uint8_t)mpqn };
						PushEv(tick, 0, d, 6);
					}
					tr[i].addr = addr + 3;
					break;
				}
				case 10: {
					uint32_t nxt = 0;
					const uint32_t dest = ReadJump(song, addr, ver, &nxt);
					if (dest == 0xF0) {
						killTrack(i);
						again = 0;
						break;
					}
					/* Only kill true self-jumps (bad patch). Forward J and
					   0xE000→first-|: must keep working for normal .mpw2. */
					if (dest == addr) {
						killTrack(i);
						again = 0;
						break;
					}
					if (dest < addr) {
						/* トラックごとの Q/J。長さは揃わない。同じ戻り先の 2 周目は
						   1 周分だけ測る。戻り先が変わったときだけ周期を取り直す（変異）。
						   ここで短い方を殺すと、他の Q/J の途中で伴奏が終わる。 */
						tr[i].everJump = 1;
						tr[i].backJumps++;
						const uint32_t destTick = DestFirstTick(i, dest, song.tracks[i].fileOff);
						const uint32_t prevSt = tr[i].loopStartTick;
						const uint32_t prevEn = tr[i].loopEndTick;
						const int sameDest = (tr[i].loopDest == dest) ? 1 : 0;
						uint32_t cycleOrigin = destTick;
						if (sameDest && prevEn > 0 && prevSt == destTick && tick > prevEn)
							cycleOrigin = prevEn;
						uint32_t span = 0;
						if (cycleOrigin != 0xFFFFFFFFu && tick > cycleOrigin)
							span = tick - cycleOrigin;
						if (span > 0 && destTick != 0xFFFFFFFFu && (!sameDest || tr[i].period == 0)) {
							tr[i].loopDest = dest;
							tr[i].period = span;
							tr[i].loopStartTick = destTick;
							tr[i].loopEndTick = destTick + span;
						}
						/* SASAMI11 case 10: 戻る J では NoteOff しない。
						   同じ tick で Q を実行し、そこのノート/休符が
						   前の音を切ってから新しい音を出す。ここで切って
						   note を捨てると、Q の NoteOn だけが残って重なる。 */
						/* 曲として揃う tick を過ぎた J は、次の周を始めない。 */
						if (gLoopEnd > 0 && tick >= gLoopEnd) {
							int pending = 0;
							for (int k = 0; k < song.trackCount && k < 64; k++) {
								if (tr[k].alive && !tr[k].everJump) { pending = 1; break; }
							}
							if (!pending) {
								killTrack(i);
								again = 0;
								break;
							}
						}
						if (tr[i].pass < 255) tr[i].pass++;
						s_pushPass = tr[i].pass;
					}
					tr[i].addr = dest;
					break;
				}
				case 11:
					PushShort(tick, port, (uint8_t)(0xE0 | ch), b1, b2);
					tr[i].addr = addr + 3;
					break;
				case 12:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 10, (flg88 == 1) ? b2 : b1);
					tr[i].addr = addr + 3;
					break;
				case 13: { // LAREV: EXCLOUT + 10 00 01 b1 b2 b3 + EXCLEND
					const uint8_t body[6] = { 0x10, 0x00, 0x01, b1, b2, b3 };
					PushExclBody(tick, port, (flg88 == 2) ? 1 : 0, 0x42, body, 6);
					tr[i].addr = addr + 4;
					break;
				}
				case 14: { // GS/XG reverb
					if (flg88 == 2) {
						uint8_t msb = 2, lsb = 0;
						switch (b1) {
						case 0: msb = 2; lsb = 0; break;
						case 1: msb = 2; lsb = 1; break;
						case 2: msb = 2; lsb = 2; break;
						case 3: msb = 1; lsb = 0; break;
						case 4: msb = 1; lsb = 1; break;
						case 5: msb = 4; lsb = 0; break;
						default: msb = 0; lsb = 0; break;
						}
						{
							uint8_t sx[10] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x00, msb, lsb, 0xF7 };
							PushEv(tick, port, sx, 10);
						}
						{
							uint8_t sx[9] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x0C, b2, 0xF7 };
							PushEv(tick, port, sx, 9);
						}
						{
							uint8_t sx[9] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x02, b3, 0xF7 };
							PushEv(tick, port, sx, 9);
						}
					} else if (!isGm) {
						PushGs(tick, port, 0x40, 0x01, 0x30, b1);
						PushGs(tick, port, 0x40, 0x01, 0x33, b2);
						PushGs(tick, port, 0x40, 0x01, 0x34, b3);
					}
					tr[i].addr = addr + 4;
					break;
				}
				case 15: {
					if (flg88 == 2) {
						uint8_t msb = 41, lsb = 0;
						switch (b1) {
						case 0: msb = 41; lsb = 0; break;
						case 1: msb = 41; lsb = 1; break;
						case 2: msb = 41; lsb = 2; break;
						case 3: msb = 41; lsb = 3; break;
						case 4: msb = 42; lsb = 0; break;
						case 5: msb = 43; lsb = 0; break;
						case 6: msb = 44; lsb = 0; break;
						case 7: msb = 48; lsb = 0; break;
						}
						{
							uint8_t sx[10] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x20, msb, lsb, 0xF7 };
							PushEv(tick, port, sx, 10);
						}
						{
							uint8_t sx[9] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x2C, b2, 0xF7 };
							PushEv(tick, port, sx, 9);
						}
						{
							uint8_t sx[9] = { 0xF0, 0x43, 0x10, 0x4C, 0x02, 0x01, 0x23, b3, 0xF7 };
							PushEv(tick, port, sx, 9);
						}
					} else if (!isGm) {
						PushGs(tick, port, 0x40, 0x01, 0x38, b1);
						PushGs(tick, port, 0x40, 0x01, 0x3A, b2);
						PushGs(tick, port, 0x40, 0x01, 0x3E, b3);
					}
					tr[i].addr = addr + 4;
					break;
				}
				case 16: // EFX。SASAMI2 は flg==0（88Pro）のときだけ 40 4n 22
					if (lv2 && flg88 == 0 && !isGm && gsBankLsb >= 3)
						PushGs(tick, port, 0x40, (uint8_t)(0x40 + GsPartIdx(ch)), 0x22, b1);
					tr[i].addr = addr + 3;
					break;
				case 17:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x5B, b1);
					tr[i].addr = addr + 3;
					break;
				case 18:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x5D, b1);
					tr[i].addr = addr + 3;
					break;
				case 19:
					tr[i].vel = b1;
					tr[i].addr = addr + 3;
					break;
				case 20:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x40, 0x7F);
					tr[i].pedal = 1;
					tr[i].addr = addr + 3;
					break;
				case 21:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x40, 0x00);
					tr[i].pedal = 0;
					tr[i].addr = addr + 3;
					break;
				case 22: // Pro の CC16/CC17。SASAMI2 は flg==0 のときだけ
					if (lv2 && flg88 == 0 && !isGm) {
						if (b1 < 128)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x10, b1);
						if (b2 < 128)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x11, b2);
					}
					tr[i].addr = addr + 3;
					break;
				case 23:
					if (tr[i].loopSp < MidiTrackState::MIDI_LOOP_NEST)
						tr[i].loopStack[tr[i].loopSp++] = b1;
					else
						tr[i].loopStack[MidiTrackState::MIDI_LOOP_NEST - 1] = b1;
					tr[i].addr = addr + 3;
					break;
				case 24: {
					uint32_t nxt = 0;
					const uint32_t dest = ReadJump(song, addr, ver, &nxt);
					tr[i].loopSafety++;
					if (tr[i].loopSp <= 0 || tr[i].loopSafety > 4096) {
						tr[i].addr = nxt;
						break;
					}
					int* lp = &tr[i].loopStack[tr[i].loopSp - 1];
					(*lp)--;
					if (*lp == 0) {
						tr[i].loopSp--;
						tr[i].addr = nxt;
					} else {
						tr[i].addr = dest;
					}
					break;
				}
				case 25:
					tr[i].addr = addr + 3;
					break;
				case 26: // PICH2 → WHAT4  fall-through（原版どおり一体）
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x65, 0);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x64, 0);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x06, 0);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x06, b1);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x26, 0);
					tr[i].addr = addr + 3;
					break;
				case 27: // WHAT4 (PICH2 続き): CC6 + RPN null
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x06, b1);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x26, 0);
					tr[i].addr = addr + 3;
					break;
				case 28: { // TITL: GS LCD 11 文字 (M_MUSIC+0C0)
					uint8_t body[14];
					body[0] = 0x10;
					body[1] = 0;
					body[2] = 0;
					for (int k = 0; k < 11; k++) {
						const uint32_t titOff = 0xC0u + (uint32_t)k;
						body[3 + k] = SasamiOffOk(song, titOff, 1) ? SasamiGet(song, titOff) : 0;
					}
					PushExclBody(tick, port, 0, 0x45, body, 14);
					tr[i].addr = addr + 3;
					break;
				}
				case 29: // KAKU1  本家: XG は b1>5 なら CC0=b1、それ以外は 127/0
					tr[i].drum = b1;
					if (flg88 == 2) {
						if (b1 > 5)
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, b1);
						else
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, (uint8_t)((b1 != 0) ? 127 : 0));
						tr[i].drum = 0;
					} else if (!isGm) {
						PushGs(tick, port, 0x40, (uint8_t)(0x10 + GsPartIdx(ch)), 0x15, b1);
					}
					tr[i].addr = addr + 3;
					break;
				case 30: // KAKU2
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, (uint8_t)((b1 != 0) ? 127 : 0));
					else if (!isGm)
						PushGs(tick, port, 0x40, (uint8_t)(0x10 + GsPartIdx(ch)), 0x30, b1);
					tr[i].addr = addr + 3;
					break;
				case 31: // KAKU3
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, (uint8_t)((b1 != 0) ? 127 : 0));
					else if (!isGm)
						PushGs(tick, port, 0x40, (uint8_t)(0x10 + GsPartIdx(ch)), 0x36, b1);
					tr[i].addr = addr + 3;
					break;
				case 32: // KAKU4
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, (uint8_t)((b1 != 0) ? 127 : 0));
					else if (!isGm)
						PushGs(tick, port, 0x40, (uint8_t)(0x10 + GsPartIdx(ch)), 0x32, b1);
					tr[i].addr = addr + 3;
					break;
				case 33: // KAKU5
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, (uint8_t)((b1 != 0) ? 127 : 0));
					else if (!isGm)
						PushGs(tick, port, 0x40, (uint8_t)(0x10 + GsPartIdx(ch)), 0x33, b1);
					tr[i].addr = addr + 3;
					break;
				case 34:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 11, b1);
					tr[i].addr = addr + 3;
					break;
				case 35: // MD5588: 原版は XG のみ CC32/CC0
					if (flg88 == 2) {
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 32, (uint8_t)(b1 + 1));
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, 0);
					}
					tr[i].addr = addr + 3;
					break;
				case 36: {
					uint8_t buf[128];
					int n = 0;
					uint32_t p = addr + 1;
					if (SasamiOffOk(song, p, 1) && SasamiGet(song, p) == 0xF0) {
						while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF && n < 127) {
							buf[n++] = SasamiGet(song, p++);
							if (buf[n - 1] == 0xF7) break;
						}
						if (n > 0 && buf[n - 1] != 0xF7 && n < 128)
							buf[n++] = 0xF7;
					} else {
						int sum = 0;
						buf[n++] = 0xF0; buf[n++] = 0x41; buf[n++] = 0x10; buf[n++] = 0x42; buf[n++] = 0x12;
						while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF && n < 120) {
							buf[n] = SasamiGet(song, p);
							sum += buf[n];
							n++;
							p++;
						}
						buf[n++] = (uint8_t)((128 - (sum % 128)) & 0x7F);
						buf[n++] = 0xF7;
					}
					PushEv(tick, port, buf, n);
					tr[i].addr = p + 1;
					break;
				}
				case 37:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x63, b1);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x62, b2);
					PushShort(tick, port, (uint8_t)(0xB0 | ch), 0x06, b3);
					tr[i].addr = addr + 4;
					break;
				case 38:
					ensurePort1();
					tr[i].port = 1;
					tr[i].addr = addr + 3;
					break;
				case 39:
					if (b1 == flg88 || (b1 == 3 && flg88 == 0 && isGm)
						|| (b1 == 3 && laBankMsb == 127)) {
						killTrack(i);
						again = 0;
					} else {
						tr[i].addr = addr + 3;
					}
					break;
				case 40: {
					uint32_t p = addr + 1;
					int match = 0;
					while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFE) {
						const uint8_t tag = SasamiGet(song, p);
						if (tag == (uint8_t)flg88) match = 1;
						else if (tag == 3 && flg88 == 0 && isGm) match = 1;
						else if (tag == 3 && laBankMsb == 127) match = 1;
						p++;
					}
					if (!match) {
						while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF) p++;
						tr[i].addr = p + 1;
						break;
					}
					p++;
					uint8_t buf[128];
					int n = 0;
					int sum = 0;
					int sumOn = 0;
					while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFE && n < 110) {
						buf[n++] = SasamiGet(song, p);
						p++;
					}
					if (SasamiOffOk(song, p, 1) && SasamiGet(song, p) == 0xFE) p++;
					sumOn = 1;
					while (SasamiOffOk(song, p, 1) && SasamiGet(song, p) != 0xFF && n < 120) {
						buf[n] = SasamiGet(song, p);
						if (sumOn) sum += buf[n];
						n++;
						p++;
					}
					if (flg88 != 2)
						buf[n++] = (uint8_t)((128 - (sum % 128)) & 0x7F);
					buf[n++] = 0xF7;
					if (!allowGsEfx && MidiIsGsEfxDt1(buf, n)) {
						tr[i].addr = p + 1;
						break;
					}
					if (n > 0 && buf[0] != 0xF0) {
						uint8_t withF0[130];
						withF0[0] = 0xF0;
						memcpy(withF0 + 1, buf, (size_t)n);
						PushEv(tick, port, withF0, n + 1);
					} else {
						PushEv(tick, port, buf, n);
					}
					tr[i].addr = p + 1;
					break;
				}
				case 41:
					PushShort(tick, port, (uint8_t)(0xB0 | ch), b1, b2);
					tr[i].addr = addr + 3;
					break;
				case 42:
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xC0 | ch), b1, 0);
					if (isGm && song.versionWord >= 50000)
						PushShort(tick, port, (uint8_t)(0xC0 | ch), b2, 0);
					tr[i].addr = addr + 3;
					break;
				case 43:
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 32, b1);
					if (isGm && song.versionWord >= 50000)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 0, b2);
					tr[i].addr = addr + 3;
					break;
				case 44:
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 7, b1);
					if (isGm && song.versionWord >= 50000)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 7, b2);
					tr[i].addr = addr + 3;
					break;
				case 45:
					if (flg88 == 2)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 10, b1);
					else if (isGm)
						PushShort(tick, port, (uint8_t)(0xB0 | ch), 10, b2);
					tr[i].addr = addr + 3;
					break;
				case 46: {
					/* Soft vib/trem: [46][mode][delay][depth] */
					uint8_t depth = 40;
					if (SasamiOffOk(song, addr, 4))
						depth = SasamiGet(song, addr + 3);
					tr[i].softMode = (int)(b1 & 1);
					tr[i].softDelay = (int)b2;
					tr[i].softDepth = (int)depth;
					tr[i].softPhase = 0;
					tr[i].addr = addr + 4;
					break;
				}
				case 47: {
					uint8_t glide = 24;
					if (SasamiOffOk(song, addr, 4))
						glide = SasamiGet(song, addr + 3);
					tr[i].portaSemi = (int)b1 - 64;
					tr[i].portaDelay = (int)b2;
					tr[i].portaGlide = (int)glide;
					tr[i].portaLeft = (int)glide;
					tr[i].addr = addr + 4;
					break;
				}
				default:
					if (cmd == 0) {
						/* COV.C case 0 は addr++（;non）。MEIREI [00] 未使用。
						   S00005 A03 は :| の直後に 00 を挟んで次の |: が続く。
						   終了は 0xF0 / J→0x10F0。00 を kill するとフレーズ1回で死ぬ。 */
						tr[i].addr = addr + 1;
					} else {
						tr[i].addr = addr + 3;
					}
					break;
				}
				if (!again) break;
			}
		}
		if (!any) break;
		for (int i = 0; i < song.trackCount && i < 64; i++) {
			if (!tr[i].alive) continue;
			if (tr[i].count > 0) tr[i].count--;
			/* Soft FX while sounding */
			if (tr[i].note) {
				const int ch = tr[i].part;
				const int port = tr[i].port;
				if (tr[i].portaSemi != 0 && tr[i].portaGlide > 0) {
					if (tr[i].portaDelay > 0) tr[i].portaDelay--;
					else if (tr[i].portaLeft > 0) {
						int done = tr[i].portaGlide - tr[i].portaLeft;
						int bend = 0x2000 + (tr[i].portaSemi * 0x200 * done) / tr[i].portaGlide;
						if (bend < 0) bend = 0;
						if (bend > 0x3FFF) bend = 0x3FFF;
						PushShort(tick, port, (uint8_t)(0xE0 | ch), (uint8_t)(bend & 0x7F), (uint8_t)((bend >> 7) & 0x7F));
						tr[i].portaLeft--;
					}
				}
				if (tr[i].softMode >= 0 && tr[i].softDepth > 0) {
					if (tr[i].softDelay > 0) tr[i].softDelay--;
					else {
						tr[i].softPhase = (tr[i].softPhase + 1) & 63;
						/* triangle 0..depth */
						int ph = tr[i].softPhase;
						int tri = (ph < 32) ? ph : (64 - ph);
						int amt = (tri * tr[i].softDepth) / 32;
						if (tr[i].softMode == 0) {
							int bend = 0x2000 + (amt - tr[i].softDepth / 2) * 16;
							if (bend < 0) bend = 0;
							if (bend > 0x3FFF) bend = 0x3FFF;
							PushShort(tick, port, (uint8_t)(0xE0 | ch), (uint8_t)(bend & 0x7F), (uint8_t)((bend >> 7) & 0x7F));
						} else {
							int cc = 127 - amt;
							if (cc < 1) cc = 1;
							PushShort(tick, port, (uint8_t)(0xB0 | ch), 11, (uint8_t)cc);
						}
					}
				}
			}
		}
		tick++;
		{
			/* まだ J していないトラックがいる間は終端を決めない。
			   揃ったら、各周期の公倍数。一番長い 1 本にはしない。
			   戻り先が変わって周期が更新されたら、次の tick で取り直す。 */
			int pending = 0;
			int nper = 0;
			uint32_t lcm = 0;
			uint32_t latestQ = 0;
			for (int i = 0; i < song.trackCount && i < 64; i++) {
				if (tr[i].alive && !tr[i].everJump) pending = 1;
				if (!tr[i].period || tr[i].loopStartTick == 0xFFFFFFFFu) continue;
				nper++;
				lcm = lcm ? LcmU32(lcm, tr[i].period) : tr[i].period;
				if (tr[i].loopStartTick > latestQ) latestQ = tr[i].loopStartTick;
			}
			enum { kLoopCap = 96000 };
			if (!pending && nper > 0 && lcm > 0 && lcm <= (uint32_t)kLoopCap) {
				gLoopStart = latestQ;
				gLoopEnd = latestQ + lcm;
			}
			if (!pending && gLoopEnd > 0 && tick >= gLoopEnd)
				break;
		}
		if (s_evCount >= SASAMI_MAX_EV - 256)
			break;
	}

	/* 終端はシミュレーション中に決めた公倍数。ここでは一番長い Q/J に差し替えない。
	   展開がイベント上限で先に止まったときは、届いていない終端を採用しない。 */
	if (gLoopEnd > 0 && tick < gLoopEnd) {
		gLoopEnd = 0;
		gLoopStart = 0xFFFFFFFFu;
	}
	/* 最長 J 同士のずれが 1 小節以内なら、遅い方へ伸ばさず多数派の J で閉じる。
	   長さは公倍数のままなので 192tick の倍数（4/4 の小節）から外れない。 */
	if (gLoopEnd > gLoopStart) {
		uint32_t lcm = 0;
		for (int i = 0; i < song.trackCount && i < 64; i++) {
			if (!tr[i].period) continue;
			lcm = lcm ? LcmU32(lcm, tr[i].period) : tr[i].period;
		}
		const uint32_t maj = MajorityLongEnd(tr, song.trackCount, lcm);
		if (maj > 0) {
			gLoopEnd = maj;
			gLoopStart = maj - lcm;
		}
	}

	if (gLoopEnd > 0 && gLoopStart == 0xFFFFFFFFu)
		gLoopStart = 0;
	const int haveLoop = (gLoopStart != 0xFFFFFFFFu && gLoopEnd > gLoopStart) ? 1 : 0;
	/* |: :| だけ伸びるパートの 2 周目以降は gLoopEnd で切る。SMF ループを
	   付けないと MAX_EV まで展開したあと再生が止まる。 */
	const int useSmfLoop = haveLoop ? 1 : 0;
	if (useSmfLoop) {
		int keepN = 0;
		for (int i = 0; i < s_evCount; i++) {
			const MidiEv& e = s_evs[i];
			const uint32_t cut = gLoopEnd;
			int take = 0;
			if (e.tick < cut)
				take = 1;
			else if (e.tick == cut && e.len > 0) {
				const uint8_t st = e.bytes[0];
				const int type = st & 0xF0;
				/* J と同じ tick で Q を再実行した NoteOn だけ捨てる。
				   拍頭のドラム（pass 0）まで捨てるとモニタにも乗らない。 */
				int replay = 0;
				if (type == 0x90) {
					const uint8_t v = (e.len > 2) ? e.bytes[2] : 0;
					/* この tick がそのトラックの Q なら、J で巻き戻した再発音。
					   位相が Q でないトラックの音は、短い Q/J の途中なので残す。 */
					if (v && e.pass > 0 && e.trk < 64 && tr[e.trk].period > 0
						&& tr[e.trk].loopStartTick != 0xFFFFFFFFu && cut >= tr[e.trk].loopStartTick
						&& ((cut - tr[e.trk].loopStartTick) % tr[e.trk].period) == 0)
						replay = 1;
				}
				if (!replay) take = 1;
			}
			if (take) {
				if (keepN != i) s_evs[keepN] = e;
				keepN++;
			}
		}
		s_evCount = keepN;
		{
			/* mpy/mpw2 に拍子は無い。1 小節 = 192 tick（PPQN 48 の 4/4）。
			   小節線はループ頭と同じ位相に置く。 */
			const uint32_t phase = (gLoopStart == 0xFFFFFFFFu) ? 0 : (gLoopStart % 192u);
			uint8_t ts[7] = { 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08 };
			PushEv(phase, 0, ts, 7);
		}
		{
			static const char kLoopStart[] = "loopStart";
			const int n = (int)(sizeof(kLoopStart) - 1);
			uint8_t d[128];
			d[0] = 0xFF; d[1] = 0x06; d[2] = (uint8_t)n;
			memcpy(d + 3, kLoopStart, (size_t)n);
			PushEv(gLoopStart, 0, d, 3 + n);
		}
		{
			static const char kLoopEnd[] = "loopEnd";
			const int n = (int)(sizeof(kLoopEnd) - 1);
			uint8_t d[128];
			d[0] = 0xFF; d[1] = 0x06; d[2] = (uint8_t)n;
			memcpy(d + 3, kLoopEnd, (size_t)n);
			PushEv(gLoopEnd, 0, d, 3 + n);
		}
		PushShort(gLoopStart, 0, 0xB0, 111, 0);
		PushShort(gLoopEnd, 0, 0xB0, 111, 127);
	} else {
		for (int i = 0; i < song.trackCount && i < 64; i++)
			killTrack(i);
		for (int p = 0; p < nports; p++) {
			for (int ch = 0; ch < 16; ch++) {
				PushShort(tick, p, (uint8_t)(0xB0 | ch), 0x40, 0);
				PushShort(tick, p, (uint8_t)(0xB0 | ch), 0x7B, 0);
			}
		}
	}

	for (int i = 0; i < s_evCount; i++) {
		if (s_evs[i].port >= nports) nports = s_evs[i].port + 1;
	}

	if (SasamiMisaoActive(song)) {
		enum { kMisaoEvMax = 8192 };
		static SasamiMisaoEv s_misaoEv[kMisaoEvMax];
		unsigned misaoTicks = 0;
		const int nMisao = SasamiMisaoBuildEvents(song, s_misaoEv, kMisaoEvMax, &misaoTicks);
		for (int i = 0; i < nMisao; i++) {
			const SasamiMisaoEv& me = s_misaoEv[i];
			PushEv(me.tick, me.port, me.bytes, me.len);
			if (me.port + 1 > nports) nports = me.port + 1;
		}
		(void)misaoTicks;
	}

	if (useSmfLoop) {
		std::stable_sort(s_evs, s_evs + s_evCount, [](const MidiEv& a, const MidiEv& b) {
			if (a.tick != b.tick) return a.tick < b.tick;
			return a.seq < b.seq;
		});
		uint8_t holdNote[2][16][128];
		int holdPed[2][16];
		memset(holdNote, 0, sizeof(holdNote));
		memset(holdPed, 0, sizeof(holdPed));
		for (int i = 0; i < s_evCount; i++) {
			const MidiEv& e = s_evs[i];
			if (e.tick > gLoopEnd || e.len == 0) continue;
			const uint8_t st = e.bytes[0];
			const int p = e.port ? 1 : 0;
			const int ch = st & 0x0F;
			const int type = st & 0xF0;
			if (type == 0x90 || type == 0x80) {
				const uint8_t n = (e.len > 1) ? e.bytes[1] : 0;
				if (n >= 128) continue;
				if (type == 0x80)
					holdNote[p][ch][n] = 0;
				else {
					const uint8_t v = (e.len > 2) ? e.bytes[2] : 0;
					holdNote[p][ch][n] = v ? 1 : 0;
				}
			} else if (type == 0xB0 && e.len >= 3 && e.bytes[1] == 0x40) {
				holdPed[p][ch] = e.bytes[2];
			}
		}
		for (int p = 0; p < 2; p++) {
			for (int ch = 0; ch < 16; ch++) {
				for (int n = 0; n < 128; n++) {
					if (holdNote[p][ch][n])
						PushShort(gLoopEnd, p, (uint8_t)(0x80 | ch), (uint8_t)n, 0);
				}
				if (holdPed[p][ch])
					PushShort(gLoopEnd, p, (uint8_t)(0xB0 | ch), 0x40, 0);
				if (p < nports)
					PushShort(gLoopEnd, p, (uint8_t)(0xB0 | ch), 0x7B, 0);
			}
		}
		/* 終端の NoteOff はループ終了サンプルにあり、再生がそこを飛ばすと
		   J の音が残ったまま Q の NoteOn が重なる。Q の tick で、まだ鳴って
		   いる音を NoteOn より前に切る。SASAMI は同じ tick で off してから on。 */
		{
			uint8_t boundary[2][16][128];
			int boundaryPed[2][16];
			memset(boundary, 0, sizeof(boundary));
			memset(boundaryPed, 0, sizeof(boundaryPed));
			for (int i = 0; i < s_evCount; i++) {
				const MidiEv& e = s_evs[i];
				if (e.tick >= gLoopEnd || e.len == 0) continue;
				const uint8_t st = e.bytes[0];
				const int p = e.port ? 1 : 0;
				const int ch = st & 0x0F;
				const int type = st & 0xF0;
				if (type == 0x90 || type == 0x80) {
					const uint8_t n = (e.len > 1) ? e.bytes[1] : 0;
					if (n >= 128) continue;
					if (type == 0x80)
						boundary[p][ch][n] = 0;
					else {
						const uint8_t v = (e.len > 2) ? e.bytes[2] : 0;
						boundary[p][ch][n] = v ? 1 : 0;
					}
				} else if (type == 0xB0 && e.len >= 3 && e.bytes[1] == 0x40)
					boundaryPed[p][ch] = e.bytes[2] ? 1 : 0;
			}
			for (int i = 0; i < s_evCount; i++) {
				const MidiEv& e = s_evs[i];
				if (e.tick != gLoopEnd || e.len == 0) continue;
				const uint8_t st = e.bytes[0];
				const int type = st & 0xF0;
				if (type != 0x90) continue;
				const uint8_t v = (e.len > 2) ? e.bytes[2] : 0;
				const uint8_t n = (e.len > 1) ? e.bytes[1] : 0;
				if (!v || n >= 128) continue;
				boundary[e.port ? 1 : 0][st & 0x0F][n] = 1;
			}
			int minSeq = 0x7fffffff;
			for (int i = 0; i < s_evCount; i++) {
				if (s_evs[i].tick == gLoopStart && s_evs[i].seq < minSeq)
					minSeq = s_evs[i].seq;
			}
			if (minSeq == 0x7fffffff) minSeq = 0;
			const int firstNew = s_evCount;
			const int savedTrk = s_pushTrk;
			const int savedPass = s_pushPass;
			s_pushTrk = -1;
			s_pushPass = 0;
			for (int p = 0; p < 2; p++) {
				for (int ch = 0; ch < 16; ch++) {
					if (boundaryPed[p][ch])
						PushShort(gLoopStart, p, (uint8_t)(0xB0 | ch), 0x40, 0);
				}
			}
			for (int p = 0; p < 2; p++) {
				for (int ch = 0; ch < 16; ch++) {
					for (int n = 0; n < 128; n++) {
						if (boundary[p][ch][n])
							PushShort(gLoopStart, p, (uint8_t)(0x80 | ch), (uint8_t)n, 0);
					}
				}
			}
			s_pushTrk = savedTrk;
			s_pushPass = savedPass;
			const int nNew = s_evCount - firstNew;
			for (int k = 0; k < nNew; k++)
				s_evs[firstNew + k].seq = minSeq - nNew + k;
		}
	}

	WriteSmf(nports, out, outCap, outSize);
	return *outSize > 22;
}

int SasamiPathIsMidi(const wchar_t* path)
{
	return SasamiExtIsMidi(path) ? 1 : 0;
}

int SasamiPathIsFm(const wchar_t* path)
{
	return SasamiExtIsFm(path) ? 1 : 0;
}

static void SasamiTempMidiPath(const wchar_t* src, wchar_t* dest, int destChars)
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	wchar_t dir[MAX_PATH];
	_snwprintf_s(dir, _TRUNCATE, L"%sogg_kbsasami", tmp);
	CreateDirectoryW(dir, NULL);
	const wchar_t* name = src ? src : L"";
	for (const wchar_t* p = name; *p; p++) {
		if (*p == L'\\' || *p == L'/')
			name = p + 1;
	}
	wchar_t stem[MAX_PATH];
	wcsncpy_s(stem, name, _TRUNCATE);
	wchar_t* dot = wcsrchr(stem, L'.');
	if (dot && dot != stem)
		*dot = 0;
	if (!stem[0])
		wcsncpy_s(stem, L"sasami", _TRUNCATE);
	_snwprintf_s(dest, destChars, _TRUNCATE, L"%s\\%s.mid", dir, stem);
}

int SasamiConvertPathToMidiFile(const wchar_t* src, wchar_t* dest, int destChars)
{
	if (!src || !dest || destChars < 8) return 0;
	dest[0] = 0;
	if (!SasamiExtIsMidi(src)) return 0;
	if (!EnsureMidiWork()) return 0;
	SasamiTempMidiPath(src, dest, destChars);
	int force = SasamiResolveMapForceW(src, SasamiRegistryMapDefault());
	static SasamiSong s_song;
	int songLoaded = 0;
	if (force <= 0) {
		if (!SasamiLoadFileW(src, &s_song)) return 0;
		songLoaded = 1;
		force = SasamiAutoMapForce(0, &s_song, NULL, 0, src, s_song.titleSjis);
	}
	if (SasamiTempCacheValid(src, force, dest))
		return 1;
	if (!songLoaded) {
		if (!SasamiLoadFileW(src, &s_song)) return 0;
	}
	SasamiMidiMap map = SASAMI_MAP_GS88;
	int gsLsb = 2;
	int laBank = 0;
	SasamiMapForceToSel(force, &map, &gsLsb, &laBank);
	int sz = 0;
	if (!SasamiConvertToSmf(s_song, map, gsLsb, s_smfWork, SASAMI_MAX_SMF, &sz, laBank)) return 0;
	HANDLE h = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD w = 0;
	const BOOL ok = WriteFile(h, s_smfWork, (DWORD)sz, &w, NULL);
	CloseHandle(h);
	if (!ok || (int)w != sz) return 0;
	{
		FILETIME wt;
		DWORD fsz = 0;
		if (SasamiReadSourceStamp(src, &wt, &fsz)) {
			wcsncpy_s(s_tempCache.src, src, _TRUNCATE);
			s_tempCache.srcWrite = wt;
			s_tempCache.srcSize = fsz;
			s_tempCache.mapForce = force;
			s_tempCache.convVer = SASAMI_SMF_CACHE_VER;
		} else {
			memset(&s_tempCache, 0, sizeof(s_tempCache));
		}
	}
	return 1;
}
