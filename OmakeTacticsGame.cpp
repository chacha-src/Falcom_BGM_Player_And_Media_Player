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
	ID_CR_NAME = 200, ID_CR_RANDOM, ID_CR_GENDER, ID_CR_LINEAGE, ID_CR_CLASS, ID_CR_BODY, ID_CR_HAIR, ID_CR_HAIRCOL, ID_CR_OUTFIT,
	ID_CR_HAT, ID_CR_CAPE, ID_CR_OK, ID_CR_BACK,
	ID_PT_CARD0 = 300, ID_PT_DEPLOY = 340, ID_PT_EDIT, ID_PT_DELETE, ID_PT_YES, ID_PT_NO, ID_PT_BACK, ID_PT_NEW,
	ID_EV_MAIN0 = 400, ID_EV_TGT0 = 430, ID_EV_PART0 = 440, ID_EV_DO = 470, ID_EV_BACK,
	ID_RS_NEXT = 700, ID_RS_RETRY, ID_RS_BASE,
	ID_HELP_CLOSE = 900
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
	m_padRepeat = 0; m_padDirPrev = -1;
	m_fastFwd = FALSE;
	m_showHelp = FALSE;
	m_codex = 0;
	m_codexPage = 0;
	m_skillPage = 0;
	m_ult = FALSE;
	m_confirmDelete = 0;
	m_camTarget = m_camGoal = v3(32, 0, 32);
	m_camYaw = m_camYawGoal = PI * .25f;
	m_camPitch = .9f;
	m_camDist = m_camDistGoal = 13.f;
	m_camPan = v3(0, 0, 0);
	m_shake = 0;
	m_eye = v3(0, 10, 0);
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) { m_portraitKey[i] = -1; m_portraitWant[i] = 0; }
	memset(&m_ptActor, 0, sizeof(m_ptActor));
	memset(&m_showMap, 0, sizeof(m_showMap));
	m_showBuilt = FALSE;
	m_showStage = 0;
	m_showN = 0;
	m_showYaw = 0;
	memset(&m_edit, 0, sizeof(m_edit));
	m_editIdx = -1;
	m_previewYaw = 0;
	m_lineage = 0;
	m_previewDrag = FALSE;
	m_partySel = -1;
	m_evMain = m_evTarget = -1;
	m_evPartner[0] = m_evPartner[1] = -1;
	m_evPick = 0;
	m_evFlash = 0;
	memset(&m_b, 0, sizeof(m_b));
	m_phase = m_phaseBeforeMenu = PH_NONE;
	m_phaseT = 0;
	m_cur = -1;
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
	m_seqCaster = m_seqPrimary = -1;
	m_seqSkill = -1;
	m_battleStage = 1;
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
	m_rng = Rng(GetTickCount() ^ 0x5EED1234u);
	m_screen = SC_LOADING;
	m_loadFrames = 0;
	m_ui = Clampf(min(m_w / 1280.f, m_h / 720.f), .62f, 2.2f);
	if (!m_r.Init(view, m_save.quality)) { m_screen = SC_FAIL; return FALSE; }
	return TRUE;
}

BOOL Game::LoadEverything()
{
	if (!m_r.LoadAssets()) return FALSE;
	if (!BuildLibrary(m_r, m_lib)) return FALSE;
	m_loaded = TRUE;
	return TRUE;
}

void Game::Shutdown()
{
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

int Game::PartyCount() const
{
	int n = 0;
	for (int i = 0; i < MAX_PARTY; i++) if (m_save.party[i] >= 0) n++;
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
	else swprintf_s(out, n, L"%s%s", u.boss ? L"★" : L"", TT(MonsterInfo(u.monster).nameTx));
}

// ============================================================ メインループ
void Game::Tick(float dt)
{
	dt = Clampf(dt, 0, .1f);
	m_time += dt;
	if (m_screen == SC_LOADING) {
		if (m_loadFrames++ >= 2) {
			if (!LoadEverything()) m_screen = SC_FAIL;
			else EnterBase();
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
	if (!m_r.Ready()) return;
	if (!m_r.FrameReady()) return;
	RenderFrame();
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
	switch (m_screen) {
	case SC_LOADING:
		m_r.HudText(TT(TX_TITLE), m_w * .5f, m_h * .42f, U(40), RGBA8(255, 230, 160), 1, 1);
		m_r.HudText(TT(TX_MISC_LOADING), m_w * .5f, m_h * .55f, U(20), RGBA8(230, 230, 240), 1, 0);
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
	if (m_evFlash > 0) m_r.HudRect(0, 0, (float)m_w, (float)m_h, RGBA8(255, 255, 255, (int)(m_evFlash * 200)));
	if (m_screen == SC_BATTLE && m_click && !m_showHelp) {
		m_click = FALSE;
		BattleClick(m_clickX, m_clickY);
	}
	m_r.EndHud();
	m_r.Present();
	m_r.EndFrameTextGc();
	m_btnsPrev = m_btns;
	m_click = FALSE;
	m_rclick = FALSE;
	m_activateId = -1;
	m_selDelta = 0;
}

// ============================================================ 入力
void Game::OnMouseMove(int x, int y)
{
	if (m_lDown && m_screen == SC_CREATE && m_previewDrag) m_previewYaw += (x - m_mx) * .012f;
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
		if (m_mDown || (scout && m_dragDist > 6)) {
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
	}
	if (btn == 1) { m_rDown = TRUE; m_dragRot = FALSE; m_dragDist = 0; m_dragLastX = x; m_dragLastY = y; }
	if (btn == 2) { m_mDown = TRUE; m_dragPan = FALSE; m_dragDist = 0; m_dragLastX = x; m_dragLastY = y; }
}

void Game::OnMouseUp(int btn, int x, int y)
{
	m_mx = x; m_my = y;
	if (btn == 0 && m_lDown) {
		m_lDown = FALSE;
		m_previewDrag = FALSE;
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
	if (btn == 2) { m_mDown = FALSE; m_dragPan = FALSE; }
}

void Game::OnWheel(int delta)
{
	if (m_screen == SC_BATTLE || m_screen == SC_BASE) {
		m_camDistGoal = Clampf(m_camDistGoal * (delta > 0 ? .88f : 1.14f), 5.f, 32.f);
	}
}

void Game::OnChar(wchar_t ch)
{
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
	case VK_PRIOR: case VK_ADD: case VK_OEM_PLUS: m_camDistGoal = Clampf(m_camDistGoal * .88f, 5.f, 32.f); break;
	case VK_NEXT: case VK_SUBTRACT: case VK_OEM_MINUS: m_camDistGoal = Clampf(m_camDistGoal * 1.14f, 5.f, 32.f); break;
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
	BOOL battleMap = m_screen == SC_BATTLE && (m_phase == PH_P_MOVE || m_phase == PH_P_TARGET || m_phase == PH_P_SKILLTARGET || m_phase == PH_P_SHOVE) && !m_showHelp;
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
	if (pressed(0)) {
		if (m_showHelp) m_showHelp = FALSE;
		else if (battleMap && !m_navMode) BattleConfirmCursor();
		else NavConfirm();
	}
	if (pressed(1)) { if (m_showHelp) m_showHelp = FALSE; else NavBack(); }
	if (pressed(4)) m_camYawGoal -= PI * .5f;
	if (pressed(5)) m_camYawGoal += PI * .5f;
	if (pressed(6)) m_showHelp = !m_showHelp;
	if (pressed(7) && m_screen == SC_BATTLE) NavBack();
	if (pressed(3) && m_screen == SC_BATTLE && m_cur >= 0) { m_cx = m_b.u[m_cur].x; m_cz = m_b.u[m_cur].z; m_camPan = v3(0, 0, 0); UpdateHover(); }
	m_fastFwd = m_keyShift || (p.buttons & 4) != 0;
	if (fabsf(p.rx) > .2f) m_camYawGoal += p.rx * dt * 2.2f;
	if (fabsf(p.ry) > .2f) m_camPitch = Clampf(m_camPitch + p.ry * dt * 1.2f, .45f, 1.35f);
	if (p.lt > .3f) m_camDistGoal = Clampf(m_camDistGoal * (1.f + dt * 1.5f * p.lt), 5.f, 32.f);
	if (p.rt > .3f) m_camDistGoal = Clampf(m_camDistGoal * (1.f - dt * 1.5f * p.rt), 5.f, 32.f);
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
	m_r.HudRectGrad(x, y, w, h, RGBA8(24, 28, 52, 226), RGBA8(12, 14, 28, 232));
	m_r.HudFrame(x, y, w, h, U(1.5f), RGBA8(196, 168, 108, (int)(255 * alpha)));
	m_r.HudFrame(x + U(3), y + U(3), w - U(6), h - U(6), U(1), RGBA8(110, 92, 60, (int)(160 * alpha)));
}

void Game::Bar(float x, float y, float w, float h, float frac, DWORD c0, DWORD c1, float ghost)
{
	m_r.HudRect(x, y, w, h, RGBA8(10, 10, 16, 220));
	frac = Clampf(frac, 0, 1);
	if (ghost > frac) m_r.HudRect(x + 1, y + 1, (w - 2) * Clampf(ghost, 0, 1), h - 2, RGBA8(255, 240, 200, 200));
	m_r.HudRectGrad(x + 1, y + 1, (w - 2) * frac, h - 2, c0, c1);
	m_r.HudFrame(x, y, w, h, 1, RGBA8(0, 0, 0, 200));
}

BOOL Game::Btn(int id, float x, float y, float w, float h, const wchar_t* label, BOOL en, BOOL sel, float px, DWORD tint)
{
	UiBtn b = { id, x, y, w, h, en ? 1 : 0, 0 };
	m_btns.push_back(b);
	BOOL hov = !m_navMode && m_mouseSeen && Hit(x, y, w, h);
	BOOL foc = m_navMode && m_focusId == id;
	BOOL fire = FALSE;
	if (en && ClickIn(x, y, w, h)) fire = TRUE;
	else if (!en && ClickIn(x, y, w, h)) TacSfx(TS_ERROR);
	if (en && m_activateId == id) { fire = TRUE; m_activateId = -1; }
	if (fire) TacSfx(TS_CONFIRM);
	DWORD top, bot;
	if (!en) { top = RGBA8(58, 58, 66, 210); bot = RGBA8(38, 38, 46, 210); }
	else if (sel) { top = RGBA8(240, 200, 100); bot = RGBA8(176, 122, 40); }
	else if (hov || foc) { top = RGBA8(96, 118, 186); bot = RGBA8(52, 64, 120); }
	else if (tint) { top = tint; bot = RGBA8((tint & 255) / 2, ((tint >> 8) & 255) / 2, ((tint >> 16) & 255) / 2, 235); }
	else { top = RGBA8(58, 66, 104, 238); bot = RGBA8(30, 34, 62, 238); }
	m_r.HudRectGrad(x, y, w, h, top, bot);
	m_r.HudRect(x + 2, y + 2, w - 4, h * .35f, RGBA8(255, 255, 255, en ? 22 : 8));
	m_r.HudFrame(x, y, w, h, U(1.5f), (hov || foc) ? RGBA8(255, 232, 150) : RGBA8(150, 128, 86));
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
	m_r.HudRectGrad(x, y, w, h, (hov || foc) ? RGBA8(64, 78, 130, 236) : RGBA8(34, 40, 70, 230), RGBA8(20, 24, 44, 236));
	m_r.HudFrame(x, y, w, h, U(1), (hov || foc) ? RGBA8(255, 232, 150) : RGBA8(120, 104, 74));
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
	if (bestId >= 0 && bestId != m_focusId) { m_focusId = bestId; TacSfx(TS_CURSOR); }
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
	switch (m_screen) {
	case SC_CREATE: if (m_textFocus) m_textFocus = 0; else EnterParty(); break;
	case SC_PARTY:
		if (m_confirmDelete) m_confirmDelete = 0;
		else EnterBase();
		break;
	case SC_EVOLVE:
		if (m_evPick > 0) { m_evPick--; if (m_evPick == 0) m_evTarget = -1; m_evPartner[0] = m_evPartner[1] = -1; }
		else EnterBase();
		break;
	case SC_BATTLE: BattleCancel(); break;
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

void Game::AddPopup(V3 p, const wchar_t* s, DWORD col, int style, float delay)
{
	Popup q = {};
	q.p = p;
	wcsncpy_s(q.s, s, _TRUNCATE);
	q.col = col; q.style = style; q.life = style == 2 ? 1.6f : 1.25f; q.delay = delay;
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
	float w = min((float)m_w - U(60), U(820)), h = min((float)m_h - U(60), U(560));
	float x = (m_w - w) * .5f, y = (m_h - h) * .5f;
	Panel(x, y, w, h);
	m_r.HudText(TT(TX_BT_HELP), x + w * .5f, y + U(30), U(24), RGBA8(255, 226, 150), 1, 1);
	m_r.HudTextBox(TT(TX_HELP_TEXT), x + U(28), y + U(60), w - U(56), h - U(130), U(15), RGBA8(236, 236, 240));
	if (Btn(ID_HELP_CLOSE, x + w * .5f - U(80), y + h - U(56), U(160), U(38), TT(TX_BASE_CLOSE))) m_showHelp = FALSE;
}

void Game::DrawStatLines(const CharData& c, float x, float y, float w, const int* gains, int hpCur, int mpCur)
{
	float lh = U(22);
	for (int s = 0; s < S_COUNT; s++) {
		float col = (float)(s % 2), row = (float)(s / 2);
		float sx = x + col * w * .5f, sy = y + row * lh;
		m_r.HudText(TT(StatNameTx(s)), sx, sy + lh * .5f, U(14), RGBA8(200, 190, 160), 0, 0);
		wchar_t v[32];
		if (s == S_HP && hpCur >= 0) swprintf_s(v, L"%d/%d", hpCur, c.stat[s]);
		else if (s == S_MP && mpCur >= 0) swprintf_s(v, L"%d/%d", mpCur, c.stat[s]);
		else swprintf_s(v, L"%d", c.stat[s]);
		m_r.HudText(v, sx + w * .5f - U(14), sy + lh * .5f, U(15), RGBA8(250, 250, 250), 2, 1);
		if (gains && gains[s] != 0) {
			wchar_t g[16];
			swprintf_s(g, L"%+d", gains[s]);
			m_r.HudText(g, sx + w * .5f - U(10), sy + lh * .5f, U(13), gains[s] > 0 ? RGBA8(120, 255, 140) : RGBA8(255, 120, 110), 0, 1);
		}
	}
}

// ============================================================ ポートレート
void Game::InvalidatePortraits()
{
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) m_portraitKey[i] = -1;
}

void Game::RenderActorPortrait(int slot, Actor& a, DWORD bg0, DWORD bg1)
{
	ActorPlay(a, CL_IDLE, 1, 0);
	a.t = .4f;
	ActorPose(a, m_lib, m_r);
	M4 world = ActorWorld(a, v3(0, 0, 0), PI);
	V3 look;
	float d;
	if (!a.proc) {
		look = ActorJointWorld(a, world, 0);
		d = a.height * .95f;
	} else {
		look = ActorJointWorld(a, world, 0);
		look.y = Lerpf(look.y, a.height * .55f, .5f);
		d = a.height * 1.35f;
	}
	V3 eye = look + v3(d * .3f, d * .12f, -d);
	M4 view = MLookAtLH(eye, look, v3(0, 1, 0));
	M4 proj = MPerspLH(.6f, 1.f, .05f, 50.f);
	if (!m_r.BeginPortrait(slot)) return;
	m_r.SetCamera(view, proj, eye, m_time);
	m_r.SetFog(500.f, 1000.f, 0, RGBA8(0, 0, 0));
	m_r.SetAmbient(RGBA8(170, 190, 230), RGBA8(120, 108, 90), .6f, .6f);
	m_r.SetLight(Norm(v3(.35f, .75f, -.55f)), look, 3.f);
	m_r.SetShadowEnabled(FALSE);
	m_r.BeginMainPass();
	m_r.DrawSky(bg0, bg1, 0, 0);
	DrawParams dp;
	dp.outline = .008f;
	dp.rim = .5f;
	ActorDraw(m_r, m_lib, a, world, dp, 0);
	m_r.EndPortrait();
}

void Game::RenderPortraits()
{
	int budget = 3;
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
				ActorFromMonster(m_ptActor, m_lib, m_r, u.monster, u.boss);
				RenderActorPortrait(s, m_ptActor, u.boss ? RGBA8(200, 140, 40) : RGBA8(180, 60, 60), RGBA8(50, 14, 20));
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
		Burst(to, n, 3.2f * k, .32f * k, .7f, 1.f, .5f, .15f, 0, 1, -1.5f, 0, .6f, .05f);
		Burst(to, n / 2, 4.5f * k, .1f, .9f, 1.f, .8f, .3f, 3, 1, 3.f, .06f, .8f);
		Burst(to, 3, .4f, 1.0f * k, .45f, 1.f, .6f, .2f, 1, 1, 0, 0, 0, 1.8f * k);
		break;
	case EL_ICE:
		Burst(to, n, 3.f * k, .22f * k, .9f, .6f, .85f, 1.f, 2, 1, 2.f, 0, .4f);
		Burst(to, n / 2, 5.f * k, .08f, .7f, .85f, .95f, 1.f, 3, 1, 4.f, .05f, .5f);
		Burst(to, 2, .2f, .9f * k, .5f, .7f, .9f, 1.f, 1, 1, 0, 0, 0, 1.6f * k);
		break;
	case EL_THUNDER:
		for (int i = 0; i < 6; i++) {
			Particle q = {};
			q.p = to + v3(m_rng.F(-.3f, .3f), 4.f, m_rng.F(-.3f, .3f));
			q.v = v3(m_rng.F(-1, 1), -40.f, m_rng.F(-1, 1));
			q.life = q.max = .18f; q.size = .12f; q.size1 = .08f;
			q.r = 1; q.g = .95f; q.b = .5f; q.a = 1; q.shape = 3; q.add = 1; q.stretch = .1f;
			m_parts.push_back(q);
		}
		Burst(to, n, 6.f * k, .1f, .45f, 1.f, 1.f, .6f, 3, 1, 0, .04f);
		Burst(to, 2, .1f, 1.2f * k, .3f, 1.f, 1.f, .7f, 0, 1, 0, 0, 0, 2.f * k);
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

// ============================================================ 拠点
void Game::BuildShowcase()
{
	if (!m_loaded) return;
	int st = Clampi(m_save.stageSel, 1, TAC_STAGE_MAX);
	if (!m_showBuilt) {
		GenerateMap(m_showMap, st <= 1 ? 1 : st, 777u + st * 31u);
		m_r.BuildTerrain(m_showMap);
		m_showBuilt = TRUE;
		m_showStage = st;
	}
	m_showN = 0;
	for (int i = 0; i < MAX_PARTY; i++) {
		int r = m_save.party[i];
		if (r < 0 || !m_save.roster[r].used) continue;
		ActorFromChar(m_showAct[m_showN], m_lib, m_r, m_save.roster[r]);
		ActorPlay(m_showAct[m_showN], CL_IDLE, 1, 0);
		m_showAct[m_showN].t = m_rng.F(0, 2);
		int px = m_showMap.pStartX[i], pz = m_showMap.pStartZ[i];
		m_showPos[m_showN] = v3(px + .5f, m_showMap.StandY(px, pz) + m_showAct[m_showN].hover, pz + .5f);
		m_showN++;
	}
}

void Game::EnterBase()
{
	m_screen = SC_BASE;
	m_focusId = ID_BASE_SORTIE;
	m_confirmDelete = 0;
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) m_portraitWant[i] = (i < MAX_ROSTER) ? i + 1 : 0;
	InvalidatePortraits();
	BuildShowcase();
	m_camDistGoal = m_camDist = 9.f;
	m_camPitch = .5f;
}

void Game::UpdateBase(float dt)
{
	m_showYaw += dt * .08f;
	for (int i = 0; i < m_showN; i++) ActorUpdate(m_showAct[i], dt);
	if (m_screen == SC_CREATE) ActorUpdate(m_preview, dt);
	m_camDist = Lerpf(m_camDist, m_camDistGoal, Clampf(dt * 5.f, 0, 1));
}

void Game::DrawShowcase3D()
{
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

void Game::DrawCodex()
{
	float x = U(30), y = U(24), w = (float)m_w - U(60), h = (float)m_h - U(48);
	Panel(x, y, w, h);
	const wchar_t* title = m_codex == 2 ? TT(TX_CDX_GROW) : (m_codex == 3 ? TT(TX_CDX_ALCH) : TT(TX_CDX_SKILL));
	m_r.HudText(title, x + w * .5f, y + U(28), U(22), RGBA8(255, 226, 150), 1, 1);
	float yy = y + U(58);
	if (m_codex == 1) {
		int list[SKL_COUNT], n = 0;
		for (int s = 0; s < SKL_COUNT; s++) if (!SkillInfo(s).enemyOnly) list[n++] = s;
		int per = 7;
		int pages = max(1, (n + per - 1) / per);
		if (m_codexPage >= pages) m_codexPage = pages - 1;
		int a = m_codexPage * per, b = min(n, a + per);
		for (int i = a; i < b; i++) {
			const SkillDef& sd = SkillInfo(list[i]);
			wchar_t how[160] = L"";
			for (int c = 0; c < C_PLAYER_COUNT && how[0] == 0; c++) {
				const ClassDef& cd = ClassInfo(c);
				for (int k = 0; k < 6; k++) if (cd.learn[k].skill == list[i]) {
					swprintf_s(how, L"%s  Lv%d", TT(cd.nameTx), cd.learn[k].lv);
				}
			}
			if (list[i] >= SKL_ART0) {
				int k = list[i] - SKL_ART0;
				int g = k / 26;
				int lv = (k % 26) + 7;
				swprintf_s(how, L"%s  Lv%d", TT(TX_G_MELEE + g), lv);
			}
			for (int line = 0; line < TAC_RANK_LINES && how[0] == 0; line++) {
				for (int r = 0; r < TAC_RANK_N && how[0] == 0; r++) {
					const ClassDef& cd = ClassInfo(C_PLAYER_COUNT + line * TAC_RANK_N + r);
					for (int k = 0; k < 6; k++) if (cd.learn[k].skill == list[i] && cd.learn[k].lv > 0) {
						swprintf_s(how, L"%s  Lv%d", TT(cd.nameTx), cd.learn[k].lv);
						break;
					}
				}
			}
			wchar_t row[220];
			swprintf_s(row, L"%s   MP%d   %s", TT(sd.nameTx), sd.mp, how);
			m_r.HudText(row, x + U(24), yy, U(16), RGBA8(240, 240, 245), 0, 1);
			m_r.HudText(TT(sd.descTx), x + U(36), yy + U(18), U(13), RGBA8(190, 200, 220), 0, 0);
			yy += U(40);
		}
		wchar_t pg[32];
		swprintf_s(pg, L"%d / %d", m_codexPage + 1, pages);
		m_r.HudText(pg, x + w * .5f, y + h - U(78), U(14), RGBA8(220, 220, 220), 1, 0);
		if (Btn(ID_CDX_PREV, x + U(24), y + h - U(56), U(140), U(36), L"◀")) m_codexPage = (m_codexPage + pages - 1) % pages;
		if (Btn(ID_CDX_NEXT, x + U(180), y + h - U(56), U(140), U(36), L"▶")) m_codexPage = (m_codexPage + 1) % pages;
	} else if (m_codex == 2) {
		static const int showLv[] = { 1, 15, 35, 55, 78, 105, 140, 180, 230, 290, 400, 999 };
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
			if (Btn(ID_CDX_REC0 + r, x + U(24), yy, w - U(48), U(36), row, ok, FALSE, U(15))) {
				m_save.itemN[rc.a] -= rc.an;
				if (rc.b >= 0) m_save.itemN[rc.b] -= rc.bn;
				if (rc.c >= 0) m_save.itemN[rc.c] -= rc.cn;
				m_save.itemN[rc.out] += rc.on;
				TacSfx(TS_CONFIRM);
				Persist();
			}
			yy += U(42);
		}
	}
	if (Btn(ID_CDX_GROW, x + U(340), y + h - U(56), U(150), U(36), TT(TX_CDX_GROW))) { m_codex = 2; }
	if (Btn(ID_CDX_ALCH, x + U(500), y + h - U(56), U(150), U(36), TT(TX_CDX_ALCH))) { m_codex = 3; }
	if (Btn(ID_CDX_SKILL, x + U(660), y + h - U(56), U(150), U(36), TT(TX_CDX_SKILL))) { m_codex = 1; m_codexPage = 0; }
	if (Btn(ID_BASE_CLOSE, x + w - U(180), y + h - U(56), U(150), U(36), TT(TX_BASE_CLOSE))) m_codex = 0;
}

void Game::DrawBaseUi()
{
	if (m_codex) { DrawCodex(); return; }
	float pw = U(330), ph = (float)m_h - U(40);
	float x = U(20), y = U(20);
	Panel(x, y, pw, ph);
	m_r.HudText(TT(TX_TITLE), x + pw * .5f, y + U(36), U(24), RGBA8(255, 226, 150), 1, 1);
	float by = y + U(74);
	float bw = pw - U(40), bh = U(44), bx = x + U(20);
	wchar_t st[64];
	swprintf_s(st, L"%d / %d", m_save.stageSel, TAC_STAGE_MAX);
	int d = Sel(ID_BASE_STAGE, bx, by, bw, U(40), TT(TX_BASE_STAGE), st);
	if (d) {
		int step = m_keyShift ? 100 : 10;
		if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 && !m_keyShift) step = 1;
		int mx = min(TAC_STAGE_MAX, m_save.stageCleared + 1);
		m_save.stageSel = Clampi(m_save.stageSel + d * step, 1, mx);
	}
	by += U(46);
	wchar_t cl[64];
	swprintf_s(cl, L"%s: %d", TT(TX_BASE_CLEARED), m_save.stageCleared);
	m_r.HudText(cl, bx + U(4), by + U(8), U(14), RGBA8(200, 200, 210), 0, 0);
	by += U(28);
	int np = PartyCount();
	if (Btn(ID_BASE_SORTIE, bx, by, bw, bh + U(8), TT(TX_BASE_SORTIE), np > 0, FALSE, U(22), RGBA8(170, 60, 50, 240))) { StartBattle(m_save.stageSel); return; }
	by += bh + U(18);
	if (Btn(ID_BASE_PARTY, bx, by, bw, bh, TT(TX_BASE_PARTY), RosterCount() > 0)) { EnterParty(); return; }
	by += bh + U(8);
	if (Btn(ID_BASE_CREATE, bx, by, bw, bh, TT(TX_BASE_CREATE), RosterCount() < MAX_ROSTER)) { EnterCreate(-1); return; }
	by += bh + U(8);
	if (Btn(ID_BASE_EVOLVE, bx, by, bw, bh, TT(TX_BASE_EVOLVE), RosterCount() > 0)) { EnterEvolve(); return; }
	by += bh + U(8);
	if (Btn(ID_BASE_CODEX, bx, by, bw, bh, TT(TX_BASE_CODEX))) { m_codex = 1; m_codexPage = 0; return; }
	by += bh + U(18);
	static const int qtx[3] = { TX_SET_Q_LOW, TX_SET_Q_STD, TX_SET_Q_HIGH };
	if (int q = Sel(ID_BASE_QUALITY, bx, by, bw, U(36), TT(TX_SET_QUALITY), TT(qtx[Clampi(m_save.quality, 0, 2)]))) {
		m_save.quality = (m_save.quality + q + 3) % 3;
		m_r.SetQuality(m_save.quality);
		InvalidatePortraits();
	}
	by += U(40);
	if (Sel(ID_BASE_ANIM, bx, by, bw, U(36), TT(TX_SET_ANIM), TT(m_save.battleAnim ? TX_SET_ANIM_SHORT : TX_SET_ANIM_FULL))) m_save.battleAnim = !m_save.battleAnim;
	by += U(40);
	if (Sel(ID_BASE_GRID, bx, by, bw, U(36), TT(TX_SET_GRID), TT(m_save.showGrid ? TX_CR_ON : TX_CR_OFF))) m_save.showGrid = !m_save.showGrid;
	by += U(48);
	if (Btn(ID_BASE_HELP, bx, by, bw * .48f, bh, TT(TX_BT_HELP))) m_showHelp = TRUE;
	if (Btn(ID_BASE_CLOSE, bx + bw * .52f, by, bw * .48f, bh, TT(TX_BASE_CLOSE))) { Persist(); m_wantClose = TRUE; }
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
	// 出撃メンバー
	float cw = U(118), chh = U(150);
	float total = cw * MAX_PARTY + U(10) * (MAX_PARTY - 1);
	float cx0 = x + pw + ((float)m_w - x - pw - total) * .5f;
	float cy = U(24);
	m_r.HudText(TT(TX_PT_DEPLOY), cx0, cy + U(10), U(16), RGBA8(255, 226, 150), 0, 1);
	cy += U(24);
	for (int i = 0; i < MAX_PARTY; i++) {
		float cx = cx0 + i * (cw + U(10));
		int r = m_save.party[i];
		m_r.HudPanel(cx, cy, cw, chh, RGBA8(16, 20, 36, 200), RGBA8(150, 128, 86));
		if (r >= 0 && m_save.roster[r].used) {
			const CharData& c = m_save.roster[r];
			DrawPortrait(r, cx + U(11), cy + U(8), cw - U(22), RGBA8(120, 160, 255));
			m_r.HudText(c.name, cx + cw * .5f, cy + U(112), U(14), RGBA8(255, 255, 255), 1, 1);
			wchar_t l[64];
			swprintf_s(l, L"%s %s %d", ClassName(c.cls), TT(TX_MISC_LV), c.lv);
			m_r.HudText(l, cx + cw * .5f, cy + U(132), U(12), RGBA8(210, 210, 200), 1, 0);
		} else {
			m_r.HudText(L"—", cx + cw * .5f, cy + chh * .5f, U(22), RGBA8(120, 120, 130), 1, 0);
		}
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
	m_focusId = ID_CR_NAME;
	RefreshPreview();
}

void Game::RefreshPreview()
{
	CharData c = m_edit;
	ActorFromChar(m_preview, m_lib, m_r, c);
	ActorPlay(m_preview, ClassInfo(c.cls).weaponLook == WL_AXE2 || ClassInfo(c.cls).weaponLook == WL_SWORD2 ? CL_2H_IDLE : CL_IDLE, 1, 0);
}

void Game::DrawCreate3D()
{
	m_r.ClearDepth();
	V3 pos = v3(0, -50.f, 0);
	Actor& a = m_preview;
	float h = a.height + a.hover;
	V3 look = pos + v3(0, h * .52f, 0);
	float dist = h * 2.9f;
	float yaw = PI;
	V3 dir = v3(sinf(yaw) * cosf(.18f), sinf(.18f), cosf(yaw) * cosf(.18f));
	V3 eye = look + dir * dist;
	V3 right = Norm(Cross(v3(0, 1, 0), dir * -1.f));
	V3 shift = right * (-dist * .14f);
	M4 view = MLookAtLH(eye + shift, look + shift, v3(0, 1, 0));
	M4 proj = MPerspLH(.55f, (float)m_w / max(1, m_h), .05f, 60.f);
	m_r.SetCamera(view, proj, eye, m_time);
	m_r.SetFog(500.f, 1000.f, 0, RGBA8(0, 0, 0));
	m_r.SetShadowEnabled(FALSE);
	m_r.CommitFrame();
	ActorPose(a, m_lib, m_r);
	DrawParams dp;
	dp.outline = .01f;
	dp.rim = .55f;
	ActorDraw(m_r, m_lib, a, ActorWorld(a, pos + v3(0, a.hover + sinf(m_time * 2.f) * .03f * (a.hover > 0), 0), PI + m_previewYaw), dp, m_time);
	m_r.DrawParticles(m_parts.data(), m_parts.size());
}

void Game::DrawCreateUi()
{
	float pw = U(420), x = U(20), y = U(20), ph = (float)m_h - U(40);
	Panel(x, y, pw, ph);
	m_r.HudText(TT(m_editIdx >= 0 ? TX_CR_EDIT : TX_CR_TITLE), x + pw * .5f, y + U(30), U(22), RGBA8(255, 226, 150), 1, 1);
	float bx = x + U(18), bw = pw - U(36), rh = U(34), gap = U(5);
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
	if (int d = Sel(ID_CR_GENDER, bx, yy, bw, rh, TT(TX_CR_GENDER), TT(m_edit.gender ? TX_CR_FEMALE : TX_CR_MALE))) { (void)d; m_edit.gender = !m_edit.gender; changed = TRUE; }
	yy += rh + gap;
	BOOL isNew = m_editIdx < 0;
	if (isNew) {
		if (Sel(ID_CR_LINEAGE, bx, yy, bw, rh, TT(TX_CR_LINEAGE), TT(m_lineage ? TX_CR_MAGIC : TX_CR_WARRIOR))) {
			m_lineage = !m_lineage;
			int lst[4];
			CreateClassList(lst);
			m_edit.cls = lst[0];
			m_edit.body = m_lineage ? B_MAGE : B_KNIGHT;
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
			if (m_edit.cls < 6) m_edit.body = bodyFor[m_edit.cls];
			changed = TRUE;
		}
		yy += rh + gap;
	}
	static const int bodyTx[B_COUNT] = { TX_BODY_KNIGHT, TX_BODY_BARBARIAN, TX_BODY_MAGE, TX_BODY_ROGUE, TX_BODY_HOODED };
	if (int d = Sel(ID_CR_BODY, bx, yy, bw, rh, TT(TX_CR_BODY), TT(bodyTx[m_edit.body]))) { m_edit.body = (m_edit.body + d + B_COUNT) % B_COUNT; changed = TRUE; }
	yy += rh + gap;
	if (int d = Sel(ID_CR_HAIR, bx, yy, bw, rh, TT(TX_CR_HAIR), TT(TX_HAIR_DEFAULT + m_edit.hair))) { m_edit.hair = (m_edit.hair + d + HAIR_STYLES) % HAIR_STYLES; changed = TRUE; }
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
		swprintf_s(v, L"%d", m_edit.outfit + 1);
		if (int d = Sel(ID_CR_OUTFIT, bx, yy, bw, rh, TT(TX_CR_OUTFIT), v)) { m_edit.outfit = (m_edit.outfit + d + OUTFIT_COLORS) % OUTFIT_COLORS; changed = TRUE; }
	}
	yy += rh + gap;
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
			for (int i = 0; i < MAX_ROSTER; i++) if (!m_save.roster[i].used) { slot = i; break; }
			if (slot >= 0) {
				CharData c = m_edit;
				c.used = 1;
				c.lv = 1; c.exp = 0;
				c.uid = m_save.uidNext++;
				CharRecalcInit(c);
				m_save.roster[slot] = c;
				for (int i = 0; i < MAX_PARTY; i++) if (m_save.party[i] < 0) { m_save.party[i] = slot; break; }
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
	if (Btn(ID_CR_BACK, bx + bw * .62f, fy, bw * .38f, U(42), TT(TX_CR_BACK))) { EnterParty(); return; }
	m_r.HudText(TT(TX_CR_PREVIEW_HINT), m_w * .62f, m_h - U(28), U(13), RGBA8(220, 220, 230), 1, 0);
}

// ============================================================ 編成
void Game::EnterParty()
{
	m_screen = SC_PARTY;
	m_confirmDelete = 0;
	m_textFocus = 0;
	if (m_partySel < 0 || !m_save.roster[m_partySel].used) {
		m_partySel = -1;
		for (int i = 0; i < MAX_ROSTER; i++) if (m_save.roster[i].used) { m_partySel = i; break; }
	}
	for (int i = 0; i < Renderer::PORTRAIT_N; i++) m_portraitWant[i] = (i < MAX_ROSTER) ? i + 1 : 0;
	m_focusId = ID_PT_CARD0 + max(0, m_partySel);
	BuildShowcase();
}

void Game::TogglePartyMember(int idx)
{
	for (int i = 0; i < MAX_PARTY; i++) if (m_save.party[i] == idx) {
		m_save.party[i] = -1;
		int k = 0;
		int tmp[MAX_PARTY];
		for (int j = 0; j < MAX_PARTY; j++) if (m_save.party[j] >= 0) tmp[k++] = m_save.party[j];
		for (int j = 0; j < MAX_PARTY; j++) m_save.party[j] = j < k ? tmp[j] : -1;
		BuildShowcase();
		return;
	}
	for (int i = 0; i < MAX_PARTY; i++) if (m_save.party[i] < 0) { m_save.party[i] = idx; BuildShowcase(); return; }
	AddBanner(TT(TX_PT_HINT), NULL, RGBA8(255, 200, 150), 2, 2.f);
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

void Game::DrawPartyUi()
{
	float x = U(20), y = U(20);
	float gw = min((float)m_w * .6f, U(760)), gh = (float)m_h - U(40);
	Panel(x, y, gw, gh);
	m_r.HudText(TT(TX_PT_TITLE), x + U(20), y + U(26), U(22), RGBA8(255, 226, 150), 0, 1);
	wchar_t cnt[64];
	swprintf_s(cnt, L"%s %d/%d   %s %d/%d", TT(TX_BASE_ROSTER), RosterCount(), MAX_ROSTER, TT(TX_PT_DEPLOY), PartyCount(), MAX_PARTY);
	m_r.HudText(cnt, x + gw - U(20), y + U(26), U(14), RGBA8(220, 220, 220), 2, 0);
	int cols = 5;
	float pad = U(14);
	float cw = (gw - pad * 2 - U(8) * (cols - 1)) / cols;
	float ch = min(cw * 1.28f, (gh - U(130)) / 4.f - U(8));
	float ps = min(cw - U(16), ch - U(44));
	for (int i = 0; i < MAX_ROSTER; i++) {
		float cx = x + pad + (i % cols) * (cw + U(8));
		float cy = y + U(52) + (i / cols) * (ch + U(8));
		const CharData& c = m_save.roster[i];
		if (!c.used) {
			if (Btn(ID_PT_CARD0 + i, cx, cy, cw, ch, L"+", RosterCount() < MAX_ROSTER, FALSE, U(28), RGBA8(30, 40, 60, 160))) { EnterCreate(-1); return; }
			continue;
		}
		BOOL inParty = FALSE;
		for (int p = 0; p < MAX_PARTY; p++) if (m_save.party[p] == i) inParty = TRUE;
		if (Btn(ID_PT_CARD0 + i, cx, cy, cw, ch, L"", TRUE, m_partySel == i, 0, inParty ? RGBA8(50, 80, 140, 240) : 0)) {
			if (m_partySel == i) TogglePartyMember(i);
			m_partySel = i;
			m_confirmDelete = 0;
		}
		DrawPortrait(i, cx + (cw - ps) * .5f, cy + U(6), ps, inParty ? RGBA8(120, 180, 255) : RGBA8(110, 100, 80));
		m_r.HudText(c.name, cx + cw * .5f, cy + U(6) + ps + U(12), U(13), m_partySel == i ? RGBA8(40, 24, 8) : RGBA8(255, 255, 255), 1, 1, m_partySel != i);
		wchar_t l[48];
		swprintf_s(l, L"%s%d", TT(TX_MISC_LV), c.lv);
		m_r.HudText(l, cx + cw * .5f, cy + U(6) + ps + U(28), U(11), m_partySel == i ? RGBA8(60, 40, 10) : RGBA8(210, 210, 200), 1, 0, m_partySel != i);
		if (inParty) m_r.HudText(L"★", cx + U(12), cy + U(14), U(14), RGBA8(255, 220, 90), 1, 1);
	}
	m_r.HudText(TT(TX_PT_HINT), x + gw * .5f, y + gh - U(64), U(13), RGBA8(210, 210, 220), 1, 0);
	if (Btn(ID_PT_BACK, x + gw - U(180), y + gh - U(46), U(160), U(36), TT(TX_CR_BACK))) { Persist(); EnterBase(); return; }
	if (Btn(ID_PT_NEW, x + U(20), y + gh - U(46), U(200), U(36), TT(TX_BASE_CREATE), RosterCount() < MAX_ROSTER)) { EnterCreate(-1); return; }
	// 詳細
	float dx = x + gw + U(14), dw = (float)m_w - dx - U(20);
	if (dw < U(220) || m_partySel < 0) return;
	const CharData& c = m_save.roster[m_partySel];
	if (!c.used) return;
	Panel(dx, y, dw, gh);
	float ps2 = min(U(110), dw * .4f);
	DrawPortrait(m_partySel, dx + U(14), y + U(14), ps2, RGBA8(200, 170, 110));
	m_r.HudText(c.name, dx + ps2 + U(26), y + U(30), U(20), RGBA8(255, 255, 255), 0, 1);
	m_r.HudText(ClassName(c.cls), dx + ps2 + U(26), y + U(56), U(15), RGBA8(255, 226, 150), 0, 1);
	wchar_t l[96];
	swprintf_s(l, L"%s %d   %s %d/100", TT(TX_MISC_LV), c.lv, TT(TX_MISC_EXP), c.exp);
	m_r.HudText(l, dx + ps2 + U(26), y + U(80), U(14), RGBA8(220, 220, 220), 0, 0);
	Bar(dx + ps2 + U(26), y + U(94), dw - ps2 - U(44), U(8), c.exp / 100.f, RGBA8(120, 200, 255), RGBA8(60, 120, 220));
	swprintf_s(l, L"%s %d  /  %s %d", TT(TX_BT_DEFEATED), c.kills, TT(TX_BT_STAGE), c.battles);
	m_r.HudText(l, dx + ps2 + U(26), y + U(114), U(12), RGBA8(190, 190, 190), 0, 0);
	float sy = y + ps2 + U(28);
	DrawStatLines(c, dx + U(14), sy, dw - U(28), NULL, -1, -1);
	sy += U(22) * 5 + U(10);
	m_r.HudText(TT(TX_PT_SKILLS), dx + U(14), sy + U(10), U(15), RGBA8(255, 226, 150), 0, 1);
	sy += U(24);
	int ns = 0;
	for (int s = 0; s < SKL_COUNT && sy < y + gh - U(110); s++) {
		if (!CharHasSkill(c, s)) continue;
		const SkillDef& sd = SkillInfo(s);
		wchar_t sl[96];
		if (sd.kind == SK_PASSIVE) swprintf_s(sl, L"%s  (%s)", TT(sd.nameTx), TT(TX_MISC_PASSIVE));
		else swprintf_s(sl, L"%s  MP%d", TT(sd.nameTx), sd.mp);
		m_r.HudText(sl, dx + U(20), sy + U(9), U(13), RGBA8(236, 236, 240), 0, 0);
		sy += U(19);
		ns++;
	}
	if (!ns) m_r.HudText(TT(TX_PT_NONE), dx + U(20), sy + U(9), U(13), RGBA8(160, 160, 170), 0, 0);
	// 操作
	float by = y + gh - U(98);
	BOOL inParty = FALSE;
	for (int p = 0; p < MAX_PARTY; p++) if (m_save.party[p] == m_partySel) inParty = TRUE;
	if (m_confirmDelete) {
		m_r.HudText(TT(TX_PT_DELETE_Q), dx + dw * .5f, by + U(10), U(15), RGBA8(255, 170, 160), 1, 1);
		if (Btn(ID_PT_YES, dx + U(14), by + U(30), dw * .5f - U(20), U(38), TT(TX_PT_YES), TRUE, FALSE, 0, RGBA8(170, 50, 40, 240))) { DeleteRoster(m_partySel); m_confirmDelete = 0; return; }
		if (Btn(ID_PT_NO, dx + dw * .5f + U(6), by + U(30), dw * .5f - U(20), U(38), TT(TX_PT_NO))) m_confirmDelete = 0;
		return;
	}
	float bw3 = (dw - U(28) - U(16)) / 3.f;
	if (Btn(ID_PT_DEPLOY, dx + U(14), by + U(30), bw3, U(38), TT(TX_PT_DEPLOY), TRUE, inParty, U(14))) TogglePartyMember(m_partySel);
	if (Btn(ID_PT_EDIT, dx + U(14) + bw3 + U(8), by + U(30), bw3, U(38), TT(TX_CR_EDIT), TRUE, FALSE, U(14))) { EnterCreate(m_partySel); return; }
	if (Btn(ID_PT_DELETE, dx + U(14) + (bw3 + U(8)) * 2, by + U(30), bw3, U(38), TT(TX_PT_DELETE), TRUE, FALSE, U(14), RGBA8(120, 40, 36, 230))) m_confirmDelete = 1;
}

// ============================================================ 進化
void Game::EnterEvolve()
{
	m_screen = SC_EVOLVE;
	m_evMain = -1; m_evTarget = -1;
	m_evPartner[0] = m_evPartner[1] = -1;
	m_evPick = 0;
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
	m_partySel = m_evMain;
	m_evPick = 0;
	m_evTarget = -1;
	m_evPartner[0] = m_evPartner[1] = -1;
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
	// 1列目: 本体
	m_r.HudText(TT(TX_EV_MAIN), c0, top + U(8), U(16), RGBA8(255, 226, 150), 0, 1);
	float ry = top + U(24);
	int nElig = 0;
	for (int i = 0; i < MAX_ROSTER && ry < y + h - U(70); i++) {
		const CharData& c = m_save.roster[i];
		if (!c.used || !EvoMainEligible(c)) continue;
		EvoOption opt[4];
		if (EvoOptions(c.cls, opt) == 0) continue;
		nElig++;
		wchar_t l[96];
		swprintf_s(l, L"%s  %s %s%d", c.name, ClassName(c.cls), TT(TX_MISC_LV), c.lv);
		if (Btn(ID_EV_MAIN0 + i, c0 + rowH, ry, colW - rowH, rowH - U(6), l, TRUE, m_evMain == i, U(14))) {
			m_evMain = i; m_evTarget = -1; m_evPartner[0] = m_evPartner[1] = -1; m_evPick = 1;
		}
		DrawPortrait(i, c0, ry, rowH - U(6), RGBA8(150, 130, 90));
		ry += rowH;
	}
	if (!nElig) m_r.HudTextBox(TT(TX_EV_NONE_ELIGIBLE), c0, top + U(30), colW, U(80), U(15), RGBA8(200, 200, 200));
	// 2列目: 進化先
	if (m_evMain >= 0) {
		const CharData& mc = m_save.roster[m_evMain];
		m_r.HudText(TT(TX_EV_TARGET), c1, top + U(8), U(16), RGBA8(255, 226, 150), 0, 1);
		EvoOption opt[4];
		int no = EvoOptions(mc.cls, opt);
		float ty = top + U(24);
		for (int k = 0; k < no; k++) {
			static const int gtx[G_COUNT] = { TX_G_MELEE, TX_G_SWIFT, TX_G_MAGIC, TX_G_HOLY };
			wchar_t l[128];
			swprintf_s(l, L"%s", ClassName(opt[k].target));
			if (Btn(ID_EV_TGT0 + k, c1, ty, colW, rowH - U(6), l, TRUE, m_evTarget == opt[k].target, U(16))) {
				m_evTarget = opt[k].target; m_evPartner[0] = m_evPartner[1] = -1; m_evPick = 2;
			}
			wchar_t g[96];
			swprintf_s(g, L"%s: %s", TT(TX_EV_NEED_GROUP), opt[k].needGroup < 0 ? TT(TX_EV_ANY) : TT(gtx[opt[k].needGroup]));
			m_r.HudText(g, c1 + U(8), ty + rowH + U(2), U(12), RGBA8(200, 200, 210), 0, 0);
			ty += rowH + U(22);
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
	}
	// 3列目: 素材
	if (m_evMain >= 0 && m_evTarget >= 0) {
		const CharData& mc = m_save.roster[m_evMain];
		int need = EvoPartnersNeeded(mc);
		int needGroup = -1;
		EvoOption opt[4];
		int no = EvoOptions(mc.cls, opt);
		for (int k = 0; k < no; k++) if (opt[k].target == m_evTarget) needGroup = opt[k].needGroup;
		wchar_t hdr[96];
		swprintf_s(hdr, L"%s (%d)", TT(TX_EV_PARTNER), need);
		m_r.HudText(hdr, c2, top + U(8), U(16), RGBA8(255, 226, 150), 0, 1);
		m_r.HudTextBox(TT(TX_EV_HINT_PARTNER), c2, top + U(22), colW, U(36), U(12), RGBA8(255, 190, 170));
		float py = top + U(62);
		for (int i = 0; i < MAX_ROSTER && py < y + h - U(80); i++) {
			const CharData& c = m_save.roster[i];
			if (!c.used || i == m_evMain || !EvoPartnerEligible(mc, c)) continue;
			BOOL picked = m_evPartner[0] == i || m_evPartner[1] == i;
			wchar_t l[96];
			swprintf_s(l, L"%s  %s %s%d", c.name, ClassName(c.cls), TT(TX_MISC_LV), c.lv);
			if (Btn(ID_EV_PART0 + i, c2 + rowH, py, colW - rowH, rowH - U(6), l, TRUE, picked, U(14))) {
				if (picked) { if (m_evPartner[0] == i) m_evPartner[0] = -1; if (m_evPartner[1] == i) m_evPartner[1] = -1; }
				else {
					if (m_evPartner[0] < 0) m_evPartner[0] = i;
					else if (need > 1 && m_evPartner[1] < 0) m_evPartner[1] = i;
					else m_evPartner[need - 1] = i;
				}
			}
			DrawPortrait(i, c2, py, rowH - U(6), RGBA8(150, 130, 90));
			py += rowH;
		}
		CharData ps[2];
		int n = 0;
		for (int k = 0; k < need; k++) if (m_evPartner[k] >= 0) ps[n++] = m_save.roster[m_evPartner[k]];
		BOOL ok = n >= need && EvoGroupSatisfied(needGroup, ps, n);
		if (n >= need && !ok) m_r.HudText(TT(TX_EV_NEED_GROUP), c2 + colW * .5f, y + h - U(72), U(13), RGBA8(255, 150, 140), 1, 1);
		if (Btn(ID_EV_DO, c2, y + h - U(56), colW * .6f, U(40), TT(TX_EV_DO), ok, FALSE, U(18), RGBA8(150, 90, 200, 240))) { DoEvolve(); return; }
	}
	if (Btn(ID_EV_BACK, x + w - U(160) - U(20), y + h - U(56), U(160), U(40), TT(TX_CR_BACK))) { EnterBase(); return; }
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
		DrawPortrait(r, x + U(30), ry, rowH - U(8), RGBA8(120, 160, 255));
		m_r.HudText(c.name, x + U(30) + rowH + U(4), ry + rowH * .28f, U(17), RGBA8(255, 255, 255), 0, 1);
		m_r.HudText(ClassName(c.cls), x + U(30) + rowH + U(4), ry + rowH * .62f, U(13), RGBA8(255, 226, 150), 0, 0);
		wchar_t l[96];
		if (c.lv > m_resultLv0[i]) swprintf_s(l, L"%s %d → %d", TT(TX_MISC_LV), m_resultLv0[i], c.lv);
		else swprintf_s(l, L"%s %d", TT(TX_MISC_LV), c.lv);
		m_r.HudText(l, x + w * .5f, ry + rowH * .3f, U(16), c.lv > m_resultLv0[i] ? RGBA8(140, 255, 150) : RGBA8(230, 230, 230), 1, 1);
		swprintf_s(l, L"%s +%d", TT(TX_BT_EXP), m_resultExp[i]);
		m_r.HudText(l, x + w * .5f, ry + rowH * .64f, U(13), RGBA8(200, 220, 255), 1, 0);
		Bar(x + w * .66f, ry + rowH * .4f, w * .28f, U(10), c.exp / 100.f, RGBA8(120, 200, 255), RGBA8(60, 120, 220));
		ry += rowH;
	}
	float by = y + h - U(60);
	float bw = U(200);
	if (m_victory && m_battleStage < 100) {
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
