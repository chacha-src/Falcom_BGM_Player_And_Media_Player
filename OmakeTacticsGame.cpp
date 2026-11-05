#include "stdafx.h"
#include "OmakeTacticsGame.h"
#include "OmakeTacticsAudio.h"
#include <afxdlgs.h>
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
	ID_CDX_FAM = 168, ID_CDX_SHAPE0 = 1720, ID_CDX_EL0 = 1730, ID_CDX_JP0 = 1740, ID_CDX_JPREV = 1750, ID_CDX_JNEXT,
	ID_OPT_BGM = 190, ID_OPT_SE, ID_OPT_CAM, ID_MON_TAB,
	ID_MON_FIND = 1760, ID_MON_CLEAR, ID_MON_LVPREV, ID_MON_LVNEXT, ID_MON_SEEN,
	ID_BASE_SHOP = 210, ID_SHOP_CLOSE, ID_SHOP_BUY0 = 220, ID_SHOP_SELL0 = 250,
	ID_SHOP_PREV = 270, ID_SHOP_NEXT, ID_SHOP_LINE0 = 272, ID_SHOP_KIND0 = 278,
	ID_SHOP_TIER0 = 1120,
	ID_SHOP_BAND0 = 1140, ID_SHOP_BAND1, ID_SHOP_BAND2, ID_SHOP_BAND3,
	ID_PT_SHOP = 362,
	ID_SHOP_FREE = 1890, ID_SHOP_SELLALL, ID_SHOP_FIT_NO, ID_SHOP_FIT_PREV, ID_SHOP_FIT_NEXT, ID_SHOP_FIT0,
	ID_SHOP_SELLJUNK = 1920, ID_SHOP_ARM0 = 1930, ID_SHOP_ARM_SELL = 1948, ID_SHOP_ARM_NO = 1949,
	ID_SHOP_ASK_YES = 1950, ID_SHOP_ASK_NO, ID_SHOP_FIT_KEEP, ID_SHOP_TO_PARTY, ID_SHOP_DET0 = 1960,
	ID_SELL_M10 = 1972, ID_SELL_M1, ID_SELL_P1, ID_SELL_P10, ID_SELL_MAX, ID_SELL_OK, ID_SELL_NO,
	ID_BASE_AI = 1870, ID_BASE_SYNC, ID_BASE_WIN, ID_BASE_FPS,
	ID_SHOP_SLOT0 = 1984, ID_SHOP_SLOT1, ID_SHOP_SLOT2, ID_SHOP_SLOT_NO,
	ID_PT_BEAD2 = 1988, ID_PT_BEAD3, ID_SHOP_ART, ID_SHOP_OUGI, ID_PT_SUM0, ID_PT_SUM1,
	ID_BASE_SET = 945, ID_BASE_THREAT = 949, ID_PT_EVO = 363,
	ID_PT_WPN = 348, ID_PT_ARM,
	ID_CR_NAME = 200, ID_CR_RANDOM, ID_CR_GENDER, ID_CR_LINEAGE, ID_CR_CLASS, ID_CR_BODY, ID_CR_HAIR, ID_CR_HAIRCOL, ID_CR_OUTFIT,
	ID_CR_HAT, ID_CR_CAPE, ID_CR_OK, ID_CR_BACK, ID_CR_HEIGHT, ID_CR_BUST, ID_CR_FIG, ID_CR_FACE, ID_CR_AVATAR, ID_CR_AVACOL, ID_CR_MODEL, ID_CR_WING, ID_CR_GROUND,
	ID_PT_CARD0 = 300, ID_PT_DEPLOY = 340, ID_PT_EDIT, ID_PT_DELETE, ID_PT_YES, ID_PT_NO, ID_PT_BACK, ID_PT_NEW,
	ID_PT_PAGE_PREV = 360, ID_PT_PAGE_NEXT,
	ID_PT_ACC0 = 366, ID_PT_ACC1, ID_PT_BEAD0, ID_PT_BEAD1, ID_PT_PAS0 = 370, ID_PT_PAS1,
	ID_PT_SKPREV = 380, ID_PT_SKNEXT,
	ID_EQ_NONE = 1860, ID_EQ_ROW0, ID_EQ_PREV = 1880, ID_EQ_NEXT, ID_EQ_CLOSE,
	ID_EV_MAIN0 = 1000, ID_EV_TGT0 = 1200, ID_EV_PART0 = 1300, ID_EV_DO = 1500, ID_EV_BACK,
	ID_EV_PART_PREV = 1510, ID_EV_PART_NEXT, ID_EV_MAIN_PREV, ID_EV_MAIN_NEXT,
	ID_CR_MOT0 = 1550, ID_MON_MOT0 = 1560,
	ID_RS_NEXT = 700, ID_RS_RETRY, ID_RS_BASE, ID_RS_LOOP,
	ID_HELP_CLOSE = 900,
	ID_BASE_REROLL = 940, ID_BASE_THEME, ID_BASE_LOOK = 942, ID_BASE_LOOK_BACK,
	ID_BASE_SAVESLOT = 947,
	ID_SLOT_ROW0 = 981, ID_SLOT_USE = 991, ID_SLOT_REN, ID_SLOT_COPY, ID_SLOT_DEL, ID_SLOT_BACK, ID_SLOT_YES, ID_SLOT_NO,
	ID_SLOT_OUT = 998, ID_SLOT_IN,
	ID_JOB_GO = 2200, ID_JOB_SKIP, ID_JOB_YES, ID_JOB_NO, ID_JOB_BACK, ID_JOB_NEXT, ID_JOB_OPT0 = 2210,
	ID_GROW_ROOT0 = 2300, ID_GROW_NODE0 = 2320, ID_GROW_BACK = 2390,
	ID_CDX_BRANCH = 2400,
	ID_BASE_PACE = 3100, ID_BASE_CUT
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
		{ { 36, 110, 150 }, { 140, 206, 214 }, { 90, 168, 186 }, { 110, 186, 200 }, { 40, 80, 90 }, .4f, .62f, .28f, .55f, .4f, { 48, 110, 100 } },
		{ { 150, 188, 214 }, { 226, 232, 236 }, { 200, 214, 220 }, { 170, 190, 200 }, { 80, 100, 80 }, .4f, .8f, .3f, .58f, .55f, { 90, 140, 80 } },
		{ { 18, 24, 58 }, { 48, 62, 110 }, { 28, 36, 70 }, { 80, 95, 140 }, { 36, 40, 58 }, .25f, .45f, .85f, .46f, .7f, { 70, 80, 108 } },
		{ { 18, 22, 36 }, { 70, 90, 110 }, { 40, 50, 70 }, { 80, 100, 120 }, { 40, 44, 52 }, .55f, .7f, .25f, .42f, .7f, { 90, 98, 108 } },
		{ { 48, 32, 72 }, { 140, 110, 150 }, { 90, 70, 110 }, { 130, 100, 150 }, { 70, 55, 78 }, .4f, .62f, .65f, .52f, .52f, { 168, 158, 176 } },
		{ { 40, 48, 90 }, { 210, 200, 230 }, { 160, 170, 200 }, { 180, 190, 220 }, { 90, 90, 110 }, .25f, .8f, .4f, .5f, .45f, { 186, 190, 210 } },
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
	memset(&m_hud, 0, sizeof(m_hud));
	m_hwnd = NULL;
	m_w = m_h = 8;
	m_ui = 1;
	m_time = 0;
	m_screen = SC_LOADING;
	m_wantClose = FALSE;
	m_goHome = FALSE;
	m_loaded = FALSE;
	m_loadFrames = 0;
	m_loadPm = 0;
	m_loadShown = -1;
	m_loadPaint = 0;
	m_loadA = m_loadB = 0;
	m_loadLive = FALSE;
	m_loadUnits = m_avaLoadUnits = m_loadHave = m_sliceBase = m_sliceUnits = 0;
	m_renderDt = 1.f / 60.f;
	m_lastRenderTime = 0;
	m_qApply = -1;
	m_qHold = -1;
	m_qMissN = 0;
	m_qOkN = 0;
	m_qCoolT = 0;
	m_qHz = 0;
	m_qSlots = 1;
	m_syncArm = -1;
	m_syncArmN = 0;
	m_syncRun = -1;
	m_qDrew = FALSE;
	m_syncLock = 0;
	m_fps = 0;
	m_workAvg = 0;
	m_msUpd = m_msSh = m_msRef = m_msMain = m_msPort = m_msHud = m_msPr = m_msAud = 0;
	ZeroMemory(m_fpsSpan, sizeof(m_fpsSpan));
	ZeroMemory(m_workSpan, sizeof(m_workSpan));
	m_fpsN = 0;
	m_fpsI = 0;
	m_fpsAt.QuadPart = 0;
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
	m_textCaret = 0;
	m_textReload = 0;
	m_imeComp[0] = 0;
	m_textBox[0] = m_textBox[1] = m_textBox[2] = m_textBox[3] = 0;
	m_textPx = 16.f;
	m_textPad = 0;
	m_keyShift = FALSE;
	memset(&m_padPrev, 0, sizeof(m_padPrev));
	m_padRepeat = 0; m_padDirPrev = -1; m_padTrig = 0;
	m_fastFwd = FALSE;
	m_showHelp = FALSE;
	m_codex = 0;
	m_codexPage = 0;
	m_monSel = 0;
	m_monBand = 0;
	m_monSeenOnly = 0;
	m_monFind[0] = 0;
	m_monYaw = 0;
	m_monShown = -2;
	m_monMot = 0;
	m_monDrag = FALSE;
	m_codexJob = 0;
	m_codexFam = 0;
	m_codexJobPage = 0;
	m_codexShape = 0;
	m_codexEl = 0;
	m_codexSort = 0;
	m_growPath[0] = C_SOLDIER;
	for (int gi = 1; gi < 14; gi++) m_growPath[gi] = -1;
	m_growN = 1;
	m_growScroll = 0.f;
	m_growSeen = 0;
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
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
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
	m_slotJob = 0;
	m_slotBusy = 0;
	m_impPath[0] = 0;
	for (int si = 0; si < TAC_SLOT_N; si++) m_slotClear[si] = -1;
	m_createFromParty = 0;
	m_evFromParty = 0;
	m_evJump = 0;
	m_statEq = 0;
	m_statEqPage = 0;
	m_itemAim = -1;
	m_itemPage = 0;
	m_aimPage = 0;
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
	m_sceneAcc = 0;
	m_wxAcc = 0;
	m_wxMeshWait = 0;
	m_wxMesh = 0;
	memset(m_cover, 0, sizeof(m_cover));
	m_eye = v3(0, 10, 0);
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) {
		m_portraitKey[i] = -1;
		m_portraitWant[i] = 0;
		m_ptCacheWant[i] = -1;
		m_ptCacheLook[i] = 0;
	}
	memset(&m_ptActor, 0, sizeof(m_ptActor));
	memset(m_ptCache, 0, sizeof(m_ptCache));
	memset(&m_showMap, 0, sizeof(m_showMap));
	m_showBuilt = FALSE;
	m_showStage = 0;
	m_showTheme = -2;
	m_themeSel = -1;
	m_showN = 0;
	m_showYaw = 0;
	m_mapLook = 0;
	m_homeUi = 1;
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
	m_previewWing = TRUE;
	m_previewGround = FALSE;
	m_partySel = -1;
	m_eqPick = 0;
	m_eqPage = 0;
	m_partySkPage = 0;
	m_partySkPages = 1;
	m_partySkX = m_partySkY = m_partySkW = m_partySkH = 0;
	m_evMain = m_evTarget = -1;
	m_evPartner[0] = m_evPartner[1] = -1;
	m_evPick = 0;
	m_evMainPage = 0; m_evMainPages = 1;
	m_evPartPage = 0; m_evPartPages = 1;
	m_evFlash = 0;
	m_evoCut = 0;
	m_evoT = 0;
	m_evoRoster = m_evoFrom = m_evoTo = -1;
	m_jobRedo = 0;
	m_jobWho = m_jobStep = m_jobN = m_jobSkipAll = 0;
	m_jobSel = -1;
	for (int i = 0; i < 12; i++) m_jobPath[i] = -1;
	memset(&m_b, 0, sizeof(m_b));
	m_phase = m_phaseBeforeMenu = PH_NONE;
	m_phaseT = 0;
	m_cur = -1;
	m_cmdFaced = 0;
	m_hoverUnit = -1;
	m_cursorValid = FALSE;
	m_cx = m_cz = 0;
	m_cLayer = 0;
	m_layerLock = 0;
	m_miniHitN = 0;
	for (int i = 0; i < 3; i++) { m_miniX[i] = m_miniY[i] = m_miniS[i] = 0; }
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
	m_demo = 0;
	m_demoT = 0;
	m_idleT = 0;
	m_demoAct = 0;
	m_demoTick = 0;
	m_demoStep = 0;
	m_demoStage = 1;
	m_demoPh = -1;
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
	m_qApply = TacQualityWanted(m_save);
	if (m_save.quality == TAC_Q_AUTO) { m_save.spare[2] = 6; m_qApply = 6; m_syncRun = TacHzDefaultI(); }
	m_qCoolT = 0.8f;
	if (!m_r.Init(view, m_qApply)) { m_screen = SC_FAIL; return FALSE; }
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

void Game::DrawLoadHud(int permille)
{
	int pct = (permille >= 0 ? permille : m_loadPm) / 10;
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	float titlePx = FitTextPx(TT(TX_TITLE), (float)m_w * .86f, U(40), 1);
	m_r.HudText(TT(TX_TITLE), m_w * .5f, m_h * .38f, titlePx, RGBA8(255, 230, 160), 1, 1);
	wchar_t line[80];
	swprintf_s(line, L"%s  %d%%/100%%", TT(TX_MISC_LOADING), pct);
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
	if (permille < 1000 && m_loadShown >= 0 && now - m_loadPaint < 80) return;
	if (!PaintLoadScreen()) return;
	m_loadShown = pct;
	m_loadPaint = now;
}

int Game::MeasureLoadUnits(int& showUnits)
{
	int nBody = 0;
	char seen[MAX_PARTY][16];
	int nShow = 0;
	for (int i = 0; i < MAX_PARTY; i++) {
		int ri = m_save.party[i];
		if (ri < 0 || ri >= MAX_ROSTER || !m_save.roster[ri].used) continue;
		nShow++;
		const CharData& c = m_save.roster[ri];
		char nm[16];
		sprintf_s(nm, c.gender == 1 ? "HumF%d" : "HumM%d", CharModel(c));
		int have = 0;
		for (int k = 0; k < nBody; k++) if (!strcmp(seen[k], nm)) have = 1;
		if (!have && nBody < MAX_PARTY) { sprintf_s(seen[nBody], "%s", nm); nBody++; }
	}
	showUnits = 16 + nShow * 4;
	int avatarUnits = 0;
	{
		int keyM[MAX_ROSTER], keyA[MAX_ROSTER], keyC[MAX_ROSTER], nk = 0;
		for (int ri = 0; ri < MAX_ROSTER; ri++) {
			if (!m_save.roster[ri].used) continue;
			const CharData& c = m_save.roster[ri];
			int av = CharAvatar(c);
			if (av <= 0) continue;
			int md = (c.gender == 1 ? 100 : 0) + CharModel(c);
			int cup = CharBust(c);
			int seen = 0;
			for (int k = 0; k < nk; k++) if (keyM[k] == md && keyA[k] == av && keyC[k] == cup) seen = 1;
			if (seen) continue;
			if (nk < MAX_ROSTER) { keyM[nk] = md; keyA[nk] = av; keyC[nk] = cup; nk++; }
			avatarUnits += 64;
		}
	}
	m_avaLoadUnits = avatarUnits;
	int n = m_r.CountAssetPulses() + 12 + nBody * 48 + m_r.CountGearMeshes() + 8 + 24 + 4 + showUnits + avatarUnits;
	return n > 0 ? n : 1;
}

void Game::OpenLoadSlice(int units)
{
	if (units < 1) units = 1;
	if (m_loadUnits < 1) m_loadUnits = 1;
	if (m_loadHave < 0) m_loadHave = 0;
	if (m_loadHave > m_loadUnits) m_loadHave = m_loadUnits;
	m_sliceBase = m_loadHave;
	m_sliceUnits = units;
	int a = (int)((long long)m_sliceBase * 1000 / m_loadUnits);
	int b = (int)((long long)(m_sliceBase + units) * 1000 / m_loadUnits);
	if (b > 1000) b = 1000;
	if (b < a) b = a;
	BeginLoadSpan(a, b);
}

void Game::CloseLoadSlice()
{
	m_loadHave = m_sliceBase + m_sliceUnits;
	if (m_loadHave > m_loadUnits) m_loadHave = m_loadUnits;
	if (m_loadUnits < 1) m_loadUnits = 1;
	NoteLoad((int)((long long)m_loadHave * 1000 / m_loadUnits));
	m_sliceUnits = 0;
}

BOOL Game::LoadEverything()
{
	m_loadLive = TRUE;
	m_loadPm = 0;
	m_loadShown = -1;
	m_loadPaint = 0;
	m_loadHave = 0;
	m_sliceBase = m_sliceUnits = 0;
	NoteLoad(0);
	int showUnits = 0;
	m_loadUnits = MeasureLoadUnits(showUnits);
	int assetN = m_r.CountAssetPulses();
	int gearN = m_r.CountGearMeshes();
	int bodyUnits = m_loadUnits - (assetN + 12 + gearN + 8 + 24 + 4 + showUnits + m_avaLoadUnits);
	if (bodyUnits < 0) bodyUnits = 0;
	OpenLoadSlice(assetN);
	if (!m_r.LoadAssets()) { m_r.SetLoadPulse(NULL, NULL); m_loadLive = FALSE; return FALSE; }
	CloseLoadSlice();
	OpenLoadSlice(12);
	if (!BuildLibrary(m_r, m_lib)) { m_r.SetLoadPulse(NULL, NULL); m_loadLive = FALSE; return FALSE; }
	CloseLoadSlice();
	{
		char seen[MAX_PARTY][16];
		int nSeen = 0;
		for (int i = 0; i < MAX_PARTY; i++) {
			int ri = m_save.party[i];
			if (ri < 0 || ri >= MAX_ROSTER || !m_save.roster[ri].used) continue;
			const CharData& c = m_save.roster[ri];
			char nm[16];
			sprintf_s(nm, c.gender == 1 ? "HumF%d" : "HumM%d", CharModel(c));
			int have = 0;
			for (int k = 0; k < nSeen; k++) if (!strcmp(seen[k], nm)) have = 1;
			if (have) continue;
			if (nSeen < MAX_PARTY) sprintf_s(seen[nSeen++], "%s", nm);
			OpenLoadSlice(48);
			m_r.EnsureBody(nm);
			CloseLoadSlice();
		}
		if (bodyUnits > nSeen * 48) {
			OpenLoadSlice(bodyUnits - nSeen * 48);
			CloseLoadSlice();
		}
	}
	OpenLoadSlice(gearN);
	m_r.LoadGearPack();
	CloseLoadSlice();
	OpenLoadSlice(8);
	m_r.LoadFacePack();
	CloseLoadSlice();
	OpenLoadSlice(24);
	m_r.LoadAvatarPack();
	CloseLoadSlice();
	OpenLoadSlice(4);
	m_r.LoadHairPack();
	CloseLoadSlice();
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
	m_qApply = TacQualityWanted(m_save);
	if (m_save.quality == TAC_Q_AUTO) { m_save.spare[2] = 6; m_qApply = 6; m_syncRun = TacHzDefaultI(); }
	else m_syncRun = -1;
	m_syncArmN = 0;
	if (!m_r.Init(m_hwnd, m_qApply)) return FALSE;
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
	// デモ中のゴールドやドロップはセーブへ残さない。幕が開いて拠点に戻ったあとだけ書く。
	if (m_demo == 1 || m_demo == 2 || (m_demo == 3 && m_screen == SC_BATTLE)) return;
	SaveWrite(m_save);
}

void Game::ApplyAudio()
{
	TacAudioSetVol(m_save.bgmVol, m_save.seVol);
}

float Game::DrawSoundOpts(float x, float y, float w, float k, int* tip, float* tipY, float* tipX, float* tipW, float* tipH)
{
	if (k < .08f) k = .08f;
	float rh = U(32) * k, step = U(34) * k;
	auto mark = [&](int id, int tx, float rowY) {
		if (!tip) return;
		BOOL on = (m_mouseSeen && !m_navMode && Hit(x, rowY, w, rh)) || (m_navMode && m_focusId == id);
		if (on) {
			*tip = tx;
			if (tipY) *tipY = rowY;
			if (tipX) *tipX = x;
			if (tipW) *tipW = w;
			if (tipH) *tipH = rh;
		}
	};
	wchar_t v[16];
	swprintf_s(v, L"%d", m_save.bgmVol * 10);
	mark(ID_OPT_BGM, TX_BSH_BGM, y);
	if (int d = Sel(ID_OPT_BGM, x, y, w, rh, TT(TX_SET_BGM), v)) {
		m_save.bgmVol = Clampi(m_save.bgmVol + d, 0, 10);
		ApplyAudio();
	}
	y += step;
	swprintf_s(v, L"%d", m_save.seVol * 10);
	mark(ID_OPT_SE, TX_BSH_SE, y);
	if (int d = Sel(ID_OPT_SE, x, y, w, rh, TT(TX_SET_SE), v)) {
		m_save.seVol = Clampi(m_save.seVol + d, 0, 10);
		ApplyAudio();
		TacSfx(TS_OK_MENU);
	}
	y += step;
	static const int camTx[3] = { TX_SET_SLOW, TX_SET_Q_STD, TX_SET_FAST };
	int c = Clampi(m_save.camSpeed, 0, 2);
	mark(ID_OPT_CAM, TX_BSH_CAM, y);
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

const wchar_t* Game::ClassName(int cls) const
{
	if (const wchar_t* t = ClassTitle(cls)) return t;
	return TT(ClassInfo(cls).nameTx);
}
float Game::FitTextPx(const wchar_t* s, float maxW, float px, int bold)
{
	if (!s || !s[0] || px <= 0.f || maxW <= 1.f) return px;
	enum { FIT_N = 512 };
	struct FitSlot { UINT h; int mw, pxq, b; float res; };
	static FitSlot s_fit[FIT_N];
	const int mwI = (int)(maxW * 4.f + 0.5f);
	const int pxI = (int)(px * 4.f + 0.5f);
	const int bI = bold ? 1 : 0;
	UINT hv = 2166136261u ^ (UINT)mwI ^ ((UINT)pxI << 12) ^ ((UINT)bI << 24);
	for (const wchar_t* p = s; *p; p++) { hv ^= (UINT)*p; hv *= 16777619u; }
	if (!hv) hv = 1;
	FitSlot& sl = s_fit[hv & (FIT_N - 1)];
	if (sl.h == hv && sl.mw == mwI && sl.pxq == pxI && sl.b == bI) return sl.res;
	float w = m_r.TextWidth(s, px, bold);
	if (w <= maxW) {
		sl.h = hv; sl.mw = mwI; sl.pxq = pxI; sl.b = bI; sl.res = px;
		return px;
	}
	float n = px * maxW / w;
	if (n > px) n = px;
	float floorPx = min(px, U(6.f));
	if (n < floorPx) n = floorPx;
	float w2 = m_r.TextWidth(s, n, bold);
	if (w2 > maxW && w2 > 1.f) {
		float n2 = n * maxW / w2;
		if (n2 < floorPx) n2 = floorPx;
		if (n2 < n) n = n2;
	}
	sl.h = hv; sl.mw = mwI; sl.pxq = pxI; sl.b = bI; sl.res = n;
	return n;
}

void Game::UnitTitle(const Unit& u, wchar_t* out, int n) const
{
	if (u.team == TEAM_PLAYER) swprintf_s(out, n, L"%s", u.ch.name);
	else {
		const MonsterDef& md = MonsterInfo(u.monster);
		const wchar_t* mark = u.boss ? L"★" : (UnitIsLucky(u) ? L"☆" : L"");
		if (md.look == LK_MESH) swprintf_s(out, n, L"%s%s", mark, MonsterLabel(u.monster));
		else if (md.nameB >= 0) swprintf_s(out, n, L"%s%s %s", mark, TT(md.nameTx), TT(md.nameB));
		else swprintf_s(out, n, L"%s%s", mark, TT(md.nameTx));
	}
}

// ============================================================ メインループ
int Game::QualityNow() const
{
	if (m_qApply >= 0) return m_qApply;
	return TacQualityWanted(m_save);
}

int Game::SyncNow() const
{
	if (m_save.quality == TAC_Q_AUTO) {
		int v = (m_syncRun >= 0) ? m_syncRun : TacHzDefaultI();
		return Clampi(v, 0, TAC_HZ_N - 1);
	}
	return Clampi(TacVsync(m_save), 0, TAC_HZ_N - 1);
}

int Game::PaceHz() const
{
	return TacHzAt(SyncNow());
}

void Game::ApplyQualityNow(int q)
{
	q = Clampi(q, 0, 6);
	if (q == m_qApply) {
		m_r.SetQuality(q);
		return;
	}
	m_qApply = q;
	BOOL rebuilt = m_r.SetQuality(q);
	m_qCoolT = rebuilt ? 0.45f : 0.20f;
	m_qMissN = 0;
	m_qOkN = 0;
}

void Game::PickQuality(int delta)
{
	int q = TacQualityAt(TacQualitySlot(m_save.quality) + delta);
	m_qHold = -1;
	m_qMissN = 0;
	m_qOkN = 0;
	if (q == TAC_Q_AUTO) {
		m_save.quality = TAC_Q_AUTO;
		m_save.spare[0] = 1;
		m_save.spare[1] = 0;
		m_save.spare[2] = 6;
		TacQualityEnsureAutoDefault(m_save);
		m_save.spare[2] = 6;
		m_syncRun = TacHzDefaultI();
		ApplyQualityNow(6);
	} else {
		m_save.quality = q;
		TacQualityUserLock(m_save);
		m_syncRun = -1;
		ApplyQualityNow(q);
	}
	InvalidatePortraits();
}

void Game::PulseQuality(float work, float span)
{
	(void)work;
	if (m_screen == SC_LOADING || !m_loaded || !m_r.Ready()) return;
	int runI = SyncNow();
	int hz = TacHzAt(runI);
	if (hz < 30) hz = 30;
	if (m_qHz != hz) {
		m_qHz = hz;
		m_qMissN = 0;
		m_qOkN = 0;
		m_syncArmN = 0;
	}
	if (m_qCoolT > 0) return;
	if (m_fpsN < 20 || span < 0.003f || span >= 0.25f) return;
	if (m_syncLock > 0) return;
	BOOL autoOn = (m_save.quality == TAC_Q_AUTO);
	const int stdSlot = TacQualityFixedSlot(1);
	const int capSlot = 6;
	if (autoOn) {
		int capI = TAC_HZ_N - 1;
		int now = QualityNow();
		int slot = TacQualityFixedSlot(now);
		int next = now;
		auto goHz = [&](int sync) {
			m_syncRun = Clampi(sync, 0, capI);
			m_qMissN = 0;
			m_qOkN = 0;
			m_syncArmN = 0;
			m_syncLock = 0.45f;
			m_fpsN = 0;
			m_fpsI = 0;
			m_qHz = TacHzAt(m_syncRun);
		};
		float holdHz = (hz >= 120) ? (float)hz * 0.88f : (float)hz * 0.95f;
		float dropHz = (hz >= 120) ? (float)hz * 0.72f : (float)hz * 0.80f;
		if (m_fps >= holdHz) { m_qOkN++; m_qMissN = 0; }
		else if (m_fps < dropHz) { m_qMissN++; m_qOkN = 0; }
		else { m_qMissN = 0; m_qOkN = 0; }
		int floorSlot = (hz <= 30) ? 0 : stdSlot;
		BOOL peakQ = (now == 5 || now == 6);
		if (peakQ && runI < capI) {
			int upI = runI + 1;
			float bU = 1.f / (float)TacHzAt(upI);
			BOOL canU = m_fps >= (float)hz * 0.95f
				&& m_workAvg > 0.f && m_workAvg <= bU * 0.98f;
			if (canU) m_syncArmN++;
			else m_syncArmN = 0;
			if (m_syncArmN >= 20) {
				goHz(upI);
				return;
			}
		} else m_syncArmN = 0;
		if (m_qMissN >= 20) {
			if (now == 3 && runI > 0) {
				goHz(runI - 1);
				return;
			}
			if (slot > floorSlot) {
				next = TacQualityFixedAt(slot - 1);
				m_qMissN = 0;
			} else m_qMissN = 0;
		} else if (m_syncArmN == 0 && m_qOkN >= 40 && slot < capSlot) {
			next = TacQualityFixedAt(slot + 1);
			m_qOkN = 0;
		}
		if (next != TacQualityWanted(m_save))
			m_save.spare[2] = next;
		if (next != now) ApplyQualityNow(next);
		return;
	}
	m_syncRun = -1;
	float period = 1.f / (float)hz;
	BOOL shownOk = (m_fps >= (float)hz * 0.98f) || (span <= period * 1.08f);
	if (shownOk) { m_qOkN++; m_qMissN = 0; }
	else { m_qMissN++; m_qOkN = 0; }
	int now = QualityNow();
	int slot = TacQualityFixedSlot(now);
	int cap = (m_qHold >= 0) ? m_qHold : TacQualityWanted(m_save);
	if (cap < 0 || cap > 6) cap = now;
	int qCapSlot = TacQualityFixedSlot(cap);
	int qFloor = qCapSlot - 2;
	if (qFloor < 0) qFloor = 0;
	int next = now;
	if (m_qMissN >= 20) {
		if (slot > qFloor) next = TacQualityFixedAt(slot - 1);
		m_qMissN = 0;
		if (m_qHold < 0) m_qHold = TacQualityWanted(m_save);
	} else if (m_qOkN >= 20) {
		if (slot < qCapSlot) next = TacQualityFixedAt(slot + 1);
		m_qOkN = 0;
		if (m_qHold >= 0 && next == m_qHold) m_qHold = -1;
	}
	if (next != now) ApplyQualityNow(next);
}

void Game::Tick(float dt)
{
	m_textPad = 0;
	SanitizeTextFocus();
	if (m_slotBusy) return;
	if (m_slotJob) {
		int job = m_slotJob;
		m_slotJob = 0;
		m_slotBusy = 1;
		RunSlotFile(job);
		m_slotBusy = 0;
	}
	dt = Clampf(dt, 0, .1f);
	LARGE_INTEGER busy0;
	QueryPerformanceCounter(&busy0);
	m_time += dt;
	if (!m_demo) {
		// 拠点でカーソルもボタンも止まっていれば、10秒でデモへ。戦闘の途中からは始めない。
		BOOL idleHome = m_screen == SC_BASE && !m_shop && !m_codex && !m_slotUi && !m_showHelp
			&& !m_mapLook && !m_sortieAsk && !m_sellAsk && !m_textFocus && !m_baseOpt && !m_confirmDelete
			&& m_jobRedo != 1 && m_jobRedo != 3;
		if (idleHome) {
			m_idleT += dt;
			if (m_idleT >= 10.f) DemoBegin();
		} else m_idleT = 0;
	}
	if (m_demo) TickDemo(dt);
	if (m_qCoolT > 0) m_qCoolT -= dt;
	if (m_syncLock > 0) m_syncLock -= dt;
	BOOL holdGame = FALSE;
	if (m_screen == SC_LOADING) {
		if (m_loadFrames++ >= 2) {
			if (!LoadEverything()) { m_screen = SC_FAIL; m_loadLive = FALSE; }
			else {
				m_loadLive = TRUE;
				int ava = m_avaLoadUnits;
				if (ava < 0) ava = 0;
				int left = m_loadUnits - m_loadHave - ava;
				if (left < 1) left = 1;
				OpenLoadSlice(left);
				EnterBase();
				CloseLoadSlice();
				m_screen = SC_LOADING;
				WarmHomeAvatars();
				NoteLoad(1000);
				m_screen = SC_BASE;
				m_r.SetLoadPulse(NULL, NULL);
				m_loadLive = FALSE;
				holdGame = TRUE;
			}
		}
	}
	if (m_r.Ready() && m_r.DeviceLost()) {
		if (!Recover()) { m_screen = SC_FAIL; return; }
	}
	float gdt = dt * ((m_fastFwd && !m_demo) ? 3.f : 1.f);
	switch (m_screen) {
	case SC_BASE: case SC_PARTY: case SC_EVOLVE: case SC_CREATE: UpdateBase(dt); break;
	case SC_BATTLE: UpdateBattle(m_showHelp ? 0 : gdt); break;
	case SC_RESULT: UpdateBase(dt); break;
	default: break;
	}
	UpdateParticles(m_screen == SC_BATTLE ? gdt : dt);
	if (m_evFlash > 0) m_evFlash = max(0.f, m_evFlash - dt);
	if (m_evoCut) {
		m_evoT += dt;
		if (m_evoT >= 2.75f) m_evoCut = 0;
	}
	SyncBgm();
	if (m_wantClose) return;
	if (!m_r.Ready()) return;
	if (!m_r.FrameReady()) return;
	if (holdGame) return;
	LARGE_INTEGER t0, t1, freq;
	QueryPerformanceCounter(&t0);
	QueryPerformanceFrequency(&freq);
	m_msUpd = freq.QuadPart ? (float)((double)(t0.QuadPart - busy0.QuadPart) * 1000.0 / (double)freq.QuadPart) : 0.f;
	RenderFrame();
	LARGE_INTEGER tPr;
	QueryPerformanceCounter(&tPr);
	if (!m_r.ConsumeAvaFlash()) m_r.Present();
	if (m_goHome) {
		m_goHome = FALSE;
		if (m_demo) DemoLeave();
		else GoHomeFromBattle();
	}
	QueryPerformanceCounter(&t1);
	QueryPerformanceFrequency(&freq);
	m_msPr = freq.QuadPart ? (float)((double)(t1.QuadPart - tPr.QuadPart) * 1000.0 / (double)freq.QuadPart) : 0.f;
	m_r.EndFrameTextGc();
	float work = 0.f;
	float span = 0.f;
	if (freq.QuadPart)
		work = (float)((double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart);
	if (m_fpsAt.QuadPart && freq.QuadPart) {
		span = (float)((double)(t1.QuadPart - m_fpsAt.QuadPart) / (double)freq.QuadPart);
		float busy = (float)((double)(t1.QuadPart - busy0.QuadPart) / (double)freq.QuadPart);
		if (span >= 0.003f && span < 0.25f && busy >= 0.f && busy < 0.25f) {
			m_fpsSpan[m_fpsI] = span;
			m_workSpan[m_fpsI] = busy;
			m_fpsI = (m_fpsI + 1) % 20;
			if (m_fpsN < 20) m_fpsN++;
			float sum = 0.f, wsum = 0.f;
			for (int i = 0; i < m_fpsN; i++) { sum += m_fpsSpan[i]; wsum += m_workSpan[i]; }
			span = sum / (float)m_fpsN;
			m_workAvg = wsum / (float)m_fpsN;
			m_fps = 1.f / span;
		}
	}
	m_fpsAt = t1;
	PulseQuality(work, span);
}

	static_assert(TAC_BGM_LANTERN - TAC_BGM_PLAINS + 1 == TH_COUNT, "地形BGM");
// 拠点=tactics.ogg。戦闘は地形ごとの tacticsgame*.ogg、無ければ tacticsgame.ogg。
// 勝利目前は二つのときだけ。距離も匹数も見ない。毎フレーム組み直す。
// 掃討は索敵の外も全員。最大HPも残りHPも数える。倒した敵の最大HPは分母に残す。
// ボス戦は、索敵済みに加え、今いる地点からボスまでの途上と、ボスから8マス以内の未起動も入れる。
// 生存ボスのHPが最大の半分を超えていれば通常曲。半分以下で勝利目前。雑魚の有無は見ない。
// ボスがいないときだけ、対象の残りHPが最大HP合計の1/4以下で勝利目前。
// 敗北寸前曲は毎フレーム見る。味方の残りHPが最大の33%未満、または死者が人数の半数以上。
// その条件を外れたら、勝利目前ならそちら、まだなら地形曲。危険曲はラッチしない。
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
		int bossAlive = 0, bossLow = 0;
		int bossX = -1, bossZ = -1;
		for (int i = 0; i < m_b.nUnits; i++) {
			const Unit& u = m_b.u[i];
			if (u.team == TEAM_ENEMY && u.boss && u.alive && u.hp > 0) { bossX = u.x; bossZ = u.z; break; }
		}
		int hereX = 0, hereZ = 0, hereD = 9999;
		if (bossX >= 0) {
			for (int i = 0; i < m_b.nUnits; i++) {
				const Unit& u = m_b.u[i];
				if (u.team != TEAM_PLAYER || !u.alive || u.hp <= 0) continue;
				int d = Dist(u.x, u.z, bossX, bossZ);
				if (d < hereD) { hereD = d; hereX = u.x; hereZ = u.z; }
			}
		}
		for (int i = 0; i < m_b.nUnits; i++) {
			const Unit& u = m_b.u[i];
			if (u.team == TEAM_ENEMY) {
				all++;
				int em = max(1, u.ch.stat[S_HP]);
				if (u.boss && u.alive && u.hp > 0) {
					bossAlive = 1;
					if ((long long)u.hp * 2 <= (long long)em) bossLow = 1;
				}
				int inPool = (m_b.sweep == TAC_VAR_SWEEP) || u.alert != 0;
				if (!inPool && bossX >= 0) {
					int dBoss = Dist(u.x, u.z, bossX, bossZ);
					// ボスの周囲と、今いる地点からボスまでの途上は起き上がる想定。
					if (dBoss <= 8) inPool = 1;
					else if (hereD < 9999 && Dist(hereX, hereZ, u.x, u.z) + dBoss <= hereD + 8) inPool = 1;
				}
				if (!inPool) continue;
				eMx += em;
				if (u.alive && u.hp > 0) eHp += u.hp;
			}
			if (u.team != TEAM_PLAYER) continue;
			partyN++;
			mx += max(1, u.ch.stat[S_HP]);
			if (u.alive && u.hp > 0) hp += u.hp;
			else deadN++;
		}
		const int fightWon = (m_victory == 1) || (m_screen == SC_RESULT) || (m_bossDown && players > 0) || (alive == 0 && all > 0 && players > 0);
		int nearWin = 0;
		if (m_screen == SC_BATTLE && !fightWon) {
			if (bossAlive) nearWin = bossLow;
			else if (m_b.bgmWin) nearWin = 1;
			else nearWin = (eMx > 0 && eHp * 4 <= eMx) ? 1 : 0;
		}
		m_b.bgmWin = nearWin;
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
	static int lastScene = -999;
	if (scene != lastScene) {
		TacBgmScene(scene);
		lastScene = scene;
	}
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
	m_r.SetSky(m_screen == SC_BATTLE ? TacWx(m_b.weather) : WX_CLEAR, m_screen == SC_BATTLE ? TacHour(m_b.weather) : HR_NOON);
	m_r.SetAvaLoadFull(m_screen != SC_CREATE && m_screen != SC_BATTLE);
	LARGE_INTEGER rf0, rf1, rfq;
	m_msSh = m_msRef = 0;
	QueryPerformanceCounter(&rf0);
	QueryPerformanceFrequency(&rfq);
	m_r.BeginFrame();
	if (m_screen == SC_BATTLE) {
		AssignBattlePortraits();
		BindImportedActors();
	} else if (m_loaded) PrepareMenuPortraits();
	switch (m_screen) {
	case SC_BASE: case SC_PARTY: case SC_EVOLVE: case SC_RESULT:
		DrawShowcase3D();
		break;
	case SC_CREATE:
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
	QueryPerformanceCounter(&rf1);
	m_msMain = rfq.QuadPart ? (float)((double)(rf1.QuadPart - rf0.QuadPart) * 1000.0 / (double)rfq.QuadPart) : 0.f;
	m_msMain -= m_msSh + m_msRef;
	if (m_msMain < 0.f) m_msMain = 0.f;
	QueryPerformanceCounter(&rf0);
	if (m_loaded) RenderPortraits();
	QueryPerformanceCounter(&rf1);
	m_msPort = rfq.QuadPart ? (float)((double)(rf1.QuadPart - rf0.QuadPart) * 1000.0 / (double)rfq.QuadPart) : 0.f;
	QueryPerformanceCounter(&rf0);
	const BOOL avaWait = m_r.AvatarLoading() && m_screen != SC_BATTLE && m_screen != SC_LOADING && m_screen != SC_FAIL;
	const BOOL avaFull = avaWait && m_screen != SC_CREATE;
	if (avaFull) {
		SetupCamera(v3(0, 0, 0), 0, .3f, 10, .8f);
		m_r.BeginMainPass();
		m_r.DrawSky(RGBA8(30, 34, 70), RGBA8(90, 70, 110), 0, 0);
	}
	m_r.BeginHud();
	if (m_screen == SC_CREATE) m_r.GrabAvaPlate();
	m_btns.clear();
	if (m_uiHoverScreen != m_screen || m_uiHoverShop != (m_shop != 0)) {
		m_uiHover = -1;
		m_uiHoverScreen = m_screen;
		m_uiHoverShop = m_shop != 0;
	}
	m_uiHoverHit = 0;
	m_statTip = -1;
	m_skillTip = -1;
	if (avaFull) {
		DrawLoadHud(m_r.AvatarLoadPct() * 10);
	} else switch (m_screen) {
	case SC_LOADING:
		DrawLoadHud();
		break;
	case SC_FAIL: {
		const wchar_t* msg = m_r.Ready() ? TT(TX_MISC_ASSETFAIL) : TT(TX_MISC_DXFAIL);
		m_r.HudText(TT(TX_TITLE), m_w * .5f, m_h * .4f, FitTextPx(TT(TX_TITLE), (float)m_w * .86f, U(34), 1), RGBA8(255, 230, 160), 1, 1);
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
	if (avaWait && !avaFull) {
		int pct = m_r.AvatarLoadPct();
		if (pct < 0) pct = 0;
		if (pct > 100) pct = 100;
		wchar_t wait[80];
		swprintf_s(wait, L"%s  %d%%/100%%", TT(TX_MISC_LOADING), pct);
		m_r.HudText(wait, m_w * .5f, m_h * .50f, U(20), RGBA8(255, 255, 255), 1, 1);
		float bw = min(U(520.f), (float)m_w * .62f);
		float bx = ((float)m_w - bw) * .5f;
		Bar(bx, m_h * .56f, bw, U(22), pct / 100.f, RGBA8(255, 214, 120), RGBA8(40, 28, 18));
	}
	BOOL homeBare = !avaWait && m_screen == SC_BASE && !m_homeUi && !m_mapLook && !m_shop && !m_codex && !m_slotUi
		&& m_jobRedo != 1 && m_jobRedo != 3 && !m_showHelp;
	if (!avaWait && !homeBare) {
		DrawPopups(0);
		DrawBanners(0);
	}
	if (!avaWait && m_showHelp) DrawHelpOverlay();
	else if (!avaWait && m_statTip >= 0) DrawStatTip();
	else if (!avaWait && m_skillTip >= 0) DrawSkillTip();
	if (!avaWait) DrawPerfHud();
	if (!avaWait && m_evFlash > 0) m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(255, 255, 255, (int)(m_evFlash * 200)));
	if (m_demo) m_click = FALSE;
	else if (m_screen == SC_BATTLE && m_click && !m_showHelp && !m_slotUi) {
		m_click = FALSE;
		BattleClick(m_clickX, m_clickY);
	}
	if (m_demo) DrawDemoCurtain();
	m_r.EndHud();
	QueryPerformanceCounter(&rf1);
	m_msHud = rfq.QuadPart ? (float)((double)(rf1.QuadPart - rf0.QuadPart) * 1000.0 / (double)rfq.QuadPart) : 0.f;
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
	// デモは盤の枠だけ動かす。OS のカーソルも、ここでの視点ドラッグも動かさない。
	if (m_demo) { m_mx = x; m_my = y; return; }
	if (x != m_mx || y != m_my) m_idleT = 0;
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
	BOOL lookPan = m_lDown && !m_downUi && m_screen == SC_BASE && m_mapLook;
	if (m_rDown || m_mDown || scout || lookPan) {
		int dx = x - m_dragLastX, dy = y - m_dragLastY;
		m_dragDist += fabsf((float)dx) + fabsf((float)dy);
		if (m_rDown && m_dragDist > 6) {
			m_dragRot = TRUE;
			m_camYawGoal += dx * .008f;
			m_camYaw = m_camYawGoal;
			float pLo = (m_screen == SC_BASE && m_mapLook) ? .18f : .45f;
			float pHi = (m_screen == SC_BASE && m_mapLook) ? 1.45f : 1.35f;
			m_camPitch = Clampf(m_camPitch + dy * .004f, pLo, pHi);
		}
		if ((m_mDown && m_screen != SC_CREATE && m_dragDist > 6) || ((scout || lookPan) && m_dragDist > 6)) {
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

static void CreateInfoRect(float ui, int sw, float& x, float& y, float& w, float& h)
{
	w = 268.f * ui;
	h = 188.f * ui;
	x = (float)sw - w - 14.f * ui;
	y = 14.f * ui;
}


void Game::OnMouseDown(int btn, int x, int y)
{
	if (m_demo) { m_mx = x; m_my = y; DemoLeave(); return; }
	m_idleT = 0;
	m_mx = x; m_my = y; m_mouseSeen = TRUE; m_navMode = FALSE;
	if (btn == 0) {
		m_lDown = TRUE; m_downX = x; m_downY = y;
		m_dragLastX = x; m_dragLastY = y; m_dragDist = 0; m_dragPan = FALSE;
		m_downUi = FALSE;
		for (auto& b : m_btnsPrev) if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) m_downUi = TRUE;
		float ix, iy, iw, ih;
		CreateInfoRect(m_ui, m_w, ix, iy, iw, ih);
		BOOL onInfo = m_screen == SC_CREATE && x >= ix && x < ix + iw && y >= iy && y < iy + ih;
		m_previewDrag = (m_screen == SC_CREATE && !onInfo && x > m_w * .46f && x < m_w * .78f);
		m_monDrag = (m_screen == SC_BASE && m_codex == 4 && !m_downUi && x > m_w * .48f);
	}
	if (btn == 1) { m_rDown = TRUE; m_dragRot = FALSE; m_dragDist = 0; m_dragLastX = x; m_dragLastY = y; }
	if (btn == 2) {
		m_mDown = TRUE; m_dragPan = FALSE; m_dragDist = 0; m_dragLastX = x; m_dragLastY = y;
		float ix, iy, iw, ih;
		CreateInfoRect(m_ui, m_w, ix, iy, iw, ih);
		BOOL onInfo = m_screen == SC_CREATE && x >= ix && x < ix + iw && y >= iy && y < iy + ih;
		m_previewPanDrag = (m_screen == SC_CREATE && !onInfo && x >= m_w * .40f);
	}
}

void Game::OnMouseUp(int btn, int x, int y)
{
	if (m_demo) {
		m_lDown = m_rDown = m_mDown = FALSE;
		m_click = FALSE;
		m_mx = x; m_my = y;
		return;
	}
	m_mx = x; m_my = y;
	if (btn == 0 && m_lDown) {
		m_lDown = FALSE;
		m_previewDrag = FALSE;
		m_monDrag = FALSE;
		if (abs(x - m_downX) < 12 && abs(y - m_downY) < 12) {
			m_click = TRUE; m_clickX = x; m_clickY = y;
			if (m_textFocus == 1
				&& m_textBox[2] > 1.f && m_textBox[3] > 1.f
				&& !Hit(m_textBox[0], m_textBox[1], m_textBox[2], m_textBox[3])) {
				const BOOL began = m_downX >= m_textBox[0] && m_downY >= m_textBox[1]
					&& m_downX < m_textBox[0] + m_textBox[2] && m_downY < m_textBox[1] + m_textBox[3];
				if (!began) {
					m_textFocus = 0;
					m_imeComp[0] = 0;
				}
			}
		}
	}
	if (btn == 1 && m_rDown) {
		m_rDown = FALSE;
		if (!m_dragRot) {
			BOOL plainHome = m_screen == SC_BASE && !m_mapLook && !m_sortieAsk && !m_codex && !m_shop && !m_slotUi
				&& m_jobRedo != 1 && m_jobRedo != 3 && !m_baseOpt && !m_showHelp;
			if (plainHome) { m_homeUi = !m_homeUi; TacSfx(m_homeUi ? TS_OPEN : TS_CANCEL); }
			else { m_rclick = TRUE; NavBack(); }
		}
		m_dragRot = FALSE;
	}
	if (btn == 2) {
		BOOL tap = m_mDown && m_dragDist <= 6.f;
		m_mDown = FALSE; m_dragPan = FALSE; m_previewPanDrag = FALSE;
		BOOL battleMap = m_screen == SC_BATTLE && !m_showHelp
			&& (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE);
		if (tap && battleMap) {
			BOOL overUi = FALSE;
			for (auto& b : m_btnsPrev) if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h) overUi = TRUE;
			if (!overUi) CycleLayer(1);
		}
	}
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
	if (m_demo) { DemoLeave(); return; }
	m_idleT = 0;
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
		if (m_screen == SC_BASE && m_codex == 5) {
			m_growScroll -= (float)delta * 0.45f;
			return;
		}
		int sign = delta > 0 ? 1 : -1;
		// ホイール直後に逆符号が届くと、今の倍率が打ち消されて元へ戻る。
		if (m_wheelSign && sign != m_wheelSign && (m_time - m_wheelTime) < .35f)
			return;
		m_wheelSign = sign;
		m_wheelTime = m_time;
		ZoomCam(powf(.88f, (float)delta / 120.f));
		return;
	}
	if (m_screen == SC_PARTY && !m_eqPick && m_partySkPages > 1 && delta != 0
		&& Hit(m_partySkX, m_partySkY, m_partySkW, m_partySkH)) {
		int pages = m_partySkPages;
		m_partySkPage = delta > 0 ? (m_partySkPage + pages - 1) % pages : (m_partySkPage + 1) % pages;
		TacSfx(TS_CURSOR);
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

static int TacWLen(const wchar_t* s) { return s ? (int)wcslen(s) : 0; }

static void TacCaretStr(wchar_t* dst, int dstN, const wchar_t* s, int caret, const wchar_t* comp, BOOL blink)
{
	if (!dst || dstN < 2) return;
	if (!s) s = L"";
	if (!comp) comp = L"";
	int n = TacWLen(s);
	if (caret < 0 || caret > n) caret = n;
	int o = 0;
	auto put = [&](wchar_t ch) { if (o + 1 < dstN) dst[o++] = ch; };
	for (int i = 0; i < caret; i++) put(s[i]);
	for (int i = 0; comp[i]; i++) put(comp[i]);
	if (blink) put(L'|');
	for (int i = caret; s[i]; i++) put(s[i]);
	dst[o] = 0;
}

static void TacInsertAt(wchar_t* s, int maxLen, int& caret, wchar_t ch)
{
	if (!s || maxLen < 2 || ch < 32) return;
	int n = TacWLen(s);
	if (n + 1 >= maxLen) return;
	if (caret < 0 || caret > n) caret = n;
	memmove(s + caret + 1, s + caret, (size_t)(n - caret + 1) * sizeof(wchar_t));
	s[caret++] = ch;
}

static void TacDeleteAt(wchar_t* s, int& caret, BOOL back)
{
	if (!s) return;
	int n = TacWLen(s);
	if (caret < 0 || caret > n) caret = n;
	if (back) {
		if (caret <= 0) return;
		memmove(s + caret - 1, s + caret, (size_t)(n - caret + 1) * sizeof(wchar_t));
		caret--;
	} else {
		if (caret >= n) return;
		memmove(s + caret, s + caret + 1, (size_t)(n - caret) * sizeof(wchar_t));
	}
}

static void TacMarkBox(float* box, float x, float y, float w, float h)
{
	if (!box) return;
	box[0] = x; box[1] = y; box[2] = w; box[3] = h;
}

void Game::ClearTextFocus()
{
	m_textFocus = 0;
	m_imeComp[0] = 0;
}

void Game::SanitizeTextFocus()
{
	int f = m_textFocus;
	if (f == 1 && m_screen != SC_CREATE) f = 0;
	else if (f == 2 && !m_slotUi) f = 0;
	else if (f == 3 && !m_sellAsk) f = 0;
	else if (f == 4 && (m_screen != SC_BASE || m_codex != 4)) f = 0;
	if (f != m_textFocus) {
		m_textFocus = 0;
		m_imeComp[0] = 0;
	}
}

BOOL Game::TextLive(wchar_t*& buf, int& maxLen)
{
	buf = NULL;
	maxLen = 0;
	if (m_textFocus == 1 && m_screen == SC_CREATE) { buf = m_edit.name; maxLen = NAME_LEN; return TRUE; }
	if (m_textFocus == 2 && m_slotUi) { buf = m_slotEdit; maxLen = TAC_SLOT_NAME; return TRUE; }
	if (m_textFocus == 4 && m_screen == SC_BASE && m_codex == 4) { buf = m_monFind; maxLen = 24; return TRUE; }
	return FALSE;
}

void Game::SetTextCaret(int caret)
{
	wchar_t* buf = NULL;
	int maxLen = 0;
	int n = 0;
	if (TextLive(buf, maxLen) && buf) n = TacWLen(buf);
	if (caret < 0) caret = 0;
	if (caret > n) caret = n;
	m_textCaret = caret;
}

void Game::SetImeComp(const wchar_t* s)
{
	if (!s || !s[0]) { m_imeComp[0] = 0; return; }
	wcsncpy_s(m_imeComp, s, _TRUNCATE);
}

void Game::TextBoxRect(RECT& rc) const
{
	rc.left = (LONG)m_textBox[0];
	rc.top = (LONG)m_textBox[1];
	rc.right = (LONG)(m_textBox[0] + m_textBox[2]);
	rc.bottom = (LONG)(m_textBox[1] + m_textBox[3]);
}

void Game::SetSellFromText(const wchar_t* s)
{
	int v = 0;
	if (s) {
		for (int i = 0; s[i]; i++) {
			if (s[i] < L'0' || s[i] > L'9') continue;
			v = v * 10 + (int)(s[i] - L'0');
			if (v > m_sellMax) { v = m_sellMax; break; }
		}
	}
	if (v < 1) v = m_sellFresh ? 1 : 1;
	if (v < 1) v = 1;
	if (v > m_sellMax) v = m_sellMax;
	m_sellN = v;
	m_sellFresh = 0;
}

void Game::SellToText(wchar_t* s, int n) const
{
	if (!s || n < 2) return;
	swprintf_s(s, n, L"%d", m_sellN < 1 ? 1 : m_sellN);
}

void Game::NotifyFindChanged()
{
	m_codexPage = 0;
}

void Game::ArmFoeFind()
{
	if (m_screen != SC_BASE || m_codex != 4) return;
	if (m_textFocus == 4) return;
	m_textFocus = 4;
	m_textCaret = TacWLen(m_monFind);
	m_imeComp[0] = 0;
	m_textReload = 1;
}

void Game::CommitMonFind(const wchar_t* s)
{
	if (m_screen != SC_BASE || m_codex != 4) return;
	if (!s) s = L"";
	wchar_t prev[24];
	wcsncpy_s(prev, m_monFind, _TRUNCATE);
	wcsncpy_s(m_monFind, s, _TRUNCATE);
	if (wcscmp(prev, m_monFind) != 0) m_codexPage = 0;
}

float Game::TextCaretPx()
{
	const wchar_t* s = L"";
	if (m_textFocus == 1) s = m_edit.name;
	else if (m_textFocus == 2) s = m_slotEdit;
	else if (m_textFocus == 4) s = m_monFind;
	int n = TacWLen(s);
	int caret = m_textCaret;
	if (caret < 0 || caret > n) caret = n;
	wchar_t pre[96];
	int o = 0;
	for (int i = 0; i < caret && o + 1 < 96; i++) pre[o++] = s[i];
	for (int i = 0; m_imeComp[i] && o + 1 < 96; i++) pre[o++] = m_imeComp[i];
	pre[o] = 0;
	float px = m_textPx > 1.f ? m_textPx : 16.f;
	return m_r.TextWidth(pre, px, 1) + m_textPad;
}

void Game::OnChar(wchar_t ch)
{
	if (ch == 127) ch = 8;
	if (m_textFocus == 3 && m_sellAsk) {
		if (ch == 8) { m_sellN /= 10; if (m_sellN < 1) m_sellN = 1; m_sellFresh = 0; return; }
		if (ch >= L'0' && ch <= L'9') {
			int d = (int)(ch - L'0');
			int v = m_sellFresh ? d : m_sellN * 10 + d;
			m_sellFresh = 0;
			if (v < 1) v = 1;
			if (v > m_sellMax) v = m_sellMax;
			m_sellN = v;
		}
		return;
	}
	if (ch == 13 || ch == 27 || ch == 9) {
		if (m_textFocus == 2 && ch == 13) CommitSlotName();
		else if (!(m_textFocus == 4 && ch == 13)) {
			m_textFocus = 0;
			m_imeComp[0] = 0;
		}
		return;
	}
	wchar_t* buf = NULL;
	int maxLen = 0;
	if (!TextLive(buf, maxLen) || !buf) return;
	if (ch == 8) { TacDeleteAt(buf, m_textCaret, TRUE); if (m_textFocus == 4) m_codexPage = 0; return; }
	if (ch < 32) return;
	TacInsertAt(buf, maxLen, m_textCaret, ch);
	if (m_textFocus == 4) m_codexPage = 0;
}

void Game::OnBecameActive()
{
	m_qMissN = 0;
	m_qOkN = 0;
	if (m_qCoolT < 0.6f) m_qCoolT = 0.6f;
	m_fpsAt.QuadPart = 0;
	m_fpsN = 0;
	m_fpsI = 0;
}

void Game::ReleaseInput()
{
	m_lDown = FALSE;
	m_rDown = FALSE;
	m_mDown = FALSE;
	m_dragRot = FALSE;
	m_dragPan = FALSE;
	m_downUi = FALSE;
	m_previewDrag = FALSE;
	m_previewPanDrag = FALSE;
	m_monDrag = FALSE;
	m_click = FALSE;
	m_rclick = FALSE;
	m_keyShift = FALSE;
	m_fastFwd = FALSE;
	m_padRepeat = 0;
	m_padDirPrev = -1;
	m_padTrig = 0;
	memset(&m_padPrev, 0, sizeof(m_padPrev));
}

void Game::OnKey(int vk, BOOL down)
{
	if (m_demo) {
		if (down && vk != VK_PROCESSKEY) DemoLeave();
		return;
	}
	m_idleT = 0;
	if (vk == VK_SHIFT) { m_keyShift = down; m_fastFwd = down; }
	if (!down) return;
	if (vk == VK_PROCESSKEY) return;
	if (m_textFocus == 3 && m_sellAsk) {
		if (vk == VK_ESCAPE) { m_sellAsk = 0; m_textFocus = 0; m_imeComp[0] = 0; TacSfx(TS_CANCEL); }
		else if (vk == VK_RETURN) m_sellGo = 1;
		else if (vk == VK_BACK || vk == VK_DELETE) { m_sellN /= 10; if (m_sellN < 1) m_sellN = 1; m_sellFresh = 0; }
		return;
	}
	if (m_textFocus == 1 || m_textFocus == 2 || m_textFocus == 4) {
		wchar_t* buf = NULL;
		int maxLen = 0;
		TextLive(buf, maxLen);
		int n = buf ? TacWLen(buf) : 0;
		if (m_textCaret < 0 || m_textCaret > n) m_textCaret = n;
		if (vk == VK_RETURN) {
			if (m_textFocus == 2) CommitSlotName();
			else if (m_textFocus != 4) { m_textFocus = 0; m_imeComp[0] = 0; }
			return;
		}
		if (vk == VK_ESCAPE || vk == VK_TAB) { m_textFocus = 0; m_imeComp[0] = 0; if (vk == VK_ESCAPE) TacSfx(TS_CANCEL); return; }
		if (vk == VK_BACK) { if (buf) TacDeleteAt(buf, m_textCaret, TRUE); if (m_textFocus == 4) m_codexPage = 0; return; }
		if (vk == VK_DELETE) { if (buf) TacDeleteAt(buf, m_textCaret, FALSE); if (m_textFocus == 4) m_codexPage = 0; return; }
		if (vk == VK_LEFT) { if (m_textCaret > 0) m_textCaret--; return; }
		if (vk == VK_RIGHT) { if (m_textCaret < n) m_textCaret++; return; }
		if (vk == VK_HOME) { m_textCaret = 0; return; }
		if (vk == VK_END) { m_textCaret = n; return; }
		return;
	}
	if (m_showHelp) {
		if (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_F1 || vk == 'H' || vk == VK_SPACE) { m_showHelp = FALSE; TacSfx(TS_CANCEL); }
		if (m_codex && (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_SPACE)) { m_codex = 0; return; }
		return;
	}
	if (vk == VK_F1 || vk == 'H') { m_showHelp = TRUE; TacSfx(TS_OPEN); return; }
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
		m_phaseT = max(m_phaseT, (m_surveyN >= 2 ? kIntroSweep : 0.35f) * TacPaceTime(m_save));
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
	case 'C': if (battleMap) CycleLayer(m_keyShift ? -1 : 1); break;
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
	if (m_textFocus) { m_padPrev = p; return; }
	auto pressed = [&](int bit) { return (p.buttons & (1 << bit)) && !(m_padPrev.buttons & (1 << bit)); };
	if (m_demo) {
		BOOL any = FALSE;
		for (int b = 0; b < 8; b++) if (pressed(b)) any = TRUE;
		m_padPrev = p;
		if (any) DemoLeave();
		return;
	}
	{
		BOOL any = FALSE;
		for (int b = 0; b < 8; b++) if (pressed(b)) any = TRUE;
		if (any) m_idleT = 0;
	}
	int dir = -1;
	float ax = p.lx, ay = p.ly;
	if (p.hat >= 0) { static const int hx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, hy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 }; ax = (float)hx[p.hat]; ay = (float)hy[p.hat]; }
	if (fabsf(ax) > .55f || fabsf(ay) > .55f) dir = fabsf(ax) > fabsf(ay) ? (ax > 0 ? 1 : 3) : (ay > 0 ? 2 : 0);
	if (dir >= 0) m_idleT = 0;
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
	if (m_screen == SC_BATTLE && m_phase == PH_INTRO && !m_showHelp && (pressed(0) || pressed(1))) m_phaseT = max(m_phaseT, (m_surveyN >= 2 ? kIntroSweep : 0.35f) * TacPaceTime(m_save));
	else {
		if (pressed(0)) {
			if (m_showHelp) { m_showHelp = FALSE; TacSfx(TS_CANCEL); }
			else if (battleMap && !m_navMode) BattleConfirmCursor();
			else NavConfirm();
		}
		if (pressed(1)) { if (m_showHelp) { m_showHelp = FALSE; TacSfx(TS_CANCEL); } else NavBack(); }
	}
	if (pressed(4)) m_camYawGoal -= PI * .5f;
	if (pressed(5)) m_camYawGoal += PI * .5f;
	if (pressed(6)) { m_showHelp = !m_showHelp; TacSfx(m_showHelp ? TS_OPEN : TS_CANCEL); }
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
	float usePx = px > 0 ? px : U(17);
	int bold = sel ? 0 : 1;
	usePx = FitTextPx(label, w - U(8), usePx, bold);
	m_r.HudText(label, x + w * .5f, y + h * .5f, usePx, tc, 1, 1, sel ? 0 : 1);
	return fire;
}

int Game::Sel(int id, float x, float y, float w, float h, const wchar_t* label, const wchar_t* value, int on)
{
	UiBtn b = { id, x, y, w, h, on ? 1 : 0, on ? 1 : 0 };
	m_btns.push_back(b);
	BOOL hov = !m_navMode && m_mouseSeen && Hit(x, y, w, h);
	BOOL foc = on && m_navMode && m_focusId == id;
	if (hov) { m_uiHoverHit = 1; m_uiHoverCand = id; }
	int d = 0;
	float vx = x + w * .4f, vw = w * .6f;
	if (!on) {
		if (m_click && m_clickX >= x && m_clickX < x + w && m_clickY >= y && m_clickY < y + h) {
			m_click = FALSE;
			TacSfx(TS_ERROR);
		}
		if (m_activateId == id) m_activateId = -1;
		m_r.HudGlass(x, y, w, h, RGBA8(26, 26, 32, 220), RGBA8(78, 78, 86), 1.f);
		m_r.HudText(label, x + U(10), y + h * .5f, FitTextPx(label, w * .36f, U(15), 0), RGBA8(128, 128, 136), 0, 0);
		m_r.HudText(value, vx + vw * .5f, y + h * .5f, FitTextPx(value, vw - U(28), U(16), 1), RGBA8(150, 150, 158), 1, 1);
		return 0;
	}
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
	m_r.HudText(label, x + U(10), y + h * .5f, FitTextPx(label, w * .36f, U(15), 0), RGBA8(220, 210, 180), 0, 0);
	BOOL hl = hov && m_mx < vx + vw * .5f, hr = hov && m_mx >= vx + vw * .5f;
	m_r.HudText(L"◀", vx + U(12), y + h * .5f, U(14), hl ? RGBA8(255, 230, 140) : RGBA8(170, 160, 140), 1, 1);
	m_r.HudText(L"▶", x + w - U(12), y + h * .5f, U(14), hr ? RGBA8(255, 230, 140) : RGBA8(170, 160, 140), 1, 1);
	m_r.HudText(value, vx + vw * .5f, y + h * .5f, FitTextPx(value, vw - U(28), U(16), 1), RGBA8(255, 255, 255), 1, 1);
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
	if (m_showHelp) { m_showHelp = FALSE; TacSfx(TS_CANCEL); return; }
	if (m_jobRedo == 3) {
		m_jobRedo = m_jobSkipAll && m_screen != SC_EVOLVE ? 1 : (m_screen == SC_EVOLVE ? 2 : 1);
		TacSfx(TS_CANCEL);
		return;
	}
	if (m_jobRedo == 2) {
		if (m_jobStep > 0) {
			m_jobStep--;
			m_jobSel = m_jobPath[m_jobStep];
			for (int i = m_jobStep; i < 12; i++) m_jobPath[i] = -1;
		} else {
			m_jobRedo = 1;
			m_screen = SC_BASE;
		}
		TacSfx(TS_CANCEL);
		return;
	}
	if (m_jobRedo == 1) { TacSfx(TS_CANCEL); return; }
	if (m_slotUi) {
		if (m_textFocus) { m_textFocus = 0; TacSfx(TS_CANCEL); return; }
		if (m_slotUi == 5) m_slotUi = 4;
		else if (m_slotUi == 6) m_slotUi = 8;
		else if (m_slotUi > 1) m_slotUi = 1;
		else m_slotUi = 0;
		TacSfx(TS_CANCEL);
		return;
	}
	switch (m_screen) {
	case SC_CREATE:
		if (m_textFocus) m_textFocus = 0;
		else if (m_createFromParty) { m_createFromParty = 0; EnterParty(); }
		else EnterBase();
		TacSfx(TS_CANCEL);
		break;
	case SC_PARTY:
		if (m_eqPick) m_eqPick = 0;
		else if (m_confirmDelete) m_confirmDelete = 0;
		else if (m_partyBack && m_shop) { m_partyBack = 0; m_screen = SC_BASE; }
		else EnterBase();
		TacSfx(TS_CANCEL);
		break;
	case SC_EVOLVE:
		if (m_evPick > 0) { m_evPick--; if (m_evPick == 0) m_evTarget = -1; m_evPartner[0] = m_evPartner[1] = -1; }
		else if (m_evFromParty) { m_evFromParty = 0; EnterParty(); }
		else EnterBase();
		TacSfx(TS_CANCEL);
		break;
	case SC_BATTLE:
		if (m_stashFull) { m_stashFull = 0; m_shopFit = 0; TacSfx(TS_CANCEL); break; }
		BattleCancel();
		break;
	case SC_BASE:
		if (m_sortieAsk) { m_sortieAsk = 0; TacSfx(TS_CANCEL); }
		else if (m_mapLook) { m_mapLook = 0; m_showYaw = m_camYaw; TacSfx(TS_CANCEL); }
		else if (!m_homeUi && !m_codex && !m_shop && !m_slotUi && m_jobRedo != 1 && m_jobRedo != 3 && !m_baseOpt) { m_homeUi = 1; TacSfx(TS_OPEN); }
		else if (m_codex) {
			if (m_textFocus == 4) { m_textFocus = 0; TacSfx(TS_CANCEL); }
			else { m_codex = 0; TacSfx(TS_CANCEL); }
		}
		else if (m_shop && m_sellAsk) { m_sellAsk = 0; m_textFocus = 0; TacSfx(TS_CANCEL); }
		else if (m_shop && m_shopFit == 13) { m_shopFit = 4; TacSfx(TS_CANCEL); }
		else if (m_shop && (m_shopHold || m_shopFit)) { m_shopHold = 0; m_shopFit = 0; m_shopWho = -1; TacSfx(TS_CANCEL); }
		else if (m_shop && m_shopBand) { m_shopBand = 0; m_shopPage = 0; TacSfx(TS_CANCEL); }
		else if (m_shop && m_shopTier) { m_shopTier = 0; m_shopPage = 0; TacSfx(TS_CANCEL); }
		else if (m_shop) {
			m_shop = 0; m_shopHold = 0; m_shopFit = 0; m_shopTier = 0; m_shopBand = 0; m_partyBack = 0; m_shopWho = -1;
			if (m_shopBack) { m_shopBack = 0; EnterParty(); }
			TacSfx(TS_CANCEL);
		} else if (m_baseOpt) { m_baseOpt = 0; TacSfx(TS_CANCEL); }
		break;
	default: break;
	}
}

// ============================================================ 通知
static void DrawMsgLine(Renderer& r, const wchar_t* s, float x, float y, float px, DWORD col, int pat, float u, float seed, int bold)
{
	if (!s || !s[0] || px < 1.f) return;
	if (pat < 0) pat = 0;
	pat %= 8;
	u = Clampf(u, 0.f, 1.f);
	float inn = Smooth01(u / 0.22f);
	float out = u < 0.62f ? 0.f : Smooth01((u - 0.62f) / 0.38f);
	int n = (int)wcslen(s);
	if (n > 96) n = 96;
	float widths[96], home[96];
	float total = 0.f;
	for (int i = 0; i < n; i++) {
		wchar_t g[2] = { s[i], 0 };
		widths[i] = r.TextWidth(g, px, bold);
		if (widths[i] < 0.5f) widths[i] = px * 0.35f;
		total += widths[i];
	}
	float cursor = x - total * 0.5f;
	for (int i = 0; i < n; i++) {
		home[i] = cursor + widths[i] * 0.5f;
		cursor += widths[i];
	}
	auto glyph = [&](int i, float gx, float gy, float sc, float a) {
		if (a < 0.03f || sc < 0.08f) return;
		wchar_t g[2] = { s[i], 0 };
		int sh = (a > 0.82f && sc > 0.9f) ? 1 : 0;
		r.HudText(g, gx, gy, px * sc, col, 1, bold, sh, a);
	};
	auto whole = [&](float gx, float gy, float sc, float a) {
		if (a < 0.03f || sc < 0.08f) return;
		int sh = (a > 0.82f && sc > 0.9f) ? 1 : 0;
		r.HudText(s, gx, gy, px * sc, col, 1, bold, sh, a);
	};
	if (pat == 0) {
		float spread = (1.f - inn) * px * 0.62f;
		if (spread > 0.8f) {
			for (int k = 0; k < 6; k++) {
				float ang = k * 1.047f + seed;
				whole(x + cosf(ang) * spread, y + sinf(ang) * spread * 0.65f, 1.f, 0.16f * inn + 0.05f);
			}
		}
		if (out < 0.04f) whole(x, y, Lerpf(1.12f, 1.f, inn), inn);
		else {
			for (int i = 0; i < n; i++) {
				float ang = seed + i * 1.71f;
				float dist = out * px * (2.2f + (i % 3) * 0.85f);
				glyph(i, home[i] + cosf(ang) * dist, y + sinf(ang) * dist * 0.75f - out * px * 0.4f, 1.f - out * 0.55f, 1.f - out);
			}
		}
	} else if (pat == 1) {
		float sc = u < 0.22f ? Lerpf(0.22f, 1.18f, inn) : (out > 0.f ? Lerpf(1.f, 0.18f, out) : Lerpf(1.18f, 1.f, Smooth01((u - 0.22f) / 0.16f)));
		float dy = (1.f - inn) * px * 1.6f - out * px * 2.2f;
		whole(x, y + dy, sc, (u < 0.22f ? inn : 1.f - out));
	} else if (pat == 2) {
		for (int i = 0; i < n; i++) {
			float base = seed + i * (6.28318f / max(1, n));
			float radIn = (1.f - inn) * px * 3.1f;
			float radOut = out * px * 4.2f;
			float ang = base + (1.f - inn) * 2.4f + out * 1.6f;
			float gx = Lerpf(x + cosf(ang) * radIn, home[i], inn) + cosf(ang) * radOut;
			float gy = y + sinf(ang) * (radIn + radOut) * 0.72f;
			glyph(i, gx, gy, 1.f - out * 0.45f, inn * (1.f - out));
		}
	} else if (pat == 3) {
		float gap = (1.f - inn) * px * 1.7f + out * px * 2.4f;
		float a = inn * (1.f - out);
		whole(x, y - gap, 1.f, a * 0.45f);
		whole(x, y, 1.f, a);
		whole(x, y + gap, 1.f, a * 0.45f);
	} else if (pat == 4) {
		float face = u < 0.22f ? inn : (out > 0.f ? 1.f - out : 1.f);
		float lift = sinf(face * 3.14159f) * px * 0.22f;
		for (int i = 0; i < n; i++)
			glyph(i, x + (home[i] - x) * face, y - lift, 0.82f + 0.18f * face, 0.2f + 0.8f * face);
	} else if (pat == 5) {
		float sc = u < 0.22f ? Lerpf(0.45f, 1.22f, inn) : (out > 0.f ? Lerpf(1.f, 0.35f, out) : Lerpf(1.22f, 1.f, Smooth01((u - 0.22f) / 0.14f)));
		float dy = (1.f - inn) * px * 1.35f - out * px * 3.1f;
		whole(x, y + dy, sc, inn * (1.f - out));
	} else if (pat == 6) {
		for (int i = 0; i < n; i++) {
			float ang = seed * 1.3f + i * 1.9f;
			float gather = 1.f - inn;
			float burst = out * px * (2.6f + (i % 4) * 0.7f);
			float gx = home[i] + (x - home[i]) * gather + cosf(ang) * burst;
			float gy = y + sinf(ang) * burst * 0.8f - out * px * 0.3f;
			float sc = Lerpf(1.7f, 1.f, inn) * (1.f - out * 0.4f);
			glyph(i, gx, gy, sc, inn * (1.f - out));
		}
	} else {
		float fall = out * out * px * 3.4f;
		float sc = 1.f - out * 0.55f;
		float a = inn * (1.f - out);
		int trails = 4;
		for (int k = trails; k >= 1; k--)
			whole(x - k * px * 0.12f, y + k * px * 0.2f + fall * (1.f + k * 0.15f), sc * (1.f - k * 0.04f), a * (0.18f / k));
		whole(x, y + fall, sc, a);
	}
}

void Game::AddBanner(const wchar_t* s, const wchar_t* sub, DWORD col, int style, float life, int pat)
{
	Banner b = {};
	wcsncpy_s(b.s, s ? s : L"", _TRUNCATE);
	wcsncpy_s(b.sub, sub ? sub : L"", _TRUNCATE);
	b.col = col; b.style = style; b.life = life; b.t = 0;
	b.pat = pat < 0 ? MSG_FACE : (pat > MSG_DROP ? MSG_DROP : pat);
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
void Game::AddPopup(V3 p, const wchar_t* s, DWORD col, int style, int pat, float delay, float life)
{
	Popup q = {};
	q.p = p;
	wcsncpy_s(q.s, s, _TRUNCATE);
	q.col = col; q.style = style; q.life = life > 0 ? life : (style == 2 ? 1.6f : 1.25f); q.delay = delay;
	q.size = style == 2 ? 1.35f : 1.f;
	q.pat = pat < 0 ? MSG_BURST : (pat > MSG_DROP ? MSG_DROP : pat);
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
			float px = (p.style == 1 ? U(17) : U(26)) * p.size;
			DrawMsgLine(m_r, p.s, sx, sy, px, p.col, p.pat, u, p.t * 0.7f + p.pat, 1);
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
		float u = b.t / max(0.05f, b.life);
		float y = m_h * .38f;
		float px = U(30);
		if (b.style == 1) { y = m_h * .4f; px = U(52); }
		else if (b.style == 2) { y = stackY + U(16); px = U(16); stackY += U(46); }
		if (b.style == 0 && b.life >= 4.f) {
			float subPx = px * 0.48f;
			float tw = m_r.TextWidth(b.s, px, 1);
			float sw = b.sub[0] ? m_r.TextWidth(b.sub, subPx, 0) : 0.f;
			float bw = max(tw, sw) + U(64);
			float bh = px * 1.15f + (b.sub[0] ? subPx * 1.7f : px * 0.35f);
			float bx = m_w * 0.5f - bw * 0.5f;
			float by = y - px * 0.72f;
			int cr = b.col & 255, cg = (b.col >> 8) & 255, cb = (b.col >> 16) & 255;
			int pulse = 170 + (int)(50.f * sinf(b.t * 2.6f));
			m_r.HudRect(bx, by, bw, bh, RGBA8(8, 10, 18, 186));
			m_r.HudFrame(bx, by, bw, bh, U(3.5f), RGBA8(cr, cg, cb, pulse));
			m_r.HudFrame(bx + U(7), by + U(7), bw - U(14), bh - U(14), U(1.5f), RGBA8(255, 236, 214, 150));
		}
		DrawMsgLine(m_r, b.s, m_w * .5f, y, px, b.col, b.pat, u, b.pat * 1.3f, 1);
		if (b.sub[0]) DrawMsgLine(m_r, b.sub, m_w * .5f, y + px * 0.85f, px * 0.48f, RGBA8(235, 235, 240), b.pat, u, b.pat * 1.3f + 0.4f, 0);
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
	wchar_t est[160];
	est[0] = 0;
	if (m_skillTip != 2 && m_partySel >= 0 && m_partySel < MAX_ROSTER && m_save.roster[m_partySel].used) {
		Unit eu{};
		eu.monster = -1;
		eu.alive = 1;
		eu.ch = m_save.roster[m_partySel];
		CraftBindUnit(eu);
		EstimateText(eu, m_skillTipId, est, 160);
	}
	float tw = min(U(460), (float)m_w - U(16));
	float th = (extra[0] ? U(128) : U(108)) + (est[0] ? U(18) : 0);
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
	if (est[0]) {
		m_r.HudText(est, tx + U(10), by, FitTextPx(est, tw - U(20), U(13), 0), RGBA8(186, 230, 255), 0, 0);
		by += U(16);
	}
	m_r.HudTextBox(body, tx + U(10), by, tw - U(20), th - (by - ty) - U(8), U(13), RGBA8(230, 230, 236));
}

float Game::DrawStatLines(const CharData& c, float x, float y, float w, const int* gains, int hpCur, int mpCur)
{
	int cols = w >= U(340) ? 4 : 2;
	float lh = U(cols == 4 ? 20.f : 18.f);
	float maxNeed = 0.f;
	for (int s = 0; s < S_COUNT; s++) {
		int bonus = gains ? gains[s] : 0;
		int total = c.stat[s] + bonus;
		wchar_t lab[32], v[48], gbuf[24];
		wcscpy_s(lab, TT(StatNameTx(s)));
		if (s == S_HP && hpCur >= 0) swprintf_s(v, L"%s/%s", TacNum(hpCur), TacNum(total));
		else if (s == S_MP && mpCur >= 0) swprintf_s(v, L"%s/%s", TacNum(mpCur), TacNum(total));
		else swprintf_s(v, L"%s", TacNum(c.stat[s]));
		gbuf[0] = 0;
		if (bonus > 0) swprintf_s(gbuf, L"+%s", TacNum(bonus));
		else if (bonus < 0) swprintf_s(gbuf, L"%s", TacNum(bonus));
		float need = m_r.TextWidth(lab, U(13), 0) + U(3) + m_r.TextWidth(v, U(14), 1);
		if (gbuf[0]) need += U(3) + m_r.TextWidth(gbuf, U(12), 1);
		if (need > maxNeed) maxNeed = need;
	}
	float cwCap = w / (float)cols;
	float cw = maxNeed + U(8);
	if (cw > cwCap) cw = cwCap;
	if (cw < U(64) && cwCap > cw) cw = min(cwCap, U(88));
	float block = cw * (float)cols;
	float x0 = x + max(0.f, (w - block) * .5f);
	for (int s = 0; s < S_COUNT; s++) {
		float col = (float)(s % cols), row = (float)(s / cols);
		float sx = x0 + col * cw, sy = y + row * lh;
		float cell = cw - U(3);
		BOOL hov = m_mouseSeen && Hit(sx, sy, cell, lh);
		if (hov) {
			m_statTip = s;
			m_r.HudRect(sx, sy, cell, lh, RGBA8(255, 220, 140, 36));
		}
		int bonus = gains ? gains[s] : 0;
		int total = c.stat[s] + bonus;
		wchar_t lab[32];
		wcscpy_s(lab, TT(StatNameTx(s)));
		wchar_t v[48], gbuf[24];
		if (s == S_HP && hpCur >= 0) swprintf_s(v, L"%s/%s", TacNum(hpCur), TacNum(total));
		else if (s == S_MP && mpCur >= 0) swprintf_s(v, L"%s/%s", TacNum(mpCur), TacNum(total));
		else swprintf_s(v, L"%s", TacNum(c.stat[s]));
		gbuf[0] = 0;
		if (bonus > 0) swprintf_s(gbuf, L"+%s", TacNum(bonus));
		else if (bonus < 0) swprintf_s(gbuf, L"%s", TacNum(bonus));
		float pxLab = U(13), pxNum = U(14), pxG = U(12);
		float gap = U(3);
		float labW = m_r.TextWidth(lab, pxLab, 0);
		float numW = m_r.TextWidth(v, pxNum, 1);
		float gW = gbuf[0] ? m_r.TextWidth(gbuf, pxG, 1) : 0.f;
		float need = labW + gap + numW + (gW > 0.f ? gap + gW : 0.f);
		if (need > cell && need > 1.f) {
			float sc = cell / need;
			if (sc < 0.55f) sc = 0.55f;
			pxLab *= sc; pxNum *= sc; pxG *= sc;
			labW = m_r.TextWidth(lab, pxLab, 0);
			numW = m_r.TextWidth(v, pxNum, 1);
			gW = gbuf[0] ? m_r.TextWidth(gbuf, pxG, 1) : 0.f;
		}
		float cx = sx + U(1);
		m_r.HudText(lab, cx, sy + lh * .5f, pxLab, hov ? RGBA8(255, 236, 180) : RGBA8(200, 190, 160), 0, 0);
		cx += labW + gap;
		m_r.HudText(v, cx, sy + lh * .5f, pxNum, RGBA8(250, 250, 250), 0, 1);
		if (gbuf[0]) m_r.HudText(gbuf, cx + numW + gap, sy + lh * .5f, pxG, bonus > 0 ? RGBA8(120, 255, 140) : RGBA8(255, 120, 110), 0, 1);
	}
	return lh * (float)((S_COUNT + cols - 1) / cols);
}

// ============================================================ ポートレート
void Game::InvalidatePortraits()
{
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) {
		m_portraitKey[i] = -1;
		m_ptCacheWant[i] = -1;
	}
}

BOOL Game::RenderActorPortrait(int slot, Actor& a, DWORD bg0, DWORD bg1, int fitBody, BOOL live)
{
	if (!live) {
		if (a.clip != CL_IDLE) ActorPlay(a, CL_IDLE, 1, 0);
		float len = ActorClipLen(a, m_lib, m_r, CL_IDLE);
		if (len < 0.25f) len = 1.6f;
		a.t = fmodf(m_time * 0.9f + slot * 0.41f, len);
	}
	if (!live || !a.poseValid || a.poseFrame != m_r.m_frame)
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
		BOOL posed = FALSE;
		if (a.proc == 3 && a.poseValid) {
			if (a.frameModel == a.model && a.frameClip == a.clip && a.frameR > 0.15f) {
				cx = a.frameC[0]; cy = a.frameC[1]; cz = a.frameC[2]; cr = a.frameR; posed = TRUE;
			} else if (m_r.CreaturePosedExtent(a.model, a.pose, cx, cy, cz, cr)) {
				a.frameC[0] = cx; a.frameC[1] = cy; a.frameC[2] = cz; a.frameR = cr;
				a.frameClip = a.clip; a.frameModel = a.model; posed = TRUE;
			}
		}
		if (posed) posedR = cr * fabsf(a.scale);
		// バインド寸法は数センチのまま、描画は骨格の倍率で広がる。その寸法で枠を切るとカメラが体内に入る。
		float h = a.height > 0.25f ? a.height : 1.2f;
		if (posed && posedR > 0.15f && posedR < 12.f && posedR < h * 4.f) {
			look = MXformPt(world, v3(cx, cy, cz));
			rad = posedR * (posedR > h * 2.2f ? 0.95f : 0.78f);
			body = TRUE;
		} else {
			look = v3(0, h * 0.55f, 0);
			rad = h * 0.62f;
			body = TRUE;
		}
	}
	if (body) {
		d = rad * 1.28f / tanf(.3f);
		if (fitBody && !(a.proc == 3)) look = v3(0, midY, 0);
		eye = look + v3(d * .2f, d * .05f, -d);
	} else if (a.proc == 3) {
		look = ActorJointWorld(a, world, 0);
		V3 chest = ActorJointWorld(a, world, 2);
		float ht = a.htScale > 0.05f ? a.htScale : 1.f;
		look.x *= ht; look.y *= ht; look.z *= ht;
		chest.x *= ht; chest.y *= ht; chest.z *= ht;
		look.y = Lerpf(look.y, chest.y, a.wings > 0 ? .55f : .28f);
		d = (a.height / ht) * (a.wings > 0 ? 1.05f : .78f);
		eye = look + v3(d * .06f, d * .02f, -d);
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
	if (!m_r.BeginPortrait(slot)) return FALSE;
	m_r.SetCamera(view, proj, eye, m_time);
	DrawParams dp;
	dp.outline = 0;
	dp.rim = .22f;
	{
		int r = ((bg0 & 255) + (bg1 & 255)) / 2;
		int g = (((bg0 >> 8) & 255) + ((bg1 >> 8) & 255)) / 2;
		int b = (((bg0 >> 16) & 255) + ((bg1 >> 16) & 255)) / 2;
		m_r.SetPortraitBg(RGBA8(r, g, b));
	}
	m_r.BeginMainPass();
	BOOL drew = ActorDraw(m_r, m_lib, a, world, dp, m_time);
	m_r.EndPortrait();
	return drew;
}

void Game::RenderPortraits()
{
	M4 camView = m_r.m_view, camProj = m_r.m_proj;
	V3 camEye = m_r.m_eye;
	float camT = m_r.m_time;
	m_r.SetSight(NULL, 0, 0);
	m_r.SetFog(500.f, 1000.f, 0, RGBA8(0, 0, 0));
	m_r.SetAmbient(RGBA8(206, 192, 178), RGBA8(148, 116, 94), .58f, .55f);
	m_r.SetLight(Norm(v3(.42f, .84f, -.40f)), v3(0, 0, 0), 3.5f);
	m_r.SetShadowEnabled(FALSE);
	auto look = [](const CharData& c) -> unsigned {
		unsigned h = (unsigned)c.gender;
		h = h * 16777619u ^ (unsigned)c.cls;
		h = h * 16777619u ^ (unsigned)c.body;
		h = h * 16777619u ^ (unsigned)c.hair;
		h = h * 16777619u ^ (unsigned)c.hairColor;
		h = h * 16777619u ^ (unsigned)c.outfit;
		h = h * 16777619u ^ (unsigned)c.hat;
		h = h * 16777619u ^ (unsigned)c.cape;
		h = h * 16777619u ^ (unsigned)c.lv;
		h = h * 16777619u ^ (unsigned)c.reserved[6];
		h = h * 16777619u ^ (unsigned)c.reserved[7];
		return h;
	};
	auto showOf = [&](int roster) -> Actor* {
		int idx = 0;
		for (int i = 0; i < MAX_PARTY && idx < m_showN; i++) {
			int r = m_save.party[i];
			if (r < 0 || r >= MAX_ROSTER || !m_save.roster[r].used) continue;
			if (r == roster) return &m_showAct[idx];
			idx++;
		}
		return NULL;
	};
	auto fillChar = [&](int s, int want, const CharData& c) {
		unsigned lk = look(c);
		if (m_ptCacheWant[s] != want || m_ptCacheLook[s] != (int)lk) {
			ActorFromChar(m_ptCache[s], m_lib, m_r, c);
			m_ptCacheWant[s] = want;
			m_ptCacheLook[s] = (int)lk;
		}
	};
	auto paint = [&](int s, int want) -> BOOL {
		if (want >= 100 && want < 100 + MAX_UNITS) {
			int ui = want - 100;
			const Unit& u = m_b.u[ui];
			if (ui >= 0 && ui < MAX_UNITS && m_act[ui].model >= 0) {
				DWORD bg0 = u.team == TEAM_PLAYER ? RGBA8(70, 110, 190) : (UnitIsLucky(u) ? RGBA8(30, 130, 180) : (u.boss ? RGBA8(200, 140, 40) : RGBA8(180, 60, 60)));
				DWORD bg1 = u.team == TEAM_PLAYER ? RGBA8(20, 30, 70) : (UnitIsLucky(u) ? RGBA8(8, 36, 64) : RGBA8(50, 14, 20));
				return RenderActorPortrait(s, m_act[ui], bg0, bg1, u.team != TEAM_PLAYER, TRUE);
			}
			if (u.team == TEAM_PLAYER) {
				fillChar(s, want, u.ch);
				return RenderActorPortrait(s, m_ptCache[s], RGBA8(70, 110, 190), RGBA8(20, 30, 70), 0, FALSE);
			}
			const MonsterDef& md = MonsterInfo(u.monster);
			if (m_ptCacheWant[s] != want || m_ptCacheLook[s] != u.monster) {
				ActorFromMonster(m_ptCache[s], m_lib, m_r, u.monster, u.boss);
				m_ptCacheWant[s] = want;
				m_ptCacheLook[s] = u.monster;
			}
			if (md.look == LK_MESH && m_ptCache[s].proc != 3) return FALSE;
			return RenderActorPortrait(s, m_ptCache[s], UnitIsLucky(u) ? RGBA8(30, 130, 180) : (u.boss ? RGBA8(200, 140, 40) : RGBA8(180, 60, 60)), UnitIsLucky(u) ? RGBA8(8, 36, 64) : RGBA8(50, 14, 20), 1, FALSE);
		}
		if (want >= 1 && want <= MAX_ROSTER) {
			const CharData& c = m_save.roster[want - 1];
			if (!c.used) return FALSE;
			Actor* sh = showOf(want - 1);
			if (sh && sh->model >= 0 && sh->poseValid) {
				return RenderActorPortrait(s, *sh, RGBA8(70, 110, 190), RGBA8(20, 30, 70), 0, TRUE);
			}
			fillChar(s, want, c);
			return RenderActorPortrait(s, m_ptCache[s], RGBA8(70, 110, 190), RGBA8(20, 30, 70), 0, FALSE);
		}
		return FALSE;
	};
	for (int s = 0; s < Renderer::PORTRAIT_N; s++) {
		int want = m_portraitWant[s];
		if (want <= 0) { m_portraitKey[s] = want; continue; }
		if (!paint(s, want)) continue;
		m_portraitKey[s] = want;
	}
	m_r.FinishPortraits();
	m_r.SetCamera(camView, camProj, camEye, camT);
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
		if (m_homeUi && !m_mapLook && !m_shop && !m_codex && !m_slotUi && m_jobRedo != 1 && m_jobRedo != 3) {
			int cap = PartyCap();
			for (int i = 0; i < cap; i++) add(m_save.party[i]);
		}
	} else if (m_screen == SC_PARTY) {
		add(m_partySel);
		int begin = m_rosterPage * 20;
		int cap = RosterCap();
		for (int i = 0; i < 20 && begin + i < cap; i++) add(begin + i);
	} else if (m_screen == SC_EVOLVE) {
		if (m_jobRedo == 2 || m_jobRedo == 3) {
			add(m_jobWho);
		} else if (m_evoCut) {
			add(m_evoRoster);
		} else {
			add(m_evMain);
			add(m_evPartner[0]);
			add(m_evPartner[1]);
			int cap = RosterCap();
			for (int i = 0; i < cap; i++) add(i);
		}
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

void Game::WeatherTread(const Unit& u, int x, int z)
{
	if (m_screen != SC_BATTLE || !Map::In(x, z)) return;
	if (UnitMoveType(u) == MV_FLY) return;
	int wx = TacWx(m_b.weather);
	if (wx == WX_CLEAR || wx == WX_FOG || wx == WX_HEAT) return;
	int i = z * MAPN + x;
	int d = m_cover[i];
	if (d < 4) {
		if (m_b.map.At(x, z).t != T_WATER) return;
	}
	int vanish = max(1, d / 6);
	int shove = max(0, d / 3);
	if (vanish + shove > d) { vanish = d / 5; shove = d - vanish; }
	m_cover[i] = (unsigned char)(d - vanish - shove);
	float fx = sinf(u.face), fz = cosf(u.face);
	int lx, lz;
	if (fabsf(fx) > fabsf(fz)) { lx = 0; lz = fx >= 0.f ? 1 : -1; }
	else { lz = 0; lx = fz >= 0.f ? -1 : 1; }
	int side[2][2] = { { x + lx, z + lz }, { x - lx, z - lz } };
	for (int s = 0; s < 2; s++) {
		int sx = side[s][0], sz = side[s][1];
		if (!Map::In(sx, sz)) continue;
		int t = m_b.map.At(sx, sz).t;
		if (t == T_VOID || t == T_CHASM) continue;
		int si = sz * MAPN + sx;
		int nd = m_cover[si] + shove / 2;
		if (nd > 220) nd = 220;
		m_cover[si] = (unsigned char)nd;
	}
	if (m_parts.size() > 3600) return;
	float gy = m_b.map.StandY(x, z);
	for (int k = 0; k < 4; k++) {
		float ang = (k + 0.5f) * 1.5708f + u.face;
		Particle q = {};
		q.p = v3(x + 0.5f, gy + 0.08f, z + 0.5f);
		q.v = v3(cosf(ang) * m_rng.F(1.2f, 2.4f), m_rng.F(0.8f, 1.8f), sinf(ang) * m_rng.F(1.2f, 2.4f));
		q.life = q.max = m_rng.F(0.28f, 0.48f);
		q.size = 0.05f; q.size1 = 0.02f; q.a = 1;
		q.grav = (wx == WX_SNOW || wx == WX_PETAL) ? 2.4f : 12.f;
		q.drag = 0.4f; q.add = 1; q.shape = 0; q.wxHit = 0;
		if (wx == WX_SNOW) { q.r = 0.95f; q.g = 0.97f; q.b = 1.f; q.wxHit = 3; }
		else if (wx == WX_ASH) { q.r = 0.55f; q.g = 0.4f; q.b = 0.32f; q.wxHit = 4; }
		else if (wx == WX_SAND) { q.r = 0.9f; q.g = 0.75f; q.b = 0.4f; q.wxHit = 6; }
		else if (wx == WX_HAIL) { q.r = 0.9f; q.g = 0.95f; q.b = 1.f; q.wxHit = 5; }
		else if (wx == WX_PETAL) { q.r = 1.f; q.g = 0.7f; q.b = 0.78f; q.wxHit = 7; }
		else { q.r = 0.7f; q.g = 0.84f; q.b = 1.f; }
		m_parts.push_back(q);
	}
}

// 髪や服と同じ突風。地形に沿った渦を足して、粒がまっすぐ飛ばないようにする。
static void WindField(float time, float x, float z, float& ox, float& oz, float& lift, int theme)
{
	float burst = TacBurst(time);
	float ang = time * 0.23f;
	float mag = 1.25f + burst * 6.2f;
	if (theme == TH_DESERT || theme == TH_CANYON) mag *= 1.48f;
	else if (theme == TH_PLAINS) mag *= 1.12f;
	else if (theme == TH_SNOW) mag *= 0.68f;
	else if (theme == TH_FOREST) mag *= 0.52f;
	else if (theme == TH_SEABED) mag *= 0.32f;
	else if (theme == TH_ORBIT) mag *= 0.18f;
	else if (theme == TH_VOLCANO) mag *= 1.22f;
	else if (theme == TH_FALLS) mag *= 0.82f;
	else if (theme == TH_STATION) mag *= 0.4f;
	else if (theme == TH_CITADEL) mag *= 0.7f;
	else if (theme == TH_LANTERN) mag *= 0.55f;
	ox = cosf(ang) * mag;
	oz = sinf(ang) * mag;
	float s = sinf(x * 0.41f + time * 0.55f) * cosf(z * 0.33f - time * 0.37f);
	float c = cosf(x * 0.27f - time * 0.48f) * sinf(z * 0.36f + time * 0.29f);
	ox += -c * (0.9f + burst * 2.2f);
	oz += s * (0.9f + burst * 2.2f);
	lift = burst * 2.4f + s * 0.45f;
	if (theme == TH_VOLCANO) lift += 1.15f;
	else if (theme == TH_SEABED) lift -= 0.35f;
	else if (theme == TH_SWAMP) lift += 0.25f;
	else if (theme == TH_ORBIT) { ox *= 0.45f; oz *= 0.45f; lift = sinf(time * 0.4f + x) * 0.22f; }
	else if (theme == TH_FALLS) lift += 0.55f;
}

void Game::UpdateParticles(float dt)
{
	static float sVX = 1e9f, sVZ = 1e9f;
	if (m_r.SceneryPuffCount() > 0 && m_parts.size() < 2800) {
		m_sceneAcc += dt;
		float step = (QualityNow() == 4) ? 0.3f : 0.12f;
		int ticks = 0;
		int puffTh = 0;
		if (m_screen == SC_BATTLE) puffTh = Clampi(m_b.map.theme, 0, TH_COUNT - 1);
		else puffTh = Clampi(m_showMap.theme, 0, TH_COUNT - 1);
		while (m_sceneAcc >= step && ticks < 3) {
			m_sceneAcc -= step;
			ticks++;
			int pn = m_r.SceneryPuffCount();
			for (int i = 0; i < pn; i++) {
				Renderer::SceneryPuff s = m_r.SceneryPuffAt(i);
				V3 p = v3(s.x, s.y, s.z);
				float pwx = 0.f, pwz = 0.f, plift = 0.f;
				WindField(m_time, s.x, s.z, pwx, pwz, plift, puffTh);
				if (s.kind == 0) {
					Burst(p, 4, 3.4f, 0.14f, 1.35f, 1.f, 0.32f, 0.05f, 2, 1, 7.5f, 0.08f, 2.1f, 0.03f);
					Burst(p, 2, 2.2f, 0.1f, 1.1f, 0.45f, 0.28f, 0.22f, 3, 1, 9.f, 0, 1.6f, 0.04f);
					Burst(p + v3(0, 0.2f, 0), 1, 0.7f, 0.55f, 2.4f, 0.28f, 0.26f, 0.24f, 0, 1, -0.35f, 0.02f, 2.6f, 0.95f);
				} else if (s.kind == 1) {
					Burst(p, 2, 0.4f, 0.7f, 1.8f, 0.9f, 0.96f, 1.f, 0, 1, -0.1f, 0, 1.2f, 1.0f);
					int drops = (QualityNow() == 4) ? 2 : 5;
					for (int k = 0; k < drops && (int)m_parts.size() < 3600; k++) {
						Particle q = {};
						q.p = p + v3(m_rng.F(-0.35f, 0.35f), m_rng.F(-1.8f, 1.6f), m_rng.F(-0.35f, 0.35f));
						q.v = v3(pwx * 0.22f + m_rng.F(-0.25f, 0.25f), m_rng.F(-4.8f, -1.6f), pwz * 0.22f + m_rng.F(-0.25f, 0.25f));
						q.life = q.max = m_rng.F(0.7f, 1.4f);
						q.size = m_rng.F(0.035f, 0.07f); q.size1 = q.size * 0.22f;
						q.r = 0.9f; q.g = 0.96f; q.b = 1.f; q.a = 1;
						q.shape = 1; q.add = 1; q.grav = 11.f; q.drag = 0.28f; q.stretch = 0.055f;
						q.wxHit = 8; q.nHit = 9;
						m_parts.push_back(q);
					}
				} else if (s.kind == 16) {
					Burst(p, 4, 1.6f, 0.06f, 0.55f, 0.92f, 0.97f, 1.f, 1, 1, 14.f, 0.03f, 2.4f, 0.02f);
					Burst(p, 2, 0.7f, 0.45f, 1.1f, 0.88f, 0.94f, 1.f, 0, 0, -0.08f, 0, 1.8f, 0.7f);
					int drops = (QualityNow() == 4) ? 3 : 7;
					for (int k = 0; k < drops && (int)m_parts.size() < 3600; k++) {
						float ang = m_rng.F(0.f, 6.2831853f);
						float sp = m_rng.F(1.1f, 3.2f);
						Particle q = {};
						q.p = p + v3(m_rng.F(-0.15f, 0.15f), m_rng.F(0.02f, 0.12f), m_rng.F(-0.15f, 0.15f));
						q.v = v3(cosf(ang) * sp + pwx * 0.35f, m_rng.F(1.4f, 3.6f) + plift * 0.25f, sinf(ang) * sp + pwz * 0.35f);
						q.life = q.max = m_rng.F(0.35f, 0.75f);
						q.size = m_rng.F(0.03f, 0.055f); q.size1 = q.size * 0.2f;
						q.r = 0.92f; q.g = 0.97f; q.b = 1.f; q.a = 1;
						q.shape = 0; q.add = 1; q.grav = 16.f; q.drag = 0.4f; q.stretch = 0.03f;
						q.wxHit = 8; q.nHit = 16;
						m_parts.push_back(q);
					}
				} else if (s.kind == 2) {
					for (int k = 0; k < 2 && (int)m_parts.size() < 3600; k++) {
						Particle q = {};
						q.p = p + v3(m_rng.F(-0.08f, 0.08f), m_rng.F(0.02f, 0.18f), m_rng.F(-0.08f, 0.08f));
						q.v = v3(pwx * 0.12f + m_rng.F(-0.08f, 0.08f), m_rng.F(0.18f, 0.45f) + plift * 0.08f, pwz * 0.12f + m_rng.F(-0.08f, 0.08f));
						q.life = q.max = m_rng.F(2.0f, 3.2f);
						q.size = 0.022f; q.size1 = 0.01f;
						q.r = 0.72f; q.g = 0.94f; q.b = 1.f; q.a = 1;
						q.shape = 1; q.add = 1; q.grav = -1.4f; q.drag = 0.7f;
						q.wxHit = 8; q.nHit = 8;
						m_parts.push_back(q);
					}
				} else if (s.kind == 3) {
					Burst(p, 1, 0.22f, 0.55f, 2.6f, 0.62f, 0.82f, 0.48f, 0, 1, -0.15f, 0, 1.2f, 0.9f);
					Burst(p, 1, 0.8f, 0.06f, 1.8f, 0.85f, 1.f, 0.45f, 1, 1, -0.4f, 0, 2.f, 0.02f);
				} else if (s.kind == 4) {
					Burst(p + v3(0, 0.55f, 0), 1, 0.32f, 0.1f, 3.6f, 0.96f, 0.9f, 0.62f, 0, 0, 0.12f, 0, 0.55f, 0.14f);
				} else if (s.kind == 5) {
					Burst(p, 2, 0.9f, 0.42f, 2.4f, 0.72f, 0.58f, 0.38f, 0, 1, -0.25f, 0, 2.8f, 0.7f);
				} else if (s.kind == 6) {
					Burst(p + v3(0, 0.6f, 0), 2, 0.55f, 0.05f, 2.6f, 0.75f, 1.f, 0.45f, 1, 1, -0.2f, 0, 1.6f, 0.02f);
				} else if (s.kind == 7) {
					for (int k = 0; k < 3 && (int)m_parts.size() < 3600; k++) {
						Particle q = {};
						q.p = p + v3(m_rng.F(-1.2f, 1.2f), m_rng.F(6.f, 9.f), m_rng.F(-1.2f, 1.2f));
						q.v = v3(pwx * 0.18f + m_rng.F(-0.3f, 0.3f), m_rng.F(-1.6f, -0.7f), pwz * 0.18f + m_rng.F(-0.2f, 0.2f));
						q.life = q.max = m_rng.F(2.2f, 3.4f);
						q.size = 0.022f; q.size1 = 0.012f;
						q.r = 0.92f; q.g = 0.96f; q.b = 1.f; q.a = 1;
						q.shape = 2; q.add = 1; q.grav = 0.4f; q.drag = 0.3f;
						q.wxHit = 8; q.nHit = 3;
						m_parts.push_back(q);
					}
				} else if (s.kind == 8) {
					Burst(p, 3, 2.8f, 0.035f, 1.3f, 0.86f, 0.72f, 0.42f, 3, 1, 2.4f, 0.1f, 0.25f, 0.02f);
				} else if (s.kind == 9) {
					Burst(p, 1, 0.4f, 0.08f, 2.4f, 0.65f, 0.82f, 1.f, 1, 1, -0.7f, 0, 2.2f, 0.03f);
				} else if (s.kind == 10) {
					for (int k = 0; k < 3 && (int)m_parts.size() < 3600; k++) {
						Particle q = {};
						q.p = p + v3(m_rng.F(-0.35f, 0.35f), m_rng.F(0.05f, 0.55f), m_rng.F(-0.35f, 0.35f));
						q.v = v3(pwx * 0.35f + m_rng.F(-0.12f, 0.12f), m_rng.F(-0.04f, 0.08f) + plift * 0.05f, pwz * 0.35f + m_rng.F(-0.12f, 0.12f));
						q.life = q.max = m_rng.F(2.8f, 4.2f);
						q.size = 0.022f; q.size1 = 0.01f;
						q.r = 0.9f; q.g = 0.94f; q.b = 1.f; q.a = 1;
						q.shape = 1; q.add = 1; q.grav = 0; q.drag = 0.22f;
						q.wxHit = 8; q.nHit = 10;
						m_parts.push_back(q);
					}
				} else if (s.kind == 11) {
					for (int k = 0; k < 2 && (int)m_parts.size() < 3600; k++) {
						Particle q = {};
						q.p = p + v3(m_rng.F(-0.12f, 0.12f), m_rng.F(0.05f, 0.28f), m_rng.F(-0.12f, 0.12f));
						q.v = v3(pwx * 0.2f + m_rng.F(-0.08f, 0.08f), m_rng.F(0.12f, 0.32f), pwz * 0.2f);
						q.life = q.max = m_rng.F(1.8f, 2.6f);
						q.size = 0.02f; q.size1 = 0.01f;
						q.r = 0.7f; q.g = 0.88f; q.b = 1.f; q.a = 1;
						q.shape = 1; q.add = 1; q.grav = -0.4f; q.drag = 0.7f;
						q.wxHit = 8; q.nHit = 11;
						m_parts.push_back(q);
					}
				} else if (s.kind == 12) {
					for (int k = 0; k < 2 && (int)m_parts.size() < 3600; k++) {
						Particle q = {};
						q.p = p + v3(m_rng.F(-0.15f, 0.15f), m_rng.F(0.1f, 0.55f), m_rng.F(-0.15f, 0.15f));
						q.v = v3(pwx * 0.28f + m_rng.F(-0.08f, 0.08f), m_rng.F(0.04f, 0.16f), pwz * 0.28f);
						q.life = q.max = m_rng.F(2.4f, 3.4f);
						q.size = 0.022f; q.size1 = 0.01f;
						q.r = (k & 1) ? 1.f : 0.82f; q.g = (k & 1) ? 0.78f : 0.55f; q.b = (k & 1) ? 0.4f : 1.f; q.a = 1;
						q.shape = 0; q.add = 1; q.grav = -0.08f; q.drag = 0.9f;
						q.wxHit = 8; q.nHit = 12;
						m_parts.push_back(q);
					}
				} else if (s.kind == 13) {
					Burst(p + v3(0, 0.4f, 0), 1, 0.22f, 0.035f, 2.8f, 1.f, 0.82f, 0.42f, 1, 1, -0.08f, 0, 0.6f, 0.85f);
				} else if (s.kind == 14) {
					Burst(p, 3, 1.4f, 0.05f, 1.6f, 0.85f, 0.94f, 1.f, 1, 1, 2.8f, 0.02f, 2.2f, 0.03f);
				}
			}
		}
	}
	{
		float wwx = 0.f, wwz = 0.f, wlift = 0.f;
		int airTh = 0;
		if (m_screen == SC_BATTLE) airTh = Clampi(m_b.map.theme, 0, TH_COUNT - 1);
		else airTh = Clampi(m_showMap.theme, 0, TH_COUNT - 1);
		WindField(m_time, m_camTarget.x, m_camTarget.z, wwx, wwz, wlift, airTh);
		if (sVX > 1e8f) { sVX = m_camTarget.x + 5.f; sVZ = m_camTarget.z; }
		sVX += wwx * dt * 0.18f;
		sVZ += wwz * dt * 0.18f;
		float vdx = sVX - m_camTarget.x, vdz = sVZ - m_camTarget.z;
		float vd = sqrtf(vdx * vdx + vdz * vdz);
		if (vd > 16.f) { sVX = m_camTarget.x + vdx / vd * 7.f; sVZ = m_camTarget.z + vdz / vd * 7.f; }
		BOOL mapView = m_screen == SC_BATTLE || ((m_screen == SC_BASE || m_screen == SC_RESULT) && m_showBuilt);
		if (mapView && m_parts.size() < 3400) {
			static float airAcc = 0.f;
			const Map& map = (m_screen == SC_BATTLE) ? m_b.map : m_showMap;
			int theme = Clampi(map.theme, 0, TH_COUNT - 1);
			int q = QualityNow();
			float step = (q == 4) ? 0.22f : ((q <= 0) ? 0.12f : ((q >= 5) ? 0.05f : 0.08f));
			int ambCap = (q == 4) ? 40 : ((q <= 0) ? 80 : ((q >= 5) ? 180 : 120));
			int amb = 0;
			for (int i = 0; i < m_parts.size(); i++) if (m_parts[i].wxHit == 8) amb++;
			airAcc += dt;
			int ticks = 0;
			while (airAcc >= step && ticks < 2 && amb < ambCap && m_parts.size() < 3400) {
				airAcc -= step;
				ticks++;
				auto born = [&](int mode, V3 p, V3 v, float life, float sz, float r, float g, float b, int shape, float grav, float drag) {
					if ((int)m_parts.size() >= 3600 || amb >= ambCap) return;
					Particle q = {};
					q.p = p; q.v = v; q.life = q.max = life;
					q.size = sz; q.size1 = sz * 0.35f;
					q.r = r; q.g = g; q.b = b; q.a = 1;
					q.shape = shape; q.add = 1; q.grav = grav; q.drag = drag;
					q.wxHit = 8; q.nHit = mode;
					q.add = (mode == 15) ? 0 : 1;
					m_parts.push_back(q);
					amb++;
				};
				V3 camF = m_camTarget - m_eye;
				float camFl = Len(camF);
				camF = camFl > 0.2f ? camF * (1.f / camFl) : v3(0, 0, 1);
				auto onLand = [&](float rad, float y0, float y1, V3& out) -> BOOL {
					for (int t = 0; t < 6; t++) {
						float ang = m_rng.F(0.f, 6.2831853f);
						float rr = m_rng.F(3.2f, rad);
						float x = m_camTarget.x + cosf(ang) * rr;
						float z = m_camTarget.z + sinf(ang) * rr;
						int ix = (int)floorf(x), iz = (int)floorf(z);
						if (!Map::In(ix, iz)) continue;
						const Cell& c = map.At(ix, iz);
						if (c.t == T_VOID || c.t == T_CHASM) continue;
						V3 cand = v3(x, map.TopY(ix, iz) + m_rng.F(y0, y1), z);
						V3 to = cand - m_eye;
						float along = to.x * camF.x + to.y * camF.y + to.z * camF.z;
						float d2 = to.x * to.x + to.y * to.y + to.z * to.z;
						if (d2 < 9.f && along > 0.2f) continue;
						out = cand;
						return TRUE;
					}
					return FALSE;
				};
				V3 at;
				float wx = 0.f, wz = 0.f, lift = 0.f;
				WindField(m_time, m_camTarget.x, m_camTarget.z, wx, wz, lift, theme);
				int sheet = 0;
				if (theme == TH_FOREST) sheet = 2;
				else if (theme == TH_SNOW) sheet = 3;
				else if (theme == TH_DESERT || theme == TH_CANYON) sheet = 4;
				else if (theme == TH_VOLCANO) sheet = 5;
				else if (theme == TH_SWAMP || theme == TH_SEABED) sheet = 6;
				else if (theme == TH_ORBIT) sheet = 10;
				else if (theme == TH_LANTERN) sheet = 13;
				else if (theme == TH_FALLS) sheet = 7;
				int nSpawn = (q == 4) ? 1 : ((q >= 5) ? 4 : 2);
				for (int n = 0; n < nSpawn; n++) {
					if (theme == TH_PLAINS) {
						if ((n & 1) == 0) {
							if (!onLand(16.f, 1.6f, 4.2f, at)) continue;
							born(15, at, v3(wx * 0.28f, m_rng.F(0.08f, 0.28f), wz * 0.28f), 4.6f, 0.09f, 0.96f, 0.9f, 0.58f, 0, 0.12f, 0.55f);
						} else {
							if (!onLand(16.f, 0.08f, 0.32f, at)) continue;
							born(4, at, v3(wx * 0.22f, 0.04f, wz * 0.22f), 2.2f, 0.028f, 0.72f, 0.64f, 0.38f, 3, 4.2f, 0.45f);
						}
					} else if (theme == TH_CANYON || theme == TH_DESERT) {
						if (!onLand(16.f, 0.08f, 0.35f, at)) continue;
						if ((n & 1) == 0) {
							float rr = m_rng.F(0.4f, 2.4f);
							float a = m_rng.F(0.f, 6.2831853f);
							V3 q = v3(sVX + cosf(a) * rr, 0.f, sVZ + sinf(a) * rr);
							int ix = (int)floorf(q.x), iz = (int)floorf(q.z);
							float gy = Map::In(ix, iz) ? map.TopY(ix, iz) : at.y;
							q.y = gy + m_rng.F(0.25f, 3.2f);
							born(1, q, v3(-sinf(a) * 1.4f, 0.4f, cosf(a) * 1.4f), 2.6f, 0.05f, 0.82f, 0.62f, 0.38f, 0, 0.25f, 0.6f);
						} else born(4, at, v3(wx * 0.5f, 0.15f, wz * 0.5f), 1.8f, 0.028f, 0.95f, 0.78f, 0.42f, 3, 6.f, 0.35f);
					} else if (theme == TH_FOREST) {
						if (!onLand(16.f, 0.2f, 0.8f, at)) continue;
						at.y += m_rng.F(2.8f, 6.5f);
						born(2, at, v3(wx * 0.15f, m_rng.F(-0.4f, 0.1f), wz * 0.15f), 3.4f, 0.055f, 0.45f, 0.75f, 0.28f, 2, 1.15f, 1.1f);
					} else if (theme == TH_SNOW) {
						if (!onLand(16.f, 0.2f, 0.8f, at)) continue;
						at.y += m_rng.F(4.2f, 8.5f);
						born(3, at, v3(wx * 0.12f, m_rng.F(-0.6f, -0.2f), wz * 0.12f), 3.6f, 0.022f, 0.96f, 0.98f, 1.f, 2, 0.55f, 1.6f);
					} else if (theme == TH_VOLCANO) {
						if (!onLand(16.f, 0.12f, 0.4f, at)) continue;
						born(5, at, v3(m_rng.F(-0.3f, 0.3f), m_rng.F(1.2f, 2.6f), m_rng.F(-0.3f, 0.3f)), 2.2f, 0.022f, 1.f, m_rng.F(0.25f, 0.5f), 0.05f, 5, -1.5f, 0.45f);
					} else if (theme == TH_SWAMP) {
						if (!onLand(16.f, 0.04f, 0.18f, at)) continue;
						born(6, at, v3(m_rng.F(-0.15f, 0.15f), 0.35f, m_rng.F(-0.1f, 0.1f)), 2.4f, 0.038f, 0.7f, 0.95f, 0.55f, 1, -0.9f, 0.5f);
					} else if (theme == TH_RUINS) {
						if (!onLand(16.f, 1.0f, 3.4f, at)) continue;
						born(0, at, v3(wx * 0.1f, 0.15f, wz * 0.1f), 3.2f, 0.032f, 0.7f, 0.75f, 0.85f, 1, 0.2f, 1.2f);
					} else if (theme == TH_SEABED) {
						if (!onLand(16.f, 0.25f, 2.2f, at)) continue;
						born((n & 1) ? 8 : 6, at, v3(m_rng.F(-0.1f, 0.15f), 0.25f, m_rng.F(-0.1f, 0.1f)), 3.4f, 0.022f, 0.55f, 0.82f, 1.f, 1, (n & 1) ? 0.15f : -0.7f, 0.7f);
					} else if (theme == TH_FALLS) {
						if ((n & 1) == 0) {
							if (!onLand(16.f, 0.08f, 0.28f, at)) continue;
							born(7, at, v3(wx * 0.22f, 0.12f, wz * 0.22f), 2.6f, 0.11f, 0.9f, 0.96f, 1.f, 0, 0.04f, 0.7f);
						} else {
							if (!onLand(16.f, 2.4f, 6.5f, at)) continue;
							born(9, at, v3(wx * 0.18f, m_rng.F(-1.6f, -0.5f), wz * 0.18f), 2.1f, 0.055f, 0.92f, 0.97f, 1.f, 1, 4.6f, 0.32f);
						}
					} else if (theme == TH_ORBIT) {
						if (!onLand(16.f, 0.4f, 1.8f, at)) continue;
						born(10, at, v3(m_rng.F(-0.4f, 0.5f), m_rng.F(-0.1f, 0.15f), m_rng.F(-0.3f, 0.3f)), 4.6f, 0.024f, 0.95f, 0.94f, 1.f, 1, 0.f, 0.22f);
					} else if (theme == TH_STATION) {
						if (!onLand(16.f, 0.5f, 2.2f, at)) continue;
						born(11, at, v3(m_rng.F(-0.1f, 0.2f), 0.4f, m_rng.F(-0.1f, 0.1f)), 2.2f, 0.022f, 0.75f, 0.9f, 1.f, 1, -0.55f, 0.6f);
					} else if (theme == TH_CITADEL) {
						if (!onLand(16.f, 0.6f, 1.8f, at)) continue;
						if ((n & 1) == 0)
							born(12, at, v3(wx * 0.35f, 0.08f, wz * 0.35f), 3.2f, 0.026f, 0.82f, 0.55f, 1.f, 0, 0.12f, 0.45f);
						else
							born(13, at, v3(m_rng.F(-0.15f, 0.15f), 0.22f, m_rng.F(-0.12f, 0.12f)), 2.8f, 0.024f, 1.f, 0.78f, 0.38f, 1, -0.1f, 0.7f);
					} else if (theme == TH_LANTERN) {
						if (!onLand(16.f, 0.9f, 2.6f, at)) continue;
						born(13, at, v3(m_rng.F(-0.2f, 0.2f), 0.25f, m_rng.F(-0.15f, 0.15f)), 2.8f, 0.035f, 1.f, 0.82f, 0.45f, 1, -0.15f, 0.8f);
					} else {
						if (!onLand(16.f, 1.8f, 4.f, at)) continue;
						born(0, at, v3(wx * 0.2f, 0.2f, wz * 0.2f), 2.4f, 0.038f, 0.9f, 0.92f, 1.f, 1, 0.3f, 0.8f);
					}
				}
				float burst = TacBurst(m_time);
				static int gustLatched = 0;
				if (burst > 0.55f && !gustLatched) {
					gustLatched = 1;
					int sheetN = (q == 4) ? 6 : 16;
					for (int i = 0; i < sheetN; i++) {
						V3 p;
						if (theme == TH_PLAINS) {
							if (!onLand(18.f, 1.2f, 3.4f, p)) continue;
							born(15, p, v3(wx * 0.38f, 0.18f + burst * 0.22f, wz * 0.38f), 3.8f, 0.12f, 0.96f, 0.9f, 0.58f, 0, 0.1f, 0.5f);
						} else {
							if (!onLand(18.f, 0.08f, 0.9f, p)) continue;
							born(sheet, p, v3(wx * 0.25f, 0.3f + burst, wz * 0.25f), 2.2f, sheet == 4 ? 0.028f : 0.045f, 0.9f, 0.88f, 0.7f, sheet == 2 ? 2 : 1, sheet == 4 ? 5.f : 0.6f, 0.7f);
						}
					}
				} else if (burst < 0.18f) gustLatched = 0;
				{
					int cx = (int)floorf(m_camTarget.x), cz = (int)floorf(m_camTarget.z);
					int extra = 0;
					for (int dz = -8; dz <= 8 && extra < 6; dz += 4) for (int dx = -8; dx <= 8 && extra < 6; dx += 4) {
						int x = cx + dx, z = cz + dz;
						if (!Map::In(x, z)) continue;
						int t = map.At(x, z).t;
						float y = map.StandY(x, z);
						V3 ep = v3(x + m_rng.F(0.2f, 0.8f), y + 0.16f, z + m_rng.F(0.2f, 0.8f));
						V3 to = ep - m_eye;
						float along = to.x * camF.x + to.y * camF.y + to.z * camF.z;
						if (along > 0.2f && (to.x * to.x + to.y * to.y + to.z * to.z) < 9.f) continue;
						BOOL abyss = FALSE;
						static const int edx[4] = { 1, -1, 0, 0 }, edz[4] = { 0, 0, 1, -1 };
						for (int k = 0; k < 4; k++) {
							int nx = x + edx[k], nz = z + edz[k];
							if (!Map::In(nx, nz) || map.At(nx, nz).t == T_VOID || map.At(nx, nz).t == T_CHASM) abyss = TRUE;
						}
						if (t == T_WATER && theme != TH_SEABED && (abyss || theme == TH_FALLS || theme == TH_SWAMP)) {
							born(theme == TH_FALLS || abyss ? 9 : 6, ep + v3(0, abyss ? m_rng.F(1.2f, 4.5f) : 0, 0), v3(wx * (abyss ? 0.22f : 0.05f), abyss || theme == TH_FALLS ? m_rng.F(-1.8f, -0.4f) : 0.4f, wz * (abyss ? 0.22f : 0.05f)), 1.8f, abyss ? 0.07f : 0.04f, 0.88f, 0.95f, 1.f, 1, abyss || theme == TH_FALLS ? 4.2f : -0.8f, 0.35f);
							extra++;
						} else if (t == T_LAVA && (abyss || theme == TH_VOLCANO)) {
							born(5, ep + v3(0, abyss ? 0.2f : 0.08f, 0), v3(m_rng.F(-0.12f, 0.12f), abyss ? m_rng.F(-0.55f, -0.12f) : m_rng.F(1.4f, 2.8f), m_rng.F(-0.12f, 0.12f)), 2.4f, abyss ? 0.06f : 0.038f, 1.f, abyss ? 0.45f : 0.35f, 0.05f, 5, abyss ? 1.1f : -1.6f, 0.35f);
							extra++;
						} else if (theme == TH_LANTERN && (t == T_ROAD || t == T_RUINS || t == T_PLAIN)) {
							ep.y += m_rng.F(0.8f, 1.8f);
							born(13, ep, v3(m_rng.F(-0.15f, 0.15f), 0.2f, m_rng.F(-0.15f, 0.15f)), 2.6f, 0.032f, 1.f, 0.8f, 0.4f, 1, -0.12f, 0.75f);
							extra++;
						} else if (theme == TH_FOREST && t == T_FOREST) {
							ep.y += m_rng.F(3.f, 6.f);
							born(2, ep, v3(wx * 0.1f, m_rng.F(-0.3f, 0.05f), wz * 0.1f), 3.f, 0.05f, 0.5f, 0.78f, 0.32f, 2, 1.1f, 1.f);
							extra++;
						} else if (theme == TH_CITADEL && (t == T_RUINS || t == T_ROAD || t == T_PLAIN)) {
							ep.y += m_rng.F(0.6f, 1.6f);
							born(12, ep, v3(wx * 0.2f, 0.12f, wz * 0.2f), 3.f, 0.024f, 0.85f, 0.58f, 1.f, 0, 0.1f, 0.5f);
							extra++;
						}
					}
				}
			}
		}
	}
	if (m_screen == SC_BATTLE) {
		int wx = TacWx(m_b.weather);
		float rate = 0.f;
		if (wx == WX_RAIN) rate = 70.f;
		else if (wx == WX_STORM) rate = 120.f;
		else if (wx == WX_SNOW) rate = 36.f;
		else if (wx == WX_ASH) rate = 32.f;
		else if (wx == WX_HAIL) rate = 42.f;
		else if (wx == WX_SAND) rate = 40.f;
		else if (wx == WX_PETAL) rate = 22.f;
		else if (wx == WX_FOG) rate = 16.f;
		else if (wx == WX_HEAT) rate = 18.f;
		if (QualityNow() <= 0) rate *= 0.45f;
		else if (QualityNow() == 4) rate *= 0.3f;
		m_wxAcc += dt * rate;
		V3 eye = m_eye;
		V3 fwd = m_camTarget - eye;
		float fl = Len(fwd);
		fwd = fl > 0.2f ? fwd * (1.f / fl) : v3(0, 0, 1);
		V3 side = Cross(v3(0, 1, 0), fwd);
		if (Len(side) < 0.2f) side = v3(1, 0, 0);
		else side = Norm(side);
		float windAng = 0.6f + sinf(m_time * 0.13f) * 0.4f;
		V3 wind = v3(cosf(windAng), 0, sinf(windAng));
		int guard = 0;
		while (m_wxAcc >= 1.f && guard < 8 && m_parts.size() < 3000) {
			m_wxAcc -= 1.f;
			guard++;
			float along = m_rng.F(3.4f, 16.f);
			float lat = m_rng.F(-9.f, 9.f);
			float up = m_rng.F(-1.5f, 6.5f);
			V3 p = eye + fwd * along + side * lat + v3(0, up, 0);
			int ix = (int)floorf(p.x), iz = (int)floorf(p.z);
			float gy = 0.f;
			if (Map::In(ix, iz)) gy = m_b.map.StandY(ix, iz);
			if (p.y < gy + 1.4f) p.y = gy + m_rng.F(3.f, 8.5f);
			Particle q = {};
			q.p = p; q.a = 1; q.add = 1; q.nHit = 0;
			if (wx == WX_RAIN || wx == WX_STORM) {
				float sp = (wx == WX_STORM) ? m_rng.F(14.f, 20.f) : m_rng.F(10.f, 15.f);
				float w = (wx == WX_STORM) ? 5.2f : 1.6f;
				q.v = wind * w + v3(m_rng.F(-0.4f, 0.4f), -sp, m_rng.F(-0.4f, 0.4f));
				q.life = q.max = 1.6f;
				q.size = 0.055f; q.size1 = 0.04f;
				q.r = 0.72f; q.g = 0.84f; q.b = 1.f;
				q.shape = 0; q.grav = 16.f; q.drag = 0.15f; q.stretch = 0.055f;
				q.wxHit = (wx == WX_STORM) ? 2 : 1;
			} else if (wx == WX_SNOW) {
				q.v = wind * 0.7f + v3(m_rng.F(-0.3f, 0.3f), m_rng.F(-1.8f, -0.8f), m_rng.F(-0.3f, 0.3f));
				q.life = q.max = 4.f;
				q.size = 0.07f; q.size1 = 0.05f;
				q.r = 0.96f; q.g = 0.98f; q.b = 1.f;
				q.shape = 2; q.grav = 0.8f; q.drag = 0.35f; q.stretch = 0;
				q.wxHit = 3;
			} else if (wx == WX_ASH) {
				q.v = wind * 0.5f + v3(m_rng.F(-0.25f, 0.25f), m_rng.F(-2.6f, -1.2f), m_rng.F(-0.25f, 0.25f));
				q.life = q.max = 3.2f;
				q.size = 0.05f; q.size1 = 0.03f;
				q.r = 0.85f; q.g = 0.4f; q.b = 0.18f;
				q.shape = 0; q.grav = 1.6f; q.drag = 0.25f; q.stretch = 0.02f;
				q.wxHit = 4;
			} else if (wx == WX_HAIL) {
				q.v = wind * 0.8f + v3(m_rng.F(-0.2f, 0.2f), m_rng.F(-18.f, -13.f), m_rng.F(-0.2f, 0.2f));
				q.life = q.max = 1.4f;
				q.size = 0.06f; q.size1 = 0.05f;
				q.r = 0.9f; q.g = 0.95f; q.b = 1.f;
				q.shape = 7; q.grav = 22.f; q.drag = 0.05f; q.stretch = 0.02f;
				q.wxHit = 5;
			} else if (wx == WX_SAND) {
				q.v = wind * m_rng.F(5.f, 8.f) + v3(0, m_rng.F(-2.2f, -0.6f), 0);
				q.life = q.max = 1.8f;
				q.size = 0.04f; q.size1 = 0.025f;
				q.r = 0.92f; q.g = 0.74f; q.b = 0.38f;
				q.shape = 0; q.grav = 3.f; q.drag = 0.2f; q.stretch = 0.04f;
				q.wxHit = 6;
			} else if (wx == WX_PETAL) {
				q.v = wind * 0.9f + v3(m_rng.F(-0.3f, 0.3f), m_rng.F(-1.1f, -0.4f), m_rng.F(-0.3f, 0.3f));
				q.life = q.max = 4.5f;
				q.size = 0.08f; q.size1 = 0.05f;
				q.r = 1.f; q.g = 0.72f; q.b = 0.8f;
				q.shape = 2; q.grav = 0.45f; q.drag = 0.3f; q.stretch = 0.01f;
				q.wxHit = 7;
			} else if (wx == WX_FOG) {
				q.v = wind * 0.35f + v3(m_rng.F(-0.2f, 0.2f), m_rng.F(-0.15f, 0.2f), m_rng.F(-0.2f, 0.2f));
				q.life = q.max = 3.5f;
				q.size = 0.16f; q.size1 = 0.24f;
				q.r = 0.78f; q.g = 0.82f; q.b = 0.86f; q.a = 0.22f;
				q.shape = 0; q.grav = 0; q.drag = 0.15f; q.stretch = 0; q.wxHit = 0;
			} else {
				q.p.y = gy + m_rng.F(0.1f, 0.6f);
				q.v = v3(m_rng.F(-0.2f, 0.2f), m_rng.F(0.8f, 1.8f), m_rng.F(-0.2f, 0.2f));
				q.life = q.max = 1.6f;
				q.size = 0.06f; q.size1 = 0.1f;
				q.r = 1.f; q.g = 0.7f; q.b = 0.35f; q.a = 0.45f;
				q.shape = 0; q.grav = -0.4f; q.drag = 0.2f; q.stretch = 0.02f; q.wxHit = 0;
			}
			m_parts.push_back(q);
		}
		if (wx == WX_SAND || wx == WX_HAIL) {
			int row = ((int)(m_time * 8.f) & 63);
			if (row < 0) row = 0;
			for (int x = 0; x < MAPN; x++) {
				int i = row * MAPN + x;
				int d = m_cover[i];
				if (d <= 0) continue;
				if (wx == WX_HAIL) m_cover[i] = (unsigned char)(d * 5 / 6);
				else if ((x & 3) == (row & 3)) {
					int nx = x + (wind.x >= 0.f ? 1 : -1);
					int nz = row + (wind.z >= 0.f ? 1 : -1);
					if (!Map::In(nx, nz)) { m_cover[i] = (unsigned char)(d * 3 / 4); continue; }
					int t = m_b.map.At(nx, nz).t;
					if (t == T_VOID || t == T_CHASM) { m_cover[i] = (unsigned char)(d * 3 / 4); continue; }
					int mv = max(1, d / 8);
					m_cover[i] = (unsigned char)(d - mv);
					int ni = nz * MAPN + nx;
					int nd = m_cover[ni] + mv;
					if (nd > 180) nd = 180;
					m_cover[ni] = (unsigned char)nd;
				}
			}
		}
	}
	auto soak = [&](int x, int z, int add) {
		if (!Map::In(x, z) || add <= 0) return;
		const Cell& c0 = m_b.map.At(x, z);
		if (c0.t == T_VOID || c0.t == T_CHASM || c0.t == T_LAVA || c0.t == T_DOOR) return;
		int i = z * MAPN + x;
		int d = m_cover[i] + add;
		if (d > 255) d = 255;
		m_cover[i] = (unsigned char)d;
		int wx = TacWx(m_b.weather);
		if ((wx == WX_RAIN || wx == WX_STORM) && d >= 170 && !c0.gimm) {
			int t = c0.t;
			if (t == T_PLAIN || t == T_FOREST || t == T_HILL || t == T_SAND || t == T_SNOW || t == T_SWAMP || t == T_ROAD || t == T_RUINS) {
				m_b.map.At(x, z).t = T_WATER;
				m_cover[i] = 0;
				m_wxMesh = 1;
			}
		}
	};
	auto splash = [&](V3 at, int n, float up, float r, float g, float b) {
		for (int k = 0; k < n && m_parts.size() < 3800; k++) {
			float ang = m_rng.F(0.f, 6.2831853f);
			float sp = m_rng.F(0.8f, 2.6f);
			Particle s = {};
			s.p = at;
			s.v = v3(cosf(ang) * sp, m_rng.F(up * 0.5f, up), sinf(ang) * sp);
			s.life = s.max = m_rng.F(0.16f, 0.32f);
			s.size = 0.045f; s.size1 = 0.015f;
			s.r = r; s.g = g; s.b = b; s.a = 1;
			s.shape = 0; s.add = 1; s.grav = 18.f; s.drag = 0.3f; s.stretch = 0.03f;
			s.wxHit = 0;
			m_parts.push_back(s);
		}
	};
	for (size_t i = 0; i < m_parts.size();) {
		Particle& p = m_parts[i];
		p.life -= dt;
		if (p.life <= 0) { m_parts[i] = m_parts.back(); m_parts.pop_back(); continue; }
		if (p.wxHit == 7) {
			p.v.x += cosf(m_time * 1.4f + p.p.z) * dt * 1.1f;
			p.v.z += sinf(m_time * 1.1f + p.p.x) * dt * 0.8f;
		}
		if (p.wxHit == 8) {
			float wx = 0.f, wz = 0.f, lift = 0.f;
			int airTh = 0;
			if (m_screen == SC_BATTLE) airTh = Clampi(m_b.map.theme, 0, TH_COUNT - 1);
			else airTh = Clampi(m_showMap.theme, 0, TH_COUNT - 1);
			WindField(m_time, p.p.x, p.p.z, wx, wz, lift, airTh);
			float follow = dt * 1.8f;
			p.v.x += (wx - p.v.x) * follow;
			p.v.z += (wz - p.v.z) * follow;
			int mode = p.nHit;
			if (mode == 0) {
				p.v.y += (lift * 0.55f - p.v.y * 0.4f) * dt;
				p.v.x += sinf(m_time * 3.1f + p.p.z * 2.f) * dt * 0.55f;
				p.v.z += cosf(m_time * 2.4f + p.p.x * 1.7f) * dt * 0.45f;
			} else if (mode == 1) {
				float dx = p.p.x - sVX, dz = p.p.z - sVZ;
				float rad = sqrtf(dx * dx + dz * dz) + 0.08f;
				float spin = (3.8f + lift) / rad;
				p.v.x += (-dz * spin - dx * 0.35f) * dt;
				p.v.z += (dx * spin - dz * 0.35f) * dt;
				p.v.y += (1.1f + lift * 0.35f) * dt;
			} else if (mode == 2) {
				p.v.y += sinf(m_time * 9.f + p.p.x * 3.1f) * dt * 2.6f;
				p.v.x += cosf(m_time * 6.f + p.p.z * 2.2f) * dt * 1.4f;
				p.v.z += sinf(m_time * 4.4f + p.p.y) * dt * 0.8f;
			} else if (mode == 3) {
				p.v.x += sinf(m_time * 1.7f + p.p.z) * dt * 0.6f;
				p.v.z += cosf(m_time * 1.3f + p.p.x) * dt * 0.45f;
				p.v.y += sinf(m_time * 2.2f + p.p.x * 0.4f) * dt * 0.2f;
			} else if (mode == 4) {
				p.v.x += wx * dt * 0.55f;
				p.v.z += wz * dt * 0.55f;
			} else if (mode == 5) {
				p.grav += dt * 1.8f;
				if (p.grav > 2.8f) p.grav = 2.8f;
				p.v.x += sinf(m_time * 5.f + p.p.y) * dt * 0.35f;
			} else if (mode == 6) {
				p.v.x += sinf(m_time * 2.2f + p.p.y * 1.5f) * dt * 0.8f;
				p.v.z += cosf(m_time * 1.8f + p.p.y) * dt * 0.5f;
			} else if (mode == 7) {
				p.v.x += wx * dt * 0.95f;
				p.v.z += wz * dt * 0.8f;
				p.v.y += sinf(m_time * 3.1f + p.p.x) * dt * 0.28f;
			} else if (mode == 8) {
				p.v.x += sinf(m_time * 1.1f + p.p.y * 2.f) * dt * 0.35f;
				p.v.y += sinf(m_time * 0.9f + p.p.x) * dt * 0.22f;
			} else if (mode == 9) {
				p.v.x += wx * dt * 1.05f + sinf(m_time * 8.f + p.p.z) * dt * 0.4f;
				p.v.z += wz * dt * 0.9f;
				p.v.y += lift * dt * 0.15f;
			} else if (mode == 16) {
				p.v.x += wx * dt * 1.35f;
				p.v.z += wz * dt * 1.15f;
				p.v.y += lift * dt * 0.45f;
			} else if (mode == 10) {
				p.grav = 0.f;
				p.v.y += sinf(m_time * 0.7f + p.p.x) * dt * 0.25f;
				p.v.x += cosf(m_time * 0.5f + p.p.z) * dt * 0.12f;
			} else if (mode == 11) {
				p.v.y += 0.12f * dt;
				p.v.x += sinf(m_time * 3.f + p.p.z) * dt * 0.22f;
			} else if (mode == 12) {
				p.v.y += 0.08f * dt;
				p.v.x += cosf(m_time * 0.8f + p.p.z) * dt * 0.25f;
			} else if (mode == 13) {
				p.v.x += cosf(m_time * 2.4f + p.p.z) * dt * 0.9f;
				p.v.z += sinf(m_time * 2.1f + p.p.x) * dt * 0.9f;
				p.v.y += sinf(m_time * 3.3f + p.p.x + p.p.z) * dt * 0.35f;
			} else if (mode == 15) {
				p.v.x += (wx * 0.55f - p.v.x) * dt * 0.7f;
				p.v.z += (wz * 0.55f - p.v.z) * dt * 0.7f;
				p.v.y += (0.16f + lift * 0.12f - p.v.y * 0.35f) * dt;
				p.v.x += sinf(m_time * 0.55f + p.p.z * 0.28f) * dt * 0.16f;
			}
		} else {
			float gust = TacBurst(m_time);
			if (gust > 0.02f) {
				float ang = m_time * 0.23f;
				p.v.x += cosf(ang) * gust * 7.5f * dt;
				p.v.z += sinf(ang) * gust * 6.2f * dt;
			}
		}
		V3 prev = p.p;
		p.v.y -= p.grav * dt;
		p.v = p.v * expf(-p.drag * dt);
		p.p = p.p + p.v * dt;
		if (p.wxHit == 8 && (m_screen == SC_BATTLE || m_showBuilt)) {
			const Map& gm = (m_screen == SC_BATTLE) ? m_b.map : m_showMap;
			int ix = (int)floorf(p.p.x), iz = (int)floorf(p.p.z);
			if (Map::In(ix, iz)) {
				const Cell& c = gm.At(ix, iz);
				if (c.t != T_VOID && c.t != T_CHASM && p.nHit != 10) {
					float gy = gm.TopY(ix, iz);
					if (p.nHit == 7) {
						float want = gy + 0.32f;
						p.p.y += (want - p.p.y) * min(1.f, dt * 3.f);
						p.v.y *= 0.4f;
					} else if (p.nHit == 15) {
						if (p.p.y < gy + 0.18f) p.v.y = max(p.v.y, 0.12f);
					} else if (p.nHit != 6 && p.nHit != 11 && prev.y > gy + 0.02f && p.p.y <= gy + 0.03f) {
						p.p.y = gy + 0.04f;
						float burst = TacBurst(m_time);
						if (p.nHit == 4) p.v.y = 0.85f + burst * 2.2f;
						else if (p.nHit == 1) p.v.y = 0.7f + burst * 1.4f;
						else if (p.nHit == 5 || p.nHit == 9 || p.nHit == 16) p.life = 0.04f;
						else {
							p.v.y = 0.f;
							p.grav = 0.f;
							p.life = min(p.life, p.nHit == 3 ? 0.7f : 0.4f);
						}
					}
				}
			}
		}
		if (p.wxHit > 0 && p.wxHit < 8 && m_screen == SC_BATTLE && p.v.y < 0.f) {
			int ix = (int)floorf(p.p.x), iz = (int)floorf(p.p.z);
			if (Map::In(ix, iz)) {
				const Cell& c = m_b.map.At(ix, iz);
				if (c.t != T_VOID && c.t != T_CHASM) {
					int lay = c.nLay >= 2 ? (int)c.nLay - 1 : 0;
					if (lay > 2) lay = 2;
					float gy = m_b.map.WalkY(ix, iz, lay);
					if (prev.y > gy + 0.02f && p.p.y <= gy + 0.02f) {
						p.p.y = gy + 0.03f;
						int kind = p.wxHit;
						if (kind == 1 || kind == 2) {
							soak(ix, iz, kind == 2 ? 5 : 3);
							splash(p.p, kind == 2 ? 6 : 4, kind == 2 ? 2.4f : 1.6f, 0.75f, 0.86f, 1.f);
							m_parts[i] = m_parts.back(); m_parts.pop_back(); continue;
						} else if (kind == 3) {
							soak(ix, iz, 2);
							p.v = v3(0, 0, 0); p.grav = 0; p.stretch = 0;
							p.life = min(p.life, 0.35f);
							p.wxHit = 0;
						} else if (kind == 4) {
							soak(ix, iz, 2);
							splash(p.p, 2, 0.9f, 0.7f, 0.35f, 0.15f);
							m_parts[i] = m_parts.back(); m_parts.pop_back(); continue;
						} else if (kind == 5) {
							soak(ix, iz, 3);
							if (p.nHit < 2) {
								p.nHit++;
								p.v.y = -p.v.y * 0.42f;
								p.v.x *= 0.7f; p.v.z *= 0.7f;
								splash(p.p, 2, 1.1f, 0.9f, 0.95f, 1.f);
							} else {
								splash(p.p, 3, 0.8f, 0.9f, 0.95f, 1.f);
								m_parts[i] = m_parts.back(); m_parts.pop_back(); continue;
							}
						} else if (kind == 6) {
							soak(ix, iz, 2);
							if (p.nHit < 1) {
								p.nHit++;
								p.v.y = 0.9f;
								p.v.x *= 0.8f; p.v.z *= 0.8f;
							} else {
								p.v.y = 0; p.grav = 0; p.p.y = gy + 0.05f;
								p.wxHit = 0; p.life = min(p.life, 0.6f);
							}
						} else if (kind == 7) {
							soak(ix, iz, 1);
							if (p.nHit < 1) {
								p.nHit++;
								p.v.y = 0.55f;
								p.v.x *= 0.5f; p.v.z *= 0.5f;
							} else {
								p.v = v3(0, 0, 0); p.grav = 0; p.wxHit = 0;
								p.life = min(p.life, 0.8f);
							}
						}
					}
				}
			}
		}
		++i;
	}
	if (m_wxMesh && m_screen == SC_BATTLE) {
		m_wxMeshWait -= dt;
		if (m_wxMeshWait <= 0.f) {
			m_wxMesh = 0;
			m_wxMeshWait = 0.45f;
			m_r.BuildTerrain(m_b.map);
			m_r.BuildMinimap(m_b.map);
		}
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
	stage = Clampi(stage, 1, TAC_STAGE_ABS);
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
	int st = Clampi(m_save.stageSel, 1, TAC_STAGE_ABS);
	RollMapSeed(st, FALSE);
	if (!m_showBuilt || m_showStage != st || m_showTheme != m_themeSel) {
		GenerateMap(m_showMap, st, m_mapSeed, m_themeSel);
		m_showTheme = m_themeSel;
		m_r.BuildTerrain(m_showMap);
		m_showBuilt = TRUE;
		m_showStage = st;
		if (m_loadLive && m_loadB > m_loadA) NoteLoad(m_loadA + (m_loadB - m_loadA) / 4);
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

void Game::DemoBegin()
{
	int n = 0;
	int cap = PartyCap();
	for (int i = 0; i < cap; i++) {
		int r = m_save.party[i];
		if (r >= 0 && r < MAX_ROSTER && m_save.roster[r].used) n++;
	}
	if (!n) { m_idleT = 0; return; }
	m_demoSave = m_save;
	int hi = StagePlayMax(m_save.stageCleared);
	m_demoStage = m_rng.Range(1, hi);
	m_demo = 1;
	m_demoT = 0;
	m_demoAct = 0;
	m_demoTick = 0;
	m_demoStep = 0;
	m_demoPh = -1;
	m_click = FALSE;
}

void Game::DemoLeave()
{
	if (m_demo == 0 || m_demo == 3) return;
	m_demo = 3;
	m_demoT = 0;
}

void Game::TickDemo(float dt)
{
	if (m_demo == 1) {
		m_demoT += dt;
		// 幕が閉じ切ってから戦闘を出す。BGM は画面が戦闘になった時点で地形曲へ替わる。
		if (m_screen != SC_BATTLE && m_demoT >= 0.62f) {
			StartBattle(m_demoStage);
			if (m_screen != SC_BATTLE) {
				m_save = m_demoSave;
				m_demo = 0;
				m_idleT = 0;
				return;
			}
			m_navMode = TRUE;
		}
		if (m_screen == SC_BATTLE && m_demoT >= 1.25f) {
			m_demo = 2;
			m_demoT = 0;
			m_demoAct = 0;
			m_demoPh = -1;
			m_navMode = TRUE;
		}
	} else if (m_demo == 3) {
		m_demoT += dt;
		if (m_screen == SC_BATTLE && m_demoT >= 0.55f) {
			m_save = m_demoSave;
			m_click = FALSE;
			m_lDown = m_rDown = m_mDown = FALSE;
			EnterBase();
			m_idleT = 0;
		}
		if (m_demoT >= 1.20f) {
			m_demo = 0;
			m_idleT = 0;
		}
	}
}

void Game::DrawDemoCurtain()
{
	if (m_demo != 1 && m_demo != 3) return;
	float cover;
	BOOL out = m_demo == 3;
	if (!out) {
		float t = m_demoT;
		cover = t < 0.62f ? Smooth01(t / 0.62f) : Smooth01((1.25f - t) / 0.63f);
	} else {
		float t = m_demoT;
		cover = t < 0.55f ? Smooth01(t / 0.55f) : Smooth01((1.20f - t) / 0.65f);
	}
	if (cover <= 0.001f) return;
	float W = (float)m_w, H = (float)m_h;
	if (cover > 0.985f) {
		m_r.HudRect(0, 0, W, H, out ? RGBA8(28, 10, 8) : RGBA8(8, 12, 28));
		return;
	}
	// 入りは縦の刃が中央から閉じる。出は横の板が中央から閉じる。同じフェードにはしない。
	if (!out) {
		const int n = 18;
		float cw = W / (float)n;
		for (int i = 0; i < n; i++) {
			float u = (i + 0.5f) / (float)n;
			float lag = fabsf(u - 0.5f) * 0.55f;
			float k = Smooth01((cover - lag) / (1.f - lag));
			if (k <= 0.f) continue;
			float bh = H * 0.5f * k;
			float x = i * cw;
			m_r.HudRectGrad(x, 0, cw + 1.f, bh, RGBA8(6, 10, 24), RGBA8(36, 44, 82));
			m_r.HudRectGrad(x, H - bh, cw + 1.f, bh + 1.f, RGBA8(36, 44, 82), RGBA8(6, 10, 24));
			m_r.HudRect(x, bh - 2.f, cw + 1.f, 3.f, RGBA8(232, 196, 110));
			m_r.HudRect(x, H - bh - 1.f, cw + 1.f, 3.f, RGBA8(232, 196, 110));
		}
	} else {
		const int n = 12;
		float rh = H / (float)n;
		for (int i = 0; i < n; i++) {
			float u = (i + 0.5f) / (float)n;
			float lag = fabsf(u - 0.5f) * 0.45f;
			float k = Smooth01((cover - lag) / (1.f - lag));
			if (k <= 0.f) continue;
			float bw = W * 0.5f * k;
			float y = i * rh;
			m_r.HudRectGrad(0, y, bw, rh + 1.f, RGBA8(72, 28, 14), RGBA8(18, 8, 8));
			m_r.HudRectGrad(W - bw, y, bw + 1.f, rh + 1.f, RGBA8(18, 8, 8), RGBA8(72, 28, 14));
			m_r.HudRect(bw - 2.f, y, 3.f, rh + 1.f, RGBA8(220, 140, 70));
			m_r.HudRect(W - bw - 1.f, y, 3.f, rh + 1.f, RGBA8(220, 140, 70));
		}
	}
}

void Game::GoHomeFromBattle()
{
	if (m_screen != SC_BATTLE) return;
	QueueSuspend(2);
	Persist();
	m_menuConfirm = 0;
	EnterBase();
}

void Game::EnterBase()
{
	m_r.LoadBattleCreatures(NULL, 0);
	m_importBind = 0;
	m_textFocus = 0;
	m_imeComp[0] = 0;
	m_screen = SC_BASE;
	m_mapLook = 0;
	m_homeUi = 1;
	m_shop = 0;
	m_shopFit = 0;
	m_shopHold = 0;
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
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
	int young = PromoteLevel(1);
	int ycap = RosterCap();
	int ydirty = 0;
	for (int i = 0; i < ycap; i++) {
		CharData& yc = m_save.roster[i];
		if (yc.used && yc.jobDone == 0 && yc.lv < young) { yc.jobDone = 1; ydirty = 1; }
	}
	if (ydirty) Persist();
	if (NextJobPending(0) < 0) m_jobRedo = 0;
	else if (m_jobRedo != 1) m_jobRedo = 1;
}

void Game::WarmHomeAvatars()
{
	if (m_avaLoadUnits < 1) return;
	int who[64];
	int model[64], ava[64], cup[64];
	int n = 0;
	for (int ri = 0; ri < MAX_ROSTER && n < 64; ri++) {
		if (!m_save.roster[ri].used) continue;
		Actor tmp;
		ActorFromChar(tmp, m_lib, m_r, m_save.roster[ri]);
		if (tmp.avatar <= 0 || tmp.model < 0) continue;
		int seen = 0;
		for (int k = 0; k < n; k++) if (model[k] == tmp.model && ava[k] == tmp.avatar && cup[k] == tmp.bust) seen = 1;
		if (seen) continue;
		who[n] = ri;
		model[n] = tmp.model;
		ava[n] = tmp.avatar;
		cup[n] = tmp.bust;
		n++;
	}
	int pend = n > 0 ? m_r.CountPendingAvatars(n, model, ava, cup) : 0;
	OpenLoadSlice(m_avaLoadUnits);
	if (pend > 0) {
		DrawParams dp = {};
		M4 ident = MIdent();
		auto drawOne = [&](int i) {
			Actor tmp;
			ActorFromChar(tmp, m_lib, m_r, m_save.roster[who[i]]);
			if (tmp.avatar <= 0 || tmp.model < 0) return;
			if (!tmp.poseValid || tmp.poseFrame != m_r.m_frame)
				ActorPose(tmp, m_lib, m_r);
			m_r.DrawAvatar(tmp.model, tmp.avatar, tmp.bust, tmp.pose, ident, dp, NULL);
		};
		// 1回目は予約だけ。生成はフレームが進んでから。ここで済ませないと、ホームのあと別画面になる。
		for (int i = 0; i < n; i++) drawOne(i);
		for (int pass = 0; pass < 4; pass++) {
			pend = m_r.CountPendingAvatars(n, model, ava, cup);
			if (pend <= 0) break;
			m_r.m_frame++;
			m_r.BeginAvaBatch(pend);
			m_r.SetAvaBuildOnly(TRUE);
			for (int i = 0; i < n; i++) drawOne(i);
			m_r.SetAvaBuildOnly(FALSE);
		}
		m_r.m_avaLoading = m_r.CountPendingAvatars(n, model, ava, cup) > 0 ? 1 : 0;
	}
	CloseLoadSlice();
}

void Game::UpdateBase(float dt)
{
	if (m_screen == SC_BASE && m_mapLook) {
		float d = m_camYawGoal - m_camYaw;
		while (d > PI) d -= PI * 2.f;
		while (d < -PI) d += PI * 2.f;
		m_camYaw += d * Clampf(dt * 6.f, 0.f, 1.f);
	} else m_showYaw += dt * .08f;
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
	m_r.SetTheme(m_showMap.theme);
	m_r.SetStageId(m_showStage);
	if (m_screen != SC_BATTLE && m_screen != SC_RESULT) {
		int st = Clampi(m_save.stageSel, 1, TAC_STAGE_ABS);
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
	if (m_screen == SC_BASE && m_mapLook) {
		yaw = m_camYaw;
		target.x = Clampf(target.x + m_camPan.x, 1.f, (float)MAPN - 2.f);
		target.z = Clampf(target.z + m_camPan.z, 1.f, (float)MAPN - 2.f);
	}
	ApplyZoomHold();
	SetupCamera(target, yaw, m_camPitch, m_camDist, .7f);
	{
		m_r.SetFog(m_camDist * 1.6f, m_camDist * 5.5f, .3f, e.fog);
	}
	m_r.SetAmbient(e.ambSky, e.ambGnd, e.amb, e.hard);
	m_r.SetLight(e.light, target, 22.f);
	m_r.SetFadeAbove(-1);
	float faceYaw[MAX_PARTY];
	float fx = 32.f, fz = 32.f;
	if (m_showMap.nE > 0) { fx = m_showMap.eStartX[0] + .5f; fz = m_showMap.eStartZ[0] + .5f; }
	int showN = m_showN;
	if (showN > MAX_PARTY) showN = MAX_PARTY;
	if (showN < 0) showN = 0;
	for (int i = 0; i < showN; i++)
		faceYaw[i] = atan2f(fx - m_showPos[i].x, fz - m_showPos[i].z);
	m_r.BeginShadowPass();
	m_r.DrawTerrain();
	DrawParams dp;
	{
		int model[MAX_PARTY], ava[MAX_PARTY], cup[MAX_PARTY], n = 0;
		for (int i = 0; i < showN && n < MAX_PARTY; i++) {
			const Actor& a = m_showAct[i];
			if (a.avatar <= 0 || a.model < 0) continue;
			model[n] = a.model; ava[n] = a.avatar; cup[n] = a.bust; n++;
		}
		int pend = m_r.CountPendingAvatars(n, model, ava, cup);
		m_r.BeginAvaBatch(pend > 0 ? pend : 1);
	}
	m_r.BeginInstBatch();
	for (int i = 0; i < showN; i++) {
		Actor& a = m_showAct[i];
		if (!a.poseValid || a.poseFrame != m_r.m_frame) ActorPose(a, m_lib, m_r);
		ActorDraw(m_r, m_lib, a, ActorWorld(a, m_showPos[i], faceYaw[i]), dp, m_time);
	}
	m_r.EndInstBatch();
	m_r.BeginMainPass();
	m_r.DrawTerrain();
	m_r.DrawSky(e.skyTop, e.skyBot, 0, 0);
	m_r.DrawWater();
	m_r.BeginInstBatch();
	for (int i = 0; i < showN; i++) {
		Actor& a = m_showAct[i];
		DrawParams d = dp;
		d.outline = .014f;
		ActorDraw(m_r, m_lib, a, ActorWorld(a, m_showPos[i], faceYaw[i]), d, m_time);
	}
	m_r.EndInstBatch();
	if (m_screen == SC_BATTLE || m_screen == SC_BASE || m_screen == SC_RESULT)
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
		for (int i = 0; i < JobCount(); i++) {
			int c = C_JOB0 + i;
			const ClassDef& cd = ClassInfo(c);
			if (cd.group != G_HOLY || cd.tier < 3 || cd.tier - 1 < rad) continue;
			if (cd.tier < showTier) { showTier = cd.tier; show = c; }
		}
		if (show >= 0) {
			const wchar_t* nm = ClassTitle(show);
			if (!nm) nm = TT(ClassInfo(show).nameTx);
			const wchar_t* more = rad <= 3 ? LangPick14(L"・他職も3まで", L", others to 3", L", autres jusqu'à 3", L", altri fino a 3", L", otros hasta 3", L", 다른 직업도 3까지", L"，其他职到3", L"، غيرهم حتى 3", L", прочие до 3", L", andere bis 3", L", outros até 3", L", anderen tot 3", L", inne do 3", L", diğerleri 3'e") : L"";
			swprintf_s(how, howN, L"%s  Lv%d%s", nm, PromoteLevel(showTier - 1), more);
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
	if (how[0] == 0) {
		int show = -1, showLv = 0, showTier = 99, more = 0;
		for (int i = 0; i < JobCount(); i++) {
			int cls = C_JOB0 + i;
			const ClassDef& cd = ClassInfo(cls);
			int lv = 0;
			for (int k = 0; k < LEARN_N; k++) if (cd.learn[k].skill == skill && cd.learn[k].lv > 0) lv = cd.learn[k].lv;
			if (lv <= 0) continue;
			more++;
			if (cd.tier < showTier || (cd.tier == showTier && (show < 0 || lv < showLv))) {
				showTier = cd.tier; show = cls; showLv = lv;
			}
		}
		if (show >= 0) {
			const wchar_t* nm = ClassTitle(show);
			if (!nm) nm = TT(ClassInfo(show).nameTx);
			const wchar_t* extra = more > 1 ? LangPick14(L"・他", L" +", L" +", L" +", L" +", L" 외", L"·其他", L" +", L" +", L" +", L" +", L" +", L" +", L" +") : L"";
			swprintf_s(how, howN, L"%s  Lv%d%s", nm, showLv, extra);
			*key = CodexJobKey(show);
			*lv = showLv;
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
static int SkillShapeBucket(const SkillDef& sd)
{
	if (sd.shape == SH_LINE) return 4;
	if (sd.shape == SH_FAN) return 5;
	if (sd.area >= 6) return 3;
	if (sd.area <= 0) return 1;
	return 2;
}
static int CodexFamJobs(int fam, int* dst, int cap)
{
	int n = 0;
	auto push = [&](int c) { if (n < cap) dst[n++] = c; };
	if (fam == 1) {
		for (int c = 0; c < C_PLAYER_COUNT; c++) push(c);
		push(C_GLADIATOR);
		push(C_SHADOW);
	} else if (fam == 2) {
		for (int c = C_PLAYER_COUNT; c < C_PLAYER_COUNT + TAC_RANK_LINES * TAC_RANK_N; c++) push(c);
	} else if (fam >= 3 && fam <= 10) {
		int want = fam - 3;
		for (int i = 0; i < JobCount(); i++) if (JobFamOf(C_JOB0 + i) == want) push(C_JOB0 + i);
	}
	return n;
}

static BOOL PreviewAlt(int style);
static void MonPreviewIn(int id, const Actor& a, int& style, BOOL& fly);

enum { FOE_LIST_MAX = 2048 };

static wchar_t FoldCh(wchar_t c)
{
	if (c >= L'A' && c <= L'Z') return (wchar_t)(c - L'A' + L'a');
	if (c == L'　') return L' ';
	return c;
}

static BOOL TextHas(const wchar_t* hay, const wchar_t* needle)
{
	if (!needle || !needle[0]) return TRUE;
	if (!hay) return FALSE;
	for (const wchar_t* s = hay; *s; s++) {
		const wchar_t* a = s;
		const wchar_t* b = needle;
		while (*a && *b && FoldCh(*a) == FoldCh(*b)) { a++; b++; }
		if (!*b) return TRUE;
	}
	return FALSE;
}

static int DebutLv(int stage)
{
	if (stage < 1) stage = 1;
	long long lv = 1 + ((long long)stage - 1) * 29 / 18;
	if (lv > TAC_ENEMY_LV_MAX) lv = TAC_ENEMY_LV_MAX;
	if (lv < 1) lv = 1;
	return (int)lv;
}

static void MonLvSpan(int id, int viewHi, int& lo, int& hi)
{
	const MonsterDef& md = MonsterInfo(id);
	int start = MonsterDebut(id);
	int end = md.maxStage > 0 ? md.maxStage : TAC_STAGE_MAX;
	if (viewHi < 1) viewHi = 1;
	if (end > viewHi) end = viewHi;
	if (end > TAC_STAGE_MAX) end = TAC_STAGE_MAX;
	if (start > end) start = end;
	lo = DebutLv(start);
	hi = DebutLv(end);
	if (hi < lo) hi = lo;
}

static void FmtMonLv(int id, int viewHi, wchar_t* out, int n)
{
	int lo = 1, hi = 1;
	MonLvSpan(id, viewHi, lo, hi);
	if (hi > lo && MonsterInfo(id).maxStage <= 0 && viewHi >= TAC_STAGE_MAX) swprintf_s(out, n, L"%d+", lo);
	else if (hi > lo) swprintf_s(out, n, L"%d–%d", lo, hi);
	else swprintf_s(out, n, L"%d", lo);
}

static void FmtAppearRange(int id, int cleared, wchar_t* out, int n)
{
	const MonsterDef& md = MonsterInfo(id);
	int lo = MonsterDebut(id);
	int view = StageFoeViewHi(cleared);
	int hi = md.maxStage > 0 ? md.maxStage : TAC_STAGE_MAX;
	if (hi > view) hi = view;
	if (hi > TAC_STAGE_MAX) hi = TAC_STAGE_MAX;
	if (hi < lo) hi = lo;
	wchar_t a[40], b[40];
	StageText(lo, a, 40);
	StageText(hi, b, 40);
	if (cleared >= TAC_STAGE_EX)
		swprintf_s(out, n, L"%s-%s / EX", a, b);
	else if (md.maxStage <= 0 && view >= TAC_STAGE_MAX && hi >= TAC_STAGE_MAX)
		swprintf_s(out, n, L"%s+", a);
	else if (hi <= lo)
		swprintf_s(out, n, L"%s", a);
	else
		swprintf_s(out, n, L"%s-%s", a, b);
}

static BOOL BandHasDebut(int band, int lo)
{
	static const int b0[8] = { 1, 1, 11, 21, 41, 61, 101, 201 };
	static const int b1[8] = { 99999, 10, 20, 40, 60, 100, 200, 99999 };
	if (band < 0) band = 0;
	if (band > 7) band = 7;
	return lo >= b0[band] && lo <= b1[band];
}

int Game::FoeList(int* out, int cap) const
{
	int ids[FOE_LIST_MAX];
	int n = MonsterCatalog(ids, FOE_LIST_MAX);
	int w = 0;
	if (!out || cap <= 0) return 0;
	int view = StageFoeViewHi(m_save.stageCleared);
	for (int i = 0; i < n && w < cap; i++) {
		int id = ids[i];
		if (MonsterDebut(id) > view) continue;
		BOOL known = MonsterKnown(id, m_save.stageCleared);
		if (m_monSeenOnly && !known) continue;
		if (m_monFind[0]) {
			if (!known) continue;
			BOOL hit = FALSE;
			for (int lang = 0; lang < 14; lang++) {
				if (TextHas(MonsterLabelAt(id, lang), m_monFind)) { hit = TRUE; break; }
			}
			if (!hit) continue;
		}
		int lo = 1, hi = 1;
		MonLvSpan(id, view, lo, hi);
		(void)hi;
		if (!BandHasDebut(m_monBand, lo)) continue;
		out[w++] = id;
	}
	return w;
}

void Game::EnsureMonPreview()
{
	static int ids[FOE_LIST_MAX];
	int n = FoeList(ids, FOE_LIST_MAX);
	if (n <= 0) {
		if (m_monShown != -1) {
			m_r.LoadBattleCreatures(NULL, 0);
			m_monShown = -1;
		}
		return;
	}
	BOOL in = FALSE;
	for (int i = 0; i < n; i++) if (ids[i] == m_monSel) in = TRUE;
	if (!in) m_monSel = ids[0];
	int id = m_monSel;
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
	int st = Clampi(m_save.stageSel, 1, TAC_STAGE_ABS);
	if (!m_showBuilt || m_showStage != st) BuildShowcase();
	m_r.SetTheme(m_showMap.theme);
	m_r.SetStageId(m_showStage);
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
	m_r.SetFadeAbove(-1);
	m_r.BeginShadowPass();
	m_r.DrawTerrain();
	DrawParams dp;
	V3 pos = c;
	m_r.BeginInstBatch();
	if (m_monShown >= 0) {
		pos.y += m_monAct.hover;
		ActorPose(m_monAct, m_lib, m_r);
		ActorDraw(m_r, m_lib, m_monAct, ActorWorld(m_monAct, pos, m_monYaw), dp, m_time);
	}
	m_r.EndInstBatch();
	m_r.BeginMainPass();
	m_r.DrawTerrain();
	m_r.DrawSky(e.skyTop, e.skyBot, 0, 0);
	m_r.DrawWater();
	m_r.BeginInstBatch();
	if (m_monShown >= 0) {
		DrawParams d = dp;
		d.outline = .014f;
		ActorDraw(m_r, m_lib, m_monAct, ActorWorld(m_monAct, pos, m_monYaw), d, m_time);
	}
	m_r.EndInstBatch();
}

void Game::DrawMonCodex()
{
	static int ids[FOE_LIST_MAX];
	int nCat = MonsterCatalog(ids, FOE_LIST_MAX);
	int view = StageFoeViewHi(m_save.stageCleared);
	int nAll = 0;
	for (int i = 0; i < nCat; i++) if (MonsterDebut(ids[i]) <= view) nAll++;
	float x = U(24), y = U(24), w = min(U(520.f), (float)m_w * .46f), h = (float)m_h - U(108);
	Panel(x, y, w, h);
	const wchar_t* title = LangPick14(L"敵一覧", L"Foes", L"Ennemis", L"Nemici", L"Enemigos", L"적 목록", L"敌人一览", L"الأعداء", L"Враги", L"Gegner", L"Inimigos", L"Vijanden", L"Wrogowie", L"Düşmanlar");
	m_r.HudText(title, x + U(16), y + U(22), U(20), RGBA8(255, 226, 150), 0, 1);
	float fy = y + U(40);
	float bh = U(28);
	float gap = U(6);
	float clearW = U(36);
	float findW = w - U(32) - clearW - gap;
	float fx = x + U(16);
	BOOL foc = m_textFocus == 4;
	UiBtn findBtn = { ID_MON_FIND, fx, fy, findW, bh, 1, 0 };
	m_btns.push_back(findBtn);
	if (ClickIn(fx, fy, findW, bh) || m_activateId == ID_MON_FIND) {
		if (m_textFocus != 4) {
			m_textFocus = 4;
			m_textCaret = TacWLen(m_monFind);
			m_imeComp[0] = 0;
			m_textReload = 1;
		}
		m_activateId = -1;
	}
	BOOL nf = m_navMode && m_focusId == ID_MON_FIND;
	TacMarkBox(m_textBox, fx, fy, findW, bh);
	m_textPx = U(13);
	m_textPad = U(10);
	m_r.HudRect(fx, fy, findW, bh, foc ? RGBA8(40, 46, 80, 240) : RGBA8(20, 24, 44, 230));
	m_r.HudFrame(fx, fy, findW, bh, U(1.5f), foc || nf ? RGBA8(255, 230, 150) : RGBA8(120, 104, 74));
	wchar_t box[48];
	if (foc) TacCaretStr(box, 48, m_monFind, m_textCaret, m_imeComp, fmodf(m_time, 1.f) < .5f);
	else if (m_monFind[0]) wcsncpy_s(box, m_monFind, _TRUNCATE);
	else wcscpy_s(box, TT(TX_CDX_FIND));
	BOOL emptyHint = !foc && !m_monFind[0];
	m_r.HudText(box, fx + U(10), fy + bh * .5f, U(13), emptyHint ? RGBA8(140, 140, 150) : RGBA8(255, 255, 255), 0, 1);
	if (Btn(ID_MON_CLEAR, x + U(16) + findW + gap, fy, clearW, bh, L"×", m_monFind[0] != 0, FALSE, U(16))) {
		m_monFind[0] = 0;
		m_codexPage = 0;
		m_textFocus = 4;
		m_textCaret = 0;
		m_imeComp[0] = 0;
		m_textReload = 1;
	}
	fy += bh + gap;
	static const wchar_t* bandL[8] = { L"", L"Lv 1–10", L"Lv 11–20", L"Lv 21–40", L"Lv 41–60", L"Lv 61–100", L"Lv 101–200", L"Lv 201+" };
	int band = m_monBand;
	if (band < 0 || band > 7) band = 0;
	const wchar_t* bandName = band == 0
		? LangPick14(L"すべてのLv", L"Any level", L"Tout niveau", L"Ogni livello", L"Cualquier nivel", L"모든 레벨", L"全部等级", L"كل المستويات", L"Любой уровень", L"Jedes Level", L"Qualquer nível", L"Elk level", L"Każdy poziom", L"Tüm seviye")
		: bandL[band];
	float lvW = U(36);
	if (Btn(ID_MON_LVPREV, x + U(16), fy, lvW, bh, L"◀")) { m_monBand = (band + 7) % 8; m_codexPage = 0; }
	m_r.HudText(bandName, x + U(16) + lvW + U(8), fy + bh * .5f, U(13), RGBA8(255, 226, 150), 0, 1);
	if (Btn(ID_MON_LVNEXT, x + U(16) + lvW + U(168), fy, lvW, bh, L"▶")) { m_monBand = (band + 1) % 8; m_codexPage = 0; }
	const wchar_t* seenW = LangPick14(L"見た敵", L"Seen", L"Vus", L"Visti", L"Vistos", L"본 적", L"见过", L"المشاهد", L"Виденные", L"Gesehen", L"Vistos", L"Gezien", L"Widziani", L"Görülen");
	float seenX = x + w - U(16) - U(96);
	if (Btn(ID_MON_SEEN, seenX, fy, U(96), bh, seenW, TRUE, m_monSeenOnly != 0, U(13))) {
		m_monSeenOnly = m_monSeenOnly ? 0 : 1;
		m_codexPage = 0;
	}
	int n = FoeList(ids, FOE_LIST_MAX);
	BOOL inList = FALSE;
	for (int i = 0; i < n; i++) if (ids[i] == m_monSel) inList = TRUE;
	if (!inList && n > 0) m_monSel = ids[0];
	wchar_t sub[64];
	swprintf_s(sub, L"%d / %d", n, nAll);
	m_r.HudText(sub, x + w - U(16), y + U(22), U(13), RGBA8(190, 200, 220), 2, 0);
	float listTop = fy + bh + U(8);
	float listBot = y + h - U(220);
	if (listBot < listTop + U(80)) listBot = listTop + U(80);
	int per = (int)((listBot - listTop) / U(28));
	if (per < 3) per = 3;
	int pages = max(1, (n + per - 1) / per);
	if (m_codexPage < 0) m_codexPage = 0;
	if (m_codexPage >= pages) m_codexPage = pages - 1;
	int a = m_codexPage * per, b = min(n, a + per);
	float yy = listTop;
	if (n < 1) {
		m_r.HudText(LangPick14(L"該当なし", L"No matches", L"Aucun", L"Nessuno", L"Ninguno", L"없음", L"没有符合的", L"لا يوجد", L"Нет совпадений", L"Keine Treffer", L"Nenhum", L"Geen", L"Brak", L"Yok"), x + U(24), yy + U(8), U(14), RGBA8(170, 176, 190), 0, 0);
	}
	for (int i = a; i < b; i++) {
		int id = ids[i];
		BOOL seen = MonsterKnown(id, m_save.stageCleared);
		BOOL on = id == m_monSel;
		if (on) m_r.HudRect(x + U(12), yy - U(4), w - U(24), U(26), RGBA8(70, 78, 120, 180));
		const wchar_t* nm = seen ? MonsterLabel(id) : L"？？？";
		m_r.HudText(nm, x + U(24), yy + U(8), FitTextPx(nm, w - U(150), U(15), 1), on ? RGBA8(255, 226, 150) : (seen ? RGBA8(236, 236, 244) : RGBA8(140, 146, 164)), 0, 1);
		wchar_t lv[24];
		FmtMonLv(id, view, lv, 24);
		m_r.HudText(lv, x + w - U(52), yy + U(8), U(12), seen ? RGBA8(180, 196, 220) : RGBA8(150, 166, 186), 2, 0);
		if (seen) {
			int kn = FoeKills(id);
			wchar_t ks[16];
			swprintf_s(ks, L"%d", kn);
			m_r.HudText(ks, x + w - U(16), yy + U(8), U(13), kn > 0 ? RGBA8(255, 214, 120) : RGBA8(140, 146, 164), 2, 0);
		}
		if (m_click && m_clickY >= listTop - U(2) && m_clickX >= x + U(12) && m_clickX < x + w - U(12) && m_clickY >= yy - U(4) && m_clickY < yy + U(22)) {
			m_monSel = id;
			m_click = FALSE;
		}
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
		wchar_t line[320];
		const wchar_t* stageW = LangPick14(L"出現", L"Appears", L"Apparition", L"Compare", L"Aparece", L"출현", L"出现", L"الظهور", L"Появление", L"Erscheint", L"Aparece", L"Verschijnt", L"Pojawia się", L"Çıkış");
		wchar_t span[80];
		FmtAppearRange(id, m_save.stageCleared, span, 80);
		swprintf_s(line, L"%s %s  Lv%d", stageW, span, lv);
		m_r.HudText(line, x + U(16), sy, U(13), RGBA8(255, 226, 150), 0, 1);
		sy += U(18);
		static const int showS[] = { S_HP, S_MP, S_STR, S_DEF, S_MAG, S_RES, S_AGI, S_DEX, S_MOV, S_JMP };
		static const int showT[] = { TX_ST_HP, TX_ST_MP, TX_ST_STR, TX_ST_DEF, TX_ST_MAG, TX_ST_RES, TX_ST_AGI, TX_ST_DEX, TX_ST_MOV, TX_ST_JMP };
		for (int i = 0; i < 10; i++) {
			swprintf_s(line, L"%s %d", TT(showT[i]), stt[showS[i]]);
			m_r.HudText(line, x + U(16) + (i % 2) * col, sy + (i / 2) * U(16), U(13), RGBA8(230, 232, 240), 0, 0);
		}
		sy += U(16) * 5 + U(2);
		wchar_t wk[240];
		MonsterWeakText(id, wk, 240);
		swprintf_s(line, L"%s  %d-%d", wk, md.rMin, md.rMax);
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
		if (n > 0) {
			wchar_t lv[24];
			FmtMonLv(m_monSel, view, lv, 24);
			wchar_t line[48];
			swprintf_s(line, L"%s %s", TT(TX_MISC_LV), lv);
			m_r.HudText(line, x + U(16), sy + U(22), U(13), RGBA8(180, 196, 220), 0, 1);
		}
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
}

void Game::DrawCodexTabs()
{
	const wchar_t* labels[6] = {
		TT(TX_CDX_GROW), TT(TX_CDX_ALCH), TT(TX_CDX_SKILL),
		LangPick14(L"ジョブツリー", L"Job Tree", L"Arbre des classes", L"Albero classi", L"Árbol de clases", L"직업 트리", L"职业树", L"شجرة الوظائف", L"Дерево классов", L"Klassenbaum", L"Árvore de classes", L"Klassenboom", L"Drzewo klas", L"Sınıf ağacı"),
		LangPick14(L"敵", L"Foes", L"Ennemis", L"Nemici", L"Enemigos", L"적", L"敌人", L"أعداء", L"Враги", L"Gegner", L"Inimigos", L"Vijanden", L"Wrogowie", L"Düşman"),
		TT(TX_BASE_CLOSE)
	};
	const int ids[6] = { ID_CDX_GROW, ID_CDX_ALCH, ID_CDX_SKILL, ID_CDX_BRANCH, ID_MON_TAB, ID_BASE_CLOSE };
	float margin = U(16), gap = U(8), bh = U(34);
	float y = (float)m_h - bh - U(10);
	float span = (float)m_w - margin * 2.f;
	float bw = (span - gap * 5.f) / 6.f;
	if (bw < U(52)) bw = U(52);
	if (bw * 6.f + gap * 5.f > span) bw = (span - gap * 5.f) / 6.f;
	float x = margin;
	for (int i = 0; i < 6; i++) {
		BOOL on = (i == 0 && m_codex == 2) || (i == 1 && m_codex == 3) || (i == 2 && m_codex == 1) || (i == 3 && m_codex == 5) || (i == 4 && m_codex == 4);
		if (Btn(ids[i], x, y, bw, bh, labels[i], TRUE, on)) {
			if (i != 4 && m_textFocus == 4) m_textFocus = 0;
			if (i == 0) m_codex = 2;
			else if (i == 1) m_codex = 3;
			else if (i == 2) { m_codex = 1; m_codexPage = 0; }
			else if (i == 3) m_codex = 5;
			else if (i == 4) {
				if (m_codex != 4) m_codexPage = 0;
				m_codex = 4;
				m_textFocus = 4;
				m_textCaret = TacWLen(m_monFind);
				m_imeComp[0] = 0;
				m_textReload = 1;
			}
			else m_codex = 0;
		}
		x += bw + gap;
	}
}

// 11体目以降の編成拡張。そのステージをクリアしてから買える。10体までは従来どおり。
static int PartySlotStage(int cap);

void Game::DrawCodex()
{
	DrawCodexTabs();
	if (m_codex == 4) { DrawMonCodex(); return; }
	float tabTop = (float)m_h - U(34) - U(10);
	float x = U(30), y = U(16), w = (float)m_w - U(60);
	float h = tabTop - U(8) - y;
	if (h < U(80)) h = U(80);
	if (y + h > tabTop - U(6)) h = tabTop - U(6) - y;
	Panel(x, y, w, h);
	const wchar_t* title = m_codex == 5
		? LangPick14(L"ジョブツリー", L"Job Tree", L"Arbre des classes", L"Albero classi", L"Árbol de clases", L"직업 트리", L"职业树", L"شجرة الوظائف", L"Дерево классов", L"Klassenbaum", L"Árvore de classes", L"Klassenboom", L"Drzewo klas", L"Sınıf ağacı")
		: (m_codex == 2 ? TT(TX_CDX_GROW) : (m_codex == 3 ? TT(TX_CDX_ALCH) : TT(TX_CDX_SKILL)));
	m_r.HudText(title, x + w * .5f, y + U(28), U(22), RGBA8(255, 226, 150), 1, 1);
	float yy = y + U(58);
	if (m_codex == 1) {
		if (m_codexFam < 0 || m_codexFam > 10) m_codexFam = 0;
		if (m_codexSort < 0 || m_codexSort > 3) m_codexSort = 0;
		if (m_codexShape < 0 || m_codexShape > 5) m_codexShape = 0;
		int famJobs[240];
		int nFam = CodexFamJobs(m_codexFam, famJobs, 240);
		if (m_codexFam == 0) {
			if (m_codexJob != 0 && m_codexJob != -1) m_codexJob = 0;
		} else if (m_codexJob > 0) {
			BOOL hit = FALSE;
			for (int i = 0; i < nFam; i++) if (famJobs[i] == m_codexJob) hit = TRUE;
			if (!hit) m_codexJob = 0;
		} else if (m_codexJob != 0) m_codexJob = 0;
		const wchar_t* famName = CodexWord(1);
		if (m_codexFam == 1) famName = LangPick14(L"初期", L"Base", L"Base", L"Base", L"Base", L"초기", L"初期", L"أساس", L"Начало", L"Basis", L"Base", L"Basis", L"Baza", L"Başlangıç");
		else if (m_codexFam == 2) famName = LangPick14(L"上位", L"High", L"Haut", L"Alto", L"Alto", L"상위", L"上位", L"عال", L"Высшие", L"Hoch", L"Alto", L"Hoog", L"Wyższe", L"Üst");
		else if (m_codexFam >= 3) famName = ClassName(LineParent(m_codexFam - 3));
		const wchar_t* sn = m_codexSort == 1 ? CodexWord(3) : (m_codexSort == 2 ? L"MP" : (m_codexSort == 3 ? CodexWord(4) : CodexWord(0)));
		float jw = min(U(460.f), (w - U(48)) * .56f);
		float sw = min(U(280.f), w - U(48) - jw - U(12));
		int df = Sel(ID_CDX_FAM, x + U(24), y + U(46), jw, U(32), LangPick14(L"系統", L"Line", L"Lignée", L"Schiera", L"Linaje", L"계열", L"系统", L"السلالة", L"Линия", L"Linie", L"Linhagem", L"Lijn", L"Linia", L"Soy"), famName);
		int ds = Sel(ID_CDX_SORT, x + U(24) + jw + U(12), y + U(46), sw, U(32), CodexWord(2), sn);
		if (df) { m_codexFam = (m_codexFam + df + 11) % 11; m_codexJob = 0; m_codexJobPage = 0; m_codexPage = 0; m_codexShape = 0; m_codexEl = 0; }
		if (ds) { m_codexSort = (m_codexSort + ds + 4) % 4; m_codexPage = 0; }
		float jy = y + U(82);
		float jh = U(26);
		float gap = U(4);
		int slots = m_codexFam == 0 ? 1 : nFam;
		int perJ = 6;
		int jPages = max(1, (slots + perJ - 1) / perJ);
		if (m_codexJobPage < 0) m_codexJobPage = 0;
		if (m_codexJobPage >= jPages) m_codexJobPage = jPages - 1;
		float arrowW = U(36);
		float allW = U(72);
		float strip = w - U(48) - allW - arrowW * 2.f - gap * 3.f;
		float bwJ = strip / (float)perJ;
		if (Btn(ID_CDX_JP0 + 7, x + U(24), jy, allW, jh, CodexWord(1), TRUE, m_codexJob == 0, U(12))) { m_codexJob = 0; m_codexPage = 0; }
		int ja = m_codexJobPage * perJ;
		for (int t = 0; t < perJ; t++) {
			int ix = ja + t;
			const wchar_t* lab = L"";
			BOOL en = FALSE;
			int pick = 0;
			BOOL on = FALSE;
			if (m_codexFam == 0) {
				if (ix == 0) { lab = TT(TX_SK_TAB_FORM); en = TRUE; pick = -1; on = m_codexJob == -1; }
			} else if (ix < nFam) {
				lab = ClassName(famJobs[ix]); en = TRUE; pick = famJobs[ix]; on = m_codexJob == pick;
			}
			if (!en) continue;
			if (Btn(ID_CDX_JP0 + t, x + U(24) + allW + gap + (bwJ + gap) * t, jy, bwJ, jh, lab, en, on, U(11))) {
				m_codexJob = pick; m_codexPage = 0;
			}
		}
		if (jPages > 1 && Btn(ID_CDX_JPREV, x + w - U(24) - arrowW * 2 - gap, jy, arrowW, jh, L"◀")) m_codexJobPage = (m_codexJobPage + jPages - 1) % jPages;
		if (jPages > 1 && Btn(ID_CDX_JNEXT, x + w - U(24) - arrowW, jy, arrowW, jh, L"▶")) m_codexJobPage = (m_codexJobPage + 1) % jPages;
		auto jobHas = [&](int s) -> BOOL {
			if (SkillInfo(s).enemyOnly) return FALSE;
			if (m_codexJob == -1) return IsFormSkill(s);
			if (IsFormSkill(s)) return m_codexFam == 0 && m_codexJob == 0;
			if (m_codexJob > 0) return CodexLearnLv(m_codexJob, s) > 0;
			if (m_codexFam == 0) return TRUE;
			for (int i = 0; i < nFam; i++) if (CodexLearnLv(famJobs[i], s) > 0) return TRUE;
			return FALSE;
		};
		int shapeHave[6] = {};
		int elHave[9] = {};
		static const int kEl[8] = { EL_FIRE, EL_ICE, EL_THUNDER, EL_HOLY, EL_DARK, EL_WIND, EL_EARTH, EL_POISON };
		for (int s = 0; s < SKL_COUNT; s++) {
			if (!jobHas(s) || SkillInfo(s).kind == SK_PASSIVE) continue;
			const SkillDef& sd = SkillInfo(s);
			int sh = SkillShapeBucket(sd);
			if (sh >= 1 && sh <= 5) shapeHave[sh]++;
			for (int e = 0; e < 8; e++) if (kEl[e] == sd.element) elHave[e + 1]++;
		}
		if (m_codexShape && !shapeHave[m_codexShape]) m_codexShape = 0;
		int curEl = 0;
		for (int e = 0; e < 8; e++) if (kEl[e] == m_codexEl) curEl = e + 1;
		if (m_codexEl && !elHave[curEl]) { m_codexEl = 0; curEl = 0; }
		int shapeOk[6] = { 1, 0, 0, 0, 0, 0 };
		int elOk[9] = { 1, 0, 0, 0, 0, 0, 0, 0, 0 };
		for (int s = 0; s < SKL_COUNT; s++) {
			if (!jobHas(s) || SkillInfo(s).kind == SK_PASSIVE) continue;
			const SkillDef& sd = SkillInfo(s);
			int sh = SkillShapeBucket(sd);
			if (!m_codexEl || sd.element == m_codexEl) if (sh >= 1 && sh <= 5) shapeOk[sh] = 1;
			if (!m_codexShape || sh == m_codexShape) for (int e = 0; e < 8; e++) if (kEl[e] == sd.element) elOk[e + 1] = 1;
		}
		const wchar_t* shapeL[6] = { CodexWord(1), TT(TX_SHAPE_ONE), TT(TX_MISC_AREA), TT(TX_SHAPE_WIDE), TT(TX_SHAPE_LINE), TT(TX_SHAPE_FAN) };
		float syy = jy + jh + gap;
		float bwS = (w - U(48) - gap * 5.f) / 6.f;
		for (int t = 0; t < 6; t++) {
			if (Btn(ID_CDX_SHAPE0 + t, x + U(24) + (bwS + gap) * t, syy, bwS, jh, shapeL[t], t == 0 || shapeOk[t], m_codexShape == t, U(12))) {
				m_codexShape = t; m_codexPage = 0;
			}
		}
		syy += jh + gap;
		const wchar_t* elL[9] = {
			CodexWord(1),
			LangPick14(L"火", L"Fi", L"Fe", L"Fu", L"Fu", L"화", L"火", L"ن", L"Ог", L"Fe", L"Fo", L"Vu", L"Og", L"At"),
			LangPick14(L"氷", L"Ic", L"Gl", L"Gh", L"Hi", L"빙", L"冰", L"ج", L"Лд", L"Ei", L"Ge", L"IJs", L"Ló", L"Bu"),
			LangPick14(L"雷", L"Th", L"To", L"Tu", L"Tr", L"뢰", L"雷", L"ر", L"Гр", L"Do", L"Tr", L"Do", L"Gr", L"Yı"),
			LangPick14(L"聖", L"Ho", L"Sa", L"Sa", L"Sa", L"성", L"圣", L"م", L"Св", L"He", L"Sa", L"He", L"Św", L"Ku"),
			LangPick14(L"闇", L"Da", L"Té", L"Os", L"Os", L"암", L"暗", L"ظ", L"Ть", L"Du", L"Tr", L"Do", L"Mr", L"Ka"),
			LangPick14(L"風", L"Wi", L"Ve", L"Ve", L"Vi", L"풍", L"风", L"ر", L"Ве", L"Wi", L"Ve", L"Wi", L"Wi", L"Rü"),
			LangPick14(L"地", L"Ea", L"Te", L"Te", L"Ti", L"지", L"地", L"أ", L"Зе", L"Er", L"Te", L"Aa", L"Zi", L"To"),
			LangPick14(L"毒", L"Po", L"Po", L"Ve", L"Ve", L"독", L"毒", L"س", L"Яд", L"Gi", L"Ve", L"Gi", L"Tr", L"Ze")
		};
		float bwE = (w - U(48) - gap * 8.f) / 9.f;
		for (int t = 0; t < 9; t++) {
			int on = t == 0 ? m_codexEl == 0 : m_codexEl == kEl[t - 1];
			if (Btn(ID_CDX_EL0 + t, x + U(24) + (bwE + gap) * t, syy, bwE, jh, elL[t], t == 0 || elOk[t], on, U(11))) {
				m_codexEl = t == 0 ? 0 : kEl[t - 1]; m_codexPage = 0;
			}
		}
		static CodexHit hits[SKL_COUNT];
		int n = 0;
		for (int s = 0; s < SKL_COUNT && n < SKL_COUNT; s++) {
			if (!jobHas(s)) continue;
			if (s >= SKL_ART0 && s < SKL_ARTN && PaceRoleOfLv((s - SKL_ART0) % 26 + 7) < 0) continue;
			const SkillDef& sd = SkillInfo(s);
			if (sd.kind == SK_PASSIVE) { if (m_codexShape || m_codexEl) continue; }
			else {
				if (m_codexShape && SkillShapeBucket(sd) != m_codexShape) continue;
				if (m_codexEl && sd.element != m_codexEl) continue;
			}
			CodexHit& h = hits[n];
			h.skill = s;
			h.mp = sd.mp;
			wcscpy_s(h.name, SkillLabel(s));
			if (m_codexJob == -1) {
				swprintf_s(h.how, L"%s", TT(TX_SK_TAB_FORM));
				h.lv = s - SKL_FORM0;
				h.key = 0;
			} else if (m_codexJob > 0) {
				int lv = CodexLearnLv(m_codexJob, s);
				const wchar_t* nm = ClassTitle(m_codexJob);
				if (!nm) nm = TT(ClassInfo(m_codexJob).nameTx);
				swprintf_s(h.how, L"%s  Lv%d", nm, lv);
				h.lv = lv;
				h.key = 0;
			} else {
				CodexFillHow(s, h.how, 160, &h.key, &h.lv);
			}
			n++;
		}
		s_codexSort = m_codexSort;
		if (n > 1) qsort(hits, n, sizeof(CodexHit), CmpCodex);
		float listTop = syy + jh + U(8);
		float listBot = y + h - U(64);
		float rowH = U(68);
		int per = (int)((listBot - listTop) / rowH);
		if (per < 2) per = 2;
		int pages = max(1, (n + per - 1) / per);
		if (m_codexPage < 0) m_codexPage = 0;
		if (m_codexPage >= pages) m_codexPage = pages - 1;
		int a = m_codexPage * per, b = min(n, a + per);
		yy = listTop;
		if (n == 0) m_r.HudText(TT(TX_PT_NONE), x + U(24), yy, U(16), RGBA8(190, 200, 220), 0, 1);
		float textW = w - U(48);
		for (int i = a; i < b; i++) {
			const SkillDef& sd = SkillInfo(hits[i].skill);
			wchar_t attr[120], row[240];
			SkillAttrText(sd, attr, 120);
			if (attr[0]) m_r.HudText(attr, x + U(24), yy + U(12), FitTextPx(attr, textW, U(13), 1), RGBA8(255, 214, 140), 0, 1);
			swprintf_s(row, L"%s   MP%d   %s", hits[i].name, hits[i].mp, hits[i].how);
			m_r.HudText(row, x + U(24), yy + U(32), FitTextPx(row, textW, U(15), 1), RGBA8(240, 240, 245), 0, 1);
			m_r.HudText(TT(sd.descTx), x + U(36), yy + U(52), FitTextPx(TT(sd.descTx), textW - U(12), U(12), 0), RGBA8(190, 200, 220), 0, 0);
			yy += rowH;
		}
		wchar_t pg[32];
		swprintf_s(pg, L"%d / %d", m_codexPage + 1, pages);
		m_r.HudText(pg, x + w * .5f, y + h - U(46), U(14), RGBA8(220, 220, 220), 1, 0);
		if (Btn(ID_CDX_PREV, x + U(24), y + h - U(40), U(120), U(32), L"◀")) m_codexPage = (m_codexPage + pages - 1) % pages;
		if (Btn(ID_CDX_NEXT, x + U(156), y + h - U(40), U(120), U(32), L"▶")) m_codexPage = (m_codexPage + 1) % pages;
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
		{
			float rx = x + min(U(460.f), w * .46f);
			float rw = x + w - U(24) - rx;
			if (rw < U(160)) rw = U(160);
			float ry = y + U(58);
			wchar_t row[160];
			swprintf_s(row, L"%s   %s %d", TT(TX_TICKET_PT),
				LangPick14(L"最大", L"Max", L"Max", L"Max", L"Máx", L"최대", L"最多", L"الحد", L"Макс.", L"Max", L"Máx", L"Max", L"Maks.", L"En çok"),
				MAX_PARTY);
			m_r.HudText(row, rx, ry, FitTextPx(row, rw, U(16), 1), RGBA8(255, 226, 150), 0, 1);
			ry += U(26);
			int cap = PartyCap();
			swprintf_s(row, L"%s %d / %d",
				LangPick14(L"いま", L"Now", L"Actuel", L"Ora", L"Ahora", L"현재", L"当前", L"الآن", L"Сейчас", L"Jetzt", L"Agora", L"Nu", L"Teraz", L"Şimdi"),
				cap, MAX_PARTY);
			m_r.HudText(row, rx, ry, U(15), RGBA8(230, 230, 240), 0, 0);
			ry += U(24);
			swprintf_s(row, L"6-10  %s",
				LangPick14(L"店だけで増える", L"Shop only", L"Boutique seule", L"Solo negozio", L"Solo tienda", L"상점만", L"仅商店", L"المتجر فقط", L"Только лавка", L"Nur Laden", L"Só loja", L"Alleen winkel", L"Tylko sklep", L"Yalnız dükkan"));
			m_r.HudText(row, rx, ry, FitTextPx(row, rw, U(15), 0), cap >= 10 ? RGBA8(140, 220, 160) : RGBA8(230, 230, 240), 0, 0);
			ry += U(22);
			swprintf_s(row, L"%s",
				LangPick14(L"クリア後に店で買える", L"Buy at the shop after clearing", L"À la boutique après le niveau", L"In negozio dopo il livello", L"En la tienda tras la fase", L"클리어 후 상점에서", L"通关后在商店购买", L"يُشترى من المتجر بعد الإنهاء", L"В лавке после этапа", L"Im Laden nach der Stufe", L"Na loja após a fase", L"In de winkel na het level", L"W sklepie po etapie", L"Bölümden sonra dükkanda"));
			m_r.HudText(row, rx, ry, FitTextPx(row, rw, U(13), 0), RGBA8(190, 200, 220), 0, 0);
			ry += U(22);
			for (int n = 11; n <= MAX_PARTY; n++) {
				int need = PartySlotStage(n - 1);
				swprintf_s(row, L"%d  %s %d", n, TT(TX_BT_STAGE), need);
				DWORD col = cap >= n ? RGBA8(140, 220, 160) : (m_save.stageCleared >= need ? RGBA8(255, 226, 150) : RGBA8(170, 174, 186));
				m_r.HudText(row, rx, ry, U(15), col, 0, 0);
				ry += U(22);
			}
		}
	} else if (m_codex == 5) {
		float rx = x + U(24);
		float rw = w - U(48);
		float ry = yy;
		float viewBot = y + h - U(8);
		if (m_growN > 1 && Btn(ID_GROW_BACK, x + w - U(78), y + U(14), U(48), U(28), L"◀", TRUE, FALSE, U(14)))
			m_growN--;
		static const int kRoot[6] = { C_SOLDIER, C_FIGHTER, C_ARCHER, C_THIEF, C_MAGE, C_CLERIC };
			float gapb = U(4);
			float bw = (rw - gapb * 5.f) / 6.f;
			if (bw < 8.f) bw = 8.f;
			wchar_t growBuf[1200];
			const wchar_t* growTip = NULL;
			float gax = 0, gay = 0, gaw = 0, gah = 0;
			auto armTip = [&](int cls, float hx, float hy, float hw, float hh, int id) {
				BOOL on = (m_mouseSeen && !m_navMode && Hit(hx, hy, hw, hh)) || (m_navMode && m_focusId == id);
				if (!on) return;
				const ClassDef& cd = ClassInfo(cls);
				static const int gx[G_COUNT] = { TX_G_MELEE, TX_G_SWIFT, TX_G_MAGIC, TX_G_HOLY };
				int g = Clampi(cd.group, 0, G_COUNT - 1);
				swprintf_s(growBuf, L"%s\n%s", TT(gx[g]), ClassBlurb(cls));
				growTip = growBuf;
				gax = hx; gay = hy; gaw = hw; gah = hh;
			};
			for (int i = 0; i < 6; i++) {
				float hx = rx + (bw + gapb) * i;
				armTip(kRoot[i], hx, ry, bw, U(26), ID_GROW_ROOT0 + i);
				if (Btn(ID_GROW_ROOT0 + i, hx, ry, bw, U(26), ClassName(kRoot[i]), TRUE, m_growN > 0 && m_growPath[0] == kRoot[i], U(11))) {
					m_growPath[0] = kRoot[i];
					m_growN = 1;
				}
			}
			ry += U(30);
			if (m_growN < 1 || m_growN > 14 || m_growPath[0] < 0) { m_growPath[0] = C_SOLDIER; m_growN = 1; }
			int rootI = 0;
			for (int i = 0; i < 6; i++) if (kRoot[i] == m_growPath[0]) rootI = i;
			float prevCx = rx + (bw + gapb) * rootI + bw * .5f;
			float prevBot = ry - U(4);
			int rowsN = 0;
			int rowOpt[14][4];
			int rowNopt[14];
			int rowPick[14];
			for (int d = 0; d < m_growN && rowsN < 14; d++) {
				EvoOption o[4];
				int n = EvoOptions(m_growPath[d], o);
				if (n <= 0) break;
				rowNopt[rowsN] = n;
				rowPick[rowsN] = -1;
				for (int k = 0; k < n; k++) {
					rowOpt[rowsN][k] = o[k].target;
					if (d + 1 < m_growN && o[k].target == m_growPath[d + 1]) rowPick[rowsN] = k;
				}
				rowsN++;
				if (d + 1 >= m_growN) break;
				if (rowPick[rowsN - 1] < 0) { m_growN = d + 1; break; }
			}
			float viewTop = ry;
			float pitch = U(56);
			float need = rowsN * pitch;
			float avail = viewBot - viewTop;
			if (avail < U(40)) avail = U(40);
			if (m_growSeen != m_growN) {
				m_growScroll = need > avail ? need - avail : 0.f;
				m_growSeen = m_growN;
			}
			if (need <= avail) m_growScroll = 0.f;
			if (m_growScroll < 0.f) m_growScroll = 0.f;
			if (need > avail && m_growScroll > need - avail) m_growScroll = need - avail;
			const ClassDef& cd0 = ClassInfo(m_growPath[0]);
			const wchar_t* role = cd0.magic
				? LangPick14(L"魔法側。物理の職へは分岐しない", L"Magic side. No branch into physical jobs", L"Côté magie. Pas de branche physique", L"Lato magico. Nessun ramo fisico", L"Lado mágico. Sin rama física", L"마법 쪽. 물리 직업으로는 갈라지지 않는다", L"魔法侧。不分向物理职业", L"جانب السحر. لا فرع نحو الجسدي", L"Сторона магии. Без ветки в физические", L"Magieseite. Kein Zweig zu physisch", L"Lado mágico. Sem ramo físico", L"Magische kant. Geen tak naar fysiek", L"Strona magii. Bez gałęzi w fizyczne", L"Büyü yanı. Fiziksel işe dal yok")
				: LangPick14(L"物理側。魔法の職へは分岐しない", L"Physical side. No branch into magic jobs", L"Côté physique. Pas de branche magique", L"Lato fisico. Nessun ramo magico", L"Lado físico. Sin rama mágica", L"물리 쪽. 마법 직업으로는 갈라지지 않는다", L"物理侧。不分向魔法职业", L"جانب الجسد. لا فرع نحو السحر", L"Сторона физики. Без ветки в магию", L"Physische Seite. Kein Zweig zur Magie", L"Lado físico. Sem ramo mágico", L"Fysieke kant. Geen tak naar magie", L"Strona fizyczna. Bez gałęzi w magię", L"Fiziksel yan. Büyü işine dal yok");
			m_r.HudText(role, rx, ry, FitTextPx(role, rw - U(120), U(13), 0), RGBA8(180, 196, 220), 0, 0);
			if (need > avail)
				m_r.HudText(LangPick14(L"ホイールで上下", L"Wheel to scroll", L"Molette pour défiler", L"Rotella per scorrere", L"Rueda para desplazar", L"휠로 스크롤", L"滚轮上下", L"العجلة للتمرير", L"Колесо листает", L"Rad zum Scrollen", L"Roda para rolar", L"Wiel om te scrollen", L"Kółko przewija", L"Tekerle kaydır"),
					rx + rw, ry, U(12), RGBA8(160, 170, 190), 2, 0);
			ry += U(20);
			viewTop = ry;
			avail = viewBot - viewTop;
			if (avail < U(40)) avail = U(40);
			need = rowsN * pitch;
			if (need <= avail) m_growScroll = 0.f;
			else if (m_growScroll > need - avail) m_growScroll = need - avail;
			float origin = viewTop - m_growScroll;
			float btnH = pitch - U(20);
			if (btnH < U(26)) btnH = U(26);
			for (int r = 0; r < rowsN; r++) {
				int n = rowNopt[r];
				float yRow = origin + r * pitch;
				float childW = (rw - gapb * (n - 1)) / (float)n;
				if (childW < 8.f) childW = 8.f;
				float rowW = childW * n + gapb * (n - 1);
				float rowX = rx + (rw - rowW) * .5f;
				float barY = yRow + U(6);
				float by = barY + U(8);
				float x0 = rowX + childW * .5f;
				float x1 = rowX + (n - 1) * (childW + gapb) + childW * .5f;
				float left = prevCx < x0 ? prevCx : x0;
				float right = prevCx > x1 ? prevCx : x1;
				if (right < left) { float t = left; left = right; right = t; }
				if (barY >= viewTop && barY <= viewBot)
					m_r.HudRect(left, barY - 1.f, right - left < 2.f ? 2.f : right - left, 2.f, RGBA8(210, 176, 90, 220));
				float stem0 = prevBot < viewTop ? viewTop : prevBot;
				float stem1 = barY > viewBot ? viewBot : barY;
				if (stem1 > stem0)
					m_r.HudRect(prevCx - 1.f, stem0, 2.f, stem1 - stem0, RGBA8(210, 176, 90, 220));
				BOOL show = by >= viewTop - 1.f && by + btnH <= viewBot + 1.f;
				for (int i = 0; i < n; i++) {
					float cx = rowX + i * (childW + gapb);
					float ccx = cx + childW * .5f;
					if (show) {
						float drop0 = barY < viewTop ? viewTop : barY;
						float drop1 = by > viewBot ? viewBot : by;
						if (drop1 > drop0) m_r.HudRect(ccx - 1.f, drop0, 2.f, drop1 - drop0, RGBA8(210, 176, 90, 220));
						BOOL on = rowPick[r] == i;
						armTip(rowOpt[r][i], cx, by, childW, btnH, ID_GROW_NODE0 + r * 4 + i);
						if (Btn(ID_GROW_NODE0 + r * 4 + i, cx, by, childW, btnH, ClassName(rowOpt[r][i]), TRUE, on, U(14))) {
							m_growPath[r + 1] = rowOpt[r][i];
							m_growN = r + 2;
							if (m_growN > 14) m_growN = 14;
						}
					}
				}
				if (rowPick[r] >= 0) prevCx = rowX + rowPick[r] * (childW + gapb) + childW * .5f;
				else prevCx = rowX + childW * .5f;
				prevBot = by + btnH;
			}
			if (growTip) DrawHoverNote(m_r, gax, gay, gaw, gah, m_ui, m_w, m_h, growTip);
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
				GainItemN(m_save, rc.out, rc.on);
				Persist();
			}
			yy += U(42);
		}
	}
}

void Game::DrawShopArm()
{
	m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 150));
	float pw = min(U(980.f), (float)m_w - U(40));
	float ph = (float)m_h - U(32);
	float px = ((float)m_w - pw) * .5f, py = ((float)m_h - ph) * .5f;
	if (m_click && !Hit(px, py, pw, ph)) { m_shopFit = 0; m_click = FALSE; TacSfx(TS_CANCEL); return; }
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
	if (m_click && !Hit(px, py, pw, ph)) { m_shopHold = 0; m_shopFit = 0; m_click = FALSE; TacSfx(TS_CANCEL); return; }
	Panel(px, py, pw, ph);
	wchar_t line[160];
	line[0] = 0;
	BOOL ok = FALSE;
	if (m_shopFit == 6) {
		const GoodDef* gd = GoodAt(m_shopFitId);
		if (gd) {
			swprintf_s(line, L"%s  %sG", TT(gd->nameTx), TacNum(gd->price));
			ok = m_save.gold >= gd->price && GoodHave(m_save, m_shopFitId) < GoodHoldMax(m_shopFitId);
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
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
		m_shopFit = 0;
		return;
	}
	if (Btn(ID_SHOP_ASK_NO, px + U(24) + bw, py + ph - U(64), bw, U(40), TT(TX_PT_NO))) {
		m_shopHold = 0;
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
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
		if (m_click && !Hit(px, py, pw, ph)) { m_shopFit = 4; m_click = FALSE; TacSfx(TS_CANCEL); return; }
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
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
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
	if (m_click && !Hit(px, py, pw, ph)) { m_shopFit = 0; m_shopHold = 0; m_click = FALSE; TacSfx(TS_CANCEL); return; }
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
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
					m_shopFitId = stash;
				}
				GearEquip(c, m_save, stash);
				done = TRUE;
			} else if (m_shopFit == 3) {
				if (m_shopHold) {
					if (!AccBuy(m_save, m_shopFitId - 1)) { TacSfx(TS_ERROR); return; }
					m_shopHold = 0;
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
				}
				int slot = EquippedAccId(c, 0) > 0 && EquippedAccId(c, 1) <= 0 ? 1 : 0;
				done = EquipAccId(c, slot, m_shopFitId);
			} else if (m_shopFit == 11) {
				if (m_shopHold) {
					if (!PasBuy(m_save, m_shopFitId)) { TacSfx(TS_ERROR); return; }
					m_shopHold = 0;
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
				}
				int slot = EquippedPasId(c, 0) > 0 && EquippedPasId(c, 1) <= 0 ? 1 : 0;
				done = EquipPasId(c, slot, m_shopFitId + 1);
			} else if (m_shopFit == 12) {
				if (m_shopHold) {
					if (SummonSpare(c, 0, m_shopFitId) < 1 && SummonSpare(c, 1, m_shopFitId) < 1) {
						if (!SummonBuy(m_save, m_shopFitId)) { TacSfx(TS_ERROR); return; }
					}
					m_shopHold = 0;
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
				}
				int slot = EquippedSummonId(c, 0) > 0 && EquippedSummonId(c, 1) <= 0 ? 1 : 0;
				done = EquipSummonId(c, slot, m_shopFitId + 1);
			} else if (IsCraftSuper(CRAFT_BASE + m_shopFitId)) {
				if (m_shopHold) {
					if (!BeadBuy(m_save, m_shopFitId)) { TacSfx(TS_ERROR); return; }
					m_shopHold = 0;
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
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
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
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
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
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

static int CommitSell(SaveData& s, int kind, int id, int n)
{
	if (n < 1) return 0;
	int sold = 0;
	for (int k = 0; k < n; k++) {
		BOOL ok = FALSE;
		if (kind == 5) ok = BeadSell(id);
		else if (kind == 6) {
			int back = max(1, PasPrice(id) / 2);
			if (PasSell(id)) { s.gold += back; ok = TRUE; }
		} else if (kind == 7) {
			int back = max(1, SummonPrice(id) / 2);
			if (SummonSell(id)) { s.gold += back; ok = TRUE; }
		} else if (kind == 4) {
			int back = max(1, AccPrice(id) / 2);
			if (AccSell(id)) { s.gold += back; ok = TRUE; }
		} else if (kind == 2) {
			const GoodDef* gd = GoodAt(id);
			if (gd && GoodTake(s, id)) { s.gold += max(1, gd->price / 2); ok = TRUE; }
		} else if (kind == 8) {
			int found = -1;
			for (int i = s.gearN - 1; i >= 0; i--) if (s.gearId[i] == id) { found = i; break; }
			if (found >= 0) ok = GearSellAt(s, found);
		}
		if (!ok) break;
		sold++;
	}
	return sold;
}

void Game::DrawShop()
{
	if (!m_sellAsk && m_textFocus == 3) m_textFocus = 0;
	if (m_sellGo && m_sellAsk) {
		m_sellGo = 0;
		int n = CommitSell(m_save, m_sellKind, m_sellId, m_sellN);
		m_sellAsk = 0;
		m_textFocus = 0;
		if (n > 0) { TacSfx(TS_OK_SELL); Persist(); }
		else TacSfx(TS_ERROR);
		return;
	}
	BOOL ask = m_sellAsk != 0;
	BOOL fit = m_shopFit > 0 || m_shopHold > 0;
	BOOL savedClick = m_click;
	int savedAct = m_activateId;
	if (fit || ask) { m_click = FALSE; m_activateId = -1; }
	float w = min((float)m_w - U(40), U(980)), h = (float)m_h - U(30);
	float x = ((float)m_w - w) * .5f, y = U(16);
	Panel(x, y, w, h);
	wchar_t title[96];
	swprintf_s(title, L"%s    %s %s", TT(TX_SHOP), TT(TX_GOLD), TacNum(m_save.gold));
	m_r.HudText(title, x + w * .5f, y + U(22), FitTextPx(title, w - U(24), U(20), 1), RGBA8(255, 226, 150), 1, 1);
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
	const float descH = U(96);
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
	wchar_t hovHold[520], hovBuf[520];
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
			swprintf_s(row, L"%s  %sG  x%d", AccName(ids[ix] + 1), TacNum(AccPrice(ids[ix])), AccHave(ids[ix]));
			ok = m_save.gold >= AccPrice(ids[ix]) && AccHave(ids[ix]) < TAC_HOLD_WIDE;
			{
				AccTipText(ids[ix] + 1, hovBuf, 520);
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
			ok = open && StoneCount() >= cost && have < TAC_HOLD_BYTE;
			takeHover(ID_SHOP_BUY0 + s, yy, CraftDesc(ids[ix]));
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, row, ok, FALSE, U(13))) {
				m_shopHold = 1; m_shopFit = 4; m_shopFitId = ids[ix]; m_shopFitPage = 0;
				TacSfx(TS_OPEN); return;
			}
		} else if (m_shopKind == 6) {
			swprintf_s(row, L"%s  %sG  x%d", PasName(ids[ix] + 1), TacNum(PasPrice(ids[ix])), PasHave(ids[ix]));
			ok = m_save.gold >= PasPrice(ids[ix]) && PasHave(ids[ix]) < TAC_HOLD_BYTE;
			takeHover(ID_SHOP_BUY0 + s, yy, PasDesc(ids[ix] + 1));
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, row, ok, FALSE, U(13))) {
				m_shopHold = 1; m_shopFit = 11; m_shopFitId = ids[ix]; m_shopFitPage = 0;
				TacSfx(TS_OPEN); return;
			}
		} else if (m_shopKind == 7) {
			swprintf_s(row, L"%s  %sG  x%d", SummonName(ids[ix] + 1), TacNum(SummonPrice(ids[ix])), SummonHave(ids[ix]));
			ok = m_save.gold >= SummonPrice(ids[ix]) && SummonHave(ids[ix]) < TAC_HOLD_BYTE;
			takeHover(ID_SHOP_BUY0 + s, yy, SummonDesc(ids[ix] + 1));
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, row, ok, FALSE, U(13))) {
				m_shopHold = 1; m_shopFit = 12; m_shopFitId = ids[ix]; m_shopFitPage = 0;
				TacSfx(TS_OPEN); return;
			}
		} else if (m_shopKind == 2) {
			const GoodDef* gd = GoodAt(ids[ix]);
			swprintf_s(row, L"%s  %sG  x%d", TT(gd->nameTx), TacNum(gd->price), GoodHave(m_save, ids[ix]));
			ok = m_save.gold >= gd->price && GoodHave(m_save, ids[ix]) < GoodHoldMax(ids[ix]);
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
			{
				wchar_t tip[400];
				GearTipText(g, tip, 400);
				swprintf_s(hovBuf, L"%s%s%s", GearName(ids[ix] + 1), tip[0] ? L"\n" : L"", tip);
				takeHover(ID_SHOP_BUY0 + s, yy, hovBuf);
			}
			if (Btn(ID_SHOP_BUY0 + s, x + U(16), yy, w * .62f, rowH, L"", ok, FALSE, U(13))) {
				m_shopHold = 1; m_shopFit = g->kind ? 2 : 1; m_shopFitId = ids[ix]; m_shopFitPage = 0;
				TacSfx(TS_OPEN); return;
			}
			m_r.HudText(row, x + U(16) + w * .31f, yy + rowH * .34f, FitTextPx(row, w * .62f - U(16), U(13), 1),
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
			int whoC[320], whoT[320], nw = 0;
			int need = g->req > 0 ? g->req : 1;
			auto pushWho = [&](int c) {
				if (nw >= 320 || ClassLine(c) != g->line) return;
				int t = ClassTier(c);
				if (t < need) return;
				whoC[nw] = c; whoT[nw] = t; nw++;
			};
			for (int c = 0; c < C_PLAYER_COUNT; c++) pushWho(c);
			for (int c = C_PLAYER_COUNT; c < C_PLAYER_COUNT + TAC_RANK_LINES * TAC_RANK_N; c++) pushWho(c);
			pushWho(C_GLADIATOR);
			pushWho(C_SHADOW);
			for (int i = 0; i < JobCount(); i++) pushWho(C_JOB0 + i);
			auto tierKey = [&](int t) { return t == need ? 0 : t; };
			for (int i = 1; i < nw; i++) {
				int vc = whoC[i], vt = whoT[i], j = i;
				while (j > 0 && (tierKey(whoT[j - 1]) > tierKey(vt) || (tierKey(whoT[j - 1]) == tierKey(vt) && whoC[j - 1] > vc))) {
					whoC[j] = whoC[j - 1]; whoT[j] = whoT[j - 1]; j--;
				}
				whoC[j] = vc; whoT[j] = vt;
			}
			for (int i = 0; i < nw; i++) if (!addUser(whoC[i])) break;
			if (users[0])
				m_r.HudText(users, x + U(16) + w * .31f, yy + rowH * .74f, FitTextPx(users, w * .62f - U(16), U(11), 0), RGBA8(186, 198, 220), 1, 1);
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
	// 売却ボタンは 250..265。266 と 267 はページ送り。購入の 270 とは重ならない。
	const int sellIdRows = 16;
	int fitSell = (int)((sellBot - sy) / U(34));
	if (fitSell < 1) fitSell = 1;
	if (fitSell > sellIdRows) fitSell = sellIdRows;
	int sellIds[CRAFT_N];
	int on = 0;
	if (m_shopKind == 5) {
		for (int i = 0; i < CRAFT_N && on < CRAFT_N; i++) if (BeadFree(i) > 0) sellIds[on++] = i;
	} else if (m_shopKind == 6) {
		for (int i = 0; i < PasCount() && on < CRAFT_N; i++) if (PasFree(i) > 0) sellIds[on++] = i;
	} else if (m_shopKind == 7) {
		for (int i = 0; i < SummonCount() && on < CRAFT_N; i++) if (SummonFree(i) > 0) sellIds[on++] = i;
	} else if (m_shopKind == 4) {
		for (int i = 0; i < AccCount() && on < CRAFT_N; i++) if (AccFree(i) > 0) sellIds[on++] = i;
	} else if (m_shopKind == 2) {
		for (int i = 0; i < GoodCount() && on < CRAFT_N; i++) if (GoodHave(m_save, i) > 0) sellIds[on++] = i;
	} else if (m_shopKind == 0 || m_shopKind == 1) {
		int seen[GEAR_STASH], sn = 0;
		for (int i = 0; i < m_save.gearN && i < GEAR_STASH && on < CRAFT_N; i++) {
			int gid = m_save.gearId[i];
			if (!GearOf(gid)) continue;
			int dup = 0;
			for (int k = 0; k < sn; k++) if (seen[k] == gid) dup = 1;
			if (dup) continue;
			if (sn < GEAR_STASH) seen[sn++] = gid;
			sellIds[on++] = gid;
		}
	}
	int sp = fitSell;
	int spages = on < 1 ? 1 : max(1, (on + sp - 1) / sp);
	if (spages > 1 && fitSell > 1) {
		sp = fitSell - 1;
		spages = max(1, (on + sp - 1) / sp);
	}
	if (m_shopSellPage >= spages) m_shopSellPage = spages - 1;
	if (m_shopSellPage < 0) m_shopSellPage = 0;
	int sa = m_shopSellPage * sp, sb = min(on, sa + sp);
	for (int s = sa; s < sb; s++) {
		int id = sellIds[s];
		wchar_t row[96];
		if (m_shopKind == 5) {
			int back = max(1, CraftStoneCost(id) / 2);
			swprintf_s(row, L"%s x%d  %s%d", CraftName(id), BeadFree(id), TT(TX_STONE), back);
		} else if (m_shopKind == 6) {
			swprintf_s(row, L"%s x%d  %sG", PasName(id + 1), PasFree(id), TacNum(max(1, PasPrice(id) / 2)));
			if (Hit(x + w * .66f, sy, w * .32f, U(30)) || (m_navMode && m_focusId == ID_SHOP_SELL0 + (s - sa))) {
				wcscpy_s(hovHold, PasDesc(id + 1));
				hov = hovHold;
			}
		} else if (m_shopKind == 7) {
			swprintf_s(row, L"%s x%d  %sG", SummonName(id + 1), SummonFree(id), TacNum(max(1, SummonPrice(id) / 2)));
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
		} else if (m_shopKind == 4) {
			int free = AccFree(id);
			swprintf_s(row, L"%s x%d  %sG", AccName(id + 1), free, TacNum(max(1, AccPrice(id) / 2)));
			if (Hit(x + w * .66f, sy, w * .32f, U(30)) || (m_navMode && m_focusId == ID_SHOP_SELL0 + (s - sa))) {
				AccTipText(id + 1, hovHold, 520);
				hov = hovHold;
			}
		} else {
			const GearDef* g = GearOf(id);
			int ncopy = 0;
			for (int i = 0; i < m_save.gearN; i++) if (m_save.gearId[i] == id) ncopy++;
			swprintf_s(row, L"%s x%d  %sG", GearName(id), ncopy, TacNum(g ? max(1, g->price / 2) : 1));
			if (g && (Hit(x + w * .66f, sy, w * .32f, U(30)) || (m_navMode && m_focusId == ID_SHOP_SELL0 + (s - sa)))) {
				wchar_t tip[360];
				GearTipText(g, tip, 360);
				swprintf_s(hovHold, L"%s%s%s", GearName(id), tip[0] ? L"\n" : L"", tip);
				hov = hovHold;
			}
		}
		m_btnSfx = -2;
		if (Btn(ID_SHOP_SELL0 + (s - sa), x + w * .66f, sy, w * .32f, U(30), row, TRUE, FALSE, U(12))) {
			int sellKind = m_shopKind;
			int maxN = 1;
			if (m_shopKind == 5) maxN = BeadFree(id);
			else if (m_shopKind == 6) maxN = PasFree(id);
			else if (m_shopKind == 7) maxN = SummonFree(id);
			else if (m_shopKind == 4) maxN = AccFree(id);
			else if (m_shopKind == 2) maxN = GoodHave(m_save, id);
			else {
				sellKind = 8;
				maxN = 0;
				for (int i = 0; i < m_save.gearN; i++) if (m_save.gearId[i] == id) maxN++;
			}
			if (maxN < 1) { TacSfx(TS_ERROR); return; }
			if (maxN == 1) {
				if (CommitSell(m_save, sellKind, id, 1) > 0) { TacSfx(TS_OK_SELL); Persist(); return; }
				TacSfx(TS_ERROR);
			} else {
				m_sellAsk = 1;
				m_sellKind = sellKind;
				m_sellId = id;
				m_sellMax = maxN;
				m_sellN = 1;
				m_sellFresh = 1;
				m_textFocus = 3;
				TacSfx(TS_OPEN);
				return;
			}
		}
		sy += U(34);
	}
	if (spages > 1) {
		wchar_t spg[32];
		swprintf_s(spg, L"%d / %d", m_shopSellPage + 1, spages);
		if (Btn(ID_SHOP_SELL0 + 16, x + w * .66f, sy, U(36), U(28), L"◀")) m_shopSellPage = (m_shopSellPage + spages - 1) % spages;
		m_r.HudText(spg, x + w * .66f + U(70), sy + U(14), U(13), RGBA8(230, 230, 236), 0, 1);
		if (Btn(ID_SHOP_SELL0 + 17, x + w * .66f + U(110), sy, U(36), U(28), L"▶")) m_shopSellPage = (m_shopSellPage + 1) % spages;
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
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
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
	} else if (ask) {
		m_click = savedClick;
		m_activateId = savedAct;
		float pw = min(U(460.f), w - U(24));
		float ph = min(U(220.f), h - U(24));
		float px = x + (w - pw) * .5f, py = y + (h - ph) * .5f;
		m_r.HudRect(x, y, w, h, RGBA8(0, 0, 0, 120));
		Panel(px, py, pw, ph);
		const wchar_t* nm = L"";
		if (m_sellKind == 5) nm = CraftName(m_sellId);
		else if (m_sellKind == 6) nm = PasName(m_sellId + 1);
		else if (m_sellKind == 7) nm = SummonName(m_sellId + 1);
		else if (m_sellKind == 4) nm = AccName(m_sellId + 1);
		else if (m_sellKind == 2) { const GoodDef* gd = GoodAt(m_sellId); nm = gd ? TT(gd->nameTx) : L""; }
		else nm = GearName(m_sellId);
		m_r.HudText(nm, px + pw * .5f, py + U(28), U(16), RGBA8(255, 226, 150), 1, 1);
		wchar_t num[32];
		if (m_textFocus == 3) {
			TacMarkBox(m_textBox, px + U(80), py + U(48), pw - U(160), U(32));
			m_textPx = U(22);
		}
		swprintf_s(num, L"%d / %d", m_sellN, m_sellMax);
		m_r.HudText(num, px + pw * .5f, py + U(64), U(22), RGBA8(255, 255, 255), 1, 1);
		float bw = (pw - U(48)) / 5.f;
		float by = py + U(92);
		auto bump = [&](int d) {
			m_sellN += d;
			if (m_sellN < 1) m_sellN = 1;
			if (m_sellN > m_sellMax) m_sellN = m_sellMax;
			m_sellFresh = 0;
		};
		if (Btn(ID_SELL_M10, px + U(16), by, bw - U(4), U(32), L"-10")) bump(-10);
		if (Btn(ID_SELL_M1, px + U(16) + bw, by, bw - U(4), U(32), L"-1")) bump(-1);
		if (Btn(ID_SELL_P1, px + U(16) + bw * 2, by, bw - U(4), U(32), L"+1")) bump(1);
		if (Btn(ID_SELL_P10, px + U(16) + bw * 3, by, bw - U(4), U(32), L"+10")) bump(10);
		if (Btn(ID_SELL_MAX, px + U(16) + bw * 4, by, bw - U(4), U(32), L"MAX")) { m_sellN = m_sellMax; m_sellFresh = 0; }
		float by2 = py + ph - U(52);
		float bw2 = (pw - U(40)) * .5f;
		if (Btn(ID_SELL_OK, px + U(16), by2, bw2 - U(6), U(36), TT(TX_SHOP_SELL))) {
			int n = CommitSell(m_save, m_sellKind, m_sellId, m_sellN);
			m_sellAsk = 0;
			m_textFocus = 0;
			if (n > 0) { TacSfx(TS_OK_SELL); Persist(); return; }
			TacSfx(TS_ERROR);
			return;
		}
		if (Btn(ID_SELL_NO, px + U(16) + bw2, by2, bw2 - U(6), U(36), TT(TX_CR_BACK))) { m_sellAsk = 0; m_textFocus = 0; }
		if (m_click && !Hit(px, py, pw, ph)) { m_sellAsk = 0; m_textFocus = 0; m_click = FALSE; }
	}
}

static void KeepAppSettings(SaveData& dst, const SaveData& src)
{
	dst.winX = src.winX; dst.winY = src.winY; dst.winW = src.winW; dst.winH = src.winH;
	dst.winSizeVer = src.winSizeVer;
	dst.camSpeed = src.camSpeed;
	dst.quality = src.quality;
	dst.spare[0] = src.spare[0];
	dst.spare[1] = src.spare[1];
	dst.spare[2] = src.spare[2];
	dst.spare[3] = src.spare[3];
	dst.spare[4] = src.spare[4];
	dst.spare[5] = src.spare[5];
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
	m_qHold = -1;
	ApplyQualityNow(TacQualityWanted(m_save));
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
	AddBanner(sn, TT(TX_SLOT), RGBA8(255, 226, 150), 0, 1.6f, MSG_FACE);
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
	AddBanner(TT(TX_SLOT_COPIED), dn, RGBA8(255, 226, 150), 0, 1.6f, MSG_RISE);
}

static void SlotFileTitle(wchar_t* out, int n, int slot)
{
	wchar_t nm[TAC_SLOT_NAME];
	SlotTitle(slot, nm, TAC_SLOT_NAME);
	for (wchar_t* p = nm; *p; p++)
		if (wcschr(L"\\/:*?\"<>|", *p)) *p = L'_';
	if (!nm[0]) wcsncpy_s(nm, L"slot", _TRUNCATE);
	swprintf_s(out, n, L"%s.tacx", nm);
}

void Game::RunSlotFile(int job)
{
	CWnd* parent = CWnd::FromHandle(::GetForegroundWindow());
	if (job == 1) {
		int slot = m_slotDest;
		if (slot < 0 || slot >= TAC_SLOT_N) return;
		if (slot == SlotCurrent()) FlushCurrentSlot();
		else WaitSuspend();
		wchar_t def[MAX_PATH];
		SlotFileTitle(def, MAX_PATH, slot);
		CFileDialog dlg(FALSE, L"tacx", def, OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_EXPLORER,
			L"Tactics (*.tacx)|*.tacx||", parent);
		if (dlg.DoModal() != IDOK) return;
		if (SlotExportTo(slot, dlg.GetPathName())) {
			wchar_t sn[TAC_SLOT_NAME];
			SlotTitle(slot, sn, TAC_SLOT_NAME);
			AddBanner(TT(TX_SLOT_OUT_OK), sn, RGBA8(255, 226, 150), 0, 1.8f, MSG_RISE);
			m_slotUi = 1;
		} else { TacSfx(TS_ERROR); AddBanner(TT(TX_SLOT_IO_BAD), NULL, RGBA8(255, 160, 140), 0, 1.8f, MSG_DROP); }
	} else if (job == 2) {
		CFileDialog dlg(TRUE, L"tacx", NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER,
			L"Tactics (*.tacx)|*.tacx||", parent);
		if (dlg.DoModal() != IDOK) return;
		wcsncpy_s(m_impPath, dlg.GetPathName(), _TRUNCATE);
		m_slotUi = 8;
		m_slotDest = -1;
		m_focusId = ID_SLOT_ROW0;
	}
}

void Game::ImportConfirmed()
{
	int slot = m_slotDest;
	if (slot < 0 || slot >= TAC_SLOT_N || !m_impPath[0]) { m_slotUi = 1; return; }
	BOOL battle = m_screen == SC_BATTLE || m_screen == SC_RESULT;
	BOOL cur = slot == SlotCurrent();
	if (cur) WaitSuspend();
	if (!SlotImportFrom(slot, m_impPath)) {
		TacSfx(TS_ERROR);
		AddBanner(TT(TX_SLOT_IO_BAD), NULL, RGBA8(255, 160, 140), 0, 1.8f, MSG_DROP);
		m_slotUi = 8;
		return;
	}
	m_impPath[0] = 0;
	if (cur) AdoptActiveSlot(battle);
	else RefreshSlotPeek();
	m_slotUi = 1;
	m_slotPick = slot;
	m_textFocus = 0;
	wchar_t sn[TAC_SLOT_NAME];
	SlotTitle(slot, sn, TAC_SLOT_NAME);
	AddBanner(TT(TX_SLOT_IN_OK), sn, RGBA8(255, 226, 150), 0, 1.8f, MSG_RISE);
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
	const wchar_t* title = TT(TX_SLOT);
	if (mode == 4) title = TT(TX_SLOT_COPYTO);
	else if (mode == 7) title = TT(TX_SLOT_OUTTO);
	else if (mode == 8) title = TT(TX_SLOT_INTO);
	m_r.HudText(title, x + w * .5f, y + U(28), U(20), RGBA8(255, 226, 150), 1, 1);
	BOOL picking = mode == 1 || mode == 4 || mode == 7 || mode == 8;
	if (picking) {
		if (mode == 4 || mode == 8) {
			wchar_t sub[120];
			if (mode == 4) {
				wchar_t sn[TAC_SLOT_NAME];
				SlotTitle(m_slotPick, sn, TAC_SLOT_NAME);
				swprintf_s(sub, L"%s  %s", TT(TX_SLOT_COPY), sn);
			} else {
				const wchar_t* slash = wcsrchr(m_impPath, L'\\');
				wchar_t fileCut[72];
				wcsncpy_s(fileCut, slash ? slash + 1 : m_impPath, _TRUNCATE);
				swprintf_s(sub, L"%s  %s", TT(TX_SLOT_IN), fileCut);
			}
			m_r.HudText(sub, x + w * .5f, y + U(52), U(14), RGBA8(220, 220, 230), 1, 0);
		}
		float btnH = U(38);
		float btnY = y + h - pad - btnH;
		float ioH = (mode == 1) ? (btnH + U(6)) : 0.f;
		float top = y + (mode == 1 || mode == 7 ? U(52) : U(70));
		float rowH = (btnY - ioH - U(8) - top) / (float)TAC_SLOT_N;
		if (rowH > U(44)) rowH = U(44);
		if (rowH < U(22)) rowH = U(22);
		for (int i = 0; i < TAC_SLOT_N; i++) {
			wchar_t nm[TAC_SLOT_NAME];
			SlotTitle(i, nm, TAC_SLOT_NAME);
			BOOL editing = m_textFocus == 2 && i == m_slotPick && mode == 1;
			wchar_t info[48];
			if (m_slotClear[i] < 0) wcsncpy_s(info, TT(TX_SLOT_EMPTY), _TRUNCATE);
			else StageCaption(m_slotClear[i], info, 48);
			wchar_t shown[64];
			if (editing) {
				TacMarkBox(m_textBox, x + pad, top + rowH * i, w - pad * 2.f, rowH - U(3));
				m_textPx = U(15);
				TacCaretStr(shown, 64, m_slotEdit, m_textCaret, m_imeComp, fmodf(m_time, 1.f) < .5f);
			} else wcsncpy_s(shown, nm, _TRUNCATE);
			wchar_t line[160];
			swprintf_s(line, L"%d   %s    %s%s%s", i + 1, shown,
				info, (i == SlotCurrent()) ? L"    " : L"", (i == SlotCurrent()) ? TT(TX_SLOT_NOW) : L"");
			BOOL en = !(mode == 4 && i == m_slotPick);
			BOOL sel = (mode == 1) ? (i == m_slotPick) : FALSE;
			if (Btn(ID_SLOT_ROW0 + i, x + pad, top + rowH * i, w - pad * 2.f, rowH - U(3), line, en, sel, U(15))) {
				if (mode == 4) {
					m_slotDest = i;
					m_slotUi = 5;
					m_focusId = ID_SLOT_NO;
				} else if (mode == 7) {
					m_slotDest = i;
					m_slotJob = 1;
				} else if (mode == 8) {
					m_slotDest = i;
					m_slotUi = 6;
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
		if (mode == 4 || mode == 7 || mode == 8) {
			if (Btn(ID_SLOT_BACK, bx, btnY, bw, btnH, TT(TX_CR_BACK))) {
				if (mode == 8) m_impPath[0] = 0;
				m_slotUi = 1;
				m_textFocus = 0;
			}
		} else {
			float ioY = btnY - ioH;
			float ioW = (bw - gap) * .5f;
			int slotTip = -1;
			float slotAx = 0, slotAy = 0, slotAw = 0, slotAh = 0;
			auto slotMark = [&](int id, int tx, float rx, float ry, float rw, float rh) {
				BOOL on = (m_mouseSeen && !m_navMode && Hit(rx, ry, rw, rh)) || (m_navMode && m_focusId == id);
				if (on) { slotTip = tx; slotAx = rx; slotAy = ry; slotAw = rw; slotAh = rh; }
			};
			slotMark(ID_SLOT_OUT, TX_SLH_OUT, bx, ioY, ioW, btnH);
			slotMark(ID_SLOT_IN, TX_SLH_IN, bx + ioW + gap, ioY, ioW, btnH);
			if (Btn(ID_SLOT_OUT, bx, ioY, ioW, btnH, TT(TX_SLOT_OUT), TRUE, FALSE, U(14))) {
				if (m_textFocus == 2) CommitSlotName();
				m_slotUi = 7;
				m_slotDest = -1;
				m_focusId = ID_SLOT_ROW0;
			}
			if (Btn(ID_SLOT_IN, bx + ioW + gap, ioY, ioW, btnH, TT(TX_SLOT_IN), TRUE, FALSE, U(14))) {
				if (m_textFocus == 2) CommitSlotName();
				m_slotJob = 2;
			}
			float bw5 = (bw - gap * 4.f) / 5.f;
			BOOL cur = m_slotPick == SlotCurrent();
			slotMark(ID_SLOT_USE, TX_SLH_USE, bx, btnY, bw5, btnH);
			slotMark(ID_SLOT_REN, TX_SLH_REN, bx + (bw5 + gap), btnY, bw5, btnH);
			slotMark(ID_SLOT_COPY, TX_SLH_COPY, bx + (bw5 + gap) * 2.f, btnY, bw5, btnH);
			slotMark(ID_SLOT_DEL, TX_SLH_DEL, bx + (bw5 + gap) * 3.f, btnY, bw5, btnH);
			if (Btn(ID_SLOT_USE, bx, btnY, bw5, btnH, cur ? TT(TX_SLOT_NOW) : TT(TX_SLOT_USE), !cur, FALSE, U(14))) {
				if (m_textFocus == 2) CommitSlotName();
				SwitchToSlot(m_slotPick);
			}
			if (Btn(ID_SLOT_REN, bx + (bw5 + gap), btnY, bw5, btnH, TT(TX_SLOT_REN), TRUE, m_textFocus == 2, U(14))) {
				SlotTitle(m_slotPick, m_slotEdit, TAC_SLOT_NAME);
				m_textFocus = 2;
				m_textCaret = TacWLen(m_slotEdit);
				m_imeComp[0] = 0;
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
			if (slotTip >= 0)
				DrawHoverNote(m_r, slotAx, slotAy, slotAw, slotAh, m_ui, m_w, m_h, TT(slotTip));
		}
	} else {
		wchar_t src[TAC_SLOT_NAME], dst[TAC_SLOT_NAME], head[180];
		SlotTitle(m_slotPick, src, TAC_SLOT_NAME);
		if (mode == 5) {
			SlotTitle(m_slotDest, dst, TAC_SLOT_NAME);
			swprintf_s(head, L"%s   →   %s", src, dst);
		} else if (mode == 6) {
			SlotTitle(m_slotDest, src, TAC_SLOT_NAME);
			const wchar_t* slash = wcsrchr(m_impPath, L'\\');
			wchar_t fileCut[72];
			wcsncpy_s(fileCut, slash ? slash + 1 : m_impPath, _TRUNCATE);
			swprintf_s(head, L"%s   ←   %s", src, fileCut);
		} else wcsncpy_s(head, src, _TRUNCATE);
		const wchar_t* body = TT(TX_SLOT_WARN1);
		if (mode == 3) body = TT(TX_SLOT_WARN2);
		else if (mode == 5) body = TT(TX_SLOT_COPYQ);
		else if (mode == 6) body = TT(TX_SLOT_IN_Q);
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
			else if (mode == 6) ImportConfirmed();
		}
		if (Btn(ID_SLOT_NO, x + pad + bw + U(10), by, bw, bh, TT(TX_PT_NO))) {
			m_slotUi = (mode == 5) ? 4 : (mode == 6) ? 8 : 1;
			m_focusId = ID_SLOT_BACK;
		}
	}
	m_click = FALSE;
	m_rclick = FALSE;
	m_activateId = -1;
}

void Game::DrawPerfHud()
{
	if (!TacFpsOn(m_save)) return;
	if (m_screen == SC_LOADING || m_screen == SC_FAIL) return;
	if (m_fps < 1.f) return;
	float fps = m_fps;
	if (fps > 999.f) fps = 999.f;
	float ms = 1000.f / fps;
	if (ms > 999.f) ms = 999.f;
	int fps10 = (int)(fps * 10.f + 0.5f);
	int ms10 = (int)(ms * 10.f + 0.5f);
	if (fps10 > 9999) fps10 = 9999;
	if (ms10 > 9999) ms10 = 9999;
	int q = QualityNow();
	const wchar_t* mode = LangPick14(L"モード", L"mode", L"mode", L"modo", L"modo", L"모드", L"模式", L"وضع", L"режим", L"Modus", L"modo", L"modus", L"tryb", L"mod");
	wchar_t line[220];
	if (m_save.quality == TAC_Q_AUTO)
		swprintf_s(line, L"%d.%dfps %d.%dms 1/%d%s %s %s", fps10 / 10, fps10 % 10, ms10 / 10, ms10 % 10, PaceHz(), mode, TT(TX_SET_Q_AUTO), TT(TacQualityTx(q)));
	else
		swprintf_s(line, L"%d.%dfps %d.%dms 1/%d%s %s", fps10 / 10, fps10 % 10, ms10 / 10, ms10 % 10, PaceHz(), mode, TT(TacQualityTx(q)));
	float px = U(13);
	if (px < 11.f) px = 11.f;
	float pad = U(8);
	float tw = m_r.TextWidth(line, px, 0);
	float th = px + U(8);
	float bw = tw + pad * 2.f;
	float x = (float)m_w - bw - U(10);
	float y = U(8);
	if (m_screen == SC_BATTLE && m_hud.col > 80.f) {
		float gapTop = m_orderBottom + U(6);
		float gapBot = (float)m_h - m_hud.margin;
		if (m_miniHitN > 0) {
			float topMini = m_miniY[0];
			for (int i = 1; i < m_miniHitN && i < 3; i++) if (m_miniY[i] < topMini) topMini = m_miniY[i];
			gapBot = topMini - U(4);
		}
		float colX = m_hud.orderX;
		float colW = m_hud.orderW > 8.f ? m_hud.orderW : (m_hud.col > 0.f ? (float)m_w - m_hud.col : bw);
		if (gapBot - gapTop >= th + 2.f && colW >= 48.f) {
			float fit = colW - U(4);
			if (bw > fit && fit > 36.f) {
				px = px * fit / bw;
				if (px < 9.f) px = 9.f;
				tw = m_r.TextWidth(line, px, 0);
				th = px + U(6);
				bw = tw + pad * 2.f;
			}
			x = colX + max(0.f, (colW - bw) * 0.5f);
			y = gapTop;
			if (x + bw > colX + colW - 2.f) x = colX + colW - bw - 2.f;
		} else {
			x = m_hud.margin;
			y = m_hud.panelTop - th - U(6);
			if (y < m_hud.partyB + U(4)) y = m_hud.partyB + U(4);
			float right = m_hud.col - U(8);
			if (x + bw > right) {
				px = max(9.f, px * (right - x) / max(bw, 1.f));
				tw = m_r.TextWidth(line, px, 0);
				th = px + U(6);
				bw = tw + pad * 2.f;
				y = m_hud.panelTop - th - U(6);
				if (y < m_hud.partyB + U(4)) y = m_hud.partyB + U(4);
			}
			if (y + th > m_hud.panelTop - 2.f) {
				x = m_hud.stageR + U(6);
				y = m_hud.margin;
				if (x + bw > m_hud.col - U(4)) x = max(m_hud.margin, m_hud.col - U(4) - bw);
			}
		}
	} else if (m_screen == SC_BASE && m_homeUi && !m_mapLook && !m_shop && !m_codex && !m_slotUi && m_jobRedo != 1 && m_jobRedo != 3) {
		float margin = max(8.f, min(U(20), (float)m_w * .02f));
		float pw = min(U(330), (float)m_w - margin * 2.f);
		float ph = min((float)m_h - margin * 2.f, (float)m_h - 8.f);
		float titleH = min(U(24), ph * .045f);
		float titlePx = FitTextPx(TT(TX_TITLE), pw - U(20), titleH, 1);
		float titleCy = margin + titleH + 8.f;
		float titleW = m_r.TextWidth(TT(TX_TITLE), titlePx, 1);
		float titleR = margin + pw * .5f + titleW * .5f + U(8);
		float right = margin + pw - U(8);
		float free = right - titleR;
		if (free < bw && free > 48.f) {
			px = max(9.f, px * free / bw);
			tw = m_r.TextWidth(line, px, 0);
			th = px + U(6);
			bw = tw + pad * 2.f;
		}
		if (bw <= free) {
			x = right - bw;
			y = titleCy - th * .5f;
		} else {
			x = margin + U(8);
			y = titleCy + titleH * .5f + U(2);
			float btnTop = margin + titleH * 2.f + 14.f;
			if (y + th > btnTop - 2.f) y = max(titleCy + titleH * .5f + 1.f, btnTop - th - 2.f);
		}
	}
	if (x < U(4)) x = U(4);
	if (y < U(4)) y = U(4);
	if (x + bw > (float)m_w - U(4)) x = (float)m_w - U(4) - bw;
	if (y + th > (float)m_h - U(4)) y = (float)m_h - U(4) - th;
	Panel(x, y, bw, th, 0.82f);
	m_r.HudText(line, x + pad + tw, y + th * .5f, px, RGBA8(236, 238, 244), 2, 0);
}

void Game::DrawBaseUi()
{
	if (m_jobRedo == 1 || m_jobRedo == 3) { DrawJobGate(); return; }
	if (m_slotUi) { DrawSlotUi(); return; }
	if (m_codex) { DrawCodex(); return; }
	if (m_shop) { DrawShop(); return; }
	if (m_mapLook) {
		float bw = min(U(160.f), max(72.f, (float)m_w * .22f));
		float bh = min(U(40.f), max(28.f, (float)m_h * .07f));
		float bx = max(12.f, U(16.f));
		float by = (float)m_h - bh - max(12.f, U(16.f));
		if (Btn(ID_BASE_LOOK_BACK, bx, by, bw, bh, TT(TX_CR_BACK))) {
			m_mapLook = 0;
			m_showYaw = m_camYaw;
			return;
		}
		float hx = bx + bw + 12.f;
		float hw = (float)m_w - hx - 16.f;
		if (hw > 80.f)
			m_r.HudTextBox(TT(TX_MAP_LOOK_HINT), hx, by, hw, bh, min(U(14.f), bh * .36f), RGBA8(236, 236, 240));
		return;
	}
	if (!m_homeUi) return;
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
	float titlePx = FitTextPx(TT(TX_TITLE), pw - U(20), titleH, 1);
	m_r.HudText(TT(TX_TITLE), x + pw * .5f, y + titleH + 8.f, titlePx, RGBA8(255, 226, 150), 1, 1);
	float helpH = min(U(40), max(24.f, ph * .07f));
	float helpY = y + ph - helpH - max(6.f, ph * .015f);
	float top = y + titleH * 2.f + 14.f;
	float midRoom = helpY - top - 4.f;
	float midNeed = U(470);
	float k = midNeed > 1.f ? min(1.f, midRoom / midNeed) : 1.f;
	if (k < .5f) k = .5f;
	auto Ku = [&](float px) { return U(px) * k; };
	float by = top;
	float bw = pw - max(16.f, pw * .1f), bh = Ku(44), bx = x + (pw - bw) * .5f;
	int tipTx = -1;
	float tipAx = 0, tipAy = 0, tipAw = 0, tipAh = 0;
	auto mark = [&](int id, int tx, float rowX, float rowY, float rowW, float rowH) {
		BOOL on = (m_mouseSeen && !m_navMode && Hit(rowX, rowY, rowW, rowH)) || (m_navMode && m_focusId == id);
		if (on) { tipTx = tx; tipAx = rowX; tipAy = rowY; tipAw = rowW; tipAh = rowH; }
	};
	wchar_t st[64];
	int shownTh = m_themeSel >= 0 ? m_themeSel : m_showMap.theme;
	{
		wchar_t cur[40], cap[40];
		StageText(m_save.stageSel, cur, 40);
		StageText(StageShownMax(m_save.stageCleared), cap, 40);
		swprintf_s(st, L"%s / %s   %s", cur, cap, TT(ThemeNameTx(shownTh)));
	}
	mark(ID_BASE_STAGE, TX_BSH_STAGE, bx, by, bw, Ku(40));
	int d = Sel(ID_BASE_STAGE, bx, by, bw, Ku(40), TT(TX_BASE_STAGE), st);
	if (d) {
		int step = m_keyShift ? 100 : 10;
		if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 && !m_keyShift) step = 1;
		int prev = m_save.stageSel;
		int mx = StagePlayMax(m_save.stageCleared);
		m_save.stageSel = Clampi(m_save.stageSel + d * step, 1, mx);
		if (m_save.stageSel != prev) {
			m_mapSeed = 0;
			m_mapSeedStage = -1;
			m_showBuilt = FALSE;
		}
	}
	by += Ku(46);
	wchar_t cl[96];
	int sumHp = 0, sumMp = 0, sumLv = 0, sumN = 0;
	int pcap = PartyCap();
	for (int i = 0; i < pcap; i++) {
		int r = m_save.party[i];
		if (r < 0 || r >= MAX_ROSTER || !m_save.roster[r].used) continue;
		const CharData& c = m_save.roster[r];
		sumHp += c.stat[S_HP];
		sumMp += c.stat[S_MP];
		sumLv += c.lv;
		sumN++;
	}
	float statPx = min(U(14), Ku(16));
	float statMax = bw - U(8);
	auto statLine = [&](const wchar_t* s, int tx) {
		float px = FitTextPx(s, statMax, statPx, 0);
		if (m_mouseSeen && Hit(bx, by, bw, Ku(18))) { tipTx = tx; tipAx = bx; tipAy = by; tipAw = bw; tipAh = Ku(18); }
		m_r.HudText(s, bx + U(4), by + px * 0.15f, px, RGBA8(200, 200, 210), 0, 0);
		by += Ku(18);
	};
	{
		wchar_t sn[40];
		StageText(m_save.stageCleared, sn, 40);
		swprintf_s(cl, L"%s: %s", TT(TX_BASE_CLEARED), sn);
	}
	statLine(cl, TX_BSH_CLEAR);
	swprintf_s(cl, L"%s  %s", TT(TX_GOLD), TacNum(m_save.gold));
	statLine(cl, TX_BSH_GOLD);
	swprintf_s(cl, L"%s %s   %s %s", TT(TX_ST_HP), TacNum(sumHp), TT(TX_ST_MP), TacNum(sumMp));
	statLine(cl, TX_BSH_SUM);
	if (sumN > 0) {
		int avg10 = (sumLv * 10 + sumN / 2) / sumN;
		swprintf_s(cl, L"%s  %d.%d", TT(TX_BASE_AVGLV), avg10 / 10, avg10 % 10);
	} else swprintf_s(cl, L"%s  —", TT(TX_BASE_AVGLV));
	statLine(cl, TX_BSH_AVGLV);
	int np = PartyCount();
	mark(ID_BASE_SORTIE, TX_BSH_SORTIE, bx, by, bw, bh + Ku(8));
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
	mark(ID_BASE_PARTY, TX_BSH_PARTY, bx, by, halfW, bh);
	mark(ID_BASE_SHOP, TX_BSH_SHOP, bx + halfW + gapb, by, halfW, bh);
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
	mark(ID_BASE_CREATE, TX_BSH_CREATE, bx, by, halfW, bh);
	mark(ID_BASE_EVOLVE, TX_BSH_EVOLVE, bx + halfW + gapb, by, halfW, bh);
	if (Btn(ID_BASE_CREATE, bx, by, halfW, bh, TT(TX_BASE_CREATE), RosterCount() < RosterCap(), FALSE, hfs)) { m_createFromParty = 0; EnterCreate(-1); return; }
	if (Btn(ID_BASE_EVOLVE, bx + halfW + gapb, by, halfW, bh, TT(TX_BASE_EVOLVE), RosterCount() > 0, FALSE, hfs)) { m_evFromParty = 0; EnterEvolve(); return; }
	by += bh + Ku(8);
	mark(ID_BASE_CODEX, TX_BSH_CODEX, bx, by, halfW, bh);
	mark(ID_BASE_SET, TX_BSH_SET, bx + halfW + gapb, by, halfW, bh);
	if (Btn(ID_BASE_CODEX, bx, by, halfW, bh, TT(TX_BASE_CODEX), TRUE, FALSE, hfs)) { m_codex = 1; m_codexPage = 0; return; }
	if (Btn(ID_BASE_SET, bx + halfW + gapb, by, halfW, bh, TT(TX_SET_OPEN), TRUE, m_baseOpt != 0, hfs)) m_baseOpt = !m_baseOpt;
	by += bh + Ku(8);
	{
		wchar_t sn[TAC_SLOT_NAME], sb[80];
		SlotTitle(SlotCurrent(), sn, TAC_SLOT_NAME);
		swprintf_s(sb, L"%s  %s", TT(TX_SLOT), sn);
		mark(ID_BASE_SAVESLOT, TX_BSH_SLOT, bx, by, bw, bh);
		if (Btn(ID_BASE_SAVESLOT, bx, by, bw, bh, sb, TRUE, FALSE, hfs)) { OpenSlotPanel(); return; }
	}
	by += bh + Ku(12);
	if (m_baseOpt) {
		const int nOpt = 13;
		float room = helpY - 8.f - by;
		if (room < 8.f) room = 8.f;
		float rowH = room / (float)nOpt;
		float rowCap = Ku(36);
		if (rowH > rowCap) rowH = rowCap;
		float bhOpt = max(14.f, rowH - 3.f);
		auto take = [&]() { float y0 = by; by += rowH; return y0; };
		float oy = take();
		mark(ID_BASE_QUALITY, TX_BSH_QUALITY, bx, oy, bw, bhOpt);
		wchar_t qv[72];
		if (m_save.quality == TAC_Q_AUTO)
			swprintf_s(qv, L"%s  %s", TT(TX_SET_Q_AUTO), TT(TacQualityTx(QualityNow())));
		else wcscpy_s(qv, TT(TacQualityTx(m_save.quality)));
		if (int q = Sel(ID_BASE_QUALITY, bx, oy, bw, bhOpt, TT(TX_SET_QUALITY), qv)) {
			PickQuality(q);
		}
		oy = take();
		mark(ID_BASE_ANIM, TX_BSH_ANIM, bx, oy, bw, bhOpt);
		if (Sel(ID_BASE_ANIM, bx, oy, bw, bhOpt, TT(TX_SET_ANIM), TT(m_save.battleAnim ? TX_SET_ANIM_SHORT : TX_SET_ANIM_FULL))) m_save.battleAnim = !m_save.battleAnim;
		oy = take();
		{
			static const int paceTx[6] = { TX_PACE0, TX_PACE1, TX_PACE2, TX_PACE3, TX_PACE4, TX_PACE5 };
			mark(ID_BASE_PACE, TX_BSH_PACE, bx, oy, bw, bhOpt);
			if (int d = Sel(ID_BASE_PACE, bx, oy, bw, bhOpt, TT(TX_SET_PACE), TT(paceTx[TacPace(m_save)]))) TacSetPace(m_save, TacPace(m_save) + d);
		}
		oy = take();
		{
			static const int paceTx[6] = { TX_PACE0, TX_PACE1, TX_PACE2, TX_PACE3, TX_PACE4, TX_PACE5 };
			mark(ID_BASE_CUT, TX_BSH_CUTSPD, bx, oy, bw, bhOpt);
			if (int d = Sel(ID_BASE_CUT, bx, oy, bw, bhOpt, TT(TX_SET_CUTSPD), TT(paceTx[TacCutPace(m_save)]), m_save.battleAnim ? 0 : 1)) TacSetCutPace(m_save, TacCutPace(m_save) + d);
		}
		oy = take();
		mark(ID_BASE_GRID, TX_BSH_GRID, bx, oy, bw, bhOpt);
		if (Sel(ID_BASE_GRID, bx, oy, bw, bhOpt, TT(TX_SET_GRID), TT(TacGridOn(m_save) ? TX_CR_ON : TX_CR_OFF))) m_save.showGrid ^= 1;
		oy = take();
		mark(ID_BASE_THREAT, TX_BSH_THREAT, bx, oy, bw, bhOpt);
		if (Sel(ID_BASE_THREAT, bx, oy, bw, bhOpt, TT(TX_SET_THREAT), TT(TacThreatOn(m_save) ? TX_CR_ON : TX_CR_OFF))) m_save.showGrid ^= 2;
		oy = take();
		mark(ID_BASE_AI, TX_BSH_AI, bx, oy, bw, bhOpt);
		if (int d = Sel(ID_BASE_AI, bx, oy, bw, bhOpt, TT(TX_SET_AI), TT(TacAiTx(TacAi(m_save))))) TacSetAi(m_save, TacAi(m_save) + d);
		oy = take();
		{
			BOOL autoQ = m_save.quality == TAC_Q_AUTO;
			wchar_t vs[32];
			if (autoQ) swprintf_s(vs, L"%s  1/%d", TT(TX_SET_Q_AUTO), PaceHz());
			else swprintf_s(vs, L"1/%d", TacVsyncHz(m_save));
			mark(ID_BASE_SYNC, TX_BSH_SYNC, bx, oy, bw, bhOpt);
			if (int d = Sel(ID_BASE_SYNC, bx, oy, bw, bhOpt, TT(TX_SET_SYNC), vs, autoQ ? 0 : 1)) {
				TacSetVsync(m_save, TacVsync(m_save) + d);
				m_syncRun = -1;
				m_syncArmN = 0;
			}
		}
		oy = take();
		mark(ID_BASE_WIN, TX_BSH_WIN, bx, oy, bw, bhOpt);
		if (Sel(ID_BASE_WIN, bx, oy, bw, bhOpt, TT(TX_SET_WIN), TT(TacWinTx(TacWinMode(m_save))))) TacSetWinMode(m_save, TacWinMode(m_save) ^ 1);
		oy = take();
		mark(ID_BASE_FPS, TX_BSH_FPS, bx, oy, bw, bhOpt);
		if (Sel(ID_BASE_FPS, bx, oy, bw, bhOpt, TT(TX_SET_FPS), TT(TacFpsOn(m_save) ? TX_CR_ON : TX_CR_OFF))) TacSetFps(m_save, !TacFpsOn(m_save));
		float kSound = U(34) > 1.f ? rowH / U(34) : k;
		by = DrawSoundOpts(bx, by, bw, kSound, &tipTx, &tipAy, &tipAx, &tipAw, &tipAh);
	}
	if (helpY + helpH > y + ph - 2.f) helpY = y + ph - helpH - 2.f;
	mark(ID_BASE_HELP, TX_BSH_HELP, bx, helpY, bw * .48f, helpH);
	mark(ID_BASE_CLOSE, TX_BSH_CLOSE, bx + bw * .52f, helpY, bw * .48f, helpH);
	if (Btn(ID_BASE_HELP, bx, helpY, bw * .48f, helpH, TT(TX_BT_HELP))) m_showHelp = TRUE;
	if (Btn(ID_BASE_CLOSE, bx + bw * .52f, helpY, bw * .48f, helpH, TT(TX_BASE_CLOSE))) m_wantClose = TRUE;
	const wchar_t* hint = NULL;
	if (RosterCount() == 0) hint = TT(TX_BASE_HINT_NOCHAR);
	else if (np == 0) hint = TT(TX_BASE_HINT_NOPARTY);
	float marginR = max(8.f, min(U(16.f), (float)m_w * .02f));
	float rightX = x + pw + max(8.f, U(10));
	float rightR = (float)m_w - marginR;
	float rightW = rightR - rightX;
	if (rightW < 36.f) { rightX = x + pw + 4.f; rightR = (float)m_w - 4.f; rightW = max(24.f, rightR - rightX); }
	float themeSelH = max(22.f, min(U(34.f), (float)m_h * 0.06f));
	float themeBtnH = max(22.f, min(U(38.f), (float)m_h * 0.07f));
	float themeH = themeSelH + 6.f + themeBtnH + 6.f + themeBtnH;
	float themeTop = (float)m_h - marginR - themeH;
	float noteH = max(26.f, min(U(64.f), (float)m_h * 0.1f));
	float noteTop = themeTop - 6.f - noteH;
	float areaTop = marginR;
	float areaBot = noteTop - 6.f;
	if (areaBot - areaTop < 48.f) {
		noteH = 22.f;
		themeTop = (float)m_h - 4.f - themeH;
		noteTop = themeTop - 4.f - noteH;
		areaBot = noteTop - 4.f;
		if (areaBot < areaTop + 16.f) areaBot = areaTop + 16.f;
	}
	float areaH = max(16.f, areaBot - areaTop);
	float mapWant = min(U(220.f), min(areaH, rightW) * 0.46f);
	int cap = PartyCap();
	int cols = 5;
	{
		int bestRows = (cap + 4) / 5;
		for (int c = 6; c <= 8 && c <= cap; c++) {
			float cwTry = (rightW - 8.f * (float)(c - 1)) / (float)c;
			if (cwTry < 44.f) continue;
			int rows = (cap + c - 1) / c;
			if (rows < bestRows) { cols = c; bestRows = rows; }
		}
	}
	int rowsN = (cap + cols - 1) / cols;
	BOOL mapBeside = mapWant >= 56.f && (rightW - mapWant - 8.f) / (float)cols >= 44.f;
	float mapS = 0.f, mapX = 0.f, mapY = 0.f;
	float cardX = rightX, cardW = rightW, cardY = areaTop, cardH = areaH;
	if (mapBeside) {
		mapS = mapWant;
		cardW = rightW - mapS - 8.f;
		mapX = rightR - mapS;
		mapY = areaTop;
	} else if (mapWant >= 48.f && areaH > 96.f) {
		mapS = min(mapWant, min(rightW * 0.55f, areaH * 0.34f));
		cardH = areaH - mapS - 6.f;
		mapX = rightR - mapS;
		mapY = cardY + cardH + 6.f;
	}
	float deployTitle = min(U(22.f), max(11.f, cardH * 0.16f));
	float gridTop = cardY + deployTitle + 4.f;
	float gridH = max(8.f, cardY + cardH - gridTop);
	float gap = max(3.f, min(U(10.f), cardW * 0.02f));
	float cw = (cardW - gap * (float)(cols - 1)) / (float)cols;
	float chh = (gridH - gap * (float)(rowsN - 1)) / (float)rowsN;
	if (cw < 6.f) cw = 6.f;
	// 縦いっぱいに引き伸ばさない。余った高さは空いたままにする。
	float chCap = cw * 1.18f;
	if (chCap < 68.f) chCap = 68.f;
	if (chh > chCap) chh = chCap;
	if (chh < 6.f) chh = 6.f;
	float cx0 = cardX;
	float cy = gridTop;
	m_r.HudText(TT(TX_PT_DEPLOY), cx0, cardY + deployTitle * .75f, min(U(16.f), deployTitle), RGBA8(255, 226, 150), 0, 1);
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
			float ps = min(max(8.f, cw - 8.f), chh * .55f);
			DrawPortrait(RosterPortrait(r), cx + (cw - ps) * .5f, ry + min(U(6), chh * .08f), ps, RGBA8(120, 160, 255));
			if (chh > ps + 14.f) {
				float nfs = min(U(14), max(8.f, chh * .12f));
				m_r.HudText(c.name, cx + cw * .5f, ry + ps + min(U(16), (chh - ps) * .35f), FitTextPx(c.name, cw - 8.f, nfs, 1), RGBA8(255, 255, 255), 1, 1);
			}
			if (chh > ps + 28.f) {
				wchar_t l[80];
				swprintf_s(l, L"%s %s %s", ClassName(c.cls), TT(TX_MISC_LV), TacNum(c.lv));
				float lfs = min(U(12), max(8.f, chh * .1f));
				m_r.HudText(l, cx + cw * .5f, ry + ps + min(U(32), (chh - ps) * .7f), FitTextPx(l, cw - 8.f, lfs, 0), RGBA8(210, 210, 200), 1, 0);
			}
		} else {
			m_r.HudText(L"—", cx + cw * .5f, ry + chh * .5f, U(22), RGBA8(120, 120, 130), 1, 0);
			if (m_mouseSeen && Hit(cx, ry, cw, chh)) { tipTx = TX_BSH_EMPTY; tipAx = cx; tipAy = ry; tipAw = cw; tipAh = chh; }
		}
		if (r >= 0 && m_save.roster[r].used && Hit(cx, ry, cw, chh)) { hovR = r; hovX = cx; hovY = ry; }
	}
	if (hovR >= 0) {
			const CharData& c = m_save.roster[hovR];
			float tw = min(U(420.f), max(120.f, rightW * .86f));
			float th = min(U(280.f), max(90.f, noteTop - 12.f));
			float tx = hovX + cw + 8.f;
			if (tx + tw > rightR - 4.f) tx = hovX - tw - 8.f;
			if (tx < x + pw + 4.f) tx = rightX;
			float ty = hovY;
			if (ty + th > noteTop - 4.f) ty = noteTop - 4.f - th;
			if (ty < 8.f) ty = 8.f;
			m_r.HudPanel(tx, ty, tw, th, RGBA8(12, 14, 26, 248), RGBA8(210, 180, 110));
			float ps = min(U(56), th * .28f);
			DrawPortrait(RosterPortrait(hovR), tx + U(8), ty + U(8), ps, RGBA8(120, 160, 255));
			float hx = tx + ps + U(14);
			float hw = tw - ps - U(22);
			m_r.HudText(c.name, hx, ty + U(18), FitTextPx(c.name, hw, U(16), 1), RGBA8(255, 236, 190), 0, 1);
			wchar_t line[96];
			swprintf_s(line, L"%s  %s %s", ClassName(c.cls), TT(TX_MISC_LV), TacNum(c.lv));
			m_r.HudText(line, hx, ty + U(38), FitTextPx(line, hw, U(13), 0), RGBA8(210, 214, 230), 0, 0);
			const wchar_t* wpn = (c.reserved[6] > 0) ? GearName(c.reserved[6]) : TT(TX_PT_NONE);
			const wchar_t* arm = (c.reserved[7] > 0) ? GearName(c.reserved[7]) : TT(TX_PT_NONE);
			swprintf_s(line, L"%s  %s", TT(TX_GEAR_WPN), wpn);
			m_r.HudText(line, tx + U(8), ty + ps + U(16), FitTextPx(line, tw - U(16), U(12), 0), RGBA8(230, 226, 210), 0, 0);
			swprintf_s(line, L"%s  %s", TT(TX_GEAR_ARM), arm);
			m_r.HudText(line, tx + U(8), ty + ps + U(32), FitTextPx(line, tw - U(16), U(12), 0), RGBA8(230, 226, 210), 0, 0);
			float statTop = ty + ps + U(48);
			float statBot = ty + th - U(6);
			float pitch = (statBot - statTop) / 10.f;
			if (pitch > U(16)) pitch = U(16);
			if (pitch < 9.f) pitch = 9.f;
			float colW = (tw - U(16)) * .5f;
			for (int st = 0; st < S_COUNT; st++) {
				int row = st / 2, col = st % 2;
				float sy = statTop + row * pitch;
				if (sy + pitch > statBot + 1.f) break;
				swprintf_s(line, L"%s %s", TT(TX_ST_HP + st), TacNum(c.stat[st]));
				float sx = tx + U(8) + col * colW;
				m_r.HudText(line, sx, sy + pitch * .45f, FitTextPx(line, colW - U(6), min(U(12), pitch * .72f), 0), RGBA8(220, 224, 236), 0, 0);
			}
	}
	if (mapS >= 48.f) DrawShowMinimap(mapX, mapY, mapS);
	if (!m_sortieAsk) {
		float bw2 = min(U(220.f), rightW);
		if (bw2 < 48.f) bw2 = rightW;
		float bx2 = rightR - bw2;
		float by2 = themeTop + themeSelH + 6.f;
		int shown = m_themeSel >= 0 ? m_themeSel : m_showMap.theme;
		mark(ID_BASE_THEME, TX_BSH_THEME, bx2, themeTop, bw2, themeSelH);
		mark(ID_BASE_REROLL, TX_BSH_REROLL, bx2, by2, bw2, themeBtnH);
		float lookY = by2 + themeBtnH + 6.f;
		mark(ID_BASE_LOOK, TX_MAP_LOOK_HINT, bx2, lookY, bw2, themeBtnH);
		if (int d = Sel(ID_BASE_THEME, bx2, themeTop, bw2, themeSelH, TT(TX_BT_TERRAIN), TT(ThemeNameTx(shown)))) {
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
		if (Btn(ID_BASE_REROLL, bx2, by2, bw2, themeBtnH, TT(TX_MAP_REROLL))) {
			m_mapSeed = 0;
			m_mapSeedStage = -1;
			m_showBuilt = FALSE;
			BuildShowcase();
			return;
		}
		if (Btn(ID_BASE_LOOK, bx2, lookY, bw2, themeBtnH, TT(TX_MAP_LOOK))) {
			m_mapLook = 1;
			m_camYaw = m_camYawGoal = m_showYaw;
			m_camPan = v3(0, 0, 0);
			if (m_camPitch < .2f || m_camPitch > 1.4f) m_camPitch = .55f;
			return;
		}
	}
	if (modal) {
		m_btns.clear();
		m_click = savedClick;
		m_activateId = savedAct;
		m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(0, 0, 0, 140));
		float w = min(U(420.f), (float)m_w - 16.f);
		float h = min(U(280.f), (float)m_h - 16.f);
		if (w < 64.f) w = max(32.f, (float)m_w - 8.f);
		if (h < 64.f) h = max(32.f, (float)m_h - 8.f);
		float sx = ((float)m_w - w) * .5f, sy = ((float)m_h - h) * .5f;
		Panel(sx, sy, w, h);
		m_r.HudText(TT(TX_SV_PICK), sx + w * .5f, sy + U(36), U(22), RGBA8(255, 226, 150), 1, 1);
		tipTx = -1;
		float pad = max(8.f, min(U(40.f), w * 0.1f));
		float bh2 = min(U(42.f), max(16.f, (h - min(U(72.f), h * 0.28f)) / 4.6f));
		float bx2 = sx + pad, bw2 = max(24.f, w - pad * 2.f), yy = sy + min(U(64.f), h * 0.22f);
		BOOL mid = m_midStage == m_save.stageSel;
		BOOL aut = m_autoStage == m_save.stageSel;
		mark(ID_SORTIE_START, TX_BSH_START, bx2, yy, bw2, bh2);
		if (Btn(ID_SORTIE_START, bx2, yy, bw2, bh2, TT(TX_SV_FROMSTART))) { m_sortieAsk = 0; StartBattle(m_save.stageSel); return; }
		yy += bh2 + U(8);
		mark(ID_SORTIE_MID, TX_BSH_MID, bx2, yy, bw2, bh2);
		if (Btn(ID_SORTIE_MID, bx2, yy, bw2, bh2, TT(TX_SV_FROMMID), mid)) { m_sortieAsk = 0; ResumeBattle(1); return; }
		yy += bh2 + U(8);
		mark(ID_SORTIE_AUTO, TX_BSH_AUTO, bx2, yy, bw2, bh2);
		if (Btn(ID_SORTIE_AUTO, bx2, yy, bw2, bh2, TT(TX_SV_FROMAUTO), aut)) { m_sortieAsk = 0; ResumeBattle(2); return; }
		yy += bh2 + U(8);
		if (Btn(ID_SORTIE_CANCEL, bx2, yy, bw2, bh2, TT(TX_CR_BACK))) m_sortieAsk = 0;
	}
	if (tipTx >= 0) {
		DrawHoverNote(m_r, tipAx, tipAy, tipAw, tipAh, m_ui, m_w, m_h, TT(tipTx));
	} else if (hint) {
		float hw = min(rightW, max(80.f, U(520.f)));
		float pulse = .75f + .25f * sinf(m_time * 3.f);
		m_r.HudPanel(rightX, noteTop, hw, noteH, RGBA8(20, 20, 40, 220), RGBA8(255, 210, 120, (int)(255 * pulse)));
		m_r.HudTextBox(hint, rightX + 8.f, noteTop + 3.f, hw - 16.f, noteH - 6.f, min(U(16.f), noteH * .42f), RGBA8(255, 236, 190), 1);
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
	m_previewWing = TRUE;
	m_previewGround = FALSE;
	m_focusId = ID_CR_NAME;
	RefreshPreview();
}

static BOOL PreviewAlt(int style)
{
	return AttackClipFor(style, 0) != AttackClipFor(style, 1);
}

static BOOL PreviewHasWings(const CharData& c)
{
	const ClassDef& cd = ClassInfo(c.cls);
	return cd.moveType == MV_FLY || CharHasPas(c, PAS_FLY) || c.cls == C_SKYKNIGHT || c.cls == C_DRAGONLORD;
}

// 翼オフは羽根だけ消す。地面は翼も浮きも羽ばたきもやめ、足の着いた動きに戻す。
static void ApplyCreatePreview(Actor& a, BOOL showWing, BOOL ground)
{
	if (ground) {
		a.wings = 0;
		a.flap = 0;
		a.hover = 0;
		a.flyLean = 0;
		a.legOk = 0;
		return;
	}
	if (!showWing) a.wings = 0;
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
	if (m_previewGround) fly = FALSE;
	PlayShown(m_preview, m_previewMot, style, idle, fly, TRUE, FALSE);
	ApplyCreatePreview(m_preview, m_previewWing, m_previewGround);
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
	m_r.SetTheme(-1);
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
	m_r.BeginInstBatch();
	ActorDraw(m_r, m_lib, a, aw, dp, m_time);
	m_r.EndInstBatch();
	m_r.BeginMainPass();
	m_r.DrawSky(RGBA8(22, 28, 48), RGBA8(58, 64, 88), 0, 0);
	m_r.BeginInstBatch();
	ActorDraw(m_r, m_lib, a, aw, dp, m_time);
	m_r.EndInstBatch();
}

void Game::DrawCreateUi()
{
	float pw = U(420), x = U(20), y = U(20), ph = (float)m_h - U(40);
	Panel(x, y, pw, ph);
	m_r.HudText(TT(m_editIdx >= 0 ? TX_CR_EDIT : TX_CR_TITLE), x + pw * .5f, y + U(30), U(22), RGBA8(255, 226, 150), 1, 1);
	float bx = x + U(18), bw = pw - U(36), rh = U(30), gap = U(3);
	float yy = y + U(58);
	int tipTx = -1;
	float tipAx = 0, tipAy = 0, tipAw = 0, tipAh = 0;
	auto note = [&](int id, int tx) {
		BOOL on = (m_mouseSeen && !m_navMode && Hit(bx, yy, bw, rh)) || (m_navMode && m_focusId == id);
		if (on) { tipTx = tx; tipAx = bx; tipAy = yy; tipAw = bw; tipAh = rh; }
	};
	// 名前
	{
		float nw = bw - U(110);
		BOOL foc = m_textFocus == 1;
		UiBtn b = { ID_CR_NAME, bx, yy, nw, rh, 1, 0 };
		m_btns.push_back(b);
		if (ClickIn(bx, yy, nw, rh) || m_activateId == ID_CR_NAME) {
			if (m_textFocus != 1) {
				m_textFocus = 1;
				m_textCaret = TacWLen(m_edit.name);
				m_imeComp[0] = 0;
			}
			m_activateId = -1;
		}
		BOOL nf = m_navMode && m_focusId == ID_CR_NAME;
		TacMarkBox(m_textBox, bx + U(70), yy, nw - U(70), rh);
		m_textPx = U(17);
		m_r.HudRect(bx, yy, nw, rh, foc ? RGBA8(40, 46, 80, 240) : RGBA8(20, 24, 44, 230));
		m_r.HudFrame(bx, yy, nw, rh, U(1.5f), foc || nf ? RGBA8(255, 230, 150) : RGBA8(120, 104, 74));
		m_r.HudText(TT(TX_CR_NAME), bx + U(8), yy + rh * .5f, U(13), RGBA8(200, 190, 160), 0, 0);
		wchar_t nm[NAME_LEN + 48];
		if (foc) TacCaretStr(nm, NAME_LEN + 48, m_edit.name, m_textCaret, m_imeComp, fmodf(m_time, 1.f) < .5f);
		else wcsncpy_s(nm, m_edit.name, _TRUNCATE);
		m_r.HudText(nm[0] ? nm : TT(TX_CR_NAMEHINT), bx + U(70), yy + rh * .5f, U(17), nm[0] ? RGBA8(255, 255, 255) : RGBA8(140, 140, 150), 0, 1);
		if (Btn(ID_CR_RANDOM, bx + nw + U(8), yy, U(102), rh, TT(TX_CR_RANDOM), TRUE, FALSE, U(14))) RandomName(m_edit.name, m_edit.gender, m_rng);
		BOOL onRand = (m_mouseSeen && !m_navMode && Hit(bx + nw + U(8), yy, U(102), rh)) || (m_navMode && m_focusId == ID_CR_RANDOM);
		if (onRand) { tipTx = TX_CRH_RANDOM; tipAx = bx + nw + U(8); tipAy = yy; tipAw = U(102); tipAh = rh; }
	}
	yy += rh + gap + U(4);
	BOOL changed = FALSE;
	if (int d = Sel(ID_CR_GENDER, bx, yy, bw, rh, TT(TX_CR_GENDER), TT(m_edit.gender ? TX_CR_FEMALE : TX_CR_MALE))) { (void)d; m_edit.gender = !m_edit.gender; CharSetAvatar(m_edit, 0); changed = TRUE; }
	note(ID_CR_GENDER, TX_CRH_GENDER);
	yy += rh + gap;
	if (int d = Sel(ID_CR_MODEL, bx, yy, bw, rh, TT(TX_CR_MODEL), TT(TX_MDL_0 + CharModel(m_edit)))) {
		CharSetModel(m_edit, (CharModel(m_edit) + d + MODEL_N) % MODEL_N); changed = TRUE;
	}
	note(ID_CR_MODEL, TX_CRH_MODEL);
	yy += rh + gap;
	BOOL isNew = m_editIdx < 0;
	BOOL classLock = !isNew && m_edit.lv >= 2;
	if (Sel(ID_CR_LINEAGE, bx, yy, bw, rh, TT(TX_CR_LINEAGE), TT(m_lineage ? TX_CR_MAGIC : TX_CR_WARRIOR), classLock ? 0 : 1)) {
		m_lineage = !m_lineage;
		int lst[4];
		CreateClassList(lst);
		m_edit.cls = lst[0];
		CharSetBody(m_edit, m_lineage ? B_MAGE : B_KNIGHT);
		changed = TRUE;
	}
	note(ID_CR_LINEAGE, classLock ? TX_CRH_LOCK : TX_CRH_LINEAGE);
	yy += rh + gap;
	{
		int lst[4];
		int n = CreateClassList(lst);
		int ci = 0;
		for (int i = 0; i < n; i++) if (lst[i] == m_edit.cls) ci = i;
		if (!classLock && ci == 0 && lst[0] != m_edit.cls) {
			for (int i = 0; i < n; i++) if (lst[i] == m_edit.cls) ci = i;
		}
		if (int d = Sel(ID_CR_CLASS, bx, yy, bw, rh, TT(TX_CR_CLASS), ClassName(m_edit.cls), classLock ? 0 : 1)) {
			ci = (ci + d + n) % n;
			m_edit.cls = lst[ci];
			static const int bodyFor[6] = { B_KNIGHT, B_BARBARIAN, B_ROGUE, B_ROGUE_HOOD, B_MAGE, B_MAGE };
			if (m_edit.cls < 6) CharSetBody(m_edit, bodyFor[m_edit.cls]);
			changed = TRUE;
		}
		note(ID_CR_CLASS, classLock ? TX_CRH_LOCK : TX_CRH_CLASS);
		yy += rh + gap;
	}
	static const int bodyTx[B_COUNT] = { TX_BODY_KNIGHT, TX_BODY_BARBARIAN, TX_BODY_MAGE, TX_BODY_ROGUE, TX_BODY_HOODED };
	if (int d = Sel(ID_CR_BODY, bx, yy, bw, rh, TT(TX_CR_BODY), TT(bodyTx[CharBody(m_edit)]))) { CharSetBody(m_edit, (CharBody(m_edit) + d + B_COUNT) % B_COUNT); changed = TRUE; }
	note(ID_CR_BODY, TX_CRH_BODY);
	yy += rh + gap;
	{
		int nf = Clampi(m_r.FaceCount(m_edit.gender ? 1 : 0), 1, 6);
		int fc = CharFace(m_edit) % nf;
		if (int d = Sel(ID_CR_FACE, bx, yy, bw, rh, TT(TX_CR_FACE), TT((m_edit.gender ? TX_FACE_F0 : TX_FACE_M0) + fc))) {
			CharSetFace(m_edit, (fc + d + nf) % nf); changed = TRUE;
		}
		note(ID_CR_FACE, TX_CRH_FACE);
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
		note(ID_CR_AVATAR, TX_CRH_AVATAR);
	}
	yy += rh + gap;
	{
		int col = CharAvaCol(m_edit);
		if (int d = Sel(ID_CR_AVACOL, bx, yy, bw, rh, TT(TX_CR_AVACOL), TT(TX_AVC_0 + col))) {
			CharSetAvaCol(m_edit, (col + d + 8) % 8); changed = TRUE;
		}
		note(ID_CR_AVACOL, TX_CRH_AVACOL);
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
		note(ID_CR_HAIR, TX_CRH_HAIR);
	}
	yy += rh + gap;
	{
		wchar_t v[16];
		swprintf_s(v, L"%d", m_edit.hairColor + 1);
		if (int d = Sel(ID_CR_HAIRCOL, bx, yy, bw, rh, TT(TX_CR_HAIRCOLOR), v)) { m_edit.hairColor = (m_edit.hairColor + d + HAIR_COLORS) % HAIR_COLORS; changed = TRUE; }
		note(ID_CR_HAIRCOL, TX_CRH_HAIRCOL);
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
		note(ID_CR_OUTFIT, TX_CRH_OUTFIT);
	}
	yy += rh + gap;
	if (int d = Sel(ID_CR_HEIGHT, bx, yy, bw, rh, TT(TX_CR_HEIGHT), TT(TX_HT_0 + CharHt(m_edit)))) {
		CharSetHt(m_edit, (CharHt(m_edit) + d + HT_N) % HT_N); changed = TRUE;
	}
	note(ID_CR_HEIGHT, TX_CRH_HEIGHT);
	yy += rh + gap;
	if (int d = Sel(ID_CR_FIG, bx, yy, bw, rh, TT(TX_CR_FIG), TT(TX_FIG_0 + CharFig(m_edit)))) {
		CharSetFig(m_edit, (CharFig(m_edit) + d + FIG_N) % FIG_N); changed = TRUE;
	}
	note(ID_CR_FIG, TX_CRH_FIG);
	yy += rh + gap;
	// 女性は胸の大きさ、男性は胸板の厚み。保存は同じ欄。
	{
		const BOOL fem = m_edit.gender != 0;
		if (int d = Sel(ID_CR_BUST, bx, yy, bw, rh, TT(fem ? TX_CR_BUST : TX_CR_PEC), TT((fem ? TX_BUST_0 : TX_PEC_0) + CharBust(m_edit)))) {
			CharSetBust(m_edit, (CharBust(m_edit) + d + BUST_N) % BUST_N); changed = TRUE;
		}
		note(ID_CR_BUST, fem ? TX_CRH_BUST : TX_CRH_PEC);
		yy += rh + gap;
	}
	if (Sel(ID_CR_HAT, bx, yy, bw, rh, TT(TX_CR_HAT), TT(m_edit.hat ? TX_CR_ON : TX_CR_OFF))) { m_edit.hat = !m_edit.hat; changed = TRUE; }
	note(ID_CR_HAT, TX_CRH_HAT);
	yy += rh + gap;
	if (Sel(ID_CR_CAPE, bx, yy, bw, rh, TT(TX_CR_CAPE), TT(m_edit.cape ? TX_CR_ON : TX_CR_OFF))) { m_edit.cape = !m_edit.cape; changed = TRUE; }
	note(ID_CR_CAPE, TX_CRH_CAPE);
	yy += rh + gap + U(6);
	if (changed) RefreshPreview();
	{
		float ix, iy, iw, ih;
		CreateInfoRect(m_ui, m_w, ix, iy, iw, ih);
		Panel(ix, iy, iw, ih);
		const ClassDef& cd = ClassInfo(m_edit.cls);
		CharData shown = m_edit;
		if (isNew) { shown.lv = 1; CharRecalcInit(shown); }
		int lv = max(1, shown.lv);
		m_r.HudText(ClassName(m_edit.cls), ix + U(10), iy + U(16), U(15), RGBA8(255, 226, 150), 0, 1);
		wchar_t lvb[24];
		swprintf_s(lvb, L"%s %d", TT(TX_MISC_LV), lv);
		m_r.HudText(lvb, ix + iw - U(10), iy + U(16), U(13), RGBA8(220, 220, 230), 2, 1);
		wchar_t meta[96];
		if (cd.moveType == MV_FLY)
			swprintf_s(meta, L"%s  %s  %s %d-%d", TT(cd.magic ? TX_CR_MAGIC : TX_CR_WARRIOR), TT(TX_MISC_FLY), TT(TX_MISC_RANGE), cd.rMin, cd.rMax);
		else
			swprintf_s(meta, L"%s  %s %d-%d", TT(cd.magic ? TX_CR_MAGIC : TX_CR_WARRIOR), TT(TX_MISC_RANGE), cd.rMin, cd.rMax);
		m_r.HudText(meta, ix + U(10), iy + U(34), U(12), RGBA8(200, 206, 220), 0, 0);
		m_r.HudTextBox(ClassBlurb(m_edit.cls), ix + U(10), iy + U(46), iw - U(20), U(40), U(12), RGBA8(226, 226, 232));
		static const int kL[4] = { S_HP, S_STR, S_DEF, S_AGI };
		static const int kR[4] = { S_MP, S_MAG, S_RES, S_MOV };
		float sy = iy + U(92);
		float colW = (iw - U(16)) * .5f;
		for (int i = 0; i < 4; i++) {
			int pair[2] = { kL[i], kR[i] };
			for (int col = 0; col < 2; col++) {
				int s = pair[col];
				float sx = ix + U(8) + colW * col;
				BOOL hov = m_mouseSeen && Hit(sx, sy, colW - U(4), U(18));
				if (hov) {
					m_statTip = s;
					m_r.HudRect(sx, sy, colW - U(4), U(18), RGBA8(255, 220, 140, 36));
				}
				const wchar_t* lab = TT(StatNameTx(s));
				wchar_t num[24];
				swprintf_s(num, L"%s", TacNum(shown.stat[s]));
				float labPx = FitTextPx(lab, colW * .46f, U(12), 1);
				float numPx = FitTextPx(num, colW * .48f, U(13), 1);
				m_r.HudText(lab, sx + U(2), sy + U(9), labPx, hov ? RGBA8(255, 236, 180) : RGBA8(200, 190, 160), 0, 1);
				m_r.HudText(num, sx + colW - U(8), sy + U(9), numPx, RGBA8(250, 250, 250), 2, 1);
			}
			sy += U(20);
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
			if (c.lv < 2 && c.cls != m_edit.cls) {
				c.cls = m_edit.cls;
				c.skills = 0;
				c.jobPath[0] = c.cls;
				c.jobDone = 1;
				CharRecalcProgress(c);
				CharGrantReached(c);
			}
			Persist();
		}
		InvalidatePortraits();
		m_showN = 0;
		BuildShowcase();
		AddBanner(m_edit.name, ClassName(m_edit.cls), RGBA8(255, 226, 150), 2, 2.f, MSG_DEPTH);
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
		if (m_previewGround) fly = FALSE;
		if (hit >= 0) {
			m_previewMot = hit;
			PlayShown(m_preview, m_previewMot, style, idle, fly, TRUE, TRUE);
			ApplyCreatePreview(m_preview, m_previewWing, m_previewGround);
		}
		if (PreviewHasWings(m_edit)) {
			const BOOL wingOn = m_previewWing && !m_previewGround;
			wchar_t lab[48];
			swprintf_s(lab, L"%s %s", TT(TX_CR_WING), TT(wingOn ? TX_CR_ON : TX_CR_OFF));
			float ww = U(132), wh = U(32), gapb = U(8);
			float span = ww * 2.f + gapb;
			float wx = cx - span * .5f, wy = m_h - U(108);
			if (Btn(ID_CR_WING, wx, wy, ww, wh, lab, TRUE, wingOn, U(15))) {
				if (m_previewGround) { m_previewGround = FALSE; m_previewWing = TRUE; }
				else m_previewWing = !m_previewWing;
				RefreshPreview();
			}
			BOOL onWing = (m_mouseSeen && !m_navMode && Hit(wx, wy, ww, wh)) || (m_navMode && m_focusId == ID_CR_WING);
			if (onWing) { tipTx = TX_CRH_WING; tipAx = wx; tipAy = wy; tipAw = ww; tipAh = wh; }
			float gx = wx + ww + gapb;
			if (Btn(ID_CR_GROUND, gx, wy, ww, wh, TT(TX_CR_GROUND), TRUE, m_previewGround != 0, U(15))) {
				m_previewGround = !m_previewGround;
				RefreshPreview();
			}
			BOOL onGround = (m_mouseSeen && !m_navMode && Hit(gx, wy, ww, wh)) || (m_navMode && m_focusId == ID_CR_GROUND);
			if (onGround) { tipTx = TX_CRH_GROUND; tipAx = gx; tipAy = wy; tipAw = ww; tipAh = wh; }
		}
		m_r.HudText(TT(TX_CR_PREVIEW_HINT), cx, m_h - U(28), U(13), RGBA8(220, 220, 230), 1, 0);
	}
	if (tipTx >= 0 && m_statTip < 0)
		DrawHoverNote(m_r, tipAx, tipAy, tipAw, tipAh, m_ui, m_w, m_h, TT(tipTx));
}

// ============================================================ 編成
void Game::EnterParty()
{
	for (int i = 0; i < MAX_ROSTER; i++)
		if (m_save.roster[i].used) CharGrantReached(m_save.roster[i]);
	m_screen = SC_PARTY;
	m_confirmDelete = 0;
	m_eqPick = 0;
	m_partySkPage = 0;
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
		AddBanner(hint, NULL, RGBA8(255, 200, 150), 2, 2.f, MSG_FACE);
		return;
	}
	for (int i = 0; i < cap; i++) if (m_save.party[i] < 0) { m_save.party[i] = idx; BuildShowcase(); return; }
	wchar_t hint[180];
	swprintf_s(hint, TT(TX_PT_HINT), cap);
	AddBanner(hint, NULL, RGBA8(255, 200, 150), 2, 2.f, MSG_FACE);
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
	wchar_t rowTip[480];
	rowTip[0] = 0;
	float rtx = 0, rty = 0, rtw = 0, rth = 0;
	BOOL rowHot = FALSE;
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
		if (m_mouseSeen && (Hit(px + U(16), yy, pw - U(32), U(32)) || (m_navMode && m_focusId == ID_EQ_ROW0 + s))) {
			rowTip[0] = 0;
			if (gear) {
				const GearDef* gg = GearOf(m_save.gearId[ids[ix]]);
				if (gg) {
					wchar_t body[360];
					GearTipText(gg, body, 360);
					swprintf_s(rowTip, L"%s%s%s", GearName(m_save.gearId[ids[ix]]), body[0] ? L"\n" : L"", body);
				}
			} else if (kind <= 4) AccTipText(ids[ix], rowTip, 480);
			else if (pasPick) wcsncpy_s(rowTip, PasDesc(ids[ix]), _TRUNCATE);
			else if (sumPick) wcsncpy_s(rowTip, SummonDesc(ids[ix]), _TRUNCATE);
			else if (beadSlot >= 0) wcsncpy_s(rowTip, CraftDesc(ids[ix] - 1), _TRUNCATE);
			if (rowTip[0]) { rowHot = TRUE; rtx = px + U(16); rty = yy; rtw = pw - U(32); rth = U(32); }
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
	if (rowHot) DrawHoverNote(m_r, rtx, rty, rtw, rth, m_ui, m_w, m_h, rowTip);
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
	int tipTx = -1;
	float tipAx = 0, tipAy = 0, tipAw = 0, tipAh = 0;
	auto mark = [&](int id, int tx, float rowX, float rowY, float rowW, float rowH) {
		BOOL on = (m_mouseSeen && !m_navMode && Hit(rowX, rowY, rowW, rowH)) || (m_navMode && m_focusId == id);
		if (on) { tipTx = tx; tipAx = rowX; tipAy = rowY; tipAw = rowW; tipAh = rowH; }
	};
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
			mark(ID_PT_CARD0 + local, TX_PTH_PLUS, cx, cy, cw, ch);
			if (Btn(ID_PT_CARD0 + local, cx, cy, cw, ch, L"+", RosterCount() < rCap, FALSE, U(28), RGBA8(30, 40, 60, 160))) { m_createFromParty = 1; EnterCreate(-1); return; }
			continue;
		}
		BOOL inParty = FALSE;
		for (int p = 0; p < pCap; p++) if (m_save.party[p] == i) inParty = TRUE;
		if (Btn(ID_PT_CARD0 + local, cx, cy, cw, ch, L"", TRUE, m_partySel == i, 0, inParty ? RGBA8(50, 80, 140, 240) : 0)) {
			if (m_partySel != i) m_partySkPage = 0;
			if (m_partySel == i) TogglePartyMember(i);
			m_partySel = i;
			m_confirmDelete = 0;
		}
		DrawPortrait(RosterPortrait(i), cx + (cw - ps) * .5f, cy + U(6), ps, inParty ? RGBA8(120, 180, 255) : RGBA8(110, 100, 80));
		m_r.HudText(c.name, cx + cw * .5f, cy + U(6) + ps + U(12), FitTextPx(c.name, cw - U(8), U(13), 1), m_partySel == i ? RGBA8(40, 24, 8) : RGBA8(255, 255, 255), 1, 1, m_partySel != i);
		wchar_t l[48];
		swprintf_s(l, L"%s%s", TT(TX_MISC_LV), TacNum(c.lv));
		m_r.HudText(l, cx + cw * .5f, cy + U(6) + ps + U(28), FitTextPx(l, cw - U(8), U(11), 0), m_partySel == i ? RGBA8(60, 40, 10) : RGBA8(210, 210, 200), 1, 0, m_partySel != i);
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
	mark(ID_PT_NEW, TX_PTH_NEW, x + U(20), y + gh - U(46), U(160), U(36));
	mark(ID_PT_SHOP, TX_PTH_SHOP, x + U(190), y + gh - U(46), U(140), U(36));
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
	m_sellAsk = 0; m_sellKind = 0; m_sellId = 0; m_sellN = 1; m_sellMax = 1; m_sellFresh = 1; m_sellGo = 0;
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
	float lineH = U(12) + 4.f;
	float expBlock = U(52);
	float statBlock = U(20) * 5.f + U(12);
	float skillMin = U(26) + U(24) * 2.f;
	float histBudget = gearY - U(8) - (y + U(70)) - expBlock - statBlock - skillMin;
	int maxHist = (int)(histBudget / lineH);
	if (maxHist < 0) maxHist = 0;
	if (maxHist > 8) maxHist = 8;
	float infoY = y + U(70);
	float histDrop = DrawJobPath(m_r, c, dx + ps2 + U(26), infoY, dw - ps2 - U(40), U(12), RGBA8(210, 220, 235), maxHist);
	infoY += histDrop + (histDrop > 0 ? U(4) : 0);
	wchar_t l[96];
	int need = ExpToNext(c.lv);
	int left = c.lv >= TAC_LV_MAX ? 0 : max(0, need - c.exp);
	swprintf_s(l, L"%s %s   %s %s/%s  %s %s", TT(TX_MISC_LV), TacNum(c.lv), TT(TX_MISC_EXP), TacNum(c.exp), TacNum(need), TT(TX_EXP_LEFT), TacNum(left));
	m_r.HudText(l, dx + ps2 + U(26), infoY, FitTextPx(l, dw - ps2 - U(40), U(14), 0), RGBA8(220, 220, 220), 0, 0);
	Bar(dx + ps2 + U(26), infoY + U(12), dw - ps2 - U(44), U(8), need > 0 ? c.exp / (float)need : 0.f, RGBA8(120, 200, 255), RGBA8(60, 120, 220));
	swprintf_s(l, L"%s %s  /  %s %s", TT(TX_BT_DEFEATED), TacNum(c.kills), TT(TX_BT_STAGE), TacNum(c.battles));
	m_r.HudText(l, dx + ps2 + U(26), infoY + U(26), FitTextPx(l, dw - ps2 - U(40), U(12), 0), RGBA8(190, 190, 190), 0, 0);
	int gearBonus[S_COUNT];
	CharFightBonus(c, gearBonus);
	float sy = infoY + U(44);
	float portraitBottom = y + U(14) + ps2 + U(8);
	if (sy < portraitBottom) sy = portraitBottom;
	if (sy > gearY - statBlock - U(8)) sy = gearY - statBlock - U(8);
	if (sy < infoY + U(40)) sy = infoY + U(40);
	sy += DrawStatLines(c, dx + U(14), sy, dw - U(28), gearBonus, -1, -1) + U(8);
	if (sy > gearY - U(8)) sy = gearY - U(8);
	m_r.HudText(TT(TX_PT_SKILLS), dx + U(14), sy + U(12), U(16), RGBA8(255, 226, 150), 0, 1);
	sy += U(26);
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
	if (skillBot < skillTop) skillBot = skillTop;
	m_partySkX = dx + U(6);
	m_partySkY = skillTop - U(24);
	m_partySkW = dw - U(12);
	m_partySkH = skillBot - m_partySkY;
	m_partySkPages = 1;
	if (!ns) {
		m_r.HudText(TT(TX_PT_NONE), dx + U(20), skillTop + U(12), U(14), RGBA8(160, 160, 170), 0, 0);
	} else {
		int buck[10][160];
		int cnt[10] = {};
		auto headOf = [&](int i) -> int {
			if (sks[i].kind == 1) return IsCraftSuper(sks[i].id) ? 7 : 6;
			if (sks[i].kind == 2) return 8;
			if (sks[i].kind == 3) return 9;
			const SkillDef& sd = SkillInfo(sks[i].id);
			if (sd.kind == SK_PASSIVE) return 5;
			int sh = SkillShapeBucket(sd);
			if (sh == 4) return 3;
			if (sh == 5) return 4;
			if (sh == 3) return 2;
			if (sh == 1) return 0;
			return 1;
		};
		for (int i = 0; i < ns; i++) {
			int h = headOf(i);
			if (cnt[h] < 160) buck[h][cnt[h]++] = i;
		}
		const wchar_t* headNm[10] = {
			TT(TX_SHAPE_ONE), TT(TX_MISC_AREA), TT(TX_SHAPE_WIDE), TT(TX_SHAPE_LINE), TT(TX_SHAPE_FAN),
			TT(TX_MISC_PASSIVE), TT(TX_CRAFT), TT(TX_OUGI), TT(TX_PAS), SummonWord()
		};
		int cols = dw >= U(360) ? 2 : 1;
		struct Ln { short head; short n; short si[2]; };
		Ln all[200];
		int nAll = 0;
		for (int h = 0; h < 10 && nAll < 200; h++) {
			if (!cnt[h]) continue;
			all[nAll].head = (short)h;
			all[nAll].n = 0;
			nAll++;
			for (int i = 0; i < cnt[h] && nAll < 200; ) {
				all[nAll].head = -1;
				all[nAll].n = 0;
				for (int k = 0; k < cols && i < cnt[h]; k++, i++) all[nAll].si[all[nAll].n++] = (short)buck[h][i];
				nAll++;
			}
		}
		const float rowH = U(24);
		const float nameFs = U(14);
		int fit = (int)((skillBot - skillTop) / rowH);
		if (fit < 1) fit = 1;
		if (fit > 16) fit = 16;
		enum { SK_PG = 48, SK_LN = 16 };
		Ln page[SK_PG][SK_LN];
		int pageN[SK_PG];
		memset(pageN, 0, sizeof(pageN));
		int pi = 0, ln = 0, curH = -1;
		auto turnPage = [&]() -> bool {
			pageN[pi] = ln;
			if (pi + 1 >= SK_PG) return false;
			pi++;
			ln = 0;
			return true;
		};
		for (int i = 0; i < nAll; i++) {
			int repeat = 0;
			if (all[i].head >= 0) {
				curH = all[i].head;
				if (ln > 0 && ln + 1 >= fit && i + 1 < nAll && !turnPage()) break;
			} else if (ln == 0 && curH >= 0 && fit >= 2) repeat = 1;
			if (ln > 0 && ln + (repeat ? 2 : 1) > fit) {
				if (!turnPage()) break;
				if (all[i].head < 0 && curH >= 0 && fit >= 2) repeat = 1;
			}
			if (repeat && ln < SK_LN) { page[pi][ln].head = (short)curH; page[pi][ln].n = 0; ln++; }
			if (ln < SK_LN) page[pi][ln++] = all[i];
			if (ln >= fit && i + 1 < nAll && !turnPage()) break;
		}
		pageN[pi] = ln;
		int pages = nAll > 0 ? pi + 1 : 1;
		if (m_partySkPage >= pages) m_partySkPage = pages - 1;
		if (m_partySkPage < 0) m_partySkPage = 0;
		m_partySkPages = pages;
		if (pages > 1) {
			wchar_t pg[24];
			swprintf_s(pg, L"%d / %d", m_partySkPage + 1, pages);
			float bw = U(28), bh = U(22);
			float by = skillTop - U(24);
			float nx = dx + dw - U(14) - bw;
			float pxb = nx - U(58) - bw;
			if (Btn(ID_PT_SKPREV, pxb, by, bw, bh, L"◀", TRUE, FALSE, U(13))) { m_partySkPage = (m_partySkPage + pages - 1) % pages; return; }
			m_r.HudText(pg, (pxb + bw + nx) * .5f, by + bh * .5f, U(13), RGBA8(230, 230, 240), 1, 1);
			if (Btn(ID_PT_SKNEXT, nx, by, bw, bh, L"▶", TRUE, FALSE, U(13))) { m_partySkPage = (m_partySkPage + 1) % pages; return; }
		}
		int pg = m_partySkPage;
		m_r.HudRect(dx + U(8), skillTop, dw - U(16), min(skillBot - skillTop, rowH * pageN[pg]), RGBA8(8, 12, 24, 110));
		float colW = (dw - U(20)) / (float)cols;
		float ysk = skillTop;
		for (int li = 0; li < pageN[pg]; li++) {
			const Ln& L = page[pg][li];
			if (L.head >= 0) {
				m_r.HudText(headNm[L.head], dx + U(14), ysk + rowH * .5f, U(14), RGBA8(255, 214, 140), 0, 1);
			} else {
				for (int k = 0; k < L.n; k++) {
					int si = L.si[k];
					float hx = dx + U(10) + k * colW;
					BOOL hov = !modal && m_mouseSeen && Hit(hx, ysk, colW - U(6), rowH);
					if (hov) {
						m_skillTip = sks[si].kind;
						m_skillTipId = sks[si].id;
						m_r.HudRect(hx, ysk, colW - U(6), rowH, RGBA8(255, 220, 140, 48));
					}
					wchar_t nm[96], mp[16];
					nm[0] = mp[0] = 0;
					BOOL showMp = FALSE;
					if (sks[si].kind == 1) {
						BOOL ougi = IsCraftSuper(sks[si].id);
						swprintf_s(nm, L"%s%s", CraftName(sks[si].id), ougi ? L" ★" : L"");
					} else if (sks[si].kind == 2) {
						wcscpy_s(nm, PasName(sks[si].id));
					} else if (sks[si].kind == 3) {
						wcscpy_s(nm, SkillLabel(sks[si].id));
					} else {
						const SkillDef& sd = SkillInfo(sks[si].id);
						wcscpy_s(nm, SkillLabel(sks[si].id));
						if (sd.kind != SK_PASSIVE) { swprintf_s(mp, L"MP%d", sd.mp); showMp = TRUE; }
					}
					float mpFs = U(12);
					float mpW = showMp ? m_r.TextWidth(mp, mpFs, 0) + U(8) : 0;
					m_r.HudText(nm, hx + U(4), ysk + rowH * .5f, FitTextPx(nm, colW - U(12) - mpW, nameFs, 0), hov ? RGBA8(255, 244, 210) : sks[si].col, 0, 0);
					if (showMp) m_r.HudText(mp, hx + colW - U(8), ysk + rowH * .5f, mpFs, RGBA8(186, 198, 220), 2, 0);
				}
			}
			ysk += rowH;
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
		wchar_t note[520];
		const wchar_t* shown = NULL;
		float noteX = 0, noteY = 0, noteW = 0, noteH = 0;
		auto see = [&](float hx, float hy, float hw, float hh, const wchar_t* text) {
			if (!text || !text[0] || !m_mouseSeen || m_eqPick) return;
			if (!Hit(hx, hy, hw, hh)) return;
			wcsncpy_s(note, text, _TRUNCATE);
			shown = note;
			noteX = hx; noteY = hy; noteW = hw; noteH = hh;
		};
		auto gearSee = [&](const GearDef* g, int gid, float hx, float hy, float hw, float hh) {
			if (!g) { see(hx, hy, hw, hh, TT(TX_PT_NONE)); return; }
			wchar_t body[400], line[520];
			GearTipText(g, body, 400);
			swprintf_s(line, L"%s%s%s", GearName(gid), body[0] ? L"\n" : L"", body);
			see(hx, hy, hw, hh, line);
		};
		auto accSee = [&](int slot, float hx, float hy, float hw, float hh) {
			wchar_t line[400];
			AccTipText(EquippedAccId(c, slot), line, 400);
			see(hx, hy, hw, hh, line);
		};
		gearSee(wpn, c.reserved[6], c0, gearRow(0), w0, gearH);
		gearSee(arm, c.reserved[7], c1, gearRow(0), w1, gearH);
		accSee(0, c0, gearRow(1), w0, gearH);
		accSee(1, c1, gearRow(1), w1, gearH);
		{
			int b0 = EquippedBeadId(c, 0), b1 = EquippedBeadId(c, 1), b2 = EquippedBeadId(c, 2), b3 = EquippedBeadId(c, 3);
			see(c0, gearRow(2), w0, gearH, b0 > 0 ? CraftDesc(b0 - 1) : TT(TX_PT_NONE));
			see(c1, gearRow(2), w1, gearH, b1 > 0 ? CraftDesc(b1 - 1) : TT(TX_PT_NONE));
			see(c0, gearRow(3), w0, gearH, b2 > 0 ? CraftDesc(b2 - 1) : TT(TX_PT_NONE));
			see(c1, gearRow(3), w1, gearH, b3 > 0 ? CraftDesc(b3 - 1) : TT(TX_PT_NONE));
			int p0 = EquippedPasId(c, 0), p1 = EquippedPasId(c, 1);
			see(c0, gearRow(4), w0, gearH, p0 > 0 ? PasDesc(p0) : TT(TX_PT_NONE));
			see(c1, gearRow(4), w1, gearH, p1 > 0 ? PasDesc(p1) : TT(TX_PT_NONE));
			int s0 = EquippedSummonId(c, 0), s1 = EquippedSummonId(c, 1);
			see(c0, gearRow(5), w0, gearH, s0 > 0 ? SummonDesc(s0) : TT(TX_PT_NONE));
			see(c1, gearRow(5), w1, gearH, s1 > 0 ? SummonDesc(s1) : TT(TX_PT_NONE));
		}
		if (shown) DrawHoverNote(m_r, noteX, noteY, noteW, noteH, m_ui, m_w, m_h, shown);
	}
	wchar_t evLab[80];
	const wchar_t* evTxt = TT(TX_BASE_EVOLVE);
	if (EvoTrialOpen(c)) {
		swprintf_s(evLab, L"%s Lv%d", TT(TX_EV_TRIAL), c.trialLv + 3);
		evTxt = evLab;
	}
	if (!m_confirmDelete) mark(ID_PT_EVO, TX_PTH_EVO, dx + U(10), evoY, dw - U(20), evoH);
	if (!m_confirmDelete && Btn(ID_PT_EVO, dx + U(10), evoY, dw - U(20), evoH, evTxt, TRUE, FALSE, U(13))) {
		m_evFromParty = 1;
		int sel = m_partySel;
		EnterEvolve();
		m_evJump = 1;
		if (sel >= 0 && m_save.roster[sel].used && (EvoMainEligible(m_save.roster[sel]) || EvoTrialOpen(m_save.roster[sel]))) {
			m_evMain = sel; m_evPick = 1;
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
		mark(ID_PT_DEPLOY, TX_PTH_DEPLOY, dx + U(14), actY, bw3, actH);
		mark(ID_PT_EDIT, TX_PTH_EDIT, dx + U(14) + bw3 + U(8), actY, bw3, actH);
		mark(ID_PT_DELETE, TX_PTH_DELETE, dx + U(14) + (bw3 + U(8)) * 2, actY, bw3, actH);
		if (Btn(ID_PT_DEPLOY, dx + U(14), actY, bw3, actH, TT(TX_PT_DEPLOY), TRUE, inParty, U(14))) TogglePartyMember(m_partySel);
		if (Btn(ID_PT_EDIT, dx + U(14) + bw3 + U(8), actY, bw3, actH, TT(TX_CR_EDIT), TRUE, FALSE, U(14))) { m_eqPick = 0; m_createFromParty = 1; EnterCreate(m_partySel); return; }
		if (Btn(ID_PT_DELETE, dx + U(14) + (bw3 + U(8)) * 2, actY, bw3, actH, TT(TX_PT_DELETE), TRUE, FALSE, U(14), RGBA8(120, 40, 36, 230))) m_confirmDelete = 1;
	}
	if (!m_eqPick && tipTx >= 0)
		DrawHoverNote(m_r, tipAx, tipAy, tipAw, tipAh, m_ui, m_w, m_h, TT(tipTx));
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
	int fromCls = mc.cls;
	int roster = m_evMain;
	if (EvoTrialOpen(mc) && m_evTarget == mc.cls) {
		mc.trialFrom = -1;
		mc.trialLv = 0;
		Persist();
		InvalidatePortraits();
		m_evFlash = 0;
		TacSfx(TS_GET);
		m_partySel = m_evMain;
		m_evMain = -1;
		m_evPick = 0;
		m_evTarget = -1;
		m_evPartner[0] = m_evPartner[1] = -1;
		m_evPartPage = 0;
		BuildShowcase();
		return;
	}
	if (EvoTrialOpen(mc)) {
		EvoOption allow[4];
		int na = EvoOptions(mc.trialFrom, allow);
		bool ok = m_evTarget == mc.trialFrom;
		for (int k = 0; k < na; k++) if (allow[k].target == m_evTarget) ok = true;
		if (!ok) return;
		EvoRetrain(mc, m_evTarget);
	} else EvoApply(mc, m_evTarget, ps, n);
	m_evoCut = 1;
	m_evoT = 0;
	m_evoRoster = roster;
	m_evoFrom = fromCls;
	m_evoTo = mc.cls;
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
	m_evFlash = 0;
	TacSfx(TS_PROMOTE);
	m_partySel = m_evMain;
	m_evMain = -1;
	m_evPick = 0;
	m_evTarget = -1;
	m_evPartner[0] = m_evPartner[1] = -1;
	m_evPartPage = 0;
	BuildShowcase();
}

void Game::DrawEvoCut()
{
	if (m_click || m_activateId >= 0) { m_evoCut = 0; m_click = FALSE; m_activateId = -1; return; }
	float t = m_evoT;
	float cover = 0;
	if (t < 0.32f) cover = Smooth01(t / 0.32f);
	else if (t < 0.55f) cover = 1.f - Smooth01((t - 0.32f) / 0.23f);
	else if (t >= 2.15f && t < 2.42f) cover = Smooth01((t - 2.15f) / 0.27f);
	else if (t >= 2.42f) cover = 1.f - Smooth01((t - 2.42f) / 0.28f);
	float show = 0;
	if (t > 0.42f && t < 2.2f) {
		float inn = Smooth01((t - 0.42f) / 0.22f);
		float out = t > 1.85f ? 1.f - Smooth01((t - 1.85f) / 0.28f) : 1.f;
		show = inn * out;
	}
	float bar = U(78);
	if (show > 0.01f) {
		int a = (int)(230 * show);
		m_r.HudRect(0, 0, (float)m_w, bar, RGBA8(0, 0, 0, a));
		m_r.HudRect(0, (float)m_h - bar, (float)m_w, bar, RGBA8(0, 0, 0, a));
		m_r.HudRect(0, bar - U(4), (float)m_w, U(4), RGBA8(255, 210, 120, (int)(255 * show)));
		m_r.HudRect(0, (float)m_h - bar, (float)m_w, U(4), RGBA8(190, 140, 255, (int)(255 * show)));
		float bh = U(168);
		float by = m_h * .34f;
		float slide = (1.f - Smooth01((t - 0.42f) / 0.22f)) * m_w;
		m_r.HudRectGrad(-slide, by, (float)m_w, bh, RGBA8(18, 8, 28, (int)(210 * show)), RGBA8(70, 28, 90, (int)(230 * show)));
		m_r.HudRect(-slide, by, (float)m_w, U(6), RGBA8(255, 220, 140, (int)(255 * show)));
		m_r.HudRect(slide, by + bh - U(6), (float)m_w, U(6), RGBA8(210, 160, 255, (int)(255 * show)));
		const wchar_t* nm = (m_evoRoster >= 0 && m_evoRoster < MAX_ROSTER && m_save.roster[m_evoRoster].used) ? m_save.roster[m_evoRoster].name : L"";
		m_r.HudText(TT(TX_EV_TITLE), m_w * .5f, by + U(28), U(22), RGBA8(255, 226, 150), 1, 1, 1, show);
		m_r.HudText(ClassName(m_evoTo), m_w * .5f, by + bh * .48f, U(52), RGBA8(255, 236, 190), 1, 1, 1, show);
		wchar_t line[160];
		swprintf_s(line, L"%s    %s  →  %s", nm, ClassName(m_evoFrom), ClassName(m_evoTo));
		m_r.HudText(line, m_w * .5f, by + bh * .82f, U(16), RGBA8(240, 236, 248), 1, 0, 1, show);
		float ps = U(120);
		DrawPortrait(RosterPortrait(m_evoRoster), m_w * .5f - ps * .5f, by - ps - U(16), ps, RGBA8(220, 180, 255));
	}
	if (cover > 0.001f) {
		int rows = 9;
		float rh = (float)m_h / rows + 1;
		for (int i = 0; i < rows; i++) {
			float lag = i * .06f;
			float c = Clampf(cover * (1.f + rows * .06f) - lag, 0, 1);
			float w = c * m_w;
			float y = i * ((float)m_h / rows);
			if (i & 1) m_r.HudRect((float)m_w - w, y, w, rh, RGBA8(10, 6, 16, 255));
			else m_r.HudRect(0, y, w, rh, RGBA8(10, 6, 16, 255));
			if (c > 0 && c < 1) {
				if (i & 1) m_r.HudRect((float)m_w - w - U(6), y, U(6), rh, RGBA8(255, 220, 150, 220));
				else m_r.HudRect(w, y, U(6), rh, RGBA8(255, 220, 150, 220));
			}
		}
	}
}

int Game::NextJobPending(int from) const
{
	int cap = RosterCap();
	if (from < 0) from = 0;
	for (int i = from; i < cap; i++)
		if (m_save.roster[i].used && m_save.roster[i].jobDone == 0 && m_save.roster[i].lv >= PromoteLevel(1)) return i;
	return -1;
}
void Game::BeginJobRedo()
{
	int who = NextJobPending(0);
	if (who < 0) { m_jobRedo = 0; return; }
	m_jobWho = who;
	m_jobStep = 0;
	m_jobN = JobStepsForLevel(m_save.roster[who].lv);
	m_jobSkipAll = 0;
	m_jobSel = -1;
	for (int i = 0; i < 12; i++) m_jobPath[i] = -1;
	m_jobRedo = 2;
	m_screen = SC_EVOLVE;
	m_focusId = ID_JOB_OPT0;
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) m_portraitWant[i] = (i < MAX_ROSTER) ? i + 1 : 0;
}
void Game::FinishJobSkip()
{
	if (m_jobSkipAll) {
		int cap = RosterCap();
		for (int i = 0; i < cap; i++) if (m_save.roster[i].used && m_save.roster[i].jobDone == 0) m_save.roster[i].jobDone = 1;
		m_jobRedo = 0;
		Persist();
		EnterBase();
		return;
	}
	if (m_jobWho >= 0 && m_jobWho < MAX_ROSTER && m_save.roster[m_jobWho].used) m_save.roster[m_jobWho].jobDone = 1;
	Persist();
	int nxt = NextJobPending(m_jobWho + 1);
	if (nxt < 0) { m_jobRedo = 0; EnterBase(); return; }
	m_jobWho = nxt;
	m_jobStep = 0;
	m_jobN = JobStepsForLevel(m_save.roster[nxt].lv);
	m_jobSel = -1;
	for (int i = 0; i < 12; i++) m_jobPath[i] = -1;
	m_jobRedo = 2;
	m_screen = SC_EVOLVE;
}
void Game::ApplyJobPick(int cls)
{
	if (m_jobWho < 0 || m_jobWho >= MAX_ROSTER) return;
	CharData& c = m_save.roster[m_jobWho];
	if (!c.used || m_jobStep < 0 || m_jobStep >= 12) return;
	m_jobPath[m_jobStep] = cls;
	for (int i = m_jobStep + 1; i < 12; i++) m_jobPath[i] = -1;
	m_jobSel = -1;
	if (m_jobStep + 1 < m_jobN) { m_jobStep++; return; }
	int n = m_jobStep + 1;
	JobRebook(c, m_jobPath, n);
	JobRefitGear(c, m_save);
	AddBanner(c.name, ClassName(c.cls), RGBA8(255, 226, 150), 1, 2.2f, MSG_DEPTH);
	Persist();
	int nxt = NextJobPending(m_jobWho + 1);
	if (nxt < 0) {
		m_jobRedo = 0;
		AddBanner(TT(TX_JOB_DONE), L"", RGBA8(255, 226, 120), 1, 2.4f, MSG_DEPTH);
		EnterBase();
		return;
	}
	m_jobWho = nxt;
	m_jobStep = 0;
	m_jobN = JobStepsForLevel(m_save.roster[nxt].lv);
	m_jobSel = -1;
	for (int i = 0; i < 12; i++) m_jobPath[i] = -1;
}
void Game::DrawJobGate()
{
	float w = min(U(640.f), (float)m_w - U(40));
	float h = U(280);
	float x = ((float)m_w - w) * .5f;
	float y = ((float)m_h - h) * .5f;
	Panel(x, y, w, h);
	m_r.HudTextBox(TT(TX_JOB_BUG), x + U(24), y + U(28), w - U(48), U(80), U(20), RGBA8(255, 226, 150));
	if (m_jobRedo == 3) {
		m_r.HudTextBox(TT(TX_JOB_SKIP_Q), x + U(24), y + U(120), w - U(48), U(48), U(16), RGBA8(255, 180, 160));
		if (Btn(ID_JOB_YES, x + U(40), y + h - U(64), U(200), U(40), TT(TX_PT_YES), TRUE, FALSE, U(16), RGBA8(150, 48, 40, 240))) { FinishJobSkip(); return; }
		if (Btn(ID_JOB_NO, x + w - U(240), y + h - U(64), U(200), U(40), TT(TX_PT_NO))) { m_jobRedo = 1; return; }
		return;
	}
	if (Btn(ID_JOB_GO, x + U(40), y + h - U(64), U(220), U(40), TT(TX_JOB_GO), TRUE, FALSE, U(16), RGBA8(60, 110, 170, 240))) { BeginJobRedo(); return; }
	if (Btn(ID_JOB_SKIP, x + w - U(260), y + h - U(64), U(220), U(40), TT(TX_JOB_SKIP))) { m_jobSkipAll = 1; m_jobRedo = 3; }
}
void Game::DrawJobRedo()
{
	if (m_jobRedo == 3 && m_jobSkipAll) { DrawJobGate(); return; }
	if (m_jobWho < 0 || m_jobWho >= RosterCap() || !m_save.roster[m_jobWho].used || m_save.roster[m_jobWho].jobDone != 0) {
		int nxt = NextJobPending(0);
		if (nxt < 0) { m_jobRedo = 0; EnterBase(); return; }
		m_jobWho = nxt;
		m_jobStep = 0;
		m_jobN = JobStepsForLevel(m_save.roster[nxt].lv);
		m_jobSel = -1;
		for (int i = 0; i < 12; i++) m_jobPath[i] = -1;
	}
	const CharData& c = m_save.roster[m_jobWho];
	int ord = 0, total = 0;
	int cap = RosterCap();
	int jobGate = PromoteLevel(1);
	for (int i = 0; i < cap; i++) if (m_save.roster[i].used && m_save.roster[i].jobDone == 0 && m_save.roster[i].lv >= jobGate) {
		total++;
		if (i <= m_jobWho) ord++;
	}
	float w = min(U(720.f), (float)m_w - U(28));
	float h = min(U(640.f), (float)m_h - U(28));
	float x = ((float)m_w - w) * .5f;
	float y = ((float)m_h - h) * .5f;
	if (m_jobRedo == 3) {
		Panel(x, y, w, h);
		m_r.HudTextBox(TT(TX_JOB_SKIP_Q), x + U(28), y + h * .35f, w - U(56), U(64), U(18), RGBA8(255, 190, 170));
		if (Btn(ID_JOB_YES, x + U(48), y + h - U(72), U(220), U(42), TT(TX_PT_YES), TRUE, FALSE, U(16), RGBA8(150, 48, 40, 240))) { FinishJobSkip(); return; }
		if (Btn(ID_JOB_NO, x + w - U(268), y + h - U(72), U(220), U(42), TT(TX_PT_NO))) m_jobRedo = 2;
		return;
	}
	x = U(16);
	y = U(16);
	w = (float)m_w - U(32);
	h = (float)m_h - U(32);
	Panel(x, y, w, h);
	wchar_t head[128];
	swprintf_s(head, L"%s   %d / %d", c.name, ord, total);
	m_r.HudText(head, x + U(88), y + U(28), U(18), RGBA8(255, 255, 255), 0, 1);
	DrawPortrait(RosterPortrait(m_jobWho), x + U(16), y + U(14), U(64), RGBA8(200, 170, 110));
	wchar_t now[96];
	swprintf_s(now, L"%s  %s  Lv%d", TT(TX_JOB_NOW), ClassName(c.cls), c.lv);
	m_r.HudText(now, x + w - U(20), y + U(26), U(14), RGBA8(220, 210, 170), 2, 0);
	BOOL strUp = c.stat[S_STR] >= c.stat[S_MAG];
	BOOL hpUp = c.stat[S_HP] >= c.stat[S_MP];
	static const int kNowStat[] = { S_STR, S_MAG, S_DEF, S_RES, S_HP, S_MP };
	float kspan = (w - U(120)) / 6.f;
	float ky0 = y + U(50);
	for (int i = 0; i < 6; i++) {
		int st = kNowStat[i];
		wchar_t bit[32];
		swprintf_s(bit, L"%s %d", TT(StatNameTx(st)), c.stat[st]);
		DWORD col = RGBA8(168, 168, 176);
		if (st == S_STR || st == S_DEF) col = strUp ? RGBA8(255, 196, 130) : RGBA8(150, 150, 158);
		else if (st == S_MAG || st == S_RES) col = strUp ? RGBA8(150, 150, 158) : RGBA8(160, 200, 255);
		else if (st == S_HP) col = hpUp ? RGBA8(230, 230, 230) : RGBA8(150, 150, 158);
		else if (st == S_MP) col = hpUp ? RGBA8(150, 150, 158) : RGBA8(170, 210, 230);
		m_r.HudText(bit, x + U(88) + kspan * (float)i, ky0, U(15), col, 0, 1);
	}
	m_r.HudTextBox(m_jobStep == 0 ? TT(TX_JOB_ASK1) : TT(TX_JOB_ASKN), x + U(20), y + U(78), w - U(40), U(40), U(16), RGBA8(255, 226, 150));
	wchar_t chain[320];
	chain[0] = 0;
	for (int i = 0; i < m_jobStep && i < 12; i++) {
		if (m_jobPath[i] < 0) break;
		wchar_t bit[72];
		swprintf_s(bit, TT(TX_JOB_NTH), i + 1, ClassName(m_jobPath[i]));
		if (i) wcscat_s(chain, L" → ");
		wcscat_s(chain, bit);
	}
	if (chain[0]) m_r.HudTextBox(chain, x + U(20), y + U(122), w - U(40), U(32), U(13), RGBA8(210, 220, 230));
	int opt[8], no = 0;
	if (m_jobStep <= 0) {
		static const int base[6] = { C_SOLDIER, C_FIGHTER, C_ARCHER, C_THIEF, C_MAGE, C_CLERIC };
		for (int i = 0; i < 6; i++) opt[no++] = base[i];
	} else {
		EvoOption raw[4];
		int nr = EvoOptions(m_jobPath[m_jobStep - 1], raw);
		int rootM = ClassInfo(m_jobPath[0]).magic;
		for (int pass = 0; pass < 2 && no == 0; pass++) {
			no = 0;
			for (int k = 0; k < nr && no < 8; k++) {
				int t = raw[k].target;
				if (t < 0) continue;
				if (pass == 0 && ClassInfo(t).magic != rootM) continue;
				opt[no++] = t;
			}
		}
	}
	BOOL selOk = FALSE;
	for (int k = 0; k < no; k++) if (opt[k] == m_jobSel) selOk = TRUE;
	if (!selOk) m_jobSel = -1;
	float leftW = min(U(860.f), w * .62f);
	float top = y + U(162);
	float bot = y + h - U(64);
	float avail = bot - top;
	float rowH = no > 0 ? avail / (float)no : U(72);
	if (rowH > U(92)) rowH = U(92);
	if (rowH < U(52)) rowH = U(52);
	float bw = min(U(210.f), leftW * .28f);
	float ty = top;
	for (int k = 0; k < no; k++) {
		BOOL on = opt[k] == m_jobSel;
		if (Btn(ID_JOB_OPT0 + k, x + U(16), ty, bw, rowH - U(6), ClassName(opt[k]), TRUE, on, U(15))) m_jobSel = opt[k];
		int pth[12], pn = 0;
		for (int i = 0; i < m_jobStep && i < 12; i++) {
			if (m_jobPath[i] < 0) break;
			pth[pn++] = m_jobPath[i];
		}
		if (pn < 12) pth[pn++] = opt[k];
		CharData pv = c;
		JobProject(pv, pth, pn, 1);
		float dx = x + U(24) + bw;
		float dw = leftW - bw - U(36);
		float statH = U(18);
		float descH = rowH - U(8) - statH;
		if (descH < U(18)) { descH = rowH - U(8); statH = 0; }
		m_r.HudTextBox(ClassBlurb(opt[k]), dx, ty + U(2), dw, descH, U(13), on ? RGBA8(255, 236, 210) : RGBA8(214, 216, 222));
		if (statH > 0) {
			wchar_t sa[24], sb[24], sc2[24], sd[24];
			swprintf_s(sa, L"%s %d", TT(TX_ST_STR), pv.stat[S_STR]);
			swprintf_s(sb, L"%s %d", TT(TX_ST_MAG), pv.stat[S_MAG]);
			swprintf_s(sc2, L"%s %d", TT(TX_ST_HP), pv.stat[S_HP]);
			swprintf_s(sd, L"%s %d", TT(TX_ST_MP), pv.stat[S_MP]);
			float kw = max(U(40.f), dw / 4.f);
			float ky = ty + U(4) + descH;
			DWORD scol = pv.stat[S_STR] >= pv.stat[S_MAG] ? RGBA8(255, 186, 120) : RGBA8(170, 170, 180);
			DWORD mcol = pv.stat[S_MAG] > pv.stat[S_STR] ? RGBA8(160, 196, 255) : RGBA8(170, 170, 180);
			m_r.HudText(sa, dx, ky, U(13), scol, 0, 1);
			m_r.HudText(sb, dx + kw, ky, U(13), mcol, 0, 1);
			m_r.HudText(sc2, dx + kw * 2.f, ky, U(13), RGBA8(230, 230, 230), 0, 1);
			m_r.HudText(sd, dx + kw * 3.f, ky, U(13), RGBA8(190, 220, 230), 0, 1);
		}
		ty += rowH;
	}
	float rx = x + leftW + U(8);
	float rw = w - leftW - U(28);
	CharData shown = c;
	CharData arrived = c;
	int have = 0;
	int pre[12];
	for (int i = 0; i < m_jobStep && i < 12; i++) {
		if (m_jobPath[i] < 0) break;
		pre[have++] = m_jobPath[i];
	}
	if (m_jobSel >= 0) {
		int pth[12], pn = have;
		for (int i = 0; i < have; i++) pth[i] = pre[i];
		pth[pn++] = m_jobSel;
		shown = c;
		JobProject(shown, pth, pn, 1);
		if (have > 0) {
			arrived = c;
			JobProject(arrived, pre, have, 1);
		} else {
			arrived = shown;
			const ClassDef& b = ClassInfo(m_jobSel);
			for (int s = 0; s < S_COUNT; s++) arrived.stat[s] = b.base[s];
			if (c.gender == 1) {
				arrived.stat[S_AGI] += 1;
				arrived.stat[S_RES] += 1;
				arrived.stat[S_STR] = max(1, arrived.stat[S_STR] - 1);
			}
		}
	} else if (have > 0) {
		shown = c;
		JobProject(shown, pre, have, 1);
	}
	wchar_t title[128];
	if (m_jobSel >= 0) swprintf_s(title, L"%s  %s  Lv%d", TT(TX_JOB_PRE), ClassName(shown.cls), shown.lv);
	else swprintf_s(title, L"%s  %s  Lv%d", TT(TX_JOB_NOW), ClassName(shown.cls), shown.lv);
	m_r.HudText(title, rx, top, U(15), RGBA8(255, 226, 150), 0, 1);
	int gains[S_COUNT];
	const int* gp = NULL;
	if (m_jobSel >= 0) {
		for (int s = 0; s < S_COUNT; s++) gains[s] = shown.stat[s] - arrived.stat[s];
		gp = gains;
	}
	float used = DrawStatLines(shown, rx, top + U(24), rw, gp, -1, -1);
	float briefTop = top + U(28) + used;
	float briefH = bot - briefTop - U(8);
	if (briefH > U(48) && (m_jobSel >= 0 || have > 0 || m_jobStep == 0)) {
		wchar_t brief[1024];
		EvoBrief(shown.cls, brief, 1024);
		m_r.HudTextBox(brief, rx, briefTop, rw, briefH, U(13), RGBA8(220, 220, 226));
	}
	if (no == 0 && m_jobStep > 0) {
		if (Btn(ID_JOB_GO, x + U(16), ty, leftW - U(24), U(40), TT(TX_JOB_GO), TRUE, FALSE, U(16))) {
			int n = m_jobStep;
			if (n < 1) n = 1;
			JobRebook(m_save.roster[m_jobWho], m_jobPath, n);
			JobRefitGear(m_save.roster[m_jobWho], m_save);
			Persist();
			int nxt = NextJobPending(m_jobWho + 1);
			if (nxt < 0) { m_jobRedo = 0; EnterBase(); return; }
			m_jobWho = nxt;
			m_jobStep = 0;
			m_jobN = JobStepsForLevel(m_save.roster[nxt].lv);
			m_jobSel = -1;
			for (int i = 0; i < 12; i++) m_jobPath[i] = -1;
			return;
		}
	}
	if (Btn(ID_JOB_BACK, x + U(16), y + h - U(52), U(150), U(36), TT(TX_CR_BACK))) {
		if (m_jobStep > 0) {
			m_jobStep--;
			m_jobSel = m_jobPath[m_jobStep];
			for (int i = m_jobStep; i < 12; i++) m_jobPath[i] = -1;
		} else { m_jobRedo = 1; m_screen = SC_BASE; }
		return;
	}
	if (Btn(ID_JOB_NEXT, x + w * .5f - U(110), y + h - U(52), U(220), U(40), TT(TX_JOB_NEXT), m_jobSel >= 0, FALSE, U(16), RGBA8(60, 110, 170, 240))) {
		ApplyJobPick(m_jobSel);
		return;
	}
	if (Btn(ID_JOB_SKIP, x + w - U(180), y + h - U(52), U(160), U(36), TT(TX_JOB_SKIP))) { m_jobSkipAll = 0; m_jobRedo = 3; }
}

void Game::DrawEvolveUi()
{
	if (m_jobRedo == 2 || m_jobRedo == 3) { DrawJobRedo(); return; }
	if (m_evoCut) { DrawEvoCut(); return; }
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
		if (!held.used || (!EvoMainEligible(held) && !EvoTrialOpen(held))) {
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
		if (!c.used || (!EvoMainEligible(c) && !EvoTrialOpen(c))) continue;
		if (!EvoTrialOpen(c)) {
			EvoOption opt[4];
			if (EvoOptions(c.cls, opt) == 0) continue;
		}
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
		BOOL trial = EvoTrialOpen(mc);
		wchar_t head[96];
		if (trial) swprintf_s(head, L"%s  Lv%d", TT(TX_EV_TRIAL), mc.trialLv + 3);
		else wcscpy_s(head, TT(TX_EV_TARGET));
		m_r.HudText(head, c1, top + U(8), U(16), RGBA8(255, 226, 150), 0, 1);
		wchar_t path[640];
		JobPathText(mc, path, 640);
		if (path[0]) wcscat_s(path, L" →");
		float pathH = U(44);
		if (path[0]) m_r.HudTextBox(path, c1, top + U(28), colW, pathH, U(13), RGBA8(210, 220, 235));
		int tgt[5];
		int nt = 0;
		int stayCls = (trial && mc.cls >= 0) ? mc.cls : -1;
		wchar_t stayLb[96];
		stayLb[0] = 0;
		if (stayCls >= 0) swprintf_s(stayLb, L"%s(%s)", TT(TX_EV_REVERT), ClassName(mc.cls));
		EvoOption opt[4];
		int no = EvoOptions(trial ? mc.trialFrom : mc.cls, opt);
		for (int k = 0; k < no && nt < 4; k++) {
			if (trial && opt[k].target == mc.cls) continue;
			tgt[nt++] = opt[k].target;
		}
		if (m_evTarget >= 0) {
			BOOL still = stayCls >= 0 && m_evTarget == stayCls;
			for (int k = 0; k < nt; k++) if (tgt[k] == m_evTarget) still = TRUE;
			if (!still) {
				m_evTarget = -1;
				m_evPartner[0] = m_evPartner[1] = -1;
				m_evPick = 1;
			}
		}
		int need = EvoPartnersNeeded(mc);
		int step = JobPathCount(mc);
		if (step < 1) step = max(1, ClassInfo(mc.cls).tier);
		static const int gtx[G_COUNT] = { TX_G_MELEE, TX_G_SWIFT, TX_G_MAGIC, TX_G_HOLY };
		float ty = top + U(28) + (path[0] ? pathH + U(6) : 0.f);
		for (int k = 0; k < nt; k++) {
			const ClassDef& cd = ClassInfo(tgt[k]);
			wchar_t nth[96], l[160];
			swprintf_s(nth, TT(TX_JOB_NTH), step + 1, ClassName(tgt[k]));
			swprintf_s(l, L"%s   %s", nth, TT(gtx[Clampi(cd.group, 0, G_COUNT - 1)]));
			if (Btn(ID_EV_TGT0 + k, c1, ty, colW, rowH - U(6), l, TRUE, m_evTarget == tgt[k], U(16))) {
				m_evTarget = tgt[k]; m_evPartner[0] = m_evPartner[1] = -1; m_evPick = 2;
			}
			if (m_mouseSeen && Hit(c1, ty, colW, rowH - U(6)) && m_evTarget != tgt[k])
				DrawHoverNote(m_r, c1, ty, colW, rowH - U(6), m_ui, m_w, m_h, ClassBlurb(tgt[k]));
			if (need > 0 && !trial) {
				wchar_t g[96];
				int ng = -1;
				for (int t = 0; t < no; t++) if (opt[t].target == tgt[k]) ng = opt[t].needGroup;
				swprintf_s(g, L"%s: %s", TT(TX_EV_NEED_GROUP), ng < 0 ? TT(TX_EV_ANY) : TT(gtx[ng]));
				m_r.HudText(g, c1 + U(8), ty + rowH + U(2), U(12), RGBA8(200, 200, 210), 0, 0);
				ty += rowH + U(22);
			} else ty += rowH;
		}
		if (stayCls >= 0) {
			if (Btn(ID_EV_TGT0 + 4, c1, ty, colW, rowH - U(6), stayLb, TRUE, m_evTarget == stayCls, U(15))) {
				m_evTarget = stayCls; m_evPartner[0] = m_evPartner[1] = -1; m_evPick = 2;
			}
			ty += rowH;
		}
		if (m_evTarget >= 0) {
			wchar_t brief[1024];
			EvoBrief(m_evTarget, brief, 1024);
			float foot = y + h - U(64);
			float blurbH = U(132);
			if (ty + U(8) + blurbH > foot - U(16)) blurbH = max(U(96.f), foot - U(16) - (ty + U(8)));
			m_r.HudTextBox(brief, c1, ty + U(6), colW, blurbH, U(14), RGBA8(226, 226, 232));
			ty += blurbH + U(8);
			m_r.HudText(TT(TX_EV_PREVIEW), c1, ty + U(8), U(14), RGBA8(255, 226, 150), 0, 1);
			CharData pv = mc;
			CharData ps[2];
			int n = 0;
			for (int kk = 0; kk < 2; kk++) if (m_evPartner[kk] >= 0) ps[n++] = m_save.roster[m_evPartner[kk]];
			if (trial && m_evTarget == mc.cls) { /* この職のまま。能力は動かさない */ }
			else if (trial) EvoRetrain(pv, m_evTarget);
			else EvoApply(pv, m_evTarget, ps, n);
			int gains[S_COUNT];
			for (int s = 0; s < S_COUNT; s++) gains[s] = pv.stat[s] - mc.stat[s];
			DrawStatLines(pv, c1, ty + U(22), colW, gains, -1, -1);
		}
		if (m_evTarget >= 0 && EvoPartnersNeeded(mc) <= 0) {
			m_btnSfx = -2;
			const wchar_t* doLb = (stayCls >= 0 && m_evTarget == stayCls && stayLb[0]) ? stayLb : TT(TX_EV_DO);
			if (Btn(ID_EV_DO, c1, y + h - U(56), colW * .72f, U(40), doLb, TRUE, FALSE, U(18), RGBA8(150, 90, 200, 240))) { DoEvolve(); return; }
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
	StageCaption(m_battleStage, st, 64);
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
		if (c.lv > m_resultLv0[i]) swprintf_s(l, L"%s %s → %s", TT(TX_MISC_LV), TacNum(m_resultLv0[i]), TacNum(c.lv));
		else swprintf_s(l, L"%s %s", TT(TX_MISC_LV), TacNum(c.lv));
		float nameEnd = x + U(30) + rowH + U(4) + U(160);
		float lvW = max(40.f, x + w * .62f - nameEnd);
		m_r.HudText(l, nameEnd + lvW * .5f, ry + rowH * .3f, FitTextPx(l, lvW, U(16), 1), c.lv > m_resultLv0[i] ? RGBA8(140, 255, 150) : RGBA8(230, 230, 230), 1, 1);
		int need = ExpToNext(c.lv);
		int left = c.lv >= TAC_LV_MAX ? 0 : max(0, need - c.exp);
		swprintf_s(l, L"%s +%s  %s %s", TT(TX_BT_EXP), TacNum(m_resultExp[i]), TT(TX_EXP_LEFT), TacNum(left));
		m_r.HudText(l, nameEnd + lvW * .5f, ry + rowH * .64f, FitTextPx(l, lvW, U(13), 0), RGBA8(200, 220, 255), 1, 0);
		Bar(x + w * .66f, ry + rowH * .4f, w * .28f, U(10), need > 0 ? c.exp / (float)need : 0.f, RGBA8(120, 200, 255), RGBA8(60, 120, 220));
		ry += rowH;
	}
	float by = y + h - U(60);
	float gap = U(12);
	BOOL canFight = PartyCount() > 0;
	int play = StagePlayMax(m_save.stageCleared);
	int nBtn = 2;
	if (m_victory && m_battleStage < play) nBtn = 3;
	float bw = min(U(200), (w - gap * (float)(nBtn - 1) - U(24)) / (float)nBtn);
	if (bw < U(120)) bw = U(120);
	float bx = x + (w - ((float)nBtn * bw + (float)(nBtn - 1) * gap)) * .5f;
	if (m_victory) {
		if (m_battleStage < play) {
			if (Btn(ID_RS_NEXT, bx, by, bw, U(42), TT(TX_BT_NEXT), canFight, FALSE, U(17), RGBA8(170, 60, 50, 240))) {
				m_save.stageSel = Clampi(m_battleStage + 1, 1, play);
				StartBattle(m_save.stageSel);
				return;
			}
			bx += bw + gap;
		}
		if (Btn(ID_RS_LOOP, bx, by, bw, U(42), TT(TX_BT_LOOP), canFight, FALSE, U(17), RGBA8(170, 60, 50, 240))) {
			m_save.stageSel = m_battleStage;
			StartBattle(m_battleStage);
			return;
		}
		bx += bw + gap;
	} else {
		if (Btn(ID_RS_RETRY, bx, by, bw, U(42), TT(TX_BT_RETRY), canFight, FALSE, U(17), RGBA8(170, 60, 50, 240))) {
			StartBattle(m_battleStage);
			return;
		}
		bx += bw + gap;
	}
	if (Btn(ID_RS_BASE, bx, by, bw, U(42), TT(TX_BT_TOBASE), TRUE, FALSE, U(17))) {
		m_showBuilt = FALSE;
		EnterBase();
		return;
	}
}

} // namespace otac
