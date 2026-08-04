#include "StdAfx.h"
#include "CDesktopLyricsWnd.h"
#include "oggDlg.h"

extern save savedata;
extern COggDlg* og;
extern void MpPersistSavedataQuick();

static CDesktopLyricsWnd* g_desktopLyricsWnd = NULL;

IMPLEMENT_DYNAMIC(CDesktopLyricsWnd, CCustomBlurDialogBase)

BEGIN_MESSAGE_MAP(CDesktopLyricsWnd, CCustomBlurDialogBase)
	ON_WM_SIZE()
	ON_WM_MOVING()
	ON_WM_HSCROLL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_DESTROY()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CDesktopLyricsWnd::CDesktopLyricsWnd(CWnd* pParent)
	: CCustomBlurDialogBase(CDesktopLyricsWnd::IDD, pParent)
	, m_dragLyrics(FALSE)
{
}

CDesktopLyricsWnd::~CDesktopLyricsWnd()
{
}

void CDesktopLyricsWnd::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_DLRC_ALPHA, m_alpha);
	DDX_Control(pDX, IDC_DLRC_ALPHA_L, m_alphaL);
	DDX_Control(pDX, IDCANCEL, m_close);
}

BOOL CDesktopLyricsWnd::PreCreateWindow(CREATESTRUCT& cs)
{
	cs.dwExStyle |= WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
	return CCustomBlurDialogBase::PreCreateWindow(cs);
}

BOOL CDesktopLyricsWnd::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(
		L"デスクトップ歌詞", L"Desktop lyrics", L"Paroles bureau", L"Testi desktop", L"Letra escritorio",
		L"데스크톱 가사", L"桌面歌词", L"كلمات سطح المكتب", L"Текст на рабочем столе", L"Desktop-Text",
		L"Letra na area de trabalho", L"Bureaublad songtekst", L"Tekst na pulpicie", L"Masaustu sozleri"));

	if (m_alphaL.GetSafeHwnd())
		m_alphaL.SetWindowText(LL14(
			L"透明度", L"Opacity", L"Opacite", L"Opacita", L"Opacidad",
			L"불투명도", L"不透明度", L"الشفافية", L"Непрозрачность", L"Deckkraft",
			L"Opacidade", L"Dekking", L"Nieprzezroczystosc", L"Opaklik"));

	int x = savedata.deskLrcX;
	int y = savedata.deskLrcY;
	int w = savedata.deskLrcW;
	int h = savedata.deskLrcH;
	if (w < 200) w = 640;
	if (h < 80) h = 160;

	ModifyStyleEx(0, WS_EX_LAYERED);
	ApplyWindowAlpha();

	SetWindowPos(&wndTopMost, x, y, w, h, SWP_SHOWWINDOW);

	if (!m_view.GetSafeHwnd()) {
		CRect rc;
		GetClientRect(&rc);
		m_view.Create(this, kViewChildId);
	}
	m_view.EnsureFonts(100, _T("Segoe UI"));
	LayoutClient();

	m_alpha.SetRange(40, 255, TRUE);
	m_alpha.SetTicFreq(32);
	int a = savedata.deskLrcAlpha;
	if (a < 40) a = 40;
	if (a > 255) a = 255;
	m_alpha.SetPos(a);

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
	if (m_alpha.GetSafeHwnd()) {
		m_tooltip.AddTool(&m_alpha, LL14(
			L"ウィンドウ全体の透明度", L"Overall window opacity", L"Opacite de la fenetre", L"Opacita finestra", L"Opacidad de ventana",
			L"창 전체 불투명도", L"窗口整体不透明度", L"شفافية النافذة", L"Прозрачность окна", L"Fensterdeckkraft",
			L"Opacidade da janela", L"Dekking van venster", L"Nieprzezroczystosc okna", L"Pencere opakligi"));
	}
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 320, 8000);

	if (m_close.GetSafeHwnd())
		m_close.SetWindowText(LL14(
			L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
			L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen",
			L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));

	savedata.deskLrcOn = 1;
	MpPersistSavedataQuick();
	SyncFromOg();
	return TRUE;
}

void CDesktopLyricsWnd::ApplyWindowAlpha()
{
	int a = savedata.deskLrcAlpha;
	if (a < 40) a = 40;
	if (a > 255) a = 255;
	::SetLayeredWindowAttributes(m_hWnd, 0, (BYTE)a, LWA_ALPHA);
}

void CDesktopLyricsWnd::PersistGeometry()
{
	if (!m_hWnd) return;
	CRect r;
	GetWindowRect(&r);
	savedata.deskLrcX = r.left;
	savedata.deskLrcY = r.top;
	savedata.deskLrcW = r.Width();
	savedata.deskLrcH = r.Height();
	MpPersistSavedataQuick();
}

void CDesktopLyricsWnd::LayoutClient()
{
	CRect rc;
	GetClientRect(&rc);
	const int footer = 28;
	CRect viewRc = rc;
	if (viewRc.Height() > footer)
		viewRc.bottom -= footer;
	if (m_view.GetSafeHwnd())
		m_view.MoveWindow(&viewRc, FALSE);

	if (m_alphaL.GetSafeHwnd())
		m_alphaL.SetWindowPos(NULL, 8, rc.bottom - 22, 52, 14, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_alpha.GetSafeHwnd())
		m_alpha.SetWindowPos(NULL, 62, rc.bottom - 24, rc.Width() - 130, 18, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_close.GetSafeHwnd())
		m_close.SetWindowPos(NULL, rc.right - 58, rc.bottom - 24, 50, 18, SWP_NOZORDER | SWP_NOACTIVATE);
}

void CDesktopLyricsWnd::SyncFromOg()
{
	if (!m_view.GetSafeHwnd()) return;
	if (!og || og->lrcnum < 2) {
		m_view.Clear();
		return;
	}
	const int n = og->lrcnum - 1;
	m_view.SetLines(og->lrc, n > 0 ? n : 0, og->lrctm, og->lrcnum);
	extern UINT ttt;
	extern double OggGetGdiPlaybackTimeSec();
	extern int mode;
	extern int videoonly;
	DWORD centis = ttt;
	if (!(mode == -2 || videoonly)) {
		const double sec = OggGetGdiPlaybackTimeSec();
		if (sec >= 0.0)
			centis = (DWORD)(sec * 100.0 + 0.5);
	}
	m_view.SetPlayCentis(centis);
}

void CDesktopLyricsWnd::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	LayoutClient();
	if (nType != SIZE_MINIMIZED)
		PersistGeometry();
}

void CDesktopLyricsWnd::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogBase::OnMoving(fwSide, pRect);
	if (pRect) {
		savedata.deskLrcX = pRect->left;
		savedata.deskLrcY = pRect->top;
		savedata.deskLrcW = pRect->right - pRect->left;
		savedata.deskLrcH = pRect->bottom - pRect->top;
	}
}

void CDesktopLyricsWnd::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (pScrollBar && pScrollBar->GetSafeHwnd() == m_alpha.GetSafeHwnd()) {
		int a = m_alpha.GetPos();
		if (a < 40) { a = 40; m_alpha.SetPos(a); }
		savedata.deskLrcAlpha = a;
		ApplyWindowAlpha();
		MpPersistSavedataQuick();
	}
	CCustomBlurDialogBase::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CDesktopLyricsWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	CWnd* hit = ChildWindowFromPoint(point, CWP_SKIPINVISIBLE);
	if (hit == &m_view || hit == this) {
		m_dragLyrics = TRUE;
		m_dragOff = point;
		ClientToScreen(&m_dragOff);
		CRect wr; GetWindowRect(&wr);
		m_dragOff.x -= wr.left;
		m_dragOff.y -= wr.top;
		SetCapture();
		return;
	}
	CCustomBlurDialogBase::OnLButtonDown(nFlags, point);
}

void CDesktopLyricsWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_dragLyrics) {
		m_dragLyrics = FALSE;
		if (GetCapture() == this)
			ReleaseCapture();
		PersistGeometry();
	}
	CCustomBlurDialogBase::OnLButtonUp(nFlags, point);
}

void CDesktopLyricsWnd::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_dragLyrics && (nFlags & MK_LBUTTON)) {
		CPoint sp = point;
		ClientToScreen(&sp);
		SetWindowPos(&wndTopMost, sp.x - m_dragOff.x, sp.y - m_dragOff.y, 0, 0,
			SWP_NOSIZE | SWP_NOACTIVATE);
		return;
	}
	CCustomBlurDialogBase::OnMouseMove(nFlags, point);
}

void CDesktopLyricsWnd::OnDestroy()
{
	PersistGeometry();
	savedata.deskLrcOn = 0;
	MpPersistSavedataQuick();
	CCustomBlurDialogBase::OnDestroy();
}

BOOL CDesktopLyricsWnd::OnEraseBkgnd(CDC* pDC)
{
	UNREFERENCED_PARAMETER(pDC);
	return TRUE;
}

void CDesktopLyricsWnd::OnCancel()
{
	DestroyWindow();
}

void CDesktopLyricsWnd::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_desktopLyricsWnd == this)
		g_desktopLyricsWnd = NULL;
	delete this;
}

void OpenDesktopLyricsModeless(CWnd* pParent)
{
	if (g_desktopLyricsWnd && ::IsWindow(g_desktopLyricsWnd->GetSafeHwnd())) {
		g_desktopLyricsWnd->ShowWindow(SW_SHOW);
		g_desktopLyricsWnd->SetForegroundWindow();
		g_desktopLyricsWnd->SyncFromOg();
		savedata.deskLrcOn = 1;
		MpPersistSavedataQuick();
		return;
	}
	CWnd* parent = pParent;
	if (!parent || !::IsWindow(parent->GetSafeHwnd()))
		parent = NULL;
	g_desktopLyricsWnd = new CDesktopLyricsWnd(parent);
	if (!g_desktopLyricsWnd->Create(CDesktopLyricsWnd::IDD, parent)) {
		delete g_desktopLyricsWnd;
		g_desktopLyricsWnd = NULL;
		return;
	}
	g_desktopLyricsWnd->ShowWindow(SW_SHOW);
	g_desktopLyricsWnd->SetForegroundWindow();
}

void CloseDesktopLyricsIfOpen()
{
	if (g_desktopLyricsWnd && ::IsWindow(g_desktopLyricsWnd->GetSafeHwnd()))
		g_desktopLyricsWnd->DestroyWindow();
}

void SyncDesktopLyricsIfOpen()
{
	if (g_desktopLyricsWnd && ::IsWindow(g_desktopLyricsWnd->GetSafeHwnd()))
		g_desktopLyricsWnd->SyncFromOg();
}
