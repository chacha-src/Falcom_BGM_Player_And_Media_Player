// Soft3DRaceDlg.cpp — aerial race (Catmull-Rom power band / cute bird-ships)
#include "stdafx.h"
#include "ogg.h"
#include "Soft3DRaceDlg.h"
#include "Soft3DMazeDlg.h"
#include "CMediaPlayerDlg.h"
#include "CCustomPopupMenu.h"
#include "oggDlg.h"
#include <math.h>
#include <new>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "Soft3DRaceNames.inc"
#include "Soft3DTextD2D.h"
#include "Soft3DGameSfx.h"
#include "Soft3DTexRes.h"

#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern void MpPersistSavedataQuick();
extern void MpTaskbarNextTrack();
extern void MpTaskbarPrevTrack();
extern CMediaPlayerDlg* mp;
extern COggDlg* og;
extern int tempo;
extern int pitch;
extern int playf;
extern int mode;

namespace {

struct S3RFloat4 { float x, y, z, w; };
struct S3RMat { float m[16]; };
struct S3RVertex { float x,y,z, nx,ny,nz, u,v, r,g,b,a; };
struct S3RInst { float x,y,z,yaw, sx,sy,sz,pitch, r,g,b,a; };
struct S3RHudVertex { float x,y, u,v, r,g,b,a; }; // uv.x<0 = solid color only
struct S3RFrameCB {
	S3RMat viewProj;
	S3RMat lightVP;
	S3RFloat4 eyePos, fogParams, dofParams, screenSize, misc, lightDir, peel;
	S3RMat reflectVP; // 水面平面反射（未使用時は単位）
};

static S3RMat S3rMatMul(const S3RMat& a, const S3RMat& b)
{
	S3RMat r = {};
	for (int i=0;i<4;i++) for (int j=0;j<4;j++)
		for (int k=0;k<4;k++) r.m[i*4+j] += a.m[i*4+k]*b.m[k*4+j];
	return r;
}
static S3RMat S3rPerspective(float fovy, float aspect, float zn, float zf)
{
	S3RMat r = {};
	const float y = 1.f / tanf(fovy*.5f), x = y / aspect;
	r.m[0]=x; r.m[5]=y; r.m[10]=zf/(zf-zn); r.m[11]=1.f; r.m[14]=-zn*zf/(zf-zn);
	return r;
}
static S3RMat S3rOrtho(float l,float rgt,float b,float t,float zn,float zf)
{
	S3RMat r = {};
	r.m[0]=2.f/(rgt-l); r.m[5]=2.f/(t-b); r.m[10]=1.f/(zf-zn);
	r.m[12]=-(rgt+l)/(rgt-l); r.m[13]=-(t+b)/(t-b); r.m[14]=-zn/(zf-zn); r.m[15]=1.f;
	return r;
}
static S3RMat S3rLookAt(float ex,float ey,float ez,float ax,float ay,float az,float ux,float uy,float uz)
{
	float zx=ax-ex, zy=ay-ey, zz=az-ez;
	float zl=sqrtf(zx*zx+zy*zy+zz*zz); if(zl<1e-6f) zl=1e-6f; zx/=zl; zy/=zl; zz/=zl;
	float xx=uy*zz-uz*zy, xy=uz*zx-ux*zz, xz=ux*zy-uy*zx;
	float xl=sqrtf(xx*xx+xy*xy+xz*xz); if(xl<1e-6f) xl=1e-6f; xx/=xl; xy/=xl; xz/=xl;
	float yx=zy*xz-zz*xy, yy=zz*xx-zx*xz, yz=zx*xy-zy*xx;
	S3RMat r = {};
	r.m[0]=xx; r.m[1]=yx; r.m[2]=zx;
	r.m[4]=xy; r.m[5]=yy; r.m[6]=zy;
	r.m[8]=xz; r.m[9]=yz; r.m[10]=zz;
	r.m[12]=-(ex*xx+ey*xy+ez*xz);
	r.m[13]=-(ex*yx+ey*yy+ez*yz);
	r.m[14]=-(ex*zx+ey*zy+ez*zz); r.m[15]=1.f;
	return r;
}

#define S3R_RELEASE(p) do { if (p) { (p)->Release(); (p)=NULL; } } while(0)
// D3D11 頂点バッファは実質 ~128MB。超えると CreateBuffer が失敗する
static const UINT kS3rMaxVbBytes = 120u * 1024u * 1024u;
// 再生成で 120MB new し直すと x86 で連続領域が取れず地表が消える。小さく分割して使い回す
static const UINT kS3rBakeVertsTry[3] = { 36864u, 12288u, 4096u };
static void S3rUnbindIA(ID3D11DeviceContext* dc)
{
	if (!dc) return;
	ID3D11Buffer* z = NULL;
	UINT st = 0, of = 0;
	dc->IASetVertexBuffers(0, 1, &z, &st, &of);
	dc->IASetVertexBuffers(1, 1, &z, &st, &of);
	dc->IASetIndexBuffer(NULL, DXGI_FORMAT_R32_UINT, 0);
}

static float S3rNormAngle(float a)
{
	while (a > (float)M_PI) a -= (float)(M_PI * 2.0);
	while (a < -(float)M_PI) a += (float)(M_PI * 2.0);
	return a;
}
static float S3rClamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float S3rLerp(float a, float b, float t) { return a + (b - a) * t; }
static float S3rDtK(float dt, float tau)
{
	if (tau < 1e-4f) return 1.f;
	if (dt < 0.f) dt = 0.f;
	float k = 1.f - expf(-dt / tau);
	if (k > 1.f) k = 1.f;
	return k;
}
static float S3rSaturate(float v) { return S3rClamp(v, 0.f, 1.f); }
static BOOL S3rWorldToNdc(const S3RMat& vp, float x, float y, float z, float& ndcX, float& ndcY, float& clipW)
{
	// HLSL row_major mul(float4(p,1), VP)
	const float ox = x*vp.m[0] + y*vp.m[4] + z*vp.m[8]  + vp.m[12];
	const float oy = x*vp.m[1] + y*vp.m[5] + z*vp.m[9]  + vp.m[13];
	const float ow = x*vp.m[3] + y*vp.m[7] + z*vp.m[11] + vp.m[15];
	if (ow <= 0.08f) return FALSE;
	ndcX = ox / ow;
	ndcY = oy / ow;
	clipW = ow;
	if (ndcX < -1.28f || ndcX > 1.28f || ndcY < -1.22f || ndcY > 1.22f) return FALSE;
	return TRUE;
}
static int S3rItemLabSlot(int kind)
{
	if (kind >= CSoft3DRaceDlg::KIND_TEMPO && kind <= CSoft3DRaceDlg::KIND_RANDOM)
		return kind - CSoft3DRaceDlg::KIND_TEMPO;
	return -1;
}
static void S3rItemLabRgb(int kind, float& r, float& g, float& b)
{
	r = 1.f; g = .55f; b = .2f;
	switch (kind) {
	case CSoft3DRaceDlg::KIND_TEMPO: r=.3f;g=1;b=.45f; break;
	case CSoft3DRaceDlg::KIND_TEMPO_DN: r=.1f;g=.55f;b=.25f; break;
	case CSoft3DRaceDlg::KIND_PITCH_UP: r=1;g=.65f;b=.25f; break;
	case CSoft3DRaceDlg::KIND_PITCH_DN: r=.35f;g=.55f;b=1; break;
	case CSoft3DRaceDlg::KIND_NEXT: r=1;g=.2f;b=.35f; break;
	case CSoft3DRaceDlg::KIND_PREV: r=.55f;g=.1f;b=.2f; break;
	case CSoft3DRaceDlg::KIND_VOL_UP: r=1;g=.95f;b=.35f; break;
	case CSoft3DRaceDlg::KIND_VOL_DN: r=.55f;g=.6f;b=.2f; break;
	case CSoft3DRaceDlg::KIND_EQ: r=.75f;g=.35f;b=1; break;
	case CSoft3DRaceDlg::KIND_EQ_FLAT: r=.6f;g=.55f;b=.8f; break;
	case CSoft3DRaceDlg::KIND_REVERB: r=.2f;g=.85f;b=.95f; break;
	case CSoft3DRaceDlg::KIND_XFADE: r=1;g=.45f;b=.75f; break;
	default: break;
	}
}
static void S3rRankWord(int rank, wchar_t* buf, size_t n)
{
	if (!buf || n < 2) return;
	if (rank == 1) wcscpy_s(buf, n, LL14(L"1st", L"1st", L"1er", L"1°", L"1.º", L"1위", L"第1", L"1", L"1-й", L"1.", L"1º", L"1e", L"1.", L"1."));
	else if (rank == 2) wcscpy_s(buf, n, LL14(L"2nd", L"2nd", L"2e", L"2°", L"2.º", L"2위", L"第2", L"2", L"2-й", L"2.", L"2º", L"2e", L"2.", L"2."));
	else if (rank == 3) wcscpy_s(buf, n, LL14(L"3rd", L"3rd", L"3e", L"3°", L"3.º", L"3위", L"第3", L"3", L"3-й", L"3.", L"3º", L"3e", L"3.", L"3."));
	else swprintf_s(buf, n, L"%d%s", rank, LL14(L"位", L"th", L"e", L"°", L".º", L"위", L"名", L"", L"-й", L".", L"º", L"e", L".", L"."));
}
static void S3rNorm3(float& x, float& y, float& z)
{
	float l = sqrtf(x*x+y*y+z*z); if (l < 1e-6f) { x=0;y=1;z=0; return; }
	x/=l;y/=l;z/=l;
}
static float S3rTunR(float bandHalf)
{
	float r = bandHalf * 2.24f;
	if (r < 10.8f) r = 10.8f;
	return r;
}
static int S3rPathIdx(float t)
{
	int n = CSoft3DRaceDlg::S3R_PATH_SAMPLES;
	int i = (int)floorf(t * (float)n);
	i %= n; if (i < 0) i += n;
	return i;
}
static float S3rTunFlare(const unsigned char* deep, int i)
{
	if (!deep[i]) return 0.f;
	const int n = CSoft3DRaceDlg::S3R_PATH_SAMPLES;
	const int win = 24;
	int edge = win;
	for (int d = 1; d <= win; d++) {
		int ip = i + d; if (ip >= n) ip -= n;
		int im = i - d; if (im < 0) im += n;
		if (!deep[ip] || !deep[im]) { edge = d; break; }
	}
	if (edge >= win) return 0.f;
	float u = 1.f - (float)edge / (float)win;
	return u * u * (3.f - 2.f * u);
}
static int S3rPathNearDeep(const unsigned char* deep, int i, int win)
{
	if (deep[i]) return 1;
	const int n = CSoft3DRaceDlg::S3R_PATH_SAMPLES;
	for (int d = 1; d <= win; d++) {
		int ip = i + d; if (ip >= n) ip -= n;
		int im = i - d; if (im < 0) im += n;
		if (deep[ip] || deep[im]) return 1;
	}
	return 0;
}
static float S3rAiMinPaceFrac(int lv)
{
	if (lv <= CSoft3DRaceDlg::AI_SUPER_EASY) return 0.66f;
	if (lv <= CSoft3DRaceDlg::AI_EASY) return 0.76f;
	if (lv <= CSoft3DRaceDlg::AI_NORMAL) return 0.86f;
	if (lv <= CSoft3DRaceDlg::AI_HARD) return 0.91f;
	return 0.95f;
}
static float S3rAiMaxPaceFrac(int lv)
{
	if (lv <= CSoft3DRaceDlg::AI_SUPER_EASY) return 0.86f;
	if (lv <= CSoft3DRaceDlg::AI_EASY) return 0.91f;
	if (lv <= CSoft3DRaceDlg::AI_NORMAL) return 0.98f;
	if (lv <= CSoft3DRaceDlg::AI_HARD) return 1.01f;
	return 1.06f;
}

// 見た目メッシュに合わせた当たり（幹シリンダ＋冠球＋横枝/土管AABB）。単位はメッシュローカル。
static void S3rThemeColShapes(int theme, float& tR, float& tH, float& cY, float& cR, float& bX, float& bY, float& bT, float& bZ)
{
	tR = 0.42f; tH = 2.5f; cY = 2.6f; cR = 1.05f; bX = 0.f; bY = 0.f; bT = 0.f; bZ = 0.f;
	if (theme == 1) { tR = 0.36f; tH = 2.65f; cY = 3.55f; cR = 1.50f; } // forest: 枝は別AABB（交差の隙間を塞がない）
	else if (theme == 6) { tR = 0.22f; tH = 1.05f; cY = 1.85f; cR = 1.18f; } // grass
	else if (theme == 3) { tR = 0.58f; tH = 4.15f; cY = 4.55f; cR = 0.85f; bX = 2.20f; bY = 1.80f; bT = 0.28f; bZ = 0.28f; } // oil
	else if (theme == 4) { tR = 0.78f; tH = 4.70f; cY = 4.95f; cR = 0.28f; } // night
	else if (theme == 2) { tR = 0.62f; tH = 3.45f; cY = 3.75f; cR = 0.45f; bX = 1.38f; bY = 3.35f; bT = 0.22f; bZ = 0.42f; } // ruins
	else if (theme == 7) { tR = 1.05f; tH = 2.75f; cY = 2.40f; cR = 0.85f; bX = 1.90f; bY = 0.50f; bT = 0.55f; bZ = 1.70f; } // mesa
	else if (theme == 5) { tR = 1.10f; tH = 0.55f; cY = 1.15f; cR = 1.08f; bX = 1.82f; bY = 1.05f; bT = 0.22f; bZ = 0.22f; } // under
	else if (theme == 8) { tR = 0.32f; tH = 0.55f; cY = 1.70f; cR = 1.20f; } // cloud
}

struct S3rObsHit { int hit; float nx, ny, nz, push; };

static void S3rAccumObsHit(S3rObsHit& h, float dx, float dy, float dz, float rad)
{
	const float d2 = dx * dx + dy * dy + dz * dz;
	if (d2 >= rad * rad) return;
	float d = sqrtf(d2);
	float pen = rad - d;
	float nx, ny, nz;
	if (d > 1e-4f) { nx = dx / d; ny = dy / d; nz = dz / d; }
	else { nx = 0.f; ny = 1.f; nz = 0.f; pen = rad; }
	if (!h.hit || pen > h.push) {
		h.hit = 1; h.push = pen; h.nx = nx; h.ny = ny; h.nz = nz;
	}
}

static void S3rHitSphereAabb(S3rObsHit& h, float px, float py, float pz,
	float x0, float x1, float y0, float y1, float z0, float z1, float cr)
{
	const float qx = (px < x0) ? x0 : ((px > x1) ? x1 : px);
	const float qy = (py < y0) ? y0 : ((py > y1) ? y1 : py);
	const float qz = (pz < z0) ? z0 : ((pz > z1) ? z1 : pz);
	float dx = px - qx, dy = py - qy, dz = pz - qz;
	const float d2 = dx * dx + dy * dy + dz * dz;
	if (d2 > 1e-8f) {
		S3rAccumObsHit(h, dx, dy, dz, cr);
		return;
	}
	const float px0 = px - x0, px1 = x1 - px;
	const float py0 = py - y0, py1 = y1 - py;
	const float pz0 = pz - z0, pz1 = z1 - pz;
	float best = px0, nx = -1.f, ny = 0.f, nz = 0.f;
	if (px1 < best) { best = px1; nx = 1.f; ny = 0.f; nz = 0.f; }
	if (py0 < best) { best = py0; nx = 0.f; ny = -1.f; nz = 0.f; }
	if (py1 < best) { best = py1; nx = 0.f; ny = 1.f; nz = 0.f; }
	if (pz0 < best) { best = pz0; nx = 0.f; ny = 0.f; nz = -1.f; }
	if (pz1 < best) { best = pz1; nx = 0.f; ny = 0.f; nz = 1.f; }
	const float push = best + cr;
	if (!h.hit || push > h.push) {
		h.hit = 1; h.push = push; h.nx = nx; h.ny = ny; h.nz = nz;
	}
}

static S3rObsHit S3rCraftVsObs(int theme, const CSoft3DRaceDlg::S3rObs& o, float cx, float cy, float cz, float craftR)
{
	S3rObsHit h; h.hit = 0; h.nx = 0.f; h.ny = 1.f; h.nz = 0.f; h.push = 0.f;
	float tR, tH, cY, cR, bX, bY, bT, bZ;
	S3rThemeColShapes(theme, tR, tH, cY, cR, bX, bY, bT, bZ);
	const float cs = cosf(o.yaw), sn = sinf(o.yaw);
	const float dx = cx - o.x, dy = cy - o.y, dz = cz - o.z;
	const float lx = dx * cs - dz * sn;
	const float ly = dy;
	const float lz = dx * sn + dz * cs;
	const float sx = (o.sx > 1e-4f) ? o.sx : 1.f;
	const float sy = (o.sy > 1e-4f) ? o.sy : 1.f;
	const float sz = (o.sz > 1e-4f) ? o.sz : 1.f;
	const float xzS = 0.5f * (sx + sz);

	{
		const float r = tR * xzS + craftR;
		const float y0 = 0.f, y1 = tH * sy;
		const float qy = (ly < y0) ? y0 : ((ly > y1) ? y1 : ly);
		float hx = lx, hy = ly - qy, hz = lz;
		if (hx * hx + hz * hz < 1e-8f && ly >= y0 && ly <= y1) {
			const float push = r;
			if (!h.hit || push > h.push) { h.hit = 1; h.push = push; h.nx = 1.f; h.ny = 0.f; h.nz = 0.f; }
		} else {
			S3rAccumObsHit(h, hx, hy, hz, r);
		}
	}
	S3rAccumObsHit(h, lx, ly - cY * sy, lz, cR * xzS + craftR);
	if (bT > 1e-4f) {
		S3rHitSphereAabb(h, lx, ly, lz,
			-bX * sx, bX * sx, (bY - bT) * sy, (bY + bT) * sy, -bZ * sz, bZ * sz, craftR);
	}
	if (theme == 1) {
		S3rAccumObsHit(h, lx, ly - 2.45f * sy, lz, 1.42f * xzS + craftR);
		S3rAccumObsHit(h, lx, ly - 5.05f * sy, lz, 0.78f * xzS + craftR);
		S3rHitSphereAabb(h, lx, ly, lz, -2.18f * sx, 2.18f * sx, 2.12f * sy, 2.45f * sy, -0.16f * sz, 0.16f * sz, craftR);
		S3rHitSphereAabb(h, lx, ly, lz, -0.16f * sx, 0.16f * sx, 2.50f * sy, 2.86f * sy, -1.88f * sz, 1.88f * sz, craftR);
		S3rHitSphereAabb(h, lx, ly, lz, -1.58f * sx, 1.58f * sx, 3.10f * sy, 3.42f * sy, -0.14f * sz, 0.14f * sz, craftR);
	} else if (theme == 2) {
		S3rHitSphereAabb(h, lx, ly, lz, -1.28f * sx, -0.82f * sx, 0.f, 3.25f * sy, -0.32f * sz, 0.32f * sz, craftR);
		S3rHitSphereAabb(h, lx, ly, lz, 0.82f * sx, 1.28f * sx, 0.f, 3.25f * sy, -0.32f * sz, 0.32f * sz, craftR);
	} else if (theme == 3) {
		S3rHitSphereAabb(h, lx, ly, lz, -1.42f * sx, -0.62f * sx, 0.f, 1.95f * sy, -0.48f * sz, 0.48f * sz, craftR);
		S3rHitSphereAabb(h, lx, ly, lz, 0.62f * sx, 1.42f * sx, 0.f, 1.40f * sy, -0.42f * sz, 0.42f * sz, craftR);
	}

	if (h.hit) {
		const float nx = h.nx, nz = h.nz;
		h.nx = nx * cs + nz * sn;
		h.nz = -nx * sn + nz * cs;
	}
	return h;
}
static DWORD S3rRand(DWORD& rng)
{
	rng = rng * 1664525u + 1013904223u;
	return rng;
}
static float S3rRand01(DWORD& rng) { return (S3rRand(rng) & 0xFFFFFF) / 16777215.f; }

static void S3rEqBump(int band, int delta)
{
	if (band < 0 || band > 14) return;
	int v = savedata.eq[band] + delta;
	if (v < 0) v = 0; if (v > 200) v = 200;
	savedata.eq[band] = v; savedata.eqsoundeq = 9;
}
static void S3rEqFlatten(int step)
{
	for (int i = 0; i < 15; i++) {
		int v = savedata.eq[i];
		if (v > 100) { v -= step; if (v < 100) v = 100; }
		else if (v < 100) { v += step; if (v > 100) v = 100; }
		savedata.eq[i] = v;
	}
	savedata.eqsoundeq = 9;
}
static void S3rSetPitchPos(int pos)
{
	if (pos < 0) pos = 0; if (pos > 400) pos = 400;
	pitch = pos;
	if (og && ::IsWindow(og->GetSafeHwnd())) og->m_pitch_sl.SetPos(pos);
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_pitch.GetSafeHwnd()) mp->m_pitch.SetPos(pos);
}
static void S3rNudgeVolPct(int delta)
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	int p = og->m_sl.GetPos() + delta * 1000;
	if (p < 0) p = 0; if (p > 100000) p = 100000;
	og->m_sl.SetPos(p);
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_vol.GetSafeHwnd()) {
		int v = p / 1000; if (v < 0) v = 0; if (v > 100) v = 100;
		mp->m_vol.SetPos(v, FALSE);
	}
}
static void S3rNudgeReverb(int delta)
{
	int v = savedata.eq_reverb + delta;
	if (v < 0) v = 0; if (v > 100) v = 100;
	savedata.eq_reverb = v;
}

// ---- DirectInput joypad (Qullusrent3-style UpdateJoypadState) ----
struct S3rJoyState {
	float lx, ly, rx, ry;
	float lt, rt;
	int buttons; // bit0=A bit1=B bit2=X bit3=Y bit4=LB bit5=RB bit6=Back bit7=Start
	int hat; // -1 none, 0 up, 1 ur, 2 r, ...
	int connected;
};
static IDirectInput8W* g_s3rDi = NULL;
static IDirectInputDevice8W* g_s3rPad = NULL;
static BOOL g_s3rDiTried = FALSE;

static BOOL CALLBACK S3rEnumPads(const DIDEVICEINSTANCEW* inst, void*)
{
	if (FAILED(g_s3rDi->CreateDevice(inst->guidInstance, &g_s3rPad, NULL)))
		return DIENUM_CONTINUE;
	return DIENUM_STOP;
}
static void S3rEnsureJoypad()
{
	if (g_s3rDiTried) return;
	g_s3rDiTried = TRUE;
	if (FAILED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&g_s3rDi, NULL)))
		return;
	g_s3rDi->EnumDevices(DI8DEVCLASS_GAMECTRL, S3rEnumPads, NULL, DIEDFL_ATTACHEDONLY);
	if (!g_s3rPad) return;
	g_s3rPad->SetDataFormat(&c_dfDIJoystick2);
	HWND hw = AfxGetMainWnd() ? AfxGetMainWnd()->GetSafeHwnd() : NULL; g_s3rPad->SetCooperativeLevel(hw, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
	DIPROPRANGE range = {};
	range.diph.dwSize = sizeof(range);
	range.diph.dwHeaderSize = sizeof(range.diph);
	range.diph.dwHow = DIPH_BYOFFSET;
	range.lMin = -1000; range.lMax = 1000;
	range.diph.dwObj = DIJOFS_X; g_s3rPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_Y; g_s3rPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_RX; g_s3rPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_RY; g_s3rPad->SetProperty(DIPROP_RANGE, &range.diph);
	DIPROPDWORD dead = {};
	dead.diph.dwSize = sizeof(dead);
	dead.diph.dwHeaderSize = sizeof(dead.diph);
	dead.diph.dwHow = DIPH_DEVICE;
	dead.dwData = 1200;
	g_s3rPad->SetProperty(DIPROP_DEADZONE, &dead.diph);
	g_s3rPad->Acquire();
}
static float S3rAxisN(LONG v)
{
	float f = (float)v / 1000.f;
	if (fabsf(f) < 0.18f) return 0.f;
	return S3rClamp(f, -1.f, 1.f);
}
// DirectInput の Z/Rz は機種で ±1000 か 0..65535。後者の静止は中央付近で、/1000 すると常時フルブレーキになる。
static void S3rSplitTriggerAxis(LONG v, float& neg, float& pos)
{
	neg = 0.f; pos = 0.f;
	if (v >= -1200 && v <= 1200) {
		if (v > 220) pos = S3rClamp((float)v / 1000.f, 0.f, 1.f);
		else if (v < -220) neg = S3rClamp((float)(-v) / 1000.f, 0.f, 1.f);
		return;
	}
	float n = ((float)v - 32767.f) / 32767.f;
	const float dz = 0.24f;
	if (n > dz) pos = S3rClamp((n - dz) / (1.f - dz), 0.f, 1.f);
	else if (n < -dz) neg = S3rClamp((-n - dz) / (1.f - dz), 0.f, 1.f);
}
static void UpdateJoypadState(S3rJoyState& out)
{
	memset(&out, 0, sizeof(out));
	out.hat = -1;
	S3rEnsureJoypad();
	if (!g_s3rPad) return;
	HRESULT hr = g_s3rPad->Poll();
	if (FAILED(hr)) { g_s3rPad->Acquire(); hr = g_s3rPad->Poll(); }
	DIJOYSTATE2 st = {};
	hr = g_s3rPad->GetDeviceState(sizeof(st), &st);
	if (FAILED(hr)) return;
	out.connected = 1;
	out.lx = S3rAxisN(st.lX);
	out.ly = S3rAxisN(st.lY);
	out.rx = S3rAxisN(st.lRx);
	out.ry = S3rAxisN(st.lRy);
	float zNeg=0.f, zPos=0.f, sNeg=0.f, sPos=0.f;
	S3rSplitTriggerAxis(st.lZ, zNeg, zPos);
	S3rSplitTriggerAxis(st.rglSlider[0], sNeg, sPos);
	// Xbox DInput: Z が LT/RT の合成（中央静止）。Rz は右スティックなのでトリガーに使わない。
	out.lt = max(zNeg, sNeg);
	out.rt = max(zPos, sPos);
	for (int i = 0; i < 8; i++) if (st.rgbButtons[i] & 0x80) out.buttons |= (1 << i);
	DWORD pov = st.rgdwPOV[0];
	if (pov != 0xFFFFFFFF && (LOWORD(pov) != 0xFFFF))
		out.hat = (int)((pov + 2250) / 4500) % 8;
}
static void S3rReleaseJoypad()
{
	if (g_s3rPad) { g_s3rPad->Unacquire(); g_s3rPad->Release(); g_s3rPad = NULL; }
	if (g_s3rDi) { g_s3rDi->Release(); g_s3rDi = NULL; }
	g_s3rDiTried = FALSE;
}

static const float kCraftColors[12][3] = {
	{1.00f,.55f,.70f},{.45f,.85f,1.00f},{1.00f,.85f,.40f},{.55f,1.00f,.65f},
	{.85f,.55f,1.00f},{1.00f,.65f,.40f},{.70f,.90f,1.00f},{1.00f,.45f,.55f},
	{.60f,.95f,.80f},{.95f,.75f,.95f},{.40f,.70f,1.00f},{1.00f,.95f,.70f}
};

class CS3rHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_S3R_HELP };
	explicit CS3rHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK() { DestroyWindow(); }
	virtual void OnCancel() { DestroyWindow(); }
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose() { DestroyWindow(); }
	DECLARE_MESSAGE_MAP()
};
static CS3rHelpDlg* g_s3rHelp = nullptr;

BEGIN_MESSAGE_MAP(CS3rHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CS3rHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	SetWindowText(LL14(
		L"空中レースガイド", L"Aerial race guide", L"Guide course aérienne", L"Guida gara aerea",
		L"Guía carrera aérea", L"공중 레이스 가이드", L"空中竞速指南", L"دليل السباق الجوي",
		L"Руководство по гонке", L"Luftrennen-Anleitung", L"Guia corrida aérea", L"Luchtracegids",
		L"Przewodnik wyścigu", L"Hava yarışı kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}
void CS3rHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_s3rHelp == this) g_s3rHelp = nullptr;
	delete this;
}
BOOL CS3rHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}
void CS3rHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp)) return;
	CDC& dc = hp.mem;
	dc.SetBkMode(TRANSPARENT);
	dc.SelectObject(GetFont());
	TEXTMETRIC tm{}; dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	CRect rc; GetClientRect(&rc);
	int y = 8; const int L = 10;
	const int contentW = max(200, rc.Width() - L * 2);

	dc.SetTextColor(RGB(55, 45, 95));
	dc.TextOut(L, y, LL14(L"空中レース — 操作と画面の見方", L"Aerial race — controls and visuals",
		L"Course aérienne — commandes et visuels", L"Gara aerea — comandi e visuali",
		L"Carrera aérea — controles y visuales", L"공중 레이스 — 조작과 화면", L"空中竞速 — 操作与画面",
		L"السباق الجوي — التحكم والعرض", L"Воздушная гонка — управление и вид", L"Luftrennen — Steuerung und Ansicht",
		L"Corrida aérea — controles e visuais", L"Luchtrace — bediening en beeld",
		L"Wyścig powietrzny — sterowanie i widok", L"Hava yarışı — kontroller ve görüntü"));
	y += lh + 6;

	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, contentW, min(160, max(120, rc.Height() / 4)),
		CCC_HELPDEMO_KRACE);

	dc.SetTextColor(RGB(55, 45, 85));
	dc.TextOut(L, y, LL14(L"凡例（見た目と説明）", L"Legend (preview & description)", L"Légende (aperçu et description)", L"Legenda (anteprima e descrizione)", L"Leyenda (vista y descripción)",
		L"범례(모습과 설명)", L"图例（外观与说明）", L"Legend (preview & description)", L"Легенда (вид и описание)", L"Legende (Vorschau & Beschreibung)", L"Legenda (visual e descrição)", L"Legenda (weergave en uitleg)",
		L"Legenda (podgląd i opis)", L"Gösterge (görünüm ve açıklama)"));
	y += lh + 2;

	const int sw = max(16, lh + 2);
	const int softColW = max(150, contentW * 38 / 100);
	const int descX = L + softColW + 8;
	const int rowH = lh + 4;
	auto line = [&](LPCTSTR t) { dc.TextOut(L, y, t); y += lh; };
	auto legendHead = [&]() {
		dc.FillSolidRect(L, y, contentW, rowH, RGB(232, 230, 242));
		dc.SetTextColor(RGB(55, 45, 85));
		dc.TextOut(L + 4, y + 2, LL14(L"見た目", L"Preview", L"Aperçu", L"Anteprima", L"Vista", L"모습", L"外观", L"معاينة", L"Вид", L"Vorschau", L"Visual", L"Weergave", L"Podgląd", L"Görünüm"));
		dc.TextOut(descX, y + 2, LL14(L"説明", L"Description", L"Description", L"Descrizione", L"Descripción",
			L"설명", L"说明", L"Description", L"Описание", L"Beschreibung", L"Descrição", L"Beschrijving", L"Opis", L"Açıklama"));
		y += rowH;
	};
	auto legendRow = [&](COLORREF col, LPCTSTR softName, LPCTSTR desc) {
		const BOOL alt = ((y / rowH) & 1) != 0;
		if (alt) dc.FillSolidRect(L, y, contentW, rowH, RGB(244, 244, 250));
		CRect swR(L + 3, y + 2, L + 3 + sw, y + rowH - 2);
		dc.FillSolidRect(swR, col);
		CBrush fr(RGB(90, 90, 110));
		dc.FrameRect(swR, &fr);
		dc.SetTextColor(RGB(40, 40, 55));
		dc.TextOut(L + 3 + sw + 6, y + 2, softName);
		dc.SetTextColor(RGB(70, 70, 85));
		CRect dr(descX, y + 2, L + contentW - 2, y + rowH);
		dc.DrawText(desc, -1, &dr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		y += rowH;
	};

	legendHead();
	legendRow(RGB(140, 220, 255),
		LL14(L"パワーバンド（光帯）", L"Power band (light ribbon)", L"Bande de puissance", L"Fascia di potenza", L"Banda de potencia", L"파워 밴드", L"能量光带", L"Power band", L"Силовая лента", L"Powerband", L"Faixa de potência", L"Powerband", L"Pas mocy", L"Güç bandı"),
		LL14(L"コース本体。帯上で推進力／中央でHP回復。帯外フリー走行可。推進力0で離脱地点へ復帰", L"On-band: thrust regen / center HP. Off-band free flight; thrust 0 = reset to exit", L"Sur bande: poussée; hors bande libre; poussée 0 = retour", L"Sulla fascia: spinta; fuori libero; spinta 0 = reset", L"En banda: empuje; fuera libre; empuje 0 = reinicio", L"밴드 위 추진력/중앙 HP. 밖 자유비행. 추진력0이면 복귀", L"带上恢复推进力/中央回HP；带外自由飞；推进力0回脱离点", L"Thrust on-band; free off-band; thrust 0 = reset", L"Тяга на ленте; вне свободно; тяга 0 = возврат", L"Schub auf Band; off-band frei; Schub 0 = Reset", L"Empuxo na faixa; fora livre; empuxo 0 = reset", L"Stuwkracht op band; buiten vrij; 0 = reset", L"Ciąg na pasie; poza wolno; ciąg 0 = reset", L"Bantta itki; dışı serbest; itki 0 = dönüş"));
	legendRow(RGB(40, 220, 130),
		LL14(L"回復ゾーン（緑の内側帯）", L"Recovery strip (inner green)", L"Zone de soin (bande verte)", L"Zona recupero (verde)", L"Zona de cura (verde)", L"회복 존(초록 내측)", L"回复区（内侧绿带）", L"Recovery strip (inner green)", L"Полоса лечения (зелёная)", L"Heilzone (grün innen)", L"Faixa de cura (verde)", L"Herstelstrook (groen)", L"Strefa leczenia (zielona)", L"İyileşme şeridi (yeşil)"),
		LL14(L"帯の中央に描画。ここにいるとHPが回復する", L"Drawn down the band centre. Stay here to restore HP", L"Au centre de la bande. Restez-y pour soigner les PV", L"Al centro della fascia. Resta qui per curare gli HP", L"En el centro de la banda. Quédate para recuperar HP", L"밴드 중앙. 여기 있으면 HP 회복", L"画在光带正中。停在这里回HP", L"Painted on the band centre. Stay there to restore HP", L"По центру ленты. Стойте здесь — HP восстанавливается", L"In der Bandmitte. Hier regeneriert HP", L"No centro da faixa. Fique aqui para recuperar HP", L"Midden op de band. Blijf hier om HP te herstellen", L"Na środku pasa. Tu regeneruje się HP", L"Bandın ortasında. Burada HP dolar"));
	legendRow(RGB(240, 240, 245),
		LL14(L"スタート／LAP線", L"Start / lap line", L"Ligne départ / tour", L"Linea partenza / giro", L"Línea de salida / vuelta", L"스타트/랩 라인", L"起跑／计圈线", L"Start / lap line", L"Старт / линия круга", L"Start-/Rundenlinie", L"Linha de largada / volta", L"Start-/rondelijn", L"Linia startu / okrążenia", L"Start / tur çizgisi"),
		LL14(L"格子のゲート。通過で周回が進む（ゴールもここ）", L"Chequered gate. Crossing it counts a lap (and the finish)", L"Portique à damiers. Le franchir compte un tour (et l'arrivée)", L"Cancello a scacchi. Attraversarlo conta un giro (e l'arrivo)", L"Puerta de cuadros. Cruzarla cuenta una vuelta (y la meta)", L"체크무늬 게이트. 통과하면 랩(골도 여기)", L"格子门。穿过计一圈（终点也在这）", L"Chequered gate. Crossing counts a lap (and the finish)", L"Клетчатые ворота. Проезд считает круг (и финиш)", L"Kariertes Tor. Überfahren zählt eine Runde (und das Ziel)", L"Portão xadrez. Cruzar conta uma volta (e a chegada)", L"Geblokte poort. Passeren telt een ronde (en de finish)", L"Krata. Przejazd liczy okrążenie (i metę)", L"Damalı kapı. Geçmek tur sayar (bitiş de burada)"));
	legendRow(RGB(55, 120, 70),
		LL14(L"地形（丘・谷・川）", L"Terrain (hills, valleys, rivers)", L"Terrain (collines, vallées, rivières)", L"Terreno (colline, valli, fiumi)", L"Terreno (colinas, valles, ríos)", L"지형(언덕·계곡·강)", L"地形（丘/谷/河）", L"Terrain (hills/valleys/rivers)", L"Рельеф (холмы, долины, реки)", L"Gelände (Hügel, Täler, Flüsse)", L"Terreno (colinas, vales, rios)", L"Terrein (heuvels, dalen, rivieren)", L"Teren (wzgórza, doliny, rzeki)", L"Arazi (tepe, vadi, ırmak)"),
		LL14(L"本物の凹凸。木などは丘の地表から生える。川底は低く、接触でダメージ", L"Real relief; trees grow from hill surfaces. Riverbeds sit low. Contact damages HP", L"Vrai relief; arbres sur les collines. Lits de rivière bas. Contact = dégâts", L"Rilievo vero; alberi sui colli. Alvei bassi. Contatto = danni", L"Relieve real; árboles en colinas. Cauces bajos. Contacto = daño", L"실제 기복. 나무는 언덕 지표에서. 강바닥은 낮음. 접촉 시 데미지", L"真实起伏；树从丘顶长出；河床较低；碰触受伤", L"Real relief; trees on hills; low rivers; hit = damage", L"Настоящий рельеф; деревья на холмах; низкие реки; касание = урон", L"Echtes Relief; Bäume auf Hügeln; tiefe Flüsse; Kontakt = Schaden", L"Relevo real; árvores nas colinas; rios baixos; contato = dano", L"Echt reliëf; bomen op heuvels; lage rivieren; raak = schade", L"Prawdziwy relief; drzewa na wzgórzach; niskie rzeki; dotyk = obrażenia", L"Gerçek rölyef; ağaçlar tepelerde; alçak nehirler; temas = hasar"));
	legendRow(RGB(90, 90, 100),
		LL14(L"山のトンネル／渓谷", L"Mountain tunnels / canyons", L"Tunnels / canyons", L"Tunnel / canyon", L"Túneles / cañones", L"산 터널/협곡", L"山体隧道／峡谷", L"Tunnels / canyons", L"Туннели / каньоны", L"Tunnel / Canyons", L"Túneis / cânions", L"Tunnels / canyons", L"Tunele / kaniony", L"Tünel / kanyon"),
		LL14(L"外側は固体。コースが当たる所だけ頂点を削りトンネルにする。壁・天井に当たるとHP減少。カメラとコースの間の地形は透けて通路が見える", L"Solid from outside; verts carved into a tunnel where the band hits. Walls/ceilings cost HP. Terrain between camera and the course fades so the lane stays visible", L"Solide dehors; tunnel là où la bande passe. Murs/plafonds = PV", L"Solido fuori; tunnel dove passa la fascia. Muri/soffitti = HP", L"Sólido fuera; túnel donde pasa la banda. Paredes/techos = HP", L"밖은 고체. 밴드가 닿는 곳만 터널. 벽·천장 충돌 시 HP↓", L"外侧实心；赛带穿过处挖隧道。撞壁/顶扣HP", L"Solid outside; tunnel where the band passes. Walls/ceilings hurt", L"Снаружи сплошные; туннель где лента. Стены/потолок ранят", L"Außen massiv; Tunnel wo das Band durchgeht. Wände/Decken schaden", L"Sólido por fora; túnel onde a faixa passa. Paredes/tetos ferem", L"Buiten massief; tunnel waar de band gaat. Wanden/plafonds raken", L"Z zewnątrz lite; tunel tam gdzie pas. Ściany/sufity ranią", L"Dışarıdan katı; bantın geçtiği yerde tünel. Duvar/tavan hasar"));
	legendRow(RGB(50, 160, 70),
		LL14(L"樹木・建造物／コース障害", L"Trees / buildings / on-path hazards", L"Arbres / bâtiments / obstacles", L"Alberi / edifici / ostacoli", L"Árboles / edificios / obstáculos", L"나무·건물·코스 장애", L"树木/建筑/赛道障碍", L"Trees/buildings/hazards", L"Деревья/здания/препятствия", L"Bäume/Gebäude/Hindernisse", L"Árvores/prédios/obstáculos", L"Bomen/gebouwen/hindernissen", L"Drzewa/budynki/przeszkody", L"Ağaç/bina/engel"),
		LL14(L"帯に木や地形がめり込む。隙間を抜けて通る。当たるとHP約1/10減り、空いている側へ弾かれる", L"Trees clip the band; fly the gaps. A hit costs ~1/10 HP and knocks you toward the open side", L"Arbres dans la bande; passez les trous. Coup ≈1/10 PV, rejet vers le vide", L"Alberi nella fascia; passa i varchi. Colpo ≈1/10 HP, respinti verso lo spazio libero", L"Árboles en la banda; pasa los huecos. Golpe ≈1/10 HP, rebote al lado libre", L"나무·지형이 밴드에 파고듦. 틈으로 통과. 맞으면 HP 약 1/10, 빈 쪽으로 밀림", L"树和地形嵌入光带；从空隙穿过；碰到约扣1/10 HP并弹向空侧", L"Terrain/props clip the band; fly the gaps. Hit ≈1/10 HP, knock toward the open side", L"Деревья в ленте; летите в просветы. Удар ≈1/10 HP, отброс в свободную сторону", L"Bäume im Band; Lücken durchfliegen. Treffer ≈1/10 HP, Stoß zur freien Seite", L"Árvores na faixa; passe pelos vãos. Acerto ≈1/10 HP, empurra para o lado livre", L"Bomen in de band; vlieg door gaten. Raak ≈1/10 HP, stoot naar de vrije kant", L"Drzewa w pasie; leć prześwitami. Trafienie ≈1/10 HP, odbicie na wolną stronę", L"Ağaç banta gömülür; boşluktan geç. Çarpınca HP ~1/10, boş tarafa savrulur"));
	legendRow(RGB(255, 120, 90),
		LL14(L"機体同士の接触", L"Craft-to-craft contact", L"Contact entre appareils", L"Contatto tra craft", L"Contacto entre naves", L"기체끼리 접촉", L"机体相撞", L"Craft contact", L"Столкновение аппаратов", L"Kontakt zwischen Crafts", L"Contato entre naves", L"Contact tussen crafts", L"Kontakt maszyn", L"Araç çarpışması"),
		LL14(L"当たると少し弾かれ、HPが約1/30減る（自機も敵も）", L"A bump knocks both slightly and costs ~1/30 HP (you and rivals)", L"Choc: petit rejet, ≈1/30 PV (vous et rivaux)", L"Urto: piccolo respingo, ≈1/30 HP (tu e rivali)", L"Choque: rebote leve, ≈1/30 HP (tú y rivales)", L"부딪히면 살짝 밀리고 HP 약 1/30 (자신·상대)", L"相撞会轻弹开，约扣1/30 HP（自己和对手）", L"Bump: slight knock, ~1/30 HP (you and rivals)", L"Удар: лёгкий отброс, ≈1/30 HP (вы и соперники)", L"Stoß: leichter Kick, ≈1/30 HP (Sie und Rivalen)", L"Batida: empurrão leve, ≈1/30 HP (você e rivais)", L"Botsing: lichte stoot, ≈1/30 HP (jij en rivalen)", L"Zderzenie: lekkie odbicie, ≈1/30 HP (ty i rywale)", L"Çarpışma: hafif savrulma, ≈1/30 HP (siz ve rakipler)"));
	legendRow(RGB(220, 80, 50),
		LL14(L"敵AIのHP／爆破リタイア", L"Rival HP / explode retire", L"PV IA / explosion abandon", L"HP IA / ritiro esplosione", L"HP IA / retiro explosión", L"상대 HP/폭발 리타이어", L"对手HP／爆破退赛", L"Rival HP / explode retire", L"HP ИИ / взрыв и сход", L"KI-HP / Explosion-Aufgabe", L"HP IA / abandono explosão", L"AI-HP / explosie-opgave", L"HP SI / wybuch i wycofanie", L"YZ HP / patlama çekilme"),
		LL14(L"敵も障害で同じダメージ。HP0で爆破してその場リタイア。他機に抜かれ順位が下がる。超簡単は12台で最大4機程度", L"Rivals take the same hazard hits. HP 0 explodes and retires in place; others pass so rank falls. Super-easy: at most ~4 of 12", L"Les IA prennent les memes coups. PV 0 = explosion et abandon sur place", L"Le IA subiscono gli stessi colpi. HP 0 = esplosione e ritiro", L"Las IA reciben los mismos golpes. HP 0 = explosion y abandono", L"상대도 같은 장애 데미지. HP0이면 폭발 리타이어. 초간단은 12대 중 최대 4기", L"对手同样受障碍伤害。HP0爆破原地退赛。超简单12台最多约4机", L"Rivals take the same hits. HP 0 explodes and retires in place", L"Соперники получают тот же урон. HP 0 — взрыв и сход", L"Rivalen erleiden denselben Schaden. HP 0 explodiert und gibt auf", L"Rivais sofrem o mesmo dano. HP 0 explode e abandona", L"Rivalen krijgen dezelfde hits. HP 0 explodeert en geeft op", L"Rywale biorą te same trafienia. HP 0 — wybuch i wycofanie", L"Rakipler aynı hasarı alır. HP 0 patlar ve çekilir"));
	legendRow(RGB(255, 170, 90),
		LL14(L"自機（鳥型クラフト）", L"Your craft (bird-ship)", L"Votre appareil", L"Il tuo craft", L"Tu nave", L"기체(새형)", L"自机（鸟型）", L"Your craft", L"Ваш корабль", L"Ihr Craft", L"Sua nave", L"Jouw craft", L"Twój craft", L"Aracınız"),
		LL14(L"斜め上カメラが追従。急坂では徐々に下から見上げる（酔い対策）。マウスで向き、LMB加速／RMBブレーキ", L"Chase cam; on steep climbs it eases to a low look-up. Mouse steers; LMB accel / RMB brake", L"Caméra chase; en montée elle descend progressivement. Souris = direction", L"Camera chase; in salita scende gradualmente. Mouse = sterzo", L"Cámara chase; en subida baja poco a poco. Ratón = dirección", L"추종 카메라. 급경사에서는 천천히 아래에서 올려다봄. 마우스 조향", L"追从相机；急坡时逐渐改从下往上看。鼠标转向", L"Chase cam; steep climbs ease to look-up from below", L"Камера сзади; на крутом подъёме плавно снизу", L"Chase-Kamera; an Steigungen langsam von unten", L"Câmera chase; em subidas desce aos poucos", L"Chase-camera; bij steile helling langzaam van onder", L"Kamera z góry; na stromiźnie stopniowo od dołu", L"Üst-arka kamera; dik yokuşta yavaşça alttan"));
	legendRow(RGB(80, 230, 130),
		LL14(L"アイテム球", L"Item orbs", L"Sphères d'objets", L"Sfere oggetto", L"Orbes de objeto", L"아이템 구", L"道具球", L"Item orbs", L"Сферы предметов", L"Item-Kugeln", L"Orbes de item", L"Item-bollen", L"Kule przedmiotów", L"Öğe küreleri"),
		LL14(L"触れると再生テンポ／ピッチ等＋レース効果。球の上に小さなラベル", L"Touch for playback + race buffs. Small label above the orb", L"Toucher = lecture + buffs. Petite étiquette", L"Tocco = riproduzione + buff. Piccola etichetta", L"Tocar = reproducción + buffs. Etiqueta pequeña", L"접촉 시 재생+레이스 버프. 구 위에 작은 라벨", L"触碰改播放并带竞速效果。球上有小标签", L"Playback + race effects. Small label above", L"Воспроизведение + баффы. Маленькая метка сверху", L"Playback + Buffs. Kleines Label oben", L"Reprodução + buffs. Rótulo pequeno acima", L"Weergave + buffs. Klein label erboven", L"Odtwarzanie + buffy. Mała etykieta u góry", L"Oynatma + buff. Üstte küçük etiket"));
	legendRow(RGB(40, 200, 255),
		LL14(L"ニトロ", L"Nitro", L"Nitro", L"Nitro", L"Nitro", L"니트로", L"氮气加速", L"Nitro", L"Нитро", L"Nitro", L"Nitro", L"Nitro", L"Nitro", L"Nitro"),
		LL14(L"最初1・周回ごとに+1（最大5）。N／Ctrl／パッドX・Y。発動中は障害をすり抜け、自機は敵に当たってもノックバックしない",
			L"Start with 1; +1 each lap (max 5). N / Ctrl / pad X·Y. While active: pass obstacles; you are not knocked back by rivals",
			L"Départ 1; +1/tour (max 5). N/Ctrl/pad X·Y. Icônes sous classement. IA aussi; effet 2/3",
			L"Parti con 1; +1 a giro (max 5). N/Ctrl/pad X·Y. Icone sotto classifica. Anche IA; effetto 2/3",
			L"Empiezas con 1; +1 por vuelta (máx 5). N/Ctrl/pad X·Y. Iconos bajo clasificación. IA también; efecto 2/3",
			L"시작 1개·랩마다 +1(최대5). N/Ctrl/패드 X·Y. 순위판 아래 아이콘. CPU도 획득, 효과는 자기의 2/3",
			L"开局1个；每圈+1（最多5）。N/Ctrl/手柄X·Y。排名板下有图标。CPU同样获得，效果为自机的2/3",
			L"Start with 1; +1 each lap (max 5). N/Ctrl/pad X·Y. Icons under standings. CPU 2/3 power",
			L"Старт с 1; +1 за круг (макс 5). N/Ctrl/pad X·Y. Иконки под таблицей. У ИИ тоже; сила 2/3",
			L"Start mit 1; +1/Runde (max 5). N/Ctrl/Pad X·Y. Icons unter Rangliste. KI auch; Wirkung 2/3",
			L"Começa com 1; +1 por volta (máx 5). N/Ctrl/pad X·Y. Ícones sob classificação. CPU também; efeito 2/3",
			L"Start met 1; +1 per ronde (max 5). N/Ctrl/pad X·Y. Iconen onder stand. CPU ook; effect 2/3",
			L"Start z 1; +1 na okrążenie (maks 5). N/Ctrl/pad X·Y. Ikony pod tabelą. CPU też; efekt 2/3",
			L"Başta 1; turda +1 (en fazla 5). N/Ctrl/pad X·Y. Sıralama altında ikon. CPU da alır; etki 2/3"));
	legendRow(RGB(140, 200, 255),
		LL14(L"デモ走行（生成〜スタート前）", L"Demo (after generate, before Start)", L"Démo (après Générer)", L"Demo (dopo Genera)", L"Demo (tras Generar)", L"데모(생성~시작 전)", L"演示（生成到开始前）", L"Demo until Start", L"Демо до Старта", L"Demo bis Start", L"Demo até Iniciar", L"Demo tot Start", L"Demo do Start", L"Demo → Başlat"),
		LL14(L"俯瞰でコース全体を見せ、自機もAI走行。マウスで視点。スタートで本番", L"Overview of whole course; your craft is AI too. Mouse pans. Start = race", L"Vue d'ensemble; votre craft en IA. Souris=vue. Démarrer=course", L"Vista d'insieme; anche il tuo craft in IA. Mouse=vista. Avvia=gara", L"Vista general; tu nave también IA. Ratón=vista. Iniciar=carrera", L"전체 조감+자기 AI. 마우스 시점. 시작=본경기", L"俯瞰全图，自机也AI跑。鼠标转视角。开始进正式赛", L"Overview + AI you; mouse pans; Start races", L"Обзор; ваш аппарат ИИ; мышь; Старт=гонка", L"Überblick; Ihr Craft KI; Maus; Start=Rennen", L"Visão geral; sua nave IA; mouse; Iniciar=corrida", L"Overzicht; jouw craft AI; muis; Start=race", L"Przegląd; Twój craft AI; mysz; Start=wyścig", L"Genel bakış; gemin AI; fare; Başlat=yarış"));
	legendRow(RGB(255, 230, 140),
		LL14(L"5カウント／GO", L"5-count / GO", L"Compte à rebours / GO", L"Conto alla rovescia / VIA", L"Cuenta atrás / ¡YA!", L"5카운트/GO", L"5倒计时/开始", L"Countdown / GO", L"Отсчёт / СТАРТ", L"Countdown / LOS", L"Contagem / JÁ", L"Aftellen / START", L"Odliczanie / START", L"Geri sayım / BAŞLA"),
		LL14(L"スタート後に大きく表示。GOでレース開始", L"Large overlay after Start; GO begins the race", L"Grand overlay après Démarrer", L"Overlay grande dopo Avvia", L"Overlay grande tras Iniciar", L"시작 후 크게 표시, GO로 레이스", L"开始后大字显示，GO开赛", L"Big overlay; GO starts", L"Крупный оверлей", L"Großes Overlay", L"Overlay grande", L"Grote overlay", L"Duży overlay", L"Büyük kaplama"));
	y += 6;

	dc.SetTextColor(RGB(55, 45, 85));
	dc.TextOut(L, y, LL14(L"操作", L"Controls", L"Commandes", L"Comandi", L"Controles",
		L"조작", L"操作", L"التحكم", L"Управление", L"Steuerung", L"Controles", L"Bediening", L"Sterowanie", L"Kontroller"));
	y += lh + 2;
	dc.SetTextColor(RGB(65, 65, 80));
	line(LL14(L"マウス: 移動=機体のヨー／ピッチ　LMB=加速　RMB=ブレーキ　ホイール=ズーム　MMB=後方視",
		L"Mouse: move=craft yaw/pitch  LMB=accel  RMB=brake  wheel=zoom  MMB=lookback",
		L"Souris : move=lacet/tangage  LMB=accél  RMB=frein  molette=zoom  MMB=recul",
		L"Mouse: move=yaw/pitch  LMB=accel  RMB=freno  rotella=zoom  MMB=dietro",
		L"Ratón: move=yaw/pitch  LMB=acel  RMB=freno  rueda=zoom  MMB=atrás",
		L"마우스: 이동=요/피치  LMB=가속  RMB=브레이크  휠=줌  MMB=후방",
		L"鼠标：移动=偏航/俯仰  左键=加速  右键=刹车  滚轮=缩放  中键=后视",
		L"Mouse: move=yaw/pitch  LMB=accel  RMB=brake  wheel=zoom  MMB=lookback",
		L"Мышь: движение=yaw/pitch  ЛКМ=газ  ПКМ=тормоз  колесо=зум  СКМ=назад",
		L"Maus: Bewegen=Gier/Nick  LMB=Gas  RMB=Bremse  Rad=Zoom  MMB=Rückblick",
		L"Mouse: mover=yaw/pitch  LMB=acel  RMB=freio  roda=zoom  MMB=olhar atrás",
		L"Muis: bewegen=yaw/pitch  LMB=gas  RMB=rem  wiel=zoom  MMB=achteruit",
		L"Mysz: ruch=yaw/pitch  LMB=gaz  RMB=hamulec  kółko=zoom  MMB=wstecz",
		L"Fare: hareket=yaw/pitch  LMB=gaz  RMB=fren  teker=zoom  MMB=geri bak"));
	line(LL14(L"パッド: 左ステック=操舵　RT/A=加速　LT/B=ブレーキ　ハット=代替操舵　右ステック=カメラオフセット　X/Y=ニトロ",
		L"Pad: left stick=steer  RT/A=accel  LT/B=brake  hat=alt steer  right stick=camera offset  X/Y=nitro",
		L"Manette : stick G=diriger  RT/A=accél  LT/B=frein  chapeau=alt  stick D=caméra  X/Y=nitro",
		L"Pad: stick SX=sterzo  RT/A=accel  LT/B=freno  hat=alt  stick DX=camera  X/Y=nitro",
		L"Mando: stick izq=girar  RT/A=acel  LT/B=freno  hat=alt  stick der=cámara  X/Y=nitro",
		L"패드: 왼쪽 스틱=조향  RT/A=가속  LT/B=브레이크  햇=대체  오른쪽 스틱=카메라  X/Y=니트로",
		L"手柄：左摇杆=转向  RT/A=加速  LT/B=刹车  十字=备用  右摇杆=相机偏移  X/Y=氮气",
		L"Pad: left stick=steer  RT/A=accel  LT/B=brake  hat=alt  right stick=camera  X/Y=nitro",
		L"Геймпад: левый стик=руль  RT/A=газ  LT/B=тормоз  hat=alt  правый стик=камера  X/Y=нитро",
		L"Pad: linker Stick=Lenken  RT/A=Gas  LT/B=Bremse  Hut=alt  rechter Stick=Kamera  X/Y=Nitro",
		L"Controle: stick esq=dirigir  RT/A=acel  LT/B=freio  hat=alt  stick dir=câmera  X/Y=nitro",
		L"Pad: linker stick=sturen  RT/A=gas  LT/B=rem  hat=alt  rechter stick=camera  X/Y=nitro",
		L"Pad: lewy stick=ster  RT/A=gaz  LT/B=hamulec  hat=alt  prawy stick=kamera  X/Y=nitro",
		L"Pad: sol çubuk=direksiyon  RT/A=gaz  LT/B=fren  hat=alt  sağ çubuk=kamera  X/Y=nitro"));
	line(LL14(L"ニトロ: N または Ctrl（パッドは X/Y）。スタート時1個・1周完了ごとに+1（最大5）。発動中は噴射エフェクト。CPUも同じ取得だが効きは自機の2/3。",
		L"Nitro: N or Ctrl (pad X/Y). Start with 1; +1 per completed lap (max 5). Exhaust VFX while active. CPU gains the same; power is 2/3 of yours.",
		L"Nitro : N ou Ctrl (pad X/Y). Départ 1; +1/tour (max 5). Effet de jet. IA aussi; puissance 2/3.",
		L"Nitro: N o Ctrl (pad X/Y). Parti con 1; +1 a giro (max 5). Effetto scarico. Anche IA; potenza 2/3.",
		L"Nitro: N o Ctrl (pad X/Y). Empiezas con 1; +1 por vuelta (máx 5). Efecto de escape. IA también; potencia 2/3.",
		L"니트로: N 또는 Ctrl(패드 X/Y). 시작 1개·랩 완료마다 +1(최대5). 분사 이펙트. CPU도 동일 획득, 효과는 2/3.",
		L"氮气：N或Ctrl（手柄X/Y）。开局1个；每完成1圈+1（最多5）。发动时有喷射特效。CPU同样获得，效果为自机的2/3。",
		L"Nitro: N or Ctrl (pad X/Y). Start 1; +1/lap (max 5). Exhaust VFX. CPU same stock; 2/3 power.",
		L"Нитро: N или Ctrl (pad X/Y). Старт 1; +1/круг (макс 5). Эффект выхлопа. У ИИ то же; сила 2/3.",
		L"Nitro: N oder Ctrl (Pad X/Y). Start 1; +1/Runde (max 5). Abgas-VFX. KI ebenso; Wirkung 2/3.",
		L"Nitro: N ou Ctrl (pad X/Y). Começa com 1; +1/volta (máx 5). Efeito de jato. CPU igual; potência 2/3.",
		L"Nitro: N of Ctrl (pad X/Y). Start 1; +1/ronde (max 5). Uitlaat-VFX. CPU ook; kracht 2/3.",
		L"Nitro: N lub Ctrl (pad X/Y). Start 1; +1/okrążenie (maks 5). Efekt dyszy. CPU też; moc 2/3.",
		L"Nitro: N veya Ctrl (pad X/Y). Başta 1; turda +1 (en fazla 5). Egzoz efekti. CPU aynı; güç 2/3."));
	line(LL14(L"上下反転コンボ: マウス上下とパッド上下のピッチを反転（savedataに保存）",
		L"Invert-Y combo: flips mouse/pad pitch (saved in savedata)",
		L"Combo inversion Y : inverse le tangage souris/manette (sauvé)",
		L"Combo inverti Y: inverte pitch mouse/pad (salvato)",
		L"Combo invertir Y: invierte pitch ratón/mando (guardado)",
		L"상하 반전 콤보: 마우스/패드 피치 반전(저장됨)",
		L"上下翻转下拉：翻转鼠标/手柄俯仰（会保存）",
		L"Invert-Y combo flips mouse/pad pitch (persisted)",
		L"Комбо Invert-Y: инверсия pitch мыши/пада (сохраняется)",
		L"Invert-Y-Kombo: Maus/Pad-Pitch umkehren (gespeichert)",
		L"Combo inverter Y: inverte pitch mouse/pad (salvo)",
		L"Invert-Y-combo: muis/pad-pitch omkeren (opgeslagen)",
		L"Combo Invert-Y: odwraca pitch myszy/pada (zapisywane)",
		L"Y ters çevir: fare/pad pitch ters (kaydedilir)"));
	line(LL14(L"帯外はフリー走行でショートカット可。推進力ゲージが0になると動けず離脱地点へ復帰（HPはそのまま）。障害物に当たるとHP約1/10減り、空いている側へ弾かれる。",
		L"Off-band free flight for shortcuts. Thrust gauge 0 = reset to exit (HP unchanged). Hitting an obstacle costs ~1/10 HP and knocks you toward the open side.",
		L"Hors bande libre. Poussée 0 = retour (PV inchangé). Obstacle ≈1/10 PV, rejet vers le vide.",
		L"Fuori libero. Spinta 0 = reset (HP invariato). Ostacolo ≈1/10 HP, respinti verso lo spazio libero.",
		L"Fuera libre. Empuje 0 = reinicio (HP igual). Obstáculo ≈1/10 HP, rebote al lado libre.",
		L"밴드 밖 자유 비행. 추진력 0이면 복귀(HP 유지). 장애물에 맞으면 HP 약 1/10, 빈 쪽으로 밀림.",
		L"带外可抄近路；推进力0回脱离点（HP不变）。撞障碍约扣1/10 HP并弹向空侧。",
		L"Off-band shortcuts; thrust 0 = reset (HP kept). Obstacle hit ≈1/10 HP, knock toward the open side.",
		L"Вне ленты свободно; тяга 0 = возврат (HP без изменений). Удар ≈1/10 HP, отброс в свободную сторону.",
		L"Off-band frei; Schub 0 = Reset (HP bleibt). Treffer ≈1/10 HP, Stoß zur freien Seite.",
		L"Fora livre; empuxo 0 = reset (HP igual). Acerto ≈1/10 HP, empurra para o lado livre.",
		L"Buiten vrij; stuwkracht 0 = reset (HP blijft). Raak ≈1/10 HP, stoot naar de vrije kant.",
		L"Poza pasem wolno; ciąg 0 = reset (HP bez zmian). Trafienie ≈1/10 HP, odbicie na wolną stronę.",
		L"Bant dışı serbest; itki 0 = dönüş (HP aynı). Çarpınca HP ~1/10, boş tarafa savrulur."));
	line(LL14(L"機体同士が当たると少し弾かれHP約1/30。敵AIもHPがあり、0で爆破してその場リタイア（順位は抜かれて下がる）。AIレベルで蛇行・ショートカット・減速の上手い下手が出る。超簡単は12台でリタイア最大4機程度。強烈でも勝てる。",
		L"Craft bumps knock slightly (~1/30 HP). Rivals have HP too; at 0 they explode and retire in place (rank falls as others pass). AI level is weaving, cuts, braking. Super-easy: at most ~4 of 12 retire. Intense is still beatable.",
		L"Choc entre appareils ≈1/30 PV. Les IA ont aussi des PV; à 0 explosion et abandon. Super facile: ≤4/12. Intense reste battable.",
		L"Urto tra craft ≈1/30 HP. Anche le IA hanno HP; a 0 esplodono e si ritirano. Super facile: ≤4/12. Intenso è battibile.",
		L"Choque entre naves ≈1/30 HP. Las IA también tienen HP; a 0 explosionan y abandonan. Super fácil: ≤4/12. Intenso se puede ganar.",
		L"기체끼리 부딪히면 HP 약 1/30. 상대도 HP가 있고 0이면 폭발 리타이어. 초간단은 12대 중 최대 4기. 강렬해도 이길 수 있음.",
		L"机体相撞约扣1/30 HP。对手也有HP，归零爆破原地退赛。超简单12台最多约4机。强烈仍可赢。",
		L"Craft bumps ~1/30 HP. Rivals explode and retire at HP 0. Super-easy: at most ~4 of 12. Intense is beatable.",
		L"Столкновение ≈1/30 HP. У ИИ тоже HP; при 0 взрыв и сход. Сверхлегко: ≤4 из 12. Сложный уровень обыгрывается.",
		L"Craft-Kontakt ≈1/30 HP. Rivalen explodieren bei HP 0. Superleicht: höchstens ~4 von 12. Intensiv ist schlagbar.",
		L"Batida entre naves ≈1/30 HP. Rivais explodem e abandonam em HP 0. Super fácil: ≤4 de 12. Intenso ainda é vencível.",
		L"Botsing ≈1/30 HP. Rivalen exploderen bij HP 0. Super makkelijk: max. ~4 van 12. Intens is te verslaan.",
		L"Zderzenie ≈1/30 HP. Rywale wybuchają przy HP 0. Super łatwy: maks. ~4 z 12. Intensywny da się wygrać.",
		L"Çarpışma ≈1/30 HP. Rakipler HP 0’da patlar ve çekilir. Çok kolay: 12’de en fazla ~4. Yoğun da yenilebilir."));
	line(LL14(L"右のミニマップ下に順位パネル（名前の右へLAPを2行・最大4枠。入りきらない周は出さない）。下段に自機ニトロ所持アイコン。自機行だけ色が違う。ゴール後は表彰台で1〜3位を表示。",
		L"Standings under the minimap (name/rank; up to 4 recent LAP times in two rows). Nitro stock icons below. Your row is tinted. After finish: podium 1–3.",
		L"Classement sous la minimap (nom/rang/tours). Icônes nitro en bas. Votre ligne est teintée. Après: podium 1–3.",
		L"Classifica sotto minimap (nome/grado/giri). Icone nitro sotto. La tua riga è evidenziata. Poi podio 1–3.",
		L"Clasificación bajo el minimapa (nombre/puesto/vueltas). Iconos nitro abajo. Tu fila resalta. Luego podio 1–3.",
		L"미니맵 아래 순위판(이름·순위·최근 랩). 하단에 니트로 아이콘. 자행만 색 다름. 종료 후 시상대 1~3.",
		L"小地图下为排名板（名字/名次/近几圈）。底部有氮气库存图标。自机行异色。完赛后领奖台1–3名。",
		L"Standings under minimap; nitro icons below; your row highlighted. Podium 1–3 after finish.",
		L"Таблица под миникартой; иконки нитро внизу; ваша строка выделена. Подиум 1–3 после финиша.",
		L"Rangliste unter der Minimap; Nitro-Icons unten; Ihre Zeile hervorgehoben. Danach Podium 1–3.",
		L"Classificação sob o minimapa; ícones nitro abaixo; sua linha destacada. Pódio 1–3 após a chegada.",
		L"Stand onder minimap; nitro-iconen onder; jouw rij gekleurd. Daarna podium 1–3.",
		L"Tabela pod minimapą; ikony nitro poniżej; Twój wiersz wyróżniony. Potem podium 1–3.",
		L"Minimapi altında sıralama; altta nitro ikonları; satırın farklı. Bitince podyum 1–3."));
	line(LL14(L"カメラは左右・上下とも遅れて追従し、3D酔いを抑えます。急坂では視点が徐々に下から見上げるようになります。トンネル前では視点が低くなりますが、MMB後方視はそのときも優先されます。画面上中央にバックミラー（後方カメラ）、水面には平面反射があります。地形は丘・谷・川があり、巨大な山は外側が固体のままトンネルで通ります。カメラとコースの間の壁や天井は透けます。木や建物は帯にめり込んでよく、通れる隙間を抜けます。",
		L"Camera lags on both axes to reduce motion sickness. On steep climbs it eases to a look-up from below. Near tunnels the view dips, but MMB lookback still wins. Top-center rearview mirror (rear camera); water uses a planar reflection. Hills/valleys/rivers; mountains stay solid outside with a tunnel for the band. Walls between camera and the course fade. Trees and buildings may clip into the band; fly the gaps.",
		L"Caméra en retard. En montée contre-plongée. Près des tunnels la vue baisse, mais MMB recul prime. Rétroviseur en haut; réflexion plane sur l'eau. Collines/vallées/rivières; montagnes tunnélées. Arbres et bâtiments peuvent s'enfoncer dans la bande; passez les trous.",
		L"Camera in ritardo. In salita dal basso. Vicino ai tunnel la vista scende, ma MMB dietro ha priorità. Specchietto in alto; riflessione planare sull'acqua. Colline/valli/fiumi; montagne con tunnel. Alberi ed edifici possono entrare nella fascia; passa i varchi.",
		L"Cámara con retraso. En subida contrapicado. Cerca de túneles baja la vista, pero MMB atrás manda. Retrovisor arriba; reflexión planar en el agua. Colinas/valles/ríos; montañas con túnel. Árboles y edificios pueden meterse en la banda; pasa los huecos.",
		L"카메라는 좌우·상하 지연 추종. 급경사에서는 아래에서 올려다봄. 터널 앞에서 시점이 내려가지만 MMB 후방이 우선. 상단 중앙 백미러, 수면은 평면 반사. 언덕·계곡·강, 산은 터널. 나무·건물이 밴드에 파고들어도 틈으로 통과.",
		L"相机延迟跟随以减轻晕动。急坡时从下往上看。隧道前视角会下降，但中键后视仍优先。画面上方中央有后视镜；水面为平面反射。丘谷河；大山走隧道。树和建筑可嵌入光带，从空隙穿过。",
		L"Lagging chase cam; steep climbs ease to a look-up. Near tunnels the view dips but MMB lookback wins. Top-center rearview; planar water reflection. Hills/valleys/rivers; tunneled mountains. Props may clip the band; fly the gaps.",
		L"Камера с запаздыванием; на подъёме снизу. У тоннеля вид опускается, но MMB назад важнее. Зеркало сверху; отражение воды. Холмы, долины, реки; туннели в горах. Деревья могут входить в ленту — летите в просветы.",
		L"Nachlaufende Kamera; an Steigungen von unten. Vor Tunneln senkt sich die Sicht, MMB-Rückblick hat Vorrang. Rückspiegel oben; planare Wasserreflexion. Hügel/Täler/Flüsse; Tunnel durch Berge. Bäume dürfen ins Band ragen; Lücken durchfliegen.",
		L"Câmera atrasada; em subidas de baixo. Perto de túneis a vista desce, mas MMB atrás manda. Retrovisor no topo; reflexão planar na água. Colinas/vales/rios; túneis nas montanhas. Árvores podem entrar na faixa; passe pelos vãos.",
		L"Nalopende camera; bij helling van onder. Bij tunnels daalt het zicht, maar MMB-achteruit wint. Achteruitkijkspiegel bovenaan; vlakke waterreflectie. Heuvels/dalen/rivieren; tunnels door bergen. Bomen mogen in de band steken; vlieg door gaten.",
		L"Opóźniona kamera; na stromiźnie od dołu. Przy tunelach widok opada, ale MMB wstecz ma pierwszeństwo. Lusterko u góry; odbicie wody. Wzgórza/doliny/rzeki; tunele w górach. Drzewa mogą wchodzić w pas; leć prześwitami.",
		L"Gecikmeli kamera; dik yokuşta alttan. Tünel önünde bakış iner ama MMB geri bak öncelikli. Üstte dikiz aynası; suda düzlemsel yansıma. Tepe/vadi/ırmak; dağ tünelleri. Ağaçlar banta gömülebilir; boşluktan geçin."));
	line(LL14(L"テーマで森〜雲の庭まで変化。地形もオブジェクトもテーマごと。ライセンスフリーの写真テクスチャ56枚（512）をexeに埋め込んでいます（別ファイル不要）。地面はテーマ写真に細部を重ね、水面・機体・空の雲カード（複数種）とキューブも写真。反射と乱反射があり、アイテムは結晶球です。生成のたびに輪郭も抽選（楕円／高低差のある八の字／角の丸い凸型／くびれた凹型）。八の字の交差は段差なので横切ってショートカットできない。枝下／土管の間／山のトンネル／急坂／開けた区間が混在。",
		L"Themes from forest to cloud garden. Terrain and props change per theme. Fifty-six license-free photo textures (512px) are embedded in the exe (no extra files): theme albedo plus detail, water, craft, several cloud cards and a sky cube. Wrap lighting and reflection; items are crystal orbs. Each Generate also picks a plan: oval, figure-8 with a height split (no flat shortcut through the crossing), rounded convex, or a concave pinch. Mix of canopy gaps, pipe gaps, mountain tunnels, steep climbs and open stretches.",
		L"Thèmes forêt→jardin de nuages. 56 textures photo 512 dans l'exe (albédo+détail, eau, nuages, ciel). Réflexion ; objets en orbes. Générer tire aussi le tracé: ovale, 8 avec dénivelé (pas de coupe à plat), convexe arrondi ou concave. Branches, tuyaux, tunnels, pentes, ouvert.",
		L"Temi foresta→giardino di nubi. 56 texture foto 512 nell'exe (albedo+dettaglio, acqua, nubi, cielo). Riflessione; oggetti a sfera. Genera estrae anche il tracciato: ovale, 8 con dislivello, convesso o concavo. Rami, tubi, tunnel, salite, aperti.",
		L"Temas bosque→jardín de nubes. 56 texturas foto 512 en el exe (albedo+detalle, agua, nubes, cielo). Reflexión; objetos en orbe. Generar también elige el trazado: óvalo, 8 con desnivel, convexo o cóncavo. Ramas, tuberías, túneles, pendientes.",
		L"테마: 숲~구름 정원. 라이선스 프리 사진 텍스처 56장(512)을 exe에 내장. 지면은 테마+세부, 수면·기체·구름 카드·하늘도 사진. 반사·난반사, 아이템은 결정 구. 생성마다 윤곽도 추첨(타원/고저 8자/볼록/오목). 8자 교차는 단차라 가로질러 숏컷 불가. 가지/파이프/터널/급경사/개방 혼합.",
		L"主题从森林到云之庭。许可证自由照片纹理56张、512、嵌入exe。地面为主题加细节，水面/机体/云卡/天空亦为照片。反射与漫反射，道具为结晶球。每次生成也抽轮廓：椭圆／带高低差的8字（交叉处不能平切）／圆角凸形／收腰凹形。枝下/管隙/山洞/急坡/开阔混在。",
		L"Themes forest→cloud garden. 56 photo textures 512 in the exe (albedo+detail, water, cloud cards, sky cube). Reflection; crystal item orbs. Generate also picks oval, a height-split figure-8 (no flat shortcut), rounded convex or a concave pinch. Branches/pipes/tunnels/climbs/open.",
		L"Темы лес→облачный сад. 56 фототекстур 512 в exe (альбедо+деталь, вода, облака, небо). Отражение; предметы-сферы. Создать также выбирает овал, восьмёрку с перепадом, выпуклый или вогнутый контур.",
		L"Themen Wald→Wolkengarten. 56 Fototexturen 512 in der exe (Albedo+Detail, Wasser, Wolken, Himmel). Reflexion; Item-Kugeln. Erzeugen wählt auch Oval, Achter mit Höhensprung, konvex oder konkav.",
		L"Temas floresta→jardim de nuvens. 56 texturas foto 512 no exe (albedo+detalhe, água, nuvens, céu). Reflexão; orbes de item. Gerar também escolhe oval, 8 com desnível, convexo ou côncavo.",
		L"Thema's bos→wolken tuin. 56 fototexturen 512 in de exe (albedo+detail, water, wolken, lucht). Reflectie; item-bollen. Genereren kiest ook ovaal, 8-vorm met hoogteverschil, convex of concaaf.",
		L"Tematy las→ogród chmur. 56 tekstur zdjęciowych 512 w exe (albedo+szczegół, woda, chmury, niebo). Odbicie; kule przedmiotów. Generuj losuje też owal, ósemkę z przewyższeniem, wypukły lub wklęsły.",
		L"Temalar orman→bulut bahçesi. 56 adet 512 fotoğraf dokusu exe içinde (albedo+ayrıntı, su, bulut, gök). Yansıma; öğe küreleri. Üret oval, yükseklik farklı 8, dışbükey veya içbükey de seçer."));
	line(LL14(L"アイテム球の上に、機体と同じ系統の小さなラベルが出ます（種類が分かります）。",
		L"A small craft-style label sits above each item orb so you can tell the type.",
		L"Petite étiquette (comme les appareils) au-dessus de chaque orbe.",
		L"Piccola etichetta (come i craft) sopra ogni sfera.",
		L"Etiqueta pequeña (como las naves) encima de cada orbe.",
		L"기체와 같은 작은 라벨이 아이템 구 위에 뜹니다.",
		L"道具球上方有与机体同系的小标签，便于辨认种类。",
		L"Small craft-style label above each orb.",
		L"Маленькая метка над каждой сферой, как у аппаратов.",
		L"Kleines Label wie bei den Crafts über jeder Item-Kugel.",
		L"Rótulo pequeno no estilo das naves acima de cada orbe.",
		L"Klein label zoals bij crafts boven elke item-bol.",
		L"Mała etykieta jak przy maszynach nad każdą kulą.",
		L"Her öğe küresinin üstünde araçlardaki gibi küçük etiket."));
	line(LL14(L"右クリック（ステータス／枠）: コース（リスタート／再生成）・表示・アイテム種類など。ビュー上はRMB=ブレーキのみ。",
		L"Right-click (status/chrome): Course (restart/regen), view, item masks. On the view, RMB=brake only.",
		L"Clic droit (statut/cadre) : parcours (redémarrer/régénérer), vue, objets. Sur la vue, RMB=frein.",
		L"Clic destro (stato/cornice): percorso (riavvio/rigenera), vista, oggetti. Sulla vista RMB=freno.",
		L"Clic derecho (estado/marco): recorrido (reinicio/regenerar), vista, objetos. En la vista RMB=freno.",
		L"우클릭(상태/프레임): 코스(재시작/재생성)·보기·아이템. 뷰에서는 RMB=브레이크만.",
		L"右键（状态/边框）：赛道（重启/再生成）、显示、道具。视图内右键仅为刹车。",
		L"Right-click chrome: course/view/items. On view RMB=brake.",
		L"ПКМ по рамке: трасса/вид/предметы. На виде ПКМ=тормоз.",
		L"Rechtsklick am Rahmen: Strecke/Ansicht/Items. In der Ansicht RMB=Bremse.",
		L"Clique direito no quadro: percurso/vista/itens. Na vista RMB=freio.",
		L"Rechtsklik op kader: parcours/weergave/items. Op view RMB=rem.",
		L"PPM na ramce: tor/widok/przedmioty. Na widoku RMB=hamulec.",
		L"Çerçevede sağ tık: parkur/görünüm/öğeler. Görüntüde RMB=fren."));
	line(LL14(L"PCM効果音: エンジンに加え、カウント／GO、LAP、ゴール、表彰、コースアウト、アイテム、衝突、逆走を合成。曲の再生とは別経路。表示メニューでON/OFF。",
		L"PCM SFX: engines plus countdown/GO, lap, finish, podium, course-out, items, hits and wrong-way (separate from music). Toggle in View.",
		L"SFX PCM : moteurs, compte à rebours/GO, tour, arrivée, podium, hors piste, objets, chocs. Menu Vue.",
		L"SFX PCM: motori, conto/VIA, giro, arrivo, podio, fuori pista, oggetti, urti. Menu Vista.",
		L"SFX PCM: motores, cuenta/YA, vuelta, meta, podio, fuera, objetos, golpes. Menú Vista.",
		L"PCM 효과음: 엔진+카운트/GO·랩·골·시상·코스아웃·아이템·충돌·역주행. 보기 메뉴 ON/OFF.",
		L"PCM效果音：引擎外还有倒计时/开始、计圈、完赛、领奖、冲出、道具、碰撞、逆行。显示菜单开关。",
		L"PCM SFX: engines, countdown/GO, lap, finish, podium, course-out, items, hits. View menu toggle.",
		L"PCM SFX: двигатели, отсчёт/старт, круг, финиш, подиум, вне трассы, предметы, удары.",
		L"PCM-SFX: Motoren, Countdown/LOS, Runde, Ziel, Podium, Course-out, Items, Treffer. Ansicht-Menü.",
		L"SFX PCM: motores, contagem/JÁ, volta, chegada, pódio, fora, itens, impactos. Menu Vista.",
		L"PCM-SFX: motoren, aftellen/START, ronde, finish, podium, course-out, items, hits. Weergave-menu.",
		L"SFX PCM: silniki, odliczanie/START, okrążenie, meta, podium, poza torem, przedmioty, uderzenia.",
		L"PCM SFX: motor, geri sayım/BAŞLA, tur, bitiş, podyum, kurs dışı, öğe, çarpışma. Görünüm menüsü."));
	line(LL14(L"迷路と同時には開けません。AI／敵機数／長さ／周回／テーマ／密度はコンボで変更（保存されます）。軽量〜美麗（11〜19と美麗は分割描画で約88〜160倍）。5が既定。",
		L"Cannot open with the maze at once. AI/opponents/length/laps/theme/density combos persist. Light–Fine (11–19 and Fine split-draw ~88–160×). Default 5.",
		L"Pas avec le labyrinthe. Combos sauvés. Léger–Fin (11–19 et Fin en plusieurs buffers, ~88–160×). Défaut 5.",
		L"Non insieme al labirinto. Combo salvate. Leggero–Fine (11–19 e Fine a più buffer, ~88–160×). Predef. 5.",
		L"No a la vez con el laberinto. Combos se guardan. Ligero–Fino (11–19 y Fino en varios buffers, ~88–160×). Predet. 5.",
		L"미로와 동시 불가. 콤보 저장. 가벼움~미려(11–19·미려는 분할 묘화 약88–160배). 기본 5.",
		L"不可与 迷宫同时开。设置会保存。轻量〜美丽（11–19与美丽为分块绘制约88–160倍）。默认5。",
		L"Not with maze. Combos persist. Light–Fine (11–19 and Fine split-draw ~88–160×). Default 5.",
		L"Не вместе с лабиринтом. Комбо сохраняются. Лёгкий–Изящный (11–19 и Fine, ~88–160×). По умолч. 5.",
		L"Nicht zusammen mit dem Labyrinth. Kombos gespeichert. Leicht–Fein (11–19 und Fein, ~88–160×). Standard 5.",
		L"Não junto com o labirinto. Combos salvos. Leve–Belo (11–19 e Belo, ~88–160×). Padrão 5.",
		L"Niet samen met het doolhof. Combo's bewaard. Licht–Fijn (11–19 en Fijn, ~88–160×). Standaard 5.",
		L"Nie razem z labiryntem. Combo zapisywane. Lekki–Piękny (11–19 i Fine, ~88–160×). Domyślnie 5.",
		L"Labirent ile birlikte değil. Kombolar kaydedilir. Hafif–Güzel (11–19 ve Fine, ~88–160×). Varsayılan 5."));
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

static CSoft3DRaceDlg* g_s3r = NULL;

// ---- view ----
IMPLEMENT_DYNAMIC(CS3rView, CCustomStatic)

BEGIN_MESSAGE_MAP(CS3rView, CCustomStatic)
	ON_WM_PAINT()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_RBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_MBUTTONDOWN()
	ON_WM_MBUTTONUP()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

CS3rView::CS3rView()
	: m_ready(FALSE), m_vw(0), m_vh(0), m_dev(NULL), m_imm(NULL), m_swap(NULL), m_bbRtv(NULL)
	, m_dsTex(NULL), m_dsv(NULL), m_dsSrv(NULL), m_sceneTex(NULL), m_sceneRtv(NULL), m_sceneSrv(NULL)
	, m_postTex(NULL), m_postRtv(NULL), m_postSrv(NULL), m_shadowTex(NULL), m_shadowDsv(NULL), m_shadowSrv(NULL)
	, m_rearTex(NULL), m_rearRtv(NULL), m_srvRear(NULL), m_rearDs(NULL), m_rearDsv(NULL)
	, m_reflectTex(NULL), m_reflectRtv(NULL), m_srvReflect(NULL), m_reflectDs(NULL), m_reflectDsv(NULL)
	, m_vsTess(NULL), m_hsTess(NULL), m_dsTess(NULL)
	, m_psBand(NULL), m_vsSolid(NULL), m_vsInst(NULL), m_psSolid(NULL), m_psCloud(NULL), m_psWater(NULL), m_psTerr(NULL), m_psCraft(NULL), m_vsHud(NULL), m_psHud(NULL), m_psHudLine(NULL), m_vsPost(NULL)
	, m_psSsr(NULL), m_psDof(NULL), m_psFinal(NULL), m_csNoise(NULL), m_ilPatch(NULL), m_ilSolid(NULL), m_ilInst(NULL), m_ilHud(NULL)
	, m_cbFrame(NULL), m_vbDyn(NULL)
	, m_vbTerrParts(0), m_vbBandParts(0), m_vbWaterParts(0), m_vbSceneryParts(0)
	, m_vbObs(NULL), m_ibObs(NULL), m_vbObsInst(NULL), m_vbCraft(NULL), m_ibCraft(NULL), m_vbCraftInst(NULL), m_vbHud(NULL)
	, m_vbDynBytes(4*1024*1024), m_vbHudBytes(512*1024)
	, m_obsNvGpu(0), m_obsNiGpu(0), m_obsInstN(0), m_craftNvGpu(0), m_craftNiGpu(0)
	, m_cpuDynScratch(NULL), m_cpuDynScratchBytes(0), m_cpuHudScratch(NULL), m_cpuHudScratchBytes(0)
	, m_cpuBakeScratch(NULL), m_cpuBakeScratchBytes(0)
	, m_texBand(NULL), m_srvBand(NULL), m_texWater(NULL), m_srvWater(NULL), m_texWaterD(NULL), m_srvWaterD(NULL)
	, m_texObs(NULL), m_srvObs(NULL), m_texObsD(NULL), m_srvObsD(NULL)
	, m_texEnv(NULL), m_srvEnv(NULL), m_texEnv2(NULL), m_srvEnv2(NULL)
	, m_texSky(NULL), m_srvSky(NULL), m_texSky2(NULL), m_srvSky2(NULL), m_texSky3(NULL), m_srvSky3(NULL), m_texSky4(NULL), m_srvSky4(NULL)
	, m_texItem(NULL), m_srvItem(NULL), m_texWood(NULL), m_srvWood(NULL)
	, m_texCraft(NULL), m_srvCraft(NULL), m_texCraftD(NULL), m_srvCraftD(NULL)
	, m_texNoise(NULL), m_srvNoise(NULL), m_uavNoise(NULL)
	, m_texClear(NULL), m_srvClear(NULL), m_clearTexW(0), m_clearTexH(0)
	, m_texHud(NULL), m_srvHud(NULL), m_hudTexW(0), m_hudTexH(0)
	, m_texGauge(NULL), m_srvGauge(NULL), m_gaugeTexW(0), m_gaugeTexH(0)
	, m_texStand(NULL), m_srvStand(NULL), m_standTexW(0), m_standTexH(0)
	, m_texBubble(NULL), m_srvBubble(NULL), m_bubbleTexW(0), m_bubbleTexH(0), m_bubbleN(0)
	, m_texItemLab(NULL), m_srvItemLab(NULL), m_itemLabN(0)
	, m_sampLin(NULL), m_sampPoint(NULL), m_sampCmp(NULL)
	, m_rsSolid(NULL), m_rsShadow(NULL), m_rsNoCull(NULL), m_dssWrite(NULL), m_dssRead(NULL), m_dssOff(NULL)
	, m_bsOpaque(NULL), m_bsAlpha(NULL), m_bsAdd(NULL)
	, m_dxFailStage(0), m_dxFailHr(S_OK)
{
	for (int i = 0; i < S3R_THEME_N; i++) {
		m_texTheme[i] = NULL; m_srvTheme[i] = NULL;
		m_texThemeD[i] = NULL; m_srvThemeD[i] = NULL;
	}
	for (int i = 0; i < S3R_MESH_PARTS; i++) {
		m_vbTerr[i] = NULL; m_vbTerrN[i] = 0;
		m_vbBand[i] = NULL; m_vbBandN[i] = 0;
		m_vbWater[i] = NULL; m_vbWaterN[i] = 0;
		m_vbScenery[i] = NULL; m_vbSceneryN[i] = 0;
	}
}
CS3rView::~CS3rView() { ReleaseDx(); }

BOOL CS3rView::CreateShaders()
{
	static const char* hlsl =
		"cbuffer F:register(b0){row_major float4x4 VP;row_major float4x4 LightVP;float4 Eye;float4 Fog;float4 Dof;float4 Screen;float4 Misc;float4 LightDir;float4 Peel;row_major float4x4 ReflectVP;}"
		"Texture2D T0:register(t0);Texture2D T1:register(t1);Texture2D Depth:register(t2);TextureCube Env:register(t3);Texture2D ShadowMap:register(t4);Texture2D NoiseMap:register(t5);Texture2D ReflectMap:register(t6);"
		"SamplerState SL:register(s0);SamplerState SP:register(s1);SamplerComparisonState SCmp:register(s2);"
		"struct V{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};"
		"struct P{float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};"
		"struct D{float4 p:SV_POSITION;float3 w:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float4 c:TEXCOORD3;};"
		"P VST(V x){P o;o.p=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;return o;}"
		"struct HC{float e[4]:SV_TessFactor;float i[2]:SV_InsideTessFactor;};"
		"HC HPC(InputPatch<P,4> p,uint id:SV_PrimitiveID){HC o;float3 c=(p[0].p+p[1].p+p[2].p+p[3].p)*.25;float d=distance(c,Eye.xyz);"
		"float tf=(LightDir.w<.5)?64.:lerp(64.,2.,saturate((d-2.)/18.));tf=clamp(tf,1.,64.);"
		"o.e[0]=o.e[1]=o.e[2]=o.e[3]=tf;o.i[0]=o.i[1]=tf;return o;}"
		"[domain(\"quad\")][partitioning(\"fractional_even\")][outputtopology(\"triangle_cw\")][outputcontrolpoints(4)][patchconstantfunc(\"HPC\")]"
		"P HST(InputPatch<P,4> p,uint i:SV_OutputControlPointID,uint id:SV_PrimitiveID){return p[i];}"
		"float hash(float2 p){return frac(sin(dot(p,float2(12.9898,78.233)))*43758.5453);}"
		"float noise(float2 p){float2 i=floor(p),f=frac(p);float a=hash(i),b=hash(i+float2(1,0)),c=hash(i+float2(0,1)),d=hash(i+float2(1,1));"
		"float2 u=f*f*(3.-2.*f);return lerp(a,b,u.x)+(c-a)*u.y*(1.-u.x)+(d-b)*u.x*u.y;}"
		"float fbm(float2 p){float f=0.,a=0.5;for(int i=0;i<4;i++){f+=a*noise(p);p*=2.;a*=.5;}return f;}"
		"float PeelAmt(float3 w){float rad=abs(Peel.w);if(rad<.05)return 0;float3 e=Eye.xyz;float3 pl=Peel.xyz-e;float lp=length(pl);if(lp<1.1)return 0;"
		"float3 dir=pl/lp;float3 tw=w-e;float along=dot(tw,dir);float dl=length(tw-dir*along);"
		"if(along<0.65||along>lp-0.85||dl>rad)return 0;float ka=saturate((along-0.65)/1.8);float kb=saturate((lp-0.85-along)/2.8);float kr=saturate(1.-dl/rad);return ka*kb*kr*kr;}"
		"[domain(\"quad\")]D DST(HC h,float2 q:SV_DomainLocation,const OutputPatch<P,4> p){"
		"P a,b,o;a.p=lerp(p[0].p,p[1].p,q.x);b.p=lerp(p[3].p,p[2].p,q.x);o.p=lerp(a.p,b.p,q.y);"
		"a.n=lerp(p[0].n,p[1].n,q.x);b.n=lerp(p[3].n,p[2].n,q.x);o.n=normalize(lerp(a.n,b.n,q.y));"
		"a.uv=lerp(p[0].uv,p[1].uv,q.x);b.uv=lerp(p[3].uv,p[2].uv,q.x);o.uv=lerp(a.uv,b.uv,q.y);o.c=p[0].c;"
		"float nse=fbm(o.uv*6.+Misc.w*.02);float bump=(nse-.45);"
		"float d=distance(o.p,Eye.xyz);float amp=lerp(.18,.02,saturate((d-3.)/20.));"
		"o.p+=o.n*bump*amp;float3 nn=normalize(o.n+float3(bump,bump*.5,bump)*.8*amp);"
		"D z;z.w=o.p;z.n=nn;z.uv=o.uv;z.c=o.c;z.p=mul(float4(o.p,1),VP);return z;}"
		"float ShadowAt(float3 w,float3 n){float3 nn=normalize(n);float3 l=normalize(LightDir.xyz);float ndl=saturate(dot(nn,l));"
		"w+=nn*(0.02+(1-ndl)*0.03);float4 sp=mul(float4(w,1),LightVP);float iw=1.0/max(sp.w,1e-5);"
		"float2 uv=sp.xy*iw*float2(.5,-.5)+.5;uv+=float2(2,-2)/1024.;float z=sp.z*iw-0.003;"
		"if(any(uv<0)||any(uv>1)||z<=0||z>=1)return 1;"
		"const float2 o[8]={float2(-.326,-.406),float2(-.84,-.074),float2(-.696,.457),float2(-.203,.621),float2(.962,-.195),float2(.473,-.48),float2(.519,.767),float2(.185,-.893)};"
		"float s=0;const float t=2./1024.;[unroll]for(int k=0;k<8;k++)s+=ShadowMap.SampleCmpLevelZero(SCmp,uv+o[k]*t,z);return pow(saturate(s*.125),1.25);}"
		"float4 PSB(D i):SV_Target{if(Fog.w>0.5&&i.w.y<Fog.z+0.08)discard;float4 a=T0.Sample(SL,i.uv)*i.c;float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);"
		"float nd=lerp(.12,max(saturate(dot(n,l)),.2),sh);float3 v=normalize(Eye.xyz-i.w);float F=0.06+(1.-0.06)*pow(1.-saturate(dot(n,v)),4.2);"
		"float3 env=Env.Sample(SL,reflect(-v,n)).rgb;float pulse=.55+.45*sin(Misc.w*2.+i.uv.x*12.);"
		"float3 col=a.rgb*nd+env*(.06+F*.28)+float3(1,.95,.8)*pow(saturate(dot(reflect(-l,n),v)),48)*sh*.45;"
		"col*=lerp(0.55,1.0,sh);col+=a.rgb*pulse*.12;col=lerp(col,env*1.05,F*.22);"
		"float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);"
		"float al=saturate(.22+a.a*.45+F*.18);float nw=saturate((5.-abs(i.w.y-Fog.z))/5.);al=saturate(lerp(al,max(al,.88),nw));"
		"if(Dof.w>0.5){col+=a.rgb*(.08+.10*pulse)+env*F*.12;al=saturate(al+.06);}"
		"return float4(lerp(col,float3(.55,.72,.95),fg*.55),al);}"
		"D VSS(V x){D o;o.w=x.p;o.n=x.n;o.uv=x.uv;o.c=x.c;o.p=mul(float4(x.p,1),VP);return o;}"
		"D VSSI(V x,float4 iw:TEXCOORD2,float4 isc:TEXCOORD3,float4 ic:TEXCOORD4){"
		"float cy=cos(iw.w),sy=sin(iw.w),cp=cos(isc.w),sp=sin(isc.w);float3 p=x.p*isc.xyz;"
		"float y2=p.y*cp-p.z*sp,z2=p.y*sp+p.z*cp;float3 w=float3(iw.x+p.x*cy+z2*sy,iw.y+y2,iw.z-p.x*sy+z2*cy);"
		"float ny2=x.n.y*cp-x.n.z*sp,nz2=x.n.y*sp+x.n.z*cp;float3 n=float3(x.n.x*cy+nz2*sy,ny2,-x.n.x*sy+nz2*cy);"
		"D o;o.w=w;o.n=n;o.uv=x.uv;o.c=x.c*ic;o.p=mul(float4(w,1),VP);return o;}"
		"struct HV{float2 p:POSITION;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};struct HO{float4 p:SV_POSITION;float2 uv:TEXCOORD0;float4 c:TEXCOORD1;};"
		"HO VSH(HV x){HO o;o.p=float4(x.p,0,1);o.uv=x.uv;o.c=x.c;return o;}"
		"float4 PSH(HO i):SV_Target{if(i.uv.x<-0.5)return i.c;float4 t=T0.Sample(SL,i.uv);return float4(t.rgb*i.c.rgb,t.a*i.c.a);}"
		"float4 PSLINE(HO i):SV_Target{float t=saturate(1.-abs(i.uv.y-.5)*2.4);float cap=saturate(min(i.uv.x,1.-i.uv.x)*10.);return float4(i.c.rgb,i.c.a*t*cap);}"
		"float4 PSS(D i):SV_Target{if(Fog.w>0.5&&i.w.y<Fog.z+0.08)discard;float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);"
		"float nz=fbm(i.w.xz*12.+i.w.y*10.+Misc.w*.02)*0.15;"
		"n=normalize(n+float3(nz,nz*0.5,nz)*0.8);"
		"float3 v=normalize(Eye.xyz-i.w);float ndl=saturate(dot(n,l));float wrap=saturate(dot(n,l)*.5+.5);"
		"float nd=lerp(.30,max(ndl,.22),sh);nd=saturate(nd*.62+wrap*wrap*.48);"
		"float uvk=saturate((0.96-i.c.a)*18.);"
		"float4 tex4=T0.Sample(SL,lerp(i.w.xz*0.0048+i.w.y*0.002,i.uv,uvk));float3 tex=tex4.rgb;"
		"float3 det=T1.Sample(SL,lerp(i.w.xz*0.021+i.w.y*0.014,i.uv*2.,uvk)).rgb;"
		"float3 photo=saturate(tex*lerp(float3(1,1,1),det*1.38,0.52*(1.-uvk)));"
		"float3 base=lerp(i.c.rgb,photo,lerp(0.58,0.90,uvk));"
		"float sp=pow(saturate(dot(reflect(-l,n),v)),56)*sh;float3 env=Env.Sample(SL,reflect(-v,n)).rgb;"
		"float fr=pow(1-saturate(dot(n,v)),2.2);float ndc=lerp(nd,saturate(.74+wrap*.28),uvk);"
		"float3 c=base*ndc+env*(.12+fr*.28)*(1.-uvk*.7)+float3(1,.96,.88)*sp*.42*(1.-uvk);"
		"float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);"
		"float al=saturate(i.c.a*lerp(1,tex4.a*tex4.a,uvk));if(al<0.08&&uvk>0.5)discard;"
		"float pe=PeelAmt(i.w);if(Peel.w>=0){if(pe>0.16)discard;return float4(lerp(c,float3(.55,.7,.92),fg*.45),al);}"
		"if(pe<0.16)discard;return float4(lerp(c,float3(.55,.7,.92),fg*.45),al*(1.-pe*0.86));}"
		"float4 PSCLoud(D i):SV_Target{"
		"float4 t0=T0.Sample(SL,i.uv);float2 j=float2(.018,-.014)*(0.55+0.45*sin(Misc.w*.7+i.uv.x*6.));"
		"float4 t1=T0.Sample(SL,saturate(i.uv+j));float dens=saturate(max(t0.a,t1.a*0.85)*i.c.a);if(dens<0.08)discard;"
		"float3 albedo=saturate((t0.rgb*0.72+t1.rgb*0.28)*i.c.rgb);"
		"float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float3 v=normalize(Eye.xyz-i.w);"
		"float nl=saturate(dot(n,l)*.55+.45);float nv=saturate(dot(n,v));"
		"float thin=dens*dens*(1.15-dens);float powder=pow(thin,1.35);"
		"float silver=pow(saturate(1.-nv),2.4)*pow(saturate(dot(v,l)*.5+.5),3.5)*dens;"
		"float3 sunC=float3(1.05,.93,.78);float3 skyC=float3(.52,.68,.95);"
		"float3 amb=lerp(skyC,sunC,nl*.55+.25);"
		"float3 c=albedo*amb*(.55+.55*nl);"
		"c+=sunC*powder*1.55*(.4+nl*.6);c+=sunC*silver*1.1;"
		"c*=lerp(float3(.88,.92,1.06),float3(1.06,.98,.90),saturate((i.w.y-Eye.y)*.015+.5));"
		"float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);"
		"float al=saturate(dens*1.05);return float4(lerp(c,float3(.55,.7,.92),fg*.32),al);}"
		"float4 PlanarMir(float3 w){float4 rp=mul(float4(w,1),ReflectVP);float iw=max(rp.w,1e-5);float2 muv=rp.xy/iw*float2(.5,-.5)+.5;"
		"float mb=(rp.w>0)*saturate(min(min(muv.x,1.-muv.x),min(muv.y,1.-muv.y))*8.);"
		"return float4(ReflectMap.Sample(SL,saturate(muv)).rgb,mb);}"
		"float4 PSW(D i):SV_Target{float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);"
		"float2 wp=i.w.xz*0.085+Misc.w*float2(.031,-.024);float w1=fbm(wp),w2=fbm(wp.yx*1.7+float2(Misc.w*.05,0));"
		"n=normalize(n+float3((w1-.5)*1.15+(w2-.5)*.45,0,(w2-.5)*1.05+(w1-.5)*.35)*.55);"
		"float sh=ShadowAt(i.w,n);float3 v=normalize(Eye.xyz-i.w);float ndl=saturate(dot(n,l));float wrap=saturate(dot(n,l)*.5+.5);"
		"float nd=lerp(.32,max(ndl,.26),sh);nd=saturate(nd*.52+wrap*wrap*.52);"
		"float3 tex=T0.Sample(SL,i.w.xz*0.0045+Misc.w*0.001+n.xz*0.012).rgb;"
		"float3 det=T1.Sample(SL,i.w.xz*0.018+n.xz*0.04).rgb;"
		"float3 deep=float3(.08,.22,.32);float3 shallow=float3(.32,.58,.62);"
		"float depthK=saturate((Fog.z+2.5-i.w.y)*.18);float3 waterAlb=lerp(shallow,deep,depthK);"
		"float3 base=saturate(tex*lerp(float3(1,1,1),det*1.25,0.42))*waterAlb;"
		"float3 env=Env.Sample(SL,reflect(-v,n)).rgb;"
		"float4 mir=PlanarMir(i.w+float3(n.x,0,n.z)*6.5);"
		"float4 mirB=PlanarMir(i.w+float3(n.z,-0.05,-n.x)*3.2);if(mirB.a>mir.a*.85)mir=lerp(mir,mirB,.45);"
		"float ndv=saturate(dot(n,v));float F=0.02+0.98*pow(1.-ndv,5.);"
		"float mk=mir.a*saturate(0.12+F*0.92);"
		"float3 c=base*nd*(1.-F*.55)+env*(.06+F*.18);"
		"c=lerp(c,mir.rgb,mk*F);"
		"float glitter=pow(saturate(dot(reflect(-l,n),v)),90)*sh;"
		"c+=float3(.75,.9,1.)*glitter*(.22+F*.45);"
		"c+=float3(.55,.75,.9)*pow(saturate(dot(reflect(-l,n),v)),28)*sh*.18;"
		"float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);"
		"return float4(lerp(c,float3(.42,.60,.80),fg*.28),saturate(0.62+F*.34));}"
		"float4 PST(D i):SV_Target{if(Fog.w>0.5&&i.w.y<Fog.z+0.08)discard;float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);"
		"float slope=saturate(1.-n.y);float h=i.w.y;float th=Eye.w;"
		"float nLo=fbm(i.w.xz*0.028), nHi=fbm(i.w.xz*0.11);"
		"n=normalize(n+float3(nHi-.45,0,nLo-.45)*slope*0.55);"
		"float3 v=normalize(Eye.xyz-i.w);float ndl=saturate(dot(n,l));float wrap=saturate(dot(n,l)*.5+.5);"
		"float nd=lerp(.28,max(ndl,.18),sh);nd=saturate(nd*.60+wrap*wrap*.50);"
		"float3 tex=T0.Sample(SL,i.w.xz*0.0038).rgb;"
		"float3 det=T1.Sample(SL,i.w.xz*0.018).rgb;"
		"float3 photo=saturate(tex*lerp(float3(1,1,1),det*1.32,0.48));"
		"float3 dirt=lerp(i.c.rgb,photo,0.72);"
		"float3 rock=dirt*lerp(float3(.62,.58,.52),float3(.48,.42,.36),saturate(slope*1.4));"
		"float3 wet=lerp(dirt,float3(.16,.26,.34),0.72);"
		"if(th<0.5){rock=float3(.36,.30,.20);wet=float3(.10,.22,.16);}"
		"else if(th<1.5){rock=float3(.50,.46,.40);wet=float3(.28,.26,.24);}"
		"else if(th<2.5){rock=float3(.30,.32,.34);wet=float3(.14,.16,.18);}"
		"else if(th<3.5){rock=float3(.22,.24,.34);wet=float3(.08,.10,.18);}"
		"else if(th<4.5){rock=float3(.20,.38,.48);wet=float3(.08,.22,.36);}"
		"else if(th<5.5){rock=float3(.42,.36,.22);wet=float3(.12,.28,.18);}"
		"else if(th<6.5){rock=float3(.62,.38,.24);wet=float3(.28,.18,.12);}"
		"else {rock=float3(.78,.80,.88);wet=float3(.55,.62,.78);}"
		"float3 base=lerp(dirt,rock,saturate(slope*2.5+nHi*0.18));"
		"float waterL=Fog.z;float low=saturate((waterL+6.-h)/10.);"
		"base=lerp(base,wet,low*(1.-slope)*0.88);"
		"if(th>6.5) base=lerp(base,float3(.93,.95,1),saturate((h-24.)/18.)*0.55);"
		"if(th>5.5&&th<6.6) base=lerp(base,float3(.95,.72,.48),saturate((h-28.)/22.)*0.35);"
		"float sp=pow(saturate(dot(reflect(-l,n),v)),40)*sh*lerp(.08,.28,slope);"
		"float3 env=Env.Sample(SL,reflect(-v,n)).rgb;float fr=pow(1-saturate(dot(n,v)),2.4);"
		"float hemi=saturate(n.y*.5+.5);float3 hemiC=lerp(float3(.55,.48,.42),float3(.72,.82,.98),hemi);"
		"float3 c=base*nd*lerp(float3(1,1,1),hemiC,0.22)+env*(.10+fr*.26)+float3(1,.97,.9)*sp;"
		"float specW=saturate(low*(1.-slope)*0.65);c+=float3(.25,.4,.5)*pow(saturate(dot(reflect(-l,n),v)),24)*specW*sh;"
		"float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);"
		"float hFog=saturate((Fog.z+18.-h)/28.);c=lerp(c,float3(.62,.74,.92),hFog*fg*.25);"
		"float pe=PeelAmt(i.w);if(Peel.w>=0){if(pe>0.16)discard;return float4(lerp(c,float3(.55,.7,.92),fg*.42),1);}"
		"if(pe<0.16)discard;return float4(lerp(c,float3(.55,.7,.92),fg*.42),saturate(1.-pe*0.86));}"
		"float4 PSC(D i):SV_Target{if(Fog.w>0.5&&i.w.y<Fog.z+0.08)discard;float3 n=normalize(i.n);float3 l=normalize(LightDir.xyz);float sh=ShadowAt(i.w,n);"
		"float3 v=normalize(Eye.xyz-i.w);"
		"float3 albedo=T0.Sample(SL,i.uv).rgb;"
		"float3 det=T1.Sample(SL,i.uv*4.6).rgb;"
		"albedo=saturate(albedo*lerp(float3(1,1,1),det*1.28,0.4));"
		"float nz=(albedo.r-0.5)*0.2 + fbm(i.w.xz*12.+i.w.y*10.+Misc.w*.02)*0.15;"
		"n=normalize(n+float3(nz,nz,nz));"
		"float nd=lerp(.48,max(saturate(dot(n,l)),.34),sh);nd=saturate(nd*.7+saturate(dot(n,l)*.5+.5)*saturate(dot(n,l)*.5+.5)*.4);"
		"float3 base=saturate(i.c.rgb*albedo*1.12);"
		"float3 env=Env.Sample(SL,reflect(-v,n)).rgb;float fr=pow(1-saturate(dot(n,v)),2.4);"
		"float sp=pow(saturate(dot(reflect(-l,n),v)),52)*sh;"
		"float3 c=base*(0.38+0.62*nd)+env*(.16+fr*.34)+float3(1,.96,.9)*sp*.62;"
		"float d=length(Eye.xyz-i.w),fg=saturate((d-Fog.x)/max(.01,Fog.y-Fog.x));fg=fg*fg*(3-2*fg);"
		"return float4(lerp(c,float3(.55,.7,.92),fg*.3),saturate(i.c.a));}"
		"struct Q{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};Q VSQ(uint id:SV_VertexID){Q o;float2 p=float2((id==2)?3:-1,(id==1)?3:-1);o.p=float4(p,0,1);o.uv=float2((p.x+1)*.5,(1-p.y)*.5);return o;}"
		"float4 SSR(Q i):SV_Target{return T0.Sample(SL,i.uv);}"
		"float4 DOFP(Q i):SV_Target{float zd=Depth.Sample(SP,i.uv).r;const float zn=.08,zf=220.;"
		"float eyeZ=zn*zf/max(1e-4,zf-zd*(zf-zn));float coc=saturate((eyeZ-Dof.x)/max(.05,Dof.y));coc=coc*coc*(3.-2.*coc);"
		"float b=coc*Dof.z*(1.+Dof.w);if(b<1.15)return T0.Sample(SL,i.uv);float2 px=float2(b,b)*Screen.zw;"
		"float4 c=T0.Sample(SL,i.uv)*.28;c+=(T0.Sample(SL,i.uv+float2(px.x,0))+T0.Sample(SL,i.uv-float2(px.x,0))+T0.Sample(SL,i.uv+float2(0,px.y))+T0.Sample(SL,i.uv-float2(0,px.y)))*.13;"
		"c+=(T0.Sample(SL,i.uv+px)+T0.Sample(SL,i.uv-px)+T0.Sample(SL,i.uv+float2(px.x,-px.y))+T0.Sample(SL,i.uv+float2(-px.x,px.y)))*.05;return c;}"
		"float4 FIN(Q i):SV_Target{float4 c=T0.Sample(SL,i.uv);if(Misc.z>8.f){c.a*=saturate(Misc.y);return c;}"
		"float v=saturate(1-dot((i.uv-.5)*1.1,(i.uv-.5)*1.1));c.rgb*=lerp(.86,1.08,v);"
		"float th=Eye.w;float3 tone=float3(1.02,.98,.96);if(th>6.5)tone=float3(1.05,.92,.85);else if(th>5.5)tone=float3(.9,1.02,1.08);"
		"else if(th>4.5)tone=float3(.88,.92,1.08);else if(th>3.5)tone=float3(.95,.95,1.02);else if(th>2.5)tone=float3(1.0,.9,.85);"
		"else if(th>1.5)tone=float3(.95,1.02,.92);c.rgb=saturate(c.rgb*tone);"
		"float3 x=max(c.rgb,0);c.rgb=saturate((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14));"
		"float lum=dot(c.rgb,float3(.299,.587,.114));c.rgb+=c.rgb*saturate(lum-.72)*.28;"
		"c.rgb=lerp(c.rgb,c.rgb*c.rgb*(3.-2.*c.rgb),0.08);"
		"if(Misc.x>0.01)c.rgb=lerp(c.rgb,1,Misc.x*.35);return c;}"
		"RWTexture2D<float4> NoiseOut:register(u0);"
		"[numthreads(8,8,1)]void CSNoise(uint3 id:SV_DispatchThreadID){"
		"uint2 p=id.xy;if(p.x>=64||p.y>=64)return;float2 uv=(float2(p)+.5)/64.;"
		"float n=fbm(uv*6.+Misc.w*0.2);"
		"float n2=fbm(uv*12.-Misc.w*0.3);"
		"float d=length(uv-.5);NoiseOut[p]=float4(n,n2,saturate(1.-d*1.4),1);}";

	ID3DBlob *b[12]={0}, *err=NULL;
	const char* entries[12]={"VST","HST","DST","PSB","VSS","PSS","VSH","PSH","VSQ","SSR","DOFP","FIN"};
	const char* profiles[12]={"vs_5_0","hs_5_0","ds_5_0","ps_5_0","vs_5_0","ps_5_0","vs_5_0","ps_5_0","vs_5_0","ps_5_0","ps_5_0","ps_5_0"};
	for(int i=0;i<12;i++){
		if(FAILED(D3DCompile(hlsl,(SIZE_T)strlen(hlsl),NULL,NULL,NULL,entries[i],profiles[i],D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b[i],&err))){
			if (err) {
				OutputDebugStringA((const char*)err->GetBufferPointer());
				OutputDebugStringA("\n");
			}
			m_dxFailHr = E_FAIL;
			S3R_RELEASE(err); for(int j=0;j<12;j++) S3R_RELEASE(b[j]); return FALSE;
		}
		S3R_RELEASE(err);
	}
	ID3DBlob* bcs=NULL;
	if(FAILED(D3DCompile(hlsl,(SIZE_T)strlen(hlsl),NULL,NULL,NULL,"CSNoise","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bcs,&err))){
		m_dxFailHr = E_FAIL;
		for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(err); return FALSE;
	}
	ID3DBlob* bcraft=NULL;
	if(FAILED(D3DCompile(hlsl,(SIZE_T)strlen(hlsl),NULL,NULL,NULL,"PSC","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bcraft,&err))){
		m_dxFailHr = E_FAIL;
		for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(bcs); S3R_RELEASE(err); return FALSE;
	}
	ID3DBlob* bline=NULL;
	if(FAILED(D3DCompile(hlsl,(SIZE_T)strlen(hlsl),NULL,NULL,NULL,"PSLINE","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bline,&err))){
		m_dxFailHr = E_FAIL;
		for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(bcs); S3R_RELEASE(bcraft); S3R_RELEASE(err); return FALSE;
	}
	S3R_RELEASE(err);
	ID3DBlob* bterr=NULL;
	if(FAILED(D3DCompile(hlsl,(SIZE_T)strlen(hlsl),NULL,NULL,NULL,"PST","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bterr,&err))){
		m_dxFailHr = E_FAIL;
		for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(bcs); S3R_RELEASE(bcraft); S3R_RELEASE(bline); S3R_RELEASE(err); return FALSE;
	}
	S3R_RELEASE(err);
	ID3DBlob* binst=NULL;
	if(FAILED(D3DCompile(hlsl,(SIZE_T)strlen(hlsl),NULL,NULL,NULL,"VSSI","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binst,&err))){
		m_dxFailHr = E_FAIL;
		for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(bcs); S3R_RELEASE(bcraft); S3R_RELEASE(bline); S3R_RELEASE(bterr); S3R_RELEASE(err); return FALSE;
	}
	S3R_RELEASE(err);
	ID3DBlob* bwater=NULL;
	if(FAILED(D3DCompile(hlsl,(SIZE_T)strlen(hlsl),NULL,NULL,NULL,"PSW","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bwater,&err))){
		m_dxFailHr = E_FAIL;
		for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(bcs); S3R_RELEASE(bcraft); S3R_RELEASE(bline); S3R_RELEASE(bterr); S3R_RELEASE(binst); S3R_RELEASE(err); return FALSE;
	}
	S3R_RELEASE(err);
	ID3DBlob* bcloud=NULL;
	if(FAILED(D3DCompile(hlsl,(SIZE_T)strlen(hlsl),NULL,NULL,NULL,"PSCLoud","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bcloud,&err))){
		m_dxFailHr = E_FAIL;
		for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(bcs); S3R_RELEASE(bcraft); S3R_RELEASE(bline); S3R_RELEASE(bterr); S3R_RELEASE(binst); S3R_RELEASE(bwater); S3R_RELEASE(err); return FALSE;
	}
	S3R_RELEASE(err);
	HRESULT hr=S_OK;
	if(FAILED(hr=m_dev->CreateVertexShader(b[0]->GetBufferPointer(),b[0]->GetBufferSize(),NULL,&m_vsTess))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateHullShader(b[1]->GetBufferPointer(),b[1]->GetBufferSize(),NULL,&m_hsTess))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateDomainShader(b[2]->GetBufferPointer(),b[2]->GetBufferSize(),NULL,&m_dsTess))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[3]->GetBufferPointer(),b[3]->GetBufferSize(),NULL,&m_psBand))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateVertexShader(b[4]->GetBufferPointer(),b[4]->GetBufferSize(),NULL,&m_vsSolid))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateVertexShader(binst->GetBufferPointer(),binst->GetBufferSize(),NULL,&m_vsInst))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[5]->GetBufferPointer(),b[5]->GetBufferSize(),NULL,&m_psSolid))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(bcloud->GetBufferPointer(),bcloud->GetBufferSize(),NULL,&m_psCloud))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(bwater->GetBufferPointer(),bwater->GetBufferSize(),NULL,&m_psWater))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(bcraft->GetBufferPointer(),bcraft->GetBufferSize(),NULL,&m_psCraft))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateVertexShader(b[6]->GetBufferPointer(),b[6]->GetBufferSize(),NULL,&m_vsHud))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[7]->GetBufferPointer(),b[7]->GetBufferSize(),NULL,&m_psHud))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(bline->GetBufferPointer(),bline->GetBufferSize(),NULL,&m_psHudLine))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(bterr->GetBufferPointer(),bterr->GetBufferSize(),NULL,&m_psTerr))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateVertexShader(b[8]->GetBufferPointer(),b[8]->GetBufferSize(),NULL,&m_vsPost))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[9]->GetBufferPointer(),b[9]->GetBufferSize(),NULL,&m_psSsr))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[10]->GetBufferPointer(),b[10]->GetBufferSize(),NULL,&m_psDof))) goto fail_sh;
	if(FAILED(hr=m_dev->CreatePixelShader(b[11]->GetBufferPointer(),b[11]->GetBufferSize(),NULL,&m_psFinal))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateComputeShader(bcs->GetBufferPointer(),bcs->GetBufferSize(),NULL,&m_csNoise))) goto fail_sh;
	D3D11_INPUT_ELEMENT_DESC il[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
	D3D11_INPUT_ELEMENT_DESC ih[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
	D3D11_INPUT_ELEMENT_DESC ii[]={
		{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
		{"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,1,0,D3D11_INPUT_PER_INSTANCE_DATA,1},
		{"TEXCOORD",3,DXGI_FORMAT_R32G32B32A32_FLOAT,1,16,D3D11_INPUT_PER_INSTANCE_DATA,1},
		{"TEXCOORD",4,DXGI_FORMAT_R32G32B32A32_FLOAT,1,32,D3D11_INPUT_PER_INSTANCE_DATA,1}};
	if(FAILED(hr=m_dev->CreateInputLayout(il,4,b[0]->GetBufferPointer(),b[0]->GetBufferSize(),&m_ilPatch))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateInputLayout(il,4,b[4]->GetBufferPointer(),b[4]->GetBufferSize(),&m_ilSolid))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateInputLayout(ii,7,binst->GetBufferPointer(),binst->GetBufferSize(),&m_ilInst))) goto fail_sh;
	if(FAILED(hr=m_dev->CreateInputLayout(ih,3,b[6]->GetBufferPointer(),b[6]->GetBufferSize(),&m_ilHud))) goto fail_sh;
	for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(bcs); S3R_RELEASE(bcraft); S3R_RELEASE(bline); S3R_RELEASE(bterr); S3R_RELEASE(binst); S3R_RELEASE(bwater); S3R_RELEASE(bcloud);
	return TRUE;
fail_sh:
	m_dxFailHr = hr;
	for(int i=0;i<12;i++) S3R_RELEASE(b[i]); S3R_RELEASE(bcs); S3R_RELEASE(bcraft); S3R_RELEASE(bline); S3R_RELEASE(bterr); S3R_RELEASE(binst); S3R_RELEASE(bwater); S3R_RELEASE(bcloud);
	return FALSE;
}

BOOL CS3rView::CreateProcTextures()
{
	auto hash=[&](int x,int y,int s)->int{return ((x*73856093)^(y*19349663)^(s*83492791))&255;};
	auto noise = [&](float x, float y, int s) -> float {
		int ix = (int)floorf(x); float fx = x - ix;
		int iy = (int)floorf(y); float fy = y - iy;
		float u = fx * fx * (3.f - 2.f * fx);
		float v = fy * fy * (3.f - 2.f * fy);
		auto h = [&](int i, int j) { return (float)hash(i, j, s) * (1.f / 255.f); };
		float n00 = h(ix, iy), n10 = h(ix + 1, iy);
		float n01 = h(ix, iy + 1), n11 = h(ix + 1, iy + 1);
		float nx0 = n00 + u * (n10 - n00);
		float nx1 = n01 + u * (n11 - n01);
		return nx0 + v * (nx1 - nx0);
	};
	auto fbm = [&](float x, float y, int s, int oct) -> float {
		float val = 0.f; float amp = 1.f; float maxAmp = 0.f;
		for (int i = 0; i < oct; i++) {
			val += noise(x, y, s + i) * amp;
			maxAmp += amp;
			amp *= 0.5f;
			x *= 2.f; y *= 2.f;
		}
		return val / maxAmp;
	};
	// 低周波のみ（画素単位ノイズは機体/地形の「UV破綻」に見える）
	auto softN=[&](int x,int y,int s)->int{
		return (int)(fbm((float)x * 0.1f, (float)y * 0.1f, s, 3) * 255.f) - 128;
	};
	const int W=512,H=512;
	DWORD* pix=new (std::nothrow) DWORD[W*H];
	if(!pix) return FALSE;
	D3D11_TEXTURE2D_DESC d={}; d.Width=W;d.Height=H;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
	static const int kThemeRes[S3R_THEME_N]={
		IDR_S3TEX_R_FOREST,IDR_S3TEX_R_RUINS,IDR_S3TEX_R_OIL,IDR_S3TEX_R_NIGHT,
		IDR_S3TEX_R_UNDER,IDR_S3TEX_R_GRASS,IDR_S3TEX_R_MESA,IDR_S3TEX_R_CLOUD
	};
	for(int th=0;th<S3R_THEME_N;th++){
		if(!Soft3DTexLoadPngRes(kThemeRes[th],pix,W,H)){
		for(int y=0;y<H;y++)for(int x=0;x<W;x++){
			int n=softN(x,y,th+3); BYTE r,g,b,a=255;
			if(th==0){ // forest
				float f1=(float)((hash(x/8,y/8,1)&255)/255.f), f2=(float)((hash(x/14,y/14,2)&255)/255.f);
				float f3=(float)((hash(x/28,y/28,3)&255)/255.f);
				r=(BYTE)(48+n/14+(int)(f2*28)); g=(BYTE)(118+n/8+(int)(f1*55)); b=(BYTE)(42+n/16+(int)(f3*20));
				if(((hash(x/10,y/10,4)&31)==0)){r=78;g=150;b=60;}
				if(((hash(x/16,y/16,5)&63)==0)){r=220;g=160;b=190;a=230;}
			}else if(th==1){ // ruins
				r=(BYTE)(148+n/10); g=(BYTE)(138+n/11); b=(BYTE)(118+n/12);
				if(((x&63)<3)||((y&63)<3)){r=118;g=108;b=98;}
				if(((hash(x/12,y/12,2)&31)==0)){r=170;g=185;b=125;}
			}else if(th==2){ // oil factory
				r=(BYTE)(72+n/12); g=(BYTE)(76+n/12); b=(BYTE)(82+n/12);
				if(((x&31)<2)||((y&31)<2)){r=48;g=48;b=52;}
				if(((hash(x/8,y/8,4)&31)==0)){r=230;g=150;b=50;}
			}else if(th==3){ // night city
				r=(BYTE)(32+n/16); g=(BYTE)(36+n/14); b=(BYTE)(72+n/10);
				if(((x&15)==0)||((y&15)==0)){r=22;g=26;b=52;}
				if(((hash(x/8,y/8,5)&15)==0)){r=240;g=210;b=130;}
			}else if(th==4){ // underwater
				r=(BYTE)(34+n/14); g=(BYTE)(92+n/10); b=(BYTE)(142+n/7);
				if(((hash(x/12,y/12,6)&31)==0)){r=230;g=130;b=175;a=210;}
				a=(BYTE)(190+abs(n)/6);
			}else if(th==5){ // grassland
				r=(BYTE)(92+n/10); g=(BYTE)(175+n/7); b=(BYTE)(72+n/12);
				if(((hash(x/14,y/14,7)&15)==0)){r=240;g=220;b=100;}
			}else if(th==6){ // sunset mesa
				r=(BYTE)(198+n/10); g=(BYTE)(112+n/12); b=(BYTE)(72+n/14);
				if(((hash(x/12,y/12,10)&31)==0)){r=165;g=85;b=55;}
			}else{ // cloud garden
				r=(BYTE)(222+n/16); g=(BYTE)(232+n/16); b=(BYTE)(250);
				if(((hash(x/18,y/18,9)&7)==0)){r=245;g=195;b=225;}
				a=(BYTE)(210+abs(n)/7);
			}
			pix[y*W+x]=((DWORD)a<<24)|((DWORD)r<<16)|((DWORD)g<<8)|b;
		}
		}
		D3D11_SUBRESOURCE_DATA sd={pix,W*4,0};
		if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texTheme[th]))||FAILED(m_dev->CreateShaderResourceView(m_texTheme[th],NULL,&m_srvTheme[th]))){delete[] pix;return FALSE;}
	}
	// 機体専用スキン（メッシュ UV 用）— パネル／ストライプ／キャノピ用グラデ
	if(!Soft3DTexLoadPngRes(IDR_S3TEX_R_CRAFT,pix,W,H)){
	for(int y=0;y<H;y++)for(int x=0;x<W;x++){
		float u=(float)x/(W-1), v=(float)y/(H-1);
		float panel = 0.88f + 0.08f*sinf(u*18.f) + 0.06f*sinf(v*10.f);
		float seamU = (fmodf(u*6.f,1.f)<0.04f) ? 0.72f : 1.f;
		float seamV = (fmodf(v*4.f,1.f)<0.045f) ? 0.78f : 1.f;
		float nose = 1.f - 0.22f*S3rSaturate((v-0.78f)/0.22f);
		float belly = 1.f + 0.12f*(1.f-v);
		float wingStripe = (v>0.35f && v<0.55f && (u<0.18f || u>0.82f)) ? 1.18f : 1.f;
		float gloss = 0.92f + 0.1f*powf(sinf((u+v)*3.14159f),2.f);
		float m = panel*seamU*seamV*nose*belly*wingStripe*gloss;
		BYTE r=(BYTE)S3rClamp(210*m, 40, 255);
		BYTE g=(BYTE)S3rClamp(215*m, 40, 255);
		BYTE b=(BYTE)S3rClamp(230*m, 50, 255);
		// キャノピ帯（高 v・中央 u は青みがかった半ツヤ）
		if (v>0.55f && v<0.92f && u>0.28f && u<0.72f) {
			r=(BYTE)S3rClamp(120+40*m,0,255); g=(BYTE)S3rClamp(170+50*m,0,255); b=(BYTE)S3rClamp(220+30*m,0,255);
		}
		pix[y*W+x]=0xff000000|((DWORD)r<<16)|((DWORD)g<<8)|b;
	}
	}
	{D3D11_SUBRESOURCE_DATA sd={pix,W*4,0}; if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texCraft))||FAILED(m_dev->CreateShaderResourceView(m_texCraft,NULL,&m_srvCraft))){delete[] pix;return FALSE;}}
	// power band ribbon texture (glowing pastel)
	if(!Soft3DTexLoadPngRes(IDR_S3TEX_R_BAND,pix,W,H)){
	for(int y=0;y<H;y++)for(int x=0;x<W;x++){
		float u=(float)x/(W-1), v=(float)y/(H-1);
		float edge=powf(1.f-fabsf(v-.5f)*2.f,1.6f);
		float pulse=.55f+.45f*sinf(u*40.f);
		BYTE r=(BYTE)S3rClamp(140+90*edge*pulse,0,255);
		BYTE g=(BYTE)S3rClamp(200+55*edge,0,255);
		BYTE b=(BYTE)S3rClamp(255,0,255);
		BYTE a=(BYTE)S3rClamp(90+140*edge,0,255);
		pix[y*W+x]=((DWORD)a<<24)|((DWORD)r<<16)|((DWORD)g<<8)|b;
	}
	}
	{D3D11_SUBRESOURCE_DATA sd={pix,W*4,0}; if(FAILED(m_dev->CreateTexture2D(&d,&sd,&m_texBand))||FAILED(m_dev->CreateShaderResourceView(m_texBand,NULL,&m_srvBand))){delete[] pix;return FALSE;}}
	auto upload2d=[&](ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv)->BOOL{
		D3D11_SUBRESOURCE_DATA sd={pix,W*4,0};
		return SUCCEEDED(m_dev->CreateTexture2D(&d,&sd,tex)) && SUCCEEDED(m_dev->CreateShaderResourceView(*tex,NULL,srv));
	};
	static const int kThemeDetRes[S3R_THEME_N]={
		IDR_S3TEX_R_FOREST_D,IDR_S3TEX_R_RUINS_D,IDR_S3TEX_R_OIL_D,IDR_S3TEX_R_NIGHT_D,
		IDR_S3TEX_R_UNDER_D,IDR_S3TEX_R_GRASS_D,IDR_S3TEX_R_MESA_D,IDR_S3TEX_R_CLOUD_D
	};
	for(int th=0;th<S3R_THEME_N;th++){
		if(Soft3DTexLoadPngRes(kThemeDetRes[th],pix,W,H)){
			if(!upload2d(&m_texThemeD[th],&m_srvThemeD[th])){delete[] pix;return FALSE;}
		}
	}
	if(Soft3DTexLoadPngRes(IDR_S3TEX_R_CRAFT_D,pix,W,H)){
		if(!upload2d(&m_texCraftD,&m_srvCraftD)){delete[] pix;return FALSE;}
	}
	if(Soft3DTexLoadPngRes(IDR_S3TEX_R_WATER,pix,W,H)){
		if(!upload2d(&m_texWater,&m_srvWater)){delete[] pix;return FALSE;}
	}
	if(Soft3DTexLoadPngRes(IDR_S3TEX_R_WATER_D,pix,W,H)){
		if(!upload2d(&m_texWaterD,&m_srvWaterD)){delete[] pix;return FALSE;}
	}
	if(Soft3DTexLoadPngRes(IDR_S3TEX_R_OBS,pix,W,H)){
		if(!upload2d(&m_texObs,&m_srvObs)){delete[] pix;return FALSE;}
	}
	if(Soft3DTexLoadPngRes(IDR_S3TEX_R_OBS_D,pix,W,H)){
		if(!upload2d(&m_texObsD,&m_srvObsD)){delete[] pix;return FALSE;}
	}
	// env cube: stacked +X -X +Y -Y +Z -Z photo strip, else procedural sky
	d.ArraySize=6; d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
	DWORD* cube=new (std::nothrow) DWORD[W*H*6];
	if(!cube){delete[] pix;return FALSE;}
	D3D11_SUBRESOURCE_DATA cs[6]={};
	BOOL envPhoto=Soft3DTexLoadPngRes(IDR_S3TEX_R_ENV,cube,W,H*6);
	if(!envPhoto){
		for(int f=0;f<6;f++){
			for(int y=0;y<H;y++)for(int x=0;x<W;x++){
				float t=(float)y/(H-1);
				BYTE r=(BYTE)(140+70*(1-t)), g=(BYTE)(180+50*(1-t)), b=(BYTE)(220+30*(1-t));
				if(f==2){r=(BYTE)(90+40*t);g=(BYTE)(150+40*t);b=(BYTE)(80+20*t);}
				if(f==3){r=(BYTE)(200+30*(1-t));g=(BYTE)(140+20*(1-t));b=(BYTE)(160+40*(1-t));}
				cube[(f*H+y)*W+x]=0xff000000|((DWORD)r<<16)|((DWORD)g<<8)|b;
			}
		}
	}
	for(int f=0;f<6;f++){ cs[f].pSysMem=cube+f*W*H; cs[f].SysMemPitch=W*4; }
	if(FAILED(m_dev->CreateTexture2D(&d,cs,&m_texEnv))||FAILED(m_dev->CreateShaderResourceView(m_texEnv,NULL,&m_srvEnv))){delete[] cube;delete[] pix;return FALSE;}
	{
		DWORD* cube2=new (std::nothrow) DWORD[W*H*6];
		if(cube2 && Soft3DTexLoadPngRes(IDR_S3TEX_R_ENV2,cube2,W,H*6)){
			D3D11_SUBRESOURCE_DATA cs2[6]={};
			for(int f=0;f<6;f++){cs2[f].pSysMem=cube2+f*W*H;cs2[f].SysMemPitch=W*4;}
			if(FAILED(m_dev->CreateTexture2D(&d,cs2,&m_texEnv2))||FAILED(m_dev->CreateShaderResourceView(m_texEnv2,NULL,&m_srvEnv2))){delete[] cube2;delete[] cube;delete[] pix;return FALSE;}
		}
		delete[] cube2;
	}
	delete[] cube;
	d.ArraySize=1; d.MiscFlags=0;
	if(Soft3DTexLoadPngRes(IDR_S3TEX_R_SKY,pix,W,H)){ if(!upload2d(&m_texSky,&m_srvSky)){delete[] pix;return FALSE;} }
	if(Soft3DTexLoadPngRes(IDR_S3TEX_R_SKY2,pix,W,H)){ if(!upload2d(&m_texSky2,&m_srvSky2)){delete[] pix;return FALSE;} }
	if(Soft3DTexLoadPngRes(IDR_S3TEX_R_SKY3,pix,W,H)){ if(!upload2d(&m_texSky3,&m_srvSky3)){delete[] pix;return FALSE;} }
	if(Soft3DTexLoadPngRes(IDR_S3TEX_R_SKY4,pix,W,H)){ if(!upload2d(&m_texSky4,&m_srvSky4)){delete[] pix;return FALSE;} }
	if(Soft3DTexLoadPngRes(IDR_S3TEX_R_ITEM,pix,W,H)){ if(!upload2d(&m_texItem,&m_srvItem)){delete[] pix;return FALSE;} }
	if(Soft3DTexLoadPngRes(IDR_S3TEX_R_WOOD,pix,W,H)){ if(!upload2d(&m_texWood,&m_srvWood)){delete[] pix;return FALSE;} }
	delete[] pix;
	// noise UAV target 64x64
	{
		D3D11_TEXTURE2D_DESC nd={}; nd.Width=64; nd.Height=64; nd.MipLevels=1; nd.ArraySize=1;
		nd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; nd.SampleDesc.Count=1;
		nd.Usage=D3D11_USAGE_DEFAULT; nd.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
		if(FAILED(m_dev->CreateTexture2D(&nd,NULL,&m_texNoise))) return FALSE;
		if(FAILED(m_dev->CreateShaderResourceView(m_texNoise,NULL,&m_srvNoise))) return FALSE;
		if(FAILED(m_dev->CreateUnorderedAccessView(m_texNoise,NULL,&m_uavNoise))) return FALSE;
	}
	return BakeNoiseCS();
}

BOOL CS3rView::BakeNoiseCS()
{
	if(!m_imm||!m_csNoise||!m_uavNoise||!m_cbFrame) return FALSE;
	S3RFrameCB cb={}; cb.misc={0,0,0,(float)(GetTickCount()&0xffff)*.01f};
	D3D11_MAPPED_SUBRESOURCE map={};
	if(FAILED(m_imm->Map(m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))) return FALSE;
	memcpy(map.pData,&cb,sizeof(cb)); m_imm->Unmap(m_cbFrame,0);
	m_imm->CSSetShader(m_csNoise,NULL,0);
	m_imm->CSSetConstantBuffers(0,1,&m_cbFrame);
	m_imm->CSSetUnorderedAccessViews(0,1,&m_uavNoise,NULL);
	m_imm->Dispatch(8,8,1);
	ID3D11UnorderedAccessView* nullu=NULL; m_imm->CSSetUnorderedAccessViews(0,1,&nullu,NULL);
	m_imm->CSSetShader(NULL,NULL,0);
	return TRUE;
}

BOOL CS3rView::InitDx()
{
	ReleaseDx();
	m_dxFailStage = 0; m_dxFailHr = S_OK;
	if (!m_hWnd || !::IsWindow(m_hWnd)) { m_dxFailStage = 1; return FALSE; }
	UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	flags|=D3D11_CREATE_DEVICE_DEBUG;
#endif
	D3D_FEATURE_LEVEL req=D3D_FEATURE_LEVEL_11_0, got=(D3D_FEATURE_LEVEL)0;
	HRESULT hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,flags,&req,1,D3D11_SDK_VERSION,&m_dev,&got,&m_imm);
	if(FAILED(hr)) hr=D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,flags&~D3D11_CREATE_DEVICE_DEBUG,&req,1,D3D11_SDK_VERSION,&m_dev,&got,&m_imm);
	if(FAILED(hr) || got < D3D_FEATURE_LEVEL_11_0) { m_dxFailStage = 2; m_dxFailHr = hr; return FALSE; }
	IDXGIDevice* xd=NULL;IDXGIAdapter* xa=NULL;IDXGIFactory2* f2=NULL;IDXGIFactory* f1=NULL;
	if(FAILED(hr=m_dev->QueryInterface(__uuidof(IDXGIDevice),(void**)&xd))||FAILED(hr=xd->GetAdapter(&xa))) { m_dxFailStage = 3; m_dxFailHr = hr; S3R_RELEASE(xd); return FALSE; }
	xa->GetParent(__uuidof(IDXGIFactory2),(void**)&f2);
	// Width/Height=0: DXGI が HWND クライアントサイズを使う（迷路側と同じ）
	DXGI_SWAP_CHAIN_DESC1 s={};s.Width=0;s.Height=0;s.Format=DXGI_FORMAT_B8G8R8A8_UNORM;s.SampleDesc.Count=1;s.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;s.BufferCount=2;s.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;s.AlphaMode=DXGI_ALPHA_MODE_IGNORE;s.Scaling=DXGI_SCALING_STRETCH;
	if(f2){IDXGISwapChain1* sc1=NULL;hr=f2->CreateSwapChainForHwnd(m_dev,m_hWnd,&s,NULL,NULL,&sc1);if(FAILED(hr)){s.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;hr=f2->CreateSwapChainForHwnd(m_dev,m_hWnd,&s,NULL,NULL,&sc1);}if(FAILED(hr)){s.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;s.BufferCount=1;hr=f2->CreateSwapChainForHwnd(m_dev,m_hWnd,&s,NULL,NULL,&sc1);}if(SUCCEEDED(hr))m_swap=sc1;}else hr=E_FAIL;
	if(FAILED(hr)){xa->GetParent(__uuidof(IDXGIFactory),(void**)&f1);DXGI_SWAP_CHAIN_DESC o={};o.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;o.SampleDesc.Count=1;o.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;o.BufferCount=1;o.OutputWindow=m_hWnd;o.Windowed=TRUE;o.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;hr=f1?f1->CreateSwapChain(m_dev,&o,&m_swap):E_FAIL;}
	S3R_RELEASE(f1);S3R_RELEASE(f2);S3R_RELEASE(xa);S3R_RELEASE(xd);
	if(FAILED(hr)||!m_swap){ m_dxFailStage = 4; m_dxFailHr = hr; return FALSE; }
	{
		IDXGIFactory* fa=NULL;
		if(SUCCEEDED(m_swap->GetParent(__uuidof(IDXGIFactory),(void**)&fa)) && fa){
			fa->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES);
			fa->Release();
		}
	}
	D3D11_BUFFER_DESC bd={};bd.ByteWidth=((sizeof(S3RFrameCB)+15)/16)*16;bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
	if(FAILED(hr=m_dev->CreateBuffer(&bd,NULL,&m_cbFrame))){ m_dxFailStage = 5; m_dxFailHr = hr; return FALSE; }
	if(!CreateShaders()){ m_dxFailStage = 6; if(m_dxFailHr==S_OK) m_dxFailHr = E_FAIL; return FALSE; }
	if(!CreateProcTextures()){ m_dxFailStage = 7; m_dxFailHr = E_FAIL; return FALSE; }
	bd.ByteWidth=m_vbDynBytes;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
	if(FAILED(hr=m_dev->CreateBuffer(&bd,NULL,&m_vbDyn))){
		static const UINT kVbTry[]={2u*1024u*1024u,1024u*1024u};
		hr=E_FAIL;
		for(int ti=0;ti<2 && FAILED(hr);ti++){
			m_vbDynBytes=kVbTry[ti]; bd.ByteWidth=m_vbDynBytes;
			hr=m_dev->CreateBuffer(&bd,NULL,&m_vbDyn);
		}
		if(FAILED(hr)){ m_dxFailStage = 8; m_dxFailHr = hr; return FALSE; }
	}
	bd.ByteWidth=m_vbHudBytes;
	if(FAILED(hr=m_dev->CreateBuffer(&bd,NULL,&m_vbHud))){ m_dxFailStage = 9; m_dxFailHr = hr; return FALSE; }
	bd.ByteWidth = sizeof(S3RInst) * (UINT)CSoft3DRaceDlg::S3R_MAX_CRAFT;
	if (bd.ByteWidth < 256) bd.ByteWidth = 256;
	if(FAILED(hr=m_dev->CreateBuffer(&bd,NULL,&m_vbCraftInst))){ m_dxFailStage = 9; m_dxFailHr = hr; return FALSE; }
	D3D11_SAMPLER_DESC ss={};ss.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;ss.MaxLOD=D3D11_FLOAT32_MAX;m_dev->CreateSamplerState(&ss,&m_sampLin);ss.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;m_dev->CreateSamplerState(&ss,&m_sampPoint);
	ss.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;ss.AddressU=ss.AddressV=ss.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;ss.ComparisonFunc=D3D11_COMPARISON_LESS;ss.BorderColor[0]=ss.BorderColor[1]=ss.BorderColor[2]=ss.BorderColor[3]=1.f;m_dev->CreateSamplerState(&ss,&m_sampCmp);
	D3D11_RASTERIZER_DESC rs={};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_BACK;rs.DepthClipEnable=TRUE;m_dev->CreateRasterizerState(&rs,&m_rsSolid);
	rs.CullMode=D3D11_CULL_NONE;m_dev->CreateRasterizerState(&rs,&m_rsNoCull);
	D3D11_RASTERIZER_DESC rss={};rss.FillMode=D3D11_FILL_SOLID;rss.CullMode=D3D11_CULL_NONE;rss.DepthClipEnable=TRUE;rss.DepthBias=1000;rss.SlopeScaledDepthBias=1.5f;m_dev->CreateRasterizerState(&rss,&m_rsShadow);
	D3D11_DEPTH_STENCIL_DESC ds={};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;m_dev->CreateDepthStencilState(&ds,&m_dssWrite);ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;m_dev->CreateDepthStencilState(&ds,&m_dssRead);ds.DepthEnable=FALSE;m_dev->CreateDepthStencilState(&ds,&m_dssOff);
	D3D11_BLEND_DESC bl={};bl.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;m_dev->CreateBlendState(&bl,&m_bsOpaque);bl.RenderTarget[0].BlendEnable=TRUE;bl.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bl.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;bl.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;bl.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bl.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;bl.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;m_dev->CreateBlendState(&bl,&m_bsAlpha);bl.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bl.RenderTarget[0].DestBlend=D3D11_BLEND_ONE;m_dev->CreateBlendState(&bl,&m_bsAdd);
	{
		D3D11_TEXTURE2D_DESC td={};td.Width=S3R_SHADOW_SIZE;td.Height=S3R_SHADOW_SIZE;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R24G8_TYPELESS;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
		if(FAILED(hr=m_dev->CreateTexture2D(&td,NULL,&m_shadowTex))){ m_dxFailStage = 10; m_dxFailHr = hr; return FALSE; }
		D3D11_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
		if(FAILED(hr=m_dev->CreateDepthStencilView(m_shadowTex,&dd,&m_shadowDsv))){ m_dxFailStage = 10; m_dxFailHr = hr; return FALSE; }
		D3D11_SHADER_RESOURCE_VIEW_DESC sd={};sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
		if(FAILED(hr=m_dev->CreateShaderResourceView(m_shadowTex,&sd,&m_shadowSrv))){ m_dxFailStage = 10; m_dxFailHr = hr; return FALSE; }
	}
	if(!EnsureAuxTargets()){ m_dxFailStage = 10; m_dxFailHr = E_FAIL; return FALSE; }
	CRect rc;GetClientRect(&rc);
	if(!ResizeDx(max(8,rc.Width()),max(8,rc.Height()))){ m_dxFailStage = 11; m_dxFailHr = E_FAIL; return FALSE; }
	return TRUE;
}

BOOL CS3rView::EnsureAuxTargets()
{
	if(!m_dev) return FALSE;
	auto makeColor=[&](int w,int h,ID3D11Texture2D** tex,ID3D11RenderTargetView** rtv,ID3D11ShaderResourceView** srv)->BOOL{
		if(*tex) return TRUE;
		D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
		if(FAILED(m_dev->CreateTexture2D(&d,NULL,tex))) return FALSE;
		if(FAILED(m_dev->CreateRenderTargetView(*tex,NULL,rtv))) return FALSE;
		if(FAILED(m_dev->CreateShaderResourceView(*tex,NULL,srv))) return FALSE;
		return TRUE;
	};
	auto makeDepth=[&](int w,int h,ID3D11Texture2D** tex,ID3D11DepthStencilView** dsv)->BOOL{
		if(*tex) return TRUE;
		D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R24G8_TYPELESS;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
		if(FAILED(m_dev->CreateTexture2D(&d,NULL,tex))) return FALSE;
		D3D11_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
		if(FAILED(m_dev->CreateDepthStencilView(*tex,&dd,dsv))) return FALSE;
		return TRUE;
	};
	if(!makeColor(S3R_REAR_W,S3R_REAR_H,&m_rearTex,&m_rearRtv,&m_srvRear)) return FALSE;
	if(!makeDepth(S3R_REAR_W,S3R_REAR_H,&m_rearDs,&m_rearDsv)) return FALSE;
	if(!makeColor(S3R_REFLECT_SIZE,S3R_REFLECT_SIZE,&m_reflectTex,&m_reflectRtv,&m_srvReflect)) return FALSE;
	if(!makeDepth(S3R_REFLECT_SIZE,S3R_REFLECT_SIZE,&m_reflectDs,&m_reflectDsv)) return FALSE;
	if (m_imm) {
		float zc[4] = {0.35f,0.48f,0.62f,1.f};
		if (m_rearRtv) { m_imm->ClearRenderTargetView(m_rearRtv, zc); }
		float wc[4] = {0.42f,0.58f,0.72f,1.f};
		if (m_reflectRtv) { m_imm->ClearRenderTargetView(m_reflectRtv, wc); }
	}
	return TRUE;
}

BOOL CS3rView::EnsureSceneTargets(int w,int h)
{
	if(m_sceneTex&&w==m_vw&&h==m_vh)return TRUE;
	ID3D11RenderTargetView* nullrt=NULL;m_imm->OMSetRenderTargets(1,&nullrt,NULL);
	S3R_RELEASE(m_dsSrv);S3R_RELEASE(m_dsv);S3R_RELEASE(m_dsTex);S3R_RELEASE(m_sceneSrv);S3R_RELEASE(m_sceneRtv);S3R_RELEASE(m_sceneTex);S3R_RELEASE(m_postSrv);S3R_RELEASE(m_postRtv);S3R_RELEASE(m_postTex);
	D3D11_TEXTURE2D_DESC d={};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
	if(FAILED(m_dev->CreateTexture2D(&d,NULL,&m_sceneTex))||FAILED(m_dev->CreateRenderTargetView(m_sceneTex,NULL,&m_sceneRtv))||FAILED(m_dev->CreateShaderResourceView(m_sceneTex,NULL,&m_sceneSrv)))return FALSE;
	if(FAILED(m_dev->CreateTexture2D(&d,NULL,&m_postTex))||FAILED(m_dev->CreateRenderTargetView(m_postTex,NULL,&m_postRtv))||FAILED(m_dev->CreateShaderResourceView(m_postTex,NULL,&m_postSrv)))return FALSE;
	d.Format=DXGI_FORMAT_R24G8_TYPELESS;d.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;if(FAILED(m_dev->CreateTexture2D(&d,NULL,&m_dsTex)))return FALSE;
	D3D11_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;if(FAILED(m_dev->CreateDepthStencilView(m_dsTex,&dd,&m_dsv)))return FALSE;
	D3D11_SHADER_RESOURCE_VIEW_DESC sd={};sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;if(FAILED(m_dev->CreateShaderResourceView(m_dsTex,&sd,&m_dsSrv)))return FALSE;
	m_vw=w;m_vh=h;return TRUE;
}
BOOL CS3rView::ResizeDx(int w,int h)
{
	if(!m_swap||w<1||h<1)return FALSE;m_ready=FALSE;m_imm->OMSetRenderTargets(0,NULL,NULL);S3R_RELEASE(m_bbRtv);
	HRESULT hr=m_swap->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr))return FALSE;ID3D11Texture2D* bb=NULL;hr=m_swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb);if(SUCCEEDED(hr))hr=m_dev->CreateRenderTargetView(bb,NULL,&m_bbRtv);S3R_RELEASE(bb);
	if(FAILED(hr)||!EnsureSceneTargets(w,h))return FALSE;m_ready=TRUE;return TRUE;
}
void CS3rView::PresentFrame(){if(m_swap&&m_ready)m_swap->Present(0,0);}
void CS3rView::ReleaseClearTexture(){S3R_RELEASE(m_srvClear);S3R_RELEASE(m_texClear);m_clearTexW=m_clearTexH=0;}
void CS3rView::ReleaseHudTexture(){S3R_RELEASE(m_srvHud);S3R_RELEASE(m_texHud);m_hudTexW=m_hudTexH=0;}
void CS3rView::ReleaseGaugeTexture(){S3R_RELEASE(m_srvGauge);S3R_RELEASE(m_texGauge);m_gaugeTexW=m_gaugeTexH=0;}
void CS3rView::ReleaseStandingsTexture(){S3R_RELEASE(m_srvStand);S3R_RELEASE(m_texStand);m_standTexW=m_standTexH=0;}
void CS3rView::ReleaseBubbleTexture()
{
	S3R_RELEASE(m_srvBubble);S3R_RELEASE(m_texBubble);m_bubbleTexW=m_bubbleTexH=0;m_bubbleN=0;
}
void CS3rView::ReleaseItemLabTexture()
{
	S3R_RELEASE(m_srvItemLab);S3R_RELEASE(m_texItemLab);m_itemLabN=0;
}

BOOL CS3rView::BakeClearTexture(const wchar_t* text,float /*alpha*/)
{
	// alpha はシェーダ Misc.y で乗算（毎フレーム再焼きしない）
	ReleaseClearTexture();
	if (!m_dev || !text || !text[0]) return FALSE;
	const int w = max(256, m_vw), h = max(96, m_vh / 4);
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return FALSE;
	const int tlen = (int)wcslen(text);
	float fontPx = (float)max(40, h / 2);
	if (tlen > 10) fontPx = (float)max(32, h / 3);
	if (tlen > 16) fontPx = (float)max(26, h / 4);
	Soft3DTextD2D_DrawTextShadow(cv, text, 6.f, 6.f, (float)(w - 12), (float)(h - 12),
		fontPx, 1, 1, 1, 255, 255, 236, 150, 3.f, 3.f, 200, 0, 0, 0);
	const BYTE* bits = NULL; UINT stride = 0;
	if (!Soft3DTextD2D_End(cv, &bits, &stride) || !bits) { Soft3DTextD2D_Release(cv); return FALSE; }
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, stride, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texClear);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texClear, NULL, &m_srvClear);
	Soft3DTextD2D_Release(cv);
	m_clearTexW = w; m_clearTexH = h;
	return SUCCEEDED(hr);
}
BOOL CS3rView::BakeHudTexture(const wchar_t* text)
{
	if (!m_dev || !text) return FALSE;
	ReleaseHudTexture();
	const int w = 640, h = 220;
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return FALSE;
	Soft3DTextD2D_FillRect(cv, 0, 0, (float)w, (float)h, 140, 8, 10, 18);
	Soft3DTextD2D_DrawTextShadow(cv, text, 16.f, 14.f, (float)(w - 28), (float)(h - 24),
		40.f, 1, 0, 0, 255, 245, 250, 255, 2.f, 2.f, 160, 0, 0, 0, 1);
	const BYTE* bits = NULL; UINT stride = 0;
	if (!Soft3DTextD2D_End(cv, &bits, &stride) || !bits) { Soft3DTextD2D_Release(cv); return FALSE; }
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, stride, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texHud);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texHud, NULL, &m_srvHud);
	Soft3DTextD2D_Release(cv);
	m_hudTexW = w; m_hudTexH = h;
	return SUCCEEDED(hr);
}
BOOL CS3rView::BakeGaugeTexture(float kmh, float rpm01)
{
	ReleaseGaugeTexture();
	if (!m_dev) return FALSE;
	if (kmh < 0.f) kmh = 0.f;
	if (rpm01 < 0.f) rpm01 = 0.f;
	if (rpm01 > 1.f) rpm01 = 1.f;
	const int w = 560, h = 250;
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return FALSE;
	const float a0 = 3.9269908f;
	const float sweep = 4.7123890f;
	auto dial = [&](float cx, float cy, float rad, float t, int tach) {
		Soft3DTextD2D_FillEllipse(cv, cx, cy, rad + 5.f, rad + 5.f, 235, 22, 24, 30);
		Soft3DTextD2D_FillEllipse(cv, cx, cy, rad, rad, 245, 8, 10, 16);
		Soft3DTextD2D_FillEllipse(cv, cx, cy, rad * 0.70f, rad * 0.70f, 200, 14, 16, 22);
		const int nTick = tach ? 18 : 26;
		for (int i = 0; i <= nTick; i++) {
			float u = (float)i / (float)nTick;
			float th = a0 - u * sweep;
			float cth = cosf(th), sth = sinf(th);
			int major = (i % 2) == 0;
			float r0 = rad * (major ? 0.76f : 0.84f);
			float r1 = rad * 0.97f;
			float x0 = cx + cth * r0, y0 = cy - sth * r0;
			float x1 = cx + cth * r1, y1 = cy - sth * r1;
			float qx = sth * (major ? 2.4f : 1.15f), qy = cth * (major ? 2.4f : 1.15f);
			BYTE ar = 210, ag = 220, ab = 235;
			if (tach && u >= 0.78f) { ar = 255; ag = 48; ab = 42; }
			Soft3DTextD2D_FillTriangle(cv, x0 + qx, y0 + qy, x0 - qx, y0 - qy, x1, y1, 255, ar, ag, ab);
		}
		if (t < 0.f) t = 0.f; if (t > 1.05f) t = 1.05f;
		float th = a0 - t * sweep;
		float cth = cosf(th), sth = sinf(th);
		float tipx = cx + cth * rad * 0.86f, tipy = cy - sth * rad * 0.86f;
		float hx = -sth * 6.5f, hy = -cth * 6.5f;
		BYTE nr = tach ? 255 : 255, ng = tach ? 196 : 72, nb = tach ? 48 : 48;
		Soft3DTextD2D_FillTriangle(cv, tipx, tipy, cx + hx, cy - hy, cx - hx, cy + hy, 255, nr, ng, nb);
		Soft3DTextD2D_FillEllipse(cv, cx, cy, 9.f, 9.f, 255, 228, 228, 236);
		Soft3DTextD2D_FillEllipse(cv, cx, cy, 4.f, 4.f, 255, 40, 40, 48);
	};
	const float spdT = kmh / 2600.f;
	dial(138.f, 128.f, 112.f, spdT, 0);
	dial(412.f, 128.f, 100.f, rpm01, 1);
	{
		const float cx = 138.f, cy = 128.f, rad = 112.f;
		const wchar_t* labs[] = { L"0", L"5", L"10", L"15", L"20", L"25" };
		for (int i = 0; i < 6; i++) {
			float u = (float)i / 5.f;
			float th = a0 - u * sweep;
			float r = rad * 0.58f;
			float x = cx + cosf(th) * r - 16.f;
			float y = cy - sinf(th) * r - 10.f;
			Soft3DTextD2D_DrawText(cv, labs[i], x, y, 36.f, 22.f, 15.f, 1, 1, 1, 230, 210, 220, 235);
		}
		wchar_t sp[32];
		swprintf_s(sp, L"%.0f", kmh);
		Soft3DTextD2D_DrawTextShadow(cv, sp, cx - 56.f, cy - 20.f, 112.f, 40.f,
			32.f, 1, 1, 1, 255, 255, 255, 255, 1.6f, 1.6f, 180, 0, 0, 0);
		Soft3DTextD2D_DrawText(cv, L"km/h", cx - 44.f, cy + 16.f, 88.f, 22.f, 16.f, 1, 1, 1, 240, 255, 210, 80);
	}
	{
		const float cx = 412.f, cy = 128.f, rad = 100.f;
		const wchar_t* labs[] = { L"0", L"2", L"4", L"6", L"8", L"9" };
		for (int i = 0; i < 6; i++) {
			float u = (i == 5) ? 1.f : ((float)i / 5.f) * (8.f / 9.f);
			float th = a0 - u * sweep;
			float r = rad * 0.56f;
			float x = cx + cosf(th) * r - 12.f;
			float y = cy - sinf(th) * r - 9.f;
			BYTE ar = (i >= 4) ? 255 : 210, ag = (i >= 4) ? 90 : 220, ab = (i >= 4) ? 80 : 235;
			Soft3DTextD2D_DrawText(cv, labs[i], x, y, 28.f, 20.f, 14.f, 1, 1, 1, 230, ar, ag, ab);
		}
		wchar_t rp[32];
		swprintf_s(rp, L"%.1f", rpm01 * 9.f);
		Soft3DTextD2D_DrawTextShadow(cv, rp, cx - 44.f, cy - 18.f, 88.f, 32.f,
			24.f, 1, 1, 1, 255, 255, 220, 190, 1.5f, 1.5f, 180, 0, 0, 0);
		Soft3DTextD2D_DrawText(cv, L"x1000rpm", cx - 52.f, cy + 14.f, 104.f, 20.f, 13.f, 1, 1, 1, 240, 255, 120, 70);
	}
	const BYTE* bits = NULL; UINT stride = 0;
	if (!Soft3DTextD2D_End(cv, &bits, &stride) || !bits) { Soft3DTextD2D_Release(cv); return FALSE; }
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, stride, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texGauge);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texGauge, NULL, &m_srvGauge);
	Soft3DTextD2D_Release(cv);
	m_gaugeTexW = w; m_gaugeTexH = h;
	return SUCCEEDED(hr);
}
BOOL CS3rView::BakeStandingsTexture(const S3rStandRow* rows, int nRows, int nitroStock, int nitroMax, float nitroPulse)
{
	if (!m_dev || !rows || nRows < 1) return FALSE;
	ReleaseStandingsTexture();
	if (nRows > 12) nRows = 12;
	if (nitroMax < 1) nitroMax = 1;
	if (nitroMax > 8) nitroMax = 8;
	if (nitroStock < 0) nitroStock = 0;
	if (nitroStock > nitroMax) nitroStock = nitroMax;
	const int rowH = 72;
	const int nitroStrip = 52;
	const int w = 400, h = nRows * rowH + 8 + nitroStrip;
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return FALSE;
	for (int i = 0; i < nRows; i++) {
		const S3rStandRow& r = rows[i];
		float y0 = (float)(4 + i * rowH);
		BYTE aBg = r.isPlayer ? (BYTE)230 : (BYTE)185;
		BYTE br = r.isPlayer ? 28 : 10, bg = r.isPlayer ? 48 : 14, bb = r.isPlayer ? 72 : 22;
		Soft3DTextD2D_FillRect(cv, 3.f, y0, (float)(w - 6), (float)(rowH - 4), aBg, br, bg, bb);
		BYTE cr = (BYTE)(r.cr * 255.f), cg = (BYTE)(r.cg * 255.f), cb = (BYTE)(r.cb * 255.f);
		float cx = 24.f, cy = y0 + (float)rowH * 0.5f;
		Soft3DTextD2D_FillEllipse(cv, cx, cy, 11.f, 9.f, 255, cr, cg, cb);
		Soft3DTextD2D_FillTriangle(cv, cx - 14.f, cy, cx - 2.f, cy - 7.f, cx - 2.f, cy + 7.f, 255, cr, cg, cb);
		Soft3DTextD2D_FillTriangle(cv, cx + 14.f, cy, cx + 2.f, cy - 7.f, cx + 2.f, cy + 7.f, 255, cr, cg, cb);
		wchar_t rankBuf[24];
		S3rRankWord(r.rank, rankBuf, _countof(rankBuf));
		BYTE fr = r.isPlayer ? 255 : 250, fg = r.isPlayer ? 252 : 250, fb = r.isPlayer ? 215 : 255;
		Soft3DTextD2D_DrawTextShadow(cv, r.name, 42.f, y0 + 4.f, 86.f, (float)(rowH - 10),
			24.f, 1, 0, 1, 255, fr, fg, fb, 1.6f, 1.6f, 200, 0, 0, 0);
		Soft3DTextD2D_DrawTextShadow(cv, rankBuf, 324.f, y0 + 4.f, 70.f, (float)(rowH - 10),
			20.f, 1, 2, 1, 255, fr, fg, fb, 1.6f, 1.6f, 200, 0, 0, 0);
		if (r.retired) {
			const wchar_t* retire = LL14(L"リタイア", L"RETIRED", L"ABANDON", L"RITIRATO", L"ABANDONO",
				L"리타이어", L"退赛", L"انسحاب", L"СОШЁЛ", L"AUFGABE", L"ABANDONO", L"OPGEGEVEN", L"WYCOFANY", L"ÇEKİLDİ");
			Soft3DTextD2D_DrawTextShadow(cv, retire, 130.f, y0 + 16.f, 190.f, 40.f,
				20.f, 1, 0, 1, 255, 255, 170, 140, 1.4f, 1.4f, 200, 0, 0, 0);
		} else {
			const int nLap = r.lapShowN < 0 ? 0 : (r.lapShowN > 4 ? 4 : r.lapShowN);
			const float lapX0 = 130.f, colW = 94.f, lapH = 26.f;
			const float lapY0 = y0 + ((float)rowH - lapH * 2.f) * 0.5f;
			for (int k = 0; k < nLap; k++) {
				int lapNo = r.lapNo[k] > 0 ? r.lapNo[k] : (k + 1);
				float sec = r.lapSec[k];
				wchar_t one[40];
				if (sec >= 0.f) swprintf_s(one, L"L%d %.1f", lapNo, sec);
				else swprintf_s(one, L"L%d ----", lapNo);
				int col = k & 1;
				int row = k >> 1;
				Soft3DTextD2D_DrawTextShadow(cv, one, lapX0 + (float)col * colW, lapY0 + (float)row * lapH, colW - 2.f, lapH,
					16.f, 1, 0, 1, 255, 210, 230, 245, 1.3f, 1.3f, 200, 0, 0, 0);
			}
		}
	}
	// 順位パネル下：自機ニトロ所持アイコン
	{
		float y0 = (float)(4 + nRows * rowH + 2);
		Soft3DTextD2D_FillRect(cv, 3.f, y0, (float)(w - 6), (float)(nitroStrip - 6), 210, 18, 22, 40);
		const wchar_t* lab = LL14(L"ニトロ", L"NITRO", L"NITRO", L"NITRO", L"NITRO",
			L"니트로", L"氮气", L"نيترو", L"НИТРО", L"NITRO", L"NITRO", L"NITRO", L"NITRO", L"NİTRO");
		Soft3DTextD2D_DrawTextShadow(cv, lab, 10.f, y0 + 6.f, 70.f, 28.f,
			16.f, 1, 0, 1, 255, 120, 220, 255, 1.2f, 1.2f, 180, 0, 0, 0);
		const float iconR = 14.f;
		const float gap = 8.f;
		float x0 = 88.f;
		float cy = y0 + (float)(nitroStrip - 6) * 0.5f;
		BYTE pulse = (BYTE)(40.f + 80.f * (nitroPulse > 0.f ? nitroPulse : 0.f));
		for (int i = 0; i < nitroMax; i++) {
			float cx = x0 + (float)i * (iconR * 2.f + gap);
			const int filled = (i < nitroStock);
			if (filled) {
				Soft3DTextD2D_FillEllipse(cv, cx, cy + 2.f, iconR * 0.72f, iconR * 0.95f, 255, 30, 160, 255);
				Soft3DTextD2D_FillEllipse(cv, cx, cy - 6.f, iconR * 0.38f, iconR * 0.32f, 255, 80, 200, 255);
				Soft3DTextD2D_FillTriangle(cv, cx - 5.f, cy - 10.f, cx + 5.f, cy - 10.f, cx, cy - 20.f,
					255, 255, (BYTE)(140 + pulse / 2), 40);
				Soft3DTextD2D_FillEllipse(cv, cx - 3.f, cy, 3.5f, 5.f, 160, 200, 255, 255);
			} else {
				Soft3DTextD2D_FillEllipse(cv, cx, cy + 2.f, iconR * 0.72f, iconR * 0.95f, 120, 50, 60, 80);
				Soft3DTextD2D_FillEllipse(cv, cx, cy - 6.f, iconR * 0.38f, iconR * 0.32f, 100, 70, 80, 100);
			}
		}
		if (nitroPulse > 0.05f) {
			wchar_t act[24];
			swprintf_s(act, L"ACTIVE");
			Soft3DTextD2D_DrawTextShadow(cv, act, 300.f, y0 + 10.f, 90.f, 24.f,
				14.f, 1, 2, 1, 255, 255, (BYTE)(180 + pulse / 2), 80, 1.2f, 1.2f, 200, 0, 0, 0);
		}
	}
	const BYTE* bits = NULL; UINT stride = 0;
	if (!Soft3DTextD2D_End(cv, &bits, &stride) || !bits) { Soft3DTextD2D_Release(cv); return FALSE; }
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, stride, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texStand);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texStand, NULL, &m_srvStand);
	Soft3DTextD2D_Release(cv);
	m_standTexW = w; m_standTexH = h;
	return SUCCEEDED(hr);
}
BOOL CS3rView::BakeBubbleTexture(const S3rBubbleRow* rows, int nRows)
{
	ReleaseBubbleTexture();
	if (!m_dev || !rows || nRows < 1) return FALSE;
	if (nRows > S3R_BUBBLE_MAX) nRows = S3R_BUBBLE_MAX;
	const int cellW = 384, cellH = 80, cols = 4;
	const int rowsN = (nRows + cols - 1) / cols;
	const int w = cellW * cols, h = cellH * rowsN;
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return FALSE;
	for (int i = 0; i < nRows; i++) {
		const int col = i % cols, row = i / cols;
		const float x = (float)(col * cellW), y = (float)(row * cellH);
		const BYTE fr = rows[i].isPlayer ? 255 : 250;
		const BYTE fg = rows[i].isPlayer ? 252 : 248;
		const BYTE fb = rows[i].isPlayer ? 210 : 255;
		Soft3DTextD2D_DrawTextShadow(cv, rows[i].text, x + 8.f, y + 6.f, (float)(cellW - 16), (float)(cellH - 12),
			40.f, 1, 1, 1, 255, fr, fg, fb, 2.2f, 2.2f, 210, 0, 0, 0);
		const float pad = 2.f;
		m_bubbleU0[i] = (x + pad) / (float)w;
		m_bubbleV0[i] = (y + pad) / (float)h;
		m_bubbleU1[i] = (x + cellW - pad) / (float)w;
		m_bubbleV1[i] = (y + cellH - pad) / (float)h;
	}
	const BYTE* bits = NULL; UINT stride = 0;
	if (!Soft3DTextD2D_End(cv, &bits, &stride) || !bits) { Soft3DTextD2D_Release(cv); return FALSE; }
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, stride, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texBubble);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texBubble, NULL, &m_srvBubble);
	Soft3DTextD2D_Release(cv);
	m_bubbleTexW = w; m_bubbleTexH = h;
	m_bubbleN = nRows;
	return SUCCEEDED(hr);
}
BOOL CS3rView::BakeItemLabTexture(const wchar_t* const* labels, int nLabels)
{
	ReleaseItemLabTexture();
	if (!m_dev || !labels || nLabels < 1) return FALSE;
	if (nLabels > S3R_ITEMLAB_MAX) nLabels = S3R_ITEMLAB_MAX;
	const int cellW = 280, cellH = 56, cols = 4;
	const int rowsN = (nLabels + cols - 1) / cols;
	const int w = cellW * cols, h = cellH * rowsN;
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return FALSE;
	for (int i = 0; i < nLabels; i++) {
		const int col = i % cols, row = i / cols;
		const float x = (float)(col * cellW), y = (float)(row * cellH);
		Soft3DTextD2D_DrawTextShadow(cv, labels[i] ? labels[i] : L"", x + 5.f, y + 3.f, (float)(cellW - 10), (float)(cellH - 6),
			32.f, 1, 1, 1, 255, 250, 248, 255, 1.6f, 1.6f, 200, 0, 0, 0);
		const float pad = 2.f;
		m_itemLabU0[i] = (x + pad) / (float)w;
		m_itemLabV0[i] = (y + pad) / (float)h;
		m_itemLabU1[i] = (x + cellW - pad) / (float)w;
		m_itemLabV1[i] = (y + cellH - pad) / (float)h;
	}
	const BYTE* bits = NULL; UINT stride = 0;
	if (!Soft3DTextD2D_End(cv, &bits, &stride) || !bits) { Soft3DTextD2D_Release(cv); return FALSE; }
	D3D11_TEXTURE2D_DESC d = {};
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = { bits, stride, 0 };
	HRESULT hr = m_dev->CreateTexture2D(&d, &sd, &m_texItemLab);
	if (SUCCEEDED(hr)) hr = m_dev->CreateShaderResourceView(m_texItemLab, NULL, &m_srvItemLab);
	Soft3DTextD2D_Release(cv);
	m_itemLabN = nLabels;
	return SUCCEEDED(hr);
}
void CS3rView::ReleaseDx()
{
	m_ready=FALSE;if(m_imm){m_imm->ClearState();m_imm->Flush();}
	ReleaseClearTexture();ReleaseHudTexture();ReleaseGaugeTexture();ReleaseStandingsTexture();ReleaseBubbleTexture();ReleaseItemLabTexture();
	S3R_RELEASE(m_uavNoise);S3R_RELEASE(m_srvNoise);S3R_RELEASE(m_texNoise);
	S3R_RELEASE(m_srvWood);S3R_RELEASE(m_texWood);S3R_RELEASE(m_srvItem);S3R_RELEASE(m_texItem);
	S3R_RELEASE(m_srvSky4);S3R_RELEASE(m_texSky4);S3R_RELEASE(m_srvSky3);S3R_RELEASE(m_texSky3);
	S3R_RELEASE(m_srvSky2);S3R_RELEASE(m_texSky2);S3R_RELEASE(m_srvSky);S3R_RELEASE(m_texSky);
	S3R_RELEASE(m_srvEnv2);S3R_RELEASE(m_texEnv2);S3R_RELEASE(m_srvEnv);S3R_RELEASE(m_texEnv);S3R_RELEASE(m_srvCraftD);S3R_RELEASE(m_texCraftD);S3R_RELEASE(m_srvCraft);S3R_RELEASE(m_texCraft);
	S3R_RELEASE(m_srvWaterD);S3R_RELEASE(m_texWaterD);S3R_RELEASE(m_srvWater);S3R_RELEASE(m_texWater);
	S3R_RELEASE(m_srvObsD);S3R_RELEASE(m_texObsD);S3R_RELEASE(m_srvObs);S3R_RELEASE(m_texObs);S3R_RELEASE(m_srvBand);S3R_RELEASE(m_texBand);
	for(int i=0;i<S3R_THEME_N;i++){S3R_RELEASE(m_srvThemeD[i]);S3R_RELEASE(m_texThemeD[i]);S3R_RELEASE(m_srvTheme[i]);S3R_RELEASE(m_texTheme[i]);}
	S3R_RELEASE(m_bsAdd);S3R_RELEASE(m_bsAlpha);S3R_RELEASE(m_bsOpaque);S3R_RELEASE(m_dssOff);S3R_RELEASE(m_dssRead);S3R_RELEASE(m_dssWrite);S3R_RELEASE(m_rsShadow);S3R_RELEASE(m_rsNoCull);S3R_RELEASE(m_rsSolid);S3R_RELEASE(m_sampCmp);S3R_RELEASE(m_sampPoint);S3R_RELEASE(m_sampLin);
	S3R_RELEASE(m_vbHud);
	S3R_RELEASE(m_vbCraftInst); S3R_RELEASE(m_ibCraft); S3R_RELEASE(m_vbCraft);
	S3R_RELEASE(m_vbObsInst); S3R_RELEASE(m_ibObs); S3R_RELEASE(m_vbObs);
	for (int i = 0; i < S3R_MESH_PARTS; i++) {
		S3R_RELEASE(m_vbScenery[i]); S3R_RELEASE(m_vbWater[i]); S3R_RELEASE(m_vbBand[i]); S3R_RELEASE(m_vbTerr[i]);
		m_vbTerrN[i] = m_vbBandN[i] = m_vbWaterN[i] = m_vbSceneryN[i] = 0;
	}
	m_vbTerrParts = m_vbBandParts = m_vbWaterParts = m_vbSceneryParts = 0;
	m_obsNvGpu=m_obsNiGpu=m_obsInstN=0; m_craftNvGpu=m_craftNiGpu=0;
	S3R_RELEASE(m_vbDyn);S3R_RELEASE(m_cbFrame);S3R_RELEASE(m_ilHud);S3R_RELEASE(m_ilInst);S3R_RELEASE(m_ilSolid);S3R_RELEASE(m_ilPatch);
	delete[] m_cpuDynScratch; m_cpuDynScratch=NULL; m_cpuDynScratchBytes=0;
	delete[] m_cpuHudScratch; m_cpuHudScratch=NULL; m_cpuHudScratchBytes=0;
	delete[] m_cpuBakeScratch; m_cpuBakeScratch=NULL; m_cpuBakeScratchBytes=0;
	S3R_RELEASE(m_csNoise);S3R_RELEASE(m_psFinal);S3R_RELEASE(m_psDof);S3R_RELEASE(m_psSsr);S3R_RELEASE(m_vsPost);S3R_RELEASE(m_psHudLine);S3R_RELEASE(m_psHud);S3R_RELEASE(m_vsHud);S3R_RELEASE(m_psCraft);S3R_RELEASE(m_psTerr);S3R_RELEASE(m_psWater);S3R_RELEASE(m_psCloud);S3R_RELEASE(m_psSolid);S3R_RELEASE(m_vsInst);S3R_RELEASE(m_vsSolid);S3R_RELEASE(m_psBand);S3R_RELEASE(m_dsTess);S3R_RELEASE(m_hsTess);S3R_RELEASE(m_vsTess);
	S3R_RELEASE(m_shadowSrv);S3R_RELEASE(m_shadowDsv);S3R_RELEASE(m_shadowTex);
	S3R_RELEASE(m_srvRear);S3R_RELEASE(m_rearRtv);S3R_RELEASE(m_rearTex);S3R_RELEASE(m_rearDsv);S3R_RELEASE(m_rearDs);
	S3R_RELEASE(m_srvReflect);S3R_RELEASE(m_reflectRtv);S3R_RELEASE(m_reflectTex);S3R_RELEASE(m_reflectDsv);S3R_RELEASE(m_reflectDs);
	S3R_RELEASE(m_postSrv);S3R_RELEASE(m_postRtv);S3R_RELEASE(m_postTex);S3R_RELEASE(m_sceneSrv);S3R_RELEASE(m_sceneRtv);S3R_RELEASE(m_sceneTex);S3R_RELEASE(m_dsSrv);S3R_RELEASE(m_dsv);S3R_RELEASE(m_dsTex);S3R_RELEASE(m_bbRtv);S3R_RELEASE(m_swap);S3R_RELEASE(m_imm);S3R_RELEASE(m_dev);m_vw=m_vh=0;
}
void CS3rView::ClearTerrMesh()
{
	S3rUnbindIA(m_imm);
	if (m_imm) m_imm->Flush();
	for (int i = 0; i < S3R_MESH_PARTS; i++) { S3R_RELEASE(m_vbTerr[i]); m_vbTerrN[i] = 0; }
	m_vbTerrParts = 0;
}
void CS3rView::ClearStaticMeshes()
{
	S3rUnbindIA(m_imm);
	if (m_imm) { m_imm->ClearState(); m_imm->Flush(); }
	ClearTerrMesh();
	for (int i = 0; i < S3R_MESH_PARTS; i++) {
		S3R_RELEASE(m_vbBand[i]); m_vbBandN[i] = 0;
		S3R_RELEASE(m_vbWater[i]); m_vbWaterN[i] = 0;
		S3R_RELEASE(m_vbScenery[i]); m_vbSceneryN[i] = 0;
	}
	m_vbBandParts = m_vbWaterParts = m_vbSceneryParts = 0;
	S3R_RELEASE(m_vbObsInst); m_obsInstN = 0;
	S3R_RELEASE(m_ibObs); S3R_RELEASE(m_vbObs); m_obsNvGpu = m_obsNiGpu = 0;
	S3R_RELEASE(m_ibCraft); S3R_RELEASE(m_vbCraft); m_craftNvGpu = m_craftNiGpu = 0;
}
BOOL CS3rView::UploadDefaultVB(ID3D11Buffer** dst, UINT* nOut, const void* verts, UINT nVerts)
{
	if (!dst || !nOut) return FALSE;
	if (!m_dev || !verts || nVerts < 1) return FALSE;
	const UINT stride = (UINT)sizeof(S3RVertex);
	if (stride < 1) return FALSE;
	if (nVerts > kS3rMaxVbBytes / stride) nVerts = kS3rMaxVbBytes / stride;
	UINT bytes = nVerts * stride;
	bytes = (bytes + 15u) & ~15u;
	if (bytes < 16u) bytes = 16u;
	if (*dst && m_imm) {
		D3D11_BUFFER_DESC old = {};
		(*dst)->GetDesc(&old);
		if (old.ByteWidth >= bytes) {
			S3rUnbindIA(m_imm);
			D3D11_BOX box = {};
			box.right = nVerts * stride;
			box.bottom = 1;
			box.back = 1;
			m_imm->UpdateSubresource(*dst, 0, &box, verts, nVerts * stride, 0);
			*nOut = nVerts;
			return TRUE;
		}
	}
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = bytes;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA srd = {};
	srd.pSysMem = verts;
	ID3D11Buffer* neu = NULL;
	HRESULT hr = m_dev->CreateBuffer(&bd, &srd, &neu);
	if (FAILED(hr) || !neu) {
		S3rUnbindIA(m_imm);
		if (m_imm) m_imm->Flush();
		neu = NULL;
		hr = m_dev->CreateBuffer(&bd, &srd, &neu);
		if (FAILED(hr) || !neu) return FALSE;
	}
	S3rUnbindIA(m_imm);
	S3R_RELEASE(*dst);
	*dst = neu;
	*nOut = nVerts;
	return TRUE;
}
BOOL CS3rView::PushDefaultVBPart(ID3D11Buffer** arr, UINT* nArr, int& parts, const void* verts, UINT nVerts)
{
	if (!arr || !nArr || parts < 0 || parts >= S3R_MESH_PARTS) return FALSE;
	if (!UploadDefaultVB(&arr[parts], &nArr[parts], verts, nVerts)) return FALSE;
	parts++;
	return TRUE;
}
BOOL CS3rView::UploadDefaultIB(ID3D11Buffer** dst, UINT* nOut, const UINT* idx, UINT nIdx)
{
	if (!dst || !nOut) return FALSE;
	if (!m_dev || !idx || nIdx < 3) return FALSE;
	const UINT stride = (UINT)sizeof(UINT);
	const UINT kMaxIb = 16u * 1024u * 1024u;
	if (nIdx > kMaxIb / stride) nIdx = kMaxIb / stride;
	if (nIdx < 3) return FALSE;
	UINT bytes = nIdx * stride;
	if (*dst && m_imm) {
		D3D11_BUFFER_DESC old = {};
		(*dst)->GetDesc(&old);
		if (old.ByteWidth >= bytes) {
			S3rUnbindIA(m_imm);
			D3D11_BOX box = {};
			box.right = bytes;
			box.bottom = 1;
			box.back = 1;
			m_imm->UpdateSubresource(*dst, 0, &box, idx, bytes, 0);
			*nOut = nIdx;
			return TRUE;
		}
	}
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = bytes;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA srd = {};
	srd.pSysMem = idx;
	ID3D11Buffer* neu = NULL;
	if (FAILED(m_dev->CreateBuffer(&bd, &srd, &neu)) || !neu) {
		S3rUnbindIA(m_imm);
		if (m_imm) m_imm->Flush();
		neu = NULL;
		if (FAILED(m_dev->CreateBuffer(&bd, &srd, &neu)) || !neu) return FALSE;
	}
	S3rUnbindIA(m_imm);
	S3R_RELEASE(*dst);
	*dst = neu;
	*nOut = nIdx;
	return TRUE;
}
void CS3rView::OnSize(UINT nType,int cx,int cy){CCustomStatic::OnSize(nType,cx,cy);if(m_swap&&cx>0&&cy>0)ResizeDx(cx,cy);}
void CS3rView::OnPaint(){CPaintDC dc(this);ValidateRect(NULL);}
LRESULT CS3rView::OnPrintClient(WPARAM,LPARAM){return 0;}
void CS3rView::OnDestroy(){ReleaseDx();CCustomStatic::OnDestroy();}
void CS3rView::OnContextMenu(CWnd*,CPoint){ /* view: no context — RMB is brake */ }
void CS3rView::OnLButtonDown(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputAccel(TRUE);SetCapture();}
void CS3rView::OnLButtonUp(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputAccel(FALSE);if(GetCapture()==this)ReleaseCapture();}
void CS3rView::OnRButtonDown(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputBrake(TRUE);SetCapture();}
void CS3rView::OnRButtonUp(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputBrake(FALSE);if(GetCapture()==this)ReleaseCapture();}
void CS3rView::OnMButtonDown(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputLookback(TRUE);}
void CS3rView::OnMButtonUp(UINT,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputLookback(FALSE);}
void CS3rView::OnMouseMove(UINT nFlags,CPoint point)
{
	CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent(); if(!d) return;
	if(d->m_mouseLook){
		float dx=(float)(point.x-d->m_lastMouse.x), dy=(float)(point.y-d->m_lastMouse.y);
		// 初回や UI から入ったときの巨大ジャンプは無視（真下へ振り切れる主因）
		if (fabsf(dx) < 64.f && fabsf(dy) < 64.f)
			d->InputSteerDelta(dx*0.0032f, (savedata.s3r_invert_y ? 1.f : -1.f) * dy*0.0024f);
	}
	d->m_lastMouse=point; d->m_mouseLook=1;
}
BOOL CS3rView::OnMouseWheel(UINT,short zDelta,CPoint){if(CSoft3DRaceDlg* d=(CSoft3DRaceDlg*)GetParent())d->InputZoom(zDelta>0?1:-1);return TRUE;}
BOOL CS3rView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT && CCC_SetUiCursor(IDC_UI_CROSS))
		return TRUE;
	return CCustomStatic::OnSetCursor(pWnd, nHitTest, message);
}

// ---- dialog ----
IMPLEMENT_DYNAMIC(CSoft3DRaceDlg, CCustomBlurDialogBase)

BEGIN_MESSAGE_MAP(CSoft3DRaceDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_S3R_START, &CSoft3DRaceDlg::OnStart)
	ON_BN_CLICKED(IDC_S3R_GEN, &CSoft3DRaceDlg::OnGen)
	ON_BN_CLICKED(IDC_S3R_CLOSE, &CSoft3DRaceDlg::OnCloseBtn)
	ON_BN_CLICKED(IDC_S3R_HELP, &CSoft3DRaceDlg::OnHelp)
	ON_CBN_SELCHANGE(IDC_S3R_AI, &CSoft3DRaceDlg::OnAiChanged)
	ON_CBN_SELCHANGE(IDC_S3R_OPP, &CSoft3DRaceDlg::OnOppChanged)
	ON_CBN_SELCHANGE(IDC_S3R_LEN, &CSoft3DRaceDlg::OnLenChanged)
	ON_CBN_SELCHANGE(IDC_S3R_LAPS, &CSoft3DRaceDlg::OnLapsChanged)
	ON_CBN_SELCHANGE(IDC_S3R_THEME, &CSoft3DRaceDlg::OnThemeChanged)
	ON_CBN_SELCHANGE(IDC_S3R_INVERT, &CSoft3DRaceDlg::OnInvertChanged)
	ON_CBN_SELCHANGE(IDC_S3R_MESH, &CSoft3DRaceDlg::OnMeshChanged)
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_SHOWWINDOW()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

CSoft3DRaceDlg::CSoft3DRaceDlg(CWnd* p)
	: CCustomBlurDialogBase(IDD, p)
	, m_knotN(0), m_pathLen(0), m_craftN(0), m_obsN(0), m_itemN(0)
	, m_craftNv(0), m_craftNi(0), m_obsNv(0), m_obsNi(0)
	, m_phase(PHASE_IDLE), m_countT(0), m_countShown(-1), m_podiumT(0)
	, m_finishSimT(0), m_aiRaceLv(AI_NORMAL), m_blastCursor(0), m_fxCursor(0)
	, m_themeActive(THEME_FOREST), m_layoutKind(0), m_lapsTarget(3), m_bandHalf(6.f)
	, m_demoCamT(0.f), m_demoCamElev(0.f), m_demoMidX(0), m_demoMidY(0), m_demoMidZ(0), m_demoRad(80.f)
	, m_hmX0(0), m_hmZ0(0), m_hmStep(1.f), m_hmReady(0), m_waterY(8.f), m_carveN(0)
	, m_camYawOff(0), m_camPitchOff(0.22f), m_camZoom(1.f)
	, m_camSx(0), m_camSy(0), m_camSz(0), m_camAx(0), m_camAy(0), m_camAz(0), m_camSmoothInit(0), m_camTunnelT(0), m_frameDt(1.f/60.f)
	, m_lookback(0), m_accelHeld(0), m_brakeHeld(0), m_nitroBtnHeld(0), m_mouseLook(0)
	, m_lastTick(0), m_inTick(0), m_rng(1), m_genSeed(1), m_spaceToggleTick(0)
	, m_baseTempoPos(100), m_basePitchPos(200), m_anim(0), m_raceClock(0), m_playerSpdEma(0), m_playerAccel(0)
	, m_wrongWay(0), m_overlayHold(0), m_sfxHitCool(0)
	, m_clearBakeA(0), m_hudDirty(1), m_gaugeKmhQ(-1), m_gaugeRpmQ(-1), m_clearDirty(1), m_standDirty(1)
	, m_reverbFogBoost(0), m_eqDofBoost(0)
	, m_podiumBaseX(0), m_podiumBaseY(0), m_podiumBaseZ(0)
{
	memset(m_knots,0,sizeof(m_knots));
	memset(m_pathSampleXYZ,0,sizeof(m_pathSampleXYZ));
	memset(m_pathSampleT,0,sizeof(m_pathSampleT));
	memset(m_pathCumLen,0,sizeof(m_pathCumLen));
	memset(m_crafts,0,sizeof(m_crafts));
	memset(m_obs,0,sizeof(m_obs));
	memset(m_items,0,sizeof(m_items));
	memset(m_craftVert,0,sizeof(m_craftVert));
	memset(m_craftIdx,0,sizeof(m_craftIdx));
	memset(m_obsVert,0,sizeof(m_obsVert));
	memset(m_obsIdx,0,sizeof(m_obsIdx));
	memset(m_podiumOrder,0,sizeof(m_podiumOrder));
	memset(m_confetti,0,sizeof(m_confetti));
	memset(m_blast,0,sizeof(m_blast));
	memset(m_fx,0,sizeof(m_fx));
	memset(m_hm,0,sizeof(m_hm));
	memset(m_hmRaw,0,sizeof(m_hmRaw));
	memset(m_hmPathDist,0,sizeof(m_hmPathDist));
	memset(m_pathDeep,0,sizeof(m_pathDeep));
	memset(m_carveX0,0,sizeof(m_carveX0)); memset(m_carveY0,0,sizeof(m_carveY0)); memset(m_carveZ0,0,sizeof(m_carveZ0));
	memset(m_carveX1,0,sizeof(m_carveX1)); memset(m_carveY1,0,sizeof(m_carveY1)); memset(m_carveZ1,0,sizeof(m_carveZ1));
	memset(m_carveCeil,0,sizeof(m_carveCeil));
	m_lastMouse=CPoint(0,0);
}
CSoft3DRaceDlg::~CSoft3DRaceDlg() {}

void CSoft3DRaceDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_S3R_HELP, m_help);
	DDX_Control(pDX, IDC_S3R_AI_L, m_aiL);
	DDX_Control(pDX, IDC_S3R_AI, m_ai);
	DDX_Control(pDX, IDC_S3R_OPP_L, m_oppL);
	DDX_Control(pDX, IDC_S3R_OPP, m_opp);
	DDX_Control(pDX, IDC_S3R_LEN_L, m_lenL);
	DDX_Control(pDX, IDC_S3R_LEN, m_len);
	DDX_Control(pDX, IDC_S3R_LAPS_L, m_lapsL);
	DDX_Control(pDX, IDC_S3R_LAPS, m_laps);
	DDX_Control(pDX, IDC_S3R_THEME_L, m_themeL);
	DDX_Control(pDX, IDC_S3R_THEME, m_theme);
	DDX_Control(pDX, IDC_S3R_INVERT_L, m_invertL);
	DDX_Control(pDX, IDC_S3R_INVERT, m_invert);
	DDX_Control(pDX, IDC_S3R_MESH_L, m_meshL);
	DDX_Control(pDX, IDC_S3R_MESH, m_mesh);
	DDX_Control(pDX, IDC_S3R_START, m_start);
	DDX_Control(pDX, IDC_S3R_GEN, m_gen);
	DDX_Control(pDX, IDC_S3R_HINT, m_hint);
	DDX_Control(pDX, IDC_S3R_VIEW, m_view);
	DDX_Control(pDX, IDC_S3R_STATUS, m_status);
	DDX_Control(pDX, IDC_S3R_CLOSE, m_close);
}

BOOL CSoft3DRaceDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(pMsg);
	if (HandleAccelMessage(pMsg)) return TRUE;
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}
BOOL CSoft3DRaceDlg::HandleAccelMessage(MSG* pMsg)
{
	if (!pMsg) return FALSE;
	const UINT msg = pMsg->message;
	if (msg != WM_KEYDOWN && msg != WM_KEYUP && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP
		&& msg != WM_CHAR && msg != WM_SYSCHAR)
		return FALSE;
	if ((msg == WM_CHAR || msg == WM_SYSCHAR) && (pMsg->wParam == _T(' ') || pMsg->wParam == VK_SPACE))
		return TRUE;
	if (pMsg->wParam == VK_SPACE) {
		if (msg == WM_KEYDOWN && !(pMsg->lParam & (1 << 30))) {
			const DWORD now = GetTickCount();
			if (now != m_spaceToggleTick) m_spaceToggleTick = now;
		}
		return TRUE;
	}
	if (msg == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE) {
		m_lookback = 0;
		return TRUE;
	}
	return FALSE;
}
void CSoft3DRaceDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_s3r == this) g_s3r = NULL;
	delete this;
}
void CSoft3DRaceDlg::LayoutHelpBtn() { CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help); }
static BOOL S3rPeekOwn(HWND root, MSG* msg, UINT fmin, UINT fmax)
{
	if (!root || !msg) return FALSE;
	MSG p = {};
	if (!::PeekMessage(&p, NULL, fmin, fmax, PM_NOREMOVE)) return FALSE;
	if (!p.hwnd || (p.hwnd != root && !::IsChild(root, p.hwnd))) return FALSE;
	return ::PeekMessage(msg, p.hwnd, p.message, p.message, PM_REMOVE) ? TRUE : FALSE;
}

void CSoft3DRaceDlg::PumpQueued(BOOL input)
{
	HWND root = GetSafeHwnd();
	if (!root) return;
	MSG msg;
	int n = 0;
	const int cap = input ? 18 : 10;
	while (n < cap) {
		if (!S3rPeekOwn(root, &msg, WM_PAINT, WM_PAINT)) {
			if (!input) break;
			if (!S3rPeekOwn(root, &msg, WM_MOUSEFIRST, WM_MOUSELAST)
				&& !S3rPeekOwn(root, &msg, WM_KEYFIRST, WM_KEYLAST)
				&& !S3rPeekOwn(root, &msg, WM_NCMOUSEMOVE, WM_NCMBUTTONDBLCLK))
				break;
		}
		if (msg.message == WM_QUIT) { ::PostQuitMessage((int)msg.wParam); return; }
		if (HandleAccelMessage(&msg)) { n++; continue; }
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
		if (!::IsWindow(root)) return;
		n++;
	}
}
void CSoft3DRaceDlg::LayoutAll()
{
	if (!GetSafeHwnd() || !m_view.GetSafeHwnd()) return;
	CRect rc; GetClientRect(&rc);
	const int cx = rc.Width(), cy = rc.Height();
	if (cx < 220 || cy < 200) return;
	int capH = CCC_GetCustomCaptionHeight(m_hWnd); if (capH < 0) capH = 0;
	const int m = 10, rowH = 36, btnH = 32, labH = 22;
	int y = capH + 8;
	float s = (float)(cx - 2 * m) / 920.f;
	if (s < 0.70f) s = 0.70f;
	if (s > 1.55f) s = 1.55f;
	auto sw = [&](int w)->int { return max(28, (int)((float)w * s + .5f)); };
	const int gap = max(4, (int)(6.f * s + .5f));
	int x = m;
	auto placeL = [&](CWnd& w, int ww){ if(w.GetSafeHwnd()) w.SetWindowPos(NULL,x,y+6,ww,labH,SWP_NOZORDER|SWP_NOACTIVATE); x+=ww+2; };
	auto placeC = [&](CWnd& w, int ww){ if(w.GetSafeHwnd()) w.SetWindowPos(NULL,x,y,ww,280,SWP_NOZORDER|SWP_NOACTIVATE); x+=ww+gap; };
	auto placeB = [&](CWnd& w, int ww){ if(w.GetSafeHwnd()) w.SetWindowPos(NULL,x,y-1,ww,34,SWP_NOZORDER|SWP_NOACTIVATE); x+=ww+gap; };
	placeL(m_aiL, sw(36)); placeC(m_ai, sw(100));
	placeL(m_oppL, sw(36)); placeC(m_opp, sw(64));
	placeL(m_lenL, sw(36)); placeC(m_len, sw(84));
	placeL(m_lapsL, sw(36)); placeC(m_laps, sw(64));
	placeL(m_themeL, sw(40)); placeC(m_theme, sw(110));
	placeL(m_invertL, sw(36)); placeC(m_invert, sw(100));
	y += rowH + 4;
	x = m;
	placeB(m_start, sw(80)); placeB(m_gen, sw(80));
	placeL(m_meshL, sw(40)); placeC(m_mesh, sw(92));
	if (m_hint.GetSafeHwnd()) m_hint.SetWindowPos(NULL, x, y+6, max(40, cx-x-m), labH, SWP_NOZORDER|SWP_NOACTIVATE);
	y += rowH + 6;
	const int btnY = cy - m - btnH;
	int viewBottom = btnY - 10; if (viewBottom < y + 80) viewBottom = y + 80;
	m_view.SetWindowPos(NULL, m, y, max(40, cx-2*m), max(40, viewBottom-y), SWP_NOZORDER|SWP_NOACTIVATE);
	const int closeW = sw(100);
	if (m_close.GetSafeHwnd()) m_close.SetWindowPos(NULL, cx-m-closeW, btnY, closeW, btnH, SWP_NOZORDER|SWP_NOACTIVATE);
	if (m_status.GetSafeHwnd()) m_status.SetWindowPos(NULL, m, btnY+6, max(40, cx-m-closeW-14-m), 20, SWP_NOZORDER|SWP_NOACTIVATE);
	if (m_ai.GetSafeHwnd()) {int v=savedata.s3r_ai; if(v<0)v=0; if(v>4)v=4; m_ai.CComboBox::SetCurSel(v);}
	if (m_opp.GetSafeHwnd()) { int o=savedata.s3r_opponents; if(o<1)o=1; if(o>11)o=11; m_opp.CComboBox::SetCurSel(o-1); }
	if (m_len.GetSafeHwnd()) {int v=savedata.s3r_len; if(v<0)v=0; if(v>3)v=3; m_len.CComboBox::SetCurSel(v);}
	if (m_laps.GetSafeHwnd()) {int v=savedata.s3r_laps; if(v<0)v=0; if(v>10)v=10; m_laps.CComboBox::SetCurSel(v);}
	if (m_theme.GetSafeHwnd()) {int v=savedata.s3r_theme; if(v<0)v=0; if(v>8)v=8; m_theme.CComboBox::SetCurSel(v);}
	if (m_invert.GetSafeHwnd()) {int v=savedata.s3r_invert_y?1:0; m_invert.CComboBox::SetCurSel(v);}
	if (m_mesh.GetSafeHwnd()) m_mesh.CComboBox::SetCurSel(S3MeshDensityLevel());
	if (m_start.GetSafeHwnd()) m_start.Invalidate();
	if (m_gen.GetSafeHwnd()) m_gen.Invalidate();
	CWnd* raise[] = { &m_aiL,&m_ai,&m_oppL,&m_opp,&m_lenL,&m_len,&m_lapsL,&m_laps,&m_themeL,&m_theme,&m_invertL,&m_invert,&m_start,&m_gen,&m_meshL,&m_mesh,&m_hint,&m_close,&m_status };
	for (int i = 0; i < (int)(sizeof(raise)/sizeof(raise[0])); i++) {
		if (raise[i]->GetSafeHwnd())
			raise[i]->SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
	}
	CCC_CaptionLayout(m_hWnd); LayoutHelpBtn();
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
}

int CSoft3DRaceDlg::ReadAiFromUi(){ int s=m_ai.GetSafeHwnd()?(int)m_ai.CComboBox::GetCurSel():savedata.s3r_ai; if(s<0||s>=AI_COUNT)s=AI_NORMAL; return s; }
void CSoft3DRaceDlg::SetAiToUi(int v){ if(v<0)v=0; if(v>=AI_COUNT)v=AI_COUNT-1; savedata.s3r_ai=v; if(m_ai.GetSafeHwnd()) m_ai.CComboBox::SetCurSel(v); }
int CSoft3DRaceDlg::ReadOppFromUi(){ int s=m_opp.GetSafeHwnd()?(int)m_opp.CComboBox::GetCurSel()+1:savedata.s3r_opponents; if(s<1)s=1; if(s>11)s=11; return s; }
void CSoft3DRaceDlg::SetOppToUi(int v){ if(v<1)v=1; if(v>11)v=11; savedata.s3r_opponents=v; if(m_opp.GetSafeHwnd()) m_opp.CComboBox::SetCurSel(v-1); }
int CSoft3DRaceDlg::ReadLenFromUi(){ int s=m_len.GetSafeHwnd()?(int)m_len.CComboBox::GetCurSel():savedata.s3r_len; if(s<0||s>3)s=0; return s; }
void CSoft3DRaceDlg::SetLenToUi(int v){ if(v<0)v=0; if(v>3)v=3; savedata.s3r_len=v; if(m_len.GetSafeHwnd()) m_len.CComboBox::SetCurSel(v); }
int CSoft3DRaceDlg::ReadLapsFromUi(){ int s=m_laps.GetSafeHwnd()?(int)m_laps.CComboBox::GetCurSel():savedata.s3r_laps; if(s<0||s>10)s=0; return s; }
void CSoft3DRaceDlg::SetLapsToUi(int v){ if(v<0)v=0; if(v>10)v=10; savedata.s3r_laps=v; if(m_laps.GetSafeHwnd()) m_laps.CComboBox::SetCurSel(v); }
int CSoft3DRaceDlg::ReadThemeFromUi(){ int s=m_theme.GetSafeHwnd()?(int)m_theme.CComboBox::GetCurSel():savedata.s3r_theme; if(s<0||s>8)s=0; return s; }
void CSoft3DRaceDlg::SetThemeToUi(int v){ if(v<0)v=0; if(v>8)v=8; savedata.s3r_theme=v; if(m_theme.GetSafeHwnd()) m_theme.CComboBox::SetCurSel(v); }
int CSoft3DRaceDlg::ReadInvertFromUi(){ int s=m_invert.GetSafeHwnd()?(int)m_invert.CComboBox::GetCurSel():savedata.s3r_invert_y; return (s!=0)?1:0; }
void CSoft3DRaceDlg::SetInvertToUi(int v){ v=v?1:0; savedata.s3r_invert_y=v; if(m_invert.GetSafeHwnd()) m_invert.CComboBox::SetCurSel(v); }
int CSoft3DRaceDlg::ReadMeshFromUi(){ int s=m_mesh.GetSafeHwnd()?(int)m_mesh.CComboBox::GetCurSel():savedata.s3_mesh_density; if(s<0||s>S3_MESH_DENSITY_MAX)s=5; return s; }
void CSoft3DRaceDlg::SetMeshToUi(int v){ if(v<0||v>S3_MESH_DENSITY_MAX)v=5; savedata.s3_mesh_density=v; if(m_mesh.GetSafeHwnd()) m_mesh.CComboBox::SetCurSel(v); }

void CSoft3DRaceDlg::PersistUi()
{
	savedata.s3r_ai = ReadAiFromUi();
	savedata.s3r_opponents = ReadOppFromUi();
	savedata.s3r_len = ReadLenFromUi();
	savedata.s3r_laps = ReadLapsFromUi();
	savedata.s3r_theme = ReadThemeFromUi();
	savedata.s3r_invert_y = ReadInvertFromUi();
	savedata.s3_mesh_density = ReadMeshFromUi();
	if (savedata.s3r_show_map != 0) savedata.s3r_show_map = 1;
	int mask = savedata.s3r_item_mask; if (mask <= 0) mask = ITEM_ALL; savedata.s3r_item_mask = mask & ITEM_ALL;
	if (savedata.s3r_zoom < 50 || savedata.s3r_zoom > 250) savedata.s3r_zoom = 100;
	MpPersistSavedataQuick();
}
void CSoft3DRaceDlg::PersistWindowRect()
{
	// Create 直後(未表示)の OnSize で既定サイズを .dat に書いて復元値を潰さない
	if (!GetSafeHwnd() || !IsWindowVisible() || IsIconic()) return;
	WINDOWPLACEMENT wp = {};
	wp.length = sizeof(wp);
	if (!GetWindowPlacement(&wp)) return;
	const RECT& r = wp.rcNormalPosition;
	const int w = r.right - r.left;
	const int h = r.bottom - r.top;
	if (w < 320 || h < 240) return;
	savedata.s3r_win_x = r.left;
	savedata.s3r_win_y = r.top;
	savedata.s3r_win_w = w;
	savedata.s3r_win_h = h;
	MpPersistSavedataQuick();
}
void CSoft3DRaceDlg::ApplySavedWindowRect()
{
	if (!GetSafeHwnd()) return;
	int w = savedata.s3r_win_w, h = savedata.s3r_win_h;
	int x = savedata.s3r_win_x, y = savedata.s3r_win_y;
	if (w < 320 || h < 240) return;
	RECT rr = { x, y, x + w, y + h };
	HMONITOR mon = MonitorFromRect(&rr, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	if (mon && GetMonitorInfo(mon, &mi)) {
		const RECT& wa = mi.rcWork;
		if (w > wa.right - wa.left) w = wa.right - wa.left;
		if (h > wa.bottom - wa.top) h = wa.bottom - wa.top;
		if (x < wa.left) x = wa.left;
		if (y < wa.top) y = wa.top;
		if (x + w > wa.right) x = wa.right - w;
		if (y + h > wa.bottom) y = wa.bottom - h;
	}
	SetWindowPos(NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}
void CSoft3DRaceDlg::CaptureAudioBaseline()
{
	m_baseTempoPos = tempo; m_basePitchPos = pitch;
	if (og && ::IsWindow(og->GetSafeHwnd())) { m_baseTempoPos = og->m_tempo_sl.GetPos(); m_basePitchPos = og->m_pitch_sl.GetPos(); }
}
void CSoft3DRaceDlg::RestoreAudioBaseline()
{
	// 動画(mode=-2)はテンポ／ピッチ変更で再生速度が動くので触らない（見た目バフだけ戻す）
	if (mode != -2) {
		if (mp && ::IsWindow(mp->GetSafeHwnd())) mp->ApplyPracticeTempoPercent(m_baseTempoPos / 2);
		else { tempo = m_baseTempoPos; if (og && ::IsWindow(og->GetSafeHwnd())) og->m_tempo_sl.SetPos(m_baseTempoPos); }
		S3rSetPitchPos(m_basePitchPos);
	}
	m_reverbFogBoost = 0; m_eqDofBoost = 0;
}
int CSoft3DRaceDlg::ItemMask() const
{
	int m = savedata.s3r_item_mask; if (m <= 0) m = ITEM_ALL; return m & ITEM_ALL;
}
float CSoft3DRaceDlg::CourseScale() const
{
	int L = savedata.s3r_len; if (L == LEN_AUTO) L = LEN_NORMAL;
	if (L == LEN_SHORT) return 0.85f; if (L == LEN_LONG) return 1.45f; return 1.1f;
}
float CSoft3DRaceDlg::EffectiveLaps() const
{
	int laps = savedata.s3r_laps;
	if (laps > 0) return (float)laps;
	int L = savedata.s3r_len; if (L == LEN_AUTO) L = LEN_NORMAL;
	if (L == LEN_LONG) return 2.f; return 3.f;
}
int CSoft3DRaceDlg::EffectiveTheme() const
{
	int t = savedata.s3r_theme;
	if (t >= THEME_FOREST && t <= THEME_CLOUD) return t;
	return 1 + (int)(m_genSeed % 8u);
}
float CSoft3DRaceDlg::BandHalfWidth() const { return m_bandHalf; }
float CSoft3DRaceDlg::SpeedScale() const
{
	// コース規模に比例（通常≈1.1）
	return CourseScale();
}
float CSoft3DRaceDlg::RaceSpeedCap(int boosted) const
{
	// 物理上限（表示は×20で空飛ぶ体感の km/h）
	float u = 125.f / 3.6f;
	u *= (0.96f + 0.08f * (CourseScale() / 1.1f));
	if (boosted) u *= 1.12f;
	return u;
}
float CSoft3DRaceDlg::NitroSpeedMul(const S3rCraft& c) const
{
	if (c.nitroT <= 0.f) return 1.f;
	// 自機 +30%。CPU は難易度で近づける（強烈はほぼ同等）
	float excess = 0.30f;
	if (!c.isPlayer) {
		if (m_aiRaceLv >= AI_FEROCIOUS) excess *= 0.92f;
		else if (m_aiRaceLv >= AI_HARD) excess *= 0.80f;
		else if (m_aiRaceLv >= AI_NORMAL) excess *= 0.72f;
		else excess *= (2.f / 3.f);
	}
	return 1.f + excess;
}
float CSoft3DRaceDlg::NitroThrustMul(const S3rCraft& c) const
{
	if (c.nitroT <= 0.f) return 1.f;
	float excess = 0.38f;
	if (!c.isPlayer) {
		if (m_aiRaceLv >= AI_FEROCIOUS) excess *= 0.92f;
		else if (m_aiRaceLv >= AI_HARD) excess *= 0.80f;
		else if (m_aiRaceLv >= AI_NORMAL) excess *= 0.72f;
		else excess *= (2.f / 3.f);
	}
	return 1.f + excess;
}
BOOL CSoft3DRaceDlg::TryUseNitro(S3rCraft& c)
{
	if (!c.alive || c.finished || c.retired) return FALSE;
	if (c.nitroStock <= 0 || c.nitroT > 0.05f) return FALSE;
	if (m_phase != PHASE_RACE) return FALSE;
	c.nitroStock--;
	c.nitroT = 2.85f;
	m_standDirty = 1;
	SpawnFxBurst(FX_EMBER, c.x, c.y + 0.15f, c.z,
		-sinf(c.yaw)*cosf(c.pitch), -sinf(c.pitch), -cosf(c.yaw)*cosf(c.pitch),
		12, 8.5f, 0.35f, 0.85f, 1.f);
	if (c.isPlayer)
		Soft3DSfxUi(S3SFX_BOOST, 0);
	else
		Soft3DSfxOneShot(S3SFX_BOOST, c.x, c.y, c.z);
	return TRUE;
}
float CSoft3DRaceDlg::SpeedToKmh(float vx, float vy, float vz) const
{
	// 表示のみ20倍（≈80→1600 km/h 帯）
	return sqrtf(vx * vx + vy * vy + vz * vz) * 3.6f * 20.f;
}
static float S3rEngPower(float rpm, float thr)
{
	if (rpm < 0.f) rpm = 0.f;
	if (rpm > 1.f) rpm = 1.f;
	if (thr < 0.f) thr = 0.f;
	if (thr > 1.f) thr = 1.f;
	float p = 0.16f + rpm * rpm * (0.95f + 0.22f * rpm);
	return p * thr;
}
void CSoft3DRaceDlg::TickEngine(S3rCraft& c, float thrIn, int brake, float dt)
{
	if (thrIn < 0.f) thrIn = 0.f;
	if (thrIn > 1.f) thrIn = 1.f;
	if (brake) thrIn = 0.f;
	if (c.fuel <= 0.01f) thrIn = 0.f;
	else if (c.fuel < 5.f) thrIn *= 0.25f;
	float thrRate = (thrIn > c.throttle) ? 3.15f : 5.4f;
	c.throttle = S3rLerp(c.throttle, thrIn, min(1.f, thrRate * dt));
	const float idle = 0.08f;
	float spd = sqrtf(c.vx * c.vx + c.vy * c.vy + c.vz * c.vz);
	float cap = RaceSpeedCap(c.boostT > 0.f ? 1 : 0) * NitroSpeedMul(c);
	float load = S3rSaturate(spd / max(1.f, cap));
	float rpmT = idle + c.throttle * (0.92f - idle);
	if (c.throttle < 0.10f)
		rpmT = S3rLerp(idle, idle + load * 0.48f, 0.70f);
	if (c.boostT > 0.f) rpmT = min(1.f, rpmT + 0.07f);
	if (c.nitroT > 0.f) rpmT = min(1.f, rpmT + (c.isPlayer ? 0.14f : 0.09f));
	if (c.slowT > 0.f) rpmT *= 0.72f;
	float rpmRate = (rpmT > c.rpm) ? 2.45f : 3.55f;
	if (brake) rpmRate = 7.2f;
	c.rpm = S3rLerp(c.rpm, rpmT, min(1.f, rpmRate * dt));
	if (c.rpm < idle * 0.6f) c.rpm = idle * 0.6f;
	if (c.rpm > 1.f) c.rpm = 1.f;
}
float CSoft3DRaceDlg::BandSpeedFactor(float lat, float vert) const
{
	// コース帯の横軸（binormal）中央＝MAX。上下（normal）に離れるほど失速
	const float half = max(0.01f, BandHalfWidth());
	const float latN = fabsf(lat) / half;
	const float vertN = fabsf(vert) / (half * 0.85f);
	float f = 1.f - latN * 0.16f - vertN * 0.34f;
	if (latN < 0.28f && vertN < 0.20f) {
		const float sweet = (1.f - latN / 0.28f) * (1.f - vertN / 0.20f);
		f += sweet * 0.10f; // 中央やや上乗せ
	}
	if (f < 0.58f) f = 0.58f;
	if (f > 1.08f) f = 1.08f;
	return f;
}
float CSoft3DRaceDlg::PathArcBetween(float t0, float t1) const
{
	if (m_pathLen < 1.f) return 0.f;
	while (t0 < 0.f) t0 += 1.f; while (t0 >= 1.f) t0 -= 1.f;
	while (t1 < 0.f) t1 += 1.f; while (t1 >= 1.f) t1 -= 1.f;
	int i0 = (int)(t0 * (float)S3R_PATH_SAMPLES) % S3R_PATH_SAMPLES; if (i0 < 0) i0 += S3R_PATH_SAMPLES;
	int i1 = (int)(t1 * (float)S3R_PATH_SAMPLES) % S3R_PATH_SAMPLES; if (i1 < 0) i1 += S3R_PATH_SAMPLES;
	float L0 = m_pathCumLen[i0], L1 = m_pathCumLen[i1];
	if (i1 >= i0) return max(0.f, L1 - L0);
	return max(0.f, (m_pathLen - L0) + L1);
}
void CSoft3DRaceDlg::RespawnCraftToCheckpoint(S3rCraft& c, float fuelAmt, float cool)
{
	// 常に帯中央へ（端座標復帰が COURSE OUT 連発の主因だった）
	c.aiCutT = -1.f; c.aiCutTimer = 0.f;
	c.aiCutCool = max(c.aiCutCool, 18.f); // 復帰後は通常ライン固定
	c.pathT = c.chkPathT;
	float cx,cy,cz,tx,ty,tz,nx,ny,nz,bx,by,bz;
	SplineFrame(c.pathT, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
	c.x = cx; c.y = cy; c.z = cz;
	c.yaw = atan2f(tx, tz);
	c.pitch = S3rClamp(asinf(S3rClamp(ty, -1.f, 1.f)), -0.55f, 0.55f);
	float keep = 16.f / 3.6f;
	c.vx = tx * keep; c.vy = ty * keep; c.vz = tz * keep;
	c.throttle = 0.f; c.rpm = 0.08f;
	c.fuel = fuelAmt;
	c.offBandT = 0.f;
	c.offBand = 0;
	c.courseOutCool = cool;
	c.chkX = cx; c.chkY = cy; c.chkZ = cz;
	c.chkYaw = c.yaw; c.chkPitch = c.pitch;
}
void CSoft3DRaceDlg::AbortAiToLine(S3rCraft& c, float lineLockSec)
{
	// 失敗カット等：COURSE OUT まで落とさず帯中央へ戻してライン走行へ
	c.aiCutT = -1.f; c.aiCutTimer = 0.f;
	c.aiCutCool = max(c.aiCutCool, lineLockSec);
	float cx,cy,cz,tx,ty,tz,nx,ny,nz,bx,by,bz;
	SplineFrame(c.pathT, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
	c.x = cx; c.y = cy; c.z = cz;
	c.yaw = atan2f(tx, tz);
	c.pitch = S3rClamp(asinf(S3rClamp(ty, -1.f, 1.f)), -0.55f, 0.55f);
	float keep = RaceSpeedCap(0) * 0.55f;
	c.vx = tx * keep; c.vy = ty * keep; c.vz = tz * keep;
	c.throttle = 0.f; c.rpm = 0.08f;
	c.fuel = max(c.fuel, 75.f);
	c.offBand = 0; c.offBandT = 0.f;
	c.courseOutCool = max(c.courseOutCool, 0.85f);
	c.chkX = cx; c.chkY = cy; c.chkZ = cz;
	c.chkYaw = c.yaw; c.chkPitch = c.pitch; c.chkPathT = c.pathT;
}

void CSoft3DRaceDlg::SplinePoint(float t, float& x, float& y, float& z) const
{
	if (m_knotN < 4) { x=y=z=0; return; }
	while (t < 0.f) t += 1.f; while (t >= 1.f) t -= 1.f;
	const float ft = t * (float)m_knotN;
	int i1 = (int)floorf(ft) % m_knotN; if (i1 < 0) i1 += m_knotN;
	int i0 = (i1 - 1 + m_knotN) % m_knotN;
	int i2 = (i1 + 1) % m_knotN;
	int i3 = (i1 + 2) % m_knotN;
	float u = ft - floorf(ft), u2=u*u, u3=u2*u;
	auto cr = [&](float p0,float p1,float p2,float p3)->float {
		return 0.5f*((2.f*p1)+(-p0+p2)*u+(2.f*p0-5.f*p1+4.f*p2-p3)*u2+(-p0+3.f*p1-3.f*p2+p3)*u3);
	};
	x = cr(m_knots[i0].x,m_knots[i1].x,m_knots[i2].x,m_knots[i3].x);
	y = cr(m_knots[i0].y,m_knots[i1].y,m_knots[i2].y,m_knots[i3].y);
	z = cr(m_knots[i0].z,m_knots[i1].z,m_knots[i2].z,m_knots[i3].z);
}
void CSoft3DRaceDlg::SplineTangent(float t, float& x, float& y, float& z) const
{
	float x0,y0,z0,x1,y1,z1; SplinePoint(t-0.002f,x0,y0,z0); SplinePoint(t+0.002f,x1,y1,z1);
	x=x1-x0; y=y1-y0; z=z1-z0; S3rNorm3(x,y,z);
}
void CSoft3DRaceDlg::SplineFrame(float t, float& px, float& py, float& pz, float& tx, float& ty, float& tz, float& nx, float& ny, float& nz, float& bx, float& by, float& bz) const
{
	SplinePoint(t,px,py,pz); SplineTangent(t,tx,ty,tz);
	float upx=0,upy=1,upz=0;
	bx = ty*upz - tz*upy; by = tz*upx - tx*upz; bz = tx*upy - ty*upx;
	if (bx*bx+by*by+bz*bz < 1e-6f) { upx=1; upy=0; upz=0; bx=ty*upz-tz*upy; by=tz*upx-tx*upz; bz=tx*upy-ty*upx; }
	S3rNorm3(bx,by,bz);
	nx = by*tz - bz*ty; ny = bz*tx - bx*tz; nz = bx*ty - by*tx; S3rNorm3(nx,ny,nz);
}
float CSoft3DRaceDlg::ClosestSplineT(float x, float y, float z, float hintT) const
{
	float bestT = hintT, bestD = 1e30f;
	// 狭い局所窓のみ（広い前方探索は pathT ジャンプ＝誤ラップの温床）
	int start = (int)(hintT * S3R_PATH_SAMPLES) - 36;
	for (int k = 0; k < 72; k++) {
		int i = start + k; while (i < 0) i += S3R_PATH_SAMPLES; i %= S3R_PATH_SAMPLES;
		float dx=x-m_pathSampleXYZ[i][0], dy=y-m_pathSampleXYZ[i][1], dz=z-m_pathSampleXYZ[i][2];
		float d=dx*dx+dy*dy+dz*dz; if (d < bestD) { bestD=d; bestT=m_pathSampleT[i]; }
	}
	float refineBestT = bestT, refineBestD = bestD;
	for (int k = -8; k <= 8; k++) {
		float t = bestT + (float)k * (0.5f / (float)S3R_PATH_SAMPLES);
		float px,py,pz; SplinePoint(t,px,py,pz);
		float dx=x-px, dy=y-py, dz=z-pz;
		float d=dx*dx+dy*dy+dz*dz;
		if (d < refineBestD) { refineBestD=d; refineBestT=t; }
	}
	while (refineBestT < 0.f) refineBestT += 1.f;
	while (refineBestT >= 1.f) refineBestT -= 1.f;
	return refineBestT;
}
float CSoft3DRaceDlg::AdvancePathT(float x, float y, float z, float prevT, float spd, float dt) const
{
	float raw = ClosestSplineT(x, y, z, prevT);
	float d = raw - prevT;
	while (d > 0.5f) d -= 1.f;
	while (d < -0.5f) d += 1.f;
	float plen = (m_pathLen > 1.f) ? m_pathLen : 800.f;
	// 実移動距離ベースのみ。大きな追いつきは禁止（ワープ周回の主因だった）
	float maxFwd = (spd * dt + 2.5f) / plen;
	if (maxFwd < 0.0008f) maxFwd = 0.0008f;
	if (maxFwd > 0.02f) maxFwd = 0.02f;
	const float maxBack = 0.006f;
	if (d > maxFwd) d = maxFwd;
	if (d < -maxBack) d = -maxBack;
	float t = prevT + d;
	while (t < 0.f) t += 1.f;
	while (t >= 1.f) t -= 1.f;
	return t;
}
float CSoft3DRaceDlg::DistToBand(float x, float y, float z, float t, float& outCx, float& outCy, float& outCz) const
{
	SplinePoint(t, outCx, outCy, outCz);
	float dx=x-outCx, dy=y-outCy, dz=z-outCz;
	return sqrtf(dx*dx+dy*dy+dz*dz);
}
void CSoft3DRaceDlg::BandLocal(float x, float y, float z, float t, float& lat, float& vert, float& cx, float& cy, float& cz) const
{
	float tx,ty,tz,nx,ny,nz,bx,by,bz;
	SplineFrame(t, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
	float ox=x-cx, oy=y-cy, oz=z-cz;
	lat = ox*bx + oy*by + oz*bz;
	vert = ox*nx + oy*ny + oz*nz;
}
float CSoft3DRaceDlg::GroundY(float x, float z) const
{
	if (!m_hmReady || m_hmStep < 1e-4f) return 8.f;
	float u = (x - m_hmX0) / m_hmStep;
	float v = (z - m_hmZ0) / m_hmStep;
	int i = (int)floorf(u);
	int j = (int)floorf(v);
	float fu = u - (float)i;
	float fv = v - (float)j;
	if (i < 0) { i = 0; fu = 0.f; }
	if (j < 0) { j = 0; fv = 0.f; }
	if (i >= S3R_HM_N - 1) { i = S3R_HM_N - 2; fu = 1.f; }
	if (j >= S3R_HM_N - 1) { j = S3R_HM_N - 2; fv = 1.f; }
	const float h00 = m_hm[j * S3R_HM_N + i];
	const float h10 = m_hm[j * S3R_HM_N + i + 1];
	const float h01 = m_hm[(j + 1) * S3R_HM_N + i];
	const float h11 = m_hm[(j + 1) * S3R_HM_N + i + 1];
	const float hx0 = h00 + (h10 - h00) * fu;
	const float hx1 = h01 + (h11 - h01) * fu;
	return hx0 + (hx1 - hx0) * fv;
}
float CSoft3DRaceDlg::RawGroundY(float x, float z) const
{
	if (!m_hmReady || m_hmStep < 1e-4f) return 8.f;
	float u = (x - m_hmX0) / m_hmStep;
	float v = (z - m_hmZ0) / m_hmStep;
	int i = (int)floorf(u);
	int j = (int)floorf(v);
	float fu = u - (float)i;
	float fv = v - (float)j;
	if (i < 0) { i = 0; fu = 0.f; }
	if (j < 0) { j = 0; fv = 0.f; }
	if (i >= S3R_HM_N - 1) { i = S3R_HM_N - 2; fu = 1.f; }
	if (j >= S3R_HM_N - 1) { j = S3R_HM_N - 2; fv = 1.f; }
	const float h00 = m_hmRaw[j * S3R_HM_N + i];
	const float h10 = m_hmRaw[j * S3R_HM_N + i + 1];
	const float h01 = m_hmRaw[(j + 1) * S3R_HM_N + i];
	const float h11 = m_hmRaw[(j + 1) * S3R_HM_N + i + 1];
	const float hx0 = h00 + (h10 - h00) * fu;
	const float hx1 = h01 + (h11 - h01) * fu;
	return hx0 + (hx1 - hx0) * fv;
}

void CSoft3DRaceDlg::GenerateCourse()
{
	m_genSeed = GetTickCount() ^ (m_rng * 2654435761u);
	GenerateCourseWithSeed(m_genSeed);
}
void CSoft3DRaceDlg::GenerateCourseWithSeed(DWORD seed)
{
	m_genSeed = seed ? seed : 1; m_rng = m_genSeed;
	PersistUi();
	m_themeActive = EffectiveTheme();
	m_lapsTarget = (int)EffectiveLaps();
	const float sc = CourseScale();
	int knots = 56;
	int L = savedata.s3r_len; if (L == LEN_AUTO) L = LEN_NORMAL;
	if (L == LEN_SHORT) knots = 44; else if (L == LEN_LONG) knots = 80;
	if (knots > S3R_SPLINE_MAX) knots = S3R_SPLINE_MAX;
	m_knotN = knots;
	m_bandHalf = 7.2f * sc;
	m_hmReady = 0;
	m_carveN = 0;
	// 旧 GPU メッシュは BakeStaticMeshes が上書きするまで残す（再生成で地表が消えない）
	// --- 平面レイアウト（XZ）：楕円／八の字／凸／凹を抽選 ---
	{
		float lr = S3rRand01(m_rng);
		int layout;
		if (lr < 0.16f) layout = 1;       // 八の字（交差は段差）
		else if (lr < 0.32f) layout = 2;  // スタジアム（凸）
		else if (lr < 0.46f) layout = 3;  // 箱型スーパー楕円（凸）
		else if (lr < 0.62f) layout = 4;  // 腎臓型（凹）
		else if (lr < 0.76f) layout = 5;  // ピーナッツ（くびれ凹）
		else if (lr < 0.88f) layout = 6;  // 丸三角（凸）
		else layout = 0;                  // 楕円＋ゆらぎ
		m_layoutKind = layout;
		float rot = S3rRand01(m_rng) * (float)(M_PI * 2.0);
		float cr = cosf(rot), sr = sinf(rot);
		float pA = S3rRand01(m_rng), pB = S3rRand01(m_rng), pC = S3rRand01(m_rng);
		float ax = (168.f + 22.f * pA) * sc;
		float az = (108.f + 28.f * pB) * sc;
		float nBox = 3.15f + 1.85f * pC;
		float limaB = (0.38f + 0.12f * pA) * ax;
		for (int i = 0; i < knots; i++) {
			float a = (float)i / (float)knots * (float)(M_PI * 2.0);
			float lx = 0.f, lz = 0.f;
			if (layout == 1) {
				lx = ax * sinf(a);
				lz = ax * sinf(a) * cosf(a);
			} else if (layout == 2) {
				float ca = cosf(a), sa = sinf(a);
				float u = 1.f - 0.28f * fabsf(sinf(2.f * a));
				lx = ca * ax * u;
				lz = sa * az * u;
			} else if (layout == 3) {
				float ca = cosf(a), sa = sinf(a);
				float px = powf(max(1e-6f, fabsf(ca)), 2.f / nBox);
				float pz = powf(max(1e-6f, fabsf(sa)), 2.f / nBox);
				lx = ((ca >= 0.f) ? px : -px) * ax;
				lz = ((sa >= 0.f) ? pz : -pz) * az;
			} else if (layout == 4) {
				float r = ax * 0.62f + limaB * cosf(a);
				lx = r * cosf(a);
				lz = r * sinf(a) * 0.88f;
			} else if (layout == 5) {
				float r = (148.f + 52.f * cosf(2.f * a)) * sc;
				lx = r * cosf(a);
				lz = r * sinf(a) * 0.70f;
			} else if (layout == 6) {
				float r = (150.f + 44.f * cosf(3.f * a + pB * 0.8f)) * sc;
				lx = r * cosf(a);
				lz = r * sinf(a);
			} else {
				float rad = (155.f + 38.f * sinf(a * 2.f + pA * 2.f)) * sc;
				float wob = 22.f * sc * sinf(a * 2.4f + pB * 1.5f);
				if (m_themeActive == THEME_FOREST) { rad *= 0.96f; wob *= 1.45f; }
				else if (m_themeActive == THEME_MESA) { rad *= 0.90f + 0.18f * fabsf(cosf(a * 2.f)); wob *= 0.72f; }
				else if (m_themeActive == THEME_OIL) { rad += 12.f * sc * ((int)(a * 5.1f) % 2 ? 1.f : -0.4f); wob *= 0.55f; }
				else if (m_themeActive == THEME_UNDER) { wob *= 1.25f; rad *= 0.94f + 0.08f * sinf(a * 3.f); }
				else if (m_themeActive == THEME_RUINS) { wob *= 1.15f; }
				else if (m_themeActive == THEME_CLOUD) { rad *= 1.06f; wob *= 1.2f; }
				lx = cosf(a) * rad + cosf(a * 1.7f) * wob;
				lz = sinf(a) * rad + sinf(a * 1.3f) * wob;
				if (m_themeActive == THEME_NIGHT) {
					lx += 10.f * sc * sinf(a * 5.f);
					lz += 8.f * sc * cosf(a * 4.f);
				}
			}
			if (layout != 0) {
				float wob = 8.f * sc * sinf(a * 2.2f + pC * 2.f);
				lx += cosf(a * 1.6f) * wob * 0.35f;
				lz += sinf(a * 1.4f) * wob * 0.35f;
			}
			m_knots[i].x = lx * cr - lz * sr;
			m_knots[i].z = lx * sr + lz * cr;
			m_knots[i].y = 0.f;
		}
	}
	// 長いコースでも地形枠内に収める（距離差はノット数＝経路長で出す）
	{
		float cx = 0.f, cz = 0.f;
		for (int i = 0; i < knots; i++) { cx += m_knots[i].x; cz += m_knots[i].z; }
		const float invK = 1.f / (float)knots;
		cx *= invK; cz *= invK;
		float maxR = 1.f;
		for (int i = 0; i < knots; i++) {
			const float dx = m_knots[i].x - cx, dz = m_knots[i].z - cz;
			const float r = sqrtf(dx * dx + dz * dz);
			if (r > maxR) maxR = r;
		}
		const float arenaR = 220.f - m_bandHalf;
		if (arenaR > 40.f && maxR > arenaR) {
			const float fit = arenaR / maxR;
			for (int i = 0; i < knots; i++) {
				m_knots[i].x = cx + (m_knots[i].x - cx) * fit;
				m_knots[i].z = cz + (m_knots[i].z - cz) * fit;
			}
			maxR = arenaR;
		}
		// --- 高さマップ（丘・谷・川・山）。コースより先に焼く ---
		float extent = maxR + 72.f;
		if (extent < 240.f) extent = 240.f;
		if (extent > 460.f) extent = 460.f;
		m_hmX0 = cx - extent;
		m_hmZ0 = cz - extent;
		m_hmStep = (extent * 2.f) / (float)(S3R_HM_N - 1);
		DWORD nr = m_genSeed ^ 0x51EDC001u;
		float peakX[8], peakZ[8], peakA[8], peakW[8];
		int nPeak = 5 + (int)(S3rRand01(nr) * 3.99f); // 5..8
		if (nPeak > 8) nPeak = 8;
		for (int p = 0; p < nPeak; p++) {
			float pa = S3rRand01(nr) * (float)(M_PI * 2.0);
			float pr = (40.f + S3rRand01(nr) * (extent * 0.72f)) * (0.55f + 0.45f * S3rRand01(nr));
			peakX[p] = cx + cosf(pa) * pr;
			peakZ[p] = cz + sinf(pa) * pr;
			peakA[p] = (18.f + S3rRand01(nr) * 28.f) * sc;
			if (m_themeActive == THEME_MESA) peakA[p] *= 1.35f;
			if (m_themeActive == THEME_CLOUD) peakA[p] *= 0.85f;
			if (m_themeActive == THEME_OIL) peakA[p] *= 0.55f;
			peakW[p] = 28.f + S3rRand01(nr) * 42.f;
		}
		float riverA = S3rRand01(nr) * (float)(M_PI * 2.0);
		float riverOff = (S3rRand01(nr) - 0.5f) * 40.f * sc;
		m_waterY = 6.5f * sc;
		if (m_themeActive == THEME_UNDER) m_waterY = 2.5f * sc;
		if (m_themeActive == THEME_CLOUD) m_waterY = 14.f * sc;
		if (m_themeActive == THEME_MESA) m_waterY = 5.0f * sc;
		if (m_themeActive == THEME_NIGHT) m_waterY = 5.5f * sc;
		for (int jz = 0; jz < S3R_HM_N; jz++) {
			for (int ix = 0; ix < S3R_HM_N; ix++) {
				float x = m_hmX0 + (float)ix * m_hmStep;
				float z = m_hmZ0 + (float)jz * m_hmStep;
				unsigned h0 = (unsigned)(ix * 73856093) ^ (unsigned)(jz * 19349663) ^ (unsigned)(m_genSeed * 83492791);
				h0 = h0 * 1664525u + 1013904223u;
				unsigned h1 = h0 * 1664525u + 1013904223u;
				unsigned h2 = h1 * 1664525u + 1013904223u;
				float n0 = (h0 & 65535u) / 65535.f;
				float n1 = (h1 & 65535u) / 65535.f;
				float n2 = (h2 & 65535u) / 65535.f;
				float h = 8.f * sc
					+ 11.f * sc * sinf(x * 0.016f + n0) * cosf(z * 0.014f)
					+ 7.5f * sc * sinf((x + z) * 0.011f)
					+ 4.2f * sc * sinf(x * 0.033f) * cosf(z * 0.029f)
					+ (n0 + n1 * 0.5f - 0.75f) * 1.6f * sc;
				if (m_themeActive == THEME_FOREST) {
					h += 6.5f * sc * sinf(x * 0.028f) * cosf(z * 0.024f);
					h += 3.5f * sc * sinf((x - z) * 0.019f);
				} else if (m_themeActive == THEME_RUINS) {
					h += 5.5f * sc * fabsf(sinf(x * 0.022f)) + 3.2f * sc * fabsf(cosf(z * 0.020f));
				} else if (m_themeActive == THEME_OIL) {
					h = 7.f * sc + 3.2f * sc * fabsf(sinf(x * 0.04f)) + 2.0f * sc * n1;
				} else if (m_themeActive == THEME_NIGHT) {
					h += 3.8f * sc * sinf(x * 0.021f) + 2.4f * sc * cosf(z * 0.025f);
				} else if (m_themeActive == THEME_UNDER) {
					h = 4.f * sc + 9.f * sc * sinf(x * 0.018f) * cosf(z * 0.016f) + 5.f * sc * n2;
				} else if (m_themeActive == THEME_GRASS) {
					h += 8.5f * sc * sinf(x * 0.015f) * cosf(z * 0.013f);
				} else if (m_themeActive == THEME_MESA) {
					float plat = 0.5f + 0.5f * sinf(x * 0.012f) * cosf(z * 0.012f);
					if (plat < 0.f) plat = 0.f; if (plat > 1.f) plat = 1.f;
					plat = plat * plat * (3.f - 2.f * plat);
					h = 6.f * sc + plat * 32.f * sc + 2.5f * sc * n0;
				} else {
					h += 12.f * sc + 7.f * sc * sinf(x * 0.009f);
				}
				for (int p = 0; p < nPeak; p++) {
					float dx = x - peakX[p], dz = z - peakZ[p];
					float u2 = (dx * dx + dz * dz) / (peakW[p] * peakW[p]);
					if (u2 < 9.f) h += peakA[p] * expf(-0.5f * u2);
				}
				float rl = (x - cx) * cosf(riverA) + (z - cz) * sinf(riverA) + riverOff;
				float rw = fabsf(rl + 18.f * sc * sinf(((x - cx) * sinf(riverA) - (z - cz) * cosf(riverA)) * 0.012f));
				float riverW = (m_themeActive == THEME_MESA) ? 16.f * sc : 22.f * sc;
				if (rw < riverW) {
					float k = 1.f - rw / riverW;
					h -= k * k * ((m_themeActive == THEME_MESA) ? 22.f : 12.f) * sc;
				}
				if (h < 1.2f * sc) h = 1.2f * sc;
				int idx = jz * S3R_HM_N + ix;
				m_hm[idx] = h;
			}
		}
		{
			float tmp[S3R_HM_N * S3R_HM_N];
			for (int pass = 0; pass < 2; pass++) {
				for (int jz = 0; jz < S3R_HM_N; jz++) {
					for (int ix = 0; ix < S3R_HM_N; ix++) {
						float acc = 0.f; int n = 0;
						for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
							int x2 = ix + di, z2 = jz + dj;
							if (x2 < 0) x2 = 0; if (x2 > S3R_HM_N - 1) x2 = S3R_HM_N - 1;
							if (z2 < 0) z2 = 0; if (z2 > S3R_HM_N - 1) z2 = S3R_HM_N - 1;
							acc += m_hm[z2 * S3R_HM_N + x2];
							n++;
						}
						tmp[jz * S3R_HM_N + ix] = acc / (float)n;
					}
				}
				for (int i = 0; i < S3R_HM_N * S3R_HM_N; i++) m_hm[i] = tmp[i];
			}
			for (int i = 0; i < S3R_HM_N * S3R_HM_N; i++) m_hmRaw[i] = m_hm[i];
		}
		m_hmReady = 1;
	}
	// --- 高さ：地形に沿う＋急坂＋山はくりぬき ---
	{
		float gyK[S3R_SPLINE_MAX];
		float cruise[S3R_SPLINE_MAX];
		for (int i = 0; i < knots; i++) {
			gyK[i] = GroundY(m_knots[i].x, m_knots[i].z);
		}
		for (int i = 0; i < knots; i++) {
			float acc = 0.f; int n = 0;
			for (int d = -5; d <= 5; d++) {
				acc += gyK[(i + d + knots) % knots];
				n++;
			}
			cruise[i] = acc / (float)n;
		}
		float follow = 4.0f * sc;
		if (m_themeActive == THEME_CLOUD) follow = 5.4f * sc;
		if (m_themeActive == THEME_UNDER) follow = 3.4f * sc;
		if (m_themeActive == THEME_MESA) follow = 3.8f * sc;
		for (int i = 0; i < knots; i++) {
			float a = (float)i / (float)knots * (float)(M_PI * 2.0);
			float local = gyK[i] + follow + 1.6f * sc * sinf(a * 2.f);
			float region = cruise[i] + follow;
			// 鋭い峰はコースを上げず貫通（後でcarve）
			if (gyK[i] > cruise[i] + 8.f * sc) m_knots[i].y = region;
			else m_knots[i].y = local * 0.72f + region * 0.28f;
		}
		// 急坂 1〜2 箇所（地形の丘を登る）
		int nSteep = 1 + ((S3rRand01(m_rng) > 0.40f) ? 1 : 0);
		for (int s = 0; s < nSteep; s++) {
			int center = (int)(S3rRand01(m_rng) * (float)knots) % knots;
			float width = 2.2f + S3rRand01(m_rng) * 2.0f;
			float amp = (10.f + S3rRand01(m_rng) * 14.f) * sc;
			if (S3rRand01(m_rng) < 0.30f) amp = -amp;
			for (int i = 0; i < knots; i++) {
				int d = i - center;
				if (d > knots / 2) d -= knots;
				if (d < -knots / 2) d += knots;
				float u = (float)d / width;
				m_knots[i].y += amp * expf(-0.5f * u * u);
			}
		}
		{
			float tmp[S3R_SPLINE_MAX];
			for (int i = 0; i < knots; i++) {
				float ym = m_knots[(i - 1 + knots) % knots].y;
				float y0 = m_knots[i].y;
				float yp = m_knots[(i + 1) % knots].y;
				tmp[i] = ym * 0.12f + y0 * 0.76f + yp * 0.12f;
			}
			for (int i = 0; i < knots; i++) m_knots[i].y = tmp[i];
		}
		for (int i = 0; i < knots; i++) {
			float gy = gyK[i];
			if (m_knots[i].y < gy + 2.4f * sc && gy <= cruise[i] + 12.f * sc)
				m_knots[i].y = gy + 2.4f * sc;
		}
		// 八の字：交差を高低で分け、平面ショートカットを封じる（cos で t=0 と t=π が逆符号）
		if (m_layoutKind == 1) {
			float figH = 14.f * sc;
			for (int i = 0; i < knots; i++) {
				float a = (float)i / (float)knots * (float)(M_PI * 2.0);
				m_knots[i].y += figH * cosf(a);
			}
		}
	}
	m_camSmoothInit = 0;
	m_pathLen = 0.f;
	float px=0,py=0,pz=0; SplinePoint(0,px,py,pz);
	for (int i = 0; i < S3R_PATH_SAMPLES; i++) {
		float t = (float)i / (float)S3R_PATH_SAMPLES;
		float x,y,z; SplinePoint(t,x,y,z);
		m_pathSampleT[i] = t;
		m_pathSampleXYZ[i][0]=x; m_pathSampleXYZ[i][1]=y; m_pathSampleXYZ[i][2]=z;
		if (i > 0) {
			float dx=x-px,dy=y-py,dz=z-pz;
			m_pathLen += sqrtf(dx*dx+dy*dy+dz*dz);
		}
		m_pathCumLen[i] = m_pathLen; px=x;py=y;pz=z;
	}
	// コース帯に浅い溝＋山は円形トンネル（坑口を広げ、床は下半円）
	{
		for (int i = 0; i < S3R_HM_N * S3R_HM_N; i++) m_hmPathDist[i] = 1e8f;
		memset(m_pathDeep, 0, sizeof(m_pathDeep));
		const float grooveR = m_bandHalf * 1.85f;
		const float tunR0 = S3rTunR(m_bandHalf);
		const float outerR = max(grooveR, tunR0 * 1.42f);
		const int span = (int)ceilf(outerR / max(0.01f, m_hmStep)) + 2;
		for (int i = 0; i < S3R_PATH_SAMPLES; i++) {
			float qx = m_pathSampleXYZ[i][0], qy = m_pathSampleXYZ[i][1], qz = m_pathSampleXYZ[i][2];
			float gq = 0.f;
			{
				float u = (qx - m_hmX0) / m_hmStep, v = (qz - m_hmZ0) / m_hmStep;
				int ii = (int)floorf(u), jj = (int)floorf(v);
				float fu = u - (float)ii, fv = v - (float)jj;
				if (ii < 0) { ii = 0; fu = 0.f; } if (jj < 0) { jj = 0; fv = 0.f; }
				if (ii >= S3R_HM_N - 1) { ii = S3R_HM_N - 2; fu = 1.f; }
				if (jj >= S3R_HM_N - 1) { jj = S3R_HM_N - 2; fv = 1.f; }
				float r00 = m_hmRaw[jj * S3R_HM_N + ii], r10 = m_hmRaw[jj * S3R_HM_N + ii + 1];
				float r01 = m_hmRaw[(jj + 1) * S3R_HM_N + ii], r11 = m_hmRaw[(jj + 1) * S3R_HM_N + ii + 1];
				gq = r00 + (r10 - r00) * fu + ((r01 + (r11 - r01) * fu) - (r00 + (r10 - r00) * fu)) * fv;
			}
			m_pathDeep[i] = (gq > qy + 1.8f * sc) ? 1 : 0;
		}
		for (int i = 0; i < S3R_PATH_SAMPLES; i++) {
			float qx = m_pathSampleXYZ[i][0], qy = m_pathSampleXYZ[i][1], qz = m_pathSampleXYZ[i][2];
			float i0f = (qx - m_hmX0) / m_hmStep;
			float j0f = (qz - m_hmZ0) / m_hmStep;
			int deepCore = m_pathDeep[i] ? 1 : 0;
			int deep = deepCore || S3rPathNearDeep(m_pathDeep, i, 22);
			float flare = S3rTunFlare(m_pathDeep, i);
			if (!deepCore && deep) flare = 1.f;
			float tunR = tunR0 * (1.f + 0.85f * flare);
			float floorY = deep ? (qy - tunR) : (qy - 2.2f);
			if (floorY < 1.f) floorY = 1.f;
			int i0 = (int)i0f - span, j0 = (int)j0f - span;
			int i1 = (int)i0f + span, j1 = (int)j0f + span;
			if (i0 < 0) i0 = 0; if (j0 < 0) j0 = 0;
			if (i1 > S3R_HM_N - 1) i1 = S3R_HM_N - 1;
			if (j1 > S3R_HM_N - 1) j1 = S3R_HM_N - 1;
			float inR = deep ? 0.f : (grooveR * 0.42f);
			float outR = deep ? tunR : grooveR;
			for (int jz = j0; jz <= j1; jz++) {
				for (int ix = i0; ix <= i1; ix++) {
					float wx = m_hmX0 + (float)ix * m_hmStep;
					float wz = m_hmZ0 + (float)jz * m_hmStep;
					float dx = wx - qx, dz = wz - qz;
					float dd = sqrtf(dx * dx + dz * dz);
					int idx = jz * S3R_HM_N + ix;
					if (dd < m_hmPathDist[idx]) m_hmPathDist[idx] = dd;
					if (dd > outR) continue;
					float cut;
					if (deep) {
						if (!deepCore && m_hmRaw[idx] < qy + 1.4f) continue;
						float h = sqrtf(max(0.f, tunR * tunR - dd * dd));
						cut = qy - h;
						if (cut < 1.f) cut = 1.f;
					} else {
						float t = (dd - inR) / max(0.01f, outR - inR);
						if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
						t = t * t * (3.f - 2.f * t);
						float lo = qy - 2.8f;
						if (lo < floorY) lo = floorY;
						cut = lo + (m_hmRaw[idx] - lo) * t;
					}
					if (m_hm[idx] > cut) m_hm[idx] = cut;
				}
			}
			if (deep && m_carveN < S3R_CARVE_MAX && (i % 8) == 0) {
				int k = m_carveN++;
				float hw = tunR * 1.08f;
				m_carveX0[k] = qx - hw; m_carveX1[k] = qx + hw;
				m_carveZ0[k] = qz - hw; m_carveZ1[k] = qz + hw;
				m_carveY0[k] = qy - tunR - 1.2f;
				m_carveY1[k] = qy + tunR + 1.2f;
				m_carveCeil[k] = 1;
			}
		}
		{
			float tmp[S3R_HM_N * S3R_HM_N];
			const float inB = m_bandHalf * 1.02f;
			const float outB = m_bandHalf * 3.35f;
			for (int pass = 0; pass < 3; pass++) {
				PumpQueued(FALSE);
				for (int i = 0; i < S3R_HM_N * S3R_HM_N; i++) tmp[i] = m_hm[i];
				for (int jz = 0; jz < S3R_HM_N; jz++) {
					for (int ix = 0; ix < S3R_HM_N; ix++) {
						int idx = jz * S3R_HM_N + ix;
						float d = m_hmPathDist[idx];
						if (d <= inB || d >= outB) continue;
						if (m_hmRaw[idx] > tmp[idx] + 3.5f) continue;
						float acc = 0.f; int n = 0;
						for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) {
							int x2 = ix + di, z2 = jz + dj;
							if (x2 < 0) x2 = 0; if (x2 > S3R_HM_N - 1) x2 = S3R_HM_N - 1;
							if (z2 < 0) z2 = 0; if (z2 > S3R_HM_N - 1) z2 = S3R_HM_N - 1;
							acc += tmp[z2 * S3R_HM_N + x2];
							n++;
						}
						m_hm[idx] = acc / (float)n;
					}
				}
			}
		}
	}
	{
		float sx = 0.f, sy = 0.f, sz = 0.f;
		for (int i = 0; i < S3R_PATH_SAMPLES; i++) {
			sx += m_pathSampleXYZ[i][0]; sy += m_pathSampleXYZ[i][1]; sz += m_pathSampleXYZ[i][2];
		}
		const float inv = 1.f / (float)S3R_PATH_SAMPLES;
		m_demoMidX = sx * inv; m_demoMidY = sy * inv; m_demoMidZ = sz * inv;
		float r2 = 1.f;
		for (int i = 0; i < S3R_PATH_SAMPLES; i++) {
			float dx = m_pathSampleXYZ[i][0] - m_demoMidX;
			float dz = m_pathSampleXYZ[i][2] - m_demoMidZ;
			float d2 = dx * dx + dz * dz;
			if (d2 > r2) r2 = d2;
		}
		m_demoRad = sqrtf(r2);
		if (m_demoRad < 40.f) m_demoRad = 40.f;
	}
	BuildCraftMeshes();
	BuildObstacleMesh(m_themeActive);
	PlaceObstaclesAndItems();
	ResetRaceState();
	BeginDemoPreview();
	if (m_view.m_ready) m_view.BakeNoiseCS();
	UpdateStatus();
	m_hudDirty = 1; m_clearDirty = 1;
	PumpQueued(FALSE);
	BakeStaticMeshes();
	PumpQueued(FALSE);
	RenderScene();
}

void CSoft3DRaceDlg::BuildCraftMeshes()
{
	// Cute bird-ship: lathe body + wings + canopy. High subdivision.
	m_craftNv = 0; m_craftNi = 0;
	auto emitV = [&](float x,float y,float z,float nx,float ny,float nz,float u,float v,float r,float g,float b,float a){
		if (m_craftNv >= S3R_CRAFT_VMAX) return;
		float* p = m_craftVert + m_craftNv * 12;
		p[0]=x;p[1]=y;p[2]=z;p[3]=nx;p[4]=ny;p[5]=nz;p[6]=u;p[7]=v;p[8]=r;p[9]=g;p[10]=b;p[11]=a;
		m_craftNv++;
	};
	auto emitTri = [&](UINT a,UINT b,UINT c){
		if (m_craftNi + 3 > S3R_CRAFT_IMAX) return;
		m_craftIdx[m_craftNi++]=a; m_craftIdx[m_craftNi++]=b; m_craftIdx[m_craftNi++]=c;
	};
	const int rings = 28, segs = 36;
	const UINT base = 0;
	for (int i = 0; i <= rings; i++) {
		float t = (float)i / (float)rings;
		float yy = (t - 0.45f) * 1.7f;
		float rad = 0.22f + 0.38f * sinf(t * (float)M_PI) * (0.55f + 0.45f * cosf(t * 2.2f));
		if (t > 0.82f) rad *= (1.f - (t - 0.82f) / 0.18f);
		if (t < 0.12f) rad *= t / 0.12f;
		for (int j = 0; j <= segs; j++) {
			float a = (float)j / (float)segs * (float)(M_PI * 2.0);
			float x = cosf(a) * rad, z = sinf(a) * rad;
			float dx = cosf(a) * rad; float dz = sinf(a) * rad;
			float nx=cosf(a), ny=(t<0.5f?0.35f:-0.15f), nz=sinf(a); S3rNorm3(nx,ny,nz);
			emitV(x, yy, z, nx, ny, nz, (float)j/segs, t, 1,1,1,1);
		}
	}
	for (int i = 0; i < rings; i++) for (int j = 0; j < segs; j++) {
		UINT a = (UINT)(i * (segs + 1) + j);
		UINT b = a + 1, c = a + (UINT)(segs + 1), d = c + 1;
		emitTri(a,c,b); emitTri(b,c,d);
	}
	// wings
	auto wing = [&](float side){
		UINT w0 = (UINT)m_craftNv;
		const int wu=20, wv=16;
		for (int v=0;v<=wv;v++) for (int u=0;u<=wu;u++) {
			float uu=(float)u/wu, vv=(float)v/wv;
			float x = side * (0.25f + uu * 1.15f);
			float y = -0.05f + vv * 0.12f + sinf(uu*(float)M_PI)*0.08f;
			float z = (uu-0.2f)*0.55f + (vv-0.5f)*0.08f;
			float nx=0,ny=1,nz=0;
			emitV(x,y,z,nx,ny,nz,uu,vv,1,1,1,1);
		}
		for (int v=0;v<wv;v++) for (int u=0;u<wu;u++) {
			UINT a=w0+(UINT)(v*(wu+1)+u), b=a+1, c=a+(UINT)(wu+1), d=c+1;
			if (side>0){ emitTri(a,c,b); emitTri(b,c,d);} else { emitTri(a,b,c); emitTri(b,d,c);}
		}
	};
	wing(1.f); wing(-1.f);
	// canopy bubble
	UINT c0=(UINT)m_craftNv;
	const int cr=20, cs=28;
	for (int i=0;i<=cr;i++){
		float t=(float)i/cr; float ph=t*(float)M_PI*.55f;
		for (int j=0;j<=cs;j++){
			float a=(float)j/cs*(float)(M_PI*2.0);
			float rad=0.22f*sinf(ph);
			float x=cosf(a)*rad, y=0.25f+0.32f*cosf(ph), z=sinf(a)*rad*0.85f-0.05f;
			float nx=cosf(a)*sinf(ph), ny=cosf(ph), nz=sinf(a)*sinf(ph); S3rNorm3(nx,ny,nz);
			emitV(x,y,z,nx,ny,nz,(float)j/cs,t,.7f,.9f,1.f,.85f);
		}
	}
	for (int i=0;i<cr;i++) for (int j=0;j<cs;j++){
		UINT a=c0+(UINT)(i*(cs+1)+j), b=a+1, c=a+(UINT)(cs+1), d=c+1;
		emitTri(a,c,b); emitTri(b,c,d);
	}
	(void)base;
}

void CSoft3DRaceDlg::BuildObstacleMesh(int theme)
{
	m_obsNv=0; m_obsNi=0;
	auto emitV=[&](float x,float y,float z,float nx,float ny,float nz,float u,float v,float r,float g,float b,float a){
		if(m_obsNv>=S3R_OBS_VMAX)return; float* p=m_obsVert+m_obsNv*12;
		p[0]=x;p[1]=y;p[2]=z;p[3]=nx;p[4]=ny;p[5]=nz;p[6]=u;p[7]=v;p[8]=r;p[9]=g;p[10]=b;p[11]=a; m_obsNv++;
	};
	auto emitTri=[&](UINT a,UINT b,UINT c){ if(m_obsNi+3>S3R_OBS_IMAX)return; m_obsIdx[m_obsNi++]=a;m_obsIdx[m_obsNi++]=b;m_obsIdx[m_obsNi++]=c; };
	auto cyl=[&](float y0,float y1,float rad,float rr,float gg,float bb,int segsN,float ox,float oz){
		segsN = S3MeshScaleCount(segsN, 256);
		if (segsN < 3) segsN = 3;
		UINT baseV=(UINT)m_obsNv;
		for(int k=0;k<=segsN;k++){
			float a=(float)k/segsN*(float)(M_PI*2);
			float x=ox+cosf(a)*rad, z=oz+sinf(a)*rad, nx=cosf(a), nz=sinf(a);
			emitV(x,y0,z,nx,0,nz,(float)k/segsN,0,rr,gg,bb,1);
			emitV(x,y1,z,nx,0,nz,(float)k/segsN,1,rr,gg,bb,1);
		}
		for(int k=0;k<segsN;k++){
			UINT a=baseV+(UINT)(k*2), b=a+1, c=a+2, d=c+1;
			emitTri(a,c,b); emitTri(b,c,d);
		}
	};
	auto cone=[&](float y0,float y1,float rad0,float rad1,float rr,float gg,float bb,int segsN){
		segsN = S3MeshScaleCount(segsN, 256);
		if (segsN < 3) segsN = 3;
		UINT baseV=(UINT)m_obsNv;
		for(int k=0;k<=segsN;k++){
			float a=(float)k/segsN*(float)(M_PI*2);
			float c=cosf(a), s=sinf(a);
			float dx = c*(rad1-rad0), dy = y1-y0, dz = s*(rad1-rad0);
			float ny = -sqrtf(dx*dx+dz*dz)/dy;
			float nx=c, nz=s;
			float nl=sqrtf(nx*nx+ny*ny+nz*nz); nx/=nl; ny/=nl; nz/=nl;
			emitV(c*rad0,y0,s*rad0,nx,ny,nz,(float)k/segsN,0,rr,gg,bb,1);
			emitV(c*rad1,y1,s*rad1,nx,ny,nz,(float)k/segsN,1,rr,gg,bb,1);
		}
		for(int k=0;k<segsN;k++){
			UINT a=baseV+(UINT)(k*2), b=a+1, c=a+2, d=c+1;
			emitTri(a,c,b); emitTri(b,c,d);
		}
	};
	// AABB は常に min/max 正規化（逆転座標で長針ポリゴンが出ないように）
	auto box=[&](float x0,float y0,float z0,float x1,float y1,float z1,float rr,float gg,float bb, int sub=1){
		{
			int sm = S3MeshAxisMul();
			if (sm > 1) {
				sub *= sm;
				if (sub > 16) sub = 16;
			}
			if (sub < 1) sub = 1;
		}
		float xa=(x0<x1)?x0:x1, xb=(x0<x1)?x1:x0;
		float ya=(y0<y1)?y0:y1, yb=(y0<y1)?y1:y0;
		float za=(z0<z1)?z0:z1, zb=(z0<z1)?z1:z0;
		if (xb-xa < 1e-4f || yb-ya < 1e-4f || zb-za < 1e-4f) return;
		auto v=[&](float x,float y,float z,float nx,float ny,float nz){emitV(x,y,z,nx,ny,nz,0,0,rr,gg,bb,1);};
		
		auto face=[&](float x0,float y0,float z0, float x1,float y1,float z1, float x2,float y2,float z2, float x3,float y3,float z3, float nx,float ny,float nz) {
			for(int j=0;j<sub;j++)for(int i=0;i<sub;i++){
				float u0=(float)i/sub, u1=(float)(i+1)/sub;
				float v0=(float)j/sub, v1=(float)(j+1)/sub;
				float p0x=x0+(x1-x0)*u0+(x3-x0)*v0, p0y=y0+(y1-y0)*u0+(y3-y0)*v0, p0z=z0+(z1-z0)*u0+(z3-z0)*v0;
				float p1x=x0+(x1-x0)*u1+(x3-x0)*v0, p1y=y0+(y1-y0)*u1+(y3-y0)*v0, p1z=z0+(z1-z0)*u1+(z3-z0)*v0;
				float p2x=x0+(x1-x0)*u1+(x3-x0)*v1, p2y=y0+(y1-y0)*u1+(y3-y0)*v1, p2z=z0+(z1-z0)*u1+(z3-z0)*v1;
				float p3x=x0+(x1-x0)*u0+(x3-x0)*v1, p3y=y0+(y1-y0)*u0+(y3-y0)*v1, p3z=z0+(z1-z0)*u0+(z3-z0)*v1;
				UINT b0=(UINT)m_obsNv;
				v(p0x,p0y,p0z,nx,ny,nz); v(p1x,p1y,p1z,nx,ny,nz); v(p2x,p2y,p2z,nx,ny,nz); v(p3x,p3y,p3z,nx,ny,nz);
				emitTri(b0,b0+1,b0+2); emitTri(b0,b0+2,b0+3);
			}
		};
		face(xa,ya,zb, xb,ya,zb, xb,ya,za, xa,ya,za, 0,-1,0);
		face(xa,yb,za, xb,yb,za, xb,yb,zb, xa,yb,zb, 0,1,0);
		face(xa,ya,za, xa,yb,za, xa,yb,zb, xa,ya,zb, -1,0,0);
		face(xb,ya,zb, xb,yb,zb, xb,yb,za, xb,ya,za, 1,0,0);
		face(xa,ya,zb, xa,yb,zb, xb,yb,zb, xb,ya,zb, 0,0,1);
		face(xb,ya,za, xb,yb,za, xa,yb,za, xa,ya,za, 0,0,-1);
	};

	if (theme == THEME_FOREST) {
		cyl(0.f, 2.6f, 0.34f, 0.42f, 0.26f, 0.12f, 32, 0.f, 0.f);
		cone(1.7f, 4.4f, 1.45f, 0.2f, 0.32f, 0.78f, 0.3f, 32);
		cone(3.0f, 5.4f, 1.05f, 0.12f, 0.28f, 0.72f, 0.26f, 32);
		cone(4.1f, 6.2f, 0.65f, 0.05f, 0.4f, 0.88f, 0.35f, 32);
		box(-0.85f,0,-0.85f,0.85f,0.16f,0.85f,0.35f,0.55f,0.25f, 2);
		box(-2.15f,2.15f,-0.11f,2.15f,2.42f,0.11f,0.38f,0.24f,0.12f, 2);
		box(-0.11f,2.55f,-1.85f,0.11f,2.82f,1.85f,0.40f,0.26f,0.12f, 2);
		box(-1.55f,3.15f,-0.09f,1.55f,3.38f,0.09f,0.34f,0.22f,0.10f, 2);
	} else if (theme == THEME_RUINS) {
		// 細切れアーチは非等方スケールで針状になるので、塔＋梁のソリッドに変更
		box(-0.85f,0,-0.85f,0.85f,0.35f,0.85f,0.62f,0.56f,0.46f, 2);
		box(-0.55f,0.35f,-0.55f,0.55f,3.4f,0.55f,0.7f,0.64f,0.52f, 4);
		box(-1.35f,3.15f,-0.4f,1.35f,3.55f,0.4f,0.68f,0.62f,0.5f, 2);
		box(-1.25f,0,-0.28f,-0.85f,3.2f,0.28f,0.58f,0.52f,0.42f, 2);
		box(0.85f,0,-0.28f,1.25f,3.2f,0.28f,0.58f,0.52f,0.42f, 2);
		box(-0.35f,3.55f,-0.35f,0.35f,4.1f,0.35f,0.66f,0.6f,0.48f, 2);
	} else if (theme == THEME_OIL) {
		cyl(0.f, 4.0f, 0.55f, 0.3f, 0.32f, 0.36f, 36, 0.f, 0.f);
		cyl(3.7f, 4.35f, 0.8f, 0.9f, 0.55f, 0.18f, 28, 0.f, 0.f);
		cyl(4.2f, 5.0f, 0.22f, 0.45f, 0.48f, 0.5f, 24, 0.f, 0.f);
		box(-1.4f,0,-0.45f,-0.65f,1.9f,0.45f,0.38f,0.4f,0.44f, 2);
		box(0.65f,0,-0.4f,1.4f,1.35f,0.4f,0.42f,0.44f,0.48f, 2);
		box(-0.35f,1.9f,-0.35f,0.35f,2.2f,0.35f,0.95f,0.7f,0.25f, 2);
		box(-2.2f,1.55f,-0.22f,2.2f,2.05f,0.22f,0.42f,0.44f,0.48f, 2);
		for (int i=0;i<6;i++) {
			float a=(float)i/6.f*(float)(M_PI*2);
			cyl(0.f, 0.4f, 0.1f, 0.55f, 0.35f, 0.15f, 16, cosf(a)*0.95f, sinf(a)*0.95f);
		}
	} else if (theme == THEME_NIGHT) {
		box(-0.7f,0,-0.7f,0.7f,4.6f,0.7f,0.28f,0.32f,0.55f, 4);
		box(-0.9f,0,-0.9f,0.9f,0.3f,0.9f,0.22f,0.25f,0.4f, 2);
		for (int w=0;w<6;w++) {
			float yy=0.5f+w*0.65f;
			box(-0.5f,yy,-0.72f,-0.18f,yy+0.26f,-0.68f,1.f,0.92f,0.45f, 1);
			box(0.18f,yy,0.68f,0.5f,yy+0.26f,0.72f,1.f,0.88f,0.4f, 1);
		}
		box(-0.18f,4.6f,-0.18f,0.18f,5.15f,0.18f,0.9f,0.3f,0.35f, 2);
	} else if (theme == THEME_UNDER) {
		cyl(0.f, 0.5f, 1.2f, 0.22f, 0.6f, 0.85f, 32, 0.f, 0.f);
		cone(0.35f, 2.4f, 1.0f, 0.1f, 0.35f, 0.82f, 0.95f, 32);
		cone(0.2f, 1.7f, 0.6f, 0.08f, 1.f, 0.5f, 0.75f, 32);
		box(-1.8f,0.85f,-0.18f,1.8f,1.25f,0.18f,0.25f,0.7f,0.85f, 2);
		for (int i=0;i<8;i++) {
			float a=(float)i/8.f*(float)(M_PI*2);
			cyl(0.f, 0.7f+0.25f*sinf(a*2.f), 0.07f, 0.3f, 0.9f, 0.7f, 16, cosf(a)*0.85f, sinf(a)*0.85f);
		}
	} else if (theme == THEME_GRASS) {
		cyl(0.f, 0.95f, 0.2f, 0.5f, 0.38f, 0.18f, 32, 0.f, 0.f);
		cone(0.55f, 2.7f, 1.15f, 0.12f, 0.5f, 0.9f, 0.32f, 32);
		cone(1.4f, 3.2f, 0.8f, 0.08f, 0.45f, 0.85f, 0.3f, 32);
		box(-0.9f,0,-0.9f,0.9f,0.18f,0.9f,0.65f,0.82f,0.38f, 2);
	} else if (theme == THEME_MESA) {
		// 段丘は太いブロックのみ（薄い板や針を作らない）
		box(-1.4f,0,-1.4f,1.4f,0.7f,1.4f,1.f,0.52f,0.28f, 3);
		box(-1.0f,0.7f,-1.0f,1.0f,1.8f,1.0f,0.98f,0.58f,0.32f, 2);
		box(-0.65f,1.8f,-0.65f,0.65f,2.7f,0.65f,0.95f,0.5f,0.26f, 2);
		box(-0.35f,2.7f,-0.35f,0.35f,3.25f,0.35f,0.9f,0.45f,0.22f, 2);
		box(-1.9f,0,0.7f,-1.2f,1.0f,1.4f,0.85f,0.4f,0.22f, 2);
		box(1.15f,0,-1.7f,1.85f,1.15f,-1.0f,0.9f,0.48f,0.25f, 2);
	} else { // cloud garden
		cyl(0.4f, 1.4f, 1.05f, 0.95f, 0.95f, 1.f, 32, 0.f, 0.f);
		cyl(1.0f, 2.05f, 0.85f, 1.f, 0.92f, 0.98f, 32, 0.f, 0.f);
		cyl(1.6f, 2.55f, 0.6f, 1.f, 0.88f, 0.96f, 32, 0.f, 0.f);
		cyl(2.1f, 2.9f, 0.38f, 1.f, 0.85f, 0.95f, 32, 0.f, 0.f);
		box(-0.3f,0,-0.3f,0.3f,0.55f,0.3f,0.8f,0.95f,0.7f, 2);
		for (int i=0;i<6;i++) {
			float a=(float)i/6.f*(float)(M_PI*2);
			cyl(0.7f, 1.35f, 0.28f, 0.95f, 0.9f, 1.f, 16, cosf(a)*0.7f, sinf(a)*0.7f);
		}
	}
}

void CSoft3DRaceDlg::PlaceObstaclesAndItems()
{
	m_obsN=0; m_itemN=0;
	DWORD rng=m_genSeed^0xA5A5u;
	auto addObs=[&](float x,float y,float z,float yaw,float pitch,float sx,float sy,float sz,int kind,float dmg,float pathT,int hazard){
		if (m_obsN >= S3R_MAX_OBS) return;
		S3rObs& o=m_obs[m_obsN++];
		o.x=x;o.y=y;o.z=z;o.yaw=yaw;o.pitch=pitch;o.sx=sx;o.sy=sy;o.sz=sz;o.kind=kind;o.damage=dmg;o.pathT=pathT;o.hazard=hazard;
	};

	// 芯半径。帯にめり込んでよい。帯幅を丸ごと塞ぐ巨大芯だけ拒否
	float solidR = 0.50f;
	if (m_themeActive==THEME_FOREST) solidR = 0.40f;
	else if (m_themeActive==THEME_GRASS) solidR = 0.28f;
	else if (m_themeActive==THEME_OIL) solidR = 0.62f;
	else if (m_themeActive==THEME_NIGHT) solidR = 0.80f;
	else if (m_themeActive==THEME_RUINS) solidR = 0.95f;
	else if (m_themeActive==THEME_MESA) solidR = 1.50f;
	else if (m_themeActive==THEME_UNDER) solidR = 1.15f;
	else if (m_themeActive==THEME_CLOUD) solidR = 1.10f;
	auto blocksLane=[&](float ox,float oz,float rad)->int{
		if (rad < m_bandHalf * 0.62f) return 0;
		float best = 1e12f;
		for (int s=0;s<S3R_PATH_SAMPLES;s+=4){
			float dx=ox-m_pathSampleXYZ[s][0], dz=oz-m_pathSampleXYZ[s][2];
			float d2=dx*dx+dz*dz;
			if (d2 < best) best = d2;
		}
		float d = sqrtf(best);
		if (d < rad * 0.35f && rad > m_bandHalf * 0.85f) return 1;
		return 0;
	};

	auto inTunnelLane=[&](float t)->int{
		return S3rPathNearDeep(m_pathDeep, S3rPathIdx(t), 12);
	};

	// 帯にめり込む両脇（枝・土管・傘が光帯を貫く。片側は通れる）
	for (int g=0;g<48 && m_obsN+4<S3R_MAX_OBS;g++){
		float t=(float)g/48.f;
		if (inTunnelLane(t)) continue;
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
		SplineFrame(t,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
		float hs= 1.10f + S3rRand01(rng)*0.55f;
		float tall= 1.20f + S3rRand01(rng)*0.70f;
		if (m_themeActive==THEME_FOREST) { hs=1.25f+S3rRand01(rng)*0.40f; tall=1.55f+S3rRand01(rng)*0.70f; }
		float side = m_bandHalf * (0.28f + S3rRand01(rng)*0.90f);
		float lx = px+bx*side, lz = pz+bz*side;
		float rx = px-bx*side, rz = pz-bz*side;
		if (!blocksLane(lx,lz, solidR*hs))
			addObs(lx, GroundY(lx,lz)-0.18f, lz, atan2f(tx,tz), 0, hs, tall, hs, 1, 7.f, t, 1);
		if (!blocksLane(rx,rz, solidR*hs))
			addObs(rx, GroundY(rx,rz)-0.18f, rz, atan2f(tx,tz), 0, hs, tall, hs, 1, 7.f, t, 1);
	}

	int wantSlalom = 64;
	for (int i=0;i<wantSlalom && m_obsN<S3R_MAX_OBS;i++){
		float t = (float)i / (float)wantSlalom + S3rRand01(rng)*0.01f;
		if (t >= 1.f) t -= 1.f;
		if (inTunnelLane(t)) continue;
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
		SplineFrame(t,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
		float side = ((i&1)?1.f:-1.f);
		float s = 0.70f + S3rRand01(rng)*0.55f;
		float tall = 1.05f + S3rRand01(rng)*0.75f;
		if (m_themeActive==THEME_FOREST) tall = 1.45f + S3rRand01(rng)*0.85f;
		float off = m_bandHalf * (0.12f + S3rRand01(rng)*0.85f);
		float obsx = px + bx*side*off;
		float obsz = pz + bz*side*off;
		if (blocksLane(obsx, obsz, solidR * s)) continue;
		float obsy = GroundY(obsx, obsz) - 0.18f;
		int k = 0;
		if (m_themeActive == THEME_FOREST) { k = 1; }
		else if (m_themeActive == THEME_NIGHT) { k = 0; }
		else if (m_themeActive == THEME_RUINS) { k = 1; }
		else if (m_themeActive == THEME_MESA) { k = 1; }
		else if (m_themeActive == THEME_UNDER) { k = 0; }
		addObs(obsx, obsy, obsz,
			atan2f(tx,tz) + (S3rRand01(rng)-0.5f)*0.4f, (S3rRand01(rng)-0.5f)*0.12f,
			s, tall, s, k, 9.f, t, 1);
	}

	int wantAir = 36;
	for (int i=0;i<wantAir && m_obsN<S3R_MAX_OBS;i++){
		float t = S3rRand01(rng);
		if (inTunnelLane(t)) continue;
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
		SplineFrame(t,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
		float side = (S3rRand01(rng) < 0.5f) ? 1.f : -1.f;
		float s = 0.90f + S3rRand01(rng)*0.70f;
		float tall = 1.10f + S3rRand01(rng)*0.80f;
		float off = m_bandHalf * (0.20f + S3rRand01(rng)*1.15f);
		float obsx = px + bx*side*off;
		float obsz = pz + bz*side*off;
		if (blocksLane(obsx, obsz, solidR * s)) continue;
		float gy = GroundY(obsx, obsz) - 0.18f;
		addObs(obsx, gy, obsz,
			atan2f(tx,tz), 0.f, s, tall, s, 5, 8.f, t, 1);
	}

	int wantFill = 320;
	if (wantFill > S3R_MAX_OBS - m_obsN) wantFill = S3R_MAX_OBS - m_obsN;
	for (int i=0;i<wantFill;i++){
		float t = S3rRand01(rng);
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
		SplineFrame(t,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
		float side = (S3rRand01(rng) < 0.5f) ? 1.f : -1.f;
		float ring = S3rRand01(rng);
		float dist;
		if (ring < 0.38f) dist = m_bandHalf * (0.10f + S3rRand01(rng) * 1.35f);
		else if (ring < 0.72f) dist = m_bandHalf * (1.6f + S3rRand01(rng) * 5.0f);
		else dist = m_bandHalf * (8.f + S3rRand01(rng) * 16.f);
		float along = (S3rRand01(rng)*2.f-1.f)*(ring<0.72f?12.f:24.f);
		float ox = px + bx * side * dist + tx * along;
		float oz = pz + bz * side * dist + tz * along;
		if (inTunnelLane(t) && dist < m_bandHalf * 4.8f) continue;
		float s = 0.90f + S3rRand01(rng) * (ring<0.38f ? 0.70f : 0.95f);
		if (blocksLane(ox, oz, solidR * s)) continue;
		float gy = GroundY(ox, oz) - 0.18f;
		if (gy < m_waterY + 1.2f && S3rRand01(rng) < 0.55f) continue;
		float tall = 1.00f + S3rRand01(rng) * 0.70f;
		if (m_themeActive == THEME_CLOUD) {
			gy += S3rRand01(rng) * 1.2f;
		}
		int k = 0;
		float r = S3rRand01(rng);
		if (m_themeActive == THEME_FOREST) { k = (r < 0.6f) ? 0 : 1; }
		else if (m_themeActive == THEME_NIGHT) { k = (r < 0.5f) ? 0 : 1; }
		else if (m_themeActive == THEME_RUINS) { k = (r < 0.33f) ? 0 : ((r < 0.66f) ? 1 : 2); }
		else if (m_themeActive == THEME_MESA) { k = (r < 0.4f) ? 0 : 1; }
		else if (m_themeActive == THEME_UNDER) { k = (r < 0.5f) ? 0 : 1; }
		else { k = 0; }
		int hz = (dist < m_bandHalf * 1.25f) ? 1 : 0;
		addObs(ox, gy, oz, S3rRand01(rng)*(float)(M_PI*2), 0.f, s, tall, s, k, hz?6.f:0.f, t, hz);
	}

	const int kinds[]={KIND_TEMPO,KIND_TEMPO_DN,KIND_PITCH_UP,KIND_PITCH_DN,KIND_NEXT,KIND_PREV,KIND_VOL_UP,KIND_VOL_DN,KIND_EQ,KIND_EQ_FLAT,KIND_REVERB,KIND_XFADE,KIND_RANDOM};
	const int masks[]={ITEM_TEMPO,ITEM_TEMPO_DN,ITEM_PITCH_UP,ITEM_PITCH_DN,ITEM_NEXT,ITEM_PREV,ITEM_VOL_UP,ITEM_VOL_DN,ITEM_EQ,ITEM_EQ_FLAT,ITEM_REVERB,ITEM_XFADE,ITEM_RANDOM};
	int mask=ItemMask();
	int wantItems=36; if(wantItems>S3R_MAX_ITEMS) wantItems=S3R_MAX_ITEMS;
	for(int i=0;i<wantItems;i++){
		int ki=i%13; if(!(mask&masks[ki])) continue;
		float t=S3rRand01(rng);
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz; SplineFrame(t,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
		float off=(S3rRand01(rng)*2.f-1.f)*m_bandHalf*0.4f;
		S3rItem& it=m_items[m_itemN++];
		it.x=px+bx*off; it.y=py+by*off+nx*0.35f; it.z=pz+bz*off;
		it.kind=kinds[ki]; it.taken=0; it.spin=S3rRand01(rng)*(float)(M_PI*2); it.pathT=t;
	}
}

void CSoft3DRaceDlg::ResetRaceState()
{
	m_phase=PHASE_IDLE; m_countT=0; m_countShown=-1; m_podiumT=0; m_raceClock=0;
	m_camYawOff=0; m_camPitchOff=0.22f;
	m_camSmoothInit=0;
	m_camTunnelT=0.f;
	m_mouseLook=0;
	m_camZoom = (savedata.s3r_zoom>=50&&savedata.s3r_zoom<=250)? (savedata.s3r_zoom/100.f) : 1.f;
	m_lookback=0; m_accelHeld=0; m_brakeHeld=0;
	m_reverbFogBoost=0; m_eqDofBoost=0;
	m_playerSpdEma=0.f;
	m_wrongWay=0; m_overlayHold=0.f;
	m_standDirty=1;
	int opp=ReadOppFromUi(); if(opp<1)opp=1; if(opp>11)opp=11;
	m_craftN = 1 + opp;
	// 名前プールから重複なくランダム割当
	int namePick[100];
	for (int i=0;i<100;i++) namePick[i]=i;
	for (int i=99;i>0;i--) {
		int j = (int)(S3rRand01(m_rng) * (float)(i + 1)); if (j < 0) j = 0; if (j > i) j = i;
		int tmp=namePick[i]; namePick[i]=namePick[j]; namePick[j]=tmp;
	}
	// 超簡単はラインが甘く遅め。強烈は上限近く
	float aiLineTable[5]={0.22f, 0.48f, 0.78f, 0.90f, 0.97f};
	int aiLv=ReadAiFromUi();
	m_aiRaceLv = aiLv;
	m_finishSimT = 0.f;
	for (int i=0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i]; memset(&c,0,sizeof(c));
		c.isPlayer=(i==0); c.alive=1; c.colorIdx=i%12; c.hp=100.f; c.fuel=100.f;
		c.throttle=0.f; c.rpm=0.08f;
		c.nitroStock = 1; c.nitroT = 0.f;
		c.dmgAccum = 0.f; c.hitFlashT = 0.f; c.fxEmitT = 0.f; c.fxHitCool = 0.f;
		wcscpy_s(c.name, kS3rGirlNames[namePick[i % 100]]);
		c.lapTimesN = 0;
		ApplyStartGridPose(c, i);
		AlignCraftToPath(c, 0.06f);
		if (c.isPlayer) {
			c.aiSkill = 0.f;
		} else {
			float tier = (m_craftN <= 2) ? 0.5f : (float)(i - 1) / (float)(m_craftN - 2);
			float base = aiLineTable[aiLv];
			float spread = 0.28f - 0.035f * (float)aiLv;
			c.aiSkill = S3rClamp(base + (tier - 0.5f) * spread + (S3rRand01(m_rng) - 0.5f) * 0.10f, 0.08f, 0.99f);
		}
		// レーン好みは弱め（端寄り暴走→帯外ループを減らす）
		c.aiSteerBias=(S3rRand01(m_rng)*2.f-1.f) * 0.55f;
		if (c.isPlayer) {
			c.aiWeaveAmp = 0.f;
			c.aiBrakeHabit = 0.f;
		} else {
			c.aiWeaveAmp = S3rClamp(0.24f + (1.f - c.aiSkill) * 0.42f + S3rRand01(m_rng) * 0.28f, 0.20f, 0.98f);
			c.aiBrakeHabit = S3rClamp(0.10f + S3rRand01(m_rng) * 0.72f + (1.f - c.aiSkill) * 0.22f, 0.08f, 1.f);
			if (aiLv >= AI_FEROCIOUS) { c.aiWeaveAmp *= 0.52f; c.aiBrakeHabit *= 0.40f; }
			else if (aiLv >= AI_HARD) { c.aiWeaveAmp *= 0.70f; c.aiBrakeHabit *= 0.60f; }
			else if (aiLv >= AI_NORMAL) { c.aiWeaveAmp *= 0.86f; c.aiBrakeHabit *= 0.80f; }
		}
		c.bestLap=1e9f;
		c.chkYaw=c.yaw; c.chkPitch=c.pitch;
		c.offBandT=0.f; c.courseOutCool=0.f; c.offBand=0;
		c.aiCutT=-1.f; c.aiCutTimer=0.f; c.aiCutCool=c.isPlayer?0.f:8.f;
	}
	UpdateRanks();
	for (int i=0;i<m_itemN;i++) m_items[i].taken=0;
	for (int i=0;i<96;i++) memset(m_confetti[i],0,sizeof(m_confetti[i]));
	memset(m_blast,0,sizeof(m_blast));
	m_blastCursor=0;
	memset(m_fx,0,sizeof(m_fx));
	m_fxCursor=0;
	m_clearBakeText=L""; m_clearDirty=1; m_hudDirty=1;
	m_gaugeKmhQ=-1; m_gaugeRpmQ=-1;
}

void CSoft3DRaceDlg::BeginDemoPreview()
{
	if (m_knotN < 4 || m_craftN < 1) { m_phase = PHASE_IDLE; return; }
	m_phase = PHASE_DEMO;
	m_demoCamT = 0.35f;
	m_demoCamElev = 0.f;
	m_camSmoothInit = 0;
	m_wrongWay = 0; m_overlayHold = 0.f;
	// 自機もデモ中はAI（スタートで通常操作に戻る）
	m_crafts[0].aiSkill = 0.70f;
	m_crafts[0].aiCutCool = 2.f; // 序盤はライン走行で見やすく
	m_playerSpdEma = RaceSpeedCap(0) * 0.55f;
	m_clearBakeText = LL14(L"スタートで開始", L"Press Start", L"Démarrer", L"Avvia", L"Iniciar",
		L"시작", L"按开始", L"Start", L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat");
	m_clearBakeA = 1.f; m_clearDirty = 1; m_standDirty = 1;
}

void CSoft3DRaceDlg::StartRace()
{
	if (m_knotN < 4) GenerateCourse();
	ResetRaceState();
	m_phase = PHASE_COUNTDOWN;
	m_countT = 0; m_countShown = -1;
	m_camYawOff = 0.f; m_camPitchOff = 0.22f; m_camSmoothInit = 0;
	m_crafts[0].aiSkill = 0.f; // 操作はプレイヤーへ
	for (int i = 0; i < m_craftN; i++) {
		if (m_crafts[i].alive) AlignCraftToPath(m_crafts[i], 0.06f);
	}
	m_clearBakeText = L"5"; m_clearBakeA = 1.f; m_clearDirty = 1;
	UpdateStatus();
}

void CSoft3DRaceDlg::ApplyStartGridPose(S3rCraft& c, int i)
{
	// 2列交互のグリッド。i=0 自機が最後尾、奇数スロットは半車分前へずらして詰める
	if (i < 0) i = 0;
	const int n = (m_craftN < 1) ? 1 : m_craftN;
	if (i >= n) i = n - 1;
	const int row = i / 2;
	const int col = i & 1;
	const float plen = (m_pathLen > 1.f) ? m_pathLen : 800.f;
	const float rowPitch = 2.55f / plen;
	const float colStagger = 0.90f / plen;
	c.pathT = (float)row * rowPitch + (float)col * colStagger;
	if (c.pathT < 0.f) c.pathT = 0.f;
	if (c.pathT > 0.9f) c.pathT = 0.01f;
	float px, py, pz, tx, ty, tz, nx, ny, nz, bx, by, bz;
	SplineFrame(c.pathT, px, py, pz, tx, ty, tz, nx, ny, nz, bx, by, bz);
	float laneOff = 1.22f;
	const float half = BandHalfWidth();
	if (laneOff > half * 0.42f) laneOff = half * 0.42f;
	const float lane = (col == 0) ? -laneOff : laneOff;
	c.x = px + bx * lane;
	c.y = py + by * lane;
	c.z = pz + bz * lane;
	c.chkX = px; c.chkY = py; c.chkZ = pz; c.chkPathT = c.pathT;
}

void CSoft3DRaceDlg::AlignCraftToPath(S3rCraft& c, float lookAhead)
{
	if (lookAhead < 0.02f) lookAhead = 0.02f;
	float tAim = c.pathT + lookAhead;
	while (tAim >= 1.f) tAim -= 1.f;
	while (tAim < 0.f) tAim += 1.f;
	float ax, ay, az;
	SplinePoint(tAim, ax, ay, az);
	float dx = ax - c.x, dy = ay - c.y, dz = az - c.z;
	float lenXZ = sqrtf(dx * dx + dz * dz);
	float len = sqrtf(dx * dx + dy * dy + dz * dz);
	if (lenXZ < 0.05f || len < 0.05f) {
		// ほぼ真上／真下：接線フォールバック
		float tx, ty, tz;
		SplineTangent(c.pathT + lookAhead * 0.5f, tx, ty, tz);
		dx = tx; dy = ty; dz = tz;
		lenXZ = sqrtf(dx * dx + dz * dz);
		len = 1.f;
	}
	// 水平向きはライン前方、ピッチは3D狙い
	c.yaw = atan2f(dx, dz);
	float pitchSrc = (len > 1e-4f) ? (dy / len) : 0.f;
	c.pitch = S3rClamp(asinf(S3rClamp(pitchSrc, -1.f, 1.f)), -0.85f, 0.85f);
	c.chkYaw = c.yaw; c.chkPitch = c.pitch;
}

float CSoft3DRaceDlg::FinishSimRailSpeed(const S3rCraft& c) const
{
	// 自機ゴール後の裏シミュ：上限巡航だと後続LAPが実レースより速くなるので、
	// 自機の平均周回を基準にし、順位が後ろほど少し遅らせる
	const float plen = (m_pathLen > 1.f) ? m_pathLen : 800.f;
	const float cap = max(1.f, RaceSpeedCap(0));
	const S3rCraft& pl = m_crafts[0];
	float refLap = 0.f;
	int n = pl.lapTimesN;
	if (n > 0) {
		for (int k = 0; k < n; k++) refLap += pl.lapTimes[k];
		refLap /= (float)n;
	} else if (pl.finished && m_lapsTarget > 0 && pl.finishTime > 1.f) {
		refLap = pl.finishTime / (float)m_lapsTarget;
	}
	if (refLap < 12.f) refLap = plen / (cap * 0.70f);
	int behind = 0;
	if (c.rank > 0 && pl.rank > 0 && c.rank > pl.rank) behind = c.rank - pl.rank;
	else if (!c.isPlayer) behind = 1;
	float want = refLap * (1.f + 0.032f * (float)behind + 0.04f * (1.f - c.aiSkill));
	if (c.lapTimesN > 0 && c.lap >= 1) {
		float own = 0.f;
		int m = 0;
		for (int k = 0; k < c.lapTimesN; k++) {
			float t = c.lapTimes[k];
			if (t > refLap * 0.60f && t < refLap * 1.85f) { own += t; m++; }
		}
		if (m > 0) {
			own /= (float)m;
			if (own > want) want = S3rLerp(want, own, 0.55f);
		}
	}
	if (want < refLap) want = refLap;
	float spd = plen / max(8.f, want);
	if (spd > cap * 0.78f) spd = cap * 0.78f;
	if (spd < cap * 0.40f) spd = cap * 0.40f;
	return spd;
}

void CSoft3DRaceDlg::InputSteerDelta(float dyaw, float dpitch)
{
	if (m_phase != PHASE_RACE && m_phase != PHASE_COUNTDOWN && m_phase != PHASE_DEMO) return;
	S3rCraft& pl = m_crafts[0];
	if (m_phase != PHASE_DEMO && (!pl.alive || pl.finished)) return;
	// デモ／カウント中は機体を触らずカメラだけで周りを見る
	if (m_phase == PHASE_COUNTDOWN || m_phase == PHASE_DEMO) {
		if (m_phase == PHASE_DEMO) {
			m_demoCamT = S3rNormAngle(m_demoCamT + dyaw * 0.85f);
			float inv = savedata.s3r_invert_y ? -1.f : 1.f;
			m_demoCamElev = S3rClamp(m_demoCamElev + inv * dpitch * 18.f, -25.f, 55.f);
		} else {
			m_camYawOff = S3rNormAngle(m_camYawOff + dyaw * 1.15f);
			float inv = savedata.s3r_invert_y ? -1.f : 1.f;
			m_camPitchOff = S3rClamp(m_camPitchOff + inv * dpitch * 0.85f, -0.35f, 0.72f);
		}
		return;
	}
	float ag = (pl.agilityT > 0.f) ? 1.45f : 1.f;
	pl.yaw = S3rNormAngle(pl.yaw + dyaw * ag);
	pl.pitch = S3rClamp(pl.pitch + dpitch * ag, -1.05f, 1.05f);
	m_camYawOff *= 0.85f;
	m_camPitchOff = S3rLerp(m_camPitchOff, 0.22f, 0.12f);
}
void CSoft3DRaceDlg::InputCameraDelta(float dyaw, float dpitch)
{
	m_camYawOff = S3rNormAngle(m_camYawOff + dyaw);
	m_camPitchOff = S3rClamp(m_camPitchOff + dpitch, 0.05f, 0.42f);
}
void CSoft3DRaceDlg::InputZoom(int dir)
{
	m_camZoom = S3rClamp(m_camZoom + (dir>0?-0.06f:0.06f), 0.55f, 2.2f);
	savedata.s3r_zoom = (int)(m_camZoom * 100.f + 0.5f);
}

void CSoft3DRaceDlg::ApplyItem(int kind)
{
	S3rCraft& pl = m_crafts[0];
	// 動画(mode=-2)は音／再生操作を無視。ゲーム側バフだけ適用
	const BOOL skipAud = (mode == -2) ? TRUE : FALSE;
	switch (kind) {
	case KIND_TEMPO: {
		if (!skipAud) {
			int pct = tempo / 2 + 10; if (pct > 200) pct = 200;
			if (mp && ::IsWindow(mp->GetSafeHwnd())) mp->ApplyPracticeTempoPercent(pct);
			else { tempo = pct * 2; if (og && ::IsWindow(og->GetSafeHwnd())) og->m_tempo_sl.SetPos(tempo); }
		}
		pl.boostT = max(pl.boostT, 4.5f); break;
	}
	case KIND_TEMPO_DN: {
		if (!skipAud) {
			int pct = tempo / 2 - 10; if (pct < 25) pct = 25;
			if (mp && ::IsWindow(mp->GetSafeHwnd())) mp->ApplyPracticeTempoPercent(pct);
			else { tempo = pct * 2; if (og && ::IsWindow(og->GetSafeHwnd())) og->m_tempo_sl.SetPos(tempo); }
		}
		pl.slowT = max(pl.slowT, 4.0f); break;
	}
	case KIND_PITCH_UP: if (!skipAud) S3rSetPitchPos(pitch + 20); pl.agilityT = max(pl.agilityT, 5.f); break;
	case KIND_PITCH_DN: if (!skipAud) S3rSetPitchPos(pitch - 20); pl.agilityT = max(pl.agilityT, 5.f); break;
	case KIND_NEXT: if (!skipAud) MpTaskbarNextTrack(); break;
	case KIND_PREV: if (!skipAud) MpTaskbarPrevTrack(); break;
	case KIND_VOL_UP: if (!skipAud) S3rNudgeVolPct(5); pl.flashT = max(pl.flashT, 0.7f); break;
	case KIND_VOL_DN: if (!skipAud) S3rNudgeVolPct(-5); pl.flashT = max(pl.flashT, 0.7f); break;
	case KIND_REVERB:
		if (!skipAud) S3rNudgeReverb(12);
		m_reverbFogBoost = min(1.f, m_reverbFogBoost + 0.35f); pl.fogT = max(pl.fogT, 6.f); break;
	case KIND_EQ:
		if (!skipAud) {
			m_rng = m_rng * 1664525u + 1013904223u; S3rEqBump((int)(m_rng % 15u), 18);
			m_rng = m_rng * 1664525u + 1013904223u; S3rEqBump((int)(m_rng % 15u), -10);
		} else {
			m_rng = m_rng * 1664525u + 1013904223u;
			m_rng = m_rng * 1664525u + 1013904223u;
		}
		m_eqDofBoost = min(1.f, m_eqDofBoost + 0.4f); pl.dofT = max(pl.dofT, 6.f); break;
	case KIND_EQ_FLAT:
		if (!skipAud) S3rEqFlatten(12);
		m_eqDofBoost = max(0.f, m_eqDofBoost - 0.25f); break;
	case KIND_XFADE: {
		if (skipAud) break;
		savedata.play_xfade = savedata.play_xfade ? 0 : 1;
		if (savedata.play_xfade) { int s = savedata.play_xfade_sec100 + 100; if (s < 200) s = 200; if (s > 12000) s = 12000; savedata.play_xfade_sec100 = s; }
		if (mp && ::IsWindow(mp->GetSafeHwnd())) mp->SyncPlayXfadeUi(TRUE);
		else if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_xfade.GetSafeHwnd())
			og->m_xfade.SetCheck(savedata.play_xfade ? BST_CHECKED : BST_UNCHECKED);
		MpPersistSavedataQuick(); break;
	}
	case KIND_RANDOM:
		if (skipAud) break;
		if (og && ::IsWindow(og->GetSafeHwnd())) {
			if (savedata.random == 0) og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CHECK6, BN_CLICKED), 0);
			else og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CHECK5, BN_CLICKED), 0);
		} else { savedata.random = savedata.random ? 0 : 1; MpPersistSavedataQuick(); }
		break;
	default: break;
	}
	if (!skipAud && !playf && mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_PLAY, BN_CLICKED), 0);
}

void CSoft3DRaceDlg::TryPickupCraft(int ci)
{
	if (ci < 0 || ci >= m_craftN) return;
	S3rCraft& c = m_crafts[ci];
	if (!c.alive || c.finished) return;
	for (int i = 0; i < m_itemN; i++) {
		S3rItem& it = m_items[i];
		if (it.taken) continue;
		float dx=c.x-it.x, dy=c.y-it.y, dz=c.z-it.z;
		if (dx*dx+dy*dy+dz*dz < 2.2f*2.2f) {
			it.taken = 1;
			float ir, ig, ib; S3rItemLabRgb(it.kind, ir, ig, ib);
			SpawnFxBurst(FX_PICK, it.x, it.y + 0.4f, it.z, 0.f, 1.f, 0.f, 16, 7.2f, ir, ig, ib);
			if (it.kind == KIND_TEMPO)
				SpawnFxBurst(FX_EMBER, it.x, it.y + 0.2f, it.z, 0.f, 1.f, 0.f, 10, 5.5f, 0.35f, 1.f, 0.45f);
			else if (it.kind == KIND_TEMPO_DN)
				SpawnFxBurst(FX_SMOKE, it.x, it.y + 0.15f, it.z, 0.f, 1.f, 0.f, 10, 3.2f, 0.35f, 0.15f, 0.55f);
			else if (it.kind == KIND_PITCH_UP || it.kind == KIND_PITCH_DN)
				SpawnFxBurst(FX_SPARK, it.x, it.y + 0.35f, it.z, 0.f, 1.f, 0.f, 14, 8.0f, ir, ig, ib);
			else if (it.kind == KIND_VOL_UP || it.kind == KIND_VOL_DN)
				SpawnFxBurst(FX_EMBER, it.x, it.y + 0.25f, it.z, 0.f, 1.f, 0.f, 8, 4.5f, ir, ig, ib);
			else if (it.kind == KIND_REVERB)
				SpawnFxBurst(FX_SMOKE, it.x, it.y + 0.2f, it.z, 0.f, 1.f, 0.f, 8, 2.8f, 0.2f, 0.85f, 0.95f);
			else if (it.kind == KIND_EQ || it.kind == KIND_EQ_FLAT)
				SpawnFxBurst(FX_SPARK, it.x, it.y + 0.3f, it.z, 0.f, 1.f, 0.f, 12, 6.5f, ir, ig, ib);
			if (c.isPlayer) {
				ApplyItem(it.kind);
				Soft3DSfxOneShot(S3SFX_ITEM, it.x, it.y, it.z);
				if (it.kind == KIND_TEMPO) Soft3DSfxUi(S3SFX_BOOST, 0);
				else if (it.kind == KIND_TEMPO_DN) Soft3DSfxUi(S3SFX_SLOW, 0);
				float flashAmt = 0.28f;
				if (it.kind == KIND_TEMPO || it.kind == KIND_VOL_UP) flashAmt = 0.42f;
				else if (it.kind == KIND_TEMPO_DN) flashAmt = 0.32f;
				else if (it.kind == KIND_NEXT || it.kind == KIND_PREV || it.kind == KIND_XFADE || it.kind == KIND_RANDOM)
					flashAmt = 0.38f;
				c.flashT = max(c.flashT, flashAmt);
			} else {
				if (it.kind==KIND_TEMPO) c.boostT=max(c.boostT,3.5f);
				else if (it.kind==KIND_TEMPO_DN) c.slowT=max(c.slowT,3.f);
				else if (it.kind==KIND_PITCH_UP||it.kind==KIND_PITCH_DN) c.agilityT=max(c.agilityT,4.f);
			}
		}
	}
}

void CSoft3DRaceDlg::UpdateRanks()
{
	float prog[S3R_MAX_CRAFT];
	int order[S3R_MAX_CRAFT];
	for (int i=0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i];
		if (!c.retired)
			c.lapProgress = (float)c.lap + c.pathT;
		prog[i]=c.lapProgress;
		order[i]=i;
	}
	for (int i=0;i<m_craftN;i++) for (int j=i+1;j<m_craftN;j++){
		const S3rCraft& a=m_crafts[order[i]];
		const S3rCraft& b=m_crafts[order[j]];
		const int aFin = (a.finished && !a.retired) ? 1 : 0;
		const int bFin = (b.finished && !b.retired) ? 1 : 0;
		BOOL swap=FALSE;
		if (aFin && bFin) swap = a.finishTime > b.finishTime;
		else if (aFin) swap=FALSE;
		else if (bFin) swap=TRUE;
		else swap = prog[order[i]] < prog[order[j]];
		if (swap) { int t=order[i]; order[i]=order[j]; order[j]=t; }
	}
	for (int i=0;i<m_craftN;i++) m_crafts[order[i]].rank = i+1;
	m_standDirty = 1;
}

void CSoft3DRaceDlg::TickCountdown(float dt)
{
	m_countT += dt;
	// カウント中は帯レーンに吸着しつつ、前方ラインを向かせる
	for (int i = 0; i < m_craftN; i++) {
		S3rCraft& c = m_crafts[i];
		if (!c.alive) continue;
		ApplyStartGridPose(c, i);
		AlignCraftToPath(c, 0.06f);
		c.vx = c.vy = c.vz = 0.f;
	}
	// スタート直前は機体向き＝カメラ基準（見回しオフセットは残すがGOで戻す）
	if (m_countT < 0.05f) {
		m_camYawOff = 0.f;
		m_camPitchOff = 0.22f;
		m_camSmoothInit = 0;
	}
	if (m_countT < 5.f) {
		int show = 5 - (int)floorf(m_countT);
		if (show < 1) show = 1;
		if (show != m_countShown) {
			m_countShown = show;
			wchar_t buf[16]; swprintf_s(buf, L"%d", show);
			m_clearBakeText = buf;
			m_clearBakeA = 1.f;
			m_clearDirty = 1;
			if (show <= 3)
				Soft3DSfxUi(S3SFX_COUNT, show);
		}
	} else {
		// GO を約1秒表示してからレース開始
		if (m_countShown != 0) {
			m_countShown = 0;
			m_clearBakeText = LL14(L"GO!", L"GO!", L"GO!", L"VIA!", L"¡YA!", L"GO!", L"开始!", L"انطلق!", L"СТАРТ!", L"LOS!", L"JÁ!", L"START!", L"START!", L"BAŞLA!");
			m_clearBakeA = 1.f;
			m_clearDirty = 1;
			Soft3DSfxUi(S3SFX_GO, 0);
			{
				S3rCraft& pl = m_crafts[0];
				pl.flashT = max(pl.flashT, 0.55f);
				SpawnFxBurst(FX_PICK, pl.x, pl.y + 0.4f, pl.z, 0.f, 1.f, 0.f, 22, 9.5f, 1.f, 0.92f, 0.35f);
				SpawnFxBurst(FX_EMBER, pl.x, pl.y + 0.2f, pl.z, 0.f, 1.f, 0.f, 14, 7.0f, 1.f, 0.55f, 0.15f);
				SpawnBlast(pl.x, pl.y, pl.z);
			}
		}
		if (m_countT >= 6.0f) {
			for (int i = 0; i < m_craftN; i++) {
				if (!m_crafts[i].alive) continue;
				S3rCraft& c = m_crafts[i];
				ApplyStartGridPose(c, i);
				AlignCraftToPath(c, 0.06f);
				float ax, ay, az; SplinePoint(c.pathT + 0.06f, ax, ay, az);
				float dx = ax - c.x, dy = ay - c.y, dz = az - c.z;
				S3rNorm3(dx, dy, dz);
				float kick = 8.f / 3.6f;
				c.vx = dx * kick; c.vy = dy * kick; c.vz = dz * kick;
			}
			// 見回しオフセットを戻して進行方向視点へ
			m_camYawOff = 0.f;
			m_camPitchOff = 0.22f;
			m_camSmoothInit = 0;
			m_phase = PHASE_RACE; m_raceClock = 0;
			m_overlayHold = 1.0f;
			m_playerSpdEma = 0.f;
			m_standDirty = 1;
		}
	}
}

void CSoft3DRaceDlg::TickPodium(float dt)
{
	m_podiumT += dt;
	// 表彰台配置：中央1位・左2位・右3位
	float h1 = 4.2f, h2 = 2.8f, h3 = 1.8f;
	float px[3] = { 0.f, -5.2f, 5.2f };
	float ph[3] = { h1, h2, h3 };
	int slotCraft[3] = { m_podiumOrder[0], m_podiumOrder[1], m_podiumOrder[2] };
	for (int s = 0; s < 3; s++) {
		int ci = slotCraft[s];
		if (ci < 0 || ci >= m_craftN) continue;
		S3rCraft& c = m_crafts[ci];
		c.x = m_podiumBaseX + px[s];
		c.y = m_podiumBaseY + ph[s] + 1.15f;
		c.z = m_podiumBaseZ;
		c.vx = c.vy = c.vz = 0.f;
		c.yaw = 0.f; // 機首 +Z＝カメラ側を向く
		c.pitch = 0.f;
		c.offBand = 0;
	}
	for (int i=0;i<96;i++){
		if (m_confetti[i][5] <= 0.f) {
			m_confetti[i][0]=m_podiumBaseX+(S3rRand01(m_rng)*2.f-1.f)*14.f;
			m_confetti[i][1]=m_podiumBaseY+8.f+S3rRand01(m_rng)*10.f;
			m_confetti[i][2]=m_podiumBaseZ+(S3rRand01(m_rng)*2.f-1.f)*10.f;
			m_confetti[i][3]=(S3rRand01(m_rng)-.5f)*4.f;
			m_confetti[i][4]=-1.f-S3rRand01(m_rng)*3.f;
			m_confetti[i][5]=2.f+S3rRand01(m_rng)*2.f;
		} else {
			m_confetti[i][0]+=m_confetti[i][3]*dt;
			m_confetti[i][1]+=m_confetti[i][4]*dt;
			m_confetti[i][5]-=dt;
		}
	}
}

float CSoft3DRaceDlg::AiPaceIndep(float sk) const
{
	// 超簡単はほぼ自機ペース。強烈は上限近くで独立
	static const float kIndep[5] = { 0.20f, 0.38f, 0.68f, 0.84f, 0.95f };
	int lv = m_aiRaceLv;
	if (lv < 0) lv = 0;
	if (lv > 4) lv = 4;
	float indep = kIndep[lv];
	indep = S3rClamp(indep + (sk - 0.78f) * 0.12f, 0.f, 0.97f);
	return indep;
}

void CSoft3DRaceDlg::AiCapPair(float sk, float raceCap, float pace, float plNow, float indep, int demo, int finishRush, float& softFloor, float& hardCap) const
{
	softFloor = raceCap * (0.18f + 0.28f * sk);
	float hold = max(plNow, pace);
	if (finishRush) {
		softFloor = raceCap * (0.72f + 0.12f * sk);
		hardCap = raceCap * 0.96f;
		if (softFloor > hardCap) softFloor = hardCap;
		return;
	}
	if (demo) {
		hardCap = raceCap * 0.62f;
		if (hardCap < 9.f) hardCap = 9.f;
		if (softFloor > hardCap) softFloor = hardCap;
		return;
	}
	int lv = m_aiRaceLv;
	float maxF = S3rAiMaxPaceFrac(lv);
	float absMax = raceCap * maxF;
	float minHold = raceCap * S3rAiMinPaceFrac(lv);
	if (hold < minHold) hold = minHold;
	hardCap = S3rLerp(hold * (0.98f + 0.04f * sk), absMax, indep);
	if (lv <= AI_EASY) {
		softFloor = minHold * (0.92f + 0.04f * sk);
		float slack = (lv <= AI_SUPER_EASY) ? 1.02f : 1.06f;
		float hmax = hold * slack;
		if (hmax > absMax) hmax = absMax;
		if (hardCap > hmax) hardCap = hmax;
	}
	if (hardCap > absMax) hardCap = absMax;
	if (hardCap < minHold) hardCap = minHold;
	if (hardCap < 9.f) hardCap = 9.f;
	if (softFloor > hardCap) softFloor = hardCap;
}

BOOL CSoft3DRaceDlg::AllAliveFinished() const
{
	for (int i = 0; i < m_craftN; i++) {
		const S3rCraft& c = m_crafts[i];
		if (c.alive && !c.finished && !c.retired) return FALSE;
	}
	return TRUE;
}

void CSoft3DRaceDlg::RetireCraft(S3rCraft& c)
{
	if (c.retired) return;
	c.lapProgress = (float)c.lap + c.pathT;
	c.retired = 1;
	c.alive = 0;
	c.finished = 0;
	c.hp = 0.f;
	c.vx = c.vy = c.vz = 0.f;
	c.explodeT = 1.20f;
	c.smokeT = 6.0f;
	c.aiCutT = -1.f; c.aiCutTimer = 0.f;
	SpawnBlast(c.x, c.y, c.z);
	m_standDirty = 1;
}

int CSoft3DRaceDlg::CountAiRetired() const
{
	int n = 0;
	for (int i = 1; i < m_craftN; i++) if (m_crafts[i].retired) n++;
	return n;
}

int CSoft3DRaceDlg::MaxAiRetire() const
{
	int nAi = m_craftN - 1;
	if (nAi < 1) return 0;
	int lv = m_aiRaceLv;
	if (lv <= AI_SUPER_EASY) {
		int cap = (nAi * 4 + 5) / 11; // 12台(11AI)で最大4
		if (cap < 1) cap = 1;
		if (cap > 4) cap = 4;
		if (cap > nAi) cap = nAi;
		return cap;
	}
	if (lv <= AI_EASY) {
		int cap = (nAi * 7 + 5) / 11;
		if (cap < 2) cap = 2;
		if (cap > nAi) cap = nAi;
		return cap;
	}
	return nAi;
}

void CSoft3DRaceDlg::SpawnBlast(float x, float y, float z)
{
	for (int n = 0; n < 14; n++) {
		int i = m_blastCursor % S3R_BLAST_N;
		m_blastCursor++;
		float a = S3rRand01(m_rng) * 6.2831853f;
		float e = S3rRand01(m_rng) * 1.6f - 0.15f;
		float sp = 4.5f + S3rRand01(m_rng) * 11.f;
		m_blast[i][0] = x;
		m_blast[i][1] = y;
		m_blast[i][2] = z;
		m_blast[i][3] = cosf(a) * sp;
		m_blast[i][4] = e * sp + 2.8f;
		m_blast[i][5] = sinf(a) * sp;
		m_blast[i][6] = 0.50f + S3rRand01(m_rng) * 0.75f;
	}
}

void CSoft3DRaceDlg::TickBlast(float dt)
{
	for (int i = 0; i < S3R_BLAST_N; i++) {
		if (m_blast[i][6] <= 0.f) continue;
		m_blast[i][0] += m_blast[i][3] * dt;
		m_blast[i][1] += m_blast[i][4] * dt;
		m_blast[i][2] += m_blast[i][5] * dt;
		m_blast[i][4] -= 16.f * dt;
		m_blast[i][3] *= (1.f - 1.4f * dt);
		m_blast[i][5] *= (1.f - 1.4f * dt);
		m_blast[i][6] -= dt;
	}
}

void CSoft3DRaceDlg::ApplyCraftDamage(S3rCraft& c, float dmg, int flash)
{
	if (dmg <= 0.f) return;
	if (!c.alive || c.retired || c.finished) return;
	c.hp -= dmg;
	c.dmgAccum += dmg;
	if (c.dmgAccum > 400.f) c.dmgAccum = 400.f;
	if (flash && c.isPlayer)
		c.hitFlashT = max(c.hitFlashT, min(0.42f, 0.14f + dmg * 0.016f));
}

void CSoft3DRaceDlg::SpawnFxBurst(int kind, float x, float y, float z,
	float nx, float ny, float nz, int count, float speed, float r, float g, float b)
{
	if (count < 1) return;
	float ln = sqrtf(nx * nx + ny * ny + nz * nz);
	if (ln > 1e-4f) { nx /= ln; ny /= ln; nz /= ln; }
	else { nx = 0.f; ny = 1.f; nz = 0.f; }
	for (int n = 0; n < count; n++) {
		int i = m_fxCursor % S3R_FX_N;
		m_fxCursor++;
		S3rFx& p = m_fx[i];
		float a = S3rRand01(m_rng) * 6.2831853f;
		float e = S3rRand01(m_rng) * 2.f - 0.35f;
		float sp = speed * (0.40f + S3rRand01(m_rng) * 0.90f);
		float jx = cosf(a), jz = sinf(a);
		p.x = x + jx * 0.12f; p.y = y + 0.08f; p.z = z + jz * 0.12f;
		p.vx = nx * sp + jx * sp * 0.55f;
		p.vy = ny * sp + e * sp * 0.42f;
		p.vz = nz * sp + jz * sp * 0.55f;
		p.kind = kind;
		p.r = r; p.g = g; p.b = b;
		if (kind == FX_SMOKE) {
			p.vy += 1.6f + S3rRand01(m_rng) * 1.8f;
			p.life = 0.55f + S3rRand01(m_rng) * 0.70f;
			p.size = 0.22f + S3rRand01(m_rng) * 0.28f;
			p.vx *= 0.35f; p.vz *= 0.35f;
		} else if (kind == FX_DUST) {
			p.life = 0.28f + S3rRand01(m_rng) * 0.32f;
			p.size = 0.16f + S3rRand01(m_rng) * 0.18f;
			p.vy *= 0.45f;
		} else if (kind == FX_PICK) {
			p.life = 0.38f + S3rRand01(m_rng) * 0.28f;
			p.size = 0.12f + S3rRand01(m_rng) * 0.14f;
			p.vy += 2.4f;
		} else if (kind == FX_EMBER) {
			p.life = 0.35f + S3rRand01(m_rng) * 0.40f;
			p.size = 0.08f + S3rRand01(m_rng) * 0.10f;
			p.vy += 0.8f;
		} else {
			p.life = 0.18f + S3rRand01(m_rng) * 0.22f;
			p.size = 0.06f + S3rRand01(m_rng) * 0.09f;
		}
		p.lifeMax = p.life;
	}
}

void CSoft3DRaceDlg::TickFx(float dt)
{
	for (int i = 0; i < S3R_FX_N; i++) {
		S3rFx& p = m_fx[i];
		if (p.life <= 0.f) continue;
		p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
		if (p.kind == FX_SMOKE) {
			p.vy += 2.2f * dt;
			p.vx *= (1.f - 1.15f * dt);
			p.vz *= (1.f - 1.15f * dt);
			p.life -= dt;
		} else if (p.kind == FX_DUST) {
			p.vy -= 7.5f * dt;
			p.vx *= (1.f - 2.4f * dt);
			p.vz *= (1.f - 2.4f * dt);
			p.life -= dt;
		} else if (p.kind == FX_PICK) {
			p.vy -= 4.5f * dt;
			p.vx *= (1.f - 0.8f * dt);
			p.vz *= (1.f - 0.8f * dt);
			p.life -= dt;
		} else if (p.kind == FX_EMBER) {
			p.vy -= 9.f * dt;
			p.vx *= (1.f - 1.1f * dt);
			p.vz *= (1.f - 1.1f * dt);
			p.life -= dt;
		} else {
			p.vy -= 18.f * dt;
			p.vx *= (1.f - 1.6f * dt);
			p.vz *= (1.f - 1.6f * dt);
			p.life -= dt * 1.15f;
		}
	}
	for (int ci = 0; ci < m_craftN; ci++) {
		S3rCraft& c = m_crafts[ci];
		if (!c.alive || c.retired || c.finished) continue;
		if (c.dmgAccum < 10.f) continue;
		c.fxEmitT -= dt;
		if (c.fxEmitT > 0.f) continue;
		float sev = S3rSaturate((c.dmgAccum - 10.f) / 140.f);
		c.fxEmitT = 0.075f - sev * 0.042f;
		float fx = sinf(c.yaw) * cosf(c.pitch), fy = sinf(c.pitch), fz = cosf(c.yaw) * cosf(c.pitch);
		float bx = c.x - fx * 0.85f, by = c.y - fy * 0.85f + 0.22f, bz = c.z - fz * 0.85f;
		int nSm = 1 + (int)(sev * 2.4f);
		float sg = 0.20f - 0.10f * sev;
		SpawnFxBurst(FX_SMOKE, bx, by, bz, -fx, 0.65f, -fz, nSm, 1.6f + sev * 1.2f, sg, sg, sg);
		if (sev > 0.52f)
			SpawnFxBurst(FX_EMBER, bx, by, bz, -fx, 0.2f, -fz, 1 + (int)(sev * 2.f), 3.2f, 1.f, 0.38f, 0.08f);
	}
}

void CSoft3DRaceDlg::EnterPodium()
{
	m_podiumT = 0.f;
	m_phase = PHASE_PODIUM;
	int order[S3R_MAX_CRAFT];
	int nOrd = 0;
	// 完走者のみ表彰候補（リタイア除外）
	for (int i = 0; i < m_craftN; i++) {
		if (m_crafts[i].finished && !m_crafts[i].retired)
			order[nOrd++] = i;
	}
	if (nOrd < 1) {
		for (int i = 0; i < m_craftN; i++) order[nOrd++] = i;
	}
	for (int i = 0; i < nOrd; i++)
		for (int j = i + 1; j < nOrd; j++)
			if (m_crafts[order[i]].rank > m_crafts[order[j]].rank) {
				int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
			}
	// 空席は -1（1人完走でも同じ機体を2位・3位台に重ねない）
	m_podiumOrder[0] = order[0];
	m_podiumOrder[1] = nOrd > 1 ? order[1] : -1;
	m_podiumOrder[2] = nOrd > 2 ? order[2] : -1;
	SplinePoint(0.f, m_podiumBaseX, m_podiumBaseY, m_podiumBaseZ);
	m_podiumBaseY += 2.5f;
	m_camSmoothInit = 0;
	m_clearBakeText = LL14(L"表彰式", L"Podium", L"Podium", L"Podio", L"Podio", L"시상식", L"领奖台", L"منصة", L"Подиум", L"Podium", L"Pódio", L"Podium", L"Podium", L"Podyum");
	m_clearBakeA = 1.f; m_clearDirty = 1;
	m_standDirty = 1;
	for (int i = 0; i < 96; i++) m_confetti[i][5] = 0.f;
	Soft3DSfxUi(S3SFX_PODIUM, 0);
}

void CSoft3DRaceDlg::TickAi(float dt)
{
	const float scv = SpeedScale();
	const int demo = (m_phase == PHASE_DEMO) ? 1 : 0;
	const int finishRush = (!demo && m_phase == PHASE_FINISH) ? 1 : 0;
	if (!demo && m_phase != PHASE_RACE && !finishRush) return;
	const S3rCraft& pl = m_crafts[0];
	const float plProg = (float)pl.lap + pl.pathT;
	const float plNow = sqrtf(pl.vx*pl.vx + pl.vy*pl.vy + pl.vz*pl.vz);
	const int plRank = (pl.rank > 0) ? pl.rank : 1;
	float paceRef = m_playerSpdEma;
	if (paceRef < plNow) paceRef = plNow;
	if (demo) {
		// デモは自機ペースに依存せず見やすい巡航速度で
		paceRef = RaceSpeedCap(0) * 0.58f;
	} else if (finishRush) {
		paceRef = RaceSpeedCap(0) * 0.96f;
	} else {
		float minP = RaceSpeedCap(0) * S3rAiMinPaceFrac(m_aiRaceLv);
		if (paceRef < minP) paceRef = minP;
	}

	const int i0 = demo ? 0 : 1;
	for (int i=i0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i];
		if (!c.alive || c.finished || c.retired) continue;
		if (finishRush) c.courseOutCool = 0.f;
		if (!finishRush && c.courseOutCool > 0.f) continue;
		float half=BandHalfWidth();
		float lat,vert,cx,cy,cz; BandLocal(c.x,c.y,c.z,c.pathT,lat,vert,cx,cy,cz);
		float latAbs=fabsf(lat), vertAbs=fabsf(vert);

		// ライン取り精度 = 難易度基準 + 順位ボーナス
		float sk = c.aiSkill;
		{
			int aiRank = (c.rank > 0) ? c.rank : (i + 1);
			int behind = aiRank - plRank;
			if (behind > 0) {
				float b = (m_aiRaceLv <= AI_SUPER_EASY) ? 0.012f : ((m_aiRaceLv <= AI_EASY) ? 0.028f : 0.05f);
				sk = S3rClamp(sk + (float)behind * b, 0.f, 1.f);
			}
		}

		if (c.aiCutCool > 0.f) c.aiCutCool = max(0.f, c.aiCutCool - dt);
		if (finishRush) c.aiCutCool = max(c.aiCutCool, 12.f);
		if (c.craftKnockT > 0.f) continue;
		const int lineLock = (c.aiCutCool > 0.f) ? 1 : 0;

		// 計画ショートカットの維持／終了判定
		int cutting = (c.aiCutT >= 0.f && c.aiCutTimer > 0.f) ? 1 : 0;
		if (finishRush && cutting) {
			AbortAiToLine(c, 12.f);
			cutting = 0;
			c.courseOutCool = 0.f;
		}
		if (cutting) {
			c.aiCutTimer -= dt;
			float jx,jy,jz; SplinePoint(c.aiCutT, jx,jy,jz);
			float jdx=c.x-jx, jdy=c.y-jy, jdz=c.z-jz;
			float jdist = sqrtf(jdx*jdx+jdy*jdy+jdz*jdz);
			const int success = (jdist < half * 1.05f) ? 1 : 0;
			float gyNow = GroundY(c.x, c.z);
			const int fail = (!success && (c.aiCutTimer <= 0.f || c.fuel < 52.f || c.hp < (38.f + 22.f * sk)
				|| c.y < gyNow + 1.5f || (c.offBand && c.aiCutTimer < (0.35f + 0.55f * sk)))) ? 1 : 0;
			if (success) {
				float dJoin = c.aiCutT - c.pathT;
				while (dJoin > 0.5f) dJoin -= 1.f;
				while (dJoin < -0.5f) dJoin += 1.f;
				if (dJoin > 0.f && dJoin < 0.35f) c.pathT = c.aiCutT;
				c.aiCutT = -1.f; c.aiCutTimer = 0.f; cutting = 0;
				c.aiCutCool = max(c.aiCutCool, 10.f);
			} else if (fail) {
				AbortAiToLine(c, 32.f);
				cutting = 0;
				continue;
			} else {
				// カット中も合流点へ pathT を進める（失敗時にスタート地点へ戻らない）
				float dJoin = c.aiCutT - c.pathT;
				while (dJoin > 0.5f) dJoin -= 1.f;
				while (dJoin < -0.5f) dJoin += 1.f;
				if (dJoin > 0.f) {
					float plen = (m_pathLen > 1.f) ? m_pathLen : 800.f;
					float step = (18.f * dt) / plen;
					if (step > dJoin) step = dJoin;
					c.pathT += step;
					while (c.pathT >= 1.f) c.pathT -= 1.f;
				}
			}
		}

		// ライン固定・ゴール後シミュ中はカット禁止。技能が高く地形が空いているときだけ
		float cutNeed = 1.10f;
		if (m_aiRaceLv >= AI_FEROCIOUS) cutNeed = 0.68f;
		else if (m_aiRaceLv >= AI_HARD) cutNeed = 0.78f;
		else if (m_aiRaceLv >= AI_NORMAL) cutNeed = 0.86f;
		else if (m_aiRaceLv >= AI_EASY) cutNeed = 0.96f;
		if (!cutting && !lineLock && !finishRush && m_layoutKind != 1 && !c.offBand && c.fuel > 78.f && c.hp > 62.f && sk >= cutNeed) {
			float spans[2] = { 0.028f + 0.012f * c.aiSkill, 0.042f + 0.018f * c.aiSkill };
			float bestSave = 0.f, bestT = -1.f, bestChord = 0.f;
			for (int s = 0; s < 2; s++) {
				float tJoin = c.pathT + spans[s];
				while (tJoin >= 1.f) tJoin -= 1.f;
				float jx,jy,jz; SplinePoint(tJoin, jx,jy,jz);
				float dx=jx-c.x, dy=jy-c.y, dz=jz-c.z;
				float chord = sqrtf(dx*dx+dy*dy+dz*dz);
				float arc = PathArcBetween(c.pathT, tJoin);
				if (arc < half * 4.0f || chord < 1.f) continue;
				float ratio = chord / arc;
				float save = arc - chord;
				int clear = 1;
				for (int k = 1; k <= 8; k++) {
					float u = (float)k / 9.f;
					float sx = c.x + dx * u, sy = c.y + dy * u, sz = c.z + dz * u;
					float gy = GroundY(sx, sz);
					if (sy < gy + 2.6f) { clear = 0; break; }
					if (m_hmReady && m_hmStep > 1e-4f) {
						float uu = (sx - m_hmX0) / m_hmStep, vv = (sz - m_hmZ0) / m_hmStep;
						int ii = (int)floorf(uu), jj = (int)floorf(vv);
						float fu = uu - (float)ii, fv = vv - (float)jj;
						if (ii < 0) { ii = 0; fu = 0.f; } if (jj < 0) { jj = 0; fv = 0.f; }
						if (ii >= S3R_HM_N - 1) { ii = S3R_HM_N - 2; fu = 1.f; }
						if (jj >= S3R_HM_N - 1) { jj = S3R_HM_N - 2; fv = 1.f; }
						float r00=m_hmRaw[jj*S3R_HM_N+ii], r10=m_hmRaw[jj*S3R_HM_N+ii+1];
						float r01=m_hmRaw[(jj+1)*S3R_HM_N+ii], r11=m_hmRaw[(jj+1)*S3R_HM_N+ii+1];
						float rawH = r00+(r10-r00)*fu+((r01+(r11-r01)*fu)-(r00+(r10-r00)*fu))*fv;
						if (rawH > gy + 3.5f && sy < rawH - 1.0f) { clear = 0; break; }
					}
				}
				if (!clear) continue;
				float needRatio = 0.48f - 0.04f * c.aiSkill;
				float needSave = half * (4.2f - 0.5f * c.aiSkill);
				if (ratio < needRatio && save > needSave && save > bestSave) {
					bestSave = save; bestT = tJoin; bestChord = chord;
				}
			}
			if (bestT >= 0.f) {
				c.aiCutT = bestT;
				float eta = bestChord / max(14.f, sqrtf(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz) + 4.f);
				c.aiCutTimer = S3rClamp(eta + 0.85f, 1.4f, 3.2f);
				cutting = 1;
			}
		}

		float phase = m_anim;
		float w1 = 1.05f + 0.31f * (float)i;
		float w2 = 2.35f + 0.17f * (float)i;
		float w3 = 0.72f + 0.23f * (float)i;
		float wLat = sinf(phase * w1 + (float)i * 1.73f) * 0.58f
			+ sinf(phase * w2 + (float)i * 0.91f) * 0.42f;
		float wVert = cosf(phase * w3 + (float)i * 2.09f) * 0.52f
			+ sinf(phase * (1.68f + 0.14f * (float)i) + (float)i * 1.31f) * 0.48f;
		float weave = lineLock ? 0.10f : (0.26f + 0.62f * c.aiWeaveAmp);
		if (m_aiRaceLv >= AI_NORMAL) weave *= 0.72f;
		if (m_aiRaceLv >= AI_HARD) weave *= 0.70f;
		if (m_aiRaceLv >= AI_FEROCIOUS) weave *= 0.62f;
		float laneLim = lineLock ? 0.16f : S3rClamp(0.34f + 0.46f * c.aiWeaveAmp, 0.28f, 0.82f);
		float vertLim = lineLock ? 0.12f : S3rClamp(0.28f + 0.34f * c.aiWeaveAmp, 0.22f, 0.62f);
		float laneFrac = S3rClamp(wLat * weave + c.aiSteerBias * 0.28f, -laneLim, laneLim);
		float vertFrac = S3rClamp(wVert * weave * 0.92f, -vertLim, vertLim);

		// 障害・機体の先読み。難易度で視距と避け方が変わる
		float dodge = 0.f;
		if (!cutting && !lineLock) {
			const int lv = m_aiRaceLv;
			float lookW = 0.022f + 0.018f * sk;
			if (lv >= AI_EASY) lookW = 0.030f + 0.028f * sk;
			if (lv >= AI_NORMAL) lookW = 0.040f + 0.036f * sk;
			if (lv >= AI_HARD) lookW = 0.050f + 0.042f * sk;
			if (lv >= AI_FEROCIOUS) lookW = 0.058f + 0.048f * sk;
			float obsGain = 0.12f + 0.22f * sk;
			if (lv >= AI_EASY) obsGain = 0.38f + 0.40f * sk;
			if (lv >= AI_NORMAL) obsGain = 0.55f + 0.55f * sk;
			if (lv >= AI_HARD) obsGain = 0.72f + 0.70f * sk;
			float obsSign = 1.f;
			if (lv <= AI_SUPER_EASY && sk < 0.28f) obsSign = -0.35f;
			else if (lv <= AI_SUPER_EASY) obsSign = 0.55f;
			else if (lv <= AI_EASY && sk < 0.32f) obsSign = 0.70f;
			if (c.nitroT <= 0.f) {
				for (int o = 0; o < m_obsN; o++) {
					if (!m_obs[o].hazard || m_obs[o].damage <= 0.f) continue;
					float dT = m_obs[o].pathT - c.pathT;
					while (dT > 0.5f) dT -= 1.f;
					while (dT < -0.5f) dT += 1.f;
					if (dT <= 0.002f || dT > lookW) continue;
					float olat, overt, ocx, ocy, ocz;
					BandLocal(m_obs[o].x, m_obs[o].y, m_obs[o].z, m_obs[o].pathT, olat, overt, ocx, ocy, ocz);
					if (fabsf(olat) > half * 1.20f) continue;
					float urg = 1.f - dT / lookW;
					float away = (olat >= 0.f) ? -1.f : 1.f;
					dodge += away * urg * obsGain * obsSign;
					if (lv >= AI_HARD && fabsf(overt) < half * 0.55f)
						vertFrac = S3rClamp(vertFrac + ((overt >= 0.f) ? -0.18f : 0.18f) * urg, -0.28f, 0.28f);
				}
			}
			float packGain = 0.06f + 0.10f * sk;
			if (lv >= AI_EASY) packGain = 0.22f + 0.28f * sk;
			if (lv >= AI_NORMAL) packGain = 0.40f + 0.38f * sk;
			if (lv >= AI_HARD) packGain = 0.58f + 0.42f * sk;
			float lookC = lookW * ((lv <= AI_SUPER_EASY) ? 0.45f : 0.85f);
			for (int j = 0; j < m_craftN; j++) {
				if (j == i) continue;
				const S3rCraft& o = m_crafts[j];
				if (!o.alive || o.retired || o.finished) continue;
				float wdx = o.x - c.x, wdy = o.y - c.y, wdz = o.z - c.z;
				float wd = sqrtf(wdx * wdx + wdy * wdy + wdz * wdz);
				float dT = o.pathT - c.pathT;
				while (dT > 0.5f) dT -= 1.f;
				while (dT < -0.5f) dT += 1.f;
				if (wd > 12.f && (dT <= 0.001f || dT > lookC)) continue;
				if (dT < -0.012f && wd > 6.5f) continue;
				float olat, overt, ocx, ocy, ocz;
				BandLocal(o.x, o.y, o.z, o.pathT, olat, overt, ocx, ocy, ocz);
				float dLat = lat - olat;
				if (fabsf(dLat) > half * 1.12f) continue;
				float away = (dLat > 0.18f) ? 1.f : ((dLat < -0.18f) ? -1.f : ((i > j) ? 1.f : -1.f));
				float urg = (wd < 12.f) ? (1.f - wd / 12.f) : (1.f - S3rSaturate(dT / max(0.008f, lookC)));
				if (wd < 3.4f) urg = max(urg, 0.85f);
				dodge += away * urg * packGain;
			}
			laneFrac = S3rClamp(laneFrac + dodge * 0.42f, -laneLim, laneLim);
		}

		float look = c.pathT + 0.012f + 0.038f * sk;
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
		if (cutting) {
			SplineFrame(c.aiCutT, px,py,pz, tx,ty,tz, nx,ny,nz, bx,by,bz);
		} else {
			SplineFrame(look, px,py,pz, tx,ty,tz, nx,ny,nz, bx,by,bz);
		}
		float aimx = px + (cutting ? 0.f : bx * (laneFrac * half));
		float aimy = py + (cutting ? 0.f : by * (laneFrac * half));
		float aimz = pz + (cutting ? 0.f : bz * (laneFrac * half));
		if (!cutting) {
			aimx += nx * (vertFrac * half * 0.82f);
			aimy += ny * (vertFrac * half * 0.82f);
			aimz += nz * (vertFrac * half * 0.82f);
		}

		// レールばね：目標レーン（蛇行）へ。中央固定だと直線で減速しない
		{
			float wantLat = cutting ? 0.f : (laneFrac * half);
			float wantVert = cutting ? 0.f : (vertFrac * half * 0.82f);
			float dLat = lat - wantLat;
			float dVert = vert - wantVert;
			float rail = cutting ? (1.0f + 1.5f * sk) : (lineLock ? (4.5f + 3.5f * sk) : (2.0f + 5.5f * sk));
			if (c.offBand && !cutting) rail *= 2.2f;
			c.vx += (-bx * dLat - nx * dVert) * rail * dt;
			c.vy += (-by * dLat - ny * dVert) * rail * dt;
			c.vz += (-bz * dLat - nz * dVert) * rail * dt;
			if (!cutting) {
				float outV = c.vx*bx + c.vy*by + c.vz*bz;
				float latErr = lat - wantLat;
				if ((latErr > 0.f && outV > 0.f) || (latErr < 0.f && outV < 0.f)) {
					float kill = min(1.f, (0.7f + 3.5f * sk) * dt);
					c.vx -= bx * outV * kill; c.vy -= by * outV * kill; c.vz -= bz * outV * kill;
				}
				float outN = c.vx*nx + c.vy*ny + c.vz*nz;
				float vertErr = vert - wantVert;
				if ((vertErr > 0.f && outN > 0.f) || (vertErr < 0.f && outN < 0.f)) {
					float kill = min(1.f, (0.7f + 3.5f * sk) * dt);
					c.vx -= nx * outN * kill; c.vy -= ny * outN * kill; c.vz -= nz * outN * kill;
				}
			}
		}

		float tangYaw = atan2f(tx, tz);
		float tangPitch = S3rClamp(asinf(S3rClamp(ty, -1.f, 1.f)), -1.0f, 1.0f);
		float txp = aimx - c.x, typ = aimy - c.y, tzp = aimz - c.z;
		S3rNorm3(txp, typ, tzp);
		float aimYaw = atan2f(txp, tzp);
		float aimPitch = S3rClamp(asinf(S3rClamp(typ, -1.f, 1.f)), -1.0f, 1.0f);
		float tangBlend = cutting ? 0.15f : (lineLock ? 0.88f : (0.66f + 0.28f * sk));
		if (c.offBand && !cutting) tangBlend = 0.94f;
		float wantYaw = S3rNormAngle(aimYaw + S3rNormAngle(tangYaw - aimYaw) * tangBlend);
		float wantPitch = S3rLerp(aimPitch, tangPitch, tangBlend);
		float turnRate = (1.1f + sk * 1.8f) * (c.agilityT > 0 ? 1.25f : 1.f);
		if (c.offBand && !cutting) turnRate *= 1.4f;
		c.yaw = S3rNormAngle(c.yaw + S3rNormAngle(wantYaw - c.yaw) * min(1.f, turnRate * dt));
		c.pitch = S3rLerp(c.pitch, wantPitch, min(1.f, turnRate * dt));
		c.pitch = S3rClamp(c.pitch, -1.05f, 1.05f);

		float raceCap = RaceSpeedCap(c.boostT > 0.f ? 1 : 0) * NitroSpeedMul(c);
		float indep = finishRush ? 0.92f : AiPaceIndep(sk);
		float maxF = S3rAiMaxPaceFrac(m_aiRaceLv);
		float rubber = paceRef * (0.90f + 0.06f * sk);
		float freeT = raceCap * maxF;
		float targetSpd = S3rLerp(rubber, freeT, indep);
		float hardCap = 9.f, softFloor = 0.f;
		AiCapPair(sk, raceCap, paceRef, plNow, indep, demo, finishRush, softFloor, hardCap);
		if (targetSpd < softFloor) targetSpd = softFloor;
		if (targetSpd > hardCap) targetSpd = hardCap;
		float absCeil = RaceSpeedCap(0) * maxF;
		if (c.nitroT > 0.f) absCeil = min(raceCap, RaceSpeedCap(0) * 1.16f);
		if (targetSpd > absCeil) targetSpd = absCeil;
		if (fabsf(dodge) > 0.12f) {
			float hesitate = 0.06f * (1.f - sk);
			if (m_aiRaceLv <= AI_SUPER_EASY) hesitate = 0.10f;
			else if (m_aiRaceLv <= AI_EASY) hesitate = 0.08f;
			targetSpd *= (1.f - hesitate * S3rSaturate(fabsf(dodge)));
		}

		float lead = ((float)c.lap + c.pathT) - plProg;
		if (!finishRush && lead > 0.f) {
			float leadStart = (m_aiRaceLv <= AI_SUPER_EASY) ? 0.06f : ((m_aiRaceLv <= AI_EASY) ? 0.08f : 0.14f);
			if (lead > leadStart) {
				float cut = S3rSaturate((lead - leadStart) / 0.28f);
				float damp = (m_aiRaceLv <= AI_SUPER_EASY) ? 0.22f : (0.18f - 0.10f * indep);
				if (m_aiRaceLv >= AI_HARD) damp = 0.08f;
				if (m_aiRaceLv >= AI_FEROCIOUS) damp = 0.035f;
				targetSpd *= (1.f - cut * damp * (1.f - 0.40f * sk));
			}
		} else if (lead < -0.03f) {
			float catchUp = S3rSaturate((-lead - 0.03f) / 0.22f);
			float catchAmt = 0.20f + 0.24f * sk + 0.20f * indep;
			if (m_aiRaceLv <= AI_SUPER_EASY) catchAmt *= 0.70f;
			else if (m_aiRaceLv <= AI_EASY) catchAmt *= 0.88f;
			targetSpd *= (1.f + catchUp * catchAmt);
			if (finishRush) targetSpd = max(targetSpd, raceCap * 0.82f);
			float catchCeil = absCeil;
			if (m_aiRaceLv >= AI_NORMAL)
				catchCeil = min(raceCap, absCeil * (1.f + 0.10f * catchUp));
			if (m_aiRaceLv >= AI_FEROCIOUS)
				catchCeil = min(raceCap * 1.10f, RaceSpeedCap(0) * 1.12f);
			if (targetSpd > catchCeil) targetSpd = catchCeil;
		}

		float spdNow = sqrtf(c.vx*c.vx + c.vy*c.vy + c.vz*c.vz);
		float bend = 0.f, bendAhead = 0.f;
		if (!cutting) {
			float t0x,t0y,t0z,t1x,t1y,t1z,t2x,t2y,t2z;
			SplineTangent(c.pathT + 0.008f, t0x,t0y,t0z);
			SplineTangent(c.pathT + 0.038f, t1x,t1y,t1z);
			SplineTangent(c.pathT + 0.085f, t2x,t2y,t2z);
			float a0 = t0x*t1x + t0y*t1y + t0z*t1z;
			float a1 = t0x*t2x + t0y*t2y + t0z*t2z;
			bend = S3rSaturate(1.f - a0);
			bendAhead = S3rSaturate(1.f - a1);
			if (bendAhead > bend) bend = bendAhead;
		}
		float wantThr = 0.f;
		if (spdNow < targetSpd * 0.86f) wantThr = 1.f;
		else if (spdNow < targetSpd * 1.04f) wantThr = 0.62f + 0.38f * S3rSaturate((targetSpd - spdNow) / max(0.01f, targetSpd * 0.18f));
		else wantThr = 0.52f;
		int aiBrake = 0;
		if (!cutting && !finishRush) {
			float need = S3rSaturate((bend - 0.08f) / 0.42f);
			wantThr *= (1.f - need * (0.22f * (1.f - 0.45f * sk)));
			float habit = c.aiBrakeHabit;
			float trig = 0.38f - 0.22f * habit;
			if (habit > 0.22f && need > trig && spdNow > targetSpd * 0.42f) {
				aiBrake = 1;
				wantThr *= (1.f - need * (0.40f + 0.45f * habit));
			}
		}
		if (c.offBand && !cutting) wantThr *= 0.70f;
		if (c.boostT>0) wantThr = min(1.f, wantThr * 1.05f);
		if (c.nitroT>0) wantThr = min(1.f, wantThr * 1.08f);
		if (c.slowT>0) wantThr *= 0.62f;
		TickEngine(c, wantThr, aiBrake, dt);
		float thrust = ((m_aiRaceLv >= AI_NORMAL) ? 70.f : (58.f + 10.f * sk)) * scv * S3rEngPower(c.rpm, c.throttle);
		if (finishRush) thrust *= 1.18f;
		if (cutting) thrust *= 1.10f;
		else if (!c.offBand) {
			thrust *= BandSpeedFactor(lat, vert);
			targetSpd *= BandSpeedFactor(lat, vert);
		}

		if (!cutting)
			thrust *= (1.f - bend * (0.18f * (1.f - 0.50f * sk)));
		if (aiBrake) {
			c.vx *= (1.f - (1.6f + 2.2f * c.aiBrakeHabit) * dt);
			c.vy *= (1.f - (1.6f + 2.2f * c.aiBrakeHabit) * dt);
			c.vz *= (1.f - (1.6f + 2.2f * c.aiBrakeHabit) * dt);
		}

		float edgeDanger = max(latAbs / max(0.01f, half), vertAbs / max(0.01f, half * 0.85f));
		if (!cutting && edgeDanger > 0.72f) {
			float caution = S3rSaturate((edgeDanger - 0.72f) / 0.55f);
			thrust *= (1.f - caution * (0.28f + 0.12f * sk));
			targetSpd *= (1.f - caution * 0.28f);
			if (spdNow > targetSpd) {
				float s = targetSpd / max(0.01f, spdNow);
				c.vx *= s; c.vy *= s; c.vz *= s;
			}
		}
		if (c.boostT>0) thrust *= 1.06f;
		if (c.nitroT>0) thrust *= NitroThrustMul(c);
		if (c.slowT>0) thrust *= 0.62f;
		if (c.fuel < 8.f) thrust *= 0.35f;
		if (c.fuel <= 0.01f) thrust = 0.f;

		float fx = sinf(c.yaw)*cosf(c.pitch), fy = sinf(c.pitch), fz = cosf(c.yaw)*cosf(c.pitch);
		if (cutting) {
			// カット中は合流点方向へ推進
			fx = txp; fy = typ; fz = tzp;
		} else {
			float tangW = 0.35f + 0.5f * sk;
			fx = fx * (1.f - tangW) + tx * tangW;
			fy = fy * (1.f - tangW) + ty * tangW;
			fz = fz * (1.f - tangW) + tz * tangW;
			S3rNorm3(fx, fy, fz);
		}
		c.vx += fx * thrust * dt; c.vy += fy * thrust * dt; c.vz += fz * thrust * dt;
		if (ty > 0.12f && !c.offBand && !cutting) {
			float climb = thrust * (0.15f + 0.35f * ty) * dt;
			c.vx += tx * climb; c.vy += ty * climb; c.vz += tz * climb;
		}

		// CPU ニトロ：遅れている／ラストスパートで使用（効きは難易度で自機に近づく）
		if (!demo && c.nitroStock > 0 && c.nitroT <= 0.f && !c.offBand) {
			float lead = ((float)c.lap + c.pathT) - plProg;
			BOOL wantN = FALSE;
			if (finishRush) wantN = TRUE;
			else if (lead < -0.06f) wantN = TRUE;
			else if (lead < -0.02f && sk > 0.28f && S3rRand01(m_rng) < min(0.42f, 2.2f * dt)) wantN = TRUE;
			else if (m_aiRaceLv >= AI_HARD && c.lap >= m_lapsTarget - 1 && lead < 0.05f) wantN = TRUE;
			if (lead > 0.22f) wantN = FALSE;
			if (wantN) TryUseNitro(c);
		}
	}
}

void CSoft3DRaceDlg::TickItems(float dt)
{
	for (int i=0;i<m_itemN;i++) if (!m_items[i].taken) m_items[i].spin += dt * 2.2f;
	if (m_phase == PHASE_DEMO) {
		// デモ中は見た目の回転のみ（音響アイテム適用なし）
		if (m_reverbFogBoost>0) m_reverbFogBoost=max(0.f,m_reverbFogBoost-dt*0.05f);
		if (m_eqDofBoost>0) m_eqDofBoost=max(0.f,m_eqDofBoost-dt*0.05f);
		return;
	}
	for (int i=0;i<m_craftN;i++) {
		S3rCraft& c=m_crafts[i];
		if (c.boostT>0) c.boostT-=dt; if (c.slowT>0) c.slowT-=dt; if (c.agilityT>0) c.agilityT-=dt;
		if (c.fogT>0) c.fogT-=dt; if (c.dofT>0) c.dofT-=dt; if (c.flashT>0) c.flashT-=dt;
		if (c.hitFlashT>0) c.hitFlashT=max(0.f,c.hitFlashT-dt);
		if (c.nitroT>0) {
			c.nitroT-=dt;
			if (c.nitroT<=0.f) {
				c.nitroT=0.f;
				if (c.isPlayer) m_standDirty = 1;
			}
		}
		if (c.explodeT>0) c.explodeT-=dt;
		if (c.smokeT>0) c.smokeT-=dt;
		TryPickupCraft(i);
	}
	if (m_reverbFogBoost>0) m_reverbFogBoost=max(0.f,m_reverbFogBoost-dt*0.05f);
	if (m_eqDofBoost>0) m_eqDofBoost=max(0.f,m_eqDofBoost-dt*0.05f);
}

void CSoft3DRaceDlg::TickPhysics(float dt)
{
	if (m_phase != PHASE_RACE && m_phase != PHASE_FINISH && m_phase != PHASE_DEMO) return;
	const int demo = (m_phase == PHASE_DEMO) ? 1 : 0;
	if (m_sfxHitCool > 0.f) m_sfxHitCool = max(0.f, m_sfxHitCool - dt);

	// player input + joypad（デモ中はAI任せ）
	S3rJoyState joy={}; UpdateJoypadState(joy);
	S3rCraft& pl = m_crafts[0];
	if (!demo && pl.alive && !pl.finished && m_phase==PHASE_RACE) {
		if (pl.courseOutCool > 0.f) {
			// 復帰クール中は入力推力を切って帯へ定着させる
			m_playerAccel = 0;
			TickEngine(pl, 0.f, 0, dt);
		} else {
		float steerX = joy.connected ? joy.lx : 0.f;
		float steerY = joy.connected ? -joy.ly : 0.f;
		if (joy.hat >= 0) {
			static const float hx[8]={0,0.7f,1,0.7f,0,-0.7f,-1,-0.7f};
			static const float hy[8]={1,0.7f,0,-0.7f,-1,-0.7f,0,0.7f};
			steerX += hx[joy.hat]; steerY += hy[joy.hat];
		}
		if (savedata.s3r_invert_y) steerY = -steerY;
		float ag = (pl.agilityT>0)?1.45f:1.f;
		pl.yaw = S3rNormAngle(pl.yaw + steerX * 1.9f * ag * dt);
		pl.pitch = S3rClamp(pl.pitch + steerY * 2.15f * ag * dt, -1.05f, 1.05f);
		// 入力が弱いときはコース接線へ追従（急勾配は許容。真下張り付きだけ防ぐ）
		if (fabsf(steerX) < 0.12f && fabsf(steerY) < 0.12f) {
			float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz; SplineFrame(pl.pathT+0.02f,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
			float want = S3rClamp(asinf(S3rClamp(ty,-1.f,1.f)), -0.95f, 0.95f);
			pl.pitch = S3rLerp(pl.pitch, want, min(1.f, 2.2f * dt));
		}
		if (joy.connected) {
			m_camYawOff = S3rNormAngle(m_camYawOff + joy.rx * 1.5f * dt);
			m_camPitchOff = S3rClamp(m_camPitchOff - joy.ry * 0.9f * dt, 0.05f, 0.42f);
		}
		BOOL keyAccel = (GetAsyncKeyState('W') & 0x8000) || (GetAsyncKeyState(VK_UP) & 0x8000)
			|| (GetAsyncKeyState(VK_SPACE) & 0x8000);
		BOOL keyBrake = (GetAsyncKeyState('S') & 0x8000) || (GetAsyncKeyState(VK_DOWN) & 0x8000);
		BOOL lmb = FALSE, rmb = FALSE;
		{
			HWND vw = m_view.GetSafeHwnd();
			if (vw && (m_accelHeld || ::GetCapture() == vw))
				lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? TRUE : FALSE;
			if (vw && (m_brakeHeld || ::GetCapture() == vw))
				rmb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? TRUE : FALSE;
			if (!lmb) lmb = m_accelHeld ? TRUE : FALSE;
			if (!rmb) rmb = m_brakeHeld ? TRUE : FALSE;
		}
		BOOL accel = lmb || keyAccel || (joy.connected && (joy.rt > 0.22f || (joy.buttons & 1)));
		BOOL brake = rmb || keyBrake || (joy.connected && (joy.lt > 0.28f || (joy.buttons & 2)));
		// マウス／キーのアクセルはパッドの誤ブレーキに負けない
		if (lmb || keyAccel) brake = FALSE;
		// ニトロ: N / Ctrl / パッド X・Y（ボタン2・3）
		BOOL nitroBtn = (GetAsyncKeyState('N') & 0x8000) || (GetAsyncKeyState(VK_CONTROL) & 0x8000)
			|| (joy.connected && (joy.buttons & ((1 << 2) | (1 << 3))));
		if (nitroBtn && !m_nitroBtnHeld)
			TryUseNitro(pl);
		m_nitroBtnHeld = nitroBtn ? 1 : 0;
		float fx=sinf(pl.yaw)*cosf(pl.pitch), fy=sinf(pl.pitch), fz=cosf(pl.yaw)*cosf(pl.pitch);
		const float scv = SpeedScale();
		float thrIn = 0.f;
		if (accel) {
			if (lmb || keyAccel) thrIn = 1.f;
			else if (joy.connected && joy.rt > 0.22f) thrIn = S3rClamp(joy.rt, 0.f, 1.f);
			else thrIn = 1.f;
		}
		TickEngine(pl, thrIn, brake ? 1 : 0, dt);
		float thrust = (70.f * scv) * S3rEngPower(pl.rpm, pl.throttle);
		if (pl.boostT>0) thrust *= 1.18f;
		if (pl.nitroT>0) thrust *= NitroThrustMul(pl);
		if (pl.slowT>0) thrust *= 0.55f;
		if (pl.offBand) thrust *= 0.62f;
		else {
			float plat, pvert, pcx, pcy, pcz;
			BandLocal(pl.x, pl.y, pl.z, pl.pathT, plat, pvert, pcx, pcy, pcz);
			thrust *= BandSpeedFactor(plat, pvert);
		}
		if (thrust > 0.2f) { pl.vx+=fx*thrust*dt; pl.vy+=fy*thrust*dt; pl.vz+=fz*thrust*dt; }
		// 急勾配：アクセル中はコース接線の上昇成分を補助（ピッチ制限だけでは足りない対策）
		if (accel && thrust > 1.f && !pl.offBand) {
			float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz; SplineFrame(pl.pathT+0.03f,px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz);
			if (ty > 0.12f) {
				float climb = thrust * (0.22f + 0.55f * ty) * dt;
				pl.vx += tx * climb; pl.vy += ty * climb; pl.vz += tz * climb;
			}
		}
		if (brake) { pl.vx*=(1.f-3.4f*dt); pl.vy*=(1.f-3.4f*dt); pl.vz*=(1.f-3.4f*dt); }
		// 旋回入力そのもので速度が落ちる（カーブの抵抗感）
		float turnIn = min(1.f, fabsf(steerX) + fabsf(steerY));
		if (turnIn > 0.08f) {
			float bleed = (0.55f + turnIn * 1.1f) * (1.f + SpeedToKmh(pl.vx,pl.vy,pl.vz) * 0.0015f);
			pl.vx*=(1.f-bleed*dt); pl.vy*=(1.f-bleed*dt); pl.vz*=(1.f-bleed*dt);
		}
		m_playerAccel = accel ? 1 : 0;
		}
	} else {
		m_playerAccel = 0;
		m_nitroBtnHeld = 0;
	}
	// 自機の実速度を平滑化（AIのペース基準）。減速時は速めに落とす
	{
		float ps = sqrtf(pl.vx*pl.vx+pl.vy*pl.vy+pl.vz*pl.vz);
		float k = (ps > m_playerSpdEma) ? min(1.f, 4.0f*dt) : min(1.f, 3.2f*dt);
		m_playerSpdEma = S3rLerp(m_playerSpdEma, ps, k);
	}

	UpdateRanks(); // 順位ボーナス用に AI 前に確定
	TickAi(dt);

	for (int i=0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i];
		if (!c.alive) continue;
		if (c.finished) continue;
		const int aiFinishRail = (!demo && m_phase == PHASE_FINISH && !c.isPlayer) ? 1 : 0;
		if (aiFinishRail) {
			// 自機ゴール後：地形・カットに掴ませず帯中央を巡航してフィニッシュさせる
			c.aiCutT = -1.f; c.aiCutTimer = 0.f;
			c.courseOutCool = 0.f;
			c.offBand = 0; c.offBandT = 0.f;
			c.fuel = 100.f;
			if (c.hp < 80.f) c.hp = 80.f;
			float rcx,rcy,rcz,rtx,rty,rtz,rnx,rny,rnz,rbx,rby,rbz;
			SplineFrame(c.pathT, rcx,rcy,rcz, rtx,rty,rtz, rnx,rny,rnz, rbx,rby,rbz);
			c.x = rcx; c.y = rcy; c.z = rcz;
			c.yaw = atan2f(rtx, rtz);
			c.pitch = S3rClamp(asinf(S3rClamp(rty, -1.f, 1.f)), -0.55f, 0.55f);
			float railCap = FinishSimRailSpeed(c);
			c.vx = rtx * railCap; c.vy = rty * railCap; c.vz = rtz * railCap;
		}

		// 進行方向へのグリップ＋横滑り抵抗（カーブで速度が流れる）
		float fx=sinf(c.yaw)*cosf(c.pitch), fy=sinf(c.pitch), fz=cosf(c.yaw)*cosf(c.pitch);
		float spd=sqrtf(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz);
		float along=0.f, latSpd=0.f;
		if (spd>0.01f) {
			along=c.vx*fx+c.vy*fy+c.vz*fz;
			float lx=c.vx-fx*along, ly=c.vy-fy*along, lz=c.vz-fz*along;
			latSpd=sqrtf(lx*lx+ly*ly+lz*lz);
			// 完全固定せず、ある程度スライドを残す
			float grip=(c.offBand?0.55f:1.45f)*(c.agilityT>0?1.35f:1.f);
			if (c.craftKnockT > 0.f) grip *= 0.12f;
			c.vx-=lx*min(1.f,grip*dt); c.vy-=ly*min(1.f,grip*dt); c.vz-=lz*min(1.f,grip*dt);
			along=c.vx*fx+c.vy*fy+c.vz*fz;
			lx=c.vx-fx*along; ly=c.vy-fy*along; lz=c.vz-fz*along;
			latSpd=sqrtf(lx*lx+ly*ly+lz*lz);
			// 横速度が大きいほど前方速度が削られる（カーブ抵抗）
			if (along > 0.f && latSpd > 0.5f) {
				float slip = latSpd / (fabsf(along) + latSpd + 1.f);
				float cornerBleed = (0.7f + 0.012f * fabsf(along)) * slip * slip * (c.offBand?1.6f:1.f);
				along *= (1.f - min(0.85f, cornerBleed * dt * 3.2f));
				c.vx = fx*along + lx; c.vy = fy*along + ly; c.vz = fz*along + lz;
			}
		}
		// 空気抵抗＋コース曲率による衰退（自機・敵共通。難所で 70→30km/h 付近まで落ちる）
		{
			spd = sqrtf(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz);
			float lin = c.offBand ? 0.50f : 0.22f;
			float quad = c.offBand ? 0.0021f : 0.00135f;
			if (c.isPlayer && !m_playerAccel && !demo) lin += 0.7f;
			float t0x,t0y,t0z,t1x,t1y,t1z;
			SplineTangent(c.pathT, t0x,t0y,t0z);
			SplineTangent(c.pathT + 0.04f, t1x,t1y,t1z);
			float align = t0x*t1x + t0y*t1y + t0z*t1z;
			float bend = S3rSaturate(1.f - align); // 直線0、急カーブで大きく
			float curveDrag = bend * (1.15f + 0.045f * spd);
			float damp = lin + quad * spd + curveDrag;
			c.vx*=(1.f-damp*dt); c.vy*=(1.f-damp*dt); c.vz*=(1.f-damp*dt);
		}
		// 絶対上限（帯横軸中央で最大。上下に浮くと落ちる）
		float maxSpd = RaceSpeedCap(c.boostT > 0.f ? 1 : 0) * NitroSpeedMul(c);
		if (!c.isPlayer || demo) {
			const float plNow = sqrtf(m_crafts[0].vx*m_crafts[0].vx + m_crafts[0].vy*m_crafts[0].vy + m_crafts[0].vz*m_crafts[0].vz);
			const int finishRush = (!demo && m_phase == PHASE_FINISH) ? 1 : 0;
			float pace = demo ? (RaceSpeedCap(0) * 0.58f) : (finishRush ? RaceSpeedCap(0) * 0.96f : m_playerSpdEma);
			if (!demo && !finishRush && pace < plNow) pace = plNow;
			if (!demo && !finishRush) {
				float minP = RaceSpeedCap(0) * S3rAiMinPaceFrac(m_aiRaceLv);
				if (pace < minP) pace = minP;
			}
			float sk = c.aiSkill;
			if (demo && c.isPlayer && sk < 0.45f) sk = 0.70f;
			float indep = demo ? 0.50f : (finishRush ? 0.92f : AiPaceIndep(sk));
			float rubber = pace * (0.90f + 0.06f * sk);
			float freeT = maxSpd * S3rAiMaxPaceFrac(m_aiRaceLv);
			float aiCap = S3rLerp(rubber, freeT, indep);
			float hardCap = 9.f, softFloor = 0.f;
			AiCapPair(sk, maxSpd, pace, plNow, indep, demo, finishRush, softFloor, hardCap);
			if (demo)
				hardCap = min(hardCap, pace * (1.05f + 0.12f * sk));
			if (aiCap < softFloor) aiCap = softFloor;
			if (aiCap > hardCap) aiCap = hardCap;
			if (aiCap < maxSpd) maxSpd = aiCap;
		}
		{
			float blat, bvert, bcx, bcy, bcz;
			BandLocal(c.x, c.y, c.z, c.pathT, blat, bvert, bcx, bcy, bcz);
			maxSpd *= BandSpeedFactor(blat, bvert);
		}
		if (c.offBand) {
			// 計画カット中だけ帯外でもレース速度を維持（フリー走行は上限だけ落とす）
			int cutting = ((!c.isPlayer || demo) && c.aiCutT >= 0.f && c.aiCutTimer > 0.f) ? 1 : 0;
			if (cutting) maxSpd = min(maxSpd, RaceSpeedCap(0) * (0.72f + 0.12f * c.aiSkill));
			else maxSpd *= 0.78f;
		}
		if (!c.isPlayer || demo) {
			float ceil = RaceSpeedCap(0) * S3rAiMaxPaceFrac(m_aiRaceLv);
			if (c.nitroT > 0.f)
				ceil = min(RaceSpeedCap(c.boostT > 0.f ? 1 : 0) * NitroSpeedMul(c), RaceSpeedCap(0) * 1.16f);
			if (!demo && m_phase == PHASE_RACE) {
				const S3rCraft& pl0 = m_crafts[0];
				float lead = ((float)c.lap + c.pathT) - ((float)pl0.lap + pl0.pathT);
				if (lead < -0.08f && m_aiRaceLv >= AI_NORMAL) {
					float boost = S3rSaturate((-lead - 0.08f) / 0.22f);
					float extra = (m_aiRaceLv >= AI_FEROCIOUS) ? 0.10f : ((m_aiRaceLv >= AI_HARD) ? 0.07f : 0.06f);
					ceil = min(RaceSpeedCap(0) * (S3rAiMaxPaceFrac(m_aiRaceLv) + extra * boost),
						RaceSpeedCap(c.boostT > 0.f ? 1 : 0) * NitroSpeedMul(c));
				}
			}
			if (maxSpd > ceil) maxSpd = ceil;
		}
		spd=sqrtf(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz);
		if (c.craftKnockT <= 0.f && spd>maxSpd){ float s=maxSpd/spd; c.vx*=s;c.vy*=s;c.vz*=s; }

		if (c.courseOutCool > 0.f) c.courseOutCool = max(0.f, c.courseOutCool - dt);
		if (c.obsHitCool > 0.f) c.obsHitCool = max(0.f, c.obsHitCool - dt);
		if (c.craftHitCool > 0.f) c.craftHitCool = max(0.f, c.craftHitCool - dt);
		if (c.fxHitCool > 0.f) c.fxHitCool = max(0.f, c.fxHitCool - dt);
		if (c.craftKnockT > 0.f) c.craftKnockT = max(0.f, c.craftKnockT - dt);

		c.x+=c.vx*dt; c.y+=c.vy*dt; c.z+=c.vz*dt;

		float preLat, preVert, preCx, preCy, preCz;
		BandLocal(c.x, c.y, c.z, c.pathT, preLat, preVert, preCx, preCy, preCz);
		(void)preCx; (void)preCy; (void)preCz;
		const float preHalf = BandHalfWidth();
		const int onLane = (fabsf(preLat) <= preHalf * 1.55f && fabsf(preVert) <= preHalf * 1.35f) ? 1 : 0;

		// 地形ヒット。床=ノックアップ、天井(トンネル)=ノックダウン、壁=横弾き。
		// 光帯上の床は溝クリアランス不足で常時当たるので、深くめり込んだときだけ。
		if (!aiFinishRail) {
			float gy = GroundY(c.x, c.z);
			const float rawH = RawGroundY(c.x, c.z);
			int hitTerr = 0;
			spd = sqrtf(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz);

			const float floorClear = onLane ? 0.42f : 1.22f;
			const int inTun = (rawH > gy + 3.2f) ? 1 : 0;
			int tunSeg = inTun;
			if (!tunSeg) {
				int pi = S3rPathIdx(c.pathT);
				if (m_pathDeep[pi]) tunSeg = 1;
				else {
					const int n = S3R_PATH_SAMPLES;
					for (int d = 1; d <= 28 && !tunSeg; d++) {
						int ip = pi + d; if (ip >= n) ip -= n;
						int im = pi - d; if (im < 0) im += n;
						if (m_pathDeep[ip] || m_pathDeep[im]) tunSeg = 1;
					}
				}
			}
			const int useTube = (inTun || (tunSeg && onLane)) ? 1 : 0;

			if (!useTube) {
				if (c.y < gy + floorClear) {
					c.y = gy + floorClear;
					if (c.vy < 8.f) c.vy = 9.5f + min(8.f, 0.18f * spd);
					c.vx *= 0.94f; c.vz *= 0.94f;
					c.rpm *= 0.92f; c.throttle *= 0.85f;
					ApplyCraftDamage(c, onLane ? (8.f * dt) : (16.f * dt), 0);
					hitTerr = 1;
				}
			}

			if (useTube) {
				float tunR = S3rTunR(m_bandHalf);
				int pi = S3rPathIdx(c.pathT);
				tunR *= (1.f + 0.85f * S3rTunFlare(m_pathDeep, pi));
				if (!m_pathDeep[pi] && tunSeg) tunR *= 1.25f;
				float qx,qy,qz,tx,ty,tz,nnx,nny,nnz,bbx,bby,bbz;
				SplineFrame(c.pathT, qx,qy,qz, tx,ty,tz, nnx,nny,nnz, bbx,bby,bbz);
				const float tubeFloor = qy - tunR * 0.92f;
				if (c.y < tubeFloor + floorClear) {
					c.y = tubeFloor + floorClear;
					if (c.vy < 0.f) c.vy = 0.f;
				}
				const float ceilY = qy + tunR * 0.92f;
				if (c.y > ceilY) {
					c.y = ceilY;
					if (c.vy > -8.f) c.vy = -9.5f - min(8.f, 0.18f * spd);
					c.vx *= 0.93f; c.vz *= 0.93f;
					c.rpm *= 0.90f; c.throttle *= 0.82f;
					ApplyCraftDamage(c, 12.f * dt, 0);
					hitTerr = 1;
				}
				float ddx = c.x - qx, ddy = c.y - qy, ddz = c.z - qz;
				float lat = ddx*bbx + ddy*bby + ddz*bbz;
				float vert = ddx*nnx + ddy*nny + ddz*nnz;
				float rad = sqrtf(lat*lat + vert*vert);
				float lim = tunR * 0.90f;
				if (rad > lim && rad > 1e-4f) {
					float k = (rad - lim) / rad;
					c.x -= (bbx*lat + nnx*vert) * k;
					c.y -= (bby*lat + nny*vert) * k;
					c.z -= (bbz*lat + nnz*vert) * k;
					float nxw = (bbx*lat + nnx*vert) / rad;
					float nyw = (bby*lat + nny*vert) / rad;
					float nzw = (bbz*lat + nnz*vert) / rad;
					float vin = c.vx*nxw + c.vy*nyw + c.vz*nzw;
					if (vin > 0.f) { c.vx -= nxw*vin; c.vy -= nyw*vin; c.vz -= nzw*vin; }
					const float kick = 4.2f + 0.08f * spd;
					c.vx -= nxw * kick; c.vy -= nyw * kick * 0.55f; c.vz -= nzw * kick;
					ApplyCraftDamage(c, 12.f * dt, 0);
					hitTerr = 1;
				}
			} else {
				const float pr = 0.95f;
				const float cliff = 3.2f;
				float gL = GroundY(c.x - pr, c.z);
				float gR = GroundY(c.x + pr, c.z);
				float gF = GroundY(c.x, c.z - pr);
				float gB = GroundY(c.x, c.z + pr);
				const float wallKick = 10.f + 0.20f * spd;
				if (!onLane) {
					if (gL > gy + cliff) { c.x += pr * 0.40f; if (c.vx < wallKick) c.vx = wallKick; hitTerr = 1; }
					if (gR > gy + cliff) { c.x -= pr * 0.40f; if (c.vx > -wallKick) c.vx = -wallKick; hitTerr = 1; }
					if (gF > gy + cliff) { c.z += pr * 0.40f; if (c.vz < wallKick) c.vz = wallKick; hitTerr = 1; }
					if (gB > gy + cliff) { c.z -= pr * 0.40f; if (c.vz > -wallKick) c.vz = -wallKick; hitTerr = 1; }
				}
			}
			if (hitTerr && c.fxHitCool <= 0.f) {
				SpawnFxBurst(FX_DUST, c.x, c.y, c.z, 0.f, 1.f, 0.f, 7, 5.5f, 0.55f, 0.42f, 0.28f);
				SpawnFxBurst(FX_SPARK, c.x, c.y + 0.3f, c.z, 0.f, 1.f, 0.f, 5, 8.5f, 1.f, 0.82f, 0.35f);
				c.fxHitCool = 0.15f;
			}
			if (hitTerr && (!c.isPlayer || demo) && c.craftKnockT <= 0.12f)
				AbortAiToLine(c, 18.f);
		}

		float prevT=c.pathT;
		spd=sqrtf(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz);
		if ((!c.isPlayer || demo) && !aiFinishRail && spd < 5.0f && c.courseOutCool <= 0.f && c.craftKnockT <= 0.f) {
			AbortAiToLine(c, 10.f);
			spd=sqrtf(c.vx*c.vx+c.vy*c.vy+c.vz*c.vz);
		}
		c.pathT = AdvancePathT(c.x,c.y,c.z,c.pathT,spd,dt);
		float lat, vert, cx, cy, cz;
		BandLocal(c.x, c.y, c.z, c.pathT, lat, vert, cx, cy, cz);
		float half=BandHalfWidth();
		float latAbs=fabsf(lat), vertAbs=fabsf(vert);
		// 許容マージン広め（トリッキーな操作・短い帯外を許容）
		const float onLat = half * 1.55f;
		const float onVert = half * 1.35f;
		if (latAbs <= onLat && vertAbs <= onVert) {
			c.offBand = 0;
			c.fuel = min(100.f, c.fuel + 28.f * dt);
			if (latAbs <= half * 0.42f && vertAbs <= half * 0.38f) {
				c.hp = min(100.f, c.hp + 14.f * dt);
			}
			// 帯中央寄りでのみチェックポイント更新（端保存→復帰ループ防止）
			if (latAbs <= half * 0.80f && vertAbs <= half * 0.70f) {
				float tcx, tcy, tcz; SplinePoint(c.pathT, tcx, tcy, tcz);
				c.chkX = tcx; c.chkY = tcy; c.chkZ = tcz;
				float ttx,tty,ttz; SplineTangent(c.pathT, ttx,tty,ttz);
				c.chkYaw = atan2f(ttx, ttz);
				c.chkPitch = S3rClamp(asinf(S3rClamp(tty, -1.f, 1.f)), -0.55f, 0.55f);
				c.chkPathT = c.pathT;
			}
			c.offBandT = 0.f;
			float tx,ty,tz,nx,ny,nz,bx,by,bz;
			SplineFrame(c.pathT, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
			float pull = min(1.f, 0.55f * dt);
			float rail = 0.08f;
			if (c.craftKnockT > 0.f) rail = 0.f;
			else if (!c.isPlayer || demo) {
				float sk = c.aiSkill;
				if (demo && c.isPlayer && sk < 0.45f) sk = 0.70f;
				int plRank = (m_crafts[0].rank > 0) ? m_crafts[0].rank : 1;
				int aiRank = (c.rank > 0) ? c.rank : 2;
				if (!demo && aiRank > plRank) sk = S3rClamp(sk + (float)(aiRank - plRank) * 0.07f, 0.f, 1.f);
				rail = 0.05f + 0.06f * sk;
				if (!demo && m_aiRaceLv <= AI_EASY) rail = 0.035f + 0.04f * sk;
				// 計画カット中は帯内レールを弱めて弦へ出やすくする
				if (c.aiCutT >= 0.f && c.aiCutTimer > 0.f) rail = 0.04f;
			}
			c.x -= (bx * lat + nx * vert) * rail * pull;
			c.y -= (by * lat + ny * vert) * rail * pull;
			c.z -= (bz * lat + nz * vert) * rail * pull;
		} else if (c.courseOutCool <= 0.f) {
			c.offBand = 1;
			c.offBandT += dt;
			if (c.craftKnockT > 0.f) {
				// 機体同士の吹っ飛び中はレールへ引き戻さない
			} else if (!c.isPlayer || demo) {
				const int cutting = (c.aiCutT >= 0.f && c.aiCutTimer > 0.f) ? 1 : 0;
				if (cutting) {
					// 計画ショートカット中：緩い燃料減＋合流点へ。危険なら即ライン復帰
					c.fuel = max(0.f, c.fuel - 9.f * dt);
					c.vx *= (1.f - 0.45f * dt);
					c.vy *= (1.f - 0.45f * dt);
					c.vz *= (1.f - 0.45f * dt);
					float jx,jy,jz; SplinePoint(c.aiCutT, jx,jy,jz);
					float hx = jx - c.x, hy = jy - c.y, hz = jz - c.z;
					float hd = sqrtf(hx*hx+hy*hy+hz*hz);
					if (hd > 0.01f) {
						float pull = 3.5f + 2.5f * c.aiSkill;
						c.vx += (hx / hd) * pull * dt;
						c.vy += (hy / hd) * pull * dt;
						c.vz += (hz / hd) * pull * dt;
						c.yaw = S3rNormAngle(c.yaw + S3rNormAngle(atan2f(hx, hz) - c.yaw) * min(1.f, 2.8f*dt));
					}
					if (c.fuel < 52.f || c.offBandT > 1.15f || c.hp < 40.f) {
						AbortAiToLine(c, 32.f);
					}
				} else {
					// 事故帯外：早めにラインへ戻す（COURSE OUT 連発を避ける）
					c.fuel = max(0.f, c.fuel - 8.f * dt);
					float starve = 1.f + (1.f - c.fuel / 100.f) * 0.70f;
					c.vx *= (1.f - 0.65f * starve * dt);
					c.vy *= (1.f - 0.65f * starve * dt);
					c.vz *= (1.f - 0.65f * starve * dt);
					float tx,ty,tz,nx,ny,nz,bx,by,bz;
					SplineFrame(c.pathT, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
					float hx = cx - c.x, hy = cy - c.y, hz = cz - c.z;
					float hd = sqrtf(hx*hx+hy*hy+hz*hz);
					if (hd > 0.01f) {
						float pull = (3.5f + 4.f * c.aiSkill);
						c.vx += (hx / hd) * pull * dt;
						c.vy += (hy / hd) * pull * dt;
						c.vz += (hz / hd) * pull * dt;
					}
					c.yaw = S3rNormAngle(c.yaw + S3rNormAngle(atan2f(tx,tz) - c.yaw) * min(1.f, 4.0f*dt));
					if (c.fuel < 55.f || c.offBandT > (0.28f + 0.90f * (1.f - c.aiSkill))) {
						AbortAiToLine(c, 22.f);
					}
				}
			} else {
				// 自機：帯外フリー走行。推進力0で離脱地点（帯中央）へ復帰
				c.fuel = max(0.f, c.fuel - 11.f * dt);
				float starve = 1.f + (1.f - c.fuel / 100.f) * 0.85f;
				c.vx *= (1.f - 0.72f * starve * dt); c.vy *= (1.f - 0.72f * starve * dt); c.vz *= (1.f - 0.72f * starve * dt);
					if (c.fuel <= 0.01f) {
					RespawnCraftToCheckpoint(c, 80.f, 2.0f);
					if (m_phase == PHASE_RACE) {
						m_clearBakeText = LL14(L"COURSE OUT", L"COURSE OUT", L"HORS PISTE", L"FUORI PISTA", L"FUERA DE PISTA",
							L"코스 아웃", L"冲出赛道", L"خارج المسار", L"ВНЕ ТРАССЫ", L"COURSE OUT", L"FORA DA PISTA", L"COURSE OUT", L"POZA TORU", L"KURS DIŞI");
						m_clearBakeA = 1.f; m_clearDirty = 1; m_overlayHold = 1.5f;
						Soft3DSfxUi(S3SFX_COURSEOUT, 0);
						if (c.isPlayer) {
							c.flashT = max(c.flashT, 0.40f);
							SpawnFxBurst(FX_DUST, c.x, c.y + 0.1f, c.z, 0.f, 1.f, 0.f, 18, 5.5f, 0.55f, 0.48f, 0.35f);
							SpawnFxBurst(FX_SMOKE, c.x, c.y + 0.2f, c.z, 0.f, 1.f, 0.f, 8, 2.8f, 0.35f, 0.35f, 0.38f);
						}
					}
				}
			}
		} else {
			// クール中：帯中央へ強吸着＋燃料回復（連発コースアウト防止）
			c.offBand = 0;
			c.pathT = c.chkPathT;
			float tx,ty,tz,nx,ny,nz,bx,by,bz;
			SplineFrame(c.pathT, cx,cy,cz, tx,ty,tz, nx,ny,nz, bx,by,bz);
			float snap = min(1.f, 8.f * dt);
			c.x = S3rLerp(c.x, cx, snap);
			c.y = S3rLerp(c.y, cy, snap);
			c.z = S3rLerp(c.z, cz, snap);
			c.yaw = S3rNormAngle(c.yaw + S3rNormAngle(atan2f(tx, tz) - c.yaw) * snap);
			c.pitch = S3rLerp(c.pitch, S3rClamp(asinf(S3rClamp(ty, -1.f, 1.f)), -0.55f, 0.55f), snap);
			float along = c.vx*tx + c.vy*ty + c.vz*tz;
			if (along < 0.f) along = 0.f;
			float keep = min(along, 22.f / 3.6f);
			c.vx = tx * keep; c.vy = ty * keep; c.vz = tz * keep;
			c.fuel = min(100.f, c.fuel + 35.f * dt);
			c.chkX = cx; c.chkY = cy; c.chkZ = cz;
		}
		if (c.courseOutCool <= 0.f)
			c.fuel = max(0.f, c.fuel - (spd>40.f?3.5f:1.2f)*dt);

		// ラップ判定：前進でスタートを跨いだときのみ＋最低周回時間（誤ラップ防止）
		{
			float dT = c.pathT - prevT;
			while (dT > 0.5f) dT -= 1.f;
			while (dT < -0.5f) dT += 1.f;
			float minLap = (m_pathLen > 1.f) ? (m_pathLen / 220.f) : 12.f;
			if (minLap < 10.f) minLap = 10.f;
			// 物理上限付近でも届かない短タイムは誤ラップ／高速シミュ飛越し
			float honestMin = (m_pathLen > 1.f) ? (m_pathLen / max(1.f, RaceSpeedCap(1) * 1.25f)) : minLap;
			if (honestMin < minLap) honestMin = minLap;
			if (m_phase == PHASE_FINISH && !c.isPlayer) {
				float rushMin = (m_pathLen > 1.f) ? (m_pathLen / max(1.f, FinishSimRailSpeed(c) * 1.12f)) : 12.f;
				if (rushMin < 12.f) rushMin = 12.f;
				honestMin = rushMin;
			}
			BOOL crossed = (prevT > 0.82f && c.pathT < 0.18f && dT > 0.f && dT < 0.35f);
			// スタート付近に実在することも要求（pathTだけ飛んでも無効）。ゴール後レールはXYZが帯上なので pathT 跨ぎで足りる
			float sx,sy,sz; SplinePoint(0.f, sx,sy,sz);
			float dx=c.x-sx, dy=c.y-sy, dz=c.z-sz;
			float nearStart = dx*dx+dy*dy+dz*dz;
			float startR = BandHalfWidth() * 3.5f;
			if (crossed && c.raceTime >= honestMin && (aiFinishRail || nearStart < startR*startR)) {
				if (c.lapTimesN < 12) {
					c.lapTimes[c.lapTimesN] = c.raceTime;
					c.lapTimesN++;
				}
				c.lap++;
				if (c.nitroStock < S3R_NITRO_MAX) {
					c.nitroStock++;
					m_standDirty = 1;
				}
				if (c.raceTime > 0.5f && c.raceTime < c.bestLap) c.bestLap = c.raceTime;
				c.raceTime = 0.f;
				m_standDirty = 1;
				if (c.lap >= m_lapsTarget) {
					if (demo) {
						c.lap = 0; // デモは周回し続けてステージを見せる
					} else {
						c.finished = 1; c.finishTime = m_raceClock;
						if (c.isPlayer) {
							m_phase = PHASE_FINISH; m_podiumT = 0.f; m_finishSimT = 0.f;
							m_clearBakeText = LL14(L"FINISH!", L"FINISH!", L"ARRIVÉE!", L"ARRIVO!", L"¡META!", L"피니시!", L"完赛!", L"نهاية!", L"ФИНИШ!", L"ZIEL!", L"CHEGADA!", L"FINISH!", L"META!", L"BİTİŞ!");
							m_clearBakeA=1.f; m_clearDirty=1; m_overlayHold = 99.f;
							Soft3DSfxUi(S3SFX_FINISH, 0);
							c.flashT = max(c.flashT, 0.70f);
							SpawnBlast(c.x, c.y, c.z);
							SpawnFxBurst(FX_PICK, c.x, c.y + 0.5f, c.z, 0.f, 1.f, 0.f, 28, 10.f, 1.f, 0.85f, 0.25f);
							SpawnFxBurst(FX_EMBER, c.x, c.y + 0.3f, c.z, 0.f, 1.f, 0.f, 18, 8.f, 1.f, 0.55f, 0.12f);
						}
					}
				} else if (c.isPlayer && m_phase == PHASE_RACE) {
					wchar_t lapBuf[32];
					swprintf_s(lapBuf, L"LAP %d/%d", min(m_lapsTarget, c.lap + 1), m_lapsTarget);
					m_clearBakeText = lapBuf;
					m_clearBakeA = 1.f; m_clearDirty = 1; m_overlayHold = 1.8f;
					Soft3DSfxUi(S3SFX_LAP, c.lap);
					c.flashT = max(c.flashT, 0.38f);
					SpawnFxBurst(FX_PICK, c.x, c.y + 0.45f, c.z, 0.f, 1.f, 0.f, 18, 8.5f, 0.35f, 0.95f, 1.f);
					SpawnFxBurst(FX_SPARK, c.x, c.y + 0.25f, c.z, 0.f, 1.f, 0.f, 12, 7.0f, 1.f, 1.f, 0.55f);
				}
			}
		}
		c.raceTime += dt;

		// 自機の逆走判定（コース接線と速度の内積）
		if (c.isPlayer && m_phase == PHASE_RACE && !c.finished) {
			float tx,ty,tz,nx,ny,nz,bx,by,bz,px,py,pz;
			SplineFrame(c.pathT + 0.01f, px,py,pz, tx,ty,tz, nx,ny,nz, bx,by,bz);
			float along = c.vx*tx + c.vy*ty + c.vz*tz;
			const int ww = (spd > 12.f && along < -0.2f * spd) ? 1 : 0;
			if (ww && !m_wrongWay) {
				Soft3DSfxUi(S3SFX_WRONG, 0);
				c.flashT = max(c.flashT, 0.28f);
				SpawnFxBurst(FX_SPARK, c.x, c.y + 0.3f, c.z, 0.f, 1.f, 0.f, 10, 5.5f, 1.f, 0.25f, 0.15f);
			}
			m_wrongWay = ww;
			if (ww && m_overlayHold <= 0.f) {
				m_clearBakeText = LL14(L"逆走中", L"WRONG WAY", L"SENS INVERSE", L"CONTROMANO", L"SENTIDO CONTRARIO",
					L"역주행", L"逆行中", L"اتجاه خاطئ", L"НЕ ТУДА", L"FALSCHE RICHTUNG", L"SENTIDO ERRADO", L"VERKEERDE KANT", L"ZŁY KIERUNEK", L"TERS YÖN");
				m_clearBakeA = 1.f; m_clearDirty = 1;
			} else if (!ww && m_overlayHold <= 0.f && m_clearBakeText.Find(L"逆走") >= 0) {
				m_clearBakeText = L""; m_clearBakeA = 0.f; m_clearDirty = 1;
			} else if (!ww && m_overlayHold <= 0.f && m_clearBakeText.Find(L"WRONG") >= 0) {
				m_clearBakeText = L""; m_clearBakeA = 0.f; m_clearDirty = 1;
			}
		}

		// 障害物: 幹・枝・冠・横梁まで見た目どおり当たる。法線方向へノックバック。
		// ニトロ中はすり抜け（自機・AIとも）
		if (!aiFinishRail && c.nitroT <= 0.f) {
			S3rObsHit best; best.hit = 0; best.nx = 0.f; best.ny = 1.f; best.nz = 0.f; best.push = 0.f;
			int hitO = -1;
			const float craftR = 0.92f;
			for (int o=0;o<m_obsN;o++){
				S3rObsHit h = S3rCraftVsObs(m_themeActive, m_obs[o], c.x, c.y, c.z, craftR);
				if (!h.hit) continue;
				if (!best.hit || h.push > best.push) { best = h; hitO = o; }
			}
			if (best.hit && hitO >= 0) {
				float nx = best.nx, ny = best.ny, nz = best.nz;
				S3rNorm3(nx, ny, nz);
				float knx = nx, kny = ny, knz = nz;
				if (fabsf(ny) <= 0.50f) {
					float px,py,pz,tx,ty,tz,nnx,nny,nnz,bx,by,bz;
					SplineFrame(c.pathT, px,py,pz, tx,ty,tz, nnx,nny,nnz, bx,by,bz);
					const float obsLat=(m_obs[hitO].x-px)*bx+(m_obs[hitO].y-py)*by+(m_obs[hitO].z-pz)*bz;
					float ox=-bx, oy=-by, oz=-bz;
					if (obsLat > 0.12f) { ox=-bx; oy=-by; oz=-bz; }
					else if (obsLat < -0.12f) { ox=bx; oy=by; oz=bz; }
					else {
						const float clat=(c.x-px)*bx+(c.y-py)*by+(c.z-pz)*bz;
						if (clat >= 0.f) { ox=bx; oy=by; oz=bz; }
						else { ox=-bx; oy=-by; oz=-bz; }
					}
					knx = nx * 0.78f + ox * 0.22f;
					kny = ny * 0.78f + oy * 0.22f;
					knz = nz * 0.78f + oz * 0.22f;
					S3rNorm3(knx, kny, knz);
				}
				const float push = best.push + 0.22f;
				c.x += knx * push; c.y += kny * push; c.z += knz * push;
				float vin = c.vx * knx + c.vy * kny + c.vz * knz;
				if (vin < 0.f) {
					c.vx -= knx * vin; c.vy -= kny * vin; c.vz -= knz * vin;
				}
				const float kick = 4.0f + 0.08f * spd;
				const float yk = (fabsf(kny) > 0.50f) ? 1.05f : 0.50f;
				c.vx += knx * kick; c.vy += kny * kick * yk; c.vz += knz * kick;
				c.rpm *= 0.88f; c.throttle *= 0.80f;
				if (c.fxHitCool <= 0.f) {
					SpawnFxBurst(FX_SPARK, c.x, c.y + 0.35f, c.z, knx, kny, knz, 10, 9.5f, 1.f, 0.85f, 0.38f);
					SpawnFxBurst(FX_SMOKE, c.x, c.y + 0.2f, c.z, knx, 0.7f, knz, 4, 2.4f, 0.28f, 0.26f, 0.24f);
					c.fxHitCool = 0.16f;
				}
				if (m_obs[hitO].hazard && m_obs[hitO].damage > 0.f && c.obsHitCool <= 0.f) {
					ApplyCraftDamage(c, 10.f, 1);
					c.obsHitCool = 0.48f;
					SpawnFxBurst(FX_EMBER, c.x, c.y + 0.4f, c.z, knx, kny, knz, 6, 7.5f, 1.f, 0.40f, 0.10f);
					if (c.isPlayer && !demo) {
						Soft3DSfxOneShot(S3SFX_HIT, c.x, c.y, c.z);
						m_sfxHitCool = 0.22f;
					}
				}
			}
		}
	}

	// 機体同士：少し弾く。HPは約1/30（dt非依存）。残骸は静止障害。
	// 自機ニトロ中はこちらはノックバックもダメージも受けない（相手は通常どおり弾かれる）
	for (int i=0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i];
		for (int j=i+1;j<m_craftN;j++){
			S3rCraft& o=m_crafts[j];
			const int cGo = (c.alive && !c.retired && !c.finished) ? 1 : 0;
			const int oGo = (o.alive && !o.retired && !o.finished) ? 1 : 0;
			if (!cGo && !oGo) continue;
			float dx=c.x-o.x, dy=c.y-o.y, dz=c.z-o.z;
			float d2=dx*dx+dy*dy+dz*dz;
			const float rr=2.2f;
			if (d2 >= rr*rr || d2 <= 1e-4f) continue;
			float d=sqrtf(d2); float nx=dx/d, ny=dy/d, nz=dz/d;
			const int cRam = (c.isPlayer && c.nitroT > 0.f) ? 1 : 0;
			const int oRam = (o.isPlayer && o.nitroT > 0.f) ? 1 : 0;
			const float gap = rr - d;
			if (cRam && !oRam) {
				if (oGo) { o.x -= nx * gap; o.y -= ny * gap; o.z -= nz * gap; }
			} else if (oRam && !cRam) {
				if (cGo) { c.x += nx * gap; c.y += ny * gap; c.z += nz * gap; }
			} else {
				float push = gap * 0.5f;
				if (cGo) { c.x+=nx*push; c.y+=ny*push; c.z+=nz*push; }
				if (oGo) { o.x-=nx*push; o.y-=ny*push; o.z-=nz*push; }
			}
			if (c.craftHitCool <= 0.f && o.craftHitCool <= 0.f) {
				float closing = (c.vx - o.vx)*nx + (c.vy - o.vy)*ny + (c.vz - o.vz)*nz;
				float bounce = 12.5f + 0.48f * fabsf(closing);
				if (cGo && !cRam) {
					if (closing < 0.f) { c.vx -= nx * closing * 0.50f; c.vy -= ny * closing * 0.50f; c.vz -= nz * closing * 0.50f; }
					c.vx += nx * bounce; c.vy += ny * bounce * 0.42f; c.vz += nz * bounce;
					c.craftKnockT = max(c.craftKnockT, 0.85f);
					c.rpm *= 0.88f; c.throttle *= 0.80f;
				}
				if (oGo && !oRam) {
					if (closing < 0.f) { o.vx += nx * closing * 0.50f; o.vy += ny * closing * 0.50f; o.vz += nz * closing * 0.50f; }
					o.vx -= nx * bounce; o.vy -= ny * bounce * 0.42f; o.vz -= nz * bounce;
					o.craftKnockT = max(o.craftKnockT, 0.85f);
					o.rpm *= 0.88f; o.throttle *= 0.80f;
				}
				const float dmg=100.f/30.f;
				if (cGo && !cRam) { ApplyCraftDamage(c, dmg, 1); c.craftHitCool=0.38f; }
				if (oGo && !oRam) { ApplyCraftDamage(o, dmg, 1); o.craftHitCool=0.38f; }
				SpawnFxBurst(FX_SPARK, (c.x+o.x)*0.5f, (c.y+o.y)*0.5f+0.25f, (c.z+o.z)*0.5f,
					nx, ny, nz, 8, 7.5f, 1.f, 0.88f, 0.45f);
				if (!demo && m_sfxHitCool <= 0.f && (c.isPlayer || o.isPlayer)) {
					Soft3DSfxOneShot(S3SFX_HIT, c.x, c.y, c.z);
					m_sfxHitCool = 0.22f;
				}
			}
		}
	}

	for (int i=0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i];
		if (c.retired || c.finished) continue;
		if (c.hp > 0.f) continue;
		c.hp = 0.f;
		if (demo) {
			c.hp = 62.f;
			AbortAiToLine(c, 14.f);
			continue;
		}
		if (c.isPlayer) {
			RetireCraft(c);
			m_phase = PHASE_FINISH; m_podiumT = 0.f; m_finishSimT = 0.f;
			m_clearBakeText = LL14(L"GAME OVER", L"GAME OVER", L"GAME OVER", L"GAME OVER", L"GAME OVER", L"게임 오버", L"游戏结束", L"انتهت", L"КОНЕЦ", L"GAME OVER", L"FIM DE JOGO", L"GAME OVER", L"KONIEC", L"OYUN BİTTİ");
			m_clearBakeA=1.f; m_clearDirty=1;
			Soft3DSfxUi(S3SFX_GAMEOVER, 0);
			continue;
		}
		RetireCraft(c);
		Soft3DSfxOneShot(S3SFX_HIT, c.x, c.y, c.z);
	}
	TickBlast(dt);
	TickFx(dt);

	if (m_phase == PHASE_RACE || m_phase == PHASE_FINISH) m_raceClock += dt;
	else if (demo) m_raceClock += dt;
	UpdateRanks();
}

void CSoft3DRaceDlg::TickDemo(float dt)
{
	m_demoCamT = S3rNormAngle(m_demoCamT + dt * 0.18f);
	TickPhysics(dt);
	TickItems(dt);
	// デモ表示は薄く維持（スタート誘導）— 文字は一度だけ焼き、αはシェーダ Misc.y
	if (m_clearBakeA < 0.70f) {
		m_clearBakeText = LL14(L"スタートで開始", L"Press Start", L"Démarrer", L"Avvia", L"Iniciar",
			L"시작", L"按开始", L"Start", L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat");
		m_clearBakeA = 0.95f; m_clearDirty = 1;
	} else {
		m_clearBakeA = max(0.70f, m_clearBakeA - dt * 0.08f);
	}
}

void CSoft3DRaceDlg::UpdateStatus()
{
	if (!m_status.GetSafeHwnd()) return;
	CString s;
	S3rCraft& pl=m_crafts[0];
	float kmh = SpeedToKmh(pl.vx, pl.vy, pl.vz);
	if (m_phase == PHASE_DEMO) {
		s.Format(_T("DEMO  Lap %d  Rank %d/%d  %.0f km/h  — Start to race"),
			pl.lap + 1, pl.rank, m_craftN, kmh);
	} else {
		s.Format(_T("Lap %d/%d  Rank %d/%d  HP %.0f  Thrust %.0f  %.0f km/h  Time %.1fs"),
			min(m_lapsTarget, pl.lap+1), m_lapsTarget, pl.rank, m_craftN, pl.hp, pl.fuel, kmh, m_raceClock);
	}
	m_status.SetWindowText(s);
	m_hudDirty = 1;
}

void CSoft3DRaceDlg::EnsureHudBake()
{
	if (!m_view.m_ready) return;
	if (!m_hudDirty && m_view.m_srvHud) return;
	S3rCraft& pl=m_crafts[0];
	wchar_t buf[512];
	if (pl.bestLap<=1e8f) {
		swprintf_s(buf, L"Lap %d/%d   #%d/%d\nHP %.0f   Thrust %.0f\nTime %.2f   BestLap %.2f",
			min(m_lapsTarget, pl.lap+1), m_lapsTarget, pl.rank, m_craftN, pl.hp, pl.fuel, m_raceClock, pl.bestLap);
	} else {
		swprintf_s(buf, L"Lap %d/%d   #%d/%d\nHP %.0f   Thrust %.0f\nTime %.2f   BestLap --",
			min(m_lapsTarget, pl.lap+1), m_lapsTarget, pl.rank, m_craftN, pl.hp, pl.fuel, m_raceClock);
	}
	m_hudBakeText = buf;
	m_view.BakeHudTexture(m_hudBakeText);
	m_hudDirty = 0;
}

void CSoft3DRaceDlg::EnsureGaugeBake()
{
	if (!m_view.m_ready) return;
	S3rCraft& pl = m_crafts[0];
	float kmh = SpeedToKmh(pl.vx, pl.vy, pl.vz);
	int ik = (int)(kmh + 0.5f);
	int ir = (int)(pl.rpm * 90.f + 0.5f);
	if (ik == m_gaugeKmhQ && ir == m_gaugeRpmQ && m_view.m_srvGauge) return;
	m_gaugeKmhQ = ik;
	m_gaugeRpmQ = ir;
	m_view.BakeGaugeTexture(kmh, pl.rpm);
}

void CSoft3DRaceDlg::EnsureStandingsBake()
{
	if (!m_view.m_ready) return;
	// ニトロ発動中はパルス表示のため毎フレ更新
	if (m_crafts[0].nitroT > 0.f) m_standDirty = 1;
	if (!m_standDirty && m_view.m_srvStand) return;
	CS3rView::S3rStandRow rows[S3R_MAX_CRAFT];
	int order[S3R_MAX_CRAFT];
	for (int i = 0; i < m_craftN; i++) order[i] = i;
	for (int i = 0; i < m_craftN; i++) for (int j = i + 1; j < m_craftN; j++)
		if (m_crafts[order[i]].rank > m_crafts[order[j]].rank) { int t = order[i]; order[i] = order[j]; order[j] = t; }
	for (int i = 0; i < m_craftN; i++) {
		S3rCraft& c = m_crafts[order[i]];
		CS3rView::S3rStandRow& r = rows[i];
		memset(&r, 0, sizeof(r));
		wcsncpy_s(r.name, c.name, _TRUNCATE);
		r.rank = c.rank > 0 ? c.rank : (i + 1);
		r.isPlayer = c.isPlayer;
		r.cr = kCraftColors[c.colorIdx][0];
		r.cg = kCraftColors[c.colorIdx][1];
		r.cb = kCraftColors[c.colorIdx][2];
		r.retired = c.retired ? 1 : 0;
		// 2行×2列で最大4枠。上段 L1 L2 / 下段 L3 L4。L5 が来たら L1 が消え L2 L3 / L4 L5
		int nDone = c.lapTimesN;
		int hi = nDone;
		if (!c.finished && !c.retired) {
			hi = nDone + 1;
			if (hi < 1) hi = 1;
		}
		if (hi < 1) hi = 1;
		if (m_lapsTarget > 0 && hi > m_lapsTarget) hi = m_lapsTarget;
		int showN = 4;
		if (m_lapsTarget > 0 && showN > m_lapsTarget) showN = m_lapsTarget;
		int startNo = hi - showN + 1;
		if (startNo < 1) startNo = 1;
		r.lapShowN = showN;
		for (int k = 0; k < 4; k++) {
			if (k < showN) {
				r.lapNo[k] = startNo + k;
				int src = r.lapNo[k] - 1;
				r.lapSec[k] = (src >= 0 && src < nDone) ? c.lapTimes[src] : -1.f;
			} else {
				r.lapNo[k] = 0;
				r.lapSec[k] = -1.f;
			}
		}
	}
	m_view.BakeStandingsTexture(rows, m_craftN,
		m_crafts[0].nitroStock, S3R_NITRO_MAX,
		(m_crafts[0].nitroT > 0.f) ? S3rSaturate(m_crafts[0].nitroT / 2.85f) : 0.f);
	{
		CS3rView::S3rBubbleRow bub[S3R_MAX_CRAFT];
		for (int i = 0; i < m_craftN; i++) {
			S3rCraft& c = m_crafts[i];
			wchar_t rankBuf[24];
			S3rRankWord(c.rank > 0 ? c.rank : (i + 1), rankBuf, _countof(rankBuf));
			swprintf_s(bub[i].text, L"%s %s", rankBuf, c.name);
			bub[i].isPlayer = c.isPlayer;
		}
		m_view.BakeBubbleTexture(bub, m_craftN);
	}
	m_standDirty = 0;
}

void CSoft3DRaceDlg::BakeStaticMeshes()
{
	if (!m_view.m_ready || m_knotN < 4 || !m_view.m_dev) return;
	S3rUnbindIA(m_view.m_imm);
	if (!m_view.m_cpuBakeScratch) {
		for (int t = 0; t < 3 && !m_view.m_cpuBakeScratch; t++) {
			UINT bytes = kS3rBakeVertsTry[t] * (UINT)sizeof(S3RVertex);
			m_view.m_cpuBakeScratch = new (std::nothrow) BYTE[bytes];
			if (m_view.m_cpuBakeScratch) m_view.m_cpuBakeScratchBytes = bytes;
		}
	}
	if (!m_view.m_cpuBakeScratch) return;
	const int keepTerr = m_view.m_vbTerrParts;
	const int keepBand = m_view.m_vbBandParts;
	const int keepWater = m_view.m_vbWaterParts;
	const int keepScenery = m_view.m_vbSceneryParts;
	m_view.m_vbTerrParts = 0;
	m_view.m_vbBandParts = 0;
	m_view.m_vbWaterParts = 0;
	m_view.m_vbSceneryParts = 0;
	const int axis = S3MeshAxisMul();
	const int segs = S3MeshScaleCount(220, 4096);
	const int gN = S3MeshScaleCount(72, 1200);
	const int tSegs = S3MeshScaleCount(220, 4096);
	const int wN = S3MeshScaleCount(28, 512);
	UINT bakeMax = m_view.m_cpuBakeScratchBytes / (UINT)sizeof(S3RVertex);
	bakeMax = (bakeMax / 12u) * 12u;
	if (bakeMax < 12u) {
		m_view.m_vbTerrParts = keepTerr;
		m_view.m_vbBandParts = keepBand;
		m_view.m_vbWaterParts = keepWater;
		m_view.m_vbSceneryParts = keepScenery;
		return;
	}
	S3RVertex* bv = (S3RVertex*)m_view.m_cpuBakeScratch;
	UINT n = 0;
	ID3D11Buffer** curVb = NULL;
	UINT* curNn = NULL;
	int* curParts = NULL;
	UINT primAlign = 3;
	auto flush=[&](){
		if (!curVb || !curNn || !curParts || n < primAlign) { n = 0; return; }
		n -= n % primAlign;
		if (n < primAlign) { n = 0; return; }
		m_view.PushDefaultVBPart(curVb, curNn, *curParts, bv, n);
		n = 0;
	};
	auto beginMesh=[&](ID3D11Buffer** vb, UINT* nn, int* parts, UINT align){
		flush();
		curVb = vb; curNn = nn; curParts = parts; primAlign = align ? align : 3u;
		n = 0;
	};
	auto need=[&](UINT k)->BOOL{
		if (k < 1) return FALSE;
		if (curParts && *curParts >= CS3rView::S3R_MESH_PARTS) return FALSE;
		if (n + k > bakeMax) flush();
		if (curParts && *curParts >= CS3rView::S3R_MESH_PARTS) return FALSE;
		if (n + k > bakeMax) return FALSE;
		return TRUE;
	};
	auto put=[&](float x,float y,float z,float nx,float ny,float nz,float u,float vv,float r,float g,float b,float a)->BOOL{
		if (!need(1)) return FALSE;
		bv[n++] = {x,y,z,nx,ny,nz,u,vv,r,g,b,a};
		return TRUE;
	};
	auto patch=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float nx,float ny,float nz,float u0,float v0,float u1,float v1,float r,float g,float b,float a){
		if (!need(4)) return;
		put(x0,y0,z0,nx,ny,nz,u0,v1,r,g,b,a); put(x1,y1,z1,nx,ny,nz,u1,v1,r,g,b,a);
		put(x2,y2,z2,nx,ny,nz,u1,v0,r,g,b,a); put(x3,y3,z3,nx,ny,nz,u0,v0,r,g,b,a);
	};
	float half = BandHalfWidth();
	beginMesh(m_view.m_vbBand, m_view.m_vbBandN, &m_view.m_vbBandParts, 4);
	for (int i=0;i<segs;i++){
		float t0=(float)i/segs, t1=(float)(i+1)/segs;
		float p0x,p0y,p0z,t0x,t0y,t0z,n0x,n0y,n0z,b0x,b0y,b0z;
		float p1x,p1y,p1z,t1x,t1y,t1z,n1x,n1y,n1z,b1x,b1y,b1z;
		SplineFrame(t0,p0x,p0y,p0z,t0x,t0y,t0z,n0x,n0y,n0z,b0x,b0y,b0z);
		SplineFrame(t1,p1x,p1y,p1z,t1x,t1y,t1z,n1x,n1y,n1z,b1x,b1y,b1z);
		float u0=t0*6.f, u1=t1*6.f;
		float colR=.65f,colG=.9f,colB=1.f,colA=.42f;
		if (m_themeActive==THEME_FOREST){colR=.55f;colG=1.f;colB=.7f;}
		else if (m_themeActive==THEME_MESA){colR=1.f;colG=.75f;colB=.45f;}
		else if (m_themeActive==THEME_NIGHT){colR=.55f;colG=.7f;colB=1.f;}
		patch(p0x-b0x*half,p0y-b0y*half,p0z-b0z*half, p0x+b0x*half,p0y+b0y*half,p0z+b0z*half,
			  p1x+b1x*half,p1y+b1y*half,p1z+b1z*half, p1x-b1x*half,p1y-b1y*half,p1z-b1z*half,
			  n0x,n0y,n0z, u0,0,u1,1, colR,colG,colB,colA);
		float rh=half*1.04f;
		patch(p0x-b0x*rh+n0x*.35f,p0y-b0y*rh+n0y*.35f,p0z-b0z*rh+n0z*.35f,
			  p0x-b0x*half+n0x*.08f,p0y-b0y*half+n0y*.08f,p0z-b0z*half+n0z*.08f,
			  p1x-b1x*half+n1x*.08f,p1y-b1y*half+n1y*.08f,p1z-b1z*half+n1z*.08f,
			  p1x-b1x*rh+n1x*.35f,p1y-b1y*rh+n1y*.35f,p1z-b1z*rh+n1z*.35f,
			  -b0x,-b0y,-b0z, u0,0,u1,1, colR,colG,colB,.28f);
		float rhRec = half * 0.40f;
		float nLift = 0.12f;
		patch(p0x-b0x*rhRec+n0x*nLift,p0y-b0y*rhRec+n0y*nLift,p0z-b0z*rhRec+n0z*nLift,
			  p0x+b0x*rhRec+n0x*nLift,p0y+b0y*rhRec+n0y*nLift,p0z+b0z*rhRec+n0z*nLift,
			  p1x+b1x*rhRec+n1x*nLift,p1y+b1y*rhRec+n1y*nLift,p1z+b1z*rhRec+n1z*nLift,
			  p1x-b1x*rhRec+n1x*nLift,p1y-b1y*rhRec+n1y*nLift,p1z-b1z*rhRec+n1z*nLift,
			  n0x,n0y,n0z, u0,0,u1,1, 0.22f,0.95f,0.52f,0.58f);
	}
	{
		float tA = 0.f, tB = 0.012f;
		float p0x,p0y,p0z,t0x,t0y,t0z,n0x,n0y,n0z,b0x,b0y,b0z;
		float p1x,p1y,p1z,t1x,t1y,t1z,n1x,n1y,n1z,b1x,b1y,b1z;
		SplineFrame(tA,p0x,p0y,p0z,t0x,t0y,t0z,n0x,n0y,n0z,b0x,b0y,b0z);
		SplineFrame(tB,p1x,p1y,p1z,t1x,t1y,t1z,n1x,n1y,n1z,b1x,b1y,b1z);
		const int nChk = S3MeshScaleCount(8, 48);
		const float lift = 0.18f;
		for (int k = 0; k < nChk; k++) {
			float a0 = -1.f + 2.f * (float)k / (float)nChk;
			float a1 = -1.f + 2.f * (float)(k + 1) / (float)nChk;
			int chk = k & 1;
			float cr = chk ? 0.98f : 0.10f, cg = chk ? 0.98f : 0.10f, cb = chk ? 0.94f : 0.12f;
			float h0 = a0 * half, h1 = a1 * half;
			patch(p0x+b0x*h0+n0x*lift,p0y+b0y*h0+n0y*lift,p0z+b0z*h0+n0z*lift,
				  p0x+b0x*h1+n0x*lift,p0y+b0y*h1+n0y*lift,p0z+b0z*h1+n0z*lift,
				  p1x+b1x*h1+n1x*lift,p1y+b1y*h1+n1y*lift,p1z+b1z*h1+n1z*lift,
				  p1x+b1x*h0+n1x*lift,p1y+b1y*h0+n1y*lift,p1z+b1z*h0+n1z*lift,
				  n0x,n0y,n0z, 0,0,1,1, cr,cg,cb,0.90f);
		}
		const int nBan = 6;
		float hLo = 0.35f, hHi = max(3.2f, half * 1.05f);
		for (int k = 0; k < nBan; k++) {
			float a0 = -1.f + 2.f * (float)k / (float)nBan;
			float a1 = -1.f + 2.f * (float)(k + 1) / (float)nBan;
			int chk = k & 1;
			float cr = chk ? 0.95f : 0.82f, cg = chk ? 0.18f : 0.95f, cb = chk ? 0.18f : 0.95f;
			float h0 = a0 * half * 0.98f, h1 = a1 * half * 0.98f;
			patch(p0x+b0x*h0+n0x*hLo,p0y+b0y*h0+n0y*hLo,p0z+b0z*h0+n0z*hLo,
				  p0x+b0x*h1+n0x*hLo,p0y+b0y*h1+n0y*hLo,p0z+b0z*h1+n0z*hLo,
				  p0x+b0x*h1+n0x*hHi,p0y+b0y*h1+n0y*hHi,p0z+b0z*h1+n0z*hHi,
				  p0x+b0x*h0+n0x*hHi,p0y+b0y*h0+n0y*hHi,p0z+b0z*h0+n0z*hHi,
				  t0x,t0y,t0z, 0,0,1,1, cr,cg,cb,0.72f);
		}
	}
	beginMesh(m_view.m_vbTerr, m_view.m_vbTerrN, &m_view.m_vbTerrParts, 3);

	float gr=.35f,gg=.55f,gb=.32f;
	if (m_themeActive==THEME_FOREST){gr=.28f;gg=.62f;gb=.28f;}
	else if (m_themeActive==THEME_RUINS){gr=.55f;gg=.5f;gb=.42f;}
	else if (m_themeActive==THEME_OIL){gr=.28f;gg=.3f;gb=.32f;}
	else if (m_themeActive==THEME_NIGHT){gr=.12f;gg=.14f;gb=.22f;}
	else if (m_themeActive==THEME_UNDER){gr=.15f;gg=.35f;gb=.5f;}
	else if (m_themeActive==THEME_GRASS){gr=.4f;gg=.7f;gb=.3f;}
	else if (m_themeActive==THEME_MESA){gr=.85f;gg=.45f;gb=.28f;}
	else {gr=.75f;gg=.8f;gb=.95f;}
	float extent = m_demoRad + m_bandHalf + 56.f;
	if (extent < 280.f) extent = 280.f;
	if (extent > 480.f) extent = 480.f;
	float step = extent * 2.f / (float)gN;
	float x0 = m_demoMidX - extent, z0 = m_demoMidZ - extent;
	auto sampHm=[&](const float* hm, float x, float z, float fb)->float{
		if (m_hmStep < 1e-4f) return fb;
		float u = (x - m_hmX0) / m_hmStep, v = (z - m_hmZ0) / m_hmStep;
		int i = (int)floorf(u), j = (int)floorf(v);
		float fu = u - (float)i, fv = v - (float)j;
		if (i < 0) { i = 0; fu = 0.f; } if (j < 0) { j = 0; fv = 0.f; }
		if (i >= S3R_HM_N - 1) { i = S3R_HM_N - 2; fu = 1.f; }
		if (j >= S3R_HM_N - 1) { j = S3R_HM_N - 2; fv = 1.f; }
		float h00=hm[j*S3R_HM_N+i], h10=hm[j*S3R_HM_N+i+1];
		float h01=hm[(j+1)*S3R_HM_N+i], h11=hm[(j+1)*S3R_HM_N+i+1];
		return h00+(h10-h00)*fu+((h01+(h11-h01)*fu)-(h00+(h10-h00)*fu))*fv;
	};
	auto rawY=[&](float x,float z)->float{ return sampHm(m_hmRaw, x, z, GroundY(x,z)); };
	auto pathD=[&](float x,float z)->float{ return sampHm(m_hmPathDist, x, z, 1e8f); };
	auto quad=[&](float ax,float ay,float az, float bx,float by,float bz, float cx2,float cy2,float cz2, float dx,float dy,float dz,
		float nx,float ny,float nz, float r,float g,float b){
		if (!need(6)) return;
		put(ax,ay,az,nx,ny,nz,0,0,r,g,b,1.f);
		put(dx,dy,dz,nx,ny,nz,0,1,r,g,b,1.f);
		put(cx2,cy2,cz2,nx,ny,nz,1,1,r,g,b,1.f);
		put(ax,ay,az,nx,ny,nz,0,0,r,g,b,1.f);
		put(cx2,cy2,cz2,nx,ny,nz,1,1,r,g,b,1.f);
		put(bx,by,bz,nx,ny,nz,1,0,r,g,b,1.f);
	};
	auto terrPatch=[&](float xa,float za,float xb,float zb, float u0,float v0,float u1,float v1){
		if (!need(6)) return;
		float y00=GroundY(xa,za), y10=GroundY(xb,za), y11=GroundY(xb,zb), y01=GroundY(xa,zb);
		float r00=rawY(xa,za), r10=rawY(xb,za), r11=rawY(xb,zb), r01=rawY(xa,zb);
		float pd = pathD(xa,za);
		float pd1 = pathD(xb,za); if (pd1 < pd) pd = pd1;
		float pd2 = pathD(xb,zb); if (pd2 < pd) pd = pd2;
		float pd3 = pathD(xa,zb); if (pd3 < pd) pd = pd3;
		float pdC = pathD((xa + xb) * 0.5f, (za + zb) * 0.5f);
		const float tunR = S3rTunR(m_bandHalf) * 1.20f;
		float yMn = y00; if (y10 < yMn) yMn = y10; if (y11 < yMn) yMn = y11; if (y01 < yMn) yMn = y01;
		float yMx = y00; if (y10 > yMx) yMx = y10; if (y11 > yMx) yMx = y11; if (y01 > yMx) yMx = y01;
		const int hole = (pdC < tunR || pd < tunR * 0.96f) ? 1 : 0;
		const int steepLip = ((pdC < tunR * 1.12f || pd < tunR) && (yMx - yMn) > tunR * 0.38f) ? 1 : 0;
		if (!steepLip) {
			float nx=(y00-y10)+(y01-y11), nz=(y00-y01)+(y10-y11), ny=(xb-xa)*2.f; S3rNorm3(nx,ny,nz);
			put(xa,y00,za,nx,ny,nz,u0,v0,gr,gg,gb,1.f);
			put(xa,y01,zb,nx,ny,nz,u0,v1,gr,gg,gb,1.f);
			put(xb,y11,zb,nx,ny,nz,u1,v1,gr,gg,gb,1.f);
			put(xa,y00,za,nx,ny,nz,u0,v0,gr,gg,gb,1.f);
			put(xb,y11,zb,nx,ny,nz,u1,v1,gr,gg,gb,1.f);
			put(xb,y10,za,nx,ny,nz,u1,v0,gr,gg,gb,1.f);
		}
		float cap = (r00-y00);
		if (r10-y10 < cap) cap = r10-y10;
		if (r11-y11 < cap) cap = r11-y11;
		if (r01-y01 < cap) cap = r01-y01;
		if (!hole && cap > 4.2f && pd > tunR * 0.98f && need(6)) {
			float cxn=(r10-r00)+(r11-r01), czn=(r01-r00)+(r11-r10), cyn=(xb-xa)*2.f;
			S3rNorm3(cxn,cyn,czn);
			put(xa,r00,za,cxn,cyn,czn,u0,v0,gr,gg,gb,1.f);
			put(xa,r01,zb,cxn,cyn,czn,u0,v1,gr,gg,gb,1.f);
			put(xb,r11,zb,cxn,cyn,czn,u1,v1,gr,gg,gb,1.f);
			put(xa,r00,za,cxn,cyn,czn,u0,v0,gr,gg,gb,1.f);
			put(xb,r11,zb,cxn,cyn,czn,u1,v1,gr,gg,gb,1.f);
			put(xb,r10,za,cxn,cyn,czn,u1,v0,gr,gg,gb,1.f);
		}
	};
	const float refineR = m_bandHalf * 3.8f;
	n = 0;
	for (int gz=0; gz<gN; gz++) {
		for (int gx=0; gx<gN; gx++) {
			float xa = x0 + gx * step, za = z0 + gz * step;
			float xb = xa + step, zb = za + step;
			float u0=(float)gx/(float)gN, v0=(float)gz/(float)gN, u1=(float)(gx+1)/(float)gN, v1=(float)(gz+1)/(float)gN;
			float dmin = pathD(xa,za);
			float d1 = pathD(xb,za); if (d1 < dmin) dmin = d1;
			float d2 = pathD(xb,zb); if (d2 < dmin) dmin = d2;
			float d3 = pathD(xa,zb); if (d3 < dmin) dmin = d3;
			int sub = 1;
			if (dmin < refineR) {
				float yspan = rawY(xa,za) - GroundY(xa,za);
				if (yspan < 0.f) yspan = -yspan;
				if (axis <= 1)
					sub = (yspan > 5.f) ? 6 : 3;
				else
					sub = (yspan > 5.f) ? 2 : 1;
				if (dmin < S3rTunR(m_bandHalf) * 1.4f)
					sub = max(sub, (axis <= 1) ? 8 : 3);
			}
			float us = (u1 - u0) / (float)sub, vs = (v1 - v0) / (float)sub;
			float xs = (xb - xa) / (float)sub, zs = (zb - za) / (float)sub;
			for (int iz=0; iz<sub; iz++) for (int ix=0; ix<sub; ix++) {
				float xA = xa + xs * (float)ix, zA = za + zs * (float)iz;
				terrPatch(xA, zA, xA + xs, zA + zs, u0 + us * (float)ix, v0 + vs * (float)iz, u0 + us * (float)(ix + 1), v0 + vs * (float)(iz + 1));
			}
		}
	}
	{
		float cr=gr*0.58f, cg=gg*0.54f, cb=gb*0.50f;
		float lipRcol=gr*0.42f, lipGcol=gg*0.40f, lipBcol=gb*0.38f;
		const float baseR = S3rTunR(m_bandHalf);
		const int ringN = S3MeshScaleCount(16, 32);
		auto ringPt=[&](float px,float py,float pz, float bx,float by,float bz, float nx,float ny,float nz,
			float ang, float rad, float& ox, float& oy, float& oz){
			float ca=cosf(ang), sa=sinf(ang);
			ox = px + (bx*ca + nx*sa)*rad;
			oy = py + (by*ca + ny*sa)*rad;
			oz = pz + (bz*ca + nz*sa)*rad;
		};
		for (int i = 0; i < tSegs; i++) {
			float t0 = (float)i / (float)tSegs, t1 = (float)(i + 1) / (float)tSegs;
			int i0 = S3rPathIdx(t0), i1s = S3rPathIdx(t1);
			int deep0 = m_pathDeep[i0] ? 1 : 0;
			int deep1 = m_pathDeep[i1s] ? 1 : 0;
			if (!deep0 && !deep1) continue;
			float p0x,p0y,p0z,t0x,t0y,t0z,n0x,n0y,n0z,b0x,b0y,b0z;
			float p1x,p1y,p1z,t1x,t1y,t1z,n1x,n1y,n1z,b1x,b1y,b1z;
			SplineFrame(t0,p0x,p0y,p0z,t0x,t0y,t0z,n0x,n0y,n0z,b0x,b0y,b0z);
			SplineFrame(t1,p1x,p1y,p1z,t1x,t1y,t1z,n1x,n1y,n1z,b1x,b1y,b1z);
			float flare0 = S3rTunFlare(m_pathDeep, i0);
			float flare1 = S3rTunFlare(m_pathDeep, i1s);
			float rad0 = baseR * (1.f + 0.85f * flare0);
			float rad1 = baseR * (1.f + 0.85f * flare1);
			int portal = (deep0 != deep1 || flare0 > 0.55f || flare1 > 0.55f) ? 1 : 0;
			for (int p = 0; p < ringN; p++) {
				float a0 = (float)p / (float)ringN * (float)(M_PI * 2.0);
				float a1 = (float)(p + 1) / (float)ringN * (float)(M_PI * 2.0);
				float sa = sinf(a0);
				if (!portal && sa < -0.72f) continue;
				float ax,ay,az, bx2,by2,bz2, cx2,cy2,cz2, dx,dy,dz;
				ringPt(p0x,p0y,p0z, b0x,b0y,b0z, n0x,n0y,n0z, a0, rad0, ax,ay,az);
				ringPt(p1x,p1y,p1z, b1x,b1y,b1z, n1x,n1y,n1z, a0, rad1, bx2,by2,bz2);
				ringPt(p1x,p1y,p1z, b1x,b1y,b1z, n1x,n1y,n1z, a1, rad1, cx2,cy2,cz2);
				ringPt(p0x,p0y,p0z, b0x,b0y,b0z, n0x,n0y,n0z, a1, rad0, dx,dy,dz);
				float ca=cosf(a0), ss=sinf(a0);
				float wnx=-(b0x*ca + n0x*ss), wny=-(b0y*ca + n0y*ss), wnz=-(b0z*ca + n0z*ss);
				S3rNorm3(wnx,wny,wnz);
				quad(ax,ay,az, bx2,by2,bz2, cx2,cy2,cz2, dx,dy,dz, wnx,wny,wnz, cr,cg,cb);
			}
			if (portal) {
				float lip0 = rad0 * 1.16f, lip1 = rad1 * 1.16f;
				for (int p = 0; p < ringN; p++) {
					float a0 = (float)p / (float)ringN * (float)(M_PI * 2.0);
					float a1 = (float)(p + 1) / (float)ringN * (float)(M_PI * 2.0);
					float ax,ay,az, bx2,by2,bz2, cx2,cy2,cz2, dx,dy,dz;
					ringPt(p0x,p0y,p0z, b0x,b0y,b0z, n0x,n0y,n0z, a0, rad0, ax,ay,az);
					ringPt(p0x,p0y,p0z, b0x,b0y,b0z, n0x,n0y,n0z, a0, lip0, bx2,by2,bz2);
					ringPt(p0x,p0y,p0z, b0x,b0y,b0z, n0x,n0y,n0z, a1, lip0, cx2,cy2,cz2);
					ringPt(p0x,p0y,p0z, b0x,b0y,b0z, n0x,n0y,n0z, a1, rad0, dx,dy,dz);
					float wnx=t0x, wny=t0y, wnz=t0z;
					S3rNorm3(wnx,wny,wnz);
					quad(ax,ay,az, bx2,by2,bz2, cx2,cy2,cz2, dx,dy,dz, wnx,wny,wnz, lipRcol,lipGcol,lipBcol);
					ringPt(p1x,p1y,p1z, b1x,b1y,b1z, n1x,n1y,n1z, a0, rad1, ax,ay,az);
					ringPt(p1x,p1y,p1z, b1x,b1y,b1z, n1x,n1y,n1z, a0, lip1, bx2,by2,bz2);
					ringPt(p1x,p1y,p1z, b1x,b1y,b1z, n1x,n1y,n1z, a1, lip1, cx2,cy2,cz2);
					ringPt(p1x,p1y,p1z, b1x,b1y,b1z, n1x,n1y,n1z, a1, rad1, dx,dy,dz);
					wnx=t1x; wny=t1y; wnz=t1z;
					S3rNorm3(wnx,wny,wnz);
					quad(ax,ay,az, bx2,by2,bz2, cx2,cy2,cz2, dx,dy,dz, wnx,wny,wnz, lipRcol,lipGcol,lipBcol);
				}
			}
		}
	}
	beginMesh(m_view.m_vbScenery, m_view.m_vbSceneryN, &m_view.m_vbSceneryParts, 3);

	n = 0;
	{
		float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
		SplineFrame(0.f, px,py,pz, tx,ty,tz, nx,ny,nz, bx,by,bz);
		float postH = half * 1.20f; if (postH < 3.4f) postH = 3.4f;
		auto gateBox = [&](float x, float y0, float z, float hx, float hy, float hz, float r, float g, float b) {
			float y1 = y0 + hy;
			float x0 = x - hx, x1 = x + hx, z0 = z - hz, z1 = z + hz;
			auto tri = [&](float ax,float ay,float az, float bx2,float by2,float bz2, float cx2,float cy2,float cz2,
				float nx2,float ny2,float nz2) {
				if (!need(3)) return;
				put(ax,ay,az,nx2,ny2,nz2,0,0,r,g,b,1.f);
				put(bx2,by2,bz2,nx2,ny2,nz2,1,0,r,g,b,1.f);
				put(cx2,cy2,cz2,nx2,ny2,nz2,.5f,1,r,g,b,1.f);
			};
			tri(x0,y1,z0, x1,y1,z0, x1,y1,z1, 0,1,0);
			tri(x0,y1,z0, x1,y1,z1, x0,y1,z1, 0,1,0);
			tri(x0,y0,z1, x1,y0,z1, x1,y1,z1, 0,0,1);
			tri(x0,y0,z1, x1,y1,z1, x0,y1,z1, 0,0,1);
			tri(x1,y0,z0, x0,y0,z0, x0,y1,z0, 0,0,-1);
			tri(x1,y0,z0, x0,y1,z0, x1,y1,z0, 0,0,-1);
			tri(x0,y0,z0, x0,y0,z1, x0,y1,z1, -1,0,0);
			tri(x0,y0,z0, x0,y1,z1, x0,y1,z0, -1,0,0);
			tri(x1,y0,z1, x1,y0,z0, x1,y1,z0, 1,0,0);
			tri(x1,y0,z1, x1,y1,z0, x1,y1,z1, 1,0,0);
		};
		float lx = px - bx * half, ly = py - by * half, lz = pz - bz * half;
		float rx = px + bx * half, ry = py + by * half, rz = pz + bz * half;
		gateBox(lx, ly, lz, 0.22f, postH, 0.22f, 0.96f, 0.96f, 0.98f);
		gateBox(rx, ry, rz, 0.22f, postH, 0.22f, 0.96f, 0.96f, 0.98f);
		float mx = (lx + rx) * 0.5f, mz = (lz + rz) * 0.5f;
		float my = (ly + ry) * 0.5f + postH - 0.18f;
		float span = half + 0.28f;
		gateBox(mx, my, mz, span, 0.16f, 0.16f, 0.88f, 0.16f, 0.18f);
	}
	beginMesh(m_view.m_vbWater, m_view.m_vbWaterN, &m_view.m_vbWaterParts, 3);
	{
		float wExt = m_demoRad + m_bandHalf + 40.f;
		if (wExt < 200.f) wExt = 200.f;
		if (wExt > 420.f) wExt = 420.f;
		float wStep = wExt * 2.f / (float)wN;
		float wx0 = m_demoMidX - wExt, wz0 = m_demoMidZ - wExt;
		float wr=.22f,wg=.42f,wb=.58f,wa=.42f;
		if (m_themeActive==THEME_FOREST){wr=.16f;wg=.38f;wb=.32f;}
		else if (m_themeActive==THEME_MESA){wr=.35f;wg=.28f;wb=.18f;wa=.38f;}
		else if (m_themeActive==THEME_UNDER){wr=.12f;wg=.40f;wb=.62f;wa=.35f;}
		else if (m_themeActive==THEME_NIGHT){wr=.10f;wg=.16f;wb=.28f;}
		float wy = m_waterY + 0.18f;
		for (int gz=0; gz<wN; gz++) for (int gx=0; gx<wN; gx++) {
			float xa = wx0 + gx * wStep, za = wz0 + gz * wStep;
			float xb = xa + wStep, zb = za + wStep;
			float y00=GroundY(xa,za), y10=GroundY(xb,za), y11=GroundY(xb,zb), y01=GroundY(xa,zb);
			if (y00>wy+0.9f || y10>wy+0.9f || y11>wy+0.9f || y01>wy+0.9f) continue;
			{
				float mx = (xa + xb) * 0.5f, mz = (za + zb) * 0.5f;
				float pd = pathD(mx, mz);
				float pd0 = pathD(xa, za); if (pd0 < pd) pd = pd0;
				float pd1 = pathD(xb, za); if (pd1 < pd) pd = pd1;
				float pd2 = pathD(xb, zb); if (pd2 < pd) pd = pd2;
				float pd3 = pathD(xa, zb); if (pd3 < pd) pd = pd3;
				if (pd < m_bandHalf * 1.35f) continue;
			}
			if (!need(6)) continue;
			put(xa,wy,za,0,1,0,0,0,wr,wg,wb,wa);
			put(xb,wy,za,0,1,0,1,0,wr,wg,wb,wa);
			put(xb,wy,zb,0,1,0,1,1,wr,wg,wb,wa);
			put(xa,wy,za,0,1,0,0,0,wr,wg,wb,wa);
			put(xb,wy,zb,0,1,0,1,1,wr,wg,wb,wa);
			put(xa,wy,zb,0,1,0,0,1,wr,wg,wb,wa);
		}
	}
	flush();

	if (m_view.m_vbTerrParts < 1) {
		m_view.m_vbTerrParts = keepTerr;
		m_view.m_vbBandParts = keepBand;
		m_view.m_vbWaterParts = keepWater;
		m_view.m_vbSceneryParts = keepScenery;
	} else {
		if (m_view.m_vbBandParts < 1) m_view.m_vbBandParts = keepBand;
		if (m_view.m_vbSceneryParts < 1) m_view.m_vbSceneryParts = keepScenery;
	}

	if (m_obsNv > 0 && m_obsNi >= 3)
		m_view.UploadDefaultVB(&m_view.m_vbObs, &m_view.m_obsNvGpu, m_obsVert, (UINT)m_obsNv);
	if (m_obsNi >= 3)
		m_view.UploadDefaultIB(&m_view.m_ibObs, &m_view.m_obsNiGpu, m_obsIdx, (UINT)m_obsNi);
	m_view.m_obsInstN = 0;
	if (m_obsN > 0 && m_view.m_dev) {
		S3RInst inst[S3R_MAX_OBS];
		memset(inst, 0, sizeof(inst));
		int nInst = m_obsN;
		if (nInst > S3R_MAX_OBS) nInst = S3R_MAX_OBS;
		for (int i = 0; i < nInst; i++) {
			S3rObs& o = m_obs[i];
			inst[i] = {o.x, o.y, o.z, o.yaw, o.sx, o.sy, o.sz, 0.f, 1.f, 1.f, 1.f, 1.f};
		}
		UINT ibytes = (UINT)nInst * (UINT)sizeof(S3RInst);
		if (m_view.m_vbObsInst && m_view.m_imm) {
			D3D11_BUFFER_DESC old = {};
			m_view.m_vbObsInst->GetDesc(&old);
			if (old.ByteWidth >= ibytes) {
				S3rUnbindIA(m_view.m_imm);
				D3D11_BOX box = {};
				box.right = ibytes;
				box.bottom = 1;
				box.back = 1;
				m_view.m_imm->UpdateSubresource(m_view.m_vbObsInst, 0, &box, inst, ibytes, 0);
				m_view.m_obsInstN = (UINT)nInst;
			}
		}
		if (m_view.m_obsInstN < 1) {
			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = (UINT)S3R_MAX_OBS * (UINT)sizeof(S3RInst);
			if (bd.ByteWidth < ibytes) bd.ByteWidth = ibytes;
			bd.Usage = D3D11_USAGE_DEFAULT;
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA srd = {};
			srd.pSysMem = inst;
			ID3D11Buffer* neu = NULL;
			if (SUCCEEDED(m_view.m_dev->CreateBuffer(&bd, &srd, &neu)) && neu) {
				S3rUnbindIA(m_view.m_imm);
				S3R_RELEASE(m_view.m_vbObsInst);
				m_view.m_vbObsInst = neu;
				m_view.m_obsInstN = (UINT)nInst;
			}
		}
	}
	if (m_craftNv > 0 && m_craftNi >= 3) {
		m_view.UploadDefaultVB(&m_view.m_vbCraft, &m_view.m_craftNvGpu, m_craftVert, (UINT)m_craftNv);
		m_view.UploadDefaultIB(&m_view.m_ibCraft, &m_view.m_craftNiGpu, m_craftIdx, (UINT)m_craftNi);
	}
}

void CSoft3DRaceDlg::RenderScene()
{
	if (!m_view.m_ready || m_knotN < 4) return;
	if (m_view.m_vbTerrParts < 1 && m_hmReady) BakeStaticMeshes();
	ID3D11DeviceContext* dc = m_view.m_imm;
	const int w = m_view.m_vw, h = m_view.m_vh; if (w < 8 || h < 8) return;
	if (m_clearDirty) {
		if (!m_clearBakeText.IsEmpty()) m_view.BakeClearTexture(m_clearBakeText, m_clearBakeA);
		else m_view.ReleaseClearTexture();
		m_clearDirty = 0;
	}
	EnsureHudBake();
	EnsureGaugeBake();
	EnsureStandingsBake();

	S3rCraft& pl = m_crafts[0];
	float px,py,pz,tx,ty,tz,nx,ny,nz,bx,by,bz;
	SplineFrame(pl.pathT, px,py,pz, tx,ty,tz, nx,ny,nz, bx,by,bz);
	float noseX=sinf(pl.yaw)*cosf(pl.pitch), noseY=sinf(pl.pitch), noseZ=cosf(pl.yaw)*cosf(pl.pitch);
	float back = m_lookback ? -1.f : 1.f;
	float camDist = 12.5f * m_camZoom;
	float camH = (5.8f + m_camPitchOff * 10.f) * m_camZoom;
	float yaw = pl.yaw + m_camYawOff + (m_lookback ? (float)M_PI : 0.f);
	float climb = S3rClamp(ty, -1.f, 1.f);
	if (!m_lookback && m_phase != PHASE_DEMO && m_phase != PHASE_PODIUM) {
		float kUp = S3rSaturate((climb - 0.10f) / 0.42f);
		float kDn = S3rSaturate((-climb - 0.10f) / 0.42f);
		camH = S3rLerp(camH, (1.7f + m_camPitchOff * 3.4f) * m_camZoom, kUp);
		camH = S3rLerp(camH, (7.4f + m_camPitchOff * 10.f) * m_camZoom, kDn * 0.45f);
	}
	float cx = pl.x - sinf(yaw)*camDist*back;
	float cy = pl.y + camH;
	float cz = pl.z - cosf(yaw)*camDist*back;
	float ax = pl.x + noseX * 9.0f * back;
	float ay = pl.y + noseY * 0.55f * back + 1.4f;
	float az = pl.z + noseZ * 9.0f * back;
	if (!m_lookback && m_phase != PHASE_DEMO && m_phase != PHASE_PODIUM) {
		float kUp = S3rSaturate((climb - 0.10f) / 0.42f);
		ay += kUp * 5.4f;
	}
	float tunAmt = 0.f;
	if (m_phase != PHASE_DEMO && m_phase != PHASE_PODIUM && !m_lookback) {
		int pi = S3rPathIdx(pl.pathT);
		if (m_pathDeep[pi]) tunAmt = 1.f;
		else {
			float gyP = GroundY(pl.x, pl.z);
			float ryP = RawGroundY(pl.x, pl.z);
			if (ryP > gyP + 3.5f && pl.y + 2.5f < ryP)
				tunAmt = S3rSaturate((ryP - gyP - 3.5f) / 8.f);
			const int tunLook = 48;
			for (int d = 1; d <= tunLook && tunAmt < 0.999f; d++) {
				int ip = pi + d; if (ip >= S3R_PATH_SAMPLES) ip -= S3R_PATH_SAMPLES;
				int im = pi - d; if (im < 0) im += S3R_PATH_SAMPLES;
				if (m_pathDeep[ip] || m_pathDeep[im]) {
					float u = 1.f - (float)d / (float)tunLook;
					float a = u * u * (3.f - 2.f * u);
					if (a > tunAmt) tunAmt = a;
					break;
				}
			}
		}
	}
	float camDt = m_frameDt;
	if (camDt < 0.0008f) camDt = 1.f / 60.f;
	if (camDt > 0.05f) camDt = 0.05f;
	{
		float tunTau = (tunAmt >= m_camTunnelT) ? 1.25f : 0.90f;
		if (m_lookback) tunTau = 0.22f;
		m_camTunnelT = S3rLerp(m_camTunnelT, tunAmt, S3rDtK(camDt, tunTau));
	}
	const float tunK = m_lookback ? 0.f : m_camTunnelT;
	if (tunK > 0.001f && m_phase != PHASE_DEMO && m_phase != PHASE_PODIUM) {
		// 機体の後ろに寄せるだけ。スプライン接線へ視線を向けない（自動視点で進行方向が変わって見えないように）
		float tunDist = 6.15f * m_camZoom;
		float tunH = 1.42f * m_camZoom;
		float tcx = pl.x - sinf(yaw) * tunDist * back;
		float tcy = pl.y + tunH;
		float tcz = pl.z - cosf(yaw) * tunDist * back;
		cx = S3rLerp(cx, tcx, tunK);
		cy = S3rLerp(cy, tcy, tunK);
		cz = S3rLerp(cz, tcz, tunK);
	}
	if (tunK < 0.35f) {
		float gyc = GroundY(cx, cz) + 2.4f;
		if (cy < gyc) cy = gyc;
	}
	if (m_phase == PHASE_PODIUM) {
		// 表彰台正面からの遅延追従カメラ
		cx = m_podiumBaseX; cy = m_podiumBaseY + 7.5f; cz = m_podiumBaseZ + 16.f;
		ax = m_podiumBaseX; ay = m_podiumBaseY + 3.2f; az = m_podiumBaseZ;
		m_camSmoothInit = 0; // 次レースでリセット
	} else if (m_phase == PHASE_DEMO) {
		// コース全体を見渡すオービット＋パック方面へ少し視線
		float packX = 0.f, packY = 0.f, packZ = 0.f; int pn = 0;
		for (int i = 0; i < m_craftN; i++) if (m_crafts[i].alive) {
			packX += m_crafts[i].x; packY += m_crafts[i].y; packZ += m_crafts[i].z; pn++;
		}
		if (pn > 0) { packX /= (float)pn; packY /= (float)pn; packZ /= (float)pn; }
		else { packX = m_demoMidX; packY = m_demoMidY; packZ = m_demoMidZ; }
		float rad = m_demoRad * 1.12f + 36.f;
		float elev = m_demoMidY + m_demoRad * 0.42f + 22.f + m_demoCamElev;
		cx = m_demoMidX + cosf(m_demoCamT) * rad;
		cy = elev;
		cz = m_demoMidZ + sinf(m_demoCamT) * rad;
		ax = S3rLerp(m_demoMidX, packX, 0.55f);
		ay = S3rLerp(m_demoMidY + 3.5f, packY + 1.2f, 0.55f);
		az = S3rLerp(m_demoMidZ, packZ, 0.55f);
	}
	if (!m_camSmoothInit) {
		m_camSx = cx; m_camSy = cy; m_camSz = cz;
		m_camAx = ax; m_camAy = ay; m_camAz = az;
		m_camSmoothInit = 1;
	} else {
		float posTau = S3rLerp(0.10f, 0.28f, m_lookback ? 0.f : m_camTunnelT);
		float hTau = S3rLerp(0.15f, 0.38f, m_lookback ? 0.f : m_camTunnelT);
		float lookTau = S3rLerp(0.11f, 0.30f, m_lookback ? 0.f : m_camTunnelT);
		if (m_lookback) { posTau = 0.038f; hTau = 0.045f; lookTau = 0.040f; }
		m_camSx = S3rLerp(m_camSx, cx, S3rDtK(camDt, posTau));
		m_camSy = S3rLerp(m_camSy, cy, S3rDtK(camDt, hTau));
		m_camSz = S3rLerp(m_camSz, cz, S3rDtK(camDt, posTau));
		m_camAx = S3rLerp(m_camAx, ax, S3rDtK(camDt, lookTau));
		m_camAy = S3rLerp(m_camAy, ay, S3rDtK(camDt, lookTau * 1.25f));
		m_camAz = S3rLerp(m_camAz, az, S3rDtK(camDt, lookTau));
	}
	cx = m_camSx; cy = m_camSy; cz = m_camSz;
	ax = m_camAx; ay = m_camAy; az = m_camAz;
	if (m_phase != PHASE_PODIUM && m_phase != PHASE_DEMO) {
		if (!m_lookback && m_camTunnelT > 0.22f) {
			float tunR = S3rTunR(m_bandHalf) * 0.80f;
			float gyc = GroundY(cx, cz) + 1.12f;
			if (cy < gyc) cy = gyc;
			float cap = py + tunR;
			float cap2 = pl.y + tunR * 0.68f;
			if (cap2 < cap) cap = cap2;
			if (cy > cap) cy = cap;
			float ddx = cx - px, ddy = cy - py, ddz = cz - pz;
			float lat = ddx * bx + ddy * by + ddz * bz;
			float vert = ddx * nx + ddy * ny + ddz * nz;
			float along = ddx * tx + ddy * ty + ddz * tz;
			float rad = sqrtf(lat * lat + vert * vert);
			if (rad > tunR && rad > 1e-4f) {
				float s = tunR / rad;
				cx = px + tx * along + bx * lat * s + nx * vert * s;
				cy = py + ty * along + by * lat * s + ny * vert * s;
				cz = pz + tz * along + bz * lat * s + nz * vert * s;
			}
		} else {
			float gyc = GroundY(cx, cz) + 2.2f;
			if (cy < gyc) cy = gyc;
		}
	}

	const float fov = ((m_phase == PHASE_DEMO) ? 68.f : 58.f) / m_camZoom * (float)(M_PI/180.0);
	const float zNear = 0.2f, zFar = 700.f;
	S3RFrameCB cb = {};
	cb.viewProj = S3rMatMul(S3rLookAt(cx,cy,cz, ax,ay,az, 0,1,0), S3rPerspective(fov, (float)w/(float)h, zNear, zFar));
	float lx=.45f, ly=1.f, lz=.2f; S3rNorm3(lx,ly,lz);
	cb.lightDir = {lx,ly,lz, 0.f};
	cb.lightVP = S3rMatMul(S3rLookAt(pl.x+lx*55.f, pl.y+ly*55.f, pl.z+lz*55.f, pl.x,pl.y,pl.z, 0,1,0), S3rOrtho(-48.f,48.f,-48.f,48.f, 4.f, 160.f));
	cb.eyePos = {cx,cy,cz, (float)(m_themeActive-1)};
	float fogNear=70.f, fogFar=320.f;
	if (m_themeActive==THEME_UNDER||m_themeActive==THEME_NIGHT){ fogNear=40.f; fogFar=200.f; }
	fogNear *= (1.f - 0.4f * m_reverbFogBoost - 0.25f * (pl.fogT>0?1.f:0.f));
	fogFar *= (1.f - 0.35f * m_reverbFogBoost);
	cb.fogParams = {fogNear, fogFar, m_waterY, 0.f};
	float dofStart=38.f, dofRise=55.f, dofAmp=1.6f + 2.5f*m_eqDofBoost + (pl.dofT>0?2.2f:0.f);
	const BOOL hq = (m_phase == PHASE_RACE || m_phase == PHASE_COUNTDOWN || m_phase == PHASE_FINISH);
	if (!hq) { dofStart = 80.f; dofRise = 40.f; dofAmp = 0.6f; } // デモ俯瞰はDOF弱め
	cb.dofParams = {dofStart, dofRise, dofAmp, hq ? 1.f : 0.f};
	cb.screenSize = {(float)w,(float)h,1.f/w,1.f/h};
	{
		float flash = pl.flashT > 0.f ? 0.55f : 0.f;
		if (pl.nitroT > 0.f) {
			float np = S3rSaturate(pl.nitroT / 2.85f);
			float pulse = 0.22f + 0.18f * (0.5f + 0.5f * sinf(m_anim * 28.f));
			flash = max(flash, pulse * (0.55f + 0.45f * np));
		}
		if (pl.hitFlashT > 0.f)
			flash = max(flash, S3rSaturate(pl.hitFlashT / 0.38f) * 0.48f);
		if (m_wrongWay)
			flash = max(flash, 0.18f + 0.12f * (0.5f + 0.5f * sinf(m_anim * 14.f)));
		cb.misc = {flash, m_clearBakeA, 1.f/tanf(fov*.5f), m_anim};
	}
	{
		float pr = m_bandHalf * 1.85f;
		if (pr < 10.f) pr = 10.f;
		if (pr > 22.f) pr = 22.f;
		// 俯瞰デモで peel すると視線方向の円柱が画面中央の○に見える
		if (m_phase == PHASE_PODIUM || m_phase == PHASE_DEMO) pr = 0.f;
		pr *= (1.f - 0.90f * (m_lookback ? 0.f : m_camTunnelT));
		cb.peel = {pl.x, pl.y, pl.z, pr};
	}
	{
		// 平面反射: メインと同じ FOV/アスペクト。水平面なので up を反転（迷路の鏡床と同じ）
		S3RMat id = {}; id.m[0]=id.m[5]=id.m[10]=id.m[15]=1.f;
		cb.reflectVP = id;
		const float wy = m_waterY;
		float rey = 2.f * wy - cy, ray = 2.f * wy - ay;
		cb.reflectVP = S3rMatMul(S3rLookAt(cx, rey, cz, ax, ray, az, 0.f, -1.f, 0.f),
			S3rPerspective(fov, (float)w / (float)h, zNear, zFar));
	}

	D3D11_MAPPED_SUBRESOURCE map={};
	if (FAILED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))) return;
	memcpy(map.pData,&cb,sizeof(cb)); dc->Unmap(m_view.m_cbFrame,0);

	UINT maxV = m_view.m_vbDynBytes / sizeof(S3RVertex);
	if (maxV < 65536u) maxV = 65536u;
	{
		const UINT need = maxV * sizeof(S3RVertex);
		if (m_view.m_cpuDynScratchBytes < need) {
			delete[] m_view.m_cpuDynScratch;
			m_view.m_cpuDynScratch = new (std::nothrow) BYTE[need];
			m_view.m_cpuDynScratchBytes = m_view.m_cpuDynScratch ? need : 0;
		}
	}
	S3RVertex* v = m_view.m_cpuDynScratch ? (S3RVertex*)m_view.m_cpuDynScratch : NULL;
	if (!v) return;
	UINT nBand=0, nWorld=0, nCraft=0, nTrans=0; int phase=0;
	UINT nTerr=0;
	auto put=[&](float x,float y,float z,float nx,float ny,float nz,float u,float vv,float r,float g,float b,float a)->BOOL{
		UINT n=nBand+nWorld+nCraft+nTrans; if(n>=maxV) return FALSE;
		v[n]={x,y,z,nx,ny,nz,u,vv,r,g,b,a};
		if(phase==3)nTrans++; else if(phase==2)nCraft++; else if(phase==1)nWorld++; else nBand++;
		return TRUE;
	};
	auto patch=[&](float x0,float y0,float z0,float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float nx,float ny,float nz,float u0,float v0,float u1,float v1,float r,float g,float b,float a){
		put(x0,y0,z0,nx,ny,nz,u0,v1,r,g,b,a); put(x1,y1,z1,nx,ny,nz,u1,v1,r,g,b,a);
		put(x2,y2,z2,nx,ny,nz,u1,v0,r,g,b,a); put(x3,y3,z3,nx,ny,nz,u0,v0,r,g,b,a);
	};

	// 帯・地形・水・ゲート・障害は BakeStaticMeshes。ここは動くものだけ。
	nTerr = 0;
	phase=1;
	// 表彰台（1位中央・2位左・3位右）— ワールドは TRIANGLELIST なので 6頂点/面
	if (m_phase == PHASE_PODIUM) {
		auto tri = [&](float ax, float ay, float az, float bx, float by, float bz, float cx2, float cy2, float cz2,
			float nx, float ny, float nz, float r, float g, float b) {
			put(ax, ay, az, nx, ny, nz, 0, 0, r, g, b, 1);
			put(bx, by, bz, nx, ny, nz, 1, 0, r, g, b, 1);
			put(cx2, cy2, cz2, nx, ny, nz, .5f, 1, r, g, b, 1);
		};
		auto box = [&](float x, float y0, float z, float hx, float hy, float hz, float r, float g, float b) {
			float y1 = y0 + hy;
			float x0 = x - hx, x1 = x + hx, z0 = z - hz, z1 = z + hz;
			// top
			tri(x0, y1, z0, x1, y1, z0, x1, y1, z1, 0, 1, 0, r, g, b);
			tri(x0, y1, z0, x1, y1, z1, x0, y1, z1, 0, 1, 0, r, g, b);
			// +Z / -Z / -X / +X
			tri(x0, y0, z1, x1, y0, z1, x1, y1, z1, 0, 0, 1, r * .85f, g * .85f, b * .85f);
			tri(x0, y0, z1, x1, y1, z1, x0, y1, z1, 0, 0, 1, r * .85f, g * .85f, b * .85f);
			tri(x1, y0, z0, x0, y0, z0, x0, y1, z0, 0, 0, -1, r * .75f, g * .75f, b * .75f);
			tri(x1, y0, z0, x0, y1, z0, x1, y1, z0, 0, 0, -1, r * .75f, g * .75f, b * .75f);
			tri(x0, y0, z0, x0, y0, z1, x0, y1, z1, -1, 0, 0, r * .7f, g * .7f, b * .7f);
			tri(x0, y0, z0, x0, y1, z1, x0, y1, z0, -1, 0, 0, r * .7f, g * .7f, b * .7f);
			tri(x1, y0, z1, x1, y0, z0, x1, y1, z0, 1, 0, 0, r * .9f, g * .9f, b * .9f);
			tri(x1, y0, z1, x1, y1, z0, x1, y1, z1, 1, 0, 0, r * .9f, g * .9f, b * .9f);
		};
		phase = 1;
		float y0 = m_podiumBaseY;
		box(m_podiumBaseX,      y0, m_podiumBaseZ, 1.6f, 4.2f, 1.6f, 1.f, .85f, .25f);   // 1st gold
		box(m_podiumBaseX-5.2f, y0, m_podiumBaseZ, 1.5f, 2.8f, 1.5f, .72f, .76f, .82f); // 2nd silver
		box(m_podiumBaseX+5.2f, y0, m_podiumBaseZ, 1.5f, 1.8f, 1.5f, .78f, .48f, .28f); // 3rd bronze
	}
	// sky cards + items（深度書き込みなし）
	phase=3;
	UINT nSky=0, nSky2=0, nSky3=0, nSky4=0, nItem=0, nShaft=0;
	{
		float rx = ax - cx, ry = ay - cy, rz = az - cz; S3rNorm3(rx,ry,rz);
		float ux=0,uy=1,uz=0;
		float sx=uy*rz-uz*ry, sy=uz*rx-ux*rz, sz=ux*ry-uy*rx; S3rNorm3(sx,sy,sz);
		float ux2=ry*sz-rz*sy, uy2=rz*sx-rx*sz, uz2=rx*sy-ry*sx;
		float br=.78f,bg=.86f,bb=1.f;
		if (m_themeActive==THEME_NIGHT){br=.2f;bg=.24f;bb=.42f;}
		else if (m_themeActive==THEME_MESA){br=1.f;bg=.7f;bb=.45f;}
		else if (m_themeActive==THEME_FOREST){br=.58f;bg=.86f;bb=.68f;}
		else if (m_themeActive==THEME_CLOUD){br=.96f;bg=.96f;bb=1.f;}
		else if (m_themeActive==THEME_UNDER){br=.28f;bg=.58f;bb=.78f;}
		else if (m_themeActive==THEME_OIL){br=.48f;bg=.5f;bb=.54f;}
		auto card=[&](float bx0,float by0,float bz0,float hs,float vs,float r,float g,float b,float a){
			float x0=bx0-sx*hs-ux2*vs, y0=by0-sy*hs-uy2*vs, z0=bz0-sz*hs-uz2*vs;
			float x1=bx0+sx*hs-ux2*vs, y1=by0+sy*hs-uy2*vs, z1=bz0+sz*hs-uz2*vs;
			float x2=bx0+sx*hs+ux2*vs, y2=by0+sy*hs+uy2*vs, z2=bz0+sz*hs+uz2*vs;
			float x3=bx0-sx*hs+ux2*vs, y3=by0-sy*hs+uy2*vs, z3=bz0-sz*hs+uz2*vs;
			put(x0,y0,z0, -rx,-ry,-rz, 0,0, r,g,b,a); put(x1,y1,z1, -rx,-ry,-rz, 1,0, r,g,b,a); put(x2,y2,z2, -rx,-ry,-rz, 1,1, r,g,b,a);
			put(x0,y0,z0, -rx,-ry,-rz, 0,0, r,g,b,a); put(x2,y2,z2, -rx,-ry,-rz, 1,1, r,g,b,a); put(x3,y3,z3, -rx,-ry,-rz, 0,1, r,g,b,a);
		};
		const BOOL liteSky = (m_phase == PHASE_DEMO || m_phase == PHASE_PODIUM);
		const int nCard1 = liteSky ? S3MeshScaleCount(4, 6) : S3MeshScaleCount(10, 12);
		const int nCard2 = liteSky ? S3MeshScaleCount(3, 4) : S3MeshScaleCount(8, 10);
		const int nCard3 = liteSky ? S3MeshScaleCount(2, 3) : S3MeshScaleCount(6, 8);
		const int nCard4 = liteSky ? 0 : S3MeshScaleCount(6, 8);
		for (int bi=0;bi<nCard1;bi++){
			float ang = (float)bi * (float)(M_PI*2.0/(double)nCard1) + m_anim*0.018f;
			float dist = 170.f + 50.f*(bi%4);
			float bx0 = pl.x + cosf(ang)*dist;
			float by0 = pl.y + 38.f + 14.f*sinf(ang*1.3f+m_anim*0.05f);
			float bz0 = pl.z + sinf(ang)*dist;
			float hs = 38.f + 22.f*(float)(bi%5);
			float vs = 14.f + 10.f*(float)((bi*3)%4);
			card(bx0,by0,bz0, hs, vs, br,bg,bb,.90f);
		}
		nSky = nTrans;
		for (int bi=0;bi<nCard2;bi++){
			float ang = (float)bi * (float)(M_PI*2.0/(double)nCard2) - m_anim*0.012f + 0.4f;
			float dist = 130.f + 36.f*(bi%3);
			float bx0 = pl.x + cosf(ang)*dist;
			float by0 = pl.y + 28.f + 10.f*sinf(ang*1.7f+m_anim*0.08f);
			float bz0 = pl.z + sinf(ang)*dist;
			float hs = 48.f + 18.f*(float)(bi%3);
			float vs = 9.f + 6.f*(float)(bi%4);
			card(bx0,by0,bz0, hs, vs, br,bg,bb,.82f);
		}
		nSky2 = nTrans - nSky;
		for (int bi=0;bi<nCard3;bi++){
			float ang = (float)bi * (float)(M_PI*2.0/(double)nCard3) + m_anim*0.009f + 1.1f;
			float dist = 200.f + 40.f*(bi%3);
			float bx0 = pl.x + cosf(ang)*dist;
			float by0 = pl.y + 48.f + 12.f*sinf(ang*0.9f+m_anim*0.04f);
			float bz0 = pl.z + sinf(ang)*dist;
			float hs = 52.f + 16.f*(float)(bi%4);
			float vs = 16.f + 8.f*(float)(bi%3);
			card(bx0,by0,bz0, hs, vs, br,bg,bb,.86f);
		}
		nSky3 = nTrans - nSky - nSky2;
		for (int bi=0;bi<nCard4;bi++){
			float ang = (float)bi * (float)(M_PI*2.0/(double)nCard4) - m_anim*0.015f + 2.2f;
			float dist = 110.f + 28.f*(bi%4);
			float bx0 = pl.x + cosf(ang)*dist;
			float by0 = pl.y + 22.f + 8.f*sinf(ang*2.1f+m_anim*0.07f);
			float bz0 = pl.z + sinf(ang)*dist;
			float hs = 36.f + 14.f*(float)(bi%3);
			float vs = 8.f + 5.f*(float)(bi%4);
			card(bx0,by0,bz0, hs, vs, br,bg,bb,.78f);
		}
		nSky4 = nTrans - nSky - nSky2 - nSky3;
	}
	for (int i=0;i<m_itemN;i++){
		S3rItem& it=m_items[i]; if (it.taken) continue;
		float r=1,g=1,b=1;
		switch(it.kind){
		case KIND_TEMPO: r=.3f;g=1;b=.45f; break; case KIND_TEMPO_DN: r=.1f;g=.55f;b=.25f; break;
		case KIND_PITCH_UP: r=1;g=.65f;b=.25f; break; case KIND_PITCH_DN: r=.35f;g=.55f;b=1; break;
		case KIND_NEXT: r=1;g=.2f;b=.35f; break; case KIND_PREV: r=.55f;g=.1f;b=.2f; break;
		case KIND_VOL_UP: r=1;g=.95f;b=.35f; break; case KIND_VOL_DN: r=.55f;g=.6f;b=.2f; break;
		case KIND_EQ: r=.75f;g=.35f;b=1; break; case KIND_EQ_FLAT: r=.6f;g=.55f;b=.8f; break;
		case KIND_REVERB: r=.2f;g=.85f;b=.95f; break; case KIND_XFADE: r=1;g=.45f;b=.75f; break;
		default: r=1;g=.55f;b=.2f; break;
		}
		const float rad=.55f;
		const int nl=S3MeshScaleCount(8, 16), nb=S3MeshScaleCount(5, 12);
		for(int ii=0;ii<nl;ii++){
			float a0=(float)ii*(6.2831853f/(float)nl)+it.spin, a1=(float)(ii+1)*(6.2831853f/(float)nl)+it.spin;
			for(int jj=0;jj<nb;jj++){
				float b0=-1.5707963f+(float)jj*(3.14159265f/(float)nb);
				float b1=b0+(3.14159265f/(float)nb);
				auto P=[&](float an,float bn,float& x,float& y,float& z,float& nx,float& ny,float& nz,float& u,float& vv){
					float cb=cosf(bn),sb=sinf(bn),ca=cosf(an),sa=sinf(an);
					nx=cb*ca; ny=sb; nz=cb*sa;
					x=it.x+nx*rad; y=it.y+ny*rad; z=it.z+nz*rad;
					u=an*(1.f/6.2831853f); vv=(bn+1.5707963f)*(1.f/3.14159265f);
				};
				float x0,y0,z0,n0x,n0y,n0z,uA,vA, x1,y1,z1,n1x,n1y,n1z,uB,vB, x2,y2,z2,n2x,n2y,n2z,uC,vC, x3,y3,z3,n3x,n3y,n3z,uD,vD;
				P(a0,b0,x0,y0,z0,n0x,n0y,n0z,uA,vA); P(a1,b0,x1,y1,z1,n1x,n1y,n1z,uB,vB);
				P(a1,b1,x2,y2,z2,n2x,n2y,n2z,uC,vC); P(a0,b1,x3,y3,z3,n3x,n3y,n3z,uD,vD);
				put(x0,y0,z0,n0x,n0y,n0z,uA,vA,r,g,b,.90f); put(x1,y1,z1,n1x,n1y,n1z,uB,vB,r,g,b,.90f); put(x2,y2,z2,n2x,n2y,n2z,uC,vC,r,g,b,.90f);
				put(x0,y0,z0,n0x,n0y,n0z,uA,vA,r,g,b,.90f); put(x2,y2,z2,n2x,n2y,n2z,uC,vC,r,g,b,.90f); put(x3,y3,z3,n3x,n3y,n3z,uD,vD,r,g,b,.90f);
			}
		}
		const int nRing = S3MeshScaleCount(8, 16);
		for(int k=0;k<nRing;k++){
			float a0=(float)k*(6.2831853f/(float)nRing)+it.spin*1.4f, a1=a0+(6.2831853f/(float)nRing);
			float ri=rad*1.08f, ro=rad*1.28f, y0=it.y-rad*.07f, y1=it.y+rad*.07f;
			float x00=it.x+cosf(a0)*ri, z00=it.z+sinf(a0)*ri;
			float x10=it.x+cosf(a1)*ri, z10=it.z+sinf(a1)*ri;
			float x01=it.x+cosf(a0)*ro, z01=it.z+sinf(a0)*ro;
			float x11=it.x+cosf(a1)*ro, z11=it.z+sinf(a1)*ro;
			put(x00,y0,z00,0,1,0,0,0,r,g,b,.88f); put(x10,y0,z10,0,1,0,1,0,r,g,b,.88f); put(x11,y1,z11,0,1,0,1,1,r,g,b,.88f);
			put(x00,y0,z00,0,1,0,0,0,r,g,b,.88f); put(x11,y1,z11,0,1,0,1,1,r,g,b,.88f); put(x01,y1,z01,0,1,0,0,1,r,g,b,.88f);
		}
	}
	nItem = nTrans - nSky - nSky2 - nSky3 - nSky4;
	// podium confetti
	if (m_phase==PHASE_PODIUM){
		for (int i=0;i<96;i++){
			if (m_confetti[i][5]<=0) continue;
			float x=m_confetti[i][0], y=m_confetti[i][1], z=m_confetti[i][2], s=.25f;
			float r=kCraftColors[i%12][0], g=kCraftColors[i%12][1], b=kCraftColors[i%12][2];
			put(x-s,y,z,0,1,0,0,0,r,g,b,1); put(x+s,y,z,0,1,0,1,0,r,g,b,1); put(x,y+s,z,0,1,0,.5f,1,r,g,b,1);
		}
	}
	// 爆破破片
	for (int i=0;i<S3R_BLAST_N;i++){
		if (m_blast[i][6]<=0.f) continue;
		float x=m_blast[i][0], y=m_blast[i][1], z=m_blast[i][2];
		float life=m_blast[i][6];
		float s=0.22f + (1.f - S3rSaturate(life))*0.55f;
		float a=S3rSaturate(life*1.4f);
		float r=1.f, g=0.38f+0.45f*S3rSaturate(life), b=0.10f;
		put(x-s,y,z,0,1,0,0,0,r,g,b,a); put(x+s,y,z,0,1,0,1,0,r,g,b,a); put(x,y+s,z,0,1,0,.5f,1,r,g,b,a);
	}
	for (int i=0;i<S3R_FX_N;i++){
		S3rFx& p=m_fx[i];
		if (p.life<=0.f) continue;
		float u=S3rSaturate(p.life / max(0.02f, p.lifeMax));
		float s=p.size;
		float a=u;
		if (p.kind==FX_SMOKE) { s *= 1.15f + (1.f-u)*1.85f; a = 0.40f * u; }
		else if (p.kind==FX_DUST) { s *= 1.1f + (1.f-u)*1.2f; a = 0.48f * u; }
		else if (p.kind==FX_PICK) { s *= 0.85f + u * 0.55f; a = 0.90f * u; }
		else if (p.kind==FX_EMBER) { s *= 0.75f + u * 0.50f; a = 0.88f * u; }
		else { s *= 0.70f + u * 0.55f; a = 0.92f * u; }
		put(p.x-s,p.y,p.z,0,1,0,0,0,p.r,p.g,p.b,a);
		put(p.x+s,p.y,p.z,0,1,0,1,0,p.r,p.g,p.b,a);
		put(p.x,p.y+s,p.z,0,1,0,.5f,1,p.r,p.g,p.b,a);
	}
	for (int i=0;i<m_craftN;i++){
		S3rCraft& c=m_crafts[i];
		if (c.explodeT > 0.f) {
			float t = 1.f - S3rSaturate(c.explodeT / 1.20f);
			float rad = 0.55f + t * 4.4f;
			float aa = (1.f - t) * 0.80f;
			for (int k=0;k<10;k++){
				float ang = (float)k * 0.628f + m_anim * 4.f;
				float px = c.x + cosf(ang)*rad;
				float py = c.y + 0.35f + sinf((float)k*1.7f)*rad*0.40f;
				float pz = c.z + sinf(ang)*rad;
				float s = 0.32f + t * 0.70f;
				float r=1.f, g=0.42f+0.35f*(1.f-t), b=0.08f;
				put(px-s,py,pz,0,1,0,0,0,r,g,b,aa); put(px+s,py,pz,0,1,0,1,0,r,g,b,aa); put(px,py+s,pz,0,1,0,.5f,1,r,g,b,aa);
			}
		}
		if (c.smokeT > 0.f && (!c.alive || c.retired)) {
			float u = S3rSaturate(c.smokeT / 6.f);
			for (int k=0;k<5;k++){
				float ang = (float)k * 1.256f + m_anim * 0.8f;
				float up = (1.f - u) * 2.8f + (float)k * 0.22f;
				float px = c.x + cosf(ang)*0.45f;
				float py = c.y + 0.4f + up;
				float pz = c.z + sinf(ang)*0.45f;
				float s = 0.28f + (1.f-u)*0.35f;
				float g = 0.22f + 0.12f * (float)k;
				put(px-s,py,pz,0,1,0,0,0,g,g,g,0.35f*u); put(px+s,py,pz,0,1,0,1,0,g,g,g,0.35f*u); put(px,py+s,pz,0,1,0,.5f,1,g,g,g,0.35f*u);
			}
		}
		if (c.alive && !c.retired && c.dmgAccum > 8.f) {
			float sev = S3rSaturate((c.dmgAccum - 8.f) / 130.f);
			float fx = sinf(c.yaw)*cosf(c.pitch), fy = sinf(c.pitch), fz = cosf(c.yaw)*cosf(c.pitch);
			float rx = cosf(c.yaw), rz = -sinf(c.yaw);
			const int nPuff = 3 + (int)(sev * 9.f);
			for (int k = 0; k < nPuff; k++) {
				float t = ((float)k + fmodf(m_anim * (3.4f + 2.2f * sev) + (float)i * 0.37f, 1.f)) / (float)nPuff;
				float along = 0.40f + t * (1.15f + sev * 2.0f);
				float wob = sinf(m_anim * 2.8f + (float)k * 1.8f + (float)i) * (0.16f + 0.38f * t);
				float px = c.x - fx * along + rx * wob;
				float py = c.y - fy * along + 0.22f + t * (0.70f + sev * 1.55f);
				float pz = c.z - fz * along + rz * wob;
				float s = (0.20f + t * 0.42f + sev * 0.28f);
				float aa = (1.f - t) * (0.22f + 0.38f * sev);
				float g = 0.14f + 0.16f * t;
				float r = g, b = g;
				if (sev > 0.58f && (k & 1)) {
					float em = (sev - 0.58f) / 0.42f;
					r = S3rLerp(g, 0.62f, em); g = S3rLerp(g, 0.22f, em); b = S3rLerp(b, 0.06f, em);
					aa *= 0.85f;
				}
				put(px-s,py,pz,0,1,0,0,0,r,g,b,aa); put(px+s,py,pz,0,1,0,1,0,r,g,b,aa); put(px,py+s,pz,0,1,0,.5f,1,r,g,b,aa);
			}
		}
		// ニトロ噴射（後方シアン／オレンジの炎）
		if (c.nitroT > 0.f && c.alive && !c.retired) {
			float fx = sinf(c.yaw)*cosf(c.pitch), fy = sinf(c.pitch), fz = cosf(c.yaw)*cosf(c.pitch);
			float rx = cosf(c.yaw), rz = -sinf(c.yaw);
			float life = S3rSaturate(c.nitroT / 2.85f);
			const int jets = c.isPlayer ? 14 : 10;
			for (int k = 0; k < jets; k++) {
				float t = ((float)k + fmodf(m_anim * 18.f, 1.f)) / (float)jets;
				float along = 0.55f + t * (2.4f + 1.1f * life);
				float wob = sinf(m_anim * 22.f + (float)k * 1.7f + (float)i) * (0.12f + 0.18f * t);
				float px = c.x - fx * along + rx * wob;
				float py = c.y - fy * along - 0.08f - t * 0.35f;
				float pz = c.z - fz * along + rz * wob;
				float s = (0.18f + (1.f - t) * 0.55f) * (0.85f + 0.35f * life);
				float aa = (1.f - t) * (0.55f + 0.40f * life);
				float r = 0.25f + 0.75f * (1.f - t);
				float g = 0.55f + 0.40f * t;
				float b = 1.f;
				if (t > 0.45f) { r = 1.f; g = 0.55f + 0.35f * (1.f - t); b = 0.15f; }
				put(px-s,py,pz,0,1,0,0,0,r,g,b,aa); put(px+s,py,pz,0,1,0,1,0,r,g,b,aa); put(px,py+s,pz,0,1,0,.5f,1,r,g,b,aa);
			}
		}
		// テンポ加速トレイル（緑）
		if (c.boostT > 0.f && c.alive && !c.retired && c.nitroT <= 0.f) {
			float fx = sinf(c.yaw)*cosf(c.pitch), fy = sinf(c.pitch), fz = cosf(c.yaw)*cosf(c.pitch);
			float rx = cosf(c.yaw), rz = -sinf(c.yaw);
			float life = S3rSaturate(c.boostT / 4.5f);
			const int jets = 8;
			for (int k = 0; k < jets; k++) {
				float t = ((float)k + fmodf(m_anim * 14.f, 1.f)) / (float)jets;
				float along = 0.40f + t * (1.6f + 0.7f * life);
				float wob = sinf(m_anim * 16.f + (float)k * 1.4f + (float)i) * (0.10f + 0.14f * t);
				float px = c.x - fx * along + rx * wob;
				float py = c.y - fy * along - t * 0.22f;
				float pz = c.z - fz * along + rz * wob;
				float s = (0.12f + (1.f - t) * 0.32f) * (0.8f + 0.25f * life);
				float aa = (1.f - t) * (0.35f + 0.25f * life);
				put(px-s,py,pz,0,1,0,0,0,.25f,1.f,.45f,aa); put(px+s,py,pz,0,1,0,1,0,.25f,1.f,.45f,aa); put(px,py+s,pz,0,1,0,.5f,1,.25f,1.f,.45f,aa);
			}
		}
		// 減速ヘイズ（紫）
		if (c.slowT > 0.f && c.alive && !c.retired) {
			float life = S3rSaturate(c.slowT / 4.0f);
			for (int k = 0; k < 6; k++) {
				float ang = (float)k * 1.047f + m_anim * 1.6f;
				float rad = 0.35f + 0.25f * sinf(m_anim * 3.f + (float)k);
				float px = c.x + cosf(ang) * rad;
				float py = c.y + 0.15f + (float)k * 0.08f;
				float pz = c.z + sinf(ang) * rad;
				float s = 0.22f + life * 0.18f;
				float aa = 0.22f * life;
				put(px-s,py,pz,0,1,0,0,0,.45f,.18f,.75f,aa); put(px+s,py,pz,0,1,0,1,0,.45f,.18f,.75f,aa); put(px,py+s,pz,0,1,0,.5f,1,.45f,.18f,.75f,aa);
			}
		}
		// 機動バフの黄スパーク
		if (c.agilityT > 0.f && c.alive && !c.retired) {
			float life = S3rSaturate(c.agilityT / 5.f);
			for (int k = 0; k < 5; k++) {
				float ang = (float)k * 1.256f + m_anim * 9.f;
				float px = c.x + cosf(ang) * 0.55f;
				float py = c.y + 0.25f + 0.15f * sinf(m_anim * 12.f + (float)k);
				float pz = c.z + sinf(ang) * 0.55f;
				float s = 0.08f + 0.06f * life;
				float aa = 0.45f * life;
				put(px-s,py,pz,0,1,0,0,0,1.f,.85f,.25f,aa); put(px+s,py,pz,0,1,0,1,0,1.f,.85f,.25f,aa); put(px,py+s,pz,0,1,0,.5f,1,1.f,.85f,.25f,aa);
			}
		}
	}
	// HQ: トンネル内ライトシャフト（末尾に置き additive 描画）
	{
		const UINT shaftBeg = nBand + nWorld + nCraft + nTrans;
		if ((m_phase == PHASE_RACE || m_phase == PHASE_COUNTDOWN || m_phase == PHASE_FINISH)
			&& m_camTunnelT > 0.12f && !m_lookback) {
			float sunR=1.f, sunG=.92f, sunB=.72f;
			if (m_themeActive==THEME_NIGHT){ sunR=.45f; sunG=.55f; sunB=.95f; }
			else if (m_themeActive==THEME_UNDER){ sunR=.35f; sunG=.75f; sunB=.9f; }
			else if (m_themeActive==THEME_MESA){ sunR=1.f; sunG=.7f; sunB=.4f; }
			float rx = ax - cx, ry = ay - cy, rz = az - cz; S3rNorm3(rx,ry,rz);
			float ux=0,uy=1,uz=0;
			float ssx=uy*rz-uz*ry, ssy=uz*rx-ux*rz, ssz=ux*ry-uy*rx; S3rNorm3(ssx,ssy,ssz);
			float u2x=ry*ssz-rz*ssy, u2y=rz*ssx-rx*ssz, u2z=rx*ssy-ry*ssx;
			const float tk = m_camTunnelT;
			for (int i=0;i<7;i++){
				float t = (float)i / 6.f;
				float along = 4.f + t * 28.f;
				float side = ((i%3)-1) * 1.1f;
				float px = pl.x + noseX * along + (-noseZ) * side;
				float py = pl.y + 2.2f + noseY * along * 0.35f + sinf(m_anim*1.5f+(float)i)*.4f;
				float pz = pl.z + noseZ * along + noseX * side;
				float hs = 0.35f + t * 1.8f;
				float vs = 6.f + t * 14.f;
				float a = (0.10f + 0.12f * (1.f-t)) * tk;
				float x0=px-ssx*hs-u2x*vs, y0=py-ssy*hs-u2y*vs, z0=pz-ssz*hs-u2z*vs;
				float x1=px+ssx*hs-u2x*vs, y1=py+ssy*hs-u2y*vs, z1=pz+ssz*hs-u2z*vs;
				float x2=px+ssx*hs+u2x*vs, y2=py+ssy*hs+u2y*vs, z2=pz+ssz*hs+u2z*vs;
				float x3=px-ssx*hs+u2x*vs, y3=py-ssy*hs+u2y*vs, z3=pz+ssz*hs+u2z*vs;
				put(x0,y0,z0,-rx,-ry,-rz,0,0,sunR,sunG,sunB,a); put(x1,y1,z1,-rx,-ry,-rz,1,0,sunR,sunG,sunB,a); put(x2,y2,z2,-rx,-ry,-rz,1,1,sunR,sunG,sunB,a);
				put(x0,y0,z0,-rx,-ry,-rz,0,0,sunR,sunG,sunB,a); put(x2,y2,z2,-rx,-ry,-rz,1,1,sunR,sunG,sunB,a); put(x3,y3,z3,-rx,-ry,-rz,0,1,sunR,sunG,sunB,a);
			}
			nShaft = (nBand + nWorld + nCraft + nTrans) - shaftBeg;
		}
	}

	UINT craftDrawN = 0;
	{
		S3RInst ci[S3R_MAX_CRAFT];
		if (m_phase == PHASE_PODIUM) {
			for (int s = 0; s < 3 && craftDrawN < (UINT)S3R_MAX_CRAFT; s++) {
				int idx = m_podiumOrder[s];
				if (idx < 0 || idx >= m_craftN) continue;
				S3rCraft& c = m_crafts[idx];
				float cr = kCraftColors[c.colorIdx][0], cg = kCraftColors[c.colorIdx][1], cb = kCraftColors[c.colorIdx][2];
				ci[craftDrawN++] = {c.x, c.y, c.z, c.yaw, 1.25f, 1.25f, 1.25f, c.pitch, cr, cg, cb, 1.f};
			}
		} else {
			for (int i = 0; i < m_craftN && craftDrawN < (UINT)S3R_MAX_CRAFT; i++) {
				S3rCraft& c = m_crafts[i];
				float cr = kCraftColors[c.colorIdx][0], cg = kCraftColors[c.colorIdx][1], cb = kCraftColors[c.colorIdx][2];
				float sc = 1.1f, a = 1.f;
				if (!c.alive || c.retired) {
					sc = 0.78f; a = 0.48f;
					cr *= 0.42f; cg *= 0.38f; cb *= 0.38f;
					if (c.explodeT > 0.f) {
						float k = S3rSaturate(c.explodeT / 1.20f);
						sc = 0.55f + 0.70f * k;
						a = 0.28f + 0.55f * k;
						cr = S3rLerp(cr, 1.f, 0.65f);
						cg = S3rLerp(cg, 0.45f, 0.50f);
					}
				} else if (c.nitroT > 0.f) {
					float np = S3rSaturate(c.nitroT / 2.85f);
					float pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf(m_anim * 26.f + (float)i));
					cr = S3rLerp(cr, 0.35f, 0.55f * np * pulse);
					cg = S3rLerp(cg, 0.85f, 0.45f * np * pulse);
					cb = S3rLerp(cb, 1.f, 0.70f * np * pulse);
					sc = 1.1f + 0.08f * np;
				}
				if (c.alive && !c.retired && c.dmgAccum > 10.f) {
					float soot = S3rSaturate(c.dmgAccum / 160.f);
					cr *= 1.f - 0.48f * soot;
					cg *= 1.f - 0.52f * soot;
					cb *= 1.f - 0.50f * soot;
				}
				ci[craftDrawN++] = {c.x, c.y, c.z, c.yaw, sc, sc, sc, c.pitch, cr, cg, cb, a};
			}
		}
		if (craftDrawN && m_view.m_vbCraftInst) {
			if (SUCCEEDED(dc->Map(m_view.m_vbCraftInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
				memcpy(map.pData, ci, craftDrawN * sizeof(S3RInst));
				dc->Unmap(m_view.m_vbCraftInst, 0);
			} else craftDrawN = 0;
		} else if (!m_view.m_vbCraftInst) craftDrawN = 0;
	}

	const UINT total = nBand + nWorld + nCraft + nTrans;
	if (total > 0) {
		if (FAILED(dc->Map(m_view.m_vbDyn,0,D3D11_MAP_WRITE_DISCARD,0,&map))) return;
		memcpy(map.pData, v, total * sizeof(S3RVertex));
		dc->Unmap(m_view.m_vbDyn,0);
	}

	float clearC[4]={0.45f,0.62f,0.92f,1.f};
	if (m_themeActive==THEME_NIGHT){clearC[0]=.08f;clearC[1]=.1f;clearC[2]=.2f;}
	else if (m_themeActive==THEME_UNDER){clearC[0]=.1f;clearC[1]=.25f;clearC[2]=.4f;}
	else if (m_themeActive==THEME_MESA){clearC[0]=.95f;clearC[1]=.55f;clearC[2]=.35f;}
	else if (m_themeActive==THEME_OIL){clearC[0]=.25f;clearC[1]=.28f;clearC[2]=.32f;}
	else if (m_themeActive==THEME_CLOUD){clearC[0]=.85f;clearC[1]=.9f;clearC[2]=1.f;}
	else if (m_themeActive==THEME_FOREST){clearC[0]=.42f;clearC[1]=.62f;clearC[2]=.48f;}
	else if (m_themeActive==THEME_GRASS){clearC[0]=.55f;clearC[1]=.72f;clearC[2]=.55f;}
	else if (m_themeActive==THEME_RUINS){clearC[0]=.55f;clearC[1]=.52f;clearC[2]=.48f;}

	const UINT nSolid = nWorld + nCraft;
	auto bindMesh=[&](ID3D11Buffer* vb){
		UINT st=sizeof(S3RVertex), of=0;
		dc->IASetVertexBuffers(0,1,&vb,&st,&of);
	};
	auto drawParts=[&](ID3D11Buffer** vb, UINT* nn, int parts, UINT align){
		for (int i = 0; i < parts; i++) {
			if (!vb[i] || nn[i] < align) continue;
			UINT d = nn[i];
			if (align > 1) d -= d % align;
			if (d < align) continue;
			bindMesh(vb[i]);
			dc->Draw(d, 0);
		}
	};
	auto bindInst=[&](ID3D11Buffer* vb, ID3D11Buffer* ib, ID3D11Buffer* inst){
		ID3D11Buffer* b[2]={vb,inst};
		UINT st[2]={sizeof(S3RVertex), sizeof(S3RInst)};
		UINT of[2]={0,0};
		dc->IASetVertexBuffers(0,2,b,st,of);
		dc->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
		dc->IASetInputLayout(m_view.m_ilInst);
		dc->VSSetShader(m_view.m_vsInst,NULL,0);
	};
	auto unbindInst=[&](){
		ID3D11Buffer* z[2]={NULL,NULL};
		UINT st[2]={0,0}, of[2]={0,0};
		dc->IASetVertexBuffers(0,2,z,st,of);
		dc->IASetIndexBuffer(NULL, DXGI_FORMAT_R32_UINT, 0);
		dc->IASetInputLayout(m_view.m_ilSolid);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0);
		UINT st1=sizeof(S3RVertex), of1=0;
		dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&st1,&of1);
	};
	auto drawObsGpu=[&](){
		if (m_phase == PHASE_PODIUM) return;
		if (!m_view.m_vsInst || !m_view.m_ilInst || !m_view.m_vbObs || !m_view.m_ibObs || !m_view.m_vbObsInst) return;
		if (m_view.m_obsNiGpu < 3 || m_view.m_obsInstN < 1) return;
		bindInst(m_view.m_vbObs, m_view.m_ibObs, m_view.m_vbObsInst);
		dc->DrawIndexedInstanced(m_view.m_obsNiGpu, m_view.m_obsInstN, 0, 0, 0);
		unbindInst();
	};
	auto drawCraftGpu=[&](){
		if (!craftDrawN || !m_view.m_vsInst || !m_view.m_ilInst || !m_view.m_vbCraft || !m_view.m_ibCraft || !m_view.m_vbCraftInst) return;
		if (m_view.m_craftNiGpu < 3) return;
		bindInst(m_view.m_vbCraft, m_view.m_ibCraft, m_view.m_vbCraftInst);
		dc->DrawIndexedInstanced(m_view.m_craftNiGpu, craftDrawN, 0, 0, 0);
		unbindInst();
	};
	// shadow pass — VP を LightVP にして投影（カメラVPのままだと影が落ちない）
	{
		S3RFrameCB cbSh = cb;
		cbSh.viewProj = cb.lightVP;
		cbSh.lightDir.w = 0.f;
		if (SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){ memcpy(map.pData,&cbSh,sizeof(cbSh)); dc->Unmap(m_view.m_cbFrame,0); }
		D3D11_VIEWPORT sp={0,0,(float)CS3rView::S3R_SHADOW_SIZE,(float)CS3rView::S3R_SHADOW_SIZE,0,1};
		dc->RSSetViewports(1,&sp); dc->OMSetRenderTargets(0,NULL,m_view.m_shadowDsv); dc->ClearDepthStencilView(m_view.m_shadowDsv,D3D11_CLEAR_DEPTH,1,0);
		dc->RSSetState(m_view.m_rsShadow); dc->OMSetDepthStencilState(m_view.m_dssWrite,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0); dc->PSSetShader(NULL,NULL,0); dc->IASetInputLayout(m_view.m_ilSolid);
		dc->VSSetConstantBuffers(0,1,&m_view.m_cbFrame); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		UINT stride=sizeof(S3RVertex), off=0;
		drawParts(m_view.m_vbTerr, m_view.m_vbTerrN, m_view.m_vbTerrParts, 3);
		drawParts(m_view.m_vbScenery, m_view.m_vbSceneryN, m_view.m_vbSceneryParts, 3);
		drawObsGpu();
		drawCraftGpu();
		dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);
		if (nSolid) dc->Draw(nSolid, nBand);
	}

	// 水面平面反射マップ（ゲーム中フル／デモは省略）
	if (hq && m_view.m_reflectRtv && m_view.m_vbWaterParts > 0 && m_phase != PHASE_PODIUM
		&& pl.y < m_waterY + 55.f) {
		S3RFrameCB cbRef = cb;
		cbRef.viewProj = cb.reflectVP;
		cbRef.peel = {0,0,0,0};
		cbRef.eyePos = {cx, 2.f * m_waterY - cy, cz, (float)(m_themeActive-1)};
		cbRef.lightDir.w = 1.f;
		cbRef.fogParams.w = 1.f; // Fog.z=水面Y → 水面下を discard
		if (SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){ memcpy(map.pData,&cbRef,sizeof(cbRef)); dc->Unmap(m_view.m_cbFrame,0); }
		D3D11_VIEWPORT rvp={0,0,(float)CS3rView::S3R_REFLECT_SIZE,(float)CS3rView::S3R_REFLECT_SIZE,0,1};
		dc->RSSetViewports(1,&rvp);
		float rclear[4]={clearC[0],clearC[1],clearC[2],1.f};
		dc->OMSetRenderTargets(1,&m_view.m_reflectRtv,m_view.m_reflectDsv);
		dc->ClearRenderTargetView(m_view.m_reflectRtv, rclear);
		dc->ClearDepthStencilView(m_view.m_reflectDsv, D3D11_CLEAR_DEPTH, 1, 0);
		dc->RSSetState(m_view.m_rsNoCull); dc->OMSetDepthStencilState(m_view.m_dssWrite,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
		dc->VSSetConstantBuffers(0,1,&m_view.m_cbFrame); dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);
		dc->HSSetConstantBuffers(0,1,&m_view.m_cbFrame); dc->DSSetConstantBuffers(0,1,&m_view.m_cbFrame);
		dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0); dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0);
		ID3D11SamplerState* smpR[]={m_view.m_sampLin,m_view.m_sampPoint,m_view.m_sampCmp};
		dc->PSSetSamplers(0,3,smpR); dc->DSSetSamplers(0,1,&m_view.m_sampLin);
		int thIdxR=m_themeActive-1; if(thIdxR<0)thIdxR=0; if(thIdxR>7)thIdxR=7;
		ID3D11ShaderResourceView* themeR=m_view.m_srvTheme[thIdxR];
		ID3D11ShaderResourceView* themeDR=m_view.m_srvThemeD[thIdxR]?m_view.m_srvThemeD[thIdxR]:themeR;
		ID3D11ShaderResourceView* envR=m_view.m_srvEnv;
		if(m_view.m_srvEnv2 && (m_themeActive==THEME_CLOUD||m_themeActive==THEME_GRASS||m_themeActive==THEME_FOREST||m_themeActive==THEME_MESA||m_themeActive==THEME_UNDER))
			envR=m_view.m_srvEnv2;
		ID3D11ShaderResourceView* shR=m_view.m_shadowSrv;
		ID3D11ShaderResourceView* noiseR=m_view.m_srvNoise;
		ID3D11ShaderResourceView* obsR=m_view.m_srvObs?m_view.m_srvObs:themeDR;
		ID3D11ShaderResourceView* bandR=m_view.m_srvBand;
		if (m_view.m_vbTerrParts > 0) {
			dc->PSSetShader(m_view.m_psTerr,NULL,0);
			ID3D11ShaderResourceView* srvs[]={themeR,themeDR,NULL,envR,shR,noiseR};
			dc->PSSetShaderResources(0,6,srvs);
			drawParts(m_view.m_vbTerr, m_view.m_vbTerrN, m_view.m_vbTerrParts, 3);
		}
		dc->PSSetShader(m_view.m_psSolid,NULL,0);
		{
			ID3D11ShaderResourceView* scenT1=m_view.m_srvWood?m_view.m_srvWood:obsR;
			ID3D11ShaderResourceView* srvs[]={themeR,scenT1,NULL,envR,shR,noiseR};
			dc->PSSetShaderResources(0,6,srvs);
			if (m_view.m_vbSceneryParts > 0)
				drawParts(m_view.m_vbScenery, m_view.m_vbSceneryN, m_view.m_vbSceneryParts, 3);
		}
		{
			ID3D11ShaderResourceView* osrvs[]={themeR,obsR,NULL,envR,shR,noiseR};
			dc->PSSetShaderResources(0,6,osrvs);
		}
		drawObsGpu();
		if (craftDrawN) {
			dc->RSSetState(m_view.m_rsSolid);
			dc->PSSetShader(m_view.m_psCraft ? m_view.m_psCraft : m_view.m_psSolid,NULL,0);
			ID3D11ShaderResourceView* craftSrv = m_view.m_srvCraft ? m_view.m_srvCraft : themeR;
			ID3D11ShaderResourceView* craftDet = m_view.m_srvCraftD ? m_view.m_srvCraftD : craftSrv;
			ID3D11ShaderResourceView* srvs[]={craftSrv,craftDet,NULL,envR,shR,noiseR};
			dc->PSSetShaderResources(0,6,srvs);
			drawCraftGpu();
		}
		if (m_view.m_vbBandParts > 0) {
			dc->RSSetState(m_view.m_rsNoCull); dc->OMSetDepthStencilState(m_view.m_dssRead,0); dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff);
			dc->IASetInputLayout(m_view.m_ilPatch); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
			dc->VSSetShader(m_view.m_vsTess,NULL,0); dc->HSSetShader(m_view.m_hsTess,NULL,0); dc->DSSetShader(m_view.m_dsTess,NULL,0); dc->PSSetShader(m_view.m_psBand,NULL,0);
			ID3D11ShaderResourceView* srvs[]={bandR,themeR,NULL,envR,shR,noiseR};
			dc->PSSetShaderResources(0,6,srvs); dc->DSSetShaderResources(5,1,&noiseR); dc->DSSetShaderResources(0,1,&bandR);
			drawParts(m_view.m_vbBand, m_view.m_vbBandN, m_view.m_vbBandParts, 4);
			dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0);
		}
		if (nSky || nSky2 || nSky3 || nSky4) {
			dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->VSSetShader(m_view.m_vsSolid,NULL,0);
			dc->PSSetShader(m_view.m_psCloud ? m_view.m_psCloud : m_view.m_psSolid,NULL,0);
			dc->RSSetState(m_view.m_rsNoCull);
			dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff); dc->OMSetDepthStencilState(m_view.m_dssRead,0);
			bindMesh(m_view.m_vbDyn);
			const UINT t0 = nBand+nWorld+nCraft;
			ID3D11ShaderResourceView* s1=m_view.m_srvSky, *s2=m_view.m_srvSky2?m_view.m_srvSky2:s1;
			ID3D11ShaderResourceView* s3=m_view.m_srvSky3?m_view.m_srvSky3:s1;
			ID3D11ShaderResourceView* s4=m_view.m_srvSky4?m_view.m_srvSky4:s2;
			auto bindT=[&](ID3D11ShaderResourceView* a, ID3D11ShaderResourceView* b){
				ID3D11ShaderResourceView* srvs[]={a?a:themeR, b?b:(a?a:themeDR), NULL, envR, shR, noiseR};
				dc->PSSetShaderResources(0,6,srvs);
			};
			if(nSky){ bindT(s1,s2); dc->Draw(nSky, t0); }
			if(nSky2){ bindT(s2,s1); dc->Draw(nSky2, t0+nSky); }
			if(nSky3){ bindT(s3,s4); dc->Draw(nSky3, t0+nSky+nSky2); }
			if(nSky4){ bindT(s4,s3); dc->Draw(nSky4, t0+nSky+nSky2+nSky3); }
		}
		ID3D11ShaderResourceView* nullsR[6]={}; dc->PSSetShaderResources(0,6,nullsR);
	}

	// バックミラー（ゲーム中フル／デモ俯瞰は省略）
	if (hq && m_view.m_rearRtv && !m_lookback && m_phase != PHASE_PODIUM) {
		float rcx = pl.x + noseX * 2.2f;
		float rcy = pl.y + 1.85f + noseY * 0.35f;
		float rcz = pl.z + noseZ * 2.2f;
		float rax = pl.x - noseX * 36.f;
		float ray = pl.y + 1.2f - noseY * 1.5f;
		float raz = pl.z - noseZ * 36.f;
		S3RFrameCB cbRear = cb;
		cbRear.viewProj = S3rMatMul(S3rLookAt(rcx,rcy,rcz, rax,ray,raz, 0,1,0),
			S3rPerspective(55.f * (float)(M_PI/180.0), (float)CS3rView::S3R_REAR_W/(float)CS3rView::S3R_REAR_H, 0.45f, 520.f));
		cbRear.eyePos = {rcx,rcy,rcz,(float)(m_themeActive-1)};
		cbRear.peel = {0,0,0,0};
		cbRear.lightDir.w = 1.f;
		if (SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){ memcpy(map.pData,&cbRear,sizeof(cbRear)); dc->Unmap(m_view.m_cbFrame,0); }
		D3D11_VIEWPORT mvp={0,0,(float)CS3rView::S3R_REAR_W,(float)CS3rView::S3R_REAR_H,0,1};
		dc->RSSetViewports(1,&mvp);
		float mclear[4]={clearC[0],clearC[1],clearC[2],1.f};
		dc->OMSetRenderTargets(1,&m_view.m_rearRtv,m_view.m_rearDsv);
		dc->ClearRenderTargetView(m_view.m_rearRtv, mclear);
		dc->ClearDepthStencilView(m_view.m_rearDsv, D3D11_CLEAR_DEPTH, 1, 0);
		dc->RSSetState(m_view.m_rsNoCull); dc->OMSetDepthStencilState(m_view.m_dssWrite,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
		dc->VSSetConstantBuffers(0,1,&m_view.m_cbFrame); dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);
		dc->HSSetConstantBuffers(0,1,&m_view.m_cbFrame); dc->DSSetConstantBuffers(0,1,&m_view.m_cbFrame);
		dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0); dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0);
		ID3D11SamplerState* smpM[]={m_view.m_sampLin,m_view.m_sampPoint,m_view.m_sampCmp};
		dc->PSSetSamplers(0,3,smpM); dc->DSSetSamplers(0,1,&m_view.m_sampLin);
		int thIdxM=m_themeActive-1; if(thIdxM<0)thIdxM=0; if(thIdxM>7)thIdxM=7;
		ID3D11ShaderResourceView* themeM=m_view.m_srvTheme[thIdxM];
		ID3D11ShaderResourceView* themeDM=m_view.m_srvThemeD[thIdxM]?m_view.m_srvThemeD[thIdxM]:themeM;
		ID3D11ShaderResourceView* envM=m_view.m_srvEnv;
		if(m_view.m_srvEnv2 && (m_themeActive==THEME_CLOUD||m_themeActive==THEME_GRASS||m_themeActive==THEME_FOREST||m_themeActive==THEME_MESA||m_themeActive==THEME_UNDER))
			envM=m_view.m_srvEnv2;
		ID3D11ShaderResourceView* shM=m_view.m_shadowSrv;
		ID3D11ShaderResourceView* noiseM=m_view.m_srvNoise;
		ID3D11ShaderResourceView* obsM=m_view.m_srvObs?m_view.m_srvObs:themeDM;
		ID3D11ShaderResourceView* waterM=m_view.m_srvWater?m_view.m_srvWater:themeM;
		ID3D11ShaderResourceView* bandM=m_view.m_srvBand;
		if (m_view.m_vbTerrParts > 0) {
			dc->PSSetShader(m_view.m_psTerr,NULL,0);
			ID3D11ShaderResourceView* srvs[]={themeM,themeDM,NULL,envM,shM,noiseM};
			dc->PSSetShaderResources(0,6,srvs);
			drawParts(m_view.m_vbTerr, m_view.m_vbTerrN, m_view.m_vbTerrParts, 3);
		}
		dc->PSSetShader(m_view.m_psSolid,NULL,0);
		{
			ID3D11ShaderResourceView* scenT1=m_view.m_srvWood?m_view.m_srvWood:obsM;
			ID3D11ShaderResourceView* srvs[]={themeM,scenT1,NULL,envM,shM,noiseM};
			dc->PSSetShaderResources(0,6,srvs);
			if (m_view.m_vbSceneryParts > 0)
				drawParts(m_view.m_vbScenery, m_view.m_vbSceneryN, m_view.m_vbSceneryParts, 3);
		}
		{
			ID3D11ShaderResourceView* osrvs[]={themeM,obsM,NULL,envM,shM,noiseM};
			dc->PSSetShaderResources(0,6,osrvs);
		}
		drawObsGpu();
		if (nWorld > nTerr) {
			dc->RSSetState(m_view.m_rsNoCull); dc->OMSetDepthStencilState(m_view.m_dssWrite,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
			dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->VSSetShader(m_view.m_vsSolid,NULL,0); dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0);
			dc->PSSetShader(m_view.m_psSolid,NULL,0);
			ID3D11ShaderResourceView* srvs[]={themeM,obsM,NULL,envM,shM,noiseM};
			dc->PSSetShaderResources(0,6,srvs);
			bindMesh(m_view.m_vbDyn);
			dc->Draw(nWorld - nTerr, nBand + nTerr);
		}
		if (craftDrawN) {
			dc->RSSetState(m_view.m_rsSolid);
			dc->PSSetShader(m_view.m_psCraft ? m_view.m_psCraft : m_view.m_psSolid,NULL,0);
			ID3D11ShaderResourceView* craftSrv = m_view.m_srvCraft ? m_view.m_srvCraft : themeM;
			ID3D11ShaderResourceView* craftDet = m_view.m_srvCraftD ? m_view.m_srvCraftD : craftSrv;
			ID3D11ShaderResourceView* srvs[]={craftSrv,craftDet,NULL,envM,shM,noiseM};
			dc->PSSetShaderResources(0,6,srvs);
			drawCraftGpu();
		}
		dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0);
		dc->PSSetShader((m_view.m_psWater && m_view.m_srvReflect) ? m_view.m_psWater : m_view.m_psSolid,NULL,0);
		dc->RSSetState(m_view.m_rsNoCull);
		dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff); dc->OMSetDepthStencilState(m_view.m_dssRead,0);
		{
			ID3D11ShaderResourceView* srvs[]={waterM,themeDM,NULL,envM,shM,noiseM};
			dc->PSSetShaderResources(0,6,srvs);
			if (m_view.m_srvReflect) dc->PSSetShaderResources(6,1,&m_view.m_srvReflect);
		}
		if (m_view.m_vbWaterParts > 0)
			drawParts(m_view.m_vbWater, m_view.m_vbWaterN, m_view.m_vbWaterParts, 3);
		{
			ID3D11ShaderResourceView* nullR=NULL; dc->PSSetShaderResources(6,1,&nullR);
		}
		if (m_view.m_vbBandParts > 0) {
			dc->RSSetState(m_view.m_rsNoCull); dc->OMSetDepthStencilState(m_view.m_dssRead,0); dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff);
			dc->IASetInputLayout(m_view.m_ilPatch); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
			dc->VSSetShader(m_view.m_vsTess,NULL,0); dc->HSSetShader(m_view.m_hsTess,NULL,0); dc->DSSetShader(m_view.m_dsTess,NULL,0); dc->PSSetShader(m_view.m_psBand,NULL,0);
			ID3D11ShaderResourceView* srvs[]={bandM,themeM,NULL,envM,shM,noiseM};
			dc->PSSetShaderResources(0,6,srvs); dc->DSSetShaderResources(5,1,&noiseM); dc->DSSetShaderResources(0,1,&bandM);
			drawParts(m_view.m_vbBand, m_view.m_vbBandN, m_view.m_vbBandParts, 4);
			dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0);
		}
		dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0);
		dc->RSSetState(m_view.m_rsNoCull);
		dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff); dc->OMSetDepthStencilState(m_view.m_dssRead,0);
		if (nSky || nSky2 || nSky3 || nSky4 || nItem) {
			bindMesh(m_view.m_vbDyn);
			const UINT t0 = nBand+nWorld+nCraft;
			ID3D11ShaderResourceView* s1=m_view.m_srvSky, *s2=m_view.m_srvSky2?m_view.m_srvSky2:s1;
			ID3D11ShaderResourceView* s3=m_view.m_srvSky3?m_view.m_srvSky3:s1;
			ID3D11ShaderResourceView* s4=m_view.m_srvSky4?m_view.m_srvSky4:s2;
			ID3D11PixelShader* cloudPs = m_view.m_psCloud ? m_view.m_psCloud : m_view.m_psSolid;
			auto bindT=[&](ID3D11ShaderResourceView* a, ID3D11ShaderResourceView* b){
				ID3D11ShaderResourceView* srvs[]={a?a:themeM, b?b:(a?a:themeDM), NULL, envM, shM, noiseM};
				dc->PSSetShaderResources(0,6,srvs);
			};
			dc->PSSetShader(cloudPs,NULL,0);
			if(nSky){ bindT(s1,s2); dc->Draw(nSky, t0); }
			if(nSky2){ bindT(s2,s1); dc->Draw(nSky2, t0+nSky); }
			if(nSky3){ bindT(s3,s4); dc->Draw(nSky3, t0+nSky+nSky2); }
			if(nSky4){ bindT(s4,s3); dc->Draw(nSky4, t0+nSky+nSky2+nSky3); }
			if(nItem){ dc->PSSetShader(m_view.m_psSolid,NULL,0); bindT(m_view.m_srvItem, m_view.m_srvItem); dc->Draw(nItem, t0+nSky+nSky2+nSky3+nSky4); }
		}
		ID3D11ShaderResourceView* nullsM[6]={}; dc->PSSetShaderResources(0,6,nullsM);
	}

	// color pass
	D3D11_VIEWPORT vp={0,0,(float)w,(float)h,0,1}; dc->RSSetViewports(1,&vp);
	dc->OMSetRenderTargets(1,&m_view.m_sceneRtv,m_view.m_dsv);
	dc->ClearRenderTargetView(m_view.m_sceneRtv, clearC);
	dc->ClearDepthStencilView(m_view.m_dsv, D3D11_CLEAR_DEPTH, 1, 0);
	cb.lightDir.w = 1.f;
	if (SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){ memcpy(map.pData,&cb,sizeof(cb)); dc->Unmap(m_view.m_cbFrame,0); }

	ID3D11Buffer* cbs[]={m_view.m_cbFrame};
	ID3D11SamplerState* smp[]={m_view.m_sampLin,m_view.m_sampPoint,m_view.m_sampCmp};
	dc->VSSetConstantBuffers(0,1,cbs); dc->PSSetConstantBuffers(0,1,cbs); dc->HSSetConstantBuffers(0,1,cbs); dc->DSSetConstantBuffers(0,1,cbs);
	dc->PSSetSamplers(0,3,smp); dc->DSSetSamplers(0,1,&m_view.m_sampLin);
	ID3D11ShaderResourceView* bandSrv=m_view.m_srvBand;
	ID3D11ShaderResourceView* noiseSrv=m_view.m_srvNoise;
	ID3D11ShaderResourceView* envSrv=m_view.m_srvEnv;
	if(m_view.m_srvEnv2 && (m_themeActive==THEME_CLOUD||m_themeActive==THEME_GRASS||m_themeActive==THEME_FOREST||m_themeActive==THEME_MESA||m_themeActive==THEME_UNDER))
		envSrv=m_view.m_srvEnv2;
	ID3D11ShaderResourceView* shSrv=m_view.m_shadowSrv;
	int thIdx=m_themeActive-1; if(thIdx<0)thIdx=0; if(thIdx>7)thIdx=7; ID3D11ShaderResourceView* themeSrv=m_view.m_srvTheme[thIdx];
	ID3D11ShaderResourceView* themeDet=m_view.m_srvThemeD[thIdx]?m_view.m_srvThemeD[thIdx]:themeSrv;
	ID3D11ShaderResourceView* obsSrv=m_view.m_srvObs?m_view.m_srvObs:themeDet;
	ID3D11ShaderResourceView* obsDet=m_view.m_srvObsD?m_view.m_srvObsD:obsSrv;
	ID3D11ShaderResourceView* waterSrv=m_view.m_srvWater?m_view.m_srvWater:themeSrv;
	ID3D11ShaderResourceView* waterDet=m_view.m_srvWaterD?m_view.m_srvWaterD:themeDet;

	UINT stride=sizeof(S3RVertex), off=0; dc->IASetVertexBuffers(0,1,&m_view.m_vbDyn,&stride,&off);
	dc->RSSetState(m_view.m_rsNoCull); dc->OMSetDepthStencilState(m_view.m_dssWrite,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
	dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	dc->VSSetShader(m_view.m_vsSolid,NULL,0); dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0);
	if (m_view.m_vbTerrParts > 0){
		dc->PSSetShader(m_view.m_psTerr,NULL,0);
		ID3D11ShaderResourceView* srvs[]={themeSrv,themeDet,NULL,envSrv,shSrv,noiseSrv};
		dc->PSSetShaderResources(0,6,srvs);
		drawParts(m_view.m_vbTerr, m_view.m_vbTerrN, m_view.m_vbTerrParts, 3);
	}
	{
		dc->PSSetShader(m_view.m_psSolid,NULL,0);
		ID3D11ShaderResourceView* scenT1=m_view.m_srvWood?m_view.m_srvWood:obsSrv;
		ID3D11ShaderResourceView* srvs[]={themeSrv,scenT1,NULL,envSrv,shSrv,noiseSrv};
		dc->PSSetShaderResources(0,6,srvs);
		if (m_view.m_vbSceneryParts > 0) {
			drawParts(m_view.m_vbScenery, m_view.m_vbSceneryN, m_view.m_vbSceneryParts, 3);
		}
		{
			ID3D11ShaderResourceView* osrvs[]={obsSrv,obsDet,NULL,envSrv,shSrv,noiseSrv};
			dc->PSSetShaderResources(0,6,osrvs);
		}
		drawObsGpu();
	}
	if (nWorld > nTerr){
		dc->RSSetState(m_view.m_rsNoCull); dc->OMSetDepthStencilState(m_view.m_dssWrite,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
		dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0); dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0); dc->PSSetShader(m_view.m_psSolid,NULL,0);
		ID3D11ShaderResourceView* srvs[]={themeSrv,obsSrv,NULL,envSrv,shSrv,noiseSrv};
		dc->PSSetShaderResources(0,6,srvs);
		bindMesh(m_view.m_vbDyn);
		dc->Draw(nWorld - nTerr, nBand + nTerr);
	}
	if (craftDrawN){
		dc->RSSetState(m_view.m_rsSolid); dc->OMSetDepthStencilState(m_view.m_dssWrite,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0);
		dc->PSSetShader(m_view.m_psCraft ? m_view.m_psCraft : m_view.m_psSolid,NULL,0);
		ID3D11ShaderResourceView* craftSrv = m_view.m_srvCraft ? m_view.m_srvCraft : themeSrv;
		ID3D11ShaderResourceView* craftDet = m_view.m_srvCraftD ? m_view.m_srvCraftD : craftSrv;
		ID3D11ShaderResourceView* srvs[]={craftSrv,craftDet,NULL,envSrv,shSrv,noiseSrv};
		dc->PSSetShaderResources(0,6,srvs);
		drawCraftGpu();
	}
	if (hq && (nWorld || m_view.m_vbTerrParts || m_view.m_vbSceneryParts) && cb.peel.w > 0.05f) {
		S3RFrameCB cbPeel = cb;
		cbPeel.peel.w = -cb.peel.w;
		if (SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){ memcpy(map.pData,&cbPeel,sizeof(cbPeel)); dc->Unmap(m_view.m_cbFrame,0); }
		dc->RSSetState(m_view.m_rsNoCull);
		dc->OMSetDepthStencilState(m_view.m_dssRead,0);
		dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff);
		dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsSolid,NULL,0); dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0);
		if (m_view.m_vbTerrParts > 0) {
			dc->PSSetShader(m_view.m_psTerr,NULL,0);
			ID3D11ShaderResourceView* srvs[]={themeSrv,themeDet,NULL,envSrv,shSrv,noiseSrv};
			dc->PSSetShaderResources(0,6,srvs);
			drawParts(m_view.m_vbTerr, m_view.m_vbTerrN, m_view.m_vbTerrParts, 3);
		}
		dc->PSSetShader(m_view.m_psSolid,NULL,0);
		{
			ID3D11ShaderResourceView* srvs[]={themeSrv,obsSrv,NULL,envSrv,shSrv,noiseSrv};
			dc->PSSetShaderResources(0,6,srvs);
		}
		if (m_view.m_vbSceneryParts > 0) {
			drawParts(m_view.m_vbScenery, m_view.m_vbSceneryN, m_view.m_vbSceneryParts, 3);
		}
		drawObsGpu();
		if (nWorld > nTerr) {
			bindMesh(m_view.m_vbDyn);
			dc->Draw(nWorld - nTerr, nBand + nTerr);
		}
		if (SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))){ memcpy(map.pData,&cb,sizeof(cb)); dc->Unmap(m_view.m_cbFrame,0); }
	}
	dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	dc->VSSetShader(m_view.m_vsSolid,NULL,0);
	dc->PSSetShader((m_view.m_psWater && m_view.m_srvReflect) ? m_view.m_psWater : m_view.m_psSolid,NULL,0);
	dc->RSSetState(m_view.m_rsNoCull);
	dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff); dc->OMSetDepthStencilState(m_view.m_dssRead,0);
	{
		ID3D11ShaderResourceView* srvs[]={waterSrv,waterDet,NULL,envSrv,shSrv,noiseSrv};
		dc->PSSetShaderResources(0,6,srvs);
		if (m_view.m_srvReflect) dc->PSSetShaderResources(6,1,&m_view.m_srvReflect);
	}
	if (m_view.m_vbWaterParts > 0) {
		drawParts(m_view.m_vbWater, m_view.m_vbWaterN, m_view.m_vbWaterParts, 3);
	}
	{
		ID3D11ShaderResourceView* nullR = NULL;
		dc->PSSetShaderResources(6,1,&nullR);
		dc->PSSetShader(m_view.m_psSolid,NULL,0);
	}
	{
		if (m_view.m_vbBandParts > 0){
			dc->RSSetState(m_view.m_rsNoCull); dc->OMSetDepthStencilState(m_view.m_dssRead,0); dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff);
			dc->IASetInputLayout(m_view.m_ilPatch); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
			dc->VSSetShader(m_view.m_vsTess,NULL,0); dc->HSSetShader(m_view.m_hsTess,NULL,0); dc->DSSetShader(m_view.m_dsTess,NULL,0); dc->PSSetShader(m_view.m_psBand,NULL,0);
			ID3D11ShaderResourceView* srvs[]={bandSrv,themeSrv,NULL,envSrv,shSrv,noiseSrv};
			dc->PSSetShaderResources(0,6,srvs); dc->DSSetShaderResources(5,1,&noiseSrv); dc->DSSetShaderResources(0,1,&bandSrv);
			drawParts(m_view.m_vbBand, m_view.m_vbBandN, m_view.m_vbBandParts, 4);
			dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0);
		}
	}
	dc->IASetInputLayout(m_view.m_ilSolid); dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	dc->VSSetShader(m_view.m_vsSolid,NULL,0);
	dc->HSSetShader(NULL,NULL,0); dc->DSSetShader(NULL,NULL,0);
	if (nTrans){
		bindMesh(m_view.m_vbDyn);
		const UINT t0 = nBand+nWorld+nCraft;
		ID3D11ShaderResourceView* s1=m_view.m_srvSky, *s2=m_view.m_srvSky2?m_view.m_srvSky2:s1;
		ID3D11ShaderResourceView* s3=m_view.m_srvSky3?m_view.m_srvSky3:s1;
		ID3D11ShaderResourceView* s4=m_view.m_srvSky4?m_view.m_srvSky4:s2;
		ID3D11PixelShader* cloudPs = m_view.m_psCloud ? m_view.m_psCloud : m_view.m_psSolid;
		auto bindT=[&](ID3D11ShaderResourceView* a, ID3D11ShaderResourceView* b){
			ID3D11ShaderResourceView* srvs[]={a?a:themeSrv, b?b:(a?a:themeDet), NULL, envSrv, shSrv, noiseSrv};
			dc->PSSetShaderResources(0,6,srvs);
		};
		dc->PSSetShader(cloudPs,NULL,0);
		if(nSky){ bindT(s1,s2); dc->Draw(nSky, t0); }
		if(nSky2){ bindT(s2,s1); dc->Draw(nSky2, t0+nSky); }
		if(nSky3){ bindT(s3,s4); dc->Draw(nSky3, t0+nSky+nSky2); }
		if(nSky4){ bindT(s4,s3); dc->Draw(nSky4, t0+nSky+nSky2+nSky3); }
		dc->PSSetShader(m_view.m_psSolid,NULL,0);
		if(nItem){ bindT(m_view.m_srvItem, m_view.m_srvItem); dc->Draw(nItem, t0+nSky+nSky2+nSky3+nSky4); }
		const UINT nRest = nTrans - nSky - nSky2 - nSky3 - nSky4 - nItem - nShaft;
		if(nRest){ bindT(themeSrv, themeDet); dc->Draw(nRest, t0+nSky+nSky2+nSky3+nSky4+nItem); }
		if(nShaft){
			dc->OMSetBlendState(m_view.m_bsAdd,NULL,0xffffffff);
			bindT(themeSrv, themeDet);
			dc->Draw(nShaft, t0+nSky+nSky2+nSky3+nSky4+nItem+nRest);
			dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff);
		}
	}

	// post: デモはDOF省略、ゲーム中はDOF→FIN（god rayはFIN側）
	ID3D11ShaderResourceView* nulls[6]={}; dc->PSSetShaderResources(0,6,nulls);
	dc->OMSetDepthStencilState(m_view.m_dssOff,0); dc->OMSetBlendState(m_view.m_bsOpaque,NULL,0xffffffff);
	dc->VSSetShader(m_view.m_vsPost,NULL,0);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	if (hq) {
		dc->OMSetRenderTargets(1,&m_view.m_postRtv,NULL);
		dc->PSSetShader(m_view.m_psDof,NULL,0);
		ID3D11ShaderResourceView* p1[]={m_view.m_sceneSrv,NULL,m_view.m_dsSrv}; dc->PSSetShaderResources(0,3,p1);
		dc->Draw(3,0);
		dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL); dc->PSSetShaderResources(0,6,nulls);
		dc->PSSetShader(m_view.m_psFinal,NULL,0);
		ID3D11ShaderResourceView* p2[]={m_view.m_postSrv}; dc->PSSetShaderResources(0,1,p2);
		dc->Draw(3,0);
	} else {
		dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL);
		dc->PSSetShader(m_view.m_psFinal,NULL,0);
		ID3D11ShaderResourceView* p2[]={m_view.m_sceneSrv}; dc->PSSetShaderResources(0,1,p2);
		dc->Draw(3,0);
	}

	// HUD quads + baked countdown / status textures
	{
		UINT maxH = m_view.m_vbHudBytes / sizeof(S3RHudVertex);
		if (m_view.m_cpuHudScratchBytes < m_view.m_vbHudBytes) {
			delete[] m_view.m_cpuHudScratch;
			m_view.m_cpuHudScratch = new (std::nothrow) BYTE[m_view.m_vbHudBytes];
			m_view.m_cpuHudScratchBytes = m_view.m_cpuHudScratch ? m_view.m_vbHudBytes : 0;
		}
		S3RHudVertex* hv = m_view.m_cpuHudScratch ? (S3RHudVertex*)m_view.m_cpuHudScratch : NULL;
		UINT nh=0;
		auto hq=[&](float x0,float y0,float x1,float y1,float r,float g,float b,float a){
			if (!hv || nh+6>maxH) return;
			const float u=-1.f,v=-1.f;
			hv[nh++]={x0,y0,u,v,r,g,b,a}; hv[nh++]={x1,y0,u,v,r,g,b,a}; hv[nh++]={x1,y1,u,v,r,g,b,a};
			hv[nh++]={x0,y0,u,v,r,g,b,a}; hv[nh++]={x1,y1,u,v,r,g,b,a}; hv[nh++]={x0,y1,u,v,r,g,b,a};
		};
		auto ht=[&](float x0,float y0,float x1,float y1,float a){
			if (!hv || nh+6>maxH) return;
			// y0<y1: y1=画面上。GDI テクスチャ v=0 が上端なので UV を合わせる（反転表示の修正）
			hv[nh++]={x0,y1,0,0,1,1,1,a}; hv[nh++]={x1,y1,1,0,1,1,1,a}; hv[nh++]={x1,y0,1,1,1,1,1,a};
			hv[nh++]={x0,y1,0,0,1,1,1,a}; hv[nh++]={x1,y0,1,1,1,1,1,a}; hv[nh++]={x0,y0,0,1,1,1,1,a};
		};
		// minimap（右上）＋順位パネル（その下）
		if (savedata.s3r_show_map) {
			float mx0=0.62f, my0=0.62f, mx1=0.96f, my1=0.94f;
			hq(mx0,my0,mx1,my1, 0.05f,0.07f,0.12f,0.55f);
			const float s=1.f/220.f;
			for (int i=0;i<S3R_PATH_SAMPLES-1;i+=4){
				float ax=m_pathSampleXYZ[i][0], az=m_pathSampleXYZ[i][2];
				float x0=mx0+(mx1-mx0)*(ax*s*0.5f+0.5f), y0=my0+(my1-my0)*(az*s*0.5f+0.5f);
				hq(x0-.003f,y0-.003f,x0+.003f,y0+.003f, .5f,.85f,1.f,.9f);
			}
			for (int i=0;i<m_craftN;i++){
				float x=mx0+(mx1-mx0)*(m_crafts[i].x*s*0.5f+0.5f);
				float y=my0+(my1-my0)*(m_crafts[i].z*s*0.5f+0.5f);
				float r=kCraftColors[m_crafts[i].colorIdx][0], g=kCraftColors[m_crafts[i].colorIdx][1], b=kCraftColors[m_crafts[i].colorIdx][2];
				float rad=m_crafts[i].isPlayer?0.012f:0.008f;
				hq(x-rad,y-rad,x+rad,y+rad,r,g,b,1);
			}
		}
		// HP / 推進力バー（中央レーン中は HP バーを緑寄り）
		{
			float lat=0,vert=0,cx,cy,cz;
			BandLocal(pl.x,pl.y,pl.z,pl.pathT,lat,vert,cx,cy,cz);
			float half=BandHalfWidth();
			const int healing = (fabsf(lat)<=half*0.42f && fabsf(vert)<=half*0.38f && m_phase==PHASE_RACE) ? 1 : 0;
			if (healing)
				hq(-0.95f,0.88f,-0.95f+0.5f*(pl.hp/100.f),0.94f, .35f,1.f,.55f,.95f);
			else
				hq(-0.95f,0.88f,-0.95f+0.5f*(pl.hp/100.f),0.94f, 1.f,.35f,.45f,.85f);
		}
		// 推進力（帯外で減り、0でコースアウト復帰）
		{
			float fr = pl.fuel / 100.f;
			float r = (fr < 0.25f) ? 1.f : 0.35f;
			float g = (fr < 0.25f) ? 0.45f : 0.85f;
			float b = (fr < 0.25f) ? 0.25f : 1.f;
			hq(-0.95f,0.80f,-0.95f+0.5f*fr,0.86f, r,g,b,.9f);
		}
		if (pl.hitFlashT > 0.f && (m_phase == PHASE_RACE || m_phase == PHASE_FINISH)) {
			float a = S3rSaturate(pl.hitFlashT / 0.38f) * 0.40f;
			hq(-1.f, -1.f, 1.f, -0.68f, 1.f, 0.10f, 0.06f, a);
			hq(-1.f, 0.68f, 1.f, 1.f, 1.f, 0.10f, 0.06f, a);
			hq(-1.f, -1.f, -0.74f, 1.f, 1.f, 0.10f, 0.06f, a);
			hq(0.74f, -1.f, 1.f, 1.f, 1.f, 0.10f, 0.06f, a);
		}

		dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL);
		// 直前の solid パスが CULL_BACK のままだと HUD 四角が消える
		dc->RSSetState(m_view.m_rsNoCull);
		dc->OMSetBlendState(m_view.m_bsAlpha,NULL,0xffffffff); dc->OMSetDepthStencilState(m_view.m_dssOff,0);
		dc->VSSetShader(m_view.m_vsHud,NULL,0); dc->PSSetShader(m_view.m_psHud,NULL,0); dc->IASetInputLayout(m_view.m_ilHud);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ID3D11SamplerState* hsmp[]={m_view.m_sampLin,m_view.m_sampPoint,m_view.m_sampCmp};
		dc->PSSetSamplers(0,3,hsmp);

		auto flushHud=[&](ID3D11ShaderResourceView* srv){
			if (!nh || !hv) return;
			if (FAILED(dc->Map(m_view.m_vbHud,0,D3D11_MAP_WRITE_DISCARD,0,&map))) return;
			memcpy(map.pData,hv,nh*sizeof(S3RHudVertex)); dc->Unmap(m_view.m_vbHud,0);
			ID3D11ShaderResourceView* srvs[]={srv?srv:m_view.m_srvBand,NULL,NULL,NULL,NULL,NULL};
			dc->PSSetShaderResources(0,6,srvs);
			UINT hs=sizeof(S3RHudVertex), ho=0; dc->IASetVertexBuffers(0,1,&m_view.m_vbHud,&hs,&ho);
			dc->Draw(nh,0); nh=0;
		};
		flushHud(NULL);

		// バックミラー（上中央・ミニマップ左）。MMB後方視中は本体が後ろ向きなので隠す
		if (m_view.m_srvRear && !m_lookback
			&& (m_phase == PHASE_RACE || m_phase == PHASE_COUNTDOWN || m_phase == PHASE_FINISH || m_phase == PHASE_DEMO)) {
			const float bx0 = -0.18f, by0 = 0.78f, bx1 = 0.42f, by1 = 0.948f;
			hq(bx0 - 0.012f, by0 - 0.016f, bx1 + 0.012f, by1 + 0.016f, 0.06f, 0.07f, 0.10f, 0.88f);
			hq(bx0 - 0.004f, by0 - 0.006f, bx1 + 0.004f, by1 + 0.006f, 0.18f, 0.22f, 0.28f, 0.55f);
			flushHud(NULL);
			ht(bx0, by0, bx1, by1, 0.96f);
			flushHud(m_view.m_srvRear);
		}

		if (m_view.m_srvHud) {
			ht(-0.96f, 0.42f, -0.28f, 0.78f, 0.92f);
			flushHud(m_view.m_srvHud);
		}
		if (m_view.m_srvGauge && (m_phase == PHASE_RACE || m_phase == PHASE_COUNTDOWN || m_phase == PHASE_FINISH
			|| m_phase == PHASE_DEMO || m_phase == PHASE_PODIUM)) {
			ht(-0.985f, -0.975f, -0.50f, -0.575f, 0.97f);
			flushHud(m_view.m_srvGauge);
		}
		// ミニマップ下にマージン → 残り縦を最大12台で等分。実台数ぶんだけ上から積む
		if (m_view.m_srvStand && (m_phase == PHASE_RACE || m_phase == PHASE_COUNTDOWN || m_phase == PHASE_FINISH || m_phase == PHASE_PODIUM || m_phase == PHASE_DEMO)) {
			const float mapBot = 0.62f; // ミニマップ下端（描画と揃える）
			const float availTop = mapBot - 0.04f; // 少しマージン
			const float availBot = -0.96f;
			const float slotH = (availTop - availBot) / 12.f;
			const int n = max(1, min(m_craftN, 12));
			const float rowPart = (float)(n * 72 + 8);
			const float fullPart = rowPart + 52.f; // ニトロ帯
			float panelH = slotH * (float)n * (fullPart / max(1.f, rowPart));
			float sy1 = availTop;
			float sy0 = sy1 - panelH;
			if (sy0 < availBot) sy0 = availBot;
			ht(0.54f, sy0, 0.995f, sy1, 0.98f);
			flushHud(m_view.m_srvStand);
		}

		// 画面上の機体へ名前＋順位の吹き出し（D2D 文字 + PSLINE 下線）
		if (m_view.m_srvBubble && m_view.m_psHudLine && m_view.m_bubbleN > 0
			&& (m_phase == PHASE_RACE || m_phase == PHASE_COUNTDOWN || m_phase == PHASE_FINISH
				|| m_phase == PHASE_PODIUM || m_phase == PHASE_DEMO)) {
			struct BubDraw { int i; float nx, ny, w, dist; };
			BubDraw bd[S3R_MAX_CRAFT];
			int nBd = 0;
			for (int i = 0; i < m_craftN && i < m_view.m_bubbleN; i++) {
				S3rCraft& c = m_crafts[i];
				float nx, ny, cw;
				if (!S3rWorldToNdc(cb.viewProj, c.x, c.y + 2.15f, c.z, nx, ny, cw)) continue;
				if (nx < -1.05f || nx > 1.05f || ny < -1.02f || ny > 1.02f) continue;
				bd[nBd++] = { i, nx, ny, cw, cw };
			}
			for (int a = 1; a < nBd; a++) {
				BubDraw q = bd[a]; int j = a - 1;
				while (j >= 0 && bd[j].dist < q.dist) { bd[j + 1] = bd[j]; j--; }
				bd[j + 1] = q;
			}
			auto htuv = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float a) {
				if (!hv || nh + 6 > maxH) return;
				hv[nh++] = { x0, y1, u0, v0, 1, 1, 1, a };
				hv[nh++] = { x1, y1, u1, v0, 1, 1, 1, a };
				hv[nh++] = { x1, y0, u1, v1, 1, 1, 1, a };
				hv[nh++] = { x0, y1, u0, v0, 1, 1, 1, a };
				hv[nh++] = { x1, y0, u1, v1, 1, 1, 1, a };
				hv[nh++] = { x0, y0, u0, v1, 1, 1, 1, a };
			};
			auto hline = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
				if (!hv || nh + 6 > maxH) return;
				hv[nh++] = { x0, y0, 0, 0, r, g, b, a };
				hv[nh++] = { x1, y0, 1, 0, r, g, b, a };
				hv[nh++] = { x1, y1, 1, 1, r, g, b, a };
				hv[nh++] = { x0, y0, 0, 0, r, g, b, a };
				hv[nh++] = { x1, y1, 1, 1, r, g, b, a };
				hv[nh++] = { x0, y1, 0, 1, r, g, b, a };
			};
			const UINT lineBeg = nh;
			for (int k = 0; k < nBd; k++) {
				const int i = bd[k].i;
				S3rCraft& c = m_crafts[i];
				const float sc = S3rClamp(1.35f / max(bd[k].w, 0.45f), 0.62f, 1.55f);
				const float bw = 0.40f * sc;
				const float bh = 0.092f * sc;
				const float x0 = bd[k].nx - bw * 0.5f;
				const float x1 = bd[k].nx + bw * 0.5f;
				const float y0 = bd[k].ny + 0.028f * sc;
				const float y1 = y0 + bh;
				float r = kCraftColors[c.colorIdx][0], g = kCraftColors[c.colorIdx][1], b = kCraftColors[c.colorIdx][2];
				if (c.isPlayer) { r = 1.f; g = 0.92f; b = 0.45f; }
				const float ly1 = y0 - 0.004f * sc;
				const float ly0 = ly1 - 0.011f * sc;
				hline(x0 + 0.01f * sc, ly0, x1 - 0.01f * sc, ly1, r, g, b, 0.95f);
				const float stemW = 0.0045f * sc;
				hline(bd[k].nx - stemW, bd[k].ny + 0.004f, bd[k].nx + stemW, ly0, r, g, b, 0.82f);
			}
			if (nh > lineBeg) {
				dc->PSSetShader(m_view.m_psHudLine, NULL, 0);
				flushHud(NULL);
				dc->PSSetShader(m_view.m_psHud, NULL, 0);
			}
			for (int k = 0; k < nBd; k++) {
				const int i = bd[k].i;
				S3rCraft& c = m_crafts[i];
				const float sc = S3rClamp(1.35f / max(bd[k].w, 0.45f), 0.62f, 1.55f);
				const float bw = 0.40f * sc;
				const float bh = 0.092f * sc;
				const float x0 = bd[k].nx - bw * 0.5f;
				const float x1 = bd[k].nx + bw * 0.5f;
				const float y0 = bd[k].ny + 0.028f * sc;
				const float y1 = y0 + bh;
				const float a = c.isPlayer ? 1.f : 0.94f;
				htuv(x0, y0, x1, y1, m_view.m_bubbleU0[i], m_view.m_bubbleV0[i], m_view.m_bubbleU1[i], m_view.m_bubbleV1[i], a);
			}
			flushHud(m_view.m_srvBubble);
		}

		// アイテム球：機体吹き出しより小さく薄いラベル
		if (m_view.m_psHudLine
			&& (m_phase == PHASE_RACE || m_phase == PHASE_COUNTDOWN || m_phase == PHASE_FINISH
				|| m_phase == PHASE_DEMO)) {
			if (!m_view.m_srvItemLab) {
				const wchar_t* labs[CS3rView::S3R_ITEMLAB_MAX] = {
					LL14(L"テンポ↑", L"Tempo↑", L"Tempo↑", L"Tempo↑", L"Tempo↑", L"템포↑", L"速度↑", L"Tempo↑", L"Темп↑", L"Tempo↑", L"Tempo↑", L"Tempo↑", L"Tempo↑", L"Tempo↑"),
					LL14(L"テンポ↓", L"Tempo↓", L"Tempo↓", L"Tempo↓", L"Tempo↓", L"템포↓", L"速度↓", L"Tempo↓", L"Темп↓", L"Tempo↓", L"Tempo↓", L"Tempo↓", L"Tempo↓", L"Tempo↓"),
					LL14(L"ピッチ↑", L"Pitch↑", L"Hauteur↑", L"Pitch↑", L"Tono↑", L"피치↑", L"音高↑", L"Pitch↑", L"Высота↑", L"Ton↑", L"Tom↑", L"Toon↑", L"Wys.↑", L"Perde↑"),
					LL14(L"ピッチ↓", L"Pitch↓", L"Hauteur↓", L"Pitch↓", L"Tono↓", L"피치↓", L"音高↓", L"Pitch↓", L"Высота↓", L"Ton↓", L"Tom↓", L"Toon↓", L"Wys.↓", L"Perde↓"),
					LL14(L"次の曲", L"Next", L"Suivant", L"Succ.", L"Siguiente", L"다음", L"下一曲", L"Next", L"След.", L"Nächster", L"Próxima", L"Volgend", L"Nast.", L"Sonraki"),
					LL14(L"前の曲", L"Prev", L"Préc.", L"Prec.", L"Anterior", L"이전", L"上一曲", L"Prev", L"Пред.", L"Vorher", L"Anterior", L"Vorig", L"Poprz.", L"Önceki"),
					LL14(L"音量↑", L"Vol↑", L"Vol↑", L"Vol↑", L"Vol↑", L"볼륨↑", L"音量↑", L"Vol↑", L"Громк.↑", L"Laut↑", L"Vol↑", L"Vol↑", L"Głoś↑", L"Ses↑"),
					LL14(L"音量↓", L"Vol↓", L"Vol↓", L"Vol↓", L"Vol↓", L"볼륨↓", L"音量↓", L"Vol↓", L"Громк.↓", L"Laut↓", L"Vol↓", L"Vol↓", L"Głoś↓", L"Ses↓"),
					LL14(L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ"),
					LL14(L"EQ平坦", L"EQ flat", L"EQ plat", L"EQ flat", L"EQ plano", L"EQ평탄", L"EQ平坦", L"EQ flat", L"EQ flat", L"EQ flach", L"EQ flat", L"EQ vlak", L"EQ płaski", L"EQ düz"),
					LL14(L"リバーブ", L"Reverb", L"Réverb", L"Reverb", L"Reverb", L"리버브", L"混响", L"Reverb", L"Реверб", L"Hall", L"Reverb", L"Reverb", L"Pogłos", L"Reverb"),
					LL14(L"クロスフェード", L"Crossfade", L"Fondu", L"Crossfade", L"Fundido", L"크로스페이드", L"交叉淡化", L"Xfade", L"Кроссфейд", L"Crossfade", L"Crossfade", L"Crossfade", L"Xfade", L"Xfade"),
					LL14(L"ランダム", L"Random", L"Aléatoire", L"Casuale", L"Aleatorio", L"랜덤", L"随机", L"Random", L"Случ.", L"Zufall", L"Aleatório", L"Willekeurig", L"Losowo", L"Rastgele")
				};
				m_view.BakeItemLabTexture(labs, CS3rView::S3R_ITEMLAB_MAX);
			}
			if (m_view.m_srvItemLab && m_view.m_itemLabN > 0) {
				struct ItDraw { int slot, kind; float nx, ny, w; };
				ItDraw idr[S3R_MAX_ITEMS];
				int nId = 0;
				for (int i = 0; i < m_itemN && nId < S3R_MAX_ITEMS; i++) {
					S3rItem& it = m_items[i];
					if (it.taken) continue;
					int slot = S3rItemLabSlot(it.kind);
					if (slot < 0 || slot >= m_view.m_itemLabN) continue;
					float nx, ny, cw;
					if (!S3rWorldToNdc(cb.viewProj, it.x, it.y + 0.95f, it.z, nx, ny, cw)) continue;
					if (nx < -1.05f || nx > 1.05f || ny < -1.02f || ny > 1.02f) continue;
					idr[nId++] = { slot, it.kind, nx, ny, cw };
				}
				for (int a = 1; a < nId; a++) {
					ItDraw q = idr[a]; int j = a - 1;
					while (j >= 0 && idr[j].w < q.w) { idr[j + 1] = idr[j]; j--; }
					idr[j + 1] = q;
				}
				auto htuvI = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float a) {
					if (!hv || nh + 6 > maxH) return;
					hv[nh++] = { x0, y1, u0, v0, 1, 1, 1, a };
					hv[nh++] = { x1, y1, u1, v0, 1, 1, 1, a };
					hv[nh++] = { x1, y0, u1, v1, 1, 1, 1, a };
					hv[nh++] = { x0, y1, u0, v0, 1, 1, 1, a };
					hv[nh++] = { x1, y0, u1, v1, 1, 1, 1, a };
					hv[nh++] = { x0, y0, u0, v1, 1, 1, 1, a };
				};
				auto hlineI = [&](float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
					if (!hv || nh + 6 > maxH) return;
					hv[nh++] = { x0, y0, 0, 0, r, g, b, a };
					hv[nh++] = { x1, y0, 1, 0, r, g, b, a };
					hv[nh++] = { x1, y1, 1, 1, r, g, b, a };
					hv[nh++] = { x0, y0, 0, 0, r, g, b, a };
					hv[nh++] = { x1, y1, 1, 1, r, g, b, a };
					hv[nh++] = { x0, y1, 0, 1, r, g, b, a };
				};
				auto fadeW = [&](float cw)->float {
					if (cw <= 6.f) return 0.92f;
					if (cw >= 22.f) return 0.18f;
					return 0.92f - (cw - 6.f) / (22.f - 6.f) * 0.74f;
				};
				const UINT lineBegI = nh;
				for (int k = 0; k < nId; k++) {
					const float sc = S3rClamp(1.05f / max(idr[k].w, 0.55f), 0.48f, 1.12f) * 0.78f;
					const float aa = fadeW(idr[k].w);
					const float bw = 0.22f * sc, bh = 0.058f * sc;
					const float x0 = idr[k].nx - bw * 0.5f, x1 = idr[k].nx + bw * 0.5f;
					const float y0 = idr[k].ny + 0.018f * sc, y1 = y0 + bh;
					float r, g, b; S3rItemLabRgb(idr[k].kind, r, g, b);
					const float ly1 = y0 - 0.003f * sc, ly0 = ly1 - 0.008f * sc;
					hlineI(x0 + 0.008f * sc, ly0, x1 - 0.008f * sc, ly1, r, g, b, 0.88f * aa);
					const float stemW = 0.0032f * sc;
					hlineI(idr[k].nx - stemW, idr[k].ny + 0.003f, idr[k].nx + stemW, ly0, r, g, b, 0.72f * aa);
				}
				if (nh > lineBegI) {
					dc->PSSetShader(m_view.m_psHudLine, NULL, 0);
					flushHud(NULL);
					dc->PSSetShader(m_view.m_psHud, NULL, 0);
				}
				for (int k = 0; k < nId; k++) {
					const float sc = S3rClamp(1.05f / max(idr[k].w, 0.55f), 0.48f, 1.12f) * 0.78f;
					const float aa = fadeW(idr[k].w);
					const float bw = 0.22f * sc, bh = 0.058f * sc;
					const float x0 = idr[k].nx - bw * 0.5f, x1 = idr[k].nx + bw * 0.5f;
					const float y0 = idr[k].ny + 0.018f * sc, y1 = y0 + bh;
					const int s = idr[k].slot;
					htuvI(x0, y0, x1, y1, m_view.m_itemLabU0[s], m_view.m_itemLabV0[s], m_view.m_itemLabU1[s], m_view.m_itemLabV1[s], 0.90f * aa);
				}
				flushHud(m_view.m_srvItemLab);
			}
		}
	}

	// カウント／FINISH 等: 迷路 Clear! と同じフルスクリーン三角＋ビューポート経路
	if (m_view.m_srvClear && m_clearBakeA > 0.02f && !m_clearBakeText.IsEmpty()) {
		S3RFrameCB cbToast = cb;
		cbToast.misc.z = 99.f;
		if (SUCCEEDED(dc->Map(m_view.m_cbFrame,0,D3D11_MAP_WRITE_DISCARD,0,&map))) {
			memcpy(map.pData,&cbToast,sizeof(cbToast)); dc->Unmap(m_view.m_cbFrame,0);
		}
		D3D11_VIEWPORT tvp={0,(float)h*.34f,(float)w,(float)h*.28f,0,1};
		dc->RSSetViewports(1,&tvp);
		dc->OMSetRenderTargets(1,&m_view.m_bbRtv,NULL);
		dc->RSSetState(m_view.m_rsNoCull);
		dc->OMSetBlendState(m_view.m_bsAlpha,NULL,~0u);
		dc->OMSetDepthStencilState(m_view.m_dssOff,0);
		dc->IASetInputLayout(NULL);
		dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		dc->VSSetShader(m_view.m_vsPost,NULL,0);
		dc->PSSetShader(m_view.m_psFinal,NULL,0);
		dc->PSSetConstantBuffers(0,1,&m_view.m_cbFrame);
		dc->PSSetSamplers(0,1,&m_view.m_sampLin);
		dc->PSSetShaderResources(0,1,&m_view.m_srvClear);
		dc->Draw(3,0);
		dc->PSSetShaderResources(0,1,nulls);
		dc->RSSetViewports(1,&vp);
	}

	m_view.PresentFrame();
}

void CSoft3DRaceDlg::ShowHelpSheet()
{
	if (g_s3rHelp && g_s3rHelp->GetSafeHwnd()) { g_s3rHelp->SetForegroundWindow(); return; }
	g_s3rHelp = new CS3rHelpDlg(this);
	if (!g_s3rHelp->Create(IDD_S3R_HELP, this)) { delete g_s3rHelp; g_s3rHelp = NULL; return; }
	CCC_PresentOwnedHelp(g_s3rHelp, this);
}

void CSoft3DRaceDlg::ShowContextMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	if (CCustomPopupMenu* courseSub = menu.AddSubMenu(LL14(L"コース", L"Course", L"Parcours", L"Percorso", L"Recorrido", L"코스", L"赛道", L"المسار", L"Трасса", L"Strecke", L"Percurso", L"Parcours", L"Tor", L"Parkur"))) {
		courseSub->AddCommand(1, LL14(L"リスタート", L"Restart", L"Redémarrer", L"Riavvia", L"Reiniciar", L"재시작", L"重启", L"إعادة", L"Рестарт", L"Neustart", L"Reiniciar", L"Herstarten", L"Restart", L"Yeniden"));
		courseSub->AddCommand(2, LL14(L"コース再生成", L"Regenerate course", L"Régénérer", L"Rigenera", L"Regenerar", L"코스 재생성", L"重新生成赛道", L"إعادة توليد", L"Новый курс", L"Strecke neu", L"Gerar de novo", L"Opnieuw genereren", L"Generuj tor", L"Parkuru yenile"));
	}
	menu.AddSeparator();
	
	if (CCustomPopupMenu* viewSub = menu.AddSubMenu(LL14(L"表示・視点", L"View", L"Vue", L"Vista", L"Vista", L"보기/시점", L"显示/视角", L"عرض", L"Вид", L"Ansicht", L"Vista", L"Weergave", L"Widok", L"Görünüm"))) {
		viewSub->AddCheck(20, LL14(L"ミニマップ表示", L"Show minimap", L"Afficher minimap", L"Mostra minimap", L"Mostrar minimapa", L"미니맵", L"显示小地图", L"خريطة", L"Мини-карта", L"Minimap", L"Minimapa", L"Minimapa", L"Minimapa", L"Minimapi"), savedata.s3r_show_map != 0);
		viewSub->AddCheck(56, LL14(L"PCM効果音（エンジン／カウント／LAP）", L"PCM SFX (engines / countdown / laps)", L"SFX PCM (moteurs / compte / tours)", L"SFX PCM (motori / conto / giri)", L"SFX PCM (motores / cuenta / vueltas)", L"PCM 효과음(엔진/카운트/랩)", L"PCM效果音（引擎／倒计时／计圈）", L"PCM SFX (engines / countdown)", L"PCM-эффекты (двигатели/отсчёт)", L"PCM-SFX (Motoren / Countdown)", L"SFX PCM (motores / contagem)", L"PCM-SFX (motoren / aftellen)", L"SFX PCM (silniki / odliczanie)", L"PCM SFX (motor / geri sayım)"), savedata.s3_pcm_sfx != 0);
		viewSub->AddCommand(54, LL14(L"ズームをリセット", L"Reset zoom", L"Réinit. zoom", L"Reset zoom", L"Restablecer zoom", L"줌 리셋", L"重置缩放", L"إعادة التكبير", L"Сброс зума", L"Zoom reset", L"Redefinir zoom", L"Zoom resetten", L"Reset zoom", L"Zoom sıfırla"));
		viewSub->AddCommand(55, LL14(L"カメラオフセットをリセット", L"Reset camera offset", L"Réinit. décalage caméra", L"Reset offset camera", L"Restablecer offset de cámara", L"카메라 오프셋 리셋", L"重置相机偏移", L"إعادة إزاحة الكاميرا", L"Сброс смещения камеры", L"Kamera-Offset reset", L"Redefinir offset da câmera", L"Camera-offset resetten", L"Reset offset kamery", L"Kamera ofsetini sıfırla"));
	}
	
	const int mask = ItemMask();
	if (CCustomPopupMenu* itemSub = menu.AddSubMenu(LL14(L"アイテム", L"Items", L"Objets", L"Oggetti", L"Objetos",
		L"아이템", L"道具", L"عناصر", L"Предметы", L"Items", L"Itens", L"Items", L"Przedmioty", L"Öğeler"))) {
		itemSub->AddCommand(28, LL14(L"アイテムをすべてON", L"Enable all items", L"Activer tous les objets", L"Attiva tutti gli oggetti", L"Activar todos los objetos", L"아이템 모두 ON", L"全部道具开启", L"تفعيل كل العناصر", L"Включить все предметы", L"Alle Items ein", L"Ativar todos os itens", L"Alle items aan", L"Włącz wszystkie przedmioty", L"Tüm öğeleri aç"));
		itemSub->AddCommand(29, LL14(L"アイテムをすべてOFF", L"Disable all items", L"Désactiver tous les objets", L"Disattiva tutti gli oggetti", L"Desactivar todos los objetos", L"아이템 모두 OFF", L"全部道具关闭", L"تعطيل كل العناصر", L"Выключить все предметы", L"Alle Items aus", L"Desativar todos os itens", L"Alle items uit", L"Wyłącz wszystkie przedmioty", L"Tüm öğeleri kapat"));
		itemSub->AddSeparator();
		itemSub->AddCheck(30, LL14(L"アイテム: テンポ↑", L"Item: tempo↑", L"Objet: tempo↑", L"Oggetto: tempo↑", L"Objeto: tempo↑", L"아이템: 템포↑", L"道具：速度↑", L"Item: tempo↑", L"Item: tempo↑", L"Item: Tempo↑", L"Item: tempo↑", L"Item: tempo↑", L"Przedmiot: tempo↑", L"Öğe: tempo↑"), (mask & ITEM_TEMPO) != 0);
		itemSub->AddCheck(31, LL14(L"アイテム: テンポ↓", L"Item: tempo↓", L"Objet: tempo↓", L"Oggetto: tempo↓", L"Objeto: tempo↓", L"아이템: 템포↓", L"道具：速度↓", L"Item: tempo↓", L"Item: tempo↓", L"Item: Tempo↓", L"Item: tempo↓", L"Item: tempo↓", L"Przedmiot: tempo↓", L"Öğe: tempo↓"), (mask & ITEM_TEMPO_DN) != 0);
		itemSub->AddCheck(32, LL14(L"アイテム: ピッチ↑", L"Item: pitch↑", L"Objet: pitch↑", L"Oggetto: pitch↑", L"Objeto: tono↑", L"아이템: 피치↑", L"道具：音高↑", L"Item: pitch↑", L"Item: pitch↑", L"Item: Ton↑", L"Item: tom↑", L"Item: toon↑", L"Przedmiot: wys↑", L"Öğe: perde↑"), (mask & ITEM_PITCH_UP) != 0);
		itemSub->AddCheck(33, LL14(L"アイテム: ピッチ↓", L"Item: pitch↓", L"Objet: pitch↓", L"Oggetto: pitch↓", L"Objeto: tono↓", L"아이템: 피치↓", L"道具：音高↓", L"Item: pitch↓", L"Item: pitch↓", L"Item: Ton↓", L"Item: tom↓", L"Item: toon↓", L"Przedmiot: wys↓", L"Öğe: perde↓"), (mask & ITEM_PITCH_DN) != 0);
		itemSub->AddCheck(34, LL14(L"アイテム: 次の曲", L"Item: next", L"Objet: suivant", L"Oggetto: succ", L"Objeto: siguiente", L"아이템: 다음", L"道具：下一曲", L"Item: next", L"Item: next", L"Item: nächster", L"Item: próxima", L"Item: volgend", L"Przedmiot: nast", L"Öğe: sonraki"), (mask & ITEM_NEXT) != 0);
		itemSub->AddCheck(35, LL14(L"アイテム: 前の曲", L"Item: prev", L"Objet: préc", L"Oggetto: prec", L"Objeto: anterior", L"아이템: 이전", L"道具：上一曲", L"Item: prev", L"Item: prev", L"Item: vorheriger", L"Item: anterior", L"Item: vorig", L"Przedmiot: poprz", L"Öğe: önceki"), (mask & ITEM_PREV) != 0);
		itemSub->AddCheck(36, LL14(L"アイテム: 音量↑", L"Item: vol↑", L"Objet: vol↑", L"Oggetto: vol↑", L"Objeto: vol↑", L"아이템: 볼륨↑", L"道具：音量↑", L"Item: vol↑", L"Item: vol↑", L"Item: Laut↑", L"Item: vol↑", L"Item: vol↑", L"Przedmiot: głoś↑", L"Öğe: ses↑"), (mask & ITEM_VOL_UP) != 0);
		itemSub->AddCheck(37, LL14(L"アイテム: 音量↓", L"Item: vol↓", L"Objet: vol↓", L"Oggetto: vol↓", L"Objeto: vol↓", L"아이템: 볼륨↓", L"道具：音量↓", L"Item: vol↓", L"Item: vol↓", L"Item: Laut↓", L"Item: vol↓", L"Item: vol↓", L"Przedmiot: głoś↓", L"Öğe: ses↓"), (mask & ITEM_VOL_DN) != 0);
		itemSub->AddCheck(38, LL14(L"アイテム: EQ", L"Item: EQ", L"Objet: EQ", L"Oggetto: EQ", L"Objeto: EQ", L"아이템: EQ", L"道具：EQ", L"Item: EQ", L"Item: EQ", L"Item: EQ", L"Item: EQ", L"Item: EQ", L"Przedmiot: EQ", L"Öğe: EQ"), (mask & ITEM_EQ) != 0);
		itemSub->AddCheck(39, LL14(L"アイテム: EQ平坦", L"Item: EQ flat", L"Objet: EQ plat", L"Oggetto: EQ flat", L"Objeto: EQ plano", L"아이템: EQ평탄", L"道具：EQ平坦", L"Item: EQ flat", L"Item: EQ flat", L"Item: EQ flach", L"Item: EQ flat", L"Item: EQ flat", L"Przedmiot: EQ flat", L"Öğe: EQ düz"), (mask & ITEM_EQ_FLAT) != 0);
		itemSub->AddCheck(40, LL14(L"アイテム: リバーブ", L"Item: reverb", L"Objet: réverb", L"Oggetto: reverb", L"Objeto: reverb", L"아이템: 리버브", L"道具：混响", L"Item: reverb", L"Item: reverb", L"Item: Hall", L"Item: reverb", L"Item: reverb", L"Przedmiot: pogłos", L"Öğe: reverb"), (mask & ITEM_REVERB) != 0);
		itemSub->AddCheck(41, LL14(L"アイテム: クロスフェード", L"Item: crossfade", L"Objet: fondu", L"Oggetto: crossfade", L"Objeto: fundido", L"아이템: 크로스페이드", L"道具：交叉淡化", L"Item: xfade", L"Item: xfade", L"Item: Crossfade", L"Item: xfade", L"Item: xfade", L"Przedmiot: xfade", L"Öğe: xfade"), (mask & ITEM_XFADE) != 0);
		itemSub->AddCheck(42, LL14(L"アイテム: ランダム", L"Item: random", L"Objet: aléatoire", L"Oggetto: casuale", L"Objeto: aleatorio", L"아이템: 랜덤", L"道具：随机", L"Item: random", L"Item: random", L"Item: Zufall", L"Item: aleatório", L"Item: willekeurig", L"Przedmiot: losowo", L"Öğe: rastgele"), (mask & ITEM_RANDOM) != 0);
	}
	menu.AddSeparator();
	menu.AddCommand(45, LL14(L"テンポ／ピッチを開いた時に戻す", L"Reset tempo/pitch to open values", L"Remettre tempo/hauteur", L"Ripristina tempo/pitch", L"Restablecer tempo/tono", L"템포/피치 복원", L"恢复速度/音高", L"إعادة الإيقاع", L"Вернуть темп", L"Tempo zurück", L"Restaurar tempo", L"Tempo herstellen", L"Przywróć tempo", L"Tempo sıfırla"));

	UINT cmd = menu.Track(screenPt, this);
	if (cmd == 1) { StartRace(); return; }
	if (cmd == 2) { GenerateCourse(); return; }
	if (cmd == 20) { savedata.s3r_show_map = savedata.s3r_show_map ? 0 : 1; PersistUi(); return; }
	if (cmd == 56) { savedata.s3_pcm_sfx = savedata.s3_pcm_sfx ? 0 : 1; PersistUi(); return; }
	if (cmd == 28) { savedata.s3r_item_mask = ITEM_ALL; PersistUi(); return; }
	if (cmd == 29) { savedata.s3r_item_mask = 0; PersistUi(); return; }
	if (cmd >= 30 && cmd <= 42) {
		const int bits[] = { ITEM_TEMPO, ITEM_TEMPO_DN, ITEM_PITCH_UP, ITEM_PITCH_DN, ITEM_NEXT, ITEM_PREV, ITEM_VOL_UP, ITEM_VOL_DN, ITEM_EQ, ITEM_EQ_FLAT, ITEM_REVERB, ITEM_XFADE, ITEM_RANDOM };
		int idx = (int)cmd - 30; if (idx < 0 || idx > 12) return;
		int m = ItemMask(); int bit = bits[idx];
		if (m & bit) m &= ~bit; else m |= bit;
		if (m == 0) m = ITEM_TEMPO;
		savedata.s3r_item_mask = m; PersistUi(); return;
	}
	if (cmd == 45) RestoreAudioBaseline();
	if (cmd == 54) { m_camZoom = 1.f; savedata.s3r_zoom = 100; PersistUi(); }
	if (cmd == 55) { m_camYawOff = 0.f; m_camPitchOff = 0.22f; m_camSmoothInit = 0; }
}

BOOL CSoft3DRaceDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	m_ai.SetAeroMode(FALSE); m_opp.SetAeroMode(FALSE); m_len.SetAeroMode(FALSE); m_laps.SetAeroMode(FALSE); m_theme.SetAeroMode(FALSE); m_invert.SetAeroMode(FALSE); m_mesh.SetAeroMode(FALSE);
	m_start.SetAeroMode(FALSE); m_gen.SetAeroMode(FALSE); m_close.SetAeroMode(FALSE); m_view.SetAeroMode(FALSE);

	if (!m_uiFont.GetSafeHandle()) {
		LOGFONT lf = {}; lf.lfHeight = -18; lf.lfWeight = FW_SEMIBOLD; lf.lfCharSet = DEFAULT_CHARSET; lf.lfQuality = CLEARTYPE_QUALITY;
		_tcscpy_s(lf.lfFaceName, _T("Segoe UI")); m_uiFont.CreateFontIndirect(&lf);
	}
	if (m_uiFont.GetSafeHandle()) {
		CFont* pf = &m_uiFont;
		CWnd* ws[] = {&m_aiL,&m_oppL,&m_lenL,&m_lapsL,&m_themeL,&m_invertL,&m_meshL,&m_hint,&m_status,&m_ai,&m_opp,&m_len,&m_laps,&m_theme,&m_invert,&m_mesh,&m_start,&m_gen,&m_close};
		for (int i=0;i<(int)(sizeof(ws)/sizeof(ws[0]));i++) if (ws[i]->GetSafeHwnd()) ws[i]->SetFont(pf);
		CCustomComboBox* cbs[]={&m_ai,&m_opp,&m_len,&m_laps,&m_theme,&m_invert,&m_mesh};
		for (int i=0;i<7;i++){ cbs[i]->SetItemHeight(-1,28); cbs[i]->SetItemHeight(0,26); }
	}

	if (savedata.s3r_ai < 0 || savedata.s3r_ai >= AI_COUNT) savedata.s3r_ai = AI_NORMAL;
	if (savedata.s3r_opponents < 1 || savedata.s3r_opponents > 11) savedata.s3r_opponents = 5;
	if (savedata.s3r_len < 0 || savedata.s3r_len > 3) savedata.s3r_len = 0;
	if (savedata.s3r_laps < 0 || savedata.s3r_laps > 10) savedata.s3r_laps = 0;
	if (savedata.s3r_theme < 0 || savedata.s3r_theme > 8) savedata.s3r_theme = 0;
	if (savedata.s3r_item_mask <= 0) savedata.s3r_item_mask = ITEM_ALL;
	if (savedata.s3r_show_map != 0) savedata.s3r_show_map = 1; else savedata.s3r_show_map = 1;
	if (savedata.s3r_zoom < 50 || savedata.s3r_zoom > 250) savedata.s3r_zoom = 100;
	if (savedata.s3r_invert_y != 0) savedata.s3r_invert_y = 1;
	if (savedata.s3_mesh_density < 0 || savedata.s3_mesh_density > S3_MESH_DENSITY_MAX) savedata.s3_mesh_density = 5;

	SetWindowText(LL14(L"空中レース", L"Aerial race", L"Course aérienne", L"Gara aerea", L"Carrera aérea",
		L"공중 레이스", L"空中竞速", L"السباق الجوي", L"Воздушная гонка", L"Luftrennen",
		L"Corrida aérea", L"Luchtrace", L"Wyścig powietrzny", L"Hava yarışı"));
	m_aiL.SetWindowText(LL14(L"AI", L"AI", L"IA", L"IA", L"IA", L"AI", L"AI", L"AI", L"ИИ", L"KI", L"IA", L"AI", L"SI", L"YZ"));
	m_oppL.SetWindowText(LL14(L"台数", L"Opponents", L"Adversaires", L"Avversari", L"Rivales", L"대수", L"对手数", L"خصوم", L"Соперники", L"Gegner", L"Oponentes", L"Tegenstanders", L"Rywalе", L"Rakip"));
	m_lenL.SetWindowText(LL14(L"距離", L"Length", L"Longueur", L"Lunghezza", L"Longitud", L"거리", L"长度", L"طول", L"Длина", L"Länge", L"Comprimento", L"Lengte", L"Długość", L"Uzunluk"));
	m_lapsL.SetWindowText(LL14(L"周回", L"Laps", L"Tours", L"Giri", L"Vueltas", L"랩", L"圈数", L"لفات", L"Круги", L"Runden", L"Voltas", L"Ronden", L"Okrążenia", L"Tur"));
	m_themeL.SetWindowText(LL14(L"テーマ", L"Theme", L"Thème", L"Tema", L"Tema", L"테마", L"主题", L"سمة", L"Тема", L"Thema", L"Tema", L"Thema", L"Motyw", L"Tema"));
	m_invertL.SetWindowText(LL14(L"上下", L"Y-axis", L"Axe Y", L"Asse Y", L"Eje Y", L"상하", L"上下", L"محور Y", L"Ось Y", L"Y-Achse", L"Eixo Y", L"Y-as", L"Oś Y", L"Y ekseni"));
	m_meshL.SetWindowText(S3MeshDensityLabel());
	m_start.SetWindowText(LL14(L"スタート", L"Start", L"Démarrer", L"Via", L"Iniciar", L"시작", L"开始", L"ابدأ", L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat"));
	m_gen.SetWindowText(LL14(L"生成", L"Generate", L"Générer", L"Genera", L"Generar", L"생성", L"生成", L"توليد", L"Создать", L"Erzeugen", L"Gerar", L"Genereren", L"Generuj", L"Oluştur"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_hint.SetWindowText(LL14(L"マウス: 機体の向き / LMB・W・↑・Space加速 / RMB・S・↓ブレーキ / ホイールズーム / MMB後方 / N・Ctrl=ニトロ　パッド対応　ステータス右クリック=設定",
		L"Mouse: steer / LMB·W·Up·Space accel / RMB·S·Down brake / wheel zoom / MMB lookback / N·Ctrl=nitro  Pad OK  Right-click status=settings",
		L"Souris : braquer / LMB accél / RMB frein / molette zoom / MMB recul / N·Ctrl=nitro  Manette OK  Clic droit statut=réglages",
		L"Mouse: sterza / LMB accel / RMB freno / rotella zoom / MMB dietro / N·Ctrl=nitro  Pad OK  Clic destro stato=impostazioni",
		L"Ratón: dirigir / LMB acel / RMB freno / rueda zoom / MMB atrás / N·Ctrl=nitro  Mando OK  Clic derecho estado=ajustes",
		L"마우스: 기체 조종 / LMB가속 / RMB브레이크 / 휠줌 / MMB후방 / N·Ctrl=니트로  패드 OK  상태 우클릭=설정",
		L"鼠标：转向机体 / 左键加速 / 右键刹车 / 滚轮缩放 / 中键后视 / N·Ctrl=氮气  手柄可用  状态栏右键=设置",
		L"Mouse steer craft / LMB accel / RMB brake / wheel zoom / MMB lookback / N·Ctrl=nitro",
		L"Мышь: рулить / ЛКМ газ / ПКМ тормоз / колесо зум / СКМ назад / N·Ctrl=нитро",
		L"Maus: steuern / LMB Gas / RMB Bremse / Rad Zoom / MMB Rückblick / N·Ctrl=Nitro",
		L"Mouse: dirigir / LMB acel / RMB freio / roda zoom / MMB atrás / N·Ctrl=nitro",
		L"Muis: sturen / LMB gas / RMB rem / wiel zoom / MMB achteruit / N·Ctrl=nitro",
		L"Mysz: steruj / LMB gaz / RMB hamulec / kółko zoom / MMB wstecz / N·Ctrl=nitro",
		L"Fare: yönlendir / LMB gaz / RMB fren / teker zoom / MMB geri / N·Ctrl=nitro"));

	m_ai.AddString(LL14(L"超簡単", L"Super easy", L"Très facile", L"Facilissimo", L"Muy fácil", L"매우 쉬움", L"超简单", L"سهل جداً", L"Очень легко", L"Sehr leicht", L"Muito fácil", L"Zeer makkelijk", L"Bardzo łatwy", L"Çok kolay"));
	m_ai.AddString(LL14(L"簡単", L"Easy", L"Facile", L"Facile", L"Fácil", L"쉬움", L"简单", L"سهل", L"Легко", L"Leicht", L"Fácil", L"Makkelijk", L"Łatwy", L"Kolay"));
	m_ai.AddString(LL14(L"普通", L"Normal", L"Normal", L"Normale", L"Normal", L"보통", L"普通", L"عادي", L"Обычный", L"Normal", L"Normal", L"Normaal", L"Normalny", L"Normal"));
	m_ai.AddString(LL14(L"難しい", L"Hard", L"Difficile", L"Difficile", L"Difícil", L"어려움", L"困难", L"صعب", L"Сложно", L"Schwer", L"Difícil", L"Moeilijk", L"Trudny", L"Zor"));
	m_ai.AddString(LL14(L"強烈", L"Intense", L"Intense", L"Intenso", L"Intenso", L"강렬", L"强烈", L"شديد", L"Жёстко", L"Intensiv", L"Intenso", L"Intens", L"Intensywny", L"Yoğun"));
	SetAiToUi(savedata.s3r_ai);

	for (int i=1;i<=11;i++){ CString s; s.Format(_T("%d"), i); m_opp.AddString(s); }
	SetOppToUi(savedata.s3r_opponents);

	m_len.AddString(LL14(L"自動", L"Auto", L"Auto", L"Auto", L"Auto", L"자동", L"自动", L"تلقائي", L"Авто", L"Auto", L"Auto", L"Auto", L"Auto", L"Otomatik"));
	m_len.AddString(LL14(L"短い", L"Short", L"Court", L"Corto", L"Corto", L"짧음", L"短", L"قصير", L"Короткий", L"Kurz", L"Curto", L"Kort", L"Krótki", L"Kısa"));
	m_len.AddString(LL14(L"普通", L"Normal", L"Normal", L"Normale", L"Normal", L"보통", L"普通", L"عادي", L"Обычный", L"Normal", L"Normal", L"Normaal", L"Normalny", L"Normal"));
	m_len.AddString(LL14(L"長い", L"Long", L"Long", L"Lungo", L"Largo", L"긴", L"长", L"طويل", L"Длинный", L"Lang", L"Longo", L"Lang", L"Długi", L"Uzun"));
	SetLenToUi(savedata.s3r_len);

	m_laps.AddString(LL14(L"自動", L"Auto", L"Auto", L"Auto", L"Auto", L"자동", L"自动", L"تلقائي", L"Авто", L"Auto", L"Auto", L"Auto", L"Auto", L"Otomatik"));
	for (int i=1;i<=10;i++){ CString s; s.Format(_T("%d"), i); m_laps.AddString(s); }
	SetLapsToUi(savedata.s3r_laps);

	m_theme.AddString(LL14(L"自動", L"Auto", L"Auto", L"Auto", L"Auto", L"자동", L"自动", L"تلقائي", L"Авто", L"Auto", L"Auto", L"Auto", L"Auto", L"Otomatik"));
	m_theme.AddString(LL14(L"森", L"Forest", L"Forêt", L"Foresta", L"Bosque", L"숲", L"森林", L"غابة", L"Лес", L"Wald", L"Floresta", L"Bos", L"Las", L"Orman"));
	m_theme.AddString(LL14(L"遺跡", L"Ruins", L"Ruines", L"Rovine", L"Ruinas", L"유적", L"遗迹", L"أطلال", L"Руины", L"Ruinen", L"Ruínas", L"Ruïnes", L"Ruiny", L"Kalıntılar"));
	m_theme.AddString(LL14(L"石油工場", L"Oil factory", L"Usine pétrolière", L"Fabbrica petrolio", L"Fábrica de petróleo", L"정유공장", L"炼油厂", L"مصفاة", L"Нефтезавод", L"Ölwerk", L"Refinaria", L"Olierefinaderij", L"Rafineria", L"Petrol fabrikası"));
	m_theme.AddString(LL14(L"夜の街", L"Night city", L"Ville nocturne", L"Città notturna", L"Ciudad nocturna", L"밤의 도시", L"夜城", L"مدينة ليلية", L"Ночной город", L"Nachtstadt", L"Cidade noturna", L"Nachtstad", L"Nocne miasto", L"Gece şehri"));
	m_theme.AddString(LL14(L"水中チューブ", L"Underwater tube", L"Tube sous-marin", L"Tubo subacqueo", L"Tubo submarino", L"수중 튜브", L"水下管道", L"أنبوب تحت الماء", L"Подводная труба", L"Unterwasser-Tube", L"Tubo submarino", L"Onderwaterbuis", L"Rura podwodna", L"Su altı tüp"));
	m_theme.AddString(LL14(L"草原", L"Grassland", L"Prairie", L"Prateria", L"Pradera", L"초원", L"草原", L"مروج", L"Луга", L"Grasland", L"Pradaria", L"Grasland", L"Łąka", L"Otlak"));
	m_theme.AddString(LL14(L"夕焼けメサ", L"Sunset mesa", L"Mesa au couchant", L"Mesa al tramonto", L"Mesa al atardecer", L"석양 메사", L"日落台地", L"هضبة الغروب", L"Закатная меса", L"Sonnenuntergang-Mesa", L"Mesa ao pôr do sol", L"Zonsondergang-mesa", L"Zachodząca mesa", L"Gün batımı mesa"));
	m_theme.AddString(LL14(L"雲の庭", L"Cloud garden", L"Jardin de nuages", L"Giardino di nuvole", L"Jardín de nubes", L"구름 정원", L"云之庭园", L"حديقة سحاب", L"Сад облаков", L"Wolkengarten", L"Jardim de nuvens", L"Wolkentuin", L"Ogród chmur", L"Bulut bahçesi"));
	SetThemeToUi(savedata.s3r_theme);

	m_invert.AddString(LL14(L"通常", L"Normal", L"Normal", L"Normale", L"Normal", L"보통", L"普通", L"عادي", L"Обычный", L"Normal", L"Normal", L"Normaal", L"Normalny", L"Normal"));
	m_invert.AddString(LL14(L"上下反転", L"Invert Y", L"Inverser Y", L"Inverti Y", L"Invertir Y", L"상하 반전", L"上下翻转", L"عكس Y", L"Инверсия Y", L"Y umkehren", L"Inverter Y", L"Y omkeren", L"Odwróć Y", L"Y ters"));
	SetInvertToUi(savedata.s3r_invert_y);

	S3MeshFillCombo(m_mesh);
	SetMeshToUi(S3MeshDensityLevel());

	if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		m_tooltip.Activate(TRUE);
		m_tooltip.AddTool(&m_start, LL14(L"デモを終えてカウントダウン開始", L"End demo and start countdown", L"Fin démo + compte à rebours", L"Fine demo + conto alla rovescia", L"Fin demo + cuenta atrás", L"데모 종료 후 카운트다운", L"结束演示并倒计时", L"End demo, start countdown", L"Конец демо и отсчёт", L"Demo beenden und Countdown", L"Fim da demo e contagem", L"Demo uit, dan countdown", L"Koniec demo i odliczanie", L"Demoyu bitir, geri sayım"));
		m_tooltip.AddTool(&m_gen, LL14(L"コース再生成（直後にデモ走行）", L"Regenerate course (then demo lap)", L"Régénérer (puis démo)", L"Rigenera (poi demo)", L"Regenerar (luego demo)", L"코스 재생성(직후 데모)", L"重新生成（随后演示）", L"Regen then demo", L"Новая трасса → демо", L"Strecke neu → Demo", L"Gerar de novo → demo", L"Opnieuw → demo", L"Nowa trasa → demo", L"Yeniden oluştur → demo"));
		m_tooltip.AddTool(&m_invert, LL14(L"マウス上下とパッド上下のピッチを反転します", L"Invert mouse and pad pitch (up/down)", L"Inverser le tangage souris/manette", L"Inverte pitch mouse/pad", L"Invertir pitch de ratón/mando", L"마우스·패드 상하 피치 반전", L"翻转鼠标/手柄俯仰", L"Invert mouse/pad pitch", L"Инверсия pitch мыши/пада", L"Maus-/Pad-Pitch umkehren", L"Inverter pitch do mouse/pad", L"Muis-/pad-pitch omkeren", L"Odwróć pitch myszy/pada", L"Fare/pad pitch ters çevir"));
		m_tooltip.AddTool(&m_theme, LL14(L"テーマで地形とオブジェクトが変わります（丘・谷・川・トンネル）", L"Theme changes terrain and props (hills, valleys, rivers, tunnels)", L"Le thème change le terrain et les objets", L"Il tema cambia terreno e oggetti", L"El tema cambia terreno y objetos", L"테마에 따라 지형·오브젝트가 바뀝니다", L"主题会改变地形与物体（丘谷河隧道）", L"Theme changes terrain and props", L"Тема меняет рельеф и объекты", L"Thema ändert Gelände und Objekte", L"O tema muda terreno e objetos", L"Thema verandert terrein en objecten", L"Motyw zmienia teren i obiekty", L"Tema arazi ve nesneleri değiştirir"));
		m_tooltip.AddTool(&m_mesh, LL14(L"地形・木などのメッシュ密度。軽量=現行、5=既定、緻密≈80倍、美麗≈160倍（バッファ分割）", L"Terrain/prop mesh density. Light=current, 5=default, Dense≈80×, Fine≈160× (split buffers)", L"Densité. Léger=actuel, 5=défaut, Dense≈80×, Fin≈160× (plusieurs buffers)", L"Densità. Leggero=attuale, 5=predef., Denso≈80×, Fine≈160× (più buffer)", L"Densidad. Ligero=actual, 5=predet., Denso≈80×, Fino≈160× (varios buffers)", L"지형·나무 메시 밀도. 가벼움=현재, 5=기본, 치밀≈80배, 미려≈160배(분할)", L"地形与树木网格密度。轻量=现行，5=默认，致密≈80倍，美丽≈160倍（分块）", L"Mesh density. Light=current, 5=default, Dense≈80×, Fine≈160× (split buffers)", L"Плотность сетки. Лёгкий=текущий, 5=по умолч., Плотный≈80×, Изящный≈160×", L"Netzdichte. Leicht=aktuell, 5=Standard, Dicht≈80×, Fein≈160× (geteilt)", L"Densidade da malha. Leve=atual, 5=padrão, Denso≈80×, Belo≈160×", L"Meshdichtheid. Licht=huidig, 5=standaard, Dicht≈80×, Fijn≈160×", L"Gęstość siatki. Lekki=obecny, 5=domyślny, Gęsty≈80×, Piękny≈160×", L"Mesh yoğunluğu. Hafif=şimdiki, 5=varsayılan, Yoğun≈80×, Güzel≈160×"));
	}

	CaptureAudioBaseline();
	ApplySavedWindowRect();
	LayoutAll();
	if (!m_view.InitDx()) {
		CString msg;
		msg.Format(L"%s\n(stage=%d hr=0x%08X)",
			LL14(L"DirectX 11 の初期化に失敗しました。",L"DirectX 11 initialization failed.",L"Échec de l'initialisation de DirectX 11.",L"Inizializzazione DirectX 11 non riuscita.",L"Error al iniciar DirectX 11.",L"DirectX 11 초기화에 실패했습니다.",L"DirectX 11 初始化失败。",L"فشل تهيئة DirectX 11.",L"Не удалось инициализировать DirectX 11.",L"DirectX 11 konnte nicht initialisiert werden.",L"Falha ao iniciar o DirectX 11.",L"Initialisatie van DirectX 11 mislukt.",L"Nie udało się zainicjować DirectX 11.",L"DirectX 11 başlatılamadı."),
			m_view.m_dxFailStage, (unsigned)m_view.m_dxFailHr);
		MessageBox(msg, NULL, MB_OK|MB_ICONERROR);
		DestroyWindow();
		return FALSE;
	}
	GenerateCourse();
	Soft3DSfxEnsure(m_hWnd);
	m_lastTick = GetTickCount();
	// 描画・更新は og の timerp 経由。独自 SetTimer は使わない
	return TRUE;
}

void CSoft3DRaceDlg::OnStart() { StartRace(); }
void CSoft3DRaceDlg::OnGen() { GenerateCourse(); PersistUi(); }
void CSoft3DRaceDlg::RequestDestroyWindow()
{
	m_view.m_ready = FALSE;
	S3rReleaseJoypad();
	if (GetSafeHwnd() && IsWindowVisible())
		ShowWindow(SW_HIDE);
	if (m_inTick) {
		PostMessage(WM_CLOSE);
		return;
	}
	DestroyWindow();
}
void CSoft3DRaceDlg::OnClose() { RequestDestroyWindow(); }
void CSoft3DRaceDlg::OnCloseBtn() { RequestDestroyWindow(); }
void CSoft3DRaceDlg::OnHelp() { ShowHelpSheet(); }
void CSoft3DRaceDlg::OnAiChanged() { savedata.s3r_ai = ReadAiFromUi(); PersistUi(); }
void CSoft3DRaceDlg::OnOppChanged() { savedata.s3r_opponents = ReadOppFromUi(); PersistUi(); }
void CSoft3DRaceDlg::OnLenChanged() { savedata.s3r_len = ReadLenFromUi(); PersistUi(); }
void CSoft3DRaceDlg::OnLapsChanged() { savedata.s3r_laps = ReadLapsFromUi(); PersistUi(); }
void CSoft3DRaceDlg::OnThemeChanged() { savedata.s3r_theme = ReadThemeFromUi(); PersistUi(); }
void CSoft3DRaceDlg::OnInvertChanged() { savedata.s3r_invert_y = ReadInvertFromUi(); PersistUi(); }
void CSoft3DRaceDlg::OnMeshChanged()
{
	savedata.s3_mesh_density = ReadMeshFromUi();
	PersistUi();
	if (m_view.m_ready && m_knotN >= 4) {
		BuildObstacleMesh(m_themeActive);
		BakeStaticMeshes();
	}
}

void CSoft3DRaceDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	if (pWnd == &m_view) return; // view: no context
	CPoint sp = point;
	if (sp.x < 0) { CRect rc; GetWindowRect(&rc); sp.x = rc.left + 40; sp.y = rc.top + 40; }
	ShowContextMenu(sp);
}

void CSoft3DRaceDlg::TickFrame()
{
	if (m_inTick) return;
	m_inTick = 1;
	auto endTick=[&](){
		const int pend = (GetSafeHwnd() && !m_view.m_ready && !IsWindowVisible()) ? 1 : 0;
		m_inTick = 0;
		if (pend) DestroyWindow();
	};
	if (!GetSafeHwnd() || !m_view.m_ready) { endTick(); return; }
	PumpQueued(TRUE);
	if (!GetSafeHwnd() || !m_view.m_ready) { endTick(); return; }
	const DWORD now = GetTickCount();
	float dt = (float)(now - m_lastTick) * 0.001f;
	m_lastTick = now;
	if (dt < 0.f) dt = 0.f; if (dt > 0.05f) dt = 0.05f;
	m_frameDt = dt;
	m_anim += dt;

	if (m_phase == PHASE_COUNTDOWN) TickCountdown(dt);
	else if (m_phase == PHASE_DEMO) {
		TickDemo(dt);
	} else if (m_phase == PHASE_RACE || m_phase == PHASE_FINISH) {
		if (m_phase == PHASE_FINISH) {
			// 自機ゴール後：残り生存機を裏で高速シミュして全LAP埋め → 表彰
			if (AllAliveFinished()) {
				m_podiumT += dt;
				if (m_podiumT > 0.85f) EnterPodium();
			} else {
				const int steps = 10;
				const float stepDt = 0.045f; // ≈10x 実時間
				for (int s = 0; s < steps; s++) {
					TickPhysics(stepDt);
					TickItems(stepDt);
					m_finishSimT += stepDt;
					if (AllAliveFinished()) break;
				}
				// 詰まった場合はレール上で残り距離を走らせて完走（偽リタイアにしない）
				if (!AllAliveFinished() && m_finishSimT > 180.f) {
					const float plen = (m_pathLen > 1.f) ? m_pathLen : 800.f;
					for (int i = 0; i < m_craftN; i++) {
						S3rCraft& c = m_crafts[i];
						if (!c.alive || c.finished || c.retired) continue;
						float remainT = (float)m_lapsTarget - ((float)c.lap + c.pathT);
						if (remainT < 0.02f) remainT = 0.02f;
						float spd = FinishSimRailSpeed(c);
						if (spd < 8.f) spd = 8.f;
						float extra = remainT * plen / spd;
						int lapsLeft = m_lapsTarget - c.lap;
						if (lapsLeft < 1) lapsLeft = 1;
						for (int k = 0; k < lapsLeft && c.lapTimesN < 12; k++) {
							float frac = (k == 0) ? max(0.02f, 1.f - c.pathT) : 1.f;
							float one = frac * plen / spd;
							if (k == 0) one += c.raceTime;
							c.lapTimes[c.lapTimesN++] = one;
							if (one > 0.5f && one < c.bestLap) c.bestLap = one;
						}
						c.lap = m_lapsTarget;
						c.finished = 1;
						c.retired = 0;
						c.finishTime = m_raceClock + extra;
						c.raceTime = 0.f;
						c.vx = c.vy = c.vz = 0.f;
					}
					UpdateRanks();
				}
				m_standDirty = 1;
			}
		} else {
			TickPhysics(dt);
			TickItems(dt);
		}
	} else if (m_phase == PHASE_PODIUM) {
		TickPodium(dt);
	}

	if (m_phase == PHASE_RACE) {
		if (m_overlayHold > 0.f) m_overlayHold = max(0.f, m_overlayHold - dt);
		const int holdOverlay = (m_overlayHold > 0.f) ? 1 : 0;
		const int wrongOverlay = (m_wrongWay && !holdOverlay) ? 1 : 0;
		if (m_clearBakeA > 0.f && !m_clearBakeText.IsEmpty() && m_clearBakeText.Find(L"FINISH") < 0) {
			if (wrongOverlay) {
				m_clearBakeA = 1.f; // 逆走中は点灯維持
			} else if (!holdOverlay || m_overlayHold < 0.55f) {
				float fade = holdOverlay ? 1.6f : 0.85f;
				m_clearBakeA = max(0.f, m_clearBakeA - dt * fade);
				if (m_clearBakeA <= 0.02f) {
					m_clearBakeText = L""; m_clearBakeA = 0.f;
					m_clearDirty = 1; // 解放のみ
				}
			}
			// αフェードはシェーダ側（Misc.y）— 毎フレーム再焼きしない
		}
		if ((now % 100u) < 20u) UpdateStatus();
	} else if ((now % 200u) < 20u) {
		UpdateStatus();
	}
	if (!GetSafeHwnd() || !m_view.m_ready) { m_inTick = 0; return; }
	RenderScene();
	{
		for (int i = 0; i < S3R_MAX_CRAFT; i++) {
			if (i < m_craftN) {
				S3rCraft& c = m_crafts[i];
				float spd = sqrtf(c.vx * c.vx + c.vy * c.vy + c.vz * c.vz);
				float thr = 0.f;
				if (c.isPlayer) {
					thr = m_playerAccel ? 1.f : (spd > 4.f ? 0.28f : 0.10f);
					if (m_brakeHeld) thr *= 0.4f;
				} else {
					thr = spd / 36.f;
					if (thr > 1.f) thr = 1.f;
				}
				int liv = (c.alive && !c.retired) ? 1 : 0;
				if (m_phase == PHASE_PODIUM) { thr = 0.f; spd *= 0.15f; }
				Soft3DSfxEngine(i, liv, c.x, c.y, c.z, spd, thr, c.colorIdx, c.isPlayer);
			} else {
				Soft3DSfxEngine(i, 0, 0, 0, 0, 0, 0, 0, 0);
			}
		}
		float lyaw = atan2f(m_camAx - m_camSx, m_camAz - m_camSz);
		Soft3DSfxSetListener(m_camSx, m_camSy, m_camSz, lyaw);
		Soft3DSfxPump();
	}
	m_view.RequestRedraw();
	if (GetSafeHwnd())
		PumpQueued(FALSE);
	endTick();
}

void CSoft3DRaceDlg::OnTimer(UINT_PTR id)
{
	CCustomBlurDialogBase::OnTimer(id);
}
void CSoft3DRaceDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) { LayoutAll(); PersistWindowRect(); }
}
void CSoft3DRaceDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogBase::OnShowWindow(bShow, nStatus);
	if (bShow) LayoutAll();
}
void CSoft3DRaceDlg::OnDestroy()
{
	m_view.m_ready = FALSE;
	PersistUi(); PersistWindowRect();
	if (!IsSoft3DMazeOpen()) {
		Soft3DSfxShutdown();
		RestoreAudioBaseline();
	}
	S3rReleaseJoypad();
	CCustomBlurDialogBase::OnDestroy();
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SetTimer(4923, 20, NULL);
}

void OpenSoft3DRaceModeless(CWnd* p)
{
	CloseSoft3DMazeIfOpen();
	if (g_s3r && g_s3r->GetSafeHwnd()) { g_s3r->SetForegroundWindow(); return; }
	g_s3r = new CSoft3DRaceDlg(p);
	if (!g_s3r->Create(IDD_SOFT3DRACE, p)) { delete g_s3r; g_s3r = NULL; return; }
	g_s3r->ShowWindow(SW_SHOW);
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		::UnregisterHotKey(og->GetSafeHwnd(), 8000);
		::UnregisterHotKey(og->GetSafeHwnd(), 8001);
		::UnregisterHotKey(og->GetSafeHwnd(), 8002);
		::UnregisterHotKey(og->GetSafeHwnd(), 8003);
	}
}
void CloseSoft3DRaceIfOpen()
{
	if (!g_s3r) return;
	HWND hw = g_s3r->GetSafeHwnd();
	if (!hw || !::IsWindow(hw)) return;
	g_s3r->RequestDestroyWindow();
	for (int i = 0; i < 24; i++) {
		if (!g_s3r || !g_s3r->GetSafeHwnd() || !::IsWindow(g_s3r->GetSafeHwnd()))
			break;
		MSG msg = {};
		HWND cur = g_s3r->GetSafeHwnd();
		if (!cur || !::PeekMessage(&msg, cur, 0, 0, PM_REMOVE))
			break;
		if (msg.message == WM_QUIT) { ::PostQuitMessage((int)msg.wParam); break; }
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}
}
BOOL IsSoft3DRaceOpen()
{
	return (g_s3r && g_s3r->GetSafeHwnd() && ::IsWindow(g_s3r->GetSafeHwnd()) && g_s3r->IsWindowVisible()) ? TRUE : FALSE;
}
BOOL IsSoft3DRaceActive()
{
	return IsSoft3DRaceOpen();
}
BOOL Soft3DRacePreTranslate(MSG* pMsg)
{
	if (!IsSoft3DRaceOpen() || !pMsg || !g_s3r) return FALSE;
	return g_s3r->HandleAccelMessage(pMsg);
}
void Soft3DRaceOnTimerp()
{
	if (!g_s3r || !g_s3r->GetSafeHwnd() || !::IsWindow(g_s3r->GetSafeHwnd())) return;
	if (!g_s3r->IsWindowVisible() || g_s3r->IsIconic()) return;
	g_s3r->TickFrame();
}
