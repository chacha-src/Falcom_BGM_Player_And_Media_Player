#include "stdafx.h"
#include "CCustomPopupMenu.h"
#include "GdiSoft2D.h"
#include "GdiSoft3D.h"
#include <uxtheme.h>
#include <math.h>
#include <algorithm>
#pragma comment(lib, "uxtheme.lib")

// ============================================================================
// CCustomPopupMenu.cpp — 自前コンテキストメニュー実装
//
// 【骨格（EnsureChromePrefix / HandleChromeClick）】
//   ルート先頭にフォント／アクリル／立体アクセント／KPI／MIDI KPI|VST優先／描画方法。
//   骨格 ID は Track が 0 を返す。MIDI は savedata.midPlayPrefer を書き、
//   PlRefreshMidiPlayModes() でプレイリストの MID(VST)/MID(KPI) を即更新する。
//   PlayList.h は IDD_PLAYLIST 欠落のためこの cpp から include しない
//   （extern void PlRefreshMidiPlayModes();）。
//
// 【行アニメ m_lineAnimPhase】
//   0=idle（一枚パネル） / 1=enter / 2=exit
//   行チップ系は HWND を飛行余白つきに拡げ UpdateLayeredWindow(α)。
//   マゼンタキー kChipChromaKey → α=0。定着は m_bridgePanel で一枚パネルを
//   強制してから ULW 解除（行き過ぎ整列の点滅防止）。
//
// 【Track / モーダル / z】
//   Track → CreatePopupAt（WS_EX_TOPMOST|NOACTIVATE）→ RunModalLoop。
//   入場中 16ms MsgWait。定着後 33ms で前面監視。
//   CCUSTOM_POPUP_RELAX_DISMISS_PROP で FG/KillFocus 自動閉じを抑止
//   （外側クリック・Esc・アプリ非アクティブは有効）。
//
// 【sticky / スクロール】
//   先頭 m_stickyCount 行は固定。本文のみ m_scrollY。HitTest も帯を分ける。
//
// 【内包 CCustom*】
//   SyncEmbeddedChildren がラベル下へ HWND を置く。飛行中は隠して代理描画。
// ============================================================================

extern void MpPersistSavedataQuick();
// PlayList.h は IDD_PLAYLIST 欠落のため include しない（骨格 MIDI 優先の即時反映用）
extern void PlRefreshMidiPlayModes();

#ifndef WM_APP_KPI_PLUGIN
#define WM_APP_KPI_PLUGIN (WM_APP + 58)
#endif

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
	// タイマ: Tip / 淫女 / 行アニメ(入場16ms・idle33ms兼用) / レ点バウンス / 強制定着
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

	// メニュー外クリック／退場中に積まれた「新しいメニューを開く」系を捨てる
	static BOOL s_reopenRClick = FALSE;
	static POINT s_reopenScreenPt = {};
	static BOOL s_reopenNc = FALSE;

	static void PopupEatOpenMenuMessages()
	{
		MSG m;
		for (;;) {
			BOOL got = FALSE;
			if (::PeekMessage(&m, NULL, WM_RBUTTONDOWN, WM_RBUTTONUP, PM_REMOVE))
				got = TRUE;
			else if (::PeekMessage(&m, NULL, WM_NCRBUTTONDOWN, WM_NCRBUTTONUP, PM_REMOVE))
				got = TRUE;
			else if (::PeekMessage(&m, NULL, WM_CONTEXTMENU, WM_CONTEXTMENU, PM_REMOVE))
				got = TRUE;
			if (!got) break;
		}
	}
	static BOOL PopupIsMenuOpenMessage(UINT msg)
	{
		return msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP
			|| msg == WM_NCRBUTTONDOWN || msg == WM_NCRBUTTONUP
			|| msg == WM_CONTEXTMENU;
	}
	// 退場アニメ／破棄中にメインへ流すと押下見た目だけ残って BN_CLICKED が死ぬ
	static BOOL PopupIsInputMessage(UINT msg)
	{
		if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) return TRUE;
		if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) return TRUE;
		if (msg >= WM_NCMOUSEMOVE && msg <= WM_NCMBUTTONDBLCLK) return TRUE;
		if (msg == WM_CONTEXTMENU || msg == WM_APPCOMMAND) return TRUE;
		return FALSE;
	}
	// 外側クリックで閉じた DOWN の対になる UP／残クリックを捨てる（キャプチャ残留防止）
	static void PopupEatDismissClickTail()
	{
		MSG m;
		for (;;) {
			BOOL got = FALSE;
			if (::PeekMessage(&m, NULL, WM_LBUTTONDOWN, WM_LBUTTONDBLCLK, PM_REMOVE))
				got = TRUE;
			else if (::PeekMessage(&m, NULL, WM_NCLBUTTONDOWN, WM_NCLBUTTONDBLCLK, PM_REMOVE))
				got = TRUE;
			else if (::PeekMessage(&m, NULL, WM_RBUTTONDOWN, WM_RBUTTONDBLCLK, PM_REMOVE))
				got = TRUE;
			else if (::PeekMessage(&m, NULL, WM_NCRBUTTONDOWN, WM_NCRBUTTONDBLCLK, PM_REMOVE))
				got = TRUE;
			else if (::PeekMessage(&m, NULL, WM_MBUTTONDOWN, WM_MBUTTONDBLCLK, PM_REMOVE))
				got = TRUE;
			else if (::PeekMessage(&m, NULL, WM_CONTEXTMENU, WM_CONTEXTMENU, PM_REMOVE))
				got = TRUE;
			if (!got) break;
		}
		if (::GetCapture())
			::ReleaseCapture();
	}
	static void PopupArmReopenAt(CPoint screenPt, BOOL nc)
	{
		s_reopenRClick = TRUE;
		s_reopenScreenPt = screenPt;
		s_reopenNc = nc ? TRUE : FALSE;
	}
	// Track 完了後: 外側右クリックで新規メニューを1つ開く。
	// DOWN+UP 再送は新規 Track のモーダルが「外側クリック」と誤認して即閉じる／
	// 二重 CONTEXTMENU の原因になるので、WM_CONTEXTMENU を1通だけ投げる。
	static void PopupFlushReopenContextClick()
	{
		if (!s_reopenRClick) return;
		s_reopenRClick = FALSE;
		const POINT pt = s_reopenScreenPt;
		s_reopenNc = FALSE;
		HWND h = ::WindowFromPoint(pt);
		if (!h || !::IsWindow(h)) return;
		// 直前のメニュー owner へ戻したフォーカスを、実際にクリックした UI へ移す。
		// これをしないと新メニューの IsForegroundOurs が即座に FALSE → 出現途中で消える。
		HWND root = ::GetAncestor(h, GA_ROOT);
		if (!root) root = h;
		if (::IsWindow(root))
			::SetForegroundWindow(root);
		::PostMessage(h, WM_CONTEXTMENU, (WPARAM)h, MAKELPARAM(pt.x, pt.y));
	}

	// Soft*（ポップアップ専用・CCustomControl の静的とは分離）
	static int s_popSoftBusy = 0;
	static BOOL s_popSoftDisabled = FALSE; // 退場フェード等で Soft 全停止
	static GdiSoft2D::Context s_popSoft2d;
	static GdiSoft3D::Context s_popSoft3d;

	static void PopupSoftPlate(CDC& dc, const CRect& rc, int strength, int animTick, float doorT = -1.f)
	{
		// 全面パネルはごく薄い背景ポリゴン。ホット行(低い)だけ少し濃く。キャンディ箱は置かない。
		// doorT>=0: EXPAND/CLASSIC 着地用（1=着地、0=開いた/回転中）
		if (s_popSoftDisabled || s_popSoftBusy || rc.Width() < 24 || rc.Height() < 14) return;
		if (rc.Width() > 640 || rc.Height() > 520) return;
		if ((LONGLONG)rc.Width() * (LONGLONG)rc.Height() > 220000) return;
		++s_popSoftBusy;
		const int w = rc.Width();
		const int h = rc.Height();
		const int boost = savedata.popupMenuSoftBoost ? 1 : 0;
		const BOOL rowPill = (h <= 40);
		const COLORREF pink = CCC_IsInwoman() ? RGB(255, 160, 200) : RGB(255, 190, 220);
		const COLORREF lav = CCC_IsInwoman() ? RGB(240, 150, 210) : RGB(210, 190, 255);
		auto premultPresent = [&](GdiSoftFB::Framebuffer& fb, BYTE constA) {
			if (!fb.color || !fb.hdc || fb.w != w || fb.h != h) return;
			const int n = w * h;
			for (int i = 0; i < n; ++i) {
				const DWORD pix = fb.color[i];
				const BYTE a = GdiSoftFB::A(pix);
				if (a == 0) { fb.color[i] = 0; continue; }
				if (a >= 255) continue;
				fb.color[i] = GdiSoftFB::PackBGRA(a,
					(BYTE)(GdiSoftFB::R(pix) * a / 255),
					(BYTE)(GdiSoftFB::G(pix) * a / 255),
					(BYTE)(GdiSoftFB::B(pix) * a / 255));
			}
			fb.PresentAlpha(dc.GetSafeHdc(), rc.left, rc.top, constA);
		};
		// Soft3D は行ピル／扉アニメのみ。全面パネル毎フレ Soft3D が重さの主因。
		const BOOL useSoft3d = rowPill || (doorT >= 0.f);
		if (useSoft3d) {
			if (s_popSoft3d.fb.w != w || s_popSoft3d.fb.h != h)
				s_popSoft3d.Create(w, h);
			if (s_popSoft3d.fb.color && s_popSoft3d.fb.w == w && s_popSoft3d.fb.h == h) {
				s_popSoft3d.fb.Clear(GdiSoftFB::PackBGRA(0, 0, 0, 0), 1e9f);
				s_popSoft3d.alphaBlend = true;
				s_popSoft3d.depthTest = true;
				s_popSoft3d.depthWrite = true;
				s_popSoft3d.fogMode = GdiSoft3D::FogNone;
				s_popSoft3d.edgeOverlay = false;
				s_popSoft3d.dofEnable = false;
				s_popSoft3d.postVignette = s_popSoft3d.postGlow = s_popSoft3d.postSaturate = false;
				const float tf = (float)animTick * 0.03f;
				const float door = (doorT < 0.f) ? 1.f : ((doorT > 1.f) ? 1.f : doorT);
				const float remain = 1.f - door;
				s_popSoft3d.cam.yawDeg = -16.f + sinf(tf) * 6.f + remain * 48.f;
				s_popSoft3d.cam.pitchDeg = 40.f + cosf(tf * 0.8f) * 2.5f + remain * 18.f;
				s_popSoft3d.cam.zoom = 1.0f + (rowPill ? 0.06f : 0.f) + boost * 0.04f + remain * 0.12f;
				float boxes[1][6] = { { -0.85f, 0.85f, 0.f, 0.16f, -0.5f, 0.5f } };
				s_popSoft3d.SetViewportFit(boxes, 1);
				const float bob = sinf(tf * 1.05f) * 0.015f;
				s_popSoft3d.DrawBox(-0.65f, -0.05f, 0.08f + bob, -0.35f, 0.05f, pink, 0.f);
				if (rowPill || boost || remain > 0.05f)
					s_popSoft3d.DrawBox(0.0f, 0.68f, 0.07f - bob, -0.1f, 0.38f, lav, 0.f);
				BYTE a3 = (BYTE)(rowPill ? (72 + strength * 14 + boost * 16) : (48 + boost * 14));
				a3 = (BYTE)min(160, (int)a3 + (int)(remain * 36.f));
				premultPresent(s_popSoft3d.fb, a3);
			}
		}
		// Soft2D: 行ピル／boost／扉。全面は Soft2D のみ（Soft3D より軽い）＋3フレに1回
		const BOOL soft2dWant = rowPill || boost || doorT >= 0.f;
		const BOOL soft2dThrottleOk = rowPill || doorT >= 0.f || ((animTick % 5) == 0);
		if (soft2dWant && soft2dThrottleOk && s_popSoft2d.Create(w, h, false) && s_popSoft2d.fb.color) {
			s_popSoft2d.ClearArgb(0);
			const int ox = (int)(sinf((float)animTick * 0.035f) * 2.f);
			s_popSoft2d.FillEllipse(w / 5 + ox, h * 3 / 4, max(3, w / 6), max(2, h / 5), pink, 22);
			if (rowPill)
				s_popSoft2d.FillEllipse(w * 4 / 5 - ox, h / 3, max(3, w / 7), max(2, h / 4), lav, 18);
			BYTE a2 = (BYTE)(rowPill ? (78 + boost * 18) : (48 + boost * 14));
			premultPresent(s_popSoft2d.fb, a2);
		}
		--s_popSoftBusy;
	}

	static void PopupSoftGem(CDC& dc, const CRect& rc, int animTick)
	{
		// セパレータ中央の小さな Soft 紙片（控えめ）。通常は SoftBoost 時のみ。
		if (s_popSoftDisabled || s_popSoftBusy || rc.Width() < 8 || rc.Height() < 8) return;
		if (rc.Width() > 48 || rc.Height() > 48) return;
		if (!savedata.popupMenuSoftBoost) return;
		++s_popSoftBusy;
		const int w = rc.Width();
		const int h = rc.Height();
		if (s_popSoft3d.fb.w != w || s_popSoft3d.fb.h != h)
			s_popSoft3d.Create(w, h);
		if (s_popSoft3d.fb.color && s_popSoft3d.fb.w == w && s_popSoft3d.fb.h == h) {
			s_popSoft3d.fb.Clear(GdiSoftFB::PackBGRA(0, 0, 0, 0), 1e9f);
			s_popSoft3d.alphaBlend = true;
			s_popSoft3d.depthTest = true;
			s_popSoft3d.depthWrite = true;
			s_popSoft3d.fogMode = GdiSoft3D::FogNone;
			s_popSoft3d.edgeOverlay = false;
			s_popSoft3d.dofEnable = false;
			s_popSoft3d.postVignette = s_popSoft3d.postGlow = s_popSoft3d.postSaturate = false;
			const float tf = (float)animTick * 0.04f;
			s_popSoft3d.cam.yawDeg = -22.f + sinf(tf) * 8.f;
			s_popSoft3d.cam.pitchDeg = 36.f;
			s_popSoft3d.cam.zoom = 1.15f;
			float boxes[1][6] = { { -0.5f, 0.5f, 0.f, 0.2f, -0.5f, 0.5f } };
			s_popSoft3d.SetViewportFit(boxes, 1);
			const COLORREF c = CCC_IsInwoman() ? RGB(255, 140, 185) : RGB(230, 170, 230);
			s_popSoft3d.DrawBox(-0.28f, 0.28f, 0.12f, -0.28f, 0.28f, c, 0.f);
			s_popSoft3d.fb.PresentAlpha(dc.GetSafeHdc(), rc.left, rc.top, savedata.popupMenuSoftBoost ? (BYTE)120 : (BYTE)95);
		}
		--s_popSoftBusy;
	}

// ---- パネル／行チップ描画（GDI）。飛行中の余白は kChipChromaKey → ULW で α=0 ----
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
		CRect gem(mid - 7, y - 7, mid + 8, y + 8);
		PopupSoftGem(dc, gem, (int)(::GetTickCount64() / 48));
	}

	static void DrawJkBackdrop(CDC& dc, const CRect& rc, int /*animTick*/, float doorT = -1.f, BOOL softOn = TRUE)
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
		// Soft2D/3D 透過プレート（巨大面は PopupSoftPlate 内でスキップ）
		if (softOn)
			PopupSoftPlate(dc, rc, savedata.popupMenuSoftBoost ? 1 : 0, (int)(::GetTickCount64() / 50), doorT);
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
		// Soft plate は行全面だと重い。小さなジェムは SoftBoost 時のみ
		if (savedata.popupMenuSoftBoost) {
			CRect gem(hr.left + 3, cy - 6, hr.left + 15, cy + 6);
			PopupSoftGem(dc, gem, (int)(::GetTickCount64() / 48));
		}
	}

	static void DrawPanelChrome(CDC& dc, const CRect& rc)
	{
		// 枠のみ（背景の一枚感を壊さない）
		dc.Draw3dRect(&rc, PopupBorderLite(), PopupBorderDark());
		CRect inn = rc; inn.DeflateRect(1, 1);
		dc.Draw3dRect(&inn, RGB(255, 255, 255), BlendRGB(PopupBorderDark(), PopupBg(), 150));
	}

	static void PopupSoftFlightAccent(CDC& dc, const CRect& chip, int style, float flightT, int fade, int animTick, int rowIdx)
	{
		// 飛行中 Soft3D。フル幅×全行×毎フレは重いので:
		// ・小さな固定 FB（リボン付近）
		// ・5フレに1回
		// ・同時描画は最大 2 行
		if (s_popSoftDisabled || s_popSoftBusy || chip.Width() < 20 || chip.Height() < 12) return;
		if (fade < 40) return;
		// SoftBoost オフ時は飛行 Soft を省略（入場は GDI だけ）
		if (!savedata.popupMenuSoftBoost) return;
		const float t = (flightT < 0.f) ? 0.f : (flightT > 1.f ? 1.f : flightT);
		const float remain = 1.f - t;
		if (remain < 0.02f) return;
		if (((animTick + rowIdx) % 5) != 0) return;
		static int s_softFlightBudget = 0;
		static ULONGLONG s_softFlightTick = 0;
		const ULONGLONG now = ::GetTickCount64();
		if (now != s_softFlightTick) { s_softFlightTick = now; s_softFlightBudget = 0; }
		if (s_softFlightBudget >= 2) return;
		++s_softFlightBudget;

		++s_popSoftBusy;
		const int boost = savedata.popupMenuSoftBoost ? 1 : 0;
		const COLORREF pink = CCC_IsInwoman() ? RGB(255, 160, 200) : RGB(255, 190, 220);
		const COLORREF lav = CCC_IsInwoman() ? RGB(240, 150, 210) : RGB(210, 190, 255);
		// リボン側の小さなパッチだけ（全幅 Soft の ~1/6）
		const int aw = min(48, max(28, chip.Width() / 5));
		const int ah = min(28, chip.Height());
		CRect accent(chip.left + 2, chip.top + (chip.Height() - ah) / 2,
			chip.left + 2 + aw, chip.top + (chip.Height() - ah) / 2 + ah);
		const int w = accent.Width();
		const int h = accent.Height();
		auto premultPresent = [&](BYTE constA) {
			if (!s_popSoft3d.fb.color || !s_popSoft3d.fb.hdc || s_popSoft3d.fb.w != w || s_popSoft3d.fb.h != h)
				return;
			const int n = w * h;
			for (int i = 0; i < n; ++i) {
				const DWORD pix = s_popSoft3d.fb.color[i];
				const BYTE a = GdiSoftFB::A(pix);
				if (a == 0) { s_popSoft3d.fb.color[i] = 0; continue; }
				if (a >= 255) continue;
				s_popSoft3d.fb.color[i] = GdiSoftFB::PackBGRA(a,
					(BYTE)(GdiSoftFB::R(pix) * a / 255),
					(BYTE)(GdiSoftFB::G(pix) * a / 255),
					(BYTE)(GdiSoftFB::B(pix) * a / 255));
			}
			s_popSoft3d.fb.PresentAlpha(dc.GetSafeHdc(), accent.left, accent.top, constA);
		};
		if (s_popSoft3d.fb.w != w || s_popSoft3d.fb.h != h)
			s_popSoft3d.Create(w, h);
		if (!s_popSoft3d.fb.color || s_popSoft3d.fb.w != w || s_popSoft3d.fb.h != h) {
			--s_popSoftBusy;
			return;
		}
		s_popSoft3d.fb.Clear(GdiSoftFB::PackBGRA(0, 0, 0, 0), 1e9f);
		s_popSoft3d.alphaBlend = true;
		s_popSoft3d.depthTest = true;
		s_popSoft3d.depthWrite = true;
		s_popSoft3d.fogMode = GdiSoft3D::FogNone;
		s_popSoft3d.edgeOverlay = false;
		s_popSoft3d.dofEnable = false;
		s_popSoft3d.postVignette = s_popSoft3d.postGlow = s_popSoft3d.postSaturate = false;

		float yaw = -16.f, pitch = 36.f, zoom = 1.15f;
		switch (style) {
		case POPUP_ANIM_SPIRAL:
			yaw = -20.f + remain * 220.f;
			pitch = 30.f + remain * 18.f;
			zoom = 1.1f + remain * 0.3f;
			break;
		case POPUP_ANIM_BIGBANG:
			yaw = -18.f + remain * 160.f * ((rowIdx & 1) ? 1.f : -1.f);
			pitch = 28.f + remain * 24.f;
			zoom = 1.05f + remain * 0.35f;
			break;
		case POPUP_ANIM_PETAL:
			yaw = -14.f + sinf(remain * 3.1f) * 28.f * remain;
			pitch = 42.f - remain * 10.f;
			break;
		case POPUP_ANIM_ZIPPER:
			yaw = -12.f + remain * 55.f * ((rowIdx & 1) ? 1.f : -1.f);
			pitch = 38.f;
			break;
		case POPUP_ANIM_AURORA:
			yaw = -22.f + sinf(t * 4.f) * 20.f * remain;
			pitch = 34.f + cosf(t * 3.f) * 8.f * remain;
			break;
		case POPUP_ANIM_CASCADE:
			yaw = -16.f;
			pitch = 20.f + remain * 35.f;
			break;
		case POPUP_ANIM_SLIDE:
			yaw = -10.f + remain * 40.f;
			pitch = 36.f;
			break;
		case POPUP_ANIM_POP:
			yaw = -18.f;
			pitch = 32.f;
			zoom = 0.85f + t * 0.4f;
			break;
		default:
			yaw = -16.f + remain * 24.f;
			pitch = 36.f + remain * 8.f;
			break;
		}
		s_popSoft3d.cam.yawDeg = yaw;
		s_popSoft3d.cam.pitchDeg = pitch;
		s_popSoft3d.cam.zoom = zoom + boost * 0.05f;
		float boxes[1][6] = { { -0.55f, 0.55f, 0.f, 0.28f, -0.45f, 0.45f } };
		s_popSoft3d.SetViewportFit(boxes, 1);
		// 1プリミティブに絞る（Sphere/Wave は飛行中は重い）
		if (style == POPUP_ANIM_POP && remain > 0.4f)
			s_popSoft3d.DrawSphere(0.f, 0.06f, 0.f, 0.2f + remain * 0.08f, pink, 8, 6);
		else
			s_popSoft3d.DrawNeonBox(-0.32f, 0.32f, 0.14f + remain * 0.03f, -0.28f, 0.28f,
				(style == POPUP_ANIM_AURORA) ? lav : pink, 0.f);

		BYTE a = (BYTE)(70 + (int)(remain * 70.f) + boost * 18);
		if (a > 160) a = 160;
		if (fade < 180) {
			const int af = (int)a * fade / 180;
			a = (BYTE)((af < 36) ? 36 : af);
		}
		premultPresent(a);
		--s_popSoftBusy;
	}

	// BigBang: 行ごとに独立したチップ（背景＋リボン＋枠）。矩形を不透明で密閉（クロマ穴を作らない）
	static void DrawRowChip(CDC& dc, const CRect& chip, int animTick, int fade, int style = -1, float flightT = 1.f, int rowIdx = 0)
	{
		if (chip.Width() <= 1 || chip.Height() <= 1 || fade < 8) return;
		const int clip = dc.SaveDC();
		dc.IntersectClipRect(&chip);
		// 下地を必ずベタ塗り（グラデ穴・キー混入防止）
		dc.FillSolidRect(&chip, PopupBg());
		const BOOL flying = (style >= POPUP_ANIM_CASCADE && style < POPUP_ANIM_COUNT && flightT < 0.995f);
		// 飛行中は Soft 付き DrawJkBackdrop を使わない（行×毎フレ Soft3D が重い）。GDI のみ。
		if (!flying && fade >= 220) {
			DrawJkBackdrop(dc, chip, animTick);
			DrawTornRibbon(dc, chip, animTick);
		} else {
			const COLORREF c0 = CCC_IsInwoman() ? RGB(255, 220, 236) : RGB(255, 232, 244);
			const COLORREF c1 = CCC_IsInwoman() ? RGB(255, 192, 224) : RGB(232, 214, 255);
			FillVGrad(dc, chip, BlendRGB(PopupBg(), c0, fade), BlendRGB(PopupBg(), c1, fade));
			CRect rib(chip.left, chip.top, chip.left + CCUSTOM_POPUP_RIBBON_W, chip.bottom);
			const COLORREF r0 = CCC_IsInwoman() ? RGB(255, 108, 168) : RGB(158, 140, 228);
			dc.FillSolidRect(&rib, BlendRGB(PopupBg(), r0, fade));
			if (!flying && fade >= 180)
				DrawTornRibbon(dc, chip, animTick);
		}
		if (flying)
			PopupSoftFlightAccent(dc, chip, style, flightT, fade, animTick, rowIdx);
		DrawPanelChrome(dc, chip);
		dc.RestoreDC(clip);
	}

	// 骨格フォントサイズスライダー。savedata を即書き、開チェーンを Relayout。
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

// 固定配列メニューの構築。HWND はまだ作らない（Track→CreatePopupAt）。
// モーダル状態（m_tracking / m_done / m_result / m_hot / m_openSub）と飛行フラグをゼロ初期化。
// 所有フォント・メモリBMPはデストラクタで破棄。
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

// Track 途中の例外／早期 return でも s_trackingRoot を残さない。
// Speana・Soft3D・淫女タイマが永久停止しないよう根を切ってから Reset。
// 所有 HFONT とバックバッファ、残 HWND を破棄。
CCustomPopupMenu::~CCustomPopupMenu()
{
	// Track 中例外／途中 return でも Speana・Soft3D・淫女タイマが永久停止しないよう掃除
	if (s_trackingRoot == this) {
		s_trackingRoot = NULL;
		s_trackingHwnd = NULL;
	}
	Reset();
	if (m_fontOwned) { ::DeleteObject(m_fontOwned); m_fontOwned = NULL; }
	if (m_memBmp.GetSafeHandle()) m_memBmp.DeleteObject();
	if (GetSafeHwnd()) DestroyWindow();
}

// 項目・サブ・内包 CCustom* HWND を破棄し、骨格注入フラグも戻す。
// 再 Track の前に呼ぶ。開いているツリーは DestroyPopupTree。
// m_stickyCount も 0。呼び出し側が SetStickyLeading し直す。
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

// 固定長 wchar へ lstrcpyn。dst 必須。src==NULL なら空文字。
// バッファ末尾は API 側で終端される前提。
void CCustomPopupMenu::CopyText(wchar_t* dst, int dstN, LPCTSTR src)
{
	if (!dst || dstN <= 0) return;
	dst[0] = 0;
	if (!src) return;
	lstrcpynW(dst, src, dstN);
}

// スライダー／Edit／Combo／List／Range／Progress／Button。
// ラベル行 + 子 HWND。ホバーピル対象外（操作は子へ）。
BOOL CCustomPopupMenu::IsInteractiveKind(int kind) const
{
	return kind == CCUSTOM_POPUP_SLIDER || kind == CCUSTOM_POPUP_EDIT
		|| kind == CCUSTOM_POPUP_COMBO || kind == CCUSTOM_POPUP_LIST
		|| kind == CCUSTOM_POPUP_RANGE || kind == CCUSTOM_POPUP_PROGRESS
		|| kind == CCUSTOM_POPUP_BUTTON;
}

// 骨格専用 ID（アクリル／Soft強め／フォント／KPI／MIDI KPI|VST／描画方法）。
// Track の戻り値には出さない（呼び出し元へは 0）。
BOOL CCustomPopupMenu::IsChromeCommand(UINT id) const
{
	return id == CCUSTOM_POPUP_ID_ACRYLIC
		|| id == CCUSTOM_POPUP_ID_SOFTBOOST
		|| id == CCUSTOM_POPUP_ID_FONT_BOLD
		|| id == CCUSTOM_POPUP_ID_FONT_ITALIC
		|| id == CCUSTOM_POPUP_ID_FONT_FACE
		|| id == CCUSTOM_POPUP_ID_KPI_DL
		|| id == CCUSTOM_POPUP_ID_KPI_RELOAD
		|| id == CCUSTOM_POPUP_ID_MID_KPI
		|| id == CCUSTOM_POPUP_ID_MID_VST
		|| (id >= CCUSTOM_POPUP_ID_ANIM0
			&& id < CCUSTOM_POPUP_ID_ANIM0 + (UINT)POPUP_ANIM_COUNT);
}

// 全 Add* の共通登録。添字類は -1、A-B は未設定 -1。
// 配列上限超過は FALSE。tip があれば hasTip。
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

// 通常コマンド行。クリックで CloseChain(id)。
// 骨格 ID は Track が 0 に正規化する。
BOOL CCustomPopupMenu::AddCommand(UINT id, LPCTSTR text, LPCTSTR tip, BOOL enabled)
{ return AddItemBase(CCUSTOM_POPUP_CMD, id, text, tip, enabled, FALSE); }

// レ点トグル行。チェック描画とバウンスは HandleChromeClick / OnLButtonDown。
// 呼び出し元は Track 戻り値または FindItem で状態を読む。
BOOL CCustomPopupMenu::AddCheck(UINT id, LPCTSTR text, BOOL checked, LPCTSTR tip, BOOL enabled)
{ return AddItemBase(CCUSTOM_POPUP_CHECK, id, text, tip, enabled, checked); }

// 区切り線。連続セパレータは二重線になるので弾く（成功扱い）。
// 高さは CCUSTOM_POPUP_SEP_H（DPI スケール）。
BOOL CCustomPopupMenu::AddSeparator()
{
	// 連続セパレータは見た目が二重線になるので弾く
	if (m_itemCount > 0 && m_items[m_itemCount - 1].kind == CCUSTOM_POPUP_SEP)
		return TRUE;
	return AddItemBase(CCUSTOM_POPUP_SEP, 0, NULL, NULL, TRUE, FALSE);
}

// 子 CCustomPopupMenu を new してこのインスタンスが所有する。
// ホバーで OpenSubAt。失敗時は new した子を delete して NULL。
// 子は SetSkipChrome(TRUE) しない限り、ルート骨格は親だけ。
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

// ラベル行 + CCustomSliderCtrl（スライダー方式）。cb はドラッグ中も呼ばれる。
// vmin/vmax は入れ替え可。id は Find/Get 用で 0 でもよい。
// 実 HWND は SyncEmbeddedChildren で Create。
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

// Edit/Combo/List の文字列セットを m_choiceSets 固定配列へコピー。
// 失敗は -1（件数 0 またはセット上限）。
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

// 初期文字列は choiceSet[0] に置く。Create 時 SetWindowText の EN_CHANGE は抑止。
// NotifyEditFromHwnd が cb を呼ぶ。
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

// ドロップダウン。curSel は範囲クランプ。物理 index で保持。
// CBN_SELCHANGE → NotifyChoiceFromHwnd。リスト HWND はチェーンヒットに含める。
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

// リストボックス行（高さ LIST_H）。LBN_SELCHANGE で cb。
// スクロール時は sticky 下の本文クリップと同期して Show/Hide。
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

// 再生ヘッド + ループ選択 + A-B。未設定 A-B は -1 のまま。
// LiveMirrorRange で Track 中の再生追従。ドラッグ中は無視。
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

// 表示専用プログレス。SetProgressPos で追従。showPercent で % 描画。
// 操作ヒットは子 HWND。行ピルは出さない。
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

// 内包 CCustomStandardButton。closeOnClick なら BN_CLICKED で CloseChain(id)。
// cb は閉じる前に呼ばれる。
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

// popupMenuPoint/Bold/Italic/Face をクランプして MpPersistSavedataQuick。
// プレビュー中の面は CommitFace 側で savedata へ書く。
void CCustomPopupMenu::PersistPopupFont()
{
	ClampPopupFontSave();
	MpPersistSavedataQuick();
}

// savedata（またはホバープレビュー面）から HFONT を作り直す。
// 旧 m_fontOwned を Delete。失敗時は DEFAULT_GUI_FONT。
// ポイント・太字・斜体は全カスタムメニュー共通。
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

// ルートと全サブへ RebuildMenuFont。開いている窓は背景のみ Invalidate。
// 子 HWND は RefreshEmbeddedChildren（全窓 Redraw は点滅の元）。
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

// フォントサイズ変更後、開いている窓の寸法・sticky・スクロールを張り直す。
// 飛行余白 pad を除いたコンテンツ原点で SetWindowPos（z は触らない）。
// 本文高さ不足時も sticky 下に最低 3 行相当を確保。
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

// フォント面ホバー中の一時プレビュー。ルートの m_previewing に載せる。
// 確定はクリック→CommitFace。離脱は ClearPreviewFace。
void CCustomPopupMenu::ApplyPreviewFace(LPCTSTR face)
{
	CCustomPopupMenu* root = RootMenu();
	if (!root || !face || !face[0]) return;
	root->m_previewing = TRUE;
	lstrcpynW(root->m_previewFace, face, 32);
	root->RefreshFontChain();
}

// ホバープレビュー解除。savedata の面へ戻して RefreshFontChain。
// プレビュー中でなければ何もしない。
void CCustomPopupMenu::ClearPreviewFace()
{
	CCustomPopupMenu* root = RootMenu();
	if (!root || !root->m_previewing) return;
	root->m_previewing = FALSE;
	root->m_previewFace[0] = 0;
	root->RefreshFontChain();
}

// 面を savedata.popupMenuFace へ確定保存し、プレビューを終了。
// 以降のメニューにも残る。
void CCustomPopupMenu::CommitFace(LPCTSTR face)
{
	if (!face || !face[0]) return;
	lstrcpynW(savedata.popupMenuFace, face, _countof(savedata.popupMenuFace));
	m_previewing = FALSE;
	m_previewFace[0] = 0;
	PersistPopupFont();
	RefreshFontChain();
}

// ルート先頭へ骨格を注入（サブ・skipChrome・再入は無視）。
// フォント（サイズ/太字/斜体 + 面一覧、先頭4行 sticky）、アクリル、立体アクセント強め。
// プラグイン: KPI DL / 再読込、MIDI再生 KPI優先 / VST優先（midPlayPrefer）。
// メニュー描画方法（popupMenuAnim）。最後に呼び出し元項目を後ろへ戻す。
// 骨格クリックは HandleChromeClick。Track 戻り値は骨格 ID なら 0。
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
			savedata.popupMenuBold ? TRUE : FALSE,
			LL14(L"メニュー文字を太字にします（全カスタムメニュー共通・すぐ反映）",
				L"Bold menu text (all custom menus; applies immediately)",
				L"Texte du menu en gras (tous les menus; immédiat)",
				L"Testo menu in grassetto (tutti i menu; subito)",
				L"Texto del menu en negrita (todos; inmediato)",
				L"메뉴 글자를 굵게 (모든 커스텀 메뉴, 즉시)",
				L"菜单文字加粗（全部自定义菜单，立即生效）",
				L"نص القائمة عريض (كل القوائم، فوري)",
				L"Жирный текст меню (все меню, сразу)",
				L"Menütext fett (alle Menüs, sofort)",
				L"Texto do menu em negrito (todos; imediato)",
				L"Menutekst vet (alle menu's; meteen)",
				L"Pogrub tekst menu (wszystkie; od razu)",
				L"Menu yazisini kalin yap (tum menu, aninda)"));
		fontSub->AddCheck(CCUSTOM_POPUP_ID_FONT_ITALIC,
			LL14(L"斜体", L"Italic", L"Italique", L"Corsivo", L"Cursiva",
				L"기울임", L"斜体", L"مائل", L"Курсив", L"Kursiv",
				L"Italico", L"Cursief", L"Kursywa", L"Italik"),
			savedata.popupMenuItalic ? TRUE : FALSE,
			LL14(L"メニュー文字を斜体にします（全カスタムメニュー共通・すぐ反映）",
				L"Italic menu text (all custom menus; applies immediately)",
				L"Texte du menu en italique (tous; immédiat)",
				L"Testo menu in corsivo (tutti; subito)",
				L"Texto del menu en cursiva (todos; inmediato)",
				L"메뉴 글자를 기울임 (모든 커스텀 메뉴, 즉시)",
				L"菜单文字斜体（全部自定义菜单，立即生效）",
				L"نص القائمة مائل (كل القوائم، فوري)",
				L"Курсив меню (все меню, сразу)",
				L"Menütext kursiv (alle Menüs, sofort)",
				L"Texto do menu em italico (todos; imediato)",
				L"Menutekst cursief (alle menu's; meteen)",
				L"Kursywa tekstu menu (wszystkie; od razu)",
				L"Menu yazisini italik yap (tum menu, aninda)"));
		fontSub->AddSeparator();
		fontSub->SetStickyLeading(4); // サイズ/太字/斜体/セパレータを固定
		EnsureFaceList();
		const int room = CCUSTOM_POPUP_MAX_ITEMS - fontSub->m_itemCount - 1;
		const int n = min(s_faceCount, max(0, room));
		LPCTSTR faceTip = LL14(
			L"ホバーでプレビュー、クリックでこの書体に確定（次回以降のメニューにも保存）",
			L"Hover to preview; click to apply this face (saved for later menus)",
			L"Survol = apercu; clic = appliquer (enregistre)",
			L"Passa = anteprima; clic = applica (salva)",
			L"Pasar = vista previa; clic = aplicar (guarda)",
			L"호버 미리보기, 클릭으로 이 서체 확정(저장)",
			L"悬停预览，点击采用该字体（会保存）",
			L"مرر للمعاينة؛ انقر لتطبيق الخط (يُحفظ)",
			L"Наведение — превью; клик — применить (сохраняется)",
			L"Hover=Vorschau; Klick=Übernehmen (gespeichert)",
			L"Passe=preview; clique=aplicar (salva)",
			L"Hover=voorbeeld; klik=toepassen (opgeslagen)",
			L"Najazd=podglad; klik=zastosuj (zapis)",
			L"Gezdir=onizleme; tikla=uygula (kaydedilir)");
		for (int i = 0; i < n; ++i) {
			const BOOL cur = (savedata.popupMenuFace[0]
				&& _wcsicmp(savedata.popupMenuFace, s_faces[i]) == 0) ? TRUE : FALSE;
			if (cur)
				fontSub->AddCheck(CCUSTOM_POPUP_ID_FONT_FACE, s_faces[i], TRUE, faceTip);
			else
				fontSub->AddCommand(CCUSTOM_POPUP_ID_FONT_FACE, s_faces[i], faceTip);
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

	AddCheck(CCUSTOM_POPUP_ID_SOFTBOOST,
		LL14(L"立体アクセント強め", L"Stronger 3D accent", L"Accent 3D fort", L"Accento 3D forte", L"Acento 3D fuerte",
			L"입체 액센트 강하게", L"加强立体强调", L"لمسة ثلاثية أقوى", L"Сильнее 3D-акцент", L"Stärkerer 3D-Akzent",
			L"Acento 3D forte", L"Sterkere 3D-accent", L"Silniejszy akcent 3D", L"Daha guclu 3B vurgu"),
		savedata.popupMenuSoftBoost ? TRUE : FALSE,
		LL14(L"メニュー面の Soft 透過ボックス／グロウを強めます", L"Boost Soft translucent boxes/glow on the menu surface",
			L"Renforce les boites Soft translucides/glow", L"Aumenta box Soft traslucidi/glow", L"Aumenta cajas Soft translucidas/glow",
			L"메뉴 Soft 반투명 박스/글로우를 강하게", L"加强菜单 Soft 半透明盒/光晕", L"تعزيز صناديق Soft الشفافة/التوهج",
			L"Усилить Soft-полупрозрачные боксы/свечение", L"Soft-Translucent-Boxen/Glow verstärken",
			L"Reforcar caixas Soft translucidas/glow", L"Soft doorschijnende dozen/glow versterken",
			L"Wzmocnij polprzezroczyste Soft boxy/glow", L"Soft yari saydam kutu/glow guclendir"));

	CCustomPopupMenu* kpiSub = AddSubMenu(
		LL14(L"プラグイン", L"Plugins", L"Plugins", L"Plugin", L"Plugins",
			L"플러그인", L"插件", L"الإضافات", L"Плагины", L"Plugins",
			L"Plugins", L"Plug-ins", L"Wtyczki", L"Eklentiler"),
		LL14(L"プラグインのダウンロード／再読込", L"Download or reload plugins",
			L"Telecharger ou relire les plugins", L"Scarica o ricarica i plugin",
			L"Descargar o recargar plugins", L"플러그인 다운로드/다시 읽기",
			L"下载或重新加载插件", L"تنزيل أو إعادة تحميل الإضافات",
			L"Скачать или перечитать плагины", L"Plugins herunterladen/neu laden",
			L"Descarregar ou recarregar plugins", L"Plugins downloaden/herladen",
			L"Pobierz lub wczytaj ponownie wtyczki", L"Eklentileri indir/yeniden yukle"));
	if (kpiSub) {
		kpiSub->SetSkipChrome(TRUE);
		kpiSub->AddCommand(CCUSTOM_POPUP_ID_KPI_DL,
			LL14(L"KPIプラグインダウンロード", L"Download KPI plugins", L"Telecharger plugins KPI",
				L"Scarica plugin KPI", L"Descargar plugins KPI", L"KPI 플러그인 다운로드",
				L"下载 KPI 插件", L"تنزيل إضافات KPI", L"Скачать KPI-плагины", L"KPI-Plugins herunterladen",
				L"Descarregar plugins KPI", L"KPI-plugins downloaden", L"Pobierz wtyczki KPI",
				L"KPI eklentilerini indir"),
			LL14(L"公式 Plugins.zip を取得し、このプログラムと同じフォルダへ展開します",
				L"Download official Plugins.zip and extract next to this program",
				L"Telecharger Plugins.zip officiel et extraire a cote du programme",
				L"Scarica Plugins.zip ufficiale e estrai accanto al programma",
				L"Descargar Plugins.zip oficial y extraer junto al programa",
				L"공식 Plugins.zip을 받아 이 프로그램과 같은 폴더에 펼칩니다",
				L"下载官方 Plugins.zip 并解压到本程序同目录",
				L"تنزيل Plugins.zip الرسمي واستخراجه بجانب البرنامج",
				L"Скачать официальный Plugins.zip и распаковать рядом с программой",
				L"Offizielles Plugins.zip laden und neben das Programm entpacken",
				L"Descarregar Plugins.zip oficial e extrair ao lado do programa",
				L"Officiele Plugins.zip downloaden en naast het programma uitpakken",
				L"Pobierz oficjalny Plugins.zip i rozpakuj obok programu",
				L"Resmi Plugins.zip indirip programla ayni klasore ac"));
		kpiSub->AddCommand(CCUSTOM_POPUP_ID_KPI_RELOAD,
			LL14(L"プラグイン再読み込み", L"Reload plugins", L"Relire plugins",
				L"Ricarica plugin", L"Recargar plugins", L"플러그인 다시 읽기",
				L"重新加载插件", L"إعادة تحميل الإضافات", L"Перечитать плагины", L"Plugins neu laden",
				L"Recarregar plugins", L"Plugins herladen", L"Wczytaj ponownie wtyczki",
				L"Eklentileri yeniden yukle"),
			LL14(L"exe フォルダ配下の .kpi を最初から読み直します",
				L"Re-scan .kpi files under the exe folder from scratch",
				L"Relire les .kpi sous le dossier exe",
				L"Rileggi i .kpi sotto la cartella exe",
				L"Volver a leer los .kpi bajo la carpeta exe",
				L"exe 폴더 아래 .kpi를 처음부터 다시 읽습니다",
				L"从头重新扫描 exe 目录下的 .kpi",
				L"إعادة قراءة ملفات .kpi تحت مجلد البرنامج",
				L"Перечитать .kpi в папке exe с начала",
				L".kpi unter dem Exe-Ordner neu einlesen",
				L"Relê os .kpi sob a pasta do exe",
				L".kpi onder de exe-map opnieuw inlezen",
				L"Wczytaj ponownie pliki .kpi w folderze exe",
				L"exe klasorundeki .kpi dosyalarini bastan oku"));
		kpiSub->AddSeparator();
		kpiSub->AddCheck(CCUSTOM_POPUP_ID_MID_KPI,
			LL14(L"MIDI再生: KPI優先", L"MIDI play: Prefer KPI", L"MIDI: Preferer KPI", L"MIDI: Preferisci KPI", L"MIDI: Preferir KPI",
				L"MIDI 재생: KPI 우선", L"MIDI播放: 优先KPI", L"MIDI: تفضيل KPI", L"MIDI: предпочитать KPI", L"MIDI: KPI bevorzugen",
				L"MIDI: Preferir KPI", L"MIDI: KPI verkiezen", L"MIDI: Preferuj KPI", L"MIDI: KPI tercih"),
			savedata.midPlayPrefer == 0,
			LL14(L".mid を KPI プラグインで再生します", L"Play .mid via KPI plugins", L"Lire .mid via plugins KPI", L"Riproduci .mid via plugin KPI", L"Reproducir .mid vía plugins KPI",
				L".mid를 KPI 플러그인으로 재생", L"通过 KPI 插件播放 .mid", L"تشغيل .mid عبر إضافات KPI", L"Воспроизводить .mid через KPI", L".mid über KPI-Plugins wiedergeben",
				L"Tocar .mid via plugins KPI", L".mid via KPI-plugins afspelen", L"Odtwarzaj .mid przez wtyczki KPI", L".mid dosyalarini KPI eklentisiyle cal"));
		kpiSub->AddCheck(CCUSTOM_POPUP_ID_MID_VST,
			LL14(L"MIDI再生: VST優先", L"MIDI play: Prefer VST", L"MIDI: Preferer VST", L"MIDI: Preferisci VST", L"MIDI: Preferir VST",
				L"MIDI 재생: VST 우선", L"MIDI播放: 优先VST", L"MIDI: تفضيل VST", L"MIDI: предпочитать VST", L"MIDI: VST bevorzugen",
				L"MIDI: Preferir VST", L"MIDI: VST verkiezen", L"MIDI: Preferuj VST", L"MIDI: VST tercih"),
			savedata.midPlayPrefer == 1,
			LL14(L".mid / プロジェクトを自前 VST ホストで再生します", L"Play .mid/projects via built-in VST host", L"Lire .mid/projets via hote VST integre", L"Riproduci .mid/progetti via host VST", L"Reproducir .mid/proyectos vía host VST",
				L".mid/프로젝트를 내장 VST 호스트로 재생", L"通过内置 VST 主机播放 .mid/项目", L"تشغيل .mid/المشاريع عبر مضيف VST", L"Воспроизводить .mid/проекты через встроенный VST-хост", L".mid/Projekte über eingebauten VST-Host",
				L"Tocar .mid/projetos via host VST", L".mid/projecten via ingebouwde VST-host", L"Odtwarzaj .mid/projekty przez wbudowany host VST", L".mid/projeleri dahili VST host ile cal"));
	}

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

// DPI スケールで行高・幅を測り、各 item.rc（スクロール前コンテンツ座標）を置く。
// 先頭 m_stickyCount 行の下端が m_stickyH。本文だけが後で m_scrollY する。
// レ点列はチェック無し行も確保（ラベル位置揃え）。
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
		// 固定ヘッダ下端。本文スクロール（m_scrollY）はこの下から
		if (i + 1 == m_stickyCount)
			m_stickyH = y;
	}
	m_contentH = y + SH(8);
	m_menuH = m_contentH;
	m_scrollY = 0;
	m_scrollMax = 0;
}

// 内包 CCustom* をラベル行下へ配置。未 Create ならここで Create。
// sticky 行は常に画面内。本文は sticky 下＋飛行 pad の交差で Show/Hide。
// 毎回 UpdateWindow すると定着直後に点滅するので、移動／初回表示だけ描く。
// Edit の初期 SetWindowText は m_suppressEditNotify で EN_CHANGE を無視。
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

// FALSE で全子 HWND を隠す（入場／退場チップ飛行中）。
// TRUE は onScreen 判定込みの SyncEmbeddedChildren に任せる。
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

// 親 BufferedPaint が子を潰したあとの再描画。ERASE 無し（白フラッシュ防止）。
// 可視子だけ RDW_UPDATENOW。
void CCustomPopupMenu::RefreshEmbeddedChildren()
{
	if (!GetSafeHwnd()) return;
	for (HWND h = ::GetWindow(m_hWnd, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT)) {
		if (!::IsWindowVisible(h)) continue;
		// ERASE 無し: 白フラッシュ→再描画の点滅を避ける
		::RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
	}
}

// 定着後に子 HWND を確実表示。マウス未移動でも着地させる。
// WM_APP+0x51C を Post して親描画後の潰しを再 Refresh。
void CCustomPopupMenu::RevealEmbeddedAfterAnim()
{
	if (!GetSafeHwnd()) return;
	ShowEmbedded(TRUE); // 内で Sync（位置合わせ＋Show）
	RefreshEmbeddedChildren();
	// 親 BufferedPaint 後の潰し対策（マウスを動かさなくても着地させる）
	PostMessage(WM_APP + 0x51C, 0, 0);
}

// 入場飛行が視覚的に静止したか（fade≈255 かつ ox/oy≈0）。
// phase!=1 は FALSE。橋渡し判定に使い、行き過ぎで点滅させない。
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

// 行オフセットとフェード。未出現は FALSE。
// m_lineAnimPhase: 0=idle（オフセット無し） / 1=enter / 2=exit。
// クラシックは常に静止。放射系は起点距離、インデックス系は上から遅延。
// BigBang は easeOutBack の行き過ぎ→整列。退場は逆スタガ。
BOOL CCustomPopupMenu::CalcLineAnim(int idx, int* ox, int* oy, int* fade, float* pT) const
{
	if (ox) *ox = 0;
	if (oy) *oy = 0;
	if (fade) *fade = 256;
	if (pT) *pT = 1.f;
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
		auto doneIn = [&]() -> BOOL {
			if (pT) *pT = t;
			return TRUE;
		};

		if (style == POPUP_ANIM_BIGBANG) {
			float sx = 0.f, sy = 0.f;
			scatterXY(idx, m_lineAnimOrigin, &sx, &sy);
			const float e = easeOutBack(t); // >1 で行き過ぎ→戻る
			if (ox) *ox = (int)(sx * (1.f - e));
			if (oy) *oy = (int)(sy * (1.f - e));
			if (fade) *fade = fadeIn(t);
			return doneIn();
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
			return doneIn();
		}
		if (style == POPUP_ANIM_PETAL) {
			const float e = easeInOutSine(t);
			const float sway = sinf((float)idx * 1.71f + t * 2.4f) * 24.f * (1.f - e);
			if (ox) *ox = (int)sway;
			if (oy) *oy = (int)(52.f * (1.f - e)); // 下からふわり
			if (fade) *fade = fadeIn(t * 0.9f);
			return doneIn();
		}
		if (style == POPUP_ANIM_ZIPPER) {
			const float e = easeOutBack(t);
			const float side = (idx & 1) ? 1.f : -1.f;
			if (ox) *ox = (int)(side * 96.f * (1.f - min(e, 1.f)));
			if (oy) *oy = (int)(side * -10.f * (1.f - min(e, 1.f)));
			if (fade) *fade = fadeIn(t);
			return doneIn();
		}
		if (style == POPUP_ANIM_AURORA) {
			const float e = easeInOutSine(t);
			const float wave = sinf((float)idx * 0.62f + t * 3.6f) * 30.f;
			if (ox) *ox = (int)((-68.f + wave) * (1.f - e));
			if (oy) *oy = (int)(sinf((float)idx * 0.95f) * 18.f * (1.f - e));
			if (fade) *fade = fadeIn(t * 0.85f);
			return doneIn();
		}
		if (style == POPUP_ANIM_CASCADE) {
			const float e = easeOut(t);
			if (ox) *ox = 0;
			if (oy) *oy = (int)(-56.f * (1.f - e));
			if (fade) *fade = fadeIn(t);
			return doneIn();
		}
		if (style == POPUP_ANIM_SLIDE) {
			const float e = easeOut(t);
			if (ox) *ox = (int)(-72.f * (1.f - e));
			if (oy) *oy = 0;
			if (fade) *fade = fadeIn(t);
			return doneIn();
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
			return doneIn();
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
		return doneIn();
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
	if (pT) *pT = 1.f - t; // 退場: 着地度が下がる＝回転が増える
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

// 入場。クラシック／skipChrome は即時表示（ヒットずれ防止）。
// EXPAND は RGN が上下に開く。行チップ系は BeginChipFlight + ULW。
// phase=1。kAnimTimer 16ms と kSettleTimer（所要+余裕）で強制定着。
// 内包コントロールは飛行中隠す。
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
	// phase 1=enter（CalcLineAnim / ForceChipPresent）。0=idle 2=exit
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

// 決定後は複雑な行飛行退場を使わず、短いフェードで即閉じる。
// ULW 中なら最終パネルを不透明 GDI へ焼いてからレイヤ解除（点滅防止）。
// skipChrome サブは AnimateWindow 無しで Hide。
void CCustomPopupMenu::AnimateOut()
{
	if (!GetSafeHwnd() || !IsWindowVisible()) return;
	ShowEmbedded(FALSE);
	if (m_tip.GetSafeHwnd()) m_tip.Activate(FALSE);

	// 決定後は複雑な退場（行飛行など）を使わず、短いフェードで即閉じる
	KillTimer(kAnimTimer);
	KillTimer(kSettleTimer);
	m_lineAnimPhase = 0;
	m_bridgePanel = FALSE;

	s_popSoftDisabled = TRUE;
	if (m_flightPad > 0 || (GetExStyle() & WS_EX_LAYERED)) {
		const int pad = m_flightPad;
		const int fw = m_menuW;
		const int fh = m_menuH;
		BOOL painted = FALSE;
		if (fw > 0 && fh > 0) {
			CRect rc; GetClientRect(&rc);
			CClientDC dc(this);
			CDC finalDC; finalDC.CreateCompatibleDC(&dc);
			CBitmap finalBmp;
			if (finalBmp.CreateCompatibleBitmap(&dc, fw, fh)) {
				CBitmap* ob = finalDC.SelectObject(&finalBmp);
				if (pad > 0 && rc.Width() >= fw && rc.Height() >= fh) {
					CDC largeDC; largeDC.CreateCompatibleDC(&dc);
					CBitmap largeBmp;
					if (largeBmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height())) {
						CBitmap* ol = largeDC.SelectObject(&largeBmp);
						largeDC.FillSolidRect(&rc, PopupBg());
						PaintToDC(largeDC);
						::BitBlt(finalDC.GetSafeHdc(), 0, 0, fw, fh,
							largeDC.GetSafeHdc(), pad, pad, SRCCOPY);
						largeDC.SelectObject(ol);
						painted = TRUE;
					}
				} else {
					CRect fr(0, 0, fw, fh);
					finalDC.FillSolidRect(&fr, PopupBg());
					PaintToDC(finalDC);
					painted = TRUE;
				}
				EndChipFlight();
				if (GetExStyle() & WS_EX_LAYERED)
					ModifyStyleEx(WS_EX_LAYERED, 0);
				if (painted)
					BlitOpaqueToWindow(finalDC.GetSafeHdc(), fw, fh);
				finalDC.SelectObject(ob);
			}
		}
		if (!painted) {
			EndChipFlight();
			if (GetExStyle() & WS_EX_LAYERED)
				ModifyStyleEx(WS_EX_LAYERED, 0);
		}
	}
	::SetWindowRgn(m_hWnd, NULL, TRUE);
	s_popSoftDisabled = FALSE;

	if (m_skipChrome) {
		ShowWindow(SW_HIDE);
		return;
	}
	if (!::AnimateWindow(m_hWnd, 90, AW_BLEND | AW_HIDE))
		ShowWindow(SW_HIDE);
}

// ポップアップ HWND を作り AnimateIn。オーナーは WS_EX_NOACTIVATE | TOPMOST。
// 行チップは DROPSHADOW 無しクラス（右端黒バー対策）。作業領域へクランプ。
// ルートは右に収まらなければ左展開（サブが親に重なってクリック不能になるのを防ぐ）。
// クリック最近傍行が m_lineAnimOrigin（上下に広がる起点）。
// z は wndTopMost。子 HWND はアニメ完了まで隠す。
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
		// ルート: 右に収まらないときはクリック位置を右端にして左へ展開
		// （右端クランプだけだとサブが親に重なりクリック不能になる）
		if (!parentMenu && clickPt.x + m_menuW > mi.rcWork.right)
			screenPt.x = clickPt.x - m_menuW;
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
	if (CCC_IsInwoman()) { CCC_StartInwomanTimer(); SetTimer(kInwomanTimer, 180, NULL); }
	SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	AnimateIn();
	// 内包コントロールは行アニメ完了後に OnTimer で表示（出現中のちらつき防止）
	return TRUE;
}

// 飛行／RGN／タイマを止めて即非表示。退出アニメ無し。
// レイヤ解除より先に隠す（透明→不透明の最終フレ防止）。
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

// 開サブ・内包 HWND・チップを破棄。animateOut=FALSE はホバー離脱用（即消し）。
// AnimateOut の Peek 中に新規 Track が走ると二重メニューになるので、
// s_trackingRoot 解放はウィンドウ破棄後。
void CCustomPopupMenu::DestroyPopupTree(BOOL animateOut)
{
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
	// AnimateOut の Peek/Dispatch 中に新規 Track が走ると二重メニューになる。
	// ルート解放はウィンドウ破棄後に行う。
	if (s_trackingRoot == this) {
		s_trackingRoot = NULL;
		s_trackingHwnd = NULL;
	}
}

// 開いているサブを退出アニメ無しで破棄（ホバーで引きずられないように）。
// フォント面プレビューも Clear。
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

// ルートに結果を書いて m_done。RunModalLoop が抜けて DestroyPopupTree。
// 骨格コマンドも一旦 id を載せ、Track 出口で 0 にする。
void CCustomPopupMenu::CloseChain(UINT result)
{
	CCustomPopupMenu* root = RootMenu();
	if (!root) return;
	root->m_result = result;
	root->m_done = TRUE;
}

// チェーンの根。m_root が無ければ this。
// フォント／閉じる／モーダルフラグは根で共有する。
CCustomPopupMenu* CCustomPopupMenu::RootMenu()
{ return m_root ? m_root : this; }

// 自分・開サブ・コンボリスト・親↔サブ隙間がヒットなら TRUE。
// 飛行余白の穴（RGN 外）は中身／チップのみ。外側クリック閉じ判定に使う。
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
		// 親→サブの隙間（余白）もチェーン内扱い（クリックで全体が閉じない）
		if (si >= 0 && si < m_subCount && m_subs[si] && m_subs[si]->GetSafeHwnd()) {
			CRect pwr; GetWindowRect(&pwr);
			CRect swr; m_subs[si]->GetWindowRect(&swr);
			CRect item = ItemViewRect(m_openSub);
			CPoint a(item.left, item.top), b(item.right, item.bottom);
			const_cast<CCustomPopupMenu*>(this)->ClientToScreen(&a);
			const_cast<CCustomPopupMenu*>(this)->ClientToScreen(&b);
			CRect bridge;
			bridge.top = (std::min)((std::min)(a.y, swr.top), pwr.top) - 20;
			bridge.bottom = (std::max)((std::max)(b.y, swr.bottom), pwr.bottom) + 20;
			// 右開き／左開きどちらでも親↔サブの隙間をカバー
			if (swr.CenterPoint().x >= pwr.CenterPoint().x) {
				bridge.left = (std::min)(pwr.right, swr.left) - 16;
				bridge.right = (std::max)(pwr.right, swr.left) + 16;
			} else {
				bridge.left = (std::min)(swr.right, pwr.left) - 16;
				bridge.right = (std::max)(swr.right, pwr.left) + 16;
			}
			if (bridge.PtInRect(screenPt))
				return TRUE;
		}
	}
	return FALSE;
}

// 見た目のメニュー本体（飛行 pad 内のコンテンツ、または行ヒット）。
// 余白クリックを HTTRANSPARENT にする判定の土台。
BOOL CCustomPopupMenu::ScreenPtOnMenuBody(CPoint screenPt) const
{
	if (!GetSafeHwnd()) return FALSE;
	CRect wr;
	GetWindowRect(&wr);
	if (!wr.PtInRect(screenPt)) return FALSE;
	CPoint c = screenPt;
	const_cast<CCustomPopupMenu*>(this)->ScreenToClient(&c);
	if (HitTest(c) >= 0) return TRUE;
	CRect body(m_flightPad, m_flightPad, m_flightPad + m_menuW, m_flightPad + m_menuH);
	if (m_flightPad <= 0)
		GetClientRect(&body);
	return body.PtInRect(c) ? TRUE : FALSE;
}

// 開いている子孫サブの本体上か。親 OnMouseMove がサブ操作を奪わないため。
// 左折り返しで重なってもサブ優先。
BOOL CCustomPopupMenu::ScreenPtOnOpenSubBody(CPoint screenPt) const
{
	if (m_openSub < 0 || m_openSub >= m_itemCount) return FALSE;
	const int si = m_items[m_openSub].subIndex;
	if (si < 0 || si >= m_subCount || !m_subs[si]) return FALSE;
	if (m_subs[si]->ScreenPtOnMenuBody(screenPt)) return TRUE;
	return m_subs[si]->ScreenPtOnOpenSubBody(screenPt);
}

// 自分／子／owner トップレベル／コンボリスト／開サブなら関連。
// Track(子コントロール) 時 FG はトップレベルになる点に注意。
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

// オーナートップレベルに CCUSTOM_POPUP_RELAX_DISMISS_PROP があれば FG/KillFocus で閉じない
// （PrintWindow/WGC が他窓を前面化してもメニュー維持。外側クリック/Esc は有効）
static BOOL PopupOwnerRelaxesDismiss(CWnd* owner)
{
	if (!owner || !owner->GetSafeHwnd()) return FALSE;
	HWND root = ::GetAncestor(owner->GetSafeHwnd(), GA_ROOT);
	if (!root) root = owner->GetSafeHwnd();
	return (::GetProp(root, CCUSTOM_POPUP_RELAX_DISMISS_PROP) != NULL) ? TRUE : FALSE;
}

// 前面が自分チェーンか owner か。違えばモーダルが閉じる。
// CCUSTOM_POPUP_RELAX_DISMISS_PROP（キャプチャ等）なら FG 変動では閉じない。
// 外側クリック・Esc・WM_ACTIVATEAPP(FALSE) は別経路。
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

// 画面上の行矩形。sticky 以外は -m_scrollY。飛行中は +m_flightPad。
// HitTest / 描画 / 子配置の共通座標。
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

// 行チップ飛行開始。WS_EX_LAYERED を付け HWND を飛行余白つきに拡げる。
// 余白は kChipChromaKey。PresentChipLayered→UpdateLayeredWindow(α抜き)。
// 表示中の一枚→行だけ切替時は先に拡大 ULW を出して追従（点滅防止）。
// 定着は CommitChipFlightSettle（m_bridgePanel で一枚パネルを強制）。
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

// 飛行余白を畳んで通常サイズへ。optDst 済みなら HWND は既に最終位置。
// 飛行サイズのときだけ +pad（二重加算すると一枚状態が右へずれる）。
// RGN は解除。レイヤ自体の外しは呼び出し側。
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

// 32bit DIB にコピーし、マゼンタキーを α=0、他を α=255 にして ULW。
// optDst!=NULL ならその画面座標に最終サイズで出す（畳む前の先送り）。
// 通常飛行は窓サイズに合わせて余白を透明埋め。
BOOL CCustomPopupMenu::PresentChipLayered(HDC hdcSrc, int w, int h, const POINT* optDst, BYTE opacity)
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
	bf.SourceConstantAlpha = opacity ? opacity : (BYTE)255;
	bf.AlphaFormat = AC_SRC_ALPHA;
	const BOOL ok = ::UpdateLayeredWindow(m_hWnd, NULL, &ptDst, &size, hdcDib, &ptSrc, 0, &bf, ULW_ALPHA);

	::SelectObject(hdcDib, old);
	::DeleteObject(hbmp);
	::DeleteDC(hdcDib);
	return ok;
}

// ULW 解除後の同期焼き込み。BufferedPaint + MakeOpaque（Win11 の α=0 対策）。
// 失敗時は素の BitBlt。
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

// UpdateLayeredWindow 使用中は Invalidate だけではコマが進まない。
// PaintToDC→PresentChipLayered をタイマから直接呼ぶ。
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

// 飛行→定着。phase=0 で焼き、m_bridgePanel で一枚パネルを強制（点滅防止）。
// easeOutBack の行き過ぎを残したまま ULW 解除すると操作不能に見える。
// 先に畳んでレイヤを外し、不透明 GDI へ同期焼き込みしてから子を出す。
// 全窓 RedrawWindow は使わない。
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

// 背景アニメ用。可視子 HWND をリージョン除外して Invalidate。
// InvalidateRect 全塗りは BufferedPaint が子を潰す。
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

// 本文スクロール（sticky より下のみ）。先に SyncEmbeddedChildren。
// 親は子領域を除外して塗る。範囲は 0..m_scrollMax。
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

// ホイール一段を SCROLL_STEP（DPI）×ノッチで SetScrollY。
// scrollMax==0 なら FALSE（チェーンの次へ）。
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

// カーソル下の自分 or 開サブへホイールを渡す。モーダル Peek から呼ぶ。
// ヒットしなければ FALSE。
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

// レ点バウンス（CCustomCheckBox と同じ 8→0、28ms）。
// CloseChain 直前でも起動しておく（1 フレームでも見える）。
void CCustomPopupMenu::StartCheckBounce(int idx)
{
	if (idx < 0 || idx >= m_itemCount) return;
	m_bounceIdx = idx;
	m_nBounce = 8;
	if (GetSafeHwnd())
		SetTimer(kBounceTimer, 28, NULL);
	InvalidateBgOnly();
}

// 出現アニメ中断→一枚状態。サブ遷移・クリック・所要超過で使う。
// 行チップ中は CommitChipFlightSettle。マウス未移動でも HitTest で hot 同期。
// その後 idle 背景用に kAnimTimer 33ms。
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
		if (!ScreenPtOnOpenSubBody(sp)) {
			ScreenToClient(&sp);
			const int idx = HitTest(sp);
			if (idx != m_hot)
				SetHot(idx);
		}
	}
	// idle 背景アニメ用（ストライプ周期 ~66ms に合わせてやや速め）
	SetTimer(kAnimTimer, 33, NULL);
}

// 指定行のサブを開く。出現中なら先に SnapAnimToIdle（飛行余白だと座標が狂う）。
// 右に余地がなければ左へ。親と overlap して隙間クリックで全体が閉じないようにする。
// z は子 CreatePopupAt が TOPMOST。
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
	sub->RebuildMenuFont();
	sub->MeasureLayout();
	const int overlap = 16;
	const int y = wr.top + vr.top;
	int xRight = wr.right - overlap;
	int xLeft = wr.left - sub->m_menuW + overlap;
	BOOL placeLeft = FALSE;
	MONITORINFO mi; ZeroMemory(&mi, sizeof(mi)); mi.cbSize = sizeof(mi);
	HMONITOR hMon = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
	if (hMon && ::GetMonitorInfo(hMon, &mi)) {
		const BOOL fitsRight = (xRight + sub->m_menuW <= mi.rcWork.right);
		const BOOL fitsLeft = (xLeft >= mi.rcWork.left);
		if (!fitsRight && fitsLeft)
			placeLeft = TRUE;
		else if (!fitsRight && !fitsLeft)
			placeLeft = (wr.left - mi.rcWork.left) >= (mi.rcWork.right - wr.right);
	}
	// 右に余地がなければ左へ。余白／影を跨いで消えないよう親と重ねる
	sub->CreatePopupAt(CPoint(placeLeft ? xLeft : xRight, y), this, RootMenu());
}

// ホバー行。余白(idx<0)でも開サブへ渡る途中なら閉じない（橋矩形）。
// FONT_FACE は ApplyPreviewFace。サブ行なら OpenSubAt、それ以外は CloseOpenSub。
// Invalidate は背景のみ（BufferedPaint が子を潰す）。
void CCustomPopupMenu::SetHot(int idx)
{
	if (idx == m_hot) return;

	// 項目間・左右パッド等の余白(idx<0): 開いているサブへ渡る途中なら閉じない
	if (idx < 0) {
		if (m_openSub >= 0) {
			CPoint sp;
			::GetCursorPos(&sp);
			CRect wr;
			GetWindowRect(&wr);
			if (wr.PtInRect(sp)) {
				if (m_hot != m_openSub) {
					m_hot = m_openSub;
					InvalidateBgOnly();
					UpdateTip();
				}
				return;
			}
			const int si = m_items[m_openSub].subIndex;
			if (si >= 0 && si < m_subCount && m_subs[si] && m_subs[si]->IsPointInChain(sp))
				return;
			if (si >= 0 && si < m_subCount && m_subs[si] && m_subs[si]->GetSafeHwnd()) {
				CRect swr; m_subs[si]->GetWindowRect(&swr);
				CRect item = ItemViewRect(m_openSub);
				CPoint a(item.left, item.top), b(item.right, item.bottom);
				ClientToScreen(&a);
				ClientToScreen(&b);
				CRect bridge;
				bridge.top = (std::min)((std::min)(a.y, swr.top), wr.top) - 20;
				bridge.bottom = (std::max)((std::max)(b.y, swr.bottom), wr.bottom) + 20;
				if (swr.CenterPoint().x >= wr.CenterPoint().x) {
					bridge.left = (std::min)(wr.right, swr.left) - 16;
					bridge.right = (std::max)(wr.right, swr.left) + 16;
				} else {
					bridge.left = (std::min)(swr.right, wr.left) - 16;
					bridge.right = (std::max)(swr.right, wr.left) + 16;
				}
				if (bridge.PtInRect(sp))
					return;
			}
			m_hot = -1;
			InvalidateBgOnly();
			UpdateTip();
			CloseOpenSub();
			return;
		}
		m_hot = -1;
		InvalidateBgOnly();
		UpdateTip();
		return;
	}

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
	}
}

// ツールチップを現在 hot に合わせる。TTN_NEEDTEXT は OnTtnNeedText。
// m_tipHot を更新して TTM_UPDATE。
void CCustomPopupMenu::UpdateTip()
{
	m_tipHot = m_hot;
	if (m_tip.GetSafeHwnd()) m_tip.SendMessage(TTM_UPDATE, 0, 0);
}

// ホバー／クリックは常に定着レイアウト座標。飛行中の見た目矩形は重なる。
// sticky 帯（+flightPad）より上は先頭固定行のみ。セパレータはヒットしない。
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

// メニュー全面を dc へ。phase!=0 の行チップはキー塗り + チップ or 橋渡し一枚。
// m_bridgePanel / ChipFlightRowsAtRest のときは行枠のまま放置せずパネル化（点滅防止）。
// idle は sticky を先に描き、本文をクリップ。右端にスクロールサム。
// 飛行／退場中の内包コントロールは DrawInteractiveProxy。
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
				// 橋渡しパネルは Soft なし（飛行アクセント側で Soft を出す）
				DrawJkBackdrop(dc, content, m_animTick, -1.f, FALSE);
				DrawTornRibbon(dc, content, m_animTick);
				for (int i = 0; i < m_itemCount; ++i)
					paintItem(i);
				DrawPanelChrome(dc, content);
				dc.RestoreDC(clipSave);
			} else {
				for (int i = 0; i < m_itemCount; ++i) {
					int ox = 0, oy = 0, fade = 0;
					float flightT = 1.f;
					if (!CalcLineAnim(i, &ox, &oy, &fade, &flightT) || fade < 8)
						continue;
					CRect vr = ItemViewRect(i);
					CRect chip(m_flightPad, vr.top, m_flightPad + m_menuW, vr.bottom);
					chip.OffsetRect(ox, oy);
					DrawRowChip(dc, chip, m_animTick, fade, animStyle, flightT, i);
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
			float doorT = 1.f;
			if (animStyle == POPUP_ANIM_EXPAND && !m_asSubmenu) {
				int ox0 = 0, oy0 = 0, fade0 = 0;
				CalcLineAnim(m_lineAnimOrigin, &ox0, &oy0, &fade0, &doorT);
			}
			DrawJkBackdrop(dc, rc, m_animTick, (animStyle == POPUP_ANIM_EXPAND) ? doorT : -1.f);
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

// 子 HWND をクリップ除外してメモリBMPへ PaintToDC。
// チップ飛行中は ULW へ Present して return（GDI 面を汚さない）。
// 通常は BufferedPaint+Opaque。MakeOpaque が子を潰したら Refresh。
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

// 消去抑制。背景は OnPaint / ULW 側。チラつき防止。
// TRUE を返してデフォルト塗りを止める。
BOOL CCustomPopupMenu::OnEraseBkgnd(CDC*) { return TRUE; }

// 定着後 Post の遅延 Refresh。phase!=0 では何もしない。
// Sync 連打は点滅するので潰された子の再描画だけ。
LRESULT CCustomPopupMenu::OnRefreshEmbedded(WPARAM, LPARAM)
{
	if (!GetSafeHwnd() || m_lineAnimPhase != 0)
		return 0;
	// Sync は MoveWindow/Invalidate 連打で点滅するので、潰された子の再描画だけ
	RefreshEmbeddedChildren();
	return 0;
}

// PrintWindow / キャプチャ向け。チップ中はキー塗り+PaintToDC。
// 通常は BufferedPaint+Opaque。
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

// 飛行余白が親を覆うときだけ HTTRANSPARENT。左折り返しで本体が親に重なるときは取る。
// パッドの穴（チップも定着行も無い）も下へ通す。
LRESULT CCustomPopupMenu::OnNcHitTest(CPoint point)
{
	// 飛行余白が親を覆うときだけ透過。左折り返しで本体が親に重なるときは
	// こちらがヒットを取る（透過すると選択不能／親がサブを閉じる）。
	if (!ScreenPtOnMenuBody(point)) {
		for (CCustomPopupMenu* p = m_parentMenu; p; p = p->m_parentMenu) {
			if (!p->GetSafeHwnd()) continue;
			CRect wr; p->GetWindowRect(&wr);
			if (wr.PtInRect(point))
				return HTTRANSPARENT;
		}
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

// 入場所要を超えた／静止したら Snap（ホバー不能の防止）。
// カーソルが開サブ本体上なら親 hot を触らない。
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
	CPoint sp;
	::GetCursorPos(&sp);
	if (ScreenPtOnOpenSubBody(sp))
		return;
	SetHot(HitTest(point));
	CWnd::OnMouseMove(nFlags, point);
}

// サブ行以外の hot をクリア。FONT_FACE ならプレビュー解除。
// サブ行は Leave しても開いたまま（橋を渡るため）。
void CCustomPopupMenu::OnMouseLeave()
{
	if (m_hot >= 0 && m_items[m_hot].kind != CCUSTOM_POPUP_SUB) {
		const BOOL wasFace = (m_items[m_hot].id == CCUSTOM_POPUP_ID_FONT_FACE);
		m_hot = -1;
		InvalidateBgOnly();
		if (wasFace) ClearPreviewFace();
	}
}

// 骨格項目のクリック処理。TRUE なら通常コマンドへ落とさない。
// アクリルは aero 切替+通知して閉じる。Soft強めは保存して描き直し。
// KPI DL/再読込は WM_APP_KPI_PLUGIN をメインへ Post して閉じる。
// MID_KPI / MID_VST は savedata.midPlayPrefer を書き、レ点を排他更新。
// 続けて PlRefreshMidiPlayModes() でプレイリストの MID(VST)/MID(KPI) を即更新。
// PlayList.h は IDD_PLAYLIST 欠落のためこの cpp から include しない（extern）。
// フォント太字/斜体は即 Relayout。面確定と描画方法は保存して閉じる。
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
	if (it.id == CCUSTOM_POPUP_ID_SOFTBOOST) {
		savedata.popupMenuSoftBoost = savedata.popupMenuSoftBoost ? 0 : 1;
		it.checked = savedata.popupMenuSoftBoost ? TRUE : FALSE;
		if (it.checked) StartCheckBounce(idx);
		MpPersistSavedataQuick();
		InvalidateBgOnly();
		return TRUE;
	}
	if (it.id == CCUSTOM_POPUP_ID_KPI_DL || it.id == CCUSTOM_POPUP_ID_KPI_RELOAD) {
		const WPARAM wp = (it.id == CCUSTOM_POPUP_ID_KPI_DL) ? 1 : 2;
		CloseChain(0);
		CWnd* main = AfxGetMainWnd();
		if (main && ::IsWindow(main->GetSafeHwnd()))
			main->PostMessage(WM_APP_KPI_PLUGIN, wp, 0);
		return TRUE;
	}
	// MIDI再生 KPI/VST 優先。savedata.midPlayPrefer を書き、レ点を排他。
	// PlayList.h は IDD_PLAYLIST 欠落のため include せず、extern PlRefreshMidiPlayModes()
	// でプレイリストの MID(VST)/MID(KPI) 表示を即更新する。
	if (it.id == CCUSTOM_POPUP_ID_MID_KPI || it.id == CCUSTOM_POPUP_ID_MID_VST) {
		savedata.midPlayPrefer = (it.id == CCUSTOM_POPUP_ID_MID_VST) ? 1 : 0;
		for (int i = 0; i < m_itemCount; ++i) {
			CCustomPopupItem& x = m_items[i];
			if (x.id == CCUSTOM_POPUP_ID_MID_KPI) x.checked = (savedata.midPlayPrefer == 0) ? TRUE : FALSE;
			if (x.id == CCUSTOM_POPUP_ID_MID_VST) x.checked = (savedata.midPlayPrefer == 1) ? TRUE : FALSE;
		}
		StartCheckBounce(idx);
		MpPersistSavedataQuick();
		{
			// ファイル先頭の extern と同じ。PlayList.h は IDD_PLAYLIST 欠落のため include しない
			extern void PlRefreshMidiPlayModes();
			PlRefreshMidiPlayModes();
		}
		InvalidateBgOnly();
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

// アニメ中も定着 HitTest で行決定。Snap はサブを開くときだけ（誤爆一枚化防止）。
// 骨格は HandleChromeClick。CMD/CHECK は CloseChain(id)。
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

// 既定処理へ委譲。行決定は Down 側。
// キャプチャ残留はモーダル終了時 PopupEatDismissClickTail。
void CCustomPopupMenu::OnLButtonUp(UINT nFlags, CPoint point)
{ CWnd::OnLButtonUp(nFlags, point); }

// Esc で閉じる（子 Edit フォーカス中も）。上下で enabled 行へ。
// Right でサブ、Left で親へ戻る。Enter は骨格 or CMD/CHECK/SUB。
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

// フォーカスがチェーン外なら CloseChain(0)。
// RELAX_DISMISS（画面キャプチャ等の偽 KillFocus）では閉じない。
// 外側クリックと Esc は RunModalLoop 側。
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

// この窓上のホイール。チェーン跨ぎは RunModalLoop の HandleWheelInChain。
// クライアント座標へ直して OnWheelDelta。
BOOL CCustomPopupMenu::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	ScreenToClient(&pt);
	if (OnWheelDelta(zDelta))
		return TRUE;
	return CWnd::OnMouseWheel(nFlags, zDelta, pt);
}

// 内包スライダー / Range のスクロール通知。item を同期して cb。
// Range は NotifyRangeFromHwnd（nSBCode / dragTarget 付き）。
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

// 該当 Range の pos/選択/A-B を item へ写し rangeCb。
// ラベルの % 表示更新のため背景 Invalidate。
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

// 内包ボタン BN_CLICKED。cb の後、closeOnClick なら CloseChain(id)。
// id==0 でも閉じる設定なら Track は 0。
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

// EN_CHANGE / KILLFOCUS。Create 初期値は m_suppressEditNotify で無視。
// editCb へ現在テキスト。
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

// Combo または List の選択変更。choiceSel を更新して choiceCb。
// Combo は物理 index（フィルタ無し前提）。
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

// 子からの EN_/CBN_/LBN_/BN_ を Notify* へ振り分け。
// 親 CWnd::OnCommand も呼ぶ。
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

// ツールチップ文字列。m_tipHot の hasTip を LPSTR_TEXTCALLBACK へ。
// 無ければ FALSE。
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

// z-order 無視で親行ホバーを同期（開サブの即閉じ用）。タイマから呼ぶ。
// 子孫本体が親に重なっているときは親判定しない。
void CCustomPopupMenu::SyncHotFromCursor()
{
	// サブHWNDが親を覆っていても、カーソル下の親行でホバーを同期（非サブ→即 CloseOpenSub）
	if (!GetSafeHwnd() || m_openSub < 0) return;
	CPoint sp;
	::GetCursorPos(&sp);
	// 左折り返しで子孫本体が親に重なっているときは親行判定しない
	if (ScreenPtOnOpenSubBody(sp))
		return;
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

// 淫女／行アニメ／定着／レ点バウンス。
// EXPAND は定位置行の外接で RGN。チップ入場は ForceChipPresent。
// 所要超過 or atRest で SnapAnimToIdle。idle は背景のみ再描画。
// phase: 0 idle / 1 enter / 2 exit（退場は現状短いフェード優先）。
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
		// idle: 斜めストライプ／Softプレート／リボンは時間ベース。再描画しないと止まる。
		// InvalidateBgOnly は子 HWND を除外するので点滅しにくい。
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

// Track の心。m_tracking 中 Peek+Dispatch。WM_QUIT は再投稿。
// 入場中は 16ms MsgWait（長い timeout だと WM_TIMER が合成されずチップが止まる）。
// 定着後は 33ms で IsForegroundOurs（ACTIVATEAPP はキューに乗らない）。
// 外側クリックで閉じる。右は reopen 武装、左は閉じるだけ。DOWN の対 UP も食う。
// RELAX_DISMISS でもアプリ非アクティブと外側クリックは有効。
// 他トップレベルへ移ったあとは owner を SetForeground しない。
void CCustomPopupMenu::RunModalLoop()
{
	m_tracking = TRUE; m_done = FALSE; m_result = 0;
	HWND hCap = (m_owner && m_owner->GetSafeHwnd()) ? m_owner->GetSafeHwnd() : NULL;
	MSG msg;
	// 他アプリ／別トップレベルへフォーカスが移って閉じたときは
	// 終了時に SetForegroundWindow(owner) しない（すぐメディアプレイヤーが前面に戻るのを防ぐ）
	BOOL lostForeignFocus = FALSE;

	auto dismissForForeignFocus = [&]() {
		m_done = TRUE;
		m_result = 0;
		lostForeignFocus = TRUE;
		// アプリ外／別トップレベルへのフォーカス移動では開き直ししない
		s_reopenRClick = FALSE;
	};

	auto dispatchOne = [&](MSG& m) -> BOOL {
		if (m.message == WM_QUIT) {
			m_done = TRUE; m_result = 0;
			::PostQuitMessage((int)m.wParam);
			return FALSE;
		}
		if (m.message == WM_ACTIVATEAPP && m.wParam == FALSE) {
			dismissForForeignFocus();
			return TRUE;
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
			if (!IsPointInChain(sp)) {
				m_done = TRUE;
				m_result = 0;
				// 右クリック外は「閉じて同じクリックで開き直す」。左は閉じるだけ。
				if (m.message == WM_RBUTTONDOWN || m.message == WM_NCRBUTTONDOWN)
					PopupArmReopenAt(sp, m.message == WM_NCRBUTTONDOWN);
				else
					s_reopenRClick = FALSE;
				PopupEatOpenMenuMessages();
				// DOWN だけ食って UP をメインへ流すと押下状態だけ残る
				PopupEatDismissClickTail();
				return TRUE;
			}
		}
		// ホスト WM_PAINT は Dispatch する（validate のみだと g_gdiPaintPending /
		// Soft3D以外の描画が止まりやすい）。重さは Soft3D/Speana 側の
		// GetTrackingRoot ガードで抑える。
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
			if (!m_done && !IsForegroundOurs())
				dismissForForeignFocus();
			continue;
		}

		// 定着後: GetMessage 無限待ちだと他アプリ切替を検知できない。
		// WM_ACTIVATEAPP は SendMessage 経由でキューに乗らないため、短周期で
		// IsForegroundOurs を見る。スペアナ／EQ コード用 WM_TIMER・PostMessage も
		// ここで起こすため、待ちは ~33ms（旧 100ms だと描画・コードが間延びする）。
		const DWORD wake = ::MsgWaitForMultipleObjects(0, NULL, FALSE, 33, QS_ALLINPUT);
		if (wake == WAIT_TIMEOUT) {
			if (!IsForegroundOurs())
				dismissForForeignFocus();
			continue;
		}
		while (!m_done && ::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				m_done = TRUE; m_result = 0;
				::PostQuitMessage((int)msg.wParam);
				break;
			}
			if (!dispatchOne(msg))
				break;
		}
		if (!m_done && !IsForegroundOurs())
			dismissForForeignFocus();
	}
	m_tracking = FALSE;
	// 破棄前に残クリック／キャプチャを掃除（AnimateOut 中の入力は別途捨てる）
	PopupEatDismissClickTail();
	DestroyPopupTree();
	PopupEatOpenMenuMessages();
	PopupEatDismissClickTail();
	if (s_trackingRoot == this) {
		s_trackingRoot = NULL;
		s_trackingHwnd = NULL;
	}
	// 他UI開き直し／他アプリへ移ったあとは owner を前面に戻さない
	if (hCap && ::IsWindow(hCap) && !s_reopenRClick && !lostForeignFocus)
		::SetForegroundWindow(hCap);
}

// 表示エントリ。ネスト禁止（GetTrackingRoot があれば 0）。
// EnsureChromePrefix → s_trackingRoot を先に立ててから CreatePopupAt（Soft3D 抑制）。
// HWND は TOPMOST + NOACTIVATE。RunModalLoop 後にタイマキックと reopen 投函。
// 骨格コマンドは 0 を返す。選択コマンド ID が戻り値。
UINT CCustomPopupMenu::Track(CPoint screenPt, CWnd* pOwner)
{
	// AnimateOut 中などにネストして呼ばれると二重メニューになる
	if (GetTrackingRoot() != NULL)
		return 0;
	m_owner = pOwner;
	m_root = this;
	m_parentMenu = NULL;
	EnsureChromePrefix();
	// CreatePopupAt／AnimateIn 中も Soft3D 等が GetTrackingRoot で抑制できるよう先に立てる
	s_trackingRoot = this;
	s_trackingHwnd = NULL;
	if (!CreatePopupAt(screenPt, NULL, this)) {
		if (s_trackingRoot == this) {
			s_trackingRoot = NULL;
			s_trackingHwnd = NULL;
		}
		return 0;
	}
	s_trackingHwnd = m_hWnd;
	RunModalLoop();
	if (s_trackingRoot == this) {
		s_trackingRoot = NULL;
		s_trackingHwnd = NULL;
	}
	// 退場中に Posted tick を落としても再生 UI が死なないようキック
	extern void COgg_KickTimerp();
	COgg_KickTimerp();
	PopupEatOpenMenuMessages();
	// 外側右クリックで閉じた場合、同じ操作で新しいメニューを開く
	PopupFlushReopenContextClick();
	// 骨格コマンドは呼び出し元へ返さない
	if (IsChromeCommand(m_result))
		return 0;
	return m_result;
}

// id 完全一致の先頭 index。id==0 は -1（骨格セパレータ等と衝突しない）。
// 内部用。公開 FindItemById はこれを呼ぶ。
int CCustomPopupMenu::FindItemIndexById(UINT id) const
{
	if (id == 0) return -1;
	for (int i = 0; i < m_itemCount; ++i)
		if (m_items[i].id == id) return i;
	return -1;
}

// 公開照会。id の項目 index。無ければ -1。
// Track 中も可。id==0 は探さない。
int CCustomPopupMenu::FindItemById(UINT id) const
{
	return FindItemIndexById(id);
}

// CCustomPopupItemKind。範囲外は -1。
// セパレータ／サブ／内包の判定用。
int CCustomPopupMenu::GetItemKind(int idx) const
{
	if (idx < 0 || idx >= m_itemCount) return -1;
	return m_items[idx].kind;
}

// 項目のコマンド ID。範囲外は 0。
// 骨格 ID もそのまま返す（Track 出口とは別）。
UINT CCustomPopupMenu::GetItemId(int idx) const
{
	if (idx < 0 || idx >= m_itemCount) return 0;
	return m_items[idx].id;
}

// スライダー位置。HWND があれば実値、なければ item.sliderPos。
// 種類不一致は FALSE。Track 中も可。
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

// スライダー位置を書き、HWND があれば SetPos。範囲クランプ。
// ラベル再描画のため背景 Invalidate。
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

// Edit 現在文字列。HWND 優先、なければ choiceSet[0]。
// buf 必須。
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

// Combo/List の選択 index。HWND があれば実選択。
// 該当なしは FALSE。
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

// Range のヘッド／ループ／A-B。HWND があれば実値。
// 各 out は NULL 可。
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

// Track 中のルート。GetTrackingRoot が dangling this を返さないよう HWND も保持
CCustomPopupMenu* CCustomPopupMenu::s_trackingRoot = NULL;
HWND CCustomPopupMenu::s_trackingHwnd = NULL;

// 現在 Track 中のルート。無ければ NULL。
// HWND 破棄済みなら dangling this を返さず NULL（s_trackingHwnd で検知）。
// Soft3D / Speana の重さガードがこれを見る。
CCustomPopupMenu* CCustomPopupMenu::GetTrackingRoot()
{
	if (s_trackingRoot == NULL)
		return NULL;
	// HWND が死んでいればポインタだけ残った固着。this は触らない。
	if (s_trackingHwnd != NULL && !::IsWindow(s_trackingHwnd)) {
		s_trackingRoot = NULL;
		s_trackingHwnd = NULL;
		return NULL;
	}
	return s_trackingRoot;
}

// Track 中の再生追従。ドラッグ中は無視。id==0 なら先頭の Range。
// 範囲も更新してラベルを描き直す。
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

// プログレス位置。HWND があれば実値。
// 種類不一致は FALSE。
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

// プログレス位置を書き、HWND があれば SetPos。
// ラベル同期のため背景 Invalidate。
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

// 内包スライダー HWND ラッパ。未 Create / 不一致は NULL。
// Track 中の直接操作・スタイル変更用。
CCustomSliderCtrl* CCustomPopupMenu::GetSliderCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_SLIDER || it.sliderIndex < 0) return NULL;
	return &m_sliders[it.sliderIndex];
}

// 内包 Edit。未 Create / 不一致は NULL。
// 初期値セットは EN_CHANGE に注意。
CCustomEdit* CCustomPopupMenu::GetEditCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_EDIT || it.editIndex < 0) return NULL;
	return &m_edits[it.editIndex];
}

// 内包 Combo。未 Create / 不一致は NULL。
// ドロップダウンリスト HWND は IsPointInChain も見ている。
CCustomComboBox* CCustomPopupMenu::GetComboCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_COMBO || it.comboIndex < 0) return NULL;
	return &m_combos[it.comboIndex];
}

// 内包 ListBox。未 Create / 不一致は NULL。
// スクロールで画面外なら Sync が Hide。
CCustomListBox* CCustomPopupMenu::GetListCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_LIST || it.listIndex < 0) return NULL;
	return &m_lists[it.listIndex];
}

// 内包 Range。未 Create / 不一致は NULL。
// LiveMirrorRange より細かい操作が必要なとき。
CCustomRangeSliderCtrl* CCustomPopupMenu::GetRangeSliderCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_RANGE || it.rangeIndex < 0) return NULL;
	return &m_ranges[it.rangeIndex];
}

// 内包 Progress。未 Create / 不一致は NULL。
// SetProgressPos で足りる場合はそちら。
CCustomProgressCtrl* CCustomPopupMenu::GetProgressCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_PROGRESS || it.progressIndex < 0) return NULL;
	return &m_progresses[it.progressIndex];
}

// 内包 Button。未 Create / 不一致は NULL。
// キャプション変更など。閉じる動作は buttonCloseOnClick。
CCustomStandardButton* CCustomPopupMenu::GetButtonCtrl(UINT id)
{
	const int idx = FindItemIndexById(id);
	if (idx < 0) return NULL;
	const CCustomPopupItem& it = m_items[idx];
	if (it.kind != CCUSTOM_POPUP_BUTTON || it.buttonIndex < 0) return NULL;
	return &m_buttons[it.buttonIndex];
}
