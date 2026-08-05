#include "stdafx.h"
#include "CCustomControl.h"
#include "resource.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <psapi.h>
#include <TlHelp32.h>

#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "psapi.lib")

static UINT CCC_GetControlDpi(HWND hWnd)
{
    if (!hWnd) return 96;
    HDC hdc = ::GetDC(hWnd);
    if (!hdc) return 96;
    const UINT dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
    ::ReleaseDC(hWnd, hdc);
    return (dpi > 0) ? dpi : 96;
}

static int CCC_ScaleDpi(int value, UINT dpi)
{
    return MulDiv(value, (int)dpi, 96);
}

static void CCC_ComputeShadowPad(int nSD, int nDist, int nBlur, BOOL bSE, UINT dpi,
    int& padX, int& padY)
{
    padX = padY = 0;
    if (!bSE || nDist <= 0 || nBlur <= 0) return;
    const int dist = CCC_ScaleDpi(nDist, dpi);
    const int blur = CCC_ScaleDpi(nBlur, dpi);
    const double rad = nSD * 3.14159265358979323846 / 180.0;
    padX = (int)floor(dist * cos(rad) + (blur + 1) / 2 + 0.5);
    padY = (int)floor(dist * sin(rad) + (blur + 1) / 2 + 0.5);
    if (padX < 0) padX = 0;
    if (padY < 0) padY = 0;
}

#ifdef SubclassWindow
#undef SubclassWindow
#endif

#if CCUSTOM_AERO_SUPPORT
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

static BOOL CCC_IsCaptionChromeCtrl(HWND hWnd);
static BOOL CCC_CaptionOnlyHostGlass(HWND hWnd);

// キャプション常時アクリル(本文 aero=0)でも子は α=255 必須
static BOOL CCC_HostNeedsChildOpaque(HWND hWnd)
{
#if CCUSTOM_AERO_SUPPORT
    return CCC_IsWin11() && (CCC_IsAeroEnabled() || CCC_CaptionOnlyHostGlass(hWnd));
#else
    UNREFERENCED_PARAMETER(hWnd);
    return FALSE;
#endif
}

// キャプション帯コントロールは AcrylicCaption 時は常に透過（本文 aero と独立）
static BOOL CCC_UseTransPaint(HWND hWnd, BOOL bAeroMode)
{
    if (hWnd) {
        HWND hParent = ::GetParent(hWnd);
        if (hParent && CCC_AcrylicCaption(hParent) && CCC_IsCaptionChromeCtrl(hWnd))
            return TRUE;
    }
    if (CCC_IsBlurDialogChild(hWnd) && CCC_IsAeroEnabled()) return TRUE;
    return bAeroMode && !CCC_IsBlurDialogChild(hWnd);
}

void CCC_InvalidateParent(HWND hWnd, BOOL bAeroMode)
{
    if (!CCC_UseTransPaint(hWnd, bAeroMode)) return;
    HWND hParent = ::GetParent(hWnd);
    if (!hParent || !::IsWindow(hParent)) return;
    RECT rc = {};
    ::GetWindowRect(hWnd, &rc);
    ::MapWindowPoints(NULL, hParent, (LPPOINT)&rc, 2);
    ::InflateRect(&rc, 6, 6);
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

void CCC_RefreshDwmBlur(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd) || !CCC_IsWin11()) return;
    if (!CCC_IsAeroEnabled() && !CCC_AcrylicCaption(hWnd)) return;
    BOOL compositionEnabled = FALSE;
    if (!::DwmIsCompositionEnabled(&compositionEnabled) || !compositionEnabled) return;
    const int backdropType = 3;
    ::DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
#ifndef DWMWA_REDIRECTIONBITMAP_ALPHA
#define DWMWA_REDIRECTIONBITMAP_ALPHA 39
#endif
    BOOL useAlpha = TRUE;
    ::DwmSetWindowAttribute(hWnd, DWMWA_REDIRECTIONBITMAP_ALPHA, &useAlpha, sizeof(useAlpha));
    const MARGINS margins = CCC_CaptionHostMargins(hWnd);
    ::DwmExtendFrameIntoClientArea(hWnd, &margins);
}

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
        pDC->FillSolidRect(&r, RGB(248, 248, 248));
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
        return TRUE;
    }
#endif
    if (capH > 0 && r.Height() > capH) {
        r.top = capH;
        pDC->FillRect(&r, &brDlg);
        return TRUE;
    }
    pDC->FillRect(&r, &brDlg);
    return TRUE;
}

static void DlgOnPaintAero(CWnd* pWnd, BOOL bAeroEnabled)
{
#if CCUSTOM_AERO_SUPPORT
    if (bAeroEnabled && CCC_IsWin11())
    {
        CPaintDC dc(pWnd);
        CRect rect;
        pWnd->GetClientRect(&rect);
        dc.FillSolidRect(&rect, RGB(250, 250, 250));
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
        static CCC_ChromaBlitCache s_fillCaches[2];
        static unsigned s_fillNext = 0;
        CCC_ChromaBlitCache* pCache = nullptr;
        for (auto& c : s_fillCaches) {
            if (c.pBits && c.dibW == w && c.dibH == h) {
                pCache = &c;
                break;
            }
        }
        if (!pCache) {
            pCache = &s_fillCaches[s_fillNext++ % 2];
            if (!pCache->Ensure(hdc, w, h))
                pCache = nullptr;
        }
        if (pCache && pCache->pBits && pCache->hdcDib) {
            RECT zr = { 0, 0, w, h };
            HBRUSH br = ::CreateSolidBrush(clr);
            ::FillRect(pCache->hdcDib, &zr, br);
            ::DeleteObject(br);
            pCache->MakeRectOpaque(0, 0, w, h);
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

static void CCC_InitBPClear(HPAINTBUFFER hBP, int w, int h)
{
    RGBQUAD* pPixels = nullptr;
    int rowLength = 0;
    if (FAILED(::GetBufferedPaintBits(hBP, &pPixels, &rowLength)) || !pPixels || w <= 0 || h <= 0)
        return;
    for (int y = 0; y < h; ++y)
    {
        RGBQUAD* pRow = reinterpret_cast<RGBQUAD*>(
            reinterpret_cast<BYTE*>(pPixels) + y * rowLength * static_cast<int>(sizeof(RGBQUAD)));
        ::ZeroMemory(pRow, w * sizeof(RGBQUAD));
    }
}

static UINT32 CCC_RgbMask(COLORREF clr)
{
    return (UINT32)(GetRValue(clr) << 16) | (UINT32)(GetGValue(clr) << 8) | GetBValue(clr);
}

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

static void CCC_AlphaFromChroma(void* pBits, int w, int h, COLORREF clrKey)
{
    if (!pBits || w <= 0 || h <= 0) return;
    RECT rc = { 0, 0, w, h };
    CCC_AlphaFromChromaRect(pBits, w, h, rc, clrKey);
}

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

void CCC_ChromaBlitCache::MakeRectOpaque(int x, int y, int rw, int rh)
{
    if (!pBits || rw <= 0 || rh <= 0 || dibW <= 0) return;
    if (x < 0 || y < 0 || x + rw > dibW || y + rh > dibH) return;
    UINT32* base = (UINT32*)pBits;
    for (int row = 0; row < rh; ++row) {
        UINT32* p = base + (size_t)(y + row) * (size_t)dibW + x;
        for (int col = 0; col < rw; ++col)
            p[col] |= 0xFF000000u;
    }
}

BOOL CCC_ChromaBlitCache::BlitRect(HDC hdcDest, int x, int y, int w, int h)
{
    if (!hdcDest || w <= 0 || h <= 0 || !pBits || !hdcDib || dibW <= 0 || dibH <= 0) return FALSE;
    if (x < 0 || y < 0 || x + w > dibW || y + h > dibH) return FALSE;

    static LONG s_bpInited = 0;
    if (InterlockedCompareExchange(&s_bpInited, 1, 0) == 0)
        ::BufferedPaintInit();

    RECT rect = { x, y, x + w, y + h };
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP) return FALSE;

    CCC_InitBPClear(hBP, w, h);
    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    ::GdiAlphaBlend(hdcBuf, x, y, w, h, hdcDib, x, y, w, h, bf);
    ::EndBufferedPaint(hBP, TRUE);
    return TRUE;
}

BOOL CCC_ChromaBlitCache::BlitFull(HDC hdcDest, int x, int y, int w, int h)
{
    if (!hdcDest || w <= 0 || h <= 0 || !pBits || !hdcDib || dibW != w || dibH != h) return FALSE;

    static LONG s_bpInited = 0;
    if (InterlockedCompareExchange(&s_bpInited, 1, 0) == 0)
        ::BufferedPaintInit();

    RECT rect = { x, y, x + w, y + h };
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP) return FALSE;

    CCC_InitBPClear(hBP, w, h);
    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    ::GdiAlphaBlend(hdcBuf, 0, 0, w, h, hdcDib, 0, 0, w, h, bf);
    ::EndBufferedPaint(hBP, TRUE);
    return TRUE;
}

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

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP) return FALSE;

    CCC_InitBPClear(hBP, destW, destH);
    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
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
    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    // buffer DC はクライアント座標系なので rect の左上から描画する
    ::GdiAlphaBlend(hdcBuf, rect.left, rect.top, destW, destH, dcDib.GetSafeHdc(), 0, 0, destW, destH, bf);
    ::EndBufferedPaint(hBP, TRUE);

    ::SelectObject(dcDib.GetSafeHdc(), hOld);
    ::DeleteObject(hDib);
    dcSrc.Detach();
    return TRUE;
}

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

    static CCC_ChromaBlitCache s_opaqueCaches[4];
    static unsigned s_opaqueNext = 0;
    CCC_ChromaBlitCache* pCache = nullptr;
    for (auto& c : s_opaqueCaches) {
        if (c.pBits && c.dibW == destW && c.dibH == destH) {
            pCache = &c;
            break;
        }
    }
    if (!pCache) {
        pCache = &s_opaqueCaches[s_opaqueNext++ % 4];
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

void CCC_BlitStretchOpaque(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH)
{
    RECT rect = { x, y, x + destW, y + destH };
    CCC_BlitToRectOpaque(hdcDest, rect, hdcSrc, srcX, srcY, destW, destH, srcW, srcH, TRUE);
}

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

void CCC_BlitStretchChroma(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH, COLORREF clrKey)
{
    RECT rect = { x, y, x + destW, y + destH };
    CCC_BlitToRectChroma(hdcDest, rect, hdcSrc, srcX, srcY, destW, destH, srcW, srcH, clrKey, TRUE);
}

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

void CCC_BlitChroma(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey)
{
    RECT rect = { x, y, x + w, y + h };
    CCC_BlitToRectChroma(hdcDest, rect, hdcSrc, srcX, srcY, w, h, w, h, clrKey, FALSE);
}

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

void CCC_BlitChromaDwm(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey)
{
    CCC_BlitChromaNF(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, clrKey);
}

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
    const int peakA = bAeroTrans ? 88 : 112;

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
    const int kB = 4;

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
    }
    pDC->SelectObject(op);
    pDC->SelectObject(ob);
}

static void DrawSmartText(CDC* pDC, CRect rect, CString str, BOOL bDis, BOOL bPushed)
{
    if (str.IsEmpty()) return;

    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(bDis ? RGB(128, 128, 128) : COLOR_EDIT_TEXT);

    CRect rt = rect;
    rt.DeflateRect(1, 1);
    if (bPushed) rt.OffsetRect(1, 1);

    CFont* pCF = pDC->GetCurrentFont();
    LOGFONT lf;
    pCF->GetLogFont(&lf);
    long tH = max(8L, abs(lf.lfHeight) - 2);
    lf.lfHeight = -tH;

    CFont fs;
    fs.CreateFontIndirect(&lf);
    CFont* po = pDC->SelectObject(&fs);
    CSize sz = pDC->GetTextExtent(str);

    if (sz.cx <= rt.Width())
    {
        pDC->DrawText(str, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        pDC->SelectObject(po);
        fs.DeleteObject();
        return;
    }

    // ボタン幅不足時は折り返しではなくフォント縮小で1行に収める
    pDC->SelectObject(po);
    fs.DeleteObject();
    while (tH > 6)
    {
        tH--;
        lf.lfHeight = -tH;
        CFont ft;
        ft.CreateFontIndirect(&lf);
        pDC->SelectObject(&ft);
        sz = pDC->GetTextExtent(str);
        if (sz.cx <= rt.Width() && sz.cy <= rt.Height())
        {
            pDC->DrawText(str, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            pDC->SelectObject(po);
            ft.DeleteObject();
            return;
        }
        pDC->SelectObject(po);
        ft.DeleteObject();
    }

    lf.lfHeight = -6;
    CFont fm;
    fm.CreateFontIndirect(&lf);
    pDC->SelectObject(&fm);
    pDC->DrawText(str, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    pDC->SelectObject(po);
    fm.DeleteObject();
}

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
    scaleX *= 0.98f;
    if (scaleX < 0.62f) scaleX = 0.62f;
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

// 名前列/印列の [SAV]/[LRC] を抜き出し、色付きチップ描画用に分離する。
static void CCC_ExtractSavLrc(CString& text, BOOL& bSav, BOOL& bLrc)
{
    bSav = FALSE;
    bLrc = FALSE;
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

// SAV=琥珀 / LRC=青。文字は黒固定（白字はアクリル上で読みにくい）
static int CCC_DrawSavLrcChips(CDC* pDC, int x, int midY, BOOL bSav, BOOL bLrc, BOOL bOpaque)
{
    if (!bSav && !bLrc) return x;
    const COLORREF fg = RGB(20, 20, 24);
    if (bSav)
        x += CCC_DrawMarkChip(pDC, x, midY, _T("SAV"),
            RGB(255, 214, 160), fg, bOpaque);
    if (bLrc)
        x += CCC_DrawMarkChip(pDC, x, midY, _T("LRC"),
            RGB(186, 210, 255), fg, bOpaque);
    return x;
}

static int CCC_MeasureSavLrcChips(CDC* pDC, BOOL bSav, BOOL bLrc)
{
    if (!pDC || (!bSav && !bLrc)) return 0;
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
    pDC->SelectObject(pOf);
    return w;
}

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
        if (scale < 0.12f) { scale = 0.12f; break; }
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

static void DrawSmartText2(CDC* pDC, CRect rect, CString str, UINT fmt, BOOL bDis, BOOL bPushed)
{
    if (str.IsEmpty()) return;

    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(bDis ? RGB(128, 128, 128) : COLOR_EDIT_TEXT);

    CRect rl = rect;
    rl.DeflateRect(2, 0);
    if (bPushed) rl.OffsetRect(1, 1);

    CFont* pCF = pDC->GetCurrentFont();
    LOGFONT lf;
    pCF->GetLogFont(&lf);
    long tH = abs(lf.lfHeight);
    const long MH = 6;
    CFont ff;

    while (tH >= MH)
    {
        lf.lfHeight = -tH;
        CFont ft;
        ft.CreateFontIndirect(&lf);
        CFont* po = pDC->SelectObject(&ft);
        CRect rc = rl;
        pDC->DrawText(str, &rc, fmt | DT_CALCRECT);
        pDC->SelectObject(po);

        if (rc.Width() <= rl.Width() && rc.Height() <= rl.Height())
        {
            ff.CreateFontIndirect(&lf);
            ft.DeleteObject();
            break;
        }
        ft.DeleteObject();
        tH--;
    }

    if (!ff.GetSafeHandle())
    {
        lf.lfHeight = -MH;
        ff.CreateFontIndirect(&lf);
    }

    CFont* po = pDC->SelectObject(&ff);
    pDC->DrawText(str, &rl, fmt);
    pDC->SelectObject(po);
    ff.DeleteObject();
}

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

// ぷるんとした濡れツヤ(ガラス/リップグロス風)を上半分にのせる。
// 不透明な面の上にのみ使用すること（クロマキー透過領域には使わない）。
static void DrawGlossHighlight(CDC* pDC, const CRect& rc, int radius)
{
    if (!pDC || rc.Width() <= 4 || rc.Height() <= 6) return;
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
}

// ぷるんとした濡れツヤ付きのチェック(レ点)。丸端の太線でやわらかく。
static void DrawCheckMark(CDC* pDC, const CRect& rc, COLORREF c, int thick)
{
    if (!pDC || rc.Width() < 5 || rc.Height() < 5) return;
    if (thick < 2) thick = 2;
    const int x1 = rc.left + rc.Width() * 12 / 100, y1 = rc.top + rc.Height() * 54 / 100;
    const int x2 = rc.left + rc.Width() * 40 / 100, y2 = rc.top + rc.Height() * 82 / 100;
    const int x3 = rc.left + rc.Width() * 92 / 100, y3 = rc.top + rc.Height() * 14 / 100;

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
static void DrawLooseRibbon(CDC* pDC, const CRect& rc, COLORREF c)
{
    if (!pDC || rc.Width() < 6 || rc.Height() < 5) return;
    const int cx = rc.CenterPoint().x;
    const int cy = rc.top + rc.Height() / 3;
    const int w = max(3, rc.Width() / 2);
    const int h = max(2, rc.Height() / 3);

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
}

// ============================================================================
// 隠し機能: 淫女モード (F12を5回でトグル / UI演出のみ)
// ============================================================================
static UINT_PTR g_inwomanTimer = 0;
static int      g_f12Count = 0;
static DWORD    g_f12First = 0;   // 連打シーケンスの最初の F12 を押した時刻

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

static BOOL CALLBACK CCC_InwomanTopProc(HWND hTop, LPARAM)
{
    if (::IsWindowVisible(hTop))
        ::EnumChildWindows(hTop, CCC_InwomanInvalidateChild, 0);
    return TRUE;
}

static void CCC_InwomanInvalidateAll()
{
    ::EnumThreadWindows(::GetCurrentThreadId(), CCC_InwomanTopProc, 0);
}

static void CALLBACK CCC_InwomanTimerProc(HWND, UINT, UINT_PTR, DWORD)
{
    if (!CCC_IsInwoman()) return; // 通常モード時は何もしない
    CCC_InwomanInvalidateAll();
}

void CCC_StartInwomanTimer()
{
    if (g_inwomanTimer == 0)
        g_inwomanTimer = ::SetTimer(NULL, 0, 70, CCC_InwomanTimerProc);
}

BOOL CCC_InwomanHotkey(MSG* pMsg, CWnd* pWnd)
{
    UNREFERENCED_PARAMETER(pWnd);
    CCC_StartInwomanTimer();
    if (!pMsg || pMsg->message != WM_KEYDOWN || pMsg->wParam != VK_F12)
        return FALSE;

    const DWORD now = ::GetTickCount();
    // 裏モードらしく「2秒以内に5連打」を要求する。最初の押下から2秒を超えたら
    // 回数をリセットして最初からやり直し。
    if (g_f12Count == 0 || (now - g_f12First) > 2000)
    {
        g_f12Count = 0;
        g_f12First = now;
    }

    if (++g_f12Count >= 5)
    {
        g_f12Count = 0;
        savedata.inwoman = savedata.inwoman ? 0 : 1;
        CCC_InwomanInvalidateAll(); // ON/OFFどちらも1回更新して反映
        return TRUE; // 5回目はトグルとして消費
    }
    return FALSE;
}

// とろけ顔(ハート目 + 半開きの口 + ほてり + 汗)。発情した子の表情をコンパクトに。
// コントロールの隅に小さく置く想定(中央や文字の上は塗らない)。
static void CCC_DrawAhegaoFace(CDC* pDC, int cx, int cy, int sz, double twitch, BOOL bAeroTrans)
{
    if (!pDC || sz < 8) return;

    // 深い火照り(頬の赤み)— 濃いめにして発情感を強める
    if (!bAeroTrans)
    {
        const int rx = sz / 2, ry = sz / 3;
        FillRectAlpha(pDC, CRect(cx - sz / 2 - rx / 2, cy, cx - sz / 6, cy + ry), RGB(255, 70, 120), 110);
        FillRectAlpha(pDC, CRect(cx + sz / 6, cy, cx + sz / 2 + rx / 2, cy + ry), RGB(255, 70, 120), 110);
    }

    const int eo = sz / 3;
    const int ey = cy - sz / 8 - (int)(sz / 8 * twitch); // ビクッで上に
    const int es = max(4, sz / 2);
    // 目: 普段は「とろん」と潤んだ半目(ハート頼みを減らす)。
    //     絶頂(twitch高)の瞬間だけハート目にして"イってる"感を出す。
    if (twitch > 0.55)
    {
        DrawHeart(pDC, CRect(cx - eo - es / 2, ey - es / 2, cx - eo + es / 2, ey + es / 2), RGB(255, 40, 92));
        DrawHeart(pDC, CRect(cx + eo - es / 2, ey - es / 2, cx + eo + es / 2, ey + es / 2), RGB(255, 40, 92));
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
        // 重い上まぶた(半目)
        CPen lid(PS_SOLID, max(1, sz / 14), RGB(120, 40, 70));
        CPen* opn = pDC->SelectObject(&lid);
        pDC->MoveTo(cx - eo - er, ey - er / 2); pDC->LineTo(cx - eo + er, ey - er / 2);
        pDC->MoveTo(cx + eo - er, ey - er / 2); pDC->LineTo(cx + eo + er, ey - er / 2);
        pDC->SelectObject(opn);
    }

    // 半開きの口 + だらしなく伸びた舌(ビクッで大きく開く)
    const int my = cy + sz / 3;
    const int mw = max(4, sz * 2 / 5);
    const int mh = max(3, sz / 5) + (int)(sz / 5 * twitch);
    CBrush bm(RGB(150, 30, 52));
    CBrush* ob = pDC->SelectObject(&bm);
    CGdiObject* op = pDC->SelectStockObject(NULL_PEN);
    pDC->Ellipse(cx - mw / 2, my - mh / 2, cx + mw / 2, my + mh / 2 + 1);
    // 舌(下にだらりと伸びる)
    CBrush bt(RGB(255, 120, 150));
    pDC->SelectObject(&bt);
    pDC->RoundRect(cx - mw / 4, my, cx + mw / 4, my + mh / 2 + (int)(sz / 4 * (0.4 + twitch)), 3, 3);
    if (op) pDC->SelectObject(op);
    pDC->SelectObject(ob);

    // よだれ(口角から1筋たらり)
    if (!bAeroTrans)
        FillRectAlpha(pDC, CRect(cx + mw / 2 - 1, my, cx + mw / 2 + 1, my + sz / 3), RGB(235, 240, 255), 140);
    DrawShine(pDC, cx - mw / 6, my, max(1, mw / 6), max(1, mh / 4), RGB(255, 200, 220));

    // 汗(こめかみ)
    CBrush bs(RGB(190, 225, 255));
    CBrush* ob2 = pDC->SelectObject(&bs);
    CGdiObject* op2 = pDC->SelectStockObject(NULL_PEN);
    pDC->Ellipse(cx + sz / 2 - 1, cy - sz / 2, cx + sz / 2 + 2, cy - sz / 2 + 3);
    if (op2) pDC->SelectObject(op2);
    pDC->SelectObject(ob2);
}

// 愛液表現: 下端からとろりと滴る半透明のしずく + 細い糸。
// 透明感のある乳白色〜淡いピンクで、脈動(breath)とビクッ(twitch)で伸縮する。
static void CCC_DrawLoveFluid(CDC* pDC, const CRect& rc, double breath, double twitch, BOOL bAeroTrans)
{
    if (!pDC || rc.Width() < 12 || rc.Height() < 12) return;
    const COLORREF fluid = RGB(235, 240, 255);
    const COLORREF tint  = RGB(255, 226, 241);

    // 下端にとろりとした溜まり(薄い半透明の帯)を敷いて"濡れ"感を強める
    if (!bAeroTrans)
    {
        const int pool = max(2, rc.Height() / 12);
        FillRectAlpha(pDC, CRect(rc.left, rc.bottom - pool, rc.right, rc.bottom), fluid, 70);
    }

    const int n = (rc.Width() >= 64) ? 4 : 3;
    for (int i = 0; i < n; ++i)
    {
        int x = rc.left + rc.Width() * (i * 2 + 1) / (n * 2);
        x += (int)(2 * sin((double)i * 1.7 + breath * 6.2831853));
        int drip = (int)(rc.Height() * 0.22 * (0.5 + 0.5 * breath) + rc.Height() * 0.16 * twitch);
        if (drip < 4) drip = 4;
        if (drip > rc.Height() * 3 / 5) drip = rc.Height() * 3 / 5;
        const int y0 = rc.bottom - drip;
        if (!bAeroTrans)
            FillRectAlpha(pDC, CRect(x - 1, y0, x + 1, rc.bottom), fluid, 140);
        const int r = max(2, rc.Width() / 22);
        CBrush bb(tint);
        CBrush* ob = pDC->SelectObject(&bb);
        CGdiObject* op = pDC->SelectStockObject(NULL_PEN);
        pDC->Ellipse(x - r, rc.bottom - r * 2, x + r, rc.bottom);
        if (op) pDC->SelectObject(op);
        pDC->SelectObject(ob);
        DrawShine(pDC, x - r / 2, rc.bottom - r - r / 2, max(1, r / 3), max(1, r / 2));
    }
}

// バイブ表現(主役): 丸い先端のトイ + ブーンの振動線。twitch/時間で小刻みに震える。
// 愛液まみれの照り(濡れツヤ)をまとい、本体の下端から愛液を糸を引いて滴らせる。
// 方向性: 体液・玩具中心 / イッてる最中(脈動 breath とビクッ twitch で滴りが伸びる)。
static void CCC_DrawVibrator(CDC* pDC, int cx, int cy, int sz, double t, double twitch, double breath, BOOL bAeroTrans)
{
    if (!pDC || sz < 10) return;
    cx += (int)(2 * sin(t / 16.0)) + (int)(2 * twitch);   // ブーンと震える
    const int w = max(5, sz / 3);
    const int h = sz;
    const int top = cy - h / 2;
    const int bot = cy + h / 2;
    const COLORREF body  = RGB(228, 126, 198);
    const COLORREF tip   = RGB(255, 182, 226);
    const COLORREF fluid = RGB(235, 240, 255);

    CBrush bb(body);
    CBrush* ob = pDC->SelectObject(&bb);
    CGdiObject* op = pDC->SelectStockObject(NULL_PEN);
    pDC->RoundRect(cx - w / 2, top + w / 2, cx + w / 2, bot, w, w);   // 本体
    CBrush bt(tip);
    pDC->SelectObject(&bt);
    pDC->Ellipse(cx - w / 2, top, cx + w / 2, top + w);              // 丸い先端
    if (op) pDC->SelectObject(op);
    pDC->SelectObject(ob);

    // 愛液まみれの濡れツヤ(縦ハイライト2本)
    DrawShine(pDC, cx - w / 5, top + w / 3, max(1, w / 6), max(2, h / 3), RGB(255, 235, 245));
    DrawShine(pDC, cx + w / 6, cy, max(1, w / 8), max(1, h / 6), RGB(255, 255, 255));

    // 振動線(ブーン)
    CPen pen(PS_SOLID, 1, RGB(255, 212, 236));
    CPen* opn = pDC->SelectObject(&pen);
    for (int s = 0; s < 2; ++s)
    {
        const int dx = w / 2 + 3 + s * 3;
        pDC->MoveTo(cx - dx, cy - h / 5); pDC->LineTo(cx - dx, cy + h / 8);
        pDC->MoveTo(cx + dx, cy - h / 5); pDC->LineTo(cx + dx, cy + h / 8);
    }
    pDC->SelectObject(opn);

    // 本体下端から愛液を垂らす(糸+先端のしずく。脈動/ビクッで伸びる)
    for (int i = 0; i < 2; ++i)
    {
        const int x = cx + ((i == 0) ? -w / 5 : w / 5);
        int drip = (int)(h * 0.32 * (0.5 + 0.5 * breath) + h * 0.28 * twitch);
        if (drip < 4) drip = 4;
        if (!bAeroTrans)
            FillRectAlpha(pDC, CRect(x - 1, bot - 1, x + 1, bot + drip), fluid, 155);
        const int r = max(2, w / 4);
        CBrush bf(RGB(255, 230, 244));
        CBrush* obf = pDC->SelectObject(&bf);
        CGdiObject* opf = pDC->SelectStockObject(NULL_PEN);
        pDC->Ellipse(x - r, bot + drip - r, x + r, bot + drip + r);
        if (opf) pDC->SelectObject(opf);
        pDC->SelectObject(obf);
        DrawShine(pDC, x - r / 3, bot + drip - r / 3, max(1, r / 3), max(1, r / 2));
    }
}

// 淫女モードの演出オーバーレイ(方向性: 発情して自慰中の子が"そこにいる"雰囲気)。
// SM/拘束は無し。読みやすさ優先で中央は塗らず、縁と隅だけで火照り・トロ顔・汗・ビクッ。
// ハート一辺倒にならないよう、愛液(滴り)とバイブ(振動)も添える。
// bAeroTrans時は半透明演出を避ける。
void CCC_DrawInwoman(CDC* pDC, const CRect& rc, BOOL bAeroTrans)
{
    if (!pDC || !CCC_IsInwoman() || rc.Width() < 8 || rc.Height() < 8)
        return;

    const DWORD t = ::GetTickCount();
    const int W = rc.Width(), H = rc.Height();

    // はぁ…はぁ… ゆっくりした発情の脈動 + ビクッ(鋭いスパイク)
    const double breath = 0.5 + 0.5 * sin(t / 600.0);
    const double cyc = (t % 1700) / 1700.0;
    double twitch = (cyc < 0.12) ? sin(cyc / 0.12 * 3.14159265) : 0.0;
    twitch *= twitch;
    const double heat = min(1.0, breath * 0.6 + twitch);

    // --- 発情の火照り: 縁が熱く脈打つ(中央は塗らない=文字/アイコンは読める) ---
    //     濃いめ+広めにして"のぼせた"色気を強調する。
    if (!bAeroTrans)
    {
        const int g = 22 + (int)(78 * heat);
        const COLORREF hot = RGB(255, 60, 110);
        const int b = max(3, min(W, H) / 6);
        FillRectAlpha(pDC, CRect(rc.left, rc.top, rc.right, rc.top + b), hot, g);
        FillRectAlpha(pDC, CRect(rc.left, rc.bottom - b, rc.right, rc.bottom), hot, g);
        FillRectAlpha(pDC, CRect(rc.left, rc.top, rc.left + b, rc.bottom), hot, g * 7 / 10);
        FillRectAlpha(pDC, CRect(rc.right - b, rc.top, rc.right, rc.bottom), hot, g * 7 / 10);

        // 上端から立ちのぼる"はぁ…"の湯気(白い半透明の小さな塊が揺らぐ)
        if (W >= 40 && H >= 22)
        {
            const int puffs = (W >= 80) ? 3 : 2;
            for (int i = 0; i < puffs; ++i)
            {
                const double ph = t / 520.0 + i * 1.3;
                const int px = rc.left + rc.Width() * (i * 2 + 1) / (puffs * 2) + (int)(3 * sin(ph));
                const int rise = (int)((0.5 + 0.5 * sin(ph)) * (H / 6));
                const int py = rc.top + 3 + rise;
                const int pr = max(2, W / 26);
                FillRectAlpha(pDC, CRect(px - pr, py - pr, px + pr, py + pr), RGB(255, 245, 250), 26);
            }
        }
    }

    // --- 主役: 愛液を垂らしたバイブ(体液・玩具中心。顔は出さない) ---
    // 右寄りに立てて中央の文字/アイコンを大きく覆わないようにしつつ、はっきり見せる。
    if (W >= 40 && H >= 20)
    {
        const int vs = min(H * 7 / 10, max(16, W * 4 / 10));
        const int vw = max(5, vs / 3);
        const int vx = rc.right - vw / 2 - max(4, W / 12);
        const int vy = rc.top + H / 2 - (int)(2 * twitch);
        CCC_DrawVibrator(pDC, vx, vy, vs, (double)t, twitch, breath, bAeroTrans);
    }

    // --- 愛液: 下端からとろりと滴る(溜まり+しずく) ---
    CCC_DrawLoveFluid(pDC, rc, breath, twitch, bAeroTrans);

    // --- 汗の玉: 左上から1粒、ゆらゆら(べた塗りなので透過でも安全) ---
    {
        const int sx = rc.left + 5 + (int)(2 * sin(t / 85.0));
        const int sy = rc.top + 4 + (int)(3 * (0.5 + 0.5 * sin(t / 120.0)));
        CBrush bb(RGB(190, 225, 255));
        CBrush* ob = pDC->SelectObject(&bb);
        CGdiObject* op = pDC->SelectStockObject(NULL_PEN);
        pDC->Ellipse(sx - 2, sy - 3, sx + 2, sy + 2);
        if (op) pDC->SelectObject(op);
        pDC->SelectObject(ob);
        DrawShine(pDC, sx - 1, sy - 1, 1, 1);
    }
}

// ============================================================================
// 子コントロールを一括でサブクラス化する処理
// ============================================================================
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
            return (HBRUSH)GetStockObject(NULL_BRUSH);
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
END_MESSAGE_MAP()

static const UINT_PTR kEditOpaqueTimerId = 4107;
static const UINT_PTR kEditSelTimerId = 4108;
static const UINT_PTR kButtonAnimTimerId    = 4120; // ボタンの流れるツヤ/鼓動パルス
static const UINT_PTR kCheckBounceTimerId   = 4121; // チェックON時のバウンス
static const UINT_PTR kSliderShimmerTimerId = 4122; // スライダーの流れるシマー

CCustomEdit::CCustomEdit() : m_bHasFocus(FALSE), m_bAutoDelete(FALSE), m_bSelDrag(FALSE), m_lastSel0(-1), m_lastSel1(-1)
{
    m_brBackground.CreateSolidBrush(COLOR_EDIT_BG);
}

CCustomEdit::~CCustomEdit()
{
    if (m_fontBold.GetSafeHandle()) m_fontBold.DeleteObject();
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

void CCustomEdit::PostNcDestroy()
{
    CEdit::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

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

HBRUSH CCustomEdit::CtlColor(CDC* pDC, UINT)
{
    pDC->SetBkColor(COLOR_EDIT_BG);
    pDC->SetTextColor(COLOR_EDIT_TEXT);
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

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
            *((WORD*)buf) = (WORD)(maxc + 2);
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
}

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

void CCustomEdit::PaintOpaqueClient(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0) return;

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

void CCustomEdit::ScheduleOpaqueRepaint()
{
    if (GetSafeHwnd())
        SendMessage(CCC_WM_POST_OPAQUE_PAINT);
}

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
    }

    CRect rL(r.left + 2, r.CenterPoint().y - 3, r.left + 8, r.CenterPoint().y + 3);
    CRect rR(r.right - 8, r.CenterPoint().y - 3, r.right - 2, r.CenterPoint().y + 3);
    DrawRibbon(&dc, rL, RGB(255, 200, 220));
    DrawRibbon(&dc, rR, RGB(255, 200, 220));
}

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

void CCustomEdit::OnEnUpdate()
{
    // OpaqueFixer が WM_CHAR 等で既に不透明描画する。ここでは Invalidate せず
    // 同期 Opaque のみ(既定描画→アクリル一瞬を避ける)。
    ScheduleOpaqueRepaint();
}

void CCustomEdit::RepaintIfSelChanged()
{
    int s0 = 0, s1 = 0;
    GetSel(s0, s1);
    if (s0 == m_lastSel0 && s1 == m_lastSel1)
        return;
    m_lastSel0 = s0;
    m_lastSel1 = s1;
    RepaintClient();
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

void CCustomEdit::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CEdit::OnKeyDown(nChar, nRepCnt, nFlags);
    RepaintIfSelChanged();
}

void CCustomEdit::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CEdit::OnKeyUp(nChar, nRepCnt, nFlags);
    RepaintIfSelChanged();
}

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
    RepaintIfSelChanged();
#if CCUSTOM_AERO_SUPPORT
    if (CCC_HostNeedsChildOpaque(m_hWnd))
        ScheduleOpaqueRepaint();
#endif
}

void CCustomEdit::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    CEdit::OnLButtonDblClk(nFlags, point);
    RepaintIfSelChanged();
}

void CCustomEdit::OnLButtonUp(UINT nFlags, CPoint point)
{
    CEdit::OnLButtonUp(nFlags, point);
    m_bSelDrag = FALSE;
    KillTimer(kEditSelTimerId);
    RepaintIfSelChanged();
}

BOOL CCustomEdit::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    BOOL r = CEdit::OnMouseWheel(nFlags, zDelta, pt);
    if (!r && (GetStyle() & ES_MULTILINE)) {
        // フォーカス無し等で既定が無視するとき
        LineScroll((zDelta > 0) ? -3 : 3);
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

void CCustomEdit::OnMouseMove(UINT nFlags, CPoint point)
{
    CEdit::OnMouseMove(nFlags, point);
    if (nFlags & MK_LBUTTON)
        RepaintIfSelChanged();
}

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
    if (nIDEvent == kEditOpaqueTimerId)
    {
        ScheduleOpaqueRepaint();
        return;
    }
    CEdit::OnTimer(nIDEvent);
}

void CCustomEdit::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CEdit::OnShowWindow(bShow, nStatus);
    if (bShow)
        RepaintClient();
    UNREFERENCED_PARAMETER(nStatus);
}

void CCustomEdit::OnSetFocus(CWnd* p)
{
    CEdit::OnSetFocus(p);
    m_bHasFocus = TRUE;
    // SWP_FRAMECHANGED / Invalidate は親ガラス消去→枠消失・一瞬アクリルの元凶。
    // 枠色変更は自前の不透明 NC 描画だけで行う。
    PaintOpaqueFrame();
    {
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
#if CCUSTOM_AERO_SUPPORT
    // キャレット点滅が α=0 で穴を開けるので、キャプション常時アクリル時も再不透明化
    if (CCC_HostNeedsChildOpaque(m_hWnd))
        SetTimer(kEditOpaqueTimerId, 50, NULL);
#endif
}

void CCustomEdit::OnKillFocus(CWnd* p)
{
    CEdit::OnKillFocus(p);
    m_bHasFocus = FALSE;
    m_bSelDrag = FALSE;
    KillTimer(kEditSelTimerId);
    KillTimer(kEditOpaqueTimerId);
    PaintOpaqueFrame();
    {
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
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

CCustomStatic::~CCustomStatic()
{
    if (m_font.GetSafeHandle()) m_font.DeleteObject();
    if (m_memBackstore.GetSafeHandle()) m_memBackstore.DeleteObject();
#if CCUSTOM_AERO_SUPPORT
    m_chromaCache.Release();
#endif
}

void CCustomStatic::PostNcDestroy()
{
    CStatic::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

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
        if (bTrans && tc == RGB(0, 0, 0)) tc = RGB(2, 2, 2);

        if (m_bGradEnable) DrawTextWithGradient(pDC, sr, m_segs[i].text, DT_VCENTER | DT_SINGLELINE | DT_LEFT, m_clrGradStart, m_clrGradEnd, m_nGradDirection, m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur, m_bShadowEnable, clrBg, sz.cx, FALSE, bTrans);
        else DrawTextWithShadow(pDC, sr, m_segs[i].text, DT_VCENTER | DT_SINGLELINE | DT_LEFT, tc, m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur, m_bShadowEnable, clrBg, bTrans);

        xP += sz.cx;
        pDC->SelectObject(po);
    }
}

void CCustomStatic::SetGradation(COLORREF s, COLORREF e, int d, BOOL en)
{
    m_clrGradStart = s; m_clrGradEnd = e;
    m_nGradDirection = d % 360;
    if (m_nGradDirection < 0) m_nGradDirection += 360;
    m_bGradEnable = en;
    m_strCachedText.Empty();
    if (GetSafeHwnd()) Invalidate();
}

void CCustomStatic::GetGradation(COLORREF* ps, COLORREF* pe, int* pd, BOOL* pbe) const
{
    if (ps) *ps = m_clrGradStart;
    if (pe) *pe = m_clrGradEnd;
    if (pd) *pd = m_nGradDirection;
    if (pbe) *pbe = m_bGradEnable;
}

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

void CCustomStatic::GetDropShadow(COLORREF* pc, int* pd, int* pdist, int* pblur, BOOL* pbe) const
{
    if (pc) *pc = m_clrShadow;
    if (pd) *pd = m_nShadowDirection;
    if (pdist) *pdist = m_nShadowDistance;
    if (pblur) *pblur = m_nShadowBlur;
    if (pbe) *pbe = m_bShadowEnable;
}

void CCustomStatic::SetPreferWideMode(BOOL b)
{
    m_bPreferWideMode = b;
    m_strCachedText.Empty();
    m_nCachedDpi = 0;
    if (GetSafeHwnd()) Invalidate();
}

BOOL CCustomStatic::GetPreferWideMode() const
{
    return m_bPreferWideMode;
}

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

    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
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
                if (stretch <= 1.35)
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
                CSize size = memDC.GetTextExtent(strText);
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

            int italicMargin = lfB.lfItalic ? (finalHeight / 2) : 0;
            int needW = szFinal.cx + italicMargin;
            float scaleX = 1.0f;
            if (needW > availW)
            {
                scaleX = (float)availW / (float)needW;
                const float kMinScaleX = 0.62f;
                if (scaleX < kMinScaleX)
                {
                    finalHeight = max(kMinHeight, (int)(finalHeight * scaleX / kMinScaleX + 0.5f));
                    szFinal = MeasureText(finalHeight, 0);
                    italicMargin = lfB.lfItalic ? (finalHeight / 2) : 0;
                    needW = szFinal.cx + italicMargin;
                    scaleX = (float)availW / (float)needW;
                }
                scaleX *= 0.98f;
                if (scaleX < kMinScaleX) scaleX = kMinScaleX;
                if (scaleX > 1.0f) scaleX = 1.0f;
            }

            if (m_bPreferWideMode && szFinal.cx < availW)
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
    UINT fmt = DT_VCENTER | DT_SINGLELINE;
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

        if (m_fCachedScaleX < 0.98f)
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
        dc.BitBlt(0, 0, rw, rh, &memDC, 0, 0, SRCCOPY);

    memDC.SelectObject(ob);
    memDC.DeleteDC();
}

void CCustomStatic::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0)
        return;

#if CCUSTOM_AERO_SUPPORT
    // ソリッド背景指定 + ホスト不透明必須: クロマ透過せず α=255 で塗る
    // （MP Lib/Hist レール等の白抜け対策）
    if (m_bSolidFill && CCC_HostNeedsChildOpaque(m_hWnd)) {
        CCC_FillRectOpaqueBits(dc.GetSafeHdc(), r, m_clrSolidFill);
        if (!m_strText.IsEmpty() || (CWnd::GetWindowTextLength() > 0)) {
            // 文字がある場合は通常描画も重ねる（レールは空文字想定）
            const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
            if (!bTrans)
                DrawClient(dc);
        }
        return;
    }
#endif

    // 透過(アクリル)時はクロマ blit を潰す MakeOpaque を避ける(CCustomCheckBox と同じ)
    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
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

LRESULT CCustomStatic::OnGetTextLength(WPARAM, LPARAM)
{
    return m_strText.GetLength();
}

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

CCustomListBox::CCustomListBox() : m_bAutoDelete(FALSE), m_bAeroMode(FALSE)
{
    m_brBackground.CreateSolidBrush(COLOR_LIST_BG);
}

CCustomListBox::~CCustomListBox()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

void CCustomListBox::PostNcDestroy()
{
    CListBox::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

void CCustomListBox::PreSubclassWindow()
{
    CListBox::PreSubclassWindow();
    ModifyStyle(0, LBS_OWNERDRAWFIXED | LBS_HASSTRINGS);
}

HBRUSH CCustomListBox::CtlColor(CDC* pDC, UINT)
{
    pDC->SetBkColor(COLOR_LIST_BG);
    pDC->SetTextColor(RGB(0, 0, 0));
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

void CCustomListBox::OnPaint()
{
    Default();
}

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

BOOL CCustomListBox::OnEraseBkgnd(CDC* pDC)
{
    if (pDC)
    {
        CRect r;
        GetClientRect(&r);
        pDC->FillSolidRect(&r, COLOR_LIST_BG);
    }
    return TRUE;
}

void CCustomListBox::DrawItem(LPDRAWITEMSTRUCT lp)
{
    if (lp->itemID == (UINT)-1) return;
    CDC* pDC = CDC::FromHandle(lp->hDC);
    CRect r = lp->rcItem;
    COLORREF bg = (lp->itemState & ODS_SELECTED) ? COLOR_SEL_BG : (lp->itemID % 2 == 0 ? COLOR_LIST_BG : RGB(183, 221, 238));

    const BOOL bListAero = m_bAeroMode && !CCC_IsBlurDialogChild(m_hWnd);
    if (bListAero)
        FillRectAlpha(pDC, r, bg, (lp->itemState & ODS_SELECTED) ? 180 : AERO_ALPHA_SEMI);
    else
        pDC->FillSolidRect(&r, bg);

    if ((lp->itemState & ODS_SELECTED) && !bListAero)
        DrawGlossHighlight(pDC, r, 6);

    int it = lp->itemID % 4;
    int is = 8;
    int ix = r.left + 5;
    int iy = r.top + (r.Height() - is) / 2;

    switch (it)
    {
    case 0: DrawFlower(pDC, ix + is / 2, iy + is / 2, is / 2, RGB(255, 200, 220)); break;
    case 1: DrawStar(pDC, ix + is / 2, iy + is / 2, is / 3, RGB(255, 215, 0)); break;
    case 2: DrawHeart(pDC, CRect(ix, iy, ix + is, iy + is), COLOR_HEART); break;
    case 3: DrawRibbon(pDC, CRect(ix, iy, ix + is, iy + is), RGB(255, 182, 193)); break;
    }

    if (lp->itemState & ODS_SELECTED) DrawStar(pDC, r.right - 12, r.top + r.Height() / 2, 3, RGB(255, 215, 0));

    CString st;
    GetText(lp->itemID, st);
    CRect rt = r;
    rt.left += 20;
    rt.DeflateRect(1, 1);

    COLORREF tc = m_bAeroMode ? RGB(1, 1, 1) : COLOR_EDIT_TEXT;
    pDC->SetTextColor(tc);
    pDC->SetBkMode(TRANSPARENT);

    CFont* po = pDC->SelectObject(GetFont());
    DrawListSubitemCellText(pDC, st, rt);
    pDC->SelectObject(po);

    if (lp->itemID < (UINT)(GetCount() - 1)) DrawLaceLine(pDC, r.left + 15, r.bottom - 1, r.right - 15, r.bottom - 1, RGB(200, 180, 220));
}

void CCustomListBox::MeasureItem(LPMEASUREITEMSTRUCT lp)
{
    lp->itemHeight = 24;
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

CCustomComboBox::CCustomComboBox()
    : m_bAutoDelete(FALSE), m_clrLabelText(RGB(240, 240, 255)),
    m_clrLabelBg(RGB(80, 60, 120)), m_bAeroMode(FALSE)
{
    m_brBackground.CreateSolidBrush(COLOR_COMBO_BG);
}

CCustomComboBox::~CCustomComboBox()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

void CCustomComboBox::PostNcDestroy()
{
    CComboBox::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

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

int CCustomComboBox::GetCurSel() const
{
    int np = CComboBox::GetCurSel();
    if (np < 0) return -1;
    for (int i = 0; i < (int)m_vSelectableIndices.size(); i++)
        if (m_vSelectableIndices[i] == np) return i;
    return -1;
}

int CCustomComboBox::SetCurSel(int n)
{
    if (n < 0) return CComboBox::SetCurSel(-1);
    if (n >= (int)m_vSelectableIndices.size())
    {
        if (m_vSelectableIndices.empty()) return CB_ERR;
        n = (int)m_vSelectableIndices.size() - 1;
    }
    return CComboBox::SetCurSel(m_vSelectableIndices[n]);
}

void CCustomComboBox::SetLabelColor(COLORREF ct, COLORREF cb)
{
    m_clrLabelText = ct;
    m_clrLabelBg = cb;
    if (GetSafeHwnd()) Invalidate();
}

void CCustomComboBox::GetLabelColor(COLORREF* pct, COLORREF* pcb) const
{
    if (pct) *pct = m_clrLabelText;
    if (pcb) *pcb = m_clrLabelBg;
}

int CCustomComboBox::LogicalToPhysical(int n) const
{
    if (n < 0 || n >= (int)m_vSelectableIndices.size()) return -1;
    return m_vSelectableIndices[n];
}

int CCustomComboBox::PhysicalToLogical(int n) const
{
    for (int i = 0; i < (int)m_vSelectableIndices.size(); i++)
        if (m_vSelectableIndices[i] == n) return i;
    return -1;
}

void CCustomComboBox::PreSubclassWindow()
{
    CComboBox::PreSubclassWindow();
    DWORD dw = GetStyle();
    dw &= ~CBS_OWNERDRAWVARIABLE;
    dw |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
    ModifyStyle(0, CBS_OWNERDRAWFIXED | CBS_HASSTRINGS);
    SetWindowLong(GetSafeHwnd(), GWL_STYLE, dw);
}

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

BOOL CCustomComboBox::OnEraseBkgnd(CDC* pDC)
{
    if (pDC)
    {
        CRect r;
        GetClientRect(&r);
        pDC->FillSolidRect(&r, COLOR_COMBO_BG);
    }
    return TRUE;
}

void CCustomComboBox::PaintClient(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0)
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
    mDC.RoundRect(&r, CPoint(10, 10));

    int nb = GetSystemMetrics(SM_CXVSCROLL);
    CRect rB(r.right - nb - 4, r.top + 4, r.right - 4, r.bottom - 4);
    mDC.FillSolidRect(&rB, RGB(255, 200, 220));
    DrawGlossHighlight(&mDC, rB, 6);

    {
        CPen pb(PS_SOLID, 1, RGB(200, 150, 180));
        mDC.SelectObject(&pb);
        mDC.SelectStockObject(NULL_BRUSH);
        mDC.RoundRect(&rB, CPoint(6, 6));
        mDC.SelectObject(op);
    }

    // ハート3つはやめ、ひとつのリボンで上品に
    {
        int cy2 = rB.Height() / 2 + rB.top;
        int bw = min(rB.Width() - 4, 16);
        DrawBow(&mDC, CRect(rB.CenterPoint().x - bw / 2, cy2 - 5, rB.CenterPoint().x + bw / 2, cy2 + 5), COLOR_BOW);
    }

    DrawSparkle(&mDC, r.right - 8, r.top + 8, 4, COLOR_SPARKLE);

    int nPS = CComboBox::GetCurSel();
    CString st;
    if (nPS != CB_ERR) GetLBText(nPS, st);

    COLORREF tc = bTrans ? RGB(1, 1, 1) : RGB(0, 0, 0);
    mDC.SetTextColor(tc);

    CFont* pOF = mDC.SelectObject(GetFont());
    CRect rt = r;
    rt.left += 12;
    rt.right = rB.left - 4;

    BOOL bIL = (nPS >= 0 && nPS < (int)m_vDisabledItems.size() && m_vDisabledItems[nPS]);
    if (nPS != CB_ERR && !bIL)
    {
        int cs = (rt.Height() - 8) / 2;
        DrawCrown(&mDC, rt.left + cs, rt.Height() / 2, cs, RGB(255, 215, 0));
        rt.left += cs * 2 + 4;
    }

    mDC.SetBkMode(TRANSPARENT);
    mDC.DrawText(st, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    mDC.SelectObject(pOF);

    CCC_DrawInwoman(&mDC, r, bTrans);

    if (bTrans) CCC_ClearDestBlt(dc.GetSafeHdc(), 0, 0, r.Width(), r.Height(), mDC.GetSafeHdc(), 0, 0, RGB(0, 0, 0));
    else dc.BitBlt(0, 0, r.Width(), r.Height(), &mDC, 0, 0, SRCCOPY);

    mDC.SelectObject(ob);
    mB.DeleteObject();
    mDC.DeleteDC();
}

void CCustomComboBox::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
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

LRESULT CCustomComboBox::OnPrintClient(WPARAM wParam, LPARAM)
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

void CCustomComboBox::DrawItem(LPDRAWITEMSTRUCT lp)
{
    if (lp->itemID == (UINT)-1) return;
    CDC* pDC = CDC::FromHandle(lp->hDC);
    CRect r = lp->rcItem;
    BOOL bD = (lp->itemID < (UINT)m_vDisabledItems.size()) && m_vDisabledItems[lp->itemID];
    BOOL bS = !bD && (lp->itemState & ODS_SELECTED);
    COLORREF bg = bD ? m_clrLabelBg : (bS ? COLOR_SEL_BG : (lp->itemID % 2 == 0 ? COLOR_COMBO_BG : RGB(255, 232, 220)));

    if (m_bAeroMode && !CCC_IsBlurDialogChild(m_hWnd))
    {
        pDC->FillSolidRect(&r, RGB(0, 0, 0));
        FillRectAlpha(pDC, r, bg, bS ? 180 : bD ? 200 : AERO_ALPHA_SEMI);
    }
    else pDC->FillSolidRect(&r, bg);

    if (!bD)
    {
        int it = lp->itemID % 4;
        int is = 8;
        int ix = r.left + 6;
        int iy = r.top + (r.Height() - is) / 2;
        switch (it)
        {
        case 0: DrawFlower(pDC, ix + is / 2, iy + is / 2, is / 2, RGB(255, 200, 220)); break;
        case 1: DrawStar(pDC, ix + is / 2, iy + is / 2, is / 3, RGB(255, 215, 0)); break;
        case 2: DrawHeart(pDC, CRect(ix, iy, ix + is, iy + is), COLOR_HEART); break;
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
        pDC->SetTextColor(m_bAeroMode ? RGB(1, 1, 1) : RGB(0, 0, 0));
        lf.lfWeight = FW_BOLD;
    }
    fc.CreateFontIndirect(&lf);
    pOF = pDC->SelectObject(&fc);
    pDC->SetBkMode(TRANSPARENT);
    CRect rt = r;
    rt.left += bD ? 4 : 20;
    pDC->DrawText(st, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (pOF)
    {
        pDC->SelectObject(pOF);
        fc.DeleteObject();
    }
    if (bS && !bD) DrawCrown(pDC, r.right - 14, r.top + r.Height() / 2, 6, RGB(255, 215, 0));
}

void CCustomComboBox::MeasureItem(LPMEASUREITEMSTRUCT lp)
{
    lp->itemHeight = 28;
}

void CCustomComboBox::OnDropdown()
{
    UpdateDropDownWidth();
}

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

    mW += GetSystemMetrics(SM_CXVSCROLL) + 40;
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

CCustomSliderCtrl::CCustomSliderCtrl() : m_bAutoDelete(FALSE), m_nMode(0), m_bAeroMode(FALSE),
    m_nShimmer(0), m_bHover(FALSE), m_backstoreW(0), m_backstoreH(0) {}
CCustomSliderCtrl::~CCustomSliderCtrl()
{
    if (m_memBackstore.GetSafeHandle()) m_memBackstore.DeleteObject();
#if CCUSTOM_AERO_SUPPORT
    m_chromaCache.Release();
#endif
}

LRESULT CCustomSliderCtrl::OnMouseLeaveMsg(WPARAM, LPARAM)
{
    m_bHover = FALSE;
    KillTimer(kSliderShimmerTimerId);
    Invalidate(FALSE);
    return 0;
}

void CCustomSliderCtrl::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kSliderShimmerTimerId)
    {
        m_nShimmer++;
        Invalidate(FALSE);
        return;
    }
    CSliderCtrl::OnTimer(nIDEvent);
}

void CCustomSliderCtrl::PostNcDestroy()
{
    CSliderCtrl::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

void CCustomSliderCtrl::SetMode(int m)
{
    m_nMode = m;
    if (GetSafeHwnd()) Invalidate(FALSE);
}
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
void CCustomSliderCtrl::PreSubclassWindow()
{
    CSliderCtrl::PreSubclassWindow();
}

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

    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
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
        dc.BitBlt(0, 0, rw, rh, &mDC, 0, 0, SRCCOPY);
    }
    mDC.SelectObject(ob);
    mDC.DeleteDC();
}

void CCustomSliderCtrl::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);

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

LRESULT CCustomSliderCtrl::OnMouseMoveMsg(WPARAM w, LPARAM l)
{
    LRESULT r = Default();
    if (!m_bHover)
    {
        TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&t);
        m_bHover = TRUE;
        SetTimer(kSliderShimmerTimerId, 40, NULL);
    }
#if CCUSTOM_AERO_SUPPORT
    CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
    Invalidate(FALSE);
    return r;
}
LRESULT CCustomSliderCtrl::OnLButtonDownMsg(WPARAM w, LPARAM l)
{
    LRESULT r = Default();
#if CCUSTOM_AERO_SUPPORT
    CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
    Invalidate(FALSE);
    return r;
}
LRESULT CCustomSliderCtrl::OnLButtonUpMsg(WPARAM w, LPARAM l)
{
    LRESULT r = Default();
#if CCUSTOM_AERO_SUPPORT
    CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
    Invalidate(FALSE);
    return r;
}

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

    // ホバー中: 通ってきたトラック上をきらめきがスーッと流れる
    if (m_bHover && mx > mn)
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
                const int gx = tL + (int)((m_nShimmer * 4) % (UINT)span);
                DrawShine(pDC, gx, cY, 3, 3);
                DrawSparkle(pDC, gx, cY, 2, COLOR_SPARKLE);
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
                const int gy = tB - (int)((m_nShimmer * 4) % (UINT)span);
                DrawShine(pDC, cX, gy, 3, 3);
                DrawSparkle(pDC, cX, gy, 2, COLOR_SPARKLE);
            }
        }
    }
}

// 描画モード0：バーと音符のつまみ
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

// 描画モード1：紫系グラデーションとダイヤのつまみ
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

// 描画モード2：緑系グラデーションとダイヤのつまみ
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

CCustomRangeSliderCtrl::CCustomRangeSliderCtrl()
    : m_bAutoDelete(FALSE), m_nMin(0), m_nMax(100), m_nSelMin(0), m_nSelMax(100),
    m_nAbA(-1), m_nAbB(-1), m_bSelLocked(TRUE),
    m_nDragTarget(0), m_bDragging(FALSE), m_nVisualPos(0), m_nLogicalPos(0), m_bAeroMode(FALSE),
    m_wavePeakCount(0), m_cueCount(0), m_nCueClick(-1),
    m_ribbonN(0), m_xfadePreviewMs(0), m_timeBaseHz(44100),
    m_beatBpm(120.f), m_bBeatGrid(FALSE), m_bHoverTracking(FALSE),
    m_backstoreW(0), m_backstoreH(0)
{
    ZeroMemory(m_wavePeaks, sizeof(m_wavePeaks));
    ZeroMemory(m_cueFrames, sizeof(m_cueFrames));
    ZeroMemory(m_ribbon, sizeof(m_ribbon));
}
CCustomRangeSliderCtrl::~CCustomRangeSliderCtrl() {}

void CCustomRangeSliderCtrl::PostNcDestroy()
{
    CSliderCtrl::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}
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

BOOL CCustomRangeSliderCtrl::PreTranslateMessage(MSG* pMsg)
{
    if (m_hoverTip.GetSafeHwnd())
        m_hoverTip.RelayEvent(pMsg);
    return CSliderCtrl::PreTranslateMessage(pMsg);
}

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

int CCustomRangeSliderCtrl::GetPos() const
{
    // ドラッグ中は見た目位置を返す（親の確定シークが旧 LogicalPos を拾わない）
    return m_bDragging ? m_nVisualPos : m_nLogicalPos;
}

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

void CCustomRangeSliderCtrl::SetSelection(int mn, int mx)
{
    if (mn > mx) { int t = mn; mn = mx; mx = t; }
    if (mn == m_nSelMin && mx == m_nSelMax) return;
    m_nSelMin = mn;
    m_nSelMax = mx;
    if (::IsWindow(m_hWnd))
        Invalidate(FALSE);
}

void CCustomRangeSliderCtrl::SetAB(int a, int b)
{
    if (a == m_nAbA && b == m_nAbB) return;
    m_nAbA = a;
    m_nAbB = b;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

void CCustomRangeSliderCtrl::GetAB(int& a, int& b) const
{
    a = m_nAbA;
    b = m_nAbB;
}

void CCustomRangeSliderCtrl::SetSelectionLocked(BOOL bLocked)
{
    if ((bLocked ? TRUE : FALSE) == m_bSelLocked) return;
    m_bSelLocked = bLocked ? TRUE : FALSE;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

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

void CCustomRangeSliderCtrl::ClearWavePeaks()
{
    SetWavePeaks(NULL, 0);
}

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

void CCustomRangeSliderCtrl::ClearCues()
{
    SetCues(NULL, 0);
}

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

void CCustomRangeSliderCtrl::SetXfadePreviewMs(int ms)
{
    if (ms < 0) ms = 0;
    if (ms == m_xfadePreviewMs) return;
    m_xfadePreviewMs = ms;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

void CCustomRangeSliderCtrl::SetTimeBaseHz(int hz)
{
    if (hz < 8000) hz = 44100;
    if (hz == m_timeBaseHz) return;
    m_timeBaseHz = hz;
}

void CCustomRangeSliderCtrl::SetBeatGrid(float bpm, BOOL enabled)
{
    if (bpm <= 1.f) bpm = 120.f;
    const BOOL en = enabled ? TRUE : FALSE;
    if (en == m_bBeatGrid && fabsf(bpm - m_beatBpm) < 0.01f) return;
    m_bBeatGrid = en;
    m_beatBpm = bpm;
    if (::IsWindow(m_hWnd) && ::IsWindowVisible(m_hWnd))
        Invalidate(FALSE);
}

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

void CCustomRangeSliderCtrl::GetSelection(int& mn, int& mx) const
{
    mn = max(m_nMin, min(m_nMax, m_nSelMin));
    mx = max(m_nMin, min(m_nMax, m_nSelMax));
}

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

    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    if (bTrans)
    {
        mDC.FillSolidRect(&r, CCC_AERO_CHROMA_KEY);
        DrawRangeSlider(&mDC);
        CCC_DrawInwoman(&mDC, r, TRUE);
#if CCUSTOM_AERO_SUPPORT
        if (CCC_IsAeroEnabled() && CCC_IsWin11())
            CCC_BlitChromaNF(dc.GetSafeHdc(), 0, 0, rw, rh,
                mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
        else
#endif
            CCC_ClearDestBlt(dc.GetSafeHdc(), 0, 0, rw, rh,
                mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    }
    else
    {
        mDC.FillSolidRect(&r, COLOR_DIALOG_BG);
        DrawRangeSlider(&mDC);
        CCC_DrawInwoman(&mDC, r, FALSE);
        dc.BitBlt(0, 0, rw, rh, &mDC, 0, 0, SRCCOPY);
    }
    mDC.SelectObject(ob);
    mDC.DeleteDC();
}

void CCustomRangeSliderCtrl::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
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

    // 拍グリッド(薄い縦線)
    if (m_bBeatGrid && m_timeBaseHz > 0) {
        const float bpm = (m_beatBpm > 1.f) ? m_beatBpm : 120.f;
        const double framesPerBeat = (double)m_timeBaseHz * 60.0 / (double)bpm;
        if (framesPerBeat > 1.0) {
            const int span = m_nMax - m_nMin;
            int maxLines = (int)(span / framesPerBeat) + 2;
            if (maxLines > 256) maxLines = 256;
            COLORREF gc = m_bAeroMode ? RGB(60, 60, 70) : RGB(210, 215, 225);
            for (int i = 0; i < maxLines; ++i) {
                int fv = m_nMin + (int)(i * framesPerBeat + 0.5);
                if (fv > m_nMax) break;
                int x = ValueToPixel(fv);
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

    // 現在位置（ハート + きらめき）— 波形時は XOR で波形を潰さない
    DrawHeart(pDC, CRect(xP - 9, cy - 12, xP + 9, cy + 6), COLOR_SLIDER_THUMB);
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

int CCustomRangeSliderCtrl::ValueToPixel(int v) const
{
    CRect r;
    GetClientRect(&r);
    int w = r.Width() - 28;
    if (w <= 0 || m_nMax <= m_nMin) return 14;
    return 14 + (int)((long long)(max(m_nMin, min(m_nMax, v)) - m_nMin) * w / (m_nMax - m_nMin));
}

int CCustomRangeSliderCtrl::PixelToValue(int x) const
{
    CRect r;
    GetClientRect(&r);
    int w = r.Width() - 28;
    if (w <= 0 || m_nMax <= m_nMin) return m_nMin;
    return m_nMin + (int)((double)(max(14, min(r.Width() - 14, x)) - 14) / w * (m_nMax - m_nMin) + 0.5);
}

int CCustomRangeSliderCtrl::HitTest(CPoint p) const
{
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
    // ロック解除時のみ loop つまみを再生位置より優先。ロック中はシークを優先（クリックを食わない）
    if (!m_bSelLocked) {
        if (CRect(xMx - 7, cy - 10, xMx + 7, cy + 10).PtInRect(p)) return 2;
        if (CRect(xMn - 7, cy - 10, xMn + 7, cy + 10).PtInRect(p)) return 1;
    }
    if (CRect(xM - 10, cy - 14, xM + 10, cy + 14).PtInRect(p)) return 3;
    return 0;
}
void CCustomRangeSliderCtrl::OnLButtonDown(UINT f, CPoint p)
{
#if CCUSTOM_AERO_SUPPORT
    CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
    SetFocus();
    m_nVisualPos = m_nLogicalPos;
    m_nDragTarget = HitTest(p);
    if (m_nDragTarget >= 10 && m_nDragTarget < 10 + kCueMax) {
        // キュークリック → 親が GetCueClick でジャンプ
        m_nCueClick = m_nDragTarget - 10;
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
#if CCUSTOM_AERO_SUPPORT
        CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
    }
}
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
        if (m_nDragTarget == 1 || m_nDragTarget == 2 || m_nDragTarget == 4 || m_nDragTarget == 5)
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, 0), (LPARAM)m_hWnd);
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
}

void CCustomRangeSliderCtrl::OnMouseLeave()
{
    m_bHoverTracking = FALSE;
    if (m_hoverTip.GetSafeHwnd())
        m_hoverTip.SendMessage(TTM_POP, 0, 0);
}

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

CCustomListCtrl::CCustomListCtrl()
    : m_bAutoDelete(FALSE), m_nHotItem(-1), m_bAeroMode(FALSE)
{
    m_brBackground.CreateSolidBrush(COLOR_LIST_BG);
}

CCustomListCtrl::~CCustomListCtrl()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

void CCustomListCtrl::PostNcDestroy()
{
    CListCtrlA::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}
void CCustomListCtrl::PreSubclassWindow()
{
    CListCtrlA::PreSubclassWindow();
    ModifyStyle(0, WS_CLIPCHILDREN);
    SetBkColor(COLOR_LIST_BG);
    SetTextBkColor(COLOR_LIST_BG);
    SetTextColor(RGB(0, 0, 0));
    SetExtendedStyle(GetExtendedStyle() | LVS_EX_DOUBLEBUFFER);
}
HBRUSH CCustomListCtrl::CtlColor(CDC* pDC, UINT)
{
    pDC->SetBkColor(COLOR_LIST_BG);
    pDC->SetTextColor(RGB(0, 0, 0));
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

void CCustomListCtrl::OnMouseMove(UINT f, CPoint p)
{
    LVHITTESTINFO h;
    h.pt = p;
    UpdateHotItem(SubItemHitTest(&h));

    TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, m_hWnd, 0 };
    TrackMouseEvent(&t);
    CListCtrl::OnMouseMove(f, p);
}

void CCustomListCtrl::OnMouseLeave()
{
    UpdateHotItem(-1);
    CListCtrl::OnMouseLeave();
}
void CCustomListCtrl::ScheduleOpaqueRepaint()
{
    if (GetSafeHwnd())
        PostMessage(CCC_WM_POST_OPAQUE_PAINT);
}

LRESULT CCustomListCtrl::OnPostOpaquePaint(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
    {
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
#endif
    return 0;
}

void CCustomListCtrl::OnVScroll(UINT n, UINT p, CScrollBar* s)
{
    CListCtrl::OnVScroll(n, p, s);
    // OpaqueFixer が直後に全面描画する。ここでの Invalidate は名前列ちらつきの元。
    m_nHotItem = -1;
}
void CCustomListCtrl::OnHScroll(UINT n, UINT p, CScrollBar* s)
{
    CListCtrl::OnHScroll(n, p, s);
    m_nHotItem = -1;
}
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

void CCustomListCtrl::OnTimer(UINT_PTR nIDEvent)
{
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

void CCustomListCtrl::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
    CListCtrl::OnWindowPosChanged(lpwndpos);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
        ScheduleOpaqueRepaint();
#endif
}

void CCustomListCtrl::UpdateHotItem(int n)
{
    if (m_nHotItem == n) return;
    const int o = m_nHotItem;
    m_nHotItem = n;
    // プレイリスト系(ジャケ/♪): ホット Invalidate → OpaqueFixer 全面描画で名前列がちらつく。
    // キャプション常時アクリル下では部分 MakeOpaque も本文透過になるため使えない。
    // 索引のみ更新し、見た目はスクロール等の全面描画時に合わせる。
    if (m_mpNoteIconGet || m_mpJacketPx > 0)
        return;
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

void CCustomListCtrl::RedrawVisibleItems()
{
    int t = GetTopIndex();
    int b = t + GetCountPerPage();
    int c = GetItemCount();
    if (b >= c) b = c - 1;
    if (t >= 0 && b >= t) RedrawItems(t, b);
}

BOOL CCustomListCtrl::OnEraseBkgnd(CDC*)
{
    return FALSE;
}

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

void CCustomListCtrl::OnPaint()
{
    Default();
    CClientDC dc(this);
    FillEmptyBelowVisible(dc.GetSafeHdc());
    ShowScrollBar(SB_HORZ, FALSE);
}

LRESULT CCustomListCtrl::OnPrintClient(WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(WM_PRINTCLIENT, (WPARAM)wParam, (LPARAM)lParam);
}

// LVS_EX_CHECKBOXES 時、列0の状態イメージ領域へチェックボックスを自前描画する。
// OnCustomDraw が CDRF_SKIPDEFAULT で全描画を奪うため、既定のチェックボックスが
// 描かれずに消えてしまう問題への対処。GDI プリミティブで自己完結描画する。
static void CCC_DrawListCheckBox(CDC* pDC, const CRect& rc, bool checked)
{
    if (!pDC || rc.Width() < 6 || rc.Height() < 6) return;

    // 枠つきの白いボックス
    CBrush brFill(RGB(255, 255, 255));
    CPen penBorder(PS_SOLID, 1, RGB(96, 96, 100));
    CBrush* ob = pDC->SelectObject(&brFill);
    CPen* op = pDC->SelectObject(&penBorder);
    pDC->Rectangle(rc);

    if (checked)
    {
        // チェックマーク(レ点)
        const int w = rc.Width();
        const int h = rc.Height();
        const int penW = (std::max)(2, w / 7);
        CPen penChk(PS_SOLID, penW, RGB(0, 128, 32));
        CPen* op2 = pDC->SelectObject(&penChk);
        POINT pt[3] = {
            { rc.left + w * 22 / 100, rc.top + h * 52 / 100 },
            { rc.left + w * 42 / 100, rc.top + h * 72 / 100 },
            { rc.left + w * 80 / 100, rc.top + h * 26 / 100 },
        };
        pDC->Polyline(pt, 3);
        pDC->SelectObject(op2);
    }

    pDC->SelectObject(ob);
    pDC->SelectObject(op);
}

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
        COLORREF bg = bS ? COLOR_SEL_BG : (ni % 2 == 0 ? COLOR_LIST_BG : RGB(183, 221, 238));
        if (bH && !bS) bg = RGB(220, 235, 250);
        if (!bS && m_mpRowMissGet && m_mpRowMissGet(m_mpJacketCtx, ni))
            bg = RGB(255, 214, 214); // 欠損行: 薄い赤

        // キャプション常時アクリル下、名前列の素 FillRect は α=0→ガラスが一瞬見える。
        // ジャケ有り行は StretchBlt で帯が埋まるが、ジャケ無し行は穴のままちらつく。
        const BOOL bNameJakCol = (ns == 0 && m_mpJacketPx > 0);
#if CCUSTOM_AERO_SUPPORT
        const BOOL bCapGlass = CCC_IsWin11() && (CCC_CaptionOnlyHostGlass(m_hWnd) || CCC_IsAeroEnabled());
#else
        const BOOL bCapGlass = FALSE;
#endif
        const BOOL bLvAero = m_bAeroMode && !CCC_IsBlurDialogChild(m_hWnd);
        if (bNameJakCol && bCapGlass)
        {
#if CCUSTOM_AERO_SUPPORT
            CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), r, bg);
#endif
        }
        else if (bLvAero)
        {
            pDC->FillSolidRect(&r, RGB(0, 0, 0));
            FillRectAlpha(pDC, r, bg, bS ? 180 : bH ? 140 : AERO_ALPHA_SEMI);
        }
        else
            pDC->FillSolidRect(&r, bg);

        // 欠損ヒート: 名前列左に 4px ストライプ
        if (ns == 0 && !bS && m_mpRowMissGet && m_mpRowMissGet(m_mpJacketCtx, ni))
            pDC->FillSolidRect(r.left, r.top, 4, r.Height(), RGB(220, 60, 60));

        if (bS && !bLvAero)
            DrawGlossHighlight(pDC, r, 6);

        // プレイリスト系は m_mpNoteIconGet で実♪を取得(GetDispInfo の iImage は空のまま)。
        // テキスト左余白計算でも同じ値を使う。
        int noteImg = 1;
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
            }
            // 再生アイコン: ImageList と pc[].icon の対応は
            //   0=♪A(IDI_ICON1) / 1=空(IDI_ICON2・透明) / 2=♪B(IDI_ICON3)
            // SIconTimer は 0↔2 で点滅。1 は非再生行。0 をスキップすると片方の♪が消える。
            // ♡ は選択装飾だが ♪ を隠さないよう、♪ の下(奥)に先に描く。
            CRect ri;
            const BOOL hasIconRect = GetItemRect(ni, &ri, LVIR_ICON);
            if (m_mpNoteIconGet)
                noteImg = m_mpNoteIconGet(m_mpJacketCtx, ni);
            else {
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
            const int iw = 16, ih = 16;
            int noteX;
            int noteY = r.top + ((int)r.Height() - ih) / 2;
            if (m_mpJacketPx > 0)
                noteX = jacketRight + 3;
            else if (hasIconRect)
                noteX = ri.left + (ri.Width() - iw) / 2;
            else
                noteX = r.left + 2;
            if (bS)
                DrawHeart(pDC, CRect(noteX, noteY + 2, noteX + 14, noteY + 16), COLOR_HEART);
            if (pIL && noteImg >= 0 && noteImg != 1) {
                // ImageList は行高確保で 24px。♪自体は従来どおり 16x16。
                HICON hNote = ImageList_GetIcon(pIL->GetSafeHandle(), noteImg, ILD_TRANSPARENT);
                if (hNote) {
                    ::DrawIconEx(pDC->GetSafeHdc(), noteX, noteY, hNote, 16, 16, 0, NULL, DI_NORMAL);
                    ::DestroyIcon(hNote);
                }
            }
            // ホバー印: DrawStar(ペン線)はアクリル上で α=0→黒線/透けになるので使わない。
            // 行背景の淡色(上で塗済)だけで十分。左端に不透明の細いアクセントのみ。
            if (bH && !bS) {
#if CCUSTOM_AERO_SUPPORT
                if (bCapGlass)
                    CCC_FillRectOpaqueBits(pDC->GetSafeHdc(),
                        CRect(r.left, r.top, r.left + 3, r.bottom), RGB(90, 150, 220));
                else
#endif
                    pDC->FillSolidRect(r.left, r.top, 3, r.Height(), RGB(90, 150, 220));
            }
        }

        CString st = GetItemText(ni, ns);
        BOOL bSav = FALSE, bLrc = FALSE;
        CCC_ExtractSavLrc(st, bSav, bLrc);
        const BOOL bOpaqueChips = bCapGlass;
        pDC->SetTextColor(m_bAeroMode ? RGB(1, 1, 1) : RGB(0, 0, 0));
        pDC->SetBkMode(TRANSPARENT);

        CRect rt = r;
        if (ns == 0)
        {
            int tl = r.left + 36;
            if (m_mpJacketPx > 0)
                tl = r.left + m_mpJacketPx + 24;
            CRect ri2;
            if (GetItemRect(ni, &ri2, LVIR_ICON))
            {
                if (noteImg >= 0 && noteImg != 1) {
                    if (m_mpJacketPx > 0)
                        tl = (std::max)(tl, (int)r.left + m_mpJacketPx + 3 + 16 + 4);
                    else if (ri2.Width() > 0)
                        tl = (std::max)(tl, (int)ri2.right + 4);
                }
            }
            tl = (std::min)(tl, (int)r.right - 4);
            rt.left = (std::max)(tl, (int)r.left + 4);
            rt.DeflateRect(2, 0);
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
        if (bSav || bLrc) {
            if (st.IsEmpty() && (uColFmt & DT_CENTER)) {
                // 印列のみ: チップを中央寄せ
                const int chipsW = CCC_MeasureSavLrcChips(pDC, bSav, bLrc);
                int x = r.left + (r.Width() - chipsW) / 2;
                if (x < r.left + 2) x = r.left + 2;
                CCC_DrawSavLrcChips(pDC, x, midY, bSav, bLrc, bOpaqueChips);
            } else {
                // 名前列: チップ → 曲名
                int x = rt.left;
                x = CCC_DrawSavLrcChips(pDC, x, midY, bSav, bLrc, bOpaqueChips);
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

CCustomTreeCtrl::CCustomTreeCtrl()
	: m_bAutoDelete(FALSE), m_hHotItem(NULL), m_nItemDrawIndex(0)
	, m_clrBk(COLOR_LIST_BG), m_bAeroMode(FALSE)
{
	m_brBackground.CreateSolidBrush(COLOR_LIST_BG);
}

CCustomTreeCtrl::~CCustomTreeCtrl()
{
	if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

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

void CCustomTreeCtrl::PostNcDestroy()
{
	CTreeCtrl::PostNcDestroy();
	if (m_bAutoDelete) delete this;
}

void CCustomTreeCtrl::PreSubclassWindow()
{
	CTreeCtrl::PreSubclassWindow();
	SetBkColor(COLOR_LIST_BG);
	DWORD dwEx = (DWORD)SendMessage(TVM_GETEXTENDEDSTYLE, 0, 0);
	SendMessage(TVM_SETEXTENDEDSTYLE, TVS_EX_DOUBLEBUFFER | TVS_EX_FULLROWSELECT,
		dwEx | TVS_EX_DOUBLEBUFFER | TVS_EX_FULLROWSELECT);
}

BOOL CCustomTreeCtrl::OnEraseBkgnd(CDC*) { return FALSE; }

void CCustomTreeCtrl::ScheduleOpaqueRepaint()
{
	if (GetSafeHwnd())
		PostMessage(CCC_WM_POST_OPAQUE_PAINT);
}

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

LRESULT CCustomTreeCtrl::OnPrintClient(WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(WM_PRINTCLIENT, wParam, lParam);
}

void CCustomTreeCtrl::PaintOpaqueIntoBuffer(HDC hdcBuf)
{
	if (!hdcBuf || !m_hWnd) return;
	CRect r;
	GetClientRect(&r);
	if (r.Width() <= 0 || r.Height() <= 0) return;
	::FillRect(hdcBuf, &r, (HBRUSH)m_brBackground.GetSafeHandle());
	::SendMessage(m_hWnd, WM_PRINTCLIENT, (WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
}

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

void CCustomTreeCtrl::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	CTreeCtrl::OnWindowPosChanged(lpwndpos);
#if CCUSTOM_AERO_SUPPORT
	if (CCC_HostNeedsChildOpaque(m_hWnd))
		ScheduleOpaqueRepaint();
#endif
}

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

void CCustomTreeCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	UINT      uFlags = 0;
	HTREEITEM hItem = HitTest(point, &uFlags);

	if (m_hHotItem != hItem)
	{
		HTREEITEM hOld = m_hHotItem;
		m_hHotItem = hItem;
		InvalidateItemRow(hOld);
		InvalidateItemRow(m_hHotItem);
		UpdateWindow();
	}

	TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
	TrackMouseEvent(&tme);

	CTreeCtrl::OnMouseMove(nFlags, point);
}

void CCustomTreeCtrl::OnMouseLeave()
{
	if (m_hHotItem)
	{
		HTREEITEM hOld = m_hHotItem;
		m_hHotItem = NULL;
		InvalidateItemRow(hOld);
		UpdateWindow();
	}
}

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
		GetItemRect(hItem, &rcRow, FALSE);
		CRect rcClient;
		GetClientRect(&rcClient);
		rcRow.left = rcClient.left;
		rcRow.right = rcClient.right;

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

		if (bSel && !strText.IsEmpty())
		{
			CFont* pFontMeasure = pDC->SelectObject(GetFont());
			CSize  szT = pDC->GetTextExtent(strText);
			pDC->SelectObject(pFontMeasure);

			CRect rcLbl(rcText.left - 2, rcRow.top + 1,
				(std::min)(rcText.left + szT.cx + 4, rcText.right),
				rcRow.bottom - 1);
#if CCUSTOM_AERO_SUPPORT
			if (CCC_HostNeedsChildOpaque(m_hWnd))
				CCC_FillRectOpaqueBits(pDC->GetSafeHdc(), rcLbl, RGB(200, 170, 235));
			else
#endif
				pDC->FillSolidRect(&rcLbl, RGB(200, 170, 235));
		}

		pDC->SetBkMode(TRANSPARENT);
		pDC->SetTextColor(RGB(0, 0, 0));
		{
			CFont* pOldFont = pDC->SelectObject(GetFont());
			pDC->DrawText(strText, &rcText,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			pDC->SelectObject(pOldFont);
		}

		if (bSel)
		{
			CRect rcH(rcRow.left + 2, nCenterY - 7, rcRow.left + 16, nCenterY + 7);
			DrawHeart(pDC, rcH, COLOR_HEART);
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

CCustomTabCtrl::CCustomTabCtrl()
	: m_bAutoDelete(FALSE), m_bAeroMode(FALSE), m_nHotItem(-1), m_bTracking(FALSE)
{
	m_brBackground.CreateSolidBrush(COLOR_DIALOG_BG);
}

CCustomTabCtrl::~CCustomTabCtrl()
{
	if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
	if (m_fontTab.GetSafeHandle()) m_fontTab.DeleteObject();
	if (m_fontTabSel.GetSafeHandle()) m_fontTabSel.DeleteObject();
}

void CCustomTabCtrl::PostNcDestroy()
{
	CTabCtrl::PostNcDestroy();
	if (m_bAutoDelete) delete this;
}

BOOL CCustomTabCtrl::IsVertical() const
{
	if (!::IsWindow(m_hWnd)) return FALSE;
	return (::GetWindowLong(m_hWnd, GWL_STYLE) & TCS_VERTICAL) ? TRUE : FALSE;
}

BOOL CCustomTabCtrl::IsRightSide() const
{
	if (!::IsWindow(m_hWnd)) return FALSE;
	return (::GetWindowLong(m_hWnd, GWL_STYLE) & TCS_RIGHT) ? TRUE : FALSE;
}

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

void CCustomTabCtrl::LayoutEqualTabs(int nSlots)
{
	if (!::IsWindow(m_hWnd) || nSlots < 1) return;
	ModifyStyle(TCS_MULTILINE, TCS_FIXEDWIDTH);

	CRect rc;
	GetClientRect(&rc);
	if (rc.Height() < 8 || rc.Width() < 8) return;

	if (IsVertical()) {
		const int usable = max(24, rc.Height() - 6);
		const int tabAlong = max(20, usable / nSlots);
		SetItemSize(CSize(tabAlong, 40));
	}
	else {
		const int usable = max(48, rc.Width() - 6);
		const int tabW = max(48, usable / nSlots);
		SetItemSize(CSize(tabW, 26));
	}
	Invalidate(FALSE);
}

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

void CCustomTabCtrl::ScheduleOpaqueRepaint()
{
	if (GetSafeHwnd())
		PostMessage(CCC_WM_POST_OPAQUE_PAINT);
}

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

LRESULT CCustomTabCtrl::OnPrintClient(WPARAM wParam, LPARAM lParam)
{
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
	if (bSelected && !IsVertical()) DrawGlossHighlight(pDC, rc, 6);
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

void CCustomTabCtrl::OnSize(UINT nType, int cx, int cy)
{
	CTabCtrl::OnSize(nType, cx, cy);
	const int n = GetItemCount();
	if (n > 0) LayoutEqualTabs(n);
	Invalidate(FALSE);
}

void CCustomTabCtrl::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	CTabCtrl::OnWindowPosChanged(lpwndpos);
#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled() && CCC_IsWin11())
		ScheduleOpaqueRepaint();
#endif
}

void CCustomTabCtrl::OnTimer(UINT_PTR nIDEvent)
{
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

void CCustomTabCtrl::InvalidateTabItem(int nItem)
{
	if (nItem < 0 || !::IsWindow(m_hWnd)) return;
	CRect rc;
	if (!GetItemRect(nItem, &rc)) return;
	rc.InflateRect(2, 2);
	InvalidateRect(&rc, FALSE);
}

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

void CCustomTabCtrl::OnMouseLeave()
{
	m_bTracking = FALSE;
	if (m_nHotItem != -1) {
		InvalidateTabItem(m_nHotItem);
		m_nHotItem = -1;
	}
	CTabCtrl::OnMouseLeave();
}

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

CCustomStandardButton::CCustomStandardButton()
    : m_bAutoDelete(FALSE), m_bMouseOver(FALSE), m_nAnimTick(0), m_bAnimRunning(FALSE),
    m_clrGradStart(RGB(255, 255, 255)),
    m_clrGradEnd(RGB(255, 255, 255)), m_nGradDirection(0), m_bGradEnable(FALSE),
    m_clrShadow(RGB(0, 0, 0)), m_nShadowDirection(135), m_nShadowDistance(2),
    m_nShadowBlur(3), m_bShadowEnable(FALSE),
    m_hIconIn(NULL), m_hIconOut(NULL), m_bFlat(FALSE), m_bAeroMode(FALSE),
    m_bIconOwnedIn(FALSE), m_bIconOwnedOut(FALSE)
{
    m_brBackground.CreateSolidBrush(COLOR_BUTTON_BG);
}

void CCustomStandardButton::EnsureAnimTimer()
{
    UpdateAnimTimer();
}

void CCustomStandardButton::UpdateAnimTimer()
{
    if (!GetSafeHwnd()) return;
    const BOOL bWant = IsWindowEnabled() && (m_bMouseOver || (GetFocus() == this));
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

void CCustomStandardButton::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kButtonAnimTimerId)
    {
        m_nAnimTick++;
        Invalidate(FALSE);
        return;
    }
    CButton::OnTimer(nIDEvent);
}

CCustomStandardButton::~CCustomStandardButton()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
    if (m_bIconOwnedIn && m_hIconIn) { ::DestroyIcon(m_hIconIn); m_hIconIn = NULL; }
    if (m_bIconOwnedOut && m_hIconOut) { ::DestroyIcon(m_hIconOut); m_hIconOut = NULL; }
}

void CCustomStandardButton::PostNcDestroy()
{
    CButton::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

void CCustomStandardButton::SetGradation(COLORREF s, COLORREF e, int d, BOOL en)
{
    m_clrGradStart = s;
    m_clrGradEnd = e;
    m_nGradDirection = d % 360;
    if (m_nGradDirection < 0) m_nGradDirection += 360;
    m_bGradEnable = en;
    if (GetSafeHwnd()) Invalidate(FALSE);
}

void CCustomStandardButton::GetGradation(COLORREF* ps, COLORREF* pe, int* pd, BOOL* pbe) const
{
    if (ps) *ps = m_clrGradStart;
    if (pe) *pe = m_clrGradEnd;
    if (pd) *pd = m_nGradDirection;
    if (pbe) *pbe = m_bGradEnable;
}

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

void CCustomStandardButton::GetDropShadow(COLORREF* pc, int* pd, int* pdist, int* pblur, BOOL* pbe) const
{
    if (pc) *pc = m_clrShadow;
    if (pd) *pd = m_nShadowDirection;
    if (pdist) *pdist = m_nShadowDistance;
    if (pblur) *pblur = m_nShadowBlur;
    if (pbe) *pbe = m_bShadowEnable;
}

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
    if (GetSafeHwnd()) Invalidate(FALSE);
    return 0;
}

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
    if (GetSafeHwnd()) Invalidate(FALSE);
    return 0;
}

void CCustomStandardButton::SetFlat(BOOL bFlat)
{
    m_bFlat = bFlat;
    if (GetSafeHwnd()) Invalidate(FALSE);
}

void CCustomStandardButton::SetAeroMode(BOOL b)
{
    m_bAeroMode = b ? TRUE : FALSE;
    if (GetSafeHwnd()) {
        Invalidate(FALSE);
        CCC_InvalidateParent(m_hWnd, m_bAeroMode);
    }
}

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
}

HBRUSH CCustomStandardButton::CtlColor(CDC*, UINT)
{
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

void CCustomStandardButton::PaintClient(CDC& dc, const CRect& r)
{
    if (r.Width() <= 0 || r.Height() <= 0) return;
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

        // 大きめのボタンは裾に透けレースの色気を(アイコン平坦ボタンは省略=はみ出し防止)
        if (!m_bFlat && !m_hIconIn && r.Width() >= 64 && r.Height() >= 26)
            DrawLaceScallop(&mDC, r.left + 8, r.bottom - 6, r.right - 8, 3, COLOR_LACE);

        // ホバー時: とろみハイライトがスーッと流れる(押下トグル上でもホバー中は表示)
        if (bShowFlow && !m_bFlat)
        {
            const int W = r.Width();
            const int bandW = max(10, W / 4);
            const int period = W + bandW + W / 2;
            const int pos = (int)((m_nAnimTick * 7) % (UINT)max(1, period));
            CRect band(r.left + pos - bandW, r.top, r.left + pos, r.bottom);
            CRgn rgn; rgn.CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, 16, 16);
            mDC.SelectClipRgn(&rgn);
            FillRectAlpha(&mDC, band, RGB(255, 255, 255), bP ? 48 : 72);
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
                // ほどけかけリボン + きらめきで色っぽく
                DrawLooseRibbon(&mDC, CRect(r.Width() / 2 - 11, r.top + 2, r.Width() / 2 + 11, r.top + 16), COLOR_BOW);
                DrawSparkle(&mDC, r.right - 10, r.top + 10, 4, COLOR_SPARKLE);
                DrawSparkle(&mDC, r.left + 12, r.bottom - 10, 3, COLOR_SPARKLE);
            }
            if (bP)
            {
                DrawSparkle(&mDC, r.Width() / 2, r.top + 8, 4, COLOR_SPARKLE);
                DrawStar(&mDC, r.left + 15, r.Height() / 2, 2, RGB(255, 240, 150));
                DrawStar(&mDC, r.right - 15, r.Height() / 2, 2, RGB(255, 240, 150));
                DrawLooseRibbon(&mDC, CRect(r.Width() / 2 - 10, r.bottom - 15, r.Width() / 2 + 10, r.bottom - 2), COLOR_BOW);
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

    // アイコン(中央)。ホバー時は Out があれば差し替え。押下で1pxずらす。
    HICON hDraw = m_hIconIn;
    if (m_bMouseOver && m_hIconOut)
        hDraw = m_hIconOut;
    if (hDraw)
    {
        ICONINFO ii = {};
        int iw = 16, ih = 16;
        if (::GetIconInfo(hDraw, &ii))
        {
            BITMAP bm = {};
            if (ii.hbmColor && ::GetObject(ii.hbmColor, sizeof(bm), &bm))
            {
                iw = bm.bmWidth;
                ih = bm.bmHeight;
            }
            else if (ii.hbmMask && ::GetObject(ii.hbmMask, sizeof(bm), &bm))
            {
                iw = bm.bmWidth;
                ih = bm.bmHeight / 2;
            }
            if (ii.hbmColor) ::DeleteObject(ii.hbmColor);
            if (ii.hbmMask) ::DeleteObject(ii.hbmMask);
        }
        // ボタン内に収める(はみ出し防止)
        const int maxSz = (std::max)(8, (std::min)(r.Width(), r.Height()) - (m_bFlat ? 4 : 8));
        if (iw > maxSz) { ih = MulDiv(ih, maxSz, iw); iw = maxSz; }
        if (ih > maxSz) { iw = MulDiv(iw, maxSz, ih); ih = maxSz; }
        int ix = r.left + (r.Width() - iw) / 2;
        int iy = r.top + (r.Height() - ih) / 2;
        if (bP) { ix += 1; iy += 1; }
        ::DrawIconEx(mDC.GetSafeHdc(), ix, iy, hDraw, iw, ih, 0, NULL, DI_NORMAL);
    }

    CString s;
    GetWindowText(s);
    // アイコン専用ボタンは空キャプション想定。文字がある場合だけ描く(はみ出し注意)。
    if (!s.IsEmpty() && !hDraw)
    {
        CFont* pF = GetFont();
        CFont* pOF = mDC.SelectObject(pF ? pF : (CFont*)mDC.SelectStockObject(DEFAULT_GUI_FONT));
        DrawSmartText(&mDC, r, s, bD, bP);
        mDC.SelectObject(pOF);
    }
    else if (!s.IsEmpty() && hDraw)
    {
        // アイコン+文字: アイコン左、文字右(狭いときは省略)
        CFont* pF = GetFont();
        CFont* pOF = mDC.SelectObject(pF ? pF : (CFont*)mDC.SelectStockObject(DEFAULT_GUI_FONT));
        CRect tr = r;
        tr.left += (std::min)(r.Width() / 2, 22);
        tr.DeflateRect(2, 1);
        mDC.SetBkMode(TRANSPARENT);
        mDC.SetTextColor(bD ? RGB(120, 120, 120) : RGB(0, 0, 0));
        mDC.DrawText(s, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        mDC.SelectObject(pOF);
    }

    if (!bD) CCC_DrawInwoman(&mDC, r, bAeroTrans); // 淫女モード演出

#if CCUSTOM_AERO_SUPPORT
    if (bAeroTrans) {
        CCC_BlitChromaTrans(dc.GetSafeHdc(), 0, 0, r.Width(), r.Height(),
            mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    } else
#endif
    {
        dc.BitBlt(0, 0, r.Width(), r.Height(), &mDC, 0, 0, SRCCOPY);
    }
    mDC.SelectObject(ob);
    mB.DeleteObject();
    mDC.DeleteDC();
}

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

void CCustomStandardButton::OnMouseMove(UINT f, CPoint p)
{
    if (!m_bMouseOver)
    {
        TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&t);
        m_bMouseOver = TRUE;
        UpdateAnimTimer();
        Invalidate(FALSE);
        CCC_InvalidateParent(m_hWnd, m_bAeroMode);
    }
    CButton::OnMouseMove(f, p);
}

LRESULT CCustomStandardButton::OnMouseLeave(WPARAM, LPARAM)
{
    m_bMouseOver = FALSE;
    UpdateAnimTimer();
    Invalidate(FALSE);
    CCC_InvalidateParent(m_hWnd, m_bAeroMode);
    return 0;
}

void CCustomStandardButton::OnSetFocus(CWnd* p)
{
    CButton::OnSetFocus(p);
    UpdateAnimTimer();
    Invalidate(FALSE);
}

void CCustomStandardButton::OnKillFocus(CWnd* p)
{
    CButton::OnKillFocus(p);
    UpdateAnimTimer();
    Invalidate(FALSE);
}

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

CCustomCheckBox::CCustomCheckBox()
    : m_bAutoDelete(FALSE), m_bIsFlatStyle(FALSE), m_bIsPressed(FALSE),
    m_bIsHot(FALSE), m_bTracking(FALSE), m_nCheck(0), m_bAeroMode(FALSE), m_nBounce(0)
{}

void CCustomCheckBox::StartCheckBounce()
{
    if (!GetSafeHwnd()) return;
    m_nBounce = 8;
    SetTimer(kCheckBounceTimerId, 28, NULL);
    Invalidate();
}

void CCustomCheckBox::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kCheckBounceTimerId)
    {
        if (--m_nBounce <= 0) { m_nBounce = 0; KillTimer(kCheckBounceTimerId); }
        Invalidate();
        return;
    }
    CButton::OnTimer(nIDEvent);
}

CCustomCheckBox::~CCustomCheckBox() {}

void CCustomCheckBox::PostNcDestroy()
{
    CButton::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

int CCustomCheckBox::GetCheck()
{
    return m_nCheck;
}

void CCustomCheckBox::SetCheck(int n)
{
    m_nCheck = n;
    CButton::SetCheck(n);
    if (n == BST_CHECKED) StartCheckBounce();
    Invalidate();
}

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

void CCustomCheckBox::SetFont(CFont* p, BOOL b)
{
    CButton::SetFont(p, b);
}

void CCustomCheckBox::OnLButtonDown(UINT n, CPoint p)
{
    m_bIsPressed = m_bIsHot = TRUE;
    SetCapture();
    Invalidate();
}

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
        Invalidate();
    }
}

void CCustomCheckBox::OnMouseLeave()
{
    m_bIsHot = m_bTracking = FALSE;
    Invalidate();
}

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
    dc.BitBlt(0, 0, r.Width(), r.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(ob);
}

LRESULT CCustomCheckBox::OnPrintClient(WPARAM w, LPARAM)
{
    CDC* pDC = CDC::FromHandle((HDC)w);
    CRect r;
    GetClientRect(&r);
    OnDrawLayer(pDC, r);
    return 0;
}

BOOL CCustomCheckBox::OnEraseBkgnd(CDC*)
{
    return TRUE;
}

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
                    dc.SetBkMode(TRANSPARENT);
                    dc.SetTextColor(bD ? RGB(180, 180, 180) : RGB(255, 255, 255));
                    dc.DrawText(t, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                }
                else {
                    DrawSmartText2(&dc, rt, t, DT_LEFT | DT_VCENTER | DT_SINGLELINE, bD, FALSE);
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
                    const double bf = sin(3.14159265 * (8 - m_nBounce) / 8.0);
                    rk.InflateRect((int)(rk.Width() * 0.20 * bf), (int)(rk.Height() * 0.20 * bf));
                }
                // 自分のクライアント矩形内に収める(隣のコントロールにはみ出さない=切れ防止)
                if (rk.left < 0)     rk.left = 0;
                if (rk.top < 0)      rk.top = 0;
                if (rk.right > rw)   rk.right = rw;
                if (rk.bottom > rh)  rk.bottom = rh;

                DrawCheckMark(&dc, rk, bD ? CCC_Desaturate(COLOR_CHECK, 62) : COLOR_CHECK, max(3, s / 4));
                DrawSparkle(&dc, rk.left + 2, rk.top + 1, 2, COLOR_SPARKLE);
                if (rk.top >= 6)
                {
                    int bL = max(0, rcB.right - 10);
                    DrawLooseRibbon(&dc, CRect(bL, rcB.top - 6, min(rw, bL + 14), rcB.top + 4), COLOR_BOW);
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
// カスタムグループボックスコントロール
// ============================================================================
// ============================================================================
// CCustomProgressCtrl
// ============================================================================
// ============================================================================
// CCustomLevelMeter
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomLevelMeter, CStatic)

BEGIN_MESSAGE_MAP(CCustomLevelMeter, CStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
END_MESSAGE_MAP()

CCustomLevelMeter::CCustomLevelMeter()
	: m_bAutoDelete(FALSE), m_level(0), m_bAeroMode(FALSE)
{
}

CCustomLevelMeter::~CCustomLevelMeter()
{
}

void CCustomLevelMeter::PostNcDestroy()
{
	CStatic::PostNcDestroy();
	if (m_bAutoDelete) delete this;
}

void CCustomLevelMeter::SetLevel(int n)
{
	if (n < 0) n = 0;
	if (n > 1000) n = 1000;
	if (n == m_level) return;
	m_level = n;
	if (GetSafeHwnd())
		Invalidate(FALSE);
}

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

void CCustomLevelMeter::OnPaint()
{
	CPaintDC dc(this);
	PaintClient(dc);
}

BOOL CCustomLevelMeter::OnEraseBkgnd(CDC* pDC)
{
	UNREFERENCED_PARAMETER(pDC);
	return TRUE;
}

LRESULT CCustomLevelMeter::OnPrintClient(WPARAM wParam, LPARAM)
{
	if (HDC hDC = (HDC)wParam) {
		CDC* pDC = CDC::FromHandle(hDC);
		if (pDC) PaintClient(*pDC);
		return 1;
	}
	return 0;
}

IMPLEMENT_DYNAMIC(CCustomProgressCtrl, CWnd)

BEGIN_MESSAGE_MAP(CCustomProgressCtrl, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
END_MESSAGE_MAP()

namespace {
COLORREF ProgLerp(COLORREF a, COLORREF b, double t)
{
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	return RGB(
		(int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t + 0.5),
		(int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t + 0.5),
		(int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t + 0.5));
}

COLORREF ProgLighten(COLORREF c, int add)
{
	return RGB(
		(std::min)(255, GetRValue(c) + add),
		(std::min)(255, GetGValue(c) + add),
		(std::min)(255, GetBValue(c) + add));
}

COLORREF ProgDarken(COLORREF c, int sub)
{
	return RGB(
		(std::max)(0, GetRValue(c) - sub),
		(std::max)(0, GetGValue(c) - sub),
		(std::max)(0, GetBValue(c) - sub));
}
} // namespace

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

BOOL CCustomProgressCtrl::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID)
{
	// CS_HREDRAW|VREDRAW は部分更新時に重ね描きしやすいので外す
	CString cls = AfxRegisterWndClass(CS_DBLCLKS,
		::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_BTNFACE + 1), NULL);
	return CWnd::Create(cls, _T(""), dwStyle | WS_CHILD, rect, pParentWnd, nID);
}

void CCustomProgressCtrl::PostNcDestroy()
{
	CWnd::PostNcDestroy();
	if (m_bAutoDelete) delete this;
}

void CCustomProgressCtrl::SetRange(int nLower, int nUpper)
{
	if (nUpper < nLower) { int t = nLower; nLower = nUpper; nUpper = t; }
	m_nMin = nLower;
	m_nMax = nUpper;
	if (m_nPos < m_nMin) m_nPos = m_nMin;
	if (m_nPos > m_nMax) m_nPos = m_nMax;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

void CCustomProgressCtrl::GetRange(int& nLower, int& nUpper) const
{
	nLower = m_nMin;
	nUpper = m_nMax;
}

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

void CCustomProgressCtrl::SetColors(COLORREF track, COLORREF fillStart, COLORREF fillEnd)
{
	m_clrTrack = track;
	m_clrFill0 = fillStart;
	m_clrFill1 = fillEnd;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

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

void CCustomProgressCtrl::PaintClient(CDC& dc)
{
	CRect r;
	GetClientRect(&r);
	PaintClient(dc, r);
}

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

CCustomSysPerfCtrl::~CCustomSysPerfCtrl()
{
}

BOOL CCustomSysPerfCtrl::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID)
{
	CString cls = AfxRegisterWndClass(CS_DBLCLKS,
		::LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_BTNFACE + 1), NULL);
	return CWnd::Create(cls, _T(""), dwStyle | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, rect, pParentWnd, nID);
}

void CCustomSysPerfCtrl::PostNcDestroy()
{
	CWnd::PostNcDestroy();
	if (m_bAutoDelete) delete this;
}

UINT CCustomSysPerfCtrl::Dpi() const
{
	return CCC_GetControlDpi(m_hWnd);
}

int CCustomSysPerfCtrl::S(int v) const
{
	return CCC_ScaleDpi(v, Dpi());
}

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

void CCustomSysPerfCtrl::SetViewMode(int mode)
{
	if (mode < VIEW_ALL) mode = VIEW_ALL;
	if (mode > VIEW_CPU_BOTH) mode = VIEW_CPU_BOTH;
	if (m_viewMode == mode) return;
	m_viewMode = mode;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

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

BOOL CCustomSysPerfCtrl::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd())
		m_tip.RelayEvent(pMsg);
	return CWnd::PreTranslateMessage(pMsg);
}

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

void CCustomSysPerfCtrl::SampleSmbiosOnce()
{
	if (m_bSmbiosDone) return;
	m_bSmbiosDone = TRUE;
	m_memSpeedMTs = 0;
	m_memSlotsUsed = 0;
	m_memSlotsTotal = 0;
	m_memFormFactor = 0;

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

void CCustomSysPerfCtrl::SampleOnce()
{
	BOOL wroteSample = FALSE;

	// --- overall CPU ---
	FILETIME idle = {}, ker = {}, usr = {};
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

void CCustomSysPerfCtrl::DrawSpark(CDC& dc, const CRect& rc, const BYTE* hist, int histCount, BOOL bAeroTrans)
{
	if (rc.Width() < 4 || rc.Height() < 4 || !hist || histCount <= 0) return;
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

	POINT pts[kHistLen + 2];
	const int n = histCount > kHistLen ? kHistLen : histCount;
	for (int i = 0; i < n; ++i) {
		int idx = m_histPos - n + i;
		while (idx < 0) idx += kHistLen;
		idx %= kHistLen;
		const int x = rc.left + (i * (rc.Width() - 1)) / (n > 1 ? (n - 1) : 1);
		const int y = rc.bottom - 1 - (hist[idx] * (rc.Height() - 1)) / 100;
		pts[i].x = x;
		pts[i].y = y;
	}
	if (n >= 2) {
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

void CCustomSysPerfCtrl::DrawPerfLayer(CDC& dc, const CRect& r, BOOL bAeroTrans)
{
	if (r.Width() <= 0 || r.Height() <= 0) return;
	CRect rcMem, rcOverall, rcGrid;
	LayoutRects(r, rcMem, rcOverall, rcGrid);
	DrawMemory(dc, rcMem, bAeroTrans);
	DrawOverallCpu(dc, rcOverall, bAeroTrans);
	DrawCoreGrid(dc, rcGrid, bAeroTrans);
	CCC_DrawInwoman(&dc, r, bAeroTrans);
}

void CCustomSysPerfCtrl::PaintClient(CDC& dc)
{
	CRect r;
	GetClientRect(&r);
	PaintClient(dc, r);
}

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

void CCustomSysPerfCtrl::ShowCtxMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	menu.AddCheck(kCmdViewAll,
		LL14(L"すべて表示", L"Show all", L"Tout afficher", L"Mostra tutto", L"Mostrar todo", L"모두 표시", L"全部显示", L"عرض الكل", L"Показать все", L"Alles anzeigen", L"Mostrar tudo", L"Alles tonen", L"Pokaz wszystko", L"Tumunu goster"),
		m_viewMode == VIEW_ALL);
	menu.AddCheck(kCmdViewMem,
		LL14(L"メモリのみ", L"Memory only", L"Memoire seule", L"Solo memoria", L"Solo memoria", L"메모리만", L"仅内存", L"الذاكرة فقط", L"Только память", L"Nur Speicher", L"Somente memoria", L"Alleen geheugen", L"Tylko pamiec", L"Yalniz bellek"),
		m_viewMode == VIEW_MEM);
	menu.AddCheck(kCmdViewCpuOverall,
		LL14(L"CPU 全体のみ", L"CPU overall only", L"CPU global seul", L"Solo CPU totale", L"Solo CPU total", L"CPU 전체만", L"仅 CPU 总体", L"وحدة المعالجة فقط", L"Только CPU общий", L"Nur CPU gesamt", L"Somente CPU geral", L"Alleen CPU totaal", L"Tylko CPU ogolne", L"Yalniz CPU genel"),
		m_viewMode == VIEW_CPU_OVERALL);
	menu.AddCheck(kCmdViewCpuGrid,
		LL14(L"CPU グリッドのみ", L"CPU grid only", L"Grille CPU seule", L"Solo griglia CPU", L"Solo cuadrícula CPU", L"CPU 그리드만", L"仅 CPU 网格", L"شبكة المعالج فقط", L"Только сетка CPU", L"Nur CPU-Raster", L"Somente grade CPU", L"Alleen CPU-raster", L"Tylko siatka CPU", L"Yalniz CPU izgarasi"),
		m_viewMode == VIEW_CPU_GRID);
	menu.AddCheck(kCmdViewCpuBoth,
		LL14(L"CPU のみ (全体+グリッド)", L"CPU only (overall+grid)", L"CPU seul (global+grille)", L"Solo CPU (totale+griglia)", L"Solo CPU (total+cuadrícula)", L"CPU만 (전체+그리드)", L"仅 CPU（总体+网格）", L"المعالج فقط (كلي+شبكة)", L"Только CPU (общий+сетка)", L"Nur CPU (gesamt+Raster)", L"Somente CPU (geral+grade)", L"Alleen CPU (totaal+raster)", L"Tylko CPU (ogolne+siatka)", L"Yalniz CPU (genel+izgara)"),
		m_viewMode == VIEW_CPU_BOTH);
	menu.AddSeparator();
	menu.AddCheck(kCmdPause,
		m_bPaused
		? LL14(L"更新を再開", L"Resume updates", L"Reprendre", L"Riprendi", L"Reanudar", L"업데이트 재개", L"恢复更新", L"استئناف التحديث", L"Возобновить", L"Fortsetzen", L"Retomar", L"Hervatten", L"Wznow", L"Devam et")
		: LL14(L"更新を一時停止", L"Pause updates", L"Pause", L"Pausa", L"Pausar", L"업데이트 일시정지", L"暂停更新", L"إيقاف مؤقت", L"Пауза", L"Pausieren", L"Pausar", L"Pauzeren", L"Wstrzymaj", L"Duraklat"),
		m_bPaused);
	menu.AddCommand(kCmdCopy,
		LL14(L"統計をコピー", L"Copy statistics", L"Copier les stats", L"Copia statistiche", L"Copiar estadisticas", L"통계 복사", L"复制统计", L"نسخ الإحصاءات", L"Копировать статистику", L"Statistik kopieren", L"Copiar estatisticas", L"Statistieken kopieren", L"Kopiuj statystyki", L"Istatistikleri kopyala"));
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

void CCustomSysPerfCtrl::OnRButtonUp(UINT nFlags, CPoint point)
{
	UNREFERENCED_PARAMETER(nFlags);
	CPoint pt = point;
	ClientToScreen(&pt);
	ShowCtxMenu(pt);
}

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
IMPLEMENT_DYNAMIC(CCustomGroupBox, CButton)

BEGIN_MESSAGE_MAP(CCustomGroupBox, CButton)
    ON_WM_PAINT()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCustomGroupBox::CCustomGroupBox() : m_bAutoDelete(FALSE), m_bAeroMode(FALSE) {}
CCustomGroupBox::~CCustomGroupBox() {}

void CCustomGroupBox::PostNcDestroy()
{
    CButton::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

void CCustomGroupBox::PreSubclassWindow()
{
    CButton::PreSubclassWindow();
    // グループは兄弟(Edit/Static)の下に回り、かつ兄弟領域へ描画しない
    ModifyStyle(0, WS_CLIPSIBLINGS);
}
void CCustomGroupBox::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
    DrawGroupBox(&dc, r);
}

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

BOOL CCustomGroupBox::OnEraseBkgnd(CDC* pDC)
{
    // 内側全面塗りは兄弟コントロールを消す。消去は親/子に任せる。
    UNREFERENCED_PARAMETER(pDC);
    return TRUE;
}

// グループボックス矩形と重なる兄弟をクリップ除外(内側塗りつぶし防止)
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

CCustomDialog::CCustomDialog() : m_bAeroEnabled(FALSE)
{
    m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
    m_brNull.CreateStockObject(NULL_BRUSH);
}

CCustomDialog::CCustomDialog(UINT n, CWnd* p) : CDialogEx(n, p), m_bAeroEnabled(FALSE)
{
    m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
    m_brNull.CreateStockObject(NULL_BRUSH);
}

CCustomDialog::~CCustomDialog()
{
    if (m_brDialog.GetSafeHandle()) m_brDialog.DeleteObject();
    if (m_brNull.GetSafeHandle()) m_brNull.DeleteObject();
}

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

BOOL CCustomDialog::OnInitDialog()
{
    BOOL r = CDialogEx::OnInitDialog();
    SubclassChildControls();
    return r;
}

LRESULT CCustomDialog::OnSubclassControls(WPARAM, LPARAM)
{
    SubclassChildControls();
    return 0;
}

void CCustomDialog::SubclassChildControls()
{
    DoSubclassChildControls(this);
}

HBRUSH CCustomDialog::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nC)
{
    HBRUSH h = DlgOnCtlColor(pDC, pWnd, nC, m_brDialog, m_bAeroEnabled);
    return h ? h : CDialogEx::OnCtlColor(pDC, pWnd, nC);
}

BOOL CCustomDialog::OnEraseBkgnd(CDC* pDC)
{
    return DlgOnEraseBkgnd(pDC, m_brDialog, m_bAeroEnabled, m_hWnd);
}

void CCustomDialog::OnPaint()
{
    if (m_bAeroEnabled)
        DlgOnPaintAero(this, m_bAeroEnabled);
    else
        CDialogEx::OnPaint();
}

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

static BOOL CCC_BtnSTNeedsChroma(HWND hWnd)
{
    return CCC_IsBlurDialogChild(hWnd) && CCC_IsAeroEnabled() && CCC_IsWin11();
}

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
class CCustomOpaqueFixer
{
public:
    CCustomOpaqueFixer(COLORREF clrBg, BOOL bChroma = FALSE) : m_hWnd(NULL), m_bPrinting(FALSE), m_clrBg(clrBg), m_bChroma(bChroma) {}
    ~CCustomOpaqueFixer() { Uninstall(); }

    BOOL Install(HWND hWnd)
    {
        if (m_hWnd) return FALSE;
        if (!::IsWindow(hWnd)) return FALSE;
        m_hWnd = hWnd;
        return ::SetWindowSubclass(hWnd, SubclassProc, (UINT_PTR)this, (DWORD_PTR)this);
    }

    void Uninstall()
    {
        if (m_hWnd && ::IsWindow(m_hWnd))
            ::RemoveWindowSubclass(m_hWnd, SubclassProc, (UINT_PTR)this);
        m_hWnd = NULL;
    }

private:
    HWND m_hWnd;
    BOOL m_bPrinting;
    COLORREF m_clrBg;
    BOOL m_bChroma;

    static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        CCustomOpaqueFixer* pThis = (CCustomOpaqueFixer*)dwRefData;
        switch (uMsg)
        {
        case WM_ERASEBKGND:
            // 空返しだと α=0 のまま残り完全透過になる（ホバーで WM_PAINT すると戻る）
            if (!pThis->m_bPrinting) {
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
            // 非クライアント(スクロールバー/枠)は既定描画だとアクリル(ガラス)上で
            // アルファ0になり透過して見えなくなる。既定描画後にウィンドウ全体の
            // アルファを不透明化して、スクロールバーを確実に表示させる。
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            pThis->MakeWindowOpaque(hWnd);
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
        case WM_IME_COMPOSITION:
        {
            wchar_t cls[32];
            cls[0] = 0;
            ::GetClassNameW(hWnd, cls, 32);
            if (::_wcsicmp(cls, L"Edit") != 0)
                break;
            // 確定時だけ(変換中の毎描画は止めない)
            if (!(lParam & GCS_RESULTSTR))
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
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            if (wParam)
                ::SendMessage(hWnd, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
            return lRes;
        }
        case WM_DESTROY:
            ::RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
            pThis->m_hWnd = NULL;
            break;
        }
        return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

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
            // 現在描画済みの内容(枠/スクロールバー含む)をバッファへ取り込み
            ::BitBlt(hdcBuf, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);
            // アルファを不透明化して書き戻す
            ::BufferedPaintMakeOpaque(hBP, &rc);
            ::EndBufferedPaint(hBP, TRUE);
        }
        ::ReleaseDC(hWnd, hdcWin);
    }

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
        // BeginBufferedPaint 毎回は重いので、再利用DIB + AlphaBlend を先に試す。
        {
            static CCC_ChromaBlitCache s_fixCaches[4];
            static unsigned s_fixNext = 0;
            CCC_ChromaBlitCache* pCache = nullptr;
            for (auto& c : s_fixCaches) {
                if (c.pBits && c.dibW == width && c.dibH == height) {
                    pCache = &c;
                    break;
                }
            }
            if (!pCache) {
                pCache = &s_fixCaches[s_fixNext++ % 4];
                if (!pCache->Ensure(hDestDC, width, height))
                    pCache = nullptr;
            }
            if (pCache && pCache->pBits && pCache->hdcDib) {
                CBrush brush(m_clrBg);
                RECT zr = { 0, 0, width, height };
                ::FillRect(pCache->hdcDib, &zr, (HBRUSH)brush.GetSafeHandle());
                PaintClientIntoBuffer(hWnd, pCache->hdcDib);
                pCache->MakeRectOpaque(0, 0, width, height);
                const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                if (::GdiAlphaBlend(hDestDC, 0, 0, width, height,
                        pCache->hdcDib, 0, 0, width, height, bf))
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
        dcDest.BitBlt(0, 0, width, height, &dcMem, 0, 0, SRCCOPY);

        dcMem.SelectObject(pOld);
        dcDest.Detach();
    }
};

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
                if (cn && (strcmp(cn, "CCommandRollView") == 0 || strcmp(cn, "CLyricsViewWnd") == 0))
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
    dc.Detach();
    return painted;
}

static void CCC_ClearOpaqueFixerList(CTypedPtrList<CPtrList, CCustomOpaqueFixer*>& fixers)
{
    while (!fixers.IsEmpty())
    {
        CCustomOpaqueFixer* p = fixers.RemoveHead();
        if (p) { p->Uninstall(); delete p; }
    }
}

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

static void CCC_DrawGroupBoxFrame(CDC& dc, const CRect& r, const CString& t, BOOL bTrans)
{
    CFont* pOF = dc.SelectObject(dc.GetCurrentFont());

    // タイトルが枠幅を食いつぶさないよう省略。右上リボン分(約24px)を確保。
    CString title = t;
    CSize s(0, 0);
    if (!title.IsEmpty())
    {
        const int maxTitleW = (std::max)(8, r.Width() - 40);
        s = dc.GetTextExtent(title);
        if (s.cx > maxTitleW)
        {
            int fit = 0;
            SIZE szFit = {};
            if (::GetTextExtentExPoint(dc.GetSafeHdc(), title, title.GetLength(),
                    maxTitleW, &fit, NULL, &szFit) && fit > 0 && fit < title.GetLength())
            {
                if (fit > 1)
                    title = title.Left(fit - 1) + _T("…");
                else
                    title = _T("…");
                s = dc.GetTextExtent(title);
            }
            else
            {
                // GetTextExtentExPoint 失敗時のフォールバック
                while (title.GetLength() > 1 && dc.GetTextExtent(title).cx > maxTitleW)
                    title = title.Left(title.GetLength() - 1);
                if (title.GetLength() < t.GetLength())
                {
                    if (title.GetLength() > 1)
                        title = title.Left(title.GetLength() - 1) + _T("…");
                    else
                        title = _T("…");
                }
                s = dc.GetTextExtent(title);
            }
        }
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
        dc.DrawText(title, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    dc.SelectObject(pOF);
}

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
    pDlg->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
    if (CCC_IsWin11())
        pDlg->PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
}

static void CCC_PostOpaqueRepaint(HWND hWnd)
{
    for (HWND hChild = ::GetWindow(hWnd, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (CCC_ShouldOpaqueFix(hChild))
            ::PostMessage(hChild, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
        CCC_PostOpaqueRepaint(hChild);
    }
}

static void CCC_ReapplyOpaqueFix(CWnd* pDlg, CTypedPtrList<CPtrList, CCustomOpaqueFixer*>& fixers)
{
    if (!pDlg || !pDlg->GetSafeHwnd() || !CCC_IsWin11()) return;
    // AcrylicCaption 時は save.aero OFF でも fixer が必要
    if (!CCC_IsAeroEnabled() && !CCC_AcrylicCaption(pDlg->m_hWnd)) return;
    CCC_ClearOpaqueFixerList(fixers);
    CCC_InstallOpaqueFixers(pDlg->m_hWnd, fixers);
    CCC_PostOpaqueRepaint(pDlg->m_hWnd);
    pDlg->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
}

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

static BOOL CALLBACK CCC_RefreshChildProc(HWND hChild, LPARAM)
{
    CCC_ForceRepaintHwnd(hChild);
    return TRUE;
}

void CCC_RefreshKids(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd))
        return;
    ::EnumChildWindows(hWnd, CCC_RefreshChildProc, 0);
}
#endif

#if !CCUSTOM_AERO_SUPPORT
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

static BOOL CALLBACK CCC_RefreshChildProcNoAero(HWND hChild, LPARAM)
{
    CCC_ForceRepaintHwnd(hChild);
    return TRUE;
}

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
    CCustomStandardButton* pClose = nullptr;
    CCustomStandardButton* pMin = nullptr;
    CCustomStandardButton* pMax = nullptr;
    CCustomStandardButton* pSettings = nullptr;
    CCustomStandardButton* pPin = nullptr;
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

// キャプション専用アクリル判定。savedata.aero / m_bAeroEnabled は見ない
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

static BOOL CCC_IsCaptionChromeCtrl(HWND hWnd)
{
    if (!hWnd) return FALSE;
    const UINT id = (UINT)::GetDlgCtrlID(hWnd);
    if (id == IDC_MAINWIN_LOCK
        || id == IDC_CAP_CLOSE || id == IDC_CAP_MIN || id == IDC_CAP_MAX
        || id == IDC_CAP_SETTINGS || id == IDC_CAP_PIN)
        return TRUE;
    // キャプション隣の「?」操作ガイド（アクリル帯で欠けないよう chrome 扱い）
    static const UINT kHelpChromeIds[] = {
        IDC_SC_HELP, IDC_PL_HELP, IDC_AN_HELP, IDC_PR_HELP, IDC_EQ_HELP,
        IDC_PT_HELP, IDC_RD_HELP, IDC_DR_HELP, IDC_WE_HELP, IDC_TC_HELP,
        IDC_TE_HELP, IDC_FD_HELP, IDC_KPI_HELP, IDC_SY_HELP, IDC_PRT_HELP,
        IDC_OGG_HELP, IDC_MP_CHEATBTN
    };
    for (int i = 0; i < (int)_countof(kHelpChromeIds); ++i) {
        if (id == kHelpChromeIds[i])
            return TRUE;
    }
    return FALSE;
}

static BOOL CCC_IsCaptionHelpChromeId(UINT id)
{
    static const UINT kHelpChromeIds[] = {
        IDC_SC_HELP, IDC_PL_HELP, IDC_AN_HELP, IDC_PR_HELP, IDC_EQ_HELP,
        IDC_PT_HELP, IDC_RD_HELP, IDC_DR_HELP, IDC_WE_HELP, IDC_TC_HELP,
        IDC_TE_HELP, IDC_FD_HELP, IDC_KPI_HELP, IDC_SY_HELP, IDC_PRT_HELP,
        IDC_OGG_HELP, IDC_MP_CHEATBTN
    };
    for (int i = 0; i < (int)_countof(kHelpChromeIds); ++i) {
        if (id == kHelpChromeIds[i])
            return TRUE;
    }
    return FALSE;
}

static HWND CCC_FindCaptionHelpChrome(HWND hDlg)
{
    if (!hDlg || !::IsWindow(hDlg)) return NULL;
    static const UINT kHelpChromeIds[] = {
        IDC_SC_HELP, IDC_PL_HELP, IDC_AN_HELP, IDC_PR_HELP, IDC_EQ_HELP,
        IDC_PT_HELP, IDC_RD_HELP, IDC_DR_HELP, IDC_WE_HELP, IDC_TC_HELP,
        IDC_TE_HELP, IDC_FD_HELP, IDC_KPI_HELP, IDC_SY_HELP, IDC_PRT_HELP,
        IDC_OGG_HELP, IDC_MP_CHEATBTN
    };
    for (int i = 0; i < (int)_countof(kHelpChromeIds); ++i) {
        HWND h = ::GetDlgItem(hDlg, kHelpChromeIds[i]);
        if (h && ::IsWindow(h))
            return h;
    }
    return NULL;
}

// 表示前でも HWND があれば「?」1 枠分を確保（追随が P/? に被るのを防ぐ）
static int CCC_CaptionHelpChromeReserve(HWND hDlg)
{
    return CCC_FindCaptionHelpChrome(hDlg) ? (CCC_CAP_BTN + CCC_CAP_GAP) : 0;
}

// PROPAGATE 後もキャプション帯は透過描画（チェック等）。ボタンは Opaque 経路。
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
        ::RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
    }
}

// 本文 aero だけ解除（ホストの backdrop/α/ExtendFrame は触らない）
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

// AcrylicCaption 時のホストガラス（常に全面 -1 + REDIRECTIONBITMAP_ALPHA）。
// 本文の不透明化は描画側（FillRectOpaque / MakeOpaque / BitBlt opaque）。
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
#else
    UNREFERENCED_PARAMETER(hWnd);
#endif
}

// 既存 RGB を保ったまま α=255 にする（ピアノ/アナライザ等の BitBlt 後の全透過を塞ぐ）
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
    CCC_PostOpaqueRepaint(pDlg->m_hWnd);
    pDlg->RedrawWindow(NULL, NULL,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
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
    if (!wParam || !lParam)
        return defResult;

    NCCALCSIZE_PARAMS* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
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
    p->rgrc[0].left += frameX;
    p->rgrc[0].right -= frameX;
    p->rgrc[0].bottom -= frameY;
    // top はそのまま（窓上端 = クライアント上端）。DWM のシステムタイトル帯を作らない。
    UNREFERENCED_PARAMETER(frameY);
    return 0;
}

static CCC_CaptionEntry* CCC_FindCaption(HWND hWnd)
{
    for (int i = 0; i < g_captionCount; ++i) {
        if (g_captions[i].hWnd == hWnd)
            return &g_captions[i];
    }
    return nullptr;
}

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

int CCC_GetCustomCaptionHeight(HWND hDlg)
{
    CCC_CaptionEntry* e = CCC_FindCaption(hDlg);
    if (!e || !e->installed)
        return 0;
    return e->height;
}

static void CCC_CaptionDestroyBtn(CCustomStandardButton*& p)
{
    if (!p)
        return;
    if (::IsWindow(p->GetSafeHwnd()))
        p->DestroyWindow();
    p = nullptr;
}

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
        for (int j = i + 1; j < g_captionCount; ++j)
            g_captions[j - 1] = g_captions[j];
        --g_captionCount;
        ZeroMemory(&g_captions[g_captionCount], sizeof(g_captions[g_captionCount]));
        break;
    }
}

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
    return p;
}

static int CCC_CaptionSysBtnCount(const CCC_CaptionEntry* e)
{
    int n = 1; // close
    if (e->hasMax) ++n;
    if (e->hasMin) ++n;
    return n;
}

static int CCC_CaptionExtraBtnCount(const CCC_CaptionEntry* e)
{
    int n = 0;
    if (e->hasSettings) ++n;
    ++n; // pin always
    return n;
}

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
    // 右から: × Max? Min? ⚙? P ←?← メインに追従
    int x = pinLeft - gap - btn;
    if (x < 4) x = 4;

    CRect cur;
    pHelp->GetWindowRect(&cur);
    ::ScreenToClient(hDlg, &cur.TopLeft());
    ::ScreenToClient(hDlg, &cur.BottomRight());
    if (cur.left == x && cur.top == y && cur.Width() == btn && cur.Height() == btn) {
        // 位置は合っていても P の下に沈んでいることがあるので前面へ
        pHelp->SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return;
    }
    pHelp->SetWindowPos(&CWnd::wndTop, x, max(0, y), btn, btn,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

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
        IDC_OGG_HELP, IDC_MP_CHEATBTN
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

#if CCUSTOM_AERO_SUPPORT
    if (bAcrylicCap) {
        // 帯ガラス: ClearRect(α=0) + タイトル。EnsureBackdrop は毎フレーム呼ばない（ちらつき源）。
        // ClearRect は帯上のボタン画素も消すので、最後に ChromeNow で載せ直す。
        const BOOL bCaptionOnly = !CCC_IsAeroEnabled();

        if (bCaptionOnly) {
            CRect body = cr;
            if (e->height > 0 && body.Height() > e->height)
                body.top = e->height;
            CRect clip;
            if (dc.GetClipBox(&clip) != ERROR && !clip.IsRectEmpty())
                body.IntersectRect(&body, &clip);
            // 帯だけの更新では本文 MakeOpaque しない（バナー演奏ちらつき抑制）
            if (body.Width() > 0 && body.Height() > 8)
                CCC_MakeRectOpaquePreserve(dc.GetSafeHdc(), body);
        }

        RECT rcBar = bar;
        CCC_ClearRectChroma(dc.GetSafeHdc(), rcBar, CCC_AERO_CHROMA_KEY);

        const int w = bar.Width();
        const int h = bar.Height();
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* pBits = nullptr;
        HBITMAP hDib = ::CreateDIBSection(dc.GetSafeHdc(), &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (hDib && pBits) {
            ::ZeroMemory(pBits, static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
            HDC hdcMem = ::CreateCompatibleDC(dc.GetSafeHdc());
            HGDIOBJ oldBmp = ::SelectObject(hdcMem, hDib);

            int textLeft = 8;
            // ウィンドウに明示 SetIcon されたものだけ描く。
            // GCLP_HICONSM はクラス共有で、未設定ダイアログに空アイコンが付く原因になる。
            HICON hIcon = (HICON)::SendMessage(hDlg, WM_GETICON, ICON_SMALL, 0);
            if (!hIcon)
                hIcon = (HICON)::SendMessage(hDlg, WM_GETICON, ICON_BIG, 0);
            if (hIcon) {
                const int isz = 16;
                const int iy = (h - isz) / 2;
                ::DrawIconEx(hdcMem, 6, iy, hIcon, isz, isz, 0, NULL, DI_NORMAL);
                textLeft = 6 + isz + 6;
            }

            wchar_t title[512];
            title[0] = 0;
            ::GetWindowTextW(hDlg, title, 511);
            int titleRight = bar.right;
            CCC_CaptionGetTitleRight(hDlg, e, titleRight);
            RECT textRc = { textLeft, 0, titleRight - 4, h };

            HTHEME hTheme = ::OpenThemeData(hDlg, L"WINDOW");
            if (hTheme) {
                DTTOPTS opt = {};
                opt.dwSize = sizeof(opt);
                opt.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR | DTT_GLOWSIZE;
                opt.crText = RGB(255, 255, 255);
                opt.iGlowSize = 10;
                ::DrawThemeTextEx(hTheme, hdcMem, 0, 0, title, -1,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
                    &textRc, &opt);
                ::CloseThemeData(hTheme);
            }
            else {
                ::SetBkMode(hdcMem, TRANSPARENT);
                ::SetTextColor(hdcMem, RGB(255, 255, 255));
                ::DrawTextW(hdcMem, title, -1, &textRc,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }

            // DrawIconEx 等が α=0 のまま残す画素を、非ゼロ RGB だけ不透明化
            {
                UINT32* px = static_cast<UINT32*>(pBits);
                const int n = w * h;
                for (int i = 0; i < n; ++i) {
                    UINT32 p = px[i];
                    if ((p & 0x00FFFFFFu) != 0 && (p >> 24) == 0)
                        px[i] = p | 0xFF000000u;
                }
            }

            BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
            HDC hdcBuf = NULL;
            HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &rcBar, BPBF_TOPDOWNDIB, &params, &hdcBuf);
            if (hdcBuf && hBP) {
                CCC_InitBPClear(hBP, w, h);
                const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                ::GdiAlphaBlend(hdcBuf, rcBar.left, rcBar.top, w, h, hdcMem, 0, 0, w, h, bf);
                ::EndBufferedPaint(hBP, TRUE);
            }
            else {
                const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                ::GdiAlphaBlend(dc.GetSafeHdc(), 0, 0, w, h, hdcMem, 0, 0, w, h, bf);
            }

            ::SelectObject(hdcMem, oldBmp);
            ::DeleteDC(hdcMem);
            ::DeleteObject(hDib);
        }
        CCC_CaptionPaintChromeNow(hDlg);
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
    HICON hIcon = (HICON)::SendMessage(hDlg, WM_GETICON, ICON_SMALL, 0);
    if (!hIcon)
        hIcon = (HICON)::SendMessage(hDlg, WM_GETICON, ICON_BIG, 0);
    if (hIcon) {
        const int isz = 16;
        const int iy = (bar.Height() - isz) / 2;
        ::DrawIconEx(mem.GetSafeHdc(), 6, iy, hIcon, isz, isz, 0, NULL, DI_NORMAL);
        textLeft = 6 + isz + 6;
    }

    wchar_t title[512];
    title[0] = 0;
    ::GetWindowTextW(hDlg, title, 511);
    int titleRight = bar.right;
    CCC_CaptionGetTitleRight(hDlg, e, titleRight);
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
}

static BOOL CCC_CaptionIsRenderClass(CWnd* pDlg)
{
    if (!pDlg || !pDlg->GetRuntimeClass())
        return FALSE;
    return strcmp(pDlg->GetRuntimeClass()->m_lpszClassName, "CRender") == 0;
}

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

static CCC_MainLockOverlayCache* CCC_MainLockEnsureOverlayCachePtr(CCC_MainLockEntry* e)
{
    if (!e)
        return nullptr;
    if (!e->pOverlay)
        e->pOverlay = new (std::nothrow) CCC_MainLockOverlayCache();
    return e->pOverlay;
}

static void CCC_MainLockMarkOverlayDirty(CCC_MainLockEntry* e)
{
    if (e && e->pOverlay)
        e->pOverlay->dirty = TRUE;
}

static CCC_MainLockEntry* CCC_FindMainLockEntry(HWND hWnd)
{
    for (int i = 0; i < g_mainLockCount; ++i) {
        if (g_mainLocks[i].hWnd == hWnd)
            return &g_mainLocks[i];
    }
    return nullptr;
}

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

static void CCC_MainLockPlaceChild(CCC_MainLockEntry& e, const RECT* pMainRect)
{
    if (!pMainRect || !::IsWindow(e.hWnd))
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
    ::SetWindowPos(e.hWnd, NULL, x, y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
}

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
        // キャプション帯: [追従][?][P][⚙?][Min?][Max?][×]
        // 右端は「?」左端 − gap。無ければ Pin 左端 − gap。
        CCC_CaptionEntry* ce = CCC_FindCaption(hDlg);
        int right = 0;
        HWND hHelp = CCC_FindCaptionHelpChrome(hDlg);
        if (hHelp) {
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

void CCC_MainLockGetOverlayRect(HWND hDlg, CRect& rc)
{
    CCC_MainLockGetClientRect(hDlg, rc);
}

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

static void CCC_MainLockSyncBtnCheck(CCC_MainLockEntry* e)
{
    if (!e || e->overlayPaint || !e->pLockBtn || !::IsWindow(e->pLockBtn->GetSafeHwnd()))
        return;
    e->pLockBtn->SetCheck(e->locked ? BST_CHECKED : BST_UNCHECKED);
}

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

static void CCC_MainLockDestroyBtn(CCC_MainLockEntry* e)
{
    if (!e || !e->pLockBtn)
        return;
    if (::IsWindow(e->pLockBtn->GetSafeHwnd()))
        e->pLockBtn->DestroyWindow();
    e->pLockBtn = nullptr;
}

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

static void CCC_MainLockDrawOverlay(CDC& dc, const CRect& rc, BOOL locked)
{
#if CCUSTOM_AERO_SUPPORT
    dc.FillSolidRect(rc, CCC_AERO_CHROMA_KEY);
#else
    dc.FillSolidRect(rc, RGB(52, 44, 68));
#endif

    CRect chk(rc.left + 3, rc.top + 4, rc.left + 17, rc.top + 18);
    dc.DrawEdge(chk, EDGE_SUNKEN, BF_RECT);
    if (locked) {
        CPen pen(PS_SOLID, 2, COLOR_CHECK);
        CPen* pOldPen = dc.SelectObject(&pen);
        dc.MoveTo(chk.left + 3, chk.top + 8);
        dc.LineTo(chk.left + 6, chk.top + 12);
        dc.LineTo(chk.right - 3, chk.top + 5);
        dc.SelectObject(pOldPen);
    }

    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(255, 255, 255));
    CRect textRc = rc;
    textRc.left += 20;
    dc.DrawText(CCC_MainLockLabel(), textRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
}

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

BOOL CCC_MainLockOverlayHitTest(HWND hDlg, CPoint ptClient)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pSaveFlag || !e->overlayPaint)
        return FALSE;
    CRect rc;
    CCC_MainLockGetClientRect(hDlg, rc);
    return rc.PtInRect(ptClient);
}

void CCC_MainLockOverlayToggle(HWND hDlg)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pSaveFlag)
        return;
    CCC_ApplyMainLockState(e, !e->locked);
}

static void CCC_MainLockOnClicked(HWND hDlg)
{
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pLockBtn || e->overlayPaint)
        return;
    CCC_ApplyMainLockState(e, e->pLockBtn->GetCheck() == BST_CHECKED);
}

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

void CCC_PresentOwnedHelp(CWnd* help, CWnd* owner)
{
    if (!help || !::IsWindow(help->GetSafeHwnd()))
        return;
    help->ShowWindow(SW_SHOW);
    // オーナー直上へ（HWND_TOPMOST は使わない）。他ツールが前面になれば一緒に下へ回る。
    if (owner && ::IsWindow(owner->GetSafeHwnd()))
        ::SetWindowPos(owner->GetSafeHwnd(), help->GetSafeHwnd(),
            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    help->BringWindowToTop();
    help->SetForegroundWindow();
}

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
    e->hasMax = (style & WS_MAXIMIZEBOX) != 0;
    e->hasSettings = !CCC_CaptionIsRenderClass(pDlg);
    e->topmost = (::GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;

    CRect rcBefore;
    pDlg->GetClientRect(&rcBefore);

    // モーダル枠だけ外す。WS_CAPTION は DWM 用に維持。
    // システムの min/max ボタンはカスタム側で描くのでスタイルから外す（残骸防止）
    pDlg->ModifyStyle(DS_MODALFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, 0);
    // ホスト α 時は CLIPCHILDREN 必須（親塗りがリスト等のスクロールバーを潰すのを防ぐ）
    pDlg->ModifyStyle(0, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    // 歌詞ウィンドウは LWA_ALPHA 透過度のためアクリル帯を使わない
    // （EnsureBackdrop / ApplyAero が WS_EX_LAYERED を剥がすとスライダーが無効になる）
    e->acrylicCaption = TRUE;
    if (pDlg->GetRuntimeClass()
        && pDlg->GetRuntimeClass()->m_lpszClassName
        && strcmp(pDlg->GetRuntimeClass()->m_lpszClassName, "CDesktopLyricsWnd") == 0)
        e->acrylicCaption = FALSE;
    e->installed = TRUE;

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

    e->pClose = CCC_CaptionMakeBtn(pDlg, IDC_CAP_CLOSE, L"\u00D7");
    if (e->hasMin)
        e->pMin = CCC_CaptionMakeBtn(pDlg, IDC_CAP_MIN, L"\u2013");
    if (e->hasMax)
        e->pMax = CCC_CaptionMakeBtn(pDlg, IDC_CAP_MAX, pDlg->IsZoomed() ? L"\u2752" : L"\u25A1");
    if (e->hasSettings)
        e->pSettings = CCC_CaptionMakeBtn(pDlg, IDC_CAP_SETTINGS, L"\u2699");
    e->pPin = CCC_CaptionMakeBtn(pDlg, IDC_CAP_PIN, e->topmost ? L"P*" : L"P");

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

BOOL CCC_MainLockPreferQuickPresent()
{
    return GetTickCount() < g_mainLockQuickPresentUntil;
}

void CCC_MainLockRefreshOffsetsFor(CWnd* pMain)
{
    if (!pMain || !::IsWindow(pMain->GetSafeHwnd()))
        return;
    for (int i = 0; i < g_mainLockCount; ++i) {
        CCC_MainLockEntry& e = g_mainLocks[i];
        if (!e.locked || !::IsWindow(e.hWnd))
            continue;
        CCC_ComputeMainLockAttach(e.hWnd, &e);
    }
}

void CCC_MainLockRefreshOffsets()
{
    CCC_MainLockRefreshOffsetsFor(CCC_GetActiveMainWindow());
}

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
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_RBUTTONUP()
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnTtnNeedText)
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnTtnNeedText)
    ON_MESSAGE(CCC_MSG_INSTALL_CAPTION, OnInstallCustomCaption)
    ON_WM_NCCALCSIZE()
#if CCUSTOM_AERO_SUPPORT
    ON_MESSAGE(CCC_MSG_REAPPLY_OPAQUE_FIXERS, OnReapplyOpaqueFixers)
#endif
END_MESSAGE_MAP()

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

CCustomBlurDialogBase::CCustomBlurDialogBase() : m_bBlurApplied(FALSE) {}
CCustomBlurDialogBase::CCustomBlurDialogBase(UINT n, CWnd* p) : CCustomDialog(n, p), m_bBlurApplied(FALSE) {}
CCustomBlurDialogBase::~CCustomBlurDialogBase() {}

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

BOOL CCustomBlurDialogBase::OnInitDialog()
{
    BOOL b = CCustomDialog::OnInitDialog();
    // ツール系ダイアログは明示 SetIcon しないとクラス/既定の空アイコンがキャプションに出る。
    // メイン画面などアイコンが必要な側は、この後に SetIcon(m_hIcon, …) する。
    SetIcon(nullptr, TRUE);
    SetIcon(nullptr, FALSE);
#if CCUSTOM_AERO_SUPPORT
    ::SetClassLongPtr(m_hWnd, GCLP_HBRBACKGROUND, 0);
#endif
    ApplyDwmBlur();
    // キャプション化は初回 OnShowWindow（表示前）で行う。
    // PostMessage だと Show 後に走り、システム帯の一瞬アクリル＋縦幅ジャンプになる。
    return b;
}

LRESULT CCustomBlurDialogBase::OnInstallCustomCaption(WPARAM, LPARAM)
{
    CCC_CaptionInstallCore(this, &m_capTip);
#if CCUSTOM_AERO_SUPPORT
    CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
#endif
    return 0;
}

void CCustomBlurDialogBase::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
{
    if (CCC_GetCustomCaptionHeight(m_hWnd) > 0 && bCalcValidRects && lpncsp) {
        CCC_CaptionHandleNcCalcSize(m_hWnd, TRUE, reinterpret_cast<LPARAM>(lpncsp), 0);
        return;
    }
    CCustomDialog::OnNcCalcSize(bCalcValidRects, lpncsp);
}

void CCustomBlurDialogBase::RefreshAeroMode()
{
    ApplyDwmBlurCore(TRUE);
}

void CCustomBlurDialogBase::ApplyDwmBlur()
{
    ApplyDwmBlurCore(FALSE);
}

void CCustomBlurDialogBase::ApplyDwmBlurCore(BOOL bForce)
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
#if CCUSTOM_AERO_SUPPORT
    // 同一 HWND への再入のみ防止（全窓共通 static だと RefreshAll が後続窓をスキップする）
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
        // FinishBlur の FRAMECHANGED 後に帯ガラス＋fixer を載せる
        CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
        if (m_pMainLockSave)
            CCC_MainLockBringToFront(m_hWnd);
        m_bInApplyBlur = FALSE;
        return;
    }

    m_bAeroEnabled = FALSE;
    if (!m_bBlurApplied && !bForce) {
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
    CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
    RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    m_bInApplyBlur = FALSE;
#else
    UNREFERENCED_PARAMETER(bForce);
    m_bBlurApplied = FALSE;
#endif
}

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
    else {
        // 本文 off・キャプション on の同居パス
        // RefreshDwmBlur(ExtendFrame) を後段で呼ぶと本文不透明塗りがガラスに戻るので、
        // ApplyGlassAndFixers（内部で EnsureBackdrop + UPDATENOW）だけにする
        CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
    }
#endif
    UNREFERENCED_PARAMETER(nStatus);
}

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

void CCustomBlurDialogBase::OnDestroy()
{
    CCC_CaptionUnregister(m_hWnd);
    CCC_MainLockUnregister(m_hWnd);
#if CCUSTOM_AERO_SUPPORT
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
#endif
    CCustomDialog::OnDestroy();
}

void CCustomBlurDialogBase::OnSize(UINT nType, int cx, int cy)
{
    CCustomDialog::OnSize(nType, cx, cy);
    CCC_CaptionLayout(m_hWnd);
    if (m_pMainLockSave)
        CCC_MainLockBringToFront(m_hWnd);
    if (CCC_CaptionEntry* e = CCC_FindCaption(m_hWnd)) {
        if (e->installed && e->pMax && ::IsWindow(e->pMax->GetSafeHwnd()))
            e->pMax->SetWindowText(IsZoomed() ? L"\u2752" : L"\u25A1");
    }
}

void CCustomBlurDialogBase::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
    CCustomDialog::OnWindowPosChanged(lpwndpos);
#if CCUSTOM_AERO_SUPPORT
    if (lpwndpos && (lpwndpos->flags & SWP_SHOWWINDOW) && !m_bBlurApplied && CCC_IsAeroEnabled())
        ApplyDwmBlur();
#endif
}

void CCustomBlurDialogBase::OnCompositionChanged()
{
#if CCUSTOM_AERO_SUPPORT
    ApplyDwmBlurCore(TRUE);
#endif
}

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

void CCustomBlurDialogBase::EnableMainWindowLock(int* pSavedLockFlag, BOOL bOverlayPaint)
{
    m_pMainLockSave = pSavedLockFlag;
    CCC_MainLockSetup(this, pSavedLockFlag, bOverlayPaint);
}

void CCustomBlurDialogBase::OnMainLockClicked()
{
    CCC_MainLockOnClicked(m_hWnd);
}

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
            SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
            return;
        }
    }
    CCustomDialog::OnLButtonDown(nFlags, point);
}

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

void CCustomBlurDialogBase::OnRButtonUp(UINT nFlags, CPoint point)
{
    const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
    if (capH > 0 && point.y >= 0 && point.y < capH) {
        CCC_CaptionTrackContextMenu(this, point, m_pMainLockSave);
        return;
    }
    CWnd::OnRButtonUp(nFlags, point);
}

void CCustomBlurDialogBase::OnCapClose()
{
    SendMessage(WM_SYSCOMMAND, SC_CLOSE, 0);
}

void CCustomBlurDialogBase::OnCapMin()
{
    SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
}

void CCustomBlurDialogBase::OnCapMax()
{
    SendMessage(WM_SYSCOMMAND, IsZoomed() ? SC_RESTORE : SC_MAXIMIZE, 0);
}

void CCustomBlurDialogBase::OnCapSettings()
{
    CCC_CaptionOpenSettings(this);
}

void CCustomBlurDialogBase::OnCapPin()
{
    CCC_CaptionTogglePin(this);
}

BOOL CCustomBlurDialogBase::OnTtnNeedText(UINT, NMHDR*, LRESULT* pResult)
{
    *pResult = 0;
    return FALSE;
}

BOOL CCustomBlurDialogBase::PreTranslateMessage(MSG* pMsg)
{
    if (m_capTip.GetSafeHwnd())
        m_capTip.RelayEvent(pMsg);
    return CCustomDialog::PreTranslateMessage(pMsg);
}

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

CCustomDialogEx::CCustomDialogEx() : m_bAeroEnabled(FALSE)
{
    m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
    m_brNull.CreateStockObject(NULL_BRUSH);
}

CCustomDialogEx::CCustomDialogEx(UINT n, CWnd* p) : CDialogEx(n, p), m_bAeroEnabled(FALSE)
{
    m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
    m_brNull.CreateStockObject(NULL_BRUSH);
}

CCustomDialogEx::~CCustomDialogEx()
{
    if (m_brDialog.GetSafeHandle()) m_brDialog.DeleteObject();
    if (m_brNull.GetSafeHandle()) m_brNull.DeleteObject();
}

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

BOOL CCustomDialogEx::OnInitDialog()
{
    BOOL b = CDialogEx::OnInitDialog();
    SubclassChildControls();
    return b;
}

LRESULT CCustomDialogEx::OnSubclassControls(WPARAM, LPARAM)
{
    SubclassChildControls();
    return 0;
}

void CCustomDialogEx::SubclassChildControls()
{
    DoSubclassChildControls(this);
}

HBRUSH CCustomDialogEx::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nC)
{
    HBRUSH h = DlgOnCtlColor(pDC, pWnd, nC, m_brDialog, m_bAeroEnabled);
    return h ? h : CDialogEx::OnCtlColor(pDC, pWnd, nC);
}

BOOL CCustomDialogEx::OnEraseBkgnd(CDC* pDC)
{
    return DlgOnEraseBkgnd(pDC, m_brDialog, m_bAeroEnabled, m_hWnd);
}

void CCustomDialogEx::OnPaint()
{
    if (m_bAeroEnabled)
        DlgOnPaintAero(this, m_bAeroEnabled);
    else
        CDialogEx::OnPaint();
}

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
        NULL, zoomed);
    menu.AddCommand(SC_MOVE,
        LL14(L"移動", L"Move", L"Deplacer", L"Sposta", L"Mover", L"이동", L"移动", L"تحريك", L"Переместить", L"Verschieben", L"Mover", L"Verplaatsen", L"Przesun", L"Tasi"));
    if (pDlg->GetStyle() & WS_THICKFRAME)
        menu.AddCommand(SC_SIZE,
            LL14(L"サイズ変更", L"Size", L"Taille", L"Dimensiona", L"Tamano", L"크기 조정", L"大小", L"الحجم", L"Размер", L"Groesse", L"Tamanho", L"Grootte", L"Rozmiar", L"Boyut"),
            NULL, !zoomed);
    CCC_CaptionEntry* e = CCC_FindCaption(hWnd);
    if (e && e->hasMin)
        menu.AddCommand(SC_MINIMIZE,
            LL14(L"最小化", L"Minimize", L"Reduire", L"Riduci a icona", L"Minimizar", L"최소화", L"最小化", L"تصغير", L"Свернуть", L"Minimieren", L"Minimizar", L"Minimaliseren", L"Minimalizuj", L"Kucult"));
    if (e && e->hasMax)
        menu.AddCommand(SC_MAXIMIZE,
            LL14(L"最大化", L"Maximize", L"Agrandir", L"Ingrandisci", L"Maximizar", L"최대화", L"最大化", L"تكبير", L"Развернуть", L"Maximieren", L"Maximizar", L"Maximaliseren", L"Maksymalizuj", L"Buyut"),
            NULL, !zoomed);
    menu.AddSeparator();
    if (e && e->hasSettings)
        menu.AddCommand(IDC_CAP_SETTINGS,
            LL14(L"設定", L"Settings", L"Parametres", L"Impostazioni", L"Ajustes", L"설정", L"设置", L"الإعدادات", L"Настройки", L"Einstellungen", L"Configuracoes", L"Instellingen", L"Ustawienia", L"Ayarlar"));
    if (e)
        menu.AddCheck(IDC_CAP_PIN,
            LL14(L"常に手前に表示", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano", L"Siempre visible", L"항상 위", L"总在最前", L"دائماً في المقدمة", L"Поверх всех окон", L"Immer im Vordergrund", L"Sempre no topo", L"Altijd bovenop", L"Zawsze na wierzchu", L"Her zaman ustte"),
            e->topmost);
    if (pMainLockSave) {
        CCC_MainLockEntry* le = CCC_FindMainLockEntry(hWnd);
        menu.AddCheck(IDC_MAINWIN_LOCK, CCC_MainLockLabel(), (le && le->locked) ? TRUE : FALSE,
            LL14(L"メイン窓の位置・サイズを固定", L"Lock main window position and size", L"Verrouiller position/taille", L"Blocca posizione/dimensione",
                L"Bloquear posicion/tamano", L"메인 창 위치·크기 고정", L"锁定主窗口位置和大小", L"قفل موضع/حجم النافذة",
                L"Зафиксировать положение/размер", L"Position/Groesse sperren", L"Travar posicao/tamanho", L"Positie/grootte vergrendelen",
                L"Zablokuj pozycje/rozmiar", L"Ana pencere konum/boyut kilitle"));
    }
    {
        HMENU hSys = ::GetSystemMenu(hWnd, FALSE);
        if (hSys) {
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
                        menu.AddCommand(id, text);
                }
            }
        }
    }
    menu.AddSeparator();
    menu.AddCommand(SC_CLOSE,
        LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
    const UINT cmd = menu.Track(scr, pDlg);
    if (cmd == IDC_CAP_SETTINGS)
        pDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CAP_SETTINGS, BN_CLICKED), 0);
    else if (cmd == IDC_CAP_PIN)
        pDlg->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CAP_PIN, BN_CLICKED), 0);
    else if (cmd == IDC_MAINWIN_LOCK)
        CCC_MainLockOverlayToggle(hWnd);
    else if (cmd)
        pDlg->SendMessage(WM_SYSCOMMAND, cmd, 0);
}

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

static void CCC_CaptionTogglePin(CWnd* pDlg)
{
    if (!pDlg) return;
    CCC_CaptionEntry* e = CCC_FindCaption(pDlg->GetSafeHwnd());
    if (!e) return;
    e->topmost = !e->topmost;
    pDlg->SetWindowPos(e->topmost ? &CWnd::wndTopMost : &CWnd::wndNoTopMost, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (e->pPin && ::IsWindow(e->pPin->GetSafeHwnd()))
        e->pPin->SetWindowText(e->topmost ? L"P*" : L"P");
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
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_RBUTTONUP()
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnTtnNeedText)
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnTtnNeedText)
    ON_MESSAGE(CCC_MSG_INSTALL_CAPTION, OnInstallCustomCaption)
    ON_WM_NCCALCSIZE()
#if CCUSTOM_AERO_SUPPORT
    ON_MESSAGE(CCC_MSG_REAPPLY_OPAQUE_FIXERS, OnReapplyOpaqueFixers)
#endif
END_MESSAGE_MAP()

CCustomBlurDialogExBase::CCustomBlurDialogExBase() : m_bBlurApplied(FALSE) {}
CCustomBlurDialogExBase::CCustomBlurDialogExBase(UINT n, CWnd* p) : CCustomDialogEx(n, p), m_bBlurApplied(FALSE) {}
CCustomBlurDialogExBase::~CCustomBlurDialogExBase() {}

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

BOOL CCustomBlurDialogExBase::OnInitDialog()
{
    BOOL b = CCustomDialogEx::OnInitDialog();
    SetIcon(nullptr, TRUE);
    SetIcon(nullptr, FALSE);
#if CCUSTOM_AERO_SUPPORT
    ::SetClassLongPtr(m_hWnd, GCLP_HBRBACKGROUND, 0);
#endif
    ApplyDwmBlur();
    // キャプション化は初回 OnShowWindow（表示前）。PostMessage だとフラッシュ＋縦幅ジャンプ。
    return b;
}

LRESULT CCustomBlurDialogExBase::OnInstallCustomCaption(WPARAM, LPARAM)
{
    CCC_CaptionInstallCore(this, &m_capTip);
#if CCUSTOM_AERO_SUPPORT
    CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
#endif
    return 0;
}

void CCustomBlurDialogExBase::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
{
    if (CCC_GetCustomCaptionHeight(m_hWnd) > 0 && bCalcValidRects && lpncsp) {
        CCC_CaptionHandleNcCalcSize(m_hWnd, TRUE, reinterpret_cast<LPARAM>(lpncsp), 0);
        return;
    }
    CCustomDialogEx::OnNcCalcSize(bCalcValidRects, lpncsp);
}

void CCustomBlurDialogExBase::RefreshAeroMode()
{
    ApplyDwmBlurCore(TRUE);
}

void CCustomBlurDialogExBase::ApplyDwmBlur()
{
    ApplyDwmBlurCore(FALSE);
}

void CCustomBlurDialogExBase::ApplyDwmBlurCore(BOOL bForce)
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
#if CCUSTOM_AERO_SUPPORT
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
        CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
        if (m_pMainLockSave)
            CCC_MainLockBringToFront(m_hWnd);
        m_bInApplyBlur = FALSE;
        return;
    }

    m_bAeroEnabled = FALSE;
    if (!m_bBlurApplied && !bForce) {
        CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
        m_bInApplyBlur = FALSE;
        return;
    }
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
    CCC_DisableBodyAeroOnly(m_hWnd);
    CCC_PrepareDialogSurface(m_hWnd, FALSE);
    PROPAGATE_AERO_TO_CHILDREN(m_hWnd, FALSE);
    m_bBlurApplied = FALSE;
    CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
    RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    m_bInApplyBlur = FALSE;
#else
    UNREFERENCED_PARAMETER(bForce);
    m_bBlurApplied = FALSE;
#endif
}

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
    else {
        // 本文 off・キャプション on。後段 RefreshDwmBlur は本文不透明を潰すので呼ばない
        CCC_CaptionApplyGlassAndFixers(this, m_opaqueFixers);
    }
#endif
    UNREFERENCED_PARAMETER(nStatus);
}

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

void CCustomBlurDialogExBase::OnDestroy()
{
    CCC_CaptionUnregister(m_hWnd);
    CCC_MainLockUnregister(m_hWnd);
#if CCUSTOM_AERO_SUPPORT
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
#endif
    CCustomDialogEx::OnDestroy();
}

void CCustomBlurDialogExBase::OnSize(UINT nType, int cx, int cy)
{
    CCustomDialogEx::OnSize(nType, cx, cy);
    CCC_CaptionLayout(m_hWnd);
    if (m_pMainLockSave)
        CCC_MainLockBringToFront(m_hWnd);
    if (CCC_CaptionEntry* e = CCC_FindCaption(m_hWnd)) {
        if (e->installed && e->pMax && ::IsWindow(e->pMax->GetSafeHwnd()))
            e->pMax->SetWindowText(IsZoomed() ? L"\u2752" : L"\u25A1");
    }
}

void CCustomBlurDialogExBase::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
    CCustomDialogEx::OnWindowPosChanged(lpwndpos);
#if CCUSTOM_AERO_SUPPORT
    if (lpwndpos && (lpwndpos->flags & SWP_SHOWWINDOW) && !m_bBlurApplied && CCC_IsAeroEnabled())
        ApplyDwmBlur();
#endif
}

void CCustomBlurDialogExBase::OnCompositionChanged()
{
#if CCUSTOM_AERO_SUPPORT
    ApplyDwmBlurCore(TRUE);
#endif
}

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

void CCustomBlurDialogExBase::EnableMainWindowLock(int* pSavedLockFlag, BOOL bOverlayPaint)
{
    m_pMainLockSave = pSavedLockFlag;
    CCC_MainLockSetup(this, pSavedLockFlag, bOverlayPaint);
}

void CCustomBlurDialogExBase::OnMainLockClicked()
{
    CCC_MainLockOnClicked(m_hWnd);
}

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
            SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
            return;
        }
    }
    CCustomDialogEx::OnLButtonDown(nFlags, point);
}

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

void CCustomBlurDialogExBase::OnRButtonUp(UINT nFlags, CPoint point)
{
    const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
    if (capH > 0 && point.y >= 0 && point.y < capH) {
        CCC_CaptionTrackContextMenu(this, point, m_pMainLockSave);
        return;
    }
    CWnd::OnRButtonUp(nFlags, point);
}

void CCustomBlurDialogExBase::OnCapClose()
{
    SendMessage(WM_SYSCOMMAND, SC_CLOSE, 0);
}

void CCustomBlurDialogExBase::OnCapMin()
{
    SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
}

void CCustomBlurDialogExBase::OnCapMax()
{
    SendMessage(WM_SYSCOMMAND, IsZoomed() ? SC_RESTORE : SC_MAXIMIZE, 0);
}

void CCustomBlurDialogExBase::OnCapSettings()
{
    CCC_CaptionOpenSettings(this);
}

void CCustomBlurDialogExBase::OnCapPin()
{
    CCC_CaptionTogglePin(this);
}

BOOL CCustomBlurDialogExBase::OnTtnNeedText(UINT, NMHDR*, LRESULT* pResult)
{
    *pResult = 0;
    return FALSE;
}

BOOL CCustomBlurDialogExBase::PreTranslateMessage(MSG* pMsg)
{
    if (m_capTip.GetSafeHwnd())
        m_capTip.RelayEvent(pMsg);
    return CCustomDialogEx::PreTranslateMessage(pMsg);
}

void CCustomBlurDialogExBase::OnMoving(UINT fwSide, LPRECT pRect)
{
    CCustomDialogEx::OnMoving(fwSide, pRect);
    CCC_MainLockOnChildMoving(this, pRect);
}