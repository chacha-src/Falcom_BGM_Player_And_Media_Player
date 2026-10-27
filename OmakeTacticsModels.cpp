#include "stdafx.h"
#include "OmakeTacticsModels.h"
#include <math.h>

namespace otac {

static const float PI = 3.14159265f;

static const char* const kClipNames[CL_COUNT] = {
	"Idle", "Walking_A", "Running_A", "1H_Melee_Attack_Chop", "1H_Melee_Attack_Slice_Diagonal", "1H_Melee_Attack_Stab",
	"2H_Melee_Attack_Chop", "2H_Melee_Attack_Spin", "1H_Ranged_Shoot", "2H_Ranged_Shoot",
	"Spellcast_Shoot", "Spellcast_Raise", "Spellcast_Long", "Hit_A", "Hit_B", "Death_A", "Cheer", "Block", "Dodge_Backward", "2H_Melee_Idle",
	"Throw", "Unarmed_Melee_Attack_Punch_A", "Jump_Full_Short", "Interact", "Taunt", "Spawn_Ground", "Use_Item",
	"Idle_GlanceL", "Idle_GlanceR", "Idle_Look", "Idle_Stretch", "Idle_Shift", "Idle_Cross"
};

static int Pal(int row, int col) { return row * 16 + col; }

// ============================================================ ジオメトリ組立
struct Geo {
	Buf<V3> p, n;
	Buf<int> pal, b0, b1;
	Buf<float> w1;
	Buf<DWORD> col;
	Buf<WORD> idx;
	int curBone, curBone2;
	float curW2;
	int curPal;
	DWORD curCol;
	Geo() : curBone(0), curBone2(-1), curW2(0), curPal(Pal(0, 15)), curCol(0xFFFFFFFF) {}

	int V(V3 pos, V3 nrm)
	{
		p.push_back(pos); n.push_back(Norm(nrm));
		pal.push_back(curPal); b0.push_back(curBone); b1.push_back(curBone2); w1.push_back(curW2);
		col.push_back(curCol);
		return (int)p.size() - 1;
	}
	void T(int a, int b, int c)
	{
		V3 fn = Cross(p[b] - p[a], p[c] - p[a]);
		V3 avg = n[a] + n[b] + n[c];
		if (Dot(fn, avg) >= 0) { idx.push_back((WORD)a); idx.push_back((WORD)b); idx.push_back((WORD)c); }
		else { idx.push_back((WORD)a); idx.push_back((WORD)c); idx.push_back((WORD)b); }
	}
	// 単位球を r でスケールし xf で配置。keep(d) で部分球
	template <class F>
	void Ellipsoid(const M4& xf, V3 r, int seg, int rings, F keep)
	{
		int base = (int)p.size();
		Buf<int> ok((seg + 1) * (rings + 1));
		for (int i = 0; i <= rings; i++) {
			float th = PI * i / rings;
			for (int j = 0; j <= seg; j++) {
				float ph = 2 * PI * j / seg;
				V3 d = v3(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
				V3 lp = v3(d.x * r.x, d.y * r.y, d.z * r.z);
				V3 ln = v3(d.x / max(1e-4f, r.x), d.y / max(1e-4f, r.y), d.z / max(1e-4f, r.z));
				V(MXformPt(xf, lp), MXformDir(xf, ln));
				ok[i * (seg + 1) + j] = keep(d) ? 1 : 0;
			}
		}
		for (int i = 0; i < rings; i++) for (int j = 0; j < seg; j++) {
			int a = i * (seg + 1) + j, b = a + 1, c = a + seg + 1, d = c + 1;
			if (!(ok[a] && ok[b] && ok[c] && ok[d])) continue;
			if (i > 0) T(base + a, base + c, base + b);
			if (i < rings - 1) T(base + b, base + c, base + d);
		}
	}
	void Ellipsoid(const M4& xf, V3 r, int seg = 12, int rings = 8)
	{
		Ellipsoid(xf, r, seg, rings, [](V3) { return true; });
	}
	void Sphere(V3 c, V3 r, int seg = 12, int rings = 8) { Ellipsoid(MTrans(c.x, c.y, c.z), r, seg, rings); }
	// 背中に沿う髪の束。center は長さ方向、halfW は左右の半幅、thick は厚み。+Z が外側。
	void Lock(const V3* center, int rows, const float* halfW, float thick)
	{
		const int cols = 5;
		if (!center || !halfW || rows < 2) return;
		int base = (int)p.size();
			for (int i = 0; i < rows; i++) {
				for (int layer = 0; layer < 2; layer++) {
					// 内側は中心線（頭と服の外）、外側だけ +Z へ厚みを足す。中にめり込まない。
					float s = layer ? thick : 0.f;
					V3 nrm = layer ? v3(0, 0, 1) : v3(0, 0, -1);
				for (int j = 0; j < cols; j++) {
					float u = (j / (float)(cols - 1)) * 2.f - 1.f;
					V(center[i] + v3(u * halfW[i], 0, s), nrm);
				}
			}
		}
		for (int i = 0; i < rows - 1; i++) for (int layer = 0; layer < 2; layer++) for (int j = 0; j < cols - 1; j++) {
			int a = base + (i * 2 + layer) * cols + j;
			int b = a + 1;
			int c = base + ((i + 1) * 2 + layer) * cols + j;
			int d = c + 1;
			if (layer) { T(a, c, b); T(b, c, d); }
			else { T(a, b, c); T(b, d, c); }
		}
	}
	// a→b に沿った楕円体
	void Limb(V3 a, V3 b, float ra, float rb = -1)
	{
		if (rb < 0) rb = ra;
		V3 d = b - a;
		float L = Len(d);
		if (L < 1e-4f) return;
		V3 y = d * (1.f / L);
		V3 x = Norm(Cross(fabsf(y.y) > .9f ? v3(1, 0, 0) : v3(0, 1, 0), y));
		V3 z = Cross(x, y);
		V3 c = (a + b) * .5f;
		float rr = (ra + rb) * .5f;
		M4 m = MIdent();
		m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
		m.m[4] = y.x; m.m[5] = y.y; m.m[6] = y.z;
		m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
		m.m[12] = c.x; m.m[13] = c.y; m.m[14] = c.z;
		Ellipsoid(m, v3(rr, L * .5f + rr * .3f, rr), 10, 6);
	}
	// 底面中心 a から先端 b への円錐
	void Cone(V3 a, V3 b, float r, int seg = 8)
	{
		V3 d = b - a;
		float L = Len(d);
		if (L < 1e-4f) return;
		V3 y = d * (1.f / L);
		V3 x = Norm(Cross(fabsf(y.y) > .9f ? v3(1, 0, 0) : v3(0, 1, 0), y));
		V3 z = Cross(x, y);
		int base = (int)p.size();
		for (int j = 0; j < seg; j++) {
			float ph = 2 * PI * j / seg;
			V3 rd = x * cosf(ph) + z * sinf(ph);
			V(a + rd * r, rd * L + y * r);
		}
		for (int j = 0; j < seg; j++) {
			float ph = 2 * PI * (j + .5f) / seg;
			V3 rd = x * cosf(ph) + z * sinf(ph);
			V(b, rd * L + y * r);
		}
		int ca = V(a, y * -1.f);
		for (int j = 0; j < seg; j++) {
			int j1 = (j + 1) % seg;
			T(base + j, base + j1, base + seg + j);
			int q0 = V(p[base + j], y * -1.f), q1 = V(p[base + j1], y * -1.f);
			T(ca, q1, q0);
		}
	}
	void Box(V3 c, V3 h, const M4* rot = NULL)
	{
		static const int F[6][4] = { { 1, 3, 7, 5 }, { 0, 4, 6, 2 }, { 2, 6, 7, 3 }, { 0, 1, 5, 4 }, { 4, 5, 7, 6 }, { 0, 2, 3, 1 } };
		static const float N[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		V3 cn[8];
		for (int i = 0; i < 8; i++) {
			V3 o = v3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
			if (rot) o = MXformDir(*rot, o);
			cn[i] = c + o;
		}
		for (int f = 0; f < 6; f++) {
			V3 nn = v3(N[f][0], N[f][1], N[f][2]);
			if (rot) nn = MXformDir(*rot, nn);
			int a = V(cn[F[f][0]], nn), b = V(cn[F[f][1]], nn), cc = V(cn[F[f][2]], nn), d = V(cn[F[f][3]], nn);
			T(a, b, cc); T(a, cc, d);
		}
	}
	// 両面の三角形
	void Tri2(V3 a, V3 b, V3 c)
	{
		V3 fn = Norm(Cross(b - a, c - a));
		V3 off = fn * .006f;
		int i0 = V(a + off, fn), i1 = V(b + off, fn), i2 = V(c + off, fn);
		T(i0, i1, i2);
		V3 bn = fn * -1.f;
		int j0 = V(a - off, bn), j1 = V(b - off, bn), j2 = V(c - off, bn);
		T(j0, j1, j2);
	}
	void Transform(const M4& m, int from = 0)
	{
		for (size_t i = from; i < p.size(); i++) { p[i] = MXformPt(m, p[i]); n[i] = Norm(MXformDir(m, n[i])); }
	}
	void MirrorX()
	{
		for (size_t i = 0; i < p.size(); i++) { p[i].x = -p[i].x; n[i].x = -n[i].x; }
		for (size_t i = 0; i + 2 < idx.size(); i += 3) std::swap(idx[i + 1], idx[i + 2]);
	}
	void ToSkinned(Buf<VtxK>& out) const
	{
		out.resize(p.size());
		for (size_t i = 0; i < p.size(); i++) {
			VtxK& v = out[i];
			v.p[0] = p[i].x; v.p[1] = p[i].y; v.p[2] = p[i].z;
			v.n[0] = n[i].x; v.n[1] = n[i].y; v.n[2] = n[i].z;
			Renderer::PaletteUV(pal[i], v.uv[0], v.uv[1]);
			memset(v.j, 0, 4); memset(v.w, 0, 4);
			v.j[0] = (BYTE)b0[i];
			if (b1[i] >= 0 && w1[i] > 0) {
				int w = Clampi((int)(w1[i] * 255.f + .5f), 0, 255);
				v.j[1] = (BYTE)b1[i]; v.w[1] = (BYTE)w; v.w[0] = (BYTE)(255 - w);
			} else v.w[0] = 255;
		}
	}
	void ToStatic(Buf<VtxS>& out) const
	{
		out.resize(p.size());
		for (size_t i = 0; i < p.size(); i++) {
			VtxS& v = out[i];
			v.p[0] = p[i].x; v.p[1] = p[i].y; v.p[2] = p[i].z;
			v.n[0] = n[i].x; v.n[1] = n[i].y; v.n[2] = n[i].z;
			v.uv[0] = v.uv[1] = .5f;
			v.c = col[i];
			if ((v.c >> 24) == 0) v.c |= 0xFF000000;
		}
	}
};

static BOOL GeoToStatic(Renderer& r, const Geo& g, GpuMesh& out)
{
	if (g.p.empty() || g.idx.empty() || g.p.size() > 65535) return FALSE;
	Buf<VtxS> v;
	g.ToStatic(v);
	if (!r.MakeMesh(out, v.data(), sizeof(VtxS), (UINT)v.size(), g.idx.data(), (UINT)g.idx.size())) return FALSE;
	out.tex = r.TexWhite();
	return TRUE;
}

// ============================================================ 手続きモンスター
struct ProcJoint { const char* name; int parent; float x, y, z; };

static int BuildProcRig(Model& m, const char* name, const ProcJoint* js, int n)
{
	memset(m.name, 0, sizeof(m.name));
	strncpy_s(m.name, name, _TRUNCATE);
	m.rig = -1;
	m.tex = -1;
	m.nJ = n;
	m.top = MIdent();
	for (int i = 0; i < n; i++) {
		m.parent[i] = js[i].parent;
		V3 wp = v3(js[i].x, js[i].y, js[i].z);
		V3 pp = js[i].parent >= 0 ? v3(js[js[i].parent].x, js[js[i].parent].y, js[js[i].parent].z) : v3(0, 0, 0);
		m.restT[i] = wp - pp;
		m.restR[i] = Q4{ 0, 0, 0, 1 };
		m.restS[i] = v3(1, 1, 1);
		m.invBind[i] = MTrans(-wp.x, -wp.y, -wp.z);
	}
	m.headJ = m.chestJ = m.handR = m.handL = m.hipsJ = -1;
	m.headMin[0] = -.3f; m.headMin[1] = 0; m.headMin[2] = -.3f;
	m.headMax[0] = .3f; m.headMax[1] = .5f; m.headMax[2] = .3f;
	return n;
}

enum { SJ_ROOT, SJ_BODY, SJ_TOP, SJ_N };
static const ProcJoint kSlimeJ[SJ_N] = { { "root", -1, 0, 0, 0 }, { "body", 0, 0, .02f, 0 }, { "top", 1, 0, .45f, 0 } };

enum { BJ_ROOT, BJ_BODY, BJ_HEAD, BJ_WL, BJ_WL2, BJ_WR, BJ_WR2, BJ_N };
static const ProcJoint kBatJ[BJ_N] = {
	{ "root", -1, 0, 0, 0 }, { "body", 0, 0, .95f, 0 }, { "head", 1, 0, 1.12f, -.08f },
	{ "wingL", 1, .1f, 1.0f, 0 }, { "wingL2", 3, .42f, 1.06f, .02f },
	{ "wingR", 1, -.1f, 1.0f, 0 }, { "wingR2", 5, -.42f, 1.06f, .02f },
};

enum { WJ_ROOT, WJ_BODY, WJ_NECK, WJ_HEAD, WJ_JAW, WJ_WL, WJ_WL2, WJ_WR, WJ_WR2, WJ_T1, WJ_T2, WJ_LL, WJ_LR, WJ_N };
static const ProcJoint kWyvJ[WJ_N] = {
	{ "root", -1, 0, 0, 0 }, { "body", 0, 0, .95f, .05f }, { "neck", 1, 0, 1.1f, -.32f }, { "head", 2, 0, 1.42f, -.6f },
	{ "jaw", 3, 0, 1.36f, -.66f },
	{ "wingL", 1, .2f, 1.12f, 0 }, { "wingL2", 5, .85f, 1.3f, .08f },
	{ "wingR", 1, -.2f, 1.12f, 0 }, { "wingR2", 7, -.85f, 1.3f, .08f },
	{ "tail1", 1, 0, .9f, .45f }, { "tail2", 9, 0, .82f, .95f },
	{ "legL", 1, .2f, .78f, .12f }, { "legR", 1, -.2f, .78f, .12f },
};

enum { GJ_ROOT, GJ_HIPS, GJ_TORSO, GJ_HEAD, GJ_AL, GJ_FL, GJ_AR, GJ_FR, GJ_LL, GJ_LR, GJ_N };
static const ProcJoint kGolJ[GJ_N] = {
	{ "root", -1, 0, 0, 0 }, { "hips", 0, 0, .55f, 0 }, { "torso", 1, 0, .85f, 0 }, { "head", 2, 0, 1.38f, -.06f },
	{ "armL", 2, .52f, 1.18f, 0 }, { "foreL", 4, .6f, .78f, -.02f },
	{ "armR", 2, -.52f, 1.18f, 0 }, { "foreR", 6, -.6f, .78f, -.02f },
	{ "legL", 1, .24f, .5f, 0 }, { "legR", 1, -.24f, .5f, 0 },
};

static int MakeSlime(Renderer& r)
{
	Model m;
	BuildProcRig(m, "proc_slime", kSlimeJ, SJ_N);
	m.headJ = SJ_TOP;
	Geo g;
	g.curPal = Pal(13, 5);
	// 体（下を潰した雫型）: body と top を高さで混合
	int base = (int)g.p.size();
	g.Ellipsoid(MTrans(0, .3f, 0), v3(.42f, .32f, .42f), 16, 10);
	for (size_t i = base; i < g.p.size(); i++) {
		V3& q = g.p[i];
		if (q.y < .1f) { q.y = .1f - (.1f - q.y) * .35f; }
		q.y += max(0.f, q.y - .4f) * .5f;
		float t = Clampf((q.y - .1f) / .5f, 0, 1);
		g.b0[i] = SJ_BODY; g.b1[i] = SJ_TOP; g.w1[i] = t * t;
	}
	g.curBone = SJ_TOP; g.curBone2 = -1;
	g.curPal = Pal(0, 1);
	g.Sphere(v3(.14f, .42f, -.33f), v3(.07f, .1f, .05f), 8, 6);
	g.Sphere(v3(-.14f, .42f, -.33f), v3(.07f, .1f, .05f), 8, 6);
	g.curPal = Pal(0, 15);
	g.Sphere(v3(.12f, .46f, -.375f), v3(.025f, .03f, .015f), 6, 4);
	g.Sphere(v3(-.16f, .46f, -.375f), v3(.025f, .03f, .015f), 6, 4);
	g.curPal = Pal(14, 5);
	g.Sphere(v3(.18f, .52f, -.2f), v3(.06f, .04f, .04f), 6, 4);
	g.curPal = Pal(13, 2);
	g.Sphere(v3(0, .2f, -.3f), v3(.09f, .03f, .04f), 8, 4);
	g.curPal = Pal(14, 8);
	g.Sphere(v3(.24f, .32f, -.08f), v3(.05f, .04f, .04f), 6, 4);
	g.Sphere(v3(-.2f, .28f, .1f), v3(.045f, .035f, .04f), 6, 4);
	g.Sphere(v3(.08f, .48f, .16f), v3(.04f, .03f, .035f), 6, 4);
	Buf<VtxK> v;
	g.ToSkinned(v);
	return r.AddProcModel(m, v, g.idx);
}

static int MakeBat(Renderer& r)
{
	Model m;
	BuildProcRig(m, "proc_bat", kBatJ, BJ_N);
	m.headJ = BJ_HEAD;
	Geo g;
	g.curBone = BJ_BODY; g.curPal = Pal(0, 4);
	g.Sphere(v3(0, .95f, 0), v3(.17f, .2f, .16f), 12, 8);
	g.curBone = BJ_HEAD;
	g.Sphere(v3(0, 1.15f, -.08f), v3(.14f, .13f, .13f), 12, 8);
	g.curPal = Pal(0, 3);
	g.Cone(v3(.08f, 1.24f, -.06f), v3(.14f, 1.42f, -.04f), .05f);
	g.Cone(v3(-.08f, 1.24f, -.06f), v3(-.14f, 1.42f, -.04f), .05f);
	g.curPal = Pal(4, 0);
	g.Sphere(v3(.06f, 1.17f, -.2f), v3(.035f, .03f, .02f), 6, 4);
	g.Sphere(v3(-.06f, 1.17f, -.2f), v3(.035f, .03f, .02f), 6, 4);
	g.curPal = Pal(0, 15);
	g.Cone(v3(.03f, 1.08f, -.2f), v3(.03f, 1.02f, -.21f), .012f, 5);
	g.Cone(v3(-.03f, 1.08f, -.2f), v3(-.03f, 1.02f, -.21f), .012f, 5);
	// 翼（左 +X）
	for (int side = 0; side < 2; side++) {
		float s = side == 0 ? 1.f : -1.f;
		int j1 = side == 0 ? BJ_WL : BJ_WR, j2 = side == 0 ? BJ_WL2 : BJ_WR2;
		g.curPal = Pal(0, 2);
		g.curBone = j1;
		g.Limb(v3(s * .1f, 1.f, 0), v3(s * .42f, 1.06f, .02f), .025f);
		g.curBone = j2;
		g.Limb(v3(s * .42f, 1.06f, .02f), v3(s * .78f, 1.02f, .05f), .02f);
		g.curPal = Pal(5, 13);
		V3 sh = v3(s * .1f, 1.f, 0), el = v3(s * .42f, 1.06f, .02f), tip = v3(s * .8f, 1.02f, .05f);
		V3 f1 = v3(s * .62f, .8f, .05f), f2 = v3(s * .36f, .78f, .04f), f3 = v3(s * .14f, .84f, .02f);
		g.curBone = j1;
		g.Tri2(sh, el, f3);
		g.Tri2(el, f2, f3);
		g.curBone = j2;
		g.Tri2(el, tip, f1);
		g.Tri2(el, f1, f2);
	}
	g.curBone = BJ_BODY; g.curPal = Pal(0, 3);
	g.Cone(v3(.06f, .8f, 0), v3(.07f, .7f, -.02f), .03f, 5);
	g.Cone(v3(-.06f, .8f, 0), v3(-.07f, .7f, -.02f), .03f, 5);
	Buf<VtxK> v;
	g.ToSkinned(v);
	return r.AddProcModel(m, v, g.idx);
}

static int MakeWyvern(Renderer& r)
{
	Model m;
	BuildProcRig(m, "proc_wyvern", kWyvJ, WJ_N);
	m.headJ = WJ_HEAD;
	Geo g;
	const int body = Pal(0, 12), belly = Pal(13, 2), horn = Pal(14, 2), memb = Pal(0, 8), eye = Pal(4, 3);
	g.curBone = WJ_BODY; g.curPal = body;
	g.Ellipsoid(MMul(MRotX(-.15f), MTrans(0, .95f, .08f)), v3(.3f, .28f, .5f), 14, 9);
	g.curPal = belly;
	g.Ellipsoid(MMul(MRotX(-.15f), MTrans(0, .86f, .02f)), v3(.22f, .2f, .4f), 12, 8);
	// 首
	g.curBone = WJ_NECK; g.curPal = body;
	g.Limb(v3(0, 1.05f, -.25f), v3(0, 1.38f, -.55f), .14f, .11f);
	// 頭
	g.curBone = WJ_HEAD;
	g.Ellipsoid(MMul(MRotX(.2f), MTrans(0, 1.46f, -.66f)), v3(.15f, .13f, .22f), 12, 8);
	g.Ellipsoid(MMul(MRotX(.1f), MTrans(0, 1.43f, -.86f)), v3(.1f, .07f, .12f), 10, 6);
	g.curPal = horn;
	g.Cone(v3(.08f, 1.55f, -.62f), v3(.14f, 1.7f, -.38f), .04f);
	g.Cone(v3(-.08f, 1.55f, -.62f), v3(-.14f, 1.7f, -.38f), .04f);
	g.curPal = eye;
	g.Sphere(v3(.1f, 1.52f, -.76f), v3(.035f, .03f, .03f), 6, 4);
	g.Sphere(v3(-.1f, 1.52f, -.76f), v3(.035f, .03f, .03f), 6, 4);
	g.curBone = WJ_JAW; g.curPal = belly;
	g.Ellipsoid(MMul(MRotX(.15f), MTrans(0, 1.35f, -.8f)), v3(.09f, .045f, .15f), 10, 6);
	g.curPal = Pal(0, 15);
	g.Cone(v3(.05f, 1.4f, -.92f), v3(.05f, 1.36f, -.93f), .015f, 4);
	g.Cone(v3(-.05f, 1.4f, -.92f), v3(-.05f, 1.36f, -.93f), .015f, 4);
	// 背びれ
	g.curBone = WJ_BODY; g.curPal = horn;
	for (int k = 0; k < 4; k++) {
		float z = -.25f + k * .17f;
		g.Cone(v3(0, 1.18f - k * .02f, z), v3(0, 1.32f - k * .03f, z + .08f), .04f, 5);
	}
	// 尾
	g.curBone = WJ_T1; g.curPal = body;
	g.Limb(v3(0, .9f, .4f), v3(0, .84f, .95f), .13f, .08f);
	g.curBone = WJ_T2;
	g.Limb(v3(0, .84f, .95f), v3(0, .78f, 1.45f), .08f, .03f);
	g.curPal = horn;
	g.Cone(v3(0, .78f, 1.4f), v3(0, .8f, 1.62f), .07f, 4);
	g.Cone(v3(.04f, .82f, 1.48f), v3(.1f, .9f, 1.58f), .03f, 4);
	g.Cone(v3(-.04f, .82f, 1.48f), v3(-.1f, .9f, 1.58f), .03f, 4);
	// 脚
	for (int side = 0; side < 2; side++) {
		float s = side == 0 ? 1.f : -1.f;
		g.curBone = side == 0 ? WJ_LL : WJ_LR; g.curPal = body;
		g.Limb(v3(s * .2f, .82f, .12f), v3(s * .24f, .45f, .05f), .09f, .06f);
		g.Limb(v3(s * .24f, .45f, .05f), v3(s * .24f, .1f, .1f), .06f, .05f);
		g.curPal = horn;
		g.Cone(v3(s * .24f, .06f, .04f), v3(s * .24f, .03f, -.12f), .04f, 4);
	}
	// 翼
	for (int side = 0; side < 2; side++) {
		float s = side == 0 ? 1.f : -1.f;
		int j1 = side == 0 ? WJ_WL : WJ_WR, j2 = side == 0 ? WJ_WL2 : WJ_WR2;
		V3 sh = v3(s * .2f, 1.12f, 0), el = v3(s * .85f, 1.3f, .08f), tip = v3(s * 1.6f, 1.15f, .25f);
		V3 f1 = v3(s * 1.3f, .7f, .5f), f2 = v3(s * .85f, .62f, .55f), f3 = v3(s * .3f, .8f, .5f);
		g.curPal = body;
		g.curBone = j1;
		g.Limb(sh, el, .05f, .04f);
		g.curBone = j2;
		g.Limb(el, tip, .04f, .02f);
		g.Limb(el, f1, .025f, .015f);
		g.curPal = memb;
		g.curBone = j1;
		g.Tri2(sh, el, f3);
		g.Tri2(el, f2, f3);
		g.curBone = j2;
		g.Tri2(el, tip, f1);
		g.Tri2(el, f1, f2);
	}
	Buf<VtxK> v;
	g.ToSkinned(v);
	return r.AddProcModel(m, v, g.idx);
}

static int MakeGolem(Renderer& r)
{
	Model m;
	BuildProcRig(m, "proc_golem", kGolJ, GJ_N);
	m.headJ = GJ_HEAD;
	Geo g;
	const int stone = Pal(0, 9), dark = Pal(0, 6), moss = Pal(6, 5), eye = Pal(4, 8), crack = Pal(1, 4);
	g.curBone = GJ_HIPS; g.curPal = dark;
	g.Sphere(v3(0, .40f, 0), v3(.32f, .16f, .26f), 12, 8);
	g.curPal = stone;
	g.Sphere(v3(-.18f, .26f, .04f), v3(.14f, .10f, .12f), 10, 6);
	g.Sphere(v3(.16f, .24f, -.02f), v3(.13f, .09f, .12f), 10, 6);
	g.curBone = GJ_TORSO; g.curPal = stone;
	g.Sphere(v3(0, .92f, .02f), v3(.40f, .30f, .28f), 14, 10);
	g.Sphere(v3(.06f, 1.16f, .06f), v3(.26f, .18f, .20f), 12, 8);
	g.curPal = moss;
	g.Sphere(v3(-.14f, 1.24f, .10f), v3(.10f, .05f, .08f), 8, 5);
	g.curPal = crack;
	g.Sphere(v3(.22f, .82f, -.16f), v3(.07f, .09f, .05f), 8, 5);
	g.curPal = stone;
	g.Sphere(v3(-.34f, 1.18f, .04f), v3(.12f, .1f, .12f), 8, 6);
	g.Sphere(v3(.36f, 1.14f, -.02f), v3(.11f, .09f, .11f), 8, 6);
	g.curPal = eye;
	g.Sphere(v3(0, .96f, -.24f), v3(.05f, .07f, .035f), 8, 5);
	g.curBone = GJ_HEAD; g.curPal = stone;
	g.Sphere(v3(0, 1.50f, -.02f), v3(.20f, .16f, .18f), 12, 8);
	g.Sphere(v3(.04f, 1.66f, .04f), v3(.10f, .07f, .10f), 8, 6);
	g.curPal = dark;
	g.Sphere(v3(0, 1.44f, -.16f), v3(.05f, .025f, .04f), 6, 4);
	g.curPal = eye;
	g.Sphere(v3(.07f, 1.52f, -.16f), v3(.04f, .03f, .025f), 6, 4);
	g.Sphere(v3(-.07f, 1.52f, -.16f), v3(.04f, .03f, .025f), 6, 4);
	for (int side = 0; side < 2; side++) {
		float s = side == 0 ? 1.f : -1.f;
		g.curBone = side == 0 ? GJ_AL : GJ_AR; g.curPal = stone;
		g.Sphere(v3(s * .52f, 1.10f, 0), v3(.18f, .14f, .16f), 10, 7);
		g.curPal = moss;
		g.Sphere(v3(s * .58f, 1.20f, .06f), v3(.05f, .035f, .04f), 6, 4);
		g.curBone = side == 0 ? GJ_FL : GJ_FR; g.curPal = stone;
		g.Sphere(v3(s * .62f, .70f, -.02f), v3(.15f, .18f, .15f), 10, 7);
		g.curPal = dark;
		g.Sphere(v3(s * .74f, .58f, -.10f), v3(.09f, .07f, .07f), 8, 5);
		g.curBone = side == 0 ? GJ_LL : GJ_LR; g.curPal = stone;
		g.Sphere(v3(s * .20f, .26f, 0), v3(.15f, .20f, .15f), 10, 7);
		g.curPal = dark;
		g.Sphere(v3(s * .22f, .05f, .04f), v3(.16f, .05f, .20f), 8, 5);
	}
	Buf<VtxK> v;
	g.ToSkinned(v);
	return r.AddProcModel(m, v, g.idx);
}

// 手続きアニメ（clip 種別ごと）
enum { PK_IDLE, PK_MOVE, PK_ATTACK, PK_CAST, PK_HIT, PK_DEATH, PK_CHEER, PK_RISE };
static int ProcKind(int clip)
{
	switch (clip) {
	case CL_WALK: case CL_RUN: case CL_JUMP: return PK_MOVE;
	case CL_1H_CHOP: case CL_1H_SLICE: case CL_1H_STAB: case CL_2H_CHOP: case CL_2H_SPIN: case CL_1H_SHOOT: case CL_2H_SHOOT:
	case CL_THROW: case CL_PUNCH: return PK_ATTACK;
	case CL_CAST_SHOOT: case CL_CAST_RAISE: case CL_CAST_LONG: return PK_CAST;
	case CL_HIT_A: case CL_HIT_B: case CL_DODGE: return PK_HIT;
	case CL_DEATH: return PK_DEATH;
	case CL_SPAWN: return PK_RISE;
	case CL_CHEER: case CL_TAUNT: case CL_INTERACT: case CL_USE: return PK_CHEER;
	}
	return PK_IDLE;
}
static float ProcLen(int kind)
{
	static const float L[] = { 2.f, .6f, .9f, 1.2f, .5f, 1.2f, 1.4f, 1.05f };
	return L[kind];
}
static int ProcLoops(int kind) { return kind == PK_IDLE || kind == PK_MOVE; }

static Q4 QX(float a) { return QAxis(v3(1, 0, 0), a); }
static Q4 QY(float a) { return QAxis(v3(0, 1, 0), a); }
static Q4 QZ(float a) { return QAxis(v3(0, 0, 1), a); }

// 攻撃カーブ: 溜め→突き→戻り（0..1）
static float AtkCurve(float u)
{
	if (u < .35f) return -Smooth01(u / .35f) * .5f;
	if (u < .5f) return -.5f + Smooth01((u - .35f) / .15f) * 1.5f;
	return 1.f - Smooth01((u - .5f) / .5f);
}

static void ProcAnim(int look, int clip, float t, Q4* rot, V3* tr, PoseMods& mods, int nJ)
{
	for (int i = 0; i < nJ; i++) { rot[i] = Q4{ 0, 0, 0, 1 }; tr[i] = v3(0, 0, 0); }
	mods.Reset(nJ);
	int kind = ProcKind(clip);
	float L = ProcLen(kind);
	float u = ProcLoops(kind) ? 0 : Clampf(t / L, 0, 1);
	if (look == LK_SLIME) {
		float sq = 1.f, lift = 0, fwd = 0, shake = 0;
		switch (kind) {
		case PK_IDLE: sq = 1.f + .07f * sinf(t * 4.f); break;
		case PK_MOVE: { float ph = fmodf(t / .42f, 1.f); lift = sinf(ph * PI) * .42f; sq = ph < .2f ? 1.f - .4f * sinf(ph / .2f * PI) : 1.f + .22f * sinf(ph * PI); } break;
		case PK_ATTACK: { float a = AtkCurve(u); sq = 1.f - a * .25f; fwd = max(0.f, a) * .45f; lift = max(0.f, a) * .15f; } break;
		case PK_CAST: sq = 1.f + .2f * sinf(u * PI * 3.f) * (1 - u); break;
		case PK_HIT: sq = 1.f - .3f * sinf(u * PI); shake = sinf(u * 40.f) * .05f * (1 - u); break;
		case PK_DEATH: sq = Lerpf(1.f, .12f, Smooth01(u)); break;
		case PK_CHEER: { float ph = fmodf(u * 3.f, 1.f); lift = sinf(ph * PI) * .3f; sq = 1.f + .15f * sinf(ph * PI); } break;
		case PK_RISE: { float k = 1.f - Smooth01(u); sq = Lerpf(1.f, .12f, k); lift = sinf(Smooth01(u) * PI) * .28f; } break;
		}
		float xz = 1.f / sqrtf(max(.1f, sq));
		if (kind == PK_DEATH) xz = Lerpf(1.f, 1.6f, Smooth01(u));
		if (kind == PK_RISE) xz = Lerpf(1.f, 1.6f, 1.f - Smooth01(u));
		mods.scale[SJ_BODY] = v3(xz, sq, xz);
		mods.any = 1;
		tr[SJ_ROOT] = v3(shake, lift, -fwd);
		float wob = sinf(t * 3.2f);
		rot[SJ_BODY] = QX(wob * .06f + (kind == PK_ATTACK ? max(0.f, AtkCurve(u)) * .35f : 0.f));
		tr[SJ_TOP] = v3(sinf(t * 5.1f) * .03f, wob * .04f, cosf(t * 4.2f) * .02f);
		if (kind == PK_CAST) tr[SJ_TOP].y += sinf(u * PI * 4.f) * .08f;
		if (kind == PK_HIT) rot[SJ_BODY] = QZ(sinf(u * PI) * .4f);
		if (kind == PK_DEATH) rot[SJ_BODY] = QX(Smooth01(u) * .8f);
		return;
	}
	if (look == LK_BAT) {
		float flapSp = kind == PK_ATTACK ? 22.f : 15.f;
		float flap = sinf(t * flapSp);
		float bob = sinf(t * flapSp) * .05f;
		Q4 wl = QZ(flap * .75f), wr = QZ(-flap * .75f);
		rot[BJ_WL] = wl; rot[BJ_WR] = wr;
		rot[BJ_WL2] = QZ(flap * .35f); rot[BJ_WR2] = QZ(-flap * .35f);
		V3 root = v3(0, bob, 0);
		if (kind == PK_MOVE) {
			float ph = t * 12.f;
			rot[BJ_BODY] = QX(-.4f + sinf(ph) * .18f);
			root.y += fabsf(sinf(ph)) * .1f;
		}
		if (kind == PK_ATTACK) {
			float a = AtkCurve(u);
			root.z = -max(0.f, a) * .7f; root.y -= max(0.f, a) * .28f;
			rot[BJ_BODY] = QX(-a * .7f); rot[BJ_HEAD] = QX(-a * .45f);
			rot[BJ_WL] = QZ(Lerpf(flap * .75f, 1.1f, max(0.f, a)));
			rot[BJ_WR] = QZ(Lerpf(-flap * .75f, -1.1f, max(0.f, a)));
		}
		rot[BJ_HEAD] = QMul(rot[BJ_HEAD], QY(sinf(t * 2.4f) * .12f));
		if (kind == PK_CAST) rot[BJ_HEAD] = QX(-sinf(u * PI) * .4f);
		if (kind == PK_HIT) { root.z = sinf(u * PI) * .25f; rot[BJ_BODY] = QX(sinf(u * PI) * .5f); }
		if (kind == PK_DEATH) { root.y -= Smooth01(u) * .85f; rot[BJ_BODY] = QX(Smooth01(u) * 1.5f); rot[BJ_WL] = QZ(-.4f); rot[BJ_WR] = QZ(.4f); }
		if (kind == PK_RISE) {
			float k = 1.f - Smooth01(u);
			root.y = -k * .85f + sinf(Smooth01(u) * PI) * .16f;
			rot[BJ_BODY] = QX(k * 1.5f);
			rot[BJ_WL] = QZ(Lerpf(flap * .75f, -.4f, k));
			rot[BJ_WR] = QZ(Lerpf(-flap * .75f, .4f, k));
		}
		if (kind == PK_CHEER) root.y += sinf(u * PI * 2.f) * .15f;
		tr[BJ_ROOT] = root;
		return;
	}
	if (look == LK_WYVERN) {
		float flapSp = kind == PK_MOVE ? 7.f : 4.5f;
		float flap = sinf(t * flapSp);
		rot[WJ_WL] = QMul(QY(.15f), QZ(flap * .55f + .1f)); rot[WJ_WR] = QMul(QY(-.15f), QZ(-flap * .55f - .1f));
		rot[WJ_WL2] = QZ(flap * .3f); rot[WJ_WR2] = QZ(-flap * .3f);
		rot[WJ_T1] = QY(sinf(t * 1.7f) * .32f); rot[WJ_T2] = QMul(QY(sinf(t * 1.7f - .9f) * .55f), QX(sinf(t * 2.3f) * .12f));
		rot[WJ_NECK] = QX(sinf(t * 2.f) * .05f);
		V3 root = v3(0, -flap * .06f, 0);
		rot[WJ_LL] = QX(.3f); rot[WJ_LR] = QX(.3f);
		if (kind == PK_MOVE) {
			float ph = t * 9.f;
			rot[WJ_LL] = QX(.25f + sinf(ph) * .4f);
			rot[WJ_LR] = QX(.25f - sinf(ph) * .4f);
			root.y += fabsf(sinf(ph)) * .1f;
			rot[WJ_BODY] = QX(sinf(ph) * .08f);
		}
		if (kind == PK_ATTACK) {
			float a = AtkCurve(u);
			rot[WJ_NECK] = QX(-a * .8f); rot[WJ_HEAD] = QX(-a * .4f);
			rot[WJ_JAW] = QX(max(0.f, a) * .9f + (u < .4f ? Smooth01(u / .4f) * .5f : 0));
			rot[WJ_WL] = QMul(rot[WJ_WL], QZ(max(0.f, a) * .4f));
			rot[WJ_WR] = QMul(rot[WJ_WR], QZ(-max(0.f, a) * .4f));
			root.z = -max(0.f, a) * .5f;
			root.y += max(0.f, -a) * .12f;
		}
		if (kind == PK_CAST) {
			float k = sinf(u * PI);
			rot[WJ_NECK] = QX(k * .4f - Smooth01(u) * .5f); rot[WJ_JAW] = QX(k * .7f);
		}
		if (kind == PK_HIT) { rot[WJ_BODY] = QX(sinf(u * PI) * .3f); root.z = sinf(u * PI) * .2f; }
		if (kind == PK_DEATH) { float k = Smooth01(u); rot[WJ_BODY] = QZ(k * 1.1f); root.y -= k * .6f; rot[WJ_WL] = QZ(-.6f * k); rot[WJ_WR] = QZ(.2f); }
		if (kind == PK_RISE) {
			float k = 1.f - Smooth01(u);
			rot[WJ_BODY] = QZ(k * 1.1f);
			root.y = -k * .6f + sinf(Smooth01(u) * PI) * .12f;
			rot[WJ_WL] = QMul(QY(.15f * (1.f - k)), QZ(-.6f * k));
			rot[WJ_WR] = QMul(QY(-.15f * (1.f - k)), QZ(.2f * k));
		}
		if (kind == PK_CHEER) { rot[WJ_NECK] = QX(-sinf(u * PI) * .6f); rot[WJ_JAW] = QX(sinf(u * PI) * .6f); }
		tr[WJ_ROOT] = root;
		return;
	}
	if (look == LK_GOLEM) {
		V3 root = v3(0, 0, 0);
		float br = sinf(t * 1.6f);
		rot[GJ_TORSO] = QX(br * .03f);
		rot[GJ_AL] = QZ(.08f + br * .03f); rot[GJ_AR] = QZ(-.08f - br * .03f);
		if (kind == PK_MOVE) {
			float ph = t * 6.2f;
			float step = sinf(ph);
			rot[GJ_LL] = QX(step * .95f); rot[GJ_LR] = QX(-step * .95f);
			rot[GJ_AL] = QX(-step * .55f); rot[GJ_AR] = QX(step * .55f);
			rot[GJ_FL] = QX(max(0.f, -step) * .6f); rot[GJ_FR] = QX(max(0.f, step) * .6f);
			rot[GJ_TORSO] = QMul(QZ(step * .06f), QY(step * .05f));
			rot[GJ_HIPS] = QY(-step * .08f);
			rot[GJ_HEAD] = QX(-step * .06f);
			root.y = fabsf(cosf(ph)) * .05f;
		}
		if (kind == PK_ATTACK) {
			float a = AtkCurve(u);
			float wind = max(0.f, -a);
			float hit = max(0.f, a);
			rot[GJ_AR] = QX(-2.2f * wind - 1.6f * hit);
			rot[GJ_FR] = QX(-hit * .9f);
			rot[GJ_AL] = QX(-wind * .4f);
			rot[GJ_TORSO] = QMul(QY(-a * .5f), QX(-hit * .4f + wind * .3f));
			rot[GJ_HEAD] = QX(wind * .3f - hit * .2f);
			root.z = -hit * .28f;
			root.y = wind * .06f;
		}
		if (kind == PK_CAST) {
			float k = sinf(u * PI);
			rot[GJ_AL] = QX(-k * 2.4f); rot[GJ_AR] = QX(-k * 2.4f);
			root.y = k * .15f - (u > .6f ? sinf((u - .6f) / .4f * PI) * .1f : 0);
		}
		if (kind == PK_HIT) { rot[GJ_TORSO] = QX(sinf(u * PI) * .3f); root.z = sinf(u * PI) * .12f; }
		if (kind == PK_DEATH) { float k = Smooth01(u); rot[GJ_HIPS] = QX(-k * 1.35f); root.y = -k * .3f; root.z = -k * .3f; }
		if (kind == PK_RISE) {
			float k = 1.f - Smooth01(u);
			rot[GJ_HIPS] = QX(-k * 1.35f);
			rot[GJ_TORSO] = QX((1.f - k) * .08f);
			root.y = -k * .3f + sinf(Smooth01(u) * PI) * .08f;
			root.z = -k * .3f;
		}
		if (kind == PK_CHEER) { float k = sinf(u * PI); rot[GJ_AL] = QX(-k * 2.6f); rot[GJ_AR] = QX(-k * 2.6f); }
		tr[GJ_ROOT] = root;
	}
}

// ============================================================ 髪・翼など
static const DWORD kHairCol[HAIR_COLORS] = {
	0x1C1A1E, 0x3A2618, 0x6A4224, 0x8E3E1E, 0xE0C060, 0xE8E4DA, 0xC0302A, 0xF090B8, 0x3E68D0, 0x48A060
};
DWORD HairColor(int i) { DWORD c = kHairCol[Clampi(i, 0, HAIR_COLORS - 1)]; return RGBA8((c >> 16) & 255, (c >> 8) & 255, c & 255); }
float OutfitHue(int i) { return Clampi(i, 0, OUTFIT_COLORS - 1) * (2.f * PI / OUTFIT_COLORS); }

static void BuildHair(Renderer& r, const Model& m, int style, GpuMesh& out, float drop = 1.f, Buf<VtxS>* cpu = NULL, V3* pivot = NULL)
{
	if (style <= 0 || m.headJ < 0) return;
	M4 bind = MInverse(m.invBind[m.headJ]);
	V3 o = v3(bind.m[12], bind.m[13], bind.m[14]);
	V3 mn = v3(m.headMin[0], m.headMin[1], m.headMin[2]), mx = v3(m.headMax[0], m.headMax[1], m.headMax[2]);
	const bool humHair = drop > 1.01f;
	V3 c = o + (mn + mx) * .5f;
	V3 rr = (mx - mn) * .5f;
	if (!humHair) {
		rr.z = min(rr.z, rr.x);
		rr.y = min(rr.y, rr.x * 1.05f);
	}
	Geo g;
	g.curCol = 0xFFFFFFFF;
	V3 capR = v3(rr.x * 1.08f, rr.y * 1.02f, rr.z * 1.15f);
	V3 cc = c;
	// 人間は頭蓋の大きさで髪型を作り分ける。顔は -Z。目の前は開けておく。
	if (humHair && rr.x < 0.16f && rr.y < 0.22f) {
		const float rx = rr.x, ry = rr.y, rz = rr.z;
		auto capKeep = [](V3 d) { return !(d.z < -0.05f && d.y < 0.42f); };
		if (style == 1) {
			g.Ellipsoid(MTrans(c.x, c.y, c.z), capR, 18, 12, capKeep);
			for (int k = 0; k < 5; k++) {
				float a = -0.4f + k * 0.2f;
				V3 d = Norm(v3(sinf(a), 0.82f, 0.25f));
				V3 b = c + v3(d.x * capR.x, d.y * capR.y, d.z * capR.z) * 0.95f;
				g.Sphere(b + v3(0, ry * 0.22f, 0), v3(rx * 0.16f, ry * 0.22f, rz * 0.16f), 6, 4);
			}
		} else if (style == 5) {
			g.Ellipsoid(MTrans(c.x, c.y - ry * 0.05f, c.z + rz * 0.15f), v3(rx * 1.25f, ry * 0.95f, rz * 1.35f), 16, 10, [](V3 d) { return d.z > -0.25f; });
		} else {
			g.Ellipsoid(MTrans(c.x, c.y + ry * 0.05f, c.z + rz * 0.08f), capR, 16, 10, capKeep);
		}
		// 束の中心は後頭の外。描画時に、着ている服の背面よりさらに後ろへ出す。
		auto addHang = [&](float hang, int nLock, float xSpread, float width) {
			const int rows = 16;
			for (int s = 0; s < nLock; s++) {
				float u = nLock <= 1 ? 0.f : ((s + 0.5f) / (float)nLock) * 2.f - 1.f;
				V3 line[16];
				float hw[16];
				for (int i = 0; i < rows; i++) {
					float t = i / (float)(rows - 1);
					float y = c.y - t * hang;
					// 付け根は頭蓋の後ろ。下へ行くほど服の背面に合わせ、描画側で足りない分を出す。
					float z = c.z + rr.z * 1.05f + t * 0.03f;
					float x = c.x + u * xSpread * (1.f + t * 0.12f);
					line[i] = v3(x, y, z);
					hw[i] = width * (1.f - t * 0.22f);
				}
				g.Lock(line, rows, hw, 0.018f);
			}
		};
		if (style == 2) addHang(0.40f, 3, rx * 0.90f, rx * 0.34f);
		if (style == 3) addHang(0.50f, 1, 0.f, rx * 0.42f);
		if (style == 4) addHang(0.48f, 2, rx * 1.35f, rx * 0.30f);
		if (style == 6) addHang(0.54f, 4, rx * 1.05f, rx * 0.32f);
		if (style == 7) addHang(0.64f, 5, rx * 1.15f, rx * 0.30f);
	} else {
	// KayKit など、頭の箱に被せる殻。
	float capS = style == 1 ? 1.06f : 1.08f;
	capR = v3(rr.x * capS, rr.y * capS, rr.z * capS);
	cc = c + v3(0, rr.y * .05f, rr.z * .04f);
	float lowBack = style == 5 ? -.72f : (style == 2 ? -.45f : -.3f);
	g.Ellipsoid(MTrans(cc.x, cc.y, cc.z), capR, 20, 14, [lowBack, humHair](V3 d) {
		// true の面を残す。人間は額の生え際より下の顔だけ抜く。
		if (humHair) return !(d.z < -0.35f && d.y < 0.30f);
		if (d.z < -.3f) return d.y > .26f;
		if (d.z < 0.f) return d.y > Lerpf(.26f, lowBack + .2f, (d.z + .3f) / .3f);
		return d.y > lowBack;
	});
	if (style == 1) {
		for (int k = 0; k < 7; k++) {
			float a = k * 2.f * PI / 7.f;
			V3 d = Norm(v3(cosf(a) * .7f, .75f, sinf(a) * .7f + .15f));
			if (humHair && d.z < 0.f) continue;
			V3 b = cc + v3(d.x * capR.x, d.y * capR.y, d.z * capR.z) * .92f;
			g.Cone(b, b + d * (rr.x * .35f), rr.x * .18f, 6);
		}
	}
	if (style == 2) {
		g.Ellipsoid(MTrans(c.x, c.y - rr.y * .5f, c.z + rr.z * .62f), v3(rr.x * .92f, rr.y * 1.05f, rr.z * .42f), 16, 10);
		g.Ellipsoid(MTrans(c.x, c.y - rr.y * 1.3f, c.z + rr.z * .7f), v3(rr.x * .78f, rr.y * .62f, rr.z * .3f), 14, 8);
		for (int side = 0; side < 2; side++) {
			float s = side == 0 ? 1.f : -1.f;
			g.Ellipsoid(MTrans(c.x + s * rr.x * .9f, c.y - rr.y * .5f, c.z - rr.z * .12f), v3(rr.x * .17f, rr.y * .72f, rr.z * .32f), 10, 8);
		}
	}
	if (style == 3) {
		for (int k = 0; k < 5; k++) {
			float t = k / 4.f;
			V3 p = c + v3(0, rr.y * (.55f - t * 1.25f), rr.z * (1.05f + .25f * sinf(t * PI)));
			float s = Lerpf(.3f, .14f, t) * rr.x;
			g.Sphere(p, v3(s, s * 1.3f, s), 10, 6);
		}
		g.curCol = 0xFF3040D0;
		g.Sphere(c + v3(0, rr.y * .6f, rr.z * 1.0f), v3(rr.x * .16f, rr.x * .1f, rr.x * .12f), 8, 5);
	}
	if (style == 4) {
		for (int side = 0; side < 2; side++) {
			float s = side == 0 ? 1.f : -1.f;
			for (int k = 0; k < 5; k++) {
				float t = k / 4.f;
				V3 p = c + v3(s * rr.x * (1.02f + .1f * sinf(t * PI)), rr.y * (.35f - t * 1.2f), rr.z * .25f);
				float sz = Lerpf(.24f, .12f, t) * rr.x;
				g.Sphere(p, v3(sz, sz * 1.35f, sz), 10, 6);
			}
		}
		g.curCol = 0xFF3040D0;
		g.Sphere(c + v3(rr.x * .98f, rr.y * .4f, rr.z * .25f), v3(rr.x * .1f, rr.x * .12f, rr.x * .12f), 8, 5);
		g.Sphere(c + v3(-rr.x * .98f, rr.y * .4f, rr.z * .25f), v3(rr.x * .1f, rr.x * .12f, rr.x * .12f), 8, 5);
	}
	if (style == 5) {
		g.Ellipsoid(MTrans(c.x, c.y - rr.y * .35f, c.z + rr.z * .18f), v3(rr.x * 1.1f, rr.y * .55f, rr.z * 1.0f), 18, 10, [](V3 d) { return d.z > -.35f; });
	}
	if (style == 6 || style == 7) {
		float hang = style == 7 ? rr.y * 3.4f : rr.y * 2.6f;
		float w0 = style == 7 ? rr.x * 1.05f : rr.x * 0.82f;
		int layers = style == 7 ? 8 : 6;
		for (int k = 0; k < layers; k++) {
			float t = k / (layers - 1.f);
			V3 p = c + v3(0, -t * hang * .85f, rr.z * (.55f + t * .35f));
			float w = Lerpf(w0, w0 * .55f, t);
			g.Ellipsoid(MTrans(p.x, p.y, p.z), v3(w, hang * .18f, rr.z * Lerpf(.42f, .28f, t)), 14, 8);
		}
	}
	}
	if (drop > 1.01f && !humHair) {
		float crown = cc.y;
		for (size_t i = 0; i < g.p.size(); i++) {
			float dy = crown - g.p[i].y;
			if (dy > 0.02f) g.p[i].y -= dy * (drop - 1.f);
		}
	}
	{
		// 頭蓋を覆う殻は頭に固定し、頭より下へ垂れた髪だけがシェーダの揺れを受ける。αが小さいほど大きく靡く。
		float root = humHair ? cc.y - capR.y * .55f : cc.y;
		float hem = root;
		for (size_t i = 0; i < g.p.size(); i++) if (g.p[i].y < hem) hem = g.p[i].y;
		float span = max(0.05f, root - hem);
		for (size_t i = 0; i < g.p.size(); i++) {
			float t = Clampf((root - g.p[i].y) / span, 0.f, 1.f);
			int a = 255 - (int)(t * t * 170.f);
			g.col[i] = (g.col[i] & 0x00FFFFFFu) | ((DWORD)a << 24);
		}
		if (pivot) *pivot = MXformPt(m.invBind[m.headJ], c);
	}
	// CPU 側は頭蓋中心からの差分（モデル軸）。頭ボーンのローカル軸はモデルと揃っていない。
	if (cpu) {
		cpu->clear();
		g.ToStatic(*cpu);
		for (size_t i = 0; i < cpu->size(); i++) {
			(*cpu)[i].p[0] -= c.x;
			(*cpu)[i].p[1] -= c.y;
			(*cpu)[i].p[2] -= c.z;
		}
	}
	g.Transform(m.invBind[m.headJ]);
	GeoToStatic(r, g, out);
	out.tex = r.TexWhite();
}

// 人体用。原点は後頭部、+Y 上、+Z 後ろ。板ではなく束で垂らす。
static void BuildWing(Renderer& r, int dragon, int left, GpuMesh& out)
{
	Geo g;
	if (!dragon) {
		g.curCol = RGBA8(250, 250, 255);
		g.Limb(v3(0, 0, 0), v3(.55f, .18f, 0), .05f, .035f);
		for (int k = 0; k < 7; k++) {
			float a = .35f - k * .17f;
			float L = .95f - k * .07f;
			V3 st = v3(.08f + k * .07f, .1f - k * .01f, .01f * k);
			V3 d = v3(cosf(a), sinf(a), 0);
			V3 c = st + d * (L * .5f);
			M4 rm = MRotZ(a);
			rm.m[12] = c.x; rm.m[13] = c.y; rm.m[14] = c.z;
			g.curCol = k < 3 ? RGBA8(255, 255, 255) : RGBA8(226, 236, 255);
			g.Ellipsoid(rm, v3(L * .5f, .075f, .022f), 10, 6);
		}
	} else {
		V3 sh = v3(0, 0, 0), el = v3(.6f, .25f, .02f), tip = v3(1.25f, .05f, .1f);
		V3 f1 = v3(1.0f, -.45f, .12f), f2 = v3(.6f, -.55f, .1f), f3 = v3(.15f, -.35f, .05f);
		g.curCol = RGBA8(70, 30, 30);
		g.Limb(sh, el, .045f, .035f);
		g.Limb(el, tip, .035f, .015f);
		g.Limb(el, f1, .02f, .012f);
		g.Limb(el, f2, .02f, .012f);
		g.curCol = RGBA8(150, 40, 36);
		g.Tri2(sh, el, f3);
		g.Tri2(el, f2, f3);
		g.Tri2(el, f1, f2);
		g.Tri2(el, tip, f1);
		g.curCol = RGBA8(230, 210, 170);
		g.Cone(el, el + v3(.04f, .14f, 0), .03f, 5);
	}
	if (!left) g.MirrorX();
	GeoToStatic(r, g, out);
}

static void BuildCrown(Renderer& r, GpuMesh& out)
{
	Geo g;
	g.curCol = RGBA8(255, 205, 60);
	const int N = 16;
	for (int i = 0; i < N; i++) {
		float a0 = 2 * PI * i / N, a1 = 2 * PI * (i + 1) / N;
		V3 p0 = v3(cosf(a0) * .18f, 0, sinf(a0) * .18f), p1 = v3(cosf(a1) * .18f, 0, sinf(a1) * .18f);
		V3 q0 = p0 + v3(0, .08f, 0), q1 = p1 + v3(0, .08f, 0);
		V3 n0 = Norm(v3(p0.x, 0, p0.z)), n1 = Norm(v3(p1.x, 0, p1.z));
		int a = g.V(p0, n0), b = g.V(p1, n1), c = g.V(q1, n1), d = g.V(q0, n0);
		g.T(a, b, c); g.T(a, c, d);
		int a2 = g.V(p0, n0 * -1.f), b2 = g.V(p1, n1 * -1.f), c2 = g.V(q1, n1 * -1.f), d2 = g.V(q0, n0 * -1.f);
		g.T(a2, c2, b2); g.T(a2, d2, c2);
	}
	for (int i = 0; i < 6; i++) {
		float a = 2 * PI * i / 6;
		V3 b = v3(cosf(a) * .18f, .06f, sinf(a) * .18f);
		g.Cone(b, b + v3(cosf(a) * .02f, .14f, sinf(a) * .02f), .045f, 6);
	}
	g.curCol = RGBA8(220, 40, 70);
	for (int i = 0; i < 6; i++) {
		float a = 2 * PI * (i + .5f) / 6;
		g.Sphere(v3(cosf(a) * .19f, .04f, sinf(a) * .19f), v3(.025f, .025f, .025f), 6, 4);
	}
	GeoToStatic(r, g, out);
}

static void BuildRing(Renderer& r, GpuMesh& out)
{
	Geo g;
	g.curCol = 0xFFFFFFFF;
	const int N = 40;
	for (int i = 0; i < N; i++) {
		float a0 = 2 * PI * i / N, a1 = 2 * PI * (i + 1) / N;
		V3 up = v3(0, 1, 0);
		V3 i0 = v3(cosf(a0) * .36f, 0, sinf(a0) * .36f), i1 = v3(cosf(a1) * .36f, 0, sinf(a1) * .36f);
		V3 o0 = v3(cosf(a0) * .46f, 0, sinf(a0) * .46f), o1 = v3(cosf(a1) * .46f, 0, sinf(a1) * .46f);
		int a = g.V(i0, up), b = g.V(o0, up), c = g.V(o1, up), d = g.V(i1, up);
		g.T(a, b, c); g.T(a, c, d);
	}
	GeoToStatic(r, g, out);
}

static void BuildOrb(Renderer& r, GpuMesh& out)
{
	Geo g;
	g.curCol = 0xFFFFFFFF;
	g.Sphere(v3(0, 0, 0), v3(1, 1, 1), 16, 10);
	GeoToStatic(r, g, out);
}

BOOL BuildLibrary(Renderer& r, Library& lib)
{
	ReleaseLibrary(lib);
	static const char* const bodies[B_COUNT] = { "Knight", "Barbarian", "Mage", "Rogue", "Rogue_Hooded" };
	static const char* const skels[4] = { "Skeleton_Warrior", "Skeleton_Minion", "Skeleton_Rogue", "Skeleton_Mage" };
	for (int i = 0; i < B_COUNT; i++) lib.bodyModel[i] = r.FindModel(bodies[i]);
	for (int i = 0; i < 4; i++) lib.skelModel[i] = r.FindModel(skels[i]);
	for (int b = 0; b < B_COUNT; b++) if (lib.bodyModel[b] < 0) return FALSE;
	for (int g = 0; g < 4; g++) {
		for (int c = 0; c < CL_COUNT; c++) {
			lib.clipMap[g][c] = -1;
			if (g < (int)r.rigs.size()) lib.clipMap[g][c] = r.rigs[g].FindClip(kClipNames[c]);
		}
		if (g < (int)r.rigs.size())
			for (int c = 0; c < CL_COUNT; c++) if (lib.clipMap[g][c] < 0) lib.clipMap[g][c] = lib.clipMap[g][CL_IDLE];
	}
	lib.procModel[0] = -1;
	lib.procModel[LK_SLIME] = MakeSlime(r);
	lib.procModel[LK_BAT] = MakeBat(r);
	lib.procModel[LK_WYVERN] = MakeWyvern(r);
	lib.procModel[LK_GOLEM] = MakeGolem(r);
	for (int b = 0; b < B_COUNT; b++)
		for (int s = 1; s < HAIR_STYLES; s++) BuildHair(r, r.models[lib.bodyModel[b]], s, lib.hair[b][s]);
	lib.humHairN = 0;
	for (int i = 0; i < (int)r.models.size() && lib.humHairN < HUM_HAIR_N; i++) {
		if (strncmp(r.models[i].name, "Hum", 3) != 0 || r.models[i].headJ < 0) continue;
		int slot = lib.humHairN++;
		lib.humHairModel[slot] = i;
		for (int s = 1; s < HAIR_STYLES; s++) BuildHair(r, r.models[i], s, lib.humHair[slot][s], 2.2f, &lib.humHairV[slot][s], &lib.humHairPivot[slot][s]);
	}
	for (int d = 0; d < 2; d++) for (int l = 0; l < 2; l++) BuildWing(r, d, l, lib.wing[d][l]);
	BuildCrown(r, lib.crown);
	BuildRing(r, lib.ring);
	BuildOrb(r, lib.orb);
	// ステージ入場でモンスターをこの印の直後へ読み直す。手続き4体は印より前に残す。
	// 印の位置だと骨を差し替えられ、メッシュだけがタイルの赤丸から外れて巨大になる。
	r.m_keepModels = (int)r.models.size();
	lib.ready = 1;
	return TRUE;
}

void ReleaseLibrary(Library& lib)
{
	for (int b = 0; b < B_COUNT; b++) for (int s = 0; s < HAIR_STYLES; s++) lib.hair[b][s].Release();
	for (int i = 0; i < HUM_HAIR_N; i++) for (int s = 0; s < HAIR_STYLES; s++) { lib.humHair[i][s].Release(); lib.humHairV[i][s].clear(); }
	lib.humHairN = 0;
	for (int d = 0; d < 2; d++) for (int l = 0; l < 2; l++) lib.wing[d][l].Release();
	lib.crown.Release();
	lib.ring.Release();
	lib.orb.Release();
	lib.ready = 0;
}

// ============================================================ アクター
static void AddAtt(Actor& a, const Renderer& r, const char* name)
{
	if (a.nAtt >= 6) return;
	int i = r.FindAttach(name);
	if (i < 0) return;
	const Model& m = r.models[a.model];
	int j = -1;
	if (m.rig >= 0 && m.rig < (int)r.rigs.size()) {
		const char* jn = r.attaches[i].joint;
		j = r.rigs[m.rig].Find(jn);
		if (j < 0 && jn) {
			if (strstr(jn, "head") || strstr(jn, "Head")) j = r.rigs[m.rig].Find("Head");
			else if (strstr(jn, "chest") || strstr(jn, "Chest")) j = r.rigs[m.rig].Find("Chest");
			if (j < 0) {
				bool left = strstr(jn, ".l") || strstr(jn, "_l") || strstr(jn, "Left");
				j = r.rigs[m.rig].Find(left ? "Hand.L" : "Hand.R");
			}
		}
	}
	if (j < 0) return;
	a.att[a.nAtt] = i; a.attJ[a.nAtt] = j; a.nAtt++;
}

static void ActorInit(Actor& a)
{
	memset(&a, 0, sizeof(a));
	static unsigned seq = 1;
	seq = seq * 1664525u + 1013904223u;
	a.idleSeed = ((seq >> 8) & 0xffffff) / 16777216.f;
	if (a.idleSeed < 0.05f) a.idleSeed = 0.05f;
	a.idleOfs = a.idleSeed * 1.6f;
	a.model = -1;
	a.hair = 0;
	a.scale = 1;
	a.htScale = 1.f;
	a.tint = 0xFFFFFFFF;
	a.height = 1.f;
	a.clip = CL_IDLE; a.prev = CL_IDLE;
	a.loop = a.ploop = 1;
	a.blend = 1; a.blendDur = .15f;
	a.animSpeed = 1;
}

static void AddWeapons(Actor& a, const Renderer& r, int wl)
{
	switch (wl) {
	case WL_SWORD_SHIELD: AddAtt(a, r, "1H_Sword"); AddAtt(a, r, "Round_Shield"); break;
	case WL_AXE2: AddAtt(a, r, "2H_Axe"); break;
	case WL_CROSSBOW2: AddAtt(a, r, "2H_Crossbow"); break;
	case WL_KNIVES: AddAtt(a, r, "Knife"); AddAtt(a, r, "Knife_Offhand"); break;
	case WL_STAFF: AddAtt(a, r, "2H_Staff"); break;
	case WL_WAND_BOOK: AddAtt(a, r, "1H_Wand"); AddAtt(a, r, "Spellbook"); break;
	case WL_SWORD_BADGE: AddAtt(a, r, "1H_Sword"); AddAtt(a, r, "Badge_Shield"); break;
	case WL_SWORD_BOOK: AddAtt(a, r, "1H_Sword"); AddAtt(a, r, "Spellbook_open"); break;
	case WL_AXES: AddAtt(a, r, "1H_Axe"); AddAtt(a, r, "1H_Axe_Offhand"); break;
	case WL_SWORD2: AddAtt(a, r, "2H_Sword"); break;
	case WL_CROSS_KNIFE: AddAtt(a, r, "1H_Crossbow"); AddAtt(a, r, "Knife_Offhand"); break;
	case WL_SWORD_SPIKE: AddAtt(a, r, "1H_Sword"); AddAtt(a, r, "Spike_Shield"); break;
	}
}

void ActorFromChar(Actor& a, const Library& lib, const Renderer& r, const CharData& c)
{
	ActorInit(a);
	a.gait = GAIT_BIPED;
	a.body = CharBody(c);
	a.model = lib.bodyModel[a.body];
	if (a.model < 0) return;
	a.rig = r.models[a.model].rig;
	a.female = c.gender == 1;
	a.scale = a.female ? .40f : .43f;
	{
		char humName[16];
		sprintf_s(humName, a.female ? "HumF%d" : "HumM%d", CharModel(c));
		int hi = r.FindModel(humName);
		if (hi < 0) {
			const char* old = a.female ? ((a.body == B_MAGE) ? "HumWitch" : "HumMedieval") : ((a.body == B_KNIGHT) ? "HumKing" : "HumAdventurer");
			hi = r.FindModel(old);
		}
		if (hi >= 0 && r.models[hi].nJ > 8) {
			a.proc = 3;
			a.model = hi;
			a.rig = r.models[hi].rig;
			a.scale = .92f;
			a.mods.Reset(r.models[hi].nJ);
		}
	}
	{
		int tier = ClassInfo(c.cls).tier;
		if (tier > 1) a.scale *= 1.f + (tier - 1) * .02f;
		if (tier >= 5) a.hue += .04f * (tier - 4);
	}
	a.hue = OutfitHue(CharOutfit(c));
	a.face = CharFace(c);
	a.avatar = CharAvatar(c);
	if (a.proc != 3 || a.avatar > r.AvatarCount(a.female ? 1 : 0)) a.avatar = 0;
	a.bust = a.female ? CharBust(c) : 2;
	a.hairCol = HairColor(c.hairColor);
	a.hair = Clampi(c.hair, 0, HAIR_STYLES - 1);
	if (!a.avatar) {
		if (a.body == B_ROGUE_HOOD) a.hair = 0;
		if (c.hat && a.body == B_KNIGHT) a.hair = 0;
	}
	const ClassDef& cd = ClassInfo(c.cls);
	const GearDef* gw = GearOf(c.reserved[6]);
	const GearDef* ga = GearOf(c.reserved[7]);
	auto gearMeshOk = [&](int m) { return m >= 0 && m < r.GearCount(); };
	auto addGearMesh = [&](int mesh, int jointKind) {
		if (!gearMeshOk(mesh) || a.nGear >= 4 || a.rig < 0) return;
		int j = -1;
		if (jointKind == 2) j = r.models[a.model].headJ;
		else if (jointKind == 3) j = r.models[a.model].chestJ;
		else j = r.rigs[a.rig].Find(jointKind == 1 ? "handslot.l" : "handslot.r");
		if (j < 0 && jointKind <= 1) j = r.rigs[a.rig].Find(jointKind == 1 ? "Hand.L" : "Hand.R");
		if (j < 0 && jointKind == 3) j = r.rigs[a.rig].Find("chest");
		if (j < 0 && jointKind == 2) j = r.rigs[a.rig].Find("head");
		if (j < 0) return;
		a.gearM[a.nGear] = mesh;
		a.gearJ[a.nGear] = j;
		a.nGear++;
	};
	auto addGearPiece = [&](const GearDef* g, int slot) {
		if (!g) return;
		const char* att = slot ? g->att1 : g->att0;
		int mesh = slot ? g->mesh1 : g->mesh0;
		int joint = slot ? g->joint1 : g->joint0;
		if (att && att[0]) AddAtt(a, r, att);
		else addGearMesh(mesh, joint);
	};
	const char* humName = (a.proc == 3 && a.model >= 0 && a.model < (int)r.models.size()) ? r.models[a.model].name : "";
	BOOL builtInHat = strncmp(humName, "HumWitch", 8) == 0 || strncmp(humName, "HumKing", 7) == 0;
	auto isHeadGear = [](const char* att, int joint) {
		if (joint == 2) return true;
		return att && (strstr(att, "Helmet") || strstr(att, "Hat") || strstr(att, "Crown"));
	};
	bool customWep = gw && (gw->att0 || gw->att1 || gearMeshOk(gw->mesh0) || gearMeshOk(gw->mesh1));
	if (customWep) { addGearPiece(gw, 0); addGearPiece(gw, 1); }
	else AddWeapons(a, r, cd.weaponLook);
	bool namedHead = ga && ga->att1 && (strstr(ga->att1, "Helmet") || strstr(ga->att1, "Hat") || strstr(ga->att1, "Crown"));
	bool helm = namedHead || (ga && ga->joint1 == 2 && gearMeshOk(ga->mesh1));
	bool capeOn = ga && ga->att1 && strstr(ga->att1, "Cape");
	if (c.hat && !helm && !builtInHat) {
		static const char* const hats[B_COUNT] = { "Knight_Helmet", "Barbarian_Hat", "Mage_Hat", NULL, NULL };
		if (hats[a.body]) AddAtt(a, r, hats[a.body]);
	}
	if (c.cape && !capeOn && !a.avatar) {
		static const char* const capes[B_COUNT] = { "Knight_Cape", "Barbarian_Cape", "Mage_Cape", "Rogue_Cape", "Rogue_Cape" };
		AddAtt(a, r, capes[a.body]);
	}
	a.wepAtt = a.nAtt;
	a.wepGear = a.nGear;
	// アバターの服を着ているあいだは、兜・鎧・体型マントを重ねない。帽子とマントの切替は残す。
	if (ga && !a.avatar) {
		if (!(builtInHat && isHeadGear(ga->att0, ga->joint0))) addGearPiece(ga, 0);
		if (!(builtInHat && isHeadGear(ga->att1, ga->joint1))) addGearPiece(ga, 1);
	}
	if (c.cls == C_SKYKNIGHT) a.wings = 1;
	if (c.cls == C_DRAGONLORD) a.wings = 2;
	if (cd.tier >= 3) a.scale *= 1.06f;
	const Model& m = r.models[a.model];
	a.mods.Reset(m.nJ);
	if (a.female && m.rig >= 0) {
		const Rig& g = r.rigs[m.rig];
		static const struct { const char* n; float x, y, z; } kOwn[] = {
			{ "chest", .82f, 1.f, .86f }, { "spine", .8f, 1.f, .86f }, { "hips", 1.16f, 1.f, 1.08f },
			{ "upperarm.l", .78f, 1.f, .78f }, { "upperarm.r", .78f, 1.f, .78f },
			{ "lowerarm.l", .8f, 1.f, .8f }, { "lowerarm.r", .8f, 1.f, .8f },
			{ "upperleg.l", .96f, 1.f, .96f }, { "upperleg.r", .96f, 1.f, .96f },
			{ "lowerleg.l", .86f, 1.f, .86f }, { "lowerleg.r", .86f, 1.f, .86f },
		};
		for (int k = 0; k < (int)(sizeof(kOwn) / sizeof(kOwn[0])) && !a.avatar; k++) {
			int j = g.Find(kOwn[k].n);
			if (j >= 0 && j < MAXJ) a.mods.own[j] = v3(kOwn[k].x, kOwn[k].y, kOwn[k].z);
		}
		int hd = g.Find("head");
		if (hd >= 0 && hd < MAXJ) a.mods.scale[hd] = v3(1.06f, 1.06f, 1.06f);
		a.mods.any = 1;
	}
	{
		static const float kHt[HT_N] = { .86f, .93f, 1.f, 1.08f, 1.16f };
		a.htScale = kHt[CharHt(c)];
		a.avaTint = AvaClothTint(CharAvaCol(c));
		// 人間は CS が身長を掛ける。手続きメッシュはワールド倍率のまま。
		if (a.proc != 3) a.scale *= a.htScale;
	}
	if (m.rig >= 0 && m.rig < (int)r.rigs.size()) {
		const Rig& g = r.rigs[m.rig];
		auto mulOwn = [&](const char* n, float x, float y, float z) {
			int j = g.Find(n);
			if (j < 0 || j >= MAXJ) return;
			a.mods.own[j].x *= x; a.mods.own[j].y *= y; a.mods.own[j].z *= z;
			a.mods.any = 1;
		};
		int fig = CharFig(c);
		const int woman = a.female ? 1 : 0;
		static const float kBody[2][FIG_N] = {
			{ 0.90f, 0.98f, 1.08f, 1.18f, 1.30f },
			{ 0.86f, 0.94f, 1.04f, 1.14f, 1.24f },
		};
		static const float kWaist[2][FIG_N] = {
			{ 0.92f, 0.98f, 1.06f, 1.14f, 1.22f },
			{ 0.70f, 0.80f, 0.92f, 1.04f, 1.16f },
		};
		static const float kArm[2][FIG_N] = {
			{ 0.84f, 0.94f, 1.08f, 1.18f, 1.32f },
			{ 0.76f, 0.86f, 0.96f, 1.06f, 1.14f },
		};
		static const float kLeg[2][FIG_N] = {
			{ 0.90f, 0.98f, 1.06f, 1.12f, 1.18f },
			{ 0.80f, 0.88f, 0.98f, 1.06f, 1.14f },
		};
		float bd = kBody[woman][fig], ws = kWaist[woman][fig], lm = kArm[woman][fig], lg = kLeg[woman][fig];
		float chest = woman ? bd * 0.98f : bd * 1.06f;
		mulOwn("Hips", bd, 1.f, bd * (woman ? 1.04f : 1.f)); mulOwn("hips", bd, 1.f, bd * (woman ? 1.04f : 1.f));
		mulOwn("Chest", chest, 1.f, bd); mulOwn("chest", chest, 1.f, bd);
		mulOwn("Abdomen", ws, 1.f, ws * 0.96f); mulOwn("abdomen", ws, 1.f, ws * 0.96f);
		mulOwn("Torso", ws, 1.f, ws); mulOwn("torso", ws, 1.f, ws);
		mulOwn("Spine", ws, 1.f, ws * 0.94f); mulOwn("spine", ws, 1.f, ws * 0.94f);
		mulOwn("UpperLeg.L", lg, 1.f, lg); mulOwn("upperleg.l", lg, 1.f, lg);
		mulOwn("UpperLeg.R", lg, 1.f, lg); mulOwn("upperleg.r", lg, 1.f, lg);
		mulOwn("LowerLeg.L", lg, 1.f, lg); mulOwn("lowerleg.l", lg, 1.f, lg);
		mulOwn("LowerLeg.R", lg, 1.f, lg); mulOwn("lowerleg.r", lg, 1.f, lg);
		mulOwn("UpperArm.L", lm, 1.f, lm); mulOwn("upperarm.l", lm, 1.f, lm);
		mulOwn("UpperArm.R", lm, 1.f, lm); mulOwn("upperarm.r", lm, 1.f, lm);
		mulOwn("LowerArm.L", lm, 1.f, lm); mulOwn("lowerarm.l", lm, 1.f, lm);
		mulOwn("LowerArm.R", lm, 1.f, lm); mulOwn("lowerarm.r", lm, 1.f, lm);
		if (a.female && !a.avatar) {
			static const float kBx[BUST_N] = { .94f, .98f, 1.04f, 1.10f, 1.16f };
			static const float kBz[BUST_N] = { .96f, 1.00f, 1.06f, 1.12f, 1.20f };
			int b = CharBust(c);
			mulOwn("Chest", kBx[b], 1.04f + (b - 2) * .03f, kBz[b]);
			mulOwn("chest", kBx[b], 1.04f + (b - 2) * .03f, kBz[b]);
		}
	}
	a.height = (a.proc == 3 ? 1.75f : 2.35f) * a.scale * (a.proc == 3 && a.htScale > 0.05f ? a.htScale : 1.f);
	a.hover = a.wings ? .35f : 0;
	if (a.proc == 3 && a.rig >= 0 && a.rig < (int)r.rigs.size()) {
		int hj = r.rigs[a.rig].Find("Hair");
		if (hj >= 0 && hj < MAXJ) {
			static const float kHair[HAIR_STYLES][3] = {
				{ 1.00f, 0.14f, 1.00f },
				{ 0.96f, 0.12f, 0.96f },
				{ 1.02f, 0.88f, 1.02f },
				{ 0.72f, 0.92f, 1.08f },
				{ 1.32f, 0.70f, 0.86f },
				{ 1.08f, 0.16f, 1.04f },
				{ 1.00f, 0.95f, 1.00f },
				{ 1.04f, 1.08f, 1.04f },
				{ 1.00f, 1.00f, 1.00f },
				{ 1.00f, 1.00f, 1.00f },
				{ 1.00f, 1.00f, 1.00f },
				{ 1.00f, 1.00f, 1.00f },
			};
			int s = Clampi(a.hair, 0, HAIR_STYLES - 1);
			a.mods.scale[hj] = v3(kHair[s][0], kHair[s][1], kHair[s][2]);
			a.mods.any = 1;
		}
	}
}

void ActorFromMonster(Actor& a, const Library& lib, const Renderer& r, int monster, int boss)
{
	ActorInit(a);
	a.gait = MonsterGait(monster);
	const MonsterDef& md = MonsterInfo(monster);
	a.look = md.look;
	DWORD t = md.tint;
	a.tint = RGBA8((t >> 16) & 255, (t >> 8) & 255, t & 255);
	float bs = boss ? 1.15f : 1.f;
	if (md.look == LK_MESH) {
		int mi = r.CreatureModel(md.model);
		a.model = md.model;
		a.scale = md.scale * bs;
		a.height = 1.7f * a.scale;
		a.hover = md.moveType == MV_FLY ? .4f : 0;
		if (mi >= 0) {
			a.proc = 3;
			a.model = mi;
			a.rig = r.models[mi].rig;
			a.mods.Reset(r.models[mi].nJ);
		} else {
			a.proc = 2;
			a.model = -1;
			a.rig = -1;
		}
		return;
	}
	if (md.look == LK_KAYKIT) {
		int mi = md.model;
		int body = -1;
		if (mi >= 100 && mi < 104) mi = lib.skelModel[mi - 100];
		else if (mi >= 104 && mi < 104 + B_COUNT) { body = mi - 104; mi = lib.bodyModel[body]; }
		a.model = (mi >= 0 && mi < (int)r.models.size()) ? mi : lib.skelModel[1];
		a.rig = r.models[a.model].rig;
		a.scale = .43f * md.scale * bs;
		if (body >= 0) {
			AddWeapons(a, r, md.weaponLook);
			if (md.moveType == MV_FLY) { a.wings = 1; a.hover = .35f; }
		} else switch (md.weaponLook) {
		case 1: AddAtt(a, r, "Skeleton_Blade"); break;
		case 2: AddAtt(a, r, "Skeleton_Axe"); AddAtt(a, r, "Skeleton_Shield_Small_A"); break;
		case 3: AddAtt(a, r, "Skeleton_Crossbow"); AddAtt(a, r, "Skeleton_Rogue_Hood"); AddAtt(a, r, "Skeleton_Rogue_Cape"); break;
		case 4: AddAtt(a, r, "Skeleton_Staff"); AddAtt(a, r, "Skeleton_Mage_Hat"); break;
		case 5: AddAtt(a, r, "Skeleton_Blade"); AddAtt(a, r, "Skeleton_Shield_Large_A"); AddAtt(a, r, "Skeleton_Warrior_Helmet"); break;
		}
		a.height = 2.3f * a.scale;
		a.mods.Reset(r.models[a.model].nJ);
	} else {
		a.proc = 1;
		a.model = lib.procModel[md.look];
		a.rig = -1;
		a.scale = md.scale * bs;
		static const float H[5] = { 1, .62f, 1.45f, 1.75f, 1.65f };
		a.height = H[md.look] * a.scale;
		if (a.model >= 0) a.mods.Reset(r.models[a.model].nJ);
	}
}

void ActorPlay(Actor& a, int clip, int loop, float blendTime)
{
	if (clip == a.clip && loop && a.loop) return;
	a.prev = a.clip; a.pt = a.t; a.ploop = a.loop;
	a.clip = clip; a.t = 0; a.loop = loop;
	a.blendDur = max(.001f, blendTime);
	a.blend = blendTime <= 0 ? 1.f : 0.f;
	if (loop && clip == CL_IDLE && a.proc == 3) a.t = a.idleOfs;
}

void ActorUpdate(Actor& a, float dt)
{
	a.t += dt * a.animSpeed;
	a.pt += dt * a.animSpeed;
	if (a.blend < 1.f) a.blend = min(1.f, a.blend + dt / a.blendDur);
}

static int MonsterClip(const Renderer& r, int rig, int clip)
{
	if (rig < 0 || rig >= (int)r.rigs.size()) return -1;
	const Rig& g = r.rigs[rig];
	int want = clip;
	if (want == CL_DODGE || want == CL_HIT_B) want = CL_HIT_A;
	if (want == CL_THROW) want = CL_1H_SHOOT;
	if (want == CL_PUNCH) want = CL_1H_STAB;
	if (want == CL_CAST_LONG) want = CL_CAST_SHOOT;
	int c = (want >= 0 && want < CL_COUNT) ? g.FindClip(kClipNames[want]) : -1;
	if (c >= 0) return c;
	switch (want) {
	case CL_WALK: case CL_RUN: c = g.FindClip("Walking_A"); break;
	case CL_1H_CHOP: case CL_1H_SLICE: case CL_1H_STAB: case CL_2H_CHOP: case CL_2H_SPIN:
	case CL_1H_SHOOT: case CL_2H_SHOOT: case CL_CAST_SHOOT: case CL_CAST_RAISE: case CL_CAST_LONG:
		c = g.FindClip("1H_Melee_Attack_Slice_Diagonal");
		if (c < 0) c = g.FindClip("1H_Melee_Attack_Chop");
		break;
	default: c = g.FindClip("Idle"); break;
	}
	if (c < 0) c = g.FindClip("Idle");
	return c;
}

float ActorClipLen(const Actor& a, const Library& lib, const Renderer& r, int clip)
{
	if (a.proc == 3) {
		if (a.model < 0 || a.model >= (int)r.models.size()) return 1.f;
		int ci = MonsterClip(r, r.models[a.model].rig, clip);
		if (ci < 0) return 1.f;
		return r.rigs[r.models[a.model].rig].clips[ci].Duration();
	}
	if (a.proc) return ProcLen(ProcKind(clip));
	if (a.rig < 0 || a.rig >= (int)r.rigs.size()) return 1.f;
	int ci = lib.clipMap[a.rig][clip];
	if (ci < 0) return 1.f;
	return r.rigs[a.rig].clips[ci].Duration();
}

BOOL ActorClipDone(const Actor& a, const Library& lib, const Renderer& r)
{
	return !a.loop && a.t >= ActorClipLen(a, lib, r, a.clip);
}

void ActorPose(Actor& a, const Library& lib, const Renderer& r)
{
	a.poseValid = 0;
	if (a.proc == 3 && a.loop && a.model >= 0 && (a.clip == CL_IDLE || (a.clip >= CL_IDLE_GLANCE_L && a.clip <= CL_IDLE_CROSS))) {
		float len = ActorClipLen(a, lib, r, a.clip);
		if (len > 0.05f && a.t >= len) {
			int next = CL_IDLE;
			if (a.clip == CL_IDLE) {
				a.idleSeed = fmodf(a.idleSeed * 12.9898f + 0.173f, 1.f);
				if (a.idleSeed < 0.f) a.idleSeed += 1.f;
				float s = a.idleSeed;
				if (s < 0.55f) next = CL_IDLE;
				else if (s < 0.64f) next = CL_IDLE_GLANCE_L;
				else if (s < 0.73f) next = CL_IDLE_GLANCE_R;
				else if (s < 0.82f) next = CL_IDLE_LOOK;
				else if (s < 0.90f) next = CL_IDLE_STRETCH;
				else if (s < 0.96f) next = CL_IDLE_SHIFT;
				else next = CL_IDLE_CROSS;
			}
			if (next == a.clip) a.t = fmodf(a.t, len);
			else ActorPlay(a, next, 1, .35f);
		}
	}
	if (a.proc == 3) {
		if (a.model < 0 || a.model >= (int)r.models.size()) return;
		const Model& m = r.models[a.model];
		a.modelPtr = &m;
		int ca = MonsterClip(r, m.rig, a.clip);
		int cb = MonsterClip(r, m.rig, a.prev);
		r.EvalPose(m, ca, a.t, a.loop, cb, a.pt, a.ploop, a.blend, &a.mods, a.pose);
		a.poseValid = 1;
		return;
	}
	if (a.proc == 2) { a.poseValid = 1; return; }
	if (a.model < 0 || a.model >= (int)r.models.size()) return;
	const Model& m = r.models[a.model];
	a.modelPtr = &m;
	if (a.proc) {
		Q4 rot[MAXJ], rotB[MAXJ];
		V3 tr[MAXJ], trB[MAXJ];
		PoseMods pm, pmB;
		ProcAnim(a.look, a.clip, a.t, rot, tr, pm, m.nJ);
		if (a.blend < 1.f) {
			ProcAnim(a.look, a.prev, a.pt, rotB, trB, pmB, m.nJ);
			for (int j = 0; j < m.nJ; j++) {
				rot[j] = QNlerp(rotB[j], rot[j], a.blend);
				tr[j] = Lerp(trB[j], tr[j], a.blend);
				pm.scale[j] = Lerp(pmB.scale[j], pm.scale[j], a.blend);
			}
			pm.any |= pmB.any;
		}
		r.EvalPoseLocal(m, rot, tr, &pm, a.pose);
	} else {
		if (a.rig < 0 || a.rig >= 4) return;
		int ca = lib.clipMap[a.rig][a.clip], cb = lib.clipMap[a.rig][a.prev];
		r.EvalPose(m, ca, a.t, a.loop, cb, a.pt, a.ploop, a.blend, &a.mods, a.pose);
	}
	a.poseValid = 1;
}

M4 ActorWorld(const Actor& a, V3 pos, float face)
{
	float lean = -a.recoil * .72f;
	return MMul(MMul(MMul(MScale(a.scale, a.scale, a.scale), MRotX(lean)), MRotY(face + PI)), MTrans(pos.x, pos.y, pos.z));
}

V3 ActorJointWorld(const Actor& a, const M4& world, int which)
{
	float hl = a.height / max(.01f, a.scale);
	V3 fallback = MXformPt(world, v3(0, hl * (which == 0 ? .85f : .55f), 0));
	if (!a.poseValid || a.model < 0) return fallback;
	int j = -1;
	if (a.proc == 3 && a.modelPtr) {
		const Model& hm = *(const Model*)a.modelPtr;
		j = which == 0 ? hm.headJ : (which == 1 ? hm.handR : hm.chestJ);
	} else if (a.proc) {
		static const int J[5][3] = { { 0, 0, 0 }, { SJ_TOP, SJ_TOP, SJ_BODY }, { BJ_HEAD, BJ_HEAD, BJ_BODY }, { WJ_HEAD, WJ_JAW, WJ_BODY }, { GJ_HEAD, GJ_FR, GJ_TORSO } };
		j = J[Clampi(a.look, 0, 4)][Clampi(which, 0, 2)];
	} else {
		const Model& m = *(const Model*)a.modelPtr;
		j = which == 0 ? m.headJ : (which == 1 ? m.handR : m.chestJ);
	}
	if (j < 0 || j >= a.pose.nJ) return fallback;
	M4 g = MMul(a.pose.global[j], world);
	V3 p = v3(g.m[12], g.m[13], g.m[14]);
	if (which == 0) p.y += a.height * (a.proc == 3 ? .04f : .18f);
	return p;
}

int AttackClipFor(int style, int variant)
{
	switch (style) {
	case AS_SLASH: return variant ? CL_1H_CHOP : CL_1H_SLICE;
	case AS_CHOP2: return variant ? CL_2H_SPIN : CL_2H_CHOP;
	case AS_STAB: return variant ? CL_1H_SLICE : CL_1H_STAB;
	case AS_SHOOT: return CL_2H_SHOOT;
	case AS_SHOOT1: return variant ? CL_THROW : CL_1H_SHOOT;
	case AS_CAST: return CL_CAST_SHOOT;
	case AS_SPIN: return CL_2H_SPIN;
	case AS_PUNCH: return CL_PUNCH;
	case AS_BITE: return CL_1H_STAB;
	}
	return CL_1H_SLICE;
}

int CastClipFor(int kind)
{
	if (kind == SK_HEAL || kind == SK_BUFF) return CL_CAST_RAISE;
	return CL_CAST_SHOOT;
}

static M4 LocoXform(const Actor& a, float time)
{
	BOOL moving = a.clip == CL_WALK || a.clip == CL_RUN;
	float clk = moving ? a.t : time * .35f;
	if (!moving) {
		float bob = sinf(time * 2.2f) * (a.hover > 0.f || a.gait == GAIT_FLY ? .08f : .02f);
		return MTrans(0, bob, 0);
	}
	switch (a.gait) {
	case GAIT_TROT: {
		float s = sinf(clk * 14.f);
		return MMul(MRotX(s * .22f), MTrans(0, fabsf(s) * .16f, 0));
	}
	case GAIT_BOUNCE: {
		float s = sinf(clk * 7.f);
		float ysc = 1.f + (s > 0.f ? s * .2f : s * .34f);
		float xz = 1.f / sqrtf(max(.25f, ysc));
		return MMul(MScale(xz, ysc, xz), MTrans(0, max(0.f, s) * .34f, 0));
	}
	case GAIT_FLY: {
		float s = sinf(clk * 12.f);
		return MMul(MRotX(s * .16f), MTrans(0, s * .1f, 0));
	}
	case GAIT_SCUTTLE: {
		float s = sinf(clk * 18.f);
		return MMul(MRotZ(s * .1f), MTrans(s * .05f, fabsf(s) * .06f, 0));
	}
	case GAIT_SLITHER: {
		float s = sinf(clk * 8.f);
		return MMul(MRotY(s * .28f), MTrans(s * .14f, fabsf(cosf(clk * 8.f)) * .04f, 0));
	}
	case GAIT_WADDLE: {
		float s = sinf(clk * 10.f);
		return MMul(MRotZ(s * .18f), MTrans(0, fabsf(s) * .1f, 0));
	}
	case GAIT_STOMP: {
		float s = sinf(clk * 6.f);
		float step = max(0.f, s);
		return MMul(MRotX(step * .14f), MTrans(0, step * .12f, 0));
	}
	case GAIT_FISH: {
		float s = sinf(clk * 11.f);
		return MMul(MRotY(s * .24f), MTrans(s * .07f, sinf(clk * 5.f) * .05f, 0));
	}
	default: {
		float s = sinf(clk * 10.f);
		return MMul(MRotZ(s * .03f), MTrans(0, fabsf(s) * .03f, 0));
	}
	}
}

static BOOL NameHas(const char* n, const char* k) { return n && k && strstr(n, k) != NULL; }

static M4 MBasis(V3 x, V3 y, V3 z, V3 t)
{
	M4 r = MIdent();
	r.m[0] = x.x; r.m[1] = x.y; r.m[2] = x.z;
	r.m[4] = y.x; r.m[5] = y.y; r.m[6] = y.z;
	r.m[8] = z.x; r.m[9] = z.y; r.m[10] = z.z;
	r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
	return r;
}

static void MakeBasis(V3 y, V3 z, V3& x, V3& oy, V3& oz)
{
	oy = Norm(y);
	oz = Norm(z);
	x = Norm(Cross(oy, oz));
	if (Len(x) < 1e-4f) x = Norm(Cross(oy, v3(1, 0, 0)));
	oz = Norm(Cross(x, oy));
}

// 装備を関節ローカルではなく、顔・上・手のひら基準で置く。
static M4 FitWorld(const M4& g, V3 y, V3 z, V3 at, float sx, float sy, float sz)
{
	V3 x, oy, oz;
	MakeBasis(y, z, x, oy, oz);
	return MMul(MScale(sx, sy, sz), MMul(MBasis(x, oy, oz, at), MInverse(g)));
}

static M4 HumanGearFit(const Pose& pose, int joint, const char* name, const char* jointName, const M4& bindG, const M4* armorD = NULL, BOOL bowMesh = FALSE, const V3* gripLocal = NULL, BOOL neo = FALSE)
{
	if (joint < 0 || joint >= pose.nJ) return MScale(0.4f, 0.4f, 0.4f);
	const M4& g = pose.global[joint];
	V3 pos = v3(g.m[12], g.m[13], g.m[14]);
	V3 up = v3(0, 1, 0);
	V3 face = v3(0, 0, -1);
	V3 back = v3(0, 0, 1);
	BOOL hat = NameHas(name, "Hat") || NameHas(name, "Helmet");
	BOOL crown = NameHas(name, "Crown") || (!name && (NameHas(jointName, "Head") || NameHas(jointName, "head")));
	BOOL cape = NameHas(name, "Cape");
	BOOL armor = NameHas(name, "Armor") || NameHas(name, "Armour");
	BOOL shield = NameHas(name, "Shield");
	BOOL book = NameHas(name, "Book") || NameHas(name, "book");
	BOOL head = NameHas(jointName, "Head") || NameHas(jointName, "head");
	BOOL chest = NameHas(jointName, "Chest") || NameHas(jointName, "chest");
	BOOL hand = NameHas(jointName, "Hand") || NameHas(jointName, "hand");
	if (hat || crown || (head && !hand)) {
		float sc = hat ? 0.20f : 0.30f;
		float lift = hat ? 0.10f : 0.08f;
		// 新しい頭は小さく、王冠の底が関節より 19cm 上に作ってある。頭頂は関節の約 20cm 上。
		if (neo) { sc = hat ? 0.10f : 0.18f; lift = hat ? 0.02f : 0.f; }
		M4 place = FitWorld(g, up, back, pos + up * lift, sc, sc, sc);
		if (neo && crown && !hat) return MMul(MRotX(-1.5707963f), place);
		return place;
	}
	// バインド姿勢のモデル空間へ置いてから、関節の動きだけ乗せる。
	auto onBind = [&](const M4& modelSpace) { return MMul(modelSpace, MInverse(bindG)); };
	if (cape) {
		// 読込が Z を反転する。マントは幅 ±0.49、上端 y=0.26、布は z=0.04〜0.39（+Z＝背中）。
		// 回すと腹の前に出る。肩幅へ縮め、上端を首、体の直後へ。
		const float s = 0.50f;
		M4 D = MMul(MScale(s, s, s), MTrans(0.f, 1.09f, 0.07f));
		return onBind(D);
	}
	if (armor || (chest && !hand)) {
		// 鎧は立った向きのまま（胸が -Z、首穴が +Y、腕穴が ±X）。体の骨から胴の箱を作って収める。
		if (armorD) return onBind(*armorD);
		M4 D = MMul(MScale(0.20f, 0.36f, 0.36f), MTrans(0.f, 1.10f, -0.05f));
		return onBind(D);
	}
	V3 out = v3(bindG.m[12], 0, bindG.m[14]);
	if (Len(out) < 1e-4f) out = v3(bindG.m[12] >= 0.f ? 1.f : -1.f, 0, 0);
	out = Norm(out);
	V3 along = Norm(v3(g.m[4], g.m[5], g.m[6]));   // 手首から指先
	V3 dorsal = Norm(v3(g.m[8], g.m[9], g.m[10])); // +Z は手の甲
	// 甲の上ではなく、曲がった指の中（掌側）に柄を通す。拳の大きさは体型で違うので中指の関節から取る。
	V3 grip = gripLocal ? MXformPt(g, *gripLocal) - dorsal * 0.01f : pos + along * 0.135f - dorsal * 0.045f;
	V3 palm = pos + along * 0.07f;
	// 静止姿勢での向きを手の骨に乗せて運ぶ。振りかぶれば刃も手首と一緒に回る。
	const V3 bx = Norm(v3(bindG.m[0], bindG.m[1], bindG.m[2])), by = Norm(v3(bindG.m[4], bindG.m[5], bindG.m[6])), bz = Norm(v3(bindG.m[8], bindG.m[9], bindG.m[10]));
	const V3 cx = Norm(v3(g.m[0], g.m[1], g.m[2])), cy = along, cz = dorsal;
	auto carry = [&](V3 w) { return cx * Dot(bx, w) + cy * Dot(by, w) + cz * Dot(bz, w); };
	if (shield) return FitWorld(g, carry(up), carry(back), palm + carry(out) * 0.05f, 0.28f, 0.28f, 0.28f);
	if (book) {
		if (neo) {
			// 底が原点。指の方向へ背を立て、厚みは掌側。向きは静止の手で決め、振りは関節が運ぶ。
			// 今の手の軸で毎フレーム向け直すと、待機で本が腰の横へ寝て板になる。
			const float sc = 0.15f;
			V3 bpos = v3(bindG.m[12], bindG.m[13], bindG.m[14]);
			V3 balong = Norm(v3(bindG.m[4], bindG.m[5], bindG.m[6]));
			V3 bdorsal = Norm(v3(bindG.m[8], bindG.m[9], bindG.m[10]));
			V3 hold = bpos + balong * 0.02f - bdorsal * 0.012f;
			return FitWorld(bindG, balong, bdorsal * -1.f, hold, sc * 0.75f, sc, sc * 0.42f);
		}
		return FitWorld(g, carry(up), carry(face), palm, 0.40f, 0.40f, 0.40f);
	}
	if (neo && NameHas(name, "Wand")) {
		V3 bpos = v3(bindG.m[12], bindG.m[13], bindG.m[14]);
		V3 balong = Norm(v3(bindG.m[4], bindG.m[5], bindG.m[6]));
		V3 bdorsal = Norm(v3(bindG.m[8], bindG.m[9], bindG.m[10]));
		return FitWorld(bindG, balong, up, bpos + balong * 0.04f - bdorsal * 0.015f, 0.22f, 0.22f, 0.22f);
	}
	if (bowMesh) {
		// 弓は握りの線（拳の指の付け根の並び）に沿って立て、弦（メッシュ +X）を射手側へ。握りは弓の中央。
		V3 y = Norm(carry(face)), x = Norm(carry(up));
		return FitWorld(g, y, Cross(x, y), grip - y * 0.09f, 0.36f, 0.36f, 0.36f);
	}
	if (NameHas(name, "Crossbow")) {
		// 弩は銃床を前腕に沿わせる。下げた手では下を、構えた腕では前を向く。
		V3 down = Norm(by - face * Dot(by, face));
		return FitWorld(g, carry(down), carry(face), grip, 0.36f, 0.36f, 0.36f);
	}
	// 剣の柄は掌を斜めに横切る。腕を前へ出すと刃は前上がりになる。
	return FitWorld(g, Norm(carry(Norm(face + v3(0, -0.6f, 0)))), carry(up), grip, 0.36f, 0.36f, 0.36f);
}

void ActorDraw(Renderer& r, const Library& lib, Actor& a, const M4& world, const DrawParams& dp, float time)
{
	if (a.proc == 3) {
		if (!a.poseValid) ActorPose(a, lib, r);
		if (!a.poseValid) return;
		DrawParams d = dp;
		BOOL hum = a.model >= 0 && a.model < (int)r.models.size() && strncmp(r.models[a.model].name, "Hum", 3) == 0;
		if (hum) {
			d.outline = 0;
			// 作成画面のリム 0.55 を足すと、順光 1.4 倍の上に白が乗り髪も鎧も金属になる。
			d.rim = 0.04f;
			// 服の色は染め色の頂点だけ。シェーダの色相回転は肌まで回すので使わない。
			d.hue = 0;
			d.clothHue = a.hue;
			d.face = a.face;
			// 唇の合わせ目と耳の内側は片面だと抜けて見える。
			d.twoSided = 1;
		}
		float hs = a.htScale > 0.05f ? a.htScale : 1.f;
		d.bodyScale = hs;
		// アバター色は着ている衣装だけ。素体へ渡すと肌まで染まる。
		d.clothTint = 0;
		// 体の頂点は CS が掛ける。武具と髪は同じ倍率をモデル原点から掛ける。
		M4 wGear = world;
		if (hs < 0.999f || hs > 1.001f) wGear = MMul(MScale(hs, hs, hs), world);
		const Model* humM = (hum && a.model >= 0 && a.model < (int)r.models.size()) ? &r.models[a.model] : NULL;
		BOOL digitBody = FALSE;
		if (humM && (strncmp(humM->name, "HumF", 4) == 0 || strncmp(humM->name, "HumM", 4) == 0) && humM->name[4] >= '0' && humM->name[4] <= '9') digitBody = TRUE;
		if (digitBody) {
			int ix = humM->name[4] - '0';
			if (ix >= 0 && ix <= 2) d.skinTone = ix + 1;
		}
		d.hideMeshHair = (hum && a.hair > 0) ? 1 : 0;
		BOOL ava = FALSE;
		// 白人の素体は裸体。性別を切り替えるとアバターは 0 に戻るので、未選択のときは既定の服を着せる。
		// 男性は 9 番（tucked shirt・ズボン・靴）、女性は 12 番（スーツ・ブーツ）。肩当てとマントは残す。
		int avaId = a.avatar;
		BOOL bareBase = FALSE;
		if (humM && avaId <= 0) {
			if (strcmp(humM->name, "HumM0") == 0) { avaId = 9; bareBase = TRUE; }
			else if (strcmp(humM->name, "HumF0") == 0) { avaId = 12; bareBase = TRUE; }
		}
		if (hum && avaId > 0) {
			d.clothTint = a.avaTint;
			ava = r.DrawAvatar(a.model, avaId, a.bust, a.pose, world, d);
		}
		if (!ava) {
			d.clothTint = 0;
			if (!hum) d.twoSided = 1;
			r.DrawSkinnedCreature(a.model, a.pose, world, d);
		}
		const int nAtt = (ava && !bareBase) ? a.wepAtt : a.nAtt, nGear = (ava && !bareBase) ? a.wepGear : a.nGear;
		DrawParams da = dp;
		if (hum) da.rim = 0.06f;
		auto bindOf = [&](int j) -> M4 {
			if (humM && j >= 0 && j < humM->nJ) return MInverse(humM->invBind[j]);
			return a.pose.global[j];
		};
		// 握りの中心＝その手の中指の関節の平均（手の骨の空間）。
		auto gripOf = [&](int j, const char* jn, V3& out) -> BOOL {
			if (!humM || !jn || j < 0 || j >= humM->nJ || a.rig < 0 || a.rig >= (int)r.rigs.size()) return FALSE;
			const char* dot = strrchr(jn, '.');
			if (!dot || !(NameHas(jn, "Hand") || NameHas(jn, "hand"))) return FALSE;
			const Rig& rg = r.rigs[a.rig];
			V3 s = v3(0, 0, 0);
			int n = 0;
			for (int k = 1; k <= 4; k++) {
				char nm[24];
				sprintf_s(nm, "Middle%d%s", k, dot);
				int mj = rg.Find(nm);
				if (mj < 0 || mj >= humM->nJ) continue;
				M4 b = MInverse(humM->invBind[mj]);
				s = s + v3(b.m[12], b.m[13], b.m[14]);
				n++;
			}
			if (n < 2) return FALSE;
			out = MXformPt(humM->invBind[j], s * (1.f / n));
			return TRUE;
		};
		auto bindPt = [&](const char* nm, V3& out) -> BOOL {
			if (!humM || a.rig < 0 || a.rig >= (int)r.rigs.size()) return FALSE;
			const Rig& rg = r.rigs[a.rig];
			for (int j = 0; j < rg.nJ && j < humM->nJ; j++) if (!strcmp(rg.names[j], nm)) {
				M4 b = MInverse(humM->invBind[j]);
				out = v3(b.m[12], b.m[13], b.m[14]);
				return TRUE;
			}
			return FALSE;
		};
		for (int i = 0; i < nAtt; i++) {
			const char* nm = r.attaches[a.att[i]].name;
			const char* jn = (hum && a.rig >= 0 && a.attJ[i] >= 0 && a.attJ[i] < r.rigs[a.rig].nJ) ? r.rigs[a.rig].names[a.attJ[i]] : NULL;
			if (hum) {
				V3 gl;
				BOOL hasGrip = gripOf(a.attJ[i], jn, gl);
				M4 fit = HumanGearFit(a.pose, a.attJ[i], nm, jn, bindOf(a.attJ[i]), NULL, FALSE, hasGrip ? &gl : NULL, digitBody);
				if (digitBody && nm && NameHas(nm, "Cape")) {
					V3 nk;
					if (bindPt("Neck", nk)) {
						// 布の上端 y=0.26 を首の高さへ。+Z が背中。
						const float s = 0.38f;
						M4 D = MMul(MScale(s, s, s), MTrans(0.f, nk.y - 0.26f * s, nk.z + 0.05f));
						fit = MMul(D, MInverse(bindOf(a.attJ[i])));
					}
				}
				DrawParams dc = da;
				if (NameHas(nm, "Cape")) dc.twoSided = 1;
				r.DrawAttach(r.attaches[a.att[i]], a.pose, a.attJ[i], wGear, dc, &fit);
			} else r.DrawAttach(r.attaches[a.att[i]], a.pose, a.attJ[i], wGear, da);
		}
		for (int i = 0; i < nGear; i++) if (a.gearJ[i] >= 0 && a.gearJ[i] < a.pose.nJ) {
			const char* jn = (hum && a.rig >= 0 && a.gearJ[i] < r.rigs[a.rig].nJ) ? r.rigs[a.rig].names[a.gearJ[i]] : NULL;
			if (hum) {
				M4 armorD;
				const M4* pd = NULL;
				V3 nk, ab, ua, ch;
				BOOL neoB = digitBody;
				if (jn && NameHas(jn, "Chest") && bindPt("Neck", nk) && bindPt("Abdomen", ab) && bindPt("UpperArm.R", ua) && bindPt("Chest", ch)) {
					// 襟は首の付け根、裾は腹、肩当ては上腕の外、胸板は胸骨の前。
					// メッシュは胴の柱が高さ 1・中心が原点。幅 1.52・奥行 0.81 の金属鎧を基準に、各鎧の形はそのまま。
					float top = nk.y + 0.025f, bot = ab.y;
					float sy = top - bot;
					float sx = 2.f * (fabsf(ua.x) + 0.13f) / 1.52f;
					float sz = 0.29f / 0.81f;
					if (a.female) {
						// 胸当ては胸の大きさに合わせて前へ張り出す（体の Chest 拡大と同じ段階）。
						static const float kArmZ[BUST_N] = { .92f, .97f, 1.06f, 1.22f, 1.40f };
						static const float kArmX[BUST_N] = { .97f, .99f, 1.02f, 1.06f, 1.10f };
						int b = Clampi(a.bust, 0, BUST_N - 1);
						sz *= kArmZ[b];
						sx *= kArmX[b];
					}
					// 新しい体は胸の厚みが半分ほど。肩の外側と胸の前に浮く分を胴へ戻す。
					if (neoB) {
						sx = 2.f * (fabsf(ua.x) + 0.02f) / 1.52f;
						sz = 0.20f / 0.81f;
						// 胸の骨より 11cm 前だと、鎧の殻が胴を包まず胸の前に立つ。
						armorD = MMul(MScale(sx, sy, sz), MTrans(0.f, (top + bot) * .5f, ch.z - 0.03f));
					} else armorD = MMul(MScale(sx, sy, sz), MTrans(0.f, (top + bot) * .5f, ch.z - 0.05f));
					pd = &armorD;
				}
				V3 gl;
				BOOL hasGrip = gripOf(a.gearJ[i], jn, gl);
				M4 fit = HumanGearFit(a.pose, a.gearJ[i], NULL, jn, bindOf(a.gearJ[i]), pd, r.GearIsBow(a.gearM[i]), hasGrip ? &gl : NULL, neoB);
				r.DrawGear(a.gearM[i], MMul(MMul(fit, a.pose.global[a.gearJ[i]]), wGear), da);
			} else r.DrawGear(a.gearM[i], MMul(a.pose.global[a.gearJ[i]], wGear), da);
		}
		const BOOL whiteBase = digitBody && humM && humM->name[4] == '0';
		if (hum && humM && humM->headJ >= 0 && (a.hair > 0 || whiteBase)) {
			DrawParams dh0 = dp;
			dh0.rim = 0.04f;
			dh0.hue = 0;
			dh0.outline = 0;
			dh0.twoSided = 1;
			dh0.sway = 0.f;
			dh0.emissive = 0.f;
			{
				int al = (dp.tint >> 24) & 255;
				auto lit = [](int c) { int v = (int)(c / 1.42f); return v < 0 ? 0 : (v > 255 ? 255 : v); };
				int hr = lit((a.hairCol & 255) * (dp.tint & 255) / 255);
				int hg = lit(((a.hairCol >> 8) & 255) * ((dp.tint >> 8) & 255) / 255);
				int hb = lit(((a.hairCol >> 16) & 255) * ((dp.tint >> 16) & 255) / 255);
				dh0.tint = RGBA8(hr, hg, hb, al);
			}
			// 白人の素体は地肌が頭なので、髪型「標準」(0) でも髪色の地毛を被せる。
			int hairStyle = a.hair > 0 ? Clampi(a.hair, 1, HAIR_STYLES - 1) : 0;
			if (!r.DrawHumanHair(a.model, hairStyle, humM->headJ, a.pose, wGear, dh0, a.avatar) && a.hair > 0 && !digitBody) {
			int style = Clampi(a.hair, 1, HAIR_STYLES - 1);
			int slot = -1;
			for (int s = 0; s < lib.humHairN; s++) if (lib.humHairModel[s] == a.model) { slot = s; break; }
			const GpuMesh* mesh = (slot >= 0) ? &lib.humHair[slot][style] : NULL;
			const Buf<VtxS>* hv = (slot >= 0) ? &lib.humHairV[slot][style] : NULL;
			V3 piv = (slot >= 0) ? lib.humHairPivot[slot][style] : v3(0, 0, 0);
			// 起動時の枠に入らなかった体でも、今のモデルで作り直して同じ経路で描く。
			static int fbModel = -2;
			static GpuMesh fbMesh[HAIR_STYLES];
			static Buf<VtxS> fbV[HAIR_STYLES];
			static V3 fbP[HAIR_STYLES];
			if (!mesh || !mesh->vb || !hv || hv->empty()) {
				if (fbModel != a.model) {
					for (int s = 1; s < HAIR_STYLES; s++) {
						fbMesh[s].Release();
						fbV[s].clear();
						BuildHair(r, *humM, s, fbMesh[s], 2.2f, &fbV[s], &fbP[s]);
					}
					fbModel = a.model;
				}
				if (fbMesh[style].vb && !fbV[style].empty()) {
					mesh = &fbMesh[style];
					hv = &fbV[style];
					piv = fbP[style];
				}
			}
			if (mesh && mesh->vb && hv && !hv->empty()) {
				DrawParams dh = dp;
				dh.rim = 0.04f;
				dh.hue = 0;
				dh.outline = 0;
				dh.twoSided = 1;
				dh.sway = 0.f;
				dh.emissive = 0.f;
				int al = (dp.tint >> 24) & 255;
				auto lit = [](int c) { int v = (int)(c / 1.42f); return v < 0 ? 0 : (v > 255 ? 255 : v); };
				int hr = lit((a.hairCol & 255) * (dp.tint & 255) / 255);
				int hg = lit(((a.hairCol >> 8) & 255) * ((dp.tint >> 8) & 255) / 255);
				int hb = lit(((a.hairCol >> 16) & 255) * ((dp.tint >> 16) & 255) / 255);
				dh.tint = RGBA8(hr, hg, hb, al);
				// 差分はモデルの右・上・後ろ。頭ボーンの軸は横を向いているので、姿勢からモデル軸を戻して載せる。
				const M4& H = a.pose.global[humM->headJ];
				const M4& IB = humM->invBind[humM->headJ];
				V3 up = Norm(MXformDir(H, MXformDir(IB, v3(0, 1, 0))));
				V3 back = Norm(MXformDir(H, MXformDir(IB, v3(0, 0, 1))));
				back = Norm(back - up * Dot(back, up));
				V3 right = Norm(Cross(up, back));
				V3 skull = MXformPt(H, piv);
				static Buf<VtxS> buf;
				buf.resize(hv->size());
				float y0 = 1e9f, y1 = -1e9f;
				for (size_t k = 0; k < hv->size(); k++) {
					const VtxS& s = (*hv)[k];
					VtxS& d = buf[k];
					d = s;
					V3 o = v3(s.p[0], s.p[1], s.p[2]);
					V3 p = skull + right * o.x + up * o.y + back * o.z;
					V3 n = Norm(right * s.n[0] + up * s.n[1] + back * s.n[2]);
					d.p[0] = p.x; d.p[1] = p.y; d.p[2] = p.z;
					d.n[0] = n.x; d.n[1] = n.y; d.n[2] = n.z;
					d.c = (s.c & 0xFF000000u) | 0x00FFFFFFu;
					if (p.y < y0) y0 = p.y;
					if (p.y > y1) y1 = p.y;
				}
				// 束ごと後ろへずらす。内側の頂点が服の背面より 2cm 外に出るまで、その高さの断面をまとめて動かす。
				if (a.avatar > 0 && y1 > y0 + 0.02f) {
					const int NB = 24;
					float push[24];
					int hit[24];
					for (int i = 0; i < NB; i++) { push[i] = 0.f; hit[i] = 0; }
					float dy = (y1 - y0) / (float)(NB - 1);
					for (size_t k = 0; k < buf.size(); k++) {
						float surf = r.AvatarClothBack(a.model, a.avatar, buf[k].p[1]);
						if (surf < -10.f) continue;
						float need = surf + 0.020f - buf[k].p[2];
						int b = (int)((buf[k].p[1] - y0) / dy + 0.5f);
						if (b < 0) b = 0;
						if (b >= NB) b = NB - 1;
						hit[b] = 1;
						if (need > push[b]) push[b] = need;
					}
					for (int i = 1; i < NB - 1; i++) if (hit[i]) {
						float m = push[i - 1] > push[i + 1] ? push[i - 1] : push[i + 1];
						if (push[i] < m - 0.01f) push[i] = m - 0.01f;
					}
					for (size_t k = 0; k < buf.size(); k++) {
						int b = (int)((buf[k].p[1] - y0) / dy + 0.5f);
						if (b < 0) b = 0;
						if (b >= NB) b = NB - 1;
						buf[k].p[2] += push[b];
					}
				}
				for (size_t k = 0; k < buf.size(); k++) if (buf[k].n[2] > 0.f) {
					buf[k].p[0] += buf[k].n[0] * 0.004f;
					buf[k].p[1] += buf[k].n[1] * 0.004f;
					buf[k].p[2] += buf[k].n[2] * 0.004f;
				}
				r.DrawStaticVerts(*mesh, buf.data(), (UINT)buf.size(), wGear, dh);
			}
			}
		}
		return;
	}
	if (a.proc == 2) {
		M4 w = MMul(LocoXform(a, time), world);
		DrawParams d = dp;
		d.tint = dp.tint;
		r.DrawCreature(a.model, w, d);
		return;
	}
	if (a.model < 0 || a.model >= (int)r.models.size()) return;
	if (!a.poseValid) ActorPose(a, lib, r);
	if (!a.poseValid) return;
	const Model& m = r.models[a.model];
	M4 drawW = world;
	if ((a.clip == CL_WALK || a.clip == CL_RUN) && (a.rig < 0 || a.rig >= 4 || lib.clipMap[a.rig][a.clip] < 0 || lib.clipMap[a.rig][a.clip] == lib.clipMap[a.rig][CL_IDLE]))
		drawW = MMul(LocoXform(a, time), world);
	DrawParams d = dp;
	d.tint = dp.tint;
	if (a.tint != 0xFFFFFFFF) {
		int rr = ((a.tint & 255) * (dp.tint & 255)) / 255, gg = (((a.tint >> 8) & 255) * ((dp.tint >> 8) & 255)) / 255;
		int bb = (((a.tint >> 16) & 255) * ((dp.tint >> 16) & 255)) / 255;
		d.tint = RGBA8(rr, gg, bb, (dp.tint >> 24) & 255);
	}
	d.hue = dp.hue + a.hue;
	r.DrawSkinned(m, a.pose, drawW, d);
	DrawParams da = d;
	da.hue = (a.proc || a.rig == 1) ? dp.hue : dp.hue + a.hue * .5f;
	for (int i = 0; i < a.nAtt; i++)
		r.DrawAttach(r.attaches[a.att[i]], a.pose, a.attJ[i], drawW, da);
	for (int i = 0; i < a.nGear; i++) if (a.gearJ[i] >= 0 && a.gearJ[i] < a.pose.nJ)
		r.DrawGear(a.gearM[i], MMul(a.pose.global[a.gearJ[i]], drawW), da);
	if (!a.proc && a.hair > 0 && m.headJ >= 0) {
		const GpuMesh& h = lib.hair[a.body][a.hair];
		if (h.vb) {
			DrawParams dh = dp;
			int al = (dp.tint >> 24) & 255;
			int hr = (a.hairCol & 255) * (dp.tint & 255) / 255, hg = ((a.hairCol >> 8) & 255) * ((dp.tint >> 8) & 255) / 255;
			int hb = ((a.hairCol >> 16) & 255) * ((dp.tint >> 16) & 255) / 255;
			dh.tint = RGBA8(hr, hg, hb, al);
			dh.hue = 0;
			r.DrawStatic(h, MMul(a.pose.global[m.headJ], drawW), dh);
		}
	}
	if (a.wings && m.chestJ >= 0) {
		int dr = a.wings == 2 ? 1 : 0;
		float flapSp = a.hover > 0 ? 6.f : 2.f;
		float flap = sinf(time * flapSp) * (dr ? .35f : .45f);
		for (int side = 0; side < 2; side++) {
			float s = side == 0 ? 1.f : -1.f;
			M4 fl = MRotZ(s * flap);
			M4 base = MMul(MMul(MRotY(s * -.55f), MRotZ(s * .35f)), MTrans(s * .18f, 1.12f, .32f));
			M4 ex = MMul(MMul(fl, base), m.invBind[m.chestJ]);
			M4 w = MMul(ex, MMul(a.pose.global[m.chestJ], drawW));
			DrawParams dw = dp;
			dw.hue = 0;
			r.DrawStatic(lib.wing[dr][side], w, dw);
		}
	}
}

} // namespace otac
