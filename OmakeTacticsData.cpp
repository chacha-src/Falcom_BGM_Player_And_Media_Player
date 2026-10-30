// OmakeTacticsData.cpp — おまけタクティクス: ルール・マップ生成・経路・戦闘・AI・セーブ

#include "stdafx.h"
#include "OmakeTacticsData.h"
#include "DatArchive.h"
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
	{ 3.2f, .050f, 2, T_PLAIN, .70f, 0, 1, 2, 1, 0 },
	{ 3.2f, .050f, 2, T_PLAIN, .72f, 0, 2, 3, 1, 0 },
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
		if (c.t == T_WATER || c.t == T_LAVA || c.t == T_CHASM) c.t = T_BRIDGE;
		else if (c.t == T_MOUNTAIN || c.t == T_ROCK || c.t == T_DOOR) { c.t = T_HILL; c.prop = PK_NONE; }
		if ((int)c.h > (int)f.h + 1) c.h = (BYTE)(f.h + 1);
		if ((int)c.h + 1 < (int)f.h) c.h = (BYTE)max(1, (int)f.h - 1);
		x = px; z = pz;
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
	default:
		return r2 < 1.f;
	}
}

static int StampShape(Map& m, int stage, Rng& r, int& pax, int& paz)
{
	int span = MapSpan(stage);
	int kindMax = span >= 30 ? 9 : (span >= 24 ? 6 : 5);
	int kind = r.Range(0, kindMax);
	float rot = r.F(0, 6.2831853f);
	float cs = cosf(rot), sn = sinf(rot);
	float half = span * 0.5f;
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
				bool rim = !ShapeInside(k, lx * 0.84f, ly * 0.84f);
				float noise = Fbm(m.seed + 17, x * 0.23f, z * 0.23f, 2);
				if (rim && noise < 0.38f) in = false;
			}
			mask[z][x] = in ? 1 : 0;
			if (in) n++;
		}
		return n;
	};
	if (build(kind, kind <= 5) < 120) build(0, false);
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (mask[z][x]) continue;
		Cell& c = m.c[z][x];
		c.t = T_VOID; c.h = 0; c.prop = PK_NONE; c.gimm = 0; c.gparam = 0;
	}
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

void GenerateMap(Map& m, int stage, DWORD seed, int theme)
{
	memset(&m, 0, sizeof(m));
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
	if (th == TH_SEABED || th == TH_FALLS) {
		span = StampFixedStage(m, th, pax, paz);
		eax = pax;
		eaz = paz;
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
		else if (stage < 120) { maxGrp = 8 + stage / 12; lo = 3; hi = min(GRP_CAP, 5 + stage / 12); }
		else { maxGrp = MAX_GROUPS; lo = 3; hi = Clampi(stage >= 200 ? 10 + (stage - 200) / 80 : 8, 4, GRP_CAP); }
		const int gap = stage <= 4 ? 5 : (stage < 40 ? 6 : 7);
		static BYTE tried[MAPN][MAPN];
		memset(tried, 0, sizeof(tried));
		int guardPick = 0;
		while (m.nGrp < maxGrp && m.nGrp < MAX_GROUPS && budget >= 3 && guardPick < 80) {
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
				if (i > 0) {
					int ph = m.c[rz[i - 1]][rx[i - 1]].h;
					if ((int)c.h > ph + 1) c.h = (BYTE)(ph + 1);
					if ((int)c.h < ph - 1) c.h = (BYTE)max(1, ph - 1);
				}
			}
			if (ln < MAX_GROUPS + 1) { linkX[ln] = m.grp[best].ax; linkZ[ln] = m.grp[best].az; ln++; }
		}
	}

	// 8) 小物
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		Cell& c = m.c[z][x];
		c.prop = PK_NONE;
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
			if (th == TH_RUINS) c.prop = PK_PILLAR;
			else if (th == TH_SWAMP || th == TH_VOLCANO) c.prop = (c.var & 1) ? PK_GRAVE : PK_ROCK;
			else c.prop = PK_ROCK;
			break;
		case T_WATER:
			if ((th == TH_SWAMP || th == TH_PLAINS || th == TH_FOREST || th == TH_FALLS || th == TH_SEABED) && (c.var % 5) == 0) c.prop = PK_LILY;
			break;
		case T_RUINS:
			if ((c.var % 23) == 0) c.prop = PK_LANTERN;
			break;
		default: break;
		}
	}
	// 拠点の飾り（通行不可の小物は岩扱い）
	{
		int tx = Clampi(pax + 2, 1, MAPN - 2), tz = Clampi(paz - 2, 1, MAPN - 2);
		bool used = false;
		for (int i = 0; i < MAX_PARTY; i++) if (m.pStartX[i] == tx && m.pStartZ[i] == tz) used = true;
		if (!used && m.c[tz][tx].t != T_VOID) { m.c[tz][tx].t = T_ROCK; m.c[tz][tx].prop = PK_TENT; }
		int fx = Clampi(pax - 2, 1, MAPN - 2), fz = Clampi(paz + 2, 1, MAPN - 2);
		used = false;
		for (int i = 0; i < MAX_PARTY; i++) if (m.pStartX[i] == fx && m.pStartZ[i] == fz) used = true;
		if (!used && m.c[fz][fx].t != T_VOID) { m.c[fz][fx].t = T_ROCK; m.c[fz][fx].prop = PK_FLAG; }
		for (int k = 0; k < 3 && m.nGrp > 0; k++) {
			const MapGroup& g0 = m.grp[0];
			int cx = Clampi(g0.ax + r.Range(-4, 4), 1, MAPN - 2), cz = Clampi(g0.az + r.Range(-4, 4), 1, MAPN - 2);
			if (taken[cz][cx] || !FootPassable(m.c[cz][cx].t)) continue;
			m.c[cz][cx].t = T_ROCK;
			m.c[cz][cx].prop = (BYTE)(th == TH_SWAMP ? PK_PUMPKIN : (k & 1) ? PK_CRATE : PK_BARREL);
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
			c.prop = PK_CRATE;
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
			m.c[lz][lx].prop = PK_LANTERN;
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
			if (c.t != T_BRIDGE || c.gimm) continue;
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
	for (int c = 0; c < C_PLAYER_COUNT; c++)
		for (int s = 0; s < LEARN_N; s++)
			if (kClass[c].learn[s].lv <= 0) kClass[c].learn[s].skill = -1;
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
		}
	}
	s_extOk = 1;
}
#include "tactics_src/job_catalog.inc"
static ClassDef s_job[TAC_JOB_N];
static int s_jobOk = 0;
static const int kFamSig[8][4] = {
	{ SKL_HOLYBLADE, SKL_CLEAVE, SKL_BRAVE, SKL_GRANDSLASH },
	{ SKL_METEOR, SKL_FIRE, SKL_GRAVITY, SKL_RUIN },
	{ SKL_SNIPE, SKL_ARROWRAIN, SKL_PIERCE, SKL_ARROWLINE },
	{ SKL_ASSASSINATE, SKL_VITALSTAB, SKL_SHADOWSTEP, SKL_BACKCUT },
	{ SKL_TRANSMUTE, SKL_RUNEBLADE, SKL_MIASMA, SKL_ELPOISON },
	{ SKL_HEAL, SKL_SANCTUARY, SKL_RENEW, SKL_ESUNA },
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
	{ TX_SK_POWER, TX_SKD_POWER, SK_PHYS, 4, 100, -1, -1, 0, EL_NONE, 2, 0, EF_NONE, 0 },
	{ TX_SK_DOUBLE, TX_SKD_DOUBLE, SK_PHYS, 6, 80, -1, -1, 0, EL_NONE, 2, 0, EF_NONE, 0 },
	{ TX_SK_WHIRL, TX_SKD_WHIRL, SK_PHYS, 8, 100, 0, 0, 1, EL_WIND, 1, 0, EF_NONE, 0 },
	{ TX_SK_PIERCE, TX_SKD_PIERCE, SK_PHYS, 6, 90, -1, -1, 0, EL_NONE, 2, 0, EF_IGNDEF, 0 },
	{ TX_SK_STITCH, TX_SKD_STITCH, SK_PHYS, 6, 80, -1, -1, 0, EL_DARK, 2, 0, EF_SLOW, 0 },
	{ TX_SK_VITAL, TX_SKD_VITAL, SK_PHYS, 5, 90, -1, -1, 0, EL_NONE, 2, 40, EF_NONE, 0 },
	{ TX_SK_BASH, TX_SKD_BASH, SK_PHYS, 5, 90, -1, -1, 0, EL_EARTH, 2, 0, EF_STUN, 0 },
	{ TX_SK_DRAGONFANG, TX_SKD_DRAGONFANG, SK_PHYS, 14, 140, -1, -1, 0, EL_FIRE, 2, 10, EF_NONE, 0 },
	{ TX_SK_HOLYBLADE, TX_SKD_HOLYBLADE, SK_PHYS, 10, 90, 1, 2, 1, EL_HOLY, 2, 5, EF_NONE, 0 },
	{ TX_SK_ARROWRAIN, TX_SKD_ARROWRAIN, SK_PHYS, 12, 90, 2, 5, 1, EL_WIND, 2, 0, EF_NONE, 0 },
	{ TX_SK_ASSASSINATE, TX_SKD_ASSASSINATE, SK_PHYS, 12, 110, -1, -1, 0, EL_DARK, 2, 20, EF_POISON, 0 },
	{ TX_SK_BRAVE, TX_SKD_BRAVE, SK_PHYS, 16, 140, -1, -1, 0, EL_HOLY, 2, 20, EF_NONE, 0 },
	{ TX_SK_FIRE, TX_SKD_FIRE, SK_MAGIC, 4, 8, 1, 3, 0, EL_FIRE, 2, 0, EF_NONE, 0 },
	{ TX_SK_BLIZZARD, TX_SKD_BLIZZARD, SK_MAGIC, 8, 11, 1, 3, 1, EL_ICE, 1, 0, EF_SLEEP, 0 },
	{ TX_SK_THUNDER, TX_SKD_THUNDER, SK_MAGIC, 8, 14, 1, 4, 0, EL_THUNDER, 2, 0, EF_STUN, 0 },
	{ TX_SK_METEOR, TX_SKD_METEOR, SK_MAGIC, 20, 14, 2, 6, 3, EL_FIRE, 1, 0, EF_NONE, 0 },
	{ TX_SK_RUNEBLADE, TX_SKD_RUNEBLADE, SK_MAGIC, 6, 12, 1, 1, 0, EL_THUNDER, 2, 0, EF_NONE, 0 },
	{ TX_SK_HOLY, TX_SKD_HOLY, SK_MAGIC, 14, 18, 1, 3, 0, EL_HOLY, 2, 0, EF_NONE, 0 },
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
	{ TX_SK_E_BREATH, TX_SKD_E_BREATH, SK_MAGIC, 8, 16, 1, 3, 1, EL_FIRE, 1, 0, EF_NONE, 1 },
	{ TX_SK_E_CRUSH, TX_SKD_E_CRUSH, SK_PHYS, 0, 160, -1, -1, 0, EL_EARTH, 1, 0, EF_STUN, 1 },
	{ TX_SK_E_DRAIN, TX_SKD_E_DRAIN, SK_PHYS, 5, 100, 1, 1, 0, EL_DARK, 1, 0, EF_DRAIN, 1 },
	{ TX_SK_E_SPIT, TX_SKD_E_SPIT, SK_PHYS, 0, 90, 1, 2, 0, EL_POISON, 1, 0, EF_SLEEP, 1 },
	{ TX_SK_E_QUAKE, TX_SKD_E_QUAKE, SK_PHYS, 0, 130, 0, 0, 1, EL_EARTH, 1, 0, EF_STUN, 1 },
	{ TX_SK_REFRESH, TX_SKD_REFRESH, SK_BUFF, 8, 0, 1, 1, 0, EL_HOLY, 1, 0, EF_REACT, 0 },
	{ TX_SK_CLEAVE, TX_SKD_CLEAVE, SK_PHYS, 8, 115, 0, 0, 1, EL_NONE, 1, 0, EF_DEFDOWN, 0 },
	{ TX_SK_SNIPE, TX_SKD_SNIPE, SK_PHYS, 7, 180, 2, 6, 0, EL_WIND, 1, 25, EF_NONE, 0 },
	{ TX_SK_VENOMEDGE, TX_SKD_VENOMEDGE, SK_PHYS, 6, 80, 1, 2, 0, EL_POISON, 2, 0, EF_POISON, 0 },
	{ TX_SK_SANCTUARY, TX_SKD_SANCTUARY, SK_HEAL, 14, 16, 0, 0, 2, EL_HOLY, 1, 0, EF_NONE, 0 },
	{ TX_SK_SMITE, TX_SKD_SMITE, SK_MAGIC, 10, 16, 1, 3, 0, EL_HOLY, 2, 10, EF_NONE, 0 },
	{ TX_SK_GRAVITY, TX_SKD_GRAVITY, SK_MAGIC, 12, 12, 1, 3, 1, EL_EARTH, 1, 0, EF_SLOW, 0 },
	{ TX_SK_WARCRY, TX_SKD_WARCRY, SK_BUFF, 8, 0, 0, 0, 1, EL_FIRE, 1, 0, EF_BUFF_ATK, 0 },
	{ TX_SK_RENEW, TX_SKD_RENEW, SK_HEAL, 12, 40, 0, 2, 0, EL_HOLY, 1, 0, EF_NONE, 0 },
	{ TX_SK_MIRROR, TX_SKD_MIRROR, SK_BUFF, 8, 0, 0, 0, 1, EL_ICE, 1, 0, EF_BUFF_DEF, 0 },
	{ TX_SK_GODSLASH, TX_SKD_GODSLASH, SK_PHYS, 22, 150, -1, -1, 0, EL_HOLY, 2, 20, EF_NONE, 0 },
	{ TX_SK_RUIN, TX_SKD_RUIN, SK_MAGIC, 24, 18, 1, 4, 1, EL_DARK, 1, 0, EF_DEFDOWN, 0 },
	{ TX_SK_APOC, TX_SKD_APOC, SK_MAGIC, 28, 16, 0, 0, 3, EL_FIRE, 1, 0, EF_NONE, 0 },
	{ TX_SK_TRANSMUTE, TX_SKD_TRANSMUTE, SK_BUFF, 4, 0, 0, 1, 0, EL_EARTH, 1, 0, EF_BUFF_ATK, 0 },
};
static_assert(sizeof(kSkill) / sizeof(kSkill[0]) == SKL_ART0, "base skill table");

static const SkillDef kWide[] = {
	{ TX_SK_SHOWER, TX_SKD_SHOWER, SK_PHYS, 14, 80, 2, 6, 2, EL_WIND, 2, 0, EF_NONE, 0 },
	{ TX_SK_SHINE, TX_SKD_SHINE, SK_PHYS, 16, 70, 2, 6, 4, EL_HOLY, 2, 0, EF_NONE, 0 },
	{ TX_SK_CRESCENT, TX_SKD_CRESCENT, SK_PHYS, 10, 100, 0, 0, 2, EL_WIND, 1, 0, EF_SLOW, 0 },
	{ TX_SK_GRANDSLASH, TX_SKD_GRANDSLASH, SK_PHYS, 16, 80, 0, 0, 4, EL_NONE, 1, 0, EF_NONE, 0 },
	{ TX_SK_STORM, TX_SKD_STORM, SK_MAGIC, 26, 14, 2, 6, 4, EL_FIRE, 1, 0, EF_NONE, 0 },
	{ TX_SK_FROSTFIELD, TX_SKD_FROSTFIELD, SK_MAGIC, 14, 9, 1, 4, 2, EL_ICE, 1, 0, EF_SLEEP, 0 },
	{ TX_SK_THUNDERFIELD, TX_SKD_THUNDERFIELD, SK_MAGIC, 16, 11, 1, 5, 2, EL_THUNDER, 1, 0, EF_STUN, 0 },
	{ TX_SK_NOVA, TX_SKD_NOVA, SK_MAGIC, 18, 14, 0, 0, 4, EL_HOLY, 1, 0, EF_NONE, 0 },
	{ TX_SK_LINESLASH, TX_SKD_LINESLASH, SK_PHYS, 10, 100, 1, 1, 4, EL_WIND, 1, 0, EF_NONE, 0, SH_LINE },
	{ TX_SK_FANSLASH, TX_SKD_FANSLASH, SK_PHYS, 12, 80, 1, 1, 3, EL_NONE, 1, 0, EF_NONE, 0, SH_FAN },
	{ TX_SK_ARROWLINE, TX_SKD_ARROWLINE, SK_PHYS, 10, 90, 2, 6, 5, EL_WIND, 2, 0, EF_NONE, 0, SH_LINE },
	{ TX_SK_ARROWFAN, TX_SKD_ARROWFAN, SK_PHYS, 12, 80, 2, 6, 3, EL_WIND, 2, 0, EF_NONE, 0, SH_FAN },
	{ TX_SK_FIREBEAM, TX_SKD_FIREBEAM, SK_MAGIC, 12, 10, 1, 4, 5, EL_FIRE, 1, 0, EF_NONE, 0, SH_LINE },
	{ TX_SK_ICEBEAM, TX_SKD_ICEBEAM, SK_MAGIC, 14, 9, 1, 4, 5, EL_ICE, 1, 0, EF_SLEEP, 0, SH_LINE },
	{ TX_SK_THUNDERBEAM, TX_SKD_THUNDERBEAM, SK_MAGIC, 14, 12, 1, 4, 4, EL_THUNDER, 1, 0, EF_STUN, 0, SH_LINE },
	{ TX_SK_HEALBEAM, TX_SKD_HEALBEAM, SK_HEAL, 12, 14, 1, 3, 4, EL_HOLY, 1, 0, EF_NONE, 0, SH_LINE },
	{ TX_SK_HEALFAN, TX_SKD_HEALFAN, SK_HEAL, 14, 12, 1, 2, 3, EL_HOLY, 1, 0, EF_NONE, 0, SH_FAN },
	{ TX_SK_RAMPAGE, TX_SKD_RAMPAGE, SK_PHYS, 8, 70, -1, -1, 0, EL_NONE, 3, 0, EF_NONE, 0 },
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
	{ TX_SK_PEERLESS, TX_SKD_PEERLESS, SK_PHYS, 22, 90, -1, -1, 0, EL_NONE, 4, 15, EF_NONE, 0 },
	{ TX_SK_ENDFIST, TX_SKD_ENDFIST, SK_PHYS, 28, 360, -1, -1, 0, EL_HOLY, 1, 25, EF_IGNDEF, 0 },
	{ TX_SK_SHADOWSTEP, TX_SKD_SHADOWSTEP, SK_PHYS, 6, 140, -1, -1, 0, EL_WIND, 1, 30, EF_NONE, 0 },
	{ TX_SK_SMOKESCREEN, TX_SKD_SMOKESCREEN, SK_BUFF, 8, 0, 0, 0, 1, EL_WIND, 1, 0, EF_BUFF_AGI, 0 },
	{ TX_SK_VITALSTAB, TX_SKD_VITALSTAB, SK_PHYS, 8, 120, -1, -1, 0, EL_POISON, 1, 20, EF_POISON, 0 },
	{ TX_SK_BACKCUT, TX_SKD_BACKCUT, SK_PHYS, 10, 170, -1, -1, 0, EL_DARK, 1, 15, EF_IGNDEF, 0 },
	{ TX_SK_NIGHTRUN, TX_SKD_NIGHTRUN, SK_PHYS, 12, 150, 1, 2, 5, EL_DARK, 1, 0, EF_NONE, 0, SH_LINE },
	{ TX_SK_SILENTKILL, TX_SKD_SILENTKILL, SK_PHYS, 16, 280, -1, -1, 0, EL_DARK, 1, 35, EF_IGNDEF, 0 },
	{ TX_SK_DAGGERRAIN, TX_SKD_DAGGERRAIN, SK_PHYS, 12, 110, 1, 3, 3, EL_WIND, 1, 10, EF_NONE, 0, SH_FAN },
	{ TX_SK_DEATHMARK, TX_SKD_DEATHMARK, SK_PHYS, 8, 90, 1, 2, 0, EL_DARK, 1, 0, EF_DEFDOWN, 0 },
	{ TX_SK_GHOSTHAND, TX_SKD_GHOSTHAND, SK_PHYS, 10, 110, -1, -1, 0, EL_WIND, 2, 20, EF_NONE, 0 },
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
	{ TX_SK_WING, TX_SKD_WING, SK_PHYS, 6, 145, 1, 1, 0, EL_NONE, 1, 0, EF_NONE, 0, SH_RADIUS, FM_WING },
	{ TX_SK_CROSS, TX_SKD_CROSS, SK_PHYS, 8, 180, 1, 1, 0, EL_NONE, 1, 5, EF_NONE, 0, SH_RADIUS, FM_CROSS },
	{ TX_SK_WALL, TX_SKD_WALL, SK_PHYS, 10, 110, 1, 1, 2, EL_NONE, 1, 0, EF_NONE, 0, SH_FAN, FM_WALL },
	{ TX_SK_BACK, TX_SKD_BACK, SK_PHYS, 8, 140, 1, 1, 4, EL_WIND, 1, 0, EF_NONE, 0, SH_LINE, FM_BACK },
	{ TX_SK_TRI, TX_SKD_TRI, SK_PHYS, 10, 120, 1, 1, 3, EL_NONE, 1, 0, EF_NONE, 0, SH_FAN, FM_TRI },
	{ TX_SK_CHAIN, TX_SKD_CHAIN, SK_PHYS, 14, 240, 1, 1, 0, EL_NONE, 1, 10, EF_NONE, 0, SH_RADIUS, FM_CHAIN },
	{ TX_SK_DIA, TX_SKD_DIA, SK_PHYS, 10, 125, 1, 1, 2, EL_HOLY, 1, 0, EF_NONE, 0, SH_FAN, FM_DIA },
	{ TX_SK_SQUARE, TX_SKD_SQUARE, SK_PHYS, 12, 140, 0, 0, 1, EL_EARTH, 1, 0, EF_NONE, 0, SH_RADIUS, FM_SQUARE },
	{ TX_SK_ECHELON, TX_SKD_ECHELON, SK_PHYS, 12, 155, 2, 5, 5, EL_WIND, 2, 0, EF_NONE, 0, SH_LINE, FM_ECHELON },
	{ TX_SK_CIRCLE, TX_SKD_CIRCLE, SK_MAGIC, 16, 18, 0, 0, 2, EL_HOLY, 1, 0, EF_NONE, 0, SH_RADIUS, FM_CIRCLE },
	{ TX_SK_PAIR, TX_SKD_PAIR, SK_HEAL, 8, 22, 0, 0, 1, EL_HOLY, 1, 0, EF_NONE, 0, SH_RADIUS, FM_PAIR },
	{ TX_SK_PINCER, TX_SKD_PINCER, SK_PHYS, 10, 200, -1, -1, 0, EL_NONE, 1, 20, EF_NONE, 0, SH_RADIUS, FM_PINCER },
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
		else if (pat == 3) { d.kind = SK_PHYS; d.power = 70 + lv; d.hits = 2; d.rMin = -1; d.rMax = -1; }
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
			else if (role == 5) { d.kind = SK_PHYS; d.mp = 12; d.power = 85; d.rMin = 1; d.rMax = 1; d.area = 3; d.hits = 2; d.critBonus = 10; d.shape = SH_FAN; }
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
	if (s >= SKL_GALE && s < SKL_COUNT) {
		static const SkillDef ex[] = {
			{ TX_SK_GALE, TX_SKD_GALE, SK_MAGIC, 6, 12, 1, 3, 0, EL_WIND, 2, 0, EF_NONE, 0 },
			{ TX_SK_MIASMA, TX_SKD_MIASMA, SK_MAGIC, 6, 12, 1, 3, 0, EL_POISON, 2, 0, EF_POISON, 0 },
			{ TX_SK_ELFIRE, TX_SKD_ELFIRE, SK_PHYS, 6, 90, -1, -1, 0, EL_FIRE, 2, 0, EF_NONE, 0 },
			{ TX_SK_ELICE, TX_SKD_ELICE, SK_PHYS, 6, 90, -1, -1, 0, EL_ICE, 2, 0, EF_NONE, 0 },
			{ TX_SK_ELTHUNDER, TX_SKD_ELTHUNDER, SK_PHYS, 6, 90, -1, -1, 0, EL_THUNDER, 2, 0, EF_NONE, 0 },
			{ TX_SK_ELHOLY, TX_SKD_ELHOLY, SK_PHYS, 6, 90, -1, -1, 0, EL_HOLY, 2, 0, EF_NONE, 0 },
			{ TX_SK_ELDARK, TX_SKD_ELDARK, SK_PHYS, 6, 90, -1, -1, 0, EL_DARK, 2, 0, EF_NONE, 0 },
			{ TX_SK_ELWIND, TX_SKD_ELWIND, SK_PHYS, 6, 90, -1, -1, 0, EL_WIND, 2, 0, EF_NONE, 0 },
			{ TX_SK_ELEATH, TX_SKD_ELEATH, SK_PHYS, 6, 90, -1, -1, 0, EL_EARTH, 2, 0, EF_NONE, 0 },
			{ TX_SK_ELPOISON, TX_SKD_ELPOISON, SK_PHYS, 6, 90, -1, -1, 0, EL_POISON, 2, 0, EF_POISON, 0 },
		};
		static_assert(sizeof(ex) / sizeof(ex[0]) == SKL_COUNT - SKL_GALE, "extra skill table");
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
	{ 20, 20, 1, 3, 8, 8, 5, 6, 4, 3 }, { 40, 50, 5, 25, 60, 50, 30, 40, 30, 0 }, { SKL_FIRE, SKL_DARKNESS, SKL_HEAL }, 8, 5, 1.0f, 0xD8C8FF, 40, -1 },
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
static const wchar_t* ImportLocName(int model)
{
	if (model < 0 || model >= kImportN) return L"?";
	const wchar_t* const* s = kImportLoc[model];
	return LangPick14(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]);
}

enum { IMP_CAP = 1024, POOL_FAN = 0, POOL_BEAST = 1, POOL_FISH = 2, POOL_HUM = 3 };
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
static void SetWeak(MonsterDef& d, int a, int b)
{
	d.weak[0] = a;
	d.weak[1] = b;
}
static void WeakFromName(MonsterDef& d, const wchar_t* n)
{
	if (!n) { SetWeak(d, EL_FIRE, EL_MELEE); return; }
	if (NameHas(n, L"Dragon") || NameHas(n, L"Wyvern") || NameHas(n, L"Drake")) SetWeak(d, EL_ICE, EL_RANGE);
	else if (NameHas(n, L"Ghost")) SetWeak(d, EL_HOLY, EL_FIRE);
	else if (NameHas(n, L"Demon")) SetWeak(d, EL_HOLY, EL_ICE);
	else if (NameHas(n, L"Wizard") || NameHas(n, L"Witch")) SetWeak(d, EL_PHYS, EL_MELEE);
	else if (NameHas(n, L"Golem")) SetWeak(d, EL_THUNDER, EL_MAGIC);
	else if (NameHas(n, L"Yeti")) SetWeak(d, EL_FIRE, EL_MAGIC);
	else if (NameHas(n, L"Ninja")) SetWeak(d, EL_HOLY, EL_RANGE);
	else if (NameHas(n, L"Mush") || NameHas(n, L"Spider") || NameHas(n, L"Cactoro")) SetWeak(d, EL_FIRE, EL_WIND);
	else if (NameHas(n, L"Blob") || NameHas(n, L"Glub")) SetWeak(d, EL_FIRE, EL_ICE);
	else if (NameHas(n, L"Alien")) SetWeak(d, EL_HOLY, EL_THUNDER);
	else if (NameHas(n, L"Orc")) SetWeak(d, EL_HOLY, EL_MAGIC);
	else if (NameHas(n, L"King") && !NameHas(n, L"Kingfisher")) SetWeak(d, EL_THUNDER, EL_MAGIC);
	else if (NameHas(n, L"Shark") || NameHas(n, L"Whale") || NameHas(n, L"Piranha") || NameHas(n, L"Fish") || NameHas(n, L"Tang") || NameHas(n, L"Koi") || NameHas(n, L"Tuna") || NameHas(n, L"Dolphin") || NameHas(n, L"Manta") || NameHas(n, L"Squid") || NameHas(n, L"Angler"))
		SetWeak(d, EL_THUNDER, EL_ICE);
	else if (NameHas(n, L"Snake") || NameHas(n, L"Frog") || NameHas(n, L"Worm")) SetWeak(d, EL_ICE, EL_FIRE);
	else if (NameHas(n, L"Wasp") || NameHas(n, L"Birb") || NameHas(n, L"Pigeon") || NameHas(n, L"Hywirl") || NameHas(n, L"Armabee") || NameHas(n, L"Chicken"))
		SetWeak(d, EL_WIND, EL_RANGE);
	else if (NameHas(n, L"Trex") || NameHas(n, L"Dino") || NameHas(n, L"Raptor")) SetWeak(d, EL_ICE, EL_MAGIC);
	else if (NameHas(n, L"Stego") || NameHas(n, L"Tricera")) SetWeak(d, EL_THUNDER, EL_MAGIC);
	else if (NameHas(n, L"Apatosaurus") || NameHas(n, L"Parasaurolophus")) SetWeak(d, EL_FIRE, EL_RANGE);
	else if (NameHas(n, L"Wolf") || NameHas(n, L"Fox") || NameHas(n, L"Husky") || NameHas(n, L"Shiba") || NameHas(n, L"Cat") || NameHas(n, L"Rat") || NameHas(n, L"Bunny"))
		SetWeak(d, EL_FIRE, EL_MELEE);
	else if (NameHas(n, L"Bull") || NameHas(n, L"Cow") || NameHas(n, L"Deer") || NameHas(n, L"Stag") || NameHas(n, L"Horse") || NameHas(n, L"Donkey") || NameHas(n, L"Alpaca"))
		SetWeak(d, EL_FIRE, EL_MELEE);
	else if (NameHas(n, L"Tribal")) SetWeak(d, EL_HOLY, EL_MELEE);
	else if (NameHas(n, L"Hum")) SetWeak(d, EL_THUNDER, EL_MAGIC);
	else {
		static const int pair[10][2] = {
			{ EL_FIRE, EL_ICE }, { EL_THUNDER, EL_RANGE }, { EL_HOLY, EL_DARK }, { EL_WIND, EL_MELEE },
			{ EL_EARTH, EL_MAGIC }, { EL_POISON, EL_FIRE }, { EL_ICE, EL_PHYS }, { EL_MAGIC, EL_RANGE },
			{ EL_DARK, EL_HOLY }, { EL_PHYS, EL_MELEE }
		};
		int h = 0;
		for (const wchar_t* p = n; *p; ++p) h = h * 17 + (int)*p;
		if (h < 0) h = -h;
		SetWeak(d, pair[h % 10][0], pair[h % 10][1]);
	}
}
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
	static const int kClassicWeak[M_COUNT][2] = {
		{ EL_FIRE, EL_ICE }, { EL_HOLY, EL_FIRE }, { EL_HOLY, EL_MAGIC }, { EL_HOLY, EL_MELEE },
		{ EL_WIND, EL_RANGE }, { EL_HOLY, EL_MELEE }, { EL_FIRE, EL_ICE }, { EL_THUNDER, EL_MAGIC },
		{ EL_ICE, EL_RANGE }, { EL_HOLY, EL_THUNDER }, { EL_ICE, EL_RANGE }
	};
	for (int i = 0; i < M_COUNT; i++) {
		MonsterDef& d = kMon[i];
		FillUnitSheet(d.base, d.grow, G_MELEE, d.magAttack, d.moveType == MV_FLY, d.jump, d.rMax);
		d.weak[0] = kClassicWeak[i][0];
		d.weak[1] = kClassicWeak[i][1];
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
		WeakFromName(d, nm);
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
	int hi = d.maxStage > 0 ? d.maxStage : TAC_STAGE_MAX;
	int top = cleared < hi ? cleared : hi;
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
	CharGrantReached(c);
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
	if (s < 0 || s >= 320) return;
	if (s < 64) { c.skills |= 1ull << s; return; }
	int i = s - 64;
	int w = i >> 5;
	if (w >= 0 && w < 6) c.reserved[w] |= (int)(1u << (i & 31));
}
static void CharTakeSkill(CharData& c, int s)
{
	if (s < 0 || s >= 320) return;
	if (s < 64) { c.skills &= ~(1ull << s); return; }
	int i = s - 64;
	int w = i >> 5;
	if (w >= 0 && w < 6) c.reserved[w] &= ~(int)(1u << (i & 31));
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
	if (s < 0 || s >= 320) return false;
	if (s < 64) return (c.skills & (1ull << s)) != 0;
	int i = s - 64;
	int w = i >> 5;
	if (w < 0 || w >= 6) return false;
	return (c.reserved[w] & (int)(1u << (i & 31))) != 0;
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
	{ C_SOLDIER,    { { C_PALADIN, -1 }, { C_RUNEKNIGHT, -1 }, { C_SKYKNIGHT, -1 }, { -1, -1 } } },
	{ C_FIGHTER,    { { C_BERSERKER, -1 }, { C_PALADIN, -1 }, { C_SKYKNIGHT, -1 }, { C_GLADIATOR, -1 } } },
	{ C_ARCHER,     { { C_SNIPER, -1 }, { C_ASSASSIN, -1 }, { C_SKYKNIGHT, -1 }, { -1, -1 } } },
	{ C_THIEF,      { { C_ASSASSIN, -1 }, { C_SNIPER, -1 }, { C_SKYKNIGHT, -1 }, { C_SHADOW, -1 } } },
	{ C_MAGE,       { { C_WIZARD, -1 }, { C_ALCHEMIST, -1 }, { C_RUNEKNIGHT, -1 }, { C_BISHOP, -1 } } },
	{ C_CLERIC,     { { C_BISHOP, -1 }, { C_PALADIN, -1 }, { C_WIZARD, -1 }, { C_ALCHEMIST, -1 } } },
	{ C_PALADIN,    { { C_HERO, -1 }, { C_SAINT, -1 }, { C_DRAGONLORD, -1 }, { -1, -1 } } },
	{ C_BERSERKER,  { { C_GLADIATOR, -1 }, { C_HERO, -1 }, { C_DRAGONLORD, -1 }, { -1, -1 } } },
	{ C_RUNEKNIGHT, { { C_HERO, -1 }, { C_ARCHMAGE, -1 }, { C_SAINT, -1 }, { C_ARCHALCHEMIST, -1 } } },
	{ C_SNIPER,     { { C_PHANTOM, -1 }, { C_HERO, -1 }, { C_SHADOW, -1 }, { C_DRAGONLORD, -1 } } },
	{ C_ASSASSIN,   { { C_SHADOW, -1 }, { C_PHANTOM, -1 }, { C_HERO, -1 }, { -1, -1 } } },
	{ C_SKYKNIGHT,  { { C_DRAGONLORD, -1 }, { C_HERO, -1 }, { C_PHANTOM, -1 }, { C_GLADIATOR, -1 } } },
	{ C_WIZARD,     { { C_ARCHMAGE, -1 }, { C_SAINT, -1 }, { C_ARCHALCHEMIST, -1 }, { -1, -1 } } },
	{ C_BISHOP,     { { C_SAINT, -1 }, { C_ARCHMAGE, -1 }, { C_HERO, -1 }, { -1, -1 } } },
	{ C_ALCHEMIST,  { { C_ARCHALCHEMIST, -1 }, { C_ARCHMAGE, -1 }, { C_SAINT, -1 }, { -1, -1 } } },
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

void EvoApply(CharData& c, int target, const CharData* ps, int n)
{
	const ClassDef& a = ClassInfo(c.cls);
	const ClassDef& b = ClassInfo(target);
	for (int s = 0; s < S_COUNT; s++) {
		int d = b.base[s] - a.base[s];
		if (d > 0) c.stat[s] += d;
		if (s == S_MOV) c.stat[s] = max(c.stat[s], b.base[s]);
		for (int i = 0; i < n && s != S_MOV; i++) c.stat[s] += ps[i].stat[s] / 10;
	}
	c.cls = target;
	// 転職ボーナス。新しい職の傾向。仲間の系統や距離では増減しない。
	{
		int bump = 2 + b.tier / 2;
		if (b.group == G_MELEE) {
			c.stat[S_STR] += bump;
			c.stat[S_DEF] += 1;
			c.stat[S_HP] += 6 + b.tier;
		} else if (b.group == G_SWIFT) {
			c.stat[S_AGI] += bump;
			c.stat[S_DEX] += bump;
			c.stat[S_HP] += 3;
		} else if (b.group == G_MAGIC) {
			c.stat[S_MAG] += bump;
			c.stat[S_RES] += 1;
			c.stat[S_MP] += 6 + b.tier;
		} else {
			c.stat[S_RES] += bump;
			c.stat[S_MAG] += 1;
			c.stat[S_MP] += 4 + b.tier;
			c.stat[S_HP] += 3;
		}
		if (b.moveType == MV_FLY) c.stat[S_JMP] += 1;
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
	if (w && s == S_STR) v += w->str;
	if (w && s == S_MAG) v += w->mag;
	if (a && s == S_DEF) v += a->def;
	if (a && s == S_RES) v += a->res;
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
	if (has("2H_Axe") || has("2H_Sword")) return AS_CHOP2;
	if (has("Staff") || has("Wand")) return AS_CAST;
	if (has("Axe") || has("Sword")) return AS_SLASH;
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
	if (sk[0] && cf[0] && (int)(wcslen(sk) + wcslen(cf) + 5) < cap)
		swprintf_s(out, (size_t)cap, L"%s    %s", sk, cf);
	else if (sk[0]) wcscpy_s(out, (size_t)cap, sk);
	else if (cf[0]) wcscpy_s(out, (size_t)cap, cf);
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
	u.ch.used = 1;
	u.ch.cls = -1;
	u.ch.lv = Clampi(lv, 1, TAC_ENEMY_LV_MAX);
	for (int s = 0; s < S_COUNT; s++) {
		int g = md.grow[s];
		int v = md.base[s];
		if (s == S_HP || s == S_MP) v += g * (u.ch.lv - 1) / 10;
		else v += g * (u.ch.lv - 1) / 100;
		// 味方のこれから先の成長が倍になった。敵はHPだけ同じ倍率にする。攻撃力は上げない。
		if (s == S_HP) v *= 2;
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
		u.ch.stat[S_STR] += 2; u.ch.stat[S_MAG] += 2; u.ch.stat[S_DEF] += 2; u.ch.stat[S_RES] += 2;
		u.ch.stat[S_MOV] = min(MoveCap(md.moveType), u.ch.stat[S_MOV] + 1);
		u.ch.stat[S_GRD] += 2;
		u.ch.stat[S_PEN] += 2;
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
	u.cp = boss ? 30 : 0;
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
	if (moveType == MV_FLY) return TerrainInfo(t.t).flyOk != 0;
	if (TerrainInfo(t.t).costFoot >= 99) return false;
	const Cell& f = m.At(fx, fz);
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

// 原点を (x,z) から (nx,nz) へ 1 マス動かす。足跡の全セルが通れるときだけ。
// 1x1 は従来どおり味方のマスを通過できる。2x2 以上は誰とも重ならない。
static bool FootStep(const Battle& b, int ui, int x, int z, int nx, int nz)
{
	const Unit& u = b.u[ui];
	int s = UnitSpan(u);
	int mt = UnitMoveType(u), jp = UnitJump(u);
	for (int iz = 0; iz < s; iz++) for (int ix = 0; ix < s; ix++) {
		int sx = x + ix, sz = z + iz, tx = nx + ix, tz = nz + iz;
		if (!Map::In(sx, sz) || !CanStep(b.map, sx, sz, tx, tz, mt, jp)) return false;
		int o = b.UnitAt(tx, tz);
		if (o < 0 || o == ui) continue;
		if (b.u[o].team != u.team) return false;
		if (s > 1) return false;
	}
	return true;
}

static int FootCost(const Battle& b, int ui, int x, int z)
{
	const Unit& u = b.u[ui];
	int s = UnitSpan(u);
	int mt = UnitMoveType(u);
	int c = 0;
	for (int iz = 0; iz < s; iz++) for (int ix = 0; ix < s; ix++) {
		if (!Map::In(x + ix, z + iz)) return 99;
		int e = EnterCost(b.map, x + ix, z + iz, mt);
		if (e > c) c = e;
	}
	return c;
}

static bool FootCanStop(const Battle& b, int ui, int x, int z)
{
	const Unit& u = b.u[ui];
	int s = UnitSpan(u);
	for (int iz = 0; iz < s; iz++) for (int ix = 0; ix < s; ix++) {
		if (!Map::In(x + ix, z + iz)) return false;
		int o = b.UnitAt(x + ix, z + iz);
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

void ComputeReach(const Battle& b, int ui, Reach& out)
{
	const Unit& u = b.u[ui];
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) { out.cost[z][x] = -1; out.from[z][x] = -1; out.canStop[z][x] = 0; }
	const int mv = UnitMove(u) * 2;
	// 小さい盤なので単純ダイクストラ（行動範囲内のみ）
	static int qx[MAPN * MAPN], qz[MAPN * MAPN];
	int qn = 0;
	out.cost[u.z][u.x] = 0;
	qx[qn] = u.x; qz[qn] = u.z; qn++;
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	while (qn > 0) {
		int bi = 0;
		for (int i = 1; i < qn; i++) if (out.cost[qz[i]][qx[i]] < out.cost[qz[bi]][qx[bi]]) bi = i;
		int x = qx[bi], z = qz[bi];
		qx[bi] = qx[qn - 1]; qz[bi] = qz[qn - 1]; qn--;
		int c0 = out.cost[z][x];
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!FootStep(b, ui, x, z, nx, nz)) continue;
			int nc = c0 + FootCost(b, ui, nx, nz);
			if (nc > mv) continue;
			if (out.cost[nz][nx] >= 0 && out.cost[nz][nx] <= nc) continue;
			bool inq = out.cost[nz][nx] >= 0;
			out.cost[nz][nx] = (short)nc;
			out.from[nz][nx] = (short)(z * MAPN + x);
			if (!inq) { qx[qn] = nx; qz[qn] = nz; qn++; }
			else {
				bool found = false;
				for (int i = 0; i < qn; i++) if (qx[i] == nx && qz[i] == nz) found = true;
				if (!found) { qx[qn] = nx; qz[qn] = nz; qn++; }
			}
		}
	}
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (out.cost[z][x] < 0) continue;
		out.canStop[z][x] = FootCanStop(b, ui, x, z) ? 1 : 0;
	}
}

int ReachPath(const Reach& r, int sx, int sz, int tx, int tz, int* outXZ, int maxN)
{
	if (!Map::In(tx, tz) || r.cost[tz][tx] < 0) return 0;
	int tmp[MAPN * 2], n = 0;
	int x = tx, z = tz;
	while (n < (int)_countof(tmp)) {
		tmp[n++] = z * MAPN + x;
		if (x == sx && z == sz) break;
		int p = r.from[z][x];
		if (p < 0) break;
		x = p % MAPN; z = p / MAPN;
	}
	int out = 0;
	for (int i = n - 1; i >= 0 && out < maxN; i--) outXZ[out++] = tmp[i];
	return out;
}

bool LineOfSight(const Map& m, int ax, int az, int bx, int bz)
{
	float ya = m.StandY(ax, az) + .6f, yb = m.StandY(bx, bz) + .6f;
	int steps = max(abs(bx - ax), abs(bz - az)) * 2;
	if (steps <= 2) return true;
	for (int i = 1; i < steps; i++) {
		float t = (float)i / steps;
		int x = (int)floorf(ax + (bx - ax) * t + .5f), z = (int)floorf(az + (bz - az) * t + .5f);
		if ((x == ax && z == az) || (x == bx && z == bz)) continue;
		const Cell& c = m.At(x, z);
		if (c.t == T_MOUNTAIN || c.t == T_ROCK) return false;
		float y = Lerpf(ya, yb, t);
		if (m.TopY(x, z) > y + .35f) return false;
	}
	return true;
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
	for (int az1 = 0; az1 < sa; az1++) for (int ax1 = 0; ax1 < sa; ax1++)
		for (int tz = 0; tz < st; tz++) for (int tx = 0; tx < st; tx++)
			if (InAttackRange(b, ax + ax1, az + az1, rMin, rMax, t.x + tx, t.z + tz, needLos))
				return true;
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

static int WeakHitsOf(const Unit& a, const Unit& d, int skill)
{
	if (d.team != TEAM_ENEMY || &a == &d) return 0;
	const SkillDef* sd = skill >= 0 ? &SkillInfo(skill) : NULL;
	int magic = sd ? (sd->kind == SK_MAGIC) : (UnitMagAttack(a) != 0);
	int el = sd ? sd->element : EL_NONE;
	int ranged;
	if (magic) ranged = 1;
	else if (sd && sd->rMax >= 0) ranged = (sd->rMin >= 2 || sd->rMax >= 2) ? 1 : 0;
	else ranged = (UnitRangeMax(a) >= 2 || WeaponOf(a) == WT_BOW || UnitAttackStyle(a) == AS_SHOOT || UnitAttackStyle(a) == AS_SHOOT1) ? 1 : 0;
	const MonsterDef& md = MonsterInfo(d.monster);
	int ws[2] = { md.weak[0], md.weak[1] };
	int n = 0;
	for (int i = 0; i < 2; i++) {
		int w = ws[i];
		if (w == EL_NONE || (i == 1 && w == ws[0])) continue;
		int hit = 0;
		if (w >= EL_FIRE && w <= EL_POISON && w == el) hit = 1;
		else if (w == EL_MAGIC && magic) hit = 1;
		else if (w == EL_PHYS && !magic) hit = 1;
		else if (w == EL_RANGE && ranged) hit = 1;
		else if (w == EL_MELEE && !ranged) hit = 1;
		if (hit) n++;
	}
	return n;
}

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
		float df = UnitStat(d, S_RES) * (1.f + tdef * .005f);
		dm = atk * mult - df * .8f;
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
		crit = 0;
	} else {
		int atk = UnitStat(a, S_STR) + UnitWeaponPow(a);
		if (sd && skill == SKL_HOLYBLADE) atk += UnitStat(a, S_MAG) / 2;
		if (UnitHasSkill(a, SKL_P_MIGHT)) atk = atk * 115 / 100;
		if (!sd && (WeaponOf(a) == WT_BOW || UnitAttackStyle(a) == AS_SHOOT || UnitAttackStyle(a) == AS_SHOOT1))
			atk = atk * 6 / 5;
		float mult = sd ? sd->power / 100.f : 1.f;
		float df = UnitStat(d, S_DEF) * (1.f + tdef * .01f);
		if (sd && sd->effect == EF_IGNDEF) df *= .5f;
		{
			int pen = UnitStat(a, S_PEN);
			if (pen > 40) pen = 40;
			if (pen > 0) df *= (100 - pen) / 100.f;
		}
		dm = atk * mult - df;
		int evade = UnitStat(d, S_AGI) / 2 + UnitStat(d, S_LUK) / 4 + tavo / 2 + (UnitHasSkill(d, SKL_P_EVADE) ? 8 : 0) + (UnitHasPas(d, PAS_EVADE) ? 6 : 0) + (UnitHasPas(d, PAS_EVADE2) ? 16 : 0);
		hit = 90 + UnitStat(a, S_DEX) + UnitStat(a, S_LUK) / 4 - evade + (sd ? 6 : 0);
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
		int elw = sd ? sd->element : EL_NONE;
		if (b.weather == WX_RAIN) {
			if (elw == EL_FIRE) dm *= 0.75f;
			else if (elw == EL_THUNDER) dm *= 1.10f;
			if (bow) hit -= 12;
		} else if (b.weather == WX_SNOW) {
			if (elw == EL_ICE) dm *= 1.15f;
			else if (elw == EL_FIRE) dm *= 0.85f;
			if (bow) hit -= 8;
		} else if (b.weather == WX_ASH) {
			if (elw == EL_FIRE) dm *= 1.12f;
			if (bow) hit -= 14;
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
		int el = sd ? sd->element : EL_NONE;
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
		dm = (float)atk * mult;
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
		dm = (float)atk * mult;
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
			if (!FootClear(b, t)) continue;
			out[n++] = i;
			continue;
		}
		if (!t.alive) continue;
		bool ally = (t.team == c.team);
		if (friendly != ally) continue;
		if (!UnitOnShape(s, c, cx, cz, t)) continue;
		if (s.rMax == 0 && i == caster && s.kind != SK_HEAL && s.kind != SK_BUFF) continue;
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
	double e = (12.0 + (double)stage) * (0.35 + 0.65 * frac) * lvK;
	if (d.boss) e *= 1.25;
	if (e < 1.0) return 1;
	if (e > 2000000000.0) return 2000000000;
	return (int)(e + 0.5);
}

/* ステージ1で Lv1、19で 30、以後同じ傾き。9999で約1.6万。 */
static int StageEnemyLevel(int stage)
{
	stage = Clampi(stage, 1, TAC_STAGE_MAX);
	return 1 + (stage - 1) * 29 / 18;
}

static bool TurnFx(int effect)
{
	return effect == EF_SLOW || effect == EF_SLEEP || effect == EF_STUN || effect == EF_CONFUSE;
}

// すでに付いている異常は伸ばさない。鈍足・睡眠・スタン・混乱は、動けるようになるまで重ねない。
static bool ApplyEffect(Battle& b, Unit& a, Unit& d, int effect, Rng& r)
{
	int turns = r.Range(1, 3);
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
		d.sleep = 1;
		return true;
	case EF_CONFUSE:
		if (UnitHasPas(d, PAS_NOCONFUSE) || UnitAilmentHeld(d)) return false;
		d.confuse = 2;
		return true;
	case EF_STUN:
		if (UnitHasPas(d, PAS_NOSTUN) || UnitAilmentHeld(d)) return false;
		d.stun = 1;
		return true;
	case EF_CLEANSE: {
		bool had = d.poison > 0 || d.sleep > 0 || d.confuse > 0 || d.stun > 0 || d.slow > 0 || d.defDown > 0;
		d.poison = d.sleep = d.confuse = d.stun = d.slow = d.defDown = 0;
		return had;
	}
	case EF_BUFF_DEF: d.buffDef = 3; return true;
	case EF_BUFF_AGI: d.buffAgi = 3; return true;
	case EF_BUFF_ATK: d.buffAtk = 3; return true;
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
	n += max(0, UnitStat(u, S_CPL) / 4);
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
		if (IsSummonId(skill)) a.hp = max(1, a.hp - SummonHpCost(a, skill));
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
			Unit& d = b.u[targets[k]];
			bool corpse = d.hp <= 0;
			if (corpse && sd->effect != EF_RAISE && (sd->kind == SK_HEAL || sd->kind == SK_BUFF)) continue;
			Strike s = {};
			s.attacker = ai; s.defender = targets[k]; s.skill = skill; s.volley = wave;
			if (sd->kind == SK_HEAL) {
				if (sd->effect == EF_RAISE) {
					d.alive = 1;
					d.killed = 0;
					d.hp = max(1, d.ch.stat[S_HP] * 45 / 100);
					d.dissolve = 0;
					d.ct = max(d.ct, 200);
					s.dmg = -d.hp;
					s.hit = 1;
				} else {
					// 回復は命中判定をしない。
					int amt = HealAmount(a, d, *sd, skill);
					amt = amt * FormBoostPct(b, ai, skill, cx0, cz0) / 100;
					int before = d.hp;
					d.hp = min(d.ch.stat[S_HP], d.hp + amt);
					s.dmg = -(d.hp - before);
					s.hit = 1;
					if (sd->effect == EF_CLEANSE) s.landed = ApplyEffect(b, a, d, sd->effect, r) ? 1 : 0;
					AddCp(a, 4);
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
					AddCp(a, 5 + s.dmg / 12);
				} else {
				s.hit = (int)(r.Next() % 100) < hit;
				if (s.hit) {
					s.crit = (int)(r.Next() % 100) < crit;
					s.dmg = scaleForm(s.crit ? dmg * 2 : dmg, cx0, cz0);
					if ((long long)s.dmg >= (long long)d.hp * 2) s.overkill = 1;
					d.hp -= s.dmg;
					AddCp(a, 5 + s.dmg / 12);
					AddCp(d, 7 + s.dmg / 8);
					bool held = UnitAilmentHeld(d);
					if (s.dmg > 0 && !(held && TurnFx(sd->effect))) d.sleep = 0;
					if (sd->effect != EF_NONE) s.landed = ApplyEffect(b, a, d, sd->effect, r) ? 1 : 0;
					if (d.hp <= 0) {
						if (!d.pasUsed && UnitHasPas(d, PAS_GUARD)) { d.hp = 1; d.pasUsed = 1; }
						else if (GutsHold(d, a, r)) s.guts = 1;
						else { d.hp = 0; d.sleep = d.confuse = d.stun = d.poison = d.slow = 0; s.kill = 1; }
					}
					if (a.team != d.team && !held && !UnitHasPas(d, PAS_NOSLOW)) d.ct = max(0, d.ct - 500);
				}
				}
			}
			out[n++] = s;
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
		bool corpse = y.hp <= 0;
		if (corpse && !atkTurn) return true;
		int sk = atkTurn ? skill : -1;
		int dmg, hit, crit;
		StrikeCalc(b, x, y, sk, dmg, hit, crit);
		Strike s = {};
		s.attacker = atkTurn ? ai : di;
		s.defender = atkTurn ? di : ai;
		s.skill = sk;
		s.counter = atkTurn ? 0 : 1;
		s.volley = volley;
		if (corpse) {
			s.hit = 1;
			s.overkill = 1;
			s.crit = (int)(r.Next() % 100) < crit;
			s.dmg = atkTurn ? scaleForm(s.crit ? dmg * 2 : dmg, y.x, y.z) : (s.crit ? dmg * 2 : dmg);
			AddCp(x, 5 + s.dmg / 12);
		} else {
		s.hit = (int)(r.Next() % 100) < hit;
		if (s.hit) {
			s.crit = (int)(r.Next() % 100) < crit;
			s.dmg = atkTurn ? scaleForm(s.crit ? dmg * 2 : dmg, y.x, y.z) : (s.crit ? dmg * 2 : dmg);
			if ((long long)s.dmg >= (long long)y.hp * 2) s.overkill = 1;
			y.hp -= s.dmg;
			AddCp(x, 5 + s.dmg / 12);
			AddCp(y, 7 + s.dmg / 8);
			if (sk >= 0 && SkillInfo(sk).effect == EF_DRAIN) {
				int h = s.dmg / 2;
				x.hp = min(x.ch.stat[S_HP], x.hp + h);
				s.drain = h;
			}
			bool held = UnitAilmentHeld(y);
			int fx = (sk >= 0) ? SkillInfo(sk).effect : EF_NONE;
			if (s.dmg > 0 && !(held && TurnFx(fx))) y.sleep = 0;
			if (fx != EF_NONE && fx != EF_DRAIN && fx != EF_IGNDEF)
				s.landed = ApplyEffect(b, x, y, fx, r) ? 1 : 0;
			if (y.hp <= 0) {
				if (!y.pasUsed && UnitHasPas(y, PAS_GUARD)) { y.hp = 1; y.pasUsed = 1; }
				else if (GutsHold(y, x, r)) s.guts = 1;
				else { y.hp = 0; y.sleep = y.confuse = y.stun = y.poison = y.slow = 0; s.kill = 1; }
			}
			if (x.team != y.team && !held && !UnitHasPas(y, PAS_NOSLOW)) y.ct = max(0, y.ct - (500 + (s.crit ? 250 : 0)));
		}
		}
		out[n++] = s;
		return s.kill != 0;
	};
	for (int v = 0; aLeft > 0 && n < maxOut; aLeft--, v++) {
		if (a.hp <= 0) break;
		swing(true, v);
	}
	for (int v = 0; dLeft > 0 && n < maxOut; dLeft--, v++) {
		if (a.hp <= 0 || d.hp <= 0) break;
		if (d.sleep > 0 || d.stun > 0) break;
		if (swing(false, v)) break;
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
			if (!UnitAilmentHeld(y) && !UnitHasPas(y, PAS_NOSLOW)) y.ct = max(0, y.ct - (500 + (s.crit ? 250 : 0)));
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
			if (occ[z][x] != 0) continue;
			occ[z][x] = i + 1;
		}
	}
}

int Battle::AliveCount(int team) const
{
	int n = 0;
	for (int i = 0; i < nUnits; i++) if (u[i].alive && u[i].team == team) n++;
	return n;
}

static int Speed(const Unit& u)
{
	int s = 10 + UnitStat(u, S_AGI);
	if (UnitHasSkill(u, SKL_P_SWIFT)) s = s * 12 / 10;
	return s;
}

int Battle::NextActor()
{
	for (int guard = 0; guard < 100000; guard++) {
		int best = -1;
		for (int i = 0; i < nUnits; i++) {
			if (!u[i].alive || u[i].alert != 1 || u[i].ct < 1000) continue;
			if (best < 0 || u[i].ct > u[best].ct || (u[i].ct == u[best].ct && u[i].team < u[best].team)) best = i;
		}
		if (best >= 0) return best;
		for (int i = 0; i < nUnits; i++) if (u[i].alive && u[i].alert == 1) u[i].ct += Speed(u[i]);
	}
	return -1;
}

int Battle::PredictOrder(int* out, int n, int acting, int cost, int* insertAt) const
{
	int ct[MAX_UNITS];
	for (int i = 0; i < nUnits; i++) ct[i] = u[i].ct;
	if (acting >= 0 && acting < nUnits) ct[acting] = max(0, ct[acting] - cost);
	if (insertAt) *insertAt = -1;
	int got = 0;
	for (int guard = 0; guard < 100000 && got < n; guard++) {
		int best = -1;
		for (int i = 0; i < nUnits; i++) {
			if (!u[i].alive || u[i].alert != 1 || ct[i] < 1000) continue;
			if (best < 0 || ct[i] > ct[best] || (ct[i] == ct[best] && u[i].team < u[best].team)) best = i;
		}
		if (best >= 0) {
			if (insertAt && *insertAt < 0 && best == acting) *insertAt = got;
			out[got++] = best;
			ct[best] -= 1000;
			continue;
		}
		bool any = false;
		for (int i = 0; i < nUnits; i++) if (u[i].alive && u[i].alert == 1) { ct[i] += Speed(u[i]); any = true; }
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

static int PickMonster(int stage, Rng& r)
{
	EnsureMonsters();
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

static int BossMonster(int stage, Rng& r)
{
	EnsureMonsters();
	if (stage >= 80 && (stage % 20) == 0) return M_DRAGON;
	if (stage >= 24 && r.Range(0, 3) == 0) return M_WYVERN;
	if (stage >= 16 && r.Range(0, 3) == 0) return M_GOLEM;
	if (stage >= 30 && r.Range(0, 2) == 0) return M_DARK_KNIGHT;
	int id = PickPool(POOL_FAN, stage, r);
	if (id >= 0 && r.Range(0, 1) == 0) return id;
	return PickClassic(stage, r);
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
	return true;
}

static bool FootCanLeave(const Battle& b, int ui, int x, int z)
{
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	for (int k = 0; k < 4; k++)
		if (FootStep(b, ui, x, z, x + dx[k], z + dz[k])) return true;
	return false;
}

static bool PlaceFoot(Battle& b, int ui, int gx, int gz)
{
	Unit& u = b.u[ui];
	int s = UnitSpan(u);
	int rad = s <= 1 ? 8 : 16;
	int bestLeave = 99999, lx = -1, lz = -1;
	int bestAny = 99999, ax = -1, az = -1;
	BOOL needStep = UnitMove(u) >= 1 && UnitMoveType(u) != MV_FLY;
	for (int dz = -rad; dz <= rad; dz++) for (int dx = -rad; dx <= rad; dx++) {
		int x = gx + dx, z = gz + dz;
		if (!FootFits(b, ui, x, z)) continue;
		int contains = (gx >= x && gx < x + s && gz >= z && gz < z + s) ? 0 : 1000;
		int rank = contains + abs(dx) + abs(dz);
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
		if (FootCanLeave(b, i, u.x, u.z)) continue;
		int gx = u.x, gz = u.z;
		u.x = -1; u.z = -1;
		b.RebuildOcc();
		if (!PlaceFoot(b, i, gx, gz) || !FootCanLeave(b, i, u.x, u.z)) {
			if (u.x < 0) { u.x = gx; u.z = gz; }
			b.RebuildOcc();
			CarveFootExit(b, i);
		}
		b.RebuildOcc();
	}
}

void SetupBattle(Battle& b, int stage, DWORD seed, const CharData* party, const int* rosterIdx, int nParty, int theme)
{
	memset(&b, 0, sizeof(b));
	stage = Clampi(stage, 1, TAC_STAGE_MAX);
	b.stage = stage;
	GenerateMap(b.map, stage, seed, theme);
	b.weather = WX_CLEAR;
	{
		int th = b.map.theme;
		if (th == TH_SNOW) b.weather = (seed % 5u) ? WX_SNOW : WX_CLEAR;
		else if (th == TH_VOLCANO) b.weather = (seed % 4u) ? WX_ASH : WX_CLEAR;
		else if (th != TH_DESERT && (seed % 3u) == 0) b.weather = WX_RAIN;
	}
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
		u.span = 1;
		u.x = b.map.pStartX[i]; u.z = b.map.pStartZ[i];
		u.face = faceToE;
		u.ct = r.Range(0, 300) + UnitStat(u, S_AGI) * 8;
		if (UnitHasPas(u, PAS_OPEN2)) u.ct += 400;
		else if (UnitHasPas(u, PAS_OPEN)) u.ct += 250;
		u.hpShow = (float)u.hp;
		u.grp = -1;
		u.alert = 1;
		n++;
	}
	b.nUnits = n;
	b.RebuildOcc();
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
			MakeEnemy(u, use, lv, boss ? 1 : 0, r);
			u.face = faceToE + 3.14159f;
			u.ct = r.Range(0, 280) + UnitStat(u, S_AGI) * 6;
			u.hpShow = (float)u.hp;
			u.grp = g;
			u.alert = 0;
			if (!PlaceFoot(b, n, grp.x[k], grp.z[k])) continue;
			n++;
			b.nUnits = n;
			b.RebuildOcc();
		}
	}
	b.nUnits = n;
	b.RebuildOcc();
	RescueStuckFeet(b);
	WakeNearPlayers(b, 16, 1);
	b.stampede = 0;
	b.stampedeLeft = 0;
	b.stampedeLine = 0;
	b.stampedeShow = 0;
	int foes = b.AliveCount(TEAM_ENEMY);
	// 以前は 8 回に 1 で、波が 1 か 2。続くと重いので 16 回に 1、波は 1。
	if (foes >= 8 && (seed % 16u) == 3u) {
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

int TurnStartEffects(Battle& b, int ui, int* healOut, int* poisonOut)
{
	Unit& u = b.u[ui];
	*healOut = 0; *poisonOut = 0;
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
			int h = max(1, u.ch.stat[S_HP] * rcv / 100);
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
		}
		if (mpr > 0 && u.ch.stat[S_MP] > 0) u.mp = min(u.ch.stat[S_MP], u.mp + mpr);
	}
	if (UnitHasPas(u, PAS_NOPOISON)) u.poison = 0;
	if (UnitHasPas(u, PAS_NOSLOW)) u.slow = 0;
	if (UnitHasPas(u, PAS_NODOWN)) u.defDown = 0;
	if (UnitHasPas(u, PAS_NOSLEEP)) u.sleep = 0;
	if (UnitHasPas(u, PAS_NOCONFUSE)) u.confuse = 0;
	if (UnitHasPas(u, PAS_NOSTUN)) u.stun = 0;
	if (u.poison > 0) {
		int d = max(1, u.ch.stat[S_HP] * 5 / 100);
		u.hp = max(1, u.hp - d);
		*poisonOut = d;
		u.poison--;
	}
	if (u.slow > 0) u.slow--;
	if (u.buffDef > 0) u.buffDef--;
	if (u.buffAgi > 0) u.buffAgi--;
	if (u.buffAtk > 0) u.buffAtk--;
	if (u.defDown > 0) u.defDown--;
	if (u.mulTurns > 0) u.mulTurns--;
	return 0;
}

void WakeGroup(Battle& b, int grp)
{
	if (grp < 0) return;
	for (int i = 0; i < b.nUnits; i++)
		if (b.u[i].alive && b.u[i].grp == grp) b.u[i].alert = 1;
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
static bool ScoutSee(const Map& m, int ex, int ez, int px, int pz)
{
	if (!Map::In(ex, ez) || !Map::In(px, pz)) return false;
	if ((int)m.At(ex, ez).h + 2 <= (int)m.At(px, pz).h) return false;
	return LineOfSight(m, ex, ez, px, pz);
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
			if (ScoutSee(b.map, e.x + ex, e.z + ez, p.x + px, p.z + pz)) return true;
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
			u.x = ox; u.z = oz;
		} else {
			int ax = -1, az = -1, best = 999;
			for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++) {
				int px = ox - dx, pz = oz - dz;
				if (!FootFits(b, ui, px, pz)) continue;
				if (dx + dz < best) { best = dx + dz; ax = px; az = pz; }
			}
			if (ax < 0) return 0;
			u.x = ax; u.z = az;
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
	for (int iz = 0; iz < s; iz++) for (int ix = 0; ix < s; ix++) {
		int x = nx + ix, z = nz + iz;
		if (!Map::In(x, z)) return false;
		int o = b.UnitAt(x, z);
		if (o >= 0 && o != ti) return false;
		int t = b.map.At(x, z).t;
		bool pit = (t == T_CHASM || t == T_LAVA);
		if (!pit && TerrainInfo(t).costFoot >= 99) return false;
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
	d.x = nx; d.z = nz;
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
		if (rc.cost[z][x] < 0 || !rc.canStop[z][x]) continue;
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

void AiDecide(const Battle& b, int ui, AiPlan& p, Rng& r)
{
	const Unit& u = b.u[ui];
	p.moveX = u.x; p.moveZ = u.z; p.target = -1; p.skill = -1; p.castX = u.x; p.castZ = u.z; p.wild = 0;
	if (u.sleep > 0 || u.stun > 0) return;
	if (u.confuse > 0) { ConfusePlan(b, ui, p, r); return; }
	static Reach rc;
	ComputeReach(b, ui, rc);
	float best = -1e9f;
	BOOL tryArt = (r.Next() % 100) < 36;
	BOOL tryOugi = (r.Next() % 100) < 14;
	Battle& bb = const_cast<Battle&>(b);
	Unit& um = bb.u[ui];
	const int ox = u.x, oz = u.z;
	// 体力半分以下のボスは、踏み込むより距離を取る。射撃が届く位置を優先し、防御を上げる技があれば先に使う。
	const int cornered = u.boss && u.hp * 2 <= max(1, u.ch.stat[S_HP]);
	static short nearD[MAPN][MAPN];
	int farReach = 0;
	if (cornered) {
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			nearD[z][x] = 999;
			if (rc.cost[z][x] < 0 || !rc.canStop[z][x]) continue;
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
		if (rc.cost[z][x] < 0 || !rc.canStop[z][x]) continue;
		um.x = x; um.z = z;
		int standT = b.map.At(x, z).t;
		float place = TerrainInfo(standT).defPct * .25f - rc.cost[z][x] * .05f;
		if (standT == T_LAVA) place -= 30.f;
		else if (standT == T_WATER) place -= 2.f;
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
			flee += r.F(0, 3.f);
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
			sc += place + r.F(0, 14.f);
			if (sc > best) { best = sc; p.moveX = x; p.moveZ = z; p.target = i; p.skill = -1; p.castX = ax; p.castZ = az; }
		}
		// スキル
		for (int s = 0; s < SKL_COUNT; s++) {
			if (!SkillUsable(b, ui, s)) continue;
			const SkillDef& sd = SkillInfo(s);
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
						if (q.hp * 100 / max(1, q.ch.stat[S_HP]) < 65) h += min(miss, HealAmount(u, q, sd, s));
						if (sd.effect == EF_CLEANSE && (q.poison > 0 || q.sleep > 0 || q.confuse > 0 || q.stun > 0 || q.slow > 0 || q.defDown > 0)) h += 48;
					}
					if (h <= 0) continue;
					sc += h * 1.2f;
				} else if (sd.kind == SK_BUFF) {
					if (!cornered || sd.effect != EF_BUFF_DEF || u.buffDef > 0) continue;
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
				sc += r.F(0, 10.f);
				if (sc > best) { best = sc; p.moveX = x; p.moveZ = z; p.target = i; p.skill = s; p.castX = tx; p.castZ = tz; }
			}
		}
		for (int ck = 0; ck < CRAFT_SLOT_N; ck++) {
			int cs = u.craftEq[ck];
			if (cs < CRAFT_BASE) continue;
			BOOL ougi = IsCraftSuper(cs);
			const SkillDef& csd = SkillInfo(cs);
			int defBuff = cornered && csd.effect == EF_BUFF_DEF && u.buffDef <= 0;
			if ((ougi ? !tryOugi : !tryArt) && !defBuff) continue;
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
					sc += r.F(0, 10.f);
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
		if (rc.cost[z][x] < 0 || !rc.canStop[z][x]) continue;
		int d = df[z][x];
		if (d >= 30000) continue;
		float sc = -d * 10.f + TerrainInfo(b.map.At(x, z).t).defPct * .2f + r.F(0, .5f);
		if (UnitRangeMax(u) >= 2 && d <= 1) sc -= 15.f;
		if (sc > bs) { bs = sc; p.moveX = x; p.moveZ = z; }
	}
	if (bs < -1e8f || df[p.moveZ][p.moveX] > bd) { p.moveX = u.x; p.moveZ = u.z; }
}

// ============================================================ 武具（生成表: tactics_src/gear_catalog.inc）
#include "tactics_src/gear_catalog.inc"
int GearCount() { return kGearN; }
const GearDef* GearOf(int id)
{
	if (id < 1 || id > kGearN) return NULL;
	return &kGear[id - 1];
}
const wchar_t* GearName(int id)
{
	if (id < 1 || id > kGearN) return L"?";
	const wchar_t* const* s = kGearNm[id - 1];
	return LangPick14(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]);
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
	if (t > 10) return 10;
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
void ItemExtraAdd(SaveData& s, int i, int d)
{
	if (i < 0 || i >= IX_N) return;
	int c = ItemExtraGet(s, i) + d;
	if (c < 0) c = 0;
	if (c > 60000) c = 60000;
	unsigned shift = (unsigned)((i & 1) * 16);
	unsigned mask = 0xFFFFu << shift;
	unsigned v = (unsigned)s.reserved[i >> 1];
	v = (v & ~mask) | ((unsigned)c << shift);
	s.reserved[i >> 1] = (int)v;
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
	if (id < 1 || id > kGearN || s.gearN < 0 || s.gearN >= GEAR_STASH) return false;
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
	if (index < 0 || index >= kGearN) return false;
	if (s.gold < kGear[index].price) return false;
	if (!StashAdd(s, index + 1)) return false;
	s.gold -= kGear[index].price;
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
		for (int i = 0; i < kGearN && n < 512; i++) {
			int req = kGear[i].req > 0 ? kGear[i].req : 1;
			if (wantLine >= 0 && kGear[i].line != wantLine) continue;
			if (req < reqLo || req > reqHi) continue;
			pool[n++] = i;
		}
	}
	if (n < 1) return -1;
	int gi = pool[r.Range(0, n - 1)];
	if (!StashAdd(s, gi + 1)) {
		s.gold += max(1, kGear[gi].price / 3);
		return -1;
	}
	return gi + 1;
}
bool DropConsumable(SaveData& s, int monster, Rng& r, wchar_t* name, int nameN)
{
	int line = MonsterDropLine(monster);
	int tx = TX_IT_ELIXIR;
	switch (line) {
	case 0: s.itemN[IT_MIGHT]++; tx = TX_IT_MIGHT; break;
	case 1: ItemExtraAdd(s, IX_ATK, 1); tx = TX_IT_ATK; break;
	case 2: s.itemN[IT_DELAY]++; tx = TX_IT_DELAY; break;
	case 3: s.itemN[IT_RAISE]++; tx = TX_IT_RAISE; break;
	case 4: ItemExtraAdd(s, IX_ETHER, 1); tx = TX_IT_ETHER; break;
	default: s.itemN[IT_ELIXIR]++; break;
	}
	wchar_t buf[96];
	wcscpy_s(buf, TT(tx));
	if (r.Chance(45)) {
		int k = r.Range(0, IX_N - 1);
		ItemExtraAdd(s, k, 1);
		wcscat_s(buf, L"  ");
		wcscat_s(buf, TT(TX_IT_FULL + k));
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
bool GoodBuy(SaveData& s, int i)
{
	const GoodDef* g = GoodAt(i);
	if (!g || s.gold < g->price || GoodHave(s, i) >= 99) return false;
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
	unsigned plainCap = 8 + TAC_SLOT_NAME * 2;
	int nfiles = 0;
	for (int k = 0; k < SLF_N; k++) {
		TCHAR leaf[96];
		SlotLeaf(leaf, 96, k, slot);
		part[k] = TacReadLeafBytes(leaf, &psz[k]);
		if (!part[k] && DatArc_Exists(leaf)) {
			for (int i = 0; i < SLF_N; i++) delete[] part[i];
			return false;
		}
		if (part[k]) { plainCap += 8 + psz[k]; nfiles++; }
	}
	BYTE* plain = new BYTE[plainCap];
	unsigned o = 0;
	TacPut32(plain + o, 0x50434154u); o += 4;
	TacPut32(plain + o, 1); o += 4;
	wchar_t nm[TAC_SLOT_NAME];
	memset(nm, 0, sizeof(nm));
	if (g_slotName[slot][0]) wcsncpy_s(nm, g_slotName[slot], _TRUNCATE);
	memcpy(plain + o, nm, sizeof(nm)); o += (unsigned)sizeof(nm);
	TacPut32(plain + o, (unsigned)nfiles); o += 4;
	for (int k = 0; k < SLF_N; k++) {
		if (!part[k]) continue;
		TacPut32(plain + o, (unsigned)k); o += 4;
		TacPut32(plain + o, psz[k]); o += 4;
		if (psz[k]) memcpy(plain + o, part[k], psz[k]);
		o += psz[k];
		delete[] part[k];
		part[k] = NULL;
	}
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
	if (need < 8 + TAC_SLOT_NAME * 2 || need > 32ull * 1024ull * 1024ull) { delete[] comp; return false; }
	BYTE* plain = new BYTE[(size_t)need];
	size_t got = ZSTD_decompress(plain, (size_t)need, comp, csz);
	delete[] comp;
	if (ZSTD_isError(got) || got != (size_t)need) { delete[] plain; return false; }
	if (TacGet32(plain) != 0x50434154u || TacGet32(plain + 4) != 1) { delete[] plain; return false; }
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

void SaveDefaults(SaveData& s)
{
	memset(&s, 0, sizeof(s));
	s.magic = kSaveMagic;
	s.version = 4;
	for (int i = 0; i < MAX_PARTY; i++) s.party[i] = -1;
	s.stageSel = 1;
	s.camSpeed = 1;
	s.quality = 1;
	s.showGrid = 1;
	s.uidNext = 1;
	s.bgmVol = 6;
	s.seVol = 8;
	s.optInit = 1;
	s.partyCap = 5;
	s.rosterCap = 20;
}

#pragma pack(push, 4)
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
// version 3。能力は現行と同じで、編成だけ 10。
struct SaveV3 {
	DWORD magic;
	int version;
	CharData roster[MAX_ROSTER];
	int party[10];
	int stageCleared, stageSel, winX, winY, winW, winH;
	int camSpeed, quality, showGrid, battleAnim;
	DWORD uidNext;
	int winSizeVer;
	int itemN[8];
	int bgmVol, seVol, optInit, gold, gearN, gearId[12], reserved[4];
	int partyCap, rosterCap;
};
#pragma pack(pop)
static_assert(sizeof(SaveData) == sizeof(SaveV3) + 5 * sizeof(int), "編成 15 は version 3 より 5 枠");

static int TacPeekClearBytes(FILE* fp, long sz)
{
	if (!fp) return 0;
	if (sz == (long)sizeof(SaveData)) {
		SaveData peek;
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
			for (int i = 0; i < MAX_ROSTER; i++) s.roster[i] = o.roster[i];
			for (int i = 0; i < 10; i++) s.party[i] = o.party[i];
			CopySaveTail(s, o.stageCleared, o.stageSel, o.winX, o.winY, o.winW, o.winH,
				o.camSpeed, o.quality, o.showGrid, o.battleAnim, o.uidNext, o.winSizeVer,
				o.itemN, o.bgmVol, o.seVol, o.optInit, o.gold, o.gearN, o.gearId, o.reserved, o.partyCap, o.rosterCap);
			got = TRUE;
		}
	} else if (sz == (long)sizeof(SaveData)) {
		SaveData t;
		if (fread(&t, 1, sizeof(t), fp) == sizeof(t) && t.magic == kSaveMagic && t.version == 4) {
			s = t;
			got = TRUE;
		}
	}
	fclose(fp);
	if (!got) return false;
	s.version = 4;
	for (int i = 0; i < MAX_ROSTER; i++) {
		CharData& c = s.roster[i];
		c.name[NAME_LEN - 1] = 0;
		if (!c.used) continue;
		c.cls = Clampi(c.cls, 0, C_JOB0 + JobCount() - 1);
		c.lv = Clampi(c.lv, 1, TAC_LV_MAX);
		if (recalc) CharRecalcProgress(c);
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
	s.stageCleared = Clampi(s.stageCleared, 0, TAC_STAGE_MAX);
	s.stageSel = Clampi(s.stageSel, 1, min(TAC_STAGE_MAX, s.stageCleared + 1));
	if (s.uidNext == 0) s.uidNext = 1;
	s.camSpeed = Clampi(s.camSpeed, 0, 2);
	if (s.optInit != 1) { s.bgmVol = 6; s.seVol = 8; s.optInit = 1; }
	else { s.bgmVol = Clampi(s.bgmVol, 0, 10); s.seVol = Clampi(s.seVol, 0, 10); }
	if (s.gold < 0) s.gold = 0;
	if (s.partyCap < 5 || s.partyCap > MAX_PARTY) s.partyCap = 5;
	if (s.rosterCap < 20 || s.rosterCap > MAX_ROSTER || (s.rosterCap % 20) != 0) s.rosterCap = 20;
	for (int i = s.partyCap; i < MAX_PARTY; i++) s.party[i] = -1;
	s.gearN = Clampi(s.gearN, 0, GEAR_STASH);
	for (int i = 0; i < s.gearN; i++) if (s.gearId[i] < 1 || s.gearId[i] > kGearN) s.gearId[i] = 1;
	TrophyLoad();
	CraftLoad();
	return true;
}

bool SaveWrite(const SaveData& s)
{
	CString path = DatArc_Path(TacLeafSave());
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("wb")) != 0 || !fp) return false;
	bool ok = fwrite(&s, 1, sizeof(s), fp) == sizeof(s);
	fclose(fp);
	if (ok) { TrophySave(); CraftPrune(s); CraftSave(); DatArc_Commit(TacLeafSave()); }
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
	const wchar_t* a = ElemWord(md.weak[0]);
	const wchar_t* b = ElemWord(md.weak[1]);
	const wchar_t* none = LangPick14(L"なし", L"None", L"Aucune", L"Nessuna", L"Ninguna", L"없음", L"无", L"لا شيء", L"Нет", L"Keine", L"Nenhuma", L"Geen", L"Brak", L"Yok");
	if (a && b) swprintf_s(out, cap, L"%s %s・%s", WeakLabel(), a, b);
	else if (a || b) swprintf_s(out, cap, L"%s %s", WeakLabel(), a ? a : b);
	else swprintf_s(out, cap, L"%s %s", WeakLabel(), none);
}

} // namespace otac
