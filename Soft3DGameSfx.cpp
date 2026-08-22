#include "stdafx.h"
#include "ogg.h"
#include "Soft3DGameSfx.h"
#include <math.h>
#include <dsound.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { S3SFX_RATE = 44100, S3SFX_ENG = 12, S3SFX_SHOT = 12 };

struct S3SfxEng {
	float phPulse, phSaw, phSub;
	float gain, hz;
	float x, y, z;
	int alive, colorIdx, isPlayer;
	float throttle;
};
struct S3SfxShot {
	int kind, on, param, centered;
	float t, dur;
	float x, y, z;
	unsigned rng;
};

static LPDIRECTSOUND8 s_ownDs;
static LPDIRECTSOUNDBUFFER s_buf;
static LPDIRECTSOUND8 s_boundDs;
static HWND s_hwnd;
static DWORD s_bytes;
static DWORD s_write;
static int s_started;
static float s_lx, s_ly, s_lz, s_yaw;
static S3SfxEng s_eng[S3SFX_ENG];
static S3SfxShot s_shot[S3SFX_SHOT];
static unsigned s_rng = 0xA341316Cu;

static unsigned S3sRng()
{
	s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
	return s_rng;
}

static void S3sReleaseBuf()
{
	if (s_buf) { s_buf->Stop(); s_buf->Release(); s_buf = NULL; }
	s_boundDs = NULL;
	s_started = 0;
	s_write = 0;
}

static void S3sReleaseOwn()
{
	S3sReleaseBuf();
	if (s_ownDs) { s_ownDs->Release(); s_ownDs = NULL; }
}

static int S3sCreateOn(LPDIRECTSOUND8 ds)
{
	if (!ds) return 0;
	WAVEFORMATEX wfx = {};
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = S3SFX_RATE;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = 4;
	wfx.nAvgBytesPerSec = S3SFX_RATE * 4;
	DSBUFFERDESC dsbd = {};
	dsbd.dwSize = sizeof(dsbd);
	dsbd.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCSOFTWARE;
	dsbd.dwBufferBytes = (S3SFX_RATE * 4 * 7) / 20; // 350ms
	dsbd.dwBufferBytes &= ~3u;
	dsbd.lpwfxFormat = &wfx;
	LPDIRECTSOUNDBUFFER raw = NULL;
	if (FAILED(ds->CreateSoundBuffer(&dsbd, &raw, NULL)) || !raw)
		return 0;
	s_buf = raw;
	s_bytes = dsbd.dwBufferBytes;
	s_boundDs = ds;
	void* p1 = NULL; void* p2 = NULL; DWORD n1 = 0, n2 = 0;
	if (SUCCEEDED(s_buf->Lock(0, s_bytes, &p1, &n1, &p2, &n2, 0))) {
		if (p1 && n1) memset(p1, 0, n1);
		if (p2 && n2) memset(p2, 0, n2);
		s_buf->Unlock(p1, n1, p2, n2);
	}
	s_buf->SetVolume(-500);
	s_write = 0;
	s_started = 0;
	return 1;
}

void Soft3DSfxEnsure(HWND hwnd)
{
	s_hwnd = hwnd;
	if (s_buf && s_ownDs && s_boundDs == s_ownDs)
		return;
	S3sReleaseBuf();
	if (!s_ownDs) {
		if (FAILED(DirectSoundCreate8(NULL, &s_ownDs, NULL)) || !s_ownDs)
			return;
		HWND coop = hwnd ? hwnd : GetDesktopWindow();
		s_ownDs->SetCooperativeLevel(coop, DSSCL_NORMAL);
	}
	S3sCreateOn(s_ownDs);
}

void Soft3DSfxShutdown()
{
	S3sReleaseOwn();
	s_hwnd = NULL;
	memset(s_eng, 0, sizeof(s_eng));
	memset(s_shot, 0, sizeof(s_shot));
}

void Soft3DSfxSetListener(float x, float y, float z, float yaw)
{
	s_lx = x; s_ly = y; s_lz = z; s_yaw = yaw;
}

void Soft3DSfxEngine(int slot, int alive, float x, float y, float z,
	float speed, float throttle, int colorIdx, int isPlayer)
{
	if (slot < 0 || slot >= S3SFX_ENG) return;
	S3SfxEng& e = s_eng[slot];
	e.alive = alive ? 1 : 0;
	e.x = x; e.y = y; e.z = z;
	e.colorIdx = colorIdx;
	e.isPlayer = isPlayer ? 1 : 0;
	if (throttle < 0.f) throttle = 0.f;
	if (throttle > 1.f) throttle = 1.f;
	e.throttle = throttle;
	if (speed < 0.f) speed = 0.f;
	float wantHz = 44.f + (float)(colorIdx % 12) * 3.85f + speed * 0.92f + throttle * 34.f;
	if (isPlayer) wantHz += 4.f;
	if (!e.alive) wantHz = e.hz > 1.f ? e.hz : wantHz;
	if (e.hz < 1.f) e.hz = wantHz;
	else e.hz += (wantHz - e.hz) * 0.18f;
	float wantG = e.alive ? (isPlayer ? 0.22f : 0.09f) : 0.f;
	if (e.alive && throttle > 0.55f) wantG *= 1.f + (throttle - 0.55f) * 0.55f;
	e.gain += (wantG - e.gain) * 0.22f;
	if (e.gain < 0.0004f && !e.alive) e.gain = 0.f;
}

static float S3sDur(int kind)
{
	switch (kind) {
	case S3SFX_STEP: return 0.11f;
	case S3SFX_ITEM: return 0.20f;
	case S3SFX_KEY: return 0.28f;
	case S3SFX_DOOR: return 0.42f;
	case S3SFX_STAIR: return 0.38f;
	case S3SFX_PORTAL: return 0.55f;
	case S3SFX_COUNT: return 0.13f;
	case S3SFX_GO: return 0.48f;
	case S3SFX_LAP: return 0.32f;
	case S3SFX_FINISH: return 0.98f;
	case S3SFX_PODIUM: return 1.15f;
	case S3SFX_COURSEOUT: return 0.40f;
	case S3SFX_BOOST: return 0.34f;
	case S3SFX_SLOW: return 0.34f;
	case S3SFX_HIT: return 0.16f;
	case S3SFX_WRONG: return 0.46f;
	case S3SFX_GAMEOVER: return 0.88f;
	case S3SFX_BUMP: return 0.10f;
	case S3SFX_LOCKED: return 0.22f;
	case S3SFX_SPIKE: return 0.22f;
	case S3SFX_SLIME: return 0.30f;
	case S3SFX_ICE: return 0.26f;
	case S3SFX_DARK: return 0.42f;
	case S3SFX_GOAL: return 0.95f;
	default: return 0.22f;
	}
}

void Soft3DSfxOneShotP(int kind, float x, float y, float z, int param)
{
	if (kind < 0 || kind >= S3SFX_N) return;
	int slot = 0;
	float oldest = 0.f;
	for (int i = 0; i < S3SFX_SHOT; i++) {
		if (!s_shot[i].on) { slot = i; break; }
		if (s_shot[i].t >= oldest) { oldest = s_shot[i].t; slot = i; }
	}
	S3SfxShot& s = s_shot[slot];
	s.on = 1;
	s.kind = kind;
	s.param = param;
	s.centered = 0;
	s.t = 0.f;
	s.x = x; s.y = y; s.z = z;
	s.rng = S3sRng();
	s.dur = S3sDur(kind);
}

void Soft3DSfxOneShot(int kind, float x, float y, float z)
{
	Soft3DSfxOneShotP(kind, x, y, z, 0);
}

void Soft3DSfxUi(int kind, int param)
{
	Soft3DSfxOneShotP(kind, s_lx, s_ly, s_lz, param);
	for (int i = 0; i < S3SFX_SHOT; i++) {
		if (s_shot[i].on && s_shot[i].kind == kind && s_shot[i].t < 1e-4f) {
			s_shot[i].centered = 1;
			break;
		}
	}
}

static void S3sPanAtten(float x, float y, float z, int centered, float& gL, float& gR, float& nearK)
{
	float dx = x - s_lx, dy = y - s_ly, dz = z - s_lz;
	float d = sqrtf(dx * dx + dy * dy + dz * dz);
	float att = 1.f / (1.f + d / 18.f);
	if (att > 1.f) att = 1.f;
	nearK = 1.f / (1.f + d / 28.f);
	if (centered || d < 0.15f) {
		gL = att; gR = att;
		return;
	}
	float rs = sinf(s_yaw), rc = cosf(s_yaw);
	float rightX = rc, rightZ = -rs;
	float side = dx * rightX + dz * rightZ;
	float pan = side / (d + 0.8f);
	if (pan < -1.f) pan = -1.f;
	if (pan > 1.f) pan = 1.f;
	float a = (pan + 1.f) * 0.5f * (float)(M_PI * 0.5);
	gL = att * cosf(a);
	gR = att * sinf(a);
}

static float S3sShotSample(S3SfxShot& s, float dt, float& nHold)
{
	s.t += dt;
	if (s.t >= s.dur) { s.on = 0; return 0.f; }
	float u = s.t / s.dur;
	float env = (u < 0.08f) ? (u / 0.08f) : (1.f - (u - 0.08f) / 0.92f);
	if (env < 0.f) env = 0.f;
	s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5;
	float n = ((s.rng & 0xffff) * (1.f / 32768.f)) - 1.f;
	nHold = nHold * 0.86f + n * 0.14f;
	float t = s.t;
	const float tw = (float)(M_PI * 2);
	switch (s.kind) {
	case S3SFX_STEP: {
		float thud = sinf(t * 78.f * tw) * (1.f - u);
		return (thud * 0.55f + nHold * 0.45f) * env * 0.55f;
	}
	case S3SFX_ITEM: {
		float hz = 880.f + 520.f * u;
		return (sinf(t * hz * tw) + 0.35f * sinf(t * hz * 2.f * tw)) * env * 0.42f;
	}
	case S3SFX_KEY: {
		float a = sinf(t * 520.f * tw);
		float b = sinf(t * 784.f * tw);
		return (a * 0.55f + b * 0.45f + nHold * 0.12f) * env * 0.40f;
	}
	case S3SFX_DOOR: {
		float hz = 180.f - 110.f * u;
		return (sinf(t * hz * tw) * 0.35f + nHold * 0.65f) * env * 0.48f;
	}
	case S3SFX_STAIR: {
		float whoosh = nHold * (0.35f + 0.65f * (1.f - u));
		float thud = sinf(t * 92.f * tw) * (1.f - u);
		return (whoosh * 0.7f + thud * 0.45f) * env * 0.46f;
	}
	case S3SFX_COUNT: {
		int step = s.param; if (step < 1) step = 1; if (step > 5) step = 5;
		float hz = 392.f + (float)(5 - step) * 88.f;
		float ph = t * hz; ph -= (float)(int)ph; if (ph < 0.f) ph += 1.f;
		float sq = (ph < 0.5f) ? 1.f : -1.f;
		float click = (u < 0.18f) ? (1.f - u / 0.18f) : 0.f;
		return (sq * 0.55f + nHold * 0.12f) * env * 0.52f + click * n * 0.08f;
	}
	case S3SFX_GO: {
		float a = sinf(t * 523.f * tw);
		float b = sinf(t * 784.f * tw);
		float c = sinf(t * 1046.f * tw) * u;
		return (a * 0.40f + b * 0.38f + c * 0.28f) * env * 0.58f;
	}
	case S3SFX_LAP: {
		float hz = (u < 0.45f) ? 880.f : 1320.f;
		return (sinf(t * hz * tw) * 0.7f + nHold * 0.15f) * env * 0.48f;
	}
	case S3SFX_FINISH: {
		const float notes[4] = { 523.f, 659.f, 784.f, 1046.f };
		int ni = (int)(u * 4.f); if (ni > 3) ni = 3;
		float tone = sinf(t * notes[ni] * tw) + 0.35f * sinf(t * notes[ni] * 2.f * tw);
		return tone * env * 0.46f;
	}
	case S3SFX_PODIUM: {
		float bell = sinf(t * (784.f + 220.f * sinf(t * 6.f)) * tw);
		float swell = sinf(t * 196.f * tw) * (0.35f + 0.65f * u);
		return (bell * 0.45f + swell * 0.40f + nHold * 0.08f) * env * 0.42f;
	}
	case S3SFX_COURSEOUT: {
		float hz = 420.f - 280.f * u;
		return (sinf(t * hz * tw) * 0.35f + nHold * 0.55f) * env * 0.50f;
	}
	case S3SFX_BOOST: {
		float hz = 360.f + 720.f * u;
		return (sinf(t * hz * tw) + 0.3f * sinf(t * hz * 2.f * tw)) * env * 0.46f;
	}
	case S3SFX_SLOW: {
		float hz = 640.f - 420.f * u;
		return (sinf(t * hz * tw) * 0.7f + nHold * 0.2f) * env * 0.44f;
	}
	case S3SFX_HIT: {
		float thud = sinf(t * 70.f * tw) * (1.f - u);
		return (thud * 0.55f + nHold * 0.70f) * env * 0.58f;
	}
	case S3SFX_WRONG: {
		float hz = ((int)(t * 8.f) & 1) ? 311.f : 277.f;
		float ph = t * hz; ph -= (float)(int)ph; if (ph < 0.f) ph += 1.f;
		float sq = (ph < 0.5f) ? 1.f : -1.f;
		return sq * env * 0.38f;
	}
	case S3SFX_GAMEOVER: {
		float hz = 392.f - 220.f * u;
		return (sinf(t * hz * tw) + 0.4f * sinf(t * hz * 1.5f * tw)) * env * 0.44f;
	}
	case S3SFX_BUMP: {
		float thud = sinf(t * 110.f * tw) * (1.f - u);
		return (thud * 0.6f + nHold * 0.45f) * env * 0.50f;
	}
	case S3SFX_LOCKED: {
		float a = sinf(t * 240.f * tw);
		float rattle = nHold * ((int)(t * 28.f) & 1 ? 1.f : 0.35f);
		return (a * 0.35f + rattle * 0.55f) * env * 0.42f;
	}
	case S3SFX_SPIKE: {
		float sting = sinf(t * (1400.f - 800.f * u) * tw);
		return (sting * 0.45f + nHold * 0.50f) * env * 0.50f;
	}
	case S3SFX_SLIME: {
		float blorp = sinf(t * (90.f + 40.f * sinf(t * 9.f)) * tw);
		return (blorp * 0.55f + nHold * 0.35f) * env * 0.44f;
	}
	case S3SFX_ICE: {
		float glass = sinf(t * (2100.f - 900.f * u) * tw);
		return (glass * 0.40f + nHold * 0.28f) * env * 0.40f;
	}
	case S3SFX_DARK: {
		float drone = sinf(t * 55.f * tw);
		return (drone * 0.45f + nHold * 0.50f) * env * 0.46f;
	}
	case S3SFX_GOAL: {
		const float notes[5] = { 523.f, 659.f, 784.f, 988.f, 1174.f };
		int ni = (int)(u * 5.f); if (ni > 4) ni = 4;
		float tone = sinf(t * notes[ni] * tw) + 0.28f * sinf(t * notes[ni] * 2.f * tw);
		return tone * env * 0.46f;
	}
	default: {
		float hz = 220.f + 680.f * u;
		float sw = sinf(t * hz * tw);
		return (sw * 0.55f + nHold * 0.4f) * env * 0.44f;
	}
	}
}

static void S3sRender(short* dst, int frames)
{
	const float dt = 1.f / (float)S3SFX_RATE;
	const int mute = (savedata.s3_pcm_sfx == 0) ? 1 : 0;
	float shotN[S3SFX_SHOT] = {};
	for (int i = 0; i < frames; i++) {
		float L = 0.f, R = 0.f;
		for (int e = 0; e < S3SFX_ENG; e++) {
			S3SfxEng& en = s_eng[e];
			if (en.gain <= 0.0003f && !en.alive) continue;
			float duty = 0.20f + en.throttle * 0.20f + (float)(en.colorIdx % 5) * 0.012f;
			en.phPulse += en.hz * dt;
			en.phSaw += en.hz * 0.5f * dt;
			en.phSub += en.hz * 0.5f * dt;
			if (en.phPulse >= 1.f) en.phPulse -= (float)(int)en.phPulse;
			if (en.phSaw >= 1.f) en.phSaw -= (float)(int)en.phSaw;
			if (en.phSub >= 1.f) en.phSub -= (float)(int)en.phSub;
			if (mute || en.gain <= 0.0003f) continue;
			float gL, gR, nearK;
			S3sPanAtten(en.x, en.y, en.z, en.isPlayer, gL, gR, nearK);
			gL *= en.gain; gR *= en.gain;
			float pulse = (en.phPulse < duty) ? 1.f : -0.42f;
			float saw = en.phSaw * 2.f - 1.f;
			float sub = sinf(en.phSub * (float)(M_PI * 2));
			s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
			float n = ((s_rng & 0xffff) * (1.f / 32768.f)) - 1.f;
			float buzz = pulse * (0.22f + (float)(en.colorIdx % 3) * 0.04f) * nearK
				+ saw * (0.14f + (float)((en.colorIdx / 3) % 3) * 0.03f) * nearK
				+ n * (0.025f + (float)(en.colorIdx & 1) * 0.012f) * nearK;
			float body = sub * (0.42f + en.throttle * 0.38f);
			float v = buzz + body;
			L += v * gL;
			R += v * gR;
		}
		for (int k = 0; k < S3SFX_SHOT; k++) {
			if (!s_shot[k].on) continue;
			float v = S3sShotSample(s_shot[k], dt, shotN[k]);
			if (mute) continue;
			float gL, gR, nearK;
			S3sPanAtten(s_shot[k].x, s_shot[k].y, s_shot[k].z, s_shot[k].centered, gL, gR, nearK);
			(void)nearK;
			L += v * gL;
			R += v * gR;
		}
		if (L > 0.95f) L = 0.95f; else if (L < -0.95f) L = -0.95f;
		if (R > 0.95f) R = 0.95f; else if (R < -0.95f) R = -0.95f;
		dst[0] = (short)(L * 30000.f);
		dst[1] = (short)(R * 30000.f);
		dst += 2;
	}
}

void Soft3DSfxPump()
{
	Soft3DSfxEnsure(s_hwnd);
	if (!s_buf) return;
	if (!s_started) {
		if (FAILED(s_buf->Play(0, 0, DSBPLAY_LOOPING))) {
			S3sReleaseBuf();
			return;
		}
		s_started = 1;
	}
	DWORD play = 0, wcur = 0;
	if (FAILED(s_buf->GetCurrentPosition(&play, &wcur))) {
		S3sReleaseBuf();
		return;
	}
	DWORD lead = (S3SFX_RATE * 4 * 90) / 1000;
	lead &= ~3u;
	DWORD target = play + lead;
	if (target >= s_bytes) target -= s_bytes;
	DWORD toFill = (target + s_bytes - s_write) % s_bytes;
	toFill &= ~3u;
	if (toFill < 256 * 4) return;
	if (toFill > s_bytes / 2) toFill = 2048 * 4;
	void* p1 = NULL; void* p2 = NULL; DWORD n1 = 0, n2 = 0;
	HRESULT hr = s_buf->Lock(s_write, toFill, &p1, &n1, &p2, &n2, 0);
	if (FAILED(hr)) {
		S3sReleaseBuf();
		return;
	}
	if (p1 && n1 >= 4) S3sRender((short*)p1, (int)(n1 / 4));
	if (p2 && n2 >= 4) S3sRender((short*)p2, (int)(n2 / 4));
	s_buf->Unlock(p1, n1, p2, n2);
	s_write += toFill;
	if (s_write >= s_bytes) s_write -= s_bytes;
}
