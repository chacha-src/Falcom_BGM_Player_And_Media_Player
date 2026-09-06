#include "stdafx.h"
#include "CEmuHelpDlg.h"
#include "CEmu/cemu_mgr.h"

extern save savedata;

CEmuHelpDlg* CEmuHelpDlg::s_inst = nullptr;

IMPLEMENT_DYNAMIC(CEmuHelpDlg, CCustomBlurDialogExBase)

CEmuHelpDlg::CEmuHelpDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CEmuHelpDlg::IDD, pParent)
	, m_memOldBmp(nullptr)
	, m_memW(0)
	, m_memH(0)
	, m_animTick(0)
	, m_timer(0)
	, m_chapter(0)
	, m_contentBottom(0)
{
	m_bodyRc.SetRectEmpty();
	m_demoCam.yawDeg = -28.f;
	m_demoCam.pitchDeg = 22.f;
	m_demoCam.zoom = 1.f;
}

CEmuHelpDlg::~CEmuHelpDlg()
{
	if (m_memOldBmp && m_mem.GetSafeHdc())
		m_mem.SelectObject(m_memOldBmp);
}

void CEmuHelpDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CEMU_HELP_TABS, m_tabs);
	DDX_Control(pDX, IDC_CEMU_HELP_BODY, m_body);
}

BEGIN_MESSAGE_MAP(CEmuHelpDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_TIMER()
	ON_WM_CLOSE()
	ON_NOTIFY(TCN_SELCHANGE, IDC_CEMU_HELP_TABS, &CEmuHelpDlg::OnTabSelChange)
END_MESSAGE_MAP()

void CEmuHelpDlg::ShowModal(CWnd* pParent)
{
	CloseIfOpen();
	CEmuHelpDlg dlg(pParent);
	dlg.DoModal();
}

void CEmuHelpDlg::CloseIfOpen()
{
	if (s_inst && ::IsWindow(s_inst->m_hWnd))
		s_inst->DestroyWindow();
}

BOOL CEmuHelpDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;

	SetWindowText(LL14(
		L"CEmu サウンドエミュレータ ガイド", L"CEmu Sound Emulator Guide", L"Guide CEmu", L"Guida CEmu",
		L"Guía CEmu", L"CEmu 사운드 에뮬레이터 가이드", L"CEmu 声音模拟器指南", L"دليل CEmu",
		L"Руководство CEmu", L"CEmu-Anleitung", L"Guia CEmu", L"CEmu-gids",
		L"Przewodnik CEmu", L"CEmu kılavuzu"));

	m_tabs.SetAeroMode(FALSE);
	{
		LPCTSTR names[kChapterN] = {
			LL14(L"概要", L"Overview", L"Aperçu", L"Panoramica", L"Resumen", L"개요", L"概要", L"نظرة عامة",
				L"Обзор", L"Überblick", L"Visão geral", L"Overzicht", L"Przegląd", L"Genel bakış"),
			LL14(L"データ", L"Data layout", L"Données", L"Dati", L"Datos", L"데이터", L"数据", L"البيانات",
				L"Данные", L"Daten", L"Dados", L"Gegevens", L"Dane", L"Veri"),
			LL14(L"FMモニタ", L"FM monitor", L"Moniteur FM", L"Monitor FM", L"Monitor FM", L"FM 모니터", L"FM监视器", L"مراقب FM",
				L"FM-монитор", L"FM-Monitor", L"Monitor FM", L"FM-monitor", L"Monitor FM", L"FM monitör"),
			L"Soft3D"
		};
		TCITEM ti = {};
		ti.mask = TCIF_TEXT;
		for (int i = 0; i < kChapterN; ++i) {
			ti.pszText = (LPTSTR)names[i];
			m_tabs.InsertItem(i, &ti);
		}
		m_tabs.SetCurSel(0);
	}

	if (m_body.GetSafeHwnd())
		m_body.ShowWindow(SW_HIDE);

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
	m_tooltip.AddTool(&m_tabs, LL14(
		L"章を切り替えます。", L"Switch chapters.", L"Changer de chapitre.", L"Cambia capitolo.", L"Cambiar capítulo.",
		L"장 전환.", L"切换章节。", L"تبديل الفصول.", L"Переключить главу.", L"Kapitel wechseln.",
		L"Mudar capítulo.", L"Hoofdstuk wisselen.", L"Zmiana rozdziału.", L"Bölüm değiştir."));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 340, 10000);

	CCC_CaptionLayout(m_hWnd);
	LayoutChrome();
	FitWindowToContent();
	if (!m_timer)
		m_timer = SetTimer(kAnimTimerId, 33, nullptr);
	return TRUE;
}

void CEmuHelpDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd)) return;
	CRect rc;
	GetClientRect(&rc);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 8;
	int tabTop = capH + pad;
	int tabH = 28;
	if (m_tabs.GetSafeHwnd()) {
		m_tabs.SetWindowPos(NULL, pad, tabTop, rc.Width() - pad * 2, tabH, SWP_NOZORDER | SWP_NOACTIVATE);
		m_tabs.LayoutEqualTabs(kChapterN);
	}
	tabTop += tabH + pad;
	m_bodyRc.SetRect(pad, tabTop, rc.Width() - pad, rc.Height() - pad);
}

void CEmuHelpDlg::FitWindowToContent()
{
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect rc(0, 0, 640, capH + 420);
	AdjustWindowRectEx(&rc, GetStyle(), FALSE, GetExStyle());
	SetWindowPos(NULL, 0, 0, rc.Width(), rc.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

BOOL CEmuHelpDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CEmuHelpDlg::OnOK() { DestroyWindow(); }
void CEmuHelpDlg::OnCancel() { DestroyWindow(); }
void CEmuHelpDlg::OnClose() { DestroyWindow(); }

void CEmuHelpDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	CCC_CaptionLayout(m_hWnd);
	LayoutChrome();
}

void CEmuHelpDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	if (lpMMI) {
		lpMMI->ptMinTrackSize.x = 480;
		lpMMI->ptMinTrackSize.y = 360;
	}
	CCustomBlurDialogExBase::OnGetMinMaxInfo(lpMMI);
}

void CEmuHelpDlg::OnTabSelChange(NMHDR*, LRESULT* pResult)
{
	m_chapter = m_tabs.GetCurSel();
	if (m_chapter < 0) m_chapter = 0;
	Invalidate(FALSE);
	if (pResult) *pResult = 0;
}

void CEmuHelpDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kAnimTimerId) {
		m_animTick = GetTickCount();
		if (m_chapter == 3)
			InvalidateRect(&m_bodyRc, FALSE);
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

BOOL CEmuHelpDlg::OnEraseBkgnd(CDC*)
{
	return TRUE;
}

static void CEmuHelpDrawText(CDC& dc, int x, int y, LPCTSTR s, COLORREF c)
{
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(c);
	dc.TextOut(x, y, s);
}

void CEmuHelpDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body = rc;
	body.top = capH;

	CDC mem;
	mem.CreateCompatibleDC(&dc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, body.Width(), body.Height());
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(0, 0, body.Width(), body.Height(), RGB(24, 26, 32));

	int y = 12;
	const COLORREF titleC = RGB(255, 220, 140);
	const COLORREF bodyC = RGB(220, 225, 235);

	if (m_chapter == 0) {
		CEmuHelpDrawText(mem, 12, y, LL14(
			L"CEmu は hoot 互換データを exe 内だけで再生します。",
			L"CEmu plays hoot-compatible data entirely inside the exe.",
			L"CEmu lit les données hoot dans l'exe.", L"CEmu riproduce dati hoot nell'exe.",
			L"CEmu reproduce datos hoot en el exe.", L"CEmu는 exe 내부에서 hoot 데이터를 재생합니다.",
			L"CEmu 在 exe 内播放 hoot 数据。", L"CEmu يشغّل بيانات hoot داخل exe.",
			L"CEmu воспроизводит hoot внутри exe.", L"CEmu spielt hoot-Daten in der exe.",
			L"CEmu reproduz dados hoot no exe.", L"CEmu speelt hoot-gegevens in exe.",
			L"CEmu odtwarza hoot w exe.", L"CEmu hoot verisini exe içinde çalar."), titleC);
		y += 28;
		CEmuHelpDrawText(mem, 12, y, LL14(
			L"CPU と音源チップを分離し、FMモニタでレジスタ/パネルを表示します。",
			L"CPU and sound chips are separate; FM monitor shows registers/panels.",
			L"CPU et puces séparés ; moniteur FM avec registres/panneaux.",
			L"CPU e chip separati; monitor FM con registri/pannelli.", L"CPU y chips separados; monitor FM con registros/paneles.",
			L"CPU와 음원 칩 분리, FM 모니터에 레지/패널.", L"CPU 与芯片分离，FM 监视器显示寄存器/面板。",
			L"CPU والرقاقات منفصلة؛ مراقب FM للسجلات/اللوحات.", L"CPU и чипы раздельно; FM-монитор — регистры/панели.",
			L"CPU und Chips getrennt; FM-Monitor zeigt Register/Panels.", L"CPU e chips separados; monitor FM com registos/painéis.",
			L"CPU en chips gescheiden; FM-monitor toont registers/panelen.", L"CPU i chipy osobno; monitor FM — rejestry/panele.",
			L"CPU ve çipler ayrı; FM monitör kayıt/panel."), bodyC);
	}
	else if (m_chapter == 1) {
		CEmuMgr* m = CEmuMgrGet();
		CEmuMgrEnsureCatalog(m);
		wchar_t line[512];
		_snwprintf_s(line, _TRUNCATE, L"data: %s", m->dataRoot[0] ? m->dataRoot : L"(not set)");
		CEmuHelpDrawText(mem, 12, y, line, titleC);
		y += 24;
		_snwprintf_s(line, _TRUNCATE, LL14(
			L"カタログ: %d タイトル", L"Catalog: %d titles", L"Catalogue : %d titres", L"Catalogo: %d titoli",
			L"Catálogo: %d títulos", L"카탈로그: %d곡", L"目录：%d 首", L"الفهرس: %d عنوان",
			L"Каталог: %d заголовков", L"Katalog: %d Titel", L"Catálogo: %d títulos", L"Catalogus: %d titels",
			L"Katalog: %d tytułów", L"Katalog: %d parça"), m->catalog.count);
		CEmuHelpDrawText(mem, 12, y, line, bodyC);
		y += 28;
		CEmuHelpDrawText(mem, 12, y, LL14(
			L"arcdata.zip + data\\pc88|pc98|…\\*.zip を配置。ZIP を D&D で再生。",
			L"Place arcdata.zip + data\\pc88|pc98|…\\*.zip. Drag-drop ZIP to play.",
			L"arcdata.zip + data\\pc88|pc98|…. Glisser-déposer le ZIP.",
			L"arcdata.zip + data\\pc88|pc98|…. Trascina ZIP.", L"arcdata.zip + data\\pc88|pc98|…. Arrastrar ZIP.",
			L"arcdata.zip + data\\pc88|pc98|… 배치. ZIP D&D.", L"放置 arcdata.zip 与 data 子目录。拖放 ZIP 播放。",
			L"arcdata.zip + data. اسحب ZIP.", L"arcdata.zip + data. Перетащите ZIP.",
			L"arcdata.zip + data. ZIP ziehen.", L"arcdata.zip + data. Arraste ZIP.",
			L"arcdata.zip + data. Sleep ZIP.", L"arcdata.zip + data. Przeciągnij ZIP.", L"arcdata.zip + data. ZIP sürükle."), bodyC);
	}
	else if (m_chapter == 2) {
		CEmuHelpDrawText(mem, 12, y, LL14(
			L"OPNA/OPM/OPL/YM2612/QSound 等 — チップごとにレジスタ hex と操作パネル。",
			L"OPNA/OPM/OPL/YM2612/QSound — per-chip register hex and panels.",
			L"OPNA/OPM/OPL/YM2612/QSound — hex et panneaux par puce.",
			L"OPNA/OPM/OPL/YM2612/QSound — hex e pannelli per chip.", L"OPNA/OPM/OPL/YM2612/QSound — hex y paneles.",
			L"OPNA/OPM/OPL/YM2612/QSound — 칩별 레지/패널.", L"各芯片寄存器 hex 与面板。",
			L"OPNA/OPM/OPL/YM2612/QSound — سجل ولوحات.", L"OPNA/OPM/OPL/YM2612/QSound — регистры/панели.",
			L"OPNA/OPM/OPL/YM2612/QSound — Register/Panels.", L"OPNA/OPM/OPL/YM2612/QSound — registos/painéis.",
			L"OPNA/OPM/OPL/YM2612/QSound — registers/panelen.", L"OPNA/OPM/OPL/YM2612/QSound — rejestry/panele.",
			L"OPNA/OPM/OPL/YM2612/QSound — kayıt/panel."), bodyC);
	}
	else if (m_chapter == 3) {
		const int bw = m_bodyRc.Width() - 24;
		const int bh = 180;
		CCC_GdiHelpDrawSoftDemoPair(mem, 12, y, bw, bh, CCC_HELPDEMO_KMIDIMON);
		y += bh + 8;
	}

	if (savedata.aero == 1 && CCC_AcrylicCaption(m_hWnd)) {
		CCC_BlitStretchOpaque(dc.GetSafeHdc(), body.left, body.top, body.Width(), body.Height(),
			mem.GetSafeHdc(), 0, 0, body.Width(), body.Height());
	}
	else {
		dc.BitBlt(body.left, body.top, body.Width(), body.Height(), &mem, 0, 0, SRCCOPY);
	}
	mem.SelectObject(old);
	CCC_CaptionPaintGdi(dc, m_hWnd);
}
