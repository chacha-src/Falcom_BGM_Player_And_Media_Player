#include "stdafx.h"
#include "CCustomControl.h"
#include "resource.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#pragma comment(lib, "msimg32.lib")

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

// ぼかしダイアログ上ではラベル・スライダー等は透過、ボタン・リスト等は不透明
static BOOL CCC_UseTransPaint(HWND hWnd, BOOL bAeroMode)
{
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

void CCC_RefreshDwmBlur(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd) || !CCC_IsAeroEnabled() || !CCC_IsWin11()) return;
    BOOL compositionEnabled = FALSE;
    if (!::DwmIsCompositionEnabled(&compositionEnabled) || !compositionEnabled) return;
    const int backdropType = 3;
    ::DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
    const MARGINS margins = { -1, -1, -1, -1 };
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

BOOL CCC_ChromaBlitCache::BlitRect(HDC hdcDest, int x, int y, int w, int h)
{
    if (!hdcDest || w <= 0 || h <= 0 || !pBits || !hdcDib || dibW <= 0 || dibH <= 0) return FALSE;
    if (x < 0 || y < 0 || x + w > dibW || y + h > dibH) return FALSE;

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

static void CCC_BlitToRectOpaque(HDC hdcDest, const RECT& rect, HDC hdcSrc, int srcX, int srcY,
    int destW, int destH, int srcW, int srcH, BOOL bStretch)
{
    if (destW <= 0 || destH <= 0) return;
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (hdcBuf && hBP)
    {
        RECT rcBuf = { 0, 0, destW, destH };
        ::FillRect(hdcBuf, &rcBuf, (HBRUSH)::GetStockObject(BLACK_BRUSH));
        ::SetStretchBltMode(hdcBuf, COLORONCOLOR);
        if (bStretch)
            ::StretchBlt(hdcBuf, 0, 0, destW, destH, hdcSrc, srcX, srcY, srcW, srcH, SRCCOPY);
        else
            ::BitBlt(hdcBuf, 0, 0, destW, destH, hdcSrc, srcX, srcY, SRCCOPY);
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
    static CCC_ChromaBlitCache s_nfCache;
    RECT rect = { x, y, x + destW, y + destH };
    if (destW > 0 && destH > 0 &&
        CCC_BlitChromaCachedRect(hdcDest, rect, hdcSrc, srcX, srcY, destW, destH, srcW, srcH, clrKey, s_nfCache))
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

static void CCC_BoxBlurAlpha(std::vector<BYTE>& alpha, int w, int h, int radius)
{
    if (radius <= 0 || w <= 0 || h <= 0) return;
    const int n = w * h;
    std::vector<BYTE> tmp(n);

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

    std::vector<BYTE> alpha((size_t)nPx);
    for (int y = 0; y < bh; ++y) {
        UINT32* row = px + y * s_shadowCache.capW;
        BYTE* arow = alpha.data() + y * bw;
        for (int x = 0; x < bw; ++x) {
            const UINT32 rgb = row[x] & 0x00FFFFFFu;
            if (rgb >= 0x00FEFEFEu)
                arow[x] = 0;
            else
                arow[x] = (BYTE)max(0, min(255, 255 - (int)GetRValue(rgb)));
        }
    }

    const int blurR = max(1, (nBlur + 1) / 2);
    CCC_BoxBlurAlpha(alpha, bw, bh, blurR);
    if (nBlur >= 5)
        CCC_BoxBlurAlpha(alpha, bw, bh, max(1, blurR / 2));

    const int tintR = (GetRValue(clrS) * 3 + 32) / 4;
    const int tintG = (GetGValue(clrS) * 3 + 28) / 4;
    const int tintB = (GetBValue(clrS) * 3 + 40) / 4;
    const int peakA = bAeroTrans ? 88 : 112;

    for (int y = 0; y < bh; ++y) {
        UINT32* row = px + y * s_shadowCache.capW;
        BYTE* arow = alpha.data() + y * bw;
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
    if (bSE)
        DrawTextShadow(pDC, rect, str, fmt, clrS, nSD, nDist, nBlur, clrBg, bAeroTrans);
    pDC->SetTextColor(clrT);
    CRect rt = rect;
    pDC->DrawText(str, rt, fmt);
}

static void DrawTextWithGradient(CDC* pDC, const CRect& rect, const CString& str, UINT fmt, COLORREF cS, COLORREF cE, int nDir, COLORREF clrSh, int nSD, int nDist, int nBlur, BOOL bSE, COLORREF clrBg, int nActW = -1, BOOL bFB = FALSE, BOOL bAeroTrans = FALSE)
{
    if (str.IsEmpty()) return;

    if (bSE)
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
    std::vector<C> corners;

    if (bPA)
    {
        corners.push_back({ rect.left + ofs, rect.top + ofs, 1, 1 });
        corners.push_back({ rect.right - 1 + ofs, rect.bottom - 1 + ofs, -1, -1 });
    }
    else
    {
        corners.push_back({ rect.right - 1 + ofs, rect.top + ofs, -1, 1 });
        corners.push_back({ rect.left + ofs, rect.bottom - 1 + ofs, 1, -1 });
    }

    for (auto& c : corners)
    {
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
    if (bSE)
        DrawTextShadow(&dc, rl, str, fitFmt, clrSh, nSD, nDist, nBlur, clrBg, bAeroTrans, scaleX, scaleY);
    if (bGrad) DrawTextWithGradient(&dc, rl, str, fitFmt, cGS, cGE, nDir, clrSh, nSD, nDist, nBlur, FALSE, clrBg, sz.cx, FALSE, bAeroTrans);
    else DrawTextWithShadow(&dc, rl, str, fitFmt, RGB(0, 0, 0), clrSh, nSD, nDist, nBlur, FALSE, clrBg, bAeroTrans);
    dc.RestoreDC(-1);
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
    // 単色塗りは 1x1 を引き伸ばして AlphaBlend する(見た目同一・GDI 生成圧を大幅削減)。
    if (!pDC || rc.Width() <= 0 || rc.Height() <= 0) return;
    CDC mDC;
    if (!mDC.CreateCompatibleDC(pDC)) return;
    CBitmap mB;
    if (!mB.CreateCompatibleBitmap(pDC, 1, 1)) { mDC.DeleteDC(); return; }
    CBitmap* ob = mDC.SelectObject(&mB);
    mDC.SetPixelV(0, 0, clr);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    ::AlphaBlend(pDC->GetSafeHdc(), rc.left, rc.top, rc.Width(), rc.Height(),
        mDC.GetSafeHdc(), 0, 0, 1, 1, bf);
    mDC.SelectObject(ob);
    mB.DeleteObject();
    mDC.DeleteDC();
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
         p->IsKindOf(RUNTIME_CLASS(CCustomComboBox))))
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
static void CCC_DrawInwoman(CDC* pDC, const CRect& rc, BOOL bAeroTrans)
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
    ON_CONTROL_REFLECT(EN_UPDATE, OnEnUpdate)
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
    ON_MESSAGE(CCC_WM_POST_OPAQUE_PAINT, OnPostOpaquePaint)
END_MESSAGE_MAP()

static const UINT_PTR kEditOpaqueTimerId = 4107;
static const UINT_PTR kButtonAnimTimerId    = 4120; // ボタンの流れるツヤ/鼓動パルス
static const UINT_PTR kCheckBounceTimerId   = 4121; // チェックON時のバウンス
static const UINT_PTR kSliderShimmerTimerId = 4122; // スライダーの流れるシマー

CCustomEdit::CCustomEdit() : m_bHasFocus(FALSE), m_bAutoDelete(FALSE)
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

void CCustomEdit::DrawClientText(CDC& dc, const CRect& r)
{
    CString text;
    GetWindowText(text);

    CFont* pFont = GetFont();
    CFont* pOld = pFont ? dc.SelectObject(pFont) : nullptr;

    dc.SetBkColor(COLOR_EDIT_BG);
    dc.SetTextColor(COLOR_EDIT_TEXT);
    dc.SetBkMode(OPAQUE);

    const DWORD style = (DWORD)GetStyle();
    UINT fmt = DT_NOPREFIX | DT_END_ELLIPSIS;
    if (style & ES_CENTER)
        fmt |= DT_CENTER;
    else if (style & ES_RIGHT)
        fmt |= DT_RIGHT;
    else
        fmt |= DT_LEFT;

    if (style & ES_MULTILINE)
        fmt |= DT_WORDBREAK;
    else
        fmt |= DT_SINGLELINE | DT_VCENTER;

    CRect rc = r;
    rc.DeflateRect(3, 1);
    dc.DrawText(text, &rc, fmt);

    if (pOld)
        dc.SelectObject(pOld);
}

void CCustomEdit::RepaintClient()
{
    if (!GetSafeHwnd())
        return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
    {
        ::RedrawWindow(m_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
        SetWindowPos(NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
        return;
    }
#endif
    Invalidate(FALSE);
    UpdateWindow();
    SetWindowPos(NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

void CCustomEdit::PaintOpaqueClient(CDC& dc)
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
        dc.FillSolidRect(&r, COLOR_EDIT_BG);
        DrawClientText(dc, r);
        return;
    }
    RECT rcBuf = { 0, 0, r.right, r.bottom };
    ::FillRect(hdcBuf, &rcBuf, (HBRUSH)m_brBackground.GetSafeHandle());
    {
        CDC dcBuf;
        dcBuf.Attach(hdcBuf);
        DrawClientText(dcBuf, r);
        dcBuf.Detach();
    }
    ::BufferedPaintMakeOpaque(hBP, &r);
    ::EndBufferedPaint(hBP, TRUE);
}

void CCustomEdit::ScheduleOpaqueRepaint()
{
    if (GetSafeHwnd())
        SendMessage(CCC_WM_POST_OPAQUE_PAINT);
}

LRESULT CCustomEdit::OnPostOpaquePaint(WPARAM, LPARAM)
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

void CCustomEdit::OnPaint()
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
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
    if (CCC_IsAeroEnabled() && CCC_IsWin11() && pDC)
    {
        CRect r;
        GetClientRect(&r);
        pDC->FillSolidRect(&r, COLOR_EDIT_BG);
        return TRUE;
    }
#endif
    UNREFERENCED_PARAMETER(pDC);
    return FALSE;
}

void CCustomEdit::OnNcPaint()
{
    CWindowDC dc(this);
    CRect r;
    GetWindowRect(&r);
    r.OffsetRect(-r.left, -r.top);

    CPen p(PS_SOLID, 2, m_bHasFocus ? RGB(255, 140, 180) : RGB(255, 182, 193));
    CPen* op = dc.SelectObject(&p);
    dc.SelectStockObject(NULL_BRUSH);
    dc.RoundRect(&r, CPoint(6, 6));
    dc.SelectObject(op);

    if (m_bHasFocus)
    {
        DrawSparkle(&dc, r.right - 8, r.top + 8, 3, COLOR_SPARKLE);
        DrawSparkle(&dc, r.left + 8, r.top + 8, 2, COLOR_SPARKLE);
        DrawSparkle(&dc, r.right - 8, r.bottom - 8, 2, COLOR_SPARKLE);
        DrawBow(&dc, CRect(r.CenterPoint().x - 8, r.top - 1, r.CenterPoint().x + 8, r.top + 9), COLOR_BOW);
    }

    CRect rL(r.left + 2, r.CenterPoint().y - 3, r.left + 8, r.CenterPoint().y + 3);
    CRect rR(r.right - 8, r.CenterPoint().y - 3, r.right - 2, r.CenterPoint().y + 3);
    DrawRibbon(&dc, rL, RGB(255, 200, 220));
    DrawRibbon(&dc, rR, RGB(255, 200, 220));
}

LRESULT CCustomEdit::OnPrintClient(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    if (HDC hDC = (HDC)wParam)
    {
        CRect r;
        GetClientRect(&r);
        CDC* pDC = CDC::FromHandle(hDC);
        pDC->FillSolidRect(&r, COLOR_EDIT_BG);
        DrawClientText(*pDC, r);
        return 1;
    }
    return 0;
}

void CCustomEdit::OnEnUpdate()
{
    ScheduleOpaqueRepaint();
}

void CCustomEdit::OnTimer(UINT_PTR nIDEvent)
{
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
    SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    Invalidate(FALSE);
    ScheduleOpaqueRepaint();
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
        SetTimer(kEditOpaqueTimerId, 50, NULL);
#endif
}

void CCustomEdit::OnKillFocus(CWnd* p)
{
    CEdit::OnKillFocus(p);
    m_bHasFocus = FALSE;
    KillTimer(kEditOpaqueTimerId);
    SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    Invalidate(FALSE);
    ScheduleOpaqueRepaint();
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
    m_backstoreW(0), m_backstoreH(0), m_bAeroMode(FALSE), m_bNoParentInvalidate(FALSE)
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

std::vector<TextSegment> CCustomStatic::ParseFormattedText(const CString& str)
{
    std::vector<TextSegment> segs;
    BOOL bB = FALSE, bI = FALSE, bHC = FALSE;
    COLORREF cc = RGB(0, 0, 0);
    int nFO = 0;
    CString cur;

    for (int i = 0; i < str.GetLength(); i++)
    {
        if (i + 1 < str.GetLength() && str[i] == _T('!') && str[i + 1] == _T('@') && i + 2 < str.GetLength())
        {
            TCHAR cmd = str[i + 2];
            // 現在のセグメントを確定してプッシュ
            auto Flush = [&]()
            {
                if (!cur.IsEmpty())
                {
                    TextSegment s;
                    s.text = cur;
                    s.bBold = bB;
                    s.bItalic = bI;
                    s.bHasColor = bHC;
                    s.clrText = cc;
                    s.nFontSizeOffset = nFO;
                    segs.push_back(s);
                    cur.Empty();
                }
            };

            // !@B / !@I / !@Cxxxxxx / !@F+NN 形式の装飾タグ
            if (cmd == _T('B'))
            {
                Flush();
                bB = !bB;
                i += 2;
                continue;
            }
            else if (cmd == _T('I'))
            {
                Flush();
                bI = !bI;
                i += 2;
                continue;
            }
            else if (cmd == _T('C') && i + 8 < str.GetLength())
            {
                CString hx = str.Mid(i + 3, 6);
                int r, g, b;
                if (_stscanf_s(hx, _T("%2x%2x%2x"), &r, &g, &b) == 3)
                {
                    Flush();
                    bHC = TRUE;
                    cc = RGB(r, g, b);
                    i += 8;
                    continue;
                }
            }
            else if (cmd == _T('F') && i + 5 < str.GetLength())
            {
                TCHAR sg = str[i + 3];
                CString nm = str.Mid(i + 4, 2);
                if ((sg == _T('+') || sg == _T('-')) && nm.GetLength() == 2
                    && _istdigit(nm[0]) && _istdigit(nm[1]))
                {
                    int off = _ttoi(nm);
                    if (sg == _T('-')) off = -off;
                    Flush();
                    nFO += off;
                    i += 5;
                    continue;
                }
            }
        }
        cur += str[i];
    }

    if (!cur.IsEmpty())
    {
        TextSegment s;
        s.text = cur;
        s.bBold = bB;
        s.bItalic = bI;
        s.bHasColor = bHC;
        s.clrText = cc;
        s.nFontSizeOffset = nFO;
        segs.push_back(s);
    }
    return segs;
}

void CCustomStatic::DrawSegmentedText(CDC* pDC, const CRect& rect, const std::vector<TextSegment>& segs, const LOGFONT& lf, int h, int w, UINT fmt)
{
    CSize tot = MeasureSegmentedText(pDC, segs, lf, h, w);
    int xP = rect.left;
    if (fmt & DT_CENTER) xP = rect.left + (rect.Width() - tot.cx) / 2;
    else if (fmt & DT_RIGHT) xP = rect.right - tot.cx;

    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    const COLORREF clrBg = COLOR_DIALOG_BG;

    for (size_t i = 0; i < segs.size(); i++)
    {
        LOGFONT lt = lf;
        lt.lfHeight = -max(6, h + segs[i].nFontSizeOffset);
        lt.lfWidth = w;
        if (segs[i].bBold) lt.lfWeight = FW_BOLD;
        if (segs[i].bItalic) lt.lfItalic = TRUE;
        CFont* pFont = CCC_GetPooledSegFont(lt);
        if (!pFont) continue;
        CFont* po = pDC->SelectObject(pFont);
        CSize sz = pDC->GetTextExtent(segs[i].text);
        CRect sr = { xP, rect.top, xP + sz.cx, rect.bottom };
        COLORREF tc = segs[i].bHasColor ? segs[i].clrText : RGB(0, 0, 0);
        if (bTrans && tc == RGB(0, 0, 0)) tc = RGB(2, 2, 2);

        if (m_bGradEnable) DrawTextWithGradient(pDC, sr, segs[i].text, DT_VCENTER | DT_SINGLELINE | DT_LEFT, m_clrGradStart, m_clrGradEnd, m_nGradDirection, m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur, m_bShadowEnable, clrBg, sz.cx, FALSE, bTrans);
        else DrawTextWithShadow(pDC, sr, segs[i].text, DT_VCENTER | DT_SINGLELINE | DT_LEFT, tc, m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur, m_bShadowEnable, clrBg, bTrans);

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
    memDC.FillSolidRect(&rect, COLOR_DIALOG_BG);

    if (m_strText.IsEmpty())
    {
        if (bTrans)
        {
            CCC_RemapSolidColorInDC(memDC, rect, COLOR_DIALOG_BG, CCC_AERO_CHROMA_KEY);
            blitTrans(memDC.GetSafeHdc());
        }
        else
            dc.BitBlt(0, 0, rw, rh, &memDC, 0, 0, SRCCOPY);
        memDC.SelectObject(ob);
        memDC.DeleteDC();
        return;
    }

    CString strText = m_strText;
    const BOOL bHasFmt = (strText.Find(_T("!@")) >= 0);
    std::vector<TextSegment> segs;
    if (bHasFmt) segs = ParseFormattedText(strText);

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
                return MeasureSegmentedText(&memDC, segs, lfB, height, width);
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
                CFont fontTry;
                fontTry.CreateFontIndirect(&lfTry);
                CFont* pOld = memDC.SelectObject(&fontTry);
                CSize size = memDC.GetTextExtent(strText);
                memDC.SelectObject(pOld);
                fontTry.DeleteObject();
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
                CFont fontTry;
                fontTry.CreateFontIndirect(&lfTry);
                CFont* pOld = memDC.SelectObject(&fontTry);
                TEXTMETRIC tm = {};
                memDC.GetTextMetrics(&tm);
                memDC.SelectObject(pOld);
                fontTry.DeleteObject();

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

    const COLORREF clrBg = COLOR_DIALOG_BG;

    if (bHasFmt)
    {
        DrawSegmentedText(&memDC, rect, segs, lfB, finalHeight, finalWidth, fmt);
    }
    else
    {
        CFont fontFinal;
        LOGFONT lfFinal = lfB;
        lfFinal.lfHeight = -finalHeight;
        lfFinal.lfWidth = finalWidth;
        fontFinal.CreateFontIndirect(&lfFinal);
        CFont* pOIF = memDC.SelectObject(&fontFinal);

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

        memDC.SelectObject(pOIF);
        fontFinal.DeleteObject();
    }

    memDC.SelectObject(pOF);

    if (bTrans)
    {
        CCC_RemapSolidColorInDC(memDC, rect, COLOR_DIALOG_BG, CCC_AERO_CHROMA_KEY);
        blitTrans(memDC.GetSafeHdc());
    }
    else
        dc.BitBlt(0, 0, rw, rh, &memDC, 0, 0, SRCCOPY);

    memDC.SelectObject(ob);
    memDC.DeleteDC();
}

void CCustomStatic::OnPaint()
{
    CPaintDC dc(this);
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
    m_strText = t ? t : _T("");
    m_strCachedText.Empty();
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

CSize CCustomStatic::MeasureSegmentedText(CDC* pDC, const std::vector<TextSegment>& segs, const LOGFONT& lf, int h, int w)
{
    CSize tot(0, 0);
    for (size_t i = 0; i < segs.size(); i++)
    {
        LOGFONT lt = lf;
        lt.lfHeight = -max(6, h + segs[i].nFontSizeOffset);
        lt.lfWidth = w;
        if (segs[i].bBold) lt.lfWeight = FW_BOLD;
        if (segs[i].bItalic) lt.lfItalic = TRUE;

        CFont* pFont = CCC_GetPooledSegFont(lt);
        if (!pFont) continue;
        CFont* po = pDC->SelectObject(pFont);

        CSize sz = pDC->GetTextExtent(segs[i].text);
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

    CDC mDC;
    CBitmap mB;
    mDC.CreateCompatibleDC(&dc);
    mB.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
    CBitmap* ob = mDC.SelectObject(&mB);

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
    mDC.DrawText(st, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
    LOGFONT lf;
    pF->GetLogFont(&lf);

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
    m_nShimmer(0), m_bHover(FALSE) {}
CCustomSliderCtrl::~CCustomSliderCtrl() {}

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
    CDC mDC;
    CBitmap mB;
    mDC.CreateCompatibleDC(&dc);
    mB.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
    CBitmap* ob = mDC.SelectObject(&mB);

    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    if (bTrans)
    {
        mDC.FillSolidRect(&r, CCC_AERO_CHROMA_KEY);
        DrawSlider(&mDC);
        CCC_DrawInwoman(&mDC, r, TRUE);
        CCC_BlitChromaTrans(dc.GetSafeHdc(), 0, 0, r.Width(), r.Height(), mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    }
    else
    {
        mDC.FillSolidRect(&r, COLOR_DIALOG_BG);
        DrawSlider(&mDC);
        CCC_DrawInwoman(&mDC, r, FALSE);
        dc.BitBlt(0, 0, r.Width(), r.Height(), &mDC, 0, 0, SRCCOPY);
    }
    mDC.SelectObject(ob);
    mB.DeleteObject();
    mDC.DeleteDC();
}

void CCustomSliderCtrl::OnPaint()
{
    CPaintDC dc(this);
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
    if (!bV)
    {
        int cY = rect.Height() / 2;
        int tL = 12;
        int tR = rect.Width() - 12;
        int tW = tR - tL;
        if (tW <= 0) return;
        int tP = tL + (int)((double)(nPos - nMin) * tW / nR);
        CPen pA(PS_SOLID, 5, RGB(200, 150, 255));
        pDC->SelectObject(&pA);
        pDC->MoveTo(tL, cY);
        pDC->LineTo(tP, cY);
        CPen pI(PS_SOLID, 3, RGB(220, 220, 230));
        pDC->SelectObject(&pI);
        pDC->LineTo(tR, cY);
        CPen pT(PS_SOLID, 2, RGB(150, 100, 200));
        pDC->SelectObject(&pT);
        for (int i = 0; i <= 10; i++)
        {
            int nx = tL + tW * i / 10;
            int nh = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(nx, cY - nh);
            pDC->LineTo(nx, cY + nh);
            if (i % 5 == 0)
            {
                CBrush b(RGB(200, 180, 255));
                CBrush* ob = pDC->SelectObject(&b);
                pDC->Ellipse(nx - 3, cY - nh - 5, nx + 3, cY - nh + 1);
                pDC->SelectObject(ob);
            }
        }
        CRect rD(tP - 9, cY - 12, tP + 9, cY + 12);
        DrawDiamond(pDC, rD, RGB(200, 180, 255));
        DrawSparkle(pDC, tP, cY - 16, 3, COLOR_SPARKLE);
        CPen pL(PS_SOLID, 1, RGB(255, 240, 200));
        pDC->SelectObject(&pL);
        for (int a = 0; a < 360; a += 45)
        {
            double r = a * 3.14159 / 180.0;
            pDC->MoveTo(tP + (int)(12 * cos(r)), cY + (int)(12 * sin(r)));
            pDC->LineTo(tP + (int)(18 * cos(r)), cY + (int)(18 * sin(r)));
        }
    }
    else
    {
        int cX = rect.Width() / 2;
        int tT = 12;
        int tB = rect.Height() - 12;
        int tH = tB - tT;
        if (tH <= 0) return;
        int tP = tT + (int)((double)(nPos - nMin) * tH / nR);
        CPen pA(PS_SOLID, 5, RGB(200, 150, 255));
        pDC->SelectObject(&pA);
        pDC->MoveTo(cX, tP);
        pDC->LineTo(cX, tB);
        CPen pI(PS_SOLID, 3, RGB(220, 220, 230));
        pDC->SelectObject(&pI);
        pDC->MoveTo(cX, tT);
        pDC->LineTo(cX, tP);
        CPen pT(PS_SOLID, 2, RGB(150, 100, 200));
        pDC->SelectObject(&pT);
        for (int i = 0; i <= 10; i++)
        {
            int ny = tT + tH * i / 10;
            int nw = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(cX - nw, ny);
            pDC->LineTo(cX + nw, ny);
            if (i % 5 == 0)
            {
                CBrush b(RGB(200, 180, 255));
                CBrush* ob = pDC->SelectObject(&b);
                pDC->Ellipse(cX + nw + 1, ny - 3, cX + nw + 7, ny + 3);
                pDC->SelectObject(ob);
            }
        }
        CRect rD(cX - 9, tP - 12, cX + 9, tP + 12);
        DrawDiamond(pDC, rD, RGB(200, 180, 255));
    }
}

// 描画モード2：緑系グラデーションとダイヤのつまみ
void CCustomSliderCtrl::DrawMode2(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
    int nR = nMax - nMin;
    BOOL bV = (GetStyle() & TBS_VERT);
    if (!bV)
    {
        int cY = rect.Height() / 2;
        int tL = 12;
        int tR = rect.Width() - 12;
        int tW = tR - tL;
        if (tW <= 0) return;
        int tP = tL + (int)((double)(nPos - nMin) * tW / nR);
        CPen pA(PS_SOLID, 5, RGB(100, 200, 150));
        pDC->SelectObject(&pA);
        pDC->MoveTo(tL, cY);
        pDC->LineTo(tP, cY);
        CPen pI(PS_SOLID, 3, RGB(220, 220, 230));
        pDC->SelectObject(&pI);
        pDC->LineTo(tR, cY);
        CPen pT(PS_SOLID, 2, RGB(80, 160, 120));
        pDC->SelectObject(&pT);
        for (int i = 0; i <= 10; i++)
        {
            int nx = tL + tW * i / 10;
            int nh = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(nx, cY - nh);
            pDC->LineTo(nx, cY + nh);
            if (i % 5 == 0)
            {
                CBrush b(RGB(150, 220, 180));
                CBrush* ob = pDC->SelectObject(&b);
                pDC->Ellipse(nx - 3, cY - nh - 5, nx + 3, cY - nh + 1);
                pDC->SelectObject(ob);
            }
        }
        CRect rD(tP - 9, cY - 12, tP + 9, cY + 12);
        DrawDiamond(pDC, rD, RGB(100, 220, 160));
        DrawSparkle(pDC, tP, cY - 16, 3, COLOR_SPARKLE);
        CPen pL(PS_SOLID, 1, RGB(200, 255, 220));
        pDC->SelectObject(&pL);
        for (int a = 0; a < 360; a += 45)
        {
            double r = a * 3.14159 / 180.0;
            pDC->MoveTo(tP + (int)(12 * cos(r)), cY + (int)(12 * sin(r)));
            pDC->LineTo(tP + (int)(18 * cos(r)), cY + (int)(18 * sin(r)));
        }
    }
    else
    {
        int cX = rect.Width() / 2;
        int tT = 12;
        int tB = rect.Height() - 12;
        int tH = tB - tT;
        if (tH <= 0) return;
        int tP = tT + (int)((double)(nPos - nMin) * tH / nR);
        CPen pA(PS_SOLID, 5, RGB(100, 200, 150));
        pDC->SelectObject(&pA);
        pDC->MoveTo(cX, tP);
        pDC->LineTo(cX, tB);
        CPen pI(PS_SOLID, 3, RGB(220, 220, 230));
        pDC->SelectObject(&pI);
        pDC->MoveTo(cX, tT);
        pDC->LineTo(cX, tP);
        CPen pT(PS_SOLID, 2, RGB(80, 160, 120));
        pDC->SelectObject(&pT);
        for (int i = 0; i <= 10; i++)
        {
            int ny = tT + tH * i / 10;
            int nw = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(cX - nw, ny);
            pDC->LineTo(cX + nw, ny);
            if (i % 5 == 0)
            {
                CBrush b(RGB(150, 220, 180));
                CBrush* ob = pDC->SelectObject(&b);
                pDC->Ellipse(cX + nw + 1, ny - 3, cX + nw + 7, ny + 3);
                pDC->SelectObject(ob);
            }
        }
        CRect rD(cX - 9, tP - 12, cX + 9, tP + 12);
        DrawDiamond(pDC, rD, RGB(100, 220, 160));
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
END_MESSAGE_MAP()

CCustomRangeSliderCtrl::CCustomRangeSliderCtrl()
    : m_bAutoDelete(FALSE), m_nMin(0), m_nMax(100), m_nSelMin(0), m_nSelMax(100),
    m_nDragTarget(0), m_bDragging(FALSE), m_nVisualPos(0), m_nLogicalPos(0), m_bAeroMode(FALSE) {}
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
    return m_nLogicalPos;
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

void CCustomRangeSliderCtrl::SetPlaybackMirror(int nPos, int selMin, int selMax, int rangeMin, int rangeMax)
{
    if (m_bDragging) return;
    if (rangeMax <= rangeMin) rangeMax = rangeMin + 1;
    if (selMin > selMax) { int t = selMin; selMin = selMax; selMax = t; }
    selMin = max(rangeMin, min(rangeMax, selMin));
    selMax = max(rangeMin, min(rangeMax, selMax));
    nPos = max(rangeMin, min(rangeMax, nPos));

    // range 更新前の見た目(px)。サブピクセルの値変化は描画しない。
    const int oldThumb = ValueToPixel(m_nLogicalPos);
    const int oldSel0 = ValueToPixel(m_nSelMin);
    const int oldSel1 = ValueToPixel(m_nSelMax);

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
    if (newThumb == oldThumb && newSel0 == oldSel0 && newSel1 == oldSel1)
        return;

    // バナー Invalidate に合流させると再生が進むほど描画間隔が崩れるため、
    // 見た目変化時のみこのコントロールを即時再描画(状態は上で一括更新済み)。
    RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
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
    CDC mDC;
    CBitmap mB;
    mDC.CreateCompatibleDC(&dc);
    mB.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
    CBitmap* ob = mDC.SelectObject(&mB);

    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    if (bTrans)
    {
        mDC.FillSolidRect(&r, CCC_AERO_CHROMA_KEY);
        DrawRangeSlider(&mDC);
        CCC_DrawInwoman(&mDC, r, TRUE);
#if CCUSTOM_AERO_SUPPORT
        if (CCC_IsAeroEnabled() && CCC_IsWin11())
            CCC_BlitChromaNF(dc.GetSafeHdc(), 0, 0, r.Width(), r.Height(),
                mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
        else
#endif
            CCC_ClearDestBlt(dc.GetSafeHdc(), 0, 0, r.Width(), r.Height(),
                mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    }
    else
    {
        mDC.FillSolidRect(&r, COLOR_DIALOG_BG);
        DrawRangeSlider(&mDC);
        CCC_DrawInwoman(&mDC, r, FALSE);
        dc.BitBlt(0, 0, r.Width(), r.Height(), &mDC, 0, 0, SRCCOPY);
    }
    mDC.SelectObject(ob);
    mB.DeleteObject();
    mDC.DeleteDC();
}

void CCustomRangeSliderCtrl::OnPaint()
{
    CPaintDC dc(this);
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

    // トラック（バー）
    CPen pT(PS_SOLID, 4, RGB(200, 200, 200));
    pDC->SelectObject(&pT);
    pDC->MoveTo(14, cy);
    pDC->LineTo(r.Width() - 14, cy);
    if (xMx > xMn)
        pDC->FillSolidRect(CRect(xMn, cy - 4, xMx, cy + 4), COLOR_RANGE_SELECTION);

    // 最小・最大つまみ
    COLORREF penC = m_bAeroMode ? RGB(1, 1, 1) : RGB(0, 0, 0);
    CPen pB(PS_SOLID, 1, penC);
    pDC->SelectObject(&pB);
    pDC->FillSolidRect(CRect(xMn - 5, cy - 8, xMn + 5, cy + 8), COLOR_RANGE_SLIDER_THUMB);
    pDC->SelectObject(GetStockObject(NULL_BRUSH));
    pDC->Rectangle(CRect(xMn - 5, cy - 8, xMn + 5, cy + 8));
    pDC->FillSolidRect(CRect(xMx - 5, cy - 8, xMx + 5, cy + 8), COLOR_RANGE_SLIDER_THUMB);
    pDC->Rectangle(CRect(xMx - 5, cy - 8, xMx + 5, cy + 8));

    // 現在位置（ハート + きらめき）
    DrawHeart(pDC, CRect(xP - 9, cy - 12, xP + 9, cy + 6), COLOR_SLIDER_THUMB);
    DrawSparkle(pDC, xP + 7, cy - 12, 3, COLOR_SPARKLE);
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
    int xM = ValueToPixel(m_nLogicalPos);
    int xMx = ValueToPixel(m_nSelMax);
    int xMn = ValueToPixel(m_nSelMin);

    if (CRect(xM - 10, cy - 14, xM + 10, cy + 14).PtInRect(p)) return 3; // 現在位置
    if (CRect(xMx - 7, cy - 10, xMx + 7, cy + 10).PtInRect(p)) return 2;  // 最大つまみ
    if (CRect(xMn - 7, cy - 10, xMn + 7, cy + 10).PtInRect(p)) return 1;  // 最小つまみ
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
    if (m_nDragTarget == 0)
    {
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
        m_bDragging = FALSE;
        ReleaseCapture();
        if (m_nDragTarget == 3)
        {
            m_nLogicalPos = m_nVisualPos;
            CSliderCtrl::SetPos(m_nLogicalPos);
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, m_nLogicalPos), (LPARAM)m_hWnd);
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_ENDSCROLL, m_nLogicalPos), (LPARAM)m_hWnd);
        }
#if CCUSTOM_AERO_SUPPORT
        CCC_InvalidateParent(m_hWnd, m_bAeroMode);
#endif
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
    }
}
void CCustomRangeSliderCtrl::OnMouseMove(UINT f, CPoint p)
{
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
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
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
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
        ScheduleOpaqueRepaint();
#endif
    UpdateHotItemFromCursor();
    RedrawVisibleItems();
}
void CCustomListCtrl::OnHScroll(UINT n, UINT p, CScrollBar* s)
{
    CListCtrl::OnHScroll(n, p, s);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
        ScheduleOpaqueRepaint();
#endif
    UpdateHotItemFromCursor();
    RedrawVisibleItems();
}
BOOL CCustomListCtrl::OnMouseWheel(UINT n, short z, CPoint p)
{
    BOOL r = CListCtrl::OnMouseWheel(n, z, p);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
    {
        ScheduleOpaqueRepaint();
        SetTimer(kListScrollOpaqueTimerId, 33, NULL);
    }
#endif
    UpdateHotItemFromCursor();
    RedrawVisibleItems();
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
    int o = m_nHotItem;
    m_nHotItem = n;
    if (o >= 0) RedrawItems(o, o);
    if (m_nHotItem >= 0) RedrawItems(m_nHotItem, m_nHotItem);
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

void CCustomListCtrl::PaintOpaqueIntoBuffer(HDC hdcBuf)
{
    if (!hdcBuf || !m_hWnd) return;
    CRect r;
    GetClientRect(&r);
    if (r.Width() <= 0 || r.Height() <= 0) return;
    ::FillRect(hdcBuf, &r, (HBRUSH)m_brBackground.GetSafeHandle());
    ::SendMessage(m_hWnd, WM_PRINTCLIENT, (WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
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
        return;
    }
    ::FillRect(hdcBuf, &r, (HBRUSH)m_brBackground.GetSafeHandle());
    ::SendMessage(m_hWnd, WM_PRINTCLIENT, (WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
    ::BufferedPaintMakeOpaque(hBP, &r);
    ::EndBufferedPaint(hBP, TRUE);
}

void CCustomListCtrl::OnPaint()
{
    Default();
}

LRESULT CCustomListCtrl::OnPrintClient(WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(WM_PRINTCLIENT, (WPARAM)wParam, (LPARAM)lParam);
}

void CCustomListCtrl::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVCUSTOMDRAW* p = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
    *pResult = CDRF_DODEFAULT;
    switch (p->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
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

        const BOOL bLvAero = m_bAeroMode && !CCC_IsBlurDialogChild(m_hWnd);
        if (bLvAero)
        {
            pDC->FillSolidRect(&r, RGB(0, 0, 0));
            FillRectAlpha(pDC, r, bg, bS ? 180 : bH ? 140 : AERO_ALPHA_SEMI);
        }
        else
            pDC->FillSolidRect(&r, bg);

        if (bS && !bLvAero)
            DrawGlossHighlight(pDC, r, 6);

        if (ns == 0)
        {
            // 再生アイコン: ImageList と pc[].icon の対応は
            //   0=♪A(IDI_ICON1) / 1=空(IDI_ICON2・透明) / 2=♪B(IDI_ICON3)
            // SIconTimer は 0↔2 で点滅。1 は非再生行。0 をスキップすると片方の♪が消える。
            // ♡ は選択装飾なので ♪ の上(手前)に描く。
            CRect ri;
            const BOOL hasIconRect = GetItemRect(ni, &ri, LVIR_ICON);
            LVITEM lvi = { 0 };
            lvi.mask = LVIF_IMAGE;
            lvi.iItem = ni;
            GetItem(&lvi);
            CImageList* pIL = GetImageList(LVSIL_SMALL);
            if (hasIconRect && pIL && lvi.iImage >= 0 && lvi.iImage != 1)
            {
                // ILC_MASK 付き ImageList は ILD_TRANSPARENT の方が塗り残しが少ない
                IMAGEINFO ii = {};
                int iw = 16, ih = 16;
                if (pIL->GetImageInfo(lvi.iImage, &ii))
                {
                    iw = CRect(ii.rcImage).Width();
                    ih = CRect(ii.rcImage).Height();
                }
                const int ix = ri.left + (ri.Width() - iw) / 2;
                const int iy = ri.top + (ri.Height() - ih) / 2;
                pIL->Draw(pDC, lvi.iImage, CPoint(ix, iy), ILD_TRANSPARENT);
            }
            if (bS)
                DrawHeart(pDC, CRect(r.left + 2, r.top + 4, r.left + 16, r.top + 18), COLOR_HEART);
            if (bH && !bS) DrawStar(pDC, r.left + 10, r.top + 10, 2, RGB(255, 215, 0));
        }

        CString st = GetItemText(ni, ns);
        pDC->SetTextColor(m_bAeroMode ? RGB(1, 1, 1) : RGB(0, 0, 0));
        pDC->SetBkMode(TRANSPARENT);

        CRect rt = r;
        if (ns == 0)
        {
            int tl = r.left + 36;
            CRect ri;
            if (GetItemRect(ni, &ri, LVIR_ICON))
            {
                LVITEM lvi = { 0 };
                lvi.mask = LVIF_IMAGE;
                lvi.iItem = ni;
                GetItem(&lvi);
                // ♪表示中(0/2)のみテキストをアイコン右へ。空(1)は無視
                if (lvi.iImage >= 0 && lvi.iImage != 1 && ri.Width() > 0)
                    tl = (std::max)(tl, (int)ri.right + 4);
            }
            tl = (std::min)(tl, (int)r.right - 4);
            rt.left = (std::max)(tl, (int)r.left + 4);
        }
        else if (uColFmt == DT_RIGHT) {
            rt.left += 4;
            rt.right -= 4;
        }
        else
            rt.left += 6;
        rt.DeflateRect(2, 0);

        CFont* po = pDC->SelectObject(GetFont());
        DrawListSubitemCellText(pDC, st, rt, uColFmt);
        pDC->SelectObject(po);

        if (nCols > 0 && ns == nCols - 1)
            DrawLaceLine(pDC, r.left + 10, r.bottom - 1, r.right - 10, r.bottom - 1, RGB(200, 180, 220));
        if (GetExtendedStyle() & LVS_EX_GRIDLINES)
        {
            CPen pp(PS_SOLID, 1, RGB(220, 220, 230));
            CPen* po2 = pDC->SelectObject(&pp);
            pDC->MoveTo(r.left, r.bottom - 1);
            pDC->LineTo(r.right, r.bottom - 1);
            pDC->SelectObject(po2);
        }
        *pResult = CDRF_SKIPDEFAULT;
        break;
    }
    }
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
    m_nShadowBlur(3), m_bShadowEnable(FALSE)
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

void CCustomStandardButton::PreSubclassWindow()
{
    CButton::PreSubclassWindow();
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

    COLORREF bg = bD ? RGB(200, 200, 200)
        : (bP ? COLOR_BUTTON_PUSHED : (m_bMouseOver ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG));
    if (m_bGradEnable && !bD)
        DrawGradientBackground(&mDC, r, m_clrGradStart, m_clrGradEnd, m_nGradDirection);
    else if (!bD)
        DrawSatinFill(&mDC, r, bg);          // サテン/シルク質感
    else
        mDC.FillSolidRect(&r, bg);

    if (!bD)
    {
        // ぷるんとした濡れツヤ + ジェリー感(リムライト&インナーシャドウ)
        CRect rg = r;
        if (bP) rg.OffsetRect(1, 1);
        DrawGlossHighlight(&mDC, rg, 8);
        DrawJellyEdges(&mDC, rg, 8, RGB(120, 40, 80));

        // 大きめのボタンは裾に透けレースの色気を
        if (r.Width() >= 64 && r.Height() >= 26)
            DrawLaceScallop(&mDC, r.left + 8, r.bottom - 6, r.right - 8, 3, COLOR_LACE);

        // ホバー時: とろみハイライトがスーッと流れる(押下トグル上でもホバー中は表示)
        if (bShowFlow)
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

    CPen pL(PS_SOLID, 2, RGB(255, 255, 255));
    CPen pD(PS_SOLID, 2, RGB(128, 128, 128));
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
        ri.DeflateRect(2, 2);
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
        ri.DeflateRect(2, 2);
        mDC.SelectObject(&pL);
        mDC.MoveTo(ri.left, ri.bottom - 1);
        mDC.LineTo(ri.left, ri.top);
        mDC.LineTo(ri.right - 1, ri.top);
    }
    mDC.SelectObject(op);

    if (bF && !bD)
    {
        CRect rf = r;
        rf.DeflateRect(4, 4);
        mDC.DrawFocusRect(&rf);
    }

    CString s;
    GetWindowText(s);
    CFont* pF = GetFont();
    CFont* pOF = mDC.SelectObject(pF ? pF : (CFont*)mDC.SelectStockObject(DEFAULT_GUI_FONT));
    DrawSmartText(&mDC, r, s, bD, bP);
    mDC.SelectObject(pOF);

    if (!bD) CCC_DrawInwoman(&mDC, r, FALSE); // 淫女モード演出

    dc.BitBlt(0, 0, r.Width(), r.Height(), &mDC, 0, 0, SRCCOPY);
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
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
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
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
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
        PaintClient(*pDC, r);
    }
    return 0;
}

BOOL CCustomStandardButton::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
        return TRUE;
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
    }
    CButton::OnMouseMove(f, p);
}

LRESULT CCustomStandardButton::OnMouseLeave(WPARAM, LPARAM)
{
    m_bMouseOver = FALSE;
    UpdateAnimTimer();
    Invalidate(FALSE);
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
            COLORREF bg = bD ? RGB(200, 200, 200) : (s ? COLOR_BUTTON_PUSHED : (m_bIsHot ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG));
            dc.FillSolidRect(0, 0, rw, rh, bg);
            if (!bD) DrawDecorations(&dc, CRect(0, 0, rw, rh), 0, s);
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
            // チェック枠はやわらかいローズで
            CPen p2(PS_SOLID, 2, bC ? RGB(255, 120, 165) : RGB(255, 156, 184));
            CBrush b2(RGB(255, 249, 252));
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
                DrawSmartText2(&dc, rt, t, DT_LEFT | DT_VCENTER | DT_SINGLELINE, bD, FALSE);
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

                DrawCheckMark(&dc, rk, COLOR_CHECK, max(3, s / 4));
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

static void CCC_DrawGroupBoxFrame(CDC& dc, const CRect& r, const CString& t, BOOL bTrans);
static void CCC_BlitGroupFrame(HDC hdcDest, int x, int y, int w, int h,
    HDC hdcSrc, COLORREF clrKey, const CRect& innerClient);

void CCustomGroupBox::DrawGroupBox(CDC* pDC, CRect& rect)
{
    const int rw = rect.Width();
    const int rh = rect.Height();
    if (!pDC || rw <= 0 || rh <= 0) return;

    const BOOL bTrans = CCC_UseTransPaint(m_hWnd, m_bAeroMode);
    CString t;
    GetWindowText(t);

    if (!bTrans)
    {
        pDC->FillSolidRect(&rect, COLOR_DIALOG_BG);
        CFont* pF = GetFont();
        if (pF) pDC->SelectObject(pF);
        CCC_DrawGroupBoxFrame(*pDC, CRect(0, 0, rw, rh), t, FALSE);
        return;
    }

    CDC memDC;
    memDC.CreateCompatibleDC(pDC);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(pDC, rw, rh);
    CBitmap* pOld = memDC.SelectObject(&bmp);
    memDC.FillSolidRect(0, 0, rw, rh, CCC_AERO_CHROMA_KEY);
    CFont* pF = GetFont();
    if (pF) memDC.SelectObject(pF);
    CCC_DrawGroupBoxFrame(memDC, CRect(0, 0, rw, rh), t, TRUE);

    // memDC は「クロマキー地に枠・デコ・ラベルを描画」したもの。
    // 内側を除外せず全面をクロマ合成すると、クロマ部分=透過(アクリル)・
    // 枠ピクセル=不透明 となり、内部透過と下辺を含む枠が一度に正しく出る。
    // (内側除外方式だと内部が未処理=白のまま残り、下辺も欠ける)
    CCC_BlitGroupFrame(pDC->GetSafeHdc(), rect.left, rect.top, rw, rh,
        memDC.GetSafeHdc(), CCC_AERO_CHROMA_KEY, CRect(0, 0, 0, 0));
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
            if (!pThis->m_bPrinting) return TRUE;
            return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
        case WM_PRINTCLIENT:
            if (pThis->m_bPrinting)
                return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            break;
        case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC hDC = ::BeginPaint(hWnd, &ps);
            if (hDC) pThis->PaintOpaque(hWnd, hDC);
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
        case WM_VSCROLL:
        case WM_HSCROLL:
        case WM_MOUSEWHEEL:
        {
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            HDC hDC = ::GetDC(hWnd);
            if (hDC)
            {
                pThis->PaintOpaque(hWnd, hDC);
                ::ReleaseDC(hWnd, hDC);
            }
            // スクロール操作でスクロールバーの表示が変わるので NC も不透明化し直す
            pThis->MakeWindowOpaque(hWnd);
            ::PostMessage(hWnd, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
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
        if (dynamic_cast<CCustomListBox*>(pw) || dynamic_cast<CCustomListCtrl*>(pw)) return COLOR_LIST_BG;
        if (dynamic_cast<CCustomComboBox*>(pw)) return COLOR_COMBO_BG;
        if (dynamic_cast<CCustomStandardButton*>(pw) || dynamic_cast<CButtonST*>(pw)) return COLOR_BUTTON_BG;
        if (dynamic_cast<CCustomEdit*>(pw)) return COLOR_EDIT_BG;
    }
    TCHAR cls[64] = {};
    ::GetClassName(hWnd, cls, 63);
    CString c(cls);
    c.MakeUpper();
    if (c.Find(_T("EDIT")) >= 0) return COLOR_EDIT_BG;
    if (c.Find(_T("LISTBOX")) >= 0) return COLOR_LIST_BG;
    if (c.Find(_T("SYSLISTVIEW32")) >= 0) return COLOR_LIST_BG;
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
    }
    TCHAR cls[64] = {};
    ::GetClassName(hWnd, cls, 63);
    CString c(cls);
    c.MakeUpper();
    if (c.Find(_T("STATIC")) >= 0) return TRUE;
    if (c.Find(TRACKBAR_CLASS) >= 0) return TRUE;
    return FALSE;
}

static BOOL CCC_ShouldOpaqueFix(HWND hWnd)
{
    if (!::IsWindow(hWnd)) return FALSE;
    if (CCC_IsBlurControl(hWnd)) return FALSE;
    if (CWnd* pw = CWnd::FromHandlePermanent(hWnd))
    {
        if (dynamic_cast<CCustomListBox*>(pw)) return TRUE;
        if (dynamic_cast<CCustomListCtrl*>(pw)) return TRUE;
        if (dynamic_cast<CCustomComboBox*>(pw)) return TRUE;
        if (dynamic_cast<CButtonST*>(pw)) return TRUE;
        if (dynamic_cast<CCustomEdit*>(pw)) return TRUE;
        return FALSE;
    }
    TCHAR cls[64] = {};
    ::GetClassName(hWnd, cls, 63);
    CString c(cls);
    c.MakeUpper();
    if (c.Find(_T("BUTTON")) >= 0) return TRUE;
    if (c.Find(_T("LISTBOX")) >= 0) return TRUE;
    if (c.Find(_T("SYSLISTVIEW32")) >= 0) return TRUE;
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
        CBrush br(COLOR_EDIT_BG);
        dc.FillRect(&r, &br);
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
    CSize s = t.IsEmpty() ? CSize(0, 0) : dc.GetTextExtent(t);
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
    if (t.IsEmpty())
        DrawLooseRibbon(&dc, CRect(r.left + 1, nT - 8, r.left + 19, nT + 8), COLOR_BOW);
    DrawSparkle(&dc, r.right - 9, r.bottom - 9, 3, COLOR_SPARKLE);
    DrawSparkle(&dc, r.left + 9, r.bottom - 9, 3, COLOR_SPARKLE);
    // 下辺に黒の細レース + 透けレースのスカラップでランジェリー風の色気
    DrawLaceLine(&dc, r.left + 18, r.bottom - 7, r.right - 18, r.bottom - 7, RGB(60, 40, 55));
    DrawLaceScallop(&dc, r.left + 16, r.bottom - 5, r.right - 16, 3, COLOR_LACE);

    if (!t.IsEmpty())
    {
        CRect rt(r.left + 8, nT - s.cy / 2, r.left + 8 + s.cx + 4, nT + s.cy / 2);
        dc.FillSolidRect(&rt, bTrans ? CCC_AERO_CHROMA_KEY : COLOR_DIALOG_BG);
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(0, 0, 0));
        dc.DrawText(t, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
    if (!pDlg || !pDlg->GetSafeHwnd() || !CCC_IsWin11() || !CCC_IsAeroEnabled()) return;
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
    if (CCC_IsAeroEnabled() && CCC_IsWin11() && CCC_ShouldOpaqueFix(hWnd))
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
// メインウィンドウ位置ロック (サブウィンドウが COggDlg / CMediaPlayerDlg に追随)
// 通常ダイアログ: 子 CCustomCheckBox / GDI全画面(ピアノロール等): オーバーレイ描画
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
static const int CCC_MAINLOCK_MIN_W = 112;
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

static void CCC_ComputeMainLockOffset(HWND hWnd, int& outX, int& outY)
{
    outX = outY = 0;
    CWnd* pMain = CCC_GetActiveMainWindow();
    if (!pMain || !::IsWindow(hWnd))
        return;
    CRect mainRc, selfRc;
    pMain->GetWindowRect(&mainRc);
    ::GetWindowRect(hWnd, &selfRc);
    outX = selfRc.left - mainRc.left;
    outY = selfRc.top - mainRc.top;
}

static const CString& CCC_MainLockLabel()
{
    static const CString s = LL14(
        L"メイン固定", L"Lock main", L"Fixer fenetre principale",
        L"Blocca finestra principale", L"Fijar ventana principal", L"메인 고정",
        L"固定主窗口", L"قفل النافذة الرئيسية", L"Фикс. главное окно",
        L"Hauptfenster fix", L"Fixar janela principal", L"Hoofdvenster vast",
        L"Przypnij do glownego okna", L"Ana pencereye sabitle");
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

static void CCC_InvalidateRectMinus(const CRect& area, const CRect& hole, HWND hWnd)
{
    if (!::IsWindow(hWnd) || area.IsRectEmpty())
        return;
    CRect overlap;
    if (!overlap.IntersectRect(&area, &hole)) {
        ::InvalidateRect(hWnd, area, FALSE);
        return;
    }
    auto inv = [&](const CRect& r) {
        CRect t;
        if (!t.IntersectRect(&r, &area) || t.IsRectEmpty())
            return;
        ::InvalidateRect(hWnd, &t, FALSE);
    };
    inv(CRect(area.left, area.top, area.right, hole.top));
    inv(CRect(area.left, hole.bottom, area.right, area.bottom));
    inv(CRect(area.left, hole.top, hole.left, hole.bottom));
    inv(CRect(hole.right, hole.top, area.right, hole.bottom));
}

void CCC_InvalidateRectMinusOverlay(HWND hDlg, const CRect& area)
{
    if (!::IsWindow(hDlg) || area.IsRectEmpty())
        return;
    CCC_MainLockEntry* e = CCC_FindMainLockEntry(hDlg);
    if (!e || !e->pSaveFlag || !e->overlayPaint) {
        ::InvalidateRect(hDlg, area, FALSE);
        return;
    }
    CRect lockRc;
    CCC_MainLockGetClientRect(hDlg, lockRc);
    if (lockRc.IsRectEmpty()) {
        ::InvalidateRect(hDlg, area, FALSE);
        return;
    }
    lockRc.InflateRect(2, 2);
    CCC_InvalidateRectMinus(area, lockRc, hDlg);
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
        CCC_ComputeMainLockOffset(e->hWnd, e->offsetX, e->offsetY);
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
    e->pLockBtn->SetAeroMode(FALSE);

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
    dc.FillSolidRect(rc, RGB(52, 44, 68));

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
    dc.SetTextColor(RGB(255, 248, 252));
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

    CRect clip;
    dc.GetClipBox(&clip);
    CRect vis;
    if (!vis.IntersectRect(&rc, &clip))
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
    dc.BitBlt(rc.left, rc.top, w, h, &pCache->dc, 0, 0, SRCCOPY);
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
    if (!e || !e->pSaveFlag || !e->overlayPaint)
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
    e->overlayPaint = bOverlayPaint;
    if (e->overlayPaint)
        CCC_MainLockDestroyBtn(e);
    CCC_ApplyMainLockState(e, (*pSavedLockFlag != 0));
    if (e->overlayPaint)
        CCC_MainLockInvalidateOverlay(pDlg->GetSafeHwnd());
    else
        CCC_MainLockBringToFront(pDlg->GetSafeHwnd());
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
        ::LockWindowUpdate(e.hWnd);
        ::SetWindowPos(e.hWnd, NULL,
            pMainRect->left + e.offsetX,
            pMainRect->top + e.offsetY,
            0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
        ::LockWindowUpdate(NULL);
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
    CRect mainRc;
    pMain->GetWindowRect(&mainRc);
    for (int i = 0; i < g_mainLockCount; ++i) {
        CCC_MainLockEntry& e = g_mainLocks[i];
        if (!e.locked || !::IsWindow(e.hWnd))
            continue;
        CRect selfRc;
        ::GetWindowRect(e.hWnd, &selfRc);
        e.offsetX = selfRc.left - mainRc.left;
        e.offsetY = selfRc.top - mainRc.top;
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
    e->offsetX = pRect->left - mainRc.left;
    e->offsetY = pRect->top - mainRc.top;
}

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
    ON_WM_LBUTTONDOWN()
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
    if (CCC_IsAeroEnabled())
    {
        RegisterBlurDialogWndClass(cs.lpszClass, _T("CCustomBlurDlg"));
        cs.lpszClass = _T("CCustomBlurDlg");
    }
#endif
    return TRUE;
}

BOOL CCustomBlurDialogBase::OnInitDialog()
{
    BOOL b = CCustomDialog::OnInitDialog();
#if CCUSTOM_AERO_SUPPORT
    ::SetClassLongPtr(m_hWnd, GCLP_HBRBACKGROUND, 0);
#endif
    ApplyDwmBlur();
    return b;
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
    const BOOL bWant = CCC_IsAeroEnabled();
    if (bWant)
    {
        if (m_bBlurApplied && !bForce)
            return;
        m_bAeroEnabled = TRUE;
        CCC_FinishBlurDlg(this, TRUE, m_bBlurApplied, m_opaqueFixers);
        if (m_pMainLockSave)
            CCC_MainLockBringToFront(m_hWnd);
        return;
    }

    m_bAeroEnabled = FALSE;
    if (!m_bBlurApplied && !bForce)
        return;
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
    CCC_ApplyAero(m_hWnd, FALSE);
    CCC_PrepareDialogSurface(m_hWnd, FALSE);
    PROPAGATE_AERO_TO_CHILDREN(m_hWnd, FALSE);
    m_bBlurApplied = FALSE;
    Invalidate();
#else
    UNREFERENCED_PARAMETER(bForce);
    m_bBlurApplied = FALSE;
#endif
}

void CCustomBlurDialogBase::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CCustomDialog::OnShowWindow(bShow, nStatus);
    if (m_pMainLockSave)
        CCC_MainLockShowBtn(m_hWnd, bShow);
    if (!bShow)
        return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled())
    {
        // 初回のみ Finalize。以降の再表示は DWM 属性の軽い再適用だけで足りる。
        ApplyDwmBlur();
        CCC_RefreshDwmBlur(m_hWnd);
    }
#endif
    UNREFERENCED_PARAMETER(nStatus);
}

void CCustomBlurDialogBase::OnPaint()
{
#if CCUSTOM_AERO_SUPPORT
    if (m_bAeroEnabled && CCC_IsWin11())
    {
        CPaintDC dc(this);
        CCC_PaintAeroGaps(dc, this, nullptr);
        if (m_pMainLockSave)
            CCC_MainLockPaintClient(dc, m_hWnd);
        return;
    }
#endif
    CCustomDialog::OnPaint();
}

void CCustomBlurDialogBase::OnDestroy()
{
    CCC_MainLockUnregister(m_hWnd);
#if CCUSTOM_AERO_SUPPORT
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
#endif
    CCustomDialog::OnDestroy();
}

void CCustomBlurDialogBase::OnSize(UINT nType, int cx, int cy)
{
    CCustomDialog::OnSize(nType, cx, cy);
    if (m_pMainLockSave)
        CCC_MainLockBringToFront(m_hWnd);
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
    if (m_bAeroEnabled && CCC_IsWin11())
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
    CCustomDialog::OnLButtonDown(nFlags, point);
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
    ON_WM_LBUTTONDOWN()
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
    if (CCC_IsAeroEnabled())
    {
        RegisterBlurDialogWndClass(cs.lpszClass, _T("CCustomBlurDlgEx"));
        cs.lpszClass = _T("CCustomBlurDlgEx");
    }
#endif
    return TRUE;
}

BOOL CCustomBlurDialogExBase::OnInitDialog()
{
    BOOL b = CCustomDialogEx::OnInitDialog();
#if CCUSTOM_AERO_SUPPORT
    ::SetClassLongPtr(m_hWnd, GCLP_HBRBACKGROUND, 0);
#endif
    ApplyDwmBlur();
    return b;
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
    const BOOL bWant = CCC_IsAeroEnabled();
    if (bWant)
    {
        if (m_bBlurApplied && !bForce)
            return;
        m_bAeroEnabled = TRUE;
        CCC_FinishBlurDlg(this, TRUE, m_bBlurApplied, m_opaqueFixers);
        if (m_pMainLockSave)
            CCC_MainLockBringToFront(m_hWnd);
        return;
    }

    m_bAeroEnabled = FALSE;
    if (!m_bBlurApplied && !bForce)
        return;
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
    CCC_ApplyAero(m_hWnd, FALSE);
    CCC_PrepareDialogSurface(m_hWnd, FALSE);
    PROPAGATE_AERO_TO_CHILDREN(m_hWnd, FALSE);
    m_bBlurApplied = FALSE;
    Invalidate();
#else
    UNREFERENCED_PARAMETER(bForce);
    m_bBlurApplied = FALSE;
#endif
}

void CCustomBlurDialogExBase::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CCustomDialogEx::OnShowWindow(bShow, nStatus);
    if (m_pMainLockSave)
        CCC_MainLockShowBtn(m_hWnd, bShow);
    if (!bShow)
        return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled())
    {
        // 初回のみ Finalize。以降の再表示は DWM 属性の軽い再適用だけで足りる。
        ApplyDwmBlur();
        CCC_RefreshDwmBlur(m_hWnd);
    }
#endif
    UNREFERENCED_PARAMETER(nStatus);
}

void CCustomBlurDialogExBase::OnPaint()
{
#if CCUSTOM_AERO_SUPPORT
    if (m_bAeroEnabled && CCC_IsWin11())
    {
        CPaintDC dc(this);
        CCC_PaintAeroGaps(dc, this, nullptr);
        if (m_pMainLockSave)
            CCC_MainLockPaintClient(dc, m_hWnd);
        return;
    }
#endif
    CCustomDialogEx::OnPaint();
}

void CCustomBlurDialogExBase::OnDestroy()
{
    CCC_MainLockUnregister(m_hWnd);
#if CCUSTOM_AERO_SUPPORT
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
#endif
    CCustomDialogEx::OnDestroy();
}

void CCustomBlurDialogExBase::OnSize(UINT nType, int cx, int cy)
{
    CCustomDialogEx::OnSize(nType, cx, cy);
    if (m_pMainLockSave)
        CCC_MainLockBringToFront(m_hWnd);
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
    if (m_bAeroEnabled && CCC_IsWin11())
        CCC_ReapplyOpaqueFix(this, m_opaqueFixers);
#endif
    if (m_pMainLockSave)
        CCC_MainLockBringToFront(m_hWnd);
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
    CCustomDialogEx::OnLButtonDown(nFlags, point);
}

void CCustomBlurDialogExBase::OnMoving(UINT fwSide, LPRECT pRect)
{
    CCustomDialogEx::OnMoving(fwSide, pRect);
    CCC_MainLockOnChildMoving(this, pRect);
}