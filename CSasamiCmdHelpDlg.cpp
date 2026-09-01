#include "stdafx.h"
#include "ogg.h"
#include "CSasamiCmdHelpDlg.h"

CSasamiCmdHelpDlg* CSasamiCmdHelpDlg::s_inst = nullptr;

static UINT SchGetDpi(HWND hWnd)
{
	if (!hWnd) return 96;
	HDC hdc = ::GetDC(hWnd);
	if (!hdc) return 96;
	const UINT dpi = (UINT)::GetDeviceCaps(hdc, LOGPIXELSX);
	::ReleaseDC(hWnd, hdc);
	return (dpi > 0) ? dpi : 96;
}

static int SchScaleDpi(int value, UINT dpi)
{
	return ::MulDiv(value, (int)dpi, 96);
}

IMPLEMENT_DYNAMIC(CSasamiCmdHelpDlg, CCustomBlurDialogExBase)

CSasamiCmdHelpDlg::CSasamiCmdHelpDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CSasamiCmdHelpDlg::IDD, pParent)
	, m_memOldBmp(nullptr)
	, m_memW(0)
	, m_memH(0)
	, m_chapter(0)
	, m_contentBottom(0)
	, m_scrollY(0)
	, m_scrollMax(0)
{
	m_bodyRc.SetRectEmpty();
}

CSasamiCmdHelpDlg::~CSasamiCmdHelpDlg()
{
	if (m_memOldBmp && m_mem.GetSafeHdc())
		m_mem.SelectObject(m_memOldBmp);
}

void CSasamiCmdHelpDlg::Show(CWnd* owner, int initialTab)
{
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) {
		if (initialTab >= 0 && initialTab < kTabN) {
			s_inst->m_chapter = initialTab;
			s_inst->m_scrollY = 0;
			if (s_inst->m_tabs.GetSafeHwnd())
				s_inst->m_tabs.SetCurSel(initialTab);
			s_inst->InvalidateBody();
		}
		CCC_PresentOwnedHelp(s_inst, owner ? owner : AfxGetMainWnd());
		return;
	}
	auto* dlg = new CSasamiCmdHelpDlg(owner);
	if (!dlg->Create(CSasamiCmdHelpDlg::IDD, owner ? owner : AfxGetMainWnd())) {
		delete dlg;
		return;
	}
	if (initialTab >= 0 && initialTab < kTabN) {
		dlg->m_chapter = initialTab;
		if (dlg->m_tabs.GetSafeHwnd())
			dlg->m_tabs.SetCurSel(initialTab);
	}
	s_inst = dlg;
	CCC_PresentOwnedHelp(dlg, owner ? owner : AfxGetMainWnd());
}

void CSasamiCmdHelpDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SCH_TABS, m_tabs);
	DDX_Control(pDX, IDC_SCH_COPY, m_copy);
	DDX_Control(pDX, IDC_SCH_BODY, m_body);
}

BEGIN_MESSAGE_MAP(CSasamiCmdHelpDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_CLOSE()
	ON_WM_MOUSEWHEEL()
	ON_NOTIFY(TCN_SELCHANGE, IDC_SCH_TABS, &CSasamiCmdHelpDlg::OnTabSelChange)
	ON_BN_CLICKED(IDC_SCH_COPY, &CSasamiCmdHelpDlg::OnCopy)
END_MESSAGE_MAP()

BOOL CSasamiCmdHelpDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;

	SetWindowText(LL14(
		L"SASAMI コマンド説明", L"SASAMI Command Reference", L"Reference commandes SASAMI", L"Riferimento comandi SASAMI",
		L"Referencia de comandos SASAMI", L"SASAMI 명령 설명", L"SASAMI 命令说明", L"مرجع أوامر SASAMI",
		L"Справка по командам SASAMI", L"SASAMI-Befehlsreferenz", L"Referência de comandos SASAMI", L"SASAMI-opmanden",
		L"Referencja poleceń SASAMI", L"SASAMI komut referansı"));

	m_tabs.SetAeroMode(FALSE);
	m_copy.SetAeroMode(FALSE);

	{
		LPCTSTR names[kTabN] = {
			L"MIDI1",
			L"MIDI2",
			L"FM1",
			L"FM2",
			L"共通",
			L"譜面1",
			L"譜面2",
			L"譜面3"
		};
		TCITEM ti = {};
		ti.mask = TCIF_TEXT;
		for (int i = 0; i < kTabN; ++i) {
			ti.pszText = (LPTSTR)names[i];
			m_tabs.InsertItem(i, &ti);
		}
		m_tabs.SetCurSel(m_chapter);
	}

	m_copy.SetWindowText(LL14(
		L"コピー", L"Copy", L"Copier", L"Copia", L"Copiar", L"복사", L"复制", L"نسخ",
		L"Копировать", L"Kopieren", L"Copiar", L"Kopiëren", L"Kopiuj", L"Kopyala"));
	m_copy.SetGradation(RGB(255, 238, 208), RGB(255, 202, 130), 0, TRUE);

	if (m_body.GetSafeHwnd())
		m_body.ShowWindow(SW_HIDE);

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
	m_tooltip.AddTool(&m_copy, LL14(
		L"表示中タブのコマンド一覧をクリップボードへコピーします。",
		L"Copy the current tab's command list to the clipboard.",
		L"Copier la liste de l'onglet actif.", L"Copia l'elenco della scheda corrente.", L"Copiar la lista de la pestaña actual.",
		L"현재 탭 명령 목록을 클립보드로 복사.", L"将当前标签页的命令列表复制到剪贴板。", L"انسخ قائمة الأوامر للتبويب الحالي.",
		L"Скопировать список команд текущей вкладки.", L"Aktuelle Registerkarte kopieren.", L"Copiar a lista da aba atual.",
		L"Kopieer de opdrachtenlijst van het tabblad.", L"Kopiuj listę poleceń bieżącej karty.", L"Geçerli sekmenin komut listesini kopyala."));

	LayoutChrome();
	{
		const UINT dpi = SchGetDpi(m_hWnd);
		const int w = SchScaleDpi(680, dpi);
		const int h = SchScaleDpi(540, dpi);
		SetWindowPos(NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		LayoutChrome();
	}
	return TRUE;
}

void CSasamiCmdHelpDlg::LayoutChrome()
{
	if (!::IsWindow(m_hWnd)) return;
	CRect rc;
	GetClientRect(&rc);
	if (rc.Width() < 80 || rc.Height() < 80) return;

	const UINT dpi = SchGetDpi(m_hWnd);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = SchScaleDpi(8, dpi);
	const int btnW = SchScaleDpi(110, dpi);
	const int btnH = SchScaleDpi(26, dpi);

	int tabTop = (capH > 0 ? capH : 0) + pad;
	int tabH = SchScaleDpi(28, dpi);
	if (m_tabs.GetSafeHwnd()) {
		m_tabs.SetWindowPos(NULL, pad, tabTop, rc.Width() - pad * 2, tabH,
			SWP_NOZORDER | SWP_NOACTIVATE);
		m_tabs.LayoutEqualTabs(kTabN);
		CRect rcItem;
		if (m_tabs.GetItemRect(0, &rcItem) && rcItem.Height() + SchScaleDpi(6, dpi) > tabH) {
			tabH = rcItem.Height() + SchScaleDpi(6, dpi);
			m_tabs.SetWindowPos(NULL, pad, tabTop, rc.Width() - pad * 2, tabH,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}
	if (m_copy.GetSafeHwnd())
		m_copy.SetWindowPos(NULL, pad, rc.bottom - pad - btnH, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);

	m_bodyRc.SetRect(pad, tabTop + tabH + 6, rc.right - pad, rc.bottom - pad - btnH - 6);
	if (m_bodyRc.bottom < m_bodyRc.top + 40)
		m_bodyRc.bottom = m_bodyRc.top + 40;
	if (m_body.GetSafeHwnd())
		m_body.SetWindowPos(NULL, m_bodyRc.left, m_bodyRc.top, m_bodyRc.Width(), m_bodyRc.Height(),
			SWP_NOZORDER | SWP_NOACTIVATE);
}

void CSasamiCmdHelpDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED) return;
	LayoutChrome();
	InvalidateBody();
}

void CSasamiCmdHelpDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	if (lpMMI) {
		const UINT dpi = SchGetDpi(m_hWnd);
		lpMMI->ptMinTrackSize.x = SchScaleDpi(520, dpi);
		lpMMI->ptMinTrackSize.y = SchScaleDpi(400, dpi);
		lpMMI->ptMaxTrackSize.x = SchScaleDpi(900, dpi);
		lpMMI->ptMaxTrackSize.y = SchScaleDpi(820, dpi);
	}
	CCustomBlurDialogExBase::OnGetMinMaxInfo(lpMMI);
}

void CSasamiCmdHelpDlg::InvalidateBody()
{
	if (!m_bodyRc.IsRectEmpty())
		InvalidateRect(&m_bodyRc, FALSE);
}

void CSasamiCmdHelpDlg::OnTabSelChange(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	const int sel = m_tabs.GetCurSel();
	if (sel >= 0 && sel < kTabN)
		m_chapter = sel;
	m_scrollY = 0;
	InvalidateBody();
	if (pResult) *pResult = 0;
}

BOOL CSasamiCmdHelpDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (m_scrollMax <= 0)
		return CCustomBlurDialogExBase::OnMouseWheel(nFlags, zDelta, pt);
	ScreenToClient(&pt);
	if (!m_bodyRc.PtInRect(pt))
		return CCustomBlurDialogExBase::OnMouseWheel(nFlags, zDelta, pt);
	const int step = SchScaleDpi(48, SchGetDpi(m_hWnd));
	int next = m_scrollY - (zDelta / WHEEL_DELTA) * step;
	if (next < 0) next = 0;
	if (next > m_scrollMax) next = m_scrollMax;
	if (next != m_scrollY) {
		m_scrollY = next;
		InvalidateBody();
	}
	return TRUE;
}

BOOL CSasamiCmdHelpDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CSasamiCmdHelpDlg::OnOK() { DestroyWindow(); }
void CSasamiCmdHelpDlg::OnCancel() { DestroyWindow(); }
void CSasamiCmdHelpDlg::OnClose() { DestroyWindow(); }

void CSasamiCmdHelpDlg::PostNcDestroy()
{
	CCustomBlurDialogExBase::PostNcDestroy();
	if (s_inst == this)
		s_inst = nullptr;
	delete this;
}

BOOL CSasamiCmdHelpDlg::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

static int SchDrawLines(CDC& dc, int x, int y, int lh, const wchar_t* const* lines, int n)
{
	for (int i = 0; i < n; ++i) {
		dc.TextOut(x, y, lines[i]);
		y += lh;
	}
	return y;
}

static int SchDrawCmd(CDC& dc, CFont& accentFont, int x, int y, int lh, LPCWSTR cmd, LPCWSTR desc)
{
	CFont* prev = dc.SelectObject(&accentFont);
	dc.SetTextColor(RGB(90, 60, 140));
	CString head;
	head.Format(L"  %s", cmd);
	dc.TextOut(x, y, head);
	CSize sz = dc.GetTextExtent(head);
	dc.SelectObject(prev);
	dc.SetTextColor(RGB(52, 52, 68));
	dc.TextOut(x + sz.cx + 4, y, desc);
	return y + lh;
}

static int SchDrawWrap(CDC& dc, int x, int y, int maxW, int lh, LPCWSTR t)
{
	if (!t || !*t) return y;
	CRect rc(x, y, x + maxW, y + lh * 40);
	dc.DrawText(t, (int)wcslen(t), &rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
	dc.DrawText(t, (int)wcslen(t), &rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
	return rc.bottom + 2;
}

static int SchTitle(CDC& dc, CFont& boldFont, int L, int titleLh, int y, LPCTSTR t)
{
	CFont* prev = dc.SelectObject(&boldFont);
	dc.SetTextColor(RGB(72, 48, 120));
	dc.TextOut(L, y, t);
	dc.SelectObject(prev);
	return y + titleLh;
}

static int SchBody(CDC& dc, int L, int maxTextW, int lh, int y, LPCTSTR t)
{
	dc.SetTextColor(RGB(52, 52, 68));
	return SchDrawWrap(dc, L, y, maxTextW - L * 2, lh, t);
}

static int SchMuted(CDC& dc, int L, int maxTextW, int lh, int y, LPCTSTR t)
{
	dc.SetTextColor(RGB(100, 100, 120));
	return SchDrawWrap(dc, L, y, maxTextW - L * 2, lh, t);
}

static int SchLines(CDC& dc, int L, int maxTextW, int lh, int y, const wchar_t* const* lines, int n)
{
	for (int i = 0; i < n; ++i)
		y = SchDrawWrap(dc, L, y, maxTextW - L * 2, lh, lines[i]);
	return y;
}

static int SchPaintMmlBasics(CDC& dc, CFont& boldFont, CFont& accentFont, int lh, int titleLh, int L, int maxTextW, bool fmMode, int y)
{
	y = SchTitle(dc, boldFont, L, titleLh, y, fmMode ? L"演奏（MML 共通）" : L"テンポ・音長・音程（MML 共通）");
	if (!fmMode)
		y = SchMuted(dc, L, maxTextW, lh, y, L"FM1 タブにも同じ内容を載せています（FM のみ使う場合用）。");
	y = SchDrawCmd(dc, accentFont, L, y, lh, L"t120", L"… テンポ BPM。譜面の Tempo ボタン／テンポ行と同期。");
	y = SchDrawCmd(dc, accentFont, L, y, lh, L"l4", L"… デフォルト音長（4=四分、8=八分）。数字省略時に使われます。");
	y = SchDrawCmd(dc, accentFont, L, y, lh, L"o4", L"… オクターブ基準。< > でも上下。");
	y = SchDrawCmd(dc, accentFont, L, y, lh, L"c d e f g a b", L"… 音符。c+ / d- / e# で半音。長さは l 省略時 l4 等。");
	y = SchDrawCmd(dc, accentFont, L, y, lh, L"c4. / d8..", L"… 付点（.）とタイ付き長さ。譜面パレットの · も同系。");
	y = SchDrawCmd(dc, accentFont, L, y, lh, L"r8", L"… 休符。Tick 空白（FM 譜面）は休符として出力。");
	y = SchDrawCmd(dc, accentFont, L, y, lh, L"^8", L"… タイ（直前の音と連結、長さ加算）。");
	y = SchDrawCmd(dc, accentFont, L, y, lh, L"< >", L"… オクターブ 1 段上下（< = down, > = up）。");
	if (fmMode) {
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"v8", L"… FM: TL 音量 0..127。SSG ch4-6: PSGVOL 0..15。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"q90", L"… ゲート %（発音率。100=フル、短く切る）。");
	} else {
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"v100", L"… ベロシティ（MML v。CC7 相当のトラック音量）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"P64", L"… ピッチベンド（64=中央。譜面 Pitch ストリップと同期）。");
	}
	y = SchTitle(dc, boldFont, L, titleLh, y, L"記号・コメント");
	{
		const wchar_t* ln[] = {
			L"  ; または // … 行末コメント（コンパイル時除去）。",
			L"  | ~ $ & … 小節区切り・無視記号（レイアウト用）。",
			L"  数字のみ行 … 旧 MML 残骸等はスキップされます。"
		};
		y = SchLines(dc, L, maxTextW, lh, y, ln, 3);
	}
	return y;
}

static void SchFillLabeledBox(CDC& dc, const CRect& rc, COLORREF fill, LPCWSTR label)
{
	dc.FillSolidRect(rc, fill);
	dc.Draw3dRect(rc, RGB(130, 130, 150), RGB(130, 130, 150));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(30, 30, 45));
	CRect trc = rc;
	trc.DeflateRect(4, 2);
	dc.DrawText(label, trc, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
}

static int SchDrawMidiScoreDiagram(CDC& dc, int x, int y, int maxW, int lh)
{
	const int gw = min(maxW, 580);
	const int gh = lh * 9 + 16;
	dc.FillSolidRect(x, y, gw, gh, RGB(243, 244, 249));
	CBrush frameBrush(RGB(150, 150, 170));
	dc.FrameRect(CRect(x, y, x + gw, y + gh), &frameBrush);

	const int leftW = gw / 6;
	const int innerL = x + leftW + 10;
	const int innerR = x + gw - 8;
	const int staffBot = y + gh - lh * 4 - 12;

	SchFillLabeledBox(dc, CRect(x + 6, y + 6, x + leftW, staffBot), RGB(200, 215, 245), L"Ch\n1-32");
	SchFillLabeledBox(dc, CRect(innerL, y + 6, innerR, staffBot - lh - 6), RGB(255, 255, 255), L"音符・Mark\nPencil入力");
	SchFillLabeledBox(dc, CRect(innerL, staffBot - lh - 2, innerR, staffBot), RGB(255, 228, 196), L"Tone  Prog / Bank / VST");

	const int sy = staffBot + 4;
	const int sh = lh * 2 + 2;
	const int sw = (innerR - innerL - 8) / 3;
	SchFillLabeledBox(dc, CRect(innerL, sy, innerL + sw, sy + sh), RGB(220, 240, 220), L"Exp\nCC11");
	SchFillLabeledBox(dc, CRect(innerL + sw + 4, sy, innerL + sw * 2 + 4, sy + sh), RGB(220, 230, 250), L"Vol\nCC7");
	SchFillLabeledBox(dc, CRect(innerL + sw * 2 + 8, sy, innerR, sy + sh), RGB(245, 220, 240), L"Pitch\nBend");

	dc.SetTextColor(RGB(80, 80, 100));
	dc.TextOut(x + 8, y + gh - lh + 2, L"↑ Toolbar: Pencil / Eraser / Select / Tempo / Mark / InsertFX / Exc");
	return y + gh + 8;
}

static int SchDrawFmScoreDiagram(CDC& dc, int x, int y, int maxW, int lh)
{
	const int gw = min(maxW, 580);
	const int gh = lh * 8 + 12;
	dc.FillSolidRect(x, y, gw, gh, RGB(243, 244, 249));
	CBrush frameBrush(RGB(150, 150, 170));
	dc.FrameRect(CRect(x, y, x + gw, y + gh), &frameBrush);

	const int leftW = gw / 6;
	const int innerL = x + leftW + 10;
	const int innerR = x + gw - 8;
	const int staffBot = y + gh - lh * 3 - 10;

	SchFillLabeledBox(dc, CRect(x + 6, y + 6, x + leftW, staffBot), RGB(230, 210, 245), L"FM\n#1-10");
	SchFillLabeledBox(dc, CRect(innerL, y + 6, innerR, staffBot - lh - 6), RGB(255, 255, 255), L"FM音符\nOPNA 6+3+ADPCM");
	SchFillLabeledBox(dc, CRect(innerL, staffBot - lh - 2, innerR, staffBot), RGB(255, 220, 180), L"Voice / @n  FM音色");

	dc.SetTextColor(RGB(80, 80, 100));
	dc.TextOut(x + 8, y + gh - lh * 2 + 2, L"↑ Toolbar: Pencil / Eraser / Select / Voice / Save As(.fpy/.fpy2)");
	dc.TextOut(x + 8, y + gh - lh + 2, L"Tick 空白 → 休符 r   Text ボタン → MML 逆同期");
	return y + gh + 8;
}

static int SchDrawSyncFlowDiagram(CDC& dc, int x, int y, int maxW, int lh)
{
	const int gw = min(maxW, 560);
	const int gh = lh * 5 + 20;
	dc.FillSolidRect(x, y, gw, gh, RGB(243, 244, 249));
	CBrush frameBrush(RGB(150, 150, 170));
	dc.FrameRect(CRect(x, y, x + gw, y + gh), &frameBrush);

	const int boxW = gw / 3 - 12;
	const int boxH = lh * 2 + 8;
	const int midY = y + (gh - boxH) / 2;
	const int bx1 = x + 10;
	const int bx2 = x + gw - boxW - 10;

	SchFillLabeledBox(dc, CRect(bx1, midY, bx1 + boxW, midY + boxH), RGB(210, 225, 255), L"テキスト\nコンポーザ");
	SchFillLabeledBox(dc, CRect(bx2, midY, bx2 + boxW, midY + boxH), RGB(210, 245, 220), L"譜面 UI\n(MIDI/FM)");

	CPen pen(PS_SOLID, 2, RGB(100, 80, 160));
	CPen* oldPen = dc.SelectObject(&pen);
	const int cy = midY + boxH / 2;
	dc.MoveTo(bx1 + boxW + 4, cy - lh / 2);
	dc.LineTo(bx2 - 4, cy - lh / 2);
	dc.MoveTo(bx2 - 4, cy + lh / 2);
	dc.LineTo(bx1 + boxW + 4, cy + lh / 2);
	dc.SelectObject(oldPen);

	dc.SetTextColor(RGB(90, 60, 140));
	dc.TextOut(bx1 + boxW + 8, cy - lh - 2, L"譜面 →");
	dc.TextOut(bx1 + boxW + 8, cy + 2, L"← Text");

	dc.SetTextColor(RGB(80, 80, 100));
	dc.TextOut(x + 8, y + gh - lh + 2, L"B64折畳 … @VSTSTATEB64 等を … に畳み編集しやすく");
	return y + gh + 8;
}

void CSasamiCmdHelpDlg::PaintChapter(CDC& dc, int chapter, int maxTextW, int& outBottom)
{
	CFont* baseFont = GetFont();
	CFont boldFont, accentFont;
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
	dc.SelectObject(baseFont);
	dc.SetBkMode(TRANSPARENT);

	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 3;
	const int L = 8;
	if (maxTextW < L * 2 + 40) maxTextW = L * 2 + 40;

	int y = 6;

	if (chapter == kTabMidi1) {
		y = SchTitle(dc, boldFont, L, titleLh, y, L"MIDI / MICP テキスト — 基本（MIDI1）");
		y = SchMuted(dc, L, maxTextW, lh, y, L"ささみ☆ﾐ MIDI エディタ（SASAMIM.HLP）と同系統の MML3 です。#ch で 32 パートを切り替えます。");
		y = SchTitle(dc, boldFont, L, titleLh, y, L"パート指定");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"#n", L"… MIDI チャンネル n（1..32）。行頭またはパート切替。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"[midiCh:dataArea]", L"… DO--.MPY 形式。dataArea=トラック番号、midiCh=MIDI ch。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"TIT\"曲名\"", L"… タイトル（Shift-JIS 文字列）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L";MIDI / ;FM", L"… モード切替。OPNA 行があれば FM 自動。");
		y = SchPaintMmlBasics(dc, boldFont, accentFont, lh, titleLh, L, maxTextW, false, y);
		y = SchTitle(dc, boldFont, L, titleLh, y, L"音長・付点・3連符");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"c4 / d8. / e16..", L"… 数字=音長、. = 付点、.. = 二重付点。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"(3cde", L"… 3連符（譜面パレット「3」）。5/6/8 も同様。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"T120 / t120", L"… テンポ（大文字 T も可）。譜面 Tempo と同期。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"P64", L"… パン（0=左 64=中央 127=右）。P64:85 副値は無視。");
		y = SchTitle(dc, boldFont, L, titleLh, y, L"ドラム（#10 推奨）");
		{
			const wchar_t* ln[] = {
				L"  s / b / h … スネア(38) / バス(36) / ハイハット(42)。",
				L"  r … チャンネル10 ではライド(51)。それ以外は休符。",
				L"  通常の c d e … も GM ドラムノート番号として解釈。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 3);
		}
		y = SchTitle(dc, boldFont, L, titleLh, y, L"MIDI1 で続き");
		y = SchMuted(dc, L, maxTextW, lh, y, L"音色・VST・RPN・ループ詳細は MIDI2 タブ。FM 専用の基本は FM1 タブ（内容重複）。");
	} else if (chapter == kTabMidi2) {
		y = SchTitle(dc, boldFont, L, titleLh, y, L"MIDI — 音色・コントロール（@）");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@63", L"… GM プログラム番号（0..127）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@63:85", L"… プログラム:バンク（DO-- / MICP 形式）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@PROG n", L"… VST プログラム変更。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@BANK msb,lsb", L"… バンクセレクト MSB/LSB。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@V100", L"… トラック音量 cmd5（譜面 Vol ストリップ）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@P8192", L"… 14bit ピッチ（8192=中央。譜面 Pitch ストリップ）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@METER 4/4", L"… 拍子。MPW3 譜面グリッド・小節線。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@RPN msb,lsb,data", L"… RPN 送信（譜面 Exc/RPN メニュー）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@NRPN msb,lsb,data", L"… NRPN 送信。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@EX F0..F7", L"… SysEx（名前または F0…F7 16進列）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@PEDON / @PEDOFF", L"… サスティンペダル（@PEDALON/OFF も可）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@TS 4/4", L"… 拍子（@METER の別名）。");
		y = SchTitle(dc, boldFont, L, titleLh, y, L"VST バインド（.mpsmv / MPW3）");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@VST\"path\"", L"… インスト VST パス（譜面 Tone 行クリックでも設定）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@VSTSTATEB64 …", L"… VST 完全状態 Base64。@VSTSTATEB64+ で続行。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@VSTCTRLB64 …", L"… パラメータのみ（軽量）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@VSTFX slot \"path\"", L"… インサート FX スロット 0..（譜面 Insert FX）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@VSTFXSTATEB64 slot …", L"… FX 状態 B64。");
		y = SchMuted(dc, L, maxTextW, lh, y, L"テキスト UI「B64折畳」… 長い @VSTSTATEB64 行を … に畳んで編集。");
		y = SchTitle(dc, boldFont, L, titleLh, y, L"ループ・ジャンプ");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"|:2 … :|", L"… PMD/MICP ネイティブループ（バイナリ内展開しない）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"{:16 … }:", L"… MICP テキスト展開ループ（.mpw2 向け）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"Q", L"… ソフトループ開始マーク。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"J", L"… チャンネル先頭／Q へ FJUMP。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"q95", L"… ゲート長 %（発音率）。");
		y = SchTitle(dc, boldFont, L, titleLh, y, L"譜面 UI との対応");
		{
			const wchar_t* ln[] = {
				L"  Exp/Vol/Pitch ストリップ … v / @V / @P / P として MML に同期。",
				L"  Tone 行 … @PROG / @BANK / @VST。VST ピッカー・エディタ起動。",
				L"  Mark … Q/J やループ位置。A/B ルーラで範囲指定。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 3);
		}
		y = SchTitle(dc, boldFont, L, titleLh, y, L"出力形式との関係");
		{
			const wchar_t* ln[] = {
				L"  @VST あり → .mpsmv（VST ライブ再生・状態 B64 保存）。",
				L"  ループ 2 段以上 → .mpw2。1 段 → .mpy。",
				L"  コンパイル／再生確認で自動判定。Save As でも同じ規則。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 3);
		}
	} else if (chapter == kTabFm1) {
		y = SchTitle(dc, boldFont, L, titleLh, y, L"FM / OPNA テキスト（FPY）— 基本（FM1）");
		y = SchMuted(dc, L, maxTextW, lh, y, L"先頭に OPNA（YM2608）または OPN（YM2203）。#1..#10 で FM/SSG パート。MIDI1 と演奏記法は同じです。");
		y = SchTitle(dc, boldFont, L, titleLh, y, L"チップ・パート");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"OPNA", L"… YM2608 10ch（6FM+3SSG+ADPCM）。.fpy 標準。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"OPN", L"… YM2203 3ch FM モード。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"#1 .. #10", L"… FM ch1-6, SSG ch4-6, ADPCM 等。パート切替。");
		y = SchTitle(dc, boldFont, L, titleLh, y, L"パート割当（OPNA）");
		{
			const wchar_t* ln[] = {
				L"  #1-3 … FM チャンネル 1-3（YM2608 FM-A/B/C）。",
				L"  #4-6 … SSG チャンネル（PSG 音源。v=0..15）。",
				L"  #7-9 … FM チャンネル 4-6。",
				L"  #10 … ADPCM-A（サンプル音源）。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 4);
		}
		y = SchPaintMmlBasics(dc, boldFont, accentFont, lh, titleLh, L, maxTextW, true, y);
		y = SchTitle(dc, boldFont, L, titleLh, y, L"FM 音色");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"@3", L"… ビルトイン音色 FNEIRO 0..31。@3:1 副番。");
		y = SchTitle(dc, boldFont, L, titleLh, y, L"音色編集 UI");
		{
			const wchar_t* ln[] = {
				L"  テキスト UI「FM音色編集」／ FM 譜面 Voice … 25byte FM パラメータ編集。",
				L"  譜面 Tone 行クリック … 同エディタ。カスタム音色を @n で割当。",
				L"  FM モニタ（.fpy 鍵盤）… コンパイル結果の試聴。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 3);
		}
		y = SchMuted(dc, L, maxTextW, lh, y, L"ループ・FPY/FPY2 の詳細は FM2 タブ。出力形式は「共通」タブ。");
	} else if (chapter == kTabFm2) {
		y = SchTitle(dc, boldFont, L, titleLh, y, L"FM — ループ・特殊（FM2）");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"|:2 … :|", L"… PMD ネイティブループ（FPY cmd13/14、展開しない）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"{:4 … }:", L"… MICP 展開ループ（テキスト上で繰返し）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"Q / J", L"… ソフトループマーク／FJUMP（チャンネル先頭へ）。");
		y = SchTitle(dc, boldFont, L, titleLh, y, L"FPY と FPY2");
		{
			const wchar_t* ln[] = {
				L"  ループネスト 2 段以上 → .fpy2（DO--.FPY2 互換）。",
				L"  ネスト 1 段 → 従来 .fpy。",
				L"  譜面 Save As … ネスト深さから自動選択。",
				L"  Tick 空白 … 休符 r として MML 出力。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 4);
		}
		y = SchTitle(dc, boldFont, L, titleLh, y, L"MICP 拡張・無視");
		{
			const wchar_t* ln[] = {
				L"  @~ @# @! 等 … MICP 拡張（引数スキップして通過）。",
				L"  | ~ $ & … 区切り。^ タイ。; // コメント。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 2);
		}
	} else if (chapter == kTabCommon) {
		y = SchTitle(dc, boldFont, L, titleLh, y, L"出力形式（コンパイル時の自動選択）");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L".mpy", L"… クラシック MICP。ループ 1 段、VST なし。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L".mpw2", L"… ループネスト 2 段以上。SMF 変換向け。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L".mpsmv", L"… @VST / @VSTFX / @METER 等 MPW3。VST ライブ再生。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L".fpy", L"… FM OPNA、ループ 1 段。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L".fpy2", L"… FM、ループ 2 段以上。");
		y = SchTitle(dc, boldFont, L, titleLh, y, L"テキスト ↔ 譜面");
		{
			const wchar_t* ln[] = {
				L"  テキスト「譜面」ボタン … MML をコンパイルし譜面 UI に反映。",
				L"  譜面「Text」 … 譜面編集結果を MML テキストへ逆同期。",
				L"  ->MIDI / ->FM … モード切替（;MIDI / ;FM / OPNA でも自動判定）。",
				L"  B64折畳 … @VSTSTATEB64 等の長行を … に畳み、展開で復元。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 4);
		}
		y = SchTitle(dc, boldFont, L, titleLh, y, L"再生・書き出し");
		{
			const wchar_t* ln[] = {
				L"  コンパイル … 一時 .fpy/.mpy 等を生成。エラーは L#: メッセージ。",
				L"  再生確認 … コンパイル後プレイリストへ一時追加。",
				L"  書き出し … WAV 等（コンパイル済みバイナリ使用）。",
				L"  保存 … MML/テキスト。.dat/.mml も可。バイナリは譜面 Save As。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 4);
		}
		y = SchTitle(dc, boldFont, L, titleLh, y, L"テキスト UI ボタン");
		{
			const wchar_t* ln[] = {
				L"  コンパイル … 上記形式を自動選択してビルド。",
				L"  譜面 … コンパイル後スタッフ UI を開く／同期。",
				L"  ->MIDI / ->FM … モード切替。B64折畳 … VST 長行の折りたたみ。",
				L"  FM音色編集 / VST … モード別の音色・VST 挿入。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 4);
		}
	} else if (chapter == kTabScore1) {
		y = SchTitle(dc, boldFont, L, titleLh, y, L"MIDI 譜面 UI（譜面1）");
		y = SchMuted(dc, L, maxTextW, lh, y, L"32 MIDI パート。左 Ch リストで編集パート選択。ShowAll で全 ch 表示。");
		y = SchDrawMidiScoreDiagram(dc, L, y, maxTextW - L * 2, lh);
		y = SchTitle(dc, boldFont, L, titleLh, y, L"操作");
		{
			const wchar_t* ln[] = {
				L"  Pencil … 音符入力（パレットから音長・付点・♯♭・3連符）。",
				L"  Eraser … 消しゴム。Select … 矩形選択→Delete/Backspace。",
				L"  Tempo … テンポ変更（t コマンド同期）。Mark … ループ/Q/J 位置。",
				L"  Tone 行 … Prog/Bank/VST。クリックで VST ピッカー／プラグイン UI。",
				L"  Exp/Vol/Pitch ストリップ … CC レーン。チャンネル別に描画。",
				L"  Insert FX … VST エフェクトチェーン（@VSTFX）。",
				L"  Exc/RPN … SysEx・RPN・NRPN プリセット挿入。",
				L"  A/B ルーラ … ループ範囲。Text … テキストコンポーザへ。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 8);
		}
	} else if (chapter == kTabScore2) {
		y = SchTitle(dc, boldFont, L, titleLh, y, L"FM 譜面 UI（譜面2）");
		y = SchMuted(dc, L, maxTextW, lh, y, L"OPNA 10ch（6FM+3SSG+ADPCM）。#1..#10 でパート切替。");
		y = SchDrawFmScoreDiagram(dc, L, y, maxTextW - L * 2, lh);
		y = SchTitle(dc, boldFont, L, titleLh, y, L"操作");
		{
			const wchar_t* ln[] = {
				L"  Voice … FM 音色エディタ（25byte パラメータ）。@n で割当。",
				L"  Pencil/Eraser/Select … MIDI 譜面と同様の編集ツール。",
				L"  Save As … .fpy / .fpy2（ループネスト深さから自動選択）。",
				L"  Tick 空白 … 休符 r として MML 出力。",
				L"  Text … FM MML テキストへ逆同期。",
				L"  FM モニタ … コンパイル結果を鍵盤で試聴。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 6);
		}
	} else if (chapter == kTabScore3) {
		y = SchTitle(dc, boldFont, L, titleLh, y, L"テキスト ↔ 譜面 連携（譜面3）");
		y = SchDrawSyncFlowDiagram(dc, L, y, maxTextW - L * 2, lh);
		y = SchTitle(dc, boldFont, L, titleLh, y, L"ショートカット");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"Delete / Backspace", L"… 選択音符・マーク削除。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"Ctrl+Z / Y", L"… Undo / Redo（譜面）。");
		y = SchDrawCmd(dc, accentFont, L, y, lh, L"マウスホイール", L"… 譜面スクロール（縦）。");
		y = SchTitle(dc, boldFont, L, titleLh, y, L"出力形式（再掲）");
		{
			const wchar_t* ln[] = {
				L"  譜面 Save As … .mpy / .mpw2 / .mpsmv / .fpy / .fpy2 を自動選択。",
				L"  詳細は「共通」タブ。CHM「sasami-composer」も参照可。"
			};
			y = SchLines(dc, L, maxTextW, lh, y, ln, 2);
		}
	}

	outBottom = y + lh;
}

void CSasamiCmdHelpDlg::CopyChapterText(int chapter)
{
	CString out;
	auto add = [&](LPCWSTR s) { out += s; out += L"\r\n"; };
	auto cmd = [&](LPCWSTR c, LPCWSTR d) {
		CString line;
		line.Format(L"%s — %s", c, d);
		out += line;
		out += L"\r\n";
	};

	if (chapter == kTabMidi1) {
		add(L"[MIDI1 — basics]");
		cmd(L"#n", L"MIDI channel 1..32");
		cmd(L"[midiCh:dataArea]", L"DO--.MPY track layout");
		cmd(L"t120 / l4 / o4", L"tempo / default length / octave");
		cmd(L"c d e … / r / ^ / v / P", L"notes / rest / tie / velocity / pitch");
		add(L"(See MIDI2 for @ commands, VST, loops)");
	} else if (chapter == kTabMidi2) {
		add(L"[MIDI2 — @ / VST / loops]");
		cmd(L"@63 / @63:85 / @PROG / @BANK", L"program & bank");
		cmd(L"@V / @P / @METER", L"volume / pitch / time sig");
		cmd(L"@VST / @VSTSTATEB64 / @VSTFX", L"VST instrument & insert FX");
		cmd(L"@RPN / @NRPN / @EX", L"RPN / NRPN / SysEx");
		cmd(L"|:n … :| / {:n … }:", L"native loop / text loop");
		cmd(L"Q / J / q", L"soft loop / jump / gate %");
	} else if (chapter == kTabFm1) {
		add(L"[FM1 — basics + shared MML]");
		cmd(L"OPNA / OPN", L"chip mode");
		cmd(L"#1..#10", L"FM/SSG part");
		cmd(L"t / l / o / v / q", L"tempo / length / octave / volume / gate");
		cmd(L"@n", L"builtin voice");
	} else if (chapter == kTabFm2) {
		add(L"[FM2 — loops]");
		cmd(L"|:n … :|", L"PMD native loop → .fpy2 if nested");
		cmd(L"{:n … }:", L"MICP text loop");
		cmd(L"Q / J", L"soft loop mark / FJUMP");
	} else if (chapter == kTabCommon) {
		add(L"[Common — output formats]");
		cmd(L".mpy", L"classic MICP, 1-level loop");
		cmd(L".mpw2", L"2+ loop nest");
		cmd(L".mpsmv", L"MPW3 + VST binds");
		cmd(L".fpy / .fpy2", L"FM OPNA output");
		add(L"Text ↔ Score sync via Score/Text buttons.");
	} else if (chapter == kTabScore1) {
		add(L"[Score1 — MIDI score UI]");
		add(L"Ch list | Staff | Tone row | Exp/Vol/Pitch strips");
		add(L"Pencil/Erase/Select, Tempo/Mark, Insert FX, Exc/RPN, A/B ruler, Text sync.");
	} else if (chapter == kTabScore2) {
		add(L"[Score2 — FM score UI]");
		add(L"FM parts #1-10, Voice editor, Save As FPY/FPY2, Text sync.");
	} else if (chapter == kTabScore3) {
		add(L"[Score3 — sync & shortcuts]");
		add(L"Text <-> Score sync, Delete/Backspace, Ctrl+Z/Y.");
		add(L"See Common tab for output formats.");
	}

	if (out.IsEmpty()) return;
	if (OpenClipboard()) {
		EmptyClipboard();
		const SIZE_T bytes = (out.GetLength() + 1) * sizeof(wchar_t);
		HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
		if (h) {
			wchar_t* p = (wchar_t*)GlobalLock(h);
			if (p) {
				memcpy(p, out, bytes);
				GlobalUnlock(h);
				SetClipboardData(CF_UNICODETEXT, h);
			} else GlobalFree(h);
		}
		CloseClipboard();
	}
}

void CSasamiCmdHelpDlg::OnCopy()
{
	CopyChapterText(m_chapter);
}

void CSasamiCmdHelpDlg::OnPaint()
{
	CPaintDC pdc(this);

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

	const int savedDc = dc.SaveDC();
	dc.IntersectClipRect(0, 0, bw, bh);
	dc.SetViewportOrg(0, -m_scrollY);

	int contentBottom = 0;
	PaintChapter(dc, m_chapter, bw, contentBottom);
	m_contentBottom = contentBottom;

	dc.SetViewportOrg(0, 0);
	dc.RestoreDC(savedDc);

	m_scrollMax = max(0, m_contentBottom - bh);
	if (m_scrollY > m_scrollMax)
		m_scrollY = m_scrollMax;

	CCC_BlitStretchOpaque(pdc.GetSafeHdc(), m_bodyRc.left, m_bodyRc.top, bw, bh,
		dc.GetSafeHdc(), 0, 0, bw, bh);
	CCC_CaptionPaint(pdc, m_hWnd);
}

