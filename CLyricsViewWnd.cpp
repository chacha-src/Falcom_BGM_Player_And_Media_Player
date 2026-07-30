#include "StdAfx.h"
#include "CLyricsViewWnd.h"

namespace {
	const UINT_PTR kAnimTimer = 61;
	const UINT kAnimMs = 16;
	inline double AbsD(double x) { return (x < 0.0) ? -x : x; }
}

BEGIN_MESSAGE_MAP(CLyricsViewWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

CLyricsViewWnd::CLyricsViewWnd()
	: m_count(0)
	, m_cur(0)
	, m_lineH(18)
	, m_scrollY(0.0)
	, m_targetY(0.0)
	, m_fontPt(0)
	, m_timer(0)
{
}

CLyricsViewWnd::~CLyricsViewWnd()
{
	StopAnim();
	if (m_hWnd)
		DestroyWindow();
}

BOOL CLyricsViewWnd::Create(CWnd* pParent, UINT nID)
{
	CString cls = AfxRegisterWndClass(
		CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
		::LoadCursor(NULL, IDC_ARROW),
		NULL,
		NULL);
	CRect rc(0, 0, 10, 10);
	return CWnd::Create(cls, _T(""), WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, rc, pParent, nID);
}

void CLyricsViewWnd::Clear()
{
	m_count = 0;
	m_cur = 0;
	m_scrollY = 0.0;
	m_targetY = 0.0;
	StopAnim();
	if (m_hWnd)
		Invalidate(FALSE);
}

void CLyricsViewWnd::EnsureFonts(int dpiPointTenths, LPCTSTR face)
{
	if (dpiPointTenths <= 0) dpiPointTenths = 90;
	if (!face || !face[0]) face = _T("Segoe UI");
	if (m_fontPt == dpiPointTenths && m_fontFace == face && m_font.GetSafeHandle() && m_fontHi.GetSafeHandle())
		return;
	m_fontPt = dpiPointTenths;
	m_fontFace = face;
	if (m_font.GetSafeHandle()) m_font.DeleteObject();
	if (m_fontHi.GetSafeHandle()) m_fontHi.DeleteObject();
	m_font.CreatePointFont(dpiPointTenths, face);
	LOGFONT lf = {};
	m_font.GetLogFont(&lf);
	lf.lfWeight = FW_BOLD;
	lf.lfHeight = (LONG)(lf.lfHeight * 1.12);
	if (lf.lfHeight == 0) lf.lfHeight = -14;
	m_fontHi.CreateFontIndirect(&lf);

	CClientDC dc(this);
	CFont* old = dc.SelectObject(&m_fontHi);
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	m_lineH = tm.tmHeight + tm.tmExternalLeading + 4;
	if (m_lineH < 16) m_lineH = 16;
	dc.SelectObject(old);
	RecalcTarget();
	m_scrollY = m_targetY;
}

void CLyricsViewWnd::SetLines(const CString* lines, int count)
{
	if (count < 0) count = 0;
	if (count > kMaxLines) count = kMaxLines;
	BOOL changed = (count != m_count);
	if (!changed) {
		for (int i = 0; i < count; i++) {
			if (m_line[i] != lines[i]) { changed = TRUE; break; }
		}
	}
	if (!changed) return;
	m_count = count;
	for (int i = 0; i < count; i++)
		m_line[i] = lines[i];
	for (int i = count; i < kMaxLines; i++)
		m_line[i].Empty();
	if (m_cur >= m_count) m_cur = m_count > 0 ? m_count - 1 : 0;
	RecalcTarget();
	m_scrollY = m_targetY;
	if (m_hWnd)
		Invalidate(FALSE);
}

void CLyricsViewWnd::SetCurrent(int idx)
{
	if (m_count <= 0) {
		m_cur = 0;
		return;
	}
	if (idx < 0) idx = 0;
	if (idx >= m_count) idx = m_count - 1;
	if (idx == m_cur) {
		RecalcTarget();
		if (AbsD(m_scrollY - m_targetY) > 0.5)
			StartAnim();
		return;
	}
	m_cur = idx;
	RecalcTarget();
	StartAnim();
	if (m_hWnd)
		Invalidate(FALSE);
}

void CLyricsViewWnd::RecalcTarget()
{
	CRect rc;
	if (!m_hWnd) { m_targetY = 0.0; return; }
	GetClientRect(&rc);
	const int viewH = rc.Height();
	if (viewH <= 0 || m_lineH <= 0) {
		m_targetY = 0.0;
		return;
	}
	// 現在行の上端がビュー中央付近に来るようオフセット
	const double curTop = (double)m_cur * (double)m_lineH;
	m_targetY = curTop - ((double)viewH - (double)m_lineH) * 0.5;
	if (m_targetY < 0.0) m_targetY = 0.0;
	const double maxY = (double)m_count * (double)m_lineH - (double)viewH;
	if (maxY > 0.0 && m_targetY > maxY)
		m_targetY = maxY;
	if (maxY <= 0.0)
		m_targetY = 0.0;
}

void CLyricsViewWnd::StartAnim()
{
	if (!m_hWnd) return;
	if (!m_timer)
		m_timer = SetTimer(kAnimTimer, kAnimMs, NULL);
}

void CLyricsViewWnd::StopAnim()
{
	if (m_timer && m_hWnd) {
		KillTimer(m_timer);
		m_timer = 0;
	}
}

void CLyricsViewWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != kAnimTimer) {
		CWnd::OnTimer(nIDEvent);
		return;
	}
	const double d = m_targetY - m_scrollY;
	if (AbsD(d) < 0.4) {
		m_scrollY = m_targetY;
		StopAnim();
		Invalidate(FALSE);
		return;
	}
	// イージング: 残り距離の約20%/tick → 16ms想定で滑らか追従
	m_scrollY += d * 0.20;
	Invalidate(FALSE);
}

void CLyricsViewWnd::OnSize(UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);
	RecalcTarget();
	if (AbsD(m_scrollY - m_targetY) > 1.0)
		StartAnim();
	else
		m_scrollY = m_targetY;
}

BOOL CLyricsViewWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	UNREFERENCED_PARAMETER(nFlags);
	UNREFERENCED_PARAMETER(pt);
	if (m_lineH <= 0) return TRUE;
	m_targetY -= (double)zDelta / 120.0 * (double)m_lineH;
	CRect rc; GetClientRect(&rc);
	const double maxY = (double)m_count * (double)m_lineH - (double)rc.Height();
	if (m_targetY < 0.0) m_targetY = 0.0;
	if (maxY > 0.0 && m_targetY > maxY) m_targetY = maxY;
	if (maxY <= 0.0) m_targetY = 0.0;
	StartAnim();
	return TRUE;
}

BOOL CLyricsViewWnd::OnEraseBkgnd(CDC* pDC)
{
	UNREFERENCED_PARAMETER(pDC);
	return TRUE;
}

void CLyricsViewWnd::OnPaint()
{
	CPaintDC pdc(this);
	CRect rc;
	GetClientRect(&rc);
	if (rc.Width() <= 0 || rc.Height() <= 0) return;

	CDC mem;
	mem.CreateCompatibleDC(&pdc);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&pdc, rc.Width(), rc.Height());
	CBitmap* oldBmp = mem.SelectObject(&bmp);

	mem.FillSolidRect(&rc, RGB(248, 250, 255));

	// 上下フェード帯
	for (int i = 0; i < 12 && i < rc.Height() / 4; i++) {
		const int a = 40 - i * 3;
		if (a <= 0) break;
		mem.FillSolidRect(0, i, rc.Width(), 1, RGB(235, 240, 250));
		mem.FillSolidRect(0, rc.bottom - 1 - i, rc.Width(), 1, RGB(235, 240, 250));
	}

	if (m_count > 0 && m_lineH > 0) {
		const int first = (int)(m_scrollY / m_lineH);
		const int last = first + rc.Height() / m_lineH + 2;
		const int padX = 6;

		// 現在行ハイライト帯
		{
			const int cy = (int)((double)m_cur * m_lineH - m_scrollY);
			CRect hi(0, cy - 1, rc.Width(), cy + m_lineH + 1);
			if (hi.bottom > 0 && hi.top < rc.Height()) {
				mem.FillSolidRect(&hi, RGB(220, 232, 255));
				CPen pen(PS_SOLID, 1, RGB(160, 190, 235));
				CPen* op = mem.SelectObject(&pen);
				mem.MoveTo(0, hi.top);
				mem.LineTo(rc.Width(), hi.top);
				mem.MoveTo(0, hi.bottom - 1);
				mem.LineTo(rc.Width(), hi.bottom - 1);
				mem.SelectObject(op);
			}
		}

		mem.SetBkMode(TRANSPARENT);
		for (int i = first; i <= last; i++) {
			if (i < 0 || i >= m_count) continue;
			const int y = (int)((double)i * m_lineH - m_scrollY);
			if (y + m_lineH < 0 || y > rc.Height()) continue;

			const BOOL isCur = (i == m_cur);
			const int dist = abs(i - m_cur);
			COLORREF col;
			if (isCur) col = RGB(30, 70, 170);
			else if (dist == 1) col = RGB(70, 90, 130);
			else if (dist == 2) col = RGB(110, 120, 145);
			else col = RGB(150, 155, 170);

			CFont* use = isCur ? &m_fontHi : &m_font;
			CFont* old = mem.SelectObject(use);
			mem.SetTextColor(col);
			CRect tr(padX, y, rc.Width() - padX, y + m_lineH);
			mem.DrawText(m_line[i], &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
			mem.SelectObject(old);
		}
	}
	else {
		mem.SetBkMode(TRANSPARENT);
		mem.SetTextColor(RGB(140, 150, 170));
		if (m_font.GetSafeHandle())
			mem.SelectObject(&m_font);
		CString empty = LL14(
			L"（歌詞なし）", L"(No lyrics)", L"(Pas de paroles)", L"(Nessun testo)", L"(Sin letra)",
			L"(가사 없음)", L"（无歌词）", L"(لا كلمات)", L"(Нет текста)", L"(Kein Text)",
			L"(Sem letra)", L"(Geen tekst)", L"(Brak tekstu)", L"(Söz yok)");
		mem.DrawText(empty, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

	pdc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	mem.SelectObject(oldBmp);
}
