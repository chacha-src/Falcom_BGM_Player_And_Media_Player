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
	ID_DLRC_CLOSE = 41008
};

static void DeskLrcAlphaSliderCb(void* ctx, int value)
{
	CDesktopLyricsWnd* p = (CDesktopLyricsWnd*)ctx;
	if (!p || !::IsWindow(p->GetSafeHwnd())) return;
	p->SetDeskLrcAlpha(value, TRUE);
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
		L"デスクトップ歌詞", L"Desktop lyrics", L"Paroles bureau", L"Testi desktop", L"Letra escritorio",
		L"데스크톱 가사", L"桌面歌词", L"كلمات سطح المكتب", L"Текст на рабочем столе", L"Desktop-Text",
		L"Letra na area de trabalho", L"Bureaublad songtekst", L"Tekst na pulpicie", L"Masaustu sozleri"));

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
	if (w > 1200) w = 720;
	if (h > 600) h = 220;

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
		m_view.EnsureFonts(140, _T("Segoe UI"));
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
			L"デスクトップ歌詞を閉じます。", L"Close desktop lyrics.", L"Fermer les paroles bureau.", L"Chiudi i testi desktop.", L"Cerrar letra de escritorio.",
			L"데스크톱 가사를 닫습니다.", L"关闭桌面歌词。", L"إغلاق كلمات سطح المكتب.", L"Закрыть текст на рабочем столе.", L"Desktop-Text schließen.",
			L"Fechar letra na area de trabalho.", L"Bureaubladtekst sluiten.", L"Zamknij tekst na pulpicie.", L"Masaustu sozlerini kapat."));
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

	const int curA = savedata.deskLrcAlpha;
	menu.AddSlider(
		LL14(L"不透明度", L"Opacity", L"Opacite", L"Opacita", L"Opacidad",
			L"불투명도", L"不透明度", L"الشفافية", L"Непрозрачность", L"Deckkraft",
			L"Opacidade", L"Dekking", L"Nieprzezroczystosc", L"Opaklik"),
		40, 255, curA, DeskLrcAlphaSliderCb, this,
		LL14(L"ウィンドウ全体の不透明度", L"Overall window opacity", L"Opacite de la fenetre", L"Opacita finestra", L"Opacidad de ventana",
			L"창 전체 불투명도", L"窗口整体不透明度", L"شفافية النافذة", L"Непрозрачность окна", L"Fensterdeckkraft",
			L"Opacidade da janela", L"Dekking van venster", L"Nieprzezroczystosc okna", L"Pencere opakligi"));

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
		LL14(L"デスクトップ歌詞を閉じます。", L"Close desktop lyrics.", L"Fermer les paroles bureau.", L"Chiudi i testi desktop.", L"Cerrar letra de escritorio.",
			L"데스크톱 가사를 닫습니다.", L"关闭桌面歌词。", L"إغلاق كلمات سطح المكتب.", L"Закрыть текст на рабочем столе.", L"Desktop-Text schließen.",
			L"Fechar letra na area de trabalho.", L"Bureaubladtekst sluiten.", L"Zamknij tekst na pulpicie.", L"Masaustu sozlerini kapat."));

	const UINT cmd = menu.Track(screenPt, this);
	if (cmd == ID_DLRC_ALPHA_120) SetDeskLrcAlpha(120, TRUE);
	else if (cmd == ID_DLRC_ALPHA_160) SetDeskLrcAlpha(160, TRUE);
	else if (cmd == ID_DLRC_ALPHA_200) SetDeskLrcAlpha(200, TRUE);
	else if (cmd == ID_DLRC_ALPHA_230) SetDeskLrcAlpha(230, TRUE);
	else if (cmd == ID_DLRC_ALPHA_255) SetDeskLrcAlpha(255, TRUE);
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

