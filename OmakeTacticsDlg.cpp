// おまけタクティクスゲーム: ダイアログ（ホスト）
#include "stdafx.h"
#include "ogg.h"
#include "OmakeTacticsDlg.h"
#include "OmakeTactics.h"
#include "OmakeTacticsAudio.h"
#include "oggDlg.h"
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

using namespace otac;

extern COggDlg* og;

static COmakeTacticsDlg* g_tac = nullptr;
static const UINT_PTR TAC_TIMER = 1;

// ---------------------------------------------------------------------------
// パッド（DirectInput / UI スレッドのみ）
// ---------------------------------------------------------------------------
static IDirectInput8W* g_tacDi = NULL;
static IDirectInputDevice8W* g_tacPad = NULL;
static DWORD g_tacRetryAt = 0;

static void TacReleaseDevice(IDirectInputDevice8W* p)
{
	if (!p) return;
	__try { p->Unacquire(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	__try { p->Release(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void TacReleaseDi(IDirectInput8W* p)
{
	if (!p) return;
	__try { p->Release(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void TacDropPad()
{
	IDirectInputDevice8W* p = g_tacPad;
	g_tacPad = NULL;
	TacReleaseDevice(p);
}
static void TacReleasePadAll()
{
	TacDropPad();
	IDirectInput8W* d = g_tacDi;
	g_tacDi = NULL;
	TacReleaseDi(d);
	g_tacRetryAt = 0;
}
static int TacPadErr(HRESULT hr)
{
	if (SUCCEEDED(hr)) return 0;
	if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED || hr == DIERR_OTHERAPPHASPRIO) return 1;
	return 2;
}
static BOOL CALLBACK TacEnumPads(const DIDEVICEINSTANCEW* inst, void*)
{
	if (!inst || !g_tacDi || g_tacPad) return DIENUM_STOP;
	if (FAILED(g_tacDi->CreateDevice(inst->guidInstance, &g_tacPad, NULL)) || !g_tacPad)
		return DIENUM_CONTINUE;
	return DIENUM_STOP;
}
static void TacTryOpenPad(HWND hw)
{
	if (g_tacPad) return;
	const DWORD now = GetTickCount();
	if (g_tacRetryAt != 0 && (LONG)(now - g_tacRetryAt) < 0) return;
	g_tacRetryAt = now + 1500;
	if (!g_tacDi) {
		if (FAILED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&g_tacDi, NULL)) || !g_tacDi)
			return;
	}
	if (FAILED(g_tacDi->EnumDevices(DI8DEVCLASS_GAMECTRL, TacEnumPads, NULL, DIEDFL_ATTACHEDONLY))) return;
	if (!g_tacPad) return;
	if (FAILED(g_tacPad->SetDataFormat(&c_dfDIJoystick2))) { TacDropPad(); return; }
	if (!hw || FAILED(g_tacPad->SetCooperativeLevel(hw, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) { TacDropPad(); return; }
	DIPROPRANGE range = {};
	range.diph.dwSize = sizeof(range);
	range.diph.dwHeaderSize = sizeof(range.diph);
	range.diph.dwHow = DIPH_BYOFFSET;
	range.lMin = -1000; range.lMax = 1000;
	range.diph.dwObj = DIJOFS_X; g_tacPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_Y; g_tacPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_RX; g_tacPad->SetProperty(DIPROP_RANGE, &range.diph);
	range.diph.dwObj = DIJOFS_RY; g_tacPad->SetProperty(DIPROP_RANGE, &range.diph);
	DIPROPDWORD dead = {};
	dead.diph.dwSize = sizeof(dead);
	dead.diph.dwHeaderSize = sizeof(dead.diph);
	dead.diph.dwHow = DIPH_DEVICE;
	dead.dwData = 1200;
	g_tacPad->SetProperty(DIPROP_DEADZONE, &dead.diph);
	if (TacPadErr(g_tacPad->Acquire()) == 2) TacDropPad();
}
static float TacAxis(LONG v)
{
	float f = (float)v / 1000.f;
	if (fabsf(f) < .18f) return 0.f;
	return f < -1.f ? -1.f : (f > 1.f ? 1.f : f);
}
static float TacTrig(LONG v)
{
	float f = (float)v / 1000.f;
	return f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
}
static void TacReadPad(HWND hw, PadState& out)
{
	memset(&out, 0, sizeof(out));
	out.hat = -1;
	if (!g_tacPad) TacTryOpenPad(hw);
	if (!g_tacPad) return;
	HRESULT hr = g_tacPad->Poll();
	if (TacPadErr(hr) == 1) {
		hr = g_tacPad->Acquire();
		if (TacPadErr(hr) != 2) hr = g_tacPad->Poll();
	}
	if (TacPadErr(hr) == 2) { TacDropPad(); return; }
	DIJOYSTATE2 st = {};
	hr = g_tacPad->GetDeviceState(sizeof(st), &st);
	if (TacPadErr(hr) == 1) {
		if (TacPadErr(g_tacPad->Acquire()) == 2) { TacDropPad(); return; }
		hr = g_tacPad->GetDeviceState(sizeof(st), &st);
	}
	if (FAILED(hr)) { if (TacPadErr(hr) != 0) TacDropPad(); return; }
	out.connected = 1;
	out.lx = TacAxis(st.lX);
	out.ly = TacAxis(st.lY);
	out.rx = TacAxis(st.lRx);
	out.ry = TacAxis(st.lRy);
	out.lt = TacTrig(st.lZ);
	float rt = TacTrig(st.lRz);
	if (rt < .01f && st.rglSlider[0] > 0) rt = TacTrig(st.rglSlider[0]);
	out.rt = rt;
	for (int i = 0; i < 8; i++) if (st.rgbButtons[i] & 0x80) out.buttons |= (1 << i);
	const DWORD pov = st.rgdwPOV[0];
	if (pov != 0xFFFFFFFF && LOWORD(pov) != 0xFFFF)
		out.hat = (int)((pov + 2250) / 4500) % 8;
}

// ---------------------------------------------------------------------------
// CTacView
// ---------------------------------------------------------------------------
BEGIN_MESSAGE_MAP(CTacView, CWnd)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_RBUTTONDOWN()
	ON_WM_RBUTTONUP()
	ON_WM_MBUTTONDOWN()
	ON_WM_MBUTTONUP()
	ON_WM_MOUSEWHEEL()
	ON_WM_CAPTURECHANGED()
	ON_WM_ERASEBKGND()
	ON_WM_PAINT()
	ON_WM_SETCURSOR()
	ON_WM_GETDLGCODE()
END_MESSAGE_MAP()

BOOL CTacView::CreateView(CWnd* parent, const CRect& rc)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	return CreateEx(0, cls, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP,
		rc, parent, 0x7A01);
}

void CTacView::Down(int btn, CPoint pt)
{
	SetFocus();
	if (!m_btnMask) SetCapture();
	m_btnMask |= 1 << btn;
	if (m_owner && m_owner->GameReady()) m_owner->GameRef().OnMouseDown(btn, pt.x, pt.y);
}
void CTacView::Up(int btn, CPoint pt)
{
	const int had = m_btnMask & (1 << btn);
	m_btnMask &= ~(1 << btn);
	if (!m_btnMask && GetCapture() == this) ReleaseCapture();
	if (had && m_owner && m_owner->GameReady()) m_owner->GameRef().OnMouseUp(btn, pt.x, pt.y);
}
void CTacView::OnMouseMove(UINT, CPoint pt)
{
	if (GetFocus() != this) {
		CWnd* par = GetParent();
		HWND fg = ::GetForegroundWindow();
		if (fg == m_hWnd || (par && fg == par->GetSafeHwnd())) SetFocus();
	}
	if (m_owner && m_owner->GameReady()) m_owner->GameRef().OnMouseMove(pt.x, pt.y);
}
void CTacView::OnLButtonDown(UINT, CPoint pt) { Down(0, pt); }
void CTacView::OnLButtonUp(UINT, CPoint pt) { Up(0, pt); }
void CTacView::OnLButtonDblClk(UINT, CPoint pt) { Down(0, pt); }
void CTacView::OnRButtonDown(UINT, CPoint pt) { Down(1, pt); }
void CTacView::OnRButtonUp(UINT, CPoint pt) { Up(1, pt); }
void CTacView::OnMButtonDown(UINT, CPoint pt) { Down(2, pt); }
void CTacView::OnMButtonUp(UINT, CPoint pt) { Up(2, pt); }
BOOL CTacView::OnMouseWheel(UINT, short zDelta, CPoint)
{
	if (m_owner && m_owner->GameReady()) m_owner->GameRef().OnWheel(zDelta);
	return TRUE;
}
void CTacView::OnCaptureChanged(CWnd* pWnd)
{
	if (pWnd != this && m_btnMask && m_owner && m_owner->GameReady()) {
		CPoint pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		for (int b = 0; b < 3; b++)
			if (m_btnMask & (1 << b)) m_owner->GameRef().OnMouseUp(b, pt.x, pt.y);
	}
	m_btnMask = 0;
	CWnd::OnCaptureChanged(pWnd);
}
BOOL CTacView::OnEraseBkgnd(CDC*) { return TRUE; }
void CTacView::OnPaint()
{
	CPaintDC dc(this);
	if (m_owner && m_owner->GameReady()) return;
	CRect rc;
	GetClientRect(&rc);
	dc.FillSolidRect(&rc, RGB(16, 18, 28));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(230, 230, 240));
	dc.DrawText(TT(TX_MISC_DXFAIL), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
BOOL CTacView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT) { ::SetCursor(::LoadCursor(NULL, IDC_ARROW)); return TRUE; }
	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}
UINT CTacView::OnGetDlgCode() { return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTTAB; }

// ---------------------------------------------------------------------------
// COmakeTacticsDlg
// ---------------------------------------------------------------------------
BEGIN_MESSAGE_MAP(COmakeTacticsDlg, CCustomBlurDialogBase)
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_SHOWWINDOW()
	ON_WM_DESTROY()
	ON_WM_CLOSE()
	ON_WM_GETMINMAXINFO()
	ON_WM_ACTIVATE()
	ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

COmakeTacticsDlg::COmakeTacticsDlg(CWnd* pParent)
	: CCustomBlurDialogBase(IDD, pParent)
{
	m_view.m_owner = this;
}

COmakeTacticsDlg::~COmakeTacticsDlg()
{
	if (g_tac == this) g_tac = nullptr;
}

static const wchar_t* TacCaption()
{
	return LL14(L"おまけタクティクスゲーム", L"Bonus Tactics Game", L"Jeu de tactique bonus",
		L"Gioco tattico bonus", L"Juego de tácticas extra", L"보너스 택틱스 게임", L"附赠战棋游戏",
		L"لعبة التكتيكات الإضافية", L"Бонусная тактическая игра", L"Bonus-Taktikspiel",
		L"Jogo de táticas bônus", L"Bonus-tactiekspel", L"Dodatkowa gra taktyczna", L"Bonus taktik oyunu");
}

BOOL COmakeTacticsDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(TacCaption());

	SaveData sv;
	SaveDefaults(sv);
	SaveLoad(sv);
	if (sv.winSizeVer >= 3 && sv.winW >= 640 && sv.winH >= 420) {
		CRect r(sv.winX, sv.winY, sv.winX + sv.winW, sv.winY + sv.winH);
		CRect vs(::GetSystemMetrics(SM_XVIRTUALSCREEN), ::GetSystemMetrics(SM_YVIRTUALSCREEN),
			::GetSystemMetrics(SM_XVIRTUALSCREEN) + ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
			::GetSystemMetrics(SM_YVIRTUALSCREEN) + ::GetSystemMetrics(SM_CYVIRTUALSCREEN));
		CRect inter;
		if (inter.IntersectRect(&r, &vs) && inter.Width() > 120 && inter.Height() > 80)
			SetWindowPos(NULL, r.left, r.top, r.Width(), r.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
	} else {
		// Per-Monitor V2 なので rcWork は物理ピクセル。4K / 2K / 1.5K とも拡大率に関わらず作業領域の7割。
		HMONITOR mon = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		if (::GetMonitorInfo(mon, &mi)) {
			const CRect wa(mi.rcWork);
			int w = wa.Width() * 7 / 10;
			int h = wa.Height() * 7 / 10;
			if (w < 800) w = min(800, wa.Width());
			if (h < 560) h = min(560, wa.Height());
			if (w > wa.Width()) w = wa.Width();
			if (h > wa.Height()) h = wa.Height();
			SetWindowPos(NULL, wa.left + (wa.Width() - w) / 2, wa.top + (wa.Height() - h) / 2, w, h,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	CRect rc;
	GetClientRect(&rc);
	rc.top += CCC_GetCustomCaptionHeight(m_hWnd);
	if (rc.bottom < rc.top + 8) rc.bottom = rc.top + 8;
	m_view.CreateView(this, rc);
	m_viewW = rc.Width();
	m_viewH = rc.Height();
	m_gameOk = m_game.Init(m_view.GetSafeHwnd(), m_viewW, m_viewH);
	TacAudioStart(m_hWnd);

	QueryPerformanceFrequency(&m_qpf);
	QueryPerformanceCounter(&m_last);
	SetTimer(TAC_TIMER, 8, NULL);
	CCC_BringDialogToForeground(this);
	ShowWindow(SW_SHOW);
	SetForegroundWindow();
	m_view.SetFocus();
	return FALSE;
}

void COmakeTacticsDlg::LayoutView()
{
	if (!m_view.GetSafeHwnd()) return;
	CRect rc;
	GetClientRect(&rc);
	rc.top += CCC_GetCustomCaptionHeight(m_hWnd);
	if (rc.bottom < rc.top + 8) rc.bottom = rc.top + 8;
	m_view.SetWindowPos(NULL, rc.left, rc.top, rc.Width(), rc.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
	if (rc.Width() != m_viewW || rc.Height() != m_viewH) {
		m_viewW = rc.Width();
		m_viewH = rc.Height();
		if (m_gameOk) m_game.Resize(m_viewW, m_viewH);
	}
}

void COmakeTacticsDlg::StoreWindowRect()
{
	if (!GetSafeHwnd()) return;
	WINDOWPLACEMENT wp = { sizeof(wp) };
	if (!GetWindowPlacement(&wp)) return;
	const CRect r(wp.rcNormalPosition);
	if (r.Width() < 200 || r.Height() < 150) return;
	SaveData& s = m_game.Save();
	s.winX = r.left; s.winY = r.top; s.winW = r.Width(); s.winH = r.Height();
	s.winSizeVer = 3;
}

void COmakeTacticsDlg::PollPad(float dt)
{
	PadState p;
	TacReadPad(m_hWnd, p);
	const BOOL fg = (::GetForegroundWindow() == m_hWnd);
	if (!fg) { memset(&p, 0, sizeof(p)); p.hat = -1; }
	m_game.OnPad(p, dt);
}

void COmakeTacticsDlg::OnTimer(UINT_PTR id)
{
	if (id != TAC_TIMER) { CCustomBlurDialogBase::OnTimer(id); return; }
	if (m_destroying || !m_gameOk) return;
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	float dt = m_qpf.QuadPart ? (float)((double)(now.QuadPart - m_last.QuadPart) / (double)m_qpf.QuadPart) : .016f;
	m_last = now;
	if (dt < 0) dt = 0;
	if (!TacAudioOnThread()) TacAudioPump();
	if (IsIconic()) return;
	PollPad(dt);
	m_game.Tick(dt);
	if (m_game.WantsClose()) {
		m_game.ClearClose();
		PostMessage(WM_CLOSE);
	}
}

void COmakeTacticsDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		LayoutView();
		StoreWindowRect();
	}
}

void COmakeTacticsDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogBase::OnShowWindow(bShow, nStatus);
	if (bShow) {
		LayoutView();
		if (m_gameOk) m_game.RefreshSurface();
		SetWindowText(TacCaption());
		CCC_InvalidateCaptionBar(m_hWnd);
		QueryPerformanceCounter(&m_last);
		SetTimer(TAC_TIMER, 8, NULL);
	}
}

void COmakeTacticsDlg::OnGetMinMaxInfo(MINMAXINFO* mmi)
{
	mmi->ptMinTrackSize.x = 800;
	mmi->ptMinTrackSize.y = 560;
	CCustomBlurDialogBase::OnGetMinMaxInfo(mmi);
}

void COmakeTacticsDlg::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CCustomBlurDialogBase::OnActivate(nState, pWndOther, bMinimized);
	if (nState != WA_INACTIVE && !bMinimized && m_gameOk && !m_swapShown) {
		m_swapShown = TRUE;
		LayoutView();
		m_game.RefreshSurface();
	}
	if (nState != WA_INACTIVE && m_view.GetSafeHwnd()) m_view.SetFocus();
	if (nState == WA_INACTIVE && m_gameOk) {
		static const int keys[] = { VK_SHIFT, VK_CONTROL, VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, 'W', 'A', 'S', 'D' };
		for (int k : keys) m_game.OnKey(k, FALSE);
	}
}

BOOL COmakeTacticsDlg::HandleKeyMessage(MSG* pMsg)
{
	if (!pMsg || !GetSafeHwnd()) return FALSE;
	if (pMsg->hwnd != m_hWnd && !::IsChild(m_hWnd, pMsg->hwnd)) return FALSE;
	const UINT msg = pMsg->message;
	if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
		if (pMsg->wParam == VK_F4 || pMsg->wParam == VK_SPACE || pMsg->wParam == VK_MENU) return FALSE;
		return TRUE;
	}
	if (msg == WM_SYSCHAR) return FALSE;
	if (msg == WM_KEYDOWN || msg == WM_KEYUP) {
		if (m_gameOk) {
			if (msg == WM_KEYDOWN && (pMsg->lParam & (1 << 30)) && !m_game.WantsTextInput()) {
				// 押しっぱなしのリピートはカーソル移動キーのみ通す
				const WPARAM k = pMsg->wParam;
				const BOOL rep = k == VK_LEFT || k == VK_RIGHT || k == VK_UP || k == VK_DOWN
					|| k == 'W' || k == 'A' || k == 'S' || k == 'D' || k == VK_PRIOR || k == VK_NEXT;
				if (rep) m_game.OnKey((int)k, TRUE);
			} else {
				m_game.OnKey((int)pMsg->wParam, msg == WM_KEYDOWN);
			}
		}
		if (msg == WM_KEYDOWN) ::TranslateMessage(pMsg);
		return TRUE;
	}
	if (msg == WM_CHAR) {
		if (m_gameOk && m_game.WantsTextInput()) m_game.OnChar((wchar_t)pMsg->wParam);
		return TRUE;
	}
	return FALSE;
}

// フリップのスワップチェインを子HWNDに張ると、ホイールがビューに届かず親へ送られる。
// ダイアログが握ってゲームへ渡す。カーソルがこの窓の上なら、フォーカスが親でも渡す。
static BOOL TacTakeWheel(COmakeTacticsDlg* dlg, MSG* pMsg)
{
	if (!dlg || !pMsg || pMsg->message != WM_MOUSEWHEEL || !dlg->GameReady()) return FALSE;
	HWND tac = dlg->GetSafeHwnd();
	if (!tac) return FALSE;
	BOOL over = (pMsg->hwnd == tac || ::IsChild(tac, pMsg->hwnd));
	if (!over) {
		POINT pt;
		pt.x = (short)LOWORD(pMsg->lParam);
		pt.y = (short)HIWORD(pMsg->lParam);
		HWND under = ::WindowFromPoint(pt);
		over = under && (under == tac || ::IsChild(tac, under));
	}
	if (!over) return FALSE;
	dlg->GameRef().OnWheel((short)GET_WHEEL_DELTA_WPARAM(pMsg->wParam));
	return TRUE;
}

BOOL COmakeTacticsDlg::OnMouseWheel(UINT, short zDelta, CPoint)
{
	if (m_gameOk) m_game.OnWheel(zDelta);
	return TRUE;
}

BOOL COmakeTacticsDlg::PreTranslateMessage(MSG* pMsg)
{
	if (TacTakeWheel(this, pMsg)) return TRUE;
	if (HandleKeyMessage(pMsg)) return TRUE;
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void COmakeTacticsDlg::OnDestroy()
{
	m_destroying = TRUE;
	KillTimer(TAC_TIMER);
	TacAudioStop();
	StoreWindowRect();
	m_game.Persist();
	m_game.Shutdown();
	m_gameOk = FALSE;
	TacReleasePadAll();
	CCustomBlurDialogBase::OnDestroy();
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SetTimer(4923, 20, NULL);
}

void COmakeTacticsDlg::RequestDestroyWindow()
{
	if (!GetSafeHwnd()) return;
	ShowWindow(SW_HIDE);
	DestroyWindow();
}

void COmakeTacticsDlg::OnClose() { RequestDestroyWindow(); }

void COmakeTacticsDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_tac == this) g_tac = nullptr;
	delete this;
}

// ---------------------------------------------------------------------------
// 公開 API
// ---------------------------------------------------------------------------
void OpenOmakeTacticsModeless(CWnd* p)
{
	if (g_tac && g_tac->GetSafeHwnd()) {
		if (g_tac->IsIconic()) g_tac->ShowWindow(SW_RESTORE);
		g_tac->SetForegroundWindow();
		return;
	}
	extern void CloseSoft3DRaceIfOpen();
	extern void CloseSoft3DMazeIfOpen();
	CloseSoft3DRaceIfOpen();
	CloseSoft3DMazeIfOpen();
	g_tac = new COmakeTacticsDlg(p);
	if (!g_tac->Create(IDD_OMAKE_TACTICS, p)) {
		delete g_tac;
		g_tac = nullptr;
		return;
	}
	g_tac->ShowWindow(SW_SHOW);
	g_tac->SetForegroundWindow();
	g_tac->BringWindowToTop();
	// 操作中は矢印グローバルホットキー（音量／シーク）を止める
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		::UnregisterHotKey(og->GetSafeHwnd(), 8000);
		::UnregisterHotKey(og->GetSafeHwnd(), 8001);
		::UnregisterHotKey(og->GetSafeHwnd(), 8002);
		::UnregisterHotKey(og->GetSafeHwnd(), 8003);
	}
}

void CloseOmakeTacticsIfOpen()
{
	if (!g_tac) return;
	HWND hw = g_tac->GetSafeHwnd();
	if (!hw || !::IsWindow(hw)) return;
	g_tac->RequestDestroyWindow();
}

BOOL IsOmakeTacticsOpen()
{
	return (g_tac && g_tac->GetSafeHwnd() && ::IsWindow(g_tac->GetSafeHwnd()) && g_tac->IsWindowVisible()) ? TRUE : FALSE;
}

BOOL IsOmakeTacticsActive()
{
	return IsOmakeTacticsOpen();
}

BOOL OmakeTacticsPreTranslate(MSG* pMsg)
{
	if (!IsOmakeTacticsOpen() || !pMsg) return FALSE;
	if (TacTakeWheel(g_tac, pMsg)) return TRUE;
	return g_tac->HandleKeyMessage(pMsg);
}
