#include "ZmusicEngine.h"
#include "CEmu/s98/device/fmgen/opm.h"
#include "CEmu/fmmon/fmmon_shadow.h"
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

enum { kRate = 44100, kTracks = 32, kCh = 8 };

struct Trk {
	const uint8_t* p;
	const uint8_t* begin;
	const uint8_t* end;
	int ch;
	int wait;     /* 残りステップをサンプル換算する前のクロック */
	int waitSamp;
	int gateSamp;
	int on;
	int oct;
	int vol;
	int voice;
	int finished;
};

struct Play {
	ZmusicSong* song;
	Trk trk[kTracks];
	int ntrk;
	int clockHz; /* 全音符クロック */
	uint8_t voice[201][55];
	int voiceOk[201];
	int primed;
};

static Play g_play;

static int Be16(const uint8_t* p)
{
	return (p[0] << 8) | p[1];
}

static int Be32(const uint8_t* p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static int Need(const uint8_t* p, const uint8_t* end, int n)
{
	return (p + n) <= end;
}

int ZmusicTitle(const ZmusicSong* song, char* out, int cap)
{
	if (!out || cap < 2) return 0;
	out[0] = 0;
	if (!song || !song->zmd || song->zmdBytes < 16) return 0;
	const uint8_t* p = song->zmd + 8;
	const uint8_t* end = song->zmd + song->zmdBytes;
	while (p < end && *p != 0xFF) {
		if (*p == 0x7F || *p == 0x61) {
			p++;
			int i = 0;
			while (p < end && *p && i < cap - 1)
				out[i++] = (char)*p++;
			out[i] = 0;
			return i > 0;
		}
		if (*p == 0x04 || *p == 0x1B) {
			if (!Need(p, end, 57)) break;
			p += 57;
			continue;
		}
		if (*p == 0x05) {
			if (!Need(p, end, 3)) break;
			p += 3;
			continue;
		}
		if (*p == 0x42) {
			if (!Need(p, end, 6)) break;
			p += 6;
			continue;
		}
		if (*p == 0x15 || *p == 0x7E) {
			p += (*p == 0x15) ? 2 : 1;
			continue;
		}
		break;
	}
	return 0;
}

static int SkipCommon(const uint8_t** pp, const uint8_t* end, ZmusicSong* song)
{
	const uint8_t* p = *pp;
	while (p < end && *p != 0xFF) {
		uint8_t op = *p;
		if (op == 0x04 || op == 0x1B) {
			if (!Need(p, end, 57)) return ZMUSIC_ERR_FORMAT;
			int vn = p[1];
			if (vn >= 1 && vn <= 200)
				memcpy(g_play.voice[vn], p + 2, 55), g_play.voiceOk[vn] = 1;
			p += 57;
		} else if (op == 0x05) {
			if (!Need(p, end, 3)) return ZMUSIC_ERR_FORMAT;
			song->tempo = Be16(p + 1);
			p += 3;
		} else if (op == 0x42) {
			if (!Need(p, end, 6)) return ZMUSIC_ERR_FORMAT;
			song->masterClock = p[1];
			p += 6;
		} else if (op == 0x15) {
			if (!Need(p, end, 2)) return ZMUSIC_ERR_FORMAT;
			p += 2;
		} else if (op == 0x7E) {
			p += 1;
		} else if (op == 0x7F || op == 0x61 || op == 0x60 || op == 0x62 || op == 0x63) {
			p++;
			while (p < end && *p) p++;
			if (p < end) p++;
		} else if (op == 0x18) {
			if (!Need(p, end, 3)) return ZMUSIC_ERR_FORMAT;
			int n = Be16(p + 1);
			if (!Need(p, end, 3 + n)) return ZMUSIC_ERR_FORMAT;
			p += 3 + n;
		} else if (op == 0x40) {
			return ZMUSIC_ERR_OPCODE;
		} else {
			song->unknownOp = op;
			return ZMUSIC_ERR_OPCODE;
		}
	}
	if (p >= end || *p != 0xFF) return ZMUSIC_ERR_FORMAT;
	p++;
	if (((p - song->zmd) & 1) && p < end && *p == 0xFF)
		p++;
	*pp = p;
	return ZMUSIC_OK;
}

int ZmusicOpenZmd(ZmusicSong* song, const uint8_t* zmd, int n)
{
	if (!song || !zmd || n < 16) return ZMUSIC_ERR_ARG;
	memset(song, 0, sizeof(*song));
	memset(&g_play, 0, sizeof(g_play));
	if (zmd[0] != 0x10 || memcmp(zmd + 1, "ZmuSiC", 6) != 0)
		return ZMUSIC_ERR_FORMAT;
	song->zmd = zmd;
	song->zmdBytes = n;
	song->tempo = 120;
	song->masterClock = 192;
	const uint8_t* p = zmd + 8;
	const uint8_t* end = zmd + n;
	int rc = SkipCommon(&p, end, song);
	if (rc) return rc;
	if (!Need(p, end, 2)) return ZMUSIC_ERR_FORMAT;
	int nt = Be16(p);
	p += 2;
	if (nt < 1 || nt > kTracks) return ZMUSIC_ERR_RANGE;
	song->trackCount = nt;
	g_play.song = song;
	g_play.ntrk = nt;
	g_play.clockHz = song->masterClock > 0 ? song->masterClock : 192;
	for (int i = 0; i < nt; i++) {
		if (!Need(p, end, 6)) return ZMUSIC_ERR_FORMAT;
		int off = Be32(p);
		int ch = p[5];
		p += 6;
		if (off < 0 || off >= n) return ZMUSIC_ERR_FORMAT;
		Trk* t = &g_play.trk[i];
		t->begin = t->p = zmd + off;
		t->end = end;
		t->ch = (ch >= 1 && ch <= 8) ? (ch - 1) : (ch & 7);
		t->oct = 4;
		t->vol = 110;
		t->voice = 1;
	}
	return ZMUSIC_OK;
}

static int StepSamples(const ZmusicSong* s, int step)
{
	int tempo = s->tempo;
	if (tempo < 20) tempo = 120;
	int q = s->masterClock > 0 ? s->masterClock / 4 : 48;
	if (q < 1) q = 48;
	/* 4分音符 = q クロック */
	int64_t samp = (int64_t)step * kRate * 60 / ((int64_t)tempo * q);
	if (samp < 1) samp = 1;
	return (int)samp;
}

static uint8_t s_opmRegs[256];
static int s_opmKey = -1;

static void OpmWrite(FM::OPM* opm, int addr, int data)
{
	if (!opm) return;
	if ((unsigned)addr < 256) {
		s_opmRegs[addr] = (uint8_t)data;
		if (addr == 0x08)
			s_opmKey = data;
	}
	opm->SetReg((uint)addr, (uint)data);
}

void ZmusicCopyOpmRegs(unsigned char* regs256, int* keyOn)
{
	if (regs256) memcpy(regs256, s_opmRegs, 256);
	if (keyOn) *keyOn = s_opmKey;
}

static int KcFromNote(int note)
{
	static const int kSemi[12] = { 0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14 };
	if (note < 0) note = 0;
	if (note > 127) note = 127;
	int oct = note / 12;
	if (oct > 7) oct = 7;
	return (oct << 4) | kSemi[note % 12];
}

/* コンパイラが書く 55 バイト。実在 ZMD の音色並びと違うファイルは OPCODE で落とす前に、
   この並びのときだけ OPM へ書く。判定は先頭 AR が 0..31。 */
static void ApplyVoice(FM::OPM* opm, int ch, const uint8_t* v)
{
	/* 11*4 + 11。op 順 M1 C1 M2 C2 */
	static const int kSlot[4] = { 0, 16, 8, 24 };
	for (int op = 0; op < 4; op++) {
		const uint8_t* r = v + op * 11;
		int slot = kSlot[op];
		int ar = r[0] & 31, dr = r[1] & 31, sr = r[2] & 31, rr = r[3] & 15;
		int sl = r[4] & 15, ol = r[5] & 127, ks = r[6] & 3, ml = r[7] & 15;
		int dt1 = r[8] & 7;
		OpmWrite(opm, 0x40 + slot + ch, (dt1 << 4) | ml);
		OpmWrite(opm, 0x60 + slot + ch, ol);
		OpmWrite(opm, 0x80 + slot + ch, (ks << 6) | ar);
		OpmWrite(opm, 0xA0 + slot + ch, dr);
		OpmWrite(opm, 0xC0 + slot + ch, (dt1 << 6) | sr);
		OpmWrite(opm, 0xE0 + slot + ch, (sl << 4) | rr);
	}
	int al = v[44] & 7, fb = v[45] & 7, pan = v[47] & 3;
	if (pan == 0) pan = 3;
	OpmWrite(opm, 0x20 + ch, (pan << 6) | (fb << 3) | al);
}

static void Key(FM::OPM* opm, int ch, int note, int on)
{
	if (!on) {
		OpmWrite(opm, 0x08, ch);
		return;
	}
	int kc = KcFromNote(note);
	OpmWrite(opm, 0x28 + ch, kc);
	OpmWrite(opm, 0x30 + ch, 0);
	OpmWrite(opm, 0x08, 0x78 | ch);
}

static int Pull(Trk* t, ZmusicSong* song, FM::OPM* opm)
{
	if (t->finished) return ZMUSIC_OK;
	if (!Need(t->p, t->end, 1)) {
		t->finished = 1;
		return ZMUSIC_OK;
	}
	uint8_t op = *t->p;
	if (op < 0x80) {
		if (!Need(t->p, t->end, 3)) return ZMUSIC_ERR_FORMAT;
		int note = t->p[0];
		int step = t->p[1];
		int gate = t->p[2];
		t->p += 3;
		if (step == 0) step = 1;
		t->waitSamp = StepSamples(song, step);
		int gstep = (gate == 255) ? step : (gate < step ? gate : step);
		t->gateSamp = StepSamples(song, gstep);
		if (t->on) Key(opm, t->ch, 0, 0);
		Key(opm, t->ch, note, 1);
		t->on = 1;
		return ZMUSIC_OK;
	}
	if (op == 0x80) {
		if (!Need(t->p, t->end, 3)) return ZMUSIC_ERR_FORMAT;
		int step = t->p[1];
		t->p += 3;
		if (t->on) Key(opm, t->ch, 0, 0);
		t->on = 0;
		t->gateSamp = 0;
		t->waitSamp = StepSamples(song, step < 1 ? 1 : step);
		return ZMUSIC_OK;
	}
	if (op == 0x91) {
		if (!Need(t->p, t->end, 3)) return ZMUSIC_ERR_FORMAT;
		song->tempo = Be16(t->p + 1);
		t->p += 3;
		return Pull(t, song, opm);
	}
	if (op == 0xFF) {
		if (t->on) Key(opm, t->ch, 0, 0);
		t->on = 0;
		t->finished = 1;
		t->p++;
		return ZMUSIC_OK;
	}
	if (op == 0xF1 || op == 0xF2) {
		if (!Need(t->p, t->end, 3)) return ZMUSIC_ERR_FORMAT;
		int rel = (int16_t)Be16(t->p + 1);
		const uint8_t* dest = t->p + rel;
		t->p += 3;
		if (op == 0xF2) {
			if (dest < t->begin || dest >= t->end) return ZMUSIC_ERR_FORMAT;
			t->p = dest;
		}
		return Pull(t, song, opm);
	}
	song->unknownOp = op;
	return ZMUSIC_ERR_OPCODE;
}

int ZmusicRender(ZmusicSong* song, int16_t* stereo, int frames, int* done)
{
	if (!song || !stereo || frames < 1) return ZMUSIC_ERR_ARG;
	if (!g_play.ntrk) return ZMUSIC_ERR_ARG;
	static FM::OPM* opm = NULL;
	if (!g_play.primed) {
		if (!opm) {
			opm = new FM::OPM();
			opm->Init(4000000, kRate, false);
		}
		opm->Reset();
		for (int c = 0; c < kCh; c++) {
			int vn = 1;
			for (int i = 0; i < g_play.ntrk; i++)
				if (g_play.trk[i].ch == c && g_play.trk[i].voice)
					vn = g_play.trk[i].voice;
			if (g_play.voiceOk[vn])
				ApplyVoice(opm, c, g_play.voice[vn]);
		}
		g_play.primed = 1;
	}
	int left = frames;
	int produced = 0;
	while (left > 0) {
		int alive = 0;
		int slice = left;
		for (int i = 0; i < g_play.ntrk; i++) {
			Trk* t = &g_play.trk[i];
			if (t->finished) continue;
			alive = 1;
			if (t->waitSamp <= 0) {
				int rc = Pull(t, song, opm);
				if (rc) return rc;
			}
			if (!t->finished && t->waitSamp > 0 && t->waitSamp < slice)
				slice = t->waitSamp;
		}
		if (slice > 2048) slice = 2048;
		if (!alive || slice < 1) break;
		for (int i = 0; i < g_play.ntrk; i++) {
			Trk* t = &g_play.trk[i];
			if (t->finished || t->waitSamp <= 0) continue;
			if (t->on && t->gateSamp > 0) {
				t->gateSamp -= slice;
				if (t->gateSamp <= 0) {
					Key(opm, t->ch, 0, 0);
					t->on = 0;
				}
			}
			t->waitSamp -= slice;
		}
		static FM_SAMPLETYPE buf[2048 * 2];
		memset(buf, 0, sizeof(FM_SAMPLETYPE) * slice * 2);
		opm->Mix(buf, slice);
		for (int i = 0; i < slice * 2; i++) {
			int s = buf[i];
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			stereo[(produced + i / 2) * 2 + (i & 1)] = (int16_t)s;
		}
		produced += slice;
		left -= slice;
	}
	if (done) *done = produced;
	return produced > 0 ? ZMUSIC_OK : ZMUSIC_OK;
}

static void PutBe16(uint8_t* p, int v)
{
	p[0] = (uint8_t)((v >> 8) & 255);
	p[1] = (uint8_t)(v & 255);
}

static void PutBe32(uint8_t* p, int v)
{
	p[0] = (uint8_t)((v >> 24) & 255);
	p[1] = (uint8_t)((v >> 16) & 255);
	p[2] = (uint8_t)((v >> 8) & 255);
	p[3] = (uint8_t)(v & 255);
}

static void DefaultVoice(uint8_t* v)
{
	memset(v, 0, 55);
	/* M1 は変調、C1/M2/C2 はキャリア。AL=4 で C2 が鳴る */
	const int ops[4][11] = {
		{ 31, 8, 0, 8, 2, 32, 0, 2, 0, 0, 0 },
		{ 31, 8, 0, 8, 1, 0, 0, 1, 0, 0, 0 },
		{ 31, 10, 0, 7, 1, 8, 0, 1, 0, 0, 0 },
		{ 31, 6, 0, 7, 0, 0, 0, 1, 0, 0, 0 }
	};
	for (int i = 0; i < 4; i++)
		for (int k = 0; k < 11; k++)
			v[i * 11 + k] = (uint8_t)ops[i][k];
	v[44] = 4;
	v[45] = 5;
	v[46] = 15;
	v[47] = 3;
}

static int NoteNum(int oct, int semi)
{
	int n = (oct + 1) * 12 + semi;
	if (n < 0) n = 0;
	if (n > 127) n = 127;
	return n;
}

static int SemiOf(char c)
{
	switch (c) {
	case 'c': case 'C': return 0;
	case 'd': case 'D': return 2;
	case 'e': case 'E': return 4;
	case 'f': case 'F': return 5;
	case 'g': case 'G': return 7;
	case 'a': case 'A': return 9;
	case 'b': case 'B': return 11;
	default: return -1;
	}
}

int ZmusicCompileZms(const char* text, uint8_t* out, int cap, int* outBytes, int* errLine)
{
	if (errLine) *errLine = 0;
	if (!text || !out || cap < 64) return ZMUSIC_ERR_ARG;
	static uint8_t trk[8][4096];
	int tn[8];
	memset(tn, 0, sizeof(tn));
	int tempo = 120;
	int line = 1;
	const char* p = text;
	static char title[80];
	title[0] = 0;
	while (*p) {
		const char* ls = p;
		while (*p && *p != '\n') p++;
		int len = (int)(p - ls);
		if (*p == '\n') p++;
		static char buf[512];
		if (len > 500) len = 500;
		memcpy(buf, ls, len);
		buf[len] = 0;
		if (len > 0 && buf[len - 1] == '\r') buf[--len] = 0;
		char* s = buf;
		while (*s == ' ' || *s == '\t') s++;
		if (*s == 0 || *s == '*' || *s == ';') { line++; continue; }
		if (s[0] == '#' ) {
			strncpy(title, s + 1, 79);
			title[79] = 0;
			line++;
			continue;
		}
		if ((s[0] == '(' && (s[1] == 'O' || s[1] == 'o') && s[2] == ')')) {
			tempo = atoi(s + 3);
			line++;
			continue;
		}
		if ((s[0] == 'T' || s[0] == 't') && (s[1] == ' ' || (s[1] >= '0' && s[1] <= '9'))) {
			tempo = atoi(s + 1);
			line++;
			continue;
		}
		int ti = -1;
		if ((s[0] >= 'A' && s[0] <= 'H') && (s[1] == ' ' || s[1] == '\t' || s[1] == 0))
			ti = s[0] - 'A';
		else if ((s[0] >= 'a' && s[0] <= 'h') && (s[1] == ' ' || s[1] == '\t'))
			ti = s[0] - 'a';
		if (ti < 0) {
			if (errLine) *errLine = line;
			return ZMUSIC_ERR_OPCODE;
		}
		s++;
		int oct = 4, length = 4, vol = 110;
		while (*s) {
			while (*s == ' ' || *s == '\t') s++;
			if (!*s) break;
			if (*s == 'o' || *s == 'O') { oct = atoi(s + 1); s++; while (*s >= '0' && *s <= '9') s++; continue; }
			if (*s == 'l' || *s == 'L') { length = atoi(s + 1); s++; while (*s >= '0' && *s <= '9') s++; continue; }
			if (*s == 'v' || *s == 'V') { vol = atoi(s + 1); s++; while (*s >= '0' && *s <= '9') s++; continue; }
			if (*s == '@') { s++; while (*s >= '0' && *s <= '9') s++; continue; }
			if (*s == '>') { oct++; s++; continue; }
			if (*s == '<') { oct--; s++; continue; }
			if (*s == 'r' || *s == 'R') {
				int ln = length;
				s++;
				if (*s >= '0' && *s <= '9') { ln = atoi(s); while (*s >= '0' && *s <= '9') s++; }
				int step = 192 / (ln > 0 ? ln : 4);
				if (step < 1) step = 1;
				if (tn[ti] + 3 > 4096) return ZMUSIC_ERR_FULL;
				trk[ti][tn[ti]++] = 0x80;
				trk[ti][tn[ti]++] = (uint8_t)step;
				trk[ti][tn[ti]++] = 0;
				(void)vol;
				continue;
			}
			int semi = SemiOf(*s);
			if (semi < 0) {
				if (errLine) *errLine = line;
				return ZMUSIC_ERR_OPCODE;
			}
			s++;
			if (*s == '+' || *s == '#') { semi++; s++; }
			else if (*s == '-') { semi--; s++; }
			int ln = length;
			if (*s >= '0' && *s <= '9') { ln = atoi(s); while (*s >= '0' && *s <= '9') s++; }
			int step = 192 / (ln > 0 ? ln : 4);
			if (step < 1) step = 1;
			if (step > 254) step = 254;
			int gate = step * 7 / 8;
			if (gate < 1) gate = 1;
			if (tn[ti] + 3 > 4096) return ZMUSIC_ERR_FULL;
			trk[ti][tn[ti]++] = (uint8_t)NoteNum(oct, semi);
			trk[ti][tn[ti]++] = (uint8_t)step;
			trk[ti][tn[ti]++] = (uint8_t)gate;
		}
		line++;
	}
	int used[8], nu = 0;
	for (int i = 0; i < 8; i++) if (tn[i] > 0) used[nu++] = i;
	if (nu < 1) return ZMUSIC_ERR_FORMAT;
	for (int i = 0; i < nu; i++) {
		int t = used[i];
		if (tn[t] + 1 > 4096) return ZMUSIC_ERR_FULL;
		trk[t][tn[t]++] = 0xFF;
	}
	uint8_t voice[55];
	DefaultVoice(voice);
	int titleN = (int)strlen(title);
	/* 10 ZmuSiC ver, 1B+55, 05+2, 7F+title+0, FF, maybe pad, count, entries, data */
	int need = 8 + 57 + 3 + (titleN ? (2 + titleN) : 0) + 2 + 2 + nu * 6;
	for (int i = 0; i < nu; i++) need += tn[used[i]];
	need += 4;
	if (need > cap) return ZMUSIC_ERR_FULL;
	int o = 0;
	out[o++] = 0x10;
	memcpy(out + o, "ZmuSiC", 6); o += 6;
	out[o++] = 1;
	out[o++] = 0x1B;
	out[o++] = 1;
	memcpy(out + o, voice, 55); o += 55;
	out[o++] = 0x05;
	PutBe16(out + o, tempo); o += 2;
	if (titleN) {
		out[o++] = 0x7F;
		memcpy(out + o, title, titleN); o += titleN;
		out[o++] = 0;
	}
	out[o++] = 0xFF;
	if (o & 1) out[o++] = 0xFF;
	PutBe16(out + o, nu); o += 2;
	int table = o;
	o += nu * 6;
	for (int i = 0; i < nu; i++) {
		PutBe32(out + table + i * 6, o);
		out[table + i * 6 + 4] = 0;
		out[table + i * 6 + 5] = (uint8_t)(used[i] + 1);
		memcpy(out + o, trk[used[i]], tn[used[i]]);
		o += tn[used[i]];
	}
	if (outBytes) *outBytes = o;
	return ZMUSIC_OK;
}

static uint8_t s_zmdMem[512 * 1024];
static char s_zmsText[256 * 1024];
static int s_session;
static int16_t s_hold[4096 * 2];
static int s_holdHave;
static int s_holdUsed;

void ZmusicRewindEngine()
{
	for (int i = 0; i < g_play.ntrk; i++) {
		Trk* t = &g_play.trk[i];
		t->p = t->begin;
		t->waitSamp = 0;
		t->gateSamp = 0;
		t->on = 0;
		t->finished = 0;
	}
	g_play.primed = 0;
	s_opmKey = -1;
}

void ZmusicEngineStop()
{
	g_play.ntrk = 0;
	g_play.song = NULL;
	g_play.primed = 0;
}

void ZmusicSessionClose()
{
	s_session = 0;
	s_holdHave = 0;
	s_holdUsed = 0;
	g_play.ntrk = 0;
	g_play.song = NULL;
	g_play.primed = 0;
}

void ZmusicSessionRewind()
{
	if (!s_session) return;
	ZmusicRewindEngine();
}

int ZmusicSessionIsOpen()
{
	return s_session;
}

static void ZmusicPushMonitor(int frames)
{
	unsigned char regs[256];
	int key = -1;
	ZmusicCopyOpmRegs(regs, &key);
	FmMonShadowSetIdentity("X68000", "OPM");
	FmMonShadowSetSampleRate(44100);
	FmMonShadowSetOpmRegSnapshotEx(regs, key);
	if (frames > 0)
		FmMonShadowAddSamples((uint32_t)frames);
	FmMonShadowFlush(0);
}

static ZmusicSong s_song;

int ZmusicSessionOpen(const wchar_t* path)
{
	ZmusicSessionClose();
	if (!path || !path[0]) return ZMUSIC_ERR_ARG;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return ZMUSIC_ERR_ARG;
	DWORD sz = GetFileSize(h, NULL);
	if (sz < 8 || sz > sizeof(s_zmsText) - 1) {
		CloseHandle(h);
		return ZMUSIC_ERR_RANGE;
	}
	DWORD got = 0;
	if (!ReadFile(h, s_zmsText, sz, &got, NULL) || got != sz) {
		CloseHandle(h);
		return ZMUSIC_ERR_ARG;
	}
	CloseHandle(h);
	s_zmsText[got] = 0;
	int n = 0;
	const uint8_t* src = (const uint8_t*)s_zmsText;
	if (src[0] == 0x10 && got >= 8 && memcmp(src + 1, "ZmuSiC", 6) == 0) {
		if (got > sizeof(s_zmdMem)) return ZMUSIC_ERR_RANGE;
		memcpy(s_zmdMem, src, got);
		n = (int)got;
	} else {
		int line = 0;
		int rc = ZmusicCompileZms(s_zmsText, s_zmdMem, (int)sizeof(s_zmdMem), &n, &line);
		if (rc) return rc;
	}
	int rc = ZmusicOpenZmd(&s_song, s_zmdMem, n);
	if (rc) return rc;
	memset(s_opmRegs, 0, sizeof(s_opmRegs));
	s_opmKey = -1;
	s_session = 1;
	FmMonShadowReset();
	FmMonShadowSetSource(path);
	FmMonShadowSetIdentity("X68000", "OPM");
	return ZMUSIC_OK;
}

int ZmusicSessionRead(unsigned char* dst, int bytes)
{
	if (!s_session || !dst || bytes < 4) return 0;
	int filled = 0;
	while (filled + 4 <= bytes) {
		if (s_holdUsed >= s_holdHave) {
			int got = 0;
			int rc = ZmusicRender(&s_song, s_hold, 2048, &got);
			if (rc || got < 1) break;
			s_holdHave = got * 2;
			s_holdUsed = 0;
			ZmusicPushMonitor(got);
		}
		int samples = s_holdHave - s_holdUsed;
		int room = (bytes - filled) / 2;
		if (samples > room) samples = room;
		memcpy(dst + filled, s_hold + s_holdUsed, (size_t)samples * 2);
		s_holdUsed += samples;
		filled += samples * 2;
	}
	return filled;
}

void ZmusicSessionSeekFrames(int frames)
{
	if (!s_session) return;
	if (frames < 0) frames = 0;
	ZmusicRewindEngine();
	int left = frames;
	while (left > 0) {
		int n = left > 2048 ? 2048 : left;
		int got = 0;
		int rc = ZmusicRender(&s_song, s_hold, n, &got);
		if (rc || got < 1) break;
		left -= got;
	}
	s_holdHave = 0;
	s_holdUsed = 0;
}
