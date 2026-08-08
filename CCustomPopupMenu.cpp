#include "stdafx.h"
#include "CCustomPopupMenu.h"
#include <uxtheme.h>
#include <math.h>
#include <algorithm>
#pragma comment(lib, "uxtheme.lib")

extern void MpPersistSavedataQuick();

IMPLEMENT_DYNAMIC(CCustomPopupMenu, CWnd)

BEGIN_MESSAGE_MAP(CCustomPopupMenu, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
	ON_MESSAGE(WM_APP + 0x51C, OnRefreshEmbedded)
	ON_WM_NCHITTEST()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_KEYDOWN()
	ON_WM_KILLFOCUS()
	ON_WM_MOUSEWHEEL()
	ON_WM_HSCROLL()
	ON_WM_TIMER()
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnTtnNeedText)
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnTtnNeedText)
END_MESSAGE_MAP()

namespace {
	enum { kTipTimer = 7701, kInwomanTimer = 7702, kAnimTimer = 7703, kBounceTimer = 7704, kSettleTimer = 7705 };
	// BigBang内部マーカー（画面には出さない。ULWでα=0へ変換）
	static const COLORREF kChipChromaKey = RGB(255, 0, 255);

	static wchar_t s_faces[CCUSTOM_POPUP_MAX_FACES][LF_FACESIZE];
	static int s_faceCount = 0;

	static int CALLBACK EnumFaceProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM)
	{
		if (!lf || !lf->lfFaceName[0]) return TRUE;
		if (lf->lfFaceName[0] == L'@') return TRUE;
		if (s_faceCount >= CCUSTOM_POPUP_MAX_FACES) return FALSE;
		for (int i = 0; i < s_faceCount; ++i) {
			if (_wcsicmp(s_faces[i], lf->lfFaceName) == 0)
				return TRUE;
		}
		lstrcpynW(s_faces[s_faceCount], lf->lfFaceName, LF_FACESIZE);
		++s_faceCount;
		return TRUE;
	}

	static void EnsureFaceList()
	{
		if (s_faceCount > 0) return;
		HDC hdc = ::GetDC(NULL);
		LOGFONTW lf;
		ZeroMemory(&lf, sizeof(lf));
		lf.lfCharSet = DEFAULT_CHARSET;
		::EnumFontFamiliesExW(hdc, &lf, EnumFaceProc, 0, 0);
		::ReleaseDC(NULL, hdc);
		// 名前順（簡易）
		for (int i = 0; i < s_faceCount; ++i) {
			for (int j = i + 1; j < s_faceCount; ++j) {
				if (_wcsicmp(s_faces[j], s_faces[i]) < 0) {
					wchar_t t[LF_FACESIZE];
					lstrcpynW(t, s_faces[i], LF_FACESIZE);
					lstrcpynW(s_faces[i], s_faces[j], LF_FACESIZE);
					lstrcpynW(s_faces[j], t, LF_FACESIZE);
				}
			}
		}
	}

	static void ClampPopupFontSave()
	{
		if (savedata.popupMenuPoint < 8) savedata.popupMenuPoint = 8;
		if (savedata.popupMenuPoint > 24) savedata.popupMenuPoint = 24;
		savedata.popupMenuBold = savedata.popupMenuBold ? 1 : 0;
		savedata.popupMenuItalic = savedata.popupMenuItalic ? 1 : 0;
		savedata.popupMenuFace[_countof(savedata.popupMenuFace) - 1] = 0;
	}
	static void ClampPopupAnimSave()
	{
		if (savedata.popupMenuAnim < 0 || savedata.popupMenuAnim >= POPUP_ANIM_COUNT)
			savedata.popupMenuAnim = POPUP_ANIM_AURORA;
	}
	static int PopupAnimStyle()
	{
		ClampPopupAnimSave();
		return savedata.popupMenuAnim;
	}
	// 行ごとのチップ飛行（ULW）。クラシック／上下伸び面は除外
	static BOOL UsesRowChipFlight(int style)
	{
		return style >= POPUP_ANIM_CASCADE && style < POPUP_ANIM_COUNT;
	}
	static int FlightPadForStyle(int style)
	{
		if (style == POPUP_ANIM_BIGBANG || style == POPUP_ANIM_SPIRAL)
			return CCUSTOM_POPUP_FLIGHT_PAD_BIG;
		if (style == POPUP_ANIM_ZIPPER || style == POPUP_ANIM_AURORA)
			return CCUSTOM_POPUP_FLIGHT_PAD_MID;
		return CCUSTOM_POPUP_FLIGHT_PAD_ROW;
	}
	static UINT PopupDpiFromHwnd(HWND hWnd)
	{
		UINT dpi = 96;
		HWND h = hWnd ? hWnd : ::GetDesktopWindow();
		if (HDC hdc = ::GetDC(h)) {
			dpi = (UINT)::GetDeviceCaps(hdc, LOGPIXELSX);
			::ReleaseDC(h, hdc);
		}
		return dpi ? dpi : 96;
	}
	static int PopupSx(UINT dpi, int v96)
	{
		return MulDiv(v96, (int)dpi, 96);
	}
	// 起点からの距離で遅延（放射系）
	static BOOL UsesRadialStagger(int style)
	{
		return style == POPUP_ANIM_BIGBANG
			|| style == POPUP_ANIM_POP
			|| style == POPUP_ANIM_SPIRAL;
	}
	// 上から順（または交互）で遅延
	static BOOL UsesIndexStagger(int style)
	{
		return style == POPUP_ANIM_CASCADE
			|| style == POPUP_ANIM_SLIDE
			|| style == POPUP_ANIM_PETAL
			|| style == POPUP_ANIM_ZIPPER
			|| style == POPUP_ANIM_AURORA;
	}
	static int ChipInDurMs(int style, BOOL asSub)
	{
		if (style == POPUP_ANIM_BIGBANG || style == POPUP_ANIM_SPIRAL)
			return asSub ? 220 : 280;
		if (style == POPUP_ANIM_PETAL || style == POPUP_ANIM_AURORA)
			return asSub ? 200 : 260;
		if (style == POPUP_ANIM_ZIPPER)
			return asSub ? 180 : 240;
		return asSub ? CCUSTOM_POPUP_ANIM_SUB_MS : CCUSTOM_POPUP_ANIM_IN_MS;
	}
	static int ChipOutDurMs(int style)
	{
		if (style == POPUP_ANIM_BIGBANG || style == POPUP_ANIM_SPIRAL)
			return 200;
		if (style == POPUP_ANIM_PETAL || style == POPUP_ANIM_AURORA)
			return 180;
		return CCUSTOM_POPUP_ANIM_OUT_MS;
	}

	// 入場アニメの想定所要(ms)。超過したら強制定着。
	static int ChipEnterTotalMs(int style, BOOL asSub, int itemCount, int origin)
	{
		int stag = asSub ? CCUSTOM_POPUP_LINE_STAGGER_SUB : CCUSTOM_POPUP_LINE_STAGGER_IN;
		if (itemCount > 1) {
			const int need = stag * (itemCount - 1);
			if (need > CCUSTOM_POPUP_LINE_STAGGER_BUDGET)
				stag = max(1, CCUSTOM_POPUP_LINE_STAGGER_BUDGET / (itemCount - 1));
		}
		const int dur = ChipInDurMs(style, asSub);
		int span = 0;
		if (UsesRadialStagger(style) || (!asSub && style == POPUP_ANIM_EXPAND))
			span = max(origin, itemCount - 1 - origin);
		else if (UsesIndexStagger(style) || asSub)
			span = (itemCount > 0) ? (itemCount - 1) : 0;
		else
			span = max(origin, itemCount - 1 - origin);
		return span * stag + dur + 40;
	}

	static void EnsurePopupClass()
	{
		static BOOL s_reg = FALSE;
		if (s_reg) return;
		WNDCLASS wc;
		ZeroMemory(&wc, sizeof(wc));
		wc.style = CS_DBLCLKS | CS_SAVEBITS | CS_DROPSHADOW;
		wc.lpfnWndProc = AfxWndProc;
		wc.hInstance = AfxGetInstanceHandle();
		wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
		wc.lpszClassName = L"CCustomPopupMenuClass";
		AfxRegisterClass(&wc);
		// 行チップ飛行は透明余白が大きい。DROPSHADOWが右端黒バーになるので無し
		wc.style = CS_DBLCLKS | CS_SAVEBITS;
		wc.lpszClassName = L"CCustomPopupMenuChipClass";
		AfxRegisterClass(&wc);
		s_reg = TRUE;
	}

	static COLORREF PopupBg() { return COLOR_DIALOG_BG; }
	static COLORREF PopupHotTop()
	{
		return CCC_IsInwoman() ? RGB(255, 198, 220) : RGB(228, 234, 255);
	}
	static COLORREF PopupHotBot()
	{
		return CCC_IsInwoman() ? RGB(255, 152, 192) : RGB(186, 204, 248);
	}
	static COLORREF PopupText(BOOL enabled)
	{
		return enabled ? RGB(52, 34, 58) : RGB(170, 158, 170);
	}
	static COLORREF PopupBorderLite() { return RGB(255, 250, 253); }
	static COLORREF PopupBorderDark() { return RGB(176, 118, 152); }

	static COLORREF BlendRGB(COLORREF a, COLORREF b, int t)
	{
		const int u = 256 - t;
		return RGB(
			(GetRValue(a) * u + GetRValue(b) * t) >> 8,
			(GetGValue(a) * u + GetGValue(b) * t) >> 8,
			(GetBValue(a) * u + GetBValue(b) * t) >> 8);
	}

	static void FillVGrad(CDC& dc, const CRect& r, COLORREF c0, COLORREF c1)
	{
		const int h = r.Height();
		if (h <= 0 || r.Width() <= 0) return;
		for (int y = 0; y < h; ++y) {
			const int t = (h <= 1) ? 0 : (y * 256) / (h - 1);
			dc.FillSolidRect(r.left, r.top + y, r.Width(), 1, BlendRGB(c0, c1, t));
		}
	}

	static void FillHGrad(CDC& dc, const CRect& r, COLORREF c0, COLORREF c1)
	{
		const int w = r.Width();
		if (w <= 0 || r.Height() <= 0) return;
		for (int x = 0; x < w; ++x) {
			const int t = (w <= 1) ? 0 : (x * 256) / (w - 1);
			dc.FillSolidRect(r.left + x, r.top, 1, r.Height(), BlendRGB(c0, c1, t));
		}
	}

	static void DrawSoftShadowText(CDC& dc, LPCTSTR text, CRect tr, UINT dt, COLORREF main, BOOL enabled)
	{
		if (!text || !text[0]) return;
		if (enabled) {
			CRect d = tr; d.OffsetRect(1, 1);
			dc.SetTextColor(RGB(210, 170, 190));
			dc.DrawText(text, -1, &d, dt);
			CRect w = tr; w.OffsetRect(-1, -1);
			dc.SetTextColor(RGB(255, 255, 255));
			dc.DrawText(text, -1, &w, dt);
		}
		dc.SetTextColor(main);
		dc.DrawText(text, -1, &tr, dt);
	}
	// fade 中は不透明な影/白縁を出さない（消えない文字の原因）
	static void DrawPopupItemText(CDC& dc, LPCTSTR text, CRect tr, UINT dt, COLORREF main, BOOL enabled, int fade)
	{
		if (!text || !text[0]) return;
		if (fade < 20) return;
		if (fade >= 220)
			DrawSoftShadowText(dc, text, tr, dt, main, enabled);
		else {
			dc.SetTextColor(main);
			dc.DrawText(text, -1, &tr, dt);
		}
	}

	// アニメ中（子HWNDは隠す）でもスライダー等が見えるよう代理描画
	static void DrawInteractiveProxy(CDC& dc, const CCustomPopupItem& it, const CRect& vr, int fade, COLORREF bgRef, UINT dpi)
	{
		auto faded = [&](COLORREF c) -> COLORREF {
			if (fade >= 250) return c;
			return BlendRGB(bgRef, c, fade);
		};
		const int padX = PopupSx(dpi, CCUSTOM_POPUP_PAD_X);
		const int padR = PopupSx(dpi, CCUSTOM_POPUP_PAD_RIGHT);
		const int checkW = PopupSx(dpi, CCUSTOM_POPUP_CHECK_W);
		CRect box = vr;
		if (it.kind == CCUSTOM_POPUP_BUTTON)
			box.DeflateRect(padX + checkW, PopupSx(dpi, 6), padR, PopupSx(dpi, 6));
		else
			box.DeflateRect(padX + checkW, PopupSx(dpi, 20), padR, PopupSx(dpi, 5));
		if (box.Width() < 8 || box.Height() < 4) return;

		const COLORREF face = faded(RGB(255, 248, 252));
		const COLORREF edge = faded(RGB(220, 160, 190));
		const COLORREF accent = faded(RGB(236, 130, 178));
		const COLORREF track = faded(RGB(230, 200, 215));

		if (it.kind == CCUSTOM_POPUP_BUTTON) {
			FillVGrad(dc, box, face, faded(RGB(255, 230, 242)));
			dc.Draw3dRect(&box, RGB(255, 255, 255), edge);
			DrawPopupItemText(dc, it.text, box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
				faded(PopupText(TRUE)), TRUE, fade);
			return;
		}
		if (it.kind == CCUSTOM_POPUP_EDIT || it.kind == CCUSTOM_POPUP_COMBO || it.kind == CCUSTOM_POPUP_LIST) {
			dc.FillSolidRect(&box, face);
			dc.Draw3dRect(&box, edge, edge);
			if (it.kind == CCUSTOM_POPUP_COMBO) {
				CRect ar = box;
				ar.left = max(box.left + PopupSx(dpi, 4), box.right - PopupSx(dpi, 18));
				dc.FillSolidRect(&ar, faded(RGB(255, 236, 245)));
				DrawPopupItemText(dc, L"▾", ar, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
					faded(PopupText(TRUE)), TRUE, fade);
			}
			return;
		}
		if (it.kind == CCUSTOM_POPUP_PROGRESS) {
			dc.FillSolidRect(&box, track);
			dc.Draw3dRect(&box, edge, edge);
			if (it.sliderMax > it.sliderMin) {
				int w = MulDiv(it.sliderPos - it.sliderMin, box.Width(), it.sliderMax - it.sliderMin);
				if (w > 0) {
					CRect fr(box.left, box.top, box.left + w, box.bottom);
					fr.DeflateRect(1, 1);
					if (fr.Width() > 0)
						FillVGrad(dc, fr, faded(RGB(255, 180, 210)), accent);
				}
			}
			return;
		}
		// SLIDER / RANGE
		const int cy = (box.top + box.bottom) / 2;
		const int trackH = max(2, PopupSx(dpi, 4));
		const int thumbR = PopupSx(dpi, 6);
		const int thumbH = PopupSx(dpi, 8);
		dc.FillSolidRect(box.left, cy - trackH / 2, box.Width(), trackH, track);
		dc.Draw3dRect(CRect(box.left, cy - trackH / 2, box.right, cy + trackH / 2), edge, edge);
		if (it.kind == CCUSTOM_POPUP_RANGE && it.sliderMax > it.sliderMin) {
			int x0 = box.left + MulDiv(it.rangeSelMin - it.sliderMin, box.Width(), it.sliderMax - it.sliderMin);
			int x1 = box.left + MulDiv(it.rangeSelMax - it.sliderMin, box.Width(), it.sliderMax - it.sliderMin);
			if (x1 < x0) { const int tmp = x0; x0 = x1; x1 = tmp; }
			dc.FillSolidRect(x0, cy - trackH / 2, max(1, x1 - x0), trackH, faded(RGB(255, 190, 215)));
		}
		if (it.sliderMax > it.sliderMin) {
			const int x = box.left + MulDiv(it.sliderPos - it.sliderMin, box.Width(), it.sliderMax - it.sliderMin);
			CRect th(x - thumbR, cy - thumbH, x + thumbR, cy + thumbH);
			CBrush br(accent);
			CPen pen(PS_SOLID, 1, edge);
			CBrush* ob = dc.SelectObject(&br);
			CPen* op = dc.SelectObject(&pen);
			dc.Ellipse(&th);
			dc.SelectObject(ob);
			dc.SelectObject(op);
		}
	}

	static void DrawCuteSep(CDC& dc, const CRect& rc, UINT dpi)
	{
		const int y = (rc.top + rc.bottom) / 2;
		const int L = rc.left + PopupSx(dpi, CCUSTOM_POPUP_PAD_X) + PopupSx(dpi, CCUSTOM_POPUP_CHECK_W);
		const int R = rc.right - PopupSx(dpi, CCUSTOM_POPUP_PAD_RIGHT);
		const int mid = (L + R) / 2;
		const int gap = PopupSx(dpi, 14);
		FillHGrad(dc, CRect(L, y, mid - gap, y + 1), RGB(255, 246, 250), RGB(228, 150, 186));
		FillHGrad(dc, CRect(mid + gap, y, R, y + 1), RGB(228, 150, 186), RGB(255, 246, 250));
		dc.FillSolidRect(L, y + 1, mid - gap - L, 1, RGB(255, 255, 255));
		dc.FillSolidRect(mid + gap, y + 1, R - (mid + gap), 1, RGB(255, 255, 255));
		CBrush brA(RGB(240, 180, 210));
		CBrush brB(RGB(236, 130, 178));
		CPen pen(PS_SOLID, 1, RGB(255, 240, 248));
		CPen* op = dc.SelectObject(&pen);
		CBrush* ob = dc.SelectObject(&brA);
		dc.Ellipse(mid - 10, y - 2, mid - 5, y + 3);
		dc.Ellipse(mid + 6, y - 2, mid + 11, y + 3);
		dc.SelectObject(&brB);
		dc.Ellipse(mid - 4, y - 4, mid + 5, y + 5);
		dc.SetPixel(mid - 1, y - 1, RGB(255, 255, 255));
		dc.SelectObject(ob);
		dc.SelectObject(op);
	}

	static void DrawJkBackdrop(CDC& dc, const CRect& rc, int /*animTick*/)
	{
		const COLORREF c0 = CCC_IsInwoman() ? RGB(255, 220, 236) : RGB(255, 232, 244);
		const COLORREF c1 = CCC_IsInwoman() ? RGB(255, 192, 224) : RGB(232, 214, 255);
		FillVGrad(dc, rc, c0, c1);

		const int L = rc.left + CCUSTOM_POPUP_RIBBON_W;
		const int R = rc.right;
		const int T = rc.top;
		const int B = rc.bottom;
		if (R <= L || B <= T) return;

		// 斜め模様タイル（一度だけ生成）を BitBlt で流す → 滑らか＆軽い
		enum { kTile = 56 };
		static CBitmap s_tile;
		static COLORREF s_key0 = 0, s_key1 = 0;
		if (!s_tile.GetSafeHandle() || s_key0 != c0 || s_key1 != c1) {
			if (s_tile.GetSafeHandle()) s_tile.DeleteObject();
			CDC mem; mem.CreateCompatibleDC(&dc);
			s_tile.CreateCompatibleBitmap(&dc, kTile, kTile);
			CBitmap* ob = mem.SelectObject(&s_tile);
			mem.FillSolidRect(0, 0, kTile, kTile, c0);
			const COLORREF hi = BlendRGB(RGB(255, 255, 255), c0, 48);
			const int pitch = 28;
			const int stripeW = 10;
			for (int y = 0; y < kTile; ++y) {
				for (int x = 0; x < kTile; ++x) {
					const int v = (x + y) % pitch;
					if (v < stripeW)
						mem.SetPixel(x, y, hi);
					else if ((x % 22) == 8 && (y % 22) == 8)
						mem.SetPixel(x, y, RGB(255, 255, 255));
				}
			}
			// うっすら縦グラデを乗せるため下辺を少し濃く
			for (int y = kTile / 2; y < kTile; ++y) {
				const int t = ((y - kTile / 2) * 40) / (kTile / 2);
				for (int x = 0; x < kTile; x += 2) {
					COLORREF p = mem.GetPixel(x, y);
					mem.SetPixel(x, y, BlendRGB(p, c1, t));
				}
			}
			mem.SelectObject(ob);
			s_key0 = c0; s_key1 = c1;
		}

		// 半分速：66ms で 1px（時間ベースでフレーム落ちても連続）
		const int shift = (int)((::GetTickCount64() / 66) % (ULONGLONG)kTile);
		CDC tileDC; tileDC.CreateCompatibleDC(&dc);
		CBitmap* ob = tileDC.SelectObject(&s_tile);
		for (int y = T - shift; y < B; y += kTile) {
			for (int x = L - shift; x < R; x += kTile) {
				const int dx = max(x, L);
				const int dy = max(y, T);
				const int sx = dx - x;
				const int sy = dy - y;
				const int dw = min(kTile - sx, R - dx);
				const int dh = min(kTile - sy, B - dy);
				if (dw > 0 && dh > 0)
					dc.BitBlt(dx, dy, dw, dh, &tileDC, sx, sy, SRCCOPY);
			}
		}
		tileDC.SelectObject(ob);

		for (int i = 0; i < 12; ++i) {
			const int a = 70 - i * 5;
			if (a <= 0) break;
			dc.FillSolidRect(L, T + i, R - L, 1, BlendRGB(RGB(255, 255, 255), c0, 256 - a));
		}
	}

	static void DrawTornRibbon(CDC& dc, const CRect& rcClient, int animTick)
	{
		const int w = CCUSTOM_POPUP_RIBBON_W;
		const COLORREF c0 = CCC_IsInwoman() ? RGB(255, 108, 168) : RGB(158, 140, 228);
		const COLORREF c1 = CCC_IsInwoman() ? RGB(255, 186, 214) : RGB(208, 198, 255);
		POINT pts[14];
		pts[0].x = rcClient.left; pts[0].y = rcClient.top;
		pts[1].x = rcClient.left + w - 1; pts[1].y = rcClient.top;
		const int segH = max(6, rcClient.Height() / 10);
		int yi = rcClient.top;
		int pi = 2;
		BOOL out = TRUE;
		while (yi < rcClient.bottom - 2 && pi < 12) {
			yi += segH;
			if (yi > rcClient.bottom) yi = rcClient.bottom;
			pts[pi].x = rcClient.left + w - (out ? 1 : 4);
			pts[pi].y = yi;
			++pi;
			out = !out;
		}
		pts[pi].x = rcClient.left; pts[pi].y = rcClient.bottom; ++pi;
		CRgn rgn;
		if (rgn.CreatePolygonRgn(pts, pi, WINDING)) {
			CBrush br(c0);
			dc.FillRgn(&rgn, &br);
			for (int y = rcClient.top; y < rcClient.bottom; ++y) {
				const int t = ((y - rcClient.top) * 256) / max(1, rcClient.Height());
				const int shimmer = ((animTick * 40) + y * 3) & 63;
				COLORREF c = BlendRGB(c0, c1, t);
				if (shimmer < 12)
					c = BlendRGB(c, RGB(255, 255, 255), 50);
				dc.FillSolidRect(rcClient.left + 1, y, w - 4, 1, c);
			}
		} else {
			FillVGrad(dc, CRect(rcClient.left, rcClient.top, rcClient.left + w, rcClient.bottom), c0, c1);
		}
		dc.FillSolidRect(rcClient.left, rcClient.top, 1, rcClient.Height(), RGB(130, 70, 120));
		const int band = max(12, rcClient.Height() / 6);
		for (int i = 0; i < band; ++i) {
			const int a = 110 - (i * 110) / max(1, band);
			dc.FillSolidRect(rcClient.left + 1, rcClient.top + i, w - 3, 1,
				BlendRGB(RGB(255, 255, 255), c1, 256 - a));
		}
		CPen fold(PS_SOLID, 1, BlendRGB(c0, RGB(255, 255, 255), 120));
		CPen* op = dc.SelectObject(&fold);
		const int fy = rcClient.top + band + 2 + (animTick % 3);
		dc.MoveTo(rcClient.left + 1, fy);
		dc.LineTo(rcClient.left + w - 2, fy + 7);
		dc.MoveTo(rcClient.left + 1, fy + 9);
		dc.LineTo(rcClient.left + w - 2, fy + 16);
		dc.SelectObject(op);
	}

	// CCustomControl 由来の赤いレ点（COLOR_CHECK）
	static void DrawRedCheck(CDC& dc, const CRect& cr)
	{
		const int thick = max(2, min(cr.Width(), cr.Height()) / 5);
		const int x1 = cr.left + cr.Width() * 12 / 100, y1 = cr.top + cr.Height() * 54 / 100;
		const int x2 = cr.left + cr.Width() * 40 / 100, y2 = cr.top + cr.Height() * 82 / 100;
		const int x3 = cr.left + cr.Width() * 92 / 100, y3 = cr.top + cr.Height() * 14 / 100;
		CPen psh(PS_SOLID, thick, RGB(180, 40, 90));
		CPen* op = dc.SelectObject(&psh);
		dc.MoveTo(x1, y1 + 1); dc.LineTo(x2, y2 + 1); dc.LineTo(x3, y3 + 1);
		LOGBRUSH lb = { BS_SOLID, COLOR_CHECK, 0 };
		CPen pc;
		if (pc.CreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, thick, &lb)) {
			dc.SelectObject(&pc);
			dc.MoveTo(x1, y1); dc.LineTo(x2, y2); dc.LineTo(x3, y3);
		} else {
			CPen solid(PS_SOLID, thick, COLOR_CHECK);
			dc.SelectObject(&solid);
			dc.MoveTo(x1, y1); dc.LineTo(x2, y2); dc.LineTo(x3, y3);
		}
		dc.SelectObject(op);
	}

	static void DrawHotPill(CDC& dc, const CRect& itemRc)
	{
		CRect hr = itemRc;
		hr.DeflateRect(4, 2, 6, 2);
		FillVGrad(dc, hr, PopupHotTop(), PopupHotBot());
		dc.Draw3dRect(&hr, RGB(255, 255, 255), BlendRGB(PopupHotBot(), RGB(120, 90, 150), 100));
		CRect inn = hr; inn.DeflateRect(1, 1);
		dc.Draw3dRect(&inn, BlendRGB(PopupHotTop(), RGB(255, 255, 255), 80),
			BlendRGB(PopupHotBot(), RGB(160, 120, 160), 40));
		const int cy = (hr.top + hr.bottom) / 2;
		CBrush br(CCC_IsInwoman() ? RGB(255, 80, 150) : RGB(120, 110, 210));
		CBrush* ob = dc.SelectObject(&br);
		dc.Ellipse(hr.left + 5, cy - 3, hr.left + 12, cy + 4);
		dc.SelectObject(ob);
	}

	static void DrawPanelChrome(CDC& dc, const CRect& rc)
	{
		// 枠のみ（背景の一枚感を壊さない）
		dc.Draw3dRect(&rc, PopupBorderLite(), PopupBorderDark());
		CRect inn = rc; inn.DeflateRect(1, 1);
		dc.Draw3dRect(&inn, RGB(255, 255, 255), BlendRGB(PopupBorderDark(), PopupBg(), 150));
	}

	// BigBang: 行ごとに独立したチップ（背景＋リボン＋枠）。矩形を不透明で密閉（クロマ穴を作らない）
	static void DrawRowChip(CDC& dc, const CRect& chip, int animTick, int fade)
	{
		if (chip.Width() <= 1 || chip.Height() <= 1 || fade < 8) return;
		const int clip = dc.SaveDC();
		dc.IntersectClipRect(&chip);
		// 下地を必ずベタ塗り（グラデ穴・キー混入防止）
		dc.FillSolidRect(&chip, PopupBg());
		if (fade >= 220) {
			DrawJkBackdrop(dc, chip, animTick);
			DrawTornRibbon(dc, chip, animTick);
		} else {
			const COLORREF c0 = CCC_IsInwoman() ? RGB(255, 220, 236) : RGB(255, 232, 244);
			const COLORREF c1 = CCC_IsInwoman() ? RGB(255, 192, 224) : RGB(232, 214, 255);
			FillVGrad(dc, chip, BlendRGB(PopupBg(), c0, fade), BlendRGB(PopupBg(), c1, fade));
			CRect rib(chip.left, chip.top, chip.left + CCUSTOM_POPUP_RIBBON_W, chip.bottom);
			const COLORREF r0 = CCC_IsInwoman() ? RGB(255, 108, 168) : RGB(158, 140, 228);
			dc.FillSolidRect(&rib, BlendRGB(PopupBg(), r0, fade));
		}
		DrawPanelChrome(dc, chip);
		dc.RestoreDC(clip);
	}

	static void FontSizeCb(void* ctx, int value)
	{
		CCustomPopupMenu* menu = (CCustomPopupMenu*)ctx;
		if (!menu) return;
		savedata.popupMenuPoint = value;
		ClampPopupFontSave();
		menu->PersistPopupFont();
		menu->RefreshFontChain();
		menu->RelayoutOpenChain();
	}
}

CCustomPopupMenu::CCustomPopupMenu()
	: m_itemCount(0), m_subCount(0), m_sliderCount(0), m_editCount(0)
	, m_comboCount(0), m_listCount(0), m_rangeCount(0), m_progressCount(0), m_buttonCount(0)
	, m_choiceSetCount(0)
	, m_bAeroMode(FALSE), m_tracking(FALSE), m_done(FALSE), m_result(0)
	, m_hot(-1), m_openSub(-1), m_owner(NULL), m_parentMenu(NULL), m_root(NULL)
	, m_tipHot(-1), m_memW(0), m_memH(0), m_font(NULL), m_fontOwned(NULL)
	, m_menuW(0), m_menuH(0), m_contentH(0), m_scrollY(0), m_scrollMax(0)
	, m_stickyCount(0), m_stickyH(0)
	, m_asSubmenu(FALSE), m_animTick(0), m_lineAnimPhase(0), m_lineAnimStart(0)
	, m_lineAnimOrigin(0), m_lineAnimOriginY(0), m_flightPad(0), m_bridgePanel(FALSE)
	, m_skipChrome(FALSE), m_chromeInjected(FALSE), m_previewing(FALSE)
	, m_bounceIdx(-1), m_nBounce(0), m_suppressEditNotify(FALSE)
{
	ZeroMemory(m_items, sizeof(m_items));
	ZeroMemory(m_subs, sizeof(m_subs));
	ZeroMemory(m_choiceSets, sizeof(m_choiceSets));
	m_previewFace[0] = 0;
}

CCustomPopupMenu::~CCustomPopupMenu()
{
	Reset();
	if (m_fontOwned) { ::DeleteObject(m_fontOwned); m_fontOwned = NULL; }
	if (m_memBmp.GetSafeHandle()) m_memBmp.DeleteObject();
	if (GetSafeHwnd()) DestroyWindow();
}

void CCustomPopupMenu::Reset()
{
	DestroyPopupTree();
	for (int i = 0; i < m_subCount; ++i) {
		if (m_subs[i]) { delete m_subs[i]; m_subs[i] = NULL; }
	}
	m_itemCount = m_subCount = m_sliderCount = m_editCount = 0;
	m_comboCount = m_listCount = m_rangeCount = m_progressCount = m_buttonCount = 0;
	m_choiceSetCount = 0;
	m_hot = m_openSub = -1; m_result = 0; m_done = FALSE; m_tipHot = -1;
	m_chromeInjected = FALSE; m_previewing = FALSE; m_previewFace[0] = 0;
	m_scrollY = m_scrollMax = m_contentH = 0;
	m_stickyCount = m_stickyH = 0;
	m_bounceIdx = -1; m_nBounce = 0;
	ZeroMemory(m_items, sizeof(m_items));
	ZeroMemory(m_choiceSets, sizeof(m_choiceSets));
}

void CCustomPopupMenu::CopyText(wchar_t* dst, int dstN, LPCTSTR src)
{
	if (!dst || dstN <= 0) return;
	dst[0] = 0;
	if (!src) return;
	lstrcpynW(dst, src, dstN);
}

BOOL CCustomPopupMenu::IsInteractiveKind(int kind) const
{
	return kind == CCUSTOM_POPUP_SLIDER || kind == CCUSTOM_POPUP_EDIT
		|| kind == CCUSTOM_POPUP_COMBO || kind == CCUSTOM_POPUP_LIST
		|| kind == CCUSTOM_POPUP_RANGE || kind == CCUSTOM_POPUP_PROGRESS
		|| kind == CCUSTOM_POPUP_BUTTON;
}

BOOL CCustomPopupMenu::IsChromeCommand(UINT id) const
{
	return id == CCUSTOM_POPUP_ID_ACRYLIC
		|| id == CCUSTOM_POPUP_ID_FONT_BOLD
		|| id == CCUSTOM_POPUP_ID_FONT_ITALIC
		|| id == CCUSTOM_POPUP_ID_FONT_FACE
		|| (id >= CCUSTOM_POPUP_ID_ANIM0
			&& id < CCUSTOM_POPUP_ID_ANIM0 + (UINT)POPUP_ANIM_COUNT);
}

BOOL CCustomPopupMenu::AddItemBase(int kind, UINT id, LPCTSTR text, LPCTSTR tip, BOOL enabled, BOOL checked)
{
	if (m_itemCount >= CCUSTOM_POPUP_MAX_ITEMS) return FALSE;
	CCustomPopupItem& it = m_items[m_itemCount++];
	ZeroMemory(&it, sizeof(it));
	it.kind = kind; it.id = id; it.enabled = enabled; it.checked = checked;
	it.subIndex = it.sliderIndex = it.editIndex = it.comboIndex = it.listIndex = it.choiceSet = -1;
	it.rangeIndex = it.progressIndex = it.buttonIndex = -1;
	it.rangeSelMin = it.rangeSelMax = 0;
	it.rangeAbA = it.rangeAbB = -1;
	it.progressShowPct = TRUE;
	it.buttonCloseOnClick = TRUE;
	it.rangeCb = NULL; it.buttonCb = NULL;
	CopyText(it.text, CCUSTOM_POPUP_TEXT_LEN, text);
	if (tip && tip[0]) { CopyText(it.tip, CCUSTOM_POPUP_TIP_LEN, tip); it.hasTip = TRUE; }
	return TRUE;
}

BOOL CCustomPopupMenu::AddCommand(UINT id, LPCTSTR text, LPCTSTR tip, BOOL enabled)
{ return AddItemBase(CCUSTOM_POPUP_CMD, id, text, tip, enabled, FALSE); }

BOOL CCustomPopupMenu::AddCheck(UINT id, LPCTSTR text, BOOL checked, LPCTSTR tip, BOOL enabled)
{ return AddItemBase(CCUSTOM_POPUP_CHECK, id, text, tip, enabled, checked); }

BOOL CCustomPopupMenu::AddSeparator()
{ return AddItemBase(CCUSTOM_POPUP_SEP, 0, NULL, NULL, TRUE, FALSE); }

CCustomPopupMenu* CCustomPopupMenu::AddSubMenu(LPCTSTR text, LPCTSTR tip)
{
	if (m_subCount >= CCUSTOM_POPUP_MAX_SUBS || m_itemCount >= CCUSTOM_POPUP_MAX_ITEMS) return NULL;
	CCustomPopupMenu* sub = new CCustomPopupMenu();
	if (!sub) return NULL;
	const int si = m_subCount;
	m_subs[si] = sub; ++m_subCount;
	if (!AddItemBase(CCUSTOM_POPUP_SUB, 0, text, tip, TRUE, FALSE)) {
		delete sub; m_subs[si] = NULL; --m_subCount; return NULL;
	}
	m_items[m_itemCount - 1].subIndex = si;
	return sub;
}

BOOL CCustomPopupMenu::AddSlider(LPCTSTR label, int vmin, int vmax, int vpos,
	CCustomPopupSliderCb cb, void* ctx, LPCTSTR tip, UINT id)
{
	if (m_sliderCount >= CCUSTOM_POPUP_MAX_SLIDERS) return FALSE;
	if (!AddItemBase(CCUSTOM_POPUP_SLIDER, id, label, tip, TRUE, FALSE)) return FALSE;
	CCustomPopupItem& it = m_items[m_itemCount - 1];
	if (vmin > vmax) { const int t = vmin; vmin = vmax; vmax = t; }
	if (vpos < vmin) vpos = vmin; if (vpos > vmax) vpos = vmax;
	it.sliderMin = vmin; it.sliderMax = vmax; it.sliderPos = vpos;
	it.sliderCb = cb; it.ctrlCtx = ctx; it.sliderIndex = m_sliderCount++;
	return TRUE;
}

int CCustomPopupMenu::AllocChoiceSet(const LPCTSTR* items, int count)
{
	if (!items || count <= 0) return -1;
	if (m_choiceSetCount >= CCUSTOM_POPUP_MAX_CHOICE_SETS) return -1;
	CCustomPopupChoiceSet& set = m_choiceSets[m_choiceSetCount];
	ZeroMemory(&set, sizeof(set));
	const int n = min(count, (int)CCUSTOM_POPUP_MAX_CHOICES);
	for (int i = 0; i < n; ++i) CopyText(set.items[i], CCUSTOM_POPUP_CHOICE_LEN, items[i]);
	set.count = n;
	return m_choiceSetCount++;
}

BOOL CCustomPopupMenu::AddEdit(LPCTSTR label, LPCTSTR initial, CCustomPopupEditCb cb, void* ctx, LPCTSTR tip, UINT id)
{
	if (m_editCount >= CCUSTOM_POPUP_MAX_EDITS) return FALSE;
	LPCTSTR one[1] = { initial ? initial : L"" };
	const int cs = AllocChoiceSet(one, 1);
	if (cs < 0) return FALSE;
	if (!AddItemBase(CCUSTOM_POPUP_EDIT, id, label, tip, TRUE, FALSE)) { --m_choiceSetCount; return FALSE; }
	CCustomPopupItem& it = m_items[m_itemCount - 1];
	it.editCb = cb; it.ctrlCtx = ctx; it.choiceSet = cs; it.editIndex = m_editCount++;
	return TRUE;
}

BOOL CCustomPopupMenu::AddCombo(LPCTSTR label, const LPCTSTR* items, int count, int curSel,
	CCustomPopupChoiceCb cb, void* ctx, LPCTSTR tip, UINT id)
{
	if (m_comboCount >= CCUSTOM_POPUP_MAX_COMBOS) return FALSE;
	const int cs = AllocChoiceSet(items, count);
	if (cs < 0) return FALSE;
	if (!AddItemBase(CCUSTOM_POPUP_COMBO, id, label, tip, TRUE, FALSE)) { --m_choiceSetCount; return FALSE; }
	CCustomPopupItem& it = m_items[m_itemCount - 1];
	it.choiceCb = cb; it.ctrlCtx = ctx; it.choiceSet = cs; it.choiceSel = curSel;
	if (it.choiceSel < 0) it.choiceSel = 0;
	if (it.choiceSel >= m_choiceSets[cs].count) it.choiceSel = m_choiceSets[cs].count - 1;
	it.comboIndex = m_comboCount++;
	return TRUE;
}

BOOL CCustomPopupMenu::AddList(LPCTSTR label, const LPCTSTR* items, int count, int curSel,
	CCustomPopupChoiceCb cb, void* ctx, LPCTSTR tip, UINT id)
{
	if (m_listCount >= CCUSTOM_POPUP_MAX_LISTS) return FALSE;
	const int cs = AllocChoiceSet(items, count);
	if (cs < 0) return FALSE;
	if (!AddItemBase(CCUSTOM_POPUP_LIST, id, label, tip, TRUE, FALSE)) { --m_choiceSetCount; return FALSE; }
	CCustomPopupItem& it = m_items[m_itemCount - 1];
	it.choiceCb = cb; it.ctrlCtx = ctx; it.choiceSet = cs; it.choiceSel = curSel;
	if (it.choiceSel < 0) it.choiceSel = 0;
	if (it.choiceSel >= m_choiceSets[cs].count) it.choiceSel = m_choiceSets[cs].count - 1;
	it.listIndex = m_listCount++;
	return TRUE;
}

BOOL CCustomPopupMenu::AddRangeSlider(LPCTSTR label, int vmin, int vmax, int vpos,
	int selMin, int selMax, int abA, int abB,
	CCustomPopupRangeCb cb, void* ctx, LPCTSTR tip, UINT id)
{
	if (m_rangeCount >= CCUSTOM_POPUP_MAX_RANGES) return FALSE;
	if (!AddItemBase(CCUSTOM_POPUP_RANGE, id, label, tip, TRUE, FALSE)) return FALSE;
	CCustomPopupItem& it = m_items[m_itemCount - 1];
	if (vmin > vmax) { const int t = vmin; vmin = vmax; vmax = t; }
	if (vpos < vmin) vpos = vmin; if (vpos > vmax) vpos = vmax;
	if (selMin > selMax) { const int t = selMin; selMin = selMax; selMax = t; }
	if (selMin < vmin) selMin = vmin; if (selMax > vmax) selMax = vmax;
	it.sliderMin = vmin; it.sliderMax = vmax; it.sliderPos = vpos;
	it.rangeSelMin = selMin; it.rangeSelMax = selMax;
	it.rangeAbA = abA; it.rangeAbB = abB;
	it.rangeCb = cb; it.ctrlCtx = ctx; it.rangeIndex = m_rangeCount++;
	return TRUE;
}

BOOL CCustomPopupMenu::AddProgress(LPCTSTR label, int vmin, int vmax, int vpos,
	BOOL showPercent, LPCTSTR tip, UINT id)
{
	if (m_progressCount >= CCUSTOM_POPUP_MAX_PROGRESSES) return FALSE;
	if (!AddItemBase(CCUSTOM_POPUP_PROGRESS, id, label, tip, TRUE, FALSE)) return FALSE;
	CCustomPopupItem& it = m_items[m_itemCount - 1];
	if (vmin > vmax) { const int t = vmin; vmin = vmax; vmax = t; }
	if (vpos < vmin) vpos = vmin; if (vpos > vmax) vpos = vmax;
	it.sliderMin = vmin; it.sliderMax = vmax; it.sliderPos = vpos;
	it.progressShowPct = showPercent ? TRUE : FALSE;
	it.progressIndex = m_progressCount++;
	return TRUE;
}

BOOL CCustomPopupMenu::AddButton(UINT id, LPCTSTR text,
	CCustomPopupButtonCb cb, void* ctx, LPCTSTR tip, BOOL closeOnClick)
{
	if (m_buttonCount >= CCUSTOM_POPUP_MAX_BUTTONS) return FALSE;
	if (!AddItemBase(CCUSTOM_POPUP_BUTTON, id, text, tip, TRUE, FALSE)) return FALSE;
	CCustomPopupItem& it = m_items[m_itemCount - 1];
	it.buttonCb = cb; it.ctrlCtx = ctx;
	it.buttonCloseOnClick = closeOnClick ? TRUE : FALSE;
	it.buttonIndex = m_buttonCount++;
	return TRUE;
}

void CCustomPopupMenu::PersistPopupFont()
{
	ClampPopupFontSave();
	MpPersistSavedataQuick();
}

void CCustomPopupMenu::RebuildMenuFont()
{
	if (m_fontOwned) { ::DeleteObject(m_fontOwned); m_fontOwned = NULL; }
	ClampPopupFontSave();
	LOGFONTW lf;
	ZeroMemory(&lf, sizeof(lf));
	HDC hdc = ::GetDC(NULL);
	const int py = ::GetDeviceCaps(hdc, LOGPIXELSY);
	::ReleaseDC(NULL, hdc);
	lf.lfHeight = -MulDiv(savedata.popupMenuPoint, py, 72);
	lf.lfWeight = savedata.popupMenuBold ? FW_BOLD : FW_NORMAL;
	lf.lfItalic = savedata.popupMenuItalic ? 1 : 0;
	lf.lfCharSet = DEFAULT_CHARSET;
	lf.lfQuality = CLEARTYPE_QUALITY;
	if (m_previewing && m_previewFace[0])
		lstrcpynW(lf.lfFaceName, m_previewFace, LF_FACESIZE);
	else if (savedata.popupMenuFace[0])
		lstrcpynW(lf.lfFaceName, savedata.popupMenuFace, LF_FACESIZE);
	else
		lstrcpynW(lf.lfFaceName, L"Segoe UI", LF_FACESIZE);
	m_fontOwned = ::CreateFontIndirectW(&lf);
	m_font = m_fontOwned ? m_fontOwned : (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
}

void CCustomPopupMenu::RefreshFontChain()
{
	CCustomPopupMenu* root = RootMenu();
	if (!root) return;
	root->RebuildMenuFont();
	if (root->GetSafeHwnd()) {
		root->InvalidateBgOnly();
		root->RefreshEmbeddedChildren();
	}
	for (int i = 0; i < root->m_subCount; ++i) {
		if (!root->m_subs[i]) continue;
		root->m_subs[i]->m_previewing = root->m_previewing;
		lstrcpynW(root->m_subs[i]->m_previewFace, root->m_previewFace, 32);
		root->m_subs[i]->RebuildMenuFont();
		if (root->m_subs[i]->GetSafeHwnd()) {
			root->m_subs[i]->InvalidateBgOnly();
			root->m_subs[i]->RefreshEmbeddedChildren();
		}
	}
}

void CCustomPopupMenu::RelayoutOpenChain()
{
	CCustomPopupMenu* root = RootMenu();
	if (!root) return;
	for (int pass = 0; pass < 1 + root->m_subCount; ++pass) {
		CCustomPopupMenu* menu = (pass == 0) ? root : root->m_subs[pass - 1];
		if (!menu || !menu->GetSafeHwnd() || !::IsWindowVisible(menu->m_hWnd)) continue;
		CRect wr;
		menu->GetWindowRect(&wr);
		const int pad = menu->m_flightPad;
		menu->MeasureLayout();
		int x = wr.left + pad; // コンテンツ原点（飛行余白を除く）
		int y = wr.top + pad;
		MONITORINFO mi; ZeroMemory(&mi, sizeof(mi)); mi.cbSize = sizeof(mi);
		HMONITOR hMon = ::MonitorFromWindow(menu->m_hWnd, MONITOR_DEFAULTTONEAREST);
		if (hMon && ::GetMonitorInfo(hMon, &mi)) {
			const int maxH = mi.rcWork.bottom - mi.rcWork.top - 8;
			if (menu->m_contentH > maxH) {
				menu->m_menuH = maxH;
			const int stickyMin = PopupSx(PopupDpiFromHwnd(menu->m_hWnd), CCUSTOM_POPUP_ITEM_H) * 3;
			if (menu->m_stickyH > 0 && menu->m_menuH < menu->m_stickyH + stickyMin)
				menu->m_menuH = min(maxH, menu->m_stickyH + stickyMin);
				const int bodyView = max(0, menu->m_menuH - menu->m_stickyH);
				const int bodyContent = max(0, menu->m_contentH - menu->m_stickyH);
				menu->m_scrollMax = max(0, bodyContent - bodyView);
				if (menu->m_scrollY > menu->m_scrollMax) menu->m_scrollY = menu->m_scrollMax;
			} else {
				menu->m_menuH = menu->m_contentH;
				menu->m_scrollMax = 0;
				menu->m_scrollY = 0;
			}
			if (x + menu->m_menuW > mi.rcWork.right) x = mi.rcWork.right - menu->m_menuW;
			if (y + menu->m_menuH > mi.rcWork.bottom) y = mi.rcWork.bottom - menu->m_menuH;
			if (x < mi.rcWork.left) x = mi.rcWork.left;
			if (y < mi.rcWork.top) y = mi.rcWork.top;
		} else {
			menu->m_menuH = menu->m_contentH;
			menu->m_scrollMax = 0;
		}
		menu->SetWindowPos(NULL, x - pad, y - pad, menu->m_menuW + 2 * pad, menu->m_menuH + 2 * pad,
			SWP_NOZORDER | SWP_NOACTIVATE);
		menu->SyncEmbeddedChildren();
		menu->InvalidateBgOnly();
		menu->RefreshEmbeddedChildren();
	}
}

void CCustomPopupMenu::ApplyPreviewFace(LPCTSTR face)
{
	CCustomPopupMenu* root = RootMenu();
	if (!root || !face || !face[0]) return;
	root->m_previewing = TRUE;
	lstrcpynW(root->m_previewFace, face, 32);
	root->RefreshFontChain();
}

void CCustomPopupMenu::ClearPreviewFace()
{
	CCustomPopupMenu* root = RootMenu();
	if (!root || !root->m_previewing) return;
	root->m_previewing = FALSE;
	root->m_previewFace[0] = 0;
	root->RefreshFontChain();
}

void CCustomPopupMenu::CommitFace(LPCTSTR face)
{
	if (!face || !face[0]) return;
	lstrcpynW(savedata.popupMenuFace, face, _countof(savedata.popupMenuFace));
	m_previewing = FALSE;
	m_previewFace[0] = 0;
	PersistPopupFont();
	RefreshFontChain();
}

void CCustomPopupMenu::EnsureChromePrefix()
{
	if (m_skipChrome || m_chromeInjected || m_parentMenu) return;
	m_chromeInjected = TRUE;

	CCustomPopupItem savedItems[CCUSTOM_POPUP_MAX_ITEMS];
	const int savedN = m_itemCount;
	if (savedN > 0)
		memcpy(savedItems, m_items, sizeof(CCustomPopupItem) * savedN);
	m_itemCount = 0;

	CCustomPopupMenu* fontSub = AddSubMenu(
		LL14(L"フォント", L"Font", L"Police", L"Carattere", L"Fuente",
			L"글꼴", L"字体", L"خط", L"Шрифт", L"Schrift",
			L"Fonte", L"Lettertype", L"Czcionka", L"Yazi tipi"),
		LL14(L"メニュー用フォント。ホバーでプレビュー、クリックで確定", L"Menu font. Hover to preview, click to apply",
			L"Police du menu. Survol = apercu, clic = appliquer", L"Font menu. Passa = anteprima, clic = applica",
			L"Fuente del menu. Pasar = vista previa, clic = aplicar", L"메뉴 글꼴. 호버 미리보기, 클릭 확정",
			L"菜单字体。悬停预览，点击确定", L"خط القائمة. مرر للمعاينة، انقر للتأكيد",
			L"Шрифт меню. Наведение — превью, клик — применить", L"Menüschrift. Hover=Vorschau, Klick=Übernehmen",
			L"Fonte do menu. Passe=preview, clique=aplicar", L"Menufont. Hover=voorbeeld, klik=toepassen",
			L"Czcionka menu. Najazd=podglad, klik=zastosuj", L"Menu yazi tipi. Gezdir=onizleme, tikla=uygula"));
	if (fontSub) {
		fontSub->SetSkipChrome(TRUE);
		ClampPopupFontSave();
		fontSub->AddSlider(
			LL14(L"サイズ", L"Size", L"Taille", L"Dimensione", L"Tamano",
				L"크기", L"大小", L"حجم", L"Размер", L"Größe",
				L"Tamanho", L"Grootte", L"Rozmiar", L"Boyut"),
			8, 24, savedata.popupMenuPoint, FontSizeCb, fontSub,
			LL14(L"8–24 pt。ドラッグで即反映", L"8–24 pt. Live while dragging",
				L"8–24 pt. Direct", L"8–24 pt. Live", L"8–24 pt. En vivo",
				L"8–24 pt. 드래그 즉시", L"8–24 pt。拖动即时", L"8–24 نقطة. مباشر",
				L"8–24 pt. Сразу", L"8–24 pt. Live", L"8–24 pt. Ao vivo",
				L"8–24 pt. Live", L"8–24 pt. Na zywo", L"8–24 pt. Anlik"));
		fontSub->AddCheck(CCUSTOM_POPUP_ID_FONT_BOLD,
			LL14(L"太字", L"Bold", L"Gras", L"Grassetto", L"Negrita",
				L"굵게", L"粗体", L"عريض", L"Жирный", L"Fett",
				L"Negrito", L"Vet", L"Pogrubienie", L"Kalin"),
			savedata.popupMenuBold ? TRUE : FALSE);
		fontSub->AddCheck(CCUSTOM_POPUP_ID_FONT_ITALIC,
			LL14(L"斜体", L"Italic", L"Italique", L"Corsivo", L"Cursiva",
				L"기울임", L"斜体", L"مائل", L"Курсив", L"Kursiv",
				L"Italico", L"Cursief", L"Kursywa", L"Italik"),
			savedata.popupMenuItalic ? TRUE : FALSE);
		fontSub->AddSeparator();
		fontSub->SetStickyLeading(4); // サイズ/太字/斜体/セパレータを固定
		EnsureFaceList();
		const int room = CCUSTOM_POPUP_MAX_ITEMS - fontSub->m_itemCount - 1;
		const int n = min(s_faceCount, max(0, room));
		for (int i = 0; i < n; ++i) {
			const BOOL cur = (savedata.popupMenuFace[0]
				&& _wcsicmp(savedata.popupMenuFace, s_faces[i]) == 0) ? TRUE : FALSE;
			if (cur)
				fontSub->AddCheck(CCUSTOM_POPUP_ID_FONT_FACE, s_faces[i], TRUE);
			else
				fontSub->AddCommand(CCUSTOM_POPUP_ID_FONT_FACE, s_faces[i]);
		}
	}

	AddCheck(CCUSTOM_POPUP_ID_ACRYLIC,
		LL14(L"アクリルモード", L"Acrylic mode", L"Mode acrylique", L"Modalita acrilica", L"Modo acrilico",
			L"아크릴 모드", L"亚克力模式", L"وضع الأكريليك", L"Акриловый режим", L"Acrylmodus",
			L"Modo acrilico", L"Acrylmodus", L"Tryb akrylowy", L"Akrilik mod"),
		savedata.aero == 1 ? TRUE : FALSE,
		LL14(L"Win10+ の半透明ぼかし。設定を開かず切替", L"Win10+ translucent blur. Toggle without opening settings",
			L"Flou translucide Win10+. Basculer sans reglages", L"Sfocatura Win10+. Commuta senza impostazioni",
			L"Desenfoque Win10+. Alternar sin ajustes", L"Win10+ 반투명 흐림. 설정 없이 전환",
			L"Win10+ 半透明模糊。无需打开设置即可切换", L"ضبابية Win10+. تبديل دون الإعدادات",
			L"Размытие Win10+. Переключение без настроек", L"Unschärfe Win10+. Umschalten ohne Einstellungen",
			L"Desfoque Win10+. Alternar sem ajustes", L"Vervaging Win10+. Schakelen zonder instellingen",
			L"Rozmycie Win10+. Przelacz bez ustawien", L"Win10+ bulaniklik. Ayar acmadan degistir"));

	ClampPopupAnimSave();
	CCustomPopupMenu* animSub = AddSubMenu(
		LL14(L"メニュー描画方法", L"Menu animation", L"Animation du menu", L"Animazione menu", L"Animacion del menu",
			L"메뉴 표시 방식", L"菜单显示方式", L"طريقة عرض القائمة", L"Отображение меню", L"Menü-Darstellung",
			L"Exibicao do menu", L"Menuweergave", L"Wyswietlanie menu", L"Menu gosterimi"),
		LL14(L"コンテキストメニューの出現／消失アニメ", L"Context menu show/hide animation",
			L"Animation d'apparition du menu", L"Animazione comparsa menu", L"Animacion de aparicion",
			L"컨텍스트 메뉴 등장/퇴장 애니메이션", L"右键菜单出现/消失动画", L"حركة ظهور/اختفاء القائمة",
			L"Анимация появления меню", L"Ein-/Ausblendanimation", L"Animacao de exibicao",
			L"Animatie tonen/verbergen", L"Animacja pojawiania", L"Acilma/kapanma animasyonu"));
	if (animSub) {
		// 描画方法サブも本体と同じ出現アニメを使う（フォントサブだけ skipChrome）
		UINT animIds[POPUP_ANIM_COUNT];
		for (int ai = 0; ai < POPUP_ANIM_COUNT; ++ai)
			animIds[ai] = CCUSTOM_POPUP_ID_ANIM0 + (UINT)ai;
		const wchar_t* names[POPUP_ANIM_COUNT] = {
			LL14(L"クラシック（フェード）", L"Classic (fade)", L"Classique (fondu)", L"Classica (dissolvenza)", L"Clasica (fundido)",
				L"클래식(페이드)", L"经典（淡入）", L"كلاسيكي (تلاشي)", L"Классика (затухание)", L"Klassisch (Fade)",
				L"Classica (fade)", L"Klassiek (fade)", L"Klasyczna (fade)", L"Klasik (fade)"),
			LL14(L"上下に伸びる", L"Expand up/down", L"Expansion haut/bas", L"Espansione su/giu", L"Expandir arriba/abajo",
				L"위아래로 펼침", L"上下展开", L"توسيع لأعلى/أسفل", L"Раскрытие вверх/вниз", L"Auf/Ab aufklappen",
				L"Expandir cima/baixo", L"Uitklappen omhoog/omlaag", L"Rozwin w gore/dol", L"Yukari/asagi acilim"),
			LL14(L"カーテン（上から）", L"Curtain (from top)", L"Rideau (haut)", L"Tendina (alto)", L"Cortina (arriba)",
				L"커튼(위에서)", L"窗帘（从上）", L"ستارة (من الأعلى)", L"Штора (сверху)", L"Vorhang (oben)",
				L"Cortina (cima)", L"Gordijn (boven)", L"Kurtyna (z gory)", L"Perde (yukaridan)"),
			LL14(L"ワイプ（横）", L"Wipe (horizontal)", L"Balayage (horizontal)", L"Wipe (orizzontale)", L"Barrido (horizontal)",
				L"와이프(가로)", L"擦除（横向）", L"مسح (أفقي)", L"Вытеснение (гориз.)", L"Wischen (horizontal)",
				L"Wipe (horizontal)", L"Wipe (horizontaal)", L"Wymazanie (poz.)", L"Silme (yatay)"),
			LL14(L"リップル（起点）", L"Ripple (from click)", L"Ondulation (clic)", L"Increspatura (clic)", L"Onda (clic)",
				L"리플(클릭)", L"涟漪（点击）", L"تموج (نقرة)", L"Рябь (от клика)", L"Wellig (Klick)",
				L"Ondulacao (clique)", L"Rimpel (klik)", L"Fala (klik)", L"Dalga (tik)"),
			LL14(L"ビッグバン／ブラックホール", L"Big Bang / Black Hole", L"Big Bang / Trou noir", L"Big Bang / Buco nero", L"Big Bang / Agujero negro",
				L"빅뱅/블랙홀", L"大爆炸／黑洞", L"الانفجار العظيم / ثقب أسود", L"Большой взрыв / Чёрная дыра", L"Urknall / Schwarzes Loch",
				L"Big Bang / Buraco negro", L"Big Bang / Zwart gat", L"Big Bang / Czarna dziura", L"Buyuk Patlama / Kara Delik"),
			LL14(L"螺旋（スパイラル）", L"Spiral", L"Spirale", L"Spirale", L"Espiral",
				L"스파이럴", L"螺旋", L"حلزوني", L"Спираль", L"Spirale",
				L"Espiral", L"Spiraal", L"Spirala", L"Spiral"),
			LL14(L"花びら", L"Petals", L"Petales", L"Petali", L"Petalos",
				L"꽃잎", L"花瓣", L"بتلات", L"Лепестки", L"Blütenblätter",
				L"Petalas", L"Bloemblaadjes", L"Platki", L"Yapraklar"),
			LL14(L"ジッパー（左右交互）", L"Zipper (L/R)", L"Fermeture (G/D)", L"Cerniera (S/D)", L"Cremallera (I/D)",
				L"지퍼(좌우)", L"拉链（左右）", L"سحاب (يسار/يمين)", L"Молния (Л/П)", L"Reißverschluss (L/R)",
				L"Ziper (E/D)", L"Rits (L/R)", L"Zamek (L/P)", L"Fermuar (S/S)"),
			LL14(L"オーロラ（波）", L"Aurora (wave)", L"Aurore (onde)", L"Aurora (onda)", L"Aurora (onda)",
				L"오로라(파도)", L"极光（波浪）", L"شفق (موجة)", L"Полярное сияние", L"Polarlicht (Welle)",
				L"Aurora (onda)", L"Noorderlicht (golf)", L"Zorza (fala)", L"Aurora (dalga)")
		};
		for (int i = 0; i < POPUP_ANIM_COUNT; ++i) {
			animSub->AddCheck(animIds[i], names[i],
				(savedata.popupMenuAnim == i) ? TRUE : FALSE,
				LL14(L"選ぶと保存し、次に開くメニューから反映します", L"Saves and applies from the next menu open",
					L"Enregistre et s'applique au prochain menu", L"Salva e applica dal prossimo menu", L"Guarda y aplica en el proximo menu",
					L"저장되며 다음 메뉴부터 적용", L"保存后从下次打开菜单起生效", L"يُحفظ ويُطبق من الفتح التالي",
					L"Сохраняется и с следующего открытия", L"Speichert und gilt ab dem nächsten Öffnen",
					L"Salva e aplica no proximo menu", L"Slaat op en geldt vanaf volgende open",
					L"Zapisuje i od kolejnego otwarcia", L"Kaydedilir, sonraki actmadan itibaren"));
		}
	}
	AddSeparator();

	for (int i = 0; i < savedN && m_itemCount < CCUSTOM_POPUP_MAX_ITEMS; ++i)
		m_items[m_itemCount++] = savedItems[i];
}

void CCustomPopupMenu::MeasureLayout()
{
	UINT dpi = PopupDpiFromHwnd(m_hWnd);
	auto SH = [dpi](int v) { return PopupSx(dpi, v); };

	CDC dc; dc.Attach(::GetDC(NULL));
	HFONT font = m_font ? m_font : (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
	HFONT old = (HFONT)dc.SelectObject(font);
	int maxTw = 0;
	for (int i = 0; i < m_itemCount; ++i) {
		const CCustomPopupItem& it = m_items[i];
		if (it.kind == CCUSTOM_POPUP_SEP) continue;
		CSize sz = dc.GetTextExtent(it.text, (int)wcslen(it.text));
		int w = sz.cx;
		// レ点列は常に確保（チェック無し行もラベル位置を揃える）
		w += SH(CCUSTOM_POPUP_CHECK_W);
		if (it.kind == CCUSTOM_POPUP_SUB) w += SH(CCUSTOM_POPUP_ARROW_W);
		if (IsInteractiveKind(it.kind)) w = max(w, SH(160));
		if (w > maxTw) maxTw = w;
	}
	dc.SelectObject(old);
	::ReleaseDC(NULL, dc.Detach());

	m_menuW = max(SH(CCUSTOM_POPUP_MIN_W),
		maxTw + SH(CCUSTOM_POPUP_PAD_X) + SH(CCUSTOM_POPUP_PAD_RIGHT) + SH(CCUSTOM_POPUP_RIBBON_W) + SH(12));
	if (m_stickyCount > m_itemCount) m_stickyCount = m_itemCount;
	int y = SH(8);
	m_stickyH = 0;
	for (int i = 0; i < m_itemCount; ++i) {
		CCustomPopupItem& it = m_items[i];
		// 行高・幅定数とも DPI スケール（描画側と共有）
		int h = SH(CCUSTOM_POPUP_ITEM_H);
		if (it.kind == CCUSTOM_POPUP_SEP) h = SH(CCUSTOM_POPUP_SEP_H);
		else if (it.kind == CCUSTOM_POPUP_SLIDER) h = SH(CCUSTOM_POPUP_SLIDER_H);
		else if (it.kind == CCUSTOM_POPUP_EDIT) h = SH(CCUSTOM_POPUP_EDIT_H);
		else if (it.kind == CCUSTOM_POPUP_COMBO) h = SH(CCUSTOM_POPUP_COMBO_H);
		else if (it.kind == CCUSTOM_POPUP_LIST) h = SH(CCUSTOM_POPUP_LIST_H);
		else if (it.kind == CCUSTOM_POPUP_RANGE) h = SH(CCUSTOM_POPUP_RANGE_H);
		else if (it.kind == CCUSTOM_POPUP_PROGRESS) h = SH(CCUSTOM_POPUP_PROGRESS_H);
		else if (it.kind == CCUSTOM_POPUP_BUTTON) h = SH(CCUSTOM_POPUP_BUTTON_H);
		it.rc.SetRect(SH(CCUSTOM_POPUP_RIBBON_W) + SH(2), y, m_menuW - SH(6), y + h);
		y += h;
		if (i + 1 == m_stickyCount)
			m_stickyH = y;
	}
	m_contentH = y + SH(8);
	m_menuH = m_contentH;
	m_scrollY = 0;
	m_scrollMax = 0;
}

void CCustomPopupMenu::SyncEmbeddedChildren()
{
	UINT dpi = PopupDpiFromHwnd(m_hWnd);
	const int padTop = PopupSx(dpi, 20);
	const int padBot = PopupSx(dpi, 5);
	const int comboEditH = PopupSx(dpi, 22);
	const int comboListH = PopupSx(dpi, 28);
	const int padX = PopupSx(dpi, CCUSTOM_POPUP_PAD_X);
	const int padR = PopupSx(dpi, CCUSTOM_POPUP_PAD_RIGHT);
	const int checkW = PopupSx(dpi, CCUSTOM_POPUP_CHECK_W);

	auto placeChild = [&](CWnd& wnd, const CRect& dest, BOOL onScreen) {
		if (!wnd.GetSafeHwnd()) return;
		CRect old;
		wnd.GetWindowRect(&old);
		ScreenToClient(&old);
		const BOOL moved = (old != dest);
		const BOOL wasVisible = wnd.IsWindowVisible();
		if (moved)
			wnd.MoveWindow(&dest, FALSE);
		if (onScreen) {
			if (!wasVisible)
				wnd.ShowWindow(SW_SHOWNA);
			// 初回表示／移動時だけ描く。毎回 UpdateWindow すると定着直後に点滅する
			if (moved || !wasVisible) {
				wnd.Invalidate(FALSE);
				wnd.UpdateWindow();
			}
		} else if (wasVisible) {
			wnd.ShowWindow(SW_HIDE);
		}
	};

	for (int i = 0; i < m_itemCount; ++i) {
		CCustomPopupItem& it = m_items[i];
		CRect vr = ItemViewRect(i);
		const BOOL sticky = (i < m_stickyCount);
		const int clipTop = sticky ? 0 : m_stickyH;
		CRect body(m_flightPad, m_flightPad + clipTop, m_flightPad + m_menuW, m_flightPad + m_menuH);
		CRect vis;
		const BOOL rowVisible = vis.IntersectRect(&vr, &body) && vis.Height() > 1;

		if (it.kind == CCUSTOM_POPUP_SLIDER && it.sliderIndex >= 0) {
			CCustomSliderCtrl& sl = m_sliders[it.sliderIndex];
			CRect sr = vr; sr.DeflateRect(padX + checkW, padTop, padR, padBot);
			CRect hit;
			const BOOL onScreen = rowVisible && hit.IntersectRect(&sr, &body) && hit.Height() > 2;
			if (!sl.GetSafeHwnd()) {
				sl.Create(WS_CHILD | WS_CLIPSIBLINGS | TBS_HORZ | TBS_NOTICKS, sr, this, 6000 + it.sliderIndex);
				sl.SetAeroMode(FALSE); sl.SetRange(it.sliderMin, it.sliderMax, TRUE); sl.SetPos(it.sliderPos);
			}
			placeChild(sl, sr, onScreen);
		} else if (it.kind == CCUSTOM_POPUP_EDIT && it.editIndex >= 0) {
			CCustomEdit& ed = m_edits[it.editIndex];
			CRect er = vr; er.DeflateRect(padX + checkW, padTop, padR, padBot);
			CRect hit;
			const BOOL onScreen = rowVisible && hit.IntersectRect(&er, &body) && hit.Height() > 2;
			LPCTSTR init = L"";
			if (it.choiceSet >= 0 && it.choiceSet < m_choiceSetCount && m_choiceSets[it.choiceSet].count > 0)
				init = m_choiceSets[it.choiceSet].items[0];
			if (!ed.GetSafeHwnd()) {
				ed.Create(WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, er, this, 6100 + it.editIndex);
				// SetWindowText は EN_CHANGE を飛ばす。初期値でスリープ等が武装しないよう抑止
				m_suppressEditNotify = TRUE;
				ed.SetWindowText(init);
				m_suppressEditNotify = FALSE;
			}
			placeChild(ed, er, onScreen);
		} else if (it.kind == CCUSTOM_POPUP_COMBO && it.comboIndex >= 0 && it.choiceSet >= 0) {
			CCustomComboBox& cb = m_combos[it.comboIndex];
			CRect cr = vr; cr.DeflateRect(padX + checkW, padTop, padR, padBot);
			CRect hit;
			const BOOL onScreen = rowVisible && hit.IntersectRect(&cr, &body) && hit.Height() > 2;
			const CCustomPopupChoiceSet& set = m_choiceSets[it.choiceSet];
			if (!cb.GetSafeHwnd()) {
				cb.Create(WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
					cr, this, 6200 + it.comboIndex);
				cb.SetAeroMode(FALSE);
				for (int k = 0; k < set.count; ++k) cb.AddString(set.items[k]);
				if (set.count > 0) cb.SetCurSelPhysical(it.choiceSel);
			}
			// -1 = 閉じた状態の表示部、-1以外 = ドロップダウン行
			const int editH = (std::max)(comboEditH, cr.Height() - 2);
			cb.SetItemHeight(-1, editH);
			cb.SetItemHeight(0, comboListH);
			placeChild(cb, cr, onScreen);
		} else if (it.kind == CCUSTOM_POPUP_LIST && it.listIndex >= 0 && it.choiceSet >= 0) {
			CCustomListBox& lb = m_lists[it.listIndex];
			CRect lr = vr; lr.DeflateRect(padX + checkW, padTop, padR, padBot);
			CRect hit;
			const BOOL onScreen = rowVisible && hit.IntersectRect(&lr, &body) && hit.Height() > 2;
			const CCustomPopupChoiceSet& set = m_choiceSets[it.choiceSet];
			if (!lb.GetSafeHwnd()) {
				lb.Create(WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | WS_BORDER
					| LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
					lr, this, 6300 + it.listIndex);
				lb.SetAeroMode(FALSE);
				for (int k = 0; k < set.count; ++k) lb.AddString(set.items[k]);
				if (set.count > 0) lb.SetCurSel(it.choiceSel);
			}
			placeChild(lb, lr, onScreen);
		} else if (it.kind == CCUSTOM_POPUP_RANGE && it.rangeIndex >= 0) {
			CCustomRangeSliderCtrl& rs = m_ranges[it.rangeIndex];
			CRect rr = vr; rr.DeflateRect(padX + checkW, padTop, padR, padBot);
			CRect hit;
			const BOOL onScreen = rowVisible && hit.IntersectRect(&rr, &body) && hit.Height() > 2;
			if (!rs.GetSafeHwnd()) {
				rs.Create(WS_CHILD | WS_CLIPSIBLINGS | TBS_HORZ | TBS_NOTICKS, rr, this, 6400 + it.rangeIndex);
				rs.SetAeroMode(FALSE);
				rs.SetRange(it.sliderMin, it.sliderMax, TRUE);
				rs.SetPos(it.sliderPos);
				rs.SetSelection(it.rangeSelMin, it.rangeSelMax);
				rs.SetAB(it.rangeAbA, it.rangeAbB);
				rs.SetSelectionLocked(FALSE);
			}
			placeChild(rs, rr, onScreen);
		} else if (it.kind == CCUSTOM_POPUP_PROGRESS && it.progressIndex >= 0) {
			CCustomProgressCtrl& pg = m_progresses[it.progressIndex];
			CRect pr = vr; pr.DeflateRect(padX + checkW, padTop, padR, padBot);
			CRect hit;
			const BOOL onScreen = rowVisible && hit.IntersectRect(&pr, &body) && hit.Height() > 2;
			if (!pg.GetSafeHwnd()) {
				pg.Create(WS_CHILD | WS_CLIPSIBLINGS, pr, this, 6500 + it.progressIndex);
				pg.SetAeroMode(FALSE);
				pg.SetRange(it.sliderMin, it.sliderMax);
				pg.SetPos(it.sliderPos);
				pg.SetShowPercent(it.progressShowPct);
			}
			placeChild(pg, pr, onScreen);
		} else if (it.kind == CCUSTOM_POPUP_BUTTON && it.buttonIndex >= 0) {
			CCustomStandardButton& bt = m_buttons[it.buttonIndex];
			const int btnPad = PopupSx(dpi, 6);
			CRect br = vr; br.DeflateRect(padX + checkW, btnPad, padR, btnPad);
			CRect hit;
			const BOOL onScreen = rowVisible && hit.IntersectRect(&br, &body) && hit.Height() > 2;
			if (!bt.GetSafeHwnd()) {
				bt.Create(it.text, WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | BS_PUSHBUTTON,
					br, this, 6600 + it.buttonIndex);
				bt.SetAeroMode(FALSE);
				bt.SetFlat(FALSE);
			}
			placeChild(bt, br, onScreen);
		}
	}
}

void CCustomPopupMenu::ShowEmbedded(BOOL show)
{
	if (!show) {
		for (int i = 0; i < m_sliderCount; ++i)
			if (m_sliders[i].GetSafeHwnd()) m_sliders[i].ShowWindow(SW_HIDE);
		for (int i = 0; i < m_editCount; ++i)
			if (m_edits[i].GetSafeHwnd()) m_edits[i].ShowWindow(SW_HIDE);
		for (int i = 0; i < m_comboCount; ++i)
			if (m_combos[i].GetSafeHwnd()) m_combos[i].ShowWindow(SW_HIDE);
		for (int i = 0; i < m_listCount; ++i)
			if (m_lists[i].GetSafeHwnd()) m_lists[i].ShowWindow(SW_HIDE);
		for (int i = 0; i < m_rangeCount; ++i)
			if (m_ranges[i].GetSafeHwnd()) m_ranges[i].ShowWindow(SW_HIDE);
		for (int i = 0; i < m_progressCount; ++i)
			if (m_progresses[i].GetSafeHwnd()) m_progresses[i].ShowWindow(SW_HIDE);
		for (int i = 0; i < m_buttonCount; ++i)
			if (m_buttons[i].GetSafeHwnd()) m_buttons[i].ShowWindow(SW_HIDE);
		return;
	}
	// 表示は onScreen 判定込みの Sync に任せる
	SyncEmbeddedChildren();
}

void CCustomPopupMenu::RefreshEmbeddedChildren()
{
	if (!GetSafeHwnd()) return;
	for (HWND h = ::GetWindow(m_hWnd, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT)) {
		if (!::IsWindowVisible(h)) continue;
		// ERASE 無し: 白フラッシュ→再描画の点滅を避ける
		::RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
	}
}

void CCustomPopupMenu::RevealEmbeddedAfterAnim()
{
	if (!GetSafeHwnd()) return;
	ShowEmbedded(TRUE); // 内で Sync（位置合わせ＋Show）
	RefreshEmbeddedChildren();
	// 親 BufferedPaint 後の潰し対策（マウスを動かさなくても着地させる）
	PostMessage(WM_APP + 0x51C, 0, 0);
}

BOOL CCustomPopupMenu::ChipFlightRowsAtRest() const
{
	if (m_lineAnimPhase != 1 || m_itemCount <= 0)
		return FALSE;
	BOOL any = FALSE;
	for (int i = 0; i < m_itemCount; ++i) {
		if (m_items[i].kind == CCUSTOM_POPUP_SEP) continue;
		int ox = 0, oy = 0, fade = 0;
		if (!CalcLineAnim(i, &ox, &oy, &fade))
			return FALSE;
		any = TRUE;
		if (fade < 250) return FALSE;
		if (ox > 2 || ox < -2 || oy > 2 || oy < -2) return FALSE;
	}
	return any;
}

BOOL CCustomPopupMenu::CalcLineAnim(int idx, int* ox, int* oy, int* fade) const
{
	if (ox) *ox = 0;
	if (oy) *oy = 0;
	if (fade) *fade = 256;
	if (m_lineAnimPhase == 0 || idx < 0 || idx >= m_itemCount)
		return TRUE;
	const int style = PopupAnimStyle();
	if (style == POPUP_ANIM_CLASSIC)
		return TRUE;

	const ULONGLONG now = GetTickCount64();
	const int elapsed = (int)(now - m_lineAnimStart);
	const int dist = (idx > m_lineAnimOrigin) ? (idx - m_lineAnimOrigin) : (m_lineAnimOrigin - idx);
	auto pickStag = [](int baseStag, int itemCount) -> int {
		if (itemCount <= 1) return baseStag;
		const int budget = CCUSTOM_POPUP_LINE_STAGGER_BUDGET;
		if (baseStag * (itemCount - 1) <= budget) return baseStag;
		int s = budget / (itemCount - 1);
		if (s < 1) s = 1;
		return s;
	};
	auto easeOut = [](float t) -> float {
		if (t <= 0.f) return 0.f;
		if (t >= 1.f) return 1.f;
		const float u = 1.f - t;
		return 1.f - u * u * u;
	};
	// 行き過ぎて戻る（ブラックホール着地）
	auto easeOutBack = [](float t) -> float {
		if (t <= 0.f) return 0.f;
		if (t >= 1.f) return 1.f;
		const float c1 = 1.70158f;
		const float c3 = c1 + 1.f;
		const float u = t - 1.f;
		return 1.f + c3 * u * u * u + c1 * u * u;
	};
	auto fadeIn = [](float t) -> int {
		if (t <= 0.f) return 0;
		if (t >= 0.40f) return 256;
		int f = (int)(256.f * (t / 0.40f));
		if (f < 0) f = 0;
		if (f > 256) f = 256;
		return f;
	};
	auto fadeOut = [](float t) -> int {
		if (t <= 0.f) return 256;
		if (t >= 1.f) return 0;
		int f = (int)(256.f * (1.f - t));
		if (f < 0) f = 0;
		if (f > 256) f = 256;
		return f;
	};
	auto scatterXY = [](int i, int origin, float* sx, float* sy) {
		// 決定的に全方位へ散らす（std 無し）
		const float ang = (float)i * 2.3999632f + (float)origin * 0.41f;
		const float rad = 110.f + (float)((i * 47 + origin * 19) % 110);
		*sx = cosf(ang) * rad;
		*sy = sinf(ang) * rad;
	};
	auto easeInOutSine = [](float t) -> float {
		if (t <= 0.f) return 0.f;
		if (t >= 1.f) return 1.f;
		return 0.5f * (1.f - cosf(t * 3.14159265f));
	};

	if (m_lineAnimPhase == 1) {
		const int stag = pickStag(m_asSubmenu ? CCUSTOM_POPUP_LINE_STAGGER_SUB : CCUSTOM_POPUP_LINE_STAGGER_IN, m_itemCount);
		const int dur = ChipInDurMs(style, m_asSubmenu);
		int delay = dist * stag;
		if (m_asSubmenu && !UsesRadialStagger(style))
			delay = idx * stag;
		else if (UsesIndexStagger(style))
			delay = idx * stag;
		else if (UsesRadialStagger(style))
			delay = dist * stag;

		if (elapsed < delay) {
			if (fade) *fade = 0;
			return FALSE;
		}
		float t = (float)(elapsed - delay) / (float)max(1, dur);
		if (t > 1.f) t = 1.f;
		if (t < 0.f) t = 0.f;

		if (style == POPUP_ANIM_BIGBANG) {
			float sx = 0.f, sy = 0.f;
			scatterXY(idx, m_lineAnimOrigin, &sx, &sy);
			const float e = easeOutBack(t); // >1 で行き過ぎ→戻る
			if (ox) *ox = (int)(sx * (1.f - e));
			if (oy) *oy = (int)(sy * (1.f - e));
			if (fade) *fade = fadeIn(t);
			return TRUE;
		}
		if (style == POPUP_ANIM_SPIRAL) {
			const float e = easeOutBack(t);
			const float ang0 = (float)idx * 2.3999632f + (float)m_lineAnimOrigin * 0.37f;
			const float spin = (1.f - min(e, 1.f)) * 2.6f;
			const float ang = ang0 + spin;
			const float rad = (125.f + (float)((idx * 37) % 90)) * (1.f - min(e, 1.f));
			if (ox) *ox = (int)(cosf(ang) * rad);
			if (oy) *oy = (int)(sinf(ang) * rad);
			if (fade) *fade = fadeIn(t);
			return TRUE;
		}
		if (style == POPUP_ANIM_PETAL) {
			const float e = easeInOutSine(t);
			const float sway = sinf((float)idx * 1.71f + t * 2.4f) * 24.f * (1.f - e);
			if (ox) *ox = (int)sway;
			if (oy) *oy = (int)(52.f * (1.f - e)); // 下からふわり
			if (fade) *fade = fadeIn(t * 0.9f);
			return TRUE;
		}
		if (style == POPUP_ANIM_ZIPPER) {
			const float e = easeOutBack(t);
			const float side = (idx & 1) ? 1.f : -1.f;
			if (ox) *ox = (int)(side * 96.f * (1.f - min(e, 1.f)));
			if (oy) *oy = (int)(side * -10.f * (1.f - min(e, 1.f)));
			if (fade) *fade = fadeIn(t);
			return TRUE;
		}
		if (style == POPUP_ANIM_AURORA) {
			const float e = easeInOutSine(t);
			const float wave = sinf((float)idx * 0.62f + t * 3.6f) * 30.f;
			if (ox) *ox = (int)((-68.f + wave) * (1.f - e));
			if (oy) *oy = (int)(sinf((float)idx * 0.95f) * 18.f * (1.f - e));
			if (fade) *fade = fadeIn(t * 0.85f);
			return TRUE;
		}
		if (style == POPUP_ANIM_CASCADE) {
			const float e = easeOut(t);
			if (ox) *ox = 0;
			if (oy) *oy = (int)(-56.f * (1.f - e));
			if (fade) *fade = fadeIn(t);
			return TRUE;
		}
		if (style == POPUP_ANIM_SLIDE) {
			const float e = easeOut(t);
			if (ox) *ox = (int)(-72.f * (1.f - e));
			if (oy) *oy = 0;
			if (fade) *fade = fadeIn(t);
			return TRUE;
		}
		if (style == POPUP_ANIM_POP) {
			const float e = easeOut(t);
			CRect vr = ItemViewRect(idx);
			const int itemY = (vr.top + vr.bottom) / 2;
			float pull = (float)(m_lineAnimOriginY - itemY);
			if (pull > 48.f) pull = 48.f;
			if (pull < -48.f) pull = -48.f;
			if (ox) *ox = 0;
			if (oy) *oy = (int)(pull * (1.f - e));
			if (fade) *fade = fadeIn(t);
			return TRUE;
		}

		const float e = easeOut(t);
		if (m_asSubmenu) {
			if (ox) *ox = (int)(-26.f * (1.f - e));
			if (oy) *oy = 0;
		} else if (style == POPUP_ANIM_EXPAND) {
			CRect vr = ItemViewRect(idx);
			const int itemY = (vr.top + vr.bottom) / 2;
			float pull = (float)(m_lineAnimOriginY - itemY);
			if (pull > 10.f) pull = 10.f;
			if (pull < -10.f) pull = -10.f;
			if (ox) *ox = 0;
			if (oy) *oy = (int)(pull * (1.f - e));
		} else {
			if (ox) *ox = 0;
			if (oy) *oy = 0;
		}
		if (fade) *fade = fadeIn(t);
		return TRUE;
	}

	// ---- exit ----
	const int stag = pickStag(CCUSTOM_POPUP_LINE_STAGGER_OUT, m_itemCount);
	const int dur = ChipOutDurMs(style);
	const int maxDist = max(m_lineAnimOrigin, m_itemCount - 1 - m_lineAnimOrigin);
	int delay = (maxDist - dist) * stag;
	if (m_asSubmenu && !UsesRadialStagger(style))
		delay = (m_itemCount - 1 - idx) * stag;
	else if (UsesIndexStagger(style))
		delay = (m_itemCount - 1 - idx) * stag;
	else if (UsesRadialStagger(style))
		delay = (maxDist - dist) * stag;

	if (elapsed < delay) {
		if (fade) *fade = 256;
		return TRUE;
	}
	float t = (float)(elapsed - delay) / (float)max(1, dur);
	if (t > 1.f) t = 1.f;
	if (t < 0.f) t = 0.f;
	const float e = easeOut(t);

	if (style == POPUP_ANIM_BIGBANG) {
		float sx = 0.f, sy = 0.f;
		scatterXY(idx, m_lineAnimOrigin, &sx, &sy);
		if (ox) *ox = (int)(sx * e * 1.25f);
		if (oy) *oy = (int)(sy * e * 1.25f);
	} else if (style == POPUP_ANIM_SPIRAL) {
		const float ang0 = (float)idx * 2.3999632f + (float)m_lineAnimOrigin * 0.37f;
		const float ang = ang0 + e * 3.1f;
		const float rad = (140.f + (float)((idx * 37) % 90)) * e;
		if (ox) *ox = (int)(cosf(ang) * rad);
		if (oy) *oy = (int)(sinf(ang) * rad);
	} else if (style == POPUP_ANIM_PETAL) {
		if (ox) *ox = (int)(sinf((float)idx * 1.71f) * 32.f * e);
		if (oy) *oy = (int)(-44.f * e); // 上へ散る
	} else if (style == POPUP_ANIM_ZIPPER) {
		const float side = (idx & 1) ? 1.f : -1.f;
		if (ox) *ox = (int)(side * 88.f * e);
		if (oy) *oy = (int)(side * 12.f * e);
	} else if (style == POPUP_ANIM_AURORA) {
		const float wave = sinf((float)idx * 0.62f + t * 4.f) * 36.f;
		if (ox) *ox = (int)((72.f + wave) * e);
		if (oy) *oy = (int)(sinf((float)idx * 0.95f) * 22.f * e);
	} else if (style == POPUP_ANIM_SLIDE) {
		if (ox) *ox = (int)(64.f * e);
		if (oy) *oy = 0;
	} else if (style == POPUP_ANIM_CASCADE) {
		if (ox) *ox = 0;
		if (oy) *oy = (int)(-48.f * e);
	} else if (style == POPUP_ANIM_POP) {
		CRect vr = ItemViewRect(idx);
		const int itemY = (vr.top + vr.bottom) / 2;
		float pull = (float)(m_lineAnimOriginY - itemY);
		if (pull > 40.f) pull = 40.f;
		if (pull < -40.f) pull = -40.f;
		if (ox) *ox = 0;
		if (oy) *oy = (int)(pull * e);
	} else if (m_asSubmenu) {
		if (ox) *ox = (int)(22.f * e);
		if (oy) *oy = 0;
	} else {
		CRect vr = ItemViewRect(idx);
		const int itemY = (vr.top + vr.bottom) / 2;
		float pull = (float)(m_lineAnimOriginY - itemY);
		if (pull > 8.f) pull = 8.f;
		if (pull < -8.f) pull = -8.f;
		if (ox) *ox = 0;
		if (oy) *oy = (int)(pull * e);
	}
	if (fade) *fade = fadeOut(t);
	return (fade ? *fade : 256) > 8;
}

void CCustomPopupMenu::AnimateIn()
{
	if (!GetSafeHwnd()) return;
	ShowEmbedded(FALSE);
	if (GetExStyle() & WS_EX_LAYERED)
		ModifyStyleEx(WS_EX_LAYERED, 0);
	m_animTick = 0;

	const int style = PopupAnimStyle();
	// クラシック／設定サブ(フォント・描画方法): 即時表示。行アニメ中ヒットずれで選択が握りつぶされるのを防ぐ
	if (style == POPUP_ANIM_CLASSIC || m_skipChrome) {
		::SetWindowRgn(m_hWnd, NULL, FALSE);
		ShowWindow(SW_SHOWNA);
		if (style == POPUP_ANIM_CLASSIC && !m_asSubmenu)
			::AnimateWindow(m_hWnd, 160, AW_BLEND);
		ShowEmbedded(TRUE);
		SyncEmbeddedChildren();
		InvalidateBgOnly();
		UpdateWindow();
		RefreshEmbeddedChildren();
		PostMessage(WM_APP + 0x51C, 0, 0);
		SetTimer(kAnimTimer, 50, NULL);
		m_lineAnimPhase = 0;
		return;
	}

	::SetWindowRgn(m_hWnd, NULL, FALSE);

	// 上下伸び: RGN。行チップ系: ULW飛行。他は全面パネル＋行オフセット
	if (style == POPUP_ANIM_EXPAND && !m_asSubmenu) {
		CRect rc; GetClientRect(&rc);
		int y0 = m_lineAnimOriginY - 4;
		int y1 = m_lineAnimOriginY + 4;
		if (y0 < 0) y0 = 0;
		if (y1 > rc.bottom) y1 = rc.bottom;
		if (y1 <= y0) { y0 = 0; y1 = 4; }
		HRGN hSeed = ::CreateRectRgn(rc.left, y0, rc.right, y1);
		::SetWindowRgn(m_hWnd, hSeed, TRUE);
	} else if (UsesRowChipFlight(style)) {
		BeginChipFlight();
	}

	ShowWindow(SW_SHOWNA);
	m_lineAnimPhase = 1;
	m_lineAnimStart = GetTickCount64();
	if (UsesRowChipFlight(style))
		ForceChipPresent();
	else {
		InvalidateBgOnly();
		UpdateWindow();
	}
	SetTimer(kAnimTimer, 16, NULL);
	// WM_TIMER(16ms) だけに頼らず、所要時間後に必ず定着（マウス未移動でも子が出る）
	SetTimer(kSettleTimer, (UINT)max(50, ChipEnterTotalMs(style, m_asSubmenu, m_itemCount, m_lineAnimOrigin) + 30), NULL);
}

void CCustomPopupMenu::AnimateOut()
{
	if (!GetSafeHwnd() || !IsWindowVisible()) return;
	ShowEmbedded(FALSE);
	if (m_tip.GetSafeHwnd()) m_tip.Activate(FALSE);

	const int style = PopupAnimStyle();
	if (style == POPUP_ANIM_CLASSIC || m_skipChrome) {
		::SetWindowRgn(m_hWnd, NULL, TRUE);
		m_lineAnimPhase = 0;
		if (style == POPUP_ANIM_CLASSIC && !m_asSubmenu) {
			if (!::AnimateWindow(m_hWnd, 120, AW_BLEND | AW_HIDE))
				ShowWindow(SW_HIDE);
		} else {
			ShowWindow(SW_HIDE);
		}
		return;
	}

	// 上下伸びは RGN 維持。行チップ系は ULW 飛行（全体→行は拡大直後に即 Present）
	if (UsesRowChipFlight(style)) {
		BeginChipFlight();
		m_bridgePanel = TRUE;
		m_lineAnimPhase = 2;
		m_lineAnimStart = GetTickCount64();
		ForceChipPresent();
		m_bridgePanel = FALSE;
	} else if (!(style == POPUP_ANIM_EXPAND && !m_asSubmenu)) {
		::SetWindowRgn(m_hWnd, NULL, TRUE);
		CClientDC dc(this);
		CRect rc; GetClientRect(&rc);
		dc.FillSolidRect(&rc, PopupBg());
		m_lineAnimPhase = 2;
		m_lineAnimStart = GetTickCount64();
	} else {
		m_lineAnimPhase = 2;
		m_lineAnimStart = GetTickCount64();
	}
	SetTimer(kAnimTimer, 16, NULL);
	int stag = CCUSTOM_POPUP_LINE_STAGGER_OUT;
	if (m_itemCount > 1) {
		const int need = stag * (m_itemCount - 1);
		if (need > CCUSTOM_POPUP_LINE_STAGGER_BUDGET)
			stag = max(1, CCUSTOM_POPUP_LINE_STAGGER_BUDGET / (m_itemCount - 1));
	}
	int span = 0;
	if (UsesRadialStagger(style)
		|| (!m_asSubmenu && style == POPUP_ANIM_EXPAND))
		span = max(m_lineAnimOrigin, m_itemCount - 1 - m_lineAnimOrigin);
	else if (UsesIndexStagger(style) || m_asSubmenu)
		span = (m_itemCount > 0) ? (m_itemCount - 1) : 0;
	else
		span = max(m_lineAnimOrigin, m_itemCount - 1 - m_lineAnimOrigin);
	const int outDur = ChipOutDurMs(style);
	const int total = span * stag + outDur + 40;
	const ULONGLONG endAt = m_lineAnimStart + (ULONGLONG)total;
	MSG msg;
	while (GetSafeHwnd() && GetTickCount64() < endAt) {
		while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				::PostQuitMessage((int)msg.wParam);
				break;
			}
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
		InvalidateBgOnly();
		UpdateWindow();
		::MsgWaitForMultipleObjects(0, NULL, FALSE, 8, QS_ALLINPUT);
	}
	KillTimer(kAnimTimer);
	KillTimer(kSettleTimer);
	m_lineAnimPhase = 0;
	// 先に隠す。レイヤ解除や EndChipFlight を見える状態でやると
	// 透明ULW→不透明GDI が一フレ出てチラつく。
	if (GetSafeHwnd())
		ShowWindow(SW_HIDE);
	if (GetSafeHwnd()) {
		if (UsesRowChipFlight(style)) {
			EndChipFlight();
			if (GetExStyle() & WS_EX_LAYERED)
				ModifyStyleEx(WS_EX_LAYERED, 0);
		} else {
			::SetWindowRgn(m_hWnd, NULL, FALSE);
		}
	}
}

BOOL CCustomPopupMenu::CreatePopupAt(CPoint screenPt, CCustomPopupMenu* parentMenu, CCustomPopupMenu* root)
{
	EnsurePopupClass();
	RebuildMenuFont();
	MeasureLayout();

	const CPoint clickPt = screenPt; // クランプ前＝コンテキスト位置（広がり起点）

	m_scrollY = 0;
	m_scrollMax = 0;
	MONITORINFO mi; ZeroMemory(&mi, sizeof(mi)); mi.cbSize = sizeof(mi);
	HMONITOR hMon = ::MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
	if (hMon && ::GetMonitorInfo(hMon, &mi)) {
		const int maxH = mi.rcWork.bottom - mi.rcWork.top - 8;
		if (m_contentH > maxH) {
			m_menuH = maxH;
			const int stickyMin = PopupSx(PopupDpiFromHwnd(m_hWnd), CCUSTOM_POPUP_ITEM_H) * 3;
			if (m_stickyH > 0 && m_menuH < m_stickyH + stickyMin)
				m_menuH = min(maxH, m_stickyH + stickyMin);
			const int bodyView = max(0, m_menuH - m_stickyH);
			const int bodyContent = max(0, m_contentH - m_stickyH);
			m_scrollMax = max(0, bodyContent - bodyView);
		} else {
			m_menuH = m_contentH;
			m_scrollMax = 0;
		}
		if (screenPt.x + m_menuW > mi.rcWork.right) screenPt.x = mi.rcWork.right - m_menuW;
		if (screenPt.y + m_menuH > mi.rcWork.bottom) screenPt.y = mi.rcWork.bottom - m_menuH;
		if (screenPt.x < mi.rcWork.left) screenPt.x = mi.rcWork.left;
		if (screenPt.y < mi.rcWork.top) screenPt.y = mi.rcWork.top;
	}

	m_parentMenu = parentMenu;
	m_root = root ? root : this;
	m_asSubmenu = (parentMenu != NULL);
	m_hot = -1; m_openSub = -1; m_done = FALSE; m_result = 0; m_animTick = 0;
	m_lineAnimPhase = 0; m_lineAnimStart = 0;
	m_lineAnimOrigin = 0; m_lineAnimOriginY = 0;
	m_flightPad = 0;
	m_bridgePanel = FALSE;
	m_bounceIdx = -1; m_nBounce = 0;
	if (GetSafeHwnd()) DestroyWindow();

	// 行チップ飛行: ドロップシャドウ無しクラス（右端黒バー対策）
	ClampPopupAnimSave();
	const BOOL useChipClass = (UsesRowChipFlight(PopupAnimStyle()) && !m_skipChrome);
	if (!CreateEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
		useChipClass ? L"CCustomPopupMenuChipClass" : L"CCustomPopupMenuClass", NULL,
		WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		screenPt.x, screenPt.y, m_menuW, m_menuH, m_owner ? m_owner->GetSafeHwnd() : NULL, NULL))
		return FALSE;

	// 描画方法レ点を savedata と再同期（サブ再表示や選択直後のずれ防止）
	ClampPopupAnimSave();
	for (int i = 0; i < m_itemCount; ++i) {
		if (m_items[i].id < CCUSTOM_POPUP_ID_ANIM0
			|| m_items[i].id >= CCUSTOM_POPUP_ID_ANIM0 + (UINT)POPUP_ANIM_COUNT)
			continue;
		m_items[i].checked =
			((int)(m_items[i].id - CCUSTOM_POPUP_ID_ANIM0) == savedata.popupMenuAnim) ? TRUE : FALSE;
	}

	// クリック位置に最も近い行を起点に（上下へ広がる）
	{
		CPoint c = clickPt;
		ScreenToClient(&c);
		m_lineAnimOriginY = c.y;
		int best = 0x7fffffff;
		m_lineAnimOrigin = 0;
		for (int i = 0; i < m_itemCount; ++i) {
			CRect vr = ItemViewRect(i);
			const int cy = (vr.top + vr.bottom) / 2;
			const int d = (cy > c.y) ? (cy - c.y) : (c.y - cy);
			if (d < best) { best = d; m_lineAnimOrigin = i; m_lineAnimOriginY = cy; }
		}
		if (m_itemCount <= 0) {
			m_lineAnimOrigin = 0;
			m_lineAnimOriginY = 0;
		}
	}

	SyncEmbeddedChildren();
	ShowEmbedded(FALSE);
	if (CCustomControlUtility::BeginDialogToolTip(m_tip, this, TTS_ALWAYSTIP | TTS_NOPREFIX | TTS_NOANIMATE)) {
		TOOLINFO ti; ZeroMemory(&ti, sizeof(ti));
		ti.cbSize = sizeof(ti);
		ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS | TTF_TRANSPARENT;
		ti.hwnd = m_hWnd; ti.uId = (UINT_PTR)m_hWnd; ti.lpszText = LPSTR_TEXTCALLBACK;
		m_tip.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
		CCustomControlUtility::FinalizeDialogToolTip(m_tip, 420, 12000);
	}
	if (CCC_IsInwoman()) { CCC_StartInwomanTimer(); SetTimer(kInwomanTimer, 50, NULL); }
	SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	AnimateIn();
	// 内包コントロールは行アニメ完了後に OnTimer で表示（出現中のちらつき防止）
	return TRUE;
}

void CCustomPopupMenu::AbortAnimAndHide()
{
	if (!GetSafeHwnd()) return;
	KillTimer(kAnimTimer);
	KillTimer(kSettleTimer);
	KillTimer(kBounceTimer);
	m_lineAnimPhase = 0;
	m_bridgePanel = FALSE;
	m_bounceIdx = -1;
	m_nBounce = 0;
	ShowEmbedded(FALSE);
	if (m_tip.GetSafeHwnd()) m_tip.Activate(FALSE);
	// レイヤ解除より先に隠す（透明→不透明の最終フレ防止）
	ShowWindow(SW_HIDE);
	if (m_flightPad > 0 || (GetExStyle() & WS_EX_LAYERED)) {
		EndChipFlight();
		if (GetExStyle() & WS_EX_LAYERED)
			ModifyStyleEx(WS_EX_LAYERED, 0);
	}
	::SetWindowRgn(m_hWnd, NULL, FALSE);
}

void CCustomPopupMenu::DestroyPopupTree(BOOL animateOut)
{
	if (s_trackingRoot == this)
		s_trackingRoot = NULL;
	CloseOpenSub();
	for (int i = 0; i < m_subCount; ++i)
		if (m_subs[i]) m_subs[i]->DestroyPopupTree(FALSE);
	for (int i = 0; i < m_sliderCount; ++i)
		if (m_sliders[i].GetSafeHwnd()) m_sliders[i].DestroyWindow();
	for (int i = 0; i < m_editCount; ++i)
		if (m_edits[i].GetSafeHwnd()) m_edits[i].DestroyWindow();
	for (int i = 0; i < m_comboCount; ++i)
		if (m_combos[i].GetSafeHwnd()) m_combos[i].DestroyWindow();
	for (int i = 0; i < m_listCount; ++i)
		if (m_lists[i].GetSafeHwnd()) m_lists[i].DestroyWindow();
	for (int i = 0; i < m_rangeCount; ++i)
		if (m_ranges[i].GetSafeHwnd()) m_ranges[i].DestroyWindow();
	for (int i = 0; i < m_progressCount; ++i)
		if (m_progresses[i].GetSafeHwnd()) m_progresses[i].DestroyWindow();
	for (int i = 0; i < m_buttonCount; ++i)
		if (m_buttons[i].GetSafeHwnd()) m_buttons[i].DestroyWindow();
	if (m_tip.GetSafeHwnd()) m_tip.DestroyWindow();
	if (GetSafeHwnd()) {
		KillTimer(kTipTimer); KillTimer(kInwomanTimer); KillTimer(kAnimTimer); KillTimer(kSettleTimer); KillTimer(kBounceTimer);
		if (animateOut)
			AnimateOut();
		else
			AbortAnimAndHide();
		DestroyWindow();
	}
	if (m_memBmp.GetSafeHandle()) { m_memBmp.DeleteObject(); m_memW = m_memH = 0; }
}

void CCustomPopupMenu::CloseOpenSub()
{
	if (m_openSub >= 0 && m_openSub < m_itemCount) {
		const int si = m_items[m_openSub].subIndex;
		// ホバーで閉じるときは退出アニメ禁止。AnimateOut 中に 4～11 へ動くと「サブが引きずられる」ように見える。
		if (si >= 0 && si < m_subCount && m_subs[si])
			m_subs[si]->DestroyPopupTree(FALSE);
	}
	m_openSub = -1;
	ClearPreviewFace();
}

void CCustomPopupMenu::CloseChain(UINT result)
{
	CCustomPopupMenu* root = RootMenu();
	if (!root) return;
	root->m_result = result;
	root->m_done = TRUE;
}

CCustomPopupMenu* CCustomPopupMenu::RootMenu()
{ return m_root ? m_root : this; }

BOOL CCustomPopupMenu::IsPointInChain(CPoint screenPt) const
{
	if (GetSafeHwnd()) {
		CRect r; GetWindowRect(&r);
		if (r.PtInRect(screenPt)) {
			// BigBang飛行余白は見た目の穴（RGN外）。余白クリックで閉じないよう中身／チップのみヒット
			if (m_flightPad > 0 && m_lineAnimPhase != 0) {
				CPoint c = screenPt;
				ScreenToClient(&c);
				CRect content(m_flightPad, m_flightPad, m_flightPad + m_menuW, m_flightPad + m_menuH);
				if (content.PtInRect(c)) return TRUE;
				return (HitTest(c) >= 0) ? TRUE : FALSE;
			}
			return TRUE;
		}
	}
	for (int i = 0; i < m_comboCount; ++i) {
		if (!m_combos[i].GetSafeHwnd()) continue;
		COMBOBOXINFO cbi = { sizeof(cbi) };
		if (::GetComboBoxInfo(m_combos[i].GetSafeHwnd(), &cbi) && cbi.hwndList && ::IsWindowVisible(cbi.hwndList)) {
			CRect lr; ::GetWindowRect(cbi.hwndList, &lr);
			if (lr.PtInRect(screenPt)) return TRUE;
		}
	}
	if (m_openSub >= 0 && m_openSub < m_itemCount) {
		const int si = m_items[m_openSub].subIndex;
		if (si >= 0 && si < m_subCount && m_subs[si] && m_subs[si]->IsPointInChain(screenPt))
			return TRUE;
	}
	return FALSE;
}

BOOL CCustomPopupMenu::IsHwndRelated(HWND h) const
{
	if (!h || !::IsWindow(h)) return FALSE;
	if (GetSafeHwnd() && (h == m_hWnd || ::IsChild(m_hWnd, h)))
		return TRUE;
	if (m_owner && m_owner->GetSafeHwnd()) {
		HWND ow = m_owner->GetSafeHwnd();
		if (h == ow || ::IsChild(ow, h))
			return TRUE;
		// Track(子コントロール) 時、フォアグラウンドはトップレベルになる
		HWND root = ::GetAncestor(ow, GA_ROOT);
		if (root && root != ow && (h == root || ::IsChild(root, h)))
			return TRUE;
	}
	for (int i = 0; i < m_comboCount; ++i) {
		if (!m_combos[i].GetSafeHwnd()) continue;
		COMBOBOXINFO cbi = { sizeof(cbi) };
		if (::GetComboBoxInfo(m_combos[i].GetSafeHwnd(), &cbi) && cbi.hwndList
			&& (h == cbi.hwndList || ::IsChild(cbi.hwndList, h)))
			return TRUE;
	}
	if (m_openSub >= 0 && m_openSub < m_itemCount) {
		const int si = m_items[m_openSub].subIndex;
		if (si >= 0 && si < m_subCount && m_subs[si] && m_subs[si]->IsHwndRelated(h))
			return TRUE;
	}
	return FALSE;
}

static BOOL PopupOwnerRelaxesDismiss(CWnd* owner)
{
	if (!owner || !owner->GetSafeHwnd()) return FALSE;
	HWND root = ::GetAncestor(owner->GetSafeHwnd(), GA_ROOT);
	if (!root) root = owner->GetSafeHwnd();
	return (::GetProp(root, CCUSTOM_POPUP_RELAX_DISMISS_PROP) != NULL) ? TRUE : FALSE;
}

BOOL CCustomPopupMenu::IsForegroundOurs() const
{
	HWND fg = ::GetForegroundWindow();
	if (!fg) return TRUE;
	const CCustomPopupMenu* root = m_root ? m_root : this;
	if (root->IsHwndRelated(fg))
		return TRUE;
	// 画面キャプチャ等: PrintWindow/WGC が他窓を前面化してもメニューを維持
	if (PopupOwnerRelaxesDismiss(root->m_owner))
		return TRUE;
	return FALSE;
}

CRect CCustomPopupMenu::ItemViewRect(int idx) const
{
	if (idx < 0 || idx >= m_itemCount)
		return CRect(0, 0, 0, 0);
	CRect r = m_items[idx].rc;
	if (idx >= m_stickyCount)
		r.OffsetRect(0, -m_scrollY);
	if (m_flightPad)
		r.OffsetRect(m_flightPad, m_flightPad);
	return r;
}

void CCustomPopupMenu::BeginChipFlight()
{
	if (!GetSafeHwnd()) return;
	::SetWindowRgn(m_hWnd, NULL, FALSE);
	ModifyStyleEx(0, WS_EX_LAYERED);
	if (m_flightPad > 0) return;

	const int pad = FlightPadForStyle(PopupAnimStyle());
	const int fw = m_menuW;
	const int fh = m_menuH;
	CRect wr; GetWindowRect(&wr);

	// 表示中の全体→行だけ: 先に拡大ULWを出し HWND を追従（初回Show前は不要）
	if (IsWindowVisible() && fw > 0 && fh > 0) {
		CClientDC dc(this);
		CDC srcDC; srcDC.CreateCompatibleDC(&dc);
		CBitmap srcBmp;
		CDC largeDC; largeDC.CreateCompatibleDC(&dc);
		CBitmap largeBmp;
		if (srcBmp.CreateCompatibleBitmap(&dc, fw, fh)
			&& largeBmp.CreateCompatibleBitmap(&dc, fw + 2 * pad, fh + 2 * pad)) {
			CBitmap* obS = srcDC.SelectObject(&srcBmp);
			{
				CRect sr(0, 0, fw, fh);
				srcDC.FillSolidRect(&sr, PopupBg());
				const BOOL oldBridge = m_bridgePanel;
				const int oldPhase = m_lineAnimPhase;
				m_bridgePanel = FALSE;
				m_lineAnimPhase = 0;
				PaintToDC(srcDC);
				m_bridgePanel = oldBridge;
				m_lineAnimPhase = oldPhase;
			}
			CBitmap* obL = largeDC.SelectObject(&largeBmp);
			{
				CRect lr(0, 0, fw + 2 * pad, fh + 2 * pad);
				largeDC.FillSolidRect(&lr, kChipChromaKey);
				::BitBlt(largeDC.GetSafeHdc(), pad, pad, fw, fh, srcDC.GetSafeHdc(), 0, 0, SRCCOPY);
			}
			const POINT dst = { wr.left - pad, wr.top - pad };
			PresentChipLayered(largeDC.GetSafeHdc(), fw + 2 * pad, fh + 2 * pad, &dst);
			largeDC.SelectObject(obL);
			srcDC.SelectObject(obS);
		}
	}

	m_flightPad = pad;
	m_lineAnimOriginY += pad;
	SetWindowPos(NULL, wr.left - pad, wr.top - pad, fw + 2 * pad, fh + 2 * pad,
		SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
}

void CCustomPopupMenu::EndChipFlight()
{
	if (!GetSafeHwnd()) return;
	if (m_flightPad > 0) {
		const int pad = m_flightPad;
		CRect wr; GetWindowRect(&wr);
		m_lineAnimOriginY -= pad;
		m_flightPad = 0;
		// PresentChipLayered(optDst) 済みだと HWND は既に最終位置。
		// 飛行サイズのときだけ +pad（二重加算すると一枚状態が右へずれる）。
		const int flightW = m_menuW + 2 * pad;
		const int flightH = m_menuH + 2 * pad;
		int x = wr.left;
		int y = wr.top;
		if (wr.Width() == flightW && wr.Height() == flightH) {
			x = wr.left + pad;
			y = wr.top + pad;
		}
		SetWindowPos(NULL, x, y, m_menuW, m_menuH,
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
	}
	::SetWindowRgn(m_hWnd, NULL, FALSE);
}

BOOL CCustomPopupMenu::PresentChipLayered(HDC hdcSrc, int w, int h, const POINT* optDst)
{
	if (!GetSafeHwnd() || !hdcSrc || w <= 0 || h <= 0) return FALSE;

	CRect wr;
	GetWindowRect(&wr);
	POINT ptDst = { wr.left, wr.top };
	if (optDst) ptDst = *optDst;
	// optDst 指定時は最終サイズそのもの。通常飛行は窓に合わせて余白を透明埋め
	const int outW = optDst ? w : max(w, wr.Width());
	const int outH = optDst ? h : max(h, wr.Height());

	BITMAPINFO bmi;
	ZeroMemory(&bmi, sizeof(bmi));
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = outW;
	bmi.bmiHeader.biHeight = -outH; // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* bits = NULL;
	HDC hdcDib = ::CreateCompatibleDC(hdcSrc);
	if (!hdcDib) return FALSE;
	HBITMAP hbmp = ::CreateDIBSection(hdcDib, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (!hbmp || !bits) {
		::DeleteDC(hdcDib);
		return FALSE;
	}
	HGDIOBJ old = ::SelectObject(hdcDib, hbmp);
	{
		CDC wrap;
		wrap.Attach(hdcDib);
		wrap.FillSolidRect(0, 0, outW, outH, kChipChromaKey);
		wrap.Detach();
	}
	::BitBlt(hdcDib, 0, 0, w, h, hdcSrc, 0, 0, SRCCOPY);

	const BYTE keyR = GetRValue(kChipChromaKey);
	const BYTE keyG = GetGValue(kChipChromaKey);
	const BYTE keyB = GetBValue(kChipChromaKey);
	BYTE* p = (BYTE*)bits;
	const int n = outW * outH;
	for (int i = 0; i < n; ++i, p += 4) {
		if (p[0] == keyB && p[1] == keyG && p[2] == keyR) {
			p[0] = p[1] = p[2] = p[3] = 0;
		} else {
			p[3] = 255;
		}
	}

	POINT ptSrc = { 0, 0 };
	SIZE size = { outW, outH };
	BLENDFUNCTION bf = {};
	bf.BlendOp = AC_SRC_OVER;
	bf.SourceConstantAlpha = 255;
	bf.AlphaFormat = AC_SRC_ALPHA;
	const BOOL ok = ::UpdateLayeredWindow(m_hWnd, NULL, &ptDst, &size, hdcDib, &ptSrc, 0, &bf, ULW_ALPHA);

	::SelectObject(hdcDib, old);
	::DeleteObject(hbmp);
	::DeleteDC(hdcDib);
	return ok;
}

void CCustomPopupMenu::BlitOpaqueToWindow(HDC hdcSrc, int w, int h)
{
	if (!GetSafeHwnd() || !hdcSrc || w <= 0 || h <= 0) return;
	CClientDC dc(this);
	BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
	params.dwFlags = BPPF_ERASE;
	CRect rc(0, 0, w, h);
	HDC hdcBuf = NULL;
	HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &rc, BPBF_TOPDOWNDIB, &params, &hdcBuf);
	if (hdcBuf && hBP) {
		::BitBlt(hdcBuf, 0, 0, w, h, hdcSrc, 0, 0, SRCCOPY);
		::BufferedPaintMakeOpaque(hBP, &rc);
		::EndBufferedPaint(hBP, TRUE);
	} else {
		::BitBlt(dc.GetSafeHdc(), 0, 0, w, h, hdcSrc, 0, 0, SRCCOPY);
	}
}

void CCustomPopupMenu::ForceChipPresent()
{
	if (!GetSafeHwnd()) return;
	if (!(GetExStyle() & WS_EX_LAYERED))
		ModifyStyleEx(0, WS_EX_LAYERED);
	CRect rc; GetClientRect(&rc);
	if (rc.Width() <= 0 || rc.Height() <= 0) return;
	CClientDC dc(this);
	CDC mDC; mDC.CreateCompatibleDC(&dc);
	CBitmap bmp;
	if (!bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height())) return;
	CBitmap* ob = mDC.SelectObject(&bmp);
	mDC.FillSolidRect(&rc, (m_lineAnimPhase != 0) ? kChipChromaKey : PopupBg());
	PaintToDC(mDC);
	PresentChipLayered(mDC.GetSafeHdc(), rc.Width(), rc.Height());
	mDC.SelectObject(ob);
}

void CCustomPopupMenu::CommitChipFlightSettle()
{
	if (!GetSafeHwnd()) return;
	if (m_lineAnimPhase == 0 && m_flightPad == 0 && !(GetExStyle() & WS_EX_LAYERED)) {
		RevealEmbeddedAfterAnim();
		return;
	}

	// 定着描画は必ず phase=0（オフセット無し）で焼く。旧実装は phase=1 のまま
	// 焼いてから 0 にしていたため、ULW解除後も「アニメ終了に見えるが操作不能」になった。
	m_bridgePanel = TRUE;
	m_lineAnimPhase = 0;

	const int pad = m_flightPad;
	const int fw = m_menuW;
	const int fh = m_menuH;
	CRect rc; GetClientRect(&rc);
	CClientDC dc(this);
	CDC largeDC; largeDC.CreateCompatibleDC(&dc);
	CBitmap largeBmp;
	CDC finalDC; finalDC.CreateCompatibleDC(&dc);
	CBitmap finalBmp;
	CBitmap* obL = NULL;
	CBitmap* obF = NULL;
	BOOL haveFinal = FALSE;
	if (fw > 0 && fh > 0 && rc.Width() > 0 && rc.Height() > 0
		&& largeBmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height())
		&& finalBmp.CreateCompatibleBitmap(&dc, fw, fh)) {
		obL = largeDC.SelectObject(&largeBmp);
		largeDC.FillSolidRect(&rc, kChipChromaKey);
		PaintToDC(largeDC);
		obF = finalDC.SelectObject(&finalBmp);
		::BitBlt(finalDC.GetSafeHdc(), 0, 0, fw, fh, largeDC.GetSafeHdc(), pad, pad, SRCCOPY);
		haveFinal = TRUE;
	}

	m_bridgePanel = FALSE;

	// ULW のまま Present→解除だと GDI 面が空のまま残り、代理描画（白コンボ等）が
	// マウス移動まで張り付く。先に畳んでレイヤを外し、不透明 GDI へ同期焼き込みする。
	EndChipFlight();
	if (GetExStyle() & WS_EX_LAYERED)
		ModifyStyleEx(WS_EX_LAYERED, 0);
	if (haveFinal)
		BlitOpaqueToWindow(finalDC.GetSafeHdc(), fw, fh);

	if (obF) finalDC.SelectObject(obF);
	if (obL) largeDC.SelectObject(obL);

	// レイヤ解除 → 子表示 → 親背景のみ1回。全窓 RedrawWindow は点滅の元なので使わない。
	RevealEmbeddedAfterAnim();
	InvalidateBgOnly();
	UpdateWindow();
	RefreshEmbeddedChildren();
}

void CCustomPopupMenu::InvalidateBgOnly()
{
	if (!GetSafeHwnd()) return;
	CRect rc; GetClientRect(&rc);
	CRgn rgn;
	if (!rgn.CreateRectRgnIndirect(&rc)) {
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_NOCHILDREN);
		return;
	}
	for (HWND h = ::GetWindow(m_hWnd, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT)) {
		if (!::IsWindowVisible(h)) continue;
		CRect cr; ::GetWindowRect(h, &cr); ScreenToClient(&cr);
		CRgn child;
		if (child.CreateRectRgnIndirect(&cr))
			rgn.CombineRgn(&rgn, &child, RGN_DIFF);
	}
	InvalidateRgn(&rgn, FALSE);
}

void CCustomPopupMenu::SetScrollY(int y)
{
	if (y < 0) y = 0;
	if (y > m_scrollMax) y = m_scrollMax;
	if (y == m_scrollY) return;
	m_scrollY = y;
	// 先に子を新位置へ。親は子領域を除外して塗る（InvalidateRect 全塗りは BufferedPaint で子を潰す）
	SyncEmbeddedChildren();
	InvalidateBgOnly();
	UpdateWindow();
	RefreshEmbeddedChildren();
}

BOOL CCustomPopupMenu::OnWheelDelta(int delta)
{
	if (m_scrollMax <= 0) return FALSE;
	const int step = PopupSx(PopupDpiFromHwnd(m_hWnd), CCUSTOM_POPUP_SCROLL_STEP);
	const int steps = (delta > 0) ? -step : step;
	// 複数ノッチ
	const int notches = max(1, abs(delta) / WHEEL_DELTA);
	SetScrollY(m_scrollY + steps * notches);
	return TRUE;
}

BOOL CCustomPopupMenu::HandleWheelInChain(CPoint screenPt, int delta)
{
	if (GetSafeHwnd()) {
		CRect r; GetWindowRect(&r);
		if (r.PtInRect(screenPt))
			return OnWheelDelta(delta);
	}
	if (m_openSub >= 0 && m_openSub < m_itemCount) {
		const int si = m_items[m_openSub].subIndex;
		if (si >= 0 && si < m_subCount && m_subs[si])
			return m_subs[si]->HandleWheelInChain(screenPt, delta);
	}
	return FALSE;
}

void CCustomPopupMenu::StartCheckBounce(int idx)
{
	if (idx < 0 || idx >= m_itemCount) return;
	m_bounceIdx = idx;
	m_nBounce = 8;
	if (GetSafeHwnd())
		SetTimer(kBounceTimer, 28, NULL);
	InvalidateBgOnly();
}

void CCustomPopupMenu::SnapAnimToIdle()
{
	if (!GetSafeHwnd() || m_lineAnimPhase == 0) return;
	KillTimer(kAnimTimer);
	KillTimer(kSettleTimer);
	if (UsesRowChipFlight(PopupAnimStyle())
		&& (m_flightPad > 0 || (GetExStyle() & WS_EX_LAYERED))) {
		CommitChipFlightSettle();
	} else {
		m_lineAnimPhase = 0;
		m_bridgePanel = FALSE;
		::SetWindowRgn(m_hWnd, NULL, TRUE);
		RevealEmbeddedAfterAnim();
		InvalidateBgOnly();
		UpdateWindow();
		RefreshEmbeddedChildren();
	}
	// マウスを動かさなくても、現在カーソル下の行をホバー同期（OnMouseMove 相当）
	{
		CPoint sp;
		::GetCursorPos(&sp);
		ScreenToClient(&sp);
		const int idx = HitTest(sp);
		if (idx != m_hot)
			SetHot(idx);
	}
	SetTimer(kAnimTimer, 50, NULL);
}

void CCustomPopupMenu::OpenSubAt(int idx)
{
	if (idx < 0 || idx >= m_itemCount) return;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_SUB || it.subIndex < 0 || !it.enabled) return;
	if (m_openSub == idx) return;
	// 出現アニメ中にサブを選んだら止めて一枚状態へ（飛行余白だと座標も狂う）
	if (m_lineAnimPhase != 0)
		SnapAnimToIdle();
	CloseOpenSub();
	m_openSub = idx;
	CCustomPopupMenu* sub = m_subs[it.subIndex];
	if (!sub) return;
	sub->m_owner = m_owner;
	CRect wr; GetWindowRect(&wr);
	CRect vr = ItemViewRect(idx);
	sub->CreatePopupAt(CPoint(wr.right - 2, wr.top + vr.top), this, RootMenu());
}

void CCustomPopupMenu::SetHot(int idx)
{
	if (idx == m_hot) return;
	m_hot = idx;
	// Invalidate(FALSE) は BufferedPaint が子 HWND を塗り潰す。背景のみ。
	InvalidateBgOnly();
	UpdateTip();
	if (idx >= 0 && idx < m_itemCount) {
		const CCustomPopupItem& it = m_items[idx];
		if (it.id == CCUSTOM_POPUP_ID_FONT_FACE && it.text[0])
			ApplyPreviewFace(it.text);
		// ホバー行が変わったら開いているサブは即破棄。サブ行ならそこから再オープン（アニメ付き）。
		// HitTest は定着座標基準なので、飛行中の重なりで非サブ→Snap 誤爆しない。
		if (it.kind == CCUSTOM_POPUP_SUB && it.enabled)
			OpenSubAt(idx);
		else if (m_openSub >= 0)
			CloseOpenSub();
	} else if (m_openSub >= 0) {
		CloseOpenSub();
	}
}

void CCustomPopupMenu::UpdateTip()
{
	m_tipHot = m_hot;
	if (m_tip.GetSafeHwnd()) m_tip.SendMessage(TTM_UPDATE, 0, 0);
}

int CCustomPopupMenu::HitTest(CPoint pt) const
{
	// ホバー／クリックは常に定着レイアウト座標で判定。
	// 飛行中の見た目矩形は重なるため、非サブ上でも SUB と誤判定→Snap 誤爆になる。
	auto hitRow = [&](int i) -> BOOL {
		if (m_items[i].kind == CCUSTOM_POPUP_SEP) return FALSE;
		CRect hit = ItemViewRect(i);
		if (m_flightPad > 0)
			hit.SetRect(m_flightPad, hit.top, m_flightPad + m_menuW, hit.bottom);
		return hit.PtInRect(pt) ? TRUE : FALSE;
	};
	if (m_stickyCount > 0 && pt.y < m_stickyH + m_flightPad) {
		for (int i = 0; i < m_stickyCount; ++i) {
			if (hitRow(i)) return i;
		}
		return -1;
	}
	for (int i = m_stickyCount; i < m_itemCount; ++i) {
		if (hitRow(i)) return i;
	}
	return -1;
}

void CCustomPopupMenu::PaintToDC(CDC& dc)
{
	CRect rc; GetClientRect(&rc);
	HFONT oldFont = (HFONT)dc.SelectObject(m_font ? m_font : (HFONT)::GetStockObject(DEFAULT_GUI_FONT));
	dc.SetBkMode(TRANSPARENT);
	const UINT dpi = PopupDpiFromHwnd(m_hWnd);
	const int padX = PopupSx(dpi, CCUSTOM_POPUP_PAD_X);
	const int padR = PopupSx(dpi, CCUSTOM_POPUP_PAD_RIGHT);
	const int checkW = PopupSx(dpi, CCUSTOM_POPUP_CHECK_W);
	const int arrowW = PopupSx(dpi, CCUSTOM_POPUP_ARROW_W);
	const int labelBand = PopupSx(dpi, 18);
	const int labelTop = PopupSx(dpi, 2);

	auto paintItem = [&](int i) {
		const CCustomPopupItem& it = m_items[i];
		int ox = 0, oy = 0, fade = 256;
		// 橋渡し一枚パネル中はオフセット禁止（easeOutBackの行き過ぎが点滅に見える）
		if (!m_bridgePanel) {
			if (!CalcLineAnim(i, &ox, &oy, &fade))
				return;
		}
		if (fade < 20)
			return;
		CRect vr = ItemViewRect(i);
		vr.OffsetRect(ox, oy);
		if (vr.bottom < 0 || vr.top > rc.bottom) return;

		if (it.kind == CCUSTOM_POPUP_SEP) {
			if (fade >= 48) DrawCuteSep(dc, vr, dpi);
			return;
		}

		const BOOL interactive = IsInteractiveKind(it.kind);
		const BOOL hot = (i == m_hot && it.enabled && !interactive
			&& (m_lineAnimPhase == 0 || ChipFlightRowsAtRest()));
		if (hot) DrawHotPill(dc, vr);

		const COLORREF bgRef = PopupBg();
		auto faded = [&](COLORREF c) -> COLORREF {
			if (fade >= 250) return c;
			return BlendRGB(bgRef, c, fade);
		};

		CRect tr = vr;
		tr.left += padX;
		tr.right -= padR;

		{
			CRect cr(tr.left, tr.top, tr.left + checkW, tr.bottom);
			if (it.kind == CCUSTOM_POPUP_CHECK && it.checked && fade >= 80) {
				CRect bounce = cr;
				bounce.InflateRect(PopupSx(dpi, 2), PopupSx(dpi, 2));
				if (i == m_bounceIdx && m_nBounce > 0) {
					const double bf = sin(3.14159265 * (8 - m_nBounce) / 8.0);
					bounce.InflateRect((int)(bounce.Width() * 0.20 * bf), (int)(bounce.Height() * 0.20 * bf));
				}
				DrawRedCheck(dc, bounce);
			}
			tr.left += checkW;
		}

		if (interactive) {
			if (it.kind != CCUSTOM_POPUP_BUTTON) {
				CRect lr = vr;
				lr.left = vr.left + padX + checkW;
				lr.right = vr.right - padR;
				lr.top = vr.top + labelTop;
				lr.bottom = vr.top + labelBand;
				wchar_t line[CCUSTOM_POPUP_TEXT_LEN + 32];
				if (it.kind == CCUSTOM_POPUP_RANGE && it.sliderMax > it.sliderMin) {
					const int pct = MulDiv(it.sliderPos - it.sliderMin, 100, it.sliderMax - it.sliderMin);
					_snwprintf_s(line, _TRUNCATE, L"%s  (%d%%)", it.text, pct);
				} else if (it.kind == CCUSTOM_POPUP_SLIDER || it.kind == CCUSTOM_POPUP_RANGE)
					_snwprintf_s(line, _TRUNCATE, L"%s  (%d)", it.text, it.sliderPos);
				else if (it.kind == CCUSTOM_POPUP_PROGRESS)
					_snwprintf_s(line, _TRUNCATE, L"%s  (%d)", it.text, it.sliderPos);
				else
					_snwprintf_s(line, _TRUNCATE, L"%s", it.text);
				DrawPopupItemText(dc, line, lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
					faded(PopupText(TRUE)), TRUE, fade);
			}
			// 飛行／退場中は子HWNDを隠すので、チップ内にコントロール外形を描く
			if (m_lineAnimPhase != 0)
				DrawInteractiveProxy(dc, it, vr, fade, bgRef, dpi);
			return;
		}

		HFONT rowFont = NULL;
		HFONT prev = NULL;
		if (it.id == CCUSTOM_POPUP_ID_FONT_FACE && it.text[0]) {
			LOGFONTW lf; ZeroMemory(&lf, sizeof(lf));
			HDC hdc = dc.GetSafeHdc();
			lf.lfHeight = -MulDiv(max(9, savedata.popupMenuPoint), ::GetDeviceCaps(hdc, LOGPIXELSY), 72);
			lf.lfWeight = savedata.popupMenuBold ? FW_BOLD : FW_NORMAL;
			lf.lfItalic = savedata.popupMenuItalic ? 1 : 0;
			lf.lfCharSet = DEFAULT_CHARSET;
			lf.lfQuality = CLEARTYPE_QUALITY;
			lstrcpynW(lf.lfFaceName, it.text, LF_FACESIZE);
			rowFont = ::CreateFontIndirectW(&lf);
			if (rowFont) prev = (HFONT)dc.SelectObject(rowFont);
		}

		UINT dt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
		if (it.kind == CCUSTOM_POPUP_SUB)
			tr.right -= arrowW;
		DrawPopupItemText(dc, it.text, tr, dt, faded(PopupText(it.enabled)), it.enabled, fade);
		if (it.kind == CCUSTOM_POPUP_SUB) {
			CRect ar = vr;
			ar.left = vr.right - padR - arrowW;
			ar.right = vr.right - padR;
			DrawPopupItemText(dc, hot ? L"▹" : L"▸", ar, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
				faded(hot ? RGB(130, 70, 160) : PopupText(it.enabled)), it.enabled, fade);
		}

		if (rowFont) {
			dc.SelectObject(prev ? prev : (m_font ? m_font : (HFONT)::GetStockObject(DEFAULT_GUI_FONT)));
			::DeleteObject(rowFont);
		}
	};

	// ---- 行アニメ中 ----
	if (m_lineAnimPhase != 0) {
		const int animStyle = PopupAnimStyle();

		// 行チップ飛行: 通常はチップ。橋渡しフレだけ一枚パネル（atRest判定禁止＝行き過ぎで点滅する）
		if (UsesRowChipFlight(animStyle)) {
			dc.FillSolidRect(&rc, kChipChromaKey);
			// 全行が定位置にいる／橋渡し中は一枚パネル（行枠のまま放置しない）
			const BOOL paintAsPanel = m_bridgePanel || ChipFlightRowsAtRest();
			if (paintAsPanel) {
				CRect content(m_flightPad, m_flightPad, m_flightPad + m_menuW, m_flightPad + m_menuH);
				if (content.Width() <= 1 || content.Height() <= 1)
					content = rc;
				const int clipSave = dc.SaveDC();
				dc.IntersectClipRect(&content);
				DrawJkBackdrop(dc, content, m_animTick);
				DrawTornRibbon(dc, content, m_animTick);
				for (int i = 0; i < m_itemCount; ++i)
					paintItem(i);
				DrawPanelChrome(dc, content);
				dc.RestoreDC(clipSave);
			} else {
				for (int i = 0; i < m_itemCount; ++i) {
					int ox = 0, oy = 0, fade = 0;
					if (!CalcLineAnim(i, &ox, &oy, &fade) || fade < 8)
						continue;
					CRect vr = ItemViewRect(i);
					CRect chip(m_flightPad, vr.top, m_flightPad + m_menuW, vr.bottom);
					chip.OffsetRect(ox, oy);
					DrawRowChip(dc, chip, m_animTick, fade);
					paintItem(i);
				}
			}
			dc.SelectObject(oldFont);
			return;
		}

		// 他方式: 背景は一枚だけ（行ごとのずらし背景＝ぐちゃつきの元なので禁止）
		dc.FillSolidRect(&rc, PopupBg());
		CRect hull(0, 0, 0, 0);
		BOOL hasHull = FALSE;

		if (animStyle == POPUP_ANIM_EXPAND && !m_asSubmenu) {
			for (int i = 0; i < m_itemCount; ++i) {
				int ox = 0, oy = 0, fade = 0;
				if (!CalcLineAnim(i, &ox, &oy, &fade) || fade < 8)
					continue;
				CRect vr = ItemViewRect(i);
				CRect band(rc.left, vr.top, rc.right, vr.bottom);
				band.InflateRect(0, 2);
				if (!band.IntersectRect(&band, &rc) || band.Height() <= 0)
					continue;
				if (!hasHull) { hull = band; hasHull = TRUE; }
				else hull.UnionRect(&hull, &band);
			}
			if (hasHull) {
				hull.left = rc.left;
				hull.right = rc.right;
				if (m_lineAnimOriginY < hull.top) hull.top = max(rc.top, m_lineAnimOriginY - 4);
				if (m_lineAnimOriginY > hull.bottom) hull.bottom = min(rc.bottom, m_lineAnimOriginY + 4);
				hull.InflateRect(0, 1);
				hull.IntersectRect(&hull, &rc);
			}
		} else {
			hull = rc;
			hasHull = TRUE;
		}

		if (hasHull) {
			const int clipSave = dc.SaveDC();
			dc.IntersectClipRect(&hull);
			DrawJkBackdrop(dc, rc, m_animTick);
			DrawTornRibbon(dc, rc, m_animTick);
			if (animStyle == POPUP_ANIM_EXPAND && !m_asSubmenu) {
				const COLORREF tip = CCC_IsInwoman() ? RGB(255, 190, 220) : RGB(210, 200, 255);
				dc.FillSolidRect(hull.left + CCUSTOM_POPUP_RIBBON_W + 2, hull.top, hull.Width() - CCUSTOM_POPUP_RIBBON_W - 4, 1,
					BlendRGB(PopupBg(), tip, 140));
				dc.FillSolidRect(hull.left + CCUSTOM_POPUP_RIBBON_W + 2, hull.bottom - 1, hull.Width() - CCUSTOM_POPUP_RIBBON_W - 4, 1,
					BlendRGB(PopupBg(), tip, 140));
			}
			dc.RestoreDC(clipSave);
		}

		if (hasHull) {
			const int clipItems = dc.SaveDC();
			dc.IntersectClipRect(&hull);
			for (int i = 0; i < m_itemCount; ++i)
				paintItem(i);
			dc.RestoreDC(clipItems);
		} else {
			for (int i = 0; i < m_itemCount; ++i)
				paintItem(i);
		}

		if (hasHull) {
			if (animStyle == POPUP_ANIM_EXPAND && !m_asSubmenu)
				DrawPanelChrome(dc, hull);
			else
				DrawPanelChrome(dc, rc);
		}

		if (m_stickyCount > 0 && m_stickyH > 0) {
			int ox0 = 0, oy0 = 0, fade0 = 0;
			if (CalcLineAnim(0, &ox0, &oy0, &fade0) && fade0 > 180) {
				dc.FillSolidRect(rc.left + CCUSTOM_POPUP_RIBBON_W + 6 + ox0, m_stickyH - 2 + oy0,
					rc.Width() - CCUSTOM_POPUP_RIBBON_W - 14, 1, RGB(255, 255, 255));
				dc.FillSolidRect(rc.left + CCUSTOM_POPUP_RIBBON_W + 6 + ox0, m_stickyH - 1 + oy0,
					rc.Width() - CCUSTOM_POPUP_RIBBON_W - 14, 1, RGB(240, 170, 200));
			}
		}
		if (hasHull) {
			const int iwSave = dc.SaveDC();
			dc.IntersectClipRect(&hull);
			CCC_DrawInwoman(&dc, rc, FALSE);
			dc.RestoreDC(iwSave);
		}
		dc.SelectObject(oldFont);
		return;
	}

	// ---- 通常: 一枚のパネル ----
	DrawJkBackdrop(dc, rc, m_animTick);
	DrawTornRibbon(dc, rc, m_animTick);

	if (m_stickyCount > 0 && m_stickyH > 0) {
		for (int i = 0; i < m_stickyCount; ++i)
			paintItem(i);
		dc.FillSolidRect(rc.left + CCUSTOM_POPUP_RIBBON_W + 6, m_stickyH - 2, rc.Width() - CCUSTOM_POPUP_RIBBON_W - 14, 1, RGB(255, 255, 255));
		dc.FillSolidRect(rc.left + CCUSTOM_POPUP_RIBBON_W + 6, m_stickyH - 1, rc.Width() - CCUSTOM_POPUP_RIBBON_W - 14, 1, RGB(240, 170, 200));
	}

	{
		const int clipSave = dc.SaveDC();
		CRect bodyClip(rc.left, m_stickyH, rc.right, rc.bottom);
		dc.IntersectClipRect(&bodyClip);
		for (int i = m_stickyCount; i < m_itemCount; ++i)
			paintItem(i);
		dc.RestoreDC(clipSave);
	}

	DrawPanelChrome(dc, rc);

	if (m_scrollMax > 0 && m_contentH > m_stickyH) {
		const int trackTop = m_stickyH + 4;
		const int trackH = max(8, rc.Height() - trackTop - 4);
		const int bodyContent = max(1, m_contentH - m_stickyH);
		const int bodyView = max(1, m_menuH - m_stickyH);
		const int thumbH = max(16, (int)((LONGLONG)trackH * bodyView / bodyContent));
		const int thumbY = trackTop + (m_scrollMax > 0
			? (int)((LONGLONG)(trackH - thumbH) * m_scrollY / m_scrollMax) : 0);
		const int x = rc.right - 7;
		dc.FillSolidRect(x, trackTop, 3, trackH, RGB(255, 210, 230));
		dc.FillSolidRect(x, thumbY, 3, thumbH, RGB(255, 120, 180));
	}

	CCC_DrawInwoman(&dc, rc, FALSE);
	dc.SelectObject(oldFont);
}

void CCustomPopupMenu::OnPaint()
{
	CPaintDC dc(this);
	CRect r; GetClientRect(&r);
	if (r.Width() <= 0 || r.Height() <= 0) return;

	// 子 HWND をクリップから除外（rcPaint は外接矩形なので InvalidateRgn だけでは足りない）
	CRgn exclude;
	BOOL haveExclude = FALSE;
	for (HWND h = ::GetWindow(m_hWnd, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT)) {
		if (!::IsWindowVisible(h)) continue;
		CRect cr; ::GetWindowRect(h, &cr); ScreenToClient(&cr);
		if (!haveExclude) {
			haveExclude = exclude.CreateRectRgnIndirect(&cr);
		} else {
			CRgn child;
			if (child.CreateRectRgnIndirect(&cr))
				exclude.CombineRgn(&exclude, &child, RGN_OR);
		}
	}
	if (haveExclude)
		dc.SelectClipRgn(&exclude, RGN_DIFF);

	CDC mDC; mDC.CreateCompatibleDC(&dc);
	if (!m_memBmp.GetSafeHandle() || m_memW != r.Width() || m_memH != r.Height()) {
		if (m_memBmp.GetSafeHandle()) m_memBmp.DeleteObject();
		m_memBmp.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
		m_memW = r.Width(); m_memH = r.Height();
	}
	CBitmap* ob = mDC.SelectObject(&m_memBmp);
	const BOOL chipFlight = (m_lineAnimPhase != 0 && UsesRowChipFlight(PopupAnimStyle()));
	mDC.FillSolidRect(&r, chipFlight ? kChipChromaKey : PopupBg());
	PaintToDC(mDC);

	if (chipFlight) {
		PresentChipLayered(mDC.GetSafeHdc(), r.Width(), r.Height());
		mDC.SelectObject(ob);
		return;
	}

	// Win11 では素の BitBlt が α=0 になり得る → BufferedPaint で不透明化
	BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
	params.dwFlags = BPPF_ERASE;
	HDC hdcBuf = NULL;
	const CRect& pr = dc.m_ps.rcPaint;
	HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &pr, BPBF_TOPDOWNDIB, &params, &hdcBuf);
	BOOL usedBuffered = FALSE;
	if (hdcBuf && hBP) {
		::BitBlt(hdcBuf, 0, 0, pr.Width(), pr.Height(), mDC.GetSafeHdc(), pr.left, pr.top, SRCCOPY);
		::BufferedPaintMakeOpaque(hBP, NULL);
		::EndBufferedPaint(hBP, TRUE);
		usedBuffered = TRUE;
	} else {
		dc.BitBlt(0, 0, r.Width(), r.Height(), &mDC, 0, 0, SRCCOPY);
	}
	mDC.SelectObject(ob);
	// BeginBufferedPaint + MakeOpaque がクリップを無視して子を潰すことがある
	if (usedBuffered)
		RefreshEmbeddedChildren();
}

BOOL CCustomPopupMenu::OnEraseBkgnd(CDC*) { return TRUE; }

LRESULT CCustomPopupMenu::OnRefreshEmbedded(WPARAM, LPARAM)
{
	if (!GetSafeHwnd() || m_lineAnimPhase != 0)
		return 0;
	// Sync は MoveWindow/Invalidate 連打で点滅するので、潰された子の再描画だけ
	RefreshEmbeddedChildren();
	return 0;
}

LRESULT CCustomPopupMenu::OnPrintClient(WPARAM wParam, LPARAM)
{
	if (HDC hdc = (HDC)wParam) {
		CRect r; GetClientRect(&r);
		const BOOL chipFlight = (m_lineAnimPhase != 0 && UsesRowChipFlight(PopupAnimStyle()));
		if (chipFlight) {
			CDC dc; dc.Attach(hdc);
			dc.FillSolidRect(&r, kChipChromaKey);
			PaintToDC(dc);
			dc.Detach();
			return 0;
		}
		BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
		params.dwFlags = BPPF_ERASE;
		HDC hdcBuf = NULL;
		HPAINTBUFFER hBP = ::BeginBufferedPaint(hdc, &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
		if (hdcBuf && hBP) {
			CDC dcBuf; dcBuf.Attach(hdcBuf); PaintToDC(dcBuf); dcBuf.Detach();
			::BufferedPaintMakeOpaque(hBP, &r); ::EndBufferedPaint(hBP, TRUE); return 0;
		}
		CDC dc; dc.Attach(hdc); PaintToDC(dc); dc.Detach();
	}
	return 0;
}

LRESULT CCustomPopupMenu::OnNcHitTest(CPoint point)
{
	// サブの飛行余白HWNDは親メニューを覆う。親項目上は透過して親の SetHot→サブ破棄へ渡す。
	for (CCustomPopupMenu* p = m_parentMenu; p; p = p->m_parentMenu) {
		if (!p->GetSafeHwnd()) continue;
		CRect wr; p->GetWindowRect(&wr);
		if (wr.PtInRect(point))
			return HTTRANSPARENT;
	}
	// 飛行パッドの穴（チップも定着行も無い）も下へ通す
	if (m_flightPad > 0 && m_lineAnimPhase != 0 && UsesRowChipFlight(PopupAnimStyle())) {
		CPoint c = point;
		ScreenToClient(&c);
		if (HitTest(c) >= 0)
			return HTCLIENT;
		for (int i = 0; i < m_itemCount; ++i) {
			if (m_items[i].kind == CCUSTOM_POPUP_SEP) continue;
			int ox = 0, oy = 0, fade = 256;
			if (!CalcLineAnim(i, &ox, &oy, &fade) || fade < 20)
				continue;
			CRect vr = ItemViewRect(i);
			CRect hit(m_flightPad, vr.top, m_flightPad + m_menuW, vr.bottom);
			hit.OffsetRect(ox, oy);
			if (hit.PtInRect(c))
				return HTCLIENT;
		}
		return HTTRANSPARENT;
	}
	return CWnd::OnNcHitTest(point);
}

void CCustomPopupMenu::OnMouseMove(UINT nFlags, CPoint point)
{
	// タイマ定着が遅れても、操作開始で入場アニメを終わらせる（ホバーハイライト不能の防止）
	if (m_lineAnimPhase == 1) {
		const int style = PopupAnimStyle();
		const int total = ChipEnterTotalMs(style, m_asSubmenu, m_itemCount, m_lineAnimOrigin);
		const int elapsed = (int)(GetTickCount64() - m_lineAnimStart);
		if (elapsed >= total || ChipFlightRowsAtRest())
			SnapAnimToIdle();
	}
	TRACKMOUSEEVENT tme = { sizeof(tme) };
	tme.dwFlags = TME_LEAVE; tme.hwndTrack = m_hWnd; ::_TrackMouseEvent(&tme);
	SetHot(HitTest(point));
	CWnd::OnMouseMove(nFlags, point);
}

void CCustomPopupMenu::OnMouseLeave()
{
	if (m_hot >= 0 && m_items[m_hot].kind != CCUSTOM_POPUP_SUB) {
		const BOOL wasFace = (m_items[m_hot].id == CCUSTOM_POPUP_ID_FONT_FACE);
		m_hot = -1;
		InvalidateBgOnly();
		if (wasFace) ClearPreviewFace();
	}
}

BOOL CCustomPopupMenu::HandleChromeClick(int idx)
{
	if (idx < 0 || idx >= m_itemCount) return FALSE;
	CCustomPopupItem& it = m_items[idx];
	if (it.id == CCUSTOM_POPUP_ID_ACRYLIC) {
		savedata.aero = (savedata.aero == 1) ? 0 : 1;
		it.checked = (savedata.aero == 1);
		if (it.checked) StartCheckBounce(idx);
		CCC_NotifyAeroSettingChanged();
		CloseChain(0);
		return TRUE;
	}
	if (it.id == CCUSTOM_POPUP_ID_FONT_BOLD) {
		savedata.popupMenuBold = savedata.popupMenuBold ? 0 : 1;
		it.checked = savedata.popupMenuBold ? TRUE : FALSE;
		if (it.checked) StartCheckBounce(idx);
		PersistPopupFont();
		RefreshFontChain();
		RelayoutOpenChain();
		InvalidateBgOnly();
		return TRUE;
	}
	if (it.id == CCUSTOM_POPUP_ID_FONT_ITALIC) {
		savedata.popupMenuItalic = savedata.popupMenuItalic ? 0 : 1;
		it.checked = savedata.popupMenuItalic ? TRUE : FALSE;
		if (it.checked) StartCheckBounce(idx);
		PersistPopupFont();
		RefreshFontChain();
		RelayoutOpenChain();
		InvalidateBgOnly();
		return TRUE;
	}
	if (it.id == CCUSTOM_POPUP_ID_FONT_FACE) {
		CommitFace(it.text);
		CloseChain(0);
		return TRUE;
	}
	if (it.id >= CCUSTOM_POPUP_ID_ANIM0
		&& it.id < CCUSTOM_POPUP_ID_ANIM0 + (UINT)POPUP_ANIM_COUNT) {
		const int style = (int)(it.id - CCUSTOM_POPUP_ID_ANIM0);
		savedata.popupMenuAnim = style;
		ClampPopupAnimSave();
		MpPersistSavedataQuick();
		for (int i = 0; i < m_itemCount; ++i) {
			if (m_items[i].id < CCUSTOM_POPUP_ID_ANIM0
				|| m_items[i].id >= CCUSTOM_POPUP_ID_ANIM0 + (UINT)POPUP_ANIM_COUNT)
				continue;
			m_items[i].checked =
				((int)(m_items[i].id - CCUSTOM_POPUP_ID_ANIM0) == savedata.popupMenuAnim) ? TRUE : FALSE;
		}
		CloseChain(0);
		return TRUE;
	}
	return FALSE;
}

void CCustomPopupMenu::OnLButtonDown(UINT nFlags, CPoint point)
{
	// アニメ中も見た目ヒットで行決定。定着スナップはサブを開くときだけ（OpenSubAt内）。
	// 通常項目で毎回 Snap すると「一枚化」が誤爆アニメになる。
	const int idx = HitTest(point);
	if (idx < 0) return;
	const CCustomPopupItem& it = m_items[idx];
	if (!it.enabled) return;
	if (IsInteractiveKind(it.kind)) return;
	if (HandleChromeClick(idx)) return;
	if (it.kind == CCUSTOM_POPUP_SUB) { OpenSubAt(idx); return; }
	if (it.kind == CCUSTOM_POPUP_CHECK && !it.checked)
		StartCheckBounce(idx); // CloseChain 前の一瞬でも起動（描画フレームがあれば見える）
	if (it.kind == CCUSTOM_POPUP_CMD || it.kind == CCUSTOM_POPUP_CHECK)
		CloseChain(it.id);
	CWnd::OnLButtonDown(nFlags, point);
}

void CCustomPopupMenu::OnLButtonUp(UINT nFlags, CPoint point)
{ CWnd::OnLButtonUp(nFlags, point); }

void CCustomPopupMenu::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	CWnd* f = GetFocus();
	if (f && f != this && IsChild(f)) {
		if (nChar == VK_ESCAPE) { CloseChain(0); return; }
		CWnd::OnKeyDown(nChar, nRepCnt, nFlags); return;
	}
	if (nChar == VK_ESCAPE) { CloseChain(0); return; }
	if (nChar == VK_DOWN || nChar == VK_UP) {
		int start = m_hot;
		for (int n = 0; n < m_itemCount; ++n) {
			start = (nChar == VK_DOWN) ? (start + 1) % m_itemCount
				: ((start <= 0) ? m_itemCount - 1 : start - 1);
			if (m_items[start].kind != CCUSTOM_POPUP_SEP && m_items[start].enabled) {
				SetHot(start); break;
			}
		}
		return;
	}
	if (nChar == VK_RIGHT && m_hot >= 0 && m_items[m_hot].kind == CCUSTOM_POPUP_SUB) {
		OpenSubAt(m_hot); return;
	}
	if (nChar == VK_LEFT) {
		if (m_parentMenu) { DestroyPopupTree(); m_parentMenu->m_openSub = -1; }
		return;
	}
	if (nChar == VK_RETURN && m_hot >= 0) {
		if (HandleChromeClick(m_hot)) return;
		const CCustomPopupItem& it = m_items[m_hot];
		if (it.enabled && (it.kind == CCUSTOM_POPUP_CMD || it.kind == CCUSTOM_POPUP_CHECK))
			CloseChain(it.id);
		else if (it.enabled && it.kind == CCUSTOM_POPUP_SUB)
			OpenSubAt(m_hot);
		return;
	}
	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CCustomPopupMenu::OnKillFocus(CWnd* pNewWnd)
{
	CWnd::OnKillFocus(pNewWnd);
	if (!m_tracking) return;
	HWND nh = pNewWnd ? pNewWnd->GetSafeHwnd() : NULL;
	if (nh && IsHwndRelated(nh)) return;
	CCustomPopupMenu* root = RootMenu();
	// 画面キャプチャ等: 合成副作用の偽 KillFocus では閉じない（外側クリック/Esc で閉じる）
	if (root && PopupOwnerRelaxesDismiss(root->m_owner))
		return;
	if (root && root->m_tracking)
		root->CloseChain(0);
}

BOOL CCustomPopupMenu::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	ScreenToClient(&pt);
	if (OnWheelDelta(zDelta))
		return TRUE;
	return CWnd::OnMouseWheel(nFlags, zDelta, pt);
}

void CCustomPopupMenu::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (!pScrollBar) return;
	const HWND hwnd = pScrollBar->GetSafeHwnd();
	for (int i = 0; i < m_itemCount; ++i) {
		CCustomPopupItem& it = m_items[i];
		if (it.kind == CCUSTOM_POPUP_SLIDER && it.sliderIndex >= 0
			&& m_sliders[it.sliderIndex].GetSafeHwnd() == hwnd) {
			const int v = m_sliders[it.sliderIndex].GetPos();
			it.sliderPos = v;
			if (it.sliderCb) it.sliderCb(it.ctrlCtx, v);
			InvalidateBgOnly();
			break;
		}
		if (it.kind == CCUSTOM_POPUP_RANGE && it.rangeIndex >= 0
			&& m_ranges[it.rangeIndex].GetSafeHwnd() == hwnd) {
			NotifyRangeFromHwnd(hwnd, nSBCode);
			break;
		}
	}
	CWnd::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CCustomPopupMenu::NotifyRangeFromHwnd(HWND hwnd, UINT nSBCode)
{
	for (int i = 0; i < m_itemCount; ++i) {
		CCustomPopupItem& it = m_items[i];
		if (it.kind != CCUSTOM_POPUP_RANGE || it.rangeIndex < 0) continue;
		CCustomRangeSliderCtrl& rs = m_ranges[it.rangeIndex];
		if (rs.GetSafeHwnd() != hwnd) continue;
		it.sliderPos = rs.GetPos();
		rs.GetSelection(it.rangeSelMin, it.rangeSelMax);
		rs.GetAB(it.rangeAbA, it.rangeAbB);
		if (it.rangeCb)
			it.rangeCb(it.ctrlCtx, it.sliderPos, it.rangeSelMin, it.rangeSelMax,
				it.rangeAbA, it.rangeAbB, nSBCode, rs.GetDragTarget());
		InvalidateBgOnly();
		break;
	}
}

void CCustomPopupMenu::NotifyButtonFromHwnd(HWND hwnd)
{
	for (int i = 0; i < m_itemCount; ++i) {
		CCustomPopupItem& it = m_items[i];
		if (it.kind != CCUSTOM_POPUP_BUTTON || it.buttonIndex < 0) continue;
		if (m_buttons[it.buttonIndex].GetSafeHwnd() != hwnd) continue;
		if (it.buttonCb) it.buttonCb(it.ctrlCtx, it.id);
		if (it.buttonCloseOnClick)
			CloseChain(it.id);
		break;
	}
}

void CCustomPopupMenu::NotifyEditFromHwnd(HWND hwnd)
{
	if (m_suppressEditNotify) return;
	for (int i = 0; i < m_itemCount; ++i) {
		CCustomPopupItem& it = m_items[i];
		if (it.kind != CCUSTOM_POPUP_EDIT || it.editIndex < 0) continue;
		if (m_edits[it.editIndex].GetSafeHwnd() != hwnd) continue;
		CString s; m_edits[it.editIndex].GetWindowText(s);
		if (it.editCb) it.editCb(it.ctrlCtx, s);
		break;
	}
}

void CCustomPopupMenu::NotifyChoiceFromHwnd(HWND hwnd, BOOL fromList)
{
	for (int i = 0; i < m_itemCount; ++i) {
		CCustomPopupItem& it = m_items[i];
		if (fromList) {
			if (it.kind != CCUSTOM_POPUP_LIST || it.listIndex < 0) continue;
			if (m_lists[it.listIndex].GetSafeHwnd() != hwnd) continue;
			const int sel = m_lists[it.listIndex].GetCurSel();
			it.choiceSel = sel;
			CString s; if (sel >= 0) m_lists[it.listIndex].GetText(sel, s);
			if (it.choiceCb) it.choiceCb(it.ctrlCtx, sel, s);
			break;
		} else {
			if (it.kind != CCUSTOM_POPUP_COMBO || it.comboIndex < 0) continue;
			if (m_combos[it.comboIndex].GetSafeHwnd() != hwnd) continue;
			const int sel = m_combos[it.comboIndex].GetCurSelPhysical();
			it.choiceSel = sel;
			CString s; if (sel >= 0) m_combos[it.comboIndex].GetLBText(sel, s);
			if (it.choiceCb) it.choiceCb(it.ctrlCtx, sel, s);
			break;
		}
	}
}

BOOL CCustomPopupMenu::OnCommand(WPARAM wParam, LPARAM lParam)
{
	const UINT code = HIWORD(wParam);
	const HWND hwnd = (HWND)lParam;
	if (hwnd) {
		if (code == EN_CHANGE || code == EN_KILLFOCUS) NotifyEditFromHwnd(hwnd);
		else if (code == CBN_SELCHANGE) NotifyChoiceFromHwnd(hwnd, FALSE);
		else if (code == LBN_SELCHANGE) NotifyChoiceFromHwnd(hwnd, TRUE);
		else if (code == BN_CLICKED) NotifyButtonFromHwnd(hwnd);
	}
	return CWnd::OnCommand(wParam, lParam);
}

BOOL CCustomPopupMenu::OnTtnNeedText(UINT, NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	TOOLTIPTEXTW* pTTT = (TOOLTIPTEXTW*)pNMHDR;
	if (!pTTT || m_tipHot < 0 || m_tipHot >= m_itemCount) return FALSE;
	const CCustomPopupItem& it = m_items[m_tipHot];
	if (!it.hasTip || !it.tip[0]) return FALSE;
	pTTT->lpszText = (LPWSTR)it.tip;
	return TRUE;
}

void CCustomPopupMenu::SyncHotFromCursor()
{
	// サブHWNDが親を覆っていても、カーソル下の親行でホバーを同期（非サブ→即 CloseOpenSub）
	if (!GetSafeHwnd() || m_openSub < 0) return;
	CPoint sp;
	::GetCursorPos(&sp);
	CRect pwr; GetWindowRect(&pwr);
	if (!pwr.PtInRect(sp)) return;
	CPoint c = sp;
	ScreenToClient(&c);
	const int idx = HitTest(c);
	if (idx < 0) return;
	// サブの定着行上にカーソルがあるときは親行よりサブ操作を優先（親右端と重ならない想定）
	if (idx == m_openSub) {
		const int si = m_items[m_openSub].subIndex;
		if (si >= 0 && si < m_subCount && m_subs[si] && m_subs[si]->GetSafeHwnd()) {
			CPoint sc = sp;
			m_subs[si]->ScreenToClient(&sc);
			if (m_subs[si]->HitTest(sc) >= 0)
				return;
		}
	}
	if (idx != m_hot)
		SetHot(idx);
	else if (idx != m_openSub && m_openSub >= 0)
		CloseOpenSub(); // hot は合っているがサブが残っている場合
}

void CCustomPopupMenu::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kInwomanTimer) {
		if (CCC_IsInwoman()) InvalidateBgOnly(); else KillTimer(kInwomanTimer);
		return;
	}
	if (nIDEvent == kAnimTimer) {
		SyncHotFromCursor();
		++m_animTick;
		const int styleNow = PopupAnimStyle();
		if (GetSafeHwnd()
			&& styleNow == POPUP_ANIM_EXPAND && !m_asSubmenu
			&& (m_lineAnimPhase == 1 || m_lineAnimPhase == 2)) {
			// 上下伸び: 定位置の行外接で綺麗な矩形 RGN（オフセット揺れを混ぜない）
			CRect rc; GetClientRect(&rc);
			CRect hull(0, 0, 0, 0);
			BOOL any = FALSE;
			for (int i = 0; i < m_itemCount; ++i) {
				int ox = 0, oy = 0, fade = 256;
				if (!CalcLineAnim(i, &ox, &oy, &fade) || fade < 4)
					continue;
				CRect vr = ItemViewRect(i);
				CRect band(rc.left, vr.top, rc.right, vr.bottom);
				band.InflateRect(0, 2);
				if (!band.IntersectRect(&band, &rc) || band.Height() <= 0)
					continue;
				if (!any) { hull = band; any = TRUE; }
				else hull.UnionRect(&hull, &band);
			}
			if (!any) {
				int y0 = m_lineAnimOriginY - 4, y1 = m_lineAnimOriginY + 4;
				if (y0 < 0) y0 = 0;
				if (y1 > rc.bottom) y1 = rc.bottom;
				hull.SetRect(rc.left, y0, rc.right, max(y1, y0 + 2));
			} else {
				if (m_lineAnimOriginY < hull.top) hull.top = max(rc.top, m_lineAnimOriginY - 4);
				if (m_lineAnimOriginY > hull.bottom) hull.bottom = min(rc.bottom, m_lineAnimOriginY + 4);
				hull.left = rc.left;
				hull.right = rc.right;
				hull.InflateRect(0, 1);
				hull.IntersectRect(&hull, &rc);
			}
			HRGN hUnion = ::CreateRectRgn(hull.left, hull.top, hull.right, hull.bottom);
			::SetWindowRgn(m_hWnd, hUnion, TRUE);
		}
		if (m_lineAnimPhase == 1) {
			const int style = styleNow;
			const int total = ChipEnterTotalMs(style, m_asSubmenu, m_itemCount, m_lineAnimOrigin);
			const int elapsed = (int)(GetTickCount64() - m_lineAnimStart);
			const BOOL timeUp = (elapsed >= total);
			const BOOL atRest = (UsesRowChipFlight(style) && ChipFlightRowsAtRest());
			if (timeUp || atRest) {
				// サブ行ホバーの SnapAnimToIdle と同じ定着経路に統一
				SnapAnimToIdle();
				return;
			}
			// UpdateLayeredWindow 使用中は Invalidate だけではコマが進まない
			if (UsesRowChipFlight(style))
				ForceChipPresent();
			else
				InvalidateBgOnly();
			return;
		}
		// idle: ホバー追従は冒頭の SyncHotFromCursor のみ。
		// 毎ティック Invalidate すると BufferedPaint が子を潰して点滅する。
		if (CCC_IsInwoman())
			InvalidateBgOnly();
		return;
	}
	if (nIDEvent == kSettleTimer) {
		KillTimer(kSettleTimer);
		if (m_lineAnimPhase != 0) {
			SnapAnimToIdle();
		} else {
			// 既に phase=0 でも、マウス未移動だと子が潰れたまま残ることがある
			InvalidateBgOnly();
			UpdateWindow();
			RefreshEmbeddedChildren();
			PostMessage(WM_APP + 0x51C, 0, 0);
		}
		return;
	}
	if (nIDEvent == kBounceTimer) {
		if (--m_nBounce <= 0) {
			m_nBounce = 0;
			m_bounceIdx = -1;
			KillTimer(kBounceTimer);
		}
		InvalidateBgOnly();
		return;
	}
	CWnd::OnTimer(nIDEvent);
}

void CCustomPopupMenu::RunModalLoop()
{
	m_tracking = TRUE; m_done = FALSE; m_result = 0;
	HWND hCap = (m_owner && m_owner->GetSafeHwnd()) ? m_owner->GetSafeHwnd() : NULL;
	MSG msg;

	auto dispatchOne = [&](MSG& m) -> BOOL {
		if (m.message == WM_QUIT) {
			m_done = TRUE; m_result = 0;
			::PostQuitMessage((int)m.wParam);
			return FALSE;
		}
		if (m.message == WM_ACTIVATEAPP && m.wParam == FALSE) {
			m_done = TRUE; m_result = 0; return TRUE;
		}
		if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
			CWnd* f = GetFocus();
			if (!(f && IsChild(f) && f->IsKindOf(RUNTIME_CLASS(CCustomEdit)))) {
				m_done = TRUE; m_result = 0; return TRUE;
			}
		}
		if (m.message == WM_MOUSEWHEEL || m.message == WM_MOUSEHWHEEL) {
			DWORD pos = ::GetMessagePos();
			CPoint sp(GET_X_LPARAM(pos), GET_Y_LPARAM(pos));
			const int delta = GET_WHEEL_DELTA_WPARAM(m.wParam);
			if (HandleWheelInChain(sp, delta))
				return TRUE;
		}
		if (m.message == WM_LBUTTONDOWN || m.message == WM_RBUTTONDOWN
			|| m.message == WM_NCLBUTTONDOWN || m.message == WM_NCRBUTTONDOWN
			|| m.message == WM_LBUTTONDBLCLK) {
			DWORD pos = ::GetMessagePos();
			CPoint sp(GET_X_LPARAM(pos), GET_Y_LPARAM(pos));
			if (!IsPointInChain(sp)) { m_done = TRUE; m_result = 0; return TRUE; }
		}
		if (m_tip.GetSafeHwnd()) m_tip.RelayEvent(&m);
		TranslateMessage(&m);
		DispatchMessage(&m);
		return TRUE;
	};

	while (!m_done) {
		// 出現／退場アニメ中は長待ち禁止。
		// MsgWait の長い timeout だと Peek されず WM_TIMER(16ms) が合成されず、
		// 背景チップがマウス移動まで止まる。
		if (m_lineAnimPhase == 1 || m_lineAnimPhase == 2) {
			::MsgWaitForMultipleObjects(0, NULL, FALSE, 16, QS_ALLINPUT);
			while (!m_done && ::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				if (!dispatchOne(msg))
					break;
			}
			if (m_lineAnimPhase == 1) {
				const int style = PopupAnimStyle();
				const int total = ChipEnterTotalMs(style, m_asSubmenu, m_itemCount, m_lineAnimOrigin);
				const int elapsed = (int)(GetTickCount64() - m_lineAnimStart);
				const BOOL atRest = (UsesRowChipFlight(style) && ChipFlightRowsAtRest());
				if (elapsed >= total || atRest)
					SnapAnimToIdle();
			}
			if (!m_done && !IsForegroundOurs()) {
				m_done = TRUE; m_result = 0;
			}
			continue;
		}

		// 定着後は GetMessage（WM_TIMER を正しく起こす）
		if (!::GetMessage(&msg, NULL, 0, 0)) {
			m_done = TRUE; m_result = 0;
			::PostQuitMessage((int)msg.wParam); break;
		}
		if (!dispatchOne(msg))
			break;
		if (!m_done && !IsForegroundOurs()) {
			m_done = TRUE; m_result = 0;
		}
	}
	m_tracking = FALSE;
	DestroyPopupTree();
	if (hCap && ::IsWindow(hCap)) ::SetForegroundWindow(hCap);
}

UINT CCustomPopupMenu::Track(CPoint screenPt, CWnd* pOwner)
{
	m_owner = pOwner;
	m_root = this;
	m_parentMenu = NULL;
	EnsureChromePrefix();
	if (!CreatePopupAt(screenPt, NULL, this))
		return 0;
	s_trackingRoot = this;
	RunModalLoop();
	if (s_trackingRoot == this)
		s_trackingRoot = NULL;
	// 骨格コマンドは呼び出し元へ返さない
	if (IsChromeCommand(m_result))
		return 0;
	return m_result;
}

int CCustomPopupMenu::FindItemIndexById(UINT id) const
{
	if (id == 0) return -1;
	for (int i = 0; i < m_itemCount; ++i)
		if (m_items[i].id == id) return i;
	return -1;
}

int CCustomPopupMenu::FindItemById(UINT id) const
{
	return FindItemIndexById(id);
}

int CCustomPopupMenu::GetItemKind(int idx) const
{
	if (idx < 0 || idx >= m_itemCount) return -1;
	return m_items[idx].kind;
}

UINT CCustomPopupMenu::GetItemId(int idx) const
{
	if (idx < 0 || idx >= m_itemCount) return 0;
	return m_items[idx].id;
}

BOOL CCustomPopupMenu::GetSliderPos(UINT id, int* outPos) const
{
	const int idx = FindItemIndexById(id);
	if (idx < 0 || !outPos) return FALSE;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_SLIDER || it.sliderIndex < 0) return FALSE;
	if (m_sliders[it.sliderIndex].GetSafeHwnd())
		*outPos = m_sliders[it.sliderIndex].GetPos();
	else
		*outPos = it.sliderPos;
	return TRUE;
}

BOOL CCustomPopupMenu::SetSliderPos(UINT id, int pos)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return FALSE;
	CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_SLIDER || it.sliderIndex < 0) return FALSE;
	if (pos < it.sliderMin) pos = it.sliderMin;
	if (pos > it.sliderMax) pos = it.sliderMax;
	it.sliderPos = pos;
	if (m_sliders[it.sliderIndex].GetSafeHwnd())
		m_sliders[it.sliderIndex].SetPos(pos);
	InvalidateBgOnly();
	return TRUE;
}

BOOL CCustomPopupMenu::GetEditText(UINT id, wchar_t* buf, int bufCch) const
{
	const int idx = FindItemIndexById(id);
	if (idx < 0 || !buf || bufCch <= 0) return FALSE;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_EDIT || it.editIndex < 0) return FALSE;
	buf[0] = 0;
	if (m_edits[it.editIndex].GetSafeHwnd()) {
		CString s; m_edits[it.editIndex].GetWindowText(s);
		lstrcpynW(buf, s, bufCch);
	} else if (it.choiceSet >= 0 && it.choiceSet < m_choiceSetCount)
		lstrcpynW(buf, m_choiceSets[it.choiceSet].items[0], bufCch);
	return TRUE;
}

BOOL CCustomPopupMenu::GetChoiceSel(UINT id, int* outSel) const
{
	const int idx = FindItemIndexById(id);
	if (idx < 0 || !outSel) return FALSE;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind == CCUSTOM_POPUP_COMBO && it.comboIndex >= 0) {
		if (m_combos[it.comboIndex].GetSafeHwnd())
			*outSel = m_combos[it.comboIndex].GetCurSelPhysical();
		else
			*outSel = it.choiceSel;
		return TRUE;
	}
	if (it.kind == CCUSTOM_POPUP_LIST && it.listIndex >= 0) {
		if (m_lists[it.listIndex].GetSafeHwnd())
			*outSel = m_lists[it.listIndex].GetCurSel();
		else
			*outSel = it.choiceSel;
		return TRUE;
	}
	return FALSE;
}

BOOL CCustomPopupMenu::GetRangeValues(UINT id, int* pos, int* selMin, int* selMax, int* abA, int* abB) const
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return FALSE;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_RANGE || it.rangeIndex < 0) return FALSE;
	int p = it.sliderPos, s0 = it.rangeSelMin, s1 = it.rangeSelMax, a = it.rangeAbA, b = it.rangeAbB;
	if (m_ranges[it.rangeIndex].GetSafeHwnd()) {
		p = m_ranges[it.rangeIndex].GetPos();
		m_ranges[it.rangeIndex].GetSelection(s0, s1);
		m_ranges[it.rangeIndex].GetAB(a, b);
	}
	if (pos) *pos = p;
	if (selMin) *selMin = s0;
	if (selMax) *selMax = s1;
	if (abA) *abA = a;
	if (abB) *abB = b;
	return TRUE;
}

CCustomPopupMenu* CCustomPopupMenu::s_trackingRoot = NULL;

CCustomPopupMenu* CCustomPopupMenu::GetTrackingRoot()
{
	return s_trackingRoot;
}

BOOL CCustomPopupMenu::LiveMirrorRange(UINT id, int pos, int selMin, int selMax, int mn, int mx, int abA, int abB)
{
	int idx = -1;
	if (id != 0)
		idx = FindItemIndexById(id);
	if (idx < 0) {
		for (int i = 0; i < m_itemCount; ++i) {
			if (m_items[i].kind == CCUSTOM_POPUP_RANGE && m_items[i].rangeIndex >= 0) {
				idx = i;
				break;
			}
		}
	}
	if (idx < 0) return FALSE;
	CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_RANGE || it.rangeIndex < 0) return FALSE;
	CCustomRangeSliderCtrl& rs = m_ranges[it.rangeIndex];
	if (!rs.GetSafeHwnd()) return FALSE;
	if (rs.IsDragging()) return FALSE;

	if (mn > mx) { const int t = mn; mn = mx; mx = t; }
	if (mx <= mn) mx = mn + 1;
	rs.SetPlaybackMirror(pos, selMin, selMax, mn, mx, abA, abB);
	it.sliderMin = mn;
	it.sliderMax = mx;
	it.sliderPos = rs.GetPos();
	rs.GetSelection(it.rangeSelMin, it.rangeSelMax);
	rs.GetAB(it.rangeAbA, it.rangeAbB);
	InvalidateBgOnly();
	return TRUE;
}

BOOL CCustomPopupMenu::GetProgressPos(UINT id, int* outPos) const
{
	const int idx = FindItemIndexById(id);
	if (idx < 0 || !outPos) return FALSE;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_PROGRESS || it.progressIndex < 0) return FALSE;
	if (m_progresses[it.progressIndex].GetSafeHwnd())
		*outPos = m_progresses[it.progressIndex].GetPos();
	else
		*outPos = it.sliderPos;
	return TRUE;
}

BOOL CCustomPopupMenu::SetProgressPos(UINT id, int pos)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return FALSE;
	CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_PROGRESS || it.progressIndex < 0) return FALSE;
	if (pos < it.sliderMin) pos = it.sliderMin;
	if (pos > it.sliderMax) pos = it.sliderMax;
	it.sliderPos = pos;
	if (m_progresses[it.progressIndex].GetSafeHwnd())
		m_progresses[it.progressIndex].SetPos(pos);
	InvalidateBgOnly();
	return TRUE;
}

CCustomSliderCtrl* CCustomPopupMenu::GetSliderCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_SLIDER || it.sliderIndex < 0) return NULL;
	return &m_sliders[it.sliderIndex];
}

CCustomEdit* CCustomPopupMenu::GetEditCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_EDIT || it.editIndex < 0) return NULL;
	return &m_edits[it.editIndex];
}

CCustomComboBox* CCustomPopupMenu::GetComboCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_COMBO || it.comboIndex < 0) return NULL;
	return &m_combos[it.comboIndex];
}

CCustomListBox* CCustomPopupMenu::GetListCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_LIST || it.listIndex < 0) return NULL;
	return &m_lists[it.listIndex];
}

CCustomRangeSliderCtrl* CCustomPopupMenu::GetRangeSliderCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_RANGE || it.rangeIndex < 0) return NULL;
	return &m_ranges[it.rangeIndex];
}

CCustomProgressCtrl* CCustomPopupMenu::GetProgressCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_PROGRESS || it.progressIndex < 0) return NULL;
	return &m_progresses[it.progressIndex];
}

CCustomStandardButton* CCustomPopupMenu::GetButtonCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_BUTTON || it.buttonIndex < 0) return NULL;
	return &m_buttons[it.buttonIndex];
}
