#include "stdafx.h"
#include "OmakeTacticsAudio.h"
#include "OmakeTacticsData.h"
#include "resource.h"
#include "oggDlg.h"
#include <vorbis/vorbisfile.h>
#include <dsound.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 効果音は短い MML（最大3パート）を YM2203 相当の 4op FM と SSG で鳴らす。Ogg は使わない。
// 音色は場面ごと。ベル・金管・打撃・属性・斬撃を分け、同じ上昇アルペジオを使い回さない。

extern COggDlg* og;
extern int plf;
extern int ps;

enum { TAC_RATE = 44100, TAC_VOICES = 12 };

struct TacMem {
	const unsigned char* p;
	size_t n, o;
};
struct TacClip {
	short* pcm;
	int n;
	int rate;
	short* play;
	int playN;
};
struct TacVoice {
	LPDIRECTSOUNDBUFFER buf;
	int frames;
};

static LPDIRECTSOUND8 s_ds;
static LPDIRECTSOUNDBUFFER s_buf;
static DWORD s_bytes;
static DWORD s_write;
static int s_started;
static TacClip s_clip[TS_N];
static TacVoice s_voice[TAC_VOICES];
static OggVorbis_File s_vf;
static int s_bgm;
static int s_bgmCh;
static int s_bgmRate;
static ogg_int64_t s_loopA;
static ogg_int64_t s_loopB;
static short s_bpcm[8192];
static int s_bhave;
static int s_bpos;
static short s_bCurL, s_bCurR, s_bNextL, s_bNextR;
static float s_bFrac;
static int s_bPrimed;
static int s_pausedMain;
static DWORD s_cursorAt;
static int s_bgmPct = 6;
static int s_sePct = 8;
static wchar_t s_bgmPath[MAX_PATH];
static int s_bgmScene = -1;
static int s_bgmFade;
static float s_tempo = 1.f;
static float s_tempoGoal = 1.f;
static int s_bgmOut = 0;
static int s_bgmOutTotal = 0;
static CRITICAL_SECTION s_cs;
static LONG s_csInit;
static HANDLE s_th;
static volatile LONG s_run;

static void TacCsInit()
{
	if (s_csInit == 2) return;
	if (InterlockedCompareExchange(&s_csInit, 1, 0) == 0) {
		InitializeCriticalSection(&s_cs);
		InterlockedExchange(&s_csInit, 2);
	} else {
		while (s_csInit != 2) Sleep(0);
	}
}

struct TacLock {
	TacLock() { TacCsInit(); EnterCriticalSection(&s_cs); }
	~TacLock() { LeaveCriticalSection(&s_cs); }
};

static int SeDb(int pct)
{
	if (pct <= 0) return -10000;
	if (pct >= 10) return 0;
	int db = (int)(4000.f * log10f(pct / 10.f));
	if (db < -10000) db = -10000;
	return db;
}

void TacAudioSetVol(int bgm, int se)
{
	TacLock lk;
	if (bgm < 0) bgm = 0;
	if (bgm > 10) bgm = 10;
	if (se < 0) se = 0;
	if (se > 10) se = 10;
	s_bgmPct = bgm;
	s_sePct = se;
	int db = SeDb(se);
	for (int i = 0; i < TAC_VOICES; i++) if (s_voice[i].buf) s_voice[i].buf->SetVolume(db);
}

static const float kGain[TS_N] = {
	0.22f, 0.48f, 0.40f, 0.44f, 0.46f,
	0.52f, 0.46f, 0.28f, 0.50f, 0.44f,
	0.46f, 0.55f, 0.62f, 0.30f, 0.44f,
	0.42f, 0.50f, 0.56f, 0.46f, 0.50f,
	0.42f, 0.46f, 0.48f, 0.40f, 0.46f,
	0.40f, 0.48f, 0.32f, 0.36f, 0.50f,
	0.44f, 0.52f,
	0.50f, 0.44f, 0.54f, 0.48f, 0.46f, 0.42f, 0.50f, 0.42f,
	0.44f, 0.42f, 0.50f,
	0.52f, 0.46f, 0.52f, 0.48f, 0.50f,
	0.50f,
	0.56f, 0.48f, 0.58f, 0.50f, 0.50f, 0.46f, 0.56f, 0.46f,
	0.46f, 0.50f, 0.54f,
	0.54f, 0.62f,
	0.30f, 0.28f,
	0.50f, 0.44f, 0.38f, 0.36f, 0.46f, 0.48f, 0.46f,
	0.48f, 0.54f, 0.46f,
	0.52f, 0.50f, 0.40f, 0.40f, 0.46f
};

static size_t TacRead(void* dst, size_t sz, size_t nm, void* h)
{
	TacMem* m = (TacMem*)h;
	size_t want = sz * nm;
	size_t left = m->n > m->o ? m->n - m->o : 0;
	if (want > left) want = left;
	if (want) memcpy(dst, m->p + m->o, want);
	m->o += want;
	return sz ? want / sz : 0;
}
static int TacSeek(void* h, ogg_int64_t off, int whence)
{
	TacMem* m = (TacMem*)h;
	ogg_int64_t n = (ogg_int64_t)m->o;
	if (whence == SEEK_CUR) n += off;
	else if (whence == SEEK_END) n = (ogg_int64_t)m->n + off;
	else n = off;
	if (n < 0 || n >(ogg_int64_t)m->n) return -1;
	m->o = (size_t)n;
	return 0;
}
static int TacCloseMem(void*) { return 0; }
static long TacTell(void* h) { return (long)((TacMem*)h)->o; }

static ov_callbacks TacMemCb()
{
	ov_callbacks c;
	c.read_func = TacRead;
	c.seek_func = TacSeek;
	c.close_func = TacCloseMem;
	c.tell_func = TacTell;
	return c;
}

static int TacDecodeMem(const unsigned char* p, size_t n, TacClip& out)
{
	out.pcm = NULL; out.n = 0; out.rate = TAC_RATE;
	if (!p || n < 16) return 0;
	TacMem mem = { p, n, 0 };
	OggVorbis_File vf;
	ov_callbacks cb = TacMemCb();
	if (ov_open_callbacks(&mem, &vf, NULL, 0, cb) < 0) return 0;
	vorbis_info* vi = ov_info(&vf, -1);
	int ch = vi ? vi->channels : 1;
	int rate = vi ? (int)vi->rate : TAC_RATE;
	if (ch < 1) ch = 1;
	if (rate < 8000) rate = TAC_RATE;
	short* buf = NULL;
	int cap = 0, got = 0;
	int bs = 0;
	for (;;) {
		if (got + 4096 > cap) {
			int nc = cap ? cap * 2 : 16384;
			short* nb = (short*)realloc(buf, (size_t)nc * sizeof(short));
			if (!nb) break;
			buf = nb; cap = nc;
		}
		long r = ov_read(&vf, (char*)(buf + got), (cap - got) * (int)sizeof(short), 0, 2, 1, &bs);
		if (r <= 0) break;
		got += (int)(r / (int)sizeof(short));
	}
	ov_clear(&vf);
	if (!buf || got < 1) { free(buf); return 0; }
	if (ch > 1) {
		int frames = got / ch;
		short* mono = (short*)malloc((size_t)frames * sizeof(short));
		if (!mono) { free(buf); return 0; }
		for (int i = 0; i < frames; i++) {
			int s = 0;
			for (int c = 0; c < ch; c++) s += buf[i * ch + c];
			mono[i] = (short)(s / ch);
		}
		free(buf);
		buf = mono;
		got = frames;
	}
	out.pcm = buf;
	out.n = got;
	out.rate = rate;
	return 1;
}

static void TacBake(int id);

// YM2203 相当。FM は 4 オペレータ、SSG は矩形とノイズ。コーラスと残響は足さない。
// kind: 0=FM 1=矩形 2=ノイズ 3=矩形+ノイズ。sweep は音の長さで動く半音。
// @0 ベル @1 エレピ @2 笛 @3 打撃 @4 金管 @5 金属 @6 矩形 @7 ノイズ
// @8 木琴 @9 ハープ @10 炎 @11 氷 @12 雷 @13 聖 @14 闇 @15 地 @16 毒
// @17 斬 @18 突 @19 殴 @20 弓 @21 硬貨 @22 盾 @23 細矩形
static float FmSin(float cyc)
{
	float x = cyc - (float)(int)cyc;
	if (x < 0.f) x += 1.f;
	return sinf(x * 6.2831853f);
}
struct FmVoice {
	float mul[4];
	float lvl[4];
	int alg;
	float fb;
	float atk, dec, sus, rel;
	int kind;
	float duty;
	float sweep;
	float nz;
	int nper;
};
static const FmVoice kFmVoice[] = {
	{ { 14.f, 1.f, 1.01f, 1.f }, { 0.42f, 0.55f, 0.72f, 0.80f }, 3, 0.28f, 0.001f, 0.045f, 0.06f, 0.06f, 0, 0.50f, 0.f, 0.f, 0 },
	{ { 1.f, 1.f, 14.f, 1.f }, { 0.32f, 0.88f, 0.18f, 0.70f }, 1, 0.10f, 0.003f, 0.08f, 0.28f, 0.07f, 0, 0.50f, 0.f, 0.f, 0 },
	{ { 1.f, 2.f, 1.f, 1.f }, { 0.16f, 0.82f, 0.08f, 0.55f }, 0, 0.06f, 0.010f, 0.07f, 0.55f, 0.08f, 0, 0.50f, 0.f, 0.f, 0 },
	{ { 0.5f, 1.f, 1.f, 1.f }, { 0.72f, 0.60f, 0.18f, 0.90f }, 0, 0.36f, 0.001f, 0.045f, 0.06f, 0.05f, 0, 0.50f, -5.f, 0.28f, 8 },
	{ { 1.f, 1.f, 1.f, 1.007f }, { 0.48f, 0.82f, 0.28f, 0.86f }, 1, 0.24f, 0.006f, 0.07f, 0.42f, 0.08f, 0, 0.50f, 0.f, 0.f, 0 },
	{ { 7.f, 1.f, 11.f, 1.f }, { 0.55f, 0.42f, 0.50f, 0.62f }, 3, 0.18f, 0.001f, 0.016f, 0.02f, 0.028f, 0, 0.50f, 0.f, 0.10f, 2 },
	{ { 1.f, 1.f, 1.f, 1.f }, { 0.f, 0.f, 0.f, 0.f }, 0, 0.f, 0.001f, 0.02f, 0.85f, 0.03f, 1, 0.50f, 0.f, 0.f, 0 },
	{ { 1.f, 1.f, 1.f, 1.f }, { 0.f, 0.f, 0.f, 0.f }, 0, 0.f, 0.001f, 0.02f, 0.08f, 0.03f, 2, 0.50f, 0.f, 0.f, 16 },
	{ { 3.5f, 1.f, 1.f, 1.f }, { 0.46f, 0.92f, 0.08f, 0.16f }, 0, 0.08f, 0.001f, 0.07f, 0.04f, 0.05f, 0, 0.50f, 0.f, 0.f, 0 },
	{ { 1.f, 1.f, 2.f, 1.004f }, { 0.14f, 0.86f, 0.10f, 0.48f }, 1, 0.06f, 0.002f, 0.12f, 0.16f, 0.10f, 0, 0.50f, 0.f, 0.f, 0 },
	{ { 1.f, 1.f, 2.f, 3.f }, { 0.50f, 0.62f, 0.38f, 0.78f }, 3, 0.40f, 0.001f, 0.06f, 0.08f, 0.05f, 0, 0.50f, 4.f, 0.32f, 3 },
	{ { 1.f, 1.f, 7.f, 1.f }, { 0.18f, 0.72f, 0.32f, 0.50f }, 3, 0.10f, 0.004f, 0.16f, 0.22f, 0.12f, 0, 0.50f, 0.f, 0.f, 0 },
	{ { 1.f, 1.f, 1.f, 1.f }, { 0.f, 0.f, 0.f, 0.f }, 0, 0.f, 0.001f, 0.01f, 0.06f, 0.02f, 3, 0.28f, -8.f, 0.f, 2 },
	{ { 2.f, 1.f, 1.f, 1.005f }, { 0.28f, 0.80f, 0.14f, 0.68f }, 1, 0.14f, 0.005f, 0.10f, 0.32f, 0.10f, 0, 0.50f, 0.f, 0.f, 0 },
	{ { 1.5f, 1.f, 0.5f, 1.f }, { 0.48f, 0.74f, 0.28f, 0.55f }, 0, 0.28f, 0.003f, 0.10f, 0.22f, 0.08f, 0, 0.50f, -6.f, 0.12f, 12 },
	{ { 0.5f, 1.f, 1.f, 1.f }, { 0.78f, 0.90f, 0.14f, 0.36f }, 0, 0.22f, 0.001f, 0.08f, 0.08f, 0.06f, 0, 0.50f, -7.f, 0.42f, 14 },
	{ { 1.f, 1.04f, 2.f, 1.f }, { 0.32f, 0.58f, 0.36f, 0.66f }, 2, 0.16f, 0.005f, 0.08f, 0.32f, 0.08f, 0, 0.50f, -2.f, 0.06f, 18 },
	{ { 1.f, 1.f, 2.f, 1.f }, { 0.38f, 0.48f, 0.22f, 0.66f }, 1, 0.18f, 0.001f, 0.05f, 0.04f, 0.04f, 0, 0.50f, -12.f, 0.38f, 4 },
	{ { 3.f, 1.f, 1.f, 1.f }, { 0.42f, 0.82f, 0.08f, 0.24f }, 0, 0.10f, 0.001f, 0.028f, 0.02f, 0.03f, 0, 0.50f, 3.f, 0.18f, 3 },
	{ { 0.5f, 1.f, 1.f, 1.f }, { 0.82f, 0.95f, 0.16f, 0.28f }, 0, 0.32f, 0.001f, 0.04f, 0.05f, 0.04f, 0, 0.50f, -4.f, 0.34f, 6 },
	{ { 1.f, 1.f, 3.f, 1.f }, { 0.18f, 0.84f, 0.22f, 0.36f }, 1, 0.08f, 0.001f, 0.09f, 0.02f, 0.06f, 0, 0.50f, -2.f, 0.06f, 0 },
	{ { 10.f, 1.f, 14.f, 1.f }, { 0.36f, 0.52f, 0.42f, 0.64f }, 3, 0.14f, 0.001f, 0.028f, 0.02f, 0.045f, 0, 0.50f, 0.f, 0.f, 0 },
	{ { 1.f, 1.f, 6.f, 1.f }, { 0.32f, 0.74f, 0.38f, 0.78f }, 1, 0.16f, 0.001f, 0.05f, 0.14f, 0.08f, 0, 0.50f, 0.f, 0.14f, 2 },
	{ { 1.f, 1.f, 1.f, 1.f }, { 0.f, 0.f, 0.f, 0.f }, 0, 0.f, 0.001f, 0.02f, 0.25f, 0.02f, 1, 0.22f, 0.f, 0.f, 0 },
};
enum { TAC_FM_N = (int)(sizeof(kFmVoice) / sizeof(kFmVoice[0])) };
struct MmlEv { int t0; int n; float hz; int voice; float vol; int pan; int ssg; };

static float FmEnv(const FmVoice& v, float t, float dur)
{
	float rel = v.rel > 0.01f ? v.rel : 0.04f;
	if (t < 0.f) return 0.f;
	if (t < v.atk) return v.atk > 0.f ? t / v.atk : 1.f;
	if (t < v.atk + v.dec) {
		float u = (t - v.atk) / (v.dec > 0.f ? v.dec : 0.01f);
		return 1.f - (1.f - v.sus) * u;
	}
	if (t < dur) return v.sus;
	float u = (t - dur) / rel;
	if (u >= 1.f) return 0.f;
	return v.sus * (1.f - u);
}
static void FmNoise(unsigned& noise, int& clk, int per)
{
	if (per < 1) per = 1;
	if (++clk < per) return;
	clk = 0;
	unsigned bit = ((noise >> 0) ^ (noise >> 3)) & 1u;
	noise = (noise >> 1) | (bit << 16);
}
static void FmAdd(float* L, float* R, int total, const MmlEv& ev, unsigned& noise)
{
	int vi = ev.voice;
	if (vi < 0 || vi >= TAC_FM_N) vi = 0;
	const FmVoice& v = kFmVoice[vi];
	float rel = v.rel > 0.01f ? v.rel : 0.04f;
	int n = ev.n + (int)(rel * (float)TAC_RATE);
	if (ev.t0 >= total) return;
	if (ev.t0 + n > total) n = total - ev.t0;
	float g = ev.vol;
	float dur = (float)ev.n / (float)TAC_RATE;
	if (dur < 0.01f) dur = 0.01f;
	if (v.kind != 0) {
		float sq = 0.f;
		int nclk = 0;
		float duty = v.duty;
		if (duty < 0.08f) duty = 0.08f;
		if (duty > 0.5f) duty = 0.5f;
		for (int i = 0; i < n; i++) {
			float t = (float)i / (float)TAC_RATE;
			float u = t / dur;
			if (u > 1.f) u = 1.f;
			float hz = ev.hz * powf(2.f, (v.sweep * u) / 12.f);
			float env;
			if (v.sus >= 0.45f) {
				env = t < 0.002f ? t / 0.002f : 1.f;
			} else if (t < 0.002f) env = t / 0.002f;
			else {
				float tau = dur * 0.38f;
				if (tau < 0.010f) tau = 0.010f;
				env = expf(-(t - 0.002f) / tau);
			}
			if (t > dur + rel) env = 0.f;
			else if (t > dur) env *= 1.f - (t - dur) / rel;
			if (env <= 0.001f) continue;
			float s = 0.f;
			if (v.kind == 1 || v.kind == 3) {
				sq += hz / (float)TAC_RATE;
				if (sq >= 1.f) sq -= 1.f;
				s += (sq < duty ? 0.48f : -0.48f);
			}
			if (v.kind == 2 || v.kind == 3) {
				int per = v.nper > 0 ? v.nper : 8;
				FmNoise(noise, nclk, per);
				s += (noise & 1u) ? 0.32f : -0.32f;
			}
			s *= env;
			L[ev.t0 + i] += s * g;
			R[ev.t0 + i] += s * g;
		}
		return;
	}
	float ph[4] = { 0, 0, 0, 0 };
	float fb = 0.f;
	int nclk = 0;
	for (int i = 0; i < n; i++) {
		float t = (float)i / (float)TAC_RATE;
		float env = FmEnv(v, t, dur);
		if (env <= 0.f) continue;
		float u = t / dur;
		if (u > 1.f) u = 1.f;
		float hz = ev.hz * powf(2.f, (v.sweep * u) / 12.f);
		float o0 = FmSin(ph[0] + fb * v.fb) * v.lvl[0];
		float o1, o2, o3, a;
		if (v.alg == 2) {
			o1 = FmSin(ph[1]) * v.lvl[1];
			o2 = FmSin(ph[2]) * v.lvl[2];
			o3 = FmSin(ph[3]) * v.lvl[3];
			a = (o0 + o1 + o2 + o3) * 0.30f;
		} else if (v.alg == 3) {
			o1 = FmSin(ph[1] + o0) * v.lvl[1];
			o2 = FmSin(ph[2] + o0) * v.lvl[2];
			o3 = FmSin(ph[3] + o0 * 0.65f) * v.lvl[3];
			a = (o1 + o2 + o3) * 0.42f;
		} else if (v.alg == 1) {
			o1 = FmSin(ph[1] + o0) * v.lvl[1];
			o2 = FmSin(ph[2]) * v.lvl[2];
			o3 = FmSin(ph[3] + o2) * v.lvl[3];
			a = (o1 + o3) * 0.50f;
		} else {
			o1 = FmSin(ph[1] + o0) * v.lvl[1];
			o2 = FmSin(ph[2] + o1) * v.lvl[2];
			o3 = FmSin(ph[3] + o2) * v.lvl[3];
			a = o3 * 0.92f;
		}
		ph[0] += hz * v.mul[0] / (float)TAC_RATE;
		ph[1] += hz * v.mul[1] / (float)TAC_RATE;
		ph[2] += hz * v.mul[2] / (float)TAC_RATE;
		ph[3] += hz * v.mul[3] / (float)TAC_RATE;
		fb = o0;
		if (v.nz > 0.001f) {
			int per = v.nper > 0 ? v.nper : 4;
			FmNoise(noise, nclk, per);
			float nenv = expf(-t / 0.018f);
			a += ((noise & 1u) ? 0.36f : -0.36f) * v.nz * nenv;
		}
		float s = a * env * g;
		L[ev.t0 + i] += s;
		R[ev.t0 + i] += s;
	}
}
static int MmlSemi(char n)
{
	switch (n) {
	case 'c': return 0;
	case 'd': return 2;
	case 'e': return 4;
	case 'f': return 5;
	case 'g': return 7;
	case 'a': return 9;
	case 'b': return 11;
	default: return -1;
	}
}
static int MmlNum(const char* s, int& i, int fb)
{
	if (s[i] < '0' || s[i] > '9') return fb;
	int v = 0;
	while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); i++; }
	return v > 0 ? v : fb;
}
static int MmlSamples(int tempo, int len, int dotted)
{
	if (len < 1) len = 8;
	if (tempo < 40) tempo = 40;
	double sec = (4.0 / (double)len) * (60.0 / (double)tempo);
	if (dotted) sec *= 1.5;
	int n = (int)(sec * (double)TAC_RATE);
	return n < 1 ? 1 : n;
}
static int MmlTrack(const char* s, int track, MmlEv* ev, int nEv, int cap)
{
	(void)track;
	int tempo = 150, len = 16, oct = 4, voice = 1;
	float vol = 0.55f;
	int cursor = 0;
	int chord = 0, chordAt = 0, chordEnd = 0;
	int pan = 0;
	for (int i = 0; s[i];) {
		char c = s[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '&') { i++; continue; }
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		if (c == '[') { chord = 1; chordAt = cursor; chordEnd = cursor; i++; continue; }
		if (c == ']') {
			i++;
			int had = (s[i] >= '0' && s[i] <= '9');
			int ln = MmlNum(s, i, len);
			int dot = 0;
			if (s[i] == '.') { dot = 1; i++; }
			if (had) {
				int dur = MmlSamples(tempo, ln, dot);
				for (int k = nEv; k-- > 0;) {
					if (ev[k].t0 != chordAt) break;
					ev[k].n = dur;
				}
				cursor = chordAt + dur;
			} else cursor = chordEnd;
			chord = 0;
			continue;
		}
		if (c == 't') { i++; tempo = MmlNum(s, i, tempo); continue; }
		if (c == 'l') { i++; len = MmlNum(s, i, len); continue; }
		if (c == 'o') { i++; oct = MmlNum(s, i, oct); if (oct < 1) oct = 1; if (oct > 7) oct = 7; continue; }
		if (c == 'v') { i++; vol = (float)MmlNum(s, i, 12) / 15.f; if (vol > 1.f) vol = 1.f; continue; }
		if (c == '@') { i++; voice = MmlNum(s, i, voice); continue; }
		if (c == '>') { if (oct < 7) oct++; i++; continue; }
		if (c == '<') { if (oct > 1) oct--; i++; continue; }
		if (c == 'r' || c == 'p') {
			i++;
			int ln = MmlNum(s, i, len);
			int dot = 0;
			if (s[i] == '.') { dot = 1; i++; }
			int dur = MmlSamples(tempo, ln, dot);
			if (!chord) cursor += dur;
			continue;
		}
		int semi = MmlSemi(c);
		if (semi < 0) { i++; continue; }
		i++;
		if (s[i] == '+' || s[i] == '#') { semi++; i++; }
		else if (s[i] == '-') { semi--; i++; }
		int ln = MmlNum(s, i, len);
		int dot = 0;
		if (s[i] == '.') { dot = 1; i++; }
		int dur = MmlSamples(tempo, ln, dot);
		if (nEv < cap) {
			MmlEv& e = ev[nEv++];
			e.t0 = chord ? chordAt : cursor;
			e.n = dur;
			int midi = (oct + 1) * 12 + semi;
			e.hz = 440.f * powf(2.f, (midi - 69) / 12.f);
			if (voice < 0) voice = 0;
			if (voice >= TAC_FM_N) voice = TAC_FM_N - 1;
			e.voice = voice;
			e.vol = vol;
			e.pan = pan;
			e.ssg = kFmVoice[voice].kind;
		}
		if (chord) {
			int end = chordAt + dur;
			if (end > chordEnd) chordEnd = end;
		} else cursor += dur;
	}
	return nEv;
}
static short* MmlMix(const char* src, int& frames, float gain)
{
	MmlEv ev[48];
	int nEv = 0;
	int track = 0;
	const char* p = src ? src : "";
	while (*p && track < 3) {
		const char* bar = strchr(p, '|');
		int len = bar ? (int)(bar - p) : (int)strlen(p);
		char buf[256];
		if (len > 254) len = 254;
		memcpy(buf, p, (size_t)len);
		buf[len] = 0;
		nEv = MmlTrack(buf, track, ev, nEv, 48);
		if (!bar) break;
		p = bar + 1;
		track++;
	}
	int total = (int)(0.04f * TAC_RATE);
	for (int i = 0; i < nEv; i++) {
		int end = ev[i].t0 + ev[i].n + (int)(0.08f * TAC_RATE);
		if (end > total) total = end;
	}
	int cap = TAC_RATE * 4;
	if (total > cap) total = cap;
	if (total < 8) total = 8;
	float* L = (float*)calloc((size_t)total, sizeof(float));
	float* R = (float*)calloc((size_t)total, sizeof(float));
	short* dst = (short*)malloc((size_t)total * 2 * sizeof(short));
	if (!L || !R || !dst) { free(L); free(R); free(dst); frames = 0; return NULL; }
	unsigned noise = 0x1FFFFu;
	for (int i = 0; i < nEv; i++) FmAdd(L, R, total, ev[i], noise);
	float pk = 0.05f;
	for (int i = 0; i < total; i++) {
		float a = fabsf(L[i]);
		float b = fabsf(R[i]);
		if (a > pk) pk = a;
		if (b > pk) pk = b;
	}
	if (gain < 0.04f) gain = 0.04f;
	if (gain > 0.85f) gain = 0.85f;
	float sc = (gain * 17000.f) / pk;
	for (int i = 0; i < total; i++) {
		float l = L[i] * sc;
		float r = R[i] * sc;
		if (l > 32767.f) l = 32767.f;
		if (l < -32768.f) l = -32768.f;
		if (r > 32767.f) r = 32767.f;
		if (r < -32768.f) r = -32768.f;
		dst[i * 2] = (short)l;
		dst[i * 2 + 1] = (short)r;
	}
	free(L);
	free(R);
	frames = total;
	return dst;
}
static const char* const kMml[TS_N] = {
	"t220 l64 o6 v8 @5 e",
	"t168 l16 o5 v12 @0 c e g",
	"t150 l16 o5 v10 @1 g e c",
	"t100 l8 o2 v12 @6 c <g",
	"t150 l16 o4 v11 @4 c e g",
	"t140 l8 o3 v12 @4 c g >e",
	"t120 l8 o3 v11 @14 e- c <a-",
	"t200 l32 o2 v10 @3 c | t200 l32 o1 v7 @7 c",
	"t180 l16 o4 v12 @17 c | t180 l32 o3 v8 @7 c",
	"t160 l16 o5 v11 @20 e c | t200 l64 o6 v6 @5 e",
	"t150 l16 o5 v10 @13 c e g",
	"t140 l16 o2 v12 @3 c | t160 l32 o3 v7 @7 c",
	"t150 l16 o2 v13 @3 c | t190 l32 o6 v9 @0 c e g",
	"t200 l16 o5 v6 @7 c e",
	"t140 l16 o5 v10 @9 c e g >c",
	"t150 l16 o5 v9 @2 e g b",
	"t80 l8 o3 v12 @14 a f c <g",
	"t128 l8 o4 v12 @4 c e g >c4 <g8 >c2 | t128 l8 o5 v9 @0 e g b >e4 c8 e2",
	"t90 l8 o3 v11 @14 e- c <a- g-2",
	"t150 l16 o5 v11 @0 c e g >c",
	"t160 l16 o5 v10 @0 c e g",
	"t200 l32 o6 v12 @21 e a",
	"t170 l16 o4 v11 @4 c g",
	"t150 l16 o5 v9 @9 c e g",
	"t200 l32 o4 v12 @5 c | t200 l64 o3 v8 @22 g",
	"t170 l16 o6 v10 @21 a e c",
	"t160 l16 o5 v10 @13 c e g | t200 l32 o6 v7 @0 g",
	"t180 l32 o5 v7 @23 e",
	"t160 l16 o4 v8 @1 c e",
	"t150 l16 o5 v11 @8 c e g >c | t190 l32 o6 v8 @21 e",
	"t90 l8 o2 v9 @7 c | t180 l32 o4 v11 @5 r16 c",
	"t130 l16 o2 v12 @19 c | t140 l16 o3 v8 @14 e- c",
	"t150 l16 o4 v12 @10 c e g | t160 l32 o3 v7 @7 c",
	"t140 l16 o6 v10 @11 e g >c",
	"t200 l32 o3 v12 @12 c g c",
	"t140 l16 o5 v11 @13 [ceg] e >c",
	"t110 l8 o3 v11 @14 e- c <g",
	"t170 l16 o5 v9 @2 c e g >e | t180 l16 o5 v6 @7 c",
	"t100 l8 o2 v12 @15 c g",
	"t120 l16 o4 v9 @16 e- f e-",
	"t140 l16 o5 v10 @9 e g >c",
	"t150 l16 o5 v9 @2 g b >d",
	"t130 l8 o4 v11 @13 c e g >c",
	"t120 l8 o3 v12 @17 c | t140 l16 o2 v8 @7 c",
	"t200 l32 o4 v12 @18 e | t200 l64 o5 v6 @5 e",
	"t150 l16 o2 v12 @19 c",
	"t160 l16 o4 v11 @17 c e c | t160 l16 o3 v7 @7 c e",
	"t180 l32 o2 v12 @19 c c | t200 l32 o3 v8 @7 c",
	"t160 l16 o5 v10 @13 e g >c | t180 l16 o4 v7 @17 c",
	"t150 l16 o3 v12 @10 c | t160 l32 o2 v8 @7 c",
	"t170 l32 o6 v11 @11 e g",
	"t220 l64 o3 v13 @12 c e c",
	"t160 l16 o5 v11 @13 [ceg]",
	"t100 l8 o2 v12 @14 e- c",
	"t180 l16 o5 v9 @2 e c | t180 l16 o5 v6 @7 c",
	"t110 l8 o2 v13 @15 c",
	"t130 l16 o4 v9 @16 e- g- e-",
	"t160 l16 o4 v11 @20 c | t180 l32 o3 v8 @3 c",
	"t190 l32 o4 v12 @18 e",
	"t140 l16 o2 v13 @19 c",
	"t150 l16 o4 v11 @13 c e | t150 l16 o3 v8 @10 c",
	"t120 l8 o4 v12 @4 c e g >c | t140 l16 o5 v9 @13 e g >c",
	"t160 l16 o5 v7 @16 e- c",
	"t200 l32 o5 v6 @7 c e",
	"t130 l8 o4 v11 @9 c e g >c | t160 l16 o6 v8 @0 e g",
	"t170 l16 o6 v9 @11 c e g",
	"t110 l16 o4 v8 @16 e- f e- f",
	"t80 l8 o5 v8 @9 e c <g",
	"t140 l16 o3 v12 @22 c | t160 l32 o2 v8 @3 c",
	"t150 l16 o4 v12 @22 c g e",
	"t160 l16 o4 v10 @2 c e g >c >e",
	"t160 l16 o5 v11 @0 c e g >e",
	"t120 l8 o4 v12 @4 c e g >c4 | t140 l16 o5 v8 @0 e g >c",
	"t170 l16 o6 v11 @21 e g | t160 l16 o5 v8 @8 c e",
	"t130 l8 o3 v12 @4 c g >e | t150 l16 o5 v8 @0 c e g",
	"t140 l8 o3 v12 @4 c e g",
	"t150 l16 o4 v9 @16 c f+ c",
	"t70 l8 o3 v9 @1 g e c",
	"t120 l16 o3 v11 @22 c <g | t140 l32 o2 v7 @7 c",
};
static void TacSynthAll()
{
	for (int id = 0; id < TS_N; id++) {
		int frames = 0;
		short* pcm = MmlMix(kMml[id], frames, kGain[id]);
		if (!pcm) continue;
		TacClip& c = s_clip[id];
		free(c.pcm);
		c.pcm = NULL;
		free(c.play);
		c.play = pcm;
		c.playN = frames;
		c.n = frames;
		c.rate = TAC_RATE;
	}
}
static int CastId(int element)
{
	switch (element) {
	case otac::EL_FIRE: return TS_CAST_FIRE;
	case otac::EL_ICE: return TS_CAST_ICE;
	case otac::EL_THUNDER: return TS_CAST_THUNDER;
	case otac::EL_HOLY: return TS_CAST_HOLY;
	case otac::EL_DARK: return TS_CAST_DARK;
	case otac::EL_WIND: return TS_CAST_WIND;
	case otac::EL_EARTH: return TS_CAST_EARTH;
	case otac::EL_POISON: return TS_CAST_POISON;
	case otac::EL_RANGE: return TS_BOW;
	case otac::EL_MELEE: case otac::EL_PHYS: return TS_SWING;
	default: return TS_CAST;
	}
}
static int HitId(int element, int style)
{
	switch (element) {
	case otac::EL_FIRE: return TS_HIT_FIRE;
	case otac::EL_ICE: return TS_HIT_ICE;
	case otac::EL_THUNDER: return TS_HIT_THUNDER;
	case otac::EL_HOLY: return TS_HIT_HOLY;
	case otac::EL_DARK: return TS_HIT_DARK;
	case otac::EL_WIND: return TS_HIT_WIND;
	case otac::EL_EARTH: return TS_HIT_EARTH;
	case otac::EL_POISON: return TS_HIT_POISON;
	default: break;
	}
	if (style == otac::AS_SHOOT || style == otac::AS_SHOOT1) return TS_HIT_RANGE;
	if (style == otac::AS_STAB) return TS_HIT_STAB;
	if (style == otac::AS_PUNCH) return TS_HIT_PUNCH;
	return TS_HIT;
}


static void TacLoadPack()
{
	memset(s_clip, 0, sizeof(s_clip));
	HMODULE mod = GetModuleHandle(NULL);
	HRSRC hr = FindResource(mod, MAKEINTRESOURCE(IDR_TAC_SFX), RT_RCDATA);
	if (!hr) return;
	HGLOBAL hg = LoadResource(mod, hr);
	if (!hg) return;
	const unsigned char* p = (const unsigned char*)LockResource(hg);
	DWORD sz = SizeofResource(mod, hr);
	if (!p || sz < 16) return;
	if (memcmp(p, "TSFX", 4) != 0) return;
	unsigned n = *(const unsigned*)(p + 4);
	if (n > 64 || 8 + n * 8 > sz) return;
	unsigned use = n < (unsigned)TS_N ? n : (unsigned)TS_N;
	for (unsigned i = 0; i < use; i++) {
		unsigned off = *(const unsigned*)(p + 8 + i * 8);
		unsigned len = *(const unsigned*)(p + 12 + i * 8);
		if (off >= sz || len > sz - off) continue;
		TacDecodeMem(p + off, len, s_clip[i]);
		TacBake((int)i);
	}
}

static void TacBake(int id)
{
	TacClip& c = s_clip[id];
	if (!c.pcm || c.n < 1 || id < 0 || id >= TS_N) return;
	int frames = (int)((long long)c.n * TAC_RATE / (c.rate > 0 ? c.rate : TAC_RATE));
	if (frames < 1) frames = 1;
	short* dst = (short*)malloc((size_t)frames * 2 * sizeof(short));
	if (!dst) return;
	float g = kGain[id];
	for (int f = 0; f < frames; f++) {
		float pos = (float)f * (float)c.rate / (float)TAC_RATE;
		int i0 = (int)pos;
		if (i0 < 0) i0 = 0;
		if (i0 >= c.n) i0 = c.n - 1;
		int i1 = i0 + 1 < c.n ? i0 + 1 : i0;
		float fr = pos - (float)(int)pos;
		if (fr < 0) fr = 0;
		float s = ((float)c.pcm[i0] + ((float)c.pcm[i1] - (float)c.pcm[i0]) * fr) * g;
		if (s > 32767.f) s = 32767.f;
		if (s < -32768.f) s = -32768.f;
		dst[f * 2] = dst[f * 2 + 1] = (short)s;
	}
	c.play = dst;
	c.playN = frames;
	free(c.pcm);
	c.pcm = NULL;
}

static size_t BgmRead(void* dst, size_t sz, size_t nm, void* h)
{
	return fread(dst, sz, nm, (FILE*)h);
}
static int BgmSeek(void* h, ogg_int64_t off, int whence)
{
	return _fseeki64((FILE*)h, off, whence);
}
static int BgmClose(void* h)
{
	if (h) fclose((FILE*)h);
	return 0;
}
static long BgmTell(void* h)
{
	return ftell((FILE*)h);
}

static void TacReadLoop(OggVorbis_File& vf)
{
	s_loopA = 0;
	s_loopB = ov_pcm_total(&vf, -1);
	if (s_loopB < 1) s_loopB = 1;
	vorbis_comment* vc = ov_comment(&vf, -1);
	if (!vc || !vc->user_comments) return;
	ogg_int64_t a = 0, len = 0, end = 0;
	int hasA = 0, hasL = 0, hasE = 0;
	for (int i = 0; i < vc->comments; i++) {
		const char* c = vc->user_comments[i];
		if (!c) continue;
		if (_strnicmp(c, "LOOPSTART=", 10) == 0) { a = _atoi64(c + 10); hasA = 1; }
		else if (_strnicmp(c, "LOOPLENGTH=", 11) == 0) { len = _atoi64(c + 11); hasL = 1; }
		else if (_strnicmp(c, "LOOPEND=", 8) == 0) { end = _atoi64(c + 8); hasE = 1; }
	}
	if (hasA) s_loopA = a;
	if (hasE && end > s_loopA) s_loopB = end;
	else if (hasL && len > 0) s_loopB = s_loopA + len;
	if (s_loopA < 0) s_loopA = 0;
	if (s_loopB <= s_loopA) s_loopB = ov_pcm_total(&vf, -1);
}

static int TacOggOk(const wchar_t* path)
{
	if (!path || !path[0]) return 0;
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a)) return 0;
	if (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
	ULONGLONG sz = ((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
	if (sz < 256) return 0;
	FILE* fp = _wfopen(path, L"rb");
	if (!fp) return 0;
	unsigned char m[4] = {};
	size_t n = fread(m, 1, 4, fp);
	fclose(fp);
	return n == 4 && m[0] == 'O' && m[1] == 'g' && m[2] == 'g' && m[3] == 'S';
}

// exe\tacticsbgm を作り、旧 exe 直下の tactics.ogg があればここへ移す。
static int TacBgmDir(wchar_t* dir, int cch)
{
	wchar_t exe[MAX_PATH];
	if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return 0;
	wchar_t* sl = wcsrchr(exe, L'\\');
	if (!sl) return 0;
	*sl = 0;
	if (swprintf_s(dir, cch, L"%s\\tacticsbgm", exe) < 0) return 0;
	CreateDirectoryW(dir, NULL);
	wchar_t oldp[MAX_PATH], neu[MAX_PATH];
	swprintf_s(oldp, L"%s\\tactics.ogg", exe);
	swprintf_s(neu, L"%s\\tactics.ogg", dir);
	if (!TacOggOk(neu) && TacOggOk(oldp)) {
		DeleteFileW(neu);
		if (!MoveFileW(oldp, neu)) {
			if (CopyFileW(oldp, neu, FALSE))
				DeleteFileW(oldp);
		}
	}
	return 1;
}

static const wchar_t* TacBgmFile(int scene)
{
	switch (scene) {
	case TAC_BGM_GAME: return L"tacticsgame.ogg";
	case TAC_BGM_WIN: return L"tacticsgamewin.ogg";
	case TAC_BGM_SHOP: return L"tacticsshop.ogg";
	case TAC_BGM_MAKE: return L"tacticsmake.ogg";
	case TAC_BGM_PLAINS: return L"tacticsgameplains.ogg";
	case TAC_BGM_CANYON: return L"tacticsgamecanyon.ogg";
	case TAC_BGM_FOREST: return L"tacticsgameforest.ogg";
	case TAC_BGM_SNOW: return L"tacticsgamesnow.ogg";
	case TAC_BGM_DESERT: return L"tacticsgamedesert.ogg";
	case TAC_BGM_VOLCANO: return L"tacticsgamevolcano.ogg";
	case TAC_BGM_SWAMP: return L"tacticsgameswamp.ogg";
	case TAC_BGM_RUINS: return L"tacticsgameruins.ogg";
	case TAC_BGM_DANGER: return L"tacticsgamedanger.ogg";
	case TAC_BGM_CLEAR: return L"tacticsgameclear.ogg";
	default: return L"tactics.ogg";
	}
}

int TacBgmHas(int scene)
{
	if (scene < 0 || scene >= TAC_BGM_N) return 0;
	wchar_t dir[MAX_PATH];
	if (!TacBgmDir(dir, MAX_PATH)) return 0;
	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\%s", dir, TacBgmFile(scene));
	return TacOggOk(path);
}

static int TacOpenBgmPath(const wchar_t* path)
{
	s_bgm = 0;
	s_bhave = s_bpos = 0;
	FILE* fp = _wfopen(path, L"rb");
	if (!fp) return 0;
	ov_callbacks cb;
	cb.read_func = BgmRead;
	cb.seek_func = BgmSeek;
	cb.close_func = BgmClose;
	cb.tell_func = BgmTell;
	if (ov_open_callbacks(fp, &s_vf, NULL, 0, cb) < 0) {
		fclose(fp);
		return 0;
	}
	vorbis_info* vi = ov_info(&s_vf, -1);
	s_bgmCh = vi && vi->channels > 0 ? vi->channels : 2;
	s_bgmRate = vi && vi->rate >= 8000 ? (int)vi->rate : TAC_RATE;
	s_bPrimed = 0;
	s_bFrac = 0;
	TacReadLoop(s_vf);
	s_bgm = 1;
	return 1;
}

static void TacCloseBgm();
static void TacPauseMain(int pause);

// 再生位置の直後だけ残し、先読みしてある旧曲を消す。効果音バッファは触らない。
static void TacDropQueuedBgm()
{
	if (!s_buf || !s_bytes || !s_started) return;
	DWORD play = 0, wrt = 0;
	if (FAILED(s_buf->GetCurrentPosition(&play, &wrt))) return;
	DWORD lead = (DWORD)(TAC_RATE * 4 / 30);
	lead &= ~3u;
	if (lead < 64 || lead >= s_bytes) return;
	void* p1 = NULL; void* p2 = NULL; DWORD n1 = 0, n2 = 0;
	if (SUCCEEDED(s_buf->Lock(play, lead, &p1, &n1, &p2, &n2, 0))) {
		int sample = 0;
		const int total = (int)(lead / 2);
		short* parts[2] = { (short*)p1, (short*)p2 };
		DWORD ns[2] = { n1, n2 };
		for (int part = 0; part < 2; part++) {
			if (!parts[part] || ns[part] < 2) continue;
			int count = (int)(ns[part] / 2);
			for (int i = 0; i < count; i++) {
				float g = total > 1 ? (float)(total - sample) / (float)total : 0.f;
				if (g < 0.f) g = 0.f;
				parts[part][i] = (short)((float)parts[part][i] * g);
				sample++;
			}
		}
		s_buf->Unlock(p1, n1, p2, n2);
	}
	DWORD start = (play + lead) % s_bytes;
	DWORD silence = (s_bytes - lead) & ~3u;
	p1 = p2 = NULL; n1 = n2 = 0;
	if (silence >= 4 && SUCCEEDED(s_buf->Lock(start, silence, &p1, &n1, &p2, &n2, 0))) {
		if (p1 && n1) memset(p1, 0, n1);
		if (p2 && n2) memset(p2, 0, n2);
		s_buf->Unlock(p1, n1, p2, n2);
	}
	s_write = start;
	s_bgmFade = TAC_RATE / 40;
}

void TacBgmScene(int scene)
{
	int pauseMain = 0;
	{
		TacLock lk;
		if (scene < 0 || scene >= TAC_BGM_N) scene = TAC_BGM_HUB;
		if (scene == s_bgmScene) return;
		wchar_t dir[MAX_PATH];
		if (!TacBgmDir(dir, MAX_PATH)) return;
		wchar_t want[MAX_PATH], game[MAX_PATH], hub[MAX_PATH];
		swprintf_s(want, L"%s\\%s", dir, TacBgmFile(scene));
		swprintf_s(game, L"%s\\tacticsgame.ogg", dir);
		swprintf_s(hub, L"%s\\tactics.ogg", dir);
		const wchar_t* use = want;
		if (!TacOggOk(want) && scene != TAC_BGM_HUB) {
			const int theme = scene >= TAC_BGM_PLAINS && scene <= TAC_BGM_RUINS;
			if (theme && TacOggOk(game)) use = game;
			else use = hub;
		}
		if (!TacOggOk(use)) {
			if (s_bgm) TacCloseBgm();
			s_bgmPath[0] = 0;
			s_bgmScene = scene;
			return;
		}
		if (s_bgm && _wcsicmp(s_bgmPath, use) == 0) {
			s_bgmScene = scene;
			return;
		}
		int had = s_bgm;
		TacCloseBgm();
		if (!TacOpenBgmPath(use)) {
			s_bgmPath[0] = 0;
			s_bgmScene = scene;
			return;
		}
		wcscpy_s(s_bgmPath, use);
		s_bgmScene = scene;
		s_bgmOut = 0;
		s_bgmOutTotal = 0;
		if (had) TacDropQueuedBgm();
		if (!had && s_started) pauseMain = 1;
	}
	if (pauseMain) TacPauseMain(1);
}

static void TacCloseBgm()
{
	if (s_bgm) ov_clear(&s_vf);
	s_bgm = 0;
	s_bhave = s_bpos = 0;
	s_bPrimed = 0;
	s_bgmPath[0] = 0;
	s_bgmScene = -1;
}

static void TacPauseMain(int pause)
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	if (pause) {
		if (plf && !ps) {
			og->OnPause();
			if (ps) s_pausedMain = 1;
		}
	} else if (s_pausedMain) {
		s_pausedMain = 0;
		if (ps) og->OnPause();
	}
}

static int TacEnsureBuf(HWND hwnd)
{
	if (s_buf) return 1;
	if (!s_ds) {
		if (FAILED(DirectSoundCreate8(NULL, &s_ds, NULL)) || !s_ds) return 0;
		HWND coop = hwnd ? hwnd : GetDesktopWindow();
		s_ds->SetCooperativeLevel(coop, DSSCL_NORMAL);
	}
	WAVEFORMATEX wfx = {};
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = TAC_RATE;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = 4;
	wfx.nAvgBytesPerSec = TAC_RATE * 4;
	DSBUFFERDESC d = {};
	d.dwSize = sizeof(d);
	d.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCSOFTWARE;
	// ステレオ 16bit で 0.5 秒。先読みは約 0.3 秒に保ち、曲切替の遅れを増やさない。
	d.dwBufferBytes = TAC_RATE * 2;
	d.dwBufferBytes &= ~3u;
	d.lpwfxFormat = &wfx;
	if (FAILED(s_ds->CreateSoundBuffer(&d, &s_buf, NULL)) || !s_buf) return 0;
	s_bytes = d.dwBufferBytes;
	void* p1 = NULL; void* p2 = NULL; DWORD n1 = 0, n2 = 0;
	if (SUCCEEDED(s_buf->Lock(0, s_bytes, &p1, &n1, &p2, &n2, 0))) {
		if (p1 && n1) memset(p1, 0, n1);
		if (p2 && n2) memset(p2, 0, n2);
		s_buf->Unlock(p1, n1, p2, n2);
	}
	s_buf->SetVolume(-400);
	s_write = 0;
	s_started = 0;
	return 1;
}

static int TacPullBgm(short& L, short& R)
{
	if (!s_bgm) return 0;
	const int ch = s_bgmCh < 1 ? 1 : s_bgmCh;
	if (s_bpos + ch > s_bhave) {
		ogg_int64_t pos = ov_pcm_tell(&s_vf);
		if (pos >= s_loopB || pos < 0)
			ov_pcm_seek(&s_vf, s_loopA);
		int bs = 0;
		long r = ov_read(&s_vf, (char*)s_bpcm, (int)sizeof(s_bpcm), 0, 2, 1, &bs);
		if (r <= 0) {
			ov_pcm_seek(&s_vf, s_loopA);
			r = ov_read(&s_vf, (char*)s_bpcm, (int)sizeof(s_bpcm), 0, 2, 1, &bs);
			if (r <= 0) return 0;
		}
		s_bhave = (int)(r / (int)sizeof(short));
		s_bpos = 0;
		ogg_int64_t now = ov_pcm_tell(&s_vf);
		if (now > s_loopB) {
			int over = (int)(now - s_loopB);
			int frames = s_bhave / ch;
			if (over > 0 && over < frames)
				s_bhave = (frames - over) * ch;
		}
	}
	if (s_bpos + ch > s_bhave) return 0;
	if (ch == 1) {
		L = R = s_bpcm[s_bpos++];
	} else {
		L = s_bpcm[s_bpos];
		R = s_bpcm[s_bpos + 1];
		s_bpos += ch;
	}
	return 1;
}

static int TacPullBgmOut(short& L, short& R)
{
	float step = (float)s_bgmRate / (float)TAC_RATE * s_tempo;
	if (step < 0.05f) step = 1.f;
	if (!s_bPrimed) {
		if (!TacPullBgm(s_bCurL, s_bCurR)) return 0;
		if (!TacPullBgm(s_bNextL, s_bNextR)) { s_bNextL = s_bCurL; s_bNextR = s_bCurR; }
		s_bFrac = 0;
		s_bPrimed = 1;
	}
	float a = s_bFrac;
	if (a < 0) a = 0;
	if (a > 1.f) a = 1.f;
	L = (short)((float)s_bCurL + ((float)s_bNextL - (float)s_bCurL) * a);
	R = (short)((float)s_bCurR + ((float)s_bNextR - (float)s_bCurR) * a);
	s_bFrac += step;
	while (s_bFrac >= 1.f) {
		s_bFrac -= 1.f;
		s_bCurL = s_bNextL;
		s_bCurR = s_bNextR;
		if (!TacPullBgm(s_bNextL, s_bNextR)) { s_bNextL = s_bCurL; s_bNextR = s_bCurR; }
	}
	return 1;
}

void TacBgmTempo(float goal)
{
	TacLock lk;
	if (goal < 1.f) goal = 1.f;
	if (goal > 2.f) goal = 2.f;
	s_tempoGoal = goal;
}

void TacBgmFadeOut()
{
	TacLock lk;
	if (!s_bgm || s_bgmOutTotal > 0) return;
	s_bgmOutTotal = TAC_RATE * 2;
	s_bgmOut = s_bgmOutTotal;
}

static void TacMix(short* dst, int frames)
{
	for (int i = 0; i < frames * 2; i++) dst[i] = 0;
	if (frames > 0 && s_tempo != s_tempoGoal) {
		float dt = (float)frames / (float)TAC_RATE;
		float k = 1.f - expf(-dt / 1.35f);
		s_tempo += (s_tempoGoal - s_tempo) * k;
	}
	if (s_bgm) {
		const float g0 = 0.07f * (float)s_bgmPct;
		const int fadeN = TAC_RATE / 40;
		short holdL = 0, holdR = 0;
		for (int f = 0; f < frames; f++) {
			short L = holdL, R = holdR;
			if (TacPullBgmOut(L, R)) { holdL = L; holdR = R; }
			float g = g0;
			if (s_bgmFade > 0 && fadeN > 0) {
				float in = 1.f - (float)s_bgmFade / (float)fadeN;
				if (in < 0.f) in = 0.f;
				g *= in;
				s_bgmFade--;
			}
			if (s_bgmOutTotal > 0) {
				if (s_bgmOut > 0) {
					g *= (float)s_bgmOut / (float)s_bgmOutTotal;
					s_bgmOut--;
				} else g = 0.f;
			}
			int l = (int)dst[f * 2] + (int)(L * g);
			int r = (int)dst[f * 2 + 1] + (int)(R * g);
			if (l > 32767) l = 32767; if (l < -32768) l = -32768;
			if (r > 32767) r = 32767; if (r < -32768) r = -32768;
			dst[f * 2] = (short)l;
			dst[f * 2 + 1] = (short)r;
		}
	}
}

static DWORD WINAPI TacAudioThread(LPVOID)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	while (InterlockedCompareExchange(&s_run, 0, 0)) {
		{
			TacLock lk;
			TacAudioPump();
		}
		Sleep(4);
	}
	return 0;
}

int TacAudioOnThread()
{
	return s_th != NULL && s_run != 0;
}

void TacAudioStart(void* hwnd)
{
	TacCsInit();
	TacLoadPack();
	TacSynthAll();
	TacBgmScene(TAC_BGM_HUB);
	int bgm = s_bgm;
	if (!TacEnsureBuf((HWND)hwnd)) {
		if (bgm) {
			TacLock lk;
			TacCloseBgm();
		}
		return;
	}
	if (!s_th) {
		InterlockedExchange(&s_run, 1);
		s_th = CreateThread(NULL, 0, TacAudioThread, NULL, 0, NULL);
		if (!s_th) InterlockedExchange(&s_run, 0);
	}
	if (bgm) TacPauseMain(1);
}

void TacAudioStop()
{
	InterlockedExchange(&s_run, 0);
	if (s_th) {
		// ポンプがロックを持ったまま戻らないときに、ここで同じロックを取ると
		// UI が死ぬ。窓は先に隠れているので、プロセスだけが exe を掴んで残る。
		if (WaitForSingleObject(s_th, 2000) != WAIT_OBJECT_0) {
			TacPauseMain(0);
			return;
		}
		CloseHandle(s_th);
		s_th = NULL;
	}
	TacPauseMain(0);
	TacLock lk;
	for (int i = 0; i < TAC_VOICES; i++) {
		if (s_voice[i].buf) { s_voice[i].buf->Stop(); s_voice[i].buf->Release(); s_voice[i].buf = NULL; }
	}
	if (s_buf) { s_buf->Stop(); s_buf->Release(); s_buf = NULL; }
	if (s_ds) { s_ds->Release(); s_ds = NULL; }
	s_started = 0;
	TacCloseBgm();
	for (int i = 0; i < TS_N; i++) {
		free(s_clip[i].pcm); s_clip[i].pcm = NULL;
		free(s_clip[i].play); s_clip[i].play = NULL;
		s_clip[i].n = s_clip[i].playN = 0;
	}
	memset(s_voice, 0, sizeof(s_voice));
}

static int TacSfxBuf(TacVoice& v, int frames)
{
	DWORD bytes = (DWORD)frames * 4;
	if (bytes < 4) bytes = 4;
	if (v.buf && v.frames == frames) return 1;
	if (v.buf) { v.buf->Stop(); v.buf->Release(); v.buf = NULL; }
	WAVEFORMATEX wfx = {};
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = TAC_RATE;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = 4;
	wfx.nAvgBytesPerSec = TAC_RATE * 4;
	DSBUFFERDESC d = {};
	d.dwSize = sizeof(d);
	d.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS | DSBCAPS_LOCSOFTWARE;
	d.dwBufferBytes = bytes;
	d.lpwfxFormat = &wfx;
	if (FAILED(s_ds->CreateSoundBuffer(&d, &v.buf, NULL)) || !v.buf) return 0;
	v.buf->SetVolume(SeDb(s_sePct));
	v.frames = frames;
	return 1;
}

static void TacSfxPlay(const short* pcm, int frames)
{
	if (!pcm || frames < 1 || !s_ds) return;
	int slot = -1;
	int steal = 0;
	DWORD stealAt = 0;
	for (int i = 0; i < TAC_VOICES; i++) {
		if (!s_voice[i].buf) { slot = i; break; }
		DWORD st = 0, pos = 0;
		s_voice[i].buf->GetStatus(&st);
		if (!(st & DSBSTATUS_PLAYING)) { slot = i; break; }
		s_voice[i].buf->GetCurrentPosition(&pos, NULL);
		if (slot < 0 || pos >= stealAt) { steal = i; stealAt = pos; }
	}
	if (slot < 0) slot = steal;
	TacVoice& v = s_voice[slot];
	if (!TacSfxBuf(v, frames)) return;
	v.buf->Stop();
	void* p1 = NULL; void* p2 = NULL; DWORD n1 = 0, n2 = 0;
	DWORD bytes = (DWORD)frames * 4;
	if (FAILED(v.buf->Lock(0, bytes, &p1, &n1, &p2, &n2, 0))) return;
	if (p1 && n1) memcpy(p1, pcm, n1);
	if (p2 && n2) memcpy(p2, (char*)pcm + n1, n2);
	v.buf->Unlock(p1, n1, p2, n2);
	v.buf->SetCurrentPosition(0);
	v.buf->Play(0, 0, 0);
}

void TacSfx(int id)
{
	TacLock lk;
	if (id < 0 || id >= TS_N || !s_clip[id].play || !s_ds) return;
	if (id == TS_CURSOR) {
		DWORD n = GetTickCount();
		if (s_cursorAt && (DWORD)(n - s_cursorAt) < 40) return;
		s_cursorAt = n;
	}
	TacSfxPlay(s_clip[id].play, s_clip[id].playN);
}

void TacSfxCast(int element)
{
	TacSfx(CastId(element));
}

void TacSfxMotion(int skill, int kind, int element, int style)
{
	if (otac::IsCraftId(skill)) {
		TacSfx(otac::IsCraftSuper(skill) ? TS_CRAFT_ULT : TS_CRAFT_SW);
		return;
	}
	if (kind == otac::SK_HEAL) {
		if (skill >= 0 && otac::SkillInfo(skill).effect == otac::EF_RAISE) TacSfx(TS_CAST_RAISE);
		else TacSfx(TS_CAST_HEAL);
		return;
	}
	if (kind == otac::SK_BUFF) { TacSfx(TS_CAST_BUFF); return; }
	if (kind == otac::SK_MAGIC) { TacSfxCast(element); return; }
	if (style == otac::AS_SHOOT || style == otac::AS_SHOOT1) { TacSfx(TS_BOW); return; }
	if (style == otac::AS_CHOP2) { TacSfx(TS_SW_CHOP); return; }
	if (style == otac::AS_STAB) { TacSfx(TS_SW_STAB); return; }
	if (style == otac::AS_PUNCH) { TacSfx(TS_SW_PUNCH); return; }
	if (style == otac::AS_SPIN) { TacSfx(TS_SW_SPIN); return; }
	if (style == otac::AS_BITE) { TacSfx(TS_SW_BITE); return; }
	if (style == otac::AS_CAST) { TacSfxCast(element); return; }
	TacSfx(TS_SWING);
}

void TacSfxHit(int skill, int kind, int element, int style, int ally)
{
	if (otac::IsCraftId(skill)) {
		TacSfx(otac::IsCraftSuper(skill) ? TS_CRAFT_ULT : TS_CRAFT_HIT);
		return;
	}
	if (kind == otac::SK_HEAL) {
		if (skill >= 0 && otac::SkillInfo(skill).effect == otac::EF_RAISE) TacSfx(TS_RAISE);
		else TacSfx(TS_HEAL);
		return;
	}
	if (kind == otac::SK_BUFF) { TacSfx(TS_BUFF); return; }
	if (kind == otac::SK_MAGIC) { TacSfx(HitId(element, style)); return; }
	if (ally) { TacSfx(TS_HURT); return; }
	TacSfx(HitId(element, style));
}

void TacSfxMiss(int kind, int style)
{
	if (kind == otac::SK_MAGIC || kind == otac::SK_HEAL || kind == otac::SK_BUFF) { TacSfx(TS_MISS_MAGIC); return; }
	if (style == otac::AS_SHOOT || style == otac::AS_SHOOT1) { TacSfx(TS_MISS_RANGE); return; }
	TacSfx(TS_MISS);
}

void TacSfxStatus(int effect, int blocked)
{
	if (blocked) { TacSfx(TS_BLOCK); return; }
	if (effect == otac::EF_POISON) TacSfx(TS_POISON);
	else if (effect == otac::EF_SLEEP) TacSfx(TS_SLEEP);
	else if (effect == otac::EF_STUN) TacSfx(TS_STUN);
	else if (effect == otac::EF_CONFUSE) TacSfx(TS_CONFUSE);
	else if (effect == otac::EF_SLOW) TacSfx(TS_SLOW);
	else if (effect == otac::EF_DEFDOWN) TacSfx(TS_DEFDOWN);
	else if (effect == otac::EF_CLEANSE) TacSfx(TS_CLEANSE);
}

void TacAudioPump()
{
	if (!s_buf || !s_bytes) return;
	static short tmp[TAC_RATE * 2];
	const int chunkMax = (int)sizeof(tmp);
	const int target = (int)s_bytes - (TAC_RATE * 4 / 5);
	for (int pass = 0; pass < 4; pass++) {
		DWORD play = 0, wrt = 0;
		if (s_started) {
			if (FAILED(s_buf->GetCurrentPosition(&play, &wrt))) return;
		}
		int queued = (int)s_write - (int)play;
		if (queued < 0) queued += (int)s_bytes;
		// アクティブ切替で再生位置が戻って見える。ここで Stop すると音が飛ぶので、この周は書かない。
		if (s_started && queued > (int)s_bytes - 8192)
			return;
		int need = target - queued;
		if (need < 4096) break;
		if (need > chunkMax) need = chunkMax;
		int freeB = (int)s_bytes - queued - 2048;
		if (need > freeB) need = freeB;
		need &= ~3;
		if (need < 4) break;
		TacMix(tmp, need / 4);
		void* p1 = NULL; void* p2 = NULL; DWORD n1 = 0, n2 = 0;
		if (FAILED(s_buf->Lock(s_write, (DWORD)need, &p1, &n1, &p2, &n2, 0))) break;
		if (p1 && n1) memcpy(p1, tmp, n1);
		if (p2 && n2) memcpy(p2, (char*)tmp + n1, n2);
		s_buf->Unlock(p1, n1, p2, n2);
		s_write = (s_write + (DWORD)need) % s_bytes;
	}
	if (!s_started) {
		s_buf->Play(0, 0, DSBPLAY_LOOPING);
		s_started = 1;
	}
}
