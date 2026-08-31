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
	, m_triplet(0), m_dotted(0), m_notify(NULL)
{
}

CSasamiNotePaletteDlg* CSasamiNotePaletteDlg::OpenNear(CWnd* owner, CPoint screenPt)
{
	if (s_inst && ::IsWindow(s_inst->GetSafeHwnd())) {
		s_inst->m_notify = owner ? owner->GetSafeHwnd() : s_inst->m_notify;
		s_inst->SetWindowPos(NULL, screenPt.x, screenPt.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
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
		if (pw < 240 || ph < 200 || pw > 1200 || ph > 1200) { pw = 300; ph = 280; }
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
	if (m_triplet) d = (d * 2) / 3;
	if (d < 1) d = 1;
	m_durTicks = d;
	if (!m_notify) return;
	LPARAM lp = (m_rest ? 1 : 0) | (m_dotted ? 2 : 0) | (m_triplet ? 4 : 0)
		| (((LPARAM)(signed char)m_accidental) << 8)
		| (((LPARAM)m_baseDur & 0xFFFF) << 16);
	::PostMessage(m_notify, WM_SASAMI_PAL_DUR, (WPARAM)m_durTicks, lp);
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

static const wchar_t* ScPalTip(int i)
{
	static const wchar_t* tips[20] = {
		L"Whole note (1)", L"Half note (1/2)", L"Quarter note (1/4)", L"Eighth note (1/8)",
		L"16th note (1/16)", L"32nd note (1/32)", L"64th note (1/64)", L"Dotted (+½)",
		L"Quarter rest", L"Eighth rest", L"16th rest", L"32nd rest",
		L"Triplet (2/3)", L"Sharp ♯", L"Natural ♮", L"Flat ♭",
		L"Tempo tool", L"Snap fit", L"Focus Gate Time", L"Close palette"
	};
	return (i >= 0 && i < 20) ? tips[i] : L"";
}

void CSasamiNotePaletteDlg::SetupCellTips()
{
	if (m_tip.GetSafeHwnd())
		m_tip.DestroyWindow();
	if (!m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX))
		return;
	m_tip.Activate(TRUE);
	m_tip.SetMaxTipWidth(280);
	for (int i = 0; i < 20; i++) {
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
	if (i < 0 || i >= 20) return FALSE;
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
	ApplyLang();
	LayoutChrome();
	SetupCellTips();
	return TRUE;
}

void CSasamiNotePaletteDlg::LayoutChrome()
{
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 5, cols = 4, rows = 5;
	const int cw = (rc.Width() - pad * 2) / cols;
	const int ch = max(26, (rc.Height() - cap - pad * 2) / rows);
	int i = 0;
	for (int r = 0; r < rows; r++)
		for (int c = 0; c < 4; c++)
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

/*
 * Cell kinds (>0 note dur, <0 command):
 *  note durs, rest durs (as -100-dur), dotted, triplet, accidentals, fit, tempo, close
 */
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

	static const int kind[20] = {
		SC_PPQN * 4, SC_PPQN * 2, SC_PPQN, SC_PPQN / 2,
		SC_PPQN / 4, SC_PPQN / 8, SC_PPQN / 16, -2,
		-(100 + SC_PPQN), -(100 + SC_PPQN / 2), -(100 + SC_PPQN / 4), -(100 + SC_PPQN / 8),
		-7, -3, -4, -5,
		-10, -8, -11, -6
	};
	for (int i = 0; i < 20; i++) {
		CRect c = m_cells[i];
		c.OffsetRect(-body.left, -body.top);
		const int k = kind[i];
		if (k > 0) {
			/* Draw flags via GDI — ICO only has whole/half/quarter/8th. */
			ScStaffDrawNoteGlyph(mem, c, k, 0, !m_rest && m_baseDur == k);
		} else if (k <= -100) {
			int rd = -(k + 100);
			ScStaffDrawNoteGlyph(mem, c, rd, 1, m_rest && m_baseDur == rd);
		} else {
			mem.FillSolidRect(c, RGB(255, 255, 255));
			mem.Draw3dRect(c, RGB(150, 150, 165), RGB(150, 150, 165));
			mem.SetBkMode(TRANSPARENT);
			CFont font;
			font.CreateFont(c.Height() / 2, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
			CFont* of = mem.SelectObject(&font);
			mem.SetTextColor(RGB(30, 30, 40));
			const wchar_t* lab = L"";
			int sel = 0;
			if (k == -2) { lab = L"·"; sel = m_dotted; }
			else if (k == -7) { lab = L"3"; sel = m_triplet; }
			else if (k == -3) { lab = L"♯"; sel = m_accidental == 1; }
			else if (k == -4) { lab = L"♮"; sel = m_accidental == 0; }
			else if (k == -5) { lab = L"♭"; sel = m_accidental == -1; }
			else if (k == -6) { lab = L"×"; }
			else if (k == -8) { lab = L"Fit"; }
			else if (k == -10) { lab = L"♪="; }
			else if (k == -11) { lab = L"GT"; }
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
	static const int kind[20] = {
		SC_PPQN * 4, SC_PPQN * 2, SC_PPQN, SC_PPQN / 2,
		SC_PPQN / 4, SC_PPQN / 8, SC_PPQN / 16, -2,
		-(100 + SC_PPQN), -(100 + SC_PPQN / 2), -(100 + SC_PPQN / 4), -(100 + SC_PPQN / 8),
		-7, -3, -4, -5,
		-10, -8, -11, -6
	};
	for (int i = 0; i < 20; i++) {
		if (!m_cells[i].PtInRect(point)) continue;
		const int k = kind[i];
		if (k > 0) { m_baseDur = k; m_rest = 0; NotifyParent(); }
		else if (k <= -100) { m_baseDur = -(k + 100); m_rest = 1; NotifyParent(); }
		else if (k == -2) { m_dotted ^= 1; NotifyParent(); }
		else if (k == -7) { m_triplet ^= 1; NotifyParent(); }
		else if (k == -3) { m_accidental = 1; NotifyParent(); }
		else if (k == -4) { m_accidental = 0; NotifyParent(); }
		else if (k == -5) { m_accidental = -1; NotifyParent(); }
		else if (k == -6) { DestroyWindow(); return; }
		else if (k == -8 && m_notify)
			::PostMessage(m_notify, WM_SASAMI_PAL_DUR, 0, (LPARAM)0x40000000);
		else if (k == -10 && m_notify)
			::PostMessage(m_notify, WM_SASAMI_PAL_DUR, 0, (LPARAM)0x20000000); /* tempo tool */
		else if (k == -11 && m_notify)
			::PostMessage(m_notify, WM_SASAMI_PAL_DUR, 0, (LPARAM)0x10000000); /* focus gate */
		Invalidate(FALSE);
		break;
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CSasamiNotePaletteDlg::OnClose() {
	ScSaveWndGeom(this, NULL, NULL, &savedata.sasamiNotePalW, &savedata.sasamiNotePalH);
	DestroyWindow();
}

void CSasamiNotePaletteDlg::PostNcDestroy()
{
	if (s_inst == this) s_inst = NULL;
	CCustomBlurDialogExBase::PostNcDestroy();
	delete this;
}
