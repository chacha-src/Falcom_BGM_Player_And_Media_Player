#include "stdafx.h"
#include "ogg.h"
#include "CMpHelpDlg.h"

extern save savedata;

// CCustomControl.cpp 内 static と同趣旨（こちらからリンクできないためローカル）
static UINT MphGetDpi(HWND hWnd)
{
	if (!hWnd) return 96;
	HDC hdc = ::GetDC(hWnd);
	if (!hdc) return 96;
	const UINT dpi = (UINT)::GetDeviceCaps(hdc, LOGPIXELSX);
	::ReleaseDC(hWnd, hdc);
	return (dpi > 0) ? dpi : 96;
}
static int MphScaleDpi(int value, UINT dpi)
{
	return ::MulDiv(value, (int)dpi, 96);
}

CMpHelpDlg* CMpHelpDlg::s_inst = nullptr;

IMPLEMENT_DYNAMIC(CMpHelpDlg, CCustomBlurDialogExBase)

CMpHelpDlg::CMpHelpDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CMpHelpDlg::IDD, pParent)
	, m_memOldBmp(nullptr)
	, m_memW(0)
	, m_memH(0)
	, m_animTick(0)
	, m_timer(0)
	, m_chapter(0)
	, m_contentBottom(0)
	, m_fittedChapter(-1)
	, m_ownerMp(pParent)
{
	m_bodyRc.SetRectEmpty();
	for (int i = 0; i < kMapRegionN; ++i)
		m_mapRc[i].SetRectEmpty();
	m_demoCam.yawDeg = -32.f;
	m_demoCam.pitchDeg = 28.f;
	m_demoCam.zoom = 1.f;
}

CMpHelpDlg::~CMpHelpDlg()
{
	if (m_memOldBmp && m_mem.GetSafeHdc())
		m_mem.SelectObject(m_memOldBmp);
}

void CMpHelpDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MPH_TABS, m_tabs);
	DDX_Control(pDX, IDC_MPH_COPY, m_copy);
	DDX_Control(pDX, IDC_MPH_BODY, m_body);
}

BEGIN_MESSAGE_MAP(CMpHelpDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_TIMER()
	ON_WM_CLOSE()
	ON_WM_LBUTTONDOWN()
	ON_NOTIFY(TCN_SELCHANGE, IDC_MPH_TABS, &CMpHelpDlg::OnTabSelChange)
	ON_BN_CLICKED(IDC_MPH_COPY, &CMpHelpDlg::OnCopy)
END_MESSAGE_MAP()

BOOL CMpHelpDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;

	SetWindowText(LL14(
		L"メディアプレイヤー操作ガイド", L"Media Player Guide", L"Guide du lecteur", L"Guida Media Player",
		L"Guía de Media Player", L"미디어 플레이어 가이드", L"媒体播放器指南", L"دليل مشغل الوسائط",
		L"Руководство плеера", L"Media-Player-Anleitung", L"Guia do Media Player", L"Mediaspeler-gids",
		L"Przewodnik Media Player", L"Medya Oynatıcı kılavuzu"));

	// タブ・ボタンは不透明のまま(アクリル帯はキャプションだけ)
	m_tabs.SetAeroMode(FALSE);
	m_copy.SetAeroMode(FALSE);

	{
		LPCTSTR names[kChapterN] = {
			LL14(L"概要", L"Overview", L"Aperçu", L"Panoramica", L"Resumen", L"개요", L"概要", L"نظرة عامة",
				L"Обзор", L"Überblick", L"Visão geral", L"Overzicht", L"Przegląd", L"Genel bakış"),
			LL14(L"再生", L"Playback", L"Lecture", L"Riproduzione", L"Reproducción", L"재생", L"播放", L"التشغيل",
				L"Воспроизведение", L"Wiedergabe", L"Reprodução", L"Weergave", L"Odtwarzanie", L"Çalma"),
			LL14(L"リスト", L"List", L"Liste", L"Elenco", L"Lista", L"목록", L"列表", L"القائمة",
				L"Список", L"Liste", L"Lista", L"Lijst", L"Lista", L"Liste"),
			L"Soft3D",
			LL14(L"ショートカット", L"Shortcuts", L"Raccourcis", L"Scorciatoie", L"Atajos", L"단축키", L"快捷键", L"اختصارات",
				L"Горячие клавиши", L"Tastenkürzel", L"Atalhos", L"Sneltoetsen", L"Skróty", L"Kısayollar")
		};
		TCITEM ti = {};
		ti.mask = TCIF_TEXT;
		for (int i = 0; i < kChapterN; ++i) {
			ti.pszText = (LPTSTR)names[i];
			m_tabs.InsertItem(i, &ti);
		}
		m_tabs.SetCurSel(0);
	}

	m_copy.SetWindowText(LL14(
		L"コピー", L"Copy", L"Copier", L"Copia", L"Copiar", L"복사", L"复制", L"نسخ",
		L"Копировать", L"Kopieren", L"Copiar", L"Kopiëren", L"Kopiuj", L"Kopyala"));
	m_copy.SetGradation(RGB(255, 238, 208), RGB(255, 202, 130), 0, TRUE);

	// 本文はダイアログ側で描くので場所取りのスタティックは隠す(当たり判定は m_bodyRc)
	if (m_body.GetSafeHwnd())
		m_body.ShowWindow(SW_HIDE);

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
	m_tooltip.AddTool(&m_copy, LL14(
		L"ショートカット一覧をクリップボードへコピーします。", L"Copy the shortcut list to the clipboard.",
		L"Copier la liste des raccourcis.", L"Copia l'elenco delle scorciatoie.", L"Copiar la lista de atajos.",
		L"단축키 목록을 클립보드로 복사.", L"将快捷键列表复制到剪贴板。", L"انسخ قائمة الاختصارات.",
		L"Скопировать список клавиш.", L"Tastenkürzel-Liste kopieren.", L"Copiar a lista de atalhos.",
		L"Kopieer de sneltoetsenlijst.", L"Kopiuj listę skrótów.", L"Kısayol listesini kopyala."));
	{
		LPCTSTR tips[kChapterN] = {
			LL14(L"画面全体の地図。四角をクリックで本体を光らせます。", L"Screen map. Click a block to flash that part.",
				L"Carte de l'écran. Cliquez un bloc.", L"Mappa schermo. Clicca un blocco.", L"Mapa de pantalla. Haga clic en un bloque.",
				L"화면 지도. 사각형 클릭으로 본체 강조.", L"界面地图。点击方块高亮本体。", L"خريطة الشاشة. انقر مربعاً.",
				L"Карта экрана. Клик по блоку.", L"Bildschirmkarte. Block anklicken.", L"Mapa da tela. Clique num bloco.",
				L"Schermkaart. Klik een blok.", L"Mapa ekranu. Kliknij blok.", L"Ekran haritası. Bir bloğa tıkla."),
			LL14(L"再生・シーク・A-B・音量まわり。", L"Play, seek, A-B and volume.", L"Lecture, seek, A-B, volume.",
				L"Play, seek, A-B, volume.", L"Play, seek, A-B, volumen.", L"재생·시크·A-B·음량.", L"播放、定位、A-B、音量。",
				L"تشغيل وseek وA-B والصوت.", L"Play, seek, A-B, громкость.", L"Play, Seek, A-B, Lautstärke.",
				L"Play, seek, A-B, volume.", L"Play, seek, A-B, volume.", L"Play, seek, A-B, głośność.", L"Play, seek, A-B, ses."),
			LL14(L"プレイリスト・ライブラリ・履歴・キュー。", L"Playlist, library, history and queue.",
				L"Liste, bibliothèque, historique, file.", L"Playlist, libreria, cronologia, coda.",
				L"Lista, biblioteca, historial, cola.", L"플레이리스트·라이브러리·기록·큐.", L"播放列表、库、历史、队列。",
				L"القائمة والمكتبة والسجل والطابور.", L"Плейлист, библиотека, история, очередь.",
				L"Playlist, Bibliothek, Verlauf, Warteschlange.", L"Playlist, biblioteca, histórico, fila.",
				L"Playlist, bibliotheek, geschiedenis, wachtrij.", L"Playlista, biblioteka, historia, kolejka.",
				L"Çalma listesi, kitaplık, geçmiş, kuyruk."),
			LL14(L"簡易3Dバナーの操作。実演が動きます。", L"Soft 3D banner controls with a live demo.",
				L"Bannière 3D simplifiée + démo.", L"Banner 3D semplificato + demo.", L"Banner 3D simple + demo.",
				L"간이 3D 배너 조작. 실연 동작.", L"简易3D横幅操作，带演示。", L"عناصر البانر 3D مع عرض حي.",
				L"Простой 3D-баннер + демо.", L"Einfaches 3D-Banner + Demo.", L"Banner 3D simples + demo.",
				L"Eenvoudig 3D-banner + demo.", L"Uproszczony baner 3D + demo.", L"Basit 3B banner + demo."),
			LL14(L"キー操作の一覧。Copy でコピーできます。", L"Key list. Use Copy to grab it.",
				L"Liste des touches. Bouton Copier.", L"Elenco tasti. Pulsante Copia.", L"Lista de teclas. Botón Copiar.",
				L"키 목록. Copy로 복사.", L"按键一览。可用 Copy 复制。", L"قائمة المفاتيح. زر النسخ.",
				L"Список клавиш. Кнопка «Копировать».", L"Tastenliste. Schaltfläche Kopieren.",
				L"Lista de teclas. Botão Copiar.", L"Toetsenlijst. Knop Kopiëren.", L"Lista klawiszy. Przycisk Kopiuj.",
				L"Tuş listesi. Kopyala düğmesi.")
		};
		CRect rcItem;
		for (int i = 0; i < kChapterN; ++i) {
			if (!m_tabs.GetItemRect(i, &rcItem))
				rcItem.SetRect(0, 0, 1, 1);
			m_tooltip.AddTool(&m_tabs, tips[i], &rcItem, (UINT)(i + 1));
		}
	}
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 12000);

	LayoutChrome();
	if (!m_timer)
		m_timer = SetTimer(kAnimTimerId, 33, nullptr);
	return TRUE;
}

void CMpHelpDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd)) return;
	CRect rc;
	GetClientRect(&rc);
	if (rc.Width() < 80 || rc.Height() < 80) return;

	const UINT dpi = MphGetDpi(m_hWnd);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = MphScaleDpi(8, dpi);
	const int btnW = MphScaleDpi(110, dpi);
	const int btnH = MphScaleDpi(26, dpi);

	int tabTop = (capH > 0 ? capH : 0) + pad;
	int tabH = MphScaleDpi(28, dpi);
	if (m_tabs.GetSafeHwnd()) {
		m_tabs.SetWindowPos(NULL, pad, tabTop, rc.Width() - pad * 2, tabH,
			SWP_NOZORDER | SWP_NOACTIVATE);
		m_tabs.LayoutEqualTabs(kChapterN);
		CRect rcItem;
		if (m_tabs.GetItemRect(0, &rcItem) && rcItem.Height() + MphScaleDpi(6, dpi) > tabH) {
			tabH = rcItem.Height() + MphScaleDpi(6, dpi);
			m_tabs.SetWindowPos(NULL, pad, tabTop, rc.Width() - pad * 2, tabH,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
		// タブ帯の tip 矩形はタブ幅の再配置で変わるため貼り直す
		if (m_tooltip.GetSafeHwnd()) {
			for (int i = 0; i < kChapterN; ++i) {
				if (m_tabs.GetItemRect(i, &rcItem))
					m_tooltip.SetToolRect(&m_tabs, (UINT)(i + 1), &rcItem);
			}
		}
	}

	if (m_copy.GetSafeHwnd()) {
		m_copy.SetWindowPos(NULL, pad, rc.bottom - pad - btnH, btnW, btnH,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}

	m_bodyRc.SetRect(pad, tabTop + tabH + 6, rc.right - pad, rc.bottom - pad - btnH - 6);
	if (m_bodyRc.bottom < m_bodyRc.top + 40)
		m_bodyRc.bottom = m_bodyRc.top + 40;
	if (m_body.GetSafeHwnd()) {
		m_body.SetWindowPos(NULL, m_bodyRc.left, m_bodyRc.top, m_bodyRc.Width(), m_bodyRc.Height(),
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void CMpHelpDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED) return;
	LayoutChrome();
	Invalidate(FALSE);
}

void CMpHelpDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	if (lpMMI) {
		const UINT dpi = MphGetDpi(m_hWnd);
		lpMMI->ptMinTrackSize.x = MphScaleDpi(520, dpi);
		lpMMI->ptMinTrackSize.y = MphScaleDpi(300, dpi);
	}
	CCustomBlurDialogExBase::OnGetMinMaxInfo(lpMMI);
}

void CMpHelpDlg::FitWindowToContent()
{
	if (!::IsWindow(m_hWnd) || m_contentBottom <= 0) return;

	const UINT dpi = MphGetDpi(m_hWnd);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = MphScaleDpi(8, dpi);
	const int btnH = MphScaleDpi(26, dpi);
	const int safety = MphScaleDpi(56, dpi); // DPI で本文が見切れるのを防ぐ余白
	int tabH = MphScaleDpi(28, dpi);
	if (m_tabs.GetSafeHwnd()) {
		CRect rcItem;
		if (m_tabs.GetItemRect(0, &rcItem) && rcItem.Height() + MphScaleDpi(6, dpi) > tabH)
			tabH = rcItem.Height() + MphScaleDpi(6, dpi);
	}

	// 本文が足りないときは必ず再フィット（章が同じでも見切れ救済）
	const bool bodyShort = (!m_bodyRc.IsRectEmpty() && m_bodyRc.Height() + 4 < m_contentBottom);
	if (m_fittedChapter == m_chapter && !bodyShort)
		return;

	const int needClient = (capH > 0 ? capH : 0) + pad + tabH + MphScaleDpi(6, dpi)
		+ m_contentBottom + pad + btnH + pad + safety;
	CRect rcW;
	GetWindowRect(&rcW);
	CRect rcC;
	GetClientRect(&rcC);
	int chrome = rcW.Height() - rcC.Height();
	if (chrome < 0) chrome = 0;
	int targetH = needClient + chrome;
	const int minH = MphScaleDpi(340, dpi);
	int maxH = ::GetSystemMetrics(SM_CYMAXIMIZED) - MphScaleDpi(48, dpi);
	if (maxH < minH) maxH = minH + MphScaleDpi(200, dpi);
	if (targetH < minH) targetH = minH;
	if (targetH > maxH) targetH = maxH;

	// 狭めすぎ防止: 現在より極端に縮めない（初回の見切れ→縮小ループ対策）
	if (targetH + MphScaleDpi(8, dpi) < rcW.Height() && m_fittedChapter < 0
		&& !bodyShort && rcW.Height() <= maxH) {
		const int floorH = MphScaleDpi(400, dpi) + chrome;
		if (targetH < floorH && floorH <= maxH)
			targetH = floorH;
	}

	if (abs(targetH - rcW.Height()) < MphScaleDpi(4, dpi) && !bodyShort) {
		m_fittedChapter = m_chapter;
		return;
	}
	m_fittedChapter = m_chapter;
	SetWindowPos(NULL, 0, 0, rcW.Width(), targetH,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	LayoutChrome();
	if (!m_bodyRc.IsRectEmpty() && m_bodyRc.Height() + 4 < m_contentBottom) {
		// まだ足りなければ次の WM_PAINT で再試行
		m_fittedChapter = -1;
		Invalidate(FALSE);
	}
}

void CMpHelpDlg::OnTabSelChange(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	const int sel = m_tabs.GetCurSel();
	if (sel >= 0 && sel < kChapterN)
		m_chapter = sel;
	m_fittedChapter = -1; // 章切替で高さを再フィット
	// Soft3D 実演は全章で回す（概要/再生/リストも Soft3D デモあり）
	if (!m_timer)
		m_timer = SetTimer(kAnimTimerId, 33, nullptr);
	if (m_chapter == 3) {
		// 本体側でもツアーヒントを流す(バナーが Soft3D のときだけ効く)
		if (m_ownerMp && ::IsWindow(m_ownerMp->GetSafeHwnd()))
			m_ownerMp->PostMessage(WM_MP_HELP_HIGHLIGHT, 1, 1);
	}
	if (!m_bodyRc.IsRectEmpty())
		InvalidateRect(&m_bodyRc, FALSE);
	if (pResult) *pResult = 0;
}

void CMpHelpDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != kAnimTimerId) {
		CCustomBlurDialogExBase::OnTimer(nIDEvent);
		return;
	}
	++m_animTick;
	m_demoCam.yawDeg += 0.7f;
	GdiSoft3D::ClampCam(m_demoCam);
	if (!m_bodyRc.IsRectEmpty())
		InvalidateRect(&m_bodyRc, FALSE);
}

BOOL CMpHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	// キャプション帯のアクリル源を潰さないため、塗りは基底に任せる
	return CCustomBlurDialogExBase::OnEraseBkgnd(pDC);
}

void CMpHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CRect rcClient;
	GetClientRect(&rcClient);

#if CCUSTOM_AERO_SUPPORT
	// アクリル(Win11)時は子の隙間だけクロマクリア。本文は直後に不透明で上書きするので除外。
	if (savedata.aero == 1 && CCC_IsWin11()) {
		const int saved = pdc.SaveDC();
		if (!m_bodyRc.IsRectEmpty())
			pdc.ExcludeClipRect(&m_bodyRc);
		CCC_PaintAeroGaps(pdc, this, nullptr);
		pdc.RestoreDC(saved);
	}
#endif

	const int bw = m_bodyRc.Width();
	const int bh = m_bodyRc.Height();
	if (bw < 40 || bh < 40) {
		CCC_CaptionPaint(pdc, m_hWnd);
		return;
	}

	if (m_mem.GetSafeHdc() == NULL)
		m_mem.CreateCompatibleDC(&pdc);
	if (m_memW != bw || m_memH != bh || m_memBmp.GetSafeHandle() == NULL) {
		if (m_memOldBmp) {
			m_mem.SelectObject(m_memOldBmp);
			m_memOldBmp = nullptr;
		}
		if (m_memBmp.GetSafeHandle())
			m_memBmp.DeleteObject();
		if (!m_memBmp.CreateCompatibleBitmap(&pdc, bw, bh)) {
			CCC_CaptionPaint(pdc, m_hWnd);
			return;
		}
		m_memOldBmp = m_mem.SelectObject(&m_memBmp);
		m_memW = bw;
		m_memH = bh;
	}

	CDC& dc = m_mem;
	dc.FillSolidRect(0, 0, bw, bh, RGB(248, 248, 252));
	dc.SetBkMode(TRANSPARENT);

	CFont* baseFont = GetFont();
	CFont boldFont;
	CFont accentFont;
	{
		LOGFONT lf = {};
		if (baseFont && baseFont->GetSafeHandle())
			baseFont->GetLogFont(&lf);
		else {
			NONCLIENTMETRICS ncm = {};
			ncm.cbSize = sizeof(ncm);
			::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
			lf = ncm.lfMessageFont;
		}
		lf.lfWeight = FW_BOLD;
		boldFont.CreateFontIndirect(&lf);
		lf.lfWeight = FW_SEMIBOLD;
		accentFont.CreateFontIndirect(&lf);
	}
	CFont* oldFont = dc.SelectObject(baseFont);

	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 3;
	CBrush frameBrush(RGB(132, 132, 152));

	auto title = [&](int x, int y, LPCTSTR t) {
		CFont* prev = dc.SelectObject(&boldFont);
		dc.SetTextColor(RGB(72, 48, 120));
		dc.TextOut(x, y, t);
		dc.SelectObject(prev);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(52, 52, 68));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 120));
		dc.TextOut(x, y, t);
	};
	// 先頭の「・Name ……」を色付き＋やや太字で、続きは通常色
	auto bulletNamed = [&](int x, int y, COLORREF nameCol, LPCTSTR name, LPCTSTR rest) {
		CFont* prev = dc.SelectObject(&accentFont);
		dc.SetTextColor(nameCol);
		CString head;
		head.Format(L"・%s", name);
		dc.TextOut(x, y, head);
		CSize sz = dc.GetTextExtent(head);
		dc.SelectObject(prev);
		dc.SetTextColor(RGB(52, 52, 68));
		dc.TextOut(x + sz.cx, y, rest);
	};

	const int L = 10;
	int y = 8;
	m_contentBottom = 0;

	for (int i = 0; i < kMapRegionN; ++i)
		m_mapRc[i].SetRectEmpty();

	if (m_chapter == 0) {
		title(L, y, LL14(
			L"全体の地図", L"Map of the window", L"Carte de la fenêtre", L"Mappa della finestra", L"Mapa de la ventana",
			L"창 전체 지도", L"窗口整体地图", L"خريطة النافذة",
			L"Карта окна", L"Karte des Fensters", L"Mapa da janela", L"Kaart van het venster",
			L"Mapa okna", L"Pencere haritası"));
		y += titleLh;
		muted(L, y, LL14(
			L"はじめての方へ: 下の色つき帯（Lib〜Tools）を押すと、プレイヤー本体の該当場所が光ります。配置を覚えてから各タブへ。",
			L"First time? Click a colored block (Lib–Tools) to flash that part of the player. Learn the layout, then open other tabs.",
			L"Débutant ? Cliquez un bloc coloré (Lib–Tools) pour faire clignoter la partie. Apprenez la carte, puis les onglets.",
			L"Prima volta? Clicca un blocco colorato (Lib–Tools) per far lampeggiare la parte. Poi le altre schede.",
			L"¿Primera vez? Haga clic en un bloque (Lib–Tools) para iluminar esa parte. Luego las otras pestañas.",
			L"처음이라면: 아래 색 띠(Lib~Tools)를 누르면 본체의 해당 부분이 빛납니다. 배치를 익힌 뒤 다른 탭으로.",
			L"初次使用：点击下方色块（Lib–Tools）会高亮播放器对应位置。熟悉布局后再看其他页签。",
			L"لأول مرة: انقر شريطاً ملوّناً (Lib–Tools) ليضيء الجزء المقابل. ثم باقي التبويبات.",
			L"Впервые? Нажмите цветной блок (Lib–Tools) — часть плеера мигнёт. Потом другие вкладки.",
			L"Neu hier? Farbblock (Lib–Tools) anklicken → passender Teil blinkt. Dann andere Register.",
			L"Primeira vez? Clique num bloco (Lib–Tools) para piscar a parte. Depois as outras abas.",
			L"Eerste keer? Klik een gekleurd blok (Lib–Tools) om dat deel te laten knipperen. Daarna andere tabbladen.",
			L"Pierwszy raz? Kliknij kolorowy blok (Lib–Tools), by podświetlić część. Potem inne karty.",
			L"İlk kez misiniz? Renkli şeride (Lib–Tools) tıklayın; ilgili bölüm parlar. Sonra diğer sekmeler."));
		y += lh;
		muted(L, y, LL14(
			L"その下: 左は黄→青→緑→紫の手順コマ（中の立体＝Soft3D）、右は大きめの自動実演。触らなくても回ります。",
			L"Below: left yellow→purple step frames (Soft 3D inside), right = larger auto demo. They spin without touching.",
			L"Dessous: cases jaune→violet (Soft 3D), droite = grande démo auto. Elles tournent seules.",
			L"Sotto: riquadri giallo→viola (Soft 3D), destra = demo auto. Girano da sole.",
			L"Abajo: paneles amarillo→violeta (Soft 3D), derecha = demo auto. Giran solas.",
			L"아래: 왼쪽 노랑→보라 단계칸(안 Soft3D), 오른쪽=큰 자동 실연. 안 만져도 돕니다.",
			L"下方：左为黄→紫步骤格（内为 Soft3D），右为较大自动演示。无需操作也会转。",
			L"أسفل: لوحات أصفر→بنفسجي (Soft3D)، يميناً عرض تلقائي أكبر. تدور وحدها.",
			L"Ниже: слева жёлт.→фиол. шаги (Soft 3D), справа крупное автодемо. Крутятся сами.",
			L"Unten: links Gelb→Violett-Schritte (Soft 3D), rechts große Auto-Demo. Laufen von allein.",
			L"Abaixo: painéis amarelo→roxo (Soft 3D), direita = demo auto. Giram sozinhas.",
			L"Eronder: geel→paarse stappen (Soft 3D), rechts grotere autodemo. Draaien vanzelf.",
			L"Poniżej: żółty→fiolet kroki (Soft 3D), prawo = większe autodemo. Kręcą się same.",
			L"Altta: sarı→mor adım kutuları (Soft 3B), sağ = büyük otomatik demo. Dokunmadan döner."));
		y += lh + 4;

		{
			const int gw = min(600, bw - L * 2);
			const int gh = lh * 3 + 12;
			const int gx = L, gy = y;
			dc.FillSolidRect(gx, gy, gw, gh, RGB(243, 244, 249));
			const int inner = gw - 12;
			const int wgt[kMapRegionN] = { 9, 20, 12, 13, 14, 16, 12 };
			const COLORREF col[kMapRegionN] = {
				RGB(160, 195, 240), RGB(255, 210, 160), RGB(130, 205, 140), RGB(240, 210, 160),
				RGB(220, 190, 245), RGB(255, 180, 120), RGB(180, 220, 200)
			};
			LPCTSTR lab[kMapRegionN] = { L"Lib", L"Banner", L"Play", L"Sound", L"List", L"Lyrics", L"Tools" };
			int total = 0;
			for (int i = 0; i < kMapRegionN; ++i) total += wgt[i];
			int cx = gx + 6;
			for (int i = 0; i < kMapRegionN; ++i) {
				int cw = inner * wgt[i] / total;
				if (i == kMapRegionN - 1) cw = gx + 6 + inner - cx;
				if (cw < 10) cw = 10;
				dc.FillSolidRect(cx, gy + 6, cw - 4, gh - 12, col[i]);
				dc.SetTextColor(RGB(34, 34, 50));
				dc.TextOut(cx + 5, gy + 6 + (gh - 12 - lh) / 2, lab[i]);
				m_mapRc[i].SetRect(m_bodyRc.left + cx, m_bodyRc.top + gy + 6,
					m_bodyRc.left + cx + cw - 4, m_bodyRc.top + gy + gh - 6);
				cx += cw;
			}
			dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
			y = gy + gh + 8;
		}

		y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, bw - L * 2, min(150, max(120, bh / 4)),
			CCC_HELPDEMO_KSPECTRUM);

		bulletNamed(L, y, RGB(40, 110, 180), L"Lib", LL14(
			L" …… 左レール。ライブラリ／履歴／一時リストの開閉。狭いときはアイコンだけになります",
			L" …… left rail: library, history and temp list. When narrow, icons only",
			L" …… rail gauche : bibliothèque, historique, liste temporaire",
			L" …… barra sinistra: libreria, cronologia, lista temporanea",
			L" …… riel izquierdo: biblioteca, historial, lista temporal",
			L" …… 왼쪽 레일. 라이브러리·기록·임시 목록. 좁으면 아이콘만",
			L" …… 左侧栏：库、历史、临时列表。变窄时只显示图标",
			L" …… الشريط الأيسر: المكتبة والسجل والقائمة المؤقتة",
			L" …… левая рейка: библиотека, история, временный список",
			L" …… linke Leiste: Bibliothek, Verlauf, Temp-Liste",
			L" …… trilho esquerdo: biblioteca, histórico, lista temporária",
			L" …… linkerrail: bibliotheek, geschiedenis, tijdelijke lijst",
			L" …… lewa szyna: biblioteka, historia, lista tymczasowa",
			L" …… sol ray: kitaplık, geçmiş, geçici liste")); y += lh;
		muted(L + 14, y, LL14(
			L"よく聴く曲やフォルダをここにまとめておくと、リストへすぐ足せます。",
			L"Pin frequent tracks/folders here so you can add them to the list quickly.",
			L"Épinglez ici vos titres/dossiers fréquents pour les ajouter vite.",
			L"Fissa qui brani/cartelle frequenti per aggiungerli in fretta.",
			L"Fije aquí pistas/carpetas frecuentes para añadirlas rápido.",
			L"자주 듣는 곡·폴더를 두면 목록에 바로 넣을 수 있습니다.",
			L"把常听曲目/文件夹放这里，可快速加到列表。",
			L"ثبّت المقاطع/المجلدات المتكررة هنا لإضافتها بسرعة.",
			L"Закрепите частые треки/папки — быстрее добавлять в список.",
			L"Häufige Titel/Ordner hier pinnen — schneller in die Liste.",
			L"Fixe faixas/pastas frequentes aqui para adicionar rápido.",
			L"Pin frequente tracks/mappen hier om snel toe te voegen.",
			L"Przypnij tu częste utwory/foldery — szybciej do listy.",
			L"Sık dinlenenleri buraya sabitleyin; listeye hızlı eklenir.")); y += lh;
		bulletNamed(L, y, RGB(200, 120, 40), L"Banner", LL14(
			L" …… スペアナ＋曲情報。幅を広げると左にジャケット、右に曲名などが分離します",
			L" …… spectrum + track info. Widen to split jacket left and info right",
			L" …… spectre + infos. Élargir sépare pochette et infos",
			L" …… spettro + info. Allargando si separano copertina e info",
			L" …… espectro + info. Al ensanchar separa carátula e info",
			L" …… 스펙트럼+곡 정보. 넓히면 자켓과 정보가 분리",
			L" …… 频谱+曲目信息。加宽后分离封面与信息",
			L" …… طيف + معلومات. التوسيع يفصل الغلاف والمعلومات",
			L" …… спектр + инфо. Шире — обложка и инфо отдельно",
			L" …… Spektrum + Info. Breiter trennt Cover und Info",
			L" …… espectro + info. Alargue para separar capa e info",
			L" …… spectrum + info. Breder scheidt omslag en info",
			L" …… widmo + info. Szersze rozdziela okładkę i info",
			L" …… spektrum + bilgi. Genişletince kapak ve bilgi ayrılır")); y += lh;
		muted(L + 14, y, LL14(
			L"右クリックでスペアナ様式や 2D↔簡易3D を選べます（Soft3D タブ参照）。",
			L"Right-click for spectrum style and 2D↔Soft 3D (see Soft3D tab).",
			L"Clic droit : style de spectre et 2D↔Soft 3D (onglet Soft3D).",
			L"Destro: stile spettro e 2D↔Soft 3D (scheda Soft3D).",
			L"Clic der.: estilo de espectro y 2D↔Soft 3D (pestaña Soft3D).",
			L"우클릭으로 스펙트럼 양식과 2D↔간이3D( Soft3D 탭).",
			L"右键可选频谱样式与 2D↔简易3D（见 Soft3D 页）。",
			L"يمين: نمط الطيف و2D↔Soft3D (تبويب Soft3D).",
			L"ПКМ: стиль спектра и 2D↔Soft 3D (вкладка Soft3D).",
			L"Rechtsklick: Spektrumstil und 2D↔Soft 3D (Register Soft3D).",
			L"Direito: estilo do espectro e 2D↔Soft 3D (aba Soft3D).",
			L"Rechtsklik: spectrumstijl en 2D↔Soft 3D (tab Soft3D).",
			L"PPM: styl widma i 2D↔Soft 3D (karta Soft3D).",
			L"Sağ tık: spektrum stili ve 2D↔Soft 3B (Soft3D sekmesi).")); y += lh;
		bulletNamed(L, y, RGB(40, 140, 70), L"Play / Sound", LL14(
			L" …… 再生ボタン列とシークバー、音量・テンポ・ピッチ。ラベルをダブルクリックで既定値",
			L" …… transport, seek bar, volume/tempo/pitch. Double-click labels to reset",
			L" …… transport, barre de seek, volume/tempo/hauteur. Double-clic = défaut",
			L" …… trasporto, seek, volume/tempo/pitch. Doppio clic = default",
			L" …… transporte, seek, volumen/tempo/tono. Doble clic = defecto",
			L" …… 재생 버튼과 시크바, 음량·템포·피치. 라벨 더블클릭=기본",
			L" …… 播放按钮与进度条、音量/速度/音高。双击标签恢复默认",
			L" …… أزرار التشغيل وseek والصوت/التمبو/النغمة. نقر مزدوج=افتراضي",
			L" …… кнопки, seek, громкость/темп/высота. Двойной клик — сброс",
			L" …… Transport, Seek, Lautstärke/Tempo/Tonhöhe. Doppelklick = Standard",
			L" …… transporte, seek, volume/tempo/tom. Duplo clique = padrão",
			L" …… transport, seek, volume/tempo/toonhoogte. Dubbelklik = standaard",
			L" …… transport, seek, głośność/tempo/wysokość. Dwuklik = domyślne",
			L" …… transport, seek, ses/tempo/perde. Çift tık = varsayılan")); y += lh;
		bulletNamed(L, y, RGB(120, 70, 180), L"List / Lyrics", LL14(
			L" …… プレイリスト表と歌詞。歌詞はデスクトップ窓にも出せます",
			L" …… playlist table and lyrics. Lyrics can pop to a desktop window",
			L" …… table de lecture et paroles (fenêtre séparée possible)",
			L" …… tabella e testi (finestra separata possibile)",
			L" …… tabla y letras (ventana aparte posible)",
			L" …… 플레이리스트 표와 가사. 가사는 데스크톱 창으로도 표시",
			L" …… 播放列表与歌词。歌词可弹出桌面窗口",
			L" …… جدول القائمة والكلمات (نافذة مستقلة ممكنة)",
			L" …… таблица и текст (можно отдельным окном)",
			L" …… Playlist-Tabelle und Text (eigenes Fenster möglich)",
			L" …… tabela e letras (janela separada possível)",
			L" …… lijst en songtekst (apart venster mogelijk)",
			L" …… tabela i tekst (osobne okno możliwe)",
			L" …… liste ve şarkı sözü (ayrı pencere olabilir)")); y += lh;
		bulletNamed(L, y, RGB(30, 140, 120), L"Tools", LL14(
			L" …… EQ・ピアノロール・アナライザー・プロンプト等。各窓にも簡易3Dや Soft3D 飾りがあります",
			L" …… EQ, piano roll, analyzer, prompt. Those windows also have Soft 3D views/accents",
			L" …… EQ, piano roll, analyseur, prompt (Soft 3D aussi)",
			L" …… EQ, piano roll, analizzatore, prompt (anche Soft 3D)",
			L" …… EQ, piano roll, analizador, prompt (también Soft 3D)",
			L" …… EQ·피아노롤·애널라이저·프롬프트(각 창에도 Soft 3D)",
			L" …… EQ、钢琴卷帘、分析器、提示（各窗也有 Soft 3D）",
			L" …… EQ والبيانو والمحلل والبرومبت (وفيها Soft 3D أيضاً)",
			L" …… EQ, piano roll, анализатор, промпт (там тоже Soft 3D)",
			L" …… EQ, Piano Roll, Analyzer, Prompt (auch Soft 3D)",
			L" …… EQ, piano roll, analisador, prompt (também Soft 3D)",
			L" …… EQ, pianorol, analyser, prompt (ook Soft 3D)",
			L" …… EQ, piano roll, analizator, prompt (też Soft 3D)",
			L" …… EQ, piano roll, analizör, prompt (onlarda da Soft 3B)")); y += lh + 4;
		muted(L, y, LL14(
			L"キャプションの ? か「?」キーでいつでもこのガイドを開けます。Ctrl+K でコマンドパレットも使えます。",
			L"Open this guide any time from the caption ? button or the ? key. Ctrl+K opens the command palette.",
			L"Ouvrez ce guide via le bouton ? ou la touche ?. Ctrl+K = palette de commandes.",
			L"Apri questa guida dal pulsante ? o dal tasto ?. Ctrl+K = palette comandi.",
			L"Abra esta guía con el botón ? o la tecla ?. Ctrl+K = paleta de comandos.",
			L"캡션의 ? 버튼이나 ? 키로 언제든 열 수 있습니다. Ctrl+K 로 명령 팔레트.",
			L"可随时通过标题栏 ? 按钮或 ? 键打开本指南。Ctrl+K 打开命令面板。",
			L"افتح هذا الدليل من زر ? أو مفتاح ?. Ctrl+K لوحة الأوامر.",
			L"Открыть это руководство: кнопка ? или клавиша ?. Ctrl+K — палитра команд.",
			L"Diese Anleitung über ? in der Titelleiste oder Taste ? öffnen. Ctrl+K = Befehlspalette.",
			L"Abra este guia pelo botão ? ou pela tecla ?. Ctrl+K = paleta de comandos.",
			L"Open deze gids via de ?-knop of de ?-toets. Ctrl+K = commandopalet.",
			L"Otwórz przewodnik przyciskiem ? lub klawiszem ?. Ctrl+K = paleta poleceń.",
			L"Bu kılavuzu ? düğmesi veya ? tuşu ile açın. Ctrl+K = komut paleti."));
	}
	else if (m_chapter == 1) {
		title(L, y, LL14(
			L"再生 / シーク / A-B", L"Play / Seek / A-B", L"Lecture / Seek / A-B", L"Play / Seek / A-B",
			L"Play / Seek / A-B", L"재생 / 시크 / A-B", L"播放 / 定位 / A-B", L"تشغيل / Seek / A-B",
			L"Play / Seek / A-B", L"Play / Seek / A-B", L"Play / Seek / A-B", L"Play / Seek / A-B",
			L"Play / Seek / A-B", L"Play / Seek / A-B"));
		y += titleLh;
		y = CCC_GdiHelpDrawSoft3DDemo(dc, L, y, min(280, bw - L * 2), min(110, max(72, bh / 5)),
			CCC_HELPDEMO_KTRANSPORT);
		body(L, y, LL14(
			L"・▶ ⏸ ■ |◀ ▶| …… 再生・一時停止・停止・前後の曲",
			L"· ▶ ⏸ ■ |◀ ▶| …… play, pause, stop, previous/next track",
			L"· ▶ ⏸ ■ …… lire, pause, stop, précédent/suivant",
			L"· ▶ ⏸ ■ …… play, pausa, stop, precedente/successivo",
			L"· ▶ ⏸ ■ …… play, pausa, stop, anterior/siguiente",
			L"· ▶ ⏸ ■ …… 재생·일시정지·정지·이전/다음 곡",
			L"· ▶ ⏸ ■ …… 播放、暂停、停止、上一/下一曲",
			L"· ▶ ⏸ ■ …… تشغيل وإيقاف مؤقت وإيقاف وسابق/تالٍ",
			L"· ▶ ⏸ ■ …… play, пауза, стоп, пред./след.",
			L"· ▶ ⏸ ■ …… Play, Pause, Stop, zurück/weiter",
			L"· ▶ ⏸ ■ …… play, pausa, parar, anterior/próxima",
			L"· ▶ ⏸ ■ …… play, pauze, stop, vorige/volgende",
			L"· ▶ ⏸ ■ …… play, pauza, stop, poprz./nast.",
			L"· ▶ ⏸ ■ …… play, duraklat, dur, önceki/sonraki")); y += lh;
		body(L, y, LL14(
			L"・シークバー …… ピンク帯=ループ区間、青つまみ=A-B。左のロックでループ固定",
			L"· Seek bar …… pink band = loop, blue thumbs = A-B. Lock pins the loop",
			L"· Barre de seek …… rose = boucle, bleu = A-B. Le verrou fixe la boucle",
			L"· Barra seek …… rosa = loop, blu = A-B. Il lucchetto fissa il loop",
			L"· Barra seek …… rosa = bucle, azul = A-B. El candado fija el bucle",
			L"· 시크바 …… 분홍=루프 구간, 파랑=A-B. 잠금으로 루프 고정",
			L"· 进度条 …… 粉色=循环区间，蓝色=A-B。锁定可固定循环",
			L"· شريط seek …… وردي=حلقة، أزرق=A-B. القفل يثبت الحلقة",
			L"· Полоса seek …… розовый=цикл, синий=A-B. Замок фиксирует цикл",
			L"· Seek-Leiste …… rosa=Loop, blau=A-B. Sperre fixiert den Loop",
			L"· Barra seek …… rosa=loop, azul=A-B. A trava fixa o loop",
			L"· Seekbalk …… roze=lus, blauw=A-B. Slot zet de lus vast",
			L"· Pasek seek …… róż=pętla, niebieski=A-B. Blokada przypina pętlę",
			L"· Seek çubuğu …… pembe=döngü, mavi=A-B. Kilit döngüyü sabitler")); y += lh;
		body(L, y, LL14(
			L"・A / B / A-B解除 …… 区間ループ。R で現在位置±フレーズ秒を一発 A-B",
			L"· A / B / Clear …… section loop. R sets A-B around the current spot",
			L"· A / B / Effacer …… boucle. R crée un A-B autour du point courant",
			L"· A / B / Cancella …… loop. R crea A-B intorno al punto corrente",
			L"· A / B / Borrar …… bucle. R crea A-B alrededor del punto actual",
			L"· A / B / 해제 …… 구간 루프. R 로 현재 위치±프레이즈 A-B",
			L"· A / B / 清除 …… 区间循环。R 以当前位置±乐句设 A-B",
			L"· A / B / مسح …… حلقة مقطع. R يضبط A-B حول الموضع الحالي",
			L"· A / B / Сброс …… петля. R ставит A-B вокруг текущей точки",
			L"· A / B / Aus …… Abschnittsloop. R setzt A-B um die aktuelle Stelle",
			L"· A / B / Limpar …… loop. R define A-B ao redor do ponto atual",
			L"· A / B / Wissen …… sectielus. R zet A-B rond de huidige plek",
			L"· A / B / Wyczyść …… pętla. R ustawia A-B wokół bieżącego miejsca",
			L"· A / B / Sil …… bölüm döngüsü. R geçerli noktada A-B kurar")); y += lh;
		body(L, y, LL14(
			L"・水色の目盛り …… LRC の時刻マーカー。クリックでその行へシーク",
			L"· Cyan ticks …… LRC time marks. Click one to seek to that line",
			L"· Traits cyan …… marques LRC. Cliquez pour aller à la ligne",
			L"· Tacche ciano …… marche LRC. Clicca per andare alla riga",
			L"· Marcas cian …… marcas LRC. Clic para ir a esa línea",
			L"· 청록 눈금 …… LRC 시각 마커. 클릭하면 해당 행으로 시크",
			L"· 青色刻度 …… LRC 时刻标记。点击定位到该行",
			L"· علامات سماوية …… مؤشرات LRC. انقر للانتقال للسطر",
			L"· Голубые метки …… метки LRC. Клик — переход к строке",
			L"· Cyan-Striche …… LRC-Marken. Klick springt zur Zeile",
			L"· Marcas ciano …… marcas LRC. Clique para ir à linha",
			L"· Cyaan streepjes …… LRC-markeringen. Klik om te gaan",
			L"· Cyjanowe znaczniki …… znaczniki LRC. Klik przenosi",
			L"· Camgöbeği çentikler …… LRC işaretleri. Tıkla, satıra git")); y += lh;
		body(L, y, LL14(
			L"・音量 / テンポ / ピッチ …… スライダー。ラベルのダブルクリックで既定へ",
			L"· Volume / tempo / pitch …… sliders. Double-click the label to reset",
			L"· Volume / tempo / hauteur …… curseurs. Double-clic = défaut",
			L"· Volume / tempo / pitch …… slider. Doppio clic = default",
			L"· Volumen / tempo / tono …… deslizadores. Doble clic = defecto",
			L"· 음량 / 템포 / 피치 …… 슬라이더. 라벨 더블클릭으로 기본값",
			L"· 音量 / 速度 / 音高 …… 滑块。双击标签恢复默认",
			L"· الصوت / التمبو / النغمة …… منزلقات. نقر مزدوج = الافتراضي",
			L"· Громкость / темп / высота …… ползунки. Двойной клик — сброс",
			L"· Lautstärke / Tempo / Tonhöhe …… Slider. Doppelklick = Standard",
			L"· Volume / tempo / tom …… sliders. Duplo clique = padrão",
			L"· Volume / tempo / toonhoogte …… sliders. Dubbelklik = standaard",
			L"· Głośność / tempo / wysokość …… suwaki. Dwuklik = domyślne",
			L"· Ses / tempo / perde …… kaydırıcılar. Çift tık = varsayılan")); y += lh;
		body(L, y, LL14(
			L"・フェードアウト …… 押すと数秒でそっと停止。スリープ予約もツール▾から",
			L"· Fade-out …… stops gently over a few seconds. Sleep timer is under Tools ▾",
			L"· Fondu …… arrêt doux en quelques secondes. Sleep dans Outils ▾",
			L"· Fade-out …… stop morbido in pochi secondi. Sleep in Strumenti ▾",
			L"· Fundido …… paro suave en segundos. Sleep en Herramientas ▾",
			L"· 페이드아웃 …… 몇 초에 걸쳐 정지. 슬립 예약은 도구 ▾",
			L"· 淡出 …… 数秒内轻柔停止。睡眠定时在工具 ▾",
			L"· تلاشٍ …… إيقاف لطيف في ثوانٍ. مؤقت النوم في الأدوات ▾",
			L"· Затухание …… мягкий стоп за секунды. Sleep — в Tools ▾",
			L"· Fade-out …… sanfter Stopp. Sleep unter Tools ▾",
			L"· Fade-out …… paragem suave. Sleep em Ferramentas ▾",
			L"· Fade-out …… zachte stop. Sleep onder Tools ▾",
			L"· Wygaszanie …… łagodny stop. Sleep w Narzędzia ▾",
			L"· Fade-out …… yumuşak duruş. Sleep, Araçlar ▾ altında")); y += lh + 4;
		muted(L, y, LL14(
			L"バナーを右クリックするとスペアナ様式(バー/ミラー/波形)と 2D↔3D を選べます。",
			L"Right-click the banner to pick the spectrum style (bar/mirror/wave) and 2D↔3D.",
			L"Clic droit sur la bannière : style de spectre et 2D↔3D.",
			L"Destro sul banner: stile spettro e 2D↔3D.",
			L"Clic derecho en el banner: estilo de espectro y 2D↔3D.",
			L"배너 우클릭으로 스펙트럼 양식과 2D↔3D 선택.",
			L"右键横幅可选频谱样式与 2D↔3D。",
			L"يمين على البانر: نمط الطيف و2D↔3D.",
			L"ПКМ по баннеру: стиль спектра и 2D↔3D.",
			L"Rechtsklick auf das Banner: Spektrumstil und 2D↔3D.",
			L"Direito no banner: estilo do espectro e 2D↔3D.",
			L"Rechtsklik op de banner: spectrumstijl en 2D↔3D.",
			L"PPM na banerze: styl widma i 2D↔3D.",
			L"Banner'a sağ tık: spektrum stili ve 2D↔3B."));
	}
	else if (m_chapter == 2) {
		title(L, y, LL14(
			L"プレイリストとライブラリ", L"Playlist and library", L"Liste et bibliothèque", L"Playlist e libreria",
			L"Lista y biblioteca", L"플레이리스트와 라이브러리", L"播放列表与库", L"القائمة والمكتبة",
			L"Плейлист и библиотека", L"Playlist und Bibliothek", L"Playlist e biblioteca", L"Lijst en bibliotheek",
			L"Playlista i biblioteka", L"Liste ve kitaplık"));
		y += titleLh;
		y = CCC_GdiHelpDrawSoft3DDemo(dc, L, y, min(280, bw - L * 2), min(110, max(72, bh / 5)),
			CCC_HELPDEMO_KLIST);
		body(L, y, LL14(
			L"・ダブルクリックで再生。ドラッグで並べ替え、Delete で行を外す",
			L"· Double-click to play. Drag to reorder, Delete removes a row",
			L"· Double-clic = lire. Glisser = réordonner, Suppr = retirer",
			L"· Doppio clic = riproduci. Trascina = riordina, Canc = rimuovi",
			L"· Doble clic = reproducir. Arrastrar = reordenar, Supr = quitar",
			L"· 더블클릭으로 재생. 드래그로 정렬, Delete 로 행 제거",
			L"· 双击播放。拖动重排，Delete 移除行",
			L"· نقر مزدوج للتشغيل. اسحب لإعادة الترتيب، Delete للإزالة",
			L"· Двойной клик — играть. Тянуть — порядок, Delete — убрать",
			L"· Doppelklick spielt. Ziehen sortiert, Entf entfernt",
			L"· Duplo clique toca. Arraste reordena, Delete remove",
			L"· Dubbelklik speelt. Slepen herordent, Delete verwijdert",
			L"· Dwuklik odtwarza. Przeciąganie zmienia kolejność, Delete usuwa",
			L"· Çift tık çalar. Sürükle sırala, Delete satırı kaldırır")); y += lh;
		body(L, y, LL14(
			L"・列見出しクリックで曲名／アーティスト／アルバム／時間で並べ替え",
			L"· Click a column header to sort by title, artist, album or time",
			L"· Clic sur l'en-tête : titre, artiste, album, durée",
			L"· Clic sull'intestazione: titolo, artista, album, durata",
			L"· Clic en el encabezado: título, artista, álbum, duración",
			L"· 열 머리글 클릭으로 제목·아티스트·앨범·시간 정렬",
			L"· 点击列标题按曲名/艺术家/专辑/时长排序",
			L"· انقر رأس العمود للترتيب حسب العنوان والفنان والألبوم والمدة",
			L"· Клик по заголовку столбца — сортировка",
			L"· Spaltenkopf klicken: Titel, Künstler, Album, Zeit",
			L"· Clique no cabeçalho: título, artista, álbum, duração",
			L"· Klik kolomkop: titel, artiest, album, tijd",
			L"· Klik nagłówek kolumny: tytuł, wykonawca, album, czas",
			L"· Kolon başlığına tıkla: başlık, sanatçı, albüm, süre")); y += lh;
		body(L, y, LL14(
			L"・検索欄 …… 入力で絞り込み。正規表現トグルも隣にあります",
			L"· Find box …… filters as you type. A regex toggle sits next to it",
			L"· Recherche …… filtre à la frappe. Bascule regex à côté",
			L"· Ricerca …… filtra digitando. Interruttore regex accanto",
			L"· Búsqueda …… filtra al escribir. Interruptor regex al lado",
			L"· 검색란 …… 입력하면 필터. 옆에 정규식 토글",
			L"· 搜索框 …… 输入即筛选。旁边有正则开关",
			L"· البحث …… يرشّح أثناء الكتابة. مفتاح regex بجانبه",
			L"· Поиск …… фильтр при вводе. Рядом переключатель regex",
			L"· Suchfeld …… filtert beim Tippen. Regex-Schalter daneben",
			L"· Busca …… filtra ao digitar. Alternador regex ao lado",
			L"· Zoekveld …… filtert tijdens typen. Regex-schakelaar ernaast",
			L"· Szukaj …… filtruje w trakcie pisania. Obok przełącznik regex",
			L"· Arama …… yazarken süzer. Yanında regex anahtarı")); y += lh;
		body(L, y, LL14(
			L"・ライブラリ …… フォルダを登録するとツリーとアルバム一覧から追加できます",
			L"· Library …… register folders, then add from the tree or album list",
			L"· Bibliothèque …… enregistrez des dossiers, ajoutez depuis l'arbre",
			L"· Libreria …… registra cartelle, aggiungi dall'albero o album",
			L"· Biblioteca …… registre carpetas y añada desde el árbol",
			L"· 라이브러리 …… 폴더를 등록하면 트리·앨범 목록에서 추가 가능",
			L"· 库 …… 注册文件夹后可从树或专辑列表添加",
			L"· المكتبة …… سجّل مجلدات ثم أضف من الشجرة أو الألبومات",
			L"· Библиотека …… добавьте папки, затем из дерева/альбомов",
			L"· Bibliothek …… Ordner registrieren, dann aus dem Baum hinzufügen",
			L"· Biblioteca …… registre pastas e adicione pela árvore",
			L"· Bibliotheek …… map registreren, toevoegen via de boom",
			L"· Biblioteka …… dodaj foldery, potem dodawaj z drzewa",
			L"· Kitaplık …… klasör kaydet, ağaçtan veya albümden ekle")); y += lh;
		body(L, y, LL14(
			L"・履歴 …… 直近に鳴った曲。ダブルクリックでもう一度再生",
			L"· History …… recently played tracks. Double-click to replay one",
			L"· Historique …… titres récents. Double-clic pour rejouer",
			L"· Cronologia …… brani recenti. Doppio clic per riascoltare",
			L"· Historial …… pistas recientes. Doble clic para repetir",
			L"· 기록 …… 최근 재생한 곡. 더블클릭으로 다시 재생",
			L"· 历史 …… 最近播放的曲目。双击可再次播放",
			L"· السجل …… المقاطع الأخيرة. نقر مزدوج لإعادة التشغيل",
			L"· История …… недавние треки. Двойной клик — повтор",
			L"· Verlauf …… kürzlich gespielt. Doppelklick spielt erneut",
			L"· Histórico …… faixas recentes. Duplo clique repete",
			L"· Geschiedenis …… recent gespeeld. Dubbelklik herhaalt",
			L"· Historia …… ostatnie utwory. Dwuklik odtwarza ponownie",
			L"· Geçmiş …… son çalınanlar. Çift tık yeniden çalar")); y += lh;
		body(L, y, LL14(
			L"・Up Next キュー …… 右クリックから追加。次に鳴る曲を割り込ませます",
			L"· Up Next queue …… add from the context menu to jump the running order",
			L"· File Up Next …… ajoutez via le menu contextuel",
			L"· Coda Up Next …… aggiungi dal menu contestuale",
			L"· Cola Up Next …… añada desde el menú contextual",
			L"· Up Next 큐 …… 우클릭으로 추가. 다음 곡에 끼워 넣기",
			L"· Up Next 队列 …… 右键添加，插队到下一首",
			L"· طابور Up Next …… أضف من قائمة السياق",
			L"· Очередь Up Next …… добавьте из контекстного меню",
			L"· Up-Next-Warteschlange …… über das Kontextmenü hinzufügen",
			L"· Fila Up Next …… adicione pelo menu de contexto",
			L"· Up Next-wachtrij …… voeg toe via het contextmenu",
			L"· Kolejka Up Next …… dodaj z menu kontekstowego",
			L"· Up Next kuyruğu …… bağlam menüsünden ekle")); y += lh + 4;
		muted(L, y, LL14(
			L"欠損ファイルは行に印が付きます。ツール▾の掃除で一括整理できます。",
			L"Missing files get a mark on the row; Tools ▾ can clean them in one go.",
			L"Les fichiers manquants sont marqués ; Outils ▾ nettoie en une fois.",
			L"I file mancanti sono segnati; Strumenti ▾ pulisce in blocco.",
			L"Los archivos ausentes se marcan; Herramientas ▾ limpia de golpe.",
			L"없는 파일은 행에 표시됩니다. 도구▾에서 일괄 정리 가능.",
			L"缺失文件会在行上标记；工具▾可一次清理。",
			L"الملفات المفقودة تُعلَّم؛ الأدوات ▾ تنظّف دفعة واحدة.",
			L"Отсутствующие файлы помечаются; Tools ▾ чистит сразу.",
			L"Fehlende Dateien werden markiert; Tools ▾ räumt auf.",
			L"Arquivos ausentes são marcados; Ferramentas ▾ limpa tudo.",
			L"Ontbrekende bestanden krijgen een markering; Tools ▾ ruimt op.",
			L"Brakujące pliki są oznaczane; Narzędzia ▾ czyszczą hurtem.",
			L"Eksik dosyalar işaretlenir; Araçlar ▾ topluca temizler."));
	}
	else if (m_chapter == 3) {
		title(L, y, LL14(
			L"簡易3D(Soft3D) — 全体像", L"Soft 3D — big picture", L"Soft 3D — vue d'ensemble", L"Soft 3D — panorama",
			L"Soft 3D — panorama", L"간이 3D(Soft3D) — 전체", L"简易3D(Soft3D) — 总览", L"Soft3D — نظرة عامة",
			L"Soft 3D — обзор", L"Soft 3D — Überblick", L"Soft 3D — visão geral", L"Soft 3D — overzicht",
			L"Soft 3D — przegląd", L"Soft 3B — genel bakış"));
		y += titleLh;
		muted(L, y, LL14(
			L"OpenGL／Direct3D なしの CPU 描画です。右の実演はゆっくり周回しています。下に「視点付きの窓」と「ボタン等の飾り」の両方があります。",
			L"Pure CPU drawing — no OpenGL/Direct3D. The demo on the right slowly orbits. Below: interactive Soft 3D views and Soft 3D accents on controls.",
			L"Rendu CPU pur, sans OpenGL ni Direct3D. La démo tourne. Ci-dessous : vues Soft 3D et accents sur les contrôles.",
			L"Rendering CPU puro, senza OpenGL né Direct3D. La demo ruota. Sotto: viste Soft 3D e accenti sui controlli.",
			L"Render por CPU, sin OpenGL ni Direct3D. La demo gira. Abajo: vistas Soft 3D y acentos en controles.",
			L"OpenGL·Direct3D 없이 CPU로 그립니다. 오른쪽 실연은 천천히 회전. 아래에 시점 Soft3D와 컨트롤 장식 Soft3D가 있습니다.",
			L"纯 CPU 渲染，不用 OpenGL 或 Direct3D。右侧演示缓慢环绕。下方既有可操作的 Soft3D 视图，也有控件上的 Soft3D 装饰。",
			L"عرض بالمعالج فقط بدون OpenGL أو Direct3D. العرض يدور ببطء. أدناه: مشاهد Soft3D وزخارف على عناصر التحكم.",
			L"Чистый CPU-рендер, без OpenGL и Direct3D. Демо медленно вращается. Ниже: виды Soft 3D и акценты на контролах.",
			L"Reines CPU-Rendering, kein OpenGL/Direct3D. Die Demo kreist. Unten: Soft-3D-Ansichten und Akzente an Steuerelementen.",
			L"Renderização por CPU, sem OpenGL nem Direct3D. A demo orbita. Abaixo: vistas Soft 3D e acentos nos controles.",
			L"Pure CPU-rendering, geen OpenGL of Direct3D. De demo draait. Hieronder: Soft 3D-weergaven en accenten op besturingen.",
			L"Renderowanie na CPU, bez OpenGL i Direct3D. Demo krąży. Poniżej: widoki Soft 3D i akcenty na kontrolkach.",
			L"Saf CPU çizimi; OpenGL/Direct3D yok. Demo yavaşça döner. Aşağıda: Soft 3B görünümler ve denetim süslemeleri."));
		y += lh + 6;

		// 実演: 右側に Soft3D、左側に Soft2D のストーリーボード
		const int demoW = min(240, max(140, bw / 3));
		const int demoH = min(150, max(96, bh / 3));
		const int demoX = bw - L - demoW;
		const int demoY = y;
		{
			const float boxes[1][6] = { { -1.05f, 1.05f, -0.02f, 0.70f, 0.0f, 0.90f } };
			GdiSoft3D::View v;
			GdiSoft3D::BuildView(demoW, demoH, m_demoCam, boxes, 1, v);
			if (m_demo3d.Create(demoW, demoH)) {
				m_demo3d.view = v;
				m_demo3d.depthTest = true;
				m_demo3d.depthWrite = true;
				m_demo3d.BeginFrame(RGB(12, 14, 22));
				m_demo3d.DrawGrid(-1.0f, 1.0f, 0.05f, 0.85f, 0.0f, 6, RGB(46, 52, 70));
				float levL[16], levR[16];
				for (int i = 0; i < 16; ++i) {
					const float p = (float)(m_animTick + i * 5) * 0.09f;
					levL[i] = 0.22f + 0.62f * fabsf(sinf(p));
					levR[i] = 0.22f + 0.62f * fabsf(sinf(p + 0.9f));
				}
				m_demo3d.DrawStereoBarsLR(-0.95f, 0.95f, 16, levL, levR,
					0.20f, 0.52f, 0.58f, 0.18f, RGB(80, 210, 255), RGB(255, 140, 90));
				m_demo3d.EndFrame();
				m_demo3d.Present(dc, demoX, demoY);
			}
			dc.FrameRect(CRect(demoX, demoY, demoX + demoW, demoY + demoH), &frameBrush);
			muted(demoX, demoY + demoH + 2, LL14(
				L"実演: 自動周回", L"Demo: auto orbit", L"Démo : orbite auto", L"Demo: orbita auto",
				L"Demo: órbita auto", L"실연: 자동 회전", L"演示：自动环绕", L"عرض: دوران تلقائي",
				L"Демо: авто-облёт", L"Demo: Auto-Orbit", L"Demo: órbita automática", L"Demo: auto-orbit",
				L"Demo: auto-orbita", L"Demo: otomatik dönüş"));
		}

		{
			// Soft2D で操作手順のコマ割りを描く(RMB → Soft3D → ドラッグ → ホイール → 0)
			const int sbW = demoX - L - 12;
			const int sbH = lh * 2 + 16;
			const int sbX = L, sbY = demoY;
			if (sbW > 160 && m_demo2d.Create(sbW, sbH)) {
				m_demo2d.Clear(RGB(248, 248, 252));
				const int gap = 14;
				const int cellW = (sbW - gap * (kStoryFrameN - 1)) / kStoryFrameN;
				const COLORREF cell[kStoryFrameN] = {
					RGB(255, 226, 196), RGB(206, 232, 255), RGB(214, 245, 220),
					RGB(232, 220, 250), RGB(255, 226, 226)
				};
				for (int i = 0; i < kStoryFrameN; ++i) {
					const int cxx = i * (cellW + gap);
					m_demo2d.FillRect(cxx, 0, cellW, sbH, cell[i], 255);
					m_demo2d.DrawRect(cxx, 0, cellW, sbH, RGB(150, 150, 172), 255, 1);
					if (i + 1 < kStoryFrameN) {
						const int ax = cxx + cellW + 2;
						const int ay = sbH / 2;
						m_demo2d.DrawLine(ax, ay, ax + gap - 4, ay, RGB(90, 100, 160), 255, 2);
						m_demo2d.DrawLine(ax + gap - 8, ay - 4, ax + gap - 4, ay, RGB(90, 100, 160), 255, 2);
						m_demo2d.DrawLine(ax + gap - 8, ay + 4, ax + gap - 4, ay, RGB(90, 100, 160), 255, 2);
					}
				}
				m_demo2d.Present(dc, sbX, sbY);

				LPCTSTR step[kStoryFrameN] = {
					LL14(L"右クリック", L"Right-click", L"Clic droit", L"Destro", L"Clic der.", L"우클릭", L"右键", L"يمين",
						L"ПКМ", L"Rechtsklick", L"Direito", L"Rechtsklik", L"PPM", L"Sağ tık"),
					L"Soft3D",
					LL14(L"ドラッグ", L"Drag", L"Glisser", L"Trascina", L"Arrastrar", L"드래그", L"拖动", L"سحب",
						L"Тянуть", L"Ziehen", L"Arrastar", L"Slepen", L"Przeciągnij", L"Sürükle"),
					LL14(L"ホイール", L"Wheel", L"Molette", L"Rotella", L"Rueda", L"휠", L"滚轮", L"العجلة",
						L"Колесо", L"Rad", L"Roda", L"Wiel", L"Kółko", L"Teker"),
					LL14(L"0 で戻す", L"0 resets", L"0 = reset", L"0 = reset", L"0 = reiniciar", L"0 으로 복귀", L"0 复位", L"0 للتصفير",
						L"0 — сброс", L"0 = Reset", L"0 = redefinir", L"0 = reset", L"0 = reset", L"0 = sıfırla")
				};
				LPCTSTR note[kStoryFrameN] = {
					LL14(L"バナー上", L"on banner", L"sur bannière", L"sul banner", L"en banner", L"배너 위", L"横幅上", L"على البانر",
						L"по баннеру", L"aufs Banner", L"no banner", L"op banner", L"na banerze", L"banner'da"),
					LL14(L"表示切替", L"switch view", L"changer vue", L"cambia vista", L"cambiar vista", L"표시 전환", L"切换显示", L"تبديل العرض",
						L"смена вида", L"Ansicht", L"trocar vista", L"weergave", L"zmiana widoku", L"görünüm"),
					LL14(L"視点回転", L"orbit", L"orbite", L"orbita", L"órbita", L"시점 회전", L"旋转视角", L"دوران",
						L"облёт", L"Orbit", L"órbita", L"orbit", L"orbita", L"yörünge"),
					LL14(L"拡大縮小", L"zoom", L"zoom", L"zoom", L"zoom", L"확대축소", L"缩放", L"تكبير",
						L"зум", L"Zoom", L"zoom", L"zoom", L"zoom", L"yakınlaştır"),
					LL14(L"既定視点", L"default cam", L"vue défaut", L"vista default", L"vista pred.", L"기본 시점", L"默认视角", L"الكاميرا الافتراضية",
						L"вид по умолч.", L"Standardsicht", L"câmera padrão", L"standaardcam", L"widok domyślny", L"varsayılan")
				};
				for (int i = 0; i < kStoryFrameN; ++i) {
					const int tx = sbX + i * (cellW + gap) + 5;
					dc.SetTextColor(RGB(40, 40, 60));
					dc.TextOut(tx, sbY + 3, step[i]);
					muted(tx, sbY + 3 + lh, note[i]);
				}
			}
			y = demoY + max(demoH + lh + 4, sbH + 8);
		}

		body(L, y, LL14(
			L"・バナー右クリック →「表示」→ 2D / 簡易3D。設定は次回起動にも残ります",
			L"· Right-click the banner → View → 2D / Soft 3D. The choice is remembered",
			L"· Clic droit → Affichage → 2D / 3D simplifiée. Le choix est mémorisé",
			L"· Destro → Vista → 2D / 3D semplificato. La scelta è ricordata",
			L"· Clic der. → Vista → 2D / 3D simple. La elección se recuerda",
			L"· 배너 우클릭 → 표시 → 2D / 간이 3D. 설정은 다음 실행에도 유지",
			L"· 右键横幅 → 显示 → 2D / 简易3D。设置会被记住",
			L"· يمين البانر ← العرض ← 2D / Soft3D. يُحفظ الاختيار",
			L"· ПКМ по баннеру → Вид → 2D / простой 3D. Выбор запоминается",
			L"· Rechtsklick → Ansicht → 2D / Soft 3D. Auswahl wird gespeichert",
			L"· Direito → Exibir → 2D / Soft 3D. A escolha é lembrada",
			L"· Rechtsklik → Weergave → 2D / Soft 3D. Keuze wordt bewaard",
			L"· PPM → Widok → 2D / Soft 3D. Wybór jest pamiętany",
			L"· Sağ tık → Görünüm → 2D / Soft 3B. Seçim hatırlanır")); y += lh;
		body(L, y, LL14(
			L"・左ドラッグ …… 視点をぐるりと回します(左右=向き、上下=見下ろし角)",
			L"· Left-drag …… orbit the camera (left/right = yaw, up/down = pitch)",
			L"· Glisser gauche …… orbite (gauche/droite = lacet, haut/bas = tangage)",
			L"· Trascina sinistro …… orbita (destra/sinistra = yaw, su/giù = pitch)",
			L"· Arrastrar izq. …… órbita (izq/der = yaw, arriba/abajo = pitch)",
			L"· 좌드래그 …… 시점 회전(좌우=방향, 상하=내려보는 각도)",
			L"· 左键拖动 …… 环绕视角（左右=偏航，上下=俯仰）",
			L"· سحب أيسر …… دوران الكاميرا (يمين/يسار = yaw، أعلى/أسفل = pitch)",
			L"· ЛКМ-перетаскивание …… облёт камеры (yaw/pitch)",
			L"· Linksziehen …… Kamera umkreisen (Yaw/Pitch)",
			L"· Arrastar esq. …… orbitar a câmera (yaw/pitch)",
			L"· Links slepen …… camera laten draaien (yaw/pitch)",
			L"· Przeciąganie LPM …… obrót kamery (yaw/pitch)",
			L"· Sol sürükleme …… kamerayı döndür (yaw/pitch)")); y += lh;
		body(L, y, LL14(
			L"・ホイール …… ズーム(0.35〜4.0倍)。0 キーで既定の視点に戻ります",
			L"· Wheel …… zoom (0.35x–4.0x). Press 0 to snap back to the default view",
			L"· Molette …… zoom (0,35–4,0x). Touche 0 = vue par défaut",
			L"· Rotella …… zoom (0,35–4,0x). Tasto 0 = vista predefinita",
			L"· Rueda …… zoom (0,35–4,0x). Tecla 0 = vista predeterminada",
			L"· 휠 …… 줌(0.35~4.0배). 0 키로 기본 시점 복귀",
			L"· 滚轮 …… 缩放（0.35～4.0 倍）。按 0 回到默认视角",
			L"· العجلة …… تكبير (0.35–4.0). المفتاح 0 يعيد العرض الافتراضي",
			L"· Колесо …… зум (0,35–4,0x). Клавиша 0 — вид по умолчанию",
			L"· Rad …… Zoom (0,35–4,0x). Taste 0 = Standardansicht",
			L"· Roda …… zoom (0,35–4,0x). Tecla 0 = vista padrão",
			L"· Wiel …… zoom (0,35–4,0x). Toets 0 = standaardweergave",
			L"· Kółko …… zoom (0,35–4,0x). Klawisz 0 = widok domyślny",
			L"· Teker …… yakınlaştırma (0,35–4,0x). 0 tuşu varsayılana döner")); y += lh;
		body(L, y, LL14(
			L"・右クリックのスライダー …… 向き・見下ろし角・ズームを数値で微調整",
			L"· Sliders in the context menu …… fine-tune yaw, pitch and zoom numerically",
			L"· Curseurs du menu …… réglage fin du lacet, tangage et zoom",
			L"· Slider nel menu …… regolazione fine di yaw, pitch e zoom",
			L"· Deslizadores del menú …… ajuste fino de yaw, pitch y zoom",
			L"· 우클릭 슬라이더 …… 방향·각도·줌을 수치로 미세 조정",
			L"· 右键菜单滑块 …… 用数值微调偏航、俯仰与缩放",
			L"· منزلقات القائمة …… ضبط دقيق للـyaw والpitch والتكبير",
			L"· Ползунки в меню …… точная настройка yaw/pitch/зума",
			L"· Slider im Menü …… Feinabstimmung von Yaw, Pitch, Zoom",
			L"· Sliders no menu …… ajuste fino de yaw, pitch e zoom",
			L"· Sliders in het menu …… fijnregeling yaw, pitch en zoom",
			L"· Suwaki w menu …… precyzyjna regulacja yaw/pitch/zoom",
			L"· Menüdeki kaydırıcılar …… yaw, pitch ve zoom ince ayarı")); y += lh;
		body(L, y, LL14(
			L"・ツアー …… 一定時間ゆっくり自動周回してヒントを出します(操作すると解除)",
			L"· Tour …… auto-orbits for a while and shows a hint (any input cancels it)",
			L"· Visite …… orbite auto avec une astuce (toute action l'annule)",
			L"· Tour …… orbita automatica con un suggerimento (un input annulla)",
			L"· Tour …… órbita automática con una pista (cualquier acción la cancela)",
			L"· 투어 …… 일정 시간 자동 회전하며 힌트 표시(조작하면 해제)",
			L"· 巡览 …… 自动环绕一段时间并显示提示（操作即取消）",
			L"· جولة …… دوران تلقائي مع تلميح (أي إدخال يلغيها)",
			L"· Тур …… авто-облёт с подсказкой (любое действие отменяет)",
			L"· Tour …… Auto-Orbit mit Hinweis (jede Eingabe bricht ab)",
			L"· Tour …… órbita automática com dica (qualquer ação cancela)",
			L"· Tour …… auto-orbit met hint (elke invoer stopt het)",
			L"· Tour …… automatyczne krążenie z podpowiedzią (akcja anuluje)",
			L"· Tur …… otomatik dönüş ve ipucu (herhangi bir işlem iptal eder)")); y += lh;
		body(L, y, LL14(
			L"・重いと感じたら …… 2D に戻すか窓を小さく。CPU 描画なので面積に比例します",
			L"· If it feels heavy …… go back to 2D or shrink the window; cost scales with area",
			L"· Si c'est lourd …… revenez en 2D ou réduisez la fenêtre",
			L"· Se è pesante …… torna al 2D o riduci la finestra",
			L"· Si va lento …… vuelva a 2D o reduzca la ventana",
			L"· 무겁게 느껴지면 …… 2D로 돌리거나 창을 줄이세요(면적 비례)",
			L"· 若觉得卡 …… 回到 2D 或缩小窗口（开销随面积增长）",
			L"· إن كان ثقيلاً …… عد إلى 2D أو صغّر النافذة",
			L"· Если тяжело …… вернитесь в 2D или уменьшите окно",
			L"· Wenn es zäh wirkt …… zurück zu 2D oder Fenster verkleinern",
			L"· Se pesar …… volte ao 2D ou diminua a janela",
			L"· Voelt het zwaar …… terug naar 2D of venster kleiner",
			L"· Jeśli ciężko …… wróć do 2D lub zmniejsz okno",
			L"· Ağır gelirse …… 2B'ye dön veya pencereyi küçült")); y += lh + 6;

		title(L, y, LL14(
			L"ほかの窓でも Soft3D", L"Soft 3D in other windows", L"Soft 3D ailleurs", L"Soft 3D in altre finestre",
			L"Soft 3D en otras ventanas", L"다른 창의 Soft3D", L"其他窗口的 Soft3D", L"Soft3D في نوافذ أخرى",
			L"Soft 3D в других окнах", L"Soft 3D in anderen Fenstern", L"Soft 3D noutras janelas", L"Soft 3D in andere vensters",
			L"Soft 3D w innych oknach", L"Diğer pencerelerde Soft 3B"));
		y += titleLh;
		bulletNamed(L, y, RGB(40, 120, 180), L"アナライザー", LL14(
			L" …… 右クリック「表示」→ 簡易3D。上下ペインをそれぞれ 2D/3D にできます",
			L" …… right-click View → Soft 3D. Upper/lower panes can be 2D/3D independently",
			L" …… clic droit Affichage → Soft 3D. Panneaux haut/bas indépendants",
			L" …… destro Vista → Soft 3D. Riquadri alto/basso indipendenti",
			L" …… clic der. Vista → Soft 3D. Paneles superior/inferior independientes",
			L" …… 우클릭 「표시」→ 간이 3D. 상·하 패널을 각각 2D/3D",
			L" …… 右键「显示」→ 简易3D。上下窗格可分别 2D/3D",
			L" …… يمين ← العرض ← Soft3D. اللوحان مستقلان",
			L" …… ПКМ «Вид» → Soft 3D. Верх/низ независимо",
			L" …… Rechtsklick Ansicht → Soft 3D. Oben/unten getrennt",
			L" …… direito Exibir → Soft 3D. Painéis independente",
			L" …… rechtsklik Weergave → Soft 3D. Boven/onder apart",
			L" …… PPM Widok → Soft 3D. Góra/dół niezależnie",
			L" …… sağ tık Görünüm → Soft 3B. Üst/alt ayrı")); y += lh;
		bulletNamed(L, y, RGB(180, 90, 40), L"ピアノロール", LL14(
			L" …… 右クリック「表示モード」→ 簡易3D。全面が1枚のシーン。0 で視点リセット",
			L" …… right-click View → Soft 3D. One full-client scene. Press 0 to reset the camera",
			L" …… clic droit Affichage → Soft 3D. Une scène plein client. 0 = reset",
			L" …… destro Vista → Soft 3D. Una scena a tutto client. 0 = reset",
			L" …… clic der. Vista → Soft 3D. Una escena completa. 0 = reiniciar",
			L" …… 우클릭 「표시 모드」→ 간이 3D. 전체 1장면. 0 으로 시점 리셋",
			L" …… 右键「显示模式」→ 简易3D。整窗一场景。按 0 复位视角",
			L" …… يمين ← العرض ← Soft3D. مشهد كامل. 0 للتصفير",
			L" …… ПКМ «Вид» → Soft 3D. Одна сцена. 0 — сброс",
			L" …… Rechtsklick Ansicht → Soft 3D. Eine Szene. 0 = Reset",
			L" …… direito Exibir → Soft 3D. Uma cena. 0 = redefinir",
			L" …… rechtsklik Weergave → Soft 3D. Eén scene. 0 = reset",
			L" …… PPM Widok → Soft 3D. Jedna scena. 0 = reset",
			L" …… sağ tık Görünüm → Soft 3B. Tek sahne. 0 = sıfırla")); y += lh;
		bulletNamed(L, y, RGB(90, 70, 160), L"コマンドロール", LL14(
			L" …… 同様に簡易3D表示あり。ドラッグ／ホイール／0／ツアーも共通です",
			L" …… Soft 3D view too. Drag / wheel / 0 / tour work the same way",
			L" …… Soft 3D aussi. Glisser / molette / 0 / visite idem",
			L" …… anche Soft 3D. Trascina / rotella / 0 / tour uguali",
			L" …… también Soft 3D. Arrastrar / rueda / 0 / tour igual",
			L" …… 간이 3D 표시 있음. 드래그·휠·0·투어 공통",
			L" …… 也有简易3D。拖动/滚轮/0/巡览相同",
			L" …… Soft3D أيضاً. سحب/عجلة/0/جولة نفسها",
			L" …… тоже Soft 3D. Перетаскивание / колесо / 0 / тур",
			L" …… auch Soft 3D. Ziehen / Rad / 0 / Tour gleich",
			L" …… também Soft 3D. Arrastar / roda / 0 / tour iguais",
			L" …… ook Soft 3D. Slepen / wiel / 0 / tour hetzelfde",
			L" …… też Soft 3D. Przeciąganie / kółko / 0 / tour tak samo",
			L" …… Soft 3B de var. Sürükle / teker / 0 / tur aynı")); y += lh;
		bulletNamed(L, y, RGB(200, 80, 120), L"CCustom コントロール", LL14(
			L" …… ボタン／チェック／スライダー／グループ枠などにも小さな Soft3D 飾りが入ります",
			L" …… buttons, checks, sliders, group boxes also get tiny Soft 3D accents",
			L" …… boutons, cases, curseurs, cadres ont aussi de petits accents Soft 3D",
			L" …… pulsanti, check, slider, group box hanno piccoli accenti Soft 3D",
			L" …… botones, casillas, deslizadores y marcos también llevan Soft 3D",
			L" …… 버튼·체크·슬라이더·그룹박스에도 작은 Soft3D 장식",
			L" …… 按钮、复选、滑块、分组框也有细小 Soft3D 装饰",
			L" …… الأزرار والمربعات والمنزلقات والأطر لها زخارف Soft3D صغيرة",
			L" …… кнопки, флажки, ползунки, group box — мелкие Soft 3D-акценты",
			L" …… Buttons, Checks, Slider, GroupBox mit kleinen Soft-3D-Akzenten",
			L" …… botões, checks, sliders e group boxes também têm Soft 3D",
			L" …… knoppen, checks, sliders en groupboxes hebben Soft 3D-accenten",
			L" …… przyciski, checkboxy, suwaki i groupboxy mają Soft 3D",
			L" …… düğme, onay, kaydırıcı ve grup kutularında da Soft 3B süs")); y += lh + 2;
		muted(L, y, LL14(
			L"EQ・画面キャプチャなど CCustom を使う窓は「飾り Soft3D」が入ります。視点付きの全面 Soft3D は上の4窓が中心です。",
			L"EQ, Screen Capture and other CCustom UIs get Soft 3D accents. Full interactive Soft 3D views center on the four windows above.",
			L"EQ, capture d'écran… ont des accents Soft 3D. Les vues Soft 3D interactives = les 4 fenêtres ci-dessus.",
			L"EQ, cattura schermo… hanno accenti Soft 3D. Le viste Soft 3D interattive = le 4 finestre sopra.",
			L"EQ, captura… llevan Soft 3D de adorno. Las vistas Soft 3D interactivas = las 4 ventanas de arriba.",
			L"EQ·화면캡처 등 CCustom UI에는 장식 Soft3D. 시점 Soft3D는 위 4창이 중심.",
			L"EQ、屏幕捕获等使用 CCustom 的窗口有装饰 Soft3D。可操作视角 Soft3D 以上述四窗为主。",
			L"EQ والالتقاط وغيرها لها زخارف Soft3D. المشاهد التفاعلية هي النوافذ الأربع أعلاه.",
			L"EQ, захват экрана и др. — Soft 3D-акценты. Интерактивные виды — четыре окна выше.",
			L"EQ, Screen Capture usw. haben Soft-3D-Akzente. Interaktive Soft-3D-Ansichten = die vier Fenster oben.",
			L"EQ, captura etc. têm Soft 3D de adorno. Soft 3D interativo = as quatro janelas acima.",
			L"EQ, schermopname enz. hebben Soft 3D-accenten. Interactieve Soft 3D = de vier vensters hierboven.",
			L"EQ, przechwyt itd. mają akcenty Soft 3D. Interaktywne Soft 3D = cztery okna powyżej.",
			L"EQ, ekran yakalama vb. Soft 3B süs alır. Etkileşimli Soft 3B = yukarıdaki dört pencere."));
	}
	else {
		title(L, y, LL14(
			L"キー操作", L"Keyboard", L"Clavier", L"Tastiera", L"Teclado", L"키 조작", L"键盘操作", L"لوحة المفاتيح",
			L"Клавиатура", L"Tastatur", L"Teclado", L"Toetsenbord", L"Klawiatura", L"Klavye"));
		y += titleLh;
		body(L, y, LL14(
			L"Space …… 再生 / 一時停止", L"Space …… play / pause", L"Espace …… lecture / pause",
			L"Spazio …… play / pausa", L"Espacio …… play / pausa", L"Space …… 재생 / 일시정지",
			L"Space …… 播放 / 暂停", L"Space …… تشغيل / إيقاف مؤقت", L"Space …… play / пауза",
			L"Leertaste …… Play / Pause", L"Espaço …… play / pausa", L"Spatie …… play / pauze",
			L"Spacja …… play / pauza", L"Boşluk …… play / duraklat")); y += lh;
		body(L, y, LL14(
			L"← / → …… 少し戻る / 進む　　↑ / ↓ …… 音量", L"← / → …… nudge back / forward　　↑ / ↓ …… volume",
			L"← / → …… reculer / avancer　　↑ / ↓ …… volume", L"← / → …… indietro / avanti　　↑ / ↓ …… volume",
			L"← / → …… atrás / adelante　　↑ / ↓ …… volumen", L"← / → …… 조금 뒤로 / 앞으로　　↑ / ↓ …… 음량",
			L"← / → …… 后退 / 前进　　↑ / ↓ …… 音量", L"← / → …… للخلف / للأمام　　↑ / ↓ …… الصوت",
			L"← / → …… назад / вперёд　　↑ / ↓ …… громкость", L"← / → …… zurück / vor　　↑ / ↓ …… Lautstärke",
			L"← / → …… voltar / avançar　　↑ / ↓ …… volume", L"← / → …… terug / vooruit　　↑ / ↓ …… volume",
			L"← / → …… wstecz / dalej　　↑ / ↓ …… głośność", L"← / → …… geri / ileri　　↑ / ↓ …… ses")); y += lh;
		body(L, y, LL14(
			L"1 - 8 …… キュー位置へジャンプ　　R …… その場を A-B ループ",
			L"1 - 8 …… jump to cue points　　R …… loop around here",
			L"1 - 8 …… points de repère　　R …… boucle ici",
			L"1 - 8 …… punti cue　　R …… loop qui",
			L"1 - 8 …… puntos cue　　R …… bucle aquí",
			L"1 - 8 …… 큐 위치로 점프　　R …… 현재 위치 A-B 루프",
			L"1 - 8 …… 跳到标记点　　R …… 就地 A-B 循环",
			L"1 - 8 …… نقاط cue　　R …… حلقة هنا",
			L"1 - 8 …… переход к меткам　　R …… петля здесь",
			L"1 - 8 …… zu Cues springen　　R …… Loop hier",
			L"1 - 8 …… ir aos cues　　R …… loop aqui",
			L"1 - 8 …… naar cues　　R …… lus hier",
			L"1 - 8 …… skok do cue　　R …… pętla tutaj",
			L"1 - 8 …… cue'lara atla　　R …… burada döngü")); y += lh;
		body(L, y, LL14(
			L"F2 …… タグ編集（複数選択はまとめて編集）　　Ctrl+K …… コマンドパレット　　? …… このガイド",
			L"F2 …… edit tags (multi-select: batch edit)　　Ctrl+K …… command palette　　? …… this guide",
			L"F2 …… tags (multi: edition groupée)　　Ctrl+K …… palette　　? …… ce guide",
			L"F2 …… tag (multi: modifica in blocco)　　Ctrl+K …… palette　　? …… questa guida",
			L"F2 …… etiquetas (multi: edición por lote)　　Ctrl+K …… paleta　　? …… esta guía",
			L"F2 …… 태그 (다중: 일괄 편집)　　Ctrl+K …… 명령 팔레트　　? …… 이 가이드",
			L"F2 …… 标签（多选：批量编辑）　　Ctrl+K …… 命令面板　　? …… 本指南",
			L"F2 …… وسوم (متعدد: تحرير دفعي)　　Ctrl+K …… لوحة الأوامر　　? …… هذا الدليل",
			L"F2 …… теги (несколько: пакетное правки)　　Ctrl+K …… палитра команд　　? …… это руководство",
			L"F2 …… Tags (Mehrfach: Sammelbearbeitung)　　Ctrl+K …… Befehlspalette　　? …… diese Anleitung",
			L"F2 …… tags (multi: edicao em lote)　　Ctrl+K …… paleta de comandos　　? …… este guia",
			L"F2 …… tags (multi: batch bewerken)　　Ctrl+K …… commandopalet　　? …… deze gids",
			L"F2 …… tagi (wiele: edycja zbiorcza)　　Ctrl+K …… paleta poleceń　　? …… ten przewodnik",
			L"F2 …… etiketler (coklu: toplu duzenleme)　　Ctrl+K …… komut paleti　　? …… bu kılavuz")); y += lh;
		body(L, y, LL14(
			L"0 …… 簡易3Dの視点を既定へ　　Delete …… リストから外す",
			L"0 …… reset the Soft 3D camera　　Delete …… remove from the list",
			L"0 …… réinitialiser la caméra 3D　　Suppr …… retirer de la liste",
			L"0 …… reset camera 3D　　Canc …… rimuovi dall'elenco",
			L"0 …… reiniciar la cámara 3D　　Supr …… quitar de la lista",
			L"0 …… 간이 3D 시점 초기화　　Delete …… 목록에서 제거",
			L"0 …… 重置简易3D视角　　Delete …… 从列表移除",
			L"0 …… تصفير كاميرا 3D　　Delete …… إزالة من القائمة",
			L"0 …… сброс 3D-камеры　　Delete …… убрать из списка",
			L"0 …… 3D-Kamera zurücksetzen　　Entf …… aus der Liste",
			L"0 …… redefinir câmera 3D　　Delete …… remover da lista",
			L"0 …… 3D-camera resetten　　Delete …… uit de lijst",
			L"0 …… reset kamery 3D　　Delete …… usuń z listy",
			L"0 …… 3B kamerayı sıfırla　　Delete …… listeden çıkar")); y += lh;
		body(L, y, LL14(
			L"Ctrl+A 全選択　Ctrl+C 行コピー　Ctrl+X 切り取り　Ctrl+V 貼付　Ctrl+Z/Y 元に戻す/やり直し",
			L"Ctrl+A select all  Ctrl+C copy rows  Ctrl+X cut  Ctrl+V paste  Ctrl+Z/Y undo/redo",
			L"Ctrl+A tout  Ctrl+C copier  Ctrl+X couper  Ctrl+V coller  Ctrl+Z/Y annuler/retablir",
			L"Ctrl+A tutto  Ctrl+C copia  Ctrl+X taglia  Ctrl+V incolla  Ctrl+Z/Y annulla/ripeti",
			L"Ctrl+A todo  Ctrl+C copiar  Ctrl+X cortar  Ctrl+V pegar  Ctrl+Z/Y deshacer/rehacer",
			L"Ctrl+A 모두  Ctrl+C 복사  Ctrl+X 잘라내기  Ctrl+V 붙여넣기  Ctrl+Z/Y 실행 취소/다시",
			L"Ctrl+A 全选  Ctrl+C 复制行  Ctrl+X 剪切  Ctrl+V 粘贴  Ctrl+Z/Y 撤销/重做",
			L"Ctrl+A الكل  Ctrl+C نسخ  Ctrl+X قص  Ctrl+V لصق  Ctrl+Z/Y تراجع/إعادة",
			L"Ctrl+A все  Ctrl+C копия  Ctrl+X вырезать  Ctrl+V вставка  Ctrl+Z/Y отмена/повтор",
			L"Ctrl+A alles  Ctrl+C kopieren  Ctrl+X ausschneiden  Ctrl+V einfuegen  Ctrl+Z/Y undo/redo",
			L"Ctrl+A tudo  Ctrl+C copiar  Ctrl+X recortar  Ctrl+V colar  Ctrl+Z/Y desfazer/refazer",
			L"Ctrl+A alles  Ctrl+C kopieren  Ctrl+X knippen  Ctrl+V plakken  Ctrl+Z/Y ongedaan/opnieuw",
			L"Ctrl+A wszystko  Ctrl+C kopiuj  Ctrl+X wytnij  Ctrl+V wklej  Ctrl+Z/Y cofnij/ponow",
			L"Ctrl+A tumu  Ctrl+C kopyala  Ctrl+X kes  Ctrl+V yapistir  Ctrl+Z/Y geri al/yinele")); y += lh + 4;
		muted(L, y, LL14(
			L"左下の「コピー」でこの一覧をクリップボードへ送れます。",
			L"The Copy button at the bottom-left sends this list to the clipboard.",
			L"Le bouton Copier envoie cette liste dans le presse-papiers.",
			L"Il pulsante Copia manda l'elenco negli appunti.",
			L"El botón Copiar envía esta lista al portapapeles.",
			L"왼쪽 아래 「복사」로 이 목록을 클립보드로 보낼 수 있습니다.",
			L"左下角的「复制」可把此列表送到剪贴板。",
			L"زر النسخ يرسل هذه القائمة إلى الحافظة.",
			L"Кнопка «Копировать» отправляет список в буфер обмена.",
			L"Die Schaltfläche Kopieren legt die Liste in die Zwischenablage.",
			L"O botão Copiar envia esta lista para a área de transferência.",
			L"De knop Kopiëren zet deze lijst op het klembord.",
			L"Przycisk Kopiuj wysyła listę do schowka.",
			L"Kopyala düğmesi listeyi panoya gönderir."));
	}

	dc.SelectObject(oldFont);

	m_contentBottom = y + 8;
	if (m_contentBottom < 80) m_contentBottom = 80;

#if CCUSTOM_AERO_SUPPORT
	// アクリルホスト上は α=255 で焼き込む(素の BitBlt だと本文が透ける)
	if (CCC_IsWin11())
		CCC_BlitStretchOpaque(pdc.GetSafeHdc(), m_bodyRc.left, m_bodyRc.top, bw, bh,
			m_mem.GetSafeHdc(), 0, 0, bw, bh);
	else
#endif
		pdc.BitBlt(m_bodyRc.left, m_bodyRc.top, bw, bh, &m_mem, 0, 0, SRCCOPY);

	CCC_CaptionPaint(pdc, m_hWnd);
	FitWindowToContent();
}

void CMpHelpDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_chapter == 0 && m_ownerMp && ::IsWindow(m_ownerMp->GetSafeHwnd())) {
		for (int i = 0; i < kMapRegionN; ++i) {
			if (!m_mapRc[i].IsRectEmpty() && m_mapRc[i].PtInRect(point)) {
				m_ownerMp->PostMessage(WM_MP_HELP_HIGHLIGHT, (WPARAM)i, 0);
				return;
			}
		}
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CMpHelpDlg::OnCopy()
{
	LPCTSTR lines[] = {
		LL14(L"メディアプレイヤー ショートカット", L"Media Player shortcuts", L"Raccourcis du lecteur",
			L"Scorciatoie Media Player", L"Atajos de Media Player", L"미디어 플레이어 단축키",
			L"媒体播放器快捷键", L"اختصارات مشغل الوسائط", L"Горячие клавиши плеера",
			L"Media-Player-Tastenkürzel", L"Atalhos do Media Player", L"Mediaspeler-sneltoetsen",
			L"Skróty Media Player", L"Medya Oynatıcı kısayolları"),
		L"",
		LL14(L"Space …… 再生 / 一時停止", L"Space …… play / pause", L"Espace …… lecture / pause",
			L"Spazio …… play / pausa", L"Espacio …… play / pausa", L"Space …… 재생 / 일시정지",
			L"Space …… 播放 / 暂停", L"Space …… تشغيل / إيقاف مؤقت", L"Space …… play / пауза",
			L"Leertaste …… Play / Pause", L"Espaço …… play / pausa", L"Spatie …… play / pauze",
			L"Spacja …… play / pauza", L"Boşluk …… play / duraklat"),
		LL14(L"← / → …… 少し戻る / 進む", L"← / → …… nudge back / forward", L"← / → …… reculer / avancer",
			L"← / → …… indietro / avanti", L"← / → …… atrás / adelante", L"← / → …… 조금 뒤로 / 앞으로",
			L"← / → …… 后退 / 前进", L"← / → …… للخلف / للأمام", L"← / → …… назад / вперёд",
			L"← / → …… zurück / vor", L"← / → …… voltar / avançar", L"← / → …… terug / vooruit",
			L"← / → …… wstecz / dalej", L"← / → …… geri / ileri"),
		LL14(L"↑ / ↓ …… 音量", L"↑ / ↓ …… volume", L"↑ / ↓ …… volume", L"↑ / ↓ …… volume",
			L"↑ / ↓ …… volumen", L"↑ / ↓ …… 음량", L"↑ / ↓ …… 音量", L"↑ / ↓ …… الصوت",
			L"↑ / ↓ …… громкость", L"↑ / ↓ …… Lautstärke", L"↑ / ↓ …… volume", L"↑ / ↓ …… volume",
			L"↑ / ↓ …… głośność", L"↑ / ↓ …… ses"),
		LL14(L"1 - 8 …… キュー位置へジャンプ", L"1 - 8 …… jump to cue points", L"1 - 8 …… points de repère",
			L"1 - 8 …… punti cue", L"1 - 8 …… puntos cue", L"1 - 8 …… 큐 위치로 점프",
			L"1 - 8 …… 跳到标记点", L"1 - 8 …… نقاط cue", L"1 - 8 …… переход к меткам",
			L"1 - 8 …… zu Cues springen", L"1 - 8 …… ir aos cues", L"1 - 8 …… naar cues",
			L"1 - 8 …… skok do cue", L"1 - 8 …… cue'lara atla"),
		LL14(L"R …… その場を A-B ループ", L"R …… loop around here", L"R …… boucle ici", L"R …… loop qui",
			L"R …… bucle aquí", L"R …… 현재 위치 A-B 루프", L"R …… 就地 A-B 循环", L"R …… حلقة هنا",
			L"R …… петля здесь", L"R …… Loop hier", L"R …… loop aqui", L"R …… lus hier",
			L"R …… pętla tutaj", L"R …… burada döngü"),
		LL14(L"F2 …… タグ編集（複数選択はまとめて編集）", L"F2 …… edit tags (multi-select: batch edit)", L"F2 …… tags (multi: edition groupée)", L"F2 …… tag (multi: modifica in blocco)",
			L"F2 …… etiquetas (multi: edición por lote)", L"F2 …… 태그 (다중: 일괄 편집)", L"F2 …… 标签（多选：批量编辑）", L"F2 …… وسوم (متعدد: تحرير دفعي)",
			L"F2 …… теги (несколько: пакетное правки)", L"F2 …… Tags (Mehrfach: Sammelbearbeitung)", L"F2 …… tags (multi: edicao em lote)", L"F2 …… tags (multi: batch bewerken)",
			L"F2 …… tagi (wiele: edycja zbiorcza)", L"F2 …… etiket (coklu: toplu duzenleme)"),
		LL14(L"Ctrl+K …… コマンドパレット", L"Ctrl+K …… command palette", L"Ctrl+K …… palette de commandes",
			L"Ctrl+K …… palette comandi", L"Ctrl+K …… paleta de comandos", L"Ctrl+K …… 명령 팔레트",
			L"Ctrl+K …… 命令面板", L"Ctrl+K …… لوحة الأوامر", L"Ctrl+K …… палитра команд",
			L"Ctrl+K …… Befehlspalette", L"Ctrl+K …… paleta de comandos", L"Ctrl+K …… commandopalet",
			L"Ctrl+K …… paleta poleceń", L"Ctrl+K …… komut paleti"),
		LL14(L"0 …… 簡易3Dの視点を既定へ", L"0 …… reset the Soft 3D camera", L"0 …… réinitialiser la caméra 3D",
			L"0 …… reset camera 3D", L"0 …… reiniciar la cámara 3D", L"0 …… 간이 3D 시점 초기화",
			L"0 …… 重置简易3D视角", L"0 …… تصفير كاميرا 3D", L"0 …… сброс 3D-камеры",
			L"0 …… 3D-Kamera zurücksetzen", L"0 …… redefinir câmera 3D", L"0 …… 3D-camera resetten",
			L"0 …… reset kamery 3D", L"0 …… 3B kamerayı sıfırla"),
		LL14(L"Delete …… リストから外す", L"Delete …… remove from the list", L"Suppr …… retirer de la liste",
			L"Canc …… rimuovi dall'elenco", L"Supr …… quitar de la lista", L"Delete …… 목록에서 제거",
			L"Delete …… 从列表移除", L"Delete …… إزالة من القائمة", L"Delete …… убрать из списка",
			L"Entf …… aus der Liste entfernen", L"Delete …… remover da lista", L"Delete …… uit de lijst",
			L"Delete …… usuń z listy", L"Delete …… listeden çıkar"),
		LL14(L"Ctrl+A 全選択　Ctrl+C 行コピー　Ctrl+X 切り取り　Ctrl+V 貼付　Ctrl+Z/Y 元に戻す/やり直し",
			L"Ctrl+A select all  Ctrl+C copy rows  Ctrl+X cut  Ctrl+V paste  Ctrl+Z/Y undo/redo",
			L"Ctrl+A tout  Ctrl+C copier  Ctrl+X couper  Ctrl+V coller  Ctrl+Z/Y annuler/retablir",
			L"Ctrl+A tutto  Ctrl+C copia  Ctrl+X taglia  Ctrl+V incolla  Ctrl+Z/Y annulla/ripeti",
			L"Ctrl+A todo  Ctrl+C copiar  Ctrl+X cortar  Ctrl+V pegar  Ctrl+Z/Y deshacer/rehacer",
			L"Ctrl+A 모두  Ctrl+C 복사  Ctrl+X 잘라내기  Ctrl+V 붙여넣기  Ctrl+Z/Y 실행 취소/다시",
			L"Ctrl+A 全选  Ctrl+C 复制行  Ctrl+X 剪切  Ctrl+V 粘贴  Ctrl+Z/Y 撤销/重做",
			L"Ctrl+A الكل  Ctrl+C نسخ  Ctrl+X قص  Ctrl+V لصق  Ctrl+Z/Y تراجع/إعادة",
			L"Ctrl+A все  Ctrl+C копия  Ctrl+X вырезать  Ctrl+V вставка  Ctrl+Z/Y отмена/повтор",
			L"Ctrl+A alles  Ctrl+C kopieren  Ctrl+X ausschneiden  Ctrl+V einfuegen  Ctrl+Z/Y undo/redo",
			L"Ctrl+A tudo  Ctrl+C copiar  Ctrl+X recortar  Ctrl+V colar  Ctrl+Z/Y desfazer/refazer",
			L"Ctrl+A alles  Ctrl+C kopieren  Ctrl+X knippen  Ctrl+V plakken  Ctrl+Z/Y ongedaan/opnieuw",
			L"Ctrl+A wszystko  Ctrl+C kopiuj  Ctrl+X wytnij  Ctrl+V wklej  Ctrl+Z/Y cofnij/ponow",
			L"Ctrl+A tumu  Ctrl+C kopyala  Ctrl+X kes  Ctrl+V yapistir  Ctrl+Z/Y geri al/yinele"),
		LL14(L"? …… この操作ガイド", L"? …… this guide", L"? …… ce guide", L"? …… questa guida",
			L"? …… esta guía", L"? …… 이 가이드", L"? …… 本指南", L"? …… هذا الدليل",
			L"? …… это руководство", L"? …… diese Anleitung", L"? …… este guia", L"? …… deze gids",
			L"? …… ten przewodnik", L"? …… bu kılavuz")
	};

	wchar_t buf[4096];
	buf[0] = L'\0';
	size_t used = 0;
	for (int i = 0; i < (int)_countof(lines); ++i) {
		const size_t n = wcslen(lines[i]);
		if (used + n + 3 >= _countof(buf))
			break;
		wcscpy_s(buf + used, _countof(buf) - used, lines[i]);
		used += n;
		wcscpy_s(buf + used, _countof(buf) - used, L"\r\n");
		used += 2;
	}
	if (used == 0) return;

	if (!OpenClipboard()) return;
	EmptyClipboard();
	const SIZE_T cb = (used + 1) * sizeof(wchar_t);
	HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, cb);
	if (hMem) {
		void* p = ::GlobalLock(hMem);
		if (p) {
			memcpy(p, buf, cb);
			::GlobalUnlock(hMem);
			if (!::SetClipboardData(CF_UNICODETEXT, hMem))
				::GlobalFree(hMem);
		} else {
			::GlobalFree(hMem);
		}
	}
	CloseClipboard();
}

BOOL CMpHelpDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	if (pMsg && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE) {
		DestroyWindow();
		return TRUE;
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CMpHelpDlg::OnOK() { DestroyWindow(); }
void CMpHelpDlg::OnCancel() { DestroyWindow(); }
void CMpHelpDlg::OnClose() { DestroyWindow(); }

void CMpHelpDlg::PostNcDestroy()
{
	CCustomBlurDialogExBase::PostNcDestroy();
	if (s_inst == this)
		s_inst = nullptr;
	delete this;
}
