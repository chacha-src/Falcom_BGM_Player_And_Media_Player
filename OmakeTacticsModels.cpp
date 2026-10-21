#include "stdafx.h"
#include "OmakeTacticsModels.h"
#include <math.h>

namespace otac {

static const float PI = 3.14159265f;

static const char* const kClipNames[CL_COUNT] = {
	"Idle", "Walking_A", "Running_A", "1H_Melee_Attack_Chop", "1H_Melee_Attack_Slice_Diagonal", "1H_Melee_Attack_Stab",
	"2H_Melee_Attack_Chop", "2H_Melee_Attack_Spin", "1H_Ranged_Shoot", "2H_Ranged_Shoot",
	"Spellcast_Shoot", "Spellcast_Raise", "Spellcast_Long", "Hit_A", "Hit_B", "Death_A", "Cheer", "Block", "Dodge_Backward", "2H_Melee_Idle",
	"Throw", "Unarmed_Melee_Attack_Punch_A", "Jump_Full_Short", "Interact", "Taunt", "Spawn_Ground", "Use_Item"
};

static int Pal(int row, int col) { return row * 16 + col; }

// ============================================================ ジオメトリ組立
struct Geo {
	std::vector<V3> p, n;
	std::vector<int> pal, b0, b1;
	std::vector<float> w1;
	std::vector<DWORD> col;
	std::vector<WORD> idx;
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
		std::vector<int> ok((seg + 1) * (rings + 1));
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
	void ToSkinned(std::vector<VtxK>& out) const
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
	void ToStatic(std::vector<VtxS>& out) const
	{
		out.resize(p.size());
		for (size_t i = 0; i < p.size(); i++) {
			VtxS& v = out[i];
			v.p[0] = p[i].x; v.p[1] = p[i].y; v.p[2] = p[i].z;
			v.n[0] = n[i].x; v.n[1] = n[i].y; v.n[2] = n[i].z;
			v.uv[0] = v.uv[1] = .5f;
			v.c = col[i] | 0xFF000000;
		}
	}
};

static BOOL GeoToStatic(Renderer& r, const Geo& g, GpuMesh& out)
{
	if (g.p.empty() || g.idx.empty() || g.p.size() > 65535) return FALSE;
	std::vector<VtxS> v;
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
	std::vector<VtxK> v;
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
	std::vector<VtxK> v;
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
	std::vector<VtxK> v;
	g.ToSkinned(v);
	return r.AddProcModel(m, v, g.idx);
}

static int MakeGolem(Renderer& r)
{
	Model m;
	BuildProcRig(m, "proc_golem", kGolJ, GJ_N);
	m.headJ = GJ_HEAD;
	Geo g;
	const int stone = Pal(0, 9), dark = Pal(0, 6), moss = Pal(6, 5), eye = Pal(4, 8);
	M4 tilt = MRotY(.08f);
	g.curBone = GJ_HIPS; g.curPal = dark;
	g.Box(v3(0, .58f, 0), v3(.36f, .14f, .26f));
	g.curBone = GJ_TORSO; g.curPal = stone;
	g.Box(v3(0, 1.0f, 0), v3(.5f, .3f, .34f), &tilt);
	g.curPal = moss;
	g.Box(v3(0, 1.31f, .05f), v3(.44f, .04f, .28f));
	g.Box(v3(.3f, 1.25f, -.15f), v3(.12f, .06f, .12f));
	g.curPal = Pal(4, 8);
	g.Box(v3(0, 1.02f, -.35f), v3(.1f, .1f, .02f));
	g.curBone = GJ_HEAD; g.curPal = stone;
	g.Box(v3(0, 1.48f, -.05f), v3(.2f, .16f, .2f));
	g.curPal = eye;
	g.Box(v3(.08f, 1.5f, -.26f), v3(.045f, .03f, .015f));
	g.Box(v3(-.08f, 1.5f, -.26f), v3(.045f, .03f, .015f));
	for (int side = 0; side < 2; side++) {
		float s = side == 0 ? 1.f : -1.f;
		g.curBone = side == 0 ? GJ_AL : GJ_AR; g.curPal = stone;
		g.Box(v3(s * .62f, 1.18f, 0), v3(.2f, .18f, .2f));
		g.curPal = dark;
		g.Box(v3(s * .6f, .98f, 0), v3(.12f, .14f, .12f));
		g.curBone = side == 0 ? GJ_FL : GJ_FR; g.curPal = stone;
		g.Box(v3(s * .62f, .66f, -.02f), v3(.17f, .22f, .17f));
		g.curPal = moss;
		g.Box(v3(s * .62f, .88f, -.02f), v3(.18f, .03f, .18f));
		g.curBone = side == 0 ? GJ_LL : GJ_LR; g.curPal = stone;
		g.Box(v3(s * .24f, .24f, 0), v3(.15f, .24f, .17f));
		g.curPal = dark;
		g.Box(v3(s * .24f, .03f, -.04f), v3(.17f, .04f, .21f));
	}
	std::vector<VtxK> v;
	g.ToSkinned(v);
	return r.AddProcModel(m, v, g.idx);
}

// 手続きアニメ（clip 種別ごと）
enum { PK_IDLE, PK_MOVE, PK_ATTACK, PK_CAST, PK_HIT, PK_DEATH, PK_CHEER };
static int ProcKind(int clip)
{
	switch (clip) {
	case CL_WALK: case CL_RUN: case CL_JUMP: return PK_MOVE;
	case CL_1H_CHOP: case CL_1H_SLICE: case CL_1H_STAB: case CL_2H_CHOP: case CL_2H_SPIN: case CL_1H_SHOOT: case CL_2H_SHOOT:
	case CL_THROW: case CL_PUNCH: return PK_ATTACK;
	case CL_CAST_SHOOT: case CL_CAST_RAISE: case CL_CAST_LONG: return PK_CAST;
	case CL_HIT_A: case CL_HIT_B: case CL_DODGE: return PK_HIT;
	case CL_DEATH: return PK_DEATH;
	case CL_CHEER: case CL_TAUNT: case CL_INTERACT: case CL_USE: case CL_SPAWN: return PK_CHEER;
	}
	return PK_IDLE;
}
static float ProcLen(int kind)
{
	static const float L[] = { 2.f, .6f, .9f, 1.2f, .5f, 1.2f, 1.4f };
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
		case PK_MOVE: { float ph = fmodf(t / .6f, 1.f); lift = sinf(ph * PI) * .22f; sq = ph < .15f ? 1.f - .25f * sinf(ph / .15f * PI) : 1.f + .12f * sinf(ph * PI); } break;
		case PK_ATTACK: { float a = AtkCurve(u); sq = 1.f - a * .25f; fwd = max(0.f, a) * .45f; lift = max(0.f, a) * .15f; } break;
		case PK_CAST: sq = 1.f + .2f * sinf(u * PI * 3.f) * (1 - u); break;
		case PK_HIT: sq = 1.f - .3f * sinf(u * PI); shake = sinf(u * 40.f) * .05f * (1 - u); break;
		case PK_DEATH: sq = Lerpf(1.f, .12f, Smooth01(u)); break;
		case PK_CHEER: { float ph = fmodf(u * 3.f, 1.f); lift = sinf(ph * PI) * .3f; sq = 1.f + .15f * sinf(ph * PI); } break;
		}
		float xz = 1.f / sqrtf(max(.1f, sq));
		if (kind == PK_DEATH) xz = Lerpf(1.f, 1.6f, Smooth01(u));
		mods.scale[SJ_BODY] = v3(xz, sq, xz);
		mods.any = 1;
		tr[SJ_ROOT] = v3(shake, lift, -fwd);
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
		if (kind == PK_MOVE) rot[BJ_BODY] = QX(-.25f);
		if (kind == PK_ATTACK) { float a = AtkCurve(u); root.z = -max(0.f, a) * .55f; root.y -= max(0.f, a) * .2f; rot[BJ_BODY] = QX(-a * .5f); rot[BJ_HEAD] = QX(-a * .3f); }
		if (kind == PK_CAST) rot[BJ_HEAD] = QX(-sinf(u * PI) * .4f);
		if (kind == PK_HIT) { root.z = sinf(u * PI) * .25f; rot[BJ_BODY] = QX(sinf(u * PI) * .5f); }
		if (kind == PK_DEATH) { root.y -= Smooth01(u) * .85f; rot[BJ_BODY] = QX(Smooth01(u) * 1.5f); rot[BJ_WL] = QZ(-.4f); rot[BJ_WR] = QZ(.4f); }
		if (kind == PK_CHEER) root.y += sinf(u * PI * 2.f) * .15f;
		tr[BJ_ROOT] = root;
		return;
	}
	if (look == LK_WYVERN) {
		float flapSp = kind == PK_MOVE ? 7.f : 4.5f;
		float flap = sinf(t * flapSp);
		rot[WJ_WL] = QMul(QY(.15f), QZ(flap * .55f + .1f)); rot[WJ_WR] = QMul(QY(-.15f), QZ(-flap * .55f - .1f));
		rot[WJ_WL2] = QZ(flap * .3f); rot[WJ_WR2] = QZ(-flap * .3f);
		rot[WJ_T1] = QY(sinf(t * 1.7f) * .25f); rot[WJ_T2] = QY(sinf(t * 1.7f - .8f) * .35f);
		rot[WJ_NECK] = QX(sinf(t * 2.f) * .05f);
		V3 root = v3(0, -flap * .06f, 0);
		rot[WJ_LL] = QX(.3f); rot[WJ_LR] = QX(.3f);
		if (kind == PK_ATTACK) {
			float a = AtkCurve(u);
			rot[WJ_NECK] = QX(-a * .55f); rot[WJ_HEAD] = QX(-a * .25f);
			rot[WJ_JAW] = QX(max(0.f, a) * .5f + (u < .4f ? Smooth01(u / .4f) * .4f : 0));
			root.z = -max(0.f, a) * .35f;
		}
		if (kind == PK_CAST) {
			float k = sinf(u * PI);
			rot[WJ_NECK] = QX(k * .4f - Smooth01(u) * .5f); rot[WJ_JAW] = QX(k * .7f);
		}
		if (kind == PK_HIT) { rot[WJ_BODY] = QX(sinf(u * PI) * .3f); root.z = sinf(u * PI) * .2f; }
		if (kind == PK_DEATH) { float k = Smooth01(u); rot[WJ_BODY] = QZ(k * 1.1f); root.y -= k * .6f; rot[WJ_WL] = QZ(-.6f * k); rot[WJ_WR] = QZ(.2f); }
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
			float ph = t / .6f * 2.f * PI;
			rot[GJ_LL] = QX(sinf(ph) * .45f); rot[GJ_LR] = QX(-sinf(ph) * .45f);
			rot[GJ_AL] = QX(-sinf(ph) * .35f); rot[GJ_AR] = QX(sinf(ph) * .35f);
			rot[GJ_TORSO] = QZ(sinf(ph) * .06f);
			root.y = fabsf(cosf(ph)) * .04f;
		}
		if (kind == PK_ATTACK) {
			float a = AtkCurve(u);
			rot[GJ_AR] = QX(-1.9f * max(0.f, -a * 2.f) + max(0.f, a) * -1.4f);
			rot[GJ_FR] = QX(-a * .5f);
			rot[GJ_TORSO] = QMul(QY(-a * .35f), QX(-max(0.f, a) * .25f));
			root.z = -max(0.f, a) * .2f;
		}
		if (kind == PK_CAST) {
			float k = sinf(u * PI);
			rot[GJ_AL] = QX(-k * 2.4f); rot[GJ_AR] = QX(-k * 2.4f);
			root.y = k * .15f - (u > .6f ? sinf((u - .6f) / .4f * PI) * .1f : 0);
		}
		if (kind == PK_HIT) { rot[GJ_TORSO] = QX(sinf(u * PI) * .3f); root.z = sinf(u * PI) * .12f; }
		if (kind == PK_DEATH) { float k = Smooth01(u); rot[GJ_HIPS] = QX(-k * 1.35f); root.y = -k * .3f; root.z = -k * .3f; }
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

static void BuildHair(Renderer& r, const Model& m, int style, GpuMesh& out)
{
	if (style <= 0 || m.headJ < 0) return;
	M4 bind = MInverse(m.invBind[m.headJ]);
	V3 o = v3(bind.m[12], bind.m[13], bind.m[14]);
	V3 mn = v3(m.headMin[0], m.headMin[1], m.headMin[2]), mx = v3(m.headMax[0], m.headMax[1], m.headMax[2]);
	V3 c = o + (mn + mx) * .5f;
	V3 rr = (mx - mn) * .5f;
	rr.z = min(rr.z, rr.x);
	rr.y = min(rr.y, rr.x * 1.05f);
	Geo g;
	g.curCol = 0xFFFFFFFF;
	float capS = style == 1 ? 1.06f : 1.08f;
	V3 capR = v3(rr.x * capS, rr.y * capS, rr.z * capS);
	V3 cc = c + v3(0, rr.y * .05f, rr.z * .04f);
	float lowBack = style == 5 ? -.72f : (style == 2 ? -.45f : -.3f);
	g.Ellipsoid(MTrans(cc.x, cc.y, cc.z), capR, 20, 14, [lowBack](V3 d) {
		if (d.z < -.3f) return d.y > .26f;
		if (d.z < 0.f) return d.y > Lerpf(.26f, lowBack + .2f, (d.z + .3f) / .3f);
		return d.y > lowBack;
	});
	if (style == 1) {
		for (int k = 0; k < 7; k++) {
			float a = k * 2.f * PI / 7.f;
			V3 d = Norm(v3(cosf(a) * .7f, .75f, sinf(a) * .7f + .15f));
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
	g.Transform(m.invBind[m.headJ]);
	GeoToStatic(r, g, out);
	out.tex = r.TexWhite();
}

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
	for (int d = 0; d < 2; d++) for (int l = 0; l < 2; l++) BuildWing(r, d, l, lib.wing[d][l]);
	BuildCrown(r, lib.crown);
	BuildRing(r, lib.ring);
	BuildOrb(r, lib.orb);
	lib.ready = 1;
	return TRUE;
}

void ReleaseLibrary(Library& lib)
{
	for (int b = 0; b < B_COUNT; b++) for (int s = 0; s < HAIR_STYLES; s++) lib.hair[b][s].Release();
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
	if (m.rig >= 0 && m.rig < (int)r.rigs.size()) j = r.rigs[m.rig].Find(r.attaches[i].joint);
	if (j < 0) return;
	a.att[a.nAtt] = i; a.attJ[a.nAtt] = j; a.nAtt++;
}

static void ActorInit(Actor& a)
{
	memset(&a, 0, sizeof(a));
	a.model = -1;
	a.hair = 0;
	a.scale = 1;
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
	a.body = Clampi(c.body, 0, B_COUNT - 1);
	a.model = lib.bodyModel[a.body];
	if (a.model < 0) return;
	a.rig = r.models[a.model].rig;
	a.female = c.gender == 1;
	a.scale = a.female ? .40f : .43f;
	{
		int tier = ClassInfo(c.cls).tier;
		if (tier > 1) a.scale *= 1.f + (tier - 1) * .02f;
		if (tier >= 5) a.hue += .04f * (tier - 4);
	}
	a.hue = OutfitHue(c.outfit);
	a.hairCol = HairColor(c.hairColor);
	a.hair = Clampi(c.hair, 0, HAIR_STYLES - 1);
	if (a.female && a.hair == 0) a.hair = 2;
	if (a.body == B_ROGUE_HOOD) a.hair = 0;
	if (c.hat && a.body == B_KNIGHT) a.hair = 0;
	const ClassDef& cd = ClassInfo(c.cls);
	AddWeapons(a, r, cd.weaponLook);
	if (c.hat) {
		static const char* const hats[B_COUNT] = { "Knight_Helmet", "Barbarian_Hat", "Mage_Hat", NULL, NULL };
		if (hats[a.body]) AddAtt(a, r, hats[a.body]);
	}
	if (c.cape) {
		static const char* const capes[B_COUNT] = { "Knight_Cape", "Barbarian_Cape", "Mage_Cape", "Rogue_Cape", "Rogue_Cape" };
		AddAtt(a, r, capes[a.body]);
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
		for (int k = 0; k < (int)(sizeof(kOwn) / sizeof(kOwn[0])); k++) {
			int j = g.Find(kOwn[k].n);
			if (j >= 0 && j < MAXJ) a.mods.own[j] = v3(kOwn[k].x, kOwn[k].y, kOwn[k].z);
		}
		int hd = g.Find("head");
		if (hd >= 0 && hd < MAXJ) a.mods.scale[hd] = v3(1.06f, 1.06f, 1.06f);
		a.mods.any = 1;
	}
	a.height = 2.35f * a.scale;
	a.hover = a.wings ? .35f : 0;
}

void ActorFromMonster(Actor& a, const Library& lib, const Renderer& r, int monster, int boss)
{
	ActorInit(a);
	const MonsterDef& md = MonsterInfo(monster);
	a.look = md.look;
	DWORD t = md.tint;
	a.tint = RGBA8((t >> 16) & 255, (t >> 8) & 255, t & 255);
	float bs = boss ? 1.15f : 1.f;
	if (md.look == LK_MESH) {
		a.proc = 2;
		a.model = md.model;
		a.rig = -1;
		a.scale = md.scale * bs;
		a.height = 1.7f * a.scale;
		a.hover = md.moveType == MV_FLY ? .4f : 0;
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
}

void ActorUpdate(Actor& a, float dt)
{
	a.t += dt * a.animSpeed;
	a.pt += dt * a.animSpeed;
	if (a.blend < 1.f) a.blend = min(1.f, a.blend + dt / a.blendDur);
}

float ActorClipLen(const Actor& a, const Library& lib, const Renderer& r, int clip)
{
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
	return MMul(MMul(MScale(a.scale, a.scale, a.scale), MRotY(face + PI)), MTrans(pos.x, pos.y, pos.z));
}

V3 ActorJointWorld(const Actor& a, const M4& world, int which)
{
	float hl = a.height / max(.01f, a.scale);
	V3 fallback = MXformPt(world, v3(0, hl * (which == 0 ? .85f : .55f), 0));
	if (!a.poseValid || a.model < 0) return fallback;
	int j = -1;
	if (a.proc) {
		static const int J[5][3] = { { 0, 0, 0 }, { SJ_TOP, SJ_TOP, SJ_BODY }, { BJ_HEAD, BJ_HEAD, BJ_BODY }, { WJ_HEAD, WJ_JAW, WJ_BODY }, { GJ_HEAD, GJ_FR, GJ_TORSO } };
		j = J[Clampi(a.look, 0, 4)][Clampi(which, 0, 2)];
	} else {
		const Model& m = *(const Model*)a.modelPtr;
		j = which == 0 ? m.headJ : (which == 1 ? m.handR : m.chestJ);
	}
	if (j < 0 || j >= a.pose.nJ) return fallback;
	M4 g = MMul(a.pose.global[j], world);
	V3 p = v3(g.m[12], g.m[13], g.m[14]);
	if (which == 0) p.y += a.height * .18f;
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

void ActorDraw(Renderer& r, const Library& lib, Actor& a, const M4& world, const DrawParams& dp, float time)
{
	if (a.proc == 2) {
		float bob = sinf(time * 2.2f) * (a.hover > 0.f ? .08f : .03f);
		M4 w = MMul(MTrans(0, bob, 0), world);
		DrawParams d = dp;
		d.tint = dp.tint;
		r.DrawCreature(a.model, w, d);
		return;
	}
	if (a.model < 0 || a.model >= (int)r.models.size()) return;
	if (!a.poseValid) ActorPose(a, lib, r);
	if (!a.poseValid) return;
	const Model& m = r.models[a.model];
	DrawParams d = dp;
	d.tint = dp.tint;
	if (a.tint != 0xFFFFFFFF) {
		int rr = ((a.tint & 255) * (dp.tint & 255)) / 255, gg = (((a.tint >> 8) & 255) * ((dp.tint >> 8) & 255)) / 255;
		int bb = (((a.tint >> 16) & 255) * ((dp.tint >> 16) & 255)) / 255;
		d.tint = RGBA8(rr, gg, bb, (dp.tint >> 24) & 255);
	}
	d.hue = dp.hue + a.hue;
	r.DrawSkinned(m, a.pose, world, d);
	DrawParams da = d;
	da.hue = (a.proc || a.rig == 1) ? dp.hue : dp.hue + a.hue * .5f;
	for (int i = 0; i < a.nAtt; i++)
		r.DrawAttach(r.attaches[a.att[i]], a.pose, a.attJ[i], world, da);
	if (!a.proc && a.hair > 0 && m.headJ >= 0) {
		const GpuMesh& h = lib.hair[a.body][a.hair];
		if (h.vb) {
			DrawParams dh = dp;
			int al = (dp.tint >> 24) & 255;
			int hr = (a.hairCol & 255) * (dp.tint & 255) / 255, hg = ((a.hairCol >> 8) & 255) * ((dp.tint >> 8) & 255) / 255;
			int hb = ((a.hairCol >> 16) & 255) * ((dp.tint >> 16) & 255) / 255;
			dh.tint = RGBA8(hr, hg, hb, al);
			dh.hue = 0;
			r.DrawStatic(h, MMul(a.pose.global[m.headJ], world), dh);
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
			M4 w = MMul(ex, MMul(a.pose.global[m.chestJ], world));
			DrawParams dw = dp;
			dw.hue = 0;
			r.DrawStatic(lib.wing[dr][side], w, dw);
		}
	}
}

} // namespace otac
