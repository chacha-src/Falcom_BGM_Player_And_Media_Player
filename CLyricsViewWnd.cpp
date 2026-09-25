#include "StdAfx.h"
#include "CLyricsViewWnd.h"
#include "CCustomControl.h"
#include <math.h>

IMPLEMENT_DYNAMIC(CLyricsViewWnd, CWnd)

namespace {
	const UINT_PTR kAnimTimer = 61;
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

	inline COLORREF LrcLerpRgb(COLORREF a, COLORREF b, double t)
	{
		if (t < 0.0) t = 0.0;
		if (t > 1.0) t = 1.0;
		const int ar = GetRValue(a), ag = GetGValue(a), ab = GetBValue(a);
		const int br = GetRValue(b), bg = GetGValue(b), bb = GetBValue(b);
		return RGB(ar + (int)((br - ar) * t + 0.5),
			ag + (int)((bg - ag) * t + 0.5),
			ab + (int)((bb - ab) * t + 0.5));
	}

	void LrcBlitScanGlow(CDC& dst, int x, int y, int h, UINT dpi, BOOL overlay)
	{
		if (h < 4) return;
		int gw = MulDiv(20, (int)dpi, 96);
		if (gw < 10) gw = 10;
		if (gw > 36) gw = 36;
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = gw;
		bi.bmiHeader.biHeight = -h;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		void* bits = NULL;
		HBITMAP hb = ::CreateDIBSection(dst.GetSafeHdc(), &bi, DIB_RGB_COLORS, &bits, NULL, 0);
		if (!hb || !bits) return;
		DWORD* p = (DWORD*)bits;
		const double peak = 0.28;
		for (int yy = 0; yy < h; ++yy) {
			const double vy = (h <= 1) ? 0.0 : fabs((double)yy / (double)(h - 1) - 0.5) * 2.0;
			const double vFade = 1.0 - vy * 0.22;
			for (int xx = 0; xx < gw; ++xx) {
				const double u = (gw <= 1) ? 0.0 : (double)xx / (double)(gw - 1);
				double a = exp(-((u - peak) * (u - peak)) * 18.0) * vFade;
				if (a < 0.0) a = 0.0;
				if (a > 1.0) a = 1.0;
				const BYTE al = (BYTE)(a * 210.0 + 0.5);
				BYTE r = overlay ? 255 : 255;
				BYTE g = overlay ? (BYTE)(230 - 40 * u) : (BYTE)(210 - 30 * u);
				BYTE b = overlay ? (BYTE)(140 + 40 * u) : (BYTE)(80 + 30 * u);
				const BYTE pr = (BYTE)((r * al) / 255);
				const BYTE pg = (BYTE)((g * al) / 255);
				const BYTE pb = (BYTE)((b * al) / 255);
				p[yy * gw + xx] = ((DWORD)al << 24) | ((DWORD)pr) | ((DWORD)pg << 8) | ((DWORD)pb << 16);
			}
		}
		HDC hdc = ::CreateCompatibleDC(dst.GetSafeHdc());
		HGDIOBJ old = ::SelectObject(hdc, hb);
		BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		::GdiAlphaBlend(dst.GetSafeHdc(), x - (int)(gw * peak), y, gw, h, hdc, 0, 0, gw, h, bf);
		::SelectObject(hdc, old);
		::DeleteDC(hdc);
		::DeleteObject(hb);
	}

	void LrcDrawStar(CDC& dc, int cx, int cy, int r, COLORREF fill, COLORREF edge)
	{
		if (r < 2) return;
		POINT pt[8];
		for (int i = 0; i < 8; ++i) {
			const double ang = (double)i * 3.141592653589793 * 0.25 - 3.141592653589793 * 0.5;
			const double rr = (i & 1) ? (double)r * 0.36 : (double)r;
			pt[i].x = cx + (int)(cos(ang) * rr + 0.5);
			pt[i].y = cy + (int)(sin(ang) * rr + 0.5);
		}
		CBrush br(fill);
		CPen pe(PS_SOLID, 1, edge);
		CBrush* obr = dc.SelectObject(&br);
		CPen* ope = dc.SelectObject(&pe);
		dc.Polygon(pt, 8);
		dc.SelectObject(obr);
		dc.SelectObject(ope);
	}
}

BEGIN_MESSAGE_MAP(CLyricsViewWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_MOUSEWHEEL()
	ON_WM_RBUTTONUP()
	ON_MESSAGE(WM_LRC_ANIM_TICK, OnAnimTick)
END_MESSAGE_MAP()

CLyricsViewWnd::CLyricsViewWnd()
	: m_count(0)
	, m_tmCount(0)
	, m_cur(0)
	, m_frac(0.0)
	, m_fracDisp(0.0)
	, m_playSec(0.0)
	, m_lineDurSec(1.0)
	, m_lastPlayQpc(0)
	, m_sparkGlyph(-1)
	, m_flashT(1.0)
	, m_rng(0xC0FFEEu)
	, m_sparkN(0)
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
	, m_overlayAlpha(255)
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
	m_fracDisp = 0.0;
	m_playSec = 0.0;
	m_sparkGlyph = -1;
	m_flashT = 1.0;
	m_sparkN = 0;
	m_scrollY = 0.0;
	m_targetY = 0.0;
	m_scrollVel = 0.0;
	m_fastCatch = FALSE;
	ZeroMemory(m_tm, sizeof(m_tm));
	StopAnim();
	if (m_hWnd)
		RequestRedraw();
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
		RequestRedraw();
}

void CLyricsViewWnd::SetOverlayStyle(BOOL on)
{
	if (m_overlay == on) return;
	m_overlay = on;
	const int pt = m_fontPt > 0 ? m_fontPt : (m_overlay ? 140 : 100);
	m_fontPt = 0; // force recreate
	EnsureFonts(m_overlay ? max(pt, 130) : pt, m_fontFace.IsEmpty() ? _T("Segoe UI") : (LPCTSTR)m_fontFace);
	RequestRedraw();
}

void CLyricsViewWnd::SetOverlayAlpha(BYTE a)
{
	if (a < 40) a = 40;
	if (m_overlayAlpha == a) return;
	m_overlayAlpha = a;
	if (m_overlay)
		RequestRedraw();
}

void CLyricsViewWnd::RequestRedraw()
{
	if (!m_hWnd || !::IsWindow(m_hWnd)) return;
	extern volatile LONG g_appExiting;
	if (InterlockedCompareExchange(&g_appExiting, 0, 0))
		return;
	if (m_overlay && (::GetWindowLong(m_hWnd, GWL_EXSTYLE) & WS_EX_LAYERED)) {
		PresentOverlay();
		return;
	}
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
		RequestRedraw();
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
		RequestRedraw();
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
		RequestRedraw();
}

void CLyricsViewWnd::SetPlayCentis(DWORD centis)
{
	SetPlaySec((double)centis * 0.01);
}

void CLyricsViewWnd::SetPlaySec(double sec)
{
	if (m_count <= 0 || m_tmCount < 2) {
		SetCurrent(0);
		m_frac = 0.0;
		m_fracDisp = 0.0;
		return;
	}
	if (sec < 0.0) sec = 0.0;
	const double centis = sec * 100.0;
	int idx = 0;
	for (int i = 0; i < m_tmCount - 1; i++) {
		if ((double)m_tm[i] <= centis && (double)m_tm[i + 1] > centis) {
			idx = i;
			break;
		}
		if (centis >= (double)m_tm[i])
			idx = i;
	}
	if (idx < 0) idx = 0;
	if (idx >= m_count) idx = m_count - 1;
	const double t0 = (double)m_tm[idx] * 0.01;
	double t1 = (idx + 1 < m_tmCount) ? ((double)m_tm[idx + 1] * 0.01) : (t0 + 5.0);
	if (t1 <= t0)
		t1 = t0 + 0.01;
	double frac = (sec - t0) / (t1 - t0);
	if (frac < 0.0) frac = 0.0;
	if (frac > 1.0) frac = 1.0;
	const BOOL curChanged = (idx != m_cur);
	const BOOL seekJump = (AbsD(frac - m_frac) > 0.18) || (AbsD(sec - m_playSec) > 0.45);
	m_frac = frac;
	m_playSec = sec;
	m_lineDurSec = t1 - t0;
	m_fracDisp = frac;
	if (curChanged || seekJump) {
		m_sparkGlyph = -1;
		m_flashT = 1.0;
		if (curChanged)
			SetCurrent(idx);
		else
			RecalcTarget();
	} else {
		RecalcTarget();
	}
	StartAnim();
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
	// 描画進捗で視線を滑らかに送る（時計の段差をそのまま目標にしない）
	double f = m_fracDisp;
	if (f < 0.0) f = 0.0;
	if (f > 1.0) f = 1.0;
	const double softFrac = f * f * (3.0 - 2.0 * f);
	const double curMid = ((double)m_cur + softFrac + 0.5) * (double)m_lineH;

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

BOOL CLyricsViewWnd::NeedAnim() const
{
	if (m_tmCount >= 2 && m_count > 0)
		return TRUE;
	if (m_sparkN > 0) return TRUE;
	if (m_fastCatch) return TRUE;
	if (AbsD(m_scrollY - m_targetY) > 0.25) return TRUE;
	return FALSE;
}

void CLyricsViewWnd::SpawnSparks(int x, int y, int n, UINT dpi)
{
	if (n < 1) n = 1;
	if (n > 5) n = 5;
	const float base = (float)MulDiv(10, (int)dpi, 96);
	for (int i = 0; i < n && m_sparkN < kMaxSparks; ++i) {
		m_rng = m_rng * 1664525u + 1013904223u;
		const double ang = ((double)(m_rng >> 16) / 65536.0) * 6.283185307179586 - 3.141592653589793;
		m_rng = m_rng * 1664525u + 1013904223u;
		const double spd = 55.0 + ((double)(m_rng >> 16) / 65536.0) * 90.0;
		LrcSpark& s = m_sparks[m_sparkN++];
		s.x = (float)x;
		s.y = (float)y;
		s.vx = (float)(cos(ang) * spd);
		s.vy = (float)(sin(ang) * spd - 30.0);
		s.life = 0.0f;
		s.maxLife = 0.22f + (float)((m_rng >> 8) & 255) / 255.0f * 0.20f;
		s.size = base * (0.55f + (float)((m_rng >> 4) & 15) / 15.0f * 0.7f);
	}
}

void CLyricsViewWnd::StepKara(double dtSec)
{
	if (dtSec < 0.0) dtSec = 0.0;
	if (dtSec > 0.05) dtSec = 0.05;
	m_flashT += dtSec;
	for (int i = 0; i < m_sparkN; ) {
		LrcSpark& s = m_sparks[i];
		s.life += (float)dtSec;
		s.x += s.vx * (float)dtSec;
		s.y += s.vy * (float)dtSec;
		s.vy += 140.0f * (float)dtSec;
		if (s.life >= s.maxLife) {
			m_sparks[i] = m_sparks[m_sparkN - 1];
			--m_sparkN;
		} else {
			++i;
		}
	}
}

void CLyricsViewWnd::RequestAnimTick()
{
	if (!m_hWnd) return;
	extern volatile LONG g_appExiting;
	if (InterlockedCompareExchange(&g_appExiting, 0, 0))
		return;
	if (InterlockedCompareExchange(&m_animPosted, 1, 0) != 0)
		return;
	if (!::PostMessage(m_hWnd, WM_LRC_ANIM_TICK, 0, 0))
		InterlockedExchange(&m_animPosted, 0);
}

void CLyricsViewWnd::StartAnim()
{
	/* 再生中は COggDlg::timerp → LyricsOnTimerp → TickFrame。
	   SetTimer/WM_TIMER は VSYNC Post が続くと合成されず、かっくんする。 */
}

void CLyricsViewWnd::TickFrame()
{
	if (!m_hWnd || !::IsWindow(m_hWnd)) return;
	extern volatile LONG g_appExiting;
	if (InterlockedCompareExchange(&g_appExiting, 0, 0))
		return;
	extern double OggGetLyricsPlaySec();
	SetPlaySec(OggGetLyricsPlaySec());
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
	if (dt > 0.05) dt = 0.016;
	StepKara(dt);
	StepScroll(dt);
	RequestRedraw();
}

LRESULT CLyricsViewWnd::OnAnimTick(WPARAM, LPARAM)
{
	InterlockedExchange(&m_animPosted, 0);
	TickFrame();
	return 0;
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

void CLyricsViewWnd::OnDestroy()
{
	StopAnim();
	CWnd::OnDestroy();
}

void CLyricsViewWnd::StepScroll(double dtSec)
{
	if (dtSec < 0.0) dtSec = 0.0;
	if (dtSec > 0.05) dtSec = 0.05;
	const double d = m_targetY - m_scrollY;
	const double ad = AbsD(d);
	const double lineH = (m_lineH > 0) ? (double)m_lineH : 18.0;

	if (ad < 0.20 && AbsD(m_scrollVel) < 6.0) {
		m_scrollY = m_targetY;
		m_scrollVel = 0.0;
		m_fastCatch = FALSE;
		return;
	}

	if (m_fastCatch || ad > lineH * 4.0) {
		m_fastCatch = TRUE;
		const double tau = 0.28;
		double v = d / tau;
		const double vmin = lineH * 70.0;
		const double vmax = lineH * 220.0;
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
			if (AbsD(m_targetY - m_scrollY) < lineH * 1.75)
				m_fastCatch = FALSE;
		}
		return;
	}

	const double omega = 9.5;
	const double zeta = 1.08;
	const double acc = (omega * omega) * d - (2.0 * zeta * omega) * m_scrollVel;
	m_scrollVel += acc * dtSec;
	const double vmax = lineH * 18.0;
	if (m_scrollVel > vmax) m_scrollVel = vmax;
	if (m_scrollVel < -vmax) m_scrollVel = -vmax;
	m_scrollY += m_scrollVel * dtSec;
}

void CLyricsViewWnd::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != kAnimTimer) {
		CWnd::OnTimer(nIDEvent);
		return;
	}
	TickFrame();
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

void CLyricsViewWnd::RenderFrame(CDC& mem, int w, int h)
{
	CRect rc(0, 0, w, h);
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
			if (isCur && m_tmCount >= 2) {
				INT dx[512];
				int nch = m_line[i].GetLength();
				if (nch > 512) nch = 512;
				SIZE te = {};
				if (nch > 0)
					::GetTextExtentExPoint(mem.GetSafeHdc(), m_line[i], nch, 0, NULL, dx, &te);
				int tw = te.cx;
				if (tw <= 0) {
					const CSize te2 = mem.GetTextExtent(m_line[i]);
					tw = te2.cx;
				}
				if (tw > tr.Width()) tw = tr.Width();
				if (tw < 1) tw = 1;
				double fd = m_fracDisp;
				if (fd + 0.02 < m_frac)
					fd = m_frac;
				if (fd < 0.0) fd = 0.0;
				if (fd > 1.0) fd = 1.0;
				const int split = tr.left + (int)(tw * fd + 0.5);

				mem.SetTextColor(m_overlay ? RGB(150, 154, 172) : RGB(150, 155, 170));
				mem.DrawText(m_line[i], &tr, dtFlags);
				if (split > tr.left) {
					const int sav = mem.SaveDC();
					mem.IntersectClipRect(tr.left, tr.top, split, tr.bottom);
					mem.SetTextColor(m_overlay ? RGB(255, 96, 150) : RGB(220, 40, 90));
					mem.DrawText(m_line[i], &tr, dtFlags);
					mem.RestoreDC(sav);
				}

				int glyph = (nch > 0) ? (nch - 1) : -1;
				const int px = (int)(tw * fd + 0.5);
				for (int g = 0; g < nch; ++g) {
					if (dx[g] > px) { glyph = g; break; }
				}
				if (!m_fastCatch) {
					if (m_sparkGlyph < 0) {
						m_sparkGlyph = glyph;
					} else if (glyph > m_sparkGlyph && nch > 0) {
						const int gi = glyph;
						const int x0 = tr.left + ((gi > 0) ? dx[gi - 1] : 0);
						const int x1 = tr.left + dx[gi];
						SpawnSparks((x0 + x1) / 2, y + m_lineH / 2, 4, dpi);
						m_flashT = 0.0;
						m_sparkGlyph = glyph;
					} else if (glyph < m_sparkGlyph) {
						m_sparkGlyph = glyph;
					}
				}

				if (!m_fastCatch && nch > 0 && m_sparkGlyph >= 0 && m_sparkGlyph < nch && m_flashT < 0.28) {
					const int gi = m_sparkGlyph;
					const int x0 = tr.left + ((gi > 0) ? dx[gi - 1] : 0);
					const int x1 = tr.left + dx[gi];
					const double k = 1.0 - m_flashT / 0.28;
					const COLORREF gold = LrcLerpRgb(
						m_overlay ? RGB(255, 96, 150) : RGB(220, 40, 90),
						RGB(255, 250, 210), k * k);
					const int sav = mem.SaveDC();
					mem.IntersectClipRect(x0 - 1, tr.top, x1 + 1, tr.bottom);
					mem.SetTextColor(gold);
					mem.DrawText(m_line[i], &tr, dtFlags);
					mem.RestoreDC(sav);
				}

				if (fd > 0.002 && fd < 0.995)
					LrcBlitScanGlow(mem, split, tr.top, tr.Height(), dpi, m_overlay);
			} else {
				mem.SetTextColor(col);
				mem.DrawText(m_line[i], &tr, dtFlags);
			}
			mem.SelectObject(old);
		}

		if (!m_fastCatch && m_sparkN > 0) {
			mem.SetBkMode(TRANSPARENT);
			for (int si = 0; si < m_sparkN; ++si) {
				const LrcSpark& s = m_sparks[si];
				double t = (s.maxLife > 0.001f) ? (s.life / s.maxLife) : 1.0;
				if (t < 0.0) t = 0.0;
				if (t > 1.0) t = 1.0;
				const double fade = 1.0 - t;
				const int r = (int)(s.size * (0.55 + 0.7 * fade) + 0.5);
				const COLORREF fill = LrcLerpRgb(RGB(255, 248, 200), RGB(255, 140, 80), t);
				const COLORREF edge = RGB(255, 255, 255);
				LrcDrawStar(mem, (int)(s.x + 0.5), (int)(s.y + 0.5), r, fill, edge);
				if (fade > 0.45) {
					CPen pe(PS_SOLID, 1, RGB(255, 255, 255));
					CPen* op = mem.SelectObject(&pe);
					const int arm = r + 2;
					const int cx = (int)(s.x + 0.5);
					const int cy = (int)(s.y + 0.5);
					mem.MoveTo(cx - arm, cy);
					mem.LineTo(cx + arm + 1, cy);
					mem.MoveTo(cx, cy - arm);
					mem.LineTo(cx, cy + arm + 1);
					mem.SelectObject(op);
				}
			}
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
}

void CLyricsViewWnd::PresentOverlay()
{
	if (!m_hWnd || !::IsWindow(m_hWnd)) return;
	extern volatile LONG g_appExiting;
	if (InterlockedCompareExchange(&g_appExiting, 0, 0))
		return;
	CRect rc;
	GetClientRect(&rc);
	const int w = rc.Width();
	const int h = rc.Height();
	if (w <= 0 || h <= 0) return;
	EnsureMemDC(w, h);
	RenderFrame(m_memDC, w, h);
	BYTE a = m_overlayAlpha;
	if (a < 40) a = 40;
	BLENDFUNCTION bf = { AC_SRC_OVER, 0, a, 0 };
	POINT ptSrc = { 0, 0 };
	SIZE sz = { w, h };
	/* 失敗時に BitBlt すると WM_PAINT → PresentOverlay 再入で止まらない */
	::UpdateLayeredWindow(m_hWnd, NULL, NULL, &sz, m_memDC.GetSafeHdc(), &ptSrc, 0, &bf, ULW_ALPHA);
}

void CLyricsViewWnd::OnPaint()
{
	extern volatile LONG g_appExiting;
	if (InterlockedCompareExchange(&g_appExiting, 0, 0)) {
		CPaintDC pdc(this);
		return;
	}
	CPaintDC pdc(this);
	if (m_overlay && (::GetWindowLong(m_hWnd, GWL_EXSTYLE) & WS_EX_LAYERED)) {
		PresentOverlay();
		return;
	}
	CRect rc;
	GetClientRect(&rc);
	const int w = rc.Width();
	const int h = rc.Height();
	if (w <= 0 || h <= 0) return;

	EnsureMemDC(w, h);
	RenderFrame(m_memDC, w, h);

#if CCUSTOM_AERO_SUPPORT
	if (m_overlay || CCC_IsAeroEnabled() || CCC_IsWin11()) {
		CCC_BlitStretchOpaque(pdc.GetSafeHdc(), 0, 0, w, h,
			m_memDC.GetSafeHdc(), 0, 0, w, h);
	} else {
		pdc.BitBlt(0, 0, w, h, &m_memDC, 0, 0, SRCCOPY);
	}
#else
	pdc.BitBlt(0, 0, w, h, &m_memDC, 0, 0, SRCCOPY);
#endif
	CCC_DrawInwomanOnClient(&pdc, m_hWnd);
}
