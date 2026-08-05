#include "StdAfx.h"
#include "CDesktopLyricsWnd.h"
#include "CCustomPopupMenu.h"
#include "oggDlg.h"

extern save savedata;
extern COggDlg* og;
extern void MpPersistSavedataQuick();

static CDesktopLyricsWnd* g_desktopLyricsWnd = NULL;

enum {
	ID_DLRC_ALPHA_120 = 41001,
	ID_DLRC_ALPHA_160 = 41002,
	ID_DLRC_ALPHA_200 = 41003,
	ID_DLRC_ALPHA_230 = 41004,
	ID_DLRC_ALPHA_255 = 41005,
	ID_DLRC_TOPMOST = 41006,
	ID_DLRC_COPY = 41007,
	ID_DLRC_CLOSE = 41008,
	ID_DLRC_ALPHA_SLIDER = 41009,
	ID_DLRC_ALPHA_PROG = 41010,
	ID_DLRC_ALPHA_RESET = 41011,
	ID_DLRC_FONT_AUTO = 41012,
	ID_DLRC_FONT_SLIDER = 41013,
	ID_DLRC_FONT_S = 41014,
	ID_DLRC_FONT_M = 41015,
	ID_DLRC_FONT_L = 41016,
	ID_DLRC_FONT_XL = 41017,
	ID_DLRC_FONT_XXL = 41018,
	ID_DLRC_LINES_SLIDER = 41019
};

struct DeskLrcMenuCtx {
	CDesktopLyricsWnd* wnd;
	CCustomPopupMenu* menu;
};

static void DeskLrcAlphaSliderCb(void* ctx, int value)
{
	DeskLrcMenuCtx* c = (DeskLrcMenuCtx*)ctx;
	if (!c || !c->wnd || !::IsWindow(c->wnd->GetSafeHwnd())) return;
	c->wnd->SetDeskLrcAlpha(value, TRUE);
	if (c->menu) c->menu->SetProgressPos(ID_DLRC_ALPHA_PROG, value);
}

static void DeskLrcAlphaResetBtnCb(void* ctx, UINT /*id*/)
{
	DeskLrcMenuCtx* c = (DeskLrcMenuCtx*)ctx;
	if (!c || !c->wnd || !::IsWindow(c->wnd->GetSafeHwnd())) return;
	c->wnd->SetDeskLrcAlpha(200, TRUE);
	if (c->menu) {
		c->menu->SetSliderPos(ID_DLRC_ALPHA_SLIDER, 200);
		c->menu->SetProgressPos(ID_DLRC_ALPHA_PROG, 200);
	}
}

static void DeskLrcFontSliderCb(void* ctx, int value)
{
	DeskLrcMenuCtx* c = (DeskLrcMenuCtx*)ctx;
	if (!c || !c->wnd || !::IsWindow(c->wnd->GetSafeHwnd())) return;
	// スライダーはポイント(8..48)。CreatePointFont は×10
	c->wnd->SetDeskLrcFont(value * 10, FALSE);
}

static void DeskLrcLinesSliderCb(void* ctx, int value)
{
	DeskLrcMenuCtx* c = (DeskLrcMenuCtx*)ctx;
	if (!c || !c->wnd || !::IsWindow(c->wnd->GetSafeHwnd())) return;
	c->wnd->SetDeskLrcLines(value);
}

IMPLEMENT_DYNAMIC(CDesktopLyricsWnd, CCustomBlurDialogBase)

BEGIN_MESSAGE_MAP(CDesktopLyricsWnd, CCustomBlurDialogBase)
	ON_WM_SIZE()
	ON_WM_MOVING()
	ON_WM_HSCROLL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_RBUTTONUP()
	ON_WM_CONTEXTMENU()
	ON_WM_DESTROY()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_SHOWWINDOW()
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
		L"歌詞ウィンドウ", L"Lyrics window", L"Fenetre de paroles", L"Finestra testi", L"Ventana de letra",
		L"가사 창", L"歌词窗口", L"نافذة الكلمات", L"Окно текста", L"Textfenster",
		L"Janela de letra", L"Songtekstvenster", L"Okno tekstu", L"Soz penceresi"));

	if (m_alphaL.GetSafeHwnd())
		m_alphaL.SetWindowText(LL14(
			L"不透明度", L"Opacity", L"Opacite", L"Opacita", L"Opacidad",
			L"불투명도", L"不透明度", L"الشفافية", L"Непрозрачность", L"Deckkraft",
			L"Opacidade", L"Dekking", L"Nieprzezroczystosc", L"Opaklik"));

	int x = savedata.deskLrcX;
	int y = savedata.deskLrcY;
	int w = savedata.deskLrcW;
	int h = savedata.deskLrcH;
	if (w < 200) w = 640;
	if (h < 80) h = 160;
	// 誤ってメイン画面サイズが保存されていると全面透過に見える
	if (w > 1600) w = 800;
	if (h > 900) h = 360;

	// 本文ガラスを切る（キャプション帯アクリルも deskLrc では無効）
	MakeSolidClient();
	ApplyWindowAlpha();

	SetWindowPos(&wndTopMost, x, y, w, h, SWP_SHOWWINDOW);

	if (!m_view.GetSafeHwnd()) {
		CRect rc;
		GetClientRect(&rc);
		if (!m_view.Create(this, kViewChildId)) {
			// create fail: still show chrome
		}
	}
	if (m_view.GetSafeHwnd()) {
		m_view.ModifyStyle(0, WS_VISIBLE | WS_CLIPSIBLINGS, 0);
		m_view.SetOverlayStyle(TRUE);
	}
	LayoutClient();
	SetTimer(1, 33, NULL);

	m_alpha.SetRange(40, 255, TRUE);
	m_alpha.SetTicFreq(16);
	m_alpha.SetMode(1);
	m_alpha.SetAeroMode(FALSE);
	m_alphaL.SetAeroMode(FALSE);
	m_close.SetAeroMode(FALSE);
	SetDeskLrcAlpha(savedata.deskLrcAlpha, TRUE);

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
	if (m_alpha.GetSafeHwnd()) {
		m_tooltip.AddTool(&m_alpha, LL14(
			L"ウィンドウ全体の不透明度（左=薄い / 右=濃い）", L"Overall window opacity (left=thin / right=solid)", L"Opacite de la fenetre (gauche=faible / droite=forte)", L"Opacita finestra (sinistra=bassa / destra=alta)", L"Opacidad de ventana (izq=baja / der=alta)",
			L"창 전체 불투명도(왼쪽=옅음 / 오른쪽=진함)", L"窗口整体不透明度（左淡/右浓）", L"شفافية النافذة (يسار=خفيف / يمين=كثيف)", L"Непрозрачность окна (лево=слабее / право=сильнее)", L"Fensterdeckkraft (links=duenn / rechts=deckend)",
			L"Opacidade da janela (esq=fraca / dir=forte)", L"Dekking van venster (links=licht / rechts=dicht)", L"Nieprzezroczystosc okna (lewo=slabo / prawo=mocno)", L"Pencere opakligi (sol=ince / sag=yogun)"));
	}
	if (m_close.GetSafeHwnd()) {
		m_tooltip.AddTool(&m_close, LL14(
			L"歌詞ウィンドウを閉じます。", L"Close the lyrics window.", L"Fermer la fenetre de paroles.", L"Chiudi la finestra testi.", L"Cerrar la ventana de letra.",
			L"가사 창을 닫습니다.", L"关闭歌词窗口。", L"إغلاق نافذة الكلمات.", L"Закрыть окно текста.", L"Textfenster schließen.",
			L"Fechar a janela de letra.", L"Songtekstvenster sluiten.", L"Zamknij okno tekstu.", L"Soz penceresini kapat."));
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
	ApplyWindowAlpha();
	return TRUE;
}

BOOL CDesktopLyricsWnd::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CDesktopLyricsWnd::MakeSolidClient()
{
	if (!GetSafeHwnd()) return;
#if CCUSTOM_AERO_SUPPORT
	// CCC_ApplyAero(FALSE) は WS_EX_LAYERED を剥がすので使わない。
	// LWA_ALPHA 透過度を維持したまま、本文アクリル/blur だけ落とす。
	CCC_ClearChildTrans(m_hWnd);
	{
		int backdropNone = 1; // DWMSBT_NONE
		::DwmSetWindowAttribute(m_hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropNone, sizeof(backdropNone));
		MARGINS mz = { 0, 0, 0, 0 };
		::DwmExtendFrameIntoClientArea(m_hWnd, &mz);
		DWM_BLURBEHIND bb = {};
		bb.dwFlags = DWM_BB_ENABLE;
		bb.fEnable = FALSE;
		::DwmEnableBlurBehindWindow(m_hWnd, &bb);
		if (CCC_GetWindowsBuildNumber() >= 26100) {
#ifndef DWMWA_REDIRECTIONBITMAP_ALPHA
#define DWMWA_REDIRECTIONBITMAP_ALPHA 39
#endif
			BOOL useAlpha = FALSE;
			::DwmSetWindowAttribute(m_hWnd, DWMWA_REDIRECTIONBITMAP_ALPHA, &useAlpha, sizeof(useAlpha));
		}
	}
#endif
	m_bAeroEnabled = FALSE;
}

void CDesktopLyricsWnd::ApplyDwmBlur()
{
	// 全面アクリルにしない（歌詞が透け落ちる / LAYERED と競合）
	MakeSolidClient();
	ApplyWindowAlpha();
}

void CDesktopLyricsWnd::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogBase::OnShowWindow(bShow, nStatus);
	if (bShow) {
		// 基底が RefreshDwmBlur で backdrop を戻す場合があるので再切断
		MakeSolidClient();
		ApplyWindowAlpha();
		if (m_view.GetSafeHwnd()) {
			m_view.ShowWindow(SW_SHOW);
			m_view.Invalidate(FALSE);
		}
		Invalidate(FALSE);
	}
}

void CDesktopLyricsWnd::SetDeskLrcAlpha(int a, BOOL syncSlider)
{
	if (a < 40) a = 40;
	if (a > 255) a = 255;
	savedata.deskLrcAlpha = a;
	ApplyWindowAlpha();
	if (syncSlider && m_alpha.GetSafeHwnd() && m_alpha.GetPos() != a)
		m_alpha.SetPos(a);
	MpPersistSavedataQuick();
}

void CDesktopLyricsWnd::SetDeskLrcFont(int ptTenths, BOOL autoFit)
{
	if (autoFit) {
		savedata.deskLrcFontAuto = 1;
	} else {
		savedata.deskLrcFontAuto = 0;
		if (ptTenths < 80) ptTenths = 80;
		if (ptTenths > 480) ptTenths = 480;
		savedata.deskLrcFontPt = ptTenths;
	}
	LayoutClient();
	MpPersistSavedataQuick();
}

void CDesktopLyricsWnd::SetDeskLrcLines(int lines)
{
	if (lines < 3) lines = 3;
	if (lines > 20) lines = 20;
	savedata.deskLrcLines = lines;
	savedata.deskLrcFontAuto = 1; // 行数指定は自動フィット前提
	LayoutClient();
	MpPersistSavedataQuick();
}

void CDesktopLyricsWnd::ApplyWindowAlpha()
{
	if (!m_hWnd || !::IsWindow(m_hWnd)) return;
	int a = savedata.deskLrcAlpha;
	if (a < 40) a = 40;
	if (a > 255) a = 255;
	savedata.deskLrcAlpha = a;
	LONG ex = ::GetWindowLong(m_hWnd, GWL_EXSTYLE);
	if (!(ex & WS_EX_LAYERED))
		::SetWindowLong(m_hWnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
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
	const int cap = GetCustomCaptionHeight();
	CRect viewRc = rc;
	if (cap > 0 && viewRc.Height() > cap)
		viewRc.top = cap;
	if (viewRc.Height() > footer)
		viewRc.bottom -= footer;
	if (viewRc.bottom < viewRc.top + 40)
		viewRc.bottom = viewRc.top + 40;
	if (m_view.GetSafeHwnd()) {
		m_view.MoveWindow(&viewRc, TRUE);
		m_view.ShowWindow(SW_SHOW);

		// フォント: 自動=表示高さ÷目標行数でフィット（既定14pt未満にはしない＝窓が小さいときは行数減）
		int pt = savedata.deskLrcFontPt;
		if (savedata.deskLrcFontAuto) {
			const int viewH = viewRc.Height();
			int dpi = 96;
			HDC hdc = ::GetDC(m_hWnd);
			if (hdc) {
				dpi = ::GetDeviceCaps(hdc, LOGPIXELSY);
				::ReleaseDC(m_hWnd, hdc);
			}
			if (dpi < 72) dpi = 96;
			int lines = savedata.deskLrcLines;
			if (lines < 3) lines = 3;
			if (lines > 20) lines = 20;
			// overlay 行高 ≈ (pt/10)*(dpi/72)*1.12 + ~10
			int targetLH = viewH / lines;
			if (targetLH < 18) targetLH = 18;
			if (targetLH > 120) targetLH = 120;
			int body = targetLH - 10;
			if (body < 8) body = 8;
			pt = (body * 72000) / (dpi * 112);
			if (pt < 140) pt = 140; // 旧既定未満へ縮めない
			if (pt > 480) pt = 480;
			savedata.deskLrcFontPt = pt; // スライダー／手動切替用に実効値を同期
		} else {
			if (pt < 80) pt = 80;
			if (pt > 480) pt = 480;
		}
		m_view.EnsureFonts(pt, _T("Segoe UI"));
	}

	if (m_alphaL.GetSafeHwnd())
		m_alphaL.SetWindowPos(NULL, 8, rc.bottom - 22, 56, 14, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_alpha.GetSafeHwnd())
		m_alpha.SetWindowPos(NULL, 66, rc.bottom - 24, rc.Width() - 134, 18, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_close.GetSafeHwnd())
		m_close.SetWindowPos(NULL, rc.right - 58, rc.bottom - 24, 50, 18, SWP_NOZORDER | SWP_NOACTIVATE);
}

void CDesktopLyricsWnd::SyncFromOg()
{
	if (!m_view.GetSafeHwnd()) return;
	if (!og || og->lrcnum < 2) {
		m_view.Clear();
		m_view.Invalidate(FALSE);
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
	m_view.Invalidate(FALSE);
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
		SetDeskLrcAlpha(a, FALSE);
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

void CDesktopLyricsWnd::ShowDeskLrcMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	DeskLrcMenuCtx ctx;
	ctx.wnd = this;
	ctx.menu = &menu;

	const int curA = savedata.deskLrcAlpha;
	menu.AddSlider(
		LL14(L"不透明度", L"Opacity", L"Opacite", L"Opacita", L"Opacidad",
			L"불투명도", L"不透明度", L"الشفافية", L"Непрозрачность", L"Deckkraft",
			L"Opacidade", L"Dekking", L"Nieprzezroczystosc", L"Opaklik"),
		40, 255, curA, DeskLrcAlphaSliderCb, &ctx,
		LL14(L"ウィンドウ全体の不透明度", L"Overall window opacity", L"Opacite de la fenetre", L"Opacita finestra", L"Opacidad de ventana",
			L"창 전체 불투명도", L"窗口整体不透明度", L"شفافية النافذة", L"Непрозрачность окна", L"Fensterdeckkraft",
			L"Opacidade da janela", L"Dekking van venster", L"Nieprzezroczystosc okna", L"Pencere opakligi"),
		ID_DLRC_ALPHA_SLIDER);
	menu.AddProgress(
		LL14(L"不透明度プレビュー", L"Opacity preview", L"Apercu opacite", L"Anteprima opacita", L"Vista previa opacidad",
			L"불투명도 미리보기", L"不透明度预览", L"معاينة الشفافية", L"Превью непрозрачности", L"Deckkraft-Vorschau",
			L"Previa opacidade", L"Dekking voorbeeld", L"Podglad nieprzezroczystosci", L"Opaklik onizleme"),
		40, 255, curA, TRUE,
		LL14(L"スライダーと連動", L"Follows the slider", L"Suit le curseur", L"Segue lo slider", L"Sigue el control",
			L"슬라이더와 연동", L"与滑块联动", L"يتبع الشريط", L"Следует за ползунком", L"Folgt dem Schieberegler",
			L"Acompanha o slider", L"Volgt de schuif", L"Podaza za suwakiem", L"Kaydiriciyi izler"),
		ID_DLRC_ALPHA_PROG);
	menu.AddButton(ID_DLRC_ALPHA_RESET,
		LL14(L"標準(200)に戻す", L"Reset to normal (200)", L"Reinitialiser (200)", L"Ripristina (200)", L"Restablecer (200)",
			L"표준(200)으로", L"恢复标准(200)", L"إعادة إلى عادي (200)", L"Сброс на обычную (200)", L"Auf Normal (200)",
			L"Redefinir (200)", L"Terugzetten (200)", L"Przywroc (200)", L"Normala don (200)"),
		DeskLrcAlphaResetBtnCb, &ctx,
		LL14(L"不透明度を 200 に戻します（メニューは開いたまま）", L"Reset opacity to 200 (menu stays open)",
			L"Remettre a 200 (menu reste ouvert)", L"Ripristina a 200 (menu aperto)", L"Restablecer a 200 (menu abierto)",
			L"불투명도 200으로 (메뉴 유지)", L"恢复为 200（菜单保持打开）", L"إعادة إلى 200 (القائمة تبقى)",
			L"Сброс на 200 (меню открыто)", L"Auf 200 (Menü bleibt)", L"Redefinir para 200 (menu aberto)",
			L"Terug naar 200 (menu open)", L"Przywroc 200 (menu otwarte)", L"200'e don (menu acik kalir)"),
		FALSE);

	menu.AddSeparator();
	menu.AddCommand(ID_DLRC_ALPHA_120,
		LL14(L"薄い (120)", L"Thin (120)", L"Faible (120)", L"Bassa (120)", L"Baja (120)",
			L"옅음 (120)", L"较淡 (120)", L"خفيف (120)", L"Слабая (120)", L"Duenn (120)",
			L"Fraca (120)", L"Licht (120)", L"Slaba (120)", L"Ince (120)"));
	menu.AddCommand(ID_DLRC_ALPHA_160,
		LL14(L"やや薄い (160)", L"Light (160)", L"Legere (160)", L"Leggera (160)", L"Ligera (160)",
			L"약간 옅음 (160)", L"略淡 (160)", L"خفيف قليلاً (160)", L"Чуть слабее (160)", L"Etwas duenn (160)",
			L"Leve (160)", L"Iets licht (160)", L"Lekko slaba (160)", L"Biraz ince (160)"));
	menu.AddCommand(ID_DLRC_ALPHA_200,
		LL14(L"標準 (200)", L"Normal (200)", L"Normale (200)", L"Normale (200)", L"Normal (200)",
			L"표준 (200)", L"标准 (200)", L"عادي (200)", L"Обычная (200)", L"Normal (200)",
			L"Normal (200)", L"Normaal (200)", L"Normalna (200)", L"Normal (200)"));
	menu.AddCommand(ID_DLRC_ALPHA_230,
		LL14(L"濃い (230)", L"Dense (230)", L"Forte (230)", L"Alta (230)", L"Alta (230)",
			L"진함 (230)", L"较浓 (230)", L"كثيف (230)", L"Плотная (230)", L"Deckend (230)",
			L"Forte (230)", L"Dicht (230)", L"Gesta (230)", L"Yogun (230)"));
	menu.AddCommand(ID_DLRC_ALPHA_255,
		LL14(L"不透明 (255)", L"Opaque (255)", L"Opaque (255)", L"Opaca (255)", L"Opaca (255)",
			L"불투명 (255)", L"不透明 (255)", L"معتم (255)", L"Непрозрачная (255)", L"Undurchsichtig (255)",
			L"Opaca (255)", L"Ondoorzichtig (255)", L"Nieprzezroczysta (255)", L"Opak (255)"));

	menu.AddSeparator();
	{
		const BOOL fontAuto = savedata.deskLrcFontAuto != 0;
		int curPt = savedata.deskLrcFontPt;
		if (m_view.GetSafeHwnd()) {
			const int vpt = m_view.GetFontPt();
			if (vpt >= 80 && vpt <= 480)
				curPt = vpt;
		}
		if (curPt < 80) curPt = 80;
		if (curPt > 480) curPt = 480;
		int curPtUi = curPt / 10;
		if (curPtUi < 8) curPtUi = 8;
		if (curPtUi > 48) curPtUi = 48;

		menu.AddCheck(ID_DLRC_FONT_AUTO,
			LL14(L"フォントをウィンドウに合わせる", L"Fit font to window", L"Police selon fenetre", L"Adatta font alla finestra", L"Ajustar fuente a ventana",
				L"글꼴을 창에 맞춤", L"字体随窗口", L"ملاءمة الخط مع النافذة", L"Шрифт по окну", L"Schrift an Fenster",
				L"Fonte conforme a janela", L"Lettertype op venster", L"Czcionka do okna", L"Yazi tipini pencereye uyarla"),
			fontAuto,
			LL14(L"表示領域の高さで文字サイズを変え、下の「表示行数」に合わせます。", L"Scales text with the view height to match Visible lines below.", L"Ajuste la taille pour le nombre de lignes ci-dessous.", L"Scala il testo per le righe sotto.", L"Escala el texto segun las lineas abajo.",
				L"표시 높이로 글자 크기를 바꿔 아래 표시 행수에 맞춥니다.", L"按显示高度调整字号以匹配下方“显示行数”。", L"يغيّر حجم النص حسب الارتفاع ليطابق عدد الأسطر أدناه.", L"Меняет размер по высоте под число строк ниже.", L"Passt die Größe an die Höhe und die Zeilenzahl unten an.",
				L"Ajusta o tamanho pela altura conforme as linhas abaixo.", L"Past grootte aan op hoogte en regels hieronder.", L"Dopasowuje rozmiar do wysokosci i liczby wierszy.", L"Yukseklige ve asagidaki satir sayisina gore boyutu ayarlar."));

		{
			int lines = savedata.deskLrcLines;
			if (lines < 3) lines = 3;
			if (lines > 20) lines = 20;
			menu.AddSlider(
				LL14(L"表示行数", L"Visible lines", L"Lignes visibles", L"Righe visibili", L"Lineas visibles",
					L"표시 행수", L"显示行数", L"الأسطر الظاهرة", L"Видимые строки", L"Sichtbare Zeilen",
					L"Linhas visiveis", L"Zichtbare regels", L"Widoczne wiersze", L"Gorunen satir"),
				3, 20, lines, DeskLrcLinesSliderCb, &ctx,
				LL14(L"画面内にだいたい何行見せるか（リアルタイム・自動フィットON）", L"About how many lines fit on screen (live; turns auto-fit on)", L"Nombre de lignes a l'ecran (direct; active l'auto)", L"Quante righe a schermo (live; attiva auto)", L"Cuantas lineas caben (en vivo; activa auto)",
					L"화면에 대략 몇 행 보일지(즉시·자동 맞춤 ON)", L"大约显示几行（即时；开启自动）", L"كم سطراً تقريباً يظهر (مباشر؛ يفعّل الملاءمة)", L"Сколько строк на экране (сразу; включает авто)", L"Wie viele Zeilen passen (live; schaltet Auto ein)",
					L"Quantas linhas cabem (ao vivo; liga o auto)", L"Hoeveel regels passen (live; zet auto aan)", L"Ile wierszy sie miesci (na zywo; wlacza auto)", L"Ekrana kac satir sigsin (anlik; otomati acar)"),
				ID_DLRC_LINES_SLIDER);
		}

		menu.AddSlider(
			LL14(L"文字サイズ (pt)", L"Font size (pt)", L"Taille police (pt)", L"Dimensione font (pt)", L"Tamano fuente (pt)",
				L"글자 크기 (pt)", L"字号 (pt)", L"حجم الخط (pt)", L"Размер шрифта (pt)", L"Schriftgroesse (pt)",
				L"Tamanho da fonte (pt)", L"Tekengrootte (pt)", L"Rozmiar czcionki (pt)", L"Yazi boyutu (pt)"),
			8, 48, curPtUi, DeskLrcFontSliderCb, &ctx,
			LL14(L"手動サイズ（動かすと自動フィットを解除）", L"Manual size (disables auto-fit)", L"Taille manuelle (desactive l'auto)", L"Manuale (disattiva auto)", L"Manual (desactiva auto)",
				L"수동 크기(움직이면 자동 맞춤 해제)", L"手动大小（拖动后关闭自动）", L"يدوي (يلغي الملاءمة التلقائية)", L"Вручную (отключает авто)", L"Manuell (schaltet Auto aus)",
				L"Manual (desativa o auto)", L"Handmatig (zet auto uit)", L"Recznie (wylacza auto)", L"Manuel (otomatigi kapatir)"),
			ID_DLRC_FONT_SLIDER);

		menu.AddCommand(ID_DLRC_FONT_S,
			LL14(L"小 (10pt)", L"Small (10pt)", L"Petite (10pt)", L"Piccola (10pt)", L"Pequena (10pt)",
				L"작게 (10pt)", L"小 (10pt)", L"صغير (10pt)", L"Мелкий (10pt)", L"Klein (10pt)",
				L"Pequena (10pt)", L"Klein (10pt)", L"Mala (10pt)", L"Kucuk (10pt)"));
		menu.AddCommand(ID_DLRC_FONT_M,
			LL14(L"標準 (14pt)", L"Normal (14pt)", L"Normale (14pt)", L"Normale (14pt)", L"Normal (14pt)",
				L"표준 (14pt)", L"标准 (14pt)", L"عادي (14pt)", L"Обычный (14pt)", L"Normal (14pt)",
				L"Normal (14pt)", L"Normaal (14pt)", L"Normalna (14pt)", L"Normal (14pt)"));
		menu.AddCommand(ID_DLRC_FONT_L,
			LL14(L"大 (20pt)", L"Large (20pt)", L"Grande (20pt)", L"Grande (20pt)", L"Grande (20pt)",
				L"크게 (20pt)", L"大 (20pt)", L"كبير (20pt)", L"Крупный (20pt)", L"Gross (20pt)",
				L"Grande (20pt)", L"Groot (20pt)", L"Duza (20pt)", L"Buyuk (20pt)"));
		menu.AddCommand(ID_DLRC_FONT_XL,
			LL14(L"特大 (28pt)", L"XL (28pt)", L"Tres grande (28pt)", L"Molto grande (28pt)", L"Muy grande (28pt)",
				L"아주 크게 (28pt)", L"特大 (28pt)", L"كبير جداً (28pt)", L"Очень крупный (28pt)", L"Sehr gross (28pt)",
				L"Muito grande (28pt)", L"Erg groot (28pt)", L"Bardzo duza (28pt)", L"Cok buyuk (28pt)"));
		menu.AddCommand(ID_DLRC_FONT_XXL,
			LL14(L"極大 (36pt)", L"XXL (36pt)", L"Enorme (36pt)", L"Enorme (36pt)", L"Enorme (36pt)",
				L"최대 (36pt)", L"极大 (36pt)", L"ضخم (36pt)", L"Огромный (36pt)", L"Riesig (36pt)",
				L"Enorme (36pt)", L"Enorm (36pt)", L"Ogromna (36pt)", L"Dev (36pt)"));
	}

	menu.AddSeparator();
	const BOOL topmost = (::GetWindowLong(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	menu.AddCheck(ID_DLRC_TOPMOST,
		LL14(L"常に手前に表示", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano", L"Siempre visible",
			L"항상 위", L"总在最前", L"دائماً في المقدمة", L"Поверх всех окон", L"Immer im Vordergrund",
			L"Sempre no topo", L"Altijd bovenop", L"Zawsze na wierzchu", L"Her zaman ustte"),
		topmost);

	BOOL canCopy = FALSE;
	if (og && og->lrcnum >= 2 && og->lrccur >= 0 && og->lrccur < og->lrcnum - 1
		&& !og->lrc[og->lrccur].IsEmpty())
		canCopy = TRUE;
	menu.AddCommand(ID_DLRC_COPY,
		LL14(L"現在の歌詞をコピー", L"Copy current line", L"Copier la ligne actuelle", L"Copia riga corrente", L"Copiar linea actual",
			L"현재 가사 복사", L"复制当前歌词", L"نسخ السطر الحالي", L"Копировать текущую строку", L"Aktuelle Zeile kopieren",
			L"Copiar linha atual", L"Huidige regel kopieren", L"Kopiuj biezacy wiersz", L"Gecerli satiri kopyala"),
		NULL, canCopy);

	menu.AddSeparator();
	menu.AddCommand(ID_DLRC_CLOSE,
		LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
			L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen",
			L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"),
		LL14(L"歌詞ウィンドウを閉じます。", L"Close the lyrics window.", L"Fermer la fenetre de paroles.", L"Chiudi la finestra testi.", L"Cerrar la ventana de letra.",
			L"가사 창을 닫습니다.", L"关闭歌词窗口。", L"إغلاق نافذة الكلمات.", L"Закрыть окно текста.", L"Textfenster schließen.",
			L"Fechar a janela de letra.", L"Songtekstvenster sluiten.", L"Zamknij okno tekstu.", L"Soz penceresini kapat."));

	const UINT cmd = menu.Track(screenPt, this);
	if (cmd == ID_DLRC_ALPHA_120) SetDeskLrcAlpha(120, TRUE);
	else if (cmd == ID_DLRC_ALPHA_160) SetDeskLrcAlpha(160, TRUE);
	else if (cmd == ID_DLRC_ALPHA_200) SetDeskLrcAlpha(200, TRUE);
	else if (cmd == ID_DLRC_ALPHA_230) SetDeskLrcAlpha(230, TRUE);
	else if (cmd == ID_DLRC_ALPHA_255) SetDeskLrcAlpha(255, TRUE);
	else if (cmd == ID_DLRC_FONT_AUTO)
		SetDeskLrcFont(savedata.deskLrcFontPt, savedata.deskLrcFontAuto ? FALSE : TRUE);
	else if (cmd == ID_DLRC_FONT_S) SetDeskLrcFont(100, FALSE);
	else if (cmd == ID_DLRC_FONT_M) SetDeskLrcFont(140, FALSE);
	else if (cmd == ID_DLRC_FONT_L) SetDeskLrcFont(200, FALSE);
	else if (cmd == ID_DLRC_FONT_XL) SetDeskLrcFont(280, FALSE);
	else if (cmd == ID_DLRC_FONT_XXL) SetDeskLrcFont(360, FALSE);
	else if (cmd == ID_DLRC_TOPMOST)
		SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CAP_PIN, BN_CLICKED), 0);
	else if (cmd == ID_DLRC_COPY && canCopy) {
		const CString& s = og->lrc[og->lrccur];
		if (::OpenClipboard(m_hWnd)) {
			::EmptyClipboard();
			const size_t bytes = (size_t)(s.GetLength() + 1) * sizeof(wchar_t);
			HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
			if (h) {
				wchar_t* p = (wchar_t*)::GlobalLock(h);
				if (p) {
					memcpy(p, (LPCWSTR)s, bytes);
					::GlobalUnlock(h);
					::SetClipboardData(CF_UNICODETEXT, h);
				} else {
					::GlobalFree(h);
				}
			}
			::CloseClipboard();
		}
	}
	else if (cmd == ID_DLRC_CLOSE)
		DestroyWindow();
}

void CDesktopLyricsWnd::OnRButtonUp(UINT nFlags, CPoint point)
{
	const int cap = GetCustomCaptionHeight();
	if (cap > 0 && point.y >= 0 && point.y < cap) {
		CCustomBlurDialogBase::OnRButtonUp(nFlags, point);
		return;
	}
	CPoint sp = point;
	ClientToScreen(&sp);
	ShowDeskLrcMenu(sp);
}

void CDesktopLyricsWnd::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	if (point.x == -1 && point.y == -1) {
		CRect r;
		GetWindowRect(&r);
		point = r.CenterPoint();
	}
	CPoint client = point;
	ScreenToClient(&client);
	const int cap = GetCustomCaptionHeight();
	if (cap > 0 && client.y >= 0 && client.y < cap) {
		CCustomBlurDialogBase::OnRButtonUp(0, client);
		return;
	}
	ShowDeskLrcMenu(point);
}

void CDesktopLyricsWnd::OnDestroy()
{
	KillTimer(1);
	PersistGeometry();
	// deskLrcOn はトグル側で管理。X 閉じでは好みを落とさない（次回起動で復元）
	MpPersistSavedataQuick();
	CCustomBlurDialogBase::OnDestroy();
}

void CDesktopLyricsWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1)
		SyncFromOg();
	CCustomBlurDialogBase::OnTimer(nIDEvent);
}

BOOL CDesktopLyricsWnd::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc;
	GetClientRect(&rc);
	const int cap = GetCustomCaptionHeight();
	if (cap > 0 && rc.Height() > cap)
		rc.top = cap;
	pDC->FillSolidRect(&rc, RGB(18, 18, 28));
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
		g_desktopLyricsWnd->MakeSolidClient();
		g_desktopLyricsWnd->ApplyWindowAlpha();
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

