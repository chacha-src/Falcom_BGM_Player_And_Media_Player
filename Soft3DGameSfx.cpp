#include "stdafx.h"
#include "ogg.h"
#include "Soft3DGameSfx.h"
#include "OmakeTacticsAudio.h"
#include <math.h>
#include <dsound.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { S3SFX_RATE = 44100, S3SFX_ENG = 12, S3SFX_SHOT = 12 };

struct S3SfxEng {
	float loopPos;
	float gain, hz;
	float x, y, z;
	int alive, colorIdx, isPlayer;
	float throttle;
};
struct S3SfxShot {
	int kind, on, param, centered;
	float pos, rate;
	float x, y, z;
};
struct S3Clip { short* pcm; int frames; int loop; };

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

static short g_s3pcm[2 * 1024 * 1024];
static int g_s3used;
static S3Clip g_clip[S3SFX_N];
static S3Clip g_eng;
static int g_baked;

static const char* const kS3Mml[S3SFX_N] = {
	// 迷路。石の廊下。足音、拾う光、鍵、扉、階段、転移、壁、錠、棘、スライム、氷、闇、ゴール。
	"t170 l32 o2 v12 @34 c | t170 l32 o2 v8 @31 c",
	"t210 l32 o5 v11 @9 e a >d | t210 l32 o6 v5 @32 r16 a",
	"t190 l32 o5 v12 @21 c e g >c | t190 l16 o4 v7 @5 r8 c",
	"t78 l8 o2 v13 @34 s-12 c4 | t88 l8 o1 v11 @31 r8 c4 | t100 l16 o3 v6 @27 s-8 c",
	"t128 l8 o3 v9 @27 s12 c4 | t148 l16 o3 v11 @34 r8 c e g",
	"t128 l16 o4 v11 @13 c e g >c e | t128 l16 o5 v7 @26 r8 c e g >c | t128 l8 o3 v8 @0 r4 [ceg]4",
	// レースのカウントは矩形。GO、周回、ゴール、表彰、コースアウト、加速、減速は後で。
	"t180 l16 o4 v13 @6 c | t180 l16 o5 v6 @5 c",
	"t148 l8 o4 v13 @4 c8 e g >c4 | t148 l8 o4 v10 @6 e g >c e4 | t148 l8 o2 v12 @31 c c",
	"t220 l32 o5 v12 @26 g >c e | t220 l32 o6 v6 @8 r16 c",
	"t128 l8 o4 v12 @4 c e g >c e g4 | t128 l8 o3 v10 @4 c g >c e g c4 | t128 l8 o5 v7 @26 r4 c e g4",
	"t108 l8 o5 v12 @0 c e g >c g e4 | t108 l8 o4 v8 @21 r4 e g >c4 | t108 l4 o3 v10 @31 c c2",
	"t118 l8 o4 v12 @2 s-12 g4 c4 <g4 | t118 l8 o3 v8 @23 g4 c4",
	"t190 l16 o4 v13 @5 s16 c8 >c8 >c4 | t210 l32 o5 v8 @12 r8 c g >c | t190 l16 o3 v7 @27 s12 c8",
	"t116 l8 o4 v12 @2 s-12 e4 c4 <g4 | t116 l8 o3 v8 @6 e4 c4",
	"t168 l16 o2 v14 @24 c8 | t176 l16 o3 v11 @7 s-6 c8 | t200 l32 o4 v7 @28 c",
	"t146 l16 o4 v12 @6 a- e- | t146 l16 o3 v8 @6 r16 a- e-",
	"t92 l8 o4 v11 @2 g e c <a4 | t92 l8 o3 v8 @14 g e c4 | t92 l4 o2 v10 @31 c2",
	"t200 l32 o2 v14 @24 c | t220 l64 o3 v8 @34 c",
	"t146 l32 o4 v12 @5 c c <g c | t166 l32 o3 v8 @23 r16 c c c",
	"t240 l32 o5 v12 @18 c >c | t220 l16 o4 v8 @7 s-10 c",
	"t88 l8 o2 v12 @16 s8 c s-6 e- s8 c | t96 l8 o3 v6 @25 c4",
	"t200 l32 o6 v11 @11 c e g >c | t176 l16 o5 v6 @26 r16 e",
	"t80 l4 o2 v12 @14 c2 | t80 l4 o2 v8 @31 c2 | t90 l8 o3 v5 @33 [ca-]2",
	"t136 l8 o4 v12 @4 c e g >c4 | t136 l8 o3 v9 @9 c e g >c4 | t136 l8 o2 v10 @31 c r8 c",
};
static const float kS3Gain[S3SFX_N] = {
	0.70f, 0.78f, 0.82f, 0.88f, 0.80f, 0.90f,
	0.72f, 0.95f, 0.80f, 0.98f, 1.00f, 0.84f, 0.90f, 0.82f, 0.92f, 0.78f, 0.90f,
	0.72f, 0.76f, 0.84f, 0.80f, 0.78f, 0.86f, 0.96f
};
static const char* const kS3Eng =
	"t110 l2 o2 v11 @6 c2 | t110 l2 o3 v5 @28 c2 | t110 l2 o1 v8 @4 c2";

static int S3sBakeOne(S3Clip& c, const char* mml, float gain, int loopTail)
{
	c.pcm = NULL; c.frames = 0; c.loop = 0;
	int room = (int)(sizeof(g_s3pcm) / sizeof(g_s3pcm[0]) / 2) - g_s3used;
	if (room < 32 || !mml) return 0;
	int cap = room > S3SFX_RATE * 3 ? S3SFX_RATE * 3 : room;
	short* dst = g_s3pcm + g_s3used * 2;
	int n = YmMmlRender(mml, dst, cap, gain);
	if (n < 8) return 0;
	c.pcm = dst;
	c.frames = n;
	c.loop = n - loopTail;
	if (c.loop < n / 2) c.loop = n;
	g_s3used += n;
	return n;
}
static void S3sBake()
{
	if (g_baked) return;
	g_baked = 1;
	g_s3used = 0;
	for (int i = 0; i < S3SFX_N; i++) S3sBakeOne(g_clip[i], kS3Mml[i], kS3Gain[i], 0);
	S3sBakeOne(g_eng, kS3Eng, 0.85f, (int)(0.12f * S3SFX_RATE));
}
static float S3sRate(int kind, int param)
{
	if (kind == S3SFX_COUNT) {
		if (param >= 3) return 0.8409f;
		if (param == 2) return 1.f;
		return 1.1892f;
	}
	if (kind == S3SFX_LAP) {
		int n = param < 1 ? 1 : param;
		if (n > 8) n = 8;
		return 1.f + (n - 1) * 0.035f;
	}
	return 1.f;
}

void Soft3DSfxOneShotP(int kind, float x, float y, float z, int param)
{
	if (kind < 0 || kind >= S3SFX_N) return;
	S3sBake();
	int slot = 0;
	float oldest = 0.f;
	for (int i = 0; i < S3SFX_SHOT; i++) {
		if (!s_shot[i].on) { slot = i; break; }
		if (s_shot[i].pos >= oldest) { oldest = s_shot[i].pos; slot = i; }
	}
	S3SfxShot& s = s_shot[slot];
	s.on = 1;
	s.kind = kind;
	s.param = param;
	s.centered = 0;
	s.pos = 0.f;
	s.rate = S3sRate(kind, param);
	s.x = x; s.y = y; s.z = z;
}

void Soft3DSfxOneShot(int kind, float x, float y, float z)
{
	Soft3DSfxOneShotP(kind, x, y, z, 0);
}

void Soft3DSfxUi(int kind, int param)
{
	Soft3DSfxOneShotP(kind, s_lx, s_ly, s_lz, param);
	for (int i = 0; i < S3SFX_SHOT; i++) {
		if (s_shot[i].on && s_shot[i].kind == kind && s_shot[i].pos < 1e-4f) {
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

static void S3sStereoAt(const S3Clip& c, float pos, float& oL, float& oR)
{
	oL = oR = 0.f;
	if (!c.pcm || c.frames < 2) return;
	if (pos < 0.f) pos = 0.f;
	int i0 = (int)pos;
	if (i0 >= c.frames) return;
	int i1 = i0 + 1;
	if (i1 >= c.frames) i1 = i0;
	float f = pos - (float)i0;
	oL = c.pcm[i0 * 2] * (1.f - f) + c.pcm[i1 * 2] * f;
	oR = c.pcm[i0 * 2 + 1] * (1.f - f) + c.pcm[i1 * 2 + 1] * f;
	oL *= (1.f / 32767.f);
	oR *= (1.f / 32767.f);
}

static void S3sRender(short* dst, int frames)
{
	const int mute = (savedata.s3_pcm_sfx == 0) ? 1 : 0;
	S3sBake();
	const float engRef = 65.406f;
	for (int i = 0; i < frames; i++) {
		float L = 0.f, R = 0.f;
		for (int e = 0; e < S3SFX_ENG; e++) {
			S3SfxEng& en = s_eng[e];
			if (en.gain <= 0.0003f && !en.alive) continue;
			float step = en.hz / engRef;
			if (step < 0.35f) step = 0.35f;
			if (step > 3.2f) step = 3.2f;
			int loop = g_eng.loop > 32 ? g_eng.loop : g_eng.frames;
			en.loopPos += step;
			if (loop > 32 && en.loopPos >= (float)loop) en.loopPos -= (float)loop;
			if (mute || en.gain <= 0.0003f || g_eng.frames < 8) continue;
			float gL, gR, nearK;
			S3sPanAtten(en.x, en.y, en.z, en.isPlayer, gL, gR, nearK);
			gL *= en.gain; gR *= en.gain;
			float eL, eR;
			S3sStereoAt(g_eng, en.loopPos, eL, eR);
			float body = 0.55f + en.throttle * 0.45f;
			L += eL * body * gL;
			R += eR * body * gR;
		}
		for (int k = 0; k < S3SFX_SHOT; k++) {
			S3SfxShot& sh = s_shot[k];
			if (!sh.on) continue;
			const S3Clip& c = g_clip[sh.kind];
			if (c.frames < 2 || sh.pos >= (float)(c.frames - 1)) { sh.on = 0; continue; }
			float sL, sR;
			S3sStereoAt(c, sh.pos, sL, sR);
			sh.pos += sh.rate > 0.2f ? sh.rate : 1.f;
			if (mute) continue;
			float gL, gR, nearK;
			S3sPanAtten(sh.x, sh.y, sh.z, sh.centered, gL, gR, nearK);
			(void)nearK;
			L += sL * gL;
			R += sR * gR;
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
