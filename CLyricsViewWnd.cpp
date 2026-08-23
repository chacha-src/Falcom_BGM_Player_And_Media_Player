#include "StdAfx.h"
#include "CLyricsViewWnd.h"
#include "CCustomControl.h"
#include <math.h>

IMPLEMENT_DYNAMIC(CLyricsViewWnd, CWnd)

namespace {
	const UINT_PTR kAnimTimer = 61;
	// timerp と同じ ~60fps（16ms）。8ms はタイマ分解能で潰れ、重い描画と重なりギクシャクしやすい
	const UINT kAnimMs = 16;
	const UINT WM_LRC_ANIM_TICK = WM_APP + 0x4C52; // 'LR'
	inline double AbsD(double x) { return (x < 0.0) ? -x : x; }
	// 描画Yの量子化を安定させ、スクロール終端の1px震えを抑える
	inline int ScrollToPix(double v)
	{
		return (int)floor(v + 1e-6);
	}
	UINT LrcGetDpi(HWND hWnd)
	{
		if (!hWnd) return 96;
		HMODULE user32 = ::GetModuleHandle(_T("user32.dll"));
		if (user32) {
			typedef UINT (WINAPI* GetDpiForWindowFn)(HWND);
			GetDpiForWindowFn p = (GetDpiForWindowFn)::GetProcAddress(user32, "GetDpiForWindow");
			if (p) {
				const UINT d = p(hWnd);
				if (d >= 72 && d <= 480) return d;
			}
		}
		HDC hdc = ::GetDC(hWnd);
		if (!hdc) return 96;
		const int d = ::GetDeviceCaps(hdc, LOGPIXELSY);
		::ReleaseDC(hWnd, hdc);
		return (d >= 72) ? (UINT)d : 96;
	}
	// テキストが maxW に収まるよう base を縮小したフォントを out に作る。縮小不要なら FALSE。
	BOOL LrcMakeFitFont(CDC& dc, CFont& base, LPCTSTR text, int maxW, UINT dpi, CFont& out)
	{
		if (out.GetSafeHandle()) out.DeleteObject();
		if (!text || !text[0] || maxW < 12 || !base.GetSafeHandle()) return FALSE;
		CFont* old = dc.SelectObject(&base);
		const CSize sz = dc.GetTextExtent(text);
		dc.SelectObject(old);
		if (sz.cx <= 0) return FALSE;
		// DrawText / ClearType の端ピクセル欠けを避けるため少し余白を残して縮める
		const int safeW = maxW - MulDiv(4, (int)dpi, 96);
		if (safeW < 8) return FALSE;
		if (sz.cx <= safeW) return FALSE;
		LOGFONT lf = {};
		base.GetLogFont(&lf);
		if (lf.lfHeight == 0) return FALSE;
		double scale = (double)safeW / (double)sz.cx;
		if (scale > 0.995) scale = 0.98; // ほぼ同じ幅でも一拍縮める
		if (scale < 0.28) scale = 0.28;
		lf.lfHeight = (LONG)((double)lf.lfHeight * scale);
		const int minPx = MulDiv(8, (int)dpi, 72);
		if (lf.lfHeight < 0) {
			if (-lf.lfHeight < minPx) lf.lfHeight = -minPx;
		} else {
			if (lf.lfHeight > 0 && lf.lfHeight < minPx) lf.lfHeight = minPx;
			if (lf.lfHeight == 0) lf.lfHeight = minPx;
		}
		return out.CreateFontIndirect(&lf) ? TRUE : FALSE;
	}
}

BEGIN_MESSAGE_MAP(CLyricsViewWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_MOUSEWHEEL()
	ON_WM_RBUTTONUP()
	ON_MESSAGE(WM_LRC_ANIM_TICK, &CLyricsViewWnd::OnAnimTick)
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
	, m_fastCatch(FALSE)
	, m_lastAnimQpc(0)
	, m_qpcFreq(0)
	, m_fontPt(0)
	, m_dpi(96)
	, m_timer(0)
	, m_overlay(FALSE)
	, m_animPosted(0)
	, m_oldBmp(nullptr)
	, m_memW(0)
	, m_memH(0)
{
	ZeroMemory(m_tm, sizeof(m_tm));
	LARGE_INTEGER f = {};
	if (::QueryPerformanceFrequency(&f) && f.QuadPart > 0)
		m_qpcFreq = (ULONGLONG)f.QuadPart;
}

CLyricsViewWnd::~CLyricsViewWnd()
{
	StopAnim();
	if (m_memDC.GetSafeHdc()) {
		if (m_oldBmp) m_memDC.SelectObject(m_oldBmp);
		m_oldBmp = nullptr;
		m_memDC.DeleteDC();
	}
	if (m_memBmp.GetSafeHandle()) m_memBmp.DeleteObject();
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
	m_fastCatch = FALSE;
	ZeroMemory(m_tm, sizeof(m_tm));
	StopAnim();
	if (m_hWnd)
		Invalidate(FALSE);
}

UINT CLyricsViewWnd::GetViewDpi() const
{
	return LrcGetDpi(m_hWnd);
}

void CLyricsViewWnd::EnsureMemDC(int w, int h)
{
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (m_memDC.GetSafeHdc() && m_memW == w && m_memH == h && m_memBmp.GetSafeHandle())
		return;
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

void CLyricsViewWnd::EnsureFonts(int dpiPointTenths, LPCTSTR face)
{
	if (dpiPointTenths <= 0) dpiPointTenths = 90;
	if (!face || !face[0]) face = _T("Segoe UI");
	const UINT dpi = GetViewDpi();
	if (m_fontPt == dpiPointTenths && m_dpi == dpi && m_fontFace == face
		&& m_font.GetSafeHandle() && m_fontHi.GetSafeHandle())
		return;
	m_fontPt = dpiPointTenths;
	m_dpi = dpi;
	m_fontFace = face;
	if (m_font.GetSafeHandle()) m_font.DeleteObject();
	if (m_fontHi.GetSafeHandle()) m_fontHi.DeleteObject();

	// 窓の DC を渡して Per-Monitor DPI でポイント→ピクセル変換する
	CClientDC dc(this);
	m_font.CreatePointFont(dpiPointTenths, face, &dc);
	LOGFONT lf = {};
	m_font.GetLogFont(&lf);
	lf.lfWeight = FW_BOLD;
	lf.lfHeight = (LONG)(lf.lfHeight * 1.12);
	if (lf.lfHeight == 0) lf.lfHeight = -MulDiv(14, (int)dpi, 72);
	m_fontHi.CreateFontIndirect(&lf);

	CFont* old = dc.SelectObject(&m_fontHi);
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	const int pad = m_overlay ? MulDiv(8, (int)dpi, 96) : MulDiv(4, (int)dpi, 96);
	const int prevLH = m_lineH;
	m_lineH = tm.tmHeight + tm.tmExternalLeading + pad;
	const int minLH = MulDiv(16, (int)dpi, 96);
	if (m_lineH < minLH) m_lineH = minLH;
	dc.SelectObject(old);
	// 行高変化時はスクロール位置を比率で引き継ぎ（大ジャンプで瞬間合わせしない＝途中 chase を殺さない）
	if (prevLH > 0 && m_lineH > 0 && prevLH != m_lineH)
		m_scrollY = m_scrollY * ((double)m_lineH / (double)prevLH);
	RecalcTarget();
	if (AbsD(m_scrollY - m_targetY) > 0.35)
		StartAnim();
	if (m_hWnd)
		Invalidate(FALSE);
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
	BOOL linesChanged = (count != m_count);
	if (!linesChanged && lines) {
		for (int i = 0; i < count; i++) {
			if (m_line[i] != lines[i]) { linesChanged = TRUE; break; }
		}
	}
	BOOL timesChanged = (timeCount != m_tmCount);
	if (!timesChanged && times) {
		for (int i = 0; i < timeCount; i++) {
			if (m_tm[i] != times[i]) { timesChanged = TRUE; break; }
		}
	}
	if (!linesChanged && !timesChanged) return;
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
	if (linesChanged) {
		// 曲／歌詞本文の入れ替え: 頭から高速 chase（途中オープンと同じ）
		m_scrollY = 0.0;
		m_fastCatch = (AbsD(m_targetY) > (double)m_lineH * 2.0) ? TRUE : FALSE;
		if (m_fastCatch)
			StartAnim();
		else
			m_scrollY = m_targetY;
	} else {
		// 時刻だけの微調整: 現位置を保ち通常追従
		if (AbsD(m_scrollY - m_targetY) > 0.35)
			StartAnim();
	}
	if (m_hWnd)
		Invalidate(FALSE);
}

void CLyricsViewWnd::BeginCatchFromTop()
{
	m_scrollY = 0.0;
	m_scrollVel = 0.0;
	RecalcTarget();
	m_fastCatch = (AbsD(m_targetY - m_scrollY) > 0.35) ? TRUE : FALSE;
	if (m_fastCatch)
		StartAnim();
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
	// 大距離 or 先頭付近からの chase → 高速パス
	const double gap = AbsD(m_targetY - m_scrollY);
	if (gap > (double)m_lineH * 3.0 || (m_scrollY < (double)m_lineH * 1.5 && gap > (double)m_lineH))
		m_fastCatch = TRUE;
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
		if (fracChanged && m_hWnd && !m_fastCatch)
			Invalidate(FALSE);
	}
}

void CLyricsViewWnd::RecalcTarget()
{
	CRect rc;
	if (!m_hWnd) { m_targetY = 0.0; return; }
	GetClientRect(&rc);
	const int viewH = rc.Height();
	if (viewH <= 0 || m_lineH <= 0 || m_count <= 0) {
		m_targetY = 0.0;
		return;
	}
	// 現在行 + 行内進捗（切替の段差を消す）
	const double softFrac = m_frac * m_frac * (3.0 - 2.0 * m_frac); // smoothstep
	const double curMid = ((double)m_cur + softFrac + 0.5) * (double)m_lineH;

	// 視線は中央よりやや下（約58%）。上に寄りすぎ／上端欠けを避ける。
	// 先頭は target<0→0、末尾は maxY で最終行を下端へ。
	const double focusY = (double)viewH * 0.58;
	m_targetY = curMid - focusY;

	const double contentH = (double)m_count * (double)m_lineH;
	const double maxY = contentH - (double)viewH;
	if (maxY <= 0.0) {
		m_targetY = 0.0;
	} else {
		if (m_targetY < 0.0) m_targetY = 0.0;
		if (m_targetY > maxY) m_targetY = maxY;
	}
}

void CLyricsViewWnd::RequestAnimTick()
{
	if (!m_hWnd) return;
	if (InterlockedCompareExchange(&m_animPosted, 1, 0) != 0)
		return;
	if (!::PostMessage(m_hWnd, WM_LRC_ANIM_TICK, 0, 0))
		InterlockedExchange(&m_animPosted, 0);
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
	// timerp と同じ: oneshot Post で UI スレッドに即時フレームを積む（タイマ待ちを減らす）
	RequestAnimTick();
}

void CLyricsViewWnd::StopAnim()
{
	if (m_timer && m_hWnd) {
		KillTimer(m_timer);
		m_timer = 0;
	}
	m_scrollVel = 0.0;
	m_fastCatch = FALSE;
	InterlockedExchange(&m_animPosted, 0);
}

void CLyricsViewWnd::StepScroll(double dtSec)
{
	if (dtSec < 0.0) dtSec = 0.0;
	if (dtSec > 0.05) dtSec = 0.05; // スパイク吸収
	const double d = m_targetY - m_scrollY;
	const double ad = AbsD(d);
	const double lineH = (m_lineH > 0) ? (double)m_lineH : 18.0;

	if (ad < 0.25 && AbsD(m_scrollVel) < 8.0) {
		m_scrollY = m_targetY;
		m_scrollVel = 0.0;
		m_fastCatch = FALSE;
		StopAnim();
		Invalidate(FALSE);
		return;
	}

	// ---- 大距離 catch-up（途中オープン／歌詞入替）: ~0.3〜0.45 秒で該当行へ ----
	if (m_fastCatch || ad > lineH * 4.0) {
		m_fastCatch = TRUE;
		// 残り距離を tau 秒で埋める速度。下限で「止まって見える」のを防ぐ
		const double tau = 0.28;
		double v = d / tau;
		const double vmin = lineH * 70.0;   // 最低 ~70 行/秒
		const double vmax = lineH * 220.0;  // 上限 ~220 行/秒（長尺でも ~0.5s）
		if (AbsD(v) < vmin) v = (d >= 0.0) ? vmin : -vmin;
		if (v > vmax) v = vmax;
		if (v < -vmax) v = -vmax;
		const double step = v * dtSec;
		if (AbsD(step) >= ad) {
			m_scrollY = m_targetY;
			m_scrollVel = 0.0;
			m_fastCatch = FALSE;
		} else {
			m_scrollY += step;
			m_scrollVel = v;
			// 残りが数行になったら通常の臨界減衰へ（着地を滑らかに）
			if (AbsD(m_targetY - m_scrollY) < lineH * 1.75)
				m_fastCatch = FALSE;
		}
		Invalidate(FALSE);
		return;
	}

	// ---- 通常追従: 臨界減衰っぽい（行送り） ----
	const double omega = 14.0;
	const double zeta = 1.05;
	const double acc = (omega * omega) * d - (2.0 * zeta * omega) * m_scrollVel;
	m_scrollVel += acc * dtSec;
	const double vmax = lineH * 28.0; // 旧14 → 行送りも少し機敏に
	if (m_scrollVel > vmax) m_scrollVel = vmax;
	if (m_scrollVel < -vmax) m_scrollVel = -vmax;
	m_scrollY += m_scrollVel * dtSec;
	Invalidate(FALSE);
}

LRESULT CLyricsViewWnd::OnAnimTick(WPARAM, LPARAM)
{
	InterlockedExchange(&m_animPosted, 0);
	if (!m_hWnd) return 0;
	double dt = 0.016;
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
	const BOOL wasCatch = m_fastCatch;
	const double before = m_scrollY;
	StepScroll(dt);
	// まだ追従中なら次フレームを即 Post（timerp の oneshot 連鎖）。16ms 未満ならタイマに任せる
	if (m_timer && AbsD(m_scrollY - m_targetY) > 0.35) {
		if (wasCatch || AbsD(m_scrollY - before) > 0.5) {
			if (dt >= 0.012)
				RequestAnimTick();
		}
	}
	return 0;
}

void CLyricsViewWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != kAnimTimer) {
		CWnd::OnTimer(nIDEvent);
		return;
	}
	// バックアップ駆動（Post が落ちても 60fps で継続）
	RequestAnimTick();
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
	m_fastCatch = FALSE;
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
	// 歌詞ウィンドウオーバーレイ時のみ親へコンテキストメニューを渡す
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
	const int w = rc.Width();
	const int h = rc.Height();
	if (w <= 0 || h <= 0) return;

	EnsureMemDC(w, h);
	CDC& mem = m_memDC;

	mem.FillSolidRect(&rc, m_overlay ? RGB(18, 18, 28) : RGB(248, 250, 255));

	// catch-up 中は上下フェード帯を省略（描画負荷を下げる）
	if (!m_fastCatch) {
		for (int i = 0; i < 12 && i < h / 4; i++) {
			const int fa = 40 - i * 3;
			if (fa <= 0) break;
			mem.FillSolidRect(0, i, w, 1, m_overlay ? RGB(28, 28, 40) : RGB(235, 240, 250));
			mem.FillSolidRect(0, h - 1 - i, w, 1, m_overlay ? RGB(28, 28, 40) : RGB(235, 240, 250));
		}
	}

	if (m_count > 0 && m_lineH > 0) {
		const int scrollPix = ScrollToPix(m_scrollY);
		const int first = (m_lineH > 0) ? (scrollPix / m_lineH) : 0;
		const int last = first + h / m_lineH + 2;
		const UINT dpi = m_dpi ? m_dpi : LrcGetDpi(m_hWnd);
		const int padX = MulDiv(8, (int)dpi, 96);

		{
			const int cy = (int)((double)m_cur * m_lineH) - scrollPix;
			CRect hi(0, cy - 1, w, cy + m_lineH + 1);
			if (hi.bottom > 0 && hi.top < h) {
				mem.FillSolidRect(&hi, m_overlay ? RGB(40, 50, 80) : RGB(220, 232, 255));
				if (!m_fastCatch) {
					CPen pen(PS_SOLID, 1, m_overlay ? RGB(90, 140, 220) : RGB(160, 190, 235));
					CPen* op = mem.SelectObject(&pen);
					mem.MoveTo(0, hi.top);
					mem.LineTo(w, hi.top);
					mem.MoveTo(0, hi.bottom - 1);
					mem.LineTo(w, hi.bottom - 1);
					mem.SelectObject(op);
				}
			}
		}

		mem.SetBkMode(TRANSPARENT);
		for (int i = first; i <= last; i++) {
			if (i < 0 || i >= m_count) continue;
			const int y = (int)((double)i * m_lineH) - scrollPix;
			if (y + m_lineH < 0 || y > h) continue;

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
			CFont fit;
			CRect tr(padX, y, w - padX, y + m_lineH);
			// catch-up 中は FitFont を省略（毎行 CreateFont がギクシャクの主因）
			const BOOL fitted = (!m_fastCatch)
				&& LrcMakeFitFont(mem, *use, m_line[i], tr.Width(), dpi, fit);
			if (fitted)
				use = &fit;
			CFont* old = mem.SelectObject(use);
			const UINT dtFlags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
			if (!m_fastCatch && isCur && m_tmCount >= 2 && m_frac > 0.001) {
				CSize te = mem.GetTextExtent(m_line[i]);
				int tw = te.cx;
				if (tw > tr.Width()) tw = tr.Width();
				if (tw < 1) tw = 1;
				const int split = tr.left + (int)(tw * m_frac + 0.5);
				mem.SetTextColor(m_overlay ? RGB(140, 145, 165) : RGB(150, 155, 170));
				mem.DrawText(m_line[i], &tr, dtFlags);
				CRgn clip;
				if (split > tr.left && clip.CreateRectRgn(tr.left, tr.top, split, tr.bottom)) {
					mem.SelectClipRgn(&clip);
					mem.SetTextColor(m_overlay ? RGB(255, 90, 140) : RGB(220, 40, 90));
					mem.DrawText(m_line[i], &tr, dtFlags);
					mem.SelectClipRgn(NULL);
				}
			} else {
				mem.SetTextColor(col);
				mem.DrawText(m_line[i], &tr, dtFlags);
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
	if (m_overlay || CCC_IsAeroEnabled() || CCC_IsWin11()) {
		CCC_BlitStretchOpaque(pdc.GetSafeHdc(), 0, 0, w, h,
			mem.GetSafeHdc(), 0, 0, w, h);
	} else {
		pdc.BitBlt(0, 0, w, h, &mem, 0, 0, SRCCOPY);
	}
#else
	pdc.BitBlt(0, 0, w, h, &mem, 0, 0, SRCCOPY);
#endif
	CCC_DrawInwomanOnClient(&pdc, m_hWnd);
}
