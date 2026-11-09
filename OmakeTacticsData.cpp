// OmakeTacticsData.cpp — おまけタクティクス: ルール・マップ生成・経路・戦闘・AI・セーブ

#include "stdafx.h"
#include "OmakeTacticsData.h"
#include "DatArchive.h"
#include "Soft3DGfxQuality.h"
#include "Soft3DGpuInfo.h"
#include <stdio.h>
#include <math.h>
#include <zstd.h>
#include <bcrypt.h>
#pragma comment(lib, "libzstd_static.lib")
#pragma comment(lib, "bcrypt.lib")

namespace otac {

static unsigned char g_tacArena[TAC_ARENA];
static size_t g_tacUsed = 0;
static int g_tacBad = 0;

// 起動後の読み込みスレッドと描画が、同じ arena から同時に切り出す。
static CRITICAL_SECTION g_arenaCs[3];
static volatile LONG g_arenaOnce = 0;
static void LockArena(int which)
{
	if (g_arenaOnce != 2) {
		if (InterlockedCompareExchange(&g_arenaOnce, 1, 0) == 0) {
			for (int i = 0; i < 3; i++) InitializeCriticalSection(&g_arenaCs[i]);
			g_arenaOnce = 2;
		} else while (g_arenaOnce != 2) Sleep(0);
	}
	EnterCriticalSection(&g_arenaCs[which]);
}

void* TacArenaAlloc(size_t bytes, size_t align)
{
	LockArena(0);
	if (align < 1) align = 1;
	size_t o = (g_tacUsed + align - 1) & ~(align - 1);
	void* r = 0;
	if (bytes > TAC_ARENA || o > TAC_ARENA - bytes) g_tacBad = 1;
	else { g_tacUsed = o + bytes; r = g_tacArena + o; }
	LeaveCriticalSection(&g_arenaCs[0]);
	return r;
}
void TacArenaReset() { LockArena(0); g_tacUsed = 0; g_tacBad = 0; LeaveCriticalSection(&g_arenaCs[0]); }
int TacArenaBad() { return g_tacBad; }

static unsigned char g_avaArena[TAC_AVA_ARENA];
static size_t g_avaUsed = 0;
static int g_avaBad = 0;

void* TacAvaAlloc(size_t bytes, size_t align)
{
	LockArena(1);
	if (align < 1) align = 1;
	size_t o = (g_avaUsed + align - 1) & ~(align - 1);
	void* r = 0;
	if (bytes > TAC_AVA_ARENA || o > TAC_AVA_ARENA - bytes) g_avaBad = 1;
	else { g_avaUsed = o + bytes; r = g_avaArena + o; }
	LeaveCriticalSection(&g_arenaCs[1]);
	return r;
}
size_t TacAvaMark() { LockArena(1); size_t m = g_avaUsed; LeaveCriticalSection(&g_arenaCs[1]); return m; }
void TacAvaRewind(size_t mark) { LockArena(1); if (mark <= g_avaUsed) g_avaUsed = mark; LeaveCriticalSection(&g_arenaCs[1]); }
void TacAvaReset() { LockArena(1); g_avaUsed = 0; g_avaBad = 0; LeaveCriticalSection(&g_arenaCs[1]); }
int TacAvaBad() { return g_avaBad; }

static unsigned char g_monArena[128 * 1024 * 1024];
static size_t g_monUsed = 0;
static size_t g_monKeep = 0;

void* TacMonAlloc(size_t bytes, size_t align)
{
	LockArena(2);
	if (align < 1) align = 1;
	size_t o = (g_monUsed + align - 1) & ~(align - 1);
	void* r = 0;
	if (bytes > sizeof(g_monArena) || o > sizeof(g_monArena) - bytes) { LeaveCriticalSection(&g_arenaCs[2]); return 0; }
	g_monUsed = o + bytes;
	r = g_monArena + o;
	LeaveCriticalSection(&g_arenaCs[2]);
	return r;
}
void TacMonReset() { LockArena(2); g_monUsed = g_monKeep; LeaveCriticalSection(&g_arenaCs[2]); }
void TacMonRestart() { LockArena(2); g_monKeep = 0; g_monUsed = 0; LeaveCriticalSection(&g_arenaCs[2]); }
void TacMonKeep() { LockArena(2); g_monKeep = g_monUsed; LeaveCriticalSection(&g_arenaCs[2]); }

// ============================================================ 数学
float WrapAngle(float a)
{
	const float P = 3.14159265f;
	while (a > P) a -= 2 * P;
	while (a < -P) a += 2 * P;
	return a;
}

Q4 QNlerp(const Q4& a, const Q4& b, float t)
{
	float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	float s = d < 0 ? -1.f : 1.f;
	Q4 r = { a.x + (b.x * s - a.x) * t, a.y + (b.y * s - a.y) * t, a.z + (b.z * s - a.z) * t, a.w + (b.w * s - a.w) * t };
	float l = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
	if (l > 1e-8f) { r.x /= l; r.y /= l; r.z /= l; r.w /= l; }
	return r;
}

Q4 QAxis(V3 ax, float ang)
{
	ax = Norm(ax);
	float s = sinf(ang * .5f);
	Q4 q = { ax.x * s, ax.y * s, ax.z * s, cosf(ang * .5f) };
	return q;
}

Q4 QMul(const Q4& a, const Q4& b)
{
	Q4 r;
	r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
	r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
	r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
	return r;
}

M4 MIdent() { M4 r = {}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }

M4 MMul(const M4& a, const M4& b)
{
	M4 r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r.m[i * 4 + j] = a.m[i * 4 + 0] * b.m[0 * 4 + j] + a.m[i * 4 + 1] * b.m[1 * 4 + j]
				+ a.m[i * 4 + 2] * b.m[2 * 4 + j] + a.m[i * 4 + 3] * b.m[3 * 4 + j];
	return r;
}

M4 MTRS(V3 t, const Q4& q, V3 s)
{
	const float x = q.x, y = q.y, z = q.z, w = q.w;
	M4 r = {};
	r.m[0] = (1 - 2 * (y * y + z * z)) * s.x; r.m[1] = (2 * (x * y + z * w)) * s.x; r.m[2] = (2 * (x * z - y * w)) * s.x;
	r.m[4] = (2 * (x * y - z * w)) * s.y; r.m[5] = (1 - 2 * (x * x + z * z)) * s.y; r.m[6] = (2 * (y * z + x * w)) * s.y;
	r.m[8] = (2 * (x * z + y * w)) * s.z; r.m[9] = (2 * (y * z - x * w)) * s.z; r.m[10] = (1 - 2 * (x * x + y * y)) * s.z;
	r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; r.m[15] = 1;
	return r;
}

M4 MTrans(float x, float y, float z) { M4 r = MIdent(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
M4 MScale(float x, float y, float z) { M4 r = MIdent(); r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
M4 MRotY(float a) { M4 r = MIdent(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
M4 MRotX(float a) { M4 r = MIdent(); float c = cosf(a), s = sinf(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
M4 MRotZ(float a) { M4 r = MIdent(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }

M4 MLookAtLH(V3 eye, V3 at, V3 up)
{
	V3 z = Norm(at - eye), x = Norm(Cross(up, z)), y = Cross(z, x);
	M4 r = MIdent();
	r.m[0] = x.x; r.m[1] = y.x; r.m[2] = z.x;
	r.m[4] = x.y; r.m[5] = y.y; r.m[6] = z.y;
	r.m[8] = x.z; r.m[9] = y.z; r.m[10] = z.z;
	r.m[12] = -Dot(x, eye); r.m[13] = -Dot(y, eye); r.m[14] = -Dot(z, eye);
	return r;
}

M4 MPerspLH(float fovY, float aspect, float zn, float zf)
{
	float ys = 1.f / tanf(fovY * .5f), xs = ys / aspect;
	M4 r = {};
	r.m[0] = xs; r.m[5] = ys; r.m[10] = zf / (zf - zn); r.m[11] = 1; r.m[14] = -zn * zf / (zf - zn);
	return r;
}

M4 MOrthoLH(float w, float h, float zn, float zf)
{
	M4 r = MIdent();
	r.m[0] = 2 / w; r.m[5] = 2 / h; r.m[10] = 1 / (zf - zn); r.m[14] = -zn / (zf - zn);
	return r;
}

M4 MTranspose(const M4& a)
{
	M4 r;
	for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r.m[i * 4 + j] = a.m[j * 4 + i];
	return r;
}

M4 MInverse(const M4& a)
{
	const float* m = a.m;
	float inv[16];
	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
	float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	M4 r = MIdent();
	if (fabsf(det) < 1e-12f) return r;
	det = 1.f / det;
	for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
	return r;
}

V3 MXformPt(const M4& a, V3 p)
{
	const float* m = a.m;
	float x = p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12];
	float y = p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13];
	float z = p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14];
	float w = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
	if (fabsf(w) > 1e-8f && fabsf(w - 1) > 1e-6f) { x /= w; y /= w; z /= w; }
	return v3(x, y, z);
}

V3 MXformDir(const M4& a, V3 d)
{
	const float* m = a.m;
	return v3(d.x * m[0] + d.y * m[4] + d.z * m[8], d.x * m[1] + d.y * m[5] + d.z * m[9], d.x * m[2] + d.y * m[6] + d.z * m[10]);
}

// ============================================================ 地形・テーマ
static const TerrainDef kTerrain[T_COUNT] = {
	// costFoot は平地=2。道=1、森・丘=3、沼=4、水辺=6。溶岩は歩けるが、手番を終えた地点で最大HPの12%。飛行は EnterCost で地形を無視し、溶岩でも傷まない。
	{ TX_TER_PLAIN,    2, 1, 0, 0 },
	{ TX_TER_FOREST,   3, 1, 10, 20 },
	{ TX_TER_HILL,     3, 1, 15, 10 },
	{ TX_TER_MOUNTAIN, 99, 0, 0, 0 },
	{ TX_TER_WATER,    6, 1, 0, 0 },
	{ TX_TER_CHASM,    99, 1, 0, 0 },
	{ TX_TER_LAVA,     4, 1, 0, 0 },
	{ TX_TER_SAND,     2, 1, 0, 5 },
	{ TX_TER_SNOW,     2, 1, 0, 5 },
	{ TX_TER_SWAMP,    4, 1, -10, -10 },
	{ TX_TER_ROAD,     1, 1, 0, 0 },
	{ TX_TER_BRIDGE,   1, 1, 0, 0 },
	{ TX_TER_RUINS,    2, 1, 10, 10 },
	{ TX_TER_ROCK,     99, 0, 0, 0 },
	{ TX_TER_DOOR,     99, 0, 0, 0 },
	{ TX_TER_VOID,     99, 0, 0, 0 },
};
const TerrainDef& TerrainInfo(int t) { return kTerrain[Clampi(t, 0, T_COUNT - 1)]; }
int ThemeNameTx(int th) { return TX_TH_PLAINS + Clampi(th, 0, TH_COUNT - 1); }
int StatNameTx(int s) { return TX_ST_HP + Clampi(s, 0, S_COUNT - 1); }

float Map::TopY(int x, int z) const
{
	if (!In(x, z)) return 0;
	const Cell& c = At(x, z);
	if (c.t == T_CHASM || c.t == T_VOID) return -3.2f;
	return c.h * kHStep;
}

float Map::StandY(int x, int z) const
{
	if (!In(x, z)) return 0;
	const Cell& c = At(x, z);
	if (c.t == T_CHASM) return 0.0f;
	if (c.t == T_WATER) return c.h * kHStep + 0.3f;
	if (c.t == T_LAVA) return c.h * kHStep + 0.25f;
	return c.h * kHStep;
}

float Map::WalkY(int x, int z, int layer) const
{
	if (!In(x, z)) return 0;
	const Cell& c = At(x, z);
	if (!LayerOn(c, layer)) layer = 0;
	int h = LayerHt(c, layer);
	int t = LayerTr(c, layer);
	if (t == T_CHASM) return 0.f;
	if (t == T_WATER) return h * kHStep + 0.3f;
	if (t == T_LAVA) return h * kHStep + 0.25f;
	return h * kHStep;
}

static float Curve01(float t)
{
	if (t <= 0.f) return 0.f;
	if (t >= 1.f) return 1.f;
	return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}

static float VisGroundY(const Map& m, int x, int z)
{
	const Cell& c = m.At(x, z);
	if (c.t == T_VOID || c.t == T_CHASM) return -3.2f;
	float y = c.h * kHStep;
	if (c.t == T_WATER && c.gimm == GM_NONE && c.gparam > 0 && c.gparam <= 4) {
		float bed = y - c.gparam * kHStep;
		if (bed < 0.15f) bed = 0.15f;
		return bed;
	}
	return y;
}

static int VisGroundOk(const Map& m, int x, int z)
{
	if (!Map::In(x, z)) return 0;
	int t = m.At(x, z).t;
	return t != T_VOID && t != T_BRIDGE;
}

static float VisDeckY(const Map& m, int x, int z, int layer)
{
	const Cell& c = m.At(x, z);
	int h = LayerHt(c, layer);
	int t = LayerTr(c, layer);
	if (t == T_CHASM) return 0.f;
	if (t == T_WATER) return h * kHStep + 0.3f;
	if (t == T_LAVA) return h * kHStep + 0.25f;
	return h * kHStep;
}

static int VisOk(const Map& m, int sx, int sz, int kind, int layer)
{
	if (!Map::In(sx, sz)) return 0;
	if (kind == 2) return (int)m.At(sx, sz).nLay >= layer + 1;
	if (kind == 1) return m.At(sx, sz).t == layer;
	return VisGroundOk(m, sx, sz);
}

static float TopStandY(const Map& m, int x, int z)
{
	if (!Map::In(x, z)) return -3.2f;
	const Cell& c = m.At(x, z);
	if (c.t == T_VOID) return -3.2f;
	float y = VisGroundY(m, x, z);
	int n = c.nLay;
	if (n < 1) n = 1;
	if (n > LAY_N) n = LAY_N;
	for (int L = 1; L < n; L++) {
		float d = VisDeckY(m, x, z, L);
		if (d > y) y = d;
	}
	return y;
}

static float VisRaw(const Map& m, int sx, int sz, int kind, int layer)
{
	if (kind == 2) return VisDeckY(m, sx, sz, layer);
	if (kind == 1) return m.StandY(sx, sz);
	return VisGroundY(m, sx, sz);
}

float TacVisY(const Map& m, int cx, int cz, float px, float pz, int kind, int layer)
{
	if (!Map::In(cx, cz)) return 0.f;
	float u = px - 0.5f, v = pz - 0.5f;
	int i = (int)floorf(u), j = (int)floorf(v);
	float fu = Curve01(u - (float)i), fv = Curve01(v - (float)j);
	int sx[4] = { i, i + 1, i, i + 1 };
	int sz[4] = { j, j, j + 1, j + 1 };
	float ys[4], acc = 0.f;
	int ok[4], hold[4], n = 0;
	for (int k = 0; k < 4; k++) {
		ok[k] = VisOk(m, sx[k], sz[k], kind, layer);
		hold[k] = 0;
		if (ok[k]) {
			ys[k] = VisRaw(m, sx[k], sz[k], kind, layer);
			acc += ys[k];
			n++;
		} else if (kind == 2 && Map::In(sx[k], sz[k]) && m.At(sx[k], sz[k]).t != T_VOID) {
			ys[k] = TopStandY(m, sx[k], sz[k]);
		} else hold[k] = 1;
	}
	if (!n) {
		if (kind == 2) return VisDeckY(m, cx, cz, layer);
		if (kind == 1) return m.StandY(cx, cz);
		return VisGroundY(m, cx, cz);
	}
	float fill = acc / (float)n;
	for (int k = 0; k < 4; k++) if (hold[k]) ys[k] = fill;
	return Lerpf(Lerpf(ys[0], ys[1], fu), Lerpf(ys[2], ys[3], fu), fv);
}

// ---- 値ノイズ
static float HashF(DWORD seed, int x, int z)
{
	DWORD h = seed * 0x9E3779B1u ^ (DWORD)x * 0x85EBCA77u ^ (DWORD)z * 0xC2B2AE3Du;
	h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
	return (h & 0xFFFFFF) / 16777216.f;
}
static float VNoise(DWORD seed, float x, float z)
{
	int ix = (int)floorf(x), iz = (int)floorf(z);
	float fx = x - ix, fz = z - iz;
	fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
	float a = HashF(seed, ix, iz), b = HashF(seed, ix + 1, iz), c = HashF(seed, ix, iz + 1), d = HashF(seed, ix + 1, iz + 1);
	return Lerpf(Lerpf(a, b, fx), Lerpf(c, d, fx), fz);
}
static float Fbm(DWORD seed, float x, float z, int oct)
{
	float s = 0, a = .5f, n = 0;
	for (int i = 0; i < oct; i++) { s += a * VNoise(seed + i * 101, x, z); n += a; x *= 2.03f; z *= 2.03f; a *= .5f; }
	return s / n;
}

struct ThemeParam {
	float amp, freq;
	int baseH, ground;
	float forestThr;
	int valley;        // 0=川 1=谷(裂け目) 2=溶岩 3=沼の池
	int ranges, rockPct, road, swamp;
};
static const ThemeParam kTheme[TH_COUNT] = {
	{ 4.5f, .055f, 2, T_PLAIN, .63f, 0, 2, 2, 1, 0 },
	{ 6.0f, .050f, 3, T_PLAIN, .74f, 1, 3, 4, 0, 0 },
	{ 3.0f, .060f, 2, T_PLAIN, .45f, 0, 1, 2, 1, 0 },
	{ 5.0f, .050f, 2, T_SNOW, .60f, 0, 3, 3, 0, 0 },
	{ 4.0f, .045f, 2, T_SAND, .84f, 1, 2, 5, 1, 0 },
	{ 5.0f, .055f, 3, T_PLAIN, .80f, 2, 3, 6, 0, 0 },
	{ 2.0f, .060f, 2, T_PLAIN, .58f, 3, 1, 2, 0, 1 },
	{ 3.0f, .050f, 2, T_PLAIN, .70f, 1, 2, 3, 1, 0 },
	{ 3.4f, .048f, 2, T_SAND, .78f, 0, 1, 4, 1, 0 },
	{ 5.5f, .052f, 3, T_PLAIN, .66f, 0, 2, 3, 1, 0 },
	{ 4.2f, .042f, 2, T_RUINS, .9f, 1, 2, 6, 0, 0 },
	{ 2.4f, .07f, 2, T_RUINS, .88f, 0, 1, 2, 1, 0 },
	{ 4.8f, .05f, 3, T_RUINS, .72f, 0, 2, 4, 1, 0 },
	{ 3.2f, .046f, 2, T_PLAIN, .8f, 0, 1, 2, 1, 0 },
};

static bool FootPassable(int t) { return TerrainInfo(t).costFoot < 99; }

static void MeanderLine(Rng& r, int* xs, int* zs, int& n, int maxN, bool vertical)
{
	n = 0;
	float p = (float)r.Range(14, MAPN - 15);
	float dir = 0;
	for (int i = 0; i < MAPN && n < maxN; i++) {
		dir += r.F(-.35f, .35f);
		dir = Clampf(dir, -.8f, .8f);
		p += dir;
		if (p < 3) { p = 3; dir = .3f; }
		if (p > MAPN - 4) { p = MAPN - 4; dir = -.3f; }
		if (vertical) { xs[n] = (int)p; zs[n] = i; }
		else { xs[n] = i; zs[n] = (int)p; }
		n++;
	}
}

static void CarveValleyAround(Map& m, const BYTE (*mark)[MAPN], int depth)
{
	// 印のセルからの距離で地面を下げる（谷）
	static short dist[MAPN][MAPN];
	int qx[MAPN * MAPN], qz[MAPN * MAPN], qh = 0, qt = 0;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		dist[z][x] = mark[z][x] ? 0 : 999;
		if (mark[z][x]) { qx[qt] = x; qz[qt] = z; qt++; }
	}
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	while (qh < qt) {
		int x = qx[qh], z = qz[qh]; qh++;
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz) || dist[nz][nx] <= dist[z][x] + 1) continue;
			dist[nz][nx] = (short)(dist[z][x] + 1);
			qx[qt] = nx; qz[qt] = nz; qt++;
		}
	}
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		int d = dist[z][x];
		if (d > depth) continue;
		Cell& c = m.c[z][x];
		int dec = depth - d;
		c.h = (BYTE)max(1, (int)c.h - dec);
	}
}

static void PlaceLine(Map& m, Rng& r, int type, int width, bool vertical, BYTE (*mark)[MAPN], int* bridgeX, int* bridgeZ, int& nb)
{
	int xs[MAPN], zs[MAPN], n = 0;
	MeanderLine(r, xs, zs, n, MAPN, vertical);
	nb = 0;
	int nBridge = r.Range(2, 3);
	int bridgeAt[3];
	for (int i = 0; i < nBridge; i++) bridgeAt[i] = (i + 1) * n / (nBridge + 1) + r.Range(-4, 4);
	for (int i = 0; i < n; i++) {
		bool bridge = false;
		for (int b = 0; b < nBridge; b++) if (abs(i - bridgeAt[b]) == 0) bridge = true;
		for (int w = 0; w < width; w++) {
			int x = xs[i] + (vertical ? w : 0), z = zs[i] + (vertical ? 0 : w);
			if (!Map::In(x, z)) continue;
			mark[z][x] = 1;
			if (bridge) {
				if (nb < 6) { bridgeX[nb] = x; bridgeZ[nb] = z; nb++; }
			}
		}
	}
	(void)type;
}

static void MountainRange(Map& m, Rng& r, int extra)
{
	int x = r.Range(6, MAPN - 7), z = r.Range(6, MAPN - 7);
	float a = r.F(0, 6.283f);
	int len = r.Range(10, 22);
	for (int i = 0; i < len; i++) {
		a += r.F(-.4f, .4f);
		int w = r.Range(1, 2);
		for (int oz = -w + 1; oz < w; oz++) for (int ox = -w + 1; ox < w; ox++) {
			int cx = x + ox, cz = z + oz;
			if (!Map::In(cx, cz)) continue;
			Cell& c = m.c[cz][cx];
			if (c.t == T_WATER || c.t == T_CHASM || c.t == T_LAVA || c.t == T_BRIDGE) continue;
			c.t = T_MOUNTAIN;
			c.h = (BYTE)min(MAXH, c.h + extra + r.Range(0, 1));
		}
		x += (int)roundf(cosf(a) * 1.5f);
		z += (int)roundf(sinf(a) * 1.5f);
		if (!Map::In(x, z)) break;
	}
}

static int StepCostGen(const Map& m, int x, int z)
{
	const Cell& c = m.c[z][x];
	switch (c.t) {
	case T_MOUNTAIN: return 30;
	case T_ROCK: return 25;
	case T_WATER: case T_LAVA: case T_CHASM: return 14;
	case T_FOREST: return 4;
	case T_SWAMP: return 4;
	case T_ROAD: return 1;
	default: return 2;
	}
}

static int CarveRoad(Map& m, int sx, int sz, int tx, int tz, int* px, int* pz, int maxN)
{
	// ダイクストラ（高低差にもペナルティ）
	static int dist[MAPN][MAPN];
	static short prev[MAPN][MAPN];
	static BYTE done[MAPN][MAPN];
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) { dist[z][x] = 1 << 29; prev[z][x] = -1; done[z][x] = 0; }
	dist[sz][sx] = 0;
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	for (;;) {
		int bx = -1, bz = -1, bd = 1 << 29;
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++)
			if (!done[z][x] && dist[z][x] < bd) { bd = dist[z][x]; bx = x; bz = z; }
		if (bx < 0 || (bx == tx && bz == tz)) break;
		done[bz][bx] = 1;
		for (int k = 0; k < 4; k++) {
			int nx = bx + dx[k], nz = bz + dz[k];
			if (!Map::In(nx, nz) || done[nz][nx]) continue;
			int dh = abs((int)m.c[nz][nx].h - (int)m.c[bz][bx].h);
			int nd = bd + StepCostGen(m, nx, nz) + dh * 3;
			if (nd < dist[nz][nx]) { dist[nz][nx] = nd; prev[nz][nx] = (short)(bz * MAPN + bx); }
		}
	}
	int n = 0;
	int cx = tx, cz = tz;
	int tmpx[MAPN * MAPN / 4], tmpz[MAPN * MAPN / 4];
	while (n < (int)_countof(tmpx)) {
		tmpx[n] = cx; tmpz[n] = cz; n++;
		if (cx == sx && cz == sz) break;
		int p = prev[cz][cx];
		if (p < 0) break;
		cx = p % MAPN; cz = p / MAPN;
	}
	int out = 0;
	for (int i = n - 1; i >= 0 && out < maxN; i--) { px[out] = tmpx[i]; pz[out] = tmpz[i]; out++; }
	return out;
}

static bool FootReachable(const Map& m, int sx, int sz, int tx, int tz)
{
	static BYTE seen[MAPN][MAPN];
	memset(seen, 0, sizeof(seen));
	static int qx[MAPN * MAPN], qz[MAPN * MAPN];
	int qh = 0, qt = 0;
	qx[qt] = sx; qz[qt] = sz; qt++; seen[sz][sx] = 1;
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	while (qh < qt) {
		int x = qx[qh], z = qz[qh]; qh++;
		if (x == tx && z == tz) return true;
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz) || seen[nz][nx]) continue;
			if (!CanStep(m, x, z, nx, nz, MV_FOOT, 2)) continue;
			seen[nz][nx] = 1;
			qx[qt] = nx; qz[qt] = nz; qt++;
		}
	}
	return false;
}

// 山や段差で拠点まで行けないとき、空白の外は通さず、道筋だけ1段差にならす。
static void EnsureClimbPath(Map& m, int sx, int sz, int tx, int tz)
{
	if (!Map::In(sx, sz) || !Map::In(tx, tz)) return;
	if (FootReachable(m, sx, sz, tx, tz)) return;
	static int prev[MAPN][MAPN];
	static BYTE seen[MAPN][MAPN];
	memset(seen, 0, sizeof(seen));
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) prev[z][x] = -1;
	static int qx[MAPN * MAPN], qz[MAPN * MAPN];
	int qh = 0, qt = 0;
	qx[qt] = sx; qz[qt] = sz; qt++;
	seen[sz][sx] = 1;
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	bool found = false;
	while (qh < qt) {
		int x = qx[qh], z = qz[qh]; qh++;
		if (x == tx && z == tz) { found = true; break; }
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz) || seen[nz][nx]) continue;
			if (m.c[nz][nx].t == T_VOID) continue;
			if ((m.c[nz][nx].t == T_ROCK || m.c[nz][nx].t == T_MOUNTAIN) && m.c[nz][nx].nLay < 2) {
				int shell = 0;
				for (int sk = 0; sk < 4; sk++) {
					int ax2 = nx + dx[sk], az2 = nz + dz[sk];
					if (Map::In(ax2, az2) && m.c[az2][ax2].nLay >= 2) shell = 1;
				}
				if (shell) continue;
			}
			seen[nz][nx] = 1;
			prev[nz][nx] = z * MAPN + x;
			qx[qt] = nx; qz[qt] = nz; qt++;
		}
	}
	if (!found) return;
	int x = tx, z = tz, guard = MAPN * MAPN;
	while (guard-- > 0 && !(x == sx && z == sz)) {
		int p = prev[z][x];
		if (p < 0) break;
		int px = p % MAPN, pz = p / MAPN;
		Cell& c = m.c[z][x];
		Cell& f = m.c[pz][px];
		if (BridgeFloor(c) || c.nLay >= 2) { x = px; z = pz; continue; }
		int beside = 0;
		for (int bk = 0; bk < 4 && !beside; bk++) {
			int bx = x + dx[bk], bz = z + dz[bk];
			if (!Map::In(bx, bz)) continue;
			int fl = BridgeFloor(m.c[bz][bx]);
			if (fl && (int)c.h <= fl + 2) beside = 1;
		}
		if (beside) { x = px; z = pz; continue; }
		if (c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM) c.t = T_BRIDGE;
		else if (c.t == T_MOUNTAIN || c.t == T_ROCK || c.t == T_DOOR) { c.t = T_HILL; c.prop = PK_NONE; }
		if ((int)c.h > (int)f.h + 1) c.h = (BYTE)(f.h + 1);
		if ((int)c.h + 1 < (int)f.h) c.h = (BYTE)max(1, (int)f.h - 1);
		x = px; z = pz;
	}
}

static int NearPartyOrGroup(const Map& m, int x, int z, int extra)
{
	for (int i = 0; i < m.nP && i < MAX_PARTY; i++)
		if (max(abs(x - m.pStartX[i]), abs(z - m.pStartZ[i])) <= extra) return 1;
	for (int g = 0; g < m.nGrp; g++) {
		int rad = m.grp[g].boss ? extra + 2 : extra;
		for (int k = 0; k < m.grp[g].n && k < GRP_CAP; k++)
			if (max(abs(x - (int)m.grp[g].x[k]), abs(z - (int)m.grp[g].z[k])) <= rad) return 1;
	}
	return 0;
}

static void FlattenStand(Cell& c, int h)
{
	if (c.t == T_VOID || c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM
		|| c.t == T_MOUNTAIN || c.t == T_ROCK || c.t == T_DOOR
		|| TerrainInfo(c.t).costFoot >= 99)
		c.t = T_PLAIN;
	c.h = (BYTE)Clampi(h, 1, MAXH);
	c.prop = PK_NONE;
	c.nLay = 0;
	if (c.gimm == GM_FENCE || c.gimm == GM_DROP) { c.gimm = 0; c.gparam = 0; }
}

// ボスは足が広い。後から岩や水を敷いても、立っているマスと周囲は平らな地面のままにする。
static void OpenStandPads(Map& m)
{
	for (int g = 0; g < m.nGrp; g++) {
		MapGroup& grp = m.grp[g];
		if (grp.n <= 0) continue;
		if (grp.boss) {
			int cx = grp.x[0], cz = grp.z[0];
			int h = 2;
			if (Map::In(cx, cz) && m.c[cz][cx].h > 0 && m.c[cz][cx].t != T_VOID && m.c[cz][cx].t != T_CHASM)
				h = (int)m.c[cz][cx].h;
			for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
				int x = cx + dx, z = cz + dz;
				if (!Map::In(x, z)) continue;
				FlattenStand(m.c[z][x], h);
			}
		}
		for (int k = 0; k < grp.n && k < GRP_CAP; k++) {
			int x = grp.x[k], z = grp.z[k];
			if (!Map::In(x, z)) continue;
			if (TerrainInfo(m.c[z][x].t).costFoot >= 99 || m.c[z][x].t == T_VOID)
				FlattenStand(m.c[z][x], max(1, (int)m.c[z][x].h > 0 ? (int)m.c[z][x].h : 2));
			m.c[z][x].prop = PK_NONE;
		}
	}
}

static void PickDeploy(Map& m, int ax, int az, int* outX, int* outZ, int want, int spacing, const BYTE (*taken)[MAPN])
{
	static BYTE seen[MAPN][MAPN];
	memset(seen, 0, sizeof(seen));
	static int qx[MAPN * MAPN], qz[MAPN * MAPN];
	int qh = 0, qt = 0, got = 0;
	qx[qt] = ax; qz[qt] = az; qt++; seen[az][ax] = 1;
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	while (qh < qt && got < want) {
		int x = qx[qh], z = qz[qh]; qh++;
		const Cell& c = m.c[z][x];
		bool ok = FootPassable(c.t) && !(taken && taken[z][x]);
		for (int i = 0; i < got && ok; i++)
			if (Dist(outX[i], outZ[i], x, z) < spacing) ok = false;
		if (ok) { outX[got] = x; outZ[got] = z; got++; }
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz) || seen[nz][nx]) continue;
			if (!CanStep(m, x, z, nx, nz, MV_FOOT, 3)) continue;
			seen[nz][nx] = 1;
			qx[qt] = nx; qz[qt] = nz; qt++;
		}
	}
	for (int i = got; i < want; i++) { outX[i] = outX[got ? i % got : 0]; outZ[i] = outZ[got ? i % got : 0]; }
}

static int MapSpan(int stage)
{
	int s = 18 + (int)(sqrtf((float)max(1, stage - 1)) * 2.4f);
	if (stage >= 80) s += (stage - 80) / 20;
	if (stage >= 200) s += (stage - 200) / 35;
	return Clampi(s, 18, 56);
}

// 正規化座標 -1..1。中身の詰まった形だけ。輪や細い帯は広いマップだけ。
static bool ShapeInside(int kind, float x, float y)
{
	float r2 = x * x + y * y;
	switch (kind) {
	case 1: { // 三日月
		float b = (x - 0.55f) * (x - 0.55f) + y * y;
		return r2 < 1.f && b > 0.62f;
	}
	case 2: { // ひょうたん
		float a = (x + 0.40f) * (x + 0.40f) + y * y;
		float b = (x - 0.40f) * (x - 0.40f) + y * y;
		return a < 0.40f || b < 0.40f || (fabsf(x) < 0.46f && fabsf(y) < 0.30f);
	}
	case 3: { // ブーツ
		bool shaft = x > -0.08f && x < 0.78f && y > -0.95f && y < 0.42f;
		bool toe = x > -0.95f && x < 0.2f && y > -1.05f && y < -0.18f;
		return shaft || toe;
	}
	case 4: { // ハート
		float a = x * x + y * y - 1.f;
		return a * a * a - x * x * y * y * y < 0.f;
	}
	case 5: // 十字
		return (fabsf(x) < 0.42f && fabsf(y) < 1.02f) || (fabsf(y) < 0.42f && fabsf(x) < 1.02f);
	case 6: { // きのこ
		bool cap = (x * x) / 1.05f + (y - 0.28f) * (y - 0.28f) / 0.5f < 1.f && y > -0.02f;
		bool stem = fabsf(x) < 0.30f && y > -1.02f && y < 0.4f;
		return cap || stem;
	}
	case 7: // 輪（穴あき）
		return r2 < 1.f && r2 > 0.20f;
	case 8: { // くねった細長い島
		float wave = 0.38f * sinf(x * 3.2f);
		return fabsf(x) < 1.02f && fabsf(y - wave) < 0.40f;
	}
	case 9: { // 鍵穴
		bool head = x * x + (y - 0.38f) * (y - 0.38f) < 0.46f;
		bool stem = fabsf(x) < 0.26f && y < 0.5f && y > -1.02f;
		return head || stem;
	}
	case 10: { // 星
		float ang = atan2f(y, x);
		float rad = sqrtf(r2);
		float spike = 0.50f + 0.50f * fabsf(cosf(ang * 2.5f));
		return rad < spike;
	}
	case 11: // 三角
		return y < 0.95f && (y + 0.98f) > 1.55f * fabsf(x);
	case 12: { // 矢
		bool shaft = fabsf(y) < 0.24f && x > -1.02f && x < 0.25f;
		bool head = x >= 0.02f && x < 1.02f && fabsf(y) < (1.08f - x) * 1.2f;
		return shaft || head;
	}
	case 13: { // 爪。開いた側からしか入れない
		return r2 < 1.f && r2 > 0.30f && x < 0.42f;
	}
	case 15: return fabsf(x) < 0.28f || fabsf(y) < 0.22f || fabsf(x - y) < 0.2f || fabsf(x + y) < 0.2f;
	case 16: return fabsf(x) < 1.02f && fabsf(y) < 0.22f;
	case 17: return fabsf(y) < 1.02f && fabsf(x) < 0.22f;
	case 18: return (fabsf(x) < 0.22f && fabsf(y) < 1.02f) || (y > 0.15f && y < 0.95f && fabsf(x) < 0.85f);
	case 19: return (fabsf(y) < 0.22f && fabsf(x) < 1.02f) || (x > 0.15f && x < 0.95f && fabsf(y) < 0.85f);
	case 20: return fabsf(x) < 0.95f && fabsf(y - 0.35f * sinf(x * 6.f)) < 0.28f;
	case 21: return fabsf(y) < 0.95f && fabsf(x - 0.35f * sinf(y * 6.f)) < 0.28f;
	case 22: { float a = atan2f(y, x); return r2 < 1.f && r2 > 0.18f && sinf(a * 3.f) > -0.15f; }
	case 23: return r2 < 1.f && (fabsf(x) > 0.28f || y > 0.f);
	case 24: return fabsf(x) < 0.9f && fabsf(y) < 0.9f && (fabsf(x) > 0.35f || fabsf(y) > 0.35f);
	case 25: return y < 0.2f && y > -1.02f && fabsf(x) < (y + 1.15f) * 0.85f;
	case 26: return y > -0.2f && y < 1.02f && fabsf(x) < (1.15f - y) * 0.85f;
	case 27: return fabsf(x + 0.35f) < 0.28f && fabsf(y) < 1.f || fabsf(x - 0.4f) < 0.28f && fabsf(y) < 0.7f;
	case 28: return fabsf(y + 0.3f) < 0.24f && fabsf(x) < 1.f || fabsf(y - 0.4f) < 0.24f && fabsf(x) < 0.65f;
	case 29: { float d1 = (x + 0.45f) * (x + 0.45f) + y * y; float d2 = (x - 0.45f) * (x - 0.45f) + y * y; return d1 < 0.36f || d2 < 0.36f; }
	case 30: return r2 < 0.95f && fabsf(y) > 0.12f;
	case 31: return r2 < 0.95f && fabsf(x) > 0.12f;
	case 32: return fabsf(x) < 1.f && fabsf(y) < 0.18f + 0.55f * fabsf(x);
	case 33: return fabsf(y) < 1.f && fabsf(x) < 0.18f + 0.55f * fabsf(y);
	case 34: return (x > -0.2f && x < 1.f && fabsf(y) < 0.22f) || (y > -0.2f && y < 1.f && fabsf(x) < 0.22f);
	case 35: return (x < 0.2f && x > -1.f && fabsf(y) < 0.22f) || (y < 0.2f && y > -1.f && fabsf(x) < 0.22f);
	case 36: return fabsf(fmodf(x + 1.5f, 0.7f) - 0.35f) < 0.16f && fabsf(y) < 1.f;
	case 37: return fabsf(fmodf(y + 1.5f, 0.7f) - 0.35f) < 0.16f && fabsf(x) < 1.f;
	case 38: return r2 < 1.f && fabsf(atan2f(y, x)) < 1.15f;
	case 39: return r2 < 1.f && fabsf(atan2f(y, x)) > 2.0f;
	case 40: return fabsf(x) < 0.55f && fabsf(y) < 1.f || fabsf(y) < 0.28f && fabsf(x) < 1.f;
	case 41: return y > -1.f && y < 0.15f && fabsf(x) < 0.9f || fabsf(x) < 0.22f && y < 1.f;
	case 42: return x * x / 0.35f + y * y < 1.f || x * x + y * y / 0.35f < 1.f;
	case 43: return fabsf(x - y) < 0.22f && fabsf(x) < 1.f || fabsf(x + y) < 0.22f && fabsf(x) < 0.7f;
	case 44: return r2 < 1.f && (sinf(atan2f(y, x) * 4.f) > 0.f || r2 < 0.2f);
	case 14: { // 三つの島。細い橋でつなぐ
		auto blob = [](float px, float py, float cx, float cy, float rad) {
			float dx = px - cx, dy = py - cy;
			return dx * dx + dy * dy < rad * rad;
		};
		bool a = blob(x, y, -0.55f, 0.42f, 0.46f);
		bool b = blob(x, y, 0.60f, 0.20f, 0.42f);
		bool c = blob(x, y, -0.05f, -0.62f, 0.44f);
		bool ab = fabsf(y - 0.32f) < 0.13f && x > -0.22f && x < 0.28f;
		bool bc = fabsf(x - 0.20f) < 0.13f && y < 0.02f && y > -0.32f;
		return a || b || c || ab || bc;
	}
	default: {
		if (kind < 45) return r2 < 1.f;
		int id = kind - 45;
		int fam = id % 16;
		int var = id / 16;
		float t = 0.18f + (var % 5) * 0.07f;
		float n = 3.f + (float)(var % 7);
		if (fam == 0) {
			float rx = 0.55f + (var % 4) * 0.14f, ry = 0.35f + ((var / 4) % 4) * 0.16f;
			return (x * x) / (rx * rx) + (y * y) / (ry * ry) < 1.f;
		}
		if (fam == 1) {
			float ang = atan2f(y, x), rad = sqrtf(r2);
			float slice = 3.14159265f / n;
			float a = fabsf(fmodf(ang + slice, slice * 2.f) - slice);
			return rad * cosf(a) < cosf(slice) * (0.72f + t);
		}
		if (fam == 2) {
			float ang = atan2f(y, x), rad = sqrtf(r2);
			float spike = (0.38f + t) + (0.45f + t * 0.3f) * fabsf(cosf(ang * n * 0.5f));
			return rad < spike;
		}
		if (fam == 3) {
			float inn = 0.18f + (var % 6) * 0.08f;
			return r2 < 1.f && r2 > inn * inn;
		}
		if (fam == 4) {
			float ax = fabsf(x), ay = fabsf(y);
			return ax < 0.95f && ay < 0.42f + t || ay < 0.95f && ax < 0.42f + t;
		}
		if (fam == 5) return fabsf(x) + fabsf(y) < 0.95f + t * 0.4f;
		if (fam == 6) {
			float a = (x + 0.35f) * (x + 0.35f) + (y - 0.15f) * (y - 0.15f);
			float b = (x - 0.4f) * (x - 0.4f) + (y + 0.2f) * (y + 0.2f);
			float c = x * x + (y - 0.55f) * (y - 0.55f);
			return a < 0.28f + t || b < 0.26f + t * 0.5f || c < 0.14f || (fabsf(y) < 0.16f && fabsf(x) < 0.7f);
		}
		if (fam == 7) {
			float ang = atan2f(y, x);
			float rad = sqrtf(r2);
			float band = fabsf(rad - (0.35f + t + 0.08f * ang));
			return fabsf(ang) < 2.4f && band < 0.16f + (var % 3) * 0.04f && rad < 1.05f;
		}
		if (fam == 8) {
			float w = 0.55f + t;
			return fabsf(x) < w && fabsf(y - 0.22f * sinf(x * (4.f + n))) < 0.22f + t * 0.3f;
		}
		if (fam == 9) {
			float ang = atan2f(y, x);
			float rad = sqrtf(r2);
			float petal = 0.45f + 0.5f * powf(fabsf(cosf(ang * n * 0.5f)), 0.6f);
			return rad < petal;
		}
		if (fam == 10) {
			bool shaft = fabsf(y) < 0.16f + t * 0.2f && x > -1.f && x < 0.2f;
			bool head = x > -0.05f && x < 1.f && fabsf(y) < (1.05f - x) * (0.7f + t);
			return shaft || head;
		}
		if (fam == 11) {
			float ax = fabsf(x), ay = fabsf(y);
			float m = ax > ay ? ax : ay;
			return m < 0.92f && (ax > 0.55f || ay > 0.55f);
		}
		if (fam == 12) {
			bool u = y < 0.15f && fabsf(x) < 0.85f && (fabsf(x) > 0.45f || y < -0.35f);
			bool stem = fabsf(x) < 0.22f && y < 0.95f && y > -0.2f;
			return u || stem;
		}
		if (fam == 13) {
			float ang = atan2f(y, x);
			float rad = sqrtf(r2);
			float tooth = 0.72f + 0.22f * (0.5f + 0.5f * cosf(ang * n));
			return rad < tooth && rad > 0.18f;
		}
		if (fam == 14) {
			float nx = x * 2.1f + (var % 3) * 0.17f;
			float ny = y * 2.1f + (var % 5) * 0.13f;
			float h = sinf(nx * 1.7f) * cosf(ny * 1.3f) + 0.45f * sinf((nx + ny) * 2.2f);
			return h > -0.15f + t * 0.4f && r2 < 1.05f;
		}
		float side = 0.35f + t;
		return fabsf(x) < side && fabsf(y) < 1.f || fabsf(y) < side && fabsf(x) < 0.75f;
	}
	}
}

// 外形。螺旋と固定図は使わない。後段が角を埋め戻しても、最後にここへ戻す。
static BYTE gShape[MAPN][MAPN];
static int gShapeOn = 0;

// 小さい欠けは捨て、残った島は消さずに一番大きい島へ3マス幅でつなぐ。
static void KeepLargestMask(BYTE mask[MAPN][MAPN])
{
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	static int comp[MAPN][MAPN];
	static int qx[MAPN * MAPN], qz[MAPN * MAPN];
	static int par[MAPN][MAPN];
	static BYTE vis[MAPN][MAPN];
	for (int pass = 0; pass < 6; pass++) {
		memset(comp, 0, sizeof(comp));
		int id = 0, bestId = 0, bestN = 0;
		int bigId[64], nBig = 0;
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			if (!mask[z][x] || comp[z][x]) continue;
			id++;
			int qh = 0, qt = 0, n = 0;
			qx[qt] = x; qz[qt] = z; qt++;
			comp[z][x] = id;
			while (qh < qt) {
				int cx = qx[qh], cz = qz[qh]; qh++; n++;
				for (int k = 0; k < 4; k++) {
					int nx = cx + dx[k], nz = cz + dz[k];
					if (!Map::In(nx, nz) || comp[nz][nx] || !mask[nz][nx]) continue;
					comp[nz][nx] = id;
					qx[qt] = nx; qz[qt] = nz; qt++;
				}
			}
			if (n < 8) {
				for (int i = 0; i < qt; i++) mask[qz[i]][qx[i]] = 0;
				continue;
			}
			if (n > bestN) { bestN = n; bestId = id; }
			if (nBig < 64) bigId[nBig++] = id;
		}
		if (bestId <= 0 || nBig <= 1) return;
		int grew = 0;
		for (int t = 0; t < nBig; t++) {
			if (bigId[t] == bestId) continue;
			memset(vis, 0, sizeof(vis));
			int qh = 0, qt = 0;
			for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
				if (comp[z][x] != bestId) continue;
				vis[z][x] = 1;
				par[z][x] = -1;
				qx[qt] = x; qz[qt] = z; qt++;
			}
			int hx = -1, hz = -1;
			while (qh < qt && hx < 0) {
				int cx = qx[qh], cz = qz[qh]; qh++;
				for (int k = 0; k < 4 && hx < 0; k++) {
					int nx = cx + dx[k], nz = cz + dz[k];
					if (!Map::In(nx, nz) || vis[nz][nx]) continue;
					vis[nz][nx] = 1;
					par[nz][nx] = cz * MAPN + cx;
					if (comp[nz][nx] == bigId[t]) { hx = nx; hz = nz; break; }
					qx[qt] = nx; qz[qt] = nz; qt++;
				}
			}
			if (hx < 0) continue;
			int x = hx, z = hz, guard = MAPN * 2;
			while (guard-- > 0) {
				for (int oz = -1; oz <= 1; oz++) for (int ox = -1; ox <= 1; ox++) {
					int xx = x + ox, zz = z + oz;
					if (!Map::In(xx, zz)) continue;
					mask[zz][xx] = 1;
					if (comp[zz][xx] == 0) comp[zz][xx] = bestId;
				}
				if (par[z][x] < 0) break;
				int p = par[z][x];
				x = p % MAPN; z = p / MAPN;
			}
			for (int z2 = 0; z2 < MAPN; z2++) for (int x2 = 0; x2 < MAPN; x2++)
				if (comp[z2][x2] == bigId[t]) comp[z2][x2] = bestId;
			grew = 1;
		}
		if (!grew) return;
	}
}

static int StampShape(Map& m, int stage, Rng& r, int& pax, int& paz)
{
	int span = MapSpan(stage);
	// 盤いっぱいに広げると四角に見える。縁の空白が形になる大きさで止める。
	float half = (float)span * 0.36f;
	if (half < 12.f) half = 12.f;
	if (half > 19.f) half = 19.f;
	int kindMax = half >= 16.f ? 236 : (half >= 14.f ? 160 : 90);
	int kind = r.Range(0, kindMax);
	float rot = r.F(0, 6.2831853f);
	float cs = cosf(rot), sn = sinf(rot);
	int cx0 = MAPN / 2, cz0 = MAPN / 2;
	static BYTE mask[MAPN][MAPN];
	auto build = [&](int k, bool nibble) {
		int n = 0;
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			float dx = x + 0.5f - cx0, dz = z + 0.5f - cz0;
			float lx = (dx * cs + dz * sn) / half;
			float ly = (-dx * sn + dz * cs) / half;
			bool in = ShapeInside(k, lx, ly);
			if (in && nibble) {
				bool rim = !ShapeInside(k, lx * 0.78f, ly * 0.78f);
				float noise = Fbm(m.seed + 17, x * 0.23f, z * 0.23f, 2);
				if (rim && noise < 0.50f) in = false;
			}
			mask[z][x] = in ? 1 : 0;
			if (in) n++;
		}
		return n;
	};
	bool nibble = !(kind == 7 || kind == 8 || kind == 12 || kind == 13 || kind == 14) && kind < 45;
	if (build(kind, nibble) < 140) build(0, false);
	KeepLargestMask(mask);
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (mask[z][x]) continue;
		Cell& c = m.c[z][x];
		c.t = T_VOID; c.h = 0; c.prop = PK_NONE; c.gimm = 0; c.gparam = 0;
		c.nLay = 0; c.t1 = 0; c.h1 = 0; c.t2 = 0; c.h2 = 0;
	}
	memcpy(gShape, mask, sizeof(gShape));
	gShapeOn = 1;
	int bestS = 999999, bx = cx0, bz = cz0;
	for (int z = 2; z < MAPN - 2; z++) for (int x = 2; x < MAPN - 2; x++) {
		if (!mask[z][x]) continue;
		int nbr = 0;
		for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++)
			if (Map::In(x + dx, z + dz) && mask[z + dz][x + dx]) nbr++;
		if (nbr < 12) continue;
		float dx = x + 0.5f - cx0, dz = z + 0.5f - cz0;
		float lx = (dx * cs + dz * sn) / half;
		int sc = (int)(lx * 100.f) - nbr;
		if (sc < bestS) { bestS = sc; bx = x; bz = z; }
	}
	pax = bx; paz = bz;
	return span;
}

#include "tactics_src/fixed_maps.inc"

static int FixedHeight(char ch)
{
	switch (ch) {
	case 'M': return 6;
	case 'P': case 'W': return 5;
	case 'h': return 4;
	case 'k': case 'u': return 3;
	case 'w': return 1;
	case ' ': return 0;
	default: return 2;
	}
}
static int FixedTerrain(char ch)
{
	switch (ch) {
	case 'f': return T_FOREST;
	case 'h': return T_HILL;
	case 'M': return T_MOUNTAIN;
	case 's': return T_SAND;
	case 'P': return T_PLAIN;
	case 'W': case 'w': return T_WATER;
	case 'r': return T_ROAD;
	case 'B': return T_BRIDGE;
	case 'u': return T_RUINS;
	case 'k': return T_ROCK;
	case ' ': return T_VOID;
	default: return T_PLAIN;
	}
}
// 海底・滝は固定の地形。味方拠点のまわりだけ歩きやすくする。振り直しは敵の配置だけ動く。
static int StampFixedStage(Map& m, int th, int& pax, int& paz)
{
	const char (*rows)[65] = (th == TH_FALLS) ? kFlMap : kSbMap;
	pax = (th == TH_FALLS) ? kFlX : kSbX;
	paz = (th == TH_FALLS) ? kFlZ : kSbZ;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		char ch = rows[z][x];
		Cell& c = m.c[z][x];
		c.t = (BYTE)FixedTerrain(ch);
		c.h = (BYTE)FixedHeight(ch);
		c.prop = PK_NONE;
		c.gimm = 0;
		c.gparam = 0;
		c.var = (BYTE)((x * 17 + z * 31) & 255);
	}
	for (int z = paz - 3; z <= paz + 3; z++) for (int x = pax - 3; x <= pax + 3; x++) {
		if (!Map::In(x, z) || m.c[z][x].t == T_VOID) continue;
		m.c[z][x].t = (BYTE)(th == TH_SEABED ? T_SAND : T_PLAIN);
		if (m.c[z][x].h < 2) m.c[z][x].h = 2;
	}
	return 28;
}

static void ReapplyShape(Map& m)
{
	if (!gShapeOn) return;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (gShape[z][x]) continue;
		Cell& c = m.c[z][x];
		c.t = T_VOID; c.h = 0; c.prop = PK_NONE; c.gimm = 0; c.gparam = 0;
		c.nLay = 0; c.t1 = 0; c.h1 = 0; c.t2 = 0; c.h2 = 0; c.t3 = 0; c.h3 = 0; c.t4 = 0; c.h4 = 0;
	}
}

static void WipeCell(Cell& c)
{
	c.t = T_VOID; c.h = 0; c.prop = PK_NONE; c.gimm = 0; c.gparam = 0;
	c.nLay = 0; c.t1 = 0; c.h1 = 0; c.t2 = 0; c.h2 = 0; c.t3 = 0; c.h3 = 0; c.t4 = 0; c.h4 = 0;
}

static int GroundWalk(const Cell& c)
{
	if (c.t == T_VOID || c.t == T_CHASM || c.t == T_MOUNTAIN || c.t == T_ROCK || c.t == T_DOOR) return 0;
	return TerrainInfo(c.t).costFoot < 99;
}

// 塵だけの欠けは消す。島そのものは残し、後段で歩いて渡れるようにつなぐ。
static void CutLooseIslands(Map& m)
{
	static int comp[MAPN][MAPN];
	memset(comp, 0, sizeof(comp));
	static int area[MAPN * MAPN + 1];
	memset(area, 0, sizeof(area));
	static int qx[MAPN * MAPN], qz[MAPN * MAPN];
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	int bestId = 0, bestN = 0, id = 0;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (!GroundWalk(m.c[z][x]) || comp[z][x]) continue;
		id++;
		int qh = 0, qt = 0, n = 0;
		qx[qt] = x; qz[qt] = z; qt++;
		comp[z][x] = id;
		while (qh < qt) {
			int cx = qx[qh], cz = qz[qh]; qh++; n++;
			for (int k = 0; k < 4; k++) {
				int nx = cx + dx[k], nz = cz + dz[k];
				if (!Map::In(nx, nz) || comp[nz][nx] || !GroundWalk(m.c[nz][nx])) continue;
				comp[nz][nx] = id;
				qx[qt] = nx; qz[qt] = nz; qt++;
			}
		}
		if (n > bestN) { bestN = n; bestId = id; }
		if (id > 0 && id <= MAPN * MAPN) area[id] = n;
	}
	if (bestId <= 0) return;
	int keepId[MAX_GROUPS + 2], keepN = 0;
	auto addKeep = [&](int id) {
		if (id <= 0) return;
		for (int i = 0; i < keepN; i++) if (keepId[i] == id) return;
		if (keepN < MAX_GROUPS + 2) keepId[keepN++] = id;
	};
	int home = 0;
	if (m.nP > 0 && Map::In(m.pStartX[0], m.pStartZ[0])) home = comp[m.pStartZ[0]][m.pStartX[0]];
	addKeep(home > 0 ? home : bestId);
	for (int g = 0; g < m.nGrp; g++) for (int k = 0; k < m.grp[g].n && k < GRP_CAP; k++) {
		int x = m.grp[g].x[k], z = m.grp[g].z[k];
		if (Map::In(x, z)) addKeep(comp[z][x]);
	}
	static BYTE seen[MAPN][MAPN];
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		int cid = comp[z][x];
		int on = (cid > 0 && cid <= MAPN * MAPN && area[cid] >= 8) ? 1 : 0;
		for (int i = 0; i < keepN && !on; i++) if (cid == keepId[i]) on = 1;
		seen[z][x] = (BYTE)on;
	}
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (seen[z][x]) continue;
		Cell& c = m.c[z][x];
		if (GroundWalk(c) || ((c.t == T_VOID || c.t == T_CHASM) && c.nLay >= 2)) WipeCell(c);
	}
	for (int i = 0; i < m.nP; i++) {
		int x = m.pStartX[i], z = m.pStartZ[i];
		if (Map::In(x, z) && seen[z][x]) continue;
		for (int r = 1; r < MAPN; r++) {
			int found = 0;
			for (int dz = -r; dz <= r && !found; dz++) for (int dx = -r; dx <= r; dx++) {
				int nx = x + dx, nz = z + dz;
				if (!Map::In(nx, nz) || seen[nz][nx] != 1) continue;
				m.pStartX[i] = nx; m.pStartZ[i] = nz;
				seen[nz][nx] = 2;
				found = 1; break;
			}
			if (found) break;
		}
	}
	for (int g = 0; g < m.nGrp; g++) {
		MapGroup& grp = m.grp[g];
		int w = 0;
		for (int k = 0; k < grp.n && k < GRP_CAP; k++) {
			int x = grp.x[k], z = grp.z[k];
			if (!Map::In(x, z) || !seen[z][x]) continue;
			grp.x[w] = grp.x[k]; grp.z[w] = grp.z[k]; w++;
		}
		grp.n = (BYTE)w;
		if (w > 0) { grp.ax = grp.x[0]; grp.az = grp.z[0]; }
	}
}

// 幅3の螺旋。腕のあいだは空白なので、飛び越えて近道できない。
// climb=1 は外が低く中心が高い。0 はその逆。
static int StampVertical(Map& m, Rng& r, const ThemeParam& tp, int climb, int& pax, int& paz, int& endX, int& endZ)
{
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		Cell& c = m.c[z][x];
		c.t = T_VOID; c.h = 0; c.prop = PK_NONE; c.gimm = 0; c.gparam = 0;
		c.var = (BYTE)((x * 17 + z * 31) & 255);
	}
	static int px[1400], pz[1400];
	int n = 0;
	int x = 0, z = 0, dx = 1, dz = 0;
	int leg = 4, left = 4, turns = 0;
	while (n < 1200 && turns < 18) {
		if (abs(x) > 24 || abs(z) > 24) break;
		px[n] = x; pz[n] = z; n++;
		x += dx; z += dz;
		if (--left > 0) continue;
		int ox = dx, oz = dz;
		dx = -oz; dz = ox;
		turns++;
		left = leg;
		if ((turns & 1) == 0) leg += 4;
	}
	const int cx = MAPN / 2, cz = MAPN / 2;
	if (n < 8) {
		for (int z = cz - 8; z <= cz + 8; z++) for (int x = cx - 8; x <= cx + 8; x++) {
			if (!Map::In(x, z)) continue;
			if ((x - cx) * (x - cx) + (z - cz) * (z - cz) > 64) continue;
			m.c[z][x].t = (BYTE)tp.ground;
			m.c[z][x].h = 3;
		}
		pax = cx - 4; paz = cz; endX = cx + 4; endZ = cz;
		return 22;
	}
	static short arm[MAPN][MAPN];
	for (int z2 = 0; z2 < MAPN; z2++) for (int x2 = 0; x2 < MAPN; x2++) arm[z2][x2] = 0;
	static int hx[1400], hz[1400];
	int hn = 0;
	for (int i = n - 1; i >= 0; i--) {
		int gx = cx + px[i], gz = cz + pz[i];
		if (!Map::In(gx, gz) || arm[gz][gx]) break;
		int step = n - 1 - i;
		int perpx = 0, perpz = 0;
		if (i + 1 < n) { perpx = pz[i] - pz[i + 1]; perpz = px[i + 1] - px[i]; }
		else if (i > 0) { perpx = pz[i - 1] - pz[i]; perpz = px[i] - px[i - 1]; }
		int h = climb ? (1 + step / 3) : (11 - step / 3);
		h = Clampi(h, 1, MAXH - 1);
		auto touches = [&](int qx, int qz) {
			for (int oz = -1; oz <= 1; oz++) for (int ox = -1; ox <= 1; ox++) {
				int nx = qx + ox, nz = qz + oz;
				if (!Map::In(nx, nz) || !arm[nz][nx]) continue;
				if (abs((int)arm[nz][nx] - (step + 1)) > 8) return 1;
			}
			return 0;
		};
		if (touches(gx, gz)) break;
		int markX[3] = { gx, gx + perpx, gx - perpx };
		int markZ[3] = { gz, gz + perpz, gz - perpz };
		for (int k = 0; k < 3; k++) {
			int mx = markX[k], mz = markZ[k];
			if (!Map::In(mx, mz) || arm[mz][mx]) continue;
			if (k > 0 && touches(mx, mz)) continue;
			arm[mz][mx] = (short)(step + 1);
			Cell& c = m.c[mz][mx];
			c.t = (BYTE)tp.ground;
			c.h = (BYTE)h;
			if (k == 1 && r.Chance(16)) c.t = T_FOREST;
			hx[hn] = mx; hz[hn] = mz; hn++;
		}
	}
	pax = cx; paz = cz;
	endX = cx; endZ = cz;
	if (hn > 0) { pax = hx[0]; paz = hz[0]; endX = hx[hn - 1]; endZ = hz[hn - 1]; }
	if (!Map::In(pax, paz) || m.c[paz][pax].t == T_VOID) { pax = endX; paz = endZ; }
	return Clampi(hn / 8, 22, 46);
}

// 既存の地面は残し、その上に2本目、一部は3本目の道を足す。地面の高さを変えないので、下の道は切れない。
static int DeckCellOk(const Map& m, int x, int z)
{
	if (!Map::In(x, z)) return 0;
	if (gShapeOn && !gShape[z][x]) return 0;
	const Cell& c = m.c[z][x];
	if (c.nLay >= 2 || c.gimm) return 0;
	if (c.t == T_VOID || c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM || c.t == T_MOUNTAIN || c.t == T_ROCK || c.t == T_DOOR || c.t == T_BRIDGE) return 0;
	if (!FootPassable(c.t)) return 0;
	for (int i = 0; i < m.nP; i++) if (Dist(x, z, m.pStartX[i], m.pStartZ[i]) < 4) return 0;
	return 1;
}
static void StampCaves(Map& m, Rng& r, const ThemeParam& tp, int ax, int az, int wantSky)
{
	(void)tp; (void)ax; (void)az;
	const int gap = 6;
	struct Run { short n, vert, lane, start; };
	Run runs[160];
	int nr = 0;
	for (int vert = 0; vert < 2; vert++) {
		for (int lane = 2; lane < MAPN - 2; lane++) {
			int bestN = 0, bestS = 0, curN = 0, curS = 0, prevH = -1;
			for (int t = 1; t < MAPN - 1; t++) {
				int x = vert ? lane : t;
				int z = vert ? t : lane;
				int h = (int)m.c[z][x].h;
				int ok = DeckCellOk(m, x, z) && (curN == 0 || abs(h - prevH) <= 2);
				if (!ok) {
					if (curN > bestN) { bestN = curN; bestS = curS; }
					curN = 0; prevH = -1;
				} else {
					if (curN == 0) curS = t;
					curN++;
					prevH = h;
				}
			}
			if (curN > bestN) { bestN = curN; bestS = curS; }
			if (bestN >= 6 && nr < 160) {
				runs[nr].n = (short)bestN;
				runs[nr].vert = (short)vert;
				runs[nr].lane = (short)lane;
				runs[nr].start = (short)bestS;
				nr++;
			}
		}
	}
	for (int a = 0; a < nr; a++) for (int b = a + 1; b < nr; b++) if (runs[b].n > runs[a].n) {
		Run tmp = runs[a]; runs[a] = runs[b]; runs[b] = tmp;
	}
	int want = 2 + (r.Chance(45) ? 1 : 0);
	int made = 0, skies = 0;
	int pickV[4], pickL[4];
	for (int ri = 0; ri < nr && made < want; ri++) {
		int clash = 0;
		for (int p = 0; p < made; p++) if (pickV[p] == runs[ri].vert && abs(pickL[p] - runs[ri].lane) < 3) clash = 1;
		if (clash) continue;
		int vert = runs[ri].vert, lane = runs[ri].lane, start = runs[ri].start, n = runs[ri].n;
		if (n > 40) n = 40;
		int xs[40], zs[40];
		for (int i = 0; i < n; i++) {
			int t = start + i;
			xs[i] = vert ? lane : t;
			zs[i] = vert ? t : lane;
		}
		int ox = vert ? 1 : 0, oz = vert ? 0 : 1;
		struct Keep { short x, z; Cell c; };
		static Keep keep[240];
		int kn = 0;
		auto hold = [&](int hx, int hz) {
			if (!Map::In(hx, hz) || kn >= 240) return;
			for (int i = 0; i < kn; i++) if (keep[i].x == hx && keep[i].z == hz) return;
			keep[kn].x = (short)hx; keep[kn].z = (short)hz; keep[kn].c = m.c[hz][hx]; kn++;
		};
		auto undo = [&](int mark) {
			while (kn > mark) { kn--; m.c[keep[kn].z][keep[kn].x] = keep[kn].c; }
		};
		int doSky = (wantSky && skies < 2) ? 1 : 0;
		int ndeck = 0, gotSky = 0;
		int i0 = (n >= 10) ? 2 : 1;
		int i1 = (n >= 10) ? n - 2 : n - 1;
		for (int i = i0; i < i1; i++) {
			for (int side = 0; side < 2; side++) {
				int cx = xs[i] + ox * side, cz = zs[i] + oz * side;
				if (!DeckCellOk(m, cx, cz)) continue;
				if (abs((int)m.c[cz][cx].h - (int)m.c[zs[i]][xs[i]].h) > 2) continue;
				hold(cx, cz);
				Cell& c = m.c[cz][cx];
				int base = (int)c.h;
				int skyHere = (doSky && i > n / 5 && i < (n * 4) / 5) ? 1 : 0;
				c.nLay = (BYTE)(skyHere ? 3 : 2);
				c.t1 = T_HILL; c.h1 = (BYTE)(base + gap);
				if (skyHere) { c.t2 = T_RUINS; c.h2 = (BYTE)(base + gap * 2); gotSky = 1; }
				else { c.t2 = 0; c.h2 = 0; }
				c.prop = PK_NONE;
				ndeck++;
			}
		}
		if (ndeck < 4) { undo(0); continue; }
		int wasReach = 1;
		if (m.nP > 0 && m.nE > 0)
			wasReach = FootReachable(m, m.pStartX[0], m.pStartZ[0], m.eStartX[0], m.eStartZ[0]) ? 1 : 0;
		auto putStair = [&](int hx, int hz, int hh) -> int {
			if (!Map::In(hx, hz) || hh < 1 || hh > 250) return 0;
			for (int i = 0; i < kn; i++) if (keep[i].x == hx && keep[i].z == hz) return 0;
			const Cell& c = m.c[hz][hx];
			if (c.nLay >= 2 || c.gimm || c.t == T_VOID || c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM || c.t == T_MOUNTAIN || c.t == T_ROCK || c.t == T_DOOR || c.t == T_BRIDGE) return 0;
			if (gShapeOn && !gShape[hz][hx]) return 0;
			hold(hx, hz);
			Cell& w = m.c[hz][hx];
			w.t = T_HILL; w.h = (BYTE)hh; w.nLay = 1; w.prop = PK_NONE; w.gparam = 0;
			w.t1 = w.h1 = w.t2 = w.h2 = 0;
			return 1;
		};
		auto ramp = [&](int lowI, int highI, int deckI) -> int {
			if (lowI < 0 || highI < 0 || deckI < 0 || deckI >= n) return 0;
			if (m.c[zs[deckI]][xs[deckI]].nLay < 2) return 0;
			int h1 = (int)m.c[zs[deckI]][xs[deckI]].h1;
			if (h1 < 6) return 0;
			int mark = kn;
			if (!putStair(xs[lowI], zs[lowI], h1 - 4) || !putStair(xs[highI], zs[highI], h1 - 2)) { undo(mark); return 0; }
			int reach = 1;
			if (wasReach && m.nP > 0 && m.nE > 0)
				reach = FootReachable(m, m.pStartX[0], m.pStartZ[0], m.eStartX[0], m.eStartZ[0]) ? 1 : 0;
			if (!reach) { undo(mark); return 0; }
			return 1;
		};
		if (i0 >= 2) ramp(0, 1, i0);
		if (n - i1 >= 2) ramp(n - 1, n - 2, i1 - 1);
		if (gotSky) {
			auto climbSky = [&](int skyIdx, int dir) {
				for (int side = 0; side < 2; side++) {
					int cx = xs[skyIdx] + ox * side, cz = zs[skyIdx] + oz * side;
					if (!Map::In(cx, cz) || m.c[cz][cx].nLay < 3) continue;
					int h2 = (int)m.c[cz][cx].h2;
					auto at = [&](int idx) -> Cell* {
						if (idx < i0 || idx >= i1) return 0;
						int x = xs[idx] + ox * side, z = zs[idx] + oz * side;
						if (!Map::In(x, z) || m.c[z][x].nLay != 2) return 0;
						return &m.c[z][x];
					};
					Cell* a = at(skyIdx + dir);
					Cell* b = at(skyIdx + dir * 2);
					if (a && b) {
						if ((int)b->h1 < h2 - 4) b->h1 = (BYTE)(h2 - 4);
						if ((int)a->h1 < h2 - 2) a->h1 = (BYTE)(h2 - 2);
					} else if (a) {
						int step = (int)a->h1 + 2;
						if (step < h2 - 2) {
							a->h1 = (BYTE)step;
							if ((int)m.c[cz][cx].h2 > step + 2) m.c[cz][cx].h2 = (BYTE)(step + 2);
						}
					}
				}
			};
			climbSky(n / 5 + 1, -1);
			climbSky((n * 4) / 5 - 1, 1);
		}
		pickV[made] = vert;
		pickL[made] = lane;
		made++;
		if (gotSky) skies++;
	}
}

static void LayDeck(Cell& c, int L, int t, int h)
{
	h = Clampi(h, 1, MAXH);
	if (L <= 0) { c.t = (BYTE)t; c.h = (BYTE)h; }
	else if (L == 1) { c.t1 = (BYTE)t; c.h1 = (BYTE)h; }
	else if (L == 2) { c.t2 = (BYTE)t; c.h2 = (BYTE)h; }
	else if (L == 3) { c.t3 = (BYTE)t; c.h3 = (BYTE)h; }
	else { c.t4 = (BYTE)t; c.h4 = (BYTE)h; }
	if ((int)c.nLay < L + 1) c.nLay = (BYTE)(L + 1);
}

static void StampDeckPreset(Map& m, Rng& r, int ground, int preset)
{
	int cx = MAPN / 2, cz = MAPN / 2;
	for (int z = 8; z < MAPN - 8; z++) for (int x = 8; x < MAPN - 8; x++) {
		if (Dist(x, z, cx, cz) > 16) continue;
		if (m.c[z][x].t == T_VOID) { m.c[z][x].t = (BYTE)ground; m.c[z][x].h = 2; }
	}
	int down = (preset % 2) == 1;
	int arms = 4 + (preset % 3);
	float rad0 = 5.f + (float)(preset % 4);
	int steps = 36 + (preset % 5) * 4;
	int x = cx, z = cz;
	int prevH = Map::In(x, z) && m.c[z][x].t != T_VOID ? (int)m.c[z][x].h : 2;
	int prevL = 0;
	for (int i = 0; i < steps; i++) {
		int layer = i * (LAY_N - 1) / steps;
		if (layer < 0) layer = 0;
		if (layer > LAY_N - 1) layer = LAY_N - 1;
		float ang = (down ? -1.f : 1.f) * (6.2831853f * (float)arms * (float)i / (float)steps) + preset * 0.4f;
		float rad = rad0 + (float)(i % 8) * 0.15f;
		int tx = Clampi(cx + (int)lroundf(cosf(ang) * rad), 1, MAPN - 2);
		int tz = Clampi(cz + (int)lroundf(sinf(ang) * rad), 1, MAPN - 2);
		int nx = x + ((tx > x) ? 1 : (tx < x) ? -1 : 0);
		int nz = z + ((tz > z) ? 1 : (tz < z) ? -1 : 0);
		if (nx != x && nz != z) {
			if ((i & 1) == 0) nz = z;
			else nx = x;
		}
		if (!Map::In(nx, nz) || (nx == x && nz == z)) continue;
		x = nx; z = nz;
		int h = prevH;
		if (down) {
			if (layer < prevL && h > 1) h = prevH - 1;
		} else if (layer > prevL || (i % 4) == 0) {
			if (h < MAXH) h = prevH + 1;
		}
		Cell& c = m.c[z][x];
		if (c.t == T_VOID || c.t == T_CHASM) c.t = (BYTE)ground;
		for (int L = 0; L <= layer; L++) {
			int hh = h - (layer - L);
			if (hh < 1) hh = 1;
			LayDeck(c, L, ground, hh);
		}
		prevH = h;
		prevL = layer;
	}
	(void)r;
}

static void StampLandmarks(Map& m, Rng& r, int th, BYTE taken[MAPN][MAPN])
{
	if (th >= TH_SEABED) return;
	struct Fp { int w, d; };
	static const Fp kFp[6] = { { 2, 2 }, { 3, 3 }, { 1, 3 }, { 3, 1 }, { 2, 3 }, { 2, 1 } };
	int want = 5 + (m.stage % 4);
	if (want > 10) want = 10;
	int guard = 0, placed = 0;
	while (placed < want && guard < 70) {
		guard++;
		Fp fp = kFp[r.Range(0, 5)];
		if (fp.w + 2 >= MAPN || fp.d + 2 >= MAPN) continue;
		int x0 = r.Range(2, MAPN - 2 - fp.w);
		int z0 = r.Range(2, MAPN - 2 - fp.d);
		int ok = 1, baseH = 99;
		BYTE oldT[9], oldH[9], oldP[9];
		int n = 0;
		for (int dz = 0; dz < fp.d && ok; dz++) for (int dx = 0; dx < fp.w; dx++) {
			int x = x0 + dx, z = z0 + dz;
			if (taken[z][x]) { ok = 0; break; }
			Cell& c = m.c[z][x];
			if (c.gimm || c.nLay >= 2) { ok = 0; break; }
			if (c.t == T_VOID || c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM || c.t == T_BRIDGE || c.t == T_DOOR || c.t == T_MOUNTAIN || c.t == T_ROCK) { ok = 0; break; }
			if (!FootPassable(c.t)) { ok = 0; break; }
			for (int p = 0; p < m.nP; p++) if (Dist(x, z, m.pStartX[p], m.pStartZ[p]) < 4) ok = 0;
			if (!ok) break;
			if ((int)c.h < baseH) baseH = c.h;
			oldT[n] = c.t; oldH[n] = c.h; oldP[n] = c.prop; n++;
		}
		if (!ok || n != fp.w * fp.d) continue;
		n = 0;
		for (int dz = 0; dz < fp.d; dz++) for (int dx = 0; dx < fp.w; dx++) {
			Cell& c = m.c[z0 + dz][x0 + dx];
			int rim = (dx == 0 || dz == 0 || dx == fp.w - 1 || dz == fp.d - 1);
			c.t = T_MOUNTAIN;
			c.h = (BYTE)Clampi(baseH + (rim ? 1 : 2), 1, MAXH);
			c.prop = PK_NONE;
			c.gparam = 0;
			c.gimm = 0;
			taken[z0 + dz][x0 + dx] = 1;
			n++;
		}
		Cell& a = m.c[z0][x0];
		a.prop = PK_MOUNT;
		a.gparam = (BYTE)((fp.d << 4) | fp.w);
		int linked = 1;
		if (m.nP > 0 && m.nE > 0)
			linked = FootReachable(m, m.pStartX[0], m.pStartZ[0], m.eStartX[0], m.eStartZ[0]) ? 1 : 0;
		if (!linked) {
			n = 0;
			for (int dz = 0; dz < fp.d; dz++) for (int dx = 0; dx < fp.w; dx++) {
				Cell& c = m.c[z0 + dz][x0 + dx];
				c.t = oldT[n]; c.h = oldH[n]; c.prop = oldP[n]; c.gparam = 0;
				taken[z0 + dz][x0 + dx] = 0;
				n++;
			}
			continue;
		}
		placed++;
		(void)th;
	}
}

static int CellStand(const Cell& c, int L)
{
	if (!LayerOn(c, L)) return 0;
	int tr = LayerTr(c, L);
	if (tr == T_VOID || tr == T_CHASM) return 0;
	return TerrainInfo(tr).costFoot < 99;
}

static void FeetOrigin(const Map& m, int& sx, int& sz, int& sL)
{
	sx = (m.nP > 0) ? m.pStartX[0] : MAPN / 2;
	sz = (m.nP > 0) ? m.pStartZ[0] : MAPN / 2;
	if (!Map::In(sx, sz)) { sx = MAPN / 2; sz = MAPN / 2; }
	sL = -1;
	for (int L = 0; L < LAY_N; L++) if (CellStand(m.c[sz][sx], L)) { sL = L; break; }
	if (sL >= 0) return;
	for (int z = 0; z < MAPN && sL < 0; z++) for (int x = 0; x < MAPN && sL < 0; x++)
		for (int L = 0; L < LAY_N; L++) if (CellStand(m.c[z][x], L)) { sx = x; sz = z; sL = L; break; }
}

static int BfsFeet(const Map& m, BYTE seen[LAY_N][MAPN][MAPN])
{
	memset(seen, 0, sizeof(BYTE) * LAY_N * MAPN * MAPN);
	int sx, sz, sL;
	FeetOrigin(m, sx, sz, sL);
	if (sL < 0) return 0;
	static int qx[MAPN * MAPN * LAY_N], qz[MAPN * MAPN * LAY_N], qL[MAPN * MAPN * LAY_N];
	int qh = 0, qt = 0, n = 0;
	qx[qt] = sx; qz[qt] = sz; qL[qt] = sL; qt++;
	seen[sL][sz][sx] = 1;
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	while (qh < qt) {
		int x = qx[qh], z = qz[qh], L = qL[qh]; qh++;
		n++;
		int h = LayerHt(m.c[z][x], L);
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz)) continue;
			const Cell& t = m.c[nz][nx];
			for (int NL = 0; NL < LAY_N; NL++) {
				if (seen[NL][nz][nx] || !CellStand(t, NL)) continue;
				if (abs(LayerHt(t, NL) - h) > 2) continue;
				seen[NL][nz][nx] = 1;
				qx[qt] = nx; qz[qt] = nz; qL[qt] = NL; qt++;
			}
		}
	}
	return n;
}

static int FeetLinked(const Map& m)
{
	static BYTE seen[LAY_N][MAPN][MAPN];
	int got = BfsFeet(m, seen);
	int need = 0;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++)
		for (int L = 0; L < LAY_N; L++) if (CellStand(m.c[z][x], L)) need++;
	return need == 0 || got >= need;
}

static void PaintStair(Cell& c, int h)
{
	if (c.t == T_VOID || c.t == T_DOOR) return;
	if (TerrainInfo(c.t).costFoot >= 99) c.t = T_HILL;
	h = Clampi(h, 1, MAXH);
	c.h = (BYTE)h;
	if (c.nLay < 1) c.nLay = 1;
}

static void SetLayerHt(Cell& c, int L, int h)
{
	h = Clampi(h, 1, MAXH);
	if (L <= 0) c.h = (BYTE)h;
	else if (L == 1) c.h1 = (BYTE)h;
	else if (L == 2) c.h2 = (BYTE)h;
	else if (L == 3) c.h3 = (BYTE)h;
	else c.h4 = (BYTE)h;
}

// 届かない足場の縁に、高さ1段ずつの階段を1本足す。移動力は見ない。
static int StairOnce(Map& m)
{
	static BYTE seen[LAY_N][MAPN][MAPN];
	BfsFeet(m, seen);
	int ux = -1, uz = 0, uL = 0, rx = 0, rz = 0, rL = 0, best = 1 << 29;
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		for (int L = 0; L < LAY_N; L++) {
			if (!CellStand(m.c[z][x], L) || seen[L][z][x]) continue;
			int hU = LayerHt(m.c[z][x], L);
			for (int k = 0; k < 4; k++) {
				int nx = x + dx[k], nz = z + dz[k];
				if (!Map::In(nx, nz)) continue;
				for (int NL = 0; NL < LAY_N; NL++) {
					if (!seen[NL][nz][nx]) continue;
					int gap = abs(hU - LayerHt(m.c[nz][nx], NL));
					if (gap < best) { best = gap; ux = x; uz = z; uL = L; rx = nx; rz = nz; rL = NL; }
				}
			}
		}
	}
	if (ux < 0) {
		for (int z = 0; z < MAPN && ux < 0; z++) for (int x = 0; x < MAPN && ux < 0; x++)
			for (int L = 0; L < LAY_N; L++) if (CellStand(m.c[z][x], L) && !seen[L][z][x]) { ux = x; uz = z; uL = L; break; }
		if (ux < 0) return 1;
		rx = -1;
	}
	int hU = LayerHt(m.c[uz][ux], uL);
	int hR = (rx >= 0) ? LayerHt(m.c[rz][rx], rL) : hU;
	int step = (hU >= hR) ? 1 : -1;
	int cx = (rx >= 0) ? rx : ux;
	int cz = (rx >= 0) ? rz : uz;
	int h = hR;
	int changed = 0;
	static BYTE used[MAPN][MAPN];
	memset(used, 0, sizeof(used));
	if (rx >= 0) used[rz][rx] = 1;
	used[uz][ux] = 1;
	for (int guard = 0; guard < 64 && abs(hU - h) > 1; guard++) {
		int pick = -1, alongBest = -1;
		for (int pass = 0; pass < 2 && pick < 0; pass++) {
			for (int k = 0; k < 4; k++) {
				int nx = cx + dx[k], nz = cz + dz[k];
				if (!Map::In(nx, nz) || used[nz][nx]) continue;
				int t = m.c[nz][nx].t;
				if (t == T_VOID || t == T_DOOR) continue;
				if (nx == ux && nz == uz) continue;
				int along = 0;
				for (int k2 = 0; k2 < 4; k2++) {
					int ax = nx + dx[k2], az = nz + dz[k2];
					if (!Map::In(ax, az)) continue;
					for (int L = 0; L < LAY_N; L++) {
						if (!CellStand(m.c[az][ax], L) || seen[L][az][ax]) continue;
						if (abs(LayerHt(m.c[az][ax], L) - hU) <= 2) along = 1;
					}
				}
				if (pass == 0 && !along) continue;
				pick = k;
				alongBest = along;
				break;
			}
		}
		if (pick < 0) {
			// 横に足場が無い段差は、届かない側を一段ずつ下げて渡れるようにする。
			if (rx >= 0 && abs(hU - h) > 2) {
				int nh = h + (hU > h ? 2 : -2);
				int before = LayerHt(m.c[uz][ux], uL);
				SetLayerHt(m.c[uz][ux], uL, nh);
				if (LayerHt(m.c[uz][ux], uL) != before) changed = 1;
			}
			break;
		}
		int nx = cx + dx[pick], nz = cz + dz[pick];
		int nh = Clampi(h + step, 1, MAXH);
		Cell& c = m.c[nz][nx];
		int beforeH = (int)c.h;
		int beforeT = (int)c.t;
		PaintStair(c, nh);
		if ((int)c.h != beforeH || (int)c.t != beforeT) changed = 1;
		used[nz][nx] = 1;
		cx = nx; cz = nz;
		h = (int)c.h;
		if (alongBest && abs(hU - h) <= 1) break;
	}
	return changed;
}

// 空白で隔てられた足場へ、一番近い到達地から丘を1本渡す。
static int BridgeOnce(Map& m)
{
	static BYTE seen[LAY_N][MAPN][MAPN];
	if (!BfsFeet(m, seen)) return 0;
	static BYTE vis[MAPN][MAPN];
	memset(vis, 0, sizeof(vis));
	static short px[MAPN][MAPN], pz[MAPN][MAPN];
	static int qx[MAPN * MAPN], qz[MAPN * MAPN];
	int qh = 0, qt = 0;
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		int reach = 0;
		for (int L = 0; L < LAY_N; L++) if (seen[L][z][x]) reach = 1;
		if (!reach) continue;
		vis[z][x] = 1;
		px[z][x] = (short)x; pz[z][x] = (short)z;
		qx[qt] = x; qz[qt] = z; qt++;
	}
	if (qt <= 0) return 0;
	int fx = -1, fz = -1;
	while (qh < qt && fx < 0) {
		int x = qx[qh], z = qz[qh]; qh++;
		for (int k = 0; k < 4 && fx < 0; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz) || vis[nz][nx]) continue;
			if (m.c[nz][nx].t == T_DOOR) continue;
			vis[nz][nx] = 1;
			px[nz][nx] = (short)x; pz[nz][nx] = (short)z;
			int hole = 0;
			for (int L = 0; L < LAY_N; L++) if (CellStand(m.c[nz][nx], L) && !seen[L][nz][nx]) hole = 1;
			if (hole) { fx = nx; fz = nz; break; }
			qx[qt] = nx; qz[qt] = nz; qt++;
		}
	}
	if (fx < 0) return 0;
	int pathX[MAPN * 2], pathZ[MAPN * 2], np = 0;
	int x = fx, z = fz;
	while (np < MAPN * 2) {
		pathX[np] = x; pathZ[np] = z; np++;
		int px0 = (int)px[z][x], pz0 = (int)pz[z][x];
		if (px0 == x && pz0 == z) break;
		x = px0; z = pz0;
	}
	if (np < 3) return 0;
	int h = 4;
	int rx = pathX[np - 1], rz = pathZ[np - 1];
	for (int L = 0; L < LAY_N; L++) if (seen[L][rz][rx]) { h = LayerHt(m.c[rz][rx], L); break; }
	h = Clampi(h, 1, MAXH);
	int changed = 0;
	for (int i = 1; i < np - 1; i++) for (int oz = -1; oz <= 1; oz++) for (int ox = -1; ox <= 1; ox++) {
		int xx = pathX[i] + ox, zz = pathZ[i] + oz;
		if (!Map::In(xx, zz)) continue;
		Cell& c = m.c[zz][xx];
		if (c.t == T_DOOR) continue;
		if (ox || oz) {
			if (c.t != T_VOID && c.t != T_CHASM && TerrainInfo(c.t).costFoot < 99) continue;
		}
		int beforeT = (int)c.t, beforeH = (int)c.h;
		if (!CellStand(c, 0) || abs((int)c.h - h) > 2) {
			if (c.t == T_VOID || c.t == T_CHASM || TerrainInfo(c.t).costFoot >= 99) c.t = T_HILL;
			c.h = (BYTE)h;
			if (c.nLay < 1) c.nLay = 1;
		}
		if ((int)c.t != beforeT || (int)c.h != beforeH) changed = 1;
	}
	return changed;
}

// 隣り合っているのに高さが2を超える足場を、外から一段ずつ歩ける傾斜にする。
static int EaseAll(Map& m)
{
	static BYTE seen[LAY_N][MAPN][MAPN];
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	int changed = 0;
	for (int wave = 0; wave < 96; wave++) {
		if (!BfsFeet(m, seen)) return changed;
		int step = 0;
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			for (int L = 0; L < LAY_N; L++) {
				if (!CellStand(m.c[z][x], L) || seen[L][z][x]) continue;
				int hU = LayerHt(m.c[z][x], L);
				int best = 999, hR = hU;
				for (int k = 0; k < 4; k++) {
					int nx = x + dx[k], nz = z + dz[k];
					if (!Map::In(nx, nz)) continue;
					for (int NL = 0; NL < LAY_N; NL++) {
						if (!seen[NL][nz][nx]) continue;
						int gap = abs(hU - LayerHt(m.c[nz][nx], NL));
						if (gap < best) { best = gap; hR = LayerHt(m.c[nz][nx], NL); }
					}
				}
				if (best > 900 || best <= 2) continue;
				SetLayerHt(m.c[z][x], L, hR + (hU > hR ? 2 : -2));
				step = 1;
				changed = 1;
			}
		}
		if (!step) break;
	}
	return changed;
}

static void ConnectFeet(Map& m)
{
	for (int i = 0; i < 64; i++) {
		if (FeetLinked(m)) return;
		StairOnce(m);
		if (FeetLinked(m)) return;
		EaseAll(m);
		if (FeetLinked(m)) return;
		if (!BridgeOnce(m)) return;
	}
}

// 最後の保険。到達済みの足場から一番近い「未到達の足場」まで、障害物・空白を貫いて
// 同じ高さの平地回廊を必ず1本掘り、確実に繋ぐ。StairOnce/EaseAll/BridgeOnce が
// 幾何的に詰んでも、これは地形を平地化するだけなので失敗しない。1回で1領域が繋がる。
static bool ForceLink(Map& m)
{
	static BYTE seen[LAY_N][MAPN][MAPN];
	if (!BfsFeet(m, seen)) return false;
	static BYTE reach[MAPN][MAPN];
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		int on = 0;
		for (int L = 0; L < LAY_N; L++) if (seen[L][z][x]) { on = 1; break; }
		reach[z][x] = (BYTE)on;
	}
	// 到達済みセルすべてを始点に、格子全体を（地形を無視して）幅優先。最初に当たった
	// 未到達の立てる土台を目標とし、そこまでの最短経路を掘る。
	static int prev[MAPN][MAPN];
	static BYTE vis[MAPN][MAPN];
	static int qx[MAPN * MAPN], qz[MAPN * MAPN];
	memset(vis, 0, sizeof(vis));
	int qh = 0, qt = 0;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) if (reach[z][x]) {
		vis[z][x] = 1; prev[z][x] = -1; qx[qt] = x; qz[qt] = z; qt++;
	}
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	int tx = -1, tz = -1;
	while (qh < qt && tx < 0) {
		int x = qx[qh], z = qz[qh]; qh++;
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz) || vis[nz][nx]) continue;
			vis[nz][nx] = 1;
			prev[nz][nx] = z * MAPN + x;
			bool stand = false;
			for (int L = 0; L < LAY_N; L++) if (CellStand(m.c[nz][nx], L)) { stand = true; break; }
			if (stand && !reach[nz][nx]) { tx = nx; tz = nz; break; }
			qx[qt] = nx; qz[qt] = nz; qt++;
		}
	}
	if (tx < 0) return false; // 未到達の足場なし＝全て繋がっている
	// 経路を集める（目標→到達済みの起点）。
	static int px[MAPN * MAPN], pz[MAPN * MAPN];
	int np = 0;
	int cx = tx, cz = tz;
	while (np < MAPN * MAPN) {
		 px[np] = cx; pz[np] = cz; np++;
		int p = prev[cz][cx];
		if (p < 0) break;           // 到達済み起点に着いた
		cx = p % MAPN; cz = p / MAPN;
	}
	if (np < 2) return false;
	// 起点が「どの層で到達済みか」の高さに合わせて回廊を平地化する（端のつなぎ目を段差 0 にする）。
	int scx = px[np - 1], scz = pz[np - 1];
	int hBase = 2;
	for (int L = 0; L < LAY_N; L++) if (seen[L][scz][scx]) { hBase = LayerHt(m.c[scz][scx], L); break; }
	hBase = Clampi(hBase, 1, MAXH);
	// 目標セルと中間セル（起点は触らない）を同じ高さの平地にならす。
	for (int i = 0; i < np - 1; i++) FlattenStand(m.c[pz[i]][px[i]], hBase);
	return true;
}

static void ForceConnectFeet(Map& m)
{
	for (int i = 0; i < 400; i++) {
		if (FeetLinked(m)) return;
		if (!ForceLink(m)) return;
	}
}

static void GenerateMapBody(Map& m, int stage, DWORD seed, int theme)
{
	memset(&m, 0, sizeof(m));
	gShapeOn = 0;
	Rng r(seed);
	m.seed = seed;
	m.stage = stage;
	int th;
	if (theme >= 0 && theme < TH_COUNT) th = theme;
	else if (stage <= 1) th = TH_PLAINS;
	else if (stage % 10 == 0) th = (stage / 10) & 1 ? TH_VOLCANO : TH_RUINS;
	else th = r.Range(0, TH_COUNT - 1);
	m.theme = th;
	const ThemeParam& tp = kTheme[th];
	const DWORD ns = seed * 7 + 13;
	int pax = MAPN / 2, paz = MAPN / 2, span = 28;
	int eax = pax, eaz = paz;
	int vertical = 0, spiralEndX = pax, spiralEndZ = paz;
	if ((seed % 8u) <= 1u) {
		vertical = 1;
		span = StampVertical(m, r, tp, (seed % 8u) == 0u, pax, paz, spiralEndX, spiralEndZ);
		eax = spiralEndX;
		eaz = spiralEndZ;
	} else {

	// 1) 高さと基本地面
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		float n = Fbm(ns, x * tp.freq, z * tp.freq, 4);
		int h = tp.baseH + (int)roundf((n - .35f) * tp.amp * 2.f);
		Cell& c = m.c[z][x];
		c.h = (BYTE)Clampi(h, 1, MAXH - 3);
		c.t = (BYTE)tp.ground;
		c.var = (BYTE)r.Range(0, 255);
	}
	// 高台は丘
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		Cell& c = m.c[z][x];
		int hmax = c.h;
		static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
		for (int k = 0; k < 4; k++) if (Map::In(x + dx[k], z + dz[k])) hmax = max(hmax, (int)m.c[z + dz[k]][x + dx[k]].h);
		if (c.h >= tp.baseH + (int)(tp.amp * .7f) && hmax - c.h <= 1 && th != TH_DESERT)
			c.t = T_HILL;
	}

	// 2) 谷（川／裂け目／溶岩）
	static BYTE mark[MAPN][MAPN];
	memset(mark, 0, sizeof(mark));
	int bx[6], bz[6], nb = 0;
	const bool vert = r.Chance(50);
	int width = (tp.valley == 1) ? r.Range(2, 3) : r.Range(1, 2);
	if (th == TH_CANYON) width = 3;
	PlaceLine(m, r, 0, width, vert, mark, bx, bz, nb);
	CarveValleyAround(m, mark, th == TH_CANYON ? 4 : 2);
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (!mark[z][x]) continue;
		Cell& c = m.c[z][x];
		switch (tp.valley) {
		case 1: c.t = T_CHASM; c.h = 0; break;
		case 2: c.t = T_LAVA; c.h = (BYTE)max(0, (int)c.h - 1); break;
		default: c.t = T_WATER; c.h = (BYTE)max(0, (int)c.h - 1); break;
		}
	}
	for (int i = 0; i < nb; i++) {
		Cell& c = m.c[bz[i]][bx[i]];
		int hh = 0, cnt = 0;
		for (int oz = -2; oz <= 2; oz++) for (int ox = -2; ox <= 2; ox++) {
			int x = bx[i] + ox, z = bz[i] + oz;
			if (Map::In(x, z) && !mark[z][x]) { hh += m.c[z][x].h; cnt++; }
		}
		c.t = T_BRIDGE;
		c.h = (BYTE)Clampi(cnt ? hh / cnt : 2, 1, MAXH);
	}
	// 小さな副谷（全テーマで「谷」を入れる）
	if (tp.valley != 1) {
		memset(mark, 0, sizeof(mark));
		int cx = r.Range(10, MAPN - 11), cz = r.Range(10, MAPN - 11);
		float a = r.F(0, 6.283f);
		int len = r.Range(6, 10);
		for (int i = 0; i < len; i++) {
			if (Map::In(cx, cz)) mark[cz][cx] = 1;
			a += r.F(-.5f, .5f);
			cx += (int)roundf(cosf(a)); cz += (int)roundf(sinf(a));
		}
		CarveValleyAround(m, mark, 1);
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++)
			if (mark[z][x] && m.c[z][x].t != T_WATER && m.c[z][x].t != T_BRIDGE && m.c[z][x].t != T_LAVA) { m.c[z][x].t = T_CHASM; m.c[z][x].h = 0; }
	}
	// 池（沼・砂漠オアシス）
	int pools = (th == TH_SWAMP) ? 6 : (th == TH_DESERT || th == TH_FOREST) ? 2 : 1;
	for (int p = 0; p < pools; p++) {
		int cx = r.Range(6, MAPN - 7), cz = r.Range(6, MAPN - 7), rad = r.Range(2, 3);
		for (int z = cz - rad; z <= cz + rad; z++) for (int x = cx - rad; x <= cx + rad; x++) {
			if (!Map::In(x, z)) continue;
			float d = sqrtf((float)((x - cx) * (x - cx) + (z - cz) * (z - cz))) + r.F(0, .8f);
			Cell& c = m.c[z][x];
			if (d < rad && c.t != T_CHASM && c.t != T_BRIDGE) {
				c.t = (th == TH_VOLCANO) ? T_LAVA : T_WATER;
				c.h = (BYTE)max(1, (int)c.h - 1);
			}
		}
	}

	// 3) 山脈
	for (int i = 0; i < tp.ranges + 1; i++) MountainRange(m, r, 3);

	// 4) 森・沼・岩・遺跡
	const DWORD fs = seed * 31 + 7;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		Cell& c = m.c[z][x];
		if (c.t != tp.ground && c.t != T_HILL) continue;
		float f = Fbm(fs, x * .11f, z * .11f, 3);
		if (f > tp.forestThr) c.t = T_FOREST;
		else if (tp.swamp && Fbm(fs + 99, x * .09f, z * .09f, 3) > .58f) c.t = T_SWAMP;
		else if (r.Range(0, 99) < tp.rockPct) c.t = T_ROCK;
	}
	if (th == TH_RUINS || (th != TH_CANYON && r.Chance(40))) {
		int nr = th == TH_RUINS ? r.Range(4, 6) : 1;
		for (int k = 0; k < nr; k++) {
			int w = r.Range(5, 9), d = r.Range(5, 9);
			int x0 = r.Range(4, MAPN - w - 5), z0 = r.Range(4, MAPN - d - 5);
			int hh = m.c[z0 + d / 2][x0 + w / 2].h;
			if (m.c[z0 + d / 2][x0 + w / 2].t == T_CHASM) hh = 2;
			for (int z = z0; z < z0 + d; z++) for (int x = x0; x < x0 + w; x++) {
				Cell& c = m.c[z][x];
				if (c.t == T_WATER || c.t == T_CHASM || c.t == T_LAVA || c.t == T_BRIDGE) continue;
				c.t = T_RUINS; c.h = (BYTE)Clampi(hh, 1, MAXH - 3);
				bool edge = (x == x0 || z == z0 || x == x0 + w - 1 || z == z0 + d - 1);
				if (edge && ((x + z) % 2 == 0) && r.Chance(70)) c.t = T_ROCK;
			}
		}
	}

	// 5) 地図の外形を切ってから、太い側に味方拠点
	pax = MAPN / 2; paz = MAPN / 2;
	span = StampShape(m, stage, r, pax, paz);
	eax = pax; eaz = paz;
	for (int pass = 0; pass < 2; pass++) {
		int ax = pass ? eax : pax, az = pass ? eaz : paz;
		int hh = max(1, (int)m.c[az][ax].h);
		if (m.c[az][ax].t == T_MOUNTAIN) hh = max(1, hh - 3);
		if (m.c[az][ax].t == T_CHASM) hh = 2;
		int rad = pass ? 4 : 3;
		for (int z = az - rad; z <= az + rad; z++) for (int x = ax - rad; x <= ax + rad; x++) {
			if (!Map::In(x, z) || m.c[z][x].t == T_VOID) continue;
			Cell& c = m.c[z][x];
			c.t = (BYTE)(tp.ground == T_SAND ? T_SAND : (tp.ground == T_SNOW ? T_SNOW : T_PLAIN));
			int d = max(abs(x - ax), abs(z - az));
			c.h = (BYTE)Clampi(d < rad - 1 ? hh : (hh + c.h) / 2, 1, MAXH);
		}
	}

	// 6) 道（連結保証）
	int px[MAPN * 4], pz[MAPN * 4];
	int np = CarveRoad(m, pax, paz, eax, eaz, px, pz, MAPN * 4);
	for (int i = 0; i < np; i++) {
		Cell& c = m.c[pz[i]][px[i]];
		if (c.t == T_VOID) continue;
		if (c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM) {
			int hh = i > 0 ? m.c[pz[i - 1]][px[i - 1]].h : 2;
			c.t = T_BRIDGE; c.h = (BYTE)Clampi(hh, 1, MAXH);
		} else if (c.t == T_MOUNTAIN || c.t == T_ROCK) {
			c.t = (BYTE)(tp.road ? T_ROAD : tp.ground);
			if (i > 0) c.h = m.c[pz[i - 1]][px[i - 1]].h;
		} else if (tp.road && c.t != T_BRIDGE && c.t != T_RUINS) {
			c.t = T_ROAD;
		} else if (c.t == T_SWAMP && !tp.swamp) {
			c.t = (BYTE)tp.ground;
		}
		if (i > 0) {
			int ph = m.c[pz[i - 1]][px[i - 1]].h;
			if ((int)c.h > ph + 1) c.h = (BYTE)(ph + 1);
			if ((int)c.h < ph - 1) c.h = (BYTE)(ph - 1);
			if (c.h < 1) c.h = 1;
		}
	}
	// 橋の両端も段差を揃える
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (m.c[z][x].t != T_BRIDGE) continue;
		static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz)) continue;
			Cell& n = m.c[nz][nx];
			if (FootPassable(n.t) && n.t != T_BRIDGE && abs((int)n.h - (int)m.c[z][x].h) > 2)
				n.h = (BYTE)Clampi(m.c[z][x].h + (n.h > m.c[z][x].h ? 2 : -2), 1, MAXH);
		}
	}
	if (!FootReachable(m, pax, paz, eax, eaz)) {
		// 保険: 経路の段差を 0 に、障害物を除去
		for (int i = 0; i < np; i++) {
			Cell& c = m.c[pz[i]][px[i]];
			if (c.t == T_VOID) continue;
			if (!FootPassable(c.t)) c.t = (BYTE)(c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM ? T_BRIDGE : T_ROAD);
			if (i > 0) c.h = m.c[pz[i - 1]][px[i - 1]].h;
			if (c.h < 1) c.h = 1;
		}
	}

	}
	if ((seed % 5u) == 0u)
		StampDeckPreset(m, r, tp.ground == T_SAND || tp.ground == T_SNOW ? tp.ground : T_PLAIN, (int)((seed / 5u) % 12u));
	// 7) 味方配置と、地図全体の敵拠点（最大 200。1 拠点 3〜15）
	static BYTE taken[MAPN][MAPN];
	memset(taken, 0, sizeof(taken));
	m.nP = MAX_PARTY;
	PickDeploy(m, pax, paz, m.pStartX, m.pStartZ, MAX_PARTY, 1, NULL);
	for (int i = 0; i < MAX_PARTY; i++) taken[m.pStartZ[i]][m.pStartX[i]] = 1;
	m.nGrp = 0;
	m.nE = 0;
	{
		int order[MAPN * MAPN];
		int nc = 0;
		for (int z = 3; z < MAPN - 3; z++) for (int x = 3; x < MAPN - 3; x++) {
			int t = m.c[z][x].t;
			if (t == T_VOID || t == T_WATER || t == T_LAVA || t == T_CHASM) continue;
			if (Dist(x, z, pax, paz) < 5) continue;
			order[nc++] = z * MAPN + x;
		}
		int budget = MAX_ENEMY;
		int maxGrp = 2, lo = 3, hi = 3;
		if (stage <= 1) { maxGrp = 2; lo = 3; hi = 3; }
		else if (stage <= 4) { maxGrp = 3; lo = 3; hi = 3; }
		else if (stage <= 12) { maxGrp = 4; lo = 3; hi = 4; }
		else if (stage < 40) { maxGrp = 5 + stage / 10; lo = 3; hi = 4 + stage / 15; }
		else {
			// 46付近で約30体。9999で約200体。その間は段階に比例して増やす。
			int cap = 26 + (stage - 40) * 2 / 3;
			if (stage > 46)
				cap = 30 + (int)((long long)(stage - 46) * 170 / (9999 - 46));
			if (cap < 24) cap = 24;
			if (cap > 200) cap = 200;
			if (budget > cap) budget = cap;
			hi = 5 + (stage - 46) / 700;
			if (hi < 5) hi = 5;
			if (hi > GRP_CAP) hi = GRP_CAP;
			int avg = (lo + hi) / 2;
			if (avg < 4) avg = 4;
			maxGrp = cap / avg + 2;
			if (maxGrp < 6) maxGrp = 6;
			if (maxGrp > 36) maxGrp = 36;
		}
		int gap = stage <= 4 ? 5 : (stage < 40 ? 6 : 7);
		if (stage >= 400) gap = 6;
		if (stage >= 2000) gap = 5;
		if (stage >= 6000) gap = 4;
		static BYTE tried[MAPN][MAPN];
		memset(tried, 0, sizeof(tried));
		int guardPick = 0;
		int guardCap = stage >= 200 ? 200 : 80;
		while (m.nGrp < maxGrp && m.nGrp < MAX_GROUPS && budget >= 3 && guardPick < guardCap) {
			guardPick++;
			int target = 6 + m.nGrp * max(5, span / 3);
			int bestI = -1, bestS = 999999;
			for (int i = 0; i < nc; i++) {
				int x = order[i] % MAPN, z = order[i] / MAPN;
				if (tried[z][x]) continue;
				bool crowded = false;
				for (int gi = 0; gi < m.nGrp; gi++)
					if (Dist(x, z, m.grp[gi].ax, m.grp[gi].az) < gap) { crowded = true; break; }
				if (crowded) continue;
				int sc = abs(Dist(x, z, pax, paz) - target);
				if (sc < bestS) { bestS = sc; bestI = i; }
			}
			if (bestI < 0) break;
			int x = order[bestI] % MAPN, z = order[bestI] / MAPN;
			tried[z][x] = 1;
			int want = r.Range(lo, max(lo, hi));
			if (want > budget) want = budget;
			MapGroup& g = m.grp[m.nGrp];
			g.ax = (short)x; g.az = (short)z; g.boss = 0; g.n = 0;
			{
				int hh = max(1, (int)m.c[z][x].h);
				if (m.c[z][x].t == T_MOUNTAIN) hh = max(1, hh - 2);
				if (m.c[z][x].t == T_CHASM) hh = 2;
				for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
					int nx = x + dx, nz = z + dz;
					if (!Map::In(nx, nz)) continue;
					Cell& c = m.c[nz][nx];
					if (c.t == T_WATER || c.t == T_LAVA || c.t == T_VOID || c.t == T_CHASM) continue;
					c.t = (BYTE)(tp.ground == T_SAND ? T_SAND : (tp.ground == T_SNOW ? T_SNOW : T_PLAIN));
					if (!vertical)
						c.h = (BYTE)Clampi(max(abs(dx), abs(dz)) < 2 ? hh : (hh + c.h) / 2, 1, MAXH);
				}
			}
			int xs[GRP_CAP], zs[GRP_CAP];
			PickDeploy(m, x, z, xs, zs, want, 1, taken);
			for (int k = 0; k < want; k++) {
				if (taken[zs[k]][xs[k]]) continue;
				if (g.n > 0 && xs[k] == g.x[0] && zs[k] == g.z[0] && k > 0) continue;
				taken[zs[k]][xs[k]] = 1;
				g.x[g.n] = (short)xs[k];
				g.z[g.n] = (short)zs[k];
				g.n++;
			}
			if (g.n < 3) {
				for (int k = 0; k < g.n; k++) taken[g.z[k]][g.x[k]] = 0;
				continue;
			}
			m.nGrp++;
			budget -= g.n;
		}
		// 最寄りが遠すぎるときは、味方の近くに 1 拠点を寄せる
		int nearest = -1, nd = 999;
		for (int g = 0; g < m.nGrp; g++) {
			int d = Dist(m.grp[g].ax, m.grp[g].az, pax, paz);
			if (d < nd) { nd = d; nearest = g; }
		}
		if (nearest > 0) {
			MapGroup tmp = m.grp[0];
			m.grp[0] = m.grp[nearest];
			m.grp[nearest] = tmp;
		}
		if (m.nGrp > 0) {
			int farI = 0, fd = -1;
			for (int gi = 0; gi < m.nGrp; gi++) {
				int d = Dist(m.grp[gi].ax, m.grp[gi].az, pax, paz);
				if (d > fd) { fd = d; farI = gi; }
			}
			int minBoss = max(14, span / 3);
			if (fd < minBoss) {
				int bx = m.grp[farI].ax, bz = m.grp[farI].az, best = fd;
				for (int z = 3; z < MAPN - 3; z++) for (int x = 3; x < MAPN - 3; x++) {
					int t = m.c[z][x].t;
					if (t == T_VOID || t == T_WATER || t == T_LAVA || t == T_CHASM) continue;
					int d = Dist(x, z, pax, paz);
					if (d > best) { best = d; bx = x; bz = z; }
				}
				if (best > fd + 3) {
					MapGroup& g = m.grp[farI];
					short ox[GRP_CAP], oz[GRP_CAP];
					int on = g.n;
					for (int k = 0; k < on && k < GRP_CAP; k++) { ox[k] = g.x[k]; oz[k] = g.z[k]; taken[oz[k]][ox[k]] = 0; }
					int xs[GRP_CAP], zs[GRP_CAP];
					PickDeploy(m, bx, bz, xs, zs, on, 1, taken);
					int nn = 0;
					for (int k = 0; k < on && nn < GRP_CAP; k++) {
						if (taken[zs[k]][xs[k]]) continue;
						taken[zs[k]][xs[k]] = 1;
						g.x[nn] = (short)xs[k];
						g.z[nn] = (short)zs[k];
						nn++;
					}
					if (nn >= 3) { g.n = nn; g.ax = (short)bx; g.az = (short)bz; }
					else {
						for (int k = 0; k < nn; k++) taken[g.z[k]][g.x[k]] = 0;
						g.n = on;
						for (int k = 0; k < on && k < GRP_CAP; k++) { g.x[k] = ox[k]; g.z[k] = oz[k]; taken[oz[k]][ox[k]] = 1; }
					}
				}
			}
			m.grp[farI].boss = 1;
			if (vertical) {
				for (int gi = 0; gi < m.nGrp; gi++) m.grp[gi].boss = 0;
				int bestG = 0, bd = 99999;
				for (int gi = 0; gi < m.nGrp; gi++) {
					int d = Dist(m.grp[gi].ax, m.grp[gi].az, spiralEndX, spiralEndZ);
					if (d < bd) { bd = d; bestG = gi; }
				}
				m.grp[bestG].boss = 1;
			}
		}
		m.nE = 0;
		for (int g = 0; g < m.nGrp && m.nE < 10; g++) {
			m.eStartX[m.nE] = m.grp[g].ax;
			m.eStartZ[m.nE] = m.grp[g].az;
			m.nE++;
		}
		if (m.nE > 0) { eax = m.eStartX[0]; eaz = m.eStartZ[0]; }
		// 拠点同士と味方を道でつなぐ（視覚と歩行の両方）
		int linkX[MAX_GROUPS + 1], linkZ[MAX_GROUPS + 1], ln = 0;
		linkX[ln] = pax; linkZ[ln] = paz; ln++;
		BYTE linked[MAX_GROUPS] = {};
		for (int step = 0; step < m.nGrp; step++) {
			int best = -1, bd = 99999, bx = pax, bz = paz;
			for (int g = 0; g < m.nGrp; g++) if (!linked[g]) {
				for (int s = 0; s < ln; s++) {
					int d = Dist(m.grp[g].ax, m.grp[g].az, linkX[s], linkZ[s]);
					if (d < bd) { bd = d; best = g; bx = linkX[s]; bz = linkZ[s]; }
				}
			}
			if (best < 0) break;
			linked[best] = 1;
			int rx[MAPN * 4], rz[MAPN * 4];
			int np2 = CarveRoad(m, bx, bz, m.grp[best].ax, m.grp[best].az, rx, rz, MAPN * 4);
			for (int i = 0; i < np2; i++) {
				Cell& c = m.c[rz[i]][rx[i]];
				if (c.t == T_VOID) continue;
				if (c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM) c.t = T_BRIDGE;
				else if (c.t == T_MOUNTAIN || c.t == T_ROCK) c.t = (BYTE)(tp.road ? T_ROAD : tp.ground);
				else if (tp.road && c.t != T_BRIDGE && c.t != T_RUINS) c.t = T_ROAD;
				if (!vertical && i > 0) {
					int ph = m.c[rz[i - 1]][rx[i - 1]].h;
					if ((int)c.h > ph + 1) c.h = (BYTE)(ph + 1);
					if ((int)c.h < ph - 1) c.h = (BYTE)max(1, ph - 1);
				}
			}
			if (ln < MAX_GROUPS + 1) { linkX[ln] = m.grp[best].ax; linkZ[ln] = m.grp[best].az; ln++; }
		}
	}

	if (th == TH_ORBIT || th == TH_STATION || th == TH_CITADEL || th == TH_LANTERN) {
		auto disc = [&](int cx, int cz, int rad, int t, int dh) {
			for (int zz = cz - rad; zz <= cz + rad; zz++) for (int xx = cx - rad; xx <= cx + rad; xx++) {
				if (!Map::In(xx, zz)) continue;
				int dx = xx - cx, dz = zz - cz;
				if (dx * dx + dz * dz > rad * rad) continue;
				Cell& c = m.c[zz][xx];
				if (c.t == T_VOID || c.t == T_BRIDGE) continue;
				if ((t == T_ROCK || t == T_WATER || t == T_CHASM) && NearPartyOrGroup(m, xx, zz, 1)) continue;
				c.t = (BYTE)t;
				c.h = (BYTE)Clampi((int)c.h + dh, 1, MAXH - 2);
			}
		};
		if (th == TH_ORBIT) {
			disc(18, 20, 5, T_HILL, 2);
			disc(46, 40, 3, T_ROCK, 1);
			disc(30, 48, 4, T_RUINS, 0);
			for (int z = 2; z < MAPN - 2; z++) for (int x = 2; x < MAPN - 2; x++) {
				Cell& c = m.c[z][x];
				if (c.t == T_PLAIN && (c.var % 9) == 0 && !NearPartyOrGroup(m, x, z, 1)) c.t = T_ROCK;
				else if (c.t == T_PLAIN && (c.var % 5) == 0) c.t = T_RUINS;
			}
		} else if (th == TH_STATION) {
			for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
				Cell& c = m.c[z][x];
				if (c.t == T_PLAIN || c.t == T_HILL) c.t = T_RUINS;
				if ((x % 8) == 0 || (z % 8) == 0) {
					if (c.t != T_CHASM && c.t != T_WATER && c.t != T_VOID) c.t = T_ROAD;
				}
			}
			disc(MAPN / 2, MAPN / 2, 6, T_RUINS, 1);
			disc(16, 16, 3, T_HILL, 2);
			disc(48, 18, 3, T_HILL, 2);
		} else if (th == TH_CITADEL) {
			for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
				Cell& c = m.c[z][x];
				if (c.t == T_PLAIN) c.t = T_RUINS;
				int dx = x - MAPN / 2, dz = z - MAPN / 2;
				int ad = dx < 0 ? -dx : dx;
				int az = dz < 0 ? -dz : dz;
				if ((ad == 10 || az == 10) && ad < 16 && az < 16 && c.t != T_BRIDGE && !NearPartyOrGroup(m, x, z, 1)) c.t = T_ROCK;
				if ((ad > 14 && ad < 17) || (az > 14 && az < 17)) {
					if (c.t != T_BRIDGE && (x % 6) != 0 && (z % 6) != 0 && !NearPartyOrGroup(m, x, z, 1)) c.t = T_WATER;
				}
			}
			disc(MAPN / 2, MAPN / 2, 4, T_RUINS, 2);
		} else {
			for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
				int dx = x - MAPN / 2, dz = z - MAPN / 2;
				Cell& c = m.c[z][x];
				if (dx * dx + dz * dz > 16 * 16 && c.t != T_BRIDGE && c.t != T_VOID && !NearPartyOrGroup(m, x, z, 1)) c.t = T_WATER;
				else if (c.t == T_PLAIN) c.t = T_SNOW;
			}
			disc(20, 22, 3, T_HILL, 1);
			disc(42, 36, 3, T_RUINS, 1);
			disc(28, 44, 2, T_RUINS, 2);
		}
		(void)r;
	}
	// 8) 小物
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		Cell& c = m.c[z][x];
		c.prop = PK_NONE;
		if (th >= TH_SEABED) {
			if (NearPartyOrGroup(m, x, z, 1)) continue;
			if (c.t == T_ROCK || c.t == T_MOUNTAIN) c.prop = PK_KIT;
			else if (c.t != T_VOID && c.t != T_WATER && c.t != T_LAVA && c.t != T_BRIDGE && c.t != T_CHASM && c.t != T_DOOR && (c.var % 17) == 0)
				c.prop = PK_KIT;
			continue;
		}
		switch (c.t) {
		case T_FOREST:
			if (th == TH_SEABED) c.prop = (c.var & 1) ? PK_LILY : PK_ROCK;
			else if (th == TH_SNOW) c.prop = PK_PINE;
			else if (th == TH_DESERT || th == TH_VOLCANO) c.prop = PK_DEAD;
			else if (th == TH_SWAMP) c.prop = (c.var & 3) == 0 ? PK_DEAD : PK_TREE;
			else c.prop = (c.var & 3) == 0 ? PK_TREES : ((c.var & 7) == 1 ? PK_HILLTREES : PK_TREE);
			break;
		case T_MOUNTAIN: c.prop = PK_MOUNT; break;
		case T_ROCK:
			if (th == TH_RUINS || th == TH_CITADEL) c.prop = PK_PILLAR;
			else if (th == TH_ORBIT || th == TH_STATION) c.prop = (c.var & 1) ? PK_ROCK : PK_PILLAR;
			else if (th == TH_SWAMP || th == TH_VOLCANO) c.prop = (c.var & 1) ? PK_GRAVE : PK_ROCK;
			else c.prop = PK_ROCK;
			break;
		case T_WATER:
			if ((th == TH_SWAMP || th == TH_PLAINS || th == TH_FOREST || th == TH_FALLS || th == TH_SEABED) && (c.var % 5) == 0) c.prop = PK_LILY;
			break;
		case T_RUINS:
			if (th == TH_ORBIT || th == TH_STATION || th == TH_LANTERN || (c.var % 23) == 0) c.prop = PK_LANTERN;
			else if (th == TH_CITADEL && (c.var % 11) == 0) c.prop = PK_PILLAR;
			break;
		default: break;
		}
	}
	StampLandmarks(m, r, th, taken);
	// 拠点の飾り（通行不可の小物は岩扱い）
	{
		int tx = Clampi(pax + 2, 1, MAPN - 2), tz = Clampi(paz - 2, 1, MAPN - 2);
		bool used = false;
		for (int i = 0; i < MAX_PARTY; i++) if (m.pStartX[i] == tx && m.pStartZ[i] == tz) used = true;
		if (!used && m.c[tz][tx].t != T_VOID) { m.c[tz][tx].t = T_ROCK; m.c[tz][tx].prop = (BYTE)(th >= TH_SEABED ? PK_KIT : PK_TENT); }
		int fx = Clampi(pax - 2, 1, MAPN - 2), fz = Clampi(paz + 2, 1, MAPN - 2);
		used = false;
		for (int i = 0; i < MAX_PARTY; i++) if (m.pStartX[i] == fx && m.pStartZ[i] == fz) used = true;
		if (!used && m.c[fz][fx].t != T_VOID) { m.c[fz][fx].t = T_ROCK; m.c[fz][fx].prop = (BYTE)(th >= TH_SEABED ? PK_KIT : PK_FLAG); }
		for (int k = 0; k < 3 && m.nGrp > 0; k++) {
			const MapGroup& g0 = m.grp[0];
			int cx = Clampi(g0.ax + r.Range(-4, 4), 1, MAPN - 2), cz = Clampi(g0.az + r.Range(-4, 4), 1, MAPN - 2);
			if (taken[cz][cx] || !FootPassable(m.c[cz][cx].t)) continue;
			m.c[cz][cx].t = T_ROCK;
			m.c[cz][cx].prop = (BYTE)(th >= TH_SEABED ? PK_KIT : (th == TH_SWAMP ? PK_PUMPKIN : (k & 1) ? PK_CRATE : PK_BARREL));
			taken[cz][cx] = 1;
		}
	}
	// 9) ギミックマス（回復・一定ターンのステータス倍・対になるワープ）
	{
		int spots[MAPN * MAPN], ns = 0;
		for (int z = 2; z < MAPN - 2; z++) for (int x = 2; x < MAPN - 2; x++) {
			if (taken[z][x] || !FootPassable(m.c[z][x].t)) continue;
			if (m.c[z][x].t == T_BRIDGE) continue;
			spots[ns++] = z * MAPN + x;
		}
		for (int i = ns - 1; i > 0; i--) {
			int j = r.Range(0, i);
			int tmp = spots[i]; spots[i] = spots[j]; spots[j] = tmp;
		}
		int usedS = 0;
		auto take = [&](int minD) -> int {
			for (int i = usedS; i < ns; i++) {
				int x = spots[i] % MAPN, z = spots[i] / MAPN;
				if (m.c[z][x].gimm) continue;
				bool ok = true;
				for (int p = 0; p < MAX_PARTY && ok; p++)
					if (Dist(x, z, m.pStartX[p], m.pStartZ[p]) < minD) ok = false;
				if (!ok) continue;
				spots[i] = spots[usedS];
				usedS++;
				return z * MAPN + x;
			}
			return -1;
		};
		int nHeal = Clampi(span / 8, 1, 8);
		int nMight = Clampi(span / 10, 1, 6);
		for (int i = 0; i < nHeal; i++) {
			int s = take(4);
			if (s < 0) break;
			m.c[s / MAPN][s % MAPN].gimm = GM_HEAL;
		}
		for (int i = 0; i < nMight; i++) {
			int s = take(5);
			if (s < 0) break;
			m.c[s / MAPN][s % MAPN].gimm = GM_MIGHT;
		}
		int nPair = Clampi(span / 16, 0, 6);
		int warpSep = max(6, span / 3);
		for (int id = 0; id < nPair; id++) {
			int a = take(6);
			int b = -1;
			for (int i = usedS; i < ns; i++) {
				int x = spots[i] % MAPN, z = spots[i] / MAPN;
				if (m.c[z][x].gimm) continue;
				if (a >= 0 && Dist(x, z, a % MAPN, a / MAPN) < warpSep) continue;
				b = z * MAPN + x;
				spots[i] = spots[usedS];
				usedS++;
				break;
			}
			if (a < 0 || b < 0) break;
			m.c[a / MAPN][a % MAPN].gimm = GM_WARP; m.c[a / MAPN][a % MAPN].gparam = (BYTE)id;
			m.c[b / MAPN][b % MAPN].gimm = GM_WARP; m.c[b / MAPN][b % MAPN].gparam = (BYTE)id;
		}
		int nChest = Clampi(span / 10, 1, 6);
		for (int i = 0; i < nChest; i++) {
			int s = take(5);
			if (s < 0) break;
			Cell& c = m.c[s / MAPN][s % MAPN];
			c.gimm = GM_CHEST;
			c.gparam = (BYTE)r.Range(0, 2);
			c.prop = (BYTE)(th >= TH_SEABED ? PK_KIT : PK_CRATE);
		}
		int nDoor = Clampi(span / 14, 0, 4);
		for (int i = 0; i < nDoor; i++) {
			int s = take(8);
			if (s < 0) break;
			int x = s % MAPN, z = s / MAPN;
			if (m.c[z][x].t != T_ROAD && m.c[z][x].t != T_PLAIN && m.c[z][x].t != T_RUINS) continue;
			BYTE was = m.c[z][x].t;
			m.c[z][x].t = T_DOOR;
			m.c[z][x].gimm = GM_NONE;
			m.c[z][x].prop = PK_NONE;
			if (m.nE > 0 && !FootReachable(m, m.pStartX[0], m.pStartZ[0], m.eStartX[0], m.eStartZ[0])) {
				m.c[z][x].t = was;
			}
		}
		int link = 1;
		static const int ldx[4] = { 1, -1, 0, 0 }, ldz[4] = { 0, 0, 1, -1 };
		for (int z = 1; z < MAPN - 1 && link <= 4; z++) for (int x = 1; x < MAPN - 1 && link <= 4; x++) {
			if (m.c[z][x].t != T_DOOR || m.c[z][x].gimm == GM_FENCE || m.c[z][x].gparam) continue;
			if (!r.Chance(50)) continue;
			int lx = -1, lz = -1, best = 9999;
			for (int k = 0; k < 4; k++) {
				int nx = x + ldx[k], nz = z + ldz[k];
				if (!Map::In(nx, nz) || m.c[nz][nx].gimm) continue;
				if (!FootPassable(m.c[nz][nx].t) || m.c[nz][nx].t == T_BRIDGE) continue;
				int d = abs(nx - m.pStartX[0]) + abs(nz - m.pStartZ[0]);
				if (d < best) { best = d; lx = nx; lz = nz; }
			}
			if (lx < 0) continue;
			m.c[z][x].gparam = (BYTE)link;
			m.c[lz][lx].gimm = GM_LEVER;
			m.c[lz][lx].gparam = (BYTE)link;
			m.c[lz][lx].prop = (BYTE)(th >= TH_SEABED ? PK_KIT : PK_LANTERN);
			link++;
		}
		int nFence = Clampi(span / 16, 0, 3);
		for (int i = 0; i < nFence; i++) {
			int s = take(6);
			if (s < 0) break;
			int x = s % MAPN, z = s / MAPN;
			if (m.c[z][x].t != T_ROAD && m.c[z][x].t != T_PLAIN && m.c[z][x].t != T_RUINS) continue;
			if (m.c[z][x].gimm) continue;
			BYTE was = m.c[z][x].t;
			m.c[z][x].t = T_DOOR;
			m.c[z][x].gimm = GM_FENCE;
			m.c[z][x].gparam = 0;
			m.c[z][x].prop = PK_NONE;
			if (m.nE > 0 && !FootReachable(m, m.pStartX[0], m.pStartZ[0], m.eStartX[0], m.eStartZ[0])) {
				m.c[z][x].t = was;
				m.c[z][x].gimm = GM_NONE;
			}
		}
	}
	if (m.nP > 0) {
		EnsureClimbPath(m, m.pStartX[0], m.pStartZ[0], pax, paz);
		for (int g = 0; g < m.nGrp; g++)
			EnsureClimbPath(m, m.pStartX[0], m.pStartZ[0], m.grp[g].ax, m.grp[g].az);
	}
	if (m.nP > 0 && m.nE > 0) {
		int nDrop = 0;
		for (int z = 1; z < MAPN - 1 && nDrop < 3; z++) for (int x = 1; x < MAPN - 1 && nDrop < 3; x++) {
			Cell& c = m.c[z][x];
			if (c.t != T_BRIDGE || c.gimm || c.gparam) continue;
			if ((((x + z * 13) ^ (int)m.seed) & 3) != 0) continue;
			bool held = false;
			for (int i = 0; i < m.nP && !held; i++) if (m.pStartX[i] == x && m.pStartZ[i] == z) held = true;
			for (int i = 0; i < m.nE && !held; i++) if (m.eStartX[i] == x && m.eStartZ[i] == z) held = true;
			for (int g = 0; g < m.nGrp && !held; g++) if (m.grp[g].ax == x && m.grp[g].az == z) held = true;
			if (held) continue;
			BYTE was = c.t;
			c.t = T_CHASM;
			bool ok = FootReachable(m, m.pStartX[0], m.pStartZ[0], m.eStartX[0], m.eStartZ[0]);
			c.t = was;
			if (!ok) continue;
			c.gimm = GM_DROP;
			c.var = 2;
			nDrop++;
		}
	}
	{
		int roll = (int)(m.seed % 5u);
		if (roll != 0)
			StampCaves(m, r, tp, pax, paz, roll <= 2);
	}
	ReapplyShape(m);
	OpenStandPads(m);
	for (int z = 1; z < MAPN - 1; z++) for (int x = 1; x < MAPN - 1; x++) {
		Cell& c = m.c[z][x];
		if (c.t != T_WATER || c.gimm) continue;
		int shore = 0;
		static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz) || m.c[nz][nx].t != T_WATER) shore = 1;
		}
		c.gparam = shore ? 0 : (BYTE)(1 + ((x * 3 + z * 5 + (int)m.seed) & 3));
		if (!shore && ((x / 3 + z / 3) & 1) == 0) c.gparam = 3;
	}
	CutLooseIslands(m);
	OpenStandPads(m);
	if (m.nP > 0) {
		for (int g = 0; g < m.nGrp; g++) if (m.grp[g].n > 0)
			EnsureClimbPath(m, m.pStartX[0], m.pStartZ[0], m.grp[g].x[0], m.grp[g].z[0]);
	}
}

void GenerateMap(Map& m, int stage, DWORD seed, int theme)
{
	for (int attempt = 0; attempt < 60; attempt++) {
		GenerateMapBody(m, stage, seed + (DWORD)attempt * 9973u, theme);
		ConnectFeet(m);
		if (FeetLinked(m)) return;
	}
	// 6 回とも全足場が繋がらない配置（例: 3マス幅の通路を 3x3 の障害物が塞ぐ）は、
	// 障害物を貫く平地回廊を強制的に掘って、スタート→ボス（全ての敵足場）を必ず歩けるようにする。
	// FeetLinked が通る＝立てる全マスが一繋がり＝クリア不能マップを出さない保証。
	ForceConnectFeet(m);
}

// ============================================================ クラス
#define L6(a,b,c,d,e,f,g,h) { {a,b},{c,d},{e,f},{g,h},{0,-1},{0,-1} }
#define L6F(a,b,c,d,e,f,g,h,i,j,k,l) { {a,b},{c,d},{e,f},{g,h},{i,j},{k,l} }
static ClassDef kClass[C_PLAYER_COUNT] = {
	// name, desc, tier, magic, group, move, jump, rMin, rMax, mag, pow, look, style, base[10], grow[10], learn
	{ TX_CL_SOLDIER, TX_CLD_SOLDIER, 1, 0, G_MELEE, MV_FOOT, 2, 1, 1, 0, 5, WL_SWORD_SHIELD, AS_SLASH,
	{ 24, 8, 7, 6, 1, 2, 6, 6, 4, 5 }, { 45, 20, 55, 50, 5, 25, 40, 45, 35, 0 }, L6F(1, SKL_POWER, 2, SKL_BASH, 3, SKL_P_IRON, 4, SKL_VITAL, 5, SKL_CLEAVE, 6, SKL_DOUBLE) },
	{ TX_CL_FIGHTER, TX_CLD_FIGHTER, 1, 0, G_MELEE, MV_FOOT, 2, 1, 1, 0, 7, WL_AXE2, AS_CHOP2,
	{ 28, 8, 9, 4, 0, 1, 5, 5, 3, 5 }, { 55, 25, 65, 35, 0, 15, 35, 40, 30, 0 }, L6F(1, SKL_POWER, 2, SKL_P_MIGHT, 3, SKL_DOUBLE, 4, SKL_WHIRL, 5, SKL_BASH, 6, SKL_CLEAVE) },
	{ TX_CL_ARCHER, TX_CLD_ARCHER, 1, 0, G_SWIFT, MV_FOOT, 2, 2, 5, 0, 5, WL_CROSSBOW2, AS_SHOOT,
	  { 20, 12, 6, 3, 1, 3, 7, 9, 5, 5 }, { 40, 25, 45, 30, 5, 25, 50, 60, 40, 0 }, L6F(1, SKL_PIERCE, 2, SKL_P_CRIT, 3, SKL_ARROWRAIN, 4, SKL_STITCH, 5, SKL_SNIPE, 6, SKL_VITAL) },
	{ TX_CL_THIEF, TX_CLD_THIEF, 1, 0, G_SWIFT, MV_FOOT, 3, 1, 1, 0, 4, WL_KNIVES, AS_STAB,
	{ 19, 10, 5, 3, 1, 3, 10, 8, 8, 6 }, { 38, 20, 40, 25, 5, 25, 65, 55, 55, 0 }, L6F(1, SKL_VITAL, 2, SKL_P_EVADE, 3, SKL_STITCH, 4, SKL_P_SWIFT, 5, SKL_VENOMEDGE, 6, SKL_DOUBLE) },
	{ TX_CL_MAGE, TX_CLD_MAGE, 1, 1, G_MAGIC, MV_FOOT, 2, 1, 2, 1, 4, WL_STAFF, AS_CAST,
	  { 17, 14, 1, 2, 9, 7, 6, 6, 4, 4 }, { 30, 60, 5, 20, 65, 45, 40, 40, 35, 0 }, L6F(1, SKL_FIRE, 2, SKL_THUNDER, 3, SKL_BLIZZARD, 4, SKL_GRAVITY, 5, SKL_P_SPELL, 6, SKL_DARKNESS) },
	{ TX_CL_CLERIC, TX_CLD_CLERIC, 1, 1, G_HOLY, MV_FOOT, 2, 1, 2, 1, 4, WL_WAND_BOOK, AS_CAST,
	  { 18, 16, 2, 3, 9, 9, 5, 5, 6, 4 }, { 32, 65, 10, 22, 65, 60, 35, 35, 45, 0 }, L6F(1, SKL_HEAL, 1, SKL_SMITE, 2, SKL_PROTECT, 4, SKL_HOLY, 5, SKL_HIGHHEAL, 6, SKL_REFRESH) },

	{ TX_CL_PALADIN, TX_CLD_PALADIN, 2, 0, G_HOLY, MV_FOOT, 2, 1, 1, 0, 8, WL_SWORD_BADGE, AS_SLASH,
	  { 34, 10, 11, 11, 4, 7, 7, 8, 6, 5 }, { 55, 25, 55, 60, 15, 40, 35, 45, 40, 0 }, L6F(15, SKL_HOLYBLADE, 18, SKL_PROTECT, 22, SKL_SMITE, 26, SKL_P_IRON, 30, SKL_HEAL, 34, SKL_BLESS) },
	{ TX_CL_RUNEKNIGHT, TX_CLD_RUNEKNIGHT, 2, 1, G_MAGIC, MV_FOOT, 2, 1, 1, 0, 7, WL_SWORD_BOOK, AS_SLASH,
	  { 30, 16, 10, 8, 10, 8, 8, 8, 5, 5 }, { 48, 40, 45, 40, 45, 40, 40, 40, 35, 0 }, L6F(15, SKL_RUNEBLADE, 18, SKL_FIRE, 22, SKL_THUNDER, 26, SKL_BLIZZARD, 30, SKL_P_SPELL, 34, SKL_DARKNESS) },
	{ TX_CL_BERSERKER, TX_CLD_BERSERKER, 2, 0, G_MELEE, MV_FOOT, 2, 1, 1, 0, 10, WL_AXES, AS_SLASH,
	  { 40, 12, 15, 6, 0, 3, 8, 7, 3, 5 }, { 70, 20, 75, 30, 0, 15, 40, 40, 25, 0 }, L6F(15, SKL_DOUBLE, 18, SKL_WHIRL, 22, SKL_WARCRY, 26, SKL_P_MIGHT, 30, SKL_DRAGONFANG, 34, SKL_CLEAVE) },
	{ TX_CL_SNIPER, TX_CLD_SNIPER, 2, 0, G_SWIFT, MV_FOOT, 2, 2, 6, 0, 7, WL_CROSSBOW2, AS_SHOOT,
	  { 26, 16, 9, 5, 1, 5, 9, 14, 7, 5 }, { 42, 25, 50, 30, 5, 30, 50, 70, 45, 0 }, L6(15, SKL_PIERCE, 18, SKL_ARROWRAIN, 24, SKL_P_CRIT, 30, SKL_SNIPE) },
	{ TX_CL_ASSASSIN, TX_CLD_ASSASSIN, 2, 0, G_SWIFT, MV_FOOT, 3, 1, 1, 0, 7, WL_KNIVES, AS_STAB,
	  { 24, 8, 9, 5, 2, 5, 14, 12, 10, 6 }, { 40, 20, 50, 25, 10, 30, 70, 60, 55, 0 }, L6(15, SKL_ASSASSINATE, 18, SKL_VENOMEDGE, 24, SKL_P_EVADE, 30, SKL_P_SWIFT) },
	{ TX_CL_WIZARD, TX_CLD_WIZARD, 2, 1, G_MAGIC, MV_FOOT, 2, 1, 3, 1, 6, WL_STAFF, AS_CAST,
	  { 22, 26, 1, 4, 15, 11, 8, 8, 5, 4 }, { 32, 70, 5, 22, 75, 50, 40, 40, 35, 0 }, L6F(15, SKL_BLIZZARD, 18, SKL_THUNDER, 22, SKL_GRAVITY, 26, SKL_THUNDERFIELD, 30, SKL_DARKNESS, 34, SKL_P_SPELL) },
	{ TX_CL_BISHOP, TX_CLD_BISHOP, 2, 1, G_HOLY, MV_FOOT, 2, 1, 2, 1, 4, WL_WAND_BOOK, AS_CAST,
	  { 24, 28, 3, 5, 16, 14, 7, 7, 8, 4 }, { 35, 70, 10, 25, 75, 65, 35, 35, 50, 0 }, { { 15, SKL_SMITE }, { 16, SKL_HOLY }, { 20, SKL_HIGHHEAL }, { 24, SKL_HEALALL }, { 28, SKL_BLESS }, { 32, SKL_P_REGEN } } },
	{ TX_CL_SKYKNIGHT, TX_CLD_SKYKNIGHT, 2, 0, G_SWIFT, MV_FLY, 9, 1, 1, 0, 7, WL_SWORD_SPIKE, AS_STAB,
	  { 28, 8, 10, 7, 3, 8, 11, 9, 7, 7 }, { 45, 20, 50, 35, 10, 40, 60, 50, 45, 0 }, L6F(15, SKL_VITAL, 18, SKL_PIERCE, 22, SKL_P_EVADE, 26, SKL_HASTE, 30, SKL_DRAGONFANG, 34, SKL_SNIPE) },

	{ TX_CL_HERO, TX_CLD_HERO, 3, 0, G_MELEE, MV_FOOT, 3, 1, 1, 0, 12, WL_SWORD2, AS_CHOP2,
	  { 46, 14, 17, 13, 6, 10, 12, 13, 10, 6 }, { 60, 30, 65, 50, 20, 40, 50, 55, 50, 0 }, L6F(35, SKL_BRAVE, 38, SKL_P_CRIT, 42, SKL_HOLYBLADE, 46, SKL_P_REGEN, 50, SKL_GODSLASH, 54, SKL_CLEAVE) },
	{ TX_CL_ARCHMAGE, TX_CLD_ARCHMAGE, 3, 1, G_MAGIC, MV_FOOT, 2, 1, 3, 1, 9, WL_STAFF, AS_CAST,
	  { 30, 40, 2, 7, 22, 16, 10, 10, 8, 5 }, { 38, 80, 5, 25, 85, 55, 45, 45, 40, 0 }, L6F(35, SKL_RUIN, 38, SKL_STORM, 42, SKL_NOVA, 46, SKL_METEOR, 50, SKL_APOC, 54, SKL_P_SPELL) },
	{ TX_CL_SAINT, TX_CLD_SAINT, 3, 1, G_HOLY, MV_FOOT, 2, 1, 2, 1, 6, WL_WAND_BOOK, AS_CAST,
	  { 32, 42, 4, 8, 24, 22, 9, 9, 12, 5 }, { 40, 80, 10, 30, 85, 75, 40, 40, 60, 0 }, { { 35, SKL_HOLY }, { 36, SKL_HEALALL }, { 40, SKL_SANCTUARY }, { 44, SKL_BLESS }, { 48, SKL_RENEW }, { 52, SKL_P_REGEN } } },
	{ TX_CL_PHANTOM, TX_CLD_PHANTOM, 3, 0, G_SWIFT, MV_FOOT, 4, 1, 4, 0, 9, WL_CROSS_KNIFE, AS_SHOOT1,
	  { 32, 12, 13, 8, 4, 8, 18, 16, 14, 7 }, { 45, 25, 55, 30, 15, 35, 80, 65, 65, 0 }, L6F(35, SKL_ASSASSINATE, 38, SKL_SNIPE, 42, SKL_ARROWRAIN, 46, SKL_VENOMEDGE, 50, SKL_P_SWIFT, 54, SKL_P_EVADE) },
	{ TX_CL_DRAGONLORD, TX_CLD_DRAGONLORD, 3, 0, G_MELEE, MV_FLY, 9, 1, 1, 0, 12, WL_SWORD2, AS_CHOP2,
	  { 44, 12, 16, 14, 5, 10, 12, 11, 8, 7 }, { 60, 25, 65, 55, 10, 40, 45, 45, 40, 0 }, L6F(35, SKL_DRAGONFANG, 38, SKL_WHIRL, 42, SKL_P_IRON, 46, SKL_BRAVE, 50, SKL_GODSLASH, 54, SKL_CLEAVE) },
	{ TX_CL_ALCHEMIST, TX_CLD_ALCHEMIST, 2, 1, G_MAGIC, MV_FOOT, 2, 1, 2, 1, 5, WL_STAFF, AS_CAST,
	  { 24, 22, 4, 5, 12, 10, 8, 9, 7, 4 }, { 36, 55, 15, 28, 55, 50, 40, 45, 40, 0 }, { { 15, SKL_GRAVITY }, { 18, SKL_TRANSMUTE }, { 22, SKL_HIGHHEAL }, { 26, SKL_MIRROR }, { 30, SKL_FIRE }, { 34, SKL_SANCTUARY } } },
	{ TX_CL_ARCHALCHEMIST, TX_CLD_ARCHALCHEMIST, 3, 1, G_MAGIC, MV_FOOT, 2, 1, 3, 1, 8, WL_STAFF, AS_CAST,
	  { 32, 36, 6, 8, 18, 16, 10, 12, 10, 5 }, { 42, 70, 20, 32, 70, 60, 45, 50, 50, 0 }, { { 35, SKL_STORM }, { 40, SKL_RUIN }, { 46, SKL_METEOR }, { 52, SKL_APOC }, { 0, -1 }, { 0, -1 } } },
};
#undef L6
#undef L6F
static const int kLineParent[TAC_RANK_LINES] = {
	C_HERO, C_ARCHMAGE, C_SAINT, C_PHANTOM, C_DRAGONLORD, C_ARCHALCHEMIST, C_GLADIATOR, C_SHADOW
};
// 段が上がるほど強い技。英雄系に終焉魔法、魔導系に初級は置かない。
static const int kExtSkill[TAC_RANK_LINES][TAC_RANK_N] = {
	{ SKL_CLEAVE, SKL_GRANDSLASH, SKL_BRAVE, SKL_GODSLASH, SKL_LINESLASH, SKL_FANSLASH, SKL_RAMPAGE },
	{ SKL_FROSTFIELD, SKL_STORM, SKL_RUIN, SKL_METEOR, SKL_APOC, SKL_THUNDERFIELD, SKL_GRAVITY },
	{ SKL_HEALALL, SKL_SANCTUARY, SKL_RENEW, SKL_NOVA, SKL_SHOWER, SKL_SHINE, SKL_ESUNA },
	{ SKL_PIERCE, SKL_SNIPE, SKL_ARROWRAIN, SKL_ASSASSINATE, SKL_ARROWLINE, SKL_ARROWFAN, SKL_CRESCENT },
	{ SKL_WHIRL, SKL_DRAGONFANG, SKL_EARTHREND, SKL_BLOODAXE, SKL_COLOSSMASH, SKL_IRONHOWL, SKL_AXEWHIRL },
	{ SKL_TRANSMUTE, SKL_MIASMA, SKL_ELFIRE, SKL_ELICE, SKL_ELTHUNDER, SKL_ELEATH, SKL_ELPOISON },
	{ SKL_FISTGOD, SKL_WARLORD, SKL_BREAKKING, SKL_STEELGOD, SKL_HEGEFIST, SKL_PEERLESS, SKL_ENDFIST },
	{ SKL_GHOSTHAND, SKL_SHADEEMP, SKL_TRACELESS, SKL_NIGHTEMP, SKL_VOIDTHIEF, SKL_LASTNIGHT, SKL_ETERNIGHT },
};
static ClassDef s_ext[TAC_RANK_LINES * TAC_RANK_N];
static ClassDef s_third[2];
static int s_extOk = 0;
static int s_thirdOk = 0;
// 後半 10 能力と移動の成長。表の 0 は未設定なので、職の傾向から一度だけ埋める。
static void FillUnitSheet(int* base, int* grow, int group, int magic, int fly, int jump, int rMax)
{
	if (grow[S_MOV] == 0) {
		if (fly) grow[S_MOV] = 10;
		else if (base[S_MOV] <= 2) grow[S_MOV] = 4;
		else if (group == G_SWIFT) grow[S_MOV] = 14;
		else if (magic) grow[S_MOV] = 6;
		else grow[S_MOV] = 8;
	}
	if (base[S_JMP] == 0 && grow[S_JMP] == 0) {
		base[S_JMP] = max(1, jump);
		grow[S_JMP] = fly ? 3 : (group == G_SWIFT ? 6 : 4);
	}
	if (base[S_HIT] == 0 && grow[S_HIT] == 0) {
		base[S_HIT] = 4 + base[S_DEX] / 4;
		grow[S_HIT] = 18 + (group == G_SWIFT ? 16 : 0) + (magic ? 6 : 0);
	}
	if (base[S_AVO] == 0 && grow[S_AVO] == 0) {
		base[S_AVO] = base[S_AGI] / 4 + (group == G_SWIFT ? 3 : 0);
		grow[S_AVO] = group == G_SWIFT ? 16 : (fly ? 12 : 6);
	}
	if (base[S_CRT] == 0 && grow[S_CRT] == 0) {
		base[S_CRT] = base[S_LUK] / 4 + ((group == G_SWIFT && !magic) ? 3 : 0);
		grow[S_CRT] = (rMax >= 4 || (group == G_SWIFT && !magic)) ? 14 : 5;
	}
	if (base[S_GRD] == 0 && grow[S_GRD] == 0) {
		base[S_GRD] = base[S_DEF] >= 8 ? 4 : base[S_DEF] / 5;
		grow[S_GRD] = base[S_DEF] >= 8 ? 8 : 3;
	}
	if (base[S_RCV] == 0 && grow[S_RCV] == 0) {
		base[S_RCV] = (group == G_HOLY) ? 2 : 0;
		grow[S_RCV] = (group == G_HOLY) ? 8 : 2;
	}
	if (base[S_MPR] == 0 && grow[S_MPR] == 0) {
		base[S_MPR] = magic ? 1 : 0;
		grow[S_MPR] = magic ? 8 : 2;
	}
	if (base[S_CPL] == 0 && grow[S_CPL] == 0) {
		base[S_CPL] = 2;
		grow[S_CPL] = 8 + (group == G_MELEE ? 4 : 0);
	}
	if (base[S_RNG] == 0 && grow[S_RNG] == 0) {
		base[S_RNG] = 0;
		grow[S_RNG] = (rMax >= 4) ? 4 : (magic ? 3 : 1);
	}
	if (base[S_PEN] == 0 && grow[S_PEN] == 0) {
		base[S_PEN] = (!magic && base[S_STR] >= 9) ? 4 : 0;
		grow[S_PEN] = (!magic && base[S_STR] >= 9) ? 10 : 4;
	}
}
// Lv1 が同Lvの敵の通常攻撃で落ちないよう、初期能力を底上げする。移動は職のまま。
static void RaiseStartBase(int* base)
{
	base[S_HP] += 18;
	base[S_MP] += 6;
	base[S_STR] += 3;
	base[S_DEF] += 3;
	base[S_MAG] += 3;
	base[S_RES] += 3;
	base[S_AGI] += 2;
	base[S_DEX] += 2;
	base[S_LUK] += 2;
}
static int s_sheet = 0;
static void FillMoreLearn(ClassDef& d, int salt)
{
	int off = 0;
	if (d.group == G_SWIFT) off = 24;
	else if (d.group == G_MAGIC) off = 48;
	else if (d.group == G_HOLY) off = 72;
	int tier = d.tier < 1 ? 1 : d.tier;
	int arrive = tier <= 1 ? 1 : PromoteLevel(tier - 1);
	int next = PromoteLevel(tier);
	if (next >= 9999 || next <= arrive) next = arrive + 80;
	int span = next - arrive;
	int want = tier >= 8 ? 2 : 4;
	int start = (salt * 5) % 24;
	int placed = 0;
	for (int i = 0; i < 24 && placed < want; i++) {
		int sk = SKL_MORE0 + off + ((start + i) % 24);
		int dup = 0;
		for (int j = 0; j < LEARN_N; j++) if (d.learn[j].skill == sk) dup = 1;
		if (dup) continue;
		int slot = -1;
		for (int j = 0; j < LEARN_N; j++) if (d.learn[j].skill < 0 || d.learn[j].lv <= 0) { slot = j; break; }
		if (slot < 0) return;
		int lv = arrive + span * (placed + 1) / (want + 1);
		if (lv <= arrive) lv = arrive + 1;
		if (lv >= next) lv = next - 1;
		d.learn[slot].lv = lv;
		d.learn[slot].skill = sk;
		placed++;
	}
}
static void EnsureSheets()
{
	if (s_sheet) return;
	s_sheet = 1;
	for (int i = 0; i < C_PLAYER_COUNT; i++) {
		ClassDef& d = kClass[i];
		RaiseStartBase(d.base);
		FillUnitSheet(d.base, d.grow, d.group, d.magic, d.moveType == MV_FLY, d.jump, d.rMax);
	}
	auto put = [](int cls, int slot, int lv, int sk) {
		kClass[cls].learn[slot].lv = lv;
		kClass[cls].learn[slot].skill = sk;
	};
	put(C_SOLDIER, 6, 8, SKL_CRESCENT);
	put(C_FIGHTER, 6, 8, SKL_CRESCENT);
	put(C_ARCHER, 6, 8, SKL_SHOWER);
	put(C_ARCHER, 7, 12, SKL_SHINE);
	put(C_MAGE, 6, 8, SKL_PROTECT);
	put(C_MAGE, 7, 5, SKL_GALE);
	put(C_PALADIN, 6, 32, SKL_CRESCENT);
	put(C_RUNEKNIGHT, 6, 28, SKL_THUNDERFIELD);
	put(C_BERSERKER, 6, 28, SKL_GRANDSLASH);
	put(C_SNIPER, 4, 22, SKL_SHOWER);
	put(C_SNIPER, 5, 28, SKL_SHINE);
	put(C_WIZARD, 6, 30, SKL_THUNDERFIELD);
	put(C_WIZARD, 7, 24, SKL_HASTE);
	put(C_BISHOP, 6, 30, SKL_SANCTUARY);
	put(C_HERO, 6, 48, SKL_GRANDSLASH);
	put(C_ARCHMAGE, 6, 48, SKL_RENEW);
	put(C_SAINT, 6, 46, SKL_NOVA);
	put(C_PHANTOM, 6, 44, SKL_SHOWER);
	put(C_PHANTOM, 7, 50, SKL_SHINE);
	put(C_DRAGONLORD, 6, 46, SKL_GRANDSLASH);
	put(C_ALCHEMIST, 6, 28, SKL_FROSTFIELD);
	put(C_ALCHEMIST, 7, 32, SKL_THUNDERFIELD);
	put(C_ARCHALCHEMIST, 4, 44, SKL_STORM);
	put(C_ARCHALCHEMIST, 5, 48, SKL_THUNDERFIELD);
	put(C_PALADIN, 8, 20, SKL_LINESLASH);
	put(C_PALADIN, 9, 36, SKL_HEALBEAM);
	put(C_RUNEKNIGHT, 8, 20, SKL_THUNDERBEAM);
	put(C_RUNEKNIGHT, 9, 36, SKL_FIREBEAM);
	put(C_BERSERKER, 8, 20, SKL_FANSLASH);
	put(C_BERSERKER, 9, 36, SKL_LINESLASH);
	put(C_SNIPER, 6, 20, SKL_ARROWLINE);
	put(C_SNIPER, 7, 32, SKL_ARROWFAN);
	put(C_ASSASSIN, 6, 20, SKL_LINESLASH);
	put(C_ASSASSIN, 7, 26, SKL_FANSLASH);
	put(C_WIZARD, 8, 20, SKL_FIREBEAM);
	put(C_WIZARD, 9, 28, SKL_ICEBEAM);
	put(C_BISHOP, 7, 18, SKL_HEALBEAM);
	put(C_BISHOP, 8, 26, SKL_HEALFAN);
	put(C_SKYKNIGHT, 6, 20, SKL_LINESLASH);
	put(C_SKYKNIGHT, 7, 28, SKL_FANSLASH);
	put(C_ALCHEMIST, 8, 20, SKL_FIREBEAM);
	put(C_ALCHEMIST, 9, 36, SKL_HEALBEAM);
	put(C_HERO, 7, 40, SKL_LINESLASH);
	put(C_HERO, 8, 56, SKL_FANSLASH);
	put(C_ARCHMAGE, 7, 40, SKL_FIREBEAM);
	put(C_ARCHMAGE, 8, 48, SKL_THUNDERBEAM);
	put(C_SAINT, 7, 38, SKL_HEALBEAM);
	put(C_SAINT, 8, 50, SKL_HEALFAN);
	put(C_PHANTOM, 8, 40, SKL_ARROWLINE);
	put(C_PHANTOM, 9, 48, SKL_ARROWFAN);
	put(C_DRAGONLORD, 7, 40, SKL_LINESLASH);
	put(C_DRAGONLORD, 8, 48, SKL_FANSLASH);
	put(C_ARCHALCHEMIST, 6, 40, SKL_FIREBEAM);
	put(C_ARCHALCHEMIST, 7, 48, SKL_ICEBEAM);
	put(C_CLERIC, 6, 3, SKL_ESUNA);
	put(C_BISHOP, 9, 22, SKL_PURIFY);
	put(C_SAINT, 9, 42, SKL_PURIFY);
	put(C_ARCHALCHEMIST, 8, 42, SKL_PURIFY);
	put(C_SOLDIER, 7, 7, SKL_CLEANCUT);
	put(C_PALADIN, 7, 24, SKL_CLEANCUT);
	put(C_HERO, 9, 44, SKL_CLEANCUT);
	for (int c = 0; c < C_PLAYER_COUNT; c++) {
		for (int s = 0; s < LEARN_N; s++)
			if (kClass[c].learn[s].lv <= 0) kClass[c].learn[s].skill = -1;
		FillMoreLearn(kClass[c], c);
	}
}
static void BuildThird()
{
	if (s_thirdOk) return;
	EnsureSheets();
	auto fill = [](ClassDef& d, const ClassDef& src, int nameTx, int descTx, int weaponPow, const int* base, const int* grow, const int* lv, const int* sk) {
		d = src;
		d.tier = 3;
		d.nameTx = nameTx;
		d.descTx = descTx;
		d.weaponPow = weaponPow;
		for (int s = 0; s < 10; s++) { d.base[s] = base[s]; d.grow[s] = grow[s]; }
		for (int s = S_JMP; s <= S_PEN; s++) { d.base[s] = 0; d.grow[s] = 0; }
		RaiseStartBase(d.base);
		FillUnitSheet(d.base, d.grow, d.group, d.magic, d.moveType == MV_FLY, d.jump, d.rMax);
		for (int i = 0; i < 8; i++) { d.learn[i].lv = lv[i]; d.learn[i].skill = sk[i]; }
		for (int i = 8; i < LEARN_N; i++) { d.learn[i].lv = 0; d.learn[i].skill = -1; }
	};
	static const int bG[10] = { 48, 10, 18, 8, 0, 4, 10, 9, 4, 6 };
	static const int gG[10] = { 75, 18, 80, 32, 0, 18, 45, 42, 28, 8 };
	static const int lvG[8] = { 35, 38, 42, 46, 50, 54, 48, 52 };
	static const int skG[8] = { SKL_RAMPAGE, SKL_EARTHREND, SKL_BLOODAXE, SKL_IRONHOWL, SKL_COLOSSMASH, SKL_ARENAKING, SKL_WARDRUM, SKL_AXEWHIRL };
	static const int bS[10] = { 30, 10, 12, 6, 3, 6, 16, 14, 12, 7 };
	static const int gS[10] = { 44, 22, 55, 28, 12, 32, 75, 62, 60, 14 };
	static const int lvS[8] = { 35, 38, 42, 46, 50, 54, 48, 52 };
	static const int skS[8] = { SKL_SHADOWSTEP, SKL_SMOKESCREEN, SKL_VITALSTAB, SKL_BACKCUT, SKL_NIGHTRUN, SKL_SILENTKILL, SKL_DAGGERRAIN, SKL_DEATHMARK };
	fill(s_third[0], kClass[C_BERSERKER], TX_CL_GLADIATOR, TX_CLD_GLADIATOR, 12, bG, gG, lvG, skG);
	fill(s_third[1], kClass[C_ASSASSIN], TX_CL_SHADOW, TX_CLD_SHADOW, 9, bS, gS, lvS, skS);
	FillMoreLearn(s_third[0], 30);
	FillMoreLearn(s_third[1], 37);
	s_thirdOk = 1;
}
static void BuildExt()
{
	if (s_extOk) return;
	EnsureSheets();
	BuildThird();
	for (int line = 0; line < TAC_RANK_LINES; line++) {
		int parent = kLineParent[line];
		const ClassDef& p = parent == C_GLADIATOR ? s_third[0] : (parent == C_SHADOW ? s_third[1] : kClass[parent]);
		for (int r = 0; r < TAC_RANK_N; r++) {
			ClassDef& d = s_ext[line * TAC_RANK_N + r];
			d = p;
			int rank = 4 + r;
			d.tier = rank;
			d.nameTx = TX_RK_FIRST + line * TAC_RANK_N + r;
			d.descTx = TX_RK_LINE0 + line;
			d.weaponPow = p.weaponPow + r * 2;
			for (int s = 0; s < S_COUNT; s++) {
				if (s == S_MOV) continue;
				if (s >= S_JMP) d.base[s] = p.base[s] + (r + 1) / 2;
				else d.base[s] = p.base[s] + r * (s == S_HP || s == S_MP ? 4 : 1);
			}
			d.base[S_MOV] = p.base[S_MOV] + (r + 1) / 2;
			int arrive = PromoteLevel(rank - 1);
			int next = PromoteLevel(rank);
			if (next >= 9999 || next <= arrive) next = arrive + 80;
			int lv = arrive;
			if (rank >= 9) lv = arrive + (next - arrive) * 4 / 5;
			else if (rank >= 6) lv = arrive + (next - arrive) / 2;
			if (lv <= arrive && rank >= 6) lv = arrive + 1;
			if (lv >= next) lv = next - 1;
			for (int i = 0; i < LEARN_N; i++) { d.learn[i].lv = 0; d.learn[i].skill = -1; }
			d.learn[0].lv = lv;
			d.learn[0].skill = kExtSkill[line][r];
			FillMoreLearn(d, line * 7 + r + 3);
		}
	}
	s_extOk = 1;
}
#include "tactics_src/job_catalog.inc"
static ClassDef s_job[TAC_JOB_N];
static int s_jobOk = 0;
// 並びは kLineParent と同じ。2聖者 3幻影 4竜王 5大錬金。技の説明もこの順。
static const int kFamSig[8][4] = {
	{ SKL_HOLYBLADE, SKL_CLEAVE, SKL_BRAVE, SKL_GRANDSLASH },
	{ SKL_METEOR, SKL_FIRE, SKL_GRAVITY, SKL_RUIN },
	{ SKL_HEAL, SKL_SANCTUARY, SKL_RENEW, SKL_ESUNA },
	{ SKL_SNIPE, SKL_ARROWRAIN, SKL_PIERCE, SKL_ARROWLINE },
	{ SKL_DRAGONFANG, SKL_WHIRL, SKL_CLEAVE, SKL_BRAVE },
	{ SKL_TRANSMUTE, SKL_RUNEBLADE, SKL_MIASMA, SKL_ELPOISON },
	{ SKL_FISTGOD, SKL_BASH, SKL_WARLORD, SKL_BREAKKING },
	{ SKL_GHOSTHAND, SKL_SMOKESCREEN, SKL_NIGHTRUN, SKL_DEATHMARK },
};
static void BuildJobs()
{
	if (s_jobOk) return;
	BuildExt();
	for (int i = 0; i < TAC_JOB_N; i++) {
		int fam = kJobFam[i];
		if (fam < 0 || fam >= 8) fam = 0;
		int parent = kLineParent[fam];
		const ClassDef& p = ClassInfo(parent);
		ClassDef& d = s_job[i];
		d = p;
		d.tier = kJobTier[i];
		d.nameTx = -1;
		d.descTx = TX_CLD_JOB + i;
		if (kJobFly[i]) d.moveType = MV_FLY;
		int up = d.tier - 3;
		if (up < 0) up = 0;
		d.weaponPow = p.weaponPow + up;
		for (int s = 0; s < S_COUNT; s++) {
			if (s == S_MOV) continue;
			if (s >= S_JMP) d.base[s] = p.base[s] + (up + 1) / 2;
			else d.base[s] = p.base[s] + up * (s == S_HP || s == S_MP ? 4 : 1);
		}
		d.base[S_MOV] = p.base[S_MOV];
		for (int k = 0; k < LEARN_N; k++) { d.learn[k].lv = 0; d.learn[k].skill = -1; }
		int arrive = PromoteLevel(d.tier - 1);
		int next = PromoteLevel(d.tier);
		if (next >= 9999 || next <= arrive) next = arrive + 80;
		int skArt = SKL_ART0 + (i % 104);
		int skFam = kFamSig[fam][i & 3];
		if (d.tier <= 5) {
			d.learn[0].lv = arrive; d.learn[0].skill = skFam;
			int mid = arrive + (next - arrive) / 2;
			if (mid <= arrive) mid = arrive + 1;
			if (mid >= next) mid = next - 1;
			d.learn[1].lv = mid; d.learn[1].skill = skArt;
		} else if (d.tier <= 8) {
			int mid = arrive + (next - arrive) / 2;
			if (mid <= arrive) mid = arrive + 1;
			if (mid >= next) mid = next - 1;
			d.learn[0].lv = mid; d.learn[0].skill = skArt;
		} else {
			int late = arrive + (next - arrive) * 4 / 5;
			if (late <= arrive) late = arrive + 1;
			if (late >= next) late = next - 1;
			d.learn[0].lv = late; d.learn[0].skill = skArt;
		}
		FillMoreLearn(d, i + 11);
	}
	s_jobOk = 1;
}
int JobCount() { return TAC_JOB_N; }
const wchar_t* ClassTitle(int cls)
{
	if (cls < C_JOB0 || cls >= C_JOB0 + TAC_JOB_N) return NULL;
	const wchar_t* const* s = kJobNm[cls - C_JOB0];
	return LangPick14(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]);
}
const wchar_t* ClassBlurb(int cls)
{
	return TT(ClassInfo(cls).descTx);
}
const ClassDef& ClassInfo(int c)
{
	EnsureSheets();
	if (c >= 0 && c < C_PLAYER_COUNT) return kClass[c];
	BuildExt();
	if (c == C_GLADIATOR) return s_third[0];
	if (c == C_SHADOW) return s_third[1];
	if (c >= C_JOB0 && c < C_JOB0 + TAC_JOB_N) {
		BuildJobs();
		return s_job[c - C_JOB0];
	}
	int i = c - C_PLAYER_COUNT;
	if (i < 0 || i >= TAC_RANK_LINES * TAC_RANK_N) return kClass[0];
	return s_ext[i];
}
int PromoteLevel(int tier)
{
	// 10次で止めない。最後の段だけが門を閉じる。
	static const int k[12] = { 0, 15, 35, 55, 78, 105, 140, 180, 230, 290, 370, 470 };
	if (tier < 1) return 15;
	if (tier >= 12) return 9999;
	return k[tier];
}
int ExpToNext(int lv)
{
	if (lv < 1) lv = 1;
	if (lv >= TAC_LV_MAX) return 999999;
	return 70 + lv * 10 + lv * lv / 10;
}
void StageText(int stage, wchar_t* out, int cap)
{
	if (!out || cap < 2) return;
	if (stage > TAC_STAGE_EX)
		swprintf_s(out, (size_t)cap, L"EX%s", TacNum(stage - TAC_STAGE_EX));
	else
		wcsncpy_s(out, (size_t)cap, TacNum(stage < 0 ? 0 : stage), _TRUNCATE);
}
void StageCaption(int stage, wchar_t* out, int cap)
{
	if (!out || cap < 2) return;
	wchar_t n[40];
	StageText(stage, n, 40);
	swprintf_s(out, (size_t)cap, L"%s %s", TT(TX_BT_STAGE), n);
}
const wchar_t* TacNum(int v)
{
	static wchar_t buf[8][32];
	static int slot;
	wchar_t* out = buf[slot & 7];
	slot++;
	unsigned int x;
	int neg = v < 0;
	if (neg) x = (unsigned int)(-(v + 1)) + 1u;
	else x = (unsigned int)v;
	wchar_t tmp[32];
	int n = 0, dig = 0;
	if (x == 0) tmp[n++] = L'0';
	while (x && n < 30) {
		if (dig && (dig % 3) == 0) tmp[n++] = L',';
		tmp[n++] = (wchar_t)(L'0' + (x % 10));
		x /= 10;
		dig++;
	}
	int i = 0;
	if (neg && i < 31) out[i++] = L'-';
	while (n > 0 && i < 31) out[i++] = tmp[--n];
	out[i] = 0;
	return out;
}
int NextPromoteClass(int cls)
{
	const ClassDef& cd = ClassInfo(cls);
	if (cd.tier >= 10 || cd.tier < 3) return -1;
	if (cls >= C_PLAYER_COUNT && cls < C_PLAYER_COUNT + TAC_RANK_LINES * TAC_RANK_N) {
		int i = cls - C_PLAYER_COUNT;
		int line = i / TAC_RANK_N;
		int r = i % TAC_RANK_N;
		if (r >= TAC_RANK_N - 1) return -1;
		return C_PLAYER_COUNT + line * TAC_RANK_N + r + 1;
	}
	for (int line = 0; line < TAC_RANK_LINES; line++)
		if (kLineParent[line] == cls) return C_PLAYER_COUNT + line * TAC_RANK_N;
	return -1;
}
bool IsUltimateSkill(int skill)
{
	if (skill >= CRAFT_BASE && skill < CRAFT_BASE + CRAFT_N) return IsCraftSuper(skill);
	switch (skill) {
	case SKL_DRAGONFANG: case SKL_BRAVE: case SKL_METEOR: case SKL_ASSASSINATE:
	case SKL_GODSLASH: case SKL_RUIN: case SKL_APOC:
	case SKL_SHINE: case SKL_GRANDSLASH: case SKL_STORM: case SKL_NOVA:
	case SKL_ARENAKING: case SKL_PEERLESS: case SKL_ENDFIST:
	case SKL_SILENTKILL: case SKL_LASTNIGHT: case SKL_ETERNIGHT: return true;
	default:
		if (skill >= SKL_ART0 && skill < SKL_ARTN) {
			int lv = (skill - SKL_ART0) % 26;
			return (lv % 8) == 1 && lv >= 16;
		}
		return false;
	}
}

// ============================================================ スキル
// name, desc, kind, mp, power, rMin, rMax(-1=武器射程, 0=自分中心), area, element, hits, critBonus, effect, enemyOnly
static const SkillDef kSkill[] = {
	{ TX_SK_POWER, TX_SKD_POWER, SK_PHYS, 4, 100, -1, -1, 0, EL_NONE, 4, 0, EF_NONE, 0 },
	{ TX_SK_DOUBLE, TX_SKD_DOUBLE, SK_PHYS, 6, 80, -1, -1, 0, EL_NONE, 2, 0, EF_NONE, 0 },
	{ TX_SK_WHIRL, TX_SKD_WHIRL, SK_PHYS, 8, 100, 0, 0, 1, EL_WIND, 1, 0, EF_NONE, 0 },
	{ TX_SK_PIERCE, TX_SKD_PIERCE, SK_PHYS, 6, 90, -1, -1, 0, EL_NONE, 2, 0, EF_IGNDEF, 0 },
	{ TX_SK_STITCH, TX_SKD_STITCH, SK_PHYS, 6, 80, -1, -1, 0, EL_DARK, 2, 0, EF_SLOW, 0 },
	{ TX_SK_VITAL, TX_SKD_VITAL, SK_PHYS, 5, 90, -1, -1, 0, EL_NONE, 3, 40, EF_NONE, 0 },
	{ TX_SK_BASH, TX_SKD_BASH, SK_PHYS, 5, 90, -1, -1, 0, EL_EARTH, 2, 0, EF_STUN, 0 },
	{ TX_SK_DRAGONFANG, TX_SKD_DRAGONFANG, SK_PHYS, 14, 140, -1, -1, 0, EL_FIRE, 2, 10, EF_NONE, 0 },
	{ TX_SK_HOLYBLADE, TX_SKD_HOLYBLADE, SK_PHYS, 10, 90, 1, 2, 1, EL_HOLY, 2, 5, EF_NONE, 0 },
	{ TX_SK_ARROWRAIN, TX_SKD_ARROWRAIN, SK_PHYS, 12, 120, 2, 5, 1, EL_WIND, 3, 0, EF_NONE, 0 },
	{ TX_SK_ASSASSINATE, TX_SKD_ASSASSINATE, SK_PHYS, 12, 110, -1, -1, 0, EL_DARK, 2, 20, EF_POISON, 0 },
	{ TX_SK_BRAVE, TX_SKD_BRAVE, SK_PHYS, 16, 140, -1, -1, 0, EL_HOLY, 3, 20, EF_NONE, 0 },
	{ TX_SK_FIRE, TX_SKD_FIRE, SK_MAGIC, 4, 8, 1, 3, 0, EL_FIRE, 2, 0, EF_BURN, 0 },
	{ TX_SK_BLIZZARD, TX_SKD_BLIZZARD, SK_MAGIC, 8, 11, 1, 3, 1, EL_ICE, 3, 0, EF_FREEZE, 0 },
	{ TX_SK_THUNDER, TX_SKD_THUNDER, SK_MAGIC, 8, 14, 1, 4, 0, EL_THUNDER, 4, 0, EF_STUN, 0 },
	{ TX_SK_METEOR, TX_SKD_METEOR, SK_MAGIC, 20, 14, 2, 6, 3, EL_FIRE, 3, 0, EF_BURN, 0 },
	{ TX_SK_RUNEBLADE, TX_SKD_RUNEBLADE, SK_MAGIC, 6, 12, 1, 1, 0, EL_THUNDER, 2, 0, EF_NONE, 0 },
	{ TX_SK_HOLY, TX_SKD_HOLY, SK_MAGIC, 14, 18, 1, 3, 0, EL_HOLY, 3, 0, EF_NONE, 0 },
	{ TX_SK_DARKNESS, TX_SKD_DARKNESS, SK_MAGIC, 10, 16, 1, 3, 0, EL_DARK, 2, 0, EF_CONFUSE, 0 },
	{ TX_SK_HEAL, TX_SKD_HEAL, SK_HEAL, 4, 12, 0, 2, 0, EL_HOLY, 1, 0, EF_NONE, 0 },
	{ TX_SK_HIGHHEAL, TX_SKD_HIGHHEAL, SK_HEAL, 9, 26, 0, 3, 0, EL_HOLY, 1, 0, EF_NONE, 0 },
	{ TX_SK_HEALALL, TX_SKD_HEALALL, SK_HEAL, 16, 18, 0, 3, 2, EL_HOLY, 1, 0, EF_NONE, 0 },
	{ TX_SK_PROTECT, TX_SKD_PROTECT, SK_BUFF, 6, 0, 0, 2, 0, EL_EARTH, 1, 0, EF_BUFF_DEF, 0 },
	{ TX_SK_HASTE, TX_SKD_HASTE, SK_BUFF, 8, 0, 0, 2, 0, EL_WIND, 1, 0, EF_BUFF_AGI, 0 },
	{ TX_SK_BLESS, TX_SKD_BLESS, SK_BUFF, 8, 0, 0, 2, 0, EL_HOLY, 1, 0, EF_BUFF_ATK, 0 },
	{ TX_SK_P_EVADE, TX_SKD_P_EVADE, SK_PASSIVE, 0, 0, 0, 0, 0, EL_NONE, 0, 0, EF_NONE, 0 },
	{ TX_SK_P_MIGHT, TX_SKD_P_MIGHT, SK_PASSIVE, 0, 0, 0, 0, 0, EL_NONE, 0, 0, EF_NONE, 0 },
	{ TX_SK_P_SPELL, TX_SKD_P_SPELL, SK_PASSIVE, 0, 0, 0, 0, 0, EL_NONE, 0, 0, EF_NONE, 0 },
	{ TX_SK_P_IRON, TX_SKD_P_IRON, SK_PASSIVE, 0, 0, 0, 0, 0, EL_NONE, 0, 0, EF_NONE, 0 },
	{ TX_SK_P_SWIFT, TX_SKD_P_SWIFT, SK_PASSIVE, 0, 0, 0, 0, 0, EL_NONE, 0, 0, EF_NONE, 0 },
	{ TX_SK_P_REGEN, TX_SKD_P_REGEN, SK_PASSIVE, 0, 0, 0, 0, 0, EL_NONE, 0, 0, EF_NONE, 0 },
	{ TX_SK_P_CRIT, TX_SKD_P_CRIT, SK_PASSIVE, 0, 0, 0, 0, 0, EL_NONE, 0, 0, EF_NONE, 0 },
	{ TX_SK_E_VENOM, TX_SKD_E_VENOM, SK_PHYS, 4, 120, 1, 1, 0, EL_POISON, 1, 0, EF_POISON, 1 },
	{ TX_SK_E_BREATH, TX_SKD_E_BREATH, SK_MAGIC, 8, 16, 1, 3, 1, EL_FIRE, 2, 0, EF_BURN, 1 },
	{ TX_SK_E_CRUSH, TX_SKD_E_CRUSH, SK_PHYS, 0, 125, -1, -1, 0, EL_EARTH, 1, 0, EF_STUN, 1 },
	{ TX_SK_E_DRAIN, TX_SKD_E_DRAIN, SK_PHYS, 5, 100, 1, 1, 0, EL_DARK, 1, 0, EF_DRAIN, 1 },
	{ TX_SK_E_SPIT, TX_SKD_E_SPIT, SK_PHYS, 0, 90, 1, 2, 0, EL_POISON, 1, 0, EF_SLEEP, 1 },
	{ TX_SK_E_QUAKE, TX_SKD_E_QUAKE, SK_PHYS, 0, 110, 0, 0, 1, EL_EARTH, 1, 0, EF_STUN, 1 },
	{ TX_SK_REFRESH, TX_SKD_REFRESH, SK_BUFF, 8, 0, 1, 1, 0, EL_HOLY, 1, 0, EF_REACT, 0 },
	{ TX_SK_CLEAVE, TX_SKD_CLEAVE, SK_PHYS, 8, 115, 0, 0, 1, EL_NONE, 1, 0, EF_DEFDOWN, 0 },
	{ TX_SK_SNIPE, TX_SKD_SNIPE, SK_PHYS, 7, 220, 2, 6, 0, EL_WIND, 1, 25, EF_NONE, 0 },
	{ TX_SK_VENOMEDGE, TX_SKD_VENOMEDGE, SK_PHYS, 6, 80, 1, 2, 0, EL_POISON, 2, 0, EF_POISON, 0 },
	{ TX_SK_SANCTUARY, TX_SKD_SANCTUARY, SK_HEAL, 14, 16, 0, 0, 2, EL_HOLY, 1, 0, EF_NONE, 0 },
	{ TX_SK_SMITE, TX_SKD_SMITE, SK_MAGIC, 10, 16, 1, 3, 0, EL_HOLY, 2, 10, EF_NONE, 0 },
	{ TX_SK_GRAVITY, TX_SKD_GRAVITY, SK_MAGIC, 12, 12, 1, 3, 1, EL_EARTH, 3, 0, EF_SLOW, 0 },
	{ TX_SK_WARCRY, TX_SKD_WARCRY, SK_BUFF, 8, 0, 0, 0, 1, EL_FIRE, 1, 0, EF_BUFF_ATK, 0 },
	{ TX_SK_RENEW, TX_SKD_RENEW, SK_HEAL, 12, 40, 0, 2, 0, EL_HOLY, 1, 0, EF_NONE, 0 },
	{ TX_SK_MIRROR, TX_SKD_MIRROR, SK_BUFF, 8, 0, 0, 0, 1, EL_ICE, 1, 0, EF_BUFF_DEF, 0 },
	{ TX_SK_GODSLASH, TX_SKD_GODSLASH, SK_PHYS, 22, 150, -1, -1, 0, EL_HOLY, 2, 20, EF_NONE, 0 },
	{ TX_SK_RUIN, TX_SKD_RUIN, SK_MAGIC, 24, 18, 1, 4, 1, EL_DARK, 3, 0, EF_BLIND, 0 },
	{ TX_SK_APOC, TX_SKD_APOC, SK_MAGIC, 28, 16, 0, 0, 3, EL_FIRE, 4, 0, EF_NONE, 0 },
	{ TX_SK_TRANSMUTE, TX_SKD_TRANSMUTE, SK_BUFF, 4, 0, 0, 1, 0, EL_EARTH, 1, 0, EF_BUFF_ATK, 0 },
};
static_assert(sizeof(kSkill) / sizeof(kSkill[0]) == SKL_ART0, "base skill table");

static const SkillDef kWide[] = {
	{ TX_SK_SHOWER, TX_SKD_SHOWER, SK_PHYS, 14, 110, 2, 6, 2, EL_WIND, 3, 0, EF_NONE, 0 },
	{ TX_SK_SHINE, TX_SKD_SHINE, SK_PHYS, 16, 95, 2, 6, 4, EL_HOLY, 3, 0, EF_NONE, 0 },
	{ TX_SK_CRESCENT, TX_SKD_CRESCENT, SK_PHYS, 10, 100, 0, 0, 2, EL_WIND, 2, 0, EF_SLOW, 0 },
	{ TX_SK_GRANDSLASH, TX_SKD_GRANDSLASH, SK_PHYS, 16, 80, 0, 0, 4, EL_NONE, 1, 0, EF_NONE, 0 },
	{ TX_SK_STORM, TX_SKD_STORM, SK_MAGIC, 26, 14, 2, 6, 4, EL_FIRE, 4, 0, EF_BURN, 0 },
	{ TX_SK_FROSTFIELD, TX_SKD_FROSTFIELD, SK_MAGIC, 14, 9, 1, 4, 2, EL_ICE, 4, 0, EF_FREEZE, 0 },
	{ TX_SK_THUNDERFIELD, TX_SKD_THUNDERFIELD, SK_MAGIC, 16, 11, 1, 5, 2, EL_THUNDER, 4, 0, EF_STUN, 0 },
	{ TX_SK_NOVA, TX_SKD_NOVA, SK_MAGIC, 18, 14, 0, 0, 4, EL_HOLY, 4, 0, EF_NONE, 0 },
	{ TX_SK_LINESLASH, TX_SKD_LINESLASH, SK_PHYS, 10, 100, 1, 1, 4, EL_WIND, 1, 0, EF_BLEED, 0, SH_LINE },
	{ TX_SK_FANSLASH, TX_SKD_FANSLASH, SK_PHYS, 12, 80, 1, 1, 3, EL_NONE, 1, 0, EF_NONE, 0, SH_FAN },
	{ TX_SK_ARROWLINE, TX_SKD_ARROWLINE, SK_PHYS, 10, 120, 2, 6, 5, EL_WIND, 3, 0, EF_NONE, 0, SH_LINE },
	{ TX_SK_ARROWFAN, TX_SKD_ARROWFAN, SK_PHYS, 12, 110, 2, 6, 3, EL_WIND, 3, 0, EF_NONE, 0, SH_FAN },
	{ TX_SK_FIREBEAM, TX_SKD_FIREBEAM, SK_MAGIC, 12, 10, 1, 4, 5, EL_FIRE, 3, 0, EF_NONE, 0, SH_LINE },
	{ TX_SK_ICEBEAM, TX_SKD_ICEBEAM, SK_MAGIC, 14, 9, 1, 4, 5, EL_ICE, 3, 0, EF_FREEZE, 0, SH_LINE },
	{ TX_SK_THUNDERBEAM, TX_SKD_THUNDERBEAM, SK_MAGIC, 14, 12, 1, 4, 4, EL_THUNDER, 3, 0, EF_STUN, 0, SH_LINE },
	{ TX_SK_HEALBEAM, TX_SKD_HEALBEAM, SK_HEAL, 12, 14, 1, 3, 4, EL_HOLY, 1, 0, EF_NONE, 0, SH_LINE },
	{ TX_SK_HEALFAN, TX_SKD_HEALFAN, SK_HEAL, 14, 12, 1, 2, 3, EL_HOLY, 1, 0, EF_NONE, 0, SH_FAN },
	{ TX_SK_RAMPAGE, TX_SKD_RAMPAGE, SK_PHYS, 8, 70, -1, -1, 0, EL_NONE, 6, 0, EF_NONE, 0 },
	{ TX_SK_EARTHREND, TX_SKD_EARTHREND, SK_PHYS, 10, 130, 0, 0, 1, EL_EARTH, 1, 0, EF_DEFDOWN, 0 },
	{ TX_SK_BLOODAXE, TX_SKD_BLOODAXE, SK_PHYS, 8, 130, -1, -1, 0, EL_DARK, 1, 0, EF_DRAIN, 0 },
	{ TX_SK_IRONHOWL, TX_SKD_IRONHOWL, SK_BUFF, 8, 0, 0, 0, 1, EL_NONE, 1, 0, EF_BUFF_DEF, 0 },
	{ TX_SK_COLOSSMASH, TX_SKD_COLOSSMASH, SK_PHYS, 14, 220, -1, -1, 0, EL_EARTH, 1, 10, EF_IGNDEF, 0 },
	{ TX_SK_ARENAKING, TX_SKD_ARENAKING, SK_PHYS, 16, 120, 1, 1, 3, EL_NONE, 1, 10, EF_NONE, 0, SH_FAN },
	{ TX_SK_WARDRUM, TX_SKD_WARDRUM, SK_BUFF, 10, 0, 0, 0, 2, EL_FIRE, 1, 0, EF_BUFF_ATK, 0 },
	{ TX_SK_AXEWHIRL, TX_SKD_AXEWHIRL, SK_PHYS, 12, 75, 1, 1, 3, EL_NONE, 2, 0, EF_NONE, 0, SH_FAN },
	{ TX_SK_FISTGOD, TX_SKD_FISTGOD, SK_PHYS, 16, 250, -1, -1, 0, EL_HOLY, 1, 15, EF_NONE, 0 },
	{ TX_SK_WARLORD, TX_SKD_WARLORD, SK_PHYS, 14, 150, 1, 1, 4, EL_NONE, 1, 0, EF_NONE, 0, SH_LINE },
	{ TX_SK_BREAKKING, TX_SKD_BREAKKING, SK_PHYS, 16, 140, 1, 1, 3, EL_EARTH, 1, 0, EF_IGNDEF, 0, SH_FAN },
	{ TX_SK_STEELGOD, TX_SKD_STEELGOD, SK_PHYS, 18, 160, 0, 0, 2, EL_EARTH, 1, 0, EF_DEFDOWN, 0 },
	{ TX_SK_HEGEFIST, TX_SKD_HEGEFIST, SK_PHYS, 18, 160, -1, -1, 0, EL_NONE, 2, 10, EF_NONE, 0 },
	{ TX_SK_PEERLESS, TX_SKD_PEERLESS, SK_PHYS, 22, 90, -1, -1, 0, EL_NONE, 8, 15, EF_NONE, 0 },
	{ TX_SK_ENDFIST, TX_SKD_ENDFIST, SK_PHYS, 28, 360, -1, -1, 0, EL_HOLY, 1, 25, EF_IGNDEF, 0 },
	{ TX_SK_SHADOWSTEP, TX_SKD_SHADOWSTEP, SK_PHYS, 6, 140, -1, -1, 0, EL_WIND, 1, 30, EF_NONE, 0 },
	{ TX_SK_SMOKESCREEN, TX_SKD_SMOKESCREEN, SK_BUFF, 8, 0, 0, 0, 1, EL_WIND, 1, 0, EF_BUFF_AGI, 0 },
	{ TX_SK_VITALSTAB, TX_SKD_VITALSTAB, SK_PHYS, 8, 120, -1, -1, 0, EL_POISON, 1, 20, EF_POISON, 0 },
	{ TX_SK_BACKCUT, TX_SKD_BACKCUT, SK_PHYS, 10, 170, -1, -1, 0, EL_DARK, 1, 15, EF_IGNDEF, 0 },
	{ TX_SK_NIGHTRUN, TX_SKD_NIGHTRUN, SK_PHYS, 12, 150, 1, 2, 5, EL_DARK, 1, 0, EF_NONE, 0, SH_LINE },
	{ TX_SK_SILENTKILL, TX_SKD_SILENTKILL, SK_PHYS, 16, 280, -1, -1, 0, EL_DARK, 1, 35, EF_IGNDEF, 0 },
	{ TX_SK_DAGGERRAIN, TX_SKD_DAGGERRAIN, SK_PHYS, 12, 110, 1, 3, 3, EL_WIND, 1, 10, EF_NONE, 0, SH_FAN },
	{ TX_SK_DEATHMARK, TX_SKD_DEATHMARK, SK_PHYS, 8, 90, 1, 2, 0, EL_DARK, 1, 0, EF_DEFDOWN, 0 },
	{ TX_SK_GHOSTHAND, TX_SKD_GHOSTHAND, SK_PHYS, 10, 110, -1, -1, 0, EL_WIND, 5, 20, EF_NONE, 0 },
	{ TX_SK_SHADEEMP, TX_SKD_SHADEEMP, SK_PHYS, 14, 130, 1, 2, 3, EL_DARK, 1, 10, EF_NONE, 0, SH_FAN },
	{ TX_SK_TRACELESS, TX_SKD_TRACELESS, SK_BUFF, 10, 0, 0, 0, 2, EL_WIND, 1, 0, EF_BUFF_AGI, 0 },
	{ TX_SK_NIGHTEMP, TX_SKD_NIGHTEMP, SK_PHYS, 16, 160, 1, 3, 5, EL_DARK, 1, 0, EF_POISON, 0, SH_LINE },
	{ TX_SK_VOIDTHIEF, TX_SKD_VOIDTHIEF, SK_PHYS, 16, 200, -1, -1, 0, EL_DARK, 1, 15, EF_DEFDOWN, 0 },
	{ TX_SK_LASTNIGHT, TX_SKD_LASTNIGHT, SK_PHYS, 20, 170, 0, 0, 2, EL_DARK, 1, 10, EF_NONE, 0 },
	{ TX_SK_ETERNIGHT, TX_SKD_ETERNIGHT, SK_PHYS, 26, 340, -1, -1, 0, EL_DARK, 1, 30, EF_IGNDEF, 0 },
	{ TX_SK_ESUNA, TX_SKD_ESUNA, SK_HEAL, 6, 8, 0, 2, 0, EL_HOLY, 1, 0, EF_CLEANSE, 0 },
	{ TX_SK_PURIFY, TX_SKD_PURIFY, SK_HEAL, 14, 6, 0, 0, 2, EL_HOLY, 1, 0, EF_CLEANSE, 0 },
	{ TX_SK_CLEANCUT, TX_SKD_CLEANCUT, SK_HEAL, 4, 6, 0, 1, 0, EL_HOLY, 1, 0, EF_CLEANSE, 0 },
};
static_assert(sizeof(kWide) / sizeof(kWide[0]) == SKL_AREAHEAL0 - (SKL_RAISE + 1), "wide skill table");
static const SkillDef kForm[] = {
	{ TX_SK_WING, TX_SKD_WING, SK_PHYS, 6, 260, 1, 1, 0, EL_NONE, 1, 8, EF_NONE, 0, SH_RADIUS, FM_WING },
	{ TX_SK_CROSS, TX_SKD_CROSS, SK_PHYS, 8, 340, 1, 1, 0, EL_NONE, 1, 12, EF_NONE, 0, SH_RADIUS, FM_CROSS },
	{ TX_SK_WALL, TX_SKD_WALL, SK_PHYS, 10, 240, 1, 1, 2, EL_NONE, 1, 8, EF_NONE, 0, SH_FAN, FM_WALL },
	{ TX_SK_BACK, TX_SKD_BACK, SK_PHYS, 8, 260, 1, 1, 4, EL_WIND, 1, 8, EF_NONE, 0, SH_LINE, FM_BACK },
	{ TX_SK_TRI, TX_SKD_TRI, SK_PHYS, 10, 260, 1, 1, 3, EL_NONE, 1, 8, EF_NONE, 0, SH_FAN, FM_TRI },
	{ TX_SK_CHAIN, TX_SKD_CHAIN, SK_PHYS, 14, 380, 1, 1, 0, EL_NONE, 1, 16, EF_NONE, 0, SH_RADIUS, FM_CHAIN },
	{ TX_SK_DIA, TX_SKD_DIA, SK_PHYS, 10, 250, 1, 1, 2, EL_HOLY, 1, 8, EF_NONE, 0, SH_FAN, FM_DIA },
	{ TX_SK_SQUARE, TX_SKD_SQUARE, SK_PHYS, 12, 280, 0, 0, 1, EL_EARTH, 1, 8, EF_NONE, 0, SH_RADIUS, FM_SQUARE },
	{ TX_SK_ECHELON, TX_SKD_ECHELON, SK_PHYS, 12, 230, 2, 5, 5, EL_WIND, 2, 8, EF_NONE, 0, SH_LINE, FM_ECHELON },
	{ TX_SK_CIRCLE, TX_SKD_CIRCLE, SK_MAGIC, 16, 36, 0, 0, 2, EL_HOLY, 1, 0, EF_NONE, 0, SH_RADIUS, FM_CIRCLE },
	{ TX_SK_PAIR, TX_SKD_PAIR, SK_HEAL, 8, 42, 0, 0, 1, EL_HOLY, 1, 0, EF_NONE, 0, SH_RADIUS, FM_PAIR },
	{ TX_SK_PINCER, TX_SKD_PINCER, SK_PHYS, 10, 360, -1, -1, 0, EL_NONE, 1, 28, EF_NONE, 0, SH_RADIUS, FM_PINCER },
};
static_assert(sizeof(kForm) / sizeof(kForm[0]) == SKL_FORMN - SKL_FORM0, "form skill table");

static const int kPaceLv[8] = { 9, 12, 16, 20, 23, 26, 29, 32 };
static SkillDef s_art[SKL_ARTN - SKL_ART0];
static int s_artOk = 0;
static void BuildArts()
{
	if (s_artOk) return;
	for (int i = 0; i < SKL_ARTN - SKL_ART0; i++) {
		SkillDef& d = s_art[i];
		memset(&d, 0, sizeof(d));
		int lv = i % 26;
		int pat = lv % 8;
		int el = 1 + ((lv + (i / 26) * 3) % 8);
		d.nameTx = TX_ART_FIRST + i;
		d.descTx = TX_ARTD_FIRST + i;
		d.element = el;
		d.hits = 1;
		d.enemyOnly = 0;
		d.mp = 4 + pat;
		d.power = 8 + lv / 2;
		d.rMin = 1;
		d.rMax = 3;
		d.area = 0;
		d.kind = SK_MAGIC;
		d.effect = EF_NONE;
		if (pat == 1) { d.area = 1; d.power = 7 + lv / 3; d.rMax = 4; }
		else if (pat == 2) { d.kind = SK_PHYS; d.power = 120 + lv * 2; d.rMin = -1; d.rMax = -1; }
		else if (pat == 3) { d.kind = SK_PHYS; d.power = 70 + lv; d.hits = lv >= 24 ? 10 : (lv >= 12 ? 6 : 4); d.rMin = -1; d.rMax = -1; }
		else if (pat == 4) { d.kind = SK_HEAL; d.power = 12 + lv / 2; d.rMin = 0; d.rMax = 2; }
		else if (pat == 5) { d.kind = SK_BUFF; d.power = 0; d.rMin = 0; d.rMax = 2; d.effect = EF_BUFF_DEF; }
		else if (pat == 6) { d.area = 1; d.power = 6 + lv / 4; d.effect = EF_SLOW; }
		else if (pat == 7) { d.kind = SK_PHYS; d.power = 100 + lv * 2; d.rMin = -1; d.rMax = -1; d.effect = EF_POISON; d.element = EL_POISON; }
		if (pat == 0 && lv >= 16) d.effect = EF_DEFDOWN;
		if (pat == 2 && lv >= 20) d.effect = EF_IGNDEF;
	}
	for (int g = 0; g < G_COUNT; g++) {
		for (int role = 0; role < 8; role++) {
			int local = kPaceLv[role] - 7;
			SkillDef& d = s_art[g * 26 + local];
			int el = 1 + ((local + g * 3) % 8);
			memset(&d, 0, sizeof(d));
			d.nameTx = TX_ART_FIRST + g * 26 + local;
			d.descTx = TX_ARTD_FIRST + g * 26 + local;
			d.element = el;
			d.hits = 1;
			d.enemyOnly = 0;
			d.shape = SH_RADIUS;
			if (role == 0) { d.kind = SK_MAGIC; d.mp = 6; d.power = 10; d.rMin = 1; d.rMax = 4; d.area = 1; }
			else if (role == 1) { d.kind = SK_HEAL; d.mp = 6; d.power = 18; d.rMin = 0; d.rMax = 2; d.element = EL_HOLY; }
			else if (role == 2) { d.kind = SK_MAGIC; d.mp = 14; d.power = 16; d.rMin = 2; d.rMax = 5; d.area = 2; }
			else if (role == 3) { d.kind = SK_BUFF; d.mp = 8; d.rMin = 0; d.rMax = 0; d.area = 1; d.effect = EF_BUFF_AGI; d.element = EL_WIND; }
			else if (role == 4) { d.kind = SK_MAGIC; d.mp = 12; d.power = 13; d.rMin = 1; d.rMax = 5; d.area = 5; d.shape = SH_LINE; }
			else if (role == 5) { d.kind = SK_PHYS; d.mp = 12; d.power = 85; d.rMin = 1; d.rMax = 1; d.area = 3; d.hits = 4; d.critBonus = 10; d.shape = SH_FAN; }
			else if (role == 6) { d.kind = SK_MAGIC; d.mp = 14; d.power = 11; d.rMin = 1; d.rMax = 4; d.area = 2; d.effect = EF_SLOW; }
			else { d.kind = SK_MAGIC; d.mp = 22; d.power = 22; d.rMin = 0; d.rMax = 0; d.area = 3; d.effect = EF_STUN; }
		}
	}
	s_artOk = 1;
}
static SkillDef s_area[16];
static int s_areaOk = 0;
static void BuildAreaSupport()
{
	if (s_areaOk) return;
	for (int i = 0; i < 8; i++) {
		int rad = 2 + i;
		SkillDef& h = s_area[i];
		memset(&h, 0, sizeof(h));
		h.nameTx = TX_SK_AREAHEAL;
		h.descTx = TX_SKD_AREAX + i;
		h.kind = SK_HEAL;
		h.mp = 8 + rad * 5;
		h.power = 12 + rad * 3;
		h.rMin = 0;
		h.rMax = rad;
		h.area = rad;
		h.element = EL_HOLY;
		h.hits = 1;
		SkillDef& z = s_area[8 + i];
		memset(&z, 0, sizeof(z));
		z.nameTx = TX_SK_AREARAISE;
		z.descTx = TX_SKD_AREAX + 8 + i;
		z.kind = SK_HEAL;
		z.mp = 14 + rad * 8;
		z.power = 0;
		z.rMin = 0;
		z.rMax = rad;
		z.area = rad;
		z.element = EL_HOLY;
		z.hits = 1;
		z.effect = EF_RAISE;
	}
	s_areaOk = 1;
}
static const SkillDef& CraftSkillRef(int index);
const SkillDef& SummonSkillRef(int index);
const SkillDef& SkillInfo(int s)
{
	if (IsSummonId(s)) return SummonSkillRef(s - SUMMON_BASE);
	if (s >= CRAFT_BASE && s < CRAFT_BASE + CRAFT_N) return CraftSkillRef(s - CRAFT_BASE);
	if (s == SKL_RAISE) {
		static const SkillDef raise = { TX_SK_RAISE, TX_SKD_RAISE, SK_HEAL, 12, 0, 1, 2, 0, EL_HOLY, 1, 0, EF_RAISE, 0 };
		return raise;
	}
	if (s >= SKL_ART0 && s < SKL_ARTN) {
		BuildArts();
		return s_art[s - SKL_ART0];
	}
	if (s >= SKL_FORM0 && s < SKL_FORMN) return kForm[s - SKL_FORM0];
	if (s >= SKL_MORE0 && s < SKL_COUNT) {
		static const SkillDef kMore[] = {
#include "tactics_src/skill_more_defs.inc"
		};
		static_assert(sizeof(kMore) / sizeof(kMore[0]) == (size_t)(SKL_COUNT - SKL_MORE0), "more skill table");
		return kMore[s - SKL_MORE0];
	}
	if (s >= SKL_GALE && s < SKL_MORE0) {
		static const SkillDef ex[] = {
			{ TX_SK_GALE, TX_SKD_GALE, SK_MAGIC, 6, 12, 1, 3, 0, EL_WIND, 2, 0, EF_NONE, 0 },
			{ TX_SK_MIASMA, TX_SKD_MIASMA, SK_MAGIC, 6, 12, 1, 3, 0, EL_POISON, 2, 0, EF_POISON, 0 },
			{ TX_SK_ELFIRE, TX_SKD_ELFIRE, SK_PHYS, 6, 90, -1, -1, 0, EL_FIRE, 2, 0, EF_BURN, 0 },
			{ TX_SK_ELICE, TX_SKD_ELICE, SK_PHYS, 6, 90, -1, -1, 0, EL_ICE, 2, 0, EF_FREEZE, 0 },
			{ TX_SK_ELTHUNDER, TX_SKD_ELTHUNDER, SK_PHYS, 6, 90, -1, -1, 0, EL_THUNDER, 2, 0, EF_NONE, 0 },
			{ TX_SK_ELHOLY, TX_SKD_ELHOLY, SK_PHYS, 6, 90, -1, -1, 0, EL_HOLY, 2, 0, EF_NONE, 0 },
			{ TX_SK_ELDARK, TX_SKD_ELDARK, SK_PHYS, 6, 90, -1, -1, 0, EL_DARK, 2, 0, EF_SILENCE, 0 },
			{ TX_SK_ELWIND, TX_SKD_ELWIND, SK_PHYS, 6, 90, -1, -1, 0, EL_WIND, 2, 0, EF_NONE, 0 },
			{ TX_SK_ELEATH, TX_SKD_ELEATH, SK_PHYS, 6, 90, -1, -1, 0, EL_EARTH, 2, 0, EF_NONE, 0 },
			{ TX_SK_ELPOISON, TX_SKD_ELPOISON, SK_PHYS, 6, 90, -1, -1, 0, EL_POISON, 2, 0, EF_POISON, 0 },
		};
		static_assert(sizeof(ex) / sizeof(ex[0]) == SKL_MORE0 - SKL_GALE, "extra skill table");
		return ex[s - SKL_GALE];
	}
	if (s >= SKL_AREAHEAL0 && s < SKL_FORM0) {
		BuildAreaSupport();
		return s_area[s - SKL_AREAHEAL0];
	}
	if (s > SKL_RAISE && s < SKL_AREAHEAL0) return kWide[s - (SKL_RAISE + 1)];
	return kSkill[Clampi(s, 0, SKL_ART0 - 1)];
}
#include "tactics_src/area_names.inc"
const wchar_t* SkillLabel(int s)
{
	if (IsSummonId(s)) return SummonName(s - SUMMON_BASE + 1);
	if (s >= SKL_AREAHEAL0 && s < SKL_FORM0) {
		const wchar_t* const* row = kAreaNm[s - SKL_AREAHEAL0];
		return LangPick14(row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7], row[8], row[9], row[10], row[11], row[12], row[13]);
	}
	return TT(SkillInfo(s).nameTx);
}

// ============================================================ モンスター
// name, look, model, wlook, move, jump, rMin, rMax, mag, pow, style, base, grow, skills, minStage, weight, scale, tint
static MonsterDef kMon[M_COUNT] = {
	{ TX_MO_SLIME, LK_SLIME, -1, 0, MV_FOOT, 1, 1, 1, 0, 3, AS_BITE,
	  { 22, 4, 6, 4, 0, 4, 3, 4, 2, 3 }, { 60, 5, 40, 40, 0, 30, 20, 30, 20, 0 }, { SKL_E_SPIT, -1, -1 }, 1, 10, 1.0f, 0x60E070, 16, -1 },
	{ TX_MO_SKEL_MINION, LK_KAYKIT, 6, 1, MV_FOOT, 2, 1, 2, 0, 4, AS_STAB,
	  { 18, 0, 6, 3, 0, 1, 5, 5, 2, 4 }, { 45, 0, 45, 30, 0, 15, 35, 40, 20, 0 }, { -1, -1, -1 }, 1, 10, 1.0f, 0xFFFFFF, 18, -1 },
	{ TX_MO_SKEL_WARRIOR, LK_KAYKIT, 5, 2, MV_FOOT, 2, 1, 1, 0, 6, AS_CHOP2,
	  { 26, 6, 9, 7, 0, 2, 4, 6, 2, 4 }, { 55, 10, 55, 50, 0, 20, 30, 40, 20, 0 }, { SKL_E_CRUSH, -1, -1 }, 3, 8, 1.0f, 0xFFFFFF, 24, -1 },
	{ TX_MO_SKEL_ARCHER, LK_KAYKIT, 7, 3, MV_FOOT, 2, 2, 4, 0, 5, AS_SHOOT,
	  { 18, 0, 7, 3, 0, 3, 6, 8, 3, 4 }, { 40, 0, 45, 25, 0, 20, 40, 55, 25, 0 }, { -1, -1, -1 }, 4, 7, 1.0f, 0xFFFFFF, 28, -1 },
	{ TX_MO_BAT, LK_BAT, -1, 0, MV_FLY, 9, 1, 1, 0, 4, AS_BITE,
	{ 15, 6, 6, 2, 0, 3, 11, 7, 6, 7 }, { 35, 10, 40, 20, 0, 25, 60, 45, 40, 0 }, { SKL_E_DRAIN, -1, -1 }, 5, 6, 1.0f, 0x806070, 32, -1 },
	{ TX_MO_LICH, LK_KAYKIT, 8, 4, MV_FOOT, 2, 1, 3, 1, 4, AS_CAST,
	{ 20, 20, 1, 3, 8, 8, 5, 6, 4, 3 }, { 40, 50, 5, 25, 60, 50, 30, 40, 30, 0 }, { SKL_FIRE, SKL_BLIZZARD, SKL_HEAL }, 8, 5, 1.0f, 0xD8C8FF, 40, -1 },
	{ TX_MO_POISON_SLIME, LK_SLIME, -1, 0, MV_FOOT, 1, 2, 2, 0, 5, AS_SHOOT1,
	  { 28, 8, 8, 6, 0, 6, 4, 5, 3, 3 }, { 65, 10, 45, 45, 0, 35, 25, 35, 20, 0 }, { SKL_E_VENOM, SKL_E_SPIT, -1 }, 12, 6, 1.15f, 0xB070E0, 36, -1 },
	{ TX_MO_GOLEM, LK_GOLEM, -1, 0, MV_FOOT, 1, 1, 1, 0, 8, AS_PUNCH,
	{ 40, 8, 12, 14, 0, 4, 2, 5, 1, 2 }, { 75, 10, 60, 70, 0, 25, 10, 30, 10, 0 }, { SKL_E_QUAKE, -1, -1 }, 15, 4, 1.0f, 0xFFFFFF, 48, -1 },
	{ TX_MO_WYVERN, LK_WYVERN, -1, 0, MV_FLY, 9, 1, 1, 0, 8, AS_BITE,
	  { 34, 12, 12, 8, 6, 6, 9, 8, 5, 6 }, { 60, 20, 60, 45, 20, 30, 45, 40, 30, 0 }, { SKL_E_BREATH, -1, -1 }, 20, 4, 1.0f, 0x7AA060, 60, -1 },
	{ TX_MO_DARK_KNIGHT, LK_KAYKIT, 5, 5, MV_FOOT, 2, 1, 1, 0, 9, AS_SLASH,
	  { 36, 10, 14, 13, 2, 8, 7, 9, 4, 5 }, { 60, 10, 65, 60, 10, 35, 35, 45, 25, 0 }, { SKL_E_CRUSH, SKL_BASH, -1 }, 30, 4, 1.12f, 0x8070A0, 70, -1 },
	{ TX_MO_DRAGON, LK_WYVERN, -1, 0, MV_FLY, 9, 1, 1, 0, 12, AS_BITE,
	{ 70, 30, 18, 14, 14, 12, 8, 10, 8, 6 }, { 90, 40, 70, 55, 60, 45, 35, 40, 30, 0 }, { SKL_E_BREATH, SKL_E_CRUSH, -1 }, 80, 1, 1.75f, 0xE05040, 0, -1 },
};

#include "tactics_src/mon_species.inc"
#include "tactics_src/mon_names.inc"
#include "tactics_src/summon.inc"
static const wchar_t* ImportLocAt(int model, int lang)
{
	if (model < 0 || model >= kImportN) return L"?";
	if (lang < 0 || lang > 13) lang = 1;
	const wchar_t* s = kImportLoc[model][lang];
	return (s && s[0]) ? s : kImportLoc[model][1];
}
static const wchar_t* ImportLocName(int model)
{
	int lang = savedata.lang;
	if (lang < 0 || lang > 13) lang = 1;
	return ImportLocAt(model, lang);
}

enum { IMP_CAP = 2048, POOL_FAN = 0, POOL_BEAST = 1, POOL_FISH = 2, POOL_HUM = 3 };
static MonsterDef s_monImp[IMP_CAP];
static int s_monImpOk = 0;
static int s_poolN[4];
static int s_poolI[4][IMP_CAP];

static int HasW(const wchar_t* n, const wchar_t* k)
{
	return n && k && wcsstr(n, k) != NULL;
}

static int ImportPool(const wchar_t* n)
{
	if (n && wcsncmp(n, L"Hum", 3) == 0 && n[3] >= L'A' && n[3] <= L'Z') return POOL_HUM;
	if (HasW(n, L"Cactoro") || HasW(n, L"Blob") || HasW(n, L"Glub") || HasW(n, L"Mush") || HasW(n, L"Gole")
		|| HasW(n, L"Ghost") || HasW(n, L"Demon") || HasW(n, L"Orc") || HasW(n, L"Wizard") || HasW(n, L"Ninja")
		|| HasW(n, L"Dragon") || HasW(n, L"Alien") || HasW(n, L"Yeti") || HasW(n, L"Squid") || HasW(n, L"Hywirl")
		|| HasW(n, L"Armabee") || HasW(n, L"Monk") || HasW(n, L"Tribal"))
		return POOL_FAN;
	if (HasW(n, L"Fish") || HasW(n, L"Shark") || HasW(n, L"Tang") || HasW(n, L"Koi") || HasW(n, L"Tuna")
		|| HasW(n, L"Tetra") || HasW(n, L"Piranha") || HasW(n, L"Puffer") || HasW(n, L"Goldfish")
		|| HasW(n, L"Betta") || HasW(n, L"Snapper") || HasW(n, L"Grouper") || HasW(n, L"Gramma")
		|| HasW(n, L"Idol") || HasW(n, L"Humphead") || HasW(n, L"Turbot")
		|| HasW(n, L"Clown") || HasW(n, L"Angler") || HasW(n, L"Whale") || HasW(n, L"Dolphin")
		|| HasW(n, L"Manta") || HasW(n, L"Catfish") || HasW(n, L"Lionfish") || HasW(n, L"Mandarin")
		|| HasW(n, L"Sunfish") || HasW(n, L"FlowerHorn") || HasW(n, L"Flatfish") || HasW(n, L"Cowfish")
		|| HasW(n, L"Worm"))
		return POOL_FISH;
	if (HasW(n, L"Horse") || HasW(n, L"Donkey") || HasW(n, L"Bull") || HasW(n, L"Cow") || HasW(n, L"Deer")
		|| HasW(n, L"Stag") || HasW(n, L"Alpaca") || HasW(n, L"Fox") || HasW(n, L"Wolf") || HasW(n, L"Husky")
		|| HasW(n, L"Shiba") || HasW(n, L"Bunny") || HasW(n, L"Cat") || HasW(n, L"Rat") || HasW(n, L"Dino")
		|| HasW(n, L"saurus") || HasW(n, L"Trex") || HasW(n, L"Tricera") || HasW(n, L"Veloci")
		|| HasW(n, L"Frog") || HasW(n, L"Snake") || HasW(n, L"Spider") || HasW(n, L"Wasp")
		|| HasW(n, L"Chicken") || HasW(n, L"Birb") || HasW(n, L"Pigeon") || HasW(n, L"Alpaking"))
		return POOL_BEAST;
	if (HasW(n, L"Wyvern") || HasW(n, L"Naga") || HasW(n, L"Treant") || HasW(n, L"Minotaur")
		|| HasW(n, L"Drake") || HasW(n, L"Elderwood") || HasW(n, L"Imp") || HasW(n, L"Siren"))
		return POOL_FAN;
	if (HasW(n, L"Beetle") || HasW(n, L"Ant") || HasW(n, L"Bee") || HasW(n, L"Moth") || HasW(n, L"Mantis")
		|| HasW(n, L"Weevil") || HasW(n, L"Cricket") || HasW(n, L"Flea") || HasW(n, L"Aphid") || HasW(n, L"Cockroach")
		|| HasW(n, L"Termite") || HasW(n, L"Eagle") || HasW(n, L"Owl") || HasW(n, L"Hawk") || HasW(n, L"Bird")
		|| HasW(n, L"saurus") || HasW(n, L"Raptor") || HasW(n, L"Ptero") || HasW(n, L"Trex") || HasW(n, L"Crab")
		|| HasW(n, L"Sheep") || HasW(n, L"Pig") || HasW(n, L"Goat") || HasW(n, L"Lion") || HasW(n, L"Tiger")
		|| HasW(n, L"Elephant") || HasW(n, L"Mammoth") || HasW(n, L"Boar") || HasW(n, L"Bear") || HasW(n, L"Rhino")
		|| HasW(n, L"Zebra") || HasW(n, L"Bat") || HasW(n, L"Scorpion") || HasW(n, L"Spider") || HasW(n, L"Insect")
		|| HasW(n, L"Penguin") || HasW(n, L"Flamingo") || HasW(n, L"Toucan") || HasW(n, L"Pelican") || HasW(n, L"Macaw")
		|| HasW(n, L"Rooster") || HasW(n, L"Duck") || HasW(n, L"Goose") || HasW(n, L"Tortoise") || HasW(n, L"Pangolin"))
		return POOL_BEAST;
	if (HasW(n, L"Fugu") || HasW(n, L"Jelly") || HasW(n, L"Narwhal") || HasW(n, L"Manatee") || HasW(n, L"Sawfish")
		|| HasW(n, L"Angler") || HasW(n, L"Archer") || HasW(n, L"Seal") || HasW(n, L"Otter"))
		return POOL_FISH;
	return POOL_FAN;
}

static int HumanoidFan(const wchar_t* n)
{
	return HasW(n, L"Orc") || HasW(n, L"Ninja") || HasW(n, L"Tribal") || HasW(n, L"Wizard")
		|| HasW(n, L"Demon") || HasW(n, L"Alien") || HasW(n, L"Yeti") || HasW(n, L"Monk")
		|| HasW(n, L"Ghost");
}

static void ApplyImportRole(MonsterDef& d, const wchar_t* n, int i)
{
	int role = 0;
	if (ImportPool(n) == POOL_FAN && HumanoidFan(n)) {
		if (HasW(n, L"Wizard") || HasW(n, L"Ghost")) role = 4;
		else if (HasW(n, L"Demon")) role = (i & 1) ? 4 : 1;
		else if (HasW(n, L"Ninja")) role = 2;
		else if (HasW(n, L"Tribal")) role = 3;
		else if (HasW(n, L"Orc")) role = (i & 1) ? 2 : 1;
		else role = (i % 3 == 0) ? 3 : ((i % 3 == 1) ? 2 : 0);
	} else if (ImportPool(n) == POOL_FAN && (i % 4 == 0)) role = 2;
	else if ((i % 13) == 0) role = 2;
	switch (role) {
	case 1:
		d.rMin = 1; d.rMax = 2; d.attackStyle = AS_STAB; d.weaponLook = 1; d.magAttack = 0; break;
	case 2:
		d.rMin = 2; d.rMax = 3; d.attackStyle = AS_SHOOT1; d.weaponLook = (i & 1) ? 3 : 4; d.magAttack = 0; break;
	case 3:
		d.rMin = 2; d.rMax = 4; d.attackStyle = AS_SHOOT; d.weaponLook = 2; d.magAttack = 0; break;
	case 4:
		d.rMin = 1; d.rMax = 3; d.attackStyle = AS_CAST; d.weaponLook = 0; d.magAttack = 1; break;
	default:
		d.rMin = 1; d.rMax = 1;
		d.attackStyle = d.moveType == MV_FLY ? AS_BITE : AS_SLASH;
		d.weaponLook = 0; d.magAttack = 0;
		break;
	}
}

static int NameHas(const wchar_t* n, const wchar_t* k)
{
	return n && k && wcsstr(n, k) != NULL;
}
static int IceCaster(const wchar_t* n)
{
	if (!n) return 0;
	if (NameHas(n, L"Tree") || NameHas(n, L"Bush") || NameHas(n, L"Rock")) return 0;
	return NameHas(n, L"Frost") || NameHas(n, L"Yeti") || NameHas(n, L"Penguin") || NameHas(n, L"Polar")
		|| NameHas(n, L"Ice") || NameHas(n, L"Snow");
}
static int FireBreather(const wchar_t* n)
{
	if (!n) return 0;
	return NameHas(n, L"Dragon") || NameHas(n, L"Wyvern") || NameHas(n, L"Drake")
		|| NameHas(n, L"Flame") || NameHas(n, L"Phoenix") || NameHas(n, L"Salamand");
}
// 取り込み種は 1 種 1 行。個体では引かない。並びは kImportName と同じ。
static const int kImportWeak[kImportN][6] = {
#include "tactics_src/mon_weak.inc"
};
static int AilmentFor(const wchar_t* n, int i)
{
	if (NameHas(n, L"Spider") || NameHas(n, L"Ghost") || NameHas(n, L"Mush") || NameHas(n, L"Frog") || NameHas(n, L"Snake") || NameHas(n, L"Puffer") || NameHas(n, L"Blob") || NameHas(n, L"Wasp"))
		return SKL_E_SPIT;
	if (NameHas(n, L"Wizard") || NameHas(n, L"Witch") || NameHas(n, L"Demon") || NameHas(n, L"Alien") || NameHas(n, L"Ninja") || NameHas(n, L"Skull"))
		return SKL_DARKNESS;
	if (NameHas(n, L"Golem") || NameHas(n, L"Bull") || NameHas(n, L"Trex") || NameHas(n, L"Orc") || NameHas(n, L"Dragon") || NameHas(n, L"Yeti") || NameHas(n, L"Stego") || NameHas(n, L"Tricera") || NameHas(n, L"Raptor"))
		return SKL_E_CRUSH;
	int k = i % 4;
	if (k == 0) return SKL_E_SPIT;
	if (k == 1) return SKL_E_CRUSH;
	if (k == 2) return SKL_DARKNESS;
	return -1;
}
static void EnsureMonsters()
{
	if (!s_monImpOk) {
	for (int p = 0; p < 4; p++) s_poolN[p] = 0;
	static const int kClassicWeak[M_COUNT][4] = {
		{ EL_FIRE, EL_ICE, 0, 0 },
		{ EL_HOLY, 0, 0, 0 },
		{ EL_HOLY, EL_MAGIC, EL_MELEE, 0 },
		{ EL_WIND, EL_RANGE, 0, 0 },
		{ EL_THUNDER, EL_DARK, EL_WIND, 0 },
		{ EL_HOLY, EL_FIRE, EL_ICE, EL_MAGIC },
		{ EL_FIRE, EL_ICE, EL_POISON, 0 },
		{ EL_THUNDER, EL_MAGIC, EL_EARTH, 0 },
		{ EL_ICE, EL_RANGE, EL_WIND, 0 },
		{ EL_HOLY, EL_THUNDER, EL_MELEE, EL_DARK },
		{ EL_ICE, EL_RANGE, EL_THUNDER, EL_HOLY }
	};
	static const int kClassicAil[M_COUNT][2] = {
		{ EF_POISON, 0 },
		{ EF_STUN, 0 },
		{ EF_BLEED, EF_DEFDOWN },
		{ EF_BLIND, 0 },
		{ EF_SILENCE, 0 },
		{ EF_SILENCE, EF_FREEZE },
		{ EF_POISON, EF_SLOW },
		{ EF_STUN, EF_DEFDOWN },
		{ EF_BLEED, 0 },
		{ EF_BURN, 0 },
		{ EF_FREEZE, EF_BLEED }
	};
	for (int i = 0; i < M_COUNT; i++) {
		MonsterDef& d = kMon[i];
		FillUnitSheet(d.base, d.grow, G_MELEE, d.magAttack, d.moveType == MV_FLY, d.jump, d.rMax);
		for (int k = 0; k < 4; k++) d.weak[k] = kClassicWeak[i][k];
		d.ailWeak[0] = kClassicAil[i][0];
		d.ailWeak[1] = kClassicAil[i][1];
		if (i == M_SKEL_MINION && d.skills[0] < 0) d.skills[0] = SKL_E_SPIT;
		if (i == M_SKEL_ARCHER && d.skills[0] < 0) d.skills[0] = SKL_E_SPIT;
		if (i == M_BAT && d.skills[1] < 0) d.skills[1] = SKL_E_SPIT;
		if (i == M_WYVERN && d.skills[1] < 0) d.skills[1] = SKL_E_CRUSH;
	}
	int nImp = kImportN < IMP_CAP ? kImportN : IMP_CAP;
	int rosterN = 0;
	for (int i = 0; i < nImp; i++) if (wcsncmp(kImportName[i], L"Sum", 3) != 0) rosterN++;
	int rosterI = 0;
	for (int i = 0; i < nImp; i++) {
		MonsterDef& d = s_monImp[i];
		const wchar_t* nm = kImportName[i];
		BOOL summonOnly = nm && wcsncmp(nm, L"Sum", 3) == 0;
		memset(&d, 0, sizeof(d));
		d.look = LK_MESH;
		d.model = i;
		d.nameTx = TX_MO_SLIME;
		d.nameB = -1;
		d.moveType = kImportFly[i] ? MV_FLY : MV_FOOT;
		d.jump = d.moveType == MV_FLY ? 9 : 2;
		d.weaponPow = 4 + (i % 9);
		ApplyImportRole(d, nm, i);
		int tier = i / 12;
		d.base[S_HP] = 20 + tier * 6 + (i % 5) * 2;
		d.base[S_MP] = d.magAttack ? 14 + tier : 2;
		d.base[S_STR] = 5 + tier + (i % 4);
		d.base[S_DEF] = 3 + tier;
		d.base[S_MAG] = d.magAttack ? 6 + tier : 1;
		d.base[S_RES] = 3 + tier / 2;
		d.base[S_AGI] = 4 + (d.moveType == MV_FLY ? 3 : 0) + (i % 3);
		d.base[S_DEX] = 4 + (d.attackStyle == AS_SHOOT || d.attackStyle == AS_SHOOT1 ? 3 : 0) + (i % 4);
		d.base[S_LUK] = 2 + (i % 3);
		if (d.attackStyle == AS_SHOOT) d.base[S_MOV] = 3 + (i % 2);
		else if (d.magAttack) d.base[S_MOV] = 3 + (i % 2);
		else if (d.moveType == MV_FLY) d.base[S_MOV] = 6 + (i % 3);
		else d.base[S_MOV] = (i % 3 == 0) ? 5 : 4;
		int grp = d.magAttack ? G_MAGIC : (d.attackStyle == AS_SHOOT || d.attackStyle == AS_SHOOT1 ? G_SWIFT : (d.moveType == MV_FLY ? G_SWIFT : G_MELEE));
		FillUnitSheet(d.base, d.grow, grp, d.magAttack, d.moveType == MV_FLY, d.jump, d.rMax);
		d.grow[S_HP] = 45 + tier * 4;
		d.grow[S_STR] = 40 + tier * 2;
		d.grow[S_DEF] = 30 + tier;
		d.grow[S_MAG] = d.magAttack ? 50 : 5;
		d.grow[S_RES] = 25;
		d.grow[S_AGI] = 30;
		d.grow[S_DEX] = 25;
		d.grow[S_MP] = d.magAttack ? 40 : 5;
		if (d.magAttack) d.skills[0] = SKL_FIRE;
		else if (d.attackStyle == AS_SHOOT) d.skills[0] = SKL_SNIPE;
		else if (d.moveType == MV_FLY) d.skills[0] = SKL_E_DRAIN;
		else d.skills[0] = SKL_E_CRUSH;
		d.skills[1] = (i % 4 == 0) ? SKL_E_VENOM : -1;
		d.skills[2] = -1;
		{
			int ail = AilmentFor(nm, i);
			if (ail >= 0 && ail != d.skills[0] && ail != d.skills[1]) d.skills[2] = ail;
		}
		// 炎上は魔法職の火炎と、竜・ワイバーンのブレス。凍結は氷・雪・ペンギン・イエティ。召喚はそのまま。
		if (nm && wcsncmp(nm, L"Sum", 3) != 0 && IceCaster(nm)) {
			d.magAttack = 1;
			d.attackStyle = AS_CAST;
			d.rMin = 1; d.rMax = 3;
			if (d.base[S_MP] < 16) d.base[S_MP] = 16;
			if (d.base[S_MAG] < 8) d.base[S_MAG] = 8;
			if (d.grow[S_MP] < 40) d.grow[S_MP] = 40;
			if (d.grow[S_MAG] < 50) d.grow[S_MAG] = 50;
			d.skills[0] = SKL_BLIZZARD;
		} else if (nm && wcsncmp(nm, L"Sum", 3) != 0 && FireBreather(nm)) {
			int burn = 0;
			for (int s = 0; s < 3; s++) if (d.skills[s] == SKL_FIRE || d.skills[s] == SKL_E_BREATH) burn = 1;
			if (!burn) {
				if (d.base[S_MP] < 12) d.base[S_MP] = 12;
				d.skills[0] = d.magAttack ? SKL_FIRE : SKL_E_BREATH;
			}
		}
		for (int k = 0; k < 4; k++) d.weak[k] = kImportWeak[i][k];
		d.ailWeak[0] = kImportWeak[i][4];
		d.ailWeak[1] = kImportWeak[i][5];
		if (summonOnly) {
			d.minStage = TAC_STAGE_MAX;
			d.maxStage = 1;
			d.weight = 0;
		} else {
			// ファイル順に登場帯をずらす。同時に場に出るのはおよそ 48 種。召喚専用は数に入れない。
			const int alive = 48;
			int band = rosterN > 0 ? alive * TAC_STAGE_MAX / rosterN : TAC_STAGE_MAX;
			if (band < 600) band = 600;
			if (band > TAC_STAGE_MAX) band = TAC_STAGE_MAX;
			int half = band / 2;
			int center = 1;
			if (rosterN > 1) center = 1 + (int)((long long)rosterI * (TAC_STAGE_MAX - 1) / (rosterN - 1));
			int start = center - half;
			if (start < 1) start = 1;
			int end = center + half;
			d.minStage = start;
			d.maxStage = end >= TAC_STAGE_MAX ? 0 : end;
			rosterI++;
			d.weight = 8;
		}
		d.scale = 1.f;
		d.tint = 0xFFFFFF;
		int pool = ImportPool(nm);
		if (!summonOnly && pool != POOL_HUM && s_poolN[pool] < IMP_CAP) s_poolI[pool][s_poolN[pool]++] = M_COUNT + i;
	}
	}
	s_monImpOk = 1;
}

const MonsterDef& MonsterInfo(int m)
{
	EnsureSheets();
	EnsureMonsters();
	if (m >= M_COUNT && m < M_COUNT + kImportN && (m - M_COUNT) < IMP_CAP) return s_monImp[m - M_COUNT];
	if (m < 0 || m >= M_COUNT) m = 0;
	return kMon[m];
}

const wchar_t* MonsterLabel(int m)
{
	const MonsterDef& d = MonsterInfo(m);
	if (d.look == LK_MESH && d.model >= 0 && d.model < kImportN) return ImportLocName(d.model);
	return TT(d.nameTx);
}

const wchar_t* MonsterLabelAt(int m, int lang)
{
	const MonsterDef& d = MonsterInfo(m);
	if (d.look == LK_MESH && d.model >= 0 && d.model < kImportN) return ImportLocAt(d.model, lang);
	return TTAt(d.nameTx, lang);
}

static int NameGait(const wchar_t* n)
{
	if (!n) return GAIT_BIPED;
	if (wcsstr(n, L"Blob") || wcsstr(n, L"Glub") || wcsstr(n, L"Mush") || wcsstr(n, L"Squid") || wcsstr(n, L"Ghost") || wcsstr(n, L"Cactoro"))
		return GAIT_BOUNCE;
	if (wcsstr(n, L"Snake") || wcsstr(n, L"Worm")) return GAIT_SLITHER;
	if (wcsstr(n, L"Spider") || wcsstr(n, L"Wasp") || wcsstr(n, L"Bee") || wcsstr(n, L"Armabee")) return GAIT_SCUTTLE;
	if (wcsstr(n, L"Frog") || wcsstr(n, L"Chicken") || wcsstr(n, L"Birb") || wcsstr(n, L"Pigeon")) return GAIT_WADDLE;
	if (wcsstr(n, L"Gole")) return GAIT_STOMP;
	if (wcsstr(n, L"Fish") || wcsstr(n, L"Shark") || wcsstr(n, L"Whale") || wcsstr(n, L"Dolphin") || wcsstr(n, L"Manta")
		|| wcsstr(n, L"Tang") || wcsstr(n, L"Koi") || wcsstr(n, L"Tuna") || wcsstr(n, L"Tetra") || wcsstr(n, L"Piranha")
		|| wcsstr(n, L"Puffer") || wcsstr(n, L"Goldfish") || wcsstr(n, L"Betta") || wcsstr(n, L"Snapper") || wcsstr(n, L"Grouper")
		|| wcsstr(n, L"Gramma") || wcsstr(n, L"Idol") || wcsstr(n, L"Humphead") || wcsstr(n, L"Turbot")
		|| wcsstr(n, L"Clown") || wcsstr(n, L"Angler"))
		return GAIT_FISH;
	if (wcsstr(n, L"Horse") || wcsstr(n, L"Donkey") || wcsstr(n, L"Bull") || wcsstr(n, L"Cow") || wcsstr(n, L"Deer")
		|| wcsstr(n, L"Stag") || wcsstr(n, L"Alpaca") || wcsstr(n, L"Alpa") || wcsstr(n, L"Fox") || wcsstr(n, L"Wolf")
		|| wcsstr(n, L"Husky") || wcsstr(n, L"Shiba") || wcsstr(n, L"Bunny") || wcsstr(n, L"Cat") || wcsstr(n, L"Rat")
		|| wcsstr(n, L"Dino") || wcsstr(n, L"saurus") || wcsstr(n, L"Trex") || wcsstr(n, L"Tricera") || wcsstr(n, L"Veloci")
		|| wcsstr(n, L"Dragon"))
		return GAIT_TROT;
	return GAIT_BIPED;
}

int MonsterGait(int monster)
{
	const MonsterDef& md = MonsterInfo(monster);
	if (md.look == LK_SLIME) return GAIT_BOUNCE;
	if (md.look == LK_GOLEM) return GAIT_STOMP;
	if (md.look == LK_BAT || md.look == LK_WYVERN || md.moveType == MV_FLY) return GAIT_FLY;
	if (md.look == LK_MESH && md.model >= 0 && md.model < kImportN) return NameGait(kImportName[md.model]);
	return GAIT_BIPED;
}

static int MonsterFirstStage(int id)
{
	const MonsterDef& d = MonsterInfo(id);
	int lo = d.minStage < 1 ? 1 : d.minStage;
	if (id >= M_COUNT) {
		int model = id - M_COUNT;
		if (model >= 0 && model < kImportN && ImportPool(kImportName[model]) == POOL_FISH && lo < 10) lo = 10;
	}
	return lo;
}

int MonsterDebut(int id)
{
	EnsureMonsters();
	return MonsterFirstStage(id);
}

bool MonsterKnown(int id, int cleared)
{
	if (cleared < 1 || id < 0) return false;
	EnsureMonsters();
	if (id >= M_COUNT) {
		int model = id - M_COUNT;
		if (model < 0 || model >= kImportN || wcsncmp(kImportName[model], L"Sum", 3) == 0 || ImportPool(kImportName[model]) == POOL_HUM) return false;
	} else if (id >= M_COUNT) return false;
	const MonsterDef& d = MonsterInfo(id);
	int lo = MonsterFirstStage(id);
	int view = StageFoeViewHi(cleared);
	if (lo > view) return false;
	if (lo >= view) return false;
	int hi = d.maxStage > 0 ? d.maxStage : TAC_STAGE_MAX;
	int top = cleared < hi ? cleared : hi;
	if (top > view) top = view;
	return lo <= top;
}

int MonsterCatalog(int* out, int cap)
{
	EnsureMonsters();
	int n = 0;
	if (!out || cap <= 0) return 0;
	for (int i = 0; i < M_COUNT && n < cap; i++) out[n++] = i;
	int nImp = kImportN < IMP_CAP ? kImportN : IMP_CAP;
	for (int i = 0; i < nImp && n < cap; i++) {
		if (wcsncmp(kImportName[i], L"Sum", 3) == 0 || ImportPool(kImportName[i]) == POOL_HUM) continue;
		out[n++] = M_COUNT + i;
	}
	for (int i = 1; i < n; i++) {
		int v = out[i];
		int j = i;
		while (j > 0) {
			int a = out[j - 1];
			int da = MonsterFirstStage(a), dv = MonsterFirstStage(v);
			if (da < dv || (da == dv && a <= v)) break;
			out[j] = a;
			--j;
		}
		out[j] = v;
	}
	return n;
}


// ============================================================ キャラ
// 7〜32 の間に、役割の違う技を8つだけ覚える。同じ型の下位は入れない。
int PaceRoleOfLv(int lv)
{
	for (int i = 0; i < 8; i++) if (kPaceLv[i] == lv) return i;
	return -1;
}
static int PaceSkill(int cls, int lv)
{
	if (PaceRoleOfLv(lv) < 0) return -1;
	const ClassDef& cd = ClassInfo(cls);
	int g = cd.group;
	if (g < 0 || g >= G_COUNT) g = 0;
	return SKL_ART0 + g * 26 + (lv - 7);
}

static int SupportAreaCap(const ClassDef& cd);
static void CharTakeSkill(CharData& c, int s);
void CharGrantReached(CharData& c)
{
	const ClassDef& cd = ClassInfo(c.cls);
	// 4次以降は着任時に戦技を全部剥がさない。覚える本数は learn 側で遅くする。
	if (cd.tier <= 3) {
		for (int i = 0; i < SKL_ARTN - SKL_ART0; i++) CharTakeSkill(c, SKL_ART0 + i);
	}
	for (int i = 0; i < LEARN_N; i++)
		if (cd.learn[i].skill >= 0 && cd.learn[i].lv > 0 && cd.learn[i].lv <= c.lv)
			CharGiveSkill(c, cd.learn[i].skill);
	if (cd.tier <= 3) {
	int hi = c.lv < 32 ? c.lv : 32;
	for (int lv = 7; lv <= hi; lv++) {
		int sk = PaceSkill(c.cls, lv);
		if (sk >= 0) CharGiveSkill(c, sk);
	}
	if (cd.group == G_HOLY && c.lv >= 7) CharGiveSkill(c, SKL_RAISE);
	{
		if (cd.magic && (cd.group == G_HOLY || cd.group == G_MAGIC)) {
			// 下位の円は二次職の入口、上位の円は三次職に入ってから。
			if (c.lv >= 8) CharGiveSkill(c, SKL_GALE);
			if (cd.tier >= 2 && c.lv >= 15) {
				CharGiveSkill(c, SKL_FIRE);
				CharGiveSkill(c, SKL_BLIZZARD);
				CharGiveSkill(c, SKL_THUNDER);
				CharGiveSkill(c, SKL_HOLY);
			}
			if (cd.tier >= 2 && c.lv >= 26) {
				CharGiveSkill(c, SKL_GRAVITY);
				CharGiveSkill(c, SKL_DARKNESS);
			}
			if (cd.tier >= 3 && c.lv >= 35) CharGiveSkill(c, SKL_MIASMA);
		} else if (!cd.magic && cd.tier >= 3) {
			if (c.lv >= 35) {
				CharGiveSkill(c, SKL_ELFIRE);
				CharGiveSkill(c, SKL_ELICE);
				CharGiveSkill(c, SKL_ELTHUNDER);
				CharGiveSkill(c, SKL_ELWIND);
			}
			if (c.lv >= 48) {
				CharGiveSkill(c, SKL_ELHOLY);
				CharGiveSkill(c, SKL_ELDARK);
				CharGiveSkill(c, SKL_ELEATH);
				CharGiveSkill(c, SKL_ELPOISON);
			}
		}
	}
	}
	{
		int cap = SupportAreaCap(cd);
		for (int rad = 2; rad <= 9; rad++) {
			int heal = SKL_AREAHEAL0 + (rad - 2);
			int raise = SKL_AREARAISE0 + (rad - 2);
			if (rad == cap) {
				CharGiveSkill(c, heal);
				CharGiveSkill(c, raise);
			} else {
				CharTakeSkill(c, heal);
				CharTakeSkill(c, raise);
			}
		}
	}
	if (c.stat[S_MP] < cd.base[S_MP]) c.stat[S_MP] = cd.base[S_MP];
}

void CharRecalcInit(CharData& c)
{
	const ClassDef& cd = ClassInfo(c.cls);
	for (int s = 0; s < S_COUNT; s++) c.stat[s] = cd.base[s];
	if (c.gender == 1) { c.stat[S_AGI] += 1; c.stat[S_RES] += 1; c.stat[S_STR] = max(1, c.stat[S_STR] - 1); }
	c.lv = 1;
	c.exp = 0;
	c.skills = 0;
	memset(c.reserved, 0, sizeof(c.reserved));
	memset(c.skillBits, 0, sizeof(c.skillBits));
	CharGrantReached(c);
	JobPathClear(c);
	if (c.cls >= 0) c.jobPath[0] = c.cls;
	c.jobDone = 1;
}

static int MoveCap(int moveType) { return moveType == MV_FLY ? 14 : 12; }
static int StatFloor(int s)
{
	if (s == S_MP || s == S_MAG || s == S_RES || s == S_RCV || s == S_MPR || s == S_CPL || s == S_RNG || s == S_PEN || s == S_GRD || s == S_HIT || s == S_AVO || s == S_CRT) return 0;
	return 1;
}
// 現在の職とレベルから能力を期待値で組み直す。技・装備・経験値はそのまま。
void CharRecalcProgress(CharData& c)
{
	if (!c.used) return;
	const ClassDef& cd = ClassInfo(c.cls);
	int lv = Clampi(c.lv, 1, TAC_LV_MAX);
	int steps = lv - 1;
	for (int s = 0; s < S_COUNT; s++) {
		int v = cd.base[s];
		if (s == S_HP || s == S_MP) v += (cd.grow[s] / 10) * steps;
		else v += cd.grow[s] * steps / 100;
		c.stat[s] = max(StatFloor(s), v);
	}
	if (c.gender == 1) {
		c.stat[S_AGI] += 1;
		c.stat[S_RES] += 1;
		c.stat[S_STR] = max(1, c.stat[S_STR] - 1);
	}
	int cap = MoveCap(cd.moveType);
	if (c.stat[S_MOV] > cap) c.stat[S_MOV] = cap;
	if (c.stat[S_JMP] > 12) c.stat[S_JMP] = 12;
	if (c.stat[S_RNG] > 3) c.stat[S_RNG] = 3;
	c.lv = lv;
}

void CharGiveSkill(CharData& c, int s)
{
	if (s < 0 || s >= 24 * 32) return;
	int w = s >> 5;
	int b = s & 31;
	if (w >= 0 && w < 24) c.skillBits[w] |= (int)(1u << b);
	if (s < 64) c.skills |= 1ull << s;
	else if (s < 256) {
		int i = s - 64;
		int rw = i >> 5;
		if (rw >= 0 && rw < 6) c.reserved[rw] |= (int)(1u << (i & 31));
	}
}
static void CharTakeSkill(CharData& c, int s)
{
	if (s < 0 || s >= 24 * 32) return;
	int w = s >> 5;
	int b = s & 31;
	if (w >= 0 && w < 24) c.skillBits[w] &= ~(int)(1u << b);
	if (s < 64) c.skills &= ~(1ull << s);
	else if (s < 256) {
		int i = s - 64;
		int rw = i >> 5;
		if (rw >= 0 && rw < 6) c.reserved[rw] &= ~(int)(1u << (i & 31));
	}
}
static int SupportAreaCap(const ClassDef& cd)
{
	if (cd.tier < 3) return 0;
	int rad = cd.tier - 1;
	if (rad > 9) rad = 9;
	if (cd.group != G_HOLY && rad > 3) rad = 3;
	return rad;
}

bool CharHasSkill(const CharData& c, int s)
{
	if (s < 0 || s >= 24 * 32) return false;
	int w = s >> 5;
	if (c.skillBits[w] & (int)(1u << (s & 31))) return true;
	if (s < 64) return (c.skills & (1ull << s)) != 0;
	if (s < 256) {
		int i = s - 64;
		int rw = i >> 5;
		if (rw >= 0 && rw < 6) return (c.reserved[rw] & (int)(1u << (i & 31))) != 0;
	}
	return false;
}

int CharLearnAt(const CharData& c, int lv)
{
	const ClassDef& cd = ClassInfo(c.cls);
	for (int i = 0; i < LEARN_N; i++) if (cd.learn[i].skill >= 0 && cd.learn[i].lv == lv) return cd.learn[i].skill;
	if (cd.group == G_HOLY && lv == 7) return SKL_RAISE;
	return PaceSkill(c.cls, lv);
}

int CharLevelUp(CharData& c, Rng& r, int gains[S_COUNT], int* learned, int learnedMax)
{
	const ClassDef& cd = ClassInfo(c.cls);
	int total = 0;
	for (int s = 0; s < S_COUNT; s++) {
		// これからのレベルだけ伸ばす。既存の能力は組み直さない。
		int g = cd.grow[s] * 2, add = 0;
		if (s == S_HP || s == S_MP) {
			add = g / 10 + ((int)(r.Next() % 10) < g % 10 ? 1 : 0);
		} else {
			add = g / 100 + ((int)(r.Next() % 100) < g % 100 ? 1 : 0);
		}
		gains[s] = add;
		c.stat[s] += add;
		if (s == S_MOV) {
			int cap = MoveCap(cd.moveType);
			if (c.stat[s] > cap) { gains[s] -= c.stat[s] - cap; c.stat[s] = cap; }
		} else if (s == S_JMP && c.stat[s] > 12) {
			gains[s] -= c.stat[s] - 12; c.stat[s] = 12;
		} else if (s == S_RNG && c.stat[s] > 3) {
			gains[s] -= c.stat[s] - 3; c.stat[s] = 3;
		}
		if (s != S_HP && s != S_MP) total += add;
	}
	if (total == 0) {
		int s = cd.magic ? S_MAG : S_STR;
		gains[s]++; c.stat[s]++;
	}
	c.lv++;
	int got[8], ng = 0;
	auto take = [&](int sk) {
		if (sk < 0 || ng >= 8 || CharHasSkill(c, sk)) return;
		for (int i = 0; i < ng; i++) if (got[i] == sk) return;
		CharGiveSkill(c, sk);
		got[ng++] = sk;
	};
	take(CharLearnAt(c, c.lv));
	take(PaceSkill(c.cls, c.lv));
	for (int i = 0; i < LEARN_N; i++) if (cd.learn[i].lv == c.lv) take(cd.learn[i].skill);
	if (learned && learnedMax > 0) {
		int n = ng < learnedMax ? ng : learnedMax;
		for (int i = 0; i < n; i++) learned[i] = got[i];
		return n;
	}
	return ng > 0 ? got[0] : -1;
}

// ============================================================ 進化
struct EvoRow { int from; EvoOption o[4]; };
static const EvoRow kEvo[] = {
	{ C_SOLDIER,    { { C_PALADIN, -1 }, { C_SKYKNIGHT, -1 }, { C_HERO, -1 }, { -1, -1 } } },
	{ C_FIGHTER,    { { C_BERSERKER, -1 }, { C_GLADIATOR, -1 }, { C_DRAGONLORD, -1 }, { -1, -1 } } },
	{ C_ARCHER,     { { C_SNIPER, -1 }, { C_PHANTOM, -1 }, { C_JOB0 + 29, -1 }, { -1, -1 } } },
	{ C_THIEF,      { { C_ASSASSIN, -1 }, { C_SHADOW, -1 }, { C_JOB0 + 36, -1 }, { -1, -1 } } },
	{ C_MAGE,       { { C_WIZARD, -1 }, { C_ALCHEMIST, -1 }, { C_ARCHMAGE, -1 }, { C_BISHOP, -1 } } },
	{ C_CLERIC,     { { C_BISHOP, -1 }, { C_SAINT, -1 }, { C_WIZARD, -1 }, { -1, -1 } } },
	{ C_PALADIN,    { { C_HERO, -1 }, { C_JOB0 + 23, -1 }, { C_JOB0 + 37, -1 }, { -1, -1 } } },
	{ C_BERSERKER,  { { C_GLADIATOR, -1 }, { C_DRAGONLORD, -1 }, { C_JOB0 + 25, -1 }, { -1, -1 } } },
	{ C_RUNEKNIGHT, { { C_ARCHMAGE, -1 }, { C_ARCHALCHEMIST, -1 }, { C_JOB0 + 13, -1 }, { -1, -1 } } },
	{ C_SNIPER,     { { C_PHANTOM, -1 }, { C_JOB0 + 29, -1 }, { C_JOB0 + 30, -1 }, { -1, -1 } } },
	{ C_ASSASSIN,   { { C_SHADOW, -1 }, { C_JOB0 + 36, -1 }, { C_JOB0 + 39, -1 }, { -1, -1 } } },
	{ C_SKYKNIGHT,  { { C_HERO, -1 }, { C_JOB0 + 23, -1 }, { C_JOB0 + 37, -1 }, { -1, -1 } } },
	{ C_WIZARD,     { { C_ARCHMAGE, -1 }, { C_ARCHALCHEMIST, -1 }, { C_JOB0 + 13, -1 }, { -1, -1 } } },
	{ C_BISHOP,     { { C_SAINT, -1 }, { C_JOB0 + 26, -1 }, { C_JOB0 + 72, -1 }, { -1, -1 } } },
	{ C_ALCHEMIST,  { { C_ARCHALCHEMIST, -1 }, { C_ARCHMAGE, -1 }, { C_JOB0 + 13, -1 }, { -1, -1 } } },
};

static int LegacyLine(int cls)
{
	if (cls == C_GLADIATOR) return 6;
	if (cls == C_SHADOW) return 7;
	if (cls >= C_PLAYER_COUNT && cls < C_PLAYER_COUNT + TAC_RANK_LINES * TAC_RANK_N)
		return (cls - C_PLAYER_COUNT) / TAC_RANK_N;
	for (int i = 0; i < TAC_RANK_LINES; i++) if (kLineParent[i] == cls) return i;
	return -1;
}

int EvoOptions(int cls, EvoOption out[4])
{
	int n = 0;
	if (cls >= C_JOB0 && cls < C_JOB0 + TAC_JOB_N) {
		int i = cls - C_JOB0;
		for (int k = 0; k < 4; k++) {
			int t = kJobNext[i][k];
			if (t < 0 || n >= 4) continue;
			out[n].target = C_JOB0 + t;
			out[n].needGroup = -1;
			n++;
		}
		return n;
	}
	int line = LegacyLine(cls);
	if (line >= 0) {
		int tier = ClassInfo(cls).tier;
		if (tier >= 3 && tier < 12) {
			for (int k = 0; k < 4; k++) {
				int t = kLineOffer[line][tier - 3][k];
				if (t < 0 || n >= 4) continue;
				out[n].target = C_JOB0 + t;
				out[n].needGroup = -1;
				n++;
			}
			if (n > 0) return n;
		}
	}
	for (int i = 0; i < (int)_countof(kEvo); i++) {
		if (kEvo[i].from != cls) continue;
		for (int k = 0; k < 4; k++) if (kEvo[i].o[k].target >= 0 && n < 4) out[n++] = kEvo[i].o[k];
	}
	return n;
}

bool EvoMainEligible(const CharData& c)
{
	if (!c.used) return false;
	const ClassDef& cd = ClassInfo(c.cls);
	EvoOption o[4];
	if (EvoOptions(c.cls, o) == 0) return false;
	return c.lv >= PromoteLevel(cd.tier);
}

bool EvoTrialOpen(const CharData& c)
{
	if (!c.used || c.trialLv <= 0 || c.trialFrom < 0) return false;
	if (c.lv > c.trialLv + 3) return false;
	return c.cls != c.trialFrom;
}

static void EvoShift(CharData& c, int fromCls, int toCls, int sign)
{
	const ClassDef& a = ClassInfo(fromCls);
	const ClassDef& b = ClassInfo(toCls);
	for (int s = 0; s < S_COUNT; s++) {
		int d = b.base[s] - a.base[s];
		if (d > 0) c.stat[s] += d * sign;
		if (s == S_MOV && sign > 0) c.stat[s] = max(c.stat[s], b.base[s]);
		if (s == S_MOV && sign < 0 && c.stat[s] < a.base[s]) c.stat[s] = a.base[s];
	}
	int bump = 2 + b.tier / 2;
	auto add = [&](int s, int v) { c.stat[s] += v * sign; };
	if (b.group == G_MELEE) {
		add(S_STR, bump);
		add(S_DEF, 1);
		add(S_HP, 6 + b.tier);
	} else if (b.group == G_SWIFT) {
		add(S_AGI, bump);
		add(S_DEX, bump);
		add(S_HP, 3);
	} else if (b.group == G_MAGIC) {
		add(S_MAG, bump);
		add(S_RES, 1);
		add(S_MP, 6 + b.tier);
	} else {
		add(S_RES, bump);
		add(S_MAG, 1);
		add(S_MP, 4 + b.tier);
		add(S_HP, 3);
	}
	if (b.moveType == MV_FLY) add(S_JMP, 1);
	for (int s = 0; s < S_COUNT; s++) if (c.stat[s] < 0) c.stat[s] = 0;
	if (c.stat[S_HP] < 1) c.stat[S_HP] = 1;
}

static bool EvoTrialAllows(const CharData& c, int target)
{
	if (c.trialLv <= 0 || c.lv > c.trialLv + 3 || c.trialFrom < 0) return false;
	if (target == c.trialFrom) return true;
	EvoOption o[4];
	int n = EvoOptions(c.trialFrom, o);
	for (int k = 0; k < n; k++) if (o[k].target == target) return true;
	return false;
}

int EvoPartnersNeeded(const CharData&)
{
	return 0;
}

bool EvoPartnerEligible(const CharData& main, const CharData& p)
{
	if (!p.used || &main == &p || main.uid == p.uid) return false;
	return p.lv >= (ClassInfo(main.cls).tier >= 2 ? 15 : 5);
}

bool EvoGroupSatisfied(int g, const CharData* ps, int n)
{
	if (g < 0) return true;
	for (int i = 0; i < n; i++) if (ClassInfo(ps[i].cls).group == g) return true;
	return false;
}

void JobPathClear(CharData& c)
{
	for (int i = 0; i < JOB_PATH_N; i++) c.jobPath[i] = -1;
}
int JobPathCount(const CharData& c)
{
	int n = 0;
	for (; n < JOB_PATH_N; n++) if (c.jobPath[n] < 0) break;
	return n;
}
void JobPathText(const CharData& c, wchar_t* out, int cap)
{
	if (!out || cap <= 0) return;
	out[0] = 0;
	int n = JobPathCount(c);
	int steps = JobStepsForLevel(c.lv);
	if (steps < 1) steps = 1;
	if (n > steps) n = steps;
	int cls[JOB_PATH_N];
	int cn = 0;
	for (int i = 0; i < n && cn < JOB_PATH_N; i++) cls[cn++] = c.jobPath[i];
	if (cn == 0 || cls[cn - 1] != c.cls) {
		if (cn < JOB_PATH_N) cls[cn++] = c.cls;
		else cls[cn - 1] = c.cls;
	}
	for (int i = 0; i < cn; i++) {
		const wchar_t* nm = ClassTitle(cls[i]);
		if (!nm || !nm[0]) nm = TT(ClassInfo(cls[i]).nameTx);
		wchar_t bit[80];
		swprintf_s(bit, TT(TX_JOB_NTH), i + 1, nm);
		if (i && wcslen(out) + 3 < (size_t)cap) wcscat_s(out, cap, L" → ");
		if (wcslen(out) + wcslen(bit) + 1 < (size_t)cap) wcscat_s(out, cap, bit);
	}
}
int JobStepsForLevel(int lv)
{
	int steps = 1;
	for (int tier = 1; tier < 12; tier++) {
		if (lv >= PromoteLevel(tier)) steps = tier + 1;
		else break;
	}
	if (steps > JOB_PATH_N) steps = JOB_PATH_N;
	if (steps < 1) steps = 1;
	return steps;
}
static void JobPathNote(CharData& c, int from, int to, int replaceTip)
{
	if (to < 0) return;
	int n = JobPathCount(c);
	if (replaceTip) {
		if (n <= 0) {
			if (from >= 0) c.jobPath[0] = from;
			if (to != from) c.jobPath[from >= 0 ? 1 : 0] = to;
		} else c.jobPath[n - 1] = to;
		return;
	}
	if (n <= 0) {
		if (from >= 0) c.jobPath[0] = from;
		if (to != from) c.jobPath[from >= 0 ? 1 : 0] = to;
		return;
	}
	if (c.jobPath[n - 1] == to) return;
	if (n < JOB_PATH_N) c.jobPath[n] = to;
}
// CharLevelUp と同じく成長率を2倍し、端数はレベルをまたいで積む。画面を開くたびに振らない。
static void JobGrowOnce(CharData& c, int acc[S_COUNT])
{
	const ClassDef& cd = ClassInfo(c.cls);
	int total = 0;
	for (int s = 0; s < S_COUNT; s++) {
		int g = cd.grow[s] * 2;
		int add;
		if (s == S_HP || s == S_MP) {
			add = g / 10;
			acc[s] += g % 10;
			if (acc[s] >= 10) { add++; acc[s] -= 10; }
		} else {
			add = g / 100;
			acc[s] += g % 100;
			if (acc[s] >= 100) { add++; acc[s] -= 100; }
		}
		c.stat[s] += add;
		if (s == S_MOV) {
			int cap = MoveCap(cd.moveType);
			if (c.stat[s] > cap) c.stat[s] = cap;
		} else if (s == S_JMP && c.stat[s] > 12) c.stat[s] = 12;
		else if (s == S_RNG && c.stat[s] > 3) c.stat[s] = 3;
		if (s != S_HP && s != S_MP) total += add;
	}
	if (total == 0) {
		int s = cd.magic ? S_MAG : S_STR;
		c.stat[s]++;
	}
	if (c.lv < TAC_LV_MAX) c.lv++;
}
void JobProject(CharData& c, const int* path, int n, int toNext)
{
	if (!path || n < 1) return;
	if (n > JOB_PATH_N) n = JOB_PATH_N;
	while (n > 0 && path[n - 1] < 0) n--;
	if (n < 1 || path[0] < 0) return;
	int finalLv = c.lv > 0 ? c.lv : 1;
	if (finalLv > TAC_LV_MAX) finalLv = TAC_LV_MAX;
	int steps = JobStepsForLevel(finalLv);
	int gender = c.gender;
	const ClassDef& b = ClassInfo(path[0]);
	for (int s = 0; s < S_COUNT; s++) c.stat[s] = b.base[s];
	if (gender == 1) {
		c.stat[S_AGI] += 1;
		c.stat[S_RES] += 1;
		c.stat[S_STR] = max(1, c.stat[S_STR] - 1);
	}
	c.cls = path[0];
	c.lv = 1;
	int acc[S_COUNT];
	for (int s = 0; s < S_COUNT; s++) acc[s] = 0;
	for (int idx = 0; idx < n; idx++) {
		if (path[idx] < 0) break;
		c.cls = path[idx];
		BOOL last = (idx + 1 >= n);
		int gate = finalLv;
		if (!last || (toNext && n < steps)) gate = PromoteLevel(ClassInfo(path[idx]).tier);
		if (gate > finalLv) gate = finalLv;
		if (gate < c.lv) gate = c.lv;
		while (c.lv < gate) JobGrowOnce(c, acc);
		if (last || c.lv >= finalLv) break;
	}
	int tip = path[n - 1] >= 0 ? path[n - 1] : path[0];
	c.cls = tip;
}
void JobRefitGear(CharData& c, SaveData& s)
{
	for (int slot = 6; slot <= 7; slot++) {
		int id = c.reserved[slot];
		if (id <= 0) continue;
		const GearDef* old = GearOf(id);
		if (!old) { c.reserved[slot] = 0; continue; }
		if (GearCanWear(c.cls, old)) continue;
		int kind = old->kind;
		int exact = 0, exactPow = -1;
		int best = 0, bestDiff = 0x7fffffff, bestPow = -1;
		int gn = GearCount();
		for (int i = 1; i <= gn; i++) {
			const GearDef* g = GearOf(i);
			if (!g || g->kind != kind || !GearCanWear(c.cls, g)) continue;
			int pow = kind ? (g->def + g->res) : (g->str + g->mag);
			int diff = g->price - old->price;
			if (diff < 0) diff = -diff;
			if (diff == 0 && pow > exactPow) { exactPow = pow; exact = i; }
			if (diff < bestDiff || (diff == bestDiff && pow > bestPow)) {
				bestDiff = diff;
				bestPow = pow;
				best = i;
			}
		}
		int pick = exact > 0 ? exact : best;
		if (pick > 0) c.reserved[slot] = pick;
		else {
			StashAdd(s, id);
			c.reserved[slot] = 0;
		}
	}
}
void JobRebook(CharData& c, const int* path, int n)
{
	if (!path || n < 1) return;
	if (n > JOB_PATH_N) n = JOB_PATH_N;
	int lv = c.lv > 0 ? c.lv : 1;
	int exp = c.exp;
	int gearW = c.reserved[6];
	int gearA = c.reserved[7];
	c.skills = 0;
	for (int i = 0; i < 6; i++) c.reserved[i] = 0;
	c.reserved[6] = gearW;
	c.reserved[7] = gearA;
	memset(c.skillBits, 0, sizeof(c.skillBits));
	JobPathClear(c);
	for (int i = 0; i < n; i++) c.jobPath[i] = path[i];
	c.jobDone = 1;
	c.cls = path[0];
	c.lv = 1;
	for (int i = 0; i < n; i++) {
		int hold = lv;
		if (i + 1 < n) {
			int gate = PromoteLevel(ClassInfo(path[i]).tier);
			if (gate > 0 && gate < hold) hold = gate;
		}
		c.cls = path[i];
		c.lv = hold > 0 ? hold : 1;
		CharGrantReached(c);
	}
	c.cls = path[n - 1];
	c.lv = lv;
	c.exp = exp;
	JobProject(c, path, n, 0);
	c.exp = exp;
	CharGrantReached(c);
	if (n >= 2) {
		int prev = path[n - 2];
		int gate = PromoteLevel(ClassInfo(prev).tier);
		if (lv >= gate && lv <= gate + 3) {
			c.trialFrom = prev;
			c.trialLv = gate > 0 ? gate : lv;
		} else {
			c.trialFrom = -1;
			c.trialLv = 0;
		}
	} else {
		c.trialFrom = -1;
		c.trialLv = 0;
	}
}

void EvoRetrain(CharData& c, int target)
{
	if (!EvoTrialAllows(c, target) || target == c.cls) return;
	int cur = c.cls;
	int from = c.trialFrom;
	EvoShift(c, from, cur, -1);
	if (target != from) EvoShift(c, from, target, +1);
	c.cls = target;
	CharGrantReached(c);
	JobPathNote(c, from, target, 1);
}

void EvoApply(CharData& c, int target, const CharData* ps, int n)
{
	int old = c.cls;
	bool window = c.trialLv > 0 && c.lv <= c.trialLv + 3 && c.trialFrom >= 0;
	int keepFrom = c.trialFrom;
	int keepLv = c.trialLv;
	EvoShift(c, old, target, +1);
	for (int s = 0; s < S_COUNT; s++) {
		if (s == S_MOV) continue;
		for (int i = 0; i < n; i++) c.stat[s] += ps[i].stat[s] / 10;
	}
	c.cls = target;
	// 段が上がった転職だけお試しを始める。お試し中の選び直しは期限を延ばさない。
	if (window) {
		c.trialFrom = keepFrom;
		c.trialLv = keepLv;
		JobPathNote(c, keepFrom, target, 1);
	} else if (ClassInfo(target).tier > ClassInfo(old).tier) {
		c.trialFrom = old;
		c.trialLv = c.lv > 0 ? c.lv : 1;
		JobPathNote(c, old, target, 0);
	}
	// 転職前に覚えたスキルはそのまま残し、新しい職業で到達している分を足す。
	int inherited = 0;
	for (int i = 0; i < n && inherited < 2; i++) {
		for (int sk = 0; sk < SKL_COUNT && inherited < 2; sk++) {
			if (SkillInfo(sk).enemyOnly || !CharHasSkill(ps[i], sk) || CharHasSkill(c, sk)) continue;
			CharGiveSkill(c, sk);
			inherited++;
		}
	}
	CharGrantReached(c);
}

#include "tactics_src/evo_brief.inc"

// ============================================================ ユニット
static bool IsMon(const Unit& u) { return u.monster >= 0; }

int UnitStat(const Unit& u, int s)
{
	int v = u.ch.stat[s];
	const GearDef* w = GearOf(u.ch.reserved[6]);
	const GearDef* a = GearOf(u.ch.reserved[7]);
	auto addGear = [&](const GearDef* g) {
		if (!g) return;
		if (s == S_STR) v += g->str;
		if (s == S_MAG) v += g->mag;
		if (s == S_DEF) v += g->def;
		if (s == S_RES) v += g->res;
	};
	addGear(w);
	addGear(a);
	v += AccStat(u.accEq[0], s) + AccStat(u.accEq[1], s);
	if (s == S_DEF && u.buffDef > 0) v = v * 13 / 10;
	if ((s == S_DEF || s == S_RES) && u.defDown > 0) v = v * 75 / 100;
	if ((s == S_AGI) && u.buffAgi > 0) v = v * 13 / 10;
	if (s == S_AGI && u.slow > 0) v = v * 75 / 100;
	if ((s == S_STR || s == S_MAG) && u.buffAtk > 0) v = v * 125 / 100;
	if (u.mulTurns > 0 && (s == S_STR || s == S_MAG || s == S_DEF || s == S_RES || s == S_AGI)) v *= 2;
	if (s == S_DEF && UnitHasSkill(u, SKL_P_IRON)) v = v * 115 / 100;
	if (s == S_STR && UnitHasPas(u, PAS_STR2)) v = v * 120 / 100;
	else if (s == S_STR && UnitHasPas(u, PAS_STR)) v = v * 110 / 100;
	if (s == S_MAG && UnitHasPas(u, PAS_MAG2)) v = v * 120 / 100;
	else if (s == S_MAG && UnitHasPas(u, PAS_MAG)) v = v * 110 / 100;
	if (s == S_DEF && UnitHasPas(u, PAS_DEF2)) v = v * 120 / 100;
	else if (s == S_DEF && UnitHasPas(u, PAS_DEF)) v = v * 110 / 100;
	if (s == S_RES && UnitHasPas(u, PAS_RES2)) v = v * 120 / 100;
	else if (s == S_RES && UnitHasPas(u, PAS_RES)) v = v * 110 / 100;
	if (s == S_AGI && UnitHasPas(u, PAS_AGI)) v = v * 110 / 100;
	if (s == S_LUK && UnitHasPas(u, PAS_LUK)) v += 4;
	if (s == S_RCV && UnitHasPas(u, PAS_RCV)) v += 4;
	if (s == S_CPL && UnitHasPas(u, PAS_CPL)) v += 4;
	if (s == S_HIT && UnitHasPas(u, PAS_HIT)) v += 6;
	if (s == S_AVO && UnitHasPas(u, PAS_AVO)) v += 6;
	if (s == S_PEN && UnitHasPas(u, PAS_PEN)) v += 4;
	if (s == S_MOV && UnitHasPas(u, PAS_MOV2)) v += 2;
	else if (s == S_MOV && UnitHasPas(u, PAS_MOV)) v += 1;
	if (s == S_JMP && UnitHasPas(u, PAS_JMP2)) v += 3;
	else if (s == S_JMP && UnitHasPas(u, PAS_JMP)) v += 2;
	if (s == S_RNG && UnitHasPas(u, PAS_RNG2)) v += 2;
	else if (s == S_RNG && UnitHasPas(u, PAS_RNG)) v += 1;
	if (s == S_MOV) {
		int cap = MoveCap(UnitMoveType(u));
		if (v > cap) v = cap;
		if (v < 1) v = 1;
	} else if (s == S_JMP) {
		int cap = UnitHasPas(u, PAS_JMP2) ? 16 : (UnitHasPas(u, PAS_JMP) ? 14 : 12);
		if (v > cap) v = cap;
		if (v < 1) v = 1;
	} else if (s == S_RNG) {
		int cap = UnitHasPas(u, PAS_RNG2) ? 5 : (UnitHasPas(u, PAS_RNG) ? 4 : 3);
		if (v > cap) v = cap;
		if (v < 0) v = 0;
	}
	return v;
}
void CharFightBonus(const CharData& c, int out[S_COUNT])
{
	Unit u{};
	u.monster = -1;
	u.alive = 1;
	u.ch = c;
	CraftBindUnit(u);
	for (int s = 0; s < S_COUNT; s++) out[s] = UnitStat(u, s) - c.stat[s];
}
int UnitMove(const Unit& u) { return UnitStat(u, S_MOV); }
int UnitJump(const Unit& u) { return UnitStat(u, S_JMP); }
int UnitMoveType(const Unit& u)
{
	int mt = IsMon(u) ? MonsterInfo(u.monster).moveType : ClassInfo(u.ch.cls).moveType;
	if (UnitHasPas(u, PAS_FLY)) mt = MV_FLY;
	return mt;
}
int UnitRangeMin(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).rMin : ClassInfo(u.ch.cls).rMin; }
int UnitRangeMax(const Unit& u)
{
	int r = (IsMon(u) ? MonsterInfo(u.monster).rMax : ClassInfo(u.ch.cls).rMax) + UnitStat(u, S_RNG);
	if (r < 1) r = 1;
	if (r > 9) r = 9;
	return r;
}
int UnitMagAttack(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).magAttack : ClassInfo(u.ch.cls).magAttack; }
int UnitWeaponPow(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).weaponPow + u.ch.lv / 6 : ClassInfo(u.ch.cls).weaponPow + u.ch.lv / 8; }
int UnitAttackStyle(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).attackStyle : ClassInfo(u.ch.cls).attackStyle; }

static int GearMotion(const GearDef* g)
{
	if (!g || g->kind != 0) return -1;
	const char* a0 = g->att0 ? g->att0 : "";
	const char* a1 = g->att1 ? g->att1 : "";
	auto has = [&](const char* s) { return strstr(a0, s) || strstr(a1, s); };
	if (has("Crossbow") || has("Bow")) return has("1H_") ? AS_SHOOT1 : AS_SHOOT;
	if (has("Knife") || has("Dagger")) return AS_STAB;
	if (has("2H_Axe") || has("2H_Sword") || has("Claymore") || has("Sword_Big") || has("Sword_big")) return AS_CHOP2;
	if (has("Staff") || has("Wand")) return AS_CAST;
	if (has("Axe") || has("Sword") || has("Blade")) {
		int shield = 0;
		if (strstr(a1, "Shield")) shield = 1;
		else if (g->mesh1 >= 0 && !a1[0]) shield = 1;
		if (!shield && g->mesh1 < 0) return AS_CHOP2;
		return AS_SLASH;
	}
	switch (g->line) {
	case 0: return AS_SLASH;
	case 1: return AS_CHOP2;
	case 2: return AS_SHOOT;
	case 3: return AS_STAB;
	case 4: case 5: return AS_CAST;
	default: return -1;
	}
}
static int MotionFamily(int style)
{
	if (style == AS_SHOOT || style == AS_SHOOT1) return 1;
	if (style == AS_CAST) return 2;
	return 0;
}
int UnitMotionStyle(const Unit& u, int skill)
{
	if (IsMon(u)) return UnitAttackStyle(u);
	int gear = GearMotion(GearOf(u.ch.reserved[6]));
	int wear = gear >= 0 ? gear : UnitAttackStyle(u);
	if (skill < 0) return wear;
	if (IsFormSkill(skill)) {
		switch (FormOf(skill)) {
		case FM_CROSS: case FM_WALL: case FM_SQUARE: return AS_SPIN;
		case FM_BACK: case FM_CHAIN: return AS_STAB;
		case FM_TRI: return AS_CHOP2;
		case FM_ECHELON: return AS_SHOOT;
		case FM_CIRCLE: case FM_PAIR: return AS_CAST;
		case FM_DIA: return AS_SLASH;
		default: return AS_SLASH;
		}
	}
	const SkillDef& sd = SkillInfo(skill);
	if (sd.kind == SK_MAGIC || sd.kind == SK_HEAL || sd.kind == SK_BUFF) return AS_CAST;
	if (sd.kind != SK_PHYS) return wear;
	// 射程未指定の物理技は、弓でも弓を引かない。近接武器ならその振り。
	if (sd.rMax < 0) return (gear >= 0 && MotionFamily(gear) == 0) ? gear : AS_SLASH;
	int want = (sd.rMin >= 2) ? AS_SHOOT : AS_SLASH;
	if (gear >= 0 && MotionFamily(want) == MotionFamily(gear)) return gear;
	return want;
}
int UnitNameTx(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).nameTx : -1; }
const wchar_t* UnitName(const Unit& u)
{
	if (!IsMon(u)) return u.ch.name;
	const MonsterDef& md = MonsterInfo(u.monster);
	if (md.look == LK_MESH) return MonsterLabel(u.monster);
	if (md.nameB < 0) return TT(md.nameTx);
	static wchar_t buf[96];
	swprintf_s(buf, L"%s %s", TT(md.nameTx), TT(md.nameB));
	return buf;
}
bool UnitHasSkill(const Unit& u, int s)
{
	if (IsFormSkill(s)) return u.team == TEAM_PLAYER && !IsMon(u);
	return CharHasSkill(u.ch, s);
}

static void HeldPush(wchar_t* dst, int cap, const wchar_t* name)
{
	if (!dst || !name || !name[0] || cap < 4) return;
	size_t have = wcslen(dst);
	size_t add = wcslen(name);
	size_t sep = dst[0] ? 3 : 0;
	if (have + sep + add + 1 > (size_t)cap) return;
	if (sep) wcscat_s(dst, (size_t)cap, L" · ");
	wcscat_s(dst, (size_t)cap, name);
}

void UnitHeldText(const Unit& u, wchar_t* out, int cap)
{
	if (!out || cap < 2) return;
	out[0] = 0;
	wchar_t luck[160] = L"";
	if (UnitIsLucky(u)) wcsncpy_s(luck, TT(TX_BT_LUCKY), _TRUNCATE);
	wchar_t act[160] = L"", pas[80] = L"", cr[160] = L"";
	for (int s = 0; s < SKL_COUNT; s++) {
		if (!UnitHasSkill(u, s)) continue;
		const wchar_t* nm = SkillLabel(s);
		if (SkillInfo(s).kind == SK_PASSIVE) HeldPush(pas, 80, nm);
		else HeldPush(act, 160, nm);
	}
	for (int i = 0; i < CRAFT_SLOT_N; i++) {
		int id = u.craftEq[i];
		if (!IsCraftId(id)) continue;
		wchar_t nm[64];
		if (IsCraftSuper(id)) swprintf_s(nm, L"%s★", CraftName(id));
		else wcscpy_s(nm, CraftName(id));
		HeldPush(cr, 160, nm);
	}
	wchar_t sk[240] = L"";
	if (act[0] || pas[0]) {
		wcscpy_s(sk, TT(TX_BT_SKILL));
		wcscat_s(sk, L" ");
		if (act[0]) wcscat_s(sk, act);
		if (act[0] && pas[0]) wcscat_s(sk, L" · ");
		if (pas[0]) wcscat_s(sk, pas);
	}
	wchar_t cf[200] = L"";
	if (cr[0]) {
		wcscpy_s(cf, TT(TX_CRAFT));
		wcscat_s(cf, L" ");
		wcscat_s(cf, cr);
	}
	wchar_t rest[400] = L"";
	if (sk[0] && cf[0] && (int)(wcslen(sk) + wcslen(cf) + 5) < 400)
		swprintf_s(rest, L"%s    %s", sk, cf);
	else if (sk[0]) wcscpy_s(rest, sk);
	else if (cf[0]) wcscpy_s(rest, cf);
	if (luck[0] && rest[0]) swprintf_s(out, (size_t)cap, L"%s    %s", luck, rest);
	else if (luck[0]) wcsncpy_s(out, (size_t)cap, luck, _TRUNCATE);
	else if (rest[0]) wcsncpy_s(out, (size_t)cap, rest, _TRUNCATE);
}

// 足の半幅。タイル中心から 0.5 が端。
// 輸入メッシュは休息姿勢の下半身で測る。ブロブフィッシュは 1.68 で 2x2。
// 半幅 1.05 まで（猫や忍者、ゴーレム級の足）は 1。そこから 2.15 までは 2。胴が 2x2 の外まで出るものは 3。
static float ClassicFoot(int look)
{
	switch (look) {
	case LK_SLIME: return 0.42f;
	case LK_BAT: return 0.18f;
	case LK_WYVERN: return 0.28f;
	case LK_GOLEM: return 0.42f;
	default: return 0.20f;
	}
}
static int SpanFromHalf(float half)
{
	if (half > 3.15f) return 4;
	if (half > 2.15f) return 3;
	if (half > 1.05f) return 2;
	return 1;
}
static int HumanoidImport(const wchar_t* nm)
{
	if (!nm || wcsncmp(nm, L"Hum", 3) != 0 || !nm[3]) return 0;
	return nm[3] >= L'A' && nm[3] <= L'Z';
}
static int MonsterSpan(const MonsterDef& md, int boss)
{
	float sc = md.scale * (boss ? 1.15f : 1.f);
	float half = ClassicFoot(md.look);
	if (md.look == LK_MESH) {
		float f = TacMonsterFoot(md.model);
		if (f > 0.01f) half = f;
		if (md.model >= 0 && md.model < kImportN && HumanoidImport(kImportName[md.model])) half = 0.2f;
	}
	return SpanFromHalf(half * sc);
}

void MakeEnemy(Unit& u, int monster, int lv, int boss, Rng& r)
{
	memset(&u, 0, sizeof(u));
	const MonsterDef& md = MonsterInfo(monster);
	u.alive = 1;
	u.team = TEAM_ENEMY;
	u.rosterIdx = -1;
	u.monster = monster;
	u.boss = boss;
	u.lucky = 0;
	u.ch.used = 1;
	u.ch.cls = -1;
	u.ch.lv = Clampi(lv, 1, TAC_ENEMY_LV_MAX);
	for (int s = 0; s < S_COUNT; s++) {
		int g = md.grow[s];
		int v = md.base[s];
		if (s == S_AGI) {
			// 味方は成長率×2。同じ倍率だと群れが手番を埋め、低すぎると先手が来ない。
			int gAgi = g * 18 / 10;
			if (gAgi < 70) gAgi = 70;
			if (gAgi > 100) gAgi = 100;
			v += gAgi * (u.ch.lv - 1) / 100;
			v += 2;
		} else if (s == S_HP || s == S_MP) v += g * (u.ch.lv - 1) / 10;
		else v += g * (u.ch.lv - 1) / 100;
		// 味方は成長率×2で体力が伸びる。敵の体力だけ同じ方向に合わせ、攻撃は別の曲線で足す。
		// 序盤（〜Lv35）は従来どおり。会心が2倍なので、ステージ60〜70帯で雑魚が1発で落ちない厚みを足す。
		if (s == S_HP) {
			v *= 2;
			int bulk = 0;
			if (u.ch.lv >= 12) {
				bulk = 12 + (u.ch.lv - 12);
				if (u.ch.lv >= 36) bulk += (u.ch.lv - 36);
				if (u.ch.lv >= 72) bulk += (u.ch.lv - 72);
				if (u.ch.lv >= 120) bulk += (u.ch.lv - 120) / 2;
				if (bulk > 280) bulk = 280;
			}
			v = v * (100 + bulk) / 100;
		}
		if (s == S_DEF || s == S_RES) {
			int db = 0;
			if (u.ch.lv >= 12) {
				db = 8 + (u.ch.lv - 12) * 2 / 3;
				if (db > 55) db = 55;
			}
			v = v * (100 + db) / 100;
		}
		if (s != S_MOV && s != S_JMP && s != S_RNG && s != S_MP) v += r.Range(-1, 1);
		u.ch.stat[s] = max(StatFloor(s), v);
	}
	{
		int cap = MoveCap(md.moveType);
		if (u.ch.stat[S_MOV] > cap) u.ch.stat[S_MOV] = cap;
		if (u.ch.stat[S_JMP] > 12) u.ch.stat[S_JMP] = 12;
		if (u.ch.stat[S_RNG] > 3) u.ch.stat[S_RNG] = 3;
	}
	if (boss) {
		u.ch.stat[S_HP] = u.ch.stat[S_HP] * 3 / 2;
		u.ch.stat[S_STR] += 2; u.ch.stat[S_MAG] += 2; u.ch.stat[S_DEF] += 2; u.ch.stat[S_RES] += 2; u.ch.stat[S_AGI] += 2;
		u.ch.stat[S_MOV] = min(MoveCap(md.moveType), u.ch.stat[S_MOV] + 1);
		u.ch.stat[S_GRD] += 2;
		u.ch.stat[S_PEN] += 2;
	}
	// 序盤は今の攻撃のまま。中盤から線形で足し、体力の 1/4〜2/5 が目安（回復は要るが即死しない）。
	{
		int lv = u.ch.lv;
		int pct = 0;
		int flat = 0;
		if (lv > 16) {
			int t = lv - 16;
			if (t > 100) t = 100;
			pct = t * 6 / 5;
			if (pct > 140) pct = 140;
		}
		if (lv > 28) {
			int t = lv - 28;
			if (t > 80) t = 80;
			flat = t / 2;
			if (flat > 40) flat = 40;
		}
		if (lv > 140) {
			int extra = (lv - 140) / 8;
			if (extra > 60) extra = 60;
			pct += extra;
		}
		if (pct > 180) pct = 180;
		if (pct > 0) {
			u.ch.stat[S_STR] = u.ch.stat[S_STR] * (100 + pct) / 100;
			u.ch.stat[S_MAG] = u.ch.stat[S_MAG] * (100 + pct) / 100;
		}
		if (flat > 0) {
			if (u.ch.stat[S_STR] >= u.ch.stat[S_MAG]) u.ch.stat[S_STR] += flat;
			else u.ch.stat[S_MAG] += flat;
		}
		if (boss && lv > 16) {
			u.ch.stat[S_STR] = u.ch.stat[S_STR] * 108 / 100;
			u.ch.stat[S_MAG] = u.ch.stat[S_MAG] * 108 / 100;
		}
	}
	for (int i = 0; i < 3; i++) {
		int sk = md.skills[i];
		if (sk < 0) continue;
		int need = i == 0 ? 1 : (i == 1 ? 8 : 15);
		if (sk == SKL_E_SPIT || sk == SKL_E_CRUSH || sk == SKL_E_QUAKE || sk == SKL_DARKNESS || sk == SKL_BASH)
			need = 1;
		if (u.ch.lv >= need || boss) CharGiveSkill(u.ch, sk);
	}
	if (u.ch.lv >= 30) CharGiveSkill(u.ch, SKL_P_IRON);
	if (u.ch.lv >= 45) CharGiveSkill(u.ch, SKL_P_CRIT);
	u.hp = u.ch.stat[S_HP];
	u.mp = u.ch.stat[S_MP];
	u.cp = 0;
	u.span = MonsterSpan(md, boss);
	{
		int line = monster % 6;
		if (monster >= 0 && monster < M_COUNT) {
			static const int kLine[M_COUNT] = { 5, 0, 1, 2, 3, 4, 5, 1, 0, 0, 0 };
			line = kLine[monster];
		} else line = MonsterDropLine(monster);
		if (line < 0 || line >= CRAFT_LINES) line = 0;
		int a = (monster * 3) % CRAFT_NORM;
		int b = (monster * 5 + 1) % CRAFT_NORM;
		if (b == a) b = (b + 3) % CRAFT_NORM;
		int cslot = (a + 7) % CRAFT_NORM;
		if (cslot == a || cslot == b) cslot = (cslot + 1) % CRAFT_NORM;
		u.craftEq[0] = CRAFT_BASE + line * CRAFT_PER_LINE + a;
		u.craftEq[1] = CRAFT_BASE + line * CRAFT_PER_LINE + b;
		u.craftEq[2] = CRAFT_BASE + line * CRAFT_PER_LINE + cslot;
		u.craftEq[3] = 0;
		if (boss || u.ch.lv >= 18) {
			int sk = boss ? 5 : (u.ch.lv >= 30 ? 2 : 0);
			u.craftEq[3] = CRAFT_BASE + line * CRAFT_PER_LINE + CRAFT_NORM + sk;
		}
	}
}

static int StageEnemyLevel(int stage);
void MonsterPreview(int id, int* lv, int* stat)
{
	if (lv) *lv = 1;
	if (stat) for (int s = 0; s < S_COUNT; s++) stat[s] = 0;
	if (id < 0) return;
	int stage = MonsterFirstStage(id);
	int level = StageEnemyLevel(stage);
	Rng r(0xA11CEu ^ (unsigned)id * 17u);
	Unit u;
	MakeEnemy(u, id, level, 0, r);
	if (lv) *lv = u.ch.lv;
	if (stat) for (int s = 0; s < S_COUNT; s++) stat[s] = u.ch.stat[s];
}

void MonsterHeldText(int id, wchar_t* out, int cap)
{
	if (!out || cap < 2) return;
	out[0] = 0;
	if (id < 0) return;
	int stage = MonsterFirstStage(id);
	int level = StageEnemyLevel(stage);
	Rng r(0xA11CEu ^ (unsigned)id * 17u);
	Unit u;
	MakeEnemy(u, id, level, 0, r);
	UnitHeldText(u, out, cap);
}

// ============================================================ 経路
int Dist(int ax, int az, int bx, int bz) { return abs(ax - bx) + abs(az - bz); }

bool CanStep(const Map& m, int fx, int fz, int tx, int tz, int moveType, int jump)
{
	if (!Map::In(tx, tz)) return false;
	const Cell& t = m.At(tx, tz);
	const Cell& f = m.At(fx, fz);
	if (moveType == MV_FLY) return TerrainInfo(t.t).flyOk != 0;
	if (TerrainInfo(t.t).costFoot >= 99) {
		// 大きな岩の中にいるユニットは、岩の上を渡って外へ出られる。外からは入れない。
		if ((f.t == T_MOUNTAIN || f.t == T_ROCK) && (t.t == T_MOUNTAIN || t.t == T_ROCK))
			return abs((int)t.h - (int)f.h) <= jump;
		return false;
	}
	// 溶岩と水は周囲より低い。立ったまま段差を許さないと、向こうのこぶへ出られない。
	if (f.t == T_LAVA || t.t == T_LAVA || f.t == T_WATER || t.t == T_WATER) return true;
	return abs((int)t.h - (int)f.h) <= jump;
}

static int EnterCost(const Map& m, int x, int z, int moveType)
{
	if (moveType == MV_FLY) return 2;
	const Cell& c = m.At(x, z);
	int cost = TerrainInfo(c.t).costFoot;
	// 砂漠・雪原の地面は歩ける。柔らかい吹きだまりだけ足を取る。
	if ((c.t == T_SAND || c.t == T_SNOW) && (c.var % 5) == 0) cost = 3;
	return cost;
}

// 1回の動きで変われる高さ。歩行は2、飛行は4。
// 層1は地面から+2を3段、層2はその先でさらに+2を3段。±1あっても、一段で層の上には飛ばない。
static int StepRise(int moveType)
{
	return moveType == MV_FLY ? 4 : 2;
}

static int LayerOpen(const Cell& to, int L, int moveType)
{
	if (!LayerOn(to, L)) return 0;
	int tr = LayerTr(to, L);
	if (moveType == MV_FLY) return TerrainInfo(tr).flyOk != 0;
	return TerrainInfo(tr).costFoot < 99;
}

static int LayerLegal(const Cell& from, int fromLayer, const Cell& to, int L, int moveType)
{
	if (!LayerOpen(to, L, moveType)) return 0;
	int dl = L - fromLayer;
	if (dl < 0) dl = -dl;
	// 直上・直下の層は、高さの段差があっても渡れる。同じ層の崖は段差で止める。
	if (dl == 1) return 1;
	int fromL = LayerOn(from, fromLayer) ? fromLayer : 0;
	int fromTr = LayerTr(from, fromL);
	int toTr = LayerTr(to, L);
	if (fromTr == T_LAVA || toTr == T_LAVA || fromTr == T_WATER || toTr == T_WATER) return 1;
	if (abs(LayerHt(to, L) - LayerHt(from, fromL)) > StepRise(moveType)) return 0;
	return 1;
}

static int ChooseLayer(const Cell& from, int fromLayer, const Cell& to, int jump, int moveType)
{
	(void)jump;
	int n = CellLayN(to);
	int best = -1, bestScore = 9999;
	int fromL = LayerOn(from, fromLayer) ? fromLayer : 0;
	int fromH = LayerHt(from, fromL);
	for (int L = 0; L < n; L++) {
		if (!LayerLegal(from, fromL, to, L, moveType)) continue;
		int dh = abs(LayerHt(to, L) - fromH);
		int score = dh * 10 + (L == fromLayer ? 0 : 3);
		if (score < bestScore) { bestScore = score; best = L; }
	}
	return best;
}

// いまいる高さから、隣の各層へ届くかを見る。近い層だけに潰さない。
static bool FootLand(const Battle& b, int ui, int x, int z, int nx, int nz, int fromLayer, int toLayer, int* climbsOut)
{
	const Unit& u = b.u[ui];
	int s = UnitSpan(u);
	int mt = UnitMoveType(u);
	for (int iz = 0; iz < s; iz++) for (int ix = 0; ix < s; ix++) {
		int sx = x + ix, sz = z + iz, tx = nx + ix, tz = nz + iz;
		if (!Map::In(sx, sz) || !Map::In(tx, tz)) return false;
		const Cell& f = b.map.At(sx, sz);
		const Cell& t = b.map.At(tx, tz);
		int fromL = LayerOn(f, fromLayer) ? fromLayer : 0;
		if (!LayerLegal(f, fromL, t, toLayer, mt)) return false;
		for (int L = 0; L < LAY_N; L++) {
			int o = b.UnitAt(tx, tz, L);
			if (o < 0 || o == ui || !b.u[o].alive) continue;
			if (b.u[o].team != u.team) return false;
			if (s > 1 && L == toLayer) return false;
		}
	}
	if (climbsOut) *climbsOut = 1;
	return true;
}

// 原点を (x,z) から (nx,nz) へ 1 マス動かす。同じ層にいる相手だけが道を塞ぐ。
static bool FootStep(const Battle& b, int ui, int x, int z, int nx, int nz, int fromLayer, int* toLayer)
{
	const Unit& u = b.u[ui];
	int s = UnitSpan(u);
	int mt = UnitMoveType(u), jp = UnitJump(u);
	int layer = 0;
	for (int iz = 0; iz < s; iz++) for (int ix = 0; ix < s; ix++) {
		int sx = x + ix, sz = z + iz, tx = nx + ix, tz = nz + iz;
		if (!Map::In(sx, sz) || !Map::In(tx, tz)) return false;
		const Cell& f = b.map.At(sx, sz);
		const Cell& t = b.map.At(tx, tz);
		int fromL = LayerOn(f, fromLayer) ? fromLayer : 0;
		int lay = ChooseLayer(f, fromL, t, jp, mt);
		if (lay < 0) return false;
		if (ix == 0 && iz == 0) layer = lay;
		else if (lay != layer) return false;
		for (int L = 0; L < LAY_N; L++) {
			int o = b.UnitAt(tx, tz, L);
			if (o < 0 || o == ui || !b.u[o].alive) continue;
			if (b.u[o].team != u.team) return false;
			if (s > 1 && L == lay) return false;
		}
	}
	if (toLayer) *toLayer = layer;
	return true;
}

static int FootCost(const Battle& b, int ui, int x, int z, int layer = 0)
{
	const Unit& u = b.u[ui];
	int s = UnitSpan(u);
	int mt = UnitMoveType(u);
	int c = 0;
	for (int iz = 0; iz < s; iz++) for (int ix = 0; ix < s; ix++) {
		if (!Map::In(x + ix, z + iz)) return 99;
		const Cell& cell = b.map.At(x + ix, z + iz);
		int e;
		if (mt == MV_FLY) e = 2;
		else {
			int t = LayerTr(cell, LayerOn(cell, layer) ? layer : 0);
			e = TerrainInfo(t).costFoot;
			if ((t == T_SAND || t == T_SNOW) && (cell.var % 5) == 0) e = 3;
		}
		if (e > c) c = e;
	}
	return c;
}

static bool FootCanStop(const Battle& b, int ui, int x, int z, int layer)
{
	const Unit& u = b.u[ui];
	int s = UnitSpan(u);
	for (int iz = 0; iz < s; iz++) for (int ix = 0; ix < s; ix++) {
		if (!Map::In(x + ix, z + iz)) return false;
		int o = b.UnitAt(x + ix, z + iz, layer);
		if (o >= 0 && o != ui) return false;
	}
	return true;
}

static bool FootTerrain(const Battle& b, int ui, int x, int z, int nx, int nz)
{
	const Unit& u = b.u[ui];
	int s = UnitSpan(u);
	int mt = UnitMoveType(u), jp = UnitJump(u);
	if (s <= 1) return CanStep(b.map, x, z, nx, nz, mt, jp);
	for (int iz = 0; iz < s; iz++) for (int ix = 0; ix < s; ix++) {
		if (!Map::In(x + ix, z + iz)) return false;
		if (!CanStep(b.map, x + ix, z + iz, nx + ix, nz + iz, mt, jp)) return false;
	}
	return true;
}

static short& ReachCostRef(Reach& r, int layer, int x, int z)
{
	if (layer == 1) return r.costHi[z][x];
	if (layer == 2) return r.cost2[z][x];
	if (layer == 3) return r.cost3[z][x];
	if (layer == 4) return r.cost4[z][x];
	return r.cost[z][x];
}
static int& ReachFromRef(Reach& r, int layer, int x, int z)
{
	if (layer == 1) return r.fromHi[z][x];
	if (layer == 2) return r.from2[z][x];
	if (layer == 3) return r.from3[z][x];
	if (layer == 4) return r.from4[z][x];
	return r.from[z][x];
}
static int ReachFromAt(const Reach& r, int layer, int x, int z)
{
	if (layer == 1) return r.fromHi[z][x];
	if (layer == 2) return r.from2[z][x];
	if (layer == 3) return r.from3[z][x];
	if (layer == 4) return r.from4[z][x];
	return r.from[z][x];
}

void ComputeReach(const Battle& b, int ui, Reach& out)
{
	const Unit& u = b.u[ui];
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		out.cost[z][x] = -1; out.from[z][x] = -1; out.canStop[z][x] = 0;
		out.costHi[z][x] = -1; out.fromHi[z][x] = -1; out.canStopHi[z][x] = 0;
		out.cost2[z][x] = -1; out.from2[z][x] = -1; out.canStop2[z][x] = 0;
		out.cost3[z][x] = -1; out.from3[z][x] = -1; out.canStop3[z][x] = 0;
		out.cost4[z][x] = -1; out.from4[z][x] = -1; out.canStop4[z][x] = 0;
	}
	const int mv = UnitMove(u) * 2;
	static int qx[MAPN * MAPN * LAY_N], qz[MAPN * MAPN * LAY_N], qd[MAPN * MAPN * LAY_N];
	int qn = 0;
	int startL = u.deck;
	if (startL < 0 || startL >= LAY_N || !LayerOn(b.map.At(u.x, u.z), startL)) startL = 0;
	ReachCostRef(out, startL, u.x, u.z) = 0;
	qx[qn] = u.x; qz[qn] = u.z; qd[qn] = startL; qn++;
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	while (qn > 0) {
		int bi = 0;
		for (int i = 1; i < qn; i++) {
			int ci = ReachCostAt(out, qd[i], qx[i], qz[i]);
			int cb = ReachCostAt(out, qd[bi], qx[bi], qz[bi]);
			if (ci < cb) bi = i;
		}
		int x = qx[bi], z = qz[bi], layer = qd[bi];
		qx[bi] = qx[qn - 1]; qz[bi] = qz[qn - 1]; qd[bi] = qd[qn - 1]; qn--;
		int c0 = ReachCostAt(out, layer, x, z);
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz)) continue;
			for (int toL = 0; toL < LAY_N; toL++) {
				if (!FootLand(b, ui, x, z, nx, nz, layer, toL, 0)) continue;
				int nc = c0 + FootCost(b, ui, nx, nz, toL);
				if (nc > mv) continue;
				short& cst = ReachCostRef(out, toL, nx, nz);
				if (cst >= 0 && cst <= nc) continue;
				int inq = cst >= 0;
				cst = (short)nc;
				ReachFromRef(out, toL, nx, nz) = PackCell(x, z, layer);
				if (qn + 1 >= MAPN * MAPN * LAY_N) continue;
				if (!inq) { qx[qn] = nx; qz[qn] = nz; qd[qn] = toL; qn++; }
				else {
					int found = 0;
					for (int i = 0; i < qn; i++) if (qx[i] == nx && qz[i] == nz && qd[i] == toL) found = 1;
					if (!found) { qx[qn] = nx; qz[qn] = nz; qd[qn] = toL; qn++; }
				}
			}
		}
	}
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (out.cost[z][x] >= 0) out.canStop[z][x] = FootCanStop(b, ui, x, z, 0) ? 1 : 0;
		if (out.costHi[z][x] >= 0) out.canStopHi[z][x] = FootCanStop(b, ui, x, z, 1) ? 1 : 0;
		if (out.cost2[z][x] >= 0) out.canStop2[z][x] = FootCanStop(b, ui, x, z, 2) ? 1 : 0;
		if (out.cost3[z][x] >= 0) out.canStop3[z][x] = FootCanStop(b, ui, x, z, 3) ? 1 : 0;
		if (out.cost4[z][x] >= 0) out.canStop4[z][x] = FootCanStop(b, ui, x, z, 4) ? 1 : 0;
	}
}

int ReachPath(const Reach& r, int sx, int sz, int tx, int tz, int* outXZ, int maxN, int preferLayer)
{
	if (!Map::In(tx, tz)) return 0;
	int on = -1;
	if (preferLayer >= 0 && preferLayer < LAY_N && ReachCostAt(r, preferLayer, tx, tz) >= 0) on = preferLayer;
	else {
		int best = 99999;
		for (int L = 0; L < LAY_N; L++) {
			int c = ReachCostAt(r, L, tx, tz);
			if (c >= 0 && c < best) { best = c; on = L; }
		}
	}
	if (on < 0) return 0;
	int tmp[MAPN * 2], n = 0;
	int x = tx, z = tz;
	while (n < (int)_countof(tmp)) {
		tmp[n++] = PackCell(x, z, on);
		if (x == sx && z == sz) break;
		int p = ReachFromAt(r, on, x, z);
		if (p < 0) break;
		on = PackDeck(p);
		x = PackX(p); z = PackZ(p);
	}
	int out = 0;
	for (int i = n - 1; i >= 0 && out < maxN; i--) outXZ[out++] = tmp[i];
	return out;
}

static bool SightTerrain(int t)
{
	return t == T_MOUNTAIN || t == T_ROCK || t == T_DOOR || t == T_VOID;
}

static bool SightCellBlocks(const Map& m, int x, int z, float y)
{
	if (!Map::In(x, z)) return true;
	const Cell& c = m.At(x, z);
	if (SightTerrain(c.t)) return true;
	if (m.TopY(x, z) > y + .35f) return true;
	int nL = CellLayN(c);
	for (int L = 1; L < nL; L++) {
		float slab = LayerHt(c, L) * kHStep;
		if (y > slab - 0.2f && y < slab + 0.05f) return true;
	}
	return false;
}

static bool LineOfSightY(const Map& m, int ax, int az, float ya, int bx, int bz, float yb)
{
	int dx = bx - ax, dz = bz - az;
	if (dx == 0 && dz == 0) return true;
	int adx = abs(dx), adz = abs(dz);
	int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
	int sz = dz > 0 ? 1 : (dz < 0 ? -1 : 0);
	int n = adx + adz;
	if (n < 1) n = 1;
	int x = ax, z = az, err = adx - adz;
	int guard = n + 2;
	while (guard-- > 0) {
		int e2 = err * 2;
		int px = x, pz = z;
		if (e2 > -adz) { err -= adz; x += sx; }
		if (e2 < adx) { err += adx; z += sz; }
		if (x == bx && z == bz) return true;
		float y = Lerpf(ya, yb, (float)(abs(x - ax) + abs(z - az)) / (float)n);
		if (x != px && z != pz) {
			if (SightCellBlocks(m, px, z, y) && SightCellBlocks(m, x, pz, y)) return false;
		}
		if (SightCellBlocks(m, x, z, y)) return false;
	}
	return false;
}

bool LineOfSight(const Map& m, int ax, int az, int bx, int bz)
{
	return LineOfSightY(m, ax, az, m.StandY(ax, az) + .6f, bx, bz, m.StandY(bx, bz) + .6f);
}

bool InAttackRange(const Battle& b, int ax, int az, int rMin, int rMax, int tx, int tz, bool needLos)
{
	int d = Dist(ax, az, tx, tz);
	if (d < rMin || d > rMax) return false;
	if (d == 1) {
		int dh = abs((int)b.map.At(ax, az).h - (int)b.map.At(tx, tz).h);
		const int ta = b.map.At(ax, az).t, tb = b.map.At(tx, tz).t;
		if (ta == T_CHASM || tb == T_CHASM || ta == T_WATER || tb == T_WATER || ta == T_LAVA || tb == T_LAVA) return true;
		return dh <= 4;
	}
	return !needLos || LineOfSight(b.map, ax, az, tx, tz);
}

bool AttackReachLayer(const Battle& b, int ax, int az, int fromLayer, int rMin, int rMax, int tx, int tz, int toLayer, bool needLos)
{
	if (!Map::In(ax, az) || !Map::In(tx, tz)) return false;
	if (toLayer < 0 || toLayer >= LAY_N) return false;
	const Cell& tc = b.map.At(tx, tz);
	if (!LayerOn(tc, toLayer)) return false;
	if (SightTerrain(LayerTr(tc, toLayer))) return false;
	if (fromLayer < 0 || fromLayer >= LAY_N || !LayerOn(b.map.At(ax, az), fromLayer)) fromLayer = 0;
	int add = LayerRangeCost(fromLayer, toLayer);
	if (add < 0) return false;
	int d = Dist(ax, az, tx, tz) + add;
	if (d < rMin || d > rMax) return false;
	if (!needLos) return true;
	int ah = LayerHt(b.map.At(ax, az), fromLayer);
	int th = LayerHt(tc, toLayer);
	return LineOfSightY(b.map, ax, az, ah * kHStep + 0.6f, tx, tz, th * kHStep + 0.6f);
}

static int FootDist(int ax, int az, int as, int bx, int bz, int bs)
{
	if (as < 1) as = 1;
	if (bs < 1) bs = 1;
	int best = 9999;
	for (int az1 = 0; az1 < as; az1++) for (int ax1 = 0; ax1 < as; ax1++)
		for (int bz1 = 0; bz1 < bs; bz1++) for (int bx1 = 0; bx1 < bs; bx1++) {
			int d = abs((ax + ax1) - (bx + bx1)) + abs((az + az1) - (bz + bz1));
			if (d < best) best = d;
		}
	return best;
}

bool InRangeOfUnit(const Battle& b, const Unit& a, int ax, int az, int rMin, int rMax, const Unit& t, bool needLos)
{
	int sa = UnitSpan(a), st = UnitSpan(t);
	int aL = LayerOn(b.map.At(a.x, a.z), a.deck) ? a.deck : 0;
	int tL = LayerOn(b.map.At(t.x, t.z), t.deck) ? t.deck : 0;
	int layerAdd = LayerRangeCost(aL, tL);
	if (layerAdd < 0) return false;
	int ah = LayerHt(b.map.At(a.x, a.z), aL);
	int tht = LayerHt(b.map.At(t.x, t.z), tL);
	for (int az1 = 0; az1 < sa; az1++) for (int ax1 = 0; ax1 < sa; ax1++)
		for (int tz = 0; tz < st; tz++) for (int tx = 0; tx < st; tx++) {
			int x = ax + ax1, z = az + az1, cx = t.x + tx, cz = t.z + tz;
			int d = Dist(x, z, cx, cz) + layerAdd;
			if (d < rMin || d > rMax) continue;
			if (needLos && !LineOfSightY(b.map, x, z, ah * kHStep + .6f, cx, cz, tht * kHStep + .6f)) continue;
			return true;
		}
	return false;
}

static bool AimFoot(const Battle& b, const Unit& a, int ax, int az, int rMin, int rMax, const Unit& t, bool needLos, int& ox, int& oz)
{
	int sa = UnitSpan(a), st = UnitSpan(t);
	int best = 9999;
	bool ok = false;
	for (int az1 = 0; az1 < sa; az1++) for (int ax1 = 0; ax1 < sa; ax1++)
		for (int tz = 0; tz < st; tz++) for (int tx = 0; tx < st; tx++) {
			int x = ax + ax1, z = az + az1, cx = t.x + tx, cz = t.z + tz;
			if (!InAttackRange(b, x, z, rMin, rMax, cx, cz, needLos)) continue;
			int d = Dist(x, z, cx, cz);
			if (d < best) { best = d; ox = cx; oz = cz; ok = true; }
		}
	return ok;
}

static bool UnitOnShape(const SkillDef& s, const Unit& c, int cx, int cz, const Unit& t)
{
	int sp = UnitSpan(t);
	for (int dz = 0; dz < sp; dz++) for (int dx = 0; dx < sp; dx++)
		if (ShapeCell(s.shape, s.area, c.x, c.z, cx, cz, t.x + dx, t.z + dz)) return true;
	return false;
}

static bool FootClear(const Battle& b, const Unit& t)
{
	int s = UnitSpan(t);
	for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++)
		if (b.UnitAt(t.x + dx, t.z + dz) >= 0) return false;
	return true;
}

// 味方出撃と敵拠点の中間より敵拠点側。配置が取れない地図では制限しない。
static bool EnemyHalf(const Map& m, int x, int z)
{
	int np = m.nP < 15 ? m.nP : 15;
	int ne = m.nE < 10 ? m.nE : 10;
	if (np < 1 || ne < 1) return false;
	double px = 0, pz = 0, ex = 0, ez = 0;
	for (int i = 0; i < np; i++) { px += m.pStartX[i]; pz += m.pStartZ[i]; }
	for (int i = 0; i < ne; i++) { ex += m.eStartX[i]; ez += m.eStartZ[i]; }
	px /= np; pz /= np; ex /= ne; ez /= ne;
	double dx = ex - px, dz = ez - pz;
	if (dx * dx + dz * dz < 1.0) return false;
	double mx = (px + ex) * 0.5, mz = (pz + ez) * 0.5;
	return (x + 0.5 - mx) * dx + (z + 0.5 - mz) * dz > 0.0;
}

static int AllySideScore(const Map& m, int x, int z)
{
	int np = m.nP < 15 ? m.nP : 15;
	int ne = m.nE < 10 ? m.nE : 10;
	if (np < 1 || ne < 1) return 0;
	double px = 0, pz = 0, ex = 0, ez = 0;
	for (int i = 0; i < np; i++) { px += m.pStartX[i]; pz += m.pStartZ[i]; }
	for (int i = 0; i < ne; i++) { ex += m.eStartX[i]; ez += m.eStartZ[i]; }
	px /= np; pz /= np; ex /= ne; ez /= ne;
	double dx = ex - px, dz = ez - pz;
	return (int)((x + 0.5 - px) * dx + (z + 0.5 - pz) * dz);
}

static bool RaiseFootOpen(const Battle& b, const Unit& who, int x, int z)
{
	int s = UnitSpan(who);
	int mv = UnitMoveType(who);
	int jump = UnitJump(who);
	for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++) {
		int tx = x + dx, tz = z + dz;
		if (!Map::In(tx, tz)) return false;
		if (b.UnitAt(tx, tz) >= 0) return false;
		if (EnemyHalf(b.map, tx, tz)) return false;
		if (!CanStep(b.map, who.x, who.z, tx, tz, mv, jump)) return false;
	}
	return true;
}

bool FindRaiseTile(const Battle& b, const Unit& corpse, int& ox, int& oz)
{
	int bestD = 9999, bestSide = 0x7fffffff;
	int bx = 0, bz = 0;
	bool found = false;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		int d = Dist(corpse.x, corpse.z, x, z);
		if (d < 1 || d > bestD) continue;
		if (!RaiseFootOpen(b, corpse, x, z)) continue;
		int side = AllySideScore(b.map, x, z);
		if (!found || d < bestD || (d == bestD && side < bestSide)) {
			found = true;
			bestD = d;
			bestSide = side;
			bx = x;
			bz = z;
		}
	}
	if (!found) return false;
	ox = bx;
	oz = bz;
	return true;
}

static bool RaiseHereOk(const Battle& b, const Unit& corpse)
{
	if (!FootClear(b, corpse)) return false;
	return true;
}

bool RaisePlaceable(const Battle& b, const Unit& corpse)
{
	if (RaiseHereOk(b, corpse)) return true;
	int x, z;
	return FindRaiseTile(b, corpse, x, z);
}

bool SettleRaised(Battle& b, Unit& d)
{
	if (RaiseHereOk(b, d)) return true;
	int x, z;
	if (!FindRaiseTile(b, d, x, z)) return false;
	d.x = x;
	d.z = z;
	if (d.deck < 0 || d.deck >= LAY_N || !LayerOn(b.map.At(x, z), d.deck)) d.deck = 0;
	int s = UnitSpan(d);
	d.px = (float)d.x + s * 0.5f;
	d.pz = (float)d.z + s * 0.5f;
	d.py = b.map.WalkY(d.x, d.z, d.deck);
	return true;
}

// ============================================================ 戦闘
enum { WT_NONE = 0, WT_SWORD, WT_LANCE, WT_AXE, WT_BOW };
static int WeaponOf(const Unit& u);
static float HeightMod(const Battle& b, const Unit& a, const Unit& d)
{
	float dh = (b.map.StandY(a.x, a.z) - b.map.StandY(d.x, d.z)) / kHStep;
	bool bow = WeaponOf(a) == WT_BOW || UnitAttackStyle(a) == AS_SHOOT || UnitAttackStyle(a) == AS_SHOOT1;
	float per = bow ? .12f : .06f;
	float lo = bow ? -.28f : -.16f;
	float hi = bow ? .48f : .24f;
	return 1.f + Clampf(dh * per, lo, hi);
}

static int WeaponOf(const Unit& u)
{
	if (UnitMagAttack(u) && UnitAttackStyle(u) == AS_CAST) return WT_NONE;
	int w = IsMon(u) ? MonsterInfo(u.monster).weaponLook : ClassInfo(u.ch.cls).weaponLook;
	switch (w) {
	case WL_SWORD_SHIELD: case WL_SWORD_BADGE: case WL_SWORD_BOOK: case WL_SWORD2: return WT_SWORD;
	case WL_SWORD_SPIKE: return WT_LANCE;
	case WL_AXE2: case WL_AXES: return WT_AXE;
	case WL_CROSSBOW2: case WL_CROSS_KNIFE: return WT_BOW;
	default: return WT_NONE;
	}
}

static int TriRel(int aw, int dw)
{
	if (aw == WT_NONE || dw == WT_NONE || aw == WT_BOW || dw == WT_BOW) return 0;
	if ((aw == WT_SWORD && dw == WT_AXE) || (aw == WT_AXE && dw == WT_LANCE) || (aw == WT_LANCE && dw == WT_SWORD)) return 1;
	if ((aw == WT_AXE && dw == WT_SWORD) || (aw == WT_LANCE && dw == WT_AXE) || (aw == WT_SWORD && dw == WT_LANCE)) return -1;
	return 0;
}

static int FacingOf(const Unit& a, const Unit& d)
{
	float ax = a.x + (UnitSpan(a) - 1) * 0.5f, az = a.z + (UnitSpan(a) - 1) * 0.5f;
	float dx = d.x + (UnitSpan(d) - 1) * 0.5f, dz = d.z + (UnitSpan(d) - 1) * 0.5f;
	float ang = atan2f(ax - dx, az - dz);
	float diff = fabsf(WrapAngle(ang - d.face));
	if (diff > 2.3f) return 2;
	if (diff > 1.2f) return 1;
	return 0;
}

int UnitFacing(const Unit& attacker, const Unit& defender)
{
	return FacingOf(attacker, defender);
}

static float DirMod(const Unit& a, const Unit& d)
{
	int f = FacingOf(a, d);
	if (f == 2) return 1.25f;
	if (f == 1) return 1.1f;
	return 0.9f;
}

static int TerrainDefPct(const Battle& b, const Unit& d)
{
	if (UnitMoveType(d) == MV_FLY) return 0;
	return TerrainInfo(b.map.At(d.x, d.z).t).defPct;
}
static int TerrainAvoPct(const Battle& b, const Unit& d)
{
	if (UnitMoveType(d) == MV_FLY) return 5;
	return TerrainInfo(b.map.At(d.x, d.z).t).avoPct;
}

// 味方だけ。Lv1 は 1。近接は通常攻撃が届き、会心でも雑魚HPを丸ごと消さない厚みと組む。
static float TacAllyScale(const Unit& a, float perLv)
{
	if (a.team != TEAM_PLAYER) return 1.f;
	int lv = a.ch.lv;
	if (lv < 2) return 1.f;
	return 1.f + (lv - 1) * perLv;
}

// 標準攻撃の素点。職の力・魔力は幅があるので、ここから防御を引いた着弾を揃える。敵には使わない。
static float TacBasicOffense(const Unit& a)
{
	if (a.team != TEAM_PLAYER) return -1.f;
	int lv = a.ch.lv;
	if (lv < 1) lv = 1;
	return 8.f + (lv - 1) * 5.35f;
}

static void BlendBasic(const Unit& a, float& dm, float mit)
{
	float off = TacBasicOffense(a);
	if (off < 0.f) return;
	float fair = off - mit;
	if (fair < 1.f) fair = 1.f;
	dm = fair * 0.8f + dm * 0.2f;
}

static int AttackElem(const Unit& a, int skill)
{
	const SkillDef* sd = skill >= 0 ? &SkillInfo(skill) : NULL;
	int el = sd ? sd->element : EL_NONE;
	if (el != EL_NONE) return el;
	const GearDef* w = GearOf(a.ch.reserved[6]);
	if (w && w->kind == 0 && w->elem >= EL_FIRE && w->elem <= EL_POISON) return w->elem;
	return EL_NONE;
}

static int WeakHitsOf(const Unit& a, const Unit& d, int skill)
{
	if (d.team != TEAM_ENEMY || &a == &d) return 0;
	const SkillDef* sd = skill >= 0 ? &SkillInfo(skill) : NULL;
	int magic = sd ? (sd->kind == SK_MAGIC) : (UnitMagAttack(a) != 0);
	int el = AttackElem(a, skill);
	int ranged;
	if (magic) ranged = 1;
	else if (sd && sd->rMax >= 0) ranged = (sd->rMin >= 2 || sd->rMax >= 2) ? 1 : 0;
	else ranged = (UnitRangeMax(a) >= 2 || WeaponOf(a) == WT_BOW || UnitAttackStyle(a) == AS_SHOOT || UnitAttackStyle(a) == AS_SHOOT1) ? 1 : 0;
	const MonsterDef& md = MonsterInfo(d.monster);
	int n = 0;
	for (int i = 0; i < 4; i++) {
		int w = md.weak[i];
		if (w == EL_NONE) continue;
		int dup = 0;
		for (int j = 0; j < i; j++) if (md.weak[j] == w) dup = 1;
		if (dup) continue;
		int hit = 0;
		if (w >= EL_FIRE && w <= EL_POISON && w == el) hit = 1;
		else if (w == EL_MAGIC && magic) hit = 1;
		else if (w == EL_PHYS && !magic) hit = 1;
		else if (w == EL_RANGE && ranged) hit = 1;
		else if (w == EL_MELEE && !ranged) hit = 1;
		if (hit) n++;
	}
	if (sd) {
		for (int i = 0; i < 2; i++) if (md.ailWeak[i] && md.ailWeak[i] == sd->effect) { n++; break; }
	}
	return n;
}

static float FormAllyMul(const Battle& b, const Unit& a, int skill, int tx, int tz);
static void StrikeCalc(const Battle& b, const Unit& a, const Unit& d, int skill, int& dmg, int& hit, int& crit)
{
	const SkillDef* sd = skill >= 0 ? &SkillInfo(skill) : NULL;
	bool magic = sd ? (sd->kind == SK_MAGIC) : (UnitMagAttack(a) != 0);
	int tdef = TerrainDefPct(b, d), tavo = TerrainAvoPct(b, d);
	float dm;
	if (magic) {
		int atk = UnitStat(a, S_MAG) + (sd ? 0 : UnitWeaponPow(a));
		if (UnitHasSkill(a, SKL_P_SPELL)) atk = atk * 115 / 100;
		float mult = sd ? (150 + sd->power * 5) / 100.f : 1.f;
		if (sd && IsFormSkill(skill)) mult *= FormAllyMul(b, a, skill, sd->rMax == 0 ? a.x : d.x, sd->rMax == 0 ? a.z : d.z);
		float df = UnitStat(d, S_RES) * (1.f + tdef * .005f);
		float atkF = (float)atk;
		if (sd && !IsSummonId(skill)) atkF *= TacAllyScale(a, 0.018f);
		dm = atkF * mult - df * .8f;
		if (!sd) BlendBasic(a, dm, df * .8f);
		else if (a.team == TEAM_PLAYER && !IsSummonId(skill)) {
			float off = TacBasicOffense(a);
			if (off > 0.f) {
				float fair = off - df * .8f;
				if (fair < 1.f) fair = 1.f;
				float floorv = fair * 0.8f + dm * 0.2f;
				if (dm < floorv) dm = floorv;
			}
		}
		if (sd && IsSummonId(skill)) {
			// 魔力−魔防だけだと個位数になる。力＋武器と魔力の高い方を使い、見積もりの半分をさらに 1.5 倍する。
			int phys = UnitStat(a, S_STR) + UnitWeaponPow(a);
			float offense = (float)(phys > atk ? phys : atk);
			float pdef = UnitStat(d, S_DEF) * (1.f + tdef * .01f);
			float mit = pdef < df ? pdef : df;
			float want = offense * (2.1f + sd->power * 0.02f) - mit * 0.2f;
			float floorv = offense * 1.6f;
			if (want < floorv) want = floorv;
			if (dm < want) dm = want;
			dm *= 0.5f * 1.5f;
		} else if (sd && sd->hits > 1) dm /= (float)sd->hits;
		hit = 96 + UnitStat(a, S_DEX) / 2 - UnitStat(d, S_AGI) / 3 - tavo / 3 + (sd ? 6 : 0);
		if (a.blind > 0) hit -= 28;
		crit = 0;
	} else {
		int atk = UnitStat(a, S_STR) + UnitWeaponPow(a);
		if (sd && skill == SKL_HOLYBLADE) atk += UnitStat(a, S_MAG) / 2;
		if (UnitHasSkill(a, SKL_P_MIGHT)) atk = atk * 115 / 100;
		bool bowish = WeaponOf(a) == WT_BOW || UnitAttackStyle(a) == AS_SHOOT || UnitAttackStyle(a) == AS_SHOOT1
			|| skill == SKL_PIERCE || skill == SKL_SNIPE || skill == SKL_ARROWRAIN
			|| skill == SKL_ARROWLINE || skill == SKL_ARROWFAN || skill == SKL_CRESCENT;
		if (bowish) atk += UnitStat(a, S_DEX);
		if (!sd && bowish) atk = atk * 6 / 5;
		float mult = sd ? sd->power / 100.f : 1.f;
		// 弓スキルは防御を大きく抜くので、倍率は抑えめ。会心で敵HPを丸ごと消さない。
		if (bowish && sd) mult *= 1.10f;
		// 弓以外の物理。防御を引くと力100では数十ダメージになるので、レベルで戻す。
		if (!bowish) mult *= TacAllyScale(a, 0.034f);
		if (sd && IsFormSkill(skill)) mult *= FormAllyMul(b, a, skill, sd->rMax == 0 ? a.x : d.x, sd->rMax == 0 ? a.z : d.z);
		float df = UnitStat(d, S_DEF) * (1.f + tdef * .01f);
		if (bowish) df *= 0.42f; // 暗黒戦士級の高DEFでも2〜3ダメに潰れない
		if (sd && sd->effect == EF_IGNDEF) df *= .5f;
		{
			int pen = UnitStat(a, S_PEN);
			if (pen > 40) pen = 40;
			if (pen > 0) df *= (100 - pen) / 100.f;
		}
		dm = atk * mult - df;
		if (bowish) {
			int floorv = max(18, (UnitStat(a, S_STR) + UnitStat(a, S_DEX) * 2) / 3);
			if (dm < (float)floorv) dm = (float)floorv;
		}
		if (!sd) BlendBasic(a, dm, df);
		else if (a.team == TEAM_PLAYER && sd->power >= 100) {
			// 100%以上の技は、同じ防御を引いた標準攻撃を下回らない。上回る職はそのまま。
			float off = TacBasicOffense(a);
			if (off > 0.f) {
				float fair = off - df;
				if (fair < 1.f) fair = 1.f;
				float floorv = fair * 0.8f + dm * 0.2f;
				if (dm < floorv) dm = floorv;
			}
		}
		int evade = UnitStat(d, S_AGI) / 2 + UnitStat(d, S_LUK) / 4 + tavo / 2 + (UnitHasSkill(d, SKL_P_EVADE) ? 8 : 0) + (UnitHasPas(d, PAS_EVADE) ? 6 : 0) + (UnitHasPas(d, PAS_EVADE2) ? 16 : 0);
		hit = 90 + UnitStat(a, S_DEX) + UnitStat(a, S_LUK) / 4 - evade + (sd ? 6 : 0);
		if (a.blind > 0) hit -= 28;
		crit = UnitStat(a, S_DEX) / 2 + UnitStat(a, S_LUK) / 3 + UnitStat(a, S_CRT) - UnitStat(d, S_LUK) / 3 + (sd ? sd->critBonus : 0)
			+ (UnitHasSkill(a, SKL_P_CRIT) ? 10 : 0) + (UnitHasPas(a, PAS_CRIT) ? 10 : 0) + (UnitHasPas(a, PAS_CRIT2) ? 18 : 0);
		crit = Clampi(crit, 0, sd ? 80 : 50);
	}
	dm *= HeightMod(b, a, d) * DirMod(a, d);
	{
		float dh = (b.map.StandY(a.x, a.z) - b.map.StandY(d.x, d.z)) / kHStep;
		int steps = (int)(dh >= 0 ? dh + .5f : dh - .5f);
		if (steps > 4) steps = 4;
		if (steps < -4) steps = -4;
		bool bow = WeaponOf(a) == WT_BOW || UnitAttackStyle(a) == AS_SHOOT || UnitAttackStyle(a) == AS_SHOOT1;
		hit += steps * (bow ? 8 : 3);
	}
	int face = FacingOf(a, d);
	if (!magic) {
		if (face == 2) hit += 20;
		else if (face == 1) hit += 8;
		else hit -= 4;
	} else {
		if (face == 2) hit += 10;
		else if (face == 0) hit -= 8;
	}
	hit += UnitStat(a, S_HIT) / 2;
	hit -= UnitStat(d, S_AVO) / 2;
	int tri = TriRel(WeaponOf(a), WeaponOf(d));
	if (tri > 0) { hit += 15; dm *= 1.1f; }
	if (tri < 0) { hit -= 15; dm *= 0.9f; }
	if (WeaponOf(a) == WT_BOW && UnitMoveType(d) == MV_FLY) { hit += 20; dm *= 1.15f; }
	{
		bool bow = WeaponOf(a) == WT_BOW || UnitAttackStyle(a) == AS_SHOOT || UnitAttackStyle(a) == AS_SHOOT1;
		int elw = AttackElem(a, skill);
		int wx = TacWx(b.weather);
		if (wx == WX_RAIN || wx == WX_STORM) {
			if (elw == EL_FIRE) dm *= wx == WX_STORM ? 0.70f : 0.75f;
			else if (elw == EL_THUNDER) dm *= wx == WX_STORM ? 1.16f : 1.10f;
			if (bow) hit -= wx == WX_STORM ? 16 : 12;
		} else if (wx == WX_SNOW || wx == WX_HAIL) {
			if (elw == EL_ICE) dm *= wx == WX_HAIL ? 1.12f : 1.15f;
			else if (elw == EL_FIRE) dm *= 0.85f;
			if (bow) hit -= wx == WX_HAIL ? 10 : 8;
		} else if (wx == WX_ASH || wx == WX_HEAT) {
			if (elw == EL_FIRE) dm *= wx == WX_HEAT ? 1.08f : 1.12f;
			else if (wx == WX_HEAT && elw == EL_ICE) dm *= 0.88f;
			if (bow && wx == WX_ASH) hit -= 14;
		} else if (wx == WX_SAND) {
			if (bow) hit -= 18;
		} else if (wx == WX_FOG) {
			if (bow) hit -= 10;
			else hit -= 4;
		}
	}
	hit = Clampi(hit, magic ? 62 : 48, magic ? 100 : 99);
	{
		int grd = UnitStat(d, S_GRD);
		if (grd > 30) grd = 30;
		if (grd > 0) dm *= (100 - grd) / 100.f;
	}
	{
		int wh = WeakHitsOf(a, d, skill);
		if (wh >= 1) dm *= 2.f;
		if (wh >= 2) dm *= 2.f;
	}
	if (a.team == TEAM_PLAYER && d.team == TEAM_ENEMY) {
		int pct = FoeMasteryPct(FoeKills(d.monster));
		if (pct > 0) dm *= (100 + pct) / 100.f;
	}
	{
		int el = AttackElem(a, skill);
		dm *= AilmentBreak(d, el);
		{
			const GearDef* arm = GearOf(d.ch.reserved[7]);
			if (arm && arm->kind == 1 && el != EL_NONE && arm->elem == el) dm *= 0.72f;
		}
		if ((el == EL_FIRE && UnitHasPas(d, PAS_FIRE2)) || (el == EL_ICE && UnitHasPas(d, PAS_ICE2))
			|| (el == EL_THUNDER && UnitHasPas(d, PAS_THUNDER2)) || (el == EL_HOLY && UnitHasPas(d, PAS_HOLY2))
			|| (el == EL_DARK && UnitHasPas(d, PAS_DARK2)))
			dm *= .5f;
		else if ((el == EL_FIRE && UnitHasPas(d, PAS_FIRE)) || (el == EL_ICE && UnitHasPas(d, PAS_ICE))
			|| (el == EL_THUNDER && UnitHasPas(d, PAS_THUNDER)) || (el == EL_HOLY && UnitHasPas(d, PAS_HOLY))
			|| (el == EL_DARK && UnitHasPas(d, PAS_DARK)))
			dm *= .7f;
	}
	dmg = max(1, (int)roundf(dm));
}

float AilmentBreak(const Unit& d, int el)
{
	if (el == EL_NONE) return 1.f;
	// 炎と氷は互いに反対。炎上には氷、凍結には炎が倍。
	if (d.burn > 0 && el == EL_ICE) return 2.f;
	if (d.freeze > 0 && el == EL_FIRE) return 2.f;
	// 雷・風・土。スタンは風、混乱は土、鈍足は雷。
	if (d.stun > 0 && el == EL_WIND) return 2.f;
	if (d.confuse > 0 && el == EL_EARTH) return 2.f;
	if (d.slow > 0 && el == EL_THUNDER) return 2.f;
	// 毒・聖・闇。毒は聖、盲目は毒、沈黙は闇。
	if (d.poison > 0 && el == EL_HOLY) return 2.f;
	if (d.blind > 0 && el == EL_POISON) return 2.f;
	if (d.silence > 0 && el == EL_DARK) return 2.f;
	// 裂傷・睡眠・防御低下。裂傷は風、睡眠は雷、防御低下は土。
	if (d.bleed > 0 && el == EL_WIND) return 2.f;
	if (d.sleep > 0 && el == EL_THUNDER) return 2.f;
	if (d.defDown > 0 && el == EL_EARTH) return 2.f;
	return 1.f;
}

// 反対属性が当たったとき、燃える体が凍る、のように状態と見た目の属性を入れ替える。
// すでにスタン等で動けないときは、その拘束は残し、新しい拘束は足さない。
static int BreakAilment(Unit& d, int el, int* fromEl)
{
	if (fromEl) *fromEl = EL_NONE;
	if (el == EL_NONE) return EL_NONE;
	if (d.burn > 0 && el == EL_ICE) {
		if (fromEl) *fromEl = EL_FIRE;
		d.burn = 0;
		if (!UnitAilmentHeld(d) && !UnitHasPas(d, PAS_NOSLEEP)) d.freeze = 2;
		return EL_ICE;
	}
	if (d.freeze > 0 && el == EL_FIRE) {
		if (fromEl) *fromEl = EL_ICE;
		d.freeze = 0;
		if (d.burn <= 0) d.burn = 2;
		return EL_FIRE;
	}
	if ((d.stun > 0 || d.bleed > 0) && el == EL_WIND) {
		if (fromEl) *fromEl = d.stun > 0 ? EL_THUNDER : EL_NONE;
		d.stun = 0;
		d.bleed = 0;
		return EL_WIND;
	}
	if ((d.confuse > 0 || d.defDown > 0) && el == EL_EARTH) {
		if (fromEl) *fromEl = EL_EARTH;
		d.confuse = 0;
		d.defDown = 0;
		return EL_EARTH;
	}
	if ((d.slow > 0 || d.sleep > 0) && el == EL_THUNDER) {
		if (fromEl) *fromEl = EL_THUNDER;
		d.slow = 0;
		d.sleep = 0;
		if (d.stun <= 0 && !UnitAilmentHeld(d) && !UnitHasPas(d, PAS_NOSTUN)) d.stun = 1;
		return EL_THUNDER;
	}
	if (d.poison > 0 && el == EL_HOLY) {
		if (fromEl) *fromEl = EL_POISON;
		d.poison = 0;
		return EL_HOLY;
	}
	if (d.blind > 0 && el == EL_POISON) {
		if (fromEl) *fromEl = EL_NONE;
		d.blind = 0;
		if (d.poison <= 0 && !UnitHasPas(d, PAS_NOPOISON)) d.poison = 2;
		return EL_POISON;
	}
	if (d.silence > 0 && el == EL_DARK) {
		if (fromEl) *fromEl = EL_DARK;
		return EL_DARK;
	}
	return EL_NONE;
}

static bool CanCounter(const Battle& b, const Unit& d, const Unit& a)
{
	if (!d.alive || d.hp <= 0) return false;
	if (d.sleep > 0 || d.stun > 0) return false;
	return InRangeOfUnit(b, d, d.x, d.z, UnitRangeMin(d), UnitRangeMax(d), a, true);
}

int EstimateText(const Unit& a, int skill, wchar_t* out, int cap)
{
	if (!out || cap < 8) return 0;
	out[0] = 0;
	const SkillDef* sd = skill >= 0 ? &SkillInfo(skill) : NULL;
	if (sd && (sd->kind == SK_HEAL || sd->kind == SK_BUFF || sd->kind == SK_PASSIVE)) return 0;
	if (sd && sd->power <= 0 && !IsSummonId(skill)) return 0;
	int magic = sd ? (sd->kind == SK_MAGIC || IsSummonId(skill)) : (UnitMagAttack(a) != 0);
	int nHit = sd ? max(1, sd->hits) : 1;
	float dm;
	if (magic) {
		int atk = UnitStat(a, S_MAG) + (sd ? 0 : UnitWeaponPow(a));
		if (UnitHasSkill(a, SKL_P_SPELL)) atk = atk * 115 / 100;
		float mult = sd ? (150 + sd->power * 5) / 100.f : 1.f;
		float atkF = (float)atk;
		if (sd && !IsSummonId(skill)) atkF *= TacAllyScale(a, 0.018f);
		dm = atkF * mult;
		if (!sd) {
			float off = TacBasicOffense(a);
			if (off > 0.f) dm = off;
		}
		if (sd && IsSummonId(skill)) {
			int phys = UnitStat(a, S_STR) + UnitWeaponPow(a);
			float offense = (float)(phys > atk ? phys : atk);
			float want = offense * (2.1f + sd->power * 0.02f);
			float floorv = offense * 1.6f;
			if (want < floorv) want = floorv;
			dm = want * 0.75f;
		} else if (nHit > 1) dm /= (float)nHit;
	} else {
		int atk = UnitStat(a, S_STR) + UnitWeaponPow(a);
		if (sd && skill == SKL_HOLYBLADE) atk += UnitStat(a, S_MAG) / 2;
		if (UnitHasSkill(a, SKL_P_MIGHT)) atk = atk * 115 / 100;
		if (!sd && (WeaponOf(a) == WT_BOW || UnitAttackStyle(a) == AS_SHOOT || UnitAttackStyle(a) == AS_SHOOT1))
			atk = atk * 6 / 5;
		float mult = sd ? sd->power / 100.f : 1.f;
		if (mult < 0.05f) mult = 1.f;
		bool bowish = WeaponOf(a) == WT_BOW || UnitAttackStyle(a) == AS_SHOOT || UnitAttackStyle(a) == AS_SHOOT1
			|| skill == SKL_PIERCE || skill == SKL_SNIPE || skill == SKL_ARROWRAIN
			|| skill == SKL_ARROWLINE || skill == SKL_ARROWFAN || skill == SKL_CRESCENT;
		if (bowish && sd) mult *= 1.10f;
		if (!bowish) mult *= TacAllyScale(a, 0.034f);
		dm = (float)atk * mult;
		if (!sd) {
			float off = TacBasicOffense(a);
			if (off > 0.f) dm = off;
		}
	}
	int per = max(1, (int)(dm + 0.5f));
	const wchar_t* kind = magic
		? LangPick14(L"魔法", L"Magic", L"Magie", L"Magia", L"Magia", L"마법", L"魔法", L"سحر", L"Магия", L"Magie", L"Magia", L"Magie", L"Magia", L"Büyü")
		: LangPick14(L"物理", L"Phys", L"Phys", L"Fis", L"Fís", L"물리", L"物理", L"جسدي", L"Физ", L"Physisch", L"Fís", L"Fys", L"Fiz", L"Fizik");
	const wchar_t* ref = LangPick14(L"参考", L"Ref", L"Réf", L"Rif", L"Ref", L"참고", L"参考", L"مرجع", L"Оценка", L"Ref", L"Ref", L"Ref", L"Szac", L"Ref");
	if (!sd) {
		swprintf_s(out, cap, L"%s %s %d    %s %d", ref, kind, per,
			LangPick14(L"2回", L"2 hits", L"2 coups", L"2 colpi", L"2 golpes", L"2회", L"2次", L"ضربتان", L"2 удара", L"2 Treffer", L"2 golpes", L"2 treffers", L"2 trafienia", L"2 vuruş"),
			per * 2);
	} else if (nHit > 1) {
		swprintf_s(out, cap, L"%s %s %d×%d    %s %d", ref, kind, per, nHit,
			LangPick14(L"推定", L"Est", L"Est", L"Stima", L"Est", L"추정", L"推定", L"تقدير", L"Итого", L"Schätz", L"Est", L"Schat", L"Suma", L"Tahmin"),
			per * nHit);
	} else {
		swprintf_s(out, cap, L"%s %s %d", ref, kind, per);
	}
	return 1;
}

void CalcForecast(const Battle& b, int ai, int di, int skill, Forecast& f)
{
	memset(&f, 0, sizeof(f));
	const Unit& a = b.u[ai];
	const Unit& d = b.u[di];
	StrikeCalc(b, a, d, skill, f.dmg, f.hit, f.crit);
	f.weak = WeakHitsOf(a, d, skill);
	f.face = FacingOf(a, d);
	f.tri = TriRel(WeaponOf(a), WeaponOf(d));
	f.bowFly = (WeaponOf(a) == WT_BOW && UnitMoveType(d) == MV_FLY) ? 1 : 0;
	f.delay = 0;
	if (skill >= 0) {
		int fx = SkillInfo(skill).effect;
		if (fx == EF_SLOW && a.team != d.team && !UnitHasPas(d, PAS_NOSLOW) && !UnitAilmentHeld(d)) f.delay = 400;
	}
	f.follow = -1;
	f.fdmg = 0;
	if (a.team == TEAM_PLAYER && d.team != TEAM_PLAYER) {
		int n = 0;
		for (int i = 0; i < b.nUnits; i++) {
			const Unit& m = b.u[i];
			if (i == ai || !m.alive || m.hp <= 0 || m.team != TEAM_PLAYER) continue;
			if (m.sleep > 0 || m.stun > 0) continue;
			if (!InRangeOfUnit(b, m, m.x, m.z, UnitRangeMin(m), UnitRangeMax(m), d, true)) continue;
			n++;
		}
		if (n > 0) f.follow = n;
	}
	const SkillDef* sd = skill >= 0 ? &SkillInfo(skill) : NULL;
	f.hits = sd ? max(1, sd->hits) : ((UnitStat(a, S_AGI) - UnitStat(d, S_AGI) >= 5) ? 2 : 1);
	bool single = !sd || sd->area == 0;
	f.canCounter = single && a.team != d.team && CanCounter(b, d, a);
	if (f.canCounter) {
		StrikeCalc(b, d, a, -1, f.cdmg, f.chit, f.ccrit);
		f.chits = (UnitStat(d, S_AGI) - UnitStat(a, S_AGI) >= 5) ? 2 : 1;
	}
}

int SkillCastRangeMin(const Unit& u, int skill)
{
	const SkillDef& s = SkillInfo(skill);
	return s.rMax < 0 ? UnitRangeMin(u) : s.rMin;
}
int SkillCastRangeMax(const Unit& u, int skill)
{
	const SkillDef& s = SkillInfo(skill);
	return s.rMax < 0 ? UnitRangeMax(u) : s.rMax;
}

bool IsFormSkill(int skill) { return skill >= SKL_FORM0 && skill < SKL_FORMN; }
int FormOf(int skill) { return IsFormSkill(skill) ? SkillInfo(skill).form : FM_NONE; }

static int FormPushMark(FormMark* o, int n, int cap, int side, int fwd, int absolute, int onFoe, int group, int foe)
{
	if (!o || n >= cap) return n;
	o[n].side = side; o[n].fwd = fwd; o[n].absolute = absolute; o[n].onFoe = onFoe; o[n].group = group; o[n].foe = foe;
	return n + 1;
}
int FormMarks(int form, FormMark* out, int cap)
{
	int n = 0;
	switch (form) {
	case FM_WING:
	case FM_CROSS:
	case FM_WALL:
		n = FormPushMark(out, n, cap, -1, 0, 0, 0, -1, 0);
		n = FormPushMark(out, n, cap, 1, 0, 0, 0, -1, 0);
		break;
	case FM_BACK:
		n = FormPushMark(out, n, cap, 0, -1, 0, 0, -1, 0);
		break;
	case FM_TRI:
		n = FormPushMark(out, n, cap, -1, -1, 0, 0, 0, 0);
		n = FormPushMark(out, n, cap, 1, -1, 0, 0, 0, 0);
		break;
	case FM_CHAIN:
		n = FormPushMark(out, n, cap, 0, -1, 0, 0, 0, 0);
		n = FormPushMark(out, n, cap, 0, -2, 0, 0, 0, 0);
		break;
	case FM_DIA:
		n = FormPushMark(out, n, cap, -1, 1, 0, 0, 0, 0);
		n = FormPushMark(out, n, cap, 1, 1, 0, 0, 0, 0);
		break;
	case FM_SQUARE:
	case FM_PAIR:
		n = FormPushMark(out, n, cap, 1, 0, 1, 0, -1, 0);
		n = FormPushMark(out, n, cap, -1, 0, 1, 0, -1, 0);
		n = FormPushMark(out, n, cap, 0, 1, 1, 0, -1, 0);
		n = FormPushMark(out, n, cap, 0, -1, 1, 0, -1, 0);
		break;
	case FM_ECHELON:
		n = FormPushMark(out, n, cap, -1, 0, 0, 0, 0, 0);
		n = FormPushMark(out, n, cap, -1, -1, 0, 0, 0, 0);
		n = FormPushMark(out, n, cap, 1, 0, 0, 0, 1, 0);
		n = FormPushMark(out, n, cap, 1, -1, 0, 0, 1, 0);
		break;
	case FM_CIRCLE:
		for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
			if (dx == 0 && dz == 0) continue;
			n = FormPushMark(out, n, cap, dx, dz, 1, 0, -1, 0);
		}
		break;
	case FM_PINCER:
		n = FormPushMark(out, n, cap, 0, 1, 0, 1, -1, 1);
		n = FormPushMark(out, n, cap, -1, 1, 0, 1, 0, 0);
		n = FormPushMark(out, n, cap, 1, 1, 0, 1, 0, 0);
		break;
	default: break;
	}
	return n;
}
void FormDir(int dx, int dz, int& fx, int& fz)
{
	if (dx == 0 && dz == 0) { fx = 0; fz = 1; return; }
	if (abs(dx) > abs(dz)) { fx = dx > 0 ? 1 : -1; fz = 0; }
	else { fx = 0; fz = dz > 0 ? 1 : -1; }
}
static void FormAim(const Unit& u, int tx, int tz, int& fx, int& fz)
{
	if (tx != u.x || tz != u.z) {
		FormDir(tx - u.x, tz - u.z, fx, fz);
		return;
	}
	float s = sinf(u.face), c = cosf(u.face);
	int dx = (int)floorf(s >= 0.f ? s + 0.5f : s - 0.5f);
	int dz = (int)floorf(c >= 0.f ? c + 0.5f : c - 0.5f);
	FormDir(dx, dz, fx, fz);
}
void FormMarkPos(const Unit& u, int tx, int tz, const FormMark& m, int& ox, int& oz)
{
	if (m.absolute) { ox = u.x + m.side; oz = u.z + m.fwd; return; }
	int fx, fz;
	FormAim(u, tx, tz, fx, fz);
	int rx = fz, rz = -fx;
	if (m.onFoe) { ox = tx + m.side * rx; oz = tz + m.side * rz; return; }
	ox = u.x + m.fwd * fx + m.side * rx;
	oz = u.z + m.fwd * fz + m.side * rz;
}

static int FormAllyAt(const Battle& b, int x, int z, int team, int self)
{
	if (!Map::In(x, z)) return -1;
	int i = b.UnitAt(x, z);
	if (i < 0 || i == self) return -1;
	if (!b.u[i].alive || b.u[i].team != team) return -1;
	return i;
}

static int FormPush(int* out, int n, int maxOut, int id)
{
	if (id < 0 || n >= maxOut) return n;
	for (int i = 0; i < n; i++) if (out[i] == id) return n;
	out[n++] = id;
	return n;
}

static int FormNeed(int form)
{
	switch (form) {
	case FM_WING: case FM_BACK: case FM_PAIR: return 1;
	case FM_CROSS: case FM_WALL: case FM_TRI: case FM_CHAIN: case FM_DIA: case FM_ECHELON: case FM_PINCER: return 2;
	case FM_SQUARE: case FM_CIRCLE: return 3;
	default: return 1;
	}
}

int FormMembers(const Battle& b, int ui, int skill, int tx, int tz, int* out, int maxOut)
{
	int n = 0;
	if (!IsFormSkill(skill) || ui < 0 || ui >= b.nUnits) return 0;
	const Unit& u = b.u[ui];
	int form = FormOf(skill);
	int fx, fz;
	FormAim(u, tx, tz, fx, fz);
	int lx = -fz, lz = fx;
	int bx = -fx, bz = -fz;
	auto at = [&](int x, int z) { return FormAllyAt(b, x, z, u.team, ui); };
	auto add = [&](int id) { n = FormPush(out, n, maxOut, id); };
	switch (form) {
	case FM_WING:
		add(at(u.x + lx, u.z + lz));
		add(at(u.x - lx, u.z - lz));
		break;
	case FM_CROSS: case FM_WALL:
		add(at(u.x + lx, u.z + lz));
		add(at(u.x - lx, u.z - lz));
		break;
	case FM_BACK:
		add(at(u.x + bx, u.z + bz));
		break;
	case FM_TRI:
		add(at(u.x + bx + lx, u.z + bz + lz));
		add(at(u.x + bx - lx, u.z + bz - lz));
		break;
	case FM_CHAIN:
		add(at(u.x + bx, u.z + bz));
		add(at(u.x + bx * 2, u.z + bz * 2));
		break;
	case FM_DIA:
		add(at(u.x + fx + lx, u.z + fz + lz));
		add(at(u.x + fx - lx, u.z + fz - lz));
		break;
	case FM_SQUARE:
		add(at(u.x + 1, u.z)); add(at(u.x - 1, u.z)); add(at(u.x, u.z + 1)); add(at(u.x, u.z - 1));
		break;
	case FM_ECHELON: {
		int L = at(u.x + lx, u.z + lz), BL = at(u.x + bx + lx, u.z + bz + lz);
		int R = at(u.x - lx, u.z - lz), BR = at(u.x + bx - lx, u.z + bz - lz);
		if (L >= 0 && BL >= 0) { add(L); add(BL); }
		if (R >= 0 && BR >= 0) { add(R); add(BR); }
		break;
	}
	case FM_CIRCLE:
		for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
			if (dx == 0 && dz == 0) continue;
			add(at(u.x + dx, u.z + dz));
		}
		break;
	case FM_PAIR:
		add(at(u.x + 1, u.z)); add(at(u.x - 1, u.z)); add(at(u.x, u.z + 1)); add(at(u.x, u.z - 1));
		break;
	case FM_PINCER:
		add(at(tx + lx, tz + lz));
		add(at(tx - lx, tz - lz));
		break;
	default: break;
	}
	return n;
}

static float FormAllyMul(const Battle& b, const Unit& a, int skill, int tx, int tz)
{
	if (!IsFormSkill(skill)) return 1.f;
	int ui = -1;
	for (int i = 0; i < b.nUnits; i++) if (&b.u[i] == &a) { ui = i; break; }
	if (ui < 0) return 1.f;
	int mem[8];
	int n = FormMembers(b, ui, skill, tx, tz, mem, 8);
	int extra = n - FormNeed(FormOf(skill));
	if (extra < 0) extra = 0;
	if (extra > 3) extra = 3;
	return 1.f + extra * 0.2f;
}

bool FormFits(const Battle& b, int ui, int skill, int tx, int tz)
{
	if (!IsFormSkill(skill) || ui < 0 || ui >= b.nUnits) return false;
	int mem[8];
	int n = FormMembers(b, ui, skill, tx, tz, mem, 8);
	int form = FormOf(skill);
	if (form == FM_WING) return n >= 1;
	if (form == FM_CROSS || form == FM_WALL) return n >= 2;
	if (form == FM_ECHELON) return n >= 2;
	if (form == FM_SQUARE || form == FM_CIRCLE) return n >= 3;
	if (form == FM_PAIR) return n >= 1;
	return n >= FormNeed(form);
}

bool FormPickAim(const Battle& b, int ui, int skill, int& fx, int& fz)
{
	if (!IsFormSkill(skill) || ui < 0 || ui >= b.nUnits) return false;
	const Unit& u = b.u[ui];
	int curx, curz;
	FormAim(u, u.x, u.z, curx, curz);
	fx = curx; fz = curz;
	const SkillDef& sd = SkillInfo(skill);
	if (sd.rMax == 0) return FormFits(b, ui, skill, u.x, u.z);
	int rmin = SkillCastRangeMin(u, skill), rmax = SkillCastRangeMax(u, skill);
	BOOL friendly = sd.kind == SK_HEAL || sd.kind == SK_BUFF;
	int ok[4] = { 0, 0, 0, 0 };
	static const int kD[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
	auto idx = [](int x, int z) -> int {
		for (int i = 0; i < 4; i++) if (kD[i][0] == x && kD[i][1] == z) return i;
		return 0;
	};
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		int d = abs(x - u.x) + abs(z - u.z);
		if (d < rmin || d > rmax) continue;
		if (!FormFits(b, ui, skill, x, z)) continue;
		if (!friendly && !InAttackRange(b, u.x, u.z, rmin, rmax, x, z, true)) continue;
		int ax, az;
		FormAim(u, x, z, ax, az);
		ok[idx(ax, az)] = 1;
	}
	int ci = idx(curx, curz);
	if (ok[ci]) { fx = kD[ci][0]; fz = kD[ci][1]; return true; }
	for (int i = 0; i < 4; i++) if (ok[i]) { fx = kD[i][0]; fz = kD[i][1]; return true; }
	return false;
}

int FormBoostPct(const Battle& b, int ui, int skill, int tx, int tz)
{
	if (!IsFormSkill(skill)) return 100;
	int mem[8];
	int n = FormMembers(b, ui, skill, tx, tz, mem, 8);
	int extra = n - FormNeed(FormOf(skill));
	if (extra < 0) extra = 0;
	if (extra > 3) extra = 3;
	return 100 + extra * 15;
}

static bool FormReady(const Battle& b, int ui, int skill)
{
	int fx, fz;
	return FormPickAim(b, ui, skill, fx, fz);
}

bool SkillUsable(const Battle& b, int ui, int skill)
{
	const Unit& u = b.u[ui];
	if (u.silence > 0 && (IsSummonId(skill) || SkillInfo(skill).kind == SK_MAGIC)) return false;
	if (IsFormSkill(skill)) {
		if (!UnitHasSkill(u, skill) || u.mp < SkillInfo(skill).mp) return false;
		return FormReady(b, ui, skill);
	}
	if (IsSummonId(skill)) {
		int id = skill - SUMMON_BASE + 1;
		BOOL have = FALSE;
		for (int k = 0; k < 2; k++) if (EquippedSummonId(u.ch, k) == id) have = TRUE;
		if (!have) return false;
		const SkillDef& s = SkillInfo(skill);
		if (u.mp < s.mp) return false;
		if (u.cp < SummonCpCost(u, skill)) return false;
		return u.hp > SummonHpCost(u, skill);
	}
	if (IsCraftId(skill)) {
		BOOL have = FALSE;
		for (int i = 0; i < CRAFT_SLOT_N; i++) if (u.craftEq[i] == skill) have = TRUE;
		if (!have) return false;
		const SkillDef& s = SkillInfo(skill);
		if (s.kind == SK_PASSIVE) return false;
		if (IsCraftSuper(skill) && u.cp < 100) return false;
		int cost = 0;
		int idx = skill - CRAFT_BASE;
		if (idx >= 0 && idx < CRAFT_N) cost = CraftCpCost(idx);
		return u.cp >= cost;
	}
	const SkillDef& s = SkillInfo(skill);
	if (s.kind == SK_PASSIVE) return false;
	return UnitHasSkill(u, skill) && u.mp >= s.mp;
}

bool SkillTargetHelps(const Battle& b, int ui, int skill, int ti)
{
	if (ti < 0 || ti >= b.nUnits) return false;
	const Unit& t = b.u[ti];
	const SkillDef& s = SkillInfo(skill);
	if (s.effect == EF_RAISE)
		return !t.alive && t.killed && t.team == b.u[ui].team && RaisePlaceable(b, t);
	if (s.kind == SK_HEAL) {
		if (!t.alive || t.team != b.u[ui].team) return false;
		BOOL hurt = t.hp < t.ch.stat[S_HP];
		BOOL ail = t.poison > 0 || t.slow > 0 || t.sleep > 0 || t.stun > 0 || t.confuse > 0 || t.defDown > 0
			|| t.freeze > 0 || t.burn > 0 || t.bleed > 0 || t.blind > 0 || t.silence > 0;
		if (s.effect == EF_CLEANSE) return hurt || ail;
		return hurt;
	}
	if (s.kind == SK_BUFF) {
		if (!t.alive || t.team != b.u[ui].team) return false;
		if (s.effect == EF_REACT) return ti != ui && t.ct < 1000;
		return true;
	}
	return t.alive && t.team != b.u[ui].team;
}

bool SkillAimHelps(const Battle& b, int ui, int skill, int cx, int cz)
{
	int tg[64];
	int n = SkillTargets(b, ui, skill, cx, cz, tg, 64);
	for (int i = 0; i < n; i++) if (SkillTargetHelps(b, ui, skill, tg[i])) return true;
	return false;
}

static bool SkillReachUnit(const Battle& b, int ui, int skill, int ti)
{
	if (ui < 0 || ui >= b.nUnits || ti < 0 || ti >= b.nUnits) return false;
	const Unit& u = b.u[ui];
	const Unit& t = b.u[ti];
	const SkillDef& s = SkillInfo(skill);
	int rmin = SkillCastRangeMin(u, skill);
	int rmax = SkillCastRangeMax(u, skill);
	int dist = FootDist(u.x, u.z, UnitSpan(u), t.x, t.z, UnitSpan(t));
	if (s.effect == EF_RAISE) {
		if (dist <= 0) return true;
		int add = LayerRangeCost(u.deck, t.deck);
		if (add < 0) return false;
		return dist + add >= rmin && dist + add <= rmax;
	}
	BOOL friendly = s.kind == SK_HEAL || s.kind == SK_BUFF;
	if (friendly) {
		int add = LayerRangeCost(u.deck, t.deck);
		if (add < 0) return false;
		return dist + add >= rmin && dist + add <= rmax;
	}
	return InRangeOfUnit(b, u, u.x, u.z, rmin, rmax, t, true);
}

static bool UnitSeenFrom(const Battle& b, int ax, int az, int fromLayer, const Unit& t)
{
	if (!Map::In(ax, az)) return false;
	int tL = LayerOn(b.map.At(t.x, t.z), t.deck) ? t.deck : 0;
	float yb = LayerHt(b.map.At(t.x, t.z), tL) * kHStep + .6f;
	float ya = LayerHt(b.map.At(ax, az), fromLayer) * kHStep + .6f;
	int st = UnitSpan(t);
	if (st < 1) st = 1;
	for (int tz = 0; tz < st; tz++) for (int tx = 0; tx < st; tx++) {
		int cx = t.x + tx, cz = t.z + tz;
		if (!Map::In(cx, cz)) continue;
		if (LineOfSightY(b.map, ax, az, ya, cx, cz, yb)) return true;
	}
	return false;
}

static bool TileSeesUnit(const Battle& b, int ax, int az, const Unit& t)
{
	if (!Map::In(ax, az)) return false;
	const Cell& ac = b.map.At(ax, az);
	int nL = CellLayN(ac);
	for (int L = 0; L < nL; L++) {
		if (!LayerOn(ac, L) || SightTerrain(LayerTr(ac, L))) continue;
		if (UnitSeenFrom(b, ax, az, L, t)) return true;
	}
	return false;
}

bool SkillWorth(const Battle& b, int ui, int skill)
{
	if (ui < 0 || ui >= b.nUnits || !SkillUsable(b, ui, skill)) return false;
	const Unit& u = b.u[ui];
	const SkillDef& s = SkillInfo(skill);
	if (s.rMax == 0) return SkillAimHelps(b, ui, skill, u.x, u.z);
	int rmin = SkillCastRangeMin(u, skill);
	int rmax = SkillCastRangeMax(u, skill);
	int aL = LayerOn(b.map.At(u.x, u.z), u.deck) ? u.deck : 0;
	BOOL friendly = s.kind == SK_HEAL || s.kind == SK_BUFF;
	if (s.area > 0) {
		for (int dz = -rmax; dz <= rmax; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
			int d = abs(dx) + abs(dz);
			if (d < rmin || d > rmax) continue;
			int cx = u.x + dx, cz = u.z + dz;
			if (!Map::In(cx, cz)) continue;
			if (IsFormSkill(skill) && !FormFits(b, ui, skill, cx, cz)) continue;
			BOOL reach = FALSE;
			for (int L = 0; L < LAY_N && !reach; L++)
				if (AttackReachLayer(b, u.x, u.z, aL, rmin, rmax, cx, cz, L, !friendly)) reach = TRUE;
			if (!reach) continue;
			if (SkillAimHelps(b, ui, skill, cx, cz)) return true;
		}
		if (s.effect == EF_RAISE && SkillAimHelps(b, ui, skill, u.x, u.z)) return true;
		return false;
	}
	for (int i = 0; i < b.nUnits; i++) {
		if (!SkillTargetHelps(b, ui, skill, i)) continue;
		if (!SkillReachUnit(b, ui, skill, i)) continue;
		if (IsFormSkill(skill) && !FormFits(b, ui, skill, b.u[i].x, b.u[i].z)) continue;
		return true;
	}
	return false;
}

bool ShapeCell(int shape, int area, int ax, int az, int cx, int cz, int tx, int tz)
{
	if (shape != SH_LINE && shape != SH_FAN)
		return Dist(tx, tz, cx, cz) <= max(0, area);
	int dx = cx - ax, dz = cz - az;
	if (dx == 0 && dz == 0) return false;
	int sx = (abs(dx) * 2 < abs(dz)) ? 0 : (dx > 0 ? 1 : -1);
	int sz = (abs(dz) * 2 < abs(dx)) ? 0 : (dz > 0 ? 1 : -1);
	if (sx == 0 && sz == 0) return false;
	int len = area > 0 ? area : 1;
	int ox = tx - ax, oz = tz - az;
	int along, side;
	if (sx != 0 && sz != 0) {
		int adx = ox * sx, adz = oz * sz;
		if (adx < 1 || adz < 1) return false;
		along = adx < adz ? adx : adz;
		side = adx > adz ? adx - adz : adz - adx;
	} else if (sx != 0) {
		along = ox * sx;
		side = oz < 0 ? -oz : oz;
	} else {
		along = oz * sz;
		side = ox < 0 ? -ox : ox;
	}
	if (along < 1 || along > len) return false;
	if (shape == SH_LINE) return side == 0;
	int wide = along - 1;
	if (wide > 2) wide = 2;
	return side <= wide;
}

int SkillTargets(const Battle& b, int caster, int skill, int cx, int cz, int* out, int maxOut)
{
	const Unit& c = b.u[caster];
	const SkillDef& s = SkillInfo(skill);
	if (IsFormSkill(skill) && !FormFits(b, caster, skill, cx, cz)) return 0;
	bool friendly = (s.kind == SK_HEAL || s.kind == SK_BUFF);
	int n = 0;
	for (int i = 0; i < b.nUnits && n < maxOut; i++) {
		const Unit& t = b.u[i];
		if (s.effect == EF_RAISE) {
			if (t.alive || !t.killed || t.team != c.team) continue;
			if (FootDist(cx, cz, 1, t.x, t.z, UnitSpan(t)) > max(0, s.area)) continue;
			if (!RaisePlaceable(b, t)) continue;
			out[n++] = i;
			continue;
		}
		if (!t.alive) continue;
		bool ally = (t.team == c.team);
		if (friendly != ally) continue;
		{
			int fromL = LayerOn(b.map.At(c.x, c.z), c.deck) ? c.deck : 0;
			int toL = LayerOn(b.map.At(t.x, t.z), t.deck) ? t.deck : 0;
			int add = LayerRangeCost(fromL, toL);
			if (add < 0) continue;
			if (s.area > 0) {
				int blast = FootDist(cx, cz, 1, t.x, t.z, UnitSpan(t));
				if (blast + add > max(0, s.area)) continue;
			} else {
				int reach = (s.rMax == 0) ? 0 : SkillCastRangeMax(c, skill);
				if (FootDist(c.x, c.z, UnitSpan(c), t.x, t.z, UnitSpan(t)) + add > reach) continue;
			}
		}
		if (!UnitOnShape(s, c, cx, cz, t)) continue;
		if (s.rMax == 0 && i == caster && s.kind != SK_HEAL && s.kind != SK_BUFF) continue;
		if (!friendly) {
			if (s.area > 0) {
				if (!TileSeesUnit(b, cx, cz, t)) continue;
			} else {
				int aL = LayerOn(b.map.At(c.x, c.z), c.deck) ? c.deck : 0;
				if (!UnitSeenFrom(b, c.x, c.z, aL, t)) continue;
			}
		}
		out[n++] = i;
	}
	return n;
}

int ExpForAction(const Unit& a, const Unit& d, int dmg, int stage)
{
	if (stage < 1) stage = 1;
	int dl = d.ch.lv - a.ch.lv;
	/* レベル差。格下でもゼロにはしない。上にはキャップしない。 */
	double lvK = 1.0 + (double)dl * 0.12;
	if (lvK < 0.2) lvK = 0.2;
	int hp = d.ch.stat[S_HP];
	double frac = 0.0;
	if (dmg > 0 && hp > 0) {
		frac = (double)dmg / (double)hp;
		if (frac > 1.0) frac = 1.0;
	}
	/* ダメージ割合・レベル差・ステージ番号。共闘は AwardExp。1 行動の上限は設けない。 */
	double e = (8.0 + (double)stage * 0.48) * (0.38 + 0.32 * frac) * lvK;
	if (d.boss) e *= 1.25;
	// ラッキー☆の×3は AwardExp / Preview 側で明示適用（ここだと見積と実付与の差が分かりにくい）。
	if (e < 1.0) return 1;
	if (e > 2000000000.0) return 2000000000;
	return (int)(e + 0.5);
}
long long ExpShareAoe(long long e, int nHit)
{
	if (e <= 0) return 0;
	if (nHit <= 1) return e;
	if (nHit > 32) nHit = 32;
	double s = 1.0 / sqrt((double)nHit);
	long long o = (long long)((double)e * s + 0.5);
	if (o < (long long)nHit) o = nHit;
	return o;
}

/* ステージ1で Lv1、19で 30、以後同じ傾き。9999で約1.6万。EX も同じ傾きのまま。 */
static int StageEnemyLevel(int stage)
{
	if (stage < 1) stage = 1;
	long long lv = 1 + ((long long)stage - 1) * 29 / 18;
	if (lv > TAC_ENEMY_LV_MAX) lv = TAC_ENEMY_LV_MAX;
	if (lv < 1) lv = 1;
	return (int)lv;
}

static bool TurnFx(int effect)
{
	return effect == EF_SLOW || effect == EF_SLEEP || effect == EF_STUN || effect == EF_CONFUSE || effect == EF_FREEZE;
}
static void ClearAilments(Unit& d)
{
	d.sleep = d.confuse = d.stun = d.poison = d.slow = d.defDown = 0;
	d.freeze = d.burn = d.bleed = d.blind = d.silence = 0;
}

// すでに付いている異常は伸ばさない。鈍足・睡眠・スタン・混乱は、動けるようになるまで重ねない。
static bool ApplyEffect(Battle& b, Unit& a, Unit& d, int effect, Rng& r)
{
	int turns = r.Range(1, 3);
	int ailBonus = 0;
	if (d.team == TEAM_ENEMY && effect != EF_NONE) {
		const MonsterDef& md = MonsterInfo(d.monster);
		for (int i = 0; i < 2; i++) if (md.ailWeak[i] == effect) ailBonus = 1;
	}
	turns += ailBonus;
	switch (effect) {
	case EF_SLOW:
		if (UnitHasPas(d, PAS_NOSLOW) || UnitAilmentHeld(d)) return false;
		d.slow = turns;
		d.ct = max(0, d.ct - 400);
		return true;
	case EF_DEFDOWN:
		if (UnitHasPas(d, PAS_NODOWN) || d.defDown > 0) return false;
		d.buffDef = 0;
		d.defDown = turns;
		return true;
	case EF_POISON:
		if (UnitHasPas(d, PAS_NOPOISON) || d.poison > 0) return false;
		d.poison = turns;
		return true;
	case EF_SLEEP:
		if (UnitHasPas(d, PAS_NOSLEEP) || UnitAilmentHeld(d)) return false;
		d.sleep = 1 + ailBonus;
		return true;
	case EF_CONFUSE:
		if (UnitHasPas(d, PAS_NOCONFUSE) || UnitAilmentHeld(d)) return false;
		d.confuse = 2 + ailBonus;
		return true;
	case EF_STUN:
		if (UnitHasPas(d, PAS_NOSTUN) || UnitAilmentHeld(d)) return false;
		d.stun = 1 + ailBonus;
		return true;
	case EF_FREEZE:
		if (UnitHasPas(d, PAS_NOSLEEP) || UnitAilmentHeld(d)) return false;
		d.freeze = 2 + ailBonus;
		return true;
	case EF_BURN:
		if (d.burn > 0) return false;
		d.burn = turns;
		return true;
	case EF_BLEED:
		if (d.bleed > 0) return false;
		d.bleed = turns;
		return true;
	case EF_BLIND:
		if (d.blind > 0) return false;
		d.blind = turns;
		return true;
	case EF_SILENCE:
		if (d.silence > 0) return false;
		d.silence = turns;
		return true;
	case EF_CLEANSE: {
		bool had = d.poison > 0 || d.sleep > 0 || d.confuse > 0 || d.stun > 0 || d.slow > 0 || d.defDown > 0
			|| d.freeze > 0 || d.burn > 0 || d.bleed > 0 || d.blind > 0 || d.silence > 0;
		ClearAilments(d);
		return had;
	}
	case EF_BUFF_DEF: d.buffDef += 3; if (d.buffDef > 6) d.buffDef = 6; return true;
	case EF_BUFF_AGI: d.buffAgi += 3; if (d.buffAgi > 6) d.buffAgi = 6; return true;
	case EF_BUFF_ATK: d.buffAtk += 3; if (d.buffAtk > 6) d.buffAtk = 6; return true;
	case EF_REACT:
		if (d.alive && d.team == a.team && d.ct < 1000) { d.ct = 1300; return true; }
		return false;
	default: break;
	}
	(void)b; (void)a; (void)turns;
	return false;
}

static void AddCp(Unit& u, int n)
{
	if (n < 1) return;
	if (UnitHasPas(u, PAS_CP2)) n *= 2;
	else if (UnitHasPas(u, PAS_CP)) n = n * 3 / 2;
	n += max(0, UnitStat(u, S_CPL) / 8);
	if (u.cp < 0) u.cp = 0;
	int c = u.cp + n;
	if (c > CP_MAX) c = CP_MAX;
	u.cp = c;
}

// 相手がかなり上のレベルのとき、味方は半分の確率で HP1 で踏みとどまる。
static BOOL GutsHold(Unit& def, const Unit& atk, Rng& r)
{
	if (def.team != TEAM_PLAYER || atk.team == def.team) return FALSE;
	int dl = def.ch.lv > 0 ? def.ch.lv : 1;
	int al = atk.ch.lv;
	if (al < dl + 8) return FALSE;
	if (al * 2 < dl * 3) return FALSE;
	if (!r.Chance(50)) return FALSE;
	def.hp = 1;
	return TRUE;
}

static int HealAmount(const Unit& a, const Unit& d, const SkillDef& sd, int skill)
{
	int pot = UnitStat(a, S_MAG);
	if (IsCraftId(skill)) pot = max(pot, (UnitStat(a, S_STR) + pot) / 2);
	else if (UnitHasSkill(a, SKL_P_SPELL)) pot = pot * 115 / 100;
	int pct = 16 + sd.power;
	if (pct > 65) pct = 65;
	if (pct < 12) pct = 12;
	int hp = d.ch.stat[S_HP];
	if (hp < 1) hp = 1;
	int amt = pot + hp * pct / 100;
	if (UnitHasPas(a, PAS_HEAL2)) amt = amt * 150 / 100;
	else if (UnitHasPas(a, PAS_HEAL)) amt = amt * 125 / 100;
	if (amt < 1) amt = 1;
	return amt;
}

int PreviewHealAmount(const Unit& a, const Unit& d, int skill)
{
	if (skill < 0) return 0;
	const SkillDef& sd = SkillInfo(skill);
	if (sd.kind != SK_HEAL || sd.effect == EF_RAISE) return 0;
	return HealAmount(a, d, sd, skill);
}

int ResolveAttack(Battle& b, int ai, int di, int skill, Rng& r, Strike* out, int maxOut, int cx, int cz)
{
	int n = 0;
	Unit& a = b.u[ai];
	const SkillDef* sd = skill >= 0 ? &SkillInfo(skill) : NULL;
	if (sd) {
		if (IsCraftId(skill)) {
			if (IsCraftSuper(skill)) a.cp = 0;
			else a.cp = max(0, a.cp - CraftCpCost(skill - CRAFT_BASE));
		}
		else a.mp = max(0, a.mp - sd->mp);
		if (IsSummonId(skill)) {
			a.hp = max(1, a.hp - SummonHpCost(a, skill));
			a.cp = max(0, a.cp - SummonCpCost(a, skill));
		}
		if (IsFormSkill(skill) && FormOf(skill) == FM_SQUARE) {
			int mem[8];
			int nm = FormMembers(b, ai, skill, a.x, a.z, mem, 8);
			a.buffDef = max(a.buffDef, 3);
			for (int i = 0; i < nm; i++) b.u[mem[i]].buffDef = max(b.u[mem[i]].buffDef, 3);
		}
	}
	auto scaleForm = [&](int dmg, int tx, int tz) -> int {
		if (dmg <= 0 || !IsFormSkill(skill)) return dmg;
		return (int)((long long)dmg * FormBoostPct(b, ai, skill, tx, tz) / 100);
	};
	// 範囲・回復・補助。範囲技は指定マスを中心にする（空マスでも可）
	if (sd && (sd->area > 0 || sd->kind == SK_HEAL || sd->kind == SK_BUFF)) {
		int targets[MAX_UNITS];
		int cx0, cz0;
		if (sd->rMax == 0) { cx0 = a.x; cz0 = a.z; }
		else if (Map::In(cx, cz)) { cx0 = cx; cz0 = cz; }
		else { cx0 = b.u[di].x; cz0 = b.u[di].z; }
		int nt = SkillTargets(b, ai, skill, cx0, cz0, targets, MAX_UNITS);
		int waves = (sd->kind == SK_HEAL || sd->kind == SK_BUFF) ? 1 : max(1, sd->hits);
		for (int wave = 0; wave < waves && n < maxOut; wave++)
		for (int k = 0; k < nt && n < maxOut; k++) {
			if (targets[k] < 0 || targets[k] >= b.nUnits) continue;
			Unit& d = b.u[targets[k]];
			bool corpse = d.hp <= 0;
			if (corpse && sd->effect != EF_RAISE) continue;
			Strike s = {};
			s.attacker = ai; s.defender = targets[k]; s.skill = skill; s.volley = wave;
			if (sd->kind == SK_HEAL) {
				if (sd->effect == EF_RAISE) {
					if (!SettleRaised(b, d)) continue;
					d.alive = 1;
					d.killed = 0;
					d.hp = max(1, d.ch.stat[S_HP] * 45 / 100);
					d.dissolve = 0;
					d.ct = max(d.ct, 200);
					s.dmg = -d.hp;
					s.hit = 1;
					b.RebuildOcc();
				} else {
					// 回復は命中判定をしない。
					int amt = HealAmount(a, d, *sd, skill);
					if (IsFormSkill(skill)) amt = (int)(amt * FormAllyMul(b, a, skill, a.x, a.z));
					amt = amt * FormBoostPct(b, ai, skill, cx0, cz0) / 100;
					int before = d.hp;
					d.hp = min(d.ch.stat[S_HP], d.hp + amt);
					s.dmg = -(d.hp - before);
					s.hit = 1;
					if (sd->effect == EF_CLEANSE) s.landed = ApplyEffect(b, a, d, sd->effect, r) ? 1 : 0;
					AddCp(a, 2);
				}
			} else if (sd->kind == SK_BUFF) {
				s.landed = ApplyEffect(b, a, d, sd->effect, r) ? 1 : 0;
				s.hit = 1;
			} else {
				int dmg, hit, crit;
				StrikeCalc(b, a, d, skill, dmg, hit, crit);
				if (corpse) {
					s.hit = 1;
					s.overkill = 1;
					s.crit = (int)(r.Next() % 100) < crit;
					s.dmg = scaleForm(s.crit ? dmg * 2 : dmg, cx0, cz0);
					AddCp(a, 2 + s.dmg / 20);
				} else {
				s.hit = (int)(r.Next() % 100) < hit;
				if (s.hit) {
					s.crit = (int)(r.Next() % 100) < crit;
					s.dmg = scaleForm(s.crit ? dmg * 2 : dmg, cx0, cz0);
					if ((long long)s.dmg >= (long long)d.hp * 2) s.overkill = 1;
					d.hp -= s.dmg;
					AddCp(a, 2 + s.dmg / 20);
					AddCp(d, 3 + s.dmg / 20);
					bool held = UnitAilmentHeld(d);
					int fromEl = EL_NONE;
					int react = BreakAilment(d, AttackElem(a, skill), &fromEl);
					if (react) { s.react = react; s.reactFrom = fromEl; }
					if (s.dmg > 0 && !(held && TurnFx(sd->effect))) d.sleep = 0;
					if (sd->effect != EF_NONE) s.landed = ApplyEffect(b, a, d, sd->effect, r) ? 1 : 0;
					if (d.hp <= 0) {
						if (!d.pasUsed && UnitHasPas(d, PAS_GUARD)) { d.hp = 1; d.pasUsed = 1; }
						else if (GutsHold(d, a, r)) s.guts = 1;
						else { d.hp = 0; ClearAilments(d); s.kill = 1; }
					}
					if (a.team != d.team && !held && !UnitHasPas(d, PAS_NOSLOW)) d.ct = max(0, d.ct - 160);
				}
				}
			}
			out[n++] = s;
			if (s.kill || s.overkill) {
				for (int t2 = k + 1; t2 < nt; t2++) if (targets[t2] == targets[k]) targets[t2] = -1;
			}
		}
		if (di >= 0 && di < b.nUnits && n < maxOut && sd->kind != SK_HEAL && sd->kind != SK_BUFF) {
			int saw = 0, missed = 0;
			for (int i = 0; i < n; i++) {
				if (out[i].counter || out[i].attacker != ai || out[i].defender != di) continue;
				saw = 1;
				if (!out[i].hit) missed = 1;
			}
			Unit& d0 = b.u[di];
			if (saw && missed && d0.hp > 0 && a.hp > 0 && d0.team != a.team
				&& d0.sleep <= 0 && d0.stun <= 0 && d0.freeze <= 0) {
				int dmg, hit, crit;
				StrikeCalc(b, d0, a, -1, dmg, hit, crit);
				Strike s = {};
				s.attacker = di; s.defender = ai; s.skill = -1; s.counter = 1;
				// MISS 起点の反撃は必ず命中（双方とも）。命中ロールはしない。
				s.hit = 1;
				if (a.hp > 0) {
					s.crit = (int)(r.Next() % 100) < crit;
					s.dmg = s.crit ? dmg * 2 : dmg;
					if ((long long)s.dmg >= (long long)a.hp * 2) s.overkill = 1;
					a.hp -= s.dmg;
					int fromEl = EL_NONE;
					int react = BreakAilment(a, AttackElem(d0, -1), &fromEl);
					if (react) { s.react = react; s.reactFrom = fromEl; }
					if (a.hp <= 0) {
						if (!a.pasUsed && UnitHasPas(a, PAS_GUARD)) { a.hp = 1; a.pasUsed = 1; }
						else if (GutsHold(a, d0, r)) s.guts = 1;
						else { a.hp = 0; ClearAilments(a); s.kill = 1; }
					}
				}
				out[n++] = s;
			}
		}
		return n;
	}
	// 単体（通常・物理スキル・単体魔法）＋反撃
	Unit& d = b.u[di];
	Forecast f;
	CalcForecast(b, ai, di, skill, f);
	int aLeft = f.hits, dLeft = f.canCounter ? f.chits : 0;
	// 多段は攻撃側の段をまとめてから反撃。段ごとにモーションを分け、同じ段の複数対象は同時着弾。
	auto swing = [&](bool atkTurn, int volley) -> bool {
		Unit& x = atkTurn ? a : d;
		Unit& y = atkTurn ? d : a;
		if (x.hp <= 0 || n >= maxOut) return true;
		if (y.hp <= 0) return true;
		int sk = atkTurn ? skill : -1;
		int dmg, hit, crit;
		StrikeCalc(b, x, y, sk, dmg, hit, crit);
		Strike s = {};
		s.attacker = atkTurn ? ai : di;
		s.defender = atkTurn ? di : ai;
		s.skill = sk;
		s.counter = atkTurn ? 0 : 1;
		s.volley = volley;
		s.hit = (int)(r.Next() % 100) < hit;
		if (s.hit) {
			s.crit = (int)(r.Next() % 100) < crit;
			s.dmg = atkTurn ? scaleForm(s.crit ? dmg * 2 : dmg, y.x, y.z) : (s.crit ? dmg * 2 : dmg);
			if ((long long)s.dmg >= (long long)y.hp * 2) s.overkill = 1;
			y.hp -= s.dmg;
			AddCp(x, 2 + s.dmg / 20);
			AddCp(y, 3 + s.dmg / 20);
			if (sk >= 0 && SkillInfo(sk).effect == EF_DRAIN) {
				int h = s.dmg / 2;
				x.hp = min(x.ch.stat[S_HP], x.hp + h);
				s.drain = h;
			}
			bool held = UnitAilmentHeld(y);
			int fx = (sk >= 0) ? SkillInfo(sk).effect : EF_NONE;
			int fromEl = EL_NONE;
			int react = BreakAilment(y, AttackElem(x, sk), &fromEl);
			if (react) { s.react = react; s.reactFrom = fromEl; }
			if (s.dmg > 0 && !(held && TurnFx(fx))) y.sleep = 0;
			if (fx != EF_NONE && fx != EF_DRAIN && fx != EF_IGNDEF)
				s.landed = ApplyEffect(b, x, y, fx, r) ? 1 : 0;
			if (y.hp <= 0) {
				if (!y.pasUsed && UnitHasPas(y, PAS_GUARD)) { y.hp = 1; y.pasUsed = 1; }
				else if (GutsHold(y, x, r)) s.guts = 1;
				else { y.hp = 0; ClearAilments(y); s.kill = 1; }
			}
			if (x.team != y.team && !held && !UnitHasPas(y, PAS_NOSLOW)) y.ct = max(0, y.ct - (160 + (s.crit ? 80 : 0)));
		}
		out[n++] = s;
		return s.kill != 0 || s.overkill != 0;
	};
	int missed = 0;
	for (int v = 0; aLeft > 0 && n < maxOut; aLeft--, v++) {
		if (a.hp <= 0) break;
		int before = n;
		if (swing(true, v)) break; // kill / overkill で余剰段を止める
		if (n > before && !out[n - 1].hit) missed = 1;
	}
	// 攻撃側が1段でも MISS したら、反撃権が無くても必ず1回カウンター（睡眠・スタン・凍結中は除く）。
	int missCounter = 0;
	if (missed && d.hp > 0 && a.hp > 0 && d.team != a.team
		&& d.sleep <= 0 && d.stun <= 0 && d.freeze <= 0) {
		if (dLeft < 1) dLeft = 1;
		missCounter = 1;
	}
	for (int v = 0; dLeft > 0 && n < maxOut; dLeft--, v++) {
		if (a.hp <= 0 || d.hp <= 0) break;
		if (d.sleep > 0 || d.stun > 0) break;
		int before = n;
		if (swing(false, v)) break;
		// MISS 起点の反撃は命中を強制（振れ幅でまた外さない）。
		if (missCounter && n > before && out[n - 1].counter && !out[n - 1].hit && a.hp > 0) {
			Strike& cs = out[n - 1];
			int dmg, hit, crit;
			StrikeCalc(b, d, a, -1, dmg, hit, crit);
			cs.hit = 1;
			cs.crit = (int)(r.Next() % 100) < crit;
			cs.dmg = cs.crit ? dmg * 2 : dmg;
			if ((long long)cs.dmg >= (long long)a.hp * 2) cs.overkill = 1;
			a.hp -= cs.dmg;
			int fromEl = EL_NONE;
			int react = BreakAilment(a, AttackElem(d, -1), &fromEl);
			if (react) { cs.react = react; cs.reactFrom = fromEl; }
			if (a.hp <= 0) {
				if (!a.pasUsed && UnitHasPas(a, PAS_GUARD)) { a.hp = 1; a.pasUsed = 1; }
				else if (GutsHold(a, d, r)) cs.guts = 1;
				else { a.hp = 0; ClearAilments(a); cs.kill = 1; }
			}
		}
		missCounter = 0; // 強制は1発だけ
	}
	return n;
}

int AppendAssists(Battle& b, Rng& r, Strike* out, int n, int maxOut)
{
	if (!out || n <= 0 || n >= maxOut) return n;
	int ai = -1;
	int foes[8];
	int nf = 0;
	for (int k = 0; k < n; k++) {
		const Strike& s = out[k];
		if (s.counter) continue;
		if (s.attacker < 0 || s.attacker >= b.nUnits) continue;
		if (b.u[s.attacker].team != TEAM_PLAYER) continue;
		if (s.skill >= 0) {
			int kind = SkillInfo(s.skill).kind;
			if (kind == SK_HEAL || kind == SK_BUFF || kind == SK_PASSIVE) continue;
		}
		if (ai < 0) ai = s.attacker;
		if (s.defender < 0 || s.defender >= b.nUnits) continue;
		if (b.u[s.defender].team == TEAM_PLAYER) continue;
		BOOL have = FALSE;
		for (int i = 0; i < nf; i++) if (foes[i] == s.defender) have = TRUE;
		if (!have && nf < 8) foes[nf++] = s.defender;
	}
	if (ai < 0 || nf == 0) return n;
	for (int i = 0; i < b.nUnits && n < maxOut; i++) {
		if (i == ai) continue;
		Unit& m = b.u[i];
		if (!m.alive || m.hp <= 0 || m.team != TEAM_PLAYER) continue;
		if (m.sleep > 0 || m.stun > 0) continue;
		int di = -1;
		for (int f = 0; f < nf; f++) {
			Unit& e = b.u[foes[f]];
			if (e.hp <= 0 || !e.alive) continue;
			if (!InRangeOfUnit(b, m, m.x, m.z, UnitRangeMin(m), UnitRangeMax(m), e, true)) continue;
			di = foes[f];
			break;
		}
		if (di < 0) continue;
		if ((r.Next() & 1u) != 0) continue;
		Unit& y = b.u[di];
		if (y.hp <= 0) continue;
		int dmg, hit, crit;
		StrikeCalc(b, m, y, -1, dmg, hit, crit);
		Strike s = {};
		s.attacker = i;
		s.defender = di;
		s.skill = -1;
		s.hit = (int)(r.Next() % 100) < hit;
		if (s.hit) {
			s.crit = (int)(r.Next() % 100) < crit;
			s.dmg = s.crit ? dmg * 2 : dmg;
			if ((long long)s.dmg >= (long long)y.hp * 2) s.overkill = 1;
			y.hp -= s.dmg;
			if (y.hp <= 0) {
				if (GutsHold(y, m, r)) s.guts = 1;
				else { y.hp = 0; s.kill = 1; }
			}
			if (!UnitAilmentHeld(y) && !UnitHasPas(y, PAS_NOSLOW)) y.ct = max(0, y.ct - (160 + (s.crit ? 80 : 0)));
		}
		out[n++] = s;
	}
	return n;
}

// ============================================================ 戦闘全体
void Battle::RebuildOcc()
{
	memset(occ, 0, sizeof(occ));
	for (int i = 0; i < nUnits; i++) {
		if (!u[i].alive) continue;
		int s = UnitSpan(u[i]);
		for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++) {
			int x = u[i].x + dx, z = u[i].z + dz;
			if (!Map::In(x, z)) continue;
			int L = u[i].deck;
			if (L < 0 || L >= LAY_N) L = 0;
			if (occ[L][z][x] != 0) continue;
			occ[L][z][x] = i + 1;
		}
	}
}

int Battle::AliveCount(int team) const
{
	int n = 0;
	for (int i = 0; i < nUnits; i++) if (u[i].alive && u[i].team == team) n++;
	return n;
}

static int Speed(const Battle& b, const Unit& u)
{
	int s = 10 + UnitStat(u, S_AGI);
	if (UnitHasSkill(u, SKL_P_SWIFT)) s = s * 12 / 10;
	if (u.team == TEAM_ENEMY) {
		int np = 0, ne = 0;
		for (int i = 0; i < b.nUnits; i++) {
			if (!b.u[i].alive || b.u[i].alert != 1) continue;
			if (b.u[i].team == TEAM_PLAYER) np++;
			else ne++;
		}
		if (np > ne && ne > 0) {
			int t = 100 + (np - ne) * 3;
			if (t > 128) t = 128;
			s = s * t / 100;
		}
	}
	return s;
}

static int ReadyCt(const Unit* u, const int* ct, int i)
{
	return ct ? ct[i] : u[i].ct;
}

static int PickReady(const Unit* u, int n, int lastTeam, const int* ct, const BYTE* also)
{
	int bestP = -1, bestE = -1;
	for (int i = 0; i < n; i++) {
		// alert 0 は未発見。行動順に入れない。also は「次の手番から入る」予告だけ。
		if (!u[i].alive || (u[i].alert != 1 && !(also && also[i]))) continue;
		int c = ReadyCt(u, ct, i);
		if (c < 1000) continue;
		int& best = (u[i].team == TEAM_PLAYER) ? bestP : bestE;
		if (best < 0) { best = i; continue; }
		int bc = ReadyCt(u, ct, best);
		if (c > bc || (c == bc && i < best)) best = i;
	}
	if (bestP >= 0 && bestE >= 0) {
		if (lastTeam == TEAM_PLAYER) return bestE;
		if (lastTeam == TEAM_ENEMY) return bestP;
		int pc = ReadyCt(u, ct, bestP), ec = ReadyCt(u, ct, bestE);
		return (ec >= pc) ? bestE : bestP;
	}
	return bestP >= 0 ? bestP : bestE;
}

int Battle::NextActor()
{
	for (int guard = 0; guard < 100000; guard++) {
		int best = PickReady(u, nUnits, lastActTeam, NULL, NULL);
		if (best >= 0) {
			lastActTeam = u[best].team;
			return best;
		}
		for (int i = 0; i < nUnits; i++) if (u[i].alive && u[i].alert == 1) u[i].ct += Speed(*this, u[i]);
	}
	return -1;
}

static int TurnJoinGoal(const Battle& b, const int* ct, int ui, const BYTE* also);

int Battle::PredictOrder(int* out, int n, int acting, int cost, int* insertAt) const
{
	int ct[MAX_UNITS];
	for (int i = 0; i < nUnits; i++) ct[i] = u[i].ct;
	int last = lastActTeam;
	if (acting >= 0 && acting < nUnits) {
		ct[acting] = max(0, ct[acting] - cost);
		last = u[acting].team;
	}
	if (insertAt) *insertAt = -1;
	// 気づいた敵（alert 2）は次の手番の直前に順へ入る。予告も同じ位置にする。未発見は入れない。
	BYTE also[MAX_UNITS];
	memset(also, 0, sizeof(also));
	for (int i = 0; i < nUnits; i++) {
		if (!u[i].alive || u[i].team != TEAM_ENEMY || u[i].alert != 2) continue;
		int goal = TurnJoinGoal(*this, ct, i, also);
		if (goal >= 0 && ct[i] < goal) ct[i] = goal;
		also[i] = 1;
	}
	int got = 0;
	for (int guard = 0; guard < 100000 && got < n; guard++) {
		int best = PickReady(u, nUnits, last, ct, also);
		if (best >= 0) {
			if (insertAt && *insertAt < 0 && best == acting) *insertAt = got;
			out[got++] = best;
			ct[best] -= 1000;
			last = u[best].team;
			continue;
		}
		bool any = false;
		for (int i = 0; i < nUnits; i++) if (u[i].alive && (u[i].alert == 1 || also[i])) { ct[i] += Speed(*this, u[i]); any = true; }
		if (!any) break;
	}
	return got;
}

static int PickPool(int pool, int stage, Rng& r)
{
	int ids[IMP_CAP];
	int n = 0;
	int src = s_poolN[pool];
	for (int i = 0; i < src && n < IMP_CAP; i++) {
		int id = s_poolI[pool][i];
		const MonsterDef& d = MonsterInfo(id);
		if (d.minStage > stage) continue;
		if (d.maxStage > 0 && stage > d.maxStage) continue;
		ids[n++] = id;
	}
	if (n <= 0) return -1;
	return ids[r.Range(0, n - 1)];
}

static int PickClassic(int stage, Rng& r)
{
	int ids[M_COUNT];
	int n = 0, wsum = 0;
	for (int i = 0; i < M_COUNT; i++) {
		if (kMon[i].minStage > stage) continue;
		if (kMon[i].maxStage > 0 && stage > kMon[i].maxStage) continue;
		int w = kMon[i].weight > 0 ? kMon[i].weight : 1;
		ids[n++] = i;
		wsum += w;
	}
	if (n <= 0 || wsum <= 0) return M_SLIME;
	int roll = r.Range(0, wsum - 1);
	for (int i = 0; i < n; i++) {
		int w = kMon[ids[i]].weight > 0 ? kMon[ids[i]].weight : 1;
		if (roll < w) return ids[i];
		roll -= w;
	}
	return ids[0];
}

static int PickMonsterAt(int stage, Rng& r)
{
	int roll = r.Range(0, 99);
	int pool = POOL_FAN;
	if (stage < 25) {
		if (roll < 48) return PickClassic(stage, r);
		if (roll < 84) pool = POOL_FAN;
		else if (roll < 96) pool = POOL_BEAST;
		else pool = stage >= 10 ? POOL_FISH : POOL_FAN;
	} else {
		if (roll < 28) return PickClassic(stage, r);
		if (roll < 70) pool = POOL_FAN;
		else if (roll < 88) pool = POOL_BEAST;
		else pool = POOL_FISH;
	}
	int id = PickPool(pool, stage, r);
	if (id < 0) return PickClassic(stage, r);
	return id;
}

static int StageMod(int stage, int mod)
{
	if (mod < 1) mod = 1;
	int k = stage % mod;
	if (k < 1) k = mod;
	if (k > TAC_STAGE_MAX) k = TAC_STAGE_MAX;
	return k;
}

static int PickAnyFoe(Rng& r)
{
	EnsureMonsters();
	int ids[IMP_CAP + M_COUNT];
	int n = 0;
	int cap = IMP_CAP + M_COUNT;
	for (int i = 0; i < M_COUNT && n < cap; i++)
		if (kMon[i].weight > 0) ids[n++] = i;
	int nImp = kImportN < IMP_CAP ? kImportN : IMP_CAP;
	for (int i = 0; i < nImp && n < cap; i++) {
		if (wcsncmp(kImportName[i], L"Sum", 3) == 0 || ImportPool(kImportName[i]) == POOL_HUM) continue;
		if (s_monImp[i].weight <= 0) continue;
		ids[n++] = M_COUNT + i;
	}
	if (n <= 0) return M_SLIME;
	return ids[r.Range(0, n - 1)];
}

static int PickMonster(int stage, Rng& r)
{
	EnsureMonsters();
	if (stage >= TAC_STAGE_EX) {
		int mode = r.Range(0, 99);
		if (mode < 40) return PickMonsterAt(StageMod(stage, 10000), r);
		if (mode < 70) return PickMonsterAt(StageMod(stage, 100), r);
		return PickAnyFoe(r);
	}
	return PickMonsterAt(stage, r);
}

static int BossMonster(int stage, Rng& r)
{
	EnsureMonsters();
	if (stage >= TAC_STAGE_EX) {
		int mode = r.Range(0, 99);
		if (mode < 20) return M_DRAGON;
		if (mode < 50) return PickMonsterAt(StageMod(stage, 10000), r);
		if (mode < 75) return PickMonsterAt(StageMod(stage, 100), r);
		return PickAnyFoe(r);
	}
	if (stage >= 80 && (stage % 20) == 0) return M_DRAGON;
	if (stage >= 24 && r.Range(0, 3) == 0) return M_WYVERN;
	if (stage >= 16 && r.Range(0, 3) == 0) return M_GOLEM;
	if (stage >= 30 && r.Range(0, 2) == 0) return M_DARK_KNIGHT;
	int id = PickPool(POOL_FAN, stage, r);
	if (id >= 0 && r.Range(0, 1) == 0) return id;
	return PickClassic(stage, r);
}

static int OpenWidth(const Map& m, int x, int z)
{
	if (!Map::In(x, z) || !FootPassable(m.At(x, z).t)) return 0;
	int wx = 1, wz = 1;
	for (int i = 1; Map::In(x - i, z) && FootPassable(m.At(x - i, z).t); i++) wx++;
	for (int i = 1; Map::In(x + i, z) && FootPassable(m.At(x + i, z).t); i++) wx++;
	for (int i = 1; Map::In(x, z - i) && FootPassable(m.At(x, z - i).t); i++) wz++;
	for (int i = 1; Map::In(x, z + i) && FootPassable(m.At(x, z + i).t); i++) wz++;
	return wx < wz ? wx : wz;
}

static int FootRoom(const Map& m, int ax, int az, int s)
{
	if (s < 1) s = 1;
	int room = 99;
	for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++) {
		int w = OpenWidth(m, ax + dx, az + dz);
		if (w < room) room = w;
	}
	return room;
}

static bool FootFits(const Battle& b, int ui, int ax, int az)
{
	const Unit& u = b.u[ui];
	int s = UnitSpan(u);
	int mt = UnitMoveType(u);
	int jp = max(1, UnitJump(u));
	int hmin = 99, hmax = -99;
	for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++) {
		int x = ax + dx, z = az + dz;
		if (!Map::In(x, z)) return false;
		int o = b.UnitAt(x, z);
		if (o >= 0 && o != ui) return false;
		const Cell& c = b.map.At(x, z);
		if (mt == MV_FLY) {
			if (!TerrainInfo(c.t).flyOk) return false;
		} else {
			if (TerrainInfo(c.t).costFoot >= 99) return false;
			int h = (int)c.h;
			if (h < hmin) hmin = h;
			if (h > hmax) hmax = h;
		}
	}
	if (mt != MV_FLY && hmax - hmin > jp) return false;
	if (s > 1 && mt != MV_FLY && FootRoom(b.map, ax, az, s) < s) return false;
	return true;
}

static bool FootCanLeave(const Battle& b, int ui, int x, int z)
{
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	for (int k = 0; k < 4; k++)
		if (FootStep(b, ui, x, z, x + dx[k], z + dz[k], b.u[ui].deck, 0)) return true;
	return false;
}

static bool PlaceFoot(Battle& b, int ui, int gx, int gz)
{
	Unit& u = b.u[ui];
	int s = UnitSpan(u);
	int rad = s <= 1 ? 8 : 20;
	int bestLeave = 99999, lx = -1, lz = -1;
	int bestAny = 99999, ax = -1, az = -1;
	BOOL needStep = UnitMove(u) >= 1 && UnitMoveType(u) != MV_FLY;
	for (int dz = -rad; dz <= rad; dz++) for (int dx = -rad; dx <= rad; dx++) {
		int x = gx + dx, z = gz + dz;
		if (!FootFits(b, ui, x, z)) continue;
		int contains = (gx >= x && gx < x + s && gz >= z && gz < z + s) ? 0 : 1000;
		int rank = contains + abs(dx) + abs(dz);
		if (s > 1) rank += (FootRoom(b.map, x, z, s) < s + 1) ? 4 : 0;
		if (rank < bestAny) { bestAny = rank; ax = x; az = z; }
		if (!needStep || FootCanLeave(b, ui, x, z)) {
			if (rank < bestLeave) { bestLeave = rank; lx = x; lz = z; }
		}
	}
	int bx = lx >= 0 ? lx : ax;
	int bz = lx >= 0 ? lz : az;
	if (bx < 0) return false;
	u.x = bx;
	u.z = bz;
	return true;
}

static void StampWalk(Cell& c, int h)
{
	if (c.t == T_VOID || c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM
		|| c.t == T_MOUNTAIN || c.t == T_ROCK || c.t == T_DOOR
		|| TerrainInfo(c.t).costFoot >= 99)
		c.t = T_PLAIN;
	c.h = (BYTE)Clampi(h, 1, MAXH);
	c.prop = PK_NONE;
}

static bool RelocateWide(Battle& b, int ui)
{
	Unit& u = b.u[ui];
	int s = UnitSpan(u);
	if (s <= 1) return FootCanLeave(b, ui, u.x, u.z);
	int ox = u.x, oz = u.z;
	int px = b.map.nP > 0 ? b.map.pStartX[0] : ox;
	int pz = b.map.nP > 0 ? b.map.pStartZ[0] : oz;
	u.x = -1; u.z = -1;
	b.RebuildOcc();
	int best = 99999, bx = -1, bz = -1;
	int rad = 22;
	for (int dz = -rad; dz <= rad; dz++) for (int dx = -rad; dx <= rad; dx++) {
		int x = ox + dx, z = oz + dz;
		if (!FootFits(b, ui, x, z)) continue;
		if (FootRoom(b.map, x, z, s) < s) continue;
		if (!FootCanLeave(b, ui, x, z)) continue;
		int rank = abs(dx) + abs(dz) * 2 + Dist(x, z, px, pz);
		if (rank < best) { best = rank; bx = x; bz = z; }
	}
	if (bx < 0) {
		u.x = ox; u.z = oz;
		b.RebuildOcc();
		return false;
	}
	u.x = bx;
	u.z = bz;
	b.RebuildOcc();
	return true;
}

// 岩と段差に挟まれて一歩も出られない大型を、味方の方向へ足跡幅の坂で出す。
static void CarveFootExit(Battle& b, int ui)
{
	Unit& u = b.u[ui];
	int s = UnitSpan(u);
	int tx = u.x, tz = u.z, best = 99999;
	for (int i = 0; i < b.nUnits; i++) {
		if (!b.u[i].alive || b.u[i].team != TEAM_PLAYER) continue;
		int d = abs(u.x - b.u[i].x) + abs(u.z - b.u[i].z);
		if (d < best) { best = d; tx = b.u[i].x; tz = b.u[i].z; }
	}
	int h = max(1, (int)b.map.At(u.x, u.z).h);
	int x = u.x, z = u.z;
	for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++) {
		int cx = x + dx, cz = z + dz;
		if (!Map::In(cx, cz)) continue;
		int o = b.UnitAt(cx, cz);
		if (o >= 0 && o != ui) continue;
		StampWalk(b.map.c[cz][cx], h);
	}
	for (int step = 0; step < 10 && (x != tx || z != tz); step++) {
		int nx = x, nz = z;
		if (abs(tx - x) >= abs(tz - z) && tx != x) nx += (tx > x ? 1 : -1);
		else if (tz != z) nz += (tz > z ? 1 : -1);
		else break;
		int nh = h;
		if (Map::In(nx, nz) && b.map.c[nz][nx].t != T_VOID) nh = (int)b.map.c[nz][nx].h;
		if (nh > h + 1) nh = h + 1;
		if (nh < h - 1) nh = h - 1;
		if (nh < 1) nh = 1;
		for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++) {
			int cx = nx + dx, cz = nz + dz;
			if (!Map::In(cx, cz)) continue;
			int o = b.UnitAt(cx, cz);
			if (o >= 0 && o != ui) continue;
			StampWalk(b.map.c[cz][cx], nh);
		}
		x = nx; z = nz; h = nh;
		if (step >= 2 && FootCanLeave(b, ui, u.x, u.z)) break;
	}
}

static void RescueStuckFeet(Battle& b)
{
	for (int i = 0; i < b.nUnits; i++) {
		Unit& u = b.u[i];
		if (!u.alive || u.team != TEAM_ENEMY) continue;
		if (UnitMove(u) < 1 || UnitMoveType(u) == MV_FLY) continue;
		int s = UnitSpan(u);
		BOOL cork = s > 1 && FootRoom(b.map, u.x, u.z, s) < s;
		if (!cork && FootCanLeave(b, i, u.x, u.z)) continue;
		if (s > 1 && RelocateWide(b, i)) continue;
		int gx = u.x, gz = u.z;
		u.x = -1; u.z = -1;
		b.RebuildOcc();
		if (!PlaceFoot(b, i, gx, gz) || !FootCanLeave(b, i, u.x, u.z)) {
			if (u.x < 0) { u.x = gx; u.z = gz; }
			b.RebuildOcc();
			if (s > 1 && RelocateWide(b, i)) continue;
			if (s > 1) {
				u.span = 1;
				b.RebuildOcc();
				if (PlaceFoot(b, i, gx, gz) && FootCanLeave(b, i, u.x, u.z)) continue;
				u.span = s;
				b.RebuildOcc();
			}
			CarveFootExit(b, i);
		}
		b.RebuildOcc();
	}
}

void SetupBattle(Battle& b, int stage, DWORD seed, const CharData* party, const int* rosterIdx, int nParty, int theme)
{
	memset(&b, 0, sizeof(b));
	stage = Clampi(stage, 1, TAC_STAGE_ABS);
	b.stage = stage;
	b.lastActTeam = -1;
	GenerateMap(b.map, stage, seed, theme);
	int wx = WX_CLEAR;
	int hour = HR_NOON;
	{
		int th = b.map.theme;
		unsigned w = seed % 20u;
		if (th == TH_SNOW) {
			if (w < 11) wx = WX_SNOW;
			else if (w < 14) wx = WX_HAIL;
			else if (w < 16) wx = WX_FOG;
		} else if (th == TH_VOLCANO) {
			if (w < 10) wx = WX_ASH;
			else if (w < 13) wx = WX_HEAT;
		} else if (th == TH_DESERT) {
			if (w < 8) wx = WX_SAND;
			else if (w < 11) wx = WX_HEAT;
		} else if (th == TH_FOREST || th == TH_PLAINS) {
			if (w < 4) wx = WX_RAIN;
			else if (w < 6) wx = WX_STORM;
			else if (w < 8) wx = WX_PETAL;
			else if (w < 10) wx = WX_FOG;
		} else if (th == TH_SWAMP) {
			if (w < 6) wx = WX_FOG;
			else if (w < 9) wx = WX_RAIN;
		} else if (w < 4) wx = WX_RAIN;
		else if (w < 6) wx = WX_STORM;
		else if (w < 8) wx = WX_FOG;
		unsigned hs = (seed / 20u) % 20u;
		if (hs == 0) hour = HR_DAWN;
		else if (hs == 1) hour = HR_MORNING;
		else if (hs == 2) hour = HR_DUSK;
		else if (hs == 3) hour = HR_NIGHT;
		else if (hs == 4) hour = HR_DEEP;
		else if (hs == 5) hour = HR_ECLIPSE;
		else if (hs == 6) hour = HR_DREAM;
		else if (hs == 7) hour = HR_STAR;
		else if (hs == 8) hour = HR_MOON;
	}
	b.weather = TacPackSky(wx, hour);
	Rng r(seed ^ 0xA5A5A5A5u);
	int n = 0;
	const float faceToE = atan2f((float)(b.map.eStartX[0] - b.map.pStartX[0]), (float)(b.map.eStartZ[0] - b.map.pStartZ[0]));
	for (int i = 0; i < nParty && i < MAX_PARTY; i++) {
		Unit& u = b.u[n];
		memset(&u, 0, sizeof(u));
		u.alive = 1;
		u.team = TEAM_PLAYER;
		u.rosterIdx = rosterIdx[i];
		u.monster = -1;
		u.ch = party[i];
		CharGrantReached(u.ch);
		u.hp = u.ch.stat[S_HP];
		u.mp = u.ch.stat[S_MP];
		CraftBindUnit(u);
		u.cp = 0;
		u.span = 1;
		u.x = b.map.pStartX[i]; u.z = b.map.pStartZ[i];
		u.face = faceToE;
		u.ct = r.Range(0, 420) + UnitStat(u, S_AGI) * 6;
		if (UnitHasPas(u, PAS_OPEN2)) u.ct += 400;
		else if (UnitHasPas(u, PAS_OPEN)) u.ct += 250;
		u.hpShow = (float)u.hp;
		u.grp = -1;
		u.alert = 1;
		n++;
	}
	b.nUnits = n;
	b.RebuildOcc();
	int kind = TAC_VAR_NONE;
	{
		unsigned r64 = seed % 64u;
		if ((seed % 16u) == 7u) kind = TAC_VAR_SWEEP;
		else if (r64 == 11u) kind = TAC_VAR_REMNANT;
		else if (r64 == 21u) kind = TAC_VAR_BOSS;
		else if (r64 == 29u) kind = TAC_VAR_SCAR;
		else if (r64 == 13u) kind = TAC_VAR_MIST;
		else if (r64 == 37u) kind = TAC_VAR_RAID;
		else if (r64 == 45u) kind = TAC_VAR_AMBUSH;
		else if (r64 == 53u) kind = TAC_VAR_TWIN;
		else if (r64 == 57u) kind = TAC_VAR_VAULT;
		else if (r64 == 61u) kind = TAC_VAR_BLESS;
	}
	if (b.map.nGrp <= 0) kind = TAC_VAR_NONE;
	if (kind == TAC_VAR_BOSS) {
		int has = 0;
		for (int g = 0; g < b.map.nGrp; g++) if (b.map.grp[g].boss && b.map.grp[g].n > 0) has = 1;
		if (!has) kind = TAC_VAR_NONE;
	}
	if (kind == TAC_VAR_SWEEP) {
		for (int g = 0; g < b.map.nGrp; g++) b.map.grp[g].boss = 0;
	} else if (kind == TAC_VAR_REMNANT) {
		int kept = 0;
		for (int g = 0; g < b.map.nGrp; g++) {
			MapGroup& grp = b.map.grp[g];
			if (grp.n <= 0) continue;
			if (grp.boss) {
				grp.n = grp.n > 4 ? 2 : 1;
				kept++;
				continue;
			}
			if (((seed + (unsigned)g * 17u) % 3u) != 0u) { grp.n = 0; continue; }
			grp.n = 1;
			kept++;
		}
		if (kept <= 0 && b.map.nGrp > 0) b.map.grp[0].n = 1;
	} else if (kind == TAC_VAR_BOSS) {
		for (int g = 0; g < b.map.nGrp; g++) {
			MapGroup& grp = b.map.grp[g];
			grp.n = (grp.boss && grp.n > 0) ? 1 : 0;
		}
	} else if (kind == TAC_VAR_TWIN) {
		int bosses = 0, best = -1, bestN = 0;
		for (int g = 0; g < b.map.nGrp; g++) {
			MapGroup& grp = b.map.grp[g];
			if (grp.boss && grp.n > 0) bosses++;
			else if (grp.n > bestN) { bestN = grp.n; best = g; }
		}
		if (bosses >= 1 && best >= 0) b.map.grp[best].boss = 1;
		else kind = TAC_VAR_NONE;
	}
	if (kind == TAC_VAR_MIST) b.weather = TacPackSky(WX_FOG, TacHour(b.weather));
	else if (kind == TAC_VAR_RAID) b.weather = TacPackSky(TacWx(b.weather), HR_NIGHT);
	if (kind == TAC_VAR_BLESS) {
		for (int i = 0; i < n; i++) {
			b.u[i].buffDef = 4;
			b.u[i].buffAgi = 3;
		}
	}
	if (kind == TAC_VAR_VAULT) {
		int added = 0;
		for (int z = 2; z < MAPN - 2 && added < 4; z++) for (int x = 2; x < MAPN - 2 && added < 4; x++) {
			Cell& c = b.map.At(x, z);
			if (c.gimm || c.t == T_VOID || c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM) continue;
			if (((unsigned)(x * 13 + z * 7) + seed) % 37u) continue;
			int closeBy = 0;
			for (int p = 0; p < nParty && p < MAX_PARTY; p++)
				if (Dist(x, z, b.map.pStartX[p], b.map.pStartZ[p]) < 6) closeBy = 1;
			if (closeBy) continue;
			c.gimm = GM_CHEST;
			c.gparam = (BYTE)((x + z) % 3);
			c.prop = (BYTE)(b.map.theme >= TH_SEABED ? PK_KIT : PK_CRATE);
			added++;
		}
	}
	b.sweep = kind;
	int baseLv = StageEnemyLevel(stage);
	for (int g = 0; g < b.map.nGrp && n < MAX_UNITS; g++) {
		const MapGroup& grp = b.map.grp[g];
		int mon = grp.boss ? BossMonster(stage, r) : PickMonster(stage, r);
		for (int k = 0; k < grp.n && n < MAX_UNITS; k++) {
			Unit& u = b.u[n];
			bool boss = grp.boss && k == 0;
			int lv = baseLv + r.Range(-1, 1) + (boss ? 3 : 0) + (grp.boss ? 1 : 0);
			if (stage <= 2) lv = max(1, lv - 1);
			int use = k == 0 ? mon : PickMonster(stage, r);
			int room = OpenWidth(b.map, grp.x[k], grp.z[k]);
			if (room < 2) {
				int slim = -1;
				for (int t = 0; t < 10; t++) {
					int id = PickMonster(stage, r);
					if (MonsterSpan(MonsterInfo(id), 0) <= 1) { slim = id; break; }
				}
				if (slim < 0) slim = PickClassic(stage, r);
				if (!boss) use = slim;
			}
			MakeEnemy(u, use, lv, boss ? 1 : 0, r);
			if (UnitSpan(u) > 1 && room < 2 && !boss) u.span = 1;
			u.face = faceToE + 3.14159f;
			u.ct = r.Range(80, 480) + UnitStat(u, S_AGI) * 6;
			u.hpShow = (float)u.hp;
			u.grp = g;
			u.alert = 0;
			if (!PlaceFoot(b, n, grp.x[k], grp.z[k])) {
				if (!boss) continue;
				int s = max(1, UnitSpan(u));
				int gx = Clampi((int)grp.x[k], 1, MAPN - s - 1);
				int gz = Clampi((int)grp.z[k], 1, MAPN - s - 1);
				u.x = gx;
				u.z = gz;
				n++;
				b.nUnits = n;
				b.RebuildOcc();
				if (!RelocateWide(b, n - 1)) {
					u.span = 1;
					if (!PlaceFoot(b, n - 1, gx, gz)) { u.x = gx; u.z = gz; }
				}
				b.RebuildOcc();
				continue;
			}
			if (!boss) {
				const Cell& cell = b.map.At(u.x, u.z);
				int nl = CellLayN(cell);
				if (nl >= 2) {
					int L = (k + g) % nl;
					if (TerrainInfo(LayerTr(cell, L)).costFoot < 99) u.deck = L;
				}
			}
			n++;
			b.nUnits = n;
			b.RebuildOcc();
		}
	}
	b.nUnits = n;
	b.RebuildOcc();
	if (b.sweep != TAC_VAR_SWEEP) {
		int hasBoss = 0, farUi = -1, farD = -1;
		int px = b.map.nP > 0 ? b.map.pStartX[0] : 0;
		int pz = b.map.nP > 0 ? b.map.pStartZ[0] : 0;
		for (int i = 0; i < b.nUnits; i++) {
			Unit& eu = b.u[i];
			if (!eu.alive || eu.team != TEAM_ENEMY) continue;
			if (eu.boss) hasBoss = 1;
			int d = abs(eu.x - px) + abs(eu.z - pz);
			if (d > farD) { farD = d; farUi = i; }
		}
		if (!hasBoss && farUi >= 0) {
			Unit& eu = b.u[farUi];
			int x = eu.x, z = eu.z, grp = eu.grp, alert = eu.alert, ct = eu.ct;
			float face = eu.face;
			int lv = max(1, eu.ch.lv + 3);
			MakeEnemy(eu, BossMonster(b.stage, r), lv, 1, r);
			eu.face = face;
			eu.ct = ct;
			eu.grp = grp;
			eu.alert = alert;
			eu.hpShow = (float)eu.hp;
			eu.x = x;
			eu.z = z;
			b.RebuildOcc();
			if (!FootFits(b, farUi, eu.x, eu.z)) {
				int s = max(1, UnitSpan(eu));
				int gx = Clampi(x, 1, MAPN - s - 1);
				int gz = Clampi(z, 1, MAPN - s - 1);
				int h = 2;
				if (Map::In(gx, gz) && b.map.c[gz][gx].h > 0) h = (int)b.map.c[gz][gx].h;
				for (int dz = -1; dz <= s; dz++) for (int dx = -1; dx <= s; dx++)
					if (Map::In(gx + dx, gz + dz)) StampWalk(b.map.c[gz + dz][gx + dx], h);
				eu.x = gx;
				eu.z = gz;
			}
		}
	}
	RescueStuckFeet(b);
	if (b.sweep == TAC_VAR_SCAR) {
		for (int i = 0; i < b.nUnits; i++) {
			Unit& u = b.u[i];
			if (!u.alive || u.hp <= 1) continue;
			u.hp = max(1, u.hp / 2);
			u.hpShow = (float)u.hp;
		}
	}
	if (b.sweep == TAC_VAR_AMBUSH || b.sweep == TAC_VAR_RAID) {
		for (int i = 0; i < b.nUnits; i++) {
			Unit& u = b.u[i];
			if (!u.alive || u.team != TEAM_ENEMY) continue;
			if (b.sweep == TAC_VAR_AMBUSH) { u.alert = 1; continue; }
			int closeBy = 0;
			for (int p = 0; p < b.nUnits; p++) {
				if (!b.u[p].alive || b.u[p].team != TEAM_PLAYER) continue;
				if (Dist(u.x, u.z, b.u[p].x, b.u[p].z) <= 22) closeBy = 1;
			}
			if (closeBy) u.alert = 1;
		}
	}
	// 未発見のままの敵は、最初の敵味方の混成順に入れない。時計も持たせない。
	// 奇襲は全員、夜襲は近くの敵だけ、上で既に alert 1。接近したあとに順へ入る。
	for (int i = 0; i < b.nUnits; i++) {
		Unit& u = b.u[i];
		if (!u.alive || u.team != TEAM_ENEMY || u.alert != 0) continue;
		u.ct = 0;
	}
	// どの特殊戦でも、ボス以外の雑魚を独立抽選。1体とは限らない。
	for (int i = 0; i < b.nUnits; i++) {
		Unit& u = b.u[i];
		if (!u.alive || u.team != TEAM_ENEMY || u.boss) continue;
		if (r.Chance(6)) u.lucky = 1;
	}
	b.stampede = 0;
	b.stampedeLeft = 0;
	b.stampedeLine = 0;
	b.stampedeShow = 0;
	int foes = b.AliveCount(TEAM_ENEMY);
	// 以前は 8 回に 1 で、波が 1 か 2。続くと重いので 16 回に 1、波は 1。
	if (b.sweep == TAC_VAR_NONE && foes >= 8 && (seed % 16u) == 3u) {
		int line = foes / 4;
		if (line < 2) line = 2;
		if (line < foes) {
			b.stampede = 1;
			b.stampedeLeft = 1;
			b.stampedeLine = line;
		}
	}
}

static int StampedeSpot(Battle& b, int ui, int ax, int az, int ally, int radMax, int needRange)
{
	int bestX = -1, bestZ = -1, bestD = 99;
	for (int rad = 1; rad <= radMax; rad++) {
		int found = 0;
		for (int dz = -rad; dz <= rad; dz++) for (int dx = -rad; dx <= rad; dx++) {
			if (max(abs(dx), abs(dz)) != rad) continue;
			int x = ax + dx, z = az + dz;
			if (!FootFits(b, ui, x, z)) continue;
			if (needRange) {
				const Unit& u = b.u[ui];
				int rmin = UnitRangeMin(u);
				int rmax = max(1, UnitRangeMax(u));
				if (!InRangeOfUnit(b, u, x, z, rmin, rmax, b.u[ally], true)) continue;
			}
			int d = abs(dx) + abs(dz);
			if (d < bestD) { bestD = d; bestX = x; bestZ = z; found = 1; }
		}
		if (found && needRange) break;
	}
	if (bestX < 0) return 0;
	b.u[ui].x = bestX;
	b.u[ui].z = bestZ;
	return 1;
}

int SpawnStampede(Battle& b, Rng& r, int prefer, int* outIdx, int cap)
{
	if (!b.stampede || b.stampedeLeft <= 0 || !outIdx || cap <= 0) return 0;
	int alive = b.AliveCount(TEAM_ENEMY);
	if (alive <= 0 || alive > b.stampedeLine) return 0;
	int allies[MAX_PARTY];
	int na = 0;
	for (int i = 0; i < b.nUnits && na < MAX_PARTY; i++)
		if (b.u[i].alive && b.u[i].team == TEAM_PLAYER) allies[na++] = i;
	if (na <= 0) return 0;
	int start = 0;
	for (int i = 0; i < na; i++) if (allies[i] == prefer) { start = i; break; }
	int want = 3 + r.Range(0, 1);
	if (want > cap) want = cap;
	int room = MAX_UNITS - b.nUnits;
	if (want > room) want = room;
	if (want <= 0) return 0;
	int top = 1000;
	for (int i = 0; i < b.nUnits; i++)
		if (b.u[i].alive && b.u[i].alert == 1 && b.u[i].ct > top) top = b.u[i].ct;
	int baseLv = StageEnemyLevel(b.stage);
	int n = 0;
	for (int k = 0; k < want; k++) {
		int ally = allies[(start + k) % na];
		int mon = -1;
		for (int t = 0; t < 8; t++) {
			int id = PickMonster(b.stage, r);
			if (MonsterSpan(MonsterInfo(id), 0) <= 1) { mon = id; break; }
		}
		if (mon < 0) mon = PickClassic(b.stage, r);
		int ui = b.nUnits;
		Unit& u = b.u[ui];
		int lv = baseLv + r.Range(-1, 1);
		if (b.stage <= 2) lv = max(1, lv - 1);
		MakeEnemy(u, mon, lv, 0, r);
		if (r.Chance(6)) u.lucky = 1;
		u.span = 1;
		u.x = -8;
		u.z = -8;
		u.alert = 1;
		u.grp = -1;
		u.rush = 1;
		u.ct = top + 240 + (want - k) * 40;
		b.RebuildOcc();
		int ax = b.u[ally].x, az = b.u[ally].z;
		if (!StampedeSpot(b, ui, ax, az, ally, 4, 1) && !StampedeSpot(b, ui, ax, az, ally, 5, 0)) {
			memset(&u, 0, sizeof(u));
			continue;
		}
		u.face = atan2f((float)(b.u[ally].x - u.x), (float)(b.u[ally].z - u.z));
		u.hpShow = (float)u.hp;
		b.nUnits = ui + 1;
		b.RebuildOcc();
		outIdx[n++] = ui;
	}
	if (n <= 0) return 0;
	b.stampedeLeft--;
	b.stampedeShow = 1;
	return n;
}

void FinishAction(Battle& b, int ui, int moved, int acted)
{
	Unit& u = b.u[ui];
	int cost = (moved && acted) ? 1000 : (moved || acted) ? 800 : 600;
	if (u.rush) {
		u.rush = 0;
		u.ct = max(0, 1000 - cost);
	} else u.ct = max(0, u.ct - cost);
	b.turnCount++;
}

int TurnStartEffects(Battle& b, int ui, int* healOut, int* poisonOut, int* burnOut, int* bleedOut)
{
	Unit& u = b.u[ui];
	*healOut = 0; *poisonOut = 0;
	if (burnOut) *burnOut = 0;
	if (bleedOut) *bleedOut = 0;
	int regenPct = 0;
	if (UnitHasPas(u, PAS_REGEN2)) regenPct = 15;
	else if (UnitHasSkill(u, SKL_P_REGEN)) regenPct = 10;
	else if (UnitHasPas(u, PAS_REGEN)) regenPct = 8;
	if (regenPct && u.hp < u.ch.stat[S_HP]) {
		int h = max(1, u.ch.stat[S_HP] * regenPct / 100);
		int before = u.hp;
		u.hp = min(u.ch.stat[S_HP], u.hp + h);
		*healOut = u.hp - before;
	}
	{
		int rcv = UnitStat(u, S_RCV);
		if (rcv > 0 && u.hp < u.ch.stat[S_HP]) {
			int h = rcv;
			int before = u.hp;
			u.hp = min(u.ch.stat[S_HP], u.hp + h);
			*healOut += u.hp - before;
		}
		int mpr = UnitStat(u, S_MPR);
		if (UnitHasPas(u, PAS_MP2)) mpr += 8;
		else if (UnitHasPas(u, PAS_MP)) mpr += 4;
		if (UnitHasPas(u, PAS_WAKE)) {
			if (u.sleep > 0) u.sleep--;
			if (u.stun > 0) u.stun--;
			if (u.freeze > 0) u.freeze--;
		}
		if (mpr > 0 && u.ch.stat[S_MP] > 0) u.mp = min(u.ch.stat[S_MP], u.mp + mpr);
	}
	if (UnitHasPas(u, PAS_NOPOISON)) u.poison = 0;
	if (UnitHasPas(u, PAS_NOSLOW)) u.slow = 0;
	if (UnitHasPas(u, PAS_NODOWN)) u.defDown = 0;
	if (UnitHasPas(u, PAS_NOSLEEP)) { u.sleep = 0; u.freeze = 0; }
	if (UnitHasPas(u, PAS_NOCONFUSE)) u.confuse = 0;
	if (UnitHasPas(u, PAS_NOSTUN)) u.stun = 0;
	// 毒・炎上・裂傷は 0 まで落とす。倒したあとの残りはここでは進めない。
	auto dot = [&](int& turns, int pct, int* out) {
		if (turns <= 0 || u.hp <= 0) return;
		int d = max(1, u.ch.stat[S_HP] * pct / 100);
		if (d > u.hp) d = u.hp;
		u.hp -= d;
		if (out) *out = d;
		turns--;
	};
	dot(u.poison, 5, poisonOut);
	dot(u.burn, 6, burnOut);
	dot(u.bleed, 4, bleedOut);
	if (u.blind > 0) u.blind--;
	if (u.silence > 0) u.silence--;
	if (u.slow > 0) u.slow--;
	if (u.buffDef > 0) u.buffDef--;
	if (u.buffAgi > 0) u.buffAgi--;
	if (u.buffAtk > 0) u.buffAtk--;
	if (u.defDown > 0) u.defDown--;
	if (u.mulTurns > 0) u.mulTurns--;
	return 0;
}

static int TurnJoinGoal(const Battle& b, const int* ct, int ui, const BYTE* also)
{
	if (ui < 0 || ui >= b.nUnits) return -1;
	int top = 0, n = 0;
	for (int i = 0; i < b.nUnits; i++) {
		if (i == ui || !b.u[i].alive) continue;
		if (b.u[i].alert != 1 && !(also && also[i])) continue;
		int c = ct ? ct[i] : b.u[i].ct;
		if (c > top) top = c;
		n++;
	}
	if (n <= 0) return -1;
	int spd = Speed(b, b.u[ui]);
	int goal;
	if (top >= 1000) {
		goal = 1000 + (spd % 37);
		if (goal > top - 1) goal = top > 1000 ? top - 1 : 1000;
	} else {
		goal = top - spd;
		if (goal < 0) goal = 0;
		if (goal < top * 3 / 4) goal = top * 3 / 4;
	}
	return goal;
}

void JoinTurnClock(Battle& b, int ui)
{
	if (ui < 0 || ui >= b.nUnits) return;
	Unit& u = b.u[ui];
	if (!u.alive || u.alert != 1) return;
	int goal = TurnJoinGoal(b, NULL, ui, NULL);
	if (goal < 0) return;
	if (u.ct < goal) u.ct = goal;
}

void WakeGroup(Battle& b, int grp)
{
	if (grp < 0) return;
	for (int i = 0; i < b.nUnits; i++) {
		if (!b.u[i].alive || b.u[i].grp != grp) continue;
		int wake = b.u[i].alert != 1;
		b.u[i].alert = 1;
		if (wake) JoinTurnClock(b, i);
	}
}

static void MarkGroup(Battle& b, int grp, int immediate, int& newly)
{
	if (grp < 0 || grp >= MAX_GROUPS) return;
	if (immediate) { WakeGroup(b, grp); return; }
	for (int i = 0; i < b.nUnits; i++) {
		Unit& u = b.u[i];
		if (!u.alive || u.grp != grp || u.alert != 0) continue;
		u.alert = 2;
		newly++;
	}
}

// 障害物を避けた歩数。山や段差で通れないマスは数えず、回り込んだ長さを返す。
static void ScoutFlood(const Battle& b, int ei, int limit, short dist[MAPN][MAPN])
{
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) dist[z][x] = -1;
	const Unit& u = b.u[ei];
	if (!Map::In(u.x, u.z) || limit < 0) return;
	static int qx[MAPN * MAPN], qz[MAPN * MAPN];
	int qh = 0, qt = 0;
	dist[u.z][u.x] = 0;
	qx[qt] = u.x; qz[qt] = u.z; qt++;
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	while (qh < qt) {
		int x = qx[qh], z = qz[qh]; qh++;
		int c0 = dist[z][x];
		if (c0 >= limit) continue;
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz) || dist[nz][nx] >= 0) continue;
			if (!FootTerrain(b, ei, x, z, nx, nz)) continue;
			dist[nz][nx] = (short)(c0 + 1);
			qx[qt] = nx; qz[qt] = nz; qt++;
		}
	}
}

// 低い側から高い側は見えない。山や尾根で視線が切れるときも見えない。
static bool ScoutSee(const Map& m, int ex, int ez, int px, int pz, int eLayer, int pLayer)
{
	if (!Map::In(ex, ez) || !Map::In(px, pz)) return false;
	int eh = LayerHt(m.At(ex, ez), LayerOn(m.At(ex, ez), eLayer) ? eLayer : 0);
	int ph = LayerHt(m.At(px, pz), LayerOn(m.At(px, pz), pLayer) ? pLayer : 0);
	if (eh + 2 <= ph) return false;
	return LineOfSightY(m, ex, ez, eh * kHStep + .6f, px, pz, ph * kHStep + .6f);
}

static bool UnitCanScout(const Battle& b, int ei, int pi, int limit, const short dist[MAPN][MAPN])
{
	const Unit& e = b.u[ei];
	const Unit& p = b.u[pi];
	int se = max(1, UnitSpan(e)), spn = max(1, UnitSpan(p));
	bool reach = false;
	for (int z = 0; z < spn && !reach; z++) for (int x = 0; x < spn; x++) {
		int cx = p.x + x, cz = p.z + z;
		if (Map::In(cx, cz) && dist[cz][cx] >= 0 && dist[cz][cx] <= limit) reach = true;
	}
	if (!reach) return false;
	for (int ez = 0; ez < se; ez++) for (int ex = 0; ex < se; ex++)
		for (int pz = 0; pz < spn; pz++) for (int px = 0; px < spn; px++)
			if (ScoutSee(b.map, e.x + ex, e.z + ez, p.x + px, p.z + pz, e.deck, p.deck)) return true;
	return false;
}

int WakeNearPlayers(Battle& b, int rad, int immediate)
{
	BYTE wake[MAX_GROUPS];
	memset(wake, 0, sizeof(wake));
	int newly = 0;
	static short dist[MAPN][MAPN];
	for (int e = 0; e < b.nUnits; e++) {
		const Unit& u = b.u[e];
		if (!u.alive || u.team != TEAM_ENEMY || u.alert || u.grp < 0 || u.grp >= MAX_GROUPS) continue;
		if (wake[u.grp]) continue;
		ScoutFlood(b, e, rad, dist);
		for (int p = 0; p < b.nUnits; p++) {
			if (!b.u[p].alive || b.u[p].team != TEAM_PLAYER) continue;
			if (!UnitCanScout(b, e, p, rad, dist)) continue;
			wake[u.grp] = 1;
			break;
		}
	}
	for (int g = 0; g < MAX_GROUPS; g++) if (wake[g]) MarkGroup(b, g, immediate, newly);
	return newly;
}

int LavaHurt(Battle& b, int ui)
{
	if (ui < 0 || ui >= b.nUnits) return 0;
	Unit& u = b.u[ui];
	if (!u.alive || u.hp <= 0 || UnitMoveType(u) == MV_FLY) return 0;
	int s = UnitSpan(u);
	int on = 0;
	for (int dz = 0; dz < s && !on; dz++) for (int dx = 0; dx < s; dx++) {
		int x = u.x + dx, z = u.z + dz;
		if (Map::In(x, z) && b.map.At(x, z).t == T_LAVA) on = 1;
	}
	if (!on) return 0;
	int dmg = max(1, u.ch.stat[S_HP] * 12 / 100);
	u.hp -= dmg;
	if (u.hp <= 0) { u.hp = 0; u.alive = 0; u.killed = 1; u.confuse = 0; u.stun = 0; }
	return dmg;
}

int ApplyTile(Battle& b, int ui)
{
	Unit& u = b.u[ui];
	if (!Map::In(u.x, u.z)) return 0;
	int hx = u.x, hz = u.z;
	int s = UnitSpan(u);
	bool got = false;
	for (int dz = 0; dz < s && !got; dz++) for (int dx = 0; dx < s && !got; dx++) {
		int x = u.x + dx, z = u.z + dz;
		if (!Map::In(x, z)) continue;
		if (b.map.At(x, z).gimm != GM_NONE) { hx = x; hz = z; got = true; }
	}
	const Cell& c = b.map.At(hx, hz);
	if (c.gimm == GM_HEAL) {
		int add = max(1, u.ch.stat[S_HP] * 30 / 100);
		u.hp = min(u.ch.stat[S_HP], u.hp + add);
		return GM_HEAL;
	}
	if (c.gimm == GM_MIGHT) {
		u.mulTurns = 3;
		return GM_MIGHT;
	}
	if (c.gimm == GM_CHEST) {
		bool thief = CanOpenDoor(u);
		int add = max(1, u.ch.stat[S_HP] * (thief ? 40 : 25) / 100);
		u.hp = min(u.ch.stat[S_HP], u.hp + add);
		if (thief || c.gparam == 2) u.mulTurns = max(u.mulTurns, 3);
		if (thief || c.gparam == 1) u.expGained += 12;
		b.map.At(hx, hz).gimm = GM_NONE;
		b.map.At(hx, hz).prop = PK_NONE;
		return GM_CHEST;
	}
	if (c.gimm == GM_WARP) {
		int ox = -1, oz = -1;
		for (int z = 0; z < MAPN && ox < 0; z++) for (int x = 0; x < MAPN; x++) {
			const Cell& o = b.map.At(x, z);
			if (o.gimm == GM_WARP && o.gparam == c.gparam && (x != hx || z != hz)) { ox = x; oz = z; break; }
		}
		if (ox < 0) return 0;
		if (s <= 1) {
			if (b.UnitAt(ox, oz) >= 0) return 0;
			u.x = ox; u.z = oz; u.deck = 0;
		} else {
			int ax = -1, az = -1, best = 999;
			for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++) {
				int px = ox - dx, pz = oz - dz;
				if (!FootFits(b, ui, px, pz)) continue;
				if (dx + dz < best) { best = dx + dz; ax = px; az = pz; }
			}
			if (ax < 0) return 0;
			u.x = ax; u.z = az; u.deck = 0;
		}
		b.RebuildOcc();
		return GM_WARP;
	}
	if (c.gimm == GM_LEVER && c.gparam) {
		int id = c.gparam;
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			Cell& o = b.map.At(x, z);
			if (o.t == T_DOOR && o.gimm != GM_FENCE && o.gparam == id) OpenDoor(b, x, z);
		}
		b.map.At(hx, hz).gimm = GM_NONE;
		b.map.At(hx, hz).prop = PK_NONE;
		return GM_LEVER;
	}
	if (c.gimm == GM_DROP && UnitMoveType(u) != MV_FLY) {
		Cell& cell = b.map.At(hx, hz);
		if (cell.var > 1) { cell.var--; return 0; }
		int ax = -1, az = -1;
		int sp = UnitSpan(u);
		static const int ddx[4] = { 1, -1, 0, 0 }, ddz[4] = { 0, 0, 1, -1 };
		for (int k = 0; k < 4 && ax < 0; k++) {
			int nx = u.x + ddx[k], nz = u.z + ddz[k];
			bool covers = false;
			for (int iz = 0; iz < sp && !covers; iz++) for (int ix = 0; ix < sp; ix++)
				if (nx + ix == hx && nz + iz == hz) covers = true;
			if (covers || !FootFits(b, ui, nx, nz)) continue;
			ax = nx; az = nz;
		}
		if (ax < 0) return 0;
		cell.t = T_CHASM;
		cell.h = 0;
		cell.gimm = GM_NONE;
		cell.var = 0;
		cell.prop = PK_NONE;
		int dmg = max(1, u.ch.stat[S_HP] * 18 / 100);
		u.hp -= dmg;
		if (u.hp <= 0) { u.hp = 0; u.alive = 0; u.killed = 1; u.confuse = 0; u.stun = 0; }
		else { u.x = ax; u.z = az; }
		b.RebuildOcc();
		return GM_DROP;
	}
	return 0;
}

bool CanOpenDoor(const Unit& u)
{
	int c = u.ch.cls;
	if (c == C_PHANTOM) return true;
	return ClassLine(c) == 3;
}

int AdjDoor(const Battle& b, int ui, int* ox, int* oz)
{
	const Unit& u = b.u[ui];
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	int n = 0;
	for (int k = 0; k < 4; k++) {
		int x = u.x + dx[k], z = u.z + dz[k];
		if (!Map::In(x, z) || b.map.At(x, z).t != T_DOOR) continue;
		if (n == 0 && ox && oz) { *ox = x; *oz = z; }
		n++;
	}
	return n;
}

void OpenDoor(Battle& b, int x, int z)
{
	if (!Map::In(x, z) || b.map.At(x, z).t != T_DOOR) return;
	Cell& c = b.map.At(x, z);
	c.t = T_ROAD;
	c.prop = PK_NONE;
	c.gimm = GM_NONE;
	c.gparam = 0;
}

bool CanTerrace(const Battle& b, int ui, int tx, int tz)
{
	if (ui < 0 || ui >= b.nUnits || !Map::In(tx, tz)) return false;
	const Unit& u = b.u[ui];
	if (!u.alive || u.team != TEAM_PLAYER) return false;
	if (Dist(u.x, u.z, tx, tz) != 1) return false;
	if (b.UnitAt(tx, tz) >= 0) return false;
	const Cell& c = b.map.At(tx, tz);
	if (c.t == T_VOID || c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM || c.t == T_DOOR) return false;
	int jump = max(1, UnitJump(u));
	int mt = UnitMoveType(u) == MV_FLY ? MV_FOOT : UnitMoveType(u);
	if (CanStep(b.map, u.x, u.z, tx, tz, mt, jump)) return false;
	bool wall = (c.t == T_MOUNTAIN || c.t == T_ROCK);
	bool cliff = abs((int)c.h - (int)b.map.At(u.x, u.z).h) > jump;
	return wall || cliff;
}

void DoTerrace(Battle& b, int ui, int tx, int tz)
{
	if (!CanTerrace(b, ui, tx, tz)) return;
	const Unit& u = b.u[ui];
	Cell& c = b.map.At(tx, tz);
	int sh = (int)b.map.At(u.x, u.z).h;
	if ((int)c.h > sh) { if (c.h > 1) c.h--; }
	else if ((int)c.h < sh) c.h++;
	if (c.h < 1) c.h = 1;
	int jump = max(1, UnitJump(u));
	if ((c.t == T_MOUNTAIN || c.t == T_ROCK) && abs((int)c.h - sh) <= jump) {
		c.t = T_HILL;
		c.prop = PK_NONE;
	}
}

bool CanShove(const Battle& b, int ui, int ti, int* outX, int* outZ)
{
	if (ui < 0 || ti < 0 || ui == ti) return false;
	const Unit& a = b.u[ui];
	const Unit& d = b.u[ti];
	if (!a.alive || !d.alive) return false;
	int s = UnitSpan(d);
	if (FootDist(a.x, a.z, UnitSpan(a), d.x, d.z, s) != 1) return false;
	if (d.team != a.team) {
		if (d.boss) return false;
		if (UnitStat(a, S_STR) + 1 < UnitStat(d, S_STR)) return false;
	}
	int pdx = 0, pdz = 0;
	int sa = UnitSpan(a);
	for (int az = 0; az < sa && pdx == 0 && pdz == 0; az++)
		for (int ax = 0; ax < sa && pdx == 0 && pdz == 0; ax++)
			for (int iz = 0; iz < s && pdx == 0 && pdz == 0; iz++)
				for (int ix = 0; ix < s; ix++) {
					int ddx = (d.x + ix) - (a.x + ax), ddz = (d.z + iz) - (a.z + az);
					if (abs(ddx) + abs(ddz) == 1) { pdx = ddx; pdz = ddz; }
				}
	if (pdx == 0 && pdz == 0) return false;
	int nx = d.x + pdx, nz = d.z + pdz;
	int fromL = LayerOn(b.map.At(d.x, d.z), d.deck) ? d.deck : 0;
	int lay = ChooseLayer(b.map.At(d.x, d.z), fromL, b.map.At(nx, nz), UnitJump(d), UnitMoveType(d));
	if (lay < 0) return false;
	for (int iz = 0; iz < s; iz++) for (int ix = 0; ix < s; ix++) {
		int x = nx + ix, z = nz + iz;
		if (!Map::In(x, z)) return false;
		const Cell& cell = b.map.At(x, z);
		int t = LayerOn(cell, lay) ? LayerTr(cell, lay) : cell.t;
		bool pit = (t == T_CHASM || t == T_LAVA || t == T_WATER);
		if (!pit && TerrainInfo(t).costFoot >= 99) return false;
		int o = b.UnitAt(x, z, lay);
		if (o >= 0 && o != ti) return false;
	}
	if (outX) *outX = nx;
	if (outZ) *outZ = nz;
	return true;
}

void DoShove(Battle& b, int ui, int ti)
{
	int nx, nz;
	if (!CanShove(b, ui, ti, &nx, &nz)) return;
	Unit& d = b.u[ti];
	int fromL = LayerOn(b.map.At(d.x, d.z), d.deck) ? d.deck : 0;
	int lay = ChooseLayer(b.map.At(d.x, d.z), fromL, b.map.At(nx, nz), UnitJump(d), UnitMoveType(d));
	d.x = nx; d.z = nz;
	d.deck = lay < 0 ? 0 : lay;
	d.face = atan2f((float)(d.x - b.u[ui].x), (float)(d.z - b.u[ui].z));
	int pit = 0;
	int s = UnitSpan(d);
	for (int iz = 0; iz < s && !pit; iz++) for (int ix = 0; ix < s; ix++) {
		int t = b.map.At(d.x + ix, d.z + iz).t;
		if (t == T_CHASM || t == T_LAVA) pit = 1;
	}
	if (pit) {
		int dmg = max(1, d.ch.stat[S_HP] * 40 / 100);
		d.hp = max(0, d.hp - dmg);
		if (d.hp <= 0) { d.hp = 0; d.alive = 0; d.killed = 1; }
	}
	b.RebuildOcc();
}

// ============================================================ AI
static void DistanceField(const Battle& b, int ui, short (*df)[MAPN])
{
	const Unit& u = b.u[ui];
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) df[z][x] = 30000;
	static int qx[MAPN * MAPN * 4], qz[MAPN * MAPN * 4];
	int qh = 0, qt = 0;
	for (int i = 0; i < b.nUnits; i++) {
		const Unit& t = b.u[i];
		if (!t.alive || t.team == u.team) continue;
		int s = UnitSpan(t);
		for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++) {
			int x = t.x + dx, z = t.z + dz;
			if (!Map::In(x, z) || df[z][x] == 0) continue;
			df[z][x] = 0;
			qx[qt] = x; qz[qt] = z; qt++;
		}
	}
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	// 0-1 ではないので緩和 BFS（反復）
	while (qh < qt) {
		int x = qx[qh], z = qz[qh]; qh++;
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz)) continue;
			if (!FootTerrain(b, ui, nx, nz, x, z)) continue;
			int c = df[z][x] + FootCost(b, ui, x, z);
			if (c < df[nz][nx]) {
				df[nz][nx] = (short)c;
				if (qt < (int)_countof(qx)) { qx[qt] = nx; qz[qt] = nz; qt++; }
			}
		}
	}
}

void ConfusePlan(const Battle& b, int ui, AiPlan& p, Rng& r)
{
	const Unit& u = b.u[ui];
	p.moveX = u.x; p.moveZ = u.z; p.target = -1; p.skill = -1; p.castX = u.x; p.castZ = u.z; p.wild = 1;
	static Reach rc;
	ComputeReach(b, ui, rc);
	int roll = (int)(r.Next() % 100);
	int want = 0;
	if (roll < 45) want = 1;
	else if (roll < 70) want = 2;
	else if (roll < 85) want = 3;
	int pick = -1;
	if (want == 1 || want == 2) {
		int ids[MAX_UNITS], n = 0;
		for (int i = 0; i < b.nUnits; i++) {
			const Unit& t = b.u[i];
			if (i == ui || !t.alive || t.hp <= 0) continue;
			int foe = t.team != u.team;
			if (want == 1 && foe) ids[n++] = i;
			if (want == 2 && !foe) ids[n++] = i;
		}
		if (n > 0) pick = ids[r.Next() % n];
	}
	if (want == 3) p.wild = 2;
	int best = -999999, bx = u.x, bz = u.z;
	int any = 0;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (!ReachCanStop(rc, x, z)) continue;
		any = 1;
		int sc = (int)(r.Next() % 7);
		if (pick >= 0) {
			const Unit& t = b.u[pick];
			if (InRangeOfUnit(b, u, x, z, UnitRangeMin(u), UnitRangeMax(u), t, true)) sc += 1000;
			else sc -= FootDist(x, z, UnitSpan(u), t.x, t.z, UnitSpan(t)) * 4;
		}
		if (sc > best) { best = sc; bx = x; bz = z; }
	}
	if (any) { p.moveX = bx; p.moveZ = bz; }
	if (pick >= 0) {
		p.target = pick;
		p.castX = b.u[pick].x;
		p.castZ = b.u[pick].z;
		p.wild = 1;
	}
}

void AiDecide(const Battle& b, int ui, AiPlan& p, Rng& r, int level)
{
	const Unit& u = b.u[ui];
	p.moveX = u.x; p.moveZ = u.z; p.target = -1; p.skill = -1; p.castX = u.x; p.castZ = u.z; p.wild = 0;
	if (u.sleep > 0 || u.stun > 0) return;
	if (u.confuse > 0) { ConfusePlan(b, ui, p, r); return; }
	if (level < 0) level = 0;
	if (level > 5) level = 5;
	static Reach rc;
	ComputeReach(b, ui, rc);
	if (level <= 0) {
		int bag[192], nc = 0;
		for (int z = 0; z < MAPN && nc < 192; z++) for (int x = 0; x < MAPN && nc < 192; x++) {
			if (!ReachCanStop(rc, x, z)) continue;
			bag[nc++] = z * MAPN + x;
		}
		if (nc > 0) {
			int pick = bag[r.Range(0, nc - 1)];
			p.moveX = pick % MAPN;
			p.moveZ = pick / MAPN;
		}
		Battle& bb = const_cast<Battle&>(b);
		Unit& um = bb.u[ui];
		int ox = u.x, oz = u.z;
		um.x = p.moveX; um.z = p.moveZ;
		int foes[32], nf = 0;
		for (int i = 0; i < b.nUnits && nf < 32; i++) {
			const Unit& t = b.u[i];
			if (!t.alive || t.team == u.team) continue;
			int ax = t.x, az = t.z;
			if (AimFoot(b, u, p.moveX, p.moveZ, UnitRangeMin(u), UnitRangeMax(u), t, true, ax, az)) foes[nf++] = i;
		}
		if (nf > 0 && (r.Next() % 100) < 70) {
			int i = foes[r.Range(0, nf - 1)];
			p.target = i;
			p.castX = b.u[i].x;
			p.castZ = b.u[i].z;
			if ((r.Next() % 100) < 16) {
				for (int s = 0; s < SKL_COUNT; s++) {
					if (!SkillUsable(b, ui, s)) continue;
					if ((r.Next() % 7) == 0) { p.skill = s; break; }
				}
			}
		}
		um.x = ox; um.z = oz;
		return;
	}
	float best = -1e9f;
	BOOL tryArt = level >= 4 || (r.Next() % 100) < (level <= 1 ? 12 : (level == 3 ? 68 : 36));
	BOOL tryOugi = level >= 4 || (r.Next() % 100) < (level <= 1 ? 4 : (level == 3 ? 34 : 14));
	BOOL allowSkill = level >= 2 || (r.Next() % 100) < 18;
	// 攻撃スキルと奥義は約半分の手番だけ。残りは通常攻撃。回復と、追い詰められた防御は残す。
	BOOL halfOff = level < 2 || (r.Next() % 100) < 50;
	float noise = level <= 1 ? 2.8f : (level == 2 ? 1.f : (level == 3 ? 0.35f : 0.06f));
	Battle& bb = const_cast<Battle&>(b);
	Unit& um = bb.u[ui];
	const int ox = u.x, oz = u.z;
	int focus = -1;
	if (level >= 5) {
		float bf = -1e9f;
		for (int i = 0; i < b.nUnits; i++) {
			const Unit& t = b.u[i];
			if (!t.alive || t.team == u.team) continue;
			float sc = (1.f - (float)t.hp / max(1, t.ch.stat[S_HP])) * 60.f + t.ch.lv * .4f;
			int nAdj = 0;
			for (int k = 0; k < b.nUnits; k++) {
				if (k == ui || !b.u[k].alive || b.u[k].team != u.team) continue;
				if (FootDist(b.u[k].x, b.u[k].z, UnitSpan(b.u[k]), t.x, t.z, UnitSpan(t)) <= 4) nAdj++;
			}
			sc += nAdj * 12.f;
			if (sc > bf) { bf = sc; focus = i; }
		}
	}
	// 体力半分以下のボスは、踏み込むより距離を取る。射撃が届く位置を優先し、防御を上げる技があれば先に使う。
	const int cornered = u.boss && u.hp * 2 <= max(1, u.ch.stat[S_HP]);
	static short nearD[MAPN][MAPN];
	int farReach = 0;
	if (cornered) {
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			nearD[z][x] = 999;
			if (!ReachCanStop(rc, x, z)) continue;
			int nd = 999;
			for (int i = 0; i < b.nUnits; i++) {
				const Unit& t = b.u[i];
				if (!t.alive || t.team == u.team) continue;
				nd = min(nd, FootDist(x, z, UnitSpan(u), t.x, t.z, UnitSpan(t)));
			}
			nearD[z][x] = (short)nd;
			if (nd < 999 && nd > farReach) farReach = nd;
		}
	}
	const int room = farReach >= 2;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (!ReachCanStop(rc, x, z)) continue;
		um.x = x; um.z = z;
		int standT = b.map.At(x, z).t;
		float place = TerrainInfo(standT).defPct * .25f - ReachBestCost(rc, x, z) * .05f;
		if (standT == T_LAVA) place -= 30.f;
		else if (standT == T_WATER) place -= 2.f;
		if (level >= 4) {
			int threat = 0;
			for (int i = 0; i < b.nUnits; i++) {
				const Unit& t = b.u[i];
				if (!t.alive || t.team == u.team) continue;
				if (FootDist(x, z, UnitSpan(u), t.x, t.z, UnitSpan(t)) <= 2) threat++;
			}
			place -= threat * (level >= 5 ? 7.f : 3.f);
		}
		if (b.map.At(x, z).gimm == GM_DROP) place -= 8.f;
		const int tileNear = cornered ? (int)nearD[z][x] : 0;
		if (cornered) {
			float flee = tileNear * 4.f + place;
			int rmax = UnitRangeMax(u);
			if (rmax >= 2) {
				if (tileNear >= 2 && tileNear <= rmax) flee += 28.f;
				else if (tileNear > rmax) flee -= (tileNear - rmax) * 3.f;
			}
			if (room && tileNear <= 1) flee -= 36.f;
			flee += r.F(0, 3.f * noise);
			if (flee > best) { best = flee; p.moveX = x; p.moveZ = z; p.target = -1; p.skill = -1; p.castX = x; p.castZ = z; }
		}
		// 通常攻撃
		for (int i = 0; i < b.nUnits; i++) {
			const Unit& t = b.u[i];
			if (!t.alive || t.team == u.team) continue;
			int ax = t.x, az = t.z;
			if (!AimFoot(b, u, x, z, UnitRangeMin(u), UnitRangeMax(u), t, true, ax, az)) continue;
			Forecast f;
			CalcForecast(b, ui, i, -1, f);
			// トドメと残HPだけでは選ばない。近い相手、手強い相手、その場の揺らぎも同じ重さで見る。
			float exp = f.dmg * f.hits * f.hit / 100.f;
			float sc = exp * .55f + (f.dmg * f.hits >= t.hp ? 12.f : 0.f);
			sc += t.ch.lv * .35f;
			sc += (1.f - (float)t.hp / max(1, t.ch.stat[S_HP])) * 4.f;
			sc -= (f.canCounter ? f.cdmg * f.chits * f.chit / 100.f * .35f : 0.f);
			int gap = FootDist(x, z, UnitSpan(u), t.x, t.z, UnitSpan(t));
			if (!cornered) sc -= FootDist(ox, oz, UnitSpan(u), t.x, t.z, UnitSpan(t)) * 2.5f;
			else {
				sc += tileNear * 4.f;
				if (room && gap <= 1) sc -= 80.f;
				else if (gap >= 2) sc += 36.f;
				int kill = f.dmg * f.hits >= t.hp;
				int suicide = f.canCounter && f.cdmg * f.chits >= u.hp;
				if (kill && !suicide) sc += 110.f;
				if (suicide) sc -= 50.f;
			}
			if (level >= 5 && focus >= 0) sc += (i == focus) ? 48.f : -10.f;
			sc += place + r.F(0, 14.f * noise);
			if (sc > best) { best = sc; p.moveX = x; p.moveZ = z; p.target = i; p.skill = -1; p.castX = ax; p.castZ = az; }
		}
		// スキル
		for (int s = 0; s < SKL_COUNT; s++) {
			if (!allowSkill) break;
			if (!SkillUsable(b, ui, s)) continue;
			const SkillDef& sd = SkillInfo(s);
			if (!halfOff && sd.kind != SK_HEAL && sd.kind != SK_BUFF) continue;
			int rmin = SkillCastRangeMin(u, s), rmax = SkillCastRangeMax(u, s);
			for (int i = 0; i < b.nUnits; i++) {
				const Unit& t = b.u[i];
				if (!t.alive) continue;
				bool friendly = sd.kind == SK_HEAL || sd.kind == SK_BUFF;
				if (friendly != (t.team == u.team)) continue;
				int tx = t.x, tz = t.z;
				if (sd.rMax == 0) { tx = x; tz = z; }
				else if (!AimFoot(b, u, x, z, rmin, rmax, t, sd.kind != SK_HEAL && sd.kind != SK_BUFF, tx, tz)) continue;
				float sc = place;
				if (sd.kind == SK_HEAL) {
					int targets[MAX_UNITS];
					int nt = SkillTargets(b, ui, s, tx, tz, targets, MAX_UNITS);
					float h = 0;
					for (int k = 0; k < nt; k++) {
						const Unit& q = b.u[targets[k]];
						int miss = q.ch.stat[S_HP] - q.hp;
						if (q.hp * 100 / max(1, q.ch.stat[S_HP]) < 65) {
							int heal = HealAmount(u, q, sd, s);
							if (IsFormSkill(s)) heal = (int)(heal * FormAllyMul(b, u, s, u.x, u.z));
							h += min(miss, heal);
						}
						if (sd.effect == EF_CLEANSE && (q.poison > 0 || q.sleep > 0 || q.confuse > 0 || q.stun > 0 || q.slow > 0 || q.defDown > 0 || q.freeze > 0 || q.burn > 0 || q.bleed > 0 || q.blind > 0 || q.silence > 0)) h += 48;
					}
					if (h <= 0) continue;
					sc += h * 1.2f;
				} else if (sd.kind == SK_BUFF) {
					if (!cornered || sd.effect != EF_BUFF_DEF || u.buffDef > 1) continue;
					int targets[MAX_UNITS];
					int nt = SkillTargets(b, ui, s, tx, tz, targets, MAX_UNITS);
					int self = 0;
					for (int k = 0; k < nt; k++) if (targets[k] == ui) self = 1;
					if (!self) continue;
					sc += tileNear * 4.f + 120.f;
				} else {
					int targets[MAX_UNITS];
					int nt = SkillTargets(b, ui, s, tx, tz, targets, MAX_UNITS);
					if (sd.area == 0) { targets[0] = i; nt = 1; }
					float tot = 0;
					int kill = 0;
					for (int k = 0; k < nt; k++) {
						Forecast f;
						CalcForecast(b, ui, targets[k], s, f);
						const Unit& q = b.u[targets[k]];
						tot += f.dmg * f.hits * f.hit / 100.f * .55f + (f.dmg * f.hits >= q.hp ? 12.f : 0.f);
						if (f.dmg * f.hits >= q.hp) kill = 1;
						if (!cornered) tot += q.ch.lv * .2f - Dist(ox, oz, q.x, q.z) * 1.6f;
						else tot += q.ch.lv * .2f;
					}
					if (tot <= 0) continue;
					sc += tot - sd.mp * .4f;
					if (cornered) {
						int gap = FootDist(x, z, UnitSpan(u), b.u[i].x, b.u[i].z, UnitSpan(b.u[i]));
						sc += tileNear * 4.f;
						if (room && gap <= 1) sc -= 80.f;
						else if (gap >= 2) sc += 36.f;
						if (kill) sc += 110.f;
					}
				}
				if (level >= 5 && focus >= 0 && sd.kind != SK_HEAL && sd.kind != SK_BUFF) sc += (i == focus) ? 36.f : -8.f;
				sc += r.F(0, 10.f * noise);
				if (sc > best) { best = sc; p.moveX = x; p.moveZ = z; p.target = i; p.skill = s; p.castX = tx; p.castZ = tz; }
			}
		}
		for (int ck = 0; ck < CRAFT_SLOT_N; ck++) {
			int cs = u.craftEq[ck];
			if (cs < CRAFT_BASE) continue;
			BOOL ougi = IsCraftSuper(cs);
			const SkillDef& csd = SkillInfo(cs);
			int defBuff = cornered && csd.effect == EF_BUFF_DEF && u.buffDef <= 1;
			if ((ougi ? !tryOugi : !tryArt) && !defBuff) continue;
			if (!halfOff && !defBuff && csd.kind != SK_HEAL) continue;
			if (!SkillUsable(b, ui, cs)) continue;
			int rmin = SkillCastRangeMin(u, cs), rmax = SkillCastRangeMax(u, cs);
			for (int i = 0; i < b.nUnits; i++) {
				const Unit& t = b.u[i];
				if (!t.alive) continue;
				bool friendly = csd.kind == SK_HEAL || csd.kind == SK_BUFF;
				if (friendly != (t.team == u.team)) continue;
				int tx = t.x, tz = t.z;
				if (csd.rMax == 0) { tx = x; tz = z; }
				else if (!AimFoot(b, u, x, z, rmin, rmax, t, !friendly, tx, tz)) continue;
				float sc = place;
				if (friendly) {
					int targets[8];
					int nt = SkillTargets(b, ui, cs, tx, tz, targets, 8);
					if (nt <= 0) continue;
					sc += nt * (ougi ? 14.f : 9.f);
					if (defBuff) {
						int self = 0;
						for (int k = 0; k < nt; k++) if (targets[k] == ui) self = 1;
						if (!self) continue;
						sc += tileNear * 4.f + 120.f;
					}
					if (csd.effect == EF_CLEANSE) {
						for (int k = 0; k < nt; k++) {
							const Unit& q = b.u[targets[k]];
							if (q.poison > 0 || q.sleep > 0 || q.confuse > 0 || q.stun > 0 || q.slow > 0 || q.defDown > 0) sc += 36.f;
						}
					}
				} else {
					Forecast f;
					CalcForecast(b, ui, i, cs, f);
					if (f.dmg <= 0) continue;
					sc += f.dmg * f.hit / 100.f * (ougi ? .25f : .35f);
					if (!cornered) sc += t.ch.lv * .25f - Dist(ox, oz, t.x, t.z) * 1.8f;
					else {
						int gap = FootDist(x, z, UnitSpan(u), t.x, t.z, UnitSpan(t));
						sc += tileNear * 4.f + t.ch.lv * .25f;
						if (room && gap <= 1) sc -= 80.f;
						else if (gap >= 2) sc += 36.f;
						if (f.dmg * f.hits >= t.hp && !(f.canCounter && f.cdmg * f.chits >= u.hp)) sc += 110.f;
					}
					if (level >= 5 && focus >= 0) sc += (i == focus) ? 28.f : -6.f;
					sc += r.F(0, 10.f * noise);
				}
				if (sc > best) { best = sc; p.moveX = x; p.moveZ = z; p.target = i; p.skill = cs; p.castX = tx; p.castZ = tz; }
			}
		}
	}
	um.x = ox; um.z = oz;
	if (p.target >= 0) return;
	// 半減以下は上で距離を取っている。ここで近づくと逃げが消える。
	if (cornered) return;
	// ボスは近づかれるまで待機
	if (u.boss) {
		int nearest = 999;
		for (int i = 0; i < b.nUnits; i++) if (b.u[i].alive && b.u[i].team != u.team) nearest = min(nearest, FootDist(u.x, u.z, UnitSpan(u), b.u[i].x, b.u[i].z, UnitSpan(b.u[i])));
		if (nearest > 10) return;
	}
	static short df[MAPN][MAPN];
	DistanceField(b, ui, df);
	int bd = df[u.z][u.x];
	float bs = -1e9f;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (!ReachCanStop(rc, x, z)) continue;
		int d = df[z][x];
		if (d >= 30000) continue;
		float sc = -d * 10.f + TerrainInfo(b.map.At(x, z).t).defPct * .2f + r.F(0, .5f * noise);
		if (level >= 5 && focus >= 0) {
			const Unit& ft = b.u[focus];
			sc -= FootDist(x, z, UnitSpan(u), ft.x, ft.z, UnitSpan(ft)) * 4.f;
		}
		if (UnitRangeMax(u) >= 2 && d <= 1) sc -= 15.f;
		if (sc > bs) { bs = sc; p.moveX = x; p.moveZ = z; }
	}
	if (bs < -1e8f || df[p.moveZ][p.moveX] > bd) { p.moveX = u.x; p.moveZ = u.z; }
}

// ============================================================ 武具（生成表: tactics_src/gear_catalog.inc）
#include "tactics_src/gear_catalog.inc"
#include "tactics_src/gear_extra.inc"
static GearDef s_gearEx[GEAR_EX_N];
static int s_gearExOk = 0;
static void EnsureGearEx()
{
	if (s_gearExOk) return;
	for (int i = 0; i < GEAR_EX_N; i++) {
		const GearExSpec& e = kGearExSpec[i];
		GearDef& d = s_gearEx[i];
		memset(&d, 0, sizeof(d));
		d.line = e.line;
		d.kind = e.kind;
		d.price = e.price;
		d.str = e.str;
		d.mag = e.mag;
		d.def = e.def;
		d.res = e.res;
		d.stage = 40;
		d.req = e.req;
		d.elem = e.elem;
		d.shop = 1;
		d.joint0 = e.kind ? 3 : 0;
		d.joint1 = e.kind ? 2 : 1;
		if (e.kind) { d.mesh0 = 26; d.mesh1 = 38; }
		else switch (e.line) {
		case 0: d.mesh0 = 20; d.mesh1 = 17; break;
		case 1: d.mesh0 = 1; d.mesh1 = 18; break;
		case 2: d.mesh0 = 4; d.mesh1 = 47; break;
		case 3: d.mesh0 = 9; d.mesh1 = 50; break;
		case 4: d.mesh0 = -1; d.mesh1 = 32; d.att0 = "2H_Staff"; break;
		default: d.mesh0 = 44; d.mesh1 = 33; break;
		}
	}
	s_gearExOk = 1;
}
int GearCount() { EnsureGearEx(); return kGearN + GEAR_EX_N; }
const GearDef* GearOf(int id)
{
	if (id < 1) return NULL;
	if (id <= kGearN) return &kGear[id - 1];
	EnsureGearEx();
	int x = id - kGearN - 1;
	if (x < 0 || x >= GEAR_EX_N) return NULL;
	return &s_gearEx[x];
}
const wchar_t* GearName(int id)
{
	if (id < 1) return L"?";
	if (id <= kGearN) {
		const wchar_t* const* s = kGearNm[id - 1];
		return LangPick14(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]);
	}
	int x = id - kGearN - 1;
	if (x < 0 || x >= GEAR_EX_N) return L"?";
	const wchar_t* ja = kGearExSpec[x].ja;
	const wchar_t* en = kGearExSpec[x].en;
	return LangPick14(ja, en, en, en, en, en, en, en, en, en, en, en, en, en);
}
const wchar_t* GearRole(const GearDef* g)
{
	if (!g || g->line < 0 || g->line > 5) return L"";
	static const int wpn[6] = { TX_GD_W0, TX_GD_W1, TX_GD_W2, TX_GD_W3, TX_GD_W4, TX_GD_W5 };
	static const int arm[6] = { TX_GD_A0, TX_GD_A1, TX_GD_A2, TX_GD_A3, TX_GD_A4, TX_GD_A5 };
	if (g->kind == 0) return TT(wpn[g->line]);
	if (g->kind == 1) return TT(arm[g->line]);
	return L"";
}
const wchar_t* GoodDesc(int i)
{
	const GoodDef* g = GoodAt(i);
	if (!g) return L"";
	if (g->item == IT_ELIXIR) return TT(TX_ITD_ELIXIR);
	if (g->item == IT_MIGHT) return TT(TX_ITD_MIGHT);
	if (g->item == IT_DELAY) return TT(TX_ITD_DELAY);
	if (g->item == IT_RAISE) return TT(TX_ITD_RAISE);
	if (g->extra == IX_FULL) return TT(TX_ITD_FULL);
	if (g->extra == IX_ATK) return TT(TX_ITD_ATK);
	if (g->extra == IX_DEF) return TT(TX_ITD_DEF);
	if (g->extra == IX_AGI) return TT(TX_ITD_AGI);
	if (g->extra == IX_SLOW) return TT(TX_ITD_SLOW);
	if (g->extra == IX_VENOM) return TT(TX_ITD_VENOM);
	if (g->extra == IX_CURE) return TT(TX_ITD_CURE);
	if (g->extra == IX_ETHER) return TT(TX_ITD_ETHER);
	switch (g->extra) {
	case 100: return LangPick14(L"3マス以内の味方の睡眠を解き、HPを最大の15%回復する。", L"Wakes an ally within 3 tiles and restores 15% max HP.", L"Réveille un allié dans 3 cases et rend 15 % des PV max.", L"Sveglia un alleato entro 3 caselle e restituisce il 15% dei PS max.", L"Despierta a un aliado a 3 casillas y recupera el 15% de PV máx.", L"3칸 이내의 아군 수면을 풀고 HP를 최대의 15% 회복한다.", L"解除3格以内同伴的睡眠，并恢复最大HP的15%。", L"يوقظ حليفًا ضمن 3 مربعات ويعيد 15٪ من الصحة القصوى.", L"Будит союзника в 3 клетках и восстанавливает 15% макс. ОЗ.", L"Weckt einen Verbündeten in 3 Feldern und stellt 15 % der max. LP her.", L"Acorda um aliado em até 3 casas e recupera 15% dos PV máx.", L"Wekt een bondgenoot binnen 3 vakken en herstelt 15% van de max. LP.", L"Budzi sojusznika w zasięgu 3 pól i przywraca 15% maks. PŻ.", L"3 kare içindeki yoldaşın uykusunu bozar ve azami CP'nin %15'ini iyileştirir.");
	case 101: return LangPick14(L"3マス以内の味方の混乱を解く。", L"Clears an ally's confusion within 3 tiles.", L"Dissipe la confusion d'un allié dans 3 cases.", L"Toglie la confusione di un alleato entro 3 caselle.", L"Quita la confusión de un aliado a 3 casillas.", L"3칸 이내의 아군 혼란을 푼다.", L"解除3格以内同伴的混乱。", L"يزيل ارتباك حليف ضمن 3 مربعات.", L"Снимает смятение союзника в пределах 3 клеток.", L"Hebt die Verwirrung eines Verbündeten in 3 Feldern auf.", L"Remove a confusão de um aliado em até 3 casas.", L"Heft de verwarring van een bondgenoot binnen 3 vakken op.", L"Zdejmuje zamęt sojusznika w zasięgu 3 pól.", L"3 kare içindeki yoldaşın kafa karışıklığını siler.");
	case 102: return LangPick14(L"3マス以内の味方のスタンを解く。", L"Clears an ally's stun within 3 tiles.", L"Dissipe l'étourdissement d'un allié dans 3 cases.", L"Toglie lo stordimento di un alleato entro 3 caselle.", L"Quita el aturdimiento de un aliado a 3 casillas.", L"3칸 이내의 아군 기절을 푼다.", L"解除3格以内同伴的硬直。", L"يزيل صعقة حليف ضمن 3 مربعات.", L"Снимает оглушение союзника в пределах 3 клеток.", L"Hebt die Betäubung eines Verbündeten in 3 Feldern auf.", L"Remove o atordoamento de um aliado em até 3 casas.", L"Heft de verdoving van een bondgenoot binnen 3 vakken op.", L"Zdejmuje ogłuszenie sojusznika w zasięgu 3 pól.", L"3 kare içindeki yoldaşın sersemliğini siler.");
	case 103: return LangPick14(L"3マス以内の味方の状態異常をすべて消し、HPを最大の20%回復する。", L"Clears every ailment on an ally within 3 tiles and restores 20% max HP.", L"Dissipe tout état d'un allié dans 3 cases et rend 20 % des PV max.", L"Toglie ogni malus a un alleato entro 3 caselle e restituisce il 20% dei PS max.", L"Quita todo estado de un aliado a 3 casillas y recupera el 20% de PV máx.", L"3칸 이내의 아군 상태 이상을 모두 풀고 HP를 최대의 20% 회복한다.", L"解除3格以内同伴的全部异常，并恢复最大HP的20%。", L"يزيل كل الحالات عن حليف ضمن 3 مربعات ويعيد 20٪ من الصحة.", L"Снимает все состояния с союзника в 3 клетках и восстанавливает 20% макс. ОЗ.", L"Hebt alle Zustände eines Verbündeten in 3 Feldern auf und stellt 20 % der max. LP her.", L"Remove todo estado de um aliado em até 3 casas e recupera 20% dos PV máx.", L"Heft alle toestanden van een bondgenoot binnen 3 vakken op en herstelt 20% van de max. LP.", L"Zdejmuje wszystkie stany sojusznika w zasięgu 3 pól i przywraca 20% maks. PŻ.", L"3 kare içindeki yoldaşın tüm durumlarını siler ve azami CP'nin %20'sini iyileştirir.");
	case 104: return LangPick14(L"3マス以内の味方のMPを全回復する。", L"Fully restores an ally's MP within 3 tiles.", L"Rend tous les PM d'un allié dans 3 cases.", L"Restituisce tutti i PM di un alleato entro 3 caselle.", L"Recupera todos los PM de un aliado a 3 casillas.", L"3칸 이내의 아군 MP를 전부 회복한다.", L"完全恢复3格以内同伴的MP。", L"يعيد كل طاقة حليف ضمن 3 مربعات.", L"Полностью восстанавливает ОМ союзника в пределах 3 клеток.", L"Stellt alle MP eines Verbündeten in 3 Feldern wieder her.", L"Recupera todos os PM de um aliado em até 3 casas.", L"Herstelt alle MP van een bondgenoot binnen 3 vakken.", L"Przywraca wszystkie PM sojusznika w zasięgu 3 pól.", L"3 kare içindeki yoldaşın tüm MP'sini yeniler.");
	case 105: return LangPick14(L"3マス以内の味方のHPとMPを、それぞれ最大の30%回復する。", L"Restores 30% max HP and MP to an ally within 3 tiles.", L"Rend 30 % des PV et PM max d'un allié dans 3 cases.", L"Restituisce il 30% dei PS e PM max di un alleato entro 3 caselle.", L"Recupera el 30% de PV y PM máx. de un aliado a 3 casillas.", L"3칸 이내의 아군 HP와 MP를 각각 최대의 30% 회복한다.", L"恢复3格以内同伴最大HP和MP的各30%。", L"يعيد 30٪ من الصحة والطاقة القصوى لحليف ضمن 3 مربعات.", L"Восстанавливает 30% макс. ОЗ и ОМ союзнику в 3 клетках.", L"Stellt 30 % der max. LP und MP eines Verbündeten in 3 Feldern her.", L"Recupera 30% dos PV e PM máx. de um aliado em até 3 casas.", L"Herstelt 30% van de max. LP en MP van een bondgenoot binnen 3 vakken.", L"Przywraca 30% maks. PŻ i PM sojusznika w zasięgu 3 pól.", L"3 kare içindeki yoldaşın azami CP ve MP'sinin %30'unu yeniler.");
	case 106: return LangPick14(L"3マス以内の倒れた味方を起こし、HPを最大の80%にする。", L"Raises a fallen ally within 3 tiles at 80% HP.", L"Relève un allié tombé dans 3 cases, à 80 % des PV.", L"Rialza un alleato caduto entro 3 caselle, all'80% dei PS.", L"Levanta a un aliado caído dentro de 3 casillas, al 80% de PV.", L"3칸 이내의 쓰러진 아군을 일으키고 HP를 최대의 80%로 한다.", L"扶起3格以内倒下的同伴，HP恢复到最大的80%。", L"ينهض حليفًا ساقطًا ضمن 3 مربعات بنسبة 80٪ من الصحة.", L"Поднимает павшего союзника в пределах 3 клеток с 80% ОЗ.", L"Richtet einen gefallenen Verbündeten innerhalb von 3 Feldern mit 80 % LP auf.", L"Ergue um aliado caído em até 3 casas com 80% dos PV.", L"Laat een gevallene binnen 3 vakken opstaan met 80% LP.", L"Podnosi poległego sojusznika w zasięgu 3 pól z 80% PŻ.", L"3 kare içindeki düşmüş yoldaşı %80 CP ile kaldırır.");
	case 107: return LangPick14(L"1〜3マス先の敵に5ターン毒を与える。毎ターン最大HPの5%。", L"Poisons an enemy 1 to 3 tiles away for 5 turns. 5% max HP each turn.", L"Empoisonne un ennemi à 1–3 cases pour 5 tours. 5 % des PV max par tour.", L"Avvelena un nemico a 1–3 caselle per 5 turni. 5% dei PS max a turno.", L"Envenena a un enemigo a 1–3 casillas durante 5 turnos. 5% de PV máx. por turno.", L"1〜3칸 앞의 적에게 5턴 독을 준다. 매 턴 최대 HP의 5%.", L"使1至3格外的敌人中毒5回合。每回合失去最大HP的5%。", L"يسمم عدوًا على بعد 1–3 مربعات لخمسة أدوار. 5٪ من الصحة كل دور.", L"Отравляет врага в 1–3 клетках на 5 ходов. 5% макс. ОЗ за ход.", L"Vergiftet einen Feind in 1–3 Feldern für 5 Züge. 5 % der max. LP je Zug.", L"Envenena um inimigo a 1–3 casas por 5 turnos. 5% dos PV máx. por turno.", L"Vergiftigt een vijand op 1–3 vakken voor 5 beurten. 5% van de max. LP per beurt.", L"Zatruwa wroga w odległości 1–3 pól na 5 tur. 5% maks. PŻ na turę.", L"1–3 kare ötedeki düşmanı 5 tur zehirler. Her tur azami CP'nin %5'i.");
	case 108: return LangPick14(L"1〜3マス先の敵の行動順を大きく遅らせ、さらに4ターン鈍足にする。", L"Greatly delays an enemy 1 to 3 tiles away and slows them for 4 turns.", L"Retarde beaucoup un ennemi à 1–3 cases et le ralentit 4 tours.", L"Ritarda molto un nemico a 1–3 caselle e lo rallenta per 4 turni.", L"Retrasa mucho a un enemigo a 1–3 casillas y lo ralentiza 4 turnos.", L"1〜3칸 앞의 적 행동 순서를 크게 늦추고 4턴 둔화시킨다.", L"大幅推迟1至3格外的敌人，并使其迟缓4回合。", L"يؤخر كثيرًا عدوًا على بعد 1–3 مربعات ويبطئه 4 أدوار.", L"Сильно откладывает врага в 1–3 клетках и замедляет на 4 хода.", L"Verzögert einen Feind in 1–3 Feldern stark und verlangsamt ihn 4 Züge.", L"Atrasa muito um inimigo a 1–3 casas e o deixa lento por 4 turnos.", L"Vertraagt een vijand op 1–3 vakken sterk en vertraagt hem 4 beurten.", L"Mocno opóźnia wroga w odległości 1–3 pól i spowalnia go na 4 tury.", L"1–3 kare ötedeki düşmanın sırasını çok geciktirir ve 4 tur yavaşlatır.");
	case 109: return LangPick14(L"3マス以内の味方の毒を消し、HPを最大の35%回復する。", L"Cures an ally's poison within 3 tiles and restores 35% max HP.", L"Soigne le poison d'un allié dans 3 cases et rend 35 % des PV max.", L"Cura il veleno di un alleato entro 3 caselle e restituisce il 35% dei PS max.", L"Cura el veneno de un aliado a 3 casillas y recupera el 35% de PV máx.", L"3칸 이내의 아군 독을 풀고 HP를 최대의 35% 회복한다.", L"解除3格以内同伴的中毒，并恢复最大HP的35%。", L"يزيل سم حليف ضمن 3 مربعات ويعيد 35٪ من الصحة.", L"Снимает яд с союзника в 3 клетках и восстанавливает 35% макс. ОЗ.", L"Hebt das Gift eines Verbündeten in 3 Feldern auf und stellt 35 % der max. LP her.", L"Cura o veneno de um aliado em até 3 casas e recupera 35% dos PV máx.", L"Heft het gif van een bondgenoot binnen 3 vakken op en herstelt 35% van de max. LP.", L"Zdejmuje truciznę sojusznika w zasięgu 3 pól i przywraca 35% maks. PŻ.", L"3 kare içindeki yoldaşın zehrini siler ve azami CP'nin %35'ini iyileştirir.");
	case 110: return LangPick14(L"3マス以内の味方を5ターン、守備を30%上げる。", L"Raises an ally's defense by 30% for 5 turns. Within 3 tiles.", L"Défense d'un allié +30 % pendant 5 tours. Portée 3.", L"Difesa di un alleato +30% per 5 turni. Entro 3 caselle.", L"Defensa de un aliado +30% durante 5 turnos. Alcance 3.", L"3칸 이내의 아군을 5턴 동안 수비 30% 올린다.", L"使3格以内的同伴在5回合内防御提高30%。", L"يرفع دفاع حليف 30٪ لخمسة أدوار. ضمن 3 مربعات.", L"Защита союзника +30% на 5 ходов. В пределах 3 клеток.", L"Abwehr eines Verbündeten +30 % für 5 Züge. Reichweite 3.", L"Defesa de um aliado +30% por 5 turnos. Até 3 casas.", L"Verdediging van een bondgenoot +30% gedurende 5 beurten. Binnen 3 vakken.", L"Obrona sojusznika +30% przez 5 tur. Zasięg 3 pól.", L"3 kare içindeki yoldaşın savunmasını 5 tur %30 artırır.");
	case 111: return LangPick14(L"3マス以内の味方を5ターン素早さ30%上げ、行動順を少し早める。", L"Raises an ally's agility by 30% for 5 turns and hurries their next action. Within 3 tiles.", L"Agilité d'un allié +30 % pendant 5 tours, et son prochain tour avance. Portée 3.", L"Agilità di un alleato +30% per 5 turni, e il prossimo turno arriva prima. Entro 3 caselle.", L"Agilidad de un aliado +30% durante 5 turnos, y su siguiente turno llega antes. Alcance 3.", L"3칸 이내의 아군을 5턴 민첩 30% 올리고 행동 순서를 조금 당긴다.", L"使3格以内的同伴敏捷提高30%共5回合，并略微提前行动。", L"يرفع رشاقة حليف 30٪ لخمسة أدوار ويقرّب دوره. ضمن 3 مربعات.", L"Ловкость союзника +30% на 5 ходов, и следующий ход ближе. В пределах 3 клеток.", L"Beweglichkeit eines Verbündeten +30 % für 5 Züge, und der nächste Zug kommt früher. Reichweite 3.", L"Agilidade de um aliado +30% por 5 turnos e o próximo turno chega antes. Até 3 casas.", L"Behendigheid van een bondgenoot +30% gedurende 5 beurten, en de volgende beurt komt eerder. Binnen 3 vakken.", L"Zwinność sojusznika +30% przez 5 tur i następna tura przychodzi wcześniej. Zasięg 3 pól.", L"3 kare içindeki yoldaşın çevikliğini 5 tur %30 artırır ve sırasını biraz öne alır.");
	case 112: return LangPick14(L"3マス以内の味方の炎上を消し、HPを最大の15%回復する。", L"Clears an ally's burn within 3 tiles and restores 15% max HP.", L"Éteint les flammes d'un allié dans 3 cases et rend 15 % des PV max.", L"Spegne le fiamme di un alleato entro 3 caselle e restituisce il 15% dei PS max.", L"Apaga la quemadura de un aliado a 3 casillas y recupera el 15% de PV máx.", L"3칸 이내의 아군 염상을 끄고 HP를 최대의 15% 회복한다.", L"解除3格以内同伴的炎上，并恢复最大HP的15%。", L"يطفئ احتراق حليف ضمن 3 مربعات ويعيد 15٪ من الصحة.", L"Снимает горение с союзника в 3 клетках и восстанавливает 15% макс. ОЗ.", L"Löscht das Brennen eines Verbündeten in 3 Feldern und stellt 15 % der max. LP her.", L"Apaga a queimadura de um aliado em até 3 casas e recupera 15% dos PV máx.", L"Dooft de brand van een bondgenoot binnen 3 vakken en herstelt 15% van de max. LP.", L"Gasi podpalenie sojusznika w zasięgu 3 pól i przywraca 15% maks. PŻ.", L"3 kare içindeki yoldaşın yanmasını söndürür ve azami CP'nin %15'ini iyileştirir.");
	case 113: return LangPick14(L"3マス以内の味方の凍結を解く。", L"Thaws an ally's freeze within 3 tiles.", L"Dégèle un allié dans 3 cases.", L"Scongela un alleato entro 3 caselle.", L"Descongela a un aliado a 3 casillas.", L"3칸 이내의 아군 동결을 푼다.", L"解除3格以内同伴的冻结。", L"يذيب تجمد حليف ضمن 3 مربعات.", L"Снимает заморозку с союзника в пределах 3 клеток.", L"Taut das Einfrieren eines Verbündeten in 3 Feldern auf.", L"Descongela um aliado em até 3 casas.", L"Ontdooit een bondgenoot binnen 3 vakken.", L"Odmraża sojusznika w zasięgu 3 pól.", L"3 kare içindeki yoldaşın donmasını çözer.");
	default: break;
	}
	return L"";
}
int JobFamOf(int cls)
{
	if (cls < C_JOB0 || cls >= C_JOB0 + TAC_JOB_N) return -1;
	int f = kJobFam[cls - C_JOB0];
	if (f < 0 || f >= 8) return 0;
	return f;
}
int LineParent(int fam)
{
	if (fam < 0 || fam >= TAC_RANK_LINES) return C_HERO;
	return kLineParent[fam];
}
int ClassLine(int cls)
{
	if (cls >= C_JOB0 && cls < C_JOB0 + TAC_JOB_N) {
		static const int gear[8] = { 0, 4, 2, 3, 4, 5, 1, 3 };
		int fam = kJobFam[cls - C_JOB0];
		if (fam < 0 || fam >= 8) fam = 0;
		return gear[fam];
	}
	// 4次以降は親職と同じ系統の装備を使う。系統は店のタブと一致する。
	if (cls == C_GLADIATOR) return 1;
	if (cls == C_SHADOW) return 3;
	if (cls >= C_PLAYER_COUNT && cls < C_PLAYER_COUNT + TAC_RANK_LINES * TAC_RANK_N) {
		int line = (cls - C_PLAYER_COUNT) / TAC_RANK_N;
		return ClassLine(kLineParent[line]);
	}
	switch (cls) {
	case C_SOLDIER: case C_PALADIN: case C_HERO: case C_SKYKNIGHT: case C_DRAGONLORD: case C_RUNEKNIGHT: return 0;
	case C_FIGHTER: case C_BERSERKER: return 1;
	case C_ARCHER: case C_SNIPER: case C_PHANTOM: return 2;
	case C_THIEF: case C_ASSASSIN: return 3;
	case C_MAGE: case C_WIZARD: case C_ARCHMAGE: case C_ALCHEMIST: case C_ARCHALCHEMIST: return 4;
	default: return 5;
	}
}
int ClassTier(int cls)
{
	int t = ClassInfo(cls).tier;
	if (t < 1) return 1;
	if (t > 12) return 12;
	return t;
}
bool GearCanWear(int cls, const GearDef* g)
{
	if (!g) return false;
	int need = g->req > 0 ? g->req : 1;
	return ClassLine(cls) == g->line && ClassTier(cls) >= need;
}
int ItemExtraGet(const SaveData& s, int i)
{
	if (i < 0 || i >= IX_N) return 0;
	unsigned v = (unsigned)s.reserved[i >> 1];
	return (int)((v >> ((i & 1) * 16)) & 0xFFFF);
}
static int ExtraSell(int extra);
int ItemExtraAdd(SaveData& s, int i, int d)
{
	if (i < 0 || i >= IX_N) return 0;
	int c = ItemExtraGet(s, i) + d;
	if (c < 0) c = 0;
	int over = 0;
	if (c > TAC_HOLD_WIDE) { over = c - TAC_HOLD_WIDE; c = TAC_HOLD_WIDE; }
	unsigned shift = (unsigned)((i & 1) * 16);
	unsigned mask = 0xFFFFu << shift;
	unsigned v = (unsigned)s.reserved[i >> 1];
	v = (v & ~mask) | ((unsigned)c << shift);
	s.reserved[i >> 1] = (int)v;
	int gold = over > 0 ? over * ExtraSell(i) : 0;
	if (gold > 0) s.gold += gold;
	return gold;
}
static int MatSell(int it)
{
	switch (it) {
	case IT_ORE: return 12;
	case IT_HERB: return 10;
	case IT_CRYSTAL: return 24;
	case IT_SHARD: return 36;
	case IT_ELIXIR: return 20;
	case IT_MIGHT: return 35;
	case IT_DELAY: return 27;
	case IT_RAISE: return 80;
	default: return 8;
	}
}
int GainItemN(SaveData& s, int item, int n)
{
	if (item < 0 || item >= 8 || n == 0) return 0;
	int have = s.itemN[item];
	if (n < 0) { s.itemN[item] = max(0, have + n); return 0; }
	int room = TAC_HOLD_WIDE - have;
	if (room < 0) room = 0;
	int keep = n < room ? n : room;
	s.itemN[item] = have + keep;
	int over = n - keep;
	int gold = over > 0 ? over * MatSell(item) : 0;
	if (gold > 0) s.gold += gold;
	return gold;
}
int ConsumableCount(const SaveData& s)
{
	int n = s.itemN[IT_ELIXIR] + s.itemN[IT_MIGHT] + s.itemN[IT_DELAY] + s.itemN[IT_RAISE];
	for (int i = 0; i < IX_N; i++) n += ItemExtraGet(s, i);
	return n;
}
int MonsterDropLine(int monster)
{
	const wchar_t* n = MonsterLabel(monster);
	if (!n) return 0;
	struct Hit { const wchar_t* k; int line; };
	static const Hit hit[] = {
		{ L"Dragon", 0 }, { L"Tribal", 0 }, { L"Shark", 0 }, { L"Bull", 0 },
		{ L"Orc", 1 }, { L"Gole", 1 }, { L"Yeti", 1 }, { L"Trex", 1 }, { L"Stego", 1 }, { L"Tricera", 1 }, { L"Dino", 1 }, { L"Wolf", 1 }, { L"Whale", 1 }, { L"Stag", 1 },
		{ L"Spider", 2 }, { L"Wasp", 2 }, { L"Birb", 2 }, { L"Pigeon", 2 }, { L"Hywirl", 2 }, { L"Fish", 2 }, { L"fish", 2 }, { L"Manta", 2 }, { L"Wasp", 2 },
		{ L"Ghost", 3 }, { L"Ninja", 3 }, { L"Fox", 3 }, { L"Raptor", 3 }, { L"Snake", 3 }, { L"Rat", 3 }, { L"Cat", 3 },
		{ L"Wizard", 4 }, { L"Demon", 4 }, { L"Alien", 4 }, { L"Mush", 4 },
		{ L"Blob", 5 }, { L"Glub", 5 }, { L"Cactoro", 5 }, { L"Frog", 5 }, { L"Bunny", 5 },
	};
	for (int i = 0; i < (int)(sizeof(hit) / sizeof(hit[0])); i++) if (wcsstr(n, hit[i].k)) return hit[i].line;
	return monster % 6;
}
bool StashAdd(SaveData& s, int id)
{
	if (id < 1 || id > GearCount() || s.gearN < 0 || s.gearN >= GEAR_STASH) return false;
	s.gearId[s.gearN++] = id;
	return true;
}
void StashRemoveAt(SaveData& s, int index)
{
	if (index < 0 || index >= s.gearN) return;
	for (int i = index; i < s.gearN - 1; i++) s.gearId[i] = s.gearId[i + 1];
	s.gearN--;
	s.gearId[s.gearN] = 0;
}
bool GearBuy(SaveData& s, int index)
{
	if (index < 0 || index >= GearCount()) return false;
	const GearDef* g = GearOf(index + 1);
	if (!g || s.gold < g->price) return false;
	if (!StashAdd(s, index + 1)) return false;
	s.gold -= g->price;
	return true;
}
bool GearSellAt(SaveData& s, int index)
{
	if (index < 0 || index >= s.gearN) return false;
	const GearDef* g = GearOf(s.gearId[index]);
	if (!g) return false;
	s.gold += max(1, g->price / 2);
	StashRemoveAt(s, index);
	return true;
}
static int GearPower(const GearDef* g)
{
	if (!g) return 0;
	if (g->kind == 0) return g->str + g->mag;
	return g->def + g->res;
}
bool GearIsSurplus(const SaveData& s, int gearId)
{
	const GearDef* g = GearOf(gearId);
	if (!g || g->line < 0 || g->line > 5) return false;
	int score = GearPower(g);
	int slot = g->kind ? 7 : 6;
	for (int i = 0; i < MAX_ROSTER; i++) {
		const CharData& c = s.roster[i];
		if (!c.used || !GearCanWear(c.cls, g)) continue;
		int have = 0;
		if (c.reserved[slot] > 0) {
			const GearDef* e = GearOf(c.reserved[slot]);
			if (e && e->kind == g->kind) have = GearPower(e);
		}
		if (score > have) return false;
	}
	return true;
}
int GearSellSurplus(SaveData& s)
{
	int n = 0;
	for (int i = s.gearN - 1; i >= 0; i--) {
		if (!GearIsSurplus(s, s.gearId[i])) continue;
		if (GearSellAt(s, i)) n++;
	}
	return n;
}
int GearBestUpgrade(const SaveData& s, int roster, int kind)
{
	if (roster < 0 || roster >= MAX_ROSTER || !s.roster[roster].used) return -1;
	if (kind < 0 || kind > 1) return -1;
	const CharData& c = s.roster[roster];
	int slot = kind ? 7 : 6;
	int have = 0;
	if (c.reserved[slot] > 0) {
		const GearDef* e = GearOf(c.reserved[slot]);
		if (e && e->kind == kind) have = GearPower(e);
	}
	int bestI = -1, best = have;
	for (int i = 0; i < s.gearN && i < GEAR_STASH; i++) {
		const GearDef* g = GearOf(s.gearId[i]);
		if (!g || g->kind != kind || !GearCanWear(c.cls, g)) continue;
		int sc = GearPower(g);
		if (sc > best) { best = sc; bestI = i; }
	}
	return bestI;
}
void GearEquip(CharData& c, SaveData& s, int stashIndex)
{
	if (stashIndex < 0 || stashIndex >= s.gearN) return;
	int id = s.gearId[stashIndex];
	const GearDef* g = GearOf(id);
	if (!g || !GearCanWear(c.cls, g)) return;
	int slot = g->kind ? 7 : 6;
	int old = c.reserved[slot];
	c.reserved[slot] = id;
	StashRemoveAt(s, stashIndex);
	if (old > 0) StashAdd(s, old);
}
int GearTryDrop(SaveData& s, int enemyLv, int monster, Rng& r)
{
	int pool[512], n = 0;
	if (enemyLv < 1) enemyLv = 1;
	// 必要段は転職と同じ閾値。敵がそのレベルで着られる段まで。高い段は売値が跳ねる。
	int cap = 1;
	for (int t = 1; t < 10; t++) {
		if (enemyLv < PromoteLevel(t)) break;
		cap = t + 1;
	}
	if (cap > 10) cap = 10;
	int line = MonsterDropLine(monster);
	for (int pass = 0; pass < 4 && n < 1; pass++) {
		int wantLine = (pass < 2) ? line : -1;
		int reqLo = 1, reqHi = cap;
		if (pass == 0 || pass == 2) reqLo = cap;
		else if (pass == 1) reqLo = cap > 1 ? cap - 1 : 1;
		for (int i = 0; i < GearCount() && n < 512; i++) {
			const GearDef* g = GearOf(i + 1);
			if (!g || g->shop) continue; // 属性・ユニークは店のみ
			int req = g->req > 0 ? g->req : 1;
			if (wantLine >= 0 && g->line != wantLine) continue;
			if (req < reqLo || req > reqHi) continue;
			pool[n++] = i;
		}
	}
	if (n < 1) return -1;
	int gi = pool[r.Range(0, n - 1)];
	if (!StashAdd(s, gi + 1)) {
		s.gold += max(1, GearOf(gi + 1) ? GearOf(gi + 1)->price / 3 : 1);
		return -1;
	}
	return gi + 1;
}
static int GoodGrant(SaveData& s, int i)
{
	const GoodDef* g = GoodAt(i);
	if (!g) return 0;
	if (GoodHave(s, i) >= GoodHoldMax(i)) {
		int gold = max(1, g->price / 2);
		s.gold += gold;
		return gold;
	}
	if (g->item >= 0) return GainItemN(s, g->item, 1);
	if (g->extra >= 100) {
		CraftGoodAdd(g->extra - 100, 1);
		return 0;
	}
	return ItemExtraAdd(s, g->extra, 1);
}
bool DropConsumable(SaveData& s, int monster, Rng& r, wchar_t* name, int nameN)
{
	(void)monster;
	int n = GoodCount();
	if (n < 1) return false;
	int a = r.Range(0, n - 1);
	int cashed = GoodGrant(s, a);
	wchar_t buf[160];
	wcscpy_s(buf, TT(GoodAt(a)->nameTx));
	if (n > 1 && r.Chance(45)) {
		int b = r.Range(0, n - 2);
		if (b >= a) b++;
		cashed += GoodGrant(s, b);
		wcscat_s(buf, L"  ");
		wcscat_s(buf, TT(GoodAt(b)->nameTx));
	}
	if (cashed > 0) {
		wchar_t g[32];
		swprintf_s(g, L" +%dG", cashed);
		wcscat_s(buf, g);
	}
	if (name && nameN > 0) wcsncpy_s(name, nameN, buf, _TRUNCATE);
	return true;
}
static const GoodDef kGood[] = {
	{ 40, IT_ELIXIR, -1, TX_IT_ELIXIR, 0 },
	{ 70, IT_MIGHT, -1, TX_IT_MIGHT, 0 },
	{ 55, IT_DELAY, -1, TX_IT_DELAY, 0 },
	{ 160, IT_RAISE, -1, TX_IT_RAISE, 0 },
	{ 90, -1, IX_FULL, TX_IT_FULL, 0 },
	{ 80, -1, IX_ATK, TX_IT_ATK, 0 },
	{ 80, -1, IX_DEF, TX_IT_DEF, 0 },
	{ 80, -1, IX_AGI, TX_IT_AGI, 0 },
	{ 60, -1, IX_SLOW, TX_IT_SLOW, 0 },
	{ 60, -1, IX_VENOM, TX_IT_VENOM, 0 },
	{ 50, -1, IX_CURE, TX_IT_CURE, 0 },
	{ 70, -1, IX_ETHER, TX_IT_ETHER, 0 },
	{ 45, -1, 100, TX_GD_WAKE, 0 },
	{ 45, -1, 101, TX_GD_CALM, 0 },
	{ 45, -1, 102, TX_GD_SALT, 0 },
	{ 120, -1, 103, TX_GD_PANACEA, 0 },
	{ 90, -1, 104, TX_GD_WELL, 0 },
	{ 80, -1, 105, TX_GD_RALLY, 0 },
	{ 220, -1, 106, TX_GD_RETURN, 0 },
	{ 70, -1, 107, TX_GD_BANE, 0 },
	{ 70, -1, 108, TX_GD_FOG, 0 },
	{ 80, -1, 109, TX_GD_BLESS, 0 },
	{ 85, -1, 110, TX_GD_IRON, 0 },
	{ 85, -1, 111, TX_GD_STEP, 0 },
	{ 55, -1, 112, TX_GD_SOOTHE, 0 },
	{ 50, -1, 113, TX_GD_THAW, 0 },
};
int GoodCount() { return (int)(sizeof(kGood) / sizeof(kGood[0])); }
const GoodDef* GoodAt(int i) { return (i >= 0 && i < GoodCount()) ? &kGood[i] : NULL; }
int GoodHave(const SaveData& s, int i)
{
	const GoodDef* g = GoodAt(i);
	if (!g) return 0;
	if (g->item >= 0) return s.itemN[g->item];
	if (g->extra >= 100) return CraftGoodGet(g->extra - 100);
	return ItemExtraGet(s, g->extra);
}
int GoodHoldMax(int i)
{
	const GoodDef* g = GoodAt(i);
	if (!g) return TAC_HOLD_WIDE;
	if (g->extra >= 100) return TAC_HOLD_BYTE;
	return TAC_HOLD_WIDE;
}
static int ExtraSell(int extra)
{
	for (int i = 0; i < GoodCount(); i++) {
		const GoodDef* g = GoodAt(i);
		if (g && g->extra == extra && g->price > 0) return max(1, g->price / 2);
	}
	return 8;
}
bool GoodBuy(SaveData& s, int i)
{
	const GoodDef* g = GoodAt(i);
	if (!g || s.gold < g->price || GoodHave(s, i) >= GoodHoldMax(i)) return false;
	s.gold -= g->price;
	if (g->item >= 0) s.itemN[g->item]++;
	else if (g->extra >= 100) CraftGoodAdd(g->extra - 100, 1);
	else ItemExtraAdd(s, g->extra, 1);
	return true;
}
bool GoodTake(SaveData& s, int i)
{
	if (GoodHave(s, i) < 1) return false;
	const GoodDef* g = GoodAt(i);
	if (g->item >= 0) s.itemN[g->item]--;
	else if (g->extra >= 100) CraftGoodAdd(g->extra - 100, -1);
	else ItemExtraAdd(s, g->extra, -1);
	return true;
}

// ============================================================ セーブ
static const DWORD kSaveMagic = 0x31434154; // 'TAC1'

// 敵部位と討伐数。セーブ本体の大きさは変えず、部位ファイルに積む。
static const int kTrophyN = 40;
static unsigned short g_trophy[40];
enum { FOE_KILL_N = M_COUNT + 1024 };
static unsigned short g_foeKill[FOE_KILL_N];
static const DWORD kTrophyMagic = 0x54525054u;

int FoeKills(int monster)
{
	if (monster < 0 || monster >= FOE_KILL_N) return 0;
	return g_foeKill[monster];
}
void FoeNoteKill(int monster)
{
	if (monster < 0 || monster >= FOE_KILL_N) return;
	if (g_foeKill[monster] < 65535) g_foeKill[monster]++;
}
int FoeMasteryPct(int kills)
{
	if (kills <= 0) return 0;
	int p = 30 * kills / (kills + 12);
	if (p < 1) p = 1;
	if (p > 30) p = 30;
	return p;
}
const wchar_t* FoeSlainWord()
{
	return LL14(L"討伐", L"Slain", L"Abattus", L"Uccisi", L"Derrotas", L"처치", L"讨伐", L"المهزوم", L"Побеждено", L"Besiegt", L"Abatidos", L"Verslagen", L"Pokonani", L"Yenilen");
}
const wchar_t* FoeMasteryWord()
{
	return LL14(L"熟練", L"Mastery", L"Maîtrise", L"Maestria", L"Maestría", L"숙련", L"熟练", L"إتقان", L"Мастерство", L"Meisterschaft", L"Maestria", L"Meesterschap", L"Biegłość", L"Ustalık");
}

// セーブスロット。0 番は従来のファイル名のまま（いまの進行がスロット1）。
enum { SLF_SAVE = 0, SLF_PART, SLF_CRAFT, SLF_MID, SLF_AUTO, SLF_N };
static const TCHAR* kSlotBookLeaf = _T("oggYSEDbgmu_tac_slots.dat");
static const DWORD kSlotMagic = 0x544C5354u;
static int g_slot = 0;
static wchar_t g_slotName[TAC_SLOT_N][TAC_SLOT_NAME];
static bool g_slotReady = false;
static TCHAR g_leafSave[96];
static TCHAR g_leafPart[96];
static TCHAR g_leafCraft[96];
static TCHAR g_leafMid[96];
static TCHAR g_leafAuto[96];

struct SlotBook {
	DWORD magic;
	int version;
	int current;
	wchar_t name[TAC_SLOT_N][TAC_SLOT_NAME];
};

static void SlotLeaf(TCHAR* out, int cap, int kind, int slot)
{
	const TCHAR* base =
		kind == SLF_PART ? _T("oggYSEDbgmu_tac_part") :
		kind == SLF_CRAFT ? _T("oggYSEDbgmu_tac_craft") :
		kind == SLF_MID ? _T("oggYSEDbgmu_tac_mid") :
		kind == SLF_AUTO ? _T("oggYSEDbgmu_tac_auto") :
		_T("oggYSEDbgmu_tactics");
	if (slot <= 0) _stprintf_s(out, cap, _T("%s.dat"), base);
	else _stprintf_s(out, cap, _T("%s_s%d.dat"), base, slot + 1);
}
static void SlotApply()
{
	SlotLeaf(g_leafSave, 96, SLF_SAVE, g_slot);
	SlotLeaf(g_leafPart, 96, SLF_PART, g_slot);
	SlotLeaf(g_leafCraft, 96, SLF_CRAFT, g_slot);
	SlotLeaf(g_leafMid, 96, SLF_MID, g_slot);
	SlotLeaf(g_leafAuto, 96, SLF_AUTO, g_slot);
}
static void SlotDefaultName(wchar_t* out, int n, int slot)
{
	const wchar_t* p = LangPick14(
		L"スロット", L"Slot ", L"Slot ", L"Slot ", L"Ranura ",
		L"슬롯 ", L"存档", L"خانة ", L"Слот ", L"Slot ",
		L"Slot ", L"Slot ", L"Slot ", L"Yuva ");
	swprintf_s(out, n, L"%s%d", p, slot + 1);
}
static void SlotWriteBook()
{
	SlotBook b = {};
	b.magic = kSlotMagic;
	b.version = 1;
	b.current = g_slot;
	memcpy(b.name, g_slotName, sizeof(g_slotName));
	CString path = DatArc_Path(kSlotBookLeaf);
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("wb")) != 0 || !fp) return;
	fwrite(&b, 1, sizeof(b), fp);
	fclose(fp);
	DatArc_Commit(kSlotBookLeaf);
}
void SlotBoot()
{
	if (g_slotReady) return;
	g_slotReady = true;
	memset(g_slotName, 0, sizeof(g_slotName));
	g_slot = 0;
	CString path = DatArc_Path(kSlotBookLeaf);
	FILE* fp = NULL;
	bool got = false;
	if (_tfopen_s(&fp, path, _T("rb")) == 0 && fp) {
		SlotBook b = {};
		if (fread(&b, 1, sizeof(b), fp) == sizeof(b) && b.magic == kSlotMagic && b.version == 1) {
			if (b.current >= 0 && b.current < TAC_SLOT_N) g_slot = b.current;
			memcpy(g_slotName, b.name, sizeof(g_slotName));
			for (int i = 0; i < TAC_SLOT_N; i++) g_slotName[i][TAC_SLOT_NAME - 1] = 0;
			got = true;
		}
		fclose(fp);
	}
	SlotApply();
	if (!got) SlotWriteBook();
}
int SlotCurrent() { SlotBoot(); return g_slot; }
void SlotTitle(int slot, wchar_t* out, int n)
{
	SlotBoot();
	if (!out || n < 2) return;
	if (slot < 0 || slot >= TAC_SLOT_N) { out[0] = 0; return; }
	if (g_slotName[slot][0]) wcsncpy_s(out, n, g_slotName[slot], _TRUNCATE);
	else SlotDefaultName(out, n, slot);
}
void SlotRename(int slot, const wchar_t* name)
{
	SlotBoot();
	if (slot < 0 || slot >= TAC_SLOT_N) return;
	wchar_t tmp[TAC_SLOT_NAME];
	tmp[0] = 0;
	if (name) wcsncpy_s(tmp, name, _TRUNCATE);
	wchar_t def[TAC_SLOT_NAME];
	SlotDefaultName(def, TAC_SLOT_NAME, slot);
	if (!tmp[0] || wcscmp(tmp, def) == 0) g_slotName[slot][0] = 0;
	else wcsncpy_s(g_slotName[slot], tmp, _TRUNCATE);
	SlotWriteBook();
}
void SlotSelect(int slot)
{
	SlotBoot();
	if (slot < 0 || slot >= TAC_SLOT_N) return;
	g_slot = slot;
	SlotApply();
	SlotWriteBook();
}
void SlotWipe(int slot)
{
	SlotBoot();
	if (slot < 0 || slot >= TAC_SLOT_N) return;
	DatArc_FlushSuspend(TRUE);
	for (int k = 0; k < SLF_N; k++) {
		TCHAR leaf[96];
		SlotLeaf(leaf, 96, k, slot);
		DatArc_Delete(leaf);
	}
	DatArc_FlushSuspend(FALSE);
}
void SlotClearRuntime()
{
	memset(g_trophy, 0, sizeof(g_trophy));
	memset(g_foeKill, 0, sizeof(g_foeKill));
	CraftReset();
}
static bool CopyOneLeaf(const TCHAR* from, const TCHAR* to)
{
	if (!from || !from[0] || !to || !to[0]) return false;
	if (!DatArc_Exists(from)) {
		DatArc_Delete(to);
		return true;
	}
	CString sp = DatArc_Path(from);
	CString dp = DatArc_Path(to);
	FILE* in = NULL;
	if (_tfopen_s(&in, sp, _T("rb")) != 0 || !in) return false;
	fseek(in, 0, SEEK_END);
	long sz = ftell(in);
	fseek(in, 0, SEEK_SET);
	if (sz < 0 || sz > 64 * 1024 * 1024) { fclose(in); return false; }
	BYTE* buf = sz ? new BYTE[(size_t)sz] : NULL;
	bool rd = !sz || (buf && fread(buf, 1, (size_t)sz, in) == (size_t)sz);
	fclose(in);
	if (!rd) { delete[] buf; return false; }
	FILE* out = NULL;
	if (_tfopen_s(&out, dp, _T("wb")) != 0 || !out) { delete[] buf; return false; }
	bool ok = !sz || fwrite(buf, 1, (size_t)sz, out) == (size_t)sz;
	fclose(out);
	delete[] buf;
	if (!ok) return false;
	return DatArc_Commit(to) != FALSE;
}
bool SlotCopyFiles(int from, int to)
{
	SlotBoot();
	if (from < 0 || to < 0 || from >= TAC_SLOT_N || to >= TAC_SLOT_N || from == to) return false;
	DatArc_FlushSuspend(TRUE);
	bool ok = true;
	for (int k = 0; k < SLF_N; k++) {
		TCHAR a[96], b[96];
		SlotLeaf(a, 96, k, from);
		SlotLeaf(b, 96, k, to);
		if (!CopyOneLeaf(a, b)) ok = false;
	}
	DatArc_FlushSuspend(FALSE);
	return ok;
}

static void TacPut32(BYTE* p, unsigned v)
{
	p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); p[2] = (BYTE)(v >> 16); p[3] = (BYTE)(v >> 24);
}
static unsigned TacGet32(const BYTE* p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static const BYTE kTacKey[32] = {
	0x6a, 0x1c, 0x91, 0x4e, 0xd3, 0x27, 0xb8, 0x55,
	0x0f, 0xe2, 0x73, 0xac, 0x19, 0x86, 0x44, 0xcf,
	0x38, 0x5d, 0xa1, 0x62, 0x9b, 0x07, 0xf4, 0x2c,
	0xc8, 0x51, 0x7e, 0xd0, 0x16, 0x8a, 0x3b, 0xe9
};
static bool TacAesGcm(bool enc, const BYTE* in, unsigned inLen, BYTE* out, BYTE nonce[12], BYTE tag[16])
{
	if (!in || !out || !inLen) return false;
	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_KEY_HANDLE key = NULL;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0) != 0) return false;
	if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0) {
		BCryptCloseAlgorithmProvider(alg, 0);
		return false;
	}
	NTSTATUS st = BCryptGenerateSymmetricKey(alg, &key, NULL, 0, (PUCHAR)kTacKey, 32, 0);
	if (st != 0) { BCryptCloseAlgorithmProvider(alg, 0); return false; }
	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
	BCRYPT_INIT_AUTH_MODE_INFO(info);
	info.pbNonce = nonce;
	info.cbNonce = 12;
	info.pbTag = tag;
	info.cbTag = 16;
	ULONG done = 0;
	if (enc) st = BCryptEncrypt(key, (PUCHAR)in, inLen, &info, NULL, 0, out, inLen, &done, 0);
	else st = BCryptDecrypt(key, (PUCHAR)in, inLen, &info, NULL, 0, out, inLen, &done, 0);
	BCryptDestroyKey(key);
	BCryptCloseAlgorithmProvider(alg, 0);
	return st == 0 && done == inLen;
}
static BYTE* TacReadLeafBytes(const TCHAR* leaf, unsigned* sz)
{
	*sz = 0;
	if (!leaf || !DatArc_Exists(leaf)) return NULL;
	CString path = DatArc_Path(leaf);
	FILE* in = NULL;
	if (_tfopen_s(&in, path, _T("rb")) != 0 || !in) return NULL;
	fseek(in, 0, SEEK_END);
	long n = ftell(in);
	fseek(in, 0, SEEK_SET);
	if (n < 0 || n > 16 * 1024 * 1024) { fclose(in); return NULL; }
	BYTE* b = new BYTE[n ? (size_t)n : 1];
	bool ok = !n || (fread(b, 1, (size_t)n, in) == (size_t)n);
	fclose(in);
	if (!ok) { delete[] b; return NULL; }
	*sz = (unsigned)n;
	return b;
}
static bool TacWriteLeafBytes(const TCHAR* leaf, const BYTE* b, unsigned n)
{
	CString path = DatArc_Path(leaf);
	FILE* out = NULL;
	if (_tfopen_s(&out, path, _T("wb")) != 0 || !out) return false;
	bool ok = !n || (b && fwrite(b, 1, n, out) == n);
	fclose(out);
	if (!ok) return false;
	return DatArc_Commit(leaf) != FALSE;
}
bool SlotExportTo(int slot, const wchar_t* path)
{
	SlotBoot();
	if (!path || !path[0] || slot < 0 || slot >= TAC_SLOT_N) return false;
	BYTE* part[SLF_N] = {};
	unsigned psz[SLF_N] = {};
	// 魔数・版・名前・ファイル数。ファイル数の4バイトを入れ忘れると、末尾がヒープを越える。
	unsigned plainCap = 12u + (unsigned)(TAC_SLOT_NAME * sizeof(wchar_t));
	int nfiles = 0;
	for (int k = 0; k < SLF_N; k++) {
		TCHAR leaf[96];
		SlotLeaf(leaf, 96, k, slot);
		part[k] = TacReadLeafBytes(leaf, &psz[k]);
		if (!part[k] && DatArc_Exists(leaf)) {
			for (int i = 0; i < SLF_N; i++) delete[] part[i];
			return false;
		}
		if (part[k]) {
			if (plainCap > 32u * 1024u * 1024u || psz[k] > 32u * 1024u * 1024u - 8u - plainCap) {
				for (int i = 0; i < SLF_N; i++) delete[] part[i];
				return false;
			}
			plainCap += 8 + psz[k];
			nfiles++;
		}
	}
	BYTE* plain = new BYTE[plainCap];
	unsigned o = 0;
	auto put = [&](const void* src, unsigned n) -> bool {
		if (n > plainCap - o) return false;
		if (n && src) memcpy(plain + o, src, n);
		o += n;
		return true;
	};
	BYTE b4[4];
	TacPut32(b4, 0x50434154u);
	bool fit = put(b4, 4);
	TacPut32(b4, 2);
	fit = fit && put(b4, 4);
	wchar_t nm[TAC_SLOT_NAME];
	memset(nm, 0, sizeof(nm));
	if (g_slotName[slot][0]) wcsncpy_s(nm, g_slotName[slot], _TRUNCATE);
	fit = fit && put(nm, (unsigned)sizeof(nm));
	TacPut32(b4, (unsigned)nfiles);
	fit = fit && put(b4, 4);
	for (int k = 0; k < SLF_N && fit; k++) {
		if (!part[k]) continue;
		TacPut32(b4, (unsigned)k);
		fit = put(b4, 4);
		TacPut32(b4, psz[k]);
		fit = fit && put(b4, 4);
		if (psz[k]) fit = fit && put(part[k], psz[k]);
	}
	for (int k = 0; k < SLF_N; k++) { delete[] part[k]; part[k] = NULL; }
	if (!fit) { delete[] plain; return false; }
	size_t bound = ZSTD_compressBound(o);
	BYTE* comp = new BYTE[bound];
	size_t csz = ZSTD_compress(comp, bound, plain, o, 6);
	delete[] plain;
	bool ok = false;
	if (!ZSTD_isError(csz) && csz > 0 && csz < 64u * 1024u * 1024u) {
		BYTE* ciph = new BYTE[csz];
		BYTE nonce[12], tag[16];
		memset(tag, 0, sizeof(tag));
		if (BCryptGenRandom(NULL, nonce, 12, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0
			&& TacAesGcm(true, comp, (unsigned)csz, ciph, nonce, tag)) {
			FILE* fp = NULL;
			if (_wfopen_s(&fp, path, L"wb") == 0 && fp) {
				BYTE hdr[36];
				memcpy(hdr, "OTS1", 4);
				memcpy(hdr + 4, nonce, 12);
				memcpy(hdr + 16, tag, 16);
				TacPut32(hdr + 32, (unsigned)csz);
				ok = fwrite(hdr, 1, 36, fp) == 36 && fwrite(ciph, 1, csz, fp) == csz;
				fclose(fp);
				if (!ok) _wremove(path);
			}
		}
		delete[] ciph;
	}
	delete[] comp;
	return ok;
}
bool SlotImportFrom(int slot, const wchar_t* path)
{
	SlotBoot();
	if (!path || !path[0] || slot < 0 || slot >= TAC_SLOT_N) return false;
	FILE* fp = NULL;
	if (_wfopen_s(&fp, path, L"rb") != 0 || !fp) return false;
	fseek(fp, 0, SEEK_END);
	long fsz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (fsz < 36 || fsz > 64 * 1024 * 1024) { fclose(fp); return false; }
	BYTE* file = new BYTE[(size_t)fsz];
	bool rd = fread(file, 1, (size_t)fsz, fp) == (size_t)fsz;
	fclose(fp);
	if (!rd || memcmp(file, "OTS1", 4) != 0) { delete[] file; return false; }
	BYTE nonce[12], tag[16];
	memcpy(nonce, file + 4, 12);
	memcpy(tag, file + 16, 16);
	unsigned csz = TacGet32(file + 32);
	if (csz == 0 || 36u + csz > (unsigned)fsz) { delete[] file; return false; }
	BYTE* comp = new BYTE[csz];
	bool dec = TacAesGcm(false, file + 36, csz, comp, nonce, tag);
	delete[] file;
	if (!dec) { delete[] comp; return false; }
	unsigned long long need = ZSTD_getFrameContentSize(comp, csz);
	if (need < 12ull + (unsigned long long)TAC_SLOT_NAME * sizeof(wchar_t) || need > 32ull * 1024ull * 1024ull) { delete[] comp; return false; }
	BYTE* plain = new BYTE[(size_t)need];
	size_t got = ZSTD_decompress(plain, (size_t)need, comp, csz);
	delete[] comp;
	if (ZSTD_isError(got) || got != (size_t)need) { delete[] plain; return false; }
	if (TacGet32(plain) != 0x50434154u || (TacGet32(plain + 4) != 1 && TacGet32(plain + 4) != 2)) { delete[] plain; return false; }
	wchar_t nm[TAC_SLOT_NAME];
	memcpy(nm, plain + 8, sizeof(nm));
	nm[TAC_SLOT_NAME - 1] = 0;
	unsigned nfiles = TacGet32(plain + 8 + sizeof(nm));
	unsigned o = 8 + (unsigned)sizeof(nm) + 4;
	BYTE* part[SLF_N] = {};
	unsigned psz[SLF_N] = {};
	bool have[SLF_N] = {};
	bool bad = nfiles > 8;
	for (unsigned i = 0; !bad && i < nfiles; i++) {
		if (o + 8 > got) { bad = true; break; }
		unsigned kind = TacGet32(plain + o); o += 4;
		unsigned sz = TacGet32(plain + o); o += 4;
		if (kind >= SLF_N || o + sz > got || sz > 16u * 1024u * 1024u) { bad = true; break; }
		delete[] part[kind];
		part[kind] = new BYTE[sz ? sz : 1];
		if (sz) memcpy(part[kind], plain + o, sz);
		psz[kind] = sz;
		have[kind] = true;
		o += sz;
	}
	delete[] plain;
	if (bad) {
		for (int k = 0; k < SLF_N; k++) delete[] part[k];
		return false;
	}
	DatArc_FlushSuspend(TRUE);
	bool ok = true;
	for (int k = 0; k < SLF_N; k++) {
		TCHAR leaf[96];
		SlotLeaf(leaf, 96, k, slot);
		if (have[k]) {
			if (!TacWriteLeafBytes(leaf, part[k], psz[k])) ok = false;
		} else DatArc_Delete(leaf);
		delete[] part[k];
	}
	DatArc_FlushSuspend(FALSE);
	if (ok) SlotRename(slot, nm);
	return ok;
}
static int TacPeekClearBytes(FILE* fp, long sz);
int SlotPeekClear(int slot)
{
	SlotBoot();
	if (slot < 0 || slot >= TAC_SLOT_N) return -1;
	TCHAR leaf[96];
	SlotLeaf(leaf, 96, SLF_SAVE, slot);
	if (!DatArc_Exists(leaf)) return -1;
	CString path = DatArc_Path(leaf);
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("rb")) != 0 || !fp) return -1;
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	int clear = TacPeekClearBytes(fp, sz);
	fclose(fp);
	return clear;
}
const TCHAR* TacLeafSave() { SlotBoot(); return g_leafSave; }
const TCHAR* TacLeafPart() { SlotBoot(); return g_leafPart; }
const TCHAR* TacLeafCraft() { SlotBoot(); return g_leafCraft; }
const TCHAR* TacLeafMid() { SlotBoot(); return g_leafMid; }
const TCHAR* TacLeafAuto() { SlotBoot(); return g_leafAuto; }

static void TrophyLoad()
{
	memset(g_trophy, 0, sizeof(g_trophy));
	memset(g_foeKill, 0, sizeof(g_foeKill));
	CString path = DatArc_Path(TacLeafPart());
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("rb")) != 0 || !fp) return;
	DWORD magic = 0; int ver = 0;
	if (fread(&magic, 4, 1, fp) == 1 && fread(&ver, 4, 1, fp) == 1 && magic == kTrophyMagic && (ver == 1 || ver == 2)) {
		if (fread(g_trophy, sizeof(g_trophy), 1, fp) == 1 && ver == 2) {
			int n = 0;
			if (fread(&n, 4, 1, fp) == 1 && n > 0 && n <= 4096) {
				int take = n < FOE_KILL_N ? n : FOE_KILL_N;
				fread(g_foeKill, sizeof(unsigned short), (size_t)take, fp);
			}
		}
	}
	fclose(fp);
}
static void TrophySave()
{
	CString path = DatArc_Path(TacLeafPart());
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("wb")) != 0 || !fp) return;
	int ver = 2;
	int n = FOE_KILL_N;
	fwrite(&kTrophyMagic, 4, 1, fp);
	fwrite(&ver, 4, 1, fp);
	fwrite(g_trophy, sizeof(g_trophy), 1, fp);
	fwrite(&n, 4, 1, fp);
	fwrite(g_foeKill, sizeof(g_foeKill), 1, fp);
	fclose(fp);
	DatArc_Commit(TacLeafPart());
}
static int TrophyId(unsigned short s) { return s & 1023; }
static int TrophyCt(unsigned short s) { return (s >> 10) & 63; }
static unsigned short TrophyPack(int id, int ct)
{
	if (ct < 0) ct = 0;
	if (ct > 63) ct = 63;
	return (unsigned short)((ct << 10) | (id & 1023));
}
static void SpeciesParts(int monster, int part[3])
{
	part[0] = 0; part[1] = 1; part[2] = 8;
	const MonsterDef& md = MonsterInfo(monster);
	if (md.look == LK_MESH && md.model >= 0 && md.model < kImportN) {
		int i = md.model;
		if (i == 0 || i == 67 || i == 68) { part[0] = 0; part[1] = 5; part[2] = 1; return; }
		if (i == 1 || i == 2 || i == 3 || i == 9) { part[0] = 0; part[1] = 2; part[2] = 1; return; }
		if (i == 4 || i == 6) { part[0] = 0; part[1] = 3; part[2] = 4; return; }
		if (i == 5 || i == 7 || i == 8 || i == 10 || i == 75) { part[0] = 0; part[1] = 6; part[2] = 7; return; }
		if (i == 44) { part[0] = 16; part[1] = 1; part[2] = 8; return; }
		if (i == 47 || i == 48 || i == 50 || i == 52 || i == 78) { part[0] = 0; part[1] = 8; part[2] = 6; return; }
		if (i == 49) { part[0] = 0; part[1] = 21; part[2] = 8; return; }
		if (i == 51) { part[0] = 0; part[1] = 2; part[2] = 1; return; }
		if (i == 53 || i == 82) { part[0] = 0; part[1] = 16; part[2] = 1; return; }
		if (i == 54 || i == 73) { part[0] = 0; part[1] = 7; part[2] = 8; return; }
		if (i == 55 || i == 56) { part[0] = 1; part[1] = 6; part[2] = 17; return; }
		if (i == 57) { part[0] = 18; part[1] = 6; part[2] = 17; return; }
		if (i == 58 || i == 69 || i == 70) { part[0] = 9; part[1] = 19; part[2] = 17; return; }
		if (i == 59 || i == 65) { part[0] = 15; part[1] = 8; part[2] = 1; return; }
		if (i == 25 || i == 37 || i == 64) { part[0] = 12; part[1] = 6; part[2] = 14; return; }
		if ((i >= 11 && i <= 46) || (i >= 60 && i <= 63) || i == 81) { part[0] = 12; part[1] = 13; part[2] = 8; return; }
		if (i == 66) { part[0] = 16; part[1] = 31; part[2] = 28; return; }
		if (i == 71 || i == 76 || i == 99) { part[0] = 11; part[1] = 9; part[2] = 10; return; }
		if (i == 72 || i == 77) { part[0] = 2; part[1] = 20; part[2] = 24; return; }
		if (i == 74) { part[0] = 19; part[1] = 26; part[2] = 25; return; }
		if (i == 79 || i == 80) { part[0] = 22; part[1] = 23; part[2] = 6; return; }
		if (i == 83 || i == 84) { part[0] = 24; part[1] = 8; part[2] = 28; return; }
		if (i == 85 || i == 86 || i == 89 || i == 90 || i == 100) { part[0] = 16; part[1] = 28; part[2] = 1; return; }
		if (i == 87 || i == 88) { part[0] = 27; part[1] = 28; part[2] = 8; return; }
		if (i == 91) { part[0] = 9; part[1] = 20; part[2] = 28; return; }
		if (i == 92) { part[0] = 0; part[1] = 20; part[2] = 6; return; }
		if (i == 93 || i == 94 || i == 95) { part[0] = 25; part[1] = 26; part[2] = 16; return; }
		if (i == 96) { part[0] = 19; part[1] = 1; part[2] = 24; return; }
		if (i == 97 || i == 98) { part[0] = 0; part[1] = 6; part[2] = 1; return; }
		if (i == 101) { part[0] = 30; part[1] = 31; part[2] = 10; return; }
		if (i == 102) { part[0] = 6; part[1] = 9; part[2] = 8; return; }
		if (i == 103) { part[0] = 26; part[1] = 16; part[2] = 28; return; }
		if (i == 104) { part[0] = 7; part[1] = 6; part[2] = 29; return; }
		return;
	}
	switch (monster) {
	case M_SLIME: part[0] = 16; part[1] = 28; part[2] = 1; break;
	case M_POISON_SLIME: part[0] = 16; part[1] = 17; part[2] = 28; break;
	case M_SKEL_MINION: part[0] = 8; part[1] = 6; part[2] = 24; break;
	case M_SKEL_WARRIOR: part[0] = 8; part[1] = 6; part[2] = 2; break;
	case M_SKEL_ARCHER: part[0] = 8; part[1] = 6; part[2] = 9; break;
	case M_BAT: part[0] = 0; part[1] = 9; part[2] = 6; break;
	case M_LICH: part[0] = 8; part[1] = 24; part[2] = 28; break;
	case M_GOLEM: part[0] = 27; part[1] = 28; part[2] = 8; break;
	case M_WYVERN: part[0] = 22; part[1] = 6; part[2] = 9; break;
	case M_DARK_KNIGHT: part[0] = 8; part[1] = 6; part[2] = 24; break;
	case M_DRAGON: part[0] = 22; part[1] = 23; part[2] = 6; break;
	default: break;
	}
}
static int TrophyKey(int monster, int which)
{
	const MonsterDef& md = MonsterInfo(monster);
	int base = (md.look == LK_MESH && md.model >= 0 && md.model < kImportN) ? md.model : (200 + Clampi(monster, 0, 40));
	return base * 3 + (which % 3) + 1;
}
static void TrophySplit(int key, int& monsterKey, int& which)
{
	int k = key - 1;
	if (k < 0) k = 0;
	which = k % 3;
	monsterKey = k / 3;
}
static const wchar_t* TrophyMonName(int monsterKey)
{
	if (monsterKey >= 0 && monsterKey < kImportN) return ImportLocName(monsterKey);
	int m = monsterKey - 200;
	if (m < 0 || m >= M_COUNT) return L"?";
	return TT(MonsterInfo(m).nameTx);
}
static void TrophyWriteName(wchar_t* name, int nameN, int key)
{
	int mk = 0, which = 0;
	TrophySplit(key, mk, which);
	int part[3];
	int mon = (mk >= 0 && mk < kImportN) ? (M_COUNT + mk) : (mk - 200);
	SpeciesParts(mon, part);
	const wchar_t* mn = TrophyMonName(mk);
	const wchar_t* pn = TT(TX_PART_FIRST + part[which]);
	int lang = savedata.lang;
	if (lang == 0) swprintf_s(name, nameN, L"%sの%s", mn, pn);
	else if (lang == 6) swprintf_s(name, nameN, L"%s的%s", mn, pn);
	else if (lang == 2 || lang == 4 || lang == 10) swprintf_s(name, nameN, L"%s de %s", pn, mn);
	else if (lang == 3) swprintf_s(name, nameN, L"%s di %s", pn, mn);
	else if (lang == 9) swprintf_s(name, nameN, L"%s von %s", pn, mn);
	else if (lang == 11) swprintf_s(name, nameN, L"%s van %s", pn, mn);
	else swprintf_s(name, nameN, L"%s %s", mn, pn);
}

static int TrophyPrice(int which) { return 8 + which * 5; }

static int TrophyStore(int key, int* goldOut)
{
	int price = 8;
	int mk = 0, which = 0;
	TrophySplit(key, mk, which);
	price = TrophyPrice(which);
	for (int i = 0; i < kTrophyN; i++) {
		if (TrophyId(g_trophy[i]) != key) continue;
		int ct = TrophyCt(g_trophy[i]);
		if (ct >= 63) { if (goldOut) *goldOut += price; return 1; }
		g_trophy[i] = TrophyPack(key, ct + 1);
		return 1;
	}
	for (int i = 0; i < kTrophyN; i++) {
		if (TrophyId(g_trophy[i]) != 0) continue;
		g_trophy[i] = TrophyPack(key, 1);
		return 1;
	}
	if (goldOut) *goldOut += price;
	return 1;
}

int DropTrophy(int monster, int boss, Rng& r, wchar_t* name, int nameN, int* goldOut)
{
	if (name && nameN > 0) name[0] = 0;
	if (goldOut) *goldOut = 0;
	int part[3];
	SpeciesParts(monster, part);
	int which = r.Range(0, 2);
	int key = TrophyKey(monster, which);
	int gold = 0;
	TrophyStore(key, &gold);
	if (name && nameN > 0) TrophyWriteName(name, nameN, key);
	if (goldOut) *goldOut = gold;
	(void)boss;
	(void)part;
	return 1;
}
int TrophyList(int* slots, int max)
{
	int n = 0;
	for (int i = 0; i < kTrophyN && n < max; i++) if (TrophyId(g_trophy[i]) > 0 && TrophyCt(g_trophy[i]) > 0) slots[n++] = i;
	return n;
}
bool TrophySell(SaveData& s, int slot)
{
	if (slot < 0 || slot >= kTrophyN) return false;
	int id = TrophyId(g_trophy[slot]);
	int ct = TrophyCt(g_trophy[slot]);
	if (id <= 0 || ct <= 0) return false;
	int mk = 0, which = 0;
	TrophySplit(id, mk, which);
	s.gold += max(1, TrophyPrice(which) * ct / 2);
	g_trophy[slot] = 0;
	return true;
}
bool TrophySellAll(SaveData& s)
{
	bool any = false;
	for (int i = 0; i < kTrophyN; i++) if (TrophySell(s, i)) any = true;
	return any;
}
void TrophyNameOf(int slot, wchar_t* name, int nameN, int* count, int* price)
{
	if (slot < 0 || slot >= kTrophyN) { if (name && nameN) name[0] = 0; return; }
	int id = TrophyId(g_trophy[slot]);
	int ct = TrophyCt(g_trophy[slot]);
	if (count) *count = ct;
	int mk = 0, which = 0;
	TrophySplit(id, mk, which);
	if (price) *price = max(1, TrophyPrice(which) * ct / 2);
	if (name && nameN > 0) TrophyWriteName(name, nameN, id);
}

int TacQualitySuggestFromGpu()
{
	static int s_cached = -1;
	if (s_cached >= 0) return s_cached;
	static const int kMap[10] = { 4, 4, 0, 1, 1, 2, 3, 5, 5, 6 };
	int s3 = S3GfxQualitySuggestFromGpu();
	if (s3 < 0) s3 = 0;
	if (s3 > 9) s3 = 9;
	s_cached = kMap[s3];
	return s_cached;
}

static int TacGpuNameHash()
{
	static int s_cached = 0;
	if (s_cached != 0) return s_cached;
	S3GpuProbe p = {};
	S3GpuProbePrimary(&p);
	unsigned h = 2166136261u;
	for (const wchar_t* n = p.name; n && *n; n++) {
		h ^= (unsigned)*n;
		h *= 16777619u;
	}
	if (h == 0) h = 1;
	s_cached = (int)h;
	return s_cached;
}

void TacQualityEnsureAutoDefault(SaveData& s)
{
	if (s.quality == TAC_Q_AUTO || s.spare[0] == 1) {
		int gpu = TacQualitySuggestFromGpu();
		int tag = TacGpuNameHash();
		BOOL newGpu = (s.spare[1] == 0 || s.spare[1] != tag);
		s.quality = TAC_Q_AUTO;
		s.spare[0] = 1;
		s.spare[1] = tag;
		s.spare[3] = gpu;
		if (newGpu || s.spare[2] < 0 || s.spare[2] > 6)
			s.spare[2] = 6;
		return;
	}
	if (s.quality < 0 || s.quality > 6) s.quality = 1;
}

void SaveDefaults(SaveData& s)
{
	memset(&s, 0, sizeof(s));
	s.magic = kSaveMagic;
	s.version = 7;
	for (int i = 0; i < MAX_PARTY; i++) s.party[i] = -1;
	s.stageSel = 1;
	s.camSpeed = 1;
	s.quality = TAC_Q_AUTO;
	s.spare[0] = 1;
	s.showGrid = 1;
	TacEnsureOpt(s);
	s.uidNext = 1;
	s.bgmVol = 6;
	s.seVol = 8;
	s.optInit = 1;
	s.partyCap = 5;
	s.rosterCap = 20;
	TacQualityEnsureAutoDefault(s);
}

#pragma pack(push, 4)
struct CharV4 {
	wchar_t name[NAME_LEN];
	int used;
	int gender;
	int cls;
	int lv;
	int exp;
	int stat[S_COUNT];
	unsigned __int64 skills;
	int body, hair, hairColor, outfit, hat, cape;
	int kills, battles;
	DWORD uid;
	int reserved[8];
};
struct CharSave10 {
	wchar_t name[NAME_LEN];
	int used;
	int gender;
	int cls;
	int lv;
	int exp;
	int stat[10];
	unsigned __int64 skills;
	int body, hair, hairColor, outfit, hat, cape;
	int kills, battles;
	DWORD uid;
	int reserved[8];
};
struct SaveV2 {
	DWORD magic;
	int version;
	CharSave10 roster[MAX_ROSTER];
	int party[10];
	int stageCleared, stageSel, winX, winY, winW, winH;
	int camSpeed, quality, showGrid, battleAnim;
	DWORD uidNext;
	int winSizeVer;
	int itemN[8];
	int bgmVol, seVol, optInit, gold, gearN, gearId[12], reserved[4];
	int partyCap, rosterCap;
};
struct SaveV1 {
	DWORD magic;
	int version;
	CharSave10 roster[20];
	int party[5];
	int stageCleared, stageSel, winX, winY, winW, winH;
	int camSpeed, quality, showGrid, battleAnim;
	DWORD uidNext;
	int winSizeVer;
	int itemN[8];
	int bgmVol, seVol, optInit, gold, gearN, gearId[12], reserved[4];
};
// version 3。能力は現行と同じで、編成だけ 10。お試し欄はまだ無い。
struct SaveV3 {
	DWORD magic;
	int version;
	CharV4 roster[MAX_ROSTER];
	int party[10];
	int stageCleared, stageSel, winX, winY, winW, winH;
	int camSpeed, quality, showGrid, battleAnim;
	DWORD uidNext;
	int winSizeVer;
	int itemN[8];
	int bgmVol, seVol, optInit, gold, gearN, gearId[12], reserved[4];
	int partyCap, rosterCap;
};
// version 4。編成 15。キャラはお試し欄の手前まで。
struct SaveV4 {
	DWORD magic;
	int version;
	CharV4 roster[MAX_ROSTER];
	int party[MAX_PARTY];
	int stageCleared, stageSel, winX, winY, winW, winH;
	int camSpeed, quality, showGrid, battleAnim;
	DWORD uidNext;
	int winSizeVer;
	int itemN[8];
	int bgmVol, seVol, optInit, gold, gearN, gearId[12], reserved[4];
	int partyCap, rosterCap;
};
// version 5。お試し欄まで。職履歴はまだ無い。
struct CharV5 {
	wchar_t name[NAME_LEN];
	int used;
	int gender;
	int cls;
	int lv;
	int exp;
	int stat[S_COUNT];
	unsigned __int64 skills;
	int body, hair, hairColor, outfit, hat, cape;
	int kills, battles;
	DWORD uid;
	int reserved[8];
	int trialFrom;
	int trialLv;
};
struct SaveV5 {
	DWORD magic;
	int version;
	CharV5 roster[MAX_ROSTER];
	int party[MAX_PARTY];
	int stageCleared, stageSel, winX, winY, winW, winH;
	int camSpeed, quality, showGrid, battleAnim;
	DWORD uidNext;
	int winSizeVer;
	int itemN[8];
	int bgmVol, seVol, optInit, gold, gearN, gearId[12], reserved[4];
	int partyCap, rosterCap;
};
#pragma pack(pop)
static_assert(sizeof(SaveV4) == sizeof(SaveV3) + 5 * sizeof(int), "編成 15 は version 3 より 5 枠");
static_assert(sizeof(CharV5) == sizeof(CharV4) + 2 * sizeof(int), "お試しは職とレベルの2欄");
static_assert(sizeof(SaveV5) == sizeof(SaveV4) + MAX_ROSTER * 2 * sizeof(int), "version 5 は名簿全員にお試し欄");
#pragma pack(push, 4)
struct CharV6 {
	wchar_t name[NAME_LEN];
	int used;
	int gender;
	int cls;
	int lv;
	int exp;
	int stat[S_COUNT];
	unsigned __int64 skills;
	int body, hair, hairColor, outfit, hat, cape;
	int kills, battles;
	DWORD uid;
	int reserved[8];
	int trialFrom;
	int trialLv;
	int jobPath[12];
	int jobDone;
};
struct SaveV6 {
	DWORD magic;
	int version;
	CharV6 roster[MAX_ROSTER];
	int party[MAX_PARTY];
	int stageCleared, stageSel, winX, winY, winW, winH;
	int camSpeed, quality, showGrid, battleAnim;
	DWORD uidNext;
	int winSizeVer;
	int itemN[8];
	int bgmVol, seVol, optInit, gold, gearN, gearId[12], reserved[4];
	int partyCap, rosterCap;
};
#pragma pack(pop)
static_assert(sizeof(CharData) == sizeof(CharV5) + (JOB_PATH_N + 1) * sizeof(int) + (24 + 16) * sizeof(int), "version 7 は技ビットと余り");
static_assert(sizeof(SaveData) == sizeof(SaveV5) + MAX_ROSTER * ((JOB_PATH_N + 1) + 24 + 16) * sizeof(int) + 32 * sizeof(int), "version 7");
static_assert(sizeof(CharV6) + (24 + 16) * sizeof(int) == sizeof(CharData), "v6 キャラに技欄を足す");

static int TacPeekClearBytes(FILE* fp, long sz)
{
	if (!fp) return 0;
	if (sz == (long)sizeof(SaveData)) {
		SaveData peek;
		if (fread(&peek, 1, sizeof(peek), fp) == sizeof(peek) && peek.magic == kSaveMagic)
			return peek.stageCleared < 0 ? 0 : peek.stageCleared;
	} else if (sz == (long)sizeof(SaveV6)) {
		SaveV6 peek;
		if (fread(&peek, 1, sizeof(peek), fp) == sizeof(peek) && peek.magic == kSaveMagic)
			return peek.stageCleared < 0 ? 0 : peek.stageCleared;
	} else if (sz == (long)sizeof(SaveV5)) {
		SaveV5 peek;
		if (fread(&peek, 1, sizeof(peek), fp) == sizeof(peek) && peek.magic == kSaveMagic)
			return peek.stageCleared < 0 ? 0 : peek.stageCleared;
	} else if (sz == (long)sizeof(SaveV4)) {
		SaveV4 peek;
		if (fread(&peek, 1, sizeof(peek), fp) == sizeof(peek) && peek.magic == kSaveMagic)
			return peek.stageCleared < 0 ? 0 : peek.stageCleared;
	} else if (sz == (long)sizeof(SaveV3)) {
		SaveV3 peek;
		if (fread(&peek, 1, sizeof(peek), fp) == sizeof(peek) && peek.magic == kSaveMagic)
			return peek.stageCleared < 0 ? 0 : peek.stageCleared;
	} else if (sz == (long)sizeof(SaveV2)) {
		SaveV2 peek;
		if (fread(&peek, 1, sizeof(peek), fp) == sizeof(peek) && peek.magic == kSaveMagic)
			return peek.stageCleared < 0 ? 0 : peek.stageCleared;
	} else if (sz == (long)sizeof(SaveV1)) {
		SaveV1 peek;
		if (fread(&peek, 1, sizeof(peek), fp) == sizeof(peek) && peek.magic == kSaveMagic)
			return peek.stageCleared < 0 ? 0 : peek.stageCleared;
	}
	return 0;
}

static void CopyCharV4(CharData& d, const CharV4& o)
{
	memset(&d, 0, sizeof(d));
	wcsncpy_s(d.name, o.name, _TRUNCATE);
	d.used = o.used;
	d.gender = o.gender;
	d.cls = o.cls;
	d.lv = o.lv;
	d.exp = o.exp;
	for (int s = 0; s < S_COUNT; s++) d.stat[s] = o.stat[s];
	d.skills = o.skills;
	d.body = o.body;
	d.hair = o.hair;
	d.hairColor = o.hairColor;
	d.outfit = o.outfit;
	d.hat = o.hat;
	d.cape = o.cape;
	d.kills = o.kills;
	d.battles = o.battles;
	d.uid = o.uid;
	for (int i = 0; i < 8; i++) d.reserved[i] = o.reserved[i];
	d.trialFrom = 0;
	d.trialLv = 0;
	JobPathClear(d);
	d.jobDone = 0;
}
static void CopyCharV5(CharData& d, const CharV5& o)
{
	memset(&d, 0, sizeof(d));
	wcsncpy_s(d.name, o.name, _TRUNCATE);
	d.used = o.used;
	d.gender = o.gender;
	d.cls = o.cls;
	d.lv = o.lv;
	d.exp = o.exp;
	for (int s = 0; s < S_COUNT; s++) d.stat[s] = o.stat[s];
	d.skills = o.skills;
	d.body = o.body;
	d.hair = o.hair;
	d.hairColor = o.hairColor;
	d.outfit = o.outfit;
	d.hat = o.hat;
	d.cape = o.cape;
	d.kills = o.kills;
	d.battles = o.battles;
	d.uid = o.uid;
	for (int i = 0; i < 8; i++) d.reserved[i] = o.reserved[i];
	d.trialFrom = o.trialFrom;
	d.trialLv = o.trialLv;
	JobPathClear(d);
	d.jobDone = 0;
}
static void MigrateSkillBits(CharData& d)
{
	d.skillBits[0] |= (int)(d.skills & 0xffffffffu);
	d.skillBits[1] |= (int)(d.skills >> 32);
	for (int i = 0; i < 6; i++) d.skillBits[2 + i] |= d.reserved[i];
}
static void CopyCharV6(CharData& d, const CharV6& o)
{
	memset(&d, 0, sizeof(d));
	wcsncpy_s(d.name, o.name, _TRUNCATE);
	d.used = o.used;
	d.gender = o.gender;
	d.cls = o.cls;
	d.lv = o.lv;
	d.exp = o.exp;
	for (int s = 0; s < S_COUNT; s++) d.stat[s] = o.stat[s];
	d.skills = o.skills;
	d.body = o.body;
	d.hair = o.hair;
	d.hairColor = o.hairColor;
	d.outfit = o.outfit;
	d.hat = o.hat;
	d.cape = o.cape;
	d.kills = o.kills;
	d.battles = o.battles;
	d.uid = o.uid;
	for (int i = 0; i < 8; i++) d.reserved[i] = o.reserved[i];
	d.trialFrom = o.trialFrom;
	d.trialLv = o.trialLv;
	for (int i = 0; i < JOB_PATH_N; i++) d.jobPath[i] = o.jobPath[i];
	d.jobDone = o.jobDone;
	MigrateSkillBits(d);
}
static void CopyChar10(CharData& d, const CharSave10& o)
{
	memset(&d, 0, sizeof(d));
	wcsncpy_s(d.name, o.name, _TRUNCATE);
	d.used = o.used;
	d.gender = o.gender;
	d.cls = o.cls;
	d.lv = o.lv;
	d.exp = o.exp;
	d.skills = o.skills;
	d.body = o.body;
	d.hair = o.hair;
	d.hairColor = o.hairColor;
	d.outfit = o.outfit;
	d.hat = o.hat;
	d.cape = o.cape;
	d.kills = o.kills;
	d.battles = o.battles;
	d.uid = o.uid;
	for (int i = 0; i < 8; i++) d.reserved[i] = o.reserved[i];
	JobPathClear(d);
	d.jobDone = 0;
	d.trialFrom = 0;
	d.trialLv = 0;
}
static void CopySaveTail(SaveData& s, int stageCleared, int stageSel, int winX, int winY, int winW, int winH,
	int camSpeed, int quality, int showGrid, int battleAnim, DWORD uidNext, int winSizeVer,
	const int* itemN, int bgmVol, int seVol, int optInit, int gold, int gearN, const int* gearId, const int* reserved,
	int partyCap, int rosterCap)
{
	s.stageCleared = stageCleared; s.stageSel = stageSel;
	s.winX = winX; s.winY = winY; s.winW = winW; s.winH = winH;
	if (quality < 0 || quality > 6) quality = 1;
	s.camSpeed = camSpeed; s.quality = quality; s.showGrid = showGrid; s.battleAnim = battleAnim;
	s.uidNext = uidNext; s.winSizeVer = winSizeVer;
	for (int i = 0; i < 8; i++) s.itemN[i] = itemN[i];
	s.bgmVol = bgmVol; s.seVol = seVol; s.optInit = optInit;
	s.gold = gold; s.gearN = gearN;
	for (int i = 0; i < 12; i++) s.gearId[i] = gearId[i];
	for (int i = 0; i < 4; i++) s.reserved[i] = reserved[i];
	s.partyCap = partyCap;
	s.rosterCap = rosterCap;
}
static void EvoAttachRecent(CharData& c)
{
	if (!c.used || c.trialLv > 0) return;
	const ClassDef& cd = ClassInfo(c.cls);
	if (cd.tier < 2) return;
	int gate = PromoteLevel(cd.tier - 1);
	// お試し窓は「現職に昇格した直後」[gate, gate+3] の時だけ。下限を欠くと、
	// レベルがその職の正規昇格レベルに遠く届かないキャラ（職エディタ等で段だけ先走った状態）にも
	// 偽のお試しが付き、Lv77 なのに「お試し Lv183」や 10次/12次職の誤表示を招く。
	if (gate >= 9999 || c.lv < gate || c.lv > gate + 3) return;
	int best = -1, score = -1;
	int last = C_JOB0 + TAC_JOB_N;
	for (int p = 0; p < last; p++) {
		if (p == c.cls) continue;
		if (p >= C_PLAYER_COUNT && p < C_GLADIATOR) continue;
		if (ClassInfo(p).tier != cd.tier - 1) continue;
		EvoOption o[4];
		int n = EvoOptions(p, o);
		int slot = -1;
		for (int k = 0; k < n; k++) if (o[k].target == c.cls) slot = k;
		if (slot < 0) continue;
		if (ClassInfo(p).magic != cd.magic) continue;
		int sc = 10;
		int pf = LegacyLine(p);
		int cf = JobFamOf(c.cls);
		if (pf >= 0 && cf >= 0 && pf == cf) sc += 20;
		if (slot < 3) sc += 1;
		if (sc > score) { score = sc; best = p; }
	}
	if (best < 0) return;
	c.trialFrom = best;
	c.trialLv = gate > 0 ? gate : c.lv;
}
static void JobFixTrial(CharData& c)
{
	if (!c.used) return;
	if (c.trialLv > 0 && c.lv > c.trialLv + 3) {
		c.trialLv = 0;
		c.trialFrom = -1;
	}
	if (c.trialLv > 0 && c.trialFrom >= 0 && c.cls != c.trialFrom) {
		EvoOption o[4];
		int n = EvoOptions(c.trialFrom, o);
		bool listed = false;
		for (int k = 0; k < n; k++) if (o[k].target == c.cls) listed = true;
		if (listed && ClassInfo(c.trialFrom).magic == ClassInfo(c.cls).magic) return;
		int keep = c.trialLv;
		c.trialLv = 0;
		c.trialFrom = -1;
		EvoAttachRecent(c);
		if (c.trialFrom >= 0 && keep > 0) c.trialLv = keep;
		else c.trialLv = 0;
		return;
	}
	if (c.trialLv <= 0) EvoAttachRecent(c);
}
static void JobPathSanitize(CharData& c)
{
	int last = C_JOB0 + JobCount() - 1;
	bool end = false;
	for (int i = 0; i < JOB_PATH_N; i++) {
		int v = c.jobPath[i];
		if (end || v < 0 || v > last) { c.jobPath[i] = -1; end = true; }
	}
	if (c.jobDone != 1) c.jobDone = 0;
}

static int JobRailRoot(const CharData& c)
{
	if (c.jobPath[0] >= 0) return c.jobPath[0];
	if (ClassInfo(c.cls).tier <= 1) return c.cls;
	return -1;
}

static bool JobPathWalkOk(const CharData& c)
{
	if (c.jobPath[0] < 0) return false;
	int prev = c.jobPath[0];
	int last = prev;
	for (int i = 1; i < JOB_PATH_N; i++) {
		int v = c.jobPath[i];
		if (v < 0) break;
		EvoOption o[4];
		int n = EvoOptions(prev, o);
		bool hit = false;
		for (int k = 0; k < n; k++) if (o[k].target == v) hit = true;
		if (!hit) return false;
		prev = v;
		last = v;
	}
	return last == c.cls;
}

static void JobRailWrite(CharData& c, int cls, const int* par)
{
	int rev[JOB_PATH_N];
	int n = 0;
	int x = cls;
	for (int guard = 0; x >= 0 && n < JOB_PATH_N && guard < 32; guard++) {
		rev[n++] = x;
		x = par[x];
	}
	for (int i = 0; i < JOB_PATH_N; i++) c.jobPath[i] = -1;
	for (int i = 0; i < n; i++) c.jobPath[i] = rev[n - 1 - i];
}

// 履歴の起点から新しい転職先を辿れない職は、同じ段に一番近い職へ戻す。
static bool JobRailFix(CharData& c)
{
	if (!c.used) return false;
	int root = JobRailRoot(c);
	if (root < 0 || root >= 1024) return false;
	int par[1024];
	int que[1024];
	for (int i = 0; i < 1024; i++) par[i] = -2;
	par[root] = -1;
	que[0] = root;
	int qn = 1;
	for (int qi = 0; qi < qn; qi++) {
		int u = que[qi];
		EvoOption o[4];
		int n = EvoOptions(u, o);
		for (int k = 0; k < n; k++) {
			int v = o[k].target;
			if (v < 0 || v >= 1024 || par[v] != -2) continue;
			par[v] = u;
			if (qn < 1024) que[qn++] = v;
		}
	}
	int cls = c.cls;
	if (cls >= 0 && cls < 1024 && par[cls] != -2) {
		if (JobPathWalkOk(c)) return false;
		JobRailWrite(c, cls, par);
		return true;
	}
	int want = ClassInfo(cls).tier;
	int best = -1;
	int bestD = 99;
	for (int i = 0; i < qn; i++) {
		int v = que[i];
		int t = ClassInfo(v).tier;
		int d = t - want;
		if (d < 0) d = -d;
		if (best < 0 || d < bestD || (d == bestD && v < best)) {
			best = v;
			bestD = d;
		}
	}
	if (best < 0 || best == c.cls) return false;
	c.cls = best;
	JobRailWrite(c, best, par);
	c.trialFrom = -1;
	c.trialLv = 0;
	const ClassDef& cd = ClassInfo(best);
	for (int s = 0; s < S_COUNT; s++) if (c.stat[s] < cd.base[s]) c.stat[s] = cd.base[s];
	if (c.stat[S_HP] < 1) c.stat[S_HP] = 1;
	return true;
}

// レベルで許される段(JobStepsForLevel)を超えた職は、履歴をたどって段相応の一番進んだ職へ戻す。
// 旧セーブ等の「Lv は低いのに職段だけ高い」不整合は、偽お試し・次職番号の誤表示・在戦で進化が起きない、の元凶になる。
// 段を正せば呼び出し側で CharRecalcProgress を回し、能力も現職・Lv 相応へ組み直す。
static bool JobLevelClamp(CharData& c)
{
	if (!c.used) return false;
	int maxTier = JobStepsForLevel(c.lv);
	if (ClassInfo(c.cls).tier <= maxTier) return false;
	int newCls = -1, cut = -1;
	for (int i = 0; i < JOB_PATH_N; i++) {
		int v = c.jobPath[i];
		if (v < 0) break;
		if (ClassInfo(v).tier <= maxTier) { newCls = v; cut = i; }
	}
	if (newCls < 0 || newCls == c.cls) return false;
	c.cls = newCls;
	for (int i = cut + 1; i < JOB_PATH_N; i++) c.jobPath[i] = -1;
	c.trialFrom = -1;
	c.trialLv = 0;
	return true;
}

bool SaveLoad(SaveData& s)
{
	SaveDefaults(s);
	CString path = DatArc_Path(TacLeafSave());
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("rb")) != 0 || !fp) return false;
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	BOOL got = FALSE;
	BOOL recalc = FALSE;
	if (sz == (long)sizeof(SaveV1)) {
		SaveV1 o;
		if (fread(&o, 1, sizeof(o), fp) == sizeof(o) && o.magic == kSaveMagic && o.version == 1) {
			s.magic = o.magic;
			for (int i = 0; i < 20; i++) CopyChar10(s.roster[i], o.roster[i]);
			for (int i = 0; i < 5; i++) s.party[i] = o.party[i];
			CopySaveTail(s, o.stageCleared, o.stageSel, o.winX, o.winY, o.winW, o.winH,
				o.camSpeed, o.quality, o.showGrid, o.battleAnim, o.uidNext, o.winSizeVer,
				o.itemN, o.bgmVol, o.seVol, o.optInit, o.gold, o.gearN, o.gearId, o.reserved, 5, 20);
			got = TRUE;
			recalc = TRUE;
		}
	} else if (sz == (long)sizeof(SaveV2)) {
		SaveV2 o;
		if (fread(&o, 1, sizeof(o), fp) == sizeof(o) && o.magic == kSaveMagic && o.version == 2) {
			s.magic = o.magic;
			for (int i = 0; i < MAX_ROSTER; i++) CopyChar10(s.roster[i], o.roster[i]);
			for (int i = 0; i < 10; i++) s.party[i] = o.party[i];
			CopySaveTail(s, o.stageCleared, o.stageSel, o.winX, o.winY, o.winW, o.winH,
				o.camSpeed, o.quality, o.showGrid, o.battleAnim, o.uidNext, o.winSizeVer,
				o.itemN, o.bgmVol, o.seVol, o.optInit, o.gold, o.gearN, o.gearId, o.reserved, o.partyCap, o.rosterCap);
			got = TRUE;
			recalc = TRUE;
		}
	} else if (sz == (long)sizeof(SaveV3)) {
		SaveV3 o;
		if (fread(&o, 1, sizeof(o), fp) == sizeof(o) && o.magic == kSaveMagic && o.version == 3) {
			s.magic = o.magic;
			for (int i = 0; i < MAX_ROSTER; i++) CopyCharV4(s.roster[i], o.roster[i]);
			for (int i = 0; i < 10; i++) s.party[i] = o.party[i];
			CopySaveTail(s, o.stageCleared, o.stageSel, o.winX, o.winY, o.winW, o.winH,
				o.camSpeed, o.quality, o.showGrid, o.battleAnim, o.uidNext, o.winSizeVer,
				o.itemN, o.bgmVol, o.seVol, o.optInit, o.gold, o.gearN, o.gearId, o.reserved, o.partyCap, o.rosterCap);
			got = TRUE;
		}
	} else if (sz == (long)sizeof(SaveV4)) {
		SaveV4 o;
		if (fread(&o, 1, sizeof(o), fp) == sizeof(o) && o.magic == kSaveMagic && o.version == 4) {
			s.magic = o.magic;
			for (int i = 0; i < MAX_ROSTER; i++) CopyCharV4(s.roster[i], o.roster[i]);
			for (int i = 0; i < MAX_PARTY; i++) s.party[i] = o.party[i];
			CopySaveTail(s, o.stageCleared, o.stageSel, o.winX, o.winY, o.winW, o.winH,
				o.camSpeed, o.quality, o.showGrid, o.battleAnim, o.uidNext, o.winSizeVer,
				o.itemN, o.bgmVol, o.seVol, o.optInit, o.gold, o.gearN, o.gearId, o.reserved, o.partyCap, o.rosterCap);
			got = TRUE;
		}
	} else if (sz == (long)sizeof(SaveV5)) {
		SaveV5 o;
		if (fread(&o, 1, sizeof(o), fp) == sizeof(o) && o.magic == kSaveMagic && o.version == 5) {
			s.magic = o.magic;
			for (int i = 0; i < MAX_ROSTER; i++) CopyCharV5(s.roster[i], o.roster[i]);
			for (int i = 0; i < MAX_PARTY; i++) s.party[i] = o.party[i];
			CopySaveTail(s, o.stageCleared, o.stageSel, o.winX, o.winY, o.winW, o.winH,
				o.camSpeed, o.quality, o.showGrid, o.battleAnim, o.uidNext, o.winSizeVer,
				o.itemN, o.bgmVol, o.seVol, o.optInit, o.gold, o.gearN, o.gearId, o.reserved, o.partyCap, o.rosterCap);
			got = TRUE;
		}
	} else if (sz == (long)sizeof(SaveV6)) {
		SaveV6 o;
		if (fread(&o, 1, sizeof(o), fp) == sizeof(o) && o.magic == kSaveMagic && o.version == 6) {
			s.magic = o.magic;
			for (int i = 0; i < MAX_ROSTER; i++) CopyCharV6(s.roster[i], o.roster[i]);
			for (int i = 0; i < MAX_PARTY; i++) s.party[i] = o.party[i];
			CopySaveTail(s, o.stageCleared, o.stageSel, o.winX, o.winY, o.winW, o.winH,
				o.camSpeed, o.quality, o.showGrid, o.battleAnim, o.uidNext, o.winSizeVer,
				o.itemN, o.bgmVol, o.seVol, o.optInit, o.gold, o.gearN, o.gearId, o.reserved, o.partyCap, o.rosterCap);
			got = TRUE;
		}
	} else if (sz == (long)sizeof(SaveData)) {
		SaveData t;
		if (fread(&t, 1, sizeof(t), fp) == sizeof(t) && t.magic == kSaveMagic && t.version == 7) {
			s = t;
			got = TRUE;
		}
	}
	fclose(fp);
	if (!got) return false;
	s.version = 7;
	bool rail = false;
	for (int i = 0; i < MAX_ROSTER; i++) {
		CharData& c = s.roster[i];
		c.name[NAME_LEN - 1] = 0;
		if (!c.used) continue;
		JobPathSanitize(c);
		c.cls = Clampi(c.cls, 0, C_JOB0 + JobCount() - 1);
		c.lv = Clampi(c.lv, 1, TAC_LV_MAX);
		if (JobRailFix(c)) rail = true;
		bool heal = JobLevelClamp(c);   // 段がレベルを追い越した不整合を是正
		JobFixTrial(c);
		if (recalc || heal) CharRecalcProgress(c);
		CharGrantReached(c);
		if (c.lv == 1) {
			const ClassDef& cd = ClassInfo(c.cls);
			for (int s = 0; s < S_COUNT; s++) {
				int floorv = cd.base[s];
				if (c.gender == 1) {
					if (s == S_AGI || s == S_RES) floorv += 1;
					if (s == S_STR) floorv = max(1, floorv - 1);
				}
				if (c.stat[s] < floorv) c.stat[s] = floorv;
			}
		}
		{
			int b = CharBody(c), m = CharModel(c);
			c.body = 0;
			CharSetBody(c, b);
			CharSetModel(c, m);
		}
		c.hair = Clampi(c.hair, 0, HAIR_STYLES - 1);
		c.hairColor = Clampi(c.hairColor, 0, HAIR_COLORS - 1);
		CharSetOutfit(c, Clampi(CharOutfit(c), 0, OUTFIT_COLORS - 1));
	}
	for (int i = 0; i < MAX_PARTY; i++)
		if (s.party[i] < -1 || s.party[i] >= MAX_ROSTER || (s.party[i] >= 0 && !s.roster[s.party[i]].used)) s.party[i] = -1;
	s.stageCleared = Clampi(s.stageCleared, 0, TAC_STAGE_ABS);
	s.stageSel = Clampi(s.stageSel, 1, StagePlayMax(s.stageCleared));
	if (s.uidNext == 0) s.uidNext = 1;
	s.camSpeed = Clampi(s.camSpeed, 0, 2);
	TacQualityEnsureAutoDefault(s);
	if (s.optInit != 1) { s.bgmVol = 6; s.seVol = 8; s.optInit = 1; }
	else { s.bgmVol = Clampi(s.bgmVol, 0, 10); s.seVol = Clampi(s.seVol, 0, 10); }
	if (s.gold < 0) s.gold = 0;
	if (s.partyCap < 5 || s.partyCap > MAX_PARTY) s.partyCap = 5;
	if (s.rosterCap < 20 || s.rosterCap > MAX_ROSTER || (s.rosterCap % 20) != 0) s.rosterCap = 20;
	for (int i = s.partyCap; i < MAX_PARTY; i++) s.party[i] = -1;
	s.gearN = Clampi(s.gearN, 0, GEAR_STASH);
	for (int i = 0; i < s.gearN; i++) if (s.gearId[i] < 1 || s.gearId[i] > GearCount()) s.gearId[i] = 1;
	TrophyLoad();
	CraftLoad();
	if (rail) SaveWrite(s);
	return true;
}

bool SaveWrite(const SaveData& s)
{
	DatArc_FlushSuspend(TRUE);
	CString path = DatArc_Path(TacLeafSave());
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("wb")) != 0 || !fp) {
		DatArc_FlushSuspend(FALSE);
		return false;
	}
	bool ok = fwrite(&s, 1, sizeof(s), fp) == sizeof(s);
	fclose(fp);
	if (ok) { TrophySave(); CraftPrune(s); CraftSave(); DatArc_Commit(TacLeafSave()); }
	DatArc_FlushSuspend(FALSE);
	return ok;
}

// ============================================================ 名前
static const wchar_t* const kNamesJaM[] = { L"アルス", L"レオン", L"カイル", L"ガルド", L"ユリウス", L"ロラン", L"ジーク", L"セシル", L"ハロルド", L"ラグナ", L"エリク", L"トール" };
static const wchar_t* const kNamesJaF[] = { L"リリア", L"セリス", L"ミーナ", L"エルザ", L"ソフィア", L"ルナ", L"アイリス", L"フィオナ", L"ノエル", L"クレア", L"シエラ", L"ティナ" };
static const wchar_t* const kNamesLaM[] = { L"Arus", L"Leon", L"Kyle", L"Gard", L"Julius", L"Roland", L"Sieg", L"Cecil", L"Harold", L"Ragna", L"Erik", L"Thor" };
static const wchar_t* const kNamesLaF[] = { L"Lilia", L"Celes", L"Mina", L"Elsa", L"Sophia", L"Luna", L"Iris", L"Fiona", L"Noel", L"Claire", L"Sierra", L"Tina" };
static const wchar_t* const kNamesKoM[] = { L"아루스", L"레온", L"카일", L"가르드", L"율리우스", L"롤랑", L"지크", L"세실", L"해럴드", L"라그나", L"에릭", L"토르" };
static const wchar_t* const kNamesKoF[] = { L"릴리아", L"셀리스", L"미나", L"엘자", L"소피아", L"루나", L"아이리스", L"피오나", L"노엘", L"클레어", L"시에라", L"티나" };
static const wchar_t* const kNamesZhM[] = { L"阿尔斯", L"雷昂", L"凯尔", L"加尔德", L"尤里乌斯", L"罗兰", L"齐格", L"塞西尔", L"哈罗德", L"拉格纳", L"埃里克", L"托尔" };
static const wchar_t* const kNamesZhF[] = { L"莉莉娅", L"塞莉丝", L"米娜", L"艾尔莎", L"索菲亚", L"露娜", L"艾莉丝", L"菲奥娜", L"诺艾尔", L"克蕾尔", L"希艾拉", L"蒂娜" };
static const wchar_t* const kNamesArM[] = { L"آروس", L"ليون", L"كايل", L"غارد", L"يوليوس", L"رولان", L"زيغ", L"سيسيل", L"هارولد", L"راغنا", L"إريك", L"ثور" };
static const wchar_t* const kNamesArF[] = { L"ليليا", L"سيليس", L"مينا", L"إلسا", L"صوفيا", L"لونا", L"إيريس", L"فيونا", L"نويل", L"كلير", L"سييرا", L"تينا" };
static const wchar_t* const kNamesRuM[] = { L"Арус", L"Леон", L"Кайл", L"Гард", L"Юлиус", L"Роланд", L"Зиг", L"Сесил", L"Гарольд", L"Рагна", L"Эрик", L"Тор" };
static const wchar_t* const kNamesRuF[] = { L"Лилия", L"Селес", L"Мина", L"Эльза", L"София", L"Луна", L"Ирис", L"Фиона", L"Ноэль", L"Клэр", L"Сиерра", L"Тина" };

void RandomName(wchar_t* out, int gender, Rng& r)
{
	const wchar_t* key = ::LangPick14(L"ja", L"la", L"la", L"la", L"la", L"ko", L"zh", L"ar", L"ru", L"la", L"la", L"la", L"la", L"la");
	const wchar_t* const* tbl;
	if (!wcscmp(key, L"ja")) tbl = gender ? kNamesJaF : kNamesJaM;
	else if (!wcscmp(key, L"ko")) tbl = gender ? kNamesKoF : kNamesKoM;
	else if (!wcscmp(key, L"zh")) tbl = gender ? kNamesZhF : kNamesZhM;
	else if (!wcscmp(key, L"ar")) tbl = gender ? kNamesArF : kNamesArM;
	else if (!wcscmp(key, L"ru")) tbl = gender ? kNamesRuF : kNamesRuM;
	else tbl = gender ? kNamesLaF : kNamesLaM;
	wcsncpy_s(out, NAME_LEN, tbl[r.Range(0, 11)], _TRUNCATE);
}

#include "tactics_src/craft_cat.inc"
#include "tactics_src/passive_cat.inc"
#include "tactics_src/craft_fns.inc"

const wchar_t* ElemWord(int el)
{
	switch (el) {
	case EL_PHYS: return LangPick14(L"物理", L"Physical", L"Physique", L"Fisico", L"Físico", L"물리", L"物理", L"جسدي", L"Физика", L"Physisch", L"Físico", L"Fysiek", L"Fizyczny", L"Fiziksel");
	case EL_MAGIC: return LangPick14(L"魔法", L"Magic", L"Magie", L"Magia", L"Magia", L"마법", L"魔法", L"سحر", L"Магия", L"Magie", L"Magia", L"Magie", L"Magia", L"Büyü");
	case EL_MELEE: return LangPick14(L"近距離", L"Melee", L"Mêlée", L"Mischia", L"Cuerpo", L"근거리", L"近战", L"قرب", L"Ближний", L"Nah", L"Corpo", L"Dichtbij", L"Wręcz", L"Yakın");
	case EL_RANGE: return LangPick14(L"遠距離", L"Ranged", L"Distance", L"Distanza", L"Distancia", L"원거리", L"远程", L"بعيد", L"Дальний", L"Fern", L"Distância", L"Ver", L"Dystans", L"Uzak");
	default: return CraftElWord(el);
	}
}
const wchar_t* EffectWord(int ef)
{
	switch (ef) {
	case EF_SLEEP: return LangPick14(L"睡眠", L"Sleep", L"Sommeil", L"Sonno", L"Sueño", L"수면", L"睡眠", L"نوم", L"Сон", L"Schlaf", L"Sono", L"Slaap", L"Sen", L"Uyku");
	case EF_CONFUSE: return LangPick14(L"混乱", L"Confuse", L"Confusion", L"Confusione", L"Confusión", L"혼란", L"混乱", L"ارتباك", L"Смятение", L"Wirren", L"Confusão", L"Verwarring", L"Chaos", L"Şaşkın");
	case EF_STUN: return LangPick14(L"スタン", L"Stun", L"Étourdi", L"Stordito", L"Aturdimiento", L"기절", L"硬直", L"صعقة", L"Оглушение", L"Betäubung", L"Atordoar", L"Verdoving", L"Ogłuszenie", L"Sersem");
	case EF_POISON: return LangPick14(L"毒", L"Poison", L"Poison", L"Veleno", L"Veneno", L"독", L"毒", L"سم", L"Яд", L"Gift", L"Veneno", L"Gif", L"Trucizna", L"Zehir");
	case EF_SLOW: return LangPick14(L"鈍足", L"Slow", L"Lenteur", L"Lentezza", L"Lentitud", L"둔화", L"迟缓", L"بطء", L"Замедление", L"Langsam", L"Lentidão", L"Traag", L"Spowolnienie", L"Yavaş");
	case EF_DEFDOWN: return LangPick14(L"防御低下", L"Def down", L"Déf. basse", L"Dif. bassa", L"Def. baja", L"방어 저하", L"防御下降", L"دفاع منخفض", L"Ослабление", L"Abwehr runter", L"Def. baixa", L"Ver. omlaag", L"Obrona w dół", L"Savunma düşüş");
	case EF_FREEZE: return LangPick14(L"凍結", L"Freeze", L"Gel", L"Gelo", L"Hielo", L"동결", L"冻结", L"تجمد", L"Заморозка", L"Frost", L"Gelo", L"Vorst", L"Zamrożenie", L"Donma");
	case EF_BURN: return LangPick14(L"炎上", L"Burn", L"Brûlure", L"Ustione", L"Quemadura", L"염상", L"炎上", L"احتراق", L"Ожог", L"Brand", L"Queima", L"Brand", L"Oparzenie", L"Yanma");
	case EF_BLEED: return LangPick14(L"裂傷", L"Bleed", L"Saignement", L"Squarcio", L"Hemorragia", L"열상", L"裂伤", L"نزف", L"Кровотечение", L"Blutung", L"Sangria", L"Bloeding", L"Krwawienie", L"Kanama");
	case EF_BLIND: return LangPick14(L"盲目", L"Blind", L"Cécité", L"Cecità", L"Ceguera", L"실명", L"盲目", L"عمى", L"Слепота", L"Blind", L"Cegueira", L"Blind", L"Ślepota", L"Körlük");
	case EF_SILENCE: return LangPick14(L"沈黙", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"침묵", L"沉默", L"صمت", L"Молчание", L"Stille", L"Silêncio", L"Stilte", L"Milczenie", L"Sessizlik");
	case EF_CLEANSE: return LangPick14(L"状態回復", L"Cleanse", L"Purge", L"Purifica", L"Purifica", L"상태 회복", L"状态恢复", L"تطهير", L"Очищение", L"Läutern", L"Purificar", L"Zuiveren", L"Oczyszczenie", L"Arınma");
	default: return NULL;
	}
}
const wchar_t* WeakLabel()
{
	return LangPick14(L"弱点", L"Weak", L"Faiblesse", L"Debolezza", L"Debilidad", L"약점", L"弱点", L"ضعف", L"Слабость", L"Schwäche", L"Fraqueza", L"Zwakte", L"Słabość", L"Zayıf");
}
static void JoinBits(wchar_t* out, int cap, const wchar_t** bits, int n)
{
	out[0] = 0;
	for (int i = 0; i < n; i++) {
		if (!bits[i] || !bits[i][0]) continue;
		if (out[0]) wcscat_s(out, cap, L"・");
		wcscat_s(out, cap, bits[i]);
	}
}
void SkillAttrText(const SkillDef& sd, wchar_t* out, int cap)
{
	if (!out || cap < 2) return;
	out[0] = 0;
	if (sd.kind == SK_PASSIVE) return;
	const wchar_t* bits[5];
	int n = 0;
	const wchar_t* el = ElemWord(sd.element);
	if (el) bits[n++] = el;
	if (sd.kind != SK_HEAL && sd.kind != SK_BUFF) {
		int magic = sd.kind == SK_MAGIC;
		bits[n++] = ElemWord(magic ? EL_MAGIC : EL_PHYS);
		if (magic || sd.rMin >= 2 || sd.rMax >= 2) bits[n++] = ElemWord(EL_RANGE);
		else if (sd.rMax < 0) bits[n++] = LangPick14(L"武器間合い", L"weapon range", L"portée d'arme", L"portata arma", L"alcance de arma", L"무기 사거리", L"武器距离", L"مدى السلاح", L"дальность оружия", L"Waffenreichweite", L"alcance da arma", L"wapenbereik", L"zasięg broni", L"silah menzili");
		else bits[n++] = ElemWord(EL_MELEE);
	}
	static wchar_t ail[64];
	ail[0] = 0;
	if (sd.effect == EF_POISON) swprintf_s(ail, L"%s 1-3 HP5%%", EffectWord(EF_POISON));
	else if (sd.effect == EF_SLOW || sd.effect == EF_DEFDOWN) swprintf_s(ail, L"%s 1-3", EffectWord(sd.effect));
	else if (sd.effect == EF_SLEEP || sd.effect == EF_STUN) swprintf_s(ail, L"%s 1", EffectWord(sd.effect));
	else if (sd.effect == EF_FREEZE) swprintf_s(ail, L"%s 2", EffectWord(EF_FREEZE));
	else if (sd.effect == EF_BURN) swprintf_s(ail, L"%s 1-3 HP6%%", EffectWord(EF_BURN));
	else if (sd.effect == EF_BLEED) swprintf_s(ail, L"%s 1-3 HP4%%", EffectWord(EF_BLEED));
	else if (sd.effect == EF_BLIND || sd.effect == EF_SILENCE) swprintf_s(ail, L"%s 1-3", EffectWord(sd.effect));
	else if (sd.effect == EF_CONFUSE) swprintf_s(ail, L"%s 2", EffectWord(EF_CONFUSE));
	const wchar_t* ew = ail[0] ? ail : EffectWord(sd.effect);
	if (ew && n < 5) bits[n++] = ew;
	static wchar_t hitn[40];
	if (sd.hits > 1 && n < 5) {
		swprintf_s(hitn, LangPick14(
			L"%d段", L"%d hits", L"%d coups", L"%d colpi", L"%d golpes", L"%d회",
			L"%d段", L"%d ضربات", L"%d удара", L"%d Treffer", L"%d golpes", L"%d treffers", L"%d uderzenia", L"%d vuruş"), sd.hits);
		bits[n++] = hitn;
	}
	JoinBits(out, cap, bits, n);
}
void MonsterWeakText(int monster, wchar_t* out, int cap)
{
	if (!out || cap < 2) return;
	const MonsterDef& md = MonsterInfo(monster);
	const wchar_t* bits[6];
	int n = 0;
	for (int i = 0; i < 4 && n < 6; i++) {
		const wchar_t* w = ElemWord(md.weak[i]);
		if (w) bits[n++] = w;
	}
	for (int i = 0; i < 2 && n < 6; i++) {
		const wchar_t* w = EffectWord(md.ailWeak[i]);
		if (w) bits[n++] = w;
	}
	const wchar_t* none = LangPick14(L"なし", L"None", L"Aucune", L"Nessuna", L"Ninguna", L"없음", L"无", L"لا شيء", L"Нет", L"Keine", L"Nenhuma", L"Geen", L"Brak", L"Yok");
	if (n <= 0) { swprintf_s(out, cap, L"%s %s", WeakLabel(), none); return; }
	wchar_t body[192];
	body[0] = 0;
	for (int i = 0; i < n; i++) {
		if (body[0]) wcscat_s(body, L"・");
		wcscat_s(body, bits[i]);
	}
	swprintf_s(out, cap, L"%s %s", WeakLabel(), body);
}

static void MarkBreakEls(const Unit& d, int* brk)
{
	if (d.burn > 0) brk[EL_ICE] = 1;
	if (d.freeze > 0) brk[EL_FIRE] = 1;
	if (d.stun > 0 || d.bleed > 0) brk[EL_WIND] = 1;
	if (d.confuse > 0 || d.defDown > 0) brk[EL_EARTH] = 1;
	if (d.slow > 0 || d.sleep > 0) brk[EL_THUNDER] = 1;
	if (d.poison > 0) brk[EL_HOLY] = 1;
	if (d.blind > 0) brk[EL_POISON] = 1;
	if (d.silence > 0) brk[EL_DARK] = 1;
}

void UnitWeakText(const Unit& u, wchar_t* out, int cap)
{
	MonsterWeakText(u.monster, out, cap);
	if (!out || cap < 8 || u.team != TEAM_ENEMY) return;
	int innate[16] = {}, brk[16] = {};
	const MonsterDef& md = MonsterInfo(u.monster);
	for (int i = 0; i < 4; i++) {
		int w = md.weak[i];
		if (w > EL_NONE && w < 16) innate[w] = 1;
	}
	MarkBreakEls(u, brk);
	static const int kTri[2][3] = {
		{ EL_THUNDER, EL_WIND, EL_EARTH },
		{ EL_POISON, EL_HOLY, EL_DARK }
	};
	int third[2], nThird = 0;
	for (int t = 0; t < 2; t++) {
		int n = 0, live = 0, miss = EL_NONE;
		for (int i = 0; i < 3; i++) {
			int el = kTri[t][i];
			if (innate[el] || brk[el]) n++;
			else miss = el;
			if (brk[el]) live = 1;
		}
		if (live && n == 2 && miss != EL_NONE && nThird < 2) third[nThird++] = miss;
	}
	if (nThird <= 0) return;
	const wchar_t* tag = LangPick14(L"三つ巴", L"Trio", L"Trio", L"Trio", L"Trío", L"삼파전", L"三方", L"ثلاثي", L"Трио", L"Trio", L"Trio", L"Trio", L"Trójkąt", L"Üçlü");
	wchar_t extra[96];
	extra[0] = 0;
	for (int i = 0; i < nThird; i++) {
		const wchar_t* w = ElemWord(third[i]);
		if (!w) continue;
		if (extra[0]) wcscat_s(extra, L"・");
		wcscat_s(extra, w);
	}
	if (!extra[0]) return;
	wchar_t add[128];
	swprintf_s(add, L"  %s %s", tag, extra);
	if ((int)wcslen(out) + (int)wcslen(add) < cap) wcscat_s(out, cap, add);
}

} // namespace otac
