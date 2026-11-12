/* 標準MIDI → ささみMIDI譜面 / FM譜面。 */
#include "stdafx.h"
#include "resource.h"
#include "SasamiComposerDoc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

enum { kSmfNoteMax = 48000, kSmfProgMax = 2048, kSmfTempoMax = 64 };

struct SmNote {
	uint32_t tick;
	uint16_t dur;
	uint8_t ch;
	uint8_t note;
	uint8_t vel;
	uint8_t cc7;
	uint8_t keep;
};

struct SmProg {
	uint32_t tick;
	uint8_t ch;
	uint8_t pc;
	uint8_t msb;
	uint8_t lsb;
};

struct SmTempo {
	uint32_t tick;
	int tempoT;
};

struct SmMeter {
	uint32_t tick;
	uint8_t num;
	uint8_t den;
};

enum { kDmReset = 1, kDmGs = 2, kDmBank = 3, kDmXg = 4, kDmEvMax = 256 };

struct SmDrumEv {
	uint32_t tick;
	uint16_t seq;
	uint8_t kind;
	uint8_t a;
	uint8_t b;
	uint8_t c;
};

struct SmBag {
	SmNote* notes;
	int nNotes;
	SmProg* progs;
	int nProgs;
	SmTempo tempos[kSmfTempoMax];
	int nTempos;
	SmMeter meters[16];
	int nMeters;
	int ppqn;
	int dropped;
	/* 0 不明、1 ドラム、2 明示的に通常。ポートBは 16〜31。 */
	uint8_t drumPart[32];
	uint8_t drumSlot[32];
	uint8_t gsReset;
	uint8_t block50;
	uint8_t portB; /* FF 21 でポートBがある */
	SmDrumEv drumEv[kDmEvMax];
	int nDrumEv;
};

static int PushEv(ScEvent* ev, int* n, uint32_t tick, uint8_t ch, uint8_t kind,
	uint8_t a, uint8_t b, uint8_t c, uint16_t dur)
{
	if (!ev || !n || *n >= SC_EV_MAX) return 0;
	ScEvent& e = ev[*n];
	e.tick = tick;
	e.seq = (uint32_t)(*n);
	e.ch = ch;
	e.kind = kind;
	e.a = a;
	e.b = b;
	e.c = c;
	e.dur = dur;
	e.flags = 0;
	e.pad0 = 0;
	(*n)++;
	return 1;
}

static uint32_t RdU32(const uint8_t* p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int RdVlq(const uint8_t* p, int n, int* used, uint32_t* v)
{
	uint32_t x = 0;
	int i = 0;
	if (!p || n <= 0) return 0;
	for (; i < 4 && i < n; i++) {
		x = (x << 7) | (p[i] & 0x7F);
		if ((p[i] & 0x80) == 0) { i++; *used = i; *v = x; return 1; }
	}
	return 0;
}

static uint32_t ScaleTick(uint32_t t, int ppqn)
{
	if (ppqn < 1) ppqn = 480;
	return (uint32_t)(((uint64_t)t * (uint32_t)SC_PPQN + (uint32_t)(ppqn / 2)) / (uint32_t)ppqn);
}

static int TempoFromUs(uint32_t us)
{
	if (us < 1) us = 500000;
	/* T = 13000 * 120 / bpm, bpm = 60000000/us → T = 13000 * us / 500000 */
	uint32_t t = (uint32_t)((13000ull * us) / 500000ull);
	if (t < 2000) t = 2000;
	if (t > 65000) t = 65000;
	return (int)t;
}

static int PortCh(int port, int ch4)
{
	int c = port * 16 + (ch4 & 15);
	if (c < 0 || c > 31) c = ch4 & 15;
	return c;
}

/* GS 40 1n の n。n=0 は ch10、1〜9 は ch1〜9、A〜F は ch11〜16。 */
static int GsNibbleToCh(int n)
{
	n &= 15;
	if (n == 0) return 9;
	if (n < 10) return n - 1;
	return n;
}

static void PushDrumEv(SmBag* bag, uint32_t tick, int kind, int a, int b, int c)
{
	if (!bag || bag->nDrumEv >= kDmEvMax) return;
	SmDrumEv& e = bag->drumEv[bag->nDrumEv];
	e.tick = tick;
	e.seq = (uint16_t)bag->nDrumEv;
	e.kind = (uint8_t)kind;
	e.a = (uint8_t)a;
	e.b = (uint8_t)b;
	e.c = (uint8_t)c;
	bag->nDrumEv++;
}

static void ApplyDrumTo(int* mode, int* slot, SmBag* bag, const SmDrumEv& e)
{
	if (e.kind == kDmReset) {
		for (int ch = 0; ch < 32; ch++) {
			mode[ch] = 2;
			slot[ch] = 0;
		}
		mode[9] = 1;
		slot[9] = 0;
		bag->gsReset = 1;
		/* ポートBがある曲だけ、リセット後の B ch10 はドラム2。 */
		if (bag->portB) {
			mode[25] = 1;
			slot[25] = 1;
		}
		return;
	}
	if (e.kind == kDmGs) {
		int ch = GsNibbleToCh(e.b);
		if (e.a == 0x50) {
			/* 2本目のポートが無いファイルの 50 は、kbsasami と同じくパートに効かせない。 */
			if (!bag->portB) return;
			ch += 16;
		} else if (e.a != 0x40) {
			return;
		}
		if (ch < 0 || ch > 31) return;
		if (e.c <= 0) {
			mode[ch] = 2;
			slot[ch] = 0;
		} else if (e.c == 2) {
			mode[ch] = 1;
			slot[ch] = 1;
		} else {
			mode[ch] = 1;
			slot[ch] = 0;
		}
		return;
	}
	if (e.kind == kDmXg) {
		const int ch = e.a & 15;
		if (e.c == 0) {
			mode[ch] = 2;
			slot[ch] = 0;
		} else if (mode[ch] != 2) {
			mode[ch] = 1;
			slot[ch] = 0;
		}
		return;
	}
	if (e.kind == kDmBank) {
		const int ch = e.a;
		if (ch >= 0 && ch < 32 && mode[ch] != 2) {
			mode[ch] = 1;
			slot[ch] = 0;
		}
	}
}

/* 同じ tick ではリセットを先、その後の 15h が勝つ。曲末より後の指定は無視。 */
static int DrumEvCmp(const void* a, const void* b)
{
	const SmDrumEv* x = (const SmDrumEv*)a;
	const SmDrumEv* y = (const SmDrumEv*)b;
	if (x->tick != y->tick) return (x->tick < y->tick) ? -1 : 1;
	if (x->kind != y->kind) return (int)x->kind - (int)y->kind;
	return (int)x->seq - (int)y->seq;
}

/* 最終的なパートモード。音符が終わった後の GS リセットでは戻さない。 */
static void ResolveDrumParts(SmBag* bag)
{
	if (!bag) return;
	uint32_t lastNote = 0;
	int anyNote = 0;
	for (int i = 0; i < bag->nNotes; i++) {
		anyNote = 1;
		if (bag->notes[i].tick > lastNote) lastNote = bag->notes[i].tick;
		if (bag->notes[i].ch >= 16) bag->portB = 1;
	}
	if (bag->nDrumEv > 1)
		qsort(bag->drumEv, (size_t)bag->nDrumEv, sizeof(SmDrumEv), DrumEvCmp);
	int mode[32], slot[32];
	memset(mode, 0, sizeof(mode));
	memset(slot, 0, sizeof(slot));
	for (int i = 0; i < bag->nDrumEv; i++) {
		if (anyNote && bag->drumEv[i].tick > lastNote) continue;
		ApplyDrumTo(mode, slot, bag, bag->drumEv[i]);
	}
	for (int ch = 0; ch < 32; ch++) {
		bag->drumPart[ch] = (uint8_t)mode[ch];
		bag->drumSlot[ch] = (uint8_t)(slot[ch] ? 1 : 0);
	}
}

/* GS は 40 1n / 50 1n の 15h。50 はポートB（ch17〜32）。値 0 通常、1 ドラム1、2 ドラム2。 */
static void NoteDrumSysex(SmBag* bag, uint32_t tick, const uint8_t* s, int n)
{
	if (!bag || !s || n < 8) return;
	if (s[0] == 0xF0) {
		s++;
		n--;
		if (n < 8) return;
	}
	if (s[0] == 0x41 && n >= 9 && s[2] == 0x42 && s[3] == 0x12) {
		if (s[4] == 0x40 && s[5] == 0x00 && s[6] == 0x7F && s[7] == 0x00) {
			PushDrumEv(bag, tick, kDmReset, 0, 0, 0);
			return;
		}
		if ((s[4] == 0x40 || s[4] == 0x50) && (s[5] & 0xF0) == 0x10 && s[6] == 0x15) {
			if (s[4] == 0x50) bag->block50 = 1;
			PushDrumEv(bag, tick, kDmGs, s[4], s[5] & 0x0F, s[7] & 0x7F);
			return;
		}
	}
	if (s[0] == 0x43 && n >= 8 && s[2] == 0x4C && s[3] == 0x08 && s[5] == 0x07) {
		const int ch = s[4] & 0x0F;
		PushDrumEv(bag, tick, kDmXg, ch, 0, (s[6] == 0) ? 0 : 1);
	}
}

static int ParseSmf(const uint8_t* b, int n, SmBag* bag)
{
	if (!b || n < 22 || !bag) return 0;
	if (memcmp(b, "MThd", 4) != 0) return 0;
	const uint32_t hlen = RdU32(b + 4);
	if (hlen < 6 || 8 + (int)hlen > n) return 0;
	const int fmt = (b[8] << 8) | b[9];
	const int ntr = (b[10] << 8) | b[11];
	int div = (b[12] << 8) | b[13];
	if (div & 0x8000) return 0;
	if (div < 1) div = 480;
	bag->ppqn = div;
	if (fmt > 1) return 0;
	int off = 8 + (int)hlen;
	uint8_t openNote[32][128];
	uint32_t openTick[32][128];
	uint8_t openVel[32][128];
	uint8_t cc0[32], cc32[32], cc7[32];
	memset(openNote, 0, sizeof(openNote));
	memset(cc0, 0, sizeof(cc0));
	memset(cc32, 0, sizeof(cc32));
	for (int i = 0; i < 32; i++) cc7[i] = 100;
	for (int tr = 0; tr < ntr && off + 8 <= n; tr++) {
		int port = 0;
		if (memcmp(b + off, "MTrk", 4) != 0) break;
		const uint32_t tlen = RdU32(b + off + 4);
		off += 8;
		if (off + (int)tlen > n) break;
		const int end = off + (int)tlen;
		uint32_t tick = 0;
		int run = 0;
		while (off < end) {
			int used = 0;
			uint32_t dt = 0;
			if (!RdVlq(b + off, end - off, &used, &dt)) break;
			off += used;
			tick += dt;
			if (off >= end) break;
			int st = b[off];
			if (st < 0x80) {
				if (run < 0x80) break;
				st = run;
			} else {
				off++;
				if (st < 0xF0) run = st;
			}
			if (st == 0xFF) {
				if (off >= end) break;
				const int meta = b[off++];
				uint32_t ln = 0;
				if (!RdVlq(b + off, end - off, &used, &ln)) break;
				off += used;
				if (off + (int)ln > end) break;
				if (meta == 0x2F) { off = end; break; }
				if (meta == 0x21 && ln >= 1) {
					port = (b[off] & 1);
					if (port) bag->portB = 1;
				}
				if (meta == 0x51 && ln >= 3 && bag->nTempos < kSmfTempoMax) {
					const uint32_t us = ((uint32_t)b[off] << 16) | ((uint32_t)b[off + 1] << 8) | b[off + 2];
					bag->tempos[bag->nTempos].tick = ScaleTick(tick, div);
					bag->tempos[bag->nTempos].tempoT = TempoFromUs(us);
					bag->nTempos++;
				} else if (meta == 0x58 && ln >= 2 && bag->nMeters < 16) {
					int den = 1 << (b[off + 1] & 7);
					if (den < 1) den = 4;
					if (den > 32) den = 32;
					bag->meters[bag->nMeters].tick = ScaleTick(tick, div);
					bag->meters[bag->nMeters].num = b[off] ? b[off] : 4;
					bag->meters[bag->nMeters].den = (uint8_t)den;
					bag->nMeters++;
				}
				off += (int)ln;
				continue;
			}
			if (st == 0xF0 || st == 0xF7) {
				uint32_t ln = 0;
				if (!RdVlq(b + off, end - off, &used, &ln)) break;
				off += used;
				if (st == 0xF0 && off + (int)ln <= end)
					NoteDrumSysex(bag, ScaleTick(tick, div), b + off, (int)ln);
				off += (int)ln;
				continue;
			}
			const int cmd = st & 0xF0;
			const int ch = PortCh(port, st & 0x0F);
			if (cmd == 0xC0 || cmd == 0xD0) {
				if (off >= end) break;
				const int d1 = b[off++] & 0x7F;
				if (cmd == 0xC0 && bag->nProgs < kSmfProgMax) {
					bag->progs[bag->nProgs].tick = ScaleTick(tick, div);
					bag->progs[bag->nProgs].ch = (uint8_t)ch;
					bag->progs[bag->nProgs].pc = (uint8_t)d1;
					bag->progs[bag->nProgs].msb = cc0[ch];
					bag->progs[bag->nProgs].lsb = cc32[ch];
					bag->nProgs++;
					if (cc0[ch] >= 120)
						PushDrumEv(bag, ScaleTick(tick, div), kDmBank, ch, 0, 1);
				}
				continue;
			}
			if (off + 1 >= end) break;
			const int d1 = b[off++] & 0x7F;
			const int d2 = b[off++] & 0x7F;
			if (cmd == 0xB0) {
				if (d1 == 0) cc0[ch] = (uint8_t)d2;
				else if (d1 == 32) cc32[ch] = (uint8_t)d2;
				else if (d1 == 7) cc7[ch] = (uint8_t)(d2 ? d2 : 1);
				continue;
			}
			if (cmd == 0x90 && d2 > 0) {
				if (openNote[ch][d1]) {
					/* 同じ音の打ち直し。前の音をここで閉じる。 */
					if (bag->nNotes < kSmfNoteMax) {
						SmNote& nn = bag->notes[bag->nNotes++];
						nn.tick = ScaleTick(openTick[ch][d1], div);
						uint32_t endT = ScaleTick(tick, div);
						uint32_t dur = (endT > nn.tick) ? (endT - nn.tick) : 1;
						if (dur > 65535) dur = 65535;
						nn.dur = (uint16_t)dur;
						nn.ch = (uint8_t)ch;
						nn.note = (uint8_t)d1;
						nn.vel = openVel[ch][d1] ? openVel[ch][d1] : 100;
						nn.cc7 = cc7[ch] ? cc7[ch] : 100;
						nn.keep = 1;
					} else bag->dropped++;
				}
				openNote[ch][d1] = 1;
				openTick[ch][d1] = tick;
				openVel[ch][d1] = (uint8_t)d2;
				continue;
			}
			if (cmd == 0x80 || (cmd == 0x90 && d2 == 0)) {
				if (!openNote[ch][d1]) continue;
				openNote[ch][d1] = 0;
				if (bag->nNotes >= kSmfNoteMax) { bag->dropped++; continue; }
				SmNote& nn = bag->notes[bag->nNotes++];
				nn.tick = ScaleTick(openTick[ch][d1], div);
				uint32_t endT = ScaleTick(tick, div);
				uint32_t dur = (endT > nn.tick) ? (endT - nn.tick) : 1;
				if (dur > 65535) dur = 65535;
				nn.dur = (uint16_t)dur;
				nn.ch = (uint8_t)ch;
				nn.note = (uint8_t)d1;
				nn.vel = openVel[ch][d1] ? openVel[ch][d1] : 100;
				nn.cc7 = cc7[ch] ? cc7[ch] : 100;
				nn.keep = 1;
			}
		}
		off = end;
	}
	/* ノートオフが無い音は四分で閉じる。 */
	for (int ch = 0; ch < 32; ch++) {
		for (int nt = 0; nt < 128; nt++) {
			if (!openNote[ch][nt]) continue;
			if (bag->nNotes >= kSmfNoteMax) { bag->dropped++; continue; }
			SmNote& nn = bag->notes[bag->nNotes++];
			nn.tick = ScaleTick(openTick[ch][nt], div);
			nn.dur = (uint16_t)SC_PPQN;
			nn.ch = (uint8_t)ch;
			nn.note = (uint8_t)nt;
			nn.vel = openVel[ch][nt] ? openVel[ch][nt] : 100;
			nn.cc7 = cc7[ch] ? cc7[ch] : 100;
			nn.keep = 1;
		}
	}
	ResolveDrumParts(bag);
	return bag->nNotes > 0;
}

static int ReadFileAll(const wchar_t* path, uint8_t** out, int* outN)
{
	*out = NULL;
	*outN = 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD sz = GetFileSize(h, NULL);
	if (sz < 22 || sz > 16 * 1024 * 1024) { CloseHandle(h); return 0; }
	uint8_t* p = (uint8_t*)malloc(sz);
	if (!p) { CloseHandle(h); return 0; }
	DWORD rd = 0;
	ReadFile(h, p, sz, &rd, NULL);
	CloseHandle(h);
	if (rd != sz) { free(p); return 0; }
	*out = p;
	*outN = (int)sz;
	return 1;
}

static int NoteCmp(const void* a, const void* b)
{
	const SmNote* x = (const SmNote*)a;
	const SmNote* y = (const SmNote*)b;
	if (x->tick != y->tick) return (x->tick < y->tick) ? -1 : 1;
	if (x->ch != y->ch) return (int)x->ch - (int)y->ch;
	return (int)x->note - (int)y->note;
}

static void PutTempos(ScEvent* ev, int* n, int ch, uint8_t kind, const SmBag* bag, int* tempoT)
{
	int t0 = 13000;
	if (bag->nTempos > 0) t0 = bag->tempos[0].tempoT;
	*tempoT = t0;
	PushEv(ev, n, 0, (uint8_t)ch, kind, (uint8_t)(t0 & 0xFF), (uint8_t)((t0 >> 8) & 0xFF), 0, 0);
	for (int i = 1; i < bag->nTempos; i++) {
		int t = bag->tempos[i].tempoT;
		PushEv(ev, n, bag->tempos[i].tick, 0, kind, (uint8_t)(t & 0xFF), (uint8_t)((t >> 8) & 0xFF), 0, 0);
	}
}

int ScMidiDocFromSmfFile(ScMidiDoc* d, const wchar_t* path, int* dropped)
{
	if (dropped) *dropped = 0;
	if (!d || !path || !path[0]) return 0;
	uint8_t* raw = NULL;
	int rawN = 0;
	if (!ReadFileAll(path, &raw, &rawN)) return 0;
	SmBag bag;
	memset(&bag, 0, sizeof(bag));
	bag.notes = (SmNote*)malloc(sizeof(SmNote) * kSmfNoteMax);
	bag.progs = (SmProg*)malloc(sizeof(SmProg) * kSmfProgMax);
	if (!bag.notes || !bag.progs) {
		free(bag.notes); free(bag.progs); free(raw);
		return 0;
	}
	const int ok = ParseSmf(raw, rawN, &bag);
	free(raw);
	if (!ok) {
		if (dropped) *dropped = bag.dropped;
		free(bag.notes); free(bag.progs);
		return 0;
	}
	ScMidiDocClear(d);
	PutTempos(d->ev, &d->evCount, 0, SC_EV_TEMPO, &bag, &d->tempoT);
	if (bag.nMeters > 0) {
		d->numer = bag.meters[0].num ? bag.meters[0].num : 4;
		d->denom = bag.meters[0].den ? bag.meters[0].den : 4;
		for (int i = 0; i < bag.nMeters; i++)
			ScMidiAddMeter(d, bag.meters[i].tick, bag.meters[i].num, bag.meters[i].den);
	}
	/* ch10/26 は無指定ならドラム。40 10 15 00 は ch10 を通常へ。
	   ポートBがあるときだけ 50 1n 15 を ch17〜32 に載せる。値 2 はドラム2。
	   ドラムは音部記号も Dr にする。指定だけでは譜面が Dr にならない。 */
	if (bag.portB) d->bind.dualPort = 1;
	for (int ch = 0; ch < 32; ch++) {
		int notes = 0;
		for (int i = 0; i < bag.nNotes; i++) if (bag.notes[i].ch == (uint8_t)ch) notes++;
		if (!notes && bag.drumPart[ch] == 0) continue;
		const int defDrum = (ch == 9 || ch == 25) ? 1 : 0;
		int drum = defDrum;
		int slot = 0;
		if (bag.drumPart[ch] == 1) {
			drum = 1;
			slot = bag.drumSlot[ch] ? 1 : 0;
		} else if (bag.drumPart[ch] == 2) {
			drum = 0;
		}
		if (ch >= 16 && (notes || drum)) d->bind.dualPort = 1;
		if (drum != defDrum || (drum && slot))
			ScMidiAddDrumEx(d, 0, ch, drum, slot);
		else if (drum && notes)
			ScMidiAddDrum(d, 0, ch, 1);
		if (drum)
			ScMidiAddClef(d, 0, ch, 3);
	}
	for (int i = 0; i < bag.nProgs; i++) {
		const SmProg& p = bag.progs[i];
		ScMidiApplyProgBankAt(d, p.ch, p.tick, p.pc, p.msb, p.lsb);
	}
	qsort(bag.notes, (size_t)bag.nNotes, sizeof(SmNote), NoteCmp);
	int landed = 0;
	for (int i = 0; i < bag.nNotes; i++) {
		const SmNote& nn = bag.notes[i];
		int dur = nn.dur ? nn.dur : 1;
		if (!ScMidiAddNote(d, nn.tick, nn.ch, nn.note, dur, nn.vel ? nn.vel : 100, 100)) {
			bag.dropped++;
			continue;
		}
		landed++;
	}
	if (dropped) *dropped = bag.dropped;
	free(bag.notes);
	free(bag.progs);
	return landed > 0;
}

/* ---- FM: WOPN → 25バイト音色、6FM + 3SSG + リズム ---- */

struct WopnVoice {
	uint8_t raw[25];
	int8_t semi;
	uint8_t ok;
};

static void PackWopnOps(uint8_t* dst, const uint8_t* inst, int stride)
{
	/* inst: WOPN instrument. ops at +37, 7 bytes each, register order. */
	const uint8_t* p = inst;
	const int alg = p[35] & 7;
	const int fb = (p[35] >> 3) & 7;
	for (int k = 0; k < 4; k++) {
		const uint8_t* r = p + 37 + k * 7;
		const int ml = r[0] & 15;
		const int dt = (r[0] >> 4) & 7;
		const int tl = r[1] & 127;
		const int ar = r[2] & 31;
		const int ks = r[2] >> 6;
		const int dr = r[3] & 31;
		const int am = (r[3] & 0x80) ? 1 : 0;
		const int sr = r[4] & 31;
		const int rr = r[5] & 15;
		const int sl = r[5] >> 4;
		dst[k] = (uint8_t)((dt << 4) | ml);
		dst[4 + k] = (uint8_t)tl;
		dst[8 + k] = (uint8_t)((ks << 6) | ar);
		dst[12 + k] = (uint8_t)((am << 7) | dr);
		dst[16 + k] = (uint8_t)sr;
		dst[20 + k] = (uint8_t)((sl << 4) | rr);
	}
	dst[24] = (uint8_t)((fb << 3) | alg);
	(void)stride;
}

static int WopnAlive(const uint8_t* inst)
{
	for (int k = 0; k < 4; k++) {
		const uint8_t* r = inst + 37 + k * 7;
		const int ar = r[2] & 31;
		const int tl = r[1] & 127;
		if (tl < 120 && ar > 0) return 1;
	}
	return 0;
}

static int LoadGsVoices(WopnVoice* tab)
{
	memset(tab, 0, sizeof(WopnVoice) * 128);
	HMODULE hi = GetModuleHandleW(NULL);
	HRSRC hr = FindResourceW(hi, MAKEINTRESOURCEW(IDR_GS_WOPN), RT_RCDATA);
	if (!hr) return 0;
	HGLOBAL hg = LoadResource(hi, hr);
	const DWORD sz = hg ? SizeofResource(hi, hr) : 0;
	const uint8_t* b = (hg && sz >= 32) ? (const uint8_t*)LockResource(hg) : NULL;
	if (!b) return 0;
	const int n = (int)sz;
	int ver = 0;
	int o = 0;
	if (n >= 13 && memcmp(b, "WOPN2-B2NK", 11) == 0) {
		ver = b[11] | (b[12] << 8);
		o = 13;
	} else if (n >= 11 && memcmp(b, "WOPN2-BANK", 11) == 0) {
		ver = 1;
		o = 11;
	} else {
		return 0;
	}
	if (o + 5 > n) return 0;
	const int mb = (b[o] << 8) | b[o + 1];
	const int pb = (b[o + 2] << 8) | b[o + 3];
	o += 5;
	if (mb < 1 || mb > 128 || pb < 0 || pb > 128) return 0;
	const int stride = (ver >= 2) ? 69 : 65;
	const int nb = mb + pb;
	if (o + nb * 34 + nb * 128 * stride > n) return 0;
	struct Id { int msb, lsb, perc; } ids[256];
	for (int i = 0; i < nb; i++) {
		ids[i].lsb = b[o + 32];
		ids[i].msb = b[o + 33];
		ids[i].perc = i >= mb;
		o += 34;
	}
	int got = 0;
	for (int i = 0; i < nb; i++) {
		for (int pc = 0; pc < 128; pc++) {
			const uint8_t* p = b + o;
			o += stride;
			if (ids[i].perc) continue;
			if (ids[i].msb != 0 || ids[i].lsb != 0) continue;
			if (tab[pc].ok) continue;
			if (!WopnAlive(p)) continue;
			PackWopnOps(tab[pc].raw, p, stride);
			int semi = (int)((p[32] << 8) | p[33]);
			if (semi >= 32768) semi -= 65536;
			if (semi < -24) semi = -24;
			if (semi > 24) semi = 24;
			tab[pc].semi = (int8_t)semi;
			tab[pc].ok = 1;
			got++;
		}
	}
	if (tab[0].ok) {
		for (int pc = 0; pc < 128; pc++) {
			if (tab[pc].ok) continue;
			memcpy(tab[pc].raw, tab[0].raw, 25);
			tab[pc].semi = tab[0].semi;
			tab[pc].ok = 1;
		}
	}
	return got > 0;
}

/* FVOL はキャリア TL を置き換える。0 が最大、127 が無音。ベロシティをそのまま引くと消える。 */
static int FmAudibleTl(int vel, int cc7)
{
	if (vel < 1) vel = 100;
	if (cc7 < 1) cc7 = 100;
	int tl = (127 - vel) * 18 / 127;
	tl += (127 - cc7) * 10 / 127;
	if (tl < 6) tl = 6;
	if (tl > 32) tl = 32;
	return tl;
}

static const uint8_t kFmFallbackVoice[25] = {
	0x3B, 0x00, 0x00, 0x20, 0x28, 0x20, 0x1A, 0x0D, 0x9F, 0x9E, 0xDE, 0x9E,
	0x05, 0x05, 0x05, 0x05, 0x0F, 0x0B, 0x0C, 0x0B, 0x8A, 0xF6, 0x86, 0xF7, 0x1B
};

static uint8_t FmByte(int midi)
{
	if (midi < 0) midi = 0;
	if (midi > 127) midi = 127;
	int oct = midi / 12 - 1;
	int sc = midi % 12;
	if (oct < 0) oct = 0;
	if (oct > 8) oct = 8;
	return (uint8_t)((oct << 4) | (sc & 15));
}

static int GmPad(int note)
{
	if (note == 35 || note == 36) return 0;
	if (note == 38 || note == 40 || note == 39) return 1;
	if (note == 49 || note == 51 || note == 52 || note == 55 || note == 57 || note == 59) return 2;
	if (note == 42 || note == 44) return 3;
	if (note == 46) return 2;
	if (note == 41 || note == 43 || note == 45 || note == 47 || note == 48 || note == 50) return 4;
	if (note == 37) return 5;
	return -1;
}

static int ProgAt(const SmBag* bag, int ch, uint32_t tick)
{
	int pc = 0;
	for (int i = 0; i < bag->nProgs; i++) {
		if (bag->progs[i].ch != (uint8_t)ch) continue;
		if (bag->progs[i].tick > tick) break;
		pc = bag->progs[i].pc;
	}
	return pc;
}

static int IsDrumCh(const SmBag* bag, int ch, int noteCount)
{
	if (ch < 0 || ch > 31) return 0;
	if (bag->drumPart[ch] == 1) return 1;
	if (bag->drumPart[ch] == 2) return 0;
	if ((ch % 16) == 9) return 1;
	if (noteCount < 4) return 0;
	int drumPc = 0;
	for (int i = 0; i < bag->nProgs; i++) {
		if (bag->progs[i].ch == (uint8_t)ch && bag->progs[i].msb >= 120)
			drumPc = 1;
	}
	if (!drumPc) return 0;
	int hits = 0;
	for (int i = 0; i < bag->nNotes; i++) {
		if (bag->notes[i].ch != (uint8_t)ch) continue;
		if (bag->notes[i].note >= 27 && bag->notes[i].note <= 87) hits++;
	}
	return hits * 2 >= noteCount;
}

/* 先頭の SysEx セットアップで空いている小節を捨て、最初のノートがある小節を 1 小節目にする。 */
static void DropFmLeadBars(SmBag* bag)
{
	if (!bag) return;
	uint32_t first = 0xFFFFFFFFu;
	for (int i = 0; i < bag->nNotes; i++) {
		if (!bag->notes[i].keep) continue;
		if (bag->notes[i].tick < first) first = bag->notes[i].tick;
	}
	if (first == 0xFFFFFFFFu || first == 0) return;
	int num = 4;
	int den = 4;
	if (bag->nMeters > 0) {
		if (bag->meters[0].num) num = bag->meters[0].num;
		if (bag->meters[0].den) den = bag->meters[0].den;
	}
	if (den < 1) den = 4;
	int bar = num * ((SC_PPQN * 4) / den);
	if (bar < SC_PPQN) bar = SC_PPQN * 4;
	const uint32_t shift = (first / (uint32_t)bar) * (uint32_t)bar;
	if (!shift) return;
	for (int i = 0; i < bag->nNotes; i++) {
		if (bag->notes[i].tick >= shift) bag->notes[i].tick -= shift;
		else if (bag->notes[i].keep) { bag->notes[i].keep = 0; bag->dropped++; }
	}
	for (int i = 0; i < bag->nProgs; i++) {
		if (bag->progs[i].tick > shift) bag->progs[i].tick -= shift;
		else bag->progs[i].tick = 0;
	}
	SmTempo tempos[kSmfTempoMax];
	int nt = 0;
	int keepT = -1;
	for (int i = 0; i < bag->nTempos; i++)
		if (bag->tempos[i].tick <= shift) keepT = i;
	if (keepT >= 0) {
		tempos[nt].tick = 0;
		tempos[nt].tempoT = bag->tempos[keepT].tempoT;
		nt++;
	}
	for (int i = 0; i < bag->nTempos; i++) {
		if (bag->tempos[i].tick <= shift || nt >= kSmfTempoMax) continue;
		tempos[nt].tick = bag->tempos[i].tick - shift;
		tempos[nt].tempoT = bag->tempos[i].tempoT;
		nt++;
	}
	for (int i = 0; i < nt; i++) bag->tempos[i] = tempos[i];
	bag->nTempos = nt;
	SmMeter meters[16];
	int nm = 0;
	int keepM = -1;
	for (int i = 0; i < bag->nMeters; i++)
		if (bag->meters[i].tick <= shift) keepM = i;
	if (keepM >= 0 && nm < 16) {
		meters[nm] = bag->meters[keepM];
		meters[nm].tick = 0;
		nm++;
	}
	for (int i = 0; i < bag->nMeters; i++) {
		if (bag->meters[i].tick <= shift || nm >= 16) continue;
		meters[nm] = bag->meters[i];
		meters[nm].tick = bag->meters[i].tick - shift;
		nm++;
	}
	for (int i = 0; i < nm; i++) bag->meters[i] = meters[i];
	bag->nMeters = nm;
}

static int BassPc(const SmBag* bag, int ch)
{
	const int pc = ProgAt(bag, ch, 0);
	return (pc >= 32 && pc <= 39) ? 1 : 0;
}

int ScFmDocFromSmfFile(ScFmDoc* d, const wchar_t* path, int* dropped)
{
	if (dropped) *dropped = 0;
	if (!d || !path || !path[0]) return 0;
	uint8_t* raw = NULL;
	int rawN = 0;
	if (!ReadFileAll(path, &raw, &rawN)) return 0;
	SmBag bag;
	memset(&bag, 0, sizeof(bag));
	bag.notes = (SmNote*)malloc(sizeof(SmNote) * kSmfNoteMax);
	bag.progs = (SmProg*)malloc(sizeof(SmProg) * kSmfProgMax);
	if (!bag.notes || !bag.progs) {
		free(bag.notes); free(bag.progs); free(raw);
		return 0;
	}
	const int ok = ParseSmf(raw, rawN, &bag);
	free(raw);
	if (!ok) {
		if (dropped) *dropped = bag.dropped;
		free(bag.notes); free(bag.progs);
		return 0;
	}
	qsort(bag.notes, (size_t)bag.nNotes, sizeof(SmNote), NoteCmp);
	qsort(bag.progs, (size_t)bag.nProgs, sizeof(SmProg),
		[](const void* a, const void* b) -> int {
			const SmProg* x = (const SmProg*)a;
			const SmProg* y = (const SmProg*)b;
			if (x->tick != y->tick) return (x->tick < y->tick) ? -1 : 1;
			return (int)x->ch - (int)y->ch;
		});

	int count[32];
	memset(count, 0, sizeof(count));
	for (int i = 0; i < bag.nNotes; i++) if (bag.notes[i].ch < 32) count[bag.notes[i].ch]++;

	int drumCh[32];
	int nDrum = 0;
	int melCh[32];
	int nMel = 0;
	for (int ch = 0; ch < 32; ch++) {
		if (!count[ch]) continue;
		if (IsDrumCh(&bag, ch, count[ch])) drumCh[nDrum++] = ch;
		else melCh[nMel++] = ch;
	}
	for (int i = 0; i < nMel; i++) {
		for (int j = i + 1; j < nMel; j++) {
			if (count[melCh[j]] > count[melCh[i]]) {
				int t = melCh[i]; melCh[i] = melCh[j]; melCh[j] = t;
			}
		}
	}
	/* FM 0,1,2,7,8,9 の6本、SSG 3,4,5 の3本。残りは捨てる。 */
	static const int kFmSlot[6] = { 0, 1, 2, 7, 8, 9 };
	static const int kSsgSlot[3] = { 3, 4, 5 };
	int destOf[32];
	int role[32]; /* 0 none, 1 fm, 2 ssg, 3 drum */
	for (int i = 0; i < 32; i++) { destOf[i] = -1; role[i] = 0; }
	for (int i = 0; i < nMel && i < 6; i++) {
		destOf[melCh[i]] = kFmSlot[i];
		role[melCh[i]] = 1;
	}
	for (int i = 6; i < nMel && i < 9; i++) {
		destOf[melCh[i]] = kSsgSlot[i - 6];
		role[melCh[i]] = 2;
	}
	for (int i = 9; i < nMel; i++) bag.dropped += count[melCh[i]];
	const int rhy = 6;

	/* 和音は1音。ベース系は低い方、それ以外は高い方。重なりは後ろを優先して前を切る。 */
	for (int ch = 0; ch < 32; ch++) {
		if (role[ch] != 1 && role[ch] != 2) continue;
		const int bass = (role[ch] == 1) && BassPc(&bag, ch);
		int i = 0;
		while (i < bag.nNotes) {
			if (bag.notes[i].ch != (uint8_t)ch || !bag.notes[i].keep) { i++; continue; }
			int j = i + 1;
			while (j < bag.nNotes && bag.notes[j].ch == (uint8_t)ch && bag.notes[j].tick == bag.notes[i].tick)
				j++;
			int win = i;
			for (int k = i + 1; k < j; k++) {
				if (!bag.notes[k].keep) continue;
				if (bass) {
					if (bag.notes[k].note < bag.notes[win].note) win = k;
				} else if (bag.notes[k].note > bag.notes[win].note) win = k;
			}
			for (int k = i; k < j; k++) {
				if (k != win && bag.notes[k].keep) { bag.notes[k].keep = 0; bag.dropped++; }
			}
			i = j;
		}
		int prev = -1;
		for (int k = 0; k < bag.nNotes; k++) {
			if (bag.notes[k].ch != (uint8_t)ch || !bag.notes[k].keep) continue;
			if (prev >= 0) {
				uint32_t end = bag.notes[prev].tick + bag.notes[prev].dur;
				if (end > bag.notes[k].tick) {
					uint32_t nd = (bag.notes[k].tick > bag.notes[prev].tick)
						? (bag.notes[k].tick - bag.notes[prev].tick) : 1;
					if (nd > 65535) nd = 65535;
					bag.notes[prev].dur = (uint16_t)nd;
				}
			}
			prev = k;
		}
	}

	DropFmLeadBars(&bag);

	WopnVoice tab[128];
	LoadGsVoices(tab);

	ScFmDocClear(d);
	d->opna10 = 1;
	PutTempos(d->ev, &d->evCount, 0, SC_EV_FM_TEMPO, &bag, &d->tempoT);
	if (bag.nMeters > 0) {
		d->numer = bag.meters[0].num ? bag.meters[0].num : 4;
		d->denom = bag.meters[0].den ? bag.meters[0].den : 4;
		for (int i = 0; i < bag.nMeters; i++)
			ScFmAddMeter(d, bag.meters[i].tick, bag.meters[i].num, bag.meters[i].den);
	}

	int voiceOfPc[128];
	for (int i = 0; i < 128; i++) voiceOfPc[i] = -1;
	int voiced[10];
	int lastPc[10];
	memset(voiced, 0, sizeof(voiced));
	memset(lastPc, 0xFF, sizeof(lastPc));

	int landed = 0;
	for (int i = 0; i < bag.nNotes; i++) {
		const SmNote& nn = bag.notes[i];
		if (!nn.keep) continue;
		const int dst = destOf[nn.ch];
		if (dst < 0) continue;
		if (role[nn.ch] == 1) {
			const int pc = ProgAt(&bag, nn.ch, nn.tick);
			if (voiceOfPc[pc] < 0 && tab[pc].ok)
				voiceOfPc[pc] = ScFmAllocVoice(d, tab[pc].raw);
			if (voiceOfPc[pc] < 0)
				voiceOfPc[pc] = ScFmAllocVoice(d, kFmFallbackVoice);
			if (voiceOfPc[pc] >= 0 && lastPc[dst] != pc) {
				ScFmAddVoiceSelect(d, voiced[dst] ? nn.tick : 0, dst, voiceOfPc[pc], 1);
				if (!voiced[dst]) {
					ScFmAddVolTl(d, 0, dst, FmAudibleTl(nn.vel, nn.cc7));
					voiced[dst] = 1;
				}
				lastPc[dst] = pc;
			}
			int midi = (int)nn.note + (int)tab[pc].semi;
			if (midi < 0) midi = 0;
			if (midi > 127) midi = 127;
			if (ScFmAddNote(d, nn.tick, dst, FmByte(midi), nn.dur ? nn.dur : 1))
				landed++;
			else bag.dropped++;
		} else if (role[nn.ch] == 2) {
			if (!voiced[dst]) {
				int v = (nn.vel ? nn.vel : 100) * (nn.cc7 ? nn.cc7 : 100) / (127 * 8);
				if (v < 9) v = 9;
				if (v > 15) v = 15;
				PushEv(d->ev, &d->evCount, 0, (uint8_t)dst, SC_EV_FM_VOL, (uint8_t)v, 1, 0, 0);
				voiced[dst] = 1;
			}
			if (ScFmAddNote(d, nn.tick, dst, FmByte(nn.note), nn.dur ? nn.dur : 1))
				landed++;
			else bag.dropped++;
		}
	}
	/* ドラムチャンネルは全部リズムへ。対応しないノートは捨てる。 */
	uint32_t lastPadTick[6];
	memset(lastPadTick, 0xFF, sizeof(lastPadTick));
	for (int di = 0; di < nDrum; di++) {
		const int ch = drumCh[di];
		for (int i = 0; i < bag.nNotes; i++) {
			if (bag.notes[i].ch != (uint8_t)ch) continue;
			const int pad = GmPad(bag.notes[i].note);
			if (pad < 0) { bag.dropped++; continue; }
			if (lastPadTick[pad] == bag.notes[i].tick) { bag.dropped++; continue; }
			lastPadTick[pad] = bag.notes[i].tick;
			uint16_t dur = bag.notes[i].dur;
			if (dur < 2) dur = 2;
			if (dur > 12) dur = 12;
			if (ScFmAddNote(d, bag.notes[i].tick, rhy, (uint8_t)pad, dur))
				landed++;
			else bag.dropped++;
		}
	}

	if (dropped) *dropped = bag.dropped;
	free(bag.notes);
	free(bag.progs);
	return landed > 0;
}
