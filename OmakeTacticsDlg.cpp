// おまけタクティクスゲーム: ダイアログ（ホスト）
#include "stdafx.h"
#include "ogg.h"
#include "OmakeTacticsDlg.h"
#include "OmakeTactics.h"
#include "OmakeTacticsAudio.h"
#include "oggDlg.h"
#include "UpdateCheck.h"
#include <imm.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "imm32.lib")

using namespace otac;

extern COggDlg* og;

static COmakeTacticsDlg* g_tac = nullptr;
#ifndef WM_TAC_PACE
#define WM_TAC_PACE (WM_APP + 549)
#endif
#ifndef WM_TAC_DESTROY
#define WM_TAC_DESTROY (WM_APP + 550)
#endif
enum { IDC_TAC_IME = 0x7A10 };

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
// 未接続時の列挙は 1 回で 100ms 超かかることがあり、UI スレッドで 1.5 秒ごとに回すと周期的に止まる。
// 有無の確認だけを別スレッドの別インスタンスで行い、見つかったときだけ UI スレッドで開く。
static volatile LONG g_padProbe = 0;
static volatile LONG g_padSeen = 0;
static BOOL CALLBACK TacSeePads(const DIDEVICEINSTANCEW*, void* p)
{
	if (p) *(LONG*)p = 1;
	return DIENUM_STOP;
}
static DWORD WINAPI TacPadProbe(LPVOID)
{
	IDirectInput8W* di = NULL;
	LONG seen = 0;
	if (SUCCEEDED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di, NULL)) && di) {
		di->EnumDevices(DI8DEVCLASS_GAMECTRL, TacSeePads, &seen, DIEDFL_ATTACHEDONLY);
		TacReleaseDi(di);
	}
	if (seen) InterlockedExchange(&g_padSeen, 1);
	InterlockedExchange(&g_padProbe, 0);
	return 0;
}
static void TacTryOpenPad(HWND hw)
{
	if (g_tacPad) return;
	if (InterlockedCompareExchange(&g_padProbe, 0, 0) != 0) return;
	const DWORD now = GetTickCount();
	if (InterlockedExchange(&g_padSeen, 0) == 0) {
		if (g_tacRetryAt != 0 && (LONG)(now - g_tacRetryAt) < 0) return;
		g_tacRetryAt = now + 1500;
		InterlockedExchange(&g_padProbe, 1);
		HANDLE th = CreateThread(NULL, 64 * 1024, TacPadProbe, NULL, 0, NULL);
		if (th) CloseHandle(th);
		else InterlockedExchange(&g_padProbe, 0);
		return;
	}
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
	ON_WM_MOUSEACTIVATE()
END_MESSAGE_MAP()

BOOL CTacView::CreateView(CWnd* parent, const CRect& rc)
{
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
	return CreateEx(0, cls, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		rc, parent, 0x7A01);
}

void CTacView::Down(int btn, CPoint pt)
{
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
void CTacView::ReleaseButtons()
{
	m_btnMask = 0;
	if (GetCapture() == this) ReleaseCapture();
}
void CTacView::OnMouseMove(UINT, CPoint pt)
{
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
int CTacView::OnMouseActivate(CWnd*, UINT, UINT) { return MA_NOACTIVATE; }

// ---------------------------------------------------------------------------
// COmakeTacticsDlg
// ---------------------------------------------------------------------------
BEGIN_MESSAGE_MAP(COmakeTacticsDlg, CCustomBlurDialogBase)
	ON_WM_TIMER()
	ON_MESSAGE(WM_TAC_PACE, OnTacPace)
	ON_MESSAGE(WM_TAC_DESTROY, OnTacDestroy)
	ON_WM_SIZE()
	ON_WM_SHOWWINDOW()
	ON_WM_DESTROY()
	ON_WM_CLOSE()
	ON_WM_SYSCOMMAND()
	ON_COMMAND(IDC_CAP_CLOSE, OnTacCapClose)
	ON_WM_GETMINMAXINFO()
	ON_WM_ACTIVATE()
	ON_WM_ACTIVATEAPP()
	ON_WM_MOUSEWHEEL()
	ON_WM_GETDLGCODE()
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
	m_ime.Create(WS_CHILD | ES_AUTOHSCROLL | ES_WANTRETURN, CRect(0, 0, 8, 8), this, IDC_TAC_IME);
	if (m_ime.GetSafeHwnd()) {
		m_imeFont.CreateFont(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
		if (m_imeFont.GetSafeHandle()) m_ime.SetFont(&m_imeFont);
		m_ime.ShowWindow(SW_HIDE);
	}
	if (m_hWnd) ::ImmAssociateContext(m_hWnd, NULL);
	if (m_view.GetSafeHwnd()) ::ImmAssociateContext(m_view.GetSafeHwnd(), NULL);
	m_gameOk = m_game.Init(m_view.GetSafeHwnd(), m_viewW, m_viewH);
	TacAudioStart(m_hWnd);

	QueryPerformanceFrequency(&m_qpf);
	QueryPerformanceCounter(&m_tickAt);
	StartPace();
	KickPace();
	CCC_BringDialogToForeground(this);
	ShowWindow(SW_SHOW);
	SetForegroundWindow();
	SetFocus();
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

static void TacPulseFront(HWND hwnd)
{
	if (!hwnd) return;
	::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void COmakeTacticsDlg::ApplyWinMode()
{
	if (!m_gameOk || !GetSafeHwnd()) return;
	int mode = otac::TacWinMode(m_game.Save());
	if (m_winApplied == mode) return;
	int prev = m_winApplied;
	m_winApplied = mode;
	if (prev < 0 && mode == 0) return;
	if (mode) {
		HMONITOR mon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		if (GetMonitorInfo(mon, &mi)) {
			SetWindowPos(NULL, mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
				SWP_NOZORDER | SWP_SHOWWINDOW);
			TacPulseFront(m_hWnd);
		}
	} else {
		otac::SaveData& s = m_game.Save();
		if (s.winW >= 200 && s.winH >= 150)
			SetWindowPos(NULL, s.winX, s.winY, s.winW, s.winH, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void COmakeTacticsDlg::StoreWindowRect()
{
	if (!GetSafeHwnd()) return;
	if (m_gameOk && otac::TacWinMode(m_game.Save())) return;
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
	CCustomBlurDialogBase::OnTimer(id);
}

void COmakeTacticsDlg::KickPace()
{
	if (m_destroying || !m_hWnd) return;
	if (InterlockedCompareExchange(&m_pacePosted, 1, 0) != 0) return;
	if (!::PostMessage(m_hWnd, WM_TAC_PACE, 0, 0))
		InterlockedExchange(&m_pacePosted, 0);
}

DWORD WINAPI COmakeTacticsDlg::PaceThread(LPVOID param)
{
	COmakeTacticsDlg* d = (COmakeTacticsDlg*)param;
	if (!d) return 0;
	HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (!timer) timer = CreateWaitableTimerW(NULL, TRUE, NULL);
	LARGE_INTEGER freq, next, now;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&next);
	for (;;) {
		HANDLE stop = d->m_paceStop;
		if (!stop || WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) break;
		int hz = (int)d->m_paceHz;
		if (hz < 30) hz = 30;
		if (hz > 240) hz = 240;
		const LONGLONG period = freq.QuadPart / hz;
		QueryPerformanceCounter(&now);
		next.QuadPart += period;
		if (next.QuadPart < now.QuadPart)
			next = now;
		else if (timer) {
			const LONGLONG remain = next.QuadPart - now.QuadPart;
			LARGE_INTEGER due;
			due.QuadPart = -(LONGLONG)((remain * 10000000ULL + (ULONGLONG)freq.QuadPart / 2)
				/ (ULONGLONG)freq.QuadPart);
			if (due.QuadPart >= 0) due.QuadPart = -1;
			if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
				HANDLE w[2] = { timer, stop };
				DWORD cap = (DWORD)((remain * 1000) / freq.QuadPart) + 4;
				if (cap < 1) cap = 1;
				if (cap > 48) cap = 48;
				if (WaitForMultipleObjects(2, w, FALSE, cap) == WAIT_OBJECT_0 + 1) break;
			}
		}
		if (d->m_destroying) break;
		d->KickPace();
	}
	if (timer) CloseHandle(timer);
	return 0;
}

void COmakeTacticsDlg::StartPace()
{
	if (m_paceThread) return;
	if (!m_paceStop) m_paceStop = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (m_paceStop) ResetEvent(m_paceStop);
	m_paceHz = 60;
	m_paceThread = CreateThread(NULL, 256u * 1024u, PaceThread, this, 0, NULL);
}

void COmakeTacticsDlg::StopPace()
{
	if (m_paceStop) SetEvent(m_paceStop);
	if (m_paceThread) {
		WaitForSingleObject(m_paceThread, INFINITE);
		CloseHandle(m_paceThread);
		m_paceThread = NULL;
	}
	if (m_paceStop) {
		CloseHandle(m_paceStop);
		m_paceStop = NULL;
	}
}

void COmakeTacticsDlg::ReleaseUiInput()
{
	m_view.ReleaseButtons();
	if (m_gameOk) m_game.ReleaseInput();
}

void COmakeTacticsDlg::DrainInput()
{
	if (m_destroying) return;
	HWND hw[2] = { m_hWnd, m_view.GetSafeHwnd() };
	MSG msg;
	for (int i = 0; i < 2; i++) {
		if (!hw[i]) continue;
		while (::PeekMessage(&msg, hw[i], WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
			if (m_destroying || !GetSafeHwnd()) return;
			if (PreTranslateMessage(&msg)) continue;
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (m_destroying || !GetSafeHwnd()) return;
		}
	}
}

LRESULT COmakeTacticsDlg::OnTacPace(WPARAM, LPARAM)
{
	if (m_destroying || m_inPace || !m_gameOk) {
		InterlockedExchange(&m_pacePosted, 0);
		return 0;
	}
	m_inPace = TRUE;
	// 描画中に次の拍が来たら捨てず、終わった直後に続ける。捨てると 16ms 超過が 33ms 空きになり 30 と 60 を往復する。
	InterlockedExchange(&m_pacePosted, 0);
	DrainInput();
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	float dt = m_qpf.QuadPart ? (float)((double)(now.QuadPart - m_tickAt.QuadPart) / (double)m_qpf.QuadPart) : .016f;
	if (dt < 0) dt = 0;
	if (dt > 0.1f) dt = 0.1f;
	m_tickAt = now;
	int hz = m_game.PaceHz();
	if (hz < 30) hz = 30;
	m_paceHz = hz;
	m_fg = (::GetForegroundWindow() == m_hWnd);
	{
		static int pri = -1;
		int want = m_fg ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL;
		if (pri != want) {
			SetThreadPriority(GetCurrentThread(), want);
			if (m_paceThread) SetThreadPriority(m_paceThread, want);
			pri = want;
		}
	}
	if (!IsIconic() && IsWindowVisible()) {
		LARGE_INTEGER a0, a1, af;
		QueryPerformanceCounter(&a0);
		if (!TacAudioOnThread()) TacAudioPump();
		QueryPerformanceCounter(&a1);
		QueryPerformanceFrequency(&af);
		if (m_gameOk) m_game.SetAudioMs(af.QuadPart ? (float)((double)(a1.QuadPart - a0.QuadPart) * 1000.0 / (double)af.QuadPart) : 0.f);
		PollPad(dt);
		m_game.Tick(dt);
		SyncImeEdit();
		ApplyWinMode();
		if (m_game.WantsClose()) {
			m_game.ClearClose();
			m_inPace = FALSE;
			InterlockedExchange(&m_pacePosted, 0);
			RequestDestroyWindow();
			return 0;
		}
	}
	m_inPace = FALSE;
	InterlockedExchange(&m_pacePosted, 0);
	return 0;
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
	if (bShow && CCC_GetCustomCaptionHeight(m_hWnd) > 0)
		CWnd::OnShowWindow(bShow, nStatus);
	else
		CCustomBlurDialogBase::OnShowWindow(bShow, nStatus);
	if (bShow) {
		QueryPerformanceCounter(&m_tickAt);
		if (m_gameOk) m_game.OnBecameActive();
		KickPace();
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
	CCustomDialog::OnActivate(nState, pWndOther, bMinimized);
	const BOOL fg = (nState != WA_INACTIVE && !bMinimized);
	m_fg = fg;
	if (fg) {
		QueryPerformanceCounter(&m_tickAt);
		if (m_gameOk && !m_swapShown) {
			m_swapShown = TRUE;
			LayoutView();
		}
		if (m_gameOk && m_game.WantsTextInput() && m_ime.GetSafeHwnd())
			m_ime.SetFocus();
		else if (GetSafeHwnd())
			SetFocus();
		if (m_gameOk) {
			m_game.OnBecameActive();
			KickPace();
		}
	} else {
		ReleaseUiInput();
	}
}

void COmakeTacticsDlg::OnActivateApp(BOOL bActive, DWORD dwThreadID)
{
	CCustomDialog::OnActivateApp(bActive, dwThreadID);
	m_fg = bActive;
	if (bActive) {
		QueryPerformanceCounter(&m_tickAt);
		if (m_gameOk) {
			m_game.OnBecameActive();
			KickPace();
		}
	} else {
		ReleaseUiInput();
	}
}

static BOOL TacImeBusy(HWND h)
{
	if (!h) return FALSE;
	HIMC himc = ::ImmGetContext(h);
	if (!himc) return FALSE;
	LONG n = ::ImmGetCompositionStringW(himc, GCS_COMPSTR, NULL, 0);
	::ImmReleaseContext(h, himc);
	return n > 0;
}

static UINT TacRealVk(MSG* pMsg)
{
	UINT vk = (UINT)pMsg->wParam;
	if ((pMsg->message == WM_KEYDOWN || pMsg->message == WM_KEYUP || pMsg->message == WM_SYSKEYDOWN || pMsg->message == WM_SYSKEYUP)
		&& vk == VK_PROCESSKEY)
		vk = ::ImmGetVirtualKey(pMsg->hwnd);
	return vk;
}

void COmakeTacticsDlg::HideImeEdit()
{
	m_imeKind = 0;
	m_imePush = FALSE;
	if (m_ime.GetSafeHwnd() && ::GetFocus() == m_ime.GetSafeHwnd() && GetSafeHwnd())
		SetFocus();
	if (m_ime.GetSafeHwnd()) m_ime.ShowWindow(SW_HIDE);
}

void COmakeTacticsDlg::PullImeToGame()
{
	if (!m_gameOk || m_imePush || !m_ime.GetSafeHwnd()) return;
	CString t;
	m_ime.GetWindowText(t);
	int caret = t.GetLength();
	int sel = 0, sel2 = 0;
	m_ime.GetSel(sel, sel2);
	caret = sel;
	HWND ih = m_ime.GetSafeHwnd();
	wchar_t comp[48];
	comp[0] = 0;
	HIMC himc = ::ImmGetContext(ih);
	if (himc) {
		LONG n = ::ImmGetCompositionStringW(himc, GCS_COMPSTR, comp, sizeof(comp) - sizeof(wchar_t));
		if (n < 0) n = 0;
		comp[n / sizeof(wchar_t)] = 0;
		::ImmReleaseContext(ih, himc);
	}
	m_game.SetImeComp(comp);
	if (m_game.TextFocus() == 3) {
		m_game.SetSellFromText(t);
		return;
	}
	wchar_t* buf = NULL;
	int maxLen = 0;
	if (!m_game.TextLive(buf, maxLen) || !buf || maxLen < 2) return;
	wchar_t prev[64];
	wcsncpy_s(prev, buf, _TRUNCATE);
	wcsncpy_s(buf, maxLen, t, _TRUNCATE);
	m_game.SetTextCaret(caret);
	if (m_game.TextFocus() == 4 && wcscmp(prev, buf) != 0) m_game.NotifyFindChanged();
}

void COmakeTacticsDlg::PlaceImeWindow()
{
	if (!m_gameOk || !m_ime.GetSafeHwnd() || !m_view.GetSafeHwnd()) return;
	static LONG s_imePosBusy = 0;
	if (InterlockedCompareExchange(&s_imePosBusy, 1, 0) != 0) return;
	RECT box = {};
	m_game.TextBoxRect(box);
	POINT caret = { box.left + (LONG)m_game.TextCaretPx(), box.top };
	::ClientToScreen(m_view.GetSafeHwnd(), &caret);
	CPoint dlgPt = caret;
	ScreenToClient(&dlgPt);
	int h = box.bottom - box.top;
	if (h < 8) h = 8;
	m_ime.SetWindowPos(&wndTop, dlgPt.x, dlgPt.y, 1, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	HIMC himc = ::ImmGetContext(m_ime.GetSafeHwnd());
	if (himc) {
		COMPOSITIONFORM cf = {};
		cf.dwStyle = CFS_POINT;
		cf.ptCurrentPos.x = 0;
		cf.ptCurrentPos.y = 0;
		::ImmSetCompositionWindow(himc, &cf);
		CANDIDATEFORM cand = {};
		cand.dwIndex = 0;
		cand.dwStyle = CFS_CANDIDATEPOS;
		cand.ptCurrentPos.x = 0;
		cand.ptCurrentPos.y = h;
		::ImmSetCandidateWindow(himc, &cand);
		::ImmReleaseContext(m_ime.GetSafeHwnd(), himc);
	}
	InterlockedExchange(&s_imePosBusy, 0);
}

void COmakeTacticsDlg::SyncImeEdit()
{
	if (!m_gameOk || m_destroying || !m_ime.GetSafeHwnd()) return;
	const int focus = m_game.TextFocus();
	if (!focus) {
		if (m_imeKind) HideImeEdit();
		return;
	}
	if (focus != m_imeKind) {
		m_imePush = TRUE;
		wchar_t tmp[64];
		tmp[0] = 0;
		int limit = 15;
		if (focus == 3) {
			m_game.SellToText(tmp, 64);
			limit = 8;
		} else {
			wchar_t* buf = NULL;
			int maxLen = 0;
			if (m_game.TextLive(buf, maxLen) && buf) {
				wcsncpy_s(tmp, buf, _TRUNCATE);
				limit = maxLen > 1 ? maxLen - 1 : 1;
			}
		}
		m_ime.SetLimitText(limit);
		m_ime.SetWindowText(tmp);
		int n = (int)wcslen(tmp);
		if (focus == 3) m_ime.SetSel(0, -1);
		else m_ime.SetSel(n, n);
		m_game.SetTextCaret(focus == 3 ? n : n);
		m_imeKind = focus;
		m_imePush = FALSE;
	}
	PlaceImeWindow();
	if (::GetFocus() != m_ime.GetSafeHwnd()) m_ime.SetFocus();
}

UINT COmakeTacticsDlg::OnGetDlgCode()
{
	return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTTAB;
}

BOOL COmakeTacticsDlg::HandleKeyMessage(MSG* pMsg)
{
	if (!pMsg || m_destroying || !GetSafeHwnd()) return FALSE;
	const UINT msg = pMsg->message;
	const BOOL imeMsg = (msg == WM_IME_CHAR || msg == WM_UNICHAR || msg == WM_DEADCHAR
		|| msg == WM_IME_COMPOSITION || msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_ENDCOMPOSITION
		|| msg == WM_IME_NOTIFY || msg == WM_IME_REQUEST || msg == WM_IME_KEYDOWN || msg == WM_IME_KEYUP
		|| msg == WM_IME_SETCONTEXT || msg == WM_IME_CONTROL);
	if (msg < WM_KEYFIRST && !imeMsg) return FALSE;
	if (msg > WM_KEYLAST && !imeMsg) return FALSE;
	HWND fg = ::GetForegroundWindow();
	const BOOL ours = (fg == m_hWnd || (fg && ::IsChild(m_hWnd, fg)));
	HWND imeHw = m_ime.GetSafeHwnd();
	if (!ours && pMsg->hwnd != m_hWnd && !::IsChild(m_hWnd, pMsg->hwnd)
		&& !(imeHw && pMsg->hwnd == imeHw))
		return FALSE;
	if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
		if (pMsg->wParam == VK_F4 || pMsg->wParam == VK_MENU) return FALSE;
	}
	if (msg == WM_SYSCHAR) return FALSE;

	const BOOL typing = m_gameOk && (m_game.WantsTextInput() || (imeHw && ::GetFocus() == imeHw));
	if (typing) {
		const UINT vk = TacRealVk(pMsg);
		const BOOL composing = TacImeBusy(imeHw);
		if (msg == WM_KEYDOWN && (vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB) && !composing) {
			PullImeToGame();
			if (m_gameOk) m_game.OnKey((int)vk, TRUE);
			HideImeEdit();
			return TRUE;
		}
		if (imeHw && ::GetFocus() != imeHw) m_ime.SetFocus();
		if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) ::TranslateMessage(pMsg);
		HWND dest = imeHw ? imeHw : pMsg->hwnd;
		if (dest && pMsg->hwnd != dest) {
			MSG m2 = *pMsg;
			m2.hwnd = dest;
			::DispatchMessage(&m2);
		} else {
			::DispatchMessage(pMsg);
		}
		PullImeToGame();
		PlaceImeWindow();
		return TRUE;
	}

	if (msg == WM_CHAR || msg == WM_IME_CHAR || msg == WM_UNICHAR || msg == WM_DEADCHAR)
		return TRUE;
	if (imeMsg)
		return TRUE;
	if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
		if (m_gameOk) {
			const UINT vk = TacRealVk(pMsg);
			const BOOL down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
			const BOOL repeat = (msg == WM_KEYDOWN && (pMsg->lParam & (1 << 30)));
			if (repeat) {
				const BOOL rep = vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN
					|| vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D' || vk == VK_PRIOR || vk == VK_NEXT;
				if (rep) m_game.OnKey((int)vk, TRUE);
			} else {
				m_game.OnKey((int)vk, down);
			}
		}
		return TRUE;
	}
	return TRUE;
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
	HideImeEdit();
	ReleaseUiInput();
	StopPace();
	TacAudioStop();
	if (m_gameOk) {
		StoreWindowRect();
		m_game.Persist();
		m_game.Shutdown();
		m_gameOk = FALSE;
	}
	TacReleasePadAll();
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
	CCustomBlurDialogBase::OnDestroy();
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SetTimer(4923, 20, NULL);
}

void COmakeTacticsDlg::RequestDestroyWindow()
{
	if (m_destroying) return;
	m_destroying = TRUE;
	HideImeEdit();
	if (GetSafeHwnd()) {
		HWND parent = ::GetParent(m_hWnd);
		ShowWindow(SW_HIDE);
		if (parent && ::IsWindow(parent) && ::IsWindowVisible(parent) && !::IsIconic(parent))
			::SetForegroundWindow(parent);
		if (!::PostMessage(m_hWnd, WM_TAC_DESTROY, 0, 0))
			OnTacDestroy(0, 0);
	}
}

void COmakeTacticsDlg::OnTacCapClose() { RequestDestroyWindow(); }

LRESULT COmakeTacticsDlg::OnTacDestroy(WPARAM, LPARAM)
{
	if (m_inPace) {
		if (GetSafeHwnd()) ::PostMessage(m_hWnd, WM_TAC_DESTROY, 0, 0);
		return 0;
	}
	HideImeEdit();
	ReleaseUiInput();
	StopPace();
	TacAudioStop();
	if (m_gameOk) {
		StoreWindowRect();
		m_game.Persist();
		m_game.Shutdown();
		m_gameOk = FALSE;
	}
	TacReleasePadAll();
	if (GetSafeHwnd()) DestroyWindow();
	return 0;
}

void COmakeTacticsDlg::OnClose() { RequestDestroyWindow(); }

void COmakeTacticsDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == SC_CLOSE) {
		RequestDestroyWindow();
		return;
	}
	CDialog::OnSysCommand(nID, lParam);
}

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
		if (g_tac->Destroying()) return;
		if (g_tac->IsIconic()) g_tac->ShowWindow(SW_RESTORE);
		g_tac->SetForegroundWindow();
		return;
	}
	extern void CloseSoft3DRaceIfOpen();
	extern void CloseSoft3DMazeIfOpen();
	CloseSoft3DRaceIfOpen();
	CloseSoft3DMazeIfOpen();
	if (!EnsureBonusImages(2)) return;
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
