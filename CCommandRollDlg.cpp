#include "stdafx.h"
#include "CCommandRollDlg.h"
#include "CPromptDlg.h"
#include "CPromptEngine.h"
#include "CPromptAnalyze.h"
#include <cmath>

extern void MpPersistSavedataQuick();

IMPLEMENT_DYNAMIC(CCommandRollView, CWnd)
IMPLEMENT_DYNAMIC(CCommandRollDlg, CCustomBlurDialogExBase)

static CCommandRollDlg* g_rollDlg = nullptr;
extern int plf;

namespace {
class CCmdPlaceDlg : public CCustomBlurDialogExBase
{
public:
	MpPromptSnapshotEvent m_ev{};
	CCmdPlaceDlg(CWnd* pParent = nullptr)
		: CCustomBlurDialogExBase(IDD_MP_CMDPLACE, pParent) {}
	enum { IDD = IDD_MP_CMDPLACE };
protected:
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogExBase::OnInitDialog();
		SetIcon(nullptr, TRUE);
		SetIcon(nullptr, FALSE);
		ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
		CString cmd;
		if (m_ev.c2) cmd.Format(L"%c%c", m_ev.c1, m_ev.c2);
		else cmd.Format(L"%c", m_ev.c1);
		CString title;
		title.Format(L"コマンド: %s%s", m_ev.isPreset ? L"(演出) " : L"", (LPCTSTR)cmd);
		SetDlgItemText(IDC_MCP_CMD_L, title);
		CString s;
		s.Format(L"%.1f", m_ev.t0); SetDlgItemText(IDC_MCP_T0, s);
		s.Format(L"%.1f", m_ev.t1); SetDlgItemText(IDC_MCP_T1, s);
		s.Format(L"%d", m_ev.v0); SetDlgItemText(IDC_MCP_V0, s);
		s.Format(L"%d", m_ev.v1); SetDlgItemText(IDC_MCP_V1, s);
		if (m_ev.isPreset) {
			if (CWnd* w = GetDlgItem(IDC_MCP_V0)) w->ShowWindow(SW_HIDE);
			if (CWnd* w = GetDlgItem(IDC_MCP_V1)) w->ShowWindow(SW_HIDE);
			if (CWnd* w = GetDlgItem(IDC_MCP_V0_L)) w->ShowWindow(SW_HIDE);
			if (CWnd* w = GetDlgItem(IDC_MCP_V1_L)) w->ShowWindow(SW_HIDE);
		}
		return TRUE;
	}
	virtual void OnOK()
	{
		CString s;
		GetDlgItemText(IDC_MCP_T0, s); m_ev.t0 = _tstof(s);
		GetDlgItemText(IDC_MCP_T1, s); m_ev.t1 = _tstof(s);
		if (!m_ev.isPreset) {
			GetDlgItemText(IDC_MCP_V0, s); m_ev.v0 = _tstoi(s);
			GetDlgItemText(IDC_MCP_V1, s); m_ev.v1 = _tstoi(s);
			m_ev.hasVal = TRUE;
		}
		if (m_ev.t1 < m_ev.t0) {
			const double t = m_ev.t0; m_ev.t0 = m_ev.t1; m_ev.t1 = t;
			const int v = m_ev.v0; m_ev.v0 = m_ev.v1; m_ev.v1 = v;
		}
		m_ev.period = 0;
		CCustomBlurDialogExBase::OnOK();
	}
};

static int ScrollGetPos(CWnd* w, int bar, BOOL tracking)
{
	SCROLLINFO si{};
	si.cbSize = sizeof(si);
	si.fMask = tracking ? (SIF_TRACKPOS | SIF_POS) : SIF_POS;
	if (!w->GetScrollInfo(bar, &si)) return 0;
	return tracking ? (int)si.nTrackPos : (int)si.nPos;
}
} // namespace

// ---------------------------------------------------------------------------
// CCommandRollView
// ---------------------------------------------------------------------------
CCommandRollView::CCommandRollView() {}

BEGIN_MESSAGE_MAP(CCommandRollView, CWnd)
	ON_WM_CREATE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEMOVE()
	ON_WM_KEYDOWN()
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
END_MESSAGE_MAP()

BOOL CCommandRollView::CreateRoll(CWnd* pParent, const CRect& rc, UINT nId)
{
	const DWORD style = WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | WS_TABSTOP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	return Create(nullptr, nullptr, style, rc, pParent, nId);
}

int CCommandRollView::OnCreate(LPCREATESTRUCT lp)
{
	if (CWnd::OnCreate(lp) == -1) return -1;
	ModifyStyleEx(0, WS_EX_CLIENTEDGE);
	SyncSoft3DCamFromSave();
	return 0;
}

LPCTSTR CCommandRollView::LaneName(int lane) const
{
	static const LPCTSTR names[kLaneCount] = {
		L"Preset",
		L"N", L"S", L"I", L"K", L"M",
		L"p", L"t", L"d",
		L"r", L"c", L"y",
		L"E", L"F",
		L"a25", L"b40", L"e160", L"f250", L"g400", L"h630",
		L"i1k", L"j1.6k", L"k2.5k", L"l4k", L"m6.3k", L"n10k", L"o16k"
	};
	return (lane >= 0 && lane < kLaneCount) ? names[lane] : L"?";
}

void CCommandRollView::LettersForLane(int lane, TCHAR& c1, TCHAR& c2, BOOL& preset) const
{
	c1 = 0; c2 = 0; preset = FALSE;
	switch (lane) {
	case LanePreset: c1 = 's'; c2 = 'b'; preset = TRUE; break;
	case LaneN: c1 = 'N'; break;
	case LaneS: c1 = 'S'; break;
	case LaneI: c1 = 'I'; break;
	case LaneK: c1 = 'K'; break;
	case LaneM: c1 = 'M'; break;
	case LaneP: c1 = 'p'; break;
	case LaneT: c1 = 't'; break;
	case LaneD: c1 = 'd'; break;
	case LaneR: c1 = 'r'; break;
	case LaneC: c1 = 'c'; break;
	case LaneY: c1 = 'y'; break;
	case LaneE: c1 = 'E'; break;
	case LaneF: c1 = 'F'; break;
	case LaneA: c1 = 'a'; break;
	case LaneB: c1 = 'b'; break;
	case LaneEe: c1 = 'e'; break;
	case LaneFf: c1 = 'f'; break;
	case LaneG: c1 = 'g'; break;
	case LaneH: c1 = 'h'; break;
	case LaneIi: c1 = 'i'; break;
	case LaneJ: c1 = 'j'; break;
	case LaneKk: c1 = 'k'; break;
	case LaneL: c1 = 'l'; break;
	case LaneMm: c1 = 'm'; break;
	case LaneNn: c1 = 'n'; break;
	case LaneO: c1 = 'o'; break;
	default: break;
	}
}

int CCommandRollView::LaneFromEvent(const MpPromptSnapshotEvent& ev) const
{
	if (ev.isPreset) return LanePreset;
	switch (ev.c1) {
	case 'N': return LaneN;
	case 'S': case 's': return LaneS;
	case 'I': return LaneI;
	case 'K': return LaneK;
	case 'M': return LaneM;
	case 'p': return LaneP;
	case 't': return LaneT;
	case 'd': return LaneD;
	case 'r': return LaneR;
	case 'c': return LaneC;
	case 'y': return LaneY;
	case 'E': return LaneE;
	case 'F': return LaneF;
	case 'a': return LaneA;
	case 'b': return LaneB;
	case 'e': return LaneEe;
	case 'f': return LaneFf;
	case 'g': return LaneG;
	case 'h': return LaneH;
	case 'i': return LaneIi;
	case 'j': return LaneJ;
	case 'k': return LaneKk;
	case 'l': return LaneL;
	case 'm': return LaneMm;
	case 'n': return LaneNn;
	case 'o': return LaneO;
	default: return -1;
	}
}

CRect CCommandRollView::ClientRoll() const
{
	CRect rc; GetClientRect(&rc);
	return rc;
}

int CCommandRollView::ContentWidthPx() const
{
	return m_labelW + (int)(max(m_duration, 60.0) * m_pxPerSec + 0.5) + 80;
}

int CCommandRollView::ContentHeightPx() const
{
	return kHeaderH + kLaneCount * m_laneH + 8;
}

void CCommandRollView::ClampScroll()
{
	CRect rc = ClientRoll();
	const int maxX = max(0, ContentWidthPx() - rc.Width());
	const int maxY = max(0, ContentHeightPx() - rc.Height());
	if (m_scrollSec * m_pxPerSec > maxX)
		m_scrollSec = (double)maxX / max(m_pxPerSec, 0.001);
	if (m_scrollSec < 0.0) m_scrollSec = 0.0;
	if (m_scrollY > maxY) m_scrollY = maxY;
	if (m_scrollY < 0) m_scrollY = 0;
}

void CCommandRollView::NoteUserScroll()
{
	m_userScrollTick = GetTickCount();
	m_followPlay = FALSE;
}

void CCommandRollView::SyncScrollBars()
{
	if (!GetSafeHwnd()) return;
	ClampScroll();
	CRect rc = ClientRoll();

	SCROLLINFO si{};
	si.cbSize = sizeof(si);
	si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;

	si.nMin = 0;
	si.nMax = max(rc.Width(), ContentWidthPx());
	si.nPage = (UINT)max(1, rc.Width());
	si.nPos = (int)(m_scrollSec * m_pxPerSec + 0.5);
	SetScrollInfo(SB_HORZ, &si, TRUE);

	si.nMin = 0;
	si.nMax = max(rc.Height(), ContentHeightPx());
	si.nPage = (UINT)max(1, rc.Height());
	si.nPos = m_scrollY;
	SetScrollInfo(SB_VERT, &si, TRUE);
}

double CCommandRollView::XToSec(int x) const
{
	return m_scrollSec + (double)(x - m_labelW) / m_pxPerSec;
}

int CCommandRollView::SecToX(double sec) const
{
	return m_labelW + (int)((sec - m_scrollSec) * m_pxPerSec + 0.5);
}

CRect CCommandRollView::LaneRect(int lane) const
{
	const int y0 = kHeaderH + lane * m_laneH - m_scrollY;
	CRect rc = ClientRoll();
	return CRect(0, y0, rc.right, y0 + m_laneH);
}

int CCommandRollView::ValueBarHeight(const MpPromptSnapshotEvent& ev, int val, int maxH) const
{
	if (maxH < 4) return maxH;
	if (ev.isPreset) return max(6, maxH * 3 / 4);
	int maxV = 200;
	if (ev.c1 == 'E' || ev.c1 == 'd') maxV = 100;
	if (val < 0) val = 0;
	if (val > maxV) val = maxV;
	int h = (maxH * val + maxV / 2) / maxV;
	if (val > 0 && h < 3) h = 3;
	if (val == 0) h = 2;
	if (h > maxH) h = maxH;
	return h;
}

CRect CCommandRollView::EventRect(int idx) const
{
	if (idx < 0 || idx >= m_evCount) return CRect();
	const MpPromptSnapshotEvent& ev = m_ev[idx];
	const int lane = LaneFromEvent(ev);
	if (lane < 0) return CRect();
	CRect lr = LaneRect(lane);
	CRect rc = ClientRoll();
	if (lr.bottom < kHeaderH || lr.top > rc.bottom) return CRect();

	const int pad = 3;
	const int maxH = max(4, lr.Height() - pad * 2);
	const int h0 = ValueBarHeight(ev, ev.hasVal ? ev.v0 : 100, maxH);
	const int h1 = ValueBarHeight(ev, ev.hasVal ? ev.v1 : 100, maxH);
	const int h = max(h0, h1);
	const int cy = (lr.top + lr.bottom) / 2;

	double t0 = ev.t0, t1 = ev.t1;
	if (ev.period > 0.001)
		t1 = (ev.t1 > ev.t0) ? ev.t1 : ev.t0 + 1.0;

	if (ev.isPreset || fabs(t1 - t0) < 0.4) {
		const int x = SecToX(t0);
		const int half = max(4, h / 2);
		return CRect(x - half, cy - h / 2, x + half, cy - h / 2 + h);
	}
	int x0 = SecToX(t0), x1 = SecToX(t1);
	if (x1 < x0 + 8) x1 = x0 + 8;
	return CRect(x0, cy - h / 2, x1, cy - h / 2 + h);
}

void CCommandRollView::DrawEventBar(CDC& dc, int idx, COLORREF fill)
{
	if (idx < 0 || idx >= m_evCount) return;
	const MpPromptSnapshotEvent& ev = m_ev[idx];
	const int lane = LaneFromEvent(ev);
	if (lane < 0) return;
	CRect lr = LaneRect(lane);
	CRect rc = ClientRoll();
	if (lr.bottom < kHeaderH || lr.top > rc.bottom) return;

	const int pad = 3;
	const int maxH = max(4, lr.Height() - pad * 2);
	const int h0 = ValueBarHeight(ev, ev.hasVal ? ev.v0 : 100, maxH);
	const int h1 = ValueBarHeight(ev, ev.hasVal ? ev.v1 : 100, maxH);
	const int cy = (lr.top + lr.bottom) / 2;

	double t0 = ev.t0, t1 = ev.t1;
	if (ev.period > 0.001)
		t1 = (ev.t1 > ev.t0) ? ev.t1 : ev.t0 + 1.0;

	CBrush br(fill);
	CBrush* oldBr = dc.SelectObject(&br);
	CPen pen(PS_SOLID, 1, RGB(60, 60, 80));
	CPen* oldPen = dc.SelectObject(&pen);

	if (ev.isPreset || fabs(t1 - t0) < 0.4) {
		const int x = SecToX(t0);
		const int half = max(4, h0 / 2);
		CRect r(x - half, cy - h0 / 2, x + half, cy - h0 / 2 + h0);
		if (r.right > m_labelW && r.left < rc.right) {
			if (r.left < m_labelW) r.left = m_labelW;
			dc.Rectangle(r);
		}
	}
	else {
		int x0 = SecToX(t0), x1 = SecToX(t1);
		if (x1 < x0 + 8) x1 = x0 + 8;
		if (x1 > m_labelW && x0 < rc.right) {
			if (x0 < m_labelW) x0 = m_labelW;
			POINT pts[4] = {
				{ x0, cy - h0 / 2 },
				{ x1, cy - h1 / 2 },
				{ x1, cy - h1 / 2 + h1 },
				{ x0, cy - h0 / 2 + h0 }
			};
			dc.Polygon(pts, 4);
		}
	}
	dc.SelectObject(oldBr);
	dc.SelectObject(oldPen);
}

int CCommandRollView::HitTestEvent(CPoint pt) const
{
	for (int i = m_evCount - 1; i >= 0; --i) {
		CRect r = EventRect(i);
		if (!r.IsRectEmpty() && r.PtInRect(pt)) return i;
	}
	return -1;
}

int CCommandRollView::HitTestLane(CPoint pt) const
{
	for (int i = 0; i < kLaneCount; ++i) {
		CRect r = LaneRect(i);
		if (r.PtInRect(pt) && pt.x >= m_labelW && r.bottom > kHeaderH)
			return i;
	}
	return -1;
}

int CCommandRollView::HitTestPalette(CPoint pt) const
{
	for (int i = 0; i < kLaneCount; ++i) {
		CRect r = LaneRect(i);
		CRect pr(3, r.top + 3, m_labelW - 3, r.bottom - 3);
		if (pr.PtInRect(pt) && r.bottom > kHeaderH) return i;
	}
	return -1;
}

void CCommandRollView::EnsureDurationFloor(double hintSec)
{
	double need = (double)kMinDurationSec;
	if (hintSec + 60.0 > need)
		need = hintSec + 300.0;
	if (need > m_duration) {
		m_duration = need;
		SyncScrollBars();
	}
}

void CCommandRollView::SetEventsFromEngine()
{
	m_evCount = 0;
	m_sel = -1;
	const int n = MpPromptGetParsedEventCount();
	double maxT = 0.0;
	for (int i = 0; i < n && m_evCount < kMaxEv; ++i) {
		MpPromptSnapshotEvent snap;
		if (!MpPromptGetParsedEvent(i, &snap)) continue;
		if (LaneFromEvent(snap) < 0) continue;
		m_ev[m_evCount++] = snap;
		double end = snap.t1;
		if (snap.period > 0.001) end = max(snap.period, snap.t1);
		if (end > maxT) maxT = end;
	}
	// 未演奏で曲長不明でも横スクロールできるよう、最低30分のタイムラインを確保
	m_duration = max(maxT + 20.0, (double)kMinDurationSec);
	EnsureDurationFloor(MpGetPerformanceTimeSec());
	SyncScrollBars();
	InvalidateRoll();
}

void CCommandRollView::ReloadFromParse()
{
	SetEventsFromEngine();
}

static void MpRollPersistPromptText(const CString& textIn)
{
	CString s = textIn;
	if (s.GetLength() > 14000)
		s = s.Left(14000);
	_tcsncpy(savedata.mpPromptTextLong, s, _countof(savedata.mpPromptTextLong) - 1);
	savedata.mpPromptTextLong[_countof(savedata.mpPromptTextLong) - 1] = 0;
	_tcsncpy(savedata.mpPromptText, s, _countof(savedata.mpPromptText) - 1);
	savedata.mpPromptText[_countof(savedata.mpPromptText) - 1] = 0;
}

CString CCommandRollView::BuildPromptText()
{
	CString prev;
	if (m_peer && ::IsWindow(m_peer->GetSafeHwnd()))
		prev = m_peer->GetPromptText();
	else if (CPromptDlg* p = MpPromptDlgInstance()) {
		if (::IsWindow(p->GetSafeHwnd()))
			prev = p->GetPromptText();
	}
	if (prev.IsEmpty())
		prev = MpPromptSourceText();
	// 旧本文に壊れたトークンがあっても、レーン上のイベントから本文を組み直す
	MpPromptParse(prev, nullptr);

	MpPromptSnapshotEvent keep[kMaxEv];
	int keepN = 0;
	const int nPrev = MpPromptGetParsedEventCount();
	for (int i = 0; i < nPrev && keepN < kMaxEv; ++i) {
		MpPromptSnapshotEvent snap;
		if (!MpPromptGetParsedEvent(i, &snap)) continue;
		if (LaneFromEvent(snap) < 0) keep[keepN++] = snap;
	}

	CString body;
	int idxAll = 0;
	auto flushTok = [&](const MpPromptSnapshotEvent& ev) {
		if (!body.IsEmpty()) body += L" ";
		body += MpPromptFormatToken(ev);
		++idxAll;
		if (idxAll % 6 == 0) body += L"\r\n";
	};
	for (int i = 0; i < keepN; ++i) flushTok(keep[i]);
	for (int i = 0; i < m_evCount; ++i) flushTok(m_ev[i]);

	CString header;
	int pos = 0;
	const int n = prev.GetLength();
	while (pos < n) {
		int lineEnd = prev.Find(L'\n', pos);
		if (lineEnd < 0) lineEnd = n;
		CString line = prev.Mid(pos, lineEnd - pos);
		line.TrimRight(L"\r");
		CString t = line; t.Trim();
		if (t.IsEmpty() || t[0] == '#') {
			if (!header.IsEmpty()) header += L"\r\n";
			header += line;
			pos = (lineEnd < n) ? lineEnd + 1 : n;
			continue;
		}
		break;
	}
	return header.IsEmpty() ? body : (header + L"\r\n" + body);
}

void CCommandRollView::CommitToPeer()
{
	CString text = BuildPromptText();
	MpRollPersistPromptText(text);
	m_pushing = TRUE;
	if (m_pSyncGen) ++(*m_pSyncGen);
	const UINT gen = m_pSyncGen ? *m_pSyncGen : 0;
	CPromptDlg* peer = m_peer;
	if (!peer || !::IsWindow(peer->GetSafeHwnd()))
		peer = MpPromptDlgInstance();
	if (peer && ::IsWindow(peer->GetSafeHwnd()))
		peer->ApplyTextFromRoll(text, gen);
	m_pushing = FALSE;
	MpPromptParse(text, nullptr);
	SetEventsFromEngine();
}

void CCommandRollView::ClearAllEvents()
{
	m_evCount = 0;
	m_sel = -1;
	InvalidateRoll();
}

BOOL CCommandRollView::RunPlaceDialog(MpPromptSnapshotEvent& ev)
{
	CWnd* main = CCC_GetActiveMainWindow();
	CCmdPlaceDlg dlg(GetParent());
	dlg.m_ev = ev;
	const INT_PTR r = dlg.DoModal();
	// DoModal がオーナー(メイン)を無効化したまま戻ることがあるので再有効化
	if (main && ::IsWindow(main->GetSafeHwnd()))
		main->EnableWindow(TRUE);
	if (CWnd* roll = GetParent()) {
		if (::IsWindow(roll->GetSafeHwnd()))
			roll->EnableWindow(TRUE);
	}
	if (r != IDOK) return FALSE;
	ev = dlg.m_ev;
	return TRUE;
}

void CCommandRollView::EnsureMemDC(int w, int h)
{
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (m_memDC.GetSafeHdc() && m_memW == w && m_memH == h) return;
	if (m_memDC.GetSafeHdc()) {
		if (m_oldBmp) m_memDC.SelectObject(m_oldBmp);
		m_oldBmp = nullptr;
		m_memDC.DeleteDC();
	}
	if (m_memBmp.GetSafeHandle()) m_memBmp.DeleteObject();
	CClientDC dc(this);
	m_memDC.CreateCompatibleDC(&dc);
	m_memBmp.CreateCompatibleBitmap(&dc, w, h);
	m_oldBmp = m_memDC.SelectObject(&m_memBmp);
	m_memW = w;
	m_memH = h;
}

void CCommandRollView::InvalidateRoll()
{
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CCommandRollView::PaintRoll(CDC& dc, const CRect& rc)
{
	if (IsSoft3D()) {
		PaintRollSoft3D(dc, rc);
		return;
	}
	dc.FillSolidRect(rc, RGB(245, 246, 250));
	CPen gridPen(PS_SOLID, 1, RGB(220, 222, 230));
	CPen* oldPen = dc.SelectObject(&gridPen);
	CFont* oldFont = dc.SelectObject(GetParent() ? GetParent()->GetFont() : GetFont());
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(60, 60, 75));

	const int clipTop = kHeaderH;
	dc.FillSolidRect(CRect(0, 0, rc.right, clipTop), RGB(230, 232, 240));
	const double step = (m_pxPerSec >= 120) ? 0.25
		: (m_pxPerSec >= 60) ? 0.5
		: (m_pxPerSec >= 24) ? 1.0
		: (m_pxPerSec >= 8) ? 5.0
		: 10.0;
	for (double t = floor(m_scrollSec / step) * step; ; t += step) {
		const int x = SecToX(t);
		if (x > rc.right) break;
		if (x >= m_labelW) {
			dc.MoveTo(x, clipTop);
			dc.LineTo(x, rc.bottom);
			CString lab;
			if (step < 1.0)
				lab.Format(L"%.2f", t);
			else
				lab.Format(L"%.0f", t);
			dc.TextOut(x + 2, 2, lab);
		}
	}
	for (int lane = 0; lane < kLaneCount; ++lane) {
		CRect lr = LaneRect(lane);
		if (lr.bottom < clipTop || lr.top > rc.bottom) continue;
		CRect vis = lr;
		if (vis.top < clipTop) vis.top = clipTop;
		dc.FillSolidRect(CRect(m_labelW, vis.top, rc.right, vis.bottom),
			(lane % 2) ? RGB(236, 238, 245) : RGB(245, 246, 250));
		dc.MoveTo(m_labelW, lr.bottom - 1);
		dc.LineTo(rc.right, lr.bottom - 1);
	}
	dc.FillSolidRect(CRect(0, clipTop, m_labelW, rc.bottom), RGB(235, 238, 245));

	// イベントはレーン列に食い込まないようクリップ
	const int savedDc = dc.SaveDC();
	dc.IntersectClipRect(m_labelW, clipTop, rc.right, rc.bottom);

	for (int i = 0; i < m_evCount; ++i) {
		CRect er = EventRect(i);
		if (er.IsRectEmpty()) continue;
		if (er.right <= m_labelW || er.left >= rc.right) continue;
		const MpPromptSnapshotEvent& ev = m_ev[i];
		const BOOL pct = (ev.period > 0.001);
		COLORREF fill = ev.isPreset ? RGB(180, 140, 220) : (pct ? RGB(120, 180, 210) : RGB(100, 170, 130));
		if (i == m_sel) fill = RGB(255, 170, 80);
		DrawEventBar(dc, i, fill);
		CString lab;
		if (ev.isPreset) {
			if (ev.c2) lab.Format(L"%c%c", ev.c1, ev.c2);
			else lab.Format(L"%c", ev.c1);
		}
		else if (ev.v0 == ev.v1) lab.Format(L"%d", ev.v0);
		else lab.Format(L"%d→%d", ev.v0, ev.v1);
		if (pct) lab += L" %";
		CRect textRc = er;
		if (textRc.left < m_labelW) textRc.left = m_labelW;
		if (textRc.Width() < 12) continue;
		dc.SetTextColor(RGB(20, 20, 30));
		dc.DrawText(lab, textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}

	if ((m_creating || m_paletteDrag) && m_dragLane >= 0) {
		CRect lr = LaneRect(m_dragLane);
		int x0 = SecToX(min(m_dragT0, m_dragT1));
		int x1 = SecToX(max(m_dragT0, m_dragT1));
		if (x1 < x0 + 4) x1 = x0 + 4;
		if (x0 < m_labelW) x0 = m_labelW;
		dc.FillSolidRect(CRect(x0, lr.top + 6, x1, lr.bottom - 6), RGB(255, 220, 120));
	}
	dc.RestoreDC(savedDc);

	// レーン名は最後に描画してバー文字と重ならないようにする
	dc.FillSolidRect(CRect(0, clipTop, m_labelW, rc.bottom), RGB(235, 238, 245));
	for (int lane = 0; lane < kLaneCount; ++lane) {
		CRect lr = LaneRect(lane);
		if (lr.bottom < clipTop || lr.top > rc.bottom) continue;
		CRect pr(3, max(lr.top + 3, clipTop + 1), m_labelW - 3, lr.bottom - 3);
		if (pr.Height() <= 4) continue;
		dc.FillSolidRect(pr, RGB(210, 220, 240));
		dc.Draw3dRect(pr, RGB(255, 255, 255), RGB(160, 170, 190));
		dc.SetTextColor(RGB(50, 55, 70));
		dc.DrawText(LaneName(lane), -1, pr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	dc.FillSolidRect(CRect(0, 0, m_labelW, clipTop), RGB(220, 225, 235));
	dc.SetTextColor(RGB(60, 60, 75));
	dc.DrawText(L"Lane", CRect(0, 0, m_labelW, clipTop), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

	// 再生ヘッドは時間軸ヘッダも含めてクリップ外で描く
	const double now = MpGetPerformanceTimeSec();
	m_lastDrawnPlay = now;
	const int hx = SecToX(now);
	if (hx >= m_labelW && hx < rc.right) {
		CPen head(PS_SOLID, 2, RGB(220, 40, 60));
		CPen* oldHead = dc.SelectObject(&head);
		dc.MoveTo(hx, 0);
		dc.LineTo(hx, rc.bottom);
		dc.SelectObject(oldHead);
		dc.SetTextColor(RGB(180, 30, 50));
		CString nowLab; nowLab.Format(L"▶%.1f", now);
		dc.TextOut(hx + 3, 2, nowLab);
	}

	dc.SelectObject(oldPen);
	dc.SelectObject(oldFont);
}

void CCommandRollView::SyncSoft3DCamFromSave()
{
	GdiSoft3D::CamFromSaved(m_cam3d, savedata.mpCmdRoll3dyaw, savedata.mpCmdRoll3dpitch, savedata.mpCmdRoll3dzoom);
}

void CCommandRollView::PersistSoft3DCam()
{
	GdiSoft3D::CamToSaved(m_cam3d, savedata.mpCmdRoll3dyaw, savedata.mpCmdRoll3dpitch, savedata.mpCmdRoll3dzoom);
}

void CCommandRollView::Soft3dYawCb(void* ctx, int value)
{
	auto* self = (CCommandRollView*)ctx;
	if (!self) return;
	self->m_cam3d.yawDeg = (float)value / 10.f;
	GdiSoft3D::ClampCam(self->m_cam3d);
	self->PersistSoft3DCam();
	self->InvalidateRoll();
}
void CCommandRollView::Soft3dPitchCb(void* ctx, int value)
{
	auto* self = (CCommandRollView*)ctx;
	if (!self) return;
	self->m_cam3d.pitchDeg = (float)value / 10.f;
	GdiSoft3D::ClampCam(self->m_cam3d);
	self->PersistSoft3DCam();
	self->InvalidateRoll();
}
void CCommandRollView::Soft3dZoomCb(void* ctx, int value)
{
	auto* self = (CCommandRollView*)ctx;
	if (!self) return;
	self->m_cam3d.zoom = (float)value / 100.f;
	GdiSoft3D::ClampCam(self->m_cam3d);
	self->PersistSoft3DCam();
	self->InvalidateRoll();
}

void CCommandRollView::PaintRollSoft3D(CDC& dc, const CRect& rc)
{
	const int w = rc.Width(), h = rc.Height();
	if (w < 8 || h < 8) return;
	if (CCustomPopupMenu::GetTrackingRoot() != NULL) {
		dc.FillSolidRect(&rc, RGB(32, 34, 42));
		return;
	}
	// スクロール／ドラッグ中は軽量パス（毎フレ Soft2D 後処理や文字投影を避ける）
	const BOOL interacting = (m_userScrollTick != 0
		&& (GetTickCount() - m_userScrollTick) < 180u)
		|| (::GetCapture() == m_hWnd);

	dc.FillSolidRect(0, 0, w, h, RGB(32, 34, 42));

	const float laneDepth = 0.085f;
	const float farZ = laneDepth * (float)kLaneCount + 0.20f;
	const float boxes[1][6] = { { -1.25f, 1.15f, -0.02f, 0.65f, -0.08f, farZ } };
	GdiSoft3D::View v;
	GdiSoft3D::BuildView(w, h, m_cam3d, boxes, 1, v);
	GdiSoft3D::Scene sc;
	sc.Begin(v);

	const double viewSec = max(1.0, (double)max(1, w - m_labelW) / m_pxPerSec);
	const double sec0 = m_scrollSec;
	const double sec1 = m_scrollSec + viewSec;
	auto secToX = [&](double t) -> float {
		float u = (float)((t - sec0) / (sec1 - sec0));
		if (u < -0.2f) u = -0.2f;
		if (u > 1.2f) u = 1.2f;
		return -1.0f + 2.0f * u;
	};

	for (int lane = kLaneCount - 1; lane >= 0; --lane) {
		const float z0 = laneDepth * (float)lane;
		const float z1 = z0 + laneDepth * 0.92f;
		const float yFloor = (lane % 2) ? 0.012f : 0.0f;
		COLORREF floor = (lane % 2) ? RGB(55, 58, 72) : RGB(48, 50, 62);
		sc.AddBox(-1.0f, 1.0f, yFloor, z0, z1, floor, yFloor - 0.01f);
	}

	for (int lane = kLaneCount - 1; lane >= 0; --lane) {
		const float yBase = (lane % 2) ? 0.012f : 0.0f;
		const float z0 = laneDepth * (float)lane + laneDepth * 0.15f;
		const float z1 = z0 + laneDepth * 0.55f;
		for (int i = 0; i < m_evCount; ++i) {
			if (LaneFromEvent(m_ev[i]) != lane) continue;
			const MpPromptSnapshotEvent& ev = m_ev[i];
			float x0 = secToX(ev.t0);
			float x1 = secToX(ev.t1);
			if (x1 < x0 + 0.02f) x1 = x0 + 0.02f;
			if (x1 < -1.05f || x0 > 1.05f) continue;
			float topY = yBase + 0.18f;
			if (ev.hasVal) {
				int vv = max(ev.v0, ev.v1);
				if (vv < 0) vv = 0;
				if (vv > 200) vv = 200;
				topY = yBase + 0.08f + (float)vv / 200.f * 0.40f;
			}
			const BOOL pct = (ev.period > 0.001);
			COLORREF fill = ev.isPreset ? RGB(180, 140, 220) : (pct ? RGB(120, 180, 210) : RGB(100, 170, 130));
			if (i == m_sel) fill = RGB(255, 170, 80);
			sc.AddBox(x0, x1, topY, z0, z1, fill, yBase);
		}
	}

	{
		const float hx = secToX(MpGetPerformanceTimeSec());
		if (hx >= -1.05f && hx <= 1.05f) {
			sc.AddBox(hx - 0.010f, hx + 0.010f, 0.55f, -0.05f, farZ, RGB(220, 60, 80), 0.0f);
		}
	}

	sc.Flush(dc);

	// 文字／Soft2D後処理は定着時のみ（スクロール中は箱だけで十分・体感が3倍遅くなる主因だった）
	if (!interacting) {
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(230, 235, 245));
		CFont* of = dc.SelectObject(GetFont());
		const double step = (m_pxPerSec >= 120) ? 0.25
			: (m_pxPerSec >= 60) ? 0.5
			: (m_pxPerSec >= 24) ? 1.0
			: (m_pxPerSec >= 8) ? 5.0
			: 10.0;
		for (double t = floor(sec0 / step) * step; t <= sec1 + step; t += step) {
			const float x = secToX(t);
			if (x < -1.05f || x > 1.05f) continue;
			POINT p;
			GdiSoft3D::Project(v, x, 0.36f, -0.02f, p);
			CString lab;
			if (step < 1.0) lab.Format(L"%.2f", t);
			else lab.Format(L"%.0f", t);
			CRect tr(p.x - 28, p.y - 10, p.x + 28, p.y + 10);
			dc.FillSolidRect(&tr, RGB(40, 44, 58));
			dc.DrawText(lab, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		for (int lane = 0; lane < kLaneCount; ++lane) {
			const float z = laneDepth * ((float)lane + 0.45f);
			const float y0 = ((lane % 2) ? 0.012f : 0.0f) + 0.08f;
			POINT p;
			GdiSoft3D::Project(v, -1.12f, y0, z, p);
			CString name = LaneName(lane);
			CRect tr(p.x - 36, p.y - 9, p.x + 36, p.y + 9);
			dc.FillSolidRect(&tr, RGB(36, 40, 54));
			dc.DrawText(name, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		if (of) dc.SelectObject(of);

		static GdiSoft2D::Context s2;
		static int s2w = 0, s2h = 0;
		if (s2w != w || s2h != h || !s2.fb.hdc) {
			if (s2.Create(w, h, false) && s2.fb.hdc) {
				s2w = w; s2h = h;
			}
		}
		if (s2.fb.hdc && s2w == w && s2h == h) {
			::BitBlt(s2.fb.hdc, 0, 0, w, h, dc.GetSafeHdc(), 0, 0, SRCCOPY);
			s2.Vignette(0.28f);
			s2.Present(dc, 0, 0);
		}
	}
}

void CCommandRollView::AutoFollowPlayhead(double t)
{
	if (!m_followPlay && m_userScrollTick != 0) {
		if (GetTickCount() - m_userScrollTick > 2500)
			m_followPlay = TRUE;
	}
	if (!m_followPlay || !plf) return;
	CRect rc = ClientRoll();
	const double viewSec = max(1.0, (double)(rc.Width() - m_labelW) / m_pxPerSec);
	if (t < m_scrollSec + viewSec * 0.12 || t > m_scrollSec + viewSec * 0.80) {
		m_scrollSec = max(0.0, t - viewSec * 0.30);
		ClampScroll();
		SyncScrollBars();
	}
}

void CCommandRollView::TickPlayhead()
{
	const double t = MpGetPerformanceTimeSec();
	EnsureDurationFloor(t);
	const double oldScroll = m_scrollSec;
	AutoFollowPlayhead(t);
	// ヘッド位置か追随スクロールが変わったときだけ再描画。
	// （旧: plf&&follow で毎タイマ Invalidate → 再生中に常時フル描画して重くなる）
	const int hxOld = (m_lastDrawnPlay < 0) ? -99999 : SecToX(m_lastDrawnPlay);
	const int hxNew = SecToX(t);
	BOOL need = (hxOld != hxNew || m_scrollSec != oldScroll);
	// Soft3D 軽量パス解除直後にラベル／ヴィネット品質パスを1回戻す
	if (IsSoft3D() && m_userScrollTick != 0) {
		const DWORD age = GetTickCount() - m_userScrollTick;
		static DWORD s_qualityForTick = 0;
		if (age >= 180u && s_qualityForTick != m_userScrollTick) {
			s_qualityForTick = m_userScrollTick;
			need = TRUE;
		}
	}
	if (need)
		InvalidateRoll();
}

void CCommandRollView::SetPxPerSec(double px)
{
	if (px < 1.5) px = 1.5;
	if (px > 480.0) px = 480.0;
	m_pxPerSec = px;
	int z10 = (int)(m_pxPerSec * 10.0 + 0.5);
	if (z10 < 15) z10 = 15;
	if (z10 > 4800) z10 = 4800;
	savedata.mpCmdRollPxPerSec10 = z10;
}

void CCommandRollView::ZoomIn()
{
	CRect rc = ClientRoll();
	const double center = m_scrollSec + (rc.Width() - m_labelW) * 0.5 / m_pxPerSec;
	SetPxPerSec(m_pxPerSec * 1.35);
	m_scrollSec = center - (rc.Width() - m_labelW) * 0.5 / m_pxPerSec;
	NoteUserScroll();
	SyncScrollBars();
	InvalidateRoll();
}

void CCommandRollView::ZoomOut()
{
	CRect rc = ClientRoll();
	const double center = m_scrollSec + (rc.Width() - m_labelW) * 0.5 / m_pxPerSec;
	SetPxPerSec(m_pxPerSec / 1.35);
	m_scrollSec = center - (rc.Width() - m_labelW) * 0.5 / m_pxPerSec;
	NoteUserScroll();
	SyncScrollBars();
	InvalidateRoll();
}

void CCommandRollView::OnPaint()
{
	CPaintDC pdc(this);
	CRect rc = ClientRoll();
	const int w = rc.Width();
	const int h = rc.Height();
	if (w <= 0 || h <= 0) return;
	EnsureMemDC(w, h);
	PaintRoll(m_memDC, CRect(0, 0, w, h));
	// ホスト REDIRECTIONBITMAP_ALPHA 時は素 BitBlt が α=0 で全透明になる
#if CCUSTOM_AERO_SUPPORT
	if (CCC_AcrylicCaption(::GetParent(m_hWnd)) || CCC_IsAeroEnabled()) {
		CCC_BlitStretchOpaque(pdc.GetSafeHdc(), 0, 0, w, h,
			m_memDC.GetSafeHdc(), 0, 0, w, h);
		return;
	}
#endif
	pdc.BitBlt(0, 0, w, h, &m_memDC, 0, 0, SRCCOPY);
}

BOOL CCommandRollView::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

void CCommandRollView::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	SyncScrollBars();
	InvalidateRoll();
}

void CCommandRollView::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* /*pScrollBar*/)
{
	CRect rc = ClientRoll();
	const int line = max(8, (int)(m_pxPerSec * 2.0));
	const int page = max(line, rc.Width() - m_labelW);
	int pos = (int)(m_scrollSec * m_pxPerSec + 0.5);
	const int maxPos = max(0, ContentWidthPx() - rc.Width());
	switch (nSBCode) {
	case SB_LINELEFT: pos -= line; break;
	case SB_LINERIGHT: pos += line; break;
	case SB_PAGELEFT: pos -= page; break;
	case SB_PAGERIGHT: pos += page; break;
	case SB_THUMBTRACK: pos = ScrollGetPos(this, SB_HORZ, TRUE); break;
	case SB_THUMBPOSITION: pos = ScrollGetPos(this, SB_HORZ, FALSE); break;
	case SB_LEFT: pos = 0; break;
	case SB_RIGHT: pos = maxPos; break;
	default: return;
	}
	if (pos < 0) pos = 0;
	if (pos > maxPos) pos = maxPos;
	m_scrollSec = (double)pos / max(m_pxPerSec, 0.001);
	NoteUserScroll();
	// THUMBTRACK 中の SetScrollInfo は毎ピクセル高コスト。描画だけ先に回す
	if (nSBCode != SB_THUMBTRACK)
		SyncScrollBars();
	InvalidateRoll();
}

void CCommandRollView::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* /*pScrollBar*/)
{
	CRect rc = ClientRoll();
	const int line = m_laneH;
	const int page = max(line, rc.Height() - kHeaderH);
	int pos = m_scrollY;
	const int maxPos = max(0, ContentHeightPx() - rc.Height());
	switch (nSBCode) {
	case SB_LINEUP: pos -= line; break;
	case SB_LINEDOWN: pos += line; break;
	case SB_PAGEUP: pos -= page; break;
	case SB_PAGEDOWN: pos += page; break;
	case SB_THUMBTRACK: pos = ScrollGetPos(this, SB_VERT, TRUE); break;
	case SB_THUMBPOSITION: pos = ScrollGetPos(this, SB_VERT, FALSE); break;
	case SB_TOP: pos = 0; break;
	case SB_BOTTOM: pos = maxPos; break;
	default: return;
	}
	if (pos < 0) pos = 0;
	if (pos > maxPos) pos = maxPos;
	m_scrollY = pos;
	NoteUserScroll();
	if (nSBCode != SB_THUMBTRACK)
		SyncScrollBars();
	InvalidateRoll();
}

BOOL CCommandRollView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (IsSoft3D() && !(nFlags & (MK_SHIFT | MK_CONTROL))) {
		GdiSoft3D::WheelZoom(m_cam3d, zDelta);
		PersistSoft3DCam();
		NoteUserScroll();
		InvalidateRoll();
		return TRUE;
	}
	const int steps = (zDelta > 0) ? -1 : 1;
	if (nFlags & MK_SHIFT) {
		m_scrollY += steps * m_laneH * 3;
	}
	else if (nFlags & MK_CONTROL) {
		if (steps < 0) ZoomIn();
		else ZoomOut();
		return TRUE;
	}
	else {
		m_scrollSec += (double)steps * (20.0 / m_pxPerSec);
	}
	NoteUserScroll();
	ClampScroll();
	SyncScrollBars();
	InvalidateRoll();
	return TRUE;
}

void CCommandRollView::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	if (IsSoft3D()) {
		m_rotDragging = true;
		m_rotOrigin = point;
		m_rotYaw0 = m_cam3d.yawDeg;
		m_rotPitch0 = m_cam3d.pitchDeg;
		SetCapture();
		return;
	}
	m_downPt = point;
	const int pal = HitTestPalette(point);
	if (pal >= 0) {
		SetCapture();
		m_paletteDrag = TRUE; m_dragLane = pal;
		m_dragT0 = m_dragT1 = XToSec(point.x);
		return;
	}
	const int hit = HitTestEvent(point);
	if (hit >= 0) {
		SetCapture();
		m_sel = hit; m_dragging = TRUE; InvalidateRoll();
		return;
	}
	const int lane = HitTestLane(point);
	if (lane >= 0) {
		SetCapture();
		m_creating = TRUE; m_dragLane = lane;
		m_dragT0 = m_dragT1 = XToSec(point.x); m_sel = -1;
	}
}

void CCommandRollView::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_rotDragging) {
		if (!(nFlags & MK_LBUTTON)) {
			m_rotDragging = false;
			if (GetCapture() == this) ReleaseCapture();
			PersistSoft3DCam();
			InvalidateRoll();
			return;
		}
		GdiSoft3D::OrbitDrag(m_cam3d, m_rotYaw0, m_rotPitch0, m_rotOrigin, point);
		NoteUserScroll();
		InvalidateRoll();
		return;
	}
	if (m_paletteDrag || m_creating) {
		m_dragT1 = XToSec(point.x);
		InvalidateRoll();
	}
	else if (m_dragging && m_sel >= 0 && m_sel < m_evCount) {
		const double dt = XToSec(point.x) - XToSec(m_downPt.x);
		MpPromptSnapshotEvent& ev = m_ev[m_sel];
		const double dur = ev.t1 - ev.t0;
		ev.t0 = max(0.0, ev.t0 + dt);
		ev.t1 = ev.t0 + max(0.0, dur);
		m_downPt = point;
		InvalidateRoll();
	}
}

void CCommandRollView::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_rotDragging) {
		m_rotDragging = false;
		if (GetCapture() == this) ReleaseCapture();
		PersistSoft3DCam();
		InvalidateRoll();
		return;
	}
	if (GetCapture() == this) ReleaseCapture();
	if (m_paletteDrag || m_creating) {
		m_paletteDrag = FALSE;
		m_creating = FALSE;
		MpPromptSnapshotEvent ev{};
		BOOL preset = FALSE;
		LettersForLane(m_dragLane, ev.c1, ev.c2, preset);
		ev.isPreset = preset;
		ev.hasVal = !preset;
		ev.t0 = min(m_dragT0, m_dragT1);
		ev.t1 = max(m_dragT0, m_dragT1);
		if (ev.t1 - ev.t0 < 0.5) ev.t1 = ev.t0;
		ev.v0 = ev.v1 = 100;
		if (ev.c1 == 'r' || ev.c1 == 'c' || ev.c1 == 'y') ev.v0 = ev.v1 = 40;
		if (ev.c1 == 'E') ev.v0 = ev.v1 = 7;
		if (ev.c1 == 'F') { ev.v0 = 0; ev.v1 = 80; }
		if (!RunPlaceDialog(ev)) { InvalidateRoll(); return; }
		if (m_evCount < kMaxEv) {
			m_ev[m_evCount++] = ev;
			m_sel = m_evCount - 1;
			CommitToPeer();
		}
	}
	else if (m_dragging) {
		m_dragging = FALSE;
		CommitToPeer();
	}
}

void CCommandRollView::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	if (IsSoft3D()) return; // 編集は2Dのみ
	const int hit = HitTestEvent(point);
	if (hit < 0) return;
	MpPromptSnapshotEvent ev = m_ev[hit];
	if (!RunPlaceDialog(ev)) return;
	m_ev[hit] = ev;
	m_sel = hit;
	CommitToPeer();
}

void CCommandRollView::OnRButtonUp(UINT nFlags, CPoint point)
{
	UNREFERENCED_PARAMETER(nFlags);
	CPoint sp = point;
	ClientToScreen(&sp);
	OnContextMenu(this, sp);
}

void CCommandRollView::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	enum {
		IDM_CR_VIEW_2D = 42320,
		IDM_CR_VIEW_3D = 42321
	};
	CCustomPopupMenu menu;
	CCustomPopupMenu* subView = menu.AddSubMenu(
		LL14(L"表示モード", L"View mode", L"Mode d'affichage", L"Modalita di visualizzazione", L"Modo de visualizacion", L"표시 모드", L"显示模式", L"وضع العرض", L"Режим отображения", L"Anzeigemodus", L"Modo de exibicao", L"Weergavemodus", L"Tryb wyswietlania", L"Goruntuleme modu"),
		LL14(L"ロールの表示モード（通常2D／簡易3D）を選びます。編集は2Dのみです。", L"Choose roll view mode (normal 2D / soft 3D). Edit only in 2D.", L"Choisir le mode (2D / 3D). Edition en 2D seulement.", L"Scegli modalita (2D / 3D). Modifica solo in 2D.", L"Elegir modo (2D / 3D). Edicion solo en 2D.", L"표시 모드(일반 2D/간이 3D). 편집은 2D만.", L"选择显示模式（普通2D/简易3D）。仅在2D中编辑。", L"اختر الوضع (2D / 3D). التحرير في 2D فقط.", L"Режим (2D / 3D). Правка только в 2D.", L"Anzeigemodus (2D / 3D). Bearbeiten nur in 2D.", L"Modo (2D / 3D). Edicao apenas em 2D.", L"Modus (2D / 3D). Bewerken alleen in 2D.", L"Tryb (2D / 3D). Edycja tylko w 2D.", L"Gorunum (2D / 3D). Duzenleme sadece 2D."));
	if (subView) {
		subView->AddCheck(IDM_CR_VIEW_2D,
			LL14(L"通常 (2D)", L"Normal (2D)", L"Normal (2D)", L"Normale (2D)", L"Normal (2D)", L"일반 (2D)", L"普通 (2D)", L"عادي (2D)", L"Обычный (2D)", L"Normal (2D)", L"Normal (2D)", L"Normaal (2D)", L"Zwykly (2D)", L"Normal (2D)"),
			!IsSoft3D());
		subView->AddCheck(IDM_CR_VIEW_3D,
			LL14(L"簡易3D", L"Soft 3D", L"3D simplifie", L"3D semplificato", L"3D simple", L"간이 3D", L"简易3D", L"ثلاثي الأبعاد مبسط", L"Простой 3D", L"Einfaches 3D", L"3D simples", L"Eenvoudig 3D", L"Uproszczone 3D", L"Basit 3B"),
			IsSoft3D());
		if (IsSoft3D()) {
			int yaw10 = (int)(m_cam3d.yawDeg * 10.f);
			int pit10 = (int)(m_cam3d.pitchDeg * 10.f);
			int zoomPct = (int)(m_cam3d.zoom * 100.f + 0.5f);
			if (yaw10 < -1800) yaw10 = -1800; if (yaw10 > 1800) yaw10 = 1800;
			if (pit10 < -850) pit10 = -850; if (pit10 > 850) pit10 = 850;
			if (zoomPct < 35) zoomPct = 35; if (zoomPct > 400) zoomPct = 400;
			subView->AddSeparator();
			subView->AddSlider(LL14(L"Yaw (0.1°)", L"Yaw (0.1°)", L"Lacet (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"偏航 (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)"),
				-1800, 1800, yaw10, &CCommandRollView::Soft3dYawCb, this,
				LL14(L"水平回転（ドラッグ中に反映）", L"Horizontal rotation (live)", L"Rotation horizontale (direct)", L"Rotazione orizzontale (live)", L"Rotacion horizontal (en vivo)", L"수평 회전(즉시)", L"水平旋转（即时）", L"دوران أفقي (مباشر)", L"Горизонтальный поворот (сразу)", L"Horizontale Drehung (live)", L"Rotacao horizontal (ao vivo)", L"Horizontale rotatie (live)", L"Obrot poziomy (na zywo)", L"Yatay donus (anlik)"));
			subView->AddSlider(LL14(L"Pitch (0.1°)", L"Pitch (0.1°)", L"Tangage (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"俯仰 (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)"),
				-850, 850, pit10, &CCommandRollView::Soft3dPitchCb, this,
				LL14(L"仰角（ドラッグ中に反映）", L"Elevation angle (live)", L"Angle d'elevation (direct)", L"Angolo di elevazione (live)", L"Angulo de elevacion (en vivo)", L"앙각(즉시)", L"仰角（即时）", L"زاوية الارتفاع (مباشر)", L"Угол наклона (сразу)", L"Neigungswinkel (live)", L"Angulo de elevacao (ao vivo)", L"Elevatiehoek (live)", L"Kat nachylenia (na zywo)", L"Yukselis acisi (anlik)"));
			subView->AddSlider(LL14(L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"缩放 (%)", L"تكبير (%)", L"Масштаб (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)"),
				35, 400, zoomPct, &CCommandRollView::Soft3dZoomCb, this,
				LL14(L"拡大縮小（ドラッグ中に反映）", L"Zoom (live)", L"Zoom (direct)", L"Zoom (live)", L"Zoom (en vivo)", L"확대/축소(즉시)", L"缩放（即时）", L"تكبير (مباشر)", L"Масштаб (сразу)", L"Zoom (live)", L"Zoom (ao vivo)", L"Zoom (live)", L"Powiększenie (na zywo)", L"Yakinlastirma (anlik)"));
		}
	}
	if (point.x == -1 && point.y == -1) {
		CRect rc; GetClientRect(&rc); ClientToScreen(&rc);
		point = CPoint(rc.left + 8, rc.top + 8);
	}
	const UINT cmd = menu.Track(point, this);
	if (cmd == IDM_CR_VIEW_2D) {
		savedata.mpCmdRollviewmode = 0;
		InvalidateRoll();
	}
	else if (cmd == IDM_CR_VIEW_3D) {
		savedata.mpCmdRollviewmode = 1;
		SyncSoft3DCamFromSave();
		InvalidateRoll();
	}
}

void CCommandRollView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if ((nChar == VK_DELETE || nChar == VK_BACK) && m_sel >= 0 && m_sel < m_evCount) {
		for (int i = m_sel; i + 1 < m_evCount; ++i) m_ev[i] = m_ev[i + 1];
		--m_evCount; m_sel = -1;
		CommitToPeer();
		return;
	}
	if (nChar == VK_LEFT) { OnHScroll(SB_LINELEFT, 0, nullptr); return; }
	if (nChar == VK_RIGHT) { OnHScroll(SB_LINERIGHT, 0, nullptr); return; }
	if (nChar == VK_UP) { OnVScroll(SB_LINEUP, 0, nullptr); return; }
	if (nChar == VK_DOWN) { OnVScroll(SB_LINEDOWN, 0, nullptr); return; }
	if (nChar == VK_HOME) {
		m_followPlay = TRUE;
		m_scrollSec = MpGetPerformanceTimeSec();
		ClampScroll();
		SyncScrollBars();
		InvalidateRoll();
		return;
	}
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

// ---------------------------------------------------------------------------
// CCommandRollDlg
// ---------------------------------------------------------------------------
CCommandRollDlg::CCommandRollDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_MP_CMDROLL, pParent) {}

CCommandRollDlg::~CCommandRollDlg()
{
	if (m_brDlg.GetSafeHandle()) m_brDlg.DeleteObject();
}

void CCommandRollDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MCR_CLOSE, m_close);
	DDX_Control(pDX, IDC_MCR_ZOOMIN, m_zoomIn);
	DDX_Control(pDX, IDC_MCR_ZOOMOUT, m_zoomOut);
	DDX_Control(pDX, IDC_MCR_HELP, m_help);
	DDX_Control(pDX, IDC_MCR_TIME, m_timeLbl);
	DDX_Control(pDX, IDC_MCR_ANALYZE, m_analyze);
	DDX_Control(pDX, IDC_MCR_RUN, m_run);
	DDX_Control(pDX, IDC_MCR_STOP, m_stop);
	DDX_Control(pDX, IDC_MCR_RESET, m_reset);
	DDX_Control(pDX, IDC_MCR_CLEAR, m_clear);
	DDX_Control(pDX, IDC_MCR_MODE, m_mode);
	DDX_Control(pDX, IDC_MCR_MODE_L, m_modeLbl);
}

BEGIN_MESSAGE_MAP(CCommandRollDlg, CCustomBlurDialogExBase)
	ON_BN_CLICKED(IDC_MCR_CLOSE, &CCommandRollDlg::OnCloseBtn)
	ON_BN_CLICKED(IDC_MCR_ZOOMIN, &CCommandRollDlg::OnZoomIn)
	ON_BN_CLICKED(IDC_MCR_ZOOMOUT, &CCommandRollDlg::OnZoomOut)
	ON_BN_CLICKED(IDC_MCR_HELP, &CCommandRollDlg::OnHelpBtn)
	ON_BN_CLICKED(IDC_MCR_ANALYZE, &CCommandRollDlg::OnAnalyze)
	ON_BN_CLICKED(IDC_MCR_RUN, &CCommandRollDlg::OnRun)
	ON_BN_CLICKED(IDC_MCR_STOP, &CCommandRollDlg::OnStop)
	ON_BN_CLICKED(IDC_MCR_RESET, &CCommandRollDlg::OnReset)
	ON_BN_CLICKED(IDC_MCR_CLEAR, &CCommandRollDlg::OnClear)
	ON_CBN_SELCHANGE(IDC_MCR_MODE, &CCommandRollDlg::OnModeSel)
	ON_WM_SIZE()
	ON_WM_ENTERSIZEMOVE()
	ON_WM_EXITSIZEMOVE()
	ON_WM_MOVING()
	ON_WM_GETMINMAXINFO()
	ON_WM_TIMER()
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

void CCommandRollDlg::SetPromptPeer(CPromptDlg* peer)
{
	m_peer = peer;
	m_view.SetPeer(peer);
	m_view.SetSyncGen(&m_syncGen);
}

void CCommandRollDlg::ReloadFromText(const CString& text, UINT syncGen)
{
	if (m_view.IsPushing()) return;
	if (syncGen && syncGen == m_syncGen) return;
	m_syncGen = syncGen;
	MpPromptParse(text, nullptr);
	m_view.ReloadFromParse();
}

void CCommandRollDlg::SavePosToSavedata()
{
	if (!::IsWindow(GetSafeHwnd()) || IsIconic()) return;
	CRect r;
	GetWindowRect(&r);
	savedata.mpCmdRollX = r.left;
	savedata.mpCmdRollY = r.top;
	savedata.mpCmdRollW = r.Width();
	savedata.mpCmdRollH = r.Height();
	savedata.mpCmdRollHasPos = 1;
	int z10 = (int)(m_view.GetPxPerSec() * 10.0 + 0.5);
	if (z10 < 15) z10 = 15;
	if (z10 > 4800) z10 = 4800;
	savedata.mpCmdRollPxPerSec10 = z10;
}

void CCommandRollDlg::RestorePosFromSavedata()
{
	int x = savedata.mpCmdRollX, y = savedata.mpCmdRollY;
	int w = savedata.mpCmdRollW, h = savedata.mpCmdRollH;
	if (!savedata.mpCmdRollHasPos || w < 400 || h < 280 || w > 10000 || h > 10000) {
		w = 900;
		h = 560;
		if (GetParent() && ::IsWindow(GetParent()->GetSafeHwnd())) {
			CRect pr;
			GetParent()->GetWindowRect(&pr);
			x = pr.left + 48;
			y = pr.top + 48;
		}
		else {
			x = 120;
			y = 120;
		}
	}
	RECT rcWork{};
	SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
	if (x < rcWork.left - 50 || x > rcWork.right - 50) x = rcWork.left + 40;
	if (y < rcWork.top - 10 || y > rcWork.bottom - 50) y = rcWork.top + 40;
	MoveWindow(x, y, w, h);
	m_posRestored = TRUE;
}

void CCommandRollDlg::LayoutControls()
{
	if (!::IsWindow(GetSafeHwnd())) return;
	CRect rc; GetClientRect(&rc);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int M = 8;
	const int btnH = 24;
	const int topY = capH + M - 2; // カスタム帯の下（被せるとアクリルが消える）
	if (capH > 0)
		CCC_MainLockClearHeaderRow(m_hWnd);
	else
		CCC_MainLockSetHeaderRow(m_hWnd, topY, btnH);
	const int lockReserve = (capH > 0) ? 0 : CCC_MainLockGetReserveWidth(m_hWnd);
	int rightEdge = rc.right - M - lockReserve;
	if (rightEdge < M + 240)
		rightEdge = rc.right - M;

	if (m_close.GetSafeHwnd())
		m_close.MoveWindow(rightEdge - 70, topY, 70, btnH);
	int x = rightEdge - 70 - 4;
	if (m_help.GetSafeHwnd()) {
		x -= 28;
		m_help.MoveWindow(x, topY, 28, 22);
		x -= 4;
	}
	if (m_zoomIn.GetSafeHwnd()) {
		x -= 32;
		m_zoomIn.MoveWindow(x, topY, 32, 22);
		x -= 4;
	}
	if (m_zoomOut.GetSafeHwnd()) {
		x -= 32;
		m_zoomOut.MoveWindow(x, topY, 32, 22);
	}

	int timeRight = x - 8;
	if (timeRight < M + 40) timeRight = M + 40;
	if (m_timeLbl.GetSafeHwnd())
		m_timeLbl.MoveWindow(M, topY, max(40, timeRight - M), 18);

	const int bottomH = 30;
	const int progH = 14;
	const int bottomY = rc.bottom - M - bottomH;
	const int progY = bottomY - 4 - progH;
	int bx = M;
	if (m_modeLbl.GetSafeHwnd()) {
		m_modeLbl.MoveWindow(bx, bottomY + 6, 40, 16);
		bx += 42;
	}
	if (m_mode.GetSafeHwnd()) {
		m_mode.MoveWindow(bx, bottomY + 2, 150, 120);
		bx += 158;
	}
	auto placeBtn = [&](CWnd& w, int bw) {
		if (!w.GetSafeHwnd()) return;
		w.MoveWindow(bx, bottomY, bw, bottomH);
		bx += bw + 4;
	};
	placeBtn(m_analyze, 52);
	placeBtn(m_run, 52);
	placeBtn(m_stop, 52);
	placeBtn(m_reset, 60);
	placeBtn(m_clear, 52);

	if (m_progress.GetSafeHwnd())
		m_progress.MoveWindow(M, progY, max(40, rc.right - M - M), progH);

	if (CWnd* host = GetDlgItem(IDC_MCR_HOST)) host->ShowWindow(SW_HIDE);
	const int viewTop = topY + btnH + 4;
	CRect viewRc(M, viewTop, rc.right - M, progY - 4);
	// 高さが潰れたら本文側を優先して最低高を確保（ボタン帯より上に収める）
	if (viewRc.Height() < 40) {
		viewRc.top = max(capH + 2, progY - 4 - 40);
		viewRc.bottom = progY - 4;
		if (viewRc.Height() < 20)
			viewRc.bottom = viewRc.top + 20;
	}
	if (m_view.GetSafeHwnd())
		m_view.MoveWindow(viewRc);
	CCC_CaptionLayout(m_hWnd);
	CCC_MainLockBringToFront(m_hWnd);
}

void CCommandRollDlg::SetupTooltips()
{
	// 本体ビューには出さない。上部/下部ボタンだけ。
	if (!CCustomControlUtility::BeginDialogToolTip(m_tooltip, this))
		return;
	auto addTip = [this](CWnd& w, LPCTSTR text) {
		if (!text || !w.GetSafeHwnd()) return;
		m_tooltip.AddTool(&w, text);
	};
	addTip(m_close, LL14(L"閉じる(位置は保存)", L"Close (position saved)", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	addTip(m_zoomIn, LL14(L"拡大 (Ctrl+ホイール上)", L"Zoom in (Ctrl+Wheel up)", L"Zoom +", L"Zoom +", L"Zoom +", L"확대", L"放大", L"Zoom +", L"Zoom +", L"Zoom +", L"Zoom +", L"Zoom +", L"Zoom +", L"Zoom +"));
	addTip(m_zoomOut, LL14(L"縮小 (Ctrl+ホイール下)", L"Zoom out (Ctrl+Wheel down)", L"Zoom -", L"Zoom -", L"Zoom -", L"축소", L"缩小", L"Zoom -", L"Zoom -", L"Zoom -", L"Zoom -", L"Zoom -", L"Zoom -", L"Zoom -"));
	addTip(m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guia", L"조작 가이드 표시", L"显示操作指南", L"Show guide", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaz przewodnik", L"Islem kilavuzunu goster"));
	addTip(m_run, LL14(L"ロールのコマンドを解析し、演奏中にパラメータを自動変更します。", L"Parse roll commands and apply parameter changes during playback.", L"Analyser et appliquer pendant la lecture.", L"Analizza e applica in riproduzione.", L"Analizar y aplicar durante la reproduccion.", L"롤 명령을 해석해 연주 중 자동 적용합니다.", L"解析卷轴命令并在播放中自动应用。", L"Parse and apply during playback.", L"Разобрать и применять при воспроизведении.", L"Parsen und waehrend Wiedergabe anwenden.", L"Analisar e aplicar na reproducao.", L"Parseren en tijdens afspelen toepassen.", L"Parsuj i stosuj podczas odtwarzania.", L"Ayristirip calma sirasinda uygula."));
	addTip(m_analyze, LL14(L"選択曲を読込しながら解析し、コマンドを自動生成します(再生は一時停止)。", L"Analyze selected track and auto-generate commands (playback pauses).", L"Analyser la piste et generer des commandes.", L"Analizza la traccia e genera comandi.", L"Analizar la pista y generar comandos.", L"선택 곡을 분석해 명령을 자동 생성합니다.", L"分析所选曲目并自动生成命令。", L"Analyze track and generate commands.", L"Анализ трека и генерация команд.", L"Titel analysieren und Befehle erzeugen.", L"Analisar faixa e gerar comandos.", L"Track analyseren en opdrachten genereren.", L"Analizuj utwor i generuj komendy.", L"Parcayi analiz edip komut uret."));
	addTip(m_stop, LL14(L"プロンプト実行を停止します(設定値は維持)。", L"Stop prompt execution (keep current settings).", L"Arreter l'execution.", L"Ferma l'esecuzione.", L"Detener la ejecucion.", L"실행을 중지합니다(값 유지).", L"停止执行(保留设置)。", L"Stop execution.", L"Остановить.", L"Ausfuehrung stoppen.", L"Parar execucao.", L"Uitvoering stoppen.", L"Zatrzymaj.", L"Calistirmayi durdur."));
	addTip(m_reset, LL14(L"実行前の設定に戻し、実行を停止します。", L"Restore pre-run settings and stop.", L"Restaurer et arreter.", L"Ripristina e ferma.", L"Restaurar y detener.", L"실행 전으로 되돌리고 중지.", L"恢复执行前并停止。", L"Restore and stop.", L"Сбросить и остановить.", L"Zuruecksetzen und stoppen.", L"Restaurar e parar.", L"Herstellen en stoppen.", L"Przywroc i zatrzymaj.", L"Onceki ayara don ve durdur."));
	addTip(m_clear, LL14(L"コマンドを消去し、設定も初期状態に戻します。", L"Clear commands and restore initial settings.", L"Effacer et reinitialiser.", L"Cancella e ripristina.", L"Borrar y restaurar.", L"명령을 지우고 초기화합니다.", L"清除命令并恢复初始设置。", L"Clear and reset.", L"Очистить и сбросить.", L"Leeren und zuruecksetzen.", L"Limpar e redefinir.", L"Wissen en resetten.", L"Wyczysc i zresetuj.", L"Temizle ve sifirla."));
	addTip(m_mode, LL14(L"解析の雰囲気モード。自動生成コマンドの傾向が変わります。", L"Analyze mood mode. Changes generated command style.", L"Mode d ambiance.", L"Modalita atmosfera.", L"Modo de ambiente.", L"분석 분위기 모드.", L"分析氛围模式。", L"Mood mode.", L"Режим настроения.", L"Stimmungsmodus.", L"Modo de humor.", L"Sfeermodus.", L"Tryb nastroju.", L"Atmosfer modu."));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 320, 8000);
}

BOOL CCommandRollDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	RestorePosFromSavedata();
	ModifyStyle(WS_HSCROLL | WS_VSCROLL, 0);
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	// キャプション既定アイコンを消す(ピアノロール/アナライザーと同じ)
	ModifyStyleEx(WS_EX_TOPMOST, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(L"コマンドロール", L"Command Roll", L"Rouleau", L"Command Roll", L"Command Roll", L"커맨드 롤", L"命令卷轴", L"Command Roll", L"Command Roll", L"Command Roll", L"Command Roll", L"Command Roll", L"Command Roll", L"Komut Rulosu"));
	SetDlgItemText(IDC_MCR_CLOSE, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	SetDlgItemText(IDC_MCR_HELP, L"?");
	SetDlgItemText(IDC_MCR_MODE_L, LL14(L"モード:", L"Mode:", L"Mode:", L"Modo:", L"Modo:", L"모드:", L"模式:", L"Mode:", L"Режим:", L"Modus:", L"Modo:", L"Modus:", L"Tryb:", L"Mod:"));
	SetDlgItemText(IDC_MCR_ANALYZE, LL14(L"解析", L"Analyze", L"Analyser", L"Analizza", L"Analizar", L"분석", L"分析", L"تحليل", L"Анализ", L"Analysieren", L"Analisar", L"Analyseren", L"Analizuj", L"Analiz"));
	SetDlgItemText(IDC_MCR_RUN, LL14(L"実行", L"Run", L"Executer", L"Esegui", L"Ejecutar", L"실행", L"执行", L"تشغيل", L"Запуск", L"Ausfuehren", L"Executar", L"Uitvoeren", L"Uruchom", L"Calistir"));
	SetDlgItemText(IDC_MCR_STOP, LL14(L"停止", L"Stop", L"Arret", L"Stop", L"Detener", L"중지", L"停止", L"إيقاف", L"Стоп", L"Stopp", L"Parar", L"Stoppen", L"Stop", L"Durdur"));
	SetDlgItemText(IDC_MCR_RESET, LL14(L"リセット", L"Reset", L"Reinit.", L"Ripristina", L"Restablecer", L"리셋", L"重置", L"إعادة ضبط", L"Сброс", L"Zuruecksetzen", L"Redefinir", L"Reset", L"Reset", L"Sifirla"));
	SetDlgItemText(IDC_MCR_CLEAR, LL14(L"クリア", L"Clear", L"Effacer", L"Cancella", L"Borrar", L"지우기", L"清除", L"مسح", L"Очистить", L"Leeren", L"Limpar", L"Wissen", L"Wyczysc", L"Temizle"));
	m_close.SetGradation(RGB(235, 230, 240), RGB(205, 195, 215), 0, TRUE);
	m_zoomIn.SetGradation(RGB(215, 235, 255), RGB(165, 205, 245), 0, TRUE);
	m_zoomOut.SetGradation(RGB(215, 235, 255), RGB(165, 205, 245), 0, TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	m_analyze.SetGradation(RGB(255, 230, 200), RGB(255, 180, 120), 0, TRUE);
	m_run.SetGradation(RGB(200, 240, 200), RGB(130, 205, 140), 0, TRUE);
	m_stop.SetGradation(RGB(255, 215, 220), RGB(255, 165, 180), 0, TRUE);
	m_reset.SetGradation(RGB(215, 235, 255), RGB(165, 205, 245), 0, TRUE);
	m_clear.SetGradation(RGB(255, 235, 205), RGB(255, 205, 150), 0, TRUE);

	if (CWnd* pPh = GetDlgItem(IDC_MCR_PROGRESS)) {
		CRect prc; pPh->GetWindowRect(&prc); ScreenToClient(&prc);
		pPh->DestroyWindow();
		m_progress.Create(WS_CHILD | WS_VISIBLE, prc, this, IDC_MCR_PROGRESS);
		m_progress.SetRange(0, 100);
		m_progress.SetPos(0);
		m_progress.SetShowPercent(TRUE);
		m_progress.SetColors(RGB(255, 236, 246), RGB(255, 170, 200), RGB(200, 120, 220));
		m_progress.SetAeroMode(CCC_IsAeroEnabled());
	}

	CRect rc; GetClientRect(&rc);
	CRect viewRc(8, 28, rc.right - 8, rc.bottom - 8);
	if (!m_view.CreateRoll(this, viewRc, IDC_MCR_HOST + 1))
		return FALSE;
	m_view.SetPeer(m_peer);
	m_view.SetSyncGen(&m_syncGen);
	{
		int z10 = savedata.mpCmdRollPxPerSec10;
		if (z10 < 15 || z10 > 4800) z10 = 120;
		m_view.SetPxPerSec((double)z10 / 10.0);
	}

	EnableMainWindowLock(&savedata.mpCmdRollMainLock);
	FillModeCombo();
	LayoutControls();
	SetupTooltips();

	const CString src = MpPromptSourceText();
	if (!src.IsEmpty())
		ReloadFromText(src, 0);
	else if (m_peer)
		ReloadFromText(m_peer->GetPromptText(), 0);

	SetTimer(kTimerId, 50, nullptr);
	m_view.SetFocus();
	return TRUE;
}

BOOL CCommandRollDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

BOOL CCommandRollDlg::OnEraseBkgnd(CDC* pDC)
{
	// 帯は ClearRect 用に残す（全面 Fill するとキャプションアクリルが潰れる）
	return CCustomBlurDialogExBase::OnEraseBkgnd(pDC);
}

void CCommandRollDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != kTimerId) return;
	const double t = MpGetPerformanceTimeSec();
	CString s;
	s.Format(LL14(L"再生ヘッド %.1f秒  (ホイール=横 / Shift+ホイール=縦 / Ctrl=ズーム)",
		L"Playhead %.1fs  (Wheel=H / Shift+Wheel=V / Ctrl=Zoom)",
		L"Tete %.1fs", L"Playhead %.1fs", L"Playhead %.1fs", L"재생헤드 %.1fs", L"播放头 %.1fs",
		L"Playhead %.1fs", L"Playhead %.1fs", L"Playhead %.1fs", L"Playhead %.1fs", L"Playhead %.1fs", L"Playhead %.1fs", L"Playhead %.1fs"), t);
	// 文字列が変わったときだけスタティック更新（ちらつき防止）
	if (s != m_lastTimeText) {
		m_lastTimeText = s;
		if (m_timeLbl.GetSafeHwnd())
			m_timeLbl.SetWindowText(s);
	}
	// 親ダイアログは Invalidate しない。ビューだけ更新。
	if (m_view.GetSafeHwnd())
		m_view.TickPlayhead();
}

void CCommandRollDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	if (lpMMI) {
		lpMMI->ptMinTrackSize.x = 560;
		lpMMI->ptMinTrackSize.y = 360;
	}
	CCustomBlurDialogExBase::OnGetMinMaxInfo(lpMMI);
}

void CCommandRollDlg::OnEnterSizeMove()
{
	m_inSizeMove = TRUE;
	Default();
}

void CCommandRollDlg::OnExitSizeMove()
{
	m_inSizeMove = FALSE;
	if (::IsWindow(m_hWnd) && !IsIconic()) {
		LayoutControls();
		if (m_posRestored)
			SavePosToSavedata();
	}
	Default();
}

void CCommandRollDlg::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogExBase::OnMoving(fwSide, pRect);
	if (m_posRestored)
		SavePosToSavedata();
}

void CCommandRollDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED)
		return;
	LayoutControls();
	if (m_posRestored)
		SavePosToSavedata();
}

void CCommandRollDlg::OnZoomIn() { if (m_view.GetSafeHwnd()) m_view.ZoomIn(); }
void CCommandRollDlg::OnZoomOut() { if (m_view.GetSafeHwnd()) m_view.ZoomOut(); }
void CCommandRollDlg::OnCloseBtn() { OnClose(); }

void CCommandRollDlg::PersistPromptText(const CString& text)
{
	MpRollPersistPromptText(text);
}

CString CCommandRollDlg::CommitAndGetText()
{
	CString text = m_view.BuildPromptText();
	PersistPromptText(text);
	++m_syncGen;
	CPromptDlg* peer = m_peer;
	if (!peer || !::IsWindow(peer->GetSafeHwnd()))
		peer = MpPromptDlgInstance();
	if (peer && ::IsWindow(peer->GetSafeHwnd()))
		peer->ApplyTextFromRoll(text, m_syncGen);
	MpPromptParse(text, nullptr);
	m_view.ReloadFromParse();
	return text;
}

void CCommandRollDlg::OnRun()
{
	if (m_analyzing) return;
	CString err;
	const CString text = CommitAndGetText();
	if (!MpPromptExecute(text, &err)) {
		CString msg = err;
		if (msg.IsEmpty()) {
			msg = LL14(L"プロンプトの解析に失敗しました。", L"Failed to parse prompt.", L"Echec analyse prompt.", L"Analisi prompt fallita.", L"Error al analizar prompt.", L"프롬프트 해석 실패.", L"提示解析失败。", L"Failed to parse prompt.", L"Ошибка разбора промпта.", L"Prompt parsen fehlgeschlagen.", L"Falha ao analisar prompt.", L"Prompt parseren mislukt.", L"Blad parsowania promptu.", L"Istem ayristirilamadi.");
		}
		AfxMessageBox(msg);
	}
}

void CCommandRollDlg::FillModeCombo()
{
	if (!m_mode.GetSafeHwnd()) return;
	m_mode.ResetContent();
	for (int i = 0; i < MP_ANA_MODE_COUNT; ++i)
		m_mode.AddString(MpPromptAnalyzeModeName(i));
	m_mode.SetCurSel(MpPromptAnalyzeModeClamp(savedata.mpPromptAnalyzeMode));
}

int CCommandRollDlg::GetSelectedAnalyzeMode() const
{
	if (!m_mode.GetSafeHwnd()) return MpPromptAnalyzeModeClamp(savedata.mpPromptAnalyzeMode);
	return MpPromptAnalyzeModeClamp(m_mode.GetCurSel());
}

void CCommandRollDlg::OnModeSel()
{
	savedata.mpPromptAnalyzeMode = GetSelectedAnalyzeMode();
	MpPersistSavedataQuick();
}

void CCommandRollDlg::SetAnalyzeUiBusy(BOOL busy)
{
	m_analyzing = busy;
	if (m_analyze.GetSafeHwnd()) m_analyze.EnableWindow(!busy);
	if (m_run.GetSafeHwnd()) m_run.EnableWindow(!busy);
	if (m_mode.GetSafeHwnd()) m_mode.EnableWindow(!busy);
	if (m_clear.GetSafeHwnd()) m_clear.EnableWindow(!busy);
	if (m_progress.GetSafeHwnd()) {
		m_progress.ShowWindow(SW_SHOW);
		if (busy)
			m_progress.SetPos(0);
	}
}

void CCommandRollDlg::AnalyzeProgressThunk(int percent, LPCTSTR status, void* user)
{
	CCommandRollDlg* self = reinterpret_cast<CCommandRollDlg*>(user);
	if (!self || !::IsWindow(self->GetSafeHwnd())) return;
	if (self->m_progress.GetSafeHwnd()) {
		self->m_progress.SetPos(percent);
		self->m_progress.Invalidate(FALSE);
		self->m_progress.UpdateWindow();
	}
	CString s;
	if (status && status[0])
		s.Format(L"%s  (%d%%)", status, percent);
	else
		s.Format(LL14(L"解析中… %d%%", L"Analyzing… %d%%", L"Analyse… %d%%", L"Analisi… %d%%", L"Analizando… %d%%", L"분석 중… %d%%", L"分析中… %d%%", L"Analyzing… %d%%", L"Анализ… %d%%", L"Analyse… %d%%", L"Analisando… %d%%", L"Analyseren… %d%%", L"Analiza… %d%%", L"Analiz… %d%%"), percent);
	if (self->m_timeLbl.GetSafeHwnd())
		self->m_timeLbl.SetWindowText(s);
	MSG msg;
	while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (!self->IsDialogMessage(&msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
}

void CCommandRollDlg::OnStop()
{
	MpPromptStop();
}

void CCommandRollDlg::OnReset()
{
	MpPromptReset();
}

void CCommandRollDlg::OnClear()
{
	if (m_analyzing) return;
	CString cur = MpPromptSourceText();
	cur.Trim();
	if (!cur.IsEmpty())
		MpPromptPushHistory(cur);
	PersistPromptText(L"");
	++m_syncGen;
	if (m_peer && ::IsWindow(m_peer->GetSafeHwnd()))
		m_peer->ApplyTextFromRoll(L"", m_syncGen);
	MpPromptClearAll();
	MpPromptParse(L"", nullptr);
	m_view.ClearAllEvents();
	m_view.ReloadFromParse();
}

void CCommandRollDlg::OnAnalyze()
{
	if (m_analyzing) return;

	CString cur = MpPromptSourceText();
	cur.Trim();
	const BOOL hasText = !cur.IsEmpty() || m_view.m_evCount > 0;
	CString ask = hasText
		? LL14(L"選択中の曲を読込しながら解析します。\r\n再生中の曲は一時停止されます。\r\n現在のコマンドは解析結果で上書きされます。よろしいですか？",
			L"Analyze the selected track while loading.\r\nPlayback will pause.\r\nCurrent commands will be replaced. Continue?",
			L"Analyser la piste. Lecture interrompue. Commandes remplacees. Continuer ?",
			L"Analizzare la traccia. Riproduzione interrotta. Comandi sostituiti. Continuare?",
			L"Analizar la pista. La reproduccion se pausara. Los comandos se reemplazaran. Continuar?",
			L"선택 곡을 분석합니다. 재생은 일시중단되고 명령이 덮어써집니다. 계속할까요?",
			L"将分析所选曲目。播放会暂停，命令将被覆盖。是否继续？",
			L"Analyze selected track. Playback pauses. Commands replaced. Continue?",
			L"Анализ трека. Воспроизведение прервётся. Команды будут заменены. Продолжить?",
			L"Titel analysieren. Wiedergabe pausiert. Befehle werden ersetzt. Fortfahren?",
			L"Analisar a faixa. Reproducao pausada. Comandos substituidos. Continuar?",
			L"Track analyseren. Afspelen pauzeert. Opdrachten worden vervangen. Doorgaan?",
			L"Analiza utworu. Odtwarzanie wstrzymane. Komendy zostana zastapione. Kontynuowac?",
			L"Parcayi analiz et. Calma duraklar. Komutlar degisir. Devam?")
		: LL14(L"選択中の曲を読込しながら解析します。\r\n再生中の曲は一時停止されます。よろしいですか？",
			L"Analyze the selected track while loading.\r\nCurrent playback will pause. Continue?",
			L"Analyser la piste. Lecture interrompue. Continuer ?",
			L"Analizzare la traccia. Riproduzione interrotta. Continuare?",
			L"Analizar la pista. La reproduccion se pausara. Continuar?",
			L"선택 곡을 분석합니다. 재생은 일시중단됩니다. 계속할까요?",
			L"将分析所选曲目。播放会暂停。是否继续？",
			L"Analyze selected track. Playback will pause. Continue?",
			L"Анализ трека. Воспроизведение будет прервано. Продолжить?",
			L"Titel analysieren. Wiedergabe wird pausiert. Fortfahren?",
			L"Analisar a faixa. A reproducao sera pausada. Continuar?",
			L"Track analyseren. Afspelen wordt gepauzeerd. Doorgaan?",
			L"Analiza utworu. Odtwarzanie zostanie wstrzymane. Kontynuowac?",
			L"Parcayi analiz et. Calma duraklar. Devam?");
	if (AfxMessageBox(ask, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;

	const int mode = GetSelectedAnalyzeMode();
	savedata.mpPromptAnalyzeMode = mode;
	MpPersistSavedataQuick();

	SetAnalyzeUiBusy(TRUE);
	if (m_progress.GetSafeHwnd()) {
		m_progress.SetPos(0);
		m_progress.ShowWindow(SW_SHOW);
	}
	MpPromptAnalyzeSetProgressCb(&CCommandRollDlg::AnalyzeProgressThunk, this);

	CString text, err;
	const BOOL ok = MpPromptAnalyzeSelected(text, mode, &err);

	MpPromptAnalyzeSetProgressCb(nullptr, nullptr);
	SetAnalyzeUiBusy(FALSE);
	if (m_progress.GetSafeHwnd())
		m_progress.SetPos(ok ? 100 : 0);

	if (!ok) {
		AfxMessageBox(err.IsEmpty()
			? LL14(L"解析に失敗しました。", L"Analysis failed.", L"Echec analyse.", L"Analisi fallita.", L"Error de analisis.", L"분석 실패.", L"分析失败。", L"فشل التحليل.", L"Ошибка анализа.", L"Analyse fehlgeschlagen.", L"Falha na analise.", L"Analyse mislukt.", L"Blad analizy.", L"Analiz basarisiz.")
			: err);
		return;
	}
	if (text.GetLength() > 14000)
		text = text.Left(14000);
	PersistPromptText(text);
	++m_syncGen;
	if (m_peer && ::IsWindow(m_peer->GetSafeHwnd()))
		m_peer->ApplyTextFromRoll(text, m_syncGen);
	MpPromptParse(text, nullptr);
	m_view.ReloadFromParse();
}
void CCommandRollDlg::OnHelpBtn() { ShowHelpSheet(); }

namespace {
class CCmdRollHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_MP_CMDROLL_HELP };
	explicit CCmdRollHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CCmdRollHelpDlg* g_helpDlg = nullptr;

BEGIN_MESSAGE_MAP(CCmdRollHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CCmdRollHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"プロンプトロール操作ガイド", L"Prompt Roll Guide", L"Guide du rouleau", L"Guida Prompt Roll",
		L"Guia Prompt Roll", L"프롬프트 롤 가이드", L"提示卷轴操作指南", L"Prompt Roll Guide",
		L"Руководство Prompt Roll", L"Prompt-Roll Anleitung", L"Guia Prompt Roll", L"Prompt-roll gids",
		L"Przewodnik Prompt Roll", L"Prompt Roll Kilavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CCmdRollHelpDlg::OnOK() { DestroyWindow(); }
void CCmdRollHelpDlg::OnCancel() { DestroyWindow(); }
void CCmdRollHelpDlg::OnClose() { DestroyWindow(); }

void CCmdRollHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_helpDlg == this)
		g_helpDlg = nullptr;
	delete this;
}

BOOL CCmdRollHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

static void HelpDrawArrow(CDC& dc, int x0, int y0, int x1, int y1, COLORREF col)
{
	CPen pen(PS_SOLID, 2, col);
	CPen* old = dc.SelectObject(&pen);
	dc.MoveTo(x0, y0);
	dc.LineTo(x1, y1);
	const int dx = x1 - x0, dy = y1 - y0;
	const double len = max(1.0, sqrt((double)dx * dx + (double)dy * dy));
	const double ux = dx / len, uy = dy / len;
	const int ax = (int)(x1 - ux * 7 + uy * 3.5);
	const int ay = (int)(y1 - uy * 7 - ux * 3.5);
	const int bx = (int)(x1 - ux * 7 - uy * 3.5);
	const int by = (int)(y1 - uy * 7 + ux * 3.5);
	dc.MoveTo(x1, y1); dc.LineTo(ax, ay);
	dc.MoveTo(x1, y1); dc.LineTo(bx, by);
	dc.SelectObject(old);
}

void CCmdRollHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	const int footerH = hp.footerH;
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(16, tm.tmHeight + tm.tmExternalLeading + 3);
	const int titleLh = lh + 2;
	CBrush frameBrush(RGB(130, 130, 150));

	auto title = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(55, 45, 85));
		dc.TextOut(x, y, t);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(65, 65, 80));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 115));
		dc.TextOut(x, y, t);
	};

	int y = 8;
	const int L = 12;
	title(L, y, LL14(L"プロンプトロール操作ガイド", L"Prompt Roll — Operation Guide", L"Guide d'utilisation", L"Guida operativa", L"Guia de operacion", L"프롬프트 롤 조작 가이드", L"提示卷轴操作指南", L"Operation guide", L"Руководство", L"Bedienungsanleitung", L"Guia de operacao", L"Bedieningshandleiding", L"Przewodnik", L"Islem kilavuzu"));
	y += titleLh;
	muted(L, y, LL14(L"時間軸で @/% コマンドを置き、プロンプト本文と双方向に同期します。", L"Place @/% commands on a timeline; two-way sync with prompt text.", L"Placer des @/% synchronises au prompt.", L"Posiziona @/% sincronizzati col prompt.", L"Coloca @/% sincronizados con el prompt.", L"시간축에 @/% 명령을 두고 프롬프트와 양방향 동기화합니다.", L"在时间轴放置 @/% 命令，并与提示文本双向同步。", L"Place @/%; two-way sync with prompt.", L"Размещение @/% и двусторонняя синхронизация.", L"@/% setzen; Zweiwege-Sync mit Prompt.", L"Coloque @/%; sync bidirecional.", L"Plaats @/%; tweeweg sync.", L"Umiesc @/%; dwukierunkowy sync.", L"@/% yerlestir; cift yonlu senkron."));
	y += lh + 6;

	// --- 上段: 左にミニロール図 / 右に操作一覧 ---
	const int gx = L;
	const int gy = y;
	const int gw = min(230, rc.Width() / 2 - 20);
	const int gh = lh * 5 + 20;
	dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
	dc.FillSolidRect(gx, gy, gw, lh, RGB(230, 232, 240));
	dc.FillSolidRect(gx, gy, 34, gh, RGB(220, 225, 235));
	CPen grid(PS_SOLID, 1, RGB(210, 214, 225));
	CPen* oldPen = dc.SelectObject(&grid);
	for (int i = 1; i < 5; ++i) {
		const int x = gx + 34 + i * ((gw - 34) / 5);
		dc.MoveTo(x, gy + lh); dc.LineTo(x, gy + gh);
	}
	const int laneH = (gh - lh) / 3;
	dc.SetTextColor(RGB(50, 50, 70));
	dc.TextOut(gx + 8, gy + lh + 4, L"N");
	dc.TextOut(gx + 8, gy + lh + laneH + 4, L"S");
	dc.TextOut(gx + 8, gy + lh + laneH * 2 + 4, L"a");
	dc.FillSolidRect(gx + 4, gy + lh + 2, 26, laneH - 6, RGB(200, 210, 230));
	dc.FillSolidRect(gx + 50, gy + lh + 6, 60, max(10, laneH - 10), RGB(90, 180, 120));
	dc.FillSolidRect(gx + 100, gy + lh + laneH + 6, 80, max(10, laneH - 10), RGB(90, 180, 120));
	dc.FillSolidRect(gx + 58, gy + lh + laneH * 2 + 8, 36, max(8, laneH / 2), RGB(70, 160, 100));
	dc.FillSolidRect(gx + 110, gy + lh + laneH * 2 + 4, 54, max(12, laneH - 8), RGB(70, 160, 100));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + 54, gy + lh + 6, L"N100");
	CPen head(PS_SOLID, 2, RGB(220, 40, 60));
	dc.SelectObject(&head);
	const int hx = gx + 34 + (gw - 34) / 2;
	dc.MoveTo(hx, gy); dc.LineTo(hx, gy + gh);
	dc.SetTextColor(RGB(200, 40, 60));
	dc.TextOut(hx + 3, gy + 1, L"▶");
	dc.SelectObject(oldPen);
	dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
	HelpDrawArrow(dc, gx + 16, gy + lh + laneH / 2, gx + 56, gy + lh + laneH / 2, RGB(80, 100, 180));

	const int rx = gx + gw + 16;
	int ry = gy;
	title(rx, ry, LL14(L"マウス / キー", L"Mouse / Keys", L"Souris / Touches", L"Mouse / Tasti", L"Raton / Teclas", L"마우스 / 키", L"鼠标 / 键", L"Mouse / Keys", L"Мышь / Клавиши", L"Maus / Tasten", L"Mouse / Teclas", L"Muis / Toetsen", L"Mysz / Klawisze", L"Fare / Tuslar"));
	ry += titleLh;
	body(rx, ry, LL14(L"ホイール …… 横スクロール", L"Wheel …… horizontal scroll", L"Molette …… horizontal", L"Rotella …… orizzontale", L"Rueda …… horizontal", L"휠 …… 가로 스크롤", L"滚轮 …… 横向滚动", L"Wheel …… H-scroll", L"Колесо …… гориз.", L"Rad …… horizontal", L"Roda …… horizontal", L"Wiel …… horizontaal", L"Kolo …… poziomo", L"Teker …… yatay")); ry += lh;
	body(rx, ry, LL14(L"Shift+ホイール …… 縦スクロール", L"Shift+Wheel …… vertical scroll", L"Shift+molette …… vertical", L"Shift+rotella …… verticale", L"Shift+rueda …… vertical", L"Shift+휠 …… 세로 스크롤", L"Shift+滚轮 …… 纵向滚动", L"Shift+Wheel …… V-scroll", L"Shift+колесо …… верт.", L"Shift+Rad …… vertikal", L"Shift+roda …… vertical", L"Shift+wiel …… verticaal", L"Shift+kolo …… pionowo", L"Shift+teker …… dikey")); ry += lh;
	body(rx, ry, LL14(L"Ctrl+ホイール …… ズーム", L"Ctrl+Wheel …… zoom", L"Ctrl+molette …… zoom", L"Ctrl+rotella …… zoom", L"Ctrl+rueda …… zoom", L"Ctrl+휠 …… 줌", L"Ctrl+滚轮 …… 缩放", L"Ctrl+Wheel …… zoom", L"Ctrl+колесо …… зум", L"Ctrl+Rad …… Zoom", L"Ctrl+roda …… zoom", L"Ctrl+wiel …… zoom", L"Ctrl+kolo …… zoom", L"Ctrl+teker …… zoom")); ry += lh;
	body(rx, ry, LL14(L"バードラッグ …… 時間方向へ移動", L"Drag bar …… move in time", L"Glisser barre …… deplacer", L"Trascina barra …… sposta", L"Arrastrar barra …… mover", L"바 드래그 …… 시간 이동", L"拖动条 …… 沿时间移动", L"Drag bar …… move", L"Тянуть полосу …… сдвиг", L"Balken ziehen …… verschieben", L"Arrastar barra …… mover", L"Sleep balk …… verplaatsen", L"Przeciagnij pasek …… przesun", L"Cubuk surukle …… tasi")); ry += lh;
	body(rx, ry, LL14(L"Home …… 再生ヘッド位置へ追随スクロール", L"Home …… scroll to follow playhead", L"Home …… suivre la tete", L"Home …… segui la testina", L"Home …… seguir el cabezal", L"Home …… 재생헤드로 추종 스크롤", L"Home …… 滚动跟随播放头", L"Home …… follow playhead", L"Home …… следовать за головой", L"Home …… Playhead folgen", L"Home …… seguir playhead", L"Home …… volg playhead", L"Home …… sledz glowe", L"Home …… playhead takip")); ry += lh;

	y = max(gy + gh, ry) + lh;

	// 図キャプション(重ならないよう図の下だけ)
	body(L, y, LL14(L"① 左のレーン名をドラッグして配置　　② 赤い縦線は再生ヘッド", L"1) Drag a lane label to place　　2) Red line = playhead", L"1) Glisser une piste　　2) Ligne rouge = tete", L"1) Trascina una corsia　　2) Linea rossa = testina", L"1) Arrastrar pista　　2) Linea roja = cabezal", L"1) 레인 이름 드래그로 배치　　2) 빨간 세로선 = 재생헤드", L"1) 拖动左侧轨道名放置　　2) 红色竖线 = 播放头", L"1) Drag lane to place　　2) Red line = playhead", L"1) Перетащите дорожку　　2) Красная линия = голова", L"1) Spur ziehen　　2) Rote Linie = Playhead", L"1) Arrastar faixa　　2) Linha vermelha = playhead", L"1) Sleep lane　　2) Rode lijn = playhead", L"1) Przeciagnij tor　　2) Czerwona linia = glowa", L"1) Serit surukle　　2) Kirmizi cizgi = playhead"));
	y += lh + 4;
	muted(L, y, LL14(L"同じレーンの細いバー/太いバーは値の大小を表します。", L"Thinner/thicker bars on a lane show smaller/larger values.", L"Barre fine/epaisse = petite/grande valeur.", L"Barra sottile/spessa = valore piccolo/grande.", L"Barra fina/gruesa = valor pequeno/grande.", L"같은 레인의 가는/굵은 바는 값의 대소를 나타냅니다.", L"同轨道上细/粗条表示数值大小。", L"Thin/thick bars show value size.", L"Тонкая/толстая полоса = малое/большое значение.", L"Duenne/dicke Balken = kleiner/groesserer Wert.", L"Barra fina/grossa = valor menor/maior.", L"Dunne/dikke balk = kleinere/grotere waarde.", L"Cienki/gruby pasek = mala/duza wartosc.", L"Ince/kalin cubuk = kucuk/buyuk deger."));
	y += lh + 8;

	// --- 編集フロー図解 ---
	title(L, y, LL14(L"編集の流れ", L"Edit flow", L"Flux d'edition", L"Flusso di modifica", L"Flujo de edicion", L"편집 흐름", L"编辑流程", L"Edit flow", L"Процесс правки", L"Bearbeitungsablauf", L"Fluxo de edicao", L"Bewerkingsstroom", L"Przebieg edycji", L"Duzenleme akisi"));
	y += titleLh;

	const int boxY = y;
	const int boxH = lh * 2 + 8;
	dc.FillSolidRect(L, boxY, 90, boxH, RGB(90, 180, 120));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(L + 8, boxY + 4, L"S 104→100");
	muted(L + 8, boxY + 4 + lh, LL14(L"バーを選択", L"Select bar", L"Selection", L"Seleziona", L"Seleccionar", L"바 선택", L"选择条", L"Select", L"Выбрать", L"Waehlen", L"Selecionar", L"Selecteren", L"Zaznacz", L"Sec"));
	HelpDrawArrow(dc, L + 98, boxY + boxH / 2, L + 120, boxY + boxH / 2, RGB(80, 100, 180));

	dc.FillSolidRect(L + 128, boxY, 120, boxH, RGB(255, 255, 255));
	dc.FrameRect(CRect(L + 128, boxY, L + 248, boxY + boxH), &frameBrush);
	dc.SetTextColor(RGB(60, 60, 80));
	dc.TextOut(L + 136, boxY + 4, LL14(L"配置ダイアログ", L"Place dialog", L"Dialogue", L"Finestra", L"Dialogo", L"배치 창", L"放置对话框", L"Dialog", L"Диалог", L"Dialog", L"Dialogo", L"Dialoog", L"Dialog", L"Pencere"));
	muted(L + 136, boxY + 4 + lh, L"t0 / t1 / v0 / v1");
	HelpDrawArrow(dc, L + 256, boxY + boxH / 2, L + 278, boxY + boxH / 2, RGB(80, 100, 180));

	dc.FillSolidRect(L + 286, boxY, 100, boxH, RGB(235, 245, 255));
	dc.FrameRect(CRect(L + 286, boxY, L + 386, boxY + boxH), &frameBrush);
	dc.SetTextColor(RGB(60, 60, 80));
	dc.TextOut(L + 294, boxY + 4, LL14(L"プロンプト本文", L"Prompt text", L"Texte prompt", L"Testo prompt", L"Texto prompt", L"프롬프트 본문", L"提示正文", L"Prompt text", L"Текст промпта", L"Prompt-Text", L"Texto prompt", L"Prompttekst", L"Tekst promptu", L"Istem metni"));
	muted(L + 294, boxY + 4 + lh, L"@S80-100[104-100]");

	y = boxY + boxH + lh;
	body(L, y, LL14(L"ダブルクリックで編集　　Delete で選択バー削除　　変更はプロンプト本文へ即反映", L"Double-click to edit　　Delete removes selected bar　　Changes sync to prompt text", L"Double-clic = editer　　Suppr = supprimer　　Sync immediate", L"Doppio clic = modifica　　Canc = elimina　　Sync immediata", L"Doble clic = editar　　Supr = borrar　　Sync inmediata", L"더블클릭=편집　　Delete=선택 바 삭제　　변경은 프롬프트에 즉시 반영", L"双击编辑　　Delete删除所选条　　变更立即反映到提示正文", L"Double-click=edit　　Delete=remove　　Syncs immediately", L"Двойной щелчок=правка　　Delete=удалить　　Сразу в промпт", L"Doppelklick=bearbeiten　　Entf=loeschen　　Sofort im Prompt", L"Duplo clique=editar　　Delete=remover　　Sync imediato", L"Dubbelklik=bewerken　　Delete=verwijderen　　Direct sync", L"Dwuklik=edytuj　　Delete=usun　　Natychmiastowy sync", L"Cift tik=duzenle　　Delete=sil　　Hemen senkron"));
	y += lh + 8;

	// --- 詳細: レーン意味 ---
	title(L, y, LL14(L"レーンの意味", L"What each lane means", L"Signification des pistes", L"Significato delle corsie", L"Significado de las pistas", L"레인의 의미", L"各轨道含义", L"Lane meanings", L"Смысл дорожек", L"Bedeutung der Spuren", L"Significado das faixas", L"Betekenis van lanes", L"Znaczenie torow", L"Seritlerin anlami"));
	y += titleLh;
	body(L, y, LL14(L"Preset …… sb/br/sl など雰囲気プリセット", L"Preset …… mood presets (sb/br/sl etc.)", L"Preset …… ambiances", L"Preset …… atmosfere", L"Preset …… ambientes", L"Preset …… sb/br/sl 등 분위기 프리셋", L"Preset …… sb/br/sl 等氛围预设", L"Preset …… mood presets", L"Preset …… пресеты настроения", L"Preset …… Stimmungs-Presets", L"Preset …… predefinicoes", L"Preset …… sfeerpresets", L"Preset …… nastroje", L"Preset …… atmosfer presetleri")); y += lh;
	body(L, y, LL14(L"N S I K M …… EQ項目(鮮明/立体/密度/高低/マスター)", L"N S I K M …… EQ items (clarity/space/density/balance/master)", L"N S I K M …… parametres EQ", L"N S I K M …… parametri EQ", L"N S I K M …… parametros EQ", L"N S I K M …… EQ 항목", L"N S I K M …… EQ 项", L"N S I K M …… EQ items", L"N S I K M …… параметры EQ", L"N S I K M …… EQ-Parameter", L"N S I K M …… itens EQ", L"N S I K M …… EQ-items", L"N S I K M …… parametry EQ", L"N S I K M …… EQ ogeleri")); y += lh;
	body(L, y, LL14(L"p t d …… ピッチ / テンポ / DirectSound音量", L"p t d …… pitch / tempo / DirectSound volume", L"p t d …… hauteur / tempo / volume DS", L"p t d …… pitch / tempo / volume DS", L"p t d …… tono / tempo / volumen DS", L"p t d …… 피치 / 템포 / DS 음량", L"p t d …… 音高 / 速度 / DS 音量", L"p t d …… pitch / tempo / DS volume", L"p t d …… высота / темп / громкость DS", L"p t d …… Tonhoehe / Tempo / DS-Lautstaerke", L"p t d …… tom / tempo / volume DS", L"p t d …… toonhoogte / tempo / DS-volume", L"p t d …… wysokosc / tempo / glosnosc DS", L"p t d …… perde / tempo / DS ses")); y += lh;
	body(L, y, LL14(L"r c y …… リバーブ / コーラス / ディレイ　　E F …… 環境番号 / かかり具合", L"r c y …… reverb / chorus / delay　　E F …… env id / amount", L"r c y …… reverb / chorus / delay　　E F …… env", L"r c y …… reverb / chorus / delay　　E F …… env", L"r c y …… reverb / chorus / delay　　E F …… env", L"r c y …… 리버브 / 코러스 / 딜레이　　E F …… 환경 / 강도", L"r c y …… 混响 / 合唱 / 延迟　　E F …… 环境 / 强度", L"r c y …… reverb / chorus / delay　　E F …… env", L"r c y …… реверб / хорус / дилей　　E F …… окружение", L"r c y …… Reverb / Chorus / Delay　　E F …… Umgebung", L"r c y …… reverb / chorus / delay　　E F …… ambiente", L"r c y …… reverb / chorus / delay　　E F …… omgeving", L"r c y …… reverb / chorus / delay　　E F …… srodowisko", L"r c y …… reverb / chorus / delay　　E F …… ortam")); y += lh;
	body(L, y, LL14(L"a b e…o …… EQ周波数帯(25Hz〜16kHz)。※c/d はコーラス/DSのためEQ帯では未使用", L"a b e…o …… EQ bands (25Hz–16kHz). *c/d unused here (chorus/DS)", L"a b e…o …… bandes EQ. *c/d non utilises", L"a b e…o …… bande EQ. *c/d non usati", L"a b e…o …… bandas EQ. *c/d no usados", L"a b e…o …… EQ 주파수 대역. *c/d 미사용", L"a b e…o …… EQ 频段。*c/d 此处未用", L"a b e…o …… EQ bands. *c/d unused here", L"a b e…o …… полосы EQ. *c/d не используются", L"a b e…o …… EQ-Baender. *c/d unbenutzt", L"a b e…o …… faixas EQ. *c/d nao usados", L"a b e…o …… EQ-banden. *c/d ongebruikt", L"a b e…o …… pasma EQ. *c/d nieuzywane", L"a b e…o …… EQ bantlari. *c/d kullanilmaz")); y += lh + 6;

	// --- 詳細Tips ---
	title(L, y, LL14(L"補足", L"Notes", L"Notes", L"Note", L"Notas", L"보완", L"补充", L"Notes", L"Заметки", L"Hinweise", L"Notas", L"Opmerkingen", L"Uwagi", L"Notlar"));
	y += titleLh;
	muted(L, y, LL14(L"・未演奏でも横に広くスクロールできます(最低約30分相当)。", L"· You can scroll far even before playback (about 30 min minimum).", L"· Defilement large meme sans lecture (~30 min).", L"· Scorri ampiamente anche senza riproduzione (~30 min).", L"· Puede desplazarse lejos sin reproducir (~30 min).", L"· 미연주여도 가로로 넓게 스크롤 가능(최소 약 30분).", L"· 未播放也可横向大幅滚动(至少约30分钟)。", L"· Scroll far even when idle (~30 min min).", L"· Можно листать далеко без воспроизведения (~30 мин).", L"· Auch ohne Wiedergabe weit scrollen (~30 Min).", L"· Role longe mesmo sem reproducao (~30 min).", L"· Ver scrollen ook zonder afspelen (~30 min).", L"· Mozna przewijac daleko bez odtwarzania (~30 min).", L"· Calmadan da genis kaydirabilirsiniz (~30 dk).")); y += lh;
	muted(L, y, LL14(L"・「メインに追従」をオンにすると、メイン窓の移動に合わせて位置が動きます。", L"· Enable Follow-main to keep this window aligned when the main window moves.", L"· Suivre la fenetre principale pour aligner la position.", L"· Segui finestra principale per allineare la posizione.", L"· Seguir ventana principal para alinear la posicion.", L"· 「메인 추종」을 켜면 메인 창 이동에 맞춰 위치가 움직입니다.", L"· 开启「跟随主窗口」后，会随主窗口移动。", L"· Follow-main keeps position aligned with the main window.", L"· «Следовать за главным» выравнивает позицию.", L"· «Hauptfenster folgen» richtet die Position aus.", L"· Seguir principal alinha a posicao.", L"· Volg-hoofdvenster houdt positie uitgelijnd.", L"· Podazaj za glownym wyrownuje pozycje.", L"· Ana pencereyi takip acikken konum hizalanir.")); y += lh;
	muted(L, y, LL14(L"・ロールとプロンプトは同じコマンド列を共有します。片方を直せばもう片方も更新されます。", L"· Roll and prompt share the same command list; editing either updates the other.", L"· Rouleau et prompt partagent les commandes.", L"· Roll e prompt condividono i comandi.", L"· Roll y prompt comparten los comandos.", L"· 롤과 프롬프트는 같은 명령열을 공유합니다.", L"· 卷轴与提示共享同一命令列。", L"· Roll and prompt share commands.", L"· Ролл и промпт делят одни команды.", L"· Roll und Prompt teilen dieselben Befehle.", L"· Roll e prompt compartilham os comandos.", L"· Roll en prompt delen dezelfde opdrachten.", L"· Roll i prompt wspoldziela komendy.", L"· Rulo ve istem ayni komutlari paylasir."));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

void CCommandRollDlg::ShowHelpSheet()
{
	if (g_helpDlg && ::IsWindow(g_helpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_helpDlg, this);
		return;
	}
	if (g_helpDlg && !::IsWindow(g_helpDlg->GetSafeHwnd()))
		g_helpDlg = nullptr;
	// オーナー付きモードレス。ヘルプはオーナー上、他UI前面時は下へ（TOPMOSTしない）
	CCmdRollHelpDlg* dlg = new CCmdRollHelpDlg(this);
	if (!dlg->Create(IDD_MP_CMDROLL_HELP, this)) {
		delete dlg;
		return;
	}
	g_helpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CCommandRollDlg::OnClose()
{
	KillTimer(kTimerId);
	SavePosToSavedata();
	savedata.mpCmdRollwindow = 0;
	DestroyWindow();
}

void CCommandRollDlg::PostNcDestroy()
{
	CCustomBlurDialogExBase::PostNcDestroy();
	if (g_rollDlg == this) g_rollDlg = nullptr;
	delete this;
}

HBRUSH CCommandRollDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (nCtlColor == CTLCOLOR_DLG || nCtlColor == CTLCOLOR_STATIC) {
		if (!m_brDlg.GetSafeHandle())
			m_brDlg.CreateSolidBrush(RGB(240, 240, 245));
		pDC->SetBkColor(RGB(240, 240, 245));
		pDC->SetTextColor(RGB(55, 55, 70));
		pDC->SetBkMode(OPAQUE);
		return m_brDlg;
	}
	return CCustomBlurDialogExBase::OnCtlColor(pDC, pWnd, nCtlColor);
}

void MpShowCommandRollDialog(CWnd* pParent, BOOL bActivate)
{
	UNREFERENCED_PARAMETER(pParent);
	if (g_rollDlg && ::IsWindow(g_rollDlg->GetSafeHwnd())) {
		g_rollDlg->ShowWindow(bActivate ? SW_SHOW : SW_SHOWNOACTIVATE);
		if (bActivate)
			g_rollDlg->SetForegroundWindow();
		MpMakeIndependentZOrder(g_rollDlg);
		savedata.mpCmdRollwindow = 1;
		const CString src = MpPromptSourceText();
		if (!src.IsEmpty())
			g_rollDlg->ReloadFromText(src, 0);
		return;
	}
	if (g_rollDlg && !::IsWindow(g_rollDlg->GetSafeHwnd())) g_rollDlg = nullptr;
	CCommandRollDlg* dlg = new CCommandRollDlg(nullptr);
	dlg->SetPromptPeer(MpPromptDlgInstance());
	if (!dlg->Create(IDD_MP_CMDROLL, nullptr)) { delete dlg; return; }
	MpMakeIndependentZOrder(dlg);
	dlg->ShowWindow(bActivate ? SW_SHOW : SW_SHOWNOACTIVATE);
	if (bActivate)
		dlg->SetForegroundWindow();
	g_rollDlg = dlg;
	savedata.mpCmdRollwindow = 1;
	const CString src = MpPromptSourceText();
	if (!src.IsEmpty())
		dlg->ReloadFromText(src, 0);
}

void MpToggleCommandRollDialog(CWnd* pParent)
{
	if (g_rollDlg && ::IsWindow(g_rollDlg->GetSafeHwnd())) {
		g_rollDlg->SendMessage(WM_CLOSE);
		return;
	}
	MpShowCommandRollDialog(pParent, TRUE);
}

BOOL MpIsCommandRollOpen()
{
	return (g_rollDlg && ::IsWindow(g_rollDlg->GetSafeHwnd())) ? TRUE : FALSE;
}

void MpCommandRollNotifyText(const CString& text, UINT syncGen)
{
	if (g_rollDlg && ::IsWindow(g_rollDlg->GetSafeHwnd()))
		g_rollDlg->ReloadFromText(text, syncGen);
}
