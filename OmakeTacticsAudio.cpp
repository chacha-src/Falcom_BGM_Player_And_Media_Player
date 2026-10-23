#include "stdafx.h"
#include "OmakeTacticsAudio.h"
#include "resource.h"
#include "oggDlg.h"
#include <vorbis/vorbisfile.h>
#include <dsound.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 効果音は Kenney (www.kenney.nl) Interface / Impact / RPG Audio。CC0。
// WAV をモノラル Ogg Vorbis にして res/tactics/tac_sfx.bin にパックしてある。

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
	0.28f, 0.42f, 0.40f, 0.45f, 0.40f,
	0.50f, 0.50f, 0.22f, 0.48f, 0.46f,
	0.50f, 0.55f, 0.70f, 0.32f, 0.48f,
	0.46f, 0.62f, 0.62f, 0.58f, 0.55f
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
	default: return L"tactics.ogg";
	}
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
	if (had) TacDropQueuedBgm();
	if (!had && s_started) TacPauseMain(1);
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
	d.dwBufferBytes = TAC_RATE * 4 * 2;
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
	float step = (float)s_bgmRate / (float)TAC_RATE;
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

static void TacMix(short* dst, int frames)
{
	for (int i = 0; i < frames * 2; i++) dst[i] = 0;
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
			int l = (int)dst[f * 2] + (int)(L * g);
			int r = (int)dst[f * 2 + 1] + (int)(R * g);
			if (l > 32767) l = 32767; if (l < -32768) l = -32768;
			if (r > 32767) r = 32767; if (r < -32768) r = -32768;
			dst[f * 2] = (short)l;
			dst[f * 2 + 1] = (short)r;
		}
	}
}

void TacAudioStart(void* hwnd)
{
	TacLoadPack();
	TacBgmScene(TAC_BGM_HUB);
	int bgm = s_bgm;
	if (!TacEnsureBuf((HWND)hwnd)) {
		if (bgm) TacCloseBgm();
		return;
	}
	if (bgm) TacPauseMain(1);
}

void TacAudioStop()
{
	TacPauseMain(0);
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

void TacSfx(int id)
{
	if (id < 0 || id >= TS_N || !s_clip[id].play || !s_ds) return;
	if (id == TS_CURSOR) {
		DWORD n = GetTickCount();
		if (s_cursorAt && (DWORD)(n - s_cursorAt) < 40) return;
		s_cursorAt = n;
	}
	const TacClip& clip = s_clip[id];
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
	if (!TacSfxBuf(v, clip.playN)) return;
	v.buf->Stop();
	void* p1 = NULL; void* p2 = NULL; DWORD n1 = 0, n2 = 0;
	DWORD bytes = (DWORD)clip.playN * 4;
	if (FAILED(v.buf->Lock(0, bytes, &p1, &n1, &p2, &n2, 0))) return;
	if (p1 && n1) memcpy(p1, clip.play, n1);
	if (p2 && n2) memcpy(p2, (char*)clip.play + n1, n2);
	v.buf->Unlock(p1, n1, p2, n2);
	v.buf->SetCurrentPosition(0);
	v.buf->Play(0, 0, 0);
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
		if (s_started && queued > (int)s_bytes - 8192) {
			s_buf->Stop();
			s_buf->GetCurrentPosition(&play, &wrt);
			s_write = play;
			s_started = 0;
			queued = 0;
		}
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
