#include "stdafx.h"
#include "OmakeTacticsGame.h"
#include "OmakeTacticsAudio.h"
#include "DatArchive.h"
#include "simd_kernels.h"
#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <immintrin.h>

namespace otac {

static const float PI = 3.14159265f;

static float FitLo(float v, float lo, float hi)
{
	if (hi < lo) hi = lo;
	return v < lo ? lo : (v > hi ? hi : v);
}

static int WeatherTx(int w);
static int HourTx(int h);

enum {
	ID_BT_NOMOVE = 448,
	ID_BT_ATTACK = 500, ID_BT_SKILL, ID_BT_WAIT, ID_BT_UNDO, ID_BT_BACK, ID_BT_LVOK, ID_BT_SHOVE, ID_BT_DOOR, ID_BT_ITEM, ID_BT_CLIMB,
	ID_BT_SKPREV = 510, ID_BT_SKNEXT,
	ID_BT_SKTAB = 480,
	ID_BT_SHAPE0 = 1100, ID_BT_SHAPE1, ID_BT_SHAPE2, ID_BT_SHAPE3,
	ID_BT_EL0 = 1110,
	ID_BT_FORM0 = 430,
	ID_BT_STAT = 515, ID_BT_LOOK, ID_BT_SUMMON = 517,
	ID_BT_SUM0 = 1160, ID_BT_SUM1,
	ID_BT_SK0 = 520,
	ID_BT_CRAFT0 = 800, ID_BT_CRAFT1, ID_BT_CRAFT2, ID_BT_CRAFT3,
	ID_BT_M_RESUME = 900, ID_BT_M_HELP, ID_BT_M_RETREAT, ID_BT_M_YES, ID_BT_M_NO, ID_BT_MENU, ID_BT_M_SAVE = 906, ID_BT_M_OPT, ID_BT_M_ZOOM, ID_BT_M_SLOT = 910, ID_BT_M_GAME = 911,
	ID_BT_OPT_Q = 920, ID_BT_OPT_ANIM, ID_BT_OPT_GRID, ID_BT_OPT_THREAT, ID_BT_OPT_AI, ID_BT_OPT_SYNC, ID_BT_OPT_WIN, ID_BT_OPT_FPS,
	ID_BT_OPT_PACE = 928, ID_BT_OPT_CUT,
	ID_BT_IT0 = 960, ID_BT_ITT0 = 980,
	ID_BT_PROMO0 = 740, ID_BT_PROMO_LATER = 748,
	ID_BT_EQ0 = 750, ID_BT_EQNONE = 754, ID_BT_EQROW = 755, ID_BT_PAS0 = 762, ID_BT_PAS1, ID_BT_EQPREV = 764, ID_BT_EQNEXT = 765, ID_BT_BEAD2 = 766, ID_BT_BEAD3,
	ID_BT_ACC0 = 768, ID_BT_ACC1, ID_BT_EQSUM0, ID_BT_EQSUM1
};

static float AngLerp(float a, float b, float t)
{
	float d = WrapAngle(b - a);
	return a + d * t;
}

// 死亡中・倒れ終わり。待機や被弾に替えると一度起きてから、蘇生の先頭（倒れた姿勢）でもう一度倒れる。
// shownHp を渡すと（カットシーン中の m_dispHp）予測解決後の u.hp=0 で先に倒れモーションにしない。
static BOOL PoseGround(const Actor& a, const Unit& u, float dieT, float shownHp = -1.f)
{
	if (a.clip == CL_DEATH) return TRUE;
	if (dieT >= 0.f) return TRUE;
	if (!u.alive && shownHp < 0.f) return TRUE;
	float hp = shownHp >= 0.f ? shownHp : (float)u.hp;
	if (hp <= 0.5f) return TRUE;
	return FALSE;
}

// 起き上がりの途中も切らない。蘇生クリップの先頭は倒れた姿勢なので、途中で待機に戻すと落ちてからまた起きる。
static BOOL PoseKeep(const Actor& a, const Unit& u, float dieT, float shownHp = -1.f)
{
	if (a.clip == CL_SPAWN) return TRUE;
	return PoseGround(a, u, dieT, shownHp);
}

// 既に倒れている体は、落下をもう一度再生せず終わりの姿勢で止める。
static V3 AilmentPos(const Actor& a, V3 p)
{
	if (a.poseHold == 1) p.y -= a.height * 0.28f;
	else if (a.poseHold == 2) p.y -= a.height * 0.42f;
	return p;
}
static void HoldCorpse(Actor& a)
{
	ActorPlay(a, CL_DEATH, 0, 0.f);
	a.t = 8.f;
	a.blend = 1.f;
	a.animSpeed = 1.f;
}

static DWORD ElemColor(int el)
{
	switch (el) {
	case EL_FIRE: return RGBA8(255, 120, 40);
	case EL_ICE: return RGBA8(120, 200, 255);
	case EL_THUNDER: return RGBA8(255, 236, 90);
	case EL_HOLY: return RGBA8(255, 240, 180);
	case EL_DARK: return RGBA8(170, 80, 230);
	case EL_WIND: return RGBA8(120, 240, 160);
	case EL_EARTH: return RGBA8(200, 150, 90);
	case EL_POISON: return RGBA8(170, 230, 80);
	}
	return RGBA8(255, 255, 255);
}

V3 Game::CellCenter(int x, int z) const
{
	const Map& m = m_b.map;
	return v3(x + .5f, m.StandY(x, z), z + .5f);
}

V3 Game::UnitGroundPos(const Unit& u) const
{
	int s = UnitSpan(u);
	if (s <= 1) {
		V3 p = CellCenter(u.x, u.z);
		p.y = m_b.map.WalkY(u.x, u.z, u.deck);
		const Cell& c = m_b.map.At(u.x, u.z);
		if (UnitMoveType(u) == MV_FLY) {
			if (c.t == T_CHASM) p.y = 0;
			p.y = max(p.y, m_b.map.TopY(u.x, u.z));
		}
		return p;
	}
	float y = -100.f;
	BOOL chasm = FALSE;
	for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++) {
		int x = u.x + dx, z = u.z + dz;
		if (!Map::In(x, z)) continue;
		if (m_b.map.At(x, z).t == T_CHASM) chasm = TRUE;
		y = max(y, m_b.map.WalkY(x, z, u.deck));
	}
	if (y < -50.f) y = 0;
	V3 p = v3((float)u.x + s * 0.5f, y, (float)u.z + s * 0.5f);
	if (UnitMoveType(u) == MV_FLY) {
		if (chasm) p.y = 0;
		for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++) {
			int x = u.x + dx, z = u.z + dz;
			if (Map::In(x, z)) p.y = max(p.y, m_b.map.TopY(x, z));
		}
	}
	return p;
}

void Game::LoadBattleCast()
{
	int ids[256];
	int n = 0;
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (u.team == TEAM_PLAYER || u.monster < 0) continue;
		const MonsterDef& md = MonsterInfo(u.monster);
		if (md.look != LK_MESH || md.model < 0) continue;
		int have = 0;
		for (int k = 0; k < n; k++) if (ids[k] == md.model) { have = 1; break; }
		if (have) continue;
		if (n >= 256) break;
		ids[n++] = md.model;
	}
	for (int i = 0; i < m_b.nUnits && n < 256; i++) {
		const Unit& u = m_b.u[i];
		if (u.team != TEAM_PLAYER) continue;
		for (int slot = 0; slot < 2 && n < 256; slot++) {
			int sid = EquippedSummonId(u.ch, slot);
			if (sid <= 0) continue;
			int mesh = SummonMesh(sid - 1);
			if (mesh < 0) continue;
			int have = 0;
			for (int k = 0; k < n; k++) if (ids[k] == mesh) { have = 1; break; }
			if (!have) ids[n++] = mesh;
		}
	}
	m_r.LoadBattleCreatures(ids, n);
}

void Game::BindImportedActors()
{
	BOOL missing = FALSE, need = FALSE;
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (u.team == TEAM_PLAYER || u.monster < 0) continue;
		const MonsterDef& md = MonsterInfo(u.monster);
		if (md.look != LK_MESH || md.model < 0) continue;
		int mi = m_r.CreatureModel(md.model);
		if (mi < 0) missing = TRUE;
		else if (m_act[i].proc != 3 || m_act[i].model != mi) need = TRUE;
	}
	if (missing && !m_importBind) {
		m_importBind = 1;
		LoadBattleCast();
		need = TRUE;
	}
	if (!need) return;
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (u.team == TEAM_PLAYER || u.monster < 0) continue;
		const MonsterDef& md = MonsterInfo(u.monster);
		if (md.look != LK_MESH || md.model < 0) continue;
		int mi = m_r.CreatureModel(md.model);
		if (mi < 0) continue;
		if (m_act[i].proc == 3 && m_act[i].model == mi) continue;
		ActorForUnit(i);
		if (u.alive && u.hp > 0 && u.rush && m_b.stampedeShow) ActorPlay(m_act[i], CL_SPAWN, 0, .05f);
	}
	InvalidatePortraits();
}

void Game::ActorForUnit(int i)
{
	Unit& u = m_b.u[i];
	if (u.team == TEAM_PLAYER) ActorFromChar(m_act[i], m_lib, m_r, u.ch);
	else ActorFromMonster(m_act[i], m_lib, m_r, u.monster, u.boss);
	if (UnitMoveType(u) == MV_FLY && m_act[i].hover < .2f) m_act[i].hover = .35f;
	if (!u.alive || u.hp <= 0) HoldCorpse(m_act[i]);
	else {
		ActorPlay(m_act[i], CL_IDLE, 1, 0);
		m_act[i].t = m_rng.F(0, 2);
	}
}

static const DWORD kSnapMagic = 0x31504153u;

struct TacSnap {
	DWORD magic;
	int version;
	int kind;
	int stage;
	DWORD rng;
	int cur, turnOpen, phase, moved, x0, z0, deck0, cx, cz;
	float face0;
	float camYaw, camPitch, camDist, camX, camY, camZ;
	int itemN[8];
	int resultN, resultLv0[MAX_PARTY], resultExp[MAX_PARTY], resultRoster[MAX_PARTY];
	int expGain[MAX_UNITS];
	int bossDown;
	AiPlan ai;
	Battle b;
};

static CRITICAL_SECTION g_snapCs;
static volatile LONG g_snapCsOk = 0;
// 中断データは Battle ごと大きい。new/delete を重ねるとヒープが割れて戦闘が遅くなる。
// 書き出し中と待ち行列で同時に要るのは中間と自動の2本ずつまで。
enum { SNAP_N = 4 };
static TacSnap g_snapPool[SNAP_N];
static TacSnap g_snapLoad;

// ラッキーフラグを足す前のユニット。途中セーブ／オートセーブの持ち上げ用。
struct UnitPreLucky {
	int alive;
	int team;
	int rosterIdx;
	int monster;
	int boss;
	CharData ch;
	int hp, mp;
	int x, z;
	int span;
	int ct;
	float face;
	int buffDef, buffAgi, buffAtk, poison, slow, defDown;
	int sleep, confuse, stun;
	int freeze, burn, bleed, blind, silence;
	int mulTurns;
	int alert;
	int rush;
	int deck;
	int grp;
	int expGained;
	int levelsGained;
	int skillLearned[4];
	int nLearned;
	int killed;
	int cp;
	int craftEq[4];
	int accEq[2];
	int pasEq[2];
	int pasUsed;
	float px, py, pz;
	float hpShow;
	float dissolve;
	float flash;
	float hover;
};
static_assert(sizeof(Unit) == sizeof(UnitPreLucky) + sizeof(int), "lucky added after boss");
static_assert(offsetof(Unit, boss) == offsetof(UnitPreLucky, boss), "unit prefix matches");
static_assert(offsetof(Unit, ch) == offsetof(UnitPreLucky, ch) + sizeof(int), "lucky sits before ch");

// 層を足す前の中断。マスは6バイト、占有は1層。ユニットはラッキー追加前。
struct Cell6 {
	BYTE t, h, prop, var, gimm, gparam;
};
struct Map6 {
	int theme;
	int stage;
	DWORD seed;
	Cell6 c[MAPN][MAPN];
	int pStartX[15], pStartZ[15];
	int eStartX[10], eStartZ[10];
	int nP, nE;
	MapGroup grp[MAX_GROUPS];
	int nGrp;
};
struct Battle6 {
	Map6 map;
	UnitPreLucky u[MAX_UNITS];
	int nUnits;
	int stage;
	int turnCount;
	int occ[MAPN][MAPN];
	int weather;
	int stampede;
	int stampedeLeft;
	int stampedeLine;
	int stampedeShow;
	int bgmWin;
	int sweep;
};
struct TacSnap6 {
	DWORD magic;
	int version;
	int kind;
	int stage;
	DWORD rng;
	int cur, turnOpen, phase, moved, x0, z0, deck0, cx, cz;
	float face0;
	float camYaw, camPitch, camDist, camX, camY, camZ;
	int itemN[8];
	int resultN, resultLv0[MAX_PARTY], resultExp[MAX_PARTY], resultRoster[MAX_PARTY];
	int expGain[MAX_UNITS];
	int bossDown;
	AiPlan ai;
	Battle6 b;
};
// 今の地図・層のまま、ユニットだけラッキー追加前。直近の途中セーブ／オートセーブ。
struct BattlePreLucky {
	Map map;
	UnitPreLucky u[MAX_UNITS];
	int nUnits;
	int stage;
	int turnCount;
	int occ[LAY_N][MAPN][MAPN];
	int weather;
	int stampede;
	int stampedeLeft;
	int stampedeLine;
	int stampedeShow;
	int bgmWin;
	int sweep;
	int lastActTeam;
};
struct TacSnapPreLucky {
	DWORD magic;
	int version;
	int kind;
	int stage;
	DWORD rng;
	int cur, turnOpen, phase, moved, x0, z0, deck0, cx, cz;
	float face0;
	float camYaw, camPitch, camDist, camX, camY, camZ;
	int itemN[8];
	int resultN, resultLv0[MAX_PARTY], resultExp[MAX_PARTY], resultRoster[MAX_PARTY];
	int expGain[MAX_UNITS];
	int bossDown;
	AiPlan ai;
	BattlePreLucky b;
};
static TacSnap6 g_snapOld;
static TacSnapPreLucky g_snapMid;
static_assert(sizeof(Cell) == 15, "cell is 15 bytes");
static_assert(sizeof(Map6) % 4 == 0, "old map stays aligned");
static_assert(sizeof(Battle) == sizeof(BattlePreLucky) + MAX_UNITS * sizeof(int), "battle grew by lucky");
static_assert(sizeof(TacSnap) == sizeof(TacSnapPreLucky) + MAX_UNITS * sizeof(int), "snap grew by lucky");
static_assert(sizeof(TacSnap) == sizeof(TacSnap6) + 9 * MAPN * MAPN + (LAY_N - 1) * MAPN * MAPN * sizeof(int) + sizeof(int) + MAX_UNITS * sizeof(int), "snap grew by cells, layers, lastActTeam, lucky");
static_assert(offsetof(TacSnap, b) == offsetof(TacSnapPreLucky, b), "snap header matches");
static_assert(offsetof(TacSnap, b) == offsetof(TacSnap6, b), "old snap header matches");
static int g_snapUse[SNAP_N];
static TacSnap* g_qMid = NULL;
static TacSnap* g_qAuto = NULL;
static HANDLE g_snapTh = NULL;
static HANDLE g_snapEv = NULL;
static volatile LONG g_snapBusy = 0;
static volatile LONG g_snapStop = 0;

static void SnapLock()
{
	if (!g_snapCsOk) { InitializeCriticalSection(&g_snapCs); g_snapCsOk = 1; }
	EnterCriticalSection(&g_snapCs);
}
static void SnapUnlock() { LeaveCriticalSection(&g_snapCs); }

static TacSnap* SnapTake()
{
	for (int i = 0; i < SNAP_N; i++) {
		if (g_snapUse[i]) continue;
		g_snapUse[i] = 1;
		return &g_snapPool[i];
	}
	return NULL;
}
static void SnapGive(TacSnap* s)
{
	if (!s) return;
	int i = (int)(s - g_snapPool);
	if (i < 0 || i >= SNAP_N) return;
	g_snapUse[i] = 0;
}

static void SnapWrite(TacSnap* job)
{
	if (!job) return;
	TCHAR leaf[96];
	_tcscpy_s(leaf, job->kind == 1 ? TacLeafMid() : TacLeafAuto());
	CString path = DatArc_Path(leaf);
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("wb")) == 0 && fp) {
		fwrite(job, 1, sizeof(*job), fp);
		fclose(fp);
		DatArc_Commit(leaf);
	}
	SnapLock();
	SnapGive(job);
	SnapUnlock();
}

static DWORD WINAPI SnapThread(void*)
{
	for (;;) {
		WaitForSingleObject(g_snapEv, INFINITE);
		if (g_snapStop) break;
		for (;;) {
			SnapLock();
			TacSnap* mid = g_qMid; g_qMid = NULL;
			TacSnap* aut = g_qAuto; g_qAuto = NULL;
			if (mid || aut) g_snapBusy = 1;
			SnapUnlock();
			if (!mid && !aut) { g_snapBusy = 0; break; }
			SnapWrite(mid);
			SnapWrite(aut);
			g_snapBusy = 0;
		}
	}
	return 0;
}

static void SnapKick()
{
	if (!g_snapTh) {
		g_snapEv = CreateEvent(NULL, FALSE, FALSE, NULL);
		g_snapTh = CreateThread(NULL, 0, SnapThread, NULL, 0, NULL);
	}
	if (g_snapEv) SetEvent(g_snapEv);
}

void Game::WaitSuspend()
{
	for (;;) {
		SnapLock();
		int pending = g_qMid || g_qAuto || g_snapBusy;
		SnapUnlock();
		if (!pending) break;
		Sleep(10);
	}
}

// 終了時だけ。書き出しが戻らなくても UI を止めない。スレッドは停止フラグで抜ける。
void Game::ShutdownSnap()
{
	InterlockedExchange(&g_snapStop, 1);
	if (g_snapEv) SetEvent(g_snapEv);
	const DWORD t0 = GetTickCount();
	for (;;) {
		SnapLock();
		int pending = g_qMid || g_qAuto || g_snapBusy;
		SnapUnlock();
		if (!pending) break;
		if (GetTickCount() - t0 > 2000) break;
		Sleep(10);
	}
	if (g_snapTh && WaitForSingleObject(g_snapTh, 500) == WAIT_OBJECT_0) {
		CloseHandle(g_snapTh);
		g_snapTh = NULL;
	}
}

static int ProbeLeaf(const TCHAR* leaf)
{
	if (!DatArc_Exists(leaf)) return 0;
	CString path = DatArc_Path(leaf);
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("rb")) != 0 || !fp) return 0;
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	DWORD magic = 0; int ver = 0, kind = 0, stage = 0;
	size_t n = fread(&magic, 1, sizeof(magic), fp);
	n += fread(&ver, 1, sizeof(ver), fp);
	n += fread(&kind, 1, sizeof(kind), fp);
	n += fread(&stage, 1, sizeof(stage), fp);
	fclose(fp);
	if (n < sizeof(magic) + sizeof(ver) * 3 || magic != kSnapMagic || ver != 1) return 0;
	if (sz != (long)sizeof(TacSnap) && sz != (long)sizeof(TacSnapPreLucky) && sz != (long)sizeof(TacSnap6)) return 0;
	return stage > 0 ? stage : 0;
}

static void LiftUnit(Unit& d, const UnitPreLucky& s)
{
	memset(&d, 0, sizeof(d));
	memcpy(&d, &s, offsetof(Unit, lucky));
	d.lucky = 0;
	memcpy(&d.ch, &s.ch, sizeof(UnitPreLucky) - offsetof(UnitPreLucky, ch));
}

static void SnapLift(TacSnap& d, const TacSnap6& s)
{
	memset(&d, 0, sizeof(d));
	d.magic = s.magic;
	d.version = s.version;
	d.kind = s.kind;
	d.stage = s.stage;
	d.rng = s.rng;
	d.cur = s.cur;
	d.turnOpen = s.turnOpen;
	d.phase = s.phase;
	d.moved = s.moved;
	d.x0 = s.x0; d.z0 = s.z0; d.deck0 = s.deck0; d.cx = s.cx; d.cz = s.cz;
	d.face0 = s.face0;
	d.camYaw = s.camYaw; d.camPitch = s.camPitch; d.camDist = s.camDist;
	d.camX = s.camX; d.camY = s.camY; d.camZ = s.camZ;
	for (int i = 0; i < 8; i++) d.itemN[i] = s.itemN[i];
	d.resultN = s.resultN;
	for (int i = 0; i < MAX_PARTY; i++) {
		d.resultLv0[i] = s.resultLv0[i];
		d.resultExp[i] = s.resultExp[i];
		d.resultRoster[i] = s.resultRoster[i];
	}
	for (int i = 0; i < MAX_UNITS; i++) d.expGain[i] = s.expGain[i];
	d.bossDown = s.bossDown;
	d.ai = s.ai;
	d.b.map.theme = s.b.map.theme;
	d.b.map.stage = s.b.map.stage;
	d.b.map.seed = s.b.map.seed;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		const Cell6& c = s.b.map.c[z][x];
		Cell& o = d.b.map.c[z][x];
		o.t = c.t; o.h = c.h; o.prop = c.prop; o.var = c.var; o.gimm = c.gimm; o.gparam = c.gparam;
		o.nLay = 0; o.t1 = o.h1 = o.t2 = o.h2 = o.t3 = o.h3 = o.t4 = o.h4 = 0;
	}
	memcpy(d.b.map.pStartX, s.b.map.pStartX, sizeof(d.b.map.pStartX));
	memcpy(d.b.map.pStartZ, s.b.map.pStartZ, sizeof(d.b.map.pStartZ));
	memcpy(d.b.map.eStartX, s.b.map.eStartX, sizeof(d.b.map.eStartX));
	memcpy(d.b.map.eStartZ, s.b.map.eStartZ, sizeof(d.b.map.eStartZ));
	d.b.map.nP = s.b.map.nP;
	d.b.map.nE = s.b.map.nE;
	memcpy(d.b.map.grp, s.b.map.grp, sizeof(d.b.map.grp));
	d.b.map.nGrp = s.b.map.nGrp;
	for (int i = 0; i < MAX_UNITS; i++) LiftUnit(d.b.u[i], s.b.u[i]);
	d.b.nUnits = s.b.nUnits;
	d.b.stage = s.b.stage;
	d.b.turnCount = s.b.turnCount;
	d.b.weather = s.b.weather;
	d.b.stampede = s.b.stampede;
	d.b.stampedeLeft = s.b.stampedeLeft;
	d.b.stampedeLine = s.b.stampedeLine;
	d.b.stampedeShow = s.b.stampedeShow;
	d.b.bgmWin = s.b.bgmWin;
	d.b.sweep = s.b.sweep;
	d.b.lastActTeam = -1;
	d.b.RebuildOcc();
}

static void SnapLiftPreLucky(TacSnap& d, const TacSnapPreLucky& s)
{
	memset(&d, 0, sizeof(d));
	memcpy(&d, &s, offsetof(TacSnap, b));
	d.b.map = s.b.map;
	for (int i = 0; i < MAX_UNITS; i++) LiftUnit(d.b.u[i], s.b.u[i]);
	d.b.nUnits = s.b.nUnits;
	d.b.stage = s.b.stage;
	d.b.turnCount = s.b.turnCount;
	d.b.weather = s.b.weather;
	d.b.stampede = s.b.stampede;
	d.b.stampedeLeft = s.b.stampedeLeft;
	d.b.stampedeLine = s.b.stampedeLine;
	d.b.stampedeShow = s.b.stampedeShow;
	d.b.bgmWin = s.b.bgmWin;
	d.b.sweep = s.b.sweep;
	d.b.lastActTeam = s.b.lastActTeam;
	d.b.RebuildOcc();
}

void Game::ProbeSuspend()
{
	SnapLock();
	const BOOL pending = g_qMid || g_qAuto || g_snapBusy;
	SnapUnlock();
	if (pending) return;
	m_midStage = ProbeLeaf(TacLeafMid());
	m_autoStage = ProbeLeaf(TacLeafAuto());
}

void Game::DropSuspend(int kind)
{
	WaitSuspend();
	DatArc_FlushSuspend(TRUE);
	if (kind == 0 || kind == 1) { DatArc_Delete(TacLeafMid()); m_midStage = 0; }
	if (kind == 0 || kind == 2) { DatArc_Delete(TacLeafAuto()); m_autoStage = 0; }
	DatArc_FlushSuspend(FALSE);
}

void Game::QueueSuspend(int kind)
{
	if (m_demo) return;
	if (m_screen != SC_BATTLE || m_b.nUnits < 1) return;
	SnapLock();
	TacSnap* s = (kind == 1) ? g_qMid : g_qAuto;
	if (s) {
		if (kind == 1) g_qMid = NULL;
		else g_qAuto = NULL;
	} else s = SnapTake();
	SnapUnlock();
	if (!s) return;
	memset(s, 0, sizeof(*s));
	s->magic = kSnapMagic;
	s->version = 1;
	s->kind = kind;
	s->stage = m_battleStage;
	s->rng = m_rng.s;
	s->cur = m_cur;
	s->turnOpen = m_turnOpen ? 1 : 0;
	s->phase = (m_phase == PH_MENU) ? (int)m_phaseBeforeMenu : (int)m_phase;
	if (s->phase == PH_P_ITEM) s->phase = PH_P_CMD;
	if (s->phase != PH_P_MOVE && s->phase != PH_P_CMD && s->phase != PH_P_SKILLS && s->phase != PH_P_TARGET
		&& s->phase != PH_P_SKILLTARGET && s->phase != PH_P_SHOVE && s->phase != PH_E_SHOW && s->phase != PH_P_SUMMON)
		s->phase = PH_P_MOVE;
	s->moved = m_moved;
	s->x0 = m_curX0; s->z0 = m_curZ0; s->deck0 = m_curDeck0; s->face0 = m_curFace0;
	s->cx = m_cx; s->cz = m_cz;
	s->camYaw = m_camYawGoal; s->camPitch = m_camPitch; s->camDist = m_camDistGoal;
	s->camX = m_camGoal.x; s->camY = m_camGoal.y; s->camZ = m_camGoal.z;
	for (int i = 0; i < 8; i++) s->itemN[i] = m_save.itemN[i];
	s->resultN = m_resultN;
	for (int i = 0; i < MAX_PARTY; i++) {
		s->resultLv0[i] = m_resultLv0[i];
		s->resultExp[i] = m_resultExp[i];
		s->resultRoster[i] = m_resultRoster[i];
	}
	for (int i = 0; i < MAX_UNITS; i++) s->expGain[i] = m_expGain[i];
	s->bossDown = m_bossDown;
	s->ai = m_ai;
	s->b = m_b;
	SnapLock();
	if (kind == 1) { g_qMid = s; m_midStage = m_battleStage; }
	else { g_qAuto = s; m_autoStage = m_battleStage; }
	SnapUnlock();
	SnapKick();
}

void Game::ResumeBattle(int kind)
{
	WaitSuspend();
	const TCHAR* leaf = kind == 1 ? TacLeafMid() : TacLeafAuto();
	TacSnap* s = &g_snapLoad;
	memset(s, 0, sizeof(*s));
	CString path = DatArc_Path(leaf);
	FILE* fp = NULL;
	int ok = 0;
	if (_tfopen_s(&fp, path, _T("rb")) == 0 && fp) {
		fseek(fp, 0, SEEK_END);
		long sz = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if (sz == (long)sizeof(TacSnap))
			ok = fread(s, 1, sizeof(*s), fp) == sizeof(*s);
		else if (sz == (long)sizeof(TacSnapPreLucky)) {
			ok = fread(&g_snapMid, 1, sizeof(g_snapMid), fp) == sizeof(g_snapMid);
			if (ok) SnapLiftPreLucky(*s, g_snapMid);
		}
		else if (sz == (long)sizeof(TacSnap6)) {
			ok = fread(&g_snapOld, 1, sizeof(g_snapOld), fp) == sizeof(g_snapOld);
			if (ok) SnapLift(*s, g_snapOld);
		}
		fclose(fp);
		if (ok) ok = s->magic == kSnapMagic && s->version == 1 && s->stage == m_save.stageSel;
	}
	if (!ok) { StartBattle(m_save.stageSel); return; }
	m_b = s->b;
	m_b.RebuildOcc();
	m_rng.s = s->rng ? s->rng : 1;
	m_battleStage = s->stage;
	for (int i = 0; i < 8; i++) m_save.itemN[i] = s->itemN[i];
	m_resultN = s->resultN;
	for (int i = 0; i < MAX_PARTY; i++) {
		m_resultLv0[i] = s->resultLv0[i];
		m_resultExp[i] = s->resultExp[i];
		m_resultRoster[i] = s->resultRoster[i];
	}
	for (int i = 0; i < MAX_UNITS; i++) m_expGain[i] = s->expGain[i];
	m_bossDown = s->bossDown;
	m_ai = s->ai;
	memset(m_visAil, 0, sizeof(m_visAil));
	memset(m_ailBlend, 0, sizeof(m_ailBlend));
	memset(m_ailBlendEl, 0, sizeof(m_ailBlendEl));
	m_ordSnapN = 0;
	m_ordLeave = -1;
	m_importBind = 0;
	LoadBattleCast();
	m_importBind = 1;
	if (m_showBuilt) m_r.ParkHomeLand(m_showStage, m_showTheme, m_mapSeed);
	m_r.BuildTerrain(m_b.map);
	m_showBuilt = FALSE;
	for (int i = 0; i < m_b.nUnits; i++) {
		Unit& u = m_b.u[i];
		if (u.team == TEAM_PLAYER) CharGrantReached(u.ch);
		ActorForUnit(i);
		if (u.alive && u.hp > 0 && u.rush && m_b.stampedeShow) ActorPlay(m_act[i], CL_SPAWN, 0, .05f);
		V3 p = UnitGroundPos(u);
		u.px = p.x; u.py = p.y; u.pz = p.z;
		u.hover = m_act[i].hover;
		if (u.alive) u.dissolve = 0;
		u.flash = 0;
		m_faceShow[i] = u.face;
		m_dispHp[i] = (float)u.hp;
		u.hpShow = (float)u.hp;
		m_dieT[i] = u.alive ? -1.f : 99.f;
		m_lunge[i] = 0;
	}
	for (int s2 = 0; s2 < Renderer::PORTRAIT_N; s2++) { m_portraitWant[s2] = 0; m_ptSlotUnit[s2] = -1; }
	for (int i = 0; i < MAX_UNITS; i++) m_ptForUnit[i] = -1;
	InvalidatePortraits();
	m_cur = s->cur;
	m_turnOpen = s->turnOpen ? TRUE : FALSE;
	m_moved = s->moved;
	m_curX0 = s->x0; m_curZ0 = s->z0; m_curDeck0 = s->deck0; m_curFace0 = s->face0;
	m_cx = s->cx; m_cz = s->cz; m_cLayer = 0; m_cursorValid = TRUE;
	m_camYaw = m_camYawGoal = s->camYaw;
	m_camPitch = s->camPitch;
	m_zoomHold = FALSE;
	m_wheelSign = 0;
	m_camDist = m_camDistGoal = m_zoomDist = s->camDist > 1.f ? s->camDist : 18.f;
	m_camTarget = m_camGoal = v3(s->camX, s->camY, s->camZ);
	m_camPan = v3(0, 0, 0);
	m_surveyN = 0;
	m_parts.clear(); m_popups.clear(); m_banners.clear(); m_deferPopups.clear();
	ClearRipples();
	memset(m_cover, 0, sizeof(m_cover));
	m_wxMesh = 0;
	m_wxEventT = m_rng.F(4.f, 9.f);
	m_boltT = 0; m_boltMag = 0;
	m_quakeT = 0; m_quakeMag = 0; m_quakeAcc = 0;
	m_hailBurstT = 0; m_hailAcc = 0;
	m_wxFlash = 0;
	m_seqCut = FALSE; m_cutStage = 0; m_victory = 0; m_hoverUnit = -1;
	m_lvqN = m_lvqCur = 0; m_promoAsk = 0; m_menuConfirm = 0; m_proj.active = 0;
	m_screen = SC_BATTLE;
	m_phase = (Phase)s->phase;
	m_phaseT = 0;
	m_phaseBeforeMenu = m_phase;
	memset(m_ovMove, 0, sizeof(m_ovMove));
	memset(m_ovAtk, 0, sizeof(m_ovAtk));
	memset(m_ovThreat, 0, sizeof(m_ovThreat));
	if (m_cur >= 0 && m_cur < m_b.nUnits && m_b.u[m_cur].alive && m_b.u[m_cur].team == TEAM_PLAYER)
		ComputeReach(m_b, m_cur, m_reach);
	ComputeOverlays();
	UpdateHover();
	int loadedKind = kind;
	DropSuspend(loadedKind);
	wchar_t t[64];
	StageCaption(m_battleStage, t, 64);
	AddBanner(t, TT(loadedKind == 1 ? TX_SV_FROMMID : TX_SV_FROMAUTO), RGBA8(255, 226, 150), 0, 2.f, MSG_FACE);
}

int Game::BattlePortrait(int unit) const
{
	if (m_screen != SC_BATTLE) return unit;
	if (unit < 0 || unit >= MAX_UNITS) return -1;
	return m_ptForUnit[unit];
}

void Game::AssignBattlePortraits()
{
	BYTE need[MAX_UNITS];
	memset(need, 0, sizeof(need));
	for (int i = 0; i < m_b.nUnits; i++) if (m_b.u[i].alive && m_b.u[i].team == TEAM_PLAYER) need[i] = 1;
	if (m_cur >= 0 && m_cur < m_b.nUnits) need[m_cur] = 1;
	if (m_hoverUnit >= 0 && m_hoverUnit < m_b.nUnits) need[m_hoverUnit] = 1;
	int order[12];
	int acting = (m_turnOpen && m_cur >= 0 && m_cur < m_b.nUnits && m_b.u[m_cur].alive) ? m_cur : -1;
	unsigned oh = (unsigned)acting * 10007u ^ (unsigned)m_b.nUnits;
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		oh = oh * 16777619u ^ (unsigned)u.ct;
		oh ^= ((unsigned)u.alive << 16) ^ ((unsigned)u.alert << 18) ^ ((unsigned)u.team << 20) ^ ((unsigned)u.slow << 8);
		oh ^= (unsigned)u.ch.stat[S_AGI];
	}
	static unsigned s_ordHash = 0;
	static int s_ordN = 0, s_ord[8];
	int n;
	if (s_ordHash == oh && s_ordN >= 0) {
		n = s_ordN;
		for (int i = 0; i < n && i < 8; i++) order[i] = s_ord[i];
	} else {
		n = m_b.PredictOrder(order, 8, acting, 1000, NULL);
		s_ordN = n;
		s_ordHash = oh;
		for (int i = 0; i < n && i < 8; i++) s_ord[i] = order[i];
	}
	for (int i = 0; i < n; i++) if (order[i] >= 0 && order[i] < m_b.nUnits) need[order[i]] = 1;
	if (m_lvqCur >= 0 && m_lvqCur < m_lvqN) {
		int u = m_lvq[m_lvqCur].unit;
		if (u >= 0 && u < m_b.nUnits) need[u] = 1;
	}
	for (int s = 0; s < Renderer::PORTRAIT_N; s++) {
		int u = m_ptSlotUnit[s];
		if (u < 0 || u >= m_b.nUnits || !need[u]) {
			if (u >= 0 && u < MAX_UNITS) m_ptForUnit[u] = -1;
			m_ptSlotUnit[s] = -1;
			m_portraitWant[s] = 0;
		} else {
			m_ptForUnit[u] = s;
			need[u] = 2;
			int want = 100 + u;
			if (m_portraitWant[s] != want) m_portraitWant[s] = want;
		}
	}
	for (int u = 0; u < m_b.nUnits; u++) {
		if (need[u] != 1) continue;
		int slot = -1;
		for (int s = 0; s < Renderer::PORTRAIT_N; s++) if (m_ptSlotUnit[s] < 0) { slot = s; break; }
		if (slot < 0) break;
		m_ptSlotUnit[slot] = u;
		m_ptForUnit[u] = slot;
		m_portraitWant[slot] = 100 + u;
	}
}

// ============================================================ 開始・終了
void Game::StartBattle(int stage)
{
	CharData chars[MAX_PARTY];
	int idx[MAX_PARTY];
	int n = 0;
	int cap = PartyCap();
	m_ordSnapN = 0;
	m_ordLeave = -1;
	for (int i = 0; i < cap; i++) {
		int r = m_save.party[i];
		if (r < 0 || !m_save.roster[r].used) continue;
		chars[n] = m_save.roster[r];
		idx[n] = r;
		n++;
	}
	if (!n) return;
	stage = Clampi(stage, 1, TAC_STAGE_ABS);
	RollMapSeed(stage, FALSE);
	DWORD seed = m_mapSeed;
	SetupBattle(m_b, stage, seed, chars, idx, n, m_themeSel);
	m_importBind = 0;
	LoadBattleCast();
	m_importBind = 1;
	m_battleStage = stage;
	m_logN = 0;
	if (m_showBuilt) m_r.ParkHomeLand(m_showStage, m_showTheme, m_mapSeed);
	m_r.BuildTerrain(m_b.map);
	m_showBuilt = FALSE;
	for (int i = 0; i < m_b.nUnits; i++) {
		Unit& u = m_b.u[i];
		ActorForUnit(i);
		V3 p = UnitGroundPos(u);
		u.px = p.x; u.py = p.y; u.pz = p.z;
		u.hpShow = (float)u.hp;
		u.dissolve = 0; u.flash = 0; u.hover = m_act[i].hover;
		m_faceShow[i] = u.face;
		m_dispHp[i] = (float)u.hp;
		m_dieT[i] = -1;
		m_expGain[i] = 0;
		m_lunge[i] = 0;
	}
	for (int s = 0; s < Renderer::PORTRAIT_N; s++) { m_portraitWant[s] = 0; m_ptSlotUnit[s] = -1; }
	for (int i = 0; i < MAX_UNITS; i++) m_ptForUnit[i] = -1;
	InvalidatePortraits();
	m_resultN = n;
	for (int i = 0; i < n; i++) { m_resultRoster[i] = idx[i]; m_resultLv0[i] = chars[i].lv; m_resultExp[i] = 0; }
	// カメラ: 自軍の背後から敵方向を見る（斜め 45 度へ寄せる）
	float px = 0, pz = 0, ex = 0, ez = 0;
	for (int i = 0; i < m_b.map.nP && i < 5; i++) { px += m_b.map.pStartX[i]; pz += m_b.map.pStartZ[i]; }
	px /= max(1, min(5, m_b.map.nP)); pz /= max(1, min(5, m_b.map.nP));
	if (m_b.map.nE > 0) { ex = (float)m_b.map.eStartX[0]; ez = (float)m_b.map.eStartZ[0]; }
	else { ex = px; ez = pz; }
	float yaw = atan2f(px - ex, pz - ez);
	yaw = floorf((yaw - PI * .25f) / (PI * .5f) + .5f) * (PI * .5f) + PI * .25f;
	m_camYaw = yaw - PI * .5f;
	m_camYawGoal = yaw;
	m_camPitch = .92f;
	m_zoomHold = FALSE;
	m_wheelSign = 0;
	m_camDist = 42.f;
	m_camDistGoal = 42.f;
	m_zoomDist = 18.f;
	m_camGoal = v3(px + .5f, m_b.map.StandY((int)px, (int)pz), pz + .5f);
	// 自軍から一番遠い敵拠点を起点に、引いたまま自軍まで滑らせる。
	{
		m_surveyN = 0;
		float best = 0.f;
		int bx = (int)px, bz = (int)pz;
		for (int g = 0; g < m_b.map.nGrp; g++) {
			float dx = m_b.map.grp[g].ax + .5f - px, dz = m_b.map.grp[g].az + .5f - pz;
			float d = dx * dx + dz * dz;
			if (d > best) { best = d; bx = m_b.map.grp[g].ax; bz = m_b.map.grp[g].az; }
		}
		if (best < 16.f) {
			for (int i = 0; i < m_b.nUnits; i++) {
				const Unit& eu = m_b.u[i];
				if (!eu.alive || eu.team != TEAM_ENEMY) continue;
				float dx = eu.px - px, dz = eu.pz - pz;
				float d = dx * dx + dz * dz;
				if (d > best) { best = d; bx = eu.x; bz = eu.z; }
			}
		}
		if (best >= 16.f && Map::In(bx, bz)) {
			m_surveyPt[0] = v3(bx + .5f, m_b.map.StandY(bx, bz), bz + .5f);
			m_surveyPt[1] = m_camGoal;
			m_surveyN = 2;
		}
	}
	m_camTarget = m_surveyN > 0 ? m_surveyPt[0] : m_camGoal;
	m_camPan = v3(0, 0, 0);
	m_parts.clear();
	m_popups.clear();
	m_banners.clear();
	m_deferPopups.clear();
	ClearRipples();
	memset(m_cover, 0, sizeof(m_cover));
	m_wxMesh = 0;
	m_wxEventT = m_rng.F(4.f, 9.f);
	m_boltT = 0; m_boltMag = 0;
	m_quakeT = 0; m_quakeMag = 0; m_quakeAcc = 0;
	m_hailBurstT = 0; m_hailAcc = 0;
	m_wxFlash = 0;
	m_seqCut = FALSE;
	m_cutStage = 0;
	m_bossDown = 0;
	m_victory = 0;
	m_cur = -1;
	m_curDeck0 = 0;
	m_cLayer = 0;
	m_turnOpen = FALSE;
	m_lastTeam = -1;
	m_hoverUnit = -1;
	m_lvqN = m_lvqCur = 0;
	m_promoAsk = 0;
	m_menuConfirm = 0;
	m_proj.active = 0;
	m_screen = SC_BATTLE;
	m_phase = PH_INTRO;
	m_phaseT = 0;
	memset(m_ovMove, 0, sizeof(m_ovMove));
	memset(m_ovAtk, 0, sizeof(m_ovAtk));
	memset(m_ovThreat, 0, sizeof(m_ovThreat));
	wchar_t t[64], sub[160];
	int title = 0;
	DWORD titleCol = RGBA8(255, 226, 150);
	if (m_b.sweep == TAC_VAR_SWEEP) { title = TX_BT_SWEEP; titleCol = RGBA8(150, 220, 170); }
	else if (m_b.sweep == TAC_VAR_REMNANT) { title = TX_BT_REMNANT; titleCol = RGBA8(220, 190, 120); }
	else if (m_b.sweep == TAC_VAR_BOSS) { title = TX_BT_BOSSMAP; titleCol = RGBA8(255, 170, 90); }
	else if (m_b.sweep == TAC_VAR_SCAR) { title = TX_BT_SCAR; titleCol = RGBA8(220, 140, 140); }
	else if (m_b.sweep == TAC_VAR_MIST) { title = TX_BT_MIST; titleCol = RGBA8(180, 200, 220); }
	else if (m_b.sweep == TAC_VAR_RAID) { title = TX_BT_RAID; titleCol = RGBA8(160, 170, 230); }
	else if (m_b.sweep == TAC_VAR_AMBUSH) { title = TX_BT_AMBUSH; titleCol = RGBA8(230, 160, 90); }
	else if (m_b.sweep == TAC_VAR_TWIN) { title = TX_BT_TWIN; titleCol = RGBA8(230, 120, 160); }
	else if (m_b.sweep == TAC_VAR_VAULT) { title = TX_BT_VAULT; titleCol = RGBA8(230, 200, 90); }
	else if (m_b.sweep == TAC_VAR_BLESS) { title = TX_BT_BLESS; titleCol = RGBA8(160, 220, 190); }
	else if (m_b.stampede) { title = TX_BT_STAMPEDE; titleCol = RGBA8(255, 150, 70); }
	if (title) {
		swprintf_s(t, L"%s", TT(title));
		wchar_t sn[48];
		StageCaption(stage, sn, 48);
		if (m_b.stampede && title != TX_BT_STAMPEDE)
			swprintf_s(sub, L"%s  —  %s  —  %s", sn, TT(ThemeNameTx(m_b.map.theme)), TT(TX_BT_STAMPEDE));
		else
			swprintf_s(sub, L"%s  —  %s", sn, TT(ThemeNameTx(m_b.map.theme)));
	} else {
		StageCaption(stage, t, 64);
		swprintf_s(sub, L"%s  —  %s", TT(ThemeNameTx(m_b.map.theme)), TT(TX_BT_GOAL));
	}
	AddBanner(t, sub, titleCol, 0, title ? 6.2f : 2.4f, MSG_DEPTH);
	TacSfx(TS_START);
}

static int NextJobGate(int lv)
{
	for (int t = 1; t < 12; t++) {
		int g = PromoteLevel(t);
		if (g > lv) return g;
	}
	return TAC_LV_MAX;
}

// 次の転職レベル（15、35、…）までを1枚にまとめる。経験値が尽きれば途中で止まる。
static void BurstToGate(Unit& u, int unitIndex, Rng& rng, long long& pool, LevelUpInfo* q, int& qn, int qcap)
{
	int guard = 0;
	while (pool >= ExpToNext(u.ch.lv) && u.ch.lv < TAC_LV_MAX && guard < TAC_LV_MAX) {
		int gate = NextJobGate(u.ch.lv);
		if (gate <= u.ch.lv) break;
		LevelUpInfo* Lp = NULL;
		if (q && qn < qcap) {
			Lp = &q[qn++];
			memset(Lp, 0, sizeof(*Lp));
			Lp->unit = unitIndex;
			Lp->oldLv = u.ch.lv;
			Lp->newLv = u.ch.lv;
		}
		int stepped = 0;
		while (pool >= ExpToNext(u.ch.lv) && u.ch.lv < gate && u.ch.lv < TAC_LV_MAX && guard < TAC_LV_MAX) {
			pool -= ExpToNext(u.ch.lv);
			int gains[S_COUNT];
			memset(gains, 0, sizeof(gains));
			int learned[4];
			int nLearn = CharLevelUp(u.ch, rng, gains, learned, 4);
			if (Lp) {
				for (int s = 0; s < S_COUNT; s++) Lp->gains[s] += gains[s];
				for (int k = 0; k < nLearn && Lp->nLearned < 4; k++) Lp->learned[Lp->nLearned++] = learned[k];
				Lp->newLv = u.ch.lv;
			}
			u.hp = min(u.ch.stat[S_HP], u.hp + gains[S_HP]);
			u.mp = min(u.ch.stat[S_MP], u.mp + gains[S_MP]);
			u.levelsGained++;
			stepped++;
			guard++;
		}
		if (!stepped) break;
	}
}

void Game::EndBattle(int victory)
{
	for (int i = 0; i < m_b.nUnits; i++) {
		Unit& u = m_b.u[i];
		if (u.team != TEAM_PLAYER) continue;
		if (u.rosterIdx < 0 || u.rosterIdx >= MAX_ROSTER) continue;
		if (victory) {
			int bonus = m_battleStage * 5;
			if (bonus > 0) {
				long long pool = (long long)u.ch.exp + bonus;
				BurstToGate(u, i, m_rng, pool, m_lvq, m_lvqN, MAX_PARTY * 4);
				if (pool > 2147483647LL) pool = 2147483647LL;
				u.ch.exp = (int)pool;
				long long acc = (long long)m_expGain[i] + bonus;
				if (acc > 2147483647LL) acc = 2147483647LL;
				m_expGain[i] = (int)acc;
			}
		}
		CharData c = u.ch;
		CharGrantReached(c);
		c.used = 1;
		c.battles++;
		m_save.roster[u.rosterIdx] = c;
		CraftSaveUnit(u);
		for (int k = 0; k < m_resultN; k++) if (m_resultRoster[k] == u.rosterIdx) m_resultExp[k] = m_expGain[i];
	}
	if (victory) {
		m_save.stageCleared = max(m_save.stageCleared, m_battleStage);
		m_save.stageSel = Clampi(m_battleStage + 1, 1, StagePlayMax(m_save.stageCleared));
		DropSuspend(0);
	}
	Persist();
	m_victory = victory;
	m_seqCut = FALSE;
	m_showBuilt = FALSE;
	BuildShowcase();
	for (int s = 0; s < Renderer::PORTRAIT_N; s++) m_portraitWant[s] = s < MAX_ROSTER ? s + 1 : 0;
	InvalidatePortraits();
	m_parts.clear();
	m_popups.clear();
	m_banners.clear();
	m_screen = SC_RESULT;
	m_focusId = victory ? 700 : 701;
	m_zoomHold = FALSE;
	m_wheelSign = 0;
	m_camDistGoal = m_camDist = m_zoomDist = 9.f;
	m_camPitch = .5f;
}

// ============================================================ 手番
void Game::BeginTurn()
{
	if (CheckBattleOver()) return;
	// 前の手番で気づいた敵を、次の行動者を決める前に順へ入れる。未発見（alert 0）は入れない。
	for (int i = 0; i < m_b.nUnits; i++)
		if (m_b.u[i].alive && m_b.u[i].team == TEAM_ENEMY && m_b.u[i].alert == 2) {
			m_b.u[i].alert = 1;
			JoinTurnClock(m_b, i);
		}
	int ui = m_b.NextActor();
	if (ui < 0) {
		if (m_demo) { DemoLeave(); return; }
		m_victory = 0; m_phase = PH_OVER; m_overT = 0; TacSfx(TS_LOSE); return;
	}
	m_cur = ui;
	m_turnOpen = TRUE;
	Unit& u = m_b.u[ui];
	int heal = 0, poison = 0, burn = 0, bleed = 0;
	TurnStartEffects(m_b, ui, &heal, &poison, &burn, &bleed);
	V3 hp = v3(u.px, u.py + m_act[ui].height + .2f, u.pz);
	wchar_t s[48];
	// 状態異常の DoT と同時の自然回復が「緑の+」に見えて異常が回復しているように誤解されるのを防ぐ。
	// DoT があるターンは回復ポップを抑え、被害側を先に見せる。
	int dotSum = poison + burn + bleed;
	int showHeal = heal;
	if (dotSum > 0) {
		showHeal = heal - dotSum;
		if (showHeal < 0) showHeal = 0;
	}
	if (poison > 0) {
		swprintf_s(s, L"-%s %s", TacNum(poison), EffectWord(EF_POISON));
		AddPopup(hp, s, RGBA8(190, 120, 255), 1, MSG_BURST);
		Burst(v3(u.px, u.py + .4f, u.pz), 14, 1.2f, .14f, 1.f, .6f, .3f, .9f, 0, 1, -1.f, 0, .8f);
		TacSfx(TS_POISON);
	}
	if (burn > 0) {
		swprintf_s(s, L"-%s %s", TacNum(burn), EffectWord(EF_BURN));
		AddPopup(hp, s, RGBA8(255, 120, 40), 1, MSG_BURST);
		Burst(v3(u.px, u.py + .5f, u.pz), 10, 1.4f, .12f, .8f, 1.f, .35f, .05f, 0, 1, -0.4f, 0, 1.1f);
	}
	if (bleed > 0) {
		swprintf_s(s, L"-%s %s", TacNum(bleed), EffectWord(EF_BLEED));
		AddPopup(hp, s, RGBA8(220, 40, 50), 1, MSG_BURST);
		Burst(v3(u.px, u.py + .3f, u.pz), 8, .6f, .08f, .7f, .75f, .05f, .05f, 0, 0, 1.2f, 0, .2f);
	}
	if (showHeal > 0) { swprintf_s(s, L"+%s", TacNum(showHeal)); AddPopup(hp, s, RGBA8(120, 255, 140), 0, MSG_RISE, dotSum > 0 ? .2f : 0.f); TacSfx(TS_HEAL); }
	m_dispHp[ui] = (float)u.hp;
	if (!u.alive || u.hp <= 0) {
		u.hp = 0; u.alive = 0; u.killed = 1;
		u.poison = u.burn = u.bleed = u.freeze = u.sleep = u.stun = u.confuse = u.slow = u.defDown = u.blind = u.silence = 0;
		if (m_act[ui].clip != CL_DEATH) ActorPlay(m_act[ui], CL_DEATH, 0, .08f);
		if (m_dieT[ui] < 0) m_dieT[ui] = 0;
		if (u.team == TEAM_ENEMY && u.boss) {
			int otherBoss = 0;
			for (int j = 0; j < m_b.nUnits; j++) {
				if (j == ui) continue;
				const Unit& o = m_b.u[j];
				if (o.alive && o.hp > 0 && o.team == TEAM_ENEMY && o.boss) otherBoss = 1;
			}
			if (!otherBoss) m_bossDown = 1;
		}
		// 状態異常での敵の死は撃破報酬は出す。経験値は攻撃側の行動にしか乗らない。
		if (u.team == TEAM_ENEMY) { GrantFoeLoot(ui, 0); TacSfx(TS_GET); }
		wchar_t nm[64], dead[96];
		UnitTitle(u, nm, 64);
		swprintf_s(dead, L"%s  %s", nm, TT(u.team == TEAM_PLAYER ? TX_RS_KO : TX_BT_DEFEATED));
		AddBanner(dead, NULL, u.team == TEAM_PLAYER ? RGBA8(255, 150, 140) : RGBA8(255, 226, 150), 2, 1.6f, u.team == TEAM_PLAYER ? MSG_DROP : MSG_BLUR);
		if (CheckBattleOver()) return;
		FinishUnitTurn(1);
		return;
	}
	m_curX0 = u.x; m_curZ0 = u.z; m_curDeck0 = u.deck; m_curFace0 = u.face;
	m_moved = 0;
	m_cmdSkill = -1;
	m_cmdFaced = 0;
	m_target = -1;
	m_pathN = 0;
	ComputeReach(m_b, ui, m_reach);
	m_camPan = v3(0, 0, 0);
	wchar_t nm[64];
	UnitTitle(u, nm, 64);
	wchar_t b[128];
	swprintf_s(b, L"%s  %s", TT(u.team == TEAM_PLAYER ? TX_BT_TURN_PLAYER : TX_BT_TURN_ENEMY), nm);
	AddBanner(b, NULL, u.team == TEAM_PLAYER ? RGBA8(140, 200, 255) : RGBA8(255, 140, 130), 2, 1.3f, MSG_FACE);
	m_lastTeam = u.team;
	if (u.team == TEAM_PLAYER) {
		int warned = WakeNearPlayers(m_b, 16);
		if (warned) AddBanner(TT(TX_BT_REIN), NULL, RGBA8(255, 210, 120), 1, 1.4f, MSG_DEPTH);
	}
	TacSfx(u.team == TEAM_PLAYER ? TS_TURN_P : TS_TURN_E);
	if (u.freeze > 0 || u.sleep > 0 || u.stun > 0) {
		const wchar_t* word = u.freeze > 0 ? EffectWord(EF_FREEZE) : (u.sleep > 0 ? TT(TX_BT_ASLEEP) : TT(TX_BT_STUN));
		DWORD col = u.freeze > 0 ? RGBA8(150, 210, 255) : (u.sleep > 0 ? RGBA8(170, 210, 255) : RGBA8(255, 220, 90));
		AddPopup(hp, word, col, 1, MSG_ORBIT);
		TacSfx(u.sleep > 0 ? TS_SLEEP : TS_STUN);
		if (u.freeze > 0) u.freeze--;
		else if (u.sleep > 0) u.sleep = 0;
		else u.stun = 0;
		m_moved = 1;
		m_phaseT = 0;
		QueueSuspend(2);
		FinishUnitTurn(1);
		return;
	}
	if (u.confuse > 0) {
		ConfusePlan(m_b, ui, m_ai, m_rng);
		if (u.confuse > 0) u.confuse--;
		AddPopup(hp, TT(TX_BT_CONFUSE), RGBA8(255, 150, 220), 1, MSG_ORBIT);
		TacSfx(TS_CONFUSE);
		m_phase = PH_E_SHOW;
	} else if (u.team == TEAM_PLAYER) {
		m_phase = PH_P_MOVE;
		m_cx = u.x; m_cz = u.z; m_cLayer = u.deck;
		m_cursorValid = TRUE;
		// デモ中はマウスの下へカーソルを吸い寄せない。盤の枠だけ後で進める。
		m_navMode = m_demo ? TRUE : FALSE;
		m_ai.wild = 0;
	} else {
		AiDecide(m_b, ui, m_ai, m_rng, TacAi(m_save));
		m_phase = PH_E_SHOW;
	}
	m_phaseT = 0;
	ComputeOverlays();
	UpdateHover();
	QueueSuspend(2);
}

BOOL Game::DemoEnding() const
{
	if (m_b.AliveCount(TEAM_PLAYER) <= 0) return TRUE;
	if (m_bossDown) return TRUE;
	int foes = m_b.AliveCount(TEAM_ENEMY);
	if (foes <= 0) return TRUE;
	if (foes > 1) return FALSE;
	int dead = 0, hp = 1, mx = 1;
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (u.team != TEAM_ENEMY) continue;
		if (!u.alive || u.hp <= 0) { dead++; continue; }
		hp = u.hp;
		mx = max(1, u.ch.stat[S_HP]);
	}
	// 誰か倒したあとの最後の1体、または最初から1体で体力が半分を切ったら、倒す前に帰る。
	if (dead > 0) return TRUE;
	return hp * 2 <= mx;
}

BOOL Game::CheckBattleOver()
{
	if (m_demo) {
		if (m_demo == 3) return TRUE;
		if (DemoEnding()) { DemoLeave(); return TRUE; }
		return FALSE;
	}
	if (m_phase == PH_OVER) return TRUE;
	int win = -1;
	if (m_bossDown || m_b.AliveCount(TEAM_ENEMY) == 0) win = 1;
	else if (m_b.AliveCount(TEAM_PLAYER) == 0) win = 0;
	if (win < 0) return FALSE;
	m_victory = win;
	m_phase = PH_OVER;
	m_overT = 0;
	AddBanner(TT(win ? TX_BT_VICTORY : TX_BT_DEFEAT), win && m_bossDown ? TT(TX_BT_BOSS) : NULL, win ? RGBA8(255, 226, 120) : RGBA8(190, 160, 220), 1, 2.8f, win ? MSG_DEPTH : MSG_DROP);
	TacSfx(win ? TS_WIN : TS_LOSE);
	if (win) {
		for (int i = 0; i < m_b.nUnits; i++) if (m_b.u[i].alive && m_b.u[i].hp > 0 && !PoseKeep(m_act[i], m_b.u[i], m_dieT[i], m_dispHp[i])) ActorPlay(m_act[i], CL_CHEER, 1, .2f);
		for (int i = 0; i < m_b.nUnits; i++) {
			Unit& u = m_b.u[i];
			if (!u.alive || u.team != TEAM_PLAYER) continue;
			Burst(v3(u.px, u.py + .6f, u.pz), 20, 2.f, .14f, 1.4f, 1.f, .9f, .5f, 2, 1, -1.f, 0, 1.2f);
		}
	}
	return TRUE;
}

void Game::DemoDrive(float dt)
{
	// マウスは動かさない。盤の選択枠と、メニューのハイライトだけを進める。
	if (m_demo != 2 || m_cur < 0 || m_cur >= m_b.nUnits) return;
	if (m_phase != m_demoPh) {
		m_demoPh = (int)m_phase;
		m_demoTick = 0;
		m_demoStep = 0;
		if (m_phase == PH_P_MOVE) m_demoAct = 0;
	}
	if (m_phase == PH_LEVELUP) {
		m_lvqN = 0;
		m_lvqCur = 0;
		m_promoAsk = 0;
		FinishUnitTurn(1);
		return;
	}
	if (m_phase == PH_MOVING || m_phase == PH_ACTION || m_phase == PH_E_MOVE || m_phase == PH_E_SHOW || m_phase == PH_INTRO)
		return;
	Unit& u = m_b.u[m_cur];
	if (u.team != TEAM_PLAYER) return;
	m_navMode = TRUE;
	if (m_phase == PH_P_MOVE) {
		if (m_demoAct == 0) {
			AiDecide(m_b, m_cur, m_ai, m_rng, 2);
			m_demoAct = 1;
			m_cx = u.x; m_cz = u.z; m_cLayer = u.deck; m_cursorValid = TRUE;
		}
		m_demoStep += dt;
		if (m_demoStep < 0.16f) return;
		m_demoStep = 0;
		int gx = m_ai.moveX, gz = m_ai.moveZ;
		if (!Map::In(gx, gz)) { gx = u.x; gz = u.z; }
		int n = ReachPath(m_reach, u.x, u.z, gx, gz, m_path, MAPN * 4, u.deck);
		auto arrive = [&]() {
			if ((m_cx != u.x || m_cz != u.z) && ReachCanStopLayer(m_reach, m_cx, m_cz, m_cLayer)) {
				m_moved = 1;
				StartMove(m_cur, m_cx, m_cz, PH_P_CMD);
			} else OpenCommand();
			m_demoAct = 2;
		};
		if (n <= 1 || (m_cx == gx && m_cz == gz)) { arrive(); return; }
		int at = -1;
		for (int i = 0; i < n; i++) if (PackX(m_path[i]) == m_cx && PackZ(m_path[i]) == m_cz) { at = i; break; }
		int nxt = at >= 0 ? at + 1 : 1;
		if (nxt >= n) { arrive(); return; }
		m_cx = PackX(m_path[nxt]);
		m_cz = PackZ(m_path[nxt]);
		m_cLayer = PackDeck(m_path[nxt]);
		m_cursorValid = TRUE;
		TacSfx(TS_CURSOR);
		UpdateHover();
		V3 cp = CellCenter(m_cx, m_cz);
		V3 d = cp - v3(u.px, u.py, u.pz);
		float L = Len(v3(d.x, 0, d.z));
		if (L > 4.f) m_camPan = v3(d.x, 0, d.z) * ((L - 4.f) / L);
		else m_camPan = v3(0, 0, 0);
		return;
	}
	if (m_phase == PH_P_CMD) {
		BOOL skillOk = m_ai.skill >= 0 && !IsSummonId(m_ai.skill) && SkillWorth(m_b, m_cur, m_ai.skill);
		int choice = skillOk ? 1 : (m_ai.target >= 0 ? 0 : 2);
		int show = m_demoTick < 2 ? m_demoTick : choice;
		if (show == 0) m_focusId = ID_BT_ATTACK;
		else if (show == 1) m_focusId = ID_BT_SKILL;
		else m_focusId = ID_BT_WAIT;
		m_demoStep += dt;
		if (m_demoStep < 0.34f) return;
		m_demoStep = 0;
		m_demoTick++;
		if (m_demoTick < 3) return;
		if (choice == 1) {
			int sk = m_ai.skill;
			m_skillShape = 0;
			m_skillEl = 0;
			int before = 0;
			if (IsCraftId(sk)) {
				m_skillTab = 2;
				for (int i = 0; i < CRAFT_SLOT_N; i++) {
					if (u.craftEq[i] < CRAFT_BASE) continue;
					if (u.craftEq[i] == sk) break;
					before++;
				}
			} else if (IsFormSkill(sk)) {
				m_skillTab = 3;
				before = sk - SKL_FORM0;
				if (before < 0) before = 0;
			} else {
				const SkillDef& sd = SkillInfo(sk);
				m_skillTab = (sd.kind == SK_HEAL || sd.kind == SK_BUFF) ? 1 : 0;
				for (int s = 0; s < sk; s++) {
					if (IsFormSkill(s) || !UnitHasSkill(u, s)) continue;
					const SkillDef& od = SkillInfo(s);
					if (od.kind == SK_PASSIVE) continue;
					if (m_skillTab == 0 && od.kind != SK_PHYS && od.kind != SK_MAGIC) continue;
					if (m_skillTab == 1 && od.kind != SK_HEAL && od.kind != SK_BUFF) continue;
					before++;
				}
			}
			int per = 8;
			float bh = min(U(40.f), max(18.f, (float)m_h * 0.055f));
			int fit = (int)(((float)m_h - U(220.f)) / (bh + U(4.f)));
			if (fit < per) per = max(3, fit);
			if (per < 1) per = 1;
			m_skillPage = before / per;
			m_phase = PH_P_SKILLS;
			m_navMode = TRUE;
			m_focusId = ID_BT_SKTAB + m_skillTab;
			return;
		}
		if (choice == 0) {
			m_phase = PH_P_TARGET;
			m_target = m_ai.target;
			m_navMode = TRUE;
			ComputeOverlays();
			m_cx = u.x; m_cz = u.z; m_cLayer = u.deck; m_cursorValid = TRUE;
			return;
		}
		if (!m_moved && u.buffDef < 1) u.buffDef = 1;
		if (!m_moved) {
			AddPopup(v3(u.px, u.py + 1.f, u.pz), TT(TX_BT_GUARD), RGBA8(180, 210, 255), 1, MSG_FACE);
			AddLog(TT(TX_BT_GUARD), RGBA8(180, 210, 255));
		}
		FinishUnitTurn(0);
		return;
	}
	if (m_phase == PH_P_SKILLS) {
		int sk = m_ai.skill;
		int bid = ID_BT_SK0 + sk;
		if (IsCraftId(sk)) {
			int slot = 0;
			for (int i = 0; i < CRAFT_SLOT_N; i++) if (u.craftEq[i] == sk) slot = i;
			bid = ID_BT_CRAFT0 + slot;
		} else if (IsFormSkill(sk)) bid = ID_BT_FORM0 + (sk - SKL_FORM0);
		m_focusId = m_demoTick == 0 ? (ID_BT_SKTAB + m_skillTab) : bid;
		m_demoStep += dt;
		if (m_demoStep < 0.42f) return;
		m_demoStep = 0;
		m_demoTick++;
		if (m_demoTick < 2) return;
		m_cmdSkill = sk;
		m_phase = PH_P_SKILLTARGET;
		m_navMode = TRUE;
		if (IsFormSkill(sk) && !m_cmdFaced) { m_cmdFace0 = u.face; m_cmdFaced = 1; }
		ComputeOverlays();
		PickSkillCursor(sk);
		AimFormFace();
		m_ai.castX = m_cx;
		m_ai.castZ = m_cz;
		m_cx = u.x; m_cz = u.z; m_cLayer = u.deck; m_cursorValid = TRUE;
		return;
	}
	if (m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET) {
		if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0 && SkillInfo(m_cmdSkill).rMax == 0) {
			m_demoStep += dt;
			if (m_demoStep < 0.28f) return;
			BattleConfirmCursor();
			if (m_phase == PH_P_SKILLTARGET) FinishUnitTurn(1);
			return;
		}
		int tx = m_ai.castX, tz = m_ai.castZ;
		if (m_phase == PH_P_TARGET && m_ai.target >= 0 && m_ai.target < m_b.nUnits) {
			tx = m_b.u[m_ai.target].x;
			tz = m_b.u[m_ai.target].z;
		}
		if (!Map::In(tx, tz)) { tx = m_cx; tz = m_cz; }
		m_demoStep += dt;
		if (m_demoStep < 0.12f) return;
		m_demoStep = 0;
		if (m_cx == tx && m_cz == tz) {
			BattleConfirmCursor();
			if (m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET) FinishUnitTurn(1);
			return;
		}
		if (m_cx < tx) m_cx++;
		else if (m_cx > tx) m_cx--;
		else if (m_cz < tz) m_cz++;
		else if (m_cz > tz) m_cz--;
		m_cursorValid = TRUE;
		TacSfx(TS_CURSOR);
		UpdateHover();
		return;
	}
	m_demoStep += dt;
	if (m_demoStep > 1.2f) FinishUnitTurn(1);
}

void Game::ConfuseSelf(int ui)
{
	if (ui < 0 || ui >= m_b.nUnits) { FinishUnitTurn(1); return; }
	Unit& u = m_b.u[ui];
	int dmg = max(1, (UnitStat(u, S_STR) + UnitWeaponPow(u)) / 4);
	u.hp -= dmg;
	u.sleep = 0;
	wchar_t s[32];
	swprintf_s(s, L"-%s", TacNum(dmg));
	AddPopup(v3(u.px, u.py + m_act[ui].height, u.pz), s, RGBA8(255, 160, 120), 1, MSG_BURST);
	wchar_t nm[40], line[80];
	UnitTitle(u, nm, 40);
	swprintf_s(line, L"%s  %s  -%d", nm, EffectWord(EF_CONFUSE), dmg);
	AddLog(line, RGBA8(255, 160, 180));
	TacSfx(u.team == TEAM_PLAYER ? TS_HURT : TS_HIT);
	if (u.hp <= 0) {
		u.hp = 0;
		u.confuse = 0;
		u.stun = 0;
		TacSfx(u.team == TEAM_PLAYER ? TS_FALL : TS_DEATH);
		m_strkN = 0;
		FinishSeq();
		return;
	}
	ActorPlay(m_act[ui], CL_HIT_A, 0, .06f);
	FinishUnitTurn(1);
}

void Game::FinishUnitTurn(int acted)
{
	// 溶岩は移動し終えて手番を閉じた位置で焼く。味方にも敵にも同じ。
	if (m_cur >= 0 && m_cur < m_b.nUnits && m_b.u[m_cur].alive && m_b.u[m_cur].hp > 0) {
		int ui = m_cur;
		Unit& u = m_b.u[ui];
		int lava = LavaHurt(m_b, ui);
		if (lava > 0) {
			V3 hp = v3(u.px, u.py + m_act[ui].height + .2f, u.pz);
			wchar_t s[48];
			swprintf_s(s, L"-%s %s", TacNum(lava), TT(TX_TER_LAVA));
			AddPopup(hp, s, RGBA8(255, 120, 40), 1, MSG_BURST);
			m_dispHp[ui] = (float)u.hp;
			if (!u.alive || u.hp <= 0) {
				u.hp = 0; u.alive = 0; u.killed = 1;
				if (m_act[ui].clip != CL_DEATH) ActorPlay(m_act[ui], CL_DEATH, 0, .08f);
				if (m_dieT[ui] < 0) m_dieT[ui] = 0;
				TacSfx(TS_FALL);
			} else TacSfx(TS_HURT);
		}
	}
	if (m_cur >= 0) FinishAction(m_b, m_cur, m_moved, acted);
	m_turnOpen = FALSE;
	if (CheckBattleOver()) return;
	RaiseStampede();
	m_phase = PH_END;
	m_phaseT = 0;
}

void Game::RaiseStampede()
{
	int watch = -1;
	float watchD = 1.0e9f;
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& a = m_b.u[i];
		if (!a.alive || a.team != TEAM_PLAYER) continue;
		float dx = a.px - m_camTarget.x, dz = a.pz - m_camTarget.z;
		float d = dx * dx + dz * dz;
		if (d < watchD) { watchD = d; watch = i; }
	}
	int idx[8];
	int n = SpawnStampede(m_b, m_rng, watch, idx, 8);
	if (n <= 0) return;
	m_importBind = 0;
	LoadBattleCast();
	m_importBind = 1;
	for (int i = 0; i < n; i++) {
		int ui = idx[i];
		Unit& u = m_b.u[ui];
		ActorForUnit(ui);
		ActorPlay(m_act[ui], CL_SPAWN, 0, .05f);
		V3 p = UnitGroundPos(u);
		u.px = p.x; u.py = p.y; u.pz = p.z;
		u.hpShow = (float)u.hp;
		u.dissolve = 0;
		u.flash = 1.f;
		u.hover = m_act[ui].hover;
		m_faceShow[ui] = u.face;
		m_dispHp[ui] = (float)u.hp;
		m_dieT[ui] = -1.f;
		m_expGain[ui] = 0;
		m_lunge[ui] = 0;
		Burst(p + v3(0, .15f, 0), 18, 2.4f, .13f, .9f, .9f, .45f, .16f, 0, 1, -.6f, 0, .7f);
	}
	m_cur = idx[0];
	AddBanner(TT(TX_BT_STAMPEDE_GO), NULL, RGBA8(255, 140, 60), 1, 2.2f, MSG_BLUR);
	AddLog(TT(TX_BT_STAMPEDE_GO), RGBA8(255, 170, 90));
	TacSfx(TS_START);
	ComputeOverlays();
}

// ============================================================ 範囲表示
static int CorpseAt(const Battle& b, int x, int z);

// 地図の外は奈落で、どのユニットも止まれない。射程の色は置かない。
static BOOL TacOffMap(const Cell& c)
{
	return c.t == T_VOID;
}

void Game::ComputeOverlays()
{
	memset(m_ovMove, 0, sizeof(m_ovMove));
	memset(m_ovAtk, 0, sizeof(m_ovAtk));
	memset(m_ovAtkLay, 0, sizeof(m_ovAtkLay));
	memset(m_ovThreat, 0, sizeof(m_ovThreat));
	m_ovDirty = TRUE;
	if (m_cur < 0) return;
	const Unit& u = m_b.u[m_cur];
	if (m_phase == PH_P_MOVE || m_phase == PH_E_SHOW) {
		int rmin = UnitRangeMin(u), rmax = UnitRangeMax(u);
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			if (!ReachCanStop(m_reach, x, z)) continue;
			m_ovMove[z][x] = 1;
		}
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			for (int Ls = 0; Ls < LAY_N; Ls++) {
				if (!ReachCanStopLayer(m_reach, x, z, Ls)) continue;
				for (int dz = -rmax; dz <= rmax; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
					int d = abs(dx) + abs(dz);
					if (d < rmin || d > rmax) continue;
					int tx = x + dx, tz = z + dz;
					if (!Map::In(tx, tz) || TacOffMap(m_b.map.At(tx, tz))) continue;
					for (int Lt = 0; Lt < LAY_N; Lt++) {
						if (ReachCanStopLayer(m_reach, tx, tz, Lt)) continue;
						if (!AttackReachLayer(m_b, x, z, Ls, rmin, rmax, tx, tz, Lt, false)) continue;
						if (LayerTr(m_b.map.At(tx, tz), Lt) == T_CHASM) continue;
						m_ovAtkLay[tz][tx] |= (BYTE)(1 << Lt);
						if (!m_ovAtk[tz][tx]) m_ovAtk[tz][tx] = 1;
					}
				}
			}
		}
		if (m_phase == PH_P_MOVE && u.team == TEAM_PLAYER) {
			static const int cdx[4] = { 1, -1, 0, 0 }, cdz[4] = { 0, 0, 1, -1 };
			for (int k = 0; k < 4; k++) {
				int tx = u.x + cdx[k], tz = u.z + cdz[k];
				if (CanTerrace(m_b, m_cur, tx, tz)) m_ovAtk[tz][tx] = 5;
			}
			for (int i = 0; i < m_b.nUnits; i++) {
				if (!CanTarget(m_cur, i, -1)) continue;
				const Unit& e = m_b.u[i];
				int sp = UnitSpan(e);
				if (sp < 1) sp = 1;
				int L = e.deck;
				if (L < 0 || L >= LAY_N || !LayerOn(m_b.map.At(e.x, e.z), L)) L = 0;
				for (int dz = 0; dz < sp; dz++) for (int dx = 0; dx < sp; dx++) {
					int tx = e.x + dx, tz = e.z + dz;
					if (!Map::In(tx, tz) || TacOffMap(m_b.map.At(tx, tz))) continue;
					m_ovAtkLay[tz][tx] |= (BYTE)(1 << L);
					m_ovAtk[tz][tx] = 7;
				}
			}
		}
	} else if (m_phase == PH_P_TARGET || m_phase == PH_P_CMD) {
		int rmin = UnitRangeMin(u), rmax = UnitRangeMax(u);
		int aL = LayerOn(m_b.map.At(u.x, u.z), u.deck) ? u.deck : 0;
		for (int dz = -rmax; dz <= rmax; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
			int tx = u.x + dx, tz = u.z + dz;
			if (!Map::In(tx, tz) || TacOffMap(m_b.map.At(tx, tz))) continue;
			BYTE bits = 0;
			for (int L = 0; L < LAY_N; L++)
				if (AttackReachLayer(m_b, u.x, u.z, aL, rmin, rmax, tx, tz, L, true)) bits |= (BYTE)(1 << L);
			if (!bits) continue;
			m_ovAtkLay[tz][tx] |= bits;
			m_ovAtk[tz][tx] = m_phase == PH_P_CMD ? 2 : 1;
		}
	} else if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0) {
		const SkillDef& sd = SkillInfo(m_cmdSkill);
		int rmin = SkillCastRangeMin(u, m_cmdSkill), rmax = SkillCastRangeMax(u, m_cmdSkill);
		int aL = LayerOn(m_b.map.At(u.x, u.z), u.deck) ? u.deck : 0;
		if (sd.rMax == 0) {
			for (int dz = -sd.area; dz <= sd.area; dz++) for (int dx = -sd.area; dx <= sd.area; dx++) {
				if (abs(dx) + abs(dz) > sd.area) continue;
				int tx = u.x + dx, tz = u.z + dz;
				if (!Map::In(tx, tz) || TacOffMap(m_b.map.At(tx, tz))) continue;
				BYTE bits = 0;
				for (int L = 0; L < LAY_N; L++) if (LayerOn(m_b.map.At(tx, tz), L) && LayerTr(m_b.map.At(tx, tz), L) != T_VOID) bits |= (BYTE)(1 << L);
				if (!bits) continue;
				m_ovAtkLay[tz][tx] |= bits;
				m_ovAtk[tz][tx] = 3;
			}
		} else {
			BOOL friendly = sd.kind == SK_HEAL || sd.kind == SK_BUFF;
			for (int dz = -rmax; dz <= rmax; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
				int tx = u.x + dx, tz = u.z + dz;
				if (!Map::In(tx, tz) || TacOffMap(m_b.map.At(tx, tz))) continue;
				if (IsFormSkill(m_cmdSkill) && !FormFits(m_b, m_cur, m_cmdSkill, tx, tz)) continue;
				BYTE bits = 0;
				for (int L = 0; L < LAY_N; L++)
					if (AttackReachLayer(m_b, u.x, u.z, aL, rmin, rmax, tx, tz, L, friendly ? false : true)) bits |= (BYTE)(1 << L);
				if (!bits) continue;
				m_ovAtkLay[tz][tx] |= bits;
				m_ovAtk[tz][tx] = friendly ? 4 : 1;
			}
			if (sd.effect == EF_RAISE) {
				int sp = UnitSpan(u);
				for (int dz = 0; dz < sp; dz++) for (int dx = 0; dx < sp; dx++) {
					int tx = u.x + dx, tz = u.z + dz;
					int c = Map::In(tx, tz) ? CorpseAt(m_b, tx, tz) : -1;
					if (c >= 0 && CanTarget(m_cur, c, m_cmdSkill)) m_ovAtk[tz][tx] = 4;
				}
			}
		}
	}
	if (u.team == TEAM_PLAYER && (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_CMD)) {
		int rmin = UnitRangeMin(u), rmax = UnitRangeMax(u);
		for (int dz = -rmax; dz <= rmax; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
			int tx = u.x + dx, tz = u.z + dz;
			if (ChestInRange(m_cur, tx, tz)) m_ovAtk[tz][tx] = 6;
		}
	}
	memset(m_ovThreat, 0, sizeof(m_ovThreat));
	if (TacThreatOn(m_save)) {
		static Reach tr;
		for (int i = 0; i < m_b.nUnits; i++) {
			const Unit& e = m_b.u[i];
			if (!e.alive || e.team != TEAM_ENEMY || e.alert != 1) continue;
			ComputeReach(m_b, i, tr);
			int rmin = UnitRangeMin(e), rmax = UnitRangeMax(e);
			if (rmax < rmin) continue;
			for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
				if (!ReachCanStop(tr, x, z)) continue;
				for (int dz = -rmax; dz <= rmax; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
					int d = abs(dx) + abs(dz);
					if (d < rmin || d > rmax) continue;
					int tx = x + dx, tz = z + dz;
					if (!Map::In(tx, tz) || TacOffMap(m_b.map.At(tx, tz))) continue;
					if (m_b.map.At(tx, tz).t == T_CHASM) continue;
					m_ovThreat[tz][tx] = 1;
				}
			}
		}
	}
}

static void AvxCopy(void* dst, const void* src, size_t n)
{
	SimdKernels::Get().Copy(dst, src, n);
}

struct OvWork {
	void (*fn)(void*);
	void* arg;
	HANDLE go, done;
	volatile LONG quit;
};
static OvWork g_ovW[2];
static int g_ovUp = 0;
static DWORD WINAPI OvThread(LPVOID p)
{
	OvWork* w = (OvWork*)p;
	for (;;) {
		WaitForSingleObject(w->go, INFINITE);
		if (w->quit) break;
		if (w->fn) w->fn(w->arg);
		SetEvent(w->done);
	}
	return 0;
}
static void OvEnsure()
{
	if (g_ovUp) return;
	for (int i = 0; i < 2; i++) {
		g_ovW[i].fn = NULL;
		g_ovW[i].arg = NULL;
		g_ovW[i].quit = 0;
		g_ovW[i].go = CreateEvent(NULL, FALSE, FALSE, NULL);
		g_ovW[i].done = CreateEvent(NULL, FALSE, FALSE, NULL);
		HANDLE th = CreateThread(NULL, 0, OvThread, &g_ovW[i], 0, NULL);
		if (th) {
			SetThreadPriority(th, THREAD_PRIORITY_ABOVE_NORMAL);
			CloseHandle(th);
		}
	}
	g_ovUp = 1;
}
static void OvRun2(void (*a)(void*), void* aa, void (*b)(void*), void* bb)
{
	OvEnsure();
	g_ovW[0].fn = a; g_ovW[0].arg = aa;
	g_ovW[1].fn = b; g_ovW[1].arg = bb;
	SetEvent(g_ovW[0].go);
	SetEvent(g_ovW[1].go);
	HANDLE hs[2] = { g_ovW[0].done, g_ovW[1].done };
	WaitForMultipleObjects(2, hs, TRUE, INFINITE);
}

int Game::AppendOverlayQuad(VtxS* dst, int n, int cap, int x, int z, DWORD c, float lift, float yAbs, int layer, int segs) const
{
	if (!dst || n >= cap) return n;
	const Map& m = m_b.map;
	const Cell& cc = m.At(x, z);
	int kind = 0, ly = 0;
	if (layer > 0) { kind = 2; ly = layer; }
	else if (yAbs < -900.f && (cc.t == T_WATER || cc.t == T_LAVA)) { kind = 1; ly = (int)cc.t; }
	float mid = TacVisY(m, x, z, x + 0.5f, z + 0.5f, kind, ly);
	float base = yAbs > -900.f ? yAbs : mid;
	if (kind == 1 && yAbs < -900.f) base = m.StandY(x, z) - 0.25f;
	auto hy = [&](float px, float pz) {
		return base + (TacVisY(m, x, z, px, pz, kind, ly) - mid) + lift + 0.1f;
	};
	if (segs < 1) segs = 1;
	if (segs > 8) segs = 8;
	float in = 0.012f;
	if (segs > 1) {
		float y0 = hy((float)x + in, (float)z + in);
		float y1 = hy((float)x + 1.f - in, (float)z + in);
		float y2 = hy((float)x + 1.f - in, (float)z + 1.f - in);
		float y3 = hy((float)x + in, (float)z + 1.f - in);
		float lo = y0, hi = y0;
		if (y1 < lo) lo = y1; if (y1 > hi) hi = y1;
		if (y2 < lo) lo = y2; if (y2 > hi) hi = y2;
		if (y3 < lo) lo = y3; if (y3 > hi) hi = y3;
		float span = hi - lo;
	if (span < 0.02f) segs = 1;
	else if (span < 0.08f && segs > 4) segs = 4;
	}
	for (int j = 0; j < segs; j++) for (int i = 0; i < segs; i++) {
		if (n + 4 > cap) return n;
		float u0 = (float)i / (float)segs, u1 = (float)(i + 1) / (float)segs;
		float v0 = (float)j / (float)segs, v1 = (float)(j + 1) / (float)segs;
		float px0 = (float)x + in + u0 * (1.f - in * 2.f);
		float px1 = (float)x + in + u1 * (1.f - in * 2.f);
		float pz0 = (float)z + in + v0 * (1.f - in * 2.f);
		float pz1 = (float)z + in + v1 * (1.f - in * 2.f);
		float xy[4] = { px0, px0, px1, px1 };
		float zy[4] = { pz0, pz1, pz1, pz0 };
		float uvx[4] = { u0, u0, u1, u1 };
		float uvy[4] = { v0, v1, v1, v0 };
		for (int k = 0; k < 4; k++) {
			VtxS& vt = dst[n++];
			vt.p[0] = xy[k]; vt.p[1] = hy(xy[k], zy[k]); vt.p[2] = zy[k];
			vt.n[0] = 0; vt.n[1] = 1; vt.n[2] = 0;
			vt.uv[0] = uvx[k]; vt.uv[1] = uvy[k];
			vt.c = c;
		}
	}
	return n;
}

int Game::BuildRangeBand(int z0, int z1, const BYTE* block, VtxS* dst, int cap) const
{
	if (!dst || !block || m_cur < 0 || m_cur >= m_b.nUnits) return 0;
	const Map& m = m_b.map;
	const Unit& u = m_b.u[m_cur];
	BOOL showMove = m_phase == PH_P_MOVE || m_phase == PH_E_SHOW || m_phase == PH_E_MOVE;
	if (z0 < 0) z0 = 0;
	if (z1 > MAPN) z1 = MAPN;
	int n = 0;
	for (int z = z0; z < z1; z++) for (int x = 0; x < MAPN; x++) {
		if (showMove) {
			for (int L = 0; L < LAY_N; L++) {
				if (ReachCostAt(m_reach, L, x, z) < 0 || !LayerOn(m.At(x, z), L) || TacOffMap(m.At(x, z))) continue;
				int a = (L == m_cLayer) ? 188 : 110;
				int blocked = block[((L * MAPN + z) * MAPN) + x];
				DWORD moveCol = blocked ? RGBA8(220, 36, 36, a)
					: (u.team == TEAM_PLAYER ? RGBA8(70, 150, 255, a) : RGBA8(255, 90, 150, a));
				n = AppendOverlayQuad(dst, n, cap, x, z, moveCol, .03f, m.WalkY(x, z, L), L, 6);
			}
		}
		if (showMove && m_ovAtk[z][x] == 7 && !TacOffMap(m.At(x, z))) {
			BYTE bits = m_ovAtkLay[z][x];
			DWORD col = RGBA8(255, 78, 36, 210);
			if (!bits) {
				float y = (m_cLayer > 0 && Map::In(x, z) && LayerOn(m.At(x, z), m_cLayer)) ? m.WalkY(x, z, m_cLayer) : -999.f;
				n = AppendOverlayQuad(dst, n, cap, x, z, col, .05f, y, y > -900.f ? m_cLayer : -1, 6);
			} else for (int L = 0; L < LAY_N; L++) {
				if (!(bits & (BYTE)(1 << L)) || !LayerOn(m.At(x, z), L)) continue;
				n = AppendOverlayQuad(dst, n, cap, x, z, col, .05f, m.WalkY(x, z, L), L, 6);
			}
		}
		if (!TacOffMap(m.At(x, z)) && m_ovAtk[z][x] == 6) {
			BYTE bits = m_ovAtkLay[z][x];
			if (!bits) {
				float y = (m_cLayer > 0 && Map::In(x, z) && LayerOn(m.At(x, z), m_cLayer)) ? m.WalkY(x, z, m_cLayer) : -999.f;
				n = AppendOverlayQuad(dst, n, cap, x, z, RGBA8(255, 210, 80, 210), .05f, y, y > -900.f ? m_cLayer : -1, 4);
			} else for (int L = 0; L < LAY_N; L++) {
				if (!(bits & (BYTE)(1 << L)) || !LayerOn(m.At(x, z), L)) continue;
				n = AppendOverlayQuad(dst, n, cap, x, z, RGBA8(255, 210, 80, 210), .05f, m.WalkY(x, z, L), L, 4);
			}
		} else if (!showMove && m_ovAtk[z][x] && !TacOffMap(m.At(x, z))) {
			DWORD col;
			switch (m_ovAtk[z][x]) {
			case 2: col = RGBA8(255, 90, 60, 70); break;
			case 3: col = RGBA8(255, 200, 60, 150); break;
			case 4: col = RGBA8(90, 255, 150, 130); break;
			case 5: col = RGBA8(255, 228, 170, 170); break;
			default: col = RGBA8(255, 80, 60, 125); break;
			}
			if (showMove && u.team != TEAM_PLAYER) col = RGBA8(255, 120, 60, 90);
			BYTE bits = m_ovAtkLay[z][x];
			if (!bits) {
				float y = (m_cLayer > 0 && Map::In(x, z) && LayerOn(m.At(x, z), m_cLayer)) ? m.WalkY(x, z, m_cLayer) : -999.f;
				n = AppendOverlayQuad(dst, n, cap, x, z, col, .033f, y, y > -900.f ? m_cLayer : -1, 4);
			} else for (int L = 0; L < LAY_N; L++) {
				if (!(bits & (BYTE)(1 << L)) || !LayerOn(m.At(x, z), L)) continue;
				n = AppendOverlayQuad(dst, n, cap, x, z, col, .033f, m.WalkY(x, z, L), L, 4);
			}
		}
	}
	return n;
}

struct RangeArg { Game* g; int z0, z1; const BYTE* block; VtxS* dst; int cap; int n; };
static void RangeGo(void* p)
{
	RangeArg* a = (RangeArg*)p;
	a->n = a->g->BuildRangeBand(a->z0, a->z1, a->block, a->dst, a->cap);
}

void Game::BuildOverlayQuads()
{
	const Map& m = m_b.map;
	auto quad = [&](int x, int z, DWORD c, float lift, float yAbs = -999.f, int layer = -1, int segs = 1) {
		int n0 = m_ovQuads.size();
		int room = 65536 - n0;
		if (room <= 0) return;
		int n = AppendOverlayQuad(m_ovQuads.data() + n0, 0, room, x, z, c, lift, yAbs, layer, segs);
		m_ovQuads.resize(n0 + n);
	};
	static const DWORD kWarp[8] = {
		RGBA8(255, 80, 80, 170), RGBA8(80, 160, 255, 170), RGBA8(255, 210, 60, 170), RGBA8(80, 230, 120, 170),
		RGBA8(200, 80, 255, 170), RGBA8(80, 230, 230, 170), RGBA8(255, 140, 40, 170), RGBA8(255, 120, 180, 170)
	};
	int hotWarp = -1;
	if (m_cursorValid && Map::In(m_cx, m_cz) && m.At(m_cx, m_cz).gimm == GM_WARP) hotWarp = m.At(m_cx, m_cz).gparam;
	auto mix = [](unsigned h, unsigned x) -> unsigned {
		h ^= x + 0x9e3779b9u + (h << 6) + (h >> 2);
		return h;
	};
	unsigned keyS = mix(0, (unsigned)m_phase);
	keyS = mix(keyS, (unsigned)m_cur);
	keyS = mix(keyS, (unsigned)m_cLayer);
	keyS = mix(keyS, (unsigned)m_cmdSkill);
	keyS = mix(keyS, (unsigned)TacThreatOn(m_save));
	keyS = mix(keyS, (unsigned)TacWx(m_b.weather));
	{
		int wxc = TacWx(m_b.weather);
		if (wxc != WX_CLEAR && wxc != WX_FOG && wxc != WX_HEAT) {
			keyS = mix(keyS, (unsigned)((int)m_camTarget.x));
			keyS = mix(keyS, (unsigned)((int)m_camTarget.z));
		}
	}
	keyS = mix(keyS, (unsigned)hotWarp);
	keyS = mix(keyS, (unsigned)m_b.nUnits);
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& uu = m_b.u[i];
		keyS = mix(keyS, (unsigned)uu.x | ((unsigned)uu.z << 8) | ((unsigned)uu.deck << 16)
			| ((unsigned)uu.alive << 20) | ((unsigned)uu.alert << 22) | ((unsigned)UnitSpan(uu) << 24));
	}
	unsigned keyD = mix(keyS, (unsigned)m_cx);
	keyD = mix(keyD, (unsigned)m_cz);
	keyD = mix(keyD, (unsigned)m_cursorValid);
	keyD = mix(keyD, (unsigned)m_pathN);
	keyD = mix(keyD, (unsigned)m_skillPreview);
	keyD = mix(keyD, (unsigned)TacGridOn(m_save));
	for (int i = 0; i < m_pathN; i++) keyD = mix(keyD, (unsigned)m_path[i]);
	static unsigned s_keyS = 0, s_keyD = 0;
	static int s_stableN = -1;
	static int s_have = 0, s_pulseN = 0;
	static int s_pa[256], s_pb[256];
	static BYTE s_pk[256];
	static BYTE s_block[LAY_N][MAPN][MAPN];
	auto patchPulse = [&]() {
		int dropA = (int)(110.f + 70.f * sinf(m_time * 5.f));
		if (dropA < 70) dropA = 70;
		if (dropA > 210) dropA = 210;
		int alertA = (int)(90.f + 50.f * sinf(m_time * 4.f));
		if (alertA < 48) alertA = 48;
		for (int i = 0; i < s_pulseN; i++) {
			DWORD c = s_pk[i] ? RGBA8(255, 150, 40, alertA) : RGBA8(255, 140, 40, dropA);
			int a = s_pa[i], b = s_pb[i];
			if (a < 0) a = 0;
			if (b > m_ovQuads.size()) b = m_ovQuads.size();
			for (int v = a; v < b; v++) m_ovQuads[v].c = c;
		}
	};
	BOOL dynOnly = FALSE;
	if (!m_ovDirty && s_have && keyS == s_keyS && keyD == s_keyD) {
		patchPulse();
		return;
	}
	dynOnly = !m_ovDirty && s_have && keyS == s_keyS && s_stableN >= 0;
	if (dynOnly) m_ovQuads.resize(s_stableN);
	else {
		m_ovQuads.clear();
		s_pulseN = 0;
	}
	if (!dynOnly) {
	memset(s_block, 0, sizeof(s_block));
	for (int ui = 0; ui < m_b.nUnits; ui++) {
		if (ui == m_cur) continue;
		const Unit& o = m_b.u[ui];
		if (!o.alive) continue;
		int sp = UnitSpan(o);
		if (sp < 1) sp = 1;
		int od = o.deck;
		if (od < 0 || od >= LAY_N) od = 0;
		float yb = m.WalkY(o.x, o.z, od);
		for (int dz = 0; dz < sp; dz++) for (int dx = 0; dx < sp; dx++) {
			int x = o.x + dx, z = o.z + dz;
			if (!Map::In(x, z)) continue;
			s_block[od][z][x] = 1;
			for (int L = 0; L < LAY_N; L++) {
				if (L == od) continue;
				if (fabsf(m.WalkY(x, z, L) - yb) <= 0.9f) s_block[L][z][x] = 1;
			}
		}
	}
	auto markPulse = [&](int kind, int begin) {
		if (s_pulseN >= 256) return;
		s_pa[s_pulseN] = begin;
		s_pb[s_pulseN] = m_ovQuads.size();
		s_pk[s_pulseN] = (BYTE)kind;
		s_pulseN++;
	};
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		const Cell& cc = m.At(x, z);
		if (!cc.gimm) continue;
		DWORD c = RGBA8(120, 255, 160, 150);
		if (cc.gimm == GM_MIGHT) c = RGBA8(255, 210, 80, 160);
		if (cc.gimm == GM_WARP) c = kWarp[cc.gparam & 7];
		if (cc.gimm == GM_CHEST) c = RGBA8(220, 170, 60, 180);
		if (cc.gimm == GM_LEVER) c = RGBA8(80, 220, 200, 190);
		if (cc.gimm == GM_FENCE) c = RGBA8(186, 148, 72, 200);
		int drop0 = -1;
		if (cc.gimm == GM_DROP) {
			int a = (int)(110.f + 70.f * sinf(m_time * 5.f));
			if (a < 70) a = 70;
			if (a > 210) a = 210;
			c = RGBA8(255, 140, 40, a);
			drop0 = m_ovQuads.size();
		}
		if (cc.gimm == GM_WARP && hotWarp == (int)cc.gparam) c = (c & 0x00FFFFFF) | 0xFF000000;
		quad(x, z, c, .028f);
		if (drop0 >= 0) markPulse(0, drop0);
	}
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (m.At(x, z).t == T_DOOR && m.At(x, z).gimm != GM_FENCE) quad(x, z, RGBA8(120, 80, 40, 200), .04f);
	}
	if (TacThreatOn(m_save)) {
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++)
			if (m_ovThreat[z][x]) quad(x, z, RGBA8(160, 36, 36, 58), .02f);
	}
	{
		int pulse = (int)(90.f + 50.f * sinf(m_time * 4.f));
		if (pulse < 48) pulse = 48;
		for (int i = 0; i < m_b.nUnits; i++) {
			const Unit& eu = m_b.u[i];
			if (!eu.alive || eu.alert != 2) continue;
			int sp = UnitSpan(eu);
			for (int dz = 0; dz < sp; dz++) for (int dx = 0; dx < sp; dx++)
				if (Map::In(eu.x + dx, eu.z + dz)) {
					float ay = eu.deck > 0 ? m.WalkY(eu.x + dx, eu.z + dz, eu.deck) : -999.f;
					int p0 = m_ovQuads.size();
					quad(eu.x + dx, eu.z + dz, RGBA8(255, 150, 40, pulse), .045f, ay, eu.deck > 0 ? eu.deck : -1);
					markPulse(1, p0);
				}
		}
	}
	}
	if (m_cur < 0) {
		if (!dynOnly) { s_stableN = m_ovQuads.size(); s_keyS = keyS; }
		s_keyD = keyD;
		s_have = 1;
		m_ovDirty = FALSE;
		return;
	}
	const Unit& u = m_b.u[m_cur];
	auto layY = [&](int x, int z) -> float {
		if (m_cLayer <= 0 || !Map::In(x, z) || !LayerOn(m.At(x, z), m_cLayer)) return -999.f;
		return m.WalkY(x, z, m_cLayer);
	};
	auto paintOpen = [&](int x, int z, DWORD c, float lift) {
		if (!Map::In(x, z) || TacOffMap(m.At(x, z))) return;
		int aL = (m_cur >= 0 && LayerOn(m.At(u.x, u.z), u.deck)) ? u.deck : 0;
		int n = 0;
		for (int L = 0; L < LAY_N; L++) {
			if (!LayerOn(m.At(x, z), L) || LayerRangeCost(aL, L) < 0) continue;
			quad(x, z, c, lift, m.WalkY(x, z, L), L, 4);
			n++;
		}
		if (!n) quad(x, z, c, lift, layY(x, z), layY(x, z) > -900.f ? m_cLayer : -1, 4);
	};
	if (!dynOnly) {
		enum { BANDV = 65536 };
		static VtxS bandV[3][BANDV];
		int zA = MAPN / 3, zB = (MAPN * 2) / 3;
		RangeArg a1 = { this, zA, zB, &s_block[0][0][0], bandV[1], BANDV, 0 };
		RangeArg a2 = { this, zB, MAPN, &s_block[0][0][0], bandV[2], BANDV, 0 };
		OvRun2(RangeGo, &a1, RangeGo, &a2);
		int n0 = BuildRangeBand(0, zA, &s_block[0][0][0], bandV[0], BANDV);
		auto take = [&](VtxS* src, int nv) {
			if (nv <= 0) return;
			int room = 65536 - m_ovQuads.size();
			if (nv > room) nv = room;
			if (nv <= 0) return;
			int at = m_ovQuads.size();
			m_ovQuads.resize(at + nv);
			AvxCopy(m_ovQuads.data() + at, src, (size_t)nv * sizeof(VtxS));
		};
		take(bandV[0], n0);
		take(bandV[1], a1.n);
		take(bandV[2], a2.n);
		{
			int wxc = TacWx(m_b.weather);
			if (wxc != WX_CLEAR && wxc != WX_FOG && wxc != WX_HEAT) {
				int cx = (int)m_camTarget.x, cz = (int)m_camTarget.z;
				int drawn = 0;
				for (int z = cz - 12; z <= cz + 12 && drawn < 420; z++) for (int x = cx - 12; x <= cx + 12 && drawn < 420; x++) {
					if (!Map::In(x, z)) continue;
					int d = m_cover[z * MAPN + x];
					if (d < 8) continue;
					const Cell& cc = m.At(x, z);
					if (cc.t == T_VOID || cc.t == T_CHASM || cc.t == T_WATER || cc.t == T_LAVA) continue;
					float y = m.StandY(x, z) + 0.025f + d * 0.00115f;
					DWORD col = RGBA8(230, 238, 246, 50 + d / 2);
					if (wxc == WX_RAIN || wxc == WX_STORM) col = RGBA8(70, 130, 180, 30 + d / 3);
					else if (wxc == WX_ASH) col = RGBA8(70, 52, 46, 40 + d / 3);
					else if (wxc == WX_SAND) col = RGBA8(196, 164, 96, 40 + d / 3);
					else if (wxc == WX_HAIL) col = RGBA8(220, 230, 240, 36 + d / 3);
					else if (wxc == WX_PETAL) col = RGBA8(230, 150, 170, 30 + d / 4);
					if (m_ovQuads.size() > 10000) break;
					quad(x, z, col, 0, y);
					drawn++;
				}
			}
		}
		s_stableN = m_ovQuads.size();
		s_keyS = keyS;
	}
	// 経路
	if (m_phase == PH_P_MOVE && m_pathN > 1) {
		for (int i = 1; i < m_pathN; i++) {
			int p = m_path[i];
			int px = PackX(p), pz = PackZ(p);
			quad(px, pz, RGBA8(255, 240, 140, 200), .04f, m.WalkY(px, pz, PackDeck(p)), PackDeck(p), 4);
		}
	}
	if (m_phase == PH_P_SKILLS && m_skillPreview >= 0) {
		const SkillDef& sd = SkillInfo(m_skillPreview);
		int rmin = SkillCastRangeMin(u, m_skillPreview), rmax = SkillCastRangeMax(u, m_skillPreview);
		BOOL friendly = sd.kind == SK_HEAL || sd.kind == SK_BUFF;
		DWORD aim = friendly ? RGBA8(90, 255, 150, 110) : RGBA8(255, 80, 60, 100);
		if (sd.rMax == 0) {
			int ar = sd.area > 0 ? sd.area : 0;
			for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++)
				if (Dist(x, z, u.x, u.z) <= ar) paintOpen(x, z, RGBA8(255, 210, 70, 160), .04f);
		} else {
			for (int dz = -rmax; dz <= rmax; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
				int d = abs(dx) + abs(dz);
				if (d < rmin || d > rmax) continue;
				int tx = u.x + dx, tz = u.z + dz;
				if (!Map::In(tx, tz)) continue;
				if (!friendly) {
					int aL = LayerOn(m.At(u.x, u.z), u.deck) ? u.deck : 0;
					BOOL ok = FALSE;
					for (int L = 0; L < LAY_N; L++) if (AttackReachLayer(m_b, u.x, u.z, aL, rmin, rmax, tx, tz, L, true)) ok = TRUE;
					if (!ok) continue;
				}
				paintOpen(tx, tz, aim, .033f);
			}
			int ax = u.x, az = u.z;
			if (m_cursorValid) {
				int d = abs(m_cx - u.x) + abs(m_cz - u.z);
				if (d >= rmin && d <= rmax && Map::In(m_cx, m_cz)) { ax = m_cx; az = m_cz; }
			}
			int len = sd.area > 0 ? sd.area : (sd.shape != SH_RADIUS ? 1 : 0);
			if (len > 0 || sd.shape != SH_RADIUS) {
				for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++)
					if (ShapeCell(sd.shape, len, u.x, u.z, ax, az, x, z)) paintOpen(x, z, RGBA8(255, 210, 70, 170), .045f);
			}
		}
	}
	// スキル効果範囲
	if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0 && m_cursorValid) {
		const SkillDef& sd = SkillInfo(m_cmdSkill);
		if ((sd.area > 0 || sd.shape != SH_RADIUS) && sd.rMax > 0 && m_ovAtk[m_cz][m_cx]) {
			int len = sd.area > 0 ? sd.area : 1;
			for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
				if (!ShapeCell(sd.shape, len, u.x, u.z, m_cx, m_cz, x, z)) continue;
				paintOpen(x, z, RGBA8(255, 210, 70, 170), .045f);
			}
		}
	}
	if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0 && IsFormSkill(m_cmdSkill)) {
		int tx = u.x, tz = u.z;
		if (m_cursorValid) { tx = m_cx; tz = m_cz; }
		FormMark mk[12];
		int nm = FormMarks(FormOf(m_cmdSkill), mk, 12);
		for (int i = 0; i < nm; i++) {
			if (mk[i].foe) continue;
			if (mk[i].onFoe && tx == u.x && tz == u.z) continue;
			int ox, oz;
			FormMarkPos(u, tx, tz, mk[i], ox, oz);
			if (!Map::In(ox, oz) || (ox == u.x && oz == u.z)) continue;
			int who = m_b.UnitAt(ox, oz);
			BOOL filled = who >= 0 && who != m_cur && m_b.u[who].alive && m_b.u[who].team == u.team;
			paintOpen(ox, oz, filled ? RGBA8(70, 210, 110, 200) : RGBA8(240, 190, 50, 170), .055f);
		}
	}
	// グリッド（カーソル周辺）
	if (TacGridOn(m_save) && m_cursorValid && (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET)) {
		for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) {
			int tx = m_cx + dx, tz = m_cz + dz;
			if (!Map::In(tx, tz) || TacOffMap(m.At(tx, tz)) || m_ovMove[tz][tx] || m_ovAtk[tz][tx]) continue;
			int a = 50 - (abs(dx) + abs(dz)) * 6;
			if (a > 0) quad(tx, tz, RGBA8(255, 255, 255, a), .03f, layY(tx, tz), layY(tx, tz) > -900.f ? m_cLayer : -1, 6);
		}
	}
	s_keyD = keyD;
	s_have = 1;
	m_ovDirty = FALSE;
}

// ============================================================ 入力（戦闘）
static bool RaySlab(const V3& org, const V3& dir, float x0, float y0, float z0, float x1, float y1, float z1, float& tHit)
{
	float tmin = 0.02f, tmax = 1e9f;
	float b0[3] = { x0, y0, z0 }, b1[3] = { x1, y1, z1 };
	float o[3] = { org.x, org.y, org.z }, d[3] = { dir.x, dir.y, dir.z };
	for (int a = 0; a < 3; a++) {
		if (fabsf(d[a]) < 1e-8f) {
			if (o[a] < b0[a] || o[a] > b1[a]) return false;
		} else {
			float inv = 1.f / d[a];
			float ta = (b0[a] - o[a]) * inv, tb = (b1[a] - o[a]) * inv;
			if (ta > tb) { float s = ta; ta = tb; tb = s; }
			if (ta > tmin) tmin = ta;
			if (tb < tmax) tmax = tb;
			if (tmin > tmax) return false;
		}
	}
	tHit = tmin;
	return true;
}

BOOL Game::PickCell(float sx, float sy, int& cx, int& cz, int* outLayer) const
{
	V3 org, dir;
	m_r.ScreenRay(sx, sy, org, dir);
	const Map& m = m_b.map;
	float top = (MAXH + 14) * kHStep + 2.f;
	float t0 = 0;
	if (dir.y < -1e-4f && org.y > top) t0 = (org.y - top) / -dir.y;
	float bestT = 1e9f;
	int bx = -1, bz = -1, bl = 0;
	int prevX = -9999, prevZ = -9999;
	for (float t = t0; t < t0 + 180.f; t += 0.28f) {
		V3 p = org + dir * t;
		if (p.y < -4.f) break;
		int ix = (int)floorf(p.x), iz = (int)floorf(p.z);
		if (!Map::In(ix, iz)) { prevX = prevZ = -9999; continue; }
		if (ix == prevX && iz == prevZ) continue;
		prevX = ix; prevZ = iz;
		const Cell& c = m.At(ix, iz);
		if (c.t == T_VOID) continue;
		int n = CellLayN(c);
		for (int L = 0; L < n; L++) {
			int tr = LayerTr(c, L);
			if (TerrainInfo(tr).costFoot >= 99 && !TerrainInfo(tr).flyOk) continue;
			float y = m.WalkY(ix, iz, L);
			float y0 = (L == 0) ? y - 0.16f : y - 0.72f;
			float th = 0;
			if (!RaySlab(org, dir, (float)ix, y0, (float)iz, (float)ix + 1.f, y + 0.04f, (float)iz + 1.f, th)) continue;
			if (th >= bestT) continue;
			bestT = th; bx = ix; bz = iz; bl = L;
		}
	}
	if (bx < 0) return FALSE;
	if (outLayer && m_phase == PH_P_MOVE && m_cur >= 0 && !ReachCanStopLayer(m_reach, bx, bz, bl)) {
		const Unit& who = m_b.u[m_cur];
		int home = who.deck;
		if (home < 0 || home >= LAY_N) home = 0;
		int n = CellLayN(m.At(bx, bz));
		int pick = -1, pickScore = 999;
		for (int L = 0; L < n; L++) {
			if (!ReachCanStopLayer(m_reach, bx, bz, L)) continue;
			int score = abs(L - home);
			if (L == home) score = -1;
			if (score < pickScore) { pickScore = score; pick = L; }
		}
		if (pick >= 0) bl = pick;
	}
	cx = bx; cz = bz;
	if (outLayer) *outLayer = bl;
	return TRUE;
}

// 死者は占有マスに残さない。蘇生対象は味方だけ。敵の遺体は拾わない。
static int CorpseAt(const Battle& b, int x, int z)
{
	if (!Map::In(x, z)) return -1;
	for (int i = 0; i < b.nUnits; i++) {
		const Unit& u = b.u[i];
		if (u.alive || !u.killed || u.team != TEAM_PLAYER) continue;
		int s = UnitSpan(u);
		if (x < u.x || z < u.z || x >= u.x + s || z >= u.z + s) continue;
		return i;
	}
	return -1;
}

void Game::SyncCursorLayer()
{
	if (!Map::In(m_cx, m_cz)) { m_cLayer = 0; return; }
	const Cell& c = m_b.map.At(m_cx, m_cz);
	if (m_cLayer < 0 || m_cLayer >= LAY_N) m_cLayer = 0;
	if (LayerOn(c, m_cLayer)) {
		int t = LayerTr(c, m_cLayer);
		if (TerrainInfo(t).costFoot < 99 || TerrainInfo(t).flyOk) return;
	}
	m_cLayer = 0;
	int n = CellLayN(c);
	for (int L = 0; L < n; L++) {
		int t = LayerTr(c, L);
		if (TerrainInfo(t).costFoot < 99 || TerrainInfo(t).flyOk) { m_cLayer = L; return; }
	}
}

void Game::CycleLayer(int dir)
{
	if (!Map::In(m_cx, m_cz)) return;
	const Cell& c = m_b.map.At(m_cx, m_cz);
	int n = CellLayN(c);
	if (n < 2) return;
	for (int k = 0; k < n; k++) {
		m_cLayer = (m_cLayer + (dir < 0 ? n - 1 : 1)) % n;
		int t = LayerTr(c, m_cLayer);
		if (TerrainInfo(t).costFoot < 99 || TerrainInfo(t).flyOk) break;
	}
	m_layerLock = 1;
	UpdateHover();
}

void Game::UpdateHover()
{
	if (m_screen != SC_BATTLE) return;
	if (m_mouseSeen && !m_navMode && !m_seqCut && !m_demo) {
		BOOL overUi = FALSE;
		for (auto& b : m_btnsPrev) if (m_mx >= b.x && m_my >= b.y && m_mx < b.x + b.w && m_my < b.y + b.h) overUi = TRUE;
		int cx, cz, lay = m_cLayer;
		if (!overUi && PickCell((float)m_mx, (float)m_my, cx, cz, &lay)) {
			BOOL moved = !m_cursorValid || cx != m_cx || cz != m_cz;
			if (m_cursorValid && moved
				&& (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET))
				TacSfx(TS_CURSOR);
			m_cx = cx; m_cz = cz; m_cursorValid = TRUE;
			if (moved) m_layerLock = 0;
			if (!m_layerLock) m_cLayer = lay;
		}
	}
	SyncCursorLayer();
	m_hoverUnit = m_cursorValid ? m_b.UnitAt(m_cx, m_cz, m_cLayer) : -1;
	// 飛行の味方は見た目が上の床にいるが、占有は deck のまま。カーソルの層が空なら同じマスのユニットを拾う。
	if (m_hoverUnit < 0 && m_cursorValid) m_hoverUnit = m_b.UnitAt(m_cx, m_cz);
	if (m_hoverUnit >= 0 && !m_b.u[m_hoverUnit].alive && m_b.u[m_hoverUnit].team != TEAM_PLAYER) m_hoverUnit = -1;
	if (m_hoverUnit < 0 && m_cursorValid)
		m_hoverUnit = CorpseAt(m_b, m_cx, m_cz);
	if (m_phase == PH_P_SKILLTARGET) AimFormFace();
	{
		static int s_px = -999, s_pz = -999, s_pl = -1, s_pu = -1, s_ph = -1, s_pn = 0;
		int same = (m_phase == s_ph && m_cur == s_pu && m_cx == s_px && m_cz == s_pz && m_cLayer == s_pl);
		if (!same) m_pathN = 0;
		if (m_phase == PH_P_MOVE && m_cur >= 0 && m_cursorValid) {
			if (same) m_pathN = s_pn;
			else {
				const Unit& u = m_b.u[m_cur];
				int tx = m_cx, tz = m_cz;
				if (!ReachCanStopLayer(m_reach, tx, tz, m_cLayer)) {
					int t = m_b.UnitAt(tx, tz);
					if (t >= 0 && m_b.u[t].team != u.team && m_ovAtk[tz][tx]) {
						int best = -1, bc = 9999;
						for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
							if (!m_ovMove[z][x]) continue;
							if (!InAttackRange(m_b, x, z, UnitRangeMin(u), UnitRangeMax(u), tx, tz, true)) continue;
							int c = ReachBestCost(m_reach, x, z) * 4 + Dist(x, z, tx, tz);
							if (c < bc) { bc = c; best = z * MAPN + x; }
						}
						if (best >= 0) { tx = best % MAPN; tz = best / MAPN; }
						else { s_ph = m_phase; s_pu = m_cur; s_px = m_cx; s_pz = m_cz; s_pl = m_cLayer; s_pn = 0; return; }
					} else { s_ph = m_phase; s_pu = m_cur; s_px = m_cx; s_pz = m_cz; s_pl = m_cLayer; s_pn = 0; return; }
				}
				m_pathN = ReachPath(m_reach, u.x, u.z, tx, tz, m_path, MAPN * 4, m_cLayer);
				s_ph = m_phase; s_pu = m_cur; s_px = m_cx; s_pz = m_cz; s_pl = m_cLayer; s_pn = m_pathN;
			}
		} else {
			s_ph = m_phase; s_pu = m_cur; s_px = m_cx; s_pz = m_cz; s_pl = m_cLayer; s_pn = m_pathN;
		}
	}
}

void Game::MoveCursor(int dx, int dz)
{
	V3 right = v3(-cosf(m_camYaw), 0, sinf(m_camYaw));
	V3 fwd = v3(-sinf(m_camYaw), 0, -cosf(m_camYaw));
	V3 w = right * (float)dx + fwd * (float)dz;
	int sx = 0, sz = 0;
	if (fabsf(w.x) >= fabsf(w.z) - .01f) sx = w.x > 0 ? 1 : -1;
	else sz = w.z > 0 ? 1 : -1;
	if (!m_cursorValid && m_cur >= 0) { m_cx = m_b.u[m_cur].x; m_cz = m_b.u[m_cur].z; m_cLayer = m_b.u[m_cur].deck; }
	int refH = Map::In(m_cx, m_cz) ? LayerHt(m_b.map.At(m_cx, m_cz), m_cLayer) : 0;
	m_cx = Clampi(m_cx + sx, 0, MAPN - 1);
	m_cz = Clampi(m_cz + sz, 0, MAPN - 1);
	m_cursorValid = TRUE;
	m_layerLock = 0;
	if (Map::In(m_cx, m_cz)) {
		const Cell& c = m_b.map.At(m_cx, m_cz);
		int n = CellLayN(c), best = 0, bestDh = 9999;
		for (int L = 0; L < n; L++) {
			int tr = LayerTr(c, L);
			if (TerrainInfo(tr).costFoot >= 99 && !TerrainInfo(tr).flyOk) continue;
			int dh = abs(LayerHt(c, L) - refH);
			if (dh < bestDh) { bestDh = dh; best = L; }
		}
		m_cLayer = best;
	}
	m_mouseSeen = FALSE;
	m_navMode = FALSE;
	if (m_cur >= 0) {
		V3 up = v3(m_b.u[m_cur].px, m_b.u[m_cur].py, m_b.u[m_cur].pz);
		V3 cp = CellCenter(m_cx, m_cz);
		V3 d = cp - up;
		float L = Len(v3(d.x, 0, d.z));
		if (L > 3.f) m_camPan = v3(d.x, 0, d.z) * ((L - 3.f) / L);
		else m_camPan = v3(0, 0, 0);
	}
	UpdateHover();
}

BOOL Game::ChestInRange(int ui, int x, int z) const
{
	if (ui < 0 || ui >= m_b.nUnits || !Map::In(x, z)) return FALSE;
	if (m_b.map.At(x, z).gimm != GM_CHEST) return FALSE;
	const Unit& u = m_b.u[ui];
	if (!u.alive || u.team != TEAM_PLAYER) return FALSE;
	return InAttackRange(m_b, u.x, u.z, UnitRangeMin(u), UnitRangeMax(u), x, z, true) ? TRUE : FALSE;
}

BOOL Game::OpenChestAt(int ui, int x, int z)
{
	if (!ChestInRange(ui, x, z)) return FALSE;
	Unit& u = m_b.u[ui];
	Cell& c = m_b.map.At(x, z);
	int gp = c.gparam;
	int it = -1;
	if (gp == 0) it = IT_HERB;
	else if (gp == 1) it = IT_ORE;
	else if (gp == 2) it = IT_CRYSTAL;
	else if (gp >= 3) it = IT_SHARD;
	int cashed = (it >= 0 && it < IT_COUNT) ? GainItemN(m_save, it, 1) : 0;
	BOOL opener = CanOpenDoor(u);
	int add = max(1, u.ch.stat[S_HP] * (opener ? 40 : 25) / 100);
	u.hp = min(u.ch.stat[S_HP], u.hp + add);
	u.hpShow = (float)u.hp;
	if (opener || gp == 2) u.mulTurns = max(u.mulTurns, 3);
	if (opener || gp == 1) u.expGained += 12;
	c.gimm = GM_NONE;
	c.prop = PK_NONE;
	const wchar_t* in = L"";
	if (it == IT_HERB) in = TT(TX_IT_HERB);
	else if (it == IT_ORE) in = TT(TX_IT_ORE);
	else if (it == IT_CRYSTAL) in = TT(TX_IT_CRYSTAL);
	else if (it == IT_SHARD) in = TT(TX_IT_SHARD);
	wchar_t cash[32];
	if (cashed > 0) swprintf_s(cash, L"+%dG", cashed);
	const wchar_t* shown = cashed > 0 ? cash : in;
	wchar_t msg[160];
	if (it >= 0 && (opener || gp == 1))
		swprintf_s(msg, L"%s  %s  %s+%d  %s+12", TT(TX_GM_CHEST), shown, TT(TX_ST_HP), add, TT(TX_MISC_EXP));
	else if (it >= 0)
		swprintf_s(msg, L"%s  %s  %s+%d", TT(TX_GM_CHEST), shown, TT(TX_ST_HP), add);
	else
		swprintf_s(msg, L"%s  %s+%d", TT(TX_GM_CHEST), TT(TX_ST_HP), add);
	V3 p = CellCenter(x, z);
	AddPopup(p + v3(0, 1.1f, 0), msg, RGBA8(255, 210, 120), 1, MSG_RISE, 0, 3.6f);
	AddLog(msg, RGBA8(255, 210, 120));
	FaceTowards(ui, x, z);
	TacSfx(TS_CHEST);
	m_r.BuildTerrain(m_b.map);
	return TRUE;
}

BOOL Game::HasAttackTarget(int ui) const
{
	for (int i = 0; i < m_b.nUnits; i++) if (CanTarget(ui, i, -1)) return TRUE;
	if (ui < 0 || ui >= m_b.nUnits) return FALSE;
	const Unit& u = m_b.u[ui];
	int rmin = UnitRangeMin(u), rmax = UnitRangeMax(u);
	for (int dz = -rmax; dz <= rmax; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
		int x = u.x + dx, z = u.z + dz;
		if (ChestInRange(ui, x, z)) return TRUE;
	}
	return FALSE;
}

BOOL Game::CursorInAttackRange() const
{
	if (!m_cursorValid || m_cur < 0 || !Map::In(m_cx, m_cz)) return FALSE;
	if (m_phase != PH_P_TARGET && m_phase != PH_P_SKILLTARGET) return FALSE;
	int v = m_ovAtk[m_cz][m_cx];
	if (v != 1 && v != 3 && v != 4 && v != 7) return FALSE;
	int L = m_cLayer;
	if (L < 0 || L >= LAY_N) L = 0;
	BYTE lay = m_ovAtkLay[m_cz][m_cx];
	if (!lay) return TRUE;
	return (lay & (BYTE)(1 << L)) != 0;
}

BOOL Game::CanTarget(int ui, int t, int skill) const
{
	if (t < 0 || t >= m_b.nUnits) return FALSE;
	const Unit& u = m_b.u[ui];
	const Unit& d = m_b.u[t];
	if (skill >= 0 && SkillInfo(skill).effect == EF_RAISE) {
		if (d.alive || !d.killed || d.team != u.team) return FALSE;
		if (!RaisePlaceable(m_b, d)) return FALSE;
		int dist = 9999;
		int s = UnitSpan(d);
		int sa = UnitSpan(u);
		for (int az = 0; az < sa; az++) for (int ax = 0; ax < sa; ax++)
			for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++)
				dist = min(dist, abs((u.x + ax) - (d.x + dx)) + abs((u.z + az) - (d.z + dz)));
		if (dist <= 0) return TRUE;
		int add = LayerRangeCost(u.deck, d.deck);
		if (add < 0) return FALSE;
		int rmin = SkillCastRangeMin(u, skill), rmax = SkillCastRangeMax(u, skill);
		return dist + add >= rmin && dist + add <= rmax;
	}
	if (!d.alive) return FALSE;
	if (skill < 0) {
		if (d.team == u.team) return FALSE;
		return InRangeOfUnit(m_b, u, u.x, u.z, UnitRangeMin(u), UnitRangeMax(u), d, true);
	}
	const SkillDef& sd = SkillInfo(skill);
	BOOL friendly = sd.kind == SK_HEAL || sd.kind == SK_BUFF;
	if (friendly != (d.team == u.team)) return FALSE;
	if (sd.effect == EF_REACT && (t == ui || d.ct >= 1000)) return FALSE;
	int rmin = SkillCastRangeMin(u, skill), rmax = SkillCastRangeMax(u, skill);
	if (friendly) {
		int add = LayerRangeCost(u.deck, d.deck);
		if (add < 0) return FALSE;
		int dist = 9999, sa = UnitSpan(u), sdn = UnitSpan(d);
		for (int az = 0; az < sa; az++) for (int ax = 0; ax < sa; ax++)
			for (int dz = 0; dz < sdn; dz++) for (int dx = 0; dx < sdn; dx++)
				dist = min(dist, abs((u.x + ax) - (d.x + dx)) + abs((u.z + az) - (d.z + dz)));
		if (dist + add < rmin || dist + add > rmax) return FALSE;
	} else if (!InRangeOfUnit(m_b, u, u.x, u.z, rmin, rmax, d, true)) return FALSE;
	if (IsFormSkill(skill) && !FormFits(m_b, ui, skill, d.x, d.z)) return FALSE;
	return SkillTargetHelps(m_b, ui, skill, t) ? TRUE : FALSE;
}

void Game::OpenCommand()
{
	m_phase = PH_P_CMD;
	m_phaseT = 0;
	m_statOpen = 0;
	m_statEq = 0;
	TacSfx(TS_OPEN);
	m_navMode = TRUE;
	m_focusId = HasAttackTarget(m_cur) ? ID_BT_ATTACK : ID_BT_WAIT;
	ComputeOverlays();
}

static const wchar_t* RaiseNoRoomWord();

void Game::BattleConfirmCursor()
{
	if (m_cur < 0 || !m_cursorValid) return;
	Unit& u = m_b.u[m_cur];
	if (m_phase == PH_P_MOVE) {
		if (m_b.map.At(m_cx, m_cz).t == T_DOOR && Dist(u.x, u.z, m_cx, m_cz) == 1) {
			BOOL fence = m_b.map.At(m_cx, m_cz).gimm == GM_FENCE;
			if (fence || CanOpenDoor(u)) {
				OpenDoor(m_b, m_cx, m_cz);
				AddPopup(CellCenter(m_cx, m_cz) + v3(0, .6f, 0), TT(fence ? TX_BT_FENCE : TX_BT_DOOR), RGBA8(220, 200, 140), 1, MSG_SLAB);
				TacSfx(TS_DOOR);
				m_r.BuildTerrain(m_b.map);
				FinishUnitTurn(1);
				return;
			}
		}
		if (CanTerrace(m_b, m_cur, m_cx, m_cz)) { ApplyTerrace(m_cx, m_cz); return; }
		if (m_cx == u.x && m_cz == u.z && m_cLayer == u.deck) { OpenCommand(); return; }
		if (ChestInRange(m_cur, m_cx, m_cz)) {
			if (OpenChestAt(m_cur, m_cx, m_cz)) { FinishUnitTurn(1); return; }
		}
		if (ReachCanStopLayer(m_reach, m_cx, m_cz, m_cLayer)) {
			m_moved = 1;
			StartMove(m_cur, m_cx, m_cz, PH_P_CMD);
			return;
		}
		int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
		if (t >= 0 && m_b.u[t].team != u.team) {
			if (CanTarget(m_cur, t, -1)) {
				m_target = t;
				m_phase = PH_P_TARGET;
				ComputeOverlays();
				return;
			}
			if (m_pathN > 1) {
				int dst = m_path[m_pathN - 1];
				m_target = t;
				m_moved = 1;
				StartMove(m_cur, PackX(dst), PackZ(dst), PH_P_TARGET);
				return;
			}
		}
		AddPopup(CellCenter(m_cx, m_cz) + v3(0, .5f, 0), L"×", RGBA8(255, 120, 110), 1, MSG_DROP);
		TacSfx(TS_ERROR);
		return;
	}
	if (m_phase == PH_P_SHOVE) {
		int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
		if (CanShove(m_b, m_cur, t, NULL, NULL)) {
			int nx, nz;
			CanShove(m_b, m_cur, t, &nx, &nz);
			Unit& d = m_b.u[t];
			int hp0 = d.hp;
			DoShove(m_b, m_cur, t);
			V3 p = UnitGroundPos(d);
			d.px = p.x; d.py = p.y; d.pz = p.z;
			AddPopup(p + v3(0, m_act[t].height, 0), TT(TX_BT_SHOVE), RGBA8(255, 220, 140), 1, MSG_SLAB);
			if (WakeNearPlayers(m_b, 16)) AddBanner(TT(TX_BT_REIN), NULL, RGBA8(255, 210, 120), 1, 1.4f, MSG_DEPTH);
			if (d.hp >= hp0) {
				TacSfx(d.team == TEAM_PLAYER ? TS_HURT : TS_HIT);
				FinishUnitTurn(1);
				return;
			}
			// 奈落・溶岩へ落とした分は通常の命中と同じく撃破処理・報酬・経験値を通す。
			wchar_t s[24];
			swprintf_s(s, L"%d", hp0 - d.hp);
			AddPopup(p + v3(0, m_act[t].height + .3f, 0), s, RGBA8(255, 255, 255), 0, MSG_BURST, .15f);
			Strike& sk = m_strk[0];
			memset(&sk, 0, sizeof(sk));
			sk.attacker = m_cur; sk.defender = t; sk.skill = -1;
			sk.dmg = hp0 - d.hp; sk.hit = 1; sk.kill = d.hp <= 0;
			m_strkN = 1;
			d.flash = 1.f;
			if (d.hp <= 0) {
				d.alive = 1;
				d.killed = 0;
				TacSfx(d.team == TEAM_PLAYER ? TS_FALL : TS_DEATH);
			} else {
				TacSfx(d.team == TEAM_PLAYER ? TS_HURT : TS_HIT);
				ActorPlay(m_act[t], CL_HIT_A, 0, .06f);
			}
			FinishSeq();
		} else TacSfx(TS_ERROR);
		return;
	}
	if (m_phase == PH_P_TARGET) {
		int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
		if (CanTarget(m_cur, t, -1)) BeginAttack(m_cur, t, -1, m_cx, m_cz);
		else if (ChestInRange(m_cur, m_cx, m_cz) && OpenChestAt(m_cur, m_cx, m_cz)) FinishUnitTurn(1);
		return;
	}
	if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0) {
		const SkillDef& sd = SkillInfo(m_cmdSkill);
		if (sd.rMax == 0) {
			if (!SkillAimHelps(m_b, m_cur, m_cmdSkill, u.x, u.z)) {
				int c = sd.effect == EF_RAISE ? CorpseAt(m_b, u.x, u.z) : -1;
				if (c >= 0 && !RaisePlaceable(m_b, m_b.u[c])) AddBanner(RaiseNoRoomWord(), NULL, RGBA8(255, 180, 160), 2, 1.8f, MSG_DROP);
				else AddBanner(TT(TX_BT_NOTARGET), NULL, RGBA8(255, 180, 160), 2, 1.4f, MSG_DROP);
				TacSfx(TS_ERROR);
				return;
			}
			BeginAttack(m_cur, m_cur, m_cmdSkill, u.x, u.z);
			return;
		}
		if (sd.area > 0 && m_cursorValid && m_ovAtk[m_cz][m_cx]) {
			if (!SkillAimHelps(m_b, m_cur, m_cmdSkill, m_cx, m_cz)) {
				BOOL blocked = FALSE;
				if (sd.effect == EF_RAISE) {
					for (int i = 0; i < m_b.nUnits && !blocked; i++) {
						const Unit& c = m_b.u[i];
						if (c.alive || !c.killed || c.team != u.team) continue;
						int dist = 9999, sp = UnitSpan(c);
						for (int dz = 0; dz < sp; dz++) for (int dx = 0; dx < sp; dx++)
							dist = min(dist, abs(m_cx - (c.x + dx)) + abs(m_cz - (c.z + dz)));
						if (dist <= max(0, sd.area)) blocked = !RaisePlaceable(m_b, c);
					}
				}
				if (blocked) AddBanner(RaiseNoRoomWord(), NULL, RGBA8(255, 180, 160), 2, 1.8f, MSG_DROP);
				else AddPopup(CellCenter(m_cx, m_cz) + v3(0, .5f, 0), L"×", RGBA8(255, 120, 110), 1, MSG_DROP);
				TacSfx(TS_ERROR);
				return;
			}
			int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
			if (t < 0) t = m_cur;
			BeginAttack(m_cur, t, m_cmdSkill, m_cx, m_cz);
			return;
		}
		int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
		if (sd.effect == EF_RAISE && (t < 0 || m_b.u[t].alive)) {
			int c = CorpseAt(m_b, m_cx, m_cz);
			if (c >= 0) t = c;
		}
		if (CanTarget(m_cur, t, m_cmdSkill)) BeginAttack(m_cur, t, m_cmdSkill, m_cx, m_cz);
		else if (sd.effect == EF_RAISE && t >= 0 && !m_b.u[t].alive && m_b.u[t].team == u.team && !RaisePlaceable(m_b, m_b.u[t])) {
			AddBanner(RaiseNoRoomWord(), NULL, RGBA8(255, 180, 160), 2, 1.8f, MSG_DROP);
			TacSfx(TS_ERROR);
		} else { AddPopup(CellCenter(m_cx, m_cz) + v3(0, .5f, 0), L"×", RGBA8(255, 120, 110), 1, MSG_DROP); TacSfx(TS_ERROR); }
	}
}

void Game::ApplyTerrace(int tx, int tz)
{
	if (m_cur < 0 || !CanTerrace(m_b, m_cur, tx, tz)) { TacSfx(TS_ERROR); return; }
	DoTerrace(m_b, m_cur, tx, tz);
	m_r.BuildTerrain(m_b.map);
	const Cell& c = m_b.map.At(tx, tz);
	wchar_t s[48];
	swprintf_s(s, L"%s  H%d", TT(TX_BT_CLIMB), (int)c.h);
	AddPopup(CellCenter(tx, tz) + v3(0, .8f, 0), s, RGBA8(230, 210, 160), 1, MSG_SLAB);
	TacSfx(TS_OK_BATTLE);
	ComputeOverlays();
	FinishUnitTurn(1);
}

void Game::BattleClick(int x, int y)
{
	for (int i = 0; i < m_miniHitN && i < 3; i++) {
		if (x < m_miniX[i] || y < m_miniY[i] || x >= m_miniX[i] + m_miniS[i] || y >= m_miniY[i] + m_miniS[i]) continue;
		if (m_cursorValid && Map::In(m_cx, m_cz) && LayerOn(m_b.map.At(m_cx, m_cz), i)) {
			m_cLayer = i;
			UpdateHover();
			TacSfx(TS_CURSOR);
		}
		return;
	}
	if (m_statOpen) return;
	if (m_phase == PH_INTRO) { m_phaseT = max(m_phaseT, (m_surveyN >= 2 ? kIntroSweep : 0.35f) * TacPaceTime(m_save)); return; }
	if (m_phase != PH_P_MOVE && m_phase != PH_P_TARGET && m_phase != PH_P_SKILLTARGET && m_phase != PH_P_SHOVE) return;
	// ログも手番欄も、その下の移動先をクリックできるようにする。ボタンは先にクリックを消費する。
	int cx, cz;
	int lay = m_cLayer;
	if (!PickCell((float)x, (float)y, cx, cz, &lay)) return;
	m_layerLock = 0;
	m_cx = cx; m_cz = cz; m_cLayer = lay; m_cursorValid = TRUE;
	UpdateHover();
	BattleConfirmCursor();
}

void Game::BattleCancel()
{
	if (m_statOpen) {
		if (m_statEq) m_statEq = 0;
		else { m_statOpen = 0; m_statEq = 0; }
		TacSfx(TS_CANCEL);
		return;
	}
	int sounded = 0;
	switch (m_phase) {
	case PH_P_MOVE:
		if (m_cur >= 0 && (m_cx != m_b.u[m_cur].x || m_cz != m_b.u[m_cur].z)) {
			m_cx = m_b.u[m_cur].x; m_cz = m_b.u[m_cur].z; m_cLayer = m_b.u[m_cur].deck; m_camPan = v3(0, 0, 0); UpdateHover();
		} else { m_phaseBeforeMenu = m_phase; m_phase = PH_MENU; m_menuConfirm = 0; m_navMode = TRUE; m_focusId = ID_BT_M_RESUME; }
		sounded = 1;
		break;
	case PH_P_CMD:
		if (m_moved && m_cur >= 0) {
			Unit& u = m_b.u[m_cur];
			u.x = m_curX0; u.z = m_curZ0; u.deck = m_curDeck0; u.face = m_curFace0;
			V3 p = UnitGroundPos(u);
			u.px = p.x; u.py = p.y; u.pz = p.z;
			m_b.RebuildOcc();
			ComputeReach(m_b, m_cur, m_reach);
			m_moved = 0;
		}
		m_phase = PH_P_MOVE;
		if (m_cur >= 0) { m_cx = m_b.u[m_cur].x; m_cz = m_b.u[m_cur].z; m_cLayer = m_b.u[m_cur].deck; }
		ComputeOverlays();
		m_navMode = FALSE;
		sounded = 1;
		break;
	case PH_P_SKILLS:
		m_phase = PH_P_CMD; m_focusId = ID_BT_SKILL; m_navMode = TRUE; m_skillPreview = -1;
		sounded = 1;
		break;
	case PH_P_ITEM:
		if (m_itemAim >= 0) m_itemAim = -1;
		else { m_phase = PH_P_CMD; m_focusId = ID_BT_ITEM; m_navMode = TRUE; }
		sounded = 1;
		break;
	case PH_P_SHOVE:
		m_phase = PH_P_CMD; m_focusId = ID_BT_SHOVE; m_navMode = TRUE;
		sounded = 1;
		break;
	case PH_P_SUMMON:
		m_phase = PH_P_CMD; m_focusId = ID_BT_SUMMON; m_navMode = TRUE;
		sounded = 1;
		break;
	case PH_P_TARGET:
		if (m_target >= 0 && !m_moved) { m_target = -1; m_phase = PH_P_MOVE; ComputeOverlays(); sounded = 1; break; }
		OpenCommand();
		sounded = 1;
		break;
	case PH_P_SKILLTARGET:
		if (m_cmdFaced && m_cur >= 0) m_b.u[m_cur].face = m_cmdFace0;
		m_cmdFaced = 0;
		if (IsSummonId(m_cmdSkill)) { m_phase = PH_P_SUMMON; m_navMode = TRUE; m_focusId = ID_BT_SUM0; ComputeOverlays(); sounded = 1; break; }
		m_phase = PH_P_SKILLS; m_navMode = TRUE; m_focusId = ID_BT_SK0 + max(0, m_cmdSkill);
		if (IsCraftId(m_cmdSkill)) {
			int slot = 0;
			for (int i = 0; i < CRAFT_SLOT_N; i++) if (m_b.u[m_cur].craftEq[i] == m_cmdSkill) slot = i;
			m_focusId = ID_BT_CRAFT0 + slot;
		} else if (IsFormSkill(m_cmdSkill)) m_focusId = ID_BT_FORM0 + (m_cmdSkill - SKL_FORM0);
		ComputeOverlays();
		sounded = 1;
		break;
	case PH_MENU:
		if (m_menuConfirm) m_menuConfirm = 0;
		else m_phase = m_phaseBeforeMenu;
		sounded = 1;
		break;
	case PH_LEVELUP:
		if (!m_promoAsk && m_lvqCur == 0 && m_lvT < 2.6f * TacPaceTime(m_save)) break;
		AdvanceLevelUp();
		sounded = 1;
		break;
	default: break;
	}
	if (sounded) TacSfx(TS_CANCEL);
}

// ============================================================ 移動
void Game::StartMove(int ui, int tx, int tz, int next)
{
	Unit& u = m_b.u[ui];
	m_moveN = ReachPath(m_reach, u.x, u.z, tx, tz, m_movePts, MAPN * 4, m_cLayer);
	if (m_moveN < 1) { m_movePts[0] = PackCell(u.x, u.z, u.deck); m_moveN = 1; }
	m_moveUnit = ui;
	m_moveT = 0;
	m_moveNext = next;
	m_phase = u.team == TEAM_PLAYER ? PH_MOVING : PH_E_MOVE;
	m_phaseT = 0;
	m_pathN = 0;
	m_camPan = v3(0, 0, 0);
	m_act[ui].flap = 0.f;
	if (m_moveN > 1 && !PoseGround(m_act[ui], u, m_dieT[ui])) {
		BOOL fly = UnitMoveType(u) == MV_FLY && (m_act[ui].wings > 0 || u.team == TEAM_PLAYER);
		if (fly) {
			if (m_act[ui].wings == 0) m_act[ui].wings = 1;
			if (m_act[ui].hover < .2f) m_act[ui].hover = .35f;
			m_act[ui].flap = 1.f;
			ActorPlay(m_act[ui], CL_IDLE, 1, .12f);
			m_act[ui].animSpeed = 1.2f;
		} else {
			int clip = m_moveN > 3 ? CL_RUN : CL_WALK;
			ActorPlay(m_act[ui], clip, 1, .1f);
			m_act[ui].animSpeed = clip == CL_RUN ? 1.15f : 1.f;
		}
	}
}

void Game::UpdateMove(float dt)
{
	int ui = m_moveUnit;
	Unit& u = m_b.u[ui];
	BOOL fly = UnitMoveType(u) == MV_FLY;
	float pace = TacPaceTime(m_save);
	if (pace < 0.05f) pace = 0.05f;
	float speed = 2.6f * 1.5f / pace;
	// 徒歩はそのまま。翼の飛行は一マスを速くするが、時間は半分まで縮めない。
	if (fly) speed *= 1.35f;
	m_moveT += dt * speed;
	int seg = (int)m_moveT;
	{
		static int stepSeg = -999;
		BOOL wing = fly && u.team == TEAM_PLAYER && m_act[ui].wings > 0;
		if (seg != stepSeg) {
			if (stepSeg >= 0 && seg > stepSeg && !fly && seg >= 0 && seg < m_moveN)
				WeatherTread(u, PackX(m_movePts[seg]), PackZ(m_movePts[seg]));
			if (stepSeg < 0 && wing && m_moveN > 1) TacSfx(TS_FLY);
			else if (stepSeg >= 0 && seg > stepSeg && !fly) TacSfx(TS_STEP);
			stepSeg = seg;
		}
		if (seg >= m_moveN - 1) stepSeg = -999;
	}
	if (seg >= m_moveN - 1) {
		int last = m_movePts[m_moveN - 1];
		u.x = PackX(last); u.z = PackZ(last);
		u.deck = PackDeck(last);
		if (!LayerOn(m_b.map.At(u.x, u.z), u.deck)) u.deck = 0;
		m_b.RebuildOcc();
		int gp = m_b.map.At(u.x, u.z).gparam;
		int tile = ApplyTile(m_b, ui);
		int chestItem = -1, chestCash = 0;
		if (tile == GM_CHEST && u.team == TEAM_PLAYER) {
			int it = -1;
			if (gp == 0) it = IT_HERB;
			else if (gp == 1) it = IT_ORE;
			else if (gp == 2) it = IT_CRYSTAL;
			else if (gp >= 3) it = IT_SHARD;
			chestItem = it;
			if (it >= 0 && it < IT_COUNT) chestCash = GainItemN(m_save, it, 1);
		}
		V3 p = UnitGroundPos(u);
		u.px = p.x; u.py = p.y; u.pz = p.z;
		u.hpShow = (float)u.hp;
		if (tile == GM_HEAL) {
			AddPopup(p + v3(0, m_act[ui].height, 0), TT(TX_GM_HEAL), RGBA8(120, 255, 160), 0, MSG_RISE);
			TacSfx(TS_HEAL);
		} else if (tile == GM_MIGHT) {
			AddPopup(p + v3(0, m_act[ui].height, 0), TT(TX_GM_MIGHT), RGBA8(255, 220, 120), 1, MSG_FACE);
			TacSfx(TS_BUFF);
		} else if (tile == GM_WARP) {
			AddPopup(p + v3(0, m_act[ui].height, 0), TT(TX_GM_WARP), RGBA8(180, 140, 255), 1, MSG_ORBIT);
			TacSfx(TS_WARP);
		} else if (tile == GM_CHEST) {
			const wchar_t* in = L"";
			if (chestItem == IT_HERB) in = TT(TX_IT_HERB);
			else if (chestItem == IT_ORE) in = TT(TX_IT_ORE);
			else if (chestItem == IT_CRYSTAL) in = TT(TX_IT_CRYSTAL);
			else if (chestItem == IT_SHARD) in = TT(TX_IT_SHARD);
			BOOL opener = CanOpenDoor(u);
			int add = max(1, u.ch.stat[S_HP] * (opener ? 40 : 25) / 100);
			wchar_t cash[32];
			if (chestCash > 0) swprintf_s(cash, L"+%dG", chestCash);
			const wchar_t* shown = chestCash > 0 ? cash : in;
			wchar_t msg[160];
			if (chestItem >= 0 && (opener || gp == 1))
				swprintf_s(msg, L"%s  %s  %s+%d  %s+12", TT(TX_GM_CHEST), shown, TT(TX_ST_HP), add, TT(TX_MISC_EXP));
			else if (chestItem >= 0)
				swprintf_s(msg, L"%s  %s  %s+%d", TT(TX_GM_CHEST), shown, TT(TX_ST_HP), add);
			else
				swprintf_s(msg, L"%s  %s+%d", TT(TX_GM_CHEST), TT(TX_ST_HP), add);
			AddPopup(p + v3(0, m_act[ui].height, 0), msg, RGBA8(255, 210, 120), 1, MSG_RISE, 0, 3.6f);
			AddLog(msg, RGBA8(255, 210, 120));
			TacSfx(TS_CHEST);
		} else if (tile == GM_LEVER) {
			AddPopup(p + v3(0, m_act[ui].height, 0), TT(TX_GM_LEVER), RGBA8(120, 230, 210), 1, MSG_SLAB);
			TacSfx(TS_DOOR);
			m_r.BuildTerrain(m_b.map);
		} else if (tile == GM_DROP) {
			AddPopup(p + v3(0, m_act[ui].height, 0), TT(TX_GM_DROP), RGBA8(255, 150, 60), 1, MSG_DROP);
			TacSfx(u.alive ? TS_HURT : TS_FALL);
			m_r.BuildTerrain(m_b.map);
			if (!u.alive || u.hp <= 0) {
				u.hp = 0; u.alive = 0; u.killed = 1;
				if (m_act[ui].clip != CL_DEATH) ActorPlay(m_act[ui], CL_DEATH, 0, .08f);
				if (m_dieT[ui] < 0) m_dieT[ui] = 0;
				if (CheckBattleOver()) return;
				FinishUnitTurn(1);
				return;
			}
		}
		if (u.team == TEAM_PLAYER && WakeNearPlayers(m_b, 16))
			AddBanner(TT(TX_BT_REIN), NULL, RGBA8(255, 210, 120), 1, 1.4f, MSG_DEPTH);
		m_act[ui].animSpeed = 1.f;
		m_act[ui].flap = 0.f;
		if (!PoseKeep(m_act[ui], u, m_dieT[ui])) ActorPlay(m_act[ui], CL_IDLE, 1, .15f);
		int next = m_moveNext;
		if (u.team == TEAM_PLAYER && m_ai.wild == 0) {
			if (next == PH_P_TARGET && m_target >= 0) {
				m_phase = PH_P_TARGET;
				FaceTowards(ui, m_b.u[m_target].x, m_b.u[m_target].z);
				ComputeOverlays();
				m_cx = m_b.u[m_target].x; m_cz = m_b.u[m_target].z; m_cLayer = m_b.u[m_target].deck;
				m_cursorValid = TRUE;
			} else OpenCommand();
		} else {
			if (m_ai.wild == 2) { ConfuseSelf(ui); return; }
			if (m_ai.target >= 0 && m_b.u[m_ai.target].alive) {
				const SkillDef* sd = m_ai.skill >= 0 ? &SkillInfo(m_ai.skill) : NULL;
				BOOL ok = sd ? (sd->rMax == 0 || CanTarget(ui, m_ai.target, m_ai.skill)) : CanTarget(ui, m_ai.target, -1);
				if (!ok && m_ai.wild && m_ai.skill < 0 && m_ai.target != ui) {
					const Unit& t = m_b.u[m_ai.target];
					ok = t.alive && InRangeOfUnit(m_b, u, u.x, u.z, UnitRangeMin(u), UnitRangeMax(u), t, true);
				}
				if (ok) { BeginAttack(ui, m_ai.target, m_ai.skill, m_ai.castX, m_ai.castZ); return; }
			}
			FinishUnitTurn(m_ai.wild ? 1 : 0);
		}
		return;
	}
	float f = m_moveT - seg;
	int a = m_movePts[seg], b = m_movePts[seg + 1];
	int ax = PackX(a), az = PackZ(a), bx = PackX(b), bz = PackZ(b);
	V3 pa = CellCenter(ax, az), pb = CellCenter(bx, bz);
	pa.y = m_b.map.WalkY(ax, az, PackDeck(a));
	pb.y = m_b.map.WalkY(bx, bz, PackDeck(b));
	float off = (UnitSpan(u) - 1) * 0.5f;
	pa.x += off; pa.z += off; pb.x += off; pb.z += off;
	V3 p = Lerp(pa, pb, Smooth01(f) * .3f + f * .7f);
	float dh = pb.y - pa.y;
	if (!fly && fabsf(dh) > .05f) {
		float hop = .2f + fabsf(dh) * .55f;
		float yl = Lerpf(pa.y, pb.y, f);
		if (dh > 0) yl = Lerpf(pa.y, pb.y, Smooth01(min(1.f, f * 1.6f)));
		p.y = yl + sinf(f * PI) * hop;
	} else if (fly) {
		p.y = Lerpf(pa.y, pb.y, Smooth01(f)) + sinf(f * 6.2831853f * 2.f) * 0.045f;
	}
	if (!fly && PackDeck(a) == PackDeck(b)) {
		int deck = PackDeck(a);
		int kind = 0, ly = 0;
		if (deck > 0) { kind = 2; ly = deck; }
		else {
			int ta = m_b.map.At(ax, az).t, tnb = m_b.map.At(bx, bz).t;
			if ((ta == T_WATER || ta == T_LAVA) && ta == tnb) { kind = 1; ly = ta; }
		}
		float gy = TacVisY(m_b.map, ax, az, p.x, p.z, kind, ly);
		if (p.y < gy + 0.02f) p.y = gy + 0.02f;
	}
	u.px = p.x; u.py = p.y; u.pz = p.z;
	u.face = atan2f((float)(bx - ax), (float)(bz - az));
	if (!fly && ((seg * 7 + (int)(f * 2)) % 2) == 0 && f > .45f && f < .55f) {
		Burst(v3(p.x, p.y + .02f, p.z), 2, .5f, .1f, .5f, .8f, .75f, .65f, 0, 0, -.2f, 0, .4f, .25f);
	}
}

void Game::FaceTowards(int ui, int tx, int tz)
{
	Unit& u = m_b.u[ui];
	if (tx == u.x && tz == u.z) return;
	u.face = atan2f((float)(tx - u.x), (float)(tz - u.z));
}

void Game::AimFormFace()
{
	if (m_cur < 0 || m_cmdSkill < 0 || !IsFormSkill(m_cmdSkill)) return;
	int form = FormOf(m_cmdSkill);
	if (form == FM_NONE || form == FM_CIRCLE || form == FM_SQUARE || form == FM_PAIR) return;
	Unit& u = m_b.u[m_cur];
	int fx, fz;
	if (m_cursorValid && (m_cx != u.x || m_cz != u.z) && FormFits(m_b, m_cur, m_cmdSkill, m_cx, m_cz)) {
		FormDir(m_cx - u.x, m_cz - u.z, fx, fz);
		u.face = atan2f((float)fx, (float)fz);
		return;
	}
	if (FormPickAim(m_b, m_cur, m_cmdSkill, fx, fz))
		u.face = atan2f((float)fx, (float)fz);
}

void Game::PickSkillCursor(int skill)
{
	if (m_cur < 0) return;
	const Unit& u = m_b.u[m_cur];
	const SkillDef& sd = SkillInfo(skill);
	m_cursorValid = FALSE;
	int wantX = 0, wantZ = 1;
	BOOL want = IsFormSkill(skill) && FormPickAim(m_b, m_cur, skill, wantX, wantZ);
	if (sd.rMax != 0) {
		if (want) {
			for (int i = 0; i < m_b.nUnits; i++) {
				if (!CanTarget(m_cur, i, skill)) continue;
				int ax, az;
				FormDir(m_b.u[i].x - u.x, m_b.u[i].z - u.z, ax, az);
				if (ax != wantX || az != wantZ) continue;
				m_cx = m_b.u[i].x; m_cz = m_b.u[i].z; m_cLayer = m_b.u[i].deck; m_cursorValid = TRUE; break;
			}
		}
		if (!m_cursorValid) {
			for (int i = 0; i < m_b.nUnits; i++) if (CanTarget(m_cur, i, skill)) { m_cx = m_b.u[i].x; m_cz = m_b.u[i].z; m_cLayer = m_b.u[i].deck; m_cursorValid = TRUE; break; }
		}
		if (!m_cursorValid && sd.area > 0) {
			if (want) {
				for (int z = 0; z < MAPN && !m_cursorValid; z++) for (int x = 0; x < MAPN; x++) {
					if (!m_ovAtk[z][x]) continue;
					int ax, az;
					FormDir(x - u.x, z - u.z, ax, az);
					if (ax != wantX || az != wantZ) continue;
					m_cx = x; m_cz = z; m_cursorValid = TRUE; break;
				}
			}
			if (!m_cursorValid) {
				for (int z = 0; z < MAPN && !m_cursorValid; z++) for (int x = 0; x < MAPN; x++)
					if (m_ovAtk[z][x]) { m_cx = x; m_cz = z; m_cursorValid = TRUE; break; }
			}
		}
	} else { m_cx = u.x; m_cz = u.z; m_cLayer = u.deck; m_cursorValid = TRUE; }
}

// ============================================================ アクション
static int CutPatFor(const Unit& u, int skill);
void Game::BeginAttack(int ui, int target, int skill, int cx, int cz)
{
	Unit& a = m_b.u[ui];
	m_cmdFaced = 0;
	if (Map::In(cx, cz) && (cx != a.x || cz != a.z)) FaceTowards(ui, cx, cz);
	else if (target != ui) FaceTowards(ui, m_b.u[target].x, m_b.u[target].z);
	if (IsFormSkill(skill)) {
		int fx, fz;
		if (Map::In(cx, cz) && (cx != a.x || cz != a.z)) FormDir(cx - a.x, cz - a.z, fx, fz);
		else if (!FormPickAim(m_b, ui, skill, fx, fz)) FormDir(0, 1, fx, fz);
		a.face = atan2f((float)fx, (float)fz);
	}
	for (int i = 0; i < m_b.nUnits; i++) {
		m_preHp[i] = m_b.u[i].hp;
		m_dispHp[i] = (float)m_b.u[i].hp;
		const Unit& vu = m_b.u[i];
		VisAil& va = m_visAil[i];
		va.poison = vu.poison; va.slow = vu.slow; va.defDown = vu.defDown;
		va.sleep = vu.sleep; va.confuse = vu.confuse; va.stun = vu.stun; va.freeze = vu.freeze;
		va.burn = vu.burn; va.bleed = vu.bleed; va.blind = vu.blind; va.silence = vu.silence;
	}
	if (target >= 0 && target < m_b.nUnits && m_b.u[ui].team == TEAM_PLAYER && m_b.u[target].team == TEAM_ENEMY) {
		int g = m_b.u[target].grp;
		int fresh = 0;
		if (g >= 0) {
			for (int i = 0; i < m_b.nUnits; i++)
				if (m_b.u[i].alive && m_b.u[i].grp == g && m_b.u[i].alert == 0) fresh = 1;
			WakeGroup(m_b, g);
		} else if (m_b.u[target].alive && m_b.u[target].alert == 0) {
			m_b.u[target].alert = 1;
			JoinTurnClock(m_b, target);
			fresh = 1;
		}
		if (fresh) AddBanner(TT(TX_BT_REIN), NULL, RGBA8(255, 210, 120), 1, 1.4f, MSG_DEPTH);
	}
	m_strkN = ResolveAttack(m_b, ui, target, skill, m_rng, m_strk, 96, cx, cz);
	m_strkN = AppendAssists(m_b, m_rng, m_strk, m_strkN, 96);
	m_seqN = 0;
	for (int i = 0; i < m_strkN && m_seqN < 24; i++) {
		const Strike& s = m_strk[i];
		// 同じ段だけまとめる。段が違うときは振り直しなので、1対1でも段の数だけ当たる。
		if (m_seqN > 0 && s.attacker == m_seq[m_seqN - 1].attacker && s.counter == m_seq[m_seqN - 1].counter
			&& s.skill == m_seq[m_seqN - 1].skill && m_seq[m_seqN - 1].nHits > 0 && m_seq[m_seqN - 1].nHits < MAX_UNITS
			&& m_seq[m_seqN - 1].hits[0].volley == s.volley) {
			SeqStep& p = m_seq[m_seqN - 1];
			p.hits[p.nHits++] = s;
			continue;
		}
		SeqStep& st = m_seq[m_seqN++];
		st.attacker = s.attacker; st.defender = s.defender; st.skill = s.skill; st.counter = s.counter;
		st.nHits = 1; st.hits[0] = s;
	}
	if (m_seqN == 0) {
		SeqStep& st = m_seq[m_seqN++];
		memset(&st, 0, sizeof(st));
		st.attacker = ui; st.defender = target; st.skill = skill; st.nHits = 0;
	}
	BOOL anyCrit = FALSE;
	for (int i = 0; i < m_strkN; i++) if (m_strk[i].crit && m_strk[i].hit) anyCrit = TRUE;
	m_seqCaster = ui;
	m_seqPrimary = m_strkN > 0 ? m_strk[0].defender : target;
	m_seqSkill = skill;
	m_seqCur = 0;
	m_seqT = 0;
	m_seqImpact = 0;
	m_ult = IsUltimateSkill(skill) || (anyCrit && skill >= 0);
	m_cutKind = IsSummonId(skill) ? 3 : (IsCraftId(skill) ? 1 : (IsFormSkill(skill) ? 2 : 0));
	m_cutPat = CutPatFor(m_b.u[ui], skill);
	m_sumOn = 0;
	m_sumHit = 0;
	m_seqCut = m_ult || m_cutKind != 0 || (!m_save.battleAnim && (skill >= 0 || anyCrit));
	if (m_ult) m_flashWhite = IsCraftSuper(skill) ? 1.f : 0.35f;
	if (skill == SKL_TRANSMUTE && m_save.itemN[IT_ORE] > 0) m_save.itemN[IT_ORE]--;
	m_deferPopups.clear();
	if (m_seqCut) {
		m_cutStage = 0;
		m_cutStageT = 0;
		m_cutA = ui;
		m_cutD = (m_seqPrimary == ui) ? -1 : m_seqPrimary;
		GatherCut();
		int side = 0, idx = 0;
		m_cutPosA = CutFind(m_cutA, &side, &idx) ? CutStand(side, idx) : v3(0, 0, 0);
		m_cutPosD = (m_cutD >= 0 && CutFind(m_cutD, &side, &idx)) ? CutStand(side, idx) : v3(1.25f, 0, 0);
	} else {
		m_cutKind = 0;
		m_cutPat = 0;
		m_cutAllyN = m_cutFoeN = 0;
		if (skill >= 0)
			AddBanner(IsCraftId(skill) ? CraftName(skill) : SkillLabel(skill), NULL, ElemColor(SkillInfo(skill).element), 2, 1.4f, MSG_SLAB);
	}
	m_phase = PH_ACTION;
	m_phaseT = 0;
	m_proj.active = 0;
	m_ovDirty = TRUE;
	memset(m_ovMove, 0, sizeof(m_ovMove));
	memset(m_ovAtk, 0, sizeof(m_ovAtk));
	memset(m_ovThreat, 0, sizeof(m_ovThreat));
}

static int CutPatFor(const Unit& u, int skill)
{
	if (skill < 0) return (u.monster >= 0) ? 13 : 0;
	if (IsSummonId(skill) || IsCraftId(skill) || IsFormSkill(skill)) {
		if (u.monster >= 0) return 13;
	}
	const SkillDef& sd = SkillInfo(skill);
	if (sd.kind == SK_HEAL) {
		if (sd.effect == EF_RAISE || sd.area >= 3 || sd.shape == SH_FAN) return 11;
		return 10;
	}
	if (sd.kind == SK_BUFF) return 12;
	if (sd.kind == SK_MAGIC) {
		if (sd.element == EL_ICE || sd.element == EL_HOLY) return 6;
		if (sd.element == EL_THUNDER || sd.element == EL_POISON) return 7;
		if (sd.element == EL_DARK) return 8;
		if (sd.element == EL_WIND || sd.element == EL_EARTH) return 9;
		return 5;
	}
	int st = UnitMotionStyle(u, skill);
	if (st == AS_SHOOT || st == AS_SHOOT1) return 4;
	if (u.monster >= 0) return 13;
	if (sd.shape == SH_LINE || st == AS_STAB) return 3;
	if (sd.shape == SH_FAN || st == AS_SPIN) return 2;
	if (sd.power >= 150 || st == AS_CHOP2) return 1;
	return 0;
}

BOOL Game::CutFind(int ui, int* side, int* idx) const
{
	for (int i = 0; i < m_cutAllyN; i++) if (m_cutAlly[i] == ui) { if (side) *side = 0; if (idx) *idx = i; return TRUE; }
	for (int i = 0; i < m_cutFoeN; i++) if (m_cutFoe[i] == ui) { if (side) *side = 1; if (idx) *idx = i; return TRUE; }
	return FALSE;
}

V3 Game::CutStand(int side, int idx) const
{
	BOOL staged = m_cutKind != 0 || (m_cutPat >= 5 && m_cutPat <= 12);
	if (staged) {
		if (side == 0 && idx >= 0 && idx < m_cutAllyN) return m_cutAllyPos[idx];
		if (side == 1 && idx >= 0 && idx < m_cutFoeN) return m_cutFoePos[idx];
	}
	if (m_cutFoeN <= 0) {
		float n = (float)max(1, m_cutAllyN);
		float x = (idx - (n - 1.f) * .5f) * .9f;
		return v3(x, 0, (idx % 2) ? .22f : -.22f);
	}
	if (side == 1 && m_cutFoeN > 6) {
		int cols = m_cutFoeN > 12 ? 6 : 4;
		int col = idx % cols;
		int row = idx / cols;
		return v3(1.15f + row * .7f, 0, (col - (cols - 1) * .5f) * .48f);
	}
	if (side == 0 && m_cutAllyN > 6) {
		int col = idx % 3;
		int row = idx / 3;
		return v3(-1.15f - row * .7f, 0, (col - 1) * .55f);
	}
	float x = (side == 0 ? -1.15f : 1.15f) + (side == 0 ? -1.f : 1.f) * idx * .82f;
	float z = (idx % 2) ? .3f : -.14f;
	return v3(x, 0, side == 0 ? z : -z);
}

void Game::GatherCut()
{
	m_cutAllyN = m_cutFoeN = 0;
	auto add = [&](int id) {
		if (id < 0 || id >= m_b.nUnits) return;
		int* arr = m_b.u[id].team == TEAM_PLAYER ? m_cutAlly : m_cutFoe;
		int& n = m_b.u[id].team == TEAM_PLAYER ? m_cutAllyN : m_cutFoeN;
		int cap = m_b.u[id].team == TEAM_PLAYER ? CUT_ALLY_MAX : CUT_FOE_MAX;
		if (n >= cap) return;
		for (int i = 0; i < n; i++) if (arr[i] == id) return;
		arr[n++] = id;
	};
	add(m_cutA);
	if (m_cutD >= 0) add(m_cutD);
	for (int k = 0; k < m_strkN; k++) { add(m_strk[k].attacker); add(m_strk[k].defender); }
	if (m_cutKind == 2 && m_seqSkill >= 0 && m_cutA >= 0) {
		int ax = m_b.u[m_cutA].x, az = m_b.u[m_cutA].z;
		if (m_cutD >= 0) { ax = m_b.u[m_cutD].x; az = m_b.u[m_cutD].z; }
		int mem[8];
		int nm = FormMembers(m_b, m_cutA, m_seqSkill, ax, az, mem, 8);
		for (int i = 0; i < nm; i++) add(mem[i]);
	}
	LayoutSpecialCut();
}

void Game::LayoutSpecialCut()
{
	if (m_cutKind == 0) {
		if (m_cutPat < 5 || m_cutPat > 12) return;
		for (int i = 0; i < m_cutAllyN; i++) {
			float z = (i & 1) ? .42f : -.22f;
			m_cutAllyPos[i] = v3(-1.85f - i * .38f, 0, z);
		}
		for (int i = 0; i < m_cutFoeN; i++) {
			int cols = m_cutFoeN > 6 ? 4 : 3;
			int col = i % cols;
			int row = i / cols;
			m_cutFoePos[i] = v3(.72f + row * .62f, 0, (col - (cols - 1) * .5f) * .5f);
		}
		return;
	}
	for (int i = 0; i < m_cutFoeN; i++) {
		int cols = m_cutFoeN > 12 ? 6 : (m_cutFoeN > 6 ? 4 : 3);
		int col = i % cols;
		int row = i / cols;
		float z = (col - (cols - 1) * .5f) * .52f;
		float x = 1.45f + row * .72f;
		if (m_cutKind == 1) x += .35f;
		m_cutFoePos[i] = v3(x, 0, z);
	}
	if (m_cutKind == 1) {
		for (int i = 0; i < m_cutAllyN; i++) {
			float z = (i == 0) ? 0.f : ((i & 1) ? .78f : -.78f);
			float x = (i == 0) ? -.42f : -1.45f - (i - 1) * .2f;
			m_cutAllyPos[i] = v3(x, 0, z);
		}
		return;
	}
	if (m_cutA < 0) return;
	const Unit& u = m_b.u[m_cutA];
	float ex = sinf(u.face), ez = cosf(u.face);
	if (m_cutD >= 0) {
		ex = (float)(m_b.u[m_cutD].x - u.x);
		ez = (float)(m_b.u[m_cutD].z - u.z);
	}
	float elen = sqrtf(ex * ex + ez * ez);
	if (elen < .01f) { ex = 1.f; ez = 0.f; }
	else { ex /= elen; ez /= elen; }
	for (int i = 0; i < m_cutAllyN; i++) {
		int id = m_cutAlly[i];
		float dx = (float)(m_b.u[id].x - u.x);
		float dz = (float)(m_b.u[id].z - u.z);
		float fwd = dx * ex + dz * ez;
		float lat = dx * ez - dz * ex;
		// 前に出た仲間が敵の立ち位置に重ならないよう、前後と左右を舞台に収める。
		m_cutAllyPos[i] = v3(Clampf(-1.2f + fwd * .92f, -3.2f, .45f), 0, Clampf(lat * .92f, -1.7f, 1.7f));
	}
}

float Game::CutIntroLen() const
{
	if (m_cutKind == 1) return IsCraftSuper(m_seqSkill) ? 2.35f : 1.55f;
	if (m_cutKind == 2) return 1.75f;
	if (m_cutKind == 3) return 2.45f;
	if (m_ult) return 2.15f;
	switch (m_cutPat) {
	case 1: return 1.35f;
	case 2: return 1.25f;
	case 3: return 0.95f;
	case 4: return 1.15f;
	case 5: case 6: case 7: case 8: case 9: return 1.45f;
	case 10: return 1.2f;
	case 11: return 1.55f;
	case 12: return 1.3f;
	case 13: return 1.25f;
	default: return m_seqSkill >= 0 ? 1.05f : .45f;
	}
}

void Game::PoseFormAllies()
{
	if (m_cutKind != 2 || m_seqSkill < 0 || m_cutA < 0) return;
	int form = FormOf(m_seqSkill);
	int allyClip = CL_1H_SLICE;
	switch (form) {
	case FM_CROSS: case FM_WALL: case FM_SQUARE: allyClip = CL_2H_SPIN; break;
	case FM_BACK: case FM_CHAIN: allyClip = CL_1H_STAB; break;
	case FM_TRI: allyClip = CL_2H_CHOP; break;
	case FM_DIA: allyClip = CL_1H_CHOP; break;
	case FM_ECHELON: allyClip = CL_1H_SHOOT; break;
	case FM_PINCER: allyClip = CL_2H_CHOP; break;
	case FM_WING: allyClip = CL_2H_SPIN; break;
	case FM_CIRCLE: allyClip = CL_CAST_LONG; break;
	case FM_PAIR: allyClip = CL_CAST_RAISE; break;
	default: break;
	}
	for (int i = 0; i < m_cutAllyN; i++) {
		int id = m_cutAlly[i];
		if (id == m_cutA || id < 0 || PoseKeep(m_act[id], m_b.u[id], m_dieT[id], m_dispHp[id])) continue;
		Actor& ac = m_act[id];
		ActorPlay(ac, allyClip, 0, .12f);
		ac.skillPose = m_seqSkill;
		// 技名のあいだは構えで止め、攻防に入ったら主役と同時に振り下ろす。
		float wind = ActorClipLen(ac, m_lib, m_r, allyClip) * ActorWindup(ac, m_r, allyClip);
		ac.animSpeed = Clampf(wind / max(0.25f, CutIntroLen()), 0.12f, 1.15f);
		m_b.u[id].flash = 1.f;
	}
}

void Game::CutDressFx()
{
	if (m_cutA < 0) return;
	if (m_cutStage != 1 && m_cutStage != 2) return;
	const SkillDef* sd = m_seqSkill >= 0 ? &SkillInfo(m_seqSkill) : NULL;
	DWORD ec = sd ? ElemColor(sd->element) : RGBA8(170, 210, 255);
	float r = (ec & 255) / 255.f, g = ((ec >> 8) & 255) / 255.f, b = ((ec >> 16) & 255) / 255.f;
	if (m_cutKind == 0) {
		V3 a = m_cutPosA + v3(0, .15f, 0);
		V3 d = (m_cutD >= 0 ? m_cutPosD : v3(1.2f, 0, 0)) + v3(0, .5f, 0);
		float u = fmodf(m_cutStageT * 2.2f, 1.f);
		switch (m_cutPat) {
		case 1:
			if (m_cutStage == 2) Burst(Lerp(a + v3(0, .55f, 0), d, u), 3, .55f, .1f, .2f, r, g, b, 3, 1, .2f, .05f, .1f);
			break;
		case 2: {
			float ang = m_time * 8.f;
			Burst(a + v3(cosf(ang) * .85f, .42f, sinf(ang) * .85f), 2, .35f, .1f, .2f, r, g, b, 6, 1, 0, 0, .2f);
			break;
		}
		case 3:
			if (m_cutStage == 2) Burst(Lerp(a + v3(0, .7f, 0), d, fmodf(m_cutStageT * 3.2f, 1.f)), 2, .2f, .07f, .14f, r, g, b, 3, 1, 0, .06f, 0);
			break;
		case 4:
			Burst(Lerp(a + v3(0, .85f, 0), d, fmodf(m_cutStageT * 3.4f, 1.f)), 1, 0, .07f, .12f, r, g, b, 0, 1, 0, .08f, 0);
			break;
		case 5:
			Burst(a + v3(0, .2f, 0), 3, .85f, .14f, .35f, 1.f, .42f, .12f, 5, 1, -1.2f, 0, .6f);
			break;
		case 6:
			Burst(a + v3(0, .9f, 0), 2, .22f, .12f, .4f, .7f, .9f, 1.f, 2, 1, .2f, 0, .1f);
			break;
		case 7:
			Burst(Lerp(a + v3(0, 1.1f, 0), d, fmodf(m_cutStageT * 4.2f, 1.f)), 2, 2.2f, .06f, .1f, 1.f, 1.f, .55f, 3, 1, 0, .12f, 0);
			break;
		case 8:
			Burst(a + v3(0, .08f, 0), 2, .4f, .16f, .45f, .32f, .1f, .48f, 0, 0, .15f, 0, .2f);
			break;
		case 9:
			Burst(a + v3(0, .5f, 0), 2, 1.7f, .1f, .28f, .72f, 1.f, .82f, 2, 1, 0, .05f, .4f);
			break;
		case 10:
		case 11:
			Burst((m_cutD >= 0 ? m_cutPosD : m_cutPosA) + v3(0, .22f, 0), 2, .2f, .1f, .35f, .55f, 1.f, .7f, 1, 1, -.4f, 0, .5f);
			break;
		case 12:
			Burst(a + v3(0, .05f, 0), 1, .15f, .12f, .4f, 1.f, .92f, .55f, 1, 1, .2f, 0, .3f);
			break;
		case 13:
			Burst(a + v3(0, .05f, 0), 2, .6f, .1f, .25f, r, g, b, 2, 1, .3f, 0, .4f);
			break;
		default:
			if (m_cutStage == 2) Burst(Lerp(a + v3(0, .7f, 0), d, u), 2, .4f, .08f, .18f, r, g, b, 3, 1, 0, .04f, 0);
			break;
		}
		return;
	}
	if (m_cutKind == 1) {
		BOOL ougi = IsCraftSuper(m_seqSkill);
		int rings = ougi ? 2 : 1;
		float spin = m_time * (ougi ? 3.1f : 2.2f);
		for (int k = 0; k < rings; k++) {
			float ang = spin + k * PI;
			float rad = (k == 0 ? 1.05f : 1.55f);
			V3 p = m_cutPosA + v3(cosf(ang) * rad, .1f + k * .08f, sinf(ang) * rad * .72f);
			Burst(p, 2, .35f, .15f, .32f, r, g, b, 6, 1, .5f, .02f, .7f);
		}
		float col = fmodf(m_cutStageT * .8f, 1.2f);
		Burst(m_cutPosA + v3(0, .15f + col, 0), 1, .15f, .12f, .4f, 1.f, .92f, .62f, 1, 1, -.15f, 0, 1.1f);
		if (m_cutStage == 2) {
			for (int i = 0; i < m_cutFoeN; i++) {
				float u = fmodf(m_cutStageT * 1.6f + i * .17f, 1.f);
				V3 p = Lerp(m_cutPosA + v3(0, 1.05f, 0), m_cutFoePos[i] + v3(0, .8f, 0), u);
				Burst(p, 1, 0, .18f, .16f, r, g, b, 3, 1, 0, .08f, 0);
			}
		}
		return;
	}
	V3 home = m_cutAllyN > 0 ? m_cutAllyPos[0] : m_cutPosA;
	Burst(home + v3(0, .05f, 0), 1, .2f, .1f, .28f, .55f, .82f, 1.f, 2, 1, .3f, 0, .4f);
	for (int i = 1; i < m_cutAllyN; i++) {
		float u = fmodf(m_time * .65f + i * .21f, 1.f);
		V3 p = Lerp(home, m_cutAllyPos[i], u) + v3(0, .12f, 0);
		Burst(p, 1, 0, .11f, .22f, .45f, .78f, 1.f, 2, 1, 0, 0, .15f);
		Burst(m_cutAllyPos[i] + v3(0, .04f, 0), 1, .1f, .08f, .25f, 1.f, .86f, .45f, 1, 1, .2f, 0, .3f);
	}
	if (m_cutStage == 2) {
		for (int i = 0; i < m_cutAllyN && i < m_cutFoeN + 1; i++) {
			V3 from = m_cutAllyPos[i] + v3(0, .9f, 0);
			V3 to = (m_cutFoeN > 0 ? m_cutFoePos[min(i, m_cutFoeN - 1)] : v3(1.4f, 0, 0)) + v3(0, .7f, 0);
			float u = fmodf(m_cutStageT * 1.3f + i * .13f, 1.f);
			Burst(Lerp(from, to, u), 1, 0, .14f, .14f, .7f, .88f, 1.f, 6, 1, 0, .06f, 0);
		}
	}
}

M4 Game::CutSceneXform() const
{
	float ax = 0.f, az = 0.f, ay = 0.f;
	int an = 0;
	auto add = [&](int id) {
		if (id < 0 || id >= m_b.nUnits) return;
		ax += m_b.u[id].px;
		az += m_b.u[id].pz;
		an++;
	};
	add(m_cutA);
	if (m_cutD >= 0) add(m_cutD);
	if (an > 0) { ax /= (float)an; az /= (float)an; }
	int atx = (int)floorf(ax), atz = (int)floorf(az);
	if (Map::In(atx, atz)) ay = m_b.map.StandY(atx, atz);
	else if (m_cutA >= 0 && m_cutA < m_b.nUnits) ay = m_b.map.StandY(m_b.u[m_cutA].x, m_b.u[m_cutA].z);
	float face = 0.f;
	if (m_cutA >= 0 && m_cutA < m_b.nUnits && m_cutD >= 0 && m_cutD < m_b.nUnits) {
		float dx = m_b.u[m_cutD].px - m_b.u[m_cutA].px;
		float dz = m_b.u[m_cutD].pz - m_b.u[m_cutA].pz;
		face = (dx * dx + dz * dz > 0.04f) ? atan2f(dx, dz) : m_b.u[m_cutA].face;
	} else if (m_cutA >= 0 && m_cutA < m_b.nUnits) face = m_b.u[m_cutA].face;
	return MMul(MTrans(-ax, -ay, -az), MRotY(PI * 0.5f - face));
}

V3 Game::ActorPos(int ui) const
{
	float kb = m_act[ui].recoil * .42f;
	if (m_seqCut && m_cutStage >= 1 && m_cutStage <= 3) {
		int side = 0, idx = 0;
		if (CutFind(ui, &side, &idx)) {
			V3 p = CutStand(side, idx);
			float lx = m_lunge[ui];
			p.x += side == 0 ? (lx - kb) : (kb - lx);
			p.y += m_b.u[ui].hover;
			if (m_cutStage == 3) {
				const Unit& u = m_b.u[ui];
				V3 mapP = UnitGroundPos(u);
				mapP.y += u.hover;
				V3 onStage = MXformPt(CutSceneXform(), mapP);
				float uBlend = Smooth01(m_cutStageT / 0.3f);
				p = Lerp(p, onStage, uBlend);
			}
			return p;
		}
	}
	const Unit& u = m_b.u[ui];
	const Actor& a = m_act[ui];
	float f = m_faceShow[ui];
	return v3(u.px + sinf(f) * m_lunge[ui] + a.recoilX * kb, u.py + u.hover, u.pz + cosf(f) * m_lunge[ui] + a.recoilZ * kb);
}

float Game::ActorFace(int ui) const
{
	if (m_seqCut && m_cutStage >= 1 && m_cutStage <= 3) {
		int side = 0, idx = 0;
		if (CutFind(ui, &side, &idx)) {
			if (m_cutFoeN <= 0) return PI - .45f;
			return side == 0 ? PI * .5f : -PI * .5f;
		}
	}
	return m_faceShow[ui];
}

BOOL Game::UnitInArena(int ui) const
{
	return CutFind(ui, NULL, NULL);
}

void Game::SeqImpact(const SeqStep& st, int volley)
{
	BOOL cutView = m_seqCut && m_cutStage >= 1 && m_cutStage <= 3;
	const SkillDef* sd = st.skill >= 0 ? &SkillInfo(st.skill) : NULL;
	int el = sd ? sd->element : EL_NONE;
	int kind = sd ? sd->kind : SK_PHYS;
	// 範囲技で同じ音を的の数だけ重ねると割れるので、一度の着弾では種類ごとに一回だけ鳴らす。
	int heard = 0;
	auto sfxOnce = [&](int bit) { if (heard & bit) return FALSE; heard |= bit; return TRUE; };
	for (int k = 0; k < st.nHits; k++) {
		const Strike& h = st.hits[k];
		if (h.volley != volley) continue;
		int d = h.defender;
		Unit& du = m_b.u[d];
		BOOL visible = !cutView || UnitInArena(d);
		float visH = m_act[d].height;
		if (cutView && visH > 1.32f) visH = 1.32f;
		float zPop = cutView ? -.48f : 0.f;
		V3 head = ActorPos(d) + v3(0, visH * .9f + .12f, zPop);
		V3 mid = ActorPos(d) + v3(0, visH * .5f, cutView ? -.22f : 0.f);
		if (!cutView || !visible) {
			V3 mp = v3(du.px, du.py + du.hover, du.pz);
			head = mp + v3(0, m_act[d].height * .9f + .1f, 0);
			mid = mp + v3(0, m_act[d].height * .5f, 0);
		}
		if (h.volley > 0) {
			head.y += h.volley * 0.46f;
			head.x += (h.volley & 1) ? 0.26f : -0.22f;
		}
		wchar_t s[48];
		auto pop = [&](const wchar_t* txt, DWORD col, int style, int pat, float delay) {
			if (visible) AddPopup(head, txt, col, style, pat, delay);
			else {
				Popup pp = {};
				pp.p = head; wcsncpy_s(pp.s, txt, _TRUNCATE); pp.col = col; pp.style = style; pp.pat = pat;
				pp.life = style == 2 ? 1.6f : 1.25f; pp.size = style == 2 ? 1.35f : 1.f; pp.delay = delay;
				m_deferPopups.push_back(pp);
			}
		};
		if (kind == SK_HEAL) {
			m_dispHp[d] = min((float)du.ch.stat[S_HP], m_dispHp[d] - h.dmg);
			swprintf_s(s, L"+%s", TacNum(-h.dmg));
			pop(s, RGBA8(120, 255, 150), 0, MSG_RISE, 0);
			if (sd && sd->effect == EF_RAISE && h.hit) {
				m_dieT[d] = -1;
				du.dissolve = 0;
				du.flash = 1.f;
				ActorPlay(m_act[d], CL_SPAWN, 0, .8f);
				pop(sd ? SkillLabel(st.skill) : TT(TX_SK_RAISE), RGBA8(255, 236, 170), 1, MSG_RISE, .12f);
				Burst(mid, 22, 2.4f, .1f, 1.1f, 1.f, .92f, .55f, 2, 1, -1.2f, 0, .9f);
			}
			if (sd && sd->effect == EF_CLEANSE && h.landed) pop(EffectWord(EF_CLEANSE), RGBA8(180, 230, 255), 1, MSG_FACE, .2f);
			{
				wchar_t an[40], dn[40], line[96];
				UnitTitle(m_b.u[h.attacker], an, 40);
				UnitTitle(du, dn, 40);
				swprintf_s(line, L"%s  %s  %+d", an, dn, -h.dmg);
				AddLog(line, RGBA8(120, 255, 160));
			}
			if (sfxOnce(1)) TacSfxHit(st.skill, kind, el, UnitMotionStyle(m_b.u[h.attacker], st.skill), 0);
			if (sd && sd->effect == EF_CLEANSE && h.landed && sfxOnce(2)) TacSfxStatus(EF_CLEANSE, 0);
			if (visible || !cutView) ElementFx(EL_NONE, SK_HEAL, mid, mid, FALSE);
			continue;
		}
		if (kind == SK_BUFF) {
			pop(TT(TX_BT_BUFF), RGBA8(150, 210, 255), 1, MSG_FACE, 0);
			if (sfxOnce(1)) TacSfx(TS_BUFF);
			if (visible || !cutView) ElementFx(EL_NONE, SK_BUFF, mid, mid, FALSE);
			continue;
		}
		if (!h.hit) {
			BOOL landed = FALSE;
			for (int q = 0; q < st.nHits; q++) if (st.hits[q].defender == d && st.hits[q].hit) landed = TRUE;
			if (!landed && !PoseGround(m_act[d], du, m_dieT[d], m_dispHp[d])) {
				m_act[d].recoil = 0.f;
				ActorPlay(m_act[d], CL_DODGE, 0, .06f);
			}
			pop(TT(TX_BT_MISS), RGBA8(200, 220, 255), 1, MSG_DROP, 0);
			{
				wchar_t an[40], dn[40], line[96];
				UnitTitle(m_b.u[h.attacker], an, 40);
				UnitTitle(du, dn, 40);
				swprintf_s(line, L"%s  %s  %s", an, dn, TT(TX_BT_MISS));
				AddLog(line, RGBA8(180, 200, 230));
			}
			if (sfxOnce(4)) TacSfxMiss(kind, UnitMotionStyle(m_b.u[h.attacker], st.skill));
			continue;
		}
		m_dispHp[d] = max(0.f, m_dispHp[d] - h.dmg);
		if (h.guts) m_dispHp[d] = max(1.f, m_dispHp[d]);
		if (h.drain > 0) {
			m_dispHp[h.attacker] = min((float)m_b.u[h.attacker].ch.stat[S_HP], m_dispHp[h.attacker] + h.drain);
			swprintf_s(s, L"+%s", TacNum(h.drain));
			AddPopup(ActorPos(h.attacker) + v3(0, min(m_act[h.attacker].height, cutView ? 1.32f : m_act[h.attacker].height) + .1f, cutView ? -.4f : 0.f), s, RGBA8(120, 255, 150), 0, MSG_RISE, .25f);
		}
		swprintf_s(s, L"%s", TacNum(h.dmg));
		BOOL followDead = h.overkill && !h.kill && du.hp <= 0;
		if (h.crit) {
			pop(TT(TX_BT_CRITICAL), RGBA8(255, 230, 80), 2, MSG_BLUR, 0);
			if (sfxOnce(8)) TacSfx(TS_CRIT);
			pop(s, RGBA8(255, 220, 60), 2, MSG_BLUR, .12f);
			Shake(.6f);
			if (cutView) m_flashWhite = 1.f;
		} else {
			pop(s, kind == SK_MAGIC ? RGBA8(200, 170, 255) : RGBA8(255, 255, 255), 0, MSG_BURST, 0);
			Shake(.22f);
			BOOL ally = du.team == TEAM_PLAYER;
			if (sfxOnce(ally && kind != SK_MAGIC ? 16 : 32)) TacSfxHit(st.skill, kind, el, UnitMotionStyle(m_b.u[h.attacker], st.skill), ally);
		}
		if (visible || !cutView) {
			int playEl = el;
			if (playEl == EL_NONE && h.attacker >= 0 && h.attacker < m_b.nUnits) {
				const GearDef* w = GearOf(m_b.u[h.attacker].ch.reserved[6]);
				if (w && w->kind == 0 && w->elem >= EL_FIRE && w->elem <= EL_POISON) playEl = w->elem;
			}
			StrikeFx(st.skill, playEl, kind, UnitMotionStyle(m_b.u[h.attacker], st.skill), ActorPos(h.attacker), mid, h.crit || cutView);
			if (h.react && h.react != playEl)
				ElementFx(h.react, SK_MAGIC, mid, mid, TRUE);
			if (h.crit) Burst(mid, 30, 7.f, .09f, .5f, 1.f, .95f, .6f, 3, 1, 3.f, .06f, .2f);
		}
		if (h.hit) {
			// 属性入れ替え（炎→氷など）は直前の属性を残して滑らかにクロスフェードする。
			if (h.reactFrom > 0) { m_ailBlend[d] = 1.05f; m_ailBlendEl[d] = h.reactFrom; }
			// 食らったこの瞬間に初めて見た目の異常を更新（予測解決の段階では動かさない）。
			VisAil& va = m_visAil[d];
			va.poison = du.poison; va.slow = du.slow; va.defDown = du.defDown;
			va.sleep = du.sleep; va.confuse = du.confuse; va.stun = du.stun; va.freeze = du.freeze;
			va.burn = du.burn; va.bleed = du.bleed; va.blind = du.blind; va.silence = du.silence;
		}
		{
			wchar_t an[40], dn[40], line[160];
			UnitTitle(m_b.u[h.attacker], an, 40);
			UnitTitle(du, dn, 40);
			const wchar_t* sk = sd ? (IsCraftId(st.skill) ? CraftName(st.skill) : SkillLabel(st.skill)) : TT(TX_BT_ATTACK);
			swprintf_s(line, L"%s  %s  %s  %d%s", an, sk, dn, h.dmg, h.crit ? L" !" : L"");
			if (h.overkill) { wcscat_s(line, L"  "); wcscat_s(line, TT(TX_BT_OVERKILL)); }
			if (h.guts) { wcscat_s(line, L"  "); wcscat_s(line, TT(TX_BT_GUTS)); }
			AddLog(line, h.guts ? RGBA8(255, 210, 120) : (h.overkill ? RGBA8(255, 170, 80) : (h.crit ? RGBA8(255, 220, 80) : RGBA8(235, 235, 240))));
		}
		if (h.overkill) pop(TT(TX_BT_OVERKILL), RGBA8(255, 170, 80), 1, MSG_BLUR, h.crit ? .28f : .14f);
		if (h.guts) { pop(TT(TX_BT_GUTS), RGBA8(255, 196, 96), 1, MSG_FACE, .22f); if (sfxOnce(64)) TacSfx(TS_GUTS); }
		if (!followDead) {
		du.flash = 1.f;
		{
			float dx = du.px - m_b.u[h.attacker].px, dz = du.pz - m_b.u[h.attacker].pz;
			float len = sqrtf(dx * dx + dz * dz);
			if (len < .05f) { dx = -sinf(du.face); dz = -cosf(du.face); len = 1.f; }
			m_act[d].recoilX = dx / len;
			m_act[d].recoilZ = dz / len;
			m_act[d].recoil = h.crit ? 1.f : .75f;
		}
		if (h.kill) {
			if (du.team == TEAM_PLAYER) { if (sfxOnce(128)) TacSfx(TS_FALL); }
			else if (sfxOnce(256)) TacSfx(TS_DEATH);
			if (m_act[d].clip != CL_DEATH) ActorPlay(m_act[d], CL_DEATH, 0, .08f);
			if (m_dieT[d] < 0) m_dieT[d] = 0;
		} else if (!PoseGround(m_act[d], du, m_dieT[d], m_dispHp[d])) {
			int guard = !h.crit && kind != SK_MAGIC && UnitFacing(m_b.u[h.attacker], du) == 0;
			ActorPlay(m_act[d], guard ? CL_BLOCK : (h.crit ? CL_HIT_B : CL_HIT_A), 0, .06f);
		}
		}
		if (!followDead && sd && sd->effect != EF_NONE && sd->effect != EF_DRAIN && sd->effect != EF_IGNDEF && !h.kill) {
			int tx = -1;
			switch (sd->effect) {
			case EF_POISON: tx = TX_BT_POISON; break;
			case EF_SLOW: tx = TX_BT_SLOWED; break;
			case EF_DEFDOWN: tx = TX_ST_DEF; break;
			}
			const wchar_t* ail = EffectWord(sd->effect);
			BOOL blocked = UnitResistsEffect(m_b.u[d], sd->effect);
			if (blocked || ((tx >= 0 || ail) && h.landed)) {
				wchar_t e[48];
				if (blocked) swprintf_s(e, L"%s", LangPick14(L"無効", L"Blocked", L"Bloqué", L"Annullato", L"Anulado", L"무효", L"无效", L"مُبطل", L"Блок", L"Geblockt", L"Anulado", L"Geblokkeerd", L"Zablokowane", L"Geçersiz"));
				else if (ail) swprintf_s(e, L"%s", ail);
				else swprintf_s(e, L"%s%s", TT(tx), sd->effect == EF_DEFDOWN ? L"↓" : L"");
				pop(e, blocked ? RGBA8(180, 220, 255) : RGBA8(220, 160, 255), 1, blocked ? MSG_DROP : MSG_ORBIT, .35f);
				if (sfxOnce(blocked ? 512 : 1024)) TacSfxStatus(sd->effect, blocked ? 1 : 0);
			}
		}
	}
}

// 弓を装備していれば弦を引く。既定の弩は両手で構えて撃つ。
static int UnitAttackClip(const Unit& u, int variant, int skill)
{
	return AttackClipFor(UnitMotionStyle(u, skill), variant, skill);
}

static int CutClipFor(const Unit& u, int skill, int variant)
{
	if (IsSummonId(skill)) {
		int fx = skill >= 0 ? ((skill * 11) % 12) : 0;
		if (fx == SUM_FX_FIRE || fx == SUM_FX_BOLT || fx == SUM_FX_WIND) return CL_CAST_LONG;
		if (fx == SUM_FX_ICE || fx == SUM_FX_HOLY) return CL_CAST_RAISE;
		if (fx == SUM_FX_DARK || fx == SUM_FX_MIASMA) return CL_TAUNT;
		if (fx == SUM_FX_SPIKE || fx == SUM_FX_WEB) return CL_SPAWN;
		return CL_CAST_SHOOT;
	}
	if (IsCraftId(skill)) {
		if (IsCraftSuper(skill)) return variant ? CL_CAST_LONG : CL_TAUNT;
		int line = (skill - CRAFT_BASE) / CRAFT_PER_LINE;
		if (line == 0 || line == 3) return CL_CAST_RAISE;
		if (line == 1 || line == 4) return CL_INTERACT;
		if (line == 2 || line == 5) return CL_USE;
		return variant ? CL_CAST_SHOOT : CL_CAST_LONG;
	}
	const SkillDef* sd = skill >= 0 ? &SkillInfo(skill) : NULL;
	if (sd && IsFormSkill(skill)) {
		switch (FormOf(skill)) {
		case FM_CIRCLE: return CL_CAST_LONG;
		case FM_PAIR: return CL_CAST_RAISE;
		case FM_CROSS: case FM_WALL: case FM_SQUARE: return CL_2H_SPIN;
		case FM_BACK: case FM_CHAIN: return CL_1H_STAB;
		case FM_TRI: return CL_2H_CHOP;
		case FM_DIA: return CL_1H_CHOP;
		case FM_ECHELON: return CL_1H_SHOOT;
		case FM_PINCER: return CL_2H_CHOP;
		case FM_WING: return CL_2H_SPIN;
		default: return CL_1H_SLICE;
		}
	}
	if (sd && sd->kind != SK_PHYS) return CastClipFor(sd->kind, skill);
	if (u.monster >= 0) {
		int st = UnitMotionStyle(u, skill);
		if (st != AS_SHOOT && st != AS_SHOOT1 && st != AS_CAST) {
			int pat = (u.monster * 3 + variant + (skill >= 0 ? skill : 0)) % 6;
			switch (pat) {
			case 0: return CL_PUNCH;
			case 1: return CL_1H_STAB;
			case 2: return CL_2H_CHOP;
			case 3: return CL_2H_SPIN;
			case 4: return CL_TAUNT;
			default: return CL_1H_SLICE;
			}
		}
	}
	return UnitAttackClip(u, variant, skill);
}

static void KillCutParts(Fix<Particle, 4096>& parts)
{
	for (int i = 0; i < parts.size();) {
		const Particle& p = parts[i];
		if (p.wxHit == 0 && p.p.x * p.p.x + p.p.z * p.p.z < 64.f && p.p.y > -2.f && p.p.y < 10.f) {
			parts[i] = parts.back();
			parts.pop_back();
			continue;
		}
		i++;
	}
}

void Game::UpdateSeq(float dt)
{
	{
		float mul = m_seqCut ? TacCutTime(m_save) : TacPaceTime(m_save);
		if (mul < 0.05f) mul = 0.05f;
		dt /= mul;
	}
	if (m_seqCut) {
		m_cutStageT += dt;
		if (m_cutStage == 0) {
			if (m_cutStageT >= .35f) {
				m_cutStage = 1; m_cutStageT = 0;
				m_lunge[m_cutA] = 0;
				if (m_cutD >= 0) m_lunge[m_cutD] = 0;
				{
					int sk0 = (m_seqN > 0) ? m_seq[0].skill : -1;
					int clip = CutClipFor(m_b.u[m_cutA], sk0, sk0 >= 0 ? ((sk0 * 3 + m_cutPat) & 1) : 0);
					Actor& ca = m_act[m_cutA];
					ActorPlay(ca, clip, 0, .06f);
					ca.skillPose = sk0;
					float full = ActorClipLen(ca, m_lib, m_r, clip);
					float need = CutIntroLen();
					float wind = full * ActorWindup(ca, m_r, clip);
					ca.animSpeed = Clampf(wind / max(0.25f, need), 0.12f, 1.15f);
				}
				if (m_cutD >= 0 && !PoseKeep(m_act[m_cutD], m_b.u[m_cutD], m_dieT[m_cutD], m_dispHp[m_cutD])) ActorPlay(m_act[m_cutD], CL_IDLE, 1, .15f);
				PoseFormAllies();
				if (m_cutKind == 3) BindSummonCut();
				if (m_seqSkill >= 0) {
					const SkillDef& sd = SkillInfo(m_seqSkill);
					Burst(m_cutPosA + v3(0, .1f, 0), 30, 2.f, .14f, 1.2f, (ElemColor(sd.element) & 255) / 255.f, ((ElemColor(sd.element) >> 8) & 255) / 255.f, ((ElemColor(sd.element) >> 16) & 255) / 255.f, 2, 1, -2.5f, 0, 1.5f);
					Burst(m_cutPosA + v3(0, .05f, 0), 2, 0, .8f, 1.f, 1.f, 1.f, 1.f, 1, 1, 0, 0, 0, 1.6f);
				}
				{
					// 技名の間は詠唱か技の閃きだけ。振りの音は攻防で振り下ろすときに鳴らす。
					int sk = m_seqSkill;
					int style = UnitMotionStyle(m_b.u[m_cutA], sk);
					const SkillDef* sd = sk >= 0 ? &SkillInfo(sk) : NULL;
					if (sd && sd->kind != SK_PHYS) TacSfxMotion(sk, sd->kind, sd->element, style);
					else if (sk >= 0) TacSfx(TS_CRAFT_SW);
				}
			}
			return;
		}
		if (m_cutStage == 1) {
			float need = CutIntroLen();
			CutDressFx();
			if (m_cutKind == 1 || (m_ult && m_seqSkill >= 0)) {
				const SkillDef& sd = SkillInfo(m_seqSkill);
				if (((int)(m_cutStageT * 8.f) & 1) == 0)
					Burst(m_cutPosA + v3(0, 1.1f, 0), 4, 3.f, .2f, .4f, (ElemColor(sd.element) & 255) / 255.f, ((ElemColor(sd.element) >> 8) & 255) / 255.f, ((ElemColor(sd.element) >> 16) & 255) / 255.f, 3, 1, -1.f, 0, .6f);
			}
			if (m_cutKind == 3 && m_sumOn) SummonArrive(dt);
			if (m_cutStageT >= need) {
				m_cutStage = 2; m_cutStageT = 0; m_seqT = 0; m_seqCur = 0; m_seqImpact = 0;
				Shake(m_cutKind == 1 ? .7f : .45f);
				if (m_cutKind == 1) m_flashWhite = IsCraftSuper(m_seqSkill) ? 1.f : .55f;
				Burst(m_cutPosA + v3(0, .05f, 0), m_cutKind == 1 ? 36 : 22, 3.4f, .2f, .9f, 1.f, .92f, .7f, 2, 1, .4f, 0, 1.4f);
				if (m_cutKind == 1) Burst(m_cutPosA + v3(0, .2f, 0), 3, 0, 1.4f, .7f, 1.f, .85f, .4f, 1, 1, 0, 0, 0, 2.2f);
				if (m_cutKind == 2) {
					for (int i = 0; i < m_cutAllyN; i++) Burst(m_cutAllyPos[i] + v3(0, .08f, 0), 8, 1.6f, .12f, .6f, .5f, .8f, 1.f, 2, 1, .3f, 0, .8f);
				}
				if (m_cutD >= 0) {
					DWORD ec = m_seqSkill >= 0 ? ElemColor(SkillInfo(m_seqSkill).element) : RGBA8(255, 230, 180);
					Burst(m_cutPosD + v3(0, .8f, 0), 14, 2.2f, .12f, .55f, (ec & 255) / 255.f, ((ec >> 8) & 255) / 255.f, ((ec >> 16) & 255) / 255.f, 3, 1, 1.2f, 0, .5f);
				}
			}
			return;
		}
		if (m_cutStage == 3) {
			if (m_cutKind == 3 && m_sumOn) SummonLeave(dt);
			// 召喚は攻撃のあと、種類ごとの退場が見えるまで結果を出さない。
			if (m_cutStageT >= ((m_cutKind == 3 && m_sumOn) ? SummonDepartNeed() : 0.3f)) {
				m_cutStage = 4; m_cutStageT = 0;
				KillCutParts(m_parts);
				m_popups.clear();
				for (auto& p : m_deferPopups) m_popups.push_back(p);
				m_deferPopups.clear();
				for (int i = 0; i < m_b.nUnits; i++) m_lunge[i] = 0;
				for (int i = 0; i < m_b.nUnits; i++) {
					m_act[i].animSpeed = 1.f;
					if (PoseKeep(m_act[i], m_b.u[i], m_dieT[i], m_dispHp[i])) continue;
					ActorPlay(m_act[i], CL_IDLE, 1, .1f);
				}
			}
			return;
		}
		if (m_cutStage == 4) {
			if (m_cutStageT >= .3f) { m_seqCut = FALSE; m_cutKind = 0; m_cutStage = 0; m_sumOn = 0; FinishSeq(); }
			return;
		}
	}
	if (m_seqCut && m_cutStage == 2 && m_cutKind == 3 && m_sumOn) {
		if (!m_sumHit) SummonStrike(dt);
		else SummonLeave(dt);
	}
	// 攻防の再生（マップ・カットイン共通）
	if (m_seqCut && m_cutStage == 2) CutDressFx();
	if (m_seqCur >= m_seqN) {
		m_seqT += dt;
		if (m_seqT >= .35f) {
			// 浴びせた瞬間から退場を進めている。他の的のダメージと並行なので、ここで退場を巻き戻さない。
			if (m_seqCut && m_cutKind == 3 && m_sumOn) {
				if (!m_sumHit || m_cutStageT < SummonStrikeNeed()) return;
				m_cutStage = 4; m_cutStageT = 0;
				KillCutParts(m_parts);
				m_popups.clear();
				for (auto& p : m_deferPopups) m_popups.push_back(p);
				m_deferPopups.clear();
				for (int i = 0; i < m_b.nUnits; i++) m_lunge[i] = 0;
				for (int i = 0; i < m_b.nUnits; i++) {
					m_act[i].animSpeed = 1.f;
					if (PoseKeep(m_act[i], m_b.u[i], m_dieT[i], m_dispHp[i])) continue;
					ActorPlay(m_act[i], CL_IDLE, 1, .1f);
				}
				return;
			}
			if (m_seqCut) { m_cutStage = 3; m_cutStageT = 0; }
			else FinishSeq();
		}
		return;
	}
	const SeqStep& st = m_seq[m_seqCur];
	int A = st.attacker;
	int D = st.nHits > 0 ? st.hits[0].defender : st.defender;
	Actor& aa = m_act[A];
	const Unit& au = m_b.u[A];
	const SkillDef* sd = st.skill >= 0 ? &SkillInfo(st.skill) : NULL;
	BOOL cast = sd && sd->kind != SK_PHYS;
	int style = UnitMotionStyle(au, st.skill);
	BOOL ranged = !cast && (style == AS_SHOOT || style == AS_SHOOT1 || (Dist(au.x, au.z, m_b.u[D].x, m_b.u[D].z) > 1 && style != AS_CAST));
	BOOL magicNormal = !sd && style == AS_CAST;
	int minV = 0, maxV = 0;
	BOOL seenV = FALSE;
	for (int k = 0; k < st.nHits; k++) {
		int v = st.hits[k].volley;
		if (!seenV || v < minV) minV = v;
		if (!seenV || v > maxV) maxV = v;
		seenV = TRUE;
	}
	int span = maxV - minV;
	BOOL follow = seenV && minV > 0 && span == 0 && !st.counter;
	if (m_seqT == 0) {
		int clip = CutClipFor(au, st.skill, sd ? ((st.skill + m_cutPat) & 1) : (m_seqCur & 1));
		if (sd && IsFormSkill(st.skill)) {
			if (FormOf(st.skill) == FM_CIRCLE) clip = CL_CAST_LONG;
			else if (FormOf(st.skill) == FM_PAIR) clip = CL_CAST_RAISE;
			int mem[8];
			int ax = au.x, az = au.z;
			if (D != A) { ax = m_b.u[D].x; az = m_b.u[D].z; }
			int nm = FormMembers(m_b, A, st.skill, ax, az, mem, 8);
			int allyClip = CL_1H_SLICE;
			switch (FormOf(st.skill)) {
			case FM_CROSS: case FM_WALL: case FM_SQUARE: allyClip = CL_2H_SPIN; break;
			case FM_BACK: case FM_CHAIN: allyClip = CL_1H_STAB; break;
			case FM_TRI: allyClip = CL_2H_CHOP; break;
			case FM_DIA: allyClip = CL_1H_CHOP; break;
			case FM_ECHELON: allyClip = CL_1H_SHOOT; break;
			case FM_PINCER: allyClip = CL_2H_CHOP; break;
			case FM_WING: allyClip = CL_2H_SPIN; break;
			case FM_CIRCLE: allyClip = CL_CAST_LONG; break;
			case FM_PAIR: allyClip = CL_CAST_RAISE; break;
			default: break;
			}
			for (int i = 0; i < nm; i++) {
				if (mem[i] == A || PoseKeep(m_act[mem[i]], m_b.u[mem[i]], m_dieT[mem[i]], m_dispHp[mem[i]])) continue;
				Actor& ma = m_act[mem[i]];
				BOOL posed = m_seqCut && m_cutStage == 2 && ma.clip == allyClip && ma.t > 0.05f;
				if (posed) ma.t = ActorClipLen(ma, m_lib, m_r, allyClip) * ActorWindup(ma, m_r, allyClip);
				else ActorPlay(ma, allyClip, 0, .1f);
				ma.animSpeed = 1.05f;
				ma.skillPose = st.skill;
				m_b.u[mem[i]].flash = 1.f;
			}
		}
		BOOL held = m_seqCut && m_cutStage == 2 && aa.clip == clip && aa.t > 0.05f && !follow;
		if (!held && !PoseGround(aa, au, m_dieT[A], m_dispHp[A])) ActorPlay(aa, clip, 0, follow ? .04f : .08f);
		aa.skillPose = st.skill;
		aa.animSpeed = cast ? 1.0f : 1.05f;
		m_stepL = ActorClipLen(aa, m_lib, m_r, clip) / aa.animSpeed;
		m_stepTi = m_stepL * (cast || magicNormal ? .5f : .42f);
		if (ranged || cast || magicNormal) {
			m_proj.active = 1;
			m_proj.t = 0;
			m_proj.launch = m_stepTi * .75f;
			m_proj.dur = .28f;
			m_proj.el = sd ? sd->element : (magicNormal ? EL_HOLY : EL_NONE);
			m_proj.kind = sd ? sd->kind : (magicNormal ? SK_MAGIC : SK_PHYS);
			m_proj.arrow = !cast && !magicNormal;
			m_proj.skill = st.skill;
			m_proj.style = m_proj.arrow ? 1 : 0;
			if (m_proj.arrow && style == AS_SHOOT1) {
				int wl = au.monster >= 0 ? MonsterInfo(au.monster).weaponLook : 0;
				m_proj.style = (wl == 3) ? 6 : 5;
			}
			if (!m_proj.arrow) {
				int el = m_proj.el;
				if (el == EL_THUNDER || st.skill == SKL_THUNDER || st.skill == SKL_SMITE) m_proj.style = 2;
				else if (st.skill == SKL_METEOR || st.skill == SKL_APOC || el == EL_EARTH) m_proj.style = 3;
				else if (el == EL_FIRE) m_proj.style = 4;
				else if (el == EL_ICE) m_proj.style = 3;
				else if (el == EL_WIND) m_proj.style = 1;
				else if (el == EL_HOLY) m_proj.style = 2;
				else if (el == EL_DARK || el == EL_POISON) m_proj.style = 4;
			}
			if (m_proj.style == 2) m_proj.dur = .22f;
			else if (m_proj.style == 3) m_proj.dur = .42f;
			m_proj.self = (D == A) || (sd && sd->rMax == 0);
			m_stepTi = m_proj.launch + (m_proj.self ? .15f : m_proj.dur);
		} else m_proj.active = 0;
		m_seqImpact = 0;
		if (cast) {
			DWORD c = ElemColor(sd->element);
			Burst(ActorPos(A) + v3(0, .05f, 0), 1, 0, .5f, .9f, (c & 255) / 255.f, ((c >> 8) & 255) / 255.f, ((c >> 16) & 255) / 255.f, 1, 1, 0, 0, 0, 1.3f);
		}
		if (held || follow) {
			float wind = ActorWindup(aa, m_r, clip) * (follow ? 0.55f : 1.f);
			aa.t = ActorClipLen(aa, m_lib, m_r, clip) * wind;
			m_seqT = aa.t / max(0.05f, aa.animSpeed);
			if (follow && m_proj.active) m_proj.t = m_seqT;
		}
		if (!(held && cast && m_seqCur == 0)) {
			if (!cast && IsCraftId(st.skill)) TacSfxMotion(-1, SK_PHYS, EL_NONE, style);
			else TacSfxMotion(st.skill, sd ? sd->kind : SK_PHYS, sd ? sd->element : EL_NONE, style);
		}
	}
	m_seqT += dt;
	// 召喚は息や一撃が乗るまで着弾しない。浴びせた瞬間にダメージを出す。
	if (m_seqCut && m_cutKind == 3 && m_sumOn && m_cutStage == 2 && m_stepTi > 0.05f) {
		if (!m_sumHit) {
			float holdAt = m_stepTi - 0.04f;
			if (holdAt < 0.02f) holdAt = 0.02f;
			if (m_seqT > holdAt) m_seqT = holdAt;
		} else if (m_seqCur == 0 && m_seqImpact == 0 && m_seqT < m_stepTi) {
			m_seqT = m_stepTi;
		}
	}
	BOOL melee = !ranged && !cast && !magicNormal;
	float maxL = .32f;
	if (m_seqCut && m_cutStage == 2) {
		int sa = 0, ia = 0, sdf = 0, idf = 0;
		maxL = 0.f;
		if (A != D && CutFind(A, &sa, &ia) && CutFind(D, &sdf, &idf) && sa != sdf)
			maxL = Clampf(fabsf(CutStand(sa, ia).x - CutStand(sdf, idf).x) - .8f, 0.f, 4.f);
	}
	if (melee && A != D) {
		float u0 = m_stepTi * .35f, u1 = m_stepTi;
		float l = 0;
		if (m_seqT < u0) l = 0;
		else if (m_seqT < u1) l = Smooth01((m_seqT - u0) / max(.01f, u1 - u0));
		else l = 1.f - Smooth01((m_seqT - u1 - .15f) / .35f);
		m_lunge[A] = maxL * Clampf(l, 0, 1);
		if (m_cutKind == 2 && m_seqCut && m_cutStage == 2) {
			for (int i = 0; i < m_cutAllyN; i++) {
				int id = m_cutAlly[i];
				if (id != A && !PoseKeep(m_act[id], m_b.u[id], m_dieT[id], m_dispHp[id])) m_lunge[id] = m_lunge[A] * .8f;
				else if (id != A) m_lunge[id] = 0;
			}
		}
	}
	if (m_proj.active) {
		m_proj.t += dt;
		if (m_proj.t >= m_proj.launch && !m_proj.self) {
			float u = Clampf((m_proj.t - m_proj.launch) / m_proj.dur, 0, 1);
			V3 a = ActorPos(A) + v3(0, m_act[A].height * .6f, 0);
			V3 b = ActorPos(D) + v3(0, m_act[D].height * .5f, 0);
			V3 p;
			if (m_proj.style == 2) p = Lerp(b + v3(0, 6.f, 0), b, u);
			else if (m_proj.style == 3) p = Lerp(b + v3(m_rng.F(-.4f, .4f), 5.f, m_rng.F(-.4f, .4f)), b, u * u);
			else {
				p = Lerp(a, b, u);
				p.y += sinf(u * PI) * (m_proj.style == 5 ? 1.15f : (m_proj.style == 4 ? .7f : (m_proj.style == 6 ? .2f : (m_proj.arrow ? .25f : .35f))));
			}
			if (u < 1.f) {
				DWORD c = m_proj.arrow ? RGBA8(255, 250, 230) : ElemColor(m_proj.el);
				float r = (c & 255) / 255.f, g = ((c >> 8) & 255) / 255.f, bb = ((c >> 16) & 255) / 255.f;
				Particle q = {};
				q.p = p; q.a = 1; q.add = 1; q.r = r; q.g = g; q.b = bb;
				if (m_proj.style == 2) {
					q.v = v3(m_rng.F(-.4f, .4f), -8.f, m_rng.F(-.4f, .4f));
					q.life = q.max = .12f; q.size = .2f; q.size1 = .06f; q.shape = 6; q.stretch = .03f;
					if (m_proj.el == EL_THUNDER || m_proj.skill == SKL_THUNDER || m_proj.skill == SKL_SMITE) { q.r = .8f; q.g = .9f; q.b = 1.f; }
				} else if (m_proj.style == 4) {
					q.v = v3(0, 1.2f, 0);
					q.life = q.max = .28f; q.size = .22f; q.size1 = .05f; q.shape = 5; q.grav = -0.3f;
				} else if (m_proj.style == 3) {
					q.v = v3(0, -2.f, 0);
					q.life = q.max = .2f; q.size = .18f; q.size1 = .05f; q.shape = (m_proj.el == EL_ICE) ? 7 : 5;
				} else if (m_proj.style == 6) {
					q.v = Norm(b - a) * 16.f; q.life = q.max = .08f; q.size = .08f; q.size1 = .05f; q.shape = 3; q.stretch = .05f;
					q.r = .62f; q.g = .38f; q.b = .16f;
				} else if (m_proj.style == 5) {
					q.v = v3(0, 2.2f, 0); q.life = q.max = .22f; q.size = .18f; q.size1 = .1f; q.shape = 0; q.grav = 3.f;
					q.r = .55f; q.g = .48f; q.b = .36f;
				} else if (m_proj.arrow || m_proj.style == 1) {
					q.v = Norm(b - a) * 14.f; q.life = q.max = .07f; q.size = .05f; q.size1 = .04f; q.shape = 3; q.stretch = .02f;
				} else {
					q.v = v3(0, .4f, 0); q.life = q.max = .16f; q.size = .16f; q.size1 = .05f; q.shape = 0;
				}
				m_parts.push_back(q);
			}
		}
	}
	const float kGap = 0.42f;
	if (m_seqT >= m_stepTi) {
		int due = min(span, (int)((m_seqT - m_stepTi) / kGap));
		while (m_seqImpact <= due) {
			int v = minV + m_seqImpact;
			BOOL stop = FALSE;
			for (int k = 0; k < st.nHits; k++)
				if (st.hits[k].volley < v && (st.hits[k].kill || st.hits[k].overkill)) stop = TRUE;
			if (stop) break;
			SeqImpact(st, v);
			m_seqImpact++;
		}
	}
	BOOL kill = FALSE;
	for (int k = 0; k < st.nHits; k++) if (st.hits[k].kill) kill = TRUE;
	float endT = follow
		? (m_stepTi + 0.46f + (kill ? .55f : 0.f))
		: (max(m_stepL * .9f, m_stepTi + .5f + span * kGap) + (kill ? .55f : .1f));
	if (m_seqT >= endT) {
		aa.animSpeed = 1.f;
		BOOL chain = FALSE;
		if (m_seqCur + 1 < m_seqN) {
			const SeqStep& nx = m_seq[m_seqCur + 1];
			chain = nx.attacker == A && nx.skill == st.skill && !nx.counter && nx.nHits > 0 && nx.hits[0].volley > 0;
		}
		if (!chain && !PoseKeep(aa, m_b.u[A], m_dieT[A], m_dispHp[A])) ActorPlay(aa, CL_IDLE, 1, .15f);
		m_lunge[A] = 0;
		BOOL ended = FALSE;
		for (int k = 0; k < st.nHits; k++) if (st.hits[k].kill || st.hits[k].overkill) ended = TRUE;
		if (ended) {
			while (m_seqCur + 1 < m_seqN) {
				const SeqStep& nx = m_seq[m_seqCur + 1];
				BOOL extra = nx.attacker == A && nx.skill == st.skill && !nx.counter && nx.nHits > 0 && nx.hits[0].volley > 0;
				if (!extra) break;
				m_seqCur++;
			}
		}
		m_seqCur++;
		m_seqT = 0;
		m_seqImpact = 0;
		m_proj.active = 0;
		if (m_seqCur >= m_seqN) m_seqT = 0;
	}
}

static int PlayerCoop(const Battle& b, const Strike* s, int n, int defender)
{
	int who[8], nw = 0;
	for (int k = 0; k < n; k++) {
		if (s[k].defender != defender || !s[k].hit) continue;
		int a = s[k].attacker;
		if (a < 0 || a >= b.nUnits || b.u[a].team != TEAM_PLAYER) continue;
		BOOL have = FALSE;
		for (int i = 0; i < nw; i++) if (who[i] == a) have = TRUE;
		if (!have && nw < 8) who[nw++] = a;
	}
	return nw;
}

void Game::GrantFoeLoot(int ui, int coop)
{
	if (ui < 0 || ui >= m_b.nUnits) return;
	Unit& u = m_b.u[ui];
	if (u.team != TEAM_ENEMY) return;
	FoeNoteKill(u.monster);
	int pay = 5 + u.ch.lv / 2 + (u.boss ? 28 : 0);
	m_save.gold += pay;
	wchar_t coin[32];
	swprintf_s(coin, L"+%s G", TacNum(pay));
	AddPopup(v3(u.px, u.py + .8f, u.pz), coin, RGBA8(255, 220, 90), 0, MSG_RISE, 0, 3.6f);
	AddLog(coin, RGBA8(255, 220, 90));
	int rolls = m_rng.Chance(coop >= 2 ? 80 : 40) ? 2 : 1;
	int chance = u.boss ? 70 : 28;
	if (coop >= 2) chance += 34;
	if (chance > 92) chance = 92;
	for (int r = 0; r < rolls; r++) {
		if (!m_rng.Chance(chance)) continue;
		int beforeN = m_save.gearN;
		int gid = GearTryDrop(m_save, u.ch.lv, u.monster, m_rng);
		if (!m_demo && m_save.gearN >= GEAR_STASH && (gid > 0 || beforeN >= GEAR_STASH)) m_stashFull = 1;
		if (gid > 0) {
			AddPopup(v3(u.px, u.py + 1.3f + r * .35f, u.pz), GearName(gid), RGBA8(180, 255, 190), 1, MSG_RISE, .12f * r, 3.6f);
			AddLog(GearName(gid), RGBA8(180, 255, 190));
		}
	}
	int goods = (m_rng.Chance(u.boss ? 78 : (coop >= 2 ? 62 : 40)) ? 1 : 0) + ((coop >= 2 && m_rng.Chance(50)) ? 1 : 0);
	for (int g = 0; g < goods; g++) {
		wchar_t cn[96];
		if (!DropConsumable(m_save, u.monster, m_rng, cn, 96)) continue;
		AddPopup(v3(u.px, u.py + 1.55f + g * .32f, u.pz), cn, RGBA8(255, 210, 140), 1, MSG_RISE, .15f, 3.6f);
		AddLog(cn, RGBA8(255, 210, 140));
	}
	if (m_rng.Chance(coop >= 2 ? 45 : 22)) {
		int it = m_rng.Chance(50) ? IT_ORE : IT_HERB;
		int cashed = GainItemN(m_save, it, 1);
		wchar_t shown[64];
		if (cashed > 0) swprintf_s(shown, L"+%dG", cashed);
		else wcsncpy_s(shown, TT(it == IT_ORE ? TX_IT_ORE : TX_IT_HERB), _TRUNCATE);
		AddPopup(v3(u.px, u.py + 1.9f, u.pz), shown, RGBA8(180, 220, 160), 1, MSG_RISE, .2f, 3.6f);
		AddLog(shown, RGBA8(180, 220, 160));
	}
	int trophyN = (u.boss || coop >= 2 || m_rng.Chance(45)) ? 2 : 1;
	for (int di = 0; di < trophyN; di++) {
		wchar_t part[80];
		int sold = 0;
		if (!DropTrophy(u.monster, u.boss, m_rng, part, 80, &sold)) continue;
		wchar_t line[96];
		if (sold > 0) swprintf_s(line, L"%s +%sG", part, TacNum(sold));
		else swprintf_s(line, L"%s", part);
		AddPopup(v3(u.px, u.py + 2.2f + di * .35f, u.pz), line, RGBA8(255, 186, 96), 1, MSG_RISE, .1f * di, 3.6f);
		AddLog(line, RGBA8(255, 186, 96));
	}
	wchar_t loot[80];
	if (DropCraftLoot(m_save, u.monster, u.boss, m_battleStage, m_rng, loot, 80)) {
		AddPopup(v3(u.px, u.py + 2.9f, u.pz), loot, RGBA8(190, 170, 255), 1, MSG_RISE, .2f, 3.6f);
		AddLog(loot, RGBA8(190, 170, 255));
	}
	int extraRolls = u.boss ? 2 : 1;
	for (int ex = 0; ex < extraRolls; ex++) {
		int chance = u.boss ? (ex == 0 ? 80 : 45) : 28;
		if (!m_rng.Chance(chance)) continue;
		wchar_t extra[96];
		if (!DropExtraLoot(m_save, m_battleStage, u.boss, m_rng, extra, 96)) continue;
		AddPopup(v3(u.px, u.py + 3.25f + ex * .32f, u.pz), extra, RGBA8(255, 196, 140), 1, MSG_RISE, .18f + ex * .08f, 3.6f);
		AddLog(extra, RGBA8(255, 196, 140));
	}
}

void Game::FinishSeq()
{
	m_sumOn = 0;
	int lootSfx = 0;
	for (int i = 0; i < m_b.nUnits; i++) {
		Unit& u = m_b.u[i];
		if (u.alive && u.hp > 0 && m_dieT[i] >= 0) {
			m_dieT[i] = -1;
			u.dissolve = 0;
			if (m_act[i].clip != CL_SPAWN) ActorPlay(m_act[i], CL_SPAWN, 0, .35f);
		}
		if (u.alive && u.hp <= 0) {
			u.alive = 0;
			u.killed = 1;
			if (m_dieT[i] < 0) { m_dieT[i] = 0; ActorPlay(m_act[i], CL_DEATH, 0, .1f); }
			if (u.team == TEAM_ENEMY && u.boss) {
				int otherBoss = 0;
				for (int j = 0; j < m_b.nUnits; j++) {
					if (j == i) continue;
					const Unit& o = m_b.u[j];
					if (o.alive && o.hp > 0 && o.team == TEAM_ENEMY && o.boss) otherBoss = 1;
				}
				if (!otherBoss) m_bossDown = 1;
			}
			if (u.team == TEAM_ENEMY) {
				lootSfx = 1;
				GrantFoeLoot(i, PlayerCoop(m_b, m_strk, m_strkN, i));
			}
			for (int k = 0; k < m_strkN; k++) {
				if (m_strk[k].defender == i && m_strk[k].kill) {
					Unit& killer = m_b.u[m_strk[k].attacker];
					if (killer.team == TEAM_PLAYER) killer.ch.kills++;
				}
			}
			wchar_t nm[64], s[96];
			UnitTitle(u, nm, 64);
			swprintf_s(s, L"%s  %s", nm, TT(u.team == TEAM_PLAYER ? TX_RS_KO : TX_BT_DEFEATED));
			AddBanner(s, NULL, u.team == TEAM_PLAYER ? RGBA8(255, 150, 140) : RGBA8(255, 226, 150), 2, 1.6f, u.team == TEAM_PLAYER ? MSG_DROP : MSG_BLUR);
		}
		m_dispHp[i] = (float)u.hp;
	}
	if (lootSfx) TacSfx(TS_GET);
	m_b.RebuildOcc();
	AwardExp();
	// デモは経験値のカードも転職の確認も出さない。数値は既に入っているので、手番だけ閉じる。
	if (m_demo) { m_lvqN = 0; m_lvqCur = 0; m_promoAsk = 0; FinishUnitTurn(1); }
	else if (m_lvqN > 0) { m_phase = PH_LEVELUP; m_lvqCur = 0; m_lvT = 0; m_navMode = TRUE; m_focusId = ID_BT_LVOK; }
	else FinishUnitTurn(1);
}

void Game::AwardExp()
{
	m_lvqN = 0;
	m_promoAsk = 0;
	long long gain[MAX_UNITS] = {};
	int coopHit[MAX_UNITS] = {};
	unsigned char luckyHit[MAX_UNITS] = {};
	int aoeN[MAX_UNITS] = {};
	unsigned char aoeSeen[MAX_PARTY][(MAX_UNITS + 7) / 8] = {};
	int aoeMap[MAX_UNITS];
	int aoePn = 0;
	for (int i = 0; i < MAX_UNITS; i++) aoeMap[i] = -1;
	int topLv = 1;
	for (int i = 0; i < m_b.nUnits; i++) {
		if (m_b.u[i].team == TEAM_PLAYER && m_b.u[i].ch.lv > topLv)
			topLv = m_b.u[i].ch.lv;
	}
	for (int k = 0; k < m_strkN; k++) {
		const Strike& s = m_strk[k];
		const Unit& a = m_b.u[s.attacker];
		if (a.team != TEAM_PLAYER) continue;
		if (s.attacker >= 0 && s.attacker < MAX_UNITS && s.defender >= 0 && s.defender < MAX_UNITS) {
			int pi = aoeMap[s.attacker];
			if (pi < 0 && aoePn < MAX_PARTY) {
				pi = aoePn++;
				aoeMap[s.attacker] = pi;
			}
			if (pi >= 0) {
				int b = s.defender >> 3;
				unsigned char m = (unsigned char)(1u << (s.defender & 7));
				if (!(aoeSeen[pi][b] & m)) {
					aoeSeen[pi][b] |= m;
					aoeN[s.attacker]++;
				}
			}
		}
		const Unit& d = m_b.u[s.defender];
		long long e;
		if (s.skill >= 0 && SkillInfo(s.skill).kind == SK_HEAL) {
			int healed = -s.dmg;
			if (healed > 0) {
				e = ExpForAction(a, d, healed, m_battleStage);
				e = e * 3 / 2;
				if (e < 21) e = 21;
			} else e = 12;
		} else if (s.skill >= 0 && SkillInfo(s.skill).kind == SK_BUFF) e = 16 + m_battleStage / 2;
		else if (!s.hit) e = 1;
		else {
			e = ExpForAction(a, d, s.dmg, m_battleStage);
			if (s.overkill && e > 1) e *= 2;
			e /= 2;
			if (e < 1) e = 1;
			// ☆ラッキーはここで明示的に3倍（表示の「☆×3」と一致させる）。
			if (UnitIsLucky(d)) {
				e *= 3;
				luckyHit[s.attacker] = 1;
			}
		}
		if (s.hit && e > 1 && d.team == TEAM_ENEMY) {
			int coop = PlayerCoop(m_b, m_strk, m_strkN, s.defender);
			if (coop >= 2) {
				long long extra = e * (50LL * (coop - 1)) / 100;
				if (extra < 1) extra = 1;
				e += extra;
				coopHit[s.attacker] = 1;
			}
		}
		gain[s.attacker] += e;
	}
	for (int i = 0; i < m_b.nUnits; i++) {
		Unit& u = m_b.u[i];
		if (u.team != TEAM_PLAYER || !gain[i]) continue;
		long long g = ExpShareAoe(gain[i], aoeN[i]);
		if (UnitHasPas(u, PAS_EXP2)) g = g * 140 / 100;
		else if (UnitHasPas(u, PAS_EXP)) g = g * 120 / 100;
		if (g < 1) g = 1;
		/* 出撃中の最高レベルはそのまま。低いほど多く、10差で2倍、30差以上は4倍。 */
		{
			int gap = topLv - u.ch.lv;
			if (gap > 0) {
				int pct = 100 + gap * 10;
				if (pct > 400) pct = 400;
				long long boosted = g * (long long)pct / 100;
				if (boosted > g) g = boosted;
			}
		}
		{
			long long acc = (long long)m_expGain[i] + g;
			if (acc > 2147483647LL) acc = 2147483647LL;
			m_expGain[i] = (int)acc;
		}
		int fromLv = u.ch.lv;
		long long pool = (long long)u.ch.exp + g;
		int q0 = m_lvqN;
		BurstToGate(u, i, m_rng, pool, m_lvq, m_lvqN, MAX_PARTY * 4);
		for (int q = q0; q < m_lvqN; q++) {
			for (int lk = 0; lk < m_lvq[q].nLearned; lk++) {
				wchar_t nm[40], lvline[120];
				UnitTitle(u, nm, 40);
				swprintf_s(lvline, L"%s  %s: %s", nm, TT(TX_BT_LEARNED), SkillLabel(m_lvq[q].learned[lk]));
				AddLog(lvline, RGBA8(255, 226, 150));
			}
		}
		if (u.ch.lv >= TAC_LV_MAX) pool = 0;
		if (pool < 0) pool = 0;
		if (pool > 2000000000LL) pool = 2000000000LL;
		u.ch.exp = (int)pool;
		if (u.ch.lv != fromLv) {
			wchar_t nm[40], lvline[120];
			UnitTitle(u, nm, 40);
			swprintf_s(lvline, L"%s  %s  %s%d → %s%d", nm, TT(TX_BT_LEVELUP), TT(TX_MISC_LV), fromLv, TT(TX_MISC_LV), u.ch.lv);
			AddLog(lvline, RGBA8(140, 255, 150));
		}
		{
			wchar_t s[160];
			int need = ExpToNext(u.ch.lv);
			int left = u.ch.lv >= TAC_LV_MAX ? 0 : (int)min(2000000000LL, max(0LL, (long long)need - pool));
			long long show = g > 2000000000LL ? 2000000000LL : g;
			if (luckyHit[i])
				swprintf_s(s, L"%s +%s  ☆×3  %s %s", TT(TX_MISC_EXP), TacNum((int)show), TT(TX_EXP_LEFT), TacNum(left));
			else if (coopHit[i])
				swprintf_s(s, L"%s +%s %s  %s %s", TT(TX_MISC_EXP), TacNum((int)show), TT(TX_BT_COOP), TT(TX_EXP_LEFT), TacNum(left));
			else
				swprintf_s(s, L"%s +%s  %s %s", TT(TX_MISC_EXP), TacNum((int)show), TT(TX_EXP_LEFT), TacNum(left));
			AddPopup(v3(u.px, u.py + m_act[i].height + .45f, u.pz), s, RGBA8(150, 210, 255), 1, MSG_RISE, .3f, 3.6f);
			wchar_t nm[40], line[120];
			UnitTitle(u, nm, 40);
			swprintf_s(line, L"%s  %s", nm, s);
			AddLog(line, RGBA8(150, 210, 255));
		}
		m_dispHp[i] = (float)u.hp;
	}
}

// ============================================================ 毎フレーム
void Game::UpdateUnitsVisual(float dt)
{
	for (int i = 0; i < m_b.nUnits; i++) {
		Unit& u = m_b.u[i];
		Actor& a = m_act[i];
		ActorUpdate(a, dt);
		if (i == 0 && m_sumOn) ActorUpdate(m_sumAct, dt);
		BOOL moving = (m_phase == PH_MOVING || m_phase == PH_E_MOVE) && m_moveUnit == i;
		BOOL loco = a.clip == CL_IDLE || a.clip == CL_WALK || a.clip == CL_RUN || a.clip == CL_2H_IDLE
			|| (a.clip >= CL_IDLE_GLANCE_L && a.clip <= CL_IDLE_CROSS);
		VisAil av = m_visAil[i];
		if (m_phase != PH_ACTION) {
			av.poison = u.poison; av.slow = u.slow; av.defDown = u.defDown;
			av.sleep = u.sleep; av.confuse = u.confuse; av.stun = u.stun; av.freeze = u.freeze;
			av.burn = u.burn; av.bleed = u.bleed; av.blind = u.blind; av.silence = u.silence;
		}
		if (m_ailBlend[i] > 0.f) m_ailBlend[i] = max(0.f, m_ailBlend[i] - dt);
		BOOL bodyUp = u.alive && ((m_phase == PH_ACTION) ? (m_dispHp[i] > 0.5f) : (u.hp > 0));
		if (bodyUp && !moving && a.clip != CL_DEATH && a.clip != CL_SPAWN && a.clip != CL_HIT_A && a.clip != CL_HIT_B) {
			int hold = 0;
			if (av.freeze > 0) hold = 3;
			else if (av.sleep > 0) hold = 2;
			else if (av.stun > 0) hold = 1;
			if (hold == 3) {
				a.poseHold = 3;
				a.animSpeed = 0.f;
				if (a.blinkSave < 0.f) { a.blinkSave = a.blinkSeed; a.blinkSeed = 0.f; }
			} else if (hold && (loco || a.poseHold)) {
				a.poseHold = hold;
				a.animSpeed = hold == 2 ? 0.22f : 0.4f;
				if (a.blinkSave >= 0.f) { a.blinkSeed = a.blinkSave; a.blinkSave = -1.f; }
				int idleClip = (UnitMotionStyle(u, -1) == AS_CHOP2) ? CL_2H_IDLE : CL_IDLE;
				if (a.clip != CL_IDLE && a.clip != CL_2H_IDLE) ActorPlay(a, idleClip, 1, .18f);
			} else if (!hold && (loco || a.poseHold)) {
				int idleClip = (UnitMotionStyle(u, -1) == AS_CHOP2) ? CL_2H_IDLE : CL_IDLE;
				if (a.poseHold) ActorPlay(a, idleClip, 1, .2f);
				a.poseHold = 0;
				if (a.blinkSave >= 0.f) { a.blinkSeed = a.blinkSave; a.blinkSave = -1.f; }
				if (av.slow > 0) a.animSpeed = 0.55f;
				else if (av.confuse > 0) a.animSpeed = 1.35f;
				else a.animSpeed = 1.f;
			}
		}
		if (bodyUp && m_parts.size() < 2600) {
			float h = a.height > 0.35f ? a.height : 1.1f;
			float rad = max(0.42f, h * 0.42f);
			int span = UnitSpan(u);
			if (span > 1) rad *= (float)span * 0.65f;
			// カットシーン中も ActorPos（演壇側）に異常パーティクルを出す。マップ座標だと画面外で消える。
			V3 ap = ActorPos(i);
			auto mote = [&](float y, float ang, float out, int shape, float r, float g, float b, float up, float grav, float life, float sz, int add) {
				if ((int)m_parts.size() >= 2800) return;
				Particle q = {};
				q.p = v3(ap.x + cosf(ang) * rad * out, ap.y + y, ap.z + sinf(ang) * rad * out);
				q.v = v3(cosf(ang) * 0.12f, up, sinf(ang) * 0.12f);
				q.life = q.max = life;
				q.size = sz;
				q.size1 = sz * 0.35f;
				q.r = r; q.g = g; q.b = b; q.a = 1.f;
				q.shape = shape; q.add = add; q.grav = grav; q.drag = 1.1f;
				m_parts.push_back(q);
			};
			float tm = m_time + i * 0.37f;
			float flame = 0.18f + h * 0.16f;
			int extra = (m_ailBlend[i] > 0.f) ? m_ailBlendEl[i] : 0;
			if (av.burn > 0 || extra == EL_FIRE) {
				for (int k = 0; k < 8; k++) {
					float ang = tm * 2.1f + k * 0.785f;
					float y = fmodf(tm * 0.9f + k * 0.13f, 1.f) * h * 0.95f;
					mote(y, ang, 0.9f, 5, 1.f, 0.28f + (k & 1) * 0.35f, 0.04f, 1.35f, -0.55f, 0.42f, flame, 1);
				}
			}
			if (av.poison > 0 || extra == EL_POISON) {
				for (int k = 0; k < 5; k++)
					mote(fmodf(tm * 0.45f + k * 0.2f, 1.f) * h, -tm * 1.1f + k * 1.256f, 0.95f, 0, 0.25f, 0.95f, 0.2f, 0.55f, -0.2f, 0.7f, flame * 0.85f, 1);
			}
			if (av.bleed > 0) {
				for (int k = 0; k < 5; k++)
					mote(h * (0.3f + (k % 3) * 0.2f), k * 1.25f, 0.5f, 0, 0.9f, 0.03f, 0.04f, 0.02f, 2.6f, 0.4f, 0.08f + h * 0.03f, 0);
			}
			if (av.blind > 0) {
				for (int k = 0; k < 4; k++)
					mote(h * 0.84f, tm * 0.6f + k * 1.57f, 0.75f, 0, 0.04f, 0.04f, 0.06f, 0.04f, 0.f, 0.5f, flame, 0);
			}
			if (av.silence > 0 || extra == EL_DARK) {
				for (int k = 0; k < 4; k++)
					mote(h * 0.55f, tm * 1.4f + k * 1.57f, 1.05f, 7, 0.55f, 0.22f, 1.f, 0.12f, 0.f, 0.55f, flame * 0.65f, 1);
			}
			if (av.slow > 0) {
				for (int k = 0; k < 3; k++)
					mote(0.06f, tm * 0.35f + k * 2.1f, 0.85f, 0, 0.42f, 0.3f, 0.1f, 0.06f, 0.5f, 0.6f, flame * 0.75f, 0);
			}
			if (av.confuse > 0 || extra == EL_EARTH) {
				for (int k = 0; k < 4; k++)
					mote(h * 0.92f, tm * 3.f + k * 1.57f, 0.8f, 2, 0.9f, 0.15f, 0.95f, 0.18f, 0.f, 0.4f, flame * 0.5f, 1);
			}
			if (av.stun > 0 || extra == EL_THUNDER) {
				for (int k = 0; k < 3; k++)
					mote(h * 0.95f, tm * 2.4f + k * 2.09f, 0.65f, 2, 1.f, 0.85f, 0.2f, 0.25f, 0.f, 0.35f, flame * 0.4f, 1);
			}
			if (av.sleep > 0) {
				for (int k = 0; k < 3; k++)
					mote(0.12f + k * 0.08f, tm * 0.5f + k * 2.1f, 0.7f, 0, 0.55f, 0.7f, 1.f, 0.12f, 0.f, 0.8f, flame * 0.7f, 1);
			}
			if (av.freeze > 0 || extra == EL_ICE) {
				for (int k = 0; k < 4; k++)
					mote(h * (0.15f + k * 0.22f), k * 1.57f + 0.4f, 1.2f, 6, 0.9f, 0.97f, 1.f, 0.f, 0.f, 0.8f, 0.07f + h * 0.03f, 1);
			}
		}
		if (!moving) {
			V3 g = UnitGroundPos(u);
			float k = Clampf(dt * 12.f, 0, 1);
			u.px = Lerpf(u.px, g.x, k); u.py = Lerpf(u.py, g.y, k); u.pz = Lerpf(u.pz, g.z, k);
		}
		float baseHover = a.hover;
		u.hover = baseHover > 0 ? baseHover + sinf(m_time * 2.2f + i) * .06f : 0;
		if (a.poseHold == 1 || a.poseHold == 2) u.hover = 0;
		m_faceShow[i] = AngLerp(m_faceShow[i], u.face, Clampf(dt * 10.f, 0, 1));
		float targetHp = (m_phase == PH_ACTION) ? m_dispHp[i] : (float)u.hp;
		u.hpShow = Lerpf(u.hpShow, targetHp, Clampf(dt * 5.f, 0, 1));
		if (fabsf(u.hpShow - targetHp) < .05f) u.hpShow = targetHp;
		u.flash = max(0.f, u.flash - dt * 3.f);
		if (!u.alive && m_dieT[i] < 0) m_dieT[i] = 0;
		BOOL holdRecoil = m_seqCut && m_cutStage >= 1 && m_cutStage <= 3 && !UnitInArena(i);
		if (a.recoil > 0 && !holdRecoil) a.recoil = max(0.f, a.recoil - dt * (a.recoil > .55f ? 1.15f : 2.5f));
		if (m_dieT[i] >= 0) {
			m_dieT[i] += dt;
			if (!u.alive && u.team != TEAM_PLAYER && m_dieT[i] > 1.15f) u.dissolve = 1.f;
			else if (m_dieT[i] > .9f) {
				float cap = u.team == TEAM_PLAYER ? 0.42f : 1.f;
				u.dissolve = min(cap, u.dissolve + dt * .9f);
			}
		}
		BOOL shownDead = !u.alive || ((m_phase == PH_ACTION) ? (m_dispHp[i] <= 0.5f) : (u.hp <= 0));
		if (shownDead && a.clip != CL_DEATH && a.clip != CL_SPAWN) HoldCorpse(a);
		else if (!a.loop && a.clip != CL_DEATH && ActorClipDone(a, m_lib, m_r) && !(m_phase == PH_ACTION && i == (m_seqCur < m_seqN ? m_seq[m_seqCur].attacker : -1))) {
			if (u.alive && u.hp > 0 && (m_dieT[i] < 0 || a.clip == CL_SPAWN)) {
				a.animSpeed = 1.f;
				ActorPlay(a, (m_phase == PH_OVER && m_victory && u.team == TEAM_PLAYER && m_dieT[i] < 0) ? CL_CHEER : CL_IDLE, 1, .2f);
			}
		}
	}
}

void Game::UpdateCamera(float dt)
{
	V3 focus = m_camGoal;
	if (m_cur >= 0 && m_phase != PH_INTRO) {
		const Unit& u = m_b.u[m_cur];
		focus = v3(u.px, u.py, u.pz);
		if (m_phase == PH_ACTION && m_seqPrimary >= 0 && m_seqPrimary != m_cur) {
			const Unit& d = m_b.u[m_seqPrimary];
			focus = Lerp(focus, v3(d.px, d.py, d.pz), .5f);
		}
	}
	if (m_phase == PH_INTRO && m_surveyN >= 2) {
		float f = Smooth01(Clampf(m_phaseT / (kIntroSweep * TacPaceTime(m_save)), 0, 1));
		focus = Lerp(m_surveyPt[0], m_surveyPt[1], f);
		m_camDistGoal = Lerpf(42.f, 18.f, f);
	}
	BOOL playerSel = m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE || m_phase == PH_P_CMD || m_phase == PH_P_SKILLS || m_phase == PH_P_ITEM || m_phase == PH_P_SUMMON || m_phase == PH_MENU;
	if (!playerSel) m_camPan = m_camPan * expf(-dt * 4.f);
	focus = focus + m_camPan;
	focus.x = Clampf(focus.x, 0, (float)MAPN);
	focus.z = Clampf(focus.z, 0, (float)MAPN);
	float sp = m_save.camSpeed <= 0 ? .55f : (m_save.camSpeed >= 2 ? 1.75f : 1.f);
	float camPace = 1.f;
	if (m_phase == PH_MOVING || m_phase == PH_E_MOVE || m_phase == PH_E_SHOW) camPace = TacPaceTime(m_save);
	else if (m_phase == PH_ACTION) camPace = m_seqCut ? TacCutTime(m_save) : TacPaceTime(m_save);
	if (camPace < 0.05f) camPace = 0.05f;
	float k = (m_phase == PH_INTRO) ? 1.f : Clampf(dt * 4.5f * sp / camPace, 0, 1);
	m_camTarget = Lerp(m_camTarget, focus, k);
	m_camYaw = AngLerp(m_camYaw, m_camYawGoal, Clampf(dt * 6.f * sp / camPace, 0, 1));
	if (m_zoomHold) ApplyZoomHold(dt);
	else m_camDist = Lerpf(m_camDist, m_camDistGoal, Clampf(dt * (m_phase == PH_INTRO ? 6.f : 5.f) / camPace, 0, 1));
}

void Game::UpdateBattle(float dt)
{
	if (m_demo == 3) return;
	// 装備の確認や売却はデモでは出さない。セーブは戻すので、ここで選ばせない。
	if (m_demo && (m_stashFull || m_shopFit)) { m_stashFull = 0; m_shopFit = 0; }
	if (m_stashFull) { UpdateUnitsVisual(dt); return; }
	m_phaseT += dt;
	{
		float vis = dt;
		float mul = 1.f;
		if (m_phase == PH_ACTION) mul = m_seqCut ? TacCutTime(m_save) : TacPaceTime(m_save);
		else if (m_phase == PH_MOVING || m_phase == PH_E_MOVE) mul = TacPaceTime(m_save);
		if (mul < 0.05f) mul = 0.05f;
		if (mul != 1.f) vis = dt / mul;
		UpdateUnitsVisual(vis);
	}
	switch (m_phase) {
	case PH_INTRO:
		if (m_phaseT > (m_surveyN >= 2 ? kIntroSweep + 0.7f : 0.35f) * TacPaceTime(m_save)) { m_phase = PH_NEXT; m_phaseT = 0; }
		break;
	case PH_NEXT: BeginTurn(); break;
	case PH_MOVING: case PH_E_MOVE: UpdateMove(dt); break;
	case PH_ACTION: UpdateSeq(dt); break;
	case PH_E_SHOW: {
		float wait = 0.85f * TacPaceTime(m_save);
		if (wait < 0.04f) wait = 0.04f;
		if (m_cur >= 0 && m_cur < m_b.nUnits) {
			const Unit& u = m_b.u[m_cur];
			float f = Clampf(m_phaseT / max(0.05f, wait), 0.f, 1.f);
			float e = f * f * (3.f - 2.f * f);
			float wob = (1.f - f) * sinf(m_phaseT * 9.f) * (TacAi(m_save) <= 1 ? 1.6f : 0.45f);
			float x = u.x + ((float)m_ai.moveX - u.x) * e + wob;
			float z = u.z + ((float)m_ai.moveZ - u.z) * e + wob * 0.6f;
			if (f > 0.55f && m_ai.target >= 0 && m_ai.target < m_b.nUnits && m_b.u[m_ai.target].alive) {
				float g = (f - 0.55f) / 0.45f;
				const Unit& t = m_b.u[m_ai.target];
				x = x + ((float)t.x - x) * g;
				z = z + ((float)t.z - z) * g;
			}
			m_cx = Clampi((int)lroundf(x), 0, MAPN - 1);
			m_cz = Clampi((int)lroundf(z), 0, MAPN - 1);
			m_cursorValid = TRUE;
		}
		if (m_phaseT >= wait) {
			const Unit& u = m_b.u[m_cur];
			if (m_ai.moveX != u.x || m_ai.moveZ != u.z) { m_moved = 1; StartMove(m_cur, m_ai.moveX, m_ai.moveZ, PH_NONE); }
			else if (m_ai.wild == 2) ConfuseSelf(m_cur);
			else if (m_ai.target >= 0) {
				const SkillDef* sd = m_ai.skill >= 0 ? &SkillInfo(m_ai.skill) : NULL;
				BOOL ok = sd ? (sd->rMax == 0 || CanTarget(m_cur, m_ai.target, m_ai.skill)) : CanTarget(m_cur, m_ai.target, -1);
				if (!ok && m_ai.wild && m_ai.skill < 0 && m_ai.target != m_cur && m_b.u[m_ai.target].alive) {
					const Unit& t = m_b.u[m_ai.target];
					ok = InRangeOfUnit(m_b, u, u.x, u.z, UnitRangeMin(u), UnitRangeMax(u), t, true);
				}
				if (ok) BeginAttack(m_cur, m_ai.target, m_ai.skill, m_ai.castX, m_ai.castZ);
				else FinishUnitTurn(m_ai.wild ? 1 : 0);
			} else FinishUnitTurn(m_ai.wild ? 1 : 0);
		}
	} break;
	case PH_END:
		if (m_phaseT > (m_b.stampedeShow ? 1.25f : .22f) * TacPaceTime(m_save)) {
			m_b.stampedeShow = 0;
			m_phase = PH_NEXT;
			m_phaseT = 0;
		}
		break;
	case PH_LEVELUP:
		m_lvT += dt;
		if (!m_promoAsk && m_lvqCur == 0 && m_lvqN > 0 && m_lvT - dt < 2.6f * TacPaceTime(m_save) && m_lvT >= 2.6f * TacPaceTime(m_save)) {
			TacSfx(TS_LEVEL);
			if (m_lvq[0].nLearned > 0) TacSfx(TS_LEARN);
		}
		break;
	case PH_OVER:
		m_overT += dt;
		if (m_overT > 3.f * TacPaceTime(m_save)) EndBattle(m_victory);
		break;
	default: break;
	}
	if (m_screen != SC_BATTLE) return;
	if (m_demo == 2) DemoDrive(dt);
	UpdateCamera(dt);
	if (m_mouseSeen && !m_navMode && !m_demo) UpdateHover();
	if (m_flashWhite > 0) m_flashWhite = max(0.f, m_flashWhite - dt * 3.f);
}

// 画面に床・水面・壁のどれかがある。奈落だけのときは反射を描かない。
BOOL Game::ViewShowsGround() const
{
	if (m_r.GroundChunkVisible()) return TRUE;
	const Map& map = m_b.map;
	for (int z = 0; z < MAPN; z++) {
		for (int x = 0; x < MAPN; x++) {
			if (map.At(x, z).t == T_VOID) continue;
			float y = map.StandY(x, z);
			if (m_r.InView(v3((float)x + 0.5f, y + 0.4f, (float)z + 0.5f), 2.4f)) return TRUE;
		}
	}
	return FALSE;
}

// ============================================================ 描画（マップ）
void Game::ClearRipples()
{
	memset(m_rip, 0, sizeof(m_rip));
	m_ripHead = 0;
	for (int i = 0; i < MAX_UNITS; i++) {
		m_ripSeen[i][0] = m_ripSeen[i][1] = 0.f;
		m_ripSeenT[i] = -100.f;
	}
}

void Game::DrawBattle3D()
{
	LARGE_INTEGER pq, pt;
	QueryPerformanceFrequency(&pq);
	QueryPerformanceCounter(&pt);
	m_msSh = m_msRef = 0;
	auto take = [&](float& dst) {
		LARGE_INTEGER n; QueryPerformanceCounter(&n);
		dst = pq.QuadPart ? (float)((double)(n.QuadPart - pt.QuadPart) * 1000.0 / (double)pq.QuadPart) : 0.f;
		pt = n;
	};
	m_r.SetTheme(m_b.map.theme);
	m_r.SetStageId(m_b.stage);
	m_r.SetSceneShift(MIdent());
	m_r.SetCutDeck(-1);
	m_r.SetCamNear(0.f, 0.f, 0.f);
	Env e;
	ThemeEnv(m_b.map.theme, e);
	V3 target = m_camTarget + v3(0, .4f, 0);
	if (m_shake > 0) target = target + v3(m_rng.F(-1, 1), m_rng.F(-1, 1) * .5f, m_rng.F(-1, 1)) * (m_shake * .08f);
	ApplyZoomHold();
	SetupCamera(target, m_camYaw, m_camPitch, m_camDist, .62f);
	{
		m_r.SetFog(m_camDist * 1.4f, m_camDist * 4.2f + 18.f, .35f, e.fog);
		float amb = e.amb;
		int hr = TacHour(m_b.weather);
		if (hr == HR_NIGHT || hr == HR_MOON || hr == HR_STAR) amb *= 0.92f;
		else if (hr == HR_DEEP) amb *= 0.88f;
		else if (hr == HR_ECLIPSE) amb *= 0.94f;
		else if (hr == HR_DUSK || hr == HR_DAWN) amb *= 0.97f;
		m_r.SetAmbient(e.ambSky, e.ambGnd, amb, e.hard);
		float sightXz[MAX_PARTY * 2];
		int sightN = 0;
		for (int i = 0; i < m_b.nUnits && sightN < MAX_PARTY; i++) {
			const Unit& u = m_b.u[i];
			if (!u.alive || u.team != TEAM_PLAYER) continue;
			sightXz[sightN * 2] = u.px;
			sightXz[sightN * 2 + 1] = u.pz;
			sightN++;
		}
		m_r.SetSight(sightXz, sightN, (float)TacSightTiles(m_b.weather));
	}
	m_r.SetLight(e.light, m_camTarget, min(30.f, m_camDist * 1.35f + 6.f));
	int fadeAbove = -1, fadeX = -1, fadeZ = -1;
	if (m_cursorValid && Map::In(m_cx, m_cz)) {
		for (int dz = -1; dz <= 1 && fadeAbove < 0; dz++) for (int dx = -1; dx <= 1; dx++) {
			int x = m_cx + dx, z = m_cz + dz;
			if (!Map::In(x, z)) continue;
			if ((int)m_b.map.At(x, z).nLay > m_cLayer + 1) { fadeAbove = m_cLayer; fadeX = m_cx; fadeZ = m_cz; }
		}
	}
	m_r.SetFadeAbove(fadeAbove, fadeX, fadeZ);
	auto nearCam = [&](const Unit& u) -> BOOL {
		int ui = (int)(&u - m_b.u);
		if (ui >= 0 && ui < m_b.nUnits && (ui == m_cur || ui == m_hoverUnit)) return TRUE;
		int sp = UnitSpan(u);
		if (sp < 1) sp = 1;
		if (sp > 4) sp = 4;
		return m_r.InView(v3(u.px, u.py + .7f * (float)sp, u.pz), 2.4f * (float)sp);
	};
	auto foeGone = [&](int i) -> BOOL {
		const Unit& u = m_b.u[i];
		if (u.alive || u.team == TEAM_PLAYER) return FALSE;
		return m_dieT[i] < 0.f || m_dieT[i] > 1.15f;
	};
	// 見ている層以外は透かす。その層にいる者は隠さない。床の奥に消さないため、別層は深度を切って描く。
	auto offLayer = [&](int i) -> BOOL {
		const Unit& u = m_b.u[i];
		if (!m_cursorValid || !u.alive || i == m_cur) return FALSE;
		if (m_phase == PH_ACTION || m_phase == PH_MOVING || m_phase == PH_E_MOVE || m_phase == PH_INTRO || m_phase == PH_OVER) return FALSE;
		int d = u.deck;
		if (d < 0 || d >= LAY_N || !LayerOn(m_b.map.At(u.x, u.z), d)) d = 0;
		return d != m_cLayer;
	};
	for (int i = 0; i < m_b.nUnits; i++) {
		if (foeGone(i) || m_b.u[i].dissolve >= 1.f || !nearCam(m_b.u[i])) continue;
		ActorPose(m_act[i], m_lib, m_r);
	}
	float shR = min(30.f, m_camDist * 1.35f + 6.f);
	auto inLight = [&](const Unit& u) -> BOOL {
		int sp = UnitSpan(u);
		if (sp < 1) sp = 1;
		float dx = u.px - m_camTarget.x, dz = u.pz - m_camTarget.z;
		float lim = shR + (float)sp + 3.f;
		return dx * dx + dz * dz <= lim * lim;
	};
	BOOL seeGround = ViewShowsGround();
	if (seeGround) {
	m_r.BeginShadowPass();
	m_r.DrawTerrain();
	{
		int model[MAX_UNITS], ava[MAX_UNITS], cup[MAX_UNITS], n = 0;
		for (int i = 0; i < m_b.nUnits && n < MAX_UNITS; i++) {
			if (foeGone(i) || m_b.u[i].dissolve >= .5f || !nearCam(m_b.u[i]) || !inLight(m_b.u[i])) continue;
			const Actor& a = m_act[i];
			if (a.avatar <= 0 || a.model < 0) continue;
			model[n] = a.model; ava[n] = a.avatar; cup[n] = a.bust; n++;
		}
		int pend = m_r.CountPendingAvatars(n, model, ava, cup);
		m_r.BeginAvaBatch(pend > 0 ? pend : 1);
	}
	m_r.BeginInstBatch();
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (foeGone(i) || u.dissolve >= .5f || !nearCam(u) || !inLight(u)) continue;
		DrawParams dp;
		if (offLayer(i)) dp.tint = 0x78FFFFFF;
		ActorDraw(m_r, m_lib, m_act[i], ActorWorld(m_act[i], AilmentPos(m_act[i], ActorPos(i)), ActorFace(i)), dp, m_act[i].poseHold == 3 ? 0.f : m_time);
	}
	m_r.EndInstBatch();
	}
	take(m_msSh);
	{
		V3 look = m_camTarget + v3(0, .4f, 0);
		BOOL reflectBusy = m_phase == PH_MOVING || m_phase == PH_E_MOVE || m_phase == PH_ACTION || m_phase == PH_INTRO || m_shake > 0.02f;
		static int s_refFrame = -100;
		static float s_yaw = 0.f, s_pitch = 0.f, s_dist = 0.f, s_tx = 0.f, s_tz = 0.f;
		BOOL camMove = fabsf(m_camYaw - s_yaw) > 0.004f || fabsf(m_camPitch - s_pitch) > 0.004f
			|| fabsf(m_camDist - s_dist) > 0.08f
			|| fabsf(m_camTarget.x - s_tx) > 0.08f || fabsf(m_camTarget.z - s_tz) > 0.08f;
		// 奈落だけを見ているときは、床も水面も反射をサンプルしない。シーンの描き直しは待ちになるだけ。
		BOOL drawRef = seeGround && (reflectBusy || camMove || s_refFrame < 0 || (m_r.m_frame - s_refFrame) >= 2);
		if (seeGround && !drawRef) m_r.ReuseReflect(m_camTarget.y);
		else if (seeGround && m_r.BeginReflectPass(m_camTarget.y, look)) {
			m_r.DrawTerrain();
			m_r.DrawSky(e.skyTop, e.skyBot, 0, 0);
			m_r.DrawWater();
			m_r.BeginInstBatch();
			for (int i = 0; i < m_b.nUnits; i++) {
				const Unit& u = m_b.u[i];
				if (foeGone(i) || u.dissolve >= 1.f || !nearCam(u)) continue;
				DrawParams dp;
				dp.rim = u.team == TEAM_PLAYER ? .35f : .45f;
				dp.dissolve = u.dissolve;
				V3 drawPos = AilmentPos(m_act[i], ActorPos(i));
				ActorDraw(m_r, m_lib, m_act[i], ActorWorld(m_act[i], drawPos, ActorFace(i)), dp, m_act[i].poseHold == 3 ? 0.f : m_time);
			}
			m_r.EndInstBatch();
			m_r.EndReflectPass();
			s_refFrame = (int)m_r.m_frame;
			s_yaw = m_camYaw;
			s_pitch = m_camPitch;
			s_dist = m_camDist;
			s_tx = m_camTarget.x;
			s_tz = m_camTarget.z;
		}
	}
	take(m_msRef);
	m_r.BeginMainPass();
	{
		for (int i = 0; i < m_b.nUnits; i++) {
			const Unit& u = m_b.u[i];
			if (!u.alive && u.dissolve > 0.2f) continue;
			int x = (int)floorf(u.px), z = (int)floorf(u.pz);
			if (!Map::In(x, z)) continue;
			int tt = m_b.map.At(x, z).t;
			if (tt != T_WATER && tt != T_LAVA) { m_ripSeenT[i] = -100.f; continue; }
			float dx = u.px - m_ripSeen[i][0], dz = u.pz - m_ripSeen[i][1];
			float since = m_time - m_ripSeenT[i];
			BOOL walk = (m_phase == PH_MOVING || m_phase == PH_E_MOVE) && m_moveUnit == i;
			float need = walk ? 0.16f : 0.34f;
			float gap = walk ? 0.09f : 0.42f;
			if (m_ripSeenT[i] < -50.f || dx * dx + dz * dz > need * need || since > gap) {
				int s = m_ripHead % TAC_RIPN;
				m_ripHead++;
				m_rip[s][0] = u.px;
				m_rip[s][1] = m_time;
				m_rip[s][2] = u.pz;
				m_rip[s][3] = walk ? 1.35f : (tt == T_LAVA ? 0.75f : 0.7f);
				m_ripSeen[i][0] = u.px;
				m_ripSeen[i][1] = u.pz;
				m_ripSeenT[i] = m_time;
			}
		}
		m_r.SetLiquidRipples(&m_rip[0][0], TAC_RIPN);
	}
	if (seeGround) m_r.DrawTerrain();
	m_r.DrawSky(e.skyTop, e.skyBot, 0, 0);
	if (seeGround) m_r.DrawWater();
	BuildOverlayQuads();
	m_r.DrawOverlayQuads(m_ovQuads.data(), m_ovQuads.size(), 0);
	// カーソル
	BOOL showCursor = m_cursorValid && (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE);
	if (showCursor) {
		DWORD cc = RGBA8(255, 255, 255, 230);
		if (m_phase == PH_P_MOVE) {
			BOOL ok = ReachCanStopLayer(m_reach, m_cx, m_cz, m_cLayer) || m_pathN > 1 || (m_cur >= 0 && m_cx == m_b.u[m_cur].x && m_cz == m_b.u[m_cur].z && m_cLayer == m_b.u[m_cur].deck);
			int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
			if (m_cur >= 0 && ChestInRange(m_cur, m_cx, m_cz)) cc = RGBA8(255, 210, 70, 255);
			else if (t >= 0 && m_cur >= 0 && m_b.u[t].team != m_b.u[m_cur].team && (m_pathN > 1 || CanTarget(m_cur, t, -1))) cc = RGBA8(255, 200, 80, 255);
			else if (!ok) cc = RGBA8(255, 70, 60, 230);
			else cc = RGBA8(140, 230, 255, 255);
		} else {
			int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
			if (t < 0 && m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0 && SkillInfo(m_cmdSkill).effect == EF_RAISE)
				t = CorpseAt(m_b, m_cx, m_cz);
			BOOL ok = m_cur >= 0 && (m_phase == PH_P_TARGET ? CanTarget(m_cur, t, -1) : (m_cmdSkill >= 0 && (SkillInfo(m_cmdSkill).rMax == 0 || CanTarget(m_cur, t, m_cmdSkill))));
			cc = ok ? RGBA8(255, 210, 80, 255) : RGBA8(255, 70, 60, 230);
		}
		VtxS cq[96];
		int cqn = 0;
		const Map& m = m_b.map;
		const Cell& c = m.At(m_cx, m_cz);
		int ly = m_cLayer;
		int kind = ly > 0 ? 2 : 0;
		if (kind == 0 && (c.t == T_WATER || c.t == T_LAVA)) { kind = 1; ly = (int)c.t; }
		float mid = TacVisY(m, m_cx, m_cz, m_cx + 0.5f, m_cz + 0.5f, kind, ly);
		float base = m.WalkY(m_cx, m_cz, m_cLayer);
		float grow = .04f * sinf(m_time * 6.f);
		float x0 = m_cx - grow, x1 = m_cx + 1.f + grow, z0 = m_cz - grow, z1 = m_cz + 1.f + grow;
		const int N = 4;
		static const int ord[6] = { 0, 1, 2, 0, 2, 3 };
		for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
			float u0 = (float)i / N, u1 = (float)(i + 1) / N;
			float v0 = (float)j / N, v1 = (float)(j + 1) / N;
			float xa[4] = { x0 + (x1 - x0) * u0, x0 + (x1 - x0) * u0, x0 + (x1 - x0) * u1, x0 + (x1 - x0) * u1 };
			float za[4] = { z0 + (z1 - z0) * v0, z0 + (z1 - z0) * v1, z0 + (z1 - z0) * v1, z0 + (z1 - z0) * v0 };
			float uvx[4] = { u0, u0, u1, u1 };
			float uvy[4] = { v0, v1, v1, v0 };
			for (int k = 0; k < 6; k++) {
				int s = ord[k];
				VtxS v;
				v.p[0] = xa[s];
				v.p[1] = base + (TacVisY(m, m_cx, m_cz, xa[s], za[s], kind, ly) - mid) + 0.1f;
				v.p[2] = za[s];
				v.n[0] = 0; v.n[1] = 1; v.n[2] = 0;
				v.uv[0] = uvx[s]; v.uv[1] = uvy[s];
				v.c = cc;
				cq[cqn++] = v;
			}
		}
		m_r.DrawOverlayQuads(cq, cqn, 1);
	}
	// 足元リング
	m_r.BeginInstBatch();
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (!nearCam(u)) continue;
		if (!u.alive && (u.team != TEAM_PLAYER || u.dissolve >= 1.f)) continue;
		BOOL active = u.alive && i == m_cur && m_phase != PH_OVER;
		DrawParams rp;
		rp.noTess = 1;
		rp.emissive = active ? 1.4f : .6f;
		rp.rim = 0;
		float pulse = active ? .8f + .2f * sinf(m_time * 5.f) : .5f;
		DWORD tc = u.team == TEAM_PLAYER ? RGBA8(90, 170, 255, (int)(255 * pulse * (active ? 1.f : .55f))) : RGBA8(255, 80, 70, (int)(255 * pulse * (active ? 1.f : .55f)));
		if (u.boss) tc = RGBA8(255, 200, 60, (int)(255 * pulse * (active ? 1.f : .7f)));
		else if (UnitIsLucky(u)) tc = RGBA8(70, 220, 255, (int)(255 * pulse * (active ? 1.f : .7f)));
		if (!u.alive) tc = RGBA8(170, 176, 190, 120);
		BOOL ghost = offLayer(i);
		if (ghost) {
			tc = (tc & 0x00FFFFFF) | 0xA0000000;
			m_r.SetDepthTest(FALSE);
		}
		rp.tint = tc;
		float s = active ? 1.f + .06f * sinf(m_time * 5.f) : .85f;
		int sp = UnitSpan(u);
		if (sp < 1) sp = 1;
		if (sp > 4) sp = 4;
		float sc = s * (float)sp * 0.92f;
		V3 g = v3(u.px, u.py + .05f, u.pz);
		m_r.DrawStatic(m_lib.ring, MMul(MMul(MScale(sc, 1, sc), MRotY(m_time * (active ? 1.2f : .3f))), MTrans(g.x, g.y, g.z)), rp);
		if (ghost) m_r.SetDepthTest(TRUE);
	}
	m_r.EndInstBatch();
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 1) m_r.BeginInstBatch();
		for (int i = 0; i < m_b.nUnits; i++) {
			const Unit& u = m_b.u[i];
			BOOL ghost = offLayer(i);
			if ((pass == 0) != ghost) continue;
			if (foeGone(i) || u.dissolve >= 1.f || !nearCam(u)) continue;
			DrawParams dp;
			dp.outline = .013f;
			if (u.team == TEAM_ENEMY && u.alert == 2) dp.tint = RGBA8(255, 176, 72);
			else if (!u.alert && u.team == TEAM_ENEMY) dp.tint = RGBA8(150, 160, 190);
			dp.dissolve = u.dissolve;
			dp.flash = u.flash;
			dp.rim = u.team == TEAM_PLAYER ? .35f : .45f;
			if (m_hoverUnit == i && m_phase != PH_ACTION) dp.rim = .9f;
			if ((m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET) && m_cur >= 0 && CanTarget(m_cur, i, m_phase == PH_P_TARGET ? -1 : m_cmdSkill))
				dp.flash = max(dp.flash, .25f + .2f * sinf(m_time * 7.f));
			// カットシーン中は予測解決済みの u.* ではなく、着弾まで据え置く m_visAil を使う。
			// （入った瞬間に死亡／凍結モーションになるのを防ぐ。着弾で SeqImpact が更新する）
			VisAil vt = (m_phase == PH_ACTION) ? m_visAil[i] : VisAil{};
			if (m_phase != PH_ACTION) {
				vt.poison = u.poison; vt.slow = u.slow; vt.defDown = u.defDown;
				vt.sleep = u.sleep; vt.confuse = u.confuse; vt.stun = u.stun; vt.freeze = u.freeze;
				vt.burn = u.burn; vt.bleed = u.bleed; vt.blind = u.blind; vt.silence = u.silence;
			}
			int blendEl = (m_ailBlend[i] > 0.f) ? m_ailBlendEl[i] : 0;
			float blendK = Clampf(m_ailBlend[i] / 1.05f, 0.f, 1.f);
			if (u.alive && (m_phase != PH_ACTION || m_dispHp[i] > 0.5f)) {
				auto applyAilTint = [&](int freeze, int burn, int poison, int bleed, int silence, int blind, int slow, int confuse) {
					if (freeze > 0) {
						dp.tint = RGBA8(225, 242, 255);
						dp.flash = 0.84f;
						dp.emissive = 0.65f;
						dp.rim = 0.15f;
					} else if (burn > 0) {
						dp.tint = RGBA8(255, 110, 28);
						dp.emissive = 1.25f;
						dp.flash = max(dp.flash, 0.12f + 0.08f * sinf(m_time * 14.f));
						dp.rim = 1.f;
					} else if (poison > 0) {
						dp.tint = RGBA8(150, 255, 90);
						dp.emissive = 0.45f;
						dp.rim = 0.7f;
					} else if (bleed > 0) {
						dp.tint = RGBA8(255, 70, 70);
						dp.emissive = 0.35f;
					} else if (silence > 0) {
						dp.tint = RGBA8(190, 140, 255);
						dp.emissive = 0.4f;
					} else if (blind > 0) {
						dp.tint = RGBA8(70, 70, 80);
						dp.emissive = 0.05f;
					} else if (slow > 0) {
						dp.tint = RGBA8(170, 140, 90);
					} else if (confuse > 0) {
						dp.tint = RGBA8(230, 140, 255);
						dp.emissive = 0.25f;
					}
				};
				applyAilTint(vt.freeze, vt.burn, vt.poison, vt.bleed, vt.silence, vt.blind, vt.slow, vt.confuse);
				// 炎↔氷などの入れ替え中は旧属性の色を残してクロスフェード。
				if (blendK > 0.02f && blendEl > 0) {
					DWORD keep = dp.tint;
					float em = dp.emissive, fl = dp.flash, rm = dp.rim;
					if (blendEl == EL_FIRE) { dp.tint = RGBA8(255, 110, 28); dp.emissive = 1.25f; dp.rim = 1.f; }
					else if (blendEl == EL_ICE) { dp.tint = RGBA8(225, 242, 255); dp.emissive = 0.65f; dp.flash = 0.84f; dp.rim = 0.15f; }
					else if (blendEl == EL_POISON) { dp.tint = RGBA8(150, 255, 90); dp.emissive = 0.45f; }
					else if (blendEl == EL_THUNDER) { dp.tint = RGBA8(255, 236, 120); dp.emissive = 0.8f; }
					else if (blendEl == EL_DARK) { dp.tint = RGBA8(190, 140, 255); dp.emissive = 0.4f; }
					else if (blendEl == EL_EARTH) { dp.tint = RGBA8(230, 140, 255); dp.emissive = 0.25f; }
					auto lerpB = [](DWORD a, DWORD b, float t) -> DWORD {
						int ar = (a >> 0) & 255, ag = (a >> 8) & 255, ab = (a >> 16) & 255, aa = (a >> 24) & 255;
						int br = (b >> 0) & 255, bg = (b >> 8) & 255, bb = (b >> 16) & 255, ba = (b >> 24) & 255;
						int r = (int)(br + (ar - br) * t), g = (int)(bg + (ag - bg) * t), bl = (int)(bb + (ab - bb) * t), al = (int)(ba + (aa - ba) * t);
						return RGBA8(r, g, bl, al);
					};
					dp.tint = lerpB(keep, dp.tint, blendK);
					dp.emissive = em + (dp.emissive - em) * (1.f - blendK);
					dp.flash = fl + (dp.flash - fl) * (1.f - blendK);
					dp.rim = rm + (dp.rim - rm) * (1.f - blendK);
				}
			}
			V3 drawPos = AilmentPos(m_act[i], ActorPos(i));
			if (!ghost && u.deck > 0) drawPos.y += 0.04f;
			if (ghost) {
				dp.tint = (dp.tint & 0x00FFFFFF) | 0xA0000000;
				m_r.SetDepthTest(FALSE);
			}
			ActorDraw(m_r, m_lib, m_act[i], ActorWorld(m_act[i], drawPos, ActorFace(i)), dp, m_act[i].poseHold == 3 ? 0.f : m_time);
			BOOL showIce = (m_phase == PH_ACTION ? vt.freeze > 0 : u.freeze > 0) && (m_phase != PH_ACTION || m_dispHp[i] > 0.5f);
			if (u.alive && showIce && m_lib.ice.vb) {
				DrawParams ip;
				ip.noTess = 1;
				ip.tint = RGBA8(236, 248, 255, 210);
				ip.emissive = 0.75f;
				ip.twoSided = 1;
				ip.outline = 0.02f;
				ip.outlineColor = RGBA8(255, 255, 255);
				float h = m_act[i].height > 0.4f ? m_act[i].height : 1.3f;
				int spn = UnitSpan(u);
				float wide = max(h * 0.95f, (float)spn * 0.9f);
				m_r.DrawStatic(m_lib.ice, MMul(MMul(MScale(wide, h * 1.15f, wide), MRotY(i * 0.35f)), MTrans(drawPos.x, drawPos.y, drawPos.z)), ip);
			}
			if (u.boss && u.alive) {
				DrawParams cp;
				cp.noTess = 1;
				cp.emissive = .6f;
				cp.outline = .008f;
				if (ghost) cp.tint = 0xA0FFFFFF;
				V3 hp = drawPos + v3(0, m_act[i].height + .22f + sinf(m_time * 2.f) * .04f, 0);
				m_r.DrawStatic(m_lib.crown, MMul(MMul(MScale(.9f, .9f, .9f), MRotY(m_time)), MTrans(hp.x, hp.y, hp.z)), cp);
			} else if (UnitIsLucky(u) && u.alive) {
				DrawParams lp;
				lp.noTess = 1;
				lp.emissive = 1.15f;
				lp.outline = .006f;
				lp.tint = ghost ? RGBA8(80, 230, 255, 160) : RGBA8(80, 230, 255);
				V3 hp = drawPos + v3(0, m_act[i].height + .18f + sinf(m_time * 3.f) * .04f, 0);
				m_r.DrawStatic(m_lib.orb, MMul(MMul(MScale(.2f, .2f, .2f), MRotY(m_time * 2.4f)), MTrans(hp.x, hp.y, hp.z)), lp);
			}
			if (ghost) m_r.SetDepthTest(TRUE);
		}
	}
	m_r.EndInstBatch();
	m_r.DrawParticles(m_parts.data(), m_parts.size());
	m_r.DrawWxGpu();
}

// 召喚の拍。媒質が生まれて広がり、獣が出て、溜めて撃ち、出口の媒質に入り、媒質が消える。
// 攻撃の型だけ獣ごとに変える。入り口と退場は属性（炎・津波・地割れ・雨など）で分かれる。
// 0息 1突進 2叩き 3急降下 4飛びかかり 5咆哮 6薙ぎ 7跳躍 8周回 9突進の溜め
static const unsigned char kSumAt[SUMMON_N] = {
	0, 2, 3, 1, 6, 9, 1, 0, 8, 9, 4, 3,
	5, 9, 6, 2, 5, 6, 7, 0, 8, 4, 2, 7,
	3, 9, 6, 0, 5, 6, 5, 1, 0, 6, 2, 3,
};
static_assert(sizeof(kSumAt) / sizeof(kSumAt[0]) == SUMMON_N, "summon attack");

static const float kSumTau = 6.2831853f;
static const float kSumPi = 3.14159265f;

static int SumAtOf(int skill)
{
	int i = skill - SUMMON_BASE;
	if (i < 0 || i >= SUMMON_N) i = 0;
	return kSumAt[i];
}
static int SumIndexOf(int skill)
{
	int i = skill - SUMMON_BASE;
	if (i < 0 || i >= SUMMON_N) i = 0;
	return i;
}
static int SumTheater(int fx)
{
	switch (fx) {
	case SUM_FX_FIRE: return 0;
	case SUM_FX_ICE: return 1;
	case SUM_FX_BOLT: return 2;
	case SUM_FX_SPIKE: return 3;
	case SUM_FX_WIND: return 4;
	case SUM_FX_WAVE: return 5;
	case SUM_FX_HOLY: return 6;
	case SUM_FX_DARK: return 7;
	case SUM_FX_MIASMA: return 8;
	case SUM_FX_ROAR: return 9;
	case SUM_FX_SLEEP: return 10;
	default: return 11;
	}
}
static float SumAmp(int phase, float u)
{
	if (u < 0.f) u = 0.f;
	if (phase <= 0) {
		if (u < 0.28f) return 0.08f + 0.42f * (u / 0.28f);
		if (u < 0.58f) return 0.50f + 0.90f * ((u - 0.28f) / 0.30f);
		return 1.40f;
	}
	if (phase == 1) return 0.72f;
	float appear = u < 0.25f ? (u / 0.25f) : 1.f;
	float shrink = u < 0.70f ? 1.f : (1.f - (u - 0.70f) / 0.30f);
	if (shrink < 0.f) shrink = 0.f;
	return (0.15f + 1.15f * appear) * shrink;
}
static V3 SumFwd(const V3& a, const V3& b)
{
	V3 d = v3(b.x - a.x, 0, b.z - a.z);
	float L = sqrtf(d.x * d.x + d.z * d.z);
	if (L < 0.05f) return v3(0, 0, 1);
	return v3(d.x / L, 0, d.z / L);
}
static V3 SumRight(const V3& f) { return v3(-f.z, 0, f.x); }
static void SummonClip(Actor& a, int clip, int loop, float speed)
{
	if (a.clip != clip || (loop && !a.loop) || (!loop && a.loop))
		ActorPlay(a, clip, loop, .08f);
	a.animSpeed = speed;
}
static float SumStrikeSpan() { return 1.35f; }
static float SumDepartSpan() { return 2.20f; }

static void SumPose(int th, int at, int index, int phase, float u,
	const V3& from, const V3& to, V3& pos, float& yaw, float& dissolve, int& hidden)
{
	if (u < 0.f) u = 0.f;
	V3 fwd = SumFwd(from, to);
	V3 right = SumRight(fwd);
	float sgn = (index & 1) ? 1.f : -1.f;
	float hover = (at == 0) ? 1.15f : 0.f;
	if (th == 0 && index == 24) hover = 2.35f;
	if (th == 0 && index == 27) hover = 1.65f;
	if (th == 2 && index != 35) hover = 1.2f;
	if (th == 4) hover = 0.95f;
	if (th == 5) hover = 0.32f;
	if (th == 6 && at == 0) hover = 0.75f;
	if (th == 7 && index == 8) hover = 1.45f;
	if (th == 8 && index == 20) hover = 1.15f;
	if (th == 10) hover = 1.1f;
	V3 home = from + v3(0, hover, 0);
	if (th == 0 && index == 27) home = from + right * (sgn * 0.35f) + v3(0, hover, 0);
	V3 start = home;
	if (th == 0) start = from + v3(0, -0.15f, 0);
	else if (th == 1 || th == 3 || th == 9 || th == 11) start = from + v3(0, -1.45f, 0);
	else if (th == 2 || th == 6) start = from + v3(0, 5.4f, 0);
	else if (th == 4) start = from + right * 2.4f + v3(0, 2.6f, 0);
	else if (th == 5) start = from - fwd * 5.4f + v3(0, 0.2f, 0);
	else if (th == 7 || th == 8) start = from + v3(0, -0.85f, 0);
	else if (th == 10) start = from + v3(0, 2.9f, 0);
	if (th == 0 && index == 27) start = from + right * (sgn * 2.5f) + v3(0, 2.8f, 0);
	if (th == 0 && at == 4) { start = from + v3(0, -0.7f, 0); home.y = 0.f; }
	V3 hit = home + fwd * 0.35f;
	if (at == 1 || at == 2 || at == 3 || at == 4 || at == 6 || at == 7 || at == 9) hit = to;
	else if (at == 5) hit = Lerp(home, to, 0.42f);
	else if (at == 8) hit = to + right * 0.45f;
	if (th == 5 && at != 0) hit = to + v3(0, 0.2f, 0);
	V3 exitP = hit;
	if (th == 0) {
		float top = home.y > hit.y ? home.y : hit.y;
		exitP = (at == 0) ? v3(home.x, home.y + 1.55f, home.z) : v3(hit.x, top + 1.7f, hit.z);
	} else if (th == 1 || th == 3 || th == 9 || th == 11) exitP = v3(hit.x, -1.5f, hit.z);
	else if (th == 2 || th == 6) exitP = v3(hit.x, 5.6f, hit.z);
	else if (th == 4 || th == 10) exitP = hit + v3(0, 3.3f, 0);
	else if (th == 5) exitP = (from - fwd * 4.2f) + v3(0, -0.55f, 0);
	else if (th == 7) exitP = (index == 8) ? hit + v3(0, 3.2f, 0) : v3(hit.x, -1.2f, hit.z);
	else if (th == 8) exitP = (index == 20) ? hit + v3(0, 2.8f, 0) : v3(hit.x, -1.15f, hit.z);
	float em = Smooth01((u - 0.58f) / 0.42f);
	if (phase <= 0) {
		pos = Lerp(start, home, em);
		if (th == 4) {
			float spin = Smooth01((u - 0.40f) / 0.60f);
			float ang = u * kSumTau * 1.35f;
			float rad = Lerpf(2.7f, 0.15f, spin);
			pos = from + right * (sinf(ang) * rad) + fwd * (cosf(ang) * rad) + v3(0, Lerpf(2.5f, home.y, spin), 0);
		} else if (th == 5) {
			pos = Lerp(start, home, Smooth01(u / 0.92f));
			pos.y = 0.22f + sinf(u * kSumPi) * 0.18f;
		}
	} else if (phase == 1) {
		float uu = u > 1.f ? 1.f : u;
		float rel = (at == 2) ? Smooth01((uu - 0.62f) / 0.38f) : Smooth01((uu - 0.38f) / 0.62f);
		pos = Lerp(home, hit, rel);
		if (at == 3) pos.y = Lerpf(home.y, 0.f, rel) + sinf(rel * kSumPi) * 1.45f;
		else if (at == 4 || at == 7) pos.y += sinf(rel * kSumPi) * 1.15f;
		else if (at == 2) pos.y += sinf(rel * kSumPi) * 0.8f;
		else if (at == 8) pos = pos + right * sinf(rel * kSumTau) * Lerpf(1.5f, 0.15f, rel);
		else if (at == 6) pos = pos + right * sinf(rel * kSumPi) * 1.1f;
	} else {
		float en = Smooth01((u - 0.25f) / 0.47f);
		pos = Lerp(hit, exitP, en);
		if (th == 4) pos = pos + right * sinf(u * kSumTau) * (1.f - en) * 0.9f;
	}
	yaw = atan2f(fwd.x, fwd.z);
	if (th == 4 && phase <= 0) yaw += u * 1.4f;
	dissolve = 0.f;
	if (th == 0) {
		if (phase <= 0) dissolve = 1.f - Smooth01((u - 0.52f) / 0.38f);
		else if (phase >= 2) dissolve = Smooth01((u - 0.28f) / 0.46f);
	}
	hidden = 0;
	if (th == 0) hidden = dissolve > 0.96f ? 1 : 0;
	else if (phase <= 0) {
		if (th == 5) hidden = u < 0.20f ? 1 : 0;
		else if (th == 4) hidden = u < 0.42f ? 1 : 0;
		else if (th == 2 || th == 6 || th == 10) hidden = em < 0.08f ? 1 : 0;
		else hidden = (em < 0.12f || pos.y < -0.9f) ? 1 : 0;
	} else if (phase >= 2) hidden = u > 0.78f ? 1 : 0;
}
void Game::BindSummonCut()
{
	m_sumOn = 0;
	if (!IsSummonId(m_seqSkill)) return;
	int index = m_seqSkill - SUMMON_BASE;
	memset(&m_sumAct, 0, sizeof(m_sumAct));
	m_sumAct.model = -1;
	m_sumAct.scale = 1.f;
	m_sumAct.htScale = 1.f;
	m_sumAct.tint = 0xFFFFFFFF;
	m_sumAct.height = 1.6f;
	m_sumAct.clip = CL_IDLE;
	m_sumAct.prev = CL_IDLE;
	m_sumAct.loop = m_sumAct.ploop = 1;
	m_sumAct.blend = 1.f;
	m_sumAct.blendDur = .12f;
	m_sumAct.animSpeed = 1.f;
	m_sumAct.skillPose = m_sumAct.prevSkill = -1;
	m_sumAct.gripClip = -1;
	int mesh = SummonMesh(index);
	int mi = m_r.CreatureModel(mesh);
	if (mi >= 0 && mi < (int)m_r.models.size()) {
		m_sumAct.proc = 3;
		m_sumAct.model = mi;
		m_sumAct.rig = m_r.models[mi].rig;
		m_sumAct.mods.Reset(m_r.models[mi].nJ);
		float cx = 0, cy = 0, cz = 0, rad = 0;
		float fit = 1.f;
		if (m_r.CreatureExtent(mi, cx, cy, cz, rad) && rad > 0.05f) fit = 1.35f / rad;
		if (fit < 0.45f) fit = 0.45f;
		if (fit > 2.2f) fit = 2.2f;
		m_sumAct.scale = fit;
		m_sumAct.height = max(0.8f, (cy > 0.05f ? cy : 1.2f) * fit);
	} else {
		m_sumAct.proc = 1;
		m_sumAct.look = LK_GOLEM;
		m_sumAct.model = m_lib.procModel[LK_GOLEM];
		m_sumAct.rig = -1;
		m_sumAct.scale = 1.15f;
		m_sumAct.height = 1.75f * 1.15f;
		if (m_sumAct.model >= 0 && m_sumAct.model < (int)m_r.models.size()) m_sumAct.mods.Reset(m_r.models[m_sumAct.model].nJ);
	}
	V3 foe = m_cutFoeN > 0 ? m_cutFoePos[0] : v3(1.4f, 0, 0);
	float dx = foe.x - m_cutPosA.x;
	float dz = foe.z - m_cutPosA.z;
	float len = sqrtf(dx * dx + dz * dz);
	if (len < 0.05f) { dx = 1.f; dz = 0.f; len = 1.f; }
	dx /= len; dz /= len;
	m_sumFrom = m_cutPosA + v3(dx * 0.85f, 0, dz * 0.85f);
	m_sumTo = foe + v3(-dx * 0.55f, 0, -dz * 0.55f);
	m_sumFrom.y = 0;
	m_sumTo.y = 0;
	m_sumFx = SummonFx(index);
	m_sumHit = 0;
	m_sumU = 0;
	m_sumOn = 1;
	{
		float yaw = 0, dis = 1;
		int hide = 1;
		SumPose(SumTheater(m_sumFx), SumAtOf(m_seqSkill), index, 0, 0.f, m_sumFrom, m_sumTo, m_sumPos, yaw, dis, hide);
		m_sumYaw = yaw;
		m_sumFade = dis;
		m_sumHide = hide;
		ActorPlay(m_sumAct, CL_IDLE, 1, 0);
	}
	TacSfxSummon(0, m_sumFx);
}

float Game::SummonStrikeNeed() const
{
	return SumStrikeSpan() + SumDepartSpan();
}

float Game::SummonDepartNeed() const
{
	return SumDepartSpan();
}

void Game::SummonApply(int phase, float u)
{
	float yaw = 0, dis = 0;
	int hide = 0;
	V3 pos = m_sumPos;
	SumPose(SumTheater(m_sumFx), SumAtOf(m_seqSkill), SumIndexOf(m_seqSkill), phase, u,
		m_sumFrom, m_sumTo, pos, yaw, dis, hide);
	m_sumPos = pos;
	m_sumYaw = yaw;
	m_sumFade = dis;
	m_sumHide = hide;
	m_sumU = u;
}

void Game::SummonArrive(float dt)
{
	(void)dt;
	float prev = m_sumU;
	float u = m_cutStageT / max(0.2f, CutIntroLen());
	if (u > 1.f) u = 1.f;
	SummonApply(0, u);
	SummonClip(m_sumAct, CL_IDLE, 1, 1.f);
	SummonMedium(0, u);
	if (prev < 0.9f && u >= 0.9f) Shake(.28f);
}

void Game::SummonStrike(float dt)
{
	(void)dt;
	float u = m_cutStageT / SumStrikeSpan();
	int at = SumAtOf(m_seqSkill);
	if (!m_sumHit) {
		SummonApply(1, u > 1.f ? 1.f : u);
		if (u < 0.12f) SummonClip(m_sumAct, CL_IDLE, 1, 1.f);
		else {
			if (m_sumAct.clip != CL_1H_CHOP) ActorPlay(m_sumAct, CL_1H_CHOP, 0, .06f);
			m_sumAct.animSpeed = u < 0.38f ? 0.38f : 1.2f;
		}
		SummonMedium(1, u > 1.f ? 1.f : u);
		if (u >= 1.f) {
			m_sumHit = 1;
			SummonImpactFx();
			Shake(at == 2 || at == 3 || at == 5 ? 1.05f : .8f);
			TacSfxSummon(2, m_sumFx);
			SummonLeave(dt);
		}
	} else {
		SummonLeave(dt);
	}
}

void Game::SummonLeave(float dt)
{
	(void)dt;
	float t = m_cutStageT;
	if (m_cutStage < 3) t -= SumStrikeSpan();
	if (t < 0.f) t = 0.f;
	float u = t / SumDepartSpan();
	if (u > 1.f) u = 1.f;
	SummonApply(2, u);
	SummonClip(m_sumAct, CL_IDLE, 1, 0.85f);
	SummonMedium(2, u);
}

void Game::SummonMedium(int phase, float u)
{
	float amp = SumAmp(phase, u);
	if (amp < 0.04f) return;
	int th = SumTheater(m_sumFx);
	V3 fwd = SumFwd(m_sumFrom, m_sumTo);
	V3 right = SumRight(fwd);
	V3 gate = m_sumFrom;
	float sz = 0.12f + amp * 0.22f;
	if (th == 0) {
		Burst(gate + v3(0, 0.15f + amp * 0.35f, 0), 2, 0.7f + amp, sz, 0.45f + amp * 0.2f, 1.f, 0.38f, 0.08f, 2, 1, -0.6f, 0.22f, 1.4f);
		if (phase >= 2) Burst(m_sumPos, 2, 0.5f, sz * 1.1f, 0.4f, 1.f, 0.45f, 0.1f, 2, 1, -0.3f, 0.15f, 1.1f);
	} else if (th == 1) {
		Burst(gate + v3(0, amp * 0.8f, 0), 2, 0.5f, sz, 0.6f, 0.75f, 0.9f, 1.f, 3, 1, -1.1f, 0.1f, 0.8f);
		Burst(gate + v3(sinf(m_time * 4.f) * 1.4f, 3.2f, cosf(m_time * 3.f) * 1.1f), 1, 0.2f, 0.06f, 0.7f, 0.85f, 0.95f, 1.f, 3, 1, 7.f, 0.4f, 0.f);
	} else if (th == 2) {
		Burst(gate + v3(0, 3.4f, 0), 1, 2.2f, 0.08f, 0.35f, 0.75f, 0.88f, 1.f, 3, 1, 6.f, 0.8f, 0.f);
		Burst(gate + v3(sinf(m_time * 5.f) * 2.2f, 3.6f, cosf(m_time * 4.f) * 1.6f), 2, 0.15f, 0.05f, 0.55f, 0.7f, 0.8f, 0.95f, 3, 1, 8.f, 0.35f, 0.f);
	} else if (th == 3 || th == 11) {
		for (int i = -2; i <= 2; i++) {
			V3 p = gate + right * (i * 0.42f * amp) + fwd * ((i & 1) ? 0.35f : -0.2f) * amp;
			Burst(p + v3(0, 0.05f, 0), 1, 0.8f, 0.1f + amp * 0.06f, 0.4f, 0.45f, 0.32f, 0.18f, 3, 0, 1.4f, 0.05f, 0.6f);
		}
	} else if (th == 4) {
		float ang = m_time * 3.2f;
		V3 p = gate + right * sinf(ang) * (0.6f + amp) + fwd * cosf(ang) * (0.6f + amp) + v3(0, 0.4f + amp * 0.5f, 0);
		Burst(p, 2, 1.4f, sz, 0.45f, 0.75f, 0.95f, 0.85f, 6, 1, -0.2f, 0.3f, 0.4f);
	} else if (th == 5) {
		float ride = phase <= 0 ? Smooth01(u / 0.92f) : (phase == 1 ? Smooth01((u - 0.38f) / 0.62f) : Smooth01((u - 0.2f) / 0.6f));
		V3 a = phase <= 0 ? (gate - fwd * 5.2f) : (phase == 1 ? gate : m_sumTo);
		V3 b = phase <= 0 ? gate : (phase == 1 ? m_sumTo : (gate - fwd * 4.f));
		V3 c = Lerp(a, b, ride);
		for (int i = -1; i <= 2; i++)
			Burst(c + right * (i * 0.7f) + v3(0, 0.25f + amp * 0.65f, 0), 1, 1.1f, 0.16f + amp * 0.1f, 0.4f, 0.2f, 0.5f, 1.f, 1, 1, 1.6f, 0.15f, 0.3f);
	} else if (th == 6) {
		Burst(gate + v3(0, amp * 1.1f, 0), 2, 0.6f, sz, 0.55f, 1.f, 0.92f, 0.55f, 2, 1, -1.4f, 0.1f, 1.2f);
	} else if (th == 7) {
		Burst(gate + v3(0, 0.2f, 0), 2, 0.5f, sz, 0.6f, 0.28f, 0.08f, 0.45f, 1, 1, 0.3f, 0.1f, 0.2f);
	} else if (th == 8) {
		Burst(gate + v3(0, 0.35f + amp * 0.4f, 0), 2, 0.35f, sz * 1.2f, 0.7f, 0.4f, 0.85f, 0.22f, 1, 1, -0.15f, 0.05f, 0.35f);
	} else if (th == 9) {
		float rad = 0.4f + amp * 1.3f;
		Burst(gate + right * sinf(m_time * 2.f) * rad + fwd * cosf(m_time * 2.f) * rad + v3(0, 0.12f, 0), 2, 1.6f, 0.12f, 0.4f, 0.85f, 0.7f, 0.35f, 2, 1, 0.8f, 0.1f, 0.2f);
	} else {
		Burst(gate + v3(0, 0.8f + amp * 0.4f, 0), 2, 0.3f, sz, 0.8f, 0.7f, 0.6f, 1.f, 6, 1, -0.25f, 0.05f, 0.3f);
	}
	if (phase == 1 && u > 0.2f) {
		V3 mouth = m_sumPos + v3(0, m_sumAct.height * 0.52f, 0);
		int nFoe = m_cutFoeN > 0 ? m_cutFoeN : 1;
		int gathering = u < 0.38f;
		for (int i = 0; i < nFoe; i++) {
			V3 foe = (m_cutFoeN > 0 ? m_cutFoePos[i] : m_sumTo) + v3(0, 0.45f, 0);
			float f = gathering ? 0.08f : (0.25f + 0.65f * fmodf(m_time * 1.6f + i * 0.37f, 1.f));
			V3 p = Lerp(mouth, foe, f);
			if (th == 0) Burst(p, 2, gathering ? 0.3f : 1.6f, gathering ? 0.1f : 0.2f, 0.35f, 1.f, 0.4f, 0.08f, 2, 1, -0.15f, 0.35f, 0.4f);
			else if (th == 1) Burst(p, 1, 1.2f, 0.12f, 0.4f, 0.8f, 0.92f, 1.f, 3, 1, -0.4f, 0.2f, 0.3f);
			else if (th == 2) Burst(p, 1, 2.4f, 0.07f, 0.25f, 0.8f, 0.9f, 1.f, 3, 1, 2.f, 0.6f, 0.f);
			else if (th == 5) Burst(foe, 1, 0.8f, 0.16f, 0.35f, 0.25f, 0.55f, 1.f, 1, 1, 0.4f, 0.1f, 0.2f);
			else if (th == 3) Burst(foe + v3(0, 0.05f, 0), 1, 1.f, 0.1f, 0.3f, 0.5f, 0.36f, 0.2f, 3, 0, -0.2f, 0.05f, 1.2f);
			else if (th == 8 || th == 10 || th == 11) Burst(p, 1, 0.5f, 0.14f, 0.5f, 0.55f, 0.8f, 0.45f, 1, 1, -0.1f, 0.05f, 0.2f);
			else if (!gathering) Burst(p, 1, 1.f, 0.1f, 0.3f, 1.f, 0.9f, 0.6f, 2, 1, 0.1f, 0.1f, 0.2f);
		}
	}
}

void Game::SummonImpactFx()
{
	if (m_seqSkill < 0) return;
	DWORD ec = ElemColor(SkillInfo(m_seqSkill).element);
	float r = (ec & 255) / 255.f;
	float g = ((ec >> 8) & 255) / 255.f;
	float b = ((ec >> 16) & 255) / 255.f;
	int fx = m_sumFx;
	TacSfxSummon(1, fx);
	int nFoe = m_cutFoeN > 0 ? m_cutFoeN : 1;
	for (int i = 0; i < nFoe; i++) {
		V3 p = (m_cutFoeN > 0 ? m_cutFoePos[i] : m_sumTo) + v3(0, .25f, 0);
		switch (fx) {
		case SUM_FX_FIRE:
			Burst(p, 52, 3.4f, .48f, 1.2f, r, g * .7f, b * .3f, 2, 1, -.35f, .25f, 2.1f, 1.05f);
			Burst(p, 16, 1.2f, .72f, .85f, 1.f, .45f, .12f, 1, 1, .15f, 0, .5f);
			break;
		case SUM_FX_ICE:
			Burst(p, 30, 1.1f, .18f, 1.15f, .72f, .9f, 1.f, 3, 1, -2.6f, 1.8f, 2.4f);
			Burst(p, 10, .25f, .55f, .9f, .82f, .94f, 1.f, 1, 1, 0, 0, 0);
			break;
		case SUM_FX_BOLT:
			Burst(p + v3(0, 2.4f, 0), 22, 6.f, .12f, .55f, .85f, .92f, 1.f, 3, 1, 4.5f, 2.2f, -1.f);
			Burst(p, 18, 2.2f, .22f, .6f, 1.f, 1.f, .7f, 2, 1, .2f, 0, .4f);
			break;
		case SUM_FX_SPIKE:
			Burst(p, 24, 1.6f, .22f, .9f, .55f, .42f, .28f, 3, 0, -1.8f, 1.4f, 1.6f);
			Burst(p, 14, 2.f, .16f, .7f, .4f, .32f, .22f, 2, 0, .8f, 0, .3f);
			break;
		case SUM_FX_WIND:
			Burst(p, 36, 4.2f, .14f, .7f, .75f, .95f, .85f, 6, 1, .1f, .4f, .2f);
			break;
		case SUM_FX_WAVE:
			Burst(p, 40, 2.4f, .28f, 1.f, .25f, .55f, 1.f, 1, 1, .35f, .15f, .8f, .7f);
			Burst(p + v3(0, .1f, 0), 12, 1.f, .5f, .8f, .4f, .7f, 1.f, 1, 1, 0, 0, .2f);
			break;
		case SUM_FX_HOLY:
			Burst(p, 26, .8f, .16f, 1.2f, 1.f, .92f, .55f, 3, 1, -2.2f, 1.5f, 2.f);
			Burst(p, 16, 1.4f, .2f, .8f, 1.f, 1.f, .75f, 2, 1, -.3f, 0, 1.2f);
			break;
		case SUM_FX_DARK:
			Burst(p + v3(0, 1.2f, 0), 34, 2.8f, .2f, 1.f, .35f, .12f, .55f, 2, 1, 1.2f, .3f, -.4f);
			Burst(p, 10, .4f, .6f, .9f, .15f, .05f, .2f, 1, 1, 0, 0, 0);
			break;
		case SUM_FX_MIASMA:
			Burst(p + v3(0, .4f, 0), 32, .7f, .34f, 1.3f, .45f, .85f, .25f, 1, 1, -.15f, 0, .35f, .8f);
			break;
		case SUM_FX_ROAR:
			Burst(p, 28, 3.6f, .18f, .75f, 1.f, .82f, .4f, 2, 1, .2f, 0, .6f);
			Burst(m_sumPos + v3(0, m_sumAct.height * .6f, 0), 16, 2.2f, .16f, .6f, 1.f, .7f, .3f, 1, 1, 0, 0, .2f);
			break;
		case SUM_FX_SLEEP:
			Burst(p + v3(0, 1.1f, 0), 22, .6f, .16f, 1.4f, .85f, .75f, 1.f, 6, 1, -.25f, 0, .15f);
			break;
		default:
			Burst(p, 20, 1.8f, .12f, .8f, .8f, .8f, .85f, 3, 1, .4f, .8f, .1f);
			break;
		}
	}
}

void Game::DrawSummonGate()
{
	if (!m_sumOn || m_cutStage >= 4) return;
	int leaving = m_sumHit && (m_cutStage >= 3 || m_cutStageT >= SumStrikeSpan());
	int phase = leaving ? 2 : (m_cutStage == 2 ? 1 : 0);
	float u = 0.f;
	if (phase == 0) u = m_cutStageT / max(0.2f, CutIntroLen());
	else if (phase == 1) u = m_cutStageT / SumStrikeSpan();
	else {
		float t = m_cutStage >= 3 ? m_cutStageT : (m_cutStageT - SumStrikeSpan());
		if (t < 0.f) t = 0.f;
		u = t / SumDepartSpan();
	}
	if (u < 0.f) u = 0.f;
	if (u > 1.f) u = 1.f;
	float amp = SumAmp(phase, u);
	if (amp < 0.03f) return;
	int th = SumTheater(m_sumFx);
	V3 fwd = SumFwd(m_sumFrom, m_sumTo);
	float yaw = atan2f(fwd.x, fwd.z);
	V3 at = m_sumFrom + v3(0, 0.25f, 0);
	float sx = 0.55f * amp, sy = 0.7f * amp, sz = 0.55f * amp;
	DWORD tint = RGBA8(255, 140, 40);
	if (phase >= 2 && th == 0) {
		float face = 0, dis = 0;
		int hide = 0;
		SumPose(th, SumAtOf(m_seqSkill), SumIndexOf(m_seqSkill), 2, 1.f, m_sumFrom, m_sumTo, at, face, dis, hide);
	}
	switch (th) {
	case 0:
		sx = sz = 0.62f * amp;
		sy = 0.85f * amp;
		if (phase < 2) at = m_sumFrom + v3(0, 0.2f + amp * 0.25f, 0);
		tint = RGBA8(255, 110, 24);
		break;
	case 1:
		sx = sz = 0.28f * amp;
		sy = 1.55f * amp;
		at = m_sumFrom + v3(0, sy * 0.45f, 0);
		tint = RGBA8(186, 224, 255);
		break;
	case 2:
		sx = sz = 0.2f * amp;
		sy = 2.3f * amp;
		at = m_sumFrom + v3(0, 1.5f, 0);
		tint = RGBA8(170, 205, 255);
		break;
	case 3:
	case 11:
		sx = 1.7f * amp;
		sy = 0.045f;
		sz = 0.26f * amp;
		at = v3((phase >= 2 ? m_sumPos.x : m_sumFrom.x), 0.04f, (phase >= 2 ? m_sumPos.z : m_sumFrom.z));
		tint = RGBA8(42, 30, 18);
		break;
	case 4:
		sx = sy = sz = 0.75f * amp;
		at = m_sumPos + v3(0, 0.55f, 0);
		tint = RGBA8(190, 235, 215);
		break;
	case 5: {
		float ride = phase <= 0 ? Smooth01(u / 0.92f) : (phase == 1 ? Smooth01((u - 0.38f) / 0.62f) : Smooth01((u - 0.2f) / 0.6f));
		V3 a = phase <= 0 ? (m_sumFrom - fwd * 5.2f) : (phase == 1 ? m_sumFrom : m_sumTo);
		V3 b = phase <= 0 ? m_sumFrom : (phase == 1 ? m_sumTo : (m_sumFrom - fwd * 4.f));
		at = Lerp(a, b, ride) + v3(0, 0.3f * amp, 0);
		sx = 2.5f * amp;
		sy = 0.75f * amp;
		sz = 0.42f * amp;
		tint = RGBA8(36, 108, 230);
		break;
	}
	case 6:
		sx = sz = 0.38f * amp;
		sy = 2.1f * amp;
		at = m_sumFrom + v3(0, 1.15f, 0);
		tint = RGBA8(255, 228, 150);
		break;
	case 7:
		sx = sz = 1.05f * amp;
		sy = 0.16f * amp;
		at = m_sumFrom + v3(0, 0.08f, 0);
		tint = RGBA8(28, 10, 48);
		break;
	case 8:
		sx = sy = sz = 0.85f * amp;
		at = m_sumFrom + v3(0, 0.45f, 0);
		tint = RGBA8(78, 168, 48);
		break;
	case 9:
		sx = sz = 1.35f * amp;
		sy = 0.07f;
		at = m_sumFrom + v3(0, 0.05f, 0);
		tint = RGBA8(150, 118, 72);
		break;
	default:
		sx = sy = sz = 0.6f * amp;
		at = m_sumFrom + v3(0, 0.85f, 0);
		tint = RGBA8(168, 146, 235);
		break;
	}
	DrawParams sig = {};
	sig.emissive = 1.f;
	sig.flash = .8f;
	sig.rim = 1.f;
	sig.tint = tint;
	M4 w = MMul(MMul(MScale(sx, sy, sz), MRotY(yaw)), MTrans(at.x, at.y, at.z));
	m_r.DrawStatic(m_lib.orb, w, sig);
}

void Game::DrawCutIn3D()
{
	m_r.SetTheme(m_b.map.theme);
	m_r.SetStageId(m_b.stage);
	Env e;
	ThemeEnv(m_b.map.theme, e);
	DWORD ec = m_seqSkill >= 0 ? ElemColor(SkillInfo(m_seqSkill).element) : RGBA8(255, 240, 200);
	V3 look, eye;
	float t = m_time;
	auto centerOf = [&](const V3* p, int n) {
		V3 c = v3(0, .7f, 0);
		if (n <= 0) return c;
		c = v3(0, 0, 0);
		for (int i = 0; i < n; i++) c = c + p[i];
		c = c * (1.f / n);
		c.y = .72f;
		return c;
	};
	if (m_cutKind == 1) {
		V3 a = m_cutPosA;
		float h = m_cutA >= 0 ? m_act[m_cutA].height : 1.f;
		if (h > 1.32f && !(m_cutA >= 0 && m_act[m_cutA].proc == 3)) h = 1.32f;
		look = a + v3(0, h * .55f, 0);
		if (m_cutStage == 1) {
			float u = Smooth01(m_cutStageT / max(.4f, CutIntroLen()));
			float ang = t * (IsCraftSuper(m_seqSkill) ? 1.7f : 1.15f);
			float rad = Lerpf(2.7f, 1.25f, u);
			float elev = Lerpf(.15f, 1.45f, u);
			eye = look + v3(cosf(ang) * rad, elev, sinf(ang) * rad);
		} else if (m_cutStage == 2 || m_cutStage == 3) {
			float u = m_cutStage == 2 ? m_cutStageT : m_cutStageT + .8f;
			float ang = -0.9f - u * 1.35f;
			float dist = Lerpf(4.4f, 2.35f, Smooth01(min(1.f, u)));
			look = Lerp(a + v3(0, h * .5f, 0), v3(.4f, .8f, 0), Smooth01(min(1.f, u * .7f)));
			eye = v3(sinf(ang) * dist, Lerpf(2.4f, .85f, Smooth01(min(1.f, u))), -cosf(ang) * dist);
			if (m_shake > .3f) eye = Lerp(eye, look, .16f);
		} else {
			look = a + v3(0, h * .4f, 0);
			eye = look + v3(1.8f, .35f, -1.4f);
		}
	} else if (m_cutKind == 2) {
		look = centerOf(m_cutAllyPos, m_cutAllyN);
		if (m_cutFoeN > 0) look = Lerp(look, centerOf(m_cutFoePos, m_cutFoeN), .28f);
		if (m_cutStage == 1) {
			float u = Smooth01(m_cutStageT / max(.4f, CutIntroLen()));
			eye = look + v3(Lerpf(.15f, 2.5f, u), Lerpf(6.2f, 1.7f, u), Lerpf(-.15f, -2.6f, u));
		} else if (m_cutStage == 2 || m_cutStage == 3) {
			float u = m_cutStage == 2 ? m_cutStageT : m_cutStageT + 1.f;
			float ang = -0.2f - u * .7f;
			int spread = max(m_cutAllyN, m_cutFoeN);
			float dist = 4.1f + max(0, spread - 1) * .4f;
			dist = Lerpf(dist + .8f, dist, Smooth01(min(1.f, u)));
			eye = v3(sinf(ang) * dist, 2.05f + sinf(u) * .08f, -cosf(ang) * dist);
			if (m_shake > .3f) eye = Lerp(eye, look, .08f);
		} else {
			eye = look + v3(.4f, 5.4f, -1.2f);
		}
	} else if (m_cutKind == 3) {
		int leaving = m_cutStage >= 3 || (m_sumHit && m_cutStageT >= SumStrikeSpan());
		int th = SumTheater(m_sumFx);
		V3 beast = m_sumOn ? m_sumPos : m_cutPosA;
		float bh = m_sumOn ? m_sumAct.height : .8f;
		look = beast + v3(0, min(bh * .42f, 1.1f), 0);
		V3 foeC = (m_cutFoeN > 0 ? centerOf(m_cutFoePos, m_cutFoeN) : m_sumTo) + v3(0, .4f, 0);
		V3 fwd = m_sumOn ? SumFwd(m_sumFrom, m_sumTo) : v3(0, 0, 1);
		if (m_cutStage == 1 && m_sumU < 0.62f) {
			V3 gate = (th == 5) ? (m_sumFrom - fwd * 2.6f) : m_sumFrom;
			look = Lerp(gate + v3(0, 0.65f, 0), look, Smooth01((m_sumU - 0.4f) / 0.22f));
		} else if (!leaving && m_cutFoeN > 0)
			look = Lerp(look, foeC, m_sumHit ? .3f : .14f);
		float ang = atan2f(fwd.x, fwd.z);
		float high = (th == 0 || th == 2 || th == 4 || th == 6) ? 1.7f : 1.2f;
		if (leaving) {
			float t = m_cutStage >= 3 ? m_cutStageT : (m_cutStageT - SumStrikeSpan());
			if (t < 0.f) t = 0.f;
			float u = t / max(0.2f, SummonDepartNeed());
			if (u > 1.f) u = 1.f;
			V3 who = m_sumFrom + v3(0, 0.95f, 0);
			look = Lerp(look, who, Smooth01(min(1.f, u * 1.6f)));
			eye = who + v3(sinf(ang + 0.9f) * 3.2f, 1.45f, cosf(ang + 0.9f) * 3.2f);
		} else if (m_cutStage == 1) {
			float u = m_cutStageT / max(.4f, CutIntroLen());
			eye = look + v3(Lerpf(-2.6f, 1.9f, u), Lerpf(high + 0.8f, high, u), Lerpf(2.8f, -2.5f, u));
		} else if (m_cutStage == 2) {
			float spin = m_cutStageT * (th == 0 || th == 2 ? 0.7f : 0.4f);
			float dist = Lerpf(5.4f, 2.5f, Smooth01(min(1.f, m_cutStageT * 0.8f)));
			eye = look + v3(sinf(ang + 0.35f + spin) * dist, high + 0.45f, cosf(ang + 0.35f + spin) * dist);
			if (m_sumHit && m_cutFoeN > 0) {
				look = Lerp(look, foeC, 0.62f);
				eye = Lerp(eye, foeC + v3(0, 1.15f, 2.1f), 0.4f);
			}
			if (m_shake > .3f) eye = Lerp(eye, look, .12f);
		} else {
			eye = look + v3(2.2f, 1.15f, -2.4f);
		}
	} else if (m_cutStage == 1 && m_seqSkill >= 0) {
		float u = Smooth01(m_cutStageT / max(.4f, CutIntroLen()));
		V3 a = m_cutPosA;
		float h = m_act[m_cutA].height;
		if (h > 1.32f && m_act[m_cutA].proc != 3) h = 1.32f;
		look = a + v3(0, h * .6f, 0);
		int pat = m_cutPat;
		if (pat == 1) eye = look + v3(Lerpf(2.2f, 1.4f, u), Lerpf(-.05f, .45f, u), Lerpf(-1.6f, -.7f, u));
		else if (pat == 2) {
			float ang = u * 2.2f;
			eye = look + v3(cosf(ang) * 1.7f, .35f, sinf(ang) * 1.7f);
		} else if (pat == 3) eye = look + v3(Lerpf(.4f, .15f, u), Lerpf(.55f, .35f, u), Lerpf(-2.1f, -1.4f, u));
		else if (pat == 4) {
			eye = look + v3(Lerpf(-.2f, .3f, u), Lerpf(.4f, .55f, u), Lerpf(-2.0f, -1.5f, u));
			look = Lerp(look, m_cutPosD + v3(0, .5f, 0), u * .35f);
		} else if (pat == 5) eye = look + v3(Lerpf(2.4f, 1.2f, u), Lerpf(.05f, .85f, u), Lerpf(-.4f, -1.6f, u));
		else if (pat == 6) eye = look + v3(Lerpf(.2f, 1.5f, u), Lerpf(2.4f, 1.1f, u), Lerpf(-1.8f, -1.2f, u));
		else if (pat == 7) eye = look + v3(Lerpf(-1.8f, 1.1f, u), Lerpf(1.6f, .7f, u), Lerpf(-.5f, -1.4f, u));
		else if (pat == 8) eye = look + v3(Lerpf(1.2f, .8f, u), Lerpf(-.15f, .25f, u), Lerpf(1.6f, -.9f, u));
		else if (pat == 9) eye = look + v3(Lerpf(-2.2f, 1.8f, u), Lerpf(.9f, .45f, u), Lerpf(-.8f, -1.5f, u));
		else if (pat == 10) eye = look + v3(Lerpf(.9f, .55f, u), Lerpf(.35f, .42f, u), Lerpf(-1.1f, -.75f, u));
		else if (pat == 11) {
			look = v3(0, .8f, 0);
			eye = look + v3(Lerpf(.3f, 1.6f, u), Lerpf(3.2f, 1.8f, u), Lerpf(-.4f, -2.2f, u));
		} else if (pat == 12) {
			float ang = -0.4f + u * 1.1f;
			eye = look + v3(cosf(ang) * 1.9f, .55f + u * .3f, sinf(ang) * 1.9f);
		} else if (pat == 13) eye = look + v3(Lerpf(-1.4f, 1.6f, u), Lerpf(.15f, .7f, u), Lerpf(-1.8f, -1.1f, u));
		else eye = look + v3(Lerpf(1.6f, 1.1f, u), Lerpf(.1f, .25f, u), Lerpf(-1.2f, -.9f, u));
	} else if (m_cutStage == 2 || m_cutStage == 3) {
		float u = m_cutStage == 2 ? m_cutStageT : m_cutStageT + 1.1f;
		int spread = max(m_cutAllyN, m_cutFoeN);
		look = v3(0, .72f, 0);
		float extra = max(0, spread - 1) * .55f;
		if (m_cutFoeN > 6) {
			int cols = m_cutFoeN > 12 ? 6 : 4;
			int rows = (m_cutFoeN + cols - 1) / cols;
			extra = 2.8f + rows * .5f;
		}
		float dist = (m_cutFoeN <= 0 ? 2.6f : 3.35f) + extra;
		dist = Lerpf(dist + .45f, dist, Smooth01(min(1.f, u * 1.4f)));
		float ang = -0.35f - u * 0.85f;
		float elev = 1.15f + sinf(u * 1.7f) * .06f;
		int pat = m_cutPat;
		float h = (m_cutA >= 0) ? m_act[m_cutA].height : 1.f;
		if (h > 1.32f && !(m_cutA >= 0 && m_act[m_cutA].proc == 3)) h = 1.32f;
		if (pat == 1) { ang = -0.55f - u * 0.55f; elev = 0.85f; dist *= 1.08f; }
		else if (pat == 2) { ang = 0.4f + u * 1.4f; elev = 1.35f; }
		else if (pat == 3) { ang = -0.15f; elev = 0.55f; dist = Lerpf(2.4f, 1.85f, Smooth01(min(1.f, u))); look = m_cutPosD + v3(0, .5f, 0); }
		else if (pat == 4) { ang = 1.15f; elev = 0.95f; look = Lerp(m_cutPosA, m_cutPosD, .45f) + v3(0, .6f, 0); }
		else if (pat == 5) { ang = -0.9f - u * .4f; elev = 0.55f; dist *= 1.15f; }
		else if (pat == 6) { ang = -0.2f; elev = 2.1f; dist *= 1.1f; }
		else if (pat == 7) { ang = -1.4f + u * 0.3f; elev = 1.55f; }
		else if (pat == 8) { ang = 0.8f + u * 0.5f; elev = 0.35f; }
		else if (pat == 9) { ang = -0.35f - u * 1.6f; elev = 1.25f; dist *= 1.2f; }
		else if (pat == 10) { ang = -0.25f; elev = 0.7f; dist *= 0.78f; look = m_cutPosD + v3(0, .65f, 0); }
		else if (pat == 11) { ang = -0.4f - u * .3f; elev = 2.4f; dist *= 1.25f; }
		else if (pat == 12) { ang = t * .35f; elev = 1.1f; }
		else if (pat == 13) { ang = -0.7f - u * 0.5f; elev = 0.95f; look = m_cutPosA + v3(0, h * .5f, 0); }
		eye = v3(sinf(ang) * dist, elev, -cosf(ang) * dist);
		if (m_shake > .3f) eye = Lerp(eye, look, .1f);
	} else {
		int spread = max(m_cutAllyN, m_cutFoeN);
		look = v3(0, .55f, 0);
		float sw = sinf(t * .4f) * .35f;
		float extra = max(0, spread - 1) * .75f;
		if (m_cutFoeN > 6) {
			int cols = m_cutFoeN > 12 ? 6 : 4;
			int rows = (m_cutFoeN + cols - 1) / cols;
			extra = 3.4f + rows * .55f;
		}
		float dist = (m_cutFoeN <= 0 ? 2.8f : 3.8f) + extra;
		float yaw = .3f + sw + (m_cutPat % 5) * .12f;
		float elev = 1.05f + (m_cutPat == 6 || m_cutPat == 11 ? .55f : 0.f);
		eye = v3(yaw, elev, -dist);
		if (m_shake > .3f) { eye = Lerp(eye, look, .12f); }
	}
	if (m_shake > 0) look = look + v3(m_rng.F(-1, 1), m_rng.F(-1, 1), 0) * (m_shake * .03f);
	// 飛行は足元が浮く。カメラは地上の立ち位置のままなので、その分だけ頭が上に切れる。
	{
		float lift = 0.f;
		auto take = [&](int id) {
			if (id < 0 || id >= m_b.nUnits) return;
			float h = m_b.u[id].hover;
			if (h > lift) lift = h;
		};
		for (int i = 0; i < m_cutAllyN; i++) take(m_cutAlly[i]);
		for (int i = 0; i < m_cutFoeN; i++) take(m_cutFoe[i]);
		take(m_cutA);
		if (m_cutD >= 0) take(m_cutD);
		if (lift > 1.2f) lift = 1.2f;
		look.y += lift;
		eye.y += lift;
	}
	M4 view = MLookAtLH(eye, look, v3(0, 1, 0));
	M4 proj = MPerspLH(.75f, (float)m_w / max(1, m_h), .05f, 160.f);
	m_r.SetCamera(view, proj, eye, m_time);
	// 敵が立つ +X へ、マップ上の相手の方向を回す。床は戦闘地点の高さ。
	M4 scene = CutSceneXform();
	m_r.SetSceneShift(scene);
	float sightXz[32];
	int sightN = 0;
	auto addSight = [&](int id) {
		if (id < 0 || id >= m_b.nUnits || sightN >= 16) return;
		V3 sp = ActorPos(id);
		sightXz[sightN * 2] = sp.x;
		sightXz[sightN * 2 + 1] = sp.z;
		sightN++;
	};
	for (int i = 0; i < m_cutAllyN; i++) addSight(m_cutAlly[i]);
	for (int i = 0; i < m_cutFoeN; i++) addSight(m_cutFoe[i]);
	if (m_sumOn && sightN < 16) {
		sightXz[sightN * 2] = m_sumPos.x;
		sightXz[sightN * 2 + 1] = m_sumPos.z;
		sightN++;
	}
	if (sightN <= 0) { sightXz[0] = 0.f; sightXz[1] = 0.f; sightN = 1; }
	m_r.SetSight(sightXz, sightN, (float)TacSightTiles(m_b.weather));
	int fadeAbove = -1, fadeX = -1, fadeZ = -1;
	if (m_cutA >= 0 && m_cutA < m_b.nUnits && Map::In(m_b.u[m_cutA].x, m_b.u[m_cutA].z)) {
		const Unit& au = m_b.u[m_cutA];
		if ((int)m_b.map.At(au.x, au.z).nLay > au.deck + 1) { fadeAbove = au.deck; fadeX = au.x; fadeZ = au.z; }
	}
	m_r.SetFadeAbove(fadeAbove, fadeX, fadeZ);
	float fogNear = 12.f, fogFar = 52.f;
	if (max(m_cutAllyN, m_cutFoeN) > 6) { fogNear = 16.f; fogFar = 78.f; }
	m_r.SetFog(fogNear, fogFar, 0.18f, e.fog);
	float amb = e.amb;
	int hr = TacHour(m_b.weather);
	if (hr == HR_NIGHT || hr == HR_MOON || hr == HR_STAR) amb *= 0.92f;
	else if (hr == HR_DEEP) amb *= 0.88f;
	else if (hr == HR_ECLIPSE) amb *= 0.94f;
	else if (hr == HR_DUSK || hr == HR_DAWN) amb *= 0.97f;
	m_r.SetAmbient(e.ambSky, e.ambGnd, amb, e.hard);
	m_r.SetLight(e.light, look, 16.f);
	{
		// 戦闘している層以外の床は描かない。視線の手前は、焦点のキャラより前だけ抜く。
		int deck = 0;
		if (m_cutA >= 0 && m_cutA < m_b.nUnits) deck = m_b.u[m_cutA].deck;
		if (deck < 0) deck = 0;
		if (deck > 4) deck = 4;
		m_r.SetCutDeck(deck);
		V3 fwd = Norm(look - eye);
		if (Len(fwd) < 0.01f) fwd = v3(0, 0, 1);
		float nearSub = Dot(look - eye, fwd);
		auto takeId = [&](int id) {
			if (id < 0 || id >= m_b.nUnits) return;
			V3 p = ActorPos(id);
			float h = m_act[id].height;
			if (h < 0.4f) h = 0.9f;
			if (m_act[id].proc != 3 && h > 1.32f) h = 1.32f;
			float d = Dot(p + v3(0, h * 0.45f, 0) - eye, fwd);
			if (d > 0.35f && d < nearSub) nearSub = d;
		};
		takeId(m_cutA);
		takeId(m_cutD);
		if (m_sumOn && !m_sumHide) {
			float h = m_sumAct.height > 0.4f ? m_sumAct.height : 1.2f;
			float d = Dot(m_sumPos + v3(0, h * 0.4f, 0) - eye, fwd);
			if (d > 0.35f && d < nearSub) nearSub = d;
		}
		if (nearSub < 0.6f) nearSub = Len(look - eye);
		// 体と手元の武器より手前の床・壁だけ抜く。境界を体の奥に置くと胸が穴になる。
		float solid = nearSub - 0.85f;
		float hide = solid - 1.25f;
		if (hide < 0.06f) hide = 0.06f;
		if (solid < hide + 0.35f) solid = hide + 0.35f;
		m_r.SetCamNear(hide, solid, 1.f);
		m_r.SetCamFwd(fwd.x, fwd.y, fwd.z);
	}
	int ids[CUT_ALLY_MAX + CUT_FOE_MAX];
	int nId = 0;
	int idCap = CUT_ALLY_MAX + CUT_FOE_MAX;
	for (int i = 0; i < m_cutAllyN && nId < idCap; i++) ids[nId++] = m_cutAlly[i];
	for (int i = 0; i < m_cutFoeN && nId < idCap; i++) ids[nId++] = m_cutFoe[i];
	if (nId == 0 && m_cutA >= 0) ids[nId++] = m_cutA;
	auto cutWorld = [&](int id) {
		const Actor& ac = m_act[id];
		V3 p = ActorPos(id);
		float face = ActorFace(id);
		// 人は戦闘・作成と同じ倍率。巨体だけ枠に収める。
		if (ac.proc == 3) return ActorWorld(ac, p, face);
		float sc = ac.scale;
		if (ac.height > 1.32f) sc *= 1.32f / ac.height;
		return MMul(MMul(MScale(sc, sc, sc), MRotY(face + PI)), MTrans(p.x, p.y, p.z));
	};
	for (int k = 0; k < nId; k++) if (ids[k] >= 0) ActorPose(m_act[ids[k]], m_lib, m_r);
	if (m_sumOn) ActorPose(m_sumAct, m_lib, m_r);
	auto sumWorld = [&]() {
		return ActorWorld(m_sumAct, m_sumPos, m_sumYaw);
	};
	m_r.SetCutActors(TRUE);
	m_r.BeginShadowPass();
	m_r.DrawTerrain();
	m_r.ResetActorPipe();
	m_r.BeginInstBatch();
	for (int k = 0; k < nId; k++) {
		if (ids[k] < 0) continue;
		DrawParams dp;
		ActorDraw(m_r, m_lib, m_act[ids[k]], cutWorld(ids[k]), dp, m_time);
	}
	if (m_sumOn && !m_sumHide) {
		DrawParams dp;
		dp.dissolve = m_sumFade;
		ActorDraw(m_r, m_lib, m_sumAct, sumWorld(), dp, m_time);
	}
	m_r.EndInstBatch();
	if (m_r.BeginReflectPass(0.f, look)) {
		m_r.DrawTerrain();
		m_r.DrawSky(e.skyTop, e.skyBot, 0, 0);
		m_r.DrawWater();
		m_r.ResetActorPipe();
		m_r.BeginInstBatch();
		for (int k = 0; k < nId; k++) {
			if (ids[k] < 0) continue;
			DrawParams dp;
			dp.rim = .55f;
			dp.dissolve = m_b.u[ids[k]].dissolve;
			ActorDraw(m_r, m_lib, m_act[ids[k]], cutWorld(ids[k]), dp, m_time);
		}
		if (m_sumOn && !m_sumHide) {
			DrawParams dp;
			dp.rim = .7f;
			dp.dissolve = m_sumFade;
			ActorDraw(m_r, m_lib, m_sumAct, sumWorld(), dp, m_time);
		}
		m_r.EndInstBatch();
		m_r.EndReflectPass();
	}
	m_r.BeginMainPass();
	{
		float rip[TAC_RIPN * 4];
		float ax = 0.f, az = 0.f;
		int an = 0;
		auto addRip = [&](int id) {
			if (id < 0 || id >= m_b.nUnits) return;
			ax += m_b.u[id].px;
			az += m_b.u[id].pz;
			an++;
		};
		addRip(m_cutA);
		if (m_cutD >= 0) addRip(m_cutD);
		if (an > 0) { ax /= (float)an; az /= (float)an; }
		float c = scene.m[0], s = scene.m[8];
		for (int i = 0; i < TAC_RIPN; i++) {
			float rx = m_rip[i][0] - ax, rz = m_rip[i][2] - az;
			rip[i * 4] = rx * c + rz * s;
			rip[i * 4 + 1] = m_rip[i][1];
			rip[i * 4 + 2] = -rx * s + rz * c;
			rip[i * 4 + 3] = m_rip[i][3];
		}
		m_r.SetLiquidRipples(rip, TAC_RIPN);
	}
	m_r.DrawTerrain();
	m_r.DrawSky(e.skyTop, e.skyBot, 0, 0);
	m_r.DrawWater();
	m_r.ResetActorPipe();
	m_r.BeginInstBatch();
	for (int k = 0; k < nId; k++) {
		if (ids[k] < 0) continue;
		const Unit& u = m_b.u[ids[k]];
		DrawParams dp;
		dp.outline = .013f;
		dp.flash = u.flash;
		dp.dissolve = u.dissolve;
		dp.rim = .55f;
		ActorDraw(m_r, m_lib, m_act[ids[k]], cutWorld(ids[k]), dp, m_time);
	}
	if (m_sumOn && !m_sumHide) {
		DrawParams dp;
		dp.outline = .016f;
		dp.rim = .7f;
		dp.dissolve = m_sumFade;
		ActorDraw(m_r, m_lib, m_sumAct, sumWorld(), dp, m_time);
	}
	m_r.EndInstBatch();
	if (m_sumOn) DrawSummonGate();
	if (m_sumOn && m_sumHit && (m_cutStageT - SumStrikeSpan()) < 0.55f && (m_sumFx == SUM_FX_ICE || m_sumFx == SUM_FX_HOLY)) {
		DrawParams sig = {};
		sig.emissive = 1.f;
		sig.flash = .9f;
		sig.rim = 1.f;
		sig.tint = m_sumFx == SUM_FX_ICE ? RGBA8(170, 220, 255) : RGBA8(255, 230, 150);
		int nCol = m_cutFoeN > 0 ? m_cutFoeN : 1;
		for (int i = 0; i < nCol; i++) {
			V3 p = m_cutFoeN > 0 ? m_cutFoePos[i] : m_sumTo;
			M4 w = MMul(MScale(.38f, 2.35f, .38f), MTrans(p.x, 1.15f, p.z));
			m_r.DrawStatic(m_lib.orb, w, sig);
		}
	}
	if (m_cutKind == 1 && m_cutA >= 0) {
		BOOL ougi = IsCraftSuper(m_seqSkill);
		float spin = m_time * (ougi ? 1.6f : 1.05f);
		float rad = ougi ? 1.35f : 1.05f;
		int nOrb = ougi ? 8 : 5;
		DrawParams sig = {};
		sig.emissive = 1.f;
		sig.flash = .85f;
		sig.rim = 1.f;
		sig.tint = ec;
		M4 disc = MMul(MScale(rad * 1.15f, .02f, rad * .82f), MTrans(m_cutPosA.x, .04f, m_cutPosA.z));
		m_r.DrawStatic(m_lib.orb, disc, sig);
		for (int i = 0; i < nOrb; i++) {
			float a = spin + i * 6.28318f / nOrb;
			float y = .35f + sinf(m_time * 2.f + i) * .18f + (i & 1) * .25f;
			V3 p = m_cutPosA + v3(cosf(a) * rad, y, sinf(a) * rad * .72f);
			float s = (i & 1) ? .11f : .07f;
			M4 w = MMul(MScale(s, s, s), MTrans(p.x, p.y, p.z));
			sig.tint = (i & 1) ? RGBA8(255, 220, 120) : ec;
			m_r.DrawStatic(m_lib.orb, w, sig);
		}
	} else if (m_cutKind == 2 && m_cutAllyN > 0) {
		DrawParams sig = {};
		sig.emissive = 1.f;
		sig.flash = .7f;
		sig.rim = 1.f;
		sig.tint = RGBA8(120, 190, 255);
		V3 home = m_cutAllyPos[0];
		M4 disc = MMul(MScale(1.6f + m_cutAllyN * .15f, .018f, 1.6f + m_cutAllyN * .15f), MTrans(home.x, .035f, home.z));
		m_r.DrawStatic(m_lib.orb, disc, sig);
		for (int i = 0; i < m_cutAllyN; i++) {
			V3 p = m_cutAllyPos[i];
			float s = i == 0 ? .16f : .1f;
			M4 w = MMul(MScale(s, s * .35f, s), MTrans(p.x, .06f, p.z));
			sig.tint = i == 0 ? RGBA8(255, 214, 120) : RGBA8(90, 180, 255);
			m_r.DrawStatic(m_lib.orb, w, sig);
		}
	}
	m_r.DrawParticles(m_parts.data(), (int)m_parts.size(), &scene);
	m_r.DrawWxGpu();
	m_r.SetCutActors(FALSE);
	m_r.SetSceneShift(MIdent());
	m_r.SetCutDeck(-1);
}

// 職歴の行数に合わせたカードの高さ。room を超えない。戻り値の高さと行数は描画側と同じ式。
static int WrapHeldLines(Renderer& r, const wchar_t* src, float maxW, float px, wchar_t lines[][120], int maxLines);
static float FitCardH(Renderer& r, const Unit* u, float w, BOOL big, float portrait, float textPad, float base, float linePx, float room, int* outLines)
{
	float tw = w - portrait - textPad;
	if (tw < 48.f) tw = 48.f;
	float lineH = linePx + 4.f;
	int maxLines = big ? 6 : 5;
	if (outLines) *outLines = 0;
	if (!u) return room > 0.f && base > room ? room : base;
	if (u->team != TEAM_PLAYER) {
		wchar_t held[480];
		held[0] = 0;
		UnitHeldText(*u, held, 480);
		wchar_t sk[8][120];
		int nSk = WrapHeldLines(r, held, tw, linePx, sk, 8);
		float need = base;
		if (nSk > 0) need += 8.f + nSk * (linePx + 6.f);
		float minH = portrait + 24.f;
		if (need < minH) need = minH;
		if (room > 0.f && need > room) need = room;
		return need;
	}
	for (int lines = maxLines; lines >= 0; --lines) {
		wchar_t buf[8][120];
		int n = lines > 0 ? SplitJobPath(r, u->ch, tw, linePx, lines, buf) : 0;
		float need = base + n * lineH + 10.f;
		if (need <= room + 0.5f || lines == 0) {
			if (outLines) *outLines = n;
			if (room > 0.f && need > room) need = room;
			return need;
		}
	}
	return room > 0.f && base > room ? room : base;
}

// 区切り（" · " と技能欄の空き）で折り返す。1行の高さは px に少し足す。
static int WrapHeldLines(Renderer& r, const wchar_t* src, float maxW, float px, wchar_t lines[][120], int maxLines)
{
	if (!src || !src[0] || maxLines < 1 || maxW < 8.f) return 0;
	int nLine = 0;
	int sepBefore = 0;
	BOOL more = FALSE;
	wchar_t cur[120];
	cur[0] = 0;
	const wchar_t* p = src;
	while (*p && nLine < maxLines) {
		const wchar_t* dot = wcsstr(p, L" · ");
		const wchar_t* gap = wcsstr(p, L"    ");
		const wchar_t* br = NULL;
		int seplen = 0;
		if (dot && (!gap || dot <= gap)) { br = dot; seplen = 3; }
		else if (gap) { br = gap; seplen = 4; }
		size_t n = br ? (size_t)(br - p) : wcslen(p);
		wchar_t tok[120];
		if (n > 118) n = 118;
		memcpy(tok, p, n * sizeof(wchar_t));
		tok[n] = 0;
		wchar_t trial[120];
		if (!cur[0]) wcscpy_s(trial, tok);
		else swprintf_s(trial, L"%s%s%s", cur, sepBefore == 4 ? L"  " : L" · ", tok);
		if (cur[0] && r.TextWidth(trial, px, 0) > maxW) {
			wcscpy_s(lines[nLine++], cur);
			if (nLine >= maxLines) { cur[0] = 0; more = TRUE; break; }
			wcscpy_s(cur, tok);
		} else wcscpy_s(cur, trial);
		if (!br) { p = L""; break; }
		sepBefore = seplen;
		p = br + seplen;
	}
	if (cur[0] && nLine < maxLines) wcscpy_s(lines[nLine++], cur);
	else if (nLine > 0 && (cur[0] || more || (p && *p))) {
		size_t len = wcslen(lines[nLine - 1]);
		if (len + 1 < 119) wcscat_s(lines[nLine - 1], L"…");
	}
	return nLine;
}

// 空白・改行でのみ折る。単語の途中では切らない。入り切らなければ末尾に…。
static int WrapSpaceLines(Renderer& r, const wchar_t* src, float maxW, float px, wchar_t lines[][180], int maxLines)
{
	if (!src || !src[0] || maxLines < 1) return 0;
	int nLine = 0;
	int dropped = 0;
	wchar_t cur[180];
	cur[0] = 0;
	const wchar_t* p = src;
	while (*p && nLine < maxLines) {
		while (*p == L' ' || *p == L'\t' || *p == 0x3000 || *p == L'\r') p++;
		if (!*p) break;
		if (*p == L'\n') {
			if (cur[0]) {
				wcscpy_s(lines[nLine++], cur);
				cur[0] = 0;
			}
			p++;
			continue;
		}
		const wchar_t* e = p;
		while (*e && *e != L' ' && *e != L'\t' && *e != 0x3000 && *e != L'\n' && *e != L'\r') e++;
		size_t n = (size_t)(e - p);
		if (n > 178) n = 178;
		wchar_t tok[180];
		memcpy(tok, p, n * sizeof(wchar_t));
		tok[n] = 0;
		wchar_t trial[180];
		if (!cur[0]) wcscpy_s(trial, tok);
		else swprintf_s(trial, L"%s %s", cur, tok);
		if (cur[0] && maxW > 8.f && r.TextWidth(trial, px, 0) > maxW) {
			wcscpy_s(lines[nLine++], cur);
			if (nLine >= maxLines) { dropped = 1; cur[0] = 0; break; }
			wcscpy_s(cur, tok);
		} else wcscpy_s(cur, trial);
		p = e;
	}
	if (*p) dropped = 1;
	if (cur[0] && nLine < maxLines) wcscpy_s(lines[nLine++], cur);
	else if (cur[0]) dropped = 1;
	if (dropped && nLine > 0) {
		size_t len = wcslen(lines[nLine - 1]);
		if (len + 1 < 178) wcscat_s(lines[nLine - 1], L"…");
	}
	return nLine;
}

static DWORD FoeInk(const Unit& u)
{
	if (UnitIsLucky(u)) return RGBA8(70, 220, 255);
	if (u.boss) return RGBA8(255, 200, 60);
	return RGBA8(255, 100, 90);
}
static DWORD FoeBarCol(const Unit& u)
{
	if (UnitIsLucky(u)) return RGBA8(50, 220, 255);
	if (u.boss) return RGBA8(255, 200, 70);
	return RGBA8(240, 70, 60);
}
static DWORD HpFillCol(const Unit& u, float frac)
{
	if (UnitIsLucky(u)) {
		if (frac > .5f) return RGBA8(80, 235, 255);
		if (frac > .25f) return RGBA8(70, 170, 230);
		return RGBA8(40, 90, 190);
	}
	if (frac > .5f) return RGBA8(120, 240, 120);
	if (frac > .25f) return RGBA8(240, 210, 80);
	return RGBA8(255, 90, 70);
}
static DWORD HpBackCol(const Unit& u)
{
	return UnitIsLucky(u) ? RGBA8(16, 48, 72) : RGBA8(40, 120, 50);
}

// ============================================================ HUD（戦闘）
void Game::DrawUnitPanel(const Unit& u, float x, float y, float w, BOOL big, float* rx, float* ry, float* rw, float* rh)
{
	auto note = [&](float px, float py, float pw, float ph) {
		if (rx) *rx = px;
		if (ry) *ry = py;
		if (rw) *rw = pw;
		if (rh) *rh = ph;
	};
	int ui = (int)(&u - m_b.u);
	if (u.team == TEAM_ENEMY) {
		wchar_t held[480];
		held[0] = 0;
		UnitHeldText(u, held, 480);
		float ps = big ? U(118.f) : U(90.f);
		float tx = x + ps + U(16);
		float tw = w - ps - U(28);
		if (tw < U(72)) tw = U(72);
		float linePx = U(11);
		wchar_t skLine[8][120];
		int nSk = WrapHeldLines(m_r, held, tw, linePx, skLine, 8);
		wchar_t nm[96], lv[128], wk[240], st[160];
		UnitTitle(u, nm, 64);
		if (!u.alive) {
			wchar_t ko[112];
			swprintf_s(ko, L"%s  %s", nm, TT(TX_RS_KO));
			wcscpy_s(nm, ko);
		}
		swprintf_s(lv, L"%s%d  %s%s", TT(TX_MISC_LV), u.ch.lv, ClassName(u.ch.cls), UnitMoveType(u) == MV_FLY ? L"  ✈" : L"");
		UnitWeakText(u, wk, 240);
		st[0] = 0;
		if (big && !(m_seqCut && m_cutStage == 0)) {
			wchar_t bit[40];
			if (u.poison > 0) { swprintf_s(bit, L"%s%d ", TT(TX_BT_POISON), u.poison); wcscat_s(st, bit); }
			if (u.slow > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_SLOW), u.slow); wcscat_s(st, bit); }
			if (u.defDown > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_DEFDOWN), u.defDown); wcscat_s(st, bit); }
			if (u.sleep > 0) { swprintf_s(bit, L"%s ", EffectWord(EF_SLEEP)); wcscat_s(st, bit); }
			if (u.confuse > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_CONFUSE), u.confuse); wcscat_s(st, bit); }
			if (u.stun > 0) { swprintf_s(bit, L"%s ", EffectWord(EF_STUN)); wcscat_s(st, bit); }
			if (u.freeze > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_FREEZE), u.freeze); wcscat_s(st, bit); }
			if (u.burn > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_BURN), u.burn); wcscat_s(st, bit); }
			if (u.bleed > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_BLEED), u.bleed); wcscat_s(st, bit); }
			if (u.blind > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_BLIND), u.blind); wcscat_s(st, bit); }
			if (u.silence > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_SILENCE), u.silence); wcscat_s(st, bit); }
			if (u.buffDef > 0 || u.buffAgi > 0 || u.buffAtk > 0 || u.mulTurns > 0) { wcscat_s(st, TT(TX_BT_BUFF)); wcscat_s(st, L" "); }
			if (!u.alert) { wcscat_s(st, TT(TX_BT_ASLEEP)); wcscat_s(st, L" "); }
		}
		float gapN = U(big ? 20.f : 18.f);
		float gapRow = U(16);
		float gapLucky = UnitIsLucky(u) ? gapRow : 0;
		float gapHp = U(big ? 20.f : 22.f);
		float gapMp = (!big && u.ch.stat[S_MP] > 0) ? U(20) : 0;
		float gapStat = big ? U(18) : 0;
		float gapSt = (big && st[0]) ? U(16) : 0;
		float gapSk = nSk > 0 ? (U(6) + nSk * U(15)) : 0;
		float span = U(12) + gapN + gapRow + gapRow + gapLucky + gapHp + gapMp + gapStat + gapSt + gapSk + U(12);
		float h = max(ps + U(16), span);
		if (big) {
			float limitBot = (float)m_h - m_hud.margin;
			float avail = limitBot - y;
			if (avail < U(72)) avail = U(72);
			if (h > avail) h = avail;
		} else {
			float bottom = y + U(112.f);
			y = bottom - h;
			if (y < U(8)) y = U(8);
		}
		float pad = U(8);
		if (ps > h - pad * 2.f) ps = max(U(40), h - pad * 2.f);
		tx = x + ps + pad + U(8);
		tw = w - (tx - x) - pad;
		if (tw < U(72)) tw = U(72);
		note(x, y, w, h);
		Panel(x, y, w, h);
		DWORD fc = FoeInk(u);
		DrawPortrait(BattlePortrait(ui), x + pad, y + pad, ps, fc);
		float cy = y + U(12);
		float textLim = y + h - U(6);
		m_r.HudText(nm, tx, cy, U(big ? 17.f : 15.f), u.alive ? (UnitIsLucky(u) ? RGBA8(180, 240, 255) : RGBA8(255, 255, 255)) : RGBA8(255, 176, 168), 0, 1);
		cy += gapN;
		m_r.HudText(lv, tx, cy, FitTextPx(lv, tw, U(13), 0), RGBA8(255, 226, 150), 0, 0);
		cy += gapRow;
		float wfs = U(12);
		if (m_r.TextWidth(wk, wfs, 0) > tw) wfs = U(10);
		m_r.HudText(wk, tx, cy, wfs, RGBA8(255, 186, 120), 0, 0);
		cy += gapRow;
		if (UnitIsLucky(u)) {
			const wchar_t* luck = TT(TX_BT_LUCKY_HELP);
			m_r.HudText(luck, tx, cy, FitTextPx(luck, tw, U(12), 0), RGBA8(140, 230, 255), 0, 0);
			cy += gapLucky;
		}
		if (big) {
			wchar_t hp[48], mp[48], cp[32];
			swprintf_s(hp, L"%s %s/%s", TT(TX_ST_HP), TacNum((int)(u.hpShow + .5f)), TacNum(u.ch.stat[S_HP]));
			mp[0] = 0;
			if (u.ch.stat[S_MP] > 0) swprintf_s(mp, L"%s %s/%s", TT(TX_ST_MP), TacNum(u.mp), TacNum(u.ch.stat[S_MP]));
			swprintf_s(cp, L"%s %s", TT(TX_CP), TacNum(u.cp));
			float fs = U(12);
			float gap = U(12);
			float need = m_r.TextWidth(hp, fs, 0) + gap + m_r.TextWidth(cp, fs, 0);
			if (mp[0]) need += gap + m_r.TextWidth(mp, fs, 0);
			if (need > tw && need > 1.f) fs *= tw / need;
			float hx = tx;
			m_r.HudText(hp, hx, cy, fs, RGBA8(230, 230, 230), 0, 0);
			hx += m_r.TextWidth(hp, fs, 0) + gap;
			if (mp[0]) {
				m_r.HudText(mp, hx, cy, fs, RGBA8(190, 210, 255), 0, 0);
				hx += m_r.TextWidth(mp, fs, 0) + gap;
			}
			m_r.HudText(cp, hx, cy, fs, u.cp >= 100 ? RGBA8(255, 214, 80) : RGBA8(210, 170, 70), 0, 0);
			cy += gapHp;
			wchar_t sl[160];
			swprintf_s(sl, L"%s%s %s%s %s%s %s%s %s%s %s%s", TT(TX_ST_STR), TacNum(UnitStat(u, S_STR)), TT(TX_ST_MAG), TacNum(UnitStat(u, S_MAG)), TT(TX_ST_DEF), TacNum(UnitStat(u, S_DEF)),
				TT(TX_ST_RES), TacNum(UnitStat(u, S_RES)), TT(TX_ST_AGI), TacNum(UnitStat(u, S_AGI)), TT(TX_ST_MOV), TacNum(UnitStat(u, S_MOV)));
			float sfs = U(11);
			float sw = m_r.TextWidth(sl, sfs, 0);
			if (sw > tw && sw > 1.f) sfs *= tw / sw;
			if (cy < textLim) m_r.HudText(sl, tx, cy, sfs, RGBA8(210, 210, 220), 0, 0);
			cy += gapStat;
			if (st[0] && cy < textLim) {
				m_r.HudText(st, tx, cy, U(11), RGBA8(200, 230, 255), 0, 0);
				cy += gapSt;
			}
		} else {
			float bw = tw * .36f;
			wchar_t hp[64];
			swprintf_s(hp, L"%s %s/%s", TT(TX_ST_HP), TacNum((int)(u.hpShow + .5f)), TacNum(u.ch.stat[S_HP]));
			float hpFs = U(12);
			if (m_r.TextWidth(hp, hpFs, 0) > tw - bw - U(40)) hpFs = U(10);
			m_r.HudText(hp, tx, cy, hpFs, RGBA8(230, 230, 230), 0, 0);
			wchar_t cp[32];
			swprintf_s(cp, L"%s %s", TT(TX_CP), TacNum(u.cp));
			m_r.HudText(cp, tx + tw - bw - U(8), cy, U(12), u.cp >= 100 ? RGBA8(255, 214, 80) : RGBA8(210, 170, 70), 2, 0);
			float frac = u.hpShow / max(1, u.ch.stat[S_HP]);
			Bar(tx + tw - bw, cy + U(2), bw, U(8), frac, HpFillCol(u, frac), HpBackCol(u));
			cy += gapHp;
			if (u.ch.stat[S_MP] > 0) {
			swprintf_s(hp, L"%s %s/%s", TT(TX_ST_MP), TacNum(u.mp), TacNum(u.ch.stat[S_MP]));
			m_r.HudText(hp, tx, cy, FitTextPx(hp, tw - bw - U(8), U(12), 0), RGBA8(220, 220, 240), 0, 0);
				Bar(tx + tw - bw, cy + U(2), bw, U(7), (float)u.mp / max(1, u.ch.stat[S_MP]), RGBA8(110, 170, 255), RGBA8(40, 70, 170));
				cy += gapMp;
			}
		}
		if (nSk > 0 && cy < textLim) {
			cy += U(6);
			for (int i = 0; i < nSk; i++) {
				if (cy > textLim) break;
				float fs = linePx;
				float lw = m_r.TextWidth(skLine[i], fs, 0);
				if (lw > tw && lw > 1.f) fs *= tw / lw;
				m_r.HudText(skLine[i], tx, cy, fs, RGBA8(186, 214, 255), 0, 0);
				cy += U(15);
			}
		}
		return;
	}
	float ps = big ? U(96.f) : U(78.f);
	float linePx = U(12);
	float baseH = big ? U(156.f) : U(132.f);
	float gapLim = max(6.f, U(12.f));
	float limitBot = big ? ((float)m_h - m_hud.margin) : (m_hud.logTop - gapLim);
	float room = limitBot - y;
	if (room < U(80)) room = U(80);
	int pathLines = 0;
	float h = FitCardH(m_r, &u, w, big, ps, U(30), baseH, linePx, room, &pathLines);
	if (y + h > limitBot) h = max(U(72), limitBot - y);
	if (ps > h - U(16)) ps = max(U(40), h - U(16));
	note(x, y, w, h);
	Panel(x, y, w, h);
	DWORD fc = u.team == TEAM_PLAYER ? RGBA8(110, 170, 255) : FoeInk(u);
		DrawPortrait(BattlePortrait(ui), x + U(10), y + U(10), ps, fc);
	float tx = x + ps + U(20), tw = w - ps - U(30);
	if (tw < U(48)) tw = U(48);
	wchar_t nm[96];
	UnitTitle(u, nm, 64);
	if (!u.alive) {
		wchar_t ko[112];
		swprintf_s(ko, L"%s  %s", nm, TT(TX_RS_KO));
		wcscpy_s(nm, ko);
	}
	m_r.HudText(nm, tx, y + U(16), U(big ? 17.f : 15.f), u.alive ? RGBA8(255, 255, 255) : RGBA8(255, 176, 168), 0, 1);
	wchar_t l[128];
	float cy = y + U(34);
	if (u.team == TEAM_PLAYER) {
		int need = ExpToNext(u.ch.lv);
		int showExp = u.ch.lv >= TAC_LV_MAX ? 0 : u.ch.exp;
		int showNeed = u.ch.lv >= TAC_LV_MAX ? 0 : need;
		swprintf_s(l, L"%s%s  %s%s", TT(TX_MISC_LV), TacNum(u.ch.lv), ClassName(u.ch.cls), UnitMoveType(u) == MV_FLY ? L"  ✈" : L"");
		m_r.HudText(l, tx, cy, FitTextPx(l, tw, U(12), 0), RGBA8(255, 226, 150), 0, 0);
		cy += U(16);
		if (pathLines > 0 && cy + U(14) < y + h - U(28)) {
			cy += DrawJobPath(m_r, u.ch, tx, cy - U(6), tw, linePx, RGBA8(210, 220, 235), pathLines) + U(2);
		}
		if (cy + U(12) < y + h - U(8)) {
			swprintf_s(l, L"%s %s/%s", TT(TX_MISC_EXP), TacNum(showExp), TacNum(showNeed));
			m_r.HudText(l, tx, cy, FitTextPx(l, tw, U(12), 0), RGBA8(180, 220, 255), 0, 0);
			cy += U(14);
		}
		if (cy + U(6) < y + h - U(8)) {
			float expF = (u.ch.lv >= TAC_LV_MAX || showNeed <= 0) ? 1.f : (float)showExp / (float)showNeed;
			if (expF < 0.f) expF = 0.f;
			if (expF > 1.f) expF = 1.f;
			Bar(tx, cy + U(1), tw, U(4), expF, RGBA8(120, 210, 255), RGBA8(36, 64, 110));
			cy += U(10);
		}
	} else {
		swprintf_s(l, L"%s%s  %s%s", TT(TX_MISC_LV), TacNum(u.ch.lv), ClassName(u.ch.cls), UnitMoveType(u) == MV_FLY ? L"  ✈" : L"");
		m_r.HudText(l, tx, y + U(36), FitTextPx(l, tw, U(13), 0), RGBA8(255, 226, 150), 0, 0);
		wchar_t wk[240];
		UnitWeakText(u, wk, 240);
		float wfs = U(12);
		if (m_r.TextWidth(wk, wfs, 0) > tw && m_r.TextWidth(wk, wfs, 0) > 1.f) wfs = U(10);
		m_r.HudText(wk, tx, y + U(50), wfs, RGBA8(255, 186, 120), 0, 0);
	}
	float by = u.team == TEAM_PLAYER ? cy : (y + U(66));
	if (big) {
		// 左下の欄は数値だけ。バーはログ側へ食い込むので置かない。
		wchar_t hp[48], mp[48], cp[32];
		swprintf_s(hp, L"%s %s/%s", TT(TX_ST_HP), TacNum((int)(u.hpShow + .5f)), TacNum(u.ch.stat[S_HP]));
		mp[0] = 0;
		if (u.ch.stat[S_MP] > 0) swprintf_s(mp, L"%s %s/%s", TT(TX_ST_MP), TacNum(u.mp), TacNum(u.ch.stat[S_MP]));
		swprintf_s(cp, L"%s %s", TT(TX_CP), TacNum(u.cp));
		float fs = U(12);
		float gap = U(12);
		float need = m_r.TextWidth(hp, fs, 0) + gap + m_r.TextWidth(cp, fs, 0);
		if (mp[0]) need += gap + m_r.TextWidth(mp, fs, 0);
		if (need > tw && need > 1.f) fs *= tw / need;
		float hx = tx;
		m_r.HudText(hp, hx, by + U(2), fs, RGBA8(230, 230, 230), 0, 0);
		hx += m_r.TextWidth(hp, fs, 0) + gap;
		if (mp[0]) {
			m_r.HudText(mp, hx, by + U(2), fs, RGBA8(190, 210, 255), 0, 0);
			hx += m_r.TextWidth(mp, fs, 0) + gap;
		}
		m_r.HudText(cp, hx, by + U(2), fs, u.cp >= 100 ? RGBA8(255, 214, 80) : RGBA8(210, 170, 70), 0, 0);
	} else {
		float bw = tw * .36f;
		swprintf_s(l, L"%s %s/%s", TT(TX_ST_HP), TacNum((int)(u.hpShow + .5f)), TacNum(u.ch.stat[S_HP]));
		m_r.HudText(l, tx, by + U(2), FitTextPx(l, tw - bw - U(8), U(12), 0), RGBA8(230, 230, 230), 0, 0);
		{
			wchar_t cp[32];
			swprintf_s(cp, L"%s %s", TT(TX_CP), TacNum(u.cp));
			m_r.HudText(cp, tx + tw - bw - U(8), by + U(2), U(12), u.cp >= 100 ? RGBA8(255, 214, 80) : RGBA8(210, 170, 70), 2, 0);
		}
		float frac = u.hpShow / max(1, u.ch.stat[S_HP]);
		Bar(tx + tw - bw, by + U(4), bw, U(8), frac, HpFillCol(u, frac), HpBackCol(u));
		by += U(20);
		if (u.ch.stat[S_MP] > 0) {
			swprintf_s(l, L"%s %s/%s", TT(TX_ST_MP), TacNum(u.mp), TacNum(u.ch.stat[S_MP]));
			m_r.HudText(l, tx, by + U(2), FitTextPx(l, tw - bw - U(8), U(12), 0), RGBA8(220, 220, 240), 0, 0);
			Bar(tx + tw - bw, by + U(4), bw, U(7), (float)u.mp / max(1, u.ch.stat[S_MP]), RGBA8(110, 170, 255), RGBA8(40, 70, 170));
		}
	}
	if (big && by + U(14) < y + h - U(4)) {
		by += U(16);
		swprintf_s(l, L"%s%s %s%s %s%s %s%s %s%s %s%s", TT(TX_ST_STR), TacNum(UnitStat(u, S_STR)), TT(TX_ST_MAG), TacNum(UnitStat(u, S_MAG)), TT(TX_ST_DEF), TacNum(UnitStat(u, S_DEF)),
			TT(TX_ST_RES), TacNum(UnitStat(u, S_RES)), TT(TX_ST_AGI), TacNum(UnitStat(u, S_AGI)), TT(TX_ST_MOV), TacNum(UnitStat(u, S_MOV)));
		float sfs = U(11);
		float sw = m_r.TextWidth(l, sfs, 0);
		if (sw > tw && sw > 1.f) sfs *= tw / sw;
		if (by + U(8) < y + h - U(2)) m_r.HudText(l, tx, by + U(6), sfs, RGBA8(210, 210, 220), 0, 0);
		by += U(16);
		wchar_t st[160] = L"";
		if (!(m_seqCut && m_cutStage == 0)) {
		wchar_t bit[40];
		if (u.poison > 0) { swprintf_s(bit, L"%s%d ", TT(TX_BT_POISON), u.poison); wcscat_s(st, bit); }
		if (u.slow > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_SLOW), u.slow); wcscat_s(st, bit); }
		if (u.defDown > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_DEFDOWN), u.defDown); wcscat_s(st, bit); }
		if (u.sleep > 0) { swprintf_s(bit, L"%s ", EffectWord(EF_SLEEP)); wcscat_s(st, bit); }
		if (u.confuse > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_CONFUSE), u.confuse); wcscat_s(st, bit); }
		if (u.stun > 0) { swprintf_s(bit, L"%s ", EffectWord(EF_STUN)); wcscat_s(st, bit); }
		if (u.freeze > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_FREEZE), u.freeze); wcscat_s(st, bit); }
		if (u.burn > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_BURN), u.burn); wcscat_s(st, bit); }
		if (u.bleed > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_BLEED), u.bleed); wcscat_s(st, bit); }
		if (u.blind > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_BLIND), u.blind); wcscat_s(st, bit); }
		if (u.silence > 0) { swprintf_s(bit, L"%s%d ", EffectWord(EF_SILENCE), u.silence); wcscat_s(st, bit); }
		if (u.buffDef > 0 || u.buffAgi > 0 || u.buffAtk > 0 || u.mulTurns > 0) { wcscat_s(st, TT(TX_BT_BUFF)); wcscat_s(st, L" "); }
		if (u.team == TEAM_ENEMY && !u.alert) { wcscat_s(st, TT(TX_BT_ASLEEP)); wcscat_s(st, L" "); }
		}
		if (st[0] && by + U(8) < y + h - U(2)) m_r.HudText(st, tx, by + U(6), U(11), RGBA8(200, 230, 255), 0, 0);
	}
}

void Game::DrawTurnOrder()
{
	int order[12];
	const int acting = (m_turnOpen && m_cur >= 0 && m_b.u[m_cur].alive) ? m_cur : -1;
	int cost = 1000;
	if (acting >= 0) {
		/* 道具の予告は遅れ10。実使用は 0〜20 で手番がすぐ戻る。 */
		if (m_phase == PH_P_ITEM || (m_phase == PH_P_CMD && m_focusId == ID_BT_ITEM))
			cost = max(0, m_b.u[acting].ct - (1000 - 10));
		else if (m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE) cost = 1000;
		else if (m_phase == PH_P_CMD || m_phase == PH_P_SKILLS || m_phase == PH_P_SUMMON) cost = (m_focusId == ID_BT_WAIT || m_focusId == ID_BT_DOOR) ? 600 : 1000;
		else if (m_phase == PH_P_MOVE && m_cursorValid) {
			int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
			BOOL atk = t >= 0 && m_b.u[t].alive && m_b.u[t].team != m_b.u[m_cur].team && (m_pathN > 1 || CanTarget(m_cur, t, -1));
			cost = atk ? 1000 : (m_pathN > 1 || m_moved ? 800 : 600);
		} else if (m_moved) cost = 800;
		else cost = 600;
	}
	int insertAt = -1;
	int n = m_b.PredictOrder(order, 8, acting, cost, acting >= 0 ? &insertAt : NULL);
	if (n > 8) n = 8;
	BOOL diff = (n != m_ordSnapN);
	if (!diff) for (int i = 0; i < n; i++) if (order[i] != m_ordSnap[i]) diff = TRUE;
	if (diff) {
		int oldN = m_ordSnapN;
		int old[8];
		for (int i = 0; i < oldN && i < 8; i++) old[i] = m_ordSnap[i];
		m_ordLeave = -1;
		for (int j = 0; j < oldN && j < 8; j++) {
			BOOL still = FALSE;
			for (int i = 0; i < n; i++) if (order[i] == old[j]) still = TRUE;
			if (!still) m_ordLeave = old[j];
		}
		for (int i = 0; i < n; i++) {
			m_ordFrom[i] = -1;
			for (int j = 0; j < oldN && j < 8; j++) if (old[j] == order[i]) m_ordFrom[i] = j;
		}
		for (int i = 0; i < n; i++) m_ordSnap[i] = order[i];
		m_ordSnapN = n;
		m_ordAnimT = m_time;
	}
	float slide = 1.f - Smooth01(Clampf((m_time - m_ordAnimT) / .34f, 0, 1));
	int aimTg[MAX_UNITS];
	int aimN = 0;
	unsigned char aimOn[MAX_UNITS];
	memset(aimOn, 0, sizeof(aimOn));
	if ((m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET) && m_cur >= 0 && m_cursorValid && CursorInAttackRange()) {
		if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0)
			aimN = SkillTargets(m_b, m_cur, m_cmdSkill, m_cx, m_cz, aimTg, MAX_UNITS);
		else {
			int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
			if (t >= 0 && CanTarget(m_cur, t, -1)) aimTg[aimN++] = t;
		}
		for (int k = 0; k < aimN; k++) if (aimTg[k] >= 0 && aimTg[k] < MAX_UNITS) aimOn[aimTg[k]] = 1;
	}
	float w = m_hud.orderW > 8.f ? m_hud.orderW : min(U(188.f), (float)m_w * 0.4f);
	float x = m_hud.orderW > 8.f ? m_hud.orderX : (float)m_w - w - 8.f;
	float y = max(4.f, m_hud.margin > 0.f ? m_hud.margin : U(14));
	float maxB = m_hud.orderMaxB > y + 20.f ? m_hud.orderMaxB : (float)m_h * 0.55f;
	int arrows = insertAt >= 0 ? 1 : 0;
	int rows = n + ((m_cur >= 0 && m_b.u[m_cur].alive) ? 1 : 0);
	if (rows < 1) rows = 1;
	float maxH = max(24.f, maxB - y);
	float head = min(U(30.f), maxH * 0.22f);
	if (head < 12.f) head = 12.f;
	float gap = min(U(18.f), maxH * 0.08f);
	float rh = U(40);
	float chrome = head + gap * arrows;
	if (chrome + rh * rows > maxH) {
		rh = (maxH - chrome) / (float)rows;
		while (rh < 12.f && n > 2) { n--; rows--; rh = (maxH - chrome) / (float)rows; }
		if (rh < 10.f) rh = 10.f;
	}
	float panelH = chrome + rh * rows;
	if (y + panelH > maxB) panelH = maxB - y;
	m_orderBottom = y + panelH;
	Panel(x, y, w, panelH);
	m_r.HudText(TT(TX_BT_ORDER), x + w * .5f, y + head * .55f, min(U(14.f), head * .7f), RGBA8(255, 226, 150), 1, 1);
	if (m_ordLeave >= 0 && slide > 0.05f)
		m_r.HudText(L"▼", x + w - U(16), y + U(16), U(14), RGBA8(255, 140, 110), 2, 1);
	float ry = y + head;
	auto row = [&](int ui, float y0, float xoff, BOOL now, BOOL ghost, const wchar_t* arrow) {
		const Unit& u = m_b.u[ui];
		DWORD fc = u.team == TEAM_PLAYER ? RGBA8(110, 170, 255) : FoeInk(u);
		if (ghost) fc = RGBA8(255, 210, 90);
		if (now) m_r.HudRect(x + U(4), y0, w - U(8), rh - U(4), RGBA8(255, 220, 120, 60));
		if (ghost) m_r.HudRect(x + U(4), y0, w - U(8), rh - U(4), RGBA8(255, 190, 60, 50));
		float ps = max(4.f, rh * 0.72f);
		float py = y0 + max(1.f, (rh - ps) * .35f);
		DrawPortrait(BattlePortrait(ui), x + U(4) + xoff, py, ps, fc);
		if (ui >= 0 && ui < MAX_UNITS && aimOn[ui] && aimN > 0 && ps >= 12.f) {
			wchar_t nb[8];
			swprintf_s(nb, L"%d", aimN);
			float bs = max(11.f, min(U(16.f), ps * 0.5f));
			float bx = x + U(4) + xoff + ps - bs * 0.72f;
			float by = py - bs * 0.12f;
			m_r.HudRect(bx, by, bs, bs, RGBA8(28, 16, 6, 235));
			m_r.HudFrame(bx, by, bs, bs, 1.f, RGBA8(255, 214, 90));
			m_r.HudText(nb, bx + bs * .5f, by + bs * .5f, min(U(12.f), bs * 0.7f), RGBA8(255, 236, 160), 1, 1);
		}
		wchar_t nm[64];
		UnitTitle(u, nm, 64);
		float nameMax = max(8.f, w - ps - U(28));
		m_r.HudText(nm, x + ps + U(8) + xoff, y0 + rh * .32f, FitTextPx(nm, nameMax, min(U(12.f), rh * 0.34f), now || ghost), ghost ? RGBA8(255, 230, 150) : (now ? RGBA8(255, 240, 180) : RGBA8(240, 240, 240)), 0, now || ghost);
		if (ghost) m_r.HudText(TT(TX_BT_REJOIN), x + w - U(22), y0 + rh * .32f, U(11), RGBA8(255, 210, 90), 2, 1);
		if (arrow) m_r.HudText(arrow, x + w - U(8), y0 + rh * .28f, U(13), RGBA8(255, 210, 90), 2, 1);
		float frac = u.hpShow / max(1, u.ch.stat[S_HP]);
		Bar(x + ps + U(8), y0 + rh * .64f, max(8.f, w - ps - U(20)), max(2.f, rh * 0.14f), frac, fc, fc);
	};
	if (m_cur >= 0 && m_b.u[m_cur].alive) row(m_cur, ry, 0, TRUE, FALSE, NULL);
	if (m_cur >= 0 && m_b.u[m_cur].alive) ry += rh;
	for (int i = 0; i < n; i++) {
		if (i == insertAt) {
			float bob = sinf(m_time * 7.f) * U(3);
			m_r.HudText(L"▼", x + U(16), ry + gap * .45f + bob, U(16), RGBA8(255, 210, 80), 1, 1);
			m_r.HudRect(x + U(28), ry + gap * .42f, w - U(40), U(2), RGBA8(255, 200, 70, 220));
			ry += gap;
		}
		int from = m_ordFrom[i];
		float y0 = ry;
		float xoff = 0;
		const wchar_t* arrow = NULL;
		if (slide > 0.02f) {
			if (from < 0) { xoff = U(26) * slide; arrow = L"▶"; }
			else if (from != i) y0 = ry + (from - i) * rh * slide;
			if (from >= 0 && from < i) arrow = L"▼";
			else if (from > i) arrow = L"▲";
		}
		row(order[i], y0, xoff, FALSE, order[i] == acting, arrow);
		ry += rh;
	}
}

void Game::DrawShowMinimap(float x, float y, float s)
{
	if (!m_showBuilt || !m_r.MinimapSrv()) return;
	m_r.HudRect(x - U(4), y - U(4), s + U(8), s + U(8), RGBA8(8, 10, 16, 210));
	const Map& map = m_showMap;
	int x0 = MAPN, z0 = MAPN, x1 = 0, z1 = 0;
	for (int z = 0; z < MAPN; z++) for (int xx = 0; xx < MAPN; xx++) {
		if (map.At(xx, z).t == T_VOID) continue;
		if (xx < x0) x0 = xx;
		if (z < z0) z0 = z;
		if (xx > x1) x1 = xx;
		if (z > z1) z1 = z;
	}
	if (x0 > x1) { x0 = 0; z0 = 0; x1 = MAPN - 1; z1 = MAPN - 1; }
	const float yaw = m_showYaw;
	const float rx = -cosf(yaw), rz = sinf(yaw);
	const float fx = -sinf(yaw), fz = -cosf(yaw);
	const float wx0 = (float)x0, wz0 = (float)z0, wx1 = (float)(x1 + 1), wz1 = (float)(z1 + 1);
	float cu[4], cv[4];
	auto proj = [&](float wx, float wz, float& u, float& v) {
		u = wx * rx + wz * rz;
		v = wx * fx + wz * fz;
	};
	proj(wx0, wz0, cu[0], cv[0]);
	proj(wx1, wz0, cu[1], cv[1]);
	proj(wx1, wz1, cu[2], cv[2]);
	proj(wx0, wz1, cu[3], cv[3]);
	float umin = cu[0], umax = cu[0], vmin = cv[0], vmax = cv[0];
	for (int i = 1; i < 4; i++) {
		if (cu[i] < umin) umin = cu[i];
		if (cu[i] > umax) umax = cu[i];
		if (cv[i] < vmin) vmin = cv[i];
		if (cv[i] > vmax) vmax = cv[i];
	}
	float span = max(umax - umin, vmax - vmin);
	if (span < 1.f) span = 1.f;
	const float cs = s / span;
	const float tw = (umax - umin) * cs, th = (vmax - vmin) * cs;
	const float ox = x + (s - tw) * .5f, oy = y + (s - th) * .5f;
	auto toScreen = [&](float wx, float wz, float& sx, float& sy) {
		float u, v;
		proj(wx, wz, u, v);
		sx = ox + (u - umin) * cs;
		sy = oy + (vmax - v) * cs;
	};
	if (m_r.MinimapSrv()) {
		float xy[8], uv[8];
		const float corn[4][2] = { { wx0, wz0 }, { wx1, wz0 }, { wx1, wz1 }, { wx0, wz1 } };
		const float uvs[4][2] = { { wx0 / MAPN, wz0 / MAPN }, { wx1 / MAPN, wz0 / MAPN }, { wx1 / MAPN, wz1 / MAPN }, { wx0 / MAPN, wz1 / MAPN } };
		for (int i = 0; i < 4; i++) {
			toScreen(corn[i][0], corn[i][1], xy[i * 2], xy[i * 2 + 1]);
			uv[i * 2] = uvs[i][0]; uv[i * 2 + 1] = uvs[i][1];
		}
		m_r.HudQuad(m_r.MinimapSrv(), xy, uv, 0xFFFFFFFF);
	}
	for (int i = 0; i < map.nP; i++) {
		float sx, sy;
		toScreen(map.pStartX[i] + .5f, map.pStartZ[i] + .5f, sx, sy);
		float d = max(cs * .7f, U(5));
		m_r.HudRect(sx - d * .5f, sy - d * .5f, d, d, RGBA8(90, 190, 255));
	}
}

void Game::DrawMinimap(float x, float y, float s, int layer)
{
	if (layer < 0) layer = 0;
	if (layer > 2) layer = 2;
	m_r.HudRect(x - U(6), y - U(6), s + U(12), s + U(12), RGBA8(8, 10, 16, 230));
	int x0 = MAPN, z0 = MAPN, x1 = 0, z1 = 0;
	for (int z = 0; z < MAPN; z++) for (int xx = 0; xx < MAPN; xx++) {
		if (m_b.map.At(xx, z).t == T_VOID) continue;
		if (xx < x0) x0 = xx;
		if (z < z0) z0 = z;
		if (xx > x1) x1 = xx;
		if (z > z1) z1 = z;
	}
	if (x0 > x1) { x0 = 0; z0 = 0; x1 = MAPN - 1; z1 = MAPN - 1; }
	x0 = max(0, x0 - 1); z0 = max(0, z0 - 1);
	x1 = min(MAPN - 1, x1 + 1); z1 = min(MAPN - 1, z1 + 1);
	// 画面右 = ミニマップ右、画面奥 = ミニマップ上。ワールド +X を右に置くとカメラ向きで左右が反転する。
	const float yaw = m_camYaw;
	const float rx = -cosf(yaw), rz = sinf(yaw);
	const float fx = -sinf(yaw), fz = -cosf(yaw);
	auto proj = [&](float wx, float wz, float& u, float& v) {
		u = wx * rx + wz * rz;
		v = wx * fx + wz * fz;
	};
	const float wx0 = (float)x0, wz0 = (float)z0, wx1 = (float)(x1 + 1), wz1 = (float)(z1 + 1);
	float cu[4], cv[4];
	proj(wx0, wz0, cu[0], cv[0]);
	proj(wx1, wz0, cu[1], cv[1]);
	proj(wx1, wz1, cu[2], cv[2]);
	proj(wx0, wz1, cu[3], cv[3]);
	float umin = cu[0], umax = cu[0], vmin = cv[0], vmax = cv[0];
	for (int i = 1; i < 4; i++) {
		if (cu[i] < umin) umin = cu[i];
		if (cu[i] > umax) umax = cu[i];
		if (cv[i] < vmin) vmin = cv[i];
		if (cv[i] > vmax) vmax = cv[i];
	}
	float span = max(umax - umin, vmax - vmin);
	if (span < 1.f) span = 1.f;
	const float cs = s / span;
	const float tw = (umax - umin) * cs, th = (vmax - vmin) * cs;
	const float ox = x + (s - tw) * .5f, oy = y + (s - th) * .5f;
	auto toScreen = [&](float wx, float wz, float& sx, float& sy) {
		float u, v;
		proj(wx, wz, u, v);
		sx = ox + (u - umin) * cs;
		sy = oy + (vmax - v) * cs;
	};
	if (m_r.MinimapSrv(layer)) {
		float xy[8], uv[8];
		const float corn[4][2] = { { wx0, wz0 }, { wx1, wz0 }, { wx1, wz1 }, { wx0, wz1 } };
		const float usu[4] = { wx0 / (float)MAPN, wx1 / (float)MAPN, wx1 / (float)MAPN, wx0 / (float)MAPN };
		const float usv[4] = { wz0 / (float)MAPN, wz0 / (float)MAPN, wz1 / (float)MAPN, wz1 / (float)MAPN };
		for (int i = 0; i < 4; i++) {
			toScreen(corn[i][0], corn[i][1], xy[i * 2], xy[i * 2 + 1]);
			uv[i * 2] = usu[i];
			uv[i * 2 + 1] = usv[i];
		}
		m_r.HudQuad(m_r.MinimapSrv(layer), xy, uv, 0xFFFFFFFF);
	}
	for (int z = z0; z <= z1; z++) for (int xx = x0; xx <= x1; xx++) {
		const Cell& c = m_b.map.At(xx, z);
		if (!LayerOn(c, layer) || LayerTr(c, layer) != T_WATER) continue;
		float sx, sy;
		toScreen(xx + .5f, z + .5f, sx, sy);
		if (sx < x || sy < y || sx > x + s || sy > y + s) continue;
		float ph = 0.5f + 0.5f * sinf(m_time * 3.2f + xx * 0.7f + z * 0.4f);
		float d = max(2.f, cs * 0.28f);
		m_r.HudRect(sx - d * .5f, sy - d * ph * .4f, d, max(1.f, d * 0.35f), RGBA8(230, 248, 255, (int)(50 + 90 * ph)));
	}
	DWORD frame = layer == m_cLayer ? RGBA8(255, 220, 140) : RGBA8(120, 110, 90);
	m_r.HudFrame(x - U(6), y - U(6), s + U(12), s + U(12), U(2.5f), frame);
	DWORD lc = layer == 0 ? RGBA8(140, 210, 255) : (layer == 1 ? RGBA8(255, 220, 120) : RGBA8(255, 150, 190));
	wchar_t lab[8];
	swprintf_s(lab, L"%d", layer + 1);
	m_r.HudText(lab, x + U(16), y + U(16), U(16), lc, 1, 1);
	auto dots = [&](float xA, float yA, float xB, float yB, int steps, float th, DWORD col) {
		const float L = x, T = y, R = x + s, B = y + s;
		auto code = [&](float px, float py) {
			int c = 0;
			if (px < L) c |= 1;
			if (px > R) c |= 2;
			if (py < T) c |= 4;
			if (py > B) c |= 8;
			return c;
		};
		int c0 = code(xA, yA), c1 = code(xB, yB);
		for (int guard = 0; guard < 8; guard++) {
			if (!(c0 | c1)) break;
			if (c0 & c1) return;
			int c = c0 ? c0 : c1;
			float nx, ny;
			if (c & 3) {
				float dx = xB - xA;
				if (fabsf(dx) < 1e-4f) return;
				float edge = (c & 1) ? L : R;
				ny = yA + (yB - yA) * (edge - xA) / dx;
				nx = edge;
			} else {
				float dy = yB - yA;
				if (fabsf(dy) < 1e-4f) return;
				float edge = (c & 4) ? T : B;
				nx = xA + (xB - xA) * (edge - yA) / dy;
				ny = edge;
			}
			if (c == c0) { xA = nx; yA = ny; c0 = code(xA, yA); }
			else { xB = nx; yB = ny; c1 = code(xB, yB); }
		}
		if (c0 | c1) return;
		if (steps < 1) steps = 1;
		for (int i = 0; i <= steps; i++) {
			float t = i / (float)steps;
			float px = xA + (xB - xA) * t, py = yA + (yB - yA) * t;
			if (px < L || px > R || py < T || py > B) continue;
			m_r.HudRect(px - th, py - th, th * 2.f, th * 2.f, col);
		}
	};
	for (int g = ((x0 + 3) / 4) * 4; g <= x1; g += 4) {
		float ax, ay, bx, by;
		toScreen((float)g, wz0, ax, ay);
		toScreen((float)g, wz1, bx, by);
		dots(ax, ay, bx, by, 16, 0.7f, RGBA8(255, 255, 255, 28));
	}
	for (int g = ((z0 + 3) / 4) * 4; g <= z1; g += 4) {
		float ax, ay, bx, by;
		toScreen(wx0, (float)g, ax, ay);
		toScreen(wx1, (float)g, bx, by);
		dots(ax, ay, bx, by, 16, 0.7f, RGBA8(255, 255, 255, 28));
	}
	{
		float gy = m_camTarget.y;
		float sxv[4] = { 0, (float)m_w, (float)m_w, 0 };
		float syv[4] = { 0, 0, (float)m_h, (float)m_h };
		float sxp[4], syp[4];
		BOOL ok = TRUE;
		for (int i = 0; i < 4; i++) {
			V3 org, dir;
			m_r.ScreenRay(sxv[i], syv[i], org, dir);
			if (fabsf(dir.y) < 1e-4f) { ok = FALSE; break; }
			float t = (gy - org.y) / dir.y;
			if (t < 0.02f) { ok = FALSE; break; }
			toScreen(org.x + dir.x * t, org.z + dir.z * t, sxp[i], syp[i]);
		}
		if (ok) {
			dots(sxp[0], syp[0], sxp[1], syp[1], 18, 1.f, RGBA8(255, 255, 255, 210));
			dots(sxp[1], syp[1], sxp[2], syp[2], 18, 1.f, RGBA8(255, 255, 255, 210));
			dots(sxp[2], syp[2], sxp[3], syp[3], 18, 1.f, RGBA8(255, 255, 255, 210));
			dots(sxp[3], syp[3], sxp[0], syp[0], 18, 1.f, RGBA8(255, 255, 255, 210));
		}
	}
	int sightMap = TacSightTiles(m_b.weather);
	auto seenCell = [&](int x, int z) -> BOOL {
		if (sightMap <= 0) return TRUE;
		for (int p = 0; p < m_b.nUnits; p++) {
			if (!m_b.u[p].alive || m_b.u[p].team != TEAM_PLAYER) continue;
			if (Dist(x, z, m_b.u[p].x, m_b.u[p].z) <= sightMap) return TRUE;
		}
		return FALSE;
	};
	if (sightMap > 0) {
		for (int z = z0; z <= z1; z++) for (int xx = x0; xx <= x1; xx++) {
			if (seenCell(xx, z)) continue;
			float sx, sy;
			toScreen(xx + .5f, z + .5f, sx, sy);
			if (sx < x - cs || sy < y - cs || sx > x + s + cs || sy > y + s + cs) continue;
			float d = max(cs * 1.05f, 2.f);
			m_r.HudRect(sx - d * .5f, sy - d * .5f, d, d, RGBA8(8, 10, 18, 214));
		}
	}
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (!u.alive) continue;
		if (u.team == TEAM_ENEMY && !seenCell(u.x, u.z)) continue;
		int deck = u.deck;
		if (deck < 0 || deck >= LAY_N || !LayerOn(m_b.map.At(u.x, u.z), deck)) deck = 0;
		if (deck != layer) continue;
		DWORD c = u.team == TEAM_PLAYER ? RGBA8(90, 190, 255) : (UnitIsLucky(u) ? RGBA8(70, 220, 255) : (u.boss ? RGBA8(255, 220, 70) : (u.alert == 2 ? RGBA8(255, 170, 60) : (u.alert ? RGBA8(255, 70, 60) : RGBA8(140, 50, 50)))));
		float d = max(cs * .72f, i == m_cur ? cs * .95f : cs * .78f);
		float sx, sy;
		toScreen(u.px, u.pz, sx, sy);
		m_r.HudRect(sx - d * .5f - U(1), sy - d * .5f - U(1), d + U(2), d + U(2), RGBA8(10, 10, 14, 220));
		m_r.HudRect(sx - d * .5f, sy - d * .5f, d, d, c);
	}
	if (m_cursorValid) {
		float cxy[8];
		toScreen((float)m_cx, (float)m_cz, cxy[0], cxy[1]);
		toScreen((float)(m_cx + 1), (float)m_cz, cxy[2], cxy[3]);
		toScreen((float)(m_cx + 1), (float)(m_cz + 1), cxy[4], cxy[5]);
		toScreen((float)m_cx, (float)(m_cz + 1), cxy[6], cxy[7]);
		float th = max(1.f, cs * .12f);
		dots(cxy[0], cxy[1], cxy[2], cxy[3], 6, th, RGBA8(255, 255, 255));
		dots(cxy[2], cxy[3], cxy[4], cxy[5], 6, th, RGBA8(255, 255, 255));
		dots(cxy[4], cxy[5], cxy[6], cxy[7], 6, th, RGBA8(255, 255, 255));
		dots(cxy[6], cxy[7], cxy[0], cxy[1], 6, th, RGBA8(255, 255, 255));
	}
	static const DWORD kWc[8] = {
		RGBA8(255, 80, 80), RGBA8(80, 160, 255), RGBA8(255, 210, 60), RGBA8(80, 230, 120),
		RGBA8(200, 80, 255), RGBA8(80, 230, 230), RGBA8(255, 140, 40), RGBA8(255, 120, 180)
	};
	for (int id = 0; id < 8; id++) {
		int ax = -1, az = -1, bx = -1, bz = -1;
		for (int z = 0; z < MAPN; z++) for (int xx = 0; xx < MAPN; xx++) {
			const Cell& c = m_b.map.At(xx, z);
			if (c.gimm != GM_WARP || (int)c.gparam != id) continue;
			if (ax < 0) { ax = xx; az = z; }
			else { bx = xx; bz = z; }
		}
		if (ax < 0 || bx < 0) continue;
		float xA, yA, xB, yB;
		toScreen(ax + .5f, az + .5f, xA, yA);
		toScreen(bx + .5f, bz + .5f, xB, yB);
		dots(xA, yA, xB, yB, 8, 1.5f, kWc[id]);
	}
}

static long long ApplyExpMods(const Battle& b, const Unit& u, long long g)
{
	if (g <= 0) return 0;
	if (UnitHasPas(u, PAS_EXP2)) g = g * 140 / 100;
	else if (UnitHasPas(u, PAS_EXP)) g = g * 120 / 100;
	if (g < 1) g = 1;
	int topLv = u.ch.lv;
	for (int i = 0; i < b.nUnits; i++) {
		if (b.u[i].team == TEAM_PLAYER && b.u[i].ch.lv > topLv)
			topLv = b.u[i].ch.lv;
	}
	int gap = topLv - u.ch.lv;
	if (gap > 0) {
		int pct = 100 + gap * 10;
		if (pct > 400) pct = 400;
		long long boosted = g * (long long)pct / 100;
		if (boosted > g) g = boosted;
	}
	if (g > 2147483647LL) g = 2147483647LL;
	return g;
}

static float AimPanelH(float baseH, float contentFloor, float expBlock, float compactPad, BOOL combat)
{
	if (combat) {
		float h = baseH;
		float want = contentFloor + expBlock;
		if (expBlock > 0.f && want > h) h = want;
		return h;
	}
	if (expBlock <= 0.f) return 0.f;
	return expBlock + compactPad;
}

void Game::PreviewBattleExp(long long& opt, long long& pes, int& showLv, int& nTg, BOOL& combat) const
{
	opt = 0;
	pes = 0;
	showLv = 0;
	nTg = 0;
	combat = FALSE;
	if (m_cur < 0 || m_cur >= m_b.nUnits || !m_cursorValid) return;
	if (!Map::In(m_cx, m_cz)) return;
	const Unit& a = m_b.u[m_cur];
	if (a.team != TEAM_PLAYER || a.hp <= 0) return;
	if ((m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET) && !CursorInAttackRange()) return;
	int skill = m_phase == PH_P_SKILLTARGET ? m_cmdSkill : -1;
	int ids[MAX_UNITS];
	int n = 0;
	int ax = m_cx, az = m_cz;
	BOOL single = FALSE;
	if (skill < 0) {
		int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
		if (CanTarget(m_cur, t, -1)) ids[n++] = t;
		single = TRUE;
	} else {
		const SkillDef& sd = SkillInfo(skill);
		if (sd.kind == SK_PASSIVE) return;
		BOOL areaCast = sd.rMax == 0 || sd.area > 0 || sd.kind == SK_HEAL || sd.kind == SK_BUFF;
		if (sd.rMax == 0) { ax = a.x; az = a.z; }
		if (areaCast) {
			if (sd.rMax == 0 || m_ovAtk[m_cz][m_cx])
				n = SkillTargets(m_b, m_cur, skill, ax, az, ids, MAX_UNITS);
		} else {
			int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
			if (CanTarget(m_cur, t, skill)) ids[n++] = t;
			single = TRUE;
		}
	}
	long long rawOpt = 0, rawPes = 0;
	BOOL canMiss = FALSE;
	int counted = 0;
	for (int i = 0; i < n; i++) {
		if (ids[i] < 0 || ids[i] >= m_b.nUnits) continue;
		const Unit& d = m_b.u[ids[i]];
		if (skill >= 0 && SkillInfo(skill).kind == SK_HEAL) {
			const SkillDef& sd = SkillInfo(skill);
			long long e;
			int healed = 0;
			if (sd.effect == EF_RAISE) healed = max(1, d.ch.stat[S_HP] * 45 / 100);
			else {
				int amt = PreviewHealAmount(a, d, skill);
				amt = (int)((long long)amt * FormBoostPct(m_b, m_cur, skill, ax, az) / 100);
				int room = d.ch.stat[S_HP] - d.hp;
				if (room < 0) room = 0;
				healed = amt < room ? amt : room;
			}
			if (healed > 0) {
				e = ExpForAction(a, d, healed, m_battleStage);
				e = e * 3 / 2;
				if (e < 21) e = 21;
			} else e = 12;
			rawOpt += e;
			rawPes += e;
			counted++;
		} else if (skill >= 0 && SkillInfo(skill).kind == SK_BUFF) {
			long long e = 16 + m_battleStage / 2;
			if (e < 1) e = 1;
			rawOpt += e;
			rawPes += e;
			counted++;
		} else {
			if (!d.alive || d.hp <= 0) continue;
			Forecast f;
			CalcForecast(m_b, m_cur, ids[i], skill, f);
			int hits = f.hits > 0 ? f.hits : 1;
			int dmg = f.dmg;
			if (skill >= 0) dmg = (int)((long long)dmg * FormBoostPct(m_b, m_cur, skill, ax, az) / 100);
			if (dmg < 1) dmg = 1;
			long long swingOpt = 0;
			if (f.hit <= 0) {
				swingOpt = hits;
				canMiss = TRUE;
			} else {
				int hp = d.hp;
				for (int s = 0; s < hits; s++) {
					BOOL over = FALSE;
					if (hp <= 0 || (long long)dmg >= (long long)hp * 2) over = TRUE;
					long long e = ExpForAction(a, d, dmg, m_battleStage);
					if (over && e > 1) e *= 2;
					e /= 2;
					if (e < 1) e = 1;
					if (UnitIsLucky(d)) e *= 3; // 見積もりも実付与と同じ☆×3
					swingOpt += e;
					if (hp > 0) {
						hp -= dmg;
						if (hp < 0) hp = 0;
					}
				}
			}
			rawOpt += swingOpt;
			if (f.hit >= 100) rawPes += swingOpt;
			else {
				rawPes += hits;
				canMiss = TRUE;
			}
			counted++;
		}
	}
	rawOpt = ExpShareAoe(rawOpt, counted);
	rawPes = ExpShareAoe(rawPes, counted);
	opt = ApplyExpMods(m_b, a, rawOpt);
	pes = ApplyExpMods(m_b, a, rawPes);
	nTg = single && counted > 0 ? 1 : counted;
	if (opt > 0 && a.ch.lv < TAC_LV_MAX) {
		long long need = (long long)ExpToNext(a.ch.lv) - (long long)a.ch.exp;
		if (need < 0) need = 0;
		if (pes >= need) showLv = 2;
		else if (opt >= need && canMiss) showLv = 1;
		else if (opt >= need) showLv = 2;
	}
	int aim = m_b.UnitAt(m_cx, m_cz, m_cLayer);
	if (aim >= 0 && CanTarget(m_cur, aim, skill)) {
		if (skill < 0) combat = TRUE;
		else {
			int k = SkillInfo(skill).kind;
			if (k != SK_HEAL && k != SK_BUFF) combat = TRUE;
		}
	}
}

void Game::DrawForecast(float* ox, float* oy, float* ow, float* oh)
{
	if (ox) *ox = 0;
	if (oy) *oy = 0;
	if (ow) *ow = 0;
	if (oh) *oh = 0;
	auto markBox = [&](float bx, float by, float bw, float bh) {
		if (ox) *ox = bx;
		if (oy) *oy = by;
		if (ow) *ow = bw;
		if (oh) *oh = bh;
	};
	long long opt = 0, pes = 0;
	int showLv = 0, nTg = 0;
	BOOL combat = FALSE;
	PreviewBattleExp(opt, pes, showLv, nTg, combat);
	(void)pes;
	if (!combat) {
		if (opt <= 0) return;
	} else {
		int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
		int skill = m_phase == PH_P_SKILLTARGET ? m_cmdSkill : -1;
		if (!CanTarget(m_cur, t, skill)) return;
	}
	int skill = m_phase == PH_P_SKILLTARGET ? m_cmdSkill : -1;
	int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
	float expBlock = opt > 0 ? (showLv ? U(44.f) : U(24.f)) : 0.f;
	float w = min(U(520.f), (float)m_w - U(16));
	float baseH = min(U(204.f), max(U(148.f), (float)m_h * .42f));
	float h = AimPanelH(baseH, U(208.f), expBlock, U(12.f), combat);
	float x = ((float)m_w - w) * .5f, y = U(8);
	{
		float left = 8.f, right = (float)m_w - 8.f, top = 8.f;
		if (m_hud.col > 40.f) right = m_hud.col - 8.f;
		float side = m_hud.stageR + 8.f;
		if (right - side >= 150.f) left = side;
		else if (m_hud.stageB > top) top = m_hud.stageB + 6.f;
		if (m_hud.partyR + 8.f > left && right - (m_hud.partyR + 8.f) >= 150.f && top < m_hud.partyB)
			left = m_hud.partyR + 8.f;
		w = min(w, max(90.f, right - left));
		x = left + max(0.f, (right - left - w) * .5f);
		float bot = m_hud.panelTop > 40.f ? m_hud.panelTop - 6.f : (float)m_h - 8.f;
		if (top + h > bot) h = max(64.f, bot - top);
		y = FitLo(y, top, max(top, bot - h));
	}
	auto drawExp = [&](float ex, float ey, float ew) {
		if (opt <= 0) return;
		wchar_t line[160];
		if (nTg > 1)
			swprintf_s(line, L"%s %s +%s   %s %d", TT(TX_BT_EXP), TT(TX_BT_EXPEST), TacNum((int)opt), TT(TX_BT_AOEN), nTg);
		else
			swprintf_s(line, L"%s %s +%s", TT(TX_BT_EXP), TT(TX_BT_EXPEST), TacNum((int)opt));
		float px = FitTextPx(line, ew - U(16), U(14), 1);
		float ty = showLv ? ey + U(11) : ey + expBlock * .5f;
		m_r.HudText(line, ex + ew * .5f, ty, px, RGBA8(186, 230, 255), 1, 1);
		if (showLv) {
			const wchar_t* lv = showLv == 2 ? TT(TX_BT_LEVELUP) : TT(TX_BT_LVMAY);
			float px2 = FitTextPx(lv, ew - U(16), U(13), 0);
			m_r.HudText(lv, ex + ew * .5f, ey + U(30), px2, showLv == 2 ? RGBA8(255, 220, 110) : RGBA8(255, 206, 140), 1, 1);
		}
	};
	if (!combat) {
		Panel(x, y, w, h, .42f);
		markBox(x, y, w, h);
		drawExp(x, y + U(6), w);
		return;
	}
	Forecast f;
	CalcForecast(m_b, m_cur, t, skill, f);
	const Unit& a = m_b.u[m_cur];
	const Unit& d = m_b.u[t];
	Panel(x, y, w, h, .42f);
	markBox(x, y, w, h);
	auto rowH = [](float px) { return px * 1.75f + 6.f; };
	float room = h - expBlock - U(8);
	float title0 = U(14), body0 = U(15), small0 = U(13), tiny0 = U(12);
	float need = rowH(title0) + rowH(body0) * 2.f + rowH(small0) * 4.f + rowH(tiny0) * 2.f;
	float sc = (room > 8.f && need > room) ? room / need : 1.f;
	if (sc < 0.68f) sc = 0.68f;
	float titlePx = title0 * sc, bodyPx = body0 * sc, smallPx = small0 * sc, tinyPx = tiny0 * sc;
	float cy = y + U(4);
	float bot = y + h - expBlock - U(2);
	{
		wchar_t ft[96];
		int wx = WeatherTx(TacWx(m_b.weather));
		int hx = HourTx(TacHour(m_b.weather));
		if (wx && hx) swprintf_s(ft, L"%s  %s  %s", TT(TX_BT_FORECAST), TT(wx), TT(hx));
		else if (wx) swprintf_s(ft, L"%s  %s", TT(TX_BT_FORECAST), TT(wx));
		else if (hx) swprintf_s(ft, L"%s  %s", TT(TX_BT_FORECAST), TT(hx));
		else wcscpy_s(ft, TT(TX_BT_FORECAST));
		m_r.HudText(ft, x + w * .5f, cy + rowH(titlePx) * .5f, FitTextPx(ft, w - U(16), titlePx, 0), RGBA8(255, 226, 150), 1, 1);
		cy += rowH(titlePx);
	}
	wchar_t nmA[64], nmD[64];
	UnitTitle(a, nmA, 64);
	UnitTitle(d, nmD, 64);
	auto pair = [&](const wchar_t* L, const wchar_t* R, float px, DWORD cL, DWORD cR, int bold) {
		if (cy + rowH(px) > bot) return;
		float yy = cy + rowH(px) * .5f;
		float col = w * .42f;
		if (L && L[0]) m_r.HudText(L, x + w * .25f, yy, FitTextPx(L, col, px, bold), cL, 1, bold);
		if (R && R[0]) m_r.HudText(R, x + w * .75f, yy, FitTextPx(R, col, px, bold), cR, 1, bold);
		cy += rowH(px);
	};
	pair(nmA, nmD, bodyPx, RGBA8(255, 255, 255), RGBA8(255, 255, 255), 1);
	wchar_t hpA[64], hpD[64];
	swprintf_s(hpA, L"%s %s", TT(TX_ST_HP), TacNum(a.hp));
	swprintf_s(hpD, L"%s %s", TT(TX_ST_HP), TacNum(d.hp));
	pair(hpA, hpD, smallPx, RGBA8(220, 220, 220), RGBA8(220, 220, 220), 0);
	wchar_t dmgA[80], dmgD[80];
	if (f.hits > 1) swprintf_s(dmgA, L"%s %s ×%d", TT(TX_BT_DMG), TacNum(f.dmg), f.hits);
	else swprintf_s(dmgA, L"%s %s", TT(TX_BT_DMG), TacNum(f.dmg));
	int el = skill >= 0 ? SkillInfo(skill).element : EL_NONE;
	BOOL weak2 = AilmentBreak(d, el) > 1.5f;
	if (!f.canCounter) wcscpy_s(dmgD, TT(TX_BT_NOCOUNTER));
	else if (f.chits > 1) swprintf_s(dmgD, L"%s %s ×%d%s", TT(TX_BT_DMG), TacNum(f.cdmg), f.chits, weak2 ? L"  ×2" : L"");
	else swprintf_s(dmgD, L"%s %s%s", TT(TX_BT_DMG), TacNum(f.cdmg), weak2 ? L"  ×2" : L"");
	pair(dmgA, dmgD, bodyPx, RGBA8(255, 200, 160), f.canCounter ? RGBA8(255, 200, 160) : RGBA8(170, 170, 180), 1);
	wchar_t hitA[64], hitD[64];
	swprintf_s(hitA, L"%s %d%%   %s %d%%", TT(TX_BT_HIT), f.hit, TT(TX_BT_CRIT), f.crit);
	if (f.canCounter) swprintf_s(hitD, L"%s %d%%   %s %d%%", TT(TX_BT_HIT), f.chit, TT(TX_BT_CRIT), f.ccrit);
	else hitD[0] = 0;
	pair(hitA, hitD, smallPx, RGBA8(220, 230, 255), RGBA8(220, 230, 255), 0);
	int after = max(0, d.hp - f.dmg * f.hits);
	int aAfter = f.canCounter ? max(0, a.hp - f.cdmg * f.chits) : a.hp;
	wchar_t aftA[64], aftD[64];
	swprintf_s(aftA, L"%s → %s", TacNum(a.hp), TacNum(aAfter));
	swprintf_s(aftD, L"%s → %s", TacNum(d.hp), TacNum(after));
	pair(aftA, aftD, smallPx, aAfter == 0 ? RGBA8(255, 120, 100) : RGBA8(200, 200, 200), after == 0 ? RGBA8(255, 120, 100) : RGBA8(200, 200, 200), 1);
	if (a.team == TEAM_PLAYER && d.team == TEAM_ENEMY) {
		int pct = FoeMasteryPct(FoeKills(d.monster));
		if (pct > 0 && cy + rowH(tinyPx) <= bot) {
			wchar_t ms[64];
			swprintf_s(ms, L"%s +%d%%", FoeMasteryWord(), pct);
			m_r.HudText(ms, x + w * .5f, cy + rowH(tinyPx) * .5f, FitTextPx(ms, w - U(16), tinyPx, 0), RGBA8(255, 214, 120), 1, 0);
			cy += rowH(tinyPx);
		}
	}
	if (cy + rowH(smallPx) <= bot) {
		wchar_t faceL[96];
		int fp = f.face == 2 ? 25 : (f.face == 1 ? 10 : -10);
		int hs = BattleHeightSteps(m_b, a.x, a.z, d.x, d.z);
		const wchar_t* face = TT(f.face == 2 ? TX_BT_FACE_BACK : (f.face == 1 ? TX_BT_FACE_SIDE : TX_BT_FACE_FRONT));
		if (hs > 0) swprintf_s(faceL, L"%s %+d%%  %s", face, fp, TT(TX_HIGH));
		else if (hs < 0) swprintf_s(faceL, L"%s %+d%%  %s", face, fp, TT(TX_LOW));
		else swprintf_s(faceL, L"%s %+d%%", face, fp);
		m_r.HudText(faceL, x + w * .5f, cy + rowH(smallPx) * .5f, FitTextPx(faceL, w - U(16), smallPx, 0), RGBA8(255, 230, 170), 1, 0);
		cy += rowH(smallPx);
	}
	if (cy + rowH(tinyPx) <= bot) {
		const TerrainDef& td = TerrainInfo(m_b.map.At(d.x, d.z).t);
		if (td.defPct || td.avoPct) {
			wchar_t ter[96];
			swprintf_s(ter, L"%s  %s+%d%%  AVO+%d%%", TT(td.nameTx), TT(TX_ST_DEF), td.defPct, td.avoPct);
			m_r.HudText(ter, x + w * .5f, cy + rowH(tinyPx) * .5f, FitTextPx(ter, w - U(16), tinyPx, 0), RGBA8(200, 220, 200), 1, 0);
		}
	}
	if (opt > 0) drawExp(x, y + h - expBlock, w);
}

static const int kItemThrow = 3;

static BOOL ItemHarms(const GoodDef* g)
{
	return g && (g->item == IT_DELAY || g->extra == IX_SLOW || g->extra == IX_VENOM || g->extra == 107 || g->extra == 108);
}

static BOOL ItemThrowOk(const GoodDef* g, const Unit& src, const Unit& t)
{
	if (!g) return FALSE;
	int dist = 9999;
	int sa = UnitSpan(src), st = UnitSpan(t);
	for (int az = 0; az < sa; az++) for (int ax = 0; ax < sa; ax++)
		for (int dz = 0; dz < st; dz++) for (int dx = 0; dx < st; dx++)
			dist = min(dist, max(abs((src.x + ax) - (t.x + dx)), abs((src.z + az) - (t.z + dz))));
	if (dist > kItemThrow) return FALSE;
	if (g->item == IT_RAISE || g->extra == 106)
		return t.team == src.team && !t.alive && t.hp <= 0 && dist >= 0;
	if (ItemHarms(g))
		return t.team != src.team && t.alive && dist >= 1;
	return t.team == src.team && t.alive;
}

static BOOL ItemHelps(const Battle& b, const GoodDef* g, const Unit& src, const Unit& t)
{
	if (!ItemThrowOk(g, src, t)) return FALSE;
	if (g->item == IT_RAISE || g->extra == 106) return TRUE;
	BOOL hurt = t.alive && t.hp < t.ch.stat[S_HP];
	BOOL mana = t.alive && t.mp < t.ch.stat[S_MP];
	BOOL ail = t.poison > 0 || t.slow > 0 || t.sleep > 0 || t.stun > 0 || t.confuse > 0 || t.defDown > 0
		|| t.freeze > 0 || t.burn > 0 || t.bleed > 0 || t.blind > 0 || t.silence > 0;
	if (g->item == IT_ELIXIR) return hurt;
	if (g->extra == IX_FULL) return hurt;
	if (g->extra == IX_CURE || g->extra == 109) return hurt || t.poison > 0;
	if (g->extra == IX_ETHER || g->extra == 104) return mana;
	if (g->extra == 100) return t.sleep > 0 || hurt;
	if (g->extra == 101) return t.confuse > 0;
	if (g->extra == 102) return t.stun > 0;
	if (g->extra == 103) return ail || hurt;
	if (g->extra == 105) return hurt || mana;
	BOOL noSlow = UnitHasPas(t, PAS_NOSLOW) || UnitAilmentHeld(t);
	BOOL noPoi = UnitHasPas(t, PAS_NOPOISON);
	if (g->item == IT_MIGHT) return t.mulTurns < 3;
	if (g->item == IT_DELAY) return !noSlow && t.ct > 0;
	if (g->extra == IX_ATK) return t.buffAtk < 6;
	if (g->extra == IX_DEF) return t.buffDef < 6;
	if (g->extra == IX_AGI) return t.buffAgi < 6;
	if (g->extra == IX_SLOW) return !noSlow && (t.ct > 0 || t.slow < 3);
	if (g->extra == IX_VENOM || g->extra == 107) return !noPoi && t.poison <= 0;
	if (g->extra == 108) return !noSlow && (t.ct > 0 || t.slow < 4);
	if (g->extra == 110) return t.buffDef < 5;
	if (g->extra == 111) return t.buffAgi < 5 || t.ct < 980;
	if (g->extra == 112) return t.burn > 0;
	if (g->extra == 113) return t.freeze > 0;
	return FALSE;
}

static const wchar_t* RaiseNoRoomWord()
{
	return LangPick14(
		L"蘇生できる場所がないので蘇生できない",
		L"Nowhere to put them, so they cannot be raised.",
		L"Aucune case libre, donc impossible de les relever.",
		L"Nessuno spazio, quindi non si possono rialzare.",
		L"No hay sitio, así que no se les puede levantar.",
		L"일으킬 자리가 없어 소생할 수 없다.",
		L"没有可站的格子，所以无法复苏。",
		L"لا مكان لإيقافهم، لذا لا يمكن إحياؤهم.",
		L"Нет клетки, поэтому поднять нельзя.",
		L"Kein Feld frei, darum kein Beleben.",
		L"Sem casa livre, então não dá para erguer.",
		L"Geen vak vrij, dus niet op te wekken.",
		L"Brak pola, więc nie da się wskrzesić.",
		L"Yer yok, bu yüzden kaldırılamaz.");
}

void Game::UseConsumable(int good, int target)
{
	const GoodDef* g = GoodAt(good);
	if (!g || m_cur < 0) return;
	if (target < 0 || target >= m_b.nUnits) return;
	Unit& self = m_b.u[m_cur];
	if (!ItemThrowOk(g, self, m_b.u[target])) return;
	if ((g->item == IT_RAISE || g->extra == 106) && !RaisePlaceable(m_b, m_b.u[target])) {
		AddBanner(RaiseNoRoomWord(), NULL, RGBA8(255, 180, 160), 2, 1.8f, MSG_DROP);
		return;
	}
	if (!GoodTake(m_save, good)) return;
	Unit* dst = &m_b.u[target];
	if (g->item == IT_ELIXIR) {
		dst->hp = min(dst->ch.stat[S_HP], dst->hp + max(1, dst->ch.stat[S_HP] / 2));
	} else if (g->item == IT_MIGHT) {
		dst->mulTurns = max(dst->mulTurns, 3);
	} else if (g->item == IT_DELAY) {
		if (!UnitHasPas(*dst, PAS_NOSLOW) && !UnitAilmentHeld(*dst)) dst->ct = max(0, dst->ct - 800);
	} else if (g->item == IT_RAISE || g->extra == 106) {
		if (SettleRaised(m_b, *dst)) {
			dst->alive = 1;
			dst->killed = 0;
			dst->hp = max(1, dst->ch.stat[S_HP] * (g->extra == 106 ? 80 : 45) / 100);
			dst->dissolve = 0;
			dst->ct = max(dst->ct, 200);
			dst->flash = 1.f;
			if (target >= 0) {
				m_dieT[target] = -1;
				m_dispHp[target] = (float)dst->hp;
				ActorPlay(m_act[target], CL_SPAWN, 0, .8f);
				Burst(v3(dst->px, dst->py + .5f, dst->pz), 22, 2.4f, .1f, 1.1f, 1.f, .92f, .55f, 2, 1, -1.2f, 0, .9f);
			}
		}
	} else if (g->extra == IX_FULL) {
		dst->hp = dst->ch.stat[S_HP];
	} else if (g->extra == IX_ATK) { dst->buffAtk += 3; if (dst->buffAtk > 6) dst->buffAtk = 6; }
	else if (g->extra == IX_DEF) { dst->buffDef += 3; if (dst->buffDef > 6) dst->buffDef = 6; }
	else if (g->extra == IX_AGI) { dst->buffAgi += 3; if (dst->buffAgi > 6) dst->buffAgi = 6; }
	else if (g->extra == IX_SLOW) {
		if (!UnitHasPas(*dst, PAS_NOSLOW) && !UnitAilmentHeld(*dst)) {
			dst->ct = max(0, dst->ct - 500);
			dst->slow = m_rng.Range(1, 3);
		}
	} else if (g->extra == IX_VENOM) {
		if (!UnitHasPas(*dst, PAS_NOPOISON) && dst->poison <= 0) dst->poison = m_rng.Range(1, 3);
	}
	else if (g->extra == IX_CURE) {
		dst->poison = 0;
		dst->hp = min(dst->ch.stat[S_HP], dst->hp + max(1, dst->ch.stat[S_HP] / 4));
	} else if (g->extra == IX_ETHER) {
		dst->mp = min(dst->ch.stat[S_MP], dst->mp + max(1, dst->ch.stat[S_MP] / 2));
	} else if (g->extra == 100) {
		dst->sleep = 0;
		dst->hp = min(dst->ch.stat[S_HP], dst->hp + max(1, dst->ch.stat[S_HP] * 15 / 100));
	} else if (g->extra == 101) {
		dst->confuse = 0;
	} else if (g->extra == 102) {
		dst->stun = 0;
	} else if (g->extra == 103) {
		dst->poison = 0;
		dst->slow = 0;
		dst->sleep = 0;
		dst->stun = 0;
		dst->confuse = 0;
		dst->defDown = 0;
		dst->freeze = 0;
		dst->burn = 0;
		dst->bleed = 0;
		dst->blind = 0;
		dst->silence = 0;
		dst->hp = min(dst->ch.stat[S_HP], dst->hp + max(1, dst->ch.stat[S_HP] / 5));
	} else if (g->extra == 104) {
		dst->mp = dst->ch.stat[S_MP];
	} else if (g->extra == 105) {
		dst->hp = min(dst->ch.stat[S_HP], dst->hp + max(1, dst->ch.stat[S_HP] * 30 / 100));
		dst->mp = min(dst->ch.stat[S_MP], dst->mp + max(1, dst->ch.stat[S_MP] * 30 / 100));
	} else if (g->extra == 107) {
		if (!UnitHasPas(*dst, PAS_NOPOISON) && dst->poison <= 0) dst->poison = 5;
	} else if (g->extra == 108) {
		if (!UnitHasPas(*dst, PAS_NOSLOW) && !UnitAilmentHeld(*dst)) {
			dst->ct = max(0, dst->ct - 700);
			dst->slow = 4;
		}
	} else if (g->extra == 109) {
		dst->poison = 0;
		dst->hp = min(dst->ch.stat[S_HP], dst->hp + max(1, dst->ch.stat[S_HP] * 35 / 100));
	} else if (g->extra == 110) {
		dst->buffDef = max(dst->buffDef, 5);
	} else if (g->extra == 111) {
		dst->buffAgi = max(dst->buffAgi, 5);
		dst->ct += 200;
	} else if (g->extra == 112) {
		dst->burn = 0;
		dst->hp = min(dst->ch.stat[S_HP], dst->hp + max(1, dst->ch.stat[S_HP] * 15 / 100));
	} else if (g->extra == 113) {
		dst->freeze = 0;
	}
	dst->flash = 1.f;
	AddPopup(v3(dst->px, dst->py + 1.f, dst->pz), TT(g->nameTx), RGBA8(140, 230, 170), 1, MSG_FACE);
	AddLog(TT(g->nameTx), RGBA8(140, 230, 170));
	if (g->item == IT_RAISE || g->extra == 106) TacSfx(TS_RAISE);
	else if (g->item == IT_ELIXIR) TacSfx(TS_HEAL);
	else if (g->item == IT_MIGHT) TacSfx(TS_BUFF);
	else if (g->item == IT_DELAY || g->extra == IX_SLOW || g->extra == 108) TacSfx(TS_SLOW);
	else if (g->extra == IX_VENOM || g->extra == 107) TacSfx(TS_POISON);
	else if (g->extra == IX_CURE || (g->extra >= 100 && g->extra <= 103) || g->extra == 109 || g->extra == 112 || g->extra == 113) TacSfx(TS_CLEANSE);
	else if (g->extra == IX_FULL || g->extra == IX_ETHER || g->extra == 104 || g->extra == 105) TacSfx(TS_HEAL);
	else TacSfx(TS_BUFF);
	m_b.RebuildOcc();
	m_itemAim = -1;
	int who = m_cur;
	FinishUnitTurn(1);
	/* 道具は消費が重い代わりに、次かその次に手番が戻る（遅れ 0〜20）。 */
	if (who >= 0 && who < m_b.nUnits && m_b.u[who].alive)
		m_b.u[who].ct = 1000 - m_rng.Range(0, 20);
}

void Game::DrawBattleStatus(BOOL edit)
{
	int who = m_statWho;
	if (who < 0 || who >= m_b.nUnits) who = m_cur;
	if (who < 0 || who >= m_b.nUnits) {
		if (edit) { m_statOpen = 0; m_statEq = 0; m_statWho = -1; }
		return;
	}
	Unit& u = m_b.u[who];
	float pw = min(U(640.f), (float)m_w - U(40));
	float pathRoom = 0.f;
	if (u.team == TEAM_PLAYER) {
		wchar_t jlines[8][120];
		int jn = SplitJobPath(m_r, u.ch, pw - U(32), U(13), 6, jlines);
		pathRoom = jn * (U(13) + 4.f);
	}
	float ph = min(U(520.f) + pathRoom, (float)m_h - U(24));
	float px = ((float)m_w - pw) * .5f, py = ((float)m_h - ph) * .5f;
	if (edit) {
		m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 120));
		if (m_click && !Hit(px, py, pw, ph)) {
			m_click = FALSE;
			if (m_statEq) m_statEq = 0;
			else { m_statOpen = 0; m_statEq = 0; }
			TacSfx(TS_CANCEL);
			return;
		}
	}
	Panel(px, py, pw, ph);
	wchar_t nm[80];
	UnitTitle(u, nm, 64);
	if (!u.alive) {
		wchar_t ko[96];
		swprintf_s(ko, L"%s  %s", nm, TT(TX_RS_KO));
		wcscpy_s(nm, ko);
	}
	m_r.HudText(nm, px + U(16), py + U(22), U(18), u.alive ? RGBA8(255, 255, 255) : RGBA8(255, 170, 160), 0, 1);
	m_r.HudText(TT(TX_BT_STATUS), px + pw - U(16), py + U(22), U(16), RGBA8(255, 226, 150), 2, 1);
	auto syncCraft = [&]() {
		int cp = u.cp;
		for (int k = 0; k < CRAFT_SLOT_N; k++) {
			int b = EquippedBeadId(u.ch, k);
			u.craftEq[k] = b >= 1 ? CRAFT_BASE + b - 1 : 0;
		}
		u.cp = cp;
	};
	auto syncGear = [&]() {
		if (u.rosterIdx >= 0 && u.rosterIdx < MAX_ROSTER) {
			m_save.roster[u.rosterIdx].reserved[6] = u.ch.reserved[6];
			m_save.roster[u.rosterIdx].reserved[7] = u.ch.reserved[7];
		}
		float t = m_act[who].t;
		ActorForUnit(who);
		if (u.alive) m_act[who].t = t;
		else { ActorPlay(m_act[who], CL_DEATH, 0, 0.f); m_act[who].t = 8.f; }
	};
	if (edit && (m_statEq == 5 || m_statEq == 6)) {
		int slot = m_statEq - 5;
		m_r.HudText(TT(TX_PAS), px + pw * .5f, py + U(48), U(16), RGBA8(255, 226, 150), 1, 1);
		int ids[32], n = 0;
		int other = EquippedPasId(u.ch, slot ^ 1);
		int cur = EquippedPasId(u.ch, slot);
		for (int i = 0; i < PasCount() && n < 32; i++) {
			if (i + 1 == other) continue;
			if (PasSpare(u.ch, slot, i) > 0 || cur == i + 1) ids[n++] = i + 1;
		}
		const int per = 6;
		int pages = max(1, (n + per - 1) / per);
		if (m_statEqPage >= pages) m_statEqPage = pages - 1;
		if (m_statEqPage < 0) m_statEqPage = 0;
		float yy = py + U(70);
		auto syncPas = [&]() {
			for (int k = 0; k < 2; k++) u.pasEq[k] = EquippedPasId(u.ch, k);
			float t = m_act[who].t;
			ActorForUnit(who);
			if (u.alive) m_act[who].t = t;
			else { ActorPlay(m_act[who], CL_DEATH, 0, 0.f); m_act[who].t = 8.f; }
		};
		if (Btn(ID_BT_EQNONE, px + U(16), yy, pw - U(32), U(32), TT(TX_PT_NONE))) {
			EquipPasId(u.ch, slot, 0);
			syncPas();
			Persist();
			return;
		}
		yy += U(36);
		int a = m_statEqPage * per;
		int b = min(n, a + per);
		for (int s = 0; s < b - a; s++) {
			int id = ids[a + s];
			wchar_t row[160];
			swprintf_s(row, L"%s", PasName(id));
			m_btnSfx = -2;
			if (Btn(ID_BT_EQROW + s, px + U(16), yy, pw - U(32), U(32), row, TRUE, id == cur, U(13))) {
				if (!EquipPasId(u.ch, slot, id)) { TacSfx(TS_ERROR); return; }
				syncPas();
				Persist();
				TacSfx(TS_OK_EQUIP);
				return;
			}
			yy += U(36);
		}
		if (cur > 0) m_r.HudTextBox(PasDesc(cur), px + U(16), py + ph - U(96), pw - U(32), U(40), U(12), RGBA8(210, 220, 235));
		if (pages > 1) {
			wchar_t pg[32];
			swprintf_s(pg, L"%d / %d", m_statEqPage + 1, pages);
			if (Btn(ID_BT_EQPREV, px + U(16), py + ph - U(46), U(64), U(32), L"◀")) m_statEqPage = (m_statEqPage + pages - 1) % pages;
			m_r.HudText(pg, px + U(120), py + ph - U(30), U(14), RGBA8(220, 220, 230), 0, 1);
			if (Btn(ID_BT_EQNEXT, px + U(168), py + ph - U(46), U(64), U(32), L"▶")) m_statEqPage = (m_statEqPage + 1) % pages;
		}
		if (Btn(ID_BT_BACK, px + pw - U(170), py + ph - U(46), U(150), U(32), TT(TX_CR_BACK))) m_statEq = 0;
		return;
	}
	if (edit && m_statEq >= 9 && m_statEq <= 12) {
		BOOL summon = m_statEq >= 11;
		int slot = summon ? m_statEq - 11 : m_statEq - 9;
		m_r.HudText(summon ? SummonWord() : TT(TX_ACC), px + pw * .5f, py + U(48), U(16), RGBA8(255, 226, 150), 1, 1);
		int ids[96], n = 0;
		int cur = summon ? EquippedSummonId(u.ch, slot) : EquippedAccId(u.ch, slot);
		int capN = summon ? SummonCount() : AccCount();
		for (int i = 0; i < capN && n < 96; i++) {
			int spare = summon ? SummonSpare(u.ch, slot, i) : AccSpare(u.ch, slot, i);
			if (spare > 0 || cur == i + 1) ids[n++] = i + 1;
		}
		const int per = 6;
		int pages = max(1, (n + per - 1) / per);
		if (m_statEqPage >= pages) m_statEqPage = pages - 1;
		if (m_statEqPage < 0) m_statEqPage = 0;
		float yy = py + U(70);
		auto syncAcc = [&]() {
			for (int k = 0; k < 2; k++) u.accEq[k] = EquippedAccId(u.ch, k);
			int hpMax = max(1, UnitStat(u, S_HP));
			int mpMax = max(0, UnitStat(u, S_MP));
			if (u.hp > hpMax) u.hp = hpMax;
			if (u.mp > mpMax) u.mp = mpMax;
			m_dispHp[who] = (float)u.hp;
			if (who == m_cur && m_phase == PH_P_MOVE) { ComputeReach(m_b, who, m_reach); ComputeOverlays(); }
		};
		if (Btn(ID_BT_EQNONE, px + U(16), yy, pw - U(32), U(32), TT(TX_PT_NONE))) {
			BOOL ok = summon ? EquipSummonId(u.ch, slot, 0) : EquipAccId(u.ch, slot, 0);
			if (!ok) { TacSfx(TS_ERROR); return; }
			if (summon) LoadBattleCast();
			else syncAcc();
			Persist();
			return;
		}
		yy += U(36);
		int a = m_statEqPage * per;
		int b = min(n, a + per);
		int hov = cur;
		for (int s = 0; s < b - a; s++) {
			int id = ids[a + s];
			wchar_t row[180];
			if (summon) {
				int spare = SummonSpare(u.ch, slot, id - 1) + (id == cur ? 1 : 0);
				swprintf_s(row, L"%s  x%d  %sG", SummonName(id), max(1, spare), TacNum(SummonPrice(id - 1)));
			} else {
				wchar_t st[96];
				st[0] = 0;
				for (int stat = S_HP; stat < S_COUNT; stat++) {
					int v = AccStat(id, stat);
					if (!v) continue;
					wchar_t bit[24];
					swprintf_s(bit, L" %s%+d", TT(StatNameTx(stat)), v);
					if (wcslen(st) + wcslen(bit) < 90) wcscat_s(st, bit);
				}
				swprintf_s(row, L"%s  x%d%s", AccName(id), max(1, AccSpare(u.ch, slot, id - 1)), st);
			}
			if (Hit(px + U(16), yy, pw - U(32), U(32)) || (m_navMode && m_focusId == ID_BT_EQROW + s)) hov = id;
			m_btnSfx = -2;
			if (Btn(ID_BT_EQROW + s, px + U(16), yy, pw - U(32), U(32), row, TRUE, id == cur, U(13))) {
				BOOL ok = summon ? EquipSummonId(u.ch, slot, id) : EquipAccId(u.ch, slot, id);
				if (!ok) { TacSfx(TS_ERROR); return; }
				if (summon) LoadBattleCast();
				else syncAcc();
				Persist();
				TacSfx(TS_OK_EQUIP);
				return;
			}
			yy += U(36);
		}
		if (summon && hov > 0) {
			wchar_t est[160];
			float by = py + ph - U(96);
			if (EstimateText(u, SUMMON_BASE + hov - 1, est, 160)) {
				m_r.HudText(est, px + U(16), by - U(4), FitTextPx(est, pw - U(32), U(13), 0), RGBA8(255, 214, 140), 0, 0);
			}
			m_r.HudTextBox(SummonDesc(hov), px + U(16), by, pw - U(32), U(40), U(12), RGBA8(210, 220, 235));
		}
		if (pages > 1) {
			wchar_t pg[32];
			swprintf_s(pg, L"%d / %d", m_statEqPage + 1, pages);
			if (Btn(ID_BT_EQPREV, px + U(16), py + ph - U(46), U(64), U(32), L"◀")) m_statEqPage = (m_statEqPage + pages - 1) % pages;
			m_r.HudText(pg, px + U(120), py + ph - U(30), U(14), RGBA8(220, 220, 230), 0, 1);
			if (Btn(ID_BT_EQNEXT, px + U(168), py + ph - U(46), U(64), U(32), L"▶")) m_statEqPage = (m_statEqPage + 1) % pages;
		}
		if (Btn(ID_BT_BACK, px + pw - U(170), py + ph - U(46), U(150), U(32), TT(TX_CR_BACK))) m_statEq = 0;
		return;
	}
	int beadSlot = m_statEq == 3 ? 0 : m_statEq == 4 ? 1 : m_statEq == 7 ? 2 : m_statEq == 8 ? 3 : -1;
	if (edit && (m_statEq == 1 || m_statEq == 2 || beadSlot >= 0)) {
		int gearKind = (m_statEq == 1 || m_statEq == 2) ? (m_statEq == 1 ? 0 : 1) : -1;
		const wchar_t* title = gearKind >= 0 ? TT(gearKind ? TX_GEAR_ARM : TX_GEAR_WPN) : (beadSlot == 3 ? TT(TX_OUGI) : TT(TX_BEAD));
		m_r.HudText(title, px + pw * .5f, py + U(48), U(16), RGBA8(255, 226, 150), 1, 1);
		int ids[96], n = 0;
		if (gearKind >= 0) {
			for (int i = 0; i < m_save.gearN && n < 96; i++) {
				const GearDef* g = GearOf(m_save.gearId[i]);
				if (!g || g->kind != gearKind || !GearCanWear(u.ch.cls, g)) continue;
				ids[n++] = i;
			}
		} else {
			int cur = EquippedBeadId(u.ch, beadSlot);
			for (int i = 0; i < CRAFT_N && n < 96; i++) {
				if (BeadSpare(u.ch, beadSlot, i) > 0 || cur == i + 1) ids[n++] = i + 1;
			}
		}
		const int per = 6;
		int pages = max(1, (n + per - 1) / per);
		if (m_statEqPage >= pages) m_statEqPage = pages - 1;
		if (m_statEqPage < 0) m_statEqPage = 0;
		float yy = py + U(70);
		if (Btn(ID_BT_EQNONE, px + U(16), yy, pw - U(32), U(32), TT(TX_PT_NONE))) {
			if (gearKind >= 0) {
				int sl = gearKind ? 7 : 6;
				if (u.ch.reserved[sl] <= 0) return;
				if (!StashAdd(m_save, u.ch.reserved[sl])) { TacSfx(TS_ERROR); return; }
				u.ch.reserved[sl] = 0;
				syncGear();
			} else {
				EquipBeadId(u.ch, beadSlot, 0);
				syncCraft();
			}
			Persist();
			return;
		}
		yy += U(36);
		int a = m_statEqPage * per;
		int b = min(n, a + per);
		wchar_t rowTip[480];
		rowTip[0] = 0;
		float rtx = 0, rty = 0;
		BOOL rowHot = FALSE;
		for (int s = 0; s < b - a; s++) {
			int ix = a + s;
			wchar_t row[180];
			if (gearKind >= 0) swprintf_s(row, L"%s", GearName(m_save.gearId[ids[ix]]));
			else {
				const SkillDef& sd = SkillInfo(CRAFT_BASE + ids[ix] - 1);
				wchar_t cp[24];
				CraftCostText(ids[ix] - 1, cp, 24);
				swprintf_s(row, L"%s  %s  %s%s", CraftName(ids[ix] - 1), CraftShapeName(sd.area, sd.shape), cp, IsCraftSuper(CRAFT_BASE + ids[ix] - 1) ? L" ★" : L"");
			}
			if (m_mouseSeen && Hit(px + U(16), yy, pw - U(32), U(32))) {
				if (gearKind >= 0) {
					const GearDef* gg = GearOf(m_save.gearId[ids[ix]]);
					wchar_t body[360];
					body[0] = 0;
					if (gg) GearTipText(gg, body, 360);
					swprintf_s(rowTip, L"%s%s%s", row, body[0] ? L"\n" : L"", body);
				} else wcsncpy_s(rowTip, CraftDesc(ids[ix] - 1), _TRUNCATE);
				rowHot = rowTip[0] != 0;
				rtx = px + U(16); rty = yy;
			}
			m_btnSfx = -2;
			if (Btn(ID_BT_EQROW + s, px + U(16), yy, pw - U(32), U(32), row, TRUE, FALSE, U(13))) {
				if (gearKind >= 0) { GearEquip(u.ch, m_save, ids[ix]); syncGear(); }
				else if (!EquipBeadId(u.ch, beadSlot, ids[ix])) { TacSfx(TS_ERROR); return; }
				else syncCraft();
				Persist();
				TacSfx(TS_OK_EQUIP);
				return;
			}
			yy += U(36);
		}
		if (rowHot) DrawHoverNote(m_r, rtx, rty, pw - U(32), U(32), m_ui, m_w, m_h, rowTip);
		if (pages > 1) {
			wchar_t pg[32];
			swprintf_s(pg, L"%d / %d", m_statEqPage + 1, pages);
			if (Btn(ID_BT_EQPREV, px + U(16), py + ph - U(46), U(64), U(32), L"◀")) m_statEqPage = (m_statEqPage + pages - 1) % pages;
			m_r.HudText(pg, px + U(120), py + ph - U(30), U(14), RGBA8(220, 220, 230), 0, 1);
			if (Btn(ID_BT_EQNEXT, px + U(168), py + ph - U(46), U(64), U(32), L"▶")) m_statEqPage = (m_statEqPage + 1) % pages;
		}
		if (Btn(ID_BT_BACK, px + pw - U(170), py + ph - U(46), U(150), U(32), TT(TX_CR_BACK))) m_statEq = 0;
		return;
	}
	int fightPlus[S_COUNT];
	for (int s = 0; s < S_COUNT; s++) fightPlus[s] = UnitStat(u, s) - u.ch.stat[s];
	float yy = py + ph - U(276);
	float statY = py + U(44);
	if (u.team == TEAM_PLAYER) {
		float lineH = U(13) + 4.f;
		float statH = U(20) * 5.f + U(8);
		float budget = yy - U(8) - statY - statH;
		int maxLines = (int)(budget / lineH);
		if (maxLines < 0) maxLines = 0;
		if (maxLines > 6) maxLines = 6;
		if (maxLines > 0) {
			float pathH = DrawJobPath(m_r, u.ch, px + U(16), statY, pw - U(32), U(13), RGBA8(210, 220, 235), maxLines);
			if (pathH > 0) statY += pathH + U(4);
		}
	}
	if (statY + U(20) < yy - U(4))
		DrawStatLines(u.ch, px + U(16), statY, pw - U(32), fightPlus, u.hp, u.mp);
	const GearDef* wpn = GearOf(u.ch.reserved[6]);
	const GearDef* arm = GearOf(u.ch.reserved[7]);
	wchar_t gl[96];
	wchar_t slotBuf[520];
	const wchar_t* slotTip = NULL;
	float slotAx = 0, slotAy = 0, slotAw = 0;
	auto slot = [&](int id, float sx, float sy, float sw, const wchar_t* s, int eq) {
		if (m_mouseSeen && Hit(sx, sy, sw, U(32))) {
			slotBuf[0] = 0;
			if (eq == 1 && wpn) {
				wchar_t body[360];
				GearTipText(wpn, body, 360);
				swprintf_s(slotBuf, L"%s%s%s", GearName(u.ch.reserved[6]), body[0] ? L"\n" : L"", body);
			} else if (eq == 2 && arm) {
				wchar_t body[360];
				GearTipText(arm, body, 360);
				swprintf_s(slotBuf, L"%s%s%s", GearName(u.ch.reserved[7]), body[0] ? L"\n" : L"", body);
			} else if (eq == 9 || eq == 10) AccTipText(EquippedAccId(u.ch, eq == 9 ? 0 : 1), slotBuf, 520);
			else if (eq == 3 || eq == 4 || eq == 7 || eq == 8) {
				int beadSlot = eq == 3 ? 0 : eq == 4 ? 1 : eq == 7 ? 2 : 3;
				int bead = EquippedBeadId(u.ch, beadSlot);
				if (bead > 0) {
					swprintf_s(slotBuf, L"%s\n%s", CraftName(bead - 1), CraftDesc(bead - 1));
				}
			} else if (eq == 5 || eq == 6) {
				int pas = EquippedPasId(u.ch, eq == 5 ? 0 : 1);
				if (pas > 0) swprintf_s(slotBuf, L"%s\n%s", PasName(pas), PasDesc(pas));
			} else if (eq == 11 || eq == 12) {
				int sm = EquippedSummonId(u.ch, eq == 11 ? 0 : 1);
				if (sm > 0) swprintf_s(slotBuf, L"%s\n%s", SummonName(sm), SummonDesc(sm));
			}
			if (!slotBuf[0]) wcsncpy_s(slotBuf, TT(TX_PT_NONE), _TRUNCATE);
			slotTip = slotBuf;
			slotAx = sx; slotAy = sy; slotAw = sw;
		}
		if (!edit) {
			m_r.HudGlass(sx, sy, sw, U(32), RGBA8(44, 48, 72, 230), RGBA8(120, 108, 78), 1.f);
			m_r.HudText(s, sx + sw * .5f, sy + U(16), FitTextPx(s, sw - U(12), U(12), 0), RGBA8(236, 232, 220), 1, 1);
			return;
		}
		if (Btn(id, sx, sy, sw, U(32), s, TRUE, FALSE, U(12))) { m_statEq = eq; m_statEqPage = 0; }
	};
	swprintf_s(gl, L"%s: %s", TT(TX_GEAR_WPN), wpn ? GearName(u.ch.reserved[6]) : TT(TX_PT_NONE));
	slot(ID_BT_EQ0, px + U(16), yy, pw * .48f, gl, 1);
	swprintf_s(gl, L"%s: %s", TT(TX_GEAR_ARM), arm ? GearName(u.ch.reserved[7]) : TT(TX_PT_NONE));
	slot(ID_BT_EQ0 + 1, px + pw * .52f, yy, pw * .44f, gl, 2);
	yy += U(36);
	swprintf_s(gl, L"%s1: %s", TT(TX_ACC), EquippedAccName(u.ch, 0));
	slot(ID_BT_ACC0, px + U(16), yy, pw * .48f, gl, 9);
	swprintf_s(gl, L"%s2: %s", TT(TX_ACC), EquippedAccName(u.ch, 1));
	slot(ID_BT_ACC1, px + pw * .52f, yy, pw * .44f, gl, 10);
	yy += U(36);
	swprintf_s(gl, L"%s1: %s", TT(TX_CRAFT), EquippedBeadName(u.ch, 0));
	slot(ID_BT_EQ0 + 2, px + U(16), yy, pw * .48f, gl, 3);
	swprintf_s(gl, L"%s2: %s", TT(TX_CRAFT), EquippedBeadName(u.ch, 1));
	slot(ID_BT_EQ0 + 3, px + pw * .52f, yy, pw * .44f, gl, 4);
	yy += U(36);
	swprintf_s(gl, L"%s3: %s", TT(TX_CRAFT), EquippedBeadName(u.ch, 2));
	slot(ID_BT_BEAD2, px + U(16), yy, pw * .48f, gl, 7);
	swprintf_s(gl, L"%s: %s", TT(TX_OUGI), EquippedBeadName(u.ch, 3));
	slot(ID_BT_BEAD3, px + pw * .52f, yy, pw * .44f, gl, 8);
	yy += U(36);
	swprintf_s(gl, L"%s1: %s", TT(TX_PAS), EquippedPasName(u.ch, 0));
	slot(ID_BT_PAS0, px + U(16), yy, pw * .48f, gl, 5);
	swprintf_s(gl, L"%s2: %s", TT(TX_PAS), EquippedPasName(u.ch, 1));
	slot(ID_BT_PAS1, px + pw * .52f, yy, pw * .44f, gl, 6);
	yy += U(36);
	swprintf_s(gl, L"%s1: %s", SummonWord(), EquippedSummonName(u.ch, 0));
	slot(ID_BT_EQSUM0, px + U(16), yy, pw * .48f, gl, 11);
	swprintf_s(gl, L"%s2: %s", SummonWord(), EquippedSummonName(u.ch, 1));
	slot(ID_BT_EQSUM1, px + pw * .52f, yy, pw * .44f, gl, 12);
	if (slotTip && slotTip[0])
		DrawHoverNote(m_r, slotAx, slotAy, slotAw, U(32), m_ui, m_w, m_h, slotTip);
	if (edit && Btn(ID_BT_BACK, px + pw - U(170), py + ph - U(46), U(150), U(32), TT(TX_CR_BACK))) { m_statOpen = 0; m_statEq = 0; }
}

static const wchar_t* FormRuleText(int form)
{
	switch (form) {
	case FM_WING: return LangPick14(L"向いている左右の、どちらかに味方がいれば使える。", L"An ally on either side of where you face.", L"Un allié d'un côté ou de l'autre, dans le sens du regard.", L"Un alleato su uno dei due lati, nel verso in cui guardi.", L"Un aliado a un lado u otro, hacia donde miras.", L"바라보는 좌우 어느 쪽에 아군이 있으면 쓸 수 있다.", L"面朝方向的左右任一侧有同伴即可使用。", L"حليف على أحد الجانبين في اتجاه نظرك.", L"Союзник слева или справа от взгляда.", L"Ein Verbündeter auf einer Seite deiner Blickrichtung.", L"Um aliado num dos lados, para onde olhas.", L"Een bondgenoot aan een van beide zijden van je blik.", L"Sojusznik po lewej lub prawej stronie spojrzenia.", L"Baktığın yönün iki yanından birinde yoldaş varsa kullanılır.");
	case FM_CROSS: return LangPick14(L"向いている左右の、両方に味方がいると使える。", L"Allies on both sides of where you face.", L"Des alliés des deux côtés, dans le sens du regard.", L"Alleati su entrambi i lati, nel verso in cui guardi.", L"Aliados a ambos lados, hacia donde miras.", L"바라보는 좌우 양쪽에 아군이 있으면 쓸 수 있다.", L"面朝方向的左右两侧都有同伴才能使用。", L"حليفان على الجانبين في اتجاه نظرك.", L"Союзники по обе стороны взгляда.", L"Verbündete auf beiden Seiten deiner Blickrichtung.", L"Aliados nos dois lados, para onde olhas.", L"Bondgenoten aan beide zijden van je blik.", L"Sojusznicy po obu stronach spojrzenia.", L"Baktığın yönün iki yanında da yoldaş varsa kullanılır.");
	case FM_WALL: return LangPick14(L"向いている左右の、両方に味方がいると使える。", L"Allies on both sides of where you face.", L"Des alliés des deux côtés, dans le sens du regard.", L"Alleati su entrambi i lati, nel verso in cui guardi.", L"Aliados a ambos lados, hacia donde miras.", L"바라보는 좌우 양쪽에 아군이 있으면 쓸 수 있다.", L"面朝方向的左右两侧都有同伴才能使用。", L"حليفان على الجانبين في اتجاه نظرك.", L"Союзники по обе стороны взгляда.", L"Verbündete auf beiden Seiten deiner Blickrichtung.", L"Aliados nos dois lados, para onde olhas.", L"Bondgenoten aan beide zijden van je blik.", L"Sojusznicy po obu stronach spojrzenia.", L"Baktığın yönün iki yanında da yoldaş varsa kullanılır.");
	case FM_BACK: return LangPick14(L"向いている真後ろに味方がいれば使える。", L"An ally directly behind where you face.", L"Un allié juste derrière, dans le sens du regard.", L"Un alleato proprio dietro, nel verso in cui guardi.", L"Un aliado justo detrás, hacia donde miras.", L"바라보는 바로 뒤에 아군이 있으면 쓸 수 있다.", L"面朝方向的正后方有同伴即可使用。", L"حليف خلفك مباشرة في اتجاه نظرك.", L"Союзник прямо за спиной по взгляду.", L"Ein Verbündeter direkt hinter deiner Blickrichtung.", L"Um aliado logo atrás, para onde olhas.", L"Een bondgenoot recht achter je blik.", L"Sojusznik tuż za plecami, w stronę spojrzenia.", L"Baktığın yönün tam arkasında yoldaş varsa kullanılır.");
	case FM_TRI: return LangPick14(L"真後ろの左右、両方に味方がいると使える。", L"Allies on both rear diagonals.", L"Des alliés sur les deux diagonales arrière.", L"Alleati sulle due diagonali dietro.", L"Aliados en las dos diagonales traseras.", L"바로 뒤의 좌우 양쪽에 아군이 있으면 쓸 수 있다.", L"正后方的左右两侧都有同伴才能使用。", L"حليفان على قطري الخلف.", L"Союзники на обеих задних диагоналях.", L"Verbündete auf beiden hinteren Diagonalen.", L"Aliados nas duas diagonais de trás.", L"Bondgenoten op beide schuine vakken achter je.", L"Sojusznicy na obu tylnych skosach.", L"Tam arkanın iki çaprazında da yoldaş varsa kullanılır.");
	case FM_CHAIN: return LangPick14(L"真後ろに味方が2人、縦に並ぶと使える。", L"Two allies in a column directly behind you.", L"Deux alliés en colonne juste derrière.", L"Due alleati in colonna proprio dietro.", L"Dos aliados en columna justo detrás.", L"바로 뒤에 아군 2명이 세로로 서면 쓸 수 있다.", L"正后方有两名同伴排成一列才能使用。", L"حليفان في عمود خلفك مباشرة.", L"Два союзника колонной прямо за спиной.", L"Zwei Verbündete in einer Kolonne direkt hinter dir.", L"Dois aliados em coluna logo atrás.", L"Twee bondgenoten in een rij recht achter je.", L"Dwóch sojuszników w kolumnie tuż za plecami.", L"Tam arkanda iki yoldaş alt alta durursa kullanılır.");
	case FM_DIA: return LangPick14(L"前方の左右、両方に味方がいると使える。", L"Allies on both front diagonals.", L"Des alliés sur les deux diagonales avant.", L"Alleati sulle due diagonali davanti.", L"Aliados en las dos diagonales delanteras.", L"전방 좌우 양쪽에 아군이 있으면 쓸 수 있다.", L"前方左右两侧都有同伴才能使用。", L"حليفان على قطري الأمام.", L"Союзники на обеих передних диагоналях.", L"Verbündete auf beiden vorderen Diagonalen.", L"Aliados nas duas diagonais da frente.", L"Bondgenoten op beide schuine vakken voor je.", L"Sojusznicy na obu przednich skosach.", L"Ön çaprazların ikisinde de yoldaş varsa kullanılır.");
	case FM_SQUARE: return LangPick14(L"上下左右の4方向のうち、3方向に味方がいると使える。", L"Allies on three of the four sides around you.", L"Des alliés sur trois des quatre côtés.", L"Alleati su tre dei quattro lati.", L"Aliados en tres de los cuatro lados.", L"상하좌우 네 방향 중 세 방향에 아군이 있으면 쓸 수 있다.", L"上下左右四个方向中有三个方向有同伴才能使用。", L"حلفاء على ثلاثة من الجوانب الأربعة.", L"Союзники с трёх из четырёх сторон.", L"Verbündete auf drei der vier Seiten.", L"Aliados em três dos quatro lados.", L"Bondgenoten aan drie van de vier zijden.", L"Sojusznicy z trzech z czterech stron.", L"Dört yönden üçünde yoldaş varsa kullanılır.");
	case FM_ECHELON: return LangPick14(L"左右どちらかで、隣とその斜め後ろに味方がいると使える。黄と水色は別の組。", L"One side: the neighbor and the ally diagonally behind. Gold and cyan are different pairs.", L"Un côté : le voisin et l'allié en diagonale derrière. Or et cyan sont deux paires.", L"Un lato: il vicino e l'alleato in diagonale dietro. Oro e ciano sono due coppie.", L"Un lado: el vecino y el aliado en diagonal detrás. Oro y cian son dos pares.", L"좌우 한쪽에서 옆과 그 대각선 뒤에 아군이 있으면 쓸 수 있다. 노랑과 청록은 다른 조.", L"左右任一侧，旁边和斜后方都有同伴才能使用。黄与青是两组。", L"جانب واحد: المجاور ومن خلفه بقطر. الذهبي والأزرق زوجان.", L"Одна сторона: сосед и союзник по диагонали сзади. Золото и циан — разные пары.", L"Eine Seite: der Nachbar und der Verbündete schräg dahinter. Gold und Cyan sind zwei Paare.", L"Um lado: o vizinho e o aliado na diagonal atrás. Ouro e ciano são pares diferentes.", L"Eén kant: de buur en de bondgenoot schuin erachter. Goud en cyaan zijn twee paren.", L"Jedna strona: sąsiad i sojusznik po skosie z tyłu. Złoty i cyjan to dwie pary.", L"Bir yanda komşu ve onun çapraz arkasında yoldaş varsa kullanılır. Sarı ve camgöbeği ayrı çifttir.");
	case FM_CIRCLE: return LangPick14(L"周囲8マスのうち、3マス以上に味方がいると使える。", L"Allies on at least three of the eight tiles around you.", L"Des alliés sur au moins trois des huit cases autour.", L"Alleati su almeno tre delle otto caselle intorno.", L"Aliados en al menos tres de las ocho casillas de alrededor.", L"주위 8칸 중 3칸 이상에 아군이 있으면 쓸 수 있다.", L"周围8格中至少3格有同伴才能使用。", L"حلفاء في ثلاث من المربعات الثمانية حولك على الأقل.", L"Союзники хотя бы на трёх из восьми клеток вокруг.", L"Verbündete auf mindestens drei der acht Felder um dich.", L"Aliados em pelo menos três das oito casas à volta.", L"Bondgenoten op minstens drie van de acht vakken om je heen.", L"Sojusznicy na co najmniej trzech z ośmiu pól wokół.", L"Çevrendeki sekiz kareden en az üçünde yoldaş varsa kullanılır.");
	case FM_PAIR: return LangPick14(L"上下左右のどこかに味方がいれば使える。", L"An ally on any of the four sides.", L"Un allié sur l'un des quatre côtés.", L"Un alleato su uno dei quattro lati.", L"Un aliado en cualquiera de los cuatro lados.", L"상하좌우 어디에든 아군이 있으면 쓸 수 있다.", L"上下左右任一方向有同伴即可使用。", L"حليف على أي جانب من الأربعة.", L"Союзник с любой из четырёх сторон.", L"Ein Verbündeter auf einer der vier Seiten.", L"Um aliado em qualquer um dos quatro lados.", L"Een bondgenoot aan een van de vier zijden.", L"Sojusznik przy dowolnym z czterech boków.", L"Dört yönden herhangi birinde yoldaş varsa kullanılır.");
	case FM_PINCER: return LangPick14(L"狙った敵の左右、両方に味方がいると使える。", L"Allies on both sides of the foe you aim at.", L"Des alliés des deux côtés de l'ennemi visé.", L"Alleati ai due lati del nemico mirato.", L"Aliados a ambos lados del enemigo apuntado.", L"노린 적의 좌우 양쪽에 아군이 있으면 쓸 수 있다.", L"瞄准的敌人左右两侧都有同伴才能使用。", L"حليفان على جانبي العدو المستهدف.", L"Союзники с обеих сторон цели.", L"Verbündete auf beiden Seiten des anvisierten Gegners.", L"Aliados nos dois lados do inimigo mirado.", L"Bondgenoten aan beide zijden van de gekozen vijand.", L"Sojusznicy po obu stronach wskazanego wroga.", L"Nişan aldığın düşmanın iki yanında da yoldaş varsa kullanılır.");
	default: return L"";
	}
}

void Game::DrawFormGuide(int skill, int tx, int tz, float avoidX, float avoidY, float avoidW, float avoidH)
{
	if (m_cur < 0 || !IsFormSkill(skill)) return;
	const Unit& u = m_b.u[m_cur];
	int form = FormOf(skill);
	FormMark mk[12];
	int nm = FormMarks(form, mk, 12);
	int fwdHi = 1, fwdLo = -1, absolute = 1;
	for (int i = 0; i < nm; i++) {
		if (mk[i].fwd > fwdHi) fwdHi = mk[i].fwd;
		if (mk[i].fwd < fwdLo) fwdLo = mk[i].fwd;
		if (!mk[i].absolute) absolute = 0;
	}
	int nRow = fwdHi - fwdLo + 1;
	if (nRow < 3) nRow = 3;
	const float cs = U(34), gap = U(4);
	float gridW = cs * 3 + gap * 2;
	float gridH = cs * nRow + gap * (nRow - 1);
	float pw = U(340);
	float top = absolute ? U(82) : U(98);
	float ph = top + gridH + U(58);
	float px = ((float)m_w - pw) * .5f;
	float py = (float)m_h - ph - U(28);
	if (avoidW > 1.f) {
		float ax1 = avoidX + avoidW, ay1 = avoidY + avoidH;
		BOOL hit = px < ax1 && px + pw > avoidX && py < ay1 && py + ph > avoidY;
		if (hit) {
			if (avoidX > (float)m_w * .5f) px = U(16);
			else px = (float)m_w - pw - U(16);
		}
	}
	if (px < 8.f) px = 8.f;
	if (py < 8.f) py = 8.f;
	{
		float right = m_hud.col > 40.f ? m_hud.col - 8.f : (float)m_w - 8.f;
		float bot = m_hud.panelTop > 40.f ? m_hud.panelTop - 8.f : (float)m_h - 8.f;
		pw = min(pw, max(100.f, right - 8.f));
		px = FitLo(px, 8.f, max(8.f, right - pw));
		if (py + ph > bot) py = bot - ph;
		if (py < 8.f) py = 8.f;
		if (py + ph > (float)m_h - 8.f) ph = max(80.f, (float)m_h - py - 8.f);
	}
	Panel(px, py, pw, ph, .94f);
	m_r.HudText(SkillLabel(skill), px + pw * .5f, py + U(18), U(16), RGBA8(255, 226, 150), 1, 1);
	m_r.HudTextBox(FormRuleText(form), px + U(12), py + U(32), pw - U(24), U(40), U(12), RGBA8(220, 226, 236));
	if (!absolute) m_r.HudText(LangPick14(L"▲ 前", L"▲ Front", L"▲ Avant", L"▲ Avanti", L"▲ Frente", L"▲ 앞", L"▲ 前", L"▲ أمام", L"▲ Вперёд", L"▲ Vorn", L"▲ Frente", L"▲ Voor", L"▲ Przód", L"▲ Ön"), px + pw * .5f, py + U(80), U(13), RGBA8(255, 220, 140), 1, 1);
	float gx = px + (pw - gridW) * .5f;
	float gy = py + top;
	BOOL aimed = (tx != u.x || tz != u.z);
	Unit view = u;
	if (!absolute && !aimed) {
		int fx, fz;
		if (FormPickAim(m_b, m_cur, skill, fx, fz)) view.face = atan2f((float)fx, (float)fz);
	}
	for (int row = 0; row < nRow; row++) {
		for (int col = 0; col < 3; col++) {
			float cx = gx + col * (cs + gap);
			float cy = gy + row * (cs + gap);
			int side = col - 1;
			int fwd = fwdHi - row;
			int mark = -1;
			for (int i = 0; i < nm; i++) if (mk[i].side == side && mk[i].fwd == fwd) { mark = i; break; }
			BOOL self = (side == 0 && fwd == 0);
			DWORD fill = RGBA8(18, 22, 34, 180);
			DWORD frame = RGBA8(70, 74, 90);
			const wchar_t* lab = L"";
			DWORD tc = RGBA8(230, 230, 236);
			if (self) {
				fill = RGBA8(46, 96, 170, 230);
				frame = RGBA8(180, 210, 255);
				lab = LangPick14(L"自", L"You", L"Soi", L"Te", L"Tú", L"나", L"自", L"أنت", L"Вы", L"Du", L"Você", L"Jij", L"Ty", L"Sen");
			} else if (mark >= 0) {
				BOOL foe = mk[mark].foe;
				BOOL filled = FALSE;
				if (!foe && !(mk[mark].onFoe && !aimed)) {
					int ox, oz;
					FormMarkPos(view, tx, tz, mk[mark], ox, oz);
					int who = Map::In(ox, oz) ? m_b.UnitAt(ox, oz) : -1;
					filled = who >= 0 && who != m_cur && m_b.u[who].alive && m_b.u[who].team == u.team;
				}
				if (foe && aimed) {
					int who = Map::In(tx, tz) ? m_b.UnitAt(tx, tz) : -1;
					filled = who >= 0 && m_b.u[who].alive && m_b.u[who].team != u.team;
				}
				if (foe) {
					fill = filled ? RGBA8(170, 56, 52, 235) : RGBA8(90, 32, 32, 220);
					frame = RGBA8(255, 140, 130);
					lab = LangPick14(L"敵", L"Foe", L"Enn.", L"Nem.", L"Enem.", L"적", L"敌", L"عدو", L"Враг", L"Feind", L"Inim.", L"Vijand", L"Wróg", L"Düşman");
				} else if (filled) {
					fill = RGBA8(42, 140, 74, 235);
					frame = RGBA8(160, 240, 180);
					lab = LangPick14(L"味", L"Ally", L"Allié", L"All.", L"Aliado", L"아군", L"友", L"حليف", L"Союз", L"Freund", L"Aliado", L"Bondg.", L"Sojusz", L"Yoldaş");
				} else {
					fill = RGBA8(48, 40, 18, 220);
					frame = mk[mark].group == 1 ? RGBA8(90, 210, 220) : RGBA8(240, 196, 70);
					lab = L"";
				}
			}
			m_r.HudRect(cx, cy, cs, cs, fill);
			m_r.HudFrame(cx, cy, cs, cs, U(2), frame);
			if (lab[0]) m_r.HudText(lab, cx + cs * .5f, cy + cs * .5f, U(13), tc, 1, 1);
		}
	}
	BOOL any = SkillUsable(m_b, m_cur, skill);
	BOOL here = FALSE;
	if (form == FM_PINCER) here = aimed ? FormFits(m_b, m_cur, skill, tx, tz) : any;
	else if (!aimed && !absolute) here = any;
	else here = FormFits(m_b, m_cur, skill, tx, tz);
	const wchar_t* st;
	DWORD sc;
	if (u.mp < SkillInfo(skill).mp) {
		st = TT(TX_BT_NOMP); sc = RGBA8(255, 150, 140);
	} else if (here) {
		int pct = FormBoostPct(m_b, m_cur, skill, tx, tz);
		st = pct > 100
			? LangPick14(L"陣形は揃っている。味方が多いほど強い。", L"Formation ready. More allies raise the power.", L"Formation prête. Plus d'alliés, plus de puissance.", L"Formazione pronta. Più alleati, più potenza.", L"Formación lista. Más aliados, más poder.", L"진형이 갖춰졌다. 아군이 많을수록 강하다.", L"阵形已齐。同伴越多越强。", L"التشكيل جاهز. مزيد من الحلفاء يرفع القوة.", L"Строй собран. Больше союзников — выше сила.", L"Formation steht. Mehr Verbündete erhöhen die Kraft.", L"Formação pronta. Mais aliados, mais poder.", L"Formatie klaar. Meer bondgenoten verhogen de kracht.", L"Szyk gotowy. Więcej sojuszników zwiększa siłę.", L"Düzen tamam. Yoldaş arttıkça güç artar.")
			: LangPick14(L"陣形は揃っている。", L"Formation ready.", L"Formation prête.", L"Formazione pronta.", L"Formación lista.", L"진형이 갖춰졌다.", L"阵形已齐。", L"التشكيل جاهز.", L"Строй собран.", L"Formation steht.", L"Formação pronta.", L"Formatie klaar.", L"Szyk gotowy.", L"Düzen tamam.");
		sc = RGBA8(140, 230, 160);
	} else if (any && form == FM_PINCER) {
		st = LangPick14(L"別の敵なら挟める。", L"Another foe can be pinched.", L"Un autre ennemi peut être pris en tenaille.", L"Un altro nemico può essere preso.", L"Otro enemigo se puede pinzar.", L"다른 적이야 끼울 수 있다.", L"换个敌人就能夹击。", L"عدو آخر يمكن قصّه.", L"Другого врага можно зажать.", L"Ein anderer Gegner lässt sich klemmen.", L"Outro inimigo pode ser pinçado.", L"Een andere vijand kan worden geklemd.", L"Innego wroga da się ścisnąć.", L"Başka düşman kıskaca alınır.");
		sc = RGBA8(255, 214, 120);
	} else if (any && !absolute) {
		st = LangPick14(L"別のマスを狙えば使える。発動時にその向きを向く。", L"Aim another tile. It turns that way when used.", L"Visez une autre case. Ça tourne à l'activation.", L"Mira un'altra casella. Si gira all'uso.", L"Apunta otra casilla. Gira al usarse.", L"다른 칸을 노리면 쓸 수 있다. 쓸 때 그 방향을 본다.", L"瞄准其他格子即可。发动时会转向。", L"صوّب خانة أخرى. يستدير عند الاستخدام.", L"Целиться в другую клетку. Повернётся при использовании.", L"Ein anderes Feld anvisieren. Dreht sich beim Einsatz.", L"Mira outra casa. Vira ao usar.", L"Richt op een ander vak. Draait bij gebruik.", L"Wskaż inne pole. Obraca się przy użyciu.", L"Başka kareye nişan al. Kullanınca o yöne döner.");
		sc = RGBA8(255, 214, 120);
	} else {
		st = TT(TX_FORM_NEED); sc = RGBA8(255, 160, 140);
	}
	m_r.HudTextBox(st, px + U(12), py + ph - U(52), pw - U(24), U(40), U(13), sc);
}

void Game::DrawCommandMenu()
{
	if (m_statOpen) return;
	if (m_cur < 0) return;
	const Unit& u = m_b.u[m_cur];
	float w = min(U(220.f), max(72.f, (float)m_w - 16.f));
	float bh = min(U(40.f), max(18.f, (float)m_h * 0.055f));
	float sx, sy;
	float x = m_w * .62f, y = m_h * .36f;
	if (m_r.ProjectToScreen(v3(u.px, u.py + m_act[m_cur].height, u.pz), sx, sy)) {
		float xHi = (float)m_w - w - 8.f;
		if (m_hud.col > w + 24.f) xHi = min(xHi, m_hud.col - w - 8.f);
		x = FitLo(sx + U(70), 8.f, max(8.f, xHi));
		y = FitLo(sy - U(80), 8.f, max(8.f, (float)m_h * 0.72f));
	}
	if (m_phase == PH_P_ITEM) {
		float iw = min(U(380.f), (float)m_w - U(24));
		float gap = U(6), margin = U(10);
		float descH = U(64);
		auto place = [&](int rows) {
			float ph = bh * rows + gap * max(0, rows - 1) + U(16) + descH + gap;
			float topMin = margin + U(8);
			float topMax = (float)m_h - margin - ph + U(8);
			if (topMax < topMin) topMax = topMin;
			y = Clampf(y, topMin, topMax);
			float xMax = (float)m_w - iw - margin;
			if (xMax < margin) xMax = margin;
			x = Clampf(x, margin, xMax);
			Panel(x - U(8), y - U(8), iw + U(16), ph);
		};
		if (m_itemAim < 0) {
			int list[64], ln = 0;
			int nGood = GoodCount();
			if (nGood > 64) nGood = 64;
			for (int i = 0; i < nGood; i++) if (GoodHave(m_save, i) > 0) list[ln++] = i;
			const int idBudget = ID_BT_ITT0 - ID_BT_IT0;
			auto fitRows = [&](int chrome) {
				float avail = (float)m_h - margin * 2.f - U(16) - descH - gap - (bh + gap) * (float)chrome;
				int nfit = (int)((avail + gap) / (bh + gap));
				if (nfit < 1) nfit = 1;
				if (nfit > idBudget) nfit = idBudget;
				if (ln > 0 && nfit > ln) nfit = ln;
				if (ln < 1) nfit = 0;
				return nfit;
			};
			int per = fitRows(1);
			int pages = ln < 1 ? 1 : max(1, (ln + max(1, per) - 1) / max(1, per));
			if (pages > 1) {
				per = fitRows(2);
				if (per < 1) per = 1;
				pages = max(1, (ln + per - 1) / per);
			}
			if (m_itemPage >= pages) m_itemPage = pages - 1;
			if (m_itemPage < 0) m_itemPage = 0;
			int a = m_itemPage * per, b = min(ln, a + per);
			int shown = b - a;
			place(shown + 1 + (pages > 1 ? 1 : 0));
			float yy = y;
			int hovGood = -1;
			for (int k = a; k < b; k++) {
				const GoodDef* gd = GoodAt(list[k]);
				wchar_t lb[80];
				swprintf_s(lb, L"%s  x%d", TT(gd->nameTx), GoodHave(m_save, list[k]));
				BOOL hot = (m_navMode && m_focusId == ID_BT_IT0 + (k - a)) || (!m_navMode && m_mouseSeen && Hit(x, yy, iw, bh));
				if (hot) hovGood = list[k];
				BOOL useful = FALSE;
				for (int i = 0; i < m_b.nUnits && !useful; i++) if (ItemHelps(m_b, gd, u, m_b.u[i])) useful = TRUE;
				if (Btn(ID_BT_IT0 + (k - a), x, yy, iw, bh, lb, useful, hot, U(14))) {
					m_itemAim = list[k]; m_aimPage = 0;
					return;
				}
				yy += bh + gap;
			}
			if (hovGood < 0 && shown > 0) hovGood = list[a];
			m_r.HudTextBox(hovGood >= 0 ? GoodDesc(hovGood) : L"", x, yy, iw, descH, U(12), RGBA8(214, 222, 234));
			yy += descH + gap;
			if (pages > 1) {
				wchar_t pg[32];
				swprintf_s(pg, L"%d / %d", m_itemPage + 1, pages);
				if (Btn(ID_BT_SKPREV, x, yy, U(64), bh, L"◀")) m_itemPage = (m_itemPage + pages - 1) % pages;
				m_r.HudText(pg, x + iw * .5f, yy + bh * .5f, U(14), RGBA8(230, 230, 240), 1, 1);
				if (Btn(ID_BT_SKNEXT, x + iw - U(64), yy, U(64), bh, L"▶")) m_itemPage = (m_itemPage + 1) % pages;
				yy += bh + gap;
			}
			m_btnSfx = -2;
			if (Btn(ID_BT_BACK, x, yy, iw, bh, TT(TX_CR_BACK))) BattleCancel();
			return;
		}
		const GoodDef* gd = GoodAt(m_itemAim);
		int tg[64], tn = 0;
		for (int i = 0; i < m_b.nUnits && tn < 64; i++) {
				if (gd && ItemThrowOk(gd, u, m_b.u[i])) {
					tg[tn++] = i;
			}
		}
		iw = min(U(460.f), (float)m_w - U(24));
		float rowH = U(58);
		auto rowsFitTall = [&](int want, int chrome) {
			float avail = (float)m_h - margin * 2 - U(16) - descH;
			int fit = (int)((avail + gap) / (rowH + gap));
			if (fit < chrome + 1) fit = chrome + 1;
			int nfit = fit - chrome;
			if (nfit > want) nfit = want;
			if (nfit < 1) nfit = 1;
			if (nfit > 16) nfit = 16;
			return nfit;
		};
		const int aimBudget = 16;
		int per = rowsFitTall(max(1, tn), 2);
		if (per > aimBudget) per = aimBudget;
		int pages = max(1, (max(1, tn) + per - 1) / per);
		if (pages > 1) per = rowsFitTall(tn, 3);
		if (per > aimBudget) per = aimBudget;
		if (per < 1) per = 1;
		pages = tn < 1 ? 1 : max(1, (tn + per - 1) / per);
		if (m_aimPage >= pages) m_aimPage = pages - 1;
		if (m_aimPage < 0) m_aimPage = 0;
		int a = m_aimPage * per, b = min(tn, a + per);
		int shown = max(0, b - a);
		{
			float ph = rowH * (shown + 2 + (pages > 1 ? 1 : 0) + (tn < 1 ? 1 : 0)) + gap * max(0, shown) + U(16) + descH;
			float topMin = margin + U(8);
			float topMax = (float)m_h - margin - ph + U(8);
			if (topMax < topMin) topMax = topMin;
			y = Clampf(y, topMin, topMax);
			float xMax = (float)m_w - iw - margin;
			if (xMax < margin) xMax = margin;
			x = Clampf(x, margin, xMax);
			Panel(x - U(8), y - U(8), iw + U(16), ph);
		}
		m_r.HudText(gd ? TT(gd->nameTx) : L"?", x + U(8), y + U(14), U(15), RGBA8(255, 226, 150), 0, 1);
		m_r.HudTextBox(gd ? GoodDesc(m_itemAim) : L"", x + U(8), y + U(30), iw - U(16), descH, U(12), RGBA8(214, 222, 234));
		float yy = y + U(30) + descH + gap;
		for (int k = a; k < b; k++) {
			const Unit& tgU = m_b.u[tg[k]];
			wchar_t nm[64], sub[80];
			UnitTitle(tgU, nm, 64);
			int mx = max(1, tgU.ch.stat[S_HP]);
			swprintf_s(sub, L"%s  %s%s  HP %s/%s", ClassName(tgU.ch.cls), TT(TX_MISC_LV), TacNum(tgU.ch.lv), TacNum(tgU.hp), TacNum(mx));
			BOOL help = gd && ItemHelps(m_b, gd, u, tgU);
			if (Btn(ID_BT_ITT0 + (k - a), x, yy, iw, rowH, L"", help, FALSE, U(14))) { UseConsumable(m_itemAim, tg[k]); return; }
			float ps = rowH - U(10);
			DrawPortrait(BattlePortrait(tg[k]), x + U(4), yy + U(5), ps, tgU.team == TEAM_PLAYER ? RGBA8(120, 170, 255) : RGBA8(255, 120, 100));
			DWORD nameC = help ? RGBA8(248, 244, 236) : RGBA8(140, 140, 150);
			DWORD subC = help ? RGBA8(196, 206, 224) : RGBA8(120, 120, 128);
			m_r.HudText(nm, x + ps + U(14), yy + U(16), U(15), nameC, 0, 1);
			m_r.HudText(sub, x + ps + U(14), yy + U(36), FitTextPx(sub, iw - ps - U(24), U(12), 0), subC, 0, 0);
			float barW = min(U(120.f), iw - ps - U(28));
			float bx = x + iw - barW - U(10);
			float by = yy + U(34);
			m_r.HudRect(bx, by, barW, U(8), RGBA8(40, 24, 24, 220));
			float frac = (float)tgU.hp / (float)mx;
			if (frac < 0.f) frac = 0.f;
			if (frac > 1.f) frac = 1.f;
			if (frac > 0.f) m_r.HudRect(bx, by, barW * frac, U(8), frac < 0.25f ? RGBA8(220, 70, 60) : RGBA8(70, 190, 90));
			yy += rowH + gap;
		}
		if (tn < 1) { m_r.HudText(TT(TX_BT_NOTARGET), x + U(8), yy, U(14), RGBA8(255, 180, 160), 0, 1); yy += bh; }
		if (pages > 1) {
			wchar_t pg[32];
			swprintf_s(pg, L"%d / %d", m_aimPage + 1, pages);
			if (Btn(ID_BT_SKPREV, x, yy, U(64), bh, L"◀")) m_aimPage = (m_aimPage + pages - 1) % pages;
			m_r.HudText(pg, x + iw * .5f, yy + bh * .5f, U(14), RGBA8(230, 230, 240), 1, 1);
			if (Btn(ID_BT_SKNEXT, x + iw - U(64), yy, U(64), bh, L"▶")) m_aimPage = (m_aimPage + 1) % pages;
			yy += bh + gap;
		}
		if (Btn(ID_BT_BACK, x, yy, iw, bh, TT(TX_CR_BACK))) { m_itemAim = -1; return; }
		return;
	}
	if (m_phase == PH_P_SUMMON) {
		float sw = min(U(520.f), (float)m_w - U(24));
		float gap = U(6);
		int rows = 3;
		float ph = bh * rows + gap * 2 + U(112);
		x = min(max(U(8), x), max(U(8), (float)m_w - sw - U(12)));
		y = min(y, max(U(8), (float)m_h - ph - U(8)));
		Panel(x - U(8), y - U(8), sw + U(16), ph, .5f);
		m_r.HudText(SummonWord(), x + sw * .5f, y + U(16), U(18), RGBA8(255, 210, 140), 1, 1);
		float yy = y + U(36);
		int hov = -1;
		for (int slot = 0; slot < 2; slot++) {
			int id = EquippedSummonId(u.ch, slot);
			int sk = id > 0 ? SUMMON_BASE + id - 1 : -1;
			wchar_t row[160];
			BOOL ok = FALSE;
			if (id <= 0) wcscpy_s(row, TT(TX_PT_NONE));
			else {
				const SkillDef& sd = SkillInfo(sk);
				int hp = SummonHpCost(u, sk);
				swprintf_s(row, L"%s   MP%d  HP%d  CP%d  %s%d", SummonName(id), sd.mp, hp, SummonCpCost(u, sk), TT(TX_SHAPE_WIDE), sd.area);
				ok = SkillWorth(m_b, m_cur, sk);
			}
			if ((m_navMode && m_focusId == ID_BT_SUM0 + slot) || (!m_navMode && m_mouseSeen && Hit(x, yy, sw, bh))) hov = id;
			if (Btn(ID_BT_SUM0 + slot, x, yy, sw, bh, row, ok, FALSE, U(14), RGBA8(120, 64, 28, 235))) {
				m_cmdSkill = sk;
				m_phase = PH_P_SKILLTARGET;
				m_navMode = FALSE;
				ComputeOverlays();
				PickSkillCursor(sk);
				return;
			}
			yy += bh + gap;
		}
		m_btnSfx = -2;
		if (Btn(ID_BT_BACK, x, yy, sw, bh, TT(TX_CR_BACK))) { BattleCancel(); return; }
		if (hov > 0) {
			wchar_t est[160];
			int sk = SUMMON_BASE + hov - 1;
			float boxY = yy + bh + U(8);
			if (EstimateText(u, sk, est, 160)) {
				m_r.HudText(est, x, boxY + U(8), FitTextPx(est, sw, U(13), 0), RGBA8(255, 214, 140), 0, 0);
				boxY += U(18);
			}
			m_r.HudTextBox(SummonDesc(hov), x, boxY, sw, U(56), U(12), RGBA8(230, 230, 236));
		}
		return;
	}
	if (m_phase == PH_P_CMD) {
		int dx = 0, dz = 0;
		BOOL canShove = FALSE;
		for (int i = 0; i < m_b.nUnits; i++) if (CanShove(m_b, m_cur, i, &dx, &dz)) canShove = TRUE;
		int doorX = 0, doorZ = 0;
		BOOL doorAdj = AdjDoor(m_b, m_cur, &doorX, &doorZ) > 0;
		BOOL fence = doorAdj && m_b.map.At(doorX, doorZ).gimm == GM_FENCE;
		BOOL canDoor = doorAdj && (fence || CanOpenDoor(u));
		BOOL canClimb = FALSE;
		{
			static const int cdx[4] = { 1, -1, 0, 0 }, cdz[4] = { 0, 0, 1, -1 };
			for (int k = 0; k < 4 && !canClimb; k++)
				if (CanTerrace(m_b, m_cur, u.x + cdx[k], u.z + cdz[k])) canClimb = TRUE;
		}
		int haveItem = ConsumableCount(m_save);
		BOOL stuckMove = FALSE;
		if (!m_moved) {
			stuckMove = TRUE;
			for (int mz = 0; mz < MAPN && stuckMove; mz++) for (int mx = 0; mx < MAPN && stuckMove; mx++) {
				if (mx == u.x && mz == u.z) continue;
				for (int L = 0; L < LAY_N; L++) if (ReachCanStopLayer(m_reach, mx, mz, L)) stuckMove = FALSE;
			}
		}
		int n = 4 + (m_moved ? 1 : 0) + (stuckMove ? 1 : 0) + (canShove ? 1 : 0) + (canDoor ? 1 : 0) + (canClimb ? 1 : 0) + (haveItem > 0 ? 1 : 0);
		float cw = min(U(320.f), max(80.f, (float)m_w - 16.f));
		float gapc = U(6);
		float extra = min(U(108.f), (float)m_h * 0.16f);
		float edge = 6.f;
		float avail = (float)m_h - edge * 2.f;
		float need = bh * n + gapc * max(0, n - 1) + extra;
		if (need > avail && n > 0) {
			extra = max(16.f, min(extra, avail * 0.14f));
			bh = (avail - extra - gapc * max(0, n - 1)) / n;
			if (bh < 8.f) bh = 8.f;
			need = bh * n + gapc * max(0, n - 1) + extra;
			if (need > avail) {
				float scale = avail / need;
				bh *= scale;
				extra *= scale;
				gapc *= scale;
				need = avail;
			}
		}
		y = FitLo(y, edge, max(edge, (float)m_h - edge - need));
		float xHi = (float)m_w - cw - edge;
		if (m_hud.col > cw + 20.f) xHi = min(xHi, m_hud.col - cw - edge);
		x = FitLo(x, edge, max(edge, xHi));
		Panel(x - U(8), y - U(8), cw + U(16), bh * n + gapc * max(0, n - 1) + extra, .55f);
		BOOL hasSkill = FALSE, skillLive = FALSE;
		for (int s = 0; s < SKL_COUNT; s++) {
			if (IsFormSkill(s)) {
				if (UnitHasSkill(u, s)) hasSkill = TRUE;
				if (SkillWorth(m_b, m_cur, s)) skillLive = TRUE;
				continue;
			}
			if (!UnitHasSkill(u, s) || SkillInfo(s).kind == SK_PASSIVE) continue;
			hasSkill = TRUE;
			if (SkillWorth(m_b, m_cur, s)) skillLive = TRUE;
		}
		BOOL hasCraft = FALSE;
		for (int i = 0; i < CRAFT_SLOT_N; i++) if (u.craftEq[i] >= CRAFT_BASE) {
			hasCraft = TRUE;
			if (SkillWorth(m_b, m_cur, u.craftEq[i])) skillLive = TRUE;
		}
		BOOL sumLive = FALSE;
		for (int slot = 0; slot < 2 && !sumLive; slot++) {
			int id = EquippedSummonId(u.ch, slot);
			if (id > 0 && SkillWorth(m_b, m_cur, SUMMON_BASE + id - 1)) sumLive = TRUE;
		}
		BOOL itemLive = FALSE;
		if (haveItem > 0) {
			for (int g = 0; g < GoodCount() && !itemLive; g++) if (GoodHave(m_save, g) > 0) {
				const GoodDef* gd = GoodAt(g);
				for (int i = 0; i < m_b.nUnits && !itemLive; i++) if (ItemHelps(m_b, gd, u, m_b.u[i])) itemLive = TRUE;
			}
		}
		BOOL canAtk = HasAttackTarget(m_cur);
		BOOL chest = FALSE;
		{
			int rmax = UnitRangeMax(u);
			for (int dz = -rmax; dz <= rmax && !chest; dz++) for (int dx = -rmax; dx <= rmax; dx++)
				if (ChestInRange(m_cur, u.x + dx, u.z + dz)) chest = TRUE;
		}
		if (chest) canAtk = TRUE;
		const wchar_t* atkLb = chest
			? LangPick14(L"攻撃/宝箱回収", L"Attack/Chest", L"Attaque/Coffre", L"Attacco/Forziere", L"Ataque/Cofre", L"공격/보물회수", L"攻击/宝箱回收", L"هجوم/صندوق", L"Атака/Сундук", L"Angriff/Truhe", L"Ataque/Baú", L"Aanval/Kist", L"Atak/Skrzynia", L"Saldırı/Sandık")
			: TT(TX_BT_ATTACK);
		float yy = y;
		if (stuckMove) {
			m_btnSfx = -2;
			Btn(ID_BT_NOMOVE, x, yy, cw, bh, TT(TX_BT_MOVE), FALSE);
			yy += bh + U(6);
		}
		int desc = -1;
		const wchar_t* descW = NULL;
		BOOL hasSum = EquippedSummonId(u.ch, 0) > 0 || EquippedSummonId(u.ch, 1) > 0;
		auto mark = [&](int id, float by, int tx) {
			if ((m_navMode && m_focusId == id) || (!m_navMode && m_mouseSeen && Hit(x, by, cw, bh))) desc = tx;
		};
		{
			BOOL atkHov = (m_navMode && m_focusId == ID_BT_ATTACK) || (!m_navMode && m_mouseSeen && Hit(x, yy, cw, bh));
			if (atkHov && chest)
				descW = LangPick14(
					L"射程内の敵を攻撃するか、宝箱を回収します。",
					L"Attack an enemy in range, or take a chest.",
					L"Attaque un ennemi à portée, ou prend un coffre.",
					L"Attacca un nemico nel raggio, o prendi un forziere.",
					L"Ataca a un enemigo a alcance, o recoge un cofre.",
					L"사거리 안의 적을 공격하거나 보물상자를 회수합니다.",
					L"攻击射程内的敌人，或回收宝箱。",
					L"هاجم عدوًا ضمن المدى، أو خذ الصندوق.",
					L"Атака по врагу в зоне, или забрать сундук.",
					L"Greif einen Feind in Reichweite an, oder nimm eine Truhe.",
					L"Ataque um inimigo no alcance, ou recolha um baú.",
					L"Val een vijand binnen bereik aan, of neem een kist.",
					L"Atakuj wroga w zasięgu albo zbierz skrzynię.",
					L"Menzildeki düşmana saldır, ya da sandığı topla.");
			else if (atkHov) desc = TX_BTD_ATTACK;
		}
		if (Btn(ID_BT_ATTACK, x, yy, cw, bh, atkLb, canAtk)) {
			m_phase = PH_P_TARGET; m_target = -1; ComputeOverlays(); m_navMode = FALSE;
			m_cursorValid = FALSE;
			for (int i = 0; i < m_b.nUnits; i++) if (CanTarget(m_cur, i, -1)) { m_cx = m_b.u[i].x; m_cz = m_b.u[i].z; m_cursorValid = TRUE; break; }
			if (!m_cursorValid) {
				int rmin = UnitRangeMin(u), rmax = UnitRangeMax(u);
				for (int dz = -rmax; dz <= rmax && !m_cursorValid; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
					int tx = u.x + dx, tz = u.z + dz;
					if (!ChestInRange(m_cur, tx, tz)) continue;
					m_cx = tx; m_cz = tz; m_cursorValid = TRUE; break;
				}
			}
		}
		yy += bh + U(6);
		mark(ID_BT_SKILL, yy, TX_BTD_SKILL);
		if (Btn(ID_BT_SKILL, x, yy, cw, bh, TT(TX_BT_SKILL), skillLive)) {
			int ri = u.rosterIdx;
			if (ri >= 0 && ri < MAX_ROSTER) { m_skillTab = m_skMemoTab[ri]; m_skillPage = m_skMemoPage[ri]; }
			if (hasCraft && !hasSkill) m_skillTab = 2;
			else if (m_skillTab == 2 && !hasCraft) m_skillTab = 0;
			if (m_skillTab < 0 || m_skillTab > 3) m_skillTab = 0;
			m_phase = PH_P_SKILLS; m_navMode = TRUE; m_focusId = ID_BT_SKTAB + m_skillTab;
		}
		yy += bh + U(6);
		if ((m_navMode && m_focusId == ID_BT_SUMMON) || (!m_navMode && m_mouseSeen && Hit(x, yy, cw, bh)))
			descW = hasSum
				? LangPick14(L"装備した召喚獣を広い範囲に放つ。MPとHPを使う。", L"Loose an equipped summon over a wide area. It spends MP and HP.", L"Lance une invocation équipée sur une large zone. Coûte des PM et des PV.", L"Scatena un'evocazione equipaggiata su un'area ampia. Costa PM e PV.", L"Suelta una invocación equipada en un área amplia. Gasta PM y PV.", L"장착한 소환수를 넓은 범위에 낸다. MP와 HP를 쓴다.", L"放出装备的召唤兽，范围很广。消耗MP和HP。", L"أطلق مستدعى مجهزا على مدى واسع. يستهلك MP وHP.", L"Выпустить надетого призванного по широкой области. Тратит MP и HP.", L"Einen ausgerüsteten Beschworenen über ein weites Feld loslassen. Kostet MP und HP.", L"Solta um invocado equipado numa área larga. Gasta MP e HP.", L"Laat een uitgeruste oproeping over een breed gebied los. Kost MP en HP.", L"Wypuść wyposażone przyzwanie na szeroki obszar. Kosztuje MP i HP.", L"Kuşanılmış çağrıyı geniş alana sal. MP ve HP harcar.")
				: LangPick14(L"店で買い、編成か戦闘中のステータスの召喚枠に付けると使えます。", L"Buy one in the shop, then set it in the summon slots on the party screen or in battle status.", L"Achetez-en une, puis placez-la dans les cases d'invocation de la formation ou du statut en combat.", L"Comprane una, poi mettila negli slot evocazione della formazione o dello stato in battaglia.", L"Cómprala en la tienda y ponla en las ranuras de invocación de la formación o del estado en combate.", L"상점에서 사서 편성이나 전투 중 스테이터스의 소환 칸에 장착하면 쓸 수 있다.", L"在商店购买后，装到编成或战斗中状态的召唤栏即可使用。", L"اشترها من المتجر ثم ضعها في خانات الاستدعاء في التشكيل أو في حالة القتال.", L"Купите в лавке и поставьте в слоты призыва в отряде или в статусе боя.", L"Im Laden kaufen und in die Beschwörungsplätze der Aufstellung oder des Kampfstatus legen.", L"Compre na loja e ponha nas vagas de invocação da formação ou do estado em batalha.", L"Koop er een en zet die in de oproepvakken van de formatie of de gevechtsstatus.", L"Kup w sklepie i włóż w pola przyzwania w składzie albo w statusie walki.", L"Dükkandan alıp kadro veya savaş durumundaki çağırma yuvalarına takınca kullanılır.");
		if (Btn(ID_BT_SUMMON, x, yy, cw, bh, SummonWord(), hasSum && sumLive, FALSE, U(15), RGBA8(120, 64, 28, 235))) {
			m_phase = PH_P_SUMMON; m_navMode = TRUE; m_focusId = ID_BT_SUM0;
			return;
		}
		yy += bh + U(6);
		{
			const BOOL canGuard = !m_moved;
			mark(ID_BT_WAIT, yy, canGuard ? TX_BTD_WAIT : TX_BTD_NOGUARD);
			// 防御は構えた盾の音だけ。決定音と重ねない。
			if (canGuard) m_btnSfx = TS_BLOCK;
			if (Btn(ID_BT_WAIT, x, yy, cw, bh, canGuard ? TT(TX_BT_GUARD) : TT(TX_BT_WAIT))) {
				if (canGuard && m_cur >= 0 && m_cur < m_b.nUnits) {
					Unit& gu = m_b.u[m_cur];
					if (gu.buffDef < 1) gu.buffDef = 1;
					AddPopup(v3(gu.px, gu.py + 1.f, gu.pz), TT(TX_BT_GUARD), RGBA8(180, 210, 255), 1, MSG_FACE);
					AddLog(TT(TX_BT_GUARD), RGBA8(180, 210, 255));
				}
				FinishUnitTurn(0);
				return;
			}
		}
		yy += bh + U(6);
		{
			int have = ConsumableCount(m_save);
			wchar_t lb[64];
			swprintf_s(lb, L"%s %d", TT(TX_BT_ITEM), have);
			if (have > 0) mark(ID_BT_ITEM, yy, TX_BTD_ITEM);
			if (have > 0 && Btn(ID_BT_ITEM, x, yy, cw, bh, lb, itemLive)) {
				m_phase = PH_P_ITEM; m_itemAim = -1; m_itemPage = 0; m_navMode = TRUE; m_focusId = ID_BT_IT0;
				return;
			}
			if (have > 0) yy += bh + U(6);
		}
		if (canShove) mark(ID_BT_SHOVE, yy, TX_BTD_SHOVE);
		if (canShove && Btn(ID_BT_SHOVE, x, yy, cw, bh, TT(TX_BT_SHOVE))) {
			m_phase = PH_P_SHOVE; m_navMode = FALSE;
			for (int i = 0; i < m_b.nUnits; i++) if (CanShove(m_b, m_cur, i, NULL, NULL)) { m_cx = m_b.u[i].x; m_cz = m_b.u[i].z; m_cursorValid = TRUE; break; }
			return;
		}
		if (canShove) yy += bh + U(6);
		if (canDoor) mark(ID_BT_DOOR, yy, fence ? TX_BTD_FENCE : TX_BTD_DOOR);
		if (canDoor) m_btnSfx = -2;
		if (canDoor && Btn(ID_BT_DOOR, x, yy, cw, bh, TT(fence ? TX_BT_FENCE : TX_BT_DOOR))) {
			int ox, oz;
			if (AdjDoor(m_b, m_cur, &ox, &oz) > 0) {
				BOOL brk = m_b.map.At(ox, oz).gimm == GM_FENCE;
				OpenDoor(m_b, ox, oz);
				AddPopup(CellCenter(ox, oz) + v3(0, .6f, 0), TT(brk ? TX_BT_FENCE : TX_BT_DOOR), RGBA8(220, 200, 140), 1, MSG_SLAB);
				TacSfx(TS_DOOR);
				m_r.BuildTerrain(m_b.map);
				FinishUnitTurn(1);
			}
			return;
		}
		if (canDoor) yy += bh + U(6);
		if (canClimb) mark(ID_BT_CLIMB, yy, TX_BTD_CLIMB);
		if (canClimb && Btn(ID_BT_CLIMB, x, yy, cw, bh, TT(TX_BT_CLIMB))) {
			int tx = -1, tz = -1;
			if (m_cursorValid && CanTerrace(m_b, m_cur, m_cx, m_cz)) { tx = m_cx; tz = m_cz; }
			else {
				static const int cdx[4] = { 1, -1, 0, 0 }, cdz[4] = { 0, 0, 1, -1 };
				for (int k = 0; k < 4; k++) if (CanTerrace(m_b, m_cur, u.x + cdx[k], u.z + cdz[k])) { tx = u.x + cdx[k]; tz = u.z + cdz[k]; break; }
			}
			if (tx >= 0) ApplyTerrace(tx, tz);
			return;
		}
		if (canClimb) yy += bh + U(6);
		if (m_moved) mark(ID_BT_UNDO, yy, TX_BTD_UNDO);
		if (m_moved) m_btnSfx = -2;
		if (m_moved && Btn(ID_BT_UNDO, x, yy, cw, bh, TT(TX_BT_UNDO))) BattleCancel();
		float boxTop = (m_moved ? yy + bh : yy - U(6)) + U(8);
		const wchar_t* body = descW ? descW : (desc >= 0 ? TT(desc) : NULL);
		float used = 0;
		if (body && body[0]) {
			wchar_t ls[4][180];
			float px = U(13);
			int n = WrapSpaceLines(m_r, body, cw - U(4), px, ls, 4);
			float lh = px * 1.6f + 4.f;
			float maxH = max(lh, extra - U(8));
			for (int i = 0; i < n && used + lh <= maxH + 0.5f; i++) {
				m_r.HudText(ls[i], x + U(4), boxTop + used + lh * .5f, FitTextPx(ls[i], cw - U(8), px, 0), RGBA8(230, 230, 236), 0, 0);
				used += lh;
			}
		}
		if (desc == TX_BTD_ATTACK) {
			wchar_t est[160];
			if (EstimateText(u, -1, est, 160))
				m_r.HudText(est, x + U(4), boxTop + used + U(8), FitTextPx(est, cw - U(8), U(13), 0), RGBA8(255, 214, 140), 0, 0);
		}
		return;
	}
	if (m_phase == PH_P_SKILLS) {
		if (m_skillTab < 0 || m_skillTab > 3) m_skillTab = 0;
		auto keepSkill = [&]() {
			int ri = u.rosterIdx;
			if (ri < 0 || ri >= MAX_ROSTER) return;
			m_skMemoTab[ri] = m_skillTab;
			m_skMemoPage[ri] = m_skillPage;
		};
		int crafts[CRAFT_SLOT_N], nc = 0;
		if (m_skillTab == 2)
			for (int i = 0; i < CRAFT_SLOT_N; i++) if (u.craftEq[i] >= CRAFT_BASE) crafts[nc++] = i;
		static const int kSkillEl[8] = { EL_FIRE, EL_ICE, EL_THUNDER, EL_HOLY, EL_DARK, EL_WIND, EL_EARTH, EL_POISON };
		int shapeOk[6] = { 1, 0, 0, 0, 0, 0 };
		int elOk[9] = { 1, 0, 0, 0, 0, 0, 0, 0, 0 };
		auto atkShape = [](const SkillDef& sd) -> int {
			if (sd.shape == SH_LINE) return 4;
			if (sd.shape == SH_FAN) return 5;
			if (sd.area >= 6) return 3;
			if (sd.area <= 0) return 1;
			return 2;
		};
		auto tabKind = [&](const SkillDef& sd) -> BOOL {
			if (m_skillTab == 0) return sd.kind == SK_PHYS || sd.kind == SK_MAGIC;
			if (m_skillTab == 1) return sd.kind == SK_HEAL || sd.kind == SK_BUFF;
			return FALSE;
		};
		if (m_skillTab == 0 || m_skillTab == 1) {
			int shapeHave[6] = {};
			int elHave[9] = {};
			for (int s = 0; s < SKL_COUNT; s++) {
				if (IsFormSkill(s) || !UnitHasSkill(u, s) || SkillInfo(s).kind == SK_PASSIVE) continue;
				const SkillDef& sd = SkillInfo(s);
				if (!tabKind(sd)) continue;
				int sh = atkShape(sd);
				if (sh >= 1 && sh <= 5) shapeHave[sh]++;
				for (int e = 0; e < 8; e++) if (kSkillEl[e] == sd.element) elHave[e + 1]++;
			}
			if (m_skillShape < 0 || m_skillShape > 5 || (m_skillShape && !shapeHave[m_skillShape])) {
				m_skillShape = 0;
				m_skillPage = 0;
			}
			int curEl = 0;
			for (int e = 0; e < 8; e++) if (kSkillEl[e] == m_skillEl) curEl = e + 1;
			if (m_skillEl && !elHave[curEl]) {
				m_skillEl = 0;
				curEl = 0;
				m_skillPage = 0;
			}
			if (m_skillShape && m_skillEl) {
				int both = 0;
				for (int s = 0; s < SKL_COUNT; s++) {
					if (IsFormSkill(s) || !UnitHasSkill(u, s) || SkillInfo(s).kind == SK_PASSIVE) continue;
					const SkillDef& sd = SkillInfo(s);
					if (!tabKind(sd)) continue;
					if (atkShape(sd) == m_skillShape && sd.element == m_skillEl) both++;
				}
				if (!both) { m_skillEl = 0; m_skillPage = 0; }
			}
			for (int s = 0; s < SKL_COUNT; s++) {
				if (IsFormSkill(s) || !UnitHasSkill(u, s) || SkillInfo(s).kind == SK_PASSIVE) continue;
				const SkillDef& sd = SkillInfo(s);
				if (!tabKind(sd)) continue;
				int sh = atkShape(sd);
				if ((!m_skillEl || sd.element == m_skillEl) && sh >= 1 && sh <= 5) shapeOk[sh] = 1;
				if (!m_skillShape || sh == m_skillShape) {
					for (int e = 0; e < 8; e++) if (kSkillEl[e] == sd.element) elOk[e + 1] = 1;
				}
			}
			if (m_focusId >= ID_BT_SHAPE1 && m_focusId <= ID_BT_SHAPE0 + 5 && !shapeOk[m_focusId - ID_BT_SHAPE0]) m_focusId = ID_BT_SHAPE0;
			if (m_focusId > ID_BT_EL0 && m_focusId <= ID_BT_EL0 + 8 && !elOk[m_focusId - ID_BT_EL0]) m_focusId = ID_BT_EL0;
		}
		int list[SKL_COUNT];
		int n = 0;
		if (m_skillTab == 3) {
			for (int s = SKL_FORM0; s < SKL_FORMN; s++) list[n++] = s;
		} else if (m_skillTab != 2) {
			for (int s = 0; s < SKL_COUNT; s++) {
				if (IsFormSkill(s) || !UnitHasSkill(u, s)) continue;
				const SkillDef& sd = SkillInfo(s);
				if (sd.kind == SK_PASSIVE) continue;
				if (m_skillTab == 0 && sd.kind != SK_PHYS && sd.kind != SK_MAGIC) continue;
				if (m_skillTab == 1 && sd.kind != SK_HEAL && sd.kind != SK_BUFF) continue;
				if ((m_skillTab == 0 || m_skillTab == 1) && m_skillShape && atkShape(sd) != m_skillShape) continue;
				if ((m_skillTab == 0 || m_skillTab == 1) && m_skillEl > 0 && sd.element != m_skillEl) continue;
				list[n++] = s;
			}
		}
		float gap = U(4);
		float tabH = max(U(26.f), min(U(32.f), bh));
		float subH = U(22);
		int subN = (m_skillTab == 0 || m_skillTab == 1) ? 2 : 0;
		float extra = subN * (subH + gap);
		float sw = min(U(460.f), max(U(220.f), (float)m_w - U(24)));
		// 範囲行・属性見出し・説明枠。同じ高さに描くと重なるので、この分をパネルに取る。
		float descBlock = U(16) + U(16) + U(18) + U(44) + U(14);
		float chrome = tabH + gap + extra + (bh + gap) * 2.f + descBlock + U(24);
		int per = 8;
		int fit = (int)(((float)m_h - chrome) / (bh + gap));
		if (fit < 3) { bh = max(U(22.f), ((float)m_h - chrome) / 5.f - gap); fit = 3; }
		if (fit < per) per = max(3, fit);
		int pages = max(1, (n + per - 1) / per);
		if (m_skillPage < 0) m_skillPage = 0;
		if (m_skillPage >= pages) m_skillPage = pages - 1;
		int a = m_skillPage * per, b = min(n, a + per);
		int shown = b - a;
		float rows = (float)(shown + nc + 1 + (pages > 1 ? 1 : 0));
		float ph = tabH + gap + extra + (bh + gap) * rows + descBlock;
		x = min(max(U(8), x), max(U(8), (float)m_w - sw - U(12)));
		y = min(y, max(U(8), (float)m_h - ph - U(8)));
		if (y < U(8)) y = U(8);
		Panel(x - U(8), y - U(8), sw + U(16), ph, .48f);
		float tw = (sw - gap * 3.f) / 4.f;
		const int tabTx[4] = { TX_SK_TAB_ATK, TX_SK_TAB_HEAL, TX_SK_TAB_CRAFT, TX_SK_TAB_FORM };
		for (int t = 0; t < 4; t++) {
			DWORD col = (m_skillTab == t) ? RGBA8(110, 78, 28, 190) : RGBA8(28, 32, 48, 120);
			if (Btn(ID_BT_SKTAB + t, x + (tw + gap) * t, y, tw, tabH, TT(tabTx[t]), TRUE, m_skillTab == t, U(13), col)) {
				m_skillTab = t;
				m_skillPage = 0;
				keepSkill();
				return;
			}
		}
		float yy = y + tabH + gap;
		if (m_skillTab == 0 || m_skillTab == 1) {
			const wchar_t* shapeL[6] = {
				LangPick14(L"全", L"All", L"Tous", L"Tutti", L"Todo", L"전체", L"全部", L"الكل", L"Все", L"Alle", L"Tudo", L"Alles", L"Wszystko", L"Tümü"),
				TT(TX_SHAPE_ONE), TT(TX_MISC_AREA), TT(TX_SHAPE_WIDE), TT(TX_SHAPE_LINE), TT(TX_SHAPE_FAN)
			};
			float bw6 = (sw - gap * 5.f) / 6.f;
			for (int t = 0; t < 6; t++) {
				if (Btn(ID_BT_SHAPE0 + t, x + (bw6 + gap) * t, yy, bw6, subH, shapeL[t], t == 0 || shapeOk[t], m_skillShape == t, U(11))) {
					m_skillShape = t;
					m_skillPage = 0;
					return;
				}
			}
			yy += subH + gap;
			const wchar_t* elL[9] = {
				shapeL[0],
				LangPick14(L"火", L"Fi", L"Fe", L"Fu", L"Fu", L"화", L"火", L"ن", L"Ог", L"Fe", L"Fo", L"Vu", L"Og", L"At"),
				LangPick14(L"氷", L"Ic", L"Gl", L"Gh", L"Hi", L"빙", L"冰", L"ج", L"Лд", L"Ei", L"Ge", L"IJs", L"Ló", L"Bu"),
				LangPick14(L"雷", L"Th", L"To", L"Tu", L"Tr", L"뢰", L"雷", L"ر", L"Гр", L"Do", L"Tr", L"Do", L"Gr", L"Yı"),
				LangPick14(L"聖", L"Ho", L"Sa", L"Sa", L"Sa", L"성", L"圣", L"م", L"Св", L"He", L"Sa", L"He", L"Św", L"Ku"),
				LangPick14(L"闇", L"Da", L"Té", L"Os", L"Os", L"암", L"暗", L"ظ", L"Ть", L"Du", L"Tr", L"Do", L"Mr", L"Ka"),
				LangPick14(L"風", L"Wi", L"Ve", L"Ve", L"Vi", L"풍", L"风", L"ر", L"Ве", L"Wi", L"Ve", L"Wi", L"Wi", L"Rü"),
				LangPick14(L"地", L"Ea", L"Te", L"Te", L"Ti", L"지", L"地", L"أ", L"Зе", L"Er", L"Te", L"Aa", L"Zi", L"To"),
				LangPick14(L"毒", L"Po", L"Po", L"Ve", L"Ve", L"독", L"毒", L"س", L"Яд", L"Gi", L"Ve", L"Gi", L"Tr", L"Ze")
			};
			float bw9 = (sw - gap * 8.f) / 9.f;
			for (int t = 0; t < 9; t++) {
				int on = t == 0 ? m_skillEl == 0 : m_skillEl == kSkillEl[t - 1];
				if (Btn(ID_BT_EL0 + t, x + (bw9 + gap) * t, yy, bw9, subH, elL[t], t == 0 || elOk[t], on, U(11))) {
					m_skillEl = t == 0 ? 0 : kSkillEl[t - 1];
					m_skillPage = 0;
					return;
				}
			}
			yy += subH + gap;
		}
		int hov = -1;
		int hovCraft = -1;
		for (int k = 0; k < nc; k++) {
			int slot = crafts[k];
			int s = u.craftEq[slot];
			const SkillDef& sd = SkillInfo(s);
			BOOL ougi = IsCraftSuper(s);
			wchar_t l[96];
			wchar_t cp[24];
			CraftCostText(s - CRAFT_BASE, cp, 24);
			swprintf_s(l, L"%s  %s  %s%s", CraftName(s), CraftShapeName(sd.area, sd.shape), cp, ougi ? L" ★" : L"");
			BOOL ok = SkillWorth(m_b, m_cur, s);
			if ((m_navMode && m_focusId == ID_BT_CRAFT0 + slot) || (!m_navMode && Hit(x, yy, sw, bh))) hovCraft = s;
			DWORD col = ougi ? RGBA8(130, 90, 24, 240) : (sd.kind == SK_HEAL || sd.kind == SK_BUFF ? RGBA8(40, 100, 70, 240) : RGBA8(92, 68, 28, 235));
			if (Btn(ID_BT_CRAFT0 + slot, x, yy, sw, bh, l, ok, FALSE, U(15), col)) {
				m_cmdSkill = s;
				m_phase = PH_P_SKILLTARGET;
				m_navMode = FALSE;
				ComputeOverlays();
				PickSkillCursor(s);
				return;
			}
			yy += bh + U(4);
		}
		for (int k = a; k < b; k++) {
			int s = list[k];
			const SkillDef& sd = SkillInfo(s);
			wchar_t l[96];
			if (sd.shape == SH_LINE || sd.shape == SH_FAN) swprintf_s(l, L"%s  MP%d  %s%d", SkillLabel(s), sd.mp, CraftShapeName(sd.area, sd.shape), sd.area);
			else if (sd.area > 0) swprintf_s(l, L"%s  MP%d  %s%d", SkillLabel(s), sd.mp, TT(TX_MISC_AREA), sd.area);
			else swprintf_s(l, L"%s   MP %d", SkillLabel(s), sd.mp);
			BOOL ok = SkillWorth(m_b, m_cur, s);
			int bid = IsFormSkill(s) ? ID_BT_FORM0 + (s - SKL_FORM0) : ID_BT_SK0 + s;
			if ((m_navMode && m_focusId == bid) || (!m_navMode && Hit(x, yy, sw, bh))) hov = s;
			DWORD scol = IsFormSkill(s) ? RGBA8(36, 72, 118, 235) : (sd.kind == SK_MAGIC ? RGBA8(70, 50, 120, 240) : (sd.kind == SK_HEAL ? RGBA8(40, 100, 70, 240) : 0));
			if (Btn(bid, x, yy, sw, bh, l, ok, FALSE, U(15), scol)) {
				m_cmdSkill = s;
				m_phase = PH_P_SKILLTARGET;
				m_navMode = FALSE;
				if (IsFormSkill(s) && !m_cmdFaced) { m_cmdFace0 = u.face; m_cmdFaced = 1; }
				ComputeOverlays();
				PickSkillCursor(s);
				AimFormFace();
				return;
			}
			yy += bh + U(4);
		}
		if (pages > 1) {
			wchar_t pg[32];
			swprintf_s(pg, L"%d / %d", m_skillPage + 1, pages);
			if (Btn(ID_BT_SKPREV, x, yy, U(64), bh, L"◀")) m_skillPage = (m_skillPage + pages - 1) % pages;
			m_r.HudText(pg, x + sw * .5f, yy + bh * .5f, U(14), RGBA8(230, 230, 240), 1, 1);
			if (Btn(ID_BT_SKNEXT, x + sw - U(64), yy, U(64), bh, L"▶")) m_skillPage = (m_skillPage + 1) % pages;
			yy += bh + U(4);
		}
		const wchar_t* backWord = TT(TX_CR_BACK);
		m_btnSfx = -2;
		if (Btn(ID_BT_BACK, x, yy, sw, bh, backWord)) BattleCancel();
		yy += bh + U(6);
		auto footLine = [&](const wchar_t* s, DWORD col, float& ly) {
			m_r.HudText(s, x, ly + U(8), FitTextPx(s, sw, U(12), 0), col, 0, 0);
			ly += U(16);
		};
		if (hovCraft >= 0) {
			const SkillDef& sd = SkillInfo(hovCraft);
			wchar_t l[96];
			int r0 = sd.rMax < 0 ? UnitRangeMin(u) : sd.rMin;
			int r1 = sd.rMax < 0 ? UnitRangeMax(u) : sd.rMax;
			wchar_t cp[24];
			CraftCostText(hovCraft - CRAFT_BASE, cp, 24);
			swprintf_s(l, L"%s %d-%d  %s  %s", TT(TX_MISC_RANGE), r0, r1, CraftShapeName(sd.area, sd.shape), cp);
			float ly = yy + U(4);
			footLine(l, RGBA8(255, 210, 120), ly);
			{
				wchar_t est[160];
				if (EstimateText(u, hovCraft, est, 160)) footLine(est, RGBA8(255, 214, 140), ly);
			}
			m_r.HudTextBox(CraftDesc(hovCraft), x, ly + U(2), sw, U(40), U(12), RGBA8(230, 230, 236));
		} else if (hov >= 0) {
			const SkillDef& sd = SkillInfo(hov);
			wchar_t l[96];
			const wchar_t* lack = u.mp < sd.mp ? TT(TX_BT_NOMP) : (IsFormSkill(hov) && !SkillUsable(m_b, m_cur, hov) ? TT(TX_FORM_NEED) : L"");
			swprintf_s(l, L"%s %d-%d  %s%d  %s", TT(TX_MISC_RANGE), sd.rMax < 0 ? UnitRangeMin(u) : sd.rMin, sd.rMax < 0 ? UnitRangeMax(u) : sd.rMax, TT(TX_MISC_AREA), sd.area, lack);
			wchar_t attr[120];
			SkillAttrText(sd, attr, 120);
			float ly = yy + U(4);
			footLine(l, RGBA8(255, 226, 150), ly);
			if (attr[0]) footLine(attr, RGBA8(255, 214, 140), ly);
			{
				wchar_t est[160];
				if (EstimateText(u, hov, est, 160)) footLine(est, RGBA8(186, 230, 255), ly);
			}
			m_r.HudTextBox(TT(sd.descTx), x, ly + U(2), sw, U(40), U(12), RGBA8(230, 230, 236));
			if (IsFormSkill(hov)) DrawFormGuide(hov, u.x, u.z, x - U(8), y - U(8), sw + U(16), ph);
		}
		m_skillPreview = hov >= 0 ? hov : hovCraft;
		keepSkill();
	}
}

void Game::DrawPromoteOffer()
{
	if (m_lvqCur < 0 || m_lvqCur >= m_lvqN) return;
	const LevelUpInfo& L = m_lvq[m_lvqCur];
	if (L.unit < 0 || L.unit >= m_b.nUnits) return;
	Unit& u = m_b.u[L.unit];
	BOOL grace = m_promoAsk == 2 && u.ch.trialFrom >= 0;
	int tgt[5];
	int nt = 0;
	int revert = -1;
	EvoOption opt[4];
	int no = EvoOptions(grace ? u.ch.trialFrom : u.ch.cls, opt);
	for (int k = 0; k < no && nt < 4; k++) {
		if (grace && opt[k].target == u.ch.cls) continue;
		tgt[nt++] = opt[k].target;
	}
	if (grace && u.ch.trialFrom != u.ch.cls) revert = u.ch.trialFrom;
	float wPre = min(U(760.f), (float)m_w - U(24));
	wchar_t jlines[8][120];
	int jn = SplitJobPath(m_r, u.ch, wPre - U(36), U(13), 8, jlines);
	float histH = jn > 0 ? jn * (U(13) + 4.f) + U(8) : 0.f;
	int rows = nt + (revert >= 0 ? 1 : 0);
	float w = min(U(760.f), (float)m_w - U(24));
	float briefPx = U(13);
	float briefLine = briefPx * 1.55f + 4.f;
	wchar_t briefs[4][4][180];
	int briefC[4];
	int briefN = 1;
	for (int k = 0; k < nt && k < 4; k++) {
		wchar_t brief[1024];
		EvoBrief(tgt[k], brief, 1024);
		briefC[k] = WrapSpaceLines(m_r, brief, w - U(56), briefPx, briefs[k], 4);
		if (briefC[k] > briefN) briefN = briefC[k];
	}
	float chrome = U(110) + histH + U(48);
	int showN = briefN;
	while (showN > 1 && rows > 0 && chrome + rows * (U(40) + showN * briefLine + U(8)) > (float)m_h - U(16))
		showN--;
	float row = U(40) + showN * briefLine + U(8);
	float h = chrome + rows * row;
	if (h > (float)m_h - U(16)) h = (float)m_h - U(16);
	float x = (m_w - w) * .5f, y = max(U(8), (m_h - h) * .5f);
	Panel(x, y, w, h, .92f);
	m_r.HudText(grace ? TT(TX_EV_TRIAL) : TT(TX_EV_JOB), x + w * .5f, y + U(28), U(26), RGBA8(255, 226, 150), 1, 1);
	wchar_t who[192];
	if (grace) swprintf_s(who, L"%s  %s  %s%d  Lv%d", u.ch.name, ClassName(u.ch.cls), TT(TX_MISC_LV), u.ch.lv, u.ch.trialLv + 3);
	else swprintf_s(who, L"%s  %s  %s%d", u.ch.name, ClassName(u.ch.cls), TT(TX_MISC_LV), u.ch.lv);
	m_r.HudText(who, x + w * .5f, y + U(54), FitTextPx(who, w - U(24), U(15), 0), RGBA8(230, 230, 240), 1, 0);
	m_r.HudText(TT(TX_EV_GRACE), x + w * .5f, y + U(74), U(13), RGBA8(170, 220, 170), 1, 0);
	float ty = y + U(96);
	if (jn > 0) {
		ty += DrawJobPath(m_r, u.ch, x + U(18), ty, w - U(36), U(13), RGBA8(210, 220, 235), 8) + U(6);
	}
	auto take = [&](int pick) {
		int oldHp = u.ch.stat[S_HP], oldMp = u.ch.stat[S_MP];
		if (grace) EvoRetrain(u.ch, pick);
		else EvoApply(u.ch, pick, NULL, 0);
		u.hp = min(u.ch.stat[S_HP], u.hp + max(0, u.ch.stat[S_HP] - oldHp));
		u.mp = min(u.ch.stat[S_MP], u.mp + max(0, u.ch.stat[S_MP] - oldMp));
		float t = m_act[L.unit].t;
		ActorForUnit(L.unit);
		m_act[L.unit].t = t;
		m_dispHp[L.unit] = (float)u.hp;
		InvalidatePortraits();
		m_evFlash = 1.15f;
		m_flashWhite = 1.f;
		Burst(v3(u.px, u.py + .2f, u.pz), 36, 4.f, .16f, 1.1f, 1.f, .9f, .45f, 2, 1, 2.f, 0, 1.2f);
		AddBanner(TT(TX_EV_DONE), ClassName(u.ch.cls), RGBA8(255, 226, 120), 1, 2.8f, MSG_DEPTH);
		TacSfx(TS_PROMOTE);
		AdvanceLevelUp();
	};
	int step = JobPathCount(u.ch);
	if (step < 1) step = max(1, ClassInfo(u.ch.cls).tier);
	static const int gtx[G_COUNT] = { TX_G_MELEE, TX_G_SWIFT, TX_G_MAGIC, TX_G_HOLY };
	for (int k = 0; k < nt; k++) {
		m_btnSfx = -2;
		const ClassDef& cd = ClassInfo(tgt[k]);
		wchar_t nth[96], lab[160];
		// 次職番号は対象の職段(tier)を正とする。JobPathCount は履歴が伸びると過大になり誤った段数を表示する。
		int nthNo = cd.tier >= 1 ? cd.tier : step + 1;
		swprintf_s(nth, TT(TX_JOB_NTH), nthNo, ClassName(tgt[k]));
		swprintf_s(lab, L"%s   %s", nth, TT(gtx[Clampi(cd.group, 0, G_COUNT - 1)]));
		if (Btn(ID_BT_PROMO0 + k, x + U(16), ty, w - U(32), U(34), lab, TRUE, FALSE, U(16))) {
			take(tgt[k]);
			return;
		}
		float ly = ty + U(38);
		int nDraw = k < 4 ? briefC[k] : 0;
		if (nDraw > showN) nDraw = showN;
		for (int i = 0; i < nDraw; i++) {
			if (ly + briefLine > ty + row - U(2)) break;
			m_r.HudText(briefs[k][i], x + U(20), ly + briefLine * .5f, FitTextPx(briefs[k][i], w - U(40), briefPx, 0), RGBA8(210, 220, 235), 0, 0);
			ly += briefLine;
		}
		ty += row;
	}
	if (revert >= 0) {
		m_btnSfx = -2;
		if (Btn(ID_BT_PROMO0 + 4, x + U(16), ty, w - U(32), U(34), TT(TX_EV_REVERT), TRUE, FALSE, U(16))) {
			take(revert);
			return;
		}
	}
	if (Btn(ID_BT_PROMO_LATER, x + w * .5f - U(110), y + h - U(50), U(220), U(36), TT(TX_EV_LATER))) AdvanceLevelUp();
}

void Game::DrawLevelUp()
{
	if (m_promoAsk) { DrawPromoteOffer(); return; }
	if (m_lvqCur >= m_lvqN) return;
	float shown = m_lvT;
	if (m_lvqCur == 0) {
		if (m_lvT < 2.6f * TacPaceTime(m_save)) return;
		shown = m_lvT - 2.6f * TacPaceTime(m_save);
	}
	const LevelUpInfo& L = m_lvq[m_lvqCur];
	const Unit& u = m_b.u[L.unit];
	float w = min(U(460.f), (float)m_w - U(16));
	int cols = (w - U(28)) >= U(340) ? 4 : 2;
	float statH = U(cols == 4 ? 20.f : 18.f) * (float)((S_COUNT + cols - 1) / cols);
	float titlePx = U(22);
	float namePx = U(16);
	float lvPx = U(15);
	float port = U(58);
	float headH = max(port, namePx + lvPx + U(14));
	float learnLine = U(20);
	float learnH = L.nLearned > 0 ? L.nLearned * learnLine + U(4) : 0;
	float btnH = U(34);
	float pad = U(10);
	float h = pad + titlePx + U(8) + headH + U(8) + statH + learnH + U(8) + btnH + pad;
	float maxH = (float)m_h - U(12);
	if (h > maxH) h = maxH;
	float x = (m_w - w) * .5f, y = max(U(6), (m_h - h) * .5f);
	float in = Smooth01(shown / .25f);
	y += (1.f - in) * U(24);
	Panel(x, y, w, h, in);
	float cy = y + pad;
	m_r.HudText(TT(TX_BT_LEVELUP), x + w * .5f, cy + titlePx * .5f, titlePx, RGBA8(255, 230, 100), 1, 1, 1, in);
	cy += titlePx + U(6);
	DrawPortrait(BattlePortrait(L.unit), x + pad, cy, port, RGBA8(110, 170, 255));
	float tx = x + pad + port + U(10);
	float tw = w - (tx - x) - pad;
	if (tw < U(80)) tw = U(80);
	m_r.HudText(u.ch.name, tx, cy + namePx * .55f, FitTextPx(u.ch.name, tw, namePx, 1), RGBA8(255, 255, 255), 0, 1);
	wchar_t l[96];
	swprintf_s(l, L"%s %s → %s", TT(TX_MISC_LV), TacNum(L.oldLv), TacNum(L.newLv));
	m_r.HudText(l, tx, cy + namePx + U(6) + lvPx * .5f, FitTextPx(l, tw, lvPx, 1), RGBA8(140, 255, 150), 0, 1);
	cy += headH + U(6);
	float got = DrawStatLines(u.ch, x + pad, cy, w - pad * 2, L.gains, -1, -1);
	cy += got + U(4);
	float lim = y + h - pad - btnH - U(4);
	for (int k = 0; k < L.nLearned && cy + learnLine <= lim; k++) {
		swprintf_s(l, L"%s: %s", TT(TX_BT_LEARNED), SkillLabel(L.learned[k]));
		m_r.HudText(l, x + w * .5f, cy + learnLine * .5f, FitTextPx(l, w - pad * 2, U(14), 1), RGBA8(255, 226, 150), 1, 1);
		cy += learnLine;
	}
	if (Btn(ID_BT_LVOK, x + w * .5f - U(60), y + h - pad - btnH, U(120), btnH, L"OK")) AdvanceLevelUp();
}

void Game::AdvanceLevelUp()
{
	if (m_promoAsk) {
		m_promoAsk = 0;
	} else if (m_lvqCur >= 0 && m_lvqCur < m_lvqN) {
		const LevelUpInfo& L = m_lvq[m_lvqCur];
		if (L.unit >= 0 && L.unit < m_b.nUnits) {
			const CharData& c = m_b.u[L.unit].ch;
			int gate = PromoteLevel(ClassInfo(c.cls).tier);
			EvoOption opt[4];
			if (L.oldLv < gate && L.newLv >= gate && EvoOptions(c.cls, opt) > 0) {
				m_promoAsk = 1;
				m_navMode = TRUE;
				m_focusId = ID_BT_PROMO0;
				return;
			}
			if (c.trialLv > 0 && c.trialFrom >= 0 && c.cls != c.trialFrom && L.newLv == c.trialLv + 3) {
				m_promoAsk = 2;
				m_navMode = TRUE;
				m_focusId = ID_BT_PROMO0;
				return;
			}
		}
	}
	m_lvqCur++;
	m_lvT = 0;
	if (m_lvqCur >= m_lvqN) {
		m_lvqN = 0;
		m_promoAsk = 0;
		FinishUnitTurn(1);
	} else {
		TacSfx(TS_LEVEL);
		if (m_lvq[m_lvqCur].nLearned > 0) TacSfx(TS_LEARN);
	}
}

void Game::DrawCutInUi()
{
	float cover = 0;
	switch (m_cutStage) {
	case 0: cover = Smooth01(m_cutStageT / .35f); break;
	case 1: case 2: cover = 1.f - Smooth01((m_cutStage == 1 ? m_cutStageT : 1.f) / .3f); break;
	case 3: cover = Smooth01(m_cutStageT / .3f); break;
	case 4: cover = 1.f - Smooth01(m_cutStageT / .3f); break;
	}
	BOOL arena = m_cutStage >= 1 && m_cutStage <= 3;
	if (arena) {
		float bar = m_cutKind == 1 ? U(84) : (m_cutKind == 2 ? U(70) : U(56));
		DWORD edge = m_cutKind == 2 ? RGBA8(120, 196, 255) : (m_seqSkill >= 0 ? ElemColor(SkillInfo(m_seqSkill).element) : RGBA8(255, 220, 140));
		m_r.HudRect(0, 0, (float)m_w, bar, RGBA8(0, 0, 0, m_cutKind ? 230 : 200));
		m_r.HudRect(0, (float)m_h - bar, (float)m_w, bar, RGBA8(0, 0, 0, m_cutKind ? 230 : 200));
		if (m_cutKind) {
			m_r.HudRect(0, bar - U(4), (float)m_w, U(4), edge);
			m_r.HudRect(0, (float)m_h - bar, (float)m_w, U(4), edge);
		}
		auto hpBox = [&](int ui, float x, int align, float y, float row) {
			if (ui < 0) return;
			const Unit& u = m_b.u[ui];
			float w = U(280);
			float bx = align ? x - w : x;
			wchar_t nm[64];
			UnitTitle(u, nm, 64);
			DWORD fc = u.team == TEAM_PLAYER ? RGBA8(110, 170, 255) : FoeInk(u);
			float fs = row >= U(20) ? U(14) : max(U(9.f), row * .42f);
			float bh = max(U(3.f), min(U(8.f), row - fs - U(2)));
			m_r.HudText(nm, align ? bx + w : bx, y, fs, fc, align ? 2 : 0, 1);
			float frac = u.hpShow / max(1, u.ch.stat[S_HP]);
			Bar(bx, y + fs, w, bh, frac, HpFillCol(u, frac), HpBackCol(u));
		};
		auto col = [&](const int* id, int n, int align) {
			if (n < 1) return;
			float top = bar + U(4);
			float bot = (float)m_h - bar - U(4);
			float avail = max(U(12.f), bot - top);
			float row = avail / (float)n;
			if (row > U(22)) row = U(22);
			float y0 = top + (avail - n * row) * .5f;
			for (int i = 0; i < n; i++)
				hpBox(id[i], align ? (float)m_w - U(16) : U(16), align, y0 + i * row, row);
		};
		col(m_cutAlly, m_cutAllyN, 0);
		col(m_cutFoe, m_cutFoeN, 1);
		if (m_seqSkill >= 0) {
			const SkillDef& sd = SkillInfo(m_seqSkill);
			DWORD ec = m_cutKind == 2 ? RGBA8(150, 214, 255) : ElemColor(sd.element);
			float hold = m_cutKind == 1 ? (IsCraftSuper(m_seqSkill) ? 1.85f : 1.15f) : (m_cutKind == 2 ? 1.35f : (m_cutKind == 3 ? 1.45f : .8f));
			float u = m_cutStage == 1 ? Smooth01(m_cutStageT / .25f) : 1.f;
			float out = m_cutStage == 1 ? 1.f - Smooth01((m_cutStageT - hold) / .3f) : 0.f;
			float a = u * (m_cutStage == 1 ? out : 0.f);
			if (m_cutStage == 2) a = 0;
			if (m_cutStage == 1) {
				BOOL ougi = IsCraftSuper(m_seqSkill);
				BOOL craft = m_cutKind == 1;
				BOOL form = m_cutKind == 2;
				BOOL sum = m_cutKind == 3;
				const wchar_t* title = craft ? CraftName(m_seqSkill) : SkillLabel(m_seqSkill);
				float by = m_h * (ougi || form || sum ? .32f : (craft || m_ult ? .4f : .6f));
				float bh2 = ougi ? U(168) : (form || sum ? U(150) : (craft ? U(132) : (m_ult ? U(120) : U(84))));
				float slide = (1.f - u) * m_w;
				m_r.HudRectGrad(-slide, by, (float)m_w, bh2, RGBA8(0, 0, 0, (int)(210 * a)), RGBA8((ec & 255) / 3, ((ec >> 8) & 255) / 3, ((ec >> 16) & 255) / 3, (int)(230 * a)));
				float edgeH = (ougi || form || craft) ? U(7) : U(3);
				m_r.HudRect(-slide, by, (float)m_w, edgeH, RGBA8(ec & 255, (ec >> 8) & 255, (ec >> 16) & 255, (int)(255 * a)));
				m_r.HudRect(slide, by + bh2 - edgeH, (float)m_w, edgeH, RGBA8(255, 220, 120, (int)(255 * a)));
				if (craft || form || sum) {
					float pulse = .5f + .5f * sinf(m_time * 6.f);
					m_r.HudRect(m_w * .08f - slide, by + U(10), U(8), bh2 - U(20), RGBA8(255, 230, 160, (int)(180 * a * pulse)));
					m_r.HudRect(m_w * .92f + slide, by + U(10), U(8), bh2 - U(20), RGBA8(255, 230, 160, (int)(180 * a * pulse)));
				}
				m_r.HudText(title, m_w * .5f, by + bh2 * .48f, ougi ? U(72) : (form || sum ? U(56) : (craft ? U(58) : (m_ult ? U(64) : U(44)))), ec, 1, 1, 1, a);
				const wchar_t* kind = ougi ? TT(TX_OUGI) : (craft ? TT(TX_CRAFT) : (form ? TT(TX_SK_TAB_FORM) : (sum ? SummonWord() : (m_ult ? TT(TX_BT_ULT) : NULL))));
				if (kind) m_r.HudText(kind, m_w * .5f, by + U(22), form || ougi ? U(24) : U(18), form ? RGBA8(255, 220, 130) : RGBA8(255, 226, 150), 1, 1, 1, a);
				wchar_t nm[64];
				UnitTitle(m_b.u[m_cutA], nm, 64);
				m_r.HudText(nm, m_w * .5f, by + bh2 * .82f, U(16), RGBA8(255, 255, 255), 1, 0, 1, a);
			}
			if (m_cutStage == 2) {
				const wchar_t* title = m_cutKind == 1 ? CraftName(m_seqSkill) : SkillLabel(m_seqSkill);
				const wchar_t* kind = m_cutKind == 1 ? (IsCraftSuper(m_seqSkill) ? TT(TX_OUGI) : TT(TX_CRAFT)) : (m_cutKind == 2 ? TT(TX_SK_TAB_FORM) : (m_cutKind == 3 ? SummonWord() : NULL));
				if (kind) m_r.HudText(kind, U(24), U(22), U(14), RGBA8(255, 220, 140), 0, 1);
				m_r.HudText(title, U(24), kind ? U(42) : U(28), U(22), ec, 0, 1);
			}
		}
	}
	if (cover > 0) {
		int rows = 9;
		float rh = (float)m_h / rows + 1;
		for (int i = 0; i < rows; i++) {
			float lag = i * .06f;
			float c = Clampf(cover * (1.f + rows * .06f) - lag, 0, 1);
			float w = c * m_w;
			float y = i * ((float)m_h / rows);
			if (i & 1) m_r.HudRect((float)m_w - w, y, w, rh, RGBA8(8, 6, 14, 255));
			else m_r.HudRect(0, y, w, rh, RGBA8(8, 6, 14, 255));
			if (c > 0 && c < 1) {
				if (i & 1) m_r.HudRect((float)m_w - w - U(6), y, U(6), rh, RGBA8(255, 230, 160, 200));
				else m_r.HudRect(w, y, U(6), rh, RGBA8(255, 230, 160, 200));
			}
		}
	}
	if (m_flashWhite > 0) m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(255, 255, 255, (int)(m_flashWhite * 180)));
	// 稲妻の閃光。寒色で、見づらくならないよう強さは控えめに。
	if (m_wxFlash > 0) {
		int fa = (int)(min(1.f, m_wxFlash) * 118.f);
		m_r.HudRect(0, 0, (float)m_w, (float)m_h,
			RGBA8((int)(m_wxFlashCol[0] * 255), (int)(m_wxFlashCol[1] * 255), (int)(m_wxFlashCol[2] * 255), fa));
	}
}

void Game::PlaceBattleHud()
{
	float sw = (float)max(8, m_w);
	float sh = (float)max(8, m_h);
	float m = max(4.f, min(U(12), min(sw, sh) * 0.015f));
	m_hud.margin = m;
	int nMap = m_r.MinimapLayers();
	if (nMap < 1) nMap = 1;
	if (nMap > 3) nMap = 3;
	float availH = sh - m * 2.f;
	if (availH < 32.f) availH = max(16.f, sh - 2.f);
	float orderW = min(U(188.f), sw * 0.40f);
	float restNeed = min(sw * 0.42f, 120.f);
	if (sw - orderW - m * 2.f < restNeed) orderW = max(56.f, sw - restNeed - m * 2.f);
	if (orderW > sw - m * 2.f) orderW = max(40.f, sw - m * 2.f);
	m_hud.orderW = orderW;
	m_hud.orderX = sw - m - orderW;
	m_hud.col = m_hud.orderX;
	float head = min(U(30.f), availH * 0.08f);
	if (head < 12.f) head = 12.f;
	float arrow = min(U(16.f), availH * 0.04f);
	float orderWant = head + U(40.f) * 9.f + arrow;
	float mapGap = max(3.f, min(U(8.f), availH * 0.015f));
	float preferMs = U(nMap >= 3 ? 112.f : (nMap == 2 ? 148.f : 200.f));
	if (preferMs > orderW) preferMs = orderW;
	float mapWant = nMap * preferMs + (nMap - 1) * mapGap;
	float split = max(6.f, min(U(10.f), availH * 0.02f));
	float mapFloor = nMap * 20.f + (nMap - 1) * mapGap;
	if (orderWant + mapWant + split > availH) {
		float over = orderWant + mapWant + split - availH;
		float give = mapWant - mapFloor;
		if (give < 0) give = 0;
		float take = min(over, give);
		mapWant -= take;
		over -= take;
		if (over > 0) orderWant -= over;
	}
	float minOrder = head + 36.f;
	if (orderWant < minOrder) orderWant = minOrder;
	if (orderWant > availH - mapFloor - split) orderWant = max(minOrder * 0.55f, availH - mapFloor - split);
	m_hud.orderMaxB = m + orderWant;
	float panelH = min(U(148.f), sh * 0.30f);
	float midMin = min(64.f, sh * 0.16f);
	if (panelH > sh - m * 2.f - midMin) panelH = max(28.f, sh - m * 2.f - midMin);
	m_hud.panelH = panelH;
	m_hud.panelTop = sh - m - panelH;
	m_hud.logH = min(U(132.f), panelH * 0.92f);
	if (m_hud.logH < 24.f) m_hud.logH = min(24.f, panelH);
	m_hud.logTop = sh - m - m_hud.logH;
	m_hud.stageR = m;
	m_hud.stageB = m + min(U(70.f), sh * 0.12f);
	m_hud.partyT = m_hud.stageB + 4.f;
	m_hud.partyR = m;
	m_hud.partyB = m_hud.partyT;
}

static void DrawAilment(Renderer& r, int kind, float x, float y, float s, float t)
{
	float plate = s * 1.25f;
	r.HudRect(x - plate * .5f, y - plate * .5f, plate, plate, RGBA8(248, 248, 252, 214));
	if (kind == 0) {
		for (int i = 0; i < 3; i++) {
			float ph = fmodf(t * 0.62f + i * 0.33f, 1.f);
			float a = sinf(ph * 3.14159265f);
			int al = (int)(a * 235.f);
			if (al < 12) continue;
			float fs = s * (0.38f + i * 0.14f + ph * 0.18f);
			r.HudText(L"Z", x + (i - 1) * s * 0.28f, y + s * 0.22f - ph * s * 1.15f, fs, RGBA8(60, 110, 200, al), 1, 1);
		}
	} else if (kind == 1) {
		for (int i = 0; i < 6; i++) {
			float a = t * 2.6f + i * 1.047f;
			float px = x + cosf(a) * s * 0.34f;
			float py = y + sinf(a) * s * 0.2f;
			float d = s * 0.07f;
			r.HudRect(px - d, py - d, d * 2.f, d * 2.f, RGBA8(190, 60, 180));
		}
		r.HudText(L"?", x + sinf(t * 5.f) * s * 0.08f, y, s * 0.55f, RGBA8(150, 40, 150), 1, 1);
	} else if (kind == 2) {
		r.HudRect(x - s * .42f, y - s * .42f, s * .84f, s * .84f, RGBA8(40, 28, 8, 230));
		for (int i = 0; i < 3; i++) {
			float a = t * 4.2f + i * 2.094f;
			float px = x + cosf(a) * s * 0.22f;
			float py = y + sinf(a) * s * 0.12f;
			float arm = s * 0.22f;
			float th = max(2.f, s * 0.11f);
			r.HudRect(px - arm, py - th * .5f, arm * 2.f, th, RGBA8(255, 220, 40));
			r.HudRect(px - th * .5f, py - arm, th, arm * 2.f, RGBA8(255, 180, 20));
		}
		r.HudText(L"!", x, y, s * 0.42f, RGBA8(255, 244, 180), 1, 1);
	} else if (kind == 3) {
		for (int i = 0; i < 4; i++) {
			float ph = fmodf(t * 0.9f + i * 0.25f, 1.f);
			float bx = x + sinf(t * 2.1f + i * 1.7f) * s * 0.26f;
			float by = y + s * 0.28f - ph * s * 1.05f;
			float d = s * (0.07f + ph * 0.12f);
			int al = (int)((1.f - ph) * 220.f);
			r.HudRect(bx - d, by - d, d * 2.f, d * 2.f, RGBA8(70, 170, 50, al));
			r.HudRect(bx - d * .4f, by - d * .4f, d * .7f, d * .7f, RGBA8(170, 240, 90, al));
		}
	} else if (kind == 4) {
		r.HudFrame(x - s * .28f, y - s * .28f, s * .56f, s * .56f, max(1.f, s * .06f), RGBA8(40, 70, 150));
		float a = t * 2.4f;
		r.HudRect(x + cosf(a) * s * 0.16f - s * .05f, y + sinf(a) * s * 0.16f - s * .05f, s * .1f, s * .1f, RGBA8(30, 60, 140));
	} else if (kind == 5) {
		float p = 0.7f + 0.3f * sinf(t * 4.f);
		r.HudRect(x - s * .06f, y - s * .28f * p, s * .12f, s * .36f * p, RGBA8(180, 90, 40));
		r.HudRect(x - s * .16f, y + s * .08f, s * .32f, s * .1f, RGBA8(180, 90, 40));
	} else if (kind == 6) {
		r.HudRect(x - s * .28f, y - s * .28f, s * .56f, s * .56f, RGBA8(170, 220, 255, 230));
		r.HudFrame(x - s * .28f, y - s * .28f, s * .56f, s * .56f, max(1.f, s * .06f), RGBA8(230, 250, 255));
		r.HudRect(x - s * .06f, y - s * .18f, s * .12f, s * .36f, RGBA8(255, 255, 255, 180));
	} else if (kind == 7) {
		for (int i = 0; i < 3; i++) {
			float ph = fmodf(t * 1.4f + i * 0.33f, 1.f);
			float fy = y + s * 0.2f - ph * s * 0.7f;
			float fw = s * (0.16f + i * 0.05f);
			r.HudRect(x - fw * .5f, fy, fw, s * 0.22f, RGBA8(255, (int)(80 + ph * 120), 20, (int)(220 * (1.f - ph))));
		}
	} else if (kind == 8) {
		for (int i = 0; i < 3; i++) {
			float ph = fmodf(t * 1.1f + i * 0.37f, 1.f);
			float dy = y - s * 0.1f + ph * s * 0.45f;
			float d = s * (0.08f + (1.f - ph) * 0.06f);
			r.HudRect(x + (i - 1) * s * 0.16f - d, dy, d * 2.f, d * 2.4f, RGBA8(190, 20, 30, (int)(230 * (1.f - ph * 0.4f))));
		}
	} else if (kind == 9) {
		r.HudRect(x - s * .3f, y - s * .12f, s * .6f, s * .24f, RGBA8(20, 20, 28, 230));
		r.HudRect(x - s * .22f, y - s * .08f, s * .16f, s * .16f, RGBA8(80, 80, 90));
		r.HudRect(x + s * .06f, y - s * .08f, s * .16f, s * .16f, RGBA8(80, 80, 90));
	} else {
		r.HudRect(x - s * .26f, y - s * .18f, s * .52f, s * .36f, RGBA8(40, 28, 70, 230));
		r.HudRect(x - s * .16f, y - s * .04f, s * .32f, s * .08f, RGBA8(220, 210, 255));
	}
}

static int WeatherTx(int w)
{
	switch (w) {
	case WX_RAIN: return TX_WX_RAIN;
	case WX_SNOW: return TX_WX_SNOW;
	case WX_ASH: return TX_WX_ASH;
	case WX_FOG: return TX_WX_FOG;
	case WX_STORM: return TX_WX_STORM;
	case WX_SAND: return TX_WX_SAND;
	case WX_HAIL: return TX_WX_HAIL;
	case WX_PETAL: return TX_WX_PETAL;
	case WX_HEAT: return TX_WX_HEAT;
	default: return 0;
	}
}

static int HourTx(int h)
{
	switch (h) {
	case HR_NOON: return TX_HR_NOON;
	case HR_DAWN: return TX_HR_DAWN;
	case HR_MORNING: return TX_HR_MORNING;
	case HR_DUSK: return TX_HR_DUSK;
	case HR_NIGHT: return TX_HR_NIGHT;
	case HR_DEEP: return TX_HR_DEEP;
	case HR_ECLIPSE: return TX_HR_ECLIPSE;
	case HR_DREAM: return TX_HR_DREAM;
	case HR_STAR: return TX_HR_STAR;
	case HR_MOON: return TX_HR_MOON;
	default: return 0;
	}
}

void Game::DrawBattleUi()
{
	int partyLook = -1;
	if (m_stashFull) {
		if (m_demo) { m_stashFull = 0; m_shopFit = 0; }
		else {
			m_shopFit = 5;
			DrawShopFit();
			if (m_shopFit != 5) m_stashFull = 0;
			m_click = FALSE;
			return;
		}
	}
	if (m_slotUi) { DrawSlotUi(); return; }
	if (m_seqCut && m_cutStage >= 1 && m_cutStage <= 3) {
		DrawCutInUi();
		return;
	}
	PlaceBattleHud();
	BOOL stampHov = FALSE, varHov = FALSE;
	float stampX = 0.f, stampW = 0.f, varX = 0.f, varW = 0.f;
	// 左上: ステージ情報。スタンピードはその段のあいだ出し続ける。
	{
		wchar_t l[96], th[128], info[220];
		StageCaption(m_battleStage, l, 96);
		int wx = WeatherTx(TacWx(m_b.weather));
		int hx = HourTx(TacHour(m_b.weather));
		if (wx && hx) swprintf_s(th, L"%s  %s  %s", TT(ThemeNameTx(m_b.map.theme)), TT(wx), TT(hx));
		else if (wx) swprintf_s(th, L"%s  %s", TT(ThemeNameTx(m_b.map.theme)), TT(wx));
		else if (hx) swprintf_s(th, L"%s  %s", TT(ThemeNameTx(m_b.map.theme)), TT(hx));
		else wcscpy_s(th, TT(ThemeNameTx(m_b.map.theme)));
		swprintf_s(info, L"%s %s   %s %s / %s %s   %s %s", TT(TX_BT_ENDTURN), TacNum(m_b.turnCount + 1), TT(TX_BT_ALLY), TacNum(m_b.AliveCount(TEAM_PLAYER)), TT(TX_BT_ENEMY), TacNum(m_b.AliveCount(TEAM_ENEMY)), TT(TX_GOLD), TacNum(m_save.gold));
		const wchar_t* sp = m_b.stampede ? TT(TX_BT_STAMPEDE) : NULL;
		int varTx = 0;
		DWORD varCol = RGBA8(220, 220, 230), varHot = RGBA8(255, 255, 255);
		if (m_b.sweep == TAC_VAR_SWEEP) { varTx = TX_BT_SWEEP; varCol = RGBA8(150, 220, 170); varHot = RGBA8(210, 255, 220); }
		else if (m_b.sweep == TAC_VAR_REMNANT) { varTx = TX_BT_REMNANT; varCol = RGBA8(210, 180, 110); varHot = RGBA8(240, 220, 160); }
		else if (m_b.sweep == TAC_VAR_BOSS) { varTx = TX_BT_BOSSMAP; varCol = RGBA8(255, 160, 80); varHot = RGBA8(255, 210, 150); }
		else if (m_b.sweep == TAC_VAR_SCAR) { varTx = TX_BT_SCAR; varCol = RGBA8(220, 130, 130); varHot = RGBA8(255, 190, 180); }
		else if (m_b.sweep == TAC_VAR_MIST) { varTx = TX_BT_MIST; varCol = RGBA8(170, 190, 210); varHot = RGBA8(220, 235, 245); }
		else if (m_b.sweep == TAC_VAR_RAID) { varTx = TX_BT_RAID; varCol = RGBA8(150, 160, 220); varHot = RGBA8(200, 210, 255); }
		else if (m_b.sweep == TAC_VAR_AMBUSH) { varTx = TX_BT_AMBUSH; varCol = RGBA8(220, 150, 80); varHot = RGBA8(255, 200, 140); }
		else if (m_b.sweep == TAC_VAR_TWIN) { varTx = TX_BT_TWIN; varCol = RGBA8(220, 110, 150); varHot = RGBA8(255, 180, 200); }
		else if (m_b.sweep == TAC_VAR_VAULT) { varTx = TX_BT_VAULT; varCol = RGBA8(220, 190, 80); varHot = RGBA8(255, 230, 150); }
		else if (m_b.sweep == TAC_VAR_BLESS) { varTx = TX_BT_BLESS; varCol = RGBA8(140, 210, 180); varHot = RGBA8(200, 245, 220); }
		const wchar_t* swp = varTx ? TT(varTx) : NULL;
		float fs = U(17), sfs = U(13), ifs = U(12);
		float need = U(42) + m_r.TextWidth(l, fs, 0) + U(18) + m_r.TextWidth(th, sfs, 0);
		float infoNeed = m_r.TextWidth(info, ifs, 0) + U(28);
		if (need < infoNeed) need = infoNeed;
		if (sp) need += U(14) + m_r.TextWidth(sp, sfs, 0);
		if (swp) need += U(14) + m_r.TextWidth(swp, sfs, 0);
		float lookW = (!m_statOpen && m_cur >= 0 && m_phase == PH_P_CMD) ? (U(90) + U(8)) : 0.f;
		float btnW = U(76) + U(8) + lookW;
		float maxW = m_hud.col - U(14) - btnW - U(8);
		BOOL buttonsBelow = FALSE;
		if (maxW < U(150)) { buttonsBelow = TRUE; maxW = m_hud.col - U(14) - U(8); }
		float w = min(max(U(250.f), need), max(72.f, maxW));
		float h = U(58);
		Panel(U(14), U(14), w, h);
		float titleMax = max(48.f, w * 0.42f);
		float titlePx = FitTextPx(l, titleMax, fs, 0);
		m_r.HudText(l, U(28), U(32), titlePx, RGBA8(255, 226, 150), 0, 1);
		float tagX = U(28) + m_r.TextWidth(l, titlePx, 0);
		if (sp) {
			stampX = tagX + U(12);
			stampW = m_r.TextWidth(sp, sfs, 0);
			stampHov = !m_navMode && !m_statOpen && Hit(stampX - U(6), U(16), stampW + U(12), U(32));
			m_r.HudText(sp, stampX, U(32), sfs, stampHov ? RGBA8(255, 220, 160) : RGBA8(255, 150, 70), 0, 1);
			tagX = stampX + stampW;
		}
		if (swp) {
			varX = tagX + U(12);
			varW = m_r.TextWidth(swp, sfs, 0);
			varHov = !m_navMode && !m_statOpen && Hit(varX - U(6), U(16), varW + U(12), U(32));
			m_r.HudText(swp, varX, U(32), sfs, varHov ? varHot : varCol, 0, 1);
			tagX = varX + varW;
		}
		float themeRight = U(14) + w - U(14);
		float themeMax = themeRight - tagX - U(12);
		if (themeMax < 16.f) themeMax = 16.f;
		m_r.HudText(th, themeRight, U(32), FitTextPx(th, themeMax, sfs, 0), RGBA8(220, 220, 230), 2, 0);
		float infoInner = w - U(28);
		if (infoInner < 8.f) infoInner = 8.f;
		m_r.HudText(info, U(28), U(54), FitTextPx(info, infoInner, ifs, 0), RGBA8(210, 210, 220), 0, 0);
		float mx = buttonsBelow ? U(14) : (U(14) + w + U(8));
		float my = buttonsBelow ? (U(14) + h + U(4)) : U(14);
		BOOL turn = m_phase == PH_P_MOVE || m_phase == PH_P_CMD;
		if (!m_statOpen && Btn(ID_BT_MENU, mx, my, U(76), U(30), TT(TX_BT_MENU), turn, FALSE, U(13))) {
			m_phaseBeforeMenu = m_phase; m_phase = PH_MENU; m_menuConfirm = 0; m_focusId = ID_BT_M_RESUME;
		}
		if (!m_statOpen && m_cur >= 0 && m_phase == PH_P_CMD && Btn(ID_BT_LOOK, mx + U(84), my, U(90), U(30), TT(TX_BT_STATUS), TRUE, FALSE, U(13))) {
			m_statWho = m_cur; m_statOpen = 1; m_navMode = TRUE; m_focusId = ID_BT_BACK;
		}
		float panelBottom = U(14) + h;
		float btnBottom = my + U(30);
		m_hud.stageB = panelBottom > btnBottom ? panelBottom : btnBottom;
		m_hud.stageR = mx + U(76) + (lookW > 0.f ? U(98) : 0.f);
		if (m_hud.stageR > m_hud.col - 4.f) m_hud.stageR = m_hud.col - 4.f;
		m_hud.partyT = m_hud.stageB + 6.f;
	}
	float partyRight = m_hud.margin;
	float partyBottom = m_hud.partyT;
	{
		int ids[MAX_PARTY];
		int nP = 0;
		for (int i = 0; i < m_b.nUnits && nP < MAX_PARTY; i++) if (m_b.u[i].team == TEAM_PLAYER) ids[nP++] = i;
		float x0 = U(10);
		float top = m_hud.partyT;
		{
			float gap0 = max(6.f, U(12));
			float panelX0 = m_hud.margin;
			float room0 = m_hud.col - gap0 - panelX0;
			if (room0 < 48.f) room0 = max(32.f, m_hud.col - panelX0 - 4.f);
			float panelW0 = min(U(460.f), max(min(U(220.f), room0), room0 * .46f));
			if (panelW0 > room0) panelW0 = room0;
			int nFit = 0;
			for (int i = 0; i < m_b.nUnits; i++) if (m_b.u[i].team == TEAM_PLAYER) nFit++;
			float minRow = max(16.f, U(18));
			float listNeed = nFit * minRow;
			float roomCard = (float)m_h - m_hud.margin - (top + listNeed);
			if (roomCard < U(120)) roomCard = U(120);
			const Unit* curU = (m_cur >= 0 && m_cur < m_b.nUnits) ? &m_b.u[m_cur] : NULL;
			float cardPort = (curU && curU->team != TEAM_PLAYER) ? U(118.f) : U(96.f);
			float cardH = FitCardH(m_r, curU, panelW0, TRUE, cardPort, U(30), U(156), U(12), roomCard, NULL);
			float topLim = (float)m_h - m_hud.margin - cardH;
			if (topLim < top + 8.f) topLim = top + 8.f;
			m_hud.panelH = (float)m_h - m_hud.margin - topLim;
			m_hud.panelTop = topLim;
		}
		float bot = m_hud.panelTop - 4.f;
		if (bot < top + 16.f) bot = top + 16.f;
		float limit = m_hud.col - U(8);
		float colW = min(U(292.f), max(96.f, limit - x0));
		if (nP > 0 && colW >= 72.f && top + 8.f < bot) {
			float avail = bot - top;
			if (avail < 8.f) avail = 8.f;
			float row = avail / (float)nP;
			float rowCap = U(50.f);
			if (row > rowCap) row = rowCap;
			float listH = row * (float)nP;
			m_r.HudRect(x0, top, colW, listH, RGBA8(10, 14, 24, 176));
			m_r.HudFrame(x0, top, colW, listH, 1.f, RGBA8(150, 128, 86, 180));
			partyRight = x0 + colW;
			partyBottom = top + listH;
			for (int k = 0; k < nP; k++) {
				const Unit& u = m_b.u[ids[k]];
				float ay = top + k * row;
				float rh = row;
				wchar_t nm[48], lv[32], job[64], fr[64];
				UnitTitle(u, nm, 48);
				swprintf_s(lv, L"%s%s", TT(TX_MISC_LV), TacNum(u.ch.lv));
				wcscpy_s(job, ClassName(u.ch.cls));
				int need = ExpToNext(u.ch.lv);
				int showExp = u.ch.lv >= TAC_LV_MAX ? 0 : u.ch.exp;
				int showNeed = u.ch.lv >= TAC_LV_MAX ? 0 : need;
				swprintf_s(fr, L"%s %s/%s", TT(TX_MISC_EXP), TacNum(showExp), TacNum(showNeed));
				BOOL rowHit = Hit(x0, ay, colW, rh);
				if (rowHit) m_r.HudRect(x0, ay, colW, max(1.f, rh - 1.f), u.alive ? RGBA8(255, 255, 255, 28) : RGBA8(255, 160, 140, 46));
				BOOL lookOk = m_phase != PH_P_MOVE && m_phase != PH_P_TARGET && m_phase != PH_P_SKILLTARGET
					&& m_phase != PH_P_SKILLS && m_phase != PH_P_SUMMON && m_phase != PH_P_SHOVE && m_phase != PH_P_ITEM
					&& m_phase != PH_ACTION && m_phase != PH_MOVING && m_phase != PH_E_MOVE
					&& m_phase != PH_INTRO && m_phase != PH_OVER && m_phase != PH_LEVELUP && m_phase != PH_MENU;
				if (lookOk && !m_statOpen && m_mouseSeen && rowHit) partyLook = ids[k];
				if (!m_statOpen && m_click && rowHit && m_phase != PH_ACTION && m_phase != PH_MOVING && m_phase != PH_E_MOVE && m_phase != PH_INTRO && m_phase != PH_OVER && m_phase != PH_LEVELUP
					&& m_phase != PH_P_MOVE && m_phase != PH_P_TARGET && m_phase != PH_P_SKILLTARGET && m_phase != PH_P_SHOVE) {
					m_statWho = ids[k];
					m_statEq = 0;
					m_statOpen = 1;
					m_navMode = TRUE;
					m_focusId = ID_BT_BACK;
					m_click = FALSE;
					TacSfx(TS_OPEN);
				}
				DWORD nc = u.alive ? RGBA8(230, 240, 255) : RGBA8(160, 160, 170);
				DWORD ic = u.alive ? RGBA8(255, 226, 150) : RGBA8(150, 140, 110);
				float pad = U(4);
				float inner = colW - pad * 2.f;
				float padY = max(1.f, rh * 0.06f);
				float gapIn = max(1.f, rh * 0.04f);
				BOOL showBars = rh >= 20.f;
				BOOL showExpBar = rh >= 22.f;
				float barH = showBars ? min(U(3.5f), max(2.f, rh * 0.09f)) : 0.f;
				float expBarH = showExpBar ? min(U(3.f), max(2.f, rh * 0.07f)) : 0.f;
				float textBlock = rh - padY * 2.f - (showBars ? barH + gapIn : 0.f) - (showExpBar ? expBarH + gapIn : 0.f);
				if (textBlock < 6.f) textBlock = 6.f;
				float line = textBlock * 0.5f;
				float fs = min(U(13.f), line * 0.62f);
				float fs2 = min(U(11.f), line * 0.58f);
				float y1 = ay + padY + line * 0.5f;
				float y2 = y1 + line;
				float lvMax = min(inner * 0.34f, U(78.f));
				float nameMax = inner - lvMax - U(4);
				if (nameMax < 12.f) nameMax = 12.f;
				m_r.HudText(nm, x0 + pad, y1, FitTextPx(nm, nameMax, fs, 0), nc, 0, 0);
				m_r.HudText(lv, x0 + colW - pad, y1, FitTextPx(lv, lvMax, fs, 0), ic, 2, 0);
				float jobMax = inner * 0.42f;
				float expMax = inner - jobMax - U(4);
				if (expMax < 16.f) { expMax = inner * 0.62f; jobMax = inner - expMax - U(4); }
				if (jobMax < 12.f) jobMax = 12.f;
				m_r.HudText(job, x0 + pad, y2, FitTextPx(job, jobMax, fs2, 0), ic, 0, 0);
				m_r.HudText(fr, x0 + colW - pad, y2, FitTextPx(fr, expMax, fs2, 0), ic, 2, 0);
				if (showExpBar) {
					float ey = ay + rh - padY - (showBars ? barH + gapIn : 0.f) - expBarH;
					float expF = (u.ch.lv >= TAC_LV_MAX || showNeed <= 0) ? 1.f : (float)showExp / (float)showNeed;
					if (expF < 0.f) expF = 0.f;
					if (expF > 1.f) expF = 1.f;
					Bar(x0 + pad, ey, inner, expBarH, expF, u.alive ? RGBA8(120, 210, 255) : RGBA8(90, 110, 130), RGBA8(24, 36, 56));
				}
				if (!showBars) continue;
				int bars = 1 + (u.ch.stat[S_MP] > 0 ? 1 : 0) + 1;
				float gapb = max(1.f, U(2));
				float bw = (inner - gapb * (float)(bars - 1)) / (float)bars;
				if (bw < 4.f) bw = 4.f;
				float by = ay + rh - padY - barH;
				float bx = x0 + pad;
				float hpF = u.alive ? u.hpShow / max(1, u.ch.stat[S_HP]) : 0.f;
				Bar(bx, by, bw, barH, hpF, u.alive ? RGBA8(90, 210, 120) : RGBA8(90, 90, 100), RGBA8(20, 24, 32));
				bx += bw + gapb;
				if (u.ch.stat[S_MP] > 0) {
					float mf = u.alive ? (float)u.mp / max(1, u.ch.stat[S_MP]) : 0.f;
					Bar(bx, by, bw, barH, mf, u.alive ? RGBA8(90, 150, 255) : RGBA8(90, 100, 120), RGBA8(20, 28, 48));
					bx += bw + gapb;
				}
				float cf = (float)u.cp / (float)CP_MAX;
				Bar(bx, by, bw, barH, cf, u.cp >= 100 ? RGBA8(255, 210, 70) : RGBA8(210, 160, 50), RGBA8(36, 28, 12));
			}
		}
	}
	m_hud.partyR = partyRight;
	m_hud.partyB = partyBottom;
	int aimMark[MAX_UNITS];
	int aimTotal = 0;
	memset(aimMark, 0, sizeof(aimMark));
	wchar_t aimCount[48];
	wchar_t aimTag[2][160];
	aimCount[0] = aimTag[0][0] = aimTag[1][0] = 0;
	int aimTagN = 0;
	float aimSx = 0, aimSy = 0;
	int aimHold = 0;
	if ((m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET) && m_cur >= 0 && m_cursorValid && CursorInAttackRange()) {
		int tg[MAX_UNITS];
		int n = 0;
		if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0)
			n = SkillTargets(m_b, m_cur, m_cmdSkill, m_cx, m_cz, tg, MAX_UNITS);
		else {
			int t = m_b.UnitAt(m_cx, m_cz, m_cLayer);
			if (t >= 0 && CanTarget(m_cur, t, -1)) tg[n++] = t;
		}
		aimTotal = n;
		for (int k = 0; k < n; k++) if (tg[k] >= 0 && tg[k] < MAX_UNITS) aimMark[tg[k]] = k + 1;
	}
	float usedSx[MAX_UNITS], usedSy[MAX_UNITS];
	int usedN = 0;
	float barBx[48], barBy[48], barBw[48], barBh[48];
	int barBn = 0;
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (u.team == TEAM_ENEMY && (!u.alive || u.dissolve >= 1.f || !u.alert)) continue;
		float dx = u.px - m_camTarget.x, dz = u.pz - m_camTarget.z;
		if (u.team != TEAM_PLAYER && dx * dx + dz * dz > 28.f * 28.f) continue;
		if (u.team == TEAM_ENEMY) {
			int sight = TacSightTiles(m_b.weather);
			if (sight > 0) {
				int nearest = 9999;
				for (int p = 0; p < m_b.nUnits; p++) {
					if (!m_b.u[p].alive || m_b.u[p].team != TEAM_PLAYER) continue;
					int d = Dist(u.x, u.z, m_b.u[p].x, m_b.u[p].z);
					if (d < nearest) nearest = d;
				}
				if (nearest > sight) continue;
			}
		}
		float sx, sy;
		if (!m_r.ProjectToScreen(v3(u.px, u.py + m_act[i].height + .12f, u.pz), sx, sy)) continue;
		float bw = U(36);
		int cells = 0;
		if (u.team == TEAM_ENEMY) {
			cells = 9999;
			if (m_cur >= 0 && m_cur < m_b.nUnits && m_b.u[m_cur].alive)
				cells = Dist(m_b.u[m_cur].x, m_b.u[m_cur].z, u.x, u.z);
			else {
				for (int p = 0; p < m_b.nUnits; p++) {
					if (!m_b.u[p].alive || m_b.u[p].team != TEAM_PLAYER) continue;
					int d = Dist(m_b.u[p].x, m_b.u[p].z, u.x, u.z);
					if (d < cells) cells = d;
				}
			}
			if (cells > 9000) cells = 0;
		}
		float fade = 1.f;
		if (u.team == TEAM_ENEMY) {
			if (cells <= 4) fade = 1.f;
			else if (cells >= 28) fade = 0.22f;
			else fade = 1.f - (cells - 4) * (0.78f / 24.f);
			if (fade < 0.22f) fade = 0.22f;
		}
		auto fadeA = [&](DWORD c) -> DWORD {
			int a = (int)(((c >> 24) & 255) * fade + 0.5f);
			if (a < 1) a = 1;
			if (a > 255) a = 255;
			return (c & 0x00ffffff) | ((DWORD)a << 24);
		};
		BOOL off = (sx < 0.f || sy < 0.f || sx > (float)m_w || sy > (float)m_h);
		float xMin = bw * .5f + U(4);
		float xMax = m_hud.orderX - U(8) - bw * .5f;
		if (xMax < xMin) xMax = xMin;
		float yMin = U(10);
		float yMax = (float)m_h - U(18);
		if (sx + bw * .5f > m_hud.orderX - U(4))
			sx = xMax;
		for (int uix = 0; uix < usedN; uix++) {
			if (fabsf(sy - usedSy[uix]) < U(16) && fabsf(sx - usedSx[uix]) < bw + U(8))
				sx += (uix & 1) ? U(22) : -U(22);
		}
		if (sx < xMin) sx = xMin;
		if (sx > xMax) sx = xMax;
		if (sy < yMin) sy = yMin;
		if (sy > yMax) sy = yMax;
		if (usedN < MAX_UNITS) { usedSx[usedN] = sx; usedSy[usedN] = sy; usedN++; }
		if (barBn < 48) {
			barBx[barBn] = sx - bw * .5f - U(20);
			barBy[barBn] = sy - U(20);
			barBw[barBn] = bw + U(40);
			barBh[barBn] = U(36);
			barBn++;
		}
		float frac = u.hpShow / max(1, u.ch.stat[S_HP]);
		DWORD col = !u.alive ? RGBA8(150, 150, 160) : (u.team == TEAM_PLAYER ? RGBA8(80, 220, 120) : FoeBarCol(u));
		m_r.HudRect(sx - bw * .5f - 1, sy - 1, bw + 2, U(6), fadeA(RGBA8(8, 8, 12, 200)));
		m_r.HudRect(sx - bw * .5f, sy, bw * Clampf(frac, 0, 1), U(4), fadeA(col));
		if (UnitIsLucky(u) && u.alive)
			m_r.HudText(L"☆", sx - bw * .5f - U(2), sy - U(1), U(11), fadeA(RGBA8(80, 235, 255)), 2, 0, 1, fade);
		if (u.team == TEAM_ENEMY && (off || cells >= 4)) {
			wchar_t dlab[24];
			swprintf_s(dlab, L"%d%s", cells, LangPick14(L"マス", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L""));
			m_r.HudText(dlab, sx, sy - U(8), U(9), RGBA8(255, 236, 200), 1, 0, 1, fade);
		}
		if (i >= 0 && i < MAX_UNITS && aimMark[i] && aimTotal > 0) {
			wchar_t nb[8];
			swprintf_s(nb, L"%d", aimTotal);
			float bs = U(16);
			float bx = sx + bw * .5f + U(3);
			if (bx + bs > m_hud.orderX - U(2)) bx = sx - bw * .5f - bs - U(3);
			m_r.HudRect(bx, sy - U(2), bs, bs, fadeA(RGBA8(28, 16, 6, 235)));
			m_r.HudFrame(bx, sy - U(2), bs, bs, 1.f, fadeA(RGBA8(255, 214, 90)));
			m_r.HudText(nb, bx + bs * .5f, sy - U(2) + bs * .5f, U(11), fadeA(RGBA8(255, 236, 160)), 1, 1, 1, fade);
		}
		if (u.ch.stat[S_MP] > 0) {
			float mf = (float)u.mp / max(1, u.ch.stat[S_MP]);
			m_r.HudRect(sx - bw * .5f - 1, sy + U(5), bw + 2, U(4), fadeA(RGBA8(8, 8, 12, 200)));
			m_r.HudRect(sx - bw * .5f, sy + U(6), bw * Clampf(mf, 0, 1), U(3), fadeA(u.alive ? RGBA8(80, 150, 255) : RGBA8(90, 100, 120)));
		}
		{
			float cf = (float)u.cp / (float)CP_MAX;
			float cy = sy + (u.ch.stat[S_MP] > 0 ? U(10) : U(5));
			m_r.HudRect(sx - bw * .5f - 1, cy, bw + 2, U(4), fadeA(RGBA8(8, 8, 12, 200)));
			m_r.HudRect(sx - bw * .5f, cy + 1, bw * Clampf(cf, 0, 1), U(3), fadeA(u.cp >= 100 ? RGBA8(255, 210, 70) : RGBA8(210, 160, 50)));
		}
		// カットシーンでも異常アイコンは出す。行動中は着弾前の m_visAil（予測で先に変わらない）。
		{
			VisAil vi = (m_phase == PH_ACTION) ? m_visAil[i] : VisAil{};
			if (m_phase != PH_ACTION) {
				vi.poison = u.poison; vi.slow = u.slow; vi.defDown = u.defDown;
				vi.sleep = u.sleep; vi.confuse = u.confuse; vi.stun = u.stun; vi.freeze = u.freeze;
				vi.burn = u.burn; vi.bleed = u.bleed; vi.blind = u.blind; vi.silence = u.silence;
			}
		if (u.alive && (vi.sleep > 0 || vi.confuse > 0 || vi.stun > 0 || vi.poison > 0 || vi.slow > 0 || vi.defDown > 0 || vi.freeze > 0 || vi.burn > 0 || vi.bleed > 0 || vi.blind > 0 || vi.silence > 0)) {
			int kinds[12], efs[12], nk = 0;
			if (vi.sleep > 0) { kinds[nk] = 0; efs[nk++] = EF_SLEEP; }
			if (vi.confuse > 0) { kinds[nk] = 1; efs[nk++] = EF_CONFUSE; }
			if (vi.stun > 0) { kinds[nk] = 2; efs[nk++] = EF_STUN; }
			if (vi.poison > 0) { kinds[nk] = 3; efs[nk++] = EF_POISON; }
			if (vi.slow > 0) { kinds[nk] = 4; efs[nk++] = EF_SLOW; }
			if (vi.defDown > 0) { kinds[nk] = 5; efs[nk++] = EF_DEFDOWN; }
			if (vi.freeze > 0) { kinds[nk] = 6; efs[nk++] = EF_FREEZE; }
			if (vi.burn > 0) { kinds[nk] = 7; efs[nk++] = EF_BURN; }
			if (vi.bleed > 0) { kinds[nk] = 8; efs[nk++] = EF_BLEED; }
			if (vi.blind > 0) { kinds[nk] = 9; efs[nk++] = EF_BLIND; }
			if (vi.silence > 0) { kinds[nk] = 10; efs[nk++] = EF_SILENCE; }
			float s = U(26);
			float gap = s + U(4);
			float x0 = sx - (nk - 1) * gap * .5f;
			float y0 = sy - U(42);
			for (int k = 0; k < nk; k++) {
				float ix = x0 + k * gap;
				DrawAilment(m_r, kinds[k], ix, y0, s, m_time + i * 0.13f + k);
				float plate = s * 1.25f;
				if (m_mouseSeen && Hit(ix - plate * .5f, y0 - plate * .5f, plate, plate))
					m_r.HudText(EffectWord(efs[k]), ix, y0 - s * 0.95f, U(12), RGBA8(255, 255, 255), 1, 1);
			}
		}
		} // vis ail block
	}
	if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0 && m_cursorValid && m_cur >= 0) {
		const SkillDef& sd = SkillInfo(m_cmdSkill);
		const Unit& atk = m_b.u[m_cur];
		wchar_t count[48] = L"";
		BOOL inRange = CursorInAttackRange();
		BOOL showCount = inRange && sd.area > 0;
		if (showCount) {
			int tg[MAX_UNITS];
			int nt = SkillTargets(m_b, m_cur, m_cmdSkill, m_cx, m_cz, tg, MAX_UNITS);
			swprintf_s(count, L"%s %d", TT(TX_BT_AOEN), nt);
		}
		wchar_t tags[10][48];
		int nt = 0;
		int def = m_b.UnitAt(m_cx, m_cz, m_cLayer);
		BOOL hostile = def >= 0 && m_b.u[def].alive && m_b.u[def].team != atk.team && sd.kind != SK_HEAL && sd.kind != SK_BUFF;
		if (hostile && CanTarget(m_cur, def, m_cmdSkill)) {
			Forecast f;
			CalcForecast(m_b, m_cur, def, m_cmdSkill, f);
			const Unit& d = m_b.u[def];
			swprintf_s(tags[nt++], L"%s %+d%%", TT(f.face == 2 ? TX_BT_FACE_BACK : (f.face == 1 ? TX_BT_FACE_SIDE : TX_BT_FACE_FRONT)), f.face == 2 ? 25 : (f.face == 1 ? 10 : -10));
			if (f.tri > 0) wcscpy_s(tags[nt++], TT(TX_BT_TRI_ADV));
			if (f.tri < 0) wcscpy_s(tags[nt++], TT(TX_BT_TRI_DIS));
			if (f.bowFly) wcscpy_s(tags[nt++], TT(TX_BT_BOWFLY));
			if (f.weak > 0) {
				swprintf_s(tags[nt], L"%s ×%d", WeakLabel(), f.weak >= 2 ? 4 : 2);
				nt++;
			}
			int hs = BattleHeightSteps(m_b, atk.x, atk.z, d.x, d.z);
			if (hs > 0) wcscpy_s(tags[nt++], TT(TX_HIGH));
			else if (hs < 0) wcscpy_s(tags[nt++], TT(TX_LOW));
			{
				const wchar_t* ew = EffectWord(sd.effect);
				if (ew && nt < 10) {
					if (UnitResistsEffect(d, sd.effect))
						wcscpy_s(tags[nt++], LangPick14(L"無効", L"Blocked", L"Bloqué", L"Annullato", L"Anulado", L"무효", L"无效", L"مُبطل", L"Блок", L"Geblockt", L"Anulado", L"Geblokkeerd", L"Zablokowane", L"Geçersiz"));
					else wcscpy_s(tags[nt++], ew);
				}
			}
			if (f.follow > 0 && nt < 10) {
				swprintf_s(tags[nt], L"%s ×%d", TT(TX_BT_FOLLOW), f.follow);
				nt++;
			}
			if (nt < 10 && d.hp > 0 && ((long long)f.dmg >= (long long)d.hp * 2 || (f.hits > 1 && f.dmg >= d.hp)))
				wcscpy_s(tags[nt++], TT(TX_BT_OVERKILL));
		}
		wchar_t lines[2][160];
		lines[0][0] = lines[1][0] = 0;
		int nLine = 0;
		float tagPx = U(13);
		float maxW = U(340);
		for (int i = 0; i < nt; i++) {
			wchar_t trial[160];
			if (nLine == 0 && !lines[0][0]) wcscpy_s(trial, tags[i]);
			else swprintf_s(trial, L"%s  %s", lines[nLine], tags[i]);
			if (lines[nLine][0] && m_r.TextWidth(trial, tagPx, 0) > maxW && nLine < 1) {
				nLine = 1;
				wcscpy_s(lines[1], tags[i]);
			} else wcscpy_s(lines[nLine], trial);
		}
		if (lines[0][0]) nLine++;
		if (inRange && (showCount || nLine > 0)) {
			aimHold = 1;
			wcscpy_s(aimCount, count);
			aimTagN = 0;
			if (nLine > 0 && lines[0][0]) { wcscpy_s(aimTag[0], lines[0]); aimTagN = 1; }
			if (nLine > 1 && lines[1][0]) { wcscpy_s(aimTag[1], lines[1]); aimTagN = 2; }
			V3 tagP = v3(m_cx + .5f, m_b.map.WalkY(m_cx, m_cz, m_cLayer) + 0.15f, m_cz + .5f);
			if (!m_r.ProjectToScreen(tagP, aimSx, aimSy)) { aimSx = m_w * .5f; aimSy = m_h * .45f; }
		}
	}
	if (m_b.nUnits > 0) DrawTurnOrder();
	else m_orderBottom = U(14);
	int nMap = m_r.MinimapLayers();
	if (nMap < 1) nMap = 1;
	if (nMap > 3) nMap = 3;
	float mapGap = max(3.f, U(8));
	float mapTop = m_orderBottom + max(6.f, U(8));
	float mapBot = (float)m_h - m_hud.margin;
	float mapRoom = mapBot - mapTop;
	float ms = min(m_hud.orderW, U(nMap >= 3 ? 112.f : (nMap == 2 ? 150.f : 220.f)));
	if (nMap * ms + (nMap - 1) * mapGap > mapRoom)
		ms = (mapRoom - (nMap - 1) * mapGap) / (float)nMap;
	if (ms < 4.f) ms = 4.f;
	if (ms > m_hud.orderW) ms = m_hud.orderW;
	float miniStep = ms + mapGap;
	float stack = nMap * ms + (nMap - 1) * mapGap;
	float miniTop = mapBot - stack;
	if (miniTop < mapTop) miniTop = mapTop;
	float miniLeft = m_hud.orderX + max(0.f, (m_hud.orderW - ms) * .5f);
	m_miniHitN = nMap;
	for (int L = 0; L < nMap; L++) {
		float my = miniTop + (nMap - 1 - L) * miniStep;
		m_miniX[L] = miniLeft - U(6);
		m_miniY[L] = my - U(6);
		m_miniS[L] = ms + U(12);
		DrawMinimap(miniLeft, my, ms, L);
	}
	float gap = max(6.f, U(12));
	float panelX = m_hud.margin;
	float panelTop = m_hud.panelTop;
	float room = m_hud.col - gap - panelX;
	if (room < 48.f) room = max(32.f, m_hud.col - panelX - 4.f);
	float panelW = min(U(460.f), max(min(U(220.f), room), room * .46f));
	if (panelW > room - gap) panelW = max(40.f, room - gap);
	if (panelW > room) panelW = room;
	float logX = panelX + panelW + gap;
	float logW = m_hud.col - gap - logX;
	if (logW < 48.f) {
		panelW = max(40.f, room * 0.55f);
		logX = panelX + panelW + gap;
		logW = max(32.f, m_hud.col - gap - logX);
	}
	float logTop = m_hud.logTop;
	float logH = m_hud.logH;
	float cardX = 0, cardY = 0, cardW = 0, cardH = 0;
	float hovX = 0, hovY = 0, hovW = 0, hovH = 0;
	if (m_cur >= 0 && m_phase != PH_OVER && m_phase != PH_INTRO && (m_b.u[m_cur].alive || m_b.u[m_cur].team == TEAM_PLAYER)) DrawUnitPanel(m_b.u[m_cur], panelX, panelTop, panelW, TRUE, &cardX, &cardY, &cardW, &cardH);
	float hoverTop = -1.f;
	if (m_hoverUnit >= 0 && m_hoverUnit != m_cur && m_phase != PH_ACTION && (m_b.u[m_hoverUnit].alive || m_b.u[m_hoverUnit].team == TEAM_PLAYER)) {
		float hw = min(U(360.f), max(U(220.f), logW));
		float roomH = logTop - gap - (m_hud.stageB + 4.f);
		if (roomH < U(80)) roomH = U(80);
		float hh = FitCardH(m_r, &m_b.u[m_hoverUnit], hw, FALSE, U(78), U(30), U(132), U(12), roomH, NULL);
		float hy = logTop - gap - hh;
		float hx = logX;
		BOOL besideParty = FALSE;
		if (hy < m_hud.partyB && hx < m_hud.partyR) {
			float alt = m_hud.partyR + gap;
			if (alt + hw <= m_hud.col - 4.f) { hx = alt; besideParty = TRUE; }
		}
		if (!besideParty && hy < m_hud.partyB) {
			float top = m_hud.partyB + 4.f;
			float bot = logTop - gap;
			if (bot - top >= U(88)) hy = top;
		}
		if (hy < m_hud.stageB + 4.f && hx < m_hud.stageR) hy = m_hud.stageB + 4.f;
		if (hy + U(80) > logTop - gap) hy = logTop - gap - U(80);
		if (hy < 4.f) hy = 4.f;
		hoverTop = hy;
		DrawUnitPanel(m_b.u[m_hoverUnit], hx, hy, hw, FALSE, &hovX, &hovY, &hovW, &hovH);
	}
	DrawBattleLog(logX, logTop, logW, logH);
	if (m_seqCut && m_cutStage == 4) DrawCutInUi();
	// 地形情報
	if (m_cursorValid && (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET)) {
		const Cell& c = m_b.map.At(m_cx, m_cz);
		const TerrainDef& td = TerrainInfo(c.t);
		wchar_t l[128];
		const wchar_t* gm = L"";
		if (c.gimm == GM_HEAL) gm = TT(TX_GM_HEAL);
		else if (c.gimm == GM_MIGHT) gm = TT(TX_GM_MIGHT);
		else if (c.gimm == GM_WARP) gm = TT(TX_GM_WARP);
		else if (c.gimm == GM_CHEST) gm = TT(TX_GM_CHEST);
		else if (c.gimm == GM_LEVER) gm = TT(TX_GM_LEVER);
		else if (c.gimm == GM_FENCE) gm = TT(TX_GM_FENCE);
		else if (c.gimm == GM_DROP) gm = TT(TX_GM_DROP);
		if (c.gimm == GM_WARP)
			swprintf_s(l, L"%s  %s  %s %d", TT(TX_BT_TERRAIN), TT(td.nameTx), gm, (int)c.gparam + 1);
		else if (c.gimm)
			swprintf_s(l, L"%s  %s  %s", TT(TX_BT_TERRAIN), TT(td.nameTx), gm);
		else if (m_cur >= 0 && CanTerrace(m_b, m_cur, m_cx, m_cz))
			swprintf_s(l, L"%s  %s  H%d   %s", TT(TX_BT_TERRAIN), TT(td.nameTx), (int)c.h, TT(TX_BT_CLIMB));
		else if (c.t == T_WATER && c.gimm == GM_NONE && c.gparam > 0 && c.gparam <= 4)
			swprintf_s(l, L"%s  %s  %s %d  H%d", TT(TX_BT_TERRAIN), TT(td.nameTx), TT(TX_BT_DEPTH), (int)c.gparam, c.h);
		else if (c.prop == PK_MOUNT && (c.gparam & 15) >= 1 && (c.gparam & 15) <= 3)
			swprintf_s(l, L"%s  %s  %dx%d  H%d", TT(TX_BT_TERRAIN), TT(td.nameTx), (int)(c.gparam & 15), (int)((c.gparam >> 4) & 15), c.h);
		else if (c.t == T_LAVA)
			swprintf_s(l, L"%s  %s  %s%d  HP-12%%  H%d", TT(TX_BT_TERRAIN), TT(td.nameTx), TT(StatNameTx(S_MOV)), td.costFoot, c.h);
		else if (td.costFoot < 99)
			swprintf_s(l, L"%s  %s  %s+%d%%  AVO+%d%%  H%d  %s%d", TT(TX_BT_TERRAIN), TT(td.nameTx), TT(TX_ST_DEF), td.defPct, td.avoPct, c.h, TT(StatNameTx(S_MOV)), td.costFoot);
		else
			swprintf_s(l, L"%s  %s  %s+%d%%  AVO+%d%%  H%d", TT(TX_BT_TERRAIN), TT(td.nameTx), TT(TX_ST_DEF), td.defPct, td.avoPct, c.h);
		float tw = m_r.TextWidth(l, U(13), 0) + U(24);
		float ty = logTop - U(34);
		if (hoverTop >= 0.f) ty = hoverTop - U(34);
		if (ty < 4.f) ty = 4.f;
		if (hoverTop >= 0.f && ty + U(32) > hoverTop - 2.f) ty = -1.f;
		float tx = m_hud.col - gap - tw;
		if (tx < panelX + panelW + 4.f && ty + U(32) > panelTop) tx = panelX + panelW + 4.f;
		if (tx < panelX) { tx = panelX; tw = max(40.f, m_hud.col - gap - tx); }
		if (ty >= 0.f && tx + 40.f < m_hud.col) {
			if (tx + tw > m_hud.col - 4.f) tw = m_hud.col - 4.f - tx;
			m_r.HudPanel(tx, ty, tw, U(30), RGBA8(16, 20, 36, 210), RGBA8(150, 128, 86));
			m_r.HudText(l, tx + U(12), ty + U(15), FitTextPx(l, tw - U(20), U(13), 0), RGBA8(236, 236, 240), 0, 0);
		}
	}
	if (partyLook >= 0 && !m_statOpen) {
		float pw = min(U(640.f), (float)m_w - U(40));
		float ph = min(U(520.f), (float)m_h - U(40));
		float px = ((float)m_w - pw) * .5f, py = ((float)m_h - ph) * .5f;
		if (m_click && m_clickX >= px && m_clickY >= py && m_clickX < px + pw && m_clickY < py + ph) m_click = FALSE;
	}
	float fx = 0, fy = 0, fw = 0, fh = 0;
	if (m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET) DrawForecast(&fx, &fy, &fw, &fh);
	if (m_phase == PH_P_CMD || m_phase == PH_P_SKILLS || m_phase == PH_P_ITEM || m_phase == PH_P_SUMMON) DrawCommandMenu();
	if (m_phase == PH_P_SKILLTARGET && m_cur >= 0 && IsFormSkill(m_cmdSkill)) {
		int tx = m_b.u[m_cur].x, tz = m_b.u[m_cur].z;
		if (m_cursorValid) { tx = m_cx; tz = m_cz; }
		DrawFormGuide(m_cmdSkill, tx, tz, 0, 0, 0, 0);
	}
	if (m_statOpen && m_phase != PH_ACTION && m_phase != PH_MOVING && m_phase != PH_E_MOVE && m_phase != PH_INTRO && m_phase != PH_OVER && m_phase != PH_LEVELUP) DrawBattleStatus(TRUE);
	if (m_phase == PH_LEVELUP) DrawLevelUp();
	// ヒント。枠に収まる行数だけ描き、単語の途中では切らない。
	float hintX = 0, hintY = 0, hintW = 0, hintH = 0;
	{
		int tx = -1;
		switch (m_phase) {
		case PH_P_MOVE: tx = TX_BT_HINT_MOVE; break;
		case PH_P_CMD: case PH_P_SKILLS: case PH_P_SUMMON: tx = TX_BT_HINT_CMD; break;
		case PH_P_TARGET: tx = TX_BT_HINT_TARGET; break;
		case PH_P_SHOVE: tx = TX_BT_SHOVE; break;
		case PH_P_SKILLTARGET: tx = TX_BT_HINT_SKILL; break;
		case PH_E_SHOW: case PH_E_MOVE: tx = TX_BT_HINT_ENEMY; break;
		case PH_ACTION: if (m_cur >= 0 && m_b.u[m_cur].team == TEAM_ENEMY) tx = TX_BT_HINT_ENEMY; break;
		default: break;
		}
		if (tx >= 0) {
			const wchar_t* hs = TT(tx);
			BOOL keys = (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE);
			float leftBound = partyRight + U(16);
			float rightBound = m_hud.col - U(8);
			float y = m_hud.stageB + U(8);
			if (fh > 1.f) {
				float leftRoom = fx - U(8) - leftBound;
				float rightRoom = rightBound - (fx + fw + U(8));
				if (rightRoom >= leftRoom && rightRoom > U(180)) leftBound = fx + fw + U(8);
				else if (leftRoom > U(180)) rightBound = fx - U(8);
				else y = max(y, fy + fh + U(8));
			}
			float room = max(U(160.f), rightBound - leftBound);
			float want = m_r.TextWidth(hs, U(16), 1) + U(36);
			if (keys) want = max(want, min(U(720.f), room));
			float tw = min(want, room);
			float tx0 = leftBound + max(0.f, (room - tw) * .5f);
			float unitTop = m_hud.panelTop;
			if (room < U(280) && partyBottom + U(80) < unitTop) {
				tw = min(want, max(U(240.f), (float)m_w - partyRight - U(36)));
				tx0 = partyRight + U(16);
				y = max(y, partyBottom + U(8));
			}
			float inner = tw - U(24);
			if (inner < 40.f) inner = 40.f;
			float limitB = unitTop - U(6);
			if (m_hud.logTop > 8.f && m_hud.logTop - U(6) < limitB) limitB = m_hud.logTop - U(6);
			float pxT = U(15);
			float pxK = U(12);
			wchar_t titleL[6][180], keyL[8][180];
			int nT = 0, nK = 0;
			float textH = 0, keysH = 0, th = 0, top = 0;
			for (int attempt = 0; attempt < 4; attempt++) {
				nT = WrapSpaceLines(m_r, hs, inner, pxT, titleL, 6);
				nK = keys ? WrapSpaceLines(m_r, TT(TX_BT_HINT_KEYS), inner, pxK, keyL, 8) : 0;
				if (nT < 1) nT = 1;
				float tLine = pxT * 1.7f + 4.f;
				float kLine = pxK * 1.7f + 4.f;
				textH = nT * tLine;
				keysH = nK > 0 ? nK * kLine + U(4) : 0;
				top = y;
				th = textH + keysH + U(12);
				if (top + th <= limitB || (pxT <= U(11) && pxK <= U(9))) break;
				pxT *= 0.88f;
				pxK *= 0.88f;
			}
			if (top + th > limitB) th = max(U(28), limitB - top);
			m_r.HudPanel(tx0, top, tw, th, RGBA8(16, 20, 36, 200), RGBA8(150, 128, 86));
			hintX = tx0; hintY = top; hintW = tw; hintH = th;
			float tLine = pxT * 1.7f + 4.f;
			float kLine = pxK * 1.7f + 4.f;
			float ly = top + U(4);
			for (int i = 0; i < nT; i++) {
				if (ly + tLine > top + th - U(2)) break;
				const wchar_t* ln = titleL[i][0] ? titleL[i] : hs;
				m_r.HudText(ln, tx0 + U(12), ly + tLine * .5f, FitTextPx(ln, inner, pxT, 1), RGBA8(255, 255, 255), 0, 1);
				ly += tLine;
			}
			if (nK > 0) ly += U(2);
			for (int i = 0; i < nK; i++) {
				if (ly + kLine > top + th - U(2)) break;
				m_r.HudText(keyL[i], tx0 + U(12), ly + kLine * .5f, FitTextPx(keyL[i], inner, pxK, 0), RGBA8(210, 210, 220), 0, 0);
				ly += kLine;
			}
		}
	}
	if (aimHold && (aimCount[0] || aimTagN > 0)) {
		float pxC = U(14), pxG = U(12);
		float lineC = pxC * 1.7f + 4.f;
		float lineG = pxG * 1.7f + 4.f;
		float chipW = aimCount[0] ? m_r.TextWidth(aimCount, pxC, 1) + U(28) : U(40);
		for (int i = 0; i < aimTagN; i++) {
			float lw = m_r.TextWidth(aimTag[i], pxG, 0) + U(28);
			if (lw > chipW) chipW = lw;
		}
		if (chipW > U(360)) chipW = U(360);
		float chipH = U(8) + (aimCount[0] ? lineC : 0) + aimTagN * lineG;
		struct Box { float x, y, w, h; };
		Box blk[64];
		int nb = 0;
		auto addB = [&](float x, float y, float w, float h) {
			if (w > 2.f && h > 2.f && nb < 64) blk[nb++] = { x, y, w, h };
		};
		addB(m_hud.orderX, m_hud.margin, m_hud.orderW, max(0.f, m_orderBottom - m_hud.margin));
		addB(0, 0, m_hud.stageR, m_hud.stageB);
		addB(m_hud.margin, m_hud.partyT, max(0.f, m_hud.partyR - m_hud.margin), max(0.f, m_hud.partyB - m_hud.partyT));
		addB(panelX, panelTop, panelW, max(0.f, (float)m_h - panelTop));
		addB(cardX - U(6), cardY - U(6), cardW + U(12), cardH + U(12));
		addB(hovX - U(6), hovY - U(6), hovW + U(12), hovH + U(12));
		addB(logX, logTop, logW, logH);
		addB(fx, fy, fw, fh);
		addB(hintX, hintY, hintW, hintH);
		addB(miniLeft, miniTop, ms, (float)nMap * miniStep);
		addB(aimSx - U(22), aimSy - U(22), U(44), U(44));
		for (int i = 0; i < barBn; i++) addB(barBx[i], barBy[i], barBw[i], barBh[i]);
		auto hitArea = [&](float x, float y, float w, float h) -> float {
			float a = 0.f;
			if (x < 2.f) a += (2.f - x) * h;
			if (y < 2.f) a += (2.f - y) * w;
			if (x + w > (float)m_w - 2.f) a += (x + w - ((float)m_w - 2.f)) * h;
			if (y + h > (float)m_h - 2.f) a += (y + h - ((float)m_h - 2.f)) * w;
			for (int i = 0; i < nb; i++) {
				float x0 = x > blk[i].x ? x : blk[i].x;
				float y0 = y > blk[i].y ? y : blk[i].y;
				float x1 = x + w < blk[i].x + blk[i].w ? x + w : blk[i].x + blk[i].w;
				float y1 = y + h < blk[i].y + blk[i].h ? y + h : blk[i].y + blk[i].h;
				if (x1 > x0 && y1 > y0) a += (x1 - x0) * (y1 - y0);
			}
			return a;
		};
		float gap = U(14);
		auto fits = [&](float x, float y) -> BOOL {
			if (x < 2.f || y < 2.f || x + chipW > (float)m_w - 2.f || y + chipH > (float)m_h - 2.f) return FALSE;
			return hitArea(x, y, chipW, chipH) <= 0.5f;
		};
		BOOL found = FALSE;
		float px = 2.f, py = 2.f;
		float bestD = 1e12f;
		auto consider = [&](float x, float y) {
			if (!fits(x, y)) return;
			float dx = (x + chipW * .5f) - aimSx;
			float dy = (y + chipH * .5f) - aimSy;
			float d = dx * dx + dy * dy;
			if (d < bestD) { bestD = d; px = x; py = y; found = TRUE; }
		};
		for (int ring = 1; ring <= 18; ring++) {
			float dist = gap + U(18.f) * (float)ring;
			for (int k = 0; k < 8; k++) {
				float ang = (float)k * 0.78539816f;
				float cx = aimSx + cosf(ang) * dist - chipW * .5f;
				float cy = aimSy + sinf(ang) * dist - chipH * .5f;
				consider(cx, cy);
			}
			if (found) break;
		}
		if (!found) {
			float step = max(10.f, U(14));
			for (float y = 2.f; y + chipH < (float)m_h - 2.f; y += step)
				for (float x = 2.f; x + chipW < (float)m_w - 2.f; x += step)
					consider(x, y);
		}
		if (!found) { px = 2.f; py = 2.f; }
		m_r.HudPanel(px, py, chipW, chipH, RGBA8(16, 18, 28, 220), RGBA8(255, 200, 90));
		float ly = py + U(3);
		if (aimCount[0]) {
			int nShown = 0;
			const wchar_t* p = aimCount;
			while (*p && (*p < L'0' || *p > L'9')) p++;
			nShown = _wtoi(p);
			m_r.HudText(aimCount, px + chipW * .5f, ly + lineC * .5f, FitTextPx(aimCount, chipW - U(12), pxC, 1), nShown > 0 ? RGBA8(255, 230, 120) : RGBA8(255, 150, 130), 1, 1);
			ly += lineC;
		}
		for (int i = 0; i < aimTagN; i++) {
			m_r.HudText(aimTag[i], px + chipW * .5f, ly + lineG * .5f, FitTextPx(aimTag[i], chipW - U(12), pxG, 0), RGBA8(255, 230, 170), 1, 0);
			ly += lineG;
		}
	}
	if (m_phase == PH_INTRO && m_surveyN >= 2 && m_phaseT < kIntroSweep * TacPaceTime(m_save)) {
		wchar_t hs[160];
		swprintf_s(hs, TT(TX_BT_SURVEY), m_b.AliveCount(TEAM_ENEMY));
		float tw = m_r.TextWidth(hs, U(16), 1) + U(48);
		float tx0 = ((float)m_w - tw) * .5f, top = (float)m_h - U(96);
		m_r.HudPanel(tx0, top, tw, U(50), RGBA8(16, 20, 36, 150), RGBA8(150, 128, 86));
		m_r.HudText(hs, tx0 + tw * .5f, top + U(17), U(16), RGBA8(255, 255, 255), 1, 1);
		Bar(tx0 + U(16), top + U(34), tw - U(32), U(6), m_phaseT / (kIntroSweep * TacPaceTime(m_save)), RGBA8(255, 200, 110), RGBA8(255, 236, 170));
	}
	if (m_phase == PH_MENU) {
		m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 120));
		float w = m_menuConfirm == 2 ? U(420) : (m_menuConfirm == 3 ? U(480) : U(320));
		float h = (m_menuConfirm == 1 || m_menuConfirm == 3) ? U(220) : (m_menuConfirm == 2 ? U(600) : U(460));
		float x = (m_w - w) * .5f, y = (m_h - h) * .5f;
		Panel(x, y, w, h);
		m_r.HudText(TT(TX_BT_MENU), x + w * .5f, y + U(26), U(20), RGBA8(255, 226, 150), 1, 1);
		float bx = x + U(30), bw = w - U(60), bh = U(40);
		if (!m_menuConfirm) {
			BOOL quiet = m_phaseBeforeMenu == PH_P_MOVE || m_phaseBeforeMenu == PH_P_CMD || m_phaseBeforeMenu == PH_P_SKILLS
				|| m_phaseBeforeMenu == PH_P_TARGET || m_phaseBeforeMenu == PH_P_SKILLTARGET || m_phaseBeforeMenu == PH_P_SHOVE
				|| m_phaseBeforeMenu == PH_E_SHOW;
			if (Btn(ID_BT_M_RESUME, bx, y + U(48), bw, bh, TT(TX_BT_RESUME))) m_phase = m_phaseBeforeMenu;
			if (Btn(ID_BT_M_SAVE, bx, y + U(94), bw, bh, TT(TX_SV_MID), quiet)) {
				QueueSuspend(1);
				AddBanner(TT(TX_SV_SAVED), NULL, RGBA8(255, 226, 150), 0, 1.6f, MSG_FACE);
				m_phase = m_phaseBeforeMenu;
			}
			if (Btn(ID_BT_M_SLOT, bx, y + U(140), bw, bh, TT(TX_SLOT))) OpenSlotPanel();
			if (Btn(ID_BT_M_OPT, bx, y + U(186), bw, bh, TT(TX_SET_OPEN))) { m_menuConfirm = 2; m_focusId = ID_BT_OPT_Q; }
			if (Btn(ID_BT_M_HELP, bx, y + U(232), bw, bh, TT(TX_BT_HELP))) m_showHelp = TRUE;
			if (Btn(ID_BT_M_ZOOM, bx, y + U(278), bw, bh, TT(TX_BT_ZOOMRESET))) ResetCamDist();
			if (Btn(ID_BT_M_GAME, bx, y + U(324), bw, bh, TT(TX_BT_TOGAME), TRUE, FALSE, 0, RGBA8(36, 64, 110, 240))) { m_menuConfirm = 3; m_focusId = ID_BT_M_NO; }
			if (Btn(ID_BT_M_RETREAT, bx, y + U(370), bw, bh, TT(TX_BT_RETREAT), TRUE, FALSE, 0, RGBA8(140, 50, 40, 240))) { m_menuConfirm = 1; m_focusId = ID_BT_M_NO; }
		} else if (m_menuConfirm == 2) {
			float oy = y + U(48);
			wchar_t qv[72];
			if (m_save.quality == TAC_Q_AUTO)
				swprintf_s(qv, L"%s  %s", TT(TX_SET_Q_AUTO), TT(TacQualityTx(QualityNow())));
			else wcscpy_s(qv, TT(TacQualityTx(m_save.quality)));
			if (int q = Sel(ID_BT_OPT_Q, bx, oy, bw, U(32), TT(TX_SET_QUALITY), qv)) {
				PickQuality(q);
			}
			oy += U(34);
			if (Sel(ID_BT_OPT_ANIM, bx, oy, bw, U(32), TT(TX_SET_ANIM), TT(m_save.battleAnim ? TX_SET_ANIM_SHORT : TX_SET_ANIM_FULL))) m_save.battleAnim = !m_save.battleAnim;
			oy += U(34);
			{
				static const int paceTx[6] = { TX_PACE0, TX_PACE1, TX_PACE2, TX_PACE3, TX_PACE4, TX_PACE5 };
				if (int d = Sel(ID_BT_OPT_PACE, bx, oy, bw, U(32), TT(TX_SET_PACE), TT(paceTx[TacPace(m_save)]))) TacSetPace(m_save, TacPace(m_save) + d);
			}
			oy += U(34);
			{
				static const int paceTx[6] = { TX_PACE0, TX_PACE1, TX_PACE2, TX_PACE3, TX_PACE4, TX_PACE5 };
				if (int d = Sel(ID_BT_OPT_CUT, bx, oy, bw, U(32), TT(TX_SET_CUTSPD), TT(paceTx[TacCutPace(m_save)]), m_save.battleAnim ? 0 : 1)) TacSetCutPace(m_save, TacCutPace(m_save) + d);
			}
			oy += U(34);
			if (Sel(ID_BT_OPT_GRID, bx, oy, bw, U(32), TT(TX_SET_GRID), TT(TacGridOn(m_save) ? TX_CR_ON : TX_CR_OFF))) m_save.showGrid ^= 1;
			oy += U(34);
			if (Sel(ID_BT_OPT_THREAT, bx, oy, bw, U(32), TT(TX_SET_THREAT), TT(TacThreatOn(m_save) ? TX_CR_ON : TX_CR_OFF))) {
				m_save.showGrid ^= 2;
				ComputeOverlays();
			}
			oy += U(34);
			if (int d = Sel(ID_BT_OPT_AI, bx, oy, bw, U(32), TT(TX_SET_AI), TT(TacAiTx(TacAi(m_save))))) TacSetAi(m_save, TacAi(m_save) + d);
			oy += U(34);
			{
				BOOL autoQ = m_save.quality == TAC_Q_AUTO;
				wchar_t vs[32];
				if (autoQ) swprintf_s(vs, L"%s  1/%d", TT(TX_SET_Q_AUTO), PaceHz());
				else swprintf_s(vs, L"1/%d", TacVsyncHz(m_save));
				if (int d = Sel(ID_BT_OPT_SYNC, bx, oy, bw, U(32), TT(TX_SET_SYNC), vs, autoQ ? 0 : 1)) {
					TacSetVsync(m_save, TacVsync(m_save) + d);
					m_syncRun = -1;
					m_syncArmN = 0;
				}
			}
			oy += U(34);
			if (Sel(ID_BT_OPT_WIN, bx, oy, bw, U(32), TT(TX_SET_WIN), TT(TacWinTx(TacWinMode(m_save))))) TacSetWinMode(m_save, TacWinMode(m_save) ^ 1);
			oy += U(34);
			if (Sel(ID_BT_OPT_FPS, bx, oy, bw, U(32), TT(TX_SET_FPS), TT(TacFpsOn(m_save) ? TX_CR_ON : TX_CR_OFF))) TacSetFps(m_save, !TacFpsOn(m_save));
			oy += U(36);
			oy = DrawSoundOpts(bx, oy, bw, 1.f);
			if (Btn(ID_BT_M_RESUME, bx, oy + U(6), bw, bh, TT(TX_CR_BACK))) m_menuConfirm = 0;
		} else if (m_menuConfirm == 3) {
			m_r.HudTextBox(TT(TX_BT_TOGAME_Q), bx, y + U(50), bw, U(72), U(14), RGBA8(220, 230, 245));
			if (Btn(ID_BT_M_YES, bx, y + U(140), bw * .47f, bh, TT(TX_PT_YES), TRUE, FALSE, 0, RGBA8(36, 64, 110, 240))) {
				m_goHome = TRUE;
				return;
			}
			if (Btn(ID_BT_M_NO, bx + bw * .53f, y + U(140), bw * .47f, bh, TT(TX_PT_NO))) m_menuConfirm = 0;
		} else {
			m_r.HudTextBox(TT(TX_BT_RETREAT_Q), bx, y + U(50), bw, U(60), U(14), RGBA8(255, 220, 210));
			if (Btn(ID_BT_M_YES, bx, y + U(140), bw * .47f, bh, TT(TX_PT_YES), TRUE, FALSE, 0, RGBA8(140, 50, 40, 240))) { TacSfx(TS_LOSE); EndBattle(0); return; }
			if (Btn(ID_BT_M_NO, bx + bw * .53f, y + U(140), bw * .47f, bh, TT(TX_PT_NO))) m_menuConfirm = 0;
		}
	}
	if (stampHov) {
		float tw = min(U(520.f), max(U(300.f), (float)m_w * .46f));
		float tx = stampX;
		if (tx + tw > (float)m_w - U(16)) tx = max(U(12.f), (float)m_w - U(16) - tw);
		float ty = U(78), thb = U(168);
		m_r.HudPanel(tx, ty, tw, thb, RGBA8(18, 14, 10, 236), RGBA8(255, 160, 70));
		m_r.HudTextBox(TT(TX_BT_STAMPEDE_HELP), tx + U(14), ty + U(12), tw - U(28), thb - U(22), U(14), RGBA8(255, 236, 214));
	}
	if (varHov) {
		int help = TX_BT_SWEEP_HELP;
		DWORD edge = RGBA8(140, 210, 160), ink = RGBA8(220, 245, 226);
		if (m_b.sweep == TAC_VAR_REMNANT) { help = TX_BT_REMNANT_HELP; edge = RGBA8(190, 160, 80); ink = RGBA8(245, 230, 190); }
		else if (m_b.sweep == TAC_VAR_BOSS) { help = TX_BT_BOSSMAP_HELP; edge = RGBA8(210, 120, 50); ink = RGBA8(255, 220, 180); }
		else if (m_b.sweep == TAC_VAR_SCAR) { help = TX_BT_SCAR_HELP; edge = RGBA8(180, 80, 80); ink = RGBA8(255, 210, 200); }
		else if (m_b.sweep == TAC_VAR_MIST) { help = TX_BT_MIST_HELP; edge = RGBA8(140, 170, 190); ink = RGBA8(220, 235, 245); }
		else if (m_b.sweep == TAC_VAR_RAID) { help = TX_BT_RAID_HELP; edge = RGBA8(110, 120, 190); ink = RGBA8(210, 215, 245); }
		else if (m_b.sweep == TAC_VAR_AMBUSH) { help = TX_BT_AMBUSH_HELP; edge = RGBA8(190, 120, 50); ink = RGBA8(255, 220, 180); }
		else if (m_b.sweep == TAC_VAR_TWIN) { help = TX_BT_TWIN_HELP; edge = RGBA8(180, 70, 110); ink = RGBA8(255, 210, 220); }
		else if (m_b.sweep == TAC_VAR_VAULT) { help = TX_BT_VAULT_HELP; edge = RGBA8(180, 150, 50); ink = RGBA8(255, 235, 190); }
		else if (m_b.sweep == TAC_VAR_BLESS) { help = TX_BT_BLESS_HELP; edge = RGBA8(80, 170, 140); ink = RGBA8(210, 245, 225); }
		float tw = min(U(520.f), max(U(300.f), (float)m_w * .46f));
		float tx = varX;
		if (tx + tw > (float)m_w - U(16)) tx = max(U(12.f), (float)m_w - U(16) - tw);
		float ty = U(78), thb = U(150);
		m_r.HudPanel(tx, ty, tw, thb, RGBA8(12, 16, 18, 236), edge);
		m_r.HudTextBox(TT(help), tx + U(14), ty + U(12), tw - U(28), thb - U(22), U(14), ink);
	}
	if (m_seqCut) DrawCutInUi();
	if (m_fastFwd && (m_phase == PH_E_SHOW || m_phase == PH_E_MOVE || m_phase == PH_ACTION)) m_r.HudText(L"▶▶", (float)m_w - U(210), U(30), U(20), RGBA8(255, 226, 150), 1, 1);
	if (partyLook >= 0 && !m_statOpen) {
		int prevWho = m_statWho;
		m_statWho = partyLook;
		DrawBattleStatus(FALSE);
		m_statWho = prevWho;
	}
}

} // namespace otac
