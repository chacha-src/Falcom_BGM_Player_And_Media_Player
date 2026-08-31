#include "stdafx.h"
#include "ogg.h"
#include "CSasamiNotePaletteDlg.h"
#include "CSasamiStaffCore.h"
#include "SasamiComposerDoc.h"

CSasamiNotePaletteDlg* CSasamiNotePaletteDlg::s_inst = NULL;

CSasamiNotePaletteDlg* CSasamiNotePaletteDlg::Instance()
{
	return (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) ? s_inst : NULL;
}

IMPLEMENT_DYNAMIC(CSasamiNotePaletteDlg, CCustomBlurDialogExBase)

CSasamiNotePaletteDlg::CSasamiNotePaletteDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_NOTE_PAL, pParent)
	, m_baseDur(SC_PPQN), m_durTicks(SC_PPQN), m_rest(0), m_accidental(0)
	, m_tuplet(0), m_dotted(0), m_markStack(0), m_notify(NULL)
{
}

CSasamiNotePaletteDlg* CSasamiNotePaletteDlg::OpenNear(CWnd* owner, CPoint screenPt)
{
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) {
		s_inst->m_notify = owner ? owner->GetSafeHwnd() : s_inst->m_notify;
		s_inst->m_markStack = savedata.sasamiMarkStack ? 1 : 0;
		s_inst->SetWindowPos(NULL, screenPt.x, screenPt.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
		s_inst->Invalidate(FALSE);
		s_inst->BringWindowToTop();
		return s_inst;
	}
	s_inst = new CSasamiNotePaletteDlg(owner);
	s_inst->m_notify = owner ? owner->GetSafeHwnd() : NULL;
	if (!s_inst->Create(IDD_SASAMI_NOTE_PAL, owner)) {
		delete s_inst; s_inst = NULL; return NULL;
	}
	{
		int pw = savedata.sasamiNotePalW, ph = savedata.sasamiNotePalH;
		if (pw < 260 || ph < 280 || pw > 1200 || ph > 1200) { pw = 320; ph = 360; }
		s_inst->SetWindowPos(NULL, screenPt.x, screenPt.y, pw, ph, SWP_NOZORDER);
	}
	s_inst->ShowWindow(SW_SHOW);
	s_inst->BringWindowToTop();
	return s_inst;
}

void CSasamiNotePaletteDlg::NotifyParent()
{
	int d = m_baseDur;
	if (m_dotted) d += d / 2;
	if (m_tuplet == 3) d = (d * 2) / 3;
	else if (m_tuplet == 5) d = (d * 4) / 5;
	else if (m_tuplet == 6) d = (d * 4) / 6;
	else if (m_tuplet == 8) d = (d * 4) / 8;
	if (d < 1) d = 1;
	m_durTicks = d;
	if (!m_notify) return;
	/* Duration path: never set SASAMI_PAL_CMD. accidental masked to 8 bits. */
	LPARAM lp = (m_rest ? 1 : 0) | (m_dotted ? 2 : 0)
		| (((LPARAM)(m_tuplet & 0xF)) << 4)
		| (((LPARAM)(m_accidental & 0xFF)) << 8)
		| (((LPARAM)(m_baseDur & 0xFFFF)) << 16);
	::PostMessage(m_notify, WM_SASAMI_PAL_DUR, (WPARAM)m_durTicks, lp);
}

static void ScPalPostCmd(HWND notify, int cmdId)
{
	if (!notify) return;
	::PostMessage(notify, WM_SASAMI_PAL_DUR, 0,
		(LPARAM)(SASAMI_PAL_CMD | (cmdId & 0xFF)));
}

BEGIN_MESSAGE_MAP(CSasamiNotePaletteDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDOWN()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnTtnNeedText)
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnTtnNeedText)
END_MESSAGE_MAP()

void CSasamiNotePaletteDlg::ApplyLang()
{
	SetWindowText(LL14(L"音符", L"Notes", L"Notes", L"Note", L"Notas",
		L"음표", L"音符", L"نغمات", L"Ноты", L"Noten", L"Notas", L"Noten", L"Nuty", L"Notalar"));
}

/* kind >0 = note dur; <=-100 = rest; other negatives = commands (see OnLButtonDown). */
static const int kPalKind[CSasamiNotePaletteDlg::kCellCount] = {
	SC_PPQN * 4, SC_PPQN * 2, SC_PPQN, SC_PPQN / 2,
	SC_PPQN / 4, SC_PPQN / 8, SC_PPQN / 16, -2,
	-(100 + SC_PPQN), -(100 + SC_PPQN / 2), -(100 + SC_PPQN / 4), -(100 + SC_PPQN / 8),
	-7, -71, -72, -73, /* 3 / 5 / 6 / 8 tuplet */
	-3, -4, -5, -8,
	-20, -21, -22, -10,
	-23, -34, -35, -26, /* Marker / 1重 / ネスト / Clear loop */
	-30, -31, -32, -33 /* |:  :|  Ped.  ＊ */
};

static const wchar_t* ScPalTip(int i)
{
	static const wchar_t* tips[CSasamiNotePaletteDlg::kCellCount] = {
		L"Whole note (1)", L"Half note (1/2)", L"Quarter note (1/4)", L"Eighth note (1/8)",
		L"16th note (1/16)", L"32nd note (1/32)", L"64th note (1/64)", L"Dotted (+½)",
		L"Quarter rest", L"Eighth rest", L"16th rest", L"32nd rest",
		L"Triplet 3", L"Quintuplet 5", L"Sextuplet 6", L"Octuplet 8",
		L"Sharp ♯", L"Natural ♮", L"Flat ♭", L"Snap fit",
		L"Pencil", L"Eraser", L"Select", L"Tempo tool",
		L"Marker", L"Place mode: replace (1-deep)", L"Place mode: nest/stack", L"Clear loop",
		L"Loop start |:n", L"Loop end :|", L"Pedal ON (Ped.)", L"Pedal OFF (＊)"
	};
	return (i >= 0 && i < CSasamiNotePaletteDlg::kCellCount) ? tips[i] : L"";
}

void CSasamiNotePaletteDlg::SetupCellTips()
{
	if (m_tip.GetSafeHwnd())
		m_tip.DestroyWindow();
	if (!m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX))
		return;
	m_tip.Activate(TRUE);
	m_tip.SetMaxTipWidth(280);
	for (int i = 0; i < kCellCount; i++) {
		TOOLINFO ti = {};
		ti.cbSize = sizeof(ti);
		ti.uFlags = TTF_SUBCLASS;
		ti.hwnd = m_hWnd;
		ti.uId = (UINT_PTR)(i + 1);
		ti.rect = m_cells[i];
		ti.lpszText = LPSTR_TEXTCALLBACK;
		m_tip.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
	}
}

BOOL CSasamiNotePaletteDlg::OnTtnNeedText(UINT, NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!pNMHDR || !pResult) return FALSE;
	*pResult = 0;
	NMTTDISPINFOW* di = (NMTTDISPINFOW*)pNMHDR;
	const int i = (int)di->hdr.idFrom - 1;
	if (i < 0 || i >= kCellCount) return FALSE;
	di->lpszText = (LPWSTR)ScPalTip(i);
	di->hinst = NULL;
	return TRUE;
}

BOOL CSasamiNotePaletteDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd())
		m_tip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

BOOL CSasamiNotePaletteDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	s_inst = this;
	m_markStack = savedata.sasamiMarkStack ? 1 : 0;
	ApplyLang();
	LayoutChrome();
	SetupCellTips();
	return TRUE;
}

void CSasamiNotePaletteDlg::LayoutChrome()
{
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 5, cols = 4, rows = 8;
	const int cw = (rc.Width() - pad * 2) / cols;
	const int ch = max(24, (rc.Height() - cap - pad * 2) / rows);
	int i = 0;
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < cols; c++)
			m_cells[i++].SetRect(pad + c * cw, cap + pad + r * ch,
				pad + (c + 1) * cw - 2, cap + pad + (r + 1) * ch - 2);
	if (m_tip.GetSafeHwnd())
		SetupCellTips();
}

void CSasamiNotePaletteDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	LayoutChrome();
	Invalidate(FALSE);
}

BOOL CSasamiNotePaletteDlg::OnEraseBkgnd(CDC* pDC) { return CCustomBlurDialogExBase::OnEraseBkgnd(pDC); }

void CSasamiNotePaletteDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	CRect body(rc.left, cap, rc.right, rc.bottom);
	CDC mem; mem.CreateCompatibleDC(&dc);
	CBitmap bmp; bmp.CreateCompatibleBitmap(&dc, body.Width(), body.Height());
	CBitmap* old = mem.SelectObject(&bmp);
	mem.FillSolidRect(0, 0, body.Width(), body.Height(), RGB(248, 248, 252));

	for (int i = 0; i < kCellCount; i++) {
		CRect c = m_cells[i];
		c.OffsetRect(-body.left, -body.top);
		const int k = kPalKind[i];
		if (k > 0) {
			int drawDur = k;
			if (m_dotted && !m_rest && m_baseDur == k)
				drawDur = k + k / 2;
			ScStaffDrawNoteGlyph(mem, c, drawDur, 0, !m_rest && m_baseDur == k);
			if (!m_rest && m_baseDur == k && m_accidental) {
				const int cy = (c.top + c.bottom) / 2;
				mem.SetBkMode(TRANSPARENT);
				mem.SetTextColor(RGB(20, 20, 35));
				CFont af;
				af.CreateFont(14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
					OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
				CFont* of = mem.SelectObject(&af);
				mem.TextOut(c.left + 2, cy - 10, m_accidental > 0 ? L"♯" : L"♭");
				mem.SelectObject(of);
			}
		} else if (k <= -100) {
			int rd = -(k + 100);
			ScStaffDrawNoteGlyph(mem, c, rd, 1, m_rest && m_baseDur == rd);
		} else {
			mem.FillSolidRect(c, RGB(255, 255, 255));
			mem.Draw3dRect(c, RGB(150, 150, 165), RGB(150, 150, 165));
			mem.SetBkMode(TRANSPARENT);
			CFont font;
			font.CreateFont(max(11, c.Height() / 2 - 1), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
			CFont* of = mem.SelectObject(&font);
			mem.SetTextColor(RGB(30, 30, 40));
			const wchar_t* lab = L"";
			int sel = 0;
			if (k == -2) { lab = L"·"; sel = m_dotted; }
			else if (k == -7) { lab = L"3"; sel = (m_tuplet == 3); }
			else if (k == -71) { lab = L"5"; sel = (m_tuplet == 5); }
			else if (k == -72) { lab = L"6"; sel = (m_tuplet == 6); }
			else if (k == -73) { lab = L"8"; sel = (m_tuplet == 8); }
			else if (k == -3) { lab = L"♯"; sel = m_accidental == 1; }
			else if (k == -4) { lab = L"♮"; sel = m_accidental == 0; }
			else if (k == -5) { lab = L"♭"; sel = m_accidental == -1; }
			else if (k == -6) { lab = L"·"; }
			else if (k == -8) { lab = L"Fit"; }
			else if (k == -10) { lab = L"♪="; }
			else if (k == -11) { lab = L"GT"; }
			else if (k == -20) { lab = L"✎"; }
			else if (k == -21) { lab = L"⌫"; }
			else if (k == -22) { lab = L"▢"; }
			else if (k == -23) { lab = L"▼"; }
			else if (k == -34) { lab = L"1重"; sel = (m_markStack == 0); }
			else if (k == -35) { lab = L"ネスト"; sel = (m_markStack != 0); }
			else if (k == -26) { lab = L"∅"; }
			else if (k == -30) { lab = L"|:"; }
			else if (k == -31) { lab = L":|"; }
			else if (k == -32) { lab = L"Ped."; }
			else if (k == -33) { lab = L"＊"; }
			if (sel) mem.FillSolidRect(c, RGB(200, 215, 245));
			mem.DrawText(lab, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			mem.SelectObject(of);
		}
	}
	CCC_BlitStretchOpaque(dc.GetSafeHdc(), body.left, body.top, body.Width(), body.Height(),
		mem.GetSafeHdc(), 0, 0, body.Width(), body.Height());
	mem.SelectObject(old);
	CCC_CaptionPaint(dc, m_hWnd);
}

void CSasamiNotePaletteDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	for (int i = 0; i < kCellCount; i++) {
		if (!m_cells[i].PtInRect(point)) continue;
		const int k = kPalKind[i];
		if (k > 0) { m_baseDur = k; m_rest = 0; NotifyParent(); }
		else if (k <= -100) { m_baseDur = -(k + 100); m_rest = 1; NotifyParent(); }
		else if (k == -2) { m_dotted ^= 1; NotifyParent(); }
		else if (k == -7) { m_tuplet = (m_tuplet == 3) ? 0 : 3; NotifyParent(); }
		else if (k == -71) { m_tuplet = (m_tuplet == 5) ? 0 : 5; NotifyParent(); }
		else if (k == -72) { m_tuplet = (m_tuplet == 6) ? 0 : 6; NotifyParent(); }
		else if (k == -73) { m_tuplet = (m_tuplet == 8) ? 0 : 8; NotifyParent(); }
		else if (k == -3) { m_accidental = 1; NotifyParent(); }
		else if (k == -4) { m_accidental = 0; NotifyParent(); }
		else if (k == -5) { m_accidental = -1; NotifyParent(); }
		else if (k == -6) {
			/* Stay open — palette is session-persistent. */
			Invalidate(FALSE);
			return;
		}
		else if (k == -8) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_FIT);
		else if (k == -10) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_TEMPO);
		else if (k == -11) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_GT);
		else if (k == -20) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_PENCIL);
		else if (k == -21) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_ERASE);
		else if (k == -22) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_SEL);
		else if (k == -23) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_MARK);
		else if (k == -34) {
			m_markStack = 0;
			savedata.sasamiMarkStack = 0;
			ScPalPostCmd(m_notify, SASAMI_PAL_CMD_MARK_REPLACE);
		}
		else if (k == -35) {
			m_markStack = 1;
			savedata.sasamiMarkStack = 1;
			ScPalPostCmd(m_notify, SASAMI_PAL_CMD_MARK_STACK);
		}
		else if (k == -26) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_LOOP_CLR);
		else if (k == -30) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_LOOP_START);
		else if (k == -31) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_LOOP_END);
		else if (k == -32) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_PED_ON);
		else if (k == -33) ScPalPostCmd(m_notify, SASAMI_PAL_CMD_PED_OFF);
		Invalidate(FALSE);
		break;
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CSasamiNotePaletteDlg::OnClose() {
	/* Keep floating palette open for the score session (× only saves size). */
	ScSaveWndGeom(this, NULL, NULL, &savedata.sasamiNotePalW, &savedata.sasamiNotePalH);
	ShowWindow(SW_SHOW);
}

void CSasamiNotePaletteDlg::PostNcDestroy()
{
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}
