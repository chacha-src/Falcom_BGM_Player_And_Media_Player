#include "stdafx.h"
#include "CCustomControl.h"
#include "resource.h"
#include "CImageBase.h"
#include "GdiSoft2D.h"
#include "GdiSoft3D.h"
#include "OfflineHelp.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <psapi.h>
#include <TlHelp32.h>
#include <imm.h>
#include <wincodec.h>

#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "windowscodecs.lib")

// ============================================================================
// CCustom アーキテクチャ（このファイル: 共通ヘルパ + CCustom* サブクラス）
//
// 親ダイアログ = DWM アクリル / Mica ガラス。ExtendFrame(-1) でクライアント全体が
// ガラス源になる。子は原則不透明に塗る（BufferedPaint / CCustomOpaqueFixer）。
// 例外: ラベル・スライダー等はクロマキーでガラスを透かす。
//
// CCC_AERO_CHROMA_KEY は RGB(1,1,1)。黒 RGB(0,0,0) は本文文字に使うためキーにできない。
// SetAeroMode(TRUE)  = ガラスを透かす（クロマ blit）
// SetAeroMode(FALSE) = ソリッド（不透明面）。ポップアップ配下は常に FALSE。
//
// CCC_* はここに集約した描画ヘルパ。CCustom* は標準 Win32/MFC 控件のサブクラス。
// ============================================================================

// Per-Monitor DPI。Win10+ は GetDpiForWindow、失敗時は LOGPIXELSX。未取得は 96。
static UINT CCC_GetControlDpi(HWND hWnd)
{
    if (!hWnd) return 96; // 96 = 100% DPI の規約値（MulDiv の分母）
    typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
    static PFN_GetDpiForWindow s_fn = nullptr;
    static BOOL s_got = FALSE;
    if (!s_got) {
        HMODULE hUser = ::GetModuleHandleW(L"user32.dll");
        if (hUser)
            s_fn = (PFN_GetDpiForWindow)::GetProcAddress(hUser, "GetDpiForWindow");
        s_got = TRUE;
    }
    if (s_fn) {
        const UINT dpi = s_fn(hWnd);
        if (dpi > 0) return dpi;
    }
    HDC hdc = ::GetDC(hWnd);
    if (!hdc) return 96;
    const UINT dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
    ::ReleaseDC(hWnd, hdc);
    return (dpi > 0) ? dpi : 96;
}

// 96dpi 基準の px を対象 DPI へ。ダイアログ単位ではなく「デザイン時ピクセル」。
static int CCC_ScaleDpi(int value, UINT dpi)
{
    return MulDiv(value, (int)dpi, 96);
}

static BOOL CCC_CaptionIsGlyphOnly(const CString& s);
static void CCC_CaptionApplySharedIcon(CCustomStandardButton* p, UINT iconId);

// ドロップシャドウが矩形外へはみ出す量。フィット縮小の予算から引く。
// nSD=角度(度) nDist=距離 nBlur=ぼかし。bSE=FALSE なら pad=0。
static void CCC_ComputeShadowPad(int nSD, int nDist, int nBlur, BOOL bSE, UINT dpi,
    int& padX, int& padY)
{
    padX = padY = 0;
    if (!bSE || nDist <= 0 || nBlur <= 0) return;
    const int dist = CCC_ScaleDpi(nDist, dpi);
    const int blur = CCC_ScaleDpi(nBlur, dpi);
    const double rad = nSD * 3.14159265358979323846 / 180.0; // 度→ラジアン
    padX = (int)floor(dist * cos(rad) + (blur + 1) / 2 + 0.5);
    padY = (int)floor(dist * sin(rad) + (blur + 1) / 2 + 0.5);
    if (padX < 0) padX = 0;
    if (padY < 0) padY = 0;
}

#ifdef SubclassWindow
#undef SubclassWindow // MFC マクロと CWnd::SubclassWindow が衝突するため
#endif

#if CCUSTOM_AERO_SUPPORT
// 祖先にアクリル有効な CCustomDialog / CCustomDialogEx があるか。
// 子の透過判定（CCC_UseTransPaint）と OpaqueFixer 要否の入口。
BOOL CCC_IsBlurDialogChild(HWND hWnd)
{
    for (HWND h = hWnd; h; h = ::GetParent(h))
    {
        CWnd* pw = CWnd::FromHandlePermanent(h);
        if (!pw) continue;
        if (auto* p = dynamic_cast<CCustomDialog*>(pw)) return p->IsAeroEnabled();
        if (auto* p = dynamic_cast<CCustomDialogEx*>(pw)) return p->IsAeroEnabled();
    }
    return FALSE;
}

// CCustomPopupMenu 配下（不透明ストライプ）。親ダイアログがアクリルでも子は透過描画しない。
static BOOL CCC_IsCustomPopupChild(HWND hWnd)
{
    for (HWND h = hWnd ? ::GetParent(hWnd) : NULL; h; h = ::GetParent(h))
    {
        TCHAR cls[64];
        if (::GetClassName(h, cls, _countof(cls)) <= 0) continue;
        if (_tcscmp(cls, _T("CCustomPopupMenuClass")) == 0
            || _tcscmp(cls, _T("CCustomPopupMenuChipClass")) == 0)
            return TRUE;
    }
    return FALSE;
}

static BOOL CCC_IsCaptionChromeCtrl(HWND hWnd);
static BOOL CCC_CaptionOnlyHostGlass(HWND hWnd);
UINT CCC_CtlIconForCtrl(UINT id);
static BOOL CCC_CaptionIsGlyphOnly(const CString& s);
static void CCC_CaptionApplySharedIcon(CCustomStandardButton* p, UINT iconId);

// キャプション常時アクリル(本文 aero=0)でも子は α=255 必須
static BOOL CCC_HostNeedsChildOpaque(HWND hWnd)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsCustomPopupChild(hWnd)) return TRUE;
    return CCC_IsWin11() && (CCC_IsAeroEnabled() || CCC_CaptionOnlyHostGlass(hWnd));
#else
    return CCC_IsCustomPopupChild(hWnd);
#endif
}

// キャプション帯コントロールは AcrylicCaption 時は常に透過（本文 aero と独立）
static BOOL CCC_UseTransPaint(HWND hWnd, BOOL bAeroMode)
{
    if (CCC_IsCustomPopupChild(hWnd)) return FALSE;
    if (hWnd) {
        HWND hParent = ::GetParent(hWnd);
        if (hParent && CCC_AcrylicCaption(hParent) && CCC_IsCaptionChromeCtrl(hWnd))
            return TRUE;
    }
    if (CCC_IsBlurDialogChild(hWnd) && CCC_IsAeroEnabled()) return TRUE;
    return bAeroMode && !CCC_IsBlurDialogChild(hWnd);
}

// クロマキー子の見た目は親のガラス面。子 Invalidate だけでは残像が残るので
// 親の該当矩形も消す。不透明子では何もしない（親消去は兄弟を抉る）。
void CCC_InvalidateParent(HWND hWnd, BOOL bAeroMode)
{
    if (!CCC_UseTransPaint(hWnd, bAeroMode)) return;
    HWND hParent = ::GetParent(hWnd);
    if (!hParent || !::IsWindow(hParent)) return;
    RECT rc = {};
    ::GetWindowRect(hWnd, &rc);
    ::MapWindowPoints(NULL, hParent, (LPPOINT)&rc, 2);
    ::InflateRect(&rc, 6, 6); // 影・角デコのはみ出し
    ::InvalidateRect(hParent, &rc, FALSE);
}

// キャプション帯アクリルは Win11 では全面 Extend(-1) が必要（上辺だけだと黒帯になる）。
// 本文との切り分けはマージンではなく描画側（MakeOpaquePreserve / OpaqueFixer / ClearRect）。
static MARGINS CCC_CaptionHostMargins(HWND hWnd)
{
    UNREFERENCED_PARAMETER(hWnd);
    MARGINS margins = { -1, -1, -1, -1 };
    return margins;
}

// Win11 のアクリル/Mica を再適用。savedata.aero 切替やキャプション常時ガラス時。
// SYSTEMBACKDROP_TYPE=3 + REDIRECTIONBITMAP_ALPHA + ExtendFrame(-1) が揃わないと黒帯。
void CCC_RefreshDwmBlur(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd) || !CCC_IsWin11()) return;
    if (!CCC_IsAeroEnabled() && !CCC_AcrylicCaption(hWnd)) return;
    BOOL compositionEnabled = FALSE;
    if (!::DwmIsCompositionEnabled(&compositionEnabled) || !compositionEnabled) return;
    const int backdropType = 3; // DWMSBT_TRANSIENTWINDOW（アクリル系。2=Mica）
    ::DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
#ifndef DWMWA_REDIRECTIONBITMAP_ALPHA
#define DWMWA_REDIRECTIONBITMAP_ALPHA 39
#endif
    BOOL useAlpha = TRUE;
    ::DwmSetWindowAttribute(hWnd, DWMWA_REDIRECTIONBITMAP_ALPHA, &useAlpha, sizeof(useAlpha));
    const MARGINS margins = CCC_CaptionHostMargins(hWnd);
    ::DwmExtendFrameIntoClientArea(hWnd, &margins);
}

// 先にキー色で塗り潰してから TransparentBlt。残像防止（Win10 / 非 DWM 経路）。
// Win11 アクリルでは α が残るので CCC_BlitChromaNF 側を使う。
static void CCC_ClearDestBlt(HDC hdcDest, int x, int y, int w, int h,
    HDC hdcSrc, int srcX, int srcY, COLORREF clrKey)
{
    if (w <= 0 || h <= 0) return;
    RECT rc = { x, y, x + w, y + h };
    CBrush br(clrKey);
    ::FillRect(hdcDest, &rc, (HBRUSH)br.GetSafeHandle());
    ::TransparentBlt(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, w, h, clrKey);
}

#endif

// ============================================================================
// 共通の描画処理
// ============================================================================

#if CCUSTOM_AERO_SUPPORT
static void CCC_FillRectOpaqueBits(HDC hdc, const RECT& rc, COLORREF clr);
static void CCC_MakeRectOpaquePreserve(HDC hdc, const RECT& rc);
#endif
static void CCC_DrawInwomanDlgBody(CDC* pDC, const CRect& rc);

// ダイアログ WM_ERASEBKGND 共通。Win11 アクリルは消去しない（ガラス源を残す）。
// キャプション帯だけアクリルのときは本文だけ不透明塗り。淫女は本文に重ねる。
static BOOL DlgOnEraseBkgnd(CDC* pDC, CBrush& brDlg, BOOL bAeroEnabled, HWND hWnd)
{
    UNREFERENCED_PARAMETER(hWnd);
#if CCUSTOM_AERO_SUPPORT
    if (bAeroEnabled && CCC_IsWin11())
        return TRUE;
    if (bAeroEnabled)
    {
        CRect r;
        ::GetClientRect(hWnd, &r);
        pDC->FillSolidRect(&r, RGB(248, 248, 248)); // Win10 フォールバック（ガラス不可）
        return TRUE;
    }
#else
    UNREFERENCED_PARAMETER(bAeroEnabled);
#endif
    CRect r;
    ::GetClientRect(hWnd, &r);
    // カスタムキャプション帯は save.aero に関係なくアクリル源を残す（帯だけ塗らない）
    const int capH = CCC_GetCustomCaptionHeight(hWnd);
#if CCUSTOM_AERO_SUPPORT
    if (capH > 0 && CCC_IsWin11() && r.Height() > capH) {
        r.top = capH;
        CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), r, COLOR_DIALOG_BG);
        if (CCC_IsInwoman())
            CCC_DrawInwomanDlgBody(pDC, r);
        return TRUE;
    }
#endif
    if (capH > 0 && r.Height() > capH) {
        r.top = capH;
        pDC->FillRect(&r, &brDlg);
        if (CCC_IsInwoman())
            CCC_DrawInwomanDlgBody(pDC, r);
        return TRUE;
    }
    pDC->FillRect(&r, &brDlg);
    if (CCC_IsInwoman())
        CCC_DrawInwomanDlgBody(pDC, r);
    return TRUE;
}

// ダイアログ WM_PAINT のアクリル側。Win11 は薄いグレーで隙間だけ（子は ClipChildren）。
// 実体のガラスは DWM。ここをべた塗りするとアクリルが死ぬので BeginPaint だけでも可。
static void DlgOnPaintAero(CWnd* pWnd, BOOL bAeroEnabled)
{
#if CCUSTOM_AERO_SUPPORT
    if (bAeroEnabled && CCC_IsWin11())
    {
        CPaintDC dc(pWnd);
        CRect rect;
        pWnd->GetClientRect(&rect);
        dc.FillSolidRect(&rect, RGB(250, 250, 250)); // 隙間の薄い下地（子の下は見えない）
        return;
    }
#else
    UNREFERENCED_PARAMETER(bAeroEnabled);
#endif
    CPaintDC dc(pWnd);
}

#if CCUSTOM_AERO_SUPPORT
#include <uxtheme.h>
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "uxtheme.lib")
#ifndef GCS_RESULTSTR
#define GCS_RESULTSTR 0x0800
#endif
#ifndef DTT_COMPOSITED
#define DTT_TEXTCOLOR  0x00000001
#define DTT_GLOWSIZE   0x00000800
#define DTT_COMPOSITED 0x00002000
#endif

// 矩形を α=255 の不透明色で塗る（REDIRECTIONBITMAP_ALPHA 時の素 FillRect 透過を防ぐ）
static void CCC_FillRectOpaqueBits(HDC hdc, const RECT& rc, COLORREF clr)
{
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0 || !hdc) return;

    // 再利用DIB + AlphaBlend（毎フレ BeginBufferedPaint + 画素ループを避ける）
    {
        static CCC_ChromaBlitCache s_fillCaches[4]; // サイズ別 4 スロット（交互サイズで破棄しない）
        static COLORREF s_fillClr[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
        static unsigned s_fillNext = 0;
        CCC_ChromaBlitCache* pCache = nullptr;
        unsigned hit = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (s_fillCaches[i].pBits && s_fillCaches[i].dibW == w && s_fillCaches[i].dibH == h) {
                pCache = &s_fillCaches[i];
                hit = i;
                break;
            }
        }
        if (!pCache) {
            hit = (s_fillNext++) % 4;
            pCache = &s_fillCaches[hit];
            if (!pCache->Ensure(hdc, w, h))
                pCache = nullptr;
            else
                s_fillClr[hit] = 0xFFFFFFFFu;
        }
        if (pCache && pCache->pBits && pCache->hdcDib) {
            if (s_fillClr[hit] != clr) {
                const UINT32 px = 0xFF000000u // A=255 不透明。DIB は 0xAARRGGBB
                    | ((UINT32)GetRValue(clr) << 16)
                    | ((UINT32)GetGValue(clr) << 8)
                    | (UINT32)GetBValue(clr);
                UINT32* p = (UINT32*)pCache->pBits;
                const int n = w * h;
                int i = 0;
                for (; i + 3 < n; i += 4) {
                    p[i] = px; p[i + 1] = px; p[i + 2] = px; p[i + 3] = px;
                }
                for (; i < n; ++i)
                    p[i] = px;
                s_fillClr[hit] = clr;
            }
            const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            if (::GdiAlphaBlend(hdc, rc.left, rc.top, w, h,
                    pCache->hdcDib, 0, 0, w, h, bf))
                return;
        }
    }

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdc, &rc, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (hdcBuf && hBP) {
        HBRUSH br = ::CreateSolidBrush(clr);
        ::FillRect(hdcBuf, &rc, br);
        ::DeleteObject(br);
        ::BufferedPaintMakeOpaque(hBP, &rc);
        ::EndBufferedPaint(hBP, TRUE);
        return;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* pBits = nullptr;
    HBITMAP hDib = ::CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hDib || !pBits) {
        HBRUSH br = ::CreateSolidBrush(clr);
        ::FillRect(hdc, &rc, br);
        ::DeleteObject(br);
        return;
    }
    {
        HDC hdcMem = ::CreateCompatibleDC(hdc);
        HGDIOBJ old = ::SelectObject(hdcMem, hDib);
        HBRUSH br = ::CreateSolidBrush(clr);
        RECT zr = { 0, 0, w, h };
        ::FillRect(hdcMem, &zr, br);
        ::DeleteObject(br);
        UINT32* px = (UINT32*)pBits;
        const int n = w * h;
        for (int i = 0; i < n; ++i)
            px[i] |= 0xFF000000u;
        const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        ::GdiAlphaBlend(hdc, rc.left, rc.top, w, h, hdcMem, 0, 0, w, h, bf);
        ::SelectObject(hdcMem, old);
        ::DeleteDC(hdcMem);
    }
    ::DeleteObject(hDib);
}

// BufferedPaint バッファを α=0 でクリア。未クリアだと前回の不透明画素が残る。
// rowLength==w なら一括 ZeroMemory、違うなら行ストライド付き。
static void CCC_InitBPClear(HPAINTBUFFER hBP, int w, int h)
{
    RGBQUAD* pPixels = nullptr;
    int rowLength = 0;
    if (FAILED(::GetBufferedPaintBits(hBP, &pPixels, &rowLength)) || !pPixels || w <= 0 || h <= 0)
        return;
    if (rowLength == w) {
        ::ZeroMemory(pPixels, (size_t)w * (size_t)h * sizeof(RGBQUAD));
        return;
    }
    for (int y = 0; y < h; ++y)
    {
        RGBQUAD* pRow = reinterpret_cast<RGBQUAD*>(
            reinterpret_cast<BYTE*>(pPixels) + y * rowLength * static_cast<int>(sizeof(RGBQUAD)));
        ::ZeroMemory(pRow, w * sizeof(RGBQUAD));
    }
}

// COLORREF(0x00BBGGRR) → DIB の 0x00RRGGBB マスク。α は見ない。
static UINT32 CCC_RgbMask(COLORREF clr)
{
    return (UINT32)(GetRValue(clr) << 16) | (UINT32)(GetGValue(clr) << 8) | GetBValue(clr);
}

// 32bit DIB のキー色画素を α=0、それ以外を α=255。クロマ blit の本体。
// キーは CCC_AERO_CHROMA_KEY(1,1,1)。黒文字を抜かないこと。
static void CCC_AlphaFromChromaRect(void* pBits, int dibW, int dibH, const RECT& rc, COLORREF clrKey)
{
    if (!pBits || dibW <= 0 || dibH <= 0) return;
    int x0 = rc.left, y0 = rc.top, x1 = rc.right, y1 = rc.bottom;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dibW) x1 = dibW;
    if (y1 > dibH) y1 = dibH;
    if (x0 >= x1 || y0 >= y1) return;

    const UINT32 key = CCC_RgbMask(clrKey);
    const UINT32 opaque = 0xFF000000u;
    UINT32* base = (UINT32*)pBits;
    for (int y = y0; y < y1; ++y) {
        UINT32* row = base + y * dibW + x0;
        const int n = x1 - x0;
        int i = 0;
        for (; i + 3 < n; i += 4) {
            UINT32 p0 = row[i];     UINT32 p1 = row[i + 1];
            UINT32 p2 = row[i + 2]; UINT32 p3 = row[i + 3];
            row[i]     = ((p0 & 0x00FFFFFFu) == key) ? 0u : (p0 | opaque);
            row[i + 1] = ((p1 & 0x00FFFFFFu) == key) ? 0u : (p1 | opaque);
            row[i + 2] = ((p2 & 0x00FFFFFFu) == key) ? 0u : (p2 | opaque);
            row[i + 3] = ((p3 & 0x00FFFFFFu) == key) ? 0u : (p3 | opaque);
        }
        for (; i < n; ++i) {
            const UINT32 p = row[i];
            row[i] = ((p & 0x00FFFFFFu) == key) ? 0u : (p | opaque);
        }
    }
}

// 全面版。部分更新は CCC_AlphaFromChromaRect。
static void CCC_AlphaFromChroma(void* pBits, int w, int h, COLORREF clrKey)
{
    if (!pBits || w <= 0 || h <= 0) return;
    RECT rc = { 0, 0, w, h };
    CCC_AlphaFromChromaRect(pBits, w, h, rc, clrKey);
}

// top-down 32bit DIB。α 付き合成（GdiAlphaBlend / AC_SRC_ALPHA）用。
static HBITMAP CCC_CreateAlphaDib32(HDC hdcRef, int w, int h, void** ppBits)
{
    if (ppBits) *ppBits = nullptr;
    if (!hdcRef || w <= 0 || h <= 0) return NULL;
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hBmp = ::CreateDIBSection(hdcRef, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (ppBits) *ppBits = bits;
    if (!hBmp || !bits)
    {
        if (hBmp) ::DeleteObject(hBmp);
        if (ppBits) *ppBits = nullptr;
        return NULL;
    }
    return hBmp;
}

// 再利用 DIB を破棄。Ensure のサイズ不一致時やコントロール破棄時。
void CCC_ChromaBlitCache::Release()
{
    if (hdcDib) {
        if (hOldBmp) ::SelectObject(hdcDib, hOldBmp);
        ::DeleteDC(hdcDib);
    }
    if (hDib) ::DeleteObject(hDib);
    hDib = NULL;
    hdcDib = NULL;
    hOldBmp = NULL;
    pBits = nullptr;
    dibW = 0;
    dibH = 0;
}

// 指定サイズの 32bit DIB+DC を用意。同じサイズなら再利用（毎フレ Create 回避）。
BOOL CCC_ChromaBlitCache::Ensure(HDC hdcRef, int w, int h)
{
    if (w <= 0 || h <= 0 || !hdcRef) return FALSE;
    if (hDib && hdcDib && pBits && dibW == w && dibH == h) return TRUE;
    Release();
    hDib = CCC_CreateAlphaDib32(hdcRef, w, h, &pBits);
    if (!hDib || !pBits) {
        Release();
        return FALSE;
    }
    hdcDib = ::CreateCompatibleDC(hdcRef);
    if (!hdcDib) {
        Release();
        return FALSE;
    }
    hOldBmp = ::SelectObject(hdcDib, hDib);
    dibW = w;
    dibH = h;
    return TRUE;
}

// 縦スクロール用: 下の行を上へ memmove。新規帯は呼び出し側が UpdateRect。
void CCC_ChromaBlitCache::ScrollRows(int y, int height, int scrollPx)
{
    if (!pBits || dibW <= 0 || scrollPx <= 0 || height <= scrollPx || y < 0 || y + height > dibH)
        return;
    UINT32* base = (UINT32*)pBits;
    const int preserveH = height - scrollPx;
    UINT32* dst = base + y * dibW;
    UINT32* src = base + (y + scrollPx) * dibW;
    memmove(dst, src, (size_t)preserveH * (size_t)dibW * sizeof(UINT32));
}

// 矩形内を左へ scrollPx ずらす（波形スクロール用）。α 付き画素をそのまま memmove。
void CCC_ChromaBlitCache::ScrollCols(int x, int y, int width, int height, int scrollPx)
{
    // 矩形内を左へ scrollPx ずらす(波形スクロール用)。アルファ付きピクセルをそのまま移動。
    if (!pBits || dibW <= 0 || scrollPx <= 0 || width <= scrollPx)
        return;
    if (x < 0 || y < 0 || x + width > dibW || y + height > dibH)
        return;
    UINT32* base = (UINT32*)pBits;
    const int keep = width - scrollPx;
    for (int row = 0; row < height; ++row) {
        UINT32* dst = base + (y + row) * dibW + x;
        memmove(dst, dst + scrollPx, (size_t)keep * sizeof(UINT32));
    }
}

// 差分矩形を SRCCOPY 後、キー色だけ α=0。ピアノ/波形の部分更新向き。
BOOL CCC_ChromaBlitCache::UpdateRect(HDC hdcSrc, int srcX, int srcY, int dx, int dy, int rw, int rh, COLORREF clrKey)
{
    if (!hdcSrc || rw <= 0 || rh <= 0 || !pBits || !hdcDib) return FALSE;
    if (dx < 0 || dy < 0 || dx + rw > dibW || dy + rh > dibH) return FALSE;

    {
        CDC dcDib;
        dcDib.Attach(hdcDib);
        dcDib.SetStretchBltMode(COLORONCOLOR);
        dcDib.BitBlt(dx, dy, rw, rh, CDC::FromHandle(hdcSrc), srcX, srcY, SRCCOPY);
        dcDib.Detach();
    }
    RECT rc = { dx, dy, dx + rw, dy + rh };
    CCC_AlphaFromChromaRect(pBits, dibW, dibH, rc, clrKey);
    return TRUE;
}

// クロマ無し。BitBlt 後 α=255。キャプションガラス下の本文提示用。
BOOL CCC_ChromaBlitCache::UpdateOpaqueRect(HDC hdcSrc, int srcX, int srcY, int dx, int dy, int rw, int rh)
{
    if (!hdcSrc || rw <= 0 || rh <= 0 || !pBits || !hdcDib) return FALSE;
    if (dx < 0 || dy < 0 || dx + rw > dibW || dy + rh > dibH) return FALSE;

    {
        CDC dcDib;
        dcDib.Attach(hdcDib);
        dcDib.SetStretchBltMode(COLORONCOLOR);
        dcDib.BitBlt(dx, dy, rw, rh, CDC::FromHandle(hdcSrc), srcX, srcY, SRCCOPY);
        dcDib.Detach();
    }
    MakeRectOpaque(dx, dy, rw, rh);
    return TRUE;
}

// 単色塗り + キー色を α=0。ベタ塗りのあと文字だけ残すとき。
BOOL CCC_ChromaBlitCache::FillOpaqueRect(int x, int y, int rw, int rh, COLORREF color, COLORREF chromaKey)
{
    if (!hdcDib || !pBits || rw <= 0 || rh <= 0) return FALSE;
    if (x < 0 || y < 0 || x + rw > dibW || y + rh > dibH) return FALSE;
    {
        CDC dcDib;
        dcDib.Attach(hdcDib);
        dcDib.FillSolidRect(x, y, rw, rh, color);
        dcDib.Detach();
    }
    RECT rc = { x, y, x + rw, y + rh };
    CCC_AlphaFromChromaRect(pBits, dibW, dibH, rc, chromaKey);
    return TRUE;
}

// 矩形の α を強制 255。オーバーレイ焼き込み後の「ガラスに穴」防止。
void CCC_ChromaBlitCache::MakeRectOpaque(int x, int y, int rw, int rh)
{
    if (!pBits || rw <= 0 || rh <= 0 || dibW <= 0) return;
    if (x < 0 || y < 0 || x + rw > dibW || y + rh > dibH) return;
    UINT32* base = (UINT32*)pBits;
    for (int row = 0; row < rh; ++row) {
        UINT32* p = base + (size_t)(y + row) * (size_t)dibW + x;
        int col = 0;
        for (; col + 3 < rw; col += 4) {
            p[col] |= 0xFF000000u;
            p[col + 1] |= 0xFF000000u;
            p[col + 2] |= 0xFF000000u;
            p[col + 3] |= 0xFF000000u;
        }
        for (; col < rw; ++col)
            p[col] |= 0xFF000000u;
    }
}

// キャッシュ DIB の部分矩形を画面へ。GdiAlphaBlend 優先（BeginBufferedPaint は 60fps で重い）。
BOOL CCC_ChromaBlitCache::BlitRect(HDC hdcDest, int x, int y, int w, int h)
{
    if (!hdcDest || w <= 0 || h <= 0 || !pBits || !hdcDib || dibW <= 0 || dibH <= 0) return FALSE;
    if (x < 0 || y < 0 || x + w > dibW || y + h > dibH) return FALSE;

    // BeginBufferedPaint はピアノ/アナライザ 60fps で約数倍重い。α付き DIB を直接合成する。
    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (::GdiAlphaBlend(hdcDest, x, y, w, h, hdcDib, x, y, w, h, bf))
        return TRUE;

    static LONG s_bpInited = 0;
    if (InterlockedCompareExchange(&s_bpInited, 1, 0) == 0)
        ::BufferedPaintInit();
    RECT rect = { x, y, x + w, y + h };
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP) return FALSE;
    CCC_InitBPClear(hBP, w, h);
    ::GdiAlphaBlend(hdcBuf, x, y, w, h, hdcDib, x, y, w, h, bf);
    ::EndBufferedPaint(hBP, TRUE);
    return TRUE;
}

// 全面提示。dest は画面座標（キャプション下 y>0 でも buffer 原点と混同しない）。
BOOL CCC_ChromaBlitCache::BlitFull(HDC hdcDest, int x, int y, int w, int h)
{
    if (!hdcDest || w <= 0 || h <= 0 || !pBits || !hdcDib || dibW != w || dibH != h) return FALSE;

    // キャプション下(y>0)でも dest は画面座標。BeginBufferedPaint を避けて直接合成。
    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (::GdiAlphaBlend(hdcDest, x, y, w, h, hdcDib, 0, 0, w, h, bf))
        return TRUE;

    static LONG s_bpInited = 0;
    if (InterlockedCompareExchange(&s_bpInited, 1, 0) == 0)
        ::BufferedPaintInit();
    RECT rect = { x, y, x + w, y + h };
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP) return FALSE;
    CCC_InitBPClear(hBP, w, h);
    ::GdiAlphaBlend(hdcBuf, x, y, w, h, hdcDib, 0, 0, w, h, bf);
    ::EndBufferedPaint(hBP, TRUE);
    return TRUE;
}

// キャッシュ DIB へ描いてキー→α、GdiAlphaBlend。失敗時のみ BeginBufferedPaint。
// 先にキー色で全面初期化してから Stretch/BitBlt（残像防止）。
static BOOL CCC_BlitChromaCachedRect(HDC hdcDest, const RECT& rect, HDC hdcSrc, int srcX, int srcY,
    int destW, int destH, int srcW, int srcH, COLORREF clrKey, CCC_ChromaBlitCache& cache)
{
    if (destW <= 0 || destH <= 0) return FALSE;
    if (!cache.Ensure(hdcDest, destW, destH)) return FALSE;

    {
        CDC dcDib;
        dcDib.Attach(cache.hdcDib);
        dcDib.FillSolidRect(0, 0, destW, destH, clrKey);
        dcDib.SetStretchBltMode(COLORONCOLOR);
        if (destW != srcW || destH != srcH)
            dcDib.StretchBlt(0, 0, destW, destH, CDC::FromHandle(hdcSrc), srcX, srcY, srcW, srcH, SRCCOPY);
        else
            dcDib.BitBlt(0, 0, destW, destH, CDC::FromHandle(hdcSrc), srcX, srcY, SRCCOPY);
        dcDib.Detach();
    }
    CCC_AlphaFromChroma(cache.pBits, destW, destH, clrKey);

    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (::GdiAlphaBlend(hdcDest, rect.left, rect.top, destW, destH,
            cache.hdcDib, 0, 0, destW, destH, bf))
        return TRUE;

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP) return FALSE;

    CCC_InitBPClear(hBP, destW, destH);
    ::GdiAlphaBlend(hdcBuf, rect.left, rect.top, destW, destH, cache.hdcDib, 0, 0, destW, destH, bf);
    ::EndBufferedPaint(hBP, TRUE);
    return TRUE;
}

// 画面 FillRect なし。バッファを毎回クロマキーで全面初期化してからアルファ合成（残像防止）
// BeginBufferedPaint の buffer DC はクライアント座標系を共有するため、
// GdiAlphaBlend の描画先は (0,0) でなく (rect.left, rect.top) でなければならない。
// (0,0) に描くと EndBufferedPaint が prcTarget でクリップした際に座標がずれる。
static BOOL CCC_BlitChromaNFRect(HDC hdcDest, const RECT& rect, HDC hdcSrc, int srcX, int srcY,
    int destW, int destH, int srcW, int srcH, COLORREF clrKey, BOOL bStretch)
{
    if (destW <= 0 || destH <= 0) return FALSE;

    void* pBits = nullptr;
    HBITMAP hDib = CCC_CreateAlphaDib32(hdcDest, destW, destH, &pBits);
    if (!hDib || !pBits) return FALSE;

    CDC dcDib, dcSrc;
    dcDib.CreateCompatibleDC(CDC::FromHandle(hdcDest));
    dcSrc.Attach(hdcSrc);
    HGDIOBJ hOld = ::SelectObject(dcDib.GetSafeHdc(), hDib);
    dcDib.FillSolidRect(0, 0, destW, destH, clrKey);
    dcDib.SetStretchBltMode(COLORONCOLOR);
    if (bStretch)
        dcDib.StretchBlt(0, 0, destW, destH, &dcSrc, srcX, srcY, srcW, srcH, SRCCOPY);
    else
        dcDib.BitBlt(0, 0, destW, destH, &dcSrc, srcX, srcY, SRCCOPY);
    CCC_AlphaFromChroma(pBits, destW, destH, clrKey);

    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (::GdiAlphaBlend(hdcDest, rect.left, rect.top, destW, destH,
            dcDib.GetSafeHdc(), 0, 0, destW, destH, bf)) {
        ::SelectObject(dcDib.GetSafeHdc(), hOld);
        ::DeleteObject(hDib);
        dcSrc.Detach();
        return TRUE;
    }

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP)
    {
        ::SelectObject(dcDib.GetSafeHdc(), hOld);
        ::DeleteObject(hDib);
        dcSrc.Detach();
        return FALSE;
    }

    CCC_InitBPClear(hBP, destW, destH);
    // buffer DC はクライアント座標系なので rect の左上から描画する
    ::GdiAlphaBlend(hdcBuf, rect.left, rect.top, destW, destH, dcDib.GetSafeHdc(), 0, 0, destW, destH, bf);
    ::EndBufferedPaint(hBP, TRUE);

    ::SelectObject(dcDib.GetSafeHdc(), hOld);
    ::DeleteObject(hDib);
    dcSrc.Detach();
    return TRUE;
}

// アクリル上の矩形を α=0 に戻す（隙間をガラスにする）。clrKey は互換のため残置。
void CCC_ClearRectChroma(HDC hdcDest, const RECT& rect, COLORREF clrKey)
{
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (hdcBuf && hBP)
    {
        CCC_InitBPClear(hBP, w, h);
        ::EndBufferedPaint(hBP, TRUE);
    }
}

// アクリルホスト上に定数αの矩形。本文パネルの透け・淫女の火照り帯。
// alpha>=255 は不透明経路。SourceConstantAlpha のみ（プレマルチ不要）。
void CCC_FillRectAlpha(HDC hdc, const RECT& rc, COLORREF clr, BYTE alpha)
{
	const int w = rc.right - rc.left;
	const int h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0 || !hdc || alpha == 0)
		return;
	if (alpha >= 255) {
		CCC_FillRectOpaqueBits(hdc, rc, clr);
		return;
	}

	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* pBits = nullptr;
	HBITMAP hDib = ::CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
	if (!hDib || !pBits) {
		CCC_FillRectOpaqueBits(hdc, rc, clr);
		return;
	}
	HDC hdcMem = ::CreateCompatibleDC(hdc);
	HGDIOBJ old = ::SelectObject(hdcMem, hDib);
	HBRUSH br = ::CreateSolidBrush(clr);
	RECT zr = { 0, 0, w, h };
	::FillRect(hdcMem, &zr, br);
	::DeleteObject(br);
	// プレマルチプライ不要: SourceConstantAlpha のみで合成
	UINT32* px = (UINT32*)pBits;
	const int n = w * h;
	for (int i = 0; i < n; ++i)
		px[i] |= 0xFF000000u;
	const BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
	::GdiAlphaBlend(hdc, rc.left, rc.top, w, h, hdcMem, 0, 0, w, h, bf);
	::SelectObject(hdcMem, old);
	::DeleteDC(hdcMem);
	::DeleteObject(hDib);
}

// 子の隙間だけガラス（α=0）。親 OnPaint から。pPreserveRect はバナー等を残す除外。
void CCC_PaintAeroGaps(CDC& dc, CWnd* pWnd, const RECT* pPreserveRect)
{
    if (!pWnd || !pWnd->GetSafeHwnd()) return;
    CRect clip;
    if (dc.GetClipBox(&clip) == ERROR || clip.IsRectEmpty()) return;
    CCC_ClipNoChildren(dc, pWnd);
    if (pPreserveRect)
    {
        CRect preserve(pPreserveRect);
        if (preserve.PtInRect(clip.TopLeft()) && preserve.PtInRect(clip.BottomRight()))
            return;
        CRgn rgnClip, rgnPreserve;
        rgnClip.CreateRectRgnIndirect(&clip);
        rgnPreserve.CreateRectRgnIndirect(pPreserveRect);
        rgnClip.CombineRgn(&rgnClip, &rgnPreserve, RGN_DIFF);
        dc.SelectClipRgn(&rgnClip, RGN_AND);
        if (dc.GetClipBox(&clip) == NULLREGION || clip.IsRectEmpty()) return;
    }
    RECT rcClip = clip;
    CCC_ClearRectChroma(dc.GetSafeHdc(), rcClip, CCC_AERO_CHROMA_KEY);
}

// DC クリップから可視の子 HWND 矩形を差し引く。隙間塗りが子の上に乗らないように。
void CCC_ClipNoChildren(CDC& dc, CWnd* pWnd)
{
    if (!pWnd || !pWnd->GetSafeHwnd()) return;
    CRect cr;
    pWnd->GetClientRect(&cr);
    CRgn rgn;
    rgn.CreateRectRgnIndirect(&cr);
    for (HWND h = ::GetWindow(pWnd->m_hWnd, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT))
    {
        if (!::IsWindowVisible(h)) continue;
        CRect r;
        ::GetWindowRect(h, &r);
        pWnd->ScreenToClient(&r);
        CRgn rc;
        rc.CreateRectRgnIndirect(&r);
        rgn.CombineRgn(&rgn, &rc, RGN_DIFF);
    }
    dc.SelectClipRgn(&rgn, RGN_AND);
}

// キャプション常時アクリル(dffb3db〜)下の不透明Blit。毎フレ BeginBufferedPaint すると
// バナーGDI/ピアノ等が約数倍重くなるため、再利用DIBへ描いて α=255 付き AlphaBlend する。
static void CCC_BlitToRectOpaque(HDC hdcDest, const RECT& rect, HDC hdcSrc, int srcX, int srcY,
    int destW, int destH, int srcW, int srcH, BOOL bStretch)
{
    if (destW <= 0 || destH <= 0 || !hdcDest || !hdcSrc) return;

    static CCC_ChromaBlitCache s_opaqueCaches[8]; // バナー等の複数サイズ共存
    static unsigned s_opaqueNext = 0;
    CCC_ChromaBlitCache* pCache = nullptr;
    for (auto& c : s_opaqueCaches) {
        if (c.pBits && c.dibW == destW && c.dibH == destH) {
            pCache = &c;
            break;
        }
    }
    if (!pCache) {
        pCache = &s_opaqueCaches[s_opaqueNext++ % 8];
        if (!pCache->Ensure(hdcDest, destW, destH))
            pCache = nullptr;
    }
    if (pCache && pCache->pBits && pCache->hdcDib) {
        ::SetStretchBltMode(pCache->hdcDib, COLORONCOLOR);
        if (bStretch && (destW != srcW || destH != srcH))
            ::StretchBlt(pCache->hdcDib, 0, 0, destW, destH, hdcSrc, srcX, srcY, srcW, srcH, SRCCOPY);
        else
            ::BitBlt(pCache->hdcDib, 0, 0, destW, destH, hdcSrc, srcX, srcY, SRCCOPY);
        pCache->MakeRectOpaque(0, 0, destW, destH);
        const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        if (::GdiAlphaBlend(hdcDest, rect.left, rect.top, destW, destH,
                pCache->hdcDib, 0, 0, destW, destH, bf))
            return;
    }

    // フォールバック: 旧 BeginBufferedPaint 経路
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (hdcBuf && hBP)
    {
        ::SetStretchBltMode(hdcBuf, COLORONCOLOR);
        if (bStretch)
            ::StretchBlt(hdcBuf, rect.left, rect.top, destW, destH, hdcSrc, srcX, srcY, srcW, srcH, SRCCOPY);
        else
            ::BitBlt(hdcBuf, rect.left, rect.top, destW, destH, hdcSrc, srcX, srcY, SRCCOPY);
        ::BufferedPaintMakeOpaque(hBP, &rect);
        ::EndBufferedPaint(hBP, TRUE);
        return;
    }
    ::SetStretchBltMode(hdcDest, COLORONCOLOR);
    if (bStretch)
        ::StretchBlt(hdcDest, rect.left, rect.top, destW, destH, hdcSrc, srcX, srcY, srcW, srcH, SRCCOPY);
    else
        ::BitBlt(hdcDest, rect.left, rect.top, destW, destH, hdcSrc, srcX, srcY, SRCCOPY);
}

// 拡縮つき不透明 blit。キャプションガラス下のバナー/ジャケット等。
void CCC_BlitStretchOpaque(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH)
{
    RECT rect = { x, y, x + destW, y + destH };
    CCC_BlitToRectOpaque(hdcDest, rect, hdcSrc, srcX, srcY, destW, destH, srcW, srcH, TRUE);
}

// 互換クロマ: memDC にキー塗り→Stretch/BitBlt→TransparentBlt。
// Win11 DWM では α が残るので NF 経路を優先すること。
static void CCC_BlitToRectChroma(HDC hdcDest, const RECT& rect, HDC hdcSrc, int srcX, int srcY,
    int destW, int destH, int srcW, int srcH, COLORREF clrKey, BOOL bStretch)
{
    if (destW <= 0 || destH <= 0) return;
    CDC dcDest, dcSrc;
    dcDest.Attach(hdcDest);
    dcSrc.Attach(hdcSrc);
    CDC memDC;
    memDC.CreateCompatibleDC(&dcDest);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&dcDest, destW, destH);
    CBitmap* pOld = memDC.SelectObject(&bmp);
    memDC.FillSolidRect(0, 0, destW, destH, clrKey);
    memDC.SetStretchBltMode(COLORONCOLOR);
    if (bStretch)
        memDC.StretchBlt(0, 0, destW, destH, &dcSrc, srcX, srcY, srcW, srcH, SRCCOPY);
    else
        memDC.BitBlt(0, 0, destW, destH, &dcSrc, srcX, srcY, SRCCOPY);
    CCC_ClearDestBlt(hdcDest, rect.left, rect.top, destW, destH,
        memDC.GetSafeHdc(), 0, 0, clrKey);
    memDC.SelectObject(pOld);
    dcDest.Detach();
    dcSrc.Detach();
}

// 拡縮つきクロマ（旧 TransparentBlt 経路）。ちらつき許容のフォールバック。
void CCC_BlitStretchChroma(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH, COLORREF clrKey)
{
    RECT rect = { x, y, x + destW, y + destH };
    CCC_BlitToRectChroma(hdcDest, rect, hdcSrc, srcX, srcY, destW, destH, srcW, srcH, clrKey, TRUE);
}

// 拡縮つき残像なしクロマ。4 スロットプール（サイズ交互で毎フレ Ensure しない）。
void CCC_BlitStretchNF(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH, COLORREF clrKey)
{
    // UI スレッド想定。DIB を再利用して毎フレーム CreateDIBSection を避ける。
    // バナー/ジャケット/曲情報パネルなど複数サイズの呼び出し元が共有するため、
    // 1本だと Ensure の作り直しが毎フレーム交互に起きる。サイズ一致スロットを
    // 使い、無ければラウンドロビンで置き換える小さなプールにする。
    static CCC_ChromaBlitCache s_nfCaches[4];
    static unsigned s_nfNext = 0;
    CCC_ChromaBlitCache* pCache = nullptr;
    for (auto& c : s_nfCaches) {
        if (c.dibW == destW && c.dibH == destH) { pCache = &c; break; }
    }
    if (!pCache) {
        for (auto& c : s_nfCaches) {
            if (!c.hDib) { pCache = &c; break; } // 未使用スロット優先
        }
    }
    if (!pCache)
        pCache = &s_nfCaches[(s_nfNext++) % _countof(s_nfCaches)];
    RECT rect = { x, y, x + destW, y + destH };
    if (destW > 0 && destH > 0 &&
        CCC_BlitChromaCachedRect(hdcDest, rect, hdcSrc, srcX, srcY, destW, destH, srcW, srcH, clrKey, *pCache))
        return;
    if (!CCC_BlitChromaNFRect(hdcDest, rect, hdcSrc, srcX, srcY, destW, destH, srcW, srcH, clrKey, TRUE))
        CCC_BlitToRectChroma(hdcDest, rect, hdcSrc, srcX, srcY, destW, destH, srcW, srcH, clrKey, TRUE);
}

// 等倍クロマ。UI アニメには CCC_BlitChromaNF（残像なし）を使う。
void CCC_BlitChroma(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey)
{
    RECT rect = { x, y, x + w, y + h };
    CCC_BlitToRectChroma(hdcDest, rect, hdcSrc, srcX, srcY, w, h, w, h, clrKey, FALSE);
}

// 等倍・残像なしクロマ。静的 1 本キャッシュ。失敗時は一時 DIB → 旧 TransparentBlt。
void CCC_BlitChromaNF(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey)
{
    // UI スレッド想定。DIB を再利用して毎フレーム CreateDIBSection を避ける。
    static CCC_ChromaBlitCache s_nfCache;
    RECT rect = { x, y, x + w, y + h };
    if (w > 0 && h > 0 &&
        CCC_BlitChromaCachedRect(hdcDest, rect, hdcSrc, srcX, srcY, w, h, w, h, clrKey, s_nfCache))
        return;
    if (!CCC_BlitChromaNFRect(hdcDest, rect, hdcSrc, srcX, srcY, w, h, w, h, clrKey, FALSE))
        CCC_BlitChroma(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, clrKey);
}

// 呼び出し側キャッシュ付き等倍クロマ。失敗時は一時 DIB（静的 NF キャッシュは使わない）。
BOOL CCC_BlitChromaCached(HDC hdcDest, int x, int y, int w, int h,
    HDC hdcSrc, int srcX, int srcY, COLORREF clrKey, CCC_ChromaBlitCache& cache)
{
    if (w <= 0 || h <= 0) return FALSE;
    RECT rect = { x, y, x + w, y + h };
    if (CCC_BlitChromaCachedRect(hdcDest, rect, hdcSrc, srcX, srcY, w, h, w, h, clrKey, cache))
        return TRUE;
    // 呼び出し側キャッシュ失敗時は一時 DIB 経路(再帰しないよう NoFlicker の静的キャッシュは使わない)
    if (!CCC_BlitChromaNFRect(hdcDest, rect, hdcSrc, srcX, srcY, w, h, w, h, clrKey, FALSE))
        CCC_BlitChroma(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, clrKey);
    return TRUE;
}

// DWM アクリル向けエイリアス。中身は残像なしクロマ。
void CCC_BlitChromaDwm(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey)
{
    CCC_BlitChromaNF(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, clrKey);
}

// Win11+aero は NF（α 合成）、それ以外は ClearDestBlt（TransparentBlt）。
static void CCC_BlitChromaTrans(HDC hdcDest, int x, int y, int w, int h,
    HDC hdcSrc, int srcX, int srcY, COLORREF clrKey)
{
    if (w <= 0 || h <= 0) return;
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
        CCC_BlitChromaNF(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, clrKey);
    else
        CCC_ClearDestBlt(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, clrKey);
}
#endif
// ============================================================================
// アイコンを透明色を抜いて描画する関数
// ============================================================================
// ImageList アイコンを mask 色抜きで中央配置。ボタン/リストのグリフ用。
static void DrawTransparentIcon(CDC * pDC, CImageList * pIL, int idx, CRect rc, COLORREF mask)
{
    if (!pIL || idx < 0) return;

    IMAGEINFO ii;
    if (!pIL->GetImageInfo(idx, &ii)) return;

    int w = CRect(ii.rcImage).Width();
    int h = CRect(ii.rcImage).Height();

    CDC mDC;
    mDC.CreateCompatibleDC(pDC);
    CBitmap b;
    b.CreateCompatibleBitmap(pDC, w, h);
    CBitmap* ob = mDC.SelectObject(&b);

    mDC.FillSolidRect(0, 0, w, h, mask);
    pIL->Draw(&mDC, idx, CPoint(0, 0), ILD_NORMAL);

    ::TransparentBlt(pDC->GetSafeHdc(), rc.left + (rc.Width() - w) / 2, rc.top + (rc.Height() - h) / 2, w, h,
        mDC.GetSafeHdc(), 0, 0, w, h, mask);

    mDC.SelectObject(ob);
    b.DeleteObject();
    mDC.DeleteDC();
}

// 2色 GradientFill。nDir は度。45/135 付近で横、それ以外は縦。bR で始終点入替。
static void DrawGradientBackground(CDC* pDC, const CRect& rect, COLORREF cS, COLORREF cE, int nDir)
{
    int d = nDir % 360;
    if (d < 0) d += 360;

    BOOL bH = (d >= 45 && d < 135) || (d >= 225 && d < 315);
    BOOL bR = (d >= 135 && d < 315);

    COLORREF cA = bR ? cE : cS;
    COLORREF cB = bR ? cS : cE;

    TRIVERTEX v[2];
    v[0].x = rect.left;
    v[0].y = rect.top;
    v[0].Red = (COLOR16)(GetRValue(cA) << 8);
    v[0].Green = (COLOR16)(GetGValue(cA) << 8);
    v[0].Blue = (COLOR16)(GetBValue(cA) << 8);
    v[0].Alpha = 0;

    v[1].x = rect.right;
    v[1].y = rect.bottom;
    v[1].Red = (COLOR16)(GetRValue(cB) << 8);
    v[1].Green = (COLOR16)(GetGValue(cB) << 8);
    v[1].Blue = (COLOR16)(GetBValue(cB) << 8);
    v[1].Alpha = 0;

    GRADIENT_RECT gr = { 0, 1 };
    ::GradientFill(pDC->GetSafeHdc(), v, 2, &gr, 1, bH ? GRADIENT_FILL_RECT_H : GRADIENT_FILL_RECT_V);
}

#if CCUSTOM_AERO_SUPPORT
// 背景がクロマキーか。透過ラベルの影省略判定などに使う。
static BOOL CCC_IsChromaBg(COLORREF clrBg)
{
    return clrBg == CCC_AERO_CHROMA_KEY;
}

// 透過クロマキー合成: 描画は COLOR_DIALOG_BG 上で行い、未描画領域だけ chroma に置換する。
// 背景を直接 chroma で塗るとシャドウ等のアンチエイリアス縁が RGB(1,1,1) に溶けて消える。
// GetPixel/SetPixel の二重ループは分単位で UI を殺すため DIB 一括置換にする。
static void CCC_RemapSolidColorInDC(CDC& dc, const CRect& r, COLORREF clrFrom, COLORREF clrTo)
{
    const int w = r.Width();
    const int h = r.Height();
    if (w <= 0 || h <= 0) return;

    static HBITMAP s_dib = nullptr;
    static void* s_bits = nullptr;
    static int s_w = 0, s_h = 0;
    static CDC s_mem;
    static bool s_memReady = false;

    if (!s_memReady) {
        if (!s_mem.CreateCompatibleDC(&dc)) return;
        s_memReady = true;
    }
    if (w > s_w || h > s_h || !s_dib) {
        if (s_dib) { ::DeleteObject(s_dib); s_dib = nullptr; s_bits = nullptr; }
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        s_dib = ::CreateDIBSection(dc.GetSafeHdc(), &bmi, DIB_RGB_COLORS, &s_bits, nullptr, 0);
        if (!s_dib || !s_bits) { s_dib = nullptr; s_bits = nullptr; s_w = s_h = 0; return; }
        s_w = w; s_h = h;
    }

    HGDIOBJ old = s_mem.SelectObject(s_dib);
    s_mem.BitBlt(0, 0, w, h, &dc, r.left, r.top, SRCCOPY);
    const UINT32 from = ((UINT32)clrFrom) & 0x00FFFFFFu;
    const UINT32 to = ((UINT32)clrTo) & 0x00FFFFFFu;
    UINT32* p = (UINT32*)s_bits;
    const int n = s_w * s_h; // may be larger than w*h; only touch used rect
    for (int y = 0; y < h; ++y) {
        UINT32* row = p + y * s_w;
        for (int x = 0; x < w; ++x) {
            if ((row[x] & 0x00FFFFFFu) == from)
                row[x] = (row[x] & 0xFF000000u) | to;
        }
    }
    dc.BitBlt(r.left, r.top, w, h, &s_mem, 0, 0, SRCCOPY);
    s_mem.SelectObject(old);
}
#else
// アクリル無効ビルド。キー判定は常に偽。
static BOOL CCC_IsChromaBg(COLORREF) { return FALSE; }
#endif

// セグメント描画用フォントプール（CreateFont/Delete 嵐で分単位に UI が死ぬのを防ぐ）
// アクリル有無に依存しない（EQ コードラベル等で常時使用）
namespace {
struct SegFontSlot {
    LONG lfHeight = 0;
    LONG lfWidth = 0;
    LONG lfWeight = 0;
    BYTE lfItalic = 0;
    WCHAR face[LF_FACESIZE] = {};
    CFont font;
    bool alive = false;
};
constexpr int kSegFontPool = 48;
SegFontSlot g_segFontPool[kSegFontPool];
unsigned g_segFontRR = 0;

// LOGFONT 一致で再利用。不一致はラウンドロビンで上書き（48 スロット）。
CFont* CCC_GetPooledSegFont(const LOGFONT& lt)
{
    for (int i = 0; i < kSegFontPool; ++i) {
        SegFontSlot& s = g_segFontPool[i];
        if (!s.alive) continue;
        if (s.lfHeight == lt.lfHeight && s.lfWidth == lt.lfWidth
            && s.lfWeight == lt.lfWeight && s.lfItalic == lt.lfItalic
            && wcscmp(s.face, lt.lfFaceName) == 0)
            return &s.font;
    }
    const int i = (int)(g_segFontRR++ % (unsigned)kSegFontPool);
    SegFontSlot& s = g_segFontPool[i];
    if (s.alive && s.font.GetSafeHandle())
        s.font.DeleteObject();
    s.lfHeight = lt.lfHeight;
    s.lfWidth = lt.lfWidth;
    s.lfWeight = lt.lfWeight;
    s.lfItalic = lt.lfItalic;
    wcsncpy_s(s.face, lt.lfFaceName, _TRUNCATE);
    if (!s.font.CreateFontIndirect(&lt)) {
        s.alive = false;
        return nullptr;
    }
    s.alive = true;
    return &s.font;
}

// 太線ペンプール（スライダー描画の毎フレーム CreatePen 嵐を止める）
struct SegPenSlot {
    int width = 0;
    COLORREF color = 0;
    CPen pen;
    bool alive = false;
};
constexpr int kSegPenPool = 64;
SegPenSlot g_segPenPool[kSegPenPool];
unsigned g_segPenRR = 0;

// 幅+色で再利用。スライダー枠線の毎フレーム CreatePen を止める。
CPen* CCC_GetPooledPen(int width, COLORREF color)
{
    if (width < 1) width = 1;
    for (int i = 0; i < kSegPenPool; ++i) {
        SegPenSlot& s = g_segPenPool[i];
        if (!s.alive) continue;
        if (s.width == width && s.color == color)
            return &s.pen;
    }
    const int i = (int)(g_segPenRR++ % (unsigned)kSegPenPool);
    SegPenSlot& s = g_segPenPool[i];
    if (s.alive && s.pen.GetSafeHandle())
        s.pen.DeleteObject();
    s.width = width;
    s.color = color;
    if (!s.pen.CreatePen(PS_SOLID, width, color)) {
        s.alive = false;
        return nullptr;
    }
    s.alive = true;
    return &s.pen;
}

struct SegBrushSlot {
    COLORREF color = 0;
    CBrush brush;
    bool alive = false;
};
constexpr int kSegBrushPool = 32;
SegBrushSlot g_segBrushPool[kSegBrushPool];
unsigned g_segBrushRR = 0;

// ブラシプール。スライダー等の毎フレーム CreateSolidBrush 嵐を止める。
CBrush* CCC_GetPooledBrush(COLORREF color)
{
    for (int i = 0; i < kSegBrushPool; ++i) {
        SegBrushSlot& s = g_segBrushPool[i];
        if (!s.alive) continue;
        if (s.color == color)
            return &s.brush;
    }
    const int i = (int)(g_segBrushRR++ % (unsigned)kSegBrushPool);
    SegBrushSlot& s = g_segBrushPool[i];
    if (s.alive && s.brush.GetSafeHandle())
        s.brush.DeleteObject();
    s.color = color;
    if (!s.brush.CreateSolidBrush(color)) {
        s.alive = false;
        return nullptr;
    }
    s.alive = true;
    return &s.brush;
}
} // namespace

// テキスト影用 top-down 32bit DIB。α は後段で書き込む。
static HBITMAP CCC_CreateShadowDib32(HDC hdcRef, int w, int h, void** ppBits)
{
    if (ppBits) *ppBits = nullptr;
    if (!hdcRef || w <= 0 || h <= 0) return NULL;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = ::CreateDIBSection(hdcRef, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (ppBits) *ppBits = bits;
    if (!hBmp || !bits)
    {
        if (hBmp) ::DeleteObject(hBmp);
        if (ppBits) *ppBits = nullptr;
        return NULL;
    }
    return hBmp;
}

// 分離ボックスぼかし（横→縦）。テキスト影の nBlur。半径0は何もしない。
static void CCC_BoxBlurAlpha(BYTE* alpha, BYTE* tmp, int w, int h, int radius)
{
    if (!alpha || !tmp || radius <= 0 || w <= 0 || h <= 0) return;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int sum = 0, cnt = 0;
            for (int dx = -radius; dx <= radius; ++dx)
            {
                const int xx = x + dx;
                if (xx >= 0 && xx < w) { sum += alpha[y * w + xx]; cnt++; }
            }
            tmp[y * w + x] = (BYTE)(sum / max(1, cnt));
        }
    }

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int sum = 0, cnt = 0;
            for (int dy = -radius; dy <= radius; ++dy)
            {
                const int yy = y + dy;
                if (yy >= 0 && yy < h) { sum += tmp[yy * w + x]; cnt++; }
            }
            alpha[y * w + x] = (BYTE)(sum / max(1, cnt));
        }
    }
}

// アクリル帯の白文字。壁紙が白いと消えるので 1px 黒縁を先に置く。
static void CCC_DrawTextBlackEdge(HDC hdc, LPCWSTR text, int cch, const RECT* rc, UINT fmt, COLORREF fill)
{
    if (!hdc || !text || !rc)
        return;
    const int oldBk = ::SetBkMode(hdc, TRANSPARENT);
    const COLORREF oldFg = ::SetTextColor(hdc, RGB(0, 0, 0));
    static const POINT kOff[8] = {
        { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
        { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 }
    };
    for (int i = 0; i < 8; ++i) {
        RECT r = *rc;
        ::OffsetRect(&r, kOff[i].x, kOff[i].y);
        ::DrawTextW(hdc, text, cch, &r, fmt);
    }
    ::SetTextColor(hdc, fill);
    RECT rf = *rc;
    ::DrawTextW(hdc, text, cch, &rf, fmt);
    ::SetTextColor(hdc, oldFg);
    ::SetBkMode(hdc, oldBk);
}

static void CCC_DrawTextBlackEdge(CDC& dc, LPCTSTR text, CRect rc, UINT fmt, COLORREF fill)
{
    if (!dc.GetSafeHdc() || !text)
        return;
    CCC_DrawTextBlackEdge(dc.GetSafeHdc(), text, -1, &rc, fmt, fill);
}

#ifndef WP_CAPTION
#define WP_CAPTION 1
#endif
#ifndef CS_ACTIVE
#define CS_ACTIVE 1
#define CS_INACTIVE 2
#endif

// アクリル帯タイトル専用。ゼロ埋め DIB へ GDI DrawText すると ClearType が黒に溶けて
// 白文字が灰色＋白ハローになる。「メインに追随」は窓DCへ直描きなのでその問題が無い。
static void CCC_DrawCaptionTitleComposited(HDC hdcMem, HWND hDlg, LPCWSTR title, RECT* textRc, BOOL active)
{
    if (!hdcMem || !title || !textRc)
        return;
    static HTHEME s_capTheme = NULL;
    if (!s_capTheme && hDlg)
        s_capTheme = ::OpenThemeData(hDlg, L"WINDOW");
    const UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
    const int st = active ? CS_ACTIVE : CS_INACTIVE;
    if (s_capTheme) {
        DTTOPTS opt = {};
        opt.dwSize = sizeof(opt);
        opt.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;
        opt.crText = RGB(0, 0, 0);
        // 1px だと合成後に消えるので 2px まで。内側は白で塗り潰す。
        static const POINT kOff[] = {
            { -2, -2 }, { -1, -2 }, { 0, -2 }, { 1, -2 }, { 2, -2 },
            { -2, -1 }, { -1, -1 }, { 0, -1 }, { 1, -1 }, { 2, -1 },
            { -2,  0 }, { -1,  0 },            { 1,  0 }, { 2,  0 },
            { -2,  1 }, { -1,  1 }, { 0,  1 }, { 1,  1 }, { 2,  1 },
            { -2,  2 }, { -1,  2 }, { 0,  2 }, { 1,  2 }, { 2,  2 }
        };
        for (int i = 0; i < (int)_countof(kOff); ++i) {
            RECT r = *textRc;
            ::OffsetRect(&r, kOff[i].x, kOff[i].y);
            ::DrawThemeTextEx(s_capTheme, hdcMem, WP_CAPTION, st, title, -1, fmt, &r, &opt);
        }
        opt.crText = RGB(255, 255, 255);
        ::DrawThemeTextEx(s_capTheme, hdcMem, WP_CAPTION, st, title, -1, fmt, textRc, &opt);
        return;
    }
    CCC_DrawTextBlackEdge(hdcMem, title, -1, textRc, fmt, RGB(255, 255, 255));
}

// ソフトドロップシャドウ。黒文字を α マップ→ボックスぼかし→プレマルチ合成。
// bAeroTrans 時は peakA を下げてもクロマ段で半透明が黒縁になるため呼び出し側で省略。
// DIB/α バッファは静的再利用（毎描画 CreateDIBSection を抑制）。
static void DrawTextShadow(CDC* pDC, const CRect& rect, const CString& str, UINT fmt,
    COLORREF clrS, int nSD, int nDist, int nBlur, COLORREF clrBg, BOOL bAeroTrans,
    float scaleX = 1.0f, float scaleY = 1.0f)
{
    UNREFERENCED_PARAMETER(clrBg);
    if (nDist <= 0 || nBlur <= 0 || str.IsEmpty() || !pDC)
        return;

    if (scaleX < 0.01f) scaleX = 0.01f;
    if (scaleY < 0.01f) scaleY = 0.01f;

    const double rad = nSD * 3.14159265358979323846 / 180.0;
    const int ox = (int)floor(nDist * cos(rad) / scaleX + 0.5);
    const int oy = (int)floor(nDist * sin(rad) / scaleY + 0.5);
    const int pad = max(3, nBlur + 2);

    CSize sz = pDC->GetTextExtent(str);
    LOGFONT lf = {};
    if (CFont* pCF = pDC->GetCurrentFont())
        pCF->GetLogFont(&lf);
    const int italicMargin = lf.lfItalic ? (abs(lf.lfHeight) / 2) : 0;
    const int textW = max(rect.Width(), sz.cx + italicMargin);
    const int textH = max(rect.Height(), sz.cy);

    const int bw = textW + abs(ox) + pad * 2;
    const int bh = textH + abs(oy) + pad * 2;
    if (bw <= 0 || bh <= 0) return;

    // 影 DIB は必要サイズまで拡張して再利用(毎描画の CreateDIBSection を抑制)
    struct ShadowDibCache {
        HBITMAP hDib = NULL;
        void* pBits = nullptr;
        int capW = 0;
        int capH = 0;
        void Release()
        {
            if (hDib) { ::DeleteObject(hDib); hDib = NULL; }
            pBits = nullptr;
            capW = capH = 0;
        }
        BOOL Ensure(HDC hdcRef, int w, int h)
        {
            if (w <= 0 || h <= 0) return FALSE;
            if (hDib && pBits && capW >= w && capH >= h) return TRUE;
            Release();
            hDib = CCC_CreateShadowDib32(hdcRef, w, h, &pBits);
            if (!hDib || !pBits) { Release(); return FALSE; }
            capW = w;
            capH = h;
            return TRUE;
        }
    };
    static ShadowDibCache s_shadowCache;
    if (!s_shadowCache.Ensure(pDC->GetSafeHdc(), bw, bh))
        return;

    HBITMAP hDib = s_shadowCache.hDib;
    void* pBits = s_shadowCache.pBits;

    CDC dcShadow;
    dcShadow.CreateCompatibleDC(pDC);
    HGDIOBJ hOldBmp = ::SelectObject(dcShadow.GetSafeHdc(), hDib);

    UINT32* px = (UINT32*)pBits;
    const int nPx = bw * bh;
    // キャッシュは cap より大きいことがあるので使用矩形だけクリア
    for (int y = 0; y < bh; ++y) {
        UINT32* row = px + y * s_shadowCache.capW;
        for (int x = 0; x < bw; ++x)
            row[x] = 0x00FFFFFFu;
    }

    CFont* pOldFont = dcShadow.SelectObject(pDC->GetCurrentFont());
    dcShadow.SetBkMode(TRANSPARENT);
    dcShadow.SetTextColor(RGB(0, 0, 0));

    CRect tr(pad + max(0, ox), pad + max(0, oy),
        pad + max(0, ox) + textW, pad + max(0, oy) + textH);
    dcShadow.DrawText(str, &tr, fmt);

    // alpha/tmp は容量拡張して再利用（毎描画 std::vector new/delete で断片化しない）
    static BYTE* s_alpha = nullptr;
    static BYTE* s_tmp = nullptr;
    static int s_cap = 0;
    if (nPx > s_cap || !s_alpha || !s_tmp) {
        int cap = (s_cap > 0) ? s_cap : 4096;
        while (cap < nPx) {
            if (cap > (INT_MAX / 2)) { cap = nPx; break; }
            cap *= 2;
        }
        BYTE* na = (BYTE*)malloc((size_t)cap);
        BYTE* nt = (BYTE*)malloc((size_t)cap);
        if (!na || !nt) {
            free(na);
            free(nt);
            dcShadow.SelectObject(pOldFont);
            ::SelectObject(dcShadow.GetSafeHdc(), hOldBmp);
            return;
        }
        free(s_alpha);
        free(s_tmp);
        s_alpha = na;
        s_tmp = nt;
        s_cap = cap;
    }
    BYTE* alpha = s_alpha;
    for (int y = 0; y < bh; ++y) {
        UINT32* row = px + y * s_shadowCache.capW;
        BYTE* arow = alpha + y * bw;
        for (int x = 0; x < bw; ++x) {
            const UINT32 rgb = row[x] & 0x00FFFFFFu;
            if (rgb >= 0x00FEFEFEu)
                arow[x] = 0;
            else
                arow[x] = (BYTE)max(0, min(255, 255 - (int)GetRValue(rgb)));
        }
    }

    const int blurR = max(1, (nBlur + 1) / 2);
    CCC_BoxBlurAlpha(alpha, s_tmp, bw, bh, blurR);
    if (nBlur >= 5)
        CCC_BoxBlurAlpha(alpha, s_tmp, bw, bh, max(1, blurR / 2));

    const int tintR = (GetRValue(clrS) * 3 + 32) / 4;
    const int tintG = (GetGValue(clrS) * 3 + 28) / 4;
    const int tintB = (GetBValue(clrS) * 3 + 40) / 4;
    const int peakA = bAeroTrans ? 88 : 112; // 透過時は薄く（それでも呼び出し側は省略推奨）

    for (int y = 0; y < bh; ++y) {
        UINT32* row = px + y * s_shadowCache.capW;
        BYTE* arow = alpha + y * bw;
        for (int x = 0; x < bw; ++x) {
            if (arow[x] == 0) { row[x] = 0; continue; }
            const BYTE a = (BYTE)((arow[x] * peakA) / 255);
            if (a < 2) { row[x] = 0; continue; }
            row[x] = ((UINT32)a << 24) | ((UINT32)tintB << 16) | ((UINT32)tintG << 8) | (UINT32)tintR;
        }
    }

    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    // ストライドが bw と異なる場合があるので、行単位ではなく CompatibleDC 経由で
    // 左上 bw×bh をブレンド(DIB は top-down、先頭が (0,0))
    ::GdiAlphaBlend(pDC->GetSafeHdc(), rect.left - pad, rect.top - pad, bw, bh,
        dcShadow.GetSafeHdc(), 0, 0, bw, bh, bf);

    dcShadow.SelectObject(pOldFont);
    ::SelectObject(dcShadow.GetSafeHdc(), hOldBmp);
    // hDib はキャッシュ保持のため Delete しない
}

// 影付き単色文字。bAeroTrans 時はソフトシャドウ省略（クロマで黒縁になる）。
static void DrawTextWithShadow(CDC* pDC, const CRect& rect, const CString& str, UINT fmt, COLORREF clrT, COLORREF clrS, int nSD, int nDist, int nBlur, BOOL bSE, COLORREF clrBg, BOOL bAeroTrans = FALSE)
{
    // アクリル透過は最終段がクロマキー(α=0/255)のため、ソフトシャドウの
    // 半透明を memDC に焼き込むと黒縁のジャギーになる。透過時は影を省略。
    if (bSE && !bAeroTrans)
        DrawTextShadow(pDC, rect, str, fmt, clrS, nSD, nDist, nBlur, clrBg, bAeroTrans);
    pDC->SetTextColor(clrT);
    CRect rt = rect;
    pDC->DrawText(str, rt, fmt);
}

// グラデ文字。4px スライスでクリップ描画。透過時は影省略。斜めは 45/135/225/315。
static void DrawTextWithGradient(CDC* pDC, const CRect& rect, const CString& str, UINT fmt, COLORREF cS, COLORREF cE, int nDir, COLORREF clrSh, int nSD, int nDist, int nBlur, BOOL bSE, COLORREF clrBg, int nActW = -1, BOOL bFB = FALSE, BOOL bAeroTrans = FALSE)
{
    if (str.IsEmpty()) return;

    // 透過時はソフトシャドウを省略(クロマキー段で半透明が黒縁になるため)
    if (bSE && !bAeroTrans)
        DrawTextShadow(pDC, rect, str, fmt, clrSh, nSD, nDist, nBlur, clrBg, bAeroTrans);

    CSize sz = pDC->GetTextExtent(str);
    int nW = (nActW > 0) ? nActW : sz.cx;
    int nH = sz.cy;

    LOGFONT lf;
    pDC->GetCurrentFont()->GetLogFont(&lf);
    int im = lf.lfItalic ? (abs(lf.lfHeight) / 2) : 0;

    CRect ga = rect;
    if (!bFB)
    {
        if (fmt & DT_CENTER) ga.left = rect.left + (rect.Width() - nW) / 2;
        else if (fmt & DT_RIGHT) ga.left = rect.right - nW;
        ga.right = ga.left + nW + im;
    }

    int nd = nDir % 360;
    if (nd < 0) nd += 360;

    pDC->SetBkMode(TRANSPARENT);
    const int kB = 4; // グラデスライス幅 px（細すぎると重い）

    auto doSlice = [&](CRect sl)
    {
        CRgn rgn;
        rgn.CreateRectRgnIndirect(&sl);
        pDC->SelectClipRgn(&rgn);
        CRect dr = rect;
        dr.right += im;
        pDC->DrawText(str, dr, fmt);
        pDC->SelectClipRgn(NULL);
    };

    auto setClr = [&](double ratio)
    {
        int r = GetRValue(cS) + (int)((GetRValue(cE) - GetRValue(cS)) * ratio);
        int g = GetGValue(cS) + (int)((GetGValue(cE) - GetGValue(cS)) * ratio);
        int b = GetBValue(cS) + (int)((GetBValue(cE) - GetBValue(cS)) * ratio);
        pDC->SetTextColor(RGB(r, g, b));
    };

    if (nd == 45 || nd == 135 || nd == 225 || nd == 315)
    {
        int diag = (int)sqrt((double)(ga.Width() * ga.Width() + nH * nH));
        if (diag <= 0) diag = 1;

        for (int i = 0; i < diag; i += kB)
        {
            setClr((double)i / diag);
            int x = ga.left + (int)(ga.Width() * (double)i / max(1, diag - 1));
            CRect sl = rect;
            sl.left = x;
            sl.right = min(x + kB, rect.right);
            doSlice(sl);
        }
    }
    else if ((nd >= 90 && nd < 180) || (nd >= 270 && nd < 360))
    {
        int tot = ga.Width();
        if (tot <= 0) tot = 1;
        BOOL ltr = (nd >= 90 && nd < 180);

        for (int i = 0; i < tot; i += kB)
        {
            setClr(ltr ? (double)i / tot : 1.0 - (double)i / tot);
            CRect sl = rect;
            sl.left = ga.left + i;
            sl.right = min(sl.left + kB, ga.right);
            doSlice(sl);
        }
    }
    else
    {
        int tot = nH;
        if (tot <= 0) tot = 1;
        BOOL b2t = (nd >= 0 && nd < 90);

        for (int i = 0; i < tot; i += kB)
        {
            setClr(b2t ? (double)i / tot : 1.0 - (double)i / tot);
            CRect sl = rect;
            sl.top = b2t ? (rect.bottom - i - kB) : (rect.top + i);
            sl.bottom = sl.top + kB;
            doSlice(sl);
        }
    }
}

// 色を暗くする(縁取り・陰影用)。pct は残す明るさの割合(0..100)。
static COLORREF CCC_Darken(COLORREF c, int pct)
{
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    return RGB(GetRValue(c) * pct / 100, GetGValue(c) * pct / 100, GetBValue(c) * pct / 100);
}

// 色を白方向に明るくする。pct は白へ寄せる割合(0..100)。
static COLORREF CCC_Lighten(COLORREF c, int pct)
{
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    return RGB(
        GetRValue(c) + (255 - GetRValue(c)) * pct / 100,
        GetGValue(c) + (255 - GetGValue(c)) * pct / 100,
        GetBValue(c) + (255 - GetBValue(c)) * pct / 100);
}

// 彩度を落とす(無効表示用)。pct はグレー(輝度)へ寄せる割合(0..100)。
// 色みをほのかに残したまま「無効」を伝えつつ、カスタム描画の質感は保つ。
static COLORREF CCC_Desaturate(COLORREF c, int pct)
{
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    int y = (r * 30 + g * 59 + b * 11) / 100;   // 知覚輝度
    r += (y - r) * pct / 100;
    g += (y - g) * pct / 100;
    b += (y - b) * pct / 100;
    return RGB(r, g, b);
}

// 不透明なシェイプの上にのせる白い濡れツヤのハイライト(べた塗りなのでクロマ透過でも安全)
static void DrawShine(CDC* pDC, int cx, int cy, int rx, int ry, COLORREF c = RGB(255, 255, 255))
{
    if (!pDC) return;
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    CBrush b(c);
    CBrush* ob = pDC->SelectObject(&b);
    CGdiObject* op = pDC->SelectStockObject(NULL_PEN);
    pDC->Ellipse(cx - rx, cy - ry, cx + rx, cy + ry);
    if (op) pDC->SelectObject(op);
    pDC->SelectObject(ob);
}

// ハート（8点ポリゴン）。リスト行デコ・淫女ハート目。べた塗りなのでクロマでも安全。
static void DrawHeart(CDC* pDC, CRect rc, COLORREF c)
{
    CBrush br(c);
    CPen p(PS_SOLID, 1, CCC_Darken(c, 68)); // 濃いローズで縁取りして色っぽく
    CBrush* ob = pDC->SelectObject(&br);
    CPen* op = pDC->SelectObject(&p);

    int cx = rc.CenterPoint().x;
    int cy = rc.CenterPoint().y;
    int w = rc.Width() / 2;
    if (w < 2) w = 2;

    POINT pts[8] = {
        {cx, cy + w}, {cx - w, cy - w / 3}, {cx - w, cy - w}, {cx, cy - w / 2},
        {cx, cy - w / 2}, {cx + w, cy - w}, {cx + w, cy - w / 3}, {cx, cy + w}
    };
    pDC->Polygon(pts, 8);

    // 左ローブにぷるんとした濡れツヤ
    if (w >= 3)
        DrawShine(pDC, cx - w / 2, cy - w / 3, max(1, w / 4), max(1, w / 3));

    pDC->SelectObject(ob);
    pDC->SelectObject(op);
}

// 4本十字の星。選択行のアクセント。線幅2。
static void DrawStar(CDC* pDC, int cx, int cy, int sz, COLORREF c)
{
    CPen p(PS_SOLID, 2, c);
    CPen* op = pDC->SelectObject(&p);

    pDC->MoveTo(cx, cy - sz);
    pDC->LineTo(cx, cy + sz);
    pDC->MoveTo(cx - sz, cy);
    pDC->LineTo(cx + sz, cy);
    pDC->MoveTo(cx - sz * 7 / 10, cy - sz * 7 / 10);
    pDC->LineTo(cx + sz * 7 / 10, cy + sz * 7 / 10);
    pDC->MoveTo(cx + sz * 7 / 10, cy - sz * 7 / 10);
    pDC->LineTo(cx - sz * 7 / 10, cy + sz * 7 / 10);

    pDC->SelectObject(op);
}

// 音符（玉+符幹+旗）。ボタン装飾用。
static void DrawMusicNote(CDC* pDC, CRect rc, COLORREF c)
{
    CBrush br(c);
    CPen p(PS_SOLID, 2, c);
    CBrush* ob = pDC->SelectObject(&br);
    CPen* op = pDC->SelectObject(&p);

    int cx = rc.CenterPoint().x;
    int cy = rc.CenterPoint().y;
    int h = rc.Height() * 6 / 10;
    int w = rc.Width() / 3;

    CRect rn(cx - w / 2, cy + h / 4, cx + w / 2, cy + h / 4 + w);
    pDC->Ellipse(&rn);
    pDC->MoveTo(cx + w / 2, cy + h / 4 + w / 2);
    pDC->LineTo(cx + w / 2, cy - h / 2);

    CPoint pts[4] = {
        {cx + w / 2, cy - h / 2}, {cx + w / 2 + w, cy - h / 4},
        {cx + w / 2 + w, cy}, {cx + w / 2, cy + h / 8}
    };
    pDC->SelectObject(GetStockObject(NULL_PEN));
    pDC->Polygon(pts, 4);

    // 音符の玉に濡れツヤ
    DrawShine(pDC, cx - w / 4, cy + h / 4 + w / 4, max(1, w / 5), max(1, w / 4));

    pDC->SelectObject(ob);
    pDC->SelectObject(op);
}

// ダイヤ（外枠クリスタル+内側色）。リスト/ボタンの宝石デコ。
static void DrawDiamond(CDC* pDC, CRect rc, COLORREF c)
{
    int cx = rc.CenterPoint().x;
    int cy = rc.CenterPoint().y;
    int w = rc.Width() / 2;
    int h = rc.Height() / 2;

    CPoint pts[4] = { {cx, cy - h}, {cx + w, cy}, {cx, cy + h}, {cx - w, cy} };
    CBrush bO(RGB(255, 214, 232)); // ローズクリスタル
    CPen pO(PS_SOLID, 1, RGB(232, 120, 170));
    CBrush* ob = pDC->SelectObject(&bO);
    CPen* op = pDC->SelectObject(&pO);
    pDC->Polygon(pts, 4);

    CPoint pi[4] = {
        {cx, cy - h * 6 / 10}, {cx + w * 6 / 10, cy},
        {cx, cy + h * 6 / 10}, {cx - w * 6 / 10, cy}
    };
    CBrush bI(c);
    pDC->SelectObject(&bI);
    pDC->Polygon(pi, 4);

    // 上面の濡れツヤ + 小さなきらめき
    CBrush bH(RGB(255, 255, 255));
    pDC->SelectObject(&bH);
    pDC->Ellipse(cx - max(2, w / 4), cy - h / 2, cx + 1, cy - h / 6);
    pDC->Ellipse(cx - 1, cy - 2, cx + 2, cy + 2);
    pDC->SelectObject(ob);
    pDC->SelectObject(op);

    CPen pL(PS_SOLID, 1, RGB(255, 255, 200));
    pDC->SelectObject(&pL);
    pDC->MoveTo(cx, cy - h - 3);
    pDC->LineTo(cx, cy - h - 6);
    pDC->MoveTo(cx, cy + h + 3);
    pDC->LineTo(cx, cy + h + 6);
    pDC->MoveTo(cx - w - 3, cy);
    pDC->LineTo(cx - w - 6, cy);
    pDC->MoveTo(cx + w + 3, cy);
    pDC->LineTo(cx + w + 6, cy);
    pDC->SelectObject(op);
}

// 王冠（7頂点+3宝石）。選択強調用。
static void DrawCrown(CDC* pDC, int cx, int cy, int sz, COLORREF c)
{
    CBrush br(c);
    CPen pen(PS_SOLID, 1, RGB(255, 215, 0));
    CBrush* ob = pDC->SelectObject(&br);
    CPen* op = pDC->SelectObject(&pen);

    CPoint pts[8] = {
        {cx - sz, cy + sz / 2}, {cx - sz * 2 / 3, cy - sz / 2}, {cx - sz / 3, cy},
        {cx, cy - sz}, {cx + sz / 3, cy}, {cx + sz * 2 / 3, cy - sz / 2},
        {cx + sz, cy + sz / 2}, {cx - sz, cy + sz / 2}
    };
    pDC->Polygon(pts, 7);

    CBrush bJ(COLOR_HEART); // 宝石はローズハート色で色っぽく
    pDC->SelectObject(&bJ);
    pDC->Ellipse(cx - 2, cy - sz - 2, cx + 2, cy - sz + 2);
    pDC->Ellipse(cx - sz * 2 / 3 - 2, cy - sz / 2 - 2, cx - sz * 2 / 3 + 2, cy - sz / 2 + 2);
    pDC->Ellipse(cx + sz * 2 / 3 - 2, cy - sz / 2 - 2, cx + sz * 2 / 3 + 2, cy - sz / 2 + 2);

    // 王冠の帯に濡れツヤ
    DrawShine(pDC, cx - sz / 3, cy + sz / 6, max(1, sz / 5), max(1, sz / 7));

    pDC->SelectObject(ob);
    pDC->SelectObject(op);
}

// 点々のレース線。リスト行の区切り。8px 間隔で交互にオフセット。
static void DrawLaceLine(CDC* pDC, int x1, int y1, int x2, int y2, COLORREF c)
{
    CPen p(PS_SOLID, 1, c);
    CPen* op = pDC->SelectObject(&p);
    int dx = x2 - x1, dy = y2 - y1;
    int steps = max(abs(dx), abs(dy)) / 8;
    if (steps < 2) steps = 2;

    for (int i = 0; i <= steps; i++)
    {
        int x = x1 + dx * i / steps;
        int y = y1 + dy * i / steps;
        int wv = (i % 2 == 0) ? 2 : -2;
        if (abs(dx) > abs(dy)) pDC->Ellipse(x - 2, y + wv - 2, x + 2, y + wv + 2);
        else pDC->Ellipse(x + wv - 2, y - 2, x + wv + 2, y + 2);
    }
    pDC->SelectObject(op);
}

// リボン（中央帯+左右ループ）。Edit 枠の脇デコなど。
static void DrawRibbon(CDC* pDC, CRect rc, COLORREF c)
{
    CBrush br(c);
    CPen pen(PS_SOLID, 1, CCC_Darken(c, 70));
    CBrush* ob = pDC->SelectObject(&br);
    CPen* op = pDC->SelectObject(&pen);

    int cx = rc.CenterPoint().x;
    int cy = rc.CenterPoint().y;
    int w = rc.Width() / 2;
    int h = rc.Height() / 2;

    CRect rC(cx - w, cy - h / 3, cx + w, cy + h / 3);
    pDC->RoundRect(&rC, CPoint(h / 2, h / 2));
    CRect rL(cx - w / 3, cy - h, cx, cy + h);
    pDC->Ellipse(&rL);
    CRect rR(cx, cy - h, cx + w / 3, cy + h);
    pDC->Ellipse(&rR);

    // 左右の輪に濡れツヤ
    if (w >= 4 && h >= 3)
    {
        DrawShine(pDC, cx - w / 6, cy - h / 3, max(1, w / 8), max(1, h / 3));
        DrawShine(pDC, cx + w / 6, cy - h / 3, max(1, w / 8), max(1, h / 3));
    }

    pDC->SelectObject(ob);
    pDC->SelectObject(op);
}

// 5弁の花。リスト行アイコンの一種。
static void DrawFlower(CDC* pDC, int cx, int cy, int sz, COLORREF c)
{
    CBrush br(c);
    CPen pen(PS_SOLID, 1, CCC_Darken(c, 80));
    CBrush* ob = pDC->SelectObject(&br);
    CPen* op = pDC->SelectObject(&pen);

    for (int i = 0; i < 5; i++)
    {
        double a = i * 2.0 * 3.14159 / 5.0;
        int px = cx + (int)(sz * 0.6 * cos(a));
        int py = cy + (int)(sz * 0.6 * sin(a));
        pDC->Ellipse(px - sz / 3, py - sz / 3, px + sz / 3, py + sz / 3);
        // 各花びらに小さなツヤ
        if (sz >= 6)
            DrawShine(pDC, px - sz / 8, py - sz / 8, max(1, sz / 10), max(1, sz / 10));
    }
    CBrush bC(RGB(255, 220, 120));
    pDC->SelectObject(&bC);
    pDC->SelectStockObject(NULL_PEN);
    pDC->Ellipse(cx - sz / 4, cy - sz / 4, cx + sz / 4, cy + sz / 4);
    // 中心にツヤ
    DrawShine(pDC, cx - sz / 10, cy - sz / 10, max(1, sz / 10), max(1, sz / 10));

    pDC->SelectObject(ob);
    pDC->SelectObject(op);
}

// はなまる（8弁+中心ハート）。達成/選択のデコ。cC=芯 cP=花弁。
static void DrawHanamaru(CDC* pDC, CRect rc, COLORREF cC, COLORREF cP)
{
    int cx = rc.CenterPoint().x;
    int cy = rc.CenterPoint().y;
    int radius = min(rc.Width(), rc.Height()) / 2 - 2;
    if (radius < 3) return;

    CBrush bP(cP);
    CPen pP(PS_SOLID, 1, RGB(255, 140, 180));
    CBrush* ob = pDC->SelectObject(&bP);
    CPen* op = pDC->SelectObject(&pP);

    const int nP = 8;
    const double as = 2.0 * 3.14159265358979323846 / nP;

    for (int i = 0; i < nP; i++)
    {
        double a = as * i;
        int px = cx + (int)(radius * 0.65 * cos(a));
        int py = cy + (int)(radius * 0.65 * sin(a));
        int ps = (int)(radius / 2.5);
        CRect rp(px - ps, py - ps, px + ps, py + ps);
        pDC->Ellipse(&rp);
    }

    CBrush bM(RGB(255, 150, 180));
    pDC->SelectObject(&bM);
    for (int i = 0; i < nP; i++)
    {
        double a = as * i + as / 2.0;
        int px = cx + (int)(radius * 0.45 * cos(a));
        int py = cy + (int)(radius * 0.45 * sin(a));
        int ps = radius / 4;
        CRect rp(px - ps, py - ps, px + ps, py + ps);
        pDC->Ellipse(&rp);
    }

    CBrush bCO(RGB(255, 120, 160));
    pDC->SelectObject(&bCO);
    int or2 = radius / 2;
    CRect rO(cx - or2, cy - or2, cx + or2, cy + or2);
    pDC->Ellipse(&rO);

    CBrush bCI(cC);
    pDC->SelectObject(&bCI);
    int ir = radius / 3;
    CRect rI(cx - ir, cy - ir, cx + ir, cy + ir);
    pDC->Ellipse(&rI);

    CBrush bOr(RGB(255, 140, 80));
    CPen pOr(PS_SOLID, 1, RGB(255, 100, 50));
    pDC->SelectObject(&bOr);
    pDC->SelectObject(&pOr);
    int sr = radius / 6;
    CRect rS(cx - sr, cy - sr, cx + sr, cy + sr);
    pDC->Ellipse(&rS);

    // ぽっと頬染め(ほっぺ)でエロ可愛い表情に + 中心に小さなハート
    if (radius >= 6)
    {
        int cr = max(2, radius / 5);
        DrawShine(pDC, cx - radius / 4, cy + radius / 4, cr, max(1, cr * 3 / 4), RGB(255, 150, 185));
        DrawShine(pDC, cx + radius / 4, cy + radius / 4, cr, max(1, cr * 3 / 4), RGB(255, 150, 185));
    }
    DrawHeart(pDC, CRect(cx - sr, cy - sr - 1, cx + sr, cy + sr - 1), COLOR_HEART_DEEP);

    // 外側リングの上面に濡れツヤ
    DrawShine(pDC, cx - radius / 3, cy - radius / 2, max(1, radius / 5), max(1, radius / 7));

    pDC->SelectObject(ob);
    pDC->SelectObject(op);

    DrawStar(pDC, cx - radius * 8 / 10, cy - radius * 8 / 10, 2, RGB(255, 140, 180));
    DrawStar(pDC, cx + radius * 8 / 10, cy - radius * 8 / 10, 2, RGB(255, 140, 180));
    DrawStar(pDC, cx - radius * 8 / 10, cy + radius * 8 / 10, 2, RGB(255, 140, 180));
    DrawStar(pDC, cx + radius * 8 / 10, cy + radius * 8 / 10, 2, RGB(255, 140, 180));
    DrawStar(pDC, cx, cy - radius * 9 / 10, 1, RGB(255, 180, 200));
    DrawStar(pDC, cx, cy + radius * 9 / 10, 1, RGB(255, 180, 200));
    DrawStar(pDC, cx - radius * 9 / 10, cy, 1, RGB(255, 180, 200));
    DrawStar(pDC, cx + radius * 9 / 10, cy, 1, RGB(255, 180, 200));
}

static void DrawSoftJkChip(CDC* pDC, const CRect& rc, int animTick, BOOL hot);

// ボタン角のつる+小さな花。bPA=対角ペアの向き。押下時は 1px オフセット。
static void DrawDecorations(CDC* pDC, CRect rect, BOOL bPA, BOOL bPushed)
{
    CPen pV(PS_SOLID, 1, COLOR_VINE_DECO);
    CBrush bF(COLOR_HEART);
    CBrush bC(RGB(255, 255, 0));
    CPen* op = pDC->SelectObject(&pV);
    CBrush* ob = pDC->SelectObject(&bF);

    int ofs = bPushed ? 1 : 0;
    rect.DeflateRect(2, 2);

    struct C { int x, y, dx, dy; };
    C corners[2];
    int nCorners = 0;

    if (bPA)
    {
        corners[0] = { rect.left + ofs, rect.top + ofs, 1, 1 };
        corners[1] = { rect.right - 1 + ofs, rect.bottom - 1 + ofs, -1, -1 };
        nCorners = 2;
    }
    else
    {
        corners[0] = { rect.right - 1 + ofs, rect.top + ofs, -1, 1 };
        corners[1] = { rect.left + ofs, rect.bottom - 1 + ofs, 1, -1 };
        nCorners = 2;
    }

    const int softTick = (int)(::GetTickCount64() / 220);
    for (int ci = 0; ci < nCorners; ++ci)
    {
        C& c = corners[ci];
        CPoint pts[4] = {
            {c.x, c.y + 12 * c.dy},
            {c.x + 4 * c.dx, c.y + 4 * c.dy},
            {c.x + 4 * c.dx, c.y + 4 * c.dy},
            {c.x + 12 * c.dx, c.y}
        };
        pDC->PolyBezier(pts, 4);

        int r = 2;
        int fx = c.x + 4 * c.dx;
        int fy = c.y + 4 * c.dy;

        pDC->SelectObject(&bF);
        pDC->SelectObject(GetStockObject(NULL_PEN));
        pDC->Ellipse(fx - r, fy - r * 2, fx + r, fy);
        pDC->Ellipse(fx - r, fy, fx + r, fy + r * 2);
        pDC->Ellipse(fx - r * 2, fy - r, fx, fy + r);
        pDC->Ellipse(fx, fy - r, fx + r * 2, fy + r);

        pDC->SelectObject(&bC);
        pDC->Ellipse(fx - 1, fy - 1, fx + 1, fy + 1);
        pDC->SelectObject(&pV);

        // Soft3D の小さな宝石チップを角花に紛れ込ませる（前面を奪わない）
        DrawSoftJkChip(pDC, CRect(fx - 5, fy - 5, fx + 5, fy + 5), softTick + ci * 5, bPushed);
    }
    pDC->SelectObject(op);
    pDC->SelectObject(ob);
}

// ラベルが複数行か。フィット縮小の経路分岐。
static BOOL CCC_TextHasBreak(const CString& str)
{
    return str.Find(_T('\n')) >= 0 || str.Find(_T('\r')) >= 0;
}

// フィット用の必要サイズ。改行ありは DT_CALCRECT、なしは GetTextExtent。
static CSize CCC_MeasureFitText(CDC* pDC, const CString& str, BOOL hasBreak)
{
    if (!pDC)
        return CSize(0, 0);
    if (hasBreak) {
        CRect mc(0, 0, 32767, 32767); // DT_CALCRECT 用の十分広い仮矩形
        pDC->DrawText(str, &mc, DT_CALCRECT | DT_LEFT | DT_TOP | DT_NOPREFIX);
        return CSize((std::max)(0, mc.Width()), (std::max)(0, mc.Height()));
    }
    return pDC->GetTextExtent(str);
}

// 改行なし: 横縮小のみ。改行あり: 先に2〜3行として出してから最長行を横縮小。
// 半分未満は呼び出し側で3段階ボタンへ。描画の下限は 0.50。
static void DrawFitControlText(CDC* pDC, CRect rc, const CString& str, UINT fmt, float minScaleX = 0.50f)
{
    if (!pDC || str.IsEmpty() || rc.Width() <= 0 || rc.Height() <= 0)
        return;

    const BOOL hasBreak = CCC_TextHasBreak(str);
    UINT baseFmt = fmt & ~(DT_END_ELLIPSIS | DT_PATH_ELLIPSIS | DT_WORD_ELLIPSIS);
    if (hasBreak) {
        baseFmt &= ~DT_SINGLELINE;
        baseFmt |= DT_NOPREFIX;
    } else {
        baseFmt |= DT_SINGLELINE;
        baseFmt &= ~DT_WORDBREAK;
        baseFmt |= DT_NOPREFIX;
    }

    CFont* pCur = pDC->GetCurrentFont();
    LOGFONT lf = {};
    if (pCur)
        pCur->GetLogFont(&lf);
    long tH = abs(lf.lfHeight);
    if (tH <= 0)
        tH = 12;

    CFont shrinkFont;
    CFont* pOldFont = nullptr;
    auto measure = [&]() -> CSize {
        return CCC_MeasureFitText(pDC, str, hasBreak);
    };

    CSize need = measure();
    const int availW = (std::max)(1, rc.Width());
    const int availH = (std::max)(1, rc.Height());

    if (hasBreak && need.cy > availH && tH > 6) {
        while (tH > 6 && need.cy > availH) {
            tH--;
            lf.lfHeight = -tH;
            if (shrinkFont.GetSafeHandle())
                shrinkFont.DeleteObject();
            if (!shrinkFont.CreateFontIndirect(&lf))
                break;
            if (!pOldFont)
                pOldFont = pDC->SelectObject(&shrinkFont);
            else
                pDC->SelectObject(&shrinkFont);
            need = measure();
        }
    }

    float scaleX = 1.0f;
    if (need.cx > availW)
        scaleX = (float)availW / (float)need.cx;
    if (scaleX > 1.0f)
        scaleX = 1.0f;
    if (scaleX < minScaleX)
        scaleX = minScaleX;
    if (scaleX < 0.99f)
        scaleX *= 0.98f; // 右端クリップ余裕

    int yTop = rc.top;
    if ((fmt & DT_VCENTER) && need.cy < availH)
        yTop = rc.top + (availH - need.cy) / 2;
    else if (fmt & DT_BOTTOM)
        yTop = rc.bottom - need.cy;
    if (yTop < rc.top)
        yTop = rc.top;

    const UINT align = fmt & (DT_CENTER | DT_RIGHT | DT_LEFT);
    auto restoreFont = [&]() {
        if (pOldFont)
            pDC->SelectObject(pOldFont);
    };

    UINT placeFmt = (baseFmt & ~(DT_VCENTER | DT_BOTTOM)) | DT_TOP;
    if (!hasBreak)
        placeFmt |= DT_SINGLELINE;

    if (scaleX >= 0.99f) {
        CRect rd(rc.left, yTop, rc.right, (std::max)(rc.bottom, yTop + need.cy));
        pDC->DrawText(str, &rd, placeFmt);
        restoreFont();
        return;
    }

    if (!pDC->SaveDC()) {
        pDC->DrawText(str, &rc, placeFmt);
        restoreFont();
        return;
    }

    pDC->IntersectClipRect(rc);
    pDC->SetGraphicsMode(GM_ADVANCED);
    const float scaledW = (float)need.cx * scaleX;
    float tx = (float)rc.left;
    if (align & DT_RIGHT)
        tx = (float)rc.right - scaledW;
    else if (align & DT_CENTER)
        tx = (float)rc.left + ((float)availW - scaledW) * 0.5f;
    if (tx < (float)rc.left)
        tx = (float)rc.left;

    XFORM xf = { scaleX, 0.0f, 0.0f, 1.0f, tx, (float)yTop };
    pDC->SetWorldTransform(&xf);
    CRect rl(0, 0, (std::max)(1, (int)need.cx), (std::max)(1, (int)need.cy));
    UINT drawFmt = (placeFmt & ~(DT_CENTER | DT_RIGHT)) | DT_LEFT;
    pDC->DrawText(str, &rl, drawFmt);
    pDC->RestoreDC(-1);
    restoreFont();
}

// ボタン中央テキスト。無効は灰、押下は 1px ずらしてフィット縮小（下限 0.50）。
static void DrawSmartText(CDC* pDC, CRect rect, CString str, BOOL bDis, BOOL bPushed)
{
    if (str.IsEmpty()) return;

    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(bDis ? RGB(128, 128, 128) : COLOR_EDIT_TEXT);

    CRect rt = rect;
    rt.DeflateRect(1, 1);
    if (bPushed) rt.OffsetRect(1, 1);

    DrawFitControlText(pDC, rt, str, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, 0.50f);
}

// ラベル本文。収まるなら影/グラデ直描き。足りなければ WorldTransform で X（必要なら Y）縮小。
// アクリル透過時は半透明影を焼かない（クロマで黒縁になる）。SaveDC 失敗時は省略記号。
static void DrawFittedText(CDC& dc, const CRect& rect, const CString& str, UINT fmt,
    BOOL bGrad, COLORREF cGS, COLORREF cGE, int nDir,
    COLORREF clrSh, int nSD, int nDist, int nBlur, BOOL bSE, COLORREF clrBg, BOOL bPreferWide,
    BOOL bAeroTrans = FALSE, UINT dpi = 96)
{
    if (str.IsEmpty() || rect.Width() <= 0 || rect.Height() <= 0) return;
    dc.SetBkMode(TRANSPARENT);
    CSize sz = dc.GetTextExtent(str);
    if (sz.cx <= 0) sz.cx = 1;

    TEXTMETRIC tm = {};
    dc.GetTextMetrics(&tm);
    const int nTextH = max(1, (int)tm.tmHeight);

    int shadowPadX = 0;
    int shadowPadY = 0;
    CCC_ComputeShadowPad(nSD, nDist, nBlur, bSE, dpi, shadowPadX, shadowPadY);

    CRect rectDraw = rect;
    if (bSE && shadowPadX > 0)
        rectDraw.right = (std::max)(rectDraw.left + 1, rectDraw.right - shadowPadX);

    LOGFONT lfCur = {};
    if (CFont* pCF = dc.GetCurrentFont())
        pCF->GetLogFont(&lfCur);
    const int italicMargin = lfCur.lfItalic ? (abs(lfCur.lfHeight) / 2) : 0;

    const int sidePad = max(1, CCC_ScaleDpi(3, dpi));
    const int nBudgetW = (std::max)(1, rectDraw.Width() - sidePad);
    const int nBudgetH = (std::max)(1, rect.Height());
    const int needW = sz.cx + italicMargin;

    const bool fitsWithoutScale = (needW <= nBudgetW && nTextH <= nBudgetH);

    if (fitsWithoutScale)
    {
        if (bGrad) DrawTextWithGradient(&dc, rectDraw, str, fmt, cGS, cGE, nDir, clrSh, nSD, nDist, nBlur, bSE, clrBg, sz.cx, bPreferWide, bAeroTrans);
        else DrawTextWithShadow(&dc, rectDraw, str, fmt, RGB(0, 0, 0), clrSh, nSD, nDist, nBlur, bSE, clrBg, bAeroTrans);
        return;
    }

    if (!dc.SaveDC())
    {
        CRect rd = rect;
        UINT ellFmt = fmt | DT_END_ELLIPSIS;
        if (bGrad) DrawTextWithGradient(&dc, rd, str, ellFmt, cGS, cGE, nDir, clrSh, nSD, nDist, nBlur, bSE, clrBg, sz.cx, FALSE, bAeroTrans);
        else DrawTextWithShadow(&dc, rd, str, ellFmt, RGB(0, 0, 0), clrSh, nSD, nDist, nBlur, bSE, clrBg, bAeroTrans);
        return;
    }

    // ワイド文字: X 軸のみ縮小（旧実装どおり）。Y は収まらないときだけ最小限縮める。
    float scaleX = 1.0f;
    if (needW > nBudgetW)
        scaleX = (float)nBudgetW / (float)needW;
    scaleX *= 0.98f; // 右端クリップ余裕
    if (scaleX < 0.50f) scaleX = 0.50f;
    if (scaleX > 1.0f) scaleX = 1.0f;

    float scaleY = 1.0f;
    if (nTextH > nBudgetH)
        scaleY = (float)nBudgetH / (float)nTextH;

    const int drawH = max(1, (int)(nTextH * scaleY + 0.5f));
    const int yTop = rect.top + max(0, (nBudgetH - drawH) / 2);

    dc.SetGraphicsMode(GM_ADVANCED);
    XFORM xf = { scaleX, 0.0f, 0.0f, scaleY, (float)rectDraw.left, (float)yTop };
    dc.SetWorldTransform(&xf);

    int mCW = (tm.tmMaxCharWidth > 0) ? (int)tm.tmMaxCharWidth : (int)tm.tmAveCharWidth;
    const int rlH = max(1, (scaleY > 0.01f) ? (int)(rectDraw.Height() / scaleY + 0.5f) : rectDraw.Height());
    CRect rl(0, 0, sz.cx + (std::max)(16, mCW + 4), rlH);
    const UINT fitFmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    if (bSE && !bAeroTrans)
        DrawTextShadow(&dc, rl, str, fitFmt, clrSh, nSD, nDist, nBlur, clrBg, bAeroTrans, scaleX, scaleY);
    if (bGrad) DrawTextWithGradient(&dc, rl, str, fitFmt, cGS, cGE, nDir, clrSh, nSD, nDist, nBlur, FALSE, clrBg, sz.cx, FALSE, bAeroTrans);
    else DrawTextWithShadow(&dc, rl, str, fitFmt, RGB(0, 0, 0), clrSh, nSD, nDist, nBlur, FALSE, clrBg, bAeroTrans);
    dc.RestoreDC(-1);
}

// 名前列/印列の [SAV]/[LRC]/[MONO]|[LR]|[2.1]…/[16ch]/[XG] を抜き出し、色付きチップ描画用に分離する。
static const int kCccExtraChips = 4;
static void CCC_ExtractSavLrc(CString& text, BOOL& bSav, BOOL& bLrc, CString extra[], int extraMax, int& extraN)
{
    bSav = FALSE;
    bLrc = FALSE;
    extraN = 0;
    if (extra && extraMax > 0) {
        for (int e = 0; e < extraMax; ++e)
            extra[e].Empty();
    }
    if (text.IsEmpty()) return;

    // 先頭の欠損印「⚠ 」は残す
    CString head;
    int i = 0;
    const int n = text.GetLength();
    while (i < n && text[i] != _T('[')) {
        head.AppendChar(text[i]);
        ++i;
    }
    CString rest = (i < n) ? text.Mid(i) : CString();
    for (;;) {
        if (rest.GetLength() >= 5 && rest.Left(5) == _T("[SAV]")) {
            bSav = TRUE;
            rest = rest.Mid(5);
            continue;
        }
        if (rest.GetLength() >= 5 && rest.Left(5) == _T("[LRC]")) {
            bLrc = TRUE;
            rest = rest.Mid(5);
            continue;
        }
        // [MONO] / [LR] / [2.1] / [16ch] / [XG] / [88P] …
        if (!rest.IsEmpty() && rest[0] == _T('[')) {
            const int end = rest.Find(_T(']'));
            if (end >= 2) {
                const CString inner = rest.Mid(1, end - 1);
                if (inner != _T("SAV") && inner != _T("LRC") && !inner.IsEmpty()) {
                    BOOL ok = TRUE;
                    for (int k = 0; k < inner.GetLength(); ++k) {
                        const TCHAR c = inner[k];
                        if (!((c >= _T('0') && c <= _T('9'))
                            || (c >= _T('A') && c <= _T('Z'))
                            || (c >= _T('a') && c <= _T('z'))
                            || c == _T('.'))) {
                            ok = FALSE;
                            break;
                        }
                    }
                    if (ok && extra && extraN < extraMax) {
                        extra[extraN++] = inner;
                        rest = rest.Mid(end + 1);
                        continue;
                    }
                }
            }
        }
        if (!rest.IsEmpty() && (rest[0] == _T(' ') || rest[0] == _T('\t'))) {
            rest = rest.Mid(1);
            continue;
        }
        break;
    }
    // head 末尾の空白は1つ残して体裁を整える（⚠ の直後など）
    while (head.GetLength() > 1 && head[head.GetLength() - 1] == _T(' ')
        && head[head.GetLength() - 2] == _T(' '))
        head = head.Left(head.GetLength() - 1);
    text = head + rest;
}

// 印チップの地色。16/32ch=藤、MONO/LR/数値=薄荷、その他マップ=薄金。
static COLORREF CCC_MarkChipBg(const CString& lab)
{
    if (lab.CompareNoCase(_T("16ch")) == 0)
        return RGB(196, 186, 255);
    if (lab.CompareNoCase(_T("32ch")) == 0)
        return RGB(230, 176, 255);
    if (lab.CompareNoCase(_T("MONO")) == 0 || lab.CompareNoCase(_T("LR")) == 0)
        return RGB(186, 236, 210);
    BOOL audioNum = !lab.IsEmpty();
    for (int k = 0; k < lab.GetLength(); ++k) {
        const TCHAR c = lab[k];
        if (!((c >= _T('0') && c <= _T('9')) || c == _T('.'))) {
            audioNum = FALSE;
            break;
        }
    }
    if (audioNum)
        return RGB(186, 236, 210);
    return RGB(255, 224, 168);
}

// 小さな色タグ（アイコン代わり）。戻り=消費幅(余白込み)
static int CCC_DrawMarkChip(CDC* pDC, int x, int midY, LPCTSTR label,
    COLORREF bg, COLORREF fg, BOOL bOpaque)
{
    if (!pDC || !label || !label[0]) return 0;

    CFont* pCur = pDC->GetCurrentFont();
    LOGFONT lf = {};
    if (pCur) pCur->GetLogFont(&lf);
    const int base = lf.lfHeight ? abs(lf.lfHeight) : 12;
    lf.lfHeight = -max(8, (base * 76) / 100);
    lf.lfWeight = FW_BOLD;
    CFont fontChip;
    if (!fontChip.CreateFontIndirect(&lf))
        return 0;
    CFont* pOf = pDC->SelectObject(&fontChip);
    const CSize sz = pDC->GetTextExtent(label);
    const int padX = 4;
    const int padY = 1;
    int w = sz.cx + padX * 2;
    int h = sz.cy + padY * 2;
    if (w < 20) w = 20;
    if (h < 12) h = 12;
    if (h > 18) h = 18;
    CRect rc(x, midY - h / 2, x + w, midY + h / 2);
    if (rc.top < 0) { rc.OffsetRect(0, -rc.top); }

#if CCUSTOM_AERO_SUPPORT
    if (bOpaque)
        CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), rc, bg);
    else
#endif
        pDC->FillSolidRect(&rc, bg);

    // ごく薄いハイライト帯（上1px）でチップ感
    CRect hi(rc.left + 1, rc.top, rc.right - 1, rc.top + 1);
#if CCUSTOM_AERO_SUPPORT
    if (bOpaque)
        CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), hi, CCC_Lighten(bg, 36));
    else
#endif
        pDC->FillSolidRect(&hi, CCC_Lighten(bg, 36));

    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(fg);
    pDC->DrawText(label, (int)_tcslen(label), &rc,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    pDC->SelectObject(pOf);
    return w + 3;
}

// SAV=琥珀 / LRC=青 / 音声ch=薄荷 / 16ch·32ch=藤 / マップ=薄金
static int CCC_DrawSavLrcChips(CDC* pDC, int x, int midY, BOOL bSav, BOOL bLrc,
    const CString extra[], int extraN, BOOL bOpaque)
{
    if (!bSav && !bLrc && extraN <= 0) return x;
    const COLORREF fg = RGB(20, 20, 24);
    if (bSav)
        x += CCC_DrawMarkChip(pDC, x, midY, _T("SAV"),
            RGB(255, 214, 160), fg, bOpaque);
    if (bLrc)
        x += CCC_DrawMarkChip(pDC, x, midY, _T("LRC"),
            RGB(186, 210, 255), fg, bOpaque);
    for (int i = 0; i < extraN; ++i) {
        if (extra[i].IsEmpty()) continue;
        x += CCC_DrawMarkChip(pDC, x, midY, extra[i],
            CCC_MarkChipBg(extra[i]), fg, bOpaque);
    }
    return x;
}

// チップ列の消費幅（描画せず）。リスト列幅計算用。パディングは DrawMarkChip と揃える。
static int CCC_MeasureSavLrcChips(CDC* pDC, BOOL bSav, BOOL bLrc, const CString extra[], int extraN)
{
    if (!pDC || (!bSav && !bLrc && extraN <= 0)) return 0;
    CFont* pCur = pDC->GetCurrentFont();
    LOGFONT lf = {};
    if (pCur) pCur->GetLogFont(&lf);
    const int base = lf.lfHeight ? abs(lf.lfHeight) : 12;
    lf.lfHeight = -max(8, (base * 76) / 100);
    lf.lfWeight = FW_BOLD;
    CFont fontChip;
    if (!fontChip.CreateFontIndirect(&lf)) return 0;
    CFont* pOf = pDC->SelectObject(&fontChip);
    int w = 0;
    if (bSav) {
        CSize s = pDC->GetTextExtent(_T("SAV"));
        w += max(20, s.cx + 8) + 3;
    }
    if (bLrc) {
        CSize s = pDC->GetTextExtent(_T("LRC"));
        w += max(20, s.cx + 8) + 3;
    }
    for (int i = 0; i < extraN; ++i) {
        if (extra[i].IsEmpty()) continue;
        CSize s = pDC->GetTextExtent(extra[i]);
        w += max(20, s.cx + 8) + 3;
    }
    pDC->SelectObject(pOf);
    return w;
}

// リストセル文字列。収まるなら直描き、足りなければ X 縮小（下限 0.12）。省略記号は SaveDC 失敗時のみ。
static void DrawListSubitemCellText(CDC* pDC, const CString& str, const CRect& rcInner, UINT uAlignFmt = DT_LEFT)
{
    if (!pDC || str.IsEmpty() || rcInner.Width() <= 0 || rcInner.Height() <= 0) return;

    TEXTMETRIC tm;
    pDC->GetTextMetrics(&tm);
    int nFH = tm.tmHeight;
    int nMW = (std::max)(1, rcInner.Width());
    int nBudget = (std::max)(1, nMW - 3);

    int yTop = rcInner.top;
    if (rcInner.Height() >= nFH) yTop = rcInner.top + (rcInner.Height() - nFH) / 2;
    int drawH = (std::min)(nFH, (std::max)(1, rcInner.Height()));

    SIZE sz = { 0, 0 };
    if (!::GetTextExtentPoint32(pDC->GetSafeHdc(), str, str.GetLength(), &sz)) return;
    if (sz.cx <= 0) sz.cx = 1;

    const UINT uDT = (uAlignFmt & (DT_LEFT | DT_RIGHT | DT_CENTER)) | DT_TOP | DT_SINGLELINE | DT_NOPREFIX;

    if (sz.cx <= nMW)
    {
        CRect rd(rcInner.left, yTop, rcInner.right, yTop + drawH);
        pDC->DrawText(str, &rd, uDT);
        return;
    }

    if (!pDC->SaveDC())
    {
        CRect rd(rcInner.left, yTop, rcInner.right, yTop + drawH);
        pDC->DrawText(str, &rd, uDT | DT_END_ELLIPSIS);
        return;
    }

    pDC->SetGraphicsMode(GM_ADVANCED);
    float scale = 1.0f;
    for (int i = 0; i < 64; ++i)
    {
        float wd = (float)sz.cx * scale;
        if (wd <= (float)nBudget) break;
        scale *= (float)nBudget / wd;
        if (scale < 0.12f) { scale = 0.12f; break; } // 12% 未満は読めないので打ち切り
    }
    scale *= 0.95f;
    if (scale < 0.12f) scale = 0.12f;

    const float scaledW = (float)sz.cx * scale;
    float tx = (float)rcInner.left;
    if (uAlignFmt & DT_RIGHT)
        tx = (float)rcInner.right - scaledW;
    else if (uAlignFmt & DT_CENTER)
        tx = (float)rcInner.left + ((float)rcInner.Width() - scaledW) * 0.5f;
    if (tx < (float)rcInner.left)
        tx = (float)rcInner.left;

    XFORM xf = { scale, 0.0f, 0.0f, 1.0f, tx, (float)yTop };
    pDC->SetWorldTransform(&xf);

    CRect rl(0, 0, sz.cx, drawH);
    pDC->DrawText(str, &rl, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    pDC->RestoreDC(-1);
}

// DrawSmartText の fmt 指定版（左寄せチェック等）。フィット下限 0.50。
static void DrawSmartText2(CDC* pDC, CRect rect, CString str, UINT fmt, BOOL bDis, BOOL bPushed)
{
    if (str.IsEmpty()) return;

    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(bDis ? RGB(128, 128, 128) : COLOR_EDIT_TEXT);

    CRect rl = rect;
    rl.DeflateRect(2, 0);
    if (bPushed) rl.OffsetRect(1, 1);

    DrawFitControlText(pDC, rl, str, fmt, 0.50f);
}

// 定数αの単色塗り。1x1 ビットマップを引き伸ばす（毎呼び出し CreateBitmap は GDI 断片化）。
static void FillRectAlpha(CDC* pDC, const CRect& rc, COLORREF clr, BYTE alpha)
{
    // 単色塗りは 1x1 を引き伸ばして AlphaBlend する。
    // 毎呼び出し CreateCompatibleBitmap すると長時間で GDI ヒープが断片化し、
    // EQ スライダー群の再描画が数倍〜数十倍遅くなる。
    if (!pDC || rc.Width() <= 0 || rc.Height() <= 0) return;

    struct Pixel1x1Cache {
        CDC dc;
        CBitmap bmp;
        CBitmap* oldBmp = nullptr;
        COLORREF lastClr = (COLORREF)0xFFFFFFFF;
        bool ready = false;
        void Ensure(HDC hdcRef)
        {
            if (ready) return;
            if (!dc.CreateCompatibleDC(CDC::FromHandle(hdcRef))) return;
            if (!bmp.CreateCompatibleBitmap(CDC::FromHandle(hdcRef), 1, 1)) {
                dc.DeleteDC();
                return;
            }
            oldBmp = dc.SelectObject(&bmp);
            ready = true;
        }
    };
    static Pixel1x1Cache s_px;
    s_px.Ensure(pDC->GetSafeHdc());
    if (!s_px.ready) return;
    if (s_px.lastClr != clr) {
        s_px.dc.SetPixelV(0, 0, clr);
        s_px.lastClr = clr;
    }
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    ::AlphaBlend(pDC->GetSafeHdc(), rc.left, rc.top, rc.Width(), rc.Height(),
        s_px.dc.GetSafeHdc(), 0, 0, 1, 1, bf);
}

// ============================================================================
// 可愛さ強化用の共通描画プリミティブ
// （全カスタムコントロールで共有して使用します）
// ============================================================================

// Soft2D/Soft3D は UI スレッド専用・file-static 再利用（ネスト再入禁止）
static int s_uiSoftBusy = 0;
static GdiSoft2D::Context s_uiSoft2d;
static GdiSoft3D::Context s_uiSoft3d;

// 淫女モード共通パルス(はぁはぁ／ピクン／イク)。SoftJk・CCC_DrawInwoman 双方から使う。
static void CCC_InwomanPulse(DWORD t, double& breath, double& twitch, double& climax);
static void CCC_DrawVibrator(CDC* pDC, int cx, int cy, int sz, double t, double twitch, double breath, double climax, BOOL bAeroTrans);
static void CCC_DrawLoveFluid(CDC* pDC, const CRect& rc, double breath, double twitch, double climax, BOOL bAeroTrans);

// Soft2D/3D の非プレマルチ画素をプレマルチへ直し PresentAlpha。
// UI スレッド専用。s_uiSoftBusy と対で再入禁止。
static void SoftPremultPresent(GdiSoftFB::Framebuffer& fb, HDC dst, int dx, int dy, int w, int h, BYTE constA)
{
    if (!fb.color || !fb.hdc || fb.w != w || fb.h != h || !dst) return;
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
    fb.PresentAlpha(dst, dx, dy, constA);
}

// 背景用: 薄い Soft3D ポリゴンがゆっくり揺れる（前面を奪わない。リスト行には使わない）
static void DrawSoftJkBackdrop(CDC* pDC, const CRect& rc, int animTick, BOOL hot)
{
    if (!pDC || s_uiSoftBusy || rc.Width() < 28 || rc.Height() < 16) return;
    if (rc.Width() > 520 || rc.Height() > 160) return;
    ++s_uiSoftBusy;
    const int w = rc.Width();
    const int h = rc.Height();
    const COLORREF pink = CCC_IsInwoman() ? RGB(255, 160, 200) : RGB(255, 190, 220);
    const COLORREF lav = CCC_IsInwoman() ? RGB(240, 150, 210) : RGB(210, 190, 255);

    if (s_uiSoft3d.fb.w != w || s_uiSoft3d.fb.h != h)
        s_uiSoft3d.Create(w, h);
    if (s_uiSoft3d.fb.color && s_uiSoft3d.fb.w == w && s_uiSoft3d.fb.h == h) {
        s_uiSoft3d.fb.Clear(GdiSoftFB::PackBGRA(0, 0, 0, 0), 1e9f);
        s_uiSoft3d.alphaBlend = true;
        s_uiSoft3d.depthTest = true;
        s_uiSoft3d.depthWrite = true;
        s_uiSoft3d.fogMode = GdiSoft3D::FogNone;
        s_uiSoft3d.edgeOverlay = false;
        s_uiSoft3d.dofEnable = false;
        s_uiSoft3d.postVignette = s_uiSoft3d.postGlow = s_uiSoft3d.postSaturate = false;

        const float t = (float)animTick * 0.035f;
        const float buzz = CCC_IsInwoman() ? (sinf(t * 8.f) * 14.f) : (sinf(t) * 7.f);
        s_uiSoft3d.cam.yawDeg = -18.f + buzz;
        s_uiSoft3d.cam.pitchDeg = 38.f + cosf(t * 0.7f) * (CCC_IsInwoman() ? 8.f : 3.f);
        s_uiSoft3d.cam.zoom = hot ? (CCC_IsInwoman() ? 1.16f : 1.08f) : (CCC_IsInwoman() ? 1.06f : 1.0f);
        float boxes[1][6] = { { -0.85f, 0.85f, 0.f, 0.18f, -0.55f, 0.55f } };
        s_uiSoft3d.SetViewportFit(boxes, 1);

        const float bob = sinf(t * 1.1f) * 0.02f;
        s_uiSoft3d.DrawBox(-0.70f, -0.10f, 0.10f + bob, -0.40f, 0.05f, pink, 0.f);
        s_uiSoft3d.DrawBox(0.05f, 0.72f, 0.08f - bob, -0.15f, 0.40f, lav, 0.f);
        s_uiSoft3d.DrawQuad(
            -0.35f, 0.02f, -0.45f, 0.35f, 0.06f + bob, -0.20f,
             0.25f, 0.02f, 0.35f, -0.40f, 0.04f, 0.25f, pink);

		SoftPremultPresent(s_uiSoft3d.fb, pDC->GetSafeHdc(), rc.left, rc.top, w, h,
            hot ? (BYTE)88 : (BYTE)64);
    }

    if (s_uiSoft2d.Create(w, h, false) && s_uiSoft2d.fb.color) {
        s_uiSoft2d.ClearArgb(0);
        const int ox = (int)(sinf((float)animTick * 0.04f) * 3.f);
        const int oy = (int)(cosf((float)animTick * 0.03f) * 2.f);
        s_uiSoft2d.FillEllipse(w / 5 + ox, h * 3 / 4 + oy, max(4, w / 5), max(3, h / 4), pink, 28);
        s_uiSoft2d.FillEllipse(w * 4 / 5 - ox, h / 3 - oy, max(3, w / 6), max(2, h / 5), lav, 22);
        SoftPremultPresent(s_uiSoft2d.fb, pDC->GetSafeHdc(), rc.left, rc.top, w, h, hot ? (BYTE)95 : (BYTE)72);
    }
    --s_uiSoftBusy;
}

// 共通装飾用の小さな Soft3D チップ（リボン結び・角デコ・♡下地など）
// 常時OKだが PresentAlpha は控えめ。animTick は間引き更新前提。
static void DrawSoftJkChip(CDC* pDC, const CRect& rc, int animTick, BOOL hot)
{
    if (!pDC || s_uiSoftBusy) return;
    if (rc.Width() < 8 || rc.Height() < 8) return;
    if (rc.Width() > 36 || rc.Height() > 36) return;
    ++s_uiSoftBusy;
    const int w = rc.Width();
    const int h = rc.Height();
    if (s_uiSoft3d.fb.w != w || s_uiSoft3d.fb.h != h)
        s_uiSoft3d.Create(w, h);
    if (s_uiSoft3d.fb.color && s_uiSoft3d.fb.w == w && s_uiSoft3d.fb.h == h) {
        s_uiSoft3d.fb.Clear(GdiSoftFB::PackBGRA(0, 0, 0, 0), 1e9f);
        s_uiSoft3d.alphaBlend = true;
        s_uiSoft3d.depthTest = true;
        s_uiSoft3d.depthWrite = true;
        s_uiSoft3d.fogMode = GdiSoft3D::FogNone;
        s_uiSoft3d.edgeOverlay = false;
        s_uiSoft3d.dofEnable = false;
        s_uiSoft3d.postVignette = s_uiSoft3d.postGlow = s_uiSoft3d.postSaturate = false;
        const float t = (float)animTick * 0.05f;
        // 淫女: バイブ先端の小刻み振動。通常: ゆるい揺れ
        const float buzz = CCC_IsInwoman() ? (sinf(t * 9.f) * 16.f + cosf(t * 13.f) * 8.f) : (sinf(t) * 10.f);
        s_uiSoft3d.cam.yawDeg = -28.f + buzz;
        s_uiSoft3d.cam.pitchDeg = 32.f + cosf(t * 0.9f) * (CCC_IsInwoman() ? 10.f : 4.f);
        s_uiSoft3d.cam.zoom = hot ? (CCC_IsInwoman() ? 1.38f : 1.25f) : (CCC_IsInwoman() ? 1.22f : 1.1f);
        float boxes[1][6] = { { -0.45f, 0.45f, 0.f, 0.28f, -0.45f, 0.45f } };
        s_uiSoft3d.SetViewportFit(boxes, 1);
        const COLORREF c = CCC_IsInwoman() ? RGB(255, 96, 168) : RGB(255, 165, 210);
        if (CCC_IsInwoman()) {
            // 愛液まみれのバイブ先端(丸) + 本体短柱
            s_uiSoft3d.DrawSphere(0.f, 0.22f + sinf(t * 11.f) * 0.03f, 0.04f, 0.20f, RGB(255, 190, 230), 8, 6);
            s_uiSoft3d.DrawNeonBox(-0.12f, 0.12f, 0.08f, -0.22f, 0.22f, c, -0.18f);
        } else {
            const float y = hot ? 0.22f : 0.16f;
            s_uiSoft3d.DrawNeonBox(-0.28f, 0.28f, y + sinf(t) * 0.02f, -0.28f, 0.28f, c, 0.f);
        }
        SoftPremultPresent(s_uiSoft3d.fb, pDC->GetSafeHdc(), rc.left, rc.top, w, h,
            hot ? (BYTE)(CCC_IsInwoman() ? 190 : 150) : (BYTE)(CCC_IsInwoman() ? 150 : 110));
        if (CCC_IsInwoman()) {
            // チップ下に愛液しずく(GDI)。Soft3D の上に載せる。
            double breath = 0, twitch = 0, climax = 0;
            CCC_InwomanPulse(::GetTickCount(), breath, twitch, climax);
            const int dx = rc.CenterPoint().x + (int)(2 * twitch);
            const int dy = rc.bottom - 1 + (int)(3 * breath + 4 * climax);
            CBrush bf(RGB(255, 230, 244));
            CBrush* ob = pDC->SelectObject(&bf);
            CGdiObject* op = pDC->SelectStockObject(NULL_PEN);
            const int r = max(2, w / 6);
            pDC->Ellipse(dx - r, dy - r, dx + r, dy + r);
            if (op) pDC->SelectObject(op);
            pDC->SelectObject(ob);
        }
    }
    --s_uiSoftBusy;
}

// Soft3D ハート（2球＋下三角）。yaw でゆっくり回転。リスト／ツリー／レンジサム用。
static void DrawSoftJkHeart(CDC* pDC, const CRect& rc, int animTick, BOOL hot, COLORREF col)
{
    if (!pDC || s_uiSoftBusy) return;
    if (rc.Width() < 10 || rc.Height() < 10) return;
    if (rc.Width() > 40 || rc.Height() > 40) return;
    ++s_uiSoftBusy;
    const int w = rc.Width();
    const int h = rc.Height();
    if (s_uiSoft3d.fb.w != w || s_uiSoft3d.fb.h != h)
        s_uiSoft3d.Create(w, h);
    if (s_uiSoft3d.fb.color && s_uiSoft3d.fb.w == w && s_uiSoft3d.fb.h == h) {
        s_uiSoft3d.fb.Clear(GdiSoftFB::PackBGRA(0, 0, 0, 0), 1e9f);
        s_uiSoft3d.alphaBlend = true;
        s_uiSoft3d.depthTest = true;
        s_uiSoft3d.depthWrite = true;
        s_uiSoft3d.fogMode = GdiSoft3D::FogNone;
        s_uiSoft3d.edgeOverlay = false;
        s_uiSoft3d.dofEnable = false;
        s_uiSoft3d.postVignette = s_uiSoft3d.postGlow = s_uiSoft3d.postSaturate = false;
        const float spin = (float)animTick * 5.5f;
        double breath = 0, twitch = 0, climax = 0;
        if (CCC_IsInwoman())
            CCC_InwomanPulse(::GetTickCount(), breath, twitch, climax);
        // 淫女: ハートごまかしをやめ、震えるバイブ形(先端球+縦柱)
        if (CCC_IsInwoman()) {
            const float buzz = (float)(twitch * 22.0 + climax * 18.0);
            s_uiSoft3d.cam.yawDeg = -12.f + sinf((float)animTick * 0.9f) * (10.f + buzz);
            s_uiSoft3d.cam.pitchDeg = 26.f + cosf((float)animTick * 0.7f) * 8.f;
            s_uiSoft3d.cam.zoom = hot ? 1.55f : 1.38f;
            float boxes[1][6] = { { -0.35f, 0.35f, -0.55f, 0.55f, -0.35f, 0.35f } };
            s_uiSoft3d.SetViewportFit(boxes, 1);
            const COLORREF tip = RGB(255, 198, 232);
            const COLORREF body = col ? col : RGB(255, 110, 175);
            s_uiSoft3d.DrawSphere(0.f, 0.38f, 0.05f, 0.20f, tip, 9, 7);
            s_uiSoft3d.DrawNeonBox(-0.11f, 0.11f, 0.28f, -0.28f, 0.28f, body, -0.42f);
            SoftPremultPresent(s_uiSoft3d.fb, pDC->GetSafeHdc(),
                rc.left + (int)(3 * twitch), rc.top + (int)(-2 * twitch), w, h,
                hot ? (BYTE)240 : (BYTE)215);
            // 下端の愛液
            CCC_DrawLoveFluid(pDC, rc, breath, twitch, climax, FALSE);
        } else {
            s_uiSoft3d.cam.yawDeg = -22.f + spin + (hot ? sinf((float)animTick * 0.08f) * 6.f : 0.f);
            s_uiSoft3d.cam.pitchDeg = 28.f + cosf((float)animTick * 0.05f) * 4.f;
            s_uiSoft3d.cam.zoom = hot ? 1.45f : 1.28f;
            float boxes[1][6] = { { -0.55f, 0.55f, -0.55f, 0.55f, -0.4f, 0.4f } };
            s_uiSoft3d.SetViewportFit(boxes, 1);
            const COLORREF c = col ? col : RGB(255, 140, 188);
            const COLORREF cDeep = CCC_Darken(c, 40);
            s_uiSoft3d.DrawSphere(-0.18f, 0.18f, 0.05f, 0.22f, c, 10, 7);
            s_uiSoft3d.DrawSphere(0.18f, 0.18f, 0.05f, 0.22f, c, 10, 7);
            s_uiSoft3d.DrawQuad(
                -0.38f, 0.02f, 0.02f,  0.38f, 0.02f, 0.02f,
                 0.0f, -0.48f, 0.08f,  0.0f, -0.48f, -0.02f, cDeep);
            SoftPremultPresent(s_uiSoft3d.fb, pDC->GetSafeHdc(), rc.left, rc.top, w, h,
                hot ? (BYTE)230 : (BYTE)200);
        }
    }
    --s_uiSoftBusy;
}

// Soft3D つまみ／先端ジェム（NeonBox）。スライダー・プログレス用。
static void DrawSoftJkThumb(CDC* pDC, const CRect& rc, int animTick, BOOL hot, float tiltDeg)
{
    if (!pDC || s_uiSoftBusy) return;
    if (rc.Width() < 8 || rc.Height() < 8) return;
    if (rc.Width() > 36 || rc.Height() > 36) return;
    ++s_uiSoftBusy;
    const int w = rc.Width();
    const int h = rc.Height();
    if (s_uiSoft3d.fb.w != w || s_uiSoft3d.fb.h != h)
        s_uiSoft3d.Create(w, h);
    if (s_uiSoft3d.fb.color && s_uiSoft3d.fb.w == w && s_uiSoft3d.fb.h == h) {
        s_uiSoft3d.fb.Clear(GdiSoftFB::PackBGRA(0, 0, 0, 0), 1e9f);
        s_uiSoft3d.alphaBlend = true;
        s_uiSoft3d.depthTest = true;
        s_uiSoft3d.depthWrite = true;
        s_uiSoft3d.fogMode = GdiSoft3D::FogNone;
        s_uiSoft3d.edgeOverlay = false;
        s_uiSoft3d.dofEnable = false;
        s_uiSoft3d.postVignette = s_uiSoft3d.postGlow = s_uiSoft3d.postSaturate = false;
        const float t = (float)animTick * 0.06f;
        double breath = 0, twitch = 0, climax = 0;
        if (CCC_IsInwoman())
            CCC_InwomanPulse(::GetTickCount(), breath, twitch, climax);
        const float buzz = CCC_IsInwoman() ? (float)(twitch * 20.0 + climax * 14.0) : 0.f;
        s_uiSoft3d.cam.yawDeg = -24.f + tiltDeg + sinf(t) * (hot ? 14.f : 6.f) + buzz;
        s_uiSoft3d.cam.pitchDeg = 34.f + cosf(t * 0.8f) * 3.f;
        s_uiSoft3d.cam.zoom = hot ? (CCC_IsInwoman() ? 1.42f : 1.3f) : (CCC_IsInwoman() ? 1.24f : 1.12f);
        float boxes[1][6] = { { -0.4f, 0.4f, 0.f, 0.3f, -0.4f, 0.4f } };
        s_uiSoft3d.SetViewportFit(boxes, 1);
        const COLORREF c = CCC_IsInwoman() ? RGB(255, 100, 178) : RGB(200, 160, 255);
        if (CCC_IsInwoman()) {
            s_uiSoft3d.DrawSphere(0.f, 0.28f, 0.04f, 0.18f, RGB(255, 200, 235), 8, 6);
            s_uiSoft3d.DrawNeonBox(-0.13f, 0.13f, 0.16f + sinf(t * 10.f) * 0.03f, -0.24f, 0.24f, c, -0.22f);
        } else {
            s_uiSoft3d.DrawNeonBox(-0.22f, 0.22f, 0.2f + sinf(t) * 0.02f, -0.22f, 0.22f, c, 0.f);
        }
        SoftPremultPresent(s_uiSoft3d.fb, pDC->GetSafeHdc(),
            rc.left + (CCC_IsInwoman() ? (int)(2 * twitch) : 0),
            rc.top + (CCC_IsInwoman() ? (int)(-2 * climax) : 0), w, h,
            hot ? (BYTE)(CCC_IsInwoman() ? 200 : 165) : (BYTE)(CCC_IsInwoman() ? 155 : 125));
        if (CCC_IsInwoman())
            CCC_DrawLoveFluid(pDC, rc, breath, twitch, climax, FALSE);
    }
    --s_uiSoftBusy;
}

// Soft3D リボン結び（ホバー時ボタン等）。小さな torus。
static void DrawSoftJkKnot(CDC* pDC, const CRect& rc, int animTick)
{
    if (!pDC || s_uiSoftBusy) return;
    if (rc.Width() < 10 || rc.Height() < 10) return;
    if (rc.Width() > 40 || rc.Height() > 40) return;
    ++s_uiSoftBusy;
    const int w = rc.Width();
    const int h = rc.Height();
    if (s_uiSoft3d.fb.w != w || s_uiSoft3d.fb.h != h)
        s_uiSoft3d.Create(w, h);
    if (s_uiSoft3d.fb.color && s_uiSoft3d.fb.w == w && s_uiSoft3d.fb.h == h) {
        s_uiSoft3d.fb.Clear(GdiSoftFB::PackBGRA(0, 0, 0, 0), 1e9f);
        s_uiSoft3d.alphaBlend = true;
        s_uiSoft3d.depthTest = true;
        s_uiSoft3d.depthWrite = true;
        s_uiSoft3d.fogMode = GdiSoft3D::FogNone;
        s_uiSoft3d.edgeOverlay = false;
        s_uiSoft3d.dofEnable = false;
        s_uiSoft3d.postVignette = s_uiSoft3d.postGlow = s_uiSoft3d.postSaturate = false;
        const float t = (float)animTick * 0.09f;
        double breath = 0, twitch = 0, climax = 0;
        if (CCC_IsInwoman())
            CCC_InwomanPulse(::GetTickCount(), breath, twitch, climax);
        s_uiSoft3d.cam.yawDeg = -40.f + sinf(t) * (CCC_IsInwoman() ? (24.f + (float)twitch * 20.f) : 18.f);
        s_uiSoft3d.cam.pitchDeg = 40.f + cosf(t * 0.7f) * (CCC_IsInwoman() ? 12.f : 8.f);
        s_uiSoft3d.cam.zoom = CCC_IsInwoman() ? 1.55f : 1.4f;
        float boxes[1][6] = { { -0.5f, 0.5f, -0.2f, 0.35f, -0.5f, 0.5f } };
        s_uiSoft3d.SetViewportFit(boxes, 1);
        const COLORREF c = CCC_IsInwoman() ? RGB(255, 96, 168) : RGB(255, 150, 200);
        if (CCC_IsInwoman()) {
            // 濡れ玉(愛液の塊) — リボン結びのごまかしをやめる
            s_uiSoft3d.DrawSphere(0.f, 0.08f, 0.f, 0.32f, c, 10, 8);
            s_uiSoft3d.DrawSphere(0.06f, 0.18f, 0.08f, 0.14f, RGB(255, 220, 240), 8, 6);
        } else {
            s_uiSoft3d.DrawTorus(0.f, 0.05f, 0.f, 0.28f, 0.09f, c, 12, 8);
        }
        SoftPremultPresent(s_uiSoft3d.fb, pDC->GetSafeHdc(), rc.left, rc.top, w, h,
            (BYTE)(CCC_IsInwoman() ? 190 : 150));
        if (CCC_IsInwoman())
            CCC_DrawLoveFluid(pDC, rc, breath, twitch, climax, FALSE);
    }
    --s_uiSoftBusy;
}

// Soft3D コーナー／枠のゆらゆら（GroupBox 等）。微小 yaw/pitch。
static void DrawSoftJkSwayCorner(CDC* pDC, const CRect& rc, int animTick, float amp)
{
    if (!pDC || s_uiSoftBusy) return;
    if (rc.Width() < 8 || rc.Height() < 8) return;
    if (rc.Width() > 28 || rc.Height() > 28) return;
    ++s_uiSoftBusy;
    const int w = rc.Width();
    const int h = rc.Height();
    if (s_uiSoft3d.fb.w != w || s_uiSoft3d.fb.h != h)
        s_uiSoft3d.Create(w, h);
    if (s_uiSoft3d.fb.color && s_uiSoft3d.fb.w == w && s_uiSoft3d.fb.h == h) {
        s_uiSoft3d.fb.Clear(GdiSoftFB::PackBGRA(0, 0, 0, 0), 1e9f);
        s_uiSoft3d.alphaBlend = true;
        s_uiSoft3d.depthTest = true;
        s_uiSoft3d.depthWrite = true;
        s_uiSoft3d.fogMode = GdiSoft3D::FogNone;
        s_uiSoft3d.edgeOverlay = false;
        s_uiSoft3d.dofEnable = false;
        s_uiSoft3d.postVignette = s_uiSoft3d.postGlow = s_uiSoft3d.postSaturate = false;
        const float t = (float)animTick * 0.04f;
        const float buzz = CCC_IsInwoman() ? (sinf(t * 11.f) * (amp + 6.f)) : (sinf(t) * amp);
        s_uiSoft3d.cam.yawDeg = -20.f + buzz;
        s_uiSoft3d.cam.pitchDeg = 36.f + cosf(t * 0.85f) * (amp * (CCC_IsInwoman() ? 0.7f : 0.4f));
        s_uiSoft3d.cam.zoom = CCC_IsInwoman() ? 1.18f : 1.08f;
        float boxes[1][6] = { { -0.4f, 0.4f, 0.f, 0.22f, -0.4f, 0.4f } };
        s_uiSoft3d.SetViewportFit(boxes, 1);
        const COLORREF c = CCC_IsInwoman() ? RGB(255, 120, 185) : RGB(220, 180, 255);
        if (CCC_IsInwoman())
            s_uiSoft3d.DrawSphere(0.f, 0.06f + sinf(t * 9.f) * 0.02f, 0.f, 0.22f, c, 8, 6);
        else
            s_uiSoft3d.DrawBox(-0.22f, 0.22f, 0.12f + sinf(t) * 0.015f, -0.22f, 0.22f, c, 0.f);
        SoftPremultPresent(s_uiSoft3d.fb, pDC->GetSafeHdc(), rc.left, rc.top, w, h,
            (BYTE)(CCC_IsInwoman() ? 140 : 100));
    }
    --s_uiSoftBusy;
}

// ぷるんとした濡れツヤ(ガラス/リップグロス風)を上半分にのせる。
// 不透明な面の上にのみ使用すること（クロマキー透過領域には使わない）。
// ※ Soft* はここへ入れない。リスト選択行の♡白飛びを防ぐため GDI ツヤのみ。
// baseBg!=CLR_NONE: アクリルDIB向け。AlphaBlend せず不透明縦グラデで塗る
// （POST_OPAQUE では 1x1 AlphaBlend が失敗しフラットになる）。
static void DrawGlossHighlight(CDC* pDC, const CRect& rc, int radius, COLORREF baseBg = CLR_NONE)
{
    if (!pDC || rc.Width() <= 4 || rc.Height() <= 6) return;
#if CCUSTOM_AERO_SUPPORT
    if (baseBg != CLR_NONE) {
        const COLORREF cTop = CCC_Lighten(baseBg, 48);
        const COLORREF cBot = CCC_Darken(baseBg, 90);
        const int hgt = rc.Height();
        int bands = (hgt < 8) ? hgt : 8;
        if (bands < 2) bands = 2;
        HDC hdc = pDC->GetSafeHdc();
        for (int i = 0; i < bands; ++i) {
            RECT b = { rc.left, rc.top + hgt * i / bands, rc.right, rc.top + hgt * (i + 1) / bands };
            if (b.bottom <= b.top) continue;
            const int t = i * 100 / (bands - 1);
            const COLORREF c = RGB(
                GetRValue(cTop) + (GetRValue(cBot) - GetRValue(cTop)) * t / 100,
                GetGValue(cTop) + (GetGValue(cBot) - GetGValue(cTop)) * t / 100,
                GetBValue(cTop) + (GetBValue(cBot) - GetBValue(cTop)) * t / 100);
            CCC_FillRectOpaqueBits(hdc, b, c);
        }
        CRect line = rc;
        const int rad = max(2, radius);
        line.DeflateRect(rad, 0);
        line.top += max(1, hgt / 14);
        line.bottom = line.top + max(1, hgt / 12);
        if (line.Width() > 2 && line.Height() > 0)
            CCC_FillRectOpaqueBits(hdc, line, CCC_Lighten(baseBg, 72));
        return;
    }
#else
    UNREFERENCED_PARAMETER(baseBg);
#endif
    CRgn rgn;
    const int d = max(2, radius * 2);
    if (!rgn.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, d, d))
        return;
    pDC->SelectClipRgn(&rgn);
    // 上半分のとろっとしたツヤ
    CRect top = rc;
    top.bottom = rc.top + max(2, rc.Height() * 46 / 100);
    FillRectAlpha(pDC, top, COLOR_GLOSS, 120);
    // 上端の強いハイライト線でリップグロスのような濡れ感
    CRect line = rc;
    line.DeflateRect(radius, 0);
    line.top += max(1, rc.Height() / 14);
    line.bottom = line.top + max(1, rc.Height() / 12);
    if (line.Width() > 2 && line.Height() > 0)
        FillRectAlpha(pDC, line, COLOR_GLOSS, 195);
    // 下側のほんのりした照り返し
    CRect bottom = rc;
    bottom.top = rc.top + rc.Height() * 72 / 100;
    bottom.DeflateRect(radius, 0);
    if (bottom.Width() > 2 && bottom.Height() > 1)
        FillRectAlpha(pDC, bottom, COLOR_GLOSS, 42);
    pDC->SelectClipRgn(NULL);
}

// ほんのり頬染め(ブラッシュ): やわらかいピンクのにじみを置く
static void DrawBlush(CDC* pDC, int cx, int cy, int rx, int ry)
{
    if (!pDC || rx < 2 || ry < 1) return;
    // 外側うっすら → 内側やや濃く、の二段でにじませる
    for (int i = 0; i < 2; ++i)
    {
        const int sx = (i == 0) ? rx : (rx * 6 / 10);
        const int sy = (i == 0) ? ry : (ry * 6 / 10);
        CRect r(cx - sx, cy - sy, cx + sx, cy + sy);
        FillRectAlpha(pDC, r, COLOR_BLUSH, (i == 0) ? 40 : 60);
    }
}

// キラキラ(4方向にのびるダイヤ型の光 + 白い芯)
static void DrawSparkle(CDC* pDC, int cx, int cy, int sz, COLORREF c)
{
    if (!pDC) return;
    if (sz < 2) sz = 2;
    CPen pen(PS_SOLID, 1, c);
    CBrush br(c);
    CPen* op = pDC->SelectObject(&pen);
    CBrush* ob = pDC->SelectObject(&br);

    POINT v[4] = { {cx, cy - sz}, {cx + sz / 2, cy}, {cx, cy + sz}, {cx - sz / 2, cy} };
    pDC->Polygon(v, 4);
    POINT h[4] = { {cx - sz, cy}, {cx, cy - sz / 2}, {cx + sz, cy}, {cx, cy + sz / 2} };
    pDC->Polygon(h, 4);

    CBrush bc(COLOR_SPARKLE_CORE);
    pDC->SelectObject(&bc);
    int r = max(1, sz / 3);
    pDC->Ellipse(cx - r, cy - r, cx + r, cy + r);

    pDC->SelectObject(ob);
    pDC->SelectObject(op);
}

// ちょうちょ結びのリボン(ぷっくり羽 + 中央の結び目)
static void DrawBow(CDC* pDC, const CRect& rc, COLORREF c)
{
    if (!pDC || rc.Width() < 4 || rc.Height() < 4) return;
    const int cx = rc.CenterPoint().x;
    const int cy = rc.CenterPoint().y;
    const int w = max(3, rc.Width() / 2);
    const int h = max(2, rc.Height() / 2);

    CBrush br(c);
    CPen pen(PS_SOLID, 1, RGB(224, 120, 162));
    CBrush* ob = pDC->SelectObject(&br);
    CPen* op = pDC->SelectObject(&pen);

    POINT l[3] = { {cx, cy}, {cx - w, cy - h}, {cx - w, cy + h} };
    POINT r2[3] = { {cx, cy}, {cx + w, cy - h}, {cx + w, cy + h} };
    pDC->Polygon(l, 3);
    pDC->Polygon(r2, 3);

    // 羽の内側のハイライト
    CBrush bh(RGB(255, 224, 236));
    pDC->SelectObject(&bh);
    pDC->SelectObject(GetStockObject(NULL_PEN));
    int iw = max(1, w / 3), ih = max(1, h / 3);
    pDC->Ellipse(cx - w + 2, cy - ih, cx - w + 2 + iw, cy + ih);
    pDC->Ellipse(cx + w - 2 - iw, cy - ih, cx + w - 2, cy + ih);

    // 中央の結び目
    CBrush bk(c);
    pDC->SelectObject(&bk);
    pDC->SelectObject(&pen);
    int k = max(2, w / 4);
    CRect rk(cx - k, cy - max(2, h / 2), cx + k, cy + max(2, h / 2));
    pDC->RoundRect(&rk, CPoint(2, 2));

    pDC->SelectObject(ob);
    pDC->SelectObject(op);

    // 結び目下に Soft3D チップ（常時でも小さく間引き前提）
    DrawSoftJkChip(pDC, CRect(cx - 6, cy - 6, cx + 6, cy + 6),
        (int)(::GetTickCount64() / 220), FALSE);
}

// ぷるんとした濡れツヤ付きのチェック(レ点)。丸端の太線でやわらかく。
// swayDeg: ホバー時のゆっくり首振り（±数度）。0=静止。
static void DrawCheckMark(CDC* pDC, const CRect& rc, COLORREF c, int thick, float swayDeg = 0.f)
{
    if (!pDC || rc.Width() < 5 || rc.Height() < 5) return;
    if (thick < 2) thick = 2;
    int x1 = rc.left + rc.Width() * 12 / 100, y1 = rc.top + rc.Height() * 54 / 100;
    int x2 = rc.left + rc.Width() * 40 / 100, y2 = rc.top + rc.Height() * 82 / 100;
    int x3 = rc.left + rc.Width() * 92 / 100, y3 = rc.top + rc.Height() * 14 / 100;
    if (swayDeg != 0.f) {
        const float rad = swayDeg * (float)(3.14159265 / 180.0);
        const float cs = cosf(rad), sn = sinf(rad);
        const float ox = (float)rc.CenterPoint().x, oy = (float)rc.CenterPoint().y;
        auto rot = [&](int& x, int& y) {
            const float dx = (float)x - ox, dy = (float)y - oy;
            x = (int)(ox + dx * cs - dy * sn + 0.5f);
            y = (int)(oy + dx * sn + dy * cs + 0.5f);
        };
        rot(x1, y1); rot(x2, y2); rot(x3, y3);
    }

    // 影でぷっくり立体感
    CPen psh(PS_SOLID, thick, CCC_Darken(c, 45));
    CPen* op = pDC->SelectObject(&psh);
    pDC->MoveTo(x1, y1 + 2); pDC->LineTo(x2, y2 + 2); pDC->LineTo(x3, y3 + 2);

    // 本体(丸端・丸つなぎのジオメトリックペン)
    LOGBRUSH lb = { BS_SOLID, c, 0 };
    CPen pc;
    if (pc.CreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, thick, &lb))
        pDC->SelectObject(&pc);
    pDC->MoveTo(x1, y1); pDC->LineTo(x2, y2); pDC->LineTo(x3, y3);
    pDC->SelectObject(op);

    // 長い方の線に濡れツヤ
    DrawShine(pDC, (x2 + x3 * 3) / 4, (y2 + y3 * 3) / 4 - thick / 3, max(1, thick / 3), max(1, thick / 4));
}

// サテン/シルク質感: 縦グラデ + 上寄りの一筋ハイライト + 下の照り返し。
// (不透明な面の上でのみ使用すること)
static void DrawSatinFill(CDC* pDC, const CRect& rc, COLORREF base)
{
    if (!pDC || rc.Width() <= 1 || rc.Height() <= 1) return;
    const COLORREF top = CCC_Lighten(base, 24);
    const COLORREF bot = CCC_Darken(base, 84);
    DrawGradientBackground(pDC, rc, top, bot, 0); // 縦(上→下)

    // サテンの照り(上1/8〜2/5あたりの水平ハイライト帯)
    CRect band = rc;
    band.top = rc.top + rc.Height() / 8;
    band.bottom = rc.top + rc.Height() * 40 / 100;
    if (band.Height() > 0)
        FillRectAlpha(pDC, band, RGB(255, 255, 255), 64);

    // 下側のやわらかい照り返し
    CRect rim = rc;
    rim.top = rc.top + rc.Height() * 80 / 100;
    if (rim.Height() > 0)
        FillRectAlpha(pDC, rim, CCC_Lighten(base, 32), 70);
}

// ぷっくりジェリー感: 角丸内に上端リムライト + 下側インナーシャドウ。
static void DrawJellyEdges(CDC* pDC, const CRect& rc, int radius, COLORREF shadowTint)
{
    if (!pDC || rc.Width() <= 4 || rc.Height() <= 6) return;
    CRgn rgn;
    const int d = max(2, radius * 2);
    if (!rgn.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, d, d))
        return;
    pDC->SelectClipRgn(&rgn);
    // 下側のインナーシャドウ
    CRect lower = rc;
    lower.top = rc.top + rc.Height() * 68 / 100;
    FillRectAlpha(pDC, lower, shadowTint, 30);
    // 上端のリムライト(細い強ハイライト)
    CRect rim = rc;
    rim.DeflateRect(radius, 0);
    rim.bottom = rim.top + max(1, rc.Height() / 14);
    if (rim.Width() > 2)
        FillRectAlpha(pDC, rim, RGB(255, 255, 255), 170);
    pDC->SelectClipRgn(NULL);
}

// シアー(透け)レース: 下向きスカラップ(半円)の連なり + ピコ(小さなドット)。
// 細い線なので半透明風の繊細な印象。べた塗りなのでクロマ透過でも安全。
static void DrawLaceScallop(CDC* pDC, int x1, int y, int x2, int r, COLORREF c)
{
    if (!pDC || r < 2 || x2 - x1 < r * 2) return;
    CPen p(PS_SOLID, 1, c);
    CPen* op = pDC->SelectObject(&p);
    CGdiObject* ob = pDC->SelectStockObject(NULL_BRUSH);
    const int step = r * 2;
    for (int cx = x1 + r; cx <= x2 - r; cx += step)
    {
        // 下向きの半円(スカラップ)
        pDC->Arc(cx - r, y - r, cx + r, y + r, cx + r, y, cx - r, y);
        // スカラップの底に小さなピコ
        pDC->SetPixel(cx, y + r, c);
    }
    if (ob) pDC->SelectObject(ob);
    pDC->SelectObject(op);
}

// ほどけかけリボン: 左右非対称のループ + だらりと垂れた2本のテール。色っぽいしどけなさ。
// angleDeg: ホバー時のゆっくり回転（±十数度まで）。0=静止。
static void DrawLooseRibbon(CDC* pDC, const CRect& rc, COLORREF c, float angleDeg = 0.f)
{
    if (!pDC || rc.Width() < 6 || rc.Height() < 5) return;
    const int cx = rc.CenterPoint().x;
    const int cy = rc.top + rc.Height() / 3;
    const int w = max(3, rc.Width() / 2);
    const int h = max(2, rc.Height() / 3);

    int saved = 0;
    if (angleDeg != 0.f) {
        saved = pDC->SaveDC();
        ::SetGraphicsMode(pDC->GetSafeHdc(), GM_ADVANCED);
        const float rad = angleDeg * (float)(3.14159265 / 180.0);
        const float cs = cosf(rad), sn = sinf(rad);
        XFORM xf = {};
        xf.eM11 = cs; xf.eM12 = sn; xf.eM21 = -sn; xf.eM22 = cs;
        xf.eDx = (float)cx - cs * (float)cx + sn * (float)cy;
        xf.eDy = (float)cy - sn * (float)cx - cs * (float)cy;
        pDC->SetWorldTransform(&xf);
    }

    CBrush br(c);
    CPen pen(PS_SOLID, 1, CCC_Darken(c, 64));
    CBrush* ob = pDC->SelectObject(&br);
    CPen* op = pDC->SelectObject(&pen);

    // 垂れたテール(下に伸びる2本)
    POINT tailL[4] = { {cx - 1, cy}, {cx - w / 2 - 1, rc.bottom}, {cx - w / 6, rc.bottom}, {cx, cy + h / 2} };
    POINT tailR[4] = { {cx + 1, cy}, {cx + w / 3, rc.bottom}, {cx + w * 2 / 3, rc.bottom - 2}, {cx, cy + h / 2} };
    pDC->Polygon(tailL, 4);
    pDC->Polygon(tailR, 4);

    // 左ループ(やや大きく傾く) / 右ループ(小さめ=ほどけかけ)
    POINT loopL[3] = { {cx, cy}, {cx - w, cy - h - 1}, {cx - w + 1, cy + h} };
    POINT loopR[3] = { {cx, cy}, {cx + w * 4 / 5, cy - h + 1}, {cx + w * 3 / 5, cy + h - 1} };
    pDC->Polygon(loopL, 3);
    pDC->Polygon(loopR, 3);

    // ループ内のツヤ
    DrawShine(pDC, cx - w * 3 / 5, cy - h / 4, max(1, w / 6), max(1, h / 2));

    // 中央の結び目
    int k = max(2, w / 4);
    CRect rk(cx - k, cy - max(2, h / 2), cx + k, cy + max(2, h / 2));
    pDC->RoundRect(&rk, CPoint(2, 2));

    pDC->SelectObject(ob);
    pDC->SelectObject(op);
    if (saved) pDC->RestoreDC(saved);
}

// ============================================================================
// 【隠し機能 / イースターエッグ】inwoman / CCC_Iw*
// ユーザー向けヘルプ・操作説明には載せない。入口/出口の手順は CCC_InwomanHotkey のみ。
// ============================================================================
static UINT_PTR g_inwomanTimer = 0;
static int      g_f12Count = 0;
static int      g_f11Count = 0;
static int      g_iwSeq = 0;      // 0=F12集め / 1=F11集め / 2=F12押しっぱなし
static DWORD    g_seqT0 = 0;      // 現バースト最初の時刻
static DWORD    g_armT0 = 0;      // 直前段階の達成時刻
static DWORD    g_holdT0 = 0;     // F12 押し始め

enum { IW_BURST_MS = 3000, IW_GAP_MS = 4000, IW_HOLD_MS = 2000 }; // 閾値の意味は CCC_InwomanHotkey

// 連打シーケンスを初期化。タイムアウト・誤キー・出入り完了時。
static void CCC_IwSeqReset()
{
    g_f12Count = 0;
    g_f11Count = 0;
    g_iwSeq = 0;
    g_seqT0 = 0;
    g_armT0 = 0;
    g_holdT0 = 0;
}

// ちらつき対策: 背景消去を伴う全画面再描画はやめ、オーナードロー(ダブルバッファ)の
// カスタムコントロールだけを消去なしで無効化する。
static BOOL CALLBACK CCC_InwomanInvalidateChild(HWND hChild, LPARAM)
{
    CWnd* p = CWnd::FromHandlePermanent(hChild);
    if (p && ::IsWindowVisible(hChild) &&
        (p->IsKindOf(RUNTIME_CLASS(CCustomStandardButton)) ||
         p->IsKindOf(RUNTIME_CLASS(CCustomCheckBox)) ||
         p->IsKindOf(RUNTIME_CLASS(CCustomSliderCtrl)) ||
         p->IsKindOf(RUNTIME_CLASS(CCustomRangeSliderCtrl)) ||
         p->IsKindOf(RUNTIME_CLASS(CCustomComboBox)) ||
         p->IsKindOf(RUNTIME_CLASS(CCustomProgressCtrl)) ||
         p->IsKindOf(RUNTIME_CLASS(CCustomSysPerfCtrl)) ||
         p->IsKindOf(RUNTIME_CLASS(CCustomGroupBox)) ||
         p->IsKindOf(RUNTIME_CLASS(CCustomStatic))))
    {
        ::InvalidateRect(hChild, NULL, FALSE); // 消去なし=ちらつかない
    }
    return TRUE;
}

// 可視トップ窓を列挙。aero=0 のとき親も消さず無効化、子は OwnerDraw のみ。
static BOOL CALLBACK CCC_InwomanTopProc(HWND hTop, LPARAM)
{
    if (::IsWindowVisible(hTop))
    {
        if (CCC_IsInwoman() && !CCC_IsAeroEnabled())
            ::InvalidateRect(hTop, NULL, FALSE);
        ::EnumChildWindows(hTop, CCC_InwomanInvalidateChild, 0);
    }
    return TRUE;
}

// UI スレッドの全トップ窓へアニメ Invalidate。タイマーから。
static void CCC_InwomanInvalidateAll()
{
    ::EnumThreadWindows(::GetCurrentThreadId(), CCC_InwomanTopProc, 0);
}

// 隠し演出 ON（savedata.inwoman=1）。シーケンスはリセット。ヘルプ非掲載。
static void CCC_IwEnter()
{
    CCC_IwSeqReset();
    savedata.inwoman = 1;
    CCC_InwomanInvalidateAll();
}

// 最終段の長押しをタイマー側でも見る（キーリピート欠落対策）。手順は CCC_InwomanHotkey。
static void CCC_IwPollHold(DWORD now)
{
    if (g_iwSeq != 2)
        return;
    if (g_holdT0 == 0) {
        if (g_armT0 && (now - g_armT0) > IW_GAP_MS)
            CCC_IwSeqReset();
        return;
    }
    if ((::GetAsyncKeyState(VK_F12) & 0x8000) == 0) {
        g_holdT0 = 0;
        if (g_armT0 && (now - g_armT0) > IW_GAP_MS)
            CCC_IwSeqReset();
        return;
    }
    if ((now - g_holdT0) >= IW_HOLD_MS)
        CCC_IwEnter();
}

// 180ms 周期。未入場なら長押し判定のみ。入場中はキャプチャ/メニュー中以外で再描画。
static void CALLBACK CCC_InwomanTimerProc(HWND, UINT, UINT_PTR, DWORD)
{
    const DWORD now = ::GetTickCount();
    if (!CCC_IsInwoman()) {
        CCC_IwPollHold(now);
        return;
    }
    // クリック／ドラッグ中に全控件 Invalidate すると BN_CLICKED が欠落しやすい
    if (::GetCapture() != NULL) return;
    if (CCustomPopupMenu::GetTrackingRoot() != NULL) return;
    CCC_InwomanInvalidateAll();
}

// 冪等。各ダイアログ PreTranslate / サブクラス時に呼ぶ。間隔は入力飢餓を避けるため 180ms。
void CCC_StartInwomanTimer()
{
    if (g_inwomanTimer == 0)
        // 55ms 全控件再描画は入力飢餓の温床。Soft タイマと同程度に間引く
        g_inwomanTimer = ::SetTimer(NULL, 0, 180, CCC_InwomanTimerProc);
}

// 隠し演出の入口/出口。各メインダイアログの PreTranslateMessage から。ヘルプ非掲載。
BOOL CCC_InwomanHotkey(MSG* pMsg, CWnd* pWnd)
{
    UNREFERENCED_PARAMETER(pWnd);
    CCC_StartInwomanTimer();
    if (!pMsg)
        return FALSE;

    const WPARAM vk = pMsg->wParam;
    const DWORD now = ::GetTickCount();

    if (pMsg->message == WM_KEYUP) {
        if (!CCC_IsInwoman() && g_iwSeq == 2 && vk == VK_F12)
            g_holdT0 = 0;
        return FALSE;
    }
    if (pMsg->message != WM_KEYDOWN)
        return FALSE;

    const int wasDown = (pMsg->lParam & (1 << 30)) ? 1 : 0;

    // 出口: 入っているときだけ F12 を 2秒以内に5回
    if (CCC_IsInwoman())
    {
        if (wasDown)
            return FALSE;
        if (vk != VK_F12)
            return FALSE;
        if (g_f12Count == 0 || (now - g_seqT0) > 2000)
        {
            g_f12Count = 0;
            g_seqT0 = now;
        }
        if (++g_f12Count >= 5)
        {
            CCC_IwSeqReset();
            savedata.inwoman = 0;
            CCC_InwomanInvalidateAll();
            return TRUE;
        }
        return FALSE;
    }

    if (vk != VK_F12 && vk != VK_F11)
        return FALSE;

    // 入口最終段: F12 を IW_HOLD_MS 押しっぱなし
    if (g_iwSeq == 2)
    {
        if (vk != VK_F12) {
            CCC_IwSeqReset();
            return FALSE;
        }
        if (!wasDown) {
            if (g_armT0 && (now - g_armT0) > IW_GAP_MS) {
                CCC_IwSeqReset();
                return FALSE;
            }
            g_holdT0 = now;
        }
        if (g_holdT0 && (now - g_holdT0) >= IW_HOLD_MS
            && (::GetAsyncKeyState(VK_F12) & 0x8000)) {
            CCC_IwEnter();
            return TRUE;
        }
        return FALSE;
    }

    // キーリピートは連打に数えない
    if (wasDown)
        return FALSE;

    // 入口: F12 を burst 以内に7回以上 → 続けて F11 を burst 以内に7回 → F12 長押し
    if (vk == VK_F12)
    {
        if (g_iwSeq == 1)
        {
            // F11待ち中の F12 は入り口やり直し
            CCC_IwSeqReset();
        }
        if (g_f12Count == 0 || (now - g_seqT0) > IW_BURST_MS)
        {
            g_f12Count = 0;
            g_seqT0 = now;
        }
        ++g_f12Count;
        if (g_f12Count >= 7)
        {
            g_iwSeq = 1;
            g_f11Count = 0;
            g_armT0 = now;
            g_seqT0 = 0;
        }
        return FALSE;
    }

    // F11
    if (g_iwSeq != 1)
        return FALSE;
    if (g_f11Count == 0)
    {
        if ((now - g_armT0) > IW_GAP_MS)
        {
            CCC_IwSeqReset();
            return FALSE;
        }
        g_seqT0 = now;
    }
    else if ((now - g_seqT0) > IW_BURST_MS)
    {
        CCC_IwSeqReset();
        return FALSE;
    }
    if (++g_f11Count >= 7)
    {
        g_iwSeq = 2;
        g_holdT0 = 0;
        g_armT0 = now;
        g_seqT0 = 0;
        return FALSE;
    }
    return FALSE;
}

// はぁはぁ / ピクン(連打スパイク) / イク / 突発の刺激。控件シェイクにも使う。
static void CCC_InwomanPulse(DWORD t, double& breath, double& twitch, double& climax)
{
    breath = 0.5 + 0.5 * sin(t / 460.0);

    // ピクン: 線の連続振動ではなく、短い連打スパイク(控件全体が跳ねる用)
    const double cyc = (t % 920) / 920.0;
    twitch = 0.0;
    if (cyc < 0.11)
        twitch = sin(cyc / 0.11 * 3.14159265);
    else if (cyc > 0.15 && cyc < 0.24)
        twitch = sin((cyc - 0.15) / 0.09 * 3.14159265);
    else if (cyc > 0.28 && cyc < 0.34)
        twitch = 0.75 * sin((cyc - 0.28) / 0.06 * 3.14159265);
    twitch *= twitch;

    // 突発の突き刺激(~2.1秒に一瞬) — ロータ等の当て直し感
    {
        const double sp = (t % 2100) / 2100.0;
        if (sp < 0.055) {
            double s = sin(sp / 0.055 * 3.14159265);
            s *= s;
            if (s > twitch) twitch = s;
        }
    }

    // イク: ~3.6秒周期。ピークで白み/赤み＋連ピクン
    const double oc = (t % 3600) / 3600.0;
    climax = 0.0;
    if (oc < 0.26) {
        climax = sin(oc / 0.26 * 3.14159265);
        climax *= climax;
    }
    if (climax > 0.04) {
        const double buzz = 0.6 + 0.4 * sin(t / 22.0);
        if (twitch < climax * buzz)
            twitch = climax * buzz;
    }
}

// ピクン=控件全体が跳ねる。静止時は 0。
// 振幅は最大2px程度に抑え、全面ピンク帯で操作不能にならないようにする。
void CCC_InwomanGetShake(int& dx, int& dy)
{
    dx = 0;
    dy = 0;
    if (!CCC_IsInwoman()) return;
    double breath = 0, twitch = 0, climax = 0;
    const DWORD t = ::GetTickCount();
    CCC_InwomanPulse(t, breath, twitch, climax);
    UNREFERENCED_PARAMETER(breath);
    const double p = (twitch > climax) ? twitch : climax;
    // 弱いピクンは無視。ピーク時だけ小さく跳ねる
    if (p < 0.35) return;
    // 8方向に跳ねる(同じ方向に張り付かない)
    static const int ox[8] = { 1, -1, 0, 0, 1, -1, 1, -1 };
    static const int oy[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
    const int dir = (int)((t / 35) % 8);
    const int amp = (p > 0.75 || climax > 0.55) ? 2 : 1;
    dx = ox[dir] * amp;
    dy = oy[dir] * amp;
}

// mem→画面の最終転送。ピクン時は露出した縁だけ gap で埋めてからずらして BitBlt。
// (全面 FillRect するとピンク帯が並んで操作不能になる)
static void CCC_InwomanBitBlt(HDC dst, int w, int h, HDC src, COLORREF gap)
{
    if (!dst || !src || w <= 0 || h <= 0) return;
    int ox = 0, oy = 0;
    CCC_InwomanGetShake(ox, oy);
    if (ox || oy) {
        HBRUSH br = ::CreateSolidBrush(gap);
        if (br) {
            if (ox > 0) {
                RECT r = { 0, 0, ox, h };
                ::FillRect(dst, &r, br);
            } else if (ox < 0) {
                RECT r = { w + ox, 0, w, h };
                ::FillRect(dst, &r, br);
            }
            if (oy > 0) {
                RECT r = { 0, 0, w, oy };
                ::FillRect(dst, &r, br);
            } else if (oy < 0) {
                RECT r = { 0, h + oy, w, h };
                ::FillRect(dst, &r, br);
            }
            ::DeleteObject(br);
        }
    }
    ::BitBlt(dst, ox, oy, w, h, src, 0, 0, SRCCOPY);
}

// α 付き最終転送。ピクン時は座標だけずらす（隙間塗りは fixer 側が下地済み）。
static void CCC_InwomanAlphaBlend(HDC dst, int w, int h, HDC src)
{
    if (!dst || !src || w <= 0 || h <= 0) return;
    int ox = 0, oy = 0;
    CCC_InwomanGetShake(ox, oy);
    if (ox || oy) {
        // ずらし分の穴は不透明黒ではなく、src の端色に近い塗りを避け dst を一度クリア相当に
        // (fixer 経路は直前に背景塗り済みのことが多いのでずらすだけ)
    }
    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    ::GdiAlphaBlend(dst, ox, oy, w, h, src, 0, 0, w, h, bf);
}

// とろけ顔(ハート目 + 半開きの口 + ほてり + 汗)。絶頂時に隅へ出す"イク顔"。
static void CCC_DrawAhegaoFace(CDC* pDC, int cx, int cy, int sz, double twitch, double climax, BOOL bAeroTrans)
{
    if (!pDC || sz < 8) return;
    const double iku = (climax > twitch) ? climax : twitch;

    // 深い火照り(頬の赤み)— 濃いめにして発情感を強める
    if (!bAeroTrans)
    {
        const int rx = sz / 2 + (int)(sz / 8 * iku), ry = sz / 3 + (int)(sz / 10 * iku);
        const BYTE a = (BYTE)(120 + (int)(60 * iku));
        FillRectAlpha(pDC, CRect(cx - sz / 2 - rx / 2, cy, cx - sz / 6, cy + ry), RGB(255, 50, 110), a);
        FillRectAlpha(pDC, CRect(cx + sz / 6, cy, cx + sz / 2 + rx / 2, cy + ry), RGB(255, 50, 110), a);
    }

    const int eo = sz / 3;
    const int ey = cy - sz / 8 - (int)(sz / 6 * iku); // ビクッ／イクで上に
    const int es = max(4, sz / 2 + (int)(sz / 8 * climax));
    // 目: 普段はとろん半目。ピクン高／イクはハート目で"イってる"
    if (iku > 0.40)
    {
        DrawHeart(pDC, CRect(cx - eo - es / 2, ey - es / 2, cx - eo + es / 2, ey + es / 2), RGB(255, 28, 88));
        DrawHeart(pDC, CRect(cx + eo - es / 2, ey - es / 2, cx + eo + es / 2, ey + es / 2), RGB(255, 28, 88));
    }
    else
    {
        const int er = max(2, es / 2);
        CBrush be(RGB(90, 40, 70));
        CBrush* obe = pDC->SelectObject(&be);
        CGdiObject* ope = pDC->SelectStockObject(NULL_PEN);
        pDC->Ellipse(cx - eo - er, ey - er, cx - eo + er, ey + er);
        pDC->Ellipse(cx + eo - er, ey - er, cx + eo + er, ey + er);
        if (ope) pDC->SelectObject(ope);
        pDC->SelectObject(obe);
        DrawShine(pDC, cx - eo - er / 3, ey - er / 3, max(1, er / 3), max(1, er / 2), RGB(255, 200, 225));
        DrawShine(pDC, cx + eo - er / 3, ey - er / 3, max(1, er / 3), max(1, er / 2), RGB(255, 200, 225));
        CPen lid(PS_SOLID, max(1, sz / 14), RGB(120, 40, 70));
        CPen* opn = pDC->SelectObject(&lid);
        pDC->MoveTo(cx - eo - er, ey - er / 2); pDC->LineTo(cx - eo + er, ey - er / 2);
        pDC->MoveTo(cx + eo - er, ey - er / 2); pDC->LineTo(cx + eo + er, ey - er / 2);
        pDC->SelectObject(opn);
    }

    // 半開きの口 + だらり舌(イクで大きく開く)
    const int my = cy + sz / 3;
    const int mw = max(4, sz * 2 / 5) + (int)(sz / 8 * climax);
    const int mh = max(3, sz / 5) + (int)(sz / 4 * iku);
    CBrush bm(RGB(150, 30, 52));
    CBrush* ob = pDC->SelectObject(&bm);
    CGdiObject* op = pDC->SelectStockObject(NULL_PEN);
    pDC->Ellipse(cx - mw / 2, my - mh / 2, cx + mw / 2, my + mh / 2 + 1);
    CBrush bt(RGB(255, 110, 145));
    pDC->SelectObject(&bt);
    pDC->RoundRect(cx - mw / 4, my, cx + mw / 4, my + mh / 2 + (int)(sz / 3 * (0.45 + iku)), 3, 3);
    if (op) pDC->SelectObject(op);
    pDC->SelectObject(ob);

    // よだれ(口角からたらり。イクで2筋)
    if (!bAeroTrans) {
        FillRectAlpha(pDC, CRect(cx + mw / 2 - 1, my, cx + mw / 2 + 1, my + sz / 3 + (int)(sz / 5 * climax)), RGB(235, 240, 255), 160);
        if (climax > 0.35)
            FillRectAlpha(pDC, CRect(cx - mw / 2 - 1, my + 1, cx - mw / 2 + 1, my + sz / 4), RGB(235, 240, 255), 130);
    }
    DrawShine(pDC, cx - mw / 6, my, max(1, mw / 6), max(1, mh / 4), RGB(255, 200, 220));

    // 汗(こめかみ) — イクで2粒
    CBrush bs(RGB(190, 225, 255));
    CBrush* ob2 = pDC->SelectObject(&bs);
    CGdiObject* op2 = pDC->SelectStockObject(NULL_PEN);
    pDC->Ellipse(cx + sz / 2 - 1, cy - sz / 2, cx + sz / 2 + 2, cy - sz / 2 + 3);
    if (climax > 0.25)
        pDC->Ellipse(cx - sz / 2 - 1, cy - sz / 3, cx - sz / 2 + 2, cy - sz / 3 + 3);
    if (op2) pDC->SelectObject(op2);
    pDC->SelectObject(ob2);
}

// 愛液: XXX(秘部)から出て垂れ・溜まる。ピクン／イクで伸び・飛び散る。
static void CCC_DrawLoveFluid(CDC* pDC, const CRect& rc, double breath, double twitch, double climax, BOOL bAeroTrans)
{
    if (!pDC || rc.Width() < 10 || rc.Height() < 10) return;
    const COLORREF fluid = RGB(242, 246, 255);
    const COLORREF tint  = RGB(255, 210, 236);
    const COLORREF wet   = RGB(255, 170, 210);
    const COLORREF lip   = RGB(255, 140, 180);
    const COLORREF lipIn = RGB(200, 70, 120);
    const double iku = (climax > twitch) ? climax : twitch;

    // --- XXX ソース: 下端中央の秘部(外唇+割れ目)。ここから愛液が出る ---
    const int cx = rc.left + rc.Width() / 2;
    const int srcY = rc.bottom - max(4, rc.Height() / 7);
    const int lipW = max(6, rc.Width() / 7 + (int)(rc.Width() / 40 * breath));
    const int lipH = max(4, rc.Height() / 12 + (int)(2 * twitch));
    {
        CBrush bl(lip);
        CBrush* ob = pDC->SelectObject(&bl);
        CGdiObject* op = pDC->SelectStockObject(NULL_PEN);
        pDC->Ellipse(cx - lipW - lipW / 3, srcY - lipH, cx - lipW / 6, srcY + lipH);
        pDC->Ellipse(cx + lipW / 6, srcY - lipH, cx + lipW + lipW / 3, srcY + lipH);
        CBrush bi(lipIn);
        pDC->SelectObject(&bi);
        const int gap = max(2, lipW / 5) + (int)(2 * climax);
        pDC->Ellipse(cx - gap, srcY - lipH / 2 - (int)(2 * twitch), cx + gap, srcY + lipH + (int)(3 * climax));
        if (op) pDC->SelectObject(op);
        pDC->SelectObject(ob);
        DrawShine(pDC, cx - lipW / 2, srcY - lipH / 3, max(1, lipW / 6), max(1, lipH / 3), RGB(255, 220, 235));
        DrawShine(pDC, cx + lipW / 4, srcY - lipH / 4, max(1, lipW / 7), max(1, lipH / 4), RGB(255, 230, 240));
    }

    // 割れ目直下の溜まり(下端だけ・中央ラベルを残す)
    if (!bAeroTrans)
    {
        const int pool = max(2, rc.Height() / 14 + (int)(rc.Height() / 20 * iku));
        FillRectAlpha(pDC, CRect(cx - lipW, rc.bottom - pool, cx + lipW, rc.bottom), tint, (BYTE)(55 + (int)(50 * breath)));
        FillRectAlpha(pDC, CRect(rc.left + rc.Width() / 5, rc.bottom - pool / 2, rc.right - rc.Width() / 5, rc.bottom), fluid, (BYTE)(40 + (int)(45 * iku)));
    }

    // 割れ目から垂れる糸+しずく(ソースは中央=XXX)。伸びすぎて文字を隠さない。
    const int n = (rc.Width() >= 72) ? 4 : ((rc.Width() >= 40) ? 3 : 2);
    for (int i = 0; i < n; ++i)
    {
        const double u = (i + 0.5) / n;
        int x = cx + (int)((u - 0.5) * lipW * 2.2);
        x += (int)(2 * sin((double)i * 1.7 + breath * 6.2831853) + 2 * twitch * ((i & 1) ? 1 : -1));
        int drip = (int)(rc.Height() * 0.14 * (0.4 + 0.6 * breath)
            + rc.Height() * 0.14 * twitch
            + rc.Height() * 0.18 * climax);
        if (drip < 4) drip = 4;
        if (drip > rc.Height() / 3) drip = rc.Height() / 3;
        const int y0 = srcY + lipH / 2;
        const int y1 = min(rc.bottom, y0 + drip);
        if (!bAeroTrans) {
            FillRectAlpha(pDC, CRect(x - 1, y0, x + 2, y1), fluid, (BYTE)(155 + (int)(50 * iku)));
            if (drip > 10)
                FillRectAlpha(pDC, CRect(x - 2, y0 + drip / 3, x + 3, y0 + drip / 3 + 3), tint, 130);
        }
        const int r = max(2, rc.Width() / 18 + (int)(2 * climax));
        CBrush bb(iku > 0.5 ? wet : tint);
        CBrush* ob = pDC->SelectObject(&bb);
        CGdiObject* op = pDC->SelectStockObject(NULL_PEN);
        pDC->Ellipse(x - r, y1 - r * 2, x + r, y1 + (int)(2 * climax));
        if (op) pDC->SelectObject(op);
        pDC->SelectObject(ob);
        DrawShine(pDC, x - r / 2, y1 - r, max(1, r / 3), max(1, r / 2));
    }

    // イク: XXXから斜めに飛び散る
    if (climax > 0.2 && !bAeroTrans && rc.Width() >= 28)
    {
        const int ns = (rc.Width() >= 64) ? 6 : 4;
        for (int s = 0; s < ns; ++s) {
            const double ang = -1.0 + s * (2.0 / (ns - 1));
            const int len = (int)((rc.Height() / 4 + rc.Width() / 9) * climax);
            const int x0 = cx + (s - ns / 2) * (lipW / 2);
            const int y0 = srcY;
            const int x1 = x0 + (int)(len * sin(ang));
            const int y1 = y0 - (int)(len * cos(ang) * 0.4);
            FillRectAlpha(pDC, CRect(min(x0, x1), min(y0, y1), max(x0, x1) + 2, max(y0, y1) + 2), fluid, (BYTE)(110 + (int)(80 * climax)));
            CBrush bb(tint);
            CBrush* ob = pDC->SelectObject(&bb);
            CGdiObject* op = pDC->SelectStockObject(NULL_PEN);
            const int rr = max(2, 2 + (int)(2 * climax));
            pDC->Ellipse(x1 - rr, y1 - rr, x1 + rr, y1 + rr);
            if (op) pDC->SelectObject(op);
            pDC->SelectObject(ob);
        }
    }
}

// バイブ=ロータ / 電マ / 吸引バイブ / クンニ。時間で切替。愛液まみれ＋振動。
static void CCC_DrawVibrator(CDC* pDC, int cx, int cy, int sz, double t, double twitch, double breath, double climax, BOOL bAeroTrans)
{
    if (!pDC || sz < 8) return;
    const double iku = (climax > twitch) ? climax : twitch;
    cx += (int)(2 * sin(t / 11.0) + 2 * cos(t / 7.0) + 3 * twitch + 4 * climax);
    cy += (int)(1 * sin(t / 9.0) - 2 * twitch - 3 * climax);
    const int kind = ((int)(t / 2600)) & 3; // 約2.6秒で切替。0ロータ 1電マ 2吸引 3クンニ
    const COLORREF body = RGB(236, 110, 188);
    const COLORREF tip  = RGB(255, 190, 230);
    const COLORREF fluid = RGB(240, 244, 255);
    const COLORREF tongue = RGB(255, 130, 160);

    CGdiObject* opN = pDC->SelectStockObject(NULL_PEN);

    if (kind == 0) {
        // ロータ: 小さな楕円エッグ + 短いコード
        const int ew = max(6, sz / 2), eh = max(8, sz * 2 / 3);
        CBrush bb(body);
        CBrush* ob = pDC->SelectObject(&bb);
        pDC->Ellipse(cx - ew / 2, cy - eh / 2, cx + ew / 2, cy + eh / 2);
        CBrush bt(tip);
        pDC->SelectObject(&bt);
        pDC->Ellipse(cx - ew / 3, cy - eh / 2, cx + ew / 3, cy - eh / 6);
        pDC->SelectObject(ob);
        CPen pc(PS_SOLID, max(1, sz / 14), RGB(180, 80, 140));
        CPen* opn = pDC->SelectObject(&pc);
        pDC->MoveTo(cx, cy + eh / 2);
        pDC->LineTo(cx + ew / 2 + 4, cy + eh / 2 + eh / 3);
        pDC->SelectObject(opn);
        DrawShine(pDC, cx - ew / 5, cy - eh / 4, max(1, ew / 5), max(2, eh / 3), RGB(255, 235, 248));
        if (!bAeroTrans)
            FillRectAlpha(pDC, CRect(cx - 1, cy + eh / 2, cx + 2, cy + eh / 2 + (int)(eh * 0.4 * (0.5 + iku))), fluid, 170);
    }
    else if (kind == 1) {
        // 電マ: 太いヘッド + 柄
        const int hw = max(8, sz * 2 / 3), hh = max(7, sz / 2);
        const int sw = max(4, sz / 4), sh = max(8, sz / 2);
        CBrush bh(tip);
        CBrush* ob = pDC->SelectObject(&bh);
        pDC->Ellipse(cx - hw / 2, cy - hh - sh / 4, cx + hw / 2, cy - sh / 4 + hh / 6);
        CBrush bb(body);
        pDC->SelectObject(&bb);
        pDC->RoundRect(cx - sw / 2, cy - sh / 4, cx + sw / 2, cy + sh / 2, sw, sw);
        pDC->SelectObject(ob);
        DrawShine(pDC, cx - hw / 4, cy - hh, max(2, hw / 5), max(2, hh / 3), RGB(255, 255, 255));
        CPen pen(PS_SOLID, 1, RGB(255, 200, 230));
        CPen* opn = pDC->SelectObject(&pen);
        for (int s = 0; s < 2 + (int)(2 * iku); ++s) {
            const int d = hw / 2 + 3 + s * 3;
            pDC->Arc(cx - d, cy - hh - sh / 4 - d / 4, cx + d, cy - sh / 4 + d / 4,
                cx + d, cy - hh / 2, cx - d, cy - hh / 2);
        }
        pDC->SelectObject(opn);
        if (!bAeroTrans)
            FillRectAlpha(pDC, CRect(cx - hw / 3, cy - sh / 4, cx + hw / 3, cy - sh / 4 + (int)(6 + 8 * breath)), fluid, 120);
    }
    else if (kind == 2) {
        // 吸引バイブ: カップ口 + 吸引の脈動リング
        const int rw = max(8, sz * 3 / 5), rh = max(6, sz / 2);
        CBrush bb(body);
        CBrush* ob = pDC->SelectObject(&bb);
        pDC->Ellipse(cx - rw / 2, cy - rh / 2, cx + rw / 2, cy + rh / 2);
        CBrush hole(RGB(90, 30, 55));
        pDC->SelectObject(&hole);
        const int gap = max(3, rw / 3) - (int)(2 * breath);
        pDC->Ellipse(cx - gap, cy - rh / 4, cx + gap, cy + rh / 3);
        pDC->SelectObject(ob);
        CPen pen(PS_SOLID, 1, RGB(255, 180, 220));
        CPen* opn = pDC->SelectObject(&pen);
        const int nR = 2 + (twitch > 0.3 ? 1 : 0) + (climax > 0.3 ? 1 : 0);
        for (int s = 0; s < nR; ++s) {
            const int d = rw / 2 + 2 + s * 3 + (int)(3 * sin(t / 80.0 + s));
            pDC->Ellipse(cx - d, cy - rh / 2 - s, cx + d, cy + rh / 2 + s);
        }
        pDC->SelectObject(opn);
        if (!bAeroTrans)
            FillRectAlpha(pDC, CRect(cx - gap, cy, cx + gap, cy + rh / 2 + (int)(rh * 0.5 * iku)), fluid, 150);
    }
    else {
        // クンニ: 割れ目に舌が這う
        const int lw = max(6, sz / 2), lh = max(5, sz / 3);
        CBrush lipBr(RGB(255, 140, 180));
        CBrush* ob = pDC->SelectObject(&lipBr);
        pDC->Ellipse(cx - lw, cy - lh / 2, cx - 1, cy + lh / 2);
        pDC->Ellipse(cx + 1, cy - lh / 2, cx + lw, cy + lh / 2);
        CBrush tg(tongue);
        pDC->SelectObject(&tg);
        const int lick = (int)(lh * 0.6 * (0.5 + 0.5 * sin(t / 90.0)) + lh * 0.4 * twitch);
        pDC->RoundRect(cx - lw / 4, cy - lick / 2, cx + lw / 4, cy + lh / 2 + lick / 3, 4, 4);
        pDC->SelectObject(ob);
        DrawShine(pDC, cx - 1, cy, max(1, lw / 6), max(1, lh / 3), RGB(255, 220, 230));
        if (!bAeroTrans)
            FillRectAlpha(pDC, CRect(cx - 2, cy + lh / 3, cx + 2, cy + lh / 2 + (int)(lh * (0.5 + iku))), fluid, 180);
    }

    if (opN) pDC->SelectObject(opN);

    for (int i = 0; i < 2 + (climax > 0.25 ? 1 : 0); ++i) {
        const int x = cx + ((i == 0) ? -sz / 6 : ((i == 1) ? sz / 6 : 0));
        int drip = (int)(sz * 0.35 * (0.45 + 0.55 * breath) + sz * 0.4 * twitch + sz * 0.5 * climax);
        if (drip < 4) drip = 4;
        const int bot = cy + sz / 3;
        if (!bAeroTrans)
            FillRectAlpha(pDC, CRect(x - 1, bot, x + 2, bot + drip), fluid, (BYTE)(160 + (int)(50 * iku)));
        const int r = max(2, sz / 10 + (int)(2 * climax));
        CBrush bf(RGB(255, 220, 240));
        CBrush* obf = pDC->SelectObject(&bf);
        CGdiObject* opf = pDC->SelectStockObject(NULL_PEN);
        pDC->Ellipse(x - r, bot + drip - r, x + r, bot + drip + r);
        if (opf) pDC->SelectObject(opf);
        pDC->SelectObject(obf);
    }
}

// 裏演出スチル: RCDATA は IWJ1 ジャム。読み出し時だけ元 PNG に戻す。
static const BYTE kIwJamKey[32] = {
    0xA7, 0x3C, 0x91, 0xE2, 0x5B, 0x08, 0xD4, 0x6F,
    0xC1, 0x2A, 0x77, 0xBE, 0x14, 0x9D, 0xF0, 0x33,
    0x4E, 0x88, 0x1B, 0xC6, 0x59, 0xA0, 0x7D, 0x02,
    0xE5, 0x36, 0xB9, 0x4C, 0x70, 0xAD, 0x18, 0xF3
};

struct CCC_IwBmp {
    HBITMAP hbm = NULL;
    int w = 0;
    int h = 0;
    BOOL tried = FALSE;
};

enum {
    IW_FACE = 0, IW_BODY, IW_BODY2, IW_LACE, IW_BOW, IW_BLUSH, IW_FLUID, IW_ROTOR, IW_VIBE, IW_COUNT
};
static CCC_IwBmp g_iwBmp[IW_COUNT];
static const UINT kIwResId[IW_COUNT] = {
    IDR_IW_FACE, IDR_IW_BODY, IDR_IW_BODY2, IDR_IW_LACE, IDR_IW_BOW, IDR_IW_BLUSH,
    IDR_IW_FLUID, IDR_IW_ROTOR, IDR_IW_VIBE
};

// RCDATA の IWJ1 ジャムを PNG バイト列へ。ヘルプ非掲載の裏リソース。
static BOOL CCC_IwUnjam(const BYTE* src, DWORD n, std::vector<BYTE>& out)
{
    if (!src || n < 8 || memcmp(src, "IWJ1", 4) != 0)
        return FALSE;
    DWORD sz = 0;
    memcpy(&sz, src + 4, 4);
    if (sz == 0 || sz > 8 * 1024 * 1024 || 8 + sz > n) // 8MB 上限（壊れたリソース対策）
        return FALSE;
    out.resize(sz);
    const BYTE* p = src + 8;
    for (DWORD i = 0; i < sz; ++i)
        out[i] = (BYTE)(p[i] ^ kIwJamKey[i % 32] ^ ((i * 13 + 7) & 0xFF));
    return TRUE;
}

// WIC で PNG→32bit PBGRA DIB。COM 未初期化ならここで CoInitialize。
static BOOL CCC_IwDecodePng(const BYTE* png, DWORD n, CCC_IwBmp& b)
{
    if (!png || n < 24)
        return FALSE;
    IWICImagingFactory* fac = NULL;
    IWICStream* stream = NULL;
    IWICBitmapDecoder* dec = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICFormatConverter* conv = NULL;
    BOOL ok = FALSE;
    UINT w = 0, h = 0;
    BITMAPINFO bi = {};
    void* bits = NULL;
    HBITMAP hbm = NULL;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&fac));
    if (FAILED(hr) || !fac) {
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&fac));
    }
    if (FAILED(hr) || !fac) goto done;
    if (FAILED(fac->CreateStream(&stream)) || !stream) goto done;
    if (FAILED(stream->InitializeFromMemory((BYTE*)png, n))) goto done;
    if (FAILED(fac->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnLoad, &dec)) || !dec) goto done;
    if (FAILED(dec->GetFrame(0, &frame)) || !frame) goto done;
    if (FAILED(fac->CreateFormatConverter(&conv)) || !conv) goto done;
    if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
        NULL, 0.0, WICBitmapPaletteTypeCustom))) goto done;
    conv->GetSize(&w, &h);
    if (w < 2 || h < 2 || w > 2048 || h > 2048) goto done; // 異常サイズ拒否
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)w;
    bi.bmiHeader.biHeight = -(LONG)h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    hbm = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbm || !bits) {
        if (hbm) ::DeleteObject(hbm);
        hbm = NULL;
        goto done;
    }
    if (FAILED(conv->CopyPixels(NULL, w * 4, w * h * 4, (BYTE*)bits))) {
        ::DeleteObject(hbm);
        hbm = NULL;
        goto done;
    }
    b.hbm = hbm;
    hbm = NULL;
    b.w = (int)w;
    b.h = (int)h;
    ok = TRUE;
done:
    if (hbm) ::DeleteObject(hbm);
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (stream) stream->Release();
    if (fac) fac->Release();
    return ok;
}

// スチルを遅延ロード。失敗も tried にして再試行しない。
static BOOL CCC_IwEnsure(int idx)
{
    if (idx < 0 || idx >= IW_COUNT)
        return FALSE;
    CCC_IwBmp& b = g_iwBmp[idx];
    if (b.tried)
        return b.hbm != NULL;
    b.tried = TRUE;
    HINSTANCE hi = AfxGetResourceHandle();
    HRSRC hrs = ::FindResource(hi, MAKEINTRESOURCE(kIwResId[idx]), RT_RCDATA);
    if (!hrs)
        return FALSE;
    HGLOBAL hg = ::LoadResource(hi, hrs);
    if (!hg)
        return FALSE;
    const DWORD n = ::SizeofResource(hi, hrs);
    const BYTE* mem = (const BYTE*)::LockResource(hg);
    if (!mem || n < 8)
        return FALSE;
    std::vector<BYTE> png;
    if (!CCC_IwUnjam(mem, n, png))
        return FALSE;
    return CCC_IwDecodePng(png.data(), (DWORD)png.size(), b);
}

// 裏スチルを定数αで拡縮合成。alpha<8 は無視（ノイズ防止）。
static void CCC_IwBlit(CDC* pDC, int x, int y, int dw, int dh, int idx, BYTE alpha)
{
    if (!pDC || dw < 2 || dh < 2 || alpha < 8)
        return;
    if (!CCC_IwEnsure(idx) || !g_iwBmp[idx].hbm)
        return;
    HDC hdc = ::CreateCompatibleDC(pDC->GetSafeHdc());
    if (!hdc)
        return;
    HGDIOBJ old = ::SelectObject(hdc, g_iwBmp[idx].hbm);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    ::GdiAlphaBlend(pDC->GetSafeHdc(), x, y, dw, dh, hdc, 0, 0, g_iwBmp[idx].w, g_iwBmp[idx].h, bf);
    ::SelectObject(hdc, old);
    ::DeleteDC(hdc);
}

// 淫女オーバーレイ: ロータ/電マ/吸引/クンニ＋XXX愛液＋控件ピクン＋イク(白み/赤み)。
// 縁デコ中心にし、中央の文字・クリック領域はなるべく残す。
void CCC_DrawInwoman(CDC* pDC, const CRect& rc, BOOL bAeroTrans)
{
    if (!pDC || !CCC_IsInwoman() || rc.Width() < 8 || rc.Height() < 8)
        return;

    const DWORD t = ::GetTickCount();
    const int W = rc.Width(), H = rc.Height();
    double breath = 0, twitch = 0, climax = 0;
    CCC_InwomanPulse(t, breath, twitch, climax);
    const double iku = (climax > twitch) ? climax : twitch;
    const double heat = min(1.0, breath * 0.45 + twitch * 0.7 + climax * 0.85);

    // --- イク: ほんのり白み＋赤み(文字が読める強さに抑える) ---
    if (!bAeroTrans && climax > 0.22) {
        FillRectAlpha(pDC, rc, RGB(255, 252, 255), (BYTE)(10 + (int)(36 * climax)));
        FillRectAlpha(pDC, rc, RGB(255, 70, 120), (BYTE)(6 + (int)(28 * climax)));
    }

    // --- 発情の火照り(細い縁だけ。太い帯になると操作不能) ---
    if (!bAeroTrans)
    {
        const int g = 18 + (int)(55 * heat);
        const COLORREF hot = (climax > 0.35) ? RGB(255, 35, 95) : RGB(255, 55, 115);
        const int b = max(2, min(5, min(W, H) / 12));
        FillRectAlpha(pDC, CRect(rc.left, rc.top, rc.right, rc.top + b), hot, (BYTE)g);
        FillRectAlpha(pDC, CRect(rc.left, rc.bottom - b, rc.right, rc.bottom), hot, (BYTE)g);
        FillRectAlpha(pDC, CRect(rc.left, rc.top, rc.left + b, rc.bottom), hot, (BYTE)(g * 3 / 4));
        FillRectAlpha(pDC, CRect(rc.right - b, rc.top, rc.right, rc.bottom), hot, (BYTE)(g * 3 / 4));

        if (W >= 40 && H >= 22)
        {
            const int puffs = (W >= 80) ? (2 + (climax > 0.4 ? 1 : 0)) : 1;
            for (int i = 0; i < puffs; ++i)
            {
                const double ph = t / 420.0 + i * 1.15;
                const int px = rc.left + rc.Width() * (i * 2 + 1) / (puffs * 2) + (int)(3 * sin(ph));
                const int rise = (int)((0.5 + 0.5 * sin(ph)) * (H / 8 + (int)(H / 14 * climax)));
                const int py = rc.top + 2 + rise;
                const int pr = max(2, W / 28 + (int)(1 * climax));
                FillRectAlpha(pDC, CRect(px - pr, py - pr, px + pr, py + pr), RGB(255, 245, 250), (BYTE)(18 + (int)(22 * iku)));
            }
        }
    }

    const BYTE aLace = (BYTE)(70 + (int)(50 * heat));
    const BYTE aBody = (BYTE)(48 + (int)(70 * heat) + (int)(40 * climax));
    const BYTE aFace = (BYTE)(80 + (int)(90 * iku));
    const int ox = (int)(1.5 * sin(t / 180.0) + 2 * twitch);
    const int oy = (int)(-1 * climax);

    // ジャム解除スチル。中央の文字は残すので縁・下半分中心。
    if (H >= 14 && W >= 20)
        CCC_IwBlit(pDC, rc.left + 2, rc.bottom - max(8, H / 5), W - 4, max(8, H / 5), IW_LACE, aLace);

    if (H >= 18 && W >= 28) {
        const int bw = max(10, W / 6), bh = max(8, H / 4);
        CCC_IwBlit(pDC, rc.left + 2, rc.bottom - bh - 1, bw, bh, IW_BLUSH, (BYTE)(50 + 40 * heat));
        CCC_IwBlit(pDC, rc.right - bw - 2, rc.bottom - bh - 1, bw, bh, IW_BLUSH, (BYTE)(50 + 40 * heat));
    }

    if (H >= 20 && W >= 32) {
        const int bh = max(12, H * 12 / 20);
        const int bw = max(18, W * 5 / 8);
        CCC_IwBlit(pDC, rc.left + ox, rc.bottom - bh + oy, bw, bh, IW_BODY, aBody);
    }

    if (H >= 26 && W >= 48) {
        const int bw = max(14, W * 2 / 7);
        CCC_IwBlit(pDC, rc.right - bw - 1 + ox, rc.top + 2 + oy, bw, H * 2 / 5, IW_BODY2, (BYTE)(aBody * 3 / 4));
    }

    if (H >= 22 && W >= 40) {
        const int fs = min(min(H * 3 / 5, W * 2 / 5), 110);
        CCC_IwBlit(pDC, rc.left + 1 + ox, rc.top + 1 + oy, fs, fs, IW_FACE, aFace);
    }

    if (H >= 20 && W >= 26) {
        const int fw = max(12, W / 6), fh = max(16, H * 2 / 5);
        CCC_IwBlit(pDC, rc.left + W / 4 - fw / 2, rc.bottom - fh - 1, fw, fh, IW_FLUID, (BYTE)(90 + 90 * iku));
    }

    // トイは裸体の秘所(左下寄り)を避ける。上端・右上へ。
    if (H >= 18 && W >= 24) {
        const int rs = min(min(H * 2 / 5, W / 4), 42);
        CCC_IwBlit(pDC, rc.right - rs - 2 + ox, rc.top + 1 + oy, rs, rs, IW_ROTOR,
            (BYTE)(110 + 80 * heat));
    }
    if (H >= 22 && W >= 36) {
        const int vs = min(min(H * 2 / 5, W / 4), 52);
        CCC_IwBlit(pDC, rc.right - vs - 1 + ox, rc.top + H / 3 + oy, vs, vs, IW_VIBE,
            (BYTE)(100 + 70 * heat));
    }

    if (H >= 20 && W >= 36) {
        const int bs = max(10, min(18, H / 3));
        CCC_IwBlit(pDC, rc.left + W / 2 - bs / 2, rc.top + 1, bs, bs, IW_BOW, (BYTE)(90 + 40 * breath));
    }

    const BOOL havePhoto = CCC_IwEnsure(IW_BODY) || CCC_IwEnsure(IW_FACE);
    const BOOL haveToy = CCC_IwEnsure(IW_ROTOR) || CCC_IwEnsure(IW_VIBE);
    const BOOL haveFluid = CCC_IwEnsure(IW_FLUID);

    // スチルが無いときだけ従来の描画デコ
    if (!haveToy)
    {
        if (W >= 48 && H >= 22)
        {
            const int vs = min(H * 5 / 10, max(10, W * 3 / 10));
            const int vw = max(4, vs / 3);
            const int vx = rc.right - vw / 2 - max(2, W / 18);
            const int vy = rc.top + H / 2;
            CCC_DrawVibrator(pDC, vx, vy, vs, (double)t, twitch, breath, climax, bAeroTrans);
        }
    }
    if (!haveFluid && !havePhoto)
    {
        if (H >= 28 && W >= 36)
            CCC_DrawLoveFluid(pDC, rc, breath, twitch, climax, bAeroTrans);
        if (W >= 72 && H >= 36 && iku > 0.45)
        {
            const int fs = min(H / 4, max(10, W / 9));
            CCC_DrawAhegaoFace(pDC, rc.left + fs / 2 + 3, rc.top + fs / 2 + 2, fs, twitch, climax, bAeroTrans);
        }
    }

    // --- 汗 ---
    if (H >= 16)
    {
        const int sx = rc.left + 4 + (int)(2 * sin(t / 70.0) + 1 * twitch);
        const int sy = rc.top + 3 + (int)(3 * (0.5 + 0.5 * sin(t / 95.0)));
        CBrush bb(RGB(190, 225, 255));
        CBrush* ob = pDC->SelectObject(&bb);
        CGdiObject* op = pDC->SelectStockObject(NULL_PEN);
        pDC->Ellipse(sx - 1, sy - 2, sx + 2, sy + 2);
        if (climax > 0.35)
            pDC->Ellipse(sx + 5, sy + 3, sx + 8, sy + 7);
        if (op) pDC->SelectObject(op);
        pDC->SelectObject(ob);
        DrawShine(pDC, sx - 1, sy - 1, 1, 1);
    }
}

// aero=0 の窓本体（控件の隙間）。キャプション帯は呼ばない側で除く。
static void CCC_DrawInwomanDlgBody(CDC* pDC, const CRect& rc)
{
    if (!pDC || !CCC_IsInwoman() || rc.Width() < 48 || rc.Height() < 36)
        return;

    const DWORD t = ::GetTickCount();
    double breath = 0, twitch = 0, climax = 0;
    CCC_InwomanPulse(t, breath, twitch, climax);
    const double iku = (climax > twitch) ? climax : twitch;
    const double heat = min(1.0, breath * 0.45 + twitch * 0.7 + climax * 0.85);
    const int W = rc.Width(), H = rc.Height();
    const int ox = (int)(3 * sin(t / 220.0) + 4 * twitch);
    const int oy = (int)(-2 * climax);

    if (!CCC_IsAeroEnabled() && climax > 0.18) {
        FillRectAlpha(pDC, rc, RGB(255, 70, 120), (BYTE)(8 + (int)(22 * climax)));
    }

    const int bodyH = max(40, H * 52 / 100);
    const int bodyW = max(80, W * 56 / 100);
    CCC_IwBlit(pDC, rc.left + ox, rc.bottom - bodyH + oy, bodyW, bodyH, IW_BODY,
        (BYTE)(70 + (int)(70 * heat)));

    const int face = min(min(W / 3, H / 2), 220);
    CCC_IwBlit(pDC, rc.left + 6 + ox, rc.top + 8 + oy, face, face, IW_FACE,
        (BYTE)(90 + (int)(80 * iku)));

    const int sideW = max(48, W / 5);
    CCC_IwBlit(pDC, rc.right - sideW - 8 + ox, rc.top + 8 + oy, sideW, H * 2 / 5, IW_BODY2,
        (BYTE)(60 + (int)(50 * heat)));

    // 電マ・ロータは裸体の下腹部を避ける（右上／右中）
    const int vh = min(140, max(52, H / 4));
    CCC_IwBlit(pDC, rc.right - vh - 10 + ox, rc.top + 10 + oy, vh, vh, IW_VIBE,
        (BYTE)(120 + (int)(80 * heat)));

    const int rh = min(80, max(32, H / 7));
    CCC_IwBlit(pDC, rc.right - rh - 18 + ox, rc.top + H * 38 / 100 + oy, rh, rh, IW_ROTOR,
        (BYTE)(130 + (int)(70 * heat)));

    const int fh = max(36, H / 6);
    const int fw = max(40, bodyW / 3);
    CCC_IwBlit(pDC, rc.left + bodyW / 2 - fw / 2 + ox, rc.bottom - fh + oy, fw, fh, IW_FLUID,
        (BYTE)(100 + (int)(90 * iku)));

    CCC_IwBlit(pDC, rc.left, rc.bottom - max(16, H / 14), W, max(16, H / 14), IW_LACE,
        (BYTE)(90 + (int)(50 * heat)));
}

// aero=0 の GDI キャンバスへ裸体オーバーレイ。アクリル時はガラスと干渉するので描かない。
void CCC_DrawInwomanOnRect(CDC* pDC, const CRect& rc)
{
    if (!pDC || !CCC_IsInwoman() || CCC_IsAeroEnabled())
        return;
    CCC_DrawInwomanDlgBody(pDC, rc);
}

// クライアント本文（キャプション帯を除く）へ。ピアノロール等の GDI 面。
void CCC_DrawInwomanOnClient(CDC* pDC, HWND hWnd)
{
    if (!pDC || !hWnd || !CCC_IsInwoman() || CCC_IsAeroEnabled())
        return;
    CRect r;
    ::GetClientRect(hWnd, &r);
    const int capH = CCC_GetCustomCaptionHeight(hWnd);
    if (capH > 0 && r.Height() > capH)
        r.top = capH;
    CCC_DrawInwomanDlgBody(pDC, r);
}

// カスタムキャプション描画の前に本文へ淫女を重ねる（aero=0 のみ中で弾く）。
void CCC_CaptionPaintGdi(CDC& dc, HWND hDlg)
{
    CCC_DrawInwomanOnClient(&dc, hDlg);
    CCC_CaptionPaint(dc, hDlg);
}

// ソリッドダイアログの WM_PAINT。本文を地色で塗ってから淫女。キャプション帯は塗らない。
static void DlgPaintSolidInwoman(CWnd* pWnd)
{
    CPaintDC dc(pWnd);
    CRect r;
    pWnd->GetClientRect(&r);
    const int capH = CCC_GetCustomCaptionHeight(pWnd->m_hWnd);
    if (capH > 0 && r.Height() > capH)
        r.top = capH;
    dc.FillSolidRect(&r, COLOR_DIALOG_BG);
    CCC_DrawInwomanDlgBody(&dc, r);
}

// ============================================================================
// 子コントロールを一括でサブクラス化する処理
// ============================================================================
// 未サブクラスの可視子をクラス名で CCustom* に置換。既に CWnd がある子は触らない。
// アイコン/ビットマップ Static は描画が消えるので除外。SetAeroMode(FALSE)=ソリッド。
template<typename DlgBase>
static void DoSubclassChildControls(DlgBase* pDlg)
{
    CCC_StartInwomanTimer();
    HWND hc = ::GetWindow(pDlg->m_hWnd, GW_CHILD);
    while (hc)
    {
        if (!::IsWindow(hc)) break;
        if (!(::GetWindowLong(hc, GWL_STYLE) & WS_VISIBLE))
        {
            hc = ::GetWindow(hc, GW_HWNDNEXT);
            continue;
        }

        CWnd* pw = CWnd::FromHandlePermanent(hc);
        if (pw && pw != pDlg)
        {
            hc = ::GetWindow(hc, GW_HWNDNEXT);
            continue;
        }

        TCHAR cls[256];
        ::GetClassName(hc, cls, 256);

        // クラス名に応じてカスタムコントロールへ置換
        if (_tcsicmp(cls, _T("Edit")) == 0)
        {
            CCustomEdit* p = new CCustomEdit();
            p->EnableAutoDelete();
            p->SubclassWindow(hc);
        }
        else if (_tcsicmp(cls, _T("Static")) == 0)
        {
            const LONG ls = ::GetWindowLong(hc, GWL_STYLE);
            const UINT st = (UINT)(ls & SS_TYPEMASK);
            // アイコン/ビットマップ Static は CCustomStatic 化すると描画されなくなる
            if (st == SS_ICON || st == SS_BITMAP)
            {
                hc = ::GetWindow(hc, GW_HWNDNEXT);
                continue;
            }
            CCustomStatic* p = new CCustomStatic();
            p->EnableAutoDelete();
            p->SetAeroMode(FALSE);
            p->SubclassWindow(hc);
        }
        else if (_tcsicmp(cls, _T("ListBox")) == 0)
        {
            CCustomListBox* p = new CCustomListBox();
            p->EnableAutoDelete();
            p->SetAeroMode(FALSE);
            p->SubclassWindow(hc);
        }
        else if (_tcsicmp(cls, _T("ComboBox")) == 0)
        {
            CCustomComboBox* p = new CCustomComboBox();
            p->EnableAutoDelete();
            p->SetAeroMode(FALSE);
            p->SubclassWindow(hc);
        }
		else if (_tcsicmp(cls, WC_LISTVIEW) == 0)
		{
			CCustomListCtrl* p = new CCustomListCtrl();
			p->EnableAutoDelete();
			p->SetAeroMode(FALSE);
			p->SubclassWindow(hc);
		}
		else if (_tcsicmp(cls, WC_TREEVIEW) == 0)
		{
			CCustomTreeCtrl* p = new CCustomTreeCtrl();
			p->EnableAutoDelete();
			p->SetAeroMode(FALSE);
			p->SubclassWindow(hc);
		}
		else if (_tcsicmp(cls, WC_TABCONTROL) == 0)
		{
			CCustomTabCtrl* p = new CCustomTabCtrl();
			p->EnableAutoDelete();
			p->SetAeroMode(FALSE);
			p->SubclassWindow(hc);
		}
		else if (_tcsicmp(cls, _T("Button")) == 0)
        {
            LONG ls = ::GetWindowLong(hc, GWL_STYLE);
            UINT nt = ls & BS_TYPEMASK;
            if (nt == BS_GROUPBOX)
            {
                CCustomGroupBox* p = new CCustomGroupBox();
                p->EnableAutoDelete();
                p->SetAeroMode(FALSE);
                p->SubclassWindow(hc);
            }
            else if (nt == BS_PUSHBUTTON || nt == BS_DEFPUSHBUTTON || (ls & BS_PUSHLIKE))
            {
                CCustomStandardButton* p = new CCustomStandardButton();
                p->EnableAutoDelete();
                p->SubclassWindow(hc);
            }
            else
            {
                CCustomCheckBox* p = new CCustomCheckBox();
                p->EnableAutoDelete();
                p->SetAeroMode(FALSE);
                p->SubclassWindow(hc);
            }
        }
        else if (_tcsicmp(cls, TRACKBAR_CLASS) == 0)
        {
            LONG ls = ::GetWindowLong(hc, GWL_STYLE);
            if (ls & TBS_ENABLESELRANGE)
            {
                CCustomRangeSliderCtrl* p = new CCustomRangeSliderCtrl();
                p->EnableAutoDelete();
                p->SetAeroMode(FALSE);
                p->SubclassWindow(hc);
            }
            else
            {
                CCustomSliderCtrl* p = new CCustomSliderCtrl();
                p->EnableAutoDelete();
                p->SetAeroMode(FALSE);
                p->SubclassWindow(hc);
            }
        }
        hc = ::GetWindow(hc, GW_HWNDNEXT);
    }
}

// ============================================================================
// ダイアログ共通処理
// ============================================================================

// WM_CTLCOLOR 共通。Win11 アクリルは DLG/STATIC/BTN を NULL_BRUSH（ガラス残し）。
// EDIT/LISTBOX は不透明ブラシ（文字可読）。コンボ内 Edit は COMBO_BG。
static HBRUSH DlgOnCtlColor(CDC* pDC, CWnd* pWnd, UINT nC, CBrush& brDlg, BOOL bAeroEnabled)
{
#if CCUSTOM_AERO_SUPPORT
    if (bAeroEnabled && CCC_IsWin11())
    {
        static CBrush brushEdit(COLOR_EDIT_BG);
        static CBrush brushList(COLOR_LIST_BG);
        if (nC == CTLCOLOR_DLG)
        {
            pDC->SetBkMode(TRANSPARENT);
            return (HBRUSH)GetStockObject(NULL_BRUSH); // ガラスを塗らない
        }
        if (nC == CTLCOLOR_EDIT)
        {
            pDC->SetBkMode(OPAQUE);
            pDC->SetBkColor(COLOR_EDIT_BG);
            pDC->SetTextColor(COLOR_EDIT_TEXT);
            return (HBRUSH)brushEdit.GetSafeHandle();
        }
        if (nC == CTLCOLOR_LISTBOX)
        {
            pDC->SetBkMode(OPAQUE);
            pDC->SetBkColor(COLOR_LIST_BG);
            pDC->SetTextColor(COLOR_EDIT_TEXT);
            return (HBRUSH)brushList.GetSafeHandle();
        }
        if (nC == CTLCOLOR_STATIC || nC == CTLCOLOR_BTN)
        {
            pDC->SetBkMode(TRANSPARENT);
            pDC->SetTextColor(RGB(0, 0, 0));
            return (HBRUSH)GetStockObject(NULL_BRUSH);
        }
    }
    else if (bAeroEnabled)
    {
        static CBrush brushBg(RGB(248, 248, 248));
        if (nC == CTLCOLOR_DLG || nC == CTLCOLOR_STATIC || nC == CTLCOLOR_BTN)
        {
            pDC->SetBkMode(TRANSPARENT);
            pDC->SetTextColor(RGB(0, 0, 0));
            return (HBRUSH)brushBg.GetSafeHandle();
        }
        if (nC == CTLCOLOR_LISTBOX || nC == CTLCOLOR_EDIT)
        {
            pDC->SetBkMode(OPAQUE);
            pDC->SetTextColor(RGB(0, 0, 0));
            pDC->SetBkColor(RGB(255, 255, 255));
            return (HBRUSH)GetStockObject(WHITE_BRUSH);
        }
    }
#endif
    if (nC == CTLCOLOR_DLG) return (HBRUSH)brDlg.GetSafeHandle();

    if (nC == CTLCOLOR_EDIT)
    {
        CWnd* pP = pWnd->GetParent();
        if (pP)
        {
            TCHAR sz[256];
            ::GetClassName(pP->m_hWnd, sz, 256);
            if (_tcsicmp(sz, _T("ComboBox")) == 0)
            {
                pDC->SetBkColor(COLOR_COMBO_BG);
                pDC->SetTextColor(RGB(0, 0, 0));
                static CBrush bC(COLOR_COMBO_BG);
                return (HBRUSH)bC.GetSafeHandle();
            }
        }
        pDC->SetBkColor(COLOR_EDIT_BG);
        pDC->SetTextColor(RGB(0, 0, 0));
        static CBrush bE(COLOR_EDIT_BG);
        return (HBRUSH)bE.GetSafeHandle();
    }

    if (nC == CTLCOLOR_LISTBOX)
    {
        pDC->SetBkColor(COLOR_COMBO_BG);
        pDC->SetTextColor(RGB(0, 0, 0));
        static CBrush bL(COLOR_COMBO_BG);
        return (HBRUSH)bL.GetSafeHandle();
    }

    if (nC == CTLCOLOR_STATIC || nC == CTLCOLOR_BTN)
    {
        pDC->SetBkColor(COLOR_DIALOG_BG);
        pDC->SetTextColor(RGB(0, 0, 0));
        pDC->SetBkMode(TRANSPARENT);
        return (HBRUSH)brDlg.GetSafeHandle();
    }
    return NULL;
}

// ============================================================================
// カスタムエディットコントロール
// CCustomEdit
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomEdit, CEdit)

BEGIN_MESSAGE_MAP(CCustomEdit, CEdit)
    ON_WM_CTLCOLOR_REFLECT()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_NCPAINT()
    ON_WM_SETFOCUS()
    ON_WM_KILLFOCUS()
    ON_WM_TIMER()
    ON_WM_SHOWWINDOW()
    ON_WM_KEYDOWN()
    ON_WM_KEYUP()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSEWHEEL()
    ON_WM_VSCROLL()
    ON_WM_HSCROLL()
    ON_CONTROL_REFLECT(EN_UPDATE, OnEnUpdate)
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
    ON_MESSAGE(CCC_WM_POST_OPAQUE_PAINT, OnPostOpaquePaint)
    ON_MESSAGE(WM_IME_STARTCOMPOSITION, OnImeStartComposition)
    ON_MESSAGE(WM_IME_COMPOSITION, OnImeComposition)
    ON_MESSAGE(WM_IME_NOTIFY, OnImeNotify)
END_MESSAGE_MAP()

static const UINT_PTR kEditOpaqueTimerId = 4107; // 互換用（未使用）
static const UINT_PTR kEditSelTimerId = 4108;
static const UINT_PTR kEditCaretTimerId = 4109; // 自前キャレット点滅
static const UINT_PTR kButtonAnimTimerId    = 4120; // ボタンの流れるツヤ/鼓動パルス
static const UINT_PTR kCheckBounceTimerId   = 4121; // チェックON時のバウンス
static const UINT_PTR kCheckHoverTimerId    = 4123; // ホバー中レ点／リボンゆっくり動き
static const UINT_PTR kButtonSoftTimerId    = 4124; // Soft3D 常時（間引き 220ms）
static const UINT_PTR kSliderShimmerTimerId = 4122; // スライダーの流れるシマー
static const UINT_PTR kListSoftTimerId      = 4125; // 選択/ホバー♡ Soft3D 回転
// ♡ は矩形だけ再描画するので短周期でも軽い。step を小さくすると速く回る。
static const UINT kListHeartTimerMs = 30;
static const int kListHeartStepMs = 26;
static const UINT_PTR kGroupSoftTimerId     = 4126; // GroupBox ゆらゆら（500ms）
static const UINT_PTR kTabSoftTimerId       = 4127; // 選択タブ Soft（220ms）

// 標準 CEdit をオーナードロー化。アクリル下は BufferedPaint 不透明面 + 自前キャレット。
CCustomEdit::CCustomEdit()
    : m_bHasFocus(FALSE), m_bAutoDelete(FALSE), m_bAeroMode(FALSE), m_bSelDrag(FALSE), m_bCaretOn(TRUE)
    , m_lastSel0(-1), m_lastSel1(-1)
{
    m_brBackground.CreateSolidBrush(COLOR_EDIT_BG);
}

// フォント/ブラシのみ解放。HWND は PostNcDestroy。
CCustomEdit::~CCustomEdit()
{
    if (m_fontBold.GetSafeHandle()) m_fontBold.DeleteObject();
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

// DoSubclassChildControls 経由は EnableAutoDelete 済み。ここで delete this。
void CCustomEdit::PostNcDestroy()
{
    CEdit::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

// 親フォントをコピーして SetFont。ウィンドウ紐づけ HFONT は Delete しない（例外の温床）。
void CCustomEdit::PreSubclassWindow()
{
    CEdit::PreSubclassWindow();
    CWnd* pP = GetParent();
    if (!pP) return;

    CFont* pF = pP->GetFont();
    if (!pF) return;

    // 親フォントを内部キャッシュへ。太字固定はしない(明示 SetFont で上書きされる)。
    // SetFont オーバーライドは作らない: ウィンドウに紐づいた HFONT を
    // DeleteObject すると後続で CInvalidArgException になり得るため。
    LOGFONT lf = {};
    if (!pF->GetLogFont(&lf)) return;
    lf.lfWeight = FW_NORMAL;
    if (m_fontBold.GetSafeHandle()) m_fontBold.DeleteObject();
    if (m_fontBold.CreateFontIndirect(&lf))
        CEdit::SetFont(&m_fontBold);
}

// 反射 CTLCOLOR。既定描画が残る経路用。アクリル本体は PaintOpaqueClient。
HBRUSH CCustomEdit::CtlColor(CDC* pDC, UINT)
{
    pDC->SetBkColor(COLOR_EDIT_BG);
    pDC->SetTextColor(COLOR_EDIT_TEXT);
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

// 複数行の可視範囲だけ描く。選択ハイライトは行跨ぎ対応。パスワードは呼び出し側で置換済み。
void CCustomEdit::DrawMultilineVisibleText(CDC& dc, const CRect& rc)
{
    // スクロール位置を EM_POSFROMCHAR / FIRSTVISIBLELINE で反映（全文明け DrawText はスクロール無視）
    TEXTMETRIC tm = {};
    dc.GetTextMetrics(&tm);
    int lineH = tm.tmHeight + tm.tmExternalLeading;
    if (lineH < 1) lineH = 12;

    CString text;
    GetWindowText(text);
    const int tlen = text.GetLength();
    int sel0 = 0, sel1 = 0;
    GetSel(sel0, sel1);
    if (sel0 > sel1) { const int t = sel0; sel0 = sel1; sel1 = t; }
    if (sel0 < 0) sel0 = 0;
    if (sel1 < 0) sel1 = 0;
    if (sel0 > tlen) sel0 = tlen;
    if (sel1 > tlen) sel1 = tlen;
    const BOOL hasSel = (sel0 < sel1) && m_bHasFocus;

    auto posFromChar = [&](int idx, int& x, int& y) -> BOOL {
        x = rc.left;
        y = rc.top;
        if (tlen <= 0) return FALSE;
        if (idx < 0) idx = 0;
        BOOL pastEnd = FALSE;
        if (idx >= tlen) {
            idx = tlen - 1;
            pastEnd = TRUE;
        }
        LRESULT lr = SendMessage(EM_POSFROMCHAR, (WPARAM)idx, 0);
        if (lr == (LRESULT)-1)
            return FALSE;
        x = (short)LOWORD(lr);
        y = (short)HIWORD(lr);
        if (pastEnd) {
            CSize ch = dc.GetTextExtent(text.Mid(idx, 1));
            x += ch.cx;
        }
        return TRUE;
    };

    const int savedDc = dc.SaveDC();
    dc.IntersectClipRect(&rc);

    int xSel0 = rc.left, ySel0 = rc.top, xSel1 = rc.left, ySel1 = rc.top;
    if (hasSel) {
        posFromChar(sel0, xSel0, ySel0);
        posFromChar(sel1, xSel1, ySel1);
        if (ySel0 == ySel1) {
            int x0 = xSel0, x1 = xSel1;
            if (x1 < x0) { const int t = x0; x0 = x1; x1 = t; }
            if (x1 <= x0) x1 = x0 + 2;
            CRect hi(x0, ySel0, x1, ySel0 + lineH);
            if (hi.IntersectRect(&hi, &rc) && hi.Width() > 0)
                dc.FillSolidRect(&hi, COLOR_EDIT_SEL_BG);
        } else {
            const int yStart = (ySel0 < ySel1) ? ySel0 : ySel1;
            const int yEnd = (ySel0 > ySel1) ? ySel0 : ySel1;
            for (int y = yStart; y <= yEnd; y += lineH) {
                int xL = rc.left, xR = rc.right;
                if (y == ySel0) xL = xSel0;
                if (y == ySel1) xR = xSel1;
                if (y == ySel0 && ySel0 > ySel1) { xL = rc.left; xR = xSel0; }
                if (y == ySel1 && ySel1 > ySel0) { xL = rc.left; xR = xSel1; }
                if (y != ySel0 && y != ySel1) { xL = rc.left; xR = rc.right; }
                if (xR < xL) { const int t = xL; xL = xR; xR = t; }
                if (xR <= xL) continue;
                CRect hi(xL, y, xR, y + lineH);
                if (hi.IntersectRect(&hi, &rc) && hi.Width() > 0)
                    dc.FillSolidRect(&hi, COLOR_EDIT_SEL_BG);
            }
        }
    }

    dc.SetBkMode(TRANSPARENT);
    const int first = (int)SendMessage(EM_GETFIRSTVISIBLELINE, 0, 0);
    const int nLines = GetLineCount();
    for (int li = first; li < nLines; ++li) {
        const int idx = (int)SendMessage(EM_LINEINDEX, (WPARAM)li, 0);
        if (idx < 0) break;
        LRESULT lr = SendMessage(EM_POSFROMCHAR, (WPARAM)idx, 0);
        if (lr == (LRESULT)-1)
            continue;
        const int x = (short)LOWORD(lr);
        const int y = (short)HIWORD(lr);
        if (y >= rc.bottom)
            break;
        if (y + lineH < rc.top)
            continue;

        int maxc = (int)SendMessage(EM_LINELENGTH, (WPARAM)idx, 0);
        if (maxc < 0) maxc = 0;
        CString line;
        if (maxc > 0) {
            TCHAR* buf = line.GetBuffer(maxc + 4);
            *((WORD*)buf) = (WORD)(maxc + 2); // EM_GETLINE: 先頭 WORD にバッファ文字数
            const int got = (int)SendMessage(EM_GETLINE, (WPARAM)li, (LPARAM)buf);
            line.ReleaseBuffer(got > 0 ? got : 0);
            while (!line.IsEmpty()) {
                const TCHAR c = line[line.GetLength() - 1];
                if (c != _T('\r') && c != _T('\n')) break;
                line.Truncate(line.GetLength() - 1);
            }
        }

        const int lineEnd = idx + line.GetLength();
        if (hasSel && sel0 < lineEnd && sel1 > idx) {
            const int a = (sel0 > idx) ? sel0 : idx;
            const int b = (sel1 < lineEnd) ? sel1 : lineEnd;
            CString pre = line.Left(a - idx);
            CString mid = line.Mid(a - idx, b - a);
            CString post = line.Mid(b - idx);
            int cx = x;
            if (!pre.IsEmpty()) {
                dc.SetTextColor(COLOR_EDIT_TEXT);
                dc.ExtTextOut(cx, y, ETO_CLIPPED, &rc, pre, pre.GetLength(), NULL);
                cx += dc.GetTextExtent(pre).cx;
            }
            if (!mid.IsEmpty()) {
                dc.SetTextColor(COLOR_EDIT_SEL_TEXT);
                dc.ExtTextOut(cx, y, ETO_CLIPPED, &rc, mid, mid.GetLength(), NULL);
                cx += dc.GetTextExtent(mid).cx;
            }
            if (!post.IsEmpty()) {
                dc.SetTextColor(COLOR_EDIT_TEXT);
                dc.ExtTextOut(cx, y, ETO_CLIPPED, &rc, post, post.GetLength(), NULL);
            }
        } else {
            dc.SetTextColor(COLOR_EDIT_TEXT);
            if (!line.IsEmpty())
                dc.ExtTextOut(x, y, ETO_CLIPPED, &rc, line, line.GetLength(), NULL);
        }
    }

    dc.RestoreDC(savedDc);
}

// クライアント文字+選択+キャレット。ES_PASSWORD は U+25CF。単一行の POSFROMCHAR は (short) 必須。
void CCustomEdit::DrawClientText(CDC& dc, const CRect& r)
{
    CString text;
    GetWindowText(text);
    // パスワード表示
    const DWORD style = (DWORD)GetStyle();
    if (style & ES_PASSWORD) {
        const TCHAR bullet = (TCHAR)0x25CF; // ●
        CString bullets;
        const int n = text.GetLength();
        for (int i = 0; i < n; ++i) bullets += bullet;
        text = bullets;
    }

    CFont* pFont = GetFont();
    CFont* pOld = pFont ? dc.SelectObject(pFont) : nullptr;

    dc.SetBkColor(COLOR_EDIT_BG);
    dc.SetTextColor(COLOR_EDIT_TEXT);
    dc.SetBkMode(OPAQUE);

    CRect rc = r;
    rc.DeflateRect(3, 1);

    if (style & ES_MULTILINE) {
        DrawMultilineVisibleText(dc, rc);
        if (pOld) dc.SelectObject(pOld);
        DrawCaretIfNeeded(dc);
        return;
    }

    UINT fmt = DT_NOPREFIX | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER;
    if (style & ES_CENTER)
        fmt |= DT_CENTER;
    else if (style & ES_RIGHT)
        fmt |= DT_RIGHT;
    else
        fmt |= DT_LEFT;

    int sel0 = 0, sel1 = 0;
    GetSel(sel0, sel1);
    const int tlen = text.GetLength();
    if (sel0 > sel1) { const int t = sel0; sel0 = sel1; sel1 = t; }
    if (sel0 < 0) sel0 = 0;
    if (sel1 < 0) sel1 = 0;
    if (sel0 > tlen) sel0 = tlen;
    if (sel1 > tlen) sel1 = tlen;
    const BOOL hasSel = (sel0 < sel1) && m_bHasFocus;

    // EM_POSFROMCHAR は負座標を返し得る → (short) 必須
    auto posFromChar = [&](int idx, int& x, int& y) -> BOOL {
        x = rc.left;
        y = rc.top;
        if (tlen <= 0) return FALSE;
        if (idx < 0) idx = 0;
        BOOL pastEnd = FALSE;
        if (idx >= tlen) {
            idx = tlen - 1;
            pastEnd = TRUE;
        }
        LRESULT lr = SendMessage(EM_POSFROMCHAR, (WPARAM)idx, 0);
        if (lr == (LRESULT)-1)
            return FALSE;
        x = (short)LOWORD(lr);
        y = (short)HIWORD(lr);
        if (pastEnd) {
            CSize ch = dc.GetTextExtent(text.Mid(idx, 1));
            x += ch.cx;
        }
        return TRUE;
    };

    if (!hasSel) {
        dc.DrawText(text, &rc, fmt);
        if (pOld) dc.SelectObject(pOld);
        DrawCaretIfNeeded(dc);
        return;
    }

    TEXTMETRIC tm = {};
    dc.GetTextMetrics(&tm);
    const int lineH = tm.tmHeight > 0 ? tm.tmHeight : rc.Height();

    const int savedDc = dc.SaveDC();
    dc.IntersectClipRect(&rc);

    int xSel0 = rc.left, ySel0 = rc.top, xSel1 = rc.left, ySel1 = rc.top;
    const BOOL ok0 = posFromChar(sel0, xSel0, ySel0);
    const BOOL ok1 = posFromChar(sel1, xSel1, ySel1);
    if (!ok0 || !ok1) {
        CSize all = dc.GetTextExtent(text);
        CSize pre0 = dc.GetTextExtent(text.Left(sel0));
        CSize pre1 = dc.GetTextExtent(text.Left(sel1));
        int baseX = rc.left;
        if (style & ES_CENTER) baseX = rc.left + (rc.Width() - all.cx) / 2;
        else if (style & ES_RIGHT) baseX = rc.right - all.cx;
        if (!ok0) { xSel0 = baseX + pre0.cx; ySel0 = rc.top + (rc.Height() - lineH) / 2; }
        if (!ok1) { xSel1 = baseX + pre1.cx; ySel1 = ySel0; }
    }

    if (!(style & (ES_CENTER | ES_RIGHT))) {
        int xText = rc.left, yText = ySel0;
        if (!posFromChar(0, xText, yText)) {
            xText = rc.left;
            yText = rc.top + (rc.Height() - lineH) / 2;
        }
        int x0 = xSel0, x1 = xSel1;
        if (x1 < x0) { const int t = x0; x0 = x1; x1 = t; }
        if (x1 <= x0) x1 = x0 + 2;
        CRect hi(x0, yText, x1, yText + lineH);
        if (hi.IntersectRect(&hi, &rc) && hi.Width() > 0)
            dc.FillSolidRect(&hi, COLOR_EDIT_SEL_BG);

        dc.SetBkMode(TRANSPARENT);
        CString pre = text.Left(sel0);
        CString mid = text.Mid(sel0, sel1 - sel0);
        CString post = text.Mid(sel1);
        if (!pre.IsEmpty()) {
            dc.SetTextColor(COLOR_EDIT_TEXT);
            dc.ExtTextOut(xText, yText, ETO_CLIPPED, &rc, pre, pre.GetLength(), NULL);
        }
        if (!mid.IsEmpty()) {
            dc.SetTextColor(COLOR_EDIT_SEL_TEXT);
            dc.ExtTextOut(xSel0, yText, ETO_CLIPPED, &rc, mid, mid.GetLength(), NULL);
        }
        if (!post.IsEmpty()) {
            dc.SetTextColor(COLOR_EDIT_TEXT);
            dc.ExtTextOut(xSel1, yText, ETO_CLIPPED, &rc, post, post.GetLength(), NULL);
        }
    } else {
        int x0 = xSel0, x1 = xSel1;
        if (x1 < x0) { const int t = x0; x0 = x1; x1 = t; }
        if (x1 <= x0) x1 = x0 + 2;
        CRect hi(x0, ySel0, x1, ySel0 + lineH);
        if (hi.IntersectRect(&hi, &rc) && hi.Width() > 0)
            dc.FillSolidRect(&hi, COLOR_EDIT_SEL_BG);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(COLOR_EDIT_TEXT);
        dc.DrawText(text, &rc, fmt & ~DT_END_ELLIPSIS);
        CString mid = text.Mid(sel0, sel1 - sel0);
        if (!mid.IsEmpty() && hi.Width() > 0) {
            dc.SetTextColor(COLOR_EDIT_SEL_TEXT);
            dc.ExtTextOut(xSel0, ySel0, ETO_CLIPPED, &hi, mid, mid.GetLength(), NULL);
        }
    }

    dc.RestoreDC(savedDc);
    if (pOld)
        dc.SelectObject(pOld);
    DrawCaretIfNeeded(dc);
}

// 自前キャレット座標。EM_POSFROMCHAR 失敗時は GetTextExtent で補う。DPI で幅 2px 以上。
BOOL CCustomEdit::GetCaretClientPos(CPoint& pt, int& lineH)
{
    pt.x = 0;
    pt.y = 0;
    lineH = 16;
    if (!GetSafeHwnd())
        return FALSE;

    CRect r;
    GetClientRect(&r);
    CRect fmt;
    SendMessage(EM_GETRECT, 0, (LPARAM)&fmt);
    if (fmt.Width() <= 0 || fmt.Height() <= 0) {
        fmt = r;
        fmt.DeflateRect(3, 1);
    }

    HDC hdcRaw = ::GetDC(m_hWnd);
    if (!hdcRaw)
        return FALSE;
    CDC dc;
    dc.Attach(hdcRaw);
    CFont* pFont = GetFont();
    CFont* pOld = pFont ? dc.SelectObject(pFont) : nullptr;
    TEXTMETRIC tm = {};
    dc.GetTextMetrics(&tm);
    lineH = tm.tmHeight + tm.tmExternalLeading;
    if (lineH < 1) lineH = 16;
    const int aveW = (tm.tmAveCharWidth > 0) ? tm.tmAveCharWidth : 8;

    CString text;
    GetWindowText(text);
    const DWORD style = (DWORD)GetStyle();
    if (style & ES_PASSWORD) {
        const TCHAR bullet = (TCHAR)0x25CF;
        const int n = text.GetLength();
        CString bullets;
        for (int i = 0; i < n; ++i)
            bullets += bullet;
        text = bullets;
    }
    const int tlen = text.GetLength();
    int sel0 = 0, sel1 = 0;
    GetSel(sel0, sel1);
    int idx = sel0;
    if (idx < 0) idx = 0;
    if (idx > tlen) idx = tlen;

    auto posOf = [&](int i) -> LRESULT {
        if (tlen <= 0) return (LRESULT)-1;
        if (i < 0) i = 0;
        if (i >= tlen) i = tlen - 1;
        return SendMessage(EM_POSFROMCHAR, (WPARAM)i, 0);
    };
    auto applyLr = [&](LRESULT lr) {
        pt.x = (short)LOWORD(lr);
        pt.y = (short)HIWORD(lr);
    };
    auto extentAt = [&](int i) -> int {
        if (i < 0 || i >= tlen) return aveW;
        CSize ch = dc.GetTextExtent(text.Mid(i, 1));
        return (ch.cx > 0) ? ch.cx : aveW;
    };

    if (tlen <= 0) {
        pt.x = fmt.left;
        if (style & ES_MULTILINE)
            pt.y = fmt.top;
        else
            pt.y = fmt.top + (fmt.Height() - lineH) / 2;
    } else if (style & ES_MULTILINE) {
        const int line = (int)SendMessage(EM_LINEFROMCHAR,
            (WPARAM)((idx < tlen) ? idx : (tlen - 1)), 0);
        int lineStart = (int)SendMessage(EM_LINEINDEX, (WPARAM)line, 0);
        if (lineStart < 0) lineStart = 0;
        int lineLen = (int)SendMessage(EM_LINELENGTH, (WPARAM)lineStart, 0);
        if (lineLen < 0) lineLen = 0;
        const int first = (int)SendMessage(EM_GETFIRSTVISIBLELINE, 0, 0);

        if (lineLen <= 0) {
            // 空行: 行頭で点滅
            pt.x = fmt.left;
            LRESULT lr = (lineStart < tlen) ? SendMessage(EM_POSFROMCHAR, (WPARAM)lineStart, 0) : (LRESULT)-1;
            if (lr != (LRESULT)-1) {
                applyLr(lr);
                pt.x = fmt.left;
            } else {
                lr = (lineStart > 0) ? posOf(lineStart - 1) : (LRESULT)-1;
                if (lr != (LRESULT)-1) {
                    applyLr(lr);
                    pt.x = fmt.left;
                    pt.y += lineH;
                } else {
                    pt.y = fmt.top + (line - first) * lineH;
                }
            }
        } else if (idx >= lineStart + lineLen) {
            // 行末（文字の右側）。CR/LF の POSFROMCHAR は -1 になりやすい
            const int last = lineStart + lineLen - 1;
            LRESULT lr = posOf(last);
            if (lr != (LRESULT)-1) {
                applyLr(lr);
                pt.x += extentAt(last);
            } else {
                pt.x = fmt.left;
                pt.y = fmt.top + (line - first) * lineH;
            }
        } else {
            LRESULT lr = posOf(idx);
            if (lr != (LRESULT)-1) {
                applyLr(lr);
            } else {
                pt.x = fmt.left;
                pt.y = fmt.top + (line - first) * lineH;
            }
        }
    } else if (idx >= tlen) {
        const int last = tlen - 1;
        LRESULT lr = posOf(last);
        if (lr != (LRESULT)-1) {
            applyLr(lr);
            pt.x += extentAt(last);
        } else {
            CSize all = dc.GetTextExtent(text);
            pt.x = fmt.left + all.cx;
            pt.y = fmt.top + (fmt.Height() - lineH) / 2;
            if (style & ES_CENTER)
                pt.x = fmt.left + (fmt.Width() + all.cx) / 2;
            else if (style & ES_RIGHT)
                pt.x = fmt.right;
        }
    } else {
        LRESULT lr = posOf(idx);
        if (lr != (LRESULT)-1) {
            applyLr(lr);
        } else {
            CSize pre = dc.GetTextExtent(text.Left(idx));
            CSize all = dc.GetTextExtent(text);
            pt.x = fmt.left + pre.cx;
            if (style & ES_CENTER)
                pt.x = fmt.left + (fmt.Width() - all.cx) / 2 + pre.cx;
            else if (style & ES_RIGHT)
                pt.x = fmt.right - all.cx + pre.cx;
            pt.y = fmt.top + (fmt.Height() - lineH) / 2;
        }
    }

    if (pOld) dc.SelectObject(pOld);
    dc.Detach();
    ::ReleaseDC(m_hWnd, hdcRaw);

    const UINT dpi = CCC_GetControlDpi(m_hWnd);
    int cw = CCC_ScaleDpi(2, dpi);
    if (cw < 2) cw = 2;
    if (pt.x < r.left) pt.x = r.left;
    if (pt.x > r.right - cw) pt.x = r.right - cw;
    if (pt.y < r.top) pt.y = r.top;
    if (!(style & ES_MULTILINE) && pt.y + lineH > r.bottom)
        pt.y = r.bottom - lineH;
    if (pt.y < r.top) pt.y = r.top;
    return TRUE;
}

// フォーカスかつ非選択かつ点灯中のみ。システムキャレットは Opaque blit で消えるので使わない。
void CCustomEdit::DrawCaretIfNeeded(CDC& dc)
{
    if (!m_bHasFocus || !m_bCaretOn)
        return;
    int s0 = 0, s1 = 0;
    GetSel(s0, s1);
    if (s0 != s1)
        return; // 選択中はキャレット非表示

    CPoint pt;
    int lineH = 16;
    if (!GetCaretClientPos(pt, lineH))
        return;

    CRect r;
    GetClientRect(&r);
    // 幅2〜3pxの濃色キャレット（桃色背景でもはっきり見える）
    const UINT dpi = CCC_GetControlDpi(m_hWnd);
    int w = CCC_ScaleDpi(2, dpi);
    if (w < 2) w = 2;
    CRect caret(pt.x, pt.y, pt.x + w, pt.y + lineH);
    if (caret.right > r.right) {
        caret.right = r.right;
        caret.left = caret.right - w;
        if (caret.left < r.left) caret.left = r.left;
    }
    if (caret.bottom > r.bottom) {
        caret.bottom = r.bottom;
        caret.top = caret.bottom - lineH;
        if (caret.top < r.top) caret.top = r.top;
    }
    if (!caret.IntersectRect(&caret, &r) || caret.Width() <= 0 || caret.Height() <= 0)
        return;
    dc.FillSolidRect(&caret, RGB(80, 20, 40));
}

// IME 変換/候補ウィンドウをキャレットへ。再入ロック必須（Imm → 再描画 → Imm で落ちる）。
void CCustomEdit::SyncImePos()
{
    if (!GetSafeHwnd() || !m_bHasFocus)
        return;

    // Imm* → IME通知 → 再描画 → Imm* の再入でクラッシュしやすい
    static LONG s_busy = 0;
    if (InterlockedCompareExchange(&s_busy, 1, 0) != 0)
        return;

    CPoint pt;
    int lineH = 16;
    if (!GetCaretClientPos(pt, lineH)) {
        InterlockedExchange(&s_busy, 0);
        return;
    }

    ::SetCaretPos(pt.x, pt.y);

    HIMC himc = ::ImmGetContext(m_hWnd);
    if (himc) {
        COMPOSITIONFORM cf = {};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos = pt;
        ::ImmSetCompositionWindow(himc, &cf);

        CANDIDATEFORM cand = {};
        cand.dwIndex = 0;
        cand.dwStyle = CFS_CANDIDATEPOS;
        cand.ptCurrentPos = CPoint(pt.x, pt.y + lineH);
        ::ImmSetCandidateWindow(himc, &cand);
        ::ImmReleaseContext(m_hWnd, himc);
    }
    InterlockedExchange(&s_busy, 0);
}

// GetCaretBlinkTime。0/INFINITE は 530ms。
void CCustomEdit::StartCaretBlink()
{
    StopCaretBlink();
    m_bCaretOn = TRUE;
    UINT blink = ::GetCaretBlinkTime();
    if (blink == 0 || blink == INFINITE)
        blink = 530;
    SetTimer(kEditCaretTimerId, blink, NULL);
}

// キャレットタイマー停止。旧 Opaque タイマーも殺す（互換 ID）。
void CCustomEdit::StopCaretBlink()
{
    KillTimer(kEditCaretTimerId);
    KillTimer(kEditOpaqueTimerId);
    m_bCaretOn = FALSE;
}

// アクリルホストは同期不透明描画（Invalidate だと一瞬ガラス）。それ以外は Invalidate+Update。
void CCustomEdit::RepaintClient()
{
    if (!GetSafeHwnd())
        return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd))
    {
        // UPDATENOW|ERASE / FRAMECHANGED は親消去で兄弟Editが消える原因になる。
        PaintOpaqueFrame();
        CClientDC dc(this);
        PaintOpaqueClient(dc);
        return;
    }
#endif
    Invalidate(FALSE);
    UpdateWindow();
}

// BufferedPaint + MakeOpaque で本文。失敗時は FillRectOpaqueBits。システムキャレットは隠す。
void CCustomEdit::PaintOpaqueClient(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0) return;

    // システムキャレットは Opaque blit で消える／α穴の原因。自前描画に任せる
    if (m_bHasFocus)
        ::HideCaret(m_hWnd);

    // ClientDC 直描きでも絶対に窓外へ出さない
    const int savedDc = dc.SaveDC();
    dc.IntersectClipRect(&r);

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP)
    {
#if CCUSTOM_AERO_SUPPORT
        CCC_FillRectOpaqueBits(dc.GetSafeHdc(), r, COLOR_EDIT_BG);
#else
        dc.FillSolidRect(&r, COLOR_EDIT_BG);
#endif
        DrawClientText(dc, r);
        CCC_DrawInwoman(&dc, r, FALSE);
        dc.RestoreDC(savedDc);
        return;
    }
    RECT rcBuf = { 0, 0, r.right, r.bottom };
    ::FillRect(hdcBuf, &rcBuf, (HBRUSH)m_brBackground.GetSafeHandle());
    {
        CDC dcBuf;
        dcBuf.Attach(hdcBuf);
        DrawClientText(dcBuf, r);
        CCC_DrawInwoman(&dcBuf, r, FALSE);
        dcBuf.Detach();
    }
    ::BufferedPaintMakeOpaque(hBP, &r);
    ::EndBufferedPaint(hBP, TRUE);
    dc.RestoreDC(savedDc);
}

// 同期 Send 再入を避け、CCC_WM_POST_OPAQUE_PAINT を1回ポスト。
void CCustomEdit::ScheduleOpaqueRepaint()
{
    // SendMessage 同期再入を避け、キューに1回まとめる
    if (GetSafeHwnd())
        PostMessage(CCC_WM_POST_OPAQUE_PAINT);
}

// ポストされた不透明再描画。ホストがガラスのときだけ枠+本文。
LRESULT CCustomEdit::OnPostOpaquePaint(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd))
    {
        PaintOpaqueFrame();
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
#endif
    return 0;
}

// ガラス下は自前不透明。それ以外は既定 CEdit（テーマ）。
void CCustomEdit::OnPaint()
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd))
    {
        CPaintDC dc(this);
        PaintOpaqueClient(dc);
        return;
    }
#endif
    Default();
}

// ガラス下は不透明地だけ塗って TRUE（ちらつき防止）。他は FALSE で既定へ。
BOOL CCustomEdit::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd) && pDC)
    {
        CRect r;
        GetClientRect(&r);
        CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), r, COLOR_EDIT_BG);
        return TRUE;
    }
#endif
    UNREFERENCED_PARAMETER(pDC);
    return FALSE;
}

// 角丸枠+フォーカス時キラキラ。top-1 のデコは親アクリルを抉るので禁止。
void CCustomEdit::DrawEditFrame(CDC& dc, const CRect& r)
{
    CPen p(PS_SOLID, 2, m_bHasFocus ? RGB(255, 140, 180) : RGB(255, 182, 193));
    CPen* op = dc.SelectObject(&p);
    dc.SelectStockObject(NULL_BRUSH);
    CRect rr = r;
    rr.DeflateRect(1, 1);
    dc.RoundRect(&rr, CPoint(6, 6));
    dc.SelectObject(op);

    if (m_bHasFocus)
    {
        DrawSparkle(&dc, r.right - 8, r.top + 8, 3, COLOR_SPARKLE);
        DrawSparkle(&dc, r.left + 8, r.top + 8, 2, COLOR_SPARKLE);
        DrawSparkle(&dc, r.right - 8, r.bottom - 8, 2, COLOR_SPARKLE);
        // 窓外へはみ出さない(top-1 は親アクリルを抉る)
        DrawBow(&dc, CRect(r.CenterPoint().x - 8, r.top + 0, r.CenterPoint().x + 8, r.top + 9), COLOR_BOW);
        DrawSoftJkThumb(&dc, CRect(r.right - 18, r.top + 2, r.right - 4, r.top + 16),
            (int)(::GetTickCount64() / 80), TRUE, 8.f);
    }

    CRect rL(r.left + 2, r.CenterPoint().y - 3, r.left + 8, r.CenterPoint().y + 3);
    CRect rR(r.right - 8, r.CenterPoint().y - 3, r.right - 2, r.CenterPoint().y + 3);
    DrawRibbon(&dc, rL, RGB(255, 200, 220));
    DrawRibbon(&dc, rR, RGB(255, 200, 220));
}

// NC 枠を WindowDC で不透明化。ガラス透過で枠が消えるのを防ぐ。
void CCustomEdit::PaintOpaqueFrame()
{
    if (!GetSafeHwnd()) return;
    CWindowDC dc(this);
    CRect r;
    GetWindowRect(&r);
    r.OffsetRect(-r.left, -r.top);
    if (r.Width() <= 0 || r.Height() <= 0) return;

#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd))
    {
        BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
        params.dwFlags = BPPF_ERASE;
        HDC hdcBuf = NULL;
        RECT rr = { 0, 0, r.right, r.bottom };
        HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &rr, BPBF_TOPDOWNDIB, &params, &hdcBuf);
        if (hdcBuf && hBP)
        {
            CDC dcBuf;
            dcBuf.Attach(hdcBuf);
            // 窓全体を不透明地で塞いでから枠(ガラス透過で枠が消えるのを防ぐ)
            dcBuf.FillSolidRect(&r, COLOR_EDIT_BG);
            DrawEditFrame(dcBuf, r);
            dcBuf.Detach();
            ::BufferedPaintMakeOpaque(hBP, &rr);
            ::EndBufferedPaint(hBP, TRUE);
            return;
        }
    }
#endif
    DrawEditFrame(dc, r);
}

// NC のあと本文を載せ直す（不透明 NC がクライアントまで塗ることがある）。
void CCustomEdit::OnNcPaint()
{
    PaintOpaqueFrame();
    // 不透明NCがクライアントも塗るので、直後に本文を載せ直す
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd))
    {
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
#endif
}

// OpaqueFixer / 印刷。ガラス下は FillRectOpaqueBits してから文字。
LRESULT CCustomEdit::OnPrintClient(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    if (HDC hDC = (HDC)wParam)
    {
        CRect r;
        GetClientRect(&r);
        CDC* pDC = CDC::FromHandle(hDC);
#if CCUSTOM_AERO_SUPPORT
        if (CCC_HostNeedsChildOpaque(m_hWnd))
            CCC_FillRectOpaqueBits(hDC, r, COLOR_EDIT_BG);
        else
#endif
            pDC->FillSolidRect(&r, COLOR_EDIT_BG);
        DrawClientText(*pDC, r);
        return 1;
    }
    return 0;
}

// 文字変更。Invalidate せず Opaque ポスト（既定描画→アクリル一瞬を避ける）。
void CCustomEdit::OnEnUpdate()
{
    // OpaqueFixer が WM_CHAR 等で既に不透明描画する。ここでは Invalidate せず
    // 同期 Opaque のみ(既定描画→アクリル一瞬を避ける)。
    m_bCaretOn = TRUE;
    ScheduleOpaqueRepaint();
    SyncImePos();
}

// 選択が変わったときだけ再描画。アクリル下は兄弟 Edit の不透明面も立て直す。
void CCustomEdit::RepaintIfSelChanged()
{
    int s0 = 0, s1 = 0;
    GetSel(s0, s1);
    if (s0 == m_lastSel0 && s1 == m_lastSel1)
        return;
    m_lastSel0 = s0;
    m_lastSel1 = s1;
    m_bCaretOn = TRUE; // 移動直後は点灯
    RepaintClient();
    SyncImePos();
    // 選択再描画で親/アクリル側が兄弟Editの不透明面を落とすことがあるので立て直す
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd))
    {
        CWnd* parent = GetParent();
        if (parent)
        {
            for (CWnd* p = parent->GetWindow(GW_CHILD); p; p = p->GetWindow(GW_HWNDNEXT))
            {
                if (p == this || !p->IsWindowVisible()) continue;
                if (CCustomEdit* e = dynamic_cast<CCustomEdit*>(p))
                {
                    CClientDC dc(e);
                    e->PaintOpaqueClient(dc);
                }
            }
        }
    }
#endif
}

// 矢印等で選択が動くので既定処理後に選択再描画。
void CCustomEdit::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CEdit::OnKeyDown(nChar, nRepCnt, nFlags);
    RepaintIfSelChanged();
}

// KeyUp でも選択が変わる（Shift 解放など）。
void CCustomEdit::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CEdit::OnKeyUp(nChar, nRepCnt, nFlags);
    RepaintIfSelChanged();
}

// クリック一瞬アクリル対策で先に不透明面。ドラッグ選択は 33ms タイマー。
void CCustomEdit::OnLButtonDown(UINT nFlags, CPoint point)
{
#if CCUSTOM_AERO_SUPPORT
    // 既定処理の前に不透明面を確保(クリック一瞬アクリル対策)
    if (CCC_HostNeedsChildOpaque(m_hWnd))
    {
        PaintOpaqueFrame();
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
#endif
    CEdit::OnLButtonDown(nFlags, point);
    m_bSelDrag = TRUE;
    SetTimer(kEditSelTimerId, 33, NULL);
    m_bCaretOn = TRUE;
    {
        const int prev0 = m_lastSel0, prev1 = m_lastSel1;
        RepaintIfSelChanged();
        // 行末クリックで挿入位置が変わらないときもキャレットを点灯し直す
        if (m_lastSel0 == prev0 && m_lastSel1 == prev1)
            RepaintClient();
    }
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd))
        ScheduleOpaqueRepaint();
#endif
}

// 単語選択後のハイライト更新。
void CCustomEdit::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    CEdit::OnLButtonDblClk(nFlags, point);
    RepaintIfSelChanged();
}

// ドラッグ選択終了。
void CCustomEdit::OnLButtonUp(UINT nFlags, CPoint point)
{
    CEdit::OnLButtonUp(nFlags, point);
    m_bSelDrag = FALSE;
    KillTimer(kEditSelTimerId);
    RepaintIfSelChanged();
}

// 複数行で既定が無視するとき LineScroll。ガラス下は不透明再描画。
BOOL CCustomEdit::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    BOOL r = CEdit::OnMouseWheel(nFlags, zDelta, pt);
    if (!r && (GetStyle() & ES_MULTILINE)) {
        // フォーカス無し等で既定が無視するとき
        LineScroll((zDelta > 0) ? -3 : 3); // 3行（WHEEL_DELTA 単位ではなく固定）
        r = TRUE;
    }
#if CCUSTOM_AERO_SUPPORT
    if (r && CCC_HostNeedsChildOpaque(m_hWnd)) {
        PaintOpaqueFrame();
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
#endif
    return r;
}

// 縦スクロール後、ガラス下は本文を不透明に載せ直す。
void CCustomEdit::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    CEdit::OnVScroll(nSBCode, nPos, pScrollBar);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd)) {
        PaintOpaqueFrame();
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
#endif
}

// 横スクロール後、ガラス下は本文を不透明に載せ直す。
void CCustomEdit::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    CEdit::OnHScroll(nSBCode, nPos, pScrollBar);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd)) {
        PaintOpaqueFrame();
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
#endif
}

// ドラッグ中は選択更新。ホバーだけでもテーマが α=0 を載せるので Opaque ポスト。
void CCustomEdit::OnMouseMove(UINT nFlags, CPoint point)
{
    CEdit::OnMouseMove(nFlags, point);
    if (nFlags & MK_LBUTTON)
        RepaintIfSelChanged();
#if CCUSTOM_AERO_SUPPORT
    // ホバーで NC/テーマが α=0 を載せるのを即打ち消す
    else if (CCC_HostNeedsChildOpaque(m_hWnd))
        ScheduleOpaqueRepaint();
#endif
}

// 選択ドラッグ / キャレット点滅。旧 Opaque タイマー ID は殺すだけ。
void CCustomEdit::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kEditSelTimerId)
    {
        if (m_bSelDrag)
            RepaintIfSelChanged();
        else
            KillTimer(kEditSelTimerId);
        return;
    }
    if (nIDEvent == kEditCaretTimerId)
    {
        if (!m_bHasFocus) {
            StopCaretBlink();
            return;
        }
        int s0 = 0, s1 = 0;
        GetSel(s0, s1);
        if (s0 != s1) {
            // 選択中は点滅不要（描画もスキップ）
            m_bCaretOn = FALSE;
            return;
        }
        m_bCaretOn = !m_bCaretOn;
        RepaintClient();
        return;
    }
    if (nIDEvent == kEditOpaqueTimerId)
    {
        // 旧: 50ms 全再描画はキャレットを潰していた。点滅タイマーへ移行済み。
        KillTimer(kEditOpaqueTimerId);
        return;
    }
    CEdit::OnTimer(nIDEvent);
}

// 再表示時に不透明面を立て直す（最小化復帰で消える）。
void CCustomEdit::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CEdit::OnShowWindow(bShow, nStatus);
    if (bShow)
        RepaintClient();
    UNREFERENCED_PARAMETER(nStatus);
}

// システムキャレットは隠して自前点滅。FRAMECHANGED は親ガラス消去の元凶なので使わない。
void CCustomEdit::OnSetFocus(CWnd* p)
{
    CEdit::OnSetFocus(p);
    m_bHasFocus = TRUE;
    // システムキャレットは Opaque 再描画で消え、点滅が α=0 穴になる。隠して自前描画。
    ::HideCaret(m_hWnd);
    StartCaretBlink();
    // SWP_FRAMECHANGED / Invalidate は親ガラス消去→枠消失・一瞬アクリルの元凶。
    // 枠色変更は自前の不透明 NC 描画だけで行う。
    PaintOpaqueFrame();
    {
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
    SyncImePos();
}

// 点滅停止して枠色を非フォーカスへ。不透明面は残す。
void CCustomEdit::OnKillFocus(CWnd* p)
{
    CEdit::OnKillFocus(p);
    m_bHasFocus = FALSE;
    m_bSelDrag = FALSE;
    KillTimer(kEditSelTimerId);
    StopCaretBlink();
    PaintOpaqueFrame();
    {
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
}

// 変換開始。候補位置を合わせてから既定。
LRESULT CCustomEdit::OnImeStartComposition(WPARAM wParam, LPARAM lParam)
{
    SyncImePos();
    return Default();
}

// 変換中は毎フレーム Opaque しない（再入クラッシュ）。確定時のみ。
LRESULT CCustomEdit::OnImeComposition(WPARAM wParam, LPARAM lParam)
{
    LRESULT r = Default();
    SyncImePos();
    // 確定時のみ再不透明化（変換中毎フレームは再入・クラッシュの温床）
    if (lParam & GCS_RESULTSTR)
        ScheduleOpaqueRepaint();
    return r;
}

// 候補開閉・位置変更で IME ウィンドウを追従。
LRESULT CCustomEdit::OnImeNotify(WPARAM wParam, LPARAM lParam)
{
    LRESULT r = Default();
    if (wParam == IMN_OPENCANDIDATE || wParam == IMN_SETCOMPOSITIONWINDOW
        || wParam == IMN_SETCANDIDATEPOS || wParam == IMN_CHANGECANDIDATE)
        SyncImePos();
    UNREFERENCED_PARAMETER(lParam);
    return r;
}

// ============================================================================
// カスタムスタティックコントロール
// CCustomStatic
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomStatic, CStatic)

BEGIN_MESSAGE_MAP(CCustomStatic, CStatic)
    ON_WM_PAINT()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
    ON_WM_ERASEBKGND()
    ON_MESSAGE(WM_SETTEXT, OnSetText)
    ON_MESSAGE(WM_GETTEXT, OnGetText)
    ON_MESSAGE(WM_GETTEXTLENGTH, OnGetTextLength)
END_MESSAGE_MAP()

// ラベル。ガラス上はクロマキー、ソリッド指定/Opaque ホストでは不透明。!@ 書式セグメント対応。
CCustomStatic::CCustomStatic()
    : m_bAutoDelete(FALSE),
    m_clrGradStart(RGB(255, 255, 255)), m_clrGradEnd(RGB(255, 255, 255)),
    m_nGradDirection(0), m_bGradEnable(FALSE),
    m_clrShadow(RGB(0, 0, 0)), m_nShadowDirection(135),
    m_nShadowDistance(2), m_nShadowBlur(3), m_bShadowEnable(FALSE),
    m_bPreferWideMode(FALSE), m_nCachedHeight(0), m_nCachedWidth(0), m_fCachedScaleX(1.0f),
    m_strCachedText(_T("")), m_strText(_T("")), m_nCachedDpi(0),
    m_backstoreW(0), m_backstoreH(0), m_segCount(0), m_strSegSource(_T("")),
    m_bAeroMode(FALSE), m_bNoParentInvalidate(FALSE),
    m_bSolidFill(FALSE), m_clrSolidFill(COLOR_DIALOG_BG)
{}

// バックストアとクロマキャッシュを解放。
CCustomStatic::~CCustomStatic()
{
    if (m_font.GetSafeHandle()) m_font.DeleteObject();
    if (m_memBackstore.GetSafeHandle()) m_memBackstore.DeleteObject();
#if CCUSTOM_AERO_SUPPORT
    m_chromaCache.Release();
#endif
}

// 自動サブクラス時はここで delete this。
void CCustomStatic::PostNcDestroy()
{
    CStatic::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

// !@B / !@I / !@Crrggbb / !@F±nn をセグメントへ。Mid 一括（1文字ずつは断片化）。
void CCustomStatic::ParseFormattedText(const CString& str)
{
    // Mid で一括コピー。文字単位 cur+= は CString 再確保が積み、長時間で断片化する。
    m_segCount = 0;
    m_strSegSource = str;
    BOOL bB = FALSE, bI = FALSE, bHC = FALSE;
    COLORREF cc = RGB(0, 0, 0);
    int nFO = 0;
    int runStart = 0;
    const int len = str.GetLength();

    auto FlushTo = [&](int end)
    {
        if (end <= runStart || m_segCount >= kMaxTextSegs)
            return;
        TextSegment& s = m_segs[m_segCount++];
        s.text = str.Mid(runStart, end - runStart);
        s.bBold = bB;
        s.bItalic = bI;
        s.bHasColor = bHC;
        s.clrText = cc;
        s.nFontSizeOffset = nFO;
    };

    for (int i = 0; i < len; )
    {
        if (i + 1 < len && str[i] == _T('!') && str[i + 1] == _T('@') && i + 2 < len)
        {
            TCHAR cmd = str[i + 2];
            if (cmd == _T('B'))
            {
                FlushTo(i);
                bB = !bB;
                i += 3;
                runStart = i;
                continue;
            }
            else if (cmd == _T('I'))
            {
                FlushTo(i);
                bI = !bI;
                i += 3;
                runStart = i;
                continue;
            }
            else if (cmd == _T('C') && i + 8 < len)
            {
                CString hx = str.Mid(i + 3, 6);
                int r, g, b;
                if (_stscanf_s(hx, _T("%2x%2x%2x"), &r, &g, &b) == 3)
                {
                    FlushTo(i);
                    bHC = TRUE;
                    cc = RGB(r, g, b);
                    i += 9;
                    runStart = i;
                    continue;
                }
            }
            else if (cmd == _T('F') && i + 5 < len)
            {
                TCHAR sg = str[i + 3];
                CString nm = str.Mid(i + 4, 2);
                if ((sg == _T('+') || sg == _T('-')) && nm.GetLength() == 2
                    && _istdigit(nm[0]) && _istdigit(nm[1]))
                {
                    int off = _ttoi(nm);
                    if (sg == _T('-')) off = -off;
                    FlushTo(i);
                    nFO += off;
                    i += 6;
                    runStart = i;
                    continue;
                }
            }
        }
        ++i;
    }
    FlushTo(len);
}

// 書式セグメントを横に並べて描く。透過時の黒文字は RGB(2,2,2)（キー 1,1,1 と衝突回避）。
void CCustomStatic::DrawSegmentedText(CDC* pDC, const CRect& rect, const LOGFONT& lf, int h, int w, UINT fmt)
{
    CSize tot = MeasureSegmentedText(pDC, lf, h, w);
    int xP = rect.left;
    if (fmt & DT_CENTER) xP = rect.left + (rect.Width() - tot.cx) / 2;
    else if (fmt & DT_RIGHT) xP = rect.right - tot.cx;

    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    const COLORREF clrBg = bTrans ? CCC_AERO_CHROMA_KEY : COLOR_DIALOG_BG;

    for (int i = 0; i < m_segCount; i++)
    {
        LOGFONT lt = lf;
        lt.lfHeight = -max(6, h + m_segs[i].nFontSizeOffset);
        lt.lfWidth = w;
        if (m_segs[i].bBold) lt.lfWeight = FW_BOLD;
        if (m_segs[i].bItalic) lt.lfItalic = TRUE;
        CFont* pFont = CCC_GetPooledSegFont(lt);
        if (!pFont) continue;
        CFont* po = pDC->SelectObject(pFont);
        CSize sz = pDC->GetTextExtent(m_segs[i].text);
        CRect sr = { xP, rect.top, xP + sz.cx, rect.bottom };
        COLORREF tc = m_segs[i].bHasColor ? m_segs[i].clrText : RGB(0, 0, 0);
        if (bTrans && tc == RGB(0, 0, 0)) tc = RGB(2, 2, 2); // キー RGB(1,1,1) と区別

        if (m_bGradEnable) DrawTextWithGradient(pDC, sr, m_segs[i].text, DT_VCENTER | DT_SINGLELINE | DT_LEFT, m_clrGradStart, m_clrGradEnd, m_nGradDirection, m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur, m_bShadowEnable, clrBg, sz.cx, FALSE, bTrans);
        else DrawTextWithShadow(pDC, sr, m_segs[i].text, DT_VCENTER | DT_SINGLELINE | DT_LEFT, tc, m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur, m_bShadowEnable, clrBg, bTrans);

        xP += sz.cx;
        pDC->SelectObject(po);
    }
}

// 文字グラデ。角度は 0..359。キャッシュ無効化して Invalidate。
void CCustomStatic::SetGradation(COLORREF s, COLORREF e, int d, BOOL en)
{
    m_clrGradStart = s; m_clrGradEnd = e;
    m_nGradDirection = d % 360;
    if (m_nGradDirection < 0) m_nGradDirection += 360;
    m_bGradEnable = en;
    m_strCachedText.Empty();
    if (GetSafeHwnd()) Invalidate();
}

// グラデ設定の取得。NULL ポインタは無視。
void CCustomStatic::GetGradation(COLORREF* ps, COLORREF* pe, int* pd, BOOL* pbe) const
{
    if (ps) *ps = m_clrGradStart;
    if (pe) *pe = m_clrGradEnd;
    if (pd) *pd = m_nGradDirection;
    if (pbe) *pbe = m_bGradEnable;
}

// ドロップシャドウ。blur は 0..20。アクリル透過時は描画側で省略。
void CCustomStatic::SetDropShadow(COLORREF c, int d, int dist, int blur, BOOL en)
{
    m_clrShadow = c;
    m_nShadowDirection = d % 360;
    if (m_nShadowDirection < 0) m_nShadowDirection += 360;
    m_nShadowDistance = max(0, dist);
    m_nShadowBlur = max(0, min(20, blur));
    m_bShadowEnable = en;
    if (GetSafeHwnd()) Invalidate();
}

// 影設定の取得。
void CCustomStatic::GetDropShadow(COLORREF* pc, int* pd, int* pdist, int* pblur, BOOL* pbe) const
{
    if (pc) *pc = m_clrShadow;
    if (pd) *pd = m_nShadowDirection;
    if (pdist) *pdist = m_nShadowDistance;
    if (pblur) *pblur = m_nShadowBlur;
    if (pbe) *pbe = m_bShadowEnable;
}

// 余白があれば文字を横に伸ばす（lfWidth 探索）。EQ コード等。
void CCustomStatic::SetPreferWideMode(BOOL b)
{
    m_bPreferWideMode = b;
    m_strCachedText.Empty();
    m_nCachedDpi = 0;
    if (GetSafeHwnd()) Invalidate();
}

// ワイド優先モードか。
BOOL CCustomStatic::GetPreferWideMode() const
{
    return m_bPreferWideMode;
}

// フォントをコピー所有。元 HFONT は触らない。フィットキャッシュを捨てる。
void CCustomStatic::SetFont(CFont* pF, BOOL bR)
{
    if (pF)
    {
        LOGFONT lf;
        pF->GetLogFont(&lf);
        if (m_font.GetSafeHandle()) m_font.DeleteObject();
        m_font.CreateFontIndirect(&lf);
        CStatic::SetFont(&m_font, bR);
        m_strCachedText.Empty();
    }
}

// 初期キャプションを内部バッファへ。親フォントを継承。
void CCustomStatic::PreSubclassWindow()
{
    CStatic::PreSubclassWindow();
    CWnd::GetWindowText(m_strText);
    CWnd* pP = GetParent();
    if (pP)
    {
        CFont* pF = pP->GetFont();
        if (pF) SetFont(pF, FALSE);
    }
}

// 透過時は消去しない（親ガラスを残す）。ソリッド時は COLOR_DIALOG_BG。
BOOL CCustomStatic::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseTransPaint(m_hWnd, m_bAeroMode)) return TRUE;
#endif
    if (pDC)
    {
        CRect r;
        GetClientRect(&r);
        pDC->FillSolidRect(&r, COLOR_DIALOG_BG);
    }
    return TRUE;
}

// memDC に描いてクロマ or 不透明 blit。透過時は最初からキー色塗り（リマップ失敗でピンク残る）。
void CCustomStatic::DrawClient(CDC& dc)
{
    CRect rect;
    GetClientRect(&rect);
    const int rw = rect.Width();
    const int rh = rect.Height();
    if (rw <= 0 || rh <= 0) return;
    if (m_strText.IsEmpty()) CWnd::GetWindowText(m_strText);

    auto blitTrans = [&](HDC hdcSrc) {
#if CCUSTOM_AERO_SUPPORT
        if (CCC_UseTransPaint(m_hWnd, m_bAeroMode) && CCC_IsAeroEnabled() && CCC_IsWin11()) {
            CCC_BlitChromaCached(dc.GetSafeHdc(), 0, 0, rw, rh,
                hdcSrc, 0, 0, CCC_AERO_CHROMA_KEY, m_chromaCache);
            return;
        }
#endif
#if CCUSTOM_AERO_SUPPORT
        CCC_BlitChromaTrans(dc.GetSafeHdc(), 0, 0, rw, rh, hdcSrc, 0, 0, CCC_AERO_CHROMA_KEY);
#else
        (void)hdcSrc;
#endif
    };

    CDC memDC;
    memDC.CreateCompatibleDC(&dc);
    if (rw != m_backstoreW || rh != m_backstoreH || !m_memBackstore.GetSafeHandle())
    {
        if (m_memBackstore.GetSafeHandle()) m_memBackstore.DeleteObject();
        m_memBackstore.CreateCompatibleBitmap(&dc, rw, rh);
        m_backstoreW = rw;
        m_backstoreH = rh;
    }
    CBitmap* ob = memDC.SelectObject(&m_memBackstore);

    BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    if (m_bSolidFill || CCC_HostNeedsChildOpaque(m_hWnd))
        bTrans = FALSE;
    // 透過時は最初からクロマキーで塗る(スライダー/チェックと同じ)。
    // COLOR_DIALOG_BG→リマップは CreateCompatibleBitmap 経由で色が
    // 量子化されると一致せず、不透明ピンクのまま残る。
    COLORREF clrFill = bTrans ? CCC_AERO_CHROMA_KEY : COLOR_DIALOG_BG;
    if (!bTrans && m_bSolidFill)
        clrFill = m_clrSolidFill;
    memDC.FillSolidRect(&rect, clrFill);

    if (m_strText.IsEmpty())
    {
        if (bTrans)
            blitTrans(memDC.GetSafeHdc());
        else
            dc.BitBlt(0, 0, rw, rh, &memDC, 0, 0, SRCCOPY);
        memDC.SelectObject(ob);
        memDC.DeleteDC();
        return;
    }

    CString strText = m_strText;
    const BOOL hasBreak = CCC_TextHasBreak(strText);
    const BOOL bHasFmt = (strText.Find(_T("!@")) >= 0);
    if (bHasFmt) {
        if (strText != m_strSegSource)
            ParseFormattedText(strText);
    }
    else {
        m_segCount = 0;
        m_strSegSource.Empty();
    }

    CRect rectWithMargin = rect;
    const UINT dpi = CCC_GetControlDpi(m_hWnd);
    const int marginPx = max(1, CCC_ScaleDpi(1, dpi));
    rectWithMargin.DeflateRect(marginPx, marginPx);

    CFont* pBF = GetFont();
    CFont* pOF = memDC.SelectObject(pBF);
    memDC.SetBkMode(TRANSPARENT);

    LOGFONT lfB;
    if (pBF)
        pBF->GetLogFont(&lfB);
    else
    {
        CFont* d = CFont::FromHandle((HFONT)GetStockObject(DEFAULT_GUI_FONT));
        d->GetLogFont(&lfB);
    }

    const int kMinHeight = max(6, CCC_ScaleDpi(6, dpi));
    const int baseHeight = abs(lfB.lfHeight);
    int finalHeight = 0;
    int finalWidth = 0;
    CSize szFinal;

    const BOOL bNeedRecalc = (strText != m_strCachedText) ||
        (m_nCachedHeight == 0) ||
        (m_rectCached != rect) ||
        (m_nCachedDpi != dpi);

    if (bNeedRecalc)
    {
        if (bHasFmt)
        {
            auto MeasureText = [&](int height, int width) -> CSize {
                return MeasureSegmentedText(&memDC, lfB, height, width);
            };

            int fitHeight = kMinHeight;
            int baseWidth = 0;
            CSize szFit;

            for (int h = baseHeight; h >= kMinHeight; h--)
            {
                LOGFONT lfTry = lfB;
                lfTry.lfHeight = -h;
                lfTry.lfWidth = 0;
                CFont* pTry = CCC_GetPooledSegFont(lfTry);
                if (!pTry) continue;
                CFont* pOld = memDC.SelectObject(pTry);
                TEXTMETRIC tm;
                memDC.GetTextMetrics(&tm);
                memDC.SelectObject(pOld);

                CSize size = MeasureText(h, 0);
                if (size.cx <= rectWithMargin.Width())
                {
                    fitHeight = h;
                    szFit = size;
                    baseWidth = tm.tmAveCharWidth;
                    break;
                }
            }

            finalHeight = fitHeight;
            finalWidth = 0;
            szFinal = szFit;

            if (szFit.cy < rectWithMargin.Height())
            {
                const double stretch = (double)rectWithMargin.Height() / fitHeight;
                if (stretch <= 1.35) // 縦に伸ばしすぎると潰れるので 135% まで
                {
                    finalHeight = rectWithMargin.Height();
                    finalWidth = max(1, (int)(baseWidth / stretch));
                    szFinal = MeasureText(finalHeight, finalWidth);
                }
            }

            int fmtShadowPadX = 0, fmtShadowPadY = 0;
            CCC_ComputeShadowPad(m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
                m_bShadowEnable, dpi, fmtShadowPadX, fmtShadowPadY);
            UNREFERENCED_PARAMETER(fmtShadowPadY);
            const int fmtSidePad = max(1, CCC_ScaleDpi(3, dpi));
            const int fmtAvailW = (std::max)(1, rectWithMargin.Width() - fmtShadowPadX - fmtSidePad);

            if (m_bPreferWideMode && szFinal.cx < fmtAvailW)
            {
                const int startWidth = (finalWidth > 0) ? finalWidth : baseWidth;
                const int maxWidth = startWidth * 3;
                int bestW = 0;
                CSize bestSz = szFinal;
                for (int w = startWidth; w <= maxWidth; w++)
                {
                    CSize sizeTry = MeasureText(finalHeight, w);
                    if (sizeTry.cx <= fmtAvailW && sizeTry.cy <= rectWithMargin.Height())
                    {
                        bestW = w;
                        bestSz = sizeTry;
                    }
                    else break;
                }
                if (bestW > 0)
                {
                    finalWidth = bestW;
                    szFinal = bestSz;
                }
            }
            m_fCachedScaleX = 1.0f;
        }
        else
        {
            auto MeasureText = [&](int height, int width) -> CSize {
                LOGFONT lfTry = lfB;
                lfTry.lfHeight = -height;
                lfTry.lfWidth = width;
                CFont* pTry = CCC_GetPooledSegFont(lfTry);
                if (!pTry) return CSize(0, 0);
                CFont* pOld = memDC.SelectObject(pTry);
                CSize size;
                if (hasBreak) {
                    CRect mc(0, 0, 32767, 32767); // DT_CALCRECT 用の十分広い仮矩形
                    memDC.DrawText(strText, &mc, DT_CALCRECT | DT_LEFT | DT_TOP | DT_NOPREFIX);
                    size = CSize(mc.Width(), mc.Height());
                } else {
                    size = memDC.GetTextExtent(strText);
                }
                memDC.SelectObject(pOld);
                return size;
            };

            int shadowPadX = 0;
            int shadowPadY = 0;
            CCC_ComputeShadowPad(m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
                m_bShadowEnable, dpi, shadowPadX, shadowPadY);
            UNREFERENCED_PARAMETER(shadowPadY);

            const int sidePad = max(1, CCC_ScaleDpi(3, dpi));
            const int availW = (std::max)(1, rectWithMargin.Width() - shadowPadX - sidePad);
            finalHeight = min(baseHeight, rectWithMargin.Height());
            finalWidth = 0;
            szFinal = MeasureText(finalHeight, 0);
            if (hasBreak) {
                while (finalHeight > kMinHeight && szFinal.cy > rectWithMargin.Height()) {
                    finalHeight--;
                    szFinal = MeasureText(finalHeight, 0);
                }
            }

            int italicMargin = lfB.lfItalic ? (finalHeight / 2) : 0;
            int needW = szFinal.cx + italicMargin;
            float scaleX = 1.0f;
            if (needW > availW)
            {
                scaleX = (float)availW / (float)needW;
                const float kMinScaleX = 0.50f;
                scaleX *= 0.98f; // 右端クリップ余裕
                if (scaleX < kMinScaleX) scaleX = kMinScaleX;
                if (scaleX > 1.0f) scaleX = 1.0f;
            }

            if (!hasBreak && m_bPreferWideMode && szFinal.cx < availW)
            {
                LOGFONT lfTry = lfB;
                lfTry.lfHeight = -finalHeight;
                lfTry.lfWidth = 0;
                CFont* pTry = CCC_GetPooledSegFont(lfTry);
                TEXTMETRIC tm = {};
                if (pTry) {
                    CFont* pOld = memDC.SelectObject(pTry);
                    memDC.GetTextMetrics(&tm);
                    memDC.SelectObject(pOld);
                }

                const int baseWidth = tm.tmAveCharWidth;
                const int maxWidth = baseWidth * 3;
                int bestW = 0;
                CSize bestSz = szFinal;
                for (int w = baseWidth; w <= maxWidth; w++)
                {
                    CSize sizeTry = MeasureText(finalHeight, w);
                    const int tryItalic = lfB.lfItalic ? (finalHeight / 2) : 0;
                    if (sizeTry.cx + tryItalic <= availW && sizeTry.cy <= rectWithMargin.Height())
                    {
                        bestW = w;
                        bestSz = sizeTry;
                    }
                    else break;
                }
                if (bestW > 0)
                {
                    finalWidth = bestW;
                    szFinal = bestSz;
                }
            }

            m_fCachedScaleX = scaleX;
        }

        m_strCachedText = strText;
        m_nCachedHeight = finalHeight;
        m_nCachedWidth = finalWidth;
        m_rectCached = rect;
        m_nCachedDpi = dpi;
    }
    else
    {
        finalHeight = m_nCachedHeight;
        finalWidth = m_nCachedWidth;
    }

    DWORD ds = GetStyle();
    UINT fmt = DT_NOPREFIX;
    if (hasBreak) fmt |= DT_TOP;
    else fmt |= DT_VCENTER | DT_SINGLELINE;
    if (ds & SS_CENTER) fmt |= DT_CENTER;
    else if (ds & SS_RIGHT) fmt |= DT_RIGHT;
    else fmt |= DT_LEFT;

    const COLORREF clrBg = bTrans ? CCC_AERO_CHROMA_KEY : COLOR_DIALOG_BG;

    if (bHasFmt)
    {
        DrawSegmentedText(&memDC, rect, lfB, finalHeight, finalWidth, fmt);
    }
    else
    {
        CFont* pFontFinal = nullptr;
        {
            LOGFONT lfFinal = lfB;
            lfFinal.lfHeight = -finalHeight;
            lfFinal.lfWidth = finalWidth;
            pFontFinal = CCC_GetPooledSegFont(lfFinal);
        }
        CFont* pOIF = pFontFinal ? memDC.SelectObject(pFontFinal) : nullptr;

        if (hasBreak)
        {
            DrawFitControlText(&memDC, rectWithMargin, strText, fmt, 0.50f);
        }
        else if (m_fCachedScaleX < 0.98f)
        {
            DrawFittedText(memDC, rect, strText, fmt,
                m_bGradEnable, m_clrGradStart, m_clrGradEnd, m_nGradDirection,
                m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
                m_bShadowEnable, clrBg, FALSE, bTrans, dpi);
        }
        else
        {
            szFinal = memDC.GetTextExtent(strText);

            if (m_bGradEnable)
            {
                DrawTextWithGradient(&memDC, rect, strText, fmt,
                    m_clrGradStart, m_clrGradEnd, m_nGradDirection,
                    m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
                    m_bShadowEnable, clrBg, szFinal.cx, m_bPreferWideMode, bTrans);
            }
            else
            {
                DrawTextWithShadow(&memDC, rect, strText, fmt, RGB(0, 0, 0),
                    m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
                    m_bShadowEnable, clrBg, bTrans);
            }
        }

        if (pOIF) memDC.SelectObject(pOIF);
    }

    memDC.SelectObject(pOF);

    // 淫女演出は文字描画の後。中央は塗らないので読みやすさは維持。
    // (高さ20未満は CCC_DrawInwoman 側で主要デコをスキップ)
    CCC_DrawInwoman(&memDC, rect, bTrans);

    if (bTrans)
        blitTrans(memDC.GetSafeHdc());
    else
        CCC_InwomanBitBlt(dc.GetSafeHdc(), rw, rh, memDC.GetSafeHdc(), COLOR_DIALOG_BG);

    memDC.SelectObject(ob);
    memDC.DeleteDC();
}

// 透過は DrawClient のみ（MakeOpaque 禁止）。ガラス下ソリッドは BufferedPaintMakeOpaque。
void CCustomStatic::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0)
        return;

    // ソリッド指定でも Win11 アクリル下は CompatibleBitmap の α=0 が黒帯になる。
    // FillRectOpaqueBits の直後に素 DrawClient すると上書きで黒に戻るので、
    // 不透明化は下の BufferedPaintMakeOpaque に任せる。

    // 透過(アクリル)時はクロマ blit を潰す MakeOpaque を避ける(CCustomCheckBox と同じ)
    BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    if (m_bSolidFill || CCC_HostNeedsChildOpaque(m_hWnd))
        bTrans = FALSE;
    if (bTrans)
    {
        DrawClient(dc);
        return;
    }

#if CCUSTOM_AERO_SUPPORT
    // キャプションのみホスト α: 素 BitBlt は α=0 で消える → MakeOpaque
    if (CCC_IsWin11() && (CCC_IsAeroEnabled() || CCC_CaptionOnlyHostGlass(m_hWnd)))
    {
        BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
        params.dwFlags = BPPF_ERASE;
        HDC hdcBuf = NULL;
        HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
        if (hdcBuf && hBP) {
            CDC dcBuf;
            dcBuf.Attach(hdcBuf);
            DrawClient(dcBuf);
            dcBuf.Detach();
            ::BufferedPaintMakeOpaque(hBP, &r);
            ::EndBufferedPaint(hBP, TRUE);
            return;
        }
    }
#endif
    DrawClient(dc);
}

// OpaqueFixer 経由。DrawClient に委譲。
LRESULT CCustomStatic::OnPrintClient(WPARAM wParam, LPARAM)
{
    if (HDC hDC = (HDC)wParam)
    {
        CDC dc;
        dc.Attach(hDC);
        DrawClient(dc);
        dc.Detach();
    }
    return 0;
}

// WM_SETTEXT。同一文字列は Invalidate しない（EQ コードの無駄再描画防止）。透過時は親も消す。
LRESULT CCustomStatic::OnSetText(WPARAM, LPARAM lp)
{
    LPCTSTR t = (LPCTSTR)lp;
    const CString neu = t ? t : _T("");
    // 同一文字列の再設定は Invalidate/フィット計算を起こさない（EQ コードの無駄再描画防止）
    if (neu == m_strText)
        return TRUE;
    m_strText = neu;
    m_strCachedText.Empty();
    m_segCount = 0;
    m_strSegSource.Empty();
    if (GetSafeHwnd())
    {
#if CCUSTOM_AERO_SUPPORT
        if (!m_bNoParentInvalidate)
            CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
        Invalidate(FALSE);
    }
    return TRUE;
}

// 内部 m_strText を返す（書式タグ込み）。既定 Static は !@ を解釈しない。
LRESULT CCustomStatic::OnGetText(WPARAM w, LPARAM l)
{
    int n = (int)w;
    LPTSTR t = (LPTSTR)l;
    if (!t || n <= 0) return 0;
    int ln = m_strText.GetLength();
    int cp = min(ln, n - 1);
    if (cp > 0) _tcsncpy_s(t, n, (LPCTSTR)m_strText, cp);
    t[cp] = _T('\0');
    return cp;
}

// WM_GETTEXTLENGTH。m_strText の文字数。
LRESULT CCustomStatic::OnGetTextLength(WPARAM, LPARAM)
{
    return m_strText.GetLength();
}

// 書式セグメントの合計サイズ。フォントはプールから。
CSize CCustomStatic::MeasureSegmentedText(CDC* pDC, const LOGFONT& lf, int h, int w)
{
    CSize tot(0, 0);
    for (int i = 0; i < m_segCount; i++)
    {
        LOGFONT lt = lf;
        lt.lfHeight = -max(6, h + m_segs[i].nFontSizeOffset);
        lt.lfWidth = w;
        if (m_segs[i].bBold) lt.lfWeight = FW_BOLD;
        if (m_segs[i].bItalic) lt.lfItalic = TRUE;

        CFont* pFont = CCC_GetPooledSegFont(lt);
        if (!pFont) continue;
        CFont* po = pDC->SelectObject(pFont);

        CSize sz = pDC->GetTextExtent(m_segs[i].text);
        tot.cx += sz.cx;
        if (sz.cy > tot.cy) tot.cy = sz.cy;

        pDC->SelectObject(po);
    }
    return tot;
}


// ============================================================================
// カスタムリストボックスコントロール
// CCustomListBox
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomListBox, CListBox)

BEGIN_MESSAGE_MAP(CCustomListBox, CListBox)
    ON_WM_CTLCOLOR_REFLECT()
    ON_WM_PAINT()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

// オーナードロー ListBox。行デコ+交互色。ガラス下は不透明 Fill。
CCustomListBox::CCustomListBox() : m_bAutoDelete(FALSE), m_bAeroMode(FALSE)
{
    m_brBackground.CreateSolidBrush(COLOR_LIST_BG);
}

// 背景ブラシのみ。
CCustomListBox::~CCustomListBox()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

// 自動サブクラス時は delete this。
void CCustomListBox::PostNcDestroy()
{
    CListBox::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

// LBS_OWNERDRAWFIXED を後付け。MeasureItem が走らないことがあるので SetItemHeight も。
void CCustomListBox::PreSubclassWindow()
{
    CListBox::PreSubclassWindow();
    // 作成後の ModifyStyle では MeasureItem が走らないことがあるので SetItemHeight も併用
    ModifyStyle(0, LBS_OWNERDRAWFIXED | LBS_HASSTRINGS);
    const UINT dpi = CCC_GetControlDpi(m_hWnd);
    int h = CCC_ScaleDpi(24, dpi);
    CDC* pDC = GetDC();
    if (pDC) {
        CFont* pf = GetFont();
        CFont* old = pf ? pDC->SelectObject(pf) : NULL;
        TEXTMETRIC tm = {};
        pDC->GetTextMetrics(&tm);
        if (old) pDC->SelectObject(old);
        ReleaseDC(pDC);
        const int fromFont = tm.tmHeight + CCC_ScaleDpi(8, dpi);
        if (fromFont > h) h = fromFont;
    }
    SetItemHeight(0, h);
}

// 反射 CTLCOLOR。非オーナードロー残骸用。本体は DrawItem。
HBRUSH CCustomListBox::CtlColor(CDC* pDC, UINT)
{
    pDC->SetBkColor(COLOR_LIST_BG);
    pDC->SetTextColor(RGB(0, 0, 0));
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

// 既定に任せる（オーナードローは DrawItem）。ガラス穴は OnEraseBkgnd 側。
void CCustomListBox::OnPaint()
{
    Default();
}

// OpaqueFixer 用。全アイテムを DrawItem。選択は GetSel。
LRESULT CCustomListBox::OnPrintClient(WPARAM wParam, LPARAM)
{
    CDC* pDC = CDC::FromHandle((HDC)wParam);
    if (!pDC) return 0;
    CRect rc;
    GetClientRect(&rc);
    pDC->FillSolidRect(&rc, COLOR_LIST_BG);
    const int n = GetCount();
    for (int i = 0; i < n; ++i)
    {
        CRect itemRc;
        if (!GetItemRect(i, &itemRc)) continue;
        DRAWITEMSTRUCT dis = {};
        dis.CtlType = ODT_LISTBOX;
        dis.CtlID = (UINT)GetDlgCtrlID();
        dis.itemID = (UINT)i;
        dis.itemAction = ODA_DRAWENTIRE;
        dis.hwndItem = m_hWnd;
        dis.hDC = (HDC)wParam;
        dis.rcItem = itemRc;
        dis.itemState = (GetSel(i) > 0) ? ODS_SELECTED : 0;
        if (!IsWindowEnabled()) dis.itemState |= ODS_DISABLED;
        DrawItem(&dis);
    }
    return 0;
}

// ガラス下は FillRectOpaqueBits（素 Fill は透過穴）。それ以外は FillSolidRect。
BOOL CCustomListBox::OnEraseBkgnd(CDC* pDC)
{
    if (pDC)
    {
        CRect r;
        GetClientRect(&r);
        if (CCC_HostNeedsChildOpaque(m_hWnd))
            CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), r, COLOR_LIST_BG);
        else
            pDC->FillSolidRect(&r, COLOR_LIST_BG);
    }
    return TRUE;
}

// 行背景（選択/縞）+ 左デコ + テキスト。ガラス下の部分描画は不透明 Fill 必須。
// m_bAeroMode かつ非 Blur 子のときだけ半透明。黒文字は RGB(1,1,1) に寄せない（キー衝突）。
void CCustomListBox::DrawItem(LPDRAWITEMSTRUCT lp)
{
    if (lp->itemID == (UINT)-1) return;
    CDC* pDC = CDC::FromHandle(lp->hDC);
    CRect r = lp->rcItem;
    COLORREF bg = (lp->itemState & ODS_SELECTED) ? COLOR_SEL_BG : (lp->itemID % 2 == 0 ? COLOR_LIST_BG : COLOR_LIST_ALT);

    const BOOL bListAero = m_bAeroMode && !CCC_IsBlurDialogChild(m_hWnd);
    // 選択切替の部分描画はアクリル下で素 Fill が透過穴になる
    if (CCC_HostNeedsChildOpaque(m_hWnd))
        CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), r, bg);
    else if (bListAero)
        FillRectAlpha(pDC, r, bg, (lp->itemState & ODS_SELECTED) ? 180 : AERO_ALPHA_SEMI); // ガラス上の半透明行（Blur 子は不透明経路）
    else
        pDC->FillSolidRect(&r, bg);

    if ((lp->itemState & ODS_SELECTED) && !bListAero)
        DrawGlossHighlight(pDC, r, 6);

    int it = lp->itemID % 4;
    int is = max(8, r.Height() / 3);
    int ix = r.left + max(4, is / 2);
    int iy = r.top + (r.Height() - is) / 2;

    if (CCC_IsInwoman()) {
        // 花/星/ハートごまかし禁止 → バイブ／愛液／ピクン
        double breath = 0, twitch = 0, climax = 0;
        CCC_InwomanPulse(::GetTickCount(), breath, twitch, climax);
        const int cx = ix + is / 2, cy = iy + is / 2;
        switch (it) {
        case 0:
        case 2:
            CCC_DrawVibrator(pDC, cx, cy, is, (double)::GetTickCount(), twitch, breath, climax, FALSE);
            break;
        case 1:
        case 3:
            CCC_DrawLoveFluid(pDC, CRect(ix, iy, ix + is, iy + is + is / 2), breath, twitch, climax, FALSE);
            break;
        }
    } else switch (it)
    {
    case 0: DrawFlower(pDC, ix + is / 2, iy + is / 2, is / 2, RGB(255, 200, 220)); break;
    case 1: DrawStar(pDC, ix + is / 2, iy + is / 2, is / 3, RGB(255, 215, 0)); break;
    case 2:
        if (lp->itemState & ODS_SELECTED)
            DrawSoftJkHeart(pDC, CRect(ix, iy, ix + is, iy + is),
                (int)(::GetTickCount64() / 160), TRUE, COLOR_HEART);
        else
            DrawHeart(pDC, CRect(ix, iy, ix + is, iy + is), COLOR_HEART);
        break;
    case 3: DrawRibbon(pDC, CRect(ix, iy, ix + is, iy + is), RGB(255, 182, 193)); break;
    }

    if (lp->itemState & ODS_SELECTED) {
        if (CCC_IsInwoman()) {
            double breath = 0, twitch = 0, climax = 0;
            CCC_InwomanPulse(::GetTickCount(), breath, twitch, climax);
            CCC_DrawVibrator(pDC, r.right - max(10, is + 4), r.top + r.Height() / 2,
                max(8, is * 2 / 3), (double)::GetTickCount(), twitch, breath, climax, FALSE);
        } else {
            DrawStar(pDC, r.right - max(10, is + 4), r.top + r.Height() / 2, max(3, is / 3), RGB(255, 215, 0));
        }
    }

    CString st;
    GetText(lp->itemID, st);
    CRect rt = r;
    rt.left += max(20, is * 2 + 4);
    rt.DeflateRect(1, 1);

    const BOOL bSel = (lp->itemState & ODS_SELECTED) != 0;
    // aero 時の非選択文字は (1,1,1)=キーそのもの → 抜ける。意図は「ほぼ黒」。キーと衝突。
    COLORREF tc = bSel ? COLOR_LIST_SEL_TEXT : (m_bAeroMode ? RGB(1, 1, 1) : COLOR_EDIT_TEXT);
    pDC->SetTextColor(tc);
    pDC->SetBkMode(TRANSPARENT);

    CFont* po = pDC->SelectObject(GetFont());
    DrawListSubitemCellText(pDC, st, rt);
    pDC->SelectObject(po);

    if (lp->itemID < (UINT)(GetCount() - 1)) DrawLaceLine(pDC, r.left + 15, r.bottom - 1, r.right - 15, r.bottom - 1, RGB(200, 180, 220));
}

// 行高。DPI スケールの 24px 相当。フォント実測は PreSubclass の SetItemHeight 側。
void CCustomListBox::MeasureItem(LPMEASUREITEMSTRUCT lp)
{
    UINT dpi = 96;
    if (GetSafeHwnd())
        dpi = CCC_GetControlDpi(m_hWnd);
    lp->itemHeight = (UINT)CCC_ScaleDpi(24, dpi);
}

// ============================================================================
// カスタムコンボボックスコントロール
// CCustomComboBox
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomComboBox, CComboBox)

BEGIN_MESSAGE_MAP(CCustomComboBox, CComboBox)
    ON_WM_CTLCOLOR_REFLECT()
    ON_WM_PAINT()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
    ON_WM_ERASEBKGND()
    ON_CONTROL_REFLECT(CBN_DROPDOWN, &CCustomComboBox::OnDropdown)
END_MESSAGE_MAP()

// オーナードローコンボ。無効行は論理インデックスから除外する。
// m_brBackground はドロップリスト CtlColor 用。選択欄本体は PaintClient が塗る。
// m_bAeroMode は半透明フィル切替。キャプションガラス下では HostNeedsChildOpaque が勝つ。
CCustomComboBox::CCustomComboBox()
    : m_bAutoDelete(FALSE), m_clrLabelText(RGB(240, 240, 255)),
    m_clrLabelBg(RGB(80, 60, 120)), m_bAeroMode(FALSE)
{
    m_brBackground.CreateSolidBrush(COLOR_COMBO_BG);
}

// 背景ブラシのみ解放。HWND は既に破棄済み想定（PostNcDestroy 後）。
CCustomComboBox::~CCustomComboBox()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

// サブクラス解放後。EnableAutoDelete 時のみ delete this（ダイアログスタック配置では使わない）。
void CCustomComboBox::PostNcDestroy()
{
    CComboBox::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

// 物理行を追加し、bD なら無効ラベルとして記録する。
// 有効行だけ m_vSelectableIndices に積み、Get/SetCurSel の論理インデックスになる。
// 途中挿入はしない（常に末尾追加）。途中へ入れると論理/物理がずれる。
int CCustomComboBox::AddString(LPCTSTR lp, BOOL bD)
{
    int n = CComboBox::AddString(lp);
    if (n >= 0)
    {
        if (n >= (int)m_vDisabledItems.size()) m_vDisabledItems.resize(n + 1, FALSE);
        m_vDisabledItems[n] = bD;
        if (!bD) m_vSelectableIndices.push_back(n);
    }
    return n;
}

// 論理インデックスを返す。無効行は飛ばす。未選択・無効行選択中は -1。
// 物理値が欲しいときは GetCurSelPhysical（基底 GetCurSel）。
int CCustomComboBox::GetCurSel() const
{
    int np = CComboBox::GetCurSel();
    if (np < 0) return -1;
    for (int i = 0; i < (int)m_vSelectableIndices.size(); i++)
        if (m_vSelectableIndices[i] == np) return i;
    return -1;
}

// 論理インデックスで選択。範囲外は末尾有効行へクランプ。n<0 はクリア。
// 選択直後の WM_DRAWITEM(ODS_COMBOBOXEDIT) が枠を壊すため、
// CCC_WM_POST_OPAQUE_PAINT を Post して閉じた欄の不透明再描画を1フレーム遅らせる。
// SendMessage 同期再入はしない（描画中に再び DrawItem が走る）。
int CCustomComboBox::SetCurSel(int n)
{
    if (n < 0) {
        int r = CComboBox::SetCurSel(-1);
        if (m_hWnd) ::PostMessage(m_hWnd, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
        return r;
    }
    if (n >= (int)m_vSelectableIndices.size())
    {
        if (m_vSelectableIndices.empty()) return CB_ERR;
        n = (int)m_vSelectableIndices.size() - 1;
    }
    const int r = CComboBox::SetCurSel(m_vSelectableIndices[n]);
    if (m_hWnd) ::PostMessage(m_hWnd, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
    return r;
}

// 無効アイテム（グループ見出し）の文字色・背景。ドロップリスト DrawItem が参照する。
void CCustomComboBox::SetLabelColor(COLORREF ct, COLORREF cb)
{
    m_clrLabelText = ct;
    m_clrLabelBg = cb;
    if (GetSafeHwnd()) Invalidate();
}

// NULL ポインタは触らない。呼び出し側は片方だけ取ることができる。
void CCustomComboBox::GetLabelColor(COLORREF* pct, COLORREF* pcb) const
{
    if (pct) *pct = m_clrLabelText;
    if (pcb) *pcb = m_clrLabelBg;
}

// 論理→物理。無効行を除いた n 番目の実インデックス。範囲外は -1。
int CCustomComboBox::LogicalToPhysical(int n) const
{
    if (n < 0 || n >= (int)m_vSelectableIndices.size()) return -1;
    return m_vSelectableIndices[n];
}

// 物理→論理。無効行や欠番は -1（選択対象ではない）。
int CCustomComboBox::PhysicalToLogical(int n) const
{
    for (int i = 0; i < (int)m_vSelectableIndices.size(); i++)
        if (m_vSelectableIndices[i] == n) return i;
    return -1;
}

// VARIABLE を外し FIXED+HASSTRINGS。未指定のままだと MeasureItem が効かない。
// CLIPSIBLINGS は縦長 HWND（ドロップ確保）時に下段兄弟へはみ出すのを防ぐ。
// 選択欄高さ 19px@96dpi は MP ツールバーと揃える。28 はドロップ行のみ。
void CCustomComboBox::PreSubclassWindow()
{
    CComboBox::PreSubclassWindow();
    // VARIABLE を外し FIXED に。CLIPSIBLINGS で縦長 HWND 時の下段兄弟へのはみ出し描画を防ぐ。
    ModifyStyle(CBS_OWNERDRAWVARIABLE, CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_CLIPSIBLINGS);
    const UINT dpi = CCC_GetControlDpi(m_hWnd);
    // 選択欄は 19px@96dpi(MP ツールバー tbH と同一)。28 はドロップダウン行のみ。
    SetItemHeight(-1, CCC_ScaleDpi(19, dpi));
    SetItemHeight(0, CCC_ScaleDpi(28, dpi));
}

// ドロップリスト（子 LISTBOX）の背景だけ返す。選択欄は OnPaint/DrawItem が描く。
// ここで選択欄まで塗ると王冠・枠がシステム色で消える。
HBRUSH CCustomComboBox::CtlColor(CDC* pDC, UINT nC)
{
    if (nC == CTLCOLOR_LISTBOX)
    {
        pDC->SetBkColor(COLOR_COMBO_BG);
        pDC->SetTextColor(RGB(0, 0, 0));
        return (HBRUSH)m_brBackground.GetSafeHandle();
    }
    return NULL;
}

// ドロップダウン確保で HWND が縦長でも、描画は閉じた選択欄だけにする。
// CB_GETITEMHEIGHT(-1) はオーナードロー未指定コンボで CB_ERR になり、王冠が巨大化する。
static void CCC_ComboClipToClosedField(HWND hWnd, CRect& r)
{
    if (!hWnd || r.Height() <= 0)
        return;
    int closedH = 0;
    COMBOBOXINFO ci = { sizeof(ci) };
    if (::GetComboBoxInfo(hWnd, &ci)) {
        CRect vis = ci.rcItem;
        vis.UnionRect(&vis, &ci.rcButton);
        // rcItem はクライアント座標。top は 0 のまま高さだけ切る（メモリDCの原点とずらさない）
        if (vis.bottom - r.top >= 8)
            closedH = vis.bottom - r.top;
    }
    if (closedH <= 0) {
        const int selH = (int)(INT_PTR)::SendMessage(hWnd, CB_GETITEMHEIGHT, (WPARAM)-1, 0);
        if (selH > 0)
            closedH = selH;
        else {
            const UINT dpi = CCC_GetControlDpi(hWnd);
            closedH = CCC_ScaleDpi(24, dpi);
        }
    }
    if (closedH > 0 && r.Height() > closedH + 1)
        r.bottom = r.top + closedH;
}

// 閉じた選択欄だけ消す。ドロップ領域まで高さがある HWND を全面塗ると下段ボタンが消える。
// アクリル/キャプションガラス下の素 FillSolidRect は α=0 穴になるので OpaqueBits。
BOOL CCustomComboBox::OnEraseBkgnd(CDC* pDC)
{
    if (pDC)
    {
        CRect r;
        GetClientRect(&r);
        // MoveWindow でドロップ領域まで高さがある場合、選択欄だけ消す(下段ボタンを塗り潰さない)
        CCC_ComboClipToClosedField(m_hWnd, r);
        // アクリル/キャプションガラス下の素 FillSolidRect は α=0 穴になる
        if (CCC_HostNeedsChildOpaque(m_hWnd))
            CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), r, COLOR_COMBO_BG);
        else
            pDC->FillSolidRect(&r, COLOR_COMBO_BG);
    }
    return TRUE;
}

// 閉じた選択欄のオーナードロー本体。メモリDCに描いてから blit。
// bTrans（Aero かつ Blur 子でない）は黒下地+α半透明。それ以外は不透明 COLOR_COMBO_BG。
// 最後: 透過は ClearDestBlt（黒クロマ抜き）、不透明は InwomanBitBlt。
// クロマ blit をガラス親で使うと未描画が穴。HostNeedsChildOpaque 時は OnPaint 側で MakeOpaque。
// CBS_DROPDOWN/SIMPLE は編集欄テキスト優先（CurSel で上書きすると手入力が消える）。
void CCustomComboBox::PaintClient(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0)
        return;
    CCC_ComboClipToClosedField(m_hWnd, r);
    if (r.Height() <= 0)
        return;

    CDC mDC;
    CBitmap mB;
    mDC.CreateCompatibleDC(&dc);
    if (!mB.CreateCompatibleBitmap(&dc, r.Width(), r.Height()))
        return;
    CBitmap* ob = mDC.SelectObject(&mB);
    if (!ob)
        return;

    const BOOL bTrans = m_bAeroMode && !CCC_IsBlurDialogChild(m_hWnd);
    if (bTrans)
    {
        mDC.FillSolidRect(&r, RGB(0, 0, 0));
        FillRectAlpha(&mDC, r, COLOR_COMBO_BG, AERO_ALPHA_SEMI);
    }
    else mDC.FillSolidRect(&r, COLOR_COMBO_BG);

    CPen pF(PS_SOLID, 2, COLOR_VINE_DECO);
    CPen* op = mDC.SelectObject(&pF);
    mDC.SelectStockObject(NULL_BRUSH);
    const UINT dpi = CCC_GetControlDpi(m_hWnd);
    const int roundR = CCC_ScaleDpi(10, dpi);
    mDC.RoundRect(&r, CPoint(roundR, roundR));

    int nb = ::GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
    const int btnPad = CCC_ScaleDpi(4, dpi);
    CRect rB(r.right - nb - btnPad, r.top + btnPad, r.right - btnPad, r.bottom - btnPad);
    mDC.FillSolidRect(&rB, RGB(255, 200, 220));
    DrawGlossHighlight(&mDC, rB, 6);

    {
        CPen pb(PS_SOLID, 1, RGB(200, 150, 180));
        mDC.SelectObject(&pb);
        mDC.SelectStockObject(NULL_BRUSH);
        const int br = CCC_ScaleDpi(6, dpi);
        mDC.RoundRect(&rB, CPoint(br, br));
        mDC.SelectObject(op);
    }

    // 淫女: リボンごまかし→震えるバイブ。通常: 上品リボン
    {
        int cy2 = rB.Height() / 2 + rB.top;
        int bw = min(rB.Width() - 4, CCC_ScaleDpi(16, dpi));
        const int bh = CCC_ScaleDpi(5, dpi);
        const int cxB = rB.CenterPoint().x;
        if (CCC_IsInwoman()) {
            double breath = 0, twitch = 0, climax = 0;
            CCC_InwomanPulse(::GetTickCount(), breath, twitch, climax);
            const int vs = max(10, rB.Height() - 2);
            CCC_DrawVibrator(&mDC, cxB, cy2, vs, (double)::GetTickCount(), twitch, breath, climax, bTrans);
        } else {
            DrawBow(&mDC, CRect(cxB - bw / 2, cy2 - bh, cxB + bw / 2, cy2 + bh), COLOR_BOW);
            if (GetDroppedState())
                DrawSoftJkKnot(&mDC, CRect(cxB - 8, cy2 - 8, cxB + 8, cy2 + 8),
                    (int)(::GetTickCount64() / 40));
        }
    }

    if (CCC_IsInwoman()) {
        double breath = 0, twitch = 0, climax = 0;
        CCC_InwomanPulse(::GetTickCount(), breath, twitch, climax);
        CCC_DrawLoveFluid(&mDC, r, breath, twitch, climax, bTrans);
    } else {
        DrawSparkle(&mDC, r.right - CCC_ScaleDpi(8, dpi), r.top + CCC_ScaleDpi(8, dpi), CCC_ScaleDpi(4, dpi), COLOR_SPARKLE);
    }

    int nPS = CComboBox::GetCurSel();
    CString st;
    // CBS_DROPDOWN/SIMPLE: 編集欄の入力を優先（CurSel の LB 文字列で上書きすると手入力が消える）
    {
        const DWORD cbs = (GetStyle() & 0x0Ful);
        if (cbs == CBS_DROPDOWN || cbs == CBS_SIMPLE) {
            GetWindowText(st);
            if (st.IsEmpty() && nPS != CB_ERR)
                GetLBText(nPS, st);
        } else if (nPS != CB_ERR) {
            GetLBText(nPS, st);
            if (st.IsEmpty())
                GetWindowText(st);
        }
    }

    COLORREF tc = bTrans ? RGB(1, 1, 1) : RGB(0, 0, 0);
    mDC.SetTextColor(tc);

    CFont* pOF = mDC.SelectObject(GetFont());
    CRect rt = r;
    rt.left += CCC_ScaleDpi(12, dpi);
    rt.right = rB.left - btnPad;

    BOOL bIL = (nPS >= 0 && nPS < (int)m_vDisabledItems.size() && m_vDisabledItems[nPS]);
    if (nPS != CB_ERR && !bIL)
    {
        int cs = max(4, (rt.Height() - CCC_ScaleDpi(8, dpi)) / 2);
        const int csMax = max(4, rt.Height() / 2 - 1);
        const int csCap = CCC_ScaleDpi(6, dpi);
        if (cs > csMax) cs = csMax;
        if (cs > csCap) cs = csCap;
        // 文字の余地が無ければ王冠は省略（狭いコンボで名前が消えるのを防ぐ）
        if (rt.Width() - (cs * 2 + CCC_ScaleDpi(4, dpi)) >= CCC_ScaleDpi(24, dpi)) {
            DrawCrown(&mDC, rt.left + cs, rt.Height() / 2, cs, RGB(255, 215, 0));
            rt.left += cs * 2 + CCC_ScaleDpi(4, dpi);
        }
    }

    mDC.SetBkMode(TRANSPARENT);
    DrawFitControlText(&mDC, rt, st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, 0.50f);
    mDC.SelectObject(pOF);

    CCC_DrawInwoman(&mDC, r, bTrans);

    // 透過: 黒クロマ抜き。不透明: 背景色キーの BitBlt（ガラス親では OnPaint が MakeOpaque）
    if (bTrans) CCC_ClearDestBlt(dc.GetSafeHdc(), 0, 0, r.Width(), r.Height(), mDC.GetSafeHdc(), 0, 0, RGB(0, 0, 0));
    else CCC_InwomanBitBlt(dc.GetSafeHdc(), r.Width(), r.Height(), mDC.GetSafeHdc(), COLOR_COMBO_BG);

    mDC.SelectObject(ob);
    mB.DeleteObject();
    mDC.DeleteDC();
}

// CPaintDC + 閉じた欄へクリップ。BufferedPaint+MakeOpaque でポップアップ子の素 BitBlt 消えを防ぐ。
// WM_PRINTCLIENT とは別経路。こちらは更新リージョン付き。PrintClient で MakeOpaque すると
// ドロップ確保中の縦長バッファまで不透明化し王冠が巨大化する。
void CCustomComboBox::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
    CCC_ComboClipToClosedField(m_hWnd, r);
    if (r.Width() <= 0 || r.Height() <= 0) return;

    // ポップアップ子など: 素 BitBlt が消える環境向けに不透明化
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (hdcBuf && hBP) {
        CDC dcBuf;
        dcBuf.Attach(hdcBuf);
        PaintClient(dcBuf);
        dcBuf.Detach();
        ::BufferedPaintMakeOpaque(hBP, &r);
        ::EndBufferedPaint(hBP, TRUE);
        return;
    }
    PaintClient(dc);
}

// 親 OpaqueFixer / WM_PRINT から来る。CPaintDC を作らない（更新リージョンが空になる）。
// PaintClient 直呼び。ここでも BeginBufferedPaint すると二重バッファ＋縦長 HWND で破綻する。
// 戻り 0 で既定処理を抑止（システムが選択欄を上書きしない）。
LRESULT CCustomComboBox::OnPrintClient(WPARAM wParam, LPARAM)
{
    // CPaintDC 禁止。親バッファへ PaintClient のみ（MakeOpaque は OnPaint 側）
    if (HDC hDC = (HDC)wParam)
    {
        CDC dc;
        dc.Attach(hDC);
        PaintClient(dc);
        dc.Detach();
    }
    return 0;
}

// ODS_COMBOBOXEDIT（閉じた欄）は項目塗りをせず PaintClient へ。素塗りは枠・王冠を消し、
// アクリル下では α=0 穴。選択チェンジ直後の DRAWITEM が主因。
// ドロップ行は無効/選択/ゼブラ。HostNeeds なら OpaqueBits、Aero なら黒+α。
// ホバー相当の選択行は SoftJkHeart。無効行はラベル色＋イタリック。
void CCustomComboBox::DrawItem(LPDRAWITEMSTRUCT lp)
{
    if (lp->itemID == (UINT)-1) return;
    CDC* pDC = CDC::FromHandle(lp->hDC);
    if (!pDC) return;

    // 閉じた選択欄: 素の項目塗りは枠・王冠を消し、アクリル下では α=0 穴になる。
    // チェンジ直後の WM_DRAWITEM(ODS_COMBOBOXEDIT) が主因。OnPaint と同じ不透明経路へ。
        if (lp->itemState & ODS_COMBOBOXEDIT)
    {
        CRect r;
        GetClientRect(&r);
        CCC_ComboClipToClosedField(m_hWnd, r);
        if (r.Width() <= 0 || r.Height() <= 0) return;
        // アクリル下では常に α=255。HostNeeds 判定に頼ると初回/選択直後に白抜けする
        {
            BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
            params.dwFlags = BPPF_ERASE;
            HDC hdcBuf = NULL;
            HPAINTBUFFER hBP = ::BeginBufferedPaint(pDC->GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
            if (hdcBuf && hBP) {
                CDC dcBuf;
                dcBuf.Attach(hdcBuf);
                PaintClient(dcBuf);
                dcBuf.Detach();
                ::BufferedPaintMakeOpaque(hBP, &r);
                ::EndBufferedPaint(hBP, TRUE);
                return;
            }
        }
        PaintClient(*pDC);
        return;
    }

    CRect r = lp->rcItem;
    BOOL bD = (lp->itemID < (UINT)m_vDisabledItems.size()) && m_vDisabledItems[lp->itemID];
    BOOL bS = !bD && (lp->itemState & ODS_SELECTED);
    COLORREF bg = bD ? m_clrLabelBg : (bS ? COLOR_SEL_BG : (lp->itemID % 2 == 0 ? COLOR_COMBO_BG : RGB(255, 232, 220)));

    if (CCC_HostNeedsChildOpaque(m_hWnd))
        CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), r, bg);
    else if (m_bAeroMode && !CCC_IsBlurDialogChild(m_hWnd))
    {
        pDC->FillSolidRect(&r, RGB(0, 0, 0));
        FillRectAlpha(pDC, r, bg, bS ? 180 : bD ? 200 : AERO_ALPHA_SEMI);
    }
    else pDC->FillSolidRect(&r, bg);

    if (!bD)
    {
        int it = lp->itemID % 4;
        int is = max(8, r.Height() / 3);
        const int isMax = max(8, r.Height() / 2 - 1);
        if (is > isMax) is = isMax;
        int ix = r.left + max(4, is / 2);
        int iy = r.top + (r.Height() - is) / 2;
        if (CCC_IsInwoman()) {
            double breath = 0, twitch = 0, climax = 0;
            CCC_InwomanPulse(::GetTickCount(), breath, twitch, climax);
            const int cx = ix + is / 2, cy = iy + is / 2;
            if (it == 0 || it == 2)
                CCC_DrawVibrator(pDC, cx, cy, is, (double)::GetTickCount(), twitch, breath, climax, FALSE);
            else
                CCC_DrawLoveFluid(pDC, CRect(ix, iy, ix + is, iy + is + is / 2), breath, twitch, climax, FALSE);
        } else switch (it)
        {
        case 0: DrawFlower(pDC, ix + is / 2, iy + is / 2, is / 2, RGB(255, 200, 220)); break;
        case 1: DrawStar(pDC, ix + is / 2, iy + is / 2, is / 3, RGB(255, 215, 0)); break;
        case 2:
            if (lp->itemState & ODS_SELECTED)
                DrawSoftJkHeart(pDC, CRect(ix, iy, ix + is, iy + is),
                    (int)(::GetTickCount64() / 160), TRUE, COLOR_HEART);
            else
                DrawHeart(pDC, CRect(ix, iy, ix + is, iy + is), COLOR_HEART);
            break;
        case 3: DrawRibbon(pDC, CRect(ix, iy, ix + is, iy + is), RGB(255, 182, 193)); break;
        }
    }

    CString st;
    GetLBText(lp->itemID, st);
    CFont* pOF = NULL;
    CFont fc;
    CFont* pF = GetFont();
    LOGFONT lf = {};
    if (pF && pF->GetSafeHandle())
        pF->GetLogFont(&lf);
    else {
        NONCLIENTMETRICS ncm = {};
        ncm.cbSize = sizeof(ncm);
        if (::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            lf = ncm.lfMessageFont;
        else {
            lf.lfHeight = -12;
            _tcscpy_s(lf.lfFaceName, LF_FACESIZE, _T("MS UI Gothic"));
        }
    }

    if (bD)
    {
        pDC->SetTextColor(m_clrLabelText);
        lf.lfWeight = FW_BOLD;
        lf.lfItalic = TRUE;
    }
    else
    {
        pDC->SetTextColor(bS ? COLOR_LIST_SEL_TEXT : (m_bAeroMode ? RGB(1, 1, 1) : RGB(0, 0, 0)));
        lf.lfWeight = FW_BOLD;
    }
    fc.CreateFontIndirect(&lf);
    pOF = pDC->SelectObject(&fc);
    pDC->SetBkMode(TRANSPARENT);
    CRect rt = r;
    int iconPad = max(20, r.Height() * 2 / 3 + 4);
    const int iconPadMax = max(20, r.Width() / 3);
    if (iconPad > iconPadMax) iconPad = iconPadMax;
    rt.left += bD ? max(4, r.Height() / 6) : iconPad;
    pDC->DrawText(st, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (pOF)
    {
        pDC->SelectObject(pOF);
        fc.DeleteObject();
    }
    if (bS && !bD) {
        const int cr = max(6, min(r.Height() / 4, r.Height() / 2 - 1));
        DrawCrown(pDC, r.right - cr * 2 - 2, r.top + r.Height() / 2, cr, RGB(255, 215, 0));
    }
}

// ドロップダウン行高 28px@96dpi。選択欄高さは PreSubclass の SetItemHeight(-1)。
void CCustomComboBox::MeasureItem(LPMEASUREITEMSTRUCT lp)
{
    UINT dpi = 96;
    if (GetSafeHwnd())
        dpi = CCC_GetControlDpi(m_hWnd);
    lp->itemHeight = (UINT)CCC_ScaleDpi(28, dpi);
}

// 開く直前に文字列幅へ DroppedWidth を合わせる。狭いと無効ラベルが切れる。
void CCustomComboBox::OnDropdown()
{
    UpdateDropDownWidth();
}

// 無効アイテムが選ばれたら次の有効行へスキップ。前後とも無効ならクリア。
// ここで TRUE を返すと親の CBN_SELCHANGE が欠けるので、スキップ時のみ TRUE。
BOOL CCustomComboBox::OnCommand(WPARAM wP, LPARAM lP)
{
    WORD wN = HIWORD(wP);
    if (wN == CBN_SELCHANGE || wN == CBN_SELENDOK)
    {
        int n = CComboBox::GetCurSel();
        if (n >= 0)
        {
            BOOL bD = (n < (int)m_vDisabledItems.size() && m_vDisabledItems[n]);
            if (bD)
            {
                int cnt = CComboBox::GetCount();
                // 無効アイテムが選ばれた場合、次の有効アイテムへスキップ
                for (int i = n + 1; i < cnt; i++)
                {
                    if (!(i < (int)m_vDisabledItems.size() && m_vDisabledItems[i]))
                    {
                        CComboBox::SetCurSel(i);
                        return TRUE;
                    }
                }
                for (int i = n - 1; i >= 0; i--)
                {
                    if (!(i < (int)m_vDisabledItems.size() && m_vDisabledItems[i]))
                    {
                        CComboBox::SetCurSel(i);
                        return TRUE;
                    }
                }
                CComboBox::SetCurSel(-1); return TRUE;
            }
        }
    }
    return CComboBox::OnCommand(wP, lP);
}

// 全アイテムのテキスト幅 + スクロールバー + アイコン余白。ウィンドウ幅より狭くしない。
void CCustomComboBox::UpdateDropDownWidth()
{
    CClientDC dc(this);
    int mW = 0;
    CFont* po = dc.SelectObject(GetFont());

    for (int i = 0; i < GetCount(); i++)
    {
        CString s;
        GetLBText(i, s);
        mW = max(mW, dc.GetTextExtent(s).cx);
    }

    mW += ::GetSystemMetricsForDpi(SM_CXVSCROLL, CCC_GetControlDpi(m_hWnd)) + CCC_ScaleDpi(40, CCC_GetControlDpi(m_hWnd));
    CRect r;
    GetWindowRect(&r);
    SetDroppedWidth(max(mW, r.Width()));
    dc.SelectObject(po);
}

// ============================================================================
// CCustomSliderCtrl 実装
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomSliderCtrl, CSliderCtrl)

BEGIN_MESSAGE_MAP(CCustomSliderCtrl, CSliderCtrl)
    ON_WM_PAINT()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
    ON_WM_ERASEBKGND()
    ON_MESSAGE(WM_MOUSEMOVE, OnMouseMoveMsg)
    ON_MESSAGE(WM_LBUTTONDOWN, OnLButtonDownMsg)
    ON_MESSAGE(WM_LBUTTONUP, OnLButtonUpMsg)
    ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeaveMsg)
    ON_WM_TIMER()
END_MESSAGE_MAP()

// モード0/1/2 のオーナードローつまみ。シマー点はホバー中だけ発生し、離脱後は慣性で消える。
// backstore は毎描画 CreateCompatibleBitmap を避ける。chromaCache は Win11 アクリル用。
CCustomSliderCtrl::CCustomSliderCtrl() : m_bAutoDelete(FALSE), m_nMode(0), m_bAeroMode(FALSE),
    m_nShimmer(0), m_bHover(FALSE), m_nSparkleN(0), m_nSparkleSpawnAcc(0),
    m_backstoreW(0), m_backstoreH(0)
{
    ZeroMemory(m_sparklePos, sizeof(m_sparklePos));
}
// バックストアとクロマキャッシュを解放。タイマーは HWND 破棄で止まる。
CCustomSliderCtrl::~CCustomSliderCtrl()
{
    if (m_memBackstore.GetSafeHandle()) m_memBackstore.DeleteObject();
#if CCUSTOM_AERO_SUPPORT
    m_chromaCache.Release();
#endif
}

// きらめき軌跡の長さ（px）。横は左端〜つまみ、縦はつまみ〜下端（アクティブ側）。
// 範囲が潰れていると 0。DrawSlider の座標計算と一致させること。
int CCustomSliderCtrl::SparkleSpan(BOOL* pbVert)
{
    CRect r;
    GetClientRect(&r);
    int mn = 0, mx = 0;
    GetRange(mn, mx);
    const int np = GetPos();
    const BOOL bV = (GetStyle() & TBS_VERT) ? TRUE : FALSE;
    if (pbVert) *pbVert = bV;
    if (mx <= mn) return 0;
    if (!bV)
    {
        const int tL = 12, tR = r.Width() - 12;
        const int tP = tL + (int)((double)(np - mn) * (tR - tL) / (mx - mn));
        return tP - tL;
    }
    const int tT = 12, tB = r.Height() - 12;
    const int tP = tT + (int)((double)(np - mn) * (tB - tT) / (mx - mn));
    return tB - tP;
}

// 40ms タイマから呼ぶ。生存点を進め、span 到達で消滅。
// bSpawn（ホバー中）だけ先頭に等間隔発生。密着防止で gap/2 以内はスキップ。
// 離脱後は bSpawn=FALSE で残点だけ流し、全滅したら OnTimer が KillTimer。
void CCustomSliderCtrl::SparkleTick(BOOL bSpawn)
{
    const int speed = 4;
    BOOL bV = FALSE;
    const int span = SparkleSpan(&bV);
    if (span <= 8)
    {
        m_nSparkleN = 0;
        m_nSparkleSpawnAcc = 0;
        return;
    }
    const int gap = max(20, span / 6);

    // 進行・到達で消滅
    int w = 0;
    for (int i = 0; i < m_nSparkleN; ++i)
    {
        const int np = m_sparklePos[i] + speed;
        if (np >= span) continue;
        m_sparklePos[w++] = np;
    }
    m_nSparkleN = w;

    if (!bSpawn) return;

    // ホバー中: 等間隔で新しい点を先頭(0)に発生
    if (m_nSparkleN == 0)
    {
        m_sparklePos[0] = 0;
        m_nSparkleN = 1;
        m_nSparkleSpawnAcc = 0;
        return;
    }
    m_nSparkleSpawnAcc += speed;
    while (m_nSparkleSpawnAcc >= gap && m_nSparkleN < kSliderSparkleMax)
    {
        m_nSparkleSpawnAcc -= gap;
        // 先頭付近に既に点があればスキップ（密着防止）
        BOOL near0 = FALSE;
        for (int i = 0; i < m_nSparkleN; ++i)
        {
            if (m_sparklePos[i] < gap / 2) { near0 = TRUE; break; }
        }
        if (near0) break;
        m_sparklePos[m_nSparkleN++] = 0;
    }
}

// ホバー解除。残点がある間はタイマを止めない（慣性きらめき）。点ゼロなら即 Kill。
LRESULT CCustomSliderCtrl::OnMouseLeaveMsg(WPARAM, LPARAM)
{
    m_bHover = FALSE;
    // 残点が消えるまでタイマー継続（全滅したら OnTimer で止める）
    if (m_nSparkleN <= 0)
        KillTimer(kSliderShimmerTimerId);
    Invalidate(FALSE);
    return 0;
}

// kSliderShimmerTimerId: Shimmer カウンタ＋ SparkleTick。非ホバーかつ点ゼロで停止。
// Invalidate(FALSE) のみ。Erase するとアクリル下で黒フラッシュする。
void CCustomSliderCtrl::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kSliderShimmerTimerId)
    {
        m_nShimmer++;
        SparkleTick(m_bHover);
        if (!m_bHover && m_nSparkleN <= 0)
            KillTimer(kSliderShimmerTimerId);
        Invalidate(FALSE);
        return;
    }
    CSliderCtrl::OnTimer(nIDEvent);
}

// EnableAutoDelete 時のみ delete this。
void CCustomSliderCtrl::PostNcDestroy()
{
    CSliderCtrl::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

// 0=音符バー / 1=紫ダイヤ / 2=緑ダイヤ。未知値は DrawSlider が mode1 扱い。
void CCustomSliderCtrl::SetMode(int m)
{
    m_nMode = m;
    if (GetSafeHwnd()) Invalidate(FALSE);
}
// 値が同じなら何もしない。MirrorSeekVol 等が 60fps で呼ぶと
// UpdateWindow + 親アクリル Invalidate が毎フレ走り全体が約2倍重くなる。
void CCustomSliderCtrl::SetPos(int nPos, BOOL bRedraw)
{
	// 値が同じなら何もしない。MirrorSeekVol 等が 60fps で呼ぶと
	// UpdateWindow + 親アクリル Invalidate が毎フレ走り全体が約2倍重くなる。
	if (GetSafeHwnd() && CSliderCtrl::GetPos() == nPos)
		return;
	CSliderCtrl::SetPos(nPos);
	if (bRedraw && GetSafeHwnd())
	{
#if CCUSTOM_AERO_SUPPORT
		CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
		Invalidate(FALSE);
		UpdateWindow();
	}
}
// アクリル時も子は WS_EX_TRANSPARENT にしない（クリックが親へ抜ける）。描画側でクロマ/不透明を分ける。
void CCustomSliderCtrl::SetAeroMode(BOOL b)
{
    m_bAeroMode = b;
    if (GetSafeHwnd())
    {
#if CCUSTOM_AERO_SUPPORT
        CCC_SetChildTransparent(m_hWnd, FALSE);
#endif
        Invalidate(FALSE);
    }
}
// 基底へ委譲のみ。テーマ無効化は RangeSlider 側（こちらは標準トラックを自前上書き）。
void CCustomSliderCtrl::PreSubclassWindow()
{
    CSliderCtrl::PreSubclassWindow();
}

// メモリDCへ DrawSlider。HostNeedsChildOpaque ならクロマ禁止（穴抜き禁止）。
// 透過: クロマキー塗り→ BlitChromaCached（Win11）/ BlitChromaTrans。
// 不透明: COLOR_DIALOG_BG 塗り→ InwomanBitBlt。クロマ blit を不透明ホストで使うと縁が溶ける。
void CCustomSliderCtrl::PaintClient(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    const int rw = r.Width();
    const int rh = r.Height();
    if (rw <= 0 || rh <= 0) return;

    CDC mDC;
    mDC.CreateCompatibleDC(&dc);
    if (rw != m_backstoreW || rh != m_backstoreH || !m_memBackstore.GetSafeHandle())
    {
        if (m_memBackstore.GetSafeHandle()) m_memBackstore.DeleteObject();
        if (!m_memBackstore.CreateCompatibleBitmap(&dc, rw, rh)) {
            mDC.DeleteDC();
            return;
        }
        m_backstoreW = rw;
        m_backstoreH = rh;
    }
    CBitmap* ob = mDC.SelectObject(&m_memBackstore);

    BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    if (CCC_HostNeedsChildOpaque(m_hWnd))
        bTrans = FALSE;
    if (bTrans)
    {
        mDC.FillSolidRect(&r, CCC_AERO_CHROMA_KEY);
        DrawSlider(&mDC);
        CCC_DrawInwoman(&mDC, r, TRUE);
#if CCUSTOM_AERO_SUPPORT
        if (CCC_IsAeroEnabled() && CCC_IsWin11())
            CCC_BlitChromaCached(dc.GetSafeHdc(), 0, 0, rw, rh,
                mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY, m_chromaCache);
        else
#endif
            CCC_BlitChromaTrans(dc.GetSafeHdc(), 0, 0, rw, rh, mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    }
    else
    {
        mDC.FillSolidRect(&r, COLOR_DIALOG_BG);
        DrawSlider(&mDC);
        CCC_DrawInwoman(&mDC, r, FALSE);
        CCC_InwomanBitBlt(dc.GetSafeHdc(), rw, rh, mDC.GetSafeHdc(), COLOR_DIALOG_BG);
    }
    mDC.SelectObject(ob);
    mDC.DeleteDC();
}

// CCustomOpaqueFixer の BufferedPaint 面へ直描き。MakeOpaque は呼び出し側。
// Attach/Detach のみ。hdcBuf を DeleteDC してはいけない。
void CCustomSliderCtrl::PaintOpaqueIntoBuffer(HDC hdcBuf)
{
    if (!hdcBuf || !m_hWnd) return;
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0) return;
    CDC mem;
    mem.Attach(hdcBuf);
    mem.FillSolidRect(&r, COLOR_DIALOG_BG);
    DrawSlider(&mem);
    CCC_DrawInwoman(&mem, r, FALSE);
    mem.Detach();
}

// ガラス親向け α=255。BeginBufferedPaint+MakeOpaque。失敗時はメモリDC+InwomanBitBlt。
// クロマ blit は使わない（未描画が透明穴になる）。
void CCustomSliderCtrl::PaintOpaqueClient(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0) return;
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP) {
        CDC mem;
        CBitmap bmp;
        mem.CreateCompatibleDC(&dc);
        if (!bmp.CreateCompatibleBitmap(&dc, r.Width(), r.Height()))
            return;
        CBitmap* old = mem.SelectObject(&bmp);
        mem.FillSolidRect(0, 0, r.Width(), r.Height(), COLOR_DIALOG_BG);
        DrawSlider(&mem);
        CCC_DrawInwoman(&mem, r, FALSE);
        CCC_InwomanBitBlt(dc.GetSafeHdc(), r.Width(), r.Height(), mem.GetSafeHdc(), COLOR_DIALOG_BG);
        mem.SelectObject(old);
        return;
    }
    CDC mem;
    mem.Attach(hdcBuf);
    mem.FillSolidRect(&r, COLOR_DIALOG_BG);
    DrawSlider(&mem);
    CCC_DrawInwoman(&mem, r, FALSE);
    mem.Detach();
    ::BufferedPaintMakeOpaque(hBP, &r);
    ::EndBufferedPaint(hBP, TRUE);
}

// HostNeeds → PaintOpaqueClient。透過時は MakeOpaque 禁止（クロマを潰す）。
// 不透明は BufferedPaint+MakeOpaque（ポップアップ子や Win11 で素 BitBlt が消える）。
// WM_PRINTCLIENT は OnPrintClient → PaintClient。CPaintDC を Print 経路で作らない。
void CCustomSliderCtrl::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd))
    {
        PaintOpaqueClient(dc);
        return;
    }
#endif

    // 透過(アクリル)時はクロマ blit を潰す MakeOpaque を避ける(CCustomCheckBox と同じ)
    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    if (bTrans || r.Width() <= 0 || r.Height() <= 0)
    {
        PaintClient(dc);
        return;
    }

    // 不透明描画は常に MakeOpaque（ポップアップ子や Win11 で素 BitBlt が消える対策）
    {
        BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
        params.dwFlags = BPPF_ERASE;
        HDC hdcBuf = NULL;
        HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
        if (hdcBuf && hBP) {
            CDC dcBuf;
            dcBuf.Attach(hdcBuf);
            PaintClient(dcBuf);
            dcBuf.Detach();
            ::BufferedPaintMakeOpaque(hBP, &r);
            ::EndBufferedPaint(hBP, TRUE);
            return;
        }
    }
    PaintClient(dc);
}

// 親の印刷/OpaqueFixer 用。PaintClient 直呼び（CPaintDC なし）。
// ここで PaintOpaqueClient すると Fixer のバッファと二重 MakeOpaque になる。
LRESULT CCustomSliderCtrl::OnPrintClient(WPARAM wParam, LPARAM)
{
    if (HDC hDC = (HDC)wParam)
    {
        CDC dc;
        dc.Attach(hDC);
        PaintClient(dc);
        dc.Detach();
    }
    return 0;
}

// 透過時は消さない（親アクリルを残す）。不透明時だけ COLOR_DIALOG_BG。
// TRUE 返却で既定消去を抑止。
BOOL CCustomSliderCtrl::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseTransPaint(m_hWnd, m_bAeroMode)) return TRUE;
#endif
    if (pDC)
    {
        CRect r;
        GetClientRect(&r);
        pDC->FillSolidRect(&r, COLOR_DIALOG_BG);
    }
    return TRUE;
}

// Default でつまみ移動を処理してからホバー開始。初回だけ TrackMouseEvent(LEAVE)。
// 点が無ければ先頭に1つ置き、40ms シマータイマを張る。
LRESULT CCustomSliderCtrl::OnMouseMoveMsg(WPARAM w, LPARAM l)
{
    LRESULT r = Default();
    if (!m_bHover)
    {
        TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&t);
        m_bHover = TRUE;
        if (m_nSparkleN <= 0)
        {
            m_sparklePos[0] = 0;
            m_nSparkleN = 1;
            m_nSparkleSpawnAcc = 0;
        }
        SetTimer(kSliderShimmerTimerId, 40, NULL); // きらめき SparkleTick。LEAVE 後も残点がある間は止めない
    }
#if CCUSTOM_AERO_SUPPORT
    CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
    Invalidate(FALSE);
    return r;
}
// 既定ドラッグのあと親アクリルと自分を Invalidate。描画は次の WM_PAINT へ。
LRESULT CCustomSliderCtrl::OnLButtonDownMsg(WPARAM w, LPARAM l)
{
    LRESULT r = Default();
#if CCUSTOM_AERO_SUPPORT
    CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
    Invalidate(FALSE);
    return r;
}
// 既定確定のあと再描画。シマーはホバー中なら継続。
LRESULT CCustomSliderCtrl::OnLButtonUpMsg(WPARAM w, LPARAM l)
{
    LRESULT r = Default();
#if CCUSTOM_AERO_SUPPORT
    CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
    Invalidate(FALSE);
    return r;
}

// モード分岐の後、ホバー/残点のきらめきをトラック上に重ねる。
// 横は左→つまみ、縦は下→つまみ方向。span<=8 は点を描かない。
void CCustomSliderCtrl::DrawSlider(CDC* pDC)
{
    CRect r;
    GetClientRect(&r);
    int mn, mx;
    GetRange(mn, mx);
    int np = GetPos();
    if (mx <= mn) return;

    if (m_nMode == 0) DrawMode0(pDC, r, mn, mx, np);
    else if (m_nMode == 1) DrawMode1(pDC, r, mn, mx, np);
    else if (m_nMode == 2) DrawMode2(pDC, r, mn, mx, np);
    else DrawMode1(pDC, r, mn, mx, np);

    // ホバー中＋残点の慣性: 通ってきたトラック上をきらめきがスーッと流れる
    if ((m_bHover || m_nSparkleN > 0) && mx > mn)
    {
        const BOOL bV = (GetStyle() & TBS_VERT);
        if (!bV)
        {
            const int tL = 12, tR = r.Width() - 12;
            const int cY = r.Height() / 2;
            const int tP = tL + (int)((double)(np - mn) * (tR - tL) / (mx - mn));
            const int span = tP - tL;
            if (span > 8)
            {
                for (int di = 0; di < m_nSparkleN; ++di)
                {
                    const int pos = m_sparklePos[di];
                    if (pos < 0 || pos >= span) continue;
                    const int gx = tL + pos;
                    const int sz = (di == 0) ? 3 : 2;
                    DrawShine(pDC, gx, cY, sz, sz);
                    DrawSparkle(pDC, gx, cY, max(1, sz - 1), COLOR_SPARKLE);
                }
            }
        }
        else
        {
            // 縦は「つまみ〜下端」がアクティブ部分。シマーは下から上へ流す。
            const int tT = 12, tB = r.Height() - 12;
            const int cX = r.Width() / 2;
            const int tP = tT + (int)((double)(np - mn) * (tB - tT) / (mx - mn));
            const int span = tB - tP;
            if (span > 8)
            {
                for (int di = 0; di < m_nSparkleN; ++di)
                {
                    const int pos = m_sparklePos[di];
                    if (pos < 0 || pos >= span) continue;
                    const int gy = tB - pos;
                    const int sz = (di == 0) ? 3 : 2;
                    DrawShine(pDC, cX, gy, sz, sz);
                    DrawSparkle(pDC, cX, gy, max(1, sz - 1), COLOR_SPARKLE);
                }
            }
        }
    }
}

// 描画モード0: 楔形バー＋音符つまみ。ホバー時は SoftJkHeart ではなく SoftJkThumb を重ねる。
// リージョン AND で進捗色を乗せる。縦は下端がアクティブ。
void CCustomSliderCtrl::DrawMode0(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
    int nR = nMax - nMin;
    BOOL bV = (GetStyle() & TBS_VERT);

    if (!bV) // 横向き
    {
        int mX = 12, tL = rect.left + mX, tR = rect.right - mX, tW = tR - tL;
        if (tW <= 0) return;
        int tX = tL + (int)((double)(nPos - nMin) * tW / nR);
        int cY = rect.Height() / 2, bY = rect.bottom - 8;

        CPoint pts[4] = { {tL, bY}, {tL, bY - 2}, {tR, rect.top + 4}, {tR, bY} };
        CBrush bB(COLOR_RANGE_SELECTION);
        pDC->SelectObject(&bB);
        CPen pV(PS_SOLID, 1, COLOR_VINE_DECO);
        pDC->SelectObject(&pV);
        pDC->Polygon(pts, 4);

        if (tX > tL)
        {
            CRgn rP, rL;
            rP.CreatePolygonRgn(pts, 4, WINDING);
            rL.CreateRectRgn(rect.left, rect.top, tX, rect.bottom);
            rP.CombineRgn(&rP, &rL, RGN_AND);
            CBrush bA(RGB(180, 200, 255));
            pDC->FillRgn(&rP, &bA);
        }
        CRect rN(tX - 10, cY - 12, tX + 10, cY + 12);
        DrawMusicNote(pDC, rN, RGB(138, 43, 226));
        if (m_bHover || m_nSparkleN > 0) {
            const float tilt = (nR > 0) ? ((float)(nPos - nMin) / (float)nR * 24.f - 12.f) : 0.f;
            DrawSoftJkThumb(pDC, CRect(tX - 8, cY - 8, tX + 8, cY + 8),
                (int)(m_nShimmer + ::GetTickCount64() / 40), TRUE, tilt);
        }
        DrawStar(pDC, tX - 12, cY - 14, 2, RGB(255, 215, 0));
        DrawStar(pDC, tX + 12, cY - 14, 2, RGB(255, 215, 0));
    }
    else // 縦向き
    {
        int mY = 12, tT = rect.top + mY, tB = rect.bottom - mY, tH = tB - tT;
        if (tH <= 0) return;
        int tY = tT + (int)((double)(nPos - nMin) * tH / nR), cX = rect.Width() / 2;
        CPoint pts[4] = { {cX - 8, tB}, {cX - 2, tT}, {cX + 2, tT}, {cX + 8, tB} };
        CBrush bB(COLOR_RANGE_SELECTION);
        pDC->SelectObject(&bB);
        CPen pV2(PS_SOLID, 1, COLOR_VINE_DECO);
        pDC->SelectObject(&pV2);
        pDC->Polygon(pts, 4);

        if (tY < tB)
        {
            CRgn rP, rB2;
            rP.CreatePolygonRgn(pts, 4, WINDING);
            rB2.CreateRectRgn(rect.left, tY, rect.right, rect.bottom);
            rP.CombineRgn(&rP, &rB2, RGN_AND);
            CBrush bA(RGB(180, 200, 255));
            pDC->FillRgn(&rP, &bA);
        }
        CRect rN(cX - 10, tY - 12, cX + 10, tY + 12);
        DrawMusicNote(pDC, rN, RGB(138, 43, 226));
        DrawStar(pDC, cX + 14, tY, 2, RGB(255, 215, 0));
    }
}

// 描画モード1: 紫グラデ線＋ダイヤ。ペン/ブラシはプール（毎描画 CreatePen 禁止）。
void CCustomSliderCtrl::DrawMode1(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
    int nR = nMax - nMin;
    BOOL bV = (GetStyle() & TBS_VERT);
    auto selPen = [&](int w, COLORREF c) {
        CPen* p = CCC_GetPooledPen(w, c);
        if (p) pDC->SelectObject(p);
    };
    auto selBrush = [&](COLORREF c) -> CBrush* {
        CBrush* b = CCC_GetPooledBrush(c);
        if (b) { pDC->SelectObject(b); return b; }
        return nullptr;
    };
    if (!bV)
    {
        int cY = rect.Height() / 2;
        int tL = 12;
        int tR = rect.Width() - 12;
        int tW = tR - tL;
        if (tW <= 0) return;
        int tP = tL + (int)((double)(nPos - nMin) * tW / nR);
        CPen* oldPen = pDC->GetCurrentPen();
        CBrush* oldBrush = pDC->GetCurrentBrush();
        selPen(5, RGB(200, 150, 255));
        pDC->MoveTo(tL, cY);
        pDC->LineTo(tP, cY);
        selPen(3, RGB(220, 220, 230));
        pDC->LineTo(tR, cY);
        selPen(2, RGB(150, 100, 200));
        for (int i = 0; i <= 10; i++)
        {
            int nx = tL + tW * i / 10;
            int nh = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(nx, cY - nh);
            pDC->LineTo(nx, cY + nh);
            if (i % 5 == 0)
            {
                selBrush(RGB(200, 180, 255));
                pDC->Ellipse(nx - 3, cY - nh - 5, nx + 3, cY - nh + 1);
            }
        }
        CRect rD(tP - 9, cY - 12, tP + 9, cY + 12);
        DrawDiamond(pDC, rD, RGB(200, 180, 255));
        DrawSparkle(pDC, tP, cY - 16, 3, COLOR_SPARKLE);
        selPen(1, RGB(255, 240, 200));
        for (int a = 0; a < 360; a += 45)
        {
            double r = a * 3.14159 / 180.0;
            pDC->MoveTo(tP + (int)(12 * cos(r)), cY + (int)(12 * sin(r)));
            pDC->LineTo(tP + (int)(18 * cos(r)), cY + (int)(18 * sin(r)));
        }
        if (oldPen) pDC->SelectObject(oldPen);
        if (oldBrush) pDC->SelectObject(oldBrush);
    }
    else
    {
        int cX = rect.Width() / 2;
        int tT = 12;
        int tB = rect.Height() - 12;
        int tH = tB - tT;
        if (tH <= 0) return;
        int tP = tT + (int)((double)(nPos - nMin) * tH / nR);
        CPen* oldPen = pDC->GetCurrentPen();
        CBrush* oldBrush = pDC->GetCurrentBrush();
        selPen(5, RGB(200, 150, 255));
        pDC->MoveTo(cX, tP);
        pDC->LineTo(cX, tB);
        selPen(3, RGB(220, 220, 230));
        pDC->MoveTo(cX, tT);
        pDC->LineTo(cX, tP);
        selPen(2, RGB(150, 100, 200));
        for (int i = 0; i <= 10; i++)
        {
            int ny = tT + tH * i / 10;
            int nw = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(cX - nw, ny);
            pDC->LineTo(cX + nw, ny);
            if (i % 5 == 0)
            {
                selBrush(RGB(200, 180, 255));
                pDC->Ellipse(cX + nw + 1, ny - 3, cX + nw + 7, ny + 3);
            }
        }
        CRect rD(cX - 9, tP - 12, cX + 9, tP + 12);
        DrawDiamond(pDC, rD, RGB(200, 180, 255));
        if (oldPen) pDC->SelectObject(oldPen);
        if (oldBrush) pDC->SelectObject(oldBrush);
    }
}

// 描画モード2: 緑グラデ線＋ダイヤ。mode1 と同構造、色だけ差し替え。
void CCustomSliderCtrl::DrawMode2(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
    int nR = nMax - nMin;
    BOOL bV = (GetStyle() & TBS_VERT);
    auto selPen = [&](int w, COLORREF c) {
        CPen* p = CCC_GetPooledPen(w, c);
        if (p) pDC->SelectObject(p);
    };
    auto selBrush = [&](COLORREF c) -> CBrush* {
        CBrush* b = CCC_GetPooledBrush(c);
        if (b) { pDC->SelectObject(b); return b; }
        return nullptr;
    };
    if (!bV)
    {
        int cY = rect.Height() / 2;
        int tL = 12;
        int tR = rect.Width() - 12;
        int tW = tR - tL;
        if (tW <= 0) return;
        int tP = tL + (int)((double)(nPos - nMin) * tW / nR);
        CPen* oldPen = pDC->GetCurrentPen();
        CBrush* oldBrush = pDC->GetCurrentBrush();
        selPen(5, RGB(100, 200, 150));
        pDC->MoveTo(tL, cY);
        pDC->LineTo(tP, cY);
        selPen(3, RGB(220, 220, 230));
        pDC->LineTo(tR, cY);
        selPen(2, RGB(80, 160, 120));
        for (int i = 0; i <= 10; i++)
        {
            int nx = tL + tW * i / 10;
            int nh = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(nx, cY - nh);
            pDC->LineTo(nx, cY + nh);
            if (i % 5 == 0)
            {
                selBrush(RGB(150, 220, 180));
                pDC->Ellipse(nx - 3, cY - nh - 5, nx + 3, cY - nh + 1);
            }
        }
        CRect rD(tP - 9, cY - 12, tP + 9, cY + 12);
        DrawDiamond(pDC, rD, RGB(100, 220, 160));
        DrawSparkle(pDC, tP, cY - 16, 3, COLOR_SPARKLE);
        selPen(1, RGB(200, 255, 220));
        for (int a = 0; a < 360; a += 45)
        {
            double r = a * 3.14159 / 180.0;
            pDC->MoveTo(tP + (int)(12 * cos(r)), cY + (int)(12 * sin(r)));
            pDC->LineTo(tP + (int)(18 * cos(r)), cY + (int)(18 * sin(r)));
        }
        if (oldPen) pDC->SelectObject(oldPen);
        if (oldBrush) pDC->SelectObject(oldBrush);
    }
    else
    {
        int cX = rect.Width() / 2;
        int tT = 12;
        int tB = rect.Height() - 12;
        int tH = tB - tT;
        if (tH <= 0) return;
        int tP = tT + (int)((double)(nPos - nMin) * tH / nR);
        CPen* oldPen = pDC->GetCurrentPen();
        CBrush* oldBrush = pDC->GetCurrentBrush();
        selPen(5, RGB(100, 200, 150));
        pDC->MoveTo(cX, tP);
        pDC->LineTo(cX, tB);
        selPen(3, RGB(220, 220, 230));
        pDC->MoveTo(cX, tT);
        pDC->LineTo(cX, tP);
        selPen(2, RGB(80, 160, 120));
        for (int i = 0; i <= 10; i++)
        {
            int ny = tT + tH * i / 10;
            int nw = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(cX - nw, ny);
            pDC->LineTo(cX + nw, ny);
            if (i % 5 == 0)
            {
                selBrush(RGB(150, 220, 180));
                pDC->Ellipse(cX + nw + 1, ny - 3, cX + nw + 7, ny + 3);
            }
        }
        CRect rD(cX - 9, tP - 12, cX + 9, tP + 12);
        DrawDiamond(pDC, rD, RGB(100, 220, 160));
        if (oldPen) pDC->SelectObject(oldPen);
        if (oldBrush) pDC->SelectObject(oldBrush);
    }
}

// ============================================================================
// CCustomRangeSliderCtrl 実装
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomRangeSliderCtrl, CSliderCtrl)

BEGIN_MESSAGE_MAP(CCustomRangeSliderCtrl, CSliderCtrl)
    ON_WM_PAINT()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_SETCURSOR()
    ON_WM_RBUTTONUP()
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnTtnNeedText)
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnTtnNeedText)
END_MESSAGE_MAP()

// 再生位置 + loop1/2 + A-B。loop つまみは既定ロック（シークを食わない）。
// 波形・キュー・LRC・拍グリッド・ホバー拡大はすべてオプション。未設定は描かない。
CCustomRangeSliderCtrl::CCustomRangeSliderCtrl()
    : m_bAutoDelete(FALSE), m_nMin(0), m_nMax(100), m_nSelMin(0), m_nSelMax(100),
    m_nAbA(-1), m_nAbB(-1), m_bSelLocked(TRUE),
    m_nDragTarget(0), m_bDragging(FALSE), m_nVisualPos(0), m_nLogicalPos(0), m_bAeroMode(FALSE),
    m_wavePeakCount(0), m_cueCount(0), m_nCueClick(-1),
    m_lrcCount(0), m_nLrcClick(-1), m_bHoverZoom(TRUE), m_hoverZoomX(-1),
    m_ribbonN(0), m_xfadePreviewMs(0), m_timeBaseHz(44100),
    m_beatBpm(120.f), m_bBeatGrid(FALSE), m_beatOffsetMs(0), m_beatMeter(4), m_bHoverTracking(FALSE),
    m_backstoreW(0), m_backstoreH(0)
{
    ZeroMemory(m_wavePeaks, sizeof(m_wavePeaks));
    ZeroMemory(m_cueFrames, sizeof(m_cueFrames));
    ZeroMemory(m_lrcFrames, sizeof(m_lrcFrames));
    ZeroMemory(m_ribbon, sizeof(m_ribbon));
}
// バックストアとクロマキャッシュを解放。ツールチップ HWND は MFC が破棄。
CCustomRangeSliderCtrl::~CCustomRangeSliderCtrl()
{
    if (m_memBackstore.GetSafeHandle()) m_memBackstore.DeleteObject();
#if CCUSTOM_AERO_SUPPORT
    m_chromaCache.Release();
#endif
}

// EnableAutoDelete 時のみ delete this。
void CCustomRangeSliderCtrl::PostNcDestroy()
{
    CSliderCtrl::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}
// 子を TRANSPARENT にしない。描画は PaintClient のクロマ/不透明分岐。
void CCustomRangeSliderCtrl::SetAeroMode(BOOL b)
{
    m_bAeroMode = b;
    if (GetSafeHwnd())
    {
#if CCUSTOM_AERO_SUPPORT
        CCC_SetChildTransparent(m_hWnd, FALSE);
#endif
        Invalidate(FALSE);
    }
}
// UxTheme を空にしてオーナードローを優先（標準トラックが枠を残すのを防ぐ）。
// 基底の Range/Pos を内部 min/max/logical へ同期。
void CCustomRangeSliderCtrl::PreSubclassWindow()
{
    CSliderCtrl::PreSubclassWindow();

    // テーマを無効化してオーナードロー描画を優先
    HMODULE h = LoadLibrary(_T("UxTheme.dll"));
    if (h)
    {
        typedef HRESULT(WINAPI* S)(HWND, LPCWSTR, LPCWSTR);
        S p = (S)GetProcAddress(h, "SetWindowTheme");
        if (p) p(m_hWnd, L"", L"");
        FreeLibrary(h);
    }

    int mn, mx;
    CSliderCtrl::GetRange(mn, mx);
    m_nMin = mn;
    m_nMax = mx;
    m_nLogicalPos = m_nVisualPos = CSliderCtrl::GetPos();
}

// ホバー時刻チップへ RelayEvent。親 PreTranslate からも呼ばれる想定。
BOOL CCustomRangeSliderCtrl::PreTranslateMessage(MSG* pMsg)
{
    if (m_hoverTip.GetSafeHwnd())
        m_hoverTip.RelayEvent(pMsg);
    return CSliderCtrl::PreTranslateMessage(pMsg);
}

// ドラッグ中は無視（親の再生追従がつまみを引き戻す）。非表示なら Invalidate しない。
// 同一値は描画しない。
void CCustomRangeSliderCtrl::SetPos(int p)
{
    if (m_bDragging) return;
    p = max(m_nMin, min(m_nMax, p));
    if (p == m_nLogicalPos && p == m_nVisualPos) return;
    m_nLogicalPos = m_nVisualPos = p;
    CSliderCtrl::SetPos(p);
    // 非表示(MP時の og->m_time)は描画不要。表示中は Invalidate(ドラッグは UPDATENOW)。
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

// ドラッグ中は見た目位置。親が確定シークに旧 LogicalPos を使わないための分岐。
int CCustomRangeSliderCtrl::GetPos() const
{
    // ドラッグ中は見た目位置を返す（親の確定シークが旧 LogicalPos を拾わない）
    return m_bDragging ? m_nVisualPos : m_nLogicalPos;
}

// 内部 min/max と基底 Range を同期。変化なしなら b のときだけ再描画。
void CCustomRangeSliderCtrl::SetRange(int mn, int mx, BOOL b)
{
    if (mn == m_nMin && mx == m_nMax) {
        if (b && ::IsWindow(m_hWnd)) Invalidate(FALSE);
        return;
    }
    m_nMin = mn;
    m_nMax = mx;
    CSliderCtrl::SetRange(mn, mx, FALSE);
    if (b && ::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

// loop1/2。mn>mx は入れ替え。A-B（m_nAbA/B）とは別変数。
void CCustomRangeSliderCtrl::SetSelection(int mn, int mx)
{
    if (mn > mx) { int t = mn; mn = mx; mx = t; }
    if (mn == m_nSelMin && mx == m_nSelMax) return;
    m_nSelMin = mn;
    m_nSelMax = mx;
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

// A-B。-1 は未設定。A のみでつまみ表示、B>A で区間塗り。非表示なら Invalidate しない。
void CCustomRangeSliderCtrl::SetAB(int a, int b)
{
    if (a == m_nAbA && b == m_nAbB) return;
    m_nAbA = a;
    m_nAbB = b;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

// 未設定は -1 のまま返す。
void CCustomRangeSliderCtrl::GetAB(int& a, int& b) const
{
    a = m_nAbA;
    b = m_nAbB;
}

// TRUE で loop つまみドラッグ不可（既定）。A-B は常に可。HitTest がシークを優先する。
void CCustomRangeSliderCtrl::SetSelectionLocked(BOOL bLocked)
{
    if ((bLocked ? TRUE : FALSE) == m_bSelLocked) return;
    m_bSelLocked = bLocked ? TRUE : FALSE;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

// 0..1 ピークを内部コピー（最大 kWavePeaksMax）。count<=0 で消去。同一内容は描画しない。
void CCustomRangeSliderCtrl::SetWavePeaks(const float* peaks, int count)
{
    if (count <= 0 || !peaks) {
        if (m_wavePeakCount == 0) return;
        m_wavePeakCount = 0;
        if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
            Invalidate(FALSE);
        return;
    }
    if (count > kWavePeaksMax) count = kWavePeaksMax;
    BOOL same = (count == m_wavePeakCount);
    if (same) {
        for (int i = 0; i < count; ++i) {
            if (m_wavePeaks[i] != peaks[i]) { same = FALSE; break; }
        }
    }
    if (same) return;
    for (int i = 0; i < count; ++i) {
        float v = peaks[i];
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        m_wavePeaks[i] = v;
    }
    m_wavePeakCount = count;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

// 波形オーバービューを消す。SetWavePeaks(NULL,0) へ委譲。
void CCustomRangeSliderCtrl::ClearWavePeaks()
{
    SetWavePeaks(NULL, 0);
}

// 再生位置ビンへ最大合成。フル概観（より多い bins）が載っている間は壊さない。
// 近傍 0.7 でならす。60fps 呼び出しでも amp が小さければ Invalidate しない。
void CCustomRangeSliderCtrl::AccumulateWaveAtPos(int pos, float amp, int bins)
{
    if (m_nMax <= m_nMin) return;
    if (amp < 0.f) amp = 0.f;
    if (amp > 1.f) amp = 1.f;
    if (bins < 32) bins = 32;
    if (bins > kWavePeaksMax) bins = kWavePeaksMax;
    // フル概観(大きめ bins)が載っている間はリアルタイムで壊さない
    if (m_wavePeakCount > bins)
        return;
    if (m_wavePeakCount != bins) {
        ZeroMemory(m_wavePeaks, sizeof(m_wavePeaks));
        m_wavePeakCount = bins;
    }
    int bin = (int)((__int64)(pos - m_nMin) * bins / (m_nMax - m_nMin));
    if (bin < 0) bin = 0;
    if (bin >= bins) bin = bins - 1;
    if (amp <= m_wavePeaks[bin]) return;
    m_wavePeaks[bin] = amp;
    // 近傍も少しならして見た目を埋める
    if (bin > 0 && amp * 0.7f > m_wavePeaks[bin - 1]) m_wavePeaks[bin - 1] = amp * 0.7f;
    if (bin + 1 < bins && amp * 0.7f > m_wavePeaks[bin + 1]) m_wavePeaks[bin + 1] = amp * 0.7f;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

// キューマーカー（フレーム）。クリックは HitTest 10+i → GetCueClick。
void CCustomRangeSliderCtrl::SetCues(const int* frames, int count)
{
    if (count <= 0 || !frames) {
        if (m_cueCount == 0) return;
        m_cueCount = 0;
        if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
            Invalidate(FALSE);
        return;
    }
    if (count > kCueMax) count = kCueMax;
    BOOL same = (count == m_cueCount);
    if (same) {
        for (int i = 0; i < count; ++i) {
            if (m_cueFrames[i] != frames[i]) { same = FALSE; break; }
        }
    }
    if (same) return;
    for (int i = 0; i < count; ++i)
        m_cueFrames[i] = frames[i];
    m_cueCount = count;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

// キュー全消去。
void CCustomRangeSliderCtrl::ClearCues()
{
    SetCues(NULL, 0);
}

// LRC 時刻線。クリックは HitTest 30+i → GetLrcClick。
void CCustomRangeSliderCtrl::SetLrcMarkers(const int* frames, int count)
{
    if (count <= 0 || !frames) {
        if (m_lrcCount == 0) return;
        m_lrcCount = 0;
        if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
            Invalidate(FALSE);
        return;
    }
    if (count > kLrcMarkMax) count = kLrcMarkMax;
    BOOL same = (count == m_lrcCount);
    if (same) {
        for (int i = 0; i < count; ++i) {
            if (m_lrcFrames[i] != frames[i]) { same = FALSE; break; }
        }
    }
    if (same) return;
    for (int i = 0; i < count; ++i)
        m_lrcFrames[i] = frames[i];
    m_lrcCount = count;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

// LRC マーカー全消去。
void CCustomRangeSliderCtrl::ClearLrcMarkers()
{
    SetLrcMarkers(NULL, 0);
}

// 範囲外は -1。クリック後のジャンプ先フレーム。
int CCustomRangeSliderCtrl::GetLrcFrame(int idx) const
{
    if (idx < 0 || idx >= m_lrcCount) return -1;
    return m_lrcFrames[idx];
}

// 波形があるときホバー拡大レンズ。ドラッグ中は Draw 側で出さない。
void CCustomRangeSliderCtrl::SetHoverZoom(BOOL on)
{
    m_bHoverZoom = on ? TRUE : FALSE;
}

// スペアナ細いバー（最大 64）。同一内容は描画しない。
void CCustomRangeSliderCtrl::SetMeterRibbon(const float* bins, int n)
{
    if (n <= 0 || !bins) {
        if (m_ribbonN == 0) return;
        m_ribbonN = 0;
        if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
            Invalidate(FALSE);
        return;
    }
    if (n > kRibbonMax) n = kRibbonMax;
    BOOL same = (n == m_ribbonN);
    if (same) {
        for (int i = 0; i < n; ++i) {
            if (m_ribbon[i] != bins[i]) { same = FALSE; break; }
        }
    }
    if (same) return;
    for (int i = 0; i < n; ++i) {
        float v = bins[i];
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        m_ribbon[i] = v;
    }
    m_ribbonN = n;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

// 書き出しクロスフェード帯。0 で非表示。フレーム換算は timeBaseHz。
void CCustomRangeSliderCtrl::SetXfadePreviewMs(int ms)
{
    if (ms < 0) ms = 0;
    if (ms == m_xfadePreviewMs) return;
    m_xfadePreviewMs = ms;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

// ホバー時刻・拍グリッド・xfade のフレーム換算。8000 未満は 44100 へ戻す。
void CCustomRangeSliderCtrl::SetTimeBaseHz(int hz)
{
    if (hz < 8000) hz = 44100;
    if (hz == m_timeBaseHz) return;
    m_timeBaseHz = hz;
}

// 拍グリッド。オフセット/拍子は現状値を維持。
void CCustomRangeSliderCtrl::SetBeatGrid(float bpm, BOOL enabled)
{
	SetBeatGrid(bpm, enabled, m_beatOffsetMs, m_beatMeter);
}

// 位相オフセット付き。拍子は現状値。
void CCustomRangeSliderCtrl::SetBeatGrid(float bpm, BOOL enabled, int offsetMs)
{
	SetBeatGrid(bpm, enabled, offsetMs, m_beatMeter);
}

// bpm<=1 は 120。拍子 2..16（既定 4）。同一設定は Invalidate しない。
void CCustomRangeSliderCtrl::SetBeatGrid(float bpm, BOOL enabled, int offsetMs, int beatsPerBar)
{
    if (bpm <= 1.f) bpm = 120.f;
    if (beatsPerBar < 2) beatsPerBar = 4;
    if (beatsPerBar > 16) beatsPerBar = 16;
    const BOOL en = enabled ? TRUE : FALSE;
    if (en == m_bBeatGrid && fabsf(bpm - m_beatBpm) < 0.01f && offsetMs == m_beatOffsetMs && beatsPerBar == m_beatMeter) return;
    m_bBeatGrid = en;
    m_beatBpm = bpm;
    m_beatOffsetMs = offsetMs;
    m_beatMeter = beatsPerBar;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

// 遅延生成。LPSTR_TEXTCALLBACK で OnTtnNeedText へ。
void CCustomRangeSliderCtrl::EnsureHoverTip()
{
    if (m_hoverTip.GetSafeHwnd()) return;
    if (!GetSafeHwnd()) return;
    m_hoverTip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX);
    m_hoverTip.Activate(TRUE);
    m_hoverTip.SetDelayTime(TTDT_INITIAL, 200);
    m_hoverTip.SetDelayTime(TTDT_RESHOW, 80);
    m_hoverTip.SetDelayTime(TTDT_AUTOPOP, 8000);
    CRect r; GetClientRect(&r);
    m_hoverTip.AddTool(this, LPSTR_TEXTCALLBACK, &r, 1);
}

// ピクセル→値→絶対時刻と残り。テキスト変化時だけ UpdateTipText。
void CCustomRangeSliderCtrl::UpdateHoverTip(CPoint p)
{
    EnsureHoverTip();
    if (!m_hoverTip.GetSafeHwnd()) return;
    const int v = PixelToValue(p.x);
    const int hz = (m_timeBaseHz > 0) ? m_timeBaseHz : 44100;
    int sec = v / hz;
    if (sec < 0) sec = 0;
    int rem = 0;
    if (m_nMax > m_nMin)
        rem = (m_nMax - v) / hz;
    if (rem < 0) rem = 0;
    auto fmt = [](int s, CString& out) {
        if (s >= 3600)
            out.Format(_T("%d:%02d:%02d"), s / 3600, (s / 60) % 60, s % 60);
        else
            out.Format(_T("%d:%02d"), s / 60, s % 60);
    };
    CString absT, remT;
    fmt(sec, absT);
    fmt(rem, remT);
    CString t;
    t.Format(_T("%s  (-%s)"), (LPCTSTR)absT, (LPCTSTR)remT);
    if (t != m_hoverTipText) {
        m_hoverTipText = t;
        m_hoverTip.UpdateTipText(m_hoverTipText, this, 1);
    }
}

// TTN_NEEDTEXT W/A。TTF_IDISHWND は無視（ツール ID=1 固定）。静的バッファを渡す。
BOOL CCustomRangeSliderCtrl::OnTtnNeedText(UINT, NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    if (!pNMHDR) return FALSE;
    if (pNMHDR->code == TTN_NEEDTEXTW) {
        TOOLTIPTEXTW* pTTT = (TOOLTIPTEXTW*)pNMHDR;
        if (pTTT->uFlags & TTF_IDISHWND) return FALSE;
        static WCHAR s_buf[64];
        wcsncpy_s(s_buf, (LPCWSTR)(LPCTSTR)m_hoverTipText, _TRUNCATE);
        pTTT->lpszText = s_buf;
        return TRUE;
    }
    if (pNMHDR->code == TTN_NEEDTEXTA) {
        TOOLTIPTEXTA* pTTT = (TOOLTIPTEXTA*)pNMHDR;
        if (pTTT->uFlags & TTF_IDISHWND) return FALSE;
        static char s_bufA[64];
#ifdef _UNICODE
        WideCharToMultiByte(CP_ACP, 0, m_hoverTipText, -1, s_bufA, 64, NULL, NULL);
#else
        strncpy_s(s_bufA, m_hoverTipText, _TRUNCATE);
#endif
        pTTT->lpszText = s_bufA;
        return TRUE;
    }
    return FALSE;
}

// 再生追従一括更新。ドラッグ中は無視。ab に 0x80000000 はその項目を触らない。
// 見た目 px が変わったときだけ Invalidate。UPDATENOW 禁止（timerp 内同期描画が二重になる）。
void CCustomRangeSliderCtrl::SetPlaybackMirror(int nPos, int selMin, int selMax, int rangeMin, int rangeMax,
    int abA, int abB)
{
    if (m_bDragging) return;
    if (rangeMax <= rangeMin) rangeMax = rangeMin + 1;
    if (selMin > selMax) { int t = selMin; selMin = selMax; selMax = t; }
    selMin = max(rangeMin, min(rangeMax, selMin));
    selMax = max(rangeMin, min(rangeMax, selMax));
    nPos = max(rangeMin, min(rangeMax, nPos));

    const int kAbKeep = (int)0x80000000;
    const BOOL touchAb = (abA != kAbKeep || abB != kAbKeep);
    int newAbA = touchAb ? abA : m_nAbA;
    int newAbB = touchAb ? abB : m_nAbB;
    if (touchAb) {
        if (abA == kAbKeep) newAbA = m_nAbA;
        if (abB == kAbKeep) newAbB = m_nAbB;
    }

    // range 更新前の見た目(px)。サブピクセルの値変化は描画しない。
    const int oldThumb = ValueToPixel(m_nLogicalPos);
    const int oldSel0 = ValueToPixel(m_nSelMin);
    const int oldSel1 = ValueToPixel(m_nSelMax);
    const int oldAbA = (m_nAbA >= 0) ? ValueToPixel(m_nAbA) : -1;
    const int oldAbB = (m_nAbB >= 0) ? ValueToPixel(m_nAbB) : -1;

    BOOL dirty = FALSE;
    if (rangeMin != m_nMin || rangeMax != m_nMax) {
        m_nMin = rangeMin;
        m_nMax = rangeMax;
        CSliderCtrl::SetRange(rangeMin, rangeMax, FALSE);
        dirty = TRUE;
    }
    if (selMin != m_nSelMin || selMax != m_nSelMax) {
        m_nSelMin = selMin;
        m_nSelMax = selMax;
        dirty = TRUE;
    }
    if (touchAb && (newAbA != m_nAbA || newAbB != m_nAbB)) {
        m_nAbA = newAbA;
        m_nAbB = newAbB;
        dirty = TRUE;
    }
    if (nPos != m_nLogicalPos || nPos != m_nVisualPos) {
        m_nLogicalPos = m_nVisualPos = nPos;
        CSliderCtrl::SetPos(nPos);
        dirty = TRUE;
    }
    if (!dirty || !::IsWindow(m_hWnd) || !::IsWindowVisible(m_hWnd))
        return;

    const int newThumb = ValueToPixel(m_nLogicalPos);
    const int newSel0 = ValueToPixel(m_nSelMin);
    const int newSel1 = ValueToPixel(m_nSelMax);
    const int newAbAPx = (m_nAbA >= 0) ? ValueToPixel(m_nAbA) : -1;
    const int newAbBPx = (m_nAbB >= 0) ? ValueToPixel(m_nAbB) : -1;
    if (newThumb == oldThumb && newSel0 == oldSel0 && newSel1 == oldSel1
        && newAbAPx == oldAbA && newAbBPx == oldAbB)
        return;

    // UPDATENOW だと timerp 内で同期描画→直後のバナー Invalidate と合わせて毎フレ2回塗る。
    // Invalidate のみにして次の WM_PAINT に合流させる（見た目の追従は十分）。
    if (::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

// loop 選択を範囲内へクランプして返す。
void CCustomRangeSliderCtrl::GetSelection(int& mn, int& mx) const
{
    mn = max(m_nMin, min(m_nMax, m_nSelMin));
    mx = max(m_nMin, min(m_nMax, m_nSelMax));
}

// スライダーと同様。キャプションのみアクリルのホストでは穴抜き禁止（HostNeeds で bTrans オフ）。
// 透過はクロマ blit、不透明は InwomanBitBlt。
void CCustomRangeSliderCtrl::PaintClient(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    const int rw = r.Width();
    const int rh = r.Height();
    if (rw <= 0 || rh <= 0) return;

    CDC mDC;
    mDC.CreateCompatibleDC(&dc);
    if (rw != m_backstoreW || rh != m_backstoreH || !m_memBackstore.GetSafeHandle())
    {
        if (m_memBackstore.GetSafeHandle()) m_memBackstore.DeleteObject();
        if (!m_memBackstore.CreateCompatibleBitmap(&dc, rw, rh)) {
            mDC.DeleteDC();
            return;
        }
        m_backstoreW = rw;
        m_backstoreH = rh;
    }
    CBitmap* ob = mDC.SelectObject(&m_memBackstore);

    // キャプションのみアクリルのホストでは穴抜き禁止。音量スライダーと同じ不透明塗り。
    BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    if (CCC_HostNeedsChildOpaque(m_hWnd))
        bTrans = FALSE;
    if (bTrans)
    {
        mDC.FillSolidRect(&r, CCC_AERO_CHROMA_KEY);
        DrawRangeSlider(&mDC);
        CCC_DrawInwoman(&mDC, r, TRUE);
#if CCUSTOM_AERO_SUPPORT
        if (CCC_IsAeroEnabled() && CCC_IsWin11())
            CCC_BlitChromaCached(dc.GetSafeHdc(), 0, 0, rw, rh,
                mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY, m_chromaCache);
        else
#endif
            CCC_BlitChromaTrans(dc.GetSafeHdc(), 0, 0, rw, rh,
                mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    }
    else
    {
        mDC.FillSolidRect(&r, COLOR_DIALOG_BG);
        DrawRangeSlider(&mDC);
        CCC_DrawInwoman(&mDC, r, FALSE);
        CCC_InwomanBitBlt(dc.GetSafeHdc(), rw, rh, mDC.GetSafeHdc(), COLOR_DIALOG_BG);
    }
    mDC.SelectObject(ob);
    mDC.DeleteDC();
}

// OpaqueFixer バッファへ直描き。MakeOpaque は呼び出し側。
void CCustomRangeSliderCtrl::PaintOpaqueIntoBuffer(HDC hdcBuf)
{
    if (!hdcBuf || !m_hWnd) return;
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0) return;
    CDC mem;
    mem.Attach(hdcBuf);
    mem.FillSolidRect(&r, COLOR_DIALOG_BG);
    DrawRangeSlider(&mem);
    CCC_DrawInwoman(&mem, r, FALSE);
    mem.Detach();
}

// α=255 BufferedPaint。失敗時はメモリDC。クロマ禁止。
void CCustomRangeSliderCtrl::PaintOpaqueClient(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0) return;
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP) {
        CDC mem;
        CBitmap bmp;
        mem.CreateCompatibleDC(&dc);
        if (!bmp.CreateCompatibleBitmap(&dc, r.Width(), r.Height()))
            return;
        CBitmap* old = mem.SelectObject(&bmp);
        mem.FillSolidRect(0, 0, r.Width(), r.Height(), COLOR_DIALOG_BG);
        DrawRangeSlider(&mem);
        CCC_DrawInwoman(&mem, r, FALSE);
        CCC_InwomanBitBlt(dc.GetSafeHdc(), r.Width(), r.Height(), mem.GetSafeHdc(), COLOR_DIALOG_BG);
        mem.SelectObject(old);
        return;
    }
    CDC mem;
    mem.Attach(hdcBuf);
    mem.FillSolidRect(&r, COLOR_DIALOG_BG);
    DrawRangeSlider(&mem);
    CCC_DrawInwoman(&mem, r, FALSE);
    mem.Detach();
    ::BufferedPaintMakeOpaque(hBP, &r);
    ::EndBufferedPaint(hBP, TRUE);
}

// HostNeeds → OpaqueClient。透過は PaintClient（MakeOpaque しない）。
// WM_PRINTCLIENT は Print 経路。CPaintDC と混ぜない。
void CCustomRangeSliderCtrl::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd))
    {
        PaintOpaqueClient(dc);
        return;
    }
#endif
    BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    if (bTrans || r.Width() <= 0 || r.Height() <= 0) {
        PaintClient(dc);
        return;
    }
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (hdcBuf && hBP) {
        CDC dcBuf;
        dcBuf.Attach(hdcBuf);
        PaintClient(dcBuf);
        dcBuf.Detach();
        ::BufferedPaintMakeOpaque(hBP, &r);
        ::EndBufferedPaint(hBP, TRUE);
        return;
    }
    PaintClient(dc);
}

// PaintClient 直呼び。ここで OpaqueClient すると Fixer と二重になる。
LRESULT CCustomRangeSliderCtrl::OnPrintClient(WPARAM wParam, LPARAM)
{
    if (HDC hDC = (HDC)wParam)
    {
        CDC dc;
        dc.Attach(hDC);
        PaintClient(dc);
        dc.Detach();
    }
    return 0;
}

// 透過時は親を残す。不透明時だけ背景塗り。
BOOL CCustomRangeSliderCtrl::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseTransPaint(m_hWnd, m_bAeroMode)) return TRUE;
#endif
    if (pDC)
    {
        CRect r;
        GetClientRect(&r);
        pDC->FillSolidRect(&r, COLOR_DIALOG_BG);
    }
    return TRUE;
}

// 奥から波形→拍グリッド→トラック/loop/A-B/キュー/LRC→ホバー拡大→再生ハート。
// 波形ありは R2_XORPEN（バーと波形を同時に読む）。数字・ハッチは一時 COPY に戻す。
// 再生つまみはホバー/ドラッグ中 SoftJkHeart、通常は DrawHeart。
void CCustomRangeSliderCtrl::DrawRangeSlider(CDC* pDC)
{
    CRect r;
    GetClientRect(&r);
    if (m_nMax <= m_nMin) return;

    int cy = r.Height() / 2;
    int cur = m_bDragging ? m_nVisualPos : m_nLogicalPos;
    int xMn = ValueToPixel(m_nSelMin);
    int xMx = ValueToPixel(m_nSelMax);
    int xP = ValueToPixel(cur);
    const int x0 = 14;
    const int tw = r.Width() - 28;
    const int half = max(4, min(cy - 1, r.Height() / 2 - 1));
    const BOOL bWave = (m_wavePeakCount > 1 && tw > 0);

    CPen* oldPen = pDC->GetCurrentPen();
    CBrush* oldBrush = pDC->GetCurrentBrush();

    // 最奥: 波形（トラック／ループ／A-B／つまみの下）
    if (bWave) {
        COLORREF wc = m_bAeroMode ? RGB(140, 200, 255) : COLOR_SEEK_WAVE;
        for (int x = 0; x < tw; ++x) {
            int bin = x * m_wavePeakCount / tw;
            if (bin < 0) bin = 0;
            if (bin >= m_wavePeakCount) bin = m_wavePeakCount - 1;
            int h = (int)(m_wavePeaks[bin] * half + 0.5f);
            if (h < 1) continue;
            pDC->FillSolidRect(x0 + x, cy - h, 1, h * 2, wc);
        }
    }

    // 拍グリッド(薄い縦線、小節頭はアクセント)
    if (m_bBeatGrid && m_timeBaseHz > 0) {
        const float bpm = (m_beatBpm > 1.f) ? m_beatBpm : 120.f;
        const double framesPerBeat = (double)m_timeBaseHz * 60.0 / (double)bpm;
        if (framesPerBeat > 1.0) {
            const int span = m_nMax - m_nMin;
            int maxLines = (int)(span / framesPerBeat) + 2;
            if (maxLines > 256) maxLines = 256;
            const int meter = (m_beatMeter >= 2 && m_beatMeter <= 16) ? m_beatMeter : 4;
            COLORREF gc = m_bAeroMode ? RGB(60, 60, 70) : RGB(210, 215, 225);
            COLORREF gcBar = m_bAeroMode ? RGB(90, 90, 110) : RGB(160, 170, 190);
            for (int i = 0; i < maxLines; ++i) {
                const int offFrames = (int)(((__int64)m_beatOffsetMs * m_timeBaseHz) / 1000);
                int fv = m_nMin + offFrames + (int)(i * framesPerBeat + 0.5);
                if (fv < m_nMin) continue;
                if (fv > m_nMax) break;
                int x = ValueToPixel(fv);
                const BOOL bar = ((i % meter) == 0);
                if (bar)
                    pDC->FillSolidRect(x, cy - 14, 2, 28, gcBar);
                else
                    pDC->FillSolidRect(x, cy - 10, 1, 20, gc);
            }
        }
    }

    // 波形あり: シークバー一式を XOR 合成（波形もバーも同時に読める）
    // 波形なし: 従来どおり不透明塗り
    const int oldRop = bWave ? pDC->SetROP2(R2_XORPEN) : 0;

    auto xorOrFill = [&](const CRect& rc, COLORREF c) {
        if (rc.Width() <= 0 || rc.Height() <= 0) return;
        if (bWave) {
            CBrush br(c);
            CBrush* obr = pDC->SelectObject(&br);
            HGDIOBJ op = pDC->SelectObject(::GetStockObject(NULL_PEN));
            pDC->Rectangle(rc.left, rc.top, rc.right, rc.bottom);
            pDC->SelectObject(op);
            pDC->SelectObject(obr);
        } else {
            pDC->FillSolidRect(rc, c);
        }
    };

    // トラック（バー）— プール済みペン（毎描画 CreatePen 禁止）
    {
        const COLORREF trackC = bWave ? RGB(255, 255, 255) : RGB(200, 200, 200);
        if (CPen* pT = CCC_GetPooledPen(bWave ? 3 : 4, trackC))
            pDC->SelectObject(pT);
        pDC->MoveTo(14, cy);
        pDC->LineTo(r.Width() - 14, cy);
    }

    // ループ選択帯(loop1/2) — 波形の上
    if (xMx > xMn)
        xorOrFill(CRect(xMn, cy - 4, xMx, cy + 4), COLOR_RANGE_SELECTION);

    // スペアナ・リボン(トラック中央の上)
    if (m_ribbonN > 0 && tw > 0) {
        const int barH = max(3, min(cy - 2, 10));
        for (int i = 0; i < m_ribbonN; ++i) {
            int x1 = x0 + i * tw / m_ribbonN;
            int x2 = x0 + (i + 1) * tw / m_ribbonN;
            if (x2 <= x1) x2 = x1 + 1;
            int h = (int)(m_ribbon[i] * barH + 0.5f);
            if (h < 1) continue;
            xorOrFill(CRect(x1, cy - 2 - h, x2 - 1, cy - 2), RGB(80, 180, 255));
        }
    }

    // 書き出しクロスフェード帯プレビュー(範囲末尾のハッチ)
    // ハッチは XOR と相性が悪いので一時的に COPY に戻す
    if (m_xfadePreviewMs > 0 && m_timeBaseHz > 0) {
        const int xfFrames = (int)(((__int64)m_xfadePreviewMs * m_timeBaseHz) / 1000);
        if (xfFrames > 0) {
            int endV = m_nSelMax;
            if (endV <= m_nSelMin) endV = m_nMax;
            int startV = endV - xfFrames;
            if (startV < m_nMin) startV = m_nMin;
            int xa = ValueToPixel(startV);
            int xb = ValueToPixel(endV);
            if (xb > xa) {
                if (bWave) pDC->SetROP2(R2_COPYPEN);
                CRect hr(xa, cy - 6, xb, cy + 6);
                CBrush brHat;
                brHat.CreateHatchBrush(HS_BDIAGONAL, RGB(255, 140, 60));
                CBrush* obr = pDC->SelectObject(&brHat);
                int oldBk = pDC->SetBkMode(TRANSPARENT);
                pDC->FillRect(&hr, &brHat);
                pDC->SetBkMode(oldBk);
                pDC->SelectObject(obr);
                if (bWave) pDC->SetROP2(R2_XORPEN);
            }
        }
    }

    // A-B 区間帯（B 確定後のみ）。ループ帯の上に重ねる。
    if (m_nAbA >= 0 && m_nAbB > m_nAbA) {
        int xA = ValueToPixel(m_nAbA);
        int xB = ValueToPixel(m_nAbB);
        if (xB > xA)
            xorOrFill(CRect(xA, cy - 3, xB, cy + 3), COLOR_AB_RANGE);
    }

    COLORREF penC = m_bAeroMode ? RGB(1, 1, 1) : RGB(0, 0, 0);
    if (bWave) penC = RGB(255, 255, 255); // XOR 縁取りは白が波形上で読みやすい
    if (CPen* pB = CCC_GetPooledPen(1, penC))
        pDC->SelectObject(pB);
    pDC->SelectObject(GetStockObject(NULL_BRUSH));

    // loop1/2 つまみ（ロック時も表示。ドラッグ不可は HitTest 側）
    {
        COLORREF th = m_bSelLocked ? RGB(220, 220, 220) : COLOR_RANGE_SLIDER_THUMB;
        if (bWave && m_bSelLocked) th = RGB(180, 180, 180);
        xorOrFill(CRect(xMn - 5, cy - 8, xMn + 5, cy + 8), th);
        pDC->Rectangle(CRect(xMn - 5, cy - 8, xMn + 5, cy + 8));
        xorOrFill(CRect(xMx - 5, cy - 8, xMx + 5, cy + 8), th);
        pDC->Rectangle(CRect(xMx - 5, cy - 8, xMx + 5, cy + 8));
    }

    // A-B つまみ（A 時点から別色。B は区間確定後）
    if (m_nAbA >= 0) {
        int xA = ValueToPixel(m_nAbA);
        xorOrFill(CRect(xA - 5, cy - 8, xA + 5, cy + 8), COLOR_AB_SLIDER_THUMB);
        pDC->Rectangle(CRect(xA - 5, cy - 8, xA + 5, cy + 8));
    }
    if (m_nAbA >= 0 && m_nAbB > m_nAbA) {
        int xB = ValueToPixel(m_nAbB);
        xorOrFill(CRect(xB - 5, cy - 8, xB + 5, cy + 8), COLOR_AB_SLIDER_THUMB);
        pDC->Rectangle(CRect(xB - 5, cy - 8, xB + 5, cy + 8));
    }

    // キューマーカー（上向き三角 + 番号）
    if (m_cueCount > 0) {
        COLORREF cueC = COLOR_SEEK_CUE;
        for (int i = 0; i < m_cueCount; ++i) {
            int x = ValueToPixel(m_cueFrames[i]);
            POINT tri[3];
            tri[0].x = x;     tri[0].y = cy - 11;
            tri[1].x = x - 5; tri[1].y = cy - 3;
            tri[2].x = x + 5; tri[2].y = cy - 3;
            CBrush br(cueC);
            CBrush* obr = pDC->SelectObject(&br);
            pDC->Polygon(tri, 3);
            pDC->SelectObject(obr);
            // 数字は XOR だと潰れるので COPY に戻して描く
            if (bWave) pDC->SetROP2(R2_COPYPEN);
            TCHAR dig[2] = { (TCHAR)(_T('1') + i), 0 };
            if (i >= 9) dig[0] = _T('0');
            pDC->SetBkMode(TRANSPARENT);
            pDC->SetTextColor(m_bAeroMode ? RGB(2, 2, 2) : RGB(40, 30, 0));
            CRect tr(x - 4, cy - 22, x + 5, cy - 11);
            pDC->DrawText(dig, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            if (bWave) pDC->SetROP2(R2_XORPEN);
        }
    }

    // LRC 時刻マーカー（下向き細い線）
    if (m_lrcCount > 0) {
        if (bWave) pDC->SetROP2(R2_COPYPEN);
        if (CPen* pL = CCC_GetPooledPen(1, RGB(80, 180, 255)))
            pDC->SelectObject(pL);
        for (int i = 0; i < m_lrcCount; ++i) {
            int x = ValueToPixel(m_lrcFrames[i]);
            pDC->MoveTo(x, cy + 2);
            pDC->LineTo(x, cy + 10);
        }
        if (bWave) pDC->SetROP2(R2_XORPEN);
    }

    // ホバー拡大波形
    if (m_bHoverZoom && m_hoverZoomX >= 0 && m_wavePeakCount > 8 && !m_bDragging) {
        if (bWave) pDC->SetROP2(R2_COPYPEN);
        CRect zr(m_hoverZoomX - 36, 2, m_hoverZoomX + 36, r.Height() - 2);
        if (zr.left < 2) zr.OffsetRect(2 - zr.left, 0);
        if (zr.right > r.Width() - 2) zr.OffsetRect(r.Width() - 2 - zr.right, 0);
        CBrush zb(RGB(20, 24, 36));
        CBrush* ob = pDC->SelectObject(&zb);
        pDC->Rectangle(zr);
        pDC->SelectObject(ob);
        const int centerVal = PixelToValue(m_hoverZoomX);
        const int halfSpan = max(1, (m_nMax - m_nMin) / 40);
        const int v0 = max(m_nMin, centerVal - halfSpan);
        const int v1 = min(m_nMax, centerVal + halfSpan);
        if (CPen* pZ = CCC_GetPooledPen(1, RGB(120, 220, 255)))
            pDC->SelectObject(pZ);
        const int zw = max(1, zr.Width());
        for (int x = zr.left; x < zr.right; ++x) {
            const double t = (double)(x - zr.left) / (double)zw;
            const int vv = v0 + (int)(t * (v1 - v0));
            int bi = 0;
            if (m_nMax > m_nMin)
                bi = (int)(((__int64)(vv - m_nMin) * m_wavePeakCount) / (m_nMax - m_nMin));
            if (bi < 0) bi = 0;
            if (bi >= m_wavePeakCount) bi = m_wavePeakCount - 1;
            float amp = m_wavePeaks[bi];
            if (amp < 0.f) amp = 0.f;
            if (amp > 1.f) amp = 1.f;
            const int hh = (int)(amp * (zr.Height() / 2 - 2));
            pDC->MoveTo(x, cy - hh);
            pDC->LineTo(x, cy + hh);
        }
        if (bWave) pDC->SetROP2(R2_XORPEN);
    }

    // 現在位置（Soft3D ハート + きらめき）— 波形時は XOR で波形を潰さない
    {
        CRect rh(xP - 9, cy - 12, xP + 9, cy + 6);
        if (m_bDragging || m_bHoverTracking)
            DrawSoftJkHeart(pDC, rh, (int)(::GetTickCount64() / 80), TRUE, COLOR_SLIDER_THUMB);
        else
            DrawHeart(pDC, rh, COLOR_SLIDER_THUMB);
    }
    DrawSparkle(pDC, xP + 7, cy - 12, 3, COLOR_SPARKLE);
    // 再生位置の縦ガイド（波形全体で位置が追える）
    if (bWave) {
        if (CPen* pG = CCC_GetPooledPen(1, RGB(255, 255, 255)))
            pDC->SelectObject(pG);
        pDC->MoveTo(xP, 1);
        pDC->LineTo(xP, r.Height() - 1);
    }

    if (bWave) pDC->SetROP2(oldRop);
    if (oldBrush) pDC->SelectObject(oldBrush);
    if (oldPen) pDC->SelectObject(oldPen);
}

// 左右 14px 余白。範囲潰れは左端。
int CCustomRangeSliderCtrl::ValueToPixel(int v) const
{
    CRect r;
    GetClientRect(&r);
    int w = r.Width() - 28;
    if (w <= 0 || m_nMax <= m_nMin) return 14;
    return 14 + (int)((long long)(max(m_nMin, min(m_nMax, v)) - m_nMin) * w / (m_nMax - m_nMin));
}

// 余白外は min/max へクランプ。
int CCustomRangeSliderCtrl::PixelToValue(int x) const
{
    CRect r;
    GetClientRect(&r);
    int w = r.Width() - 28;
    if (w <= 0 || m_nMax <= m_nMin) return m_nMin;
    return m_nMin + (int)((double)(max(14, min(r.Width() - 14, x)) - 14) / w * (m_nMax - m_nMin) + 0.5);
}

// 戻り値: 0=なし 1=loop最小 2=loop最大 3=シーク 4=A 5=B。
// 10+i=キュー、30+i=LRC。優先は A-B → キュー/LRC →（ロック解除時のみ）loop → シーク。
// ロック中は loop を当てず 0 を返し、OnLButtonDown がトラック空白＝シークへ落とす。
// つまみ矩形は描画より少し広い（掴みやすくするため）。
int CCustomRangeSliderCtrl::HitTest(CPoint p) const
{
    // 0 none / 1 loop min / 2 loop max / 3 seek / 4 A / 5 B （10+ cue, 30+ LRC）
    CRect r;
    GetClientRect(&r);
    int cy = r.Height() / 2;
    int xM = ValueToPixel(m_bDragging ? m_nVisualPos : m_nLogicalPos);
    int xMx = ValueToPixel(m_nSelMax);
    int xMn = ValueToPixel(m_nSelMin);

    // A-B つまみは常に優先
    if (m_nAbA >= 0 && m_nAbB > m_nAbA) {
        int xB = ValueToPixel(m_nAbB);
        if (CRect(xB - 7, cy - 10, xB + 7, cy + 10).PtInRect(p)) return 5;
    }
    if (m_nAbA >= 0) {
        int xA = ValueToPixel(m_nAbA);
        if (CRect(xA - 7, cy - 10, xA + 7, cy + 10).PtInRect(p)) return 4;
    }
    for (int i = 0; i < m_cueCount; ++i) {
        int x = ValueToPixel(m_cueFrames[i]);
        if (CRect(x - 6, cy - 22, x + 6, cy - 1).PtInRect(p)) return 10 + i;
    }
    for (int i = 0; i < m_lrcCount; ++i) {
        int x = ValueToPixel(m_lrcFrames[i]);
        if (CRect(x - 4, cy + 1, x + 4, cy + 12).PtInRect(p)) return 30 + i;
    }
    // ロック解除時のみ loop つまみを再生位置より優先。ロック中はシークを優先（クリックを食わない）
    if (!m_bSelLocked) {
        if (CRect(xMx - 7, cy - 10, xMx + 7, cy + 10).PtInRect(p)) return 2;
        if (CRect(xMn - 7, cy - 10, xMn + 7, cy + 10).PtInRect(p)) return 1;
    }
    if (CRect(xM - 10, cy - 14, xM + 10, cy + 14).PtInRect(p)) return 3;
    return 0;
}
// Alt+ドラッグは拍グリッド位相（target=99）。キュー/LRC はクリック通知のみ（ドラッグしない）。
// ロック中の loop ヒットは 0 扱い→トラックシーク。空白クリックもシーク（target=3）。
void CCustomRangeSliderCtrl::OnLButtonDown(UINT f, CPoint p)
{
#if CCUSTOM_AERO_SUPPORT
    CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
    SetFocus();
    // Alt+ドラッグ: 拍グリッド位相オフセット
    if ((f & MK_ALT) && m_bBeatGrid) {
        m_nDragTarget = 99; // grid offset
        m_bDragging = TRUE;
        m_nVisualPos = p.x;
        SetCapture();
        return;
    }
    m_nVisualPos = m_nLogicalPos;
    m_nDragTarget = HitTest(p);
    if (m_nDragTarget >= 10 && m_nDragTarget < 10 + kCueMax) {
        // キュークリック → 親が GetCueClick でジャンプ
        m_nCueClick = m_nDragTarget - 10;
        m_nDragTarget = 0;
        GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, 0), (LPARAM)m_hWnd);
        return;
    }
    if (m_nDragTarget >= 30 && m_nDragTarget < 30 + kLrcMarkMax) {
        m_nLrcClick = m_nDragTarget - 30;
        m_nDragTarget = 0;
        GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, 0), (LPARAM)m_hWnd);
        return;
    }
    // ロック中に loop つまみへ当たってもシークへ落とす（旧: return でシーク不能）
    if ((m_nDragTarget == 1 || m_nDragTarget == 2) && m_bSelLocked)
        m_nDragTarget = 0;
    if (m_nDragTarget == 0)
    {
        // トラック空白＝シーク（つまみ上ではここに来ない＝相打ち回避）
        m_nVisualPos = PixelToValue(p.x);
        m_nDragTarget = 3;
        CSliderCtrl::SetPos(m_nVisualPos);
    }
    m_bDragging = TRUE;
    SetCapture();
    RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}
// シーク確定は SB_THUMBPOSITION（ENDSCROLL と衝突しない）。
// loop/A-B/グリッドは TB_ENDTRACK。親は GetDragTarget で判別。
void CCustomRangeSliderCtrl::OnLButtonUp(UINT f, CPoint p)
{
    if (m_bDragging)
    {
        const int dragTarget = m_nDragTarget;
        m_bDragging = FALSE;
        ReleaseCapture();
        if (dragTarget == 3)
        {
            m_nLogicalPos = m_nVisualPos;
            CSliderCtrl::SetPos(m_nLogicalPos);
            // SB_ENDSCROLL==TB_ENDTRACK のため、シーク確定は THUMBPOSITION のみ送る
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, m_nLogicalPos), (LPARAM)m_hWnd);
        }
        else if (dragTarget == 1 || dragTarget == 2 || dragTarget == 4 || dragTarget == 5)
        {
            // loop / A-B つまみ確定。親が GetSelection / GetAB + GetDragTarget で判別。
            // SB_THUMBPOSITION は位置シークと衝突しないよう LOWORD に TB_ENDTRACK を使う。
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, 0), (LPARAM)m_hWnd);
        }
        else if (dragTarget == 99)
        {
            // グリッド位相確定 → 親へ通知（TB_ENDTRACK）
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, 0), (LPARAM)m_hWnd);
        }
#if CCUSTOM_AERO_SUPPORT
        CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
    }
}
// 非ドラッグ: 時刻チップとホバー拡大 X。ドラッグ: target ごとに値更新＋ THUMBTRACK。
// ハートは m_bHoverTracking で Soft 化（DrawRangeSlider）。
void CCustomRangeSliderCtrl::OnMouseMove(UINT f, CPoint p)
{
    if (!m_bHoverTracking) {
        TRACKMOUSEEVENT tme = { sizeof(tme) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hWnd;
        ::TrackMouseEvent(&tme);
        m_bHoverTracking = TRUE;
    }
    if (!m_bDragging)
        UpdateHoverTip(p);
    if (m_bHoverZoom && !m_bDragging && m_wavePeakCount > 0) {
        if (m_hoverZoomX != p.x) {
            m_hoverZoomX = p.x;
            Invalidate(FALSE);
        }
    }
    if (m_bDragging)
    {
#if CCUSTOM_AERO_SUPPORT
        CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
        int v = PixelToValue(p.x);
        if (m_nDragTarget == 3)
        {
            m_nVisualPos = max(m_nMin, min(m_nMax, v));
            CSliderCtrl::SetPos(m_nVisualPos);
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, m_nVisualPos), (LPARAM)m_hWnd);
        }
        else if (m_nDragTarget == 1)
            m_nSelMin = min(v, m_nSelMax);
        else if (m_nDragTarget == 2)
            m_nSelMax = max(v, m_nSelMin);
        else if (m_nDragTarget == 4) {
            m_nAbA = max(m_nMin, min(m_nMax, v));
            if (m_nAbB >= 0 && m_nAbA >= m_nAbB)
                m_nAbA = max(m_nMin, m_nAbB - 1);
        }
        else if (m_nDragTarget == 5) {
            m_nAbB = max(m_nMin, min(m_nMax, v));
            if (m_nAbA >= 0 && m_nAbB <= m_nAbA)
                m_nAbB = min(m_nMax, m_nAbA + 1);
        }
        else if (m_nDragTarget == 99) {
            // ピクセル差分 → ms（1px ≒ 数ms）。相対ドラッグ
            const int dx = p.x - m_nVisualPos;
            m_nVisualPos = p.x;
            if (m_timeBaseHz > 0 && dx != 0) {
                const int span = max(1, m_nMax - m_nMin);
                CRect rc; GetClientRect(&rc);
                const int tw = max(1, rc.Width() - 28);
                const double framesPerPx = (double)span / (double)tw;
                const int dMs = (int)((dx * framesPerPx * 1000.0) / (double)m_timeBaseHz);
                m_beatOffsetMs += dMs;
                if (m_beatOffsetMs < -60000) m_beatOffsetMs = -60000;
                if (m_beatOffsetMs > 60000) m_beatOffsetMs = 60000;
            }
        }
        if (m_nDragTarget == 1 || m_nDragTarget == 2 || m_nDragTarget == 4 || m_nDragTarget == 5)
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, 0), (LPARAM)m_hWnd);
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
}

// 拡大レンズとチップを閉じ、通常ハートへ戻す。
void CCustomRangeSliderCtrl::OnMouseLeave()
{
    m_bHoverTracking = FALSE;
    m_hoverZoomX = -1;
    if (m_hoverTip.GetSafeHwnd())
        m_hoverTip.SendMessage(TTM_POP, 0, 0);
    Invalidate(FALSE);
}

// A-B / ロック解除 loop はサイズ左右、キューはハンド。それ以外は既定。
BOOL CCustomRangeSliderCtrl::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    if (nHitTest == HTCLIENT && GetSafeHwnd()) {
        CPoint pt;
        ::GetCursorPos(&pt);
        ScreenToClient(&pt);
        const int ht = HitTest(pt);
        if (ht == 4 || ht == 5 || ((ht == 1 || ht == 2) && !m_bSelLocked) || (ht >= 10 && ht < 10 + kCueMax)) {
            ::SetCursor(::LoadCursor(NULL, (ht >= 10) ? IDC_HAND : IDC_SIZEWE));
            return TRUE;
        }
    }
    return CSliderCtrl::OnSetCursor(pWnd, nHitTest, message);
}

// 親へクライアント座標変換して転送（シーク上コンテキストメニュー）。
void CCustomRangeSliderCtrl::OnRButtonUp(UINT nFlags, CPoint point)
{
    // 親ダイアログへクライアント座標を変換して渡す（シーク上のコンテキストメニュー拡充用）
    CWnd* pParent = GetParent();
    if (pParent && pParent->GetSafeHwnd()) {
        CPoint sp = point;
        ClientToScreen(&sp);
        pParent->ScreenToClient(&sp);
        pParent->SendMessage(WM_RBUTTONUP, (WPARAM)nFlags, MAKELPARAM(sp.x, sp.y));
        return;
    }
    CSliderCtrl::OnRButtonUp(nFlags, point);
}

// ============================================================================
// カスタムリストコントロール
// CCustomListCtrl
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomListCtrl, CListCtrlA)

BEGIN_MESSAGE_MAP(CCustomListCtrl, CListCtrlA)
    ON_WM_CTLCOLOR_REFLECT()
    ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnCustomDraw)
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_VSCROLL()
    ON_WM_HSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_WINDOWPOSCHANGED()
    ON_WM_TIMER()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
    ON_MESSAGE(CCC_WM_POST_OPAQUE_PAINT, OnPostOpaquePaint)
    ON_WM_DROPFILES()
END_MESSAGE_MAP()

static const UINT_PTR kListScrollOpaqueTimerId = 4108;

// リスト上にドロップされたファイルを親ダイアログへ転送する。
// (プレイリストではリストがダイアログの大部分を覆っているため、ダイアログだけが
//  WS_EX_ACCEPTFILES を持っていてもリスト上へのドロップは届かない。DragAcceptFiles で
//  リスト自身を登録し、ここで受け取って親の OnDropFiles へ中継する。)
void CCustomListCtrl::OnDropFiles(HDROP hDropInfo)
{
    CWnd* pParent = GetParent();
    if (pParent && pParent->GetSafeHwnd()) {
        pParent->SendMessage(WM_DROPFILES, (WPARAM)hDropInfo, 0);
        return;   // DragFinish は転送先(親の OnDropFiles)側で完結させる
    }
    CListCtrlA::OnDropFiles(hDropInfo);
}

// カスタムドローリスト。ホバー行と選択行の♡矩形だけタイマー再描画する。
// リスト全体 Invalidate は再生中ピアノ提示を遅らせるので使わない。
CCustomListCtrl::CCustomListCtrl()
    : m_bAutoDelete(FALSE), m_nHotItem(-1), m_bAeroMode(FALSE)
{
    m_brBackground.CreateSolidBrush(COLOR_LIST_BG);
    m_heartRcSel.SetRectEmpty();
    m_heartRcHot.SetRectEmpty();
}

// 背景ブラシのみ。ハートタイマは HWND 破棄で停止。
CCustomListCtrl::~CCustomListCtrl()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

// EnableAutoDelete 時のみ delete this。
void CCustomListCtrl::PostNcDestroy()
{
    CListCtrlA::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}
// CLIPCHILDREN + DOUBLEBUFFER。親 ACCEPTFILES だけではリスト上が受け取れないので DragAcceptFiles。
void CCustomListCtrl::PreSubclassWindow()
{
    CListCtrlA::PreSubclassWindow();
    ModifyStyle(0, WS_CLIPCHILDREN);
    SetBkColor(COLOR_LIST_BG);
    SetTextBkColor(COLOR_LIST_BG);
    SetTextColor(RGB(0, 0, 0));
    SetExtendedStyle(GetExtendedStyle() | LVS_EX_DOUBLEBUFFER);
    // 親の WS_EX_ACCEPTFILES だけではリスト上にドロップが届かない
    DragAcceptFiles(TRUE);
}

// Ctrl+A 等のキー処理後、アクリル下では不透明再描画を予約（選択が一気に変わる）。
BOOL CCustomListCtrl::PreTranslateMessage(MSG* pMsg)
{
    const BOOL handled = CListCtrlA::PreTranslateMessage(pMsg);
    if (handled && pMsg && pMsg->hwnd == m_hWnd && pMsg->message == WM_KEYDOWN
        && (pMsg->wParam == 'A' || pMsg->wParam == 'a'))
        ScheduleOpaqueRepaint();
    return handled;
}
// リスト本体色。セルは OnCustomDraw が上書きする。
HBRUSH CCustomListCtrl::CtlColor(CDC* pDC, UINT)
{
    pDC->SetBkColor(COLOR_LIST_BG);
    pDC->SetTextColor(RGB(0, 0, 0));
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

// SubItemHitTest でホバー行更新。LEAVE を張り、♡タイマは CustomDraw が必要なら開始。
void CCustomListCtrl::OnMouseMove(UINT f, CPoint p)
{
    LVHITTESTINFO h;
    h.pt = p;
    UpdateHotItem(SubItemHitTest(&h));

    TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, m_hWnd, 0 };
    TrackMouseEvent(&t);
    CListCtrl::OnMouseMove(f, p);
}

// ホバー解除。Opaque ホストでは UpdateHotItem が POST_OPAQUE_PAINT する。
void CCustomListCtrl::OnMouseLeave()
{
    UpdateHotItem(-1);
    CListCtrl::OnMouseLeave();
}
// CCC_WM_POST_OPAQUE_PAINT を Post。Send 同期再入禁止。キューに1回まとまる。
void CCustomListCtrl::ScheduleOpaqueRepaint()
{
    if (GetSafeHwnd())
        PostMessage(CCC_WM_POST_OPAQUE_PAINT);
}

// ガラス親のときだけ PaintOpaqueClient。未処理なら無視（メッセージは無害）。
// ホバー行変更・サイズ変更・Ctrl+A から来る。
LRESULT CCustomListCtrl::OnPostOpaquePaint(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd))
    {
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
#endif
    return 0;
}

// OpaqueFixer が直後に全面描画する。ここでの Invalidate は名前列ちらつきの元。ホット行だけ捨てる。
void CCustomListCtrl::OnVScroll(UINT n, UINT p, CScrollBar* s)
{
    CListCtrl::OnVScroll(n, p, s);
    // OpaqueFixer が直後に全面描画する。ここでの Invalidate は名前列ちらつきの元。
    m_nHotItem = -1;
}
// 横スクロール後もホット索引だけ破棄（部分 Invalidate しない）。
void CCustomListCtrl::OnHScroll(UINT n, UINT p, CScrollBar* s)
{
    CListCtrl::OnHScroll(n, p, s);
    m_nHotItem = -1;
}
// ホット行の再 Invalidate 禁止。索引だけ合わせて直後の Opaque に任せる。
BOOL CCustomListCtrl::OnMouseWheel(UINT n, short z, CPoint p)
{
    BOOL r = CListCtrl::OnMouseWheel(n, z, p);
    // ホット行の再 Invalidate 禁止(名前列ちらつき)。索引だけ合わせて直後の Opaque に任せる。
    CPoint pt;
    if (GetCursorPos(&pt)) {
        ScreenToClient(&pt);
        LVHITTESTINFO h;
        h.pt = pt;
        m_nHotItem = SubItemHitTest(&h);
    }
    return r;
}

// kListSoftTimerId: 選択/ホバー♡の矩形だけ Invalidate（30ms）。両方空なら Kill。
// kListScrollOpaqueTimerId: スクロール後の遅延不透明塗り。
void CCustomListCtrl::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kListSoftTimerId)
    {
        if (m_nHotItem < 0)
            m_heartRcHot.SetRectEmpty();
        if (GetSelectedCount() <= 0)
            m_heartRcSel.SetRectEmpty();
        if (m_heartRcSel.IsRectEmpty() && m_heartRcHot.IsRectEmpty()) {
            KillTimer(kListSoftTimerId);
            return;
        }
        if (!m_heartRcSel.IsRectEmpty())
            InvalidateRect(&m_heartRcSel, FALSE);
        if (!m_heartRcHot.IsRectEmpty())
            InvalidateRect(&m_heartRcHot, FALSE);
        return;
    }
    if (nIDEvent == kListScrollOpaqueTimerId)
    {
        KillTimer(kListScrollOpaqueTimerId);
#if CCUSTOM_AERO_SUPPORT
        if (CCC_IsAeroEnabled() && CCC_IsWin11())
        {
            CClientDC dc(this);
            PaintOpaqueClient(dc);
        }
#endif
        return;
    }
    CListCtrl::OnTimer(nIDEvent);
}

// サイズ変化でアクリル穴が残ることがあるので遅延 Opaque。
void CCustomListCtrl::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
    CListCtrl::OnWindowPosChanged(lpwndpos);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
        ScheduleOpaqueRepaint();
#endif
}

// ホバー行切替。古い♡矩形は捨てる（前の行を回し続けない）。
// ガラス親は部分 Invalidate が α=0 穴になるので POST_OPAQUE_PAINT のみ。
void CCustomListCtrl::UpdateHotItem(int n)
{
    if (m_nHotItem == n) return;
    const int o = m_nHotItem;
    m_nHotItem = n;
    /* 前の行の♡はもう無い。古い矩形を回し続けない */
    m_heartRcHot.SetRectEmpty();
    // アクリル/キャプションガラス: 部分 Invalidate の素塗りは α=0 穴→ホバーで透過。
    // 連続ホバー行変更は Post でキューに載せ、OpaqueFixer が全面不透明再描画する。
    // 旧: ジャケ/♪リストはここで return し、♪点滅(SIconTimer)まで見た目が止まっていた。
    if (CCC_HostNeedsChildOpaque(m_hWnd)) {
        PostMessage(CCC_WM_POST_OPAQUE_PAINT);
        return;
    }
    if (o >= 0) {
        CRect rr;
        if (GetItemRect(o, &rr, LVIR_BOUNDS))
            RedrawWindow(&rr, NULL, RDW_INVALIDATE | RDW_NOERASE);
        else
            RedrawItems(o, o);
    }
    if (m_nHotItem >= 0) {
        CRect rr;
        if (GetItemRect(m_nHotItem, &rr, LVIR_BOUNDS))
            RedrawWindow(&rr, NULL, RDW_INVALIDATE | RDW_NOERASE);
        else
            RedrawItems(m_nHotItem, m_nHotItem);
    }
}

// カーソル位置からホット行を取り直す（外部から呼ぶ用）。
void CCustomListCtrl::UpdateHotItemFromCursor()
{
    if (!GetSafeHwnd()) return;
    CPoint pt;
    if (!GetCursorPos(&pt)) return;
    ScreenToClient(&pt);
    LVHITTESTINFO h;
    h.pt = pt;
    UpdateHotItem(SubItemHitTest(&h));
}

// 可視範囲 RedrawItems。ガラス親では使わず Opaque 全面を優先すること。
void CCustomListCtrl::RedrawVisibleItems()
{
    int t = GetTopIndex();
    int b = t + GetCountPerPage();
    int c = GetItemCount();
    if (b >= c) b = c - 1;
    if (t >= 0 && b >= t) RedrawItems(t, b);
}

// FALSE=既定消去させない。空きは FillEmptyBelowVisible が交互色で塗る。
// TRUE+Fill だと PREPAINT と二重になり黒ちらつきする。
BOOL CCustomListCtrl::OnEraseBkgnd(CDC*)
{
    return FALSE;
}

// 可視最終行より下（と PREPAINT 時は行下地）をゼブラ不透明で塗る。
// belowItemsOnly=TRUE（描画後）: 行の上に塗ると文字/ジャケが消えるので空きだけ。
// FALSE: 行下地ごと（ITEMPREPAINT が同色で上書き）。OnPaint では TRUE のみ使う。
// 素 FillRect はアクリルで α=0 黒。32bpp DIB α=255 を AlphaBlend。ネスト BP は黒フラッシュ。
void CCustomListCtrl::FillEmptyBelowVisible(HDC hdc, BOOL belowItemsOnly)
{
    if (!hdc || !m_hWnd) return;
    CRect rcClient;
    GetClientRect(&rcClient);
    if (rcClient.Width() <= 0 || rcClient.Height() <= 0) return;

    // OnCustomDraw の行色と一致
    const COLORREF alt0 = COLOR_LIST_BG;
    const COLORREF alt1 = RGB(183, 221, 238);

    int hdrBottom = rcClient.top;
    if (CHeaderCtrl* pHdr = GetHeaderCtrl()) {
        CRect rh;
        pHdr->GetWindowRect(&rh);
        ScreenToClient(&rh);
        if (rh.bottom > hdrBottom) hdrBottom = rh.bottom;
    }

    const int n = GetItemCount();
    int topIdx = (n > 0) ? GetTopIndex() : 0;
    if (topIdx < 0) topIdx = 0;

    int rowH = 0;
    int yBand0 = hdrBottom;
    int idx0 = topIdx;
    if (n > 0 && topIdx < n) {
        CRect rc0;
        if (GetItemRect(topIdx, &rc0, LVIR_BOUNDS) && rc0.Height() > 0) {
            rowH = rc0.Height();
            yBand0 = rc0.top;
            idx0 = topIdx;
        }
    }
    if (rowH <= 0) {
        const int cpp = GetCountPerPage();
        const int avail = rcClient.bottom - hdrBottom;
        rowH = (cpp > 0 && avail > 0) ? (avail / cpp) : 18;
        if (rowH <= 0) rowH = 18;
        yBand0 = hdrBottom;
        idx0 = topIdx;
    }

    int fillTop = hdrBottom;
    int fillIdx = idx0;
    if (belowItemsOnly) {
        // 描画後: 行の上に塗ると文字/ジャケが消えるので空きだけ
        int bottomY = hdrBottom;
        if (n > 0) {
            int scanEnd = topIdx + GetCountPerPage() + 2;
            if (scanEnd > n) scanEnd = n;
            CRect rcItem;
            for (int i = topIdx; i < scanEnd; ++i) {
                if (!GetItemRect(i, &rcItem, LVIR_BOUNDS)) continue;
                if (rcItem.bottom > bottomY) bottomY = rcItem.bottom;
            }
            UINT ht = 0;
            const int hit = HitTest(CPoint(rcClient.left + 8, rcClient.bottom - 4), &ht);
            if (hit >= 0 && hit < n) {
                CRect rh;
                if (GetItemRect(hit, &rh, LVIR_BOUNDS) && rh.bottom > bottomY)
                    bottomY = rh.bottom;
            }
            else if (hit < 0 && bottomY <= hdrBottom)
                bottomY = hdrBottom;
        }
        if (bottomY >= rcClient.bottom) return;
        if (rowH > 0 && yBand0 <= bottomY) {
            const int steps = (bottomY - yBand0) / rowH;
            fillIdx = idx0 + steps;
            fillTop = yBand0 + steps * rowH;
            if (fillTop < bottomY)
                fillTop = bottomY;
        }
        else {
            fillTop = bottomY;
            fillIdx = n;
        }
    }
    else {
        // PREPAINT: 行下地ごとゼブラ(ITEMPREPAINT が同色で上書き)
        fillTop = hdrBottom;
        fillIdx = idx0;
        if (yBand0 <= hdrBottom)
            yBand0 = hdrBottom;
    }
    if (fillTop >= rcClient.bottom) return;

    // アクリル: 素 FillRect は α=0 で黒。BPPF_ERASE のネスト BP は黒フラッシュ。
    // 32bpp DIB(α=255)を1回 AlphaBlend(不透明塗り・フラッシュなし)。
    const int saved = ::SaveDC(hdc);
    ::SelectClipRgn(hdc, NULL);

    const int dibTop = belowItemsOnly ? fillTop : hdrBottom;
    int stripeY = fillTop;
    int stripeIdx = fillIdx;
    if (!belowItemsOnly) {
        stripeY = (yBand0 > hdrBottom) ? yBand0 : hdrBottom;
        stripeIdx = idx0;
    }
    const int fw = rcClient.right - rcClient.left;
    const int fh = rcClient.bottom - dibTop;
    if (fw <= 0 || fh <= 0) {
        if (saved) ::RestoreDC(hdc, saved);
        else ::SelectClipRgn(hdc, NULL);
        return;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = fw;
    bi.bmiHeader.biHeight = -fh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* pBits = nullptr;
    HBITMAP hDib = ::CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (hDib && pBits) {
        RGBQUAD* pq = static_cast<RGBQUAD*>(pBits);
        RGBQUAD c0 = { GetBValue(alt0), GetGValue(alt0), GetRValue(alt0), 255 };
        RGBQUAD c1 = { GetBValue(alt1), GetGValue(alt1), GetRValue(alt1), 255 };
        const int nPix = fw * fh;
        for (int i = 0; i < nPix; ++i) pq[i] = c0;
        for (int y = stripeY, idx = stripeIdx; y < rcClient.bottom; y += rowH, ++idx) {
            int rowStart = y - dibTop;
            int rowEnd = rowStart + rowH;
            if (rowStart < 0) rowStart = 0;
            if (rowEnd > fh) rowEnd = fh;
            if (rowStart >= rowEnd) continue;
            const RGBQUAD c = (idx % 2 == 0) ? c0 : c1;
            for (int yy = rowStart; yy < rowEnd; ++yy) {
                RGBQUAD* row = pq + yy * fw;
                for (int x = 0; x < fw; ++x) row[x] = c;
            }
        }
        HDC hdcMem = ::CreateCompatibleDC(hdc);
        if (hdcMem) {
            HGDIOBJ old = ::SelectObject(hdcMem, hDib);
            const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            ::GdiAlphaBlend(hdc, rcClient.left, dibTop, fw, fh, hdcMem, 0, 0, fw, fh, bf);
            ::SelectObject(hdcMem, old);
            ::DeleteDC(hdcMem);
        }
        ::DeleteObject(hDib);
    }

    if (saved)
        ::RestoreDC(hdc, saved);
    else
        ::SelectClipRgn(hdc, NULL);
}

// Fixer バッファへ Fill + WM_PRINTCLIENT。PrintClient が空きを黒くしクリップを残す →
// クリップ解除して FillEmptyBelowVisible で交互色に塗り直す。
void CCustomListCtrl::PaintOpaqueIntoBuffer(HDC hdcBuf)
{
    if (!hdcBuf || !m_hWnd) return;
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0) return;
    {
        const int saved = ::SaveDC(hdcBuf);
        ::SelectClipRgn(hdcBuf, NULL);
        ::FillRect(hdcBuf, &r, (HBRUSH)m_brBackground.GetSafeHandle());
        if (saved) ::RestoreDC(hdcBuf, saved);
    }
    ::SendMessage(m_hWnd, WM_PRINTCLIENT, (WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
    // PrintClient が空きを黒くしクリップを残すことがある → 解除して交互色で塗り直す
    FillEmptyBelowVisible(hdcBuf);
}

// BufferedPaint 内で PrintClient。失敗時 Default + FillEmpty。
// OnPrintClient は DefWindowProc のみ（ここへ戻ると無限再入）。
void CCustomListCtrl::PaintOpaqueClient(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0) return;

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP)
    {
        Default();
        FillEmptyBelowVisible(dc.GetSafeHdc());
        return;
    }
    ::FillRect(hdcBuf, &r, (HBRUSH)m_brBackground.GetSafeHandle());
    ::SendMessage(m_hWnd, WM_PRINTCLIENT, (WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
    FillEmptyBelowVisible(hdcBuf);
    ::BufferedPaintMakeOpaque(hBP, &r);
    ::EndBufferedPaint(hBP, TRUE);
}

// Default（NM_CUSTOMDRAW）のあと空きを FillEmpty。CPaintDC を自前で取らない。
// WM_PRINTCLIENT は OnPrintClient。OnPaint から Print を呼ぶと二重描画。
// 横スクロールバーは出さない（最終列を右端まで伸ばしている）。
void CCustomListCtrl::OnPaint()
{
    Default(); // NM_CUSTOMDRAW。PrintClient は別経路（ここから送ると二重）
    CClientDC dc(this);
    FillEmptyBelowVisible(dc.GetSafeHdc());
    ShowScrollBar(SB_HORZ, FALSE);
}

// 既定に委譲して CustomDraw を走らせるだけ。FillEmpty は呼び出し側（Opaque/OnPaint）。
// ここで PaintOpaqueClient すると SendMessage(WM_PRINTCLIENT) と再入する。
LRESULT CCustomListCtrl::OnPrintClient(WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(WM_PRINTCLIENT, (WPARAM)wParam, (LPARAM)lParam);
}

// LVS_EX_CHECKBOXES 時、列0の状態イメージ領域へチェックボックスを自前描画する。
// OnCustomDraw が CDRF_SKIPDEFAULT で全描画を奪うため、既定のチェックボックスが
// 描かれずに消えてしまう問題への対処。
// 素の Rectangle/Pen はアクリル／REDIRECTIONBITMAP 上で α=0 になり透過して見えるので、
// 32bpp DIB に描いて α=255 固定で AlphaBlend する。
static void CCC_DrawListCheckBox(CDC* pDC, const CRect& rc, bool checked)
{
    if (!pDC || rc.Width() < 6 || rc.Height() < 6) return;
    const int w = rc.Width();
    const int h = rc.Height();
    HDC hdcDst = pDC->GetSafeHdc();
    if (!hdcDst) return;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = ::CreateDIBSection(hdcDst, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) {
        // 最低限の不透明白だけでも文字との区別は付く
        CCC_FillRectOpaqueBits(hdcDst, rc, RGB(255, 255, 255));
        return;
    }

    HDC mem = ::CreateCompatibleDC(hdcDst);
    HGDIOBJ oldBmp = ::SelectObject(mem, dib);
    RECT zr = { 0, 0, w, h };
    HBRUSH brFill = ::CreateSolidBrush(RGB(255, 255, 255));
    ::FillRect(mem, &zr, brFill);
    ::DeleteObject(brFill);

    HPEN penBorder = ::CreatePen(PS_SOLID, 1, RGB(70, 70, 78));
    HGDIOBJ oldPen = ::SelectObject(mem, penBorder);
    HGDIOBJ oldBr = ::SelectObject(mem, ::GetStockObject(NULL_BRUSH));
    ::Rectangle(mem, 0, 0, w, h);
    ::SelectObject(mem, oldBr);

    if (checked)
    {
        const int penW = (std::max)(2, w / 7);
        HPEN penChk = ::CreatePen(PS_SOLID, penW, RGB(0, 140, 40));
        ::SelectObject(mem, penChk);
        ::MoveToEx(mem, w * 22 / 100, h * 52 / 100, NULL);
        ::LineTo(mem, w * 42 / 100, h * 72 / 100);
        ::LineTo(mem, w * 80 / 100, h * 26 / 100);
        ::SelectObject(mem, penBorder);
        ::DeleteObject(penChk);
    }

    ::SelectObject(mem, oldPen);
    ::DeleteObject(penBorder);

    // 全画素 α=255（透過禁止）
    {
        DWORD* px = (DWORD*)bits;
        const int n = w * h;
        for (int i = 0; i < n; ++i)
            px[i] |= 0xFF000000u;
    }
    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (!::GdiAlphaBlend(hdcDst, rc.left, rc.top, w, h, mem, 0, 0, w, h, bf))
        CCC_FillRectOpaqueBits(hdcDst, rc, RGB(255, 255, 255));

    ::SelectObject(mem, oldBmp);
    ::DeleteDC(mem);
    ::DeleteObject(dib);
}

// SUBITEM でセル全面を自前描画し CDRF_SKIPDEFAULT。
// PREPAINT で FillEmpty しない（名前列と二重→黒ちらつき）。
// ガラス: OpaqueBits。Aero: 黒+α。通常: FillSolid。
// 選択/ホバー行は SoftJkHeart を♪の奥に描き、♡矩形だけ kListSoftTimerId で回す。
// チェックは CCC_DrawListCheckBox（素 Rectangle は α=0）。
void CCustomListCtrl::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVCUSTOMDRAW* p = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
    *pResult = CDRF_DODEFAULT;
    switch (p->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        // FillEmpty は行描画後のみ(OnPaint/PaintOpaque)。PREPAINT で塗ると
        // 名前列(既定IL→自前ジャケ/♪)と二重になり黒ちらつきの元になる。
        *pResult = CDRF_NOTIFYITEMDRAW;
        break;
    case CDDS_ITEMPREPAINT:
        *pResult = CDRF_NOTIFYSUBITEMDRAW;
        break;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
    {
        CDC* pDC = CDC::FromHandle(p->nmcd.hdc);
        int ni = (int)p->nmcd.dwItemSpec;
        int ns = p->iSubItem;
        CRect r;
        GetSubItemRect(ni, ns, LVIR_BOUNDS, r);

        if (ns == 0)
        {
            int cx0 = GetColumnWidth(0);
            if (cx0 > 0) r.right = (std::min)((int)r.right, (int)r.left + cx0);
        }
        CHeaderCtrl* pHdr = GetHeaderCtrl();
        int nCols = pHdr ? pHdr->GetItemCount() : 0;
        CRect rcC;
        GetClientRect(&rcC);
        // 最終列(アルバム/コメント)だけ右端まで伸ばし、後ろの余白をなくす
        if (nCols > 0 && ns == nCols - 1 && r.right < rcC.right) r.right = rcC.right;

        UINT uColFmt = DT_LEFT;
        if (pHdr)
        {
            HDITEM hi = {};
            hi.mask = HDI_FORMAT;
            if (pHdr->GetItem(ns, &hi))
            {
                if (hi.fmt & LVCFMT_RIGHT) uColFmt = DT_RIGHT;
                else if (hi.fmt & LVCFMT_CENTER) uColFmt = DT_CENTER;
            }
        }

        BOOL bS = (GetItemState(ni, LVIS_SELECTED) & LVIS_SELECTED);
        BOOL bH = (ni == m_nHotItem);
        // 再生行(♪ 0/2)は選択が外れてもツヤグラデを残す。ホバー再描画で消えないように
        // 名前列以外のセルでも同じ判定が要るので、ここで先に取る。
        int noteImg = 1;
        if (m_mpNoteIconGet)
            noteImg = m_mpNoteIconGet(m_mpJacketCtx, ni);
        const BOOL bPlay = (noteImg == 0 || noteImg == 2);
        COLORREF bg = bS ? COLOR_SEL_BG : (ni % 2 == 0 ? COLOR_LIST_BG : COLOR_LIST_ALT);
        if (bPlay && !bS)
            bg = RGB(214, 186, 232); // 再生行(非選択): 選択紫より少し明るい下地
        if (bH && !bS && !bPlay) bg = RGB(210, 228, 248);
        if (!bS && !bPlay && m_mpRowMissGet && m_mpRowMissGet(m_mpJacketCtx, ni))
            bg = RGB(255, 214, 214); // 欠損行: 薄い赤

        // アクリル/キャプションガラス下の素 FillRect は α=0→ホバー・選択で透過穴になる。
        // 全セルを不透明ビット塗り(名前列ジャケ有無を問わない)。
#if CCUSTOM_AERO_SUPPORT
        const BOOL bCapGlass = CCC_IsWin11() && (CCC_CaptionOnlyHostGlass(m_hWnd) || CCC_IsAeroEnabled());
#else
        const BOOL bCapGlass = FALSE;
#endif
        const BOOL bLvAero = m_bAeroMode && !CCC_IsBlurDialogChild(m_hWnd);
        const BOOL bOpaqueHost = bCapGlass || CCC_HostNeedsChildOpaque(m_hWnd);
        const BOOL bHi = (bS || bPlay || bH);
        if (bOpaqueHost)
        {
#if CCUSTOM_AERO_SUPPORT
            if (bHi)
                DrawGlossHighlight(pDC, r, 6, bg);
            else
                CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), r, bg);
#endif
        }
        else if (bLvAero)
        {
            pDC->FillSolidRect(&r, RGB(0, 0, 0));
            FillRectAlpha(pDC, r, bg, (bS || bPlay) ? 180 : bH ? 140 : AERO_ALPHA_SEMI);
        }
        else
            pDC->FillSolidRect(&r, bg);

        // 欠損ヒート: 名前列左に 4px ストライプ
        if (ns == 0 && !bS && !bPlay && m_mpRowMissGet && m_mpRowMissGet(m_mpJacketCtx, ni))
        {
            CRect rs(r.left, r.top, r.left + 4, r.bottom);
            if (CCC_HostNeedsChildOpaque(m_hWnd))
                CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), rs, RGB(220, 60, 60));
            else
                pDC->FillSolidRect(&rs, RGB(220, 60, 60));
        }

        if (bHi && !bLvAero && !bOpaqueHost)
            DrawGlossHighlight(pDC, r, 6);

        // プレイリスト系は m_mpNoteIconGet で実♪を取得(GetDispInfo の iImage は空のまま)。
        // テキスト左余白計算でも同じ値を使う。
        int checkPad = 0; // LVS_EX_CHECKBOXES 時、文字開始をチェック右へずらす
        if (ns == 0)
        {
            // LVS_EX_CHECKBOXES 指定リスト(kpi一覧)のみ、状態イメージ領域へ
            // チェックボックスを描画する。クリックのトグル判定はコントロール側が
            // 状態イメージ矩形で行うため、同じ左端位置へ描けば操作性も復活する。
            if (GetExtendedStyle() & LVS_EX_CHECKBOXES)
            {
                int cbSize = 16;
                if (CImageList* pStIL = GetImageList(LVSIL_STATE))
                {
                    int iw = 0, ih = 0;
                    if (ImageList_GetIconSize(pStIL->GetSafeHandle(), &iw, &ih) && iw > 0)
                        cbSize = iw;
                }
                const int rowH = (int)r.Height();
                if (cbSize > rowH - 2) cbSize = (std::max)(8, rowH - 2);
                CRect rcCb;
                rcCb.left = r.left + 2;
                rcCb.top = r.top + (rowH - cbSize) / 2;
                rcCb.right = rcCb.left + cbSize;
                rcCb.bottom = rcCb.top + cbSize;
                CCC_DrawListCheckBox(pDC, rcCb, GetCheck(ni) != FALSE);
                checkPad = (rcCb.right - r.left) + 4; // チェック右端 + 隙間
            }
            // 再生アイコン: ImageList と pc[].icon の対応は
            //   0=♪A(IDI_ICON1) / 1=空(IDI_ICON2・透明) / 2=♪B(IDI_ICON3)
            // SIconTimer は 0↔2 で点滅。1 は非再生行。0 をスキップすると片方の♪が消える。
            // ♡ は選択装飾だが ♪ を隠さないよう、♪ の下(奥)に先に描く。
            CRect ri;
            const BOOL hasIconRect = GetItemRect(ni, &ri, LVIR_ICON);
            if (!m_mpNoteIconGet) {
                LVITEM lvi = { 0 };
                lvi.mask = LVIF_IMAGE;
                lvi.iItem = ni;
                if (GetItem(&lvi))
                    noteImg = lvi.iImage;
            }
            CImageList* pIL = GetImageList(LVSIL_SMALL);
            // ジャケット(左) → ♪(その右)。♡ は ♪ の下(奥)に先に描く。
            int jacketRight = r.left;
            if (ns == 0 && m_mpJacketPx > 0 && m_mpJacketGet) {
                HBITMAP hb = m_mpJacketGet(m_mpJacketCtx, ni);
                const int jsz = m_mpJacketPx;
                const int rowHJak = (int)r.Height();
                CRect rj;
                rj.left = r.left + 2;
                rj.top = r.top + (rowHJak - jsz) / 2;
                if (rj.top < r.top + 1) rj.top = r.top + 1;
                rj.right = rj.left + jsz;
                rj.bottom = rj.top + jsz;
                // ジャケ無し: 追加塗りしない(行背景のまま)。板/偽グラデは違和感の元。
                if (hb) {
                    jacketRight = rj.right;
                    CDC src;
                    src.CreateCompatibleDC(pDC);
                    HGDIOBJ old = src.SelectObject(hb);
                    BITMAP bm; ZeroMemory(&bm, sizeof(bm));
                    ::GetObject(hb, sizeof(bm), &bm);
                    if (bm.bmWidth > 0 && bm.bmHeight > 0) {
                        // CreateCompatibleBitmap 由来のサムネは α=0 が多い。
                        // 素 StretchBlt だとアクリル上でホバー部分再描画時に透ける。
                        // α=255 付き不透明 Blit に統一する。
#if CCUSTOM_AERO_SUPPORT
                        if (CCC_IsWin11() && (CCC_IsAeroEnabled() || CCC_CaptionOnlyHostGlass(m_hWnd)
                            || CCC_HostNeedsChildOpaque(m_hWnd))) {
                            CCC_BlitStretchOpaque(pDC->GetSafeHdc(),
                                rj.left, rj.top, jsz, jsz,
                                src.GetSafeHdc(), 0, 0, bm.bmWidth, bm.bmHeight);
                        } else
#endif
                        {
                            pDC->SetStretchBltMode(COLORONCOLOR);
                            pDC->StretchBlt(rj.left, rj.top, jsz, jsz, &src,
                                0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                        }
                    }
                    src.SelectObject(old);
                    src.DeleteDC();
                }
                else {
                    jacketRight = rj.right;
                }
            }
            // ♪描画位置(ジャケ有無で切替)。♡ は従来どおり ♪ と同位置・奥に重ねる。
            const UINT dpiNote = CCC_GetControlDpi(m_hWnd);
            const int iw = CCC_ScaleDpi(16, dpiNote), ih = CCC_ScaleDpi(16, dpiNote);
            int noteX;
            int noteY = r.top + ((int)r.Height() - ih) / 2;
            if (m_mpJacketPx > 0)
                noteX = jacketRight + CCC_ScaleDpi(3, dpiNote);
            else if (hasIconRect)
                noteX = ri.left + (ri.Width() - iw) / 2;
            else
                noteX = r.left + CCC_ScaleDpi(2, dpiNote);
            if (bS || bH) {
                // Soft3D 回転♡（選択行 + ホバー行）。GDI ハートは載せない。
                CRect rh(noteX, noteY + CCC_ScaleDpi(2, dpiNote),
                    noteX + CCC_ScaleDpi(14, dpiNote), noteY + ih);
                DrawSoftJkHeart(pDC, rh, (int)(::GetTickCount64() / kListHeartStepMs),
                    TRUE, RGB(255, 140, 188));
                // リスト全体の周期 Invalidate は再生中のピアノ提示を遅らせる。
                // ♡ の矩形だけ回して、なめらかさと軽さを両立させる。
                if (bS) m_heartRcSel = rh; else m_heartRcHot = rh;
                if (GetSafeHwnd())
                    SetTimer(kListSoftTimerId, kListHeartTimerMs, NULL);
            }
            if (pIL && noteImg >= 0 && noteImg != 1) {
                // ImageList は行高確保(♪相当)。♪自体は 16x16@96dpi。
                HICON hNote = ImageList_GetIcon(pIL->GetSafeHandle(), noteImg, ILD_TRANSPARENT);
                if (hNote) {
                    ::DrawIconEx(pDC->GetSafeHdc(), noteX, noteY, hNote, iw, ih, 0, NULL, DI_NORMAL);
                    ::DestroyIcon(hNote);
                }
            }
            // ホバー印: DrawStar(ペン線)はアクリル上で α=0→黒線/透けになるので使わない。
            // 行背景の淡色(上で塗済)だけで十分。左端に不透明の細いアクセントのみ。
            if (bH && !bS) {
#if CCUSTOM_AERO_SUPPORT
                if (bCapGlass)
                    CCC_FillRectOpaqueBits(pDC->GetSafeHdc(),
                        CRect(r.left, r.top, r.left + CCC_ScaleDpi(3, dpiNote), r.bottom), RGB(90, 150, 220));
                else
#endif
                    pDC->FillSolidRect(r.left, r.top, CCC_ScaleDpi(3, dpiNote), r.Height(), RGB(90, 150, 220));
            }
        }

        CString st = GetItemText(ni, ns);
        BOOL bSav = FALSE, bLrc = FALSE;
        CString extra[kCccExtraChips];
        int extraN = 0;
        CCC_ExtractSavLrc(st, bSav, bLrc, extra, kCccExtraChips, extraN);
        const BOOL bOpaqueChips = bCapGlass;
        pDC->SetTextColor(bS ? COLOR_LIST_SEL_TEXT : (m_bAeroMode ? RGB(1, 1, 1) : RGB(0, 0, 0)));
        pDC->SetBkMode(TRANSPARENT);

        CRect rt = r;
        if (ns == 0)
        {
            const UINT dpiTxt = CCC_GetControlDpi(m_hWnd);
            // 名前列テキスト開始: ジャケ右 + ♪(16) + 隙間。旧 36/24@96 固定のまま ScaleDpi
            // するとジャケ縮小後も余白だけ残り横に間延びする。
            // kpi一覧(LVS_EX_CHECKBOXES)はチェック幅を先に確保して文字と重ねない。
            int tl = r.left + CCC_ScaleDpi(4, dpiTxt);
            if (checkPad > 0)
                tl = r.left + checkPad;
            if (m_mpJacketPx > 0)
                tl = r.left + checkPad + m_mpJacketPx + CCC_ScaleDpi(3, dpiTxt) + CCC_ScaleDpi(16, dpiTxt) + CCC_ScaleDpi(4, dpiTxt);
            CRect ri2;
            if (GetItemRect(ni, &ri2, LVIR_ICON))
            {
                if (noteImg >= 0 && noteImg != 1) {
                    if (m_mpJacketPx > 0)
                        tl = (std::max)(tl, (int)r.left + checkPad + m_mpJacketPx + CCC_ScaleDpi(3, dpiTxt) + CCC_ScaleDpi(16, dpiTxt) + CCC_ScaleDpi(4, dpiTxt));
                    else if (ri2.Width() > 0)
                        tl = (std::max)(tl, (int)(std::max)((int)ri2.right, (int)r.left + checkPad) + CCC_ScaleDpi(4, dpiTxt));
                }
            }
            // チェックありでアイコン矩形が無い／空♪のときも、チェック右を下回らない
            if (checkPad > 0)
                tl = (std::max)(tl, (int)r.left + checkPad);
            tl = (std::min)(tl, (int)r.right - CCC_ScaleDpi(4, dpiTxt));
            rt.left = (std::max)(tl, (int)r.left + CCC_ScaleDpi(4, dpiTxt));
            rt.DeflateRect(CCC_ScaleDpi(2, dpiTxt), 0);
        }
        else if (uColFmt == DT_RIGHT) {
            rt.DeflateRect(4, 0);
        }
        else if (uColFmt == DT_CENTER) {
            // 左右対称のみ。左だけ +6 すると中央寄せが右へ寄り、狭い★列が潰れて見える
            rt.DeflateRect(1, 0);
        }
        else {
            // 左寄せ: 左に少し余白
            rt.left += 6;
            rt.DeflateRect(2, 0);
        }

        CFont* po = pDC->SelectObject(GetFont());
        const int midY = (r.top + r.bottom) / 2;
        if (bSav || bLrc || extraN > 0) {
            if (st.IsEmpty() && (uColFmt & DT_CENTER)) {
                // 印列のみ: チップを中央寄せ
                const int chipsW = CCC_MeasureSavLrcChips(pDC, bSav, bLrc, extra, extraN);
                int x = r.left + (r.Width() - chipsW) / 2;
                if (x < r.left + 2) x = r.left + 2;
                CCC_DrawSavLrcChips(pDC, x, midY, bSav, bLrc, extra, extraN, bOpaqueChips);
            } else {
                // 名前列: チップ → 曲名
                int x = rt.left;
                x = CCC_DrawSavLrcChips(pDC, x, midY, bSav, bLrc, extra, extraN, bOpaqueChips);
                if (x + 2 < rt.right)
                    rt.left = x + 1;
                if (!st.IsEmpty() && rt.Width() > 4)
                    DrawListSubitemCellText(pDC, st, rt, DT_LEFT);
            }
        } else if (!st.IsEmpty()) {
            DrawListSubitemCellText(pDC, st, rt, uColFmt);
        }
        pDC->SelectObject(po);

        // 装飾線: アクリル上の素 Pen/Ellipse は α=0 で黒線・透けになる → 不透明1px塗りに置換
        if (nCols > 0 && ns == nCols - 1) {
#if CCUSTOM_AERO_SUPPORT
            if (bCapGlass)
                CCC_FillRectOpaqueBits(pDC->GetSafeHdc(),
                    CRect(r.left + 10, r.bottom - 1, r.right - 10, r.bottom), RGB(200, 180, 220));
            else
#endif
                DrawLaceLine(pDC, r.left + 10, r.bottom - 1, r.right - 10, r.bottom - 1, RGB(200, 180, 220));
        }
        if (GetExtendedStyle() & LVS_EX_GRIDLINES)
        {
#if CCUSTOM_AERO_SUPPORT
            if (bCapGlass)
                CCC_FillRectOpaqueBits(pDC->GetSafeHdc(),
                    CRect(r.left, r.bottom - 1, r.right, r.bottom), RGB(220, 220, 230));
            else
#endif
            {
                CPen pp(PS_SOLID, 1, RGB(220, 220, 230));
                CPen* po2 = pDC->SelectObject(&pp);
                pDC->MoveTo(r.left, r.bottom - 1);
                pDC->LineTo(r.right, r.bottom - 1);
                pDC->SelectObject(po2);
            }
        }
        *pResult = CDRF_SKIPDEFAULT;
        break;
    }
    }
}

// ============================================================================
// CCustomTreeCtrl (KotoriClient 移植 + アクリル不透明対応)
// ============================================================================
#ifndef TVS_EX_DOUBLEBUFFER
#define TVS_EX_DOUBLEBUFFER 0x00000004
#endif

IMPLEMENT_DYNAMIC(CCustomTreeCtrl, CTreeCtrl)

static const UINT_PTR kTreeScrollOpaqueTimerId = 4109;

BEGIN_MESSAGE_MAP(CCustomTreeCtrl, CTreeCtrl)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnCustomDraw)
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_VSCROLL()
	ON_WM_MOUSEWHEEL()
	ON_WM_WINDOWPOSCHANGED()
	ON_WM_TIMER()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
	ON_MESSAGE(CCC_WM_POST_OPAQUE_PAINT, OnPostOpaquePaint)
END_MESSAGE_MAP()

// フル行選択のオーナードローツリー。ホット項目は行全体 Invalidate。
// ゼブラは描画順 m_nItemDrawIndex（階層ではなく可視順）。
CCustomTreeCtrl::CCustomTreeCtrl()
	: m_bAutoDelete(FALSE), m_hHotItem(NULL), m_nItemDrawIndex(0)
	, m_clrBk(COLOR_LIST_BG), m_bAeroMode(FALSE)
{
	m_brBackground.CreateSolidBrush(COLOR_LIST_BG);
}

// 背景ブラシのみ。
CCustomTreeCtrl::~CCustomTreeCtrl()
{
	if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

// 内部ブラシと TreeView_SetBkColor を揃える。空きギャップ塗りもこの色。
COLORREF CCustomTreeCtrl::SetBkColor(COLORREF clr)
{
	COLORREF clrOld = m_clrBk;
	m_clrBk = clr;
	if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
	m_brBackground.CreateSolidBrush(m_clrBk);
	if (GetSafeHwnd())
		TreeView_SetBkColor(m_hWnd, clr);
	Invalidate(FALSE);
	return clrOld;
}

// EnableAutoDelete 時のみ delete this。
void CCustomTreeCtrl::PostNcDestroy()
{
	CTreeCtrl::PostNcDestroy();
	if (m_bAutoDelete) delete this;
}

// TVS_EX_FULLROWSELECT + DOUBLEBUFFER。ラベル以外の行クリックも項目扱い（HitTest と整合）。
// 標準ヒットはアイコン/ラベルだけなので、拡張無しだと行右端が NOWHERE になる。
void CCustomTreeCtrl::PreSubclassWindow()
{
	CTreeCtrl::PreSubclassWindow();
	SetBkColor(COLOR_LIST_BG);
	DWORD dwEx = (DWORD)SendMessage(TVM_GETEXTENDEDSTYLE, 0, 0);
	SendMessage(TVM_SETEXTENDEDSTYLE, TVS_EX_DOUBLEBUFFER | TVS_EX_FULLROWSELECT,
		dwEx | TVS_EX_DOUBLEBUFFER | TVS_EX_FULLROWSELECT);
}

// 消去しない。空きは OnPaint のギャップ塗り。TRUE+Fill は CustomDraw と競合する。
BOOL CCustomTreeCtrl::OnEraseBkgnd(CDC*) { return FALSE; }

// スクロール直後のチラつき回避。POST_OPAQUE_PAINT を Post（Send 再入禁止）。
void CCustomTreeCtrl::ScheduleOpaqueRepaint()
{
	if (GetSafeHwnd())
		PostMessage(CCC_WM_POST_OPAQUE_PAINT);
}

// ガラス親のとき PaintOpaqueClient。ホバー部分 Invalidate の穴を全面不透明で潰す。
LRESULT CCustomTreeCtrl::OnPostOpaquePaint(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
	if (CCC_HostNeedsChildOpaque(m_hWnd))
	{
		CClientDC dc(this);
		PaintOpaqueClient(dc);
	}
#endif
	return 0;
}

// DefWindowProc のみ。CustomDraw を走らせる。OpaqueClient へ戻さない（再入防止）。
LRESULT CCustomTreeCtrl::OnPrintClient(WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(WM_PRINTCLIENT, wParam, lParam);
}

// Fixer バッファへ背景 + WM_PRINTCLIENT。MakeOpaque は呼び出し側。
void CCustomTreeCtrl::PaintOpaqueIntoBuffer(HDC hdcBuf)
{
	if (!hdcBuf || !m_hWnd) return;
	CRect r;
	GetClientRect(&r);
	if (r.Width() <= 0 || r.Height() <= 0) return;
	::FillRect(hdcBuf, &r, (HBRUSH)m_brBackground.GetSafeHandle());
	::SendMessage(m_hWnd, WM_PRINTCLIENT, (WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
}

// BufferedPaint+MakeOpaque。失敗時 Default。クロマ blit は使わない。
void CCustomTreeCtrl::PaintOpaqueClient(CDC& dc)
{
	CRect r;
	GetClientRect(&r);
	if (r.Width() <= 0 || r.Height() <= 0) return;
	BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
	params.dwFlags = BPPF_ERASE;
	HDC hdcBuf = NULL;
	HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
	if (!hdcBuf || !hBP) { Default(); return; }
	::FillRect(hdcBuf, &r, (HBRUSH)m_brBackground.GetSafeHandle());
	::SendMessage(m_hWnd, WM_PRINTCLIENT, (WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
	::BufferedPaintMakeOpaque(hBP, &r);
	::EndBufferedPaint(hBP, TRUE);
}

// OpaqueFixer 未装着でもアクリル穴を避ける（遅延生成 Lib ツリー等）。
// CPaintDC は更新矩形クリップのため、フルクライアントは GetDC へ描く。
// 最終可視行より下は HitTest で行が無いときだけ塗る（追いついていない行を潰さない）。
// WM_PRINTCLIENT は Print 経路。
void CCustomTreeCtrl::OnPaint()
{
#if CCUSTOM_AERO_SUPPORT
	// OpaqueFixer 未装着時でもアクリル穴を避ける（遅延生成 Lib ツリー等）
	// CPaintDC は更新矩形クリップのため、フルクライアントは GetDC へ描く。
	if (CCC_HostNeedsChildOpaque(m_hWnd))
	{
		PAINTSTRUCT ps = {};
		::BeginPaint(m_hWnd, &ps);
		CClientDC dc(this);
		PaintOpaqueClient(dc);
		::EndPaint(m_hWnd, &ps);
		return;
	}
#endif
	if (GetCount() == 0)
	{
		CPaintDC dc(this);
		CRect rc;
		GetClientRect(&rc);
		dc.FillSolidRect(&rc, GetBkColor());
		return;
	}

	Default();

	// 最終可視行より下だけ塗る。GetNextVisibleItem が追いついていない直後は
	// 実在する行を塗り潰しやすいので、HitTest で行が無いか確認してから塗る。
	int maxBottom = 0;
	HTREEITEM hVis = GetFirstVisibleItem();
	while (hVis) {
		CRect rcItem;
		if (GetItemRect(hVis, &rcItem, FALSE) && rcItem.bottom > maxBottom)
			maxBottom = rcItem.bottom;
		hVis = GetNextVisibleItem(hVis);
	}
	CRect rcClient;
	GetClientRect(&rcClient);
	if (maxBottom > 0 && maxBottom < rcClient.bottom)
	{
		CRect rcGap = rcClient;
		rcGap.top = maxBottom;
		UINT htFlags = 0;
		CPoint probe(rcClient.left + 4, rcGap.top + 1);
		HTREEITEM hUnder = CTreeCtrl::HitTest(probe, &htFlags);
		if (!hUnder && rcGap.top < rcGap.bottom)
		{
			CClientDC dc(this);
			dc.FillSolidRect(&rcGap, GetBkColor());
		}
	}
}

// サイズ変化後のガラス穴対策。
void CCustomTreeCtrl::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	CTreeCtrl::OnWindowPosChanged(lpwndpos);
#if CCUSTOM_AERO_SUPPORT
	if (CCC_HostNeedsChildOpaque(m_hWnd))
		ScheduleOpaqueRepaint();
#endif
}

// ホイール後 33ms 遅延の不透明塗り。連続ホイールでタイマが上書きされる。
void CCustomTreeCtrl::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kTreeScrollOpaqueTimerId)
	{
		KillTimer(kTreeScrollOpaqueTimerId);
#if CCUSTOM_AERO_SUPPORT
		if (CCC_HostNeedsChildOpaque(m_hWnd))
		{
			CClientDC dc(this);
			PaintOpaqueClient(dc);
		}
#endif
		return;
	}
	CTreeCtrl::OnTimer(nIDEvent);
}

// ルート=0。インデントと接続線の X 計算用。
int CCustomTreeCtrl::GetItemLevel(HTREEITEM hItem) const
{
	int nLevel = 0;
	HTREEITEM hParent = GetParentItem(hItem);
	while (hParent != NULL)
	{
		nLevel++;
		hParent = GetParentItem(hParent);
	}
	return nLevel;
}

// ラベル幅ではなくクライアント全幅（フル行選択の見た目と一致）。
void CCustomTreeCtrl::InvalidateItemRow(HTREEITEM hItem)
{
	if (!hItem || !GetSafeHwnd()) return;
	CRect rc;
	if (GetItemRect(hItem, &rc, FALSE))
	{
		CRect rcClient;
		GetClientRect(&rcClient);
		rc.left = rcClient.left;
		rc.right = rcClient.right;
		InvalidateRect(&rc, FALSE);
	}
}

// 可視行の Y 帯だけでヒット。右端空白も ONITEM。ボタン矩形は見ない。
HTREEITEM CCustomTreeCtrl::HitTestRowAtPoint(CPoint pt, UINT* pFlags)
{
	CRect rcClient;
	GetClientRect(&rcClient);
	if (!rcClient.PtInRect(pt))
		return NULL;

	HTREEITEM hVis = GetFirstVisibleItem();
	while (hVis != NULL) {
		CRect rcRow;
		if (GetItemRect(hVis, &rcRow, FALSE)) {
			rcRow.left = rcClient.left;
			rcRow.right = rcClient.right;
			if (pt.y >= rcRow.top && pt.y < rcRow.bottom) {
				if (pFlags)
					*pFlags = TVHT_ONITEM | TVHT_ONITEMINDENT | TVHT_ONITEMLABEL;
				return hVis;
			}
		}
		hVis = GetNextVisibleItem(hVis);
	}
	return NULL;
}

// 自前 SelectItem 後に TVN_SELCHANGED を親へ。既定 LButtonDown を食うため必須。
void CCustomTreeCtrl::NotifySelChangedByMouse(HTREEITEM hNew, HTREEITEM hOld)
{
	NM_TREEVIEW nmtv = {};
	nmtv.hdr.hwndFrom = m_hWnd;
	nmtv.hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
	nmtv.hdr.code = TVN_SELCHANGED;
	nmtv.action = TVC_BYMOUSE;

	nmtv.itemNew.mask = TVIF_HANDLE | TVIF_STATE;
	nmtv.itemNew.hItem = hNew;
	nmtv.itemNew.stateMask = TVIS_SELECTED;
	nmtv.itemNew.state = GetItemState(hNew, TVIS_SELECTED);

	if (hOld) {
		nmtv.itemOld.mask = TVIF_HANDLE | TVIF_STATE;
		nmtv.itemOld.hItem = hOld;
		nmtv.itemOld.stateMask = TVIS_SELECTED;
		nmtv.itemOld.state = GetItemState(hOld, TVIS_SELECTED);
	}

	GetParent()->SendMessage(WM_NOTIFY, (WPARAM)nmtv.hdr.idFrom, (LPARAM)&nmtv);
}

// フル行ヒットと整合する TVN_BEGINDRAG。既定はラベル上だけで始まる。
void CCustomTreeCtrl::NotifyBeginDrag(HTREEITEM hItem, CPoint pt)
{
	if (!hItem || !GetParent()) return;
	NMTREEVIEW nmtv = {};
	nmtv.hdr.hwndFrom = m_hWnd;
	nmtv.hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
	nmtv.hdr.code = TVN_BEGINDRAG;
	nmtv.action = TVC_BYMOUSE;
	nmtv.itemNew.mask = TVIF_HANDLE | TVIF_STATE | TVIF_PARAM;
	nmtv.itemNew.hItem = hItem;
	nmtv.itemNew.lParam = GetItemData(hItem);
	nmtv.itemNew.stateMask = TVIS_SELECTED;
	nmtv.itemNew.state = GetItemState(hItem, TVIS_SELECTED);
	nmtv.ptDrag = pt;
	GetParent()->SendMessage(WM_NOTIFY, (WPARAM)nmtv.hdr.idFrom, (LPARAM)&nmtv);
}

// +/- ボタンとラベルは既定。それ以外の行（アイコン右〜右端）は HitTestRowAtPoint。
// FULLROWSELECT と OnLButtonDown の選択がこれで揃う。
HTREEITEM CCustomTreeCtrl::HitTest(CPoint pt, UINT* pFlags)
{
	UINT flags = 0;
	HTREEITEM hItem = CTreeCtrl::HitTest(pt, &flags);
	if (hItem != NULL && (flags & TVHT_ONITEMBUTTON)) {
		if (pFlags) *pFlags = flags;
		return hItem;
	}
	if (hItem != NULL && (flags & TVHT_ONITEMLABEL)) {
		if (pFlags) *pFlags = flags;
		return hItem;
	}

	HTREEITEM hRow = HitTestRowAtPoint(pt, pFlags);
	if (hRow != NULL)
		return hRow;

	if (hItem != NULL) {
		if (pFlags) *pFlags = flags;
		return hItem;
	}
	if (pFlags) *pFlags = TVHT_NOWHERE;
	return NULL;
}

// 展開ボタンは既定へ。行ヒットは SelectItem + 自前 SELCHANGED。DragDetect で BEGINDRAG。
void CCustomTreeCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	UINT uFlags = 0;
	HTREEITEM hItem = HitTest(point, &uFlags);

	if (hItem != NULL && (uFlags & TVHT_ONITEMBUTTON)) {
		CTreeCtrl::OnLButtonDown(nFlags, point);
		return;
	}

	if (hItem != NULL) {
		HTREEITEM hOld = GetSelectedItem();
		SelectItem(hItem);
		SetFocus();
		if (hItem != hOld)
			NotifySelChangedByMouse(hItem, hOld);
		// ドラッグ開始(親へ TVN_BEGINDRAG)
		if (::DragDetect(m_hWnd, point))
			NotifyBeginDrag(hItem, point);
		return;
	}

	CTreeCtrl::OnLButtonDown(nFlags, point);
}

// ホット行切替。ガラス親は部分 Invalidate+UpdateWindow が α=0 穴 → Post 全面不透明。
void CCustomTreeCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	UINT      uFlags = 0;
	HTREEITEM hItem = HitTest(point, &uFlags);

	if (m_hHotItem != hItem)
	{
		HTREEITEM hOld = m_hHotItem;
		m_hHotItem = hItem;
		// アクリル下の部分 Invalidate+UpdateWindow は α=0 穴になり得る → Post 全面不透明
		if (CCC_HostNeedsChildOpaque(m_hWnd))
			ScheduleOpaqueRepaint();
		else {
			InvalidateItemRow(hOld);
			InvalidateItemRow(m_hHotItem);
			UpdateWindow();
		}
	}

	TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
	TrackMouseEvent(&tme);

	CTreeCtrl::OnMouseMove(nFlags, point);
}

// ホット解除。ガラスは全面 Opaque、それ以外は行だけ。
void CCustomTreeCtrl::OnMouseLeave()
{
	if (m_hHotItem)
	{
		HTREEITEM hOld = m_hHotItem;
		m_hHotItem = NULL;
		if (CCC_HostNeedsChildOpaque(m_hWnd))
			ScheduleOpaqueRepaint();
		else {
			InvalidateItemRow(hOld);
			UpdateWindow();
		}
	}
}

// スクロール後にホットをカーソルへ追従。ガラスは Opaque 予約。
void CCustomTreeCtrl::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	CTreeCtrl::OnVScroll(nSBCode, nPos, pScrollBar);
	if (GetSafeHwnd())
	{
		CPoint pt;
		if (GetCursorPos(&pt)) { ScreenToClient(&pt); UINT f = 0; m_hHotItem = HitTest(pt, &f); }
	}
#if CCUSTOM_AERO_SUPPORT
	if (CCC_HostNeedsChildOpaque(m_hWnd))
		ScheduleOpaqueRepaint();
#endif
	Invalidate(FALSE);
}

// ホイール後ホット更新。ガラスは即 Post + 33ms タイマ（中間フレームの穴対策）。
BOOL CCustomTreeCtrl::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	BOOL r = CTreeCtrl::OnMouseWheel(nFlags, zDelta, pt);
	if (GetSafeHwnd())
	{
		CPoint ptC = pt;
		ScreenToClient(&ptC);
		UINT f = 0;
		m_hHotItem = HitTest(ptC, &f);
	}
#if CCUSTOM_AERO_SUPPORT
	if (CCC_HostNeedsChildOpaque(m_hWnd))
	{
		ScheduleOpaqueRepaint();
		SetTimer(kTreeScrollOpaqueTimerId, 33, NULL);
	}
#endif
	Invalidate(FALSE);
	return r;
}

// ITEMPREPAINT で行全体（クライアント幅）を塗って SKIPDEFAULT。
// 標準はラベル幅だけ選択色→右端が親アクリルの穴に見える。
// ガラスは OpaqueBits。選択行は左に SoftJkHeart。フォーカスは点線枠。
void CCustomTreeCtrl::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMTVCUSTOMDRAW* pTVCD = reinterpret_cast<NMTVCUSTOMDRAW*>(pNMHDR);
	*pResult = CDRF_DODEFAULT;

	switch (pTVCD->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		m_nItemDrawIndex = 0;
		*pResult = CDRF_NOTIFYITEMDRAW;
		break;

	case CDDS_ITEMPREPAINT:
	{
		CDC* pDC = CDC::FromHandle(pTVCD->nmcd.hdc);
		HTREEITEM hItem = (HTREEITEM)pTVCD->nmcd.dwItemSpec;
		if (!hItem) { *pResult = CDRF_DODEFAULT; break; }

		CRect rcRow;
		GetItemRect(hItem, &rcRow, FALSE); // FALSE=行全体。TRUE だとラベル幅だけになり右端が穴
		CRect rcClient;
		GetClientRect(&rcClient);
		rcRow.left = rcClient.left;
		rcRow.right = rcClient.right; // TVS_EX_FULLROWSELECT の見た目（標準選択色はラベル幅）

		BOOL bSel = (GetItemState(hItem, TVIS_SELECTED) & TVIS_SELECTED) != 0;
		BOOL bHot = (hItem == m_hHotItem);
		BOOL bFocused = (GetFocus() == this) && bSel;

		COLORREF clrBg;
		if (bSel)                           clrBg = COLOR_SEL_BG;
		else if (bHot)                      clrBg = RGB(255, 210, 230);
		else if (m_nItemDrawIndex % 2 == 0) clrBg = COLOR_LIST_BG;
		else                                clrBg = RGB(255, 236, 246);

#if CCUSTOM_AERO_SUPPORT
		if (CCC_HostNeedsChildOpaque(m_hWnd))
			CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), rcRow, clrBg);
		else
#endif
			pDC->FillSolidRect(&rcRow, clrBg);

		int nLevel = GetItemLevel(hItem);
		int nIndent = GetIndent();
		if (nIndent <= 0) nIndent = 19;
		int nConnX = rcClient.left + nIndent * nLevel + nIndent / 2;
		int nCenterY = rcRow.CenterPoint().y;
		BOOL bHasLines = (GetStyle() & TVS_HASLINES) != 0;

		if (bHasLines && nLevel > 0)
		{
			DrawLaceLine(pDC,
				rcClient.left + nIndent * nLevel - nIndent / 2, nCenterY,
				nConnX - 2, nCenterY,
				RGB(180, 150, 200));
			DrawFlower(pDC, nConnX - 2, nCenterY, 3, RGB(255, 200, 220));
		}

		BOOL bHasChild = ItemHasChildren(hItem) != FALSE;
		if (bHasChild)
		{
			BOOL bExpanded = (GetItemState(hItem, TVIS_EXPANDED) & TVIS_EXPANDED) != 0;
			int  bx = nConnX, by = nCenterY;
			int  btnR = 7;
			CRect rcBtn(bx - btnR, by - btnR, bx + btnR, by + btnR);

			CPen   penBtn(PS_SOLID, 1, RGB(200, 150, 200));
			CBrush brBtn(bExpanded ? RGB(255, 230, 240) : RGB(240, 230, 255));
			CPen* pOldPen = pDC->SelectObject(&penBtn);
			CBrush* pOldBr = pDC->SelectObject(&brBtn);
			pDC->RoundRect(&rcBtn, CPoint(4, 4));
			pDC->SelectObject(pOldPen);
			pDC->SelectObject(pOldBr);

			if (bExpanded)
			{
				DrawStar(pDC, bx, by, 4, RGB(255, 100, 150));
				DrawFlower(pDC, bx, by, 3, RGB(255, 200, 220));
				CPen penMinus(PS_SOLID, 2, RGB(180, 60, 130));
				CPen* pOP = pDC->SelectObject(&penMinus);
				pDC->MoveTo(bx - 3, by); pDC->LineTo(bx + 4, by);
				pDC->SelectObject(pOP);
			}
			else
			{
				DrawFlower(pDC, bx, by, 3, RGB(180, 130, 230));
				CRect rcDiamond(bx - 5, by - 5, bx + 5, by + 5);
				DrawDiamond(pDC, rcDiamond, RGB(200, 180, 255));
				CPen penPlus(PS_SOLID, 2, RGB(120, 60, 200));
				CPen* pOP = pDC->SelectObject(&penPlus);
				pDC->MoveTo(bx - 3, by); pDC->LineTo(bx + 4, by);
				pDC->MoveTo(bx, by - 3); pDC->LineTo(bx, by + 4);
				pDC->SelectObject(pOP);
			}
		}

		int nIconRight = nConnX + 14;
		CImageList* pImgList = GetImageList(TVSIL_NORMAL);
		if (pImgList)
		{
			TVITEM tvi = {};
			tvi.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE;
			tvi.hItem = hItem;
			GetItem(&tvi);
			int nImg = (bSel && tvi.iSelectedImage >= 0) ? tvi.iSelectedImage : tvi.iImage;
			int nIconLeft = nConnX + 12;
			nIconRight = nIconLeft + 18;
			CRect rcIcon(nIconLeft, rcRow.top + 1, nIconRight, rcRow.bottom - 1);
			DrawTransparentIcon(pDC, pImgList, nImg, rcIcon, RGB(255, 255, 255));
		}

		CString strText = GetItemText(hItem);
		CRect   rcText(nIconRight + 4, rcRow.top, rcRow.right - 22, rcRow.bottom);

		pDC->SetBkMode(TRANSPARENT);
		pDC->SetTextColor(bSel ? COLOR_LIST_SEL_TEXT : RGB(0, 0, 0));
		{
			CFont* pOldFont = pDC->SelectObject(GetFont());
			DrawFitControlText(pDC, rcText, strText, DT_LEFT | DT_VCENTER | DT_NOPREFIX, 0.50f);
			pDC->SelectObject(pOldFont);
		}

		if (bSel)
		{
			CRect rcH(rcRow.left + 2, nCenterY - 7, rcRow.left + 16, nCenterY + 7);
			DrawSoftJkHeart(pDC, rcH, (int)(::GetTickCount64() / 160), TRUE, COLOR_HEART);
			DrawStar(pDC, rcRow.right - 12, nCenterY, 3, RGB(255, 215, 0));
		}
		else if (bHot)
		{
			DrawStar(pDC, rcRow.right - 12, nCenterY, 2, RGB(255, 215, 0));
		}

		if (bFocused)
		{
			CPen  penFoc(PS_DOT, 1, RGB(138, 43, 226));
			CPen* pOldPen = pDC->SelectObject(&penFoc);
			pDC->SelectStockObject(NULL_BRUSH);
			pDC->Rectangle(&rcRow);
			pDC->SelectObject(pOldPen);
		}

		DrawLaceLine(pDC,
			rcRow.left + 10, rcRow.bottom - 1,
			rcRow.right - 10, rcRow.bottom - 1,
			RGB(200, 180, 220));

		m_nItemDrawIndex++;
		*pResult = CDRF_SKIPDEFAULT;
		break;
	}
	}
}

// ============================================================================
// CCustomTabCtrl (listing4 準拠オーナードロー + アクリル不透明)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomTabCtrl, CTabCtrl)

static const UINT_PTR kTabScrollOpaqueTimerId = 4110;

BEGIN_MESSAGE_MAP(CCustomTabCtrl, CTabCtrl)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_WINDOWPOSCHANGED()
	ON_WM_TIMER()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
	ON_MESSAGE(CCC_WM_POST_OPAQUE_PAINT, OnPostOpaquePaint)
	ON_NOTIFY_REFLECT_EX(TCN_SELCHANGE, OnSelChange)
END_MESSAGE_MAP()

// 等幅オーナードロータブ。縦置き TCS_VERTICAL / TCS_RIGHT 対応。
// 選択タブの Soft 揺れは常時タイマを張らず、描画時の TickCount で足す。
CCustomTabCtrl::CCustomTabCtrl()
	: m_bAutoDelete(FALSE), m_bAeroMode(FALSE), m_nHotItem(-1), m_bTracking(FALSE)
{
	m_brBackground.CreateSolidBrush(COLOR_DIALOG_BG);
}

// ブラシとタブフォントを解放。
CCustomTabCtrl::~CCustomTabCtrl()
{
	if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
	if (m_fontTab.GetSafeHandle()) m_fontTab.DeleteObject();
	if (m_fontTabSel.GetSafeHandle()) m_fontTabSel.DeleteObject();
}

// EnableAutoDelete 時のみ delete this。
void CCustomTabCtrl::PostNcDestroy()
{
	CTabCtrl::PostNcDestroy();
	if (m_bAutoDelete) delete this;
}

// TCS_VERTICAL。未作成 HWND は FALSE。
BOOL CCustomTabCtrl::IsVertical() const
{
	if (!::IsWindow(m_hWnd)) return FALSE;
	return (::GetWindowLong(m_hWnd, GWL_STYLE) & TCS_VERTICAL) ? TRUE : FALSE;
}

// TCS_RIGHT（縦のとき右側）。文字の escapement 2700/900 切替に使う。
BOOL CCustomTabCtrl::IsRightSide() const
{
	if (!::IsWindow(m_hWnd)) return FALSE;
	return (::GetWindowLong(m_hWnd, GWL_STYLE) & TCS_RIGHT) ? TRUE : FALSE;
}

// 親フォントから通常/太字。縦書きは DrawTabItem が escapement 付き一時フォントを作る。
void CCustomTabCtrl::RebuildFonts()
{
	LOGFONT lf = {};
	CFont* pF = GetFont();
	if (pF && pF->GetSafeHandle())
		pF->GetLogFont(&lf);
	else {
		NONCLIENTMETRICS ncm = {};
		ncm.cbSize = sizeof(ncm);
		if (::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
			lf = ncm.lfMessageFont;
		else
			_tcscpy_s(lf.lfFaceName, LF_FACESIZE, _T("MS UI Gothic"));
	}
	if (lf.lfHeight == 0) lf.lfHeight = -13;
	if (abs(lf.lfHeight) > 16) lf.lfHeight = (lf.lfHeight < 0) ? -14 : 14;
	lf.lfEscapement = 0;
	lf.lfOrientation = 0;
	lf.lfQuality = CLEARTYPE_QUALITY;

	if (m_fontTab.GetSafeHandle()) m_fontTab.DeleteObject();
	if (m_fontTabSel.GetSafeHandle()) m_fontTabSel.DeleteObject();

	LOGFONT lfNormal = lf;
	lfNormal.lfWeight = FW_NORMAL;
	m_fontTab.CreateFontIndirect(&lfNormal);

	LOGFONT lfBold = lf;
	lfBold.lfWeight = FW_BOLD;
	m_fontTabSel.CreateFontIndirect(&lfBold);
}

// TCS_FIXEDWIDTH で等分。縦は高さをスロット分割、横は幅。DPI 下限あり。
void CCustomTabCtrl::LayoutEqualTabs(int nSlots)
{
	if (!::IsWindow(m_hWnd) || nSlots < 1) return;
	ModifyStyle(TCS_MULTILINE, TCS_FIXEDWIDTH);

	CRect rc;
	GetClientRect(&rc);
	if (rc.Height() < 8 || rc.Width() < 8) return;

	if (IsVertical()) {
		const UINT dpi = CCC_GetControlDpi(m_hWnd);
		const int usable = max(CCC_ScaleDpi(24, dpi), rc.Height() - CCC_ScaleDpi(6, dpi));
		const int tabAlong = max(CCC_ScaleDpi(20, dpi), usable / nSlots);
		SetItemSize(CSize(tabAlong, CCC_ScaleDpi(40, dpi)));
	}
	else {
		const UINT dpi = CCC_GetControlDpi(m_hWnd);
		const int usable = max(CCC_ScaleDpi(48, dpi), rc.Width() - CCC_ScaleDpi(6, dpi));
		const int tabW = max(CCC_ScaleDpi(48, dpi), usable / nSlots);
		SetItemSize(CSize(tabW, CCC_ScaleDpi(26, dpi)));
	}
	Invalidate(FALSE);
}

// テーマ無効 + OWNERDRAWFIXED を外して FIXEDWIDTH（自前 DrawToDC）。
// オーナードローメッセージは使わず OnPaint で描く。
void CCustomTabCtrl::PreSubclassWindow()
{
	CTabCtrl::PreSubclassWindow();
	HMODULE h = LoadLibrary(_T("UxTheme.dll"));
	if (h) {
		typedef HRESULT(WINAPI* S)(HWND, LPCWSTR, LPCWSTR);
		S p = (S)GetProcAddress(h, "SetWindowTheme");
		if (p) p(m_hWnd, L"", L"");
		FreeLibrary(h);
	}
	ModifyStyle(TCS_MULTILINE | TCS_OWNERDRAWFIXED, TCS_FIXEDWIDTH);
	RebuildFonts();
}

// Aero タブは消さない。SetAeroMode(FALSE) は不透明塗り（親アクリルでも穴を開けない）。
BOOL CCustomTabCtrl::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
	// SetAeroMode(FALSE) のタブは不透明。親アクリルでも穴を開けない
	if (m_bAeroMode && CCC_IsAeroEnabled() && CCC_IsWin11())
		return TRUE;
#endif
	if (pDC) {
		CRect r;
		GetClientRect(&r);
		pDC->FillSolidRect(&r, COLOR_DIALOG_BG);
	}
	return TRUE;
}

// 選択変更・サイズ変更の遅延不透明。POST_OPAQUE_PAINT。
void CCustomTabCtrl::ScheduleOpaqueRepaint()
{
	if (GetSafeHwnd())
		PostMessage(CCC_WM_POST_OPAQUE_PAINT);
}

// Win11 アクリル時 PaintOpaqueClient。
LRESULT CCustomTabCtrl::OnPostOpaquePaint(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled() && CCC_IsWin11()) {
		CClientDC dc(this);
		PaintOpaqueClient(dc);
	}
#endif
	return 0;
}

// DrawToDC 直呼び（不透明）。DefWindowProc だとシステムタブが上書きする。
// OnPaint のクロマ経路は使わない（印刷バッファに穴を作らない）。
LRESULT CCustomTabCtrl::OnPrintClient(WPARAM wParam, LPARAM lParam)
{
	// OnPaint と違い常に不透明 DrawToDC。CPaintDC / クロマ blit は使わない
	HDC hdc = (HDC)wParam;
	if (!hdc) return 0;
	CRect rc;
	GetClientRect(&rc);
	CDC dc;
	dc.Attach(hdc);
	DrawToDC(&dc, rc, FALSE);
	dc.Detach();
	return 0;
}

// AdjustRect 後のページ枠。見出し専用帯（高さほぼ無し）では描かない。
void CCustomTabCtrl::DrawPagePanel(CDC* pDC, const CRect& rcClient)
{
	CRect rcDisp = rcClient;
	AdjustRect(FALSE, &rcDisp);
	// 見出し専用帯（ページ領域がほぼ無い）では枠を描かない
	if (rcDisp.Height() < 12 || rcDisp.Width() < 12)
		return;
	CRect rcPanel = rcDisp;
	rcPanel.InflateRect(2, 2);
	CRect rcClip;
	rcClip.IntersectRect(&rcPanel, &rcClient);
	if (rcClip.IsRectEmpty()) return;
	DrawGradientBackground(pDC, rcClip, CCC_Lighten(COLOR_DIALOG_BG, 25), COLOR_DIALOG_BG, 0);
	CPen pen(PS_SOLID, 1, CCC_Darken(COLOR_DIALOG_BG, 30));
	CPen* pOldPen = pDC->SelectObject(&pen);
	CBrush* pOldBr = (CBrush*)pDC->SelectStockObject(NULL_BRUSH);
	pDC->RoundRect(&rcClip, CPoint(6, 6));
	pDC->SelectObject(pOldBr);
	pDC->SelectObject(pOldPen);
}

// 角丸グラデ。選択横タブは Soft 立体。常時 Soft タイマは張らない（UI スレッドを食う）。
// 縦は GM_ADVANCED + escapement で TextOut。横は DrawText 中央。
void CCustomTabCtrl::DrawTabItem(CDC* pDC, int nItem, CRect rc, BOOL bSelected, BOOL bHot)
{
	if (rc.Width() <= 2 || rc.Height() <= 2) return;
	if (bSelected) rc.InflateRect(1, 1);
	else rc.DeflateRect(1, 1);

	COLORREF clrTop, clrBottom, clrEdge, clrText;
	if (bSelected) {
		clrTop = CCC_Lighten(COLOR_BUTTON_BG, 45);
		clrBottom = COLOR_BUTTON_BG;
		clrEdge = CCC_Darken(COLOR_BUTTON_BG, 45);
		clrText = RGB(20, 60, 20);
	}
	else if (bHot) {
		clrTop = CCC_Lighten(COLOR_BUTTON_HOVER, 35);
		clrBottom = COLOR_BUTTON_HOVER;
		clrEdge = CCC_Darken(COLOR_BUTTON_HOVER, 35);
		clrText = RGB(25, 55, 25);
	}
	else {
		clrTop = RGB(255, 236, 244);
		clrBottom = RGB(255, 210, 228);
		clrEdge = RGB(220, 140, 170);
		clrText = RGB(40, 40, 40);
	}

	CRgn rgn;
	rgn.CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, 8, 8);
	pDC->SelectClipRgn(&rgn);
	DrawGradientBackground(pDC, rc, clrTop, clrBottom, IsVertical() ? 90 : 0);
	if (bSelected && !IsVertical()) {
		DrawGlossHighlight(pDC, rc, 6);
		if (rc.Width() >= 36 && rc.Height() >= 18)
			DrawSoftJkBackdrop(pDC, rc, (int)(::GetTickCount64() / 80), bHot);
		DrawSoftJkThumb(pDC, CRect(rc.right - 16, rc.top + 2, rc.right - 2, rc.top + 16),
			(int)(::GetTickCount64() / 220), bHot, bHot ? 10.f : 0.f);
		// 常時 Soft タイマーは UI スレッドを食うので張らない（描画は上で済む）
		if (GetSafeHwnd())
			KillTimer(kTabSoftTimerId);
	}
	pDC->SelectClipRgn(NULL);

	CPen pen(PS_SOLID, 1, clrEdge);
	CPen* pOldPen = pDC->SelectObject(&pen);
	CBrush* pOldBr = (CBrush*)pDC->SelectStockObject(NULL_BRUSH);
	pDC->RoundRect(&rc, CPoint(8, 8));
	pDC->SelectObject(pOldBr);
	pDC->SelectObject(pOldPen);

	TCHAR szText[256] = {};
	TCITEM ti = {};
	ti.mask = TCIF_TEXT;
	ti.pszText = szText;
	ti.cchTextMax = _countof(szText) - 1;
	if (!GetItem(nItem, &ti)) return;
	CString strText(szText);
	if (strText.IsEmpty()) return;

	const int nOldBk = pDC->SetBkMode(TRANSPARENT);
	const COLORREF clrOldText = pDC->SetTextColor(clrText);

	CFont* pBase = bSelected ? &m_fontTabSel : &m_fontTab;
	LOGFONT lfBase = {};
	if (pBase && pBase->GetSafeHandle())
		pBase->GetLogFont(&lfBase);
	else
		lfBase.lfHeight = -12;
	if (lfBase.lfHeight == 0) lfBase.lfHeight = -12;
	lfBase.lfEscapement = 0;
	lfBase.lfOrientation = 0;

	const BOOL bVert = IsVertical();
	const int escape = bVert ? (IsRightSide() ? 2700 : 900) : 0;
	CRect rcInner = rc;
	rcInner.DeflateRect(bVert ? 2 : 4, bVert ? 4 : 2);

	long target = abs(lfBase.lfHeight);
	if (target < 12) target = 12;
	const long MIN_H = 10;
	long maxH = 16;
	if (bVert) {
		maxH = 18;
		const long cross = (long)max(10, rcInner.Width() - 2);
		if (maxH > cross) maxH = cross;
	}
	if (target > maxH) target = maxH;

	CFont fontFit;
	SIZE sz = { 0, 0 };
	while (target >= MIN_H) {
		LOGFONT lf = lfBase;
		lf.lfHeight = -target;
		lf.lfEscapement = 0;
		lf.lfOrientation = 0;
		CFont fontTry;
		if (!fontTry.CreateFontIndirect(&lf)) break;
		CFont* pOld = pDC->SelectObject(&fontTry);
		::GetTextExtentPoint32(pDC->GetSafeHdc(), strText, strText.GetLength(), &sz);
		pDC->SelectObject(pOld);
		fontTry.DeleteObject();

		const int boxAlong = bVert ? rcInner.Height() : rcInner.Width();
		const int boxCross = bVert ? rcInner.Width() : rcInner.Height();
		if (sz.cx <= boxAlong && sz.cy <= boxCross) {
			lf.lfEscapement = escape;
			lf.lfOrientation = escape;
			fontFit.CreateFontIndirect(&lf);
			break;
		}
		target--;
	}
	if (!fontFit.GetSafeHandle()) {
		LOGFONT lf = lfBase;
		lf.lfHeight = -MIN_H;
		lf.lfEscapement = escape;
		lf.lfOrientation = escape;
		fontFit.CreateFontIndirect(&lf);
		LOGFONT lfM = lf;
		lfM.lfEscapement = 0;
		lfM.lfOrientation = 0;
		CFont fontM;
		fontM.CreateFontIndirect(&lfM);
		CFont* pOld = pDC->SelectObject(&fontM);
		::GetTextExtentPoint32(pDC->GetSafeHdc(), strText, strText.GetLength(), &sz);
		pDC->SelectObject(pOld);
	}

	CFont* pOldFont = pDC->SelectObject(&fontFit);
	const int oldMode = ::SetGraphicsMode(pDC->GetSafeHdc(), GM_ADVANCED);
	if (bVert) {
		int x, y;
		if (escape == 900) {
			x = rcInner.left + max(0, (rcInner.Width() - (int)sz.cy) / 2);
			y = rcInner.bottom - max(0, (rcInner.Height() - (int)sz.cx) / 2);
		}
		else {
			x = rcInner.right - max(0, (rcInner.Width() - (int)sz.cy) / 2);
			y = rcInner.top + max(0, (rcInner.Height() - (int)sz.cx) / 2);
		}
		pDC->TextOut(x, y, strText);
	}
	else {
		pDC->DrawText(strText, &rcInner, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	if (oldMode) ::SetGraphicsMode(pDC->GetSafeHdc(), oldMode);
	pDC->SelectObject(pOldFont);
	fontFit.DeleteObject();
	pDC->SetTextColor(clrOldText);
	pDC->SetBkMode(nOldBk);
}

// 背景（クロマ or ダイアログ色）→ページパネル→非選択タブ→選択タブ（前面）。
void CCustomTabCtrl::DrawToDC(CDC* pDC, const CRect& rcClient, BOOL bAeroChroma)
{
	if (!pDC || rcClient.IsRectEmpty()) return;
#if CCUSTOM_AERO_SUPPORT
	if (bAeroChroma)
		pDC->FillSolidRect(&rcClient, CCC_AERO_CHROMA_KEY);
	else
#endif
		pDC->FillSolidRect(&rcClient, COLOR_DIALOG_BG);
	DrawPagePanel(pDC, rcClient);

	const int nSel = GetCurSel();
	const int nCount = GetItemCount();
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < nCount; i++) {
			const BOOL bSel = (i == nSel);
			if ((pass == 0) == (bSel != FALSE)) continue;
			CRect rc;
			if (!GetItemRect(i, &rc)) continue;
			DrawTabItem(pDC, i, rc, bSel, (i == m_nHotItem));
		}
	}
}

// Fixer バッファへ不透明 DrawToDC。
void CCustomTabCtrl::PaintOpaqueIntoBuffer(HDC hdcBuf)
{
	if (!hdcBuf || !m_hWnd) return;
	CRect r;
	GetClientRect(&r);
	if (r.Width() <= 0 || r.Height() <= 0) return;
	CDC dc;
	dc.Attach(hdcBuf);
	DrawToDC(&dc, r, FALSE);
	dc.Detach();
}

// BufferedPaint+MakeOpaque。失敗時 Default。
void CCustomTabCtrl::PaintOpaqueClient(CDC& dc)
{
	CRect r;
	GetClientRect(&r);
	if (r.Width() <= 0 || r.Height() <= 0) return;
	BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
	params.dwFlags = BPPF_ERASE;
	HDC hdcBuf = NULL;
	HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
	if (!hdcBuf || !hBP) { Default(); return; }
	CDC mem;
	mem.Attach(hdcBuf);
	DrawToDC(&mem, r, FALSE);
	mem.Detach();
	::BufferedPaintMakeOpaque(hBP, &r);
	::EndBufferedPaint(hBP, TRUE);
}

// HostNeeds / 非 Aero → OpaqueClient（α=255）。
// Aero 透過: メモリDCをクロマ塗り→ページ領域を ExcludeClip して BlitChromaTrans。
// ページ内側を blit すると子ダイアログを上書きする。
// WM_PRINTCLIENT は常に不透明 DrawToDC。
void CCustomTabCtrl::OnPaint()
{
	CPaintDC dcPaint(this);
	CRect rcClient;
	GetClientRect(&rcClient);
	if (rcClient.IsRectEmpty()) return;

#if CCUSTOM_AERO_SUPPORT
	// SetAeroMode(FALSE) または親が不透明子を要求 → BufferedPaint で α=255
	const BOOL bWantChroma = m_bAeroMode && CCC_IsAeroEnabled() && CCC_IsWin11()
		&& !CCC_HostNeedsChildOpaque(m_hWnd);
	if (!bWantChroma) {
		PaintOpaqueClient(dcPaint);
		return;
	}
#else
	const BOOL bWantChroma = FALSE;
#endif

	CDC memDC;
	memDC.CreateCompatibleDC(&dcPaint);
	CBitmap memBmp;
	memBmp.CreateCompatibleBitmap(&dcPaint, rcClient.Width(), rcClient.Height());
	CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

	DrawToDC(&memDC, rcClient, bWantChroma);

	CRect rcDisp = rcClient;
	AdjustRect(FALSE, &rcDisp);
	if (!rcDisp.IsRectEmpty())
		dcPaint.ExcludeClipRect(&rcDisp);

#if CCUSTOM_AERO_SUPPORT
	if (bWantChroma)
		CCC_BlitChromaTrans(dcPaint.GetSafeHdc(), 0, 0, rcClient.Width(), rcClient.Height(),
			memDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
	else
#endif
		dcPaint.BitBlt(0, 0, rcClient.Width(), rcClient.Height(), &memDC, 0, 0, SRCCOPY);

	memDC.SelectObject(pOldBmp);
	memBmp.DeleteObject();
	memDC.DeleteDC();
}

// 等幅再計算。Invalidate のみ（同期描画しない）。
void CCustomTabCtrl::OnSize(UINT nType, int cx, int cy)
{
	CTabCtrl::OnSize(nType, cx, cy);
	const int n = GetItemCount();
	if (n > 0) LayoutEqualTabs(n);
	Invalidate(FALSE);
}

// ガラス穴対策の遅延 Opaque。
void CCustomTabCtrl::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	CTabCtrl::OnWindowPosChanged(lpwndpos);
#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled() && CCC_IsWin11())
		ScheduleOpaqueRepaint();
#endif
}

// kTabSoftTimerId: 互換。現在は DrawTabItem が Kill する（常時揺れ禁止）。
// kTabScrollOpaqueTimerId: 遅延不透明。
void CCustomTabCtrl::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kTabSoftTimerId) {
		if (GetCurSel() < 0) {
			KillTimer(kTabSoftTimerId);
			return;
		}
		Invalidate(FALSE);
		return;
	}
	if (nIDEvent == kTabScrollOpaqueTimerId) {
		KillTimer(kTabScrollOpaqueTimerId);
#if CCUSTOM_AERO_SUPPORT
		if (CCC_IsAeroEnabled() && CCC_IsWin11()) {
			CClientDC dc(this);
			PaintOpaqueClient(dc);
		}
#endif
		return;
	}
	CTabCtrl::OnTimer(nIDEvent);
}

// ホバー切替用。タブ矩形を少し膨らませて角丸はみ出しを拾う。
void CCustomTabCtrl::InvalidateTabItem(int nItem)
{
	if (nItem < 0 || !::IsWindow(m_hWnd)) return;
	CRect rc;
	if (!GetItemRect(nItem, &rc)) return;
	rc.InflateRect(2, 2);
	InvalidateRect(&rc, FALSE);
}

// ホットタブ切替。LEAVE を張り、前後のタブだけ Invalidate。
void CCustomTabCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_bTracking) {
		TRACKMOUSEEVENT tme = {};
		tme.cbSize = sizeof(tme);
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = m_hWnd;
		if (::TrackMouseEvent(&tme)) m_bTracking = TRUE;
	}
	TCHITTESTINFO hti = {};
	hti.pt = point;
	const int nHit = HitTest(&hti);
	if (nHit != m_nHotItem) {
		InvalidateTabItem(m_nHotItem);
		m_nHotItem = nHit;
		InvalidateTabItem(m_nHotItem);
	}
	CTabCtrl::OnMouseMove(nFlags, point);
}

// ホット解除。
void CCustomTabCtrl::OnMouseLeave()
{
	m_bTracking = FALSE;
	if (m_nHotItem != -1) {
		InvalidateTabItem(m_nHotItem);
		m_nHotItem = -1;
	}
	CTabCtrl::OnMouseLeave();
}

// REFLECT_EX。FALSE を返し親の ON_NOTIFY(TCN_SELCHANGE) も通す。
// 選択タブの不透明再描画を予約（アクリル下で古い選択色が残る）。
BOOL CCustomTabCtrl::OnSelChange(NMHDR*, LRESULT* pResult)
{
	Invalidate(FALSE);
#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled() && CCC_IsWin11())
		ScheduleOpaqueRepaint();
#endif
	if (pResult) *pResult = 0;
	return FALSE; // 親の ON_NOTIFY(TCN_SELCHANGE) も通す
}

// ============================================================================
// カスタム標準ボタンコントロール
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomStandardButton, CButton)

BEGIN_MESSAGE_MAP(CCustomStandardButton, CButton)
    ON_WM_CTLCOLOR_REFLECT()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
    ON_MESSAGE(BM_SETSTATE, OnBmSetState)
    ON_WM_MOUSEMOVE()
    ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
    ON_WM_SETFOCUS()
    ON_WM_KILLFOCUS()
    ON_WM_ENABLE()
    ON_WM_TIMER()
END_MESSAGE_MAP()

// カスタム標準ボタン。グラデ/影/アイコン/スパークル軌道の初期値。
// 常時 Soft3D タイマーは張らない（ピアノロール 60fps を食うため）。
// ホバー／フォーカス時だけ 33ms のキラキラタイマーを後から張る。
CCustomStandardButton::CCustomStandardButton()
    : m_bAutoDelete(FALSE), m_bMouseOver(FALSE), m_nAnimTick(0), m_bAnimRunning(FALSE),
    m_nSparkleN(0), m_nSparkleSpawnAcc(0),
    m_clrGradStart(RGB(255, 255, 255)),
    m_clrGradEnd(RGB(255, 255, 255)), m_nGradDirection(0), m_bGradEnable(FALSE),
    m_clrShadow(RGB(0, 0, 0)), m_nShadowDirection(135), m_nShadowDistance(2),
    m_nShadowBlur(3), m_bShadowEnable(FALSE),
    m_hIconIn(NULL), m_hIconOut(NULL), m_bFlat(FALSE), m_bAeroMode(FALSE),
    m_bIconOwnedIn(FALSE), m_bIconOwnedOut(FALSE), m_bAutoGlyphDone(FALSE)
{
    ZeroMemory(m_sparklePos, sizeof(m_sparklePos));
    m_brBackground.CreateSolidBrush(COLOR_BUTTON_BG);
}

// 外部からアニメ再開を頼む入口。実体は UpdateAnimTimer。
// HWND 未作成でも呼べるが、そちらで即 return する。
void CCustomStandardButton::EnsureAnimTimer()
{
    UpdateAnimTimer();
}

// スパークル点を 1 ティック進める。speed=7px、幅の 1/6 間隔で発生。
// bSpawn=FALSE は残点の移動のみ（マウス離脱後も点が消えるまで描く）。
// flat ボタンは点を持たない。端(W-2)に達した点は捨てる。
// 発生時、先頭付近に点がいると重ね発生を止めて帯が真っ白になるのを防ぐ。
void CCustomStandardButton::SparkleTick(BOOL bSpawn)
{
    if (m_bFlat) {
        m_nSparkleN = 0;
        m_nSparkleSpawnAcc = 0;
        return;
    }
    CRect r;
    GetClientRect(&r);
    const int W = r.Width();
    if (W < 8) {
        m_nSparkleN = 0;
        return;
    }
    // 点は左→右へ 7px/tick。gap 未満では新規 spawn しない。
    // ホバー終了後も残点が endPos に消えるまでタイマーが生きる。
    const int speed = 7;
    const int gap = max(20, W / 6);
    const int endPos = W - 2;

    int w = 0;
    for (int i = 0; i < m_nSparkleN; ++i)
    {
        const int np = m_sparklePos[i] + speed;
        if (np >= endPos) continue;
        m_sparklePos[w++] = np;
    }
    m_nSparkleN = w;

    if (!bSpawn) return;

    if (m_nSparkleN == 0)
    {
        m_sparklePos[0] = 0;
        m_nSparkleN = 1;
        m_nSparkleSpawnAcc = 0;
        return;
    }
    m_nSparkleSpawnAcc += speed;
    while (m_nSparkleSpawnAcc >= gap && m_nSparkleN < kBtnSparkleMax)
    {
        m_nSparkleSpawnAcc -= gap;
        BOOL near0 = FALSE;
        for (int i = 0; i < m_nSparkleN; ++i)
        {
            if (m_sparklePos[i] < gap / 2) { near0 = TRUE; break; }
        }
        if (near0) break;
        m_sparklePos[m_nSparkleN++] = 0;
    }
}

// 33ms キラキラタイマーの ON/OFF。常時軌道はうざいのでやめる。
// 条件: 有効 かつ (ホバー or フォーカス or 残点あり)。
// 不要になったら KillTimer + Invalidate。キャプション chrome でも同じ ID。
void CCustomStandardButton::UpdateAnimTimer()
{
    if (!GetSafeHwnd()) return;
    // ホバー／フォーカス／残点のみ（33ms の常時軌道はうざいのでやめる）
    const BOOL bWant = IsWindowEnabled() &&
        (m_bMouseOver || (GetFocus() == this) || m_nSparkleN > 0);
    if (bWant && !m_bAnimRunning)
    {
        m_bAnimRunning = TRUE;
        SetTimer(kButtonAnimTimerId, 33, NULL);
    }
    else if (!bWant && m_bAnimRunning)
    {
        m_bAnimRunning = FALSE;
        KillTimer(kButtonAnimTimerId);
        Invalidate(FALSE);
    }
}

// Soft3D 背景の常時ゆらぎ。
// 旧: 全 CCustom ボタンが 220ms で Invalidate → Soft3D ラスタが UI スレッドを占有し、
// ピアノロール等の 60fps 提示が「割り込みが長い／遅い」体感になった（〜7月末は無し）。
// Soft3D チップ自体は通常の OnPaint（ホバー／押下／親の再描画）で描く。常時タイマーは張らない。
static void CCC_ButtonSoftTimerSync(HWND hWnd, BOOL /*bFlat*/)
{
	if (hWnd)
		::KillTimer(hWnd, kButtonSoftTimerId);
}

// kButtonSoftTimerId: 残骸。キャプション chrome では即 Kill。
// kButtonAnimTimerId: ティック++、ホバー中のみ spawn、残点ゼロで停止。
// Invalidate(FALSE) だけ。Erase するとアクリルが抜ける。
void CCustomStandardButton::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kButtonSoftTimerId)
    {
        if (CCC_IsCaptionChromeCtrl(m_hWnd)) {
            KillTimer(kButtonSoftTimerId);
            return;
        }
        m_nAnimTick++;
        Invalidate(FALSE);
        return;
    }
    if (nIDEvent == kButtonAnimTimerId)
    {
        m_nAnimTick++;
        // ホバー中だけ新規点。flat（キャプション）は動かさない。
        SparkleTick(m_bMouseOver && !m_bFlat);
        UpdateAnimTimer(); // 残点ゼロ＆非ホバーなら停止
        Invalidate(FALSE);
        return;
    }
    CButton::OnTimer(nIDEvent);
}

// CCustomStandardButton の破棄。ブラシ／フォント／所有アイコンを解放。
// HWND は既に無い。所有 GDI/アイコンだけここで解放する。
CCustomStandardButton::~CCustomStandardButton()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
    if (m_bIconOwnedIn && m_hIconIn) { ::DestroyIcon(m_hIconIn); m_hIconIn = NULL; }
    if (m_bIconOwnedOut && m_hIconOut) { ::DestroyIcon(m_hIconOut); m_hIconOut = NULL; }
}

// HWND 破棄後。基底の PostNcDestroy のあと m_bAutoDelete なら this を delete。
// サブクラス解除後。new したコントロールはここで自殺する。
void CCustomStandardButton::PostNcDestroy()
{
    CButton::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

// 塗りグラデ。角度は 0..359。HWND があれば Invalidate(FALSE)。
// 無効角度は正規化。即再描画。
void CCustomStandardButton::SetGradation(COLORREF s, COLORREF e, int d, BOOL en)
{
    m_clrGradStart = s;
    m_clrGradEnd = e;
    m_nGradDirection = d % 360;
    if (m_nGradDirection < 0) m_nGradDirection += 360;
    m_bGradEnable = en;
    if (GetSafeHwnd()) Invalidate(FALSE);
}

// グラデ設定の読み出し。NULL ポインタは飛ばす。
// 出力ポインタは個別に省略可。
void CCustomStandardButton::GetGradation(COLORREF* ps, COLORREF* pe, int* pd, BOOL* pbe) const
{
    if (ps) *ps = m_clrGradStart;
    if (pe) *pe = m_clrGradEnd;
    if (pd) *pd = m_nGradDirection;
    if (pbe) *pbe = m_bGradEnable;
}

// ドロップシャドウ。blur は 0..20。未作成 HWND では描画しない。
// 距離は 0 以上。描画は PaintClient 側。
void CCustomStandardButton::SetDropShadow(COLORREF c, int d, int dist, int blur, BOOL en)
{
    m_clrShadow = c;
    m_nShadowDirection = d % 360;
    if (m_nShadowDirection < 0) m_nShadowDirection += 360;
    m_nShadowDistance = max(0, dist);
    m_nShadowBlur = max(0, min(20, blur));
    m_bShadowEnable = en;
    if (GetSafeHwnd()) Invalidate(FALSE);
}

// 影パラメータの読み出し。NULL は飛ばす。
// 出力ポインタは個別に省略可。
void CCustomStandardButton::GetDropShadow(COLORREF* pc, int* pd, int* pdist, int* pblur, BOOL* pbe) const
{
    if (pc) *pc = m_clrShadow;
    if (pd) *pd = m_nShadowDirection;
    if (pdist) *pdist = m_nShadowDistance;
    if (pblur) *pblur = m_nShadowBlur;
    if (pbe) *pbe = m_bShadowEnable;
}

// リソース ID からアイコンを LoadImage。所有して DestroyIcon。
// in/out の 2 枚。0 は無し。オートグリフ済み扱いにする。
DWORD CCustomStandardButton::SetIcon(int nIconIn, int nIconOut)
{
    HICON hIn = NULL;
    HICON hOut = NULL;
    if (nIconIn)
        hIn = (HICON)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(nIconIn),
            IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR);
    if (nIconOut)
        hOut = (HICON)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(nIconOut),
            IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR);
    if (m_bIconOwnedIn && m_hIconIn)
        ::DestroyIcon(m_hIconIn);
    if (m_bIconOwnedOut && m_hIconOut)
        ::DestroyIcon(m_hIconOut);
    m_hIconIn = hIn;
    m_hIconOut = hOut;
    m_bIconOwnedIn = (hIn != NULL);
    m_bIconOwnedOut = (hOut != NULL);
    m_bAutoGlyphDone = TRUE;
    if (GetSafeHwnd()) Invalidate(FALSE);
    return 0;
}

// 外部 HICON。所有しない（呼び出し側が寿命管理）。
// 以前所有していたアイコンだけ Destroy。
DWORD CCustomStandardButton::SetIcon(HICON hIconIn, HICON hIconOut)
{
    if (m_bIconOwnedIn && m_hIconIn && m_hIconIn != hIconIn)
        ::DestroyIcon(m_hIconIn);
    if (m_bIconOwnedOut && m_hIconOut && m_hIconOut != hIconOut)
        ::DestroyIcon(m_hIconOut);
    m_hIconIn = hIconIn;
    m_hIconOut = hIconOut;
    // 呼び出し側が寿命管理(非所有)
    m_bIconOwnedIn = FALSE;
    m_bIconOwnedOut = FALSE;
    m_bAutoGlyphDone = TRUE;
    if (GetSafeHwnd()) Invalidate(FALSE);
    return 0;
}

// ダイアログ ID から共有アイコンを一度だけ載せる。所有しない。
// 既にアイコンがある／一度試した ID は再 Load しない。
void CCustomStandardButton::EnsureAutoGlyph()
{
    if (m_hIconIn || m_bAutoGlyphDone || !GetSafeHwnd())
        return;
    m_bAutoGlyphDone = TRUE;
    const UINT iconId = CCC_CtlIconForCtrl((UINT)GetDlgCtrlID());
    if (!iconId)
        return;
    HICON h = CCC_LoadSharedIcon(iconId, 24);
    if (!h)
        return;
    m_hIconIn = h;
    m_bIconOwnedIn = FALSE;
}

// flat=キャプション chrome 等。スパークル無し、枠は細く。Soft タイマーは殺す。
// chrome ボタンはスパークル無し。
void CCustomStandardButton::SetFlat(BOOL bFlat)
{
    m_bFlat = bFlat;
    if (GetSafeHwnd()) {
        CCC_ButtonSoftTimerSync(m_hWnd, m_bFlat);
        Invalidate(FALSE);
    }
}

// アクリル透過オプトイン。親の隙間も Invalidate。
// 親の隙間再描画も忘れない。
void CCustomStandardButton::SetAeroMode(BOOL b)
{
    m_bAeroMode = b ? TRUE : FALSE;
    if (GetSafeHwnd()) {
        Invalidate(FALSE);
        CCC_InvalidateParent(m_hWnd, m_bAeroMode);
    }
}

// サブクラス直後。UxTheme を空にして既定テーマとカスタム OnPaint の白抜けを防ぐ。
// テーマ空指定はメニュー後の白抜け対策。
void CCustomStandardButton::PreSubclassWindow()
{
    CButton::PreSubclassWindow();
    // テーマ描画がカスタム OnPaint と混ざると白抜けする（メニュー後に顕在化しやすい）
    HMODULE h = ::LoadLibrary(_T("UxTheme.dll"));
    if (h)
    {
        typedef HRESULT(WINAPI* PFN)(HWND, LPCWSTR, LPCWSTR);
        if (PFN p = (PFN)::GetProcAddress(h, "SetWindowTheme"))
            p(m_hWnd, L"", L"");
        ::FreeLibrary(h);
    }
    UpdateAnimTimer();
    CCC_ButtonSoftTimerSync(m_hWnd, m_bFlat);
    EnsureAutoGlyph();
}

// WM_CTLCOLOR 反射。親が背景を塗るときのブラシ。
// 背景ブラシ。テキスト色は OnPaint 側。
HBRUSH CCustomStandardButton::CtlColor(CDC*, UINT)
{
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

// ボタン本体。メモリ DC にサテン/グラデ/ジェリー/スパークル/アイコン/文字。
// アクリル透過時はクロマ塗り＋薄いティントのみ（白枠は不透明板に見える）。
// 無効でも質感は描き、最後にグレーヴェール。隠し淫女は短く重ねるだけ。
// 最後に BitBlt またはクロマ合成。キャプション帯のグリフ専用文字は出さない。
void CCustomStandardButton::PaintClient(CDC& dc, const CRect& r)
{
    if (r.Width() <= 0 || r.Height() <= 0) return;
    EnsureAutoGlyph();
    CDC mDC;
    CBitmap mB;
    mDC.CreateCompatibleDC(&dc);
    mB.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
    CBitmap* ob = mDC.SelectObject(&mB);

    BOOL bP = (GetState() & BST_PUSHED) != 0;
    BOOL bF = (GetFocus() == this);
    BOOL bD = !IsWindowEnabled();
    const UINT stBtn = GetStyle() & BS_TYPEMASK;
    const BOOL bPushLike = (stBtn == BS_CHECKBOX || stBtn == BS_AUTOCHECKBOX)
        && (GetStyle() & BS_PUSHLIKE);
    if (bPushLike && (GetCheck() == BST_CHECKED)) bP = TRUE;
    const BOOL bShowFlow = m_bMouseOver;
#if CCUSTOM_AERO_SUPPORT
    // オプトイン透過は Win11+アクリル時のみ（非アクリルでクロマ穴を開けない）
    const BOOL bAeroTrans = m_bAeroMode && CCC_IsWin11() && CCC_IsAeroEnabled();
#else
    const BOOL bAeroTrans = FALSE;
#endif

    // 無効時もカスタムの質感(グラデ/サテン/ツヤ/装飾)は描く。ただし彩度を落とし、
    // 最後にやわらかいグレーヴェールを重ねて「無効」であることを明確に伝える。
    // アクリル透過(Lib/Hist 等): 背景はクロマ、ホバー/押下のみ薄いティントを載せる。
    if (bAeroTrans)
    {
        mDC.FillSolidRect(&r, CCC_AERO_CHROMA_KEY);
        if (bP)
            mDC.FillSolidRect(&r, bD ? CCC_Desaturate(RGB(255, 210, 230), 68) : RGB(255, 210, 230));
        else if (m_bMouseOver)
            mDC.FillSolidRect(&r, bD ? CCC_Desaturate(RGB(255, 235, 245), 68) : RGB(255, 235, 245));
    }
    else
    {
    COLORREF bg = bP ? COLOR_BUTTON_PUSHED : (m_bMouseOver ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG);
    if (bD) bg = CCC_Desaturate(bg, 68);
    if (m_bGradEnable)
        DrawGradientBackground(&mDC, r,
            bD ? CCC_Desaturate(m_clrGradStart, 68) : m_clrGradStart,
            bD ? CCC_Desaturate(m_clrGradEnd, 68) : m_clrGradEnd,
            m_nGradDirection);
    else
        DrawSatinFill(&mDC, r, bg);          // サテン/シルク質感
    }

    if (!bAeroTrans)
    {
        // ぷるんとした濡れツヤ + ジェリー感(リムライト&インナーシャドウ)
        CRect rg = r;
        if (bP) rg.OffsetRect(1, 1);
        DrawGlossHighlight(&mDC, rg, m_bFlat ? 4 : 8);
        if (!m_bFlat)
            DrawJellyEdges(&mDC, rg, 8, RGB(120, 40, 80));
        // Soft3D: 通常ボタンは背景ゆらぎ、flat でも角チップ（キャプション帯以外）
        if (!CCC_IsCaptionChromeCtrl(m_hWnd) && r.Width() >= 24 && r.Height() >= 16)
        {
            const int tick = (int)(::GetTickCount64() / 80) + (int)(m_nAnimTick / 2);
            if (!m_bFlat && r.Width() >= 36 && r.Height() >= 20)
                DrawSoftJkBackdrop(&mDC, rg, tick, m_bMouseOver || bP);
            else {
                const int cs = min(12, min(r.Width(), r.Height()) / 2);
                DrawSoftJkChip(&mDC,
                    CRect(rg.right - cs - 2, rg.top + 2, rg.right - 2, rg.top + 2 + cs),
                    tick, m_bMouseOver || bP);
            }
        }

        // 大きめのボタンは裾に透けレースの色気を(アイコン平坦ボタンは省略=はみ出し防止)
        if (!m_bFlat && !m_hIconIn && r.Width() >= 64 && r.Height() >= 26)
            DrawLaceScallop(&mDC, r.left + 8, r.bottom - 6, r.right - 8, 3, COLOR_LACE);

        // ホバー時: とろみハイライトがスーッと流れる(押下トグル上でもホバー中は表示)
        // 点はホバー終了後も残点が消えるまで描画
        if (!m_bFlat && (bShowFlow || m_nSparkleN > 0))
        {
            const int W = r.Width();
            const int H = r.Height();
            CRgn rgn; rgn.CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, 16, 16);
            mDC.SelectClipRgn(&rgn);
            if (bShowFlow)
            {
                const int bandW = max(10, W / 4);
                const int period = W + bandW + W / 2;
                const int pos = (int)((m_nAnimTick * 7) % (UINT)max(1, period));
                CRect band(r.left + pos - bandW, r.top, r.left + pos, r.bottom);
                FillRectAlpha(&mDC, band, RGB(255, 255, 255), bP ? 48 : 72);
            }
            const int cy = r.top + H / 2;
            for (int di = 0; di < m_nSparkleN; ++di)
            {
                const int ox = m_sparklePos[di];
                if (ox < 2 || ox >= W - 2) continue;
                const int sx = r.left + ox;
                const int yoff = ((di & 1) ? -1 : 1) * (2 + (di % 3));
                const int sy = cy + yoff;
                if (sy < r.top + 2 || sy > r.bottom - 2) continue;
                const int sz = (di == 0) ? 4 : 2;
                DrawShine(&mDC, sx, sy, sz, sz);
                DrawSparkle(&mDC, sx, sy, max(1, sz - 1), COLOR_SPARKLE);
            }
            mDC.SelectClipRgn(NULL);
        }
        // フォーカス: 鼓動のようにほのかに明滅
        if (bF)
        {
            const double ph = (m_nAnimTick % 44) / 44.0 * 6.2831853;
            const int a = 24 + (int)(26 * (0.5 + 0.5 * sin(ph)));
            FillRectAlpha(&mDC, r, COLOR_BUTTON_HOVER, a);
        }

        if (!m_bFlat && !m_hIconIn)
        {
            DrawDecorations(&mDC, r, 0, bP);
            // 四隅にさりげないキラキラ
            DrawSparkle(&mDC, r.left + 9, r.top + 9, 3, COLOR_SPARKLE);
            DrawSparkle(&mDC, r.right - 9, r.bottom - 9, 3, COLOR_SPARKLE);
            // ほんのり頬染めで色っぽく
            {
                const int by = r.top + r.Height() * 64 / 100;
                const int bx = max(10, r.Width() / 6);
                DrawBlush(&mDC, r.left + bx, by, max(6, r.Width() / 10), max(3, r.Height() / 7));
                DrawBlush(&mDC, r.right - bx, by, max(6, r.Width() / 10), max(3, r.Height() / 7));
            }
            if (m_bMouseOver && !bP)
            {
                // ホバーでリボンがゆっくり回転 + Soft3D 結び
                const float ang = sinf((float)m_nAnimTick * 0.08f) * 14.f;
                DrawLooseRibbon(&mDC, CRect(r.Width() / 2 - 11, r.top + 2, r.Width() / 2 + 11, r.top + 16), COLOR_BOW, ang);
                DrawSoftJkKnot(&mDC, CRect(r.Width() / 2 - 10, r.top + 1, r.Width() / 2 + 10, r.top + 18), (int)m_nAnimTick);
                DrawSparkle(&mDC, r.right - 10, r.top + 10, 4, COLOR_SPARKLE);
                DrawSparkle(&mDC, r.left + 12, r.bottom - 10, 3, COLOR_SPARKLE);
            }
            if (bP)
            {
                DrawSparkle(&mDC, r.Width() / 2, r.top + 8, 4, COLOR_SPARKLE);
                DrawStar(&mDC, r.left + 15, r.Height() / 2, 2, RGB(255, 240, 150));
                DrawStar(&mDC, r.right - 15, r.Height() / 2, 2, RGB(255, 240, 150));
                const float ang = sinf((float)m_nAnimTick * 0.1f) * 10.f;
                DrawLooseRibbon(&mDC, CRect(r.Width() / 2 - 10, r.bottom - 15, r.Width() / 2 + 10, r.bottom - 2), COLOR_BOW, ang);
            }
        }
        else if (m_bMouseOver && m_hIconIn)
        {
            // アイコンボタンは控えめなハイライトだけ
            FillRectAlpha(&mDC, r, RGB(255, 255, 255), 36);
        }
    }

    // 無効: 装飾の質感は残しつつ、やわらかいグレーのヴェールで沈めて「押せない」ことを明示。
    if (bD && !bAeroTrans)
        FillRectAlpha(&mDC, r, RGB(232, 232, 232), 122);

    if (!bAeroTrans)
    {
    CPen pL(PS_SOLID, m_bFlat ? 1 : 2, RGB(255, 255, 255));
    CPen pD(PS_SOLID, m_bFlat ? 1 : 2, RGB(128, 128, 128));
    CPen* op;
    if (bP)
    {
        op = mDC.SelectObject(&pD);
        mDC.MoveTo(r.left, r.bottom - 1);
        mDC.LineTo(r.left, r.top);
        mDC.LineTo(r.right - 1, r.top);
        mDC.SelectObject(&pL);
        mDC.LineTo(r.right - 1, r.bottom - 1);
        mDC.LineTo(r.left, r.bottom - 1);
        CRect ri = r;
        ri.DeflateRect(m_bFlat ? 1 : 2, m_bFlat ? 1 : 2);
        mDC.SelectObject(&pD);
        mDC.MoveTo(ri.left, ri.bottom - 1);
        mDC.LineTo(ri.left, ri.top);
        mDC.LineTo(ri.right - 1, ri.top);
    }
    else
    {
        op = mDC.SelectObject(&pL);
        mDC.MoveTo(r.left, r.bottom - 1);
        mDC.LineTo(r.left, r.top);
        mDC.LineTo(r.right - 1, r.top);
        mDC.SelectObject(&pD);
        mDC.LineTo(r.right - 1, r.bottom - 1);
        mDC.LineTo(r.left, r.bottom - 1);
        CRect ri = r;
        ri.DeflateRect(m_bFlat ? 1 : 2, m_bFlat ? 1 : 2);
        mDC.SelectObject(&pL);
        mDC.MoveTo(ri.left, ri.bottom - 1);
        mDC.LineTo(ri.left, ri.top);
        mDC.LineTo(ri.right - 1, ri.top);
    }
    mDC.SelectObject(op);

    if (bF && !bD)
    {
        CRect rf = r;
        rf.DeflateRect(m_bFlat ? 2 : 4, m_bFlat ? 2 : 4);
        mDC.DrawFocusRect(&rf);
    }
    }
    else if (bF && !bD)
    {
        // 透過時は枠なし・フォーカスは点線のみ（白枠が不透明板に見えるのを防ぐ）
        CRect rf = r;
        rf.DeflateRect(1, 1);
        mDC.DrawFocusRect(&rf);
    }

    // アイコン。ラベル付きはアイコン+文字を一塊にして中央。狭いときはアイコンのみ中央。
    CString s;
    GetWindowText(s);
    const BOOL glyphOnly = CCC_CaptionIsGlyphOnly(s);
    HICON hDraw = m_hIconIn;
    if (m_bMouseOver && m_hIconOut)
        hDraw = m_hIconOut;
    BOOL drawLabel = hDraw && !glyphOnly && !s.IsEmpty();
    int iconRight = r.left;

    CFont* pF = GetFont();
    CFont* pOF = mDC.SelectObject(pF ? pF : (CFont*)mDC.SelectStockObject(DEFAULT_GUI_FONT));
    CSize te(0, 0);
    if (!s.IsEmpty() && !glyphOnly)
        te = CCC_MeasureFitText(&mDC, s, CCC_TextHasBreak(s));

    if (hDraw)
    {
        const int pad = m_bFlat ? 4 : 6;
        const int maxSz = (std::max)(8, (std::min)(r.Width(), r.Height()) - pad);
        const UINT dpi = CCC_GetControlDpi(m_hWnd);
        int want = CCC_ScaleDpi(drawLabel ? 16 : 24, dpi);
        if (drawLabel) {
            if (want < 14) want = 14;
            if (want > 20) want = 20;
        } else {
            if (want < 16) want = 16;
            if (want > 36) want = 36;
        }
        const int iw = (std::min)(want, maxSz);
        const int ih = iw;
        const int edge = m_bFlat ? 3 : 5;
        const int gap = 3;
        if (drawLabel) {
            const int remain = r.Width() - edge * 2 - iw - gap;
            if (remain < (std::max)(8, (int)te.cx / 2))
                drawLabel = FALSE;
        }
        int ix, iy;
        if (drawLabel) {
            const int cluster = iw + gap + te.cx;
            int ix0 = r.left + (r.Width() - cluster) / 2;
            if (ix0 < r.left + edge)
                ix0 = r.left + edge;
            ix = ix0;
            iy = r.top + (r.Height() - ih) / 2;
            iconRight = ix + iw;
        } else {
            ix = r.left + (r.Width() - iw) / 2;
            iy = r.top + (r.Height() - ih) / 2;
            iconRight = ix + iw;
        }
        if (bP) { ix += 1; iy += 1; }
        ::DrawIconEx(mDC.GetSafeHdc(), ix, iy, hDraw, iw, ih, 0, NULL, DI_NORMAL);
    }

    if (!s.IsEmpty() && !hDraw)
        DrawSmartText(&mDC, r, s, bD, bP);
    else if (drawLabel)
    {
        CRect tr = r;
        tr.left = iconRight + 3;
        tr.DeflateRect(2, 1);
        DrawSmartText2(&mDC, tr, s, DT_LEFT | DT_VCENTER | DT_NOPREFIX, bD, bP);
    }
    mDC.SelectObject(pOF);

    if (!bD) CCC_DrawInwoman(&mDC, r, bAeroTrans); // 淫女モード演出

#if CCUSTOM_AERO_SUPPORT
    if (bAeroTrans) {
        int ox = 0, oy = 0;
        CCC_InwomanGetShake(ox, oy);
        CCC_BlitChromaTrans(dc.GetSafeHdc(), ox, oy, r.Width(), r.Height(),
            mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    } else
#endif
    {
        CCC_InwomanBitBlt(dc.GetSafeHdc(), r.Width(), r.Height(), mDC.GetSafeHdc(), COLOR_BUTTON_BG);
    }
    mDC.SelectObject(ob);
    mB.DeleteObject();
    mDC.DeleteDC();
}

// ガラス上では GDI が α=0 のまま合成され消えるので、全面不透明にして出す。
// キャプション常時アクリル後は BeginBufferedPaint 連打を避け AlphaBlend。
// 失敗時だけ BufferedPaint + MakeOpaque。中身は PaintClient。
void CCustomStandardButton::PaintOpaqueClient(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0)
        return;

    // dffb3db 以降のキャプション常時アクリル: BeginBufferedPaint 連打を避けて AlphaBlend
    RECT rr = { 0, 0, r.Width(), r.Height() };
    CDC mDC;
    CBitmap mB;
    if (mDC.CreateCompatibleDC(&dc) && mB.CreateCompatibleBitmap(&dc, r.Width(), r.Height())) {
        HGDIOBJ ob = mDC.SelectObject(mB);
        PaintClient(mDC, r);
        CCC_BlitToRectOpaque(dc.GetSafeHdc(), rr, mDC.GetSafeHdc(), 0, 0,
            r.Width(), r.Height(), r.Width(), r.Height(), FALSE);
        mDC.SelectObject(ob);
        return;
    }

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP)
    {
        PaintClient(dc, r);
        return;
    }
    {
        CDC dcBuf;
        dcBuf.Attach(hdcBuf);
        PaintClient(dcBuf, r);
        dcBuf.Detach();
    }
    ::BufferedPaintMakeOpaque(hBP, &r);
    ::EndBufferedPaint(hBP, TRUE);
}

// CPaintDC を待たず即描画。オプトイン透過はクロマ、ホスト α は不透明パス。
// キャプション chrome かつ親が AcrylicCaption のときも不透明パス。
void CCustomStandardButton::RepaintClient()
{
    if (!GetSafeHwnd())
        return;
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0)
        return;
#if CCUSTOM_AERO_SUPPORT
    // オプトイン透過: 不透明パスを使わずクロマ合成
    if (m_bAeroMode && CCC_IsWin11() && CCC_IsAeroEnabled())
    {
        CClientDC dc(this);
        PaintClient(dc, r);
        CCC_InvalidateParent(m_hWnd, TRUE);
        return;
    }
    // ホスト α（本文 aero / キャプションのみガラス）では素 BitBlt が消える
    if (CCC_IsWin11() && (CCC_IsAeroEnabled() || CCC_HostNeedsChildOpaque(m_hWnd)
        || CCC_CaptionOnlyHostGlass(m_hWnd)
        || (CCC_IsCaptionChromeCtrl(m_hWnd) && CCC_AcrylicCaption(::GetParent(m_hWnd)))))
    {
        CClientDC dc(this);
        PaintOpaqueClient(dc);
        return;
    }
#endif
    CClientDC dc(this);
    PaintClient(dc, r);
}

// WM_PAINT。透過はクロマ、ホストガラスは不透明パス、それ以外は素描画。
// CPaintDC。経路分岐は aero / ホストガラス / 通常。
void CCustomStandardButton::OnPaint()
{
#if CCUSTOM_AERO_SUPPORT
    if (m_bAeroMode && CCC_IsWin11() && CCC_IsAeroEnabled())
    {
        CPaintDC dc(this);
        CRect r;
        GetClientRect(&r);
        PaintClient(dc, r);
        return;
    }
    if (CCC_IsWin11() && (CCC_IsAeroEnabled() || CCC_HostNeedsChildOpaque(m_hWnd)
        || CCC_CaptionOnlyHostGlass(m_hWnd)
        || (CCC_IsCaptionChromeCtrl(m_hWnd) && CCC_AcrylicCaption(::GetParent(m_hWnd)))))
    {
        CPaintDC dc(this);
        PaintOpaqueClient(dc);
        return;
    }
#endif
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
    PaintClient(dc, r);
}

// WM_PRINTCLIENT。OpaqueFixer の BufferedPaint 先へ同じ内容を描く。
// fixer の BufferedPaint 先。戻り 0。
LRESULT CCustomStandardButton::OnPrintClient(WPARAM wParam, LPARAM)
{
    CDC* pDC = CDC::FromHandle((HDC)wParam);
    if (pDC)
    {
        CRect r;
        GetClientRect(&r);
#if CCUSTOM_AERO_SUPPORT
        if (m_bAeroMode && CCC_IsWin11() && CCC_IsAeroEnabled())
            PaintClient(*pDC, r);
        else if (CCC_IsWin11() && (CCC_IsAeroEnabled() || CCC_HostNeedsChildOpaque(m_hWnd)
            || CCC_CaptionOnlyHostGlass(m_hWnd)
            || (CCC_IsCaptionChromeCtrl(m_hWnd) && CCC_AcrylicCaption(::GetParent(m_hWnd)))))
            PaintOpaqueClient(*pDC);
        else
#endif
            PaintClient(*pDC, r);
    }
    return 0;
}

// テーマ無効時、Default の押下描画は空/白→アクリル上では完全透過。
// 状態だけ更新してからカスタムで描き直す。
LRESULT CCustomStandardButton::OnBmSetState(WPARAM wParam, LPARAM)
{
    // テーマ無効時、Default の押下描画は空/白になり、アクリル上では完全透過に見える。
    // 状態だけ更新してからカスタムで描き直す。
    const LRESULT lr = Default();
    UNREFERENCED_PARAMETER(wParam);
    if (GetSafeHwnd())
        RepaintClient();
    return lr;
}

// 消去握りつぶし。アクリル上で ERASE だけだと完全透過のまま残る。
// TRUE で既定塗りを止める。
BOOL CCustomStandardButton::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
    // 透過ボタン: 消去で不透明塗りをしない（親アクリルを残す）
    if (m_bAeroMode && CCC_IsWin11() && CCC_IsAeroEnabled())
        return TRUE;
    // 空返し禁止: ERASE だけの更新だとアクリル上で完全透過のまま残る（ホバーで復帰する現象）
    if (CCC_IsWin11() && (CCC_IsAeroEnabled() || CCC_HostNeedsChildOpaque(m_hWnd)
        || CCC_CaptionOnlyHostGlass(m_hWnd)))
    {
        if (pDC)
            PaintOpaqueClient(*pDC);
        return TRUE;
    }
#endif
    if (pDC)
    {
        CRect r;
        GetClientRect(&r);
        pDC->FillSolidRect(&r, COLOR_BUTTON_BG);
    }
    return TRUE;
}

// 初回ホバーで TME_LEAVE を張り、スパークル先頭点を 0 から起動。
// 既に点が残っているときは位置をリセットしない（離脱後の残点を生かす）。
void CCustomStandardButton::OnMouseMove(UINT f, CPoint p)
{
    if (!m_bMouseOver)
    {
        TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&t);
        m_bMouseOver = TRUE;
        if (!m_bFlat && m_nSparkleN <= 0)
        {
            m_sparklePos[0] = 0;
            m_nSparkleN = 1;
            m_nSparkleSpawnAcc = 0;
        }
        UpdateAnimTimer();
        Invalidate(FALSE);
        CCC_InvalidateParent(m_hWnd, m_bAeroMode);
    }
    CButton::OnMouseMove(f, p);
}

// ホバー解除。残点がある間は UpdateAnimTimer が 33ms を維持する。
// 親アクリルの隙間も Invalidate。
LRESULT CCustomStandardButton::OnMouseLeave(WPARAM, LPARAM)
{
    m_bMouseOver = FALSE;
    // 残点がある間は UpdateAnimTimer がタイマーを維持
    UpdateAnimTimer();
    Invalidate(FALSE);
    CCC_InvalidateParent(m_hWnd, m_bAeroMode);
    return 0;
}

// フォーカスでキラキラ／鼓動を再開。
// フォーカス枠と鼓動アニメ。
void CCustomStandardButton::OnSetFocus(CWnd* p)
{
    CButton::OnSetFocus(p);
    UpdateAnimTimer();
    Invalidate(FALSE);
}

// フォーカス喪失。残点が無ければタイマー停止。
// 残点が無ければ 33ms を止める。
void CCustomStandardButton::OnKillFocus(CWnd* p)
{
    CButton::OnKillFocus(p);
    UpdateAnimTimer();
    Invalidate(FALSE);
}

// 無効化でアニメ停止。質感は OnPaint 側でヴェール。
// 無効でも質感は描き、ヴェールで沈める。
void CCustomStandardButton::OnEnable(BOOL b)
{
    CButton::OnEnable(b);
    UpdateAnimTimer();
    Invalidate(FALSE);
}

// ============================================================================
// カスタムチェックボックスコントロール
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomCheckBox, CButton)

BEGIN_MESSAGE_MAP(CCustomCheckBox, CButton)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_TIMER()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
END_MESSAGE_MAP()

// CCustomCheckBox のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。タイマーは PreSubclass/OnCreate で張る。
CCustomCheckBox::CCustomCheckBox()
    : m_bAutoDelete(FALSE), m_bIsFlatStyle(FALSE), m_bIsPressed(FALSE),
    m_bIsHot(FALSE), m_bTracking(FALSE), m_nCheck(0), m_bAeroMode(FALSE), m_nBounce(0)
{}

// チェック ON のぷるん。m_nBounce=8、28ms で減衰しながら Invalidate。
// レ点矩形を sin で膨らませる（OnDrawLayer）。HWND 無しでは何もしない。
void CCustomCheckBox::StartCheckBounce()
{
    if (!GetSafeHwnd()) return;
    // 8 フレームの減衰バウンス（28ms × 約 0.2 秒）。
    m_nBounce = 8;
    SetTimer(kCheckBounceTimerId, 28, NULL);
    Invalidate();
}

// バウンス: カウントを減らし 0 で Kill。ホバー: ホット中だけ再描画。
// ホバータイマーは Soft3D ヨー用。非ホットなら即停止。
void CCustomCheckBox::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kCheckBounceTimerId)
    {
        if (--m_nBounce <= 0) { m_nBounce = 0; KillTimer(kCheckBounceTimerId); }
        Invalidate();
        return;
    }
    if (nIDEvent == kCheckHoverTimerId)
    {
        if (!m_bIsHot) { KillTimer(kCheckHoverTimerId); return; }
        Invalidate(FALSE);
        return;
    }
    CButton::OnTimer(nIDEvent);
}

// CCustomCheckBox の破棄。所有 GDI は無い（空）。
CCustomCheckBox::~CCustomCheckBox() {}

// HWND 破棄後。基底の PostNcDestroy のあと m_bAutoDelete なら this を delete。
// サブクラス解除後。new したコントロールはここで自殺する。
void CCustomCheckBox::PostNcDestroy()
{
    CButton::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

// オーナードローなので CButton::GetCheck ではなく内部値。
// オーナードロー後は CButton の値が信用できない。
int CCustomCheckBox::GetCheck()
{
    return m_nCheck;
}

// 内部 m_nCheck と CButton を同期。CHECKED ならバウンス開始。
// プログラムからの Set でも ON 時は同じ演出。
void CCustomCheckBox::SetCheck(int n)
{
    m_nCheck = n;
    CButton::SetCheck(n);
    if (n == BST_CHECKED) StartCheckBounce();
    Invalidate();
}

// サブクラス直後。UxTheme を空にして既定テーマとカスタム OnPaint の白抜けを防ぐ。
// テーマ空指定はメニュー後の白抜け対策。
void CCustomCheckBox::PreSubclassWindow()
{
    HMODULE h = LoadLibrary(_T("UxTheme.dll"));
    if (h)
    {
        typedef HRESULT(WINAPI* S)(HWND, LPCWSTR, LPCWSTR);
        S p = (S)GetProcAddress(h, "SetWindowTheme");
        if (p) p(m_hWnd, L"", L"");
        FreeLibrary(h);
    }
    m_bIsFlatStyle = (GetStyle() & BS_FLAT) || (GetStyle() & BS_PUSHLIKE);
    m_nCheck = CButton::GetCheck();
    ModifyStyle(BS_TYPEMASK | BS_FLAT | BS_PUSHLIKE, BS_OWNERDRAW);
    CButton::PreSubclassWindow();
}

// フォントを基底へ渡す。自前 HFONT は持たない。
// 親フォントをそのまま使う。
void CCustomCheckBox::SetFont(CFont* p, BOOL b)
{
    CButton::SetFont(p, b);
}

// 帯の空きは HTCAPTION ドラッグ。最大化中は先に復元。
// 帯ドラッグは HTCAPTION。
void CCustomCheckBox::OnLButtonDown(UINT n, CPoint p)
{
    m_bIsPressed = m_bIsHot = TRUE;
    SetCapture();
    Invalidate();
}

// キャプチャ内・矩形内ならトグル。ON でバウンス、親へ BN_CLICKED。
// 外で離すとキャンセル（押下フラグだけ落とす）。
void CCustomCheckBox::OnLButtonUp(UINT n, CPoint p)
{
    if (m_bIsPressed)
    {
        m_bIsPressed = FALSE;
        ReleaseCapture();
        CRect r;
        GetClientRect(&r);
        if (r.PtInRect(p))
        {
            m_nCheck = (m_nCheck == BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED;
            CButton::SetCheck(m_nCheck);
            if (m_nCheck == BST_CHECKED) StartCheckBounce();
            GetParent()->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), BN_CLICKED), (LPARAM)m_hWnd);
        }
        Invalidate();
    }
}

// ホット追跡。初回だけ TME_LEAVE。矩形内外でホバータイマーを張る/止める。
// ホット変化時だけ Invalidate（Soft3D ヨー用）。
void CCustomCheckBox::OnMouseMove(UINT n, CPoint p)
{
    if (!m_bTracking)
    {
        TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&t);
        m_bTracking = TRUE;
    }
    CRect r;
    GetClientRect(&r);
    BOOL h = r.PtInRect(p);
    if (m_bIsHot != h)
    {
        m_bIsHot = h;
        if (h) SetTimer(kCheckHoverTimerId, 50, NULL);
        else KillTimer(kCheckHoverTimerId);
        Invalidate();
    }
}

// ホット解除。ホバータイマーも止める。
// ホット／トラッキング解除。
void CCustomCheckBox::OnMouseLeave()
{
    m_bIsHot = m_bTracking = FALSE;
    KillTimer(kCheckHoverTimerId);
    Invalidate();
}

// WM_PAINT。透過はクロマ、ホストガラスは不透明パス、それ以外は素描画。
// CPaintDC。経路分岐は aero / ホストガラス / 通常。
void CCustomCheckBox::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);

    // 透過(アクリル)時は CompositeTransparent が処理するので従来通り直接描画
    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    if (bTrans || r.Width() <= 0 || r.Height() <= 0)
    {
        OnDrawLayer(&dc, r);
        return;
    }

#if CCUSTOM_AERO_SUPPORT
    // キャプションのみホスト α / 本文 aero: 素 BitBlt は α=0 で消える
    if (CCC_IsWin11() && (CCC_IsAeroEnabled() || CCC_CaptionOnlyHostGlass(m_hWnd)))
    {
        BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
        params.dwFlags = BPPF_ERASE;
        HDC hdcBuf = NULL;
        HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
        if (hdcBuf && hBP) {
            CDC dcBuf;
            dcBuf.Attach(hdcBuf);
            OnDrawLayer(&dcBuf, r);
            dcBuf.Detach();
            ::BufferedPaintMakeOpaque(hBP, &r);
            ::EndBufferedPaint(hBP, TRUE);
            return;
        }
    }
#endif

    // 非透過時はダブルバッファ化してちらつきを防ぐ(淫女モードの毎フレーム再描画対策)
    CDC mem;
    if (!mem.CreateCompatibleDC(&dc)) { OnDrawLayer(&dc, r); return; }
    CBitmap bmp;
    if (!bmp.CreateCompatibleBitmap(&dc, r.Width(), r.Height())) { OnDrawLayer(&dc, r); return; }
    CBitmap* ob = mem.SelectObject(&bmp);
    OnDrawLayer(&mem, r);
    CCC_InwomanBitBlt(dc.GetSafeHdc(), r.Width(), r.Height(), mem.GetSafeHdc(), COLOR_DIALOG_BG);
    mem.SelectObject(ob);
}

// WM_PRINTCLIENT。OpaqueFixer の BufferedPaint 先へ同じ内容を描く。
// fixer の BufferedPaint 先。戻り 0。
LRESULT CCustomCheckBox::OnPrintClient(WPARAM w, LPARAM)
{
    CDC* pDC = CDC::FromHandle((HDC)w);
    CRect r;
    GetClientRect(&r);
    OnDrawLayer(pDC, r);
    return 0;
}

// 消去握りつぶし。アクリル上で ERASE だけだと完全透過のまま残る。
// TRUE で既定塗りを止める。
BOOL CCustomCheckBox::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

// アクリル透過時、クロマ塗りしたメモリ DC に drawFn してクロマ合成。
// 非透過は destDC へ直接。チェック／他コントロール共用。
static void CCC_CompositeTrans(HWND hWnd, BOOL bAeroMode, CDC& destDC, const CRect& rect, std::function<void(CDC&)> drawFn)
{
    const BOOL bTrans = CCC_UseTransPaint(hWnd, bAeroMode);
    if (!bTrans)
    {
        drawFn(destDC);
        return;
    }
    CDC memDC;
    memDC.CreateCompatibleDC(&destDC);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&destDC, rect.Width(), rect.Height());
    CBitmap* pOld = memDC.SelectObject(&bmp);
    memDC.FillSolidRect(0, 0, rect.Width(), rect.Height(), CCC_AERO_CHROMA_KEY);
    drawFn(memDC);
    CCC_BlitChromaTrans(destDC.GetSafeHdc(), rect.left, rect.top, rect.Width(), rect.Height(),
        memDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    memDC.SelectObject(pOld);
}

// 本体描画。flat/pushlike はボタン塗り、通常はローズ枠＋レ点。
// バウンス中はレ点を枠からはみ出して膨らませる（隣にはみ出さないようクランプ）。
// キャプション帯の「メインに追従」は白文字（暗色だと黒帯に溶ける）。
// 隠し淫女は短く重ねるだけ。
void CCustomCheckBox::OnDrawLayer(CDC* pDC, CRect rect)
{
    const int rw = rect.Width();
    const int rh = rect.Height();
    if (rw <= 0 || rh <= 0) return;

    CCC_CompositeTrans(m_hWnd, m_bAeroMode, *pDC, rect, [&](CDC& dc)
    {
        BOOL bC = (m_nCheck == BST_CHECKED); BOOL bD = !IsWindowEnabled(); BOOL bP = m_bIsPressed && m_bIsHot;
        dc.SelectObject(GetFont() ? GetFont() : (CFont*)dc.SelectStockObject(DEFAULT_GUI_FONT));

        if (m_bIsFlatStyle)
        {
            BOOL s = bC || bP;
            // 無効時も装飾は描く(彩度を落とす)。最後にグレーヴェールで沈める。
            COLORREF bg = s ? COLOR_BUTTON_PUSHED : (m_bIsHot ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG);
            if (bD) bg = CCC_Desaturate(bg, 68);
            dc.FillSolidRect(0, 0, rw, rh, bg);
            DrawDecorations(&dc, CRect(0, 0, rw, rh), 0, s);
            if (bD) FillRectAlpha(&dc, CRect(0, 0, rw, rh), RGB(232, 232, 232), 122);
            dc.Draw3dRect(CRect(0, 0, rw, rh), s ? RGB(100, 100, 100) : RGB(255, 255, 255), s ? RGB(255, 255, 255) : RGB(100, 100, 100));
            CString t; GetWindowText(t);
            DrawSmartText(&dc, CRect(0, 0, rw, rh), t, bD, s);
        }
        else
        {
            const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
            if (!bTrans) dc.FillSolidRect(0, 0, rw, rh, COLOR_DIALOG_BG);
            int s = rh - 4;
            if (s > 18) s = 18;
            if (s < 14) s = 14;
            int cy2 = rh / 2;
            CRect rcB(0, cy2 - s / 2, s, cy2 + s / 2);
            // チェック枠はやわらかいローズで(無効時は彩度を落として「無効」を伝える)
            COLORREF clrFrame = bC ? RGB(255, 120, 165) : RGB(255, 156, 184);
            COLORREF clrBox = RGB(255, 249, 252);
            if (bD) { clrFrame = CCC_Desaturate(clrFrame, 62); clrBox = CCC_Desaturate(clrBox, 62); }
            CPen p2(PS_SOLID, 2, clrFrame);
            CBrush b2(clrBox);
            dc.SelectObject(&p2); dc.SelectObject(&b2);
            dc.RoundRect(&rcB, CPoint(6, 6));
            DrawGlossHighlight(&dc, rcB, 4);
            DrawJellyEdges(&dc, rcB, 4, RGB(120, 40, 80));   // ぷっくりジェリー感
            // 枠内にさりげない Soft3D（ホバー／ON で NeonBox ヨー。レ点 sway は別）
            {
                CRect chip = rcB;
                chip.DeflateRect(1, 1);
                const int tick = (int)(::GetTickCount64() / 50);
                if (m_bIsHot || bC)
                    DrawSoftJkThumb(&dc, chip, tick, TRUE, m_bIsHot ? 12.f : 4.f);
                else
                    DrawSoftJkChip(&dc, chip, tick / 4, FALSE);
            }
            // 箱の下に透けレース + 黒の細レースでランジェリー風の色気
            if (rcB.bottom + 5 < rh)
            {
                DrawLaceLine(&dc, rcB.left + 1, rcB.bottom + 2, rcB.right - 1, rcB.bottom + 2, RGB(60, 40, 55));
                DrawLaceScallop(&dc, rcB.left, rcB.bottom + 4, rcB.right, 3, COLOR_LACE);
            }
            // 先にテキストを描く(チェック✓はこの上に乗せる)。
            // 文字高さは箱(最大18)ではなくコントロール全体を使い、意図したフォントが縮小されないようにする。
            CString t;
            GetWindowText(t);
            if (!t.IsEmpty())
            {
                CRect rt(rcB.right + 8, 0, rw, rh);
                // キャプション帯の「メインに追従」は白文字（透過時の暗色だと黒帯に溶ける）
                const BOOL bCapLock = (GetDlgCtrlID() == IDC_MAINWIN_LOCK)
                    && (CCC_GetCustomCaptionHeight(::GetParent(m_hWnd)) > 0);
                if (bCapLock) {
                    dc.Draw3dRect(&rcB, RGB(0, 0, 0), RGB(0, 0, 0));
                    const COLORREF fill = bD ? RGB(180, 180, 180) : RGB(255, 255, 255);
                    CCC_DrawTextBlackEdge(dc, t, rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, fill);
                }
                else {
                    DrawSmartText2(&dc, rt, t, DT_LEFT | DT_VCENTER | DT_NOPREFIX, bD, FALSE);
                }
            }
            if (bC)
            {
                // 枠から豪快にはみ出してOK(テキストの上に乗る)。クライアント内にはクランプ。
                CRect rk = rcB;
                rk.InflateRect(s / 4, s / 4);
                rk.OffsetRect(2, 0);
                // チェックON時のぷるんバウンス
                if (m_nBounce > 0)
                {
                    // 半周期の sin でレ点を最大 +20%。クライアント内にクランプ済み。
                    const double bf = sin(3.14159265 * (8 - m_nBounce) / 8.0);
                    rk.InflateRect((int)(rk.Width() * 0.20 * bf), (int)(rk.Height() * 0.20 * bf));
                }
                // 自分のクライアント矩形内に収める(隣のコントロールにはみ出さない=切れ防止)
                if (rk.left < 0)     rk.left = 0;
                if (rk.top < 0)      rk.top = 0;
                if (rk.right > rw)   rk.right = rw;
                if (rk.bottom > rh)  rk.bottom = rh;

                DrawCheckMark(&dc, rk, bD ? CCC_Desaturate(COLOR_CHECK, 62) : COLOR_CHECK, max(3, s / 4),
                    m_bIsHot ? sinf((float)::GetTickCount64() * 0.004f) * 8.f : 0.f);
                DrawSparkle(&dc, rk.left + 2, rk.top + 1, 2, COLOR_SPARKLE);
                if (rk.top >= 6)
                {
                    int bL = max(0, rcB.right - 10);
                    const float ang = m_bIsHot ? sinf((float)::GetTickCount64() * 0.0035f) * 12.f : 0.f;
                    DrawLooseRibbon(&dc, CRect(bL, rcB.top - 6, min(rw, bL + 14), rcB.top + 4), COLOR_BOW, ang);
                }
            }
        }
        if (GetFocus() == this)
        {
            CRect rf(0, 0, rw, rh);
            if (!m_bIsFlatStyle) rf.left += 20; else rf.DeflateRect(3, 3);
            dc.DrawFocusRect(&rf);
        }
        CCC_DrawInwoman(&dc, CRect(0, 0, rw, rh), CCC_UseTransPaint(m_hWnd, m_bAeroMode));
    });
}

// ============================================================================
// CCustomLevelMeter
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomLevelMeter, CStatic)

BEGIN_MESSAGE_MAP(CCustomLevelMeter, CStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
END_MESSAGE_MAP()

// CCustomLevelMeter のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。タイマーは PreSubclass/OnCreate で張る。
CCustomLevelMeter::CCustomLevelMeter()
	: m_bAutoDelete(FALSE), m_level(0), m_bAeroMode(FALSE)
{
}

// CCustomLevelMeter の破棄。所有 GDI は無い（空）。
CCustomLevelMeter::~CCustomLevelMeter()
{
}

// HWND 破棄後。基底の PostNcDestroy のあと m_bAutoDelete なら this を delete。
// サブクラス解除後。new したコントロールはここで自殺する。
void CCustomLevelMeter::PostNcDestroy()
{
	CStatic::PostNcDestroy();
	if (m_bAutoDelete) delete this;
}

// 0..1000。変化時のみ Invalidate(FALSE)。
// 変化なしなら描かない。
void CCustomLevelMeter::SetLevel(int n)
{
	if (n < 0) n = 0;
	if (n > 1000) n = 1000;
	if (n == m_level) return;
	m_level = n;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

// 縦レベル。0..1000。ホスト不透明が要るときは α=255 塗り。
// 850 超で赤、650 超で黄。トラックは暗い紺。
void CCustomLevelMeter::PaintClient(CDC& dc)
{
	CRect r;
	GetClientRect(&r);
	if (r.Width() <= 0 || r.Height() <= 0) return;
#if CCUSTOM_AERO_SUPPORT
	const BOOL needOpaque = CCC_HostNeedsChildOpaque(m_hWnd);
#else
	const BOOL needOpaque = FALSE;
#endif
	const BOOL bTrans = !needOpaque && CCC_UseTransPaint(m_hWnd, m_bAeroMode);
	if (needOpaque)
		CCC_FillRectOpaqueBits(dc.GetSafeHdc(), r, COLOR_DIALOG_BG);
	else
		dc.FillSolidRect(&r, bTrans ? CCC_AERO_CHROMA_KEY : COLOR_DIALOG_BG);

	CRect track = r;
	track.DeflateRect(2, 2);
	if (needOpaque)
		CCC_FillRectOpaqueBits(dc.GetSafeHdc(), track, RGB(40, 44, 56));
	else
		dc.FillSolidRect(&track, RGB(40, 44, 56));
	const int h = track.Height();
	int fillH = (int)(((__int64)h * m_level) / 1000);
	if (m_level > 0 && fillH < 1) fillH = 1;
	if (fillH > h) fillH = h;
	if (fillH > 0) {
		CRect fill(track.left, track.bottom - fillH, track.right, track.bottom);
		COLORREF c = RGB(80, 220, 120);
		if (m_level > 850) c = RGB(255, 70, 70);
		else if (m_level > 650) c = RGB(255, 200, 60);
		if (needOpaque)
			CCC_FillRectOpaqueBits(dc.GetSafeHdc(), fill, c);
		else
			dc.FillSolidRect(&fill, c);
	}
}

// WM_PAINT。透過はクロマ、ホストガラスは不透明パス、それ以外は素描画。
// CPaintDC。経路分岐は aero / ホストガラス / 通常。
void CCustomLevelMeter::OnPaint()
{
	CPaintDC dc(this);
	PaintClient(dc);
}

// 消去握りつぶし。アクリル上で ERASE だけだと完全透過のまま残る。
// TRUE で既定塗りを止める。
BOOL CCustomLevelMeter::OnEraseBkgnd(CDC* pDC)
{
	UNREFERENCED_PARAMETER(pDC);
	return TRUE;
}

// WM_PRINTCLIENT。OpaqueFixer の BufferedPaint 先へ同じ内容を描く。
// fixer の BufferedPaint 先。戻り 0。
LRESULT CCustomLevelMeter::OnPrintClient(WPARAM wParam, LPARAM)
{
	if (HDC hDC = (HDC)wParam) {
		CDC* pDC = CDC::FromHandle(hDC);
		if (pDC) PaintClient(*pDC);
		return 1;
	}
	return 0;
}

// ============================================================================
// CCustomProgressCtrl
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomProgressCtrl, CWnd)

BEGIN_MESSAGE_MAP(CCustomProgressCtrl, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
END_MESSAGE_MAP()

namespace {
// 進捗バー用の線形補間。t は 0..1 にクランプ。
// peach→mid→lilac の横方向ミックスに使う。
COLORREF ProgLerp(COLORREF a, COLORREF b, double t)
{
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	return RGB(
		(int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t + 0.5),
		(int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t + 0.5),
		(int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t + 0.5));
}

// 各チャネルを add だけ明るく（255 クランプ）。
// キャンディグラデのハイライト帯に使う。
COLORREF ProgLighten(COLORREF c, int add)
{
	return RGB(
		(std::min)(255, GetRValue(c) + add),
		(std::min)(255, GetGValue(c) + add),
		(std::min)(255, GetBValue(c) + add));
}

// 各チャネルを sub だけ暗く（0 クランプ）。
// バー下縁の陰影に使う。
COLORREF ProgDarken(COLORREF c, int sub)
{
	return RGB(
		(std::max)(0, GetRValue(c) - sub),
		(std::max)(0, GetGValue(c) - sub),
		(std::max)(0, GetBValue(c) - sub));
}
} // namespace

// CCustomProgressCtrl のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。タイマーは PreSubclass/OnCreate で張る。
CCustomProgressCtrl::CCustomProgressCtrl()
	: m_bAutoDelete(FALSE)
	, m_nMin(0), m_nMax(100), m_nPos(0)
	, m_bShowPercent(TRUE)
	, m_bAeroMode(FALSE)
	, m_clrTrack(RGB(255, 236, 246))
	, m_clrFill0(RGB(255, 170, 200))
	, m_clrFill1(RGB(200, 120, 220))
{
	m_brBackground.CreateSolidBrush(COLOR_DIALOG_BG);
}

// CCustomProgressCtrl の破棄。背景ブラシ・％フォント・クロマキャッシュを解放。
CCustomProgressCtrl::~CCustomProgressCtrl()
{
	if (m_brBackground.GetSafeHandle())
		m_brBackground.DeleteObject();
	if (m_fontPct.GetSafeHandle())
		m_fontPct.DeleteObject();
#if CCUSTOM_AERO_SUPPORT
	m_chromaCache.Release();
#endif
}

// 子ウィンドウ生成。CS_HREDRAW|VREDRAW は部分更新で重ね描きしやすいので付けない。
// WS_CHILD。背景は自前。
BOOL CCustomProgressCtrl::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID)
{
	// CS_HREDRAW|VREDRAW は部分更新時に重ね描きしやすいので外す
	CString cls = AfxRegisterWndClass(CS_DBLCLKS,
		::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_BTNFACE + 1), NULL);
	return CWnd::Create(cls, _T(""), dwStyle | WS_CHILD, rect, pParentWnd, nID);
}

// HWND 破棄後。基底の PostNcDestroy のあと m_bAutoDelete なら this を delete。
// サブクラス解除後。new したコントロールはここで自殺する。
void CCustomProgressCtrl::PostNcDestroy()
{
	CWnd::PostNcDestroy();
	if (m_bAutoDelete) delete this;
}

// 進捗範囲。上下逆転は入れ替え、pos をクランプ。
// pos も範囲内へ。
void CCustomProgressCtrl::SetRange(int nLower, int nUpper)
{
	if (nUpper < nLower) { int t = nLower; nLower = nUpper; nUpper = t; }
	m_nMin = nLower;
	m_nMax = nUpper;
	if (m_nPos < m_nMin) m_nPos = m_nMin;
	if (m_nPos > m_nMax) m_nPos = m_nMax;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

// 進捗 min/max の読み出し。
// コピーアウト。
void CCustomProgressCtrl::GetRange(int& nLower, int& nUpper) const
{
	nLower = m_nMin;
	nUpper = m_nMax;
}

// 進捗位置。UpdateWindow は PeekMessage と競合してちらつくので Invalidate のみ。
// 同期 UpdateWindow はしない。
int CCustomProgressCtrl::SetPos(int nPos)
{
	const int old = m_nPos;
	if (nPos < m_nMin) nPos = m_nMin;
	if (nPos > m_nMax) nPos = m_nMax;
	if (nPos != m_nPos) {
		m_nPos = nPos;
		// UpdateWindow は呼び出し側の PeekMessage と競合してちらつくので Invalidate のみ
		if (GetSafeHwnd()) {
			Invalidate(FALSE);
#if CCUSTOM_AERO_SUPPORT
			CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
		}
	}
	return old;
}

// トラック／フィル両端色。
// 次回 Paint で反映。
void CCustomProgressCtrl::SetColors(COLORREF track, COLORREF fillStart, COLORREF fillEnd)
{
	m_clrTrack = track;
	m_clrFill0 = fillStart;
	m_clrFill1 = fillEnd;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

// アクリル透過オプトイン。親の隙間も Invalidate。
// 親の隙間再描画も忘れない。
void CCustomProgressCtrl::SetAeroMode(BOOL b)
{
	m_bAeroMode = b;
	if (GetSafeHwnd()) {
#if CCUSTOM_AERO_SUPPORT
		CCC_SetChildTransparent(m_hWnd, FALSE);
		CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
		Invalidate(FALSE);
	}
}

// キャンディバー。丸角トラック＋ 3 色グラデ（peach→pink→lilac）＋斜め縞。
// 上 1/3 を明るく、下 1/3 を落とす縦陰影。先端にハイライト玉。
// ホバー時のみ Soft3D ジェム。％は縁取り白文字（グラデ上でも読む）。
// 透過時はドロップシャドウを描かない（半透明塗りが穴になる）。
// 隠し淫女は最後に短く重ねるだけ。
void CCustomProgressCtrl::DrawProgressLayer(CDC& dc, const CRect& r, BOOL bAeroTrans)
{
	if (r.Width() <= 0 || r.Height() <= 0) return;

	CRect track = r;
	track.DeflateRect(2, 3);
	if (track.Width() < 4 || track.Height() < 4) {
		CCC_DrawInwoman(&dc, r, bAeroTrans);
		return;
	}
	const int rr = (std::max)(4, track.Height() / 2);

	// うっすら影（透過時は半透明塗りを避けて枠の下だけ薄く）
	if (!bAeroTrans)
	{
		CRect sh = track;
		sh.OffsetRect(0, 1);
		CBrush brSh(RGB(230, 200, 214));
		CPen penNull(PS_NULL, 0, RGB(0, 0, 0));
		CPen* op = dc.SelectObject(&penNull);
		CBrush* ob = dc.SelectObject(&brSh);
		dc.RoundRect(&sh, CPoint(rr, rr));
		dc.SelectObject(op);
		dc.SelectObject(ob);
	}

	CPen penEdge(PS_SOLID, 1, RGB(232, 170, 198));
	CBrush brTrack(m_clrTrack);
	CPen* oldPen = dc.SelectObject(&penEdge);
	CBrush* oldBr = dc.SelectObject(&brTrack);
	dc.RoundRect(&track, CPoint(rr, rr));

	const int span = (std::max)(1, m_nMax - m_nMin);
	const double ratio = (double)(m_nPos - m_nMin) / (double)span;
	const int pct = (int)(ratio * 100.0 + 0.5);
	int fillW = (int)(track.Width() * ratio + 0.5);
	if (m_nPos > m_nMin && fillW < (std::min)(track.Width(), track.Height()))
		fillW = (std::min)(track.Width(), track.Height());
	if (fillW > track.Width()) fillW = track.Width();

	if (fillW > 0) {
		CRect fill = track;
		fill.right = fill.left + fillW;

		CRgn clip;
		clip.CreateRoundRectRgn(track.left, track.top, track.right + 1, track.bottom + 1, rr * 2, rr * 2);
		const int oldClip = dc.SelectClipRgn(&clip);

	// 列ごとに 3 帯。縞は 7px 周期。クリップは丸角リージョン。
		// 3色キャンディグラデ (peach → pink → lilac) + 縦方向の陰影 + 斜め縞
		const COLORREF midCol = ProgLerp(m_clrFill0, m_clrFill1, 0.45);
		const COLORREF peach = ProgLighten(m_clrFill0, 18);
		const int h = fill.Height();
		const int yTop = fill.top;
		const int yMid1 = fill.top + (std::max)(1, h / 3);
		const int yMid2 = fill.top + (std::max)(2, h * 2 / 3);
		const int yBot = fill.bottom;
		for (int x = fill.left; x < fill.right; ++x) {
			const double tx = (fill.Width() <= 1) ? 1.0
				: (double)(x - fill.left) / (double)(fill.Width() - 1);
			COLORREF base;
			if (tx < 0.5)
				base = ProgLerp(peach, midCol, tx * 2.0);
			else
				base = ProgLerp(midCol, m_clrFill1, (tx - 0.5) * 2.0);
			if ((((x - fill.left) + (fill.top)) / 7) & 1)
				base = ProgLighten(base, 12);
			dc.FillSolidRect(x, yTop, 1, yMid1 - yTop, ProgLighten(base, 28));
			dc.FillSolidRect(x, yMid1, 1, yMid2 - yMid1, base);
			dc.FillSolidRect(x, yMid2, 1, yBot - yMid2, ProgDarken(base, 22));
		}

		// 大きめツヤ（上半分の楕円ハイライト）
		{
			CRect gloss = fill;
			gloss.DeflateRect(2, 1);
			gloss.bottom = gloss.top + (std::max)(3, gloss.Height() * 2 / 5);
			CPen penNull(PS_NULL, 0, RGB(0, 0, 0));
			CPen* op = dc.SelectObject(&penNull);
			CBrush brGloss2(RGB(255, 230, 242));
			CBrush* ob = dc.SelectObject(&brGloss2);
			dc.Ellipse(&gloss);
			dc.SelectObject(op);
			dc.SelectObject(ob);
		}

		// 先端のキャンディ玉
		if (fillW >= track.Height()) {
			const int rad = track.Height() / 2;
			const int cx = fill.right - rad;
			const int cy = track.top + rad;
			CRect orb(cx - rad + 1, cy - rad + 1, cx + rad, cy + rad);
			CBrush brOrb(ProgLighten(m_clrFill1, 25));
			CPen penOrb(PS_SOLID, 1, ProgDarken(m_clrFill1, 20));
			CPen* op = dc.SelectObject(&penOrb);
			CBrush* ob = dc.SelectObject(&brOrb);
			dc.Ellipse(&orb);
			CRect hi = orb;
			hi.DeflateRect(rad / 2, rad / 2);
			hi.OffsetRect(-1, -1);
			CBrush brHi(RGB(255, 255, 255));
			dc.SelectObject(&brHi);
			dc.SelectStockObject(NULL_PEN);
			if (hi.Width() > 1 && hi.Height() > 1)
				dc.Ellipse(&hi);
			dc.SelectObject(op);
			dc.SelectObject(ob);
		}

		(void)oldClip;
		dc.SelectClipRgn(NULL);

		// 枠を描き直し
		dc.SelectObject(&penEdge);
		dc.SelectStockObject(NULL_BRUSH);
		dc.RoundRect(&track, CPoint(rr, rr));
		// 内側の白い縁取り
		CRect inner = track;
		inner.DeflateRect(1, 1);
		CPen penIn(PS_SOLID, 1, RGB(255, 245, 250));
		dc.SelectObject(&penIn);
		dc.RoundRect(&inner, CPoint((std::max)(2, rr - 1), (std::max)(2, rr - 1)));

		// ホバー時のみ Soft3D 先端ジェム（クリップ解除後）
		if (fillW >= track.Height()) {
			const int rad = track.Height() / 2;
			const int cx = fill.right - rad;
			const int cy = track.top + rad;
			POINT pt = {};
			::GetCursorPos(&pt);
			::ScreenToClient(m_hWnd, &pt);
			if (track.PtInRect(pt))
				DrawSoftJkThumb(&dc, CRect(cx - 8, cy - 8, cx + 8, cy + 8),
					(int)(::GetTickCount64() / 60), TRUE, 6.f);
		}
	}

	if (m_bShowPercent) {
		CString s;
		s.Format(_T("%d%%"), pct);
		const int fh = (std::max)(9, track.Height() - 5);
		LOGFONT lf = {};
		lf.lfHeight = -fh;
		lf.lfWeight = FW_BOLD;
		lf.lfQuality = ANTIALIASED_QUALITY;
		lf.lfCharSet = DEFAULT_CHARSET;
		_tcsncpy_s(lf.lfFaceName, _T("Segoe UI"), _TRUNCATE);
		if (m_fontPct.GetSafeHandle())
			m_fontPct.DeleteObject();
		m_fontPct.CreateFontIndirect(&lf);

		CFont* oldFont = dc.SelectObject(&m_fontPct);
		dc.SetBkMode(TRANSPARENT);
		CRect textRc = track;
		textRc.DeflateRect(4, 0);
		// はみ出し防止: クリップ
		CRgn textClip;
		textClip.CreateRoundRectRgn(track.left, track.top, track.right + 1, track.bottom + 1, rr * 2, rr * 2);
		dc.SelectClipRgn(&textClip);
		const UINT dt = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
		// 薄い縁取り → 白文字 (グラデ上でも読める)
		dc.SetTextColor(RGB(170, 80, 120));
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (dx == 0 && dy == 0) continue;
				CRect o = textRc;
				o.OffsetRect(dx, dy);
				dc.DrawText(s, &o, dt);
			}
		}
		dc.SetTextColor(RGB(255, 255, 255));
		dc.DrawText(s, &textRc, dt);
		dc.SelectClipRgn(NULL);
		dc.SelectObject(oldFont);
	}

	dc.SelectObject(oldPen);
	dc.SelectObject(oldBr);

	CCC_DrawInwoman(&dc, r, bAeroTrans);
}

// クライアント全体へ DrawProgressLayer。矩形版へ委譲。
// CPaintDC を持たない内部再描画からも使う。
void CCustomProgressCtrl::PaintClient(CDC& dc)
{
	CRect r;
	GetClientRect(&r);
	PaintClient(dc, r);
}

// メモリ DC にキャンディバーを描き、透過ならクロマ、通常は BitBlt。
// 隠し淫女は DrawProgressLayer 末尾。親アクリルの穴を開けない。
void CCustomProgressCtrl::PaintClient(CDC& dc, const CRect& r)
{
	const int rw = r.Width();
	const int rh = r.Height();
	if (rw <= 0 || rh <= 0) return;

	CDC mDC;
	if (!mDC.CreateCompatibleDC(&dc)) {
		DrawProgressLayer(dc, r, FALSE);
		return;
	}
	CBitmap bmp;
	if (!bmp.CreateCompatibleBitmap(&dc, rw, rh)) {
		mDC.DeleteDC();
		DrawProgressLayer(dc, r, FALSE);
		return;
	}
	CBitmap* ob = mDC.SelectObject(&bmp);
	CRect local(0, 0, rw, rh);

#if CCUSTOM_AERO_SUPPORT
	const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
#else
	const BOOL bTrans = FALSE;
#endif
	if (bTrans)
	{
#if CCUSTOM_AERO_SUPPORT
		mDC.FillSolidRect(&local, CCC_AERO_CHROMA_KEY);
		DrawProgressLayer(mDC, local, TRUE);
		if (CCC_IsAeroEnabled() && CCC_IsWin11())
			CCC_BlitChromaCached(dc.GetSafeHdc(), r.left, r.top, rw, rh,
				mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY, m_chromaCache);
		else
			CCC_BlitChromaTrans(dc.GetSafeHdc(), r.left, r.top, rw, rh,
				mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
#endif
	}
	else
	{
		mDC.FillSolidRect(&local, COLOR_DIALOG_BG);
		DrawProgressLayer(mDC, local, FALSE);
		dc.BitBlt(r.left, r.top, rw, rh, &mDC, 0, 0, SRCCOPY);
	}

	mDC.SelectObject(ob);
	mDC.DeleteDC();
}

// ガラス上は α=255 必須。BufferedPaint + DrawProgressLayer。
// 失敗時もメモリ DC 経由で不透明強制（素 BitBlt は消える）。
void CCustomProgressCtrl::PaintOpaqueClient(CDC& dc)
{
	CRect r;
	GetClientRect(&r);
	if (r.Width() <= 0 || r.Height() <= 0) return;
	BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
	params.dwFlags = BPPF_ERASE;
	HDC hdcBuf = NULL;
	HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
	if (!hdcBuf || !hBP) {
		// フォールバックも必ずメモリ経由（不透明強制）
		CDC mem;
		CBitmap bmp;
		mem.CreateCompatibleDC(&dc);
		bmp.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
		CBitmap* old = mem.SelectObject(&bmp);
		mem.FillSolidRect(0, 0, r.Width(), r.Height(), COLOR_DIALOG_BG);
		DrawProgressLayer(mem, CRect(0, 0, r.Width(), r.Height()), FALSE);
		dc.BitBlt(0, 0, r.Width(), r.Height(), &mem, 0, 0, SRCCOPY);
		mem.SelectObject(old);
		return;
	}
	CDC mem;
	mem.Attach(hdcBuf);
	mem.FillSolidRect(&r, COLOR_DIALOG_BG);
	DrawProgressLayer(mem, r, FALSE);
	mem.Detach();
	::BufferedPaintMakeOpaque(hBP, &r);
	::EndBufferedPaint(hBP, TRUE);
}

// fixer / PRINTCLIENT 用。不透明バッファへレイヤを描く。
// PRINTCLIENT / fixer 共用。
void CCustomProgressCtrl::PaintOpaqueIntoBuffer(HDC hdcBuf)
{
	if (!hdcBuf || !m_hWnd) return;
	CRect r;
	GetClientRect(&r);
	CDC mem;
	mem.Attach(hdcBuf);
	mem.FillSolidRect(&r, COLOR_DIALOG_BG);
	DrawProgressLayer(mem, r, FALSE);
	mem.Detach();
}

// WM_PAINT。透過はクロマ、ホストガラスは不透明パス、それ以外は素描画。
// CPaintDC。経路分岐は aero / ホストガラス / 通常。
void CCustomProgressCtrl::OnPaint()
{
	CPaintDC dc(this);
	CRect r;
	GetClientRect(&r);
#if CCUSTOM_AERO_SUPPORT
	if (CCC_HostNeedsChildOpaque(m_hWnd))
	{
		PaintOpaqueClient(dc);
		return;
	}
#endif
	if (r.Width() <= 0 || r.Height() <= 0) return;
	// ポップアップ子など素 BitBlt が消える環境向け
	BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
	params.dwFlags = BPPF_ERASE;
	HDC hdcBuf = NULL;
	HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
	if (hdcBuf && hBP) {
		CDC dcBuf;
		dcBuf.Attach(hdcBuf);
		PaintClient(dcBuf);
		dcBuf.Detach();
		::BufferedPaintMakeOpaque(hBP, &r);
		::EndBufferedPaint(hBP, TRUE);
		return;
	}
	PaintClient(dc);
}

// 消去握りつぶし。アクリル上で ERASE だけだと完全透過のまま残る。
// TRUE で既定塗りを止める。
BOOL CCustomProgressCtrl::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
	if (CCC_HostNeedsChildOpaque(m_hWnd) && pDC)
	{
		CRect r;
		GetClientRect(&r);
		CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), r, COLOR_DIALOG_BG);
		return TRUE;
	}
	if (CCC_UseTransPaint(m_hWnd, m_bAeroMode)) return TRUE;
#endif
	if (pDC)
	{
		CRect r;
		GetClientRect(&r);
		pDC->FillSolidRect(&r, COLOR_DIALOG_BG);
	}
	return TRUE;
}

// WM_PRINTCLIENT。OpaqueFixer の BufferedPaint 先へ同じ内容を描く。
// fixer の BufferedPaint 先。戻り 0。
LRESULT CCustomProgressCtrl::OnPrintClient(WPARAM wParam, LPARAM)
{
	CDC* pDC = CDC::FromHandle((HDC)wParam);
	if (!pDC) return 0;
#if CCUSTOM_AERO_SUPPORT
	if (CCC_HostNeedsChildOpaque(m_hWnd))
	{
		PaintOpaqueClient(*pDC);
		return 1;
	}
#endif
	PaintClient(*pDC);
	return 0;
}

// ============================================================================
// CCustomSysPerfCtrl
// ============================================================================
namespace {
enum {
	kSysPerfTimerId = 0x53504631, // 'SPF1'
	kCmdViewAll = 0x7101,
	kCmdViewMem,
	kCmdViewCpuOverall,
	kCmdViewCpuGrid,
	kCmdViewCpuBoth,
	kCmdPause,
	kCmdCopy,
	kCmdColsAuto,
	kCmdCols4,
	kCmdCols6,
	kCmdCols8
};

struct SysProcPerfInfo {
	LARGE_INTEGER IdleTime;
	LARGE_INTEGER KernelTime;
	LARGE_INTEGER UserTime;
	LARGE_INTEGER Reserved1[2];
	ULONG Reserved2;
};

typedef LONG(WINAPI* PFN_NtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);

// FILETIME を 64bit に。GetSystemTimes 差分用。
// 失敗時は何もしない／呼び出し側でフォールバック。
static ULONGLONG FtToU64(const FILETIME& ft)
{
	ULARGE_INTEGER u;
	u.LowPart = ft.dwLowDateTime;
	u.HighPart = ft.dwHighDateTime;
	return u.QuadPart;
}
} // namespace

IMPLEMENT_DYNAMIC(CCustomSysPerfCtrl, CWnd)

BEGIN_MESSAGE_MAP(CCustomSysPerfCtrl, CWnd)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
	ON_WM_TIMER()
	ON_WM_RBUTTONUP()
	ON_WM_CONTEXTMENU()
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnToolTipNotify)
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnToolTipNotify)
END_MESSAGE_MAP()

CCustomSysPerfCtrl::CCustomSysPerfCtrl()
	: m_bAutoDelete(FALSE)
	, m_bAeroMode(FALSE)
	, m_bPaused(FALSE)
	, m_bSmbiosDone(FALSE)
	, m_viewMode(VIEW_ALL)
	, m_gridCols(0)
	, m_coreCount(0)
	, m_histCount(0)
	, m_histPos(0)
	, m_overallNow(0)
	, m_bHaveTimes(FALSE)
	, m_bHaveCore(FALSE)
	, m_memInUse(0)
	, m_memCompressed(0)
	, m_memAvail(0)
	, m_memCommit(0)
	, m_memCommitLimit(0)
	, m_memCached(0)
	, m_memPaged(0)
	, m_memNonPaged(0)
	, m_memHwReserved(0)
	, m_memSpeedMTs(0)
	, m_memSlotsUsed(0)
	, m_memSlotsTotal(0)
	, m_memFormFactor(0)
	, m_bHaveCompressed(FALSE)
{
	ZeroMemory(m_overallHist, sizeof(m_overallHist));
	ZeroMemory(m_coreHist, sizeof(m_coreHist));
	ZeroMemory(m_coreNow, sizeof(m_coreNow));
	ZeroMemory(&m_ftIdlePrev, sizeof(m_ftIdlePrev));
	ZeroMemory(&m_ftKerPrev, sizeof(m_ftKerPrev));
	ZeroMemory(&m_ftUsrPrev, sizeof(m_ftUsrPrev));
	ZeroMemory(m_coreIdlePrev, sizeof(m_coreIdlePrev));
	ZeroMemory(m_coreKerPrev, sizeof(m_coreKerPrev));
	ZeroMemory(m_coreUsrPrev, sizeof(m_coreUsrPrev));
	SYSTEM_INFO si = {};
	GetSystemInfo(&si);
	m_coreCount = (int)si.dwNumberOfProcessors;
	if (m_coreCount < 1) m_coreCount = 1;
	if (m_coreCount > kMaxCores) m_coreCount = kMaxCores;
}

// CCustomSysPerfCtrl の破棄。フォントは OnDestroy。ここは空。
CCustomSysPerfCtrl::~CCustomSysPerfCtrl()
{
}

// 子ウィンドウ生成。CS_HREDRAW|VREDRAW は部分更新で重ね描きしやすいので付けない。
// WS_CHILD。背景は自前。
BOOL CCustomSysPerfCtrl::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID)
{
	CString cls = AfxRegisterWndClass(CS_DBLCLKS,
		::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_BTNFACE + 1), NULL);
	return CWnd::Create(cls, _T(""), dwStyle | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, rect, pParentWnd, nID);
}

// HWND 破棄後。基底の PostNcDestroy のあと m_bAutoDelete なら this を delete。
// サブクラス解除後。new したコントロールはここで自殺する。
void CCustomSysPerfCtrl::PostNcDestroy()
{
	CWnd::PostNcDestroy();
	if (m_bAutoDelete) delete this;
}

// コントロール DPI。スケール計算の基準。
// Per-Monitor v2。
UINT CCustomSysPerfCtrl::Dpi() const
{
	return CCC_GetControlDpi(m_hWnd);
}

// 96dpi 基準 px を現在 DPI へ。
// 96dpi ピクセルを拡大。
int CCustomSysPerfCtrl::S(int v) const
{
	return CCC_ScaleDpi(v, Dpi());
}

// アクリル透過オプトイン。親の隙間も Invalidate。
// 親の隙間再描画も忘れない。
void CCustomSysPerfCtrl::SetAeroMode(BOOL b)
{
	m_bAeroMode = b;
	if (GetSafeHwnd()) {
#if CCUSTOM_AERO_SUPPORT
		CCC_SetChildTransparent(m_hWnd, FALSE);
		CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
		Invalidate(FALSE);
	}
}

// CPU/メモリ表示モード。範囲外はクランプ。
// 変化時のみ Invalidate。
void CCustomSysPerfCtrl::SetViewMode(int mode)
{
	if (mode < VIEW_ALL) mode = VIEW_ALL;
	if (mode > VIEW_CPU_BOTH) mode = VIEW_CPU_BOTH;
	if (m_viewMode == mode) return;
	m_viewMode = mode;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

// フォント・ツールチップ・初回 SMBIOS/CPU サンプル、1 秒タイマー。
// 1 秒サンプリング開始。
int CCustomSysPerfCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	LOGFONT lf = {};
	lf.lfHeight = -S(11);
	lf.lfWeight = FW_NORMAL;
	lf.lfCharSet = DEFAULT_CHARSET;
	_tcscpy_s(lf.lfFaceName, _T("Segoe UI"));
	m_fontLabel.CreateFontIndirect(&lf);
	lf.lfHeight = -S(16);
	lf.lfWeight = FW_BOLD;
	m_fontValue.CreateFontIndirect(&lf);

	m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX);
	m_tip.AddTool(this, LPSTR_TEXTCALLBACK);
	m_tip.SetMaxTipWidth(S(320));
	m_tip.Activate(TRUE);
	EnableToolTips(TRUE);

	SampleSmbiosOnce();
	SampleOnce();
	SetTimer(kSysPerfTimerId, 1000, NULL);
	return 0;
}

// キャプション／追従エントリ解除、fixer 破棄。
// KillTimer とフォント破棄。
void CCustomSysPerfCtrl::OnDestroy()
{
	KillTimer(kSysPerfTimerId);
	if (m_tip.GetSafeHwnd())
		m_tip.DestroyWindow();
	if (m_fontLabel.GetSafeHandle())
		m_fontLabel.DeleteObject();
	if (m_fontValue.GetSafeHandle())
		m_fontValue.DeleteObject();
	CWnd::OnDestroy();
}

// ツールチップ Relay。ダイアログ側は F1 ヘルプ。隠し演出入力は短く見るだけ。
// ツールチップ Relay。F1 はヘルプ。隠し演出は短く見るだけ。
BOOL CCustomSysPerfCtrl::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd())
		m_tip.RelayEvent(pMsg);
	return CWnd::PreTranslateMessage(pMsg);
}

// CPU％と利用可能メモリの短いチップ。
// Unicode/ANSI 両対応。
BOOL CCustomSysPerfCtrl::OnToolTipNotify(UINT, NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!pNMHDR || !pResult) return FALSE;
	CString tip;
	tip.Format(LL14(
		L"CPU %u%% / 利用可能メモリ ",
		L"CPU %u%% / Available memory ",
		L"CPU %u%% / Memoire disponible ",
		L"CPU %u%% / Memoria disponibile ",
		L"CPU %u%% / Memoria disponible ",
		L"CPU %u%% / 사용 가능 메모리 ",
		L"CPU %u%% / 可用内存 ",
		L"CPU %u%% / الذاكرة المتاحة ",
		L"CPU %u%% / Доступная память ",
		L"CPU %u%% / Verfugbarer Speicher ",
		L"CPU %u%% / Memoria disponivel ",
		L"CPU %u%% / Beschikbaar geheugen ",
		L"CPU %u%% / Dostepna pamiec ",
		L"CPU %u%% / Kullanilabilir bellek "),
		(UINT)m_overallNow);
	CString avail;
	FormatBytesGB(m_memAvail, avail);
	tip += avail;
	m_tipText = tip;

	if (pNMHDR->code == TTN_NEEDTEXTW) {
		TOOLTIPTEXTW* pttt = (TOOLTIPTEXTW*)pNMHDR;
		pttt->lpszText = (LPWSTR)(LPCWSTR)m_tipText;
	} else if (pNMHDR->code == TTN_NEEDTEXTA) {
		TOOLTIPTEXTA* pttt = (TOOLTIPTEXTA*)pNMHDR;
		static char tipA[512];
		WideCharToMultiByte(CP_ACP, 0, m_tipText, -1, tipA, 512, NULL, NULL);
		pttt->lpszText = tipA;
	}
	*pResult = 0;
	return TRUE;
}

// 1 秒サンプリング。ポーズ中は SampleOnce しない。
// 親アクリルの隙間も Invalidate。
void CCustomSysPerfCtrl::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != kSysPerfTimerId) {
		CWnd::OnTimer(nIDEvent);
		return;
	}
	if (!m_bPaused) {
		SampleOnce();
		Invalidate(FALSE);
#if CCUSTOM_AERO_SUPPORT
		CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
	}
}

// バイトを GB/MB 表示。10GB 未満も 1 桁。
// 1GB 未満は MB。
void CCustomSysPerfCtrl::FormatBytesGB(ULONGLONG bytes, CString& out)
{
	const double gb = (double)bytes / (1024.0 * 1024.0 * 1024.0);
	if (gb >= 10.0)
		out.Format(_T("%.1f GB"), gb);
	else if (gb >= 1.0)
		out.Format(_T("%.1f GB"), gb);
	else {
		const double mb = (double)bytes / (1024.0 * 1024.0);
		out.Format(_T("%.0f MB"), mb);
	}
}

// SMBIOS を一度だけ読む（GetSystemFirmwareTable 'RSMB'）。
// Type16=スロット数、Type17=実装 DIMM。size=0/FFFF は空。
// 速度は Type17 の MT/s 最大、フォームファクタは最後の実装スロット。
// 128KB 超や長さ壊れは捨てる。失敗しても m_bSmbiosDone は立てる。
void CCustomSysPerfCtrl::SampleSmbiosOnce()
{
	if (m_bSmbiosDone) return;
	m_bSmbiosDone = TRUE;
	m_memSpeedMTs = 0;
	m_memSlotsUsed = 0;
	m_memSlotsTotal = 0;
	m_memFormFactor = 0;

	// Raw SMBIOS。Type16 NumberOfSlots、Type17 Size/Speed。空スロットは size=0。
	const DWORD sig = 'RSMB';
	const DWORD need = GetSystemFirmwareTable(sig, 0, NULL, 0);
	if (need == 0 || need > 128 * 1024)
		return;
	static BYTE raw[128 * 1024];
	if (GetSystemFirmwareTable(sig, 0, raw, need) != need)
		return;
	if (need < 8) return;
	const DWORD tableLen = *(DWORD*)(raw + 4);
	if (tableLen == 0 || 8 + tableLen > need) return;
	const BYTE* p = raw + 8;
	const BYTE* end = raw + 8 + tableLen;
	UINT maxSpeed = 0;
	UINT form = 0;
	UINT used = 0;
	UINT totalSlots = 0;
	while (p + 4 <= end) {
		const BYTE type = p[0];
		const BYTE len = p[1];
		if (len < 4) break;
		if (type == 16 && len >= 0x0F) {
			totalSlots = p[0x0D] | (p[0x0E] << 8);
		} else if (type == 17 && len >= 0x17) {
			const WORD sizeRaw = (WORD)(p[0x0C] | (p[0x0D] << 8));
			if (sizeRaw != 0 && sizeRaw != 0xFFFF) {
				used++;
				if (len > 0x12)
					form = p[0x0E];
				if (len >= 0x17) {
					const WORD spd = (WORD)(p[0x15] | (p[0x16] << 8));
					if (spd > maxSpeed) maxSpeed = spd;
				}
			} else if (sizeRaw == 0) {
				// empty slot counted via Type16
			}
		}
		const BYTE* q = p + len;
		while (q + 1 < end && !(q[0] == 0 && q[1] == 0))
			q++;
		if (q + 1 >= end) break;
		p = q + 2;
	}
	m_memSpeedMTs = maxSpeed;
	m_memSlotsUsed = used;
	m_memSlotsTotal = totalSlots ? totalSlots : used;
	m_memFormFactor = form;
}

// 1 秒サンプリング。全体 CPU は GetSystemTimes の差分（idle/kernel/user）。
// kernel には idle が含まれるので busy= (ker+usr) - idle。
// コア別は NtQuerySystemInformation(class=8)。メモリは GlobalMemoryStatusEx
// ＋ GetPerformanceInfo。圧縮は Memory Compression プロセスの WS。
// 初回は差分が取れないのでヒストリを進めない。
void CCustomSysPerfCtrl::SampleOnce()
{
	BOOL wroteSample = FALSE;

	// --- overall CPU ---
	FILETIME idle = {}, ker = {}, usr = {};
	// FILETIME 差分。ker に idle が含まれるので busy=(ker+usr)-idle。
	if (GetSystemTimes(&idle, &ker, &usr)) {
		if (m_bHaveTimes) {
			const ULONGLONG di = FtToU64(idle) - FtToU64(m_ftIdlePrev);
			const ULONGLONG dk = FtToU64(ker) - FtToU64(m_ftKerPrev);
			const ULONGLONG du = FtToU64(usr) - FtToU64(m_ftUsrPrev);
			const ULONGLONG tot = dk + du;
			BYTE pct = 0;
			if (tot > 0) {
				ULONGLONG busy = tot - di;
				if (busy > tot) busy = tot;
				pct = (BYTE)((busy * 100ull) / tot);
				if (pct > 100) pct = 100;
			}
			m_overallNow = pct;
			m_overallHist[m_histPos] = pct;
			wroteSample = TRUE;
		}
		m_ftIdlePrev = idle;
		m_ftKerPrev = ker;
		m_ftUsrPrev = usr;
		m_bHaveTimes = TRUE;
	}

	// --- per-core ---
	static PFN_NtQuerySystemInformation pNtQ = NULL;
	if (!pNtQ) {
		HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		if (ntdll)
			pNtQ = (PFN_NtQuerySystemInformation)GetProcAddress(ntdll, "NtQuerySystemInformation");
	}
	if (pNtQ) {
		SysProcPerfInfo info[kMaxCores];
		ULONG retLen = 0;
		const ULONG classId = 8; // SystemProcessorPerformanceInformation
		LONG st = pNtQ(classId, info, sizeof(SysProcPerfInfo) * (ULONG)m_coreCount, &retLen);
		if (st >= 0) {
			const int n = (int)(retLen / sizeof(SysProcPerfInfo));
			int cores = n;
			if (cores > m_coreCount) cores = m_coreCount;
			if (cores > kMaxCores) cores = kMaxCores;
			if (cores > 0) m_coreCount = cores;
			for (int i = 0; i < m_coreCount; ++i) {
				const ULONGLONG idleC = (ULONGLONG)info[i].IdleTime.QuadPart;
				const ULONGLONG kerC = (ULONGLONG)info[i].KernelTime.QuadPart;
				const ULONGLONG usrC = (ULONGLONG)info[i].UserTime.QuadPart;
				if (m_bHaveCore) {
					const ULONGLONG di = idleC - m_coreIdlePrev[i];
					const ULONGLONG dk = kerC - m_coreKerPrev[i];
					const ULONGLONG du = usrC - m_coreUsrPrev[i];
					const ULONGLONG tot = dk + du;
					BYTE pct = 0;
					if (tot > 0) {
						ULONGLONG busy = tot - di;
						if (busy > tot) busy = tot;
						pct = (BYTE)((busy * 100ull) / tot);
						if (pct > 100) pct = 100;
					}
					m_coreNow[i] = pct;
					m_coreHist[i][m_histPos] = pct;
				}
				m_coreIdlePrev[i] = idleC;
				m_coreKerPrev[i] = kerC;
				m_coreUsrPrev[i] = usrC;
			}
			if (m_bHaveCore)
				wroteSample = TRUE;
			m_bHaveCore = TRUE;
		}
	}

	if (wroteSample) {
		m_histPos = (m_histPos + 1) % kHistLen;
		if (m_histCount < kHistLen) m_histCount++;
	}

	// --- memory ---
	MEMORYSTATUSEX ms = {};
	ms.dwLength = sizeof(ms);
	if (GlobalMemoryStatusEx(&ms)) {
		m_memAvail = ms.ullAvailPhys;
		m_memInUse = (ms.ullTotalPhys > ms.ullAvailPhys) ? (ms.ullTotalPhys - ms.ullAvailPhys) : 0;
		ULONGLONG installedKB = 0;
		if (GetPhysicallyInstalledSystemMemory(&installedKB)) {
			const ULONGLONG installed = installedKB * 1024ull;
			if (installed > ms.ullTotalPhys)
				m_memHwReserved = installed - ms.ullTotalPhys;
			else
				m_memHwReserved = 0;
		}
	}

	PERFORMANCE_INFORMATION pi = {};
	pi.cb = sizeof(pi);
	if (GetPerformanceInfo(&pi, sizeof(pi))) {
		const SIZE_T pg = pi.PageSize ? pi.PageSize : 4096;
		m_memCommit = (ULONGLONG)pi.CommitTotal * pg;
		m_memCommitLimit = (ULONGLONG)pi.CommitLimit * pg;
		m_memCached = (ULONGLONG)pi.SystemCache * pg;
		m_memPaged = (ULONGLONG)pi.KernelPaged * pg;
		m_memNonPaged = (ULONGLONG)pi.KernelNonpaged * pg;
	}

	// 圧縮メモリ概算: Memory Compression プロセスの WS
	m_bHaveCompressed = FALSE;
	m_memCompressed = 0;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32W pe = {};
		pe.dwSize = sizeof(pe);
		if (Process32FirstW(snap, &pe)) {
			do {
				if (_wcsicmp(pe.szExeFile, L"Memory Compression") == 0) {
					HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
					if (hp) {
						PROCESS_MEMORY_COUNTERS pmc = {};
						pmc.cb = sizeof(pmc);
						if (GetProcessMemoryInfo(hp, &pmc, sizeof(pmc))) {
							m_memCompressed = (ULONGLONG)pmc.WorkingSetSize;
							m_bHaveCompressed = TRUE;
						}
						CloseHandle(hp);
					}
					break;
				}
			} while (Process32NextW(snap, &pe));
		}
		CloseHandle(snap);
	}
}

// メモリ／全体 CPU／コア格子の縦配分。メモリ行が潰れない比率。
// VIEW_* で列の有無が変わる。
void CCustomSysPerfCtrl::LayoutRects(const CRect& r, CRect& rcMem, CRect& rcOverall, CRect& rcGrid)
{
	rcMem.SetRectEmpty();
	rcOverall.SetRectEmpty();
	rcGrid.SetRectEmpty();
	const int pad = S(2);
	CRect body = r;
	body.DeflateRect(pad, pad);
	if (body.Width() <= 0 || body.Height() <= 0) return;

	const BOOL showMem = (m_viewMode == VIEW_ALL || m_viewMode == VIEW_MEM);
	const BOOL showOv = (m_viewMode == VIEW_ALL || m_viewMode == VIEW_CPU_OVERALL || m_viewMode == VIEW_CPU_BOTH);
	const BOOL showGr = (m_viewMode == VIEW_ALL || m_viewMode == VIEW_CPU_GRID || m_viewMode == VIEW_CPU_BOTH);

	int parts = 0;
	int wMem = 0, wOv = 0, wGr = 0;
	// メモリ行(4段)が潰れないよう比率を確保
	if (showMem) { parts += 3; wMem = 3; }
	if (showOv) { parts += 3; wOv = 3; }
	if (showGr) { parts += 5; wGr = 5; }
	if (parts <= 0) return;

	const int H = body.Height();
	int y = body.top;
	if (showMem) {
		int h = (H * wMem) / parts;
		if (m_viewMode == VIEW_MEM) h = H;
		const int minMem = S(26) * 4; // 4行×最低高さ
		if (m_viewMode == VIEW_ALL && h < minMem && minMem < H * 4 / 10)
			h = minMem;
		rcMem.SetRect(body.left, y, body.right, y + h);
		y += h;
	}
	if (showOv) {
		int h = (H * wOv) / parts;
		if (m_viewMode == VIEW_CPU_OVERALL) h = H;
		if (y + h > body.bottom && !showGr) h = body.bottom - y;
		rcOverall.SetRect(body.left, y, body.right, y + h);
		y += h;
	}
	if (showGr) {
		rcGrid.SetRect(body.left, y, body.right, body.bottom);
	}
}

// スパークライン（履歴リング）。透過時は線のみ。
// リングバッファを折れ線に。
void CCustomSysPerfCtrl::DrawSpark(CDC& dc, const CRect& rc, const BYTE* hist, int histCount, BOOL bAeroTrans)
{
	if (rc.Width() < 4 || rc.Height() < 4 || !hist) return;
	const COLORREF grid = bAeroTrans ? RGB(90, 90, 90) : RGB(210, 210, 210);
	const COLORREF line = RGB(17, 125, 212);
	const COLORREF fill = RGB(120, 180, 230);

	CPen penGrid(PS_SOLID, 1, grid);
	CPen* oldPen = dc.SelectObject(&penGrid);
	for (int i = 1; i < 4; ++i) {
		const int y = rc.top + (rc.Height() * i) / 4;
		dc.MoveTo(rc.left, y);
		dc.LineTo(rc.right, y);
	}
	for (int i = 1; i < 6; ++i) {
		const int x = rc.left + (rc.Width() * i) / 6;
		dc.MoveTo(x, rc.top);
		dc.LineTo(x, rc.bottom);
	}

	// 常に kHistLen 幅で描く。未蓄積分は 0（左）／新しい値は右端。
	// 旧: histCount 点を全幅に引き伸ばすため、たまるまでスクロールに見えなかった。
	POINT pts[kHistLen + 2];
	const int n = kHistLen;
	const int valid = (histCount > kHistLen) ? kHistLen : ((histCount > 0) ? histCount : 0);
	for (int i = 0; i < n; ++i) {
		BYTE v = 0;
		const int ageFromNewest = n - 1 - i; // 0=最新
		if (ageFromNewest < valid) {
			int idx = m_histPos - 1 - ageFromNewest;
			while (idx < 0) idx += kHistLen;
			idx %= kHistLen;
			v = hist[idx];
		}
		const int x = rc.left + (i * (rc.Width() - 1)) / (n > 1 ? (n - 1) : 1);
		const int y = rc.bottom - 1 - (v * (rc.Height() - 1)) / 100;
		pts[i].x = x;
		pts[i].y = y;
	}
	{
		POINT poly[kHistLen + 2];
		for (int i = 0; i < n; ++i) poly[i] = pts[i];
		poly[n].x = pts[n - 1].x;
		poly[n].y = rc.bottom - 1;
		poly[n + 1].x = pts[0].x;
		poly[n + 1].y = rc.bottom - 1;
		CBrush br(fill);
		CBrush* oldBr = dc.SelectObject(&br);
		CPen penFill(PS_NULL, 0, fill);
		dc.SelectObject(&penFill);
		dc.SetPolyFillMode(WINDING);
		dc.Polygon(poly, n + 2);
		dc.SelectObject(oldBr);
		CPen penLine(PS_SOLID, S(1) < 1 ? 1 : S(1), line);
		dc.SelectObject(&penLine);
		dc.Polyline(pts, n);
	}
	dc.SelectObject(oldPen);
}

// 実装/使用/圧縮/キャッシュ等の段。SMBIOS 速度があれば併記。
// 透過時はラベルを明るくしてガラス上で読む。
void CCustomSysPerfCtrl::DrawMemory(CDC& dc, const CRect& rc, BOOL bAeroTrans)
{
	if (rc.IsRectEmpty()) return;
	const COLORREF clrLabel = bAeroTrans ? RGB(200, 200, 200) : RGB(80, 80, 80);
	const COLORREF clrValue = bAeroTrans ? RGB(255, 255, 255) : RGB(20, 20, 20);
	CFont* oldFont = dc.SelectObject(&m_fontLabel);
	dc.SetBkMode(TRANSPARENT);

	CString sInUseL = LL14(L"使用中 (圧縮)", L"In use (Compressed)", L"Utilise (compresse)", L"In uso (compressa)", L"En uso (comprimida)", L"사용 중 (압축)", L"正在使用 (已压缩)", L"قيد الاستخدام (مضغوط)", L"Используется (сжат.)", L"In Verwendung (kompr.)", L"Em uso (comprimida)", L"In gebruik (gecomprimeerd)", L"W uzyciu (skompr.)", L"Kullanimda (sikistirilmis)");
	CString sAvailL = LL14(L"利用可能", L"Available", L"Disponible", L"Disponibile", L"Disponible", L"사용 가능", L"可用", L"متاح", L"Доступно", L"Verfugbar", L"Disponivel", L"Beschikbaar", L"Dostepne", L"Kullanilabilir");
	CString sCommitL = LL14(L"コミット済み", L"Committed", L"Valide", L"Assegnato", L"Confirmado", L"커밋됨", L"已提交", L"ملتزم", L"Выделено", L"Festgeschrieben", L"Confirmado", L"Vastgelegd", L"Zadeklarowane", L"Ayrilmis");
	CString sCacheL = LL14(L"キャッシュ済み", L"Cached", L"Cache", L"Cache", L"Cache", L"캐시됨", L"已缓存", L"مخزن مؤقت", L"Кэш", L"Zwischengespeichert", L"Em cache", L"Gecached", L"W pamieci podrecznej", L"Onbellekte");
	CString sPagedL = LL14(L"ページ プール", L"Paged pool", L"Pool page", L"Pool paginato", L"Grupo paginado", L"페이지 풀", L"分页池", L"تجمع الصفحات", L"Выгружаемый пул", L"Seitenpool", L"Pool paginado", L"Wisselpool", L"Stronicowany pul", L"Sayfali havuz");
	CString sNonPagedL = LL14(L"非ページ プール", L"Non-paged pool", L"Pool non page", L"Pool non paginato", L"Grupo no paginado", L"비페이지 풀", L"非分页池", L"تجمع غير صفحات", L"Невыгружаемый пул", L"Nichtseitenpool", L"Pool nao paginado", L"Niet-wisselpool", L"Niestronicowany pul", L"Sayfasiz havuz");
	CString sSpeedL = LL14(L"速度", L"Speed", L"Vitesse", L"Velocita", L"Velocidad", L"속도", L"速度", L"السرعة", L"Скорость", L"Geschwindigkeit", L"Velocidade", L"Snelheid", L"Predkosc", L"Hiz");
	CString sSlotsL = LL14(L"スロットの使用", L"Slots used", L"Emplacements", L"Slot usati", L"Ranuras usadas", L"슬롯 사용", L"已用插槽", L"الفتحات المستخدمة", L"Слоты", L"Steckplatze", L"Slots usados", L"Slots gebruikt", L"Uzyte sloty", L"Kullanilan yuvalar");
	CString sFormL = LL14(L"フォーム ファクター", L"Form factor", L"Facteur de forme", L"Fattore di forma", L"Factor de forma", L"폼 팩터", L"外形规格", L"عامل الشكل", L"Форм-фактор", L"Formfaktor", L"Fator de forma", L"Vormfactor", L"Forma", L"Form faktoru");
	CString sHwL = LL14(L"ハードウェア予約済み", L"Hardware reserved", L"Reserve materiel", L"Riservata hardware", L"Reservado por hardware", L"하드웨어 예약", L"硬件保留", L"محجوز للأجهزة", L"Зарезерв. оборудованием", L"Hardwarereserviert", L"Reservado por hardware", L"Gereserveerd door hardware", L"Zarezerwowane przez sprzet", L"Donanim tarafindan ayrilan");

	CString vInUse, vComp, vAvail, vCommit, vCache, vPaged, vNonPaged, vHw, vSpeed, vSlots, vForm;
	FormatBytesGB(m_memInUse, vInUse);
	if (m_bHaveCompressed) {
		FormatBytesGB(m_memCompressed, vComp);
		vInUse += _T(" (");
		vInUse += vComp;
		vInUse += _T(")");
	}
	FormatBytesGB(m_memAvail, vAvail);
	{
		CString a, b;
		FormatBytesGB(m_memCommit, a);
		FormatBytesGB(m_memCommitLimit, b);
		vCommit.Format(_T("%s/%s"), (LPCTSTR)a, (LPCTSTR)b);
	}
	FormatBytesGB(m_memCached, vCache);
	FormatBytesGB(m_memPaged, vPaged);
	FormatBytesGB(m_memNonPaged, vNonPaged);
	FormatBytesGB(m_memHwReserved, vHw);
	if (m_memSpeedMTs > 0)
		vSpeed.Format(_T("%u MT/s"), m_memSpeedMTs);
	else
		vSpeed = _T("-");
	if (m_memSlotsTotal > 0)
		vSlots.Format(_T("%u/%u"), m_memSlotsUsed, m_memSlotsTotal);
	else if (m_memSlotsUsed > 0)
		vSlots.Format(_T("%u"), m_memSlotsUsed);
	else
		vSlots = _T("-");
	if (m_memFormFactor == 0x09)
		vForm = _T("DIMM");
	else if (m_memFormFactor == 0x0D)
		vForm = _T("SODIMM");
	else if (m_memFormFactor != 0)
		vForm.Format(_T("0x%02X"), m_memFormFactor);
	else
		vForm = _T("-");

	struct Cell { const CString* lab; const CString* val; };
	Cell cells[10];
	cells[0].lab = &sInUseL; cells[0].val = &vInUse;
	cells[1].lab = &sAvailL; cells[1].val = &vAvail;
	cells[2].lab = &sCommitL; cells[2].val = &vCommit;
	cells[3].lab = &sCacheL; cells[3].val = &vCache;
	cells[4].lab = &sPagedL; cells[4].val = &vPaged;
	cells[5].lab = &sNonPagedL; cells[5].val = &vNonPaged;
	cells[6].lab = &sSpeedL; cells[6].val = &vSpeed;
	cells[7].lab = &sSlotsL; cells[7].val = &vSlots;
	cells[8].lab = &sFormL; cells[8].val = &vForm;
	cells[9].lab = &sHwL; cells[9].val = &vHw;

	const int cols = 3;
	const int rows = 4;
	const int cw = rc.Width() / cols;
	const int rh = rc.Height() / rows;
	if (cw < 8 || rh < 12) {
		dc.SelectObject(oldFont);
		return;
	}

	// セル高さからラベル/値の領域とフォントを決める（固定オフセットだと値が潰れる）
	int labelH = rh * 38 / 100;
	int valueH = rh - labelH;
	if (labelH < 10) labelH = 10;
	if (valueH < 12) {
		valueH = (rh > 18) ? 12 : (rh * 55 / 100);
		labelH = rh - valueH;
		if (labelH < 8) labelH = rh / 2;
		valueH = rh - labelH;
	}
	int labelPx = labelH - 1;
	int valuePx = valueH - 1;
	if (labelPx < 8) labelPx = 8;
	if (valuePx < 10) valuePx = 10;
	if (labelPx > labelH) labelPx = labelH;
	if (valuePx > valueH) valuePx = valueH;

	LOGFONT lf = {};
	lf.lfCharSet = DEFAULT_CHARSET;
	_tcscpy_s(lf.lfFaceName, _T("Segoe UI"));
	lf.lfHeight = -labelPx;
	lf.lfWeight = FW_NORMAL;
	CFont fontLab;
	fontLab.CreateFontIndirect(&lf);
	lf.lfHeight = -valuePx;
	lf.lfWeight = FW_BOLD;
	CFont fontVal;
	fontVal.CreateFontIndirect(&lf);

	for (int i = 0; i < 10; ++i) {
		const int col = i % cols;
		const int row = i / cols;
		CRect cell(rc.left + col * cw, rc.top + row * rh, rc.left + (col + 1) * cw, rc.top + (row + 1) * rh);
		cell.DeflateRect(S(2), 0);
		if (cell.Height() < 8) continue;

		CRect labR = cell;
		labR.bottom = labR.top + labelH;
		if (labR.bottom > cell.bottom) labR.bottom = cell.bottom;
		CRect valR = cell;
		valR.top = labR.bottom;
		if (valR.top >= valR.bottom) {
			// 高不足時は同一セル内で値のみ（ラベル省略しないようラベル優先で縮める）
			valR = cell;
			valR.top = cell.top + cell.Height() / 2;
		}

		dc.SelectObject(&fontLab);
		dc.SetTextColor(clrLabel);
		dc.DrawText(*cells[i].lab, labR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		dc.SelectObject(&fontVal);
		dc.SetTextColor(clrValue);
		dc.DrawText(*cells[i].val, valR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	dc.SelectObject(oldFont);
}

// 全体 CPU％とスパーク。
// 全体％＋履歴。
void CCustomSysPerfCtrl::DrawOverallCpu(CDC& dc, const CRect& rc, BOOL bAeroTrans)
{
	if (rc.IsRectEmpty()) return;
	const COLORREF clrLabel = bAeroTrans ? RGB(200, 200, 200) : RGB(80, 80, 80);
	CFont* oldFont = dc.SelectObject(&m_fontLabel);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(clrLabel);
	CString title = LL14(
		L"CPU  60 秒間の使用率 (%)",
		L"CPU  Utilization over 60 seconds (%)",
		L"CPU  Utilisation sur 60 secondes (%)",
		L"CPU  Utilizzo in 60 secondi (%)",
		L"CPU  Uso durante 60 segundos (%)",
		L"CPU  60초간 사용률 (%)",
		L"CPU  60 秒利用率 (%)",
		L"CPU  الاستخدام خلال 60 ثانية (%)",
		L"CPU  Использование за 60 с (%)",
		L"CPU  Auslastung uber 60 Sekunden (%)",
		L"CPU  Utilizacao em 60 segundos (%)",
		L"CPU  Gebruik over 60 seconden (%)",
		L"CPU  Uzycie przez 60 sekund (%)",
		L"CPU  60 saniyelik kullanim (%)");
	CRect hdr = rc;
	hdr.bottom = hdr.top + S(14);
	dc.DrawText(title, hdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	CString pct;
	pct.Format(_T("%u%%"), (UINT)m_overallNow);
	dc.DrawText(pct, hdr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

	CRect plot = rc;
	plot.top = hdr.bottom + S(1);
	if (plot.Height() > 4)
		DrawSpark(dc, plot, m_overallHist, m_histCount, bAeroTrans);
	dc.SelectObject(oldFont);
}

// 論理コア格子。列数は自動またはコンテキスト指定。
// 論理プロセッサ数まで。
void CCustomSysPerfCtrl::DrawCoreGrid(CDC& dc, const CRect& rc, BOOL bAeroTrans)
{
	if (rc.IsRectEmpty() || m_coreCount <= 0) return;
	int cols = m_gridCols;
	if (cols <= 0) {
		const int minCell = S(56);
		cols = rc.Width() / (minCell > 1 ? minCell : 1);
		if (cols < 2) cols = 2;
		if (cols > 12) cols = 12;
	}
	int rows = (m_coreCount + cols - 1) / cols;
	if (rows < 1) rows = 1;
	const int cw = rc.Width() / cols;
	const int rh = rc.Height() / rows;
	const COLORREF border = bAeroTrans ? RGB(70, 70, 70) : RGB(200, 200, 200);
	CPen pen(PS_SOLID, 1, border);
	CPen* oldPen = dc.SelectObject(&pen);
	for (int i = 0; i < m_coreCount; ++i) {
		const int col = i % cols;
		const int row = i / cols;
		CRect cell(rc.left + col * cw, rc.top + row * rh, rc.left + (col + 1) * cw, rc.top + (row + 1) * rh);
		dc.Rectangle(cell);
		cell.DeflateRect(1, 1);
		if (cell.Width() > 2 && cell.Height() > 2)
			DrawSpark(dc, cell, m_coreHist[i], m_histCount, bAeroTrans);
	}
	dc.SelectObject(oldPen);
}

// モードに応じてメモリ/CPU/格子を並べる。
// 空矩形は描かない。
void CCustomSysPerfCtrl::DrawPerfLayer(CDC& dc, const CRect& r, BOOL bAeroTrans)
{
	if (r.Width() <= 0 || r.Height() <= 0) return;
	CRect rcMem, rcOverall, rcGrid;
	LayoutRects(r, rcMem, rcOverall, rcGrid);
	DrawMemory(dc, rcMem, bAeroTrans);
	DrawOverallCpu(dc, rcOverall, bAeroTrans);
	DrawCoreGrid(dc, rcGrid, bAeroTrans);
	// ホバー時のみ Soft アクセント（1s 更新には載せない）
	{
		POINT pt = {};
		::GetCursorPos(&pt);
		::ScreenToClient(m_hWnd, &pt);
		if (r.PtInRect(pt) && !bAeroTrans)
			DrawSoftJkThumb(&dc, CRect(r.right - 18, r.top + 4, r.right - 4, r.top + 18),
				(int)(::GetTickCount64() / 80), TRUE, 5.f);
	}
	CCC_DrawInwoman(&dc, r, bAeroTrans);
}

// クライアント全体へ DrawPerfLayer。矩形版へ委譲。
// ツールチップ更新後の Invalidate からも来る。
void CCustomSysPerfCtrl::PaintClient(CDC& dc)
{
	CRect r;
	GetClientRect(&r);
	PaintClient(dc, r);
}

// メモリ DC に CPU/メモリ層を描き、透過ならクロマ、通常は BitBlt。
// ホストガラスでは不透明経路（PaintOpaqueClient）を OnPaint 側が選ぶ。
void CCustomSysPerfCtrl::PaintClient(CDC& dc, const CRect& r)
{
	const int rw = r.Width();
	const int rh = r.Height();
	if (rw <= 0 || rh <= 0) return;

	CDC mDC;
	if (!mDC.CreateCompatibleDC(&dc)) {
		DrawPerfLayer(dc, r, FALSE);
		return;
	}
	CBitmap bmp;
	if (!bmp.CreateCompatibleBitmap(&dc, rw, rh)) {
		mDC.DeleteDC();
		DrawPerfLayer(dc, r, FALSE);
		return;
	}
	CBitmap* ob = mDC.SelectObject(&bmp);
	CRect local(0, 0, rw, rh);

#if CCUSTOM_AERO_SUPPORT
	const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
#else
	const BOOL bTrans = FALSE;
#endif
	if (bTrans)
	{
#if CCUSTOM_AERO_SUPPORT
		mDC.FillSolidRect(&local, CCC_AERO_CHROMA_KEY);
		DrawPerfLayer(mDC, local, TRUE);
		if (CCC_IsAeroEnabled() && CCC_IsWin11())
			CCC_BlitChromaCached(dc.GetSafeHdc(), r.left, r.top, rw, rh,
				mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY, m_chromaCache);
		else
			CCC_BlitChromaTrans(dc.GetSafeHdc(), r.left, r.top, rw, rh,
				mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
#endif
	}
	else
	{
		mDC.FillSolidRect(&local, COLOR_DIALOG_BG);
		DrawPerfLayer(mDC, local, FALSE);
		dc.BitBlt(r.left, r.top, rw, rh, &mDC, 0, 0, SRCCOPY);
	}

	mDC.SelectObject(ob);
	mDC.DeleteDC();
}

// ガラス上は α=255 必須。BufferedPaint に DrawPerfLayer を載せる。
// 失敗時もメモリ DC 経由（素 BitBlt は消える）。
void CCustomSysPerfCtrl::PaintOpaqueClient(CDC& dc)
{
	CRect r;
	GetClientRect(&r);
	if (r.Width() <= 0 || r.Height() <= 0) return;
	BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
	params.dwFlags = BPPF_ERASE;
	HDC hdcBuf = NULL;
	HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
	if (!hdcBuf || !hBP) {
		CDC mem;
		CBitmap bmp;
		mem.CreateCompatibleDC(&dc);
		bmp.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
		CBitmap* old = mem.SelectObject(&bmp);
		mem.FillSolidRect(0, 0, r.Width(), r.Height(), COLOR_DIALOG_BG);
		DrawPerfLayer(mem, CRect(0, 0, r.Width(), r.Height()), FALSE);
		dc.BitBlt(0, 0, r.Width(), r.Height(), &mem, 0, 0, SRCCOPY);
		mem.SelectObject(old);
		return;
	}
	CDC mem;
	mem.Attach(hdcBuf);
	mem.FillSolidRect(&r, COLOR_DIALOG_BG);
	DrawPerfLayer(mem, r, FALSE);
	mem.Detach();
	::BufferedPaintMakeOpaque(hBP, &r);
	::EndBufferedPaint(hBP, TRUE);
}

// fixer / PRINTCLIENT 用。不透明バッファへレイヤを描く。
// PRINTCLIENT / fixer 共用。
void CCustomSysPerfCtrl::PaintOpaqueIntoBuffer(HDC hdcBuf)
{
	if (!hdcBuf || !m_hWnd) return;
	CRect r;
	GetClientRect(&r);
	CDC mem;
	mem.Attach(hdcBuf);
	mem.FillSolidRect(&r, COLOR_DIALOG_BG);
	DrawPerfLayer(mem, r, FALSE);
	mem.Detach();
}

// WM_PAINT。透過はクロマ、ホストガラスは不透明パス、それ以外は素描画。
// CPaintDC。経路分岐は aero / ホストガラス / 通常。
void CCustomSysPerfCtrl::OnPaint()
{
	CPaintDC dc(this);
#if CCUSTOM_AERO_SUPPORT
	if (CCC_HostNeedsChildOpaque(m_hWnd))
	{
		PaintOpaqueClient(dc);
		return;
	}
#endif
	PaintClient(dc);
}

// 消去握りつぶし。アクリル上で ERASE だけだと完全透過のまま残る。
// TRUE で既定塗りを止める。
BOOL CCustomSysPerfCtrl::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
	if (CCC_HostNeedsChildOpaque(m_hWnd) && pDC)
	{
		CRect r;
		GetClientRect(&r);
		CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), r, COLOR_DIALOG_BG);
		return TRUE;
	}
	if (CCC_UseTransPaint(m_hWnd, m_bAeroMode)) return TRUE;
#endif
	if (pDC)
	{
		CRect r;
		GetClientRect(&r);
		pDC->FillSolidRect(&r, COLOR_DIALOG_BG);
	}
	return TRUE;
}

// WM_PRINTCLIENT。OpaqueFixer の BufferedPaint 先へ同じ内容を描く。
// fixer の BufferedPaint 先。戻り 0。
LRESULT CCustomSysPerfCtrl::OnPrintClient(WPARAM wParam, LPARAM)
{
	CDC* pDC = CDC::FromHandle((HDC)wParam);
	if (!pDC) return 0;
#if CCUSTOM_AERO_SUPPORT
	if (CCC_HostNeedsChildOpaque(m_hWnd))
	{
		PaintOpaqueClient(*pDC);
		return 1;
	}
#endif
	PaintClient(*pDC);
	return 0;
}

// 現状の数値をテキストでクリップボードへ。
// CF_UNICODETEXT。
void CCustomSysPerfCtrl::CopyStatsToClipboard()
{
	CString a, b, c, d, e, f, g;
	FormatBytesGB(m_memInUse, a);
	FormatBytesGB(m_memAvail, b);
	FormatBytesGB(m_memCommit, c);
	FormatBytesGB(m_memCommitLimit, d);
	FormatBytesGB(m_memCached, e);
	FormatBytesGB(m_memPaged, f);
	FormatBytesGB(m_memNonPaged, g);
	CString s;
	s.Format(_T("CPU %u%%\r\nInUse %s\r\nAvail %s\r\nCommit %s/%s\r\nCache %s\r\nPaged %s\r\nNonPaged %s\r\nSpeed %u MT/s\r\nSlots %u/%u\r\n"),
		(UINT)m_overallNow, (LPCTSTR)a, (LPCTSTR)b, (LPCTSTR)c, (LPCTSTR)d, (LPCTSTR)e, (LPCTSTR)f, (LPCTSTR)g,
		m_memSpeedMTs, m_memSlotsUsed, m_memSlotsTotal);
	if (OpenClipboard()) {
		EmptyClipboard();
		const SIZE_T bytes = ((SIZE_T)s.GetLength() + 1) * sizeof(WCHAR);
		HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
		if (h) {
			void* p = GlobalLock(h);
			if (p) {
				memcpy(p, (LPCWSTR)s, bytes);
				GlobalUnlock(h);
				SetClipboardData(CF_UNICODETEXT, h);
			} else {
				GlobalFree(h);
			}
		}
		CloseClipboard();
	}
}

// 表示モード・列数・ポーズ・コピー。
// 画面座標。
void CCustomSysPerfCtrl::ShowCtxMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	{
		CCustomPopupMenu* viewSub = menu.AddSubMenu(
			LL14(L"表示モード", L"View mode", L"Mode d'affichage", L"Modalita di visualizzazione", L"Modo de visualizacion", L"표시 모드", L"显示模式", L"وضع العرض", L"Режим отображения", L"Anzeigemodus", L"Modo de exibicao", L"Weergavemodus", L"Tryb wyswietlania", L"Goruntuleme modu"),
			LL14(L"メモリ／CPU全体／グリッドなどの表示切替。", L"Switch memory / overall CPU / grid display modes.", L"Basculer memoire / CPU global / grille.", L"Alterna memoria / CPU totale / griglia.", L"Alternar memoria / CPU total / cuadrícula.", L"메모리/CPU 전체/그리드 표시 전환.", L"切换内存/总体 CPU/网格等显示。", L"تبديل عرض الذاكرة / المعالج الكلي / الشبكة.", L"Переключать память / общий CPU / сетку.", L"Speicher / Gesamt-CPU / Raster umschalten.", L"Alternar memoria / CPU geral / grade.", L"Schakel geheugen / totale CPU / raster om.", L"Przelacz pamiec / CPU ogolne / siatke.", L"Bellek / genel CPU / izgara gorunumunu degistir."));
		if (viewSub) {
			viewSub->AddCheck(kCmdViewAll,
				LL14(L"すべて表示", L"Show all", L"Tout afficher", L"Mostra tutto", L"Mostrar todo", L"모두 표시", L"全部显示", L"عرض الكل", L"Показать все", L"Alles anzeigen", L"Mostrar tudo", L"Alles tonen", L"Pokaz wszystko", L"Tumunu goster"),
				m_viewMode == VIEW_ALL,
				LL14(L"メモリ＋CPU全体＋コアグリッドをまとめて表示", L"Show memory + overall CPU + per-core grid together",
					L"Afficher memoire + CPU global + grille", L"Mostra memoria + CPU totale + griglia",
					L"Mostrar memoria + CPU total + cuadrícula", L"메모리+CPU 전체+코어 그리드를 함께 표시",
					L"同时显示内存+CPU总体+核心网格", L"عرض الذاكرة + المعالج الكلي + الشبكة",
					L"Показать память + общий CPU + сетку ядер", L"Speicher + Gesamt-CPU + Kernraster zusammen",
					L"Mostrar memoria + CPU geral + grade", L"Geheugen + totale CPU + kernraster tonen",
					L"Pokaz pamiec + CPU ogolne + siatke rdzeni", L"Bellek + genel CPU + cekirdek ızgarasini goster"));
			viewSub->AddCheck(kCmdViewMem,
				LL14(L"メモリのみ", L"Memory only", L"Memoire seule", L"Solo memoria", L"Solo memoria", L"메모리만", L"仅内存", L"الذاكرة فقط", L"Только память", L"Nur Speicher", L"Somente memoria", L"Alleen geheugen", L"Tylko pamiec", L"Yalniz bellek"),
				m_viewMode == VIEW_MEM,
				LL14(L"メモリ使用量グラフだけを表示（CPUは非表示）", L"Show only the memory graph (hide CPU)",
					L"Afficher seulement le graphe memoire (masquer CPU)", L"Solo grafico memoria (nascondi CPU)",
					L"Solo grafico de memoria (ocultar CPU)", L"메모리 그래프만 표시(CPU 숨김)",
					L"仅显示内存图（隐藏 CPU）", L"عرض رسم الذاكرة فقط (إخفاء المعالج)",
					L"Только график памяти (скрыть CPU)", L"Nur Speicherdiagramm (CPU aus)",
					L"Somente grafico de memoria (ocultar CPU)", L"Alleen geheugengrafiek (CPU verbergen)",
					L"Tylko wykres pamieci (ukryj CPU)", L"Yalniz bellek grafigi (CPU gizle)"));
			viewSub->AddCheck(kCmdViewCpuOverall,
				LL14(L"CPU 全体のみ", L"CPU overall only", L"CPU global seul", L"Solo CPU totale", L"Solo CPU total", L"CPU 전체만", L"仅 CPU 总体", L"وحدة المعالجة فقط", L"Только CPU общий", L"Nur CPU gesamt", L"Somente CPU geral", L"Alleen CPU totaal", L"Tylko CPU ogolne", L"Yalniz CPU genel"),
				m_viewMode == VIEW_CPU_OVERALL,
				LL14(L"全体CPU使用率グラフだけを表示", L"Show only the overall CPU usage graph",
					L"Afficher seulement le graphe CPU global", L"Solo grafico CPU totale",
					L"Solo grafico CPU total", L"전체 CPU 사용률 그래프만 표시",
					L"仅显示总体 CPU 使用率图", L"عرض رسم استخدام المعالج الكلي فقط",
					L"Только график общей загрузки CPU", L"Nur Gesamt-CPU-Diagramm",
					L"Somente grafico CPU geral", L"Alleen totale CPU-grafiek",
					L"Tylko wykres ogolnego CPU", L"Yalniz genel CPU kullanim grafigi"));
			viewSub->AddCheck(kCmdViewCpuGrid,
				LL14(L"CPU グリッドのみ", L"CPU grid only", L"Grille CPU seule", L"Solo griglia CPU", L"Solo cuadrícula CPU", L"CPU 그리드만", L"仅 CPU 网格", L"شبكة المعالج فقط", L"Только сетка CPU", L"Nur CPU-Raster", L"Somente grade CPU", L"Alleen CPU-raster", L"Tylko siatka CPU", L"Yalniz CPU izgarasi"),
				m_viewMode == VIEW_CPU_GRID,
				LL14(L"コア別CPUグリッドだけを表示", L"Show only the per-core CPU grid",
					L"Afficher seulement la grille CPU par coeur", L"Solo griglia CPU per core",
					L"Solo cuadrícula CPU por nucleo", L"코어별 CPU 그리드만 표시",
					L"仅显示每核 CPU 网格", L"عرض شبكة المعالج لكل نواة فقط",
					L"Только сетка CPU по ядрам", L"Nur per-Kern-CPU-Raster",
					L"Somente grade CPU por nucleo", L"Alleen CPU-raster per kern",
					L"Tylko siatka CPU per rdzen", L"Yalniz cekirdek basina CPU ızgarasi"));
			viewSub->AddCheck(kCmdViewCpuBoth,
				LL14(L"CPU のみ (全体+グリッド)", L"CPU only (overall+grid)", L"CPU seul (global+grille)", L"Solo CPU (totale+griglia)", L"Solo CPU (total+cuadrícula)", L"CPU만 (전체+그리드)", L"仅 CPU（总体+网格）", L"المعالج فقط (كلي+شبكة)", L"Только CPU (общий+сетка)", L"Nur CPU (gesamt+Raster)", L"Somente CPU (geral+grade)", L"Alleen CPU (totaal+raster)", L"Tylko CPU (ogolne+siatka)", L"Yalniz CPU (genel+izgara)"),
				m_viewMode == VIEW_CPU_BOTH,
				LL14(L"CPU全体＋コアグリッドのみ（メモリは非表示）", L"Overall CPU + per-core grid only (hide memory)",
					L"CPU global + grille seulement (masquer memoire)", L"Solo CPU totale + griglia (nascondi memoria)",
					L"Solo CPU total + cuadrícula (ocultar memoria)", L"CPU 전체+코어 그리드만(메모리 숨김)",
					L"仅总体 CPU+核心网格（隐藏内存）", L"المعالج الكلي + الشبكة فقط (إخفاء الذاكرة)",
					L"Только общий CPU + сетка (скрыть память)", L"Nur Gesamt-CPU + Raster (Speicher aus)",
					L"Somente CPU geral + grade (ocultar memoria)", L"Alleen totale CPU + raster (geheugen uit)",
					L"Tylko CPU ogolne + siatka (ukryj pamiec)", L"Yalniz genel CPU + izgara (bellek gizle)"));
		}
	}
	menu.AddSeparator();
	menu.AddCheck(kCmdPause,
		m_bPaused
		? LL14(L"更新を再開", L"Resume updates", L"Reprendre", L"Riprendi", L"Reanudar", L"업데이트 재개", L"恢复更新", L"استئناف التحديث", L"Возобновить", L"Fortsetzen", L"Retomar", L"Hervatten", L"Wznow", L"Devam et")
		: LL14(L"更新を一時停止", L"Pause updates", L"Pause", L"Pausa", L"Pausar", L"업데이트 일시정지", L"暂停更新", L"إيقاف مؤقت", L"Пауза", L"Pausieren", L"Pausar", L"Pauzeren", L"Wstrzymaj", L"Duraklat"),
		m_bPaused,
		LL14(L"モニタ表示のライブ更新を一時停止／再開", L"Pause or resume live monitor updates",
			L"Mettre en pause / reprendre les maj live", L"Pausa o riprendi aggiornamenti live",
			L"Pausar o reanudar actualizaciones en vivo", L"모니터 실시간 갱신 일시정지/재개",
			L"暂停/恢复监视器实时更新", L"إيقاف أو استئناف تحديثات المراقبة",
			L"Пауза / возобновление живого обновления", L"Live-Updates pausieren / fortsetzen",
			L"Pausar ou retomar atualizacoes ao vivo", L"Live-updates pauzeren / hervatten",
			L"Wstrzymaj lub wznow aktualizacje na zywo", L"Canli monitor guncellemelerini duraklat/devam et"));
	menu.AddCommand(kCmdCopy,
		LL14(L"統計をコピー", L"Copy statistics", L"Copier les stats", L"Copia statistiche", L"Copiar estadisticas", L"통계 복사", L"复制统计", L"نسخ الإحصاءات", L"Копировать статистику", L"Statistik kopieren", L"Copiar estatisticas", L"Statistieken kopieren", L"Kopiuj statystyki", L"Istatistikleri kopyala"),
		LL14(L"現在のCPU／メモリ統計をクリップボードへコピー", L"Copy current CPU/memory stats to the clipboard",
			L"Copier les stats CPU/memoire dans le presse-papiers", L"Copia stats CPU/memoria negli appunti",
			L"Copiar stats CPU/memoria al portapapeles", L"현재 CPU/메모리 통계를 클립보드에 복사",
			L"将当前 CPU/内存统计复制到剪贴板", L"نسخ إحصاءات المعالج/الذاكرة إلى الحافظة",
			L"Копировать статистику CPU/памяти в буфер", L"CPU-/Speicherstatistik in Zwischenablage",
			L"Copiar stats CPU/memoria para a area de transferencia", L"CPU-/geheugenstatistieken naar klembord",
			L"Kopiuj statystyki CPU/pamieci do schowka", L"Guncel CPU/bellek istatistiklerini panoya kopyala"));
	menu.AddSeparator();
	{
		CCustomPopupMenu* cols = menu.AddSubMenu(
			LL14(L"グリッド列", L"Grid columns", L"Colonnes", L"Colonne", L"Columnas", L"그리드 열", L"网格列", L"أعمدة الشبكة", L"Столбцы", L"Rasterspalten", L"Colunas", L"Rasterkolommen", L"Kolumny", L"Izgara sutun"),
			LL14(L"CPUグリッドの列数", L"Column count for the CPU grid", L"Nombre de colonnes de la grille CPU", L"Numero di colonne griglia CPU", L"Numero de columnas de la cuadrícula CPU", L"CPU 그리드 열 수", L"CPU 网格列数", L"عدد أعمدة شبكة المعالج", L"Число столбцов сетки CPU", L"Spaltenanzahl des CPU-Rasters", L"Numero de colunas da grade CPU", L"Aantal kolommen CPU-raster", L"Liczba kolumn siatki CPU", L"CPU izgara sutun sayisi"));
		if (cols) {
			cols->AddCheck(kCmdColsAuto,
				LL14(L"自動", L"Auto", L"Auto", L"Auto", L"Auto", L"자동", L"自动", L"تلقائي", L"Авто", L"Auto", L"Auto", L"Auto", L"Auto", L"Otomatik"),
				m_gridCols == 0);
			cols->AddCheck(kCmdCols4, _T("4"), m_gridCols == 4);
			cols->AddCheck(kCmdCols6, _T("6"), m_gridCols == 6);
			cols->AddCheck(kCmdCols8, _T("8"), m_gridCols == 8);
		}
	}

	const UINT cmd = menu.Track(screenPt, this);
	switch (cmd) {
	case kCmdViewAll: SetViewMode(VIEW_ALL); break;
	case kCmdViewMem: SetViewMode(VIEW_MEM); break;
	case kCmdViewCpuOverall: SetViewMode(VIEW_CPU_OVERALL); break;
	case kCmdViewCpuGrid: SetViewMode(VIEW_CPU_GRID); break;
	case kCmdViewCpuBoth: SetViewMode(VIEW_CPU_BOTH); break;
	case kCmdPause: m_bPaused = !m_bPaused; break;
	case kCmdCopy: CopyStatsToClipboard(); break;
	case kCmdColsAuto: m_gridCols = 0; Invalidate(FALSE); break;
	case kCmdCols4: m_gridCols = 4; Invalidate(FALSE); break;
	case kCmdCols6: m_gridCols = 6; Invalidate(FALSE); break;
	case kCmdCols8: m_gridCols = 8; Invalidate(FALSE); break;
	default: break;
	}
}

// 右クリックで表示モード／列数／ポーズ／コピー。
// クライアント座標をスクリーンへ。
void CCustomSysPerfCtrl::OnRButtonUp(UINT nFlags, CPoint point)
{
	UNREFERENCED_PARAMETER(nFlags);
	CPoint pt = point;
	ClientToScreen(&pt);
	ShowCtxMenu(pt);
}

// キーボードメニューキーからも同じメニュー。
// (-1,-1) ならコントロール内に出す。
void CCustomSysPerfCtrl::OnContextMenu(CWnd* pWnd, CPoint point)
{
	UNREFERENCED_PARAMETER(pWnd);
	if (point.x == -1 && point.y == -1) {
		CRect r;
		GetWindowRect(&r);
		point.x = r.left + S(8);
		point.y = r.top + S(8);
	}
	ShowCtxMenu(point);
}

// ============================================================================
// CCustomGroupBox
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomGroupBox, CButton)

BEGIN_MESSAGE_MAP(CCustomGroupBox, CButton)
    ON_WM_PAINT()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
    ON_WM_ERASEBKGND()
    ON_WM_TIMER()
END_MESSAGE_MAP()

// CCustomGroupBox のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。タイマーは PreSubclass/OnCreate で張る。
CCustomGroupBox::CCustomGroupBox() : m_bAutoDelete(FALSE), m_bAeroMode(FALSE) {}
// CCustomGroupBox の破棄。所有 GDI は無い（空）。
CCustomGroupBox::~CCustomGroupBox() {}

// HWND 破棄後。基底の PostNcDestroy のあと m_bAutoDelete なら this を delete。
// サブクラス解除後。new したコントロールはここで自殺する。
void CCustomGroupBox::PostNcDestroy()
{
    CButton::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

// グループは兄弟の下に回り、兄弟領域へ描かない（WS_CLIPSIBLINGS）。
// Soft3D ゆらゆら常時タイマーはピアノ等と競合するため張らず Kill する。
void CCustomGroupBox::PreSubclassWindow()
{
    CButton::PreSubclassWindow();
    // グループは兄弟(Edit/Static)の下に回り、かつ兄弟領域へ描画しない
    ModifyStyle(0, WS_CLIPSIBLINGS);
    // Soft3D ゆらゆら常時タイマーはピアノ等と競合するため張らない
    KillTimer(kGroupSoftTimerId);
}

// 旧 Soft3D ゆらゆら（kGroupSoftTimerId）。可視なら Invalidate。
// 常時タイマーは PreSubclass で Kill 済み。残骸 ID だけ処理。
void CCustomGroupBox::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kGroupSoftTimerId) {
        if (!IsWindowVisible()) return;
        Invalidate(FALSE);
        return;
    }
    CButton::OnTimer(nIDEvent);
}
// WM_PAINT。透過はクロマ、ホストガラスは不透明パス、それ以外は素描画。
// CPaintDC。経路分岐は aero / ホストガラス / 通常。
void CCustomGroupBox::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
    DrawGroupBox(&dc, r);
}

// WM_PRINTCLIENT。OpaqueFixer の BufferedPaint 先へ同じ内容を描く。
// fixer の BufferedPaint 先。戻り 0。
LRESULT CCustomGroupBox::OnPrintClient(WPARAM wParam, LPARAM)
{
    if (HDC hDC = (HDC)wParam)
    {
        CDC* pDC = CDC::FromHandle(hDC);
        CRect r;
        GetClientRect(&r);
        DrawGroupBox(pDC, r);
    }
    return 0;
}

// 内側全面塗りは兄弟コントロールを消す。消去は親/子に任せる。
// TRUE を返して既定の背景塗りを止める。
BOOL CCustomGroupBox::OnEraseBkgnd(CDC* pDC)
{
    // 内側全面塗りは兄弟コントロールを消す。消去は親/子に任せる。
    UNREFERENCED_PARAMETER(pDC);
    return TRUE;
}

// グループボックス矩形と重なる兄弟をクリップ除外(内側塗りつぶし防止)
// グループ矩形と重なる可視兄弟をクリップ除外（内側塗りつぶし防止）。
// z 順に依存せず、重なる兄弟はすべて除外する。
static void CCC_ExcludeGroupBoxSiblings(HWND hGrp, HDC hdc)
{
    if (!hGrp || !hdc) return;
    HWND hParent = ::GetParent(hGrp);
    if (!hParent) return;
    RECT gr = {};
    ::GetWindowRect(hGrp, &gr);
    for (HWND h = ::GetWindow(hParent, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT))
    {
        if (h == hGrp || !::IsWindowVisible(h)) continue;
        // 自分より手前(後に作られた)兄弟だけ除外すれば足りるが、
        // z順に依存せず重なる可視兄弟はすべて除外する方が安全。
        RECT cr = {};
        ::GetWindowRect(h, &cr);
        RECT inter = {};
        if (!::IntersectRect(&inter, &gr, &cr)) continue;
        POINT pt1 = { inter.left, inter.top };
        POINT pt2 = { inter.right, inter.bottom };
        ::ScreenToClient(hGrp, &pt1);
        ::ScreenToClient(hGrp, &pt2);
        ::ExcludeClipRect(hdc, pt1.x, pt1.y, pt2.x, pt2.y);
    }
}

// 兄弟(LRC GDI / Edit / Static)を差し引いた領域だけ転送。
// BeginBufferedPaint は DC クリップを無視しがちなので、全面 Opaque は禁止。
// bChroma=TRUE のときクロマ透過(アクリル穴)。bOpaque はキャプションのみガラス用。
static void CCC_BlitGroupBoxMinusSiblings(HWND hGrp, HDC hdcDest, int x, int y,
    int w, int h, HDC hdcSrc, BOOL bOpaque, BOOL bChroma)
{
    if (!hGrp || !hdcDest || !hdcSrc || w <= 0 || h <= 0) return;
    CRgn rgn;
    if (!rgn.CreateRectRgn(0, 0, w, h)) return;
    HWND hParent = ::GetParent(hGrp);
    RECT gr = {};
    ::GetWindowRect(hGrp, &gr);
    if (hParent) {
        for (HWND hs = ::GetWindow(hParent, GW_CHILD); hs; hs = ::GetWindow(hs, GW_HWNDNEXT))
        {
            if (hs == hGrp || !::IsWindowVisible(hs)) continue;
            RECT cr = {}, inter = {};
            ::GetWindowRect(hs, &cr);
            if (!::IntersectRect(&inter, &gr, &cr)) continue;
            POINT pt1 = { inter.left, inter.top };
            POINT pt2 = { inter.right, inter.bottom };
            ::ScreenToClient(hGrp, &pt1);
            ::ScreenToClient(hGrp, &pt2);
            CRgn rs;
            if (rs.CreateRectRgn(pt1.x, pt1.y, pt2.x, pt2.y))
                rgn.CombineRgn(&rgn, &rs, RGN_DIFF);
        }
    }
    const DWORD need = ::GetRegionData((HRGN)rgn.GetSafeHandle(), 0, NULL);
    if (!need) return;
    RGNDATA* rd = (RGNDATA*)malloc(need);
    if (!rd) return;
    if (::GetRegionData((HRGN)rgn.GetSafeHandle(), need, rd) && rd->rdh.nCount > 0) {
        const RECT* rects = (const RECT*)rd->Buffer;
        for (DWORD i = 0; i < rd->rdh.nCount; ++i) {
            const int sx = rects[i].left;
            const int sy = rects[i].top;
            const int bw = rects[i].right - rects[i].left;
            const int bh = rects[i].bottom - rects[i].top;
            if (bw <= 0 || bh <= 0) continue;
            if (bOpaque) {
                RECT dest = { x + sx, y + sy, x + sx + bw, y + sy + bh };
                CCC_BlitToRectOpaque(hdcDest, dest, hdcSrc, sx, sy, bw, bh, bw, bh, FALSE);
            }
#if CCUSTOM_AERO_SUPPORT
            else if (bChroma) {
                CCC_BlitChromaTrans(hdcDest, x + sx, y + sy, bw, bh, hdcSrc, sx, sy, CCC_AERO_CHROMA_KEY);
            }
#endif
            else {
                ::BitBlt(hdcDest, x + sx, y + sy, bw, bh, hdcSrc, sx, sy, SRCCOPY);
            }
        }
    }
    free(rd);
}

static void CCC_DrawGroupBoxFrame(CDC& dc, const CRect& r, const CString& t, BOOL bTrans);

// 枠だけ描く。内側はクロマ（フルアクリル）か不透明ピンク（キャプションのみガラス）。
// 常にダブルバッファ。転送は兄弟矩形を差し引いたリージョン単位。
// BeginBufferedPaint は DC クリップを無視しがちなので全面 Opaque は禁止。
void CCustomGroupBox::DrawGroupBox(CDC* pDC, CRect& rect)
{
    const int rw = rect.Width();
    const int rh = rect.Height();
    if (!pDC || rw <= 0 || rh <= 0) return;

    // フルアクリル: 内側をクロマ透過。キャプションのみガラス: 不透明ピンク。
    const BOOL bAeroTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
#if CCUSTOM_AERO_SUPPORT
    const BOOL bCaptionOnly = CCC_CaptionOnlyHostGlass(m_hWnd);
    const BOOL bOpaqueFrame = !bAeroTrans && (bCaptionOnly || CCC_HostNeedsChildOpaque(m_hWnd));
#else
    const BOOL bOpaqueFrame = FALSE;
#endif
    CString t;
    GetWindowText(t);

    // 常にダブルバッファ: 淫女タイマーの Invalidate(FALSE) でもちらつかない
    CDC memDC;
    if (!memDC.CreateCompatibleDC(pDC)) {
        if (!bOpaqueFrame) {
            const int saved = pDC->SaveDC();
            CCC_ExcludeGroupBoxSiblings(m_hWnd, pDC->GetSafeHdc());
            CFont* pF = GetFont();
            if (pF) pDC->SelectObject(pF);
            CCC_DrawGroupBoxFrame(*pDC, CRect(0, 0, rw, rh), t, bAeroTrans);
            pDC->RestoreDC(saved);
        }
        return;
    }
    CBitmap bmp;
    if (!bmp.CreateCompatibleBitmap(pDC, rw, rh)) {
        memDC.DeleteDC();
        if (!bOpaqueFrame) {
            const int saved = pDC->SaveDC();
            CCC_ExcludeGroupBoxSiblings(m_hWnd, pDC->GetSafeHdc());
            CFont* pF = GetFont();
            if (pF) pDC->SelectObject(pF);
            CCC_DrawGroupBoxFrame(*pDC, CRect(0, 0, rw, rh), t, bAeroTrans);
            pDC->RestoreDC(saved);
        }
        return;
    }
    CBitmap* pOld = memDC.SelectObject(&bmp);
#if CCUSTOM_AERO_SUPPORT
    memDC.FillSolidRect(0, 0, rw, rh, bAeroTrans ? CCC_AERO_CHROMA_KEY : COLOR_DIALOG_BG);
#else
    memDC.FillSolidRect(0, 0, rw, rh, COLOR_DIALOG_BG);
#endif
    CFont* pF = GetFont();
    if (pF) memDC.SelectObject(pF);
    CCC_DrawGroupBoxFrame(memDC, CRect(0, 0, rw, rh), t, bAeroTrans);
    CCC_DrawInwoman(&memDC, CRect(0, 0, rw, rh), bAeroTrans);

    CCC_BlitGroupBoxMinusSiblings(m_hWnd, pDC->GetSafeHdc(), rect.left, rect.top,
        rw, rh, memDC.GetSafeHdc(), bOpaqueFrame, bAeroTrans);
    memDC.SelectObject(pOld);
}

// ============================================================================
// カスタムダイアログクラス基底 (CDialog版)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomDialog, CDialog)

BEGIN_MESSAGE_MAP(CCustomDialog, CDialogEx)
    ON_WM_CTLCOLOR()
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_MESSAGE(WM_USER + 1000, OnSubclassControls)
END_MESSAGE_MAP()

// CCustomDialog のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。実初期化は OnInitDialog / PreCreateWindow。
CCustomDialog::CCustomDialog() : m_bAeroEnabled(FALSE)
{
    m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
    m_brNull.CreateStockObject(NULL_BRUSH);
}

// CCustomDialog のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。実初期化は OnInitDialog / PreCreateWindow。
CCustomDialog::CCustomDialog(UINT n, CWnd* p) : CDialogEx(n, p), m_bAeroEnabled(FALSE)
{
    m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
    m_brNull.CreateStockObject(NULL_BRUSH);
}

// CCustomDialog の破棄。ダイアログ／NULL ブラシのみ。fixer は OnDestroy。
CCustomDialog::~CCustomDialog()
{
    if (m_brDialog.GetSafeHandle()) m_brDialog.DeleteObject();
    if (m_brNull.GetSafeHandle()) m_brNull.DeleteObject();
}

// 本文ぼかしフラグ。HWND があれば ApplyAero と子へ伝播。
// 子へ PROPAGATE。
void CCustomDialog::EnableAero(BOOL b)
{
    m_bAeroEnabled = b;
#if CCUSTOM_AERO_SUPPORT
    if (GetSafeHwnd())
    {
        CCC_ApplyAero(m_hWnd, b);
        CCC_PrepareDialogSurface(m_hWnd, b);
        PROPAGATE_AERO_TO_CHILDREN(m_hWnd, b);
        Invalidate();
    }
#endif
}

// 子を CCustom* へサブクラス。Blur 系はキャプション install を Post。
// 子サブクラス。Blur はキャプションを Show 前に。
BOOL CCustomDialog::OnInitDialog()
{
    BOOL r = CDialogEx::OnInitDialog();
    SubclassChildControls();
    return r;
}

// 遅延サブクラス。初期化順で HWND が後から付く子用。
// 遅延 HWND 用。
LRESULT CCustomDialog::OnSubclassControls(WPARAM, LPARAM)
{
    SubclassChildControls();
    return 0;
}

// 標準コントロールを CCustom* に差し替え（Edit/Button/List 等）。
// 標準子を CCustom* へ。
void CCustomDialog::SubclassChildControls()
{
    DoSubclassChildControls(this);
}

// 未サブクラス子の背景。アクリル時は null brush 寄り。
// サブクラス済み CCustom* は CtlColor 反射側。
HBRUSH CCustomDialog::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nC)
{
    HBRUSH h = DlgOnCtlColor(pDC, pWnd, nC, m_brDialog, m_bAeroEnabled);
    return h ? h : CDialogEx::OnCtlColor(pDC, pWnd, nC);
}

// 消去握りつぶし。アクリル上で ERASE だけだと完全透過のまま残る。
// TRUE で既定塗りを止める。
BOOL CCustomDialog::OnEraseBkgnd(CDC* pDC)
{
    return DlgOnEraseBkgnd(pDC, m_brDialog, m_bAeroEnabled, m_hWnd);
}

// WM_PAINT。透過はクロマ、ホストガラスは不透明パス、それ以外は素描画。
// CPaintDC。経路分岐は aero / ホストガラス / 通常。
void CCustomDialog::OnPaint()
{
    if (m_bAeroEnabled)
        DlgOnPaintAero(this, m_bAeroEnabled);
    else if (CCC_IsInwoman())
        DlgPaintSolidInwoman(this);
    else
        CDialogEx::OnPaint();
}

// CCC_GroupBoxesBack: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_GroupBoxesBack(HWND hDlg)
{
    if (!hDlg || !::IsWindow(hDlg)) return;
    for (HWND h = ::GetWindow(hDlg, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT))
    {
        if ((::GetWindowLong(h, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)
            ::SetWindowPos(h, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

#if CCUSTOM_AERO_SUPPORT
static BOOL CCC_PaintChildDirect(HWND hWnd, HDC hdcBuf);

// CCC_BtnSTNeedsChroma: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_BtnSTNeedsChroma(HWND hWnd)
{
    return CCC_IsBlurDialogChild(hWnd) && CCC_IsAeroEnabled() && CCC_IsWin11();
}

// CCC_RemapBtnSTChroma: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_RemapBtnSTChroma(CDC& dc, CButtonST* pBtn, const CRect& r)
{
    CCC_RemapSolidColorInDC(dc, r, COLOR_DIALOG_BG, CCC_AERO_CHROMA_KEY);
    CCC_RemapSolidColorInDC(dc, r, COLOR_BUTTON_BG, CCC_AERO_CHROMA_KEY);
    if (!pBtn) return;
    COLORREF cr = 0;
    if (pBtn->GetColor(CButtonST::BTNST_COLOR_BK_OUT, &cr) == BTNST_OK)
        CCC_RemapSolidColorInDC(dc, r, cr, CCC_AERO_CHROMA_KEY);
    if (pBtn->GetColor(CButtonST::BTNST_COLOR_BK_IN, &cr) == BTNST_OK)
        CCC_RemapSolidColorInDC(dc, r, cr, CCC_AERO_CHROMA_KEY);
    if (pBtn->GetColor(CButtonST::BTNST_COLOR_BK_FOCUS, &cr) == BTNST_OK)
        CCC_RemapSolidColorInDC(dc, r, cr, CCC_AERO_CHROMA_KEY);
}

// CCC_DrawButtonSTClient: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_DrawButtonSTClient(HWND hWnd, CButtonST* pBtn, HDC hdc, const RECT& rect)
{
    if (!pBtn || !hdc) return;
    const int rw = rect.right - rect.left;
    const int rh = rect.bottom - rect.top;
    if (rw <= 0 || rh <= 0) return;

    const BOOL bChroma = CCC_BtnSTNeedsChroma(hWnd);
    CBrush br(bChroma ? COLOR_DIALOG_BG : COLOR_BUTTON_BG);
    ::FillRect(hdc, &rect, (HBRUSH)br.GetSafeHandle());

    DRAWITEMSTRUCT dis = {};
    dis.CtlType = ODT_BUTTON;
    dis.CtlID = (UINT)::GetDlgCtrlID(hWnd);
    dis.itemID = dis.CtlID;
    dis.itemAction = ODA_DRAWENTIRE;
    const UINT st = (UINT)::SendMessage(hWnd, BM_GETSTATE, 0, 0);
    if (st & BST_PUSHED) dis.itemState |= ODS_SELECTED;
    if (!::IsWindowEnabled(hWnd)) dis.itemState |= ODS_DISABLED;
    if (::GetFocus() == hWnd) dis.itemState |= ODS_FOCUS;
    dis.hwndItem = hWnd;
    dis.hDC = hdc;
    dis.rcItem = rect;
    pBtn->DrawItem(&dis);

    if (bChroma)
    {
        CDC dc;
        dc.Attach(hdc);
        CCC_RemapBtnSTChroma(dc, pBtn, CRect(0, 0, rw, rh));
        dc.Detach();
    }
}

// CCC_BlitBtnSTChroma: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_BlitBtnSTChroma(HDC hdcDest, HWND hWnd, CButtonST* pBtn, const RECT& rect)
{
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return;

    CDC dcDest;
    dcDest.Attach(hdcDest);
    CDC dcMem;
    dcMem.CreateCompatibleDC(&dcDest);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&dcDest, width, height);
    CBitmap* pOld = dcMem.SelectObject(&bmp);
    CCC_DrawButtonSTClient(hWnd, pBtn, dcMem.GetSafeHdc(), rect);
    CCC_BlitChromaTrans(hdcDest, rect.left, rect.top, width, height,
        dcMem.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    dcMem.SelectObject(pOld);
    dcDest.Detach();
}

// CCC_ForcePaintButtonST: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_ForcePaintButtonST(HWND hWnd, CButtonST* pBtn)
{
    if (!hWnd || !::IsWindow(hWnd) || !pBtn)
        return;
    pBtn->ClearBackgroundCache();
    CWnd* pw = CWnd::FromHandlePermanent(hWnd);
    if (!pw) return;
    RECT rect = {};
    ::GetClientRect(hWnd, &rect);
    if (rect.right <= rect.left || rect.bottom <= rect.top)
        return;
    CClientDC dc(pw);
    CCC_BlitBtnSTChroma(dc.GetSafeHdc(), hWnd, pBtn, rect);
}

// Win11: 子ウィンドウの GDI はアルファ0のまま DWM に合成される。
// BufferedPaint で全面 alpha=255 にしてから WM_PRINTCLIENT で CCustom* を描く。
// ※WM_PRINTCLIENT を再帰的に PaintOpaque へ渡すと OnPrintClient が呼ばれず全面透過になる。
static BOOL CCC_FixerNeedsNcOpaque(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd))
        return FALSE;
    const LONG style = ::GetWindowLong(hWnd, GWL_STYLE);
    if (style & (WS_VSCROLL | WS_HSCROLL))
        return TRUE;
    wchar_t cls[32] = {};
    ::GetClassNameW(hWnd, cls, 32);
    if (::_wcsicmp(cls, L"SysListView32") == 0) return TRUE;
    if (::_wcsicmp(cls, L"SysTreeView32") == 0) return TRUE;
    if (::_wcsicmp(cls, L"SysTabControl32") == 0) return TRUE;
    if (::_wcsicmp(cls, L"SysHeader32") == 0) return TRUE;
    if (::_wcsicmp(cls, L"Edit") == 0) return TRUE;
    if (::_wcsicmp(cls, L"ComboBox") == 0) return TRUE;
    if (::_wcsicmp(cls, L"ListBox") == 0) return TRUE;
    return FALSE;
}

// Win11 ガラス: 子 GDI は α=0 のまま合成されるので、BufferedPaint で α=255 面を作る。
// その上に WM_PRINTCLIENT → OnPrintClient。CControlFixer（白透過）は CCustom* に使わない。
class CCustomOpaqueFixer
{
public:
    // ガラス上の子 HWND を不透明面にする。clrBg は穴埋め、bChroma はキー抜き。
    // Install で SetWindowSubclass。親の ExtendFrame(-1) があると GDI が消えるため必須。
    CCustomOpaqueFixer(COLORREF clrBg, BOOL bChroma = FALSE) : m_hWnd(NULL), m_bPrinting(FALSE), m_clrBg(clrBg), m_bChroma(bChroma) {}
    // サブクラスと DIB キャッシュを外す。ダイアログ OnDestroy からも呼ばれる。
    ~CCustomOpaqueFixer() { Uninstall(); }

    // 子 HWND に SetWindowSubclass。ガラス上の GDI を不透明面に差し替える。
    // 既に Install 済み／無効 HWND は FALSE。dwRefData は this。
    BOOL Install(HWND hWnd)
    {
        if (m_hWnd) return FALSE;
        if (!::IsWindow(hWnd)) return FALSE;
        m_hWnd = hWnd;
        return ::SetWindowSubclass(hWnd, SubclassProc, (UINT_PTR)this, (DWORD_PTR)this);
    }

    // サブクラス解除と DIB キャッシュ解放。破棄経路からも呼ぶ。
    // m_hWnd を NULL にして二重 Uninstall を無害化する。
    void Uninstall()
    {
        if (m_hWnd && ::IsWindow(m_hWnd))
            ::RemoveWindowSubclass(m_hWnd, SubclassProc, (UINT_PTR)this);
        m_hWnd = NULL;
        m_dib.Release();
    }

private:
    HWND m_hWnd;           // Install した子。Uninstall で NULL
    BOOL m_bPrinting;      // WM_PRINTCLIENT 再入中。PaintOpaque を重ねない
    COLORREF m_clrBg;      // 穴埋め色（リスト交互色の下地）
    BOOL m_bChroma;        // TRUE=キー抜き（ラベル等）。FALSE=全面 α=255
    CCC_ChromaBlitCache m_dib;

    // ガラス上では子の GDI が α=0 のまま DWM 合成され消える。
    // WM_PAINT は BeginPaint のクリップを捨て、GetDC で全面 PaintOpaque。
    // 部分 MakeOpaque は本文が透過して見えるので禁止。
    // ERASE は更新矩形があるとき二重描画を避けて空返し。
    // Edit/List のキー・クリックは SETREDRAW で既定の α=0 描画を止めてから載せる。
    // WM_PRINTCLIENT 再入は Def へ（再帰 PaintOpaque は全面透過になる）。
    static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        CCustomOpaqueFixer* pThis = (CCustomOpaqueFixer*)dwRefData;
        switch (uMsg)
        {
        case WM_ERASEBKGND:
            // 空返しだと α=0 のまま残り完全透過になる（ホバーで WM_PAINT すると戻る）。
            // ただし更新矩形があるときは直後の WM_PAINT が全面 PaintOpaque するので、
            // ここで描くとリスト等で 2 回分の不透明化になる。
            if (!pThis->m_bPrinting) {
                RECT ur = {};
                if (::GetUpdateRect(hWnd, &ur, FALSE) && (ur.right > ur.left) && (ur.bottom > ur.top))
                    return TRUE;
                if (wParam)
                    pThis->PaintOpaque(hWnd, (HDC)wParam);
                return TRUE;
            }
            return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
        case WM_PRINTCLIENT:
            if (pThis->m_bPrinting)
                return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            break;
        case WM_PAINT:
        {
            // BeginPaint の DC は更新矩形でクリップされるため、最終行より下の空きが
            // 矩形外だと BPPF_ERASE の黒のまま残る。検証は BeginPaint で行い、
            // 実描画はクリップ無しの GetDC へフルクライアントを描く。
            // ※部分 MakeOpaque はキャプション常時アクリル時に本文が透過して見えるので禁止。
            PAINTSTRUCT ps = {};
            ::BeginPaint(hWnd, &ps);
            HDC hDC = ::GetDC(hWnd);
            if (hDC) {
                pThis->PaintOpaque(hWnd, hDC);
                ::ReleaseDC(hWnd, hDC);
            }
            ::EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_NCPAINT:
        {
            // スクロールバー/枠付きだけ NC 不透明化。Button/Slider まで全面 PaintOpaque
            // すると最大化で子が1個ずつ描画されて見える。
            if (!CCC_FixerNeedsNcOpaque(hWnd))
                return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            HDC hDC = ::GetDC(hWnd);
            if (hDC) {
                pThis->PaintOpaque(hWnd, hDC);
                ::ReleaseDC(hWnd, hDC);
            }
            pThis->MakeWindowOpaque(hWnd);
            return lRes;
        }
        // Edit/ListBox/ComboBox: マウス進入/移動でテーマ NC や既定描画が α=0 を載せる → 透過に見える
        // ListBox は項目切替(LBUTTON)の部分描画でも同様。離脱で WM_PAINT が来ると直る。
        // ComboBox は CBN 選択後の ODS_COMBOBOXEDIT 素塗りも同様(DrawItem 側でも抑止)。
        // SysListView32 は毎 MOVE の全面再描画だと重いので UpdateHotItem 側の Post に任せる。
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
        case WM_NCMOUSEMOVE:
        case WM_NCMOUSELEAVE:
        {
            wchar_t cls[32];
            cls[0] = 0;
            ::GetClassNameW(hWnd, cls, 32);
            if (::_wcsicmp(cls, L"Edit") != 0
                && ::_wcsicmp(cls, L"ListBox") != 0
                && ::_wcsicmp(cls, L"ComboBox") != 0)
                break;
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            // 連続 WM_MOUSEMOVE は Post でまとめて再不透明化
            ::PostMessage(hWnd, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
            return lRes;
        }
        // ListBox: 選択切替の owner-draw が α=0 で先に載る。描画を止めてから不透明全面を載せる。
        // ListView: SETREDRAW はドラッグ選択等を阻害するので Post で再不透明化。
        // ComboBox は LBUTTON でドロップダウンを開くため SETREDRAW 禁止(選択欄は DrawItem で抑止)。
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
        {
            wchar_t cls[32];
            cls[0] = 0;
            ::GetClassNameW(hWnd, cls, 32);
            if (::_wcsicmp(cls, L"SysListView32") == 0
                || ::_wcsicmp(cls, L"SysTreeView32") == 0) {
                LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
                ::PostMessage(hWnd, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
                return lRes;
            }
            if (::_wcsicmp(cls, L"ListBox") != 0)
                break;
            ::SendMessage(hWnd, WM_SETREDRAW, FALSE, 0);
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            HDC hDC = ::GetDC(hWnd);
            if (hDC) {
                pThis->PaintOpaque(hWnd, hDC);
                ::ReleaseDC(hWnd, hDC);
            }
            ::ValidateRect(hWnd, NULL);
            ::SendMessage(hWnd, WM_SETREDRAW, TRUE, 0);
            ::ValidateRect(hWnd, NULL);
            return lRes;
        }
        // Edit: キー入力の既定描画が α=0 で先に載り一瞬アクリルが見える。
        // 描画停止中に不透明を載せ、SETREDRAW TRUE の Invalidate はすぐ潰す。
        case WM_CHAR:
        case WM_DEADCHAR:
        case WM_IME_CHAR:
        case WM_PASTE:
        case WM_CUT:
        case WM_CLEAR:
        case WM_UNDO:
        {
            wchar_t cls[32];
            cls[0] = 0;
            ::GetClassNameW(hWnd, cls, 32);
            if (::_wcsicmp(cls, L"Edit") != 0)
                break;
            ::SendMessage(hWnd, WM_SETREDRAW, FALSE, 0);
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            HDC hDC = ::GetDC(hWnd);
            if (hDC) {
                pThis->PaintOpaque(hWnd, hDC);
                ::ReleaseDC(hWnd, hDC);
            }
            ::ValidateRect(hWnd, NULL);
            ::SendMessage(hWnd, WM_SETREDRAW, TRUE, 0);
            ::ValidateRect(hWnd, NULL);
            return lRes;
        }
        case WM_KEYDOWN:
        {
            wchar_t cls[32];
            cls[0] = 0;
            ::GetClassNameW(hWnd, cls, 32);
            // ListBox: 矢印等で選択が動くときも α=0 部分描画になる
            // ListView: SETREDRAW せず Post(ホバーと同じ)。Combo キーボードは DrawItem 側。
            if (::_wcsicmp(cls, L"SysListView32") == 0
                || ::_wcsicmp(cls, L"SysTreeView32") == 0) {
                const WPARAM vk = wParam;
                const BOOL nav = (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT
                    || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_HOME || vk == VK_END
                    || vk == VK_SPACE || vk == VK_RETURN);
                if (!nav)
                    break;
                LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
                ::PostMessage(hWnd, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
                return lRes;
            }
            if (::_wcsicmp(cls, L"ListBox") == 0) {
                const WPARAM vk = wParam;
                const BOOL nav = (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT
                    || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_HOME || vk == VK_END
                    || vk == VK_SPACE || vk == VK_RETURN);
                if (!nav)
                    break;
                ::SendMessage(hWnd, WM_SETREDRAW, FALSE, 0);
                LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
                HDC hDC = ::GetDC(hWnd);
                if (hDC) {
                    pThis->PaintOpaque(hWnd, hDC);
                    ::ReleaseDC(hWnd, hDC);
                }
                ::ValidateRect(hWnd, NULL);
                ::SendMessage(hWnd, WM_SETREDRAW, TRUE, 0);
                ::ValidateRect(hWnd, NULL);
                return lRes;
            }
            if (::_wcsicmp(cls, L"Edit") != 0)
                break;
            const WPARAM vk = wParam;
            const BOOL ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const BOOL mut = (vk == VK_BACK || vk == VK_DELETE
                || (ctrl && (vk == 'V' || vk == 'X' || vk == 'Z' || vk == 'Y')));
            if (!mut)
                break;
            ::SendMessage(hWnd, WM_SETREDRAW, FALSE, 0);
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            HDC hDC = ::GetDC(hWnd);
            if (hDC) {
                pThis->PaintOpaque(hWnd, hDC);
                ::ReleaseDC(hWnd, hDC);
            }
            ::ValidateRect(hWnd, NULL);
            ::SendMessage(hWnd, WM_SETREDRAW, TRUE, 0);
            ::ValidateRect(hWnd, NULL);
            return lRes;
        }
        case WM_IME_STARTCOMPOSITION:
        {
            wchar_t cls[32];
            cls[0] = 0;
            ::GetClassNameW(hWnd, cls, 32);
            if (::_wcsicmp(cls, L"Edit") != 0)
                break;
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            if (CWnd* pw = CWnd::FromHandlePermanent(hWnd)) {
                if (CCustomEdit* e = dynamic_cast<CCustomEdit*>(pw))
                    e->SyncImePos();
            }
            return lRes;
        }
        case WM_IME_COMPOSITION:
        {
            wchar_t cls[32];
            cls[0] = 0;
            ::GetClassNameW(hWnd, cls, 32);
            if (::_wcsicmp(cls, L"Edit") != 0)
                break;
            // 変換中も IME 位置をキャレットへ追従。確定時は不透明再描画。
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            if (CWnd* pw = CWnd::FromHandlePermanent(hWnd)) {
                if (CCustomEdit* e = dynamic_cast<CCustomEdit*>(pw))
                    e->SyncImePos();
            }
            if (lParam & GCS_RESULTSTR) {
                ::SendMessage(hWnd, WM_SETREDRAW, FALSE, 0);
                HDC hDC = ::GetDC(hWnd);
                if (hDC) {
                    pThis->PaintOpaque(hWnd, hDC);
                    ::ReleaseDC(hWnd, hDC);
                }
                ::ValidateRect(hWnd, NULL);
                ::SendMessage(hWnd, WM_SETREDRAW, TRUE, 0);
                ::ValidateRect(hWnd, NULL);
            }
            return lRes;
        }
        case WM_VSCROLL:
        case WM_HSCROLL:
        case WM_MOUSEWHEEL:
        {
            // キャプション常時アクリル下では、ListView の中間描画(ジャケ/♪の透明画素)が
            // α=0 のまま画面に載り一瞬ガラスが見える=ちらつき。描画を止めてから
            // 全面 MakeOpaque 1回だけ出す。部分 MakeOpaque は本文透過になるので使わない。
            wchar_t cls[32];
            cls[0] = 0;
            ::GetClassNameW(hWnd, cls, 32);
            const BOOL bList = (::_wcsicmp(cls, L"SysListView32") == 0);
            const BOOL bTree = (::_wcsicmp(cls, L"SysTreeView32") == 0);
            if (bList || bTree)
                ::SendMessage(hWnd, WM_SETREDRAW, FALSE, 0);
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            if (bList || bTree)
                ::SendMessage(hWnd, WM_SETREDRAW, TRUE, 0);
            ::ValidateRect(hWnd, NULL);
            HDC hDC = ::GetDC(hWnd);
            if (hDC) {
                pThis->PaintOpaque(hWnd, hDC);
                ::ReleaseDC(hWnd, hDC);
            }
            pThis->MakeWindowOpaque(hWnd);
            return lRes;
        }
        case CCC_WM_POST_OPAQUE_PAINT:
        {
            HDC hDC = ::GetDC(hWnd);
            if (hDC)
            {
                pThis->PaintOpaque(hWnd, hDC);
                ::ReleaseDC(hWnd, hDC);
            }
            return 0;
        }
        case WM_SHOWWINDOW:
        {
            // SHOW は通常 Invalidate→WM_PAINT。ここで Send POST_OPAQUE すると
            // PaintOpaque が 2 回走る。WM_PAINT 側に任せる。
            return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
        }
        case WM_DESTROY:
            ::RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
            pThis->m_hWnd = NULL;
            break;
        }
        return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    // m_bPrinting 中は fixer が WM_PRINTCLIENT を握らない。
    // CCustom* は CCC_PaintChildDirect、それ以外は WM_PRINTCLIENT。
    // 再帰的に PaintOpaque へ渡すと OnPrintClient が呼ばれず全面透過。
    void PaintClientIntoBuffer(HWND hWnd, HDC hdcBuf)
    {
        m_bPrinting = TRUE;
        if (!CCC_PaintChildDirect(hWnd, hdcBuf))
            ::SendMessage(hWnd, WM_PRINTCLIENT, (WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
        m_bPrinting = FALSE;
    }

    // ウィンドウ全体(クライアント+非クライアント=スクロールバー/枠)の現在のピクセルを
    // 取り込み、アルファを不透明(255)にして書き戻す。アクリル(ガラス)上で GDI 描画の
    // 非クライアントがアルファ0となり透過してしまうのを防ぎ、スクロールバーを表示させる。
    void MakeWindowOpaque(HWND hWnd)
    {
        if (!::IsWindow(hWnd)) return;
        RECT wr = {};
        ::GetWindowRect(hWnd, &wr);
        const int w = wr.right - wr.left;
        const int h = wr.bottom - wr.top;
        if (w <= 0 || h <= 0) return;
        HDC hdcWin = ::GetWindowDC(hWnd);
        if (!hdcWin) return;
        RECT rc = { 0, 0, w, h };
        BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
        HDC hdcBuf = NULL;
        HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcWin, &rc, BPBF_TOPDOWNDIB, &params, &hdcBuf);
        if (hdcBuf && hBP)
        {
            // BufferedPaint で α 面を 255 に揃えてから DWM へ返す。
            // 現在描画済みの内容(枠/スクロールバー含む)をバッファへ取り込み
            ::BitBlt(hdcBuf, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);
            // アルファを不透明化して書き戻す
            ::BufferedPaintMakeOpaque(hBP, &rc);
            ::EndBufferedPaint(hBP, TRUE);
        }
        ::ReleaseDC(hWnd, hdcWin);
    }

    // 子を不透明面として dest へ出す。ガラス（ExtendFrame -1）では必須。
    // クロマボタンはクロマ合成。通常は共有 DIB を α=255 にして AlphaBlend。
    // 失敗時 BeginBufferedPaint + MakeOpaque。部分 MakeOpaque は周囲が穴になる。
    // 淫女シェイク時だけ AlphaBlend 経路でずらす（短く、ホットキーは書かない）。
    void PaintOpaque(HWND hWnd, HDC hDestDC)
    {
        RECT rect = {};
        ::GetClientRect(hWnd, &rect);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0) return;

        if (m_bChroma)
        {
            CDC dcDest;
            dcDest.Attach(hDestDC);
            CDC dcMem;
            dcMem.CreateCompatibleDC(&dcDest);
            CBitmap bmp;
            bmp.CreateCompatibleBitmap(&dcDest, width, height);
            CBitmap* pOld = dcMem.SelectObject(&bmp);
            PaintClientIntoBuffer(hWnd, dcMem.GetSafeHdc());
            CCC_BlitChromaTrans(hDestDC, 0, 0, width, height,
                dcMem.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
            dcMem.SelectObject(pOld);
            dcDest.Detach();
            return;
        }

        // キャプション常時アクリル(ExtendFrame -1)下では本文コントロールを
        // 必ず全面 α=255 にする。部分 MakeOpaque は周囲が透過して見える。
        // 16枠の共有DIBだとボタンとリストがサイズ衝突して毎回 CreateDIB になる。
        {
            if (m_dib.Ensure(hDestDC, width, height) && m_dib.pBits && m_dib.hdcDib) {
                CBrush brush(m_clrBg);
                RECT zr = { 0, 0, width, height };
                ::FillRect(m_dib.hdcDib, &zr, (HBRUSH)brush.GetSafeHandle());
                PaintClientIntoBuffer(hWnd, m_dib.hdcDib);
                m_dib.MakeRectOpaque(0, 0, width, height);
                if (CCC_IsInwoman()) {
                    int ox = 0, oy = 0;
                    CCC_InwomanGetShake(ox, oy);
                    if (ox || oy) {
                        CBrush brush(m_clrBg);
                        RECT zr = { 0, 0, width, height };
                        ::FillRect(hDestDC, &zr, (HBRUSH)brush.GetSafeHandle());
                    }
                    CCC_InwomanAlphaBlend(hDestDC, width, height, m_dib.hdcDib);
                    return;
                }
                const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                if (::GdiAlphaBlend(hDestDC, 0, 0, width, height,
                        m_dib.hdcDib, 0, 0, width, height, bf))
                    return;
            }
        }

        BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
        params.dwFlags = BPPF_ERASE;
        HDC hdcBuf = NULL;
        HPAINTBUFFER hBufferedPaint = ::BeginBufferedPaint(hDestDC, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);

        if (hdcBuf && hBufferedPaint)
        {
            CBrush brush(m_clrBg);
            ::FillRect(hdcBuf, &rect, (HBRUSH)brush.GetSafeHandle());
            PaintClientIntoBuffer(hWnd, hdcBuf);
            ::BufferedPaintMakeOpaque(hBufferedPaint, &rect);
            ::EndBufferedPaint(hBufferedPaint, TRUE);
            return;
        }

        // BeginBufferedPaint 失敗時: 互換 DC で不透明合成
        CDC dcDest;
        dcDest.Attach(hDestDC);
        CDC dcMem;
        dcMem.CreateCompatibleDC(&dcDest);
        CBitmap bmp;
        bmp.CreateCompatibleBitmap(&dcDest, width, height);
        CBitmap* pOld = dcMem.SelectObject(&bmp);

        CBrush brush(m_clrBg);
        dcMem.FillRect(CRect(0, 0, width, height), &brush);
        PaintClientIntoBuffer(hWnd, dcMem.GetSafeHdc());
        CCC_InwomanBitBlt(dcDest.GetSafeHdc(), width, height, dcMem.GetSafeHdc(), m_clrBg);

        dcMem.SelectObject(pOld);
        dcDest.Detach();
    }
};

// CCC_OpaqueBgForHwnd: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static COLORREF CCC_OpaqueBgForHwnd(HWND hWnd)
{
    if (CWnd* pw = CWnd::FromHandlePermanent(hWnd))
    {
        if (dynamic_cast<CCustomListBox*>(pw) || dynamic_cast<CCustomListCtrl*>(pw) || dynamic_cast<CCustomTreeCtrl*>(pw) || dynamic_cast<CCustomTabCtrl*>(pw)) return COLOR_LIST_BG;
        if (dynamic_cast<CCustomComboBox*>(pw)) return COLOR_COMBO_BG;
        if (dynamic_cast<CCustomStandardButton*>(pw) || dynamic_cast<CButtonST*>(pw)) return COLOR_BUTTON_BG;
        if (dynamic_cast<CCustomProgressCtrl*>(pw)) return COLOR_DIALOG_BG;
        if (dynamic_cast<CCustomSysPerfCtrl*>(pw)) return COLOR_DIALOG_BG;
        if (dynamic_cast<CCustomEdit*>(pw)) return COLOR_EDIT_BG;
    }
    TCHAR cls[64] = {};
    ::GetClassName(hWnd, cls, 63);
    CString c(cls);
    c.MakeUpper();
    if (c.Find(_T("EDIT")) >= 0) return COLOR_EDIT_BG;
    if (c.Find(_T("LISTBOX")) >= 0) return COLOR_LIST_BG;
    if (c.Find(_T("SYSLISTVIEW32")) >= 0) return COLOR_LIST_BG;
    if (c.Find(_T("SYSTREEVIEW32")) >= 0) return COLOR_LIST_BG;
    if (c.Find(_T("SYSTABCONTROL32")) >= 0) return COLOR_LIST_BG;
    if (c.Find(_T("COMBOBOX")) >= 0) return COLOR_COMBO_BG;
    return COLOR_DIALOG_BG;
}

// CCC_IsBlurControl: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_IsBlurControl(HWND hWnd)
{
    if (CWnd* pw = CWnd::FromHandlePermanent(hWnd))
    {
        if (dynamic_cast<CCustomStatic*>(pw)) return TRUE;
        if (dynamic_cast<CCustomSliderCtrl*>(pw)) return TRUE;
        if (dynamic_cast<CCustomRangeSliderCtrl*>(pw)) return TRUE;
        if (dynamic_cast<CCustomGroupBox*>(pw)) return TRUE;
        if (dynamic_cast<CCustomCheckBox*>(pw)) return TRUE;
        if (dynamic_cast<CCustomProgressCtrl*>(pw)) return TRUE;
        if (dynamic_cast<CCustomSysPerfCtrl*>(pw)) return TRUE;
    }
    TCHAR cls[64] = {};
    ::GetClassName(hWnd, cls, 63);
    CString c(cls);
    c.MakeUpper();
    if (c.Find(_T("STATIC")) >= 0) return TRUE;
    if (c.Find(TRACKBAR_CLASS) >= 0) return TRUE;
    return FALSE;
}

// キャプションだけアクリル（本文 save.aero=0）。ホストは α ガラスだが本文は不透明必須。
// CCC_CaptionOnlyHostGlass: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_CaptionOnlyHostGlass(HWND hWnd)
{
#if CCUSTOM_AERO_SUPPORT
    if (!hWnd || !CCC_IsWin11() || CCC_IsAeroEnabled())
        return FALSE;
    for (HWND h = hWnd; h; h = ::GetParent(h)) {
        if (CCC_AcrylicCaption(h))
            return TRUE;
    }
    return FALSE;
#else
    UNREFERENCED_PARAMETER(hWnd);
    return FALSE;
#endif
}

// CCC_ShouldOpaqueFix: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_ShouldOpaqueFix(HWND hWnd)
{
    if (!::IsWindow(hWnd)) return FALSE;
    // キャプション帯のボタン/追随はガラス透過描画するため fixer しない
    if (CCC_IsCaptionChromeCtrl(hWnd)) return FALSE;

    // GroupBox を fixer すると全面 α=255 塗りで兄弟 Edit/Static を消す。枠は自前描画。
    if (CWnd* pwGb = CWnd::FromHandlePermanent(hWnd)) {
        if (dynamic_cast<CCustomGroupBox*>(pwGb))
            return FALSE;
    }
    if ((::GetWindowLong(hWnd, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX)
        return FALSE;

    // キャプションのみアクリル時は、本文の blur 系（スライダー/STATIC 等）も不透明化
    // （αホストのまま通常 GDI だと穴＝変なアクリルになる）
    if (CCC_CaptionOnlyHostGlass(hWnd)) {
        if (CWnd* pw = CWnd::FromHandlePermanent(hWnd))
        {
            // 自前 Opaque blit する GDI ビュー。fixer の PRINTCLIENT だと中身が空になる
            if (pw->GetRuntimeClass()) {
                const char* cn = pw->GetRuntimeClass()->m_lpszClassName;
                if (cn && (strcmp(cn, "CCommandRollView") == 0 || strcmp(cn, "CLyricsViewWnd") == 0
                    || strcmp(cn, "CCustomDjVinylCtrl") == 0))
                    return FALSE;
            }
            if (dynamic_cast<CCustomListBox*>(pw)) return TRUE;
            if (dynamic_cast<CCustomListCtrl*>(pw)) return TRUE;
            if (dynamic_cast<CCustomTreeCtrl*>(pw)) return TRUE;
            if (dynamic_cast<CCustomTabCtrl*>(pw)) return TRUE;
            if (dynamic_cast<CCustomComboBox*>(pw)) return TRUE;
            if (dynamic_cast<CButtonST*>(pw)) return TRUE;
            if (dynamic_cast<CCustomEdit*>(pw)) return TRUE;
            if (dynamic_cast<CCustomStandardButton*>(pw)) return TRUE;
            if (dynamic_cast<CCustomStatic*>(pw)) return TRUE;
            if (dynamic_cast<CCustomSliderCtrl*>(pw)) return TRUE;
            if (dynamic_cast<CCustomRangeSliderCtrl*>(pw)) return TRUE;
            if (dynamic_cast<CCustomCheckBox*>(pw)) return TRUE;
            if (dynamic_cast<CCustomProgressCtrl*>(pw)) return TRUE;
            if (dynamic_cast<CCustomSysPerfCtrl*>(pw)) return TRUE;
        }
        TCHAR cls[64] = {};
        ::GetClassName(hWnd, cls, 63);
        CString c(cls);
        c.MakeUpper();
        if (c.Find(_T("BUTTON")) >= 0) return TRUE;
        if (c.Find(_T("LISTBOX")) >= 0) return TRUE;
        if (c.Find(_T("SYSLISTVIEW32")) >= 0) return TRUE;
        if (c.Find(_T("SYSTREEVIEW32")) >= 0) return TRUE;
        if (c.Find(_T("SYSTABCONTROL32")) >= 0) return TRUE;
        if (c.Find(_T("COMBOBOX")) >= 0) return TRUE;
        if (c.Find(_T("EDIT")) >= 0) return TRUE;
        if (c.Find(_T("STATIC")) >= 0) return TRUE;
        if (c.Find(TRACKBAR_CLASS) >= 0) return TRUE;
        if (c.Find(_T("MSCTLS_PROGRESS32")) >= 0) return TRUE;
        return TRUE; // その他の子も穴防止
    }

    if (CCC_IsBlurControl(hWnd)) return FALSE;
    if (CWnd* pw = CWnd::FromHandlePermanent(hWnd))
    {
        if (dynamic_cast<CCustomListBox*>(pw)) return TRUE;
        if (dynamic_cast<CCustomListCtrl*>(pw)) return TRUE;
        if (dynamic_cast<CCustomTreeCtrl*>(pw)) return TRUE;
        if (dynamic_cast<CCustomTabCtrl*>(pw)) return TRUE;
        if (dynamic_cast<CCustomComboBox*>(pw)) return TRUE;
        if (dynamic_cast<CButtonST*>(pw)) return TRUE;
        if (dynamic_cast<CCustomEdit*>(pw)) return TRUE;
        // StandardButton も自前 BitBlt だけだと ExtendFrame/ERASE 後に α=0 で消える
        if (dynamic_cast<CCustomStandardButton*>(pw)) return TRUE;
        return FALSE;
    }
    TCHAR cls[64] = {};
    ::GetClassName(hWnd, cls, 63);
    CString c(cls);
    c.MakeUpper();
    if (c.Find(_T("BUTTON")) >= 0) return TRUE;
    if (c.Find(_T("LISTBOX")) >= 0) return TRUE;
    if (c.Find(_T("SYSLISTVIEW32")) >= 0) return TRUE;
    if (c.Find(_T("SYSTREEVIEW32")) >= 0) return TRUE;
    if (c.Find(_T("SYSTABCONTROL32")) >= 0) return TRUE;
    if (c.Find(_T("COMBOBOX")) >= 0) return TRUE;
    if (c.Find(_T("EDIT")) >= 0) return TRUE;
    if (c.Find(_T("SYSHEADER32")) >= 0) return TRUE;
    return FALSE;
}

// CCC_PaintChildDirect: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_PaintChildDirect(HWND hWnd, HDC hdcBuf)
{
    CWnd* pw = CWnd::FromHandlePermanent(hWnd);
    if (!pw) return FALSE;
    CDC dc;
    dc.Attach(hdcBuf);
    CRect r;
    pw->GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0) { dc.Detach(); return FALSE; }

    BOOL painted = FALSE;
    if (auto* p = dynamic_cast<CCustomStandardButton*>(pw))
    {
        // fixer の DIB へ素の PaintClient。α=255 は直後の MakeRectOpaque が担う。
        // ここで PaintOpaqueClient すると AlphaBlend が二重になりボタンが重い。
        p->PaintClient(dc, r);
        painted = TRUE;
    }
    else if (auto* pEdit = dynamic_cast<CCustomEdit*>(pw))
    {
#if CCUSTOM_AERO_SUPPORT
        if (CCC_HostNeedsChildOpaque(hWnd))
            CCC_FillRectOpaqueBits(hdcBuf, r, COLOR_EDIT_BG);
        else
#endif
        {
            CBrush br(COLOR_EDIT_BG);
            dc.FillRect(&r, &br);
        }
        pEdit->DrawClientText(dc, r);
        dc.Detach();
        return TRUE;
    }
    else if (auto* pList = dynamic_cast<CCustomListCtrl*>(pw))
    {
        pList->PaintOpaqueIntoBuffer(hdcBuf);
        dc.Detach();
        return TRUE;
    }
    else if (auto* pTree = dynamic_cast<CCustomTreeCtrl*>(pw))
    {
        pTree->PaintOpaqueIntoBuffer(hdcBuf);
        dc.Detach();
        return TRUE;
    }
    else if (auto* pTab = dynamic_cast<CCustomTabCtrl*>(pw))
    {
        pTab->PaintOpaqueIntoBuffer(hdcBuf);
        dc.Detach();
        return TRUE;
    }
    else if (auto* pProg = dynamic_cast<CCustomProgressCtrl*>(pw))
    {
        pProg->PaintOpaqueIntoBuffer(hdcBuf);
        dc.Detach();
        return TRUE;
    }
    else if (auto* pRange = dynamic_cast<CCustomRangeSliderCtrl*>(pw))
    {
        pRange->PaintOpaqueIntoBuffer(hdcBuf);
        dc.Detach();
        return TRUE;
    }
    else if (auto* pSld = dynamic_cast<CCustomSliderCtrl*>(pw))
    {
        pSld->PaintOpaqueIntoBuffer(hdcBuf);
        dc.Detach();
        return TRUE;
    }
    else if (auto* pPerf = dynamic_cast<CCustomSysPerfCtrl*>(pw))
    {
        pPerf->PaintOpaqueIntoBuffer(hdcBuf);
        dc.Detach();
        return TRUE;
    }
    else if (auto* pBtn = dynamic_cast<CButtonST*>(pw))
    {
        RECT rect = {};
        ::GetClientRect(hWnd, &rect);
        CCC_DrawButtonSTClient(hWnd, pBtn, hdcBuf, rect);
        dc.Detach();
        return TRUE;
    }
    else if (auto* pSt = dynamic_cast<CCustomStatic*>(pw))
    {
        if (pSt->PaintCustomOpaque(dc)) {
            dc.Detach();
            return TRUE;
        }
    }
    dc.Detach();
    return painted;
}

// CCC_ClearOpaqueFixerList: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_ClearOpaqueFixerList(CTypedPtrList<CPtrList, CCustomOpaqueFixer*>& fixers)
{
    while (!fixers.IsEmpty())
    {
        CCustomOpaqueFixer* p = fixers.RemoveHead();
        if (p) { p->Uninstall(); delete p; }
    }
}

// 子を再帰走査し ShouldOpaqueFix なら fixer を Install。
// CButtonST でクロマが要るものだけ bChroma。背景色はコントロール種別。
static void CCC_InstallOpaqueFixers(HWND hParent, CTypedPtrList<CPtrList, CCustomOpaqueFixer*>& fixers)
{
    for (HWND hChild = ::GetWindow(hParent, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (CCC_ShouldOpaqueFix(hChild))
        {
            BOOL bChromaBtn = FALSE;
            if (CWnd* pw = CWnd::FromHandlePermanent(hChild))
                bChromaBtn = (dynamic_cast<CButtonST*>(pw) != NULL) && CCC_BtnSTNeedsChroma(hChild);
            CCustomOpaqueFixer* pFixer = new CCustomOpaqueFixer(
                bChromaBtn ? COLOR_DIALOG_BG : CCC_OpaqueBgForHwnd(hChild), bChromaBtn);
            if (pFixer->Install(hChild))
                fixers.AddTail(pFixer);
            else
                delete pFixer;
        }
        CCC_InstallOpaqueFixers(hChild, fixers);
    }
}

// CCC_BlitGroupFrame: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_BlitGroupFrame(HDC hdcDest, int x, int y, int w, int h,
    HDC hdcSrc, COLORREF clrKey, const CRect& innerClient)
{
    if (w <= 0 || h <= 0) return;
    CRgn rgnOuter, rgnInner, rgnFrame;
    rgnOuter.CreateRectRgn(0, 0, w, h);
    if (innerClient.Width() > 0 && innerClient.Height() > 0)
    {
        rgnInner.CreateRectRgnIndirect(&innerClient);
        rgnFrame.CreateRectRgn(0, 0, 0, 0);
        rgnFrame.CombineRgn(&rgnOuter, &rgnInner, RGN_DIFF);
    }
    else
    {
        rgnFrame.CreateRectRgn(0, 0, w, h);
    }
    HRGN hrgnScreen = ::CreateRectRgn(0, 0, 0, 0);
    ::CombineRgn(hrgnScreen, (HRGN)rgnFrame.GetSafeHandle(), NULL, RGN_COPY);
    ::OffsetRgn(hrgnScreen, x, y);
    const int saved = ::SaveDC(hdcDest);
    ::ExtSelectClipRgn(hdcDest, hrgnScreen, RGN_AND);
    CCC_BlitChromaNF(hdcDest, x, y, w, h, hdcSrc, 0, 0, clrKey);
    ::RestoreDC(hdcDest, saved);
    ::DeleteObject(hrgnScreen);
}

// 二重線の枠＋タイトルギャップ＋裾レース＋角リボン。
// Soft3D ゆらゆらは GetTickCount/500（疎）。タイトル下地はクロマまたはダイアログ色。
// 透過時の黒文字は RGB(2,2,2)（クロマ 1,1,1 と区別）。
static void CCC_DrawGroupBoxFrame(CDC& dc, const CRect& r, const CString& t, BOOL bTrans)
{
    CFont* pOF = dc.SelectObject(dc.GetCurrentFont());

    // タイトルは枠幅に合わせて横縮小。右上リボン分(約24px)を確保。
    CString title = t;
    CSize s(0, 0);
    if (!title.IsEmpty())
    {
        const int maxTitleW = (std::max)(8, r.Width() - 40);
        s = dc.GetTextExtent(title);
        if (s.cx > maxTitleW)
            s.cx = maxTitleW;
    }

    int nT = r.top + (s.cy > 0 ? s.cy / 2 : 8);

    CPen pO(PS_SOLID, 2, RGB(255, 140, 180));
    CPen pI(PS_SOLID, 1, RGB(255, 200, 220));
    dc.SelectObject(&pO);
    dc.SelectStockObject(NULL_BRUSH);
    dc.MoveTo(r.left + 1, nT);
    if (s.cx > 0)
    {
        dc.LineTo(r.left + 6, nT);
        dc.MoveTo(r.left + s.cx + 16, nT);
    }
    dc.LineTo(r.right - 2, nT);
    dc.LineTo(r.right - 2, r.bottom - 2);
    dc.LineTo(r.left + 1, r.bottom - 2);
    dc.LineTo(r.left + 1, nT);

    dc.SelectObject(&pI);
    const int off = 3;
    dc.MoveTo(r.left + off, nT + off);
    if (s.cx > 0)
    {
        dc.LineTo(r.left + 6 + off, nT + off);
        dc.MoveTo(r.left + s.cx + 16, nT + off);
    }
    dc.LineTo(r.right - off, nT + off);
    dc.LineTo(r.right - off, r.bottom - off);
    dc.LineTo(r.left + off, r.bottom - off);
    dc.LineTo(r.left + off, nT + off);

    DrawRibbon(&dc, CRect(r.left + 2, r.bottom - 12, r.left + 14, r.bottom), RGB(255, 182, 193));
    DrawRibbon(&dc, CRect(r.right - 14, r.bottom - 12, r.right - 2, r.bottom), RGB(255, 182, 193));

    // 右上の角はしどけないリボンで色っぽく（左上はタイトルと重なるため省略）
    DrawLooseRibbon(&dc, CRect(r.right - 19, nT - 8, r.right - 1, nT + 8), COLOR_BOW);
    if (title.IsEmpty())
        DrawLooseRibbon(&dc, CRect(r.left + 1, nT - 8, r.left + 19, nT + 8), COLOR_BOW);
    // Soft3D ゆらゆら（疎タイマーで Invalidate される前提）
    {
        const int tick = (int)(::GetTickCount64() / 500);
        DrawSoftJkSwayCorner(&dc, CRect(r.right - 16, nT - 6, r.right - 2, nT + 8), tick, 8.f);
        DrawSoftJkSwayCorner(&dc, CRect(r.left + 2, r.bottom - 16, r.left + 16, r.bottom - 2), tick + 3, 6.f);
        DrawSoftJkChip(&dc, CRect(r.right - 14, r.bottom - 14, r.right - 2, r.bottom - 2), tick, FALSE);
    }
    DrawSparkle(&dc, r.right - 9, r.bottom - 9, 3, COLOR_SPARKLE);
    DrawSparkle(&dc, r.left + 9, r.bottom - 9, 3, COLOR_SPARKLE);
    // 下辺に黒の細レース + 透けレースのスカラップでランジェリー風の色気
    DrawLaceLine(&dc, r.left + 18, r.bottom - 7, r.right - 18, r.bottom - 7, RGB(60, 40, 55));
    DrawLaceScallop(&dc, r.left + 16, r.bottom - 5, r.right - 16, 3, COLOR_LACE);

    if (!title.IsEmpty())
    {
        CRect rt(r.left + 8, nT - s.cy / 2, r.left + 8 + s.cx + 4, nT + s.cy / 2);
        if (rt.right > r.right - 22)
            rt.right = r.right - 22;
#if CCUSTOM_AERO_SUPPORT
        dc.FillSolidRect(&rt, bTrans ? CCC_AERO_CHROMA_KEY : COLOR_DIALOG_BG);
#else
        dc.FillSolidRect(&rt, COLOR_DIALOG_BG);
        UNREFERENCED_PARAMETER(bTrans);
#endif
        dc.SetBkMode(TRANSPARENT);
        // クロマキー RGB(1,1,1) と区別するため、透過時の黒文字は 2,2,2 にずらす
        dc.SetTextColor(bTrans ? RGB(2, 2, 2) : RGB(0, 0, 0));
        DrawFitControlText(&dc, rt, title, DT_LEFT | DT_VCENTER | DT_NOPREFIX, 0.50f);
    }
    dc.SelectObject(pOF);
}

// ぼかし ON: ApplyAero → CLIPCHILDREN → GroupBoxesBack → Win11 なら fixer。
// 末尾 FRAMECHANGED は NC 再計算用。二重 REAPPLY は載せない。
// 直後の CaptionApplyGlass / 初回 OnShowWindow が fixer を載せる。
static void CCC_FinishBlurDlg(CWnd* pDlg, BOOL bAero, BOOL& bBlurApplied,
    CTypedPtrList<CPtrList, CCustomOpaqueFixer*>& fixers)
{
    if (!pDlg || !pDlg->GetSafeHwnd()) return;

    CCC_ClearOpaqueFixerList(fixers);
    if (!bAero)
    {
        bBlurApplied = FALSE;
        return;
    }

    bBlurApplied = CCC_ApplyAero(pDlg->m_hWnd, TRUE) != FALSE;
    CCC_PrepareDialogSurface(pDlg->m_hWnd, TRUE);
    PROPAGATE_AERO_TO_CHILDREN(pDlg->m_hWnd, TRUE);
    pDlg->ModifyStyle(0, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    CCC_GroupBoxesBack(pDlg->m_hWnd);

    if (CCC_IsWin11())
        CCC_InstallOpaqueFixers(pDlg->m_hWnd, fixers);

    pDlg->SetWindowPos(NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_DRAWFRAME);
    pDlg->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
    // REAPPLY は直後の CaptionApplyGlassAndFixers / 初回 OnShowWindow が fixer を載せる。
    // ここで Post すると ALLCHILDREN 不透明化がもう一周する。
}

// fixer を張り直す。AcrylicCaption 時は save.aero OFF でも必要。
// PostOpaque + ALLCHILDREN は各子 PaintOpaque が 2 回になるので Invalidate 1 回。
static void CCC_ReapplyOpaqueFix(CWnd* pDlg, CTypedPtrList<CPtrList, CCustomOpaqueFixer*>& fixers)
{
    if (!pDlg || !pDlg->GetSafeHwnd() || !CCC_IsWin11()) return;
    // AcrylicCaption 時は save.aero OFF でも fixer が必要
    if (!CCC_IsAeroEnabled() && !CCC_AcrylicCaption(pDlg->m_hWnd)) return;
    CCC_ClearOpaqueFixerList(fixers);
    CCC_InstallOpaqueFixers(pDlg->m_hWnd, fixers);
    // PostOpaque + RedrawWindow(ALLCHILDREN) は各子 PaintOpaque が 2 回。Invalidate 1 回に任せる。
    pDlg->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

// CCC_ForceRepaintHwnd: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_ForceRepaintHwnd(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd))
        return;

    CWnd* pw = CWnd::FromHandlePermanent(hWnd);
    if (auto* pStd = dynamic_cast<CCustomStandardButton*>(pw))
    {
        pStd->RepaintClient();
        return;
    }
    if (auto* pBtn = dynamic_cast<CButtonST*>(pw))
    {
        if (CCC_BtnSTNeedsChroma(hWnd))
            CCC_ForcePaintButtonST(hWnd, pBtn);
        else
        {
            RECT rect = {};
            ::GetClientRect(hWnd, &rect);
            if (rect.right <= rect.left || rect.bottom <= rect.top)
                return;
            CClientDC dc(pw);
#if CCUSTOM_AERO_SUPPORT
            if (CCC_IsWin11() && (CCC_IsAeroEnabled() || CCC_CaptionOnlyHostGlass(hWnd)))
            {
                BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
                params.dwFlags = BPPF_ERASE;
                HDC hdcBuf = NULL;
                HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
                if (hdcBuf && hBP) {
                    CCC_DrawButtonSTClient(hWnd, pBtn, hdcBuf, rect);
                    ::BufferedPaintMakeOpaque(hBP, &rect);
                    ::EndBufferedPaint(hBP, TRUE);
                    return;
                }
            }
#endif
            CCC_DrawButtonSTClient(hWnd, pBtn, dc.GetSafeHdc(), rect);
        }
        return;
    }

    if (auto* pEdit = dynamic_cast<CCustomEdit*>(pw))
    {
        pEdit->RepaintClient();
        return;
    }

#if CCUSTOM_AERO_SUPPORT
    if ((CCC_IsAeroEnabled() || CCC_CaptionOnlyHostGlass(hWnd)) && CCC_IsWin11() && CCC_ShouldOpaqueFix(hWnd))
    {
        ::RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
        return;
    }
#endif
    ::RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
}

// CCC_RefreshChildProc: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CALLBACK CCC_RefreshChildProc(HWND hChild, LPARAM)
{
    CCC_ForceRepaintHwnd(hChild);
    return TRUE;
}

// CCC_RefreshKids: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_RefreshKids(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd))
        return;
    ::EnumChildWindows(hWnd, CCC_RefreshChildProc, 0);
}
#endif

#if !CCUSTOM_AERO_SUPPORT
// CCC_ForceRepaintHwnd: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_ForceRepaintHwnd(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd))
        return;
    CWnd* pw = CWnd::FromHandlePermanent(hWnd);
    if (auto* pStd = dynamic_cast<CCustomStandardButton*>(pw))
    {
        pStd->RepaintClient();
        return;
    }
    if (auto* pBtn = dynamic_cast<CButtonST*>(pw))
    {
        RECT rect = {};
        ::GetClientRect(hWnd, &rect);
        if (rect.right <= rect.left || rect.bottom <= rect.top)
            return;
        CClientDC dc(pw);
        DRAWITEMSTRUCT dis = {};
        dis.CtlType = ODT_BUTTON;
        dis.CtlID = (UINT)::GetDlgCtrlID(hWnd);
        dis.itemID = dis.CtlID;
        dis.itemAction = ODA_DRAWENTIRE;
        const UINT st = (UINT)::SendMessage(hWnd, BM_GETSTATE, 0, 0);
        if (st & BST_PUSHED) dis.itemState |= ODS_SELECTED;
        if (!::IsWindowEnabled(hWnd)) dis.itemState |= ODS_DISABLED;
        if (::GetFocus() == hWnd) dis.itemState |= ODS_FOCUS;
        dis.hwndItem = hWnd;
        dis.hDC = dc.GetSafeHdc();
        dis.rcItem = rect;
        pBtn->DrawItem(&dis);
        return;
    }
    ::RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
}

// CCC_RefreshChildProcNoAero: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CALLBACK CCC_RefreshChildProcNoAero(HWND hChild, LPARAM)
{
    CCC_ForceRepaintHwnd(hChild);
    return TRUE;
}

// CCC_RefreshKids: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_RefreshKids(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd))
        return;
    ::EnumChildWindows(hWnd, CCC_RefreshChildProcNoAero, 0);
}
#endif

// ============================================================================
// カスタムキャプション (CCustomBlurDialog* 共通・アクリル有無に関係なく常時)
// システム WS_CAPTION を外し、クライアント先頭帯に CCustom ボタンを置く。
// ============================================================================
struct CCC_MainLockEntry;
static CCC_MainLockEntry* CCC_FindMainLockEntry(HWND hWnd);
static void CCC_MainLockReleaseOverlayCache(CCC_MainLockEntry* e);

struct CCC_CaptionEntry {
    HWND hWnd = NULL;
    int height = 0;
    BOOL hasMin = FALSE;
    BOOL hasMax = FALSE;
    BOOL hasSettings = FALSE;
    BOOL topmost = FALSE;
    BOOL installed = FALSE;
    // savedata.aero 非依存。キャプション帯は常にアクリル(1)
    BOOL acrylicCaption = TRUE;
    // WS_POPUP では ShowWindow(SW_SHOWMAXIMIZED) が効かないことがあるので手動最大化
    BOOL manualZoomed = FALSE;
    BOOL haveRestore = FALSE;
    RECT restoreRc = {};
    CCustomStandardButton* pClose = nullptr;
    CCustomStandardButton* pMin = nullptr;
    CCustomStandardButton* pMax = nullptr;
    CCustomStandardButton* pSettings = nullptr;
    CCustomStandardButton* pPin = nullptr;
    CCustomStandardButton* pOfflineHelp = nullptr;
    CToolTipCtrl* pCapTip = nullptr;
    // 帯の見た目キャッシュ（本文60fps再描画で帯を毎回作り直さない）
    BOOL paintValid = FALSE;
    BOOL paintActive = FALSE;
    BOOL paintHadIcon = FALSE;
    int paintW = 0;
    int paintH = 0;
    int paintTitleRight = 0;
    wchar_t paintTitle[512] = {};
    // WS_SYSMENU を外す前に、About などアプリ追加のシステムメニュー項目を退避
    UINT sysExtraId[8] = {};
    wchar_t sysExtraText[8][256] = {};
    int sysExtraCount = 0;
};

static CCC_CaptionEntry g_captions[64];
static int g_captionCount = 0;
static const int CCC_CAP_BTN = 26;
static const int CCC_CAP_GAP = 2;
static const int CCC_CAP_RIGHT_MARGIN = 4;
static const COLORREF CCC_CAP_BG = RGB(48, 40, 62);
static const COLORREF CCC_CAP_BG_INACTIVE = RGB(58, 52, 68);
static const COLORREF CCC_CAP_TEXT = RGB(255, 248, 252);

static CCC_CaptionEntry* CCC_FindCaption(HWND hWnd);

// キャプション専用アクリル判定。savedata.aero / m_bAeroEnabled は見ない（帯は常時ガラス可）
// CCC_AcrylicCaption: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
BOOL CCC_AcrylicCaption(HWND hWnd)
{
#if CCUSTOM_AERO_SUPPORT
    if (!hWnd || !::IsWindow(hWnd) || !CCC_IsWin11())
        return FALSE;
    CCC_CaptionEntry* e = CCC_FindCaption(hWnd);
    return (e && e->installed && e->acrylicCaption) ? TRUE : FALSE;
#else
    UNREFERENCED_PARAMETER(hWnd);
    return FALSE;
#endif
}

// CCC_IsCaptionChromeCtrl: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_IsCaptionChromeCtrl(HWND hWnd)
{
    if (!hWnd) return FALSE;
    const UINT id = (UINT)::GetDlgCtrlID(hWnd);
    if (id == IDC_MAINWIN_LOCK
        || id == IDC_CAP_CLOSE || id == IDC_CAP_MIN || id == IDC_CAP_MAX
        || id == IDC_CAP_SETTINGS || id == IDC_CAP_PIN || id == IDC_CAP_OFFLINE_HELP)
        return TRUE;
    // キャプション隣の「?」操作ガイド（アクリル帯で欠けないよう chrome 扱い）
    static const UINT kHelpChromeIds[] = {
        IDC_SC_HELP, IDC_PL_HELP, IDC_AN_HELP, IDC_PR_HELP, IDC_EQ_HELP,
        IDC_PT_HELP, IDC_RD_HELP, IDC_DR_HELP, IDC_WE_HELP, IDC_TC_HELP,
        IDC_TE_HELP, IDC_FD_HELP, IDC_KPI_HELP, IDC_SY_HELP, IDC_PRT_HELP,
        IDC_OGG_HELP, IDC_PRM_HELP, IDC_MCR_HELP, IDC_DOUGA_HELP, IDC_MP_CHEATBTN,
        IDC_SM_HELP, IDC_DIG_HELP, IDC_VC_HELP, IDC_TN_HELP, IDC_PF_HELP,
        IDC_S3M_HELP, IDC_S3R_HELP, IDC_MP_BPM_HELP, IDC_TB_HELP, IDC_VST_HELP, IDC_CD_HELP, IDC_MM_HELP
    };
    for (int i = 0; i < (int)_countof(kHelpChromeIds); ++i) {
        if (id == kHelpChromeIds[i])
            return TRUE;
    }
    return FALSE;
}

// CCC_IsCaptionHelpChromeId: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_IsCaptionHelpChromeId(UINT id)
{
    static const UINT kHelpChromeIds[] = {
        IDC_SC_HELP, IDC_PL_HELP, IDC_AN_HELP, IDC_PR_HELP, IDC_EQ_HELP,
        IDC_PT_HELP, IDC_RD_HELP, IDC_DR_HELP, IDC_WE_HELP, IDC_TC_HELP,
        IDC_TE_HELP, IDC_FD_HELP, IDC_KPI_HELP, IDC_SY_HELP, IDC_PRT_HELP,
        IDC_OGG_HELP, IDC_PRM_HELP, IDC_MCR_HELP, IDC_DOUGA_HELP, IDC_MP_CHEATBTN,
        IDC_SM_HELP, IDC_DIG_HELP, IDC_VC_HELP, IDC_TN_HELP, IDC_PF_HELP,
        IDC_S3M_HELP, IDC_S3R_HELP, IDC_MP_BPM_HELP, IDC_TB_HELP, IDC_VST_HELP, IDC_CD_HELP, IDC_MM_HELP
    };
    for (int i = 0; i < (int)_countof(kHelpChromeIds); ++i) {
        if (id == kHelpChromeIds[i])
            return TRUE;
    }
    return FALSE;
}

// CCC_FindCaptionHelpChrome: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static HWND CCC_FindCaptionHelpChrome(HWND hDlg)
{
    if (!hDlg || !::IsWindow(hDlg)) return NULL;
    static const UINT kHelpChromeIds[] = {
        IDC_SC_HELP, IDC_PL_HELP, IDC_AN_HELP, IDC_PR_HELP, IDC_EQ_HELP,
        IDC_PT_HELP, IDC_RD_HELP, IDC_DR_HELP, IDC_WE_HELP, IDC_TC_HELP,
        IDC_TE_HELP, IDC_FD_HELP, IDC_KPI_HELP, IDC_SY_HELP, IDC_PRT_HELP,
        IDC_OGG_HELP, IDC_PRM_HELP, IDC_MCR_HELP, IDC_DOUGA_HELP, IDC_MP_CHEATBTN,
        IDC_SM_HELP, IDC_DIG_HELP, IDC_VC_HELP, IDC_TN_HELP, IDC_PF_HELP,
        IDC_S3M_HELP, IDC_S3R_HELP, IDC_MP_BPM_HELP, IDC_TB_HELP, IDC_VST_HELP, IDC_CD_HELP, IDC_MM_HELP
    };
    for (int i = 0; i < (int)_countof(kHelpChromeIds); ++i) {
        HWND h = ::GetDlgItem(hDlg, kHelpChromeIds[i]);
        if (h && ::IsWindow(h))
            return h;
    }
    return NULL;
}

// 表示前でも HWND があれば「本」+「?」2 枠分を確保（追随が被るのを防ぐ）
// CCC_CaptionHelpChromeReserve: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static int CCC_CaptionHelpChromeReserve(HWND hDlg)
{
    return CCC_FindCaptionHelpChrome(hDlg) ? (2 * (CCC_CAP_BTN + CCC_CAP_GAP)) : 0;
}

// PROPAGATE 後もキャプション帯は透過描画（チェック等）。ボタンは Opaque 経路。
// CCC_CaptionChromeReapplyTrans: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionChromeReapplyTrans(HWND hDlg)
{
#if CCUSTOM_AERO_SUPPORT
    if (!CCC_AcrylicCaption(hDlg)) return;
    for (HWND h = ::GetWindow(hDlg, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT)) {
        if (!CCC_IsCaptionChromeCtrl(h)) continue;
        CWnd* pw = CWnd::FromHandlePermanent(h);
        if (!pw) continue;
        if (auto* p = dynamic_cast<CCustomCheckBox*>(pw))
            p->SetAeroMode(TRUE);
    }
#else
    UNREFERENCED_PARAMETER(hDlg);
#endif
}

// ClearRect が帯上の子画素を消すので、キャプション描画の最後に必ず載せ直す
// CCC_CaptionPaintChromeNow: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionPaintChromeNow(HWND hDlg)
{
    if (!hDlg || !::IsWindow(hDlg)) return;
    for (HWND h = ::GetWindow(hDlg, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT)) {
        if (!CCC_IsCaptionChromeCtrl(h) || !::IsWindowVisible(h)) continue;
        CWnd* pw = CWnd::FromHandlePermanent(h);
        if (auto* pBtn = dynamic_cast<CCustomStandardButton*>(pw)) {
            pBtn->RepaintClient();
            continue;
        }
        ::RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    }
}

// 本文 aero だけ解除（ホストの backdrop/α/ExtendFrame は触らない）
// CCC_DisableBodyAeroOnly: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_DisableBodyAeroOnly(HWND hWnd)
{
#if CCUSTOM_AERO_SUPPORT
    if (!hWnd || !::IsWindow(hWnd)) return;
    CCC_ClearChildDwmBackdrop(hWnd);
    CCC_ClearChildTrans(hWnd);
#else
    UNREFERENCED_PARAMETER(hWnd);
#endif
}

// AcrylicCaption ではない窓でも、DWM 既定キャプション（アイコン含む）の重ね描きを止める
// CCC_CaptionHideDwmTitleChrome: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionHideDwmTitleChrome(HWND hWnd)
{
	if (!hWnd || !::IsWindow(hWnd))
		return;
#if CCUSTOM_AERO_SUPPORT
#ifndef WTNCA_NODRAWCAPTION
#define WTNCA_NODRAWCAPTION 0x00000001
#endif
#ifndef WTNCA_NODRAWICON
#define WTNCA_NODRAWICON 0x00000002
#endif
	WTA_OPTIONS opts = {};
	opts.dwFlags = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON;
	opts.dwMask = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON;
	::SetWindowThemeAttribute(hWnd, WTA_NONCLIENT, &opts, sizeof(opts));

	BOOL compositionEnabled = FALSE;
	if (!::DwmIsCompositionEnabled(&compositionEnabled) || !compositionEnabled)
		return;
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
	const COLORREF colorNone = (COLORREF)DWMWA_COLOR_NONE;
	::DwmSetWindowAttribute(hWnd, DWMWA_CAPTION_COLOR, &colorNone, sizeof(colorNone));
	::DwmSetWindowAttribute(hWnd, DWMWA_TEXT_COLOR, &colorNone, sizeof(colorNone));
	::DwmSetWindowAttribute(hWnd, DWMWA_BORDER_COLOR, &colorNone, sizeof(colorNone));
#endif
}

// CCC_CaptionEnsureBackdrop: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionEnsureBackdrop(HWND hWnd)
{
#if CCUSTOM_AERO_SUPPORT
    if (!CCC_AcrylicCaption(hWnd))
        return;
    BOOL compositionEnabled = FALSE;
    if (!::DwmIsCompositionEnabled(&compositionEnabled) || !compositionEnabled)
        return;

    static BOOL s_bpInit = FALSE;
    if (!s_bpInit) {
        ::BufferedPaintInit();
        s_bpInit = TRUE;
    }

#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_REDIRECTIONBITMAP_ALPHA
#define DWMWA_REDIRECTIONBITMAP_ALPHA 39
#endif
    const COLORREF colorNone = (COLORREF)DWMWA_COLOR_NONE;
    ::DwmSetWindowAttribute(hWnd, DWMWA_CAPTION_COLOR, &colorNone, sizeof(colorNone));
    ::DwmSetWindowAttribute(hWnd, DWMWA_TEXT_COLOR, &colorNone, sizeof(colorNone));
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
    ::DwmSetWindowAttribute(hWnd, DWMWA_BORDER_COLOR, &colorNone, sizeof(colorNone));

    LONG exStyle = ::GetWindowLong(hWnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_LAYERED)
        ::SetWindowLong(hWnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);

    int backdropType = 3; // DWMSBT_TRANSIENTWINDOW = Desktop Acrylic
    ::DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
    ::EnableRoundedCorners(hWnd);

    {
        BOOL useAlpha = TRUE;
        ::DwmSetWindowAttribute(hWnd, DWMWA_REDIRECTIONBITMAP_ALPHA, &useAlpha, sizeof(useAlpha));
    }

    const MARGINS margins = CCC_CaptionHostMargins(hWnd);
    ::DwmExtendFrameIntoClientArea(hWnd, &margins);
    ::SetClassLongPtr(hWnd, GCLP_HBRBACKGROUND, 0);
    // backdrop 後に DWM がシステム帯を戻すことがあるので、描画抑制を最後に再適用
    CCC_CaptionHideDwmTitleChrome(hWnd);
#else
    UNREFERENCED_PARAMETER(hWnd);
#endif
}

// CCC_CaptionEnsureHostAcrylic: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_CaptionEnsureHostAcrylic(HWND hWnd)
{
#if CCUSTOM_AERO_SUPPORT
	CCC_CaptionEnsureBackdrop(hWnd);
#else
	UNREFERENCED_PARAMETER(hWnd);
#endif
}

// 既存 RGB を保ったまま α=255 にする（ピアノ/アナライザ等の BitBlt 後の全透過を塞ぐ）
// CCC_MakeRectOpaquePreserve: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MakeRectOpaquePreserve(HDC hdc, const RECT& rc)
{
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0 || !hdc) return;

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdc, &rc, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (hdcBuf && hBP) {
        ::BitBlt(hdcBuf, rc.left, rc.top, w, h, hdc, rc.left, rc.top, SRCCOPY);
        ::BufferedPaintMakeOpaque(hBP, &rc);
        RGBQUAD* pPixels = nullptr;
        int rowLength = 0;
        if (SUCCEEDED(::GetBufferedPaintBits(hBP, &pPixels, &rowLength)) && pPixels && rowLength > 0) {
            for (int y = 0; y < h; ++y) {
                RGBQUAD* row = reinterpret_cast<RGBQUAD*>(
                    reinterpret_cast<BYTE*>(pPixels) + y * rowLength * static_cast<int>(sizeof(RGBQUAD)));
                for (int x = 0; x < w; ++x)
                    row[x].rgbReserved = 255;
            }
        }
        ::EndBufferedPaint(hBP, TRUE);
    }
}

// ホストアクリルを有効化してから fixer（描画フラグ m_bAeroEnabled は別）
// CCC_CaptionApplyGlassAndFixers: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionApplyGlassAndFixers(CWnd* pDlg,
    CTypedPtrList<CPtrList, CCustomOpaqueFixer*>& fixers)
{
#if CCUSTOM_AERO_SUPPORT
    if (!pDlg || !pDlg->GetSafeHwnd()) return;
    if (!CCC_AcrylicCaption(pDlg->m_hWnd) || !CCC_IsWin11()) {
        CCC_CaptionEnsureBackdrop(pDlg->m_hWnd);
        return;
    }

    // キャプションアクリル用ホストは本文 aero と同じ ApplyAero(TRUE)/-1。
    // 子への透過モード伝播だけ save.aero に従う（本文 GDI は描画で α=255 化）。
    if (CCC_IsAeroEnabled()) {
        CCC_ApplyAero(pDlg->m_hWnd, TRUE);
        PROPAGATE_AERO_TO_CHILDREN(pDlg->m_hWnd, TRUE);
    }
    else {
        CCC_ApplyAero(pDlg->m_hWnd, TRUE);
        PROPAGATE_AERO_TO_CHILDREN(pDlg->m_hWnd, FALSE);
    }

    // ApplyAero 末尾 FRAMECHANGED でマージンが落ちるので載せ直す
    CCC_CaptionEnsureBackdrop(pDlg->m_hWnd);
    CCC_PrepareDialogSurface(pDlg->m_hWnd, CCC_IsAeroEnabled());
    pDlg->ModifyStyle(0, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    CCC_CaptionChromeReapplyTrans(pDlg->m_hWnd);

    CCC_ClearOpaqueFixerList(fixers);
    CCC_InstallOpaqueFixers(pDlg->m_hWnd, fixers);
    CCC_GroupBoxesBack(pDlg->m_hWnd);
    // PostOpaque は直後の ALLCHILDREN と二重。ERASE も fixer の WM_PAINT と二重になる。
    pDlg->RedrawWindow(NULL, NULL,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
#else
    UNREFERENCED_PARAMETER(pDlg);
    UNREFERENCED_PARAMETER(fixers);
#endif
}

// キャプション下の本文を α=255 で塗り、ホストガラスが本文に出ないようにする。
// 帯は CaptionPaint の ClearRect のまま残す（clip 全体を塗るとアクリルが消える）。
static void CCC_PaintOpaqueBodyBelowCaption(CDC& dc, CWnd* pDlg, CBrush& brDlg)
{
#if CCUSTOM_AERO_SUPPORT
    if (!pDlg || !pDlg->GetSafeHwnd()) return;
    if (!CCC_CaptionOnlyHostGlass(pDlg->m_hWnd)) return;
    CRect body;
    pDlg->GetClientRect(&body);
    const int capH = CCC_GetCustomCaptionHeight(pDlg->m_hWnd);
    if (capH > 0 && body.Height() > capH)
        body.top = capH;
    if (body.Width() <= 0 || body.Height() <= 0) return;
    // 子(リストのスクロールバー等)を潰さないよう隙間だけ塗る
    const int saved = dc.SaveDC();
    CCC_ClipNoChildren(dc, pDlg);
    UNREFERENCED_PARAMETER(brDlg);
    CRect clip;
    if (dc.GetClipBox(&clip) != ERROR && !clip.IsRectEmpty()) {
        CRect fill;
        // 更新領域∩本文のみ。帯を含む clip をそのまま塗ると ClearRect が潰れる
        // （MP/ピアノ/アナライザは自前 OnPaint 末尾 CaptionPaint のため被害なし）
        if (fill.IntersectRect(&body, &clip) && fill.Width() > 0 && fill.Height() > 0)
            CCC_FillRectOpaqueBits(dc.GetSafeHdc(), fill, COLOR_DIALOG_BG);
    }
    if (CCC_IsInwoman())
        CCC_DrawInwomanDlgBody(&dc, body);
    dc.RestoreDC(saved);
#else
    UNREFERENCED_PARAMETER(dc);
    UNREFERENCED_PARAMETER(pDlg);
    UNREFERENCED_PARAMETER(brDlg);
#endif
}

// カスタムキャプション時: システムキャプション NC を作らず、左右下のリサイズ枠のみ残す。
// 上辺 NC も残さない（細い枠に DWM タイトル残骸が出るのを防ぐ）。上端リサイズはクライアント側で。
static LRESULT CCC_CaptionHandleNcCalcSize(HWND hWnd, WPARAM wParam, LPARAM lParam, LRESULT defResult)
{
    if (CCC_GetCustomCaptionHeight(hWnd) <= 0)
        return defResult;
    if (!lParam)
        return defResult;

    // wParam=FALSE は RECT*。ここを既定に落とすとシステムキャプション NC が復活する。
    RECT* rc = wParam
        ? &(reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0])
        : reinterpret_cast<RECT*>(lParam);
    const DWORD style = (DWORD)::GetWindowLong(hWnd, GWL_STYLE);
    const UINT dpi = CCC_GetControlDpi(hWnd);
    int frameX = 0, frameY = 0;
    if (style & WS_THICKFRAME) {
        frameX = ::GetSystemMetricsForDpi(SM_CXFRAME, dpi) + ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        frameY = ::GetSystemMetricsForDpi(SM_CYFRAME, dpi) + ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    }
    else if (style & WS_BORDER) {
        frameX = ::GetSystemMetricsForDpi(SM_CXBORDER, dpi);
        frameY = ::GetSystemMetricsForDpi(SM_CYBORDER, dpi);
    }
    // rgrc[0] = 新しい窓矩形。上は NC ゼロ（クライアントが天辺まで）。キャプションはクライアント描画。
    rc->left += frameX;
    rc->right -= frameX;
    rc->bottom -= frameY;
    // top はそのまま（窓上端 = クライアント上端）。DWM のシステムタイトル帯を作らない。
    UNREFERENCED_PARAMETER(frameY);
    return 0;
}

// CCC_FindCaption: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static CCC_CaptionEntry* CCC_FindCaption(HWND hWnd)
{
    for (int i = 0; i < g_captionCount; ++i) {
        if (g_captions[i].hWnd == hWnd)
            return &g_captions[i];
    }
    return nullptr;
}

// CCC_GetOrCreateCaption: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static CCC_CaptionEntry* CCC_GetOrCreateCaption(HWND hWnd)
{
    if (CCC_CaptionEntry* e = CCC_FindCaption(hWnd))
        return e;
    if (g_captionCount >= (int)_countof(g_captions))
        return nullptr;
    CCC_CaptionEntry* e = &g_captions[g_captionCount++];
    e->hWnd = hWnd;
    return e;
}

// CCC_GetCustomCaptionHeight: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
int CCC_GetCustomCaptionHeight(HWND hDlg)
{
    CCC_CaptionEntry* e = CCC_FindCaption(hDlg);
    if (!e || !e->installed)
        return 0;
    return e->height;
}

// CCC_CaptionDestroyBtn: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionDestroyBtn(CCustomStandardButton*& p)
{
    if (!p)
        return;
    if (::IsWindow(p->GetSafeHwnd()))
        p->DestroyWindow();
    p = nullptr;
}

// CCC_CaptionUnregister: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_CaptionUnregister(HWND hWnd)
{
    for (int i = 0; i < g_captionCount; ++i) {
        if (g_captions[i].hWnd != hWnd)
            continue;
        CCC_CaptionDestroyBtn(g_captions[i].pClose);
        CCC_CaptionDestroyBtn(g_captions[i].pMin);
        CCC_CaptionDestroyBtn(g_captions[i].pMax);
        CCC_CaptionDestroyBtn(g_captions[i].pSettings);
        CCC_CaptionDestroyBtn(g_captions[i].pPin);
        CCC_CaptionDestroyBtn(g_captions[i].pOfflineHelp);
        for (int j = i + 1; j < g_captionCount; ++j)
            g_captions[j - 1] = g_captions[j];
        --g_captionCount;
        ZeroMemory(&g_captions[g_captionCount], sizeof(g_captions[g_captionCount]));
        break;
    }
}

// キャプション用の flat CCustomStandardButton。共有アイコン、AutoDelete。
// 失敗したら delete して nullptr。
static CCustomStandardButton* CCC_CaptionMakeBtn(CWnd* pDlg, UINT id, LPCWSTR text)
{
    CCustomStandardButton* p = new CCustomStandardButton();
    p->EnableAutoDelete(TRUE);
    p->SetFlat(TRUE);
    CRect rc(0, 0, CCC_CAP_BTN, CCC_CAP_BTN);
    if (!p->Create(text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, rc, pDlg, id)) {
        delete p;
        return nullptr;
    }
    if (CFont* pFont = pDlg->GetFont())
        p->SetFont(pFont);
    const UINT iconId = CCC_CtlIconForCtrl(id);
    if (iconId)
        CCC_CaptionApplySharedIcon(p, iconId);
    return p;
}

// CCC_CaptionSysBtnCount: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static int CCC_CaptionSysBtnCount(const CCC_CaptionEntry* e)
{
    int n = 1; // close
    if (e->hasMax) ++n;
    if (e->hasMin) ++n;
    return n;
}

// CCC_CaptionExtraBtnCount: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static int CCC_CaptionExtraBtnCount(const CCC_CaptionEntry* e)
{
    int n = 0;
    if (e->hasSettings) ++n;
    ++n; // pin always
    return n;
}

// CCC_CaptionGetTitleRight: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionGetTitleRight(HWND hDlg, CCC_CaptionEntry* e, int& titleRight)
{
    CRect cr;
    ::GetClientRect(hDlg, &cr);
    const int sysN = CCC_CaptionSysBtnCount(e);
    const int extraN = CCC_CaptionExtraBtnCount(e);
    const int lockW = CCC_MainLockGetReserveWidth(hDlg);
    // オーバーレイ式「メインに追随」は GetReserveWidth=0 なので、キャプション内に描かれている分を別途確保
    int lockOverlayW = 0;
    if (lockW <= 0 && CCC_GetCustomCaptionHeight(hDlg) > 0) {
        CRect lockRc;
        CCC_MainLockGetOverlayRect(hDlg, lockRc);
        if (!lockRc.IsRectEmpty() && lockRc.top < CCC_GetCustomCaptionHeight(hDlg))
            lockOverlayW = lockRc.Width() + CCC_CAP_GAP;
    }
    const int helpW = CCC_CaptionHelpChromeReserve(hDlg);
    titleRight = cr.right - CCC_CAP_RIGHT_MARGIN
        - sysN * (CCC_CAP_BTN + CCC_CAP_GAP)
        - extraN * (CCC_CAP_BTN + CCC_CAP_GAP)
        - ((lockW > 0) ? (lockW + CCC_CAP_GAP) : 0)
        - lockOverlayW
        - helpW;
    if (titleRight < 28)
        titleRight = 28;
}

// CAP 配置と同じ規則で「ピン左端」を求める
// CCC_CaptionLayout: Close を置いた直後に x-=(BTN+GAP) してから Max?/Min?/Settings?/Pin。
// has* フラグだけでなく実 HWND があるときだけスロットを消費（無いのに空きが出るのを防ぐ）。
static int CCC_CaptionPinLeft(HWND hDlg, const CCC_CaptionEntry* e)
{
    if (!hDlg || !e || !::IsWindow(hDlg))
        return 0;
    // レイアウト後は実ウィンドウ位置が最優先
    if (e->pPin && ::IsWindow(e->pPin->GetSafeHwnd()) && e->pPin->IsWindowVisible()) {
        CRect pr;
        e->pPin->GetWindowRect(&pr);
        ::ScreenToClient(hDlg, &pr.TopLeft());
        ::ScreenToClient(hDlg, &pr.BottomRight());
        return pr.left;
    }
    CRect cr;
    ::GetClientRect(hDlg, &cr);
    int x = cr.right - CCC_CAP_RIGHT_MARGIN - CCC_CAP_BTN; // Close left
    // Close を置いたあと同じく 1 スロット左へ
    x -= (CCC_CAP_BTN + CCC_CAP_GAP);
    if (e->hasMax && e->pMax && ::IsWindow(e->pMax->GetSafeHwnd()))
        x -= (CCC_CAP_BTN + CCC_CAP_GAP);
    if (e->hasMin && e->pMin && ::IsWindow(e->pMin->GetSafeHwnd()))
        x -= (CCC_CAP_BTN + CCC_CAP_GAP);
    if (e->hasSettings && e->pSettings && ::IsWindow(e->pSettings->GetSafeHwnd()))
        x -= (CCC_CAP_BTN + CCC_CAP_GAP);
    return x;
}

// CCC_CaptionPlaceHelpBtn: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_CaptionPlaceHelpBtn(HWND hDlg, CWnd* pHelp)
{
    if (!hDlg || !::IsWindow(hDlg) || !pHelp || !pHelp->GetSafeHwnd())
        return;
    if (!CCC_IsCaptionHelpChromeId((UINT)pHelp->GetDlgCtrlID()))
        return;

    CCC_CaptionEntry* e = CCC_FindCaption(hDlg);
    if (!e || !e->installed)
        return;

    const int btn = CCC_CAP_BTN;
    const int gap = CCC_CAP_GAP;
    const int pinLeft = CCC_CaptionPinLeft(hDlg, e);
    const int y = (e->height > btn) ? (e->height - btn) / 2 : 2;
    // 右から: × Max? Min? ⚙? P ←?←本← メインに追従
    int xHelp = pinLeft - gap - btn;
    if (xHelp < 4) xHelp = 4;
    int xChm = xHelp - gap - btn;
    if (xChm < 4) xChm = 4;

    CWnd* pDlg = CWnd::FromHandle(hDlg);
    if (pDlg && (!e->pOfflineHelp || !::IsWindow(e->pOfflineHelp->GetSafeHwnd())))
    {
        e->pOfflineHelp = CCC_CaptionMakeBtn(pDlg, IDC_CAP_OFFLINE_HELP, L"");
        if (e->pOfflineHelp && e->pCapTip && e->pCapTip->GetSafeHwnd())
        {
            e->pCapTip->AddTool(e->pOfflineHelp, LL14(
                L"オフラインヘルプを開く（F1）",
                L"Open offline help (F1)",
                L"Ouvrir l'aide hors ligne (F1)",
                L"Apri guida offline (F1)",
                L"Abrir ayuda sin conexion (F1)",
                L"오프라인 도움말 열기(F1)",
                L"打开离线帮助（F1）",
                L"فتح التعليمات دون اتصال (F1)",
                L"Открыть офлайн-справку (F1)",
                L"Offline-Hilfe offnen (F1)",
                L"Abrir ajuda offline (F1)",
                L"Offline-help openen (F1)",
                L"Otworz pomoc offline (F1)",
                L"Cevrimdisi yardimi ac (F1)"));
        }
    }

    CWnd* placeTargets[2] = { pHelp, e->pOfflineHelp };
    const int placeXs[2] = { xHelp, xChm };
    for (int i = 0; i < 2; ++i) {
        CWnd* p = placeTargets[i];
        if (!p || !p->GetSafeHwnd())
            continue;
        const int x = placeXs[i];
        CRect cur;
        p->GetWindowRect(&cur);
        ::ScreenToClient(hDlg, &cur.TopLeft());
        ::ScreenToClient(hDlg, &cur.BottomRight());
        if (cur.left == x && cur.top == y && cur.Width() == btn && cur.Height() == btn) {
            p->SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            continue;
        }
        p->SetWindowPos(&CWnd::wndTop, x, max(0, y), btn, btn,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

// CCC_CaptionRefreshDpi: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_CaptionRefreshDpi(HWND hDlg)
{
    CCC_CaptionEntry* e = CCC_FindCaption(hDlg);
    if (!e || !e->installed || !::IsWindow(hDlg))
        return;
    const UINT dpi = CCC_GetControlDpi(hDlg);
    int sysCap = ::GetSystemMetricsForDpi(SM_CYCAPTION, dpi);
    if (sysCap < 24)
        sysCap = 24;
    int capH = CCC_ScaleDpi(32, dpi);
    if (capH < CCC_CAP_BTN + 6)
        capH = CCC_CAP_BTN + 6;
    if (capH < sysCap)
        capH = sysCap;
    e->height = capH;
    CCC_CaptionLayout(hDlg);
}

// 右から Close → Max? → Min? → Settings? → Pin。その左に help と追従。
// SWP_NOCOPYBITS で帯アクリルのゴミコピーを避ける。
// 追従を前面にしたあと help が沈まないよう再度 Top。
void CCC_CaptionLayout(HWND hDlg)
{
    CCC_CaptionEntry* e = CCC_FindCaption(hDlg);
    if (!e || !e->installed || !::IsWindow(hDlg))
        return;
    CRect cr;
    ::GetClientRect(hDlg, &cr);
    const int y = (e->height - CCC_CAP_BTN) / 2;
    int x = cr.right - CCC_CAP_RIGHT_MARGIN - CCC_CAP_BTN;

    if (e->pClose && ::IsWindow(e->pClose->GetSafeHwnd())) {
        e->pClose->SetWindowPos(&CWnd::wndTop, x, y, CCC_CAP_BTN, CCC_CAP_BTN,
            SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
        x -= (CCC_CAP_BTN + CCC_CAP_GAP);
    }
    if (e->hasMax && e->pMax && ::IsWindow(e->pMax->GetSafeHwnd())) {
        e->pMax->SetWindowPos(&CWnd::wndTop, x, y, CCC_CAP_BTN, CCC_CAP_BTN,
            SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
        x -= (CCC_CAP_BTN + CCC_CAP_GAP);
    }
    if (e->hasMin && e->pMin && ::IsWindow(e->pMin->GetSafeHwnd())) {
        e->pMin->SetWindowPos(&CWnd::wndTop, x, y, CCC_CAP_BTN, CCC_CAP_BTN,
            SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
        x -= (CCC_CAP_BTN + CCC_CAP_GAP);
    }
    if (e->hasSettings && e->pSettings && ::IsWindow(e->pSettings->GetSafeHwnd())) {
        e->pSettings->SetWindowPos(&CWnd::wndTop, x, y, CCC_CAP_BTN, CCC_CAP_BTN,
            SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
        x -= (CCC_CAP_BTN + CCC_CAP_GAP);
    }
    if (e->pPin && ::IsWindow(e->pPin->GetSafeHwnd())) {
        e->pPin->SetWindowPos(&CWnd::wndTop, x, y, CCC_CAP_BTN, CCC_CAP_BTN,
            SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
    }

    // P を置いた後に、実 Pin 左端基準で「?」→ その左に「メインに追従」
    static const UINT kHelpChromeIds[] = {
        IDC_SC_HELP, IDC_PL_HELP, IDC_AN_HELP, IDC_PR_HELP, IDC_EQ_HELP,
        IDC_PT_HELP, IDC_RD_HELP, IDC_DR_HELP, IDC_WE_HELP, IDC_TC_HELP,
        IDC_TE_HELP, IDC_FD_HELP, IDC_KPI_HELP, IDC_SY_HELP, IDC_PRT_HELP,
        IDC_OGG_HELP, IDC_PRM_HELP, IDC_MCR_HELP, IDC_DOUGA_HELP, IDC_MP_CHEATBTN,
        IDC_SM_HELP, IDC_DIG_HELP, IDC_VC_HELP, IDC_TN_HELP, IDC_PF_HELP,
        IDC_S3M_HELP, IDC_S3R_HELP, IDC_MP_BPM_HELP, IDC_TB_HELP, IDC_VST_HELP, IDC_CD_HELP, IDC_MM_HELP
    };
    for (int i = 0; i < (int)_countof(kHelpChromeIds); ++i) {
        HWND hHelp = ::GetDlgItem(hDlg, kHelpChromeIds[i]);
        if (!hHelp || !::IsWindow(hHelp))
            continue;
        CWnd* pHelp = CWnd::FromHandlePermanent(hHelp);
        if (!pHelp)
            pHelp = CWnd::FromHandle(hHelp);
        if (!pHelp)
            continue;
        // 未表示でも位置だけ合わせる（初回に P の裏に残るのを防ぐ）
        if (!pHelp->IsWindowVisible())
            pHelp->ShowWindow(SW_SHOWNA);
        CCC_CaptionPlaceHelpBtn(hDlg, pHelp);
    }

    CCC_MainLockBringToFront(hDlg);
    // 追随を前面にしたあと、? が沈まないよう再度前面へ
    for (int i = 0; i < (int)_countof(kHelpChromeIds); ++i) {
        HWND hHelp = ::GetDlgItem(hDlg, kHelpChromeIds[i]);
        if (!hHelp || !::IsWindowVisible(hHelp))
            continue;
        CWnd* pHelp = CWnd::FromHandlePermanent(hHelp);
        if (!pHelp)
            pHelp = CWnd::FromHandle(hHelp);
        if (pHelp)
            pHelp->SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// CCC_ApplyDlgResourceIcon: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_ApplyDlgResourceIcon(CWnd* w, UINT iconId)
{
	if (!w || !w->GetSafeHwnd() || iconId == 0)
		return;
	HINSTANCE hi = AfxGetResourceHandle();
	HICON hBig = (HICON)::LoadImage(hi, MAKEINTRESOURCE(iconId),
		IMAGE_ICON, 32, 32, LR_SHARED);
	if (!hBig && AfxGetApp())
		hBig = AfxGetApp()->LoadIcon(iconId);
	if (!hBig)
		hBig = (HICON)::LoadImage(hi, MAKEINTRESOURCE(iconId),
			IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
	if (!hBig)
		return;
	w->SetIcon(hBig, TRUE);
	w->SetIcon(hBig, FALSE);
}

// CCC_IconIdForDialogTemplate: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
UINT CCC_IconIdForDialogTemplate(UINT idd)
{
	if (idd == 0)
		return 0;
	// MP 本窓・ファルコム本窓・プレイリスト・歌詞オーバーレイは既存アイコン／非表示のまま
	if (idd == IDD_MEDIAPLAYER || idd == IDD_OGG_DIALOG || idd == IDD_PLAYLIST
		|| idd == IDD_DESKTOP_LYRICS)
		return 0;

	static const UINT kMap[][2] = {
		{ IDD_ANALYZER, IDI_UI_ANALYZER }, { IDD_AN_HELP, IDI_UI_ANALYZER },
		{ IDD_PIANOROLL, IDI_UI_PIANO }, { IDD_PR_HELP, IDI_UI_PIANO },
		{ IDD_PIANOROLL_TUNE, IDI_UI_TUNE }, { IDD_PRT_HELP, IDI_UI_TUNE },
		{ IDD_MIDIMONITOR, IDI_UI_PIANO }, { IDD_MM_HELP, IDI_UI_PIANO },
		{ IDD_EQUALIZER, IDI_UI_EQ }, { IDD_EQ_HELP, IDI_UI_EQ },
		{ IDD_PROTOOLS, IDI_UI_TUNE }, { IDD_PT_HELP, IDI_UI_TUNE },
		{ IDD_Render, IDI_UI_RENDER }, { IDD_RD_HELP, IDI_UI_RENDER },
		{ IDD_WAVEXPORT, IDI_UI_EXPORT }, { IDD_WE_HELP, IDI_UI_EXPORT },
		{ IDD_TRANSCODE, IDI_UI_EXPORT }, { IDD_TC_HELP, IDI_UI_EXPORT },
		{ IDD_TAGEDIT, IDI_UI_TAG }, { IDD_TE_HELP, IDI_UI_TAG },
		{ IDD_TAGBATCH, IDI_UI_TAG }, { IDD_TB_HELP, IDI_UI_TAG },
		{ IDD_MP_PROMPT, IDI_UI_PROMPT }, { IDD_PRM_HELP, IDI_UI_PROMPT },
		{ IDD_DEVICERECORD, IDI_UI_MIC }, { IDD_DR_HELP, IDI_UI_MIC },
		{ IDD_VOICECHANGER, IDI_UI_MIC }, { IDD_VC_HELP, IDI_UI_MIC },
		{ IDD_TUNERPRACTICE, IDI_UI_MIC }, { IDD_TN_HELP, IDI_UI_MIC },
		{ IDD_SCREENCAPTURE, IDI_UI_CAPTURE }, { IDD_SC_HELP, IDI_UI_CAPTURE },
		{ IDD_SC_LIVESETTINGS, IDI_UI_CAPTURE },
		{ IDD_KPI, IDI_UI_FOLDER }, { IDD_KPI_HELP, IDI_UI_FOLDER },
		{ IDD_KPI5CFG, IDI_UI_FOLDER },
		{ IDD_ZEROFOL, IDI_UI_FOLDER }, { IDD_FD_HELP, IDI_UI_FOLDER },
		{ IDD_SYOSAI, IDI_UI_INFO }, { IDD_SY_HELP, IDI_UI_INFO },
		{ IDD_ABOUTBOX, IDI_UI_INFO }, { IDD_OGG_HELP, IDI_UI_INFO },
		{ IDD_DOUGA_HELP, IDI_UI_VIDEO },
		{ IDD_MP_CMDROLL, IDI_UI_KEYBOARD }, { IDD_MP_CMDROLL_HELP, IDI_UI_KEYBOARD },
		{ IDD_MP_CMDPLACE, IDI_UI_KEYBOARD }, { IDD_MP_CMDPAL, IDI_UI_KEYBOARD },
		{ IDD_SOUNDMETER, IDI_UI_METER }, { IDD_SM_HELP, IDI_UI_METER },
		{ IDD_DIGITIZE, IDI_UI_DISC }, { IDD_DIG_HELP, IDI_UI_DISC },
		{ IDD_MP_DJPAD, IDI_UI_DISC },
		{ IDD_PHOTOFRAME, IDI_UI_PHOTO }, { IDD_PF_HELP, IDI_UI_PHOTO },
		{ IDD_IMAGE, IDI_UI_PHOTO }, { IDD_IMAGEBASE, IDI_UI_PHOTO },
		{ IDD_SOFT3DMAZE, IDI_UI_MAZE }, { IDD_S3M_HELP, IDI_UI_MAZE },
		{ IDD_SOFT3DRACE, IDI_UI_RACE }, { IDD_S3R_HELP, IDI_UI_RACE },
		{ IDD_VSTHOST, IDI_UI_VST }, { IDD_VST_HELP, IDI_UI_VST }, { IDD_VST_WAIT, IDI_UI_VST },
		{ IDD_CDPLAYER, IDI_UI_DISC }, { IDD_CD_HELP, IDI_UI_DISC },
		{ IDD_MP_CHEATSHEET, IDI_UI_HELP }, { IDD_PL_HELP, IDI_UI_MUSIC },
		{ IDD_MP_ALARM, IDI_UI_ALARM },
		{ IDD_MP_REMOTE, IDI_UI_REMOTE },
		{ IDD_MP_MIRROR, IDI_UI_SHARE },
		{ IDD_MP_SSVIZ, IDI_UI_VIZ }, { IDD_GRAPH, IDI_UI_VIZ },
		{ IDD_MP_BPM, IDI_UI_MUSIC }, { IDD_PLAYLIST_NEW, IDI_UI_MUSIC },
		{ IDD_MP_QUEUE, IDI_UI_MUSIC }, { IDD_MP_SMART, IDI_UI_MUSIC },
		{ IDD_MP_M3U_IMPORT, IDI_UI_FILE },
		{ IDD_MP_FOLDER_SYNC, IDI_UI_SYNC },
		{ IDD_AUDIOSELECT, IDI_UI_AUDIO },
		{ IDD_FILENAME, IDI_UI_FILE },
		{ IDD_MODESELECT, IDI_UI_APPS },
		{ IDD_MISSING_FILES, IDI_UI_COPY },
		{ IDD_MP_DUPES, IDI_UI_COPY },
		{ IDD_MP_MBPICK, IDI_UI_MUSIC },
	};
	for (int i = 0; i < (int)(sizeof(kMap) / sizeof(kMap[0])); ++i) {
		if (kMap[i][0] == idd)
			return kMap[i][1];
	}
	return IDI_UI_APPS;
}

// CCC_ApplyWindowIconFromTemplate: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_ApplyWindowIconFromTemplate(CWnd* w, UINT idd)
{
	CCC_ApplyDlgResourceIcon(w, CCC_IconIdForDialogTemplate(idd));
}

HCURSOR CCC_LoadUiCursor(UINT id)
{
	if (id == 0)
		return NULL;
	HCURSOR h = NULL;
	if (AfxGetApp())
		h = AfxGetApp()->LoadCursor(id);
	if (!h)
		h = (HCURSOR)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(id),
			IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
	return h;
}

// CCC_SetUiCursor: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
BOOL CCC_SetUiCursor(UINT id)
{
	HCURSOR h = CCC_LoadUiCursor(id);
	if (!h)
		return FALSE;
	::SetCursor(h);
	return TRUE;
}

// CCC_LoadSharedIcon: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
HICON CCC_LoadSharedIcon(UINT iconId, int px)
{
	if (!iconId)
		return NULL;
	if (px < 12)
		px = 12;
	if (px > 36)
		px = 36;
	HICON h = (HICON)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(iconId),
		IMAGE_ICON, px, px, LR_SHARED);
	if (!h)
		h = (HICON)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(iconId),
			IMAGE_ICON, 16, 16, LR_SHARED);
	return h;
}

// CCC_CtlIconForCtrl: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
UINT CCC_CtlIconForCtrl(UINT id)
{
	if (id == 0)
		return 0;
	switch (id) {
	case IDC_CAP_CLOSE: return IDI_CTL_CLOSE;
	case IDC_CAP_MIN: return IDI_CTL_MIN;
	case IDC_CAP_MAX: return IDI_CTL_MAX;
	case IDC_CAP_SETTINGS: return IDI_CTL_COG;
	case IDC_CAP_PIN: return IDI_CTL_PIN;
	case IDC_CAP_OFFLINE_HELP: return IDI_CTL_BOOK;
	case IDC_MP_PLAY: return IDI_CTL_PLAY;
	case IDC_MP_PAUSE: return IDI_CTL_PAUSE;
	case IDC_MP_STOP: return IDI_CTL_STOP;
	case IDC_MP_NEXT: return IDI_CTL_NEXT;
	case IDC_MP_PREV: return IDI_CTL_PREV;
	case IDC_CD_PLAY: return IDI_CTL_PLAY;
	case IDC_CD_PAUSE: return IDI_CTL_PAUSE;
	case IDC_CD_STOP: return IDI_CTL_STOP;
	case IDC_CD_NEXT: return IDI_CTL_NEXT;
	case IDC_CD_PREV: return IDI_CTL_PREV;
	case IDC_CD_CLOSE: return IDI_CTL_CLOSE;
	case IDC_CD_REFRESH: return IDI_CTL_REFRESH;
	case IDC_MP_EQ: return IDI_UI_EQ;
	case IDC_MP_PIANO: return IDI_UI_PIANO;
	case IDC_MP_ANALYZER: return IDI_UI_ANALYZER;
	case IDC_MP_PRO: return IDI_UI_TUNE;
	case IDC_MP_PROMPT: return IDI_UI_PROMPT;
	case IDC_MP_CMDROLL: return IDI_UI_KEYBOARD;
	case IDC_MP_SETTINGS: return IDI_CTL_COG;
	case IDC_MP_FOLDER: return IDI_UI_FOLDER;
	case IDC_MP_JACK:
	case IDC_MP_JACKET: return IDI_UI_PHOTO;
	case IDC_MP_EXIT: return IDI_CTL_POWER;
	case IDC_MP_FADEOUT: return IDI_CTL_FADE;
	case IDC_MP_RECORD: return IDI_CTL_RECORD;
	case IDC_MP_CAPTURE: return IDI_UI_CAPTURE;
	case IDC_MP_MICMIX: return IDI_UI_MIC;
	case IDC_MP_LOOP: return IDI_CTL_LOOP;
	case IDC_MP_RANDOM: return IDI_CTL_SHUFFLE;
	case IDC_MP_XFADE: return IDI_CTL_XFADE;
	case IDC_MP_SEEKLOCK: return IDI_CTL_LOCK;
	case IDC_MP_FIND: return IDI_CTL_SEARCH;
	case IDC_MP_ITEMDEL:
	case IDC_MP_PLDELETE: return IDI_CTL_DELETE;
	case IDC_MP_LSUP: return IDI_CTL_CHEVTOP;
	case IDC_MP_UP:
	case IDC_MP_FINDUP: return IDI_CTL_CHEVUP;
	case IDC_MP_LSDOWN: return IDI_CTL_CHEVBOTTOM;
	case IDC_MP_DOWN:
	case IDC_MP_FINDDOWN: return IDI_CTL_CHEVDOWN;
	case IDC_MCR_ZOOMIN: return IDI_CTL_PLUS;
	case IDC_MCR_ZOOMOUT: return IDI_CTL_MINUS;
	case IDC_DJPAD_PLAY: return IDI_CTL_PLAY;
	case IDC_DJPAD_PAUSE: return IDI_CTL_PAUSE;
	case IDC_DJPAD_STOP: return IDI_CTL_STOP;
	case IDC_DJPAD_PREV: return IDI_CTL_PREV;
	case IDC_DJPAD_NEXT: return IDI_CTL_NEXT;
	case IDC_DJPAD_PITCH_UP:
	case IDC_DJPAD_TEMPO_UP: return IDI_CTL_PLUS;
	case IDC_DJPAD_PITCH_DN:
	case IDC_DJPAD_TEMPO_DN: return IDI_CTL_MINUS;
	case IDC_DJPAD_ABA:
	case IDC_DJPAD_ABB:
	case IDC_DJPAD_ABCLR: return IDI_CTL_AB;
	case IDC_DJPAD_LOOP1:
	case IDC_DJPAD_LOOP2:
	case IDC_DJPAD_LOOP4:
	case IDC_DJPAD_LOOP8: return IDI_CTL_LOOP;
	case IDC_MP_M3U_EXPORT: return IDI_UI_EXPORT;
	case IDC_MP_M3U_IMPORT: return IDI_UI_FILE;
	case IDC_MP_ADDFOLDER: return IDI_CTL_PLUS;
	case IDC_MP_BOT_VST: return IDI_UI_VST;
	case IDC_MP_BOT_MIDI: return IDI_UI_PIANO;
	case IDC_MP_BOT_CD: return IDI_UI_DISC;
	case IDC_MP_BOT_MAZE: return IDI_UI_MAZE;
	case IDC_MP_BOT_RACE: return IDI_UI_RACE;
	case IDC_MP_BOT_DJ: return IDI_UI_DISC;
	case IDC_MP_BOT_TAG: return IDI_UI_TAG;
	case IDC_MP_BOT_BPM: return IDI_UI_MUSIC;
	case IDC_MP_BOT_SLEEP: return IDI_CTL_SLEEP;
	case IDC_MP_BOT_MIRROR: return IDI_UI_SHARE;
	case IDC_MP_BOT_SSVIZ: return IDI_UI_VIZ;
	case IDC_MP_BOT_ALARM: return IDI_UI_ALARM;
	case IDC_MP_BOT_REMOTE: return IDI_UI_REMOTE;
	case IDC_MP_DESKLRC: return IDI_UI_KEYBOARD;
	case IDC_MP_SWITCHMODE: return IDI_UI_APPS;
	case IDC_MP_ABA:
	case IDC_MP_ABB:
	case IDC_MP_ABCLR:
	case IDC_CD_ABA:
	case IDC_CD_ABB:
	case IDC_CD_ABCLR: return IDI_CTL_AB;
	case IDC_MP_SUPE: return IDI_UI_ANALYZER;
	case IDC_S3M_GEN:
	case IDC_S3R_GEN: return IDI_CTL_PLUS;
	case IDC_S3R_START: return IDI_CTL_PLAY;
	case IDC_S3M_CLOSE:
	case IDC_S3R_CLOSE:
	case IDC_MP_BPM_CLOSE: return IDI_CTL_CLOSE;
	case IDC_S3M_NAVI: return IDI_CTL_CHEVRIGHT;
	case IDC_MP_MICDEV_REFRESH:
	case IDC_MP_LOOPDEV_REFRESH:
	case IDC_OGG_MICDEV_REFRESH:
	case IDC_DR_MICDEV_REFRESH:
	case IDC_SC_MICDEV_REFRESH:
	case IDC_DJPAD_MICDEV_REFRESH:
	case IDC_COMBO_MICDEV_REFRESH:
	case IDC_SM_MIC_REFRESH:
	case IDC_VC_MIC_REFRESH:
	case IDC_TN_MIC_REFRESH:
	case IDC_DIG_CAP_REFRESH:
	case IDC_VC_OUT_REFRESH:
	case IDC_TN_OUT_REFRESH:
	case IDC_DIG_MON_REFRESH:
	case IDC_SC_REFRESH: return IDI_CTL_REFRESH;
	case IDC_SC_HELP:
	case IDC_PL_HELP:
	case IDC_AN_HELP:
	case IDC_PR_HELP:
	case IDC_EQ_HELP:
	case IDC_PT_HELP:
	case IDC_RD_HELP:
	case IDC_DR_HELP:
	case IDC_WE_HELP:
	case IDC_TC_HELP:
	case IDC_TE_HELP:
	case IDC_FD_HELP:
	case IDC_KPI_HELP:
	case IDC_SY_HELP:
	case IDC_PRT_HELP:
	case IDC_OGG_HELP:
	case IDC_PRM_HELP:
	case IDC_MCR_HELP:
	case IDC_DOUGA_HELP:
	case IDC_MP_CHEATBTN:
	case IDC_SM_HELP:
	case IDC_DIG_HELP:
	case IDC_VC_HELP:
	case IDC_TN_HELP:
	case IDC_PF_HELP:
	case IDC_S3M_HELP:
	case IDC_S3R_HELP:
	case IDC_MP_BPM_HELP:
	case IDC_TB_HELP:
	case IDC_VST_HELP:
	case IDC_CD_HELP:
	case IDC_MM_HELP: return IDI_UI_HELP;
	default: return 0;
	}
}

// CCC_CaptionIsGlyphOnly: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_CaptionIsGlyphOnly(const CString& s)
{
	if (s.IsEmpty())
		return TRUE;
	const int n = s.GetLength();
	if (n > 3)
		return FALSE;
	for (int i = 0; i < n; ++i) {
		const WCHAR c = s[i];
		if ((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'))
			return FALSE;
		if (c >= 0x3040) // かな・漢字はラベル
			return FALSE;
	}
	return TRUE;
}

// CCC_CaptionApplySharedIcon: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionApplySharedIcon(CCustomStandardButton* p, UINT iconId)
{
	if (!p || !p->GetSafeHwnd() || !iconId)
		return;
	p->SetWindowText(L"");
	HICON h = CCC_LoadSharedIcon(iconId, 24);
	if (h)
		p->SetIcon(h, NULL);
}

// カスタムキャプション用アイコン取得。
// WM_GETICON が NULL なら描かない（クラス／exe 既定へフォールバックしない）。
// ツール窓は SetIcon したリソースだけを出す。歌詞オーバーレイは非表示。
// 帯へは ICON_BIG（32）を優先。SMALL だと 16 を引き伸ばすことになる。
static HICON CCC_CaptionGetTitleIcon(HWND hDlg)
{
	if (!hDlg || !::IsWindow(hDlg))
		return NULL;
	CWnd* pWnd = CWnd::FromHandlePermanent(hDlg);
	if (pWnd && pWnd->GetRuntimeClass() && pWnd->GetRuntimeClass()->m_lpszClassName
		&& strcmp(pWnd->GetRuntimeClass()->m_lpszClassName, "CDesktopLyricsWnd") == 0)
		return NULL;
	HICON hIcon = (HICON)::SendMessage(hDlg, WM_GETICON, ICON_BIG, 0);
	if (!hIcon)
		hIcon = (HICON)::SendMessage(hDlg, WM_GETICON, ICON_SMALL, 0);
	return hIcon;
}

// 帯いっぱい(barH-2)は大きすぎ、旧16は小さい。96dpi 帯32なら 24。
// CCC_CaptionTitleIconSize: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static int CCC_CaptionTitleIconSize(int barH)
{
	int isz = (barH * 3) / 4;
	if (isz < 16)
		isz = 16;
	if (isz > barH - 6)
		isz = barH - 6;
	if (isz < 16)
		isz = 16;
	return isz;
}

// カスタム帯。本文だけの再描画（60fps）では帯クリップ外なら触らない。
// アクリル帯は ClearRect(α=0)+タイトル。EnsureBackdrop は毎フレーム呼ばない。
// ClearRect はボタン画素も消すので最後に ChromeNow で載せ直す。
void CCC_CaptionPaint(CDC& dc, HWND hDlg)
{
    CCC_CaptionEntry* e = CCC_FindCaption(hDlg);
    if (!e || !e->installed || !::IsWindow(hDlg))
        return;
    CRect cr;
    ::GetClientRect(hDlg, &cr);
    CRect bar(0, 0, cr.right, e->height);
    if (bar.Width() <= 0 || bar.Height() <= 0)
        return;

    // 本文だけの再描画では帯に触らない（ピアノ/アナライザ演奏中のちらつき・負荷の主因）
    {
        CRect clip;
        if (dc.GetClipBox(&clip) != ERROR && !clip.IsRectEmpty()) {
            CRect hit;
            if (!hit.IntersectRect(&clip, &bar))
                return;
        }
    }

    const BOOL bAcrylicCap = CCC_AcrylicCaption(hDlg);
    const BOOL active = (::GetForegroundWindow() == hDlg)
        || (::GetActiveWindow() == hDlg)
        || (::GetAncestor(hDlg, GA_ROOT) && ::GetForegroundWindow() == ::GetAncestor(hDlg, GA_ROOT));
    const COLORREF bgSolid = active ? CCC_CAP_BG : CCC_CAP_BG_INACTIVE;

    HICON hIcon = CCC_CaptionGetTitleIcon(hDlg);
    wchar_t title[512];
    title[0] = 0;
    ::GetWindowTextW(hDlg, title, 511);
    int titleRight = bar.right;
    CCC_CaptionGetTitleRight(hDlg, e, titleRight);
    const BOOL hadIcon = hIcon ? TRUE : FALSE;

    // 本文込みの再描画（ピアノ/MP/アナライザの60fps）では帯の見た目が同じなら触らない。
    // 帯だけの Invalidate（活性切替・タイトル変更）は clip が帯内なので描く。
    BOOL clipBody = TRUE;
    {
        CRect clip;
        if (dc.GetClipBox(&clip) != ERROR && !clip.IsRectEmpty()) {
            if (clip.bottom <= bar.bottom + 1)
                clipBody = FALSE;
        }
    }
    if (clipBody && e->paintValid
        && e->paintW == bar.Width() && e->paintH == bar.Height()
        && e->paintActive == active && e->paintHadIcon == hadIcon
        && e->paintTitleRight == titleRight
        && wcscmp(e->paintTitle, title) == 0)
        return;

#if CCUSTOM_AERO_SUPPORT
    if (bAcrylicCap) {
        // 帯ガラス: ClearRect(α=0) + タイトル。EnsureBackdrop は毎フレーム呼ばない（ちらつき源）。
        // 本文 MakeOpaque は呼び出し側（PaintOpaqueBody / BlitStretchOpaque）が行う。
        // ClearRect は帯上のボタン画素も消すので、最後に ChromeNow で載せ直す。
        RECT rcBar = bar;
        CCC_ClearRectChroma(dc.GetSafeHdc(), rcBar, CCC_AERO_CHROMA_KEY);

        const int w = bar.Width();
        const int h = bar.Height();
        static CCC_ChromaBlitCache s_capDib;
        if (s_capDib.Ensure(dc.GetSafeHdc(), w, h) && s_capDib.pBits && s_capDib.hdcDib) {
            ::ZeroMemory(s_capDib.pBits, static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
            HDC hdcMem = s_capDib.hdcDib;

            int textLeft = 8;
            // WM_GETICON が載っている窓だけ描く（未設定は NULL）
            if (hIcon) {
                const int isz = CCC_CaptionTitleIconSize(h);
                const int iy = (h - isz) / 2;
                ::DrawIconEx(hdcMem, 6, iy, hIcon, isz, isz, 0, NULL, DI_NORMAL);
                textLeft = 6 + isz + 6;
            }

            RECT textRc = { textLeft, 0, titleRight - 4, h };
            CCC_DrawCaptionTitleComposited(hdcMem, hDlg, title, &textRc, active);

            // DrawIconEx 等が α=0 のまま残す画素を、非ゼロ RGB だけ不透明化
            {
                UINT32* px = static_cast<UINT32*>(s_capDib.pBits);
                const int n = w * h;
                int i = 0;
                for (; i + 3 < n; i += 4) {
                    UINT32 p0 = px[i], p1 = px[i + 1], p2 = px[i + 2], p3 = px[i + 3];
                    if ((p0 & 0x00FFFFFFu) != 0 && (p0 >> 24) == 0) px[i] = p0 | 0xFF000000u;
                    if ((p1 & 0x00FFFFFFu) != 0 && (p1 >> 24) == 0) px[i + 1] = p1 | 0xFF000000u;
                    if ((p2 & 0x00FFFFFFu) != 0 && (p2 >> 24) == 0) px[i + 2] = p2 | 0xFF000000u;
                    if ((p3 & 0x00FFFFFFu) != 0 && (p3 >> 24) == 0) px[i + 3] = p3 | 0xFF000000u;
                }
                for (; i < n; ++i) {
                    UINT32 p = px[i];
                    if ((p & 0x00FFFFFFu) != 0 && (p >> 24) == 0)
                        px[i] = p | 0xFF000000u;
                }
            }

            // ClearRect 済みの帯へ直接合成（第2 BeginBufferedPaint を避ける）
            const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            if (!::GdiAlphaBlend(dc.GetSafeHdc(), 0, 0, w, h, hdcMem, 0, 0, w, h, bf)) {
                BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
                HDC hdcBuf = NULL;
                HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &rcBar, BPBF_TOPDOWNDIB, &params, &hdcBuf);
                if (hdcBuf && hBP) {
                    CCC_InitBPClear(hBP, w, h);
                    ::GdiAlphaBlend(hdcBuf, rcBar.left, rcBar.top, w, h, hdcMem, 0, 0, w, h, bf);
                    ::EndBufferedPaint(hBP, TRUE);
                }
            }
            CCC_CaptionPaintChromeNow(hDlg);
            e->paintValid = TRUE;
            e->paintActive = active;
            e->paintHadIcon = hadIcon;
            e->paintW = bar.Width();
            e->paintH = bar.Height();
            e->paintTitleRight = titleRight;
            for (int ti = 0; ti < 511; ++ti) {
                e->paintTitle[ti] = title[ti];
                if (!title[ti]) break;
            }
            e->paintTitle[511] = 0;
        }
        else {
            CCC_CaptionPaintChromeNow(hDlg);
        }
        return;
    }
#endif

    CDC mem;
    CBitmap bmp;
    mem.CreateCompatibleDC(&dc);
    bmp.CreateCompatibleBitmap(&dc, bar.Width(), bar.Height());
    CBitmap* old = mem.SelectObject(&bmp);
    mem.FillSolidRect(0, 0, bar.Width(), bar.Height(), bgSolid);
    mem.FillSolidRect(0, bar.Height() - 1, bar.Width(), 1, RGB(90, 70, 110));

    int textLeft = 8;
    if (hIcon) {
        const int isz = CCC_CaptionTitleIconSize(bar.Height());
        const int iy = (bar.Height() - isz) / 2;
        ::DrawIconEx(mem.GetSafeHdc(), 6, iy, hIcon, isz, isz, 0, NULL, DI_NORMAL);
        textLeft = 6 + isz + 6;
    }

    CRect textRc(textLeft, 0, titleRight - 4, bar.Height());
    mem.SetBkMode(TRANSPARENT);
    mem.SetTextColor(CCC_CAP_TEXT);
    CWnd* pDlg = CWnd::FromHandlePermanent(hDlg);
    if (!pDlg)
        pDlg = CWnd::FromHandle(hDlg);
    CFont* pOldFont = nullptr;
    if (pDlg && pDlg->GetFont())
        pOldFont = mem.SelectObject(pDlg->GetFont());
    mem.DrawText(title, textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (pOldFont)
        mem.SelectObject(pOldFont);

    dc.BitBlt(0, 0, bar.Width(), bar.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(old);
    e->paintValid = TRUE;
    e->paintActive = active;
    e->paintHadIcon = hadIcon;
    e->paintW = bar.Width();
    e->paintH = bar.Height();
    e->paintTitleRight = titleRight;
    for (int ti = 0; ti < 511; ++ti) {
        e->paintTitle[ti] = title[ti];
        if (!title[ti]) break;
    }
    e->paintTitle[511] = 0;
}

// CCC_CaptionIsRenderClass: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_CaptionIsRenderClass(CWnd* pDlg)
{
    if (!pDlg || !pDlg->GetRuntimeClass())
        return FALSE;
    return strcmp(pDlg->GetRuntimeClass()->m_lpszClassName, "CRender") == 0;
}

// システム WS_CAPTION を描画から外し、クライアント先頭帯に min/max/close/pin/help。
// FRAMECHANGED は ExtendFrame より先。後だとマージンが消えて黒帯＋縦幅ジャンプ。
// NC 吸収分だけ子を下げる。capH で上乗せすると内容が余分に下がって見える。
// 二重 install 禁止（installed なら return）。
static void CCC_CaptionInstallCore(CWnd* pDlg, CToolTipCtrl* pTip); // MainLock 定義後に実装

// ============================================================================
// メインウィンドウ位置ロック (サブウィンドウが COggDlg / CMediaPlayerDlg に追随)
// 通常ダイアログ: 子 CCustomCheckBox / GDI全画面(ピアノロール等): オーバーレイ描画
// カスタムキャプション時はキャプション帯へ子チェックを置く（オーバーレイも子へ切替）
// ============================================================================
struct CCC_MainLockOverlayCache {
    CDC dc;
    CBitmap bmp;
    CBitmap* oldBmp = nullptr;
    int w = 0;
    int h = 0;
    BOOL dirty = TRUE;
};

struct CCC_MainLockEntry {
    HWND hWnd = NULL;
    CCustomCheckBox* pLockBtn = nullptr;
    int offsetX = 0;
    int offsetY = 0;
    // 0=メイン左上相対 / 1=メイン右外側に密着 / 2=メイン左外側に密着
    int dockH = 0;
    // 0=メイン左上相対 / 1=メイン下外側に密着 / 2=メイン上外側に密着
    int dockV = 0;
    int gapX = 0;
    int gapY = 0;
    int* pSaveFlag = nullptr;
    BOOL locked = FALSE;
    BOOL overlayPaint = FALSE;
    int headerRowTop = -1;
    int headerRowH = 0;
    CCC_MainLockOverlayCache* pOverlay = nullptr;
};

static CCC_MainLockEntry g_mainLocks[16];
static int g_mainLockCount = 0;
static BOOL g_mainLockInternalMove = FALSE;
static DWORD g_mainLockQuickPresentUntil = 0;

static const int CCC_MAINLOCK_H = 20;
static const int CCC_MAINLOCK_MIN_W = 128;
static const int CCC_MAINLOCK_MARGIN = 10;

static void CCC_MainLockLayoutBtn(HWND hDlg);
static void CCC_MainLockInvalidateOverlay(HWND hDlg);

// CCC_ClampWindowPos: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_ClampWindowPos(int& x, int& y, int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    RECT wr;
    wr.left = x;
    wr.top = y;
    wr.right = x + w;
    wr.bottom = y + h;
    // いずれかのモニタに載っていればそのまま(左/上サブモニタの負座標も可)
    if (::MonitorFromRect(&wr, MONITOR_DEFAULTTONULL))
        return;
    HMONITOR hMon = ::MonitorFromRect(&wr, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    RECT rcWork;
    if (hMon && ::GetMonitorInfo(hMon, &mi))
        rcWork = mi.rcWork;
    else if (!::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0))
        return;
    int nw = w;
    int nh = h;
    const int mw = rcWork.right - rcWork.left;
    const int mh = rcWork.bottom - rcWork.top;
    if (nw > mw) nw = mw;
    if (nh > mh) nh = mh;
    if (x < rcWork.left) x = rcWork.left;
    if (y < rcWork.top) y = rcWork.top;
    if (x + nw > rcWork.right) x = rcWork.right - nw;
    if (y + nh > rcWork.bottom) y = rcWork.bottom - nh;
}

// CCC_MainLockReleaseOverlayCache: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MainLockReleaseOverlayCache(CCC_MainLockEntry* e)
{
    if (!e || !e->pOverlay)
        return;
    CCC_MainLockOverlayCache* p = e->pOverlay;
    if (p->dc.GetSafeHdc()) {
        if (p->oldBmp)
            p->dc.SelectObject(p->oldBmp);
        p->dc.DeleteDC();
    }
    p->bmp.DeleteObject();
    delete p;
    e->pOverlay = nullptr;
}

// CCC_MainLockEnsureOverlayCachePtr: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static CCC_MainLockOverlayCache* CCC_MainLockEnsureOverlayCachePtr(CCC_MainLockEntry* e)
{
    if (!e)
        return nullptr;
    if (!e->pOverlay)
        e->pOverlay = new (std::nothrow) CCC_MainLockOverlayCache();
    return e->pOverlay;
}

// CCC_MainLockMarkOverlayDirty: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MainLockMarkOverlayDirty(CCC_MainLockEntry* e)
{
    if (e && e->pOverlay)
        e->pOverlay->dirty = TRUE;
}

// CCC_FindMainLockEntry: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static CCC_MainLockEntry* CCC_FindMainLockEntry(HWND hWnd)
{
    for (int i = 0; i < g_mainLockCount; ++i) {
        if (g_mainLocks[i].hWnd == hWnd)
            return &g_mainLocks[i];
    }
    return nullptr;
}

// CCC_GetOrCreateMainLockEntry: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static CCC_MainLockEntry* CCC_GetOrCreateMainLockEntry(HWND hWnd)
{
    if (CCC_MainLockEntry* e = CCC_FindMainLockEntry(hWnd))
        return e;
    if (g_mainLockCount >= (int)_countof(g_mainLocks))
        return nullptr;
    CCC_MainLockEntry* e = &g_mainLocks[g_mainLockCount++];
    e->hWnd = hWnd;
    return e;
}

// 子窓の位置から「横/上/下に並んでいるか」を見て密着モードを決める。
// リサイズ時も隙間を保つため、単純な左上相対だけでは足りない。
static void CCC_ComputeMainLockAttach(HWND hWnd, CCC_MainLockEntry* e)
{
    if (!e) return;
    e->offsetX = e->offsetY = 0;
    e->dockH = e->dockV = 0;
    e->gapX = e->gapY = 0;
    CWnd* pMain = CCC_GetActiveMainWindow();
    if (!pMain || !::IsWindow(hWnd))
        return;
    CRect mainRc, selfRc;
    pMain->GetWindowRect(&mainRc);
    ::GetWindowRect(hWnd, &selfRc);
    e->offsetX = selfRc.left - mainRc.left;
    e->offsetY = selfRc.top - mainRc.top;
    // ほぼ外側に出ている辺を密着とみなす(重なり8pxまでは許容)
    if (selfRc.left >= mainRc.right - 8) {
        e->dockH = 1;
        e->gapX = selfRc.left - mainRc.right;
    }
    else if (selfRc.right <= mainRc.left + 8) {
        e->dockH = 2;
        e->gapX = mainRc.left - selfRc.right;
    }
    if (selfRc.top >= mainRc.bottom - 8) {
        e->dockV = 1;
        e->gapY = selfRc.top - mainRc.bottom;
    }
    else if (selfRc.bottom <= mainRc.top + 8) {
        e->dockV = 2;
        e->gapY = mainRc.top - selfRc.bottom;
    }
}

// CCC_MainLockPlaceChild: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MainLockPlaceChild(CCC_MainLockEntry& e, const RECT* pMainRect)
{
    if (!pMainRect || !::IsWindow(e.hWnd))
        return;
    // 最大化中の子は動かさない（最大化窓は移動不可／追随対象外）
    if (::IsZoomed(e.hWnd))
        return;
    CRect selfRc;
    ::GetWindowRect(e.hWnd, &selfRc);
    const int w = selfRc.Width();
    const int h = selfRc.Height();
    int x = pMainRect->left + e.offsetX;
    int y = pMainRect->top + e.offsetY;
    if (e.dockH == 1)
        x = pMainRect->right + e.gapX;
    else if (e.dockH == 2)
        x = pMainRect->left - w - e.gapX;
    if (e.dockV == 1)
        y = pMainRect->bottom + e.gapY;
    else if (e.dockV == 2)
        y = pMainRect->top - h - e.gapY;
    CCC_ClampWindowPos(x, y, w, h);
    ::SetWindowPos(e.hWnd, NULL, x, y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
}

// CCC_MainLockLabel: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static const CString& CCC_MainLockLabel()
{
    // 「メイン固定」だと何を固定するか不明瞭 → メイン窓への位置追従だと分かる文言へ
    static const CString s = LL14(
        L"メインに追従", L"Follow main", L"Suivre la fenetre principale",
        L"Segui finestra principale", L"Seguir ventana principal", L"메인 따라가기",
        L"跟随主窗口", L"متابعة النافذة الرئيسية", L"Следовать за главным",
        L"Hauptfenster folgen", L"Seguir janela principal", L"Volg hoofdvenster",
        L"Podazaj za glownym", L"Ana pencereyi takip et");
    return s;
}

// CCC_MainLockMeasureWidth: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static int CCC_MainLockMeasureWidth(CWnd* pDlg)
{
    if (!pDlg || !::IsWindow(pDlg->GetSafeHwnd()))
        return CCC_MAINLOCK_MIN_W;
    CClientDC dc(pDlg);
    CFont* pFont = pDlg->GetFont();
    CFont* pOld = pFont ? dc.SelectObject(pFont) : NULL;
    const CString& label = CCC_MainLockLabel();
    CSize sz(0, 0);
    ::GetTextExtentPoint32W(dc.GetSafeHdc(), label, label.GetLength(), &sz);
    if (pOld)
        dc.SelectObject(pOld);
    return max(CCC_MAINLOCK_MIN_W, 20 + sz.cx + 8);
}

// CCC_MainLockGetClientRect: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MainLockGetClientRect(HWND hDlg, CRect& rc)
{
    rc.SetRectEmpty();
    CWnd* pDlg = CWnd::FromHandlePermanent(hDlg);
    if (!pDlg)
        pDlg = CWnd::FromHandle(hDlg);
    if (!pDlg || !::IsWindow(hDlg))
        return;
    CRect cr;
    pDlg->GetClientRect(&cr);
    const int w = CCC_MainLockMeasureWidth(pDlg);
    const int capH = CCC_GetCustomCaptionHeight(hDlg);
    if (capH > 0) {
        // キャプション帯: [追従][本][?][P][⚙?][Min?][Max?][×]
        // 右端は「本」(無ければ「?」)左端 − gap。無ければ Pin 左端 − gap − 予約。
        CCC_CaptionEntry* ce = CCC_FindCaption(hDlg);
        int right = 0;
        if (ce && ce->pOfflineHelp && ::IsWindow(ce->pOfflineHelp->GetSafeHwnd())) {
            CRect orc;
            ce->pOfflineHelp->GetWindowRect(&orc);
            ::ScreenToClient(hDlg, &orc.TopLeft());
            ::ScreenToClient(hDlg, &orc.BottomRight());
            if (orc.Width() == CCC_CAP_BTN && orc.Height() == CCC_CAP_BTN && orc.left > 0)
                right = orc.left - CCC_CAP_GAP;
        }
        HWND hHelp = CCC_FindCaptionHelpChrome(hDlg);
        if (right <= 0 && hHelp) {
            CRect hr;
            ::GetWindowRect(hHelp, &hr);
            ::ScreenToClient(hDlg, &hr.TopLeft());
            ::ScreenToClient(hDlg, &hr.BottomRight());
            // まだ RC 配置のまま（幅が CAP でない）なら予約幅で見積もる
            if (hr.Width() == CCC_CAP_BTN && hr.Height() == CCC_CAP_BTN && hr.left > 0) {
                right = hr.left - CCC_CAP_GAP;
            }
        }
        if (right <= 0) {
            if (ce && ce->installed)
                right = CCC_CaptionPinLeft(hDlg, ce) - CCC_CAP_GAP
                    - CCC_CaptionHelpChromeReserve(hDlg);
            else
                right = cr.right - CCC_CAP_RIGHT_MARGIN - CCC_CAP_BTN - CCC_CAP_GAP
                    - CCC_CaptionHelpChromeReserve(hDlg);
        }
        if (right < 36)
            right = 36;
        int left = right - w;
        if (left < 28)
            left = 28;
        if (left > right - 8)
            left = max(28, right - 8);
        rc.left = left;
        rc.right = right;
        if (rc.right <= rc.left)
            rc.right = rc.left + 8;
        rc.top = (capH - CCC_MAINLOCK_H) / 2;
        if (rc.top < 1)
            rc.top = 1;
        rc.bottom = rc.top + CCC_MAINLOCK_H;
        return;
    }
    int left = cr.right - w - CCC_MAINLOCK_MARGIN;
    if (left < 4)
        left = 4;
    int right = left + w;
    if (right > cr.right - 4) {
        right = cr.right - 4;
        left = max(4, right - w);
    }
    rc.left = left;
    rc.right = right;
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (e && e->headerRowTop >= 0) {
        rc.top = e->headerRowTop;
        rc.bottom = rc.top + max(e->headerRowH, CCC_MAINLOCK_H);
    }
    else {
        rc.top = 4;
        rc.bottom = rc.top + CCC_MAINLOCK_H;
    }
}

// CCC_MainLockInvalidateOverlay: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MainLockInvalidateOverlay(HWND hDlg)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (e)
        CCC_MainLockMarkOverlayDirty(e);
    CRect rc;
    CCC_MainLockGetClientRect(hDlg, rc);
    if (!rc.IsRectEmpty())
        ::InvalidateRect(hDlg, rc, FALSE);
}

// CCC_MainLockSetHeaderRow: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_MainLockSetHeaderRow(HWND hDlg, int top, int height)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e)
        return;
    e->headerRowTop = top;
    e->headerRowH = max(height, CCC_MAINLOCK_H);
    if (e->overlayPaint)
        CCC_MainLockInvalidateOverlay(hDlg);
    else
        CCC_MainLockLayoutBtn(hDlg);
}

// CCC_MainLockClearHeaderRow: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_MainLockClearHeaderRow(HWND hDlg)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || e->headerRowTop < 0)
        return;
    e->headerRowTop = -1;
    e->headerRowH = 0;
    if (e->overlayPaint)
        CCC_MainLockInvalidateOverlay(hDlg);
    else
        CCC_MainLockLayoutBtn(hDlg);
}

// CCC_MainLockGetOverlayRect: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_MainLockGetOverlayRect(HWND hDlg, CRect& rc)
{
    CCC_MainLockGetClientRect(hDlg, rc);
}

// CCC_InvalidateRectMinusOverlay: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_InvalidateRectMinusOverlay(HWND hDlg, const CRect& area)
{
    // ロック矩形を更新領域から除外する。毎フレームの content blit → ロック再描画が
    // アクリル面で2段合成になり「メインに追従」がちらつくのを防ぐ。
    // ロック自体は状態変更時の InvalidateOverlay、および OnPaint 末尾の焼き込みで描く。
    if (!::IsWindow(hDlg) || area.IsRectEmpty())
        return;
    CRect lockRc;
    CCC_MainLockGetClientRect(hDlg, lockRc);
    if (lockRc.IsRectEmpty()) {
        ::InvalidateRect(hDlg, area, FALSE);
        return;
    }
    CRect overlap;
    if (!overlap.IntersectRect(&area, &lockRc)) {
        ::InvalidateRect(hDlg, area, FALSE);
        return;
    }
    HRGN rArea = ::CreateRectRgnIndirect(&area);
    HRGN rLock = ::CreateRectRgnIndirect(&lockRc);
    if (rArea && rLock) {
        ::CombineRgn(rArea, rArea, rLock, RGN_DIFF);
        ::InvalidateRgn(hDlg, rArea, FALSE);
    }
    else {
        ::InvalidateRect(hDlg, area, FALSE);
    }
    if (rArea) ::DeleteObject(rArea);
    if (rLock) ::DeleteObject(rLock);
}

// CCC_MainLockSyncBtnCheck: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MainLockSyncBtnCheck(CCC_MainLockEntry* e)
{
    if (!e || e->overlayPaint || !e->pLockBtn || !::IsWindow(e->pLockBtn->GetSafeHwnd()))
        return;
    e->pLockBtn->SetCheck(e->locked ? BST_CHECKED : BST_UNCHECKED);
}

// CCC_ApplyMainLockState: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_ApplyMainLockState(CCC_MainLockEntry* e, BOOL locked)
{
    if (!e)
        return;
    e->locked = locked;
    if (e->pSaveFlag)
        *e->pSaveFlag = locked ? 1 : 0;
    if (locked)
        CCC_ComputeMainLockAttach(e->hWnd, e);
    if (e->overlayPaint)
        CCC_MainLockInvalidateOverlay(e->hWnd);
    else
        CCC_MainLockSyncBtnCheck(e);
}

// CCC_MainLockDestroyBtn: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MainLockDestroyBtn(CCC_MainLockEntry* e)
{
    if (!e || !e->pLockBtn)
        return;
    if (::IsWindow(e->pLockBtn->GetSafeHwnd()))
        e->pLockBtn->DestroyWindow();
    e->pLockBtn = nullptr;
}

// CCC_MainLockEnsureBtn: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MainLockEnsureBtn(CWnd* pDlg, CCC_MainLockEntry* e)
{
    if (!pDlg || !e || !e->pSaveFlag || e->overlayPaint)
        return;
    if (e->pLockBtn && ::IsWindow(e->pLockBtn->GetSafeHwnd()))
        return;

    e->pLockBtn = new CCustomCheckBox();
    e->pLockBtn->EnableAutoDelete(TRUE);
    // 追従チェックは save.aero に関係なく Win11 ならアクリル透過描画
#if CCUSTOM_AERO_SUPPORT
    e->pLockBtn->SetAeroMode(CCC_AcrylicCaption(pDlg->GetSafeHwnd()) ? TRUE : FALSE);
#else
    e->pLockBtn->SetAeroMode(FALSE);
#endif

    const int w = CCC_MainLockMeasureWidth(pDlg);
    CRect rc(0, 0, w, CCC_MAINLOCK_H);
    if (!e->pLockBtn->Create(CCC_MainLockLabel(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            rc, pDlg, IDC_MAINWIN_LOCK))
    {
        delete e->pLockBtn;
        e->pLockBtn = nullptr;
        return;
    }

    if (CFont* pFont = pDlg->GetFont())
        e->pLockBtn->SetFont(pFont);
    CCC_MainLockSyncBtnCheck(e);
}

// CCC_MainLockLayoutBtn: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MainLockLayoutBtn(HWND hDlg)
{
    if (g_mainLockInternalMove)
        return;

    CWnd* pDlg = CWnd::FromHandlePermanent(hDlg);
    if (!pDlg)
        pDlg = CWnd::FromHandle(hDlg);
    if (!pDlg || !::IsWindow(hDlg))
        return;

    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pSaveFlag || e->overlayPaint)
        return;

    CCC_MainLockEnsureBtn(pDlg, e);
    if (!e->pLockBtn || !::IsWindow(e->pLockBtn->GetSafeHwnd()))
        return;

    CRect rc;
    CCC_MainLockGetClientRect(hDlg, rc);
    e->pLockBtn->SetWindowPos(&CWnd::wndTop, rc.left, rc.top, rc.Width(), rc.Height(),
        SWP_NOACTIVATE | SWP_NOCOPYBITS);
    CCC_MainLockSyncBtnCheck(e);
}

// CCC_MainLockShowBtn: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MainLockShowBtn(HWND hDlg, BOOL bShow)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pSaveFlag)
        return;
    if (e->overlayPaint) {
        if (bShow)
            CCC_MainLockInvalidateOverlay(hDlg);
        return;
    }
    if (bShow)
        CCC_MainLockLayoutBtn(hDlg);
    else if (e->pLockBtn && ::IsWindow(e->pLockBtn->GetSafeHwnd()))
        e->pLockBtn->ShowWindow(SW_HIDE);
}

// CCC_MainLockDrawOverlay: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_MainLockDrawOverlay(CDC& dc, const CRect& rc, BOOL locked)
{
#if CCUSTOM_AERO_SUPPORT
    dc.FillSolidRect(rc, CCC_AERO_CHROMA_KEY);
#else
    dc.FillSolidRect(rc, RGB(52, 44, 68));
#endif

    CRect chk(rc.left + 3, rc.top + 4, rc.left + 17, rc.top + 18);
    dc.FillSolidRect(&chk, RGB(255, 249, 252));
    dc.Draw3dRect(&chk, RGB(0, 0, 0), RGB(0, 0, 0));
    dc.DrawEdge(chk, EDGE_SUNKEN, BF_RECT);
    if (locked) {
        CPen pen(PS_SOLID, 2, COLOR_CHECK);
        CPen* pOldPen = dc.SelectObject(&pen);
        dc.MoveTo(chk.left + 3, chk.top + 8);
        dc.LineTo(chk.left + 6, chk.top + 12);
        dc.LineTo(chk.right - 3, chk.top + 5);
        dc.SelectObject(pOldPen);
    }

    CRect textRc = rc;
    textRc.left += 20;
    CCC_DrawTextBlackEdge(dc, CCC_MainLockLabel(), textRc,
        DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX, RGB(255, 255, 255));
}

// CCC_MainLockPaintClient: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_MainLockPaintClient(CDC& dc, HWND hDlg)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pSaveFlag || !e->overlayPaint)
        return;
    CRect rc;
    CCC_MainLockGetClientRect(hDlg, rc);
    if (rc.IsRectEmpty())
        return;

    const int w = rc.Width();
    const int h = rc.Height();
    if (w <= 0 || h <= 0)
        return;
    CCC_MainLockOverlayCache* pCache = CCC_MainLockEnsureOverlayCachePtr(e);
    if (!pCache)
        return;
    if (pCache->w != w || pCache->h != h || !pCache->dc.GetSafeHdc()) {
        if (pCache->dc.GetSafeHdc()) {
            if (pCache->oldBmp)
                pCache->dc.SelectObject(pCache->oldBmp);
            pCache->dc.DeleteDC();
        }
        pCache->bmp.DeleteObject();
        pCache->oldBmp = nullptr;
        if (!pCache->dc.CreateCompatibleDC(&dc))
            return;
        if (!pCache->bmp.CreateCompatibleBitmap(&dc, w, h)) {
            pCache->dc.DeleteDC();
            return;
        }
        pCache->oldBmp = pCache->dc.SelectObject(&pCache->bmp);
        pCache->w = w;
        pCache->h = h;
        pCache->dirty = TRUE;
    }
    if (pCache->dirty) {
        CRect local(0, 0, w, h);
        CCC_MainLockDrawOverlay(pCache->dc, local, e->locked);
        pCache->dirty = FALSE;
    }

    // PaintAeroGaps の ClipNoChildren 等でクリップが削られていても、
    // ロック矩形は必ず描く（子スタティックと重なるダイアログ対策）。
    const int saved = dc.SaveDC();
    dc.SelectClipRgn(NULL);
    dc.IntersectClipRect(&rc);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_AcrylicCaption(hDlg)) {
        CCC_BlitChromaNF(dc.GetSafeHdc(), rc.left, rc.top, w, h,
            pCache->dc.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
        dc.RestoreDC(saved);
        return;
    }
#endif
    dc.BitBlt(rc.left, rc.top, w, h, &pCache->dc, 0, 0, SRCCOPY);
    dc.RestoreDC(saved);
}

// CCC_MainLockOverlayHitTest: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
BOOL CCC_MainLockOverlayHitTest(HWND hDlg, CPoint ptClient)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pSaveFlag || !e->overlayPaint)
        return FALSE;
    CRect rc;
    CCC_MainLockGetClientRect(hDlg, rc);
    return rc.PtInRect(ptClient);
}

// CCC_MainLockOverlayToggle: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_MainLockOverlayToggle(HWND hDlg)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pSaveFlag)
        return;
    CCC_ApplyMainLockState(e, !e->locked);
}

// トグル。ON なら現在位置から密着モードを計算し、以降メイン移動に追随。
// 保存フラグがあれば *pSaved を更新。
static void CCC_MainLockOnClicked(HWND hDlg)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pLockBtn || e->overlayPaint)
        return;
    CCC_ApplyMainLockState(e, e->pLockBtn->GetCheck() == BST_CHECKED);
}

// CCC_MainLockBringToFront: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_MainLockBringToFront(HWND hDlg)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pSaveFlag)
        return;
    if (e->overlayPaint) {
        CCC_MainLockInvalidateOverlay(hDlg);
        return;
    }
    CCC_MainLockLayoutBtn(hDlg);
    if (e->pLockBtn && ::IsWindow(e->pLockBtn->GetSafeHwnd()))
        e->pLockBtn->SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// ---- GDI ヘルプ: DPI 込み実描画範囲フィット ----
static const UINT CCC_GDIHELP_SUBCLASS_ID = 0x47444948u; // 'GDIH'
static const UINT CCC_WM_GDIHELP_FIT = WM_USER + 0x47F1;
static const UINT_PTR CCC_GDIHELP_ANIM_TIMER = 0x47444154u; // Soft3D 実演アニメ
static const COLORREF CCC_GDIHELP_BG = RGB(248, 248, 252);
static const WCHAR CCC_GDIHELP_PROP_FITTED[] = L"CCC_GdiHelpFitted";
static const WCHAR CCC_GDIHELP_PROP_CW[] = L"CCC_GdiHelpCW";
static const WCHAR CCC_GDIHELP_PROP_CH[] = L"CCC_GdiHelpCH";

// Soft3D ヘルプ実演（UI スレッド専用・静的再利用）
static GdiSoft3D::Context s_helpSoft3d;
static GdiSoft2D::Context s_helpSoft2d;

// CCC_GdiHelpFillDemoScene: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_GdiHelpFillDemoScene(GdiSoft3D::Context& ctx, int kind, DWORD tick)
{
	const float t = (float)tick * 0.09f;
	switch (kind) {
	case CCC_HELPDEMO_KSPECTRUM: {
		float levL[16], levR[16];
		for (int i = 0; i < 16; ++i) {
			const float p = t + i * 0.45f;
			levL[i] = 0.18f + 0.70f * fabsf(sinf(p));
			levR[i] = 0.18f + 0.70f * fabsf(sinf(p + 0.85f));
		}
		ctx.DrawGrid(-1.05f, 1.05f, 0.0f, 0.95f, 0.0f, 6, RGB(46, 52, 70));
		ctx.DrawStereoBarsLR(-0.98f, 0.98f, 16, levL, levR,
			0.18f, 0.55f, 0.62f, 0.16f, RGB(80, 210, 255), RGB(255, 140, 90));
		break;
	}
	case CCC_HELPDEMO_KWAVE: {
		float samp[48];
		for (int i = 0; i < 48; ++i)
			samp[i] = sinf(t * 1.2f + i * 0.28f) * (0.55f + 0.35f * sinf(t * 0.4f + i * 0.05f));
		ctx.DrawGrid(-1.05f, 1.05f, 0.0f, 0.9f, 0.0f, 5, RGB(50, 56, 72));
		ctx.DrawMirrorFloor(-1.0f, 1.0f, 0.05f, 0.85f, RGB(40, 80, 120), 0.28f);
		ctx.DrawWaveRibbon(-0.95f, 0.95f, 0.42f, 0.28f, 0.42f, samp, 48, RGB(120, 210, 255), 0.025f);
		ctx.DrawWaveRibbon(-0.95f, 0.95f, 0.58f, 0.22f, 0.32f, samp, 48, RGB(255, 160, 120), 0.02f);
		break;
	}
	case CCC_HELPDEMO_KPIANO: {
		ctx.DrawGrid(-1.1f, 1.1f, 0.0f, 1.0f, 0.0f, 5, RGB(48, 50, 62));
		for (int i = 0; i < 14; ++i) {
			const float x0 = -1.0f + i * (2.0f / 14.f);
			const float x1 = x0 + (2.0f / 14.f) * 0.88f;
			const BOOL black = (i % 7 == 1 || i % 7 == 3 || i % 7 == 6);
			const float h = black ? 0.28f : 0.42f;
			const COLORREF c = black ? RGB(40, 42, 55) : RGB(235, 238, 248);
			ctx.DrawBox(x0, x1, h, 0.15f, 0.55f, c, 0.f);
		}
		for (int n = 0; n < 5; ++n) {
			const float ph = t * 0.7f + n * 1.1f;
			const float x = -0.85f + fmodf(ph, 1.7f);
			const float y = 0.55f + 0.22f * fabsf(sinf(ph * 2.f));
			ctx.DrawNeonBox(x, x + 0.12f, y, 0.58f, 0.78f, RGB(90, 170, 255), 0.48f);
		}
		break;
	}
	case CCC_HELPDEMO_KMIDIMON: {
		ctx.DrawGrid(-1.1f, 1.1f, 0.0f, 1.0f, 0.0f, 4, RGB(40, 48, 62));
		for (int i = 0; i < 16; ++i) {
			const float x0 = -1.02f + (i % 8) * (2.04f / 8.f);
			const float x1 = x0 + (2.04f / 8.f) * 0.78f;
			const float z = (i < 8) ? 0.28f : 0.62f;
			const float h = 0.12f + 0.38f * (0.5f + 0.5f * sinf(t * 1.3f + i * 0.4f));
			const COLORREF c = (i < 8) ? RGB(90, 180, 255) : RGB(255, 140, 90);
			ctx.DrawBox(x0, x1, h, z, z + 0.22f, c, 0.f);
		}
		break;
	}
	case CCC_HELPDEMO_KEQ: {
		ctx.DrawGrid(-1.05f, 1.05f, 0.0f, 0.9f, 0.0f, 5, RGB(48, 52, 68));
		for (int i = 0; i < 12; ++i) {
			const float x0 = -0.95f + i * (1.9f / 12.f);
			const float x1 = x0 + (1.9f / 12.f) * 0.72f;
			const float h = 0.15f + 0.55f * (0.5f + 0.5f * sinf(t + i * 0.55f));
			ctx.DrawBox(x0, x1, h, 0.25f, 0.55f,
				RGB(255, (BYTE)(120 + i * 8), (BYTE)(160 + i * 4)), 0.f);
		}
		ctx.DrawSphere(0.f, 0.72f + 0.08f * sinf(t), 0.7f, 0.08f, RGB(255, 200, 120), 10, 6);
		break;
	}
	case CCC_HELPDEMO_KCAPTURE: {
		ctx.DrawGrid(-1.1f, 1.1f, 0.0f, 1.0f, 0.0f, 4, RGB(44, 50, 64));
		// 背面モニタ平面
		ctx.DrawQuad(-0.95f, 0.75f, 0.85f, 0.95f, 0.75f, 0.85f,
			0.95f, 0.05f, 0.85f, -0.95f, 0.05f, 0.85f, RGB(55, 110, 90));
		// FX レイヤ（手前に浮遊）
		const float bob = 0.04f * sinf(t);
		ctx.DrawNeonBox(-0.55f, -0.15f, 0.55f + bob, 0.35f, 0.55f, RGB(180, 140, 60), 0.2f);
		ctx.DrawNeonBox(-0.05f, 0.35f, 0.48f - bob, 0.30f, 0.50f, RGB(180, 140, 60), 0.18f);
		ctx.DrawNeonBox(0.40f, 0.75f, 0.42f + bob * 0.5f, 0.25f, 0.45f, RGB(150, 70, 70), 0.16f);
		break;
	}
	case CCC_HELPDEMO_KCMDROLL: {
		ctx.DrawGrid(-1.1f, 1.1f, 0.0f, 0.95f, 0.0f, 5, RGB(46, 50, 66));
		ctx.DrawBox(-1.0f, 1.0f, 0.08f, 0.35f, 0.55f, RGB(70, 78, 100), 0.f); // タイムライン帯
		for (int i = 0; i < 7; ++i) {
			const float x = -0.85f + i * 0.28f + 0.06f * sinf(t + i);
			const float h = 0.25f + 0.35f * (0.5f + 0.5f * sinf(t * 1.3f + i));
			ctx.DrawNeonBox(x, x + 0.14f, h, 0.40f, 0.70f,
				(i & 1) ? RGB(120, 200, 255) : RGB(255, 160, 100), 0.12f);
		}
		break;
	}
	case CCC_HELPDEMO_KLIST: {
		ctx.DrawGrid(-1.05f, 1.05f, 0.0f, 0.9f, 0.0f, 4, RGB(48, 52, 68));
		for (int row = 0; row < 5; ++row) {
			const float z0 = 0.15f + row * 0.14f;
			const float z1 = z0 + 0.10f;
			const float pulse = 0.5f + 0.5f * sinf(t + row * 0.7f);
			const COLORREF c = (row == ((tick / 8) % 5))
				? RGB(255, (BYTE)(180 + 40 * pulse), (BYTE)(200 + 30 * pulse))
				: RGB(210, 215, 230);
			ctx.DrawBox(-0.9f, 0.9f, 0.12f + 0.04f * pulse, z0, z1, c, 0.f);
		}
		break;
	}
	case CCC_HELPDEMO_KTRANSPORT: {
		ctx.DrawGrid(-1.05f, 1.05f, 0.0f, 0.9f, 0.0f, 5, RGB(48, 52, 68));
		ctx.DrawBox(-0.95f, 0.95f, 0.10f, 0.45f, 0.58f, RGB(255, 170, 200), 0.f); // シーク
		const float thumb = -0.7f + 1.4f * (0.5f + 0.5f * sinf(t * 0.55f));
		ctx.DrawSphere(thumb, 0.18f, 0.52f, 0.09f, RGB(90, 140, 255), 10, 6);
		ctx.DrawNeonBox(-0.35f, -0.10f, 0.42f, 0.20f, 0.38f, RGB(120, 220, 140), 0.12f); // play
		ctx.DrawBox(0.00f, 0.18f, 0.40f, 0.20f, 0.38f, RGB(240, 210, 120), 0.12f);
		ctx.DrawBox(0.28f, 0.46f, 0.40f, 0.20f, 0.38f, RGB(240, 140, 140), 0.12f);
		break;
	}
	case CCC_HELPDEMO_KMAZE: {
		// 通路・壁・窓・アイテム・ゴールを周回カメラで説明
		ctx.DrawMirrorFloor(-1.1f, 1.1f, 0.05f, 1.15f, RGB(40, 48, 70), 0.30f);
		ctx.DrawGrid(-1.05f, 1.05f, 0.05f, 1.10f, 0.002f, 6, RGB(55, 65, 95));
		// 左右の壁列
		for (int i = 0; i < 5; ++i) {
			const float z0 = 0.08f + i * 0.20f;
			const float z1 = z0 + 0.16f;
			ctx.DrawBox(-1.05f, -0.55f, 0.95f, z0, z1, RGB(72, 86, 118), 0.f);
			ctx.DrawBox(0.55f, 1.05f, 0.95f, z0, z1, RGB(72, 86, 118), 0.f);
		}
		// 窓（ネオン）
		ctx.DrawNeonBox(-1.02f, -0.58f, 0.92f, 0.48f, 0.68f, RGB(120, 190, 255), 0.f);
		ctx.DrawBox(-0.92f, -0.68f, 0.55f, 0.54f, 0.62f, RGB(40, 90, 140), 0.28f);
		// 浮遊アイテム（色分け）
		const float bob = 0.06f * sinf(t * 1.8f);
		ctx.DrawSphere(-0.22f, 0.28f + bob, 0.35f, 0.11f, RGB(80, 220, 140), 10, 8);   // tempo
		ctx.DrawSphere(0.05f, 0.30f - bob, 0.52f, 0.11f, RGB(255, 180, 90), 10, 8);    // pitch up
		ctx.DrawSphere(0.28f, 0.28f + bob * 0.7f, 0.70f, 0.11f, RGB(200, 140, 255), 10, 8); // EQ
		// ゴール
		ctx.DrawNeonBox(-0.22f, 0.22f, 0.72f + 0.04f * sinf(t), 0.88f, 1.08f, RGB(255, 210, 80), 0.f);
		// プレイヤー位置の三角っぽいマーカー（手前）
		ctx.DrawNeonBox(-0.06f, 0.06f, 0.18f, 0.12f, 0.22f, RGB(255, 240, 120), 0.f);
		break;
	}
	case CCC_HELPDEMO_KRACE: {
		// 空中レース: 地形・パワーバンド・機体・障害・アイテム
		ctx.DrawMirrorFloor(-1.15f, 1.15f, 0.0f, 1.20f, RGB(55, 120, 70), 0.22f);
		ctx.DrawGrid(-1.1f, 1.1f, 0.02f, 1.15f, 0.002f, 7, RGB(70, 140, 80));
		// うねるパワーバンド（ネオン帯）
		for (int i = 0; i < 8; ++i) {
			const float z0 = 0.06f + i * 0.13f;
			const float z1 = z0 + 0.11f;
			const float y = 0.22f + 0.10f * sinf(t * 0.8f + i * 0.55f);
			ctx.DrawNeonBox(-0.38f, 0.38f, y + 0.08f, z0, z1, RGB(140, 220, 255), 0.f);
			ctx.DrawBox(-0.34f, 0.34f, y + 0.05f, z0 + 0.01f, z1 - 0.01f, RGB(90, 180, 230), 0.35f);
		}
		// 森の木（左右）
		for (int i = 0; i < 4; ++i) {
			const float z = 0.18f + i * 0.24f;
			ctx.DrawBox(-0.95f, -0.82f, 0.55f, z, z + 0.12f, RGB(90, 55, 30), 0.f);
			ctx.DrawSphere(-0.88f, 0.72f + 0.04f * sinf(t + i), z + 0.06f, 0.18f, RGB(50, 160, 70), 8, 6);
			ctx.DrawBox(0.82f, 0.95f, 0.55f, z, z + 0.12f, RGB(90, 55, 30), 0.f);
			ctx.DrawSphere(0.88f, 0.70f - 0.03f * sinf(t + i), z + 0.06f, 0.17f, RGB(45, 150, 65), 8, 6);
		}
		// トンネル枠
		ctx.DrawBox(-0.55f, -0.42f, 0.85f, 0.55f, 0.70f, RGB(150, 140, 120), 0.f);
		ctx.DrawBox(0.42f, 0.55f, 0.85f, 0.55f, 0.70f, RGB(150, 140, 120), 0.f);
		ctx.DrawBox(-0.55f, 0.55f, 0.95f, 0.68f, 0.78f, RGB(140, 130, 110), 0.f);
		// 自機（鳥っぽい）
		const float craftZ = 0.28f + 0.08f * sinf(t * 1.2f);
		const float craftY = 0.38f + 0.05f * sinf(t * 1.6f);
		ctx.DrawSphere(0.f, craftY, craftZ, 0.10f, RGB(255, 170, 90), 10, 8);
		ctx.DrawBox(-0.22f, -0.06f, craftY + 0.04f, craftZ - 0.02f, craftZ + 0.04f, RGB(255, 200, 120), 0.f);
		ctx.DrawBox(0.06f, 0.22f, craftY + 0.04f, craftZ - 0.02f, craftZ + 0.04f, RGB(255, 200, 120), 0.f);
		// アイテム
		const float bob = 0.05f * sinf(t * 2.f);
		ctx.DrawSphere(-0.18f, 0.48f + bob, 0.62f, 0.08f, RGB(80, 230, 130), 8, 6);
		ctx.DrawSphere(0.16f, 0.50f - bob, 0.78f, 0.08f, RGB(200, 120, 255), 8, 6);
		ctx.DrawSphere(0.02f, 0.55f + bob * 0.6f, 0.95f, 0.08f, RGB(255, 90, 120), 8, 6);
		break;
	}
	default: { // GENERIC
		ctx.DrawGrid(-1.0f, 1.0f, 0.0f, 0.95f, 0.0f, 5, RGB(46, 52, 70));
		ctx.DrawNeonBox(-0.45f, 0.45f, 0.55f + 0.06f * sinf(t), 0.25f, 0.65f, RGB(255, 140, 190), 0.f);
		ctx.DrawTorus(0.f, 0.35f, 0.45f, 0.38f, 0.08f, RGB(120, 200, 255), 14, 8);
		ctx.DrawSphere(0.55f * cosf(t), 0.55f, 0.45f + 0.35f * sinf(t), 0.10f, RGB(255, 210, 120), 10, 6);
		break;
	}
	}
}

// CCC_GdiHelpDrawSoft3DDemo: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
int CCC_GdiHelpDrawSoft3DDemo(CDC& dc, int x, int y, int maxW, int maxH, int kind)
{
	if (maxW < 80 || maxH < 48) return y;
	int w = maxW;
	int h = maxH;
	if (w > 360) w = 360;
	if (h > 180) h = 180;
	if (h < 96) h = min(maxH > 0 ? maxH : 96, 96);
	if (h < 72) h = 72;
	if (w < 140) w = min(maxW, 140);

	GdiSoft3D::Cam cam;
	const DWORD tick = (DWORD)(::GetTickCount64() / 40);
	cam.yawDeg = -28.f + 22.f * sinf((float)tick * 0.035f);
	cam.pitchDeg = 26.f + 8.f * sinf((float)tick * 0.028f);
	cam.zoom = 1.05f;
	GdiSoft3D::ClampCam(cam);

	const float boxes[1][6] = { { -1.15f, 1.15f, -0.02f, 0.85f, 0.0f, 1.05f } };
	GdiSoft3D::View v;
	GdiSoft3D::BuildView(w, h, cam, boxes, 1, v);
	if (!s_helpSoft3d.Create(w, h))
		return y;
	s_helpSoft3d.view = v;
	s_helpSoft3d.depthTest = true;
	s_helpSoft3d.depthWrite = true;
	s_helpSoft3d.BeginFrame(RGB(14, 16, 24));
	CCC_GdiHelpFillDemoScene(s_helpSoft3d, kind, tick);
	s_helpSoft3d.EndFrame();
	s_helpSoft3d.Present(dc, x, y);

	CBrush fr(RGB(132, 132, 152));
	dc.FrameRect(CRect(x, y, x + w, y + h), &fr);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(100, 100, 120));
	dc.TextOut(x + 4, y + h + 2, LL14(
		L"Soft3D 実演（自動周回）", L"Soft 3D demo (auto orbit)", L"Démo Soft 3D (orbite auto)",
		L"Demo Soft 3D (orbita auto)", L"Demo Soft 3D (órbita auto)", L"Soft3D 실연(자동 회전)",
		L"Soft3D 演示（自动环绕）", L"عرض Soft3D (دوران تلقائي)", L"Демо Soft 3D (авто-облёт)",
		L"Soft-3D-Demo (Auto-Orbit)", L"Demo Soft 3D (órbita auto)", L"Soft 3D-demo (auto-orbit)",
		L"Demo Soft 3D (auto-orbita)", L"Soft 3B demo (otomatik dönüş)"));
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	const int lh = max(12, tm.tmHeight + tm.tmExternalLeading);
	return y + h + lh + 8;
}

// CCC_GdiHelpDrawSoftDemoPair: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
int CCC_GdiHelpDrawSoftDemoPair(CDC& dc, int x, int y, int totalW, int demoH, int kind)
{
	if (totalW < 160 || demoH < 48) return y;
	// 手順コマ内 Soft3D が潰れないよう最低高さを確保
	if (demoH < 112) demoH = 112;
	const int gap = 10;
	int rightW = min(260, max(140, totalW / 3));
	int leftW = totalW - rightW - gap;
	if (leftW < 140) {
		return CCC_GdiHelpDrawSoft3DDemo(dc, x, y, totalW, demoH, kind);
	}
	const DWORD tick = (DWORD)(::GetTickCount64() / 40);
	const int cells = 4;
	const int cg = 6;
	const int cellW = (leftW - cg * (cells - 1)) / cells;
	const COLORREF cellCol[4] = {
		RGB(255, 226, 196), RGB(206, 232, 255), RGB(214, 245, 220), RGB(232, 220, 250)
	};
	LPCTSTR stepLab[4] = {
		LL14(L"1.見る", L"1.Look", L"1.Voir", L"1.Guarda", L"1.Ver", L"1.보기", L"1.看", L"1.انظر",
			L"1.Смотри", L"1.Sehen", L"1.Ver", L"1.Kijk", L"1.Patrz", L"1.Bak"),
		LL14(L"2.切替", L"2.Switch", L"2.Basculer", L"2.Cambia", L"2.Cambiar", L"2.전환", L"2.切换", L"2.بدّل",
			L"2.Смена", L"2.Umsch.", L"2.Trocar", L"2.Wissel", L"2.Zmień", L"2.Değiş"),
		LL14(L"3.回す", L"3.Orbit", L"3.Orbite", L"3.Orbita", L"3.Órbita", L"3.회전", L"3.旋转", L"3.دور",
			L"3.Облёт", L"3.Orbit", L"3.Órbita", L"3.Orbit", L"3.Orbita", L"3.Dön"),
		LL14(L"4.拡大", L"4.Zoom", L"4.Zoom", L"4.Zoom", L"4.Zoom", L"4.확대", L"4.放大", L"4.تكبير",
			L"4.Зум", L"4.Zoom", L"4.Zoom", L"4.Zoom", L"4.Zoom", L"4.Yakın")
	};

	TEXTMETRIC tm0 = {};
	dc.GetTextMetrics(&tm0);
	const int labH = max(14, tm0.tmHeight + 2);
	const int innerPad = 3;
	const int softTop = labH + 4;
	const int softH = max(40, demoH - softTop - 4);
	const int softW = max(40, cellW - innerPad * 2);

	if (s_helpSoft2d.Create(leftW, demoH)) {
		s_helpSoft2d.Clear(RGB(248, 248, 252));
		for (int i = 0; i < cells; ++i) {
			const int cx = i * (cellW + cg);
			s_helpSoft2d.FillRect(cx, 0, cellW, demoH, cellCol[i], 255);
			s_helpSoft2d.DrawRect(cx, 0, cellW, demoH, RGB(150, 150, 172), 255, 1);
			if (i + 1 < cells) {
				const int ax = cx + cellW + 1;
				const int ay = softTop + softH / 2;
				s_helpSoft2d.DrawLine(ax, ay, ax + cg - 2, ay, RGB(90, 100, 160), 255, 2);
				s_helpSoft2d.DrawLine(ax + cg - 6, ay - 3, ax + cg - 2, ay, RGB(90, 100, 160), 255, 2);
				s_helpSoft2d.DrawLine(ax + cg - 6, ay + 3, ax + cg - 2, ay, RGB(90, 100, 160), 255, 2);
			}
		}
		s_helpSoft2d.Present(dc, x, y);
	}

	// 各コマほぼ全面を Soft3D 実演にする（28px チップは小さすぎた）
	for (int i = 0; i < cells; ++i) {
		const int cx0 = x + i * (cellW + cg) + innerPad;
		const int cy0 = y + softTop;
		GdiSoft3D::Cam cam;
		cam.yawDeg = -50.f + (float)i * 18.f + 28.f * sinf((float)(tick + i * 9) * 0.05f);
		cam.pitchDeg = 24.f + 8.f * cosf((float)(tick + i * 5) * 0.04f);
		cam.zoom = 0.82f; // コマ内でも立体が小さく見えないよう寄る
		GdiSoft3D::ClampCam(cam);
		const float boxes[1][6] = { { -1.1f, 1.1f, -0.05f, 0.85f, 0.0f, 1.0f } };
		GdiSoft3D::View v;
		GdiSoft3D::BuildView(softW, softH, cam, boxes, 1, v);
		if (!s_helpSoft3d.Create(softW, softH))
			continue;
		s_helpSoft3d.view = v;
		s_helpSoft3d.depthTest = true;
		s_helpSoft3d.depthWrite = true;
		s_helpSoft3d.BeginFrame(RGB(16, 18, 26));
		// コマごとに少し違う見本（同じ kind を位相ずらし）
		CCC_GdiHelpFillDemoScene(s_helpSoft3d, kind, tick + (DWORD)(i * 17));
		s_helpSoft3d.EndFrame();
		s_helpSoft3d.Present(dc, cx0, cy0);
		CBrush frCell(RGB(120, 120, 140));
		dc.FrameRect(CRect(cx0, cy0, cx0 + softW, cy0 + softH), &frCell);

		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(40, 40, 60));
		dc.TextOut(x + i * (cellW + cg) + 4, y + 2, stepLab[i]);
	}
	CBrush fr(RGB(132, 132, 152));
	dc.FrameRect(CRect(x, y, x + leftW, y + demoH), &fr);

	const int rightX = x + leftW + gap;
	CCC_GdiHelpDrawSoft3DDemo(dc, rightX, y, rightW, demoH, kind);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(90, 70, 130));
	dc.TextOut(x + 4, y + demoH + 2, LL14(
		L"左の黄・青・緑・紫＝見る→切替→回す→拡大の流れ（中の立体が Soft3D）。右＝大きめの自動実演。",
		L"Yellow/blue/green/purple = Look→Switch→Orbit→Zoom (Soft 3D inside). Right = larger auto demo.",
		L"Jaune/bleu/vert/violet = Voir→Basculer→Orbite→Zoom (Soft 3D). Droite = grande démo auto.",
		L"Giallo/blu/verde/viola = Guarda→Cambia→Orbita→Zoom (Soft 3D). Destra = demo auto grande.",
		L"Amarillo/azul/verde/violeta = Ver→Cambiar→Órbita→Zoom (Soft 3D). Derecha = demo auto.",
		L"노랑·파랑·초록·보라=보기→전환→회전→확대(안 Soft3D). 오른쪽=큰 자동 실연.",
		L"黄/蓝/绿/紫=看→切换→旋转→放大（内为 Soft3D）。右=较大自动演示。",
		L"أصفر/أزرق/أخضر/بنفسجي=انظر→بدّل→دور→كبّر (Soft3D). يميناً=عرض تلقائي أكبر.",
		L"Жёлт./син./зел./фиол. = Смотри→Смена→Облёт→Зум (Soft 3D). Справа — крупное автодемо.",
		L"Gelb/Blau/Grün/Violett = Sehen→Umsch.→Orbit→Zoom (Soft 3D). Rechts = große Auto-Demo.",
		L"Amarelo/azul/verde/roxo = Ver→Trocar→Órbita→Zoom (Soft 3D). Direita = demo auto maior.",
		L"Geel/blauw/groen/paars = Kijk→Wissel→Orbit→Zoom (Soft 3D). Rechts = grotere autodemo.",
		L"Żółty/nieb./ziel./fiolet = Patrz→Zmień→Orbita→Zoom (Soft 3D). Prawo = większe autodemo.",
		L"Sarı/mavi/yeşil/mor = Bak→Değiş→Dön→Yakın (Soft 3B). Sağ = büyük otomatik demo."));
	TEXTMETRIC tm = {};
	dc.GetTextMetrics(&tm);
	const int lh = max(12, tm.tmHeight + tm.tmExternalLeading);
	return y + demoH + lh + 8;
}

// CCC_GdiHelpLayoutCloseBtn: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_GdiHelpLayoutCloseBtn(CWnd* wnd, int footerH)
{
    if (!wnd || !::IsWindow(wnd->GetSafeHwnd())) return;
    CWnd* ok = wnd->GetDlgItem(IDOK);
    if (!ok || !::IsWindow(ok->GetSafeHwnd())) return;
    const UINT dpi = CCC_GetControlDpi(wnd->GetSafeHwnd());
    CRect rc;
    wnd->GetClientRect(&rc);
    const int btnW = CCC_ScaleDpi(50, dpi);
    const int btnH = CCC_ScaleDpi(14, dpi);
    const int margin = CCC_ScaleDpi(8, dpi);
    int top = rc.bottom - btnH - margin;
    if (footerH > 0) {
        const int ft = rc.bottom - footerH + (footerH - btnH) / 2;
        if (ft > 0) top = ft;
    }
    ok->SetWindowPos(NULL, rc.right - btnW - margin, top, btnW, btnH,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

// CCC_GdiHelpApplyClientSize: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_GdiHelpApplyClientSize(HWND hWnd, int clientW, int clientH)
{
    if (!hWnd || !::IsWindow(hWnd) || clientW < 80 || clientH < 60) return;
    CRect rcW, rcC;
    ::GetWindowRect(hWnd, &rcW);
    ::GetClientRect(hWnd, &rcC);
    const int borderW = rcW.Width() - rcC.Width();
    const int borderH = rcW.Height() - rcC.Height();
    int newW = clientW + borderW;
    int newH = clientH + borderH;

    HMONITOR mon = ::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    CRect work;
    if (mon && ::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);

    int x = rcW.left;
    int y = rcW.top;
    if (x + newW > work.right) x = work.right - newW;
    if (y + newH > work.bottom) y = work.bottom - newH;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;

    ::SetWindowPos(hWnd, NULL, x, y, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);
    CWnd* w = CWnd::FromHandle(hWnd);
    const UINT dpi = CCC_GetControlDpi(hWnd);
    CCC_GdiHelpLayoutCloseBtn(w, CCC_ScaleDpi(26, dpi));
}

// CCC_GdiHelpSubclassProc: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static LRESULT CALLBACK CCC_GdiHelpSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(uIdSubclass);
    UNREFERENCED_PARAMETER(dwRefData);
    if (uMsg == CCC_WM_GDIHELP_FIT) {
        CCC_GdiHelpApplyClientSize(hWnd, (int)wParam, (int)lParam);
        ::InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    if (uMsg == WM_TIMER && wParam == CCC_GDIHELP_ANIM_TIMER) {
        // Soft3D 実演の再描画（ちらつき低減のため全体 Invalidate は FALSE）
        ::InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    }
    if (uMsg == WM_DPICHANGED) {
        ::RemoveProp(hWnd, CCC_GDIHELP_PROP_FITTED);
        ::RemoveProp(hWnd, CCC_GDIHELP_PROP_CW);
        ::RemoveProp(hWnd, CCC_GDIHELP_PROP_CH);
    }
    if (uMsg == WM_NCDESTROY) {
        ::KillTimer(hWnd, CCC_GDIHELP_ANIM_TIMER);
        ::RemoveProp(hWnd, CCC_GDIHELP_PROP_FITTED);
        ::RemoveProp(hWnd, CCC_GDIHELP_PROP_CW);
        ::RemoveProp(hWnd, CCC_GDIHELP_PROP_CH);
        ::RemoveWindowSubclass(hWnd, CCC_GdiHelpSubclassProc, CCC_GDIHELP_SUBCLASS_ID);
    }
    return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// CCC_GdiHelpEnsureSubclass: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_GdiHelpEnsureSubclass(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd)) return;
    DWORD_PTR data = 0;
    if (!::GetWindowSubclass(hWnd, CCC_GdiHelpSubclassProc, CCC_GDIHELP_SUBCLASS_ID, &data))
        ::SetWindowSubclass(hWnd, CCC_GdiHelpSubclassProc, CCC_GDIHELP_SUBCLASS_ID, 0);
    // Soft3D 実演アニメ（未設定なら開始）
    ::SetTimer(hWnd, CCC_GDIHELP_ANIM_TIMER, 40, NULL);
}

// CCC_GdiHelpScanExtent: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_GdiHelpScanExtent(const UINT32* bits, int w, int h, COLORREF bg, int* pMaxX, int* pMaxY)
{
    const UINT32 bgPx = ((UINT32)GetRValue(bg) << 16) | ((UINT32)GetGValue(bg) << 8) | (UINT32)GetBValue(bg);
    int mx = 0, my = 0;
    BOOL any = FALSE;
    for (int y = 0; y < h; ++y) {
        const UINT32* row = bits + y * w;
        for (int x = 0; x < w; ++x) {
            if ((row[x] & 0x00FFFFFFu) != bgPx) {
                any = TRUE;
                if (x > mx) mx = x;
                if (y > my) my = y;
            }
        }
    }
    if (!any) {
        *pMaxX = CCC_ScaleDpi(200, 96);
        *pMaxY = CCC_ScaleDpi(120, 96);
        return;
    }
    *pMaxX = mx + 1;
    *pMaxY = my + 1;
}

// CCC_GdiHelpBeginPaint: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
BOOL CCC_GdiHelpBeginPaint(CWnd* wnd, CDC& paintDc, CCC_GdiHelpPaint& hp)
{
    hp.ok = FALSE;
    hp.wnd = wnd;
    hp.pPaintDc = &paintDc;
    hp.oldBmp = NULL;
    hp.bits = NULL;
    if (!wnd || !::IsWindow(wnd->GetSafeHwnd()))
        return FALSE;

    HWND hWnd = wnd->GetSafeHwnd();
    CCC_GdiHelpEnsureSubclass(hWnd);

    const UINT dpi = CCC_GetControlDpi(hWnd);
    hp.footerH = CCC_ScaleDpi(26, dpi);

    const BOOL fitted = (::GetProp(hWnd, CCC_GDIHELP_PROP_FITTED) != NULL);
    int bw = 0, bh = 0;
    if (fitted) {
        bw = (int)(INT_PTR)::GetProp(hWnd, CCC_GDIHELP_PROP_CW);
        bh = (int)(INT_PTR)::GetProp(hWnd, CCC_GDIHELP_PROP_CH);
    }
    if (bw < 80 || bh < 60) {
        // 未フィット時は DPI スケールした十分大きなキャンバスで実座標を測る
        bw = CCC_ScaleDpi(1000, dpi);
        bh = CCC_ScaleDpi(2200, dpi);
        CRect rcClient;
        wnd->GetClientRect(&rcClient);
        if (rcClient.Width() > bw) bw = rcClient.Width();
        if (rcClient.Height() > bh) bh = rcClient.Height();
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -bh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hDib = ::CreateDIBSection(paintDc.GetSafeHdc(), &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hDib || !bits)
        return FALSE;

    hp.mem.CreateCompatibleDC(&paintDc);
    hp.bmp.Attach(hDib);
    hp.oldBmp = hp.mem.SelectObject(&hp.bmp);
    hp.bits = bits;
    hp.bw = bw;
    hp.bh = bh;
    hp.rc.SetRect(0, 0, bw, max(1, bh - hp.footerH));
    hp.mem.FillSolidRect(0, 0, bw, bh, CCC_GDIHELP_BG);
    hp.ok = TRUE;
    return TRUE;
}

// CCC_GdiHelpEndPaint: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_GdiHelpEndPaint(CCC_GdiHelpPaint& hp)
{
    if (!hp.ok || !hp.wnd || !hp.pPaintDc || !hp.bits)
        return;

    HWND hWnd = hp.wnd->GetSafeHwnd();
    const UINT dpi = CCC_GetControlDpi(hWnd);
    const int pad = CCC_ScaleDpi(18, dpi); // DPI で末尾行が見切れないよう余白を厚めに

    int maxX = 0, maxY = 0;
    CCC_GdiHelpScanExtent((const UINT32*)hp.bits, hp.bw, hp.bh, CCC_GDIHELP_BG, &maxX, &maxY);

    // フッター帯より上に描画がある前提。フッター分を足して論理サイズに
    int logicalW = maxX + pad;
    int logicalH = maxY + pad;
    if (logicalH < maxY + hp.footerH)
        logicalH = maxY + hp.footerH;
    if (logicalW < CCC_ScaleDpi(200, dpi)) logicalW = CCC_ScaleDpi(200, dpi);
    if (logicalH < CCC_ScaleDpi(120, dpi)) logicalH = CCC_ScaleDpi(120, dpi);
    if (logicalW > hp.bw) logicalW = hp.bw;
    if (logicalH > hp.bh) logicalH = hp.bh;

    HMONITOR mon = ::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    CRect work;
    if (mon && ::GetMonitorInfo(mon, &mi))
        work = mi.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);

    CRect rcW, rcC;
    ::GetWindowRect(hWnd, &rcW);
    ::GetClientRect(hWnd, &rcC);
    const int borderW = rcW.Width() - rcC.Width();
    const int borderH = rcW.Height() - rcC.Height();
    const int maxClientW = max(120, work.Width() - borderW - CCC_ScaleDpi(16, dpi));
    const int maxClientH = max(80, work.Height() - borderH - CCC_ScaleDpi(16, dpi));

    double scale = 1.0;
    if (logicalW > maxClientW)
        scale = (double)maxClientW / (double)logicalW;
    if (logicalH > maxClientH) {
        const double sy = (double)maxClientH / (double)logicalH;
        if (sy < scale) scale = sy;
    }
    if (scale > 1.0) scale = 1.0;
    if (scale < 0.25) scale = 0.25;

    int clientW = (int)(logicalW * scale + 0.5);
    int clientH = (int)(logicalH * scale + 0.5);
    if (clientW < 80) clientW = 80;
    if (clientH < 60) clientH = 60;
    if (clientW > maxClientW) clientW = maxClientW;
    if (clientH > maxClientH) clientH = maxClientH;

    // 現フレームは論理内容を現クライアントへ縮小描画（ちらつき低減）
    CRect rcNow;
    hp.wnd->GetClientRect(&rcNow);
    if (rcNow.Width() > 0 && rcNow.Height() > 0) {
        ::SetStretchBltMode(hp.pPaintDc->GetSafeHdc(), HALFTONE);
        ::StretchBlt(hp.pPaintDc->GetSafeHdc(), 0, 0, rcNow.Width(), rcNow.Height(),
            hp.mem.GetSafeHdc(), 0, 0, logicalW, logicalH, SRCCOPY);
    }

    const BOOL fitted = (::GetProp(hWnd, CCC_GDIHELP_PROP_FITTED) != NULL);
    const int prevW = (int)(INT_PTR)::GetProp(hWnd, CCC_GDIHELP_PROP_CW);
    const int prevH = (int)(INT_PTR)::GetProp(hWnd, CCC_GDIHELP_PROP_CH);
    const BOOL sizeChanged = !fitted || prevW != logicalW || prevH != logicalH
        || rcNow.Width() != clientW || rcNow.Height() != clientH;

    ::SetProp(hWnd, CCC_GDIHELP_PROP_CW, (HANDLE)(INT_PTR)logicalW);
    ::SetProp(hWnd, CCC_GDIHELP_PROP_CH, (HANDLE)(INT_PTR)logicalH);
    ::SetProp(hWnd, CCC_GDIHELP_PROP_FITTED, (HANDLE)(INT_PTR)1);

    if (hp.oldBmp)
        hp.mem.SelectObject(hp.oldBmp);
    hp.oldBmp = NULL;
    hp.bmp.DeleteObject();
    hp.mem.DeleteDC();
    hp.bits = NULL;
    hp.ok = FALSE;

    if (sizeChanged)
        ::PostMessage(hWnd, CCC_WM_GDIHELP_FIT, (WPARAM)clientW, (LPARAM)clientH);
}

// CCC_PresentOwnedHelp: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_PresentOwnedHelp(CWnd* help, CWnd* owner)
{
    if (!help || !::IsWindow(help->GetSafeHwnd()))
        return;
    CCC_GdiHelpEnsureSubclass(help->GetSafeHwnd());
    help->ShowWindow(SW_SHOW);
    // オーナー直上へ（HWND_TOPMOST は使わない）。他ツールが前面になれば一緒に下へ回る。
    if (owner && ::IsWindow(owner->GetSafeHwnd()))
        ::SetWindowPos(owner->GetSafeHwnd(), help->GetSafeHwnd(),
            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    help->BringWindowToTop();
    help->SetForegroundWindow();
    // 初回表示で実描画範囲フィットを走らせる
    help->Invalidate(FALSE);
}

// CCC_MainLockGetReserveWidth: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
int CCC_MainLockGetReserveWidth(HWND hDlg)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pSaveFlag || e->overlayPaint)
        return 0;
    CWnd* pDlg = CWnd::FromHandlePermanent(hDlg);
    if (!pDlg)
        pDlg = CWnd::FromHandle(hDlg);
    return CCC_MainLockMeasureWidth(pDlg) + CCC_MAINLOCK_MARGIN;
}

// エントリ作成、保存フラグ接続、ボタンまたはオーバーレイを用意。
// キャプション導入後は Layout が右端 chrome の左へ置く。
void CCC_MainLockSetup(CWnd* pDlg, int* pSavedLockFlag, BOOL bOverlayPaint)
{
    if (!pDlg || !::IsWindow(pDlg->GetSafeHwnd()) || !pSavedLockFlag)
        return;
    CCC_MainLockEntry* e = CCC_GetOrCreateMainLockEntry(pDlg->GetSafeHwnd());
    if (!e)
        return;
    e->pSaveFlag = pSavedLockFlag;
    // カスタムキャプション時は子チェックのみ（オーバーレイはキャプション帯と競合するため）
    if (CCC_GetCustomCaptionHeight(pDlg->GetSafeHwnd()) > 0)
        bOverlayPaint = FALSE;
    e->overlayPaint = bOverlayPaint;
    if (e->overlayPaint)
        CCC_MainLockDestroyBtn(e);
    CCC_ApplyMainLockState(e, (*pSavedLockFlag != 0));
    if (e->overlayPaint)
        CCC_MainLockInvalidateOverlay(pDlg->GetSafeHwnd());
    else
        CCC_MainLockBringToFront(pDlg->GetSafeHwnd());
}

// システム WS_CAPTION を描画から外し、クライアント先頭帯に min/max/close/pin/help。
// FRAMECHANGED は ExtendFrame より先。後だとマージンが消えて黒帯＋縦幅ジャンプ。
// NC 吸収分だけ子を下げる。capH で上乗せすると内容が余分に下がって見える。
// 二重 install 禁止（installed なら return）。
static void CCC_CaptionInstallCore(CWnd* pDlg, CToolTipCtrl* pTip)
{
    if (!pDlg || !::IsWindow(pDlg->GetSafeHwnd()))
        return;
    HWND hWnd = pDlg->GetSafeHwnd();
    if (CCC_FindCaption(hWnd) && CCC_FindCaption(hWnd)->installed)
        return;

    const DWORD style = (DWORD)::GetWindowLong(hWnd, GWL_STYLE);
    if (!(style & WS_CAPTION))
        return;

    // DWM 連携のため WS_CAPTION は残す。描画は NC 吸収後のクライアント帯のみ。
    const UINT dpi = CCC_GetControlDpi(hWnd);
    int sysCap = ::GetSystemMetricsForDpi(SM_CYCAPTION, dpi);
    if (sysCap < 24)
        sysCap = 24;
    int capH = CCC_ScaleDpi(32, dpi);
    if (capH < CCC_CAP_BTN + 6)
        capH = CCC_CAP_BTN + 6;
    if (capH < sysCap)
        capH = sysCap;

    CCC_CaptionEntry* e = CCC_GetOrCreateCaption(hWnd);
    if (!e)
        return;
    e->height = capH;
    e->hasMin = (style & WS_MINIMIZEBOX) != 0;
    // WS_POPUP は SC_MAXIMIZE が効きにくいが、ボタン自体は MAXIMIZEBOX または
    // リサイズ可能(MIN+THICKFRAME)なら出す（ShowWindow で切り替える）
    e->hasMax = (style & WS_MAXIMIZEBOX) != 0
        || ((style & (WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX))
            == (WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX));
    e->hasSettings = !CCC_CaptionIsRenderClass(pDlg);
    e->topmost = (::GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;

    CRect rcBefore;
    pDlg->GetClientRect(&rcBefore);

    // About 等は OnInit でシステムメニューへ足している。SYSMENU を外す前に退避。
    e->sysExtraCount = 0;
    if (HMENU hSys = ::GetSystemMenu(hWnd, FALSE)) {
        const int nSys = ::GetMenuItemCount(hSys);
        for (int i = 0; i < nSys && e->sysExtraCount < 8; ++i) {
            const UINT id = ::GetMenuItemID(hSys, i);
            if (id == 0 || id == (UINT)-1)
                continue;
            if (id >= 0xF000)
                continue;
            e->sysExtraId[e->sysExtraCount] = id;
            e->sysExtraText[e->sysExtraCount][0] = 0;
            ::GetMenuStringW(hSys, i, e->sysExtraText[e->sysExtraCount], 256, MF_BYPOSITION);
            if (e->sysExtraText[e->sysExtraCount][0])
                ++e->sysExtraCount;
        }
    }

    // WS_SYSMENU を外す。Win11 DWM は SYSMENU があるとシステム min/max/close を
    // クライアントへ重ね描きする（テーマ／CAPTION_COLOR では消えない）。Firefox/Edge と同じ。
    // WS_CAPTION + MINIMIZEBOX + MAXIMIZEBOX は残す（タスクバー再クリック最小化・スナップ）。
    pDlg->ModifyStyle(DS_MODALFRAME | WS_SYSMENU, 0);
    // ホスト α 時は CLIPCHILDREN 必須（親塗りがリスト等のスクロールバーを潰すのを防ぐ）
    pDlg->ModifyStyle(0, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    // キャプション帯は常時アクリル（本文の不透明化は描画側）。歌詞も同様。
    e->acrylicCaption = TRUE;
    e->installed = TRUE;
    // WS_CAPTION は残すので、DWM/テーマのシステム帯描画だけ止める（カスタム帯のみ）
    CCC_CaptionHideDwmTitleChrome(hWnd);

    // 先に NC 吸収（FRAMECHANGED）。ExtendFrame より後に FRAMECHANGED すると
    // DWM マージンが消えて「一瞬アクリル→黒帯」＋クライアント縦幅変化になる。
    pDlg->SetWindowPos(NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    CRect rcAfter;
    pDlg->GetClientRect(&rcAfter);
    int grown = rcAfter.Height() - rcBefore.Height();
    if (grown < 0)
        grown = 0;
    // NC 吸収分だけ下げる。capH で上乗せすると内容が余分に下がり「縦幅変化」に見える
    int shift = grown > 0 ? grown : capH;
    e->height = capH;

    HWND hChild = ::GetWindow(hWnd, GW_CHILD);
    while (hChild) {
        RECT r;
        ::GetWindowRect(hChild, &r);
        ::ScreenToClient(hWnd, (LPPOINT)&r.left);
        ::ScreenToClient(hWnd, (LPPOINT)&r.right);
        ::SetWindowPos(hChild, NULL, r.left, r.top + shift, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        hChild = ::GetWindow(hChild, GW_HWNDNEXT);
    }

    e->pClose = CCC_CaptionMakeBtn(pDlg, IDC_CAP_CLOSE, L"");
    if (e->hasMin)
        e->pMin = CCC_CaptionMakeBtn(pDlg, IDC_CAP_MIN, L"");
    if (e->hasMax) {
        e->pMax = CCC_CaptionMakeBtn(pDlg, IDC_CAP_MAX, L"");
        CCC_CaptionApplySharedIcon(e->pMax,
            (pDlg->IsZoomed() || e->manualZoomed) ? IDI_CTL_RESTORE : IDI_CTL_MAX);
    }
    if (e->hasSettings)
        e->pSettings = CCC_CaptionMakeBtn(pDlg, IDC_CAP_SETTINGS, L"");
    e->pPin = CCC_CaptionMakeBtn(pDlg, IDC_CAP_PIN, L"");
    CCC_CaptionApplySharedIcon(e->pPin, e->topmost ? IDI_CTL_PIN : IDI_CTL_PINOFF);

    if (CCC_MainLockEntry* le = CCC_FindMainLockEntry(hWnd)) {
        if (le->overlayPaint) {
            le->overlayPaint = FALSE;
            CCC_MainLockReleaseOverlayCache(le);
        }
        le->headerRowTop = -1;
        le->headerRowH = 0;
    }

    CCC_CaptionLayout(hWnd);

    // DWM アクリル源は FRAMECHANGED の後（呼び出し側で fixer 込み ApplyGlass も可）
    CCC_CaptionEnsureBackdrop(hWnd);

    if (pTip) {
        e->pCapTip = pTip;
        CCustomControlUtility::BeginDialogToolTip(*pTip, pDlg, TTS_NOPREFIX);
        if (e->pClose)
            pTip->AddTool(e->pClose, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
        if (e->pMin)
            pTip->AddTool(e->pMin, LL14(L"最小化", L"Minimize", L"Reduire", L"Riduci a icona", L"Minimizar", L"최소화", L"最小化", L"تصغير", L"Свернуть", L"Minimieren", L"Minimizar", L"Minimaliseren", L"Minimalizuj", L"Kucult"));
        if (e->pMax)
            pTip->AddTool(e->pMax, LL14(L"最大化 / 元のサイズ", L"Maximize / Restore", L"Agrandir / Restaurer", L"Ingrandisci / Ripristina", L"Maximizar / Restaurar", L"최대화 / 복원", L"最大化 / 还原", L"تكبير / استعادة", L"Развернуть / Восстановить", L"Maximieren / Wiederherstellen", L"Maximizar / Restaurar", L"Maximaliseren / Herstellen", L"Maksymalizuj / Przywroc", L"Buyut / Geri yukle"));
        if (e->pSettings)
            pTip->AddTool(e->pSettings, LL14(L"設定（描画・アクリルなど）", L"Settings (render, acrylic, etc.)", L"Parametres (rendu, acrylique...)", L"Impostazioni (render, acrilico...)", L"Ajustes (render, acrilico...)", L"설정(렌더/아크릴 등)", L"设置（渲染、亚克力等）", L"الإعدادات (العرض، الأكريليك...)", L"Настройки (рендер, акрил...)", L"Einstellungen (Render, Acryl...)", L"Configuracoes (render, acrilico...)", L"Instellingen (render, acryl...)", L"Ustawienia (render, akryl...)", L"Ayarlar (render, akrilik...)"));
        if (e->pPin)
            pTip->AddTool(e->pPin, LL14(L"常に手前に表示", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano", L"Siempre visible", L"항상 위", L"总在最前", L"دائماً في المقدمة", L"Поверх всех окон", L"Immer im Vordergrund", L"Sempre no topo", L"Altijd bovenop", L"Zawsze na wierzchu", L"Her zaman ustte"));
        CCustomControlUtility::FinalizeDialogToolTip(*pTip);
    }

    CRect crc;
    pDlg->GetClientRect(&crc);
    pDlg->SendMessage(WM_SIZE, SIZE_RESTORED, MAKELPARAM(crc.Width(), crc.Height()));
    pDlg->Invalidate(FALSE);
}

// CCC_MainLockUnregister: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_MainLockUnregister(HWND hWnd)
{
    for (int i = 0; i < g_mainLockCount; ++i) {
        if (g_mainLocks[i].hWnd != hWnd)
            continue;
        CCC_MainLockDestroyBtn(&g_mainLocks[i]);
        CCC_MainLockReleaseOverlayCache(&g_mainLocks[i]);
        for (int j = i + 1; j < g_mainLockCount; ++j)
            g_mainLocks[j - 1] = g_mainLocks[j];
        --g_mainLockCount;
        g_mainLocks[g_mainLockCount].hWnd = NULL;
        g_mainLocks[g_mainLockCount].pLockBtn = nullptr;
        g_mainLocks[g_mainLockCount].pSaveFlag = nullptr;
        g_mainLocks[g_mainLockCount].pOverlay = nullptr;
        g_mainLocks[g_mainLockCount].locked = FALSE;
        g_mainLocks[g_mainLockCount].overlayPaint = FALSE;
        break;
    }
}

// CCC_MainLockOnMainMoving: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_MainLockOnMainMoving(LPRECT pMainRect)
{
    if (!pMainRect || g_mainLockInternalMove)
        return;
    g_mainLockInternalMove = TRUE;
    g_mainLockQuickPresentUntil = GetTickCount() + 200;
    for (int i = 0; i < g_mainLockCount; ++i) {
        CCC_MainLockEntry& e = g_mainLocks[i];
        if (!e.locked || !::IsWindow(e.hWnd))
            continue;
        // LockWindowUpdate はシステム全体で1つの排他ロックで、WM_MOVING の
        // tick 毎に取得/解放すると画面全体(mp バナー含む)が点滅する。使わない。
        // 移動・リサイズとも密着辺(横/上/下)を保って追随する。
        CCC_MainLockPlaceChild(e, pMainRect);
    }
    g_mainLockInternalMove = FALSE;
}

// ---- 四辺リサイズ時の隣窓連鎖押し出し（追従フラグ無関係）----
// ENTERSIZEMOVE 無しでも pOld→pNew の差分で動く（増分）。密着は広め＋最近傍フォールバック。
// SINK: Winの不可視リサイズ枠で「見た目密着」が gap=-7〜-20 になりがちなので深めに許容。
// 左/上辺リサイズでは右隣を MainLockOnMainMoving(左上相対)で動かさないこと。
enum { CCC_CASCADE_MAX = 16, CCC_CASCADE_GAP = 280, CCC_CASCADE_NEAR = 1600, CCC_CASCADE_SINK = 56 };

struct CCC_CascadeSnap {
    BOOL active;
    HWND hMain;
    RECT mainRc;
    HWND hWnd[CCC_CASCADE_MAX];
    RECT rc[CCC_CASCADE_MAX];
    BYTE locked[CCC_CASCADE_MAX];
    BYTE dockH[CCC_CASCADE_MAX];
    BYTE dockV[CCC_CASCADE_MAX];
    int n;
};
static CCC_CascadeSnap g_cascadeSnap = {};

// CCC_CascadeVertOverlap: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_CascadeVertOverlap(const RECT& a, const RECT& b)
{
    return a.top < b.bottom && b.top < a.bottom;
}
// CCC_CascadeHorzOverlap: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_CascadeHorzOverlap(const RECT& a, const RECT& b)
{
    return a.left < b.right && b.left < a.right;
}
// a の右辺に b が密着／めり込み／すぐ右
// CCC_CascadeTouchRight: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_CascadeTouchRight(const RECT& a, const RECT& b)
{
    if (!CCC_CascadeVertOverlap(a, b)) return FALSE;
    const int gap = b.left - a.right;
    if (gap >= -CCC_CASCADE_SINK && gap <= CCC_CASCADE_GAP)
        return TRUE;
    return FALSE;
}
// a の左辺に b が密着／めり込み／すぐ左
// CCC_CascadeTouchLeft: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_CascadeTouchLeft(const RECT& a, const RECT& b)
{
    if (!CCC_CascadeVertOverlap(a, b)) return FALSE;
    const int gap = a.left - b.right;
    if (gap >= -CCC_CASCADE_SINK && gap <= CCC_CASCADE_GAP)
        return TRUE;
    return FALSE;
}
// a の下辺に b が密着／めり込み／すぐ下
// CCC_CascadeTouchBottom: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_CascadeTouchBottom(const RECT& a, const RECT& b)
{
    if (!CCC_CascadeHorzOverlap(a, b)) return FALSE;
    const int gap = b.top - a.bottom;
    if (gap >= -CCC_CASCADE_SINK && gap <= CCC_CASCADE_GAP)
        return TRUE;
    return FALSE;
}
// a の上辺に b が密着／めり込み／すぐ上
// CCC_CascadeTouchTop: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static BOOL CCC_CascadeTouchTop(const RECT& a, const RECT& b)
{
    if (!CCC_CascadeHorzOverlap(a, b)) return FALSE;
    const int gap = a.top - b.bottom;
    if (gap >= -CCC_CASCADE_SINK && gap <= CCC_CASCADE_GAP)
        return TRUE;
    return FALSE;
}

// CCC_CascadeCollect: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CascadeCollect(HWND hMain, CCC_CascadeSnap& snap, const RECT* pMainRcOpt)
{
    ZeroMemory(&snap, sizeof(snap));
    if (!hMain || !::IsWindow(hMain))
        return;
    snap.hMain = hMain;
    if (pMainRcOpt)
        snap.mainRc = *pMainRcOpt;
    else
        ::GetWindowRect(hMain, &snap.mainRc);
    for (int i = 0; i < g_mainLockCount && snap.n < CCC_CASCADE_MAX; ++i) {
        CCC_MainLockEntry& e = g_mainLocks[i];
        if (!::IsWindow(e.hWnd) || e.hWnd == hMain)
            continue;
        if (!::IsWindowVisible(e.hWnd) || ::IsIconic(e.hWnd))
            continue;
        const int idx = snap.n++;
        snap.hWnd[idx] = e.hWnd;
        ::GetWindowRect(e.hWnd, &snap.rc[idx]);
        snap.locked[idx] = e.locked ? 1 : 0;
        snap.dockH[idx] = (BYTE)e.dockH;
        snap.dockV[idx] = (BYTE)e.dockV;
    }
}

// CCC_NeighborCascadeBegin: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_NeighborCascadeBegin(HWND hMain)
{
    CCC_CascadeCollect(hMain, g_cascadeSnap, NULL);
    g_cascadeSnap.active = TRUE;
}

// CCC_NeighborCascadeEnd: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_NeighborCascadeEnd()
{
    g_cascadeSnap.active = FALSE;
    g_cascadeSnap.hMain = NULL;
    g_cascadeSnap.n = 0;
}

// CCC_CascadeGrowReach: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CascadeGrowReach(
    BYTE* reach, int n, const RECT* pOldMain, const RECT* liveRc,
    BOOL (*touchMain)(const RECT&, const RECT&),
    BOOL (*touchPeer)(const RECT&, const RECT&),
    int (*gapFromMain)(const RECT&, const RECT&),
    BOOL (*overlapMain)(const RECT&, const RECT&))
{
    BOOL any = FALSE;
    for (int i = 0; i < n; ++i) {
        if (touchMain(*pOldMain, liveRc[i])) {
            reach[i] = 1; any = TRUE;
        }
    }
    if (!any) {
        int best = -1, bestGap = CCC_CASCADE_NEAR + 1;
        for (int i = 0; i < n; ++i) {
            const RECT& wi = liveRc[i];
            if (!overlapMain(*pOldMain, wi)) continue;
            const int gap = gapFromMain(*pOldMain, wi);
            if (gap < -CCC_CASCADE_SINK || gap > CCC_CASCADE_NEAR) continue;
            if (gap < bestGap) { bestGap = gap; best = i; }
        }
        if (best >= 0) { reach[best] = 1; any = TRUE; }
    }
    if (!any) return;
    BOOL grew = TRUE;
    while (grew) {
        grew = FALSE;
        for (int i = 0; i < n; ++i) {
            if (reach[i]) continue;
            for (int j = 0; j < n; ++j) {
                if (!reach[j]) continue;
                if (touchPeer(liveRc[j], liveRc[i])) {
                    reach[i] = 1; grew = TRUE; break;
                }
            }
        }
    }
}

// CCC_CascadeGapRight: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static int CCC_CascadeGapRight(const RECT& m, const RECT& w) { return w.left - m.right; }
// CCC_CascadeGapLeft: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static int CCC_CascadeGapLeft(const RECT& m, const RECT& w) { return m.left - w.right; }
// CCC_CascadeGapBottom: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static int CCC_CascadeGapBottom(const RECT& m, const RECT& w) { return w.top - m.bottom; }
// CCC_CascadeGapTop: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static int CCC_CascadeGapTop(const RECT& m, const RECT& w) { return m.top - w.bottom; }

// CCC_NeighborCascadeOnMainResize: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_NeighborCascadeOnMainResize(const RECT* pOldMain, const RECT* pNewMain)
{
    if (!pNewMain)
        return;

    if (!pOldMain) {
        CCC_MainLockOnMainMoving((LPRECT)pNewMain);
        return;
    }

    const int oldW = pOldMain->right - pOldMain->left;
    const int oldH = pOldMain->bottom - pOldMain->top;
    const int newW = pNewMain->right - pNewMain->left;
    const int newH = pNewMain->bottom - pNewMain->top;
    if (oldW == newW && oldH == newH) {
        CCC_MainLockOnMainMoving((LPRECT)pNewMain);
        return;
    }

    const int dxL = pNewMain->left - pOldMain->left;
    const int dxR = pNewMain->right - pOldMain->right;
    const int dyT = pNewMain->top - pOldMain->top;
    const int dyB = pNewMain->bottom - pOldMain->bottom;

    HWND hMain = g_cascadeSnap.active ? g_cascadeSnap.hMain : NULL;
    if (!hMain || !::IsWindow(hMain)) {
        CWnd* pActive = CCC_GetActiveMainWindow();
        hMain = (pActive && ::IsWindow(pActive->GetSafeHwnd())) ? pActive->GetSafeHwnd() : NULL;
    }
    if (!hMain || !::IsWindow(hMain))
        return;

    CCC_CascadeSnap live = {};
    CCC_CascadeCollect(hMain, live, pOldMain);
    if (live.n <= 0) {
        // 隣窓なし: 密着ロックだけ更新（左辺リサイズで右相対 offset を動かさない）
        if (g_mainLockInternalMove)
            return;
        g_mainLockInternalMove = TRUE;
        g_mainLockQuickPresentUntil = GetTickCount() + 200;
        for (int i = 0; i < g_mainLockCount; ++i) {
            CCC_MainLockEntry& e = g_mainLocks[i];
            if (!e.locked || !::IsWindow(e.hWnd))
                continue;
            // 左辺のみ動いたとき offsetH(0) は絶対Xを保つ（右の UI が一緒に来るのを防ぐ）
            if (dxL != 0 && dxR == 0 && e.dockH == 0)
                e.offsetX = e.offsetX - dxL;
            if (dyT != 0 && dyB == 0 && e.dockV == 0)
                e.offsetY = e.offsetY - dyT;
            CCC_MainLockPlaceChild(e, pNewMain);
        }
        g_mainLockInternalMove = FALSE;
        return;
    }

    BYTE reachR[CCC_CASCADE_MAX], reachL[CCC_CASCADE_MAX];
    BYTE reachB[CCC_CASCADE_MAX], reachT[CCC_CASCADE_MAX];
    ZeroMemory(reachR, sizeof(reachR));
    ZeroMemory(reachL, sizeof(reachL));
    ZeroMemory(reachB, sizeof(reachB));
    ZeroMemory(reachT, sizeof(reachT));
    const int n = live.n;

    if (dxR != 0) {
        CCC_CascadeGrowReach(reachR, n, pOldMain, live.rc,
            CCC_CascadeTouchRight, CCC_CascadeTouchRight,
            CCC_CascadeGapRight, CCC_CascadeVertOverlap);
    }
    if (dxL != 0) {
        CCC_CascadeGrowReach(reachL, n, pOldMain, live.rc,
            CCC_CascadeTouchLeft, CCC_CascadeTouchLeft,
            CCC_CascadeGapLeft, CCC_CascadeVertOverlap);
    }
    if (dyB != 0) {
        CCC_CascadeGrowReach(reachB, n, pOldMain, live.rc,
            CCC_CascadeTouchBottom, CCC_CascadeTouchBottom,
            CCC_CascadeGapBottom, CCC_CascadeHorzOverlap);
    }
    if (dyT != 0) {
        CCC_CascadeGrowReach(reachT, n, pOldMain, live.rc,
            CCC_CascadeTouchTop, CCC_CascadeTouchTop,
            CCC_CascadeGapTop, CCC_CascadeHorzOverlap);
    }

    if (g_mainLockInternalMove)
        return;
    g_mainLockInternalMove = TRUE;
    g_mainLockQuickPresentUntil = GetTickCount() + 200;

    for (int i = 0; i < n; ++i) {
        if (!reachR[i] && !reachL[i] && !reachB[i] && !reachT[i])
            continue;
        HWND h = live.hWnd[i];
        if (!::IsWindow(h) || ::IsZoomed(h) || ::IsIconic(h))
            continue;
        const int ox = (reachR[i] ? dxR : 0) + (reachL[i] ? dxL : 0);
        const int oy = (reachB[i] ? dyB : 0) + (reachT[i] ? dyT : 0);
        if (ox == 0 && oy == 0)
            continue;
        RECT cur;
        ::GetWindowRect(h, &cur);
        ::SetWindowPos(h, NULL, cur.left + ox, cur.top + oy, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
        if (live.locked[i]) {
            CCC_MainLockEntry* e = CCC_FindMainLockEntry(h);
            if (e)
                CCC_ComputeMainLockAttach(h, e);
        }
    }

    for (int i = 0; i < g_mainLockCount; ++i) {
        CCC_MainLockEntry& e = g_mainLocks[i];
        if (!e.locked || !::IsWindow(e.hWnd))
            continue;
        int snapIdx = -1;
        for (int s = 0; s < n; ++s) {
            if (live.hWnd[s] == e.hWnd) { snapIdx = s; break; }
        }
        if (snapIdx >= 0 && (reachR[snapIdx] || reachL[snapIdx] || reachB[snapIdx] || reachT[snapIdx]))
            continue;
        // 右辺リサイズ時の右密着は増分押し出し済み
        if (dxR != 0 && e.dockH == 1)
            continue;
        if (dxL != 0 && e.dockH == 2)
            continue;
        if (dyB != 0 && e.dockV == 1)
            continue;
        if (dyT != 0 && e.dockV == 2)
            continue;
        // 左辺だけ動かしたとき、offset 相対(dockH=0)は絶対Xを保つ
        if (dxL != 0 && dxR == 0 && e.dockH == 0)
            e.offsetX = e.offsetX - dxL;
        if (dyT != 0 && dyB == 0 && e.dockV == 0)
            e.offsetY = e.offsetY - dyT;
        CCC_MainLockPlaceChild(e, pNewMain);
    }

    g_mainLockInternalMove = FALSE;
}

// CCC_MainLockPreferQuickPresent: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
BOOL CCC_MainLockPreferQuickPresent()
{
    return GetTickCount() < g_mainLockQuickPresentUntil;
}

// CCC_MainLockRefreshOffsetsFor: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_MainLockRefreshOffsetsFor(CWnd* pMain, const RECT* pOldMain)
{
    if (!pMain || !::IsWindow(pMain->GetSafeHwnd()))
        return;

    // オフセット再計算のみ。起動時やメイン確定後に使い、絶対座標は触らない。
    // (PlaceChild すると og 基準で付けた offset のまま mp へ飛んでドリフトする)
    if (!pOldMain) {
        for (int i = 0; i < g_mainLockCount; ++i) {
            CCC_MainLockEntry& e = g_mainLocks[i];
            if (!e.locked || !::IsWindow(e.hWnd) || e.hWnd == pMain->GetSafeHwnd())
                continue;
            CCC_ComputeMainLockAttach(e.hWnd, &e);
        }
        return;
    }

    CRect newMainRc;
    pMain->GetWindowRect(&newMainRc);
    const HWND hMain = pMain->GetSafeHwnd();

    // 追随ON: 旧メインで保持している offset/dock を新メインへ適用して実窓を動かす。
    // SWP_NOREDRAW でまとめ移動し、最後に一括再描画してちらつきを防ぐ。
    HWND moved[16];
    int nMoved = 0;
    BOOL liveEq = FALSE, livePiano = FALSE, liveAn = FALSE, livePl = FALSE;
    BOOL livePrompt = FALSE, liveTune = FALSE, liveCmd = FALSE;

    extern CImageBase* playbase;
    extern CImageBase* renderbase;
    extern CImageBase* folderbase;
    CImageBase* aeroBacks[3];
    aeroBacks[0] = playbase;
    aeroBacks[1] = renderbase;
    aeroBacks[2] = folderbase;

    if (g_mainLockInternalMove)
        return;
    g_mainLockInternalMove = TRUE;
    g_mainLockQuickPresentUntil = GetTickCount() + 200;
    for (int i = 0; i < g_mainLockCount; ++i) {
        CCC_MainLockEntry& e = g_mainLocks[i];
        if (!e.locked || !::IsWindow(e.hWnd) || e.hWnd == hMain)
            continue;
        CRect before;
        ::GetWindowRect(e.hWnd, &before);
        CCC_MainLockPlaceChild(e, &newMainRc);
        CRect after;
        ::GetWindowRect(e.hWnd, &after);
        if ((before.left != after.left || before.top != after.top) && nMoved < (int)_countof(moved))
            moved[nMoved++] = e.hWnd;

        // PlaceChild は WM_MOVING を飛ばない → aero==2 グラス背面を親に追従
        for (int bi = 0; bi < 3; ++bi) {
            CImageBase* b = aeroBacks[bi];
            if (!b || !::IsWindow(b->GetSafeHwnd()) || !b->oya)
                continue;
            if (b->oya->GetSafeHwnd() != e.hWnd)
                continue;
            b->MoveWindow(&after);
        }

        // 開いている追随窓は実座標を savedata へ同期(密着辺+サイズ差も反映済み)
        if (e.pSaveFlag == &savedata.eqMainLock) {
            savedata.eqx = after.left;
            savedata.eqy = after.top;
            liveEq = TRUE;
        }
        else if (e.pSaveFlag == &savedata.pianorollMainLock) {
            savedata.pianorollx = after.left;
            savedata.pianorolly = after.top;
            livePiano = TRUE;
        }
        else if (e.pSaveFlag == &savedata.analyzerMainLock) {
            savedata.analyzerx = after.left;
            savedata.analyzery = after.top;
            liveAn = TRUE;
        }
        else if (e.pSaveFlag == &savedata.playlistMainLock) {
            savedata.p.left = after.left;
            savedata.p.top = after.top;
            savedata.p.right = after.right;
            savedata.p.bottom = after.bottom;
            livePl = TRUE;
        }
        else if (e.pSaveFlag == &savedata.mpPromptMainLock) {
            savedata.mpPromptX = after.left;
            savedata.mpPromptY = after.top;
            savedata.mpPromptHasPos = 1;
            livePrompt = TRUE;
        }
        else if (e.pSaveFlag == &savedata.prTuneMainLock) {
            savedata.prTunex = after.left;
            savedata.prTuney = after.top;
            liveTune = TRUE;
        }
        else if (e.pSaveFlag == &savedata.mpCmdRollMainLock) {
            savedata.mpCmdRollX = after.left;
            savedata.mpCmdRollY = after.top;
            savedata.mpCmdRollHasPos = 1;
            liveCmd = TRUE;
        }
    }
    g_mainLockInternalMove = FALSE;

    for (int i = 0; i < nMoved; ++i) {
        ::RedrawWindow(moved[i], NULL, NULL,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }

    // 閉じている追随ON窓: 旧メイン→新メインのデルタで保存座標だけ変換
    const int dx = newMainRc.left - pOldMain->left;
    const int dy = newMainRc.top - pOldMain->top;
    if (dx == 0 && dy == 0)
        return;
    if (savedata.eqMainLock && !liveEq && savedata.eqx != -1) {
        savedata.eqx += dx;
        savedata.eqy += dy;
        CCC_ClampWindowPos(savedata.eqx, savedata.eqy, 320, 240);
    }
    if (savedata.pianorollMainLock && !livePiano && savedata.pianorollx != -1) {
        savedata.pianorollx += dx;
        savedata.pianorolly += dy;
        int pw = savedata.pianorollw > 0 ? savedata.pianorollw : 800;
        int ph = savedata.pianorollh > 0 ? savedata.pianorollh : 450;
        CCC_ClampWindowPos(savedata.pianorollx, savedata.pianorolly, pw, ph);
    }
    if (savedata.analyzerMainLock && !liveAn && savedata.analyzerx != -1) {
        savedata.analyzerx += dx;
        savedata.analyzery += dy;
        CCC_ClampWindowPos(savedata.analyzerx, savedata.analyzery, 400, 300);
    }
    if (savedata.playlistMainLock && !livePl) {
        const int pw = savedata.p.right - savedata.p.left;
        const int ph = savedata.p.bottom - savedata.p.top;
        savedata.p.left += dx;
        savedata.p.right += dx;
        savedata.p.top += dy;
        savedata.p.bottom += dy;
        int px = savedata.p.left, py = savedata.p.top;
        CCC_ClampWindowPos(px, py, pw > 0 ? pw : 400, ph > 0 ? ph : 300);
        savedata.p.left = px;
        savedata.p.top = py;
        savedata.p.right = px + (pw > 0 ? pw : 400);
        savedata.p.bottom = py + (ph > 0 ? ph : 300);
    }
    if (savedata.mpPromptMainLock && !livePrompt && savedata.mpPromptHasPos) {
        savedata.mpPromptX += dx;
        savedata.mpPromptY += dy;
        int pw = savedata.mpPromptW > 0 ? savedata.mpPromptW : 480;
        int ph = savedata.mpPromptH > 0 ? savedata.mpPromptH : 360;
        CCC_ClampWindowPos(savedata.mpPromptX, savedata.mpPromptY, pw, ph);
    }
    if (savedata.prTuneMainLock && !liveTune && savedata.prTunex != -1) {
        savedata.prTunex += dx;
        savedata.prTuney += dy;
        CCC_ClampWindowPos(savedata.prTunex, savedata.prTuney, 360, 280);
    }
    if (savedata.mpCmdRollMainLock && !liveCmd && savedata.mpCmdRollHasPos) {
        savedata.mpCmdRollX += dx;
        savedata.mpCmdRollY += dy;
        int pw = savedata.mpCmdRollW > 0 ? savedata.mpCmdRollW : 900;
        int ph = savedata.mpCmdRollH > 0 ? savedata.mpCmdRollH : 560;
        CCC_ClampWindowPos(savedata.mpCmdRollX, savedata.mpCmdRollY, pw, ph);
    }
}

// CCC_MainLockRefreshOffsets: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_MainLockRefreshOffsets()
{
    CCC_MainLockRefreshOffsetsFor(CCC_GetActiveMainWindow(), NULL);
}

// CCC_MainLockOnChildMoving: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
void CCC_MainLockOnChildMoving(CWnd* pDlg, LPRECT pRect)
{
    if (!pDlg || !pRect || g_mainLockInternalMove)
        return;
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(pDlg->GetSafeHwnd());
    if (!e || !e->locked)
        return;
    CWnd* pMain = CCC_GetActiveMainWindow();
    if (!pMain)
        return;
    CRect mainRc;
    pMain->GetWindowRect(&mainRc);
    // 移動中の仮矩形で密着を再計算(GetWindowRect はまだ旧位置のことがある)
    e->offsetX = pRect->left - mainRc.left;
    e->offsetY = pRect->top - mainRc.top;
    e->dockH = e->dockV = 0;
    e->gapX = e->gapY = 0;
    const int right = pRect->right;
    const int bottom = pRect->bottom;
    if (pRect->left >= mainRc.right - 8) {
        e->dockH = 1;
        e->gapX = pRect->left - mainRc.right;
    }
    else if (right <= mainRc.left + 8) {
        e->dockH = 2;
        e->gapX = mainRc.left - right;
    }
    if (pRect->top >= mainRc.bottom - 8) {
        e->dockV = 1;
        e->gapY = pRect->top - mainRc.bottom;
    }
    else if (bottom <= mainRc.top + 8) {
        e->dockV = 2;
        e->gapY = mainRc.top - bottom;
    }
}

static void CCC_CaptionTrackContextMenu(CWnd* pDlg, CPoint ptClient, int* pMainLockSave);
static void CCC_CaptionOpenSettings(CWnd* pDlg);
static void CCC_CaptionTogglePin(CWnd* pDlg);
static void CCC_CaptionToggleMaximize(CWnd* pDlg);

// ============================================================================
// アクリルぼかし適用済みカスタムダイアログ (CDialog版)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomBlurDialogBase, CCustomDialog)

BEGIN_MESSAGE_MAP(CCustomBlurDialogBase, CCustomDialog)
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_SHOWWINDOW()
    ON_WM_WINDOWPOSCHANGED()
    ON_WM_DWMCOMPOSITIONCHANGED()
    ON_WM_DESTROY()
    ON_WM_MOVING()
    ON_COMMAND(IDC_MAINWIN_LOCK, OnMainLockClicked)
    ON_COMMAND(IDC_CAP_CLOSE, OnCapClose)
    ON_COMMAND(IDC_CAP_MIN, OnCapMin)
    ON_COMMAND(IDC_CAP_MAX, OnCapMax)
    ON_COMMAND(IDC_CAP_SETTINGS, OnCapSettings)
    ON_COMMAND(IDC_CAP_PIN, OnCapPin)
    ON_COMMAND(IDC_CAP_OFFLINE_HELP, OnCapOfflineHelp)
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_RBUTTONUP()
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnTtnNeedText)
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnTtnNeedText)
    ON_MESSAGE(CCC_MSG_INSTALL_CAPTION, OnInstallCustomCaption)
    ON_WM_NCCALCSIZE()
    ON_WM_NCACTIVATE()
    ON_MESSAGE(0x00AE, OnNcThemeCaptionPaint)
    ON_MESSAGE(0x00AF, OnNcThemeCaptionPaint)
#if CCUSTOM_AERO_SUPPORT
    ON_MESSAGE(CCC_MSG_REAPPLY_OPAQUE_FIXERS, OnReapplyOpaqueFixers)
#endif
END_MESSAGE_MAP()

// ダイアログクラスをコピーし背景ブラシとアイコンを空にする。
// コピーした空/不正アイコンがキャプションに出るのを防ぐ。
static BOOL RegisterBlurDialogWndClass(LPCTSTR pszClass, LPCTSTR pszNewClass)
{
    WNDCLASS wc = {};
    HINSTANCE hInst = AfxGetInstanceHandle();
    if (!::GetClassInfo(hInst, pszClass, &wc) && !::GetClassInfo(NULL, pszClass, &wc))
        return FALSE;
    wc.hbrBackground = NULL;
    // ダイアログクラスからコピーした空/不正アイコンがキャプションに出るのを防ぐ
    wc.hIcon = NULL;
    wc.lpszClassName = pszNewClass;
    wc.hInstance = hInst;
    return AfxRegisterClass(&wc) != FALSE;
}

// CCustomBlurDialogBase のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。実初期化は OnInitDialog / PreCreateWindow。
CCustomBlurDialogBase::CCustomBlurDialogBase() : m_bBlurApplied(FALSE) {}
// CCustomBlurDialogBase のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。実初期化は OnInitDialog / PreCreateWindow。
CCustomBlurDialogBase::CCustomBlurDialogBase(UINT n, CWnd* p) : CCustomDialog(n, p), m_bBlurApplied(FALSE) {}
// CCustomBlurDialogBase の破棄。fixer / キャプションは OnDestroy。ここは空。
CCustomBlurDialogBase::~CCustomBlurDialogBase() {}

// AcrylicCaption 常時のため null brush 専用クラス。#32770 のクラスブラシは黒帯になる。
// save.aero に関係なく登録する。
BOOL CCustomBlurDialogBase::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CCustomDialog::PreCreateWindow(cs))
        return FALSE;
#if CCUSTOM_AERO_SUPPORT
    // AcrylicCaption 常時のため、save.aero に関係なく null brush 専用クラスを使う。
    // #32770 のままだとクラスブラシが残り、αクリアが黒帯になる。
    RegisterBlurDialogWndClass(cs.lpszClass, _T("CCustomBlurDlg"));
    cs.lpszClass = _T("CCustomBlurDlg");
#endif
    return TRUE;
}

// 子を CCustom* へサブクラス。Blur 系はキャプション install を Post。
// 子サブクラス。Blur はキャプションを Show 前に。
BOOL CCustomBlurDialogBase::OnInitDialog()
{
    BOOL b = CCustomDialog::OnInitDialog();
    // クラス既定アイコンを消し、テンプレート対応のリソースアイコンを載せる。
    // MP/ファルコム本窓/プレイリストはマップで 0 を返し、後段の SetIcon(m_hIcon) に任せる。
    SetIcon(nullptr, TRUE);
    SetIcon(nullptr, FALSE);
    {
        UINT idd = 0;
        if (m_lpszTemplateName && IS_INTRESOURCE(m_lpszTemplateName))
            idd = (UINT)(ULONG_PTR)m_lpszTemplateName;
        CCC_ApplyWindowIconFromTemplate(this, idd);
    }
#if CCUSTOM_AERO_SUPPORT
    ::SetClassLongPtr(m_hWnd, GCLP_HBRBACKGROUND, 0);
#endif
    ApplyDwmBlur();
    // キャプション化は初回 OnShowWindow（表示前）で行う。PostMessage だと Show 後に走り、システム帯の一瞬アクリル＋縦幅ジャンプになる。
    return b;
}

// 遅延キャプション導入。OnInit 時点では NC がまだ不安定。
// Show 前に一度だけ。
LRESULT CCustomBlurDialogBase::OnInstallCustomCaption(WPARAM, LPARAM)
{
    CCC_CaptionInstallCore(this, &m_capTip);
#if CCUSTOM_AERO_SUPPORT
    CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
#endif
    return 0;
}

// カスタム帯分の NC 吸収。FRAMECHANGED と対。
// 帯高さ分をクライアントへ取り込む。
void CCustomBlurDialogBase::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
{
    if (CCC_GetCustomCaptionHeight(m_hWnd) > 0 && lpncsp) {
        CCC_CaptionHandleNcCalcSize(m_hWnd, bCalcValidRects ? TRUE : FALSE, reinterpret_cast<LPARAM>(lpncsp), 0);
        return;
    }
    CCustomDialog::OnNcCalcSize(bCalcValidRects, lpncsp);
}

// システムキャプションの再描画を止める。lParam=-1 が定石。帯の活性色はクライアント側で。
BOOL CCustomBlurDialogBase::OnNcActivate(BOOL bActive)
{
    if (CCC_GetCustomCaptionHeight(m_hWnd) > 0) {
        const BOOL r = (BOOL)::DefWindowProc(m_hWnd, WM_NCACTIVATE, (WPARAM)bActive, (LPARAM)-1);
        const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
        CRect cr;
        GetClientRect(&cr);
        if (cr.bottom > capH)
            cr.bottom = capH;
        InvalidateRect(&cr, FALSE);
        return r;
    }
    return CCustomDialog::OnNcActivate(bActive);
}

// WM_NCUAHDRAWCAPTION / WM_NCUAHDRAWFRAME。テーマのシステム帯描画を捨てる。
LRESULT CCustomBlurDialogBase::OnNcThemeCaptionPaint(WPARAM, LPARAM)
{
    if (CCC_GetCustomCaptionHeight(m_hWnd) > 0)
        return 0;
    return Default();
}

// 設定変更など強制再適用。bForce=TRUE で FinishBlur をやり直す。
// 二重 FRAMECHANGED は Core 側の m_bInApplyBlur で防ぐ。
void CCustomBlurDialogBase::RefreshAeroMode()
{
    ApplyDwmBlurCore(TRUE);
}

// 通常経路。bForce=FALSE なので既適用なら何もしない。
// FRAMECHANGED 連打で帯が凍るのをここで止める。
void CCustomBlurDialogBase::ApplyDwmBlur()
{
    ApplyDwmBlurCore(FALSE);
}

// ぼかしの唯一の入口。m_bInApplyBlur で同一 HWND 再入を防ぐ。
// 全窓共通 static にしない（RefreshAll が後続窓をスキップするため）。
// 既に m_bBlurApplied かつ !bForce なら二重適用しない（FRAMECHANGED で凍る）。
// OFF は ApplyAero(FALSE) を使わず本文だけ解除（ホスト α を落とすと黒帯）。
// キャプション導入済みなら GlassAndFixers を一度載せる。
void CCustomBlurDialogBase::ApplyDwmBlurCore(BOOL bForce)
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
#if CCUSTOM_AERO_SUPPORT
    // 同一 HWND への再入のみ防止（全窓共通 static だと RefreshAll が後続窓をスキップする）
    // 再入・二重 FRAMECHANGED 禁止。既適用かつ !bForce はすぐ return。
    if (m_bInApplyBlur) return;
    m_bInApplyBlur = TRUE;

    const BOOL bWant = CCC_IsAeroEnabled();
    if (bWant)
    {
        if (m_bBlurApplied && !bForce) {
            m_bInApplyBlur = FALSE;
            return;
        }
        m_bAeroEnabled = TRUE;
        CCC_FinishBlurDlg(this, TRUE, m_bBlurApplied, m_opaqueFixers);
        // キャプション未導入時は初回 OnShowWindow で帯＋fixer を一度だけ載せる
        if (CCC_GetCustomCaptionHeight(m_hWnd) > 0)
            CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
        if (m_pMainLockSave)
            CCC_MainLockBringToFront(m_hWnd);
        m_bInApplyBlur = FALSE;
        return;
    }

    m_bAeroEnabled = FALSE;
    if (!m_bBlurApplied && !bForce) {
        if (CCC_GetCustomCaptionHeight(m_hWnd) > 0)
            CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
        m_bInApplyBlur = FALSE;
        return;
    }
    // 本文 aero だけ解除。ApplyAero(FALSE) はホスト α/backdrop まで落とすので使わない
    // （落とす→Ensure の隙間で黒帯が定着する）
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
    CCC_DisableBodyAeroOnly(m_hWnd);
    CCC_PrepareDialogSurface(m_hWnd, FALSE);
    PROPAGATE_AERO_TO_CHILDREN(m_hWnd, FALSE);
    m_bBlurApplied = FALSE;
    if (CCC_GetCustomCaptionHeight(m_hWnd) > 0)
        CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
    m_bInApplyBlur = FALSE;
#else
    UNREFERENCED_PARAMETER(bForce);
    m_bBlurApplied = FALSE;
#endif
}

// 表示前にキャプション化（システム帯のフラッシュと NC 吸収ジャンプを隠す）。
// 本文 aero は ApplyDwmBlur 1 回。本文 off で RefreshDwmBlur するとガラスに戻る。
void CCustomBlurDialogBase::OnShowWindow(BOOL bShow, UINT nStatus)
{
    // 表示前にキャプション化（システム帯のフラッシュと NC 吸収ジャンプを隠す）
    if (bShow) {
        CCC_CaptionInstallCore(this, &m_capTip);
#if CCUSTOM_AERO_SUPPORT
        CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
#endif
    }
    CCustomDialog::OnShowWindow(bShow, nStatus);
    if (m_pMainLockSave)
        CCC_MainLockShowBtn(m_hWnd, bShow);
    if (!bShow)
        return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled())
    {
        ApplyDwmBlur();
        CCC_RefreshDwmBlur(m_hWnd);
        CCC_CaptionEnsureBackdrop(m_hWnd);
    }
    // 本文 off は CaptionInstall 直後の ApplyGlassAndFixers 1 回。
    // RefreshDwmBlur を後段で呼ぶと本文不透明塗りがガラスに戻るので呼ばない。
#endif
    UNREFERENCED_PARAMETER(nStatus);
}

// Win11+aero: 隙間＋帯。キャプションのみ時は帯の【後】に本文を不透明化。
// EnsureBackdrop(ExtendFrame) が本文 α をガラスに戻すため順序が重要。
void CCustomBlurDialogBase::OnPaint()
{
    CPaintDC dc(this);
#if CCUSTOM_AERO_SUPPORT
    if (m_bAeroEnabled && CCC_IsWin11())
    {
        CCC_PaintAeroGaps(dc, this, nullptr);
        CCC_CaptionPaint(dc, m_hWnd);
        if (m_pMainLockSave)
            CCC_MainLockPaintClient(dc, m_hWnd);
        return;
    }
    if (m_bAeroEnabled)
    {
        CRect rect;
        GetClientRect(&rect);
        dc.FillSolidRect(&rect, RGB(250, 250, 250));
    }
#endif
    // CaptionPaint 内の EnsureBackdrop(ExtendFrame) が本文 α をガラスに戻すので、
    // キャプションのみ時は帯描画の【後】に本文を不透明化する
    CCC_CaptionPaint(dc, m_hWnd);
#if CCUSTOM_AERO_SUPPORT
    if (!m_bAeroEnabled && CCC_AcrylicCaption(m_hWnd))
        CCC_PaintOpaqueBodyBelowCaption(dc, this, m_brDialog);
#endif
    if (m_pMainLockSave)
        CCC_MainLockPaintClient(dc, m_hWnd);
}

// キャプション／追従エントリ解除、fixer 破棄。
// 基底 OnDestroy の前に登録を外す。
void CCustomBlurDialogBase::OnDestroy()
{
    CCC_CaptionUnregister(m_hWnd);
    CCC_MainLockUnregister(m_hWnd);
#if CCUSTOM_AERO_SUPPORT
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
#endif
    CCustomDialog::OnDestroy();
}

// キャプションボタン再配置。最大化アイコンの切替。
// chrome 再配置。
void CCustomBlurDialogBase::OnSize(UINT nType, int cx, int cy)
{
    CCustomDialog::OnSize(nType, cx, cy);
    CCC_CaptionLayout(m_hWnd);
    if (m_pMainLockSave)
        CCC_MainLockBringToFront(m_hWnd);
    if (CCC_CaptionEntry* e = CCC_FindCaption(m_hWnd)) {
        if (e->installed && e->pMax && ::IsWindow(e->pMax->GetSafeHwnd())) {
            if (nType == SIZE_RESTORED)
                e->manualZoomed = FALSE;
            else if (nType == SIZE_MAXIMIZED)
                e->manualZoomed = TRUE;
            CCC_CaptionApplySharedIcon(e->pMax,
                (IsZoomed() || e->manualZoomed) ? IDI_CTL_RESTORE : IDI_CTL_MAX);
        }
    }
}

// SHOW で未適用ならぼかしを載せる（初期 Show 経路の穴埋め）。
// 初回 SHOW のぼかし穴埋め。
void CCustomBlurDialogBase::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
    CCustomDialog::OnWindowPosChanged(lpwndpos);
#if CCUSTOM_AERO_SUPPORT
    if (lpwndpos && (lpwndpos->flags & SWP_SHOWWINDOW) && !m_bBlurApplied && CCC_IsAeroEnabled())
        ApplyDwmBlur();
#endif
}

// DWM 構成変化。ぼかしを強制再適用。
// DWM 再構成。
void CCustomBlurDialogBase::OnCompositionChanged()
{
#if CCUSTOM_AERO_SUPPORT
    ApplyDwmBlurCore(TRUE);
#endif
}

// 子が増えたあと fixer を張り直す。追従ボタンを前面へ。
// 動的に増えた子用。
LRESULT CCustomBlurDialogBase::OnReapplyOpaqueFixers(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsWin11() && (m_bAeroEnabled || CCC_AcrylicCaption(m_hWnd)))
        CCC_ReapplyOpaqueFix(this, m_opaqueFixers);
#endif
    if (m_pMainLockSave)
        CCC_MainLockBringToFront(m_hWnd);
    return 0;
}

// 子ダイアログをメイン窓へ位置追従させる。pSavedLockFlag は ini 等の保存先。
// bOverlayPaint=TRUE ならキャプション内オーバーレイ（実ボタンを出さない）。
// クリックは OnMainLockClicked / オーバーレイ HitTest。
void CCustomBlurDialogBase::EnableMainWindowLock(int* pSavedLockFlag, BOOL bOverlayPaint)
{
    m_pMainLockSave = pSavedLockFlag;
    // 保存フラグを結び、帯内オーバーレイか実ボタンかを決める。
    CCC_MainLockSetup(this, pSavedLockFlag, bOverlayPaint);
}

// 「メインに追従」ボタン／オーバーレイのトグル入口。
// 保存フラグも反転。
void CCustomBlurDialogBase::OnMainLockClicked()
{
    CCC_MainLockOnClicked(m_hWnd);
}

// 帯の空きは HTCAPTION ドラッグ。最大化中は先に復元。
// 帯ドラッグは HTCAPTION。
void CCustomBlurDialogBase::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (m_pMainLockSave && CCC_MainLockOverlayHitTest(m_hWnd, point)) {
        CCC_MainLockOverlayToggle(m_hWnd);
        return;
    }
    const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
    if (capH > 0 && point.y >= 0 && point.y < capH) {
        CWnd* pHit = ChildWindowFromPoint(point, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!pHit || pHit == this) {
            // 最大化中のタイトルドラッグで元サイズへ戻す
            CCC_CaptionEntry* e = CCC_FindCaption(m_hWnd);
            if (IsZoomed() || (e && e->manualZoomed))
                CCC_CaptionToggleMaximize(this);
            SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
            return;
        }
    }
    CCustomDialog::OnLButtonDown(nFlags, point);
}

// 帯ダブルクリックで最大化トグル（hasMax 時）。
// 帯で最大化トグル。
void CCustomBlurDialogBase::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
    if (capH > 0 && point.y >= 0 && point.y < capH) {
        CWnd* pHit = ChildWindowFromPoint(point, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!pHit || pHit == this) {
            CCC_CaptionEntry* e = CCC_FindCaption(m_hWnd);
            if (e && e->hasMax)
                OnCapMax();
            return;
        }
    }
    CCustomDialog::OnLButtonDblClk(nFlags, point);
}

// コンテキストメニュー。キャプション帯ならシステムメニューへ。
// 空き帯以外は既定の右クリック。
void CCustomBlurDialogBase::OnRButtonUp(UINT nFlags, CPoint point)
{
    const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
    if (capH > 0 && point.y >= 0 && point.y < capH) {
        CCC_CaptionTrackContextMenu(this, point, m_pMainLockSave);
        return;
    }
    CWnd::OnRButtonUp(nFlags, point);
}

// カスタム×。システムメニューと同じ SC_CLOSE。
// BN_CLICKED はメッセージマップ IDC_CAP_CLOSE。
void CCustomBlurDialogBase::OnCapClose()
{
    SendMessage(WM_SYSCOMMAND, SC_CLOSE, 0);
}

// カスタム最小化。SC_MINIMIZE（タスクバーへ）。
// カスタム帯の Min ボタンから。
void CCustomBlurDialogBase::OnCapMin()
{
    SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
}

// カスタム最大化／復元。手動ズームフラグも見る。
// ポップアップでも THICKFRAME ならボタンを出す。
void CCustomBlurDialogBase::OnCapMax()
{
    CCC_CaptionToggleMaximize(this);
}

// 歯車。設定ダイアログを開く。
// レンダラ系窓ではボタン自体を出さない。
void CCustomBlurDialogBase::OnCapSettings()
{
    CCC_CaptionOpenSettings(this);
}

// ピン。TOPMOST トグル。
// アイコンは PIN / PINOFF を入れ替える。
void CCustomBlurDialogBase::OnCapPin()
{
    CCC_CaptionTogglePin(this);
}

// オフラインヘルプ（CHM）。F1 と同じ。
// CHM。F1 と同じ。
void CCustomBlurDialogBase::OnCapOfflineHelp()
{
    OfflineHelpOpen(m_hWnd);
}

// キャプションボタンのツールチップは各 AddTool 側。ここでは握るだけ。
// AddTool 側の文字列を使う。
BOOL CCustomBlurDialogBase::OnTtnNeedText(UINT, NMHDR*, LRESULT* pResult)
{
    *pResult = 0;
    return FALSE;
}

// ツールチップ Relay。ダイアログ側は F1 ヘルプ。隠し演出入力は短く見るだけ。
// キャプションボタンのチップは m_capTip.RelayEvent。
BOOL CCustomBlurDialogBase::PreTranslateMessage(MSG* pMsg)
{
    if (CCC_InwomanHotkey(pMsg, this))
        return TRUE;
    if (pMsg && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_F1)
    {
        OfflineHelpOpen(m_hWnd);
        return TRUE;
    }
    if (m_capTip.GetSafeHwnd())
        m_capTip.RelayEvent(pMsg);
    return CCustomDialog::PreTranslateMessage(pMsg);
}

// 移動中。追従 ON ならオフセットを更新。カスケード密着も見る。
// 追従 ON ならオフセット更新。
void CCustomBlurDialogBase::OnMoving(UINT fwSide, LPRECT pRect)
{
    CCustomDialog::OnMoving(fwSide, pRect);
    CCC_MainLockOnChildMoving(this, pRect);
}

// ============================================================================
// カスタムダイアログクラス基底 (CDialogEx版)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomDialogEx, CDialogEx)

BEGIN_MESSAGE_MAP(CCustomDialogEx, CDialogEx)
    ON_WM_CTLCOLOR()
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_MESSAGE(WM_USER + 1000, OnSubclassControls)
END_MESSAGE_MAP()

// CCustomDialogEx のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。実初期化は OnInitDialog / PreCreateWindow。
CCustomDialogEx::CCustomDialogEx() : m_bAeroEnabled(FALSE)
{
    m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
    m_brNull.CreateStockObject(NULL_BRUSH);
}

// CCustomDialogEx のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。実初期化は OnInitDialog / PreCreateWindow。
CCustomDialogEx::CCustomDialogEx(UINT n, CWnd* p) : CDialogEx(n, p), m_bAeroEnabled(FALSE)
{
    m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
    m_brNull.CreateStockObject(NULL_BRUSH);
}

// CCustomDialogEx の破棄。ダイアログ／NULL ブラシのみ。fixer は OnDestroy。
CCustomDialogEx::~CCustomDialogEx()
{
    if (m_brDialog.GetSafeHandle()) m_brDialog.DeleteObject();
    if (m_brNull.GetSafeHandle()) m_brNull.DeleteObject();
}

// 本文ぼかしフラグ。HWND があれば ApplyAero と子へ伝播。
// 子へ PROPAGATE。
void CCustomDialogEx::EnableAero(BOOL b)
{
    m_bAeroEnabled = b;
#if CCUSTOM_AERO_SUPPORT
    if (GetSafeHwnd())
    {
        CCC_ApplyAero(m_hWnd, b);
        CCC_PrepareDialogSurface(m_hWnd, b);
        PROPAGATE_AERO_TO_CHILDREN(m_hWnd, b);
        Invalidate();
    }
#endif
}

// 子を CCustom* へサブクラス。Blur 系はキャプション install を Post。
// 子サブクラス。Blur はキャプションを Show 前に。
BOOL CCustomDialogEx::OnInitDialog()
{
    BOOL b = CDialogEx::OnInitDialog();
    SubclassChildControls();
    return b;
}

// 遅延サブクラス。初期化順で HWND が後から付く子用。
// 遅延 HWND 用。
LRESULT CCustomDialogEx::OnSubclassControls(WPARAM, LPARAM)
{
    SubclassChildControls();
    return 0;
}

// 標準コントロールを CCustom* に差し替え（Edit/Button/List 等）。
// 標準子を CCustom* へ。
void CCustomDialogEx::SubclassChildControls()
{
    DoSubclassChildControls(this);
}

// 未サブクラス子の背景。アクリル時は null brush 寄り。
// サブクラス済み CCustom* は CtlColor 反射側。
HBRUSH CCustomDialogEx::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nC)
{
    HBRUSH h = DlgOnCtlColor(pDC, pWnd, nC, m_brDialog, m_bAeroEnabled);
    return h ? h : CDialogEx::OnCtlColor(pDC, pWnd, nC);
}

// 消去握りつぶし。アクリル上で ERASE だけだと完全透過のまま残る。
// TRUE で既定塗りを止める。
BOOL CCustomDialogEx::OnEraseBkgnd(CDC* pDC)
{
    return DlgOnEraseBkgnd(pDC, m_brDialog, m_bAeroEnabled, m_hWnd);
}

// WM_PAINT。透過はクロマ、ホストガラスは不透明パス、それ以外は素描画。
// CPaintDC。経路分岐は aero / ホストガラス / 通常。
void CCustomDialogEx::OnPaint()
{
    if (m_bAeroEnabled)
        DlgOnPaintAero(this, m_bAeroEnabled);
    else if (CCC_IsInwoman())
        DlgPaintSolidInwoman(this);
    else
        CDialogEx::OnPaint();
}

// CCC_CaptionTrackContextMenu: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionTrackContextMenu(CWnd* pDlg, CPoint ptClient, int* pMainLockSave)
{
    if (!pDlg || !::IsWindow(pDlg->GetSafeHwnd()))
        return;
    HWND hWnd = pDlg->GetSafeHwnd();
    CPoint scr = ptClient;
    pDlg->ClientToScreen(&scr);
    CCustomPopupMenu menu;
    const BOOL zoomed = pDlg->IsZoomed();
    menu.AddCommand(SC_RESTORE,
        LL14(L"元のサイズに戻す", L"Restore", L"Restaurer", L"Ripristina", L"Restaurar", L"이전 크기로", L"还原", L"استعادة", L"Восстановить", L"Wiederherstellen", L"Restaurar", L"Vorige grootte", L"Przywroc", L"Onceki boyut"),
        LL14(L"最大化前の位置・サイズに戻します", L"Restore the window to its previous size and position",
            L"Restaurer la taille et la position precedentes", L"Ripristina dimensioni e posizione precedenti",
            L"Restaurar tamano y posicion anteriores", L"최대화 전 위치·크기로 되돌림",
            L"还原为最大化前的位置和大小", L"استعادة الحجم والموضع السابقين",
            L"Вернуть прежний размер и положение", L"Vorherige Groesse und Position wiederherstellen",
            L"Restaurar tamanho e posicao anteriores", L"Vorige grootte en positie herstellen",
            L"Przywroc poprzedni rozmiar i pozycje", L"Onceki boyut ve konuma geri yukle"),
        zoomed);
    menu.AddCommand(SC_MOVE,
        LL14(L"移動", L"Move", L"Deplacer", L"Sposta", L"Mover", L"이동", L"移动", L"تحريك", L"Переместить", L"Verschieben", L"Mover", L"Verplaatsen", L"Przesun", L"Tasi"),
        LL14(L"タイトルバーをドラッグしてウィンドウを移動", L"Drag the title bar to move the window",
            L"Glisser la barre de titre pour deplacer", L"Trascina la barra del titolo per spostare",
            L"Arrastrar la barra de titulo para mover", L"제목 표시줄을 끌어 창 이동",
            L"拖动标题栏移动窗口", L"اسحب شريط العنوان لتحريك النافذة",
            L"Перетащить за заголовок для перемещения", L"Titelleiste ziehen zum Verschieben",
            L"Arrastar a barra de titulo para mover", L"Titelbalk slepen om te verplaatsen",
            L"Przeciagnij pasek tytulu aby przesunac", L"Tasimak icin baslik cubugunu surukle"));
    if (pDlg->GetStyle() & WS_THICKFRAME)
        menu.AddCommand(SC_SIZE,
            LL14(L"サイズ変更", L"Size", L"Taille", L"Dimensiona", L"Tamano", L"크기 조정", L"大小", L"الحجم", L"Размер", L"Groesse", L"Tamanho", L"Grootte", L"Rozmiar", L"Boyut"),
            LL14(L"枠をドラッグしてウィンドウサイズを変更", L"Drag the window borders to resize",
                L"Glisser les bords pour redimensionner", L"Trascina i bordi per ridimensionare",
                L"Arrastrar los bordes para cambiar tamano", L"테두리를 끌어 창 크기 변경",
                L"拖动边框调整窗口大小", L"اسحب حواف النافذة لتغيير الحجم",
                L"Потянуть края для изменения размера", L"Raender ziehen zum Groesse aendern",
                L"Arrastar as bordas para redimensionar", L"Randen slepen om te vergroten/verkleinen",
                L"Przeciagnij krawedzie aby zmienic rozmiar", L"Boyut degistirmek icin kenarlari surukle"),
            !zoomed);
    CCC_CaptionEntry* e = CCC_FindCaption(hWnd);
    if (e && e->hasMin)
        menu.AddCommand(SC_MINIMIZE,
            LL14(L"最小化", L"Minimize", L"Reduire", L"Riduci a icona", L"Minimizar", L"최소화", L"最小化", L"تصغير", L"Свернуть", L"Minimieren", L"Minimizar", L"Minimaliseren", L"Minimalizuj", L"Kucult"),
            LL14(L"タスクバーへ最小化します", L"Minimize the window to the taskbar",
                L"Reduire la fenetre dans la barre des taches", L"Riduci a icona nella barra delle applicazioni",
                L"Minimizar a la barra de tareas", L"작업 표시줄로 최소화",
                L"最小化到任务栏", L"تصغير النافذة إلى شريط المهام",
                L"Свернуть окно на панель задач", L"Fenster in die Taskleiste minimieren",
                L"Minimizar para a barra de tarefas", L"Minimaliseren naar de taakbalk",
                L"Minimalizuj do paska zadan", L"Gorev cubuguna kucult"));
    if (e && e->hasMax)
        menu.AddCommand(SC_MAXIMIZE,
            LL14(L"最大化", L"Maximize", L"Agrandir", L"Ingrandisci", L"Maximizar", L"최대화", L"最大化", L"تكبير", L"Развернуть", L"Maximieren", L"Maximizar", L"Maximaliseren", L"Maksymalizuj", L"Buyut"),
            LL14(L"画面いっぱいに最大化します", L"Maximize the window to fill the screen",
                L"Agrandir la fenetre pour remplir l'ecran", L"Ingrandisci per riempire lo schermo",
                L"Maximizar para llenar la pantalla", L"화면 가득 최대화",
                L"最大化以填满屏幕", L"تكبير النافذة لملء الشاشة",
                L"Развернуть на весь экран", L"Fenster auf Bildschirmgroesse maximieren",
                L"Maximizar para preencher a tela", L"Maximaliseren om het scherm te vullen",
                L"Maksymalizuj aby wypelnic ekran", L"Ekrani dolduracak sekilde buyut"),
            !zoomed);
    menu.AddSeparator();
    if (e && e->hasSettings)
        menu.AddCommand(IDC_CAP_SETTINGS,
            LL14(L"設定", L"Settings", L"Parametres", L"Impostazioni", L"Ajustes", L"설정", L"设置", L"الإعدادات", L"Настройки", L"Einstellungen", L"Configuracoes", L"Instellingen", L"Ustawienia", L"Ayarlar"),
            LL14(L"キャプション／ウィンドウ関連の設定を開く", L"Open caption / window-related settings",
                L"Ouvrir les reglages barre de titre / fenetre", L"Apri impostazioni titolo / finestra",
                L"Abrir ajustes de titulo / ventana", L"캡션/창 관련 설정 열기",
                L"打开标题栏/窗口相关设置", L"فتح إعدادات شريط العنوان / النافذة",
                L"Открыть настройки заголовка / окна", L"Titelleisten-/Fenstereinstellungen oeffnen",
                L"Abrir configuracoes de titulo / janela", L"Titelbalk-/vensterinstellingen openen",
                L"Otworz ustawienia paska tytulu / okna", L"Baslik cubugu / pencere ayarlarini ac"));
    if (e)
        menu.AddCheck(IDC_CAP_PIN,
            LL14(L"常に手前に表示", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano", L"Siempre visible", L"항상 위", L"总在最前", L"دائماً في المقدمة", L"Поверх всех окон", L"Immer im Vordergrund", L"Sempre no topo", L"Altijd bovenop", L"Zawsze na wierzchu", L"Her zaman ustte"),
            e->topmost,
            LL14(L"他ウィンドウの上に常に表示（ピン留め）", L"Keep this window above others (pin / always on top)",
                L"Garder cette fenetre au-dessus des autres", L"Mantieni questa finestra sopra le altre",
                L"Mantener esta ventana sobre las demas", L"다른 창 위에 항상 표시(핀)",
                L"始终显示在其他窗口之上（置顶）", L"إبقاء هذه النافذة فوق الأخريات",
                L"Держать окно поверх остальных", L"Fenster immer ueber anderen halten",
                L"Manter esta janela acima das outras", L"Dit venster boven andere houden",
                L"Trzymaj to okno nad innymi", L"Bu pencereyi digerlerinin ustunde tut"));

    if (pMainLockSave) {
        CCC_MainLockEntry* le = CCC_FindMainLockEntry(hWnd);
        menu.AddCheck(IDC_MAINWIN_LOCK, CCC_MainLockLabel(), (le && le->locked) ? TRUE : FALSE,
            LL14(L"メイン窓の位置・サイズを固定", L"Lock main window position and size", L"Verrouiller position/taille", L"Blocca posizione/dimensione",
                L"Bloquear posicion/tamano", L"메인 창 위치·크기 고정", L"锁定主窗口位置和大小", L"قفل موضع/حجم النافذة",
                L"Зафиксировать положение/размер", L"Position/Groesse sperren", L"Travar posicao/tamanho", L"Positie/grootte vergrendelen",
                L"Zablokuj pozycje/rozmiar", L"Ana pencere konum/boyut kilitle"));
    }
    {
        if (e && e->sysExtraCount > 0) {
            menu.AddSeparator();
            for (int i = 0; i < e->sysExtraCount; ++i) {
                if (!e->sysExtraText[i][0])
                    continue;
                menu.AddCommand(e->sysExtraId[i], e->sysExtraText[i],
                    LL14(L"このシステムメニュー／キャプチャ操作を実行", L"Run this system-menu / capture action",
                        L"Executer cette action systeme / capture", L"Esegui questa azione di sistema / cattura",
                        L"Ejecutar esta accion de sistema / captura", L"이 시스템 메뉴/캡처 동작 실행",
                        L"执行此系统菜单/捕获操作", L"تشغيل أمر قائمة النظام / الالتقاط هذا",
                        L"Выполнить эту команду системного меню / захвата", L"Diesen Systemmenü-/Capture-Befehl ausführen",
                        L"Executar esta acao de menu do sistema / captura", L"Deze systeemmenu-/capture-actie uitvoeren",
                        L"Wykonaj te polecenie menu systemu / przechwytu", L"Bu sistem menusu / yakalama eylemini calistir"));
            }
        } else if (HMENU hSys = ::GetSystemMenu(hWnd, FALSE)) {
            const int nSys = ::GetMenuItemCount(hSys);
            BOOL anyCustom = FALSE;
            for (int i = 0; i < nSys; ++i) {
                const UINT id = ::GetMenuItemID(hSys, i);
                if (id == 0 || id == (UINT)-1) continue;
                if (id >= 0xF000) continue;
                anyCustom = TRUE;
                break;
            }
            if (anyCustom) {
                menu.AddSeparator();
                for (int i = 0; i < nSys; ++i) {
                    const UINT id = ::GetMenuItemID(hSys, i);
                    if (id == 0 || id == (UINT)-1) continue;
                    if (id >= 0xF000) continue;
                    wchar_t text[256];
                    text[0] = 0;
                    ::GetMenuStringW(hSys, i, text, 256, MF_BYPOSITION);
                    if (text[0])
                        menu.AddCommand(id, text,
                            LL14(L"このシステムメニュー／キャプチャ操作を実行", L"Run this system-menu / capture action",
                                L"Executer cette action systeme / capture", L"Esegui questa azione di sistema / cattura",
                                L"Ejecutar esta accion de sistema / captura", L"이 시스템 메뉴/캡처 동작 실행",
                                L"执行此系统菜单/捕获操作", L"تشغيل أمر قائمة النظام / الالتقاط هذا",
                                L"Выполнить эту команду системного меню / захвата", L"Diesen Systemmenü-/Capture-Befehl ausführen",
                                L"Executar esta acao de menu do sistema / captura", L"Deze systeemmenu-/capture-actie uitvoeren",
                                L"Wykonaj te polecenie menu systemu / przechwytu", L"Bu sistem menusu / yakalama eylemini calistir"));
                }
            }
        }
    }
    menu.AddSeparator();
    menu.AddCommand(SC_CLOSE,
        LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"),
        LL14(L"このウィンドウを閉じます", L"Close this window",
            L"Fermer cette fenetre", L"Chiudi questa finestra",
            L"Cerrar esta ventana", L"이 창을 닫습니다",
            L"关闭此窗口", L"إغلاق هذه النافذة",
            L"Закрыть это окно", L"Dieses Fenster schliessen",
            L"Fechar esta janela", L"Dit venster sluiten",
            L"Zamknij to okno", L"Bu pencereyi kapat"));
    const UINT cmd = menu.Track(scr, pDlg);
    if (cmd == IDC_CAP_SETTINGS)
        pDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CAP_SETTINGS, BN_CLICKED), 0);
    else if (cmd == IDC_CAP_PIN)
        pDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CAP_PIN, BN_CLICKED), 0);
    else if (cmd == IDC_MAINWIN_LOCK)
        CCC_MainLockOverlayToggle(hWnd);
    else if (cmd == SC_MAXIMIZE || cmd == SC_RESTORE)
        CCC_CaptionToggleMaximize(pDlg);
    else if (cmd)
        pDlg->SendMessage(WM_SYSCOMMAND, cmd, 0);
}

// CCC_CaptionOpenSettings: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionOpenSettings(CWnd* pDlg)
{
    CWnd* pMain = AfxGetMainWnd();
    if (!pMain || !::IsWindow(pMain->GetSafeHwnd()))
        return;
    if (pDlg && pMain->GetSafeHwnd() == pDlg->GetSafeHwnd())
        pDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON21, BN_CLICKED), 0);
    else
        pMain->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON21, BN_CLICKED), 0);
}

// CCC_CaptionToggleMaximize: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionToggleMaximize(CWnd* pDlg)
{
    if (!pDlg || !::IsWindow(pDlg->GetSafeHwnd()))
        return;
    HWND h = pDlg->GetSafeHwnd();
    CCC_CaptionEntry* e = CCC_FindCaption(h);
    const BOOL zoomed = pDlg->IsZoomed() || (e && e->manualZoomed);

    if (zoomed) {
        if (e && e->haveRestore) {
            const LONG st = ::GetWindowLong(h, GWL_STYLE);
            if (st & WS_MAXIMIZE)
                ::SetWindowLong(h, GWL_STYLE, st & ~WS_MAXIMIZE);
            ::SetWindowPos(h, NULL,
                e->restoreRc.left, e->restoreRc.top,
                e->restoreRc.right - e->restoreRc.left,
                e->restoreRc.bottom - e->restoreRc.top,
                SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
            e->haveRestore = FALSE;
            e->manualZoomed = FALSE;
        } else {
            pDlg->ShowWindow(SW_RESTORE);
            if (e)
                e->manualZoomed = FALSE;
        }
    } else {
        CRect wr;
        pDlg->GetWindowRect(&wr);
        if (e) {
            e->restoreRc = wr;
            e->haveRestore = TRUE;
        }
        MONITORINFO mi = { sizeof(mi) };
        const HMONITOR mon = ::MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
        if (::GetMonitorInfo(mon, &mi)) {
            // WS_POPUP でも IsZoomed() が真になるよう WS_MAXIMIZE を立て、作業領域へ広げる
            const LONG st = ::GetWindowLong(h, GWL_STYLE);
            ::SetWindowLong(h, GWL_STYLE, st | WS_MAXIMIZE);
            ::SetWindowPos(h, NULL,
                mi.rcWork.left, mi.rcWork.top,
                mi.rcWork.right - mi.rcWork.left,
                mi.rcWork.bottom - mi.rcWork.top,
                SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
            if (e)
                e->manualZoomed = TRUE;
        } else {
            pDlg->ShowWindow(SW_SHOWMAXIMIZED);
            if (e)
                e->manualZoomed = pDlg->IsZoomed() ? TRUE : FALSE;
        }
    }
    if (e && e->pMax && ::IsWindow(e->pMax->GetSafeHwnd())) {
        const BOOL z = pDlg->IsZoomed() || e->manualZoomed;
        CCC_CaptionApplySharedIcon(e->pMax, z ? IDI_CTL_RESTORE : IDI_CTL_MAX);
    }
}

// CCC_CaptionTogglePin: カスタム UI / アクリル補助。
// ガラス上の子は不透明、キャプション chrome は帯専用ボタン。
// 詳細は呼び出し元のコメントを優先。
static void CCC_CaptionTogglePin(CWnd* pDlg)
{
    if (!pDlg) return;
    CCC_CaptionEntry* e = CCC_FindCaption(pDlg->GetSafeHwnd());
    if (!e) return;
    e->topmost = !e->topmost;
    pDlg->SetWindowPos(e->topmost ? &CWnd::wndTopMost : &CWnd::wndNoTopMost, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (e->pPin && ::IsWindow(e->pPin->GetSafeHwnd()))
        CCC_CaptionApplySharedIcon(e->pPin, e->topmost ? IDI_CTL_PIN : IDI_CTL_PINOFF);
}

// ============================================================================
// アクリルぼかし適用済みカスタムダイアログ (CDialogEx版)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomBlurDialogExBase, CCustomDialogEx)

BEGIN_MESSAGE_MAP(CCustomBlurDialogExBase, CCustomDialogEx)
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_SHOWWINDOW()
    ON_WM_WINDOWPOSCHANGED()
    ON_WM_DWMCOMPOSITIONCHANGED()
    ON_WM_DESTROY()
    ON_WM_MOVING()
    ON_COMMAND(IDC_MAINWIN_LOCK, OnMainLockClicked)
    ON_COMMAND(IDC_CAP_CLOSE, OnCapClose)
    ON_COMMAND(IDC_CAP_MIN, OnCapMin)
    ON_COMMAND(IDC_CAP_MAX, OnCapMax)
    ON_COMMAND(IDC_CAP_SETTINGS, OnCapSettings)
    ON_COMMAND(IDC_CAP_PIN, OnCapPin)
    ON_COMMAND(IDC_CAP_OFFLINE_HELP, OnCapOfflineHelp)
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_RBUTTONUP()
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnTtnNeedText)
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnTtnNeedText)
    ON_MESSAGE(CCC_MSG_INSTALL_CAPTION, OnInstallCustomCaption)
    ON_WM_NCCALCSIZE()
    ON_WM_NCACTIVATE()
    ON_MESSAGE(0x00AE, OnNcThemeCaptionPaint)
    ON_MESSAGE(0x00AF, OnNcThemeCaptionPaint)
#if CCUSTOM_AERO_SUPPORT
    ON_MESSAGE(CCC_MSG_REAPPLY_OPAQUE_FIXERS, OnReapplyOpaqueFixers)
#endif
END_MESSAGE_MAP()

// CCustomBlurDialogExBase のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。実初期化は OnInitDialog / PreCreateWindow。
CCustomBlurDialogExBase::CCustomBlurDialogExBase() : m_bBlurApplied(FALSE) {}
// CCustomBlurDialogExBase のコンストラクタ。描画フラグとブラシを初期化。
// HWND はまだ無い。実初期化は OnInitDialog / PreCreateWindow。
CCustomBlurDialogExBase::CCustomBlurDialogExBase(UINT n, CWnd* p) : CCustomDialogEx(n, p), m_bBlurApplied(FALSE) {}
// CCustomBlurDialogExBase の破棄。fixer / キャプションは OnDestroy。ここは空。
CCustomBlurDialogExBase::~CCustomBlurDialogExBase() {}

// AcrylicCaption 常時のため null brush 専用クラス。#32770 のクラスブラシは黒帯になる。
// save.aero に関係なく CCustomBlurDlgEx を登録する。
BOOL CCustomBlurDialogExBase::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CCustomDialogEx::PreCreateWindow(cs))
        return FALSE;
#if CCUSTOM_AERO_SUPPORT
    // AcrylicCaption 常時のため save.aero 不問で null brush 専用クラス
    RegisterBlurDialogWndClass(cs.lpszClass, _T("CCustomBlurDlgEx"));
    cs.lpszClass = _T("CCustomBlurDlgEx");
#endif
    return TRUE;
}

// 子を CCustom* へサブクラス。Blur 系はキャプション install を Post。
// 子サブクラス。Blur はキャプションを Show 前に。
BOOL CCustomBlurDialogExBase::OnInitDialog()
{
    BOOL b = CCustomDialogEx::OnInitDialog();
    SetIcon(nullptr, TRUE);
    SetIcon(nullptr, FALSE);
    {
        UINT idd = 0;
        if (m_lpszTemplateName && IS_INTRESOURCE(m_lpszTemplateName))
            idd = (UINT)(ULONG_PTR)m_lpszTemplateName;
        CCC_ApplyWindowIconFromTemplate(this, idd);
    }
#if CCUSTOM_AERO_SUPPORT
    ::SetClassLongPtr(m_hWnd, GCLP_HBRBACKGROUND, 0);
#endif
    ApplyDwmBlur();
    // キャプション化は初回 OnShowWindow（表示前）。PostMessage だとフラッシュ＋縦幅ジャンプ。
    return b;
}

// 遅延キャプション導入。OnInit 時点では NC がまだ不安定。
// Show 前に一度だけ。
LRESULT CCustomBlurDialogExBase::OnInstallCustomCaption(WPARAM, LPARAM)
{
    CCC_CaptionInstallCore(this, &m_capTip);
#if CCUSTOM_AERO_SUPPORT
    CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
#endif
    return 0;
}

// カスタム帯分の NC 吸収。FRAMECHANGED と対。
// 帯高さ分をクライアントへ取り込む。
void CCustomBlurDialogExBase::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
{
    if (CCC_GetCustomCaptionHeight(m_hWnd) > 0 && lpncsp) {
        CCC_CaptionHandleNcCalcSize(m_hWnd, bCalcValidRects ? TRUE : FALSE, reinterpret_cast<LPARAM>(lpncsp), 0);
        return;
    }
    CCustomDialogEx::OnNcCalcSize(bCalcValidRects, lpncsp);
}

// システムキャプションの再描画を止める。lParam=-1 が定石。帯の活性色はクライアント側で。
BOOL CCustomBlurDialogExBase::OnNcActivate(BOOL bActive)
{
    if (CCC_GetCustomCaptionHeight(m_hWnd) > 0) {
        const BOOL r = (BOOL)::DefWindowProc(m_hWnd, WM_NCACTIVATE, (WPARAM)bActive, (LPARAM)-1);
        const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
        CRect cr;
        GetClientRect(&cr);
        if (cr.bottom > capH)
            cr.bottom = capH;
        InvalidateRect(&cr, FALSE);
        return r;
    }
    return CCustomDialogEx::OnNcActivate(bActive);
}

// WM_NCUAHDRAWCAPTION / WM_NCUAHDRAWFRAME。テーマのシステム帯描画を捨てる。
LRESULT CCustomBlurDialogExBase::OnNcThemeCaptionPaint(WPARAM, LPARAM)
{
    if (CCC_GetCustomCaptionHeight(m_hWnd) > 0)
        return 0;
    return Default();
}

// 設定変更からの強制ぼかし再適用。
// bForce。
void CCustomBlurDialogExBase::RefreshAeroMode()
{
    ApplyDwmBlurCore(TRUE);
}

// 二重適用しない通常経路（bForce=FALSE）。
// 既適用なら何もしない。
void CCustomBlurDialogExBase::ApplyDwmBlur()
{
    ApplyDwmBlurCore(FALSE);
}

// DialogEx 版。再入ガードと !bForce の二重適用禁止は Base と同じ。
// FRAMECHANGED を重ねると NC 吸収がループし帯が凍って見える。
// ぼかし OFF もホスト backdrop は残し、fixer を張り直す。
void CCustomBlurDialogExBase::ApplyDwmBlurCore(BOOL bForce)
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
#if CCUSTOM_AERO_SUPPORT
    // 再入・二重 FRAMECHANGED 禁止。既適用かつ !bForce はすぐ return。
    if (m_bInApplyBlur) return;
    m_bInApplyBlur = TRUE;

    const BOOL bWant = CCC_IsAeroEnabled();
    if (bWant)
    {
        if (m_bBlurApplied && !bForce) {
            m_bInApplyBlur = FALSE;
            return;
        }
        m_bAeroEnabled = TRUE;
        CCC_FinishBlurDlg(this, TRUE, m_bBlurApplied, m_opaqueFixers);
        if (CCC_GetCustomCaptionHeight(m_hWnd) > 0)
            CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
        if (m_pMainLockSave)
            CCC_MainLockBringToFront(m_hWnd);
        m_bInApplyBlur = FALSE;
        return;
    }

    m_bAeroEnabled = FALSE;
    if (!m_bBlurApplied && !bForce) {
        if (CCC_GetCustomCaptionHeight(m_hWnd) > 0)
            CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
        m_bInApplyBlur = FALSE;
        return;
    }
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
    CCC_DisableBodyAeroOnly(m_hWnd);
    CCC_PrepareDialogSurface(m_hWnd, FALSE);
    PROPAGATE_AERO_TO_CHILDREN(m_hWnd, FALSE);
    m_bBlurApplied = FALSE;
    if (CCC_GetCustomCaptionHeight(m_hWnd) > 0)
        CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
    m_bInApplyBlur = FALSE;
#else
    UNREFERENCED_PARAMETER(bForce);
    m_bBlurApplied = FALSE;
#endif
}

// 表示前にキャプション化（システム帯のフラッシュと NC 吸収ジャンプを隠す）。
// 本文 aero は ApplyDwmBlur 1 回。本文 off で RefreshDwmBlur するとガラスに戻る。
void CCustomBlurDialogExBase::OnShowWindow(BOOL bShow, UINT nStatus)
{
    if (bShow) {
        CCC_CaptionInstallCore(this, &m_capTip);
#if CCUSTOM_AERO_SUPPORT
        CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
#endif
    }
    CCustomDialogEx::OnShowWindow(bShow, nStatus);
    if (m_pMainLockSave)
        CCC_MainLockShowBtn(m_hWnd, bShow);
    if (!bShow)
        return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled())
    {
        ApplyDwmBlur();
        CCC_RefreshDwmBlur(m_hWnd);
        CCC_CaptionEnsureBackdrop(m_hWnd);
    }
    // 本文 off は CaptionInstall 直後の ApplyGlassAndFixers 1 回。
    // 後段 RefreshDwmBlur は本文不透明を潰すので呼ばない。
#endif
    UNREFERENCED_PARAMETER(nStatus);
}

// WM_PAINT。透過はクロマ、ホストガラスは不透明パス、それ以外は素描画。
// CPaintDC。経路分岐は aero / ホストガラス / 通常。
void CCustomBlurDialogExBase::OnPaint()
{
    CPaintDC dc(this);
#if CCUSTOM_AERO_SUPPORT
    if (m_bAeroEnabled && CCC_IsWin11())
    {
        CCC_PaintAeroGaps(dc, this, nullptr);
        CCC_CaptionPaint(dc, m_hWnd);
        if (m_pMainLockSave)
            CCC_MainLockPaintClient(dc, m_hWnd);
        return;
    }
    if (m_bAeroEnabled)
    {
        CRect rect;
        GetClientRect(&rect);
        dc.FillSolidRect(&rect, RGB(250, 250, 250));
    }
#endif
    CCC_CaptionPaint(dc, m_hWnd);
#if CCUSTOM_AERO_SUPPORT
    // EnsureBackdrop 後に本文 α=255（キャプション帯 Clear の後でも本文だけ塞ぐ）
    if (!m_bAeroEnabled && CCC_AcrylicCaption(m_hWnd))
        CCC_PaintOpaqueBodyBelowCaption(dc, this, m_brDialog);
#endif
    if (m_pMainLockSave)
        CCC_MainLockPaintClient(dc, m_hWnd);
}

// キャプション／追従エントリ解除、fixer 破棄。
// 基底 OnDestroy の前に登録を外す。
void CCustomBlurDialogExBase::OnDestroy()
{
    CCC_CaptionUnregister(m_hWnd);
    CCC_MainLockUnregister(m_hWnd);
#if CCUSTOM_AERO_SUPPORT
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
#endif
    CCustomDialogEx::OnDestroy();
}

// キャプションボタン再配置。最大化アイコンの切替。
// chrome 再配置。
void CCustomBlurDialogExBase::OnSize(UINT nType, int cx, int cy)
{
    CCustomDialogEx::OnSize(nType, cx, cy);
    CCC_CaptionLayout(m_hWnd);
    if (m_pMainLockSave)
        CCC_MainLockBringToFront(m_hWnd);
    if (CCC_CaptionEntry* e = CCC_FindCaption(m_hWnd)) {
        if (e->installed && e->pMax && ::IsWindow(e->pMax->GetSafeHwnd())) {
            if (nType == SIZE_RESTORED)
                e->manualZoomed = FALSE;
            else if (nType == SIZE_MAXIMIZED)
                e->manualZoomed = TRUE;
            CCC_CaptionApplySharedIcon(e->pMax,
                (IsZoomed() || e->manualZoomed) ? IDI_CTL_RESTORE : IDI_CTL_MAX);
        }
    }
}

// SHOW で未適用ならぼかしを載せる（初期 Show 経路の穴埋め）。
// 初回 SHOW のぼかし穴埋め。
void CCustomBlurDialogExBase::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
    CCustomDialogEx::OnWindowPosChanged(lpwndpos);
#if CCUSTOM_AERO_SUPPORT
    if (lpwndpos && (lpwndpos->flags & SWP_SHOWWINDOW) && !m_bBlurApplied && CCC_IsAeroEnabled())
        ApplyDwmBlur();
#endif
}

// DWM 構成変化。ぼかしを強制再適用。
// DWM 再構成。
void CCustomBlurDialogExBase::OnCompositionChanged()
{
#if CCUSTOM_AERO_SUPPORT
    ApplyDwmBlurCore(TRUE);
#endif
}

// 子が増えたあと fixer を張り直す。追従ボタンを前面へ。
// 動的に増えた子用。
LRESULT CCustomBlurDialogExBase::OnReapplyOpaqueFixers(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsWin11() && (m_bAeroEnabled || CCC_AcrylicCaption(m_hWnd)))
        CCC_ReapplyOpaqueFix(this, m_opaqueFixers);
#endif
    if (m_pMainLockSave)
        CCC_MainLockBringToFront(m_hWnd);
    CCC_CaptionLayout(m_hWnd);
    return 0;
}

// DialogEx 版。保存フラグとオーバーレイ描画の有無を CCC_MainLockSetup へ渡す。
// キャプション帯の「メインに追従」と同じ状態機械。
void CCustomBlurDialogExBase::EnableMainWindowLock(int* pSavedLockFlag, BOOL bOverlayPaint)
{
    m_pMainLockSave = pSavedLockFlag;
    // 保存フラグを結び、帯内オーバーレイか実ボタンかを決める。
    CCC_MainLockSetup(this, pSavedLockFlag, bOverlayPaint);
}

// 「メインに追従」ボタン／オーバーレイのトグル入口。
// 保存フラグも反転。
void CCustomBlurDialogExBase::OnMainLockClicked()
{
    CCC_MainLockOnClicked(m_hWnd);
}

// 帯の空きは HTCAPTION ドラッグ。最大化中は先に復元。
// 帯ドラッグは HTCAPTION。
void CCustomBlurDialogExBase::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (m_pMainLockSave && CCC_MainLockOverlayHitTest(m_hWnd, point)) {
        CCC_MainLockOverlayToggle(m_hWnd);
        return;
    }
    const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
    if (capH > 0 && point.y >= 0 && point.y < capH) {
        CWnd* pHit = ChildWindowFromPoint(point, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!pHit || pHit == this) {
            CCC_CaptionEntry* e = CCC_FindCaption(m_hWnd);
            if (IsZoomed() || (e && e->manualZoomed))
                CCC_CaptionToggleMaximize(this);
            SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
            return;
        }
    }
    CCustomDialogEx::OnLButtonDown(nFlags, point);
}

// 帯ダブルクリックで最大化トグル（hasMax 時）。
// 帯で最大化トグル。
void CCustomBlurDialogExBase::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
    if (capH > 0 && point.y >= 0 && point.y < capH) {
        CWnd* pHit = ChildWindowFromPoint(point, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!pHit || pHit == this) {
            CCC_CaptionEntry* e = CCC_FindCaption(m_hWnd);
            if (e && e->hasMax)
                OnCapMax();
            return;
        }
    }
    CCustomDialogEx::OnLButtonDblClk(nFlags, point);
}

// コンテキストメニュー。キャプション帯ならシステムメニューへ。
// 空き帯以外は既定の右クリック。
void CCustomBlurDialogExBase::OnRButtonUp(UINT nFlags, CPoint point)
{
    const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
    if (capH > 0 && point.y >= 0 && point.y < capH) {
        CCC_CaptionTrackContextMenu(this, point, m_pMainLockSave);
        return;
    }
    CWnd::OnRButtonUp(nFlags, point);
}

// カスタム×。システムメニューと同じ SC_CLOSE。
// BN_CLICKED はメッセージマップ IDC_CAP_CLOSE。
void CCustomBlurDialogExBase::OnCapClose()
{
    SendMessage(WM_SYSCOMMAND, SC_CLOSE, 0);
}

// カスタム最小化。SC_MINIMIZE（タスクバーへ）。
// カスタム帯の Min ボタンから。
void CCustomBlurDialogExBase::OnCapMin()
{
    SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
}

// カスタム最大化／復元。手動ズームフラグも見る。
// ポップアップでも THICKFRAME ならボタンを出す。
void CCustomBlurDialogExBase::OnCapMax()
{
    CCC_CaptionToggleMaximize(this);
}

// 歯車。設定ダイアログを開く。
// レンダラ系窓ではボタン自体を出さない。
void CCustomBlurDialogExBase::OnCapSettings()
{
    CCC_CaptionOpenSettings(this);
}

// ピン。TOPMOST トグル。
// アイコンは PIN / PINOFF を入れ替える。
void CCustomBlurDialogExBase::OnCapPin()
{
    CCC_CaptionTogglePin(this);
}

// オフラインヘルプ（CHM）。F1 と同じ。
// CHM。F1 と同じ。
void CCustomBlurDialogExBase::OnCapOfflineHelp()
{
    OfflineHelpOpen(m_hWnd);
}

// キャプションボタンのツールチップは各 AddTool 側。ここでは握るだけ。
// AddTool 側の文字列を使う。
BOOL CCustomBlurDialogExBase::OnTtnNeedText(UINT, NMHDR*, LRESULT* pResult)
{
    *pResult = 0;
    return FALSE;
}

// ツールチップ Relay。ダイアログ側は F1 ヘルプ。隠し演出入力は短く見るだけ。
// キャプションボタンのチップは m_capTip.RelayEvent。
BOOL CCustomBlurDialogExBase::PreTranslateMessage(MSG* pMsg)
{
    if (CCC_InwomanHotkey(pMsg, this))
        return TRUE;
    if (pMsg && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_F1)
    {
        OfflineHelpOpen(m_hWnd);
        return TRUE;
    }
    if (m_capTip.GetSafeHwnd())
        m_capTip.RelayEvent(pMsg);
    return CCustomDialogEx::PreTranslateMessage(pMsg);
}

// 移動中。追従 ON ならオフセットを更新。カスケード密着も見る。
// 追従 ON ならオフセット更新。
void CCustomBlurDialogExBase::OnMoving(UINT fwSide, LPRECT pRect)
{
    CCustomDialogEx::OnMoving(fwSide, pRect);
    CCC_MainLockOnChildMoving(this, pRect);
}
