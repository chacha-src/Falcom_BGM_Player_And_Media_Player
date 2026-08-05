#include "stdafx.h"
#include "CCustomPopupMenu.h"
#include <uxtheme.h>
#include <math.h>
#pragma comment(lib, "uxtheme.lib")

extern void MpPersistSavedataQuick();

IMPLEMENT_DYNAMIC(CCustomPopupMenu, CWnd)

BEGIN_MESSAGE_MAP(CCustomPopupMenu, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
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
	enum { kTipTimer = 7701, kInwomanTimer = 7702, kAnimTimer = 7703, kBounceTimer = 7704 };

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

	static void DrawCuteSep(CDC& dc, const CRect& rc)
	{
		const int y = (rc.top + rc.bottom) / 2;
		const int L = rc.left + CCUSTOM_POPUP_PAD_X + CCUSTOM_POPUP_CHECK_W;
		const int R = rc.right - CCUSTOM_POPUP_PAD_RIGHT;
		const int mid = (L + R) / 2;
		const int gap = 14;
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

	static void FontSizeCb(void* ctx, int value)
	{
		CCustomPopupMenu* menu = (CCustomPopupMenu*)ctx;
		if (!menu) return;
		savedata.popupMenuPoint = value;
		ClampPopupFontSave();
		menu->PersistPopupFont();
		menu->RefreshFontChain();
	}
}

CCustomPopupMenu::CCustomPopupMenu()
	: m_itemCount(0), m_subCount(0), m_sliderCount(0), m_editCount(0)
	, m_comboCount(0), m_listCount(0), m_choiceSetCount(0)
	, m_bAeroMode(FALSE), m_tracking(FALSE), m_done(FALSE), m_result(0)
	, m_hot(-1), m_openSub(-1), m_owner(NULL), m_parentMenu(NULL), m_root(NULL)
	, m_tipHot(-1), m_memW(0), m_memH(0), m_font(NULL), m_fontOwned(NULL)
	, m_menuW(0), m_menuH(0), m_contentH(0), m_scrollY(0), m_scrollMax(0)
	, m_stickyCount(0), m_stickyH(0)
	, m_asSubmenu(FALSE), m_animTick(0)
	, m_skipChrome(FALSE), m_chromeInjected(FALSE), m_previewing(FALSE)
	, m_bounceIdx(-1), m_nBounce(0)
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
	m_comboCount = m_listCount = m_choiceSetCount = 0;
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
		|| kind == CCUSTOM_POPUP_COMBO || kind == CCUSTOM_POPUP_LIST;
}

BOOL CCustomPopupMenu::IsChromeCommand(UINT id) const
{
	return id == CCUSTOM_POPUP_ID_ACRYLIC
		|| id == CCUSTOM_POPUP_ID_FONT_BOLD
		|| id == CCUSTOM_POPUP_ID_FONT_ITALIC
		|| id == CCUSTOM_POPUP_ID_FONT_FACE;
}

BOOL CCustomPopupMenu::AddItemBase(int kind, UINT id, LPCTSTR text, LPCTSTR tip, BOOL enabled, BOOL checked)
{
	if (m_itemCount >= CCUSTOM_POPUP_MAX_ITEMS) return FALSE;
	CCustomPopupItem& it = m_items[m_itemCount++];
	ZeroMemory(&it, sizeof(it));
	it.kind = kind; it.id = id; it.enabled = enabled; it.checked = checked;
	it.subIndex = it.sliderIndex = it.editIndex = it.comboIndex = it.listIndex = it.choiceSet = -1;
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
	CCustomPopupSliderCb cb, void* ctx, LPCTSTR tip)
{
	if (m_sliderCount >= CCUSTOM_POPUP_MAX_SLIDERS) return FALSE;
	if (!AddItemBase(CCUSTOM_POPUP_SLIDER, 0, label, tip, TRUE, FALSE)) return FALSE;
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
	if (m_choiceSetCount >= (CCUSTOM_POPUP_MAX_COMBOS + CCUSTOM_POPUP_MAX_LISTS)) return -1;
	CCustomPopupChoiceSet& set = m_choiceSets[m_choiceSetCount];
	ZeroMemory(&set, sizeof(set));
	const int n = min(count, (int)CCUSTOM_POPUP_MAX_CHOICES);
	for (int i = 0; i < n; ++i) CopyText(set.items[i], CCUSTOM_POPUP_CHOICE_LEN, items[i]);
	set.count = n;
	return m_choiceSetCount++;
}

BOOL CCustomPopupMenu::AddEdit(LPCTSTR label, LPCTSTR initial, CCustomPopupEditCb cb, void* ctx, LPCTSTR tip)
{
	if (m_editCount >= CCUSTOM_POPUP_MAX_EDITS) return FALSE;
	LPCTSTR one[1] = { initial ? initial : L"" };
	const int cs = AllocChoiceSet(one, 1);
	if (cs < 0) return FALSE;
	if (!AddItemBase(CCUSTOM_POPUP_EDIT, 0, label, tip, TRUE, FALSE)) { --m_choiceSetCount; return FALSE; }
	CCustomPopupItem& it = m_items[m_itemCount - 1];
	it.editCb = cb; it.ctrlCtx = ctx; it.choiceSet = cs; it.editIndex = m_editCount++;
	return TRUE;
}

BOOL CCustomPopupMenu::AddCombo(LPCTSTR label, const LPCTSTR* items, int count, int curSel,
	CCustomPopupChoiceCb cb, void* ctx, LPCTSTR tip)
{
	if (m_comboCount >= CCUSTOM_POPUP_MAX_COMBOS) return FALSE;
	const int cs = AllocChoiceSet(items, count);
	if (cs < 0) return FALSE;
	if (!AddItemBase(CCUSTOM_POPUP_COMBO, 0, label, tip, TRUE, FALSE)) { --m_choiceSetCount; return FALSE; }
	CCustomPopupItem& it = m_items[m_itemCount - 1];
	it.choiceCb = cb; it.ctrlCtx = ctx; it.choiceSet = cs; it.choiceSel = curSel;
	if (it.choiceSel < 0) it.choiceSel = 0;
	if (it.choiceSel >= m_choiceSets[cs].count) it.choiceSel = m_choiceSets[cs].count - 1;
	it.comboIndex = m_comboCount++;
	return TRUE;
}

BOOL CCustomPopupMenu::AddList(LPCTSTR label, const LPCTSTR* items, int count, int curSel,
	CCustomPopupChoiceCb cb, void* ctx, LPCTSTR tip)
{
	if (m_listCount >= CCUSTOM_POPUP_MAX_LISTS) return FALSE;
	const int cs = AllocChoiceSet(items, count);
	if (cs < 0) return FALSE;
	if (!AddItemBase(CCUSTOM_POPUP_LIST, 0, label, tip, TRUE, FALSE)) { --m_choiceSetCount; return FALSE; }
	CCustomPopupItem& it = m_items[m_itemCount - 1];
	it.choiceCb = cb; it.ctrlCtx = ctx; it.choiceSet = cs; it.choiceSel = curSel;
	if (it.choiceSel < 0) it.choiceSel = 0;
	if (it.choiceSel >= m_choiceSets[cs].count) it.choiceSel = m_choiceSets[cs].count - 1;
	it.listIndex = m_listCount++;
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
	if (root->GetSafeHwnd()) root->Invalidate(FALSE);
	for (int i = 0; i < root->m_subCount; ++i) {
		if (!root->m_subs[i]) continue;
		root->m_subs[i]->m_previewing = root->m_previewing;
		lstrcpynW(root->m_subs[i]->m_previewFace, root->m_previewFace, 32);
		root->m_subs[i]->RebuildMenuFont();
		if (root->m_subs[i]->GetSafeHwnd())
			root->m_subs[i]->Invalidate(FALSE);
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
	AddSeparator();

	for (int i = 0; i < savedN && m_itemCount < CCUSTOM_POPUP_MAX_ITEMS; ++i)
		m_items[m_itemCount++] = savedItems[i];
}

void CCustomPopupMenu::MeasureLayout()
{
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
		w += CCUSTOM_POPUP_CHECK_W;
		if (it.kind == CCUSTOM_POPUP_SUB) w += CCUSTOM_POPUP_ARROW_W;
		if (IsInteractiveKind(it.kind)) w = max(w, 160);
		if (w > maxTw) maxTw = w;
	}
	dc.SelectObject(old);
	::ReleaseDC(NULL, dc.Detach());

	m_menuW = max(CCUSTOM_POPUP_MIN_W,
		maxTw + CCUSTOM_POPUP_PAD_X + CCUSTOM_POPUP_PAD_RIGHT + CCUSTOM_POPUP_RIBBON_W + 12);
	if (m_stickyCount > m_itemCount) m_stickyCount = m_itemCount;
	int y = 8;
	m_stickyH = 0;
	for (int i = 0; i < m_itemCount; ++i) {
		CCustomPopupItem& it = m_items[i];
		int h = CCUSTOM_POPUP_ITEM_H;
		if (it.kind == CCUSTOM_POPUP_SEP) h = CCUSTOM_POPUP_SEP_H;
		else if (it.kind == CCUSTOM_POPUP_SLIDER) h = CCUSTOM_POPUP_SLIDER_H;
		else if (it.kind == CCUSTOM_POPUP_EDIT) h = CCUSTOM_POPUP_EDIT_H;
		else if (it.kind == CCUSTOM_POPUP_COMBO) h = CCUSTOM_POPUP_COMBO_H;
		else if (it.kind == CCUSTOM_POPUP_LIST) h = CCUSTOM_POPUP_LIST_H;
		it.rc.SetRect(CCUSTOM_POPUP_RIBBON_W + 2, y, m_menuW - 6, y + h);
		y += h;
		if (i + 1 == m_stickyCount)
			m_stickyH = y;
	}
	m_contentH = y + 8;
	m_menuH = m_contentH;
	m_scrollY = 0;
	m_scrollMax = 0;
}

void CCustomPopupMenu::SyncEmbeddedChildren()
{
	for (int i = 0; i < m_itemCount; ++i) {
		CCustomPopupItem& it = m_items[i];
		CRect vr = ItemViewRect(i);
		const BOOL sticky = (i < m_stickyCount);
		const BOOL onScreen = sticky
			? (vr.bottom > 0 && vr.top < m_menuH)
			: (vr.bottom > m_stickyH && vr.top < m_menuH);
		if (it.kind == CCUSTOM_POPUP_SLIDER && it.sliderIndex >= 0) {
			CCustomSliderCtrl& sl = m_sliders[it.sliderIndex];
			CRect sr = vr; sr.DeflateRect(CCUSTOM_POPUP_PAD_X + CCUSTOM_POPUP_CHECK_W, 20, CCUSTOM_POPUP_PAD_RIGHT, 5);
			if (!sl.GetSafeHwnd()) {
				sl.Create(WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | TBS_BOTH, sr, this, 6000 + it.sliderIndex);
				sl.SetAeroMode(FALSE); sl.SetRange(it.sliderMin, it.sliderMax, TRUE); sl.SetPos(it.sliderPos);
			} else sl.MoveWindow(&sr, TRUE);
			sl.ShowWindow(onScreen ? SW_SHOWNA : SW_HIDE);
		} else if (it.kind == CCUSTOM_POPUP_EDIT && it.editIndex >= 0) {
			CCustomEdit& ed = m_edits[it.editIndex];
			CRect er = vr; er.DeflateRect(CCUSTOM_POPUP_PAD_X + CCUSTOM_POPUP_CHECK_W, 20, CCUSTOM_POPUP_PAD_RIGHT, 5);
			LPCTSTR init = L"";
			if (it.choiceSet >= 0 && it.choiceSet < m_choiceSetCount && m_choiceSets[it.choiceSet].count > 0)
				init = m_choiceSets[it.choiceSet].items[0];
			if (!ed.GetSafeHwnd()) {
				ed.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, er, this, 6100 + it.editIndex);
				ed.SetWindowText(init);
			} else ed.MoveWindow(&er, TRUE);
			ed.ShowWindow(onScreen ? SW_SHOWNA : SW_HIDE);
		} else if (it.kind == CCUSTOM_POPUP_COMBO && it.comboIndex >= 0 && it.choiceSet >= 0) {
			CCustomComboBox& cb = m_combos[it.comboIndex];
			CRect cr = vr; cr.DeflateRect(CCUSTOM_POPUP_PAD_X + CCUSTOM_POPUP_CHECK_W, 20, CCUSTOM_POPUP_PAD_RIGHT, 5);
			const CCustomPopupChoiceSet& set = m_choiceSets[it.choiceSet];
			if (!cb.GetSafeHwnd()) {
				cb.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
					cr, this, 6200 + it.comboIndex);
				cb.SetAeroMode(FALSE);
				for (int k = 0; k < set.count; ++k) cb.AddString(set.items[k]);
				if (set.count > 0) cb.SetCurSelPhysical(it.choiceSel);
			} else cb.MoveWindow(&cr, TRUE);
			cb.ShowWindow(onScreen ? SW_SHOWNA : SW_HIDE);
		} else if (it.kind == CCUSTOM_POPUP_LIST && it.listIndex >= 0 && it.choiceSet >= 0) {
			CCustomListBox& lb = m_lists[it.listIndex];
			CRect lr = vr; lr.DeflateRect(CCUSTOM_POPUP_PAD_X + CCUSTOM_POPUP_CHECK_W, 20, CCUSTOM_POPUP_PAD_RIGHT, 5);
			const CCustomPopupChoiceSet& set = m_choiceSets[it.choiceSet];
			if (!lb.GetSafeHwnd()) {
				lb.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_BORDER
					| LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
					lr, this, 6300 + it.listIndex);
				lb.SetAeroMode(FALSE);
				for (int k = 0; k < set.count; ++k) lb.AddString(set.items[k]);
				if (set.count > 0) lb.SetCurSel(it.choiceSel);
			} else lb.MoveWindow(&lr, TRUE);
			lb.ShowWindow(onScreen ? SW_SHOWNA : SW_HIDE);
		}
	}
}

void CCustomPopupMenu::ShowEmbedded(BOOL show)
{
	for (int i = 0; i < m_sliderCount; ++i)
		if (m_sliders[i].GetSafeHwnd()) m_sliders[i].ShowWindow(show ? SW_SHOWNA : SW_HIDE);
	for (int i = 0; i < m_editCount; ++i)
		if (m_edits[i].GetSafeHwnd()) m_edits[i].ShowWindow(show ? SW_SHOWNA : SW_HIDE);
	for (int i = 0; i < m_comboCount; ++i)
		if (m_combos[i].GetSafeHwnd()) m_combos[i].ShowWindow(show ? SW_SHOWNA : SW_HIDE);
	for (int i = 0; i < m_listCount; ++i)
		if (m_lists[i].GetSafeHwnd()) m_lists[i].ShowWindow(show ? SW_SHOWNA : SW_HIDE);
}

void CCustomPopupMenu::AnimateIn()
{
	if (!GetSafeHwnd()) return;
	DWORD flags = AW_BLEND;
	const DWORD ms = m_asSubmenu ? CCUSTOM_POPUP_ANIM_SUB_MS : CCUSTOM_POPUP_ANIM_IN_MS;
	flags |= m_asSubmenu ? (AW_SLIDE | AW_HOR_POSITIVE) : (AW_SLIDE | AW_VER_POSITIVE);
	if (!::AnimateWindow(m_hWnd, ms, flags)) {
		ShowWindow(SW_SHOWNA); Invalidate(FALSE); UpdateWindow();
	}
	m_animTick = 0;
	SetTimer(kAnimTimer, 50, NULL); // 再描画は控えめ、位置は GetTickCount でスムーズ
}

void CCustomPopupMenu::AnimateOut()
{
	if (!GetSafeHwnd() || !IsWindowVisible()) return;
	ShowEmbedded(FALSE);
	if (m_tip.GetSafeHwnd()) m_tip.Activate(FALSE);
	KillTimer(kAnimTimer);
	if (!::AnimateWindow(m_hWnd, CCUSTOM_POPUP_ANIM_OUT_MS, AW_HIDE | AW_BLEND))
		ShowWindow(SW_HIDE);
}

BOOL CCustomPopupMenu::CreatePopupAt(CPoint screenPt, CCustomPopupMenu* parentMenu, CCustomPopupMenu* root)
{
	EnsurePopupClass();
	RebuildMenuFont();
	MeasureLayout();

	m_scrollY = 0;
	m_scrollMax = 0;
	MONITORINFO mi; ZeroMemory(&mi, sizeof(mi)); mi.cbSize = sizeof(mi);
	HMONITOR hMon = ::MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
	if (hMon && ::GetMonitorInfo(hMon, &mi)) {
		const int maxH = mi.rcWork.bottom - mi.rcWork.top - 8;
		if (m_contentH > maxH) {
			m_menuH = maxH;
			if (m_stickyH > 0 && m_menuH < m_stickyH + CCUSTOM_POPUP_ITEM_H * 3)
				m_menuH = min(maxH, m_stickyH + CCUSTOM_POPUP_ITEM_H * 3);
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
	m_bounceIdx = -1; m_nBounce = 0;
	if (GetSafeHwnd()) DestroyWindow();

	if (!CreateEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"CCustomPopupMenuClass", NULL, WS_POPUP,
		screenPt.x, screenPt.y, m_menuW, m_menuH, m_owner ? m_owner->GetSafeHwnd() : NULL, NULL))
		return FALSE;

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
	ShowEmbedded(TRUE);
	return TRUE;
}

void CCustomPopupMenu::DestroyPopupTree()
{
	CloseOpenSub();
	for (int i = 0; i < m_subCount; ++i)
		if (m_subs[i]) m_subs[i]->DestroyPopupTree();
	for (int i = 0; i < m_sliderCount; ++i)
		if (m_sliders[i].GetSafeHwnd()) m_sliders[i].DestroyWindow();
	for (int i = 0; i < m_editCount; ++i)
		if (m_edits[i].GetSafeHwnd()) m_edits[i].DestroyWindow();
	for (int i = 0; i < m_comboCount; ++i)
		if (m_combos[i].GetSafeHwnd()) m_combos[i].DestroyWindow();
	for (int i = 0; i < m_listCount; ++i)
		if (m_lists[i].GetSafeHwnd()) m_lists[i].DestroyWindow();
	if (m_tip.GetSafeHwnd()) m_tip.DestroyWindow();
	if (GetSafeHwnd()) {
		KillTimer(kTipTimer); KillTimer(kInwomanTimer); KillTimer(kAnimTimer); KillTimer(kBounceTimer);
		AnimateOut(); DestroyWindow();
	}
	if (m_memBmp.GetSafeHandle()) { m_memBmp.DeleteObject(); m_memW = m_memH = 0; }
}

void CCustomPopupMenu::CloseOpenSub()
{
	if (m_openSub >= 0 && m_openSub < m_itemCount) {
		const int si = m_items[m_openSub].subIndex;
		if (si >= 0 && si < m_subCount && m_subs[si])
			m_subs[si]->DestroyPopupTree();
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
		if (r.PtInRect(screenPt)) return TRUE;
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
	if (m_owner && m_owner->GetSafeHwnd()
		&& (h == m_owner->GetSafeHwnd() || ::IsChild(m_owner->GetSafeHwnd(), h)))
		return TRUE;
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

BOOL CCustomPopupMenu::IsForegroundOurs() const
{
	HWND fg = ::GetForegroundWindow();
	if (!fg) return TRUE;
	const CCustomPopupMenu* root = m_root ? m_root : this;
	return root->IsHwndRelated(fg);
}

CRect CCustomPopupMenu::ItemViewRect(int idx) const
{
	if (idx < 0 || idx >= m_itemCount)
		return CRect(0, 0, 0, 0);
	CRect r = m_items[idx].rc;
	if (idx >= m_stickyCount)
		r.OffsetRect(0, -m_scrollY);
	return r;
}

void CCustomPopupMenu::SetScrollY(int y)
{
	if (y < 0) y = 0;
	if (y > m_scrollMax) y = m_scrollMax;
	if (y == m_scrollY) return;
	m_scrollY = y;
	SyncEmbeddedChildren();
	Invalidate(FALSE);
}

BOOL CCustomPopupMenu::OnWheelDelta(int delta)
{
	if (m_scrollMax <= 0) return FALSE;
	const int steps = (delta > 0) ? -CCUSTOM_POPUP_SCROLL_STEP : CCUSTOM_POPUP_SCROLL_STEP;
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
	Invalidate(FALSE);
}

void CCustomPopupMenu::OpenSubAt(int idx)
{
	if (idx < 0 || idx >= m_itemCount) return;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_SUB || it.subIndex < 0 || !it.enabled) return;
	if (m_openSub == idx) return;
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
	Invalidate(FALSE);
	UpdateTip();
	if (idx >= 0 && idx < m_itemCount) {
		const CCustomPopupItem& it = m_items[idx];
		if (it.id == CCUSTOM_POPUP_ID_FONT_FACE && it.text[0])
			ApplyPreviewFace(it.text);
		if (it.kind == CCUSTOM_POPUP_SUB && it.enabled)
			OpenSubAt(idx);
		else if (m_openSub >= 0 && it.kind != CCUSTOM_POPUP_SUB)
			CloseOpenSub();
	} else if (m_openSub >= 0) {
		// leave
	}
}

void CCustomPopupMenu::UpdateTip()
{
	m_tipHot = m_hot;
	if (m_tip.GetSafeHwnd()) m_tip.SendMessage(TTM_UPDATE, 0, 0);
}

int CCustomPopupMenu::HitTest(CPoint pt) const
{
	if (m_stickyCount > 0 && pt.y < m_stickyH) {
		for (int i = 0; i < m_stickyCount; ++i) {
			if (m_items[i].kind == CCUSTOM_POPUP_SEP) continue;
			if (m_items[i].rc.PtInRect(pt))
				return i;
		}
		return -1;
	}
	const CPoint content(pt.x, pt.y + m_scrollY);
	for (int i = m_stickyCount; i < m_itemCount; ++i) {
		if (m_items[i].kind == CCUSTOM_POPUP_SEP) continue;
		if (m_items[i].rc.PtInRect(content))
			return i;
	}
	return -1;
}

void CCustomPopupMenu::PaintToDC(CDC& dc)
{
	CRect rc; GetClientRect(&rc);
	DrawJkBackdrop(dc, rc, m_animTick);
	DrawTornRibbon(dc, rc, m_animTick);

	HFONT oldFont = (HFONT)dc.SelectObject(m_font ? m_font : (HFONT)::GetStockObject(DEFAULT_GUI_FONT));
	dc.SetBkMode(TRANSPARENT);

	auto paintItem = [&](int i) {
		const CCustomPopupItem& it = m_items[i];
		CRect vr = ItemViewRect(i);
		if (vr.bottom < 0 || vr.top > rc.bottom) return;

		if (it.kind == CCUSTOM_POPUP_SEP) { DrawCuteSep(dc, vr); return; }

		const BOOL interactive = IsInteractiveKind(it.kind);
		const BOOL hot = (i == m_hot && it.enabled && !interactive);
		if (hot) DrawHotPill(dc, vr);

		CRect tr = vr;
		tr.left += CCUSTOM_POPUP_PAD_X;
		tr.right -= CCUSTOM_POPUP_PAD_RIGHT;

		{
			CRect cr(tr.left, tr.top, tr.left + CCUSTOM_POPUP_CHECK_W, tr.bottom);
			if (it.kind == CCUSTOM_POPUP_CHECK && it.checked) {
				CRect bounce = cr;
				bounce.InflateRect(2, 2);
				if (i == m_bounceIdx && m_nBounce > 0) {
					const double bf = sin(3.14159265 * (8 - m_nBounce) / 8.0);
					bounce.InflateRect((int)(bounce.Width() * 0.20 * bf), (int)(bounce.Height() * 0.20 * bf));
				}
				DrawRedCheck(dc, bounce);
			}
			tr.left += CCUSTOM_POPUP_CHECK_W;
		}

		if (interactive) {
			CRect lr = vr;
			lr.left = vr.left + CCUSTOM_POPUP_PAD_X + CCUSTOM_POPUP_CHECK_W;
			lr.right = vr.right - CCUSTOM_POPUP_PAD_RIGHT;
			lr.top = vr.top + 2;
			lr.bottom = vr.top + 18;
			wchar_t line[CCUSTOM_POPUP_TEXT_LEN + 32];
			if (it.kind == CCUSTOM_POPUP_SLIDER)
				_snwprintf_s(line, _TRUNCATE, L"%s  (%d)", it.text, it.sliderPos);
			else
				_snwprintf_s(line, _TRUNCATE, L"%s", it.text);
			DrawSoftShadowText(dc, line, lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, PopupText(TRUE), TRUE);
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
			tr.right -= CCUSTOM_POPUP_ARROW_W;
		DrawSoftShadowText(dc, it.text, tr, dt, PopupText(it.enabled), it.enabled);
		if (it.kind == CCUSTOM_POPUP_SUB) {
			CRect ar = vr;
			ar.left = vr.right - CCUSTOM_POPUP_PAD_RIGHT - CCUSTOM_POPUP_ARROW_W;
			ar.right = vr.right - CCUSTOM_POPUP_PAD_RIGHT;
			DrawSoftShadowText(dc, hot ? L"▹" : L"▸", ar, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
				hot ? RGB(130, 70, 160) : PopupText(it.enabled), it.enabled);
		}

		if (rowFont) {
			dc.SelectObject(prev ? prev : (m_font ? m_font : (HFONT)::GetStockObject(DEFAULT_GUI_FONT)));
			::DeleteObject(rowFont);
		}
	};

	// 固定ヘッダ — 背景は共通のまま、下端に細い区切りだけ
	if (m_stickyCount > 0 && m_stickyH > 0) {
		for (int i = 0; i < m_stickyCount; ++i)
			paintItem(i);
		dc.FillSolidRect(rc.left + CCUSTOM_POPUP_RIBBON_W + 6, m_stickyH - 2, rc.Width() - CCUSTOM_POPUP_RIBBON_W - 14, 1, RGB(255, 255, 255));
		dc.FillSolidRect(rc.left + CCUSTOM_POPUP_RIBBON_W + 6, m_stickyH - 1, rc.Width() - CCUSTOM_POPUP_RIBBON_W - 14, 1, RGB(240, 170, 200));
	}

	// スクロール本体
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
	BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
	params.dwFlags = BPPF_ERASE;
	HDC hdcBuf = NULL;
	HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
	if (hdcBuf && hBP) {
		CDC dcBuf; dcBuf.Attach(hdcBuf); PaintToDC(dcBuf); dcBuf.Detach();
		::BufferedPaintMakeOpaque(hBP, &r); ::EndBufferedPaint(hBP, TRUE); return;
	}
	CDC mDC; mDC.CreateCompatibleDC(&dc);
	if (!m_memBmp.GetSafeHandle() || m_memW != r.Width() || m_memH != r.Height()) {
		if (m_memBmp.GetSafeHandle()) m_memBmp.DeleteObject();
		m_memBmp.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
		m_memW = r.Width(); m_memH = r.Height();
	}
	CBitmap* ob = mDC.SelectObject(&m_memBmp);
	PaintToDC(mDC);
	dc.BitBlt(0, 0, r.Width(), r.Height(), &mDC, 0, 0, SRCCOPY);
	mDC.SelectObject(ob);
}

BOOL CCustomPopupMenu::OnEraseBkgnd(CDC*) { return TRUE; }

LRESULT CCustomPopupMenu::OnPrintClient(WPARAM wParam, LPARAM)
{
	if (HDC hdc = (HDC)wParam) {
		CRect r; GetClientRect(&r);
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

void CCustomPopupMenu::OnMouseMove(UINT nFlags, CPoint point)
{
	TRACKMOUSEEVENT tme = { sizeof(tme) };
	tme.dwFlags = TME_LEAVE; tme.hwndTrack = m_hWnd; ::_TrackMouseEvent(&tme);
	SetHot(HitTest(point));
	CWnd::OnMouseMove(nFlags, point);
}

void CCustomPopupMenu::OnMouseLeave()
{
	if (m_hot >= 0 && m_items[m_hot].kind != CCUSTOM_POPUP_SUB) {
		const BOOL wasFace = (m_items[m_hot].id == CCUSTOM_POPUP_ID_FONT_FACE);
		m_hot = -1; Invalidate(FALSE);
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
		Invalidate(FALSE);
		return TRUE;
	}
	if (it.id == CCUSTOM_POPUP_ID_FONT_ITALIC) {
		savedata.popupMenuItalic = savedata.popupMenuItalic ? 0 : 1;
		it.checked = savedata.popupMenuItalic ? TRUE : FALSE;
		if (it.checked) StartCheckBounce(idx);
		PersistPopupFont();
		RefreshFontChain();
		Invalidate(FALSE);
		return TRUE;
	}
	if (it.id == CCUSTOM_POPUP_ID_FONT_FACE) {
		CommitFace(it.text);
		CloseChain(0);
		return TRUE;
	}
	return FALSE;
}

void CCustomPopupMenu::OnLButtonDown(UINT nFlags, CPoint point)
{
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
	// 他ウィンドウへフォーカスが移ったら閉じる
	CCustomPopupMenu* root = RootMenu();
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
	for (int i = 0; i < m_itemCount; ++i) {
		CCustomPopupItem& it = m_items[i];
		if (it.kind != CCUSTOM_POPUP_SLIDER || it.sliderIndex < 0) continue;
		if (m_sliders[it.sliderIndex].GetSafeHwnd() != pScrollBar->GetSafeHwnd()) continue;
		const int v = m_sliders[it.sliderIndex].GetPos();
		it.sliderPos = v;
		if (it.sliderCb) it.sliderCb(it.ctrlCtx, v);
		Invalidate(FALSE); break;
	}
	CWnd::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CCustomPopupMenu::NotifyEditFromHwnd(HWND hwnd)
{
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

void CCustomPopupMenu::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kInwomanTimer) {
		if (CCC_IsInwoman()) Invalidate(FALSE); else KillTimer(kInwomanTimer);
		return;
	}
	if (nIDEvent == kAnimTimer) {
		++m_animTick;
		Invalidate(FALSE); // 斜め背景のスクロールを継続
		return;
	}
	if (nIDEvent == kBounceTimer) {
		if (--m_nBounce <= 0) {
			m_nBounce = 0;
			m_bounceIdx = -1;
			KillTimer(kBounceTimer);
		}
		Invalidate(FALSE);
		return;
	}
	CWnd::OnTimer(nIDEvent);
}

void CCustomPopupMenu::RunModalLoop()
{
	m_tracking = TRUE; m_done = FALSE; m_result = 0;
	HWND hCap = (m_owner && m_owner->GetSafeHwnd()) ? m_owner->GetSafeHwnd() : NULL;
	MSG msg;
	while (!m_done) {
		if (!::GetMessage(&msg, NULL, 0, 0)) {
			m_done = TRUE; m_result = 0;
			::PostQuitMessage((int)msg.wParam); break;
		}
		if (msg.message == WM_ACTIVATEAPP && msg.wParam == FALSE) {
			m_done = TRUE; m_result = 0; continue;
		}
		if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
			CWnd* f = GetFocus();
			if (!(f && IsChild(f) && f->IsKindOf(RUNTIME_CLASS(CCustomEdit)))) {
				m_done = TRUE; m_result = 0; continue;
			}
		}
		if (msg.message == WM_MOUSEWHEEL || msg.message == WM_MOUSEHWHEEL) {
			DWORD pos = ::GetMessagePos();
			CPoint sp(GET_X_LPARAM(pos), GET_Y_LPARAM(pos));
			const int delta = GET_WHEEL_DELTA_WPARAM(msg.wParam);
			if (HandleWheelInChain(sp, delta))
				continue;
		}
		if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN
			|| msg.message == WM_NCLBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN
			|| msg.message == WM_LBUTTONDBLCLK) {
			DWORD pos = ::GetMessagePos();
			CPoint sp(GET_X_LPARAM(pos), GET_Y_LPARAM(pos));
			if (!IsPointInChain(sp)) { m_done = TRUE; m_result = 0; continue; }
		}
		if (m_tip.GetSafeHwnd()) m_tip.RelayEvent(&msg);
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		// 他ウィンドウへ移ったら閉じる（NOACTIVATE でもフォアグラウンド監視）
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
	RunModalLoop();
	// 骨格コマンドは呼び出し元へ返さない
	if (IsChromeCommand(m_result))
		return 0;
	return m_result;
}
