#include "stdafx.h"
#include "OmakeTacticsGame.h"
#include "OmakeTacticsAudio.h"
#include "DatArchive.h"
#include <math.h>
#include <stdio.h>

namespace otac {

static const float PI = 3.14159265f;

enum {
	ID_BT_ATTACK = 500, ID_BT_SKILL, ID_BT_WAIT, ID_BT_UNDO, ID_BT_BACK, ID_BT_LVOK, ID_BT_SHOVE, ID_BT_DOOR, ID_BT_ITEM, ID_BT_CLIMB,
	ID_BT_SKPREV = 510, ID_BT_SKNEXT,
	ID_BT_SKTAB = 480,
	ID_BT_FORM0 = 430,
	ID_BT_STAT = 515, ID_BT_LOOK,
	ID_BT_SK0 = 520,
	ID_BT_CRAFT0 = 800, ID_BT_CRAFT1, ID_BT_CRAFT2, ID_BT_CRAFT3,
	ID_BT_M_RESUME = 900, ID_BT_M_HELP, ID_BT_M_RETREAT, ID_BT_M_YES, ID_BT_M_NO, ID_BT_MENU, ID_BT_M_SAVE = 906, ID_BT_M_OPT, ID_BT_M_ZOOM, ID_BT_M_SLOT = 910,
	ID_BT_OPT_Q = 920, ID_BT_OPT_ANIM, ID_BT_OPT_GRID,
	ID_BT_IT0 = 960, ID_BT_ITT0 = 980,
	ID_BT_PROMO0 = 740, ID_BT_PROMO_LATER = 748,
	ID_BT_EQ0 = 750, ID_BT_EQNONE = 754, ID_BT_EQROW = 755, ID_BT_PAS0 = 762, ID_BT_PAS1, ID_BT_EQPREV = 764, ID_BT_EQNEXT = 765, ID_BT_BEAD2 = 766, ID_BT_BEAD3
};

static float AngLerp(float a, float b, float t)
{
	float d = WrapAngle(b - a);
	return a + d * t;
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
		y = max(y, m_b.map.StandY(x, z));
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
	}
	InvalidatePortraits();
}

void Game::ActorForUnit(int i)
{
	Unit& u = m_b.u[i];
	if (u.team == TEAM_PLAYER) ActorFromChar(m_act[i], m_lib, m_r, u.ch);
	else ActorFromMonster(m_act[i], m_lib, m_r, u.monster, u.boss);
	if (UnitMoveType(u) == MV_FLY && m_act[i].hover < .2f) m_act[i].hover = .35f;
	ActorPlay(m_act[i], CL_IDLE, 1, 0);
	m_act[i].t = m_rng.F(0, 2);
}

static const DWORD kSnapMagic = 0x31504153u;

struct TacSnap {
	DWORD magic;
	int version;
	int kind;
	int stage;
	DWORD rng;
	int cur, turnOpen, phase, moved, x0, z0, cx, cz;
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
	delete job;
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
	DWORD magic = 0; int ver = 0, kind = 0, stage = 0;
	size_t n = fread(&magic, 1, sizeof(magic), fp);
	n += fread(&ver, 1, sizeof(ver), fp);
	n += fread(&kind, 1, sizeof(kind), fp);
	n += fread(&stage, 1, sizeof(stage), fp);
	fclose(fp);
	if (n < sizeof(magic) + sizeof(ver) * 3 || magic != kSnapMagic || ver != 1) return 0;
	return stage > 0 ? stage : 0;
}

void Game::ProbeSuspend()
{
	WaitSuspend();
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
	if (m_screen != SC_BATTLE || m_b.nUnits < 1) return;
	TacSnap* s = new TacSnap();
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
		&& s->phase != PH_P_SKILLTARGET && s->phase != PH_P_SHOVE && s->phase != PH_E_SHOW)
		s->phase = PH_P_MOVE;
	s->moved = m_moved;
	s->x0 = m_curX0; s->z0 = m_curZ0; s->face0 = m_curFace0;
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
	if (kind == 1) { delete g_qMid; g_qMid = s; m_midStage = m_battleStage; }
	else { delete g_qAuto; g_qAuto = s; m_autoStage = m_battleStage; }
	SnapUnlock();
	SnapKick();
}

void Game::ResumeBattle(int kind)
{
	WaitSuspend();
	const TCHAR* leaf = kind == 1 ? TacLeafMid() : TacLeafAuto();
	TacSnap* s = new TacSnap();
	memset(s, 0, sizeof(*s));
	CString path = DatArc_Path(leaf);
	FILE* fp = NULL;
	int ok = 0;
	if (_tfopen_s(&fp, path, _T("rb")) == 0 && fp) {
		ok = fread(s, 1, sizeof(*s), fp) == sizeof(*s) && s->magic == kSnapMagic && s->version == 1 && s->stage == m_save.stageSel;
		fclose(fp);
	}
	if (!ok) { delete s; StartBattle(m_save.stageSel); return; }
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
	m_ordSnapN = 0;
	m_ordLeave = -1;
	m_importBind = 0;
	LoadBattleCast();
	m_importBind = 1;
	m_r.BuildTerrain(m_b.map);
	m_r.BuildMinimap(m_b.map);
	m_showBuilt = FALSE;
	for (int i = 0; i < m_b.nUnits; i++) {
		Unit& u = m_b.u[i];
		if (u.team == TEAM_PLAYER) CharGrantReached(u.ch);
		ActorForUnit(i);
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
	m_curX0 = s->x0; m_curZ0 = s->z0; m_curFace0 = s->face0;
	m_cx = s->cx; m_cz = s->cz; m_cursorValid = TRUE;
	m_camYaw = m_camYawGoal = s->camYaw;
	m_camPitch = s->camPitch;
	m_zoomHold = FALSE;
	m_wheelSign = 0;
	m_camDist = m_camDistGoal = m_zoomDist = s->camDist > 1.f ? s->camDist : 18.f;
	m_camTarget = m_camGoal = v3(s->camX, s->camY, s->camZ);
	m_camPan = v3(0, 0, 0);
	m_surveyN = 0;
	m_parts.clear(); m_popups.clear(); m_banners.clear(); m_deferPopups.clear();
	m_seqCut = FALSE; m_cutStage = 0; m_victory = 0; m_hoverUnit = -1;
	m_lvqN = m_lvqCur = 0; m_promoAsk = 0; m_menuConfirm = 0; m_proj.active = 0;
	m_screen = SC_BATTLE;
	m_phase = (Phase)s->phase;
	m_phaseT = 0;
	m_phaseBeforeMenu = m_phase;
	memset(m_ovMove, 0, sizeof(m_ovMove));
	memset(m_ovAtk, 0, sizeof(m_ovAtk));
	if (m_cur >= 0 && m_cur < m_b.nUnits && m_b.u[m_cur].alive && m_b.u[m_cur].team == TEAM_PLAYER)
		ComputeReach(m_b, m_cur, m_reach);
	ComputeOverlays();
	UpdateHover();
	int loadedKind = kind;
	delete s;
	DropSuspend(loadedKind);
	wchar_t t[64];
	swprintf_s(t, L"%s %d", TT(TX_BT_STAGE), m_battleStage);
	AddBanner(t, TT(loadedKind == 1 ? TX_SV_FROMMID : TX_SV_FROMAUTO), RGBA8(255, 226, 150), 0, 2.f);
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
	int n = m_b.PredictOrder(order, 8, acting, 1000, NULL);
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
	stage = Clampi(stage, 1, TAC_STAGE_MAX);
	RollMapSeed(stage, FALSE);
	DWORD seed = m_mapSeed;
	SetupBattle(m_b, stage, seed, chars, idx, n, m_themeSel);
	m_importBind = 0;
	LoadBattleCast();
	m_importBind = 1;
	m_battleStage = stage;
	m_logN = 0;
	m_r.BuildTerrain(m_b.map);
	m_r.BuildMinimap(m_b.map);
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
	m_camDist = 34.f;
	m_camDistGoal = 26.f;
	m_zoomDist = 34.f;
	m_camGoal = v3(px + .5f, m_b.map.StandY((int)px, (int)pz), pz + .5f);
	// 開戦前に戦場を区画ごとに見て回る。敵の側から蛇行し、自軍の区画で終わってから手番に入る。
	// 区画の行は偶数なので、自軍に近い列から始めると最後も自軍の列に戻る。
	{
		const int nb = MAPN / SURVEY_BLK;
		const BOOL nearX0 = px < MAPN * .5f, nearZ0 = pz < MAPN * .5f;
		m_surveyN = 0;
		for (int r = 0; r < nb; r++) {
			const int bz = nearZ0 ? nb - 1 - r : r;
			for (int c = 0; c < nb; c++) {
				const int step = (r & 1) ? nb - 1 - c : c;
				const int bx = nearX0 ? step : nb - 1 - step;
				float sx = 0, sy = 0, sz = 0;
				int n = 0;
				for (int z = bz * SURVEY_BLK; z < (bz + 1) * SURVEY_BLK; z++)
					for (int x = bx * SURVEY_BLK; x < (bx + 1) * SURVEY_BLK; x++) {
						if (m_b.map.At(x, z).t == T_VOID) continue;
						sx += x + .5f; sy += m_b.map.StandY(x, z); sz += z + .5f; n++;
					}
				if (n > 0 && m_surveyN < SURVEY_MAX) m_surveyPt[m_surveyN++] = v3(sx / n, sy / n, sz / n);
			}
		}
	}
	m_camTarget = m_surveyN > 0 ? m_surveyPt[0] : v3(MAPN * .5f, 2.f, MAPN * .5f);
	m_camPan = v3(0, 0, 0);
	m_parts.clear();
	m_popups.clear();
	m_banners.clear();
	m_deferPopups.clear();
	m_seqCut = FALSE;
	m_cutStage = 0;
	m_bossDown = 0;
	m_victory = 0;
	m_cur = -1;
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
	wchar_t t[64], sub[160];
	swprintf_s(t, L"%s %d", TT(TX_BT_STAGE), stage);
	swprintf_s(sub, L"%s  —  %s", TT(ThemeNameTx(m_b.map.theme)), TT(TX_BT_GOAL));
	AddBanner(t, sub, RGBA8(255, 226, 150), 0, 2.4f);
	TacSfx(TS_START);
}

void Game::EndBattle(int victory)
{
	for (int i = 0; i < m_b.nUnits; i++) {
		Unit& u = m_b.u[i];
		if (u.team != TEAM_PLAYER) continue;
		if (u.rosterIdx < 0 || u.rosterIdx >= MAX_ROSTER) continue;
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
		m_save.stageSel = Clampi(m_battleStage + 1, 1, min(TAC_STAGE_MAX, m_save.stageCleared + 1));
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
	int ui = m_b.NextActor();
	if (ui < 0) { m_victory = 0; m_phase = PH_OVER; m_overT = 0; TacSfx(TS_LOSE); return; }
	m_cur = ui;
	m_turnOpen = TRUE;
	Unit& u = m_b.u[ui];
	int heal = 0, poison = 0;
	TurnStartEffects(m_b, ui, &heal, &poison);
	V3 hp = v3(u.px, u.py + m_act[ui].height + .2f, u.pz);
	wchar_t s[48];
	if (heal > 0) { swprintf_s(s, L"+%d", heal); AddPopup(hp, s, RGBA8(120, 255, 140), 0); TacSfx(TS_HEAL); }
	if (poison > 0) {
		swprintf_s(s, L"-%d %s", poison, TT(TX_BT_POISON));
		AddPopup(hp, s, RGBA8(190, 120, 255), 1);
		Burst(v3(u.px, u.py + .4f, u.pz), 14, 1.2f, .14f, 1.f, .6f, .3f, .9f, 0, 1, -1.f, 0, .8f);
		TacSfx(TS_POISON);
	}
	m_dispHp[ui] = (float)u.hp;
	m_curX0 = u.x; m_curZ0 = u.z; m_curFace0 = u.face;
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
	AddBanner(b, NULL, u.team == TEAM_PLAYER ? RGBA8(140, 200, 255) : RGBA8(255, 140, 130), 2, 1.3f);
	m_lastTeam = u.team;
	WakeNearPlayers(m_b, 9);
	TacSfx(u.team == TEAM_PLAYER ? TS_TURN_P : TS_TURN_E);
	if (u.sleep > 0 || u.stun > 0) {
		AddPopup(hp, TT(u.sleep > 0 ? TX_BT_ASLEEP : TX_BT_STUN), u.sleep > 0 ? RGBA8(170, 210, 255) : RGBA8(255, 220, 90), 1);
		TacSfx(u.sleep > 0 ? TS_SLEEP : TS_STUN);
		if (u.sleep > 0) u.sleep = 0; else u.stun = 0;
		m_moved = 1;
		m_phaseT = 0;
		QueueSuspend(2);
		FinishUnitTurn(1);
		return;
	}
	if (u.confuse > 0) {
		ConfusePlan(m_b, ui, m_ai, m_rng);
		if (u.confuse > 0) u.confuse--;
		AddPopup(hp, TT(TX_BT_CONFUSE), RGBA8(255, 150, 220), 1);
		TacSfx(TS_CONFUSE);
		m_phase = PH_E_SHOW;
	} else if (u.team == TEAM_PLAYER) {
		m_phase = PH_P_MOVE;
		m_cx = u.x; m_cz = u.z;
		m_cursorValid = TRUE;
		m_navMode = FALSE;
		m_ai.wild = 0;
	} else {
		AiDecide(m_b, ui, m_ai, m_rng);
		m_phase = PH_E_SHOW;
	}
	m_phaseT = 0;
	ComputeOverlays();
	UpdateHover();
	QueueSuspend(2);
}

BOOL Game::CheckBattleOver()
{
	if (m_phase == PH_OVER) return TRUE;
	int win = -1;
	if (m_bossDown || m_b.AliveCount(TEAM_ENEMY) == 0) win = 1;
	else if (m_b.AliveCount(TEAM_PLAYER) == 0) win = 0;
	if (win < 0) return FALSE;
	m_victory = win;
	m_phase = PH_OVER;
	m_overT = 0;
	AddBanner(TT(win ? TX_BT_VICTORY : TX_BT_DEFEAT), win && m_bossDown ? TT(TX_BT_BOSS) : NULL, win ? RGBA8(255, 226, 120) : RGBA8(190, 160, 220), 1, 2.8f);
	TacSfx(win ? TS_WIN : TS_LOSE);
	if (win) {
		for (int i = 0; i < m_b.nUnits; i++) if (m_b.u[i].alive && m_b.u[i].team == TEAM_PLAYER) ActorPlay(m_act[i], CL_CHEER, 1, .2f);
		for (int i = 0; i < m_b.nUnits; i++) {
			Unit& u = m_b.u[i];
			if (!u.alive || u.team != TEAM_PLAYER) continue;
			Burst(v3(u.px, u.py + .6f, u.pz), 20, 2.f, .14f, 1.4f, 1.f, .9f, .5f, 2, 1, -1.f, 0, 1.2f);
		}
	}
	return TRUE;
}

void Game::ConfuseSelf(int ui)
{
	if (ui < 0 || ui >= m_b.nUnits) { FinishUnitTurn(1); return; }
	Unit& u = m_b.u[ui];
	int dmg = max(1, (UnitStat(u, S_STR) + UnitWeaponPow(u)) / 4);
	u.hp -= dmg;
	u.sleep = 0;
	wchar_t s[32];
	swprintf_s(s, L"-%d", dmg);
	AddPopup(v3(u.px, u.py + m_act[ui].height, u.pz), s, RGBA8(255, 160, 120), 1);
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
	if (m_cur >= 0) FinishAction(m_b, m_cur, m_moved, acted);
	m_turnOpen = FALSE;
	if (CheckBattleOver()) return;
	m_phase = PH_END;
	m_phaseT = 0;
}

// ============================================================ 範囲表示
void Game::ComputeOverlays()
{
	memset(m_ovMove, 0, sizeof(m_ovMove));
	memset(m_ovAtk, 0, sizeof(m_ovAtk));
	m_ovDirty = TRUE;
	if (m_cur < 0) return;
	const Unit& u = m_b.u[m_cur];
	if (m_phase == PH_P_MOVE || m_phase == PH_E_SHOW) {
		int rmin = UnitRangeMin(u), rmax = UnitRangeMax(u);
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			if (m_reach.cost[z][x] < 0 || !m_reach.canStop[z][x]) continue;
			m_ovMove[z][x] = 1;
		}
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			if (!m_ovMove[z][x]) continue;
			for (int dz = -rmax; dz <= rmax; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
				int d = abs(dx) + abs(dz);
				if (d < rmin || d > rmax) continue;
				int tx = x + dx, tz = z + dz;
				if (!Map::In(tx, tz) || m_ovMove[tz][tx]) continue;
				const Cell& c = m_b.map.At(tx, tz);
				if (c.t == T_CHASM) continue;
				m_ovAtk[tz][tx] = 1;
			}
		}
		if (m_phase == PH_P_MOVE && u.team == TEAM_PLAYER) {
			static const int cdx[4] = { 1, -1, 0, 0 }, cdz[4] = { 0, 0, 1, -1 };
			for (int k = 0; k < 4; k++) {
				int tx = u.x + cdx[k], tz = u.z + cdz[k];
				if (CanTerrace(m_b, m_cur, tx, tz)) m_ovAtk[tz][tx] = 5;
			}
		}
	} else if (m_phase == PH_P_TARGET || m_phase == PH_P_CMD) {
		int rmin = UnitRangeMin(u), rmax = UnitRangeMax(u);
		for (int dz = -rmax; dz <= rmax; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
			int d = abs(dx) + abs(dz);
			if (d < rmin || d > rmax) continue;
			int tx = u.x + dx, tz = u.z + dz;
			if (!Map::In(tx, tz)) continue;
			if (!InAttackRange(m_b, u.x, u.z, rmin, rmax, tx, tz, true)) continue;
			m_ovAtk[tz][tx] = m_phase == PH_P_CMD ? 2 : 1;
		}
	} else if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0) {
		const SkillDef& sd = SkillInfo(m_cmdSkill);
		int rmin = SkillCastRangeMin(u, m_cmdSkill), rmax = SkillCastRangeMax(u, m_cmdSkill);
		if (sd.rMax == 0) {
			for (int dz = -sd.area; dz <= sd.area; dz++) for (int dx = -sd.area; dx <= sd.area; dx++) {
				if (abs(dx) + abs(dz) > sd.area) continue;
				int tx = u.x + dx, tz = u.z + dz;
				if (Map::In(tx, tz)) m_ovAtk[tz][tx] = 3;
			}
		} else {
			BOOL friendly = sd.kind == SK_HEAL || sd.kind == SK_BUFF;
			for (int dz = -rmax; dz <= rmax; dz++) for (int dx = -rmax; dx <= rmax; dx++) {
				int d = abs(dx) + abs(dz);
				if (d < rmin || d > rmax) continue;
				int tx = u.x + dx, tz = u.z + dz;
				if (!Map::In(tx, tz)) continue;
				if (!friendly && !InAttackRange(m_b, u.x, u.z, rmin, rmax, tx, tz, true)) continue;
				if (IsFormSkill(m_cmdSkill) && !FormFits(m_b, m_cur, m_cmdSkill, tx, tz)) continue;
				m_ovAtk[tz][tx] = friendly ? 4 : 1;
			}
		}
	}
}

void Game::BuildOverlayQuads()
{
	m_ovQuads.clear();
	const Map& m = m_b.map;
	auto quad = [&](int x, int z, DWORD c, float lift) {
		const Cell& cc = m.At(x, z);
		float y = (cc.t == T_WATER || cc.t == T_LAVA) ? m.StandY(x, z) - .25f : m.TopY(x, z);
		if (cc.t == T_CHASM) y = -3.15f;
		y += lift;
		float in = .03f;
		V3 p[4] = { v3(x + in, y, z + in), v3(x + in, y, z + 1 - in), v3(x + 1 - in, y, z + 1 - in), v3(x + 1 - in, y, z + in) };
		float uv[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
		static const int ord[6] = { 0, 1, 2, 0, 2, 3 };
		for (int k = 0; k < 6; k++) {
			VtxS v;
			int i = ord[k];
			v.p[0] = p[i].x; v.p[1] = p[i].y; v.p[2] = p[i].z;
			v.n[0] = 0; v.n[1] = 1; v.n[2] = 0;
			v.uv[0] = uv[i][0]; v.uv[1] = uv[i][1];
			v.c = c;
			m_ovQuads.push_back(v);
		}
	};
	static const DWORD kWarp[8] = {
		RGBA8(255, 80, 80, 170), RGBA8(80, 160, 255, 170), RGBA8(255, 210, 60, 170), RGBA8(80, 230, 120, 170),
		RGBA8(200, 80, 255, 170), RGBA8(80, 230, 230, 170), RGBA8(255, 140, 40, 170), RGBA8(255, 120, 180, 170)
	};
	int hotWarp = -1;
	if (m_cursorValid && Map::In(m_cx, m_cz) && m.At(m_cx, m_cz).gimm == GM_WARP) hotWarp = m.At(m_cx, m_cz).gparam;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		const Cell& cc = m.At(x, z);
		if (!cc.gimm) continue;
		DWORD c = RGBA8(120, 255, 160, 150);
		if (cc.gimm == GM_MIGHT) c = RGBA8(255, 210, 80, 160);
		if (cc.gimm == GM_WARP) c = kWarp[cc.gparam & 7];
		if (cc.gimm == GM_CHEST) c = RGBA8(220, 170, 60, 180);
		if (cc.gimm == GM_WARP && hotWarp == (int)cc.gparam) c = (c & 0x00FFFFFF) | 0xFF000000;
		quad(x, z, c, .028f);
	}
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (m.At(x, z).t == T_DOOR) quad(x, z, RGBA8(120, 80, 40, 200), .04f);
	}
	if (m_cur < 0) return;
	const Unit& u = m_b.u[m_cur];
	BOOL showMove = m_phase == PH_P_MOVE || m_phase == PH_E_SHOW;
	DWORD moveCol = u.team == TEAM_PLAYER ? RGBA8(70, 150, 255, 150) : RGBA8(255, 80, 150, 135);
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		if (showMove && m_ovMove[z][x]) quad(x, z, moveCol, .035f);
		else if (m_ovAtk[z][x]) {
			DWORD c;
			switch (m_ovAtk[z][x]) {
			case 2: c = RGBA8(255, 90, 60, 70); break;
			case 3: c = RGBA8(255, 200, 60, 150); break;
			case 4: c = RGBA8(90, 255, 150, 130); break;
			case 5: c = RGBA8(255, 228, 170, 170); break;
			default: c = RGBA8(255, 80, 60, 125); break;
			}
			if (showMove && u.team != TEAM_PLAYER) c = RGBA8(255, 120, 60, 90);
			quad(x, z, c, .033f);
		}
	}
	// 経路
	if (m_phase == PH_P_MOVE && m_pathN > 1) {
		for (int i = 1; i < m_pathN; i++) {
			int p = m_path[i];
			quad(p % MAPN, p / MAPN, RGBA8(255, 240, 140, 200), .04f);
		}
	}
	// スキル効果範囲
	if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0 && m_cursorValid) {
		const SkillDef& sd = SkillInfo(m_cmdSkill);
		if ((sd.area > 0 || sd.shape != SH_RADIUS) && sd.rMax > 0 && m_ovAtk[m_cz][m_cx]) {
			int len = sd.area > 0 ? sd.area : 1;
			for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
				if (!ShapeCell(sd.shape, len, u.x, u.z, m_cx, m_cz, x, z)) continue;
				quad(x, z, RGBA8(255, 210, 70, 170), .045f);
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
			quad(ox, oz, filled ? RGBA8(70, 210, 110, 200) : RGBA8(240, 190, 50, 170), .055f);
		}
	}
	// グリッド（カーソル周辺）
	if (m_save.showGrid && m_cursorValid && (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET)) {
		for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) {
			int tx = m_cx + dx, tz = m_cz + dz;
			if (!Map::In(tx, tz) || m_ovMove[tz][tx] || m_ovAtk[tz][tx]) continue;
			int a = 50 - (abs(dx) + abs(dz)) * 6;
			if (a > 0) quad(tx, tz, RGBA8(255, 255, 255, a), .03f);
		}
	}
}

// ============================================================ 入力（戦闘）
BOOL Game::PickCell(float sx, float sy, int& cx, int& cz) const
{
	V3 org, dir;
	m_r.ScreenRay(sx, sy, org, dir);
	if (fabsf(dir.y) < 1e-4f) return FALSE;
	const Map& m = m_b.map;
	float top = MAXH * kHStep + 1.f;
	float t0 = 0;
	if (org.y > top && dir.y < 0) t0 = (org.y - top) / -dir.y;
	for (float t = t0; t < t0 + 160.f; t += .04f) {
		V3 p = org + dir * t;
		int ix = (int)floorf(p.x), iz = (int)floorf(p.z);
		if (p.y < -3.5f) return FALSE;
		if (!Map::In(ix, iz)) continue;
		const Cell& c = m.At(ix, iz);
		float y = (c.t == T_WATER || c.t == T_LAVA) ? m.StandY(ix, iz) - .25f : m.TopY(ix, iz);
		if (p.y <= y) { cx = ix; cz = iz; return TRUE; }
	}
	return FALSE;
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

void Game::UpdateHover()
{
	if (m_screen != SC_BATTLE) return;
	if (m_mouseSeen && !m_navMode && !m_seqCut) {
		BOOL overUi = FALSE;
		for (auto& b : m_btnsPrev) if (m_mx >= b.x && m_my >= b.y && m_mx < b.x + b.w && m_my < b.y + b.h) overUi = TRUE;
		int cx, cz;
		if (!overUi && PickCell((float)m_mx, (float)m_my, cx, cz)) {
			if (m_cursorValid && (cx != m_cx || cz != m_cz)
				&& (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET))
				TacSfx(TS_CURSOR);
			m_cx = cx; m_cz = cz; m_cursorValid = TRUE;
		}
	}
	m_hoverUnit = m_cursorValid ? m_b.UnitAt(m_cx, m_cz) : -1;
	if (m_hoverUnit >= 0 && !m_b.u[m_hoverUnit].alive && m_b.u[m_hoverUnit].team != TEAM_PLAYER) m_hoverUnit = -1;
	if (m_hoverUnit < 0 && m_cursorValid)
		m_hoverUnit = CorpseAt(m_b, m_cx, m_cz);
	if (m_phase == PH_P_SKILLTARGET) AimFormFace();
	m_pathN = 0;
	if (m_phase == PH_P_MOVE && m_cur >= 0 && m_cursorValid) {
		const Unit& u = m_b.u[m_cur];
		int tx = m_cx, tz = m_cz;
		if (!(m_reach.cost[tz][tx] >= 0 && m_reach.canStop[tz][tx])) {
			int t = m_b.UnitAt(tx, tz);
			if (t >= 0 && m_b.u[t].team != u.team && m_ovAtk[tz][tx]) {
				int best = -1, bc = 9999;
				for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
					if (!m_ovMove[z][x]) continue;
					if (!InAttackRange(m_b, x, z, UnitRangeMin(u), UnitRangeMax(u), tx, tz, true)) continue;
					int c = m_reach.cost[z][x] * 4 + Dist(x, z, tx, tz);
					if (c < bc) { bc = c; best = z * MAPN + x; }
				}
				if (best >= 0) { tx = best % MAPN; tz = best / MAPN; }
				else return;
			} else return;
		}
		m_pathN = ReachPath(m_reach, u.x, u.z, tx, tz, m_path, MAPN * 4);
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
	if (!m_cursorValid && m_cur >= 0) { m_cx = m_b.u[m_cur].x; m_cz = m_b.u[m_cur].z; }
	m_cx = Clampi(m_cx + sx, 0, MAPN - 1);
	m_cz = Clampi(m_cz + sz, 0, MAPN - 1);
	m_cursorValid = TRUE;
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

BOOL Game::HasAttackTarget(int ui) const
{
	for (int i = 0; i < m_b.nUnits; i++) if (CanTarget(ui, i, -1)) return TRUE;
	return FALSE;
}

BOOL Game::CanTarget(int ui, int t, int skill) const
{
	if (t < 0 || t >= m_b.nUnits) return FALSE;
	const Unit& u = m_b.u[ui];
	const Unit& d = m_b.u[t];
	if (skill >= 0 && SkillInfo(skill).effect == EF_RAISE) {
		if (d.alive || !d.killed || d.team != u.team) return FALSE;
		int s = UnitSpan(d);
		for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++)
			if (m_b.UnitAt(d.x + dx, d.z + dz) >= 0) return FALSE;
		int dist = 9999;
		int sa = UnitSpan(u);
		for (int az = 0; az < sa; az++) for (int ax = 0; ax < sa; ax++)
			for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++)
				dist = min(dist, abs((u.x + ax) - (d.x + dx)) + abs((u.z + az) - (d.z + dz)));
		int rmin = SkillCastRangeMin(u, skill), rmax = SkillCastRangeMax(u, skill);
		return dist >= rmin && dist <= rmax;
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
		int dist = 9999, sa = UnitSpan(u), sdn = UnitSpan(d);
		for (int az = 0; az < sa; az++) for (int ax = 0; ax < sa; ax++)
			for (int dz = 0; dz < sdn; dz++) for (int dx = 0; dx < sdn; dx++)
				dist = min(dist, abs((u.x + ax) - (d.x + dx)) + abs((u.z + az) - (d.z + dz)));
		if (dist < rmin || dist > rmax) return FALSE;
	} else if (!InRangeOfUnit(m_b, u, u.x, u.z, rmin, rmax, d, true)) return FALSE;
	if (IsFormSkill(skill) && !FormFits(m_b, ui, skill, d.x, d.z)) return FALSE;
	return TRUE;
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

void Game::BattleConfirmCursor()
{
	if (m_cur < 0 || !m_cursorValid) return;
	Unit& u = m_b.u[m_cur];
	if (m_phase == PH_P_MOVE) {
		if (m_b.map.At(m_cx, m_cz).t == T_DOOR && Dist(u.x, u.z, m_cx, m_cz) == 1 && CanOpenDoor(u)) {
			OpenDoor(m_b, m_cx, m_cz);
			AddPopup(CellCenter(m_cx, m_cz) + v3(0, .6f, 0), TT(TX_BT_DOOR), RGBA8(220, 200, 140), 1);
			TacSfx(TS_DOOR);
			FinishUnitTurn(1);
			return;
		}
		if (CanTerrace(m_b, m_cur, m_cx, m_cz)) { ApplyTerrace(m_cx, m_cz); return; }
		if (m_cx == u.x && m_cz == u.z) { OpenCommand(); return; }
		if (m_reach.cost[m_cz][m_cx] >= 0 && m_reach.canStop[m_cz][m_cx]) {
			m_moved = 1;
			StartMove(m_cur, m_cx, m_cz, PH_P_CMD);
			return;
		}
		int t = m_b.UnitAt(m_cx, m_cz);
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
				StartMove(m_cur, dst % MAPN, dst / MAPN, PH_P_TARGET);
				return;
			}
		}
		AddPopup(CellCenter(m_cx, m_cz) + v3(0, .5f, 0), L"×", RGBA8(255, 120, 110), 1);
		TacSfx(TS_ERROR);
		return;
	}
	if (m_phase == PH_P_SHOVE) {
		int t = m_b.UnitAt(m_cx, m_cz);
		if (CanShove(m_b, m_cur, t, NULL, NULL)) {
			int nx, nz;
			CanShove(m_b, m_cur, t, &nx, &nz);
			Unit& d = m_b.u[t];
			int hp0 = d.hp;
			DoShove(m_b, m_cur, t);
			V3 p = UnitGroundPos(d);
			d.px = p.x; d.py = p.y; d.pz = p.z;
			AddPopup(p + v3(0, m_act[t].height, 0), TT(TX_BT_SHOVE), RGBA8(255, 220, 140), 1);
			WakeNearPlayers(m_b, 9);
			if (d.hp >= hp0) {
				TacSfx(d.team == TEAM_PLAYER ? TS_HURT : TS_HIT);
				FinishUnitTurn(1);
				return;
			}
			// 奈落・溶岩へ落とした分は通常の命中と同じく撃破処理・報酬・経験値を通す。
			wchar_t s[24];
			swprintf_s(s, L"%d", hp0 - d.hp);
			AddPopup(p + v3(0, m_act[t].height + .3f, 0), s, RGBA8(255, 255, 255), 0, .15f);
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
		int t = m_b.UnitAt(m_cx, m_cz);
		if (CanTarget(m_cur, t, -1)) BeginAttack(m_cur, t, -1, m_cx, m_cz);
		return;
	}
	if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0) {
		const SkillDef& sd = SkillInfo(m_cmdSkill);
		if (sd.rMax == 0) {
			int tg[MAX_UNITS];
			if (SkillTargets(m_b, m_cur, m_cmdSkill, u.x, u.z, tg, MAX_UNITS) == 0) {
				AddBanner(TT(TX_BT_NOTARGET), NULL, RGBA8(255, 180, 160), 2, 1.4f);
				TacSfx(TS_ERROR);
				return;
			}
			BeginAttack(m_cur, m_cur, m_cmdSkill, u.x, u.z);
			return;
		}
		if (sd.area > 0 && m_cursorValid && m_ovAtk[m_cz][m_cx]) {
			int tg[MAX_UNITS];
			if (SkillTargets(m_b, m_cur, m_cmdSkill, m_cx, m_cz, tg, MAX_UNITS) == 0) {
				AddPopup(CellCenter(m_cx, m_cz) + v3(0, .5f, 0), L"×", RGBA8(255, 120, 110), 1);
				TacSfx(TS_ERROR);
				return;
			}
			int t = m_b.UnitAt(m_cx, m_cz);
			if (t < 0) t = m_cur;
			BeginAttack(m_cur, t, m_cmdSkill, m_cx, m_cz);
			return;
		}
		int t = m_b.UnitAt(m_cx, m_cz);
		if (sd.effect == EF_RAISE && (t < 0 || m_b.u[t].alive)) {
			int c = CorpseAt(m_b, m_cx, m_cz);
			if (c >= 0) t = c;
		}
		if (CanTarget(m_cur, t, m_cmdSkill)) BeginAttack(m_cur, t, m_cmdSkill, m_cx, m_cz);
		else { AddPopup(CellCenter(m_cx, m_cz) + v3(0, .5f, 0), L"×", RGBA8(255, 120, 110), 1); TacSfx(TS_ERROR); }
	}
}

void Game::ApplyTerrace(int tx, int tz)
{
	if (m_cur < 0 || !CanTerrace(m_b, m_cur, tx, tz)) { TacSfx(TS_ERROR); return; }
	DoTerrace(m_b, m_cur, tx, tz);
	m_r.BuildTerrain(m_b.map);
	m_r.BuildMinimap(m_b.map);
	const Cell& c = m_b.map.At(tx, tz);
	wchar_t s[48];
	swprintf_s(s, L"%s  H%d", TT(TX_BT_CLIMB), (int)c.h);
	AddPopup(CellCenter(tx, tz) + v3(0, .8f, 0), s, RGBA8(230, 210, 160), 1);
	TacSfx(TS_OK_BATTLE);
	ComputeOverlays();
	FinishUnitTurn(1);
}

void Game::BattleClick(int x, int y)
{
	if (m_statOpen) return;
	if (m_phase == PH_INTRO) { m_phaseT = max(m_phaseT, m_surveyN * .5f); return; }
	if (m_phase != PH_P_MOVE && m_phase != PH_P_TARGET && m_phase != PH_P_SKILLTARGET && m_phase != PH_P_SHOVE) return;
	if (y > m_h - U(150) && x < U(720)) return;
	if (x > m_w - U(190)) return;
	if (y < U(80) && x < U(360)) return;
	int cx, cz;
	if (!PickCell((float)x, (float)y, cx, cz)) return;
	m_cx = cx; m_cz = cz; m_cursorValid = TRUE;
	UpdateHover();
	BattleConfirmCursor();
}

void Game::BattleCancel()
{
	int sounded = 0;
	switch (m_phase) {
	case PH_P_MOVE:
		if (m_statOpen) { m_statOpen = 0; m_statEq = 0; sounded = 1; break; }
		if (m_cur >= 0 && (m_cx != m_b.u[m_cur].x || m_cz != m_b.u[m_cur].z)) {
			m_cx = m_b.u[m_cur].x; m_cz = m_b.u[m_cur].z; m_camPan = v3(0, 0, 0); UpdateHover();
		} else { m_phaseBeforeMenu = m_phase; m_phase = PH_MENU; m_menuConfirm = 0; m_navMode = TRUE; m_focusId = ID_BT_M_RESUME; }
		sounded = 1;
		break;
	case PH_P_CMD:
		if (m_statOpen) { m_statOpen = 0; m_statEq = 0; sounded = 1; break; }
		if (m_moved && m_cur >= 0) {
			Unit& u = m_b.u[m_cur];
			u.x = m_curX0; u.z = m_curZ0; u.face = m_curFace0;
			V3 p = UnitGroundPos(u);
			u.px = p.x; u.py = p.y; u.pz = p.z;
			m_b.RebuildOcc();
			ComputeReach(m_b, m_cur, m_reach);
			m_moved = 0;
		}
		m_phase = PH_P_MOVE;
		if (m_cur >= 0) { m_cx = m_b.u[m_cur].x; m_cz = m_b.u[m_cur].z; }
		ComputeOverlays();
		m_navMode = FALSE;
		sounded = 1;
		break;
	case PH_P_SKILLS:
		m_phase = PH_P_CMD; m_focusId = ID_BT_SKILL; m_navMode = TRUE;
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
	case PH_P_TARGET:
		if (m_target >= 0 && !m_moved) { m_target = -1; m_phase = PH_P_MOVE; ComputeOverlays(); sounded = 1; break; }
		OpenCommand();
		sounded = 1;
		break;
	case PH_P_SKILLTARGET:
		if (m_cmdFaced && m_cur >= 0) m_b.u[m_cur].face = m_cmdFace0;
		m_cmdFaced = 0;
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
		if (!m_promoAsk && m_lvqCur == 0 && m_lvT < 2.6f) break;
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
	m_moveN = ReachPath(m_reach, u.x, u.z, tx, tz, m_movePts, MAPN * 4);
	if (m_moveN < 1) { m_movePts[0] = u.z * MAPN + u.x; m_moveN = 1; }
	m_moveUnit = ui;
	m_moveT = 0;
	m_moveNext = next;
	m_phase = u.team == TEAM_PLAYER ? PH_MOVING : PH_E_MOVE;
	m_phaseT = 0;
	m_pathN = 0;
	m_camPan = v3(0, 0, 0);
	m_act[ui].flap = 0.f;
	if (m_moveN > 1) {
		BOOL fly = UnitMoveType(u) == MV_FLY && (m_act[ui].wings > 0 || u.team == TEAM_PLAYER);
		if (fly) {
			if (m_act[ui].wings == 0) m_act[ui].wings = 1;
			if (m_act[ui].hover < .2f) m_act[ui].hover = .35f;
			m_act[ui].flap = 1.f;
			ActorPlay(m_act[ui], CL_IDLE, 1, .12f);
			m_act[ui].animSpeed = 1.f;
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
	float speed = (m_save.battleAnim ? 5.2f : 2.6f) * 1.5f;
	m_moveT += dt * speed;
	int seg = (int)m_moveT;
	{
		static int stepSeg = -999;
		if (seg != stepSeg) {
			if (stepSeg >= 0 && seg > stepSeg && !fly) TacSfx(TS_STEP);
			stepSeg = seg;
		}
		if (seg >= m_moveN - 1) stepSeg = -999;
	}
	if (seg >= m_moveN - 1) {
		int last = m_movePts[m_moveN - 1];
		u.x = last % MAPN; u.z = last / MAPN;
		m_b.RebuildOcc();
		int gp = m_b.map.At(u.x, u.z).gparam;
		int tile = ApplyTile(m_b, ui);
		int chestItem = -1;
		if (tile == GM_CHEST && u.team == TEAM_PLAYER) {
			int it = -1;
			if (gp == 0) it = IT_HERB;
			else if (gp == 1) it = IT_ORE;
			else if (gp == 2) it = IT_CRYSTAL;
			else if (gp >= 3) it = IT_SHARD;
			if (it >= 0 && it < IT_COUNT) m_save.itemN[it]++;
			chestItem = it;
		}
		V3 p = UnitGroundPos(u);
		u.px = p.x; u.py = p.y; u.pz = p.z;
		u.hpShow = (float)u.hp;
		if (tile == GM_HEAL) {
			AddPopup(p + v3(0, m_act[ui].height, 0), TT(TX_GM_HEAL), RGBA8(120, 255, 160), 0);
			TacSfx(TS_HEAL);
		} else if (tile == GM_MIGHT) {
			AddPopup(p + v3(0, m_act[ui].height, 0), TT(TX_GM_MIGHT), RGBA8(255, 220, 120), 1);
			TacSfx(TS_BUFF);
		} else if (tile == GM_WARP) {
			AddPopup(p + v3(0, m_act[ui].height, 0), TT(TX_GM_WARP), RGBA8(180, 140, 255), 1);
			TacSfx(TS_WARP);
		} else if (tile == GM_CHEST) {
			const wchar_t* in = L"";
			if (chestItem == IT_HERB) in = TT(TX_IT_HERB);
			else if (chestItem == IT_ORE) in = TT(TX_IT_ORE);
			else if (chestItem == IT_CRYSTAL) in = TT(TX_IT_CRYSTAL);
			else if (chestItem == IT_SHARD) in = TT(TX_IT_SHARD);
			BOOL opener = CanOpenDoor(u);
			int add = max(1, u.ch.stat[S_HP] * (opener ? 40 : 25) / 100);
			wchar_t msg[160];
			if (chestItem >= 0 && (opener || gp == 1))
				swprintf_s(msg, L"%s  %s  %s+%d  %s+12", TT(TX_GM_CHEST), in, TT(TX_ST_HP), add, TT(TX_MISC_EXP));
			else if (chestItem >= 0)
				swprintf_s(msg, L"%s  %s  %s+%d", TT(TX_GM_CHEST), in, TT(TX_ST_HP), add);
			else
				swprintf_s(msg, L"%s  %s+%d", TT(TX_GM_CHEST), TT(TX_ST_HP), add);
			AddPopup(p + v3(0, m_act[ui].height, 0), msg, RGBA8(255, 210, 120), 1, 0, 3.6f);
			AddLog(msg, RGBA8(255, 210, 120));
			TacSfx(TS_CHEST);
		}
		WakeNearPlayers(m_b, 9);
		m_act[ui].animSpeed = 1.f;
		m_act[ui].flap = 0.f;
		ActorPlay(m_act[ui], CL_IDLE, 1, .15f);
		int next = m_moveNext;
		if (u.team == TEAM_PLAYER && m_ai.wild == 0) {
			if (next == PH_P_TARGET && m_target >= 0) {
				m_phase = PH_P_TARGET;
				FaceTowards(ui, m_b.u[m_target].x, m_b.u[m_target].z);
				ComputeOverlays();
				m_cx = m_b.u[m_target].x; m_cz = m_b.u[m_target].z;
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
	int ax = a % MAPN, az = a / MAPN, bx = b % MAPN, bz = b / MAPN;
	V3 pa = CellCenter(ax, az), pb = CellCenter(bx, bz);
	float off = (UnitSpan(u) - 1) * 0.5f;
	pa.x += off; pa.z += off; pb.x += off; pb.z += off;
	if (fly) {
		pa.y = max(pa.y, m_b.map.TopY(ax, az)); pb.y = max(pb.y, m_b.map.TopY(bx, bz));
		if (m_b.map.At(ax, az).t == T_CHASM) pa.y = 0;
		if (m_b.map.At(bx, bz).t == T_CHASM) pb.y = 0;
	}
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
				m_cx = m_b.u[i].x; m_cz = m_b.u[i].z; m_cursorValid = TRUE; break;
			}
		}
		if (!m_cursorValid) {
			for (int i = 0; i < m_b.nUnits; i++) if (CanTarget(m_cur, i, skill)) { m_cx = m_b.u[i].x; m_cz = m_b.u[i].z; m_cursorValid = TRUE; break; }
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
	} else { m_cx = u.x; m_cz = u.z; m_cursorValid = TRUE; }
}

// ============================================================ アクション
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
	for (int i = 0; i < m_b.nUnits; i++) { m_preHp[i] = m_b.u[i].hp; m_dispHp[i] = (float)m_b.u[i].hp; }
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
	m_cutKind = IsCraftId(skill) ? 1 : (IsFormSkill(skill) ? 2 : 0);
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
		m_cutAllyN = m_cutFoeN = 0;
		if (skill >= 0)
			AddBanner(IsCraftId(skill) ? CraftName(skill) : SkillLabel(skill), NULL, ElemColor(SkillInfo(skill).element), 2, 1.4f);
	}
	m_phase = PH_ACTION;
	m_phaseT = 0;
	if (target >= 0 && target < m_b.nUnits) WakeGroup(m_b, m_b.u[target].grp);
	m_proj.active = 0;
	m_ovDirty = TRUE;
	memset(m_ovMove, 0, sizeof(m_ovMove));
	memset(m_ovAtk, 0, sizeof(m_ovAtk));
}

BOOL Game::CutFind(int ui, int* side, int* idx) const
{
	for (int i = 0; i < m_cutAllyN; i++) if (m_cutAlly[i] == ui) { if (side) *side = 0; if (idx) *idx = i; return TRUE; }
	for (int i = 0; i < m_cutFoeN; i++) if (m_cutFoe[i] == ui) { if (side) *side = 1; if (idx) *idx = i; return TRUE; }
	return FALSE;
}

V3 Game::CutStand(int side, int idx) const
{
	if (m_cutKind != 0) {
		if (side == 0 && idx >= 0 && idx < m_cutAllyN) return m_cutAllyPos[idx];
		if (side == 1 && idx >= 0 && idx < m_cutFoeN) return m_cutFoePos[idx];
	}
	if (m_cutFoeN <= 0) {
		float n = (float)max(1, m_cutAllyN);
		float x = (idx - (n - 1.f) * .5f) * .9f;
		return v3(x, 0, (idx % 2) ? .22f : -.22f);
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
		if (n >= 6) return;
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
	if (m_cutKind == 0) return;
	for (int i = 0; i < m_cutFoeN; i++) {
		float z = (i == 0) ? 0.f : ((i & 1) ? .48f : -.36f);
		float x = 1.55f + i * .78f;
		if (m_cutKind == 1) x += .4f;
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
	if (m_ult) return 2.15f;
	return m_seqSkill >= 0 ? 1.05f : .45f;
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
	case FM_CIRCLE: allyClip = CL_CAST_LONG; break;
	case FM_PAIR: allyClip = CL_CAST_RAISE; break;
	default: break;
	}
	for (int i = 0; i < m_cutAllyN; i++) {
		int id = m_cutAlly[i];
		if (id == m_cutA || id < 0 || m_dieT[id] >= 0) continue;
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
	if (m_cutKind == 0 || m_cutA < 0) return;
	if (m_cutStage != 1 && m_cutStage != 2) return;
	const SkillDef* sd = m_seqSkill >= 0 ? &SkillInfo(m_seqSkill) : NULL;
	DWORD ec = sd ? ElemColor(sd->element) : RGBA8(170, 210, 255);
	float r = (ec & 255) / 255.f, g = ((ec >> 8) & 255) / 255.f, b = ((ec >> 16) & 255) / 255.f;
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
		auto pop = [&](const wchar_t* txt, DWORD col, int style, float delay) {
			if (visible) AddPopup(head, txt, col, style, delay);
			else {
				Popup pp = {};
				pp.p = head; wcsncpy_s(pp.s, txt, _TRUNCATE); pp.col = col; pp.style = style; pp.life = style == 2 ? 1.6f : 1.25f; pp.size = style == 2 ? 1.35f : 1.f; pp.delay = delay;
				m_deferPopups.push_back(pp);
			}
		};
		if (kind == SK_HEAL) {
			m_dispHp[d] = min((float)du.ch.stat[S_HP], m_dispHp[d] - h.dmg);
			swprintf_s(s, L"+%d", -h.dmg);
			pop(s, RGBA8(120, 255, 150), 0, 0);
			if (sd && sd->effect == EF_RAISE && h.hit) {
				m_dieT[d] = -1;
				du.dissolve = 0;
				du.flash = 1.f;
				ActorPlay(m_act[d], CL_SPAWN, 0, .8f);
				pop(sd ? SkillLabel(st.skill) : TT(TX_SK_RAISE), RGBA8(255, 236, 170), 1, .12f);
				Burst(mid, 22, 2.4f, .1f, 1.1f, 1.f, .92f, .55f, 2, 1, -1.2f, 0, .9f);
			}
			if (sd && sd->effect == EF_CLEANSE && h.landed) pop(EffectWord(EF_CLEANSE), RGBA8(180, 230, 255), 1, .2f);
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
			pop(TT(TX_BT_BUFF), RGBA8(150, 210, 255), 1, 0);
			if (sfxOnce(1)) TacSfx(TS_BUFF);
			if (visible || !cutView) ElementFx(EL_NONE, SK_BUFF, mid, mid, FALSE);
			continue;
		}
		if (!h.hit) {
			BOOL landed = FALSE;
			for (int q = 0; q < st.nHits; q++) if (st.hits[q].defender == d && st.hits[q].hit) landed = TRUE;
			if (!landed) {
				m_act[d].recoil = 0.f;
				int cc = m_act[d].clip;
				if (cc == CL_HIT_A || cc == CL_HIT_B || cc == CL_DODGE) ActorPlay(m_act[d], CL_IDLE, 1, .1f);
			}
			pop(TT(TX_BT_MISS), RGBA8(200, 220, 255), 1, 0);
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
			swprintf_s(s, L"+%d", h.drain);
			AddPopup(ActorPos(h.attacker) + v3(0, min(m_act[h.attacker].height, cutView ? 1.32f : m_act[h.attacker].height) + .1f, cutView ? -.4f : 0.f), s, RGBA8(120, 255, 150), 0, .25f);
		}
		swprintf_s(s, L"%d", h.dmg);
		BOOL followDead = h.overkill && !h.kill && du.hp <= 0;
		if (h.crit) {
			pop(TT(TX_BT_CRITICAL), RGBA8(255, 230, 80), 2, 0);
			if (sfxOnce(8)) TacSfx(TS_CRIT);
			pop(s, RGBA8(255, 220, 60), 2, .12f);
			Shake(.6f);
			if (cutView) m_flashWhite = 1.f;
		} else {
			pop(s, kind == SK_MAGIC ? RGBA8(200, 170, 255) : RGBA8(255, 255, 255), 0, 0);
			Shake(.22f);
			BOOL ally = du.team == TEAM_PLAYER;
			if (sfxOnce(ally && kind != SK_MAGIC ? 16 : 32)) TacSfxHit(st.skill, kind, el, UnitMotionStyle(m_b.u[h.attacker], st.skill), ally);
		}
		if (visible || !cutView) {
			StrikeFx(st.skill, el, kind, UnitMotionStyle(m_b.u[h.attacker], st.skill), ActorPos(h.attacker), mid, h.crit || cutView);
			if (h.crit) Burst(mid, 30, 7.f, .09f, .5f, 1.f, .95f, .6f, 3, 1, 3.f, .06f, .2f);
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
		if (h.overkill) pop(TT(TX_BT_OVERKILL), RGBA8(255, 170, 80), 1, h.crit ? .28f : .14f);
		if (h.guts) { pop(TT(TX_BT_GUTS), RGBA8(255, 196, 96), 1, .22f); if (sfxOnce(64)) TacSfx(TS_GUTS); }
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
			ActorPlay(m_act[d], CL_DEATH, 0, .08f);
			m_dieT[d] = 0;
		} else {
			ActorPlay(m_act[d], h.crit ? CL_HIT_B : CL_HIT_A, 0, .06f);
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
				pop(e, blocked ? RGBA8(180, 220, 255) : RGBA8(220, 160, 255), 1, .35f);
				if (sfxOnce(blocked ? 512 : 1024)) TacSfxStatus(sd->effect, blocked ? 1 : 0);
			}
		}
	}
}

// 弓を装備していれば弦を引く。既定の弩は両手で構えて撃つ。
static int UnitAttackClip(const Unit& u, int variant, int skill)
{
	return AttackClipFor(UnitMotionStyle(u, skill), variant);
}

static int CutClipFor(const Unit& u, int skill, int variant)
{
	const SkillDef* sd = skill >= 0 ? &SkillInfo(skill) : NULL;
	if (sd && IsFormSkill(skill)) {
		if (FormOf(skill) == FM_CIRCLE) return CL_CAST_LONG;
		if (FormOf(skill) == FM_PAIR) return CL_CAST_RAISE;
	}
	if (sd && sd->kind != SK_PHYS) return CastClipFor(sd->kind);
	return UnitAttackClip(u, variant, skill);
}

void Game::UpdateSeq(float dt)
{
	if (m_seqCut) {
		m_cutStageT += dt;
		if (m_cutStage == 0) {
			if (m_cutStageT >= .35f) {
				m_cutStage = 1; m_cutStageT = 0;
				m_lunge[m_cutA] = 0;
				if (m_cutD >= 0) m_lunge[m_cutD] = 0;
				{
					int sk0 = (m_seqN > 0) ? m_seq[0].skill : -1;
					int clip = CutClipFor(m_b.u[m_cutA], sk0, sk0 >= 0 ? 1 : 0);
					Actor& ca = m_act[m_cutA];
					ActorPlay(ca, clip, 0, .06f);
					ca.skillPose = sk0;
					float full = ActorClipLen(ca, m_lib, m_r, clip);
					float need = CutIntroLen();
					float wind = full * ActorWindup(ca, m_r, clip);
					ca.animSpeed = Clampf(wind / max(0.25f, need), 0.12f, 1.15f);
				}
				if (m_cutD >= 0) ActorPlay(m_act[m_cutD], CL_IDLE, 1, .15f);
				PoseFormAllies();
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
			if (m_cutStageT >= .3f) {
				m_cutStage = 4; m_cutStageT = 0;
				m_popups.clear();
				for (auto& p : m_deferPopups) m_popups.push_back(p);
				m_deferPopups.clear();
				for (int i = 0; i < m_b.nUnits; i++) m_lunge[i] = 0;
				for (int i = 0; i < m_b.nUnits; i++) {
					m_act[i].animSpeed = 1.f;
					if (m_dieT[i] >= 0 && m_b.u[i].hp <= 0) continue;
					ActorPlay(m_act[i], CL_IDLE, 1, .1f);
				}
			}
			return;
		}
		if (m_cutStage == 4) {
			if (m_cutStageT >= .3f) { m_seqCut = FALSE; m_cutKind = 0; m_cutStage = 0; FinishSeq(); }
			return;
		}
	}
	// 攻防の再生（マップ・カットイン共通）
	if (m_seqCut && m_cutStage == 2) CutDressFx();
	if (m_seqCur >= m_seqN) {
		m_seqT += dt;
		if (m_seqT >= .35f) {
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
		int clip = cast ? CastClipFor(sd->kind) : UnitAttackClip(au, sd ? 1 : (m_seqCur & 1), st.skill);
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
			case FM_CIRCLE: allyClip = CL_CAST_LONG; break;
			case FM_PAIR: allyClip = CL_CAST_RAISE; break;
			default: break;
			}
			for (int i = 0; i < nm; i++) {
				if (mem[i] == A || m_dieT[mem[i]] >= 0) continue;
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
		if (!held) ActorPlay(aa, clip, 0, follow ? .04f : .08f);
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
				if (id != A) m_lunge[id] = m_lunge[A] * .8f;
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
			SeqImpact(st, minV + m_seqImpact);
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
		if (!chain && (m_b.u[A].hp > 0 || m_dispHp[A] > 0)) ActorPlay(aa, CL_IDLE, 1, .15f);
		m_lunge[A] = 0;
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

void Game::FinishSeq()
{
	int lootSfx = 0;
	for (int i = 0; i < m_b.nUnits; i++) {
		Unit& u = m_b.u[i];
		if (u.alive && u.hp > 0 && m_dieT[i] >= 0) {
			m_dieT[i] = -1;
			u.dissolve = 0;
			ActorPlay(m_act[i], CL_IDLE, 1, .15f);
		}
		if (u.alive && u.hp <= 0) {
			u.alive = 0;
			u.killed = 1;
			if (m_dieT[i] < 0) { m_dieT[i] = 0; ActorPlay(m_act[i], CL_DEATH, 0, .1f); }
			if (u.team == TEAM_ENEMY && u.boss) m_bossDown = 1;
			if (u.team == TEAM_ENEMY) {
				FoeNoteKill(u.monster);
				lootSfx = 1;
				int coop = PlayerCoop(m_b, m_strk, m_strkN, i);
				int pay = 5 + u.ch.lv / 2 + (u.boss ? 28 : 0);
				m_save.gold += pay;
				wchar_t coin[32];
				swprintf_s(coin, L"+%d G", pay);
				AddPopup(v3(u.px, u.py + .8f, u.pz), coin, RGBA8(255, 220, 90), 0, 0, 3.6f);
				AddLog(coin, RGBA8(255, 220, 90));
				int rolls = m_rng.Chance(coop >= 2 ? 80 : 40) ? 2 : 1;
				int chance = u.boss ? 70 : 28;
				if (coop >= 2) chance += 34;
				if (chance > 92) chance = 92;
				for (int r = 0; r < rolls; r++) {
					if (!m_rng.Chance(chance)) continue;
					int beforeN = m_save.gearN;
					int gid = GearTryDrop(m_save, m_battleStage, u.monster, m_rng);
					if (m_save.gearN >= GEAR_STASH && (gid > 0 || beforeN >= GEAR_STASH)) m_stashFull = 1;
					if (gid > 0) {
						AddPopup(v3(u.px, u.py + 1.3f + r * .35f, u.pz), GearName(gid), RGBA8(180, 255, 190), 1, .12f * r, 3.6f);
						AddLog(GearName(gid), RGBA8(180, 255, 190));
					}
				}
				int goods = (m_rng.Chance(u.boss ? 78 : (coop >= 2 ? 62 : 40)) ? 1 : 0) + ((coop >= 2 && m_rng.Chance(50)) ? 1 : 0);
				for (int g = 0; g < goods; g++) {
					wchar_t cn[96];
					if (!DropConsumable(m_save, u.monster, m_rng, cn, 96)) continue;
					AddPopup(v3(u.px, u.py + 1.55f + g * .32f, u.pz), cn, RGBA8(255, 210, 140), 1, .15f, 3.6f);
					AddLog(cn, RGBA8(255, 210, 140));
				}
				if (m_rng.Chance(coop >= 2 ? 45 : 22)) {
					int it = m_rng.Chance(50) ? IT_ORE : IT_HERB;
					m_save.itemN[it]++;
					const wchar_t* in = TT(it == IT_ORE ? TX_IT_ORE : TX_IT_HERB);
					AddPopup(v3(u.px, u.py + 1.9f, u.pz), in, RGBA8(180, 220, 160), 1, .2f, 3.6f);
					AddLog(in, RGBA8(180, 220, 160));
				}
				int trophyN = (u.boss || coop >= 2 || m_rng.Chance(45)) ? 2 : 1;
				for (int di = 0; di < trophyN; di++) {
					wchar_t part[80];
					int sold = 0;
					if (!DropTrophy(u.monster, u.boss, m_rng, part, 80, &sold)) continue;
					wchar_t line[96];
					if (sold > 0) swprintf_s(line, L"%s +%dG", part, sold);
					else swprintf_s(line, L"%s", part);
					AddPopup(v3(u.px, u.py + 2.2f + di * .35f, u.pz), line, RGBA8(255, 186, 96), 1, .1f * di, 3.6f);
					AddLog(line, RGBA8(255, 186, 96));
				}
				wchar_t loot[80];
				if (DropCraftLoot(u.monster, u.boss, m_rng, loot, 80)) {
					AddPopup(v3(u.px, u.py + 2.9f, u.pz), loot, RGBA8(190, 170, 255), 1, .2f, 3.6f);
					AddLog(loot, RGBA8(190, 170, 255));
				}
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
			AddBanner(s, NULL, u.team == TEAM_PLAYER ? RGBA8(255, 150, 140) : RGBA8(255, 226, 150), 2, 1.6f);
		}
		m_dispHp[i] = (float)u.hp;
	}
	if (lootSfx) TacSfx(TS_GET);
	m_b.RebuildOcc();
	AwardExp();
	if (m_lvqN > 0) { m_phase = PH_LEVELUP; m_lvqCur = 0; m_lvT = 0; m_navMode = TRUE; m_focusId = ID_BT_LVOK; }
	else FinishUnitTurn(1);
}

void Game::AwardExp()
{
	m_lvqN = 0;
	m_promoAsk = 0;
	long long gain[MAX_UNITS] = {};
	int coopHit[MAX_UNITS] = {};
	int topLv = 1;
	for (int i = 0; i < m_b.nUnits; i++) {
		if (m_b.u[i].team == TEAM_PLAYER && m_b.u[i].ch.lv > topLv)
			topLv = m_b.u[i].ch.lv;
	}
	for (int k = 0; k < m_strkN; k++) {
		const Strike& s = m_strk[k];
		const Unit& a = m_b.u[s.attacker];
		if (a.team != TEAM_PLAYER) continue;
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
		long long g = gain[i];
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
		int steps = 0;
		while (pool >= ExpToNext(u.ch.lv) && u.ch.lv < TAC_LV_MAX && steps < TAC_LV_MAX) {
			pool -= ExpToNext(u.ch.lv);
			int gains[S_COUNT];
			int* gp = gains;
			int learnedBuf[4];
			int nLearn = 0;
			int oldLv = u.ch.lv;
			LevelUpInfo* Lp = NULL;
			if (m_lvqN < MAX_PARTY * 4) {
				Lp = &m_lvq[m_lvqN++];
				memset(Lp, 0, sizeof(*Lp));
				Lp->unit = i;
				Lp->oldLv = oldLv;
				gp = Lp->gains;
			}
			int* learned = Lp ? Lp->learned : learnedBuf;
			nLearn = CharLevelUp(u.ch, m_rng, gp, learned, 4);
			if (Lp) { Lp->nLearned = nLearn; Lp->newLv = u.ch.lv; }
			u.hp = min(u.ch.stat[S_HP], u.hp + gp[S_HP]);
			u.mp = min(u.ch.stat[S_MP], u.mp + gp[S_MP]);
			u.levelsGained++;
			for (int lk = 0; lk < nLearn; lk++) {
				wchar_t nm[40], lvline[120];
				UnitTitle(u, nm, 40);
				swprintf_s(lvline, L"%s  %s: %s", nm, TT(TX_BT_LEARNED), SkillLabel(learned[lk]));
				AddLog(lvline, RGBA8(255, 226, 150));
			}
			steps++;
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
			wchar_t s[80];
			int need = ExpToNext(u.ch.lv);
			int left = u.ch.lv >= TAC_LV_MAX ? 0 : (int)min(2000000000LL, max(0LL, (long long)need - pool));
			long long show = g > 2000000000LL ? 2000000000LL : g;
			if (coopHit[i])
				swprintf_s(s, L"%s +%lld %s  %s %d", TT(TX_MISC_EXP), show, TT(TX_BT_COOP), TT(TX_EXP_LEFT), left);
			else
				swprintf_s(s, L"%s +%lld  %s %d", TT(TX_MISC_EXP), show, TT(TX_EXP_LEFT), left);
			AddPopup(v3(u.px, u.py + m_act[i].height + .45f, u.pz), s, RGBA8(150, 210, 255), 1, .3f, 3.6f);
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
		BOOL moving = (m_phase == PH_MOVING || m_phase == PH_E_MOVE) && m_moveUnit == i;
		if (!moving) {
			V3 g = UnitGroundPos(u);
			float k = Clampf(dt * 12.f, 0, 1);
			u.px = Lerpf(u.px, g.x, k); u.py = Lerpf(u.py, g.y, k); u.pz = Lerpf(u.pz, g.z, k);
		}
		float baseHover = a.hover;
		u.hover = baseHover > 0 ? baseHover + sinf(m_time * 2.2f + i) * .06f : 0;
		m_faceShow[i] = AngLerp(m_faceShow[i], u.face, Clampf(dt * 10.f, 0, 1));
		float targetHp = (m_phase == PH_ACTION) ? m_dispHp[i] : (float)u.hp;
		u.hpShow = Lerpf(u.hpShow, targetHp, Clampf(dt * 5.f, 0, 1));
		if (fabsf(u.hpShow - targetHp) < .05f) u.hpShow = targetHp;
		u.flash = max(0.f, u.flash - dt * 3.f);
		if (!u.alive && u.team != TEAM_PLAYER && m_dieT[i] < 0) m_dieT[i] = 0;
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
		if (!a.loop && a.clip != CL_DEATH && ActorClipDone(a, m_lib, m_r) && !(m_phase == PH_ACTION && i == (m_seqCur < m_seqN ? m_seq[m_seqCur].attacker : -1))) {
			a.animSpeed = 1.f;
			ActorPlay(a, (m_phase == PH_OVER && m_victory && u.team == TEAM_PLAYER) ? CL_CHEER : CL_IDLE, 1, .2f);
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
	if (m_phase == PH_INTRO) {
		// 各区画は 0.5 秒。前半で次の区画へ寄せ、残りは止めて見せる。最後に自軍へ戻って寄る。
		const float tEnd = m_surveyN * .5f;
		if (m_phaseT < tEnd) {
			int i = (int)(m_phaseT / .5f);
			if (i > m_surveyN - 1) i = m_surveyN - 1;
			float f = Smooth01(Clampf((m_phaseT - i * .5f) / .22f, 0, 1));
			focus = Lerp(m_surveyPt[i > 0 ? i - 1 : 0], m_surveyPt[i], f);
			m_camDistGoal = 26.f;
		} else {
			V3 from = m_surveyN > 0 ? m_surveyPt[m_surveyN - 1] : v3(MAPN * .5f, 2.f, MAPN * .5f);
			focus = Lerp(from, m_camGoal, Smooth01((m_phaseT - tEnd) / 1.2f));
			m_camDistGoal = 18.f;
		}
	}
	BOOL playerSel = m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE || m_phase == PH_P_CMD || m_phase == PH_P_SKILLS || m_phase == PH_P_ITEM || m_phase == PH_MENU;
	if (!playerSel) m_camPan = m_camPan * expf(-dt * 4.f);
	focus = focus + m_camPan;
	focus.x = Clampf(focus.x, 0, (float)MAPN);
	focus.z = Clampf(focus.z, 0, (float)MAPN);
	float sp = m_save.camSpeed <= 0 ? .55f : (m_save.camSpeed >= 2 ? 1.75f : 1.f);
	float k = Clampf(dt * (m_phase == PH_INTRO ? 9.f : 4.5f * sp), 0, 1);
	m_camTarget = Lerp(m_camTarget, focus, k);
	m_camYaw = AngLerp(m_camYaw, m_camYawGoal, Clampf(dt * 6.f * sp, 0, 1));
	if (m_zoomHold) ApplyZoomHold(dt);
	else m_camDist = Lerpf(m_camDist, m_camDistGoal, Clampf(dt * (m_phase == PH_INTRO ? 2.2f : 5.f), 0, 1));
}

void Game::UpdateBattle(float dt)
{
	if (m_stashFull) { UpdateUnitsVisual(dt); return; }
	m_phaseT += dt;
	UpdateUnitsVisual(dt);
	switch (m_phase) {
	case PH_INTRO:
		if (m_phaseT > m_surveyN * .5f + 1.4f) { m_phase = PH_NEXT; m_phaseT = 0; }
		break;
	case PH_NEXT: BeginTurn(); break;
	case PH_MOVING: case PH_E_MOVE: UpdateMove(dt); break;
	case PH_ACTION: UpdateSeq(dt); break;
	case PH_E_SHOW: {
		float wait = m_save.battleAnim ? .45f : .85f;
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
		if (m_phaseT > .22f) { m_phase = PH_NEXT; m_phaseT = 0; }
		break;
	case PH_LEVELUP:
		m_lvT += dt;
		if (!m_promoAsk && m_lvqCur == 0 && m_lvqN > 0 && m_lvT - dt < 2.6f && m_lvT >= 2.6f) {
			TacSfx(TS_LEVEL);
			if (m_lvq[0].nLearned > 0) TacSfx(TS_LEARN);
		}
		break;
	case PH_OVER:
		m_overT += dt;
		if (m_overT > 3.f) EndBattle(m_victory);
		break;
	default: break;
	}
	if (m_screen != SC_BATTLE) return;
	UpdateCamera(dt);
	if (m_mouseSeen && !m_navMode) UpdateHover();
	if (m_flashWhite > 0) m_flashWhite = max(0.f, m_flashWhite - dt * 3.f);
}

// ============================================================ 描画（マップ）
void Game::DrawBattle3D()
{
	Env e;
	ThemeEnv(m_b.map.theme, e);
	V3 target = m_camTarget + v3(0, .4f, 0);
	if (m_shake > 0) target = target + v3(m_rng.F(-1, 1), m_rng.F(-1, 1) * .5f, m_rng.F(-1, 1)) * (m_shake * .08f);
	ApplyZoomHold();
	SetupCamera(target, m_camYaw, m_camPitch, m_camDist, .62f);
	m_r.SetFog(m_camDist * 1.4f, m_camDist * 4.2f + 18.f, .35f, e.fog);
	m_r.SetAmbient(e.ambSky, e.ambGnd, e.amb, e.hard);
	m_r.SetLight(e.light, m_camTarget, min(30.f, m_camDist * 1.35f + 6.f));
	auto nearCam = [&](const Unit& u) -> BOOL {
		if (u.team == TEAM_PLAYER) return TRUE;
		float dx = u.px - m_camTarget.x, dz = u.pz - m_camTarget.z;
		return (dx * dx + dz * dz) < 26.f * 26.f ? TRUE : FALSE;
	};
	auto foeGone = [&](int i) -> BOOL {
		const Unit& u = m_b.u[i];
		if (u.alive || u.team == TEAM_PLAYER) return FALSE;
		return m_dieT[i] < 0.f || m_dieT[i] > 1.15f;
	};
	for (int i = 0; i < m_b.nUnits; i++) {
		if (foeGone(i) || m_b.u[i].dissolve >= 1.f || !nearCam(m_b.u[i])) continue;
		ActorPose(m_act[i], m_lib, m_r);
	}
	m_r.BeginShadowPass();
	m_r.DrawTerrain();
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (foeGone(i) || u.dissolve >= .5f || !nearCam(u)) continue;
		DrawParams dp;
		ActorDraw(m_r, m_lib, m_act[i], ActorWorld(m_act[i], ActorPos(i), ActorFace(i)), dp, m_time);
	}
	m_r.BeginMainPass();
	m_r.DrawSky(e.skyTop, e.skyBot, 0, 0);
	m_r.DrawTerrain();
	m_r.DrawWater();
	BuildOverlayQuads();
	m_r.DrawOverlayQuads(m_ovQuads.data(), m_ovQuads.size(), 0);
	// カーソル
	BOOL showCursor = m_cursorValid && (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE);
	if (showCursor) {
		DWORD cc = RGBA8(255, 255, 255, 230);
		if (m_phase == PH_P_MOVE) {
			BOOL ok = (m_reach.cost[m_cz][m_cx] >= 0 && m_reach.canStop[m_cz][m_cx]) || m_pathN > 1 || (m_cur >= 0 && m_cx == m_b.u[m_cur].x && m_cz == m_b.u[m_cur].z);
			int t = m_b.UnitAt(m_cx, m_cz);
			if (t >= 0 && m_cur >= 0 && m_b.u[t].team != m_b.u[m_cur].team && (m_pathN > 1 || CanTarget(m_cur, t, -1))) cc = RGBA8(255, 200, 80, 255);
			else if (!ok) cc = RGBA8(255, 70, 60, 230);
			else cc = RGBA8(140, 230, 255, 255);
		} else {
			int t = m_b.UnitAt(m_cx, m_cz);
			if (t < 0 && m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0 && SkillInfo(m_cmdSkill).effect == EF_RAISE)
				t = CorpseAt(m_b, m_cx, m_cz);
			BOOL ok = m_cur >= 0 && (m_phase == PH_P_TARGET ? CanTarget(m_cur, t, -1) : (m_cmdSkill >= 0 && (SkillInfo(m_cmdSkill).rMax == 0 || CanTarget(m_cur, t, m_cmdSkill))));
			cc = ok ? RGBA8(255, 210, 80, 255) : RGBA8(255, 70, 60, 230);
		}
		VtxS cq[6];
		int cqn = 0;
		const Map& m = m_b.map;
		const Cell& c = m.At(m_cx, m_cz);
		float y = (c.t == T_WATER || c.t == T_LAVA) ? m.StandY(m_cx, m_cz) - .25f : m.TopY(m_cx, m_cz);
		y += .06f;
		float grow = .04f * sinf(m_time * 6.f);
		float x0 = m_cx - grow, x1 = m_cx + 1 + grow, z0 = m_cz - grow, z1 = m_cz + 1 + grow;
		V3 p[4] = { v3(x0, y, z0), v3(x0, y, z1), v3(x1, y, z1), v3(x1, y, z0) };
		float uv[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
		static const int ord[6] = { 0, 1, 2, 0, 2, 3 };
		for (int k = 0; k < 6; k++) {
			VtxS v;
			int i = ord[k];
			v.p[0] = p[i].x; v.p[1] = p[i].y; v.p[2] = p[i].z;
			v.n[0] = 0; v.n[1] = 1; v.n[2] = 0;
			v.uv[0] = uv[i][0]; v.uv[1] = uv[i][1];
			v.c = cc;
			cq[cqn++] = v;
		}
		m_r.DrawOverlayQuads(cq, cqn, 1);
	}
	// 足元リング
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (!nearCam(u)) continue;
		if (!u.alive && (u.team != TEAM_PLAYER || u.dissolve >= 1.f)) continue;
		BOOL active = u.alive && i == m_cur && m_phase != PH_OVER;
		DrawParams rp;
		rp.emissive = active ? 1.4f : .6f;
		rp.rim = 0;
		float pulse = active ? .8f + .2f * sinf(m_time * 5.f) : .5f;
		DWORD tc = u.team == TEAM_PLAYER ? RGBA8(90, 170, 255, (int)(255 * pulse * (active ? 1.f : .55f))) : RGBA8(255, 80, 70, (int)(255 * pulse * (active ? 1.f : .55f)));
		if (u.boss) tc = RGBA8(255, 200, 60, (int)(255 * pulse * (active ? 1.f : .7f)));
		if (!u.alive) tc = RGBA8(170, 176, 190, 120);
		rp.tint = tc;
		float s = active ? 1.f + .06f * sinf(m_time * 5.f) : .85f;
		int sp = UnitSpan(u);
		if (sp <= 1) {
			V3 g = v3(u.px, u.py + .05f, u.pz);
			m_r.DrawStatic(m_lib.ring, MMul(MMul(MScale(s, 1, s), MRotY(m_time * (active ? 1.2f : .3f))), MTrans(g.x, g.y, g.z)), rp);
		} else {
			float sc = s * 0.72f;
			for (int dz = 0; dz < sp; dz++) for (int dx = 0; dx < sp; dx++) {
				int x = u.x + dx, z = u.z + dz;
				if (!Map::In(x, z)) continue;
				V3 g = CellCenter(x, z);
				g.y += .05f;
				m_r.DrawStatic(m_lib.ring, MMul(MMul(MScale(sc, 1, sc), MRotY(m_time * (active ? 1.2f : .3f))), MTrans(g.x, g.y, g.z)), rp);
			}
		}
	}
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (foeGone(i) || u.dissolve >= 1.f || !nearCam(u)) continue;
		DrawParams dp;
		dp.outline = .013f;
		if (!u.alert && u.team == TEAM_ENEMY) dp.tint = RGBA8(150, 160, 190);
		dp.dissolve = u.dissolve;
		dp.flash = u.flash;
		dp.rim = u.team == TEAM_PLAYER ? .35f : .45f;
		if (m_hoverUnit == i && m_phase != PH_ACTION) dp.rim = .9f;
		if ((m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET) && m_cur >= 0 && CanTarget(m_cur, i, m_phase == PH_P_TARGET ? -1 : m_cmdSkill))
			dp.flash = max(dp.flash, .25f + .2f * sinf(m_time * 7.f));
		ActorDraw(m_r, m_lib, m_act[i], ActorWorld(m_act[i], ActorPos(i), ActorFace(i)), dp, m_time);
		if (u.boss && u.alive) {
			DrawParams cp;
			cp.emissive = .6f;
			cp.outline = .008f;
			V3 hp = ActorPos(i) + v3(0, m_act[i].height + .22f + sinf(m_time * 2.f) * .04f, 0);
			m_r.DrawStatic(m_lib.crown, MMul(MMul(MScale(.9f, .9f, .9f), MRotY(m_time)), MTrans(hp.x, hp.y, hp.z)), cp);
		}
	}
	m_r.DrawParticles(m_parts.data(), m_parts.size());
}

void Game::DrawCutIn3D()
{
	Env e;
	ThemeEnv(m_b.map.theme, e);
	const SkillDef* sd = m_seqSkill >= 0 ? &SkillInfo(m_seqSkill) : NULL;
	DWORD ec = sd ? ElemColor(sd->element) : RGBA8(255, 240, 200);
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
		if (h > 1.32f) h = 1.32f;
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
	} else if (m_cutStage == 1 && m_seqSkill >= 0) {
		float u = Smooth01(m_cutStageT / 1.05f);
		V3 a = m_cutPosA;
		float h = m_act[m_cutA].height;
		if (h > 1.32f) h = 1.32f;
		look = a + v3(0, h * .6f, 0);
		eye = look + v3(Lerpf(1.6f, 1.1f, u), Lerpf(.1f, .25f, u), Lerpf(-1.2f, -.9f, u));
	} else if (m_cutStage == 2 || m_cutStage == 3) {
		float u = m_cutStage == 2 ? m_cutStageT : m_cutStageT + 1.1f;
		// 自キャラは -X。正方向に回すと敵が手前に来るので、反対へ回す。
		float ang = -0.35f - u * 0.85f;
		int spread = max(m_cutAllyN, m_cutFoeN);
		look = v3(0, .72f, 0);
		float dist = (m_cutFoeN <= 0 ? 2.6f : 3.35f) + max(0, spread - 1) * .55f;
		dist = Lerpf(dist + .45f, dist, Smooth01(min(1.f, u * 1.4f)));
		eye = v3(sinf(ang) * dist, 1.15f + sinf(u * 1.7f) * .06f, -cosf(ang) * dist);
		if (m_shake > .3f) eye = Lerp(eye, look, .1f);
	} else {
		int spread = max(m_cutAllyN, m_cutFoeN);
		look = v3(0, .55f, 0);
		float sw = sinf(t * .4f) * .35f;
		float dist = (m_cutFoeN <= 0 ? 2.8f : 3.8f) + max(0, spread - 1) * .75f;
		eye = v3(.3f + sw, 1.05f, -dist);
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
	M4 proj = MPerspLH(.75f, (float)m_w / max(1, m_h), .05f, 120.f);
	m_r.SetCamera(view, proj, eye, m_time);
	m_r.SetFog(10.f, 40.f, 0, e.skyBot);
	m_r.SetAmbient(e.ambSky, e.ambGnd, e.amb + .1f, e.hard);
	m_r.SetLight(Norm(v3(-.4f, .8f, -.45f)), v3(0, 0, 0), 5.f);
	int ids[12];
	int nId = 0;
	for (int i = 0; i < m_cutAllyN && nId < 12; i++) ids[nId++] = m_cutAlly[i];
	for (int i = 0; i < m_cutFoeN && nId < 12; i++) ids[nId++] = m_cutFoe[i];
	if (nId == 0 && m_cutA >= 0) ids[nId++] = m_cutA;
	auto cutWorld = [&](int id) {
		const Actor& ac = m_act[id];
		float sc = ac.scale;
		if (ac.height > 1.32f) sc *= 1.32f / ac.height;
		V3 p = ActorPos(id);
		return MMul(MMul(MScale(sc, sc, sc), MRotY(ActorFace(id) + PI)), MTrans(p.x, p.y, p.z));
	};
	for (int k = 0; k < nId; k++) if (ids[k] >= 0) ActorPose(m_act[ids[k]], m_lib, m_r);
	DrawParams gp;
	gp.tint = e.ground;
	gp.rim = 0;
	M4 gw = MMul(MScale(9.f, .02f, 9.f), MTrans(0, -.02f, 0));
	m_r.BeginShadowPass();
	for (int k = 0; k < nId; k++) {
		if (ids[k] < 0) continue;
		DrawParams dp;
		ActorDraw(m_r, m_lib, m_act[ids[k]], cutWorld(ids[k]), dp, m_time);
	}
	m_r.BeginMainPass();
	DWORD top = e.skyTop, bot = e.skyBot;
	if (sd) {
		top = RGBA8(((ec & 255) * 2) / 5, (((ec >> 8) & 255) * 2) / 5, (((ec >> 16) & 255) * 2) / 5);
		bot = RGBA8(((ec & 255) * 4) / 5 + 40, (((ec >> 8) & 255) * 4) / 5 + 40, (((ec >> 16) & 255) * 4) / 5 + 40);
	}
	if (m_cutKind == 2) {
		top = RGBA8(10, 24, 58);
		bot = RGBA8(36, 78, 130);
	} else if (m_cutKind == 1 && sd) {
		top = RGBA8((ec & 255) / 2, ((ec >> 8) & 255) / 2, ((ec >> 16) & 255) / 2);
		bot = RGBA8(min(255, (ec & 255) + 30), min(255, ((ec >> 8) & 255) + 24), min(255, ((ec >> 16) & 255) + 16));
	}
	int lines = (m_cutStage == 1 && m_cutKind != 0) ? 0 : 1;
	DWORD lineC = m_cutKind == 2 ? RGBA8(150, 210, 255) : (m_cutKind == 1 ? ec : RGBA8(255, 255, 255));
	m_r.DrawSky(top, bot, lines, lineC);
	m_r.DrawStatic(m_lib.orb, gw, gp);
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
	m_r.DrawParticles(m_parts.data(), m_parts.size());
}

// ============================================================ HUD（戦闘）
void Game::DrawUnitPanel(const Unit& u, float x, float y, float w, BOOL big)
{
	int ui = (int)(&u - m_b.u);
	float h = big ? U(148) : U(112);
	Panel(x, y, w, h);
	float ps = h - U(20);
	DWORD fc = u.team == TEAM_PLAYER ? RGBA8(110, 170, 255) : (u.boss ? RGBA8(255, 200, 60) : RGBA8(255, 100, 90));
		DrawPortrait(BattlePortrait(ui), x + U(10), y + U(10), ps, fc);
	float tx = x + ps + U(20), tw = w - ps - U(30);
	wchar_t nm[96];
	UnitTitle(u, nm, 64);
	if (!u.alive) {
		wchar_t ko[112];
		swprintf_s(ko, L"%s  %s", nm, TT(TX_RS_KO));
		wcscpy_s(nm, ko);
	}
	m_r.HudText(nm, tx, y + U(16), U(big ? 17.f : 15.f), u.alive ? RGBA8(255, 255, 255) : RGBA8(255, 176, 168), 0, 1);
	wchar_t l[128];
	if (u.team == TEAM_PLAYER) {
		int need = ExpToNext(u.ch.lv);
		int showExp = u.ch.lv >= TAC_LV_MAX ? 0 : u.ch.exp;
		int showNeed = u.ch.lv >= TAC_LV_MAX ? 0 : need;
		swprintf_s(l, L"%s%d  %s%s", TT(TX_MISC_LV), u.ch.lv, ClassName(u.ch.cls), UnitMoveType(u) == MV_FLY ? L"  ✈" : L"");
		float fs = U(12);
		if (m_r.TextWidth(l, fs, 0) > tw) fs = U(10);
		m_r.HudText(l, tx, y + U(34), fs, RGBA8(255, 226, 150), 0, 0);
		swprintf_s(l, L"%s %d/%d", TT(TX_MISC_EXP), showExp, showNeed);
		fs = U(12);
		if (m_r.TextWidth(l, fs, 0) > tw) fs = U(10);
		m_r.HudText(l, tx, y + U(48), fs, RGBA8(180, 220, 255), 0, 0);
	} else {
		swprintf_s(l, L"%s%d  %s%s", TT(TX_MISC_LV), u.ch.lv, ClassName(u.ch.cls), UnitMoveType(u) == MV_FLY ? L"  ✈" : L"");
		m_r.HudText(l, tx, y + U(36), U(13), RGBA8(255, 226, 150), 0, 0);
		wchar_t wk[96];
		MonsterWeakText(u.monster, wk, 96);
		float wfs = U(12);
		if (m_r.TextWidth(wk, wfs, 0) > tw && m_r.TextWidth(wk, wfs, 0) > 1.f) wfs = U(10);
		m_r.HudText(wk, tx, y + U(50), wfs, RGBA8(255, 186, 120), 0, 0);
	}
	float by = y + (u.team == TEAM_PLAYER ? U(64) : U(66));
	if (big) {
		// 左下の欄は数値だけ。バーはログ側へ食い込むので置かない。
		wchar_t hp[48], mp[48], cp[32];
		swprintf_s(hp, L"%s %d/%d", TT(TX_ST_HP), (int)(u.hpShow + .5f), u.ch.stat[S_HP]);
		mp[0] = 0;
		if (u.ch.stat[S_MP] > 0) swprintf_s(mp, L"%s %d/%d", TT(TX_ST_MP), u.mp, u.ch.stat[S_MP]);
		swprintf_s(cp, L"%s %d", TT(TX_CP), u.cp);
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
		swprintf_s(l, L"%s %d/%d", TT(TX_ST_HP), (int)(u.hpShow + .5f), u.ch.stat[S_HP]);
		float hpFs = U(12);
		if (m_r.TextWidth(l, hpFs, 0) > tw - bw - U(40)) hpFs = U(10);
		m_r.HudText(l, tx, by + U(2), hpFs, RGBA8(230, 230, 230), 0, 0);
		{
			wchar_t cp[32];
			swprintf_s(cp, L"%s %d", TT(TX_CP), u.cp);
			m_r.HudText(cp, tx + tw - bw - U(8), by + U(2), U(12), u.cp >= 100 ? RGBA8(255, 214, 80) : RGBA8(210, 170, 70), 2, 0);
		}
		float frac = u.hpShow / max(1, u.ch.stat[S_HP]);
		Bar(tx + tw - bw, by + U(4), bw, U(8), frac, frac > .5f ? RGBA8(120, 240, 120) : (frac > .25f ? RGBA8(240, 210, 80) : RGBA8(255, 90, 70)), RGBA8(40, 120, 50));
		by += U(20);
		if (u.ch.stat[S_MP] > 0) {
			swprintf_s(l, L"%s %d/%d", TT(TX_ST_MP), u.mp, u.ch.stat[S_MP]);
			m_r.HudText(l, tx, by + U(2), U(12), RGBA8(220, 220, 240), 0, 0);
			Bar(tx + tw - bw, by + U(4), bw, U(7), (float)u.mp / max(1, u.ch.stat[S_MP]), RGBA8(110, 170, 255), RGBA8(40, 70, 170));
		}
	}
	if (big) {
		by += U(18);
		swprintf_s(l, L"%s%d %s%d %s%d %s%d %s%d %s%d", TT(TX_ST_STR), UnitStat(u, S_STR), TT(TX_ST_MAG), UnitStat(u, S_MAG), TT(TX_ST_DEF), UnitStat(u, S_DEF),
			TT(TX_ST_RES), UnitStat(u, S_RES), TT(TX_ST_AGI), UnitStat(u, S_AGI), TT(TX_ST_MOV), UnitStat(u, S_MOV));
		float sfs = U(11);
		float sw = m_r.TextWidth(l, sfs, 0);
		if (sw > tw && sw > 1.f) sfs *= tw / sw;
		m_r.HudText(l, tx, by + U(6), sfs, RGBA8(210, 210, 220), 0, 0);
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
		if (u.buffDef > 0 || u.buffAgi > 0 || u.buffAtk > 0 || u.mulTurns > 0) { wcscat_s(st, TT(TX_BT_BUFF)); wcscat_s(st, L" "); }
		if (u.team == TEAM_ENEMY && !u.alert) { wcscat_s(st, TT(TX_BT_ASLEEP)); wcscat_s(st, L" "); }
		}
		m_r.HudText(st, tx, by + U(6), U(11), RGBA8(200, 230, 255), 0, 0);
	}
}

void Game::DrawTurnOrder()
{
	int order[12];
	const int acting = (m_turnOpen && m_cur >= 0 && m_b.u[m_cur].alive) ? m_cur : -1;
	int cost = 1000;
	if (acting >= 0) {
		if (m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE) cost = 1000;
		else if (m_phase == PH_P_CMD || m_phase == PH_P_SKILLS) cost = (m_focusId == ID_BT_WAIT || m_focusId == ID_BT_DOOR) ? 600 : 1000;
		else if (m_phase == PH_P_MOVE && m_cursorValid) {
			int t = m_b.UnitAt(m_cx, m_cz);
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
	float w = U(188), rh = U(40), gap = U(18);
	int arrows = insertAt >= 0 ? 1 : 0;
	float x = (float)m_w - w - U(14), y = U(14);
	float panelH = U(30) + rh * (n + (m_cur >= 0 ? 1 : 0)) + gap * arrows;
	m_orderBottom = y + panelH;
	Panel(x, y, w, panelH);
	m_r.HudText(TT(TX_BT_ORDER), x + w * .5f, y + U(16), U(14), RGBA8(255, 226, 150), 1, 1);
	if (m_ordLeave >= 0 && slide > 0.05f)
		m_r.HudText(L"▼", x + w - U(16), y + U(16), U(14), RGBA8(255, 140, 110), 2, 1);
	float ry = y + U(30);
	auto row = [&](int ui, float y0, float xoff, BOOL now, BOOL ghost, const wchar_t* arrow) {
		const Unit& u = m_b.u[ui];
		DWORD fc = u.team == TEAM_PLAYER ? RGBA8(110, 170, 255) : (u.boss ? RGBA8(255, 200, 60) : RGBA8(255, 100, 90));
		if (ghost) fc = RGBA8(255, 210, 90);
		if (now) m_r.HudRect(x + U(4), y0, w - U(8), rh - U(4), RGBA8(255, 220, 120, 60));
		if (ghost) m_r.HudRect(x + U(4), y0, w - U(8), rh - U(4), RGBA8(255, 190, 60, 50));
		DrawPortrait(BattlePortrait(ui), x + U(8) + xoff, y0 + U(2), rh - U(8), fc);
		wchar_t nm[64];
		UnitTitle(u, nm, 64);
		m_r.HudText(nm, x + rh + U(6) + xoff, y0 + rh * .32f, U(12), ghost ? RGBA8(255, 230, 150) : (now ? RGBA8(255, 240, 180) : RGBA8(240, 240, 240)), 0, now || ghost);
		if (ghost) m_r.HudText(TT(TX_BT_REJOIN), x + w - U(22), y0 + rh * .32f, U(11), RGBA8(255, 210, 90), 2, 1);
		if (arrow) m_r.HudText(arrow, x + w - U(8), y0 + rh * .28f, U(13), RGBA8(255, 210, 90), 2, 1);
		float frac = u.hpShow / max(1, u.ch.stat[S_HP]);
		Bar(x + rh + U(6), y0 + rh * .64f, w - rh - U(28), U(5), frac, fc, fc);
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

void Game::DrawMinimap(float x, float y, float s)
{
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
	if (m_r.MinimapSrv()) {
		float xy[8], uv[8];
		const float corn[4][2] = { { wx0, wz0 }, { wx1, wz0 }, { wx1, wz1 }, { wx0, wz1 } };
		const float usu[4] = { wx0 / (float)MAPN, wx1 / (float)MAPN, wx1 / (float)MAPN, wx0 / (float)MAPN };
		const float usv[4] = { wz0 / (float)MAPN, wz0 / (float)MAPN, wz1 / (float)MAPN, wz1 / (float)MAPN };
		for (int i = 0; i < 4; i++) {
			toScreen(corn[i][0], corn[i][1], xy[i * 2], xy[i * 2 + 1]);
			uv[i * 2] = usu[i];
			uv[i * 2 + 1] = usv[i];
		}
		m_r.HudQuad(m_r.MinimapSrv(), xy, uv, 0xFFFFFFFF);
	}
	m_r.HudFrame(x - U(6), y - U(6), s + U(12), s + U(12), U(2.5f), RGBA8(220, 190, 120));
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
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (!u.alive) continue;
		DWORD c = u.team == TEAM_PLAYER ? RGBA8(90, 190, 255) : (u.boss ? RGBA8(255, 220, 70) : (u.alert ? RGBA8(255, 70, 60) : RGBA8(140, 50, 50)));
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

void Game::DrawForecast()
{
	if (m_cur < 0) return;
	int t = m_b.UnitAt(m_cx, m_cz);
	int skill = m_phase == PH_P_SKILLTARGET ? m_cmdSkill : -1;
	if (!CanTarget(m_cur, t, skill)) return;
	if (skill >= 0) {
		const SkillDef& sd = SkillInfo(skill);
		if (sd.kind == SK_HEAL || sd.kind == SK_BUFF) return;
	}
	Forecast f;
	CalcForecast(m_b, m_cur, t, skill, f);
	const Unit& a = m_b.u[m_cur];
	const Unit& d = m_b.u[t];
	float w = min(U(520.f), (float)m_w - U(16));
	float h = min(U(176.f), max(U(120.f), (float)m_h * .38f));
	float x = (m_w - w) * .5f, y = U(8);
	Panel(x, y, w, h, .42f);
	m_r.HudText(TT(TX_BT_FORECAST), x + w * .5f, y + U(16), U(14), RGBA8(255, 226, 150), 1, 1);
	auto side = [&](const Unit& u, float cx, int dmg, int hit, int crit, int hits, BOOL can, int enemyHp) {
		wchar_t nm[64];
		UnitTitle(u, nm, 64);
		m_r.HudText(nm, cx, y + U(40), U(15), RGBA8(255, 255, 255), 1, 1);
		wchar_t l[64];
		int after = max(0, enemyHp - (can ? dmg * hits : 0));
		swprintf_s(l, L"%s %d", TT(TX_ST_HP), u.hp);
		m_r.HudText(l, cx, y + U(62), U(13), RGBA8(220, 220, 220), 1, 0);
		if (!can) { m_r.HudText(TT(TX_BT_NOCOUNTER), cx, y + U(96), U(14), RGBA8(170, 170, 180), 1, 0); return; }
		if (hits > 1) swprintf_s(l, L"%s %d ×%d", TT(TX_BT_DMG), dmg, hits);
		else swprintf_s(l, L"%s %d", TT(TX_BT_DMG), dmg);
		m_r.HudText(l, cx, y + U(84), U(15), RGBA8(255, 200, 160), 1, 1);
		swprintf_s(l, L"%s %d%%   %s %d%%", TT(TX_BT_HIT), hit, TT(TX_BT_CRIT), crit);
		m_r.HudText(l, cx, y + U(106), U(13), RGBA8(220, 230, 255), 1, 0);
		(void)after;
	};
	side(a, x + w * .25f, f.dmg, f.hit, f.crit, f.hits, TRUE, d.hp);
	side(d, x + w * .75f, f.cdmg, f.chit, f.ccrit, f.chits, f.canCounter, a.hp);
	m_r.HudText(L"VS", x + w * .5f, y + U(70), U(20), RGBA8(255, 226, 150), 1, 1);
	int after = max(0, d.hp - f.dmg * f.hits);
	wchar_t l[64];
	swprintf_s(l, L"%d → %d", d.hp, after);
	m_r.HudText(l, x + w * .75f, y + U(130), U(13), after == 0 ? RGBA8(255, 120, 100) : RGBA8(200, 200, 200), 1, 1);
	int aAfter = f.canCounter ? max(0, a.hp - f.cdmg * f.chits) : a.hp;
	swprintf_s(l, L"%d → %d", a.hp, aAfter);
	m_r.HudText(l, x + w * .25f, y + U(130), U(13), aAfter == 0 ? RGBA8(255, 120, 100) : RGBA8(200, 200, 200), 1, 1);
	if (a.team == TEAM_PLAYER && d.team == TEAM_ENEMY) {
		int pct = FoeMasteryPct(FoeKills(d.monster));
		if (pct > 0) {
			swprintf_s(l, L"%s +%d%%", FoeMasteryWord(), pct);
			m_r.HudText(l, x + w * .25f, y + U(150), U(12), RGBA8(255, 214, 120), 1, 0);
		}
	}
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
		return t.team == src.team && !t.alive && t.killed && dist >= 1;
	if (ItemHarms(g))
		return t.team != src.team && t.alive && dist >= 1;
	return t.team == src.team && t.alive;
}

void Game::UseConsumable(int good, int target)
{
	const GoodDef* g = GoodAt(good);
	if (!g || m_cur < 0) return;
	if (target < 0 || target >= m_b.nUnits) return;
	Unit& self = m_b.u[m_cur];
	if (!ItemThrowOk(g, self, m_b.u[target])) return;
	if (g->item == IT_RAISE || g->extra == 106) {
		const Unit& corpse = m_b.u[target];
		int s = UnitSpan(corpse);
		for (int dz = 0; dz < s; dz++) for (int dx = 0; dx < s; dx++)
			if (m_b.UnitAt(corpse.x + dx, corpse.z + dz) >= 0) return;
	}
	if (!GoodTake(m_save, good)) return;
	Unit* dst = &m_b.u[target];
	if (g->item == IT_ELIXIR) {
		dst->hp = min(dst->ch.stat[S_HP], dst->hp + max(1, dst->ch.stat[S_HP] / 2));
	} else if (g->item == IT_MIGHT) {
		dst->mulTurns = max(dst->mulTurns, 3);
	} else if (g->item == IT_DELAY) {
		if (!UnitHasPas(*dst, PAS_NOSLOW) && !UnitAilmentHeld(*dst)) dst->ct = max(0, dst->ct - 800);
	} else if (g->item == IT_RAISE) {
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
	} else if (g->extra == IX_FULL) {
		dst->hp = dst->ch.stat[S_HP];
	} else if (g->extra == IX_ATK) dst->buffAtk = max(dst->buffAtk, 3);
	else if (g->extra == IX_DEF) dst->buffDef = max(dst->buffDef, 3);
	else if (g->extra == IX_AGI) dst->buffAgi = max(dst->buffAgi, 3);
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
	}
	dst->flash = 1.f;
	AddPopup(v3(dst->px, dst->py + 1.f, dst->pz), TT(g->nameTx), RGBA8(140, 230, 170), 1);
	AddLog(TT(g->nameTx), RGBA8(140, 230, 170));
	if (g->item == IT_RAISE) TacSfx(TS_RAISE);
	else if (g->extra == IX_CURE || g->extra == 103 || g->extra == 109) TacSfx(TS_CLEANSE);
	else if (g->item == IT_ELIXIR || g->extra == IX_FULL || g->extra == 105) TacSfx(TS_HEAL);
	else TacSfx(TS_BUFF);
	m_b.RebuildOcc();
	m_itemAim = -1;
	FinishUnitTurn(1);
}

void Game::DrawBattleStatus()
{
	int who = m_statWho;
	if (who < 0 || who >= m_b.nUnits) who = m_cur;
	if (who < 0 || who >= m_b.nUnits) { m_statOpen = 0; m_statEq = 0; m_statWho = -1; return; }
	Unit& u = m_b.u[who];
	float pw = min(U(640.f), (float)m_w - U(40));
	float ph = min(U(520.f), (float)m_h - U(40));
	float px = ((float)m_w - pw) * .5f, py = ((float)m_h - ph) * .5f;
	m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 120));
	if (m_click && !Hit(px, py, pw, ph)) {
		m_click = FALSE;
		if (m_statEq) m_statEq = 0;
		else { m_statOpen = 0; m_statEq = 0; }
		return;
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
	if (m_statEq == 5 || m_statEq == 6) {
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
	int beadSlot = m_statEq == 3 ? 0 : m_statEq == 4 ? 1 : m_statEq == 7 ? 2 : m_statEq == 8 ? 3 : -1;
	if (m_statEq == 1 || m_statEq == 2 || beadSlot >= 0) {
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
	CharData view = u.ch;
	for (int s = 0; s < S_COUNT; s++) view.stat[s] = UnitStat(u, s);
	DrawStatLines(view, px + U(16), py + U(48), pw - U(32), NULL, u.hp, u.mp);
	float yy = py + ph - U(204);
	const GearDef* wpn = GearOf(u.ch.reserved[6]);
	const GearDef* arm = GearOf(u.ch.reserved[7]);
	wchar_t gl[96];
	swprintf_s(gl, L"%s: %s", TT(TX_GEAR_WPN), wpn ? GearName(u.ch.reserved[6]) : TT(TX_PT_NONE));
	if (Btn(ID_BT_EQ0, px + U(16), yy, pw * .48f, U(32), gl, TRUE, FALSE, U(12))) { m_statEq = 1; m_statEqPage = 0; }
	swprintf_s(gl, L"%s: %s", TT(TX_GEAR_ARM), arm ? GearName(u.ch.reserved[7]) : TT(TX_PT_NONE));
	if (Btn(ID_BT_EQ0 + 1, px + pw * .52f, yy, pw * .44f, U(32), gl, TRUE, FALSE, U(12))) { m_statEq = 2; m_statEqPage = 0; }
	yy += U(36);
	swprintf_s(gl, L"%s1: %s", TT(TX_CRAFT), EquippedBeadName(u.ch, 0));
	if (Btn(ID_BT_EQ0 + 2, px + U(16), yy, pw * .48f, U(32), gl, TRUE, FALSE, U(12))) { m_statEq = 3; m_statEqPage = 0; }
	swprintf_s(gl, L"%s2: %s", TT(TX_CRAFT), EquippedBeadName(u.ch, 1));
	if (Btn(ID_BT_EQ0 + 3, px + pw * .52f, yy, pw * .44f, U(32), gl, TRUE, FALSE, U(12))) { m_statEq = 4; m_statEqPage = 0; }
	yy += U(36);
	swprintf_s(gl, L"%s3: %s", TT(TX_CRAFT), EquippedBeadName(u.ch, 2));
	if (Btn(ID_BT_BEAD2, px + U(16), yy, pw * .48f, U(32), gl, TRUE, FALSE, U(12))) { m_statEq = 7; m_statEqPage = 0; }
	swprintf_s(gl, L"%s: %s", TT(TX_OUGI), EquippedBeadName(u.ch, 3));
	if (Btn(ID_BT_BEAD3, px + pw * .52f, yy, pw * .44f, U(32), gl, TRUE, FALSE, U(12))) { m_statEq = 8; m_statEqPage = 0; }
	yy += U(36);
	swprintf_s(gl, L"%s1: %s", TT(TX_PAS), EquippedPasName(u.ch, 0));
	if (Btn(ID_BT_PAS0, px + U(16), yy, pw * .48f, U(32), gl, TRUE, FALSE, U(12))) { m_statEq = 5; m_statEqPage = 0; }
	swprintf_s(gl, L"%s2: %s", TT(TX_PAS), EquippedPasName(u.ch, 1));
	if (Btn(ID_BT_PAS1, px + pw * .52f, yy, pw * .44f, U(32), gl, TRUE, FALSE, U(12))) { m_statEq = 6; m_statEqPage = 0; }
	if (Btn(ID_BT_BACK, px + pw - U(170), py + ph - U(46), U(150), U(32), TT(TX_CR_BACK))) { m_statOpen = 0; m_statEq = 0; }
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
	if (px < U(8)) px = U(8);
	if (py < U(8)) py = U(8);
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
	if (m_cur < 0) return;
	const Unit& u = m_b.u[m_cur];
	float w = U(220), bh = U(40);
	float sx, sy;
	float x = m_w * .62f, y = m_h * .36f;
	if (m_r.ProjectToScreen(v3(u.px, u.py + m_act[m_cur].height, u.pz), sx, sy)) {
		x = Clampf(sx + U(70), U(10), m_w - w - U(200));
		y = Clampf(sy - U(80), U(170), m_h - U(320));
	}
	if (m_phase == PH_P_ITEM) {
		float iw = U(280);
		float gap = U(6), margin = U(10);
		auto place = [&](int rows) {
			float ph = bh * rows + gap * max(0, rows - 1) + U(16);
			float topMin = margin + U(8);
			float topMax = (float)m_h - margin - ph + U(8);
			if (topMax < topMin) topMax = topMin;
			y = Clampf(y, topMin, topMax);
			float xMax = (float)m_w - iw - margin;
			if (xMax < margin) xMax = margin;
			x = Clampf(x, margin, xMax);
			Panel(x - U(8), y - U(8), iw + U(16), ph);
		};
		auto rowsThatFit = [&](int want, int chrome) {
			float avail = (float)m_h - margin * 2 - U(16);
			int fit = (int)((avail + gap) / (bh + gap));
			if (fit < chrome + 3) fit = chrome + 3;
			int per = fit - chrome;
			if (per > want) per = want;
			if (per < 1) per = 1;
			return per;
		};
		if (m_itemAim < 0) {
			int list[32], ln = 0;
			for (int i = 0; i < GoodCount() && ln < 32; i++) if (GoodHave(m_save, i) > 0) list[ln++] = i;
			int per = rowsThatFit(ln, 1);
			if (per > 18) per = 18;
			int pages = max(1, (ln + per - 1) / per);
			if (pages > 1) per = rowsThatFit(ln, 2);
			pages = max(1, (ln + max(1, per) - 1) / max(1, per));
			if (m_itemPage >= pages) m_itemPage = pages - 1;
			if (m_itemPage < 0) m_itemPage = 0;
			int a = m_itemPage * per, b = min(ln, a + per);
			int shown = b - a;
			place(shown + 1 + (pages > 1 ? 1 : 0));
			float yy = y;
			for (int k = a; k < b; k++) {
				const GoodDef* gd = GoodAt(list[k]);
				wchar_t lb[80];
				swprintf_s(lb, L"%s  x%d", TT(gd->nameTx), GoodHave(m_save, list[k]));
				if (Btn(ID_BT_IT0 + (k - a), x, yy, iw, bh, lb, TRUE, FALSE, U(14))) {
					m_itemAim = list[k]; m_itemPage = 0;
					return;
				}
				yy += bh + gap;
			}
			if (pages > 1) {
				wchar_t pg[32];
				swprintf_s(pg, L"%d / %d", m_itemPage + 1, pages);
				if (Btn(ID_BT_SKPREV, x, yy, U(64), bh, L"◀")) m_itemPage = (m_itemPage + pages - 1) % pages;
				m_r.HudText(pg, x + iw * .5f, yy + bh * .5f, U(14), RGBA8(230, 230, 240), 1, 1);
				if (Btn(ID_BT_SKNEXT, x + iw - U(64), yy, U(64), bh, L"▶")) m_itemPage = (m_itemPage + 1) % pages;
				yy += bh + gap;
			}
			if (Btn(ID_BT_BACK, x, yy, iw, bh, TT(TX_CR_BACK))) BattleCancel();
			return;
		}
		const GoodDef* gd = GoodAt(m_itemAim);
		int tg[64], tn = 0;
		for (int i = 0; i < m_b.nUnits && tn < 64; i++) {
			if (gd && ItemThrowOk(gd, u, m_b.u[i])) {
				if (gd->item == IT_RAISE) {
					const Unit& corpse = m_b.u[i];
					int s = UnitSpan(corpse);
					BOOL blocked = FALSE;
					for (int dz = 0; dz < s && !blocked; dz++) for (int dx = 0; dx < s; dx++)
						if (m_b.UnitAt(corpse.x + dx, corpse.z + dz) >= 0) blocked = TRUE;
					if (blocked) continue;
				}
				tg[tn++] = i;
			}
		}
		iw = min(U(460.f), (float)m_w - U(24));
		float rowH = U(58);
		auto rowsFitTall = [&](int want, int chrome) {
			float avail = (float)m_h - margin * 2 - U(16);
			int fit = (int)((avail + gap) / (rowH + gap));
			if (fit < chrome + 1) fit = chrome + 1;
			int nfit = fit - chrome;
			if (nfit > want) nfit = want;
			if (nfit < 1) nfit = 1;
			if (nfit > 16) nfit = 16;
			return nfit;
		};
		int per = rowsFitTall(max(1, tn), 2);
		int pages = max(1, (max(1, tn) + per - 1) / per);
		if (pages > 1) per = rowsFitTall(tn, 3);
		pages = tn < 1 ? 1 : max(1, (tn + max(1, per) - 1) / max(1, per));
		if (m_itemPage >= pages) m_itemPage = pages - 1;
		if (m_itemPage < 0) m_itemPage = 0;
		int a = m_itemPage * per, b = min(tn, a + per);
		int shown = max(0, b - a);
		{
			float ph = rowH * (shown + 2 + (pages > 1 ? 1 : 0) + (tn < 1 ? 1 : 0)) + gap * max(0, shown) + U(16);
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
		float yy = y + rowH;
		for (int k = a; k < b; k++) {
			const Unit& tgU = m_b.u[tg[k]];
			wchar_t nm[64], sub[80];
			UnitTitle(tgU, nm, 64);
			int mx = max(1, tgU.ch.stat[S_HP]);
			swprintf_s(sub, L"%s  %s%d  HP %d/%d", ClassName(tgU.ch.cls), TT(TX_MISC_LV), tgU.ch.lv, tgU.hp, mx);
			if (Btn(ID_BT_ITT0 + (k - a), x, yy, iw, rowH, L"", TRUE, FALSE, U(14))) { UseConsumable(m_itemAim, tg[k]); return; }
			float ps = rowH - U(10);
			DrawPortrait(BattlePortrait(tg[k]), x + U(4), yy + U(5), ps, tgU.team == TEAM_PLAYER ? RGBA8(120, 170, 255) : RGBA8(255, 120, 100));
			m_r.HudText(nm, x + ps + U(14), yy + U(16), U(15), RGBA8(248, 244, 236), 0, 1);
			m_r.HudText(sub, x + ps + U(14), yy + U(36), U(12), RGBA8(196, 206, 224), 0, 0);
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
			swprintf_s(pg, L"%d / %d", m_itemPage + 1, pages);
			if (Btn(ID_BT_SKPREV, x, yy, U(64), bh, L"◀")) m_itemPage = (m_itemPage + pages - 1) % pages;
			m_r.HudText(pg, x + iw * .5f, yy + bh * .5f, U(14), RGBA8(230, 230, 240), 1, 1);
			if (Btn(ID_BT_SKNEXT, x + iw - U(64), yy, U(64), bh, L"▶")) m_itemPage = (m_itemPage + 1) % pages;
			yy += bh + gap;
		}
		if (Btn(ID_BT_BACK, x, yy, iw, bh, TT(TX_CR_BACK))) { m_itemAim = -1; m_itemPage = 0; return; }
		return;
	}
	if (m_phase == PH_P_CMD) {
		int dx = 0, dz = 0;
		BOOL canShove = FALSE;
		for (int i = 0; i < m_b.nUnits; i++) if (CanShove(m_b, m_cur, i, &dx, &dz)) canShove = TRUE;
		BOOL canDoor = CanOpenDoor(u) && AdjDoor(m_b, m_cur, NULL, NULL) > 0;
		BOOL canClimb = FALSE;
		{
			static const int cdx[4] = { 1, -1, 0, 0 }, cdz[4] = { 0, 0, 1, -1 };
			for (int k = 0; k < 4 && !canClimb; k++)
				if (CanTerrace(m_b, m_cur, u.x + cdx[k], u.z + cdz[k])) canClimb = TRUE;
		}
		int haveItem = ConsumableCount(m_save);
		int n = (m_moved ? 4 : 3) + (canShove ? 1 : 0) + (canDoor ? 1 : 0) + (canClimb ? 1 : 0) + (haveItem > 0 ? 1 : 0) + 1;
		if (m_statOpen) { DrawBattleStatus(); return; }
		float cw = min(U(320), (float)m_w - U(28));
		float gapc = U(6);
		float extra = U(88);
		float avail = (float)m_h - y - U(16);
		float need = bh * n + gapc * max(0, n - 1) + extra;
		if (need > avail && n > 0) {
			bh = max(U(22.f), (avail - extra - gapc * max(0, n - 1)) / n);
			need = bh * n + gapc * max(0, n - 1) + extra;
		}
		if (y + need > (float)m_h - U(8)) y = max(U(8), (float)m_h - U(8) - need);
		x = min(x, max(U(8), m_w - cw - U(16)));
		Panel(x - U(8), y - U(8), cw + U(16), bh * n + gapc * max(0, n - 1) + extra, .55f);
		BOOL hasSkill = FALSE;
		for (int s = 0; s < SKL_COUNT; s++) if (UnitHasSkill(u, s) && SkillInfo(s).kind != SK_PASSIVE) hasSkill = TRUE;
		BOOL hasCraft = FALSE;
		for (int i = 0; i < CRAFT_SLOT_N; i++) if (u.craftEq[i] >= CRAFT_BASE) hasCraft = TRUE;
		float yy = y;
		int desc = -1;
		auto mark = [&](int id, float by, int tx) {
			if ((m_navMode && m_focusId == id) || (!m_navMode && m_mouseSeen && Hit(x, by, cw, bh))) desc = tx;
		};
		mark(ID_BT_ATTACK, yy, TX_BTD_ATTACK);
		if (Btn(ID_BT_ATTACK, x, yy, cw, bh, TT(TX_BT_ATTACK), HasAttackTarget(m_cur))) {
			m_phase = PH_P_TARGET; m_target = -1; ComputeOverlays(); m_navMode = FALSE;
			for (int i = 0; i < m_b.nUnits; i++) if (CanTarget(m_cur, i, -1)) { m_cx = m_b.u[i].x; m_cz = m_b.u[i].z; m_cursorValid = TRUE; break; }
		}
		yy += bh + U(6);
		mark(ID_BT_SKILL, yy, TX_BTD_SKILL);
		if (Btn(ID_BT_SKILL, x, yy, cw, bh, TT(TX_BT_SKILL), hasSkill || hasCraft)) {
			int ri = u.rosterIdx;
			if (ri >= 0 && ri < MAX_ROSTER) { m_skillTab = m_skMemoTab[ri]; m_skillPage = m_skMemoPage[ri]; }
			if (hasCraft && !hasSkill) m_skillTab = 2;
			else if (m_skillTab == 2 && !hasCraft) m_skillTab = 0;
			if (m_skillTab < 0 || m_skillTab > 3) m_skillTab = 0;
			m_phase = PH_P_SKILLS; m_navMode = TRUE; m_focusId = ID_BT_SKTAB + m_skillTab;
		}
		yy += bh + U(6);
		mark(ID_BT_STAT, yy, TX_BTD_STATUS);
		if (Btn(ID_BT_STAT, x, yy, cw, bh, TT(TX_BT_STATUS))) { m_statWho = m_cur; m_statOpen = 1; m_navMode = TRUE; m_focusId = ID_BT_BACK; return; }
		yy += bh + U(6);
		mark(ID_BT_WAIT, yy, TX_BTD_WAIT);
		if (Btn(ID_BT_WAIT, x, yy, cw, bh, TT(TX_BT_WAIT))) { FinishUnitTurn(0); return; }
		yy += bh + U(6);
		{
			int have = ConsumableCount(m_save);
			wchar_t lb[64];
			swprintf_s(lb, L"%s %d", TT(TX_BT_ITEM), have);
			if (have > 0) mark(ID_BT_ITEM, yy, TX_BTD_ITEM);
			if (have > 0 && Btn(ID_BT_ITEM, x, yy, cw, bh, lb)) {
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
		if (canDoor) mark(ID_BT_DOOR, yy, TX_BTD_DOOR);
		if (canDoor) m_btnSfx = -2;
		if (canDoor && Btn(ID_BT_DOOR, x, yy, cw, bh, TT(TX_BT_DOOR))) {
			int ox, oz;
			if (AdjDoor(m_b, m_cur, &ox, &oz) > 0) {
				OpenDoor(m_b, ox, oz);
				AddPopup(CellCenter(ox, oz) + v3(0, .6f, 0), TT(TX_BT_DOOR), RGBA8(220, 200, 140), 1);
				TacSfx(TS_DOOR);
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
		if (m_moved && Btn(ID_BT_UNDO, x, yy, cw, bh, TT(TX_BT_UNDO))) BattleCancel();
		float boxTop = (m_moved ? yy + bh : yy - U(6)) + U(8);
		if (desc >= 0) m_r.HudTextBox(TT(desc), x, boxTop, cw, U(62), U(13), RGBA8(230, 230, 236));
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
				list[n++] = s;
			}
		}
		float gap = U(4);
		float tabH = max(U(26.f), min(U(32.f), bh));
		float sw = min(U(460.f), max(U(220.f), (float)m_w - U(24)));
		int per = 8;
		int fit = (int)(((float)m_h - U(150) - tabH) / (bh + gap));
		if (fit < 3) { bh = max(U(22.f), ((float)m_h - U(150) - tabH) / 4.f - gap); fit = 4; }
		if (fit < per) per = max(3, fit);
		int pages = max(1, (n + per - 1) / per);
		if (m_skillPage < 0) m_skillPage = 0;
		if (m_skillPage >= pages) m_skillPage = pages - 1;
		int a = m_skillPage * per, b = min(n, a + per);
		int shown = b - a;
		float rows = (float)(shown + nc + 1 + (pages > 1 ? 1 : 0));
		float ph = tabH + gap + (bh + gap) * rows + U(78);
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
			BOOL ok = SkillUsable(m_b, m_cur, s);
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
			BOOL ok = SkillUsable(m_b, m_cur, s);
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
		if (Btn(ID_BT_BACK, x, yy, sw, bh, TT(TX_CR_BACK))) BattleCancel();
		yy += bh + U(6);
		if (hovCraft >= 0) {
			const SkillDef& sd = SkillInfo(hovCraft);
			wchar_t l[96];
			int r0 = sd.rMax < 0 ? UnitRangeMin(u) : sd.rMin;
			int r1 = sd.rMax < 0 ? UnitRangeMax(u) : sd.rMax;
			wchar_t cp[24];
			CraftCostText(hovCraft - CRAFT_BASE, cp, 24);
			swprintf_s(l, L"%s %d-%d  %s  %s", TT(TX_MISC_RANGE), r0, r1, CraftShapeName(sd.area, sd.shape), cp);
			m_r.HudText(l, x, yy + U(8), U(12), RGBA8(255, 210, 120), 0, 0);
			m_r.HudTextBox(CraftDesc(hovCraft), x, yy + U(18), sw, U(64), U(12), RGBA8(230, 230, 236));
		} else if (hov >= 0) {
			const SkillDef& sd = SkillInfo(hov);
			wchar_t l[96];
			const wchar_t* lack = u.mp < sd.mp ? TT(TX_BT_NOMP) : (IsFormSkill(hov) && !SkillUsable(m_b, m_cur, hov) ? TT(TX_FORM_NEED) : L"");
			swprintf_s(l, L"%s %d-%d  %s%d  %s", TT(TX_MISC_RANGE), sd.rMax < 0 ? UnitRangeMin(u) : sd.rMin, sd.rMax < 0 ? UnitRangeMax(u) : sd.rMax, TT(TX_MISC_AREA), sd.area, lack);
			m_r.HudText(l, x, yy + U(8), U(12), RGBA8(255, 226, 150), 0, 0);
			wchar_t attr[120], desc[420];
			SkillAttrText(sd, attr, 120);
			if (attr[0]) swprintf_s(desc, L"%s  %s", TT(sd.descTx), attr);
			else wcscpy_s(desc, TT(sd.descTx));
			m_r.HudTextBox(desc, x, yy + U(18), sw, U(48), U(12), RGBA8(230, 230, 236));
			if (IsFormSkill(hov)) DrawFormGuide(hov, u.x, u.z, x - U(8), y - U(8), sw + U(16), ph);
		}
		keepSkill();
	}
}

void Game::DrawPromoteOffer()
{
	if (m_lvqCur < 0 || m_lvqCur >= m_lvqN) return;
	const LevelUpInfo& L = m_lvq[m_lvqCur];
	if (L.unit < 0 || L.unit >= m_b.nUnits) return;
	Unit& u = m_b.u[L.unit];
	EvoOption opt[4];
	int no = EvoOptions(u.ch.cls, opt);
	float w = min(U(560.f), (float)m_w - U(24));
	float row = U(78);
	float h = U(108) + no * row + U(56);
	if (h > (float)m_h - U(16)) { h = (float)m_h - U(16); row = max(U(52.f), (h - U(164)) / max(1, no)); }
	float x = (m_w - w) * .5f, y = max(U(8), (m_h - h) * .5f);
	Panel(x, y, w, h, .92f);
	m_r.HudText(TT(TX_EV_JOB), x + w * .5f, y + U(28), U(26), RGBA8(255, 226, 150), 1, 1);
	wchar_t who[96];
	swprintf_s(who, L"%s  %s%d", u.ch.name, TT(TX_MISC_LV), u.ch.lv);
	m_r.HudText(who, x + w * .5f, y + U(54), U(15), RGBA8(230, 230, 240), 1, 0);
	float ty = y + U(78);
	for (int k = 0; k < no; k++) {
		const ClassDef& cd = ClassInfo(opt[k].target);
		if (Btn(ID_BT_PROMO0 + k, x + U(16), ty, w - U(32), U(34), ClassName(opt[k].target), TRUE, FALSE, U(16))) {
			int oldHp = u.ch.stat[S_HP], oldMp = u.ch.stat[S_MP];
			EvoApply(u.ch, opt[k].target, NULL, 0);
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
			AddBanner(TT(TX_EV_DONE), ClassName(u.ch.cls), RGBA8(255, 226, 120), 1, 2.8f);
			TacSfx(TS_PROMOTE);
			AdvanceLevelUp();
			return;
		}
		m_r.HudTextBox(TT(cd.descTx), x + U(20), ty + U(34), w - U(40), max(U(18.f), row - U(40)), U(12), RGBA8(210, 220, 235));
		ty += row;
	}
	if (Btn(ID_BT_PROMO_LATER, x + w * .5f - U(110), y + h - U(50), U(220), U(36), TT(TX_EV_LATER))) AdvanceLevelUp();
}

void Game::DrawLevelUp()
{
	if (m_promoAsk) { DrawPromoteOffer(); return; }
	if (m_lvqCur >= m_lvqN) return;
	float shown = m_lvT;
	if (m_lvqCur == 0) {
		if (m_lvT < 2.6f) return;
		shown = m_lvT - 2.6f;
	}
	const LevelUpInfo& L = m_lvq[m_lvqCur];
	const Unit& u = m_b.u[L.unit];
	float w = min(U(440.f), (float)m_w - U(20));
	float h = min(U(320) + L.nLearned * U(22), (float)m_h - U(20));
	float x = (m_w - w) * .5f, y = max(U(8), (m_h - h) * .5f);
	float in = Smooth01(shown / .25f);
	y += (1.f - in) * U(40);
	Panel(x, y, w, h, in);
	m_r.HudText(TT(TX_BT_LEVELUP), x + w * .5f, y + U(30), U(28), RGBA8(255, 230, 100), 1, 1, 1, in);
	DrawPortrait(BattlePortrait(L.unit), x + U(18), y + U(56), U(80), RGBA8(110, 170, 255));
	m_r.HudText(u.ch.name, x + U(112), y + U(72), U(18), RGBA8(255, 255, 255), 0, 1);
	wchar_t l[64];
	swprintf_s(l, L"%s %d → %d", TT(TX_MISC_LV), L.oldLv, L.newLv);
	m_r.HudText(l, x + U(112), y + U(100), U(16), RGBA8(140, 255, 150), 0, 1);
	float statH = DrawStatLines(u.ch, x + U(20), y + U(146), w - U(40), L.gains, -1, -1);
	float yy = y + U(146) + statH + U(6);
	for (int k = 0; k < L.nLearned; k++) {
		swprintf_s(l, L"%s: %s", TT(TX_BT_LEARNED), SkillLabel(L.learned[k]));
		m_r.HudText(l, x + w * .5f, yy + U(10), U(15), RGBA8(255, 226, 150), 1, 1);
		yy += U(22);
	}
	if (Btn(ID_BT_LVOK, x + w * .5f - U(60), y + h - U(46), U(120), U(34), L"OK")) AdvanceLevelUp();
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
		auto hpBox = [&](int ui, float x, int align, float y) {
			if (ui < 0) return;
			const Unit& u = m_b.u[ui];
			float w = U(280);
			float bx = align ? x - w : x;
			wchar_t nm[64];
			UnitTitle(u, nm, 64);
			DWORD fc = u.team == TEAM_PLAYER ? RGBA8(110, 170, 255) : RGBA8(255, 110, 100);
			m_r.HudText(nm, align ? bx + w : bx, y, U(14), fc, align ? 2 : 0, 1);
			float frac = u.hpShow / max(1, u.ch.stat[S_HP]);
			Bar(bx, y + U(14), w, U(8), frac, frac > .5f ? RGBA8(120, 240, 120) : (frac > .25f ? RGBA8(240, 210, 80) : RGBA8(255, 90, 70)), RGBA8(40, 120, 50));
		};
		auto col = [&](const int* id, int n, int align) {
			if (n < 1) return;
			float row = U(n > 3 ? 16.f : 22.f);
			float y0 = (float)m_h - bar - U(6) - n * row;
			for (int i = 0; i < n; i++)
				hpBox(id[i], align ? (float)m_w - U(16) : U(16), align, y0 + i * row);
		};
		col(m_cutAlly, m_cutAllyN, 0);
		col(m_cutFoe, m_cutFoeN, 1);
		if (m_seqSkill >= 0) {
			const SkillDef& sd = SkillInfo(m_seqSkill);
			DWORD ec = m_cutKind == 2 ? RGBA8(150, 214, 255) : ElemColor(sd.element);
			float hold = m_cutKind == 1 ? (IsCraftSuper(m_seqSkill) ? 1.85f : 1.15f) : (m_cutKind == 2 ? 1.35f : .8f);
			float u = m_cutStage == 1 ? Smooth01(m_cutStageT / .25f) : 1.f;
			float out = m_cutStage == 1 ? 1.f - Smooth01((m_cutStageT - hold) / .3f) : 0.f;
			float a = u * (m_cutStage == 1 ? out : 0.f);
			if (m_cutStage == 2) a = 0;
			if (m_cutStage == 1) {
				BOOL ougi = IsCraftSuper(m_seqSkill);
				BOOL craft = m_cutKind == 1;
				BOOL form = m_cutKind == 2;
				const wchar_t* title = craft ? CraftName(m_seqSkill) : SkillLabel(m_seqSkill);
				float by = m_h * (ougi || form ? .32f : (craft || m_ult ? .4f : .6f));
				float bh2 = ougi ? U(168) : (form ? U(150) : (craft ? U(132) : (m_ult ? U(120) : U(84))));
				float slide = (1.f - u) * m_w;
				m_r.HudRectGrad(-slide, by, (float)m_w, bh2, RGBA8(0, 0, 0, (int)(210 * a)), RGBA8((ec & 255) / 3, ((ec >> 8) & 255) / 3, ((ec >> 16) & 255) / 3, (int)(230 * a)));
				float edgeH = (ougi || form || craft) ? U(7) : U(3);
				m_r.HudRect(-slide, by, (float)m_w, edgeH, RGBA8(ec & 255, (ec >> 8) & 255, (ec >> 16) & 255, (int)(255 * a)));
				m_r.HudRect(slide, by + bh2 - edgeH, (float)m_w, edgeH, RGBA8(255, 220, 120, (int)(255 * a)));
				if (craft || form) {
					float pulse = .5f + .5f * sinf(m_time * 6.f);
					m_r.HudRect(m_w * .08f - slide, by + U(10), U(8), bh2 - U(20), RGBA8(255, 230, 160, (int)(180 * a * pulse)));
					m_r.HudRect(m_w * .92f + slide, by + U(10), U(8), bh2 - U(20), RGBA8(255, 230, 160, (int)(180 * a * pulse)));
				}
				m_r.HudText(title, m_w * .5f, by + bh2 * .48f, ougi ? U(72) : (form ? U(64) : (craft ? U(58) : (m_ult ? U(64) : U(44)))), ec, 1, 1, 1, a);
				const wchar_t* kind = ougi ? TT(TX_OUGI) : (craft ? TT(TX_CRAFT) : (form ? TT(TX_SK_TAB_FORM) : (m_ult ? TT(TX_BT_ULT) : NULL)));
				if (kind) m_r.HudText(kind, m_w * .5f, by + U(22), form || ougi ? U(24) : U(18), form ? RGBA8(255, 220, 130) : RGBA8(255, 226, 150), 1, 1, 1, a);
				wchar_t nm[64];
				UnitTitle(m_b.u[m_cutA], nm, 64);
				m_r.HudText(nm, m_w * .5f, by + bh2 * .82f, U(16), RGBA8(255, 255, 255), 1, 0, 1, a);
			}
			if (m_cutStage == 2) {
				const wchar_t* title = m_cutKind == 1 ? CraftName(m_seqSkill) : SkillLabel(m_seqSkill);
				const wchar_t* kind = m_cutKind == 1 ? (IsCraftSuper(m_seqSkill) ? TT(TX_OUGI) : TT(TX_CRAFT)) : (m_cutKind == 2 ? TT(TX_SK_TAB_FORM) : NULL);
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
}

void Game::DrawBattleUi()
{
	if (m_stashFull) {
		m_shopFit = 5;
		DrawShopFit();
		if (m_shopFit != 5) m_stashFull = 0;
		m_click = FALSE;
		return;
	}
	if (m_slotUi) { DrawSlotUi(); return; }
	if (m_seqCut && m_cutStage >= 1 && m_cutStage <= 3) {
		DrawCutInUi();
		return;
	}
	// 左上: ステージ情報
	{
		float w = U(250), h = U(58);
		Panel(U(14), U(14), w, h);
		wchar_t l[96];
		swprintf_s(l, L"%s %d", TT(TX_BT_STAGE), m_battleStage);
		m_r.HudText(l, U(28), U(32), U(17), RGBA8(255, 226, 150), 0, 1);
		m_r.HudText(TT(ThemeNameTx(m_b.map.theme)), U(14) + w - U(14), U(32), U(13), RGBA8(220, 220, 230), 2, 0);
		swprintf_s(l, L"%s %d   %s %d / %s %d   %s %d", TT(TX_BT_ENDTURN), m_b.turnCount + 1, TT(TX_BT_ALLY), m_b.AliveCount(TEAM_PLAYER), TT(TX_BT_ENEMY), m_b.AliveCount(TEAM_ENEMY), TT(TX_GOLD), m_save.gold);
		m_r.HudText(l, U(28), U(54), U(12), RGBA8(210, 210, 220), 0, 0);
		float mx = U(14) + w + U(8);
		BOOL turn = m_phase == PH_P_MOVE || m_phase == PH_P_CMD;
		if (!m_statOpen && Btn(ID_BT_MENU, mx, U(14), U(76), U(30), TT(TX_BT_MENU), turn, FALSE, U(13))) {
			m_phaseBeforeMenu = m_phase; m_phase = PH_MENU; m_menuConfirm = 0; m_focusId = ID_BT_M_RESUME;
		}
		if (!m_statOpen && m_cur >= 0 && turn && Btn(ID_BT_LOOK, mx + U(84), U(14), U(90), U(30), TT(TX_BT_STATUS), TRUE, FALSE, U(13))) {
			m_statWho = m_cur; m_statOpen = 1; m_navMode = TRUE; m_focusId = ID_BT_BACK;
		}
	}
	float partyRight = U(18);
	float partyBottom = U(84);
	{
		int nP = 0;
		for (int i = 0; i < m_b.nUnits; i++) if (m_b.u[i].team == TEAM_PLAYER) nP++;
		float ay0 = U(84);
		float avail = (float)m_h - U(186) - ay0;
		float row = U(22);
		if (nP > 0 && nP * row > avail) row = max(12.f, avail / nP);
		float fs = min(U(12.f), row * .5f);
		float nameW = U(36), lvW = U(28), clsW = U(36), fracW = U(44);
		const wchar_t* expLab = TT(TX_MISC_EXP);
		float expLabW = 0, gap = U(14), bw = U(56);
		float xName = U(18), xLv = 0, xCls = 0, xExp = 0, xFracR = 0, xBar = 0;
		float limit = (float)m_w - U(214);
		for (int fit = 0; fit < 4; fit++) {
			nameW = U(36); lvW = U(28); clsW = U(36); fracW = U(44);
			expLabW = m_r.TextWidth(expLab, fs, 0);
			for (int i = 0; i < m_b.nUnits; i++) {
				const Unit& u = m_b.u[i];
				if (u.team != TEAM_PLAYER) continue;
				wchar_t nm[48], lv[24], fr[32];
				UnitTitle(u, nm, 48);
				swprintf_s(lv, L"%s%d", TT(TX_MISC_LV), u.ch.lv);
				int need = ExpToNext(u.ch.lv);
				int showExp = u.ch.lv >= TAC_LV_MAX ? 0 : u.ch.exp;
				int showNeed = u.ch.lv >= TAC_LV_MAX ? 0 : need;
				swprintf_s(fr, L"%d/%d", showExp, showNeed);
				nameW = max(nameW, m_r.TextWidth(nm, fs, 0));
				lvW = max(lvW, m_r.TextWidth(lv, fs, 0));
				clsW = max(clsW, m_r.TextWidth(ClassName(u.ch.cls), fs, 0));
				fracW = max(fracW, m_r.TextWidth(fr, fs, 0));
			}
			xLv = xName + nameW + gap;
			xCls = xLv + lvW + gap;
			xExp = xCls + clsW + gap;
			xFracR = xExp + expLabW + U(8) + fracW;
			xBar = xFracR + gap;
			partyRight = xBar + bw;
			if (partyRight <= limit || fs <= U(9)) break;
			fs *= .82f;
		}
		float ay = ay0;
		for (int i = 0; i < m_b.nUnits; i++) {
			const Unit& u = m_b.u[i];
			if (u.team != TEAM_PLAYER) continue;
			wchar_t nm[48], lv[24], fr[32];
			UnitTitle(u, nm, 48);
			swprintf_s(lv, L"%s%d", TT(TX_MISC_LV), u.ch.lv);
			int need = ExpToNext(u.ch.lv);
			int showExp = u.ch.lv >= TAC_LV_MAX ? 0 : u.ch.exp;
			int showNeed = u.ch.lv >= TAC_LV_MAX ? 0 : need;
			swprintf_s(fr, L"%d/%d", showExp, showNeed);
			float ty = ay + row * .45f;
			BOOL rowHit = Hit(xName - U(8), ay, max(U(48.f), partyRight - xName + U(12)), row);
			if (rowHit) m_r.HudRect(xName - U(8), ay, max(U(48.f), partyRight - xName + U(12)), max(1.f, row - 1.f), u.alive ? RGBA8(255, 255, 255, 28) : RGBA8(255, 160, 140, 46));
			if (!m_statOpen && m_click && rowHit && m_phase != PH_ACTION && m_phase != PH_MOVING && m_phase != PH_E_MOVE && m_phase != PH_INTRO && m_phase != PH_OVER && m_phase != PH_LEVELUP) {
				m_statWho = i;
				m_statEq = 0;
				m_statOpen = 1;
				m_navMode = TRUE;
				m_focusId = ID_BT_BACK;
				m_click = FALSE;
			}
			DWORD nc = u.alive ? RGBA8(230, 240, 255) : RGBA8(160, 160, 170);
			DWORD ic = u.alive ? RGBA8(255, 226, 150) : RGBA8(150, 140, 110);
			m_r.HudText(nm, xName, ty, fs, nc, 0, 0);
			m_r.HudText(lv, xLv + lvW, ty, fs, ic, 2, 0);
			m_r.HudText(ClassName(u.ch.cls), xCls, ty, fs, ic, 0, 0);
			m_r.HudText(expLab, xExp, ty, fs, ic, 0, 0);
			m_r.HudText(fr, xFracR, ty, fs, ic, 2, 0);
			float frac = u.hpShow / max(1, u.ch.stat[S_HP]);
			Bar(xBar, ay + 1.f, bw, max(4.f, row * .36f), u.alive ? frac : 0, u.alive ? RGBA8(90, 210, 120) : RGBA8(90, 90, 100), RGBA8(20, 24, 32));
			if (row >= 16.f && u.ch.stat[S_MP] > 0) {
				float mf = (float)u.mp / max(1, u.ch.stat[S_MP]);
				Bar(xBar, ay + row * .55f, bw, max(3.f, row * .22f), u.alive ? mf : 0, RGBA8(90, 150, 255), RGBA8(20, 28, 48));
			}
			ay += row;
		}
		partyBottom = ay;
	}
	for (int i = 0; i < m_b.nUnits; i++) {
		const Unit& u = m_b.u[i];
		if (u.team == TEAM_ENEMY && (!u.alive || u.dissolve >= 1.f || !u.alert)) continue;
		float dx = u.px - m_camTarget.x, dz = u.pz - m_camTarget.z;
		if (u.team != TEAM_PLAYER && dx * dx + dz * dz > 28.f * 28.f) continue;
		float sx, sy;
		if (!m_r.ProjectToScreen(v3(u.px, u.py + m_act[i].height + .12f, u.pz), sx, sy)) continue;
		float bw = U(36);
		float frac = u.hpShow / max(1, u.ch.stat[S_HP]);
		DWORD col = !u.alive ? RGBA8(150, 150, 160) : (u.team == TEAM_PLAYER ? RGBA8(80, 220, 120) : (u.boss ? RGBA8(255, 200, 70) : RGBA8(240, 70, 60)));
		m_r.HudRect(sx - bw * .5f - 1, sy - 1, bw + 2, U(6), RGBA8(8, 8, 12, 200));
		m_r.HudRect(sx - bw * .5f, sy, bw * Clampf(frac, 0, 1), U(4), col);
		if (u.ch.stat[S_MP] > 0) {
			float mf = (float)u.mp / max(1, u.ch.stat[S_MP]);
			m_r.HudRect(sx - bw * .5f - 1, sy + U(5), bw + 2, U(4), RGBA8(8, 8, 12, 200));
			m_r.HudRect(sx - bw * .5f, sy + U(6), bw * Clampf(mf, 0, 1), U(3), u.alive ? RGBA8(80, 150, 255) : RGBA8(90, 100, 120));
		}
		{
			float cf = (float)u.cp / (float)CP_MAX;
			float cy = sy + (u.ch.stat[S_MP] > 0 ? U(10) : U(5));
			m_r.HudRect(sx - bw * .5f - 1, cy, bw + 2, U(4), RGBA8(8, 8, 12, 200));
			m_r.HudRect(sx - bw * .5f, cy + 1, bw * Clampf(cf, 0, 1), U(3), u.cp >= 100 ? RGBA8(255, 210, 70) : RGBA8(210, 160, 50));
		}
		if (!(m_seqCut && m_cutStage == 0) && u.alive && (u.sleep > 0 || u.confuse > 0 || u.stun > 0 || u.poison > 0 || u.slow > 0 || u.defDown > 0)) {
			const wchar_t* bbl = L"!";
			DWORD bc = RGBA8(160, 110, 20);
			float fs = U(15);
			if (u.sleep > 0) { bbl = L"Zzz"; bc = RGBA8(70, 110, 180); fs = U(15); }
			else if (u.confuse > 0) { bbl = L"??"; bc = RGBA8(170, 60, 150); fs = U(16); }
			else if (u.stun > 0) { bbl = L"!"; bc = RGBA8(160, 110, 20); fs = U(16); }
			else if (u.poison > 0) { bbl = EffectWord(EF_POISON); bc = RGBA8(110, 50, 150); fs = U(13); }
			else if (u.slow > 0) { bbl = EffectWord(EF_SLOW); bc = RGBA8(50, 80, 150); fs = U(13); }
			else { bbl = EffectWord(EF_DEFDOWN); bc = RGBA8(150, 90, 40); fs = U(12); }
			float bob = sinf(m_time * (u.sleep > 0 ? 2.4f : 7.f)) * U(3);
			float twb = m_r.TextWidth(bbl, fs, 0);
			float bx = sx - twb * .5f - U(7);
			float byb = sy - U(28) + bob;
			m_r.HudRect(bx, byb, twb + U(14), U(18), RGBA8(248, 248, 252, 230));
			m_r.HudText(bbl, sx, byb + U(8), fs, bc, 1, 1);
		}
	}
	if (m_phase == PH_P_SKILLTARGET && m_cmdSkill >= 0 && m_cursorValid && m_cur >= 0) {
		const SkillDef& sd = SkillInfo(m_cmdSkill);
		const Unit& atk = m_b.u[m_cur];
		wchar_t count[48] = L"";
		BOOL showCount = sd.area > 0;
		if (showCount) {
			int tg[MAX_UNITS];
			int nt = SkillTargets(m_b, m_cur, m_cmdSkill, m_cx, m_cz, tg, MAX_UNITS);
			swprintf_s(count, L"%s %d", TT(TX_BT_AOEN), nt);
		}
		wchar_t tags[10][48];
		int nt = 0;
		int def = m_b.UnitAt(m_cx, m_cz);
		BOOL hostile = def >= 0 && m_b.u[def].alive && m_b.u[def].team != atk.team && sd.kind != SK_HEAL && sd.kind != SK_BUFF;
		if (hostile && CanTarget(m_cur, def, m_cmdSkill)) {
			Forecast f;
			CalcForecast(m_b, m_cur, def, m_cmdSkill, f);
			const Unit& d = m_b.u[def];
			wcscpy_s(tags[nt++], TT(f.face == 2 ? TX_BT_FACE_BACK : (f.face == 1 ? TX_BT_FACE_SIDE : TX_BT_FACE_FRONT)));
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
		float sx, sy;
		if ((showCount || nLine > 0) && m_r.ProjectToScreen(CellCenter(m_cx, m_cz) + v3(0, 1.15f, 0), sx, sy)) {
			float countPx = U(15);
			float countH = countPx * 1.6f + 6.f;
			float tagH = tagPx * 1.6f + 6.f;
			float gap = U(4);
			float block = 0;
			if (showCount) block += countH;
			if (nLine > 0) block += (showCount ? gap : 0.f) + nLine * tagH + (nLine - 1) * gap;
			float fw = min(U(520.f), (float)m_w - U(16));
			float fh = min(U(176.f), max(U(120.f), (float)m_h * .38f));
			float fx = ((float)m_w - fw) * .5f, fy = U(8);
			float top = sy - U(10) - block;
			BOOL underForecast = top < fy + fh && sx > fx - U(40) && sx < fx + fw + U(40);
			if (underForecast || top < U(4)) top = sy + U(20);
			float y = top;
			if (showCount) {
				int nShown = 0;
				if (count[0]) {
					const wchar_t* p = count;
					while (*p && (*p < L'0' || *p > L'9')) p++;
					nShown = _wtoi(p);
				}
				m_r.HudText(count, sx, y + countH * .5f, countPx, nShown > 0 ? RGBA8(255, 230, 120) : RGBA8(255, 140, 120), 1, 1);
				y += countH;
				if (nLine > 0) y += gap;
			}
			for (int i = 0; i < nLine; i++) {
				m_r.HudText(lines[i], sx, y + tagH * .5f, tagPx, RGBA8(255, 230, 170), 1, 0);
				y += tagH;
				if (i + 1 < nLine) y += gap;
			}
		}
	}
	if (m_b.nUnits > 0) DrawTurnOrder();
	else m_orderBottom = U(14);
	float ms = U(236);
	float miniLeft = (float)m_w - ms - U(16);
	DrawMinimap(miniLeft, (float)m_h - ms - U(16), ms);
	float gap = U(12);
	float panelX = U(14);
	float panelTop = (float)m_h - U(162);
	float room = miniLeft - gap - panelX;
	float panelW = min(U(460.f), max(U(260.f), room * .46f));
	if (panelW > room - U(200) - gap) panelW = max(U(220.f), room - U(200) - gap);
	float logX = panelX + panelW + gap;
	float logW = miniLeft - gap - logX;
	if (logW < U(140)) logW = U(140);
	float logTop = (float)m_h - U(146);
	float logH = U(132);
	if (m_cur >= 0 && m_phase != PH_OVER && m_phase != PH_INTRO && (m_b.u[m_cur].alive || m_b.u[m_cur].team == TEAM_PLAYER)) DrawUnitPanel(m_b.u[m_cur], panelX, panelTop, panelW, TRUE);
	if (m_hoverUnit >= 0 && m_hoverUnit != m_cur && m_phase != PH_ACTION && (m_b.u[m_hoverUnit].alive || m_b.u[m_hoverUnit].team == TEAM_PLAYER)) {
		float hw = min(U(320.f), max(U(180.f), logW));
		float hh = U(112);
		float hy = logTop - U(8) - hh;
		DrawUnitPanel(m_b.u[m_hoverUnit], logX, hy, hw, FALSE);
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
		if (c.gimm == GM_WARP)
			swprintf_s(l, L"%s  %s  %s %d", TT(TX_BT_TERRAIN), TT(td.nameTx), gm, (int)c.gparam + 1);
		else if (c.gimm)
			swprintf_s(l, L"%s  %s  %s", TT(TX_BT_TERRAIN), TT(td.nameTx), gm);
		else if (m_cur >= 0 && CanTerrace(m_b, m_cur, m_cx, m_cz))
			swprintf_s(l, L"%s  %s  H%d   %s", TT(TX_BT_TERRAIN), TT(td.nameTx), (int)c.h, TT(TX_BT_CLIMB));
		else
			swprintf_s(l, L"%s  %s  %s+%d%%  AVO+%d%%  H%d", TT(TX_BT_TERRAIN), TT(td.nameTx), TT(TX_ST_DEF), td.defPct, td.avoPct, c.h);
		float tw = m_r.TextWidth(l, U(13), 0) + U(24);
		float tx = (float)m_w - ms - U(30) - tw, ty = (float)m_h - U(188);
		m_r.HudPanel(tx, ty, tw, U(30), RGBA8(16, 20, 36, 210), RGBA8(150, 128, 86));
		m_r.HudText(l, tx + U(12), ty + U(15), U(13), RGBA8(236, 236, 240), 0, 0);
	}
	if (m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET) DrawForecast();
	if (m_phase == PH_P_CMD || m_phase == PH_P_SKILLS || m_phase == PH_P_ITEM) DrawCommandMenu();
	if (m_phase == PH_P_SKILLTARGET && m_cur >= 0 && IsFormSkill(m_cmdSkill)) {
		int tx = m_b.u[m_cur].x, tz = m_b.u[m_cur].z;
		if (m_cursorValid) { tx = m_cx; tz = m_cz; }
		DrawFormGuide(m_cmdSkill, tx, tz, 0, 0, 0, 0);
	}
	if (m_statOpen && m_phase != PH_P_CMD && m_phase != PH_ACTION && m_phase != PH_MOVING && m_phase != PH_E_MOVE && m_phase != PH_INTRO && m_phase != PH_OVER && m_phase != PH_LEVELUP) DrawBattleStatus();
	if (m_phase == PH_LEVELUP) DrawLevelUp();
	// ヒント
	{
		int tx = -1;
		switch (m_phase) {
		case PH_P_MOVE: tx = TX_BT_HINT_MOVE; break;
		case PH_P_CMD: case PH_P_SKILLS: tx = TX_BT_HINT_CMD; break;
		case PH_P_TARGET: tx = TX_BT_HINT_TARGET; break;
		case PH_P_SHOVE: tx = TX_BT_SHOVE; break;
		case PH_P_SKILLTARGET: tx = TX_BT_HINT_SKILL; break;
		case PH_E_SHOW: case PH_E_MOVE: tx = TX_BT_HINT_ENEMY; break;
		case PH_ACTION: if (m_cur >= 0 && m_b.u[m_cur].team == TEAM_ENEMY) tx = TX_BT_HINT_ENEMY; break;
		default: break;
		}
		if (tx >= 0) {
			const wchar_t* hs = TT(tx);
			BOOL wide = (m_phase == PH_P_MOVE);
			BOOL keys = (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET);
			float leftBound = partyRight + U(16);
			float rightBound = (float)m_w - U(214);
			float y = U(96);
			if (m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET) {
				float fw = U(520), fx0 = ((float)m_w - fw) * .5f, fx1 = fx0 + fw + U(10);
				float leftRoom = fx0 - U(8) - leftBound;
				float rightRoom = rightBound - fx1;
				if (rightRoom >= leftRoom && rightRoom > U(220)) leftBound = fx1;
				else if (leftRoom > U(220)) rightBound = fx0 - U(8);
				else y = U(14) + U(176) + U(20);
			}
			float room = max(U(200.f), rightBound - leftBound);
			float want = wide ? U(820) : (m_r.TextWidth(hs, U(16), 1) + U(40));
			float tw = min(want, room);
			float tx0 = leftBound + max(0.f, (room - tw) * .5f);
			float unitTop = (float)m_h - U(160);
			if (room < U(300) && partyBottom + U(100) < unitTop) {
				tw = min(want, max(U(280.f), (float)m_w - partyRight - U(36)));
				tx0 = partyRight + U(16);
				y = partyBottom + U(24);
			}
			float textH = wide ? (tw < U(640) ? U(64) : U(42)) : U(28);
			float keysH = keys ? U(34) : 0;
			float top = y - U(8);
			float th = textH + keysH + U(14);
			m_r.HudPanel(tx0, top, tw, th, RGBA8(16, 20, 36, 96), RGBA8(150, 128, 86));
			if (wide) m_r.HudTextBox(hs, tx0 + U(12), top + U(6), tw - U(24), textH, U(15), RGBA8(255, 255, 255), 1);
			else m_r.HudText(hs, tx0 + tw * .5f, top + U(16), U(16), RGBA8(255, 255, 255), 1, 1);
			if (keys) m_r.HudTextBox(TT(TX_BT_HINT_KEYS), tx0 + U(12), top + textH + U(6), tw - U(24), keysH, U(12), RGBA8(210, 210, 220), 0);
		}
	}
	if (m_phase == PH_INTRO && m_surveyN > 0 && m_phaseT < m_surveyN * .5f) {
		wchar_t hs[160];
		swprintf_s(hs, TT(TX_BT_SURVEY), m_b.AliveCount(TEAM_ENEMY));
		float tw = m_r.TextWidth(hs, U(16), 1) + U(48);
		float tx0 = ((float)m_w - tw) * .5f, top = (float)m_h - U(96);
		m_r.HudPanel(tx0, top, tw, U(50), RGBA8(16, 20, 36, 150), RGBA8(150, 128, 86));
		m_r.HudText(hs, tx0 + tw * .5f, top + U(17), U(16), RGBA8(255, 255, 255), 1, 1);
		Bar(tx0 + U(16), top + U(34), tw - U(32), U(6), m_phaseT / (m_surveyN * .5f), RGBA8(255, 200, 110), RGBA8(255, 236, 170));
	}
	if (m_phase == PH_MENU) {
		m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 120));
		float w = m_menuConfirm == 2 ? U(420) : U(320);
		float h = m_menuConfirm == 1 ? U(190) : (m_menuConfirm == 2 ? U(430) : U(410));
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
				AddBanner(TT(TX_SV_SAVED), NULL, RGBA8(255, 226, 150), 0, 1.6f);
				m_phase = m_phaseBeforeMenu;
			}
			if (Btn(ID_BT_M_SLOT, bx, y + U(140), bw, bh, TT(TX_SLOT))) OpenSlotPanel();
			if (Btn(ID_BT_M_OPT, bx, y + U(186), bw, bh, TT(TX_SET_OPEN))) { m_menuConfirm = 2; m_focusId = ID_BT_OPT_Q; }
			if (Btn(ID_BT_M_HELP, bx, y + U(232), bw, bh, TT(TX_BT_HELP))) m_showHelp = TRUE;
			if (Btn(ID_BT_M_ZOOM, bx, y + U(278), bw, bh, TT(TX_BT_ZOOMRESET))) ResetCamDist();
			if (Btn(ID_BT_M_RETREAT, bx, y + U(324), bw, bh, TT(TX_BT_RETREAT), TRUE, FALSE, 0, RGBA8(140, 50, 40, 240))) { m_menuConfirm = 1; m_focusId = ID_BT_M_NO; }
		} else if (m_menuConfirm == 2) {
			static const int qtx[3] = { TX_SET_Q_LOW, TX_SET_Q_STD, TX_SET_Q_HIGH };
			float oy = y + U(48);
			if (int q = Sel(ID_BT_OPT_Q, bx, oy, bw, U(32), TT(TX_SET_QUALITY), TT(qtx[Clampi(m_save.quality, 0, 2)]))) {
				m_save.quality = (m_save.quality + q + 3) % 3;
				m_r.SetQuality(m_save.quality);
				InvalidatePortraits();
			}
			oy += U(34);
			if (Sel(ID_BT_OPT_ANIM, bx, oy, bw, U(32), TT(TX_SET_ANIM), TT(m_save.battleAnim ? TX_SET_ANIM_SHORT : TX_SET_ANIM_FULL))) m_save.battleAnim = !m_save.battleAnim;
			oy += U(34);
			if (Sel(ID_BT_OPT_GRID, bx, oy, bw, U(32), TT(TX_SET_GRID), TT(m_save.showGrid ? TX_CR_ON : TX_CR_OFF))) m_save.showGrid = !m_save.showGrid;
			oy += U(36);
			oy = DrawSoundOpts(bx, oy, bw, 1.f);
			if (Btn(ID_BT_M_RESUME, bx, oy + U(6), bw, bh, TT(TX_CR_BACK))) m_menuConfirm = 0;
		} else {
			m_r.HudTextBox(TT(TX_BT_RETREAT_Q), bx, y + U(50), bw, U(60), U(14), RGBA8(255, 220, 210));
			if (Btn(ID_BT_M_YES, bx, y + U(126), bw * .47f, bh, TT(TX_PT_YES), TRUE, FALSE, 0, RGBA8(140, 50, 40, 240))) { TacSfx(TS_LOSE); EndBattle(0); return; }
			if (Btn(ID_BT_M_NO, bx + bw * .53f, y + U(126), bw * .47f, bh, TT(TX_PT_NO))) m_menuConfirm = 0;
		}
	}
	if (m_seqCut) DrawCutInUi();
	if (m_fastFwd && (m_phase == PH_E_SHOW || m_phase == PH_E_MOVE || m_phase == PH_ACTION)) m_r.HudText(L"▶▶", (float)m_w - U(210), U(30), U(20), RGBA8(255, 226, 150), 1, 1);
}

} // namespace otac
