// おまけタクティクスゲーム: ダイアログ（ホスト）
#include "stdafx.h"
#include "ogg.h"
#include "OmakeTacticsDlg.h"
#include "OmakeTactics.h"
#include "OmakeTacticsAudio.h"
#include "oggDlg.h"
#include "UpdateCheck.h"
#include <imm.h>
#include <dwmapi.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace otac;
namespace otac { void TacShopSerReset(); }

extern COggDlg* og;

void TacHookSysCommands(HWND hwnd);

static COmakeTacticsDlg* g_tac = nullptr;
// ロード完了から少しのあいだ、クリックなしで自プロセスの窓が被さったら一度だけ戻す。
static DWORD s_frontUntil = 0;
static int s_frontUsed = 0;
static int s_tacStarting = 0;
static int s_paceArmed = 0;
static DWORD s_holdFgUntil = 0;
static int s_holdAfterLoad = 0;

static BOOL TacKeepFg()
{
	return s_holdFgUntil && ::GetTickCount() < s_holdFgUntil;
}
static void TacClearWaitCursor();
static void TacDropHostNoise(HWND root);

static void TacParkHostUi(HWND keep)
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->ParkHostUiForTactics(keep);
}

static void TacUnparkHostUi()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->UnparkHostUiAfterTactics();
}
// らいらXのボタン処理中は 1。このあいだに描き続けると、処理が戻らず画面が黒のままになる。
static int s_tacOpenDepth = 0;
#ifndef WM_TAC_PACE
#define WM_TAC_PACE (WM_APP + 549)
#endif
#ifndef WM_TAC_DESTROY
#define WM_TAC_DESTROY (WM_APP + 550)
#endif
enum { IDC_TAC_IME = 0x7A10 };

static void TacBindHIMC(HWND h, void*& ctx)
{
	if (!h) return;
	if (!ctx) {
		HIMC hc = ::ImmCreateContext();
		if (hc) ctx = hc;
	}
	if (ctx) ::ImmAssociateContext(h, (HIMC)ctx);
}

static void TacFocusIme(CEdit& ed, void*& ctx)
{
	HWND h = ed.GetSafeHwnd();
	if (!h) return;
	if (!::IsWindowEnabled(h)) ::EnableWindow(h, TRUE);
	TacBindHIMC(h, ctx);
	HIMC himc = ::ImmGetContext(h);
	if (himc) {
		::ImmSetOpenStatus(himc, TRUE);
		::ImmReleaseContext(h, himc);
	}
	if (::GetFocus() != h)
		ed.SetFocus();
}

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
// COmakeTacticsDlg
// ダイアログ＋所有ポップアップだと DWM が合成する。非アクティブから戻したときだけ直出しになり、
// 最小化でまた合成に落ちる。フレーム自身にスワップチェインを張る。
// ---------------------------------------------------------------------------
IMPLEMENT_DYNAMIC(COmakeTacticsDlg, CFrameWnd)

BEGIN_MESSAGE_MAP(COmakeTacticsDlg, CFrameWnd)
	ON_WM_CREATE()
	ON_WM_TIMER()
	ON_MESSAGE(WM_TAC_PACE, OnTacPace)
	ON_MESSAGE(WM_TAC_DESTROY, OnTacDestroy)
	ON_WM_SIZE()
	ON_WM_MOVE()
	ON_WM_ENTERSIZEMOVE()
	ON_WM_EXITSIZEMOVE()
	ON_WM_NCHITTEST()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_RBUTTONDOWN()
	ON_WM_RBUTTONUP()
	ON_WM_MBUTTONDOWN()
	ON_WM_MBUTTONUP()
	ON_WM_CAPTURECHANGED()
	ON_WM_ERASEBKGND()
	ON_WM_PAINT()
	ON_WM_SETCURSOR()
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
	ON_WM_CTLCOLOR()
	ON_EN_CHANGE(IDC_TAC_IME, OnImeChange)
	ON_EN_SETFOCUS(IDC_TAC_IME, OnImeSetFocus)
END_MESSAGE_MAP()

COmakeTacticsDlg::COmakeTacticsDlg()
{
}

COmakeTacticsDlg::~COmakeTacticsDlg()
{
	if (g_tac == this) g_tac = nullptr;
}

static const wchar_t* TacCaption()
{
	return TT(TX_TITLE);
}

BOOL COmakeTacticsDlg::CreateFrame()
{
	SaveData& sv = m_game.Save();
	SaveDefaults(sv);
	SaveLoad(sv);
	int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1280, h = 800;
	if (sv.winSizeVer >= 3 && sv.winW >= 640 && sv.winH >= 420) {
		CRect r(sv.winX, sv.winY, sv.winX + sv.winW, sv.winY + sv.winH);
		CRect vs(::GetSystemMetrics(SM_XVIRTUALSCREEN), ::GetSystemMetrics(SM_YVIRTUALSCREEN),
			::GetSystemMetrics(SM_XVIRTUALSCREEN) + ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
			::GetSystemMetrics(SM_YVIRTUALSCREEN) + ::GetSystemMetrics(SM_CYVIRTUALSCREEN));
		CRect inter;
		if (inter.IntersectRect(&r, &vs) && inter.Width() > 120 && inter.Height() > 80) {
			x = r.left; y = r.top; w = r.Width(); h = r.Height();
		}
	}
	if (x == CW_USEDEFAULT) {
		HMONITOR mon = ::MonitorFromWindow(NULL, MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO mi = { sizeof(mi) };
		if (::GetMonitorInfo(mon, &mi)) {
			const CRect wa(mi.rcWork);
			w = wa.Width() * 7 / 10;
			h = wa.Height() * 7 / 10;
			if (w < 800) w = min(800, wa.Width());
			if (h < 560) h = min(560, wa.Height());
			if (w > wa.Width()) w = wa.Width();
			if (h > wa.Height()) h = wa.Height();
			x = wa.left + (wa.Width() - w) / 2;
			y = wa.top + (wa.Height() - h) / 2;
		}
	}
	HICON icon = ::LoadIcon(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_UI_TACTICS));
	const CString cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW),
		(HBRUSH)::GetStockObject(BLACK_BRUSH), icon);
	return CFrameWnd::CreateEx(WS_EX_APPWINDOW, cls, TacCaption(),
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		x, y, w, h, NULL, NULL, 0);
}

int COmakeTacticsDlg::OnCreate(LPCREATESTRUCT cs)
{
	if (CFrameWnd::OnCreate(cs) == -1) return -1;
	SetWindowText(TacCaption());
	CRect rc;
	GetClientRect(&rc);
	if (rc.Width() < 8) rc.right = rc.left + 8;
	if (rc.Height() < 8) rc.bottom = rc.top + 8;
	m_viewW = rc.Width();
	m_viewH = rc.Height();
	m_ime.Create(WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_WANTRETURN, CRect(0, 0, 8, 8), this, IDC_TAC_IME);
	if (m_ime.GetSafeHwnd()) {
		HWND ih = m_ime.GetSafeHwnd();
		m_ime.ModifyStyle(WS_BORDER, 0);
		m_ime.ModifyStyleEx(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE, 0);
		LONG ex = ::GetWindowLongW(ih, GWL_EXSTYLE);
		::SetWindowLongW(ih, GWL_EXSTYLE, (ex | WS_EX_NOINHERITLAYOUT) & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_LAYERED | WS_EX_TRANSPARENT));
		m_imeBrush.CreateSolidBrush(RGB(40, 46, 80));
		m_imeFontPx = 16;
		m_imeFont.CreateFont(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
		if (m_imeFont.GetSafeHandle()) m_ime.SetFont(&m_imeFont);
		m_ime.SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
		m_ime.EnableWindow(TRUE);
		m_ime.ShowWindow(SW_HIDE);
	}
	if (m_ime.GetSafeHwnd()) TacBindHIMC(m_ime.GetSafeHwnd(), m_imeCtx);
	m_gameOk = m_game.Init(m_hWnd, m_viewW, m_viewH);
	TacHookSysCommands(m_hWnd);
	TacAudioStart(m_hWnd);

	QueryPerformanceFrequency(&m_qpf);
	QueryPerformanceCounter(&m_tickAt);
	return 0;
}

void COmakeTacticsDlg::LayoutView()
{
	if (!GetSafeHwnd() || IsIconic()) return;
	CRect rc;
	GetClientRect(&rc);
	if (rc.Width() < 8 || rc.Height() < 8) return;
	if (rc.Width() != m_viewW || rc.Height() != m_viewH) {
		m_viewW = rc.Width();
		m_viewH = rc.Height();
		if (m_gameOk) m_game.Resize(m_viewW, m_viewH);
	}
}

static const DWORD kTacChrome = WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER;

static void TacEnableSystemMenu(HWND hwnd)
{
	if (!hwnd) return;
	HMENU menu = ::GetSystemMenu(hwnd, FALSE);
	if (!menu) return;
	const UINT cmd[] = { SC_CLOSE, SC_MINIMIZE, SC_MAXIMIZE, SC_RESTORE, SC_MOVE, SC_SIZE };
	for (int i = 0; i < 6; i++)
		::EnableMenuItem(menu, cmd[i], MF_BYCOMMAND | MF_ENABLED);
}

static LRESULT TacCaptionButton(LONG style, int relX, int left, int right)
{
	const BOOL hasMin = (style & WS_MINIMIZEBOX) != 0;
	const BOOL hasMax = (style & WS_MAXIMIZEBOX) != 0;
	const BOOL hasClose = (style & WS_SYSMENU) != 0;
	int n = (hasMin ? 1 : 0) + (hasMax ? 1 : 0) + (hasClose ? 1 : 0);
	if (n < 1) return HTCAPTION;
	const int span = right - left;
	const int bw = span / n;
	int i = bw > 0 ? (relX - left) / bw : 0;
	if (i < 0) i = 0;
	if (i >= n) i = n - 1;
	int k = 0;
	if (hasMin) { if (i == k) return HTMINBUTTON; k++; }
	if (hasMax) { if (i == k) return HTMAXBUTTON; k++; }
	if (hasClose) return HTCLOSE;
	return HTCAPTION;
}

// 転送面なし＋フリップだと、枠ボタンの当たりがクライアントになり × がゲームのクリックになる。
static LRESULT TacFixNcHit(HWND hwnd, LPARAM lp, LRESULT hit)
{
	if (!hwnd) return hit;
	const LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
	if (!(style & WS_CAPTION)) return hit;
	if (hit == HTCLOSE || hit == HTMINBUTTON || hit == HTMAXBUTTON || hit == HTHELP)
		return hit;
	POINT pt;
	pt.x = (short)LOWORD(lp);
	pt.y = (short)HIWORD(lp);
	RECT wr;
	if (!::GetWindowRect(hwnd, &wr)) return hit;
	const int relX = pt.x - wr.left;
	const int relY = pt.y - wr.top;
	const int ww = wr.right - wr.left;
	if (relX < 0 || relY < 0 || relX >= ww) return hit;

	RECT buttons = {};
	const BOOL haveBtns = SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &buttons, sizeof(buttons)))
		&& buttons.right > buttons.left && buttons.bottom > buttons.top;
	if (haveBtns && relX >= buttons.left && relX < buttons.right && relY >= buttons.top && relY < buttons.bottom)
		return TacCaptionButton(style, relX, buttons.left, buttons.right);

	if (hit != HTCLIENT && hit != HTNOWHERE && hit != HTCAPTION && hit != HTBORDER)
		return hit;

	RECT cr;
	if (!::GetClientRect(hwnd, &cr)) return hit;
	POINT tl = { 0, 0 };
	::ClientToScreen(hwnd, &tl);
	const int cl = tl.x;
	const int ct = tl.y;
	const int crg = tl.x + (cr.right - cr.left);
	const int cb = tl.y + (cr.bottom - cr.top);
	if (pt.x >= cl && pt.x < crg && pt.y >= ct && pt.y < cb)
		return hit;

	const int fx = ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
	const int fy = ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
	const BOOL thick = (style & WS_THICKFRAME) != 0;
	const BOOL left = pt.x < wr.left + fx;
	const BOOL right = pt.x >= wr.right - fx;
	const BOOL top = pt.y < wr.top + fy;
	const BOOL bottom = pt.y >= wr.bottom - fy;
	if (thick && (left || right || top || bottom)) {
		if (top && left) return HTTOPLEFT;
		if (top && right) return HTTOPRIGHT;
		if (bottom && left) return HTBOTTOMLEFT;
		if (bottom && right) return HTBOTTOMRIGHT;
		if (left) return HTLEFT;
		if (right) return HTRIGHT;
		if (top) return HTTOP;
		if (bottom) return HTBOTTOM;
	}
	if (pt.y < ct) {
		if ((style & WS_SYSMENU) && relX < fx + ::GetSystemMetrics(SM_CXSMICON))
			return HTSYSMENU;
		if (!haveBtns && (style & WS_SYSMENU)) {
			const int cx = ::GetSystemMetrics(SM_CXSIZE);
			int n = 1;
			if (style & WS_MINIMIZEBOX) n++;
			if (style & WS_MAXIMIZEBOX) n++;
			const int btnL = ww - fx - cx * n;
			if (relX >= btnL) return TacCaptionButton(style, relX, btnL, ww - fx);
		}
		return HTCAPTION;
	}
	return hit;
}

// DXGI はスワップチェイン作成時に窓手続きを掴み、×・最小化・最大化を捨てる。
// それより外側に置き、システムコマンドだけ DefWindowProc へ直に渡す。
static WNDPROC g_tacAfxProc = NULL;
static WNDPROC g_tacNextProc = NULL;
static int g_tacProcDepth = 0;
static LRESULT CALLBACK TacSysProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static LRESULT TacCallNext(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	WNDPROC next = g_tacNextProc;
	if (!next || next == TacSysProc) next = g_tacAfxProc;
	if (!next || next == TacSysProc) return ::DefWindowProc(hwnd, msg, wp, lp);
	g_tacProcDepth++;
	const LRESULT r = ::CallWindowProc(next, hwnd, msg, wp, lp);
	g_tacProcDepth--;
	return r;
}

static LRESULT CALLBACK TacSysProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (g_tacProcDepth > 0) {
		if (g_tacAfxProc && g_tacAfxProc != TacSysProc)
			return ::CallWindowProc(g_tacAfxProc, hwnd, msg, wp, lp);
		return ::DefWindowProc(hwnd, msg, wp, lp);
	}
	if (msg == WM_NCDESTROY) {
		WNDPROC afx = g_tacAfxProc;
		g_tacAfxProc = NULL;
		g_tacNextProc = NULL;
		if (afx) return ::CallWindowProc(afx, hwnd, msg, wp, lp);
		return ::DefWindowProc(hwnd, msg, wp, lp);
	}
	if (msg == WM_NCHITTEST) {
		const LRESULT hit = TacCallNext(hwnd, msg, wp, lp);
		return TacFixNcHit(hwnd, lp, hit);
	}
	if (msg == WM_NCLBUTTONDOWN || msg == WM_NCLBUTTONUP || msg == WM_NCLBUTTONDBLCLK
		|| msg == WM_NCRBUTTONDOWN || msg == WM_NCRBUTTONUP || msg == WM_NCRBUTTONDBLCLK) {
		const UINT hit = (UINT)wp;
		if (hit == HTCLOSE || hit == HTMINBUTTON || hit == HTMAXBUTTON || hit == HTHELP
			|| hit == HTCAPTION || hit == HTSYSMENU
			|| hit == HTLEFT || hit == HTRIGHT || hit == HTTOP || hit == HTBOTTOM
			|| hit == HTTOPLEFT || hit == HTTOPRIGHT || hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT)
			return ::DefWindowProc(hwnd, msg, wp, lp);
	}
	if (msg == WM_SYSCOMMAND) {
		const UINT cmd = ((UINT)wp) & 0xFFF0;
		COmakeTacticsDlg* self = (COmakeTacticsDlg*)CWnd::FromHandlePermanent(hwnd);
		if (cmd == SC_CLOSE) {
			if (self) self->RequestDestroyWindow();
			else ::DestroyWindow(hwnd);
			return 0;
		}
		if (cmd == SC_MINIMIZE || cmd == SC_MAXIMIZE || cmd == SC_RESTORE
			|| cmd == SC_MOVE || cmd == SC_SIZE
			|| cmd == SC_MOUSEMENU || cmd == SC_KEYMENU || cmd == SC_DEFAULT) {
			if (cmd == SC_MINIMIZE && self && self->GameReady() && self->GameRef().Exclusive())
				self->GameRef().SetExclusive(FALSE);
			if (cmd == SC_KEYMENU || cmd == SC_MOUSEMENU)
				TacEnableSystemMenu(hwnd);
			return ::DefWindowProc(hwnd, msg, wp, lp);
		}
	}
	return TacCallNext(hwnd, msg, wp, lp);
}

void TacNoteFrameProc(HWND hwnd)
{
	if (!hwnd || g_tacAfxProc) return;
	WNDPROC cur = (WNDPROC)::GetWindowLongPtr(hwnd, GWLP_WNDPROC);
	if (cur && cur != TacSysProc) g_tacAfxProc = cur;
}

void TacHookSysCommands(HWND hwnd)
{
	if (!hwnd) return;
	WNDPROC cur = (WNDPROC)::GetWindowLongPtr(hwnd, GWLP_WNDPROC);
	if (!g_tacAfxProc && cur && cur != TacSysProc) g_tacAfxProc = cur;
	if (cur == TacSysProc) return;
	g_tacNextProc = cur;
	::SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)TacSysProc);
	TacEnableSystemMenu(hwnd);
}

void TacSysProcDetachSwap()
{
	g_tacNextProc = g_tacAfxProc;
}

void COmakeTacticsDlg::ApplyChrome(BOOL windowed)
{
	if (windowed) {
		ModifyStyle(0, kTacChrome, SWP_FRAMECHANGED);
		TacEnableSystemMenu(m_hWnd);
	} else {
		ModifyStyle(kTacChrome, 0, SWP_FRAMECHANGED);
	}
}

BOOL COmakeTacticsDlg::MonitorSize(RECT* rc, int* pxW, int* pxH) const
{
	if (!rc) return FALSE;
	HMONITOR mon = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW mi = { sizeof(mi) };
	if (!::GetMonitorInfoW(mon, &mi)) return FALSE;
	*rc = mi.rcMonitor;
	int w = mi.rcMonitor.right - mi.rcMonitor.left;
	int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
	DEVMODEW dm = {};
	dm.dmSize = sizeof(dm);
	if (::EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
		if (dm.dmPelsWidth > 0) w = (int)dm.dmPelsWidth;
		if (dm.dmPelsHeight > 0) h = (int)dm.dmPelsHeight;
	}
	if (pxW) *pxW = w;
	if (pxH) *pxH = h;
	return w > 0 && h > 0;
}

static BOOL TacInFamily(HWND h, HWND root)
{
	if (!h || !root) return FALSE;
	for (int i = 0; h && i < 12; i++) {
		if (h == root || ::IsChild(root, h)) return TRUE;
		HWND owner = ::GetWindow(h, GW_OWNER);
		if (!owner || owner == h) break;
		h = owner;
	}
	return FALSE;
}

void COmakeTacticsDlg::SyncBorderlessZ()
{
	if (!GetSafeHwnd() || m_destroying) return;
	// 前面のあいだは下げない。NOACTIVATE で最前面を外すと、操作中なのに本体の裏へ回る。
	if (m_fg) return;
	const LONG ex = ::GetWindowLong(m_hWnd, GWL_EXSTYLE);
	if (ex & WS_EX_TOPMOST)
		::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}


void COmakeTacticsDlg::ApplyWinMode()
{
	if (!m_gameOk || !GetSafeHwnd() || m_sizing) return;
	int mode = otac::TacWinMode(m_game.Save());
	if (m_winApplied == mode) {
		if (mode == 2 && m_fg && !IsIconic() && !m_exclFail && !m_game.Exclusive()) {
			if (!m_game.SetExclusive(TRUE)) m_exclFail = TRUE;
		}
		return;
	}
	int prev = m_winApplied;
	if (prev <= 0) StoreWindowRect();
	otac::SaveData& saved = m_game.Save();
	const int rx = saved.winX, ry = saved.winY, rw = saved.winW, rh = saved.winH;
	m_exclFail = FALSE;
	if (prev < 0 && mode == 0) {
		// 起動時は枠付きのまま。スワップチェインが外した×・最小化・最大化を戻す。
		ApplyChrome(TRUE);
		m_winApplied = 0;
		return;
	}
	// 窓に戻すまでは m_winApplied を 0 にしない。0 にした瞬間の OnSize が、
	// まだ画面いっぱいの矩形をウィンドウサイズとして保存してしまう。
	if (mode != 0) m_winApplied = mode;
	if (prev == 2) m_game.SetExclusive(FALSE);
	RECT mon = {};
	int pxW = 0, pxH = 0;
	const BOOL haveMon = MonitorSize(&mon, &pxW, &pxH);
	if (mode == 0) {
		ApplyChrome(TRUE);
		::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		if (rw >= 200 && rh >= 150)
			SetWindowPos(NULL, rx, ry, rw, rh, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
		saved.winX = rx; saved.winY = ry; saved.winW = rw; saved.winH = rh;
		saved.winSizeVer = 3;
		m_winApplied = 0;
		LayoutView();
		SyncBorderlessZ();
		return;
	}
	ApplyChrome(FALSE);
	if (haveMon) {
		::SetWindowPos(m_hWnd, HWND_NOTOPMOST,
			mon.left, mon.top, mon.right - mon.left, mon.bottom - mon.top,
			SWP_SHOWWINDOW | SWP_FRAMECHANGED);
		if (mode == 1) ::SetForegroundWindow(m_hWnd);
	}
	LayoutView();
	if (mode == 2) {
		if (!m_game.SetExclusive(TRUE)) m_exclFail = TRUE;
		LayoutView();
	}
}

void COmakeTacticsDlg::StoreWindowRect()
{
	if (!GetSafeHwnd()) return;
	if (m_winApplied > 0) return;
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
	if (!m_fg) { memset(&p, 0, sizeof(p)); p.hat = -1; }
	m_game.OnPad(p, dt);
}

void COmakeTacticsDlg::OnTimer(UINT_PTR id)
{
	CWnd::OnTimer(id);
}

static void TacRaiseOnce(HWND h)
{
	if (!h) return;
	HWND fg = ::GetForegroundWindow();
	DWORD fgTid = 0;
	const DWORD selfTid = ::GetCurrentThreadId();
	if (fg) fgTid = ::GetWindowThreadProcessId(fg, NULL);
	BOOL att = FALSE;
	if (fg && fgTid && fgTid != selfTid)
		att = ::AttachThreadInput(fgTid, selfTid, TRUE);
	::AllowSetForegroundWindow(ASFW_ANY);
	// 最前面にして外すと、外した瞬間に本体の裏へ落ちる。最前面の帯には入れない。
	::SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
	::SetForegroundWindow(h);
	::BringWindowToTop(h);
	::SetFocus(h);
	if (att) ::AttachThreadInput(fgTid, selfTid, FALSE);
}

static int s_tacHoldDisp = 0;
static void TacHoldDisplay(BOOL on)
{
	if (on) {
		if (s_tacHoldDisp) return;
		s_tacHoldDisp = 1;
		::timeBeginPeriod(1);
		::SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
		::DwmEnableMMCSS(TRUE);
	} else if (s_tacHoldDisp) {
		s_tacHoldDisp = 0;
		::SetThreadExecutionState(ES_CONTINUOUS);
	}
}

void COmakeTacticsDlg::StartShown()
{
	if (!m_gameOk || m_destroying || !GetSafeHwnd()) return;
	// ボタン処理が戻ってから、別スレッドが 1 枚ずつ SendMessage する。
	// ここで描き続けると合成されず真っ黒。投稿して GetMessage を待つと、入力が無いとき約 3fps。
	TacHoldDisplay(TRUE);
	::DisableProcessWindowsGhosting();
	if (GetSafeHwnd())
		::SetClassLongPtr(GetSafeHwnd(), GCLP_HCURSOR, (LONG_PTR)::LoadCursor(NULL, IDC_ARROW));
	TacClearWaitCursor();
	s_frontUntil = 0;
	s_frontUsed = 0;
	s_paceArmed = 0;
	s_holdAfterLoad = 0;
	s_holdFgUntil = ::GetTickCount() + 25000;
	m_fg = TRUE;
	m_game.SetPaceFg(TRUE);
	StartPace();
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
	for (;;) {
		HANDLE stop = d->m_paceStop;
		if (!stop || WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) break;
		if (d->m_destroying) break;
		HWND hwnd = d->m_hWnd;
		if (!hwnd || !::IsWindow(hwnd)) break;
		const LRESULT r = ::SendMessage(hwnd, WM_TAC_PACE, 0, 0);
		if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) break;
		if (d->m_destroying) break;
		if (r == 2 || r == 3) {
			if (WaitForSingleObject(stop, r == 2 ? 50 : 1) == WAIT_OBJECT_0) break;
		}
	}
	return 0;
}

void COmakeTacticsDlg::StartPace()
{
	if (m_paceThread) return;
	if (!m_paceStop) m_paceStop = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (m_paceStop) ResetEvent(m_paceStop);
	if (!m_paceDone) m_paceDone = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (m_paceDone) ResetEvent(m_paceDone);
	m_paceHz = TAC_HZ_MAX;
	m_paceThread = CreateThread(NULL, 256u * 1024u, PaceThread, this, 0, NULL);
}

void COmakeTacticsDlg::StopPace()
{
	if (m_paceStop) SetEvent(m_paceStop);
	if (m_paceDone) SetEvent(m_paceDone);
	if (m_paceThread) {
		HANDLE th = m_paceThread;
		for (;;) {
			if (WaitForSingleObject(th, 0) == WAIT_OBJECT_0) break;
			// 拍スレッドは SendMessage で止まっている。送られたメッセージを流さないと終わらない。
			const DWORD r = MsgWaitForMultipleObjects(1, &th, FALSE, 50, QS_SENDMESSAGE);
			if (r == WAIT_OBJECT_0) break;
			MSG msg;
			::PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);
		}
		CloseHandle(m_paceThread);
		m_paceThread = NULL;
	}
	if (m_paceStop) {
		CloseHandle(m_paceStop);
		m_paceStop = NULL;
	}
	if (m_paceDone) {
		CloseHandle(m_paceDone);
		m_paceDone = NULL;
	}
}

void COmakeTacticsDlg::ReleaseUiInput()
{
	ReleaseButtons();
	if (m_gameOk) m_game.ReleaseInput();
}

void COmakeTacticsDlg::PumpPace()
{
	LARGE_INTEGER due;
	if (!m_gameOk || !m_game.TakePaceDue(due)) return;
	HWND self = m_hWnd;
	for (;;) {
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		if (now.QuadPart >= due.QuadPart) break;
		if (m_destroying || !GetSafeHwnd()) break;
		MSG msg;
		if (::PeekMessage(&msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {
			::PostQuitMessage((int)msg.wParam);
			break;
		}
		if (self && ::PeekMessage(&msg, self, WM_TAC_DESTROY, WM_TAC_DESTROY, PM_REMOVE)) {
			::PostMessage(self, WM_TAC_DESTROY, msg.wParam, msg.lParam);
			break;
		}
		// 余りがあるときだけ、マウスとキーを拾って待つ。WaitForSingleObject は使わない。
		DrainInput();
	}
}

void COmakeTacticsDlg::PumpLiveInput()
{
	if (m_destroying || !GetSafeHwnd()) return;
	static int s_busy = 0;
	if (s_busy) return;
	static DWORD s_last = 0;
	const DWORD now = ::GetTickCount();
	if (s_last && (now - s_last) < 4) return;
	s_last = now;
	s_busy = 1;
	DrainInput();
	s_busy = 0;
}

void OmakeTacticsPumpInput()
{
	if (g_tac) g_tac->PumpLiveInput();
}

static BOOL TacMsgOurs(HWND root, HWND h)
{
	return (h && root && (h == root || ::IsChild(root, h) || TacInFamily(h, root))) ? TRUE : FALSE;
}

static void TacClearWaitCursor()
{
	if (CWinApp* app = AfxGetApp()) {
		app->DoWaitCursor(-1);
		app->DoWaitCursor(0);
	}
	::SetCursor(::LoadCursor(NULL, IDC_ARROW));
}

// 本体の WM_TIMER / WM_PAINT を Dispatch せず捨てる。捨てないとキューが空にならず、
// 起動フィードバックの砂時計が約1秒残って、その間クリックが届かない。
static void TacDropHostNoise(HWND root)
{
	if (!root) return;
	MSG msg;
	int n = 0;
	while (n < 48) {
		if (!::PeekMessage(&msg, NULL, WM_TIMER, WM_TIMER, PM_NOREMOVE)) break;
		if (TacMsgOurs(root, msg.hwnd)) break;
		if (!msg.hwnd || !::PeekMessage(&msg, msg.hwnd, WM_TIMER, WM_TIMER, PM_REMOVE)) break;
		n++;
	}
	n = 0;
	while (n < 16) {
		if (!::PeekMessage(&msg, NULL, WM_PAINT, WM_PAINT, PM_NOREMOVE)) break;
		if (TacMsgOurs(root, msg.hwnd)) break;
		if (msg.hwnd) ::ValidateRect(msg.hwnd, NULL);
		if (!msg.hwnd || !::PeekMessage(&msg, msg.hwnd, WM_PAINT, WM_PAINT, PM_REMOVE)) break;
		n++;
	}
	::PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);
}

static BOOL TacPeekInput(HWND root, MSG* msg, UINT lo, UINT hi)
{
	if (!root || !msg) return FALSE;
	MSG p = {};
	if (!::PeekMessage(&p, NULL, lo, hi, PM_NOREMOVE)) return FALSE;
	if (p.message == WM_QUIT) {
		if (!::PeekMessage(msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) return FALSE;
		return TRUE;
	}
	if (!p.hwnd) return FALSE;
	DWORD pid = 0;
	::GetWindowThreadProcessId(p.hwnd, &pid);
	if (pid != ::GetCurrentProcessId()) return FALSE;
	const BOOL ours = TacMsgOurs(root, p.hwnd);
	const BOOL input =
		(p.message >= WM_MOUSEFIRST && p.message <= WM_MOUSELAST)
		|| (p.message >= WM_KEYFIRST && p.message <= WM_KEYLAST)
		|| (p.message >= WM_NCMOUSEMOVE && p.message <= WM_NCMBUTTONDBLCLK);
	if (!ours) {
		if (!input) return FALSE;
		if (!::PeekMessage(msg, p.hwnd, p.message, p.message, PM_REMOVE)) return FALSE;
		if (msg->message >= WM_MOUSEFIRST && msg->message <= WM_MOUSELAST) {
			POINT pt = { (short)LOWORD(msg->lParam), (short)HIWORD(msg->lParam) };
			::ClientToScreen(msg->hwnd, &pt);
			::ScreenToClient(root, &pt);
			msg->lParam = MAKELPARAM((WORD)pt.x, (WORD)pt.y);
		}
		msg->hwnd = root;
		return TRUE;
	}
	return ::PeekMessage(msg, p.hwnd, p.message, p.message, PM_REMOVE) ? TRUE : FALSE;
}

void COmakeTacticsDlg::DrainInput()
{
	if (m_destroying) return;
	HWND root = GetSafeHwnd();
	if (!root) return;
	TacDropHostNoise(root);
	const UINT range[][2] = {
		{ WM_MOUSEFIRST, WM_MOUSELAST },
		{ WM_KEYFIRST, WM_IME_COMPOSITION },
		{ WM_IME_SETCONTEXT, WM_IME_KEYUP },
		{ WM_NCMOUSEMOVE, WM_NCMBUTTONDBLCLK },
		{ WM_SYSCOMMAND, WM_SYSCOMMAND },
		{ WM_CLOSE, WM_CLOSE },
	};
	MSG msg;
	int n = 0;
	for (int pass = 0; pass < (int)(sizeof(range) / sizeof(range[0])) && n < 64; pass++) {
		while (n < 64 && TacPeekInput(root, &msg, range[pass][0], range[pass][1])) {
			if (m_destroying || !GetSafeHwnd()) return;
			if (msg.message == WM_QUIT) {
				::PostQuitMessage((int)msg.wParam);
				return;
			}
			if (msg.message == WM_SYSCOMMAND || msg.message == WM_CLOSE) {
				if (!TacMsgOurs(root, msg.hwnd))
					continue;
			}
			if (!PreTranslateMessage(&msg)) {
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
			if (m_destroying || !GetSafeHwnd()) return;
			n++;
		}
	}
	TacDropHostNoise(root);
	TacClearWaitCursor();
}

LRESULT COmakeTacticsDlg::OnTacPace(WPARAM, LPARAM)
{
	InterlockedExchange(&m_pacePosted, 0);
	if (m_paceDone && !m_inPace) SetEvent(m_paceDone);
	if (m_destroying || !m_gameOk || !GetSafeHwnd()) return 0;
	if (m_paceStop && WaitForSingleObject(m_paceStop, 0) == WAIT_OBJECT_0) return 0;
	if (s_tacOpenDepth > 0 || m_inPace) return 0;
	if (m_sizing || IsIconic() || !IsWindowVisible()) return 2;
	// 描画の前にホストのキューを最大 24 個さばくと、その時間が拍の外に足され、
	// 1/72 なら 13.9ms に描画分が乗って 50fps 前後で GPU が空く。ここでは終了だけ見る。
	{
		MSG msg;
		if (::PeekMessage(&msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {
			::PostQuitMessage((int)msg.wParam);
			return 0;
		}
		if (::PeekMessage(&msg, m_hWnd, WM_TAC_DESTROY, WM_TAC_DESTROY, PM_REMOVE)) {
			::PostMessage(m_hWnd, WM_TAC_DESTROY, msg.wParam, msg.lParam);
			return 0;
		}
		while (::PeekMessage(&msg, m_hWnd, WM_TAC_PACE, WM_TAC_PACE, PM_REMOVE))
			InterlockedExchange(&m_pacePosted, 0);
	}
	RunIdleFrame();
	return 1;
}

BOOL COmakeTacticsDlg::RunIdleFrame()
{
	if (s_tacOpenDepth > 0 || m_destroying || m_inPace || !m_gameOk || m_sizing) return FALSE;
	if (!GetSafeHwnd() || IsIconic() || !IsWindowVisible()) return FALSE;
	static int windowCheckFlag = 0;
	m_inPace = TRUE;
	m_game.BeginPaceLoop();
	DrainInput();
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	float dt = m_qpf.QuadPart ? (float)((double)(now.QuadPart - m_tickAt.QuadPart) / (double)m_qpf.QuadPart) : .016f;
	if (dt < 0) dt = 0;
	if (dt > 0.1f) dt = 0.1f;
	m_tickAt = now;
	int hz = m_game.PaceHz();
	if (hz < 15) hz = 15;
	m_paceHz = hz;
	{
		HWND fg = ::GetForegroundWindow();
		const BOOL mine = (fg == m_hWnd) || TacInFamily(fg, m_hWnd);
		if (mine) {
			m_fg = TRUE;
			s_paceArmed = 1;
		} else if (!s_paceArmed || TacKeepFg()) {
			m_fg = TRUE;
			if (windowCheckFlag == 0) TacRaiseOnce(m_hWnd);
		} else {
			DWORD pid = 0;
			if (fg) ::GetWindowThreadProcessId(fg, &pid);
			m_fg = (pid == ::GetCurrentProcessId());
		}
	}
	m_game.SetPaceFg(m_fg);
	if (m_game.PastLoad()) {
		const DWORD now = ::GetTickCount();
		if (!s_frontUntil) s_frontUntil = now + 12000;
		if (!s_holdAfterLoad) {
			s_holdAfterLoad = 1;
			if ((int)(s_holdFgUntil - now) < 12000) s_holdFgUntil = now + 12000;
		}
		if (TacKeepFg()) {
			if (windowCheckFlag == 0) TacParkHostUi(m_hWnd);
			HWND foc = ::GetFocus();
			if (!foc || !TacMsgOurs(m_hWnd, foc))
				::SetFocus(m_hWnd);
		}
	}
	if (!s_frontUsed && s_frontUntil && ::GetTickCount() < s_frontUntil && !m_fg) {
		HWND fg = ::GetForegroundWindow();
		DWORD pid = 0;
		if (fg && !TacInFamily(fg, m_hWnd))
			::GetWindowThreadProcessId(fg, &pid);
		const SHORT click = (SHORT)(::GetAsyncKeyState(VK_LBUTTON) | ::GetAsyncKeyState(VK_RBUTTON) | ::GetAsyncKeyState(VK_MBUTTON));
		if (pid == ::GetCurrentProcessId() && (click & 0x8000) == 0) {
			s_frontUsed = 1;
			TacRaiseOnce(m_hWnd);
			m_fg = TRUE;
			m_game.SetPaceFg(TRUE);
		}
	}
	if (windowCheckFlag == 0) {
		if (!m_fg) SyncBorderlessZ();
		{
			static int pri = -1;
			int want = m_fg ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL;
			if (pri != want) {
				SetThreadPriority(GetCurrentThread(), want);
				if (m_paceThread) SetThreadPriority(m_paceThread, want);
				pri = want;
			}
		}
	}
	windowCheckFlag++;
	windowCheckFlag = windowCheckFlag % 20;
	if (!TacAudioOnThread()) TacAudioPump();
	PollPad(dt);
	PullImeToGame();
	m_game.Tick(dt);
	m_clientLive = TRUE;
	SyncImeEdit();
	ApplyWinMode();
	PumpPace();
	m_inPace = FALSE;
	if (m_game.WantsClose()) {
		m_game.ClearClose();
		RequestDestroyWindow();
		return FALSE;
	}
	return TRUE;
}

BOOL OmakeTacticsIdleFrame()
{
	if (!g_tac) return FALSE;
	return g_tac->RunIdleFrame();
}

void COmakeTacticsDlg::OnSize(UINT nType, int cx, int cy)
{
	CFrameWnd::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED) return;
	if (m_sizing) {
		LayoutView();
		return;
	}
	LayoutView();
	StoreWindowRect();
}

void COmakeTacticsDlg::OnMove(int, int)
{
	if (IsIconic()) return;
	LayoutView();
}

void COmakeTacticsDlg::OnEnterSizeMove()
{
	m_sizing = TRUE;
	CWnd::OnEnterSizeMove();
}

void COmakeTacticsDlg::OnExitSizeMove()
{
	m_sizing = FALSE;
	CWnd::OnExitSizeMove();
	LayoutView();
	StoreWindowRect();
	QueryPerformanceCounter(&m_tickAt);
	KickPace();
}

LRESULT COmakeTacticsDlg::OnNcHitTest(CPoint pt)
{
	const LRESULT hit = CFrameWnd::OnNcHitTest(pt);
	return TacFixNcHit(m_hWnd, MAKELPARAM((UINT)(short)pt.x, (UINT)(short)pt.y), hit);
}

void COmakeTacticsDlg::Down(int btn, CPoint pt)
{
	if (!m_btnMask) SetCapture();
	m_btnMask |= 1 << btn;
	if (m_gameOk) m_game.OnMouseDown(btn, pt.x, pt.y);
}

void COmakeTacticsDlg::Up(int btn, CPoint pt)
{
	const int had = m_btnMask & (1 << btn);
	m_btnMask &= ~(1 << btn);
	if (!m_btnMask && GetCapture() == this) ReleaseCapture();
	if (had && m_gameOk) m_game.OnMouseUp(btn, pt.x, pt.y);
	if (had && m_gameOk && m_game.WantsTextInput())
		EnsureImeFocus();
}

void COmakeTacticsDlg::ReleaseButtons()
{
	m_btnMask = 0;
	if (GetCapture() == this) ReleaseCapture();
}

void COmakeTacticsDlg::OnMouseMove(UINT, CPoint pt)
{
	if (m_gameOk) m_game.OnMouseMove(pt.x, pt.y);
}

void COmakeTacticsDlg::OnLButtonDown(UINT, CPoint pt) { Down(0, pt); }
void COmakeTacticsDlg::OnLButtonUp(UINT, CPoint pt) { Up(0, pt); }
void COmakeTacticsDlg::OnLButtonDblClk(UINT, CPoint pt) { Down(0, pt); }
void COmakeTacticsDlg::OnRButtonDown(UINT, CPoint pt) { Down(1, pt); }
void COmakeTacticsDlg::OnRButtonUp(UINT, CPoint pt) { Up(1, pt); }
void COmakeTacticsDlg::OnMButtonDown(UINT, CPoint pt) { Down(2, pt); }
void COmakeTacticsDlg::OnMButtonUp(UINT, CPoint pt) { Up(2, pt); }

void COmakeTacticsDlg::OnCaptureChanged(CWnd* pWnd)
{
	if (pWnd != this && m_btnMask && m_gameOk) {
		CPoint pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		for (int b = 0; b < 3; b++)
			if (m_btnMask & (1 << b)) m_game.OnMouseUp(b, pt.x, pt.y);
	}
	m_btnMask = 0;
	CFrameWnd::OnCaptureChanged(pWnd);
}

BOOL COmakeTacticsDlg::OnEraseBkgnd(CDC* pDC)
{
	if (m_clientLive) return TRUE;
	if (!pDC) return TRUE;
	CRect rc;
	GetClientRect(&rc);
	pDC->FillSolidRect(&rc, RGB(8, 10, 16));
	return TRUE;
}

void COmakeTacticsDlg::OnPaint()
{
	if (m_gameOk && m_clientLive) {
		::ValidateRect(m_hWnd, NULL);
		return;
	}
	CPaintDC dc(this);
	if (m_gameOk) {
		CRect rc;
		GetClientRect(&rc);
		dc.FillSolidRect(&rc, RGB(8, 10, 16));
		return;
	}
	CRect rc;
	GetClientRect(&rc);
	dc.FillSolidRect(&rc, RGB(16, 18, 28));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(230, 230, 240));
	dc.DrawText(TT(TX_MISC_DXFAIL), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

BOOL COmakeTacticsDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT) {
		TacClearWaitCursor();
		return TRUE;
	}
	return CFrameWnd::OnSetCursor(pWnd, nHitTest, message);
}

void COmakeTacticsDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CFrameWnd::OnShowWindow(bShow, nStatus);
	if (bShow) {
		QueryPerformanceCounter(&m_tickAt);
		if (m_gameOk) m_game.OnBecameActive();
		KickPace();
	}
}

void COmakeTacticsDlg::OnGetMinMaxInfo(MINMAXINFO* mmi)
{
	CFrameWnd::OnGetMinMaxInfo(mmi);
	mmi->ptMinTrackSize.x = 800;
	mmi->ptMinTrackSize.y = 560;
	if (mmi->ptMaxTrackSize.x < 16000) mmi->ptMaxTrackSize.x = 16000;
	if (mmi->ptMaxTrackSize.y < 16000) mmi->ptMaxTrackSize.y = 16000;
}

void COmakeTacticsDlg::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CFrameWnd::OnActivate(nState, pWndOther, bMinimized);
	const BOOL fg = (nState != WA_INACTIVE && !bMinimized);
	if (fg) {
		s_paceArmed = 1;
		m_fg = TRUE;
		QueryPerformanceCounter(&m_tickAt);
		if (m_gameOk && !m_swapShown) {
			m_swapShown = TRUE;
			LayoutView();
		}
		if (m_gameOk && m_game.WantsTextInput())
			TacFocusIme(m_ime, m_imeCtx);
		else if (GetSafeHwnd())
			SetFocus();
		if (m_gameOk) {
			m_game.SetPaceFg(TRUE);
			m_game.OnBecameActive();
			KickPace();
		}
		TacRaiseOnce(m_hWnd);
	} else if (s_paceArmed && !TacKeepFg()) {
		m_fg = FALSE;
		if (m_gameOk) m_game.SetPaceFg(FALSE);
		ReleaseUiInput();
		SyncBorderlessZ();
	}
}

void COmakeTacticsDlg::OnActivateApp(BOOL bActive, DWORD dwThreadID)
{
	CFrameWnd::OnActivateApp(bActive, dwThreadID);
	if (bActive) {
		s_paceArmed = 1;
		m_fg = TRUE;
		QueryPerformanceCounter(&m_tickAt);
		if (m_gameOk) {
			m_game.SetPaceFg(TRUE);
			m_game.OnBecameActive();
			KickPace();
		}
		TacRaiseOnce(m_hWnd);
	} else if (s_paceArmed && !TacKeepFg()) {
		m_fg = FALSE;
		if (m_gameOk) m_game.SetPaceFg(FALSE);
		ReleaseUiInput();
		SyncBorderlessZ();
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

static void TacReadComp(HWND h, wchar_t* comp, int compN)
{
	if (!comp || compN < 2) return;
	comp[0] = 0;
	if (!h) return;
	HIMC himc = ::ImmGetContext(h);
	if (!himc) return;
	LONG n = ::ImmGetCompositionStringW(himc, GCS_COMPSTR, comp, (DWORD)((compN - 1) * sizeof(wchar_t)));
	if (n < 0) n = 0;
	int c = (int)(n / sizeof(wchar_t));
	if (c < 0) c = 0;
	if (c > compN - 1) c = compN - 1;
	comp[c] = 0;
	::ImmReleaseContext(h, himc);
}

void COmakeTacticsDlg::HideImeEdit()
{
	m_imeKind = 0;
	m_imePush = TRUE;
	if (m_ime.GetSafeHwnd()) m_ime.SetWindowText(L"");
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
	TacReadComp(ih, comp, 48);
	m_game.SetImeComp(comp);
	if (m_game.TextFocus() == 3) {
		m_game.SetSellFromText(t);
		return;
	}
	wchar_t* buf = NULL;
	int maxLen = 0;
	if (!m_game.TextLive(buf, maxLen) || !buf || maxLen < 2) return;
	wchar_t prev[32];
	wcsncpy_s(prev, buf, _TRUNCATE);
	wcsncpy_s(buf, maxLen, t, _TRUNCATE);
	m_game.SetTextCaret(caret);
	if ((m_game.TextFocus() == 4 || m_game.TextFocus() == 5) && wcscmp(prev, buf) != 0)
		m_game.NotifyFindChanged();
}

void COmakeTacticsDlg::PlaceImeWindow()
{
	if (!m_gameOk || !m_ime.GetSafeHwnd()) return;
	static LONG s_imePosBusy = 0;
	if (InterlockedCompareExchange(&s_imePosBusy, 1, 0) != 0) return;
	RECT box = {};
	m_game.TextBoxRect(box);
	int caretPx = (int)(m_game.TextCaretPx() + .5f);
	if (caretPx < 0) caretPx = 0;
	// キャラ名と同じ 1px。表示は HUD が描く。広い編集欄はスワップチェインに隠れて文字が見えなくなる。
	int left = box.left + caretPx;
	int w = 1;
	int h = box.bottom - box.top;
	if (h < 8) h = 8;
	int fontPx = (int)(m_game.TextFontPx() + .5f);
	if (fontPx < 10) fontPx = 10;
	if (fontPx > 48) fontPx = 48;
	if (fontPx != m_imeFontPx) {
		CFont neu;
		if (neu.CreateFont(-fontPx, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI")) {
			m_ime.SetFont(&neu);
			m_imeFont.DeleteObject();
			m_imeFont.Attach(neu.Detach());
			m_imeFontPx = fontPx;
		}
	}
	HWND ih = m_ime.GetSafeHwnd();
	if (!::IsWindowEnabled(ih)) ::EnableWindow(ih, TRUE);
	const BOOL busy = TacImeBusy(ih);
	static int sx = -99999, sy = 0, sw = 0, sh = 0;
	if (!m_ime.IsWindowVisible()) sx = -99999;
	const BOOL same = (left == sx && box.top == sy && w == sw && h == sh);
	if (!same && !busy) {
		m_ime.SetWindowPos(&wndTop, left, box.top, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
		sx = left; sy = box.top; sw = w; sh = h;
	} else if (!m_ime.IsWindowVisible()) {
		m_ime.ShowWindow(SW_SHOWNA);
	}
	HIMC himc = ::ImmGetContext(ih);
	if (himc && !busy) {
		COMPOSITIONFORM cf = {};
		cf.dwStyle = CFS_POINT;
		cf.ptCurrentPos.x = 0;
		cf.ptCurrentPos.y = 0;
		::ImmSetCompositionWindow(himc, &cf);
		LOGFONTW lf = {};
		if (m_imeFont.GetSafeHandle()) m_imeFont.GetLogFont(&lf);
		if (lf.lfFaceName[0]) ::ImmSetCompositionFontW(himc, &lf);
		CANDIDATEFORM cand = {};
		cand.dwIndex = 0;
		cand.dwStyle = CFS_CANDIDATEPOS;
		cand.ptCurrentPos.x = 0;
		cand.ptCurrentPos.y = h;
		::ImmSetCandidateWindow(himc, &cand);
		::ImmReleaseContext(ih, himc);
	} else if (himc) {
		::ImmReleaseContext(ih, himc);
	}
	InterlockedExchange(&s_imePosBusy, 0);
}

void COmakeTacticsDlg::SyncImeEdit()
{
	if (!m_gameOk || m_destroying || !m_ime.GetSafeHwnd()) return;
	const int focus = m_game.TextFocus();
	const BOOL reload = m_game.TakeImeReload();
	if (!focus) {
		if (m_imeKind) HideImeEdit();
		return;
	}
	if (!m_ime.IsWindowEnabled()) m_ime.EnableWindow(TRUE);
	wchar_t want[64];
	want[0] = 0;
	int limit = 15;
	if (focus == 3) {
		m_game.SellToText(want, 64);
		limit = 8;
	} else {
		wchar_t* buf = NULL;
		int maxLen = 0;
		if (m_game.TextLive(buf, maxLen) && buf) {
			wcsncpy_s(want, buf, _TRUNCATE);
			limit = maxLen > 1 ? maxLen - 1 : 1;
		}
	}
	if (focus != m_imeKind || reload) {
		m_imePush = TRUE;
		m_ime.SetLimitText(limit);
		m_ime.SetWindowText(want);
		int n = (int)wcslen(want);
		if (focus == 3) m_ime.SetSel(0, -1);
		else m_ime.SetSel(n, n);
		m_game.SetTextCaret(n);
		m_imeKind = focus;
		m_imePush = FALSE;
	}
	PlaceImeWindow();
	TacFocusIme(m_ime, m_imeCtx);
	if (!m_imePush) PullImeToGame();
}

void COmakeTacticsDlg::OnImeChange()
{
	if (!m_imePush) PullImeToGame();
}

void COmakeTacticsDlg::OnImeSetFocus()
{
}

void COmakeTacticsDlg::EnsureImeFocus()
{
	if (m_destroying || !m_gameOk) return;
	if (!m_ime.GetSafeHwnd() || !m_game.WantsTextInput()) return;
	if (::GetForegroundWindow() != m_hWnd) SetForegroundWindow();
	SyncImeEdit();
}

HBRUSH COmakeTacticsDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (pWnd == &m_ime) {
		pDC->SetBkMode(OPAQUE);
		pDC->SetBkColor(RGB(40, 46, 80));
		pDC->SetTextColor(RGB(255, 255, 255));
		return (HBRUSH)m_imeBrush.GetSafeHandle();
	}
	return (HBRUSH)::GetStockObject(BLACK_BRUSH);
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
	const BOOL ours = (fg == m_hWnd || (fg && ::IsChild(m_hWnd, fg)) || TacKeepFg());
	HWND imeHw = m_ime.GetSafeHwnd();
	if (!ours && pMsg->hwnd != m_hWnd && !::IsChild(m_hWnd, pMsg->hwnd)
		&& !(imeHw && pMsg->hwnd == imeHw))
		return FALSE;
	// Alt+F4 / Alt+Space / Alt+Esc はゲームに渡さず、システムの閉じる・メニューへ戻す。
	if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_SYSCHAR)
		return FALSE;

	const BOOL typing = m_gameOk && (m_game.WantsTextInput() || (imeHw && ::GetFocus() == imeHw));
	if (typing) {
		const UINT vk = TacRealVk(pMsg);
		if (msg == WM_KEYDOWN && (vk == VK_BACK || vk == VK_DELETE)) m_imeAllowClear = TRUE;
		if ((msg == WM_CHAR || msg == WM_IME_CHAR) && (pMsg->wParam == 8 || pMsg->wParam == 0x7F))
			m_imeAllowClear = TRUE;
		const BOOL composing = TacImeBusy(imeHw);
		if (msg == WM_KEYDOWN && (vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_TAB) && !composing) {
			PullImeToGame();
			if (m_gameOk) m_game.OnKey((int)vk, TRUE);
			if (!m_gameOk || !m_game.WantsTextInput()) HideImeEdit();
			else TacFocusIme(m_ime, m_imeCtx);
			m_imeAllowClear = FALSE;
			return TRUE;
		}
		TacFocusIme(m_ime, m_imeCtx);
		CString before;
		if (imeHw) m_ime.GetWindowText(before);
		MSG m2 = *pMsg;
		if (imeHw) m2.hwnd = imeHw;
		if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) ::TranslateMessage(&m2);
		::DispatchMessage(&m2);
		CString after;
		if (imeHw) m_ime.GetWindowText(after);
		if (m_gameOk && after == before && !composing && (msg == WM_CHAR || msg == WM_IME_CHAR)) {
			wchar_t ch = (wchar_t)pMsg->wParam;
			if (ch == 8 || ch >= 32) m_game.OnChar(ch);
			wchar_t* buf = NULL;
			int maxLen = 0;
			if (m_game.TextLive(buf, maxLen) && buf && imeHw) {
				m_imePush = TRUE;
				m_ime.SetWindowText(buf);
				int caret = m_game.TextCaret();
				m_ime.SetSel(caret, caret);
				m_imePush = FALSE;
			}
		} else {
			PullImeToGame();
		}
		m_imeAllowClear = FALSE;
		return TRUE;
	}

	if (imeMsg)
		return FALSE;
	if (msg == WM_CHAR || msg == WM_IME_CHAR || msg == WM_UNICHAR || msg == WM_DEADCHAR)
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
	return CFrameWnd::PreTranslateMessage(pMsg);
}

void COmakeTacticsDlg::OnDestroy()
{
	m_destroying = TRUE;
	HideImeEdit();
	if (m_imeCtx) {
		if (m_ime.GetSafeHwnd()) ::ImmAssociateContext(m_ime.GetSafeHwnd(), NULL);
		::ImmDestroyContext((HIMC)m_imeCtx);
		m_imeCtx = nullptr;
	}
	ReleaseUiInput();
	TacHoldDisplay(FALSE);
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
	CFrameWnd::OnDestroy();
	TacUnparkHostUi();
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SetTimer(4923, 20, NULL);
}

void COmakeTacticsDlg::RequestDestroyWindow()
{
	if (m_destroying) return;
	m_destroying = TRUE;
	if (m_gameOk && m_game.Exclusive()) m_game.SetExclusive(FALSE);
	HideImeEdit();
	if (GetSafeHwnd()) {
		HWND parent = ::GetParent(m_hWnd);
		::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		ShowWindow(SW_HIDE);
		TacUnparkHostUi();
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
		otac::TacShopSerReset();
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
	CFrameWnd::OnSysCommand(nID, lParam);
}

void COmakeTacticsDlg::PostNcDestroy()
{
	if (g_tac == this) g_tac = nullptr;
	CFrameWnd::PostNcDestroy();
}

// ---------------------------------------------------------------------------
// 公開 API
// ---------------------------------------------------------------------------
void OpenOmakeTacticsModeless(CWnd* p)
{
	(void)p;
	if (g_tac && g_tac->GetSafeHwnd()) {
		if (g_tac->Destroying()) return;
		if (g_tac->IsIconic()) g_tac->ShowWindow(SW_RESTORE);
		TacRaiseOnce(g_tac->GetSafeHwnd());
		return;
	}
	extern void CloseSoft3DRaceIfOpen();
	extern void CloseSoft3DMazeIfOpen();
	CloseSoft3DRaceIfOpen();
	CloseSoft3DMazeIfOpen();
	if (!EnsureBonusImages(2)) return;
	s_tacStarting = 1;
	s_paceArmed = 0;
	// ボタン処理の途中で Peek+Dispatch すると終了や再入が混ざる。隠すだけしてキューは触らない。
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->ParkHostUiForTactics(NULL);
	s_tacOpenDepth++;
	g_tac = new COmakeTacticsDlg();
	if (!g_tac->CreateFrame()) {
		delete g_tac;
		g_tac = nullptr;
		s_tacOpenDepth--;
		s_tacStarting = 0;
		if (og && ::IsWindow(og->GetSafeHwnd()))
			og->UnparkHostUiAfterTactics();
		return;
	}
	if (g_tac->IsWindowVisible()) g_tac->ShowWindow(SW_HIDE);
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->ParkHostUiForTactics(g_tac->GetSafeHwnd());
	g_tac->ShowWindow(SW_SHOW);
	TacRaiseOnce(g_tac->GetSafeHwnd());
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		::UnregisterHotKey(og->GetSafeHwnd(), 8000);
		::UnregisterHotKey(og->GetSafeHwnd(), 8001);
		::UnregisterHotKey(og->GetSafeHwnd(), 8002);
		::UnregisterHotKey(og->GetSafeHwnd(), 8003);
	}
	s_tacOpenDepth--;
	g_tac->StartShown();
	s_tacStarting = 0;
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
	if (s_tacStarting) return TRUE;
	return (g_tac && g_tac->GetSafeHwnd() && ::IsWindow(g_tac->GetSafeHwnd()) && g_tac->IsWindowVisible()) ? TRUE : FALSE;
}

BOOL IsOmakeTacticsActive()
{
	return IsOmakeTacticsOpen();
}

BOOL OmakeTacticsPreTranslate(MSG* pMsg)
{
	if (!IsOmakeTacticsOpen() || !pMsg || !g_tac) return FALSE;
	HWND tac = g_tac->GetSafeHwnd();
	if (pMsg->message == WM_TIMER && pMsg->hwnd && tac
		&& pMsg->hwnd != tac && !::IsChild(tac, pMsg->hwnd))
		return TRUE;
	if (TacTakeWheel(g_tac, pMsg)) return TRUE;
	return g_tac->HandleKeyMessage(pMsg);
}
