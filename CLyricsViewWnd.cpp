#include "StdAfx.h"
#include "CLyricsViewWnd.h"
#include "CCustomControl.h"
#include <math.h>

IMPLEMENT_DYNAMIC(CLyricsViewWnd, CWnd)

namespace {
	const UINT_PTR kAnimTimer = 61;
	const UINT kAnimMs = 8; // ~120Hz サンプリング（実dtで積分）
	inline double AbsD(double x) { return (x < 0.0) ? -x : x; }
	// 描画Yの量子化を安定させ、スクロール終端の1px震えを抑える
	inline int ScrollToPix(double v)
	{
		return (int)floor(v + 1e-6);
	}
}

BEGIN_MESSAGE_MAP(CLyricsViewWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_MOUSEWHEEL()
	ON_WM_RBUTTONUP()
END_MESSAGE_MAP()

CLyricsViewWnd::CLyricsViewWnd()
	: m_count(0)
	, m_tmCount(0)
	, m_cur(0)
	, m_frac(0.0)
	, m_lineH(18)
	, m_scrollY(0.0)
	, m_targetY(0.0)
	, m_scrollVel(0.0)
	, m_lastAnimQpc(0)
	, m_qpcFreq(0)
	, m_fontPt(0)
	, m_timer(0)
	, m_overlay(FALSE)
{
	ZeroMemory(m_tm, sizeof(m_tm));
	LARGE_INTEGER f = {};
	if (::QueryPerformanceFrequency(&f) && f.QuadPart > 0)
		m_qpcFreq = (ULONGLONG)f.QuadPart;
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
	m_tmCount = 0;
	m_cur = 0;
	m_frac = 0.0;
	m_scrollY = 0.0;
	m_targetY = 0.0;
	m_scrollVel = 0.0;
	ZeroMemory(m_tm, sizeof(m_tm));
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
	m_lineH = tm.tmHeight + tm.tmExternalLeading + (m_overlay ? 8 : 4);
	if (m_lineH < 16) m_lineH = 16;
	dc.SelectObject(old);
	RecalcTarget();
	m_scrollY = m_targetY;
}

void CLyricsViewWnd::SetOverlayStyle(BOOL on)
{
	if (m_overlay == on) return;
	m_overlay = on;
	const int pt = m_fontPt > 0 ? m_fontPt : (m_overlay ? 140 : 100);
	m_fontPt = 0; // force recreate
	EnsureFonts(m_overlay ? max(pt, 130) : pt, m_fontFace.IsEmpty() ? _T("Segoe UI") : (LPCTSTR)m_fontFace);
	Invalidate(FALSE);
}

void CLyricsViewWnd::SetLines(const CString* lines, int count, const DWORD* times, int timeCount)
{
	if (count < 0) count = 0;
	if (count > kMaxLines) count = kMaxLines;
	if (timeCount < 0) timeCount = 0;
	// 番兵時刻(次行開始)を含めるため count+1 まで許可
	if (timeCount > kMaxLines) timeCount = kMaxLines;
	BOOL changed = (count != m_count) || (timeCount != m_tmCount);
	if (!changed && lines) {
		for (int i = 0; i < count; i++) {
			if (m_line[i] != lines[i]) { changed = TRUE; break; }
		}
	}
	if (!changed && times) {
		for (int i = 0; i < timeCount; i++) {
			if (m_tm[i] != times[i]) { changed = TRUE; break; }
		}
	}
	if (!changed) return;
	m_count = count;
	m_tmCount = times ? timeCount : 0;
	for (int i = 0; i < count; i++)
		m_line[i] = lines ? lines[i] : CString();
	for (int i = count; i < kMaxLines; i++)
		m_line[i].Empty();
	if (times) {
		for (int i = 0; i < m_tmCount; i++)
			m_tm[i] = times[i];
	}
	for (int i = m_tmCount; i < kMaxLines; i++)
		m_tm[i] = 0;
	if (m_cur >= m_count) m_cur = m_count > 0 ? m_count - 1 : 0;
	m_scrollVel = 0.0;
	RecalcTarget();
	// 歌詞入れ替え時のみ瞬間合わせ（追従アニメの途中ジャンプを防ぐ）
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
		if (AbsD(m_scrollY - m_targetY) > 0.35)
			StartAnim();
		return;
	}
	m_cur = idx;
	RecalcTarget();
	StartAnim();
	if (m_hWnd)
		Invalidate(FALSE);
}

void CLyricsViewWnd::SetPlayCentis(DWORD centis)
{
	if (m_count <= 0 || m_tmCount < 2) {
		SetCurrent(0);
		m_frac = 0.0;
		return;
	}
	int idx = 0;
	for (int i = 0; i < m_tmCount - 1; i++) {
		if (m_tm[i] <= centis && m_tm[i + 1] > centis) {
			idx = i;
			break;
		}
		if (centis >= m_tm[i])
			idx = i;
	}
	if (idx < 0) idx = 0;
	if (idx >= m_count) idx = m_count - 1;
	double frac = 0.0;
	const DWORD t0 = m_tm[idx];
	// 次行開始までを分母にする(文字数キャップは塗りが音より早く終わる原因になるので使わない)
	DWORD t1 = (idx + 1 < m_tmCount) ? m_tm[idx + 1] : (t0 + 500);
	if (t1 <= t0)
		t1 = t0 + 1;
	frac = (double)(centis - t0) / (double)(t1 - t0);
	if (frac < 0.0) frac = 0.0;
	if (frac > 1.0) frac = 1.0;
	const BOOL curChanged = (idx != m_cur);
	const BOOL fracChanged = (AbsD(frac - m_frac) > 0.0015);
	m_frac = frac;
	if (curChanged) {
		SetCurrent(idx);
	} else {
		// 行内進捗でも目標を少しずつ進め、行切替の段差を消す
		RecalcTarget();
		if (AbsD(m_scrollY - m_targetY) > 0.35)
			StartAnim();
		if (fracChanged && m_hWnd)
			Invalidate(FALSE);
	}
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
	// 現在行 + 行内進捗で連続的に次行へ寄せる（切替瞬間のジャンプを緩和）
	const double softFrac = m_frac * m_frac * (3.0 - 2.0 * m_frac); // smoothstep
	const double curTop = (double)m_cur * (double)m_lineH + softFrac * (double)m_lineH;
	// 中央やや上（カラオケ視線）
	m_targetY = curTop - ((double)viewH - (double)m_lineH) * 0.42;
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
	LARGE_INTEGER now = {};
	if (m_qpcFreq && ::QueryPerformanceCounter(&now))
		m_lastAnimQpc = (ULONGLONG)now.QuadPart;
	else
		m_lastAnimQpc = ::GetTickCount64();
}

void CLyricsViewWnd::StopAnim()
{
	if (m_timer && m_hWnd) {
		KillTimer(m_timer);
		m_timer = 0;
	}
	m_scrollVel = 0.0;
}

void CLyricsViewWnd::StepScroll(double dtSec)
{
	if (dtSec < 0.0) dtSec = 0.0;
	if (dtSec > 0.05) dtSec = 0.05; // スパイク吸収
	const double d = m_targetY - m_scrollY;
	if (AbsD(d) < 0.25 && AbsD(m_scrollVel) < 8.0) {
		m_scrollY = m_targetY;
		m_scrollVel = 0.0;
		StopAnim();
		Invalidate(FALSE);
		return;
	}
	// 臨界減衰っぽい追従: 加速度 = ω^2 * d - 2ζω * v
	// ω≈10, ζ≈1.05 → 素早く・行き過ぎ少なめ
	const double omega = 11.0;
	const double zeta = 1.05;
	const double acc = (omega * omega) * d - (2.0 * zeta * omega) * m_scrollVel;
	m_scrollVel += acc * dtSec;
	// 速度上限（行高の約14倍/秒）で大ジャンプ時の飛び過ぎを抑える
	const double vmax = (m_lineH > 0) ? ((double)m_lineH * 14.0) : 400.0;
	if (m_scrollVel > vmax) m_scrollVel = vmax;
	if (m_scrollVel < -vmax) m_scrollVel = -vmax;
	m_scrollY += m_scrollVel * dtSec;
	Invalidate(FALSE);
}

void CLyricsViewWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != kAnimTimer) {
		CWnd::OnTimer(nIDEvent);
		return;
	}
	double dt = 0.008;
	if (m_qpcFreq) {
		LARGE_INTEGER now = {};
		if (::QueryPerformanceCounter(&now)) {
			const ULONGLONG q = (ULONGLONG)now.QuadPart;
			if (m_lastAnimQpc > 0 && q > m_lastAnimQpc)
				dt = (double)(q - m_lastAnimQpc) / (double)m_qpcFreq;
			m_lastAnimQpc = q;
		}
	} else {
		const ULONGLONG t = ::GetTickCount64();
		if (m_lastAnimQpc > 0 && t > m_lastAnimQpc)
			dt = (double)(t - m_lastAnimQpc) * 0.001;
		m_lastAnimQpc = t;
	}
	StepScroll(dt);
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

void CLyricsViewWnd::OnRButtonUp(UINT nFlags, CPoint point)
{
	// デスクトップ歌詞オーバーレイ時のみ親へコンテキストメニューを渡す
	if (m_overlay) {
		CPoint sp = point;
		ClientToScreen(&sp);
		CWnd* p = GetParent();
		if (p) {
			p->SendMessage(WM_CONTEXTMENU, (WPARAM)m_hWnd, MAKELPARAM(sp.x, sp.y));
			return;
		}
	}
	CWnd::OnRButtonUp(nFlags, point);
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

	mem.FillSolidRect(&rc, m_overlay ? RGB(18, 18, 28) : RGB(248, 250, 255));

	// 上下フェード帯
	for (int i = 0; i < 12 && i < rc.Height() / 4; i++) {
		const int a = 40 - i * 3;
		if (a <= 0) break;
		mem.FillSolidRect(0, i, rc.Width(), 1, m_overlay ? RGB(28, 28, 40) : RGB(235, 240, 250));
		mem.FillSolidRect(0, rc.bottom - 1 - i, rc.Width(), 1, m_overlay ? RGB(28, 28, 40) : RGB(235, 240, 250));
	}

	if (m_count > 0 && m_lineH > 0) {
		const int scrollPix = ScrollToPix(m_scrollY);
		const int first = (m_lineH > 0) ? (scrollPix / m_lineH) : 0;
		const int last = first + rc.Height() / m_lineH + 2;
		const int padX = 6;

		// 現在行ハイライト帯
		{
			const int cy = (int)((double)m_cur * m_lineH) - scrollPix;
			CRect hi(0, cy - 1, rc.Width(), cy + m_lineH + 1);
			if (hi.bottom > 0 && hi.top < rc.Height()) {
				mem.FillSolidRect(&hi, m_overlay ? RGB(40, 50, 80) : RGB(220, 232, 255));
				CPen pen(PS_SOLID, 1, m_overlay ? RGB(90, 140, 220) : RGB(160, 190, 235));
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
			const int y = (int)((double)i * m_lineH) - scrollPix;
			if (y + m_lineH < 0 || y > rc.Height()) continue;

			const BOOL isCur = (i == m_cur);
			const int dist = abs(i - m_cur);
			COLORREF col;
			if (m_overlay) {
				if (isCur) col = RGB(255, 230, 120);
				else if (dist == 1) col = RGB(220, 225, 240);
				else if (dist == 2) col = RGB(170, 175, 195);
				else col = RGB(130, 135, 155);
			} else {
				if (isCur) col = RGB(30, 70, 170);
				else if (dist == 1) col = RGB(70, 90, 130);
				else if (dist == 2) col = RGB(110, 120, 145);
				else col = RGB(150, 155, 170);
			}

			CFont* use = isCur ? &m_fontHi : &m_font;
			CFont* old = mem.SelectObject(use);
			CRect tr(padX, y, rc.Width() - padX, y + m_lineH);
			// 現在行: 時刻間の進捗で左→右に色を追従(文字単位ではなくピクセルクリップ)
			// END_ELLIPSIS は幅計算と描画がずれるのでカラオケ塗りでは使わない。
			if (isCur && m_tmCount >= 2 && m_frac > 0.001) {
				CSize te = mem.GetTextExtent(m_line[i]);
				int tw = te.cx;
				if (tw > tr.Width()) tw = tr.Width();
				if (tw < 1) tw = 1;
				const int split = tr.left + (int)(tw * m_frac + 0.5);
				mem.SetTextColor(m_overlay ? RGB(140, 145, 165) : RGB(150, 155, 170));
				mem.DrawText(m_line[i], &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
				CRgn clip;
				if (split > tr.left && clip.CreateRectRgn(tr.left, tr.top, split, tr.bottom)) {
					mem.SelectClipRgn(&clip);
					mem.SetTextColor(m_overlay ? RGB(255, 90, 140) : RGB(220, 40, 90));
					mem.DrawText(m_line[i], &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
					mem.SelectClipRgn(NULL);
				}
			} else {
				mem.SetTextColor(col);
				mem.DrawText(m_line[i], &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
			}
			mem.SelectObject(old);
		}
	}
	else {
		mem.SetBkMode(TRANSPARENT);
		mem.SetTextColor(m_overlay ? RGB(200, 205, 220) : RGB(140, 150, 170));
		if (m_font.GetSafeHandle())
			mem.SelectObject(&m_font);
		CString empty = LL14(
			L"（歌詞なし）", L"(No lyrics)", L"(Pas de paroles)", L"(Nessun testo)", L"(Sin letra)",
			L"(가사 없음)", L"（无歌词）", L"(لا كلمات)", L"(Нет текста)", L"(Kein Text)",
			L"(Sem letra)", L"(Geen tekst)", L"(Brak tekstu)", L"(Söz yok)");
		mem.DrawText(empty, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

#if CCUSTOM_AERO_SUPPORT
	// キャプション常時アクリル(本文 aero=0)でも親を辿ってガラスなら不透明合成。
	// 素 BitBlt だと α=0 のまま開き閉じて追従描画が見えない。
	BOOL needOpaque = m_overlay ? TRUE : FALSE;
	if (!needOpaque && CCC_IsWin11()) {
		if (CCC_IsAeroEnabled())
			needOpaque = TRUE;
		else {
			for (HWND h = m_hWnd; h; h = ::GetParent(h)) {
				if (CCC_AcrylicCaption(h)) { needOpaque = TRUE; break; }
			}
		}
	}
	if (needOpaque) {
		CCC_BlitStretchOpaque(pdc.GetSafeHdc(), 0, 0, rc.Width(), rc.Height(),
			mem.GetSafeHdc(), 0, 0, rc.Width(), rc.Height());
	}
	else
#endif
	{
		pdc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
	}
	mem.SelectObject(oldBmp);
}
