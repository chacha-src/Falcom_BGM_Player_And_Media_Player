#include "stdafx.h"
#include "OmakeTacticsGame.h"
#include "OmakeTacticsAudio.h"
#include <math.h>
#include <stdio.h>

namespace otac {

static const float PI = 3.14159265f;

// UI ID
enum {
	ID_BASE_SORTIE = 100, ID_BASE_CREATE, ID_BASE_PARTY, ID_BASE_EVOLVE, ID_BASE_CLOSE, ID_BASE_STAGE, ID_BASE_QUALITY, ID_BASE_ANIM, ID_BASE_CODEX,
	ID_CDX_PREV = 180, ID_CDX_NEXT, ID_CDX_GROW, ID_CDX_ALCH, ID_CDX_SKILL, ID_CDX_REC0,
	ID_BASE_GRID, ID_BASE_HELP, ID_BASE_SLOT0 = 120,
	ID_SORTIE_START = 160, ID_SORTIE_MID, ID_SORTIE_AUTO, ID_SORTIE_CANCEL,
	ID_CDX_JOB = 164, ID_CDX_SORT,
	ID_OPT_BGM = 190, ID_OPT_SE, ID_OPT_CAM, ID_MON_TAB,
	ID_BASE_SHOP = 210, ID_SHOP_CLOSE, ID_SHOP_BUY0 = 220, ID_SHOP_SELL0 = 250,
	ID_SHOP_PREV = 270, ID_SHOP_NEXT, ID_SHOP_LINE0 = 272, ID_SHOP_KIND0 = 278,
	ID_SHOP_TIER0 = 1120,
	ID_SHOP_BAND0 = 1140, ID_SHOP_BAND1, ID_SHOP_BAND2, ID_SHOP_BAND3,
	ID_PT_SHOP = 362,
	ID_SHOP_FREE = 1890, ID_SHOP_SELLALL, ID_SHOP_FIT_NO, ID_SHOP_FIT_PREV, ID_SHOP_FIT_NEXT, ID_SHOP_FIT0,
	ID_SHOP_SELLJUNK = 1920, ID_SHOP_ARM0 = 1930, ID_SHOP_ARM_SELL = 1948, ID_SHOP_ARM_NO = 1949,
	ID_SHOP_ASK_YES = 1950, ID_SHOP_ASK_NO, ID_SHOP_FIT_KEEP, ID_SHOP_TO_PARTY, ID_SHOP_DET0 = 1960,
	ID_SHOP_SLOT0 = 1984, ID_SHOP_SLOT1, ID_SHOP_SLOT2, ID_SHOP_SLOT_NO,
	ID_PT_BEAD2 = 1988, ID_PT_BEAD3, ID_SHOP_ART, ID_SHOP_OUGI, ID_PT_SUM0, ID_PT_SUM1,
	ID_BASE_SET = 945, ID_BASE_THREAT = 949, ID_PT_EVO = 363,
	ID_PT_WPN = 348, ID_PT_ARM,
	ID_CR_NAME = 200, ID_CR_RANDOM, ID_CR_GENDER, ID_CR_LINEAGE, ID_CR_CLASS, ID_CR_BODY, ID_CR_HAIR, ID_CR_HAIRCOL, ID_CR_OUTFIT,
	ID_CR_HAT, ID_CR_CAPE, ID_CR_OK, ID_CR_BACK, ID_CR_HEIGHT, ID_CR_BUST, ID_CR_FIG, ID_CR_FACE, ID_CR_AVATAR, ID_CR_AVACOL, ID_CR_MODEL,
	ID_PT_CARD0 = 300, ID_PT_DEPLOY = 340, ID_PT_EDIT, ID_PT_DELETE, ID_PT_YES, ID_PT_NO, ID_PT_BACK, ID_PT_NEW,
	ID_PT_PAGE_PREV = 360, ID_PT_PAGE_NEXT,
	ID_PT_ACC0 = 366, ID_PT_ACC1, ID_PT_BEAD0, ID_PT_BEAD1, ID_PT_PAS0 = 370, ID_PT_PAS1,
	ID_EQ_NONE = 1860, ID_EQ_ROW0, ID_EQ_PREV = 1880, ID_EQ_NEXT, ID_EQ_CLOSE,
	ID_EV_MAIN0 = 1000, ID_EV_TGT0 = 1200, ID_EV_PART0 = 1300, ID_EV_DO = 1500, ID_EV_BACK,
	ID_EV_PART_PREV = 1510, ID_EV_PART_NEXT, ID_EV_MAIN_PREV, ID_EV_MAIN_NEXT,
	ID_CR_MOT0 = 1550, ID_MON_MOT0 = 1560,
	ID_RS_NEXT = 700, ID_RS_RETRY, ID_RS_BASE,
	ID_HELP_CLOSE = 900,
	ID_BASE_REROLL = 940, ID_BASE_THEME,
	ID_BASE_SAVESLOT = 947,
	ID_SLOT_ROW0 = 981, ID_SLOT_USE = 991, ID_SLOT_REN, ID_SLOT_COPY, ID_SLOT_DEL, ID_SLOT_BACK, ID_SLOT_YES, ID_SLOT_NO
};

void ThemeEnv(int th, Env& e)
{
	struct T { BYTE top[3], bot[3], fog[3], sky[3], gnd[3]; float lx, ly, lz, amb, hard; BYTE ground[3]; };
	static const T k[TH_COUNT] = {
		{ { 84, 146, 228 }, { 206, 228, 246 }, { 196, 220, 242 }, { 160, 190, 230 }, { 120, 108, 84 }, .45f, .82f, .35f, .55f, .6f, { 110, 170, 80 } },
		{ { 112, 146, 214 }, { 246, 206, 164 }, { 236, 200, 166 }, { 190, 180, 200 }, { 140, 100, 70 }, .55f, .7f, .3f, .55f, .62f, { 170, 120, 80 } },
		{ { 66, 128, 172 }, { 176, 214, 194 }, { 150, 192, 172 }, { 140, 180, 170 }, { 80, 96, 60 }, .35f, .85f, .4f, .5f, .55f, { 70, 120, 60 } },
		{ { 128, 160, 214 }, { 232, 238, 248 }, { 222, 230, 242 }, { 200, 214, 240 }, { 150, 160, 180 }, .4f, .7f, .5f, .62f, .5f, { 230, 236, 246 } },
		{ { 98, 162, 232 }, { 252, 228, 176 }, { 244, 218, 176 }, { 200, 196, 190 }, { 170, 140, 90 }, .5f, .85f, .2f, .58f, .7f, { 220, 194, 140 } },
		{ { 40, 24, 34 }, { 170, 66, 40 }, { 120, 52, 40 }, { 120, 70, 80 }, { 140, 60, 30 }, .3f, .75f, .55f, .48f, .65f, { 80, 70, 68 } },
		{ { 70, 92, 82 }, { 156, 166, 132 }, { 124, 136, 112 }, { 120, 140, 120 }, { 80, 80, 50 }, .35f, .8f, .45f, .5f, .5f, { 80, 96, 60 } },
		{ { 58, 56, 120 }, { 206, 170, 204 }, { 166, 144, 176 }, { 150, 140, 190 }, { 110, 90, 90 }, .6f, .6f, .35f, .52f, .6f, { 160, 156, 140 } },
	};
	const T& t = k[Clampi(th, 0, TH_COUNT - 1)];
	e.skyTop = RGBA8(t.top[0], t.top[1], t.top[2]);
	e.skyBot = RGBA8(t.bot[0], t.bot[1], t.bot[2]);
	e.fog = RGBA8(t.fog[0], t.fog[1], t.fog[2]);
	e.ambSky = RGBA8(t.sky[0], t.sky[1], t.sky[2]);
	e.ambGnd = RGBA8(t.gnd[0], t.gnd[1], t.gnd[2]);
	e.ground = RGBA8(t.ground[0], t.ground[1], t.ground[2]);
	e.light = Norm(v3(t.lx, t.ly, t.lz));
	e.amb = t.amb;
	e.hard = t.hard;
}

Game::Game()
{
	m_hwnd = NULL;
	m_w = m_h = 8;
	m_ui = 1;
	m_time = 0;
	m_screen = SC_LOADING;
	m_wantClose = FALSE;
	m_loaded = FALSE;
	m_loadFrames = 0;
	m_loadPm = 0;
	m_loadShown = -1;
	m_loadPaint = 0;
	m_loadA = m_loadB = 0;
	m_loadLive = FALSE;
	m_renderDt = 1.f / 60.f;
	m_lastRenderTime = 0;
	m_recoverTries = 0;
	m_mx = m_my = 0;
	m_mouseSeen = FALSE;
	m_lDown = m_rDown = m_mDown = FALSE;
	m_downUi = FALSE;
	m_downX = m_downY = 0;
	m_click = m_rclick = FALSE;
	m_clickX = m_clickY = 0;
	m_dragRot = m_dragPan = FALSE;
	m_dragLastX = m_dragLastY = 0;
	m_dragDist = 0;
	m_focusId = -1; m_activateId = -1;
	m_navMode = FALSE;
	m_selDelta = 0;
	m_textFocus = 0;
	m_keyShift = FALSE;
	memset(&m_padPrev, 0, sizeof(m_padPrev));
	m_padRepeat = 0; m_padDirPrev = -1; m_padTrig = 0;
	m_fastFwd = FALSE;
	m_showHelp = FALSE;
	m_codex = 0;
	m_codexPage = 0;
	m_monSel = 0;
	m_monYaw = 0;
	m_monShown = -2;
	m_monMot = 0;
	m_monDrag = FALSE;
	m_codexJob = 0;
	m_codexSort = 0;
	m_skillPage = 0;
	m_skillTab = 0;
	m_skillShape = 0;
	m_skillEl = 0;
	m_skillPreview = -1;
	memset(m_skMemoTab, 0, sizeof(m_skMemoTab));
	memset(m_skMemoPage, 0, sizeof(m_skMemoPage));
	m_stashFull = 0;
	m_sortieAsk = 0;
	m_midStage = 0;
	m_autoStage = 0;
	m_shop = 0;
	m_btnSfx = -1;
	m_uiHover = m_uiHoverCand = -1;
	m_uiHoverHit = 0;
	m_uiHoverScreen = -1;
	m_uiHoverShop = 0;
	m_shopLine = 0;
	m_shopTier = 0;
	m_shopBand = 0;
	m_shopPage = 0;
	m_shopSellPage = 0;
	m_shopKind = 0;
	m_shopSuper = 0;
	m_shopBack = 0;
	m_shopFit = 0;
	m_shopFitId = 0;
	m_shopFitPage = 0;
	m_shopHold = 0;
	m_shopWho = -1;
	m_sumOn = 0;
	m_sumFx = 0;
	m_sumHit = 0;
	m_sumU = 0;
	m_partyBack = 0;
	m_baseOpt = 0;
	m_slotUi = 0;
	m_slotPick = 0;
	m_slotDest = -1;
	m_slotEdit[0] = 0;
	for (int si = 0; si < TAC_SLOT_N; si++) m_slotClear[si] = -1;
	m_createFromParty = 0;
	m_evFromParty = 0;
	m_evJump = 0;
	m_statEq = 0;
	m_statEqPage = 0;
	m_itemAim = -1;
	m_itemPage = 0;
	m_statTip = -1;
	m_skillTip = -1;
	m_skillTipId = 0;
	m_statOpen = 0;
	m_statWho = -1;
	m_rosterPage = 0;
	m_mapSeed = 0;
	m_mapSeedStage = -1;
	m_ordSnapN = 0;
	m_ordLeave = -1;
	m_ordAnimT = 0;
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) m_ptSlotUnit[i] = -1;
	for (int i = 0; i < MAX_UNITS; i++) m_ptForUnit[i] = -1;
	m_ult = FALSE;
	m_confirmDelete = 0;
	m_camTarget = m_camGoal = v3(32, 0, 32);
	m_surveyN = 0;
	m_camYaw = m_camYawGoal = PI * .25f;
	m_camPitch = .9f;
	m_camDist = m_camDistGoal = 18.f;
	m_zoomHold = FALSE;
	m_zoomDist = 18.f;
	m_wheelSign = 0;
	m_wheelTime = -1.f;
	m_camPan = v3(0, 0, 0);
	m_shake = 0;
	m_eye = v3(0, 10, 0);
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) { m_portraitKey[i] = -1; m_portraitWant[i] = 0; }
	memset(&m_ptActor, 0, sizeof(m_ptActor));
	memset(&m_showMap, 0, sizeof(m_showMap));
	m_showBuilt = FALSE;
	m_showStage = 0;
	m_showTheme = -2;
	m_themeSel = -1;
	m_showN = 0;
	m_showYaw = 0;
	memset(&m_edit, 0, sizeof(m_edit));
	m_editIdx = -1;
	m_previewYaw = 0;
	m_previewPitch = .18f;
	m_previewZoom = 1.f;
	m_previewPan = v3(0, 0, 0);
	m_lineage = 0;
	m_previewDrag = FALSE;
	m_previewPanDrag = FALSE;
	m_previewMot = 0;
	m_partySel = -1;
	m_eqPick = 0;
	m_eqPage = 0;
	m_evMain = m_evTarget = -1;
	m_evPartner[0] = m_evPartner[1] = -1;
	m_evPick = 0;
	m_evMainPage = 0; m_evMainPages = 1;
	m_evPartPage = 0; m_evPartPages = 1;
	m_evFlash = 0;
	memset(&m_b, 0, sizeof(m_b));
	m_phase = m_phaseBeforeMenu = PH_NONE;
	m_phaseT = 0;
	m_cur = -1;
	m_cmdFaced = 0;
	m_hoverUnit = -1;
	m_cursorValid = FALSE;
	m_cx = m_cz = 0;
	m_ovDirty = TRUE;
	m_pathN = 0;
	m_moveN = 0;
	m_seqN = m_seqCur = 0;
	m_seqCut = FALSE;
	m_cutStage = 0;
	m_lvqN = m_lvqCur = 0;
	m_promoAsk = 0;
	m_victory = 0;
	m_resultN = 0;
	m_menuConfirm = 0;
	m_enemyWait = 0;
	m_flashWhite = 0;
	m_strkN = 0;
	m_turnOpen = FALSE;
	m_stepL = m_stepTi = 0;
	memset(&m_proj, 0, sizeof(m_proj));
	m_cutStageT = 0;
	m_cutA = m_cutD = -1;
	m_cutAllyN = m_cutFoeN = 0;
	m_cutKind = 0;
	m_seqCaster = m_seqPrimary = -1;
	m_seqSkill = -1;
	m_battleStage = 1;
	m_importBind = 0;
	m_logN = 0;
	m_orderBottom = 0;
	m_overT = 0;
	m_moveUnit = -1;
	SaveDefaults(m_save);
}

Game::~Game() { Shutdown(); }

BOOL Game::Init(HWND view, int w, int h)
{
	m_hwnd = view;
	m_w = max(8, w); m_h = max(8, h);
	SaveLoad(m_save);
	ApplyAudio();
	m_rng = Rng(GetTickCount() ^ 0x5EED1234u);
	m_screen = SC_LOADING;
	m_loadFrames = 0;
	m_ui = Clampf(min(m_w / 1280.f, m_h / 720.f), .62f, 2.2f);
	if (!m_r.Init(view, m_save.quality)) { m_screen = SC_FAIL; return FALSE; }
	return TRUE;
}

void Game::LoadPulseCb(int cur, int n, void* user)
{
	Game* g = (Game*)user;
	if (!g || n < 1) return;
	if (cur < 0) cur = 0;
	if (cur > n) cur = n;
	int a = g->m_loadA, b = g->m_loadB;
	if (b < a) b = a;
	g->NoteLoad(a + (int)((long long)(b - a) * cur / n));
}

void Game::BeginLoadSpan(int a, int b)
{
	if (a < 0) a = 0;
	if (b > 1000) b = 1000;
	if (b < a) b = a;
	m_loadA = a;
	m_loadB = b;
	m_r.SetLoadPulse(&Game::LoadPulseCb, this);
	NoteLoad(a);
}

void Game::DrawLoadHud()
{
	int pct = m_loadPm / 10;
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	m_r.HudText(TT(TX_TITLE), m_w * .5f, m_h * .38f, U(40), RGBA8(255, 230, 160), 1, 1);
	wchar_t line[80];
	swprintf_s(line, L"%s  %d%%", TT(TX_MISC_LOADING), pct);
	m_r.HudText(line, m_w * .5f, m_h * .50f, U(20), RGBA8(230, 230, 240), 1, 0);
	float bw = min(U(520.f), (float)m_w * .62f);
	float bh = U(22);
	float bx = ((float)m_w - bw) * .5f;
	float by = m_h * .56f;
	Bar(bx, by, bw, bh, pct / 100.f, RGBA8(255, 214, 120), RGBA8(196, 120, 48));
}

BOOL Game::PaintLoadScreen()
{
	if (!m_r.Ready() || !m_r.FrameReady()) return FALSE;
	SetupCamera(v3(0, 0, 0), 0, .3f, 10, .8f);
	m_r.BeginFrame();
	m_r.BeginMainPass();
	m_r.DrawSky(RGBA8(30, 34, 70), RGBA8(90, 70, 110), 0, 0);
	m_r.BeginHud();
	DrawLoadHud();
	m_r.EndHud();
	m_r.Present();
	m_r.EndFrameTextGc();
	return TRUE;
}

void Game::NoteLoad(int permille)
{
	if (!m_loadLive) return;
	if (permille < m_loadPm) permille = m_loadPm;
	if (permille > 1000) permille = 1000;
	m_loadPm = permille;
	int pct = m_loadPm / 10;
	unsigned now = GetTickCount();
	if (pct == m_loadShown && permille < 1000) return;
	if (permille < 1000 && m_loadShown >= 0 && now - m_loadPaint < 50) return;
	if (!PaintLoadScreen()) return;
	m_loadShown = pct;
	m_loadPaint = now;
}

BOOL Game::LoadEverything()
{
	m_loadLive = TRUE;
	m_loadPm = 0;
	m_loadShown = -1;
	m_loadPaint = 0;
	NoteLoad(0);
	BeginLoadSpan(0, 300);
	if (!m_r.LoadAssets()) { m_r.SetLoadPulse(NULL, NULL); m_loadLive = FALSE; return FALSE; }
	NoteLoad(300);
	BeginLoadSpan(300, 450);
	if (!BuildLibrary(m_r, m_lib)) { m_r.SetLoadPulse(NULL, NULL); m_loadLive = FALSE; return FALSE; }
	NoteLoad(450);
	int slot[MAX_PARTY], nb = 0;
	for (int i = 0; i < MAX_PARTY; i++) {
		int ri = m_save.party[i];
		if (ri < 0 || ri >= MAX_ROSTER || !m_save.roster[ri].used) continue;
		slot[nb++] = ri;
	}
	if (nb < 1) NoteLoad(650);
	for (int k = 0; k < nb; k++) {
		BeginLoadSpan(450 + 200 * k / nb, 450 + 200 * (k + 1) / nb);
		const CharData& c = m_save.roster[slot[k]];
		char nm[16];
		sprintf_s(nm, c.gender == 1 ? "HumF%d" : "HumM%d", CharModel(c));
		m_r.EnsureBody(nm);
		NoteLoad(m_loadB);
	}
	BeginLoadSpan(650, 710);
	m_r.LoadGearPack();
	NoteLoad(710);
	BeginLoadSpan(710, 740);
	m_r.LoadFacePack();
	NoteLoad(740);
	BeginLoadSpan(740, 820);
	m_r.LoadAvatarPack();
	NoteLoad(820);
	BeginLoadSpan(820, 850);
	m_r.LoadHairPack();
	NoteLoad(850);
	m_r.SetLoadPulse(NULL, NULL);
	m_loaded = TRUE;
	m_loadLive = FALSE;
	return TRUE;
}

void Game::Shutdown()
{
	ShutdownSnap();
	ReleaseLibrary(m_lib);
	m_r.Release();
	m_loaded = FALSE;
}

BOOL Game::Recover()
{
	if (m_recoverTries > 5) return FALSE;
	m_recoverTries++;
	ReleaseLibrary(m_lib);
	m_r.Release();
	m_loaded = FALSE;
	if (!m_r.Init(m_hwnd, m_save.quality)) return FALSE;
	if (!LoadEverything()) return FALSE;
	InvalidatePortraits();
	if (m_screen == SC_BATTLE || m_screen == SC_RESULT) {
		m_importBind = 0;
		LoadBattleCast();
		m_importBind = 1;
		m_r.BuildTerrain(m_b.map);
		m_r.BuildMinimap(m_b.map);
		for (int i = 0; i < m_b.nUnits; i++) ActorForUnit(i);
		m_showBuilt = FALSE;
	} else {
		m_showBuilt = FALSE;
		BuildShowcase();
	}
	if (m_screen == SC_CREATE) RefreshPreview();
	return TRUE;
}

void Game::Resize(int w, int h)
{
	m_w = max(8, w); m_h = max(8, h);
	m_ui = Clampf(min(m_w / 1280.f, m_h / 720.f), .62f, 2.2f);
	if (m_r.Ready()) m_r.Resize(m_w, m_h);
}

void Game::RefreshSurface()
{
	if (m_r.Ready()) m_r.RefreshSurface();
}

void Game::Persist()
{
	SaveWrite(m_save);
}

void Game::ApplyAudio()
{
	TacAudioSetVol(m_save.bgmVol, m_save.seVol);
}

float Game::DrawSoundOpts(float x, float y, float w, float k)
{
	if (k < .4f) k = .4f;
	float rh = U(32) * k, step = U(34) * k;
	wchar_t v[16];
	swprintf_s(v, L"%d", m_save.bgmVol * 10);
	if (int d = Sel(ID_OPT_BGM, x, y, w, rh, TT(TX_SET_BGM), v)) {
		m_save.bgmVol = Clampi(m_save.bgmVol + d, 0, 10);
		ApplyAudio();
	}
	y += step;
	swprintf_s(v, L"%d", m_save.seVol * 10);
	if (int d = Sel(ID_OPT_SE, x, y, w, rh, TT(TX_SET_SE), v)) {
		m_save.seVol = Clampi(m_save.seVol + d, 0, 10);
		ApplyAudio();
		TacSfx(TS_OK_MENU);
	}
	y += step;
	static const int camTx[3] = { TX_SET_SLOW, TX_SET_Q_STD, TX_SET_FAST };
	int c = Clampi(m_save.camSpeed, 0, 2);
	if (int d = Sel(ID_OPT_CAM, x, y, w, rh, TT(TX_SET_CAM), TT(camTx[c])))
		m_save.camSpeed = (c + d + 3) % 3;
	return y + step;
}

int Game::PartyCap() const
{
	int c = m_save.partyCap;
	if (c < 5) c = 5;
	if (c > MAX_PARTY) c = MAX_PARTY;
	return c;
}
int Game::RosterCap() const
{
	int c = m_save.rosterCap;
	if (c < 20) c = 20;
	if (c > MAX_ROSTER) c = MAX_ROSTER;
	c -= c % 20;
	if (c < 20) c = 20;
	return c;
}
int Game::PartyCount() const
{
	int n = 0, cap = PartyCap();
	for (int i = 0; i < cap; i++) if (m_save.party[i] >= 0) n++;
	return n;
}
int Game::RosterCount() const
{
	int n = 0;
	for (int i = 0; i < MAX_ROSTER; i++) if (m_save.roster[i].used) n++;
	return n;
}

const wchar_t* Game::ClassName(int cls) const { return TT(ClassInfo(cls).nameTx); }

void Game::UnitTitle(const Unit& u, wchar_t* out, int n) const
{
	if (u.team == TEAM_PLAYER) swprintf_s(out, n, L"%s", u.ch.name);
	else {
		const MonsterDef& md = MonsterInfo(u.monster);
		if (md.look == LK_MESH) swprintf_s(out, n, L"%s%s", u.boss ? L"★" : L"", MonsterLabel(u.monster));
		else if (md.nameB >= 0) swprintf_s(out, n, L"%s%s %s", u.boss ? L"★" : L"", TT(md.nameTx), TT(md.nameB));
		else swprintf_s(out, n, L"%s%s", u.boss ? L"★" : L"", TT(md.nameTx));
	}
}

// ============================================================ メインループ
void Game::Tick(float dt)
{
	dt = Clampf(dt, 0, .1f);
	m_time += dt;
	if (m_screen == SC_LOADING) {
		if (m_loadFrames++ >= 2) {
			if (!LoadEverything()) { m_screen = SC_FAIL; m_loadLive = FALSE; }
			else {
				m_loadLive = TRUE;
				BeginLoadSpan(850, 1000);
				EnterBase();
				NoteLoad(1000);
				m_r.SetLoadPulse(NULL, NULL);
				m_loadLive = FALSE;
			}
		}
	}
	if (m_r.Ready() && m_r.DeviceLost()) {
		if (!Recover()) { m_screen = SC_FAIL; return; }
	}
	float gdt = dt * (m_fastFwd ? 3.f : 1.f);
	switch (m_screen) {
	case SC_BASE: case SC_PARTY: case SC_EVOLVE: case SC_CREATE: UpdateBase(dt); break;
	case SC_BATTLE: UpdateBattle(m_showHelp ? 0 : gdt); break;
	case SC_RESULT: UpdateBase(dt); break;
	default: break;
	}
	UpdateParticles(m_screen == SC_BATTLE ? gdt : dt);
	if (m_evFlash > 0) m_evFlash = max(0.f, m_evFlash - dt);
	SyncBgm();
	if (!m_r.Ready()) return;
	if (!m_r.FrameReady()) return;
	RenderFrame();
}

static_assert(TAC_BGM_RUINS - TAC_BGM_PLAINS + 1 == TH_COUNT, "地形BGM");
// 拠点=tactics.ogg。戦闘は地形ごとの tacticsgame*.ogg、無ければ tacticsgame.ogg。
// 勝利目前は二つのときだけ。距離も匹数も見ない。
// 敵全体の残りHPが、その最大HP合計の1/4以下。倒した敵の最大HPも分母に残す。
// または生存しているボスのHPが最大の40%以下。
// 一度入ったら bgmWin を立て、その戦闘では通常曲へ戻さない。スタンピードで敵が増えても戻さない。
// 敗北寸前曲は毎フレーム見る。味方の残りHPが最大の33%未満、または死者が人数の半数以上。
// その条件を外れたら、勝利目前に入っていればそちら、まだなら地形曲。危険曲はラッチしない。
// 勝利確定とリザルトは tacticsgameclear。店=tacticsshop、作成・編成・進化=tacticsmake。テンポは変えない。
void Game::SyncBgm()
{
	if (m_screen == SC_BATTLE && m_phase == PH_OVER && !m_victory) {
		TacBgmFadeOut();
		return;
	}
	static int danger = 0;
	if (m_screen != SC_BATTLE) danger = 0;
	int scene = TAC_BGM_HUB;
	if (m_screen == SC_BATTLE || (m_screen == SC_RESULT && m_victory)) {
		int alive = m_b.AliveCount(TEAM_ENEMY);
		int players = m_b.AliveCount(TEAM_PLAYER);
		int all = 0;
		int hp = 0, mx = 0, partyN = 0, deadN = 0;
		long long eHp = 0, eMx = 0;
		int bossLow = 0;
		for (int i = 0; i < m_b.nUnits; i++) {
			const Unit& u = m_b.u[i];
			if (u.team == TEAM_ENEMY) {
				all++;
				int em = max(1, u.ch.stat[S_HP]);
				eMx += em;
				if (u.alive && u.hp > 0) eHp += u.hp;
				if (u.boss && u.alive && u.hp > 0 && (long long)u.hp * 5 <= (long long)em * 2) bossLow = 1;
			}
			if (u.team != TEAM_PLAYER) continue;
			partyN++;
			mx += max(1, u.ch.stat[S_HP]);
			if (u.alive && u.hp > 0) hp += u.hp;
			else deadN++;
		}
		const int fightWon = (m_victory == 1) || (m_screen == SC_RESULT) || (m_bossDown && players > 0) || (alive == 0 && all > 0 && players > 0);
		if (m_screen == SC_BATTLE && !fightWon && (bossLow || (eMx > 0 && eHp * 4 <= eMx))) m_b.bgmWin = 1;
		const int lowHp = mx > 0 && (long long)hp * 100 < (long long)mx * 33;
		const int halfDown = partyN > 0 && deadN * 2 >= partyN;
		if (fightWon || players <= 0 || m_phase == PH_OVER) danger = 0;
		else if (m_screen == SC_BATTLE) danger = (lowHp || halfDown) ? 1 : 0;
		const int fewLeft = m_b.bgmWin != 0;
		int th = Clampi(m_b.map.theme, 0, TH_COUNT - 1);
		scene = TAC_BGM_PLAINS + th;
		if (fewLeft) scene = TAC_BGM_WIN;
		if (fightWon) scene = TacBgmHas(TAC_BGM_CLEAR) ? TAC_BGM_CLEAR : TAC_BGM_WIN;
		if (danger && m_screen == SC_BATTLE && m_phase != PH_OVER && !fightWon && TacBgmHas(TAC_BGM_DANGER))
			scene = TAC_BGM_DANGER;
	} else if (m_shop) {
		scene = TAC_BGM_SHOP;
	} else if (m_screen == SC_CREATE || m_screen == SC_PARTY || m_screen == SC_EVOLVE) {
		scene = TAC_BGM_MAKE;
	}
	TacBgmScene(scene);
	TacBgmTempo(1.f);
}

void Game::SetupCamera(V3 target, float yaw, float pitch, float dist, float fov)
{
	V3 dir = v3(sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch));
	V3 eye = target + dir * dist;
	m_eye = eye;
	M4 view = MLookAtLH(eye, target, v3(0, 1, 0));
	M4 proj = MPerspLH(fov, (float)m_w / max(1, m_h), .1f, 400.f);
	m_r.SetCamera(view, proj, eye, m_time);
}

void Game::RenderFrame()
{
	m_renderDt = Clampf(m_time - m_lastRenderTime, 0, .1f);
	m_lastRenderTime = m_time;
	if (m_screen == SC_BATTLE && m_fastFwd) m_renderDt *= 2.f;
	m_r.BeginFrame();
	if (m_screen == SC_BATTLE) {
		AssignBattlePortraits();
		BindImportedActors();
	} else if (m_loaded) PrepareMenuPortraits();
	if (m_loaded) RenderPortraits();
	switch (m_screen) {
	case SC_BASE: case SC_PARTY: case SC_EVOLVE: case SC_RESULT:
		DrawShowcase3D();
		break;
	case SC_CREATE:
		DrawShowcase3D();
		DrawCreate3D();
		break;
	case SC_BATTLE:
		if (m_seqCut && m_cutStage >= 1 && m_cutStage <= 3) DrawCutIn3D();
		else DrawBattle3D();
		break;
	default: {
		SetupCamera(v3(0, 0, 0), 0, .3f, 10, .8f);
		m_r.BeginMainPass();
		m_r.DrawSky(RGBA8(30, 34, 70), RGBA8(90, 70, 110), 0, 0);
	} break;
	}
	m_r.BeginHud();
	m_btns.clear();
	if (m_uiHoverScreen != m_screen || m_uiHoverShop != (m_shop != 0)) {
		m_uiHover = -1;
		m_uiHoverScreen = m_screen;
		m_uiHoverShop = m_shop != 0;
	}
	m_uiHoverHit = 0;
	m_statTip = -1;
	m_skillTip = -1;
	switch (m_screen) {
	case SC_LOADING:
		DrawLoadHud();
		break;
	case SC_FAIL: {
		const wchar_t* msg = m_r.Ready() ? TT(TX_MISC_ASSETFAIL) : TT(TX_MISC_DXFAIL);
		m_r.HudText(TT(TX_TITLE), m_w * .5f, m_h * .4f, U(34), RGBA8(255, 230, 160), 1, 1);
		m_r.HudText(msg, m_w * .5f, m_h * .52f, U(18), RGBA8(255, 160, 150), 1, 0);
		wchar_t hr[64];
		swprintf_s(hr, L"0x%08X / %d", (unsigned)m_r.LastError(), m_r.FailStage());
		m_r.HudText(hr, m_w * .5f, m_h * .58f, U(14), RGBA8(200, 200, 210), 1, 0);
		if (Btn(ID_BASE_CLOSE, m_w * .5f - U(90), m_h * .66f, U(180), U(40), TT(TX_BASE_CLOSE))) m_wantClose = TRUE;
	} break;
	case SC_BASE: DrawBaseUi(); break;
	case SC_CREATE: DrawCreateUi(); break;
	case SC_PARTY: DrawPartyUi(); break;
	case SC_EVOLVE: DrawEvolveUi(); break;
	case SC_BATTLE: DrawBattleUi(); break;
	case SC_RESULT: DrawResultUi(); break;
	}
	DrawPopups(0);
	DrawBanners(0);
	if (m_showHelp) DrawHelpOverlay();
	else if (m_statTip >= 0) DrawStatTip();
	else if (m_skillTip >= 0) DrawSkillTip();
	if (m_evFlash > 0) m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(255, 255, 255, (int)(m_evFlash * 200)));
	if (m_screen == SC_BATTLE && m_click && !m_showHelp && !m_slotUi) {
		m_click = FALSE;
		BattleClick(m_clickX, m_clickY);
	}
	m_r.EndHud();
	m_r.Present();
	m_r.EndFrameTextGc();
	m_btnsPrev = m_btns;
	if (m_uiHoverHit) {
		if (m_uiHoverCand != m_uiHover && !m_click && m_activateId < 0)
			TacSfx(UiHoverId());
		m_uiHover = m_uiHoverCand;
	} else m_uiHover = -1;
	m_click = FALSE;
	m_rclick = FALSE;
	m_activateId = -1;
	m_selDelta = 0;
}

// 作成プレビューのカメラ。yaw は固定で、傾きだけが動く。right/up は MLookAtLH と同じ基底。
static float PreviewBodyH(const Actor& a)
{
	float ht = a.htScale > 0.05f ? a.htScale : 1.f;
	return a.height / ht + a.hover;
}

static void PreviewCamAxes(float pitch, V3& right, V3& up)
{
	V3 dir = v3(sinf(PI) * cosf(pitch), sinf(pitch), cosf(PI) * cosf(pitch));
	V3 fwd = dir * -1.f;
	right = Norm(Cross(v3(0, 1, 0), fwd));
	up = Cross(fwd, right);
	float l = Len(up);
	up = l > 1e-8f ? up * (1.f / l) : v3(0, 1, 0);
}

static void ClampPreviewPan(V3& pan, float h)
{
	float lim = max(h, .4f) * 1.6f;
	float l = Len(pan);
	if (l > lim) pan = pan * (lim / l);
}

// ============================================================ 入力
void Game::OnMouseMove(int x, int y)
{
	if (m_lDown && m_screen == SC_CREATE && m_previewDrag) {
		m_previewYaw += (x - m_mx) * .012f;
		m_previewPitch = Clampf(m_previewPitch + (y - m_my) * .005f, -1.05f, 1.15f);
	}
	if (m_mDown && m_screen == SC_CREATE && m_previewPanDrag) {
		float dx = (float)(x - m_mx), dy = (float)(y - m_my);
		float h = PreviewBodyH(m_preview);
		float dist = h * 2.9f * m_previewZoom;
		float worldPerPx = 2.f * dist * tanf(.55f * .5f) / (float)max(1, m_h);
		V3 right, up;
		PreviewCamAxes(Clampf(m_previewPitch, -1.05f, 1.15f), right, up);
		// 掴んで動かす。マウスを上げるとキャラが上がり、右へ動かすと右へ寄る。
		m_previewPan = m_previewPan - right * (dx * worldPerPx) + up * (dy * worldPerPx);
		ClampPreviewPan(m_previewPan, h);
	}
	if (m_lDown && m_monDrag) m_monYaw += (x - m_mx) * .012f;
	BOOL scout = m_lDown && !m_downUi && m_screen == SC_BATTLE && (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE || m_phase == PH_P_CMD || m_phase == PH_P_SKILLS);
	if (m_rDown || m_mDown || scout) {
		int dx = x - m_dragLastX, dy = y - m_dragLastY;
		m_dragDist += fabsf((float)dx) + fabsf((float)dy);
		if (m_rDown && m_dragDist > 6) {
			m_dragRot = TRUE;
			m_camYawGoal += dx * .008f;
			m_camYaw = m_camYawGoal;
			m_camPitch = Clampf(m_camPitch + dy * .004f, .45f, 1.35f);
		}
		if ((m_mDown && m_screen != SC_CREATE) || (scout && m_dragDist > 6)) {
			m_dragPan = TRUE;
			float s = m_camDist * .0022f;
			V3 right = v3(-cosf(m_camYaw), 0, sinf(m_camYaw));
			V3 fwd = v3(-sinf(m_camYaw), 0, -cosf(m_camYaw));
			m_camPan = m_camPan - right * (dx * s) + fwd * (dy * s);
		}
		m_dragLastX = x; m_dragLastY = y;
	}
	if (x != m_mx || y != m_my) { m_navMode = FALSE; m_mouseSeen = TRUE; }
	m_mx = x; m_my = y;
}

void Game::OnMouseDown(int btn, int x, int y)
{
	m_mx = x; m_my = y; m_mouseSeen = TRUE; m_navMode = FALSE;
	if (btn == 0) {
		m_lDown = TRUE; m_downX = x; m_downY = y;
		m_dragLastX = x; m_dragLastY = y; m_dragDist = 0; m_dragPan = FALSE;
		m_downUi = FALSE;
		for (auto& b : m_btnsPrev) if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) m_downUi = TRUE;
		m_previewDrag = (m_screen == SC_CREATE && x > m_w * .46f && x < m_w * .78f);
		m_monDrag = (m_screen == SC_BASE && m_codex == 4 && !m_downUi && x > m_w * .48f);
	}
	if (btn == 1) { m_rDown = TRUE; m_dragRot = FALSE; m_dragDist = 0; m_dragLastX = x; m_dragLastY = y; }
	if (btn == 2) {
		m_mDown = TRUE; m_dragPan = FALSE; m_dragDist = 0; m_dragLastX = x; m_dragLastY = y;
		m_previewPanDrag = (m_screen == SC_CREATE && x >= m_w * .40f);
	}
}

void Game::OnMouseUp(int btn, int x, int y)
{
	m_mx = x; m_my = y;
	if (btn == 0 && m_lDown) {
		m_lDown = FALSE;
		m_previewDrag = FALSE;
		m_monDrag = FALSE;
		if (abs(x - m_downX) < 12 && abs(y - m_downY) < 12) {
			m_click = TRUE; m_clickX = x; m_clickY = y;
			if (m_textFocus && m_screen == SC_CREATE) m_textFocus = 0;
		}
	}
	if (btn == 1 && m_rDown) {
		m_rDown = FALSE;
		if (!m_dragRot) { m_rclick = TRUE; NavBack(); }
		m_dragRot = FALSE;
	}
	if (btn == 2) { m_mDown = FALSE; m_dragPan = FALSE; m_previewPanDrag = FALSE; }
}

void Game::ApplyZoomHold(float dt)
{
	if (!m_zoomHold) return;
	m_camDistGoal = m_zoomDist;
	if (dt <= 0.f) return;
	float k = 1.f - expf(-dt * 9.f);
	m_camDist = Lerpf(m_camDist, m_zoomDist, k);
}

void Game::ZoomCam(float mul)
{
	if (mul <= 0.f) return;
	if (!m_zoomHold) {
		m_zoomHold = TRUE;
		m_zoomDist = m_camDist;
	}
	m_zoomDist = Clampf(m_zoomDist * mul, 5.f, 72.f);
	m_camDistGoal = m_zoomDist;
}

void Game::ResetCamDist()
{
	m_zoomHold = FALSE;
	m_wheelSign = 0;
	float d = (m_screen == SC_BASE) ? 12.f : 18.f;
	m_camDist = m_camDistGoal = m_zoomDist = d;
}

void Game::OnWheel(int delta)
{
	if (m_screen == SC_CREATE) {
		if (delta == 0 || m_mx < m_w * .40f) return;
		int sign = delta > 0 ? 1 : -1;
		if (m_wheelSign && sign != m_wheelSign && (m_time - m_wheelTime) < .35f) return;
		m_wheelSign = sign;
		m_wheelTime = m_time;
		m_previewZoom = Clampf(m_previewZoom * powf(.88f, (float)delta / 120.f), .38f, 2.6f);
		return;
	}
	if (m_screen == SC_BATTLE || m_screen == SC_BASE) {
		if (delta == 0) return;
		int sign = delta > 0 ? 1 : -1;
		// ホイール直後に逆符号が届くと、今の倍率が打ち消されて元へ戻る。
		if (m_wheelSign && sign != m_wheelSign && (m_time - m_wheelTime) < .35f)
			return;
		m_wheelSign = sign;
		m_wheelTime = m_time;
		ZoomCam(powf(.88f, (float)delta / 120.f));
		return;
	}
	if (m_screen == SC_EVOLVE) {
		float colW = ((float)m_w - U(40) - U(60)) / 3.f;
		float c0 = U(40);
		float c2 = c0 + 2.f * (colW + U(10));
		int* page = NULL;
		int pages = 1;
		if (m_mx >= c2 && m_evPartPages > 1) { page = &m_evPartPage; pages = m_evPartPages; }
		else if (m_mx < c0 + colW && m_evMainPages > 1) { page = &m_evMainPage; pages = m_evMainPages; }
		if (page) {
			*page = delta > 0 ? (*page + pages - 1) % pages : (*page + 1) % pages;
			TacSfx(TS_CURSOR);
		}
	}
}

void Game::OnChar(wchar_t ch)
{
	if (m_textFocus == 2) {
		size_t n = wcslen(m_slotEdit);
		if (ch == 8) { if (n > 0) m_slotEdit[n - 1] = 0; return; }
		if (ch == 13) { CommitSlotName(); return; }
		if (ch == 27 || ch == 9) { m_textFocus = 0; return; }
		if (ch < 32) return;
		if (n + 1 < TAC_SLOT_NAME) { m_slotEdit[n] = ch; m_slotEdit[n + 1] = 0; }
		return;
	}
	if (!m_textFocus || m_screen != SC_CREATE) return;
	size_t n = wcslen(m_edit.name);
	if (ch == 8) { if (n > 0) m_edit.name[n - 1] = 0; return; }
	if (ch == 13 || ch == 27 || ch == 9) { m_textFocus = 0; return; }
	if (ch < 32) return;
	if (n + 1 < NAME_LEN) { m_edit.name[n] = ch; m_edit.name[n + 1] = 0; }
}

void Game::OnKey(int vk, BOOL down)
{
	if (vk == VK_SHIFT) { m_keyShift = down; m_fastFwd = down; }
	if (!down) return;
	if (m_textFocus == 2) {
		if (vk == VK_RETURN) CommitSlotName();
		else if (vk == VK_ESCAPE) m_textFocus = 0;
		return;
	}
	if (m_textFocus && m_screen == SC_CREATE) {
		if (vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB) m_textFocus = 0;
		return;
	}
	if (m_showHelp) {
		if (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_F1 || vk == 'H' || vk == VK_SPACE) m_showHelp = FALSE;
		if (m_codex && (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_SPACE)) { m_codex = 0; return; }
		return;
	}
	if (vk == VK_F1 || vk == 'H') { m_showHelp = TRUE; return; }
	if (m_slotUi) {
		switch (vk) {
		case VK_LEFT: case 'A': NavMove(-1, 0); break;
		case VK_RIGHT: case 'D': NavMove(1, 0); break;
		case VK_UP: case 'W': NavMove(0, -1); break;
		case VK_DOWN: case 'S': NavMove(0, 1); break;
		case VK_RETURN: case VK_SPACE: case 'Z': NavConfirm(); break;
		case VK_ESCAPE: case VK_BACK: case 'X': NavBack(); break;
		default: break;
		}
		return;
	}
	if (m_screen == SC_BATTLE && m_phase == PH_INTRO && (vk == VK_RETURN || vk == VK_SPACE || vk == 'Z' || vk == VK_ESCAPE || vk == VK_BACK || vk == 'X')) {
		m_phaseT = max(m_phaseT, m_surveyN * .5f);
		return;
	}
	BOOL battleMap = m_screen == SC_BATTLE && (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE);
	switch (vk) {
	case VK_LEFT: case 'A': if (battleMap) MoveCursor(-1, 0); else NavMove(-1, 0); break;
	case VK_RIGHT: case 'D': if (battleMap) MoveCursor(1, 0); else NavMove(1, 0); break;
	case VK_UP: case 'W': if (battleMap) MoveCursor(0, 1); else NavMove(0, -1); break;
	case VK_DOWN: case 'S': if (battleMap) MoveCursor(0, -1); else NavMove(0, 1); break;
	case VK_RETURN: case VK_SPACE: case 'Z':
		if (battleMap && !m_navMode) BattleConfirmCursor(); else NavConfirm();
		break;
	case VK_ESCAPE: case VK_BACK: case 'X': NavBack(); break;
	case 'Q': m_camYawGoal -= PI * .5f; break;
	case 'E': m_camYawGoal += PI * .5f; break;
	case VK_PRIOR: case VK_ADD: case VK_OEM_PLUS: ZoomCam(.88f); break;
	case VK_NEXT: case VK_SUBTRACT: case VK_OEM_MINUS: ZoomCam(1.14f); break;
	case 'R': m_camPan = v3(0, 0, 0); break;
	case VK_TAB:
		if (m_screen == SC_BATTLE && m_b.nUnits > 0) {
			int start = m_hoverUnit >= 0 ? m_hoverUnit : m_cur;
			for (int k = 1; k <= m_b.nUnits; k++) {
				int i = (start + k + m_b.nUnits) % m_b.nUnits;
				if (m_b.u[i].alive) { m_cx = m_b.u[i].x; m_cz = m_b.u[i].z; m_cursorValid = TRUE; m_navMode = FALSE; UpdateHover(); break; }
			}
		}
		break;
	}
}

void Game::OnPad(const PadState& p, float dt)
{
	if (!p.connected) { memset(&m_padPrev, 0, sizeof(m_padPrev)); return; }
	auto pressed = [&](int bit) { return (p.buttons & (1 << bit)) && !(m_padPrev.buttons & (1 << bit)); };
	int dir = -1;
	float ax = p.lx, ay = p.ly;
	if (p.hat >= 0) { static const int hx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, hy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 }; ax = (float)hx[p.hat]; ay = (float)hy[p.hat]; }
	if (fabsf(ax) > .55f || fabsf(ay) > .55f) dir = fabsf(ax) > fabsf(ay) ? (ax > 0 ? 1 : 3) : (ay > 0 ? 2 : 0);
	BOOL battleMap = m_slotUi == 0 && m_screen == SC_BATTLE && (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE) && !m_showHelp;
	if (dir >= 0) {
		BOOL fire = FALSE;
		if (dir != m_padDirPrev) { fire = TRUE; m_padRepeat = .32f; }
		else { m_padRepeat -= dt; if (m_padRepeat <= 0) { fire = TRUE; m_padRepeat = .09f; } }
		if (fire) {
			static const int dx[4] = { 0, 1, 0, -1 }, dy[4] = { -1, 0, 1, 0 };
			if (battleMap) MoveCursor(dx[dir], -dy[dir]);
			else NavMove(dx[dir], dy[dir]);
		}
	}
	m_padDirPrev = dir;
	if (m_screen == SC_BATTLE && m_phase == PH_INTRO && !m_showHelp && (pressed(0) || pressed(1))) m_phaseT = max(m_phaseT, m_surveyN * .5f);
	else {
		if (pressed(0)) {
			if (m_showHelp) m_showHelp = FALSE;
			else if (battleMap && !m_navMode) BattleConfirmCursor();
			else NavConfirm();
		}
		if (pressed(1)) { if (m_showHelp) m_showHelp = FALSE; else NavBack(); }
	}
	if (pressed(4)) m_camYawGoal -= PI * .5f;
	if (pressed(5)) m_camYawGoal += PI * .5f;
	if (pressed(6)) m_showHelp = !m_showHelp;
	if (pressed(7) && m_screen == SC_BATTLE) NavBack();
	if (pressed(3) && m_screen == SC_BATTLE && m_cur >= 0) { m_cx = m_b.u[m_cur].x; m_cz = m_b.u[m_cur].z; m_camPan = v3(0, 0, 0); UpdateHover(); }
	m_fastFwd = m_keyShift || (p.buttons & 4) != 0;
	if (fabsf(p.rx) > .2f) m_camYawGoal += p.rx * dt * 2.2f;
	if (fabsf(p.ry) > .2f) m_camPitch = Clampf(m_camPitch + p.ry * dt * 1.2f, .45f, 1.35f);
	if (p.lt < .25f && p.rt < .25f) m_padTrig = 1;
	if (m_padTrig) {
		if (p.lt > .3f) ZoomCam(1.f + dt * 1.5f * p.lt);
		if (p.rt > .3f) ZoomCam(1.f - dt * 1.5f * p.rt);
	}
	m_padPrev = p;
}

// ============================================================ UI 部品
BOOL Game::Hit(float x, float y, float w, float h) const
{
	return m_mx >= x && m_my >= y && m_mx < x + w && m_my < y + h;
}

BOOL Game::ClickIn(float x, float y, float w, float h)
{
	if (!m_click) return FALSE;
	if (m_clickX >= x && m_clickY >= y && m_clickX < x + w && m_clickY < y + h &&
		m_downX >= x && m_downY >= y && m_downX < x + w && m_downY < y + h) {
		m_click = FALSE;
		return TRUE;
	}
	return FALSE;
}

void Game::Panel(float x, float y, float w, float h, float alpha)
{
	alpha = Clampf(alpha, 0.f, 1.f);
	m_r.HudGlass(x, y, w, h, RGBA8(22, 26, 48, (int)(206.f * alpha)), RGBA8(196, 168, 108, (int)(230.f * alpha)), alpha);
}

void Game::Bar(float x, float y, float w, float h, float frac, DWORD c0, DWORD c1, float ghost)
{
	m_r.HudRect(x, y, w, h, RGBA8(10, 10, 16, 220));
	frac = Clampf(frac, 0, 1);
	if (ghost > frac) m_r.HudRect(x + 1, y + 1, (w - 2) * Clampf(ghost, 0, 1), h - 2, RGBA8(255, 240, 200, 200));
	m_r.HudRectGrad(x + 1, y + 1, (w - 2) * frac, h - 2, c0, c1);
	m_r.HudFrame(x, y, w, h, 1, RGBA8(0, 0, 0, 200));
}

int Game::UiHoverId() const
{
	if (m_screen == SC_BATTLE) return TS_HOVER_GAME;
	if (m_shop) return TS_HOVER_SHOP;
	return TS_HOVER_BASE;
}

void Game::UiOk()
{
	int id = TS_OK_BASE;
	if (m_screen == SC_BATTLE) id = TS_OK_BATTLE;
	else if (m_screen == SC_CREATE || m_screen == SC_PARTY || m_screen == SC_EVOLVE) id = TS_OK_MAKE;
	else if (m_screen == SC_RESULT) id = TS_OK_BASE;
	else if (m_shop) id = TS_CONFIRM;
	TacSfx(id);
}

BOOL Game::Btn(int id, float x, float y, float w, float h, const wchar_t* label, BOOL en, BOOL sel, float px, DWORD tint)
{
	int sfx = m_btnSfx;
	m_btnSfx = -1;
	UiBtn b = { id, x, y, w, h, en ? 1 : 0, 0 };
	m_btns.push_back(b);
	BOOL hov = !m_navMode && m_mouseSeen && Hit(x, y, w, h);
	BOOL foc = m_navMode && m_focusId == id;
	if (hov && en) { m_uiHoverHit = 1; m_uiHoverCand = id; }
	BOOL fire = FALSE;
	if (en && ClickIn(x, y, w, h)) fire = TRUE;
	else if (!en && ClickIn(x, y, w, h)) TacSfx(TS_ERROR);
	if (en && m_activateId == id) { fire = TRUE; m_activateId = -1; }
	if (fire) {
		// 戻る・閉じる・いいえは場面の決定音ではなく取消音にする。
		if (sfx >= 0) TacSfx(sfx);
		else if (sfx != -2) {
			if (label == TT(TX_CR_BACK) || label == TT(TX_BASE_CLOSE) || label == TT(TX_PT_NO)) TacSfx(TS_CANCEL);
			else UiOk();
		}
	}
	DWORD top, bot;
	if (!en) { top = RGBA8(58, 58, 66, 210); bot = RGBA8(38, 38, 46, 210); }
	else if (sel) { top = RGBA8(240, 200, 100); bot = RGBA8(176, 122, 40); }
	else if (hov || foc) { top = RGBA8(96, 118, 186); bot = RGBA8(52, 64, 120); }
	else if (tint) { top = tint; bot = RGBA8((tint & 255) / 2, ((tint >> 8) & 255) / 2, ((tint >> 16) & 255) / 2, 235); }
	else { top = RGBA8(58, 66, 104, 238); bot = RGBA8(30, 34, 62, 238); }
	DWORD fill = RGBA8(
		((top & 255) + (bot & 255)) / 2,
		(((top >> 8) & 255) + ((bot >> 8) & 255)) / 2,
		(((top >> 16) & 255) + ((bot >> 16) & 255)) / 2,
		(top >> 24) & 255);
	m_r.HudGlass(x, y, w, h, fill, (hov || foc) ? RGBA8(255, 232, 150) : RGBA8(150, 128, 86), 1.f);
	if (foc) {
		float pulse = .5f + .5f * sinf(m_time * 6.f);
		m_r.HudFrame(x - U(3), y - U(3), w + U(6), h + U(6), U(2), RGBA8(255, 220, 120, (int)(120 + 120 * pulse)));
	}
	DWORD tc = !en ? RGBA8(140, 140, 150) : (sel ? RGBA8(40, 24, 8) : RGBA8(246, 242, 232));
	m_r.HudText(label, x + w * .5f, y + h * .5f, px > 0 ? px : U(17), tc, 1, 1, sel ? 0 : 1);
	return fire;
}

int Game::Sel(int id, float x, float y, float w, float h, const wchar_t* label, const wchar_t* value)
{
	UiBtn b = { id, x, y, w, h, 1, 1 };
	m_btns.push_back(b);
	BOOL hov = !m_navMode && m_mouseSeen && Hit(x, y, w, h);
	BOOL foc = m_navMode && m_focusId == id;
	if (hov) { m_uiHoverHit = 1; m_uiHoverCand = id; }
	int d = 0;
	float vx = x + w * .4f, vw = w * .6f;
	if (m_click && m_clickX >= vx && m_clickX < x + w && m_clickY >= y && m_clickY < y + h) {
		d = m_clickX < vx + vw * .5f ? -1 : 1;
		m_click = FALSE;
	} else if (m_click && m_clickX >= x && m_clickX < vx && m_clickY >= y && m_clickY < y + h) {
		d = 1;
		m_click = FALSE;
	}
	if (foc && m_selDelta) { d = m_selDelta; m_selDelta = 0; }
	if (m_activateId == id) { d = 1; m_activateId = -1; }
	if (d) TacSfx(TS_CURSOR);
	m_r.HudGlass(x, y, w, h, (hov || foc) ? RGBA8(64, 78, 130, 236) : RGBA8(34, 40, 70, 230), (hov || foc) ? RGBA8(255, 232, 150) : RGBA8(120, 104, 74), 1.f);
	if (foc) m_r.HudFrame(x - U(2), y - U(2), w + U(4), h + U(4), U(2), RGBA8(255, 220, 120, 180));
	m_r.HudText(label, x + U(10), y + h * .5f, U(15), RGBA8(220, 210, 180), 0, 0);
	BOOL hl = hov && m_mx < vx + vw * .5f, hr = hov && m_mx >= vx + vw * .5f;
	m_r.HudText(L"◀", vx + U(12), y + h * .5f, U(14), hl ? RGBA8(255, 230, 140) : RGBA8(170, 160, 140), 1, 1);
	m_r.HudText(L"▶", x + w - U(12), y + h * .5f, U(14), hr ? RGBA8(255, 230, 140) : RGBA8(170, 160, 140), 1, 1);
	m_r.HudText(value, vx + vw * .5f, y + h * .5f, U(16), RGBA8(255, 255, 255), 1, 1);
	return d;
}

void Game::NavMove(int dx, int dy)
{
	if (m_btnsPrev.empty()) return;
	const UiBtn* cur = NULL;
	for (auto& b : m_btnsPrev) if (b.id == m_focusId) cur = &b;
	if (!m_navMode || !cur) {
		m_navMode = TRUE;
		if (!cur) for (auto& b : m_btnsPrev) if (b.enabled) { m_focusId = b.id; break; }
		return;
	}
	if (cur->sel && dx != 0) { m_selDelta = dx; return; }
	float cx = cur->x + cur->w * .5f, cy = cur->y + cur->h * .5f;
	float best = 1e9f;
	int bestId = -1;
	for (auto& b : m_btnsPrev) {
		if (b.id == cur->id || !b.enabled) continue;
		float bx = b.x + b.w * .5f, by = b.y + b.h * .5f;
		float vx = bx - cx, vy = by - cy;
		float along = vx * dx + vy * dy;
		if (along <= 2) continue;
		float ortho = fabsf(vx * dy - vy * dx);
		float sc = along + ortho * 2.2f;
		if (sc < best) { best = sc; bestId = b.id; }
	}
	if (bestId >= 0 && bestId != m_focusId) { m_focusId = bestId; m_uiHover = bestId; TacSfx(UiHoverId()); }
}

void Game::NavConfirm()
{
	if (!m_navMode) {
		m_navMode = TRUE;
		BOOL found = FALSE;
		for (auto& b : m_btnsPrev) if (b.id == m_focusId && b.enabled) found = TRUE;
		if (!found) for (auto& b : m_btnsPrev) if (b.enabled) { m_focusId = b.id; break; }
		return;
	}
	m_activateId = m_focusId;
}

void Game::NavBack()
{
	if (m_showHelp) { m_showHelp = FALSE; return; }
	if (m_slotUi) {
		if (m_textFocus) { m_textFocus = 0; return; }
		if (m_slotUi == 5) m_slotUi = 4;
		else if (m_slotUi > 1) m_slotUi = 1;
		else m_slotUi = 0;
		return;
	}
	switch (m_screen) {
	case SC_CREATE:
		if (m_textFocus) m_textFocus = 0;
		else if (m_createFromParty) { m_createFromParty = 0; EnterParty(); }
		else EnterBase();
		break;
	case SC_PARTY:
		if (m_eqPick) m_eqPick = 0;
		else if (m_confirmDelete) m_confirmDelete = 0;
		else if (m_partyBack && m_shop) { m_partyBack = 0; m_screen = SC_BASE; }
		else EnterBase();
		break;
	case SC_EVOLVE:
		if (m_evPick > 0) { m_evPick--; if (m_evPick == 0) m_evTarget = -1; m_evPartner[0] = m_evPartner[1] = -1; }
		else if (m_evFromParty) { m_evFromParty = 0; EnterParty(); }
		else EnterBase();
		break;
	case SC_BATTLE:
		if (m_stashFull) { m_stashFull = 0; m_shopFit = 0; break; }
		BattleCancel();
		break;
	case SC_BASE:
		if (m_sortieAsk) m_sortieAsk = 0;
		else if (m_codex) m_codex = 0;
		else if (m_shop && m_shopFit == 13) m_shopFit = 4;
		else if (m_shop && (m_shopHold || m_shopFit)) { m_shopHold = 0; m_shopFit = 0; m_shopWho = -1; }
		else if (m_shop && m_shopBand) { m_shopBand = 0; m_shopPage = 0; }
		else if (m_shop && m_shopTier) { m_shopTier = 0; m_shopPage = 0; }
		else if (m_shop) {
			m_shop = 0; m_shopHold = 0; m_shopFit = 0; m_shopTier = 0; m_shopBand = 0; m_partyBack = 0; m_shopWho = -1;
			if (m_shopBack) { m_shopBack = 0; EnterParty(); }
		} else if (m_baseOpt) m_baseOpt = 0;
		break;
	default: break;
	}
}

// ============================================================ 通知
void Game::AddBanner(const wchar_t* s, const wchar_t* sub, DWORD col, int style, float life)
{
	Banner b = {};
	wcsncpy_s(b.s, s ? s : L"", _TRUNCATE);
	wcsncpy_s(b.sub, sub ? sub : L"", _TRUNCATE);
	b.col = col; b.style = style; b.life = life; b.t = 0;
	if (style == 0) for (auto& o : m_banners) if (o.style == 0) o.t = max(o.t, o.life - .2f);
	m_banners.push_back(b);
}

void Game::AddLog(const wchar_t* s, DWORD col)
{
	if (!s || !s[0]) return;
	if (m_logN >= 24) {
		memmove(&m_log[0], &m_log[1], sizeof(m_log[0]) * 23);
		m_logN = 23;
	}
	TacLog& L = m_log[m_logN++];
	wcsncpy_s(L.s, s, _TRUNCATE);
	L.col = col;
}
void Game::DrawBattleLog(float lx, float top, float lw, float lh)
{
	if (lw < U(80) || lh < U(40)) return;
	m_r.HudRect(lx, top, lw, lh, RGBA8(6, 10, 18, 150));
	m_r.HudFrame(lx, top, lw, lh, U(1.5f), RGBA8(220, 190, 120, 180));
	m_r.HudText(TT(TX_BT_LOG), lx + U(12), top + U(16), U(14), RGBA8(255, 226, 150), 0, 1);
	float y = top + U(34);
	int n = m_logN;
	int first = 0;
	float row = U(18);
	int fit = (int)((lh - U(44)) / row);
	if (fit < 1) fit = 1;
	if (n > fit) first = n - fit;
	for (int i = first; i < n; i++) {
		int age = n - 1 - i;
		int a = age == 0 ? 255 : (age < 4 ? 210 : 150);
		DWORD c = m_log[i].col;
		DWORD col = RGBA8(c & 255, (c >> 8) & 255, (c >> 16) & 255, a);
		m_r.HudText(m_log[i].s, lx + U(12), y + U(8), U(13), col, 0, 0);
		y += row;
	}
}
void Game::AddPopup(V3 p, const wchar_t* s, DWORD col, int style, float delay, float life)
{
	Popup q = {};
	q.p = p;
	wcsncpy_s(q.s, s, _TRUNCATE);
	q.col = col; q.style = style; q.life = life > 0 ? life : (style == 2 ? 1.6f : 1.25f); q.delay = delay;
	q.size = style == 2 ? 1.35f : 1.f;
	m_popups.push_back(q);
}

void Game::DrawPopups(float)
{
	float dt = m_renderDt;
	for (size_t i = 0; i < m_popups.size();) {
		Popup& p = m_popups[i];
		if (p.delay > 0) { p.delay -= dt; ++i; continue; }
		p.t += dt;
		if (p.t >= p.life) { m_popups.eraseAt((int)i); continue; }
		float sx, sy;
		if (m_r.ProjectToScreen(p.p, sx, sy)) {
			float u = p.t / p.life;
			float bounce = u < .18f ? sinf(u / .18f * PI) * U(18) : 0;
			float rise = u * U(36);
			float a = u > .7f ? 1.f - (u - .7f) / .3f : 1.f;
			float sc = (u < .1f ? Lerpf(1.6f, 1.f, u / .1f) : 1.f) * p.size;
			float px = (p.style == 1 ? U(17) : U(26)) * sc;
			m_r.HudText(p.s, sx, sy - rise - bounce, px, p.col, 1, 1, 1, a);
		}
		++i;
	}
}

void Game::DrawBanners(float)
{
	float dt = m_renderDt;
	float stackY = m_h * .2f;
	for (size_t i = 0; i < m_banners.size();) {
		Banner& b = m_banners[i];
		b.t += dt;
		if (b.t >= b.life) { m_banners.eraseAt((int)i); continue; }
		float in = Smooth01(b.t / .25f), out = 1.f - Smooth01((b.t - (b.life - .3f)) / .3f);
		float a = in * out;
		if (b.style == 0) {
			float h = U(64);
			float y = m_h * .36f;
			float slide = (1.f - in) * m_w * .4f - (1.f - out) * m_w * .4f;
			m_r.HudRectGrad(0, y, (float)m_w, h, RGBA8(0, 0, 0, (int)(170 * a)), RGBA8(0, 0, 0, (int)(110 * a)));
			m_r.HudRect(0, y, (float)m_w, U(2), RGBA8((b.col & 255), (b.col >> 8) & 255, (b.col >> 16) & 255, (int)(255 * a)));
			m_r.HudRect(0, y + h - U(2), (float)m_w, U(2), RGBA8((b.col & 255), (b.col >> 8) & 255, (b.col >> 16) & 255, (int)(255 * a)));
			m_r.HudText(b.s, m_w * .5f + slide, y + h * (b.sub[0] ? .38f : .5f), U(30), b.col, 1, 1, 1, a);
			if (b.sub[0]) m_r.HudText(b.sub, m_w * .5f - slide, y + h * .76f, U(15), RGBA8(235, 235, 240), 1, 0, 1, a);
		} else if (b.style == 1) {
			float sc = 1.f + (1.f - in) * .6f;
			m_r.HudText(b.s, m_w * .5f, m_h * .4f, U(64) * sc, b.col, 1, 1, 1, a);
			if (b.sub[0]) m_r.HudText(b.sub, m_w * .5f, m_h * .4f + U(52), U(20), RGBA8(240, 240, 240), 1, 0, 1, a);
		} else {
			float w = max(U(260), m_r.TextWidth(b.s, U(16), 1) + U(40));
			m_r.HudRect(m_w * .5f - w * .5f, stackY, w, U(32), RGBA8(10, 12, 24, (int)(200 * a)));
			m_r.HudFrame(m_w * .5f - w * .5f, stackY, w, U(32), 1, RGBA8(200, 170, 110, (int)(220 * a)));
			m_r.HudText(b.s, m_w * .5f, stackY + U(16), U(16), b.col, 1, 1, 1, a);
			stackY += U(38);
		}
		++i;
	}
}

void Game::DrawHelpOverlay()
{
	m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 150));
	float margin = max(8.f, min(U(24), m_h * .04f));
	float w = min((float)m_w - margin * 2.f, U(820));
	float h = min((float)m_h - margin * 2.f, U(560));
	if (w < 120.f) w = (float)m_w - 8.f;
	if (h < 100.f) h = (float)m_h - 8.f;
	float x = ((float)m_w - w) * .5f, y = ((float)m_h - h) * .5f;
	Panel(x, y, w, h);
	float title = min(U(24), h * .06f);
	float btnH = min(U(38), max(22.f, h * .1f));
	float btnW = min(U(160), w * .46f);
	float btnY = y + h - btnH - max(6.f, h * .03f);
	m_r.HudText(TT(TX_BT_HELP), x + w * .5f, y + title + 6.f, title, RGBA8(255, 226, 150), 1, 1);
	float boxY = y + title * 2.f + 10.f;
	float boxH = btnY - boxY - 8.f;
	if (boxH < 24.f) boxH = 24.f;
	m_r.HudTextBox(TT(TX_HELP_TEXT), x + max(8.f, w * .04f), boxY, w - max(16.f, w * .08f), boxH, min(U(15), h * .035f), RGBA8(236, 236, 240));
	if (Btn(ID_HELP_CLOSE, x + w * .5f - btnW * .5f, btnY, btnW, btnH, TT(TX_BASE_CLOSE))) m_showHelp = FALSE;
}

static_assert(TX_SD_PEN - TX_SD_HP + 1 == S_COUNT, "能力の説明は能力の数と一致");
void Game::DrawStatTip()
{
	if (m_statTip < 0 || m_statTip >= S_COUNT) return;
	float tw = min(U(420), (float)m_w - U(16));
	float th = U(88);
	float tx = (float)m_mx + U(14);
	float ty = (float)m_my + U(16);
	if (tx + tw > (float)m_w - U(6)) tx = (float)m_mx - tw - U(10);
	if (ty + th > (float)m_h - U(6)) ty = (float)m_my - th - U(8);
	if (tx < U(4)) tx = U(4);
	if (ty < U(4)) ty = U(4);
	Panel(tx, ty, tw, th);
	m_r.HudText(TT(StatNameTx(m_statTip)), tx + U(10), ty + U(16), U(14), RGBA8(255, 226, 150), 0, 1);
	m_r.HudTextBox(TT(TX_SD_HP + m_statTip), tx + U(10), ty + U(32), tw - U(20), U(48), U(13), RGBA8(230, 230, 236));
}

void Game::DrawSkillTip()
{
	if (m_skillTip < 0) return;
	wchar_t title[96], body[768], extra[160];
	title[0] = body[0] = extra[0] = 0;
	if (m_skillTip == 0) {
		const SkillDef& sd = SkillInfo(m_skillTipId);
		wcscpy_s(title, SkillLabel(m_skillTipId));
		wcscpy_s(body, TT(sd.descTx));
		if (sd.kind != SK_PASSIVE) {
			wchar_t tag[32];
			if (sd.shape == SH_LINE || sd.shape == SH_FAN) swprintf_s(tag, L"%s%d", CraftShapeName(sd.area, sd.shape), sd.area);
			else if (sd.area <= 0) wcscpy_s(tag, TT(TX_SHAPE_ONE));
			else if (sd.area == 1) wcscpy_s(tag, TT(TX_MISC_AREA));
			else swprintf_s(tag, L"%s%d", TT(TX_SHAPE_WIDE), sd.area);
			swprintf_s(extra, L"MP %d   %s", sd.mp, tag);
		}
	} else if (m_skillTip == 1) {
		const SkillDef& sd = SkillInfo(m_skillTipId);
		wcscpy_s(title, CraftName(m_skillTipId));
		wcscpy_s(body, CraftDesc(m_skillTipId));
		wchar_t cp[24];
		CraftCostText(m_skillTipId - CRAFT_BASE, cp, 24);
		swprintf_s(extra, L"%s  %s", CraftShapeName(sd.area, sd.shape), cp);
	} else if (m_skillTip == 2) {
		wcscpy_s(title, PasName(m_skillTipId));
		wcscpy_s(body, PasDesc(m_skillTipId));
	} else {
		wcscpy_s(title, SummonName(m_skillTipId - SUMMON_BASE + 1));
		wcscpy_s(body, SummonDesc(m_skillTipId - SUMMON_BASE + 1));
	}
	float tw = min(U(460), (float)m_w - U(16));
	float th = extra[0] ? U(128) : U(108);
	float tx = (float)m_mx + U(14);
	float ty = (float)m_my + U(16);
	if (tx + tw > (float)m_w - U(6)) tx = (float)m_mx - tw - U(10);
	if (ty + th > (float)m_h - U(6)) ty = (float)m_my - th - U(8);
	if (tx < U(4)) tx = U(4);
	if (ty < U(4)) ty = U(4);
	Panel(tx, ty, tw, th);
	m_r.HudText(title, tx + U(10), ty + U(16), U(14), RGBA8(255, 226, 150), 0, 1);
	float by = ty + U(30);
	if (extra[0]) {
		m_r.HudText(extra, tx + U(10), by, U(12), RGBA8(255, 210, 140), 0, 0);
		by += U(16);
	}
	m_r.HudTextBox(body, tx + U(10), by, tw - U(20), th - (by - ty) - U(8), U(13), RGBA8(230, 230, 236));
}

float Game::DrawStatLines(const CharData& c, float x, float y, float w, const int* gains, int hpCur, int mpCur)
{
	int cols = w >= U(340) ? 4 : 2;
	float lh = U(cols == 4 ? 20.f : 18.f);
	float cw = w / (float)cols;
	for (int s = 0; s < S_COUNT; s++) {
		float col = (float)(s % cols), row = (float)(s / cols);
		float sx = x + col * cw, sy = y + row * lh;
		BOOL hov = m_mouseSeen && Hit(sx, sy, cw - U(2), lh);
		if (hov) {
			m_statTip = s;
			m_r.HudRect(sx, sy, cw - U(2), lh, RGBA8(255, 220, 140, 36));
		}
		m_r.HudText(TT(StatNameTx(s)), sx, sy + lh * .5f, U(14), hov ? RGBA8(255, 236, 180) : RGBA8(200, 190, 160), 0, 0);
		wchar_t v[32];
		if (s == S_HP && hpCur >= 0) swprintf_s(v, L"%s/%s", TacNum(hpCur), TacNum(c.stat[s]));
		else if (s == S_MP && mpCur >= 0) swprintf_s(v, L"%s/%s", TacNum(mpCur), TacNum(c.stat[s]));
		else swprintf_s(v, L"%s", TacNum(c.stat[s]));
		float edge = sx + w / (float)cols - U(4);
		float numEdge = (gains && gains[s] != 0) ? edge - U(46) : edge;
		m_r.HudText(v, numEdge, sy + lh * .5f, U(15), RGBA8(250, 250, 250), 2, 1);
		if (gains && gains[s] != 0) {
			wchar_t g[16];
			swprintf_s(g, L"%+d", gains[s]);
			m_r.HudText(g, edge, sy + lh * .5f, U(13), gains[s] > 0 ? RGBA8(120, 255, 140) : RGBA8(255, 120, 110), 2, 1);
		}
	}
	return lh * (float)((S_COUNT + cols - 1) / cols);
}

// ============================================================ ポートレート
void Game::InvalidatePortraits()
{
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) m_portraitKey[i] = -1;
}

void Game::RenderActorPortrait(int slot, Actor& a, DWORD bg0, DWORD bg1, int fitBody)
{
	ActorPlay(a, CL_IDLE, 1, 0);
	a.t = .4f;
	ActorPose(a, m_lib, m_r);
	M4 world = ActorWorld(a, v3(0, 0, 0), PI);
	V3 look;
	float d;
	V3 eye;
	BOOL body = FALSE;
	float midY = a.height * .5f;
	float rad = a.height * 1.05f;
	look = v3(0, midY, 0);
	if (fitBody) {
		float cx = 0, cy = 0, cz = 0, cr = 0;
		float posedR = 0.f;
		BOOL posed = a.proc == 3 && a.poseValid && m_r.CreaturePosedExtent(a.model, a.pose, cx, cy, cz, cr);
		if (posed) posedR = cr * fabsf(a.scale);
		// バインド寸法は数センチのまま、描画は骨格の倍率で広がる。その寸法で枠を切るとカメラが体内に入る。
		if (posed && posedR > 0.15f && posedR < 12.f) {
			look = MXformPt(world, v3(cx, cy, cz));
			rad = posedR;
			body = TRUE;
		} else if (a.height >= 1.15f || a.proc == 2) {
			look = v3(0, midY, 0);
			body = TRUE;
		}
	}
	if (body) {
		d = rad * 1.28f / tanf(.3f);
		if (fitBody && !(a.proc == 3)) look = v3(0, midY, 0);
		eye = look + v3(d * .2f, d * .05f, -d);
	} else if (a.proc == 3) {
		look = ActorJointWorld(a, world, 0);
		float ht = a.htScale > 0.05f ? a.htScale : 1.f;
		look.x *= ht; look.y *= ht; look.z *= ht;
		d = (a.height / ht) * .62f;
		eye = look + v3(d * .08f, d * .02f, -d);
	} else if (!a.proc) {
		look = ActorJointWorld(a, world, 0);
		d = a.height * .95f;
		eye = look + v3(d * .3f, d * .12f, -d);
	} else {
		look = ActorJointWorld(a, world, 0);
		look.y = Lerpf(look.y, a.height * .55f, .5f);
		d = a.height * 1.35f;
		eye = look + v3(d * .3f, d * .12f, -d);
	}
	M4 view = MLookAtLH(eye, look, v3(0, 1, 0));
	float zn = d * 0.04f;
	if (zn < 0.02f) zn = 0.02f;
	if (zn > 0.25f) zn = 0.25f;
	M4 proj = MPerspLH(.6f, 1.f, zn, max(50.f, d * 6.f));
	if (!m_r.BeginPortrait(slot)) return;
	m_r.SetCamera(view, proj, eye, m_time);
	m_r.SetFog(500.f, 1000.f, 0, RGBA8(0, 0, 0));
	m_r.SetAmbient(RGBA8(206, 192, 178), RGBA8(148, 116, 94), .58f, .55f);
	m_r.SetLight(Norm(v3(.42f, .84f, -.40f)), look, 3.5f);
	m_r.SetShadowEnabled(TRUE);
	DrawParams dp;
	dp.outline = .0035f;
	dp.rim = .58f;
	m_r.BeginShadowPass();
	ActorDraw(m_r, m_lib, a, world, dp, 0);
	m_r.BeginMainPass();
	m_r.DrawSky(bg0, bg1, 0, 0);
	ActorDraw(m_r, m_lib, a, world, dp, 0);
	m_r.EndPortrait();
}

void Game::RenderPortraits()
{
	int budget = 8;
	for (int s = 0; s < Renderer::PORTRAIT_N && budget > 0; s++) {
		int want = m_portraitWant[s];
		if (want == m_portraitKey[s]) continue;
		m_portraitKey[s] = want;
		if (want <= 0) continue;
		if (want >= 100 && want < 100 + MAX_UNITS) {
			const Unit& u = m_b.u[want - 100];
			if (u.team == TEAM_PLAYER) {
				ActorFromChar(m_ptActor, m_lib, m_r, u.ch);
				RenderActorPortrait(s, m_ptActor, RGBA8(70, 110, 190), RGBA8(20, 30, 70));
			} else {
				const MonsterDef& md = MonsterInfo(u.monster);
				ActorFromMonster(m_ptActor, m_lib, m_r, u.monster, u.boss);
				if (md.look == LK_MESH && m_ptActor.proc != 3) {
					m_portraitKey[s] = -1;
					continue;
				}
				RenderActorPortrait(s, m_ptActor, u.boss ? RGBA8(200, 140, 40) : RGBA8(180, 60, 60), RGBA8(50, 14, 20), 1);
			}
		} else if (want >= 1 && want <= MAX_ROSTER) {
			const CharData& c = m_save.roster[want - 1];
			if (!c.used) continue;
			ActorFromChar(m_ptActor, m_lib, m_r, c);
			RenderActorPortrait(s, m_ptActor, RGBA8(70, 110, 190), RGBA8(20, 30, 70));
		}
		budget--;
	}
}

int Game::RosterPortrait(int roster) const
{
	if (roster < 0) return -1;
	int want = roster + 1;
	for (int s = 0; s < Renderer::PORTRAIT_N; s++) if (m_portraitWant[s] == want) return s;
	return -1;
}

void Game::PrepareMenuPortraits()
{
	int idx[Renderer::PORTRAIT_N];
	int n = 0;
	auto add = [&](int r) {
		if (r < 0 || r >= MAX_ROSTER || n >= Renderer::PORTRAIT_N) return;
		if (!m_save.roster[r].used) return;
		for (int i = 0; i < n; i++) if (idx[i] == r) return;
		idx[n++] = r;
	};
	if (m_screen == SC_BASE) {
		int cap = PartyCap();
		for (int i = 0; i < cap; i++) add(m_save.party[i]);
	} else if (m_screen == SC_PARTY) {
		add(m_partySel);
		int begin = m_rosterPage * 20;
		int cap = RosterCap();
		for (int i = 0; i < 20 && begin + i < cap; i++) add(begin + i);
	} else if (m_screen == SC_EVOLVE) {
		add(m_evMain);
		add(m_evPartner[0]);
		add(m_evPartner[1]);
		int cap = RosterCap();
		for (int i = 0; i < cap; i++) add(i);
	} else if (m_screen == SC_RESULT) {
		for (int i = 0; i < m_resultN; i++) add(m_resultRoster[i]);
	} else if (m_screen == SC_CREATE && m_editIdx >= 0) {
		add(m_editIdx);
	}
	for (int s = 0; s < Renderer::PORTRAIT_N; s++)
		m_portraitWant[s] = s < n ? idx[s] + 1 : 0;
}

void Game::DrawPortrait(int slot, float x, float y, float s, DWORD frame)
{
	if (slot < 0 || slot >= Renderer::PORTRAIT_N || m_portraitKey[slot] <= 0) {
		m_r.HudRect(x, y, s, s, RGBA8(30, 34, 50, 220));
	} else {
		float u0 = (slot % 6) / 6.f, v0 = (slot / 6) / 4.f;
		m_r.HudImage(m_r.PortraitSrv(), x, y, s, s, u0, v0, u0 + 1.f / 6.f, v0 + .25f, 0xFFFFFFFF);
	}
	m_r.HudFrame(x, y, s, s, max(1.f, U(2)), frame);
}

// ============================================================ 粒子
void Game::Burst(V3 p, int n, float spd, float size, float life, float r, float g, float b, int shape, int add, float grav, float stretch, float upBias, float size1)
{
	for (int i = 0; i < n; i++) {
		Particle q = {};
		V3 d = Norm(v3(m_rng.F(-1, 1), m_rng.F(-1, 1) + upBias, m_rng.F(-1, 1)));
		q.p = p;
		q.v = d * (spd * m_rng.F(.4f, 1.f));
		q.life = q.max = life * m_rng.F(.7f, 1.2f);
		q.size = size * m_rng.F(.7f, 1.2f);
		q.size1 = size1 >= 0 ? size1 : q.size * .2f;
		q.r = r; q.g = g; q.b = b; q.a = 1;
		q.shape = shape; q.add = add; q.grav = grav; q.stretch = stretch;
		q.drag = 1.6f;
		m_parts.push_back(q);
	}
}

void Game::UpdateParticles(float dt)
{
	for (size_t i = 0; i < m_parts.size();) {
		Particle& p = m_parts[i];
		p.life -= dt;
		if (p.life <= 0) { m_parts[i] = m_parts.back(); m_parts.pop_back(); continue; }
		p.v.y -= p.grav * dt;
		p.v = p.v * expf(-p.drag * dt);
		p.p = p.p + p.v * dt;
		++i;
	}
	if (m_shake > 0) m_shake = max(0.f, m_shake - dt * 2.5f);
}

void Game::ElementFx(int el, int kind, V3 from, V3 to, BOOL big)
{
	float k = big ? 1.6f : 1.f;
	int n = (int)(24 * k);
	switch (el) {
	case EL_FIRE:
		for (int i = 0; i < 8; i++) {
			Particle q = {};
			q.p = to + v3(m_rng.F(-.45f, .45f), .05f, m_rng.F(-.45f, .45f));
			q.v = v3(m_rng.F(-.4f, .4f), m_rng.F(1.4f, 3.2f), m_rng.F(-.4f, .4f));
			q.life = q.max = m_rng.F(0.7f, 1.35f);
			q.size = .28f * k; q.size1 = .06f;
			q.r = 1; q.g = m_rng.F(.25f, .55f); q.b = .05f; q.a = 1;
			q.shape = 5; q.add = 1; q.grav = -0.4f; q.drag = .6f;
			m_parts.push_back(q);
		}
		Burst(to, n, 2.2f * k, .16f, .8f, 1.f, .75f, .2f, 0, 1, -2.2f, 0, 1.1f, .04f);
		Burst(to + v3(0, .05f, 0), 2, .2f, 1.1f * k, .55f, 1.f, .35f, .05f, 1, 1, 0, 0, 0, 1.6f * k);
		break;
	case EL_ICE:
		Burst(to, n, 3.f * k, .22f * k, .9f, .6f, .85f, 1.f, 2, 1, 2.f, 0, .4f);
		Burst(to, n / 2, 5.f * k, .08f, .7f, .85f, .95f, 1.f, 3, 1, 4.f, .05f, .5f);
		Burst(to, 2, .2f, .9f * k, .5f, .7f, .9f, 1.f, 1, 1, 0, 0, 0, 1.6f * k);
		break;
	case EL_THUNDER:
		for (int bolt = 0; bolt < (big ? 3 : 2); bolt++) {
			float ox = m_rng.F(-.7f, .7f), oz = m_rng.F(-.7f, .7f);
			for (int s = 0; s < 7; s++) {
				Particle q = {};
				float y = 5.2f - s * .72f;
				q.p = to + v3(ox + m_rng.F(-.18f, .18f), y, oz + m_rng.F(-.18f, .18f));
				q.v = v3(0, -6.f, 0);
				q.life = q.max = .16f + s * .02f;
				q.size = .22f; q.size1 = .08f;
				q.r = .85f; q.g = .92f; q.b = 1.f; q.a = 1;
				q.shape = 6; q.add = 1; q.stretch = .04f; q.drag = 0;
				m_parts.push_back(q);
			}
		}
		Burst(to, 3, .15f, 1.4f * k, .22f, .9f, .95f, 1.f, 1, 1, 0, 0, 0, 2.2f * k);
		Burst(to, n, 7.f * k, .08f, .35f, 1.f, 1.f, .75f, 3, 1, 1.f, .02f, .2f);
		m_flashWhite = max(m_flashWhite, big ? .85f : .45f);
		m_shake = max(m_shake, big ? .7f : .35f);
		break;
	case EL_HOLY:
		Burst(to, n, 2.f * k, .18f, 1.2f, 1.f, .95f, .7f, 2, 1, -2.5f, 0, 1.f);
		for (int i = 0; i < 3; i++) Burst(to + v3(0, i * .4f, 0), 1, 0, .4f, .7f + i * .15f, 1.f, .92f, .6f, 1, 1, 0, 0, 0, 1.4f * k);
		break;
	case EL_DARK:
		Burst(to, n, 2.6f * k, .3f, .9f, .55f, .2f, .8f, 0, 1, -1.f, 0, .3f, .02f);
		Burst(to, 2, .1f, 1.4f * k, .6f, .6f, .2f, .9f, 1, 1, 0, 0, 0, .2f);
		break;
	case EL_WIND:
		Burst(to, n, 4.f * k, .5f, .6f, .6f, 1.f, .7f, 4, 1, 0, 0, .2f);
		break;
	case EL_EARTH:
		Burst(to, n, 4.5f * k, .14f, 1.f, .55f, .42f, .3f, 0, 0, 9.f, 0, 1.2f);
		Burst(to, n / 2, 1.5f, .5f, .9f, .6f, .55f, .45f, 0, 0, -.3f, 0, .2f, 1.f);
		break;
	case EL_POISON:
		Burst(to, n, 1.8f * k, .2f, 1.1f, .6f, .9f, .3f, 1, 1, -1.5f, 0, .8f);
		Burst(to, n / 2, 1.2f * k, .14f, 1.3f, .7f, .3f, .9f, 0, 1, -1.f, 0, .6f);
		break;
	default:
		if (kind == SK_HEAL) {
			Burst(to, n, 1.6f, .16f, 1.2f, .5f, 1.f, .6f, 2, 1, -2.5f, 0, 1.2f);
			Burst(to, 2, 0, .7f, .7f, .5f, 1.f, .7f, 1, 1, 0, 0, 0, 1.3f);
		} else if (kind == SK_BUFF) {
			Burst(to, n, 1.6f, .16f, 1.2f, .5f, .8f, 1.f, 2, 1, -2.f, 0, 1.2f);
			for (int i = 0; i < 3; i++) Burst(to + v3(0, i * .3f, 0), 1, 0, .5f, .8f, 1.f, .85f, .4f, 1, 1, 0, 0, 0, 1.0f);
		} else {
			Burst(to, n, 5.f * k, .07f, .35f, 1.f, .95f, .8f, 3, 1, 5.f, .05f, .3f);
			Burst(to, 1, 0, .9f * k, .22f, 1.f, 1.f, 1.f, 4, 1, 0, 0, 0, 1.3f * k);
		}
		break;
	}
	(void)from;
}

void Game::StrikeFx(int skill, int el, int kind, int atkStyle, V3 from, V3 to, BOOL big)
{
	float k = big ? 1.5f : 1.f;
	auto slash = [&](float yaw) {
		for (int i = 0; i < 7; i++) {
			Particle q = {};
			float a = yaw + (i - 3) * .22f;
			q.p = to + v3(sinf(a) * .15f, .35f + i * .02f, cosf(a) * .15f);
			q.v = v3(sinf(a) * 3.5f, .2f, cosf(a) * 3.5f);
			q.life = q.max = .28f; q.size = .34f * k; q.size1 = .08f;
			q.r = 1; q.g = .95f; q.b = .8f; q.a = 1; q.shape = 4; q.add = 1;
			m_parts.push_back(q);
		}
	};
	if (skill == SKL_THUNDER || skill == SKL_THUNDERFIELD || skill == SKL_SMITE || (el == EL_THUNDER && kind != SK_PHYS)) {
		ElementFx(EL_THUNDER, SK_MAGIC, from, to, big); return;
	}
	if (skill == SKL_METEOR || skill == SKL_APOC || skill == SKL_STORM) {
		for (int i = 0; i < 6; i++) {
			Particle q = {};
			q.p = to + v3(m_rng.F(-1.2f, 1.2f), 4.5f, m_rng.F(-1.2f, 1.2f));
			q.v = v3(0, -9.f, 0);
			q.life = q.max = .45f; q.size = .4f; q.size1 = .1f;
			q.r = 1; q.g = .4f; q.b = .08f; q.a = 1; q.shape = 5; q.add = 1; q.grav = 4.f;
			m_parts.push_back(q);
		}
		ElementFx(EL_FIRE, SK_MAGIC, from, to, TRUE); return;
	}
	if (skill == SKL_BLIZZARD || skill == SKL_FROSTFIELD || (skill == SKL_GRAVITY && el == EL_ICE)) {
		for (int i = 0; i < 16; i++) {
			Particle q = {};
			q.p = to + v3(m_rng.F(-1.6f, 1.6f), m_rng.F(1.5f, 3.5f), m_rng.F(-1.6f, 1.6f));
			q.v = v3(m_rng.F(-.4f, .4f), -2.5f, m_rng.F(-.4f, .4f));
			q.life = q.max = .8f; q.size = .16f; q.size1 = .04f;
			q.r = .75f; q.g = .9f; q.b = 1; q.a = 1; q.shape = 7; q.add = 1; q.grav = 3.f;
			m_parts.push_back(q);
		}
		ElementFx(EL_ICE, kind, from, to, big); return;
	}
	if (skill == SKL_ARROWRAIN || skill == SKL_SHOWER || skill == SKL_SHINE) {
		for (int i = 0; i < 10; i++) {
			Particle q = {};
			q.p = to + v3(m_rng.F(-1.4f, 1.4f), 3.2f, m_rng.F(-1.4f, 1.4f));
			q.v = v3(0, -8.f, 0);
			q.life = q.max = .35f; q.size = .08f; q.size1 = .04f;
			q.r = .9f; q.g = .85f; q.b = .6f; q.a = 1; q.shape = 3; q.add = 1; q.stretch = .05f;
			m_parts.push_back(q);
		}
		return;
	}
	if (skill == SKL_WHIRL || skill == SKL_CLEAVE || skill == SKL_CRESCENT || skill == SKL_GRANDSLASH || atkStyle == AS_SPIN) {
		Burst(to, 3, .2f, .9f * k, .4f, 1, .95f, .85f, 1, 1, 0, 0, 0, 1.8f * k);
		slash(0); slash(2.1f); return;
	}
	if (skill == SKL_NOVA) { ElementFx(EL_HOLY, SK_MAGIC, from, to, TRUE); return; }
	if (kind == SK_HEAL || skill == SKL_HEAL || skill == SKL_HIGHHEAL || skill == SKL_HEALALL || skill == SKL_RENEW || skill == SKL_RAISE) {
		ElementFx(EL_NONE, SK_HEAL, from, to, big); return;
	}
	if (kind == SK_BUFF) { ElementFx(el == EL_NONE ? EL_HOLY : el, SK_BUFF, from, to, big); return; }
	if (kind == SK_MAGIC || atkStyle == AS_CAST) { ElementFx(el == EL_NONE ? EL_HOLY : el, SK_MAGIC, from, to, big); return; }
	if (atkStyle == AS_CHOP2 || skill == SKL_BASH || skill == SKL_POWER) {
		for (int i = 0; i < 6; i++) {
			Particle q = {};
			q.p = to + v3(m_rng.F(-.2f, .2f), 1.6f - i * .05f, m_rng.F(-.2f, .2f));
			q.v = v3(0, -4.f, 0);
			q.life = q.max = .22f; q.size = .4f; q.size1 = .08f;
			q.r = 1; q.g = .9f; q.b = .7f; q.a = 1; q.shape = 3; q.add = 1; q.stretch = .06f;
			m_parts.push_back(q);
		}
		Burst(to, 8, 3.f, .1f, .3f, .8f, .75f, .65f, 0, 0, 6.f, 0, .4f);
		return;
	}
	if (atkStyle == AS_STAB || skill == SKL_PIERCE || skill == SKL_ASSASSINATE || skill == SKL_STITCH) {
		V3 dir = Norm(to - from);
		for (int i = 0; i < 6; i++) {
			Particle q = {};
			q.p = Lerp(from, to, .3f + i * .1f);
			q.v = dir * 6.f;
			q.life = q.max = .16f; q.size = .12f; q.size1 = .04f;
			q.r = 1; q.g = .95f; q.b = .85f; q.a = 1; q.shape = 3; q.add = 1; q.stretch = .03f;
			m_parts.push_back(q);
		}
		return;
	}
	if (atkStyle == AS_SHOOT || atkStyle == AS_SHOOT1) {
		Burst(to, 6, 2.f, .08f, .25f, 1, .9f, .6f, 3, 1, 1.f, .04f);
		return;
	}
	if (atkStyle == AS_PUNCH) {
		Burst(to, 2, .1f, .7f * k, .25f, 1, .85f, .6f, 1, 1, 0, 0, 0, 1.4f);
		Burst(to, 10, 4.f, .08f, .3f, 1, .8f, .55f, 0, 0, 2.f, 0, .3f);
		return;
	}
	if (atkStyle == AS_BITE) {
		Burst(to, 12, 2.5f, .12f, .35f, .9f, .2f, .15f, 0, 1, 1.f, 0, .2f);
		return;
	}
	slash(atan2f(to.x - from.x, to.z - from.z));
	Burst(to, 6, 3.f, .08f, .25f, 1, .95f, .85f, 3, 1, 2.f, .04f);
}

// ============================================================ 拠点
void Game::RollMapSeed(int stage, BOOL force)
{
	stage = Clampi(stage, 1, TAC_STAGE_MAX);
	if (!force && m_mapSeed != 0 && m_mapSeedStage == stage) return;
	DWORD s = m_rng.Next() ^ GetTickCount() ^ (DWORD)(stage * 2654435761u) ^ (DWORD)(m_time * 1000.f);
	if (s == 0 || s == m_mapSeed) s ^= 0x9E3779B9u;
	if (s == 0) s = 1;
	m_mapSeed = s;
	m_mapSeedStage = stage;
	m_showBuilt = FALSE;
}

void Game::BuildShowcase()
{
	if (!m_loaded) return;
	int st = Clampi(m_save.stageSel, 1, TAC_STAGE_MAX);
	RollMapSeed(st, FALSE);
	if (!m_showBuilt || m_showStage != st || m_showTheme != m_themeSel) {
		GenerateMap(m_showMap, st, m_mapSeed, m_themeSel);
		m_showTheme = m_themeSel;
		m_r.BuildTerrain(m_showMap);
		m_showBuilt = TRUE;
		m_showStage = st;
	}
	m_showN = 0;
	int want = 0;
	for (int i = 0; i < MAX_PARTY; i++) {
		int r = m_save.party[i];
		if (r >= 0 && r < MAX_ROSTER && m_save.roster[r].used) want++;
	}
	int got = 0;
	const int spanA = m_loadA, spanB = m_loadB;
	const BOOL slicing = m_loadLive && spanB > spanA && want > 0;
	for (int i = 0; i < MAX_PARTY; i++) {
		int r = m_save.party[i];
		if (r < 0 || r >= MAX_ROSTER || !m_save.roster[r].used) continue;
		if (slicing) {
			m_loadA = spanA + (spanB - spanA) * got / want;
			m_loadB = spanA + (spanB - spanA) * (got + 1) / want;
		}
		ActorFromChar(m_showAct[m_showN], m_lib, m_r, m_save.roster[r]);
		if (slicing) NoteLoad(m_loadB);
		got++;
		ActorPlay(m_showAct[m_showN], CL_IDLE, 1, 0);
		m_showAct[m_showN].t = m_rng.F(0, 2);
		int px = m_showMap.pStartX[i], pz = m_showMap.pStartZ[i];
		m_showPos[m_showN] = v3(px + .5f, m_showMap.StandY(px, pz) + m_showAct[m_showN].hover, pz + .5f);
		m_showN++;
	}
	if (slicing) { m_loadA = spanA; m_loadB = spanB; }
}

void Game::EnterBase()
{
	m_r.LoadBattleCreatures(NULL, 0);
	m_importBind = 0;
	m_screen = SC_BASE;
	m_shop = 0;
	m_shopFit = 0;
	m_shopHold = 0;
	m_partyBack = 0;
	m_shopBack = 0;
	m_focusId = ID_BASE_SORTIE;
	m_confirmDelete = 0;
	m_sortieAsk = 0;
	ProbeSuspend();
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) m_portraitWant[i] = (i < MAX_ROSTER) ? i + 1 : 0;
	InvalidatePortraits();
	BuildShowcase();
	m_zoomHold = FALSE;
	m_wheelSign = 0;
	m_camDistGoal = m_camDist = m_zoomDist = 12.f;
	m_camPitch = .5f;
}

void Game::UpdateBase(float dt)
{
	m_showYaw += dt * .08f;
	if (m_codex == 4) {
		float d = m_camYawGoal - m_camYaw;
		const float pi = 3.14159265f;
		while (d > pi) d -= pi * 2.f;
		while (d < -pi) d += pi * 2.f;
		m_camYaw += d * Clampf(dt * 6.f, 0.f, 1.f);
		if (!m_monDrag) m_monYaw += dt * 0.35f;
		EnsureMonPreview();
		if (m_monShown >= 0) ActorUpdate(m_monAct, dt);
	} else if (m_monShown != -2) {
		m_r.LoadBattleCreatures(NULL, 0);
		m_monShown = -2;
	}
	for (int i = 0; i < m_showN; i++) ActorUpdate(m_showAct[i], dt);
	if (m_screen == SC_CREATE) ActorUpdate(m_preview, dt);
	if (m_zoomHold) ApplyZoomHold(dt);
	else m_camDist = Lerpf(m_camDist, m_camDistGoal, Clampf(dt * 5.f, 0, 1));
}

void Game::DrawShowcase3D()
{
	if (m_screen == SC_BASE && m_codex == 4) { DrawMonStage(); return; }
	if (m_screen != SC_BATTLE && m_screen != SC_RESULT) {
		int st = Clampi(m_save.stageSel, 1, TAC_STAGE_MAX);
		if (!m_showBuilt || m_showStage != st) BuildShowcase();
	}
	Env e;
	ThemeEnv(m_showMap.theme, e);
	V3 c = v3(32, 0, 32);
	if (m_showN > 0) {
		c = v3(0, 0, 0);
		for (int i = 0; i < m_showN; i++) c = c + m_showPos[i];
		c = c * (1.f / m_showN);
	} else {
		int px = m_showMap.pStartX[0], pz = m_showMap.pStartZ[0];
		c = v3(px + .5f, m_showMap.StandY(px, pz), pz + .5f);
	}
	V3 target = c + v3(0, .8f, 0);
	float yaw = m_showYaw;
	ApplyZoomHold();
	SetupCamera(target, yaw, m_camPitch, m_camDist, .7f);
	m_r.SetFog(m_camDist * 1.6f, m_camDist * 5.5f, .3f, e.fog);
	m_r.SetAmbient(e.ambSky, e.ambGnd, e.amb, e.hard);
	m_r.SetLight(e.light, c, 18.f);
	m_r.BeginShadowPass();
	m_r.DrawTerrain();
	DrawParams dp;
	for (int i = 0; i < m_showN; i++) {
		Actor& a = m_showAct[i];
		ActorPose(a, m_lib, m_r);
		float face = atan2f(target.x - m_showPos[i].x + sinf(yaw) * 10.f, target.z - m_showPos[i].z + cosf(yaw) * 10.f);
		ActorDraw(m_r, m_lib, a, ActorWorld(a, m_showPos[i], face), dp, m_time);
	}
	m_r.BeginMainPass();
	m_r.DrawSky(e.skyTop, e.skyBot, 0, 0);
	m_r.DrawTerrain();
	m_r.DrawWater();
	for (int i = 0; i < m_showN; i++) {
		Actor& a = m_showAct[i];
		float face = atan2f(target.x - m_showPos[i].x + sinf(yaw) * 10.f, target.z - m_showPos[i].z + cosf(yaw) * 10.f);
		DrawParams d = dp;
		d.outline = .014f;
		ActorDraw(m_r, m_lib, a, ActorWorld(a, m_showPos[i], face), d, m_time);
	}
	m_r.DrawParticles(m_parts.data(), m_parts.size());
}

struct CodexHit {
	int skill, lv, mp, key;
	wchar_t how[160];
	wchar_t name[80];
};
static int s_codexSort;
static int CodexAreaCap(const ClassDef& cd)
{
	if (cd.tier < 3) return 0;
	int rad = cd.tier - 1;
	if (rad > 9) rad = 9;
	if (cd.group != G_HOLY && rad > 3) rad = 3;
	return rad;
}
static int CodexJobKey(int cls)
{
	const ClassDef& cd = ClassInfo(cls);
	int g = cd.group;
	if (g < 0 || g >= G_COUNT) g = 0;
	int tier = cd.tier;
	if (tier < 0) tier = 0;
	if (tier > 12) tier = 12;
	int id = cls < 0 ? 0 : cls;
	if (id > 200) id = 200;
	return g * 100000 + tier * 1000 + id;
}
static int CodexLearnLv(int cls, int skill)
{
	const ClassDef& cd = ClassInfo(cls);
	int lv = 0;
	for (int k = 0; k < LEARN_N; k++)
		if (cd.learn[k].skill == skill && cd.learn[k].lv > 0) lv = cd.learn[k].lv;
	if (lv > 0) return lv;
	if (skill == SKL_RAISE && cd.group == G_HOLY) return 7;
	if (skill >= SKL_ART0 && skill < SKL_ARTN) {
		int g = (skill - SKL_ART0) / 26;
		int alv = (skill - SKL_ART0) % 26 + 7;
		if (cd.group == g && PaceRoleOfLv(alv) >= 0) return alv;
	}
	if (skill >= SKL_AREAHEAL0 && skill < SKL_FORM0) {
		int cap = CodexAreaCap(cd);
		if (cap >= 2 && (skill == SKL_AREAHEAL0 + (cap - 2) || skill == SKL_AREARAISE0 + (cap - 2)))
			return PromoteLevel(cd.tier - 1);
	}
	if (cd.magic && (cd.group == G_HOLY || cd.group == G_MAGIC) && cd.tier >= 2) {
		if (skill == SKL_FIRE || skill == SKL_BLIZZARD || skill == SKL_THUNDER || skill == SKL_HOLY
			|| skill == SKL_DARKNESS || skill == SKL_GRAVITY || skill == SKL_GALE || skill == SKL_MIASMA)
			return PromoteLevel(1);
	}
	if (skill == SKL_GALE && cd.magic && cd.group == G_MAGIC) return 5;
	if (!cd.magic && cd.tier >= 3 && skill >= SKL_ELFIRE && skill <= SKL_ELPOISON)
		return PromoteLevel(2);
	return 0;
}
static int CodexJobList(int* dst, int cap)
{
	struct E { int cls, g, tier; };
	E e[40];
	int n = 0;
	for (int c = 0; c < C_PLAYER_COUNT; c++) {
		if (n >= 40) break;
		const ClassDef& cd = ClassInfo(c);
		e[n].cls = c; e[n].g = cd.group < 0 ? 0 : cd.group; e[n].tier = cd.tier; n++;
	}
	int extra[2] = { C_GLADIATOR, C_SHADOW };
	for (int i = 0; i < 2 && n < 40; i++) {
		const ClassDef& cd = ClassInfo(extra[i]);
		e[n].cls = extra[i]; e[n].g = cd.group < 0 ? 0 : cd.group; e[n].tier = cd.tier; n++;
	}
	for (int i = 1; i < n; i++) {
		E v = e[i];
		int j = i;
		while (j > 0) {
			const E& p = e[j - 1];
			if (!(p.g > v.g || (p.g == v.g && (p.tier > v.tier || (p.tier == v.tier && p.cls > v.cls))))) break;
			e[j] = p;
			j--;
		}
		e[j] = v;
	}
	if (n > cap) n = cap;
	for (int i = 0; i < n; i++) dst[i] = e[i].cls;
	return n;
}
static void CodexFillHow(int skill, wchar_t* how, int howN, int* key, int* lv)
{
	how[0] = 0;
	*key = 0;
	*lv = 0;
	for (int c = 0; c < C_PLAYER_COUNT && how[0] == 0; c++) {
		int got = 0;
		const ClassDef& cd = ClassInfo(c);
		for (int k = 0; k < LEARN_N; k++) if (cd.learn[k].skill == skill && cd.learn[k].lv > 0) got = cd.learn[k].lv;
		if (got > 0) {
			swprintf_s(how, howN, L"%s  Lv%d", TT(cd.nameTx), got);
			*key = CodexJobKey(c);
			*lv = got;
		}
	}
	if (how[0] == 0 && skill >= SKL_ELFIRE && skill <= SKL_ELPOISON) {
		swprintf_s(how, howN, L"%s  Lv%d", LangPick14(L"物理職3次", L"Physical tier 3", L"Physique rang 3", L"Fisico grado 3", L"Físico grado 3", L"물리 3차", L"物理三职", L"جسدي رتبة 3", L"Физика 3", L"Physisch Rang 3", L"Físico grau 3", L"Fysiek rang 3", L"Fizyczny stopień 3", L"Fiziksel kademe 3"), PromoteLevel(2));
		*lv = PromoteLevel(2);
		*key = 800000 + skill;
	}
	if (how[0] == 0 && (skill == SKL_GALE || skill == SKL_MIASMA)) {
		swprintf_s(how, howN, L"%s  Lv%d", LangPick14(L"魔法・聖職2次", L"Caster tier 2", L"Lanceur rang 2", L"Lanciatore grado 2", L"Conjurador grado 2", L"마법·성직 2차", L"魔法·圣职二职", L"ساحر رتبة 2", L"Магия 2", L"Zauberer Rang 2", L"Mago grau 2", L"Magie rang 2", L"Mag stopień 2", L"Büyücü kademe 2"), PromoteLevel(1));
		*lv = PromoteLevel(1);
		*key = 700000 + skill;
	}
	if (skill == SKL_RAISE) {
		swprintf_s(how, howN, L"%s  Lv7", TT(TX_G_HOLY));
		*key = G_HOLY * 100000 + 7;
		*lv = 7;
	}
	if (IsFormSkill(skill)) {
		swprintf_s(how, howN, L"%s", TT(TX_SK_TAB_FORM));
		*key = 900000;
		*lv = skill - SKL_FORM0;
	}
	if (skill >= SKL_AREAHEAL0 && skill < SKL_FORM0) {
		int rad = SkillInfo(skill).area;
		int show = -1, showTier = 99;
		for (int c = 0; c <= C_SHADOW; c++) {
			const ClassDef& cd = ClassInfo(c);
			if (cd.group != G_HOLY || cd.tier < 3 || cd.tier - 1 < rad) continue;
			if (cd.tier < showTier) { showTier = cd.tier; show = c; }
		}
		if (show >= 0) {
			const wchar_t* more = rad <= 3 ? LangPick14(L"・他職も3まで", L", others to 3", L", autres jusqu'à 3", L", altri fino a 3", L", otros hasta 3", L", 다른 직업도 3까지", L"，其他职到3", L"، غيرهم حتى 3", L", прочие до 3", L", andere bis 3", L", outros até 3", L", anderen tot 3", L", inne do 3", L", diğerleri 3'e") : L"";
			swprintf_s(how, howN, L"%s  Lv%d%s", TT(ClassInfo(show).nameTx), PromoteLevel(showTier - 1), more);
			*key = CodexJobKey(show);
			*lv = PromoteLevel(showTier - 1);
		}
	}
	if (skill >= SKL_ART0 && skill < SKL_ARTN) {
		int k = skill - SKL_ART0;
		int g = k / 26;
		int alv = (k % 26) + 7;
		if (g < 0 || g >= G_COUNT) g = 0;
		swprintf_s(how, howN, L"%s  Lv%d", TT(TX_G_MELEE + g), alv);
		*key = g * 100000 + 80000 + alv;
		*lv = alv;
	}
	if (how[0] == 0) {
		int extra[2] = { C_GLADIATOR, C_SHADOW };
		for (int e = 0; e < 2 && how[0] == 0; e++) {
			const ClassDef& cd = ClassInfo(extra[e]);
			for (int k = 0; k < LEARN_N; k++) if (cd.learn[k].skill == skill && cd.learn[k].lv > 0) {
				swprintf_s(how, howN, L"%s  Lv%d", TT(cd.nameTx), cd.learn[k].lv);
				*key = CodexJobKey(extra[e]);
				*lv = cd.learn[k].lv;
				break;
			}
		}
	}
	for (int line = 0; line < TAC_RANK_LINES && how[0] == 0; line++) {
		for (int r = 0; r < TAC_RANK_N && how[0] == 0; r++) {
			int cls = C_PLAYER_COUNT + line * TAC_RANK_N + r;
			const ClassDef& cd = ClassInfo(cls);
			for (int k = 0; k < LEARN_N; k++) if (cd.learn[k].skill == skill && cd.learn[k].lv > 0) {
				swprintf_s(how, howN, L"%s  Lv%d", TT(cd.nameTx), cd.learn[k].lv);
				*key = CodexJobKey(cls);
				*lv = cd.learn[k].lv;
				break;
			}
		}
	}
}
static int CmpCodex(const void* pa, const void* pb)
{
	const CodexHit* a = (const CodexHit*)pa;
	const CodexHit* b = (const CodexHit*)pb;
	int c = 0;
	if (s_codexSort == 1) c = wcscmp(a->name, b->name);
	else if (s_codexSort == 2) c = (a->mp > b->mp) - (a->mp < b->mp);
	else if (s_codexSort == 3) c = (a->lv > b->lv) - (a->lv < b->lv);
	else c = (a->key > b->key) - (a->key < b->key);
	if (!c && s_codexSort != 3) c = (a->lv > b->lv) - (a->lv < b->lv);
	if (!c && s_codexSort != 1) c = wcscmp(a->name, b->name);
	if (!c) c = (a->skill > b->skill) - (a->skill < b->skill);
	return c;
}
static const wchar_t* CodexWord(int w)
{
	switch (w) {
	case 0: return LangPick14(L"職", L"Job", L"Classe", L"Classe", L"Clase", L"직업", L"职业", L"الوظيفة", L"Класс", L"Klasse", L"Classe", L"Klasse", L"Klasa", L"Sınıf");
	case 1: return LangPick14(L"すべて", L"All", L"Tous", L"Tutti", L"Todos", L"전체", L"全部", L"الكل", L"Все", L"Alle", L"Todos", L"Alles", L"Wszystkie", L"Tümü");
	case 2: return LangPick14(L"並び", L"Sort", L"Tri", L"Ordine", L"Orden", L"정렬", L"排序", L"الترتيب", L"Порядок", L"Sortierung", L"Ordem", L"Sortering", L"Kolejność", L"Sıra");
	case 3: return LangPick14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad");
	default: return LangPick14(L"習得", L"Learned", L"Acquis", L"Appreso", L"Aprendido", L"습득", L"习得", L"الاكتساب", L"Изучение", L"Erlernt", L"Aprendido", L"Geleerd", L"Nauka", L"Öğrenim");
	}
}

static BOOL PreviewAlt(int style);
static void MonPreviewIn(int id, const Actor& a, int& style, BOOL& fly);

void Game::EnsureMonPreview()
{
	static int ids[256];
	int n = MonsterCatalog(ids, 256);
	if (n <= 0) { m_monShown = -1; return; }
	if (m_monSel < 0) m_monSel = 0;
	if (m_monSel >= n) m_monSel = n - 1;
	int id = ids[m_monSel];
	if (!MonsterKnown(id, m_save.stageCleared)) {
		if (m_monShown != -1) {
			m_r.LoadBattleCreatures(NULL, 0);
			m_monShown = -1;
		}
		return;
	}
	if (m_monShown == id) return;
	const MonsterDef& md = MonsterInfo(id);
	if (md.look == LK_MESH && md.model >= 0) {
		int pack[1] = { md.model };
		m_r.LoadBattleCreatures(pack, 1);
	} else m_r.LoadBattleCreatures(NULL, 0);
	ActorFromMonster(m_monAct, m_lib, m_r, id, 0);
	int style = 0;
	BOOL fly = FALSE;
	MonPreviewIn(id, m_monAct, style, fly);
	PlayShown(m_monAct, m_monMot, style, CL_IDLE, fly, FALSE, FALSE);
	m_monShown = id;
}

void Game::DrawMonStage()
{
	int st = Clampi(m_save.stageSel, 1, TAC_STAGE_MAX);
	if (!m_showBuilt || m_showStage != st) BuildShowcase();
	EnsureMonPreview();
	Env e;
	ThemeEnv(m_showMap.theme, e);
	int px = m_showMap.pStartX[0], pz = m_showMap.pStartZ[0];
	V3 c = v3(px + .5f, m_showMap.StandY(px, pz), pz + .5f);
	float ht = m_monShown >= 0 ? m_monAct.height : 1.7f;
	if (ht < 0.4f) ht = 0.4f;
	V3 target = c + v3(0, ht * .42f, 0);
	float dist = Clampf(ht * 3.2f, 5.2f, 16.f);
	if (m_zoomHold) dist = m_camDist;
	SetupCamera(target, m_showYaw * 0.15f + m_camYaw, m_camPitch > 0.15f ? m_camPitch : 0.55f, dist, .7f);
	m_r.SetFog(dist * 1.6f, dist * 5.5f, .3f, e.fog);
	m_r.SetAmbient(e.ambSky, e.ambGnd, e.amb, e.hard);
	m_r.SetLight(e.light, c, 18.f);
	m_r.BeginShadowPass();
	m_r.DrawTerrain();
	DrawParams dp;
	V3 pos = c;
	if (m_monShown >= 0) {
		pos.y += m_monAct.hover;
		ActorPose(m_monAct, m_lib, m_r);
		ActorDraw(m_r, m_lib, m_monAct, ActorWorld(m_monAct, pos, m_monYaw), dp, m_time);
	}
	m_r.BeginMainPass();
	m_r.DrawSky(e.skyTop, e.skyBot, 0, 0);
	m_r.DrawTerrain();
	m_r.DrawWater();
	if (m_monShown >= 0) {
		DrawParams d = dp;
		d.outline = .014f;
		ActorDraw(m_r, m_lib, m_monAct, ActorWorld(m_monAct, pos, m_monYaw), d, m_time);
	}
}

void Game::DrawMonCodex()
{
	static int ids[256];
	int n = MonsterCatalog(ids, 256);
	if (m_monSel < 0) m_monSel = 0;
	if (n > 0 && m_monSel >= n) m_monSel = n - 1;
	float x = U(24), y = U(24), w = min(U(520.f), (float)m_w * .46f), h = (float)m_h - U(108);
	Panel(x, y, w, h);
	const wchar_t* title = LangPick14(L"敵一覧", L"Foes", L"Ennemis", L"Nemici", L"Enemigos", L"적 목록", L"敌人一览", L"الأعداء", L"Враги", L"Gegner", L"Inimigos", L"Vijanden", L"Wrogowie", L"Düşmanlar");
	m_r.HudText(title, x + w * .5f, y + U(28), U(22), RGBA8(255, 226, 150), 1, 1);
	int known = 0;
	for (int i = 0; i < n; i++) if (MonsterKnown(ids[i], m_save.stageCleared)) known++;
	wchar_t sub[64];
	swprintf_s(sub, L"%d / %d", known, n);
	m_r.HudText(sub, x + w * .5f, y + U(52), U(14), RGBA8(190, 200, 220), 1, 0);
	float listTop = y + U(72);
	float listBot = y + h - U(236);
	int per = (int)((listBot - listTop) / U(28));
	if (per < 4) per = 4;
	int pages = max(1, (n + per - 1) / per);
	if (m_codexPage < 0) m_codexPage = 0;
	if (m_codexPage >= pages) m_codexPage = pages - 1;
	int a = m_codexPage * per, b = min(n, a + per);
	float yy = listTop;
	for (int i = a; i < b; i++) {
		BOOL seen = MonsterKnown(ids[i], m_save.stageCleared);
		BOOL on = i == m_monSel;
		if (on) m_r.HudRect(x + U(12), yy - U(4), w - U(24), U(26), RGBA8(70, 78, 120, 180));
		const wchar_t* nm = seen ? MonsterLabel(ids[i]) : L"？？？";
		m_r.HudText(nm, x + U(24), yy + U(8), U(15), on ? RGBA8(255, 226, 150) : (seen ? RGBA8(236, 236, 244) : RGBA8(140, 146, 164)), 0, 1);
		if (seen) {
			int kn = FoeKills(ids[i]);
			wchar_t ks[16];
			swprintf_s(ks, L"%d", kn);
			m_r.HudText(ks, x + w - U(20), yy + U(8), U(14), kn > 0 ? RGBA8(255, 214, 120) : RGBA8(140, 146, 164), 2, 0);
		}
		if (m_click && m_clickX >= x + U(12) && m_clickX < x + w - U(12) && m_clickY >= yy - U(4) && m_clickY < yy + U(22))
			m_monSel = i;
		yy += U(28);
	}
	float navY = listBot + U(4);
	float navH = U(30);
	float navW = min(U(88.f), (w - U(36)) * .28f);
	if (navW < U(56)) navW = U(56);
	wchar_t pg[32];
	swprintf_s(pg, L"%d / %d", m_codexPage + 1, pages);
	if (Btn(ID_CDX_PREV, x + U(12), navY, navW, navH, L"◀")) m_codexPage = (m_codexPage + pages - 1) % pages;
	if (Btn(ID_CDX_NEXT, x + w - U(12) - navW, navY, navW, navH, L"▶")) m_codexPage = (m_codexPage + 1) % pages;
	m_r.HudText(pg, x + w * .5f, navY + navH * .5f, U(13), RGBA8(210, 214, 224), 1, 1);
	float sy = navY + navH + U(10);
	float col = (w - U(36)) * .5f;
	if (n > 0 && MonsterKnown(ids[m_monSel], m_save.stageCleared)) {
		int id = ids[m_monSel];
		const MonsterDef& md = MonsterInfo(id);
		int lv = 1, stt[S_COUNT];
		MonsterPreview(id, &lv, stt);
		int lo = MonsterDebut(id);
		wchar_t line[160];
		const wchar_t* stageW = LangPick14(L"出現", L"Appears", L"Apparition", L"Compare", L"Aparece", L"출현", L"出现", L"الظهور", L"Появление", L"Erscheint", L"Aparece", L"Verschijnt", L"Pojawia się", L"Çıkış");
		if (md.maxStage > 0) swprintf_s(line, L"%s %d-%d  Lv%d", stageW, lo, md.maxStage, lv);
		else swprintf_s(line, L"%s %d+  Lv%d", stageW, lo, lv);
		m_r.HudText(line, x + U(16), sy, U(13), RGBA8(255, 226, 150), 0, 1);
		sy += U(18);
		static const int showS[] = { S_HP, S_MP, S_STR, S_DEF, S_MAG, S_RES, S_AGI, S_DEX, S_MOV, S_JMP };
		static const int showT[] = { TX_ST_HP, TX_ST_MP, TX_ST_STR, TX_ST_DEF, TX_ST_MAG, TX_ST_RES, TX_ST_AGI, TX_ST_DEX, TX_ST_MOV, TX_ST_JMP };
		for (int i = 0; i < 10; i++) {
			swprintf_s(line, L"%s %d", TT(showT[i]), stt[showS[i]]);
			m_r.HudText(line, x + U(16) + (i % 2) * col, sy + (i / 2) * U(16), U(13), RGBA8(230, 232, 240), 0, 0);
		}
		sy += U(16) * 5 + U(2);
		const wchar_t* wa = md.weak[0] ? ElemWord(md.weak[0]) : NULL;
		const wchar_t* wb = md.weak[1] ? ElemWord(md.weak[1]) : NULL;
		if (wa && wb) swprintf_s(line, L"%s %s・%s  %d-%d", WeakLabel(), wa, wb, md.rMin, md.rMax);
		else if (wa) swprintf_s(line, L"%s %s  %d-%d", WeakLabel(), wa, md.rMin, md.rMax);
		else swprintf_s(line, L"%d-%d", md.rMin, md.rMax);
		m_r.HudText(line, x + U(16), sy, U(13), RGBA8(210, 220, 235), 0, 0);
		sy += U(16);
		int kn = FoeKills(id);
		swprintf_s(line, L"%s %d    %s +%d%%", FoeSlainWord(), kn, FoeMasteryWord(), FoeMasteryPct(kn));
		m_r.HudText(line, x + U(16), sy, U(13), kn > 0 ? RGBA8(255, 214, 120) : RGBA8(170, 176, 190), 0, 0);
		sy += U(18);
		wchar_t held[360];
		MonsterHeldText(id, held, 360);
		if (held[0]) m_r.HudTextBox(held, x + U(16), sy, w - U(32), U(36), U(12), RGBA8(186, 214, 255));
	} else {
		m_r.HudText(L"？？？", x + U(16), sy, U(15), RGBA8(150, 156, 172), 0, 1);
	}
	const wchar_t* hint = LangPick14(
		L"ドラッグで回転　右ドラッグ・Q/Eで見回す",
		L"Drag to turn. Right-drag or Q/E to look around.",
		L"Glisser pour tourner. Clic droit ou Q/E pour orbiter.",
		L"Trascina per ruotare. Destro o Q/E per girare.",
		L"Arrastra para girar. Derecho o Q/E para orbitar.",
		L"드래그로 회전. 우클릭·Q/E로 둘러보기.",
		L"拖动旋转。右拖或 Q/E 环视。",
		L"اسحب للدوران. الأيمن أو Q/E للنظر.",
		L"Тяните для вращения. ПКМ или Q/E — обзор.",
		L"Ziehen dreht. Rechts oder Q/E schaut herum.",
		L"Arraste para girar. Direito ou Q/E para orbitar.",
		L"Slepen draait. Rechts of Q/E kijkt rond.",
		L"Przeciągnij, by obrócić. Prawy lub Q/E — ogląd.",
		L"Sürükleyerek döndürün. Sağ veya Q/E ile bakın.");
	float hintX = x + w + ((float)m_w - (x + w)) * .5f;
	m_r.HudText(hint, hintX, U(28), U(13), RGBA8(230, 232, 238), 1, 0);
	if (n > 0 && MonsterKnown(ids[m_monSel], m_save.stageCleared) && m_monShown == ids[m_monSel]) {
		int style = 0;
		BOOL fly = FALSE;
		MonPreviewIn(ids[m_monSel], m_monAct, style, fly);
		float rightW = (float)m_w - (x + w);
		float by = (float)m_h - U(84);
		int hit = DrawMotRow(ID_MON_MOT0, hintX, by, rightW - U(24), m_monMot, PreviewAlt(style));
		if (hit >= 0) {
			m_monMot = hit;
			PlayShown(m_monAct, m_monMot, style, CL_IDLE, fly, FALSE, TRUE);
		}
	}
	DrawCodexTabs();
}

void Game::DrawCodexTabs()
{
	const wchar_t* labels[5] = {
		TT(TX_CDX_GROW), TT(TX_CDX_ALCH), TT(TX_CDX_SKILL),
		LangPick14(L"敵", L"Foes", L"Ennemis", L"Nemici", L"Enemigos", L"적", L"敌人", L"أعداء", L"Враги", L"Gegner", L"Inimigos", L"Vijanden", L"Wrogowie", L"Düşman"),
		TT(TX_BASE_CLOSE)
	};
	const int ids[5] = { ID_CDX_GROW, ID_CDX_ALCH, ID_CDX_SKILL, ID_MON_TAB, ID_BASE_CLOSE };
	float margin = U(16), gap = U(8), bh = U(34);
	float y = (float)m_h - bh - U(10);
	float span = (float)m_w - margin * 2.f;
	float bw = (span - gap * 4.f) / 5.f;
	if (bw < U(64)) bw = U(64);
	if (bw * 5.f + gap * 4.f > span) bw = (span - gap * 4.f) / 5.f;
	float x = margin;
	for (int i = 0; i < 5; i++) {
		BOOL on = (i == 0 && m_codex == 2) || (i == 1 && m_codex == 3) || (i == 2 && m_codex == 1) || (i == 3 && m_codex == 4);
		if (Btn(ids[i], x, y, bw, bh, labels[i], TRUE, on)) {
			if (i == 0) m_codex = 2;
			else if (i == 1) m_codex = 3;
			else if (i == 2) { m_codex = 1; m_codexPage = 0; }
			else if (i == 3) { if (m_codex != 4) m_codexPage = 0; m_codex = 4; }
			else m_codex = 0;
		}
		x += bw + gap;
	}
}

void Game::DrawCodex()
{
	if (m_codex == 4) { DrawMonCodex(); return; }
	float x = U(30), y = U(24), w = (float)m_w - U(60), h = (float)m_h - U(78);
	Panel(x, y, w, h);
	const wchar_t* title = m_codex == 2 ? TT(TX_CDX_GROW) : (m_codex == 3 ? TT(TX_CDX_ALCH) : TT(TX_CDX_SKILL));
	m_r.HudText(title, x + w * .5f, y + U(28), U(22), RGBA8(255, 226, 150), 1, 1);
	float yy = y + U(58);
	if (m_codex == 1) {
		int jobs[40];
		int nJob = CodexJobList(jobs, 40);
		int slots = nJob + 2;
		if (m_codexJob < 0 || m_codexJob >= slots) m_codexJob = 0;
		if (m_codexSort < 0 || m_codexSort > 3) m_codexSort = 0;
		const wchar_t* jn = CodexWord(1);
		if (m_codexJob == nJob + 1) jn = TT(TX_SK_TAB_FORM);
		else if (m_codexJob > 0) jn = TT(ClassInfo(jobs[m_codexJob - 1]).nameTx);
		const wchar_t* sn = m_codexSort == 1 ? CodexWord(3) : (m_codexSort == 2 ? L"MP" : (m_codexSort == 3 ? CodexWord(4) : CodexWord(0)));
		float jw = min(U(460.f), (w - U(48)) * .56f);
		float sw = min(U(280.f), w - U(48) - jw - U(12));
		int dj = Sel(ID_CDX_JOB, x + U(24), y + U(50), jw, U(36), CodexWord(0), jn);
		int ds = Sel(ID_CDX_SORT, x + U(24) + jw + U(12), y + U(50), sw, U(36), CodexWord(2), sn);
		if (dj) { m_codexJob = (m_codexJob + dj + slots) % slots; m_codexPage = 0; }
		if (ds) { m_codexSort = (m_codexSort + ds + 4) % 4; m_codexPage = 0; }
		int want = m_codexJob > 0 && m_codexJob <= nJob ? jobs[m_codexJob - 1] : -1;
		int wantForm = m_codexJob == nJob + 1;
		static CodexHit hits[SKL_COUNT];
		int n = 0;
		for (int s = 0; s < SKL_COUNT && n < SKL_COUNT; s++) {
			if (SkillInfo(s).enemyOnly) continue;
			if (s >= SKL_ART0 && s < SKL_ARTN && PaceRoleOfLv((s - SKL_ART0) % 26 + 7) < 0) continue;
			CodexHit& h = hits[n];
			h.skill = s;
			h.mp = SkillInfo(s).mp;
			wcscpy_s(h.name, SkillLabel(s));
			if (wantForm) {
				if (!IsFormSkill(s)) continue;
				swprintf_s(h.how, L"%s", TT(TX_SK_TAB_FORM));
				h.lv = s - SKL_FORM0;
				h.key = 0;
			} else if (want >= 0) {
				int lv = CodexLearnLv(want, s);
				if (lv <= 0 || IsFormSkill(s)) continue;
				swprintf_s(h.how, L"%s  Lv%d", TT(ClassInfo(want).nameTx), lv);
				h.lv = lv;
				h.key = 0;
			} else {
				CodexFillHow(s, h.how, 160, &h.key, &h.lv);
			}
			n++;
		}
		s_codexSort = m_codexSort;
		if (n > 1) qsort(hits, n, sizeof(CodexHit), CmpCodex);
		float listTop = y + U(98);
		float listBot = y + h - U(96);
		int per = (int)((listBot - listTop) / U(40));
		if (per < 3) per = 3;
		int pages = max(1, (n + per - 1) / per);
		if (m_codexPage < 0) m_codexPage = 0;
		if (m_codexPage >= pages) m_codexPage = pages - 1;
		int a = m_codexPage * per, b = min(n, a + per);
		yy = listTop;
		if (n == 0) m_r.HudText(TT(TX_PT_NONE), x + U(24), yy, U(16), RGBA8(190, 200, 220), 0, 1);
		for (int i = a; i < b; i++) {
			const SkillDef& sd = SkillInfo(hits[i].skill);
			wchar_t row[240];
			swprintf_s(row, L"%s   MP%d   %s", hits[i].name, hits[i].mp, hits[i].how);
			m_r.HudText(row, x + U(24), yy, U(16), RGBA8(240, 240, 245), 0, 1);
			wchar_t attr[120], desc[420];
			SkillAttrText(sd, attr, 120);
			if (attr[0]) swprintf_s(desc, L"%s  %s", TT(sd.descTx), attr);
			else wcscpy_s(desc, TT(sd.descTx));
			m_r.HudText(desc, x + U(36), yy + U(18), U(13), RGBA8(190, 200, 220), 0, 0);
			yy += U(40);
		}
		wchar_t pg[32];
		swprintf_s(pg, L"%d / %d", m_codexPage + 1, pages);
		m_r.HudText(pg, x + w * .5f, y + h - U(78), U(14), RGBA8(220, 220, 220), 1, 0);
		if (Btn(ID_CDX_PREV, x + U(24), y + h - U(56), U(140), U(36), L"◀")) m_codexPage = (m_codexPage + pages - 1) % pages;
		if (Btn(ID_CDX_NEXT, x + U(180), y + h - U(56), U(140), U(36), L"▶")) m_codexPage = (m_codexPage + 1) % pages;
	} else if (m_codex == 2) {
		static const int showLv[] = { 1, 15, 35, 55, 78, 105, 140, 180, 230, 290, 1000, 9999 };
		m_r.HudText(TT(TX_CDX_EXP), x + U(24), yy, U(16), RGBA8(255, 226, 150), 0, 1);
		yy += U(26);
		for (int i = 0; i < 12; i++) {
			int lv = showLv[i];
			wchar_t row[96];
			swprintf_s(row, L"Lv %d    %s %d", lv, TT(TX_CDX_NEXT), ExpToNext(lv));
			m_r.HudText(row, x + U(36), yy, U(15), RGBA8(230, 230, 240), 0, 0);
			yy += U(22);
		}
		yy += U(8);
		m_r.HudText(TT(TX_CDX_JOB), x + U(24), yy, U(16), RGBA8(255, 226, 150), 0, 1);
		yy += U(24);
		for (int t = 1; t <= 9; t++) {
			wchar_t row[96];
			swprintf_s(row, L"%d → %d    Lv %d", t, t + 1, PromoteLevel(t));
			m_r.HudText(row, x + U(36), yy, U(15), RGBA8(230, 230, 240), 0, 0);
			yy += U(20);
		}
	} else {
		struct Rec { int a, an, b, bn, c, cn; int out, on; };
		static const Rec rec[4] = {
			{ IT_ORE, 2, -1, 0, -1, 0, IT_ELIXIR, 1 },
			{ IT_HERB, 2, IT_ORE, 1, -1, 0, IT_MIGHT, 1 },
			{ IT_CRYSTAL, 1, IT_ORE, 2, -1, 0, IT_DELAY, 1 },
			{ IT_SHARD, 1, IT_CRYSTAL, 1, IT_HERB, 1, IT_ELIXIR, 2 },
		};
		static const int nm[IT_COUNT] = { TX_IT_ORE, TX_IT_HERB, TX_IT_CRYSTAL, TX_IT_SHARD, TX_IT_ELIXIR, TX_IT_MIGHT, TX_IT_DELAY };
		for (int i = 0; i < IT_COUNT; i++) {
			wchar_t row[96];
			swprintf_s(row, L"%s  %d", TT(nm[i]), m_save.itemN[i]);
			m_r.HudText(row, x + U(24) + (i % 2) * U(280), yy + (i / 2) * U(24), U(16), RGBA8(230, 230, 240), 0, 0);
		}
		yy += U(100);
		for (int r = 0; r < 4; r++) {
			const Rec& rc = rec[r];
			BOOL ok = m_save.itemN[rc.a] >= rc.an && (rc.b < 0 || m_save.itemN[rc.b] >= rc.bn) && (rc.c < 0 || m_save.itemN[rc.c] >= rc.cn);
			wchar_t row[160];
			swprintf_s(row, L"%s x%d → %s x%d", TT(nm[rc.a]), rc.an, TT(nm[rc.out]), rc.on);
			m_btnSfx = TS_OK_CRAFT;
			if (Btn(ID_CDX_REC0 + r, x + U(24), yy, w - U(48), U(36), row, ok, FALSE, U(15))) {
				m_save.itemN[rc.a] -= rc.an;
				if (rc.b >= 0) m_save.itemN[rc.b] -= rc.bn;
				if (rc.c >= 0) m_save.itemN[rc.c] -= rc.cn;
				m_save.itemN[rc.out] += rc.on;
				Persist();
			}
			yy += U(42);
		}
	}
	DrawCodexTabs();
}

void Game::DrawShopArm()
{
	m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 150));
	float pw = min(U(980.f), (float)m_w - U(40));
	float ph = (float)m_h - U(32);
	float px = ((float)m_w - pw) * .5f, py = ((float)m_h - ph) * .5f;
	if (m_click && !Hit(px, py, pw, ph)) { m_shopFit = 0; m_click = FALSE; return; }
	Panel(px, py, pw, ph);
	m_r.HudText(TT(TX_SHOP_ARM_ASK), px + pw * .5f, py + U(22), U(18), RGBA8(255, 226, 150), 1, 1);
	struct Arm { int who, kind, stash, have; };
	Arm row[MAX_ROSTER * 2];
	int n = 0;
	for (int i = 0; i < MAX_ROSTER; i++) if (m_save.roster[i].used) {
		for (int k = 0; k < 2; k++) {
			int st = GearBestUpgrade(m_save, i, k);
			if (st < 0 || n >= MAX_ROSTER * 2) continue;
			int have = 0;
			int id = m_save.roster[i].reserved[k ? 7 : 6];
			if (id > 0) {
				const GearDef* e = GearOf(id);
				if (e) have = (e->kind == 0) ? e->str + e->mag : e->def + e->res;
			}
			row[n].who = i; row[n].kind = k; row[n].stash = st; row[n].have = have;
			n++;
		}
	}
	for (int a = 1; a < n; a++) {
		Arm v = row[a];
		int b = a;
		while (b > 0 && row[b - 1].have > v.have) { row[b] = row[b - 1]; b--; }
		row[b] = v;
	}
	float listTop = py + U(52);
	float listBot = py + ph - U(56);
	int per = (int)((listBot - listTop) / U(40));
	if (per < 3) per = 3;
	if (per > 17) per = 17;
	int pages = max(1, (n + per - 1) / per);
	if (m_shopFitPage >= pages) m_shopFitPage = pages - 1;
	if (m_shopFitPage < 0) m_shopFitPage = 0;
	float yy = listTop;
	int base = m_shopFitPage * per;
	for (int s = 0; s < per && base + s < n; s++) {
		const Arm& ar = row[base + s];
		CharData& c = m_save.roster[ar.who];
		const GearDef* ng = GearOf(m_save.gearId[ar.stash]);
		const wchar_t* cur = TT(TX_PT_NONE);
		if (c.reserved[ar.kind ? 7 : 6] > 0) cur = GearName(c.reserved[ar.kind ? 7 : 6]);
		wchar_t lab[180];
		swprintf_s(lab, L"%s  %s  %s → %s", c.name, ClassName(c.cls), cur, ng ? GearName(m_save.gearId[ar.stash]) : L"?");
		if (Btn(ID_SHOP_ARM0 + s, px + U(16), yy, pw - U(32), U(36), lab, TRUE, FALSE, U(13))) {
			GearEquip(c, m_save, ar.stash);
			Persist();
			InvalidatePortraits();
			return;
		}
		yy += U(40);
	}
	if (pages > 1) {
		wchar_t pg[32];
		swprintf_s(pg, L"%d / %d", m_shopFitPage + 1, pages);
		if (Btn(ID_SHOP_FIT_PREV, px + U(16), py + ph - U(46), U(64), U(32), L"◀")) m_shopFitPage = (m_shopFitPage + pages - 1) % pages;
		m_r.HudText(pg, px + U(120), py + ph - U(30), U(14), RGBA8(220, 220, 230), 0, 1);
		if (Btn(ID_SHOP_FIT_NEXT, px + U(168), py + ph - U(46), U(64), U(32), L"▶")) m_shopFitPage = (m_shopFitPage + 1) % pages;
	}
	BOOL junk = FALSE;
	for (int i = 0; i < m_save.gearN && !junk; i++) if (GearIsSurplus(m_save, m_save.gearId[i])) junk = TRUE;
	if (Btn(ID_SHOP_ARM_SELL, px + pw - U(360), py + ph - U(46), U(170), U(32), TT(TX_SHOP_ARM_SELL), junk, FALSE, U(13))) {
		int sold = GearSellSurplus(m_save);
		if (sold > 0) Persist();
		BOOL still = FALSE;
		for (int i = 0; i < MAX_ROSTER && !still; i++) if (m_save.roster[i].used) {
			if (GearBestUpgrade(m_save, i, 0) >= 0 || GearBestUpgrade(m_save, i, 1) >= 0) still = TRUE;
		}
		if (!still) m_shopFit = 0;
		if (sold <= 0) TacSfx(TS_ERROR);
		return;
	}
	if (Btn(ID_SHOP_ARM_NO, px + pw - U(180), py + ph - U(46), U(160), U(32), TT(TX_EV_LATER))) m_shopFit = 0;
}

// 11体目以降の編成拡張。そのステージをクリアしてから買える。10体までは従来どおり。
static int PartySlotStage(int cap)
{
	static const int kNeed[5] = { 50, 100, 200, 400, 800 };
	if (cap < 10 || cap >= MAX_PARTY) return 0;
	return kNeed[cap - 10];
}

void Game::DrawShopAsk()
{
	m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 150));
	float pw = min(U(460.f), (float)m_w - U(40));
	float ph = U(210);
	float px = ((float)m_w - pw) * .5f, py = ((float)m_h - ph) * .5f;
	if (m_click && !Hit(px, py, pw, ph)) { m_shopHold = 0; m_shopFit = 0; m_click = FALSE; return; }
	Panel(px, py, pw, ph);
	wchar_t line[160];
	line[0] = 0;
	BOOL ok = FALSE;
	if (m_shopFit == 6) {
		const GoodDef* gd = GoodAt(m_shopFitId);
		if (gd) {
			swprintf_s(line, L"%s  %sG", TT(gd->nameTx), TacNum(gd->price));
			ok = m_save.gold >= gd->price && GoodHave(m_save, m_shopFitId) < 99;
		}
	} else if (m_shopFit == 7 || m_shopFit == 8) {
		BOOL partyT = m_shopFit == 7;
		int cap = partyT ? PartyCap() : RosterCap();
		int maxCap = partyT ? MAX_PARTY : MAX_ROSTER;
		int price = 0;
		int need = partyT ? PartySlotStage(cap) : 0;
		if (cap < maxCap) price = partyT ? 800 + (cap - 5) * 350 : 1600 + ((cap - 20) / 20) * 600;
		swprintf_s(line, L"%s  %sG", TT(partyT ? TX_TICKET_PT : TX_TICKET_RS), TacNum(price));
		ok = cap < maxCap && m_save.gold >= price && m_save.stageCleared >= need;
	} else if (m_shopFit == 9) {
		swprintf_s(line, L"%s  250G", TT(TX_STONE));
		ok = m_save.gold >= 250 && StoneCount() < 999;
	}
	m_r.HudText(line, px + pw * .5f, py + U(48), U(16), RGBA8(240, 240, 245), 1, 1);
	m_r.HudText(TT(TX_SHOP_ASK), px + pw * .5f, py + U(84), U(20), RGBA8(255, 226, 150), 1, 1);
	float bw = (pw - U(48)) * .5f;
	m_btnSfx = -2;
	if (Btn(ID_SHOP_ASK_YES, px + U(16), py + ph - U(64), bw, U(40), TT(TX_PT_YES), ok, FALSE, U(16))) {
		BOOL done = FALSE;
		if (m_shopFit == 6) done = GoodBuy(m_save, m_shopFitId);
		else if (m_shopFit == 7 || m_shopFit == 8) {
			BOOL partyT = m_shopFit == 7;
			int cap = partyT ? PartyCap() : RosterCap();
			int maxCap = partyT ? MAX_PARTY : MAX_ROSTER;
			int price = partyT ? 800 + (cap - 5) * 350 : 1600 + ((cap - 20) / 20) * 600;
			int need = partyT ? PartySlotStage(cap) : 0;
			if (cap < maxCap && m_save.gold >= price && m_save.stageCleared >= need) {
				m_save.gold -= price;
				if (partyT) m_save.partyCap = cap + 1;
				else m_save.rosterCap = cap + 20;
				done = TRUE;
			}
		} else if (m_shopFit == 9 && m_save.gold >= 250 && StoneCount() < 999) {
			m_save.gold -= 250;
			StoneAdd(1);
			done = TRUE;
		}
		if (!done) { TacSfx(TS_ERROR); return; }
		TacSfx(TS_OK_SHOP);
		Persist();
		m_shopHold = 0;
		m_shopFit = 0;
		return;
	}
	if (Btn(ID_SHOP_ASK_NO, px + U(24) + bw, py + ph - U(64), bw, U(40), TT(TX_PT_NO))) {
		m_shopHold = 0;
		m_shopFit = 0;
	}
}

void Game::DrawShopFit()
{
	if (m_shopFit == 13) {
		if (m_shopWho < 0 || m_shopWho >= MAX_ROSTER || !m_save.roster[m_shopWho].used) { m_shopFit = 4; return; }
		CharData& c = m_save.roster[m_shopWho];
		m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 150));
		float pw = min(U(520.f), (float)m_w - U(40));
		float ph = U(280);
		float px = ((float)m_w - pw) * .5f, py = ((float)m_h - ph) * .5f;
		if (m_click && !Hit(px, py, pw, ph)) { m_shopFit = 4; m_click = FALSE; return; }
		Panel(px, py, pw, ph);
		m_r.HudText(CraftName(m_shopFitId), px + pw * .5f, py + U(28), U(18), RGBA8(255, 226, 150), 1, 1);
		m_r.HudText(c.name, px + pw * .5f, py + U(52), U(14), RGBA8(230, 230, 240), 1, 0);
		float bw = pw - U(32);
		float yy = py + U(72);
		for (int slot = 0; slot < 3; slot++) {
			wchar_t row[160];
			swprintf_s(row, L"%s%d: %s", TT(TX_CRAFT), slot + 1, EquippedBeadName(c, slot));
			m_btnSfx = -2;
			if (Btn(ID_SHOP_SLOT0 + slot, px + U(16), yy, bw, U(36), row, TRUE, FALSE, U(14))) {
				int bead = m_shopFitId + 1;
				if (m_shopHold) {
					if (!BeadBuy(m_save, m_shopFitId)) { TacSfx(TS_ERROR); return; }
					m_shopHold = 0;
				}
				if (!EquipBeadId(c, slot, bead)) { TacSfx(TS_ERROR); return; }
				TacSfx(TS_OK_EQUIP);
				Persist();
				InvalidatePortraits();
				m_shopFit = 0;
				m_shopWho = -1;
				return;
			}
			yy += U(42);
		}
		if (Btn(ID_SHOP_SLOT_NO, px + U(16), py + ph - U(48), bw, U(32), LangPick14(L"買わない", L"Don't buy", L"Ne pas acheter", L"Non comprare", L"No comprar", L"사지 않기", L"不买", L"عدم الشراء", L"Не покупать", L"Nicht kaufen", L"Não comprar", L"Niet kopen", L"Nie kupuj", L"Alma"))) { m_shopFit = 4; }
		return;
	}
	if (m_shopFit == 5) { DrawShopArm(); return; }
	if (!((m_shopFit >= 1 && m_shopFit <= 4) || m_shopFit == 11 || m_shopFit == 12)) return;
	m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 150));
	float pw = min(U(860.f), (float)m_w - U(40));
	float ph = (float)m_h - U(32);
	float px = ((float)m_w - pw) * .5f, py = ((float)m_h - ph) * .5f;
	if (m_click && !Hit(px, py, pw, ph)) { m_shopFit = 0; m_shopHold = 0; m_click = FALSE; return; }
	Panel(px, py, pw, ph);
	m_r.HudText(TT(TX_EQ_ASK), px + pw * .5f, py + U(22), U(18), RGBA8(255, 226, 150), 1, 1);
	wchar_t item[96];
	wcscpy_s(item, TT(TX_PT_NONE));
	const GearDef* fitGear = NULL;
	if (m_shopFit <= 2) {
		if (m_shopHold) {
			fitGear = GearOf(m_shopFitId + 1);
			if (fitGear) wcscpy_s(item, GearName(m_shopFitId + 1));
		} else if (m_shopFitId >= 0 && m_shopFitId < m_save.gearN) {
			wcscpy_s(item, GearName(m_save.gearId[m_shopFitId]));
			fitGear = GearOf(m_save.gearId[m_shopFitId]);
		}
	} else if (m_shopFit == 3) wcscpy_s(item, AccName(m_shopFitId));
	else if (m_shopFit == 11) wcscpy_s(item, PasName(m_shopFitId + 1));
	else if (m_shopFit == 12) wcscpy_s(item, SummonName(m_shopFitId + 1));
	else wcscpy_s(item, CraftName(m_shopFitId));
	m_r.HudText(item, px + pw * .5f, py + U(46), U(14), RGBA8(230, 230, 240), 1, 0);
	int who[MAX_ROSTER], n = 0;
	for (int i = 0; i < MAX_ROSTER && n < MAX_ROSTER; i++) {
		if (!m_save.roster[i].used) continue;
		if (m_shopHold && m_shopFit <= 2 && (!fitGear || !GearCanWear(m_save.roster[i].cls, fitGear))) continue;
		who[n++] = i;
	}
	float listTop = py + U(68);
	float listBot = py + ph - U(56);
	int per = (int)((listBot - listTop) / U(40));
	if (per < 3) per = 3;
	if (per > 24) per = 24;
	int pages = max(1, (n + per - 1) / per);
	if (m_shopFitPage >= pages) m_shopFitPage = pages - 1;
	if (m_shopFitPage < 0) m_shopFitPage = 0;
	float yy = listTop;
	int a = m_shopFitPage * per;
	for (int s = 0; s < per && a + s < n; s++) {
		CharData& c = m_save.roster[who[a + s]];
		BOOL ok = TRUE;
		wchar_t row[180];
		if (m_shopFit <= 2) {
			int slot = (fitGear && fitGear->kind) ? 7 : 6;
			const wchar_t* cur = TT(TX_PT_NONE);
			if (c.reserved[slot] > 0) cur = GearName(c.reserved[slot]);
			if (fitGear && !GearCanWear(c.cls, fitGear)) ok = FALSE;
			swprintf_s(row, L"%s  %s  %s", c.name, ClassName(c.cls), cur);
		} else if (m_shopFit == 3) {
			swprintf_s(row, L"%s  %s  %s / %s", c.name, ClassName(c.cls), EquippedAccName(c, 0), EquippedAccName(c, 1));
		} else if (m_shopFit == 11) {
			int pid = m_shopFitId + 1;
			if (EquippedPasId(c, 0) == pid || EquippedPasId(c, 1) == pid) ok = FALSE;
			swprintf_s(row, L"%s  %s  %s / %s", c.name, ClassName(c.cls), EquippedPasName(c, 0), EquippedPasName(c, 1));
		} else if (m_shopFit == 12) {
			int sid = m_shopFitId + 1;
			int slot = EquippedSummonId(c, 0) > 0 && EquippedSummonId(c, 1) <= 0 ? 1 : 0;
			BOOL owned = EquippedSummonId(c, 0) == sid || EquippedSummonId(c, 1) == sid;
			if (owned && EquippedSummonId(c, 0) == sid && EquippedSummonId(c, 1) == sid) ok = FALSE;
			else if (!m_shopHold && !owned && SummonSpare(c, slot, m_shopFitId) < 1) ok = FALSE;
			else if (m_shopHold && m_save.gold < SummonPrice(m_shopFitId) && SummonSpare(c, slot, m_shopFitId) < 1) ok = FALSE;
			swprintf_s(row, L"%s  %s  %s / %s", c.name, ClassName(c.cls), EquippedSummonName(c, 0), EquippedSummonName(c, 1));
		} else {
			int bead = m_shopFitId + 1;
			BOOL super = IsCraftSuper(CRAFT_BASE + m_shopFitId);
			if (super) {
				if (EquippedBeadId(c, 3) == bead) ok = FALSE;
				swprintf_s(row, L"%s  %s  %s", c.name, ClassName(c.cls), EquippedBeadName(c, 3));
			} else {
				swprintf_s(row, L"%s  %s  %s / %s / %s", c.name, ClassName(c.cls), EquippedBeadName(c, 0), EquippedBeadName(c, 1), EquippedBeadName(c, 2));
			}
		}
		float nameW = pw - U(118);
		m_btnSfx = -2;
		if (Btn(ID_SHOP_FIT0 + s, px + U(16), yy, nameW, U(36), row, ok, FALSE, U(13))) {
			BOOL done = FALSE;
			if (m_shopFit <= 2) {
				if (!ok) { TacSfx(TS_ERROR); return; }
				int stash = m_shopFitId;
				if (m_shopHold) {
					if (!GearBuy(m_save, m_shopFitId)) { TacSfx(TS_ERROR); return; }
					stash = m_save.gearN - 1;
					m_shopHold = 0;
					m_shopFitId = stash;
				}
				GearEquip(c, m_save, stash);
				done = TRUE;
			} else if (m_shopFit == 3) {
				if (m_shopHold) {
					if (!AccBuy(m_save, m_shopFitId - 1)) { TacSfx(TS_ERROR); return; }
					m_shopHold = 0;
				}
				int slot = EquippedAccId(c, 0) > 0 && EquippedAccId(c, 1) <= 0 ? 1 : 0;
				done = EquipAccId(c, slot, m_shopFitId);
			} else if (m_shopFit == 11) {
				if (m_shopHold) {
					if (!PasBuy(m_save, m_shopFitId)) { TacSfx(TS_ERROR); return; }
					m_shopHold = 0;
				}
				int slot = EquippedPasId(c, 0) > 0 && EquippedPasId(c, 1) <= 0 ? 1 : 0;
				done = EquipPasId(c, slot, m_shopFitId + 1);
			} else if (m_shopFit == 12) {
				if (m_shopHold) {
					if (SummonSpare(c, 0, m_shopFitId) < 1 && SummonSpare(c, 1, m_shopFitId) < 1) {
						if (!SummonBuy(m_save, m_shopFitId)) { TacSfx(TS_ERROR); return; }
					}
					m_shopHold = 0;
				}
				int slot = EquippedSummonId(c, 0) > 0 && EquippedSummonId(c, 1) <= 0 ? 1 : 0;
				done = EquipSummonId(c, slot, m_shopFitId + 1);
			} else if (IsCraftSuper(CRAFT_BASE + m_shopFitId)) {
				if (m_shopHold) {
					if (!BeadBuy(m_save, m_shopFitId)) { TacSfx(TS_ERROR); return; }
					m_shopHold = 0;
				}
				done = EquipBeadId(c, 3, m_shopFitId + 1);
			} else {
				m_shopWho = who[a + s];
				m_shopFit = 13;
				return;
			}
			if (!done) { TacSfx(TS_ERROR); return; }
			TacSfx(TS_OK_EQUIP);
			Persist();
			InvalidatePortraits();
			m_shopFit = 0;
			m_shopHold = 0;
			return;
		}
		if (Btn(ID_SHOP_DET0 + s, px + U(22) + nameW, yy, U(78), U(36), TT(TX_SHOP_DETAIL), TRUE, FALSE, U(12))) {
			m_partySel = who[a + s];
			m_partyBack = 1;
			EnterParty();
			return;
		}
		yy += U(40);
	}
	if (n < 1) m_r.HudText(TT(TX_SHOP_NOWEAR), px + pw * .5f, py + U(120), U(16), RGBA8(255, 180, 160), 1, 1);
	BOOL keep = m_shopHold && ((m_shopFit <= 2 && n > 0) || m_shopFit == 11 || m_shopFit == 12);
	if (keep) m_btnSfx = -2;
	if (keep && Btn(ID_SHOP_FIT_KEEP, px + U(16), py + ph - U(86), pw - U(200), U(32), TT(TX_SHOP_KEEP))) {
		BOOL bought = m_shopFit == 12 ? SummonBuy(m_save, m_shopFitId) : (m_shopFit == 11 ? PasBuy(m_save, m_shopFitId) : GearBuy(m_save, m_shopFitId));
		if (!bought) { TacSfx(TS_ERROR); return; }
		TacSfx(TS_OK_SHOP);
		Persist();
		m_shopFit = 0;
		m_shopHold = 0;
		return;
	}
	if (pages > 1) {
		wchar_t pg[32];
		swprintf_s(pg, L"%d / %d", m_shopFitPage + 1, pages);
		if (Btn(ID_SHOP_FIT_PREV, px + U(16), py + ph - U(46), U(64), U(32), L"◀")) m_shopFitPage = (m_shopFitPage + pages - 1) % pages;
		m_r.HudText(pg, px + U(120), py + ph - U(30), U(14), RGBA8(220, 220, 230), 0, 1);
		if (Btn(ID_SHOP_FIT_NEXT, px + U(168), py + ph - U(46), U(64), U(32), L"▶")) m_shopFitPage = (m_shopFitPage + 1) % pages;
	}
	if (Btn(ID_SHOP_FIT_NO, px + pw - U(180), py + ph - U(46), U(160), U(32), TT(TX_EV_LATER))) { m_shopFit = 0; m_shopHold = 0; }
}

static BOOL GearListBefore(int a, int b)
{
	const GearDef* ga = GearOf(a + 1);
	const GearDef* gb = GearOf(b + 1);
	int ra = ga && ga->req > 0 ? ga->req : 1;
	int rb = gb && gb->req > 0 ? gb->req : 1;
	if (ra != rb) return ra < rb;
	int pa = ga ? ga->price : 0;
	int pb = gb ? gb->price : 0;
	if (pa != pb) return pa < pb;
	return a < b;
}

void Game::DrawShop()
{
	BOOL fit = m_shopFit > 0 || m_shopHold > 0;
	BOOL savedClick = m_click;
	int savedAct = m_activateId;
	if (fit) { m_click = FALSE; m_activateId = -1; }
	float w = min((float)m_w - U(40), U(980)), h = (float)m_h - U(30);
	float x = ((float)m_w - w) * .5f, y = U(16);
	Panel(x, y, w, h);
	wchar_t title[96];
	swprintf_s(title, L"%s    %s %s", TT(TX_SHOP), TT(TX_GOLD), TacNum(m_save.gold));
	m_r.HudText(title, x + w * .5f, y + U(22), U(20), RGBA8(255, 226, 150), 1, 1);
	static const int lineCls[6] = { C_SOLDIER, C_FIGHTER, C_ARCHER, C_THIEF, C_MAGE, C_CLERIC };
	float tx = x + U(12);
	float step = min(U(108.f), (w - U(24)) / 7.f);
	for (int i = 0; i < 7; i++) {
		const wchar_t* lab = i < 6 ? ClassName(lineCls[i]) : TT(TX_SHOP_FREE);
		int id = i < 6 ? ID_SHOP_LINE0 + i : ID_SHOP_FREE;
		if (Btn(id, tx, y + U(40), step - U(4), U(28), lab, TRUE, m_shopLine == i, U(12))) {
			m_shopLine = i; m_shopTier = 0; m_shopPage = 0; m_shopSellPage = 0;
			if (i == 6) { if (m_shopKind != 4 && m_shopKind != 6 && m_shopKind != 7) m_shopKind = 5; }
			else if (m_shopKind == 2) m_shopKind = 0;
		}
		tx += step;
	}
	float ky = y + U(74);
	float sellX = x + w * .66f;
	float tab0 = x + U(16);
	float gap = U(6);
	float avail = sellX - tab0 - U(12);
	static const int kindTx[7] = { TX_SHOP_WPN, TX_SHOP_ARM, TX_SHOP_GOODS, TX_SHOP_PART, TX_SHOP_ACC, TX_SHOP_BEAD, TX_SHOP_PAS };
	float bw = (avail - gap * 7.f) / 8.f;
	if (bw < U(8)) bw = U(8);
	float kfs = U(12);
	if (bw < kfs * 4.6f) kfs = max(U(8.f), bw / 4.6f);
	for (int i = 0; i < 8; i++) {
		const wchar_t* klab = i < 7 ? TT(kindTx[i]) : SummonWord();
		if (Btn(ID_SHOP_KIND0 + i, tab0 + (bw + gap) * i, ky, bw, U(28), klab, TRUE, m_shopKind == i, kfs)) {
			m_shopKind = i; m_shopPage = 0; m_shopSellPage = 0;
		}
	}
	const BOOL gearTab = (m_shopKind == 0 || m_shopKind == 1);
	const float rowH = gearTab ? U(50.f) : U(30.f);
	const float rowStep = gearTab ? U(54.f) : U(34.f);
	float listTop = ky + U(36);
	if (m_shopKind == 5) {
		float fw = U(150);
		if (Btn(ID_SHOP_ART, x + U(16), listTop, fw, U(28), TT(TX_CRAFT), TRUE, m_shopSuper == 0, U(13))) { m_shopSuper = 0; m_shopPage = 0; }
		if (Btn(ID_SHOP_OUGI, x + U(24) + fw, listTop, fw, U(28), TT(TX_OUGI), TRUE, m_shopSuper != 0, U(13))) { m_shopSuper = 1; m_shopPage = 0; }
		listTop += U(34);
	}
	if (gearTab && m_shopLine >= 0 && m_shopLine < 6) {
		static const int tierCls[6][3] = {
			{ C_SOLDIER, C_PALADIN, C_HERO },
			{ C_FIGHTER, C_BERSERKER, C_GLADIATOR },
			{ C_ARCHER, C_SNIPER, C_PHANTOM },
			{ C_THIEF, C_ASSASSIN, C_SHADOW },
			{ C_MAGE, C_WIZARD, C_ARCHMAGE },
			{ C_CLERIC, C_BISHOP, C_SAINT }
		};
		static const int rankLine[6] = { 0, 6, 3, 7, 1, 2 };
		float leftSpan = sellX - (x + U(16)) - U(8);
		float twb = (leftSpan - gap * 9.f) / 10.f;
		if (twb < U(8)) twb = U(8);
		float tfs = U(10);
		if (twb < tfs * 5.2f) tfs = max(U(8.f), twb / 5.4f);
		for (int t = 0; t < 10; t++) {
			int cls = t < 3 ? tierCls[m_shopLine][t] : C_PLAYER_COUNT + rankLine[m_shopLine] * TAC_RANK_N + (t - 3);
			int on = m_shopTier == t + 1;
			if (Btn(ID_SHOP_TIER0 + t, x + U(16) + (twb + gap) * t, listTop, twb, U(24), ClassName(cls), TRUE, on, tfs)) {
				m_shopTier = on ? 0 : t + 1;
				m_shopPage = 0;
			}
		}
		listTop += U(28);
	}
	if (m_shopKind == 4) {
		const wchar_t* bandL[4] = {
			LangPick14(L"全", L"All", L"Tous", L"Tutti", L"Todo", L"전체", L"全部", L"الكل", L"Все", L"Alle", L"Tudo", L"Alles", L"Wszystko", L"Tümü"),
			LangPick14(L"千未満", L"Under 1,000", L"Moins de 1 000", L"Sotto 1.000", L"Menos de 1.000", L"1천 미만", L"千以下", L"أقل من 1000", L"До 1 000", L"Unter 1.000", L"Abaixo de 1.000", L"Onder 1.000", L"Poniżej 1 000", L"1.000 altı"),
			LangPick14(L"千〜十万", L"1,000–100,000", L"1 000–100 000", L"1.000–100.000", L"1.000–100.000", L"1천–10만", L"千到十万", L"1000–100000", L"1 000–100 000", L"1.000–100.000", L"1.000–100.000", L"1.000–100.000", L"1 000–100 000", L"1.000–100.000"),
			LangPick14(L"十万以上", L"100,000+", L"100 000+", L"100.000+", L"100.000+", L"10만 이상", L"十万以上", L"100000+", L"100 000+", L"100.000+", L"100.000+", L"100.000+", L"100 000+", L"100.000+")
		};
		float bw4 = (sellX - (x + U(16)) - U(8) - gap * 3.f) / 4.f;
		if (bw4 < U(8)) bw4 = U(8);
		for (int t = 0; t < 4; t++) {
			if (Btn(ID_SHOP_BAND0 + t, x + U(16) + (bw4 + gap) * t, listTop, bw4, U(24), bandL[t], TRUE, m_shopBand == t, U(11))) {
				m_shopBand = t;
				m_shopPage = 0;
			}
		}
		listTop += U(28);
	}
	// 下のボタン列のすぐ上に説明枠を取る。一覧はそこまでで止める。
	const float descH = U(58);
	const float descY = y + h - U(48) - U(8) - descH;
	float listBot = descY - U(6);
	int per = (int)((listBot - listTop) / rowStep);
	if (per > 30) per = 30;
	if (per < 3) per = 3;
	int ids[1024], n = 0;
	if (m_shopKind == 3) {
		n = TrophyList(ids, 512);
	} else if (m_shopKind == 2) {
		ids[n++] = -1;
		ids[n++] = -2;
		ids[n++] = -3;
		for (int i = 0; i < GoodCount() && n < 512; i++) ids[n++] = i;
	} else if (m_shopKind == 4) {
		for (int i = 0; i < AccCount() && n < 1024; i++) {
			int price = AccPrice(i);
			if (m_shopBand == 1 && price >= 1000) continue;
			if (m_shopBand == 2 && (price < 1000 || price >= 100000)) continue;
			if (m_shopBand == 3 && price < 100000) continue;
			ids[n++] = i;
		}
	} else if (m_shopKind == 5) {
		for (int i = 0; i < CRAFT_N && n < 512; i++) {
			if (CraftLineOf(CRAFT_BASE + i) != m_shopLine) continue;
			BOOL sup = IsCraftSuper(CRAFT_BASE + i);
			if (sup != (m_shopSuper != 0)) continue;
			ids[n++] = i;
		}
	} else if (m_shopKind == 6) {
		for (int i = 0; i < PasCount() && n < 512; i++) ids[n++] = i;
	} else if (m_shopKind == 7) {
		for (int i = 0; i < SummonCount() && n < 512; i++) ids[n++] = i;
	} else {
		for (int i = 0; i < GearCount() && n < 512; i++) {
			const GearDef* g = GearOf(i + 1);
			if (!g || g->line != m_shopLine || g->kind != m_shopKind) continue;
			int need = g->req > 0 ? g->req : 1;
			if (m_shopTier > 0 && need != m_shopTier) continue;
			ids[n++] = i;
		}
		for (int i = 1; i < n; i++) {
			int v = ids[i];
			int j = i;
			while (j > 0 && GearListBefore(v, ids[j - 1])) { ids[j] = ids[j - 1]; j--; }
			ids[j] = v;
		}
	}
	int pages = max(1, (n + per - 1) / per);
	if (m_shopPage >= pages) m_shopPage = pages - 1;
	if (m_shopPage < 0) m_shopPage = 0;
	float yy = listTop;
	int a = m_shopPage * per, b = min(n, a + per);
	wchar_t hovHold[360], hovBuf[360];
	hovHold[0] = 0;
	hovBuf[0] = 0;
	const wchar_t* hov = NULL;
	float rowX = x + U(16), rowW = w * .62f;
	auto takeHover = [&](int btn, float hy, const wchar_t* text) {
		if (!text || !text[0]) return;
		if (Hit(rowX, hy, rowW, rowH) || (m_navMode && m_focusId == btn)) {
			wcscpy_s(hovHold, text);
			hov = hovHold;
		}
	};
	for (int s = 0; s < per; s++) {
		int ix = a + s;
		if (ix >= b) break;
		wchar_t row[180];
		BOOL ok = FALSE;
		// 行は確認窓を開く音か売却音を自前で鳴らすので、ボタン側は黙らせる。
		m_btnSfx = -2;
		if (m_shopKind == 3) {
			wchar_t nm[80];
			int ct = 0, price = 0;
			TrophyNameOf(ids[ix], nm, 80, &ct, &price);
			swprintf_s(row, L"%s  x%d  %sG", nm, ct, TacNum(price));
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, row, TRUE, FALSE, U(13))) {
				if (TrophySell(m_save, ids[ix])) { TacSfx(TS_OK_SELL); Persist(); return; }
			}
		} else if (m_shopKind == 2 && (ids[ix] == -1 || ids[ix] == -2)) {
			BOOL partyT = ids[ix] == -1;
			int cap = partyT ? PartyCap() : RosterCap();
			int maxCap = partyT ? MAX_PARTY : MAX_ROSTER;
			int price = 0;
			int need = partyT ? PartySlotStage(cap) : 0;
			BOOL locked = partyT && need > 0 && m_save.stageCleared < need;
			if (cap < maxCap) price = partyT ? 800 + (cap - 5) * 350 : 1600 + ((cap - 20) / 20) * 600;
			if (cap >= maxCap) swprintf_s(row, L"%s  MAX", TT(partyT ? TX_TICKET_PT : TX_TICKET_RS));
			else if (locked) swprintf_s(row, L"%s  %s %d  (%d→%d)", TT(TX_TICKET_PT), TT(TX_BT_STAGE), need, cap, cap + 1);
			else if (partyT) swprintf_s(row, L"%s  %sG  (%d→%d)", TT(TX_TICKET_PT), TacNum(price), cap, cap + 1);
			else swprintf_s(row, L"%s  %sG  (%d→%d)", TT(TX_TICKET_RS), TacNum(price), cap, cap + 20);
			ok = cap < maxCap && !locked && m_save.gold >= price;
			if (locked) swprintf_s(hovBuf, L"%s  %s %d", TT(TX_ITD_PT), TT(TX_BT_STAGE), need);
			takeHover(ID_SHOP_BUY0 + s, yy, locked ? hovBuf : TT(partyT ? TX_ITD_PT : TX_ITD_RS));
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, row, ok, FALSE, U(13))) {
				m_shopHold = 2; m_shopFit = partyT ? 7 : 8; m_shopFitId = 0;
				TacSfx(TS_OPEN); return;
			}
		} else if (m_shopKind == 2 && ids[ix] == -3) {
			int price = 250;
			swprintf_s(row, L"%s  %sG  x%d", TT(TX_STONE), TacNum(price), StoneCount());
			ok = m_save.gold >= price && StoneCount() < 999;
			takeHover(ID_SHOP_BUY0 + s, yy, TT(TX_ITD_STONE));
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, row, ok, FALSE, U(13))) {
				m_shopHold = 2; m_shopFit = 9; m_shopFitId = 0;
				TacSfx(TS_OPEN); return;
			}
		} else if (m_shopKind == 4) {
			swprintf_s(row, L"%s  %sG", AccName(ids[ix] + 1), TacNum(AccPrice(ids[ix])));
			ok = m_save.gold >= AccPrice(ids[ix]);
			{
				wchar_t bits[160] = L"";
				for (int st = 0; st <= S_MOV; st++) {
					int v = AccStat(ids[ix] + 1, st);
					if (!v) continue;
					wchar_t one[32];
					swprintf_s(one, L" %s%+d", TT(TX_ST_HP + st), v);
					wcscat_s(bits, one);
				}
				swprintf_s(hovBuf, L"%s%s", TT(TX_ACC), bits);
				takeHover(ID_SHOP_BUY0 + s, yy, hovBuf);
			}
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, row, ok, FALSE, U(13))) {
				m_shopHold = 1; m_shopFit = 3; m_shopFitId = ids[ix] + 1; m_shopFitPage = 0;
				TacSfx(TS_OPEN); return;
			}
		} else if (m_shopKind == 5) {
			int cost = CraftStoneCost(ids[ix]);
			int need = CraftSuperNeed(ids[ix]);
			BOOL open = need == 0 || CraftSuperUnlockedBest(m_save) >= need;
			int have = BeadCount(ids[ix]);
			{
				const SkillDef& sd = SkillInfo(CRAFT_BASE + ids[ix]);
				const wchar_t* star = IsCraftSuper(CRAFT_BASE + ids[ix]) ? L" ★" : L"";
				if (!open) swprintf_s(row, L"%s  %s  %s%d  %s%d%s", CraftName(ids[ix]), CraftShapeName(sd.area, sd.shape), TT(TX_STONE), cost, TT(TX_MISC_TIER), need, star);
				else swprintf_s(row, L"%s  %s  %s%d  x%d%s", CraftName(ids[ix]), CraftShapeName(sd.area, sd.shape), TT(TX_STONE), cost, have, star);
			}
			ok = open && StoneCount() >= cost && have < 99;
			takeHover(ID_SHOP_BUY0 + s, yy, CraftDesc(ids[ix]));
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, row, ok, FALSE, U(13))) {
				m_shopHold = 1; m_shopFit = 4; m_shopFitId = ids[ix]; m_shopFitPage = 0;
				TacSfx(TS_OPEN); return;
			}
		} else if (m_shopKind == 6) {
			swprintf_s(row, L"%s  %sG  x%d", PasName(ids[ix] + 1), TacNum(PasPrice(ids[ix])), PasHave(ids[ix]));
			ok = m_save.gold >= PasPrice(ids[ix]) && PasHave(ids[ix]) < 99;
			takeHover(ID_SHOP_BUY0 + s, yy, PasDesc(ids[ix] + 1));
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, row, ok, FALSE, U(13))) {
				m_shopHold = 1; m_shopFit = 11; m_shopFitId = ids[ix]; m_shopFitPage = 0;
				TacSfx(TS_OPEN); return;
			}
		} else if (m_shopKind == 7) {
			swprintf_s(row, L"%s  %sG  x%d", SummonName(ids[ix] + 1), TacNum(SummonPrice(ids[ix])), SummonHave(ids[ix]));
			ok = m_save.gold >= SummonPrice(ids[ix]) && SummonHave(ids[ix]) < 99;
			takeHover(ID_SHOP_BUY0 + s, yy, SummonDesc(ids[ix] + 1));
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, row, ok, FALSE, U(13))) {
				m_shopHold = 1; m_shopFit = 12; m_shopFitId = ids[ix]; m_shopFitPage = 0;
				TacSfx(TS_OPEN); return;
			}
		} else if (m_shopKind == 2) {
			const GoodDef* gd = GoodAt(ids[ix]);
			swprintf_s(row, L"%s  %sG  x%d", TT(gd->nameTx), TacNum(gd->price), GoodHave(m_save, ids[ix]));
			ok = m_save.gold >= gd->price && GoodHave(m_save, ids[ix]) < 99;
			takeHover(ID_SHOP_BUY0 + s, yy, GoodDesc(ids[ix]));
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, row, ok, FALSE, U(13))) {
				m_shopHold = 2; m_shopFit = 6; m_shopFitId = ids[ix];
				TacSfx(TS_OPEN); return;
			}
		} else {
			const GearDef* g = GearOf(ids[ix] + 1);
			swprintf_s(row, L"%s  %sG  %s+%s %s+%s %s+%s %s+%s", GearName(ids[ix] + 1), TacNum(g->price),
				TT(TX_ST_STR), TacNum(g->str), TT(TX_ST_MAG), TacNum(g->mag), TT(TX_ST_DEF), TacNum(g->def), TT(TX_ST_RES), TacNum(g->res));
			ok = m_save.gold >= g->price && m_save.gearN < GEAR_STASH;
			takeHover(ID_SHOP_BUY0 + s, yy, GearRole(g));
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, L"", ok, FALSE, U(13))) {
				m_shopHold = 1; m_shopFit = g->kind ? 2 : 1; m_shopFitId = ids[ix]; m_shopFitPage = 0;
				TacSfx(TS_OPEN); return;
			}
			m_r.HudText(row, x + U(16) + w * .31f, yy + rowH * .34f, U(13),
				ok ? RGBA8(246, 242, 232) : RGBA8(140, 140, 150), 1, 1);
			wchar_t users[200];
			users[0] = 0;
			auto addUser = [&](int cls) -> bool {
				wchar_t tmp[200];
				wcscpy_s(tmp, users);
				if (tmp[0]) wcscat_s(tmp, L"・");
				wcscat_s(tmp, ClassName(cls));
				if (m_r.TextWidth(tmp, U(11), 0) > w * .62f - U(24)) {
					if (users[0]) wcscat_s(users, L"…");
					return false;
				}
				wcscpy_s(users, tmp);
				return true;
			};
			int whoC[40], whoT[40], nw = 0;
			int need = g->req > 0 ? g->req : 1;
			auto pushWho = [&](int c) {
				if (nw >= 40 || ClassLine(c) != g->line) return;
				int t = ClassTier(c);
				if (t < need) return;
				whoC[nw] = c; whoT[nw] = t; nw++;
			};
			for (int c = 0; c < C_PLAYER_COUNT; c++) pushWho(c);
			for (int c = C_PLAYER_COUNT; c < C_PLAYER_COUNT + TAC_RANK_LINES * TAC_RANK_N; c++) pushWho(c);
			pushWho(C_GLADIATOR);
			pushWho(C_SHADOW);
			for (int i = 1; i < nw; i++) {
				int vc = whoC[i], vt = whoT[i], j = i;
				while (j > 0 && (whoT[j - 1] > vt || (whoT[j - 1] == vt && whoC[j - 1] > vc))) {
					whoC[j] = whoC[j - 1]; whoT[j] = whoT[j - 1]; j--;
				}
				whoC[j] = vc; whoT[j] = vt;
			}
			for (int i = 0; i < nw; i++) if (!addUser(whoC[i])) break;
			if (users[0])
				m_r.HudText(users, x + U(16) + w * .31f, yy + rowH * .74f, U(11), RGBA8(186, 198, 220), 1, 1);
		}
		yy += rowStep;
	}
	wchar_t pg[32];
	swprintf_s(pg, L"%d / %d", m_shopPage + 1, pages);
	if (pages > 1 && Btn(ID_SHOP_PREV, x + U(16), y + h - U(48), U(64), U(32), L"◀")) m_shopPage = (m_shopPage + pages - 1) % pages;
	m_r.HudText(pg, x + U(150), y + h - U(32), U(14), RGBA8(220, 220, 230), 0, 1);
	if (pages > 1 && Btn(ID_SHOP_NEXT, x + U(200), y + h - U(48), U(64), U(32), L"▶")) m_shopPage = (m_shopPage + 1) % pages;
	float colX = x + w * .66f;
	float colR = x + w - U(16);
	float headY = ky + U(42);
	wchar_t cap[40];
	swprintf_s(cap, TT(TX_SHOP_CAP), m_save.gearN, GEAR_STASH);
	float sellW = m_r.TextWidth(TT(TX_SHOP_SELL), U(14), 1);
	float capW = m_r.TextWidth(cap, U(12), 0);
	BOOL stackCap = colX + sellW + U(10) > colR - capW;
	m_r.HudText(TT(TX_SHOP_SELL), colX, headY, U(14), RGBA8(255, 220, 170), 0, 1);
	m_r.HudText(cap, colR, stackCap ? headY + U(16) : headY, U(12), RGBA8(255, 214, 150), 2, 0);
	float sy = headY + (stackCap ? U(36) : U(22));
	if (gearTab) {
		BOOL need = FALSE;
		for (int i = 0; i < MAX_ROSTER && !need; i++) if (m_save.roster[i].used) {
			if (GearBestUpgrade(m_save, i, 0) >= 0 || GearBestUpgrade(m_save, i, 1) >= 0) need = TRUE;
		}
		BOOL junk = FALSE;
		for (int i = 0; i < m_save.gearN && !junk; i++) if (GearIsSurplus(m_save, m_save.gearId[i])) junk = TRUE;
		BOOL en = need || junk;
		if (Btn(ID_SHOP_SELLJUNK, x + w * .66f, sy, w * .32f, U(28), TT(TX_SHOP_SELLSURP), en, FALSE, U(12))) {
			if (need) { m_shopFit = 5; m_shopFitPage = 0; return; }
			if (GearSellSurplus(m_save) > 0) { Persist(); return; }
			TacSfx(TS_ERROR);
		}
		if (Hit(x + w * .66f, sy, w * .32f, U(28)) || (m_navMode && m_focusId == ID_SHOP_SELLJUNK)) hov = TT(TX_SHOP_SURP_D);
		sy += U(34);
	}
	float sellBot = y + h - U(96);
	int sp = (int)((sellBot - sy) / U(34));
	if (sp > 20) sp = 20;
	if (sp < 3) sp = 3;
	int sellIds[CRAFT_N];
	int on = 0;
	if (m_shopKind == 5) {
		for (int i = 0; i < CRAFT_N && on < CRAFT_N; i++) if (BeadCount(i) > 0) sellIds[on++] = i;
	} else if (m_shopKind == 6) {
		for (int i = 0; i < PasCount() && on < CRAFT_N; i++) if (PasHave(i) > 0) sellIds[on++] = i;
	} else if (m_shopKind == 7) {
		for (int i = 0; i < SummonCount() && on < CRAFT_N; i++) if (SummonHave(i) > 0) sellIds[on++] = i;
	} else if (m_shopKind == 2) {
		for (int i = 0; i < GoodCount() && on < CRAFT_N; i++) if (GoodHave(m_save, i) > 0) sellIds[on++] = i;
	} else if (m_shopKind == 0 || m_shopKind == 1) {
		for (int i = 0; i < m_save.gearN && i < GEAR_STASH && on < CRAFT_N; i++) if (GearOf(m_save.gearId[i])) sellIds[on++] = i;
	}
	int spages = max(1, (on + sp - 1) / sp);
	if (m_shopSellPage >= spages) m_shopSellPage = spages - 1;
	if (m_shopSellPage < 0) m_shopSellPage = 0;
	int sa = m_shopSellPage * sp, sb = min(on, sa + sp);
	for (int s = sa; s < sb; s++) {
		int id = sellIds[s];
		wchar_t row[96];
		if (m_shopKind == 5) {
			int back = max(1, CraftStoneCost(id) / 2);
			swprintf_s(row, L"%s x%d  %s%d", CraftName(id), BeadCount(id), TT(TX_STONE), back);
		} else if (m_shopKind == 6) {
			swprintf_s(row, L"%s x%d  %sG", PasName(id + 1), PasHave(id), TacNum(max(1, PasPrice(id) / 2)));
			if (Hit(x + w * .66f, sy, w * .32f, U(30)) || (m_navMode && m_focusId == ID_SHOP_SELL0 + (s - sa))) {
				wcscpy_s(hovHold, PasDesc(id + 1));
				hov = hovHold;
			}
		} else if (m_shopKind == 7) {
			swprintf_s(row, L"%s x%d  %sG", SummonName(id + 1), SummonHave(id), TacNum(max(1, SummonPrice(id) / 2)));
			if (Hit(x + w * .66f, sy, w * .32f, U(30)) || (m_navMode && m_focusId == ID_SHOP_SELL0 + (s - sa))) {
				wcscpy_s(hovHold, SummonDesc(id + 1));
				hov = hovHold;
			}
		} else if (m_shopKind == 2) {
			const GoodDef* gd = GoodAt(id);
			if (!gd) { sy += U(34); continue; }
			swprintf_s(row, L"%s x%d  %sG", TT(gd->nameTx), GoodHave(m_save, id), TacNum(max(1, gd->price / 2)));
			if (Hit(x + w * .66f, sy, w * .32f, U(30)) || (m_navMode && m_focusId == ID_SHOP_SELL0 + (s - sa))) {
				wcscpy_s(hovHold, GoodDesc(id));
				hov = hovHold;
			}
		} else {
			const GearDef* g = GearOf(m_save.gearId[id]);
			swprintf_s(row, L"%s  %sG", GearName(m_save.gearId[id]), TacNum(g ? max(1, g->price / 2) : 1));
			if (g && (Hit(x + w * .66f, sy, w * .32f, U(30)) || (m_navMode && m_focusId == ID_SHOP_SELL0 + (s - sa)))) {
				const wchar_t* role = GearRole(g);
				if (role && role[0]) { wcscpy_s(hovHold, role); hov = hovHold; }
			}
		}
		m_btnSfx = -2;
		if (Btn(ID_SHOP_SELL0 + (s - sa), x + w * .66f, sy, w * .32f, U(30), row, TRUE, FALSE, U(12))) {
			BOOL sold = FALSE;
			if (m_shopKind == 5) sold = BeadSell(id);
			else if (m_shopKind == 6) {
				int back = max(1, PasPrice(id) / 2);
				if (PasSell(id)) { m_save.gold += back; sold = TRUE; }
			} else if (m_shopKind == 7) {
				int back = max(1, SummonPrice(id) / 2);
				if (SummonSell(id)) { m_save.gold += back; sold = TRUE; }
			} else if (m_shopKind == 2) {
				const GoodDef* gd = GoodAt(id);
				if (gd && GoodTake(m_save, id)) { m_save.gold += max(1, gd->price / 2); sold = TRUE; }
			} else sold = GearSellAt(m_save, id);
			if (sold) { TacSfx(TS_OK_SELL); Persist(); return; }
			TacSfx(TS_ERROR);
		}
		sy += U(34);
	}
	if (spages > 1) {
		wchar_t spg[32];
		swprintf_s(spg, L"%d / %d", m_shopSellPage + 1, spages);
		if (Btn(ID_SHOP_SELL0 + 8, x + w * .66f, sy, U(36), U(28), L"◀")) m_shopSellPage = (m_shopSellPage + spages - 1) % spages;
		m_r.HudText(spg, x + w * .66f + U(70), sy + U(14), U(13), RGBA8(230, 230, 236), 0, 1);
		if (Btn(ID_SHOP_SELL0 + 9, x + w * .66f + U(110), sy, U(36), U(28), L"▶")) m_shopSellPage = (m_shopSellPage + 1) % spages;
	}
	if (hov && hov[0]) {
		float dx = x + U(12);
		float dw = x + w * .66f - dx - U(10);
		m_r.HudGlass(dx, descY, dw, descH, RGBA8(14, 18, 34, 216), RGBA8(196, 168, 108, 210), 1.f);
		m_r.HudTextBox(hov, dx + U(10), descY + U(6), dw - U(20), descH - U(12), U(13), RGBA8(232, 236, 244));
	}
	if (m_shopKind == 3) {
		int tmp[512];
		int piles = TrophyList(tmp, 512);
		m_btnSfx = -2;
		if (Btn(ID_SHOP_SELLALL, x + w * .66f, sy, w * .32f, U(32), TT(TX_SHOP_SELLALL), piles > 0, FALSE, U(13))) {
			if (TrophySellAll(m_save)) { TacSfx(TS_OK_SELL); Persist(); return; }
			TacSfx(TS_ERROR);
		}
	}
	if (Btn(ID_SHOP_TO_PARTY, x + w * .42f, y + h - U(48), U(150), U(32), TT(TX_BASE_PARTY), RosterCount() > 0, FALSE, U(13))) {
		m_partyBack = 1;
		EnterParty();
		return;
	}
	if (Btn(ID_SHOP_CLOSE, x + w - U(170), y + h - U(48), U(150), U(32), TT(TX_CR_BACK))) {
		if (m_shopFit == 13) { m_shopFit = 4; return; }
		if (m_shopHold || m_shopFit) { m_shopHold = 0; m_shopFit = 0; m_shopWho = -1; return; }
		if (m_shopBand) { m_shopBand = 0; m_shopPage = 0; return; }
		if (m_shopTier) { m_shopTier = 0; m_shopPage = 0; return; }
		Persist();
		m_shop = 0;
		m_shopFit = 0;
		m_shopHold = 0;
		m_shopTier = 0;
		m_shopBand = 0;
		m_partyBack = 0;
		m_shopWho = -1;
		if (m_shopBack) { m_shopBack = 0; EnterParty(); }
	}
	if (fit) {
		m_click = savedClick;
		m_activateId = savedAct;
		if (m_shopHold == 2) DrawShopAsk();
		else DrawShopFit();
	}
}

static void KeepAppSettings(SaveData& dst, const SaveData& src)
{
	dst.winX = src.winX; dst.winY = src.winY; dst.winW = src.winW; dst.winH = src.winH;
	dst.winSizeVer = src.winSizeVer;
	dst.camSpeed = src.camSpeed;
	dst.quality = src.quality;
	dst.showGrid = src.showGrid;
	dst.battleAnim = src.battleAnim;
	dst.bgmVol = src.bgmVol;
	dst.seVol = src.seVol;
	dst.optInit = src.optInit;
}

void Game::OpenSlotPanel()
{
	m_slotUi = 1;
	m_slotPick = SlotCurrent();
	m_slotDest = -1;
	m_textFocus = 0;
	m_slotEdit[0] = 0;
	RefreshSlotPeek();
	m_focusId = ID_SLOT_ROW0 + m_slotPick;
}

void Game::RefreshSlotPeek()
{
	int cur = SlotCurrent();
	for (int i = 0; i < TAC_SLOT_N; i++)
		m_slotClear[i] = (i == cur) ? m_save.stageCleared : SlotPeekClear(i);
}

void Game::CommitSlotName()
{
	if (m_textFocus != 2) return;
	m_textFocus = 0;
	SlotRename(m_slotPick, m_slotEdit);
}

void Game::FlushCurrentSlot()
{
	if (m_screen == SC_BATTLE) {
		QueueSuspend(1);
		WaitSuspend();
	} else WaitSuspend();
	SaveWrite(m_save);
}

void Game::AdoptActiveSlot(BOOL leaveBattle)
{
	SaveData keep = m_save;
	if (!SaveLoad(m_save)) {
		SaveDefaults(m_save);
		KeepAppSettings(m_save, keep);
		SlotClearRuntime();
	}
	ApplyAudio();
	m_r.SetQuality(m_save.quality);
	if (leaveBattle || m_screen == SC_BATTLE || m_screen == SC_RESULT) EnterBase();
	else {
		ProbeSuspend();
		for (int i = 0; i < Renderer::PORTRAIT_N; i++) m_portraitWant[i] = (i < MAX_ROSTER) ? i + 1 : 0;
		InvalidatePortraits();
		BuildShowcase();
	}
	RefreshSlotPeek();
}

void Game::SwitchToSlot(int to)
{
	if (to < 0 || to >= TAC_SLOT_N || to == SlotCurrent()) return;
	BOOL battle = m_screen == SC_BATTLE;
	FlushCurrentSlot();
	SlotSelect(to);
	AdoptActiveSlot(battle);
	m_slotUi = 1;
	m_slotPick = to;
	m_textFocus = 0;
	wchar_t sn[TAC_SLOT_NAME];
	SlotTitle(to, sn, TAC_SLOT_NAME);
	AddBanner(sn, TT(TX_SLOT), RGBA8(255, 226, 150), 0, 1.6f);
}

void Game::DeletePickedSlot()
{
	int slot = m_slotPick;
	if (slot < 0 || slot >= TAC_SLOT_N) { m_slotUi = 1; return; }
	BOOL cur = slot == SlotCurrent();
	WaitSuspend();
	SlotWipe(slot);
	SlotRename(slot, L"");
	if (cur) {
		SaveData keep = m_save;
		SlotClearRuntime();
		SaveDefaults(m_save);
		KeepAppSettings(m_save, keep);
		SaveWrite(m_save);
		if (m_screen == SC_BATTLE || m_screen == SC_RESULT) EnterBase();
		else {
			ProbeSuspend();
			for (int i = 0; i < Renderer::PORTRAIT_N; i++) m_portraitWant[i] = (i < MAX_ROSTER) ? i + 1 : 0;
			InvalidatePortraits();
			BuildShowcase();
		}
	}
	m_slotUi = 1;
	m_textFocus = 0;
	RefreshSlotPeek();
}

void Game::CopyPickedSlot()
{
	int from = m_slotPick;
	int to = m_slotDest;
	if (from < 0 || to < 0 || from >= TAC_SLOT_N || to >= TAC_SLOT_N || from == to) { m_slotUi = 1; return; }
	BOOL battle = m_screen == SC_BATTLE;
	if (from == SlotCurrent()) FlushCurrentSlot();
	else WaitSuspend();
	if (!SlotCopyFiles(from, to)) { TacSfx(TS_ERROR); m_slotUi = 1; return; }
	if (to == SlotCurrent()) AdoptActiveSlot(battle);
	else RefreshSlotPeek();
	m_slotUi = 1;
	m_textFocus = 0;
	m_slotPick = to;
	wchar_t dn[TAC_SLOT_NAME];
	SlotTitle(to, dn, TAC_SLOT_NAME);
	AddBanner(TT(TX_SLOT_COPIED), dn, RGBA8(255, 226, 150), 0, 1.6f);
}

void Game::DrawSlotUi()
{
	m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 150));
	float w = min(U(760.f), (float)m_w - U(24));
	float h = min(U(640.f), (float)m_h - U(24));
	if (w < U(280)) w = (float)m_w - 8.f;
	if (h < U(220)) h = (float)m_h - 8.f;
	float x = ((float)m_w - w) * .5f;
	float y = ((float)m_h - h) * .5f;
	Panel(x, y, w, h);
	float pad = U(18);
	int mode = m_slotUi;
	const wchar_t* title = mode == 4 ? TT(TX_SLOT_COPYTO) : TT(TX_SLOT);
	m_r.HudText(title, x + w * .5f, y + U(28), U(20), RGBA8(255, 226, 150), 1, 1);
	if (mode == 1 || mode == 4) {
		if (mode == 4) {
			wchar_t sn[TAC_SLOT_NAME], sub[96];
			SlotTitle(m_slotPick, sn, TAC_SLOT_NAME);
			swprintf_s(sub, L"%s  %s", TT(TX_SLOT_COPY), sn);
			m_r.HudText(sub, x + w * .5f, y + U(52), U(14), RGBA8(220, 220, 230), 1, 0);
		}
		float btnH = U(38);
		float btnY = y + h - pad - btnH;
		float top = y + (mode == 4 ? U(70) : U(52));
		float rowH = (btnY - U(8) - top) / (float)TAC_SLOT_N;
		if (rowH > U(44)) rowH = U(44);
		if (rowH < U(22)) rowH = U(22);
		for (int i = 0; i < TAC_SLOT_N; i++) {
			wchar_t nm[TAC_SLOT_NAME];
			SlotTitle(i, nm, TAC_SLOT_NAME);
			BOOL editing = m_textFocus == 2 && i == m_slotPick && mode == 1;
			wchar_t info[48];
			if (m_slotClear[i] < 0) wcsncpy_s(info, TT(TX_SLOT_EMPTY), _TRUNCATE);
			else swprintf_s(info, L"%s %d", TT(TX_BASE_STAGE), m_slotClear[i]);
			wchar_t line[160];
			swprintf_s(line, L"%d   %s%s    %s%s%s", i + 1,
				editing ? m_slotEdit : nm, editing ? L"_" : L"",
				info, (i == SlotCurrent()) ? L"    " : L"", (i == SlotCurrent()) ? TT(TX_SLOT_NOW) : L"");
			BOOL en = !(mode == 4 && i == m_slotPick);
			BOOL sel = i == m_slotPick;
			if (Btn(ID_SLOT_ROW0 + i, x + pad, top + rowH * i, w - pad * 2.f, rowH - U(3), line, en, sel, U(15))) {
				if (mode == 4) {
					m_slotDest = i;
					m_slotUi = 5;
					m_focusId = ID_SLOT_NO;
				} else {
					if (m_textFocus == 2 && m_slotPick != i) CommitSlotName();
					m_slotPick = i;
				}
			}
		}
		float gap = U(6);
		float bx = x + pad;
		float bw = w - pad * 2.f;
		if (mode == 4) {
			if (Btn(ID_SLOT_BACK, bx, btnY, bw, btnH, TT(TX_CR_BACK))) { m_slotUi = 1; m_textFocus = 0; }
		} else {
			float bw5 = (bw - gap * 4.f) / 5.f;
			BOOL cur = m_slotPick == SlotCurrent();
			if (Btn(ID_SLOT_USE, bx, btnY, bw5, btnH, cur ? TT(TX_SLOT_NOW) : TT(TX_SLOT_USE), !cur, FALSE, U(14))) {
				if (m_textFocus == 2) CommitSlotName();
				SwitchToSlot(m_slotPick);
			}
			if (Btn(ID_SLOT_REN, bx + (bw5 + gap), btnY, bw5, btnH, TT(TX_SLOT_REN), TRUE, m_textFocus == 2, U(14))) {
				SlotTitle(m_slotPick, m_slotEdit, TAC_SLOT_NAME);
				m_textFocus = 2;
			}
			if (Btn(ID_SLOT_COPY, bx + (bw5 + gap) * 2.f, btnY, bw5, btnH, TT(TX_SLOT_COPY), TRUE, FALSE, U(14))) {
				if (m_textFocus == 2) CommitSlotName();
				m_slotUi = 4;
				m_slotDest = -1;
				m_focusId = ID_SLOT_ROW0;
			}
			if (Btn(ID_SLOT_DEL, bx + (bw5 + gap) * 3.f, btnY, bw5, btnH, TT(TX_PT_DELETE), TRUE, FALSE, U(14), RGBA8(140, 50, 40, 240))) {
				if (m_textFocus == 2) CommitSlotName();
				m_slotUi = 2;
				m_focusId = ID_SLOT_NO;
			}
			if (Btn(ID_SLOT_BACK, bx + (bw5 + gap) * 4.f, btnY, bw5, btnH, TT(TX_CR_BACK), TRUE, FALSE, U(14))) {
				if (m_textFocus == 2) CommitSlotName();
				m_slotUi = 0;
				m_textFocus = 0;
			}
		}
	} else {
		wchar_t src[TAC_SLOT_NAME], dst[TAC_SLOT_NAME], head[120];
		SlotTitle(m_slotPick, src, TAC_SLOT_NAME);
		if (mode == 5) {
			SlotTitle(m_slotDest, dst, TAC_SLOT_NAME);
			swprintf_s(head, L"%s   →   %s", src, dst);
		} else wcsncpy_s(head, src, _TRUNCATE);
		const wchar_t* body = TT(TX_SLOT_WARN1);
		if (mode == 3) body = TT(TX_SLOT_WARN2);
		else if (mode == 5) body = TT(TX_SLOT_COPYQ);
		m_r.HudText(head, x + w * .5f, y + U(78), U(18), RGBA8(255, 236, 190), 1, 1);
		m_r.HudTextBox(body, x + pad, y + U(110), w - pad * 2.f, U(110), U(16), RGBA8(255, 220, 210));
		float bh = U(40);
		float by = y + h - pad - bh;
		float bw = (w - pad * 2.f - U(10)) * .5f;
		DWORD yesCol = (mode == 3) ? RGBA8(140, 50, 40, 240) : 0;
		if (Btn(ID_SLOT_YES, x + pad, by, bw, bh, TT(TX_PT_YES), TRUE, FALSE, 0, yesCol)) {
			if (mode == 2) { m_slotUi = 3; m_focusId = ID_SLOT_NO; }
			else if (mode == 3) DeletePickedSlot();
			else if (mode == 5) CopyPickedSlot();
		}
		if (Btn(ID_SLOT_NO, x + pad + bw + U(10), by, bw, bh, TT(TX_PT_NO))) {
			m_slotUi = (mode == 5) ? 4 : 1;
			m_focusId = ID_SLOT_BACK;
		}
	}
	m_click = FALSE;
	m_rclick = FALSE;
	m_activateId = -1;
}

void Game::DrawBaseUi()
{
	if (m_slotUi) { DrawSlotUi(); return; }
	if (m_codex) { DrawCodex(); return; }
	if (m_shop) { DrawShop(); return; }
	// 開始メニューは出撃カードより後に描く。それまでは下のボタンを押させない。
	BOOL modal = m_sortieAsk != 0;
	BOOL savedClick = m_click;
	int savedAct = m_activateId;
	if (modal) { m_click = FALSE; m_activateId = -1; }
	float margin = max(8.f, min(U(20), (float)m_w * .02f));
	float pw = min(U(330), (float)m_w - margin * 2.f);
	float ph = min((float)m_h - margin * 2.f, (float)m_h - 8.f);
	float x = margin, y = margin;
	if (x + pw > m_w) pw = (float)m_w - x - 4.f;
	if (y + ph > m_h) ph = (float)m_h - y - 4.f;
	Panel(x, y, pw, ph);
	float titleH = min(U(24), ph * .045f);
	m_r.HudText(TT(TX_TITLE), x + pw * .5f, y + titleH + 8.f, titleH, RGBA8(255, 226, 150), 1, 1);
	float helpH = min(U(40), max(24.f, ph * .07f));
	float helpY = y + ph - helpH - max(6.f, ph * .015f);
	float top = y + titleH * 2.f + 14.f;
	float midRoom = helpY - top - 4.f;
	float midNeed = m_baseOpt ? U(620) : U(400);
	float k = midNeed > 1.f ? min(1.f, midRoom / midNeed) : 1.f;
	if (k < .42f) k = .42f;
	auto Ku = [&](float px) { return U(px) * k; };
	float by = top;
	float bw = pw - max(16.f, pw * .1f), bh = Ku(44), bx = x + (pw - bw) * .5f;
	wchar_t st[64];
	swprintf_s(st, L"%d / %d", m_save.stageSel, TAC_STAGE_MAX);
	int d = Sel(ID_BASE_STAGE, bx, by, bw, Ku(40), TT(TX_BASE_STAGE), st);
	if (d) {
		int step = m_keyShift ? 100 : 10;
		if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 && !m_keyShift) step = 1;
		int prev = m_save.stageSel;
		int mx = min(TAC_STAGE_MAX, m_save.stageCleared + 1);
		m_save.stageSel = Clampi(m_save.stageSel + d * step, 1, mx);
		if (m_save.stageSel != prev) {
			m_mapSeed = 0;
			m_mapSeedStage = -1;
			m_showBuilt = FALSE;
		}
	}
	by += Ku(46);
	wchar_t cl[64];
	swprintf_s(cl, L"%s: %s    %s %s", TT(TX_BASE_CLEARED), TacNum(m_save.stageCleared), TT(TX_GOLD), TacNum(m_save.gold));
	m_r.HudText(cl, bx + U(4), by + Ku(8), min(U(14), Ku(16)), RGBA8(200, 200, 210), 0, 0);
	by += Ku(28);
	int np = PartyCount();
	if (Btn(ID_BASE_SORTIE, bx, by, bw, bh + Ku(8), TT(TX_BASE_SORTIE), np > 0, FALSE, min(U(22), bh * .5f), RGBA8(170, 60, 50, 240))) {
		ProbeSuspend();
		if (m_midStage == m_save.stageSel || m_autoStage == m_save.stageSel) { m_sortieAsk = 1; m_focusId = ID_SORTIE_START; return; }
		StartBattle(m_save.stageSel);
		return;
	}
	by += bh + Ku(14);
	float gapb = Ku(8);
	float halfW = (bw - gapb) * .5f;
	float hfs = min(U(15), bh * .36f);
	if (Btn(ID_BASE_PARTY, bx, by, halfW, bh, TT(TX_BASE_PARTY), RosterCount() > 0, FALSE, hfs)) { EnterParty(); return; }
	if (Btn(ID_BASE_SHOP, bx + halfW + gapb, by, halfW, bh, TT(TX_SHOP), TRUE, FALSE, hfs)) {
		m_shop = 1; m_shopPage = 0; m_shopSellPage = 0; m_shopKind = 0; m_shopSuper = 0; m_shopHold = 0; m_shopFit = 0; m_partyBack = 0;
		int cap = PartyCap();
		for (int i = 0; i < cap; i++) {
			int r = m_save.party[i];
			if (r >= 0 && m_save.roster[r].used) { m_shopLine = ClassLine(m_save.roster[r].cls); break; }
		}
		return;
	}
	by += bh + Ku(8);
	if (Btn(ID_BASE_CREATE, bx, by, halfW, bh, TT(TX_BASE_CREATE), RosterCount() < RosterCap(), FALSE, hfs)) { m_createFromParty = 0; EnterCreate(-1); return; }
	if (Btn(ID_BASE_EVOLVE, bx + halfW + gapb, by, halfW, bh, TT(TX_BASE_EVOLVE), RosterCount() > 0, FALSE, hfs)) { m_evFromParty = 0; EnterEvolve(); return; }
	by += bh + Ku(8);
	if (Btn(ID_BASE_CODEX, bx, by, halfW, bh, TT(TX_BASE_CODEX), TRUE, FALSE, hfs)) { m_codex = 1; m_codexPage = 0; return; }
	if (Btn(ID_BASE_SET, bx + halfW + gapb, by, halfW, bh, TT(TX_SET_OPEN), TRUE, m_baseOpt != 0, hfs)) m_baseOpt = !m_baseOpt;
	by += bh + Ku(8);
	{
		wchar_t sn[TAC_SLOT_NAME], sb[80];
		SlotTitle(SlotCurrent(), sn, TAC_SLOT_NAME);
		swprintf_s(sb, L"%s  %s", TT(TX_SLOT), sn);
		if (Btn(ID_BASE_SAVESLOT, bx, by, bw, bh, sb, TRUE, FALSE, hfs)) { OpenSlotPanel(); return; }
	}
	by += bh + Ku(12);
	if (m_baseOpt) {
		static const int qtx[3] = { TX_SET_Q_LOW, TX_SET_Q_STD, TX_SET_Q_HIGH };
		if (int q = Sel(ID_BASE_QUALITY, bx, by, bw, Ku(36), TT(TX_SET_QUALITY), TT(qtx[Clampi(m_save.quality, 0, 2)]))) {
			m_save.quality = (m_save.quality + q + 3) % 3;
			m_r.SetQuality(m_save.quality);
			InvalidatePortraits();
		}
		by += Ku(40);
		if (Sel(ID_BASE_ANIM, bx, by, bw, Ku(36), TT(TX_SET_ANIM), TT(m_save.battleAnim ? TX_SET_ANIM_SHORT : TX_SET_ANIM_FULL))) m_save.battleAnim = !m_save.battleAnim;
		by += Ku(40);
		if (Sel(ID_BASE_GRID, bx, by, bw, Ku(36), TT(TX_SET_GRID), TT(TacGridOn(m_save) ? TX_CR_ON : TX_CR_OFF))) m_save.showGrid ^= 1;
		by += Ku(40);
		if (Sel(ID_BASE_THREAT, bx, by, bw, Ku(36), TT(TX_SET_THREAT), TT(TacThreatOn(m_save) ? TX_CR_ON : TX_CR_OFF))) m_save.showGrid ^= 2;
		by += Ku(40);
		by = DrawSoundOpts(bx, by, bw, k);
	}
	if (by > helpY) helpY = min(by, y + ph - helpH - 4.f);
	if (helpY + helpH > y + ph - 2.f) helpY = y + ph - helpH - 2.f;
	if (Btn(ID_BASE_HELP, bx, helpY, bw * .48f, helpH, TT(TX_BT_HELP))) m_showHelp = TRUE;
	if (Btn(ID_BASE_CLOSE, bx + bw * .52f, helpY, bw * .48f, helpH, TT(TX_BASE_CLOSE))) { Persist(); m_wantClose = TRUE; }
	// ヒント
	const wchar_t* hint = NULL;
	if (RosterCount() == 0) hint = TT(TX_BASE_HINT_NOCHAR);
	else if (np == 0) hint = TT(TX_BASE_HINT_NOPARTY);
	if (hint) {
		float hw = m_r.TextWidth(hint, U(18), 1) + U(40);
		float hx = x + pw + (m_w - x - pw - hw) * .5f;
		float pulse = .75f + .25f * sinf(m_time * 3.f);
		m_r.HudPanel(hx, m_h - U(90), hw, U(44), RGBA8(20, 20, 40, 220), RGBA8(255, 210, 120, (int)(255 * pulse)));
		m_r.HudText(hint, hx + hw * .5f, m_h - U(68), U(18), RGBA8(255, 236, 190), 1, 1);
	}
	// 出撃メンバー（5 人で 1 段、拡張後は 2 段）
	int cap = PartyCap();
	int cols = 5;
	int rows = (cap + cols - 1) / cols;
	float gap = U(10);
	float availW = (float)m_w - x - pw - U(28);
	float cw = min(U(118), (availW - gap * (cols - 1)) / cols);
	if (cw < 48.f) cw = 48.f;
	float chh = min(U(150), ((float)m_h - U(120)) / rows - gap);
	if (chh < 64.f) chh = 64.f;
	float total = cw * cols + gap * (cols - 1);
	float cx0 = x + pw + max(8.f, ((float)m_w - x - pw - total) * .5f);
	float cy = U(24);
	m_r.HudText(TT(TX_PT_DEPLOY), cx0, cy + U(10), U(16), RGBA8(255, 226, 150), 0, 1);
	cy += U(24);
	int hovR = -1;
	float hovX = 0, hovY = 0;
	for (int i = 0; i < cap; i++) {
		int col = i % cols, row = i / cols;
		float cx = cx0 + col * (cw + gap);
		float ry = cy + row * (chh + gap);
		int r = m_save.party[i];
		m_r.HudPanel(cx, ry, cw, chh, RGBA8(16, 20, 36, 200), RGBA8(150, 128, 86));
		if (r >= 0 && m_save.roster[r].used) {
			const CharData& c = m_save.roster[r];
			float ps = min(cw - U(16), chh * .55f);
			DrawPortrait(RosterPortrait(r), cx + (cw - ps) * .5f, ry + U(6), ps, RGBA8(120, 160, 255));
			m_r.HudText(c.name, cx + cw * .5f, ry + ps + U(16), min(U(14), chh * .1f), RGBA8(255, 255, 255), 1, 1);
			wchar_t l[64];
			swprintf_s(l, L"%s %s %d", ClassName(c.cls), TT(TX_MISC_LV), c.lv);
			m_r.HudText(l, cx + cw * .5f, ry + ps + U(32), min(U(12), chh * .08f), RGBA8(210, 210, 200), 1, 0);
		} else {
			m_r.HudText(L"—", cx + cw * .5f, ry + chh * .5f, U(22), RGBA8(120, 120, 130), 1, 0);
		}
		if (r >= 0 && m_save.roster[r].used && Hit(cx, ry, cw, chh)) { hovR = r; hovX = cx; hovY = ry; }
	}
	if (hovR >= 0) {
			const CharData& c = m_save.roster[hovR];
			float tw = min(U(320.f), (float)m_w * .34f);
			float th = U(250);
			float tx = hovX + cw + U(8);
			if (tx + tw > (float)m_w - 8.f) tx = hovX - tw - U(8);
			if (tx < 8.f) tx = 8.f;
			float ty = hovY;
			if (ty + th > (float)m_h - 8.f) ty = (float)m_h - th - 8.f;
			if (ty < 8.f) ty = 8.f;
			m_r.HudPanel(tx, ty, tw, th, RGBA8(16, 18, 32, 235), RGBA8(210, 180, 110));
			float ps = U(64);
			DrawPortrait(RosterPortrait(hovR), tx + U(10), ty + U(10), ps, RGBA8(120, 160, 255));
			m_r.HudText(c.name, tx + U(82), ty + U(22), U(16), RGBA8(255, 236, 190), 0, 1);
			wchar_t line[96];
			swprintf_s(line, L"%s  %s %d", ClassName(c.cls), TT(TX_MISC_LV), c.lv);
			m_r.HudText(line, tx + U(82), ty + U(44), U(13), RGBA8(210, 214, 230), 0, 0);
			const wchar_t* wpn = (c.reserved[6] > 0) ? GearName(c.reserved[6]) : TT(TX_PT_NONE);
			const wchar_t* arm = (c.reserved[7] > 0) ? GearName(c.reserved[7]) : TT(TX_PT_NONE);
			swprintf_s(line, L"%s  %s", TT(TX_GEAR_WPN), wpn);
			m_r.HudText(line, tx + U(10), ty + U(84), U(12), RGBA8(230, 226, 210), 0, 0);
			swprintf_s(line, L"%s  %s", TT(TX_GEAR_ARM), arm);
			m_r.HudText(line, tx + U(10), ty + U(102), U(12), RGBA8(230, 226, 210), 0, 0);
			float sy = ty + U(124);
			for (int st = 0; st <= S_MOV && sy < ty + th - U(8); st++) {
				swprintf_s(line, L"%s %d", TT(TX_ST_HP + st), c.stat[st]);
				float col = (st % 2) ? tx + tw * .52f : tx + U(10);
				if ((st % 2) == 0 && st) sy += U(16);
				m_r.HudText(line, col, sy, U(12), RGBA8(220, 224, 236), 0, 0);
			}
	}
	if (!m_sortieAsk) {
		float bw2 = min(U(220), max(120.f, (float)m_w * .2f));
		float bh2 = U(38);
		float bx2 = (float)m_w - bw2 - max(8.f, U(16));
		float by2 = (float)m_h - bh2 - max(8.f, U(16));
		int shown = m_themeSel >= 0 ? m_themeSel : m_showMap.theme;
		if (int d = Sel(ID_BASE_THEME, bx2, by2 - U(44), bw2, U(34), TT(TX_BT_TERRAIN), TT(ThemeNameTx(shown)))) {
			int cur = shown;
			m_themeSel = (cur + (d > 0 ? 1 : TH_COUNT - 1)) % TH_COUNT;
			m_mapSeed = 0;
			m_mapSeedStage = -1;
			m_showBuilt = FALSE;
			BuildShowcase();
			TacSfx(TS_OK_MAP);
			return;
		}
		m_btnSfx = TS_OK_MAP;
		if (Btn(ID_BASE_REROLL, bx2, by2, bw2, bh2, TT(TX_MAP_REROLL))) {
			m_mapSeed = 0;
			m_mapSeedStage = -1;
			m_showBuilt = FALSE;
			BuildShowcase();
			return;
		}
	}
	if (modal) {
		m_btns.clear();
		m_click = savedClick;
		m_activateId = savedAct;
		m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 140));
		float w = U(420), h = U(280);
		float sx = (m_w - w) * .5f, sy = (m_h - h) * .5f;
		Panel(sx, sy, w, h);
		m_r.HudText(TT(TX_SV_PICK), sx + w * .5f, sy + U(36), U(22), RGBA8(255, 226, 150), 1, 1);
		float bx2 = sx + U(40), bw2 = w - U(80), bh2 = U(42), yy = sy + U(64);
		BOOL mid = m_midStage == m_save.stageSel;
		BOOL aut = m_autoStage == m_save.stageSel;
		if (Btn(ID_SORTIE_START, bx2, yy, bw2, bh2, TT(TX_SV_FROMSTART))) { m_sortieAsk = 0; StartBattle(m_save.stageSel); return; }
		yy += bh2 + U(8);
		if (Btn(ID_SORTIE_MID, bx2, yy, bw2, bh2, TT(TX_SV_FROMMID), mid)) { m_sortieAsk = 0; ResumeBattle(1); return; }
		yy += bh2 + U(8);
		if (Btn(ID_SORTIE_AUTO, bx2, yy, bw2, bh2, TT(TX_SV_FROMAUTO), aut)) { m_sortieAsk = 0; ResumeBattle(2); return; }
		yy += bh2 + U(8);
		if (Btn(ID_SORTIE_CANCEL, bx2, yy, bw2, bh2, TT(TX_CR_BACK))) m_sortieAsk = 0;
	}
}

// ============================================================ キャラクタ作成
int Game::CreateClassList(int* out) const
{
	static const int war[4] = { C_SOLDIER, C_FIGHTER, C_ARCHER, C_THIEF };
	static const int mag[2] = { C_MAGE, C_CLERIC };
	if (m_lineage == 0) { for (int i = 0; i < 4; i++) out[i] = war[i]; return 4; }
	for (int i = 0; i < 2; i++) out[i] = mag[i];
	return 2;
}

void Game::EnterCreate(int editIdx)
{
	m_screen = SC_CREATE;
	m_editIdx = editIdx;
	m_textFocus = 0;
	if (editIdx >= 0) {
		m_edit = m_save.roster[editIdx];
		m_lineage = ClassInfo(m_edit.cls).magic;
	} else {
		memset(&m_edit, 0, sizeof(m_edit));
		m_edit.gender = m_rng.Range(0, 1);
		m_lineage = 0;
		m_edit.cls = C_SOLDIER;
		m_edit.body = B_KNIGHT;
		m_edit.hair = m_edit.gender ? 2 : 1;
		m_edit.hairColor = m_rng.Range(0, HAIR_COLORS - 1);
		m_edit.outfit = 0;
		m_edit.hat = 0;
		m_edit.cape = 1;
		m_edit.lv = 1;
		RandomName(m_edit.name, m_edit.gender, m_rng);
	}
	m_previewYaw = 0;
	m_previewPitch = .18f;
	m_previewZoom = 1.f;
	m_previewPan = v3(0, 0, 0);
	m_previewPanDrag = FALSE;
	m_previewMot = 0;
	m_focusId = ID_CR_NAME;
	RefreshPreview();
}

static BOOL PreviewAlt(int style)
{
	return AttackClipFor(style, 0) != AttackClipFor(style, 1);
}

static void CharPreviewIn(const CharData& c, const Actor& a, int& style, int& idle, BOOL& fly)
{
	const ClassDef& cd = ClassInfo(c.cls);
	idle = (cd.weaponLook == WL_AXE2 || cd.weaponLook == WL_SWORD2) ? CL_2H_IDLE : CL_IDLE;
	Unit u;
	memset(&u, 0, sizeof(u));
	u.monster = -1;
	u.ch = c;
	style = UnitMotionStyle(u, -1);
	fly = cd.moveType == MV_FLY || a.wings > 0;
}

static void MonPreviewIn(int id, const Actor& a, int& style, BOOL& fly)
{
	const MonsterDef& md = MonsterInfo(id);
	style = md.attackStyle;
	fly = md.moveType == MV_FLY || a.wings > 0;
}

void Game::PlayShown(Actor& a, int& mot, int style, int idleClip, BOOL fly, BOOL giveWings, BOOL restart)
{
	if (mot < 0 || mot > 5) mot = 0;
	if (mot == 5 && !PreviewAlt(style)) mot = 4;
	ActorPreview(a, m_lib, m_r, mot, style, idleClip, fly, giveWings, restart);
}

int Game::DrawMotRow(int id0, float cx, float y, float avail, int mot, BOOL alt)
{
	static const int tx[6] = { TX_CR_MOT_IDLE, TX_CR_MOT_WALK, TX_CR_MOT_RUN, TX_CR_MOT_READY, TX_CR_MOT_ATK, TX_CR_MOT_ATK2 };
	int n = alt ? 6 : 5;
	float gap = U(6);
	float bh = U(32);
	if (avail < U(220)) avail = U(220);
	float bw = (avail - gap * (n - 1)) / n;
	if (bw > U(108)) bw = U(108);
	if (bw < U(52)) bw = U(52);
	float span = bw * n + gap * (n - 1);
	float x = cx - span * .5f;
	float px = bw < U(72) ? U(12) : U(14);
	int hit = -1;
	for (int i = 0; i < n; i++) {
		if (Btn(id0 + i, x, y, bw, bh, TT(tx[i]), TRUE, mot == i, px)) hit = i;
		x += bw + gap;
	}
	return hit;
}

void Game::RefreshPreview()
{
	CharData c = m_edit;
	ActorFromChar(m_preview, m_lib, m_r, c);
	int style = 0, idle = CL_IDLE;
	BOOL fly = FALSE;
	CharPreviewIn(c, m_preview, style, idle, fly);
	PlayShown(m_preview, m_previewMot, style, idle, fly, TRUE, FALSE);
}

void Game::DrawCreate3D()
{
	V3 pos = v3(0, -50.f, 0);
	Actor& a = m_preview;
	float ht = a.htScale > 0.05f ? a.htScale : 1.f;
	float h = a.height / ht + a.hover;
	ClampPreviewPan(m_previewPan, h);
	V3 look = pos + v3(0, h * .52f, 0) + m_previewPan;
	float dist = h * 2.9f * m_previewZoom;
	float yaw = PI;
	float pitch = Clampf(m_previewPitch, -1.05f, 1.15f);
	V3 dir = v3(sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch));
	V3 eye = look + dir * dist;
	V3 right = Norm(Cross(v3(0, 1, 0), dir * -1.f));
	V3 shift = right * (-dist * .14f);
	M4 view = MLookAtLH(eye + shift, look + shift, v3(0, 1, 0));
	M4 proj = MPerspLH(.55f, (float)m_w / max(1, m_h), .05f, 60.f);
	m_r.SetCamera(view, proj, eye, m_time);
	m_r.SetFog(500.f, 1000.f, 0, RGBA8(0, 0, 0));
	m_r.SetAmbient(RGBA8(210, 196, 182), RGBA8(150, 118, 96), .60f, .55f);
	m_r.SetLight(Norm(v3(.42f, .86f, -.42f)), look, 4.5f);
	m_r.SetShadowEnabled(TRUE);
	ActorPose(a, m_lib, m_r);
	M4 aw = ActorWorld(a, pos + v3(0, a.hover + sinf(m_time * 2.f) * .03f * (a.hover > 0), 0), PI + m_previewYaw);
	DrawParams dp;
	dp.outline = .01f;
	dp.rim = .55f;
	m_r.BeginShadowPass();
	ActorDraw(m_r, m_lib, a, aw, dp, m_time);
	m_r.BeginMainPass();
	m_r.DrawSky(RGBA8(22, 28, 48), RGBA8(58, 64, 88), 0, 0);
	ActorDraw(m_r, m_lib, a, aw, dp, m_time);
	m_r.DrawParticles(m_parts.data(), m_parts.size());
}

void Game::DrawCreateUi()
{
	float pw = U(420), x = U(20), y = U(20), ph = (float)m_h - U(40);
	Panel(x, y, pw, ph);
	m_r.HudText(TT(m_editIdx >= 0 ? TX_CR_EDIT : TX_CR_TITLE), x + pw * .5f, y + U(30), U(22), RGBA8(255, 226, 150), 1, 1);
	float bx = x + U(18), bw = pw - U(36), rh = U(30), gap = U(3);
	float yy = y + U(58);
	// 名前
	{
		float nw = bw - U(110);
		BOOL foc = m_textFocus != 0;
		UiBtn b = { ID_CR_NAME, bx, yy, nw, rh, 1, 0 };
		m_btns.push_back(b);
		if (ClickIn(bx, yy, nw, rh) || m_activateId == ID_CR_NAME) { m_textFocus = 1; m_activateId = -1; }
		BOOL nf = m_navMode && m_focusId == ID_CR_NAME;
		m_r.HudRect(bx, yy, nw, rh, foc ? RGBA8(40, 46, 80, 240) : RGBA8(20, 24, 44, 230));
		m_r.HudFrame(bx, yy, nw, rh, U(1.5f), foc || nf ? RGBA8(255, 230, 150) : RGBA8(120, 104, 74));
		m_r.HudText(TT(TX_CR_NAME), bx + U(8), yy + rh * .5f, U(13), RGBA8(200, 190, 160), 0, 0);
		wchar_t nm[NAME_LEN + 4];
		swprintf_s(nm, L"%s%s", m_edit.name, (foc && fmodf(m_time, 1.f) < .5f) ? L"|" : L"");
		m_r.HudText(nm[0] ? nm : TT(TX_CR_NAMEHINT), bx + U(70), yy + rh * .5f, U(17), nm[0] ? RGBA8(255, 255, 255) : RGBA8(140, 140, 150), 0, 1);
		if (Btn(ID_CR_RANDOM, bx + nw + U(8), yy, U(102), rh, TT(TX_CR_RANDOM), TRUE, FALSE, U(14))) RandomName(m_edit.name, m_edit.gender, m_rng);
	}
	yy += rh + gap + U(4);
	BOOL changed = FALSE;
	if (int d = Sel(ID_CR_GENDER, bx, yy, bw, rh, TT(TX_CR_GENDER), TT(m_edit.gender ? TX_CR_FEMALE : TX_CR_MALE))) { (void)d; m_edit.gender = !m_edit.gender; CharSetAvatar(m_edit, 0); changed = TRUE; }
	yy += rh + gap;
	if (int d = Sel(ID_CR_MODEL, bx, yy, bw, rh, TT(TX_CR_MODEL), TT(TX_MDL_0 + CharModel(m_edit)))) {
		CharSetModel(m_edit, (CharModel(m_edit) + d + MODEL_N) % MODEL_N); changed = TRUE;
	}
	yy += rh + gap;
	BOOL isNew = m_editIdx < 0;
	if (isNew) {
		if (Sel(ID_CR_LINEAGE, bx, yy, bw, rh, TT(TX_CR_LINEAGE), TT(m_lineage ? TX_CR_MAGIC : TX_CR_WARRIOR))) {
			m_lineage = !m_lineage;
			int lst[4];
			CreateClassList(lst);
			m_edit.cls = lst[0];
			CharSetBody(m_edit, m_lineage ? B_MAGE : B_KNIGHT);
			changed = TRUE;
		}
		yy += rh + gap;
		int lst[4];
		int n = CreateClassList(lst);
		int ci = 0;
		for (int i = 0; i < n; i++) if (lst[i] == m_edit.cls) ci = i;
		if (int d = Sel(ID_CR_CLASS, bx, yy, bw, rh, TT(TX_CR_CLASS), ClassName(m_edit.cls))) {
			ci = (ci + d + n) % n;
			m_edit.cls = lst[ci];
			static const int bodyFor[6] = { B_KNIGHT, B_BARBARIAN, B_ROGUE, B_ROGUE_HOOD, B_MAGE, B_MAGE };
			if (m_edit.cls < 6) CharSetBody(m_edit, bodyFor[m_edit.cls]);
			changed = TRUE;
		}
		yy += rh + gap;
	}
	static const int bodyTx[B_COUNT] = { TX_BODY_KNIGHT, TX_BODY_BARBARIAN, TX_BODY_MAGE, TX_BODY_ROGUE, TX_BODY_HOODED };
	if (int d = Sel(ID_CR_BODY, bx, yy, bw, rh, TT(TX_CR_BODY), TT(bodyTx[CharBody(m_edit)]))) { CharSetBody(m_edit, (CharBody(m_edit) + d + B_COUNT) % B_COUNT); changed = TRUE; }
	yy += rh + gap;
	{
		int nf = Clampi(m_r.FaceCount(m_edit.gender ? 1 : 0), 1, 6);
		int fc = CharFace(m_edit) % nf;
		if (int d = Sel(ID_CR_FACE, bx, yy, bw, rh, TT(TX_CR_FACE), TT((m_edit.gender ? TX_FACE_F0 : TX_FACE_M0) + fc))) {
			CharSetFace(m_edit, (fc + d + nf) % nf); changed = TRUE;
		}
	}
	yy += rh + gap;
	{
		const int g = m_edit.gender ? 1 : 0;
		const int nMax = g ? TX_AVA_F31 - TX_AVA_F0 + 1 : TX_AVA_M18 - TX_AVA_M0 + 1;
		int n = Clampi(m_r.AvatarCount(g), 0, nMax) + 1;
		int av = Clampi(CharAvatar(m_edit), 0, n - 1);
		int tx = av == 0 ? TX_AVA_NONE : (g ? TX_AVA_F0 : TX_AVA_M0) + av - 1;
		if (int d = Sel(ID_CR_AVATAR, bx, yy, bw, rh, TT(TX_CR_AVATAR), TT(tx))) {
			CharSetAvatar(m_edit, (av + d + n) % n); changed = TRUE;
		}
	}
	yy += rh + gap;
	{
		int col = CharAvaCol(m_edit);
		if (int d = Sel(ID_CR_AVACOL, bx, yy, bw, rh, TT(TX_CR_AVACOL), TT(TX_AVC_0 + col))) {
			CharSetAvaCol(m_edit, (col + d + 8) % 8); changed = TRUE;
		}
		DWORD tc = AvaClothTint(col);
		if (tc) m_r.HudRect(bx + bw * .4f + U(26), yy + U(9), U(16), rh - U(18), tc | 0xFF000000);
	}
	yy += rh + gap;
	{
		static const int kHairTx[HAIR_STYLES] = {
			TX_HAIR_DEFAULT, TX_HAIR_SHORT, TX_HAIR_LONG, TX_HAIR_PONY, TX_HAIR_TWIN, TX_HAIR_BOB,
			TX_HAIR_STRAIGHT, TX_HAIR_WAIST, TX_HAIR_AFRO, TX_HAIR_UPDO, TX_HAIR_BUN, TX_HAIR_FEATHER
		};
		if (int d = Sel(ID_CR_HAIR, bx, yy, bw, rh, TT(TX_CR_HAIR), TT(kHairTx[Clampi(m_edit.hair, 0, HAIR_STYLES - 1)]))) {
			m_edit.hair = (m_edit.hair + d + HAIR_STYLES) % HAIR_STYLES; changed = TRUE;
		}
	}
	yy += rh + gap;
	{
		wchar_t v[16];
		swprintf_s(v, L"%d", m_edit.hairColor + 1);
		if (int d = Sel(ID_CR_HAIRCOL, bx, yy, bw, rh, TT(TX_CR_HAIRCOLOR), v)) { m_edit.hairColor = (m_edit.hairColor + d + HAIR_COLORS) % HAIR_COLORS; changed = TRUE; }
		DWORD hc = HairColor(m_edit.hairColor);
		m_r.HudRect(bx + bw * .4f + U(26), yy + U(9), U(16), rh - U(18), hc | 0xFF000000);
	}
	yy += rh + gap;
	{
		wchar_t v[16];
		swprintf_s(v, L"%d", CharOutfit(m_edit) + 1);
		if (int d = Sel(ID_CR_OUTFIT, bx, yy, bw, rh, TT(TX_CR_OUTFIT), v)) {
			CharSetOutfit(m_edit, (CharOutfit(m_edit) + d + OUTFIT_COLORS) % OUTFIT_COLORS); changed = TRUE;
		}
	}
	yy += rh + gap;
	if (int d = Sel(ID_CR_HEIGHT, bx, yy, bw, rh, TT(TX_CR_HEIGHT), TT(TX_HT_0 + CharHt(m_edit)))) {
		CharSetHt(m_edit, (CharHt(m_edit) + d + HT_N) % HT_N); changed = TRUE;
	}
	yy += rh + gap;
	if (int d = Sel(ID_CR_FIG, bx, yy, bw, rh, TT(TX_CR_FIG), TT(TX_FIG_0 + CharFig(m_edit)))) {
		CharSetFig(m_edit, (CharFig(m_edit) + d + FIG_N) % FIG_N); changed = TRUE;
	}
	yy += rh + gap;
	if (m_edit.gender) {
		if (int d = Sel(ID_CR_BUST, bx, yy, bw, rh, TT(TX_CR_BUST), TT(TX_BUST_0 + CharBust(m_edit)))) {
			CharSetBust(m_edit, (CharBust(m_edit) + d + BUST_N) % BUST_N); changed = TRUE;
		}
		yy += rh + gap;
	}
	if (Sel(ID_CR_HAT, bx, yy, bw, rh, TT(TX_CR_HAT), TT(m_edit.hat ? TX_CR_ON : TX_CR_OFF))) { m_edit.hat = !m_edit.hat; changed = TRUE; }
	yy += rh + gap;
	if (Sel(ID_CR_CAPE, bx, yy, bw, rh, TT(TX_CR_CAPE), TT(m_edit.cape ? TX_CR_ON : TX_CR_OFF))) { m_edit.cape = !m_edit.cape; changed = TRUE; }
	yy += rh + gap + U(6);
	if (changed) RefreshPreview();
	// クラス説明・初期能力
	{
		const ClassDef& cd = ClassInfo(m_edit.cls);
		float dh = (y + ph - U(64)) - yy;
		if (dh > U(80)) {
			m_r.HudRect(bx, yy, bw, dh, RGBA8(10, 12, 24, 160));
			m_r.HudText(ClassName(m_edit.cls), bx + U(10), yy + U(14), U(16), RGBA8(255, 226, 150), 0, 1);
			wchar_t info[96];
			swprintf_s(info, L"%s %s  %s %d-%d", TT(cd.magic ? TX_CR_MAGIC : TX_CR_WARRIOR), cd.moveType == MV_FLY ? TT(TX_MISC_FLY) : L"", TT(TX_MISC_RANGE), cd.rMin, cd.rMax);
			m_r.HudText(info, bx + bw - U(10), yy + U(14), U(13), RGBA8(200, 200, 210), 2, 0);
			m_r.HudTextBox(TT(cd.descTx), bx + U(10), yy + U(28), bw - U(20), U(40), U(13), RGBA8(226, 226, 232));
			if (dh > U(180) && isNew) {
				CharData t = m_edit;
				t.lv = 1;
				CharRecalcInit(t);
				DrawStatLines(t, bx + U(10), yy + U(74), bw - U(20), NULL, -1, -1);
			}
		}
	}
	float fy = y + ph - U(54);
	BOOL nameOk = m_edit.name[0] != 0;
	if (Btn(ID_CR_OK, bx, fy, bw * .58f, U(42), TT(isNew ? TX_CR_CREATE : TX_CR_SAVE), nameOk, FALSE, U(19), RGBA8(60, 130, 70, 240))) {
		if (isNew) {
			int slot = -1;
			for (int i = 0; i < RosterCap(); i++) if (!m_save.roster[i].used) { slot = i; break; }
			if (slot >= 0) {
				CharData c = m_edit;
				c.used = 1;
				c.lv = 1; c.exp = 0;
				c.uid = m_save.uidNext++;
				CharRecalcInit(c);
				m_save.roster[slot] = c;
				if (PartyCount() < PartyCap()) {
					int pcap = PartyCap();
					for (int i = 0; i < pcap; i++) if (m_save.party[i] < 0) { m_save.party[i] = slot; break; }
				}
				Persist();
				m_partySel = slot;
			}
		} else {
			CharData& c = m_save.roster[m_editIdx];
			wcsncpy_s(c.name, m_edit.name, _TRUNCATE);
			c.gender = m_edit.gender; c.body = m_edit.body; c.hair = m_edit.hair; c.hairColor = m_edit.hairColor;
			c.outfit = m_edit.outfit; c.hat = m_edit.hat; c.cape = m_edit.cape;
			Persist();
		}
		InvalidatePortraits();
		m_showN = 0;
		BuildShowcase();
		AddBanner(m_edit.name, ClassName(m_edit.cls), RGBA8(255, 226, 150), 2, 2.f);
		EnterParty();
		return;
	}
	if (Btn(ID_CR_BACK, bx + bw * .62f, fy, bw * .38f, U(42), TT(TX_CR_BACK))) {
		if (m_createFromParty) { m_createFromParty = 0; EnterParty(); return; }
		EnterBase();
		return;
	}
	{
		int style = 0, idle = CL_IDLE;
		BOOL fly = FALSE;
		CharPreviewIn(m_edit, m_preview, style, idle, fly);
		float paneL = U(440);
		float paneW = (float)m_w - paneL;
		float cx = paneL + paneW * .5f;
		int hit = DrawMotRow(ID_CR_MOT0, cx, m_h - U(68), paneW - U(24), m_previewMot, PreviewAlt(style));
		if (hit >= 0) {
			m_previewMot = hit;
			PlayShown(m_preview, m_previewMot, style, idle, fly, TRUE, TRUE);
		}
		m_r.HudText(TT(TX_CR_PREVIEW_HINT), cx, m_h - U(28), U(13), RGBA8(220, 220, 230), 1, 0);
	}
}

// ============================================================ 編成
void Game::EnterParty()
{
	for (int i = 0; i < MAX_ROSTER; i++)
		if (m_save.roster[i].used) CharGrantReached(m_save.roster[i]);
	m_screen = SC_PARTY;
	m_confirmDelete = 0;
	m_eqPick = 0;
	m_textFocus = 0;
	if (m_partySel < 0 || !m_save.roster[m_partySel].used) {
		m_partySel = -1;
		for (int i = 0; i < MAX_ROSTER; i++) if (m_save.roster[i].used) { m_partySel = i; break; }
	}
	if (m_partySel >= 0) m_rosterPage = m_partySel / 20;
	m_focusId = ID_PT_CARD0 + (m_partySel >= 0 ? m_partySel % 20 : 0);
	BuildShowcase();
}

void Game::TogglePartyMember(int idx)
{
	int cap = PartyCap();
	for (int i = 0; i < cap; i++) if (m_save.party[i] == idx) {
		m_save.party[i] = -1;
		int k = 0;
		int tmp[MAX_PARTY];
		for (int j = 0; j < cap; j++) if (m_save.party[j] >= 0) tmp[k++] = m_save.party[j];
		for (int j = 0; j < MAX_PARTY; j++) m_save.party[j] = j < k ? tmp[j] : -1;
		BuildShowcase();
		return;
	}
	if (PartyCount() >= cap) {
		wchar_t hint[180];
		swprintf_s(hint, TT(TX_PT_HINT), cap);
		AddBanner(hint, NULL, RGBA8(255, 200, 150), 2, 2.f);
		return;
	}
	for (int i = 0; i < cap; i++) if (m_save.party[i] < 0) { m_save.party[i] = idx; BuildShowcase(); return; }
	wchar_t hint[180];
	swprintf_s(hint, TT(TX_PT_HINT), cap);
	AddBanner(hint, NULL, RGBA8(255, 200, 150), 2, 2.f);
}

void Game::DeleteRoster(int idx)
{
	for (int i = 0; i < MAX_PARTY; i++) if (m_save.party[i] == idx) m_save.party[i] = -1;
	memset(&m_save.roster[idx], 0, sizeof(CharData));
	int k = 0, tmp[MAX_PARTY];
	for (int j = 0; j < MAX_PARTY; j++) if (m_save.party[j] >= 0) tmp[k++] = m_save.party[j];
	for (int j = 0; j < MAX_PARTY; j++) m_save.party[j] = j < k ? tmp[j] : -1;
	Persist();
	m_partySel = -1;
	for (int i = 0; i < MAX_ROSTER; i++) if (m_save.roster[i].used) { m_partySel = i; break; }
	InvalidatePortraits();
	BuildShowcase();
}

void Game::DrawEquipLayer()
{
	if (m_eqPick < 1 || m_partySel < 0 || m_partySel >= MAX_ROSTER || !m_save.roster[m_partySel].used) { m_eqPick = 0; return; }
	CharData& c = m_save.roster[m_partySel];
	int kind = m_eqPick;
	m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 150));
	float pw = min(U(640), (float)m_w - U(40));
	float ph = min(U(540), (float)m_h - U(32));
	float px = ((float)m_w - pw) * .5f, py = ((float)m_h - ph) * .5f;
	if (m_click && !Hit(px, py, pw, ph)) { m_eqPick = 0; m_click = FALSE; TacSfx(TS_CANCEL); return; }
	Panel(px, py, pw, ph);

	const wchar_t* title = TT(TX_GEAR_WPN);
	wchar_t cur[96];
	wcscpy_s(cur, TT(TX_PT_NONE));
	int ids[96], n = 0, curId = 0;
	BOOL gear = kind <= 2;
	int beadSlot = kind == 5 ? 0 : kind == 6 ? 1 : kind == 9 ? 2 : kind == 10 ? 3 : -1;
	BOOL pasPick = kind == 7 || kind == 8;
	BOOL sumPick = kind == 11 || kind == 12;
	int sumSlot = kind == 12 ? 1 : 0;
	if (kind <= 2) {
		int gk = kind == 1 ? 0 : 1;
		title = TT(gk ? TX_GEAR_ARM : TX_GEAR_WPN);
		int eq = c.reserved[gk ? 7 : 6];
		if (eq > 0) wcscpy_s(cur, GearName(eq));
		for (int i = 0; i < m_save.gearN && n < 96; i++) {
			const GearDef* g = GearOf(m_save.gearId[i]);
			if (!g || g->kind != gk || !GearCanWear(c.cls, g)) continue;
			ids[n++] = i;
		}
	} else if (kind <= 4) {
		int slot = kind - 3;
		title = TT(TX_ACC);
		curId = EquippedAccId(c, slot);
		if (curId > 0) wcscpy_s(cur, AccName(curId));
		for (int i = 0; i < AccCount() && n < 96; i++)
			if (AccSpare(c, slot, i) > 0 || i + 1 == curId) ids[n++] = i + 1;
	} else if (pasPick) {
		int slot = kind - 7;
		title = TT(TX_PAS);
		curId = EquippedPasId(c, slot);
		if (curId > 0) wcscpy_s(cur, PasName(curId));
		for (int i = 0; i < PasCount() && n < 96; i++)
			if (PasSpare(c, slot, i) > 0 || i + 1 == curId) ids[n++] = i + 1;
	} else if (sumPick) {
		title = SummonWord();
		curId = EquippedSummonId(c, sumSlot);
		if (curId > 0) wcscpy_s(cur, SummonName(curId));
		for (int i = 0; i < SummonCount() && n < 96; i++)
			if (SummonSpare(c, sumSlot, i) > 0 || i + 1 == curId) ids[n++] = i + 1;
	} else if (beadSlot >= 0) {
		title = beadSlot == 3 ? TT(TX_OUGI) : TT(TX_BEAD);
		curId = EquippedBeadId(c, beadSlot);
		if (curId > 0) wcscpy_s(cur, CraftName(curId - 1));
		for (int i = 0; i < CRAFT_N && n < 96; i++)
			if (BeadSpare(c, beadSlot, i) > 0 || i + 1 == curId) ids[n++] = i + 1;
	}
	m_r.HudText(title, px + pw * .5f, py + U(22), U(20), RGBA8(255, 226, 150), 1, 1);
	m_r.HudText(cur, px + pw * .5f, py + U(46), U(14), RGBA8(220, 220, 230), 1, 0);

	BOOL beadPick = beadSlot >= 0;
	float listTop = py + U(62), listBot = py + ph - U(58) - ((beadPick || pasPick || sumPick) ? U(48) : 0);
	int per = (int)((listBot - listTop) / U(36)) - 1;
	if (per < 3) per = 3;
	if (per > 18) per = 18;
	int pages = max(1, (n + per - 1) / per);
	if (m_eqPage >= pages) m_eqPage = pages - 1;
	if (m_eqPage < 0) m_eqPage = 0;
	float yy = listTop;
	BOOL noneSel = gear ? (c.reserved[kind == 1 ? 6 : 7] <= 0) : (curId <= 0);
	if (Btn(ID_EQ_NONE, px + U(16), yy, pw - U(32), U(32), TT(TX_PT_NONE), TRUE, noneSel, U(14))) {
		BOOL ok = TRUE;
		if (gear) {
			int sl = kind == 1 ? 6 : 7;
			if (c.reserved[sl] > 0) {
				if (!StashAdd(m_save, c.reserved[sl])) ok = FALSE;
				else c.reserved[sl] = 0;
			}
			if (ok) { Persist(); InvalidatePortraits(); BuildShowcase(); }
		} else if (kind <= 4) {
			ok = EquipAccId(c, kind - 3, 0);
			if (ok) Persist();
		} else if (pasPick) {
			ok = EquipPasId(c, kind - 7, 0);
			if (ok) Persist();
		} else if (sumPick) {
			ok = EquipSummonId(c, sumSlot, 0);
			if (ok) Persist();
		} else if (beadSlot >= 0) {
			ok = EquipBeadId(c, beadSlot, 0);
			if (ok) Persist();
		}
		if (ok) m_eqPick = 0;
		else TacSfx(TS_ERROR);
		return;
	}
	yy += U(36);
	int a = m_eqPage * per, b = min(n, a + per);
	int hovBead = -1;
	for (int s = 0; s < per; s++) {
		int ix = a + s;
		if (ix >= b) break;
		wchar_t row[180];
		BOOL sel = FALSE;
		if (gear) {
			const GearDef* g = GearOf(m_save.gearId[ids[ix]]);
			if (!g) continue;
			swprintf_s(row, L"%s   %s+%d %s+%d %s+%d %s+%d", GearName(m_save.gearId[ids[ix]]),
				TT(TX_ST_STR), g->str, TT(TX_ST_MAG), g->mag, TT(TX_ST_DEF), g->def, TT(TX_ST_RES), g->res);
		} else if (kind <= 4) {
			int id = ids[ix];
			sel = id == curId;
			wchar_t st[96];
			st[0] = 0;
			for (int stat = S_STR; stat <= S_LUK; stat++) {
				int v = AccStat(id, stat);
				if (!v) continue;
				wchar_t bit[24];
				swprintf_s(bit, L" %s+%d", TT(StatNameTx(stat)), v);
				wcscat_s(st, bit);
			}
			swprintf_s(row, L"%s  x%d%s", AccName(id), max(1, AccSpare(c, kind - 3, id - 1)), st);
		} else if (pasPick) {
			int id = ids[ix];
			sel = id == curId;
			swprintf_s(row, L"%s  x%d", PasName(id), max(1, PasSpare(c, kind - 7, id - 1)));
			if (Hit(px + U(16), yy, pw - U(32), U(32)) || (m_navMode && m_focusId == ID_EQ_ROW0 + s)) hovBead = id;
		} else if (sumPick) {
			int id = ids[ix];
			sel = id == curId;
			swprintf_s(row, L"%s  x%d  %sG", SummonName(id), max(1, SummonSpare(c, sumSlot, id - 1) + (id == curId ? 1 : 0)), TacNum(SummonPrice(id - 1)));
			if (Hit(px + U(16), yy, pw - U(32), U(32)) || (m_navMode && m_focusId == ID_EQ_ROW0 + s)) hovBead = id;
		} else if (beadSlot >= 0) {
			int id = ids[ix];
			sel = id == curId;
			BOOL ougi = IsCraftSuper(CRAFT_BASE + id - 1);
			{
				const SkillDef& sd = SkillInfo(CRAFT_BASE + id - 1);
				wchar_t cp[24];
				CraftCostText(id - 1, cp, 24);
				swprintf_s(row, L"%s  %s  %s%s  x%d", CraftName(id - 1), CraftShapeName(sd.area, sd.shape), cp, ougi ? L" ★" : L"", max(1, BeadSpare(c, beadSlot, id - 1)));
			}
			if (Hit(px + U(16), yy, pw - U(32), U(32)) || (m_navMode && m_focusId == ID_EQ_ROW0 + s)) hovBead = id;
		}
		if (Btn(ID_EQ_ROW0 + s, px + U(16), yy, pw - U(32), U(32), row, TRUE, sel, U(13))) {
			BOOL ok = TRUE;
			if (gear) {
				GearEquip(c, m_save, ids[ix]);
				Persist(); InvalidatePortraits(); BuildShowcase();
			} else if (kind <= 4) {
				ok = EquipAccId(c, kind - 3, ids[ix]);
				if (ok) Persist();
			} else if (pasPick) {
				ok = EquipPasId(c, kind - 7, ids[ix]);
				if (ok) Persist();
			} else if (sumPick) {
				ok = EquipSummonId(c, sumSlot, ids[ix]);
				if (ok) Persist();
			} else if (beadSlot >= 0) {
				ok = EquipBeadId(c, beadSlot, ids[ix]);
				if (ok) Persist();
			}
			if (ok) m_eqPick = 0;
			else TacSfx(TS_ERROR);
			return;
		}
		yy += U(36);
	}
	if (beadPick || pasPick || sumPick) {
		int show = hovBead > 0 ? hovBead : curId;
		if (show > 0) {
			const wchar_t* tip = sumPick ? SummonDesc(show) : (pasPick ? PasDesc(show) : CraftDesc(show - 1));
			m_r.HudTextBox(tip, px + U(16), py + ph - U(96), pw - U(32), U(44), U(12), RGBA8(210, 220, 235));
		}
	}
	wchar_t pg[32];
	swprintf_s(pg, L"%d / %d", m_eqPage + 1, pages);
	if (pages > 1 && Btn(ID_EQ_PREV, px + U(16), py + ph - U(46), U(64), U(32), L"◀")) m_eqPage = (m_eqPage + pages - 1) % pages;
	m_r.HudText(pg, px + U(120), py + ph - U(30), U(14), RGBA8(220, 220, 230), 0, 1);
	if (pages > 1 && Btn(ID_EQ_NEXT, px + U(168), py + ph - U(46), U(64), U(32), L"▶")) m_eqPage = (m_eqPage + 1) % pages;
	if (Btn(ID_EQ_CLOSE, px + pw - U(170), py + ph - U(46), U(150), U(32), TT(TX_BASE_CLOSE))) m_eqPick = 0;
	m_activateId = -1;
}

void Game::DrawPartyUi()
{
	BOOL modal = m_eqPick != 0;
	BOOL savedClick = m_click;
	int savedAct = m_activateId;
	if (modal) { m_click = FALSE; m_activateId = -1; }
	float x = max(8.f, min(U(20), (float)m_w * .02f));
	float y = max(8.f, min(U(20), (float)m_h * .02f));
	float gw = min((float)m_w * .5f, min(U(640), (float)m_w - x - U(440)));
	if (gw < U(340)) gw = min((float)m_w * .58f, (float)m_w - x - U(200));
	float gh = (float)m_h - y * 2.f;
	if (gw < 200.f) gw = min(200.f, (float)m_w - 16.f);
	Panel(x, y, gw, gh);
	m_r.HudText(TT(TX_PT_TITLE), x + U(20), y + U(26), U(22), RGBA8(255, 226, 150), 0, 1);
	int rCap = RosterCap(), pCap = PartyCap();
	int pages = max(1, (rCap + 19) / 20);
	if (m_rosterPage < 0) m_rosterPage = 0;
	if (m_rosterPage >= pages) m_rosterPage = pages - 1;
	int begin = m_rosterPage * 20;
	wchar_t cnt[80];
	swprintf_s(cnt, L"%s %d/%d   %s %d/%d", TT(TX_BASE_ROSTER), RosterCount(), rCap, TT(TX_PT_DEPLOY), PartyCount(), pCap);
	m_r.HudText(cnt, x + gw - U(20), y + U(26), U(14), RGBA8(220, 220, 220), 2, 0);
	int cols = 5;
	float pad = U(14);
	float cw = (gw - pad * 2 - U(8) * (cols - 1)) / cols;
	float ch = min(cw * 1.28f, (gh - U(130)) / 4.f - U(8));
	float ps = min(cw - U(16), ch - U(44));
	for (int local = 0; local < 20; local++) {
		int i = begin + local;
		if (i >= rCap) break;
		float cx = x + pad + (local % cols) * (cw + U(8));
		float cy = y + U(52) + (local / cols) * (ch + U(8));
		const CharData& c = m_save.roster[i];
		if (!c.used) {
			if (Btn(ID_PT_CARD0 + local, cx, cy, cw, ch, L"+", RosterCount() < rCap, FALSE, U(28), RGBA8(30, 40, 60, 160))) { m_createFromParty = 1; EnterCreate(-1); return; }
			continue;
		}
		BOOL inParty = FALSE;
		for (int p = 0; p < pCap; p++) if (m_save.party[p] == i) inParty = TRUE;
		if (Btn(ID_PT_CARD0 + local, cx, cy, cw, ch, L"", TRUE, m_partySel == i, 0, inParty ? RGBA8(50, 80, 140, 240) : 0)) {
			if (m_partySel == i) TogglePartyMember(i);
			m_partySel = i;
			m_confirmDelete = 0;
		}
		DrawPortrait(RosterPortrait(i), cx + (cw - ps) * .5f, cy + U(6), ps, inParty ? RGBA8(120, 180, 255) : RGBA8(110, 100, 80));
		m_r.HudText(c.name, cx + cw * .5f, cy + U(6) + ps + U(12), U(13), m_partySel == i ? RGBA8(40, 24, 8) : RGBA8(255, 255, 255), 1, 1, m_partySel != i);
		wchar_t l[48];
		swprintf_s(l, L"%s%d", TT(TX_MISC_LV), c.lv);
		m_r.HudText(l, cx + cw * .5f, cy + U(6) + ps + U(28), U(11), m_partySel == i ? RGBA8(60, 40, 10) : RGBA8(210, 210, 200), 1, 0, m_partySel != i);
		if (inParty) m_r.HudText(L"★", cx + U(12), cy + U(14), U(14), RGBA8(255, 220, 90), 1, 1);
	}
	if (pages > 1) {
		wchar_t pg[32];
		swprintf_s(pg, L"%d / %d", m_rosterPage + 1, pages);
		if (Btn(ID_PT_PAGE_PREV, x + gw * .5f - U(110), y + gh - U(92), U(36), U(28), L"◀")) { m_rosterPage = (m_rosterPage + pages - 1) % pages; InvalidatePortraits(); return; }
		m_r.HudText(pg, x + gw * .5f, y + gh - U(78), U(13), RGBA8(230, 230, 240), 1, 1);
		if (Btn(ID_PT_PAGE_NEXT, x + gw * .5f + U(74), y + gh - U(92), U(36), U(28), L"▶")) { m_rosterPage = (m_rosterPage + 1) % pages; InvalidatePortraits(); return; }
	} else {
		wchar_t hint[180];
		swprintf_s(hint, TT(TX_PT_HINT), pCap);
		m_r.HudText(hint, x + gw * .5f, y + gh - U(64), U(13), RGBA8(210, 210, 220), 1, 0);
	}
	if (Btn(ID_PT_BACK, x + gw - U(180), y + gh - U(46), U(160), U(36), TT(TX_CR_BACK))) {
		Persist();
		if (m_partyBack && m_shop) { m_partyBack = 0; m_screen = SC_BASE; m_focusId = ID_SHOP_CLOSE; return; }
		EnterBase();
		return;
	}
	if (Btn(ID_PT_NEW, x + U(20), y + gh - U(46), U(160), U(36), TT(TX_BASE_CREATE), RosterCount() < rCap)) { m_createFromParty = 1; EnterCreate(-1); return; }
	if (Btn(ID_PT_SHOP, x + U(190), y + gh - U(46), U(140), U(36), TT(TX_SHOP))) {
		if (m_shop) { m_partyBack = 0; m_screen = SC_BASE; m_focusId = ID_SHOP_CLOSE; return; }
		m_shopBack = 1;
		m_shop = 1;
		m_shopPage = 0;
		m_shopSellPage = 0;
		m_shopKind = 0;
		m_shopSuper = 0;
		m_shopFit = 0;
		m_shopHold = 0;
		if (m_partySel >= 0 && m_save.roster[m_partySel].used) m_shopLine = ClassLine(m_save.roster[m_partySel].cls);
		m_screen = SC_BASE;
		return;
	}
	// 詳細
	float dx = x + gw + U(14), dw = (float)m_w - dx - U(20);
	if (dw < U(220) || m_partySel < 0) {
		if (modal) m_eqPick = 0;
		return;
	}
	CharData& c = m_save.roster[m_partySel];
	if (!c.used) return;
	Panel(dx, y, dw, gh);
	float ps2 = min(U(96), dw * .34f);
	DrawPortrait(RosterPortrait(m_partySel), dx + U(14), y + U(14), ps2, RGBA8(200, 170, 110));
	m_r.HudText(c.name, dx + ps2 + U(26), y + U(30), U(20), RGBA8(255, 255, 255), 0, 1);
	m_r.HudText(ClassName(c.cls), dx + ps2 + U(26), y + U(56), U(15), RGBA8(255, 226, 150), 0, 1);
	wchar_t l[96];
	int need = ExpToNext(c.lv);
	int left = c.lv >= TAC_LV_MAX ? 0 : max(0, need - c.exp);
	swprintf_s(l, L"%s %d   %s %d/%d  %s %d", TT(TX_MISC_LV), c.lv, TT(TX_MISC_EXP), c.exp, need, TT(TX_EXP_LEFT), left);
	m_r.HudText(l, dx + ps2 + U(26), y + U(80), U(14), RGBA8(220, 220, 220), 0, 0);
	Bar(dx + ps2 + U(26), y + U(94), dw - ps2 - U(44), U(8), need > 0 ? c.exp / (float)need : 0.f, RGBA8(120, 200, 255), RGBA8(60, 120, 220));
	swprintf_s(l, L"%s %d  /  %s %d", TT(TX_BT_DEFEATED), c.kills, TT(TX_BT_STAGE), c.battles);
	m_r.HudText(l, dx + ps2 + U(26), y + U(114), U(12), RGBA8(190, 190, 190), 0, 0);
	float gearH = U(22);
	float gearGap = U(2);
	float gearBlock = gearH * 6.f + gearGap * 5.f;
	float actH = U(36);
	float evoH = U(26);
	float bot = y + gh - U(8);
	float actY = bot - actH;
	float evoY = actY - U(4) - evoH;
	float gearY = evoY - U(6) - gearBlock;
	auto gearRow = [&](int r) { return gearY + r * (gearH + gearGap); };
	int gearBonus[S_COUNT];
	CharFightBonus(c, gearBonus);
	float sy = y + max(ps2 + U(18.f), U(128.f));
	sy += DrawStatLines(c, dx + U(14), sy, dw - U(28), gearBonus, -1, -1) + U(6);
	m_r.HudText(TT(TX_PT_SKILLS), dx + U(14), sy + U(4), U(15), RGBA8(255, 226, 150), 0, 1);
	sy += U(18);
	struct SkRow { int kind; int id; DWORD col; };
	SkRow sks[160];
	int ns = 0;
	auto pushSk = [&](int kind, int id, DWORD col) { if (ns < 160) sks[ns++] = { kind, id, col }; };
	for (int s = 0; s < SKL_COUNT; s++) {
		if (!CharHasSkill(c, s)) continue;
		const SkillDef& sd = SkillInfo(s);
		if (sd.enemyOnly) continue;
		DWORD col = RGBA8(236, 236, 240);
		if (sd.kind == SK_PHYS) col = RGBA8(255, 214, 160);
		else if (sd.kind == SK_MAGIC) col = RGBA8(214, 198, 255);
		else if (sd.kind == SK_HEAL) col = RGBA8(166, 230, 190);
		else if (sd.kind == SK_BUFF) col = RGBA8(176, 220, 255);
		else if (sd.kind == SK_PASSIVE) col = RGBA8(196, 196, 214);
		pushSk(0, s, col);
	}
	for (int slot = 0; slot < 3; slot++) {
		int id = EquippedBeadId(c, slot);
		if (id > 0) pushSk(1, CRAFT_BASE + id - 1, RGBA8(255, 206, 120));
	}
	{
		int id = EquippedBeadId(c, 3);
		if (id > 0) pushSk(1, CRAFT_BASE + id - 1, RGBA8(255, 176, 110));
	}
	for (int slot = 0; slot < 2; slot++) {
		int id = EquippedPasId(c, slot);
		if (id > 0) pushSk(2, id, RGBA8(150, 214, 255));
	}
	for (int slot = 0; slot < 2; slot++) {
		int id = EquippedSummonId(c, slot);
		if (id > 0) pushSk(3, SUMMON_BASE + id - 1, RGBA8(255, 170, 120));
	}
	float skillTop = sy;
	float skillBot = gearY - U(6);
	if (skillBot < skillTop + U(16)) skillBot = skillTop + U(16);
	if (!ns) {
		m_r.HudText(TT(TX_PT_NONE), dx + U(20), skillTop + U(9), U(13), RGBA8(160, 160, 170), 0, 0);
	} else {
		float avail = skillBot - skillTop;
		if (avail < U(16)) avail = U(16);
		int cols = dw >= U(420) ? 2 : 1;
		while (cols < 4 && avail / (float)((ns + cols - 1) / cols) < U(16)) cols++;
		int rows = (ns + cols - 1) / cols;
		float rowH = avail / (float)rows;
		if (rowH > U(20)) rowH = U(20);
		float fs = rowH < U(15) ? U(11) : U(13);
		float colW = (dw - U(16)) / (float)cols;
		for (int i = 0; i < ns; i++) {
			int col = i % cols;
			int row = i / cols;
			float hx = dx + U(8) + col * colW;
			float hy = skillTop + row * rowH;
			float cy = hy + rowH * .5f;
			BOOL hov = !modal && m_mouseSeen && Hit(hx, hy, colW - U(4), rowH);
			if (hov) {
				m_skillTip = sks[i].kind;
				m_skillTipId = sks[i].id;
				m_r.HudRect(hx, hy, colW - U(4), rowH, RGBA8(255, 220, 140, 40));
			}
			wchar_t sl[140];
			if (sks[i].kind == 1) {
				BOOL ougi = IsCraftSuper(sks[i].id);
				swprintf_s(sl, L"%s %s%s", ougi ? TT(TX_OUGI) : TT(TX_CRAFT), CraftName(sks[i].id), ougi ? L" ★" : L"");
			} else if (sks[i].kind == 2) {
				swprintf_s(sl, L"%s %s", TT(TX_PAS), PasName(sks[i].id));
			} else if (sks[i].kind == 3) {
				swprintf_s(sl, L"%s %s", SummonWord(), SkillLabel(sks[i].id));
			} else {
				const SkillDef& sd = SkillInfo(sks[i].id);
				wchar_t tag[24];
				if (sd.kind == SK_PASSIVE) wcscpy_s(tag, TT(TX_MISC_PASSIVE));
				else if (sd.shape == SH_LINE || sd.shape == SH_FAN) swprintf_s(tag, L"%s%d", CraftShapeName(sd.area, sd.shape), sd.area);
				else if (sd.area <= 0) wcscpy_s(tag, TT(TX_SHAPE_ONE));
				else if (sd.area == 1) wcscpy_s(tag, TT(TX_MISC_AREA));
				else swprintf_s(tag, L"%s%d", TT(TX_SHAPE_WIDE), sd.area);
				if (sd.kind == SK_PASSIVE) swprintf_s(sl, L"%s (%s)", SkillLabel(sks[i].id), tag);
				else swprintf_s(sl, L"%s %s MP%d", SkillLabel(sks[i].id), tag, sd.mp);
			}
			m_r.HudText(sl, hx + U(2), cy, fs, hov ? RGBA8(255, 244, 210) : sks[i].col, 0, 0);
		}
	}
	// 操作
	{
		float c0 = dx + U(10), c1 = dx + dw * .52f;
		float w0 = dw * .48f - U(4), w1 = dw * .46f;
		const GearDef* wpn = GearOf(c.reserved[6]);
		const GearDef* arm = GearOf(c.reserved[7]);
		wchar_t gl[96];
		swprintf_s(gl, L"%s: %s", TT(TX_GEAR_WPN), wpn ? GearName(c.reserved[6]) : TT(TX_PT_NONE));
		if (Btn(ID_PT_WPN, c0, gearRow(0), w0, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 1; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
		swprintf_s(gl, L"%s: %s", TT(TX_GEAR_ARM), arm ? GearName(c.reserved[7]) : TT(TX_PT_NONE));
		if (Btn(ID_PT_ARM, c1, gearRow(0), w1, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 2; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
		swprintf_s(gl, L"%s1: %s", TT(TX_ACC), EquippedAccName(c, 0));
		if (Btn(ID_PT_ACC0, c0, gearRow(1), w0, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 3; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
		swprintf_s(gl, L"%s2: %s", TT(TX_ACC), EquippedAccName(c, 1));
		if (Btn(ID_PT_ACC1, c1, gearRow(1), w1, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 4; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
		swprintf_s(gl, L"%s1: %s", TT(TX_CRAFT), EquippedBeadName(c, 0));
		if (Btn(ID_PT_BEAD0, c0, gearRow(2), w0, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 5; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
		swprintf_s(gl, L"%s2: %s", TT(TX_CRAFT), EquippedBeadName(c, 1));
		if (Btn(ID_PT_BEAD1, c1, gearRow(2), w1, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 6; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
		swprintf_s(gl, L"%s3: %s", TT(TX_CRAFT), EquippedBeadName(c, 2));
		if (Btn(ID_PT_BEAD2, c0, gearRow(3), w0, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 9; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
		swprintf_s(gl, L"%s: %s", TT(TX_OUGI), EquippedBeadName(c, 3));
		if (Btn(ID_PT_BEAD3, c1, gearRow(3), w1, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 10; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
		swprintf_s(gl, L"%s1: %s", TT(TX_PAS), EquippedPasName(c, 0));
		if (Btn(ID_PT_PAS0, c0, gearRow(4), w0, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 7; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
		swprintf_s(gl, L"%s2: %s", TT(TX_PAS), EquippedPasName(c, 1));
		if (Btn(ID_PT_PAS1, c1, gearRow(4), w1, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 8; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
		swprintf_s(gl, L"%s1: %s", SummonWord(), EquippedSummonName(c, 0));
		if (Btn(ID_PT_SUM0, c0, gearRow(5), w0, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 11; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
		swprintf_s(gl, L"%s2: %s", SummonWord(), EquippedSummonName(c, 1));
		if (Btn(ID_PT_SUM1, c1, gearRow(5), w1, gearH, gl, TRUE, FALSE, U(11))) { m_eqPick = 12; m_eqPage = 0; m_focusId = ID_EQ_NONE; }
	}
	if (!m_confirmDelete && Btn(ID_PT_EVO, dx + U(10), evoY, dw - U(20), evoH, TT(TX_BASE_EVOLVE), TRUE, FALSE, U(13))) {
		m_evFromParty = 1;
		int sel = m_partySel;
		EnterEvolve();
		m_evJump = 1;
		if (sel >= 0 && m_save.roster[sel].used && EvoMainEligible(m_save.roster[sel])) {
			EvoOption opt[4];
			if (EvoOptions(m_save.roster[sel].cls, opt) > 0) { m_evMain = sel; m_evPick = 1; }
		}
		return;
	}
	BOOL inParty = FALSE;
	for (int p = 0; p < MAX_PARTY; p++) if (m_save.party[p] == m_partySel) inParty = TRUE;
	if (m_confirmDelete) {
		m_r.HudText(TT(TX_PT_DELETE_Q), dx + dw * .5f, evoY + evoH * .5f, U(15), RGBA8(255, 170, 160), 1, 1);
		if (Btn(ID_PT_YES, dx + U(14), actY, dw * .5f - U(20), actH, TT(TX_PT_YES), TRUE, FALSE, 0, RGBA8(170, 50, 40, 240))) { DeleteRoster(m_partySel); m_confirmDelete = 0; m_eqPick = 0; return; }
		if (Btn(ID_PT_NO, dx + dw * .5f + U(6), actY, dw * .5f - U(20), actH, TT(TX_PT_NO))) m_confirmDelete = 0;
	} else {
		float bw3 = (dw - U(28) - U(16)) / 3.f;
		if (Btn(ID_PT_DEPLOY, dx + U(14), actY, bw3, actH, TT(TX_PT_DEPLOY), TRUE, inParty, U(14))) TogglePartyMember(m_partySel);
		if (Btn(ID_PT_EDIT, dx + U(14) + bw3 + U(8), actY, bw3, actH, TT(TX_CR_EDIT), TRUE, FALSE, U(14))) { m_eqPick = 0; m_createFromParty = 1; EnterCreate(m_partySel); return; }
		if (Btn(ID_PT_DELETE, dx + U(14) + (bw3 + U(8)) * 2, actY, bw3, actH, TT(TX_PT_DELETE), TRUE, FALSE, U(14), RGBA8(120, 40, 36, 230))) m_confirmDelete = 1;
	}
	if (m_eqPick) {
		if (modal) { m_click = savedClick; m_activateId = savedAct; }
		DrawEquipLayer();
	}
}

// ============================================================ 進化
void Game::EnterEvolve()
{
	m_screen = SC_EVOLVE;
	m_evMain = -1; m_evTarget = -1;
	m_evPartner[0] = m_evPartner[1] = -1;
	m_evPick = 0;
	m_evJump = 0;
	m_evMainPage = 0;
	m_evPartPage = 0;
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) m_portraitWant[i] = (i < MAX_ROSTER) ? i + 1 : 0;
	m_focusId = ID_EV_MAIN0;
	BuildShowcase();
}

void Game::DoEvolve()
{
	if (m_evMain < 0 || m_evTarget < 0) return;
	CharData& mc = m_save.roster[m_evMain];
	int need = EvoPartnersNeeded(mc);
	CharData ps[2];
	int n = 0;
	for (int k = 0; k < need; k++) if (m_evPartner[k] >= 0) ps[n++] = m_save.roster[m_evPartner[k]];
	if (n < need) return;
	EvoApply(mc, m_evTarget, ps, n);
	for (int k = 0; k < n; k++) {
		int idx = m_evPartner[k];
		for (int i = 0; i < MAX_PARTY; i++) if (m_save.party[i] == idx) m_save.party[i] = -1;
		memset(&m_save.roster[idx], 0, sizeof(CharData));
	}
	int k2 = 0, tmp[MAX_PARTY];
	for (int j = 0; j < MAX_PARTY; j++) if (m_save.party[j] >= 0) tmp[k2++] = m_save.party[j];
	for (int j = 0; j < MAX_PARTY; j++) m_save.party[j] = j < k2 ? tmp[j] : -1;
	Persist();
	InvalidatePortraits();
	m_evFlash = .9f;
	AddBanner(TT(TX_EV_DONE), ClassName(mc.cls), RGBA8(255, 226, 120), 1, 2.6f);
	TacSfx(TS_PROMOTE);
	m_partySel = m_evMain;
	m_evMain = -1;
	m_evPick = 0;
	m_evTarget = -1;
	m_evPartner[0] = m_evPartner[1] = -1;
	m_evPartPage = 0;
	BuildShowcase();
}

void Game::DrawEvolveUi()
{
	float x = U(20), y = U(20), w = (float)m_w - U(40), h = (float)m_h - U(40);
	Panel(x, y, w, h);
	m_r.HudText(TT(TX_EV_TITLE), x + U(20), y + U(26), U(22), RGBA8(255, 226, 150), 0, 1);
	m_r.HudText(TT(TX_EV_HINT_MAIN), x + w - U(20), y + U(26), U(13), RGBA8(210, 210, 220), 2, 0);
	float colW = (w - U(60)) / 3.f;
	float c0 = x + U(20), c1 = c0 + colW + U(10), c2 = c1 + colW + U(10);
	float top = y + U(56);
	float rowH = U(52);
	if (m_evMain >= 0) {
		const CharData& held = m_save.roster[m_evMain];
		if (!held.used || !EvoMainEligible(held)) {
			m_evMain = -1;
			m_evTarget = -1;
			m_evPartner[0] = m_evPartner[1] = -1;
			m_evPick = 0;
		}
	}
	// 1列目: 本体
	m_r.HudText(TT(TX_EV_MAIN), c0, top + U(8), U(16), RGBA8(255, 226, 150), 0, 1);
	int mains[MAX_ROSTER];
	int nElig = 0;
	for (int i = 0; i < MAX_ROSTER; i++) {
		const CharData& c = m_save.roster[i];
		if (!c.used || !EvoMainEligible(c)) continue;
		EvoOption opt[4];
		if (EvoOptions(c.cls, opt) == 0) continue;
		mains[nElig++] = i;
	}
	float listTop = top + U(24);
	float listBot = y + h - U(70);
	int fitMain = (int)((listBot - listTop) / rowH);
	if (fitMain < 1) fitMain = 1;
	int mainPages = nElig > 0 ? (nElig + fitMain - 1) / fitMain : 1;
	if (mainPages > 1) {
		listBot = y + h - U(112);
		fitMain = (int)((listBot - listTop) / rowH);
		if (fitMain < 1) fitMain = 1;
		mainPages = (nElig + fitMain - 1) / fitMain;
		if (mainPages < 1) mainPages = 1;
	}
	if (m_evJump && m_evMain >= 0) {
		int at = -1;
		for (int i = 0; i < nElig; i++) if (mains[i] == m_evMain) at = i;
		if (at >= 0) m_evMainPage = at / fitMain;
		m_evJump = 0;
	}
	if (m_evMainPage >= mainPages) m_evMainPage = mainPages - 1;
	if (m_evMainPage < 0) m_evMainPage = 0;
	m_evMainPages = mainPages;
	float ry = listTop;
	for (int k = m_evMainPage * fitMain; k < nElig && k < (m_evMainPage + 1) * fitMain; k++) {
		int i = mains[k];
		const CharData& c = m_save.roster[i];
		wchar_t l[96];
		swprintf_s(l, L"%s  %s %s%d", c.name, ClassName(c.cls), TT(TX_MISC_LV), c.lv);
		if (Btn(ID_EV_MAIN0 + i, c0 + rowH, ry, colW - rowH, rowH - U(6), l, TRUE, m_evMain == i, U(14))) {
			m_evMain = i; m_evTarget = -1; m_evPartner[0] = m_evPartner[1] = -1; m_evPick = 1; m_evPartPage = 0;
		}
		DrawPortrait(RosterPortrait(i), c0, ry, rowH - U(6), RGBA8(150, 130, 90));
		ry += rowH;
	}
	if (!nElig) m_r.HudTextBox(TT(TX_EV_NONE_ELIGIBLE), c0, top + U(30), colW, U(80), U(15), RGBA8(200, 200, 200));
	if (mainPages > 1) {
		float pageY = y + h - U(108);
		wchar_t pg[32];
		swprintf_s(pg, L"%d / %d", m_evMainPage + 1, mainPages);
		m_r.HudText(pg, c0 + colW * .5f, pageY + U(16), U(14), RGBA8(230, 230, 236), 1, 1);
		if (Btn(ID_EV_MAIN_PREV, c0, pageY, U(52), U(32), L"◀")) m_evMainPage = (m_evMainPage + mainPages - 1) % mainPages;
		if (Btn(ID_EV_MAIN_NEXT, c0 + colW - U(52), pageY, U(52), U(32), L"▶")) m_evMainPage = (m_evMainPage + 1) % mainPages;
	}
	// 2列目: 進化先
	if (m_evMain >= 0) {
		const CharData& mc = m_save.roster[m_evMain];
		m_r.HudText(TT(TX_EV_TARGET), c1, top + U(8), U(16), RGBA8(255, 226, 150), 0, 1);
		EvoOption opt[4];
		int no = EvoOptions(mc.cls, opt);
		if (m_evTarget >= 0) {
			BOOL still = FALSE;
			for (int k = 0; k < no; k++) if (opt[k].target == m_evTarget) still = TRUE;
			if (!still) {
				m_evTarget = -1;
				m_evPartner[0] = m_evPartner[1] = -1;
				m_evPick = 1;
			}
		}
		int need = EvoPartnersNeeded(mc);
		float ty = top + U(24);
		for (int k = 0; k < no; k++) {
			static const int gtx[G_COUNT] = { TX_G_MELEE, TX_G_SWIFT, TX_G_MAGIC, TX_G_HOLY };
			wchar_t l[128];
			swprintf_s(l, L"%s", ClassName(opt[k].target));
			if (Btn(ID_EV_TGT0 + k, c1, ty, colW, rowH - U(6), l, TRUE, m_evTarget == opt[k].target, U(16))) {
				m_evTarget = opt[k].target; m_evPartner[0] = m_evPartner[1] = -1; m_evPick = 2;
			}
			if (need > 0) {
				wchar_t g[96];
				swprintf_s(g, L"%s: %s", TT(TX_EV_NEED_GROUP), opt[k].needGroup < 0 ? TT(TX_EV_ANY) : TT(gtx[opt[k].needGroup]));
				m_r.HudText(g, c1 + U(8), ty + rowH + U(2), U(12), RGBA8(200, 200, 210), 0, 0);
				ty += rowH + U(22);
			} else ty += rowH;
		}
		if (m_evTarget >= 0) {
			const ClassDef& td = ClassInfo(m_evTarget);
			m_r.HudTextBox(TT(td.descTx), c1, ty + U(6), colW, U(70), U(13), RGBA8(226, 226, 232));
			ty += U(80);
			m_r.HudText(TT(TX_EV_PREVIEW), c1, ty + U(8), U(14), RGBA8(255, 226, 150), 0, 1);
			CharData pv = mc;
			CharData ps[2];
			int n = 0;
			for (int kk = 0; kk < 2; kk++) if (m_evPartner[kk] >= 0) ps[n++] = m_save.roster[m_evPartner[kk]];
			EvoApply(pv, m_evTarget, ps, n);
			int gains[S_COUNT];
			for (int s = 0; s < S_COUNT; s++) gains[s] = pv.stat[s] - mc.stat[s];
			DrawStatLines(pv, c1, ty + U(22), colW, gains, -1, -1);
		}
		if (m_evTarget >= 0 && EvoPartnersNeeded(mc) <= 0) {
			m_btnSfx = -2;
			if (Btn(ID_EV_DO, c1, y + h - U(56), colW * .72f, U(40), TT(TX_EV_DO), TRUE, FALSE, U(18), RGBA8(150, 90, 200, 240))) { DoEvolve(); return; }
		}
	}
	// 3列目: 素材（必要なときだけ）
	if (m_evMain >= 0 && m_evTarget >= 0 && EvoPartnersNeeded(m_save.roster[m_evMain]) > 0) {
		const CharData& mc = m_save.roster[m_evMain];
		int need = EvoPartnersNeeded(mc);
		int needGroup = -1;
		EvoOption opt[4];
		int no = EvoOptions(mc.cls, opt);
		for (int k = 0; k < no; k++) if (opt[k].target == m_evTarget) needGroup = opt[k].needGroup;
		int needLv = ClassInfo(mc.cls).tier >= 2 ? 15 : 5;
		wchar_t hdr[96];
		int nRoster = 0;
		for (int i = 0; i < MAX_ROSTER; i++) if (m_save.roster[i].used && i != m_evMain) nRoster++;
		swprintf_s(hdr, L"%s (%d)  %d", TT(TX_EV_PARTNER), need, nRoster);
		m_r.HudText(hdr, c2, top + U(8), U(16), RGBA8(255, 226, 150), 0, 1);
		m_r.HudTextBox(TT(TX_EV_HINT_PARTNER), c2, top + U(22), colW, U(36), U(12), RGBA8(255, 190, 170));
		int parts[MAX_ROSTER];
		BYTE partOk[MAX_ROSTER];
		int nPart = 0;
		for (int pass = 0; pass < 2; pass++) {
			for (int i = 0; i < MAX_ROSTER; i++) {
				const CharData& c = m_save.roster[i];
				if (!c.used || i == m_evMain) continue;
				BOOL okLv = EvoPartnerEligible(mc, c);
				if (pass == 0 && !okLv) continue;
				if (pass == 1 && okLv) continue;
				parts[nPart] = i;
				partOk[nPart] = okLv ? 1 : 0;
				nPart++;
			}
		}
		float py0 = top + U(62);
		float pageY = y + h - U(148);
		float listBot = pageY - U(6);
		int fit = (int)((listBot - py0) / rowH);
		if (fit < 1) fit = 1;
		int pages = nPart > 0 ? (nPart + fit - 1) / fit : 1;
		if (pages < 1) pages = 1;
		if (m_evPartPage >= pages) m_evPartPage = pages - 1;
		if (m_evPartPage < 0) m_evPartPage = 0;
		m_evPartPages = pages;
		float py = py0;
		for (int k = m_evPartPage * fit; k < nPart && k < (m_evPartPage + 1) * fit; k++) {
			int i = parts[k];
			const CharData& c = m_save.roster[i];
			BOOL can = partOk[k] != 0;
			BOOL picked = can && (m_evPartner[0] == i || m_evPartner[1] == i);
			wchar_t l[96];
			if (can) swprintf_s(l, L"%s  %s %s%d", c.name, ClassName(c.cls), TT(TX_MISC_LV), c.lv);
			else swprintf_s(l, L"%s  %s %s%d  (%s%d)", c.name, ClassName(c.cls), TT(TX_MISC_LV), c.lv, TT(TX_MISC_LV), needLv);
			if (Btn(ID_EV_PART0 + i, c2 + rowH, py, colW - rowH, rowH - U(6), l, can, picked, U(14))) {
				if (picked) { if (m_evPartner[0] == i) m_evPartner[0] = -1; if (m_evPartner[1] == i) m_evPartner[1] = -1; }
				else {
					if (m_evPartner[0] < 0) m_evPartner[0] = i;
					else if (need > 1 && m_evPartner[1] < 0) m_evPartner[1] = i;
					else m_evPartner[need - 1] = i;
				}
			}
			DrawPortrait(RosterPortrait(i), c2, py, rowH - U(6), can ? RGBA8(150, 130, 90) : RGBA8(80, 80, 88));
			py += rowH;
		}
		{
			wchar_t pg[32];
			swprintf_s(pg, L"%d / %d", m_evPartPage + 1, pages);
			m_r.HudText(pg, c2 + colW * .5f, pageY + U(16), U(14), RGBA8(230, 230, 236), 1, 1);
			if (Btn(ID_EV_PART_PREV, c2, pageY, U(52), U(32), L"◀", pages > 1)) m_evPartPage = (m_evPartPage + pages - 1) % pages;
			if (Btn(ID_EV_PART_NEXT, c2 + colW - U(52), pageY, U(52), U(32), L"▶", pages > 1)) m_evPartPage = (m_evPartPage + 1) % pages;
		}
		CharData ps[2];
		int n = 0;
		for (int k = 0; k < need; k++) if (m_evPartner[k] >= 0) ps[n++] = m_save.roster[m_evPartner[k]];
		BOOL ok = n >= need && EvoGroupSatisfied(needGroup, ps, n);
		if (n >= need && !ok) m_r.HudText(TT(TX_EV_NEED_GROUP), c2 + colW * .5f, y + h - U(72), U(13), RGBA8(255, 150, 140), 1, 1);
		m_btnSfx = -2;
		if (Btn(ID_EV_DO, c2, y + h - U(56), colW * .6f, U(40), TT(TX_EV_DO), ok, FALSE, U(18), RGBA8(150, 90, 200, 240))) { DoEvolve(); return; }
	} else m_evPartPages = 1;
	if (Btn(ID_EV_BACK, x + w - U(160) - U(20), y + h - U(56), U(160), U(40), TT(TX_CR_BACK))) {
		if (m_evFromParty) { m_evFromParty = 0; EnterParty(); return; }
		EnterBase();
		return;
	}
}

// ============================================================ 結果
void Game::DrawResultUi()
{
	float w = min((float)m_w - U(40), U(900)), h = min((float)m_h - U(40), U(560));
	float x = (m_w - w) * .5f, y = (m_h - h) * .5f;
	Panel(x, y, w, h);
	m_r.HudText(TT(m_victory ? TX_RS_STAGECLEAR : TX_BT_DEFEAT), x + w * .5f, y + U(34), U(30), m_victory ? RGBA8(255, 226, 120) : RGBA8(200, 160, 200), 1, 1);
	wchar_t st[64];
	swprintf_s(st, L"%s %d", TT(TX_BT_STAGE), m_battleStage);
	m_r.HudText(st, x + w * .5f, y + U(66), U(15), RGBA8(220, 220, 230), 1, 0);
	float rowH = min(U(76), (h - U(170)) / max(1, m_resultN));
	float ry = y + U(90);
	for (int i = 0; i < m_resultN; i++) {
		int r = m_resultRoster[i];
		if (r < 0 || !m_save.roster[r].used) continue;
		const CharData& c = m_save.roster[r];
		DrawPortrait(RosterPortrait(r), x + U(30), ry, rowH - U(8), RGBA8(120, 160, 255));
		m_r.HudText(c.name, x + U(30) + rowH + U(4), ry + rowH * .28f, U(17), RGBA8(255, 255, 255), 0, 1);
		m_r.HudText(ClassName(c.cls), x + U(30) + rowH + U(4), ry + rowH * .62f, U(13), RGBA8(255, 226, 150), 0, 0);
		wchar_t l[96];
		if (c.lv > m_resultLv0[i]) swprintf_s(l, L"%s %d → %d", TT(TX_MISC_LV), m_resultLv0[i], c.lv);
		else swprintf_s(l, L"%s %d", TT(TX_MISC_LV), c.lv);
		m_r.HudText(l, x + w * .5f, ry + rowH * .3f, U(16), c.lv > m_resultLv0[i] ? RGBA8(140, 255, 150) : RGBA8(230, 230, 230), 1, 1);
		int need = ExpToNext(c.lv);
		int left = c.lv >= TAC_LV_MAX ? 0 : max(0, need - c.exp);
		swprintf_s(l, L"%s +%s  %s %s", TT(TX_BT_EXP), TacNum(m_resultExp[i]), TT(TX_EXP_LEFT), TacNum(left));
		m_r.HudText(l, x + w * .5f, ry + rowH * .64f, U(13), RGBA8(200, 220, 255), 1, 0);
		Bar(x + w * .66f, ry + rowH * .4f, w * .28f, U(10), need > 0 ? c.exp / (float)need : 0.f, RGBA8(120, 200, 255), RGBA8(60, 120, 220));
		ry += rowH;
	}
	float by = y + h - U(60);
	float bw = U(200);
	if (m_victory && m_battleStage < TAC_STAGE_MAX) {
		if (Btn(ID_RS_NEXT, x + w * .5f - bw * 1.5f - U(12), by, bw, U(42), TT(TX_BT_NEXT), PartyCount() > 0, FALSE, U(17), RGBA8(170, 60, 50, 240))) {
			m_save.stageSel = Clampi(m_battleStage + 1, 1, min(TAC_STAGE_MAX, m_save.stageCleared + 1));
			StartBattle(m_save.stageSel);
			return;
		}
	} else if (!m_victory) {
		if (Btn(ID_RS_RETRY, x + w * .5f - bw * 1.5f - U(12), by, bw, U(42), TT(TX_BT_RETRY), PartyCount() > 0, FALSE, U(17), RGBA8(170, 60, 50, 240))) {
			StartBattle(m_battleStage);
			return;
		}
	}
	if (Btn(ID_RS_BASE, x + w * .5f - bw * .5f, by, bw, U(42), TT(TX_BT_TOBASE), TRUE, FALSE, U(17))) {
		m_showBuilt = FALSE;
		EnterBase();
		return;
	}
}

} // namespace otac
