// OmakeTacticsData.cpp — おまけタクティクス: ルール・マップ生成・経路・戦闘・AI・セーブ

#include "stdafx.h"
#include "OmakeTacticsData.h"
#include "DatArchive.h"
#include <stdio.h>

namespace otac {

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
	// costFoot は平地=2。道=1（半分）、森・砂=3、沼=4。飛行は EnterCost で地形を無視する
	{ TX_TER_PLAIN,    2, 1, 0, 0 },
	{ TX_TER_FOREST,   3, 1, 10, 20 },
	{ TX_TER_HILL,     3, 1, 15, 10 },
	{ TX_TER_MOUNTAIN, 99, 0, 0, 0 },
	{ TX_TER_WATER,    99, 1, 0, 0 },
	{ TX_TER_CHASM,    99, 1, 0, 0 },
	{ TX_TER_LAVA,     99, 1, 0, 0 },
	{ TX_TER_SAND,     3, 1, 0, 5 },
	{ TX_TER_SNOW,     3, 1, 0, 5 },
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

void GenerateMap(Map& m, int stage, DWORD seed)
{
	memset(&m, 0, sizeof(m));
	Rng r(seed);
	m.seed = seed;
	m.stage = stage;
	int th;
	if (stage <= 1) th = TH_PLAINS;
	else if (stage % 10 == 0) th = (stage / 10) & 1 ? TH_VOLCANO : TH_RUINS;
	else th = r.Range(0, TH_COUNT - 1);
	m.theme = th;
	const ThemeParam& tp = kTheme[th];
	const DWORD ns = seed * 7 + 13;

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
	int pax = MAPN / 2, paz = MAPN / 2;
	int span = StampShape(m, stage, r, pax, paz);
	int eax = pax, eaz = paz;
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
			if (th == TH_SNOW) c.prop = PK_PINE;
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
			if ((th == TH_SWAMP || th == TH_PLAINS || th == TH_FOREST) && (c.var % 5) == 0) c.prop = PK_LILY;
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
	}
	if (m.nE > 0 && !FootReachable(m, m.pStartX[0], m.pStartZ[0], m.eStartX[0], m.eStartZ[0])) {
		for (int i = 0; i < np; i++) {
			Cell& c = m.c[pz[i]][px[i]];
			if (!FootPassable(c.t)) { c.t = T_ROAD; c.prop = PK_NONE; }
		}
	}
}

// ============================================================ クラス
#define L6(a,b,c,d,e,f,g,h) { {a,b},{c,d},{e,f},{g,h},{0,-1},{0,-1} }
#define L6F(a,b,c,d,e,f,g,h,i,j,k,l) { {a,b},{c,d},{e,f},{g,h},{i,j},{k,l} }
static const ClassDef kClass[C_PLAYER_COUNT] = {
	// name, desc, tier, magic, group, move, jump, rMin, rMax, mag, pow, look, style, base[10], grow[10], learn
	{ TX_CL_SOLDIER, TX_CLD_SOLDIER, 1, 0, G_MELEE, MV_FOOT, 2, 1, 1, 0, 5, WL_SWORD_SHIELD, AS_SLASH,
	  { 24, 4, 7, 6, 1, 2, 6, 6, 4, 5 }, { 45, 10, 55, 50, 5, 25, 40, 45, 35, 0 }, L6F(1, SKL_POWER, 2, SKL_BASH, 3, SKL_P_IRON, 4, SKL_VITAL, 5, SKL_CLEAVE, 6, SKL_DOUBLE) },
	{ TX_CL_FIGHTER, TX_CLD_FIGHTER, 1, 0, G_MELEE, MV_FOOT, 2, 1, 1, 0, 7, WL_AXE2, AS_CHOP2,
	  { 28, 2, 9, 4, 0, 1, 5, 5, 3, 5 }, { 55, 5, 65, 35, 0, 15, 35, 40, 30, 0 }, L6F(1, SKL_POWER, 2, SKL_P_MIGHT, 3, SKL_DOUBLE, 4, SKL_WHIRL, 5, SKL_BASH, 6, SKL_CLEAVE) },
	{ TX_CL_ARCHER, TX_CLD_ARCHER, 1, 0, G_SWIFT, MV_FOOT, 2, 2, 3, 0, 5, WL_CROSSBOW2, AS_SHOOT,
	  { 20, 4, 6, 3, 1, 3, 7, 9, 5, 5 }, { 40, 10, 45, 30, 5, 25, 50, 60, 40, 0 }, L6F(1, SKL_PIERCE, 2, SKL_P_CRIT, 3, SKL_ARROWRAIN, 4, SKL_STITCH, 5, SKL_SNIPE, 6, SKL_VITAL) },
	{ TX_CL_THIEF, TX_CLD_THIEF, 1, 0, G_SWIFT, MV_FOOT, 3, 1, 1, 0, 4, WL_KNIVES, AS_STAB,
	  { 19, 4, 5, 3, 1, 3, 10, 8, 8, 6 }, { 38, 10, 40, 25, 5, 25, 65, 55, 55, 0 }, L6F(1, SKL_VITAL, 2, SKL_P_EVADE, 3, SKL_STITCH, 4, SKL_P_SWIFT, 5, SKL_VENOMEDGE, 6, SKL_DOUBLE) },
	{ TX_CL_MAGE, TX_CLD_MAGE, 1, 1, G_MAGIC, MV_FOOT, 2, 1, 2, 1, 4, WL_STAFF, AS_CAST,
	  { 17, 14, 1, 2, 9, 7, 6, 6, 4, 4 }, { 30, 60, 5, 20, 65, 45, 40, 40, 35, 0 }, L6F(1, SKL_FIRE, 2, SKL_THUNDER, 3, SKL_BLIZZARD, 4, SKL_GRAVITY, 5, SKL_P_SPELL, 6, SKL_DARKNESS) },
	{ TX_CL_CLERIC, TX_CLD_CLERIC, 1, 1, G_HOLY, MV_FOOT, 2, 1, 2, 1, 2, WL_WAND_BOOK, AS_CAST,
	  { 18, 16, 2, 3, 7, 9, 5, 5, 6, 4 }, { 32, 65, 10, 22, 50, 60, 35, 35, 45, 0 }, L6F(1, SKL_HEAL, 2, SKL_PROTECT, 3, SKL_REFRESH, 4, SKL_HIGHHEAL, 5, SKL_BLESS, 6, SKL_HEALALL) },

	{ TX_CL_PALADIN, TX_CLD_PALADIN, 2, 0, G_HOLY, MV_FOOT, 2, 1, 1, 0, 8, WL_SWORD_BADGE, AS_SLASH,
	  { 34, 10, 11, 11, 4, 7, 7, 8, 6, 5 }, { 55, 25, 55, 60, 15, 40, 35, 45, 40, 0 }, L6(15, SKL_PROTECT, 20, SKL_HOLYBLADE, 26, SKL_P_IRON, 32, SKL_HEAL) },
	{ TX_CL_RUNEKNIGHT, TX_CLD_RUNEKNIGHT, 2, 1, G_MAGIC, MV_FOOT, 2, 1, 1, 0, 7, WL_SWORD_BOOK, AS_SLASH,
	  { 30, 16, 10, 8, 10, 8, 8, 8, 5, 5 }, { 48, 40, 45, 40, 45, 40, 40, 40, 35, 0 }, L6(15, SKL_RUNEBLADE, 18, SKL_FIRE, 24, SKL_THUNDER, 30, SKL_P_SPELL) },
	{ TX_CL_BERSERKER, TX_CLD_BERSERKER, 2, 0, G_MELEE, MV_FOOT, 2, 1, 1, 0, 10, WL_AXES, AS_SLASH,
	  { 40, 4, 15, 6, 0, 3, 8, 7, 3, 5 }, { 70, 5, 75, 30, 0, 15, 40, 40, 25, 0 }, L6(15, SKL_DOUBLE, 18, SKL_WHIRL, 24, SKL_P_MIGHT, 30, SKL_DRAGONFANG) },
	{ TX_CL_SNIPER, TX_CLD_SNIPER, 2, 0, G_SWIFT, MV_FOOT, 2, 2, 4, 0, 7, WL_CROSSBOW2, AS_SHOOT,
	  { 26, 6, 9, 5, 1, 5, 9, 14, 7, 5 }, { 42, 15, 50, 30, 5, 30, 50, 70, 45, 0 }, L6(15, SKL_PIERCE, 18, SKL_ARROWRAIN, 24, SKL_P_CRIT, 30, SKL_VITAL) },
	{ TX_CL_ASSASSIN, TX_CLD_ASSASSIN, 2, 0, G_SWIFT, MV_FOOT, 3, 1, 1, 0, 7, WL_KNIVES, AS_STAB,
	  { 24, 8, 9, 5, 2, 5, 14, 12, 10, 6 }, { 40, 20, 50, 25, 10, 30, 70, 60, 55, 0 }, L6(15, SKL_ASSASSINATE, 18, SKL_P_EVADE, 24, SKL_STITCH, 30, SKL_P_SWIFT) },
	{ TX_CL_WIZARD, TX_CLD_WIZARD, 2, 1, G_MAGIC, MV_FOOT, 2, 1, 3, 1, 6, WL_STAFF, AS_CAST,
	  { 22, 26, 1, 4, 15, 11, 8, 8, 5, 4 }, { 32, 70, 5, 22, 75, 50, 40, 40, 35, 0 }, L6(15, SKL_BLIZZARD, 18, SKL_THUNDER, 24, SKL_METEOR, 30, SKL_P_SPELL) },
	{ TX_CL_BISHOP, TX_CLD_BISHOP, 2, 1, G_HOLY, MV_FOOT, 2, 1, 2, 1, 4, WL_WAND_BOOK, AS_CAST,
	  { 24, 28, 3, 5, 12, 14, 7, 7, 8, 4 }, { 35, 70, 10, 25, 55, 65, 35, 35, 50, 0 }, { { 15, SKL_HIGHHEAL }, { 18, SKL_HOLY }, { 20, SKL_REFRESH }, { 24, SKL_HEALALL }, { 30, SKL_P_REGEN }, { 0, -1 } } },
	{ TX_CL_SKYKNIGHT, TX_CLD_SKYKNIGHT, 2, 0, G_SWIFT, MV_FLY, 9, 1, 1, 0, 7, WL_SWORD_SPIKE, AS_STAB,
	  { 28, 8, 10, 7, 3, 8, 11, 9, 7, 7 }, { 45, 20, 50, 35, 10, 40, 60, 50, 45, 0 }, L6(15, SKL_VITAL, 18, SKL_P_EVADE, 24, SKL_HASTE, 30, SKL_DRAGONFANG) },

	{ TX_CL_HERO, TX_CLD_HERO, 3, 0, G_MELEE, MV_FOOT, 3, 1, 1, 0, 12, WL_SWORD2, AS_CHOP2,
	  { 46, 14, 17, 13, 6, 10, 12, 13, 10, 6 }, { 60, 30, 65, 50, 20, 40, 50, 55, 50, 0 }, L6(35, SKL_BRAVE, 38, SKL_P_CRIT, 44, SKL_HOLYBLADE, 50, SKL_P_REGEN) },
	{ TX_CL_ARCHMAGE, TX_CLD_ARCHMAGE, 3, 1, G_MAGIC, MV_FOOT, 2, 1, 3, 1, 9, WL_STAFF, AS_CAST,
	  { 30, 40, 2, 7, 22, 16, 10, 10, 8, 5 }, { 38, 80, 5, 25, 85, 55, 45, 45, 40, 0 }, L6(35, SKL_METEOR, 38, SKL_HOLY, 44, SKL_DARKNESS, 50, SKL_HASTE) },
	{ TX_CL_SAINT, TX_CLD_SAINT, 3, 1, G_HOLY, MV_FOOT, 2, 1, 2, 1, 6, WL_WAND_BOOK, AS_CAST,
	  { 32, 42, 4, 8, 17, 22, 9, 9, 12, 5 }, { 40, 80, 10, 30, 60, 75, 40, 40, 60, 0 }, { { 35, SKL_HEALALL }, { 36, SKL_REFRESH }, { 38, SKL_HOLY }, { 44, SKL_BLESS }, { 50, SKL_P_REGEN }, { 0, -1 } } },
	{ TX_CL_PHANTOM, TX_CLD_PHANTOM, 3, 0, G_SWIFT, MV_FOOT, 4, 1, 2, 0, 9, WL_CROSS_KNIFE, AS_SHOOT1,
	  { 32, 12, 13, 8, 4, 8, 18, 16, 14, 7 }, { 45, 25, 55, 30, 15, 35, 80, 65, 65, 0 }, L6(35, SKL_ASSASSINATE, 38, SKL_ARROWRAIN, 44, SKL_P_SWIFT, 50, SKL_P_EVADE) },
	{ TX_CL_DRAGONLORD, TX_CLD_DRAGONLORD, 3, 0, G_MELEE, MV_FLY, 9, 1, 1, 0, 12, WL_SWORD2, AS_CHOP2,
	  { 44, 12, 16, 14, 5, 10, 12, 11, 8, 7 }, { 60, 25, 65, 55, 10, 40, 45, 45, 40, 0 }, L6(35, SKL_DRAGONFANG, 38, SKL_WHIRL, 44, SKL_P_IRON, 50, SKL_BRAVE) },
	{ TX_CL_ALCHEMIST, TX_CLD_ALCHEMIST, 2, 1, G_MAGIC, MV_FOOT, 2, 1, 2, 1, 5, WL_STAFF, AS_CAST,
	  { 24, 22, 4, 5, 12, 10, 8, 9, 7, 4 }, { 36, 55, 15, 28, 55, 50, 40, 45, 40, 0 }, { { 15, SKL_TRANSMUTE }, { 18, SKL_RENEW }, { 22, SKL_MIRROR }, { 28, SKL_SANCTUARY }, { 0, -1 }, { 0, -1 } } },
	{ TX_CL_ARCHALCHEMIST, TX_CLD_ARCHALCHEMIST, 3, 1, G_MAGIC, MV_FOOT, 2, 1, 3, 1, 8, WL_STAFF, AS_CAST,
	  { 32, 36, 6, 8, 18, 16, 10, 12, 10, 5 }, { 42, 70, 20, 32, 70, 60, 45, 50, 50, 0 }, { { 35, SKL_RUIN }, { 40, SKL_APOC }, { 48, SKL_TRANSMUTE }, { 55, SKL_P_SPELL }, { 0, -1 }, { 0, -1 } } },
};
#undef L6
#undef L6F
static const int kLineParent[TAC_RANK_LINES] = { C_HERO, C_ARCHMAGE, C_SAINT, C_PHANTOM, C_DRAGONLORD, C_ARCHALCHEMIST };
static const int kExtSkill[TAC_RANK_LINES][TAC_RANK_N] = {
	{ SKL_GODSLASH, SKL_CLEAVE, SKL_WARCRY, SKL_SMITE, SKL_GODSLASH, SKL_APOC, SKL_RUIN },
	{ SKL_RUIN, SKL_GRAVITY, SKL_APOC, SKL_MIRROR, SKL_SANCTUARY, SKL_RUIN, SKL_APOC },
	{ SKL_SANCTUARY, SKL_RENEW, SKL_SMITE, SKL_MIRROR, SKL_WARCRY, SKL_SANCTUARY, SKL_APOC },
	{ SKL_SNIPE, SKL_VENOMEDGE, SKL_CLEAVE, SKL_GRAVITY, SKL_GODSLASH, SKL_SNIPE, SKL_RUIN },
	{ SKL_APOC, SKL_CLEAVE, SKL_SMITE, SKL_GODSLASH, SKL_RUIN, SKL_WARCRY, SKL_APOC },
	{ SKL_TRANSMUTE, SKL_RENEW, SKL_MIRROR, SKL_RUIN, SKL_SANCTUARY, SKL_APOC, SKL_GRAVITY },
};
static ClassDef s_ext[TAC_RANK_LINES * TAC_RANK_N];
static int s_extOk = 0;
static void BuildExt()
{
	if (s_extOk) return;
	for (int line = 0; line < TAC_RANK_LINES; line++) {
		const ClassDef& p = kClass[kLineParent[line]];
		for (int r = 0; r < TAC_RANK_N; r++) {
			ClassDef& d = s_ext[line * TAC_RANK_N + r];
			d = p;
			int rank = 4 + r;
			d.tier = rank;
			d.nameTx = TX_RK_FIRST + line * TAC_RANK_N + r;
			d.descTx = TX_RK_LINE0 + line;
			d.weaponPow = p.weaponPow + r * 2;
			for (int s = 0; s < S_COUNT; s++) if (s != S_MOV) d.base[s] = p.base[s] + r * (s == S_HP || s == S_MP ? 4 : 1);
			int arrive = PromoteLevel(rank - 1);
			d.learn[0].lv = arrive; d.learn[0].skill = kExtSkill[line][r];
			int g = p.group;
			if (g < 0 || g >= G_COUNT) g = 0;
			for (int i = 1; i < 6; i++) {
				int og = (g + i) % G_COUNT;
				int slot = (r * 4 + i * 5) % 26;
				d.learn[i].lv = arrive + i * 2;
				d.learn[i].skill = SKL_ART0 + og * 26 + slot;
			}
		}
	}
	s_extOk = 1;
}
const ClassDef& ClassInfo(int c)
{
	if (c >= 0 && c < C_PLAYER_COUNT) return kClass[c];
	BuildExt();
	int i = c - C_PLAYER_COUNT;
	if (i < 0 || i >= TAC_RANK_LINES * TAC_RANK_N) return kClass[0];
	return s_ext[i];
}
int PromoteLevel(int tier)
{
	static const int k[10] = { 0, 15, 35, 55, 78, 105, 140, 180, 230, 290 };
	if (tier < 1) return 15;
	if (tier > 9) return 9999;
	return k[tier];
}
int ExpToNext(int lv)
{
	if (lv < 1) lv = 1;
	if (lv >= TAC_LV_MAX) return 999999;
	return 70 + lv * 10 + lv * lv / 10;
}
int NextPromoteClass(int cls)
{
	const ClassDef& cd = ClassInfo(cls);
	if (cd.tier >= 10 || cd.tier < 3) return -1;
	if (cls >= C_PLAYER_COUNT) {
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
	switch (skill) {
	case SKL_DRAGONFANG: case SKL_BRAVE: case SKL_METEOR: case SKL_ASSASSINATE:
	case SKL_GODSLASH: case SKL_RUIN: case SKL_APOC: return true;
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
	{ TX_SK_POWER, TX_SKD_POWER, SK_PHYS, 4, 150, -1, -1, 0, EL_NONE, 1, 0, EF_NONE, 0 },
	{ TX_SK_DOUBLE, TX_SKD_DOUBLE, SK_PHYS, 6, 80, -1, -1, 0, EL_NONE, 2, 0, EF_NONE, 0 },
	{ TX_SK_WHIRL, TX_SKD_WHIRL, SK_PHYS, 8, 100, 0, 0, 1, EL_WIND, 1, 0, EF_NONE, 0 },
	{ TX_SK_PIERCE, TX_SKD_PIERCE, SK_PHYS, 6, 110, -1, -1, 0, EL_NONE, 1, 0, EF_IGNDEF, 0 },
	{ TX_SK_STITCH, TX_SKD_STITCH, SK_PHYS, 6, 100, -1, -1, 0, EL_DARK, 1, 0, EF_SLOW, 0 },
	{ TX_SK_VITAL, TX_SKD_VITAL, SK_PHYS, 5, 120, -1, -1, 0, EL_NONE, 1, 40, EF_NONE, 0 },
	{ TX_SK_BASH, TX_SKD_BASH, SK_PHYS, 5, 120, -1, -1, 0, EL_EARTH, 1, 0, EF_DEFDOWN, 0 },
	{ TX_SK_DRAGONFANG, TX_SKD_DRAGONFANG, SK_PHYS, 14, 220, -1, -1, 0, EL_FIRE, 1, 10, EF_NONE, 0 },
	{ TX_SK_HOLYBLADE, TX_SKD_HOLYBLADE, SK_PHYS, 12, 170, -1, -1, 0, EL_HOLY, 1, 5, EF_NONE, 0 },
	{ TX_SK_ARROWRAIN, TX_SKD_ARROWRAIN, SK_PHYS, 12, 90, 2, 5, 1, EL_WIND, 1, 0, EF_NONE, 0 },
	{ TX_SK_ASSASSINATE, TX_SKD_ASSASSINATE, SK_PHYS, 12, 200, -1, -1, 0, EL_DARK, 1, 25, EF_NONE, 0 },
	{ TX_SK_BRAVE, TX_SKD_BRAVE, SK_PHYS, 18, 260, -1, -1, 0, EL_HOLY, 1, 15, EF_NONE, 0 },
	{ TX_SK_FIRE, TX_SKD_FIRE, SK_MAGIC, 4, 8, 1, 3, 0, EL_FIRE, 1, 0, EF_NONE, 0 },
	{ TX_SK_BLIZZARD, TX_SKD_BLIZZARD, SK_MAGIC, 8, 11, 1, 3, 1, EL_ICE, 1, 0, EF_NONE, 0 },
	{ TX_SK_THUNDER, TX_SKD_THUNDER, SK_MAGIC, 8, 14, 1, 4, 0, EL_THUNDER, 1, 0, EF_NONE, 0 },
	{ TX_SK_METEOR, TX_SKD_METEOR, SK_MAGIC, 20, 22, 2, 5, 2, EL_FIRE, 1, 0, EF_NONE, 0 },
	{ TX_SK_RUNEBLADE, TX_SKD_RUNEBLADE, SK_MAGIC, 6, 12, 1, 1, 0, EL_THUNDER, 1, 0, EF_NONE, 0 },
	{ TX_SK_HOLY, TX_SKD_HOLY, SK_MAGIC, 14, 18, 1, 3, 0, EL_HOLY, 1, 0, EF_NONE, 0 },
	{ TX_SK_DARKNESS, TX_SKD_DARKNESS, SK_MAGIC, 10, 16, 1, 3, 0, EL_DARK, 1, 0, EF_DEFDOWN, 0 },
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
	{ TX_SK_E_CRUSH, TX_SKD_E_CRUSH, SK_PHYS, 5, 160, -1, -1, 0, EL_EARTH, 1, 0, EF_NONE, 1 },
	{ TX_SK_E_DRAIN, TX_SKD_E_DRAIN, SK_PHYS, 5, 100, 1, 1, 0, EL_DARK, 1, 0, EF_DRAIN, 1 },
	{ TX_SK_E_SPIT, TX_SKD_E_SPIT, SK_PHYS, 3, 90, 1, 2, 0, EL_POISON, 1, 0, EF_SLOW, 1 },
	{ TX_SK_E_QUAKE, TX_SKD_E_QUAKE, SK_PHYS, 8, 130, 0, 0, 1, EL_EARTH, 1, 0, EF_NONE, 1 },
	{ TX_SK_REFRESH, TX_SKD_REFRESH, SK_BUFF, 8, 0, 1, 1, 0, EL_HOLY, 1, 0, EF_REACT, 0 },
	{ TX_SK_CLEAVE, TX_SKD_CLEAVE, SK_PHYS, 8, 110, 0, 0, 1, EL_NONE, 1, 0, EF_NONE, 0 },
	{ TX_SK_SNIPE, TX_SKD_SNIPE, SK_PHYS, 7, 140, 2, 6, 0, EL_WIND, 1, 25, EF_NONE, 0 },
	{ TX_SK_VENOMEDGE, TX_SKD_VENOMEDGE, SK_PHYS, 6, 100, -1, -1, 0, EL_POISON, 1, 0, EF_POISON, 0 },
	{ TX_SK_SANCTUARY, TX_SKD_SANCTUARY, SK_HEAL, 14, 16, 0, 0, 2, EL_HOLY, 1, 0, EF_NONE, 0 },
	{ TX_SK_SMITE, TX_SKD_SMITE, SK_MAGIC, 10, 16, 1, 3, 0, EL_HOLY, 1, 10, EF_NONE, 0 },
	{ TX_SK_GRAVITY, TX_SKD_GRAVITY, SK_MAGIC, 12, 12, 1, 3, 1, EL_EARTH, 1, 0, EF_SLOW, 0 },
	{ TX_SK_WARCRY, TX_SKD_WARCRY, SK_BUFF, 8, 0, 0, 0, 1, EL_FIRE, 1, 0, EF_BUFF_ATK, 0 },
	{ TX_SK_RENEW, TX_SKD_RENEW, SK_HEAL, 12, 40, 0, 2, 0, EL_HOLY, 1, 0, EF_NONE, 0 },
	{ TX_SK_MIRROR, TX_SKD_MIRROR, SK_BUFF, 8, 0, 0, 2, 0, EL_ICE, 1, 0, EF_BUFF_DEF, 0 },
	{ TX_SK_GODSLASH, TX_SKD_GODSLASH, SK_PHYS, 22, 300, -1, -1, 0, EL_HOLY, 1, 20, EF_NONE, 0 },
	{ TX_SK_RUIN, TX_SKD_RUIN, SK_MAGIC, 24, 30, 1, 4, 1, EL_DARK, 1, 0, EF_DEFDOWN, 0 },
	{ TX_SK_APOC, TX_SKD_APOC, SK_MAGIC, 28, 26, 0, 0, 2, EL_FIRE, 1, 0, EF_NONE, 0 },
	{ TX_SK_TRANSMUTE, TX_SKD_TRANSMUTE, SK_BUFF, 4, 0, 0, 1, 0, EL_EARTH, 1, 0, EF_BUFF_ATK, 0 },
};
static_assert(sizeof(kSkill) / sizeof(kSkill[0]) == SKL_ART0, "base skill table");

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
	s_artOk = 1;
}
const SkillDef& SkillInfo(int s)
{
	if (s == SKL_RAISE) {
		static const SkillDef raise = { TX_SK_RAISE, TX_SKD_RAISE, SK_HEAL, 12, 0, 1, 2, 0, EL_HOLY, 1, 0, EF_RAISE, 0 };
		return raise;
	}
	if (s >= SKL_ART0 && s < SKL_ARTN) {
		BuildArts();
		return s_art[s - SKL_ART0];
	}
	return kSkill[Clampi(s, 0, SKL_ART0 - 1)];
}

// ============================================================ モンスター
// name, look, model, wlook, move, jump, rMin, rMax, mag, pow, style, base, grow, skills, minStage, weight, scale, tint
static const MonsterDef kMon[M_COUNT] = {
	{ TX_MO_SLIME, LK_SLIME, -1, 0, MV_FOOT, 1, 1, 1, 0, 3, AS_BITE,
	  { 22, 4, 6, 4, 0, 4, 3, 4, 2, 3 }, { 60, 5, 40, 40, 0, 30, 20, 30, 20, 0 }, { SKL_E_SPIT, -1, -1 }, 1, 10, 1.0f, 0x60E070, 16, -1 },
	{ TX_MO_SKEL_MINION, LK_KAYKIT, 6, 1, MV_FOOT, 2, 1, 1, 0, 4, AS_SLASH,
	  { 18, 0, 6, 3, 0, 1, 5, 5, 2, 4 }, { 45, 0, 45, 30, 0, 15, 35, 40, 20, 0 }, { -1, -1, -1 }, 1, 10, 1.0f, 0xFFFFFF, 18, -1 },
	{ TX_MO_SKEL_WARRIOR, LK_KAYKIT, 5, 2, MV_FOOT, 2, 1, 1, 0, 6, AS_CHOP2,
	  { 26, 6, 9, 7, 0, 2, 4, 6, 2, 4 }, { 55, 10, 55, 50, 0, 20, 30, 40, 20, 0 }, { SKL_E_CRUSH, -1, -1 }, 3, 8, 1.0f, 0xFFFFFF, 24, -1 },
	{ TX_MO_SKEL_ARCHER, LK_KAYKIT, 7, 3, MV_FOOT, 2, 2, 3, 0, 5, AS_SHOOT,
	  { 18, 0, 7, 3, 0, 3, 6, 8, 3, 4 }, { 40, 0, 45, 25, 0, 20, 40, 55, 25, 0 }, { -1, -1, -1 }, 4, 7, 1.0f, 0xFFFFFF, 28, -1 },
	{ TX_MO_BAT, LK_BAT, -1, 0, MV_FLY, 9, 1, 1, 0, 4, AS_BITE,
	  { 15, 6, 6, 2, 0, 3, 11, 7, 6, 6 }, { 35, 10, 40, 20, 0, 25, 60, 45, 40, 0 }, { SKL_E_DRAIN, -1, -1 }, 5, 6, 1.0f, 0x806070, 32, -1 },
	{ TX_MO_LICH, LK_KAYKIT, 8, 4, MV_FOOT, 2, 1, 3, 1, 4, AS_CAST,
	  { 20, 20, 1, 3, 8, 8, 5, 6, 4, 4 }, { 40, 50, 5, 25, 60, 50, 30, 40, 30, 0 }, { SKL_FIRE, SKL_DARKNESS, SKL_HEAL }, 8, 5, 1.0f, 0xD8C8FF, 40, -1 },
	{ TX_MO_POISON_SLIME, LK_SLIME, -1, 0, MV_FOOT, 1, 1, 1, 0, 5, AS_BITE,
	  { 28, 8, 8, 6, 0, 6, 4, 5, 3, 3 }, { 65, 10, 45, 45, 0, 35, 25, 35, 20, 0 }, { SKL_E_VENOM, SKL_E_SPIT, -1 }, 12, 6, 1.15f, 0xB070E0, 36, -1 },
	{ TX_MO_GOLEM, LK_GOLEM, -1, 0, MV_FOOT, 1, 1, 1, 0, 8, AS_PUNCH,
	  { 40, 8, 12, 14, 0, 4, 2, 5, 1, 3 }, { 75, 10, 60, 70, 0, 25, 10, 30, 10, 0 }, { SKL_E_QUAKE, -1, -1 }, 15, 4, 1.0f, 0xFFFFFF, 48, -1 },
	{ TX_MO_WYVERN, LK_WYVERN, -1, 0, MV_FLY, 9, 1, 1, 0, 8, AS_BITE,
	  { 34, 12, 12, 8, 6, 6, 9, 8, 5, 6 }, { 60, 20, 60, 45, 20, 30, 45, 40, 30, 0 }, { SKL_E_BREATH, -1, -1 }, 20, 4, 1.0f, 0x7AA060, 60, -1 },
	{ TX_MO_DARK_KNIGHT, LK_KAYKIT, 5, 5, MV_FOOT, 2, 1, 1, 0, 9, AS_SLASH,
	  { 36, 10, 14, 13, 2, 8, 7, 9, 4, 5 }, { 60, 10, 65, 60, 10, 35, 35, 45, 25, 0 }, { SKL_E_CRUSH, SKL_BASH, -1 }, 30, 4, 1.12f, 0x8070A0, 70, -1 },
	{ TX_MO_DRAGON, LK_WYVERN, -1, 0, MV_FLY, 9, 1, 1, 0, 12, AS_BITE,
	  { 70, 30, 18, 14, 14, 12, 8, 10, 8, 5 }, { 90, 40, 70, 55, 60, 45, 35, 40, 30, 0 }, { SKL_E_BREATH, SKL_E_CRUSH, -1 }, 80, 1, 1.75f, 0xE05040, 0, -1 },
};

#include "tactics_src/mon_species.inc"
#include "tactics_src/mon_names.inc"
static const wchar_t* ImportLocName(int model)
{
	if (model < 0 || model >= kImportN) return L"?";
	const wchar_t* const* s = kImportLoc[model];
	return LangPick14(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]);
}

static MonsterDef s_monImp[160];
static int s_monImpOk = 0;

static void EnsureMonsters()
{
	if (s_monImpOk) return;
	for (int i = 0; i < kImportN && i < 160; i++) {
		MonsterDef& d = s_monImp[i];
		memset(&d, 0, sizeof(d));
		d.look = LK_MESH;
		d.model = i;
		d.nameTx = TX_MO_SLIME;
		d.nameB = -1;
		d.moveType = kImportFly[i] ? MV_FLY : MV_FOOT;
		d.jump = d.moveType == MV_FLY ? 9 : 2;
		d.rMin = 1;
		d.rMax = (i % 5 == 0) ? 2 : 1;
		d.magAttack = (i % 7 == 0) ? 1 : 0;
		d.weaponPow = 4 + (i % 9);
		d.attackStyle = d.magAttack ? AS_CAST : (d.moveType == MV_FLY ? AS_BITE : AS_SLASH);
		int tier = i / 12;
		d.base[S_HP] = 20 + tier * 6 + (i % 5) * 2;
		d.base[S_MP] = d.magAttack ? 14 + tier : 2;
		d.base[S_STR] = 5 + tier + (i % 4);
		d.base[S_DEF] = 3 + tier;
		d.base[S_MAG] = d.magAttack ? 6 + tier : 1;
		d.base[S_RES] = 3 + tier / 2;
		d.base[S_AGI] = 4 + (d.moveType == MV_FLY ? 3 : 0) + (i % 3);
		d.base[S_DEX] = 4 + (i % 4);
		d.base[S_LUK] = 2 + (i % 3);
		d.base[S_MOV] = d.moveType == MV_FLY ? 6 : 4;
		d.grow[S_HP] = 45 + tier * 4;
		d.grow[S_STR] = 40 + tier * 2;
		d.grow[S_DEF] = 30 + tier;
		d.grow[S_MAG] = d.magAttack ? 50 : 5;
		d.grow[S_RES] = 25;
		d.grow[S_AGI] = 30;
		d.grow[S_DEX] = 25;
		d.grow[S_MP] = d.magAttack ? 40 : 5;
		d.skills[0] = d.magAttack ? SKL_FIRE : (d.moveType == MV_FLY ? SKL_E_DRAIN : SKL_E_CRUSH);
		d.skills[1] = (i % 4 == 0) ? SKL_E_VENOM : -1;
		d.skills[2] = -1;
		d.minStage = 1;
		d.maxStage = 0;
		d.weight = 8;
		d.scale = 1.f;
		d.tint = 0xFFFFFF;
	}
	s_monImpOk = 1;
}

static int ImportPick(int stage, Rng& r, int bias)
{
	EnsureMonsters();
	int n = kImportN;
	if (n < 1) return M_SLIME;
	int origin = ((stage - 1) / 4) % n;
	int idx = origin + bias;
	if (idx >= n) idx -= n;
	if (idx < 0) idx += n;
	return M_COUNT + idx;
}

const MonsterDef& MonsterInfo(int m)
{
	EnsureMonsters();
	if (m >= M_COUNT && m < M_COUNT + kImportN) return s_monImp[m - M_COUNT];
	if (m < 0 || m >= M_COUNT) m = 0;
	return kMon[m];
}

const wchar_t* MonsterLabel(int m)
{
	const MonsterDef& d = MonsterInfo(m);
	if (d.look == LK_MESH && d.model >= 0 && d.model < kImportN) return ImportLocName(d.model);
	return TT(d.nameTx);
}


// ============================================================ キャラ
static int PaceSkill(int cls, int lv)
{
	if (lv < 7 || lv > 32) return -1;
	const ClassDef& cd = ClassInfo(cls);
	for (int i = 0; i < 6; i++) if (cd.learn[i].skill >= 0 && cd.learn[i].lv == lv) return -1;
	int g = cd.group;
	if (g < 0 || g >= G_COUNT) g = 0;
	return SKL_ART0 + g * 26 + (lv - 7);
}

static void GrantReached(CharData& c)
{
	const ClassDef& cd = ClassInfo(c.cls);
	for (int i = 0; i < 6; i++)
		if (cd.learn[i].skill >= 0 && cd.learn[i].lv > 0 && cd.learn[i].lv <= c.lv)
			CharGiveSkill(c, cd.learn[i].skill);
	int hi = c.lv < 32 ? c.lv : 32;
	for (int lv = 7; lv <= hi; lv++) {
		int sk = PaceSkill(c.cls, lv);
		if (sk >= 0) CharGiveSkill(c, sk);
	}
	if (cd.group == G_HOLY && c.lv >= 7) CharGiveSkill(c, SKL_RAISE);
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
	GrantReached(c);
}

void CharGiveSkill(CharData& c, int s)
{
	if (s < 0 || s >= 320) return;
	if (s < 64) { c.skills |= 1ull << s; return; }
	int i = s - 64;
	int w = i >> 5;
	if (w >= 0 && w < 6) c.reserved[w] |= (int)(1u << (i & 31));
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
	for (int i = 0; i < 6; i++) if (cd.learn[i].skill >= 0 && cd.learn[i].lv == lv) return cd.learn[i].skill;
	if (cd.group == G_HOLY && lv == 7) return SKL_RAISE;
	return PaceSkill(c.cls, lv);
}

int CharLevelUp(CharData& c, Rng& r, int gains[S_COUNT])
{
	const ClassDef& cd = ClassInfo(c.cls);
	int total = 0;
	for (int s = 0; s < S_COUNT; s++) {
		int g = cd.grow[s], add = 0;
		if (s == S_HP || s == S_MP) {
			add = g / 10 + ((int)(r.Next() % 10) < g % 10 ? 1 : 0);
		} else if (s != S_MOV) {
			add = g / 100 + ((int)(r.Next() % 100) < g % 100 ? 1 : 0);
		}
		gains[s] = add;
		c.stat[s] += add;
		if (s != S_HP && s != S_MP) total += add;
	}
	if (total == 0) {
		int s = cd.magic ? S_MAG : S_STR;
		gains[s]++; c.stat[s]++;
	}
	c.lv++;
	int sk = CharLearnAt(c, c.lv);
	if (sk >= 0 && !CharHasSkill(c, sk)) { CharGiveSkill(c, sk); return sk; }
	return -1;
}

// ============================================================ 進化
struct EvoRow { int from; EvoOption o[4]; };
static const EvoRow kEvo[] = {
	{ C_SOLDIER,    { { C_PALADIN, -1 }, { C_RUNEKNIGHT, G_MAGIC }, { C_SKYKNIGHT, G_SWIFT }, { -1, -1 } } },
	{ C_FIGHTER,    { { C_BERSERKER, -1 }, { C_PALADIN, G_HOLY }, { C_SKYKNIGHT, G_SWIFT }, { -1, -1 } } },
	{ C_ARCHER,     { { C_SNIPER, -1 }, { C_ASSASSIN, G_SWIFT }, { C_SKYKNIGHT, G_MELEE }, { -1, -1 } } },
	{ C_THIEF,      { { C_ASSASSIN, -1 }, { C_SNIPER, G_SWIFT }, { C_SKYKNIGHT, G_MELEE }, { -1, -1 } } },
	{ C_MAGE,       { { C_WIZARD, -1 }, { C_ALCHEMIST, -1 }, { C_RUNEKNIGHT, G_MELEE }, { C_BISHOP, G_HOLY } } },
	{ C_CLERIC,     { { C_BISHOP, -1 }, { C_PALADIN, G_MELEE }, { C_WIZARD, G_MAGIC }, { -1, -1 } } },
	{ C_PALADIN,    { { C_HERO, -1 }, { C_SAINT, G_HOLY }, { C_DRAGONLORD, G_SWIFT }, { -1, -1 } } },
	{ C_BERSERKER,  { { C_HERO, -1 }, { C_DRAGONLORD, G_SWIFT }, { -1, -1 }, { -1, -1 } } },
	{ C_RUNEKNIGHT, { { C_HERO, -1 }, { C_ARCHMAGE, G_MAGIC }, { -1, -1 }, { -1, -1 } } },
	{ C_SNIPER,     { { C_PHANTOM, -1 }, { C_DRAGONLORD, G_MELEE }, { -1, -1 }, { -1, -1 } } },
	{ C_ASSASSIN,   { { C_PHANTOM, -1 }, { C_HERO, G_MELEE }, { -1, -1 }, { -1, -1 } } },
	{ C_SKYKNIGHT,  { { C_DRAGONLORD, -1 }, { C_HERO, G_MELEE }, { C_PHANTOM, G_SWIFT }, { -1, -1 } } },
	{ C_WIZARD,     { { C_ARCHMAGE, -1 }, { C_SAINT, G_HOLY }, { -1, -1 }, { -1, -1 } } },
	{ C_BISHOP,     { { C_SAINT, -1 }, { C_ARCHMAGE, G_MAGIC }, { -1, -1 }, { -1, -1 } } },
	{ C_ALCHEMIST,  { { C_ARCHALCHEMIST, -1 }, { C_ARCHMAGE, G_MAGIC }, { -1, -1 }, { -1, -1 } } },
};

int EvoOptions(int cls, EvoOption out[4])
{
	int n = 0;
	for (int i = 0; i < (int)_countof(kEvo); i++) {
		if (kEvo[i].from != cls) continue;
		for (int k = 0; k < 4; k++) if (kEvo[i].o[k].target >= 0 && n < 4) out[n++] = kEvo[i].o[k];
	}
	int nxt = NextPromoteClass(cls);
	if (nxt >= 0 && n < 4) { EvoOption e; e.target = nxt; e.needGroup = -1; out[n++] = e; }
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

int EvoPartnersNeeded(const CharData& main)
{
	int t = ClassInfo(main.cls).tier;
	if (t >= 3) return 0;
	return t >= 2 ? 2 : 1;
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
	int inherited = 0;
	for (int i = 0; i < n && inherited < 2; i++) {
		for (int sk = 0; sk < SKL_COUNT && inherited < 2; sk++) {
			if (SkillInfo(sk).enemyOnly || !CharHasSkill(ps[i], sk) || CharHasSkill(c, sk)) continue;
			CharGiveSkill(c, sk);
			inherited++;
		}
	}
	GrantReached(c);
}

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
	if (s == S_DEF && u.buffDef > 0) v = v * 13 / 10;
	if ((s == S_DEF || s == S_RES) && u.defDown > 0) v = v * 75 / 100;
	if ((s == S_AGI) && u.buffAgi > 0) v = v * 13 / 10;
	if ((s == S_STR || s == S_MAG) && u.buffAtk > 0) v = v * 125 / 100;
	if (u.mulTurns > 0 && (s == S_STR || s == S_MAG || s == S_DEF || s == S_RES || s == S_AGI)) v *= 2;
	if (s == S_DEF && UnitHasSkill(u, SKL_P_IRON)) v = v * 115 / 100;
	return v;
}
int UnitMove(const Unit& u) { return u.ch.stat[S_MOV]; }
int UnitJump(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).jump : ClassInfo(u.ch.cls).jump; }
int UnitMoveType(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).moveType : ClassInfo(u.ch.cls).moveType; }
int UnitRangeMin(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).rMin : ClassInfo(u.ch.cls).rMin; }
int UnitRangeMax(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).rMax : ClassInfo(u.ch.cls).rMax; }
int UnitMagAttack(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).magAttack : ClassInfo(u.ch.cls).magAttack; }
int UnitWeaponPow(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).weaponPow + u.ch.lv / 6 : ClassInfo(u.ch.cls).weaponPow + u.ch.lv / 8; }
int UnitAttackStyle(const Unit& u) { return IsMon(u) ? MonsterInfo(u.monster).attackStyle : ClassInfo(u.ch.cls).attackStyle; }
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
bool UnitHasSkill(const Unit& u, int s) { return CharHasSkill(u.ch, s); }

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
	u.ch.lv = Clampi(lv, 1, TAC_LV_MAX);
	for (int s = 0; s < S_COUNT; s++) {
		int g = md.grow[s];
		int v = md.base[s];
		if (s == S_HP || s == S_MP) v += g * (u.ch.lv - 1) / 10;
		else if (s != S_MOV) v += g * (u.ch.lv - 1) / 100;
		if (s != S_MOV && s != S_MP) v += r.Range(-1, 1);
		u.ch.stat[s] = max(s == S_MP ? 0 : 1, v);
	}
	if (boss) {
		u.ch.stat[S_HP] = u.ch.stat[S_HP] * 3 / 2;
		u.ch.stat[S_STR] += 2; u.ch.stat[S_MAG] += 2; u.ch.stat[S_DEF] += 2; u.ch.stat[S_RES] += 2;
	}
	for (int i = 0; i < 3; i++) {
		int sk = md.skills[i];
		if (sk < 0) continue;
		int need = i == 0 ? 1 : (i == 1 ? 8 : 15);
		if (u.ch.lv >= need || boss) CharGiveSkill(u.ch, sk);
	}
	if (u.ch.lv >= 30) CharGiveSkill(u.ch, SKL_P_IRON);
	if (u.ch.lv >= 45) CharGiveSkill(u.ch, SKL_P_CRIT);
	u.hp = u.ch.stat[S_HP];
	u.mp = u.ch.stat[S_MP];
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
	return TerrainInfo(m.At(x, z).t).costFoot;
}

void ComputeReach(const Battle& b, int ui, Reach& out)
{
	const Unit& u = b.u[ui];
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) { out.cost[z][x] = -1; out.from[z][x] = -1; out.canStop[z][x] = 0; }
	const int mv = UnitMove(u) * 2, mt = UnitMoveType(u), jp = UnitJump(u);
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
			if (!CanStep(b.map, x, z, nx, nz, mt, jp)) continue;
			int o = b.UnitAt(nx, nz);
			if (o >= 0 && b.u[o].team != u.team) continue;
			int nc = c0 + EnterCost(b.map, nx, nz, mt);
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
		int o = b.UnitAt(x, z);
		out.canStop[z][x] = (o < 0 || o == ui) ? 1 : 0;
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

// ============================================================ 戦闘
static float HeightMod(const Battle& b, const Unit& a, const Unit& d)
{
	float dh = (b.map.StandY(a.x, a.z) - b.map.StandY(d.x, d.z)) / kHStep;
	return 1.f + Clampf(dh * .05f, -.15f, .25f);
}

enum { WT_NONE = 0, WT_SWORD, WT_LANCE, WT_AXE, WT_BOW };

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
	float ang = atan2f((float)(a.x - d.x), (float)(a.z - d.z));
	float diff = fabsf(WrapAngle(ang - d.face));
	if (diff > 2.3f) return 2;
	if (diff > 1.2f) return 1;
	return 0;
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

static void StrikeCalc(const Battle& b, const Unit& a, const Unit& d, int skill, int& dmg, int& hit, int& crit)
{
	const SkillDef* sd = skill >= 0 ? &SkillInfo(skill) : NULL;
	bool magic = sd ? (sd->kind == SK_MAGIC) : (UnitMagAttack(a) != 0);
	int tdef = TerrainDefPct(b, d), tavo = TerrainAvoPct(b, d);
	float dm;
	if (magic) {
		int atk = UnitStat(a, S_MAG) + (sd ? sd->power : UnitWeaponPow(a));
		if (UnitHasSkill(a, SKL_P_SPELL)) atk = atk * 115 / 100;
		float df = UnitStat(d, S_RES) * (1.f + tdef * .005f);
		dm = atk - df * .8f;
		hit = 90 + UnitStat(a, S_DEX) - UnitStat(d, S_AGI) - tavo / 2 + (sd ? 10 : 0);
		hit = Clampi(hit, 30, 100);
		crit = 0;
	} else {
		int atk = UnitStat(a, S_STR) + UnitWeaponPow(a);
		if (sd && skill == SKL_HOLYBLADE) atk += UnitStat(a, S_MAG) / 2;
		if (UnitHasSkill(a, SKL_P_MIGHT)) atk = atk * 115 / 100;
		float mult = sd ? sd->power / 100.f : 1.f;
		float df = UnitStat(d, S_DEF) * (1.f + tdef * .01f);
		if (sd && sd->effect == EF_IGNDEF) df *= .5f;
		dm = atk * mult - df;
		int evade = UnitStat(d, S_AGI) * 2 + UnitStat(d, S_LUK) / 2 + tavo + (UnitHasSkill(d, SKL_P_EVADE) ? 15 : 0);
		hit = 80 + UnitStat(a, S_DEX) * 2 + UnitStat(a, S_LUK) / 2 - evade + (sd ? 10 : 0);
		hit = Clampi(hit, 5, 99);
		crit = UnitStat(a, S_DEX) / 2 + UnitStat(a, S_LUK) / 3 - UnitStat(d, S_LUK) / 3 + (sd ? sd->critBonus : 0)
			+ (UnitHasSkill(a, SKL_P_CRIT) ? 10 : 0);
		crit = Clampi(crit, 0, sd ? 80 : 50);
	}
	dm *= HeightMod(b, a, d) * DirMod(a, d);
	int face = FacingOf(a, d);
	if (!magic) {
		if (face == 2) hit += 20;
		else if (face == 1) hit += 8;
		else hit -= 12;
	} else {
		if (face == 2) hit += 10;
		else if (face == 0) hit -= 8;
	}
	int tri = TriRel(WeaponOf(a), WeaponOf(d));
	if (tri > 0) { hit += 15; dm *= 1.1f; }
	if (tri < 0) { hit -= 15; dm *= 0.9f; }
	if (WeaponOf(a) == WT_BOW && UnitMoveType(d) == MV_FLY) { hit += 20; dm *= 1.15f; }
	hit = Clampi(hit, magic ? 20 : 5, magic ? 100 : 99);
	dmg = max(1, (int)roundf(dm));
}

static bool CanCounter(const Battle& b, const Unit& d, const Unit& a)
{
	if (!d.alive || d.hp <= 0) return false;
	return InAttackRange(b, d.x, d.z, UnitRangeMin(d), UnitRangeMax(d), a.x, a.z, true);
}

void CalcForecast(const Battle& b, int ai, int di, int skill, Forecast& f)
{
	memset(&f, 0, sizeof(f));
	const Unit& a = b.u[ai];
	const Unit& d = b.u[di];
	StrikeCalc(b, a, d, skill, f.dmg, f.hit, f.crit);
	f.face = FacingOf(a, d);
	f.tri = TriRel(WeaponOf(a), WeaponOf(d));
	f.bowFly = (WeaponOf(a) == WT_BOW && UnitMoveType(d) == MV_FLY) ? 1 : 0;
	f.delay = (a.team != d.team) ? 500 : 0;
	f.follow = -1;
	f.fdmg = 0;
	if (a.team != d.team) {
		int best = -1, bs = -1;
		for (int i = 0; i < b.nUnits; i++) {
			const Unit& m = b.u[i];
			if (i == ai || !m.alive || m.team != a.team) continue;
			if (Dist(m.x, m.z, a.x, a.z) != 1) continue;
			int s = UnitStat(m, S_STR) + UnitStat(m, S_MAG) / 2;
			if (s > bs) { bs = s; best = i; }
		}
		if (best >= 0) { f.follow = best; f.fdmg = max(1, bs / 2 + 2); }
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

bool SkillUsable(const Battle& b, int ui, int skill)
{
	const Unit& u = b.u[ui];
	const SkillDef& s = SkillInfo(skill);
	if (s.kind == SK_PASSIVE) return false;
	return UnitHasSkill(u, skill) && u.mp >= s.mp;
}

int SkillTargets(const Battle& b, int caster, int skill, int cx, int cz, int* out, int maxOut)
{
	const Unit& c = b.u[caster];
	const SkillDef& s = SkillInfo(skill);
	bool friendly = (s.kind == SK_HEAL || s.kind == SK_BUFF);
	int n = 0;
	for (int i = 0; i < b.nUnits && n < maxOut; i++) {
		const Unit& t = b.u[i];
		if (s.effect == EF_RAISE) {
			if (t.alive || !t.killed || t.team != c.team) continue;
			if (Dist(t.x, t.z, cx, cz) > max(0, s.area)) continue;
			out[n++] = i;
			continue;
		}
		if (!t.alive) continue;
		bool ally = (t.team == c.team);
		if (friendly != ally) continue;
		if (Dist(t.x, t.z, cx, cz) > s.area) continue;
		if (s.rMax == 0 && i == caster) continue;
		out[n++] = i;
	}
	return n;
}

int ExpForAction(const Unit& a, const Unit& d, int killed, int stage)
{
	int dl = d.ch.lv - a.ch.lv;
	int e = 10 + dl * 3;
	if (killed) e += 30 + dl * 5 + (d.boss ? 40 : 0);
	e = Clampi(e, 1, 100);
	float f = 1.f + (stage - 1) * .025f;
	return Clampi((int)roundf(e * f), 1, 100);
}

static void ApplyEffect(Battle& b, Unit& a, Unit& d, int effect)
{
	switch (effect) {
	case EF_SLOW: d.ct = max(0, d.ct - 400); break;
	case EF_DEFDOWN: d.buffDef = 0; d.defDown = 3; break;
	case EF_POISON: d.poison = 3; break;
	case EF_BUFF_DEF: d.buffDef = 3; break;
	case EF_BUFF_AGI: d.buffAgi = 3; break;
	case EF_BUFF_ATK: d.buffAtk = 3; break;
	case EF_REACT: if (d.alive && d.team == a.team && d.ct < 1000) d.ct = 1300; break;
	default: break;
	}
	(void)b; (void)a;
}

int ResolveAttack(Battle& b, int ai, int di, int skill, Rng& r, Strike* out, int maxOut)
{
	int n = 0;
	Unit& a = b.u[ai];
	const SkillDef* sd = skill >= 0 ? &SkillInfo(skill) : NULL;
	if (sd) a.mp = max(0, a.mp - sd->mp);
	// 範囲・回復・補助
	if (sd && (sd->area > 0 || sd->kind == SK_HEAL || sd->kind == SK_BUFF)) {
		int targets[MAX_UNITS];
		int cx = b.u[di].x, cz = b.u[di].z;
		if (sd->rMax == 0) { cx = a.x; cz = a.z; }
		int nt = SkillTargets(b, ai, skill, cx, cz, targets, MAX_UNITS);
		for (int k = 0; k < nt && n < maxOut; k++) {
			Unit& d = b.u[targets[k]];
			Strike s = {};
			s.attacker = ai; s.defender = targets[k]; s.skill = skill;
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
					int amt = UnitStat(a, S_MAG) + sd->power;
					int before = d.hp;
					d.hp = min(d.ch.stat[S_HP], d.hp + amt);
					s.dmg = -(d.hp - before);
					s.hit = 1;
				}
			} else if (sd->kind == SK_BUFF) {
				ApplyEffect(b, a, d, sd->effect);
				s.hit = 1;
			} else {
				int dmg, hit, crit;
				StrikeCalc(b, a, d, skill, dmg, hit, crit);
				s.hit = (int)(r.Next() % 100) < hit;
				if (s.hit) {
					s.crit = (int)(r.Next() % 100) < crit;
					s.dmg = s.crit ? dmg * 2 : dmg;
					d.hp -= s.dmg;
					if (sd->effect != EF_NONE) ApplyEffect(b, a, d, sd->effect);
					if (d.hp <= 0) { d.hp = 0; s.kill = 1; }
					if (a.team != d.team) d.ct = max(0, d.ct - 500);
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
	int turn = 0;
	while ((aLeft > 0 || dLeft > 0) && n < maxOut) {
		bool atkTurn = (turn % 2 == 0) ? aLeft > 0 : dLeft <= 0;
		turn++;
		Unit& x = atkTurn ? a : d;
		Unit& y = atkTurn ? d : a;
		if (x.hp <= 0 || y.hp <= 0) break;
		int sk = atkTurn ? skill : -1;
		int dmg, hit, crit;
		StrikeCalc(b, x, y, sk, dmg, hit, crit);
		Strike s = {};
		s.attacker = atkTurn ? ai : di;
		s.defender = atkTurn ? di : ai;
		s.skill = sk;
		s.counter = atkTurn ? 0 : 1;
		s.hit = (int)(r.Next() % 100) < hit;
		if (s.hit) {
			s.crit = (int)(r.Next() % 100) < crit;
			s.dmg = s.crit ? dmg * 2 : dmg;
			y.hp -= s.dmg;
			if (sk >= 0 && SkillInfo(sk).effect == EF_DRAIN) {
				int h = s.dmg / 2;
				x.hp = min(x.ch.stat[S_HP], x.hp + h);
				s.drain = h;
			}
			if (sk >= 0 && SkillInfo(sk).effect != EF_NONE && SkillInfo(sk).effect != EF_DRAIN && SkillInfo(sk).effect != EF_IGNDEF)
				ApplyEffect(b, x, y, SkillInfo(sk).effect);
			if (y.hp <= 0) { y.hp = 0; s.kill = 1; }
			if (x.team != y.team) y.ct = max(0, y.ct - (500 + (s.crit ? 250 : 0)));
		}
		out[n++] = s;
		if (atkTurn) aLeft--; else dLeft--;
		if (s.kill) break;
	}
	if (n < maxOut && d.hp > 0 && a.team != d.team) {
		int best = -1, bs = -1;
		for (int i = 0; i < b.nUnits; i++) {
			Unit& m = b.u[i];
			if (i == ai || !m.alive || m.team != a.team) continue;
			if (Dist(m.x, m.z, a.x, a.z) != 1) continue;
			int sc = UnitStat(m, S_STR) + UnitStat(m, S_MAG) / 2;
			if (sc > bs) { bs = sc; best = i; }
		}
		if (best >= 0) {
			int fd = max(1, bs / 2 + 2);
			Strike s = {};
			s.attacker = best; s.defender = di; s.skill = -1; s.hit = 1; s.dmg = fd;
			d.hp -= fd;
			if (d.hp <= 0) { d.hp = 0; s.kill = 1; }
			d.ct = max(0, d.ct - 200);
			out[n++] = s;
		}
	}
	return n;
}

// ============================================================ 戦闘全体
void Battle::RebuildOcc()
{
	memset(occ, 0, sizeof(occ));
	for (int i = 0; i < nUnits; i++)
		if (u[i].alive && Map::In(u[i].x, u[i].z)) occ[u[i].z][u[i].x] = i + 1;
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
			if (!u[i].alive || !u[i].alert || u[i].ct < 1000) continue;
			if (best < 0 || u[i].ct > u[best].ct || (u[i].ct == u[best].ct && u[i].team < u[best].team)) best = i;
		}
		if (best >= 0) return best;
		for (int i = 0; i < nUnits; i++) if (u[i].alive && u[i].alert) u[i].ct += Speed(u[i]);
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
			if (!u[i].alive || !u[i].alert || ct[i] < 1000) continue;
			if (best < 0 || ct[i] > ct[best] || (ct[i] == ct[best] && u[i].team < u[best].team)) best = i;
		}
		if (best >= 0) {
			if (insertAt && *insertAt < 0 && best == acting) *insertAt = got;
			out[got++] = best;
			ct[best] -= 1000;
			continue;
		}
		bool any = false;
		for (int i = 0; i < nUnits; i++) if (u[i].alive && u[i].alert) { ct[i] += Speed(u[i]); any = true; }
		if (!any) break;
	}
	return got;
}

static int PickMonster(int stage, Rng& r)
{
	if (kImportN >= 80) return ImportPick(stage, r, r.Range(0, 11));
	return M_SLIME;
}

static int BossMonster(int stage, Rng& r)
{
	if (stage >= 200 && (stage % 40) == 0) return M_DRAGON;
	if (kImportN >= 80) return ImportPick(stage, r, 8);
	return PickMonster(stage, r);
}

void SetupBattle(Battle& b, int stage, DWORD seed, const CharData* party, const int* rosterIdx, int nParty)
{
	memset(&b, 0, sizeof(b));
	stage = Clampi(stage, 1, TAC_STAGE_MAX);
	b.stage = stage;
	GenerateMap(b.map, stage, seed);
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
		GrantReached(u.ch);
		u.hp = u.ch.stat[S_HP];
		u.mp = u.ch.stat[S_MP];
		u.x = b.map.pStartX[i]; u.z = b.map.pStartZ[i];
		u.face = faceToE;
		u.ct = r.Range(0, 300) + u.ch.stat[S_AGI] * 8;
		u.hpShow = (float)u.hp;
		u.grp = -1;
		u.alert = 1;
		n++;
	}
	int baseLv = stage <= 100 ? 1 + (stage - 1) * 9 / 10 : Clampi(90 + (stage - 100) * 6 / 5, 1, TAC_LV_MAX);
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
			if (stage > 100) {
				int mul = 100 + (stage - 100) / 4;
				if (mul > 450) mul = 450;
				u.ch.stat[S_HP] = max(1, u.ch.stat[S_HP] * mul / 100);
				u.ch.stat[S_STR] += (stage - 100) / 25;
				u.ch.stat[S_MAG] += (stage - 100) / 25;
				u.ch.stat[S_DEF] += (stage - 100) / 40;
				u.ch.stat[S_RES] += (stage - 100) / 40;
			}
			u.x = grp.x[k]; u.z = grp.z[k];
			u.face = faceToE + 3.14159f;
			u.ct = r.Range(0, 280) + u.ch.stat[S_AGI] * 6;
			u.hpShow = (float)u.hp;
			u.grp = g;
			u.alert = 0;
			n++;
		}
	}
	b.nUnits = n;
	b.RebuildOcc();
	WakeNearPlayers(b, 8);
	int nearGrp = -1, nearD = 9999;
	for (int e = 0; e < b.nUnits; e++) {
		if (!b.u[e].alive || b.u[e].team != TEAM_ENEMY || b.u[e].grp < 0) continue;
		for (int p = 0; p < b.nUnits; p++) {
			if (!b.u[p].alive || b.u[p].team != TEAM_PLAYER) continue;
			int d = Dist(b.u[e].x, b.u[e].z, b.u[p].x, b.u[p].z);
			if (d < nearD) { nearD = d; nearGrp = b.u[e].grp; }
		}
	}
	if (nearGrp >= 0) WakeGroup(b, nearGrp);
}

void FinishAction(Battle& b, int ui, int moved, int acted)
{
	Unit& u = b.u[ui];
	int cost = (moved && acted) ? 1000 : (moved || acted) ? 800 : 600;
	u.ct = max(0, u.ct - cost);
	b.turnCount++;
}

int TurnStartEffects(Battle& b, int ui, int* healOut, int* poisonOut)
{
	Unit& u = b.u[ui];
	*healOut = 0; *poisonOut = 0;
	if (UnitHasSkill(u, SKL_P_REGEN) && u.hp < u.ch.stat[S_HP]) {
		int h = max(1, u.ch.stat[S_HP] / 10);
		int before = u.hp;
		u.hp = min(u.ch.stat[S_HP], u.hp + h);
		*healOut = u.hp - before;
	}
	if (u.poison > 0) {
		int d = max(1, u.ch.stat[S_HP] * 8 / 100);
		u.hp = max(1, u.hp - d);
		*poisonOut = d;
		u.poison--;
	}
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

void WakeNearPlayers(Battle& b, int rad)
{
	BYTE wake[MAX_GROUPS];
	memset(wake, 0, sizeof(wake));
	for (int p = 0; p < b.nUnits; p++) {
		const Unit& a = b.u[p];
		if (!a.alive || a.team != TEAM_PLAYER) continue;
		for (int e = 0; e < b.nUnits; e++) {
			const Unit& d = b.u[e];
			if (!d.alive || d.team != TEAM_ENEMY || d.grp < 0 || d.alert) continue;
			if (Dist(a.x, a.z, d.x, d.z) <= rad) wake[d.grp] = 1;
		}
	}
	for (int g = 0; g < MAX_GROUPS; g++) if (wake[g]) WakeGroup(b, g);
}

int ApplyTile(Battle& b, int ui)
{
	Unit& u = b.u[ui];
	if (!Map::In(u.x, u.z)) return 0;
	const Cell& c = b.map.At(u.x, u.z);
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
		b.map.At(u.x, u.z).gimm = GM_NONE;
		b.map.At(u.x, u.z).prop = PK_NONE;
		return GM_CHEST;
	}
	if (c.gimm == GM_WARP) {
		int ox = -1, oz = -1;
		for (int z = 0; z < MAPN && ox < 0; z++) for (int x = 0; x < MAPN; x++) {
			const Cell& o = b.map.At(x, z);
			if (o.gimm == GM_WARP && o.gparam == c.gparam && (x != u.x || z != u.z)) { ox = x; oz = z; break; }
		}
		if (ox < 0) return 0;
		if (b.UnitAt(ox, oz) >= 0) return 0;
		u.x = ox; u.z = oz;
		b.RebuildOcc();
		return GM_WARP;
	}
	return 0;
}

bool CanOpenDoor(const Unit& u)
{
	int c = u.ch.cls;
	return c == C_THIEF || c == C_ASSASSIN || c == C_PHANTOM;
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
}

bool CanShove(const Battle& b, int ui, int ti, int* outX, int* outZ)
{
	if (ui < 0 || ti < 0 || ui == ti) return false;
	const Unit& a = b.u[ui];
	const Unit& d = b.u[ti];
	if (!a.alive || !d.alive) return false;
	if (Dist(a.x, a.z, d.x, d.z) != 1) return false;
	if (d.team != a.team) {
		if (d.boss) return false;
		if (UnitStat(a, S_STR) + 1 < UnitStat(d, S_STR)) return false;
	}
	int dx = d.x - a.x, dz = d.z - a.z;
	int nx = d.x + dx, nz = d.z + dz;
	if (!Map::In(nx, nz)) return false;
	if (b.UnitAt(nx, nz) >= 0) return false;
	int t = b.map.At(nx, nz).t;
	bool pit = (t == T_CHASM || t == T_LAVA);
	if (!pit && TerrainInfo(t).costFoot >= 99) return false;
	if (outX) *outX = nx;
	if (outZ) *outZ = nz;
	return true;
}

void DoShove(Battle& b, int ui, int ti)
{
	int nx, nz;
	if (!CanShove(b, ui, ti, &nx, &nz)) return;
	Unit& d = b.u[ti];
	int t = b.map.At(nx, nz).t;
	d.x = nx; d.z = nz;
	d.face = atan2f((float)(d.x - b.u[ui].x), (float)(d.z - b.u[ui].z));
	if (t == T_CHASM || t == T_LAVA) {
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
	const int mt = UnitMoveType(u), jp = UnitJump(u);
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) df[z][x] = 30000;
	static int qx[MAPN * MAPN * 4], qz[MAPN * MAPN * 4];
	int qh = 0, qt = 0;
	for (int i = 0; i < b.nUnits; i++) {
		const Unit& t = b.u[i];
		if (!t.alive || t.team == u.team) continue;
		df[t.z][t.x] = 0;
		qx[qt] = t.x; qz[qt] = t.z; qt++;
	}
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	// 0-1 ではないので緩和 BFS（反復）
	while (qh < qt) {
		int x = qx[qh], z = qz[qh]; qh++;
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			if (!Map::In(nx, nz)) continue;
			if (!CanStep(b.map, nx, nz, x, z, mt, jp)) continue;
			int c = df[z][x] + EnterCost(b.map, x, z, mt);
			if (c < df[nz][nx]) {
				df[nz][nx] = (short)c;
				if (qt < (int)_countof(qx)) { qx[qt] = nx; qz[qt] = nz; qt++; }
			}
		}
	}
}

void AiDecide(const Battle& b, int ui, AiPlan& p, Rng& r)
{
	const Unit& u = b.u[ui];
	p.moveX = u.x; p.moveZ = u.z; p.target = -1; p.skill = -1; p.castX = u.x; p.castZ = u.z;
	static Reach rc;
	ComputeReach(b, ui, rc);
	float best = -1e9f;
	Battle& bb = const_cast<Battle&>(b);
	Unit& um = bb.u[ui];
	const int ox = u.x, oz = u.z;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (rc.cost[z][x] < 0 || !rc.canStop[z][x]) continue;
		um.x = x; um.z = z;
		float place = TerrainInfo(b.map.At(x, z).t).defPct * .25f - rc.cost[z][x] * .05f;
		// 通常攻撃
		for (int i = 0; i < b.nUnits; i++) {
			const Unit& t = b.u[i];
			if (!t.alive || t.team == u.team) continue;
			if (!InAttackRange(b, x, z, UnitRangeMin(u), UnitRangeMax(u), t.x, t.z, true)) continue;
			Forecast f;
			CalcForecast(b, ui, i, -1, f);
			float exp = f.dmg * f.hits * f.hit / 100.f;
			float sc = exp + (f.dmg * f.hits >= t.hp ? 40.f : 0) - (f.canCounter ? f.cdmg * f.chits * f.chit / 100.f * .5f : 0) + place;
			sc += (1.f - (float)t.hp / max(1, t.ch.stat[S_HP])) * 8.f;
			if (sc > best) { best = sc; p.moveX = x; p.moveZ = z; p.target = i; p.skill = -1; p.castX = t.x; p.castZ = t.z; }
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
				else if (!InAttackRange(b, x, z, rmin, rmax, tx, tz, sd.kind != SK_HEAL && sd.kind != SK_BUFF)) continue;
				float sc = place;
				if (sd.kind == SK_HEAL) {
					int targets[MAX_UNITS];
					int nt = SkillTargets(b, ui, s, tx, tz, targets, MAX_UNITS);
					float h = 0;
					for (int k = 0; k < nt; k++) {
						const Unit& q = b.u[targets[k]];
						int miss = q.ch.stat[S_HP] - q.hp;
						if (q.hp * 100 / max(1, q.ch.stat[S_HP]) < 65) h += min(miss, UnitStat(u, S_MAG) + sd.power);
					}
					if (h <= 0) continue;
					sc += h * 1.2f;
				} else if (sd.kind == SK_BUFF) {
					continue;
				} else {
					int targets[MAX_UNITS];
					int nt = SkillTargets(b, ui, s, tx, tz, targets, MAX_UNITS);
					if (sd.area == 0) { targets[0] = i; nt = 1; }
					float tot = 0;
					for (int k = 0; k < nt; k++) {
						Forecast f;
						CalcForecast(b, ui, targets[k], s, f);
						const Unit& q = b.u[targets[k]];
						tot += f.dmg * f.hits * f.hit / 100.f + (f.dmg * f.hits >= q.hp ? 40.f : 0);
					}
					if (tot <= 0) continue;
					sc += tot - sd.mp * .4f;
				}
				sc += r.F(0, 1.5f);
				if (sc > best) { best = sc; p.moveX = x; p.moveZ = z; p.target = i; p.skill = s; p.castX = tx; p.castZ = tz; }
			}
		}
	}
	um.x = ox; um.z = oz;
	if (p.target >= 0) return;
	// ボスは近づかれるまで待機
	if (u.boss) {
		int nearest = 999;
		for (int i = 0; i < b.nUnits; i++) if (b.u[i].alive && b.u[i].team != u.team) nearest = min(nearest, Dist(u.x, u.z, b.u[i].x, b.u[i].z));
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
int ClassLine(int cls)
{
	switch (cls) {
	case C_SOLDIER: case C_PALADIN: case C_HERO: case C_SKYKNIGHT: case C_DRAGONLORD: case C_RUNEKNIGHT: return 0;
	case C_FIGHTER: case C_BERSERKER: return 1;
	case C_ARCHER: case C_SNIPER: case C_PHANTOM: return 2;
	case C_THIEF: case C_ASSASSIN: return 3;
	case C_MAGE: case C_WIZARD: case C_ARCHMAGE: case C_ALCHEMIST: case C_ARCHALCHEMIST: return 4;
	default: return 5;
	}
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
	if (s.stageCleared < kGear[index].stage) return false;
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
void GearEquip(CharData& c, SaveData& s, int stashIndex)
{
	if (stashIndex < 0 || stashIndex >= s.gearN) return;
	int id = s.gearId[stashIndex];
	const GearDef* g = GearOf(id);
	if (!g) return;
	int slot = g->kind ? 7 : 6;
	int old = c.reserved[slot];
	c.reserved[slot] = id;
	StashRemoveAt(s, stashIndex);
	if (old > 0) StashAdd(s, old);
}
int GearTryDrop(SaveData& s, int stage, int monster, Rng& r)
{
	int pool[512], n = 0;
	int gate = stage <= 1 ? 0 : stage;
	int line = MonsterDropLine(monster);
	for (int i = 0; i < kGearN && n < 512; i++) if (kGear[i].line == line && kGear[i].stage <= gate) pool[n++] = i;
	if (n < 1) {
		for (int i = 0; i < kGearN && n < 512; i++) if (kGear[i].stage <= gate) pool[n++] = i;
	}
	if (n < 1) return -1;
	int gi = pool[r.Range(0, n - 1)];
	if (!StashAdd(s, gi + 1)) {
		s.gold += max(1, kGear[gi].price / 3);
		return -1;
	}
	return gi + 1;
}
bool DropConsumable(SaveData& s, int monster, Rng& r)
{
	int line = MonsterDropLine(monster);
	switch (line) {
	case 0: s.itemN[IT_MIGHT]++; break;
	case 1: ItemExtraAdd(s, IX_ATK, 1); break;
	case 2: s.itemN[IT_DELAY]++; break;
	case 3: s.itemN[IT_RAISE]++; break;
	case 4: ItemExtraAdd(s, IX_ETHER, 1); break;
	default: s.itemN[IT_ELIXIR]++; break;
	}
	if (r.Chance(45)) {
		int k = r.Range(0, IX_N - 1);
		ItemExtraAdd(s, k, 1);
	}
	return true;
}
static const GoodDef kGood[] = {
	{ 40, IT_ELIXIR, -1, TX_IT_ELIXIR, 1 },
	{ 70, IT_MIGHT, -1, TX_IT_MIGHT, 1 },
	{ 55, IT_DELAY, -1, TX_IT_DELAY, 0 },
	{ 160, IT_RAISE, -1, TX_IT_RAISE, 0 },
	{ 90, -1, IX_FULL, TX_IT_FULL, 1 },
	{ 80, -1, IX_ATK, TX_IT_ATK, 1 },
	{ 80, -1, IX_DEF, TX_IT_DEF, 1 },
	{ 80, -1, IX_AGI, TX_IT_AGI, 1 },
	{ 60, -1, IX_SLOW, TX_IT_SLOW, 0 },
	{ 60, -1, IX_VENOM, TX_IT_VENOM, 0 },
	{ 50, -1, IX_CURE, TX_IT_CURE, 1 },
	{ 70, -1, IX_ETHER, TX_IT_ETHER, 1 },
};
int GoodCount() { return (int)(sizeof(kGood) / sizeof(kGood[0])); }
const GoodDef* GoodAt(int i) { return (i >= 0 && i < GoodCount()) ? &kGood[i] : NULL; }
int GoodHave(const SaveData& s, int i)
{
	const GoodDef* g = GoodAt(i);
	if (!g) return 0;
	if (g->item >= 0) return s.itemN[g->item];
	return ItemExtraGet(s, g->extra);
}
bool GoodBuy(SaveData& s, int i)
{
	const GoodDef* g = GoodAt(i);
	if (!g || s.gold < g->price || GoodHave(s, i) >= 99) return false;
	s.gold -= g->price;
	if (g->item >= 0) s.itemN[g->item]++;
	else ItemExtraAdd(s, g->extra, 1);
	return true;
}
bool GoodTake(SaveData& s, int i)
{
	if (GoodHave(s, i) < 1) return false;
	const GoodDef* g = GoodAt(i);
	if (g->item >= 0) s.itemN[g->item]--;
	else ItemExtraAdd(s, g->extra, -1);
	return true;
}

// ============================================================ セーブ
static const DWORD kSaveMagic = 0x31434154; // 'TAC1'
static LPCTSTR kSaveLeaf = _T("oggYSEDbgmu_tactics.dat");

// 敵部位。セーブ本体の大きさは変えず、別ファイルに積む。
static const int kTrophyN = 40;
static unsigned short g_trophy[40];
static const DWORD kTrophyMagic = 0x54525054u;
static LPCTSTR kTrophyLeaf = _T("oggYSEDbgmu_tac_part.dat");

static void TrophyLoad()
{
	memset(g_trophy, 0, sizeof(g_trophy));
	CString path = DatArc_Path(kTrophyLeaf);
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("rb")) != 0 || !fp) return;
	DWORD magic = 0; int ver = 0;
	if (fread(&magic, 4, 1, fp) == 1 && fread(&ver, 4, 1, fp) == 1 && magic == kTrophyMagic && ver == 1)
		fread(g_trophy, sizeof(g_trophy), 1, fp);
	fclose(fp);
}
static void TrophySave()
{
	CString path = DatArc_Path(kTrophyLeaf);
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("wb")) != 0 || !fp) return;
	int ver = 1;
	fwrite(&kTrophyMagic, 4, 1, fp);
	fwrite(&ver, 4, 1, fp);
	fwrite(g_trophy, sizeof(g_trophy), 1, fp);
	fclose(fp);
	DatArc_Commit(kTrophyLeaf);
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
	s.version = 1;
	for (int i = 0; i < MAX_PARTY; i++) s.party[i] = -1;
	s.stageSel = 1;
	s.camSpeed = 1;
	s.quality = 1;
	s.showGrid = 1;
	s.uidNext = 1;
	s.bgmVol = 6;
	s.seVol = 8;
	s.optInit = 1;
}

bool SaveLoad(SaveData& s)
{
	SaveDefaults(s);
	CString path = DatArc_Path(kSaveLeaf);
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("rb")) != 0 || !fp) return false;
	SaveData t;
	size_t rd = fread(&t, 1, sizeof(t), fp);
	fclose(fp);
	if (rd != sizeof(t) || t.magic != kSaveMagic || t.version != 1) return false;
	s = t;
	for (int i = 0; i < MAX_ROSTER; i++) {
		CharData& c = s.roster[i];
		c.name[NAME_LEN - 1] = 0;
		if (!c.used) continue;
		c.cls = Clampi(c.cls, 0, C_PLAYER_COUNT - 1);
		c.lv = Clampi(c.lv, 1, 99);
		c.body = Clampi(c.body, 0, B_COUNT - 1);
		c.hair = Clampi(c.hair, 0, HAIR_STYLES - 1);
		c.hairColor = Clampi(c.hairColor, 0, HAIR_COLORS - 1);
		c.outfit = Clampi(c.outfit, 0, OUTFIT_COLORS - 1);
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
	s.gearN = Clampi(s.gearN, 0, GEAR_STASH);
	for (int i = 0; i < s.gearN; i++) if (s.gearId[i] < 1 || s.gearId[i] > kGearN) s.gearId[i] = 1;
	TrophyLoad();
	return true;
}

bool SaveWrite(const SaveData& s)
{
	CString path = DatArc_Path(kSaveLeaf);
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("wb")) != 0 || !fp) return false;
	bool ok = fwrite(&s, 1, sizeof(s), fp) == sizeof(s);
	fclose(fp);
	if (ok) { TrophySave(); DatArc_Commit(kSaveLeaf); }
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

} // namespace otac
