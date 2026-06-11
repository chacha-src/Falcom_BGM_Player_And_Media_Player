#include "stdafx.h"
#include "CCustomControl.h"
#include "BtnST.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#pragma comment(lib, "msimg32.lib")

#ifdef SubclassWindow
#undef SubclassWindow
#endif

#if CCUSTOM_AERO_SUPPORT
static UINT32 CCC_RgbMask(COLORREF clr);
static BYTE CCC_ClampDwmGlassAlpha(BYTE a);
static COLORREF CCC_DarkenGlassTint(COLORREF clr, BYTE rawAlpha);
static void CCC_InstallComboDropListOpaqueFix(HWND hwndCombo);
static void CCC_ReleaseComboDropListOpaqueFix(HWND hwndCombo);
static void CCC_InstallComboDropListGlassFix(HWND hwndCombo);
static void CCC_ReleaseComboDropListGlassFix(HWND hwndCombo);
static void CCC_InstallComboDropListFix(HWND hwndCombo);
static void CCC_ReleaseComboDropListFix(HWND hwndCombo);
static BOOL CCC_IsComboDropGlassInstalled(HWND hwndCombo);
static void CCC_RepaintComboDropGlass(HWND hwndCombo);
static BOOL CCC_IsDescendantOf(HWND hAncestor, HWND hWnd);
static void CCC_InvalidateAllButtonSTOnDialog(HWND hDlg);
static void CCC_RepaintGlassHwnd(HWND hWnd);
static void CCC_BlitTransparentChroma(HDC hdcDest, int x, int y, int w, int h,
    HDC hdcSrc, int srcX, int srcY, COLORREF clrKey, COLORREF clrGlassBase);

static BOOL CCC_UseListCtrlRowGlass(HWND hWnd)
{
    if (!hWnd || !CCC_IsAeroEnabled() || !CCC_IsWin11() || !CCC_IsBlurDialogChild(hWnd))
        return FALSE;
    CWnd* pw = CWnd::FromHandlePermanent(hWnd);
    return pw && dynamic_cast<CCustomListCtrl*>(pw) != NULL;
}

static BOOL CCC_UseComboGlass(HWND hWnd)
{
    if (!hWnd || !CCC_IsAeroEnabled() || !CCC_IsWin11() || !CCC_IsBlurDialogChild(hWnd))
        return FALSE;
    CWnd* pw = CWnd::FromHandlePermanent(hWnd);
    return pw && dynamic_cast<CCustomComboBox*>(pw) != NULL;
}

static CString CCC_FetchListSubitemText(HWND hList, int ni, int ns)
{
    if (!hList || ni < 0 || ns < 0) return CString();
    TCHAR buf[1024];
    buf[0] = _T('\0');
    LVITEM lvi = {};
    lvi.iSubItem = ns;
    lvi.pszText = buf;
    lvi.cchTextMax = (int)_countof(buf);
    ::SendMessage(hList, LVM_GETITEMTEXT, (WPARAM)ni, (LPARAM)&lvi);
    return buf;
}

static int CCC_ListHotItemFromPoint(CListCtrl* pList, CPoint ptClient)
{
    if (!pList || !pList->GetSafeHwnd()) return -1;
    LVHITTESTINFO h = {};
    h.pt = ptClient;
    const int flags = pList->SubItemHitTest(&h);
    if (flags & (LVHT_ONITEM | LVHT_ONITEMICON | LVHT_ONITEMLABEL | LVHT_ONITEMSTATEICON))
        return h.iItem;
    return -1;
}

static COLORREF CCC_ListZebraBg(int ni)
{
    return (ni % 2 == 0) ? COLOR_LIST_BG : COLOR_LIST_ZEBRA_ALT;
}

static COLORREF CCC_ListGlassRowBg(int ni, BOOL bS, BOOL bH)
{
    if (bS) return COLOR_SEL_BG;
    if (bH) return COLOR_LIST_HOVER;
    return CCC_ListZebraBg(ni);
}

static BYTE CCC_ListGlassRowAlpha(int ni, BOOL bS, BOOL bH)
{
#if CCUSTOM_AERO_SUPPORT
    if (bS) return CCC_ScaleGlassAlpha((BYTE)200);
    if (bH) return CCC_ScaleGlassAlpha((BYTE)215);
    return CCC_ScaleGlassAlpha((ni % 2 == 0) ? (BYTE)200 : (BYTE)220);
#else
    if (bS) return (BYTE)200;
    if (bH) return (BYTE)215;
    return (ni % 2 == 0) ? (BYTE)200 : (BYTE)220;
#endif
}

static COLORREF CCC_ComboGlassRowBg(int ni, BOOL bS, BOOL bH, BOOL bD, COLORREF clrDisabledBg)
{
    if (bD) return clrDisabledBg;
    if (bS) return COLOR_SEL_BG;
    if (bH) return RGB(255, 185, 130);
    return (ni % 2 == 0) ? COLOR_COMBO_BG : RGB(255, 232, 220);
}

static BYTE CCC_ComboGlassRowAlpha(int ni, BOOL bS, BOOL bH, BOOL bD, BOOL bNoScroll)
{
#if CCUSTOM_AERO_SUPPORT
    if (bS) return CCC_ScaleGlassAlpha((BYTE)180);
    if (bD) return CCC_ScaleGlassAlpha((BYTE)200);
    if (bH) return CCC_ScaleGlassAlpha((BYTE)195);
    if (bNoScroll)
        return CCC_ScaleGlassAlpha((ni % 2 == 0) ? (BYTE)178 : (BYTE)210);
    return CCC_ScaleGlassAlpha((ni % 2 == 0) ? (BYTE)145 : (BYTE)178);
#else
    if (bS) return (BYTE)180;
    if (bD) return (BYTE)200;
    if (bH) return (BYTE)195;
    if (bNoScroll)
        return (ni % 2 == 0) ? (BYTE)178 : (BYTE)210;
    return (ni % 2 == 0) ? (BYTE)145 : (BYTE)178;
#endif
}

static void CCC_LbVisibleRange(HWND hList, const CRect& rcClient, int& nFirst, int& nLast)
{
    nFirst = 0;
    nLast = -1;
    if (!hList || !::IsWindow(hList)) return;
    const int nCount = (int)::SendMessage(hList, LB_GETCOUNT, 0, 0);
    if (nCount <= 0) return;
    nFirst = (int)::SendMessage(hList, LB_GETTOPINDEX, 0, 0);
    if (nFirst < 0) nFirst = 0;
    for (int i = nFirst; i < nCount; ++i)
    {
        RECT rr = {};
        if (!::SendMessage(hList, LB_GETITEMRECT, i, (LPARAM)&rr))
            break;
        if (rr.top >= rcClient.bottom)
            break;
        nLast = i;
    }
}

static BOOL CCC_ComboDropListNoScroll(HWND hwndList)
{
    if (!hwndList || !::IsWindow(hwndList)) return FALSE;
    CRect rc;
    ::GetClientRect(hwndList, &rc);
    const int nCount = (int)::SendMessage(hwndList, LB_GETCOUNT, 0, 0);
    if (nCount <= 0) return TRUE;
    RECT rrLast = {};
    if (!::SendMessage(hwndList, LB_GETITEMRECT, nCount - 1, (LPARAM)&rrLast))
        return FALSE;
    return rrLast.bottom <= rc.bottom;
}

static BOOL CCC_UseListBoxRowGlass(HWND hWnd)
{
    if (!hWnd || !CCC_IsAeroEnabled() || !CCC_IsWin11() || !CCC_IsBlurDialogChild(hWnd))
        return FALSE;
    CWnd* pw = CWnd::FromHandlePermanent(hWnd);
    return pw && dynamic_cast<CCustomListBox*>(pw) != NULL;
}

static int CCC_ListBoxHotItemFromPoint(CListBox* pList, CPoint ptClient)
{
    if (!pList || !pList->GetSafeHwnd()) return -1;
    BOOL bOutside = FALSE;
    const int idx = pList->ItemFromPoint(ptClient, bOutside);
    if (bOutside || idx < 0 || idx >= pList->GetCount()) return -1;
    return idx;
}


static void CCC_SetDibAlphaGlassRect(void* pBits, int dibW, int dibH, const RECT& rc, COLORREF clrKey, BYTE alpha)
{
    if (!pBits || dibW <= 0 || dibH <= 0) return;
    const UINT32 key = CCC_RgbMask(clrKey);
    const UINT32 a = ((UINT32)CCC_ClampDwmGlassAlpha(alpha)) << 24;
    UINT32* px = (UINT32*)pBits;
    const int y0 = (std::max)(0, (int)rc.top);
    const int y1 = (std::min)(dibH, (int)rc.bottom);
    const int x0 = (std::max)(0, (int)rc.left);
    const int x1 = (std::min)(dibW, (int)rc.right);
    for (int y = y0; y < y1; ++y)
    {
        UINT32* pRow = px + y * dibW;
        for (int x = x0; x < x1; ++x)
        {
            const UINT32 rgb = pRow[x] & 0x00FFFFFFu;
            if (rgb != key)
                pRow[x] = rgb | a;
        }
    }
}

BOOL CCC_IsBlurDialogChild(HWND hWnd)
{
    for (HWND h = hWnd; h; h = ::GetParent(h))
    {
        CWnd* pw = CWnd::FromHandlePermanent(h);
        if (!pw) continue;
        if (dynamic_cast<CCustomDialog*>(pw) || dynamic_cast<CCustomDialogEx*>(pw))
            return TRUE;
    }
    return FALSE;
}

// ぼかしダイアログ上: 前景だけ描き、未描画部は ms2 連動ガラス（Win11）／クロマ透過+Win10 LWA。
// リスト／コンボは別ガラス DIB。ボタン／エディットは不透明 fixer。
static BOOL CCC_UseTransparentPaint(HWND hWnd, BOOL bAeroMode)
{
    if (CCC_IsBlurDialogChild(hWnd) && CCC_IsAeroEnabled()) return TRUE;
    return bAeroMode && !CCC_IsBlurDialogChild(hWnd);
}

static BOOL CCC_UseBlurChildGlassPaint(HWND hWnd)
{
    if (!hWnd || !CCC_IsAeroEnabled() || !CCC_IsWin11() || !CCC_IsBlurDialogChild(hWnd))
        return FALSE;
    CWnd* pw = CWnd::FromHandlePermanent(hWnd);
    return pw && dynamic_cast<CButtonST*>(pw) != NULL;
}

static BOOL CCC_UseEditGlass(HWND hWnd)
{
    if (!hWnd || !CCC_IsAeroEnabled() || !CCC_IsWin11() || !CCC_IsBlurDialogChild(hWnd))
        return FALSE;
    CWnd* pw = CWnd::FromHandlePermanent(hWnd);
    return pw && dynamic_cast<CCustomEdit*>(pw) != NULL;
}

void CCC_InvalidateBlurParent(HWND hWnd, BOOL bAeroMode)
{
    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(bAeroMode);
}

void CCC_RefreshDialogDwmBlur(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd) || !CCC_IsAeroEnabled() || !CCC_IsWin11()) return;
    BOOL compositionEnabled = FALSE;
    if (!::DwmIsCompositionEnabled(&compositionEnabled) || !compositionEnabled) return;
    const int backdropType = 3;
    ::DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
    const MARGINS margins = { -1, -1, -1, -1 };
    ::DwmExtendFrameIntoClientArea(hWnd, &margins);
}

void CCC_RefreshAeroWindowLayer(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd) || !CCC_IsAeroEnabled()) return;

    const DWORD build = CCC_GetWindowsBuildNumber();
    if (build >= 22000)
    {
        CCC_RefreshDialogDwmBlur(hWnd);
        return;
    }
    if (build < 10240) return;

    CCC_ApplyAeroLayeredAlpha(hWnd);

    DWM_BLURBEHIND bb = {};
    bb.dwFlags = DWM_BB_ENABLE;
    bb.fEnable = TRUE;
    ::DwmEnableBlurBehindWindow(hWnd, &bb);
}

static void CCC_PaintDialogGlassRect(HDC hdcDest, const RECT& rect);
static BOOL CCC_AlphaBlendChromaGlassToHDC(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, COLORREF clrKey, BYTE contentAlpha);

static void CCC_TransparentBltClearDest(HDC hdcDest, int x, int y, int w, int h,
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

static void CCC_InitBufferedPaintTransparent(HPAINTBUFFER hBP, int w, int h)
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

// リスト PaintGlassClient と同型: Win11 DWM へアルファをコミットする
static thread_local int s_cccGlassPaintDepth = 0;

static BOOL CCC_CommitGlassDibLayers(CDC& dc, const CRect& rcC, int w, int h,
    HDC hdcGlass, HDC hdcFg, BOOL bHasFg, BOOL bToPaintBuffer)
{
    if (w <= 0 || h <= 0 || !hdcGlass) return FALSE;

    const BLENDFUNCTION bfGlass = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    const BLENDFUNCTION bfFg = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    auto BlendLayers = [&](HDC hdcDest) -> BOOL {
        if (!::GdiAlphaBlend(hdcDest, 0, 0, w, h, hdcGlass, 0, 0, w, h, bfGlass))
            return FALSE;
        if (bHasFg && hdcFg)
            return ::GdiAlphaBlend(hdcDest, 0, 0, w, h, hdcFg, 0, 0, w, h, bfFg) != FALSE;
        return TRUE;
    };

    if (bToPaintBuffer)
        return BlendLayers(dc.GetSafeHdc());

    if (s_cccGlassPaintDepth > 0)
        return BlendLayers(dc.GetSafeHdc());

    struct CCC_GlassPaintGuard { int& d; CCC_GlassPaintGuard(int& x) : d(x) { ++d; } ~CCC_GlassPaintGuard() { --d; } };
    CCC_GlassPaintGuard guard(s_cccGlassPaintDepth);

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &rcC, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP)
        return BlendLayers(dc.GetSafeHdc());
    CCC_InitBufferedPaintTransparent(hBP, w, h);
    const BOOL ok = BlendLayers(hdcBuf);
    ::EndBufferedPaint(hBP, TRUE);
    return ok;
}

static UINT32 CCC_RgbMask(COLORREF clr)
{
    return (UINT32)(GetRValue(clr) << 16) | (UINT32)(GetGValue(clr) << 8) | GetBValue(clr);
}

static void CCC_SetDibAlphaFromChroma(void* pBits, int w, int h, COLORREF clrKey)
{
    if (!pBits || w <= 0 || h <= 0) return;
    const UINT32 key = CCC_RgbMask(clrKey);
    UINT32* px = (UINT32*)pBits;
    const int n = w * h;
    for (int i = 0; i < n; ++i)
    {
        const UINT32 rgb = px[i] & 0x00FFFFFFu;
        px[i] = (rgb == key) ? 0u : (rgb | 0xFF000000u);
    }
}

// Win11 DWM: アルファ>=128 の均一オーバーレイは背面ぼかしが打ち切られる（スライダー25付近で消失）
static BYTE CCC_ClampDwmGlassAlpha(BYTE a)
{
    return (a > 126) ? (BYTE)126 : a;
}

static COLORREF CCC_DarkenGlassTint(COLORREF clr, BYTE rawAlpha)
{
    if (rawAlpha <= 127) return clr;
    const int boost = (int)rawAlpha - 127;
    const int mul = 255 - boost * 128 / 103;
    return RGB(GetRValue(clr) * mul / 255, GetGValue(clr) * mul / 255, GetBValue(clr) * mul / 255);
}

static void CCC_SetDibAlphaGlass(void* pBits, int w, int h, COLORREF clrKey, BYTE contentAlpha)
{
    if (!pBits || w <= 0 || h <= 0) return;
    const UINT32 key = CCC_RgbMask(clrKey);
    const UINT32 a = ((UINT32)CCC_ClampDwmGlassAlpha(contentAlpha)) << 24;
    UINT32* px = (UINT32*)pBits;
    const int n = w * h;
    for (int i = 0; i < n; ++i)
    {
        const UINT32 rgb = px[i] & 0x00FFFFFFu;
        px[i] = (rgb == key) ? 0u : (rgb | a);
    }
}

static void CCC_SetDibChromaTransparent(void* pBits, int w, int h, COLORREF clrKey)
{
    if (!pBits || w <= 0 || h <= 0) return;
    const UINT32 key = CCC_RgbMask(clrKey);
    UINT32* px = (UINT32*)pBits;
    const int n = w * h;
    for (int i = 0; i < n; ++i)
    {
        if ((px[i] & 0x00FFFFFFu) == key)
            px[i] = 0u;
    }
}

static HWND CCC_FindBlurDialogHwnd(HWND hWnd)
{
    for (HWND h = hWnd; h; h = ::GetParent(h))
    {
        CWnd* pw = CWnd::FromHandlePermanent(h);
        if (!pw) continue;
        if (dynamic_cast<CCustomDialog*>(pw) || dynamic_cast<CCustomDialogEx*>(pw))
            return h;
    }
    return NULL;
}

static void CCC_ApplyComboDropListDwm(HWND hwndCombo, HWND hwndList)
{
    if (!hwndCombo || !hwndList || !::IsWindow(hwndList)) return;
    const MARGINS margins = { -1, -1, -1, -1 };
    ::DwmExtendFrameIntoClientArea(hwndList, &margins);
    const int backdropType = 3;
    ::DwmSetWindowAttribute(hwndList, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
    UNREFERENCED_PARAMETER(hwndCombo);
}

// コンボのドロップダウンは別 HWND のポップアップのため、背面のぼかしを親ダイアログから転写する
static void CCC_BlitDialogBackdropUnderPopup(HWND hwndCombo, HWND hwndPopup, HDC hdcDest, int destW, int destH)
{
    if (!hwndCombo || !hwndPopup || !hdcDest || destW <= 0 || destH <= 0) return;
    HWND hDlg = CCC_FindBlurDialogHwnd(hwndCombo);
    if (!hDlg || !::IsWindow(hDlg)) return;

    CCC_RefreshDialogDwmBlur(hDlg);

    CRect rcDlg;
    ::GetClientRect(hDlg, &rcDlg);
    if (rcDlg.IsRectEmpty()) return;

    CRect rcPop;
    ::GetClientRect(hwndPopup, &rcPop);
    ::MapWindowPoints(hwndPopup, HWND_DESKTOP, (LPPOINT)&rcPop, 2);
    CPoint pt(rcPop.left, rcPop.top);
    ::ScreenToClient(hDlg, &pt);

    const int srcW = min(destW, (int)rcDlg.right - pt.x);
    const int srcH = min(destH, (int)rcDlg.bottom - pt.y);
    if (srcW <= 0 || srcH <= 0) return;

    CDC dcDest;
    dcDest.Attach(hdcDest);
    CDC dcCap;
    dcCap.CreateCompatibleDC(&dcDest);
    CBitmap bmpCap;
    bmpCap.CreateCompatibleBitmap(&dcDest, rcDlg.Width(), rcDlg.Height());
    CBitmap* obCap = dcCap.SelectObject(&bmpCap);
    dcCap.FillSolidRect(0, 0, rcDlg.Width(), rcDlg.Height(), RGB(0, 0, 0));
    if (!::PrintWindow(hDlg, dcCap.GetSafeHdc(), PW_CLIENTONLY))
        ::SendMessage(hDlg, WM_PRINT, (WPARAM)dcCap.GetSafeHdc(), PRF_CLIENT | PRF_ERASEBKGND);
    dcDest.BitBlt(0, 0, srcW, srcH, &dcCap, pt.x, pt.y, SRCCOPY);
    dcCap.SelectObject(obCap);
    dcDest.Detach();
}

struct CCC_ComboDropBackdropCache
{
    HWND hwndList;
    CSize popupSize;
    CBitmap bmp;
    BOOL bValid;

    CCC_ComboDropBackdropCache() : hwndList(NULL), bValid(FALSE) {}
};

static CCC_ComboDropBackdropCache g_comboDropBackdrop;

static void CCC_ForcePaintAllButtonSTOnDialog(HWND hDlg);
static void CCC_ForcePaintGlassFieldsOnDialog(HWND hDlg);
static void CCC_InstallButtonSTGlassFixersRecursive(HWND hParent);
static void CCC_InstallEditComboGlassFixersRecursive(HWND hParent);
static void CCC_ReinstallBlurGlassFixersOnDialog(HWND hDlg);
static void CCC_RepaintGlassHwnd(HWND hWnd);

static const UINT_PTR kComboDropBackdropWarmTimerId = 0xCB02;

static void CCC_InvalidateComboDropBackdrop(HWND hwndList)
{
    if (hwndList && g_comboDropBackdrop.hwndList != hwndList)
        return;
    g_comboDropBackdrop.bValid = FALSE;
    g_comboDropBackdrop.hwndList = NULL;
    if (g_comboDropBackdrop.bmp.GetSafeHandle())
        g_comboDropBackdrop.bmp.DeleteObject();
}

static BOOL CCC_GetComboDropBackdropOffset(HWND hwndCombo, HWND hwndPopup, int destW, int destH,
    HWND& hDlg, CRect& rcDlg, CPoint& ptDlg, int& srcW, int& srcH)
{
    hDlg = NULL;
    ptDlg = CPoint(0, 0);
    srcW = srcH = 0;
    if (!hwndCombo || !hwndPopup || destW <= 0 || destH <= 0) return FALSE;
    if (!::IsWindowVisible(hwndPopup)) return FALSE;

    hDlg = CCC_FindBlurDialogHwnd(hwndCombo);
    if (!hDlg || !::IsWindow(hDlg)) return FALSE;

    ::GetClientRect(hDlg, &rcDlg);
    if (rcDlg.IsRectEmpty()) return FALSE;

    CRect rcPop;
    ::GetWindowRect(hwndPopup, &rcPop);
    if (rcPop.IsRectEmpty()) return FALSE;

    ptDlg = CPoint(rcPop.left, rcPop.top);
    ::ScreenToClient(hDlg, &ptDlg);
    srcW = min(destW, (int)rcDlg.right - ptDlg.x);
    srcH = min(destH, (int)rcDlg.bottom - ptDlg.y);
    return srcW > 0 && srcH > 0;
}

static void CCC_ScheduleComboDropBackdropWarm(HWND hwndList)
{
    if (!hwndList || !::IsWindow(hwndList)) return;
    ::SetTimer(hwndList, kComboDropBackdropWarmTimerId, 16, NULL);
}

static BOOL CCC_CaptureComboDropBackdrop(HWND hwndCombo, HWND hwndPopup, HDC hdcRef, int destW, int destH)
{
    if (!hwndCombo || !hwndPopup || !hdcRef || destW <= 0 || destH <= 0) return FALSE;

    HWND hDlg = NULL;
    CRect rcDlg;
    CPoint ptDlg;
    int srcW = 0, srcH = 0;
    if (!CCC_GetComboDropBackdropOffset(hwndCombo, hwndPopup, destW, destH, hDlg, rcDlg, ptDlg, srcW, srcH))
        return FALSE;

    CDC dcCap, dcCache;
    dcCap.CreateCompatibleDC(CDC::FromHandle(hdcRef));
    dcCache.CreateCompatibleDC(CDC::FromHandle(hdcRef));
    CBitmap bmpDlg, bmpSlice;
    bmpDlg.CreateCompatibleBitmap(CDC::FromHandle(hdcRef), rcDlg.Width(), rcDlg.Height());
    bmpSlice.CreateCompatibleBitmap(CDC::FromHandle(hdcRef), destW, destH);
    CBitmap* obCap = dcCap.SelectObject(&bmpDlg);
    dcCap.FillSolidRect(0, 0, rcDlg.Width(), rcDlg.Height(), RGB(0, 0, 0));
    if (!::PrintWindow(hDlg, dcCap.GetSafeHdc(), PW_CLIENTONLY))
        ::SendMessage(hDlg, WM_PRINT, (WPARAM)dcCap.GetSafeHdc(), PRF_CLIENT | PRF_ERASEBKGND);
    CBitmap* obSlice = dcCache.SelectObject(&bmpSlice);
    dcCache.BitBlt(0, 0, destW, destH, &dcCap, ptDlg.x, ptDlg.y, SRCCOPY);
    dcCap.SelectObject(obCap);
    dcCache.SelectObject(obSlice);

    g_comboDropBackdrop.bValid = FALSE;
    if (g_comboDropBackdrop.bmp.GetSafeHandle())
        g_comboDropBackdrop.bmp.DeleteObject();
    if (!g_comboDropBackdrop.bmp.CreateCompatibleBitmap(CDC::FromHandle(hdcRef), destW, destH))
        return FALSE;

    CDC dcStore;
    dcStore.CreateCompatibleDC(CDC::FromHandle(hdcRef));
    CBitmap* obStore = dcStore.SelectObject(&g_comboDropBackdrop.bmp);
    dcStore.BitBlt(0, 0, destW, destH, &dcCache, 0, 0, SRCCOPY);
    dcStore.SelectObject(obStore);
    g_comboDropBackdrop.hwndList = hwndPopup;
    g_comboDropBackdrop.popupSize = CSize(destW, destH);
    g_comboDropBackdrop.bValid = TRUE;
    CCC_ForcePaintAllButtonSTOnDialog(hDlg);
    return TRUE;
}

static BOOL CCC_BlitComboDropBackdropCached(HWND hwndCombo, HWND hwndPopup, HDC hdcDest, int destW, int destH)
{
    if (!hwndCombo || !hwndPopup || !hdcDest || destW <= 0 || destH <= 0) return FALSE;

    const BOOL bCacheHit = g_comboDropBackdrop.bValid && g_comboDropBackdrop.hwndList == hwndPopup
        && g_comboDropBackdrop.popupSize.cx == destW && g_comboDropBackdrop.popupSize.cy == destH
        && g_comboDropBackdrop.bmp.GetSafeHandle();

    if (!bCacheHit && !CCC_CaptureComboDropBackdrop(hwndCombo, hwndPopup, hdcDest, destW, destH))
        return FALSE;

    if (!g_comboDropBackdrop.bValid || !g_comboDropBackdrop.bmp.GetSafeHandle())
        return FALSE;

    CDC dcDest;
    dcDest.Attach(hdcDest);
    CDC dcMem;
    dcMem.CreateCompatibleDC(&dcDest);
    CBitmap* ob = dcMem.SelectObject(&g_comboDropBackdrop.bmp);
    dcDest.BitBlt(0, 0, destW, destH, &dcMem, 0, 0, SRCCOPY);
    dcMem.SelectObject(ob);
    dcDest.Detach();
    return TRUE;
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

// 画面 FillRect なし。バッファを毎回クロマキーで全面初期化してからアルファ合成（残像防止）
static BOOL CCC_BlitToRectChromaNoFlicker(HDC hdcDest, const RECT& rect, HDC hdcSrc, int srcX, int srcY,
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
    CCC_SetDibAlphaFromChroma(pBits, destW, destH, clrKey);

    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    BOOL bOk = FALSE;
    if (hdcBuf && hBP)
    {
        CCC_InitBufferedPaintTransparent(hBP, destW, destH);
        bOk = ::GdiAlphaBlend(hdcBuf, 0, 0, destW, destH, dcDib.GetSafeHdc(), 0, 0, destW, destH, bf);
        ::EndBufferedPaint(hBP, TRUE);
    }
    else
    {
        bOk = ::GdiAlphaBlend(hdcDest, rect.left, rect.top, destW, destH,
            dcDib.GetSafeHdc(), 0, 0, destW, destH, bf);
    }

    ::SelectObject(dcDib.GetSafeHdc(), hOld);
    ::DeleteObject(hDib);
    dcSrc.Detach();
    return bOk;
}

static BOOL CCC_BlitToRectAeroChildGlass(HDC hdcDest, const RECT& rect, HDC hdcSrc, int srcX, int srcY,
    int destW, int destH, int srcW, int srcH, COLORREF clrKey, BOOL bStretch, COLORREF clrGlassBase = COLOR_DIALOG_BG)
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
    CCC_SetDibAlphaFromChroma(pBits, destW, destH, clrKey);

    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdcDest, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    BOOL bOk = FALSE;
    if (hdcBuf && hBP)
    {
        CCC_InitBufferedPaintTransparent(hBP, destW, destH);
        CDC memBase;
        memBase.CreateCompatibleDC(CDC::FromHandle(hdcBuf));
        CBitmap bmpBase;
        bmpBase.CreateCompatibleBitmap(CDC::FromHandle(hdcBuf), destW, destH);
        CBitmap* pOldBase = memBase.SelectObject(&bmpBase);
        const BYTE rawA = CCC_GetAeroGlassAlpha();
        memBase.FillSolidRect(0, 0, destW, destH, CCC_DarkenGlassTint(clrGlassBase, rawA));
        CCC_AlphaBlendChromaGlassToHDC(hdcBuf, 0, 0, destW, destH, memBase.GetSafeHdc(), 0, 0,
            CCC_AERO_CHROMA_KEY, rawA);
        memBase.SelectObject(pOldBase);
        bOk = ::GdiAlphaBlend(hdcBuf, 0, 0, destW, destH, dcDib.GetSafeHdc(), 0, 0, destW, destH, bf);
        ::EndBufferedPaint(hBP, TRUE);
    }
    else
    {
        CDC memBase;
        memBase.CreateCompatibleDC(CDC::FromHandle(hdcDest));
        CBitmap bmpBase;
        bmpBase.CreateCompatibleBitmap(CDC::FromHandle(hdcDest), destW, destH);
        CBitmap* pOldBase = memBase.SelectObject(&bmpBase);
        const BYTE rawA = CCC_GetAeroGlassAlpha();
        memBase.FillSolidRect(0, 0, destW, destH, CCC_DarkenGlassTint(clrGlassBase, rawA));
        CCC_AlphaBlendChromaGlassToHDC(hdcDest, rect.left, rect.top, destW, destH, memBase.GetSafeHdc(), 0, 0,
            CCC_AERO_CHROMA_KEY, rawA);
        memBase.SelectObject(pOldBase);
        bOk = ::GdiAlphaBlend(hdcDest, rect.left, rect.top, destW, destH,
            dcDib.GetSafeHdc(), 0, 0, destW, destH, bf);
    }

    ::SelectObject(dcDib.GetSafeHdc(), hOld);
    ::DeleteObject(hDib);
    dcSrc.Detach();
    return bOk;
}

void CCC_BlitAeroChildComposite(HDC hdcDest, int x, int y, int w, int h,
    HDC hdcSrc, int srcX, int srcY, COLORREF clrKey, COLORREF clrGlassBase)
{
    if (w <= 0 || h <= 0) return;
    RECT rect = { x, y, x + w, y + h };
    if (!CCC_BlitToRectAeroChildGlass(hdcDest, rect, hdcSrc, srcX, srcY, w, h, w, h, clrKey, FALSE, clrGlassBase))
        ::BitBlt(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, SRCCOPY);
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
        CCC_InitBufferedPaintTransparent(hBP, w, h);
        ::EndBufferedPaint(hBP, TRUE);
    }
}

void CCC_RepaintDialogAeroGaps(HWND hWnd, const RECT* pPreserveRect)
{
    if (!hWnd || !::IsWindow(hWnd) || !CCC_IsAeroEnabled()) return;
    CWnd* pWnd = CWnd::FromHandlePermanent(hWnd);
    if (!pWnd) return;
    CClientDC dc(pWnd);
    CCC_PaintDialogAeroGaps(dc, pWnd, pPreserveRect);
}

void CCC_PaintDialogAeroGaps(CDC& dc, CWnd* pWnd, const RECT* pPreserveRect)
{
    if (!pWnd || !pWnd->GetSafeHwnd()) return;
    CRect clip;
    if (dc.GetClipBox(&clip) == ERROR || clip.IsRectEmpty())
    {
        CRect cr;
        pWnd->GetClientRect(&cr);
        clip = cr;
        dc.SelectClipRgn(NULL);
    }
    CCC_SelectClipExcludeChildren(dc, pWnd);
    // クライアント全体が preserve 内に収まる（格納状態など）と RGN_DIFF が空になり隙間が描画されない
    CRect crClient;
    pWnd->GetClientRect(&crClient);
    const RECT* pEffPreserve = pPreserveRect;
    if (pPreserveRect)
    {
        CRect rp(pPreserveRect);
        if (crClient.bottom <= rp.bottom && crClient.right <= rp.right)
            pEffPreserve = nullptr;
    }
    if (pEffPreserve)
    {
        CRgn rgnClip, rgnPreserve;
        rgnClip.CreateRectRgnIndirect(&clip);
        rgnPreserve.CreateRectRgnIndirect(pEffPreserve);
        rgnClip.CombineRgn(&rgnClip, &rgnPreserve, RGN_DIFF);
        dc.SelectClipRgn(&rgnClip, RGN_AND);
        if (dc.GetClipBox(&clip) == NULLREGION || clip.IsRectEmpty()) return;
    }
    RECT rcClip = clip;
    CCC_PaintDialogGlassRect(dc.GetSafeHdc(), rcClip);
}

void CCC_SelectClipExcludeChildren(CDC& dc, CWnd* pWnd)
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
    CCC_TransparentBltClearDest(hdcDest, rect.left, rect.top, destW, destH,
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

void CCC_BlitStretchChromaNoFlicker(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH, COLORREF clrKey)
{
    RECT rect = { x, y, x + destW, y + destH };
    if (!CCC_BlitToRectChromaNoFlicker(hdcDest, rect, hdcSrc, srcX, srcY, destW, destH, srcW, srcH, clrKey, TRUE))
        CCC_BlitToRectChroma(hdcDest, rect, hdcSrc, srcX, srcY, destW, destH, srcW, srcH, clrKey, TRUE);
}

void CCC_BlitChroma(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey)
{
    RECT rect = { x, y, x + w, y + h };
    CCC_BlitToRectChroma(hdcDest, rect, hdcSrc, srcX, srcY, w, h, w, h, clrKey, FALSE);
}

void CCC_BlitChromaNoFlicker(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey)
{
    RECT rect = { x, y, x + w, y + h };
    if (!CCC_BlitToRectChromaNoFlicker(hdcDest, rect, hdcSrc, srcX, srcY, w, h, w, h, clrKey, FALSE))
        ::BitBlt(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, SRCCOPY);
}

static BOOL CCC_BlitToRectChromaGlass(HDC hdcDest, const RECT& rect, HDC hdcSrc, int srcX, int srcY,
    int destW, int destH, int srcW, int srcH, COLORREF clrKey, BYTE contentAlpha, BOOL bStretch)
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
    CCC_SetDibAlphaGlass(pBits, destW, destH, clrKey, contentAlpha);

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

    CCC_InitBufferedPaintTransparent(hBP, destW, destH);
    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    ::GdiAlphaBlend(hdcBuf, 0, 0, destW, destH, dcDib.GetSafeHdc(), 0, 0, destW, destH, bf);
    ::EndBufferedPaint(hBP, TRUE);

    ::SelectObject(dcDib.GetSafeHdc(), hOld);
    ::DeleteObject(hDib);
    dcSrc.Detach();
    return TRUE;
}

static void CCC_BlitChromaGlass(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY,
    COLORREF clrKey, BYTE contentAlpha)
{
    RECT rect = { x, y, x + w, y + h };
    if (!CCC_BlitToRectChromaGlass(hdcDest, rect, hdcSrc, srcX, srcY, w, h, w, h, clrKey, contentAlpha, FALSE))
    {
        if (!CCC_AlphaBlendChromaGlassToHDC(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, clrKey, contentAlpha))
            ::BitBlt(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, SRCCOPY);
    }
}

static void CCC_PaintDialogGlassRect(HDC hdcDest, const RECT& rect)
{
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    if (!hdcDest || w <= 0 || h <= 0) return;

    CDC memDC;
    memDC.CreateCompatibleDC(CDC::FromHandle(hdcDest));
    CBitmap memBmp;
    memBmp.CreateCompatibleBitmap(CDC::FromHandle(hdcDest), w, h);
    CBitmap* pOld = memDC.SelectObject(&memBmp);
    const BYTE rawA = CCC_GetAeroGlassAlpha();
    const BYTE glassA = CCC_ClampDwmGlassAlpha(rawA);
    memDC.FillSolidRect(0, 0, w, h, CCC_DarkenGlassTint(COLOR_DIALOG_BG, glassA));
    CCC_BlitChromaGlass(hdcDest, rect.left, rect.top, w, h, memDC.GetSafeHdc(), 0, 0,
        CCC_AERO_CHROMA_KEY, glassA);
    memDC.SelectObject(pOld);
}

// 既存 HDC（BufferedPaint バッファ等）へ直接アルファ合成。BeginBufferedPaint は呼ばない。
static BOOL CCC_AlphaBlendChromaGlassToHDC(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, COLORREF clrKey, BYTE contentAlpha)
{
    if (!hdcDest || destW <= 0 || destH <= 0) return FALSE;

    void* pBits = nullptr;
    HBITMAP hDib = CCC_CreateAlphaDib32(hdcDest, destW, destH, &pBits);
    if (!hDib || !pBits) return FALSE;

    CDC dcDib, dcSrc;
    dcDib.CreateCompatibleDC(CDC::FromHandle(hdcDest));
    dcSrc.Attach(hdcSrc);
    HGDIOBJ hOld = ::SelectObject(dcDib.GetSafeHdc(), hDib);
    dcDib.FillSolidRect(0, 0, destW, destH, clrKey);
    dcDib.SetStretchBltMode(COLORONCOLOR);
    dcDib.BitBlt(0, 0, destW, destH, &dcSrc, srcX, srcY, SRCCOPY);
    CCC_SetDibAlphaGlass(pBits, destW, destH, clrKey, contentAlpha);

    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    const BOOL ok = ::GdiAlphaBlend(hdcDest, x, y, destW, destH, dcDib.GetSafeHdc(), 0, 0, destW, destH, bf);

    ::SelectObject(dcDib.GetSafeHdc(), hOld);
    ::DeleteObject(hDib);
    dcSrc.Detach();
    return ok;
}

void CCC_BlitChromaDwm(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey)
{
    CCC_BlitChromaNoFlicker(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, clrKey);
}

// Win11: 隙間と同一ガラス下地 + 前景不透明。Win10: クロマ透過 + LWA_ALPHA。
static void CCC_BlitTransparentChroma(HDC hdcDest, int x, int y, int w, int h,
    HDC hdcSrc, int srcX, int srcY, COLORREF clrKey, COLORREF clrGlassBase = COLOR_DIALOG_BG)
{
    if (w <= 0 || h <= 0) return;
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
        CCC_BlitAeroChildComposite(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, clrKey, clrGlassBase);
    else if (CCC_IsAeroEnabled())
        CCC_BlitChromaNoFlicker(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, clrKey);
    else
        CCC_TransparentBltClearDest(hdcDest, x, y, w, h, hdcSrc, srcX, srcY, clrKey);
}

// リストと同型: 均一Tintガラス層 + 前景クロマ層 → DWMへコミット
static BOOL CCC_CommitUniformGlassFromFgMem(CDC& dc, const CRect& rcC, COLORREF clrGlassTint,
    HDC hdcFgMem, int w, int h, BOOL bToPaintBuffer)
{
    if (w <= 0 || h <= 0 || !hdcFgMem) return FALSE;

    const BYTE rawA = CCC_GetAeroGlassAlpha();
    const BYTE glassA = CCC_ClampDwmGlassAlpha(rawA);
    void* pGlassBits = nullptr;
    HBITMAP hGlassDib = CCC_CreateAlphaDib32(dc.GetSafeHdc(), w, h, &pGlassBits);
    CDC memGlass, memTint;
    memGlass.CreateCompatibleDC(&dc);
    memTint.CreateCompatibleDC(&dc);
    CBitmap bmpTint;
    bmpTint.CreateCompatibleBitmap(&dc, w, h);
    HGDIOBJ obGlass = nullptr;
    if (hGlassDib)
    {
        obGlass = ::SelectObject(memGlass.GetSafeHdc(), hGlassDib);
        memGlass.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
        CBitmap* pOldTint = memTint.SelectObject(&bmpTint);
        memTint.FillSolidRect(0, 0, w, h, CCC_DarkenGlassTint(clrGlassTint, glassA));
        CCC_AlphaBlendChromaGlassToHDC(memGlass.GetSafeHdc(), 0, 0, w, h,
            memTint.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY, glassA);
        memTint.SelectObject(pOldTint);
        const RECT rrFull = { 0, 0, w, h };
        CCC_SetDibAlphaGlassRect(pGlassBits, w, h, rrFull, CCC_AERO_CHROMA_KEY, glassA);
        CCC_SetDibChromaTransparent(pGlassBits, w, h, CCC_AERO_CHROMA_KEY);
    }

    void* pFgBits = nullptr;
    HBITMAP hFgDib = CCC_CreateAlphaDib32(dc.GetSafeHdc(), w, h, &pFgBits);
    CDC memFgDib;
    memFgDib.CreateCompatibleDC(&dc);
    HGDIOBJ obFg = nullptr;
    if (hFgDib)
    {
        obFg = ::SelectObject(memFgDib.GetSafeHdc(), hFgDib);
        memFgDib.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
        ::BitBlt(memFgDib.GetSafeHdc(), 0, 0, w, h, hdcFgMem, 0, 0, SRCCOPY);
        CCC_SetDibAlphaFromChroma(pFgBits, w, h, CCC_AERO_CHROMA_KEY);
    }

    BOOL ok = FALSE;
    if (hGlassDib)
        ok = CCC_CommitGlassDibLayers(dc, rcC, w, h, memGlass.GetSafeHdc(), memFgDib.GetSafeHdc(),
            hFgDib != NULL, bToPaintBuffer);
    if (!ok)
        CCC_BlitChromaGlass(dc.GetSafeHdc(), 0, 0, w, h, hdcFgMem, 0, 0, CCC_AERO_CHROMA_KEY, glassA);

    if (obGlass) ::SelectObject(memGlass.GetSafeHdc(), obGlass);
    if (hGlassDib) ::DeleteObject(hGlassDib);
    if (obFg) { ::SelectObject(memFgDib.GetSafeHdc(), obFg); ::DeleteObject(hFgDib); }
    return ok;
}
#endif
// ============================================================================
// アイコンを透明色を抜いて描画する関数
// ============================================================================
static void DrawTransparentIcon(CDC* pDC, CImageList* pIL, int idx, CRect rc, COLORREF mask, COLORREF bgFill = CLR_NONE)
{
    if (!pIL || idx < 0 || !pDC) return;

    IMAGEINFO ii;
    if (!pIL->GetImageInfo(idx, &ii)) return;

    const int w = CRect(ii.rcImage).Width();
    const int h = CRect(ii.rcImage).Height();
    if (w <= 0 || h <= 0) return;

    const int x = rc.left + (rc.Width() - w) / 2;
    const int y = rc.top + (rc.Height() - h) / 2;

    // 不透明 fixer バッファでは転送先を先に不透明単色で埋める（alpha=0 抜け防止）
    if (bgFill != CLR_NONE)
        pDC->FillSolidRect(x, y, w, h, bgFill);

    if (pIL->Draw(pDC, idx, CPoint(x, y), ILD_TRANSPARENT))
        return;

    // ILC_COLOR 向け白キー透過（力業）
    CDC mDC;
    CBitmap b;
    mDC.CreateCompatibleDC(pDC);
    b.CreateCompatibleBitmap(pDC, w, h);
    CBitmap* ob = mDC.SelectObject(&b);
    mDC.FillSolidRect(0, 0, w, h, mask);
    pIL->Draw(&mDC, idx, CPoint(0, 0), ILD_NORMAL);
    ::TransparentBlt(pDC->GetSafeHdc(), x, y, w, h, mDC.GetSafeHdc(), 0, 0, w, h, mask);
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
static void CCC_RemapSolidColorInDC(CDC& dc, const CRect& r, COLORREF clrFrom, COLORREF clrTo)
{
    if (r.Width() <= 0 || r.Height() <= 0) return;
    for (int y = r.top; y < r.bottom; ++y)
    {
        for (int x = r.left; x < r.right; ++x)
        {
            if (dc.GetPixel(x, y) == clrFrom)
                dc.SetPixel(x, y, clrTo);
        }
    }
}
#else
static BOOL CCC_IsChromaBg(COLORREF) { return FALSE; }
#endif

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

static void DrawDecorativeTextShadowLayers(CDC* pDC, const CRect& rect, const CString& str, UINT fmt,
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

    void* pBits = nullptr;
    HBITMAP hDib = CCC_CreateShadowDib32(pDC->GetSafeHdc(), bw, bh, &pBits);
    if (!hDib || !pBits) return;

    CDC dcShadow;
    dcShadow.CreateCompatibleDC(pDC);
    HGDIOBJ hOldBmp = ::SelectObject(dcShadow.GetSafeHdc(), hDib);

    UINT32* px = (UINT32*)pBits;
    const int nPx = bw * bh;
    for (int i = 0; i < nPx; ++i)
        px[i] = 0x00FFFFFFu;

    CFont* pOldFont = dcShadow.SelectObject(pDC->GetCurrentFont());
    dcShadow.SetBkMode(TRANSPARENT);
    dcShadow.SetTextColor(RGB(0, 0, 0));

    CRect tr(pad + max(0, ox), pad + max(0, oy),
        pad + max(0, ox) + textW, pad + max(0, oy) + textH);
    dcShadow.DrawText(str, &tr, fmt);

    std::vector<BYTE> alpha(nPx);
    for (int i = 0; i < nPx; ++i)
    {
        const UINT32 rgb = px[i] & 0x00FFFFFFu;
        if (rgb >= 0x00FEFEFEu)
            alpha[i] = 0;
        else
            alpha[i] = (BYTE)max(0, min(255, 255 - (int)GetRValue(rgb)));
    }

    const int blurR = max(1, (nBlur + 1) / 2);
    CCC_BoxBlurAlpha(alpha, bw, bh, blurR);
    if (nBlur >= 5)
        CCC_BoxBlurAlpha(alpha, bw, bh, max(1, blurR / 2));

    const int tintR = (GetRValue(clrS) * 3 + 32) / 4;
    const int tintG = (GetGValue(clrS) * 3 + 28) / 4;
    const int tintB = (GetBValue(clrS) * 3 + 40) / 4;
    const int peakA = bAeroTrans ? 88 : 112;

    for (int i = 0; i < nPx; ++i)
    {
        if (alpha[i] == 0) { px[i] = 0; continue; }
        const BYTE a = (BYTE)((alpha[i] * peakA) / 255);
        if (a < 2) { px[i] = 0; continue; }
        px[i] = ((UINT32)a << 24) | ((UINT32)tintB << 16) | ((UINT32)tintG << 8) | (UINT32)tintR;
    }

    const BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    ::GdiAlphaBlend(pDC->GetSafeHdc(), rect.left - pad, rect.top - pad, bw, bh,
        dcShadow.GetSafeHdc(), 0, 0, bw, bh, bf);

    dcShadow.SelectObject(pOldFont);
    ::SelectObject(dcShadow.GetSafeHdc(), hOldBmp);
    ::DeleteObject(hDib);
}

static void DrawTextWithShadow(CDC* pDC, const CRect& rect, const CString& str, UINT fmt, COLORREF clrT, COLORREF clrS, int nSD, int nDist, int nBlur, BOOL bSE, COLORREF clrBg, BOOL bAeroTrans = FALSE)
{
    if (bSE)
        DrawDecorativeTextShadowLayers(pDC, rect, str, fmt, clrS, nSD, nDist, nBlur, clrBg, bAeroTrans);
    pDC->SetTextColor(clrT);
    CRect rt = rect;
    pDC->DrawText(str, rt, fmt);
}

static void DrawTextWithGradient(CDC* pDC, const CRect& rect, const CString& str, UINT fmt, COLORREF cS, COLORREF cE, int nDir, COLORREF clrSh, int nSD, int nDist, int nBlur, BOOL bSE, COLORREF clrBg, int nActW = -1, BOOL bFB = FALSE, BOOL bAeroTrans = FALSE)
{
    if (str.IsEmpty()) return;

    if (bSE)
        DrawDecorativeTextShadowLayers(pDC, rect, str, fmt, clrSh, nSD, nDist, nBlur, clrBg, bAeroTrans);

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

static void DrawHeart(CDC* pDC, CRect rc, COLORREF c)
{
    CBrush br(c);
    CPen p(PS_SOLID, 1, c);
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
    CBrush bO(RGB(200, 200, 255));
    CPen pO(PS_SOLID, 1, RGB(150, 150, 255));
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

    CBrush bH(RGB(255, 255, 255));
    pDC->SelectObject(&bH);
    pDC->Ellipse(cx - 2, cy - 3, cx + 2, cy + 1);
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

    CBrush bJ(RGB(255, 100, 100));
    pDC->SelectObject(&bJ);
    pDC->Ellipse(cx - 2, cy - sz - 2, cx + 2, cy - sz + 2);
    pDC->Ellipse(cx - sz * 2 / 3 - 2, cy - sz / 2 - 2, cx - sz * 2 / 3 + 2, cy - sz / 2 + 2);
    pDC->Ellipse(cx + sz * 2 / 3 - 2, cy - sz / 2 - 2, cx + sz * 2 / 3 + 2, cy - sz / 2 + 2);

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
    CPen pen(PS_SOLID, 1, RGB(200, 100, 150));
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

    pDC->SelectObject(ob);
    pDC->SelectObject(op);
}

static void DrawFlower(CDC* pDC, int cx, int cy, int sz, COLORREF c)
{
    CBrush br(c);
    CPen pen(PS_SOLID, 1, c);
    CBrush* ob = pDC->SelectObject(&br);
    CPen* op = pDC->SelectObject(&pen);

    for (int i = 0; i < 5; i++)
    {
        double a = i * 2.0 * 3.14159 / 5.0;
        int px = cx + (int)(sz * 0.6 * cos(a));
        int py = cy + (int)(sz * 0.6 * sin(a));
        pDC->Ellipse(px - sz / 3, py - sz / 3, px + sz / 3, py + sz / 3);
    }
    CBrush bC(RGB(255, 255, 100));
    pDC->SelectObject(&bC);
    pDC->Ellipse(cx - sz / 4, cy - sz / 4, cx + sz / 4, cy + sz / 4);

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
    }
    else
    {
        CRect rc = rt;
        int nH = pDC->DrawText(str, &rc, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
        if (nH <= rt.Height())
        {
            CRect rd = rt;
            rd.top += (rt.Height() - nH) / 2;
            pDC->DrawText(str, &rd, DT_CENTER | DT_WORDBREAK);
        }
        else
        {
            pDC->SelectObject(po);
            fs.DeleteObject();
            BOOL bP = FALSE;

            while (tH > 6)
            {
                tH--;
                lf.lfHeight = -tH;
                CFont ft;
                ft.CreateFontIndirect(&lf);
                pDC->SelectObject(&ft);

                CRect ry = rt;
                int nh = pDC->DrawText(str, &ry, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
                if (nh <= rt.Height() && ry.Width() <= rt.Width())
                {
                    CRect rd = rt;
                    rd.top += (rt.Height() - nh) / 2;
                    pDC->DrawText(str, &rd, DT_CENTER | DT_WORDBREAK);
                    pDC->SelectObject(po);
                    ft.DeleteObject();
                    bP = TRUE;
                    break;
                }
                pDC->SelectObject(po);
                ft.DeleteObject();
            }

            if (!bP)
            {
                lf.lfHeight = -6;
                CFont fm;
                fm.CreateFontIndirect(&lf);
                pDC->SelectObject(&fm);
                pDC->DrawText(str, &rt, DT_CENTER | DT_WORDBREAK | DT_VCENTER);
                pDC->SelectObject(po);
                fm.DeleteObject();
            }
            return;
        }
    }
    pDC->SelectObject(po);
    fs.DeleteObject();
}

static void DrawFittedSingleLineDecorativeText(CDC& dc, const CRect& rect, const CString& str, UINT fmt,
    BOOL bGrad, COLORREF cGS, COLORREF cGE, int nDir,
    COLORREF clrSh, int nSD, int nDist, int nBlur, BOOL bSE, COLORREF clrBg, BOOL bPreferWide, BOOL bAeroTrans = FALSE)
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
    if (bSE && nDist > 0 && nBlur > 0)
    {
        const double rad = nSD * 3.14159265358979323846 / 180.0;
        shadowPadX = (int)floor(nDist * cos(rad) + (nBlur + 1) / 2 + 0.5);
        shadowPadY = (int)floor(nDist * sin(rad) + (nBlur + 1) / 2 + 0.5);
    }

    CRect rectDraw = rect;
    if (bSE && shadowPadX > 0)
        rectDraw.right = (std::max)(rectDraw.left + 1, rectDraw.right - shadowPadX);

    LOGFONT lfCur = {};
    if (CFont* pCF = dc.GetCurrentFont())
        pCF->GetLogFont(&lfCur);
    const int italicMargin = lfCur.lfItalic ? (abs(lfCur.lfHeight) / 2) : 0;

    const int nBudgetW = (std::max)(1, rectDraw.Width() - 3);
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
        DrawDecorativeTextShadowLayers(&dc, rl, str, fitFmt, clrSh, nSD, nDist, nBlur, clrBg, bAeroTrans, scaleX, scaleY);
    if (bGrad) DrawTextWithGradient(&dc, rl, str, fitFmt, cGS, cGE, nDir, clrSh, nSD, nDist, nBlur, FALSE, clrBg, sz.cx, FALSE, bAeroTrans);
    else DrawTextWithShadow(&dc, rl, str, fitFmt, RGB(0, 0, 0), clrSh, nSD, nDist, nBlur, FALSE, clrBg, bAeroTrans);
    dc.RestoreDC(-1);
}

static void DrawListSubitemCellText(CDC* pDC, const CString& str, const CRect& rcInner)
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

    const UINT uDT = DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX;

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

    XFORM xf = { scale, 0.0f, 0.0f, 1.0f, (float)rcInner.left, (float)yTop };
    pDC->SetWorldTransform(&xf);

    int mCW = (tm.tmMaxCharWidth > 0) ? (int)tm.tmMaxCharWidth : (int)tm.tmAveCharWidth;
    CRect rl(0, 0, sz.cx + (std::max)(16, mCW + 4), drawH + 6);
    pDC->DrawText(str, &rl, uDT);
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

static COLORREF CCC_BlendOnColor(COLORREF dst, COLORREF src, BYTE srcAlpha)
{
    const int sa = (int)srcAlpha;
    const int da = 255 - sa;
    const int r = (GetRValue(src) * sa + GetRValue(dst) * da) / 255;
    const int g = (GetGValue(src) * sa + GetGValue(dst) * da) / 255;
    const int b = (GetBValue(src) * sa + GetBValue(dst) * da) / 255;
    return RGB(r, g, b);
}

static COLORREF CCC_BlendOnBlack(COLORREF clr, BYTE alpha)
{
    return CCC_BlendOnColor(RGB(0, 0, 0), clr, alpha);
}

static void FillRectAlpha(CDC* pDC, const CRect& rc, COLORREF clr, BYTE alpha)
{
    if (rc.Width() <= 0 || rc.Height() <= 0) return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsWin11())
        alpha = CCC_ClampDwmGlassAlpha(alpha);
#endif
    CDC mDC;
    CBitmap mB;
    mDC.CreateCompatibleDC(pDC);
    mB.CreateCompatibleBitmap(pDC, rc.Width(), rc.Height());
    CBitmap* ob = mDC.SelectObject(&mB);

    mDC.FillSolidRect(0, 0, rc.Width(), rc.Height(), clr);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    ::AlphaBlend(pDC->GetSafeHdc(), rc.left, rc.top, rc.Width(), rc.Height(), mDC.GetSafeHdc(), 0, 0, rc.Width(), rc.Height(), bf);

    mDC.SelectObject(ob);
    mB.DeleteObject();
    mDC.DeleteDC();
}

#if CCUSTOM_AERO_SUPPORT
static BOOL CCC_DeferListToOpaquePaint(HWND hWnd)
{
    if (!hWnd || !CCC_IsAeroEnabled() || !CCC_IsWin11() || !CCC_IsBlurDialogChild(hWnd))
        return FALSE;
    if (CCC_UseListCtrlRowGlass(hWnd) || CCC_UseListBoxRowGlass(hWnd))
        return FALSE;
    if (CCC_UseComboGlass(hWnd) || CCC_UseEditGlass(hWnd))
        return FALSE;
    return TRUE;
}
#endif

static const CCC_RowGlassStyle* CCC_LookupRowStyle(const std::map<int, CCC_RowGlassStyle>& styles, int nRow)
{
    std::map<int, CCC_RowGlassStyle>::const_iterator it = styles.find(nRow);
    if (it == styles.end() || !it->second.bValid)
        return NULL;
    return &it->second;
}

static void CCC_ResolveRowGlassStyle(HWND hWnd, HWND hPaintWnd, BOOL bAeroMode, COLORREF bg, BYTE alpha,
    BOOL bUnderlayBlack, const CCC_RowGlassStyle* pCustom,
    COLORREF& drawBg, BYTE& drawAlpha, BOOL& bGlass, BOOL& bBlurChild, BOOL& bLegacyAeroGlass)
{
    drawBg = bg;
    drawAlpha = alpha;
    bGlass = FALSE;
    bBlurChild = FALSE;
    bLegacyAeroGlass = bAeroMode;

#if CCUSTOM_AERO_SUPPORT
    const BOOL bDialogAero = CCC_IsAeroEnabled();
    bBlurChild = (hWnd && CCC_IsBlurDialogChild(hWnd));
    const BOOL bComboDrop = (hPaintWnd && hWnd && hPaintWnd != hWnd);
    const BOOL bDropdownGlass = bComboDrop && bBlurChild && bDialogAero;
    bLegacyAeroGlass = bDialogAero && ((bAeroMode && !bBlurChild) || bBlurChild || bDropdownGlass);
#else
    UNREFERENCED_PARAMETER(hPaintWnd);
#endif
    bGlass = bLegacyAeroGlass;

    if (pCustom && pCustom->bValid)
    {
        drawBg = pCustom->clrBg;
        if (pCustom->nAlpha > 0)
            drawAlpha = pCustom->nAlpha;
        if (pCustom->bUseGlass)
            bGlass = TRUE;
    }

    UNREFERENCED_PARAMETER(bUnderlayBlack);
}

static COLORREF CCC_RowSolidFillColor(HWND hWnd, HWND hPaintWnd, BOOL bAeroMode, COLORREF bg, BYTE alpha,
    BOOL bUnderlayBlack, const CCC_RowGlassStyle* pCustom)
{
    COLORREF drawBg;
    BYTE drawAlpha;
    BOOL bGlass, bBlurChild, bLegacyAeroGlass;
    CCC_ResolveRowGlassStyle(hWnd, hPaintWnd, bAeroMode, bg, alpha, bUnderlayBlack, pCustom,
        drawBg, drawAlpha, bGlass, bBlurChild, bLegacyAeroGlass);

    if (!bGlass)
        return drawBg;
#if CCUSTOM_AERO_SUPPORT
    if (bBlurChild)
    {
        // 不透明 fixer 用: 黒ブレンドは意図的な暗化ではないため、元の行色をそのまま使う
        UNREFERENCED_PARAMETER(bUnderlayBlack);
        return drawBg;
    }
#endif
    return drawBg;
}

static void CCC_DrawRowGlassBackground(CDC* pDC, const CRect& r, HWND hWnd, BOOL bAeroMode,
    COLORREF bg, BYTE alpha, BOOL bUnderlayBlack, const CCC_RowGlassStyle* pCustom, HWND hPaintWnd = NULL)
{
    if (!pDC || r.Width() <= 0 || r.Height() <= 0) return;
    if (!hPaintWnd) hPaintWnd = hWnd;

    COLORREF drawBg;
    BYTE drawAlpha;
    BOOL bGlass, bBlurChild, bLegacyAeroGlass;
    CCC_ResolveRowGlassStyle(hWnd, hPaintWnd, bAeroMode, bg, alpha, bUnderlayBlack, pCustom,
        drawBg, drawAlpha, bGlass, bBlurChild, bLegacyAeroGlass);

    if (bGlass)
    {
#if CCUSTOM_AERO_SUPPORT
        if (bBlurChild)
        {
            if (CCC_UseListCtrlRowGlass(hWnd))
                FillRectAlpha(pDC, r, drawBg, drawAlpha);
            else
            {
                const COLORREF solid = CCC_RowSolidFillColor(hWnd, hPaintWnd, bAeroMode, bg, alpha, bUnderlayBlack, pCustom);
                pDC->FillSolidRect(&r, solid);
            }
        }
        else
#endif
        {
            if (bUnderlayBlack && bLegacyAeroGlass)
                pDC->FillSolidRect(&r, RGB(0, 0, 0));
            FillRectAlpha(pDC, r, drawBg, drawAlpha);
        }
    }
    else
        pDC->FillSolidRect(&r, drawBg);
}

static void CCC_SetRowStyleMap(std::map<int, CCC_RowGlassStyle>& styles, int nRow, COLORREF clrBg, BYTE nAlpha, BOOL bUseGlass)
{
    if (nRow < 0) return;
    CCC_RowGlassStyle s;
    s.bValid = TRUE;
    s.clrBg = clrBg;
    s.nAlpha = nAlpha;
    s.bUseGlass = bUseGlass;
    styles[nRow] = s;
}

// ============================================================================
// 子コントロールを一括でサブクラス化する処理
// ============================================================================
template<typename DlgBase>
static void DoSubclassChildControls(DlgBase* pDlg)
{
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
            if (pWnd && CCC_UseEditGlass(pWnd->GetSafeHwnd()))
            {
                pDC->SetBkMode(TRANSPARENT);
                pDC->SetTextColor(COLOR_EDIT_TEXT);
                return (HBRUSH)GetStockObject(NULL_BRUSH);
            }
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

    LOGFONT lf;
    pF->GetLogFont(&lf);
    lf.lfWeight = FW_BOLD;

    if (m_fontBold.GetSafeHandle()) m_fontBold.DeleteObject();
    m_fontBold.CreateFontIndirect(&lf);
    CEdit::SetFont(&m_fontBold);
}

HBRUSH CCustomEdit::CtlColor(CDC* pDC, UINT)
{
    pDC->SetTextColor(COLOR_EDIT_TEXT);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseEditGlass(m_hWnd))
    {
        pDC->SetBkMode(TRANSPARENT);
        return (HBRUSH)::GetStockObject(HOLLOW_BRUSH);
    }
#endif
    pDC->SetBkColor(COLOR_EDIT_BG);
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

void CCustomEdit::DrawClientText(CDC& dc, const CRect& r)
{
    CString text;
    GetWindowText(text);

    CFont* pFont = GetFont();
    CFont* pOld = pFont ? dc.SelectObject(pFont) : nullptr;

    const BOOL bGlass = CCC_UseEditGlass(m_hWnd);
    dc.SetTextColor(COLOR_EDIT_TEXT);
    if (bGlass)
        dc.SetBkMode(TRANSPARENT);
    else
    {
        dc.SetBkColor(COLOR_EDIT_BG);
        dc.SetBkMode(OPAQUE);
    }

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

void CCustomEdit::PaintGlassClient(CDC& dc, BOOL bToPaintBuffer)
{
#if CCUSTOM_AERO_SUPPORT
    CRect rcC;
    GetClientRect(&rcC);
    const int w = rcC.Width();
    const int h = rcC.Height();
    if (w <= 0 || h <= 0) return;

    CDC memFG;
    memFG.CreateCompatibleDC(&dc);
    CBitmap bmpFG;
    bmpFG.CreateCompatibleBitmap(&dc, w, h);
    CBitmap* pOldFG = memFG.SelectObject(&bmpFG);
    memFG.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
    DrawClientText(memFG, rcC);

    CCC_CommitUniformGlassFromFgMem(dc, rcC, COLOR_EDIT_BG, memFG.GetSafeHdc(), w, h, bToPaintBuffer);

    memFG.SelectObject(pOldFG);
#endif
}

void CCustomEdit::SyncGlassCaret()
{
#if CCUSTOM_AERO_SUPPORT
    if (!GetSafeHwnd() || !CCC_UseEditGlass(m_hWnd) || !m_bHasFocus)
        return;

    CString text;
    GetWindowText(text);
    int nStart = 0, nEnd = 0;
    GetSel(nStart, nEnd);
    const int len = text.GetLength();
    nStart = max(0, min(nStart, len));

    CClientDC dc(this);
    CFont* pFont = GetFont();
    CFont* pOld = pFont ? dc.SelectObject(pFont) : nullptr;
    TEXTMETRIC tm = {};
    dc.GetTextMetrics(&tm);
    CSize sz = dc.GetTextExtent(text.Left(nStart));
    if (pOld) dc.SelectObject(pOld);

    CRect rc;
    GetClientRect(&rc);
    const int caretH = max(12, tm.tmHeight);
    const int y = rc.top + max(0, (rc.Height() - caretH) / 2);
    const int x = rc.left + 3 + sz.cx;

    ::HideCaret(m_hWnd);
    ::DestroyCaret();
    if (::CreateCaret(m_hWnd, NULL, 2, caretH))
    {
        ::SetCaretPos(x, y);
        ::ShowCaret(m_hWnd);
    }
#endif
}

void CCustomEdit::RepaintClient()
{
    if (!GetSafeHwnd())
        return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseEditGlass(m_hWnd))
    {
        CCC_RepaintGlassHwnd(m_hWnd);
        return;
    }
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
    if (CCC_UseEditGlass(m_hWnd))
        return 0;
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
    if (CCC_UseEditGlass(m_hWnd))
    {
        CPaintDC dc(this);
        PaintGlassClient(dc, FALSE);
        return;
    }
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
    if (CCC_UseEditGlass(m_hWnd))
    {
        UNREFERENCED_PARAMETER(pDC);
        return TRUE;
    }
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
        DrawStar(&dc, r.right - 8, r.top + 8, 3, RGB(255, 215, 0));
        DrawStar(&dc, r.left + 8, r.top + 8, 2, RGB(255, 240, 150));
        DrawStar(&dc, r.right - 8, r.bottom - 8, 2, RGB(255, 240, 150));
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
        CDC dc;
        dc.Attach(hDC);
        CRect r;
        GetClientRect(&r);
#if CCUSTOM_AERO_SUPPORT
        if (CCC_UseEditGlass(m_hWnd))
        {
            PaintGlassClient(dc, TRUE);
            dc.Detach();
            return 1;
        }
#endif
        dc.FillSolidRect(&r, COLOR_EDIT_BG);
        DrawClientText(dc, r);
        dc.Detach();
        return 1;
    }
    return 0;
}

void CCustomEdit::OnEnUpdate()
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseEditGlass(m_hWnd))
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
        SyncGlassCaret();
        return;
    }
#endif
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
#if CCUSTOM_AERO_SUPPORT
    if (bShow)
        RepaintClient();
#endif
    UNREFERENCED_PARAMETER(nStatus);
}

void CCustomEdit::OnSetFocus(CWnd* p)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseEditGlass(m_hWnd))
    {
        m_bHasFocus = TRUE;
        SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        ::InvalidateRect(m_hWnd, NULL, FALSE);
        SyncGlassCaret();
        return;
    }
#endif
    m_bHasFocus = TRUE;
    CEdit::OnSetFocus(p);
    SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
#if CCUSTOM_AERO_SUPPORT
    Invalidate(FALSE);
    ScheduleOpaqueRepaint();
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
        SetTimer(kEditOpaqueTimerId, 50, NULL);
#else
    Invalidate(FALSE);
#endif
}

void CCustomEdit::OnKillFocus(CWnd* p)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseEditGlass(m_hWnd))
    {
        m_bHasFocus = FALSE;
        ::HideCaret(m_hWnd);
        ::DestroyCaret();
        SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        ::InvalidateRect(m_hWnd, NULL, FALSE);
        return;
    }
#endif
    CEdit::OnKillFocus(p);
    m_bHasFocus = FALSE;
    KillTimer(kEditOpaqueTimerId);
    SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
#if CCUSTOM_AERO_SUPPORT
    Invalidate(FALSE);
    ScheduleOpaqueRepaint();
#endif
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
    m_strCachedText(_T("")), m_strText(_T("")),
    m_backstoreW(0), m_backstoreH(0), m_bAeroMode(FALSE)
{}

CCustomStatic::~CCustomStatic()
{
    if (m_font.GetSafeHandle()) m_font.DeleteObject();
    if (m_memBackstore.GetSafeHandle()) m_memBackstore.DeleteObject();
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

    const BOOL bTrans = CCC_UseTransparentPaint(m_hWnd, m_bAeroMode);
    const COLORREF clrBg = COLOR_DIALOG_BG;

    for (size_t i = 0; i < segs.size(); i++)
    {
        LOGFONT lt = lf;
        lt.lfHeight = -max(6, h + segs[i].nFontSizeOffset);
        lt.lfWidth = w;
        if (segs[i].bBold) lt.lfWeight = FW_BOLD;
        if (segs[i].bItalic) lt.lfItalic = TRUE;
        CFont ft; ft.CreateFontIndirect(&lt);
        CFont* po = pDC->SelectObject(&ft);
        CSize sz = pDC->GetTextExtent(segs[i].text);
        CRect sr = { xP, rect.top, xP + sz.cx, rect.bottom };
        COLORREF tc = segs[i].bHasColor ? segs[i].clrText : RGB(0, 0, 0);
        if (bTrans && tc == RGB(0, 0, 0)) tc = RGB(2, 2, 2);

        if (m_bGradEnable) DrawTextWithGradient(pDC, sr, segs[i].text, DT_VCENTER | DT_SINGLELINE | DT_LEFT, m_clrGradStart, m_clrGradEnd, m_nGradDirection, m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur, m_bShadowEnable, clrBg, sz.cx, FALSE, bTrans);
        else DrawTextWithShadow(pDC, sr, segs[i].text, DT_VCENTER | DT_SINGLELINE | DT_LEFT, tc, m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur, m_bShadowEnable, clrBg, bTrans);

        xP += sz.cx;
        pDC->SelectObject(po); ft.DeleteObject();
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
    if (CCC_UseTransparentPaint(m_hWnd, m_bAeroMode)) return TRUE;
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

    const BOOL bTrans = CCC_UseTransparentPaint(m_hWnd, m_bAeroMode);
    memDC.FillSolidRect(&rect, COLOR_DIALOG_BG);

    if (m_strText.IsEmpty())
    {
        if (bTrans)
        {
            CCC_RemapSolidColorInDC(memDC, rect, COLOR_DIALOG_BG, CCC_AERO_CHROMA_KEY);
            CCC_BlitTransparentChroma(dc.GetSafeHdc(), 0, 0, rw, rh, memDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
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
    rectWithMargin.DeflateRect(1, 1);

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

    const int kMinHeight = 6;
    const int baseHeight = abs(lfB.lfHeight);
    int finalHeight = 0;
    int finalWidth = 0;
    CSize szFinal;

    const BOOL bNeedRecalc = (strText != m_strCachedText) ||
        (m_nCachedHeight == 0) ||
        (m_rectCached != rect);

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
                CFont fontTry;
                fontTry.CreateFontIndirect(&lfTry);
                CFont* pOld = memDC.SelectObject(&fontTry);
                TEXTMETRIC tm;
                memDC.GetTextMetrics(&tm);
                memDC.SelectObject(pOld);
                fontTry.DeleteObject();

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

            if (m_bPreferWideMode && szFinal.cx < rectWithMargin.Width())
            {
                const int startWidth = (finalWidth > 0) ? finalWidth : baseWidth;
                const int maxWidth = startWidth * 3;
                for (int w = startWidth; w <= maxWidth; w++)
                {
                    CSize sizeTry = MeasureText(finalHeight, w);
                    if (sizeTry.cx <= rectWithMargin.Width() && sizeTry.cy <= rectWithMargin.Height())
                    {
                        finalWidth = w;
                        szFinal = sizeTry;
                    }
                    else break;
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
            if (m_bShadowEnable && m_nShadowDistance > 0 && m_nShadowBlur > 0)
            {
                const double rad = m_nShadowDirection * 3.14159265358979323846 / 180.0;
                shadowPadX = (int)floor(m_nShadowDistance * cos(rad) + (m_nShadowBlur + 1) / 2 + 0.5);
                shadowPadY = (int)floor(m_nShadowDistance * sin(rad) + (m_nShadowBlur + 1) / 2 + 0.5);
            }
            UNREFERENCED_PARAMETER(shadowPadY);

            const int availW = (std::max)(1, rectWithMargin.Width() - shadowPadX - 3);
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

            if (scaleX >= 0.98f && m_bPreferWideMode && szFinal.cx < availW)
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
                for (int w = baseWidth; w <= maxWidth; w++)
                {
                    CSize sizeTry = MeasureText(finalHeight, w);
                    if (sizeTry.cx <= availW && sizeTry.cy <= rectWithMargin.Height())
                    {
                        finalWidth = w;
                        szFinal = sizeTry;
                    }
                    else break;
                }
            }

            m_fCachedScaleX = scaleX;
        }

        m_strCachedText = strText;
        m_nCachedHeight = finalHeight;
        m_nCachedWidth = finalWidth;
        m_rectCached = rect;
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
            DrawFittedSingleLineDecorativeText(memDC, rect, strText, fmt,
                m_bGradEnable, m_clrGradStart, m_clrGradEnd, m_nGradDirection,
                m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
                m_bShadowEnable, clrBg, FALSE, bTrans);
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
        CCC_BlitTransparentChroma(dc.GetSafeHdc(), 0, 0, rw, rh, memDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
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
        CCC_InvalidateBlurParent(m_hWnd, m_bAeroMode);
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

// ============================================================================
// CCustomStatic クラスの描画計算と解析処理（全実装）
// ============================================================================

// セグメント化されたテキストの表示サイズを正確に測定する関数
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

        CFont ft;
        ft.CreateFontIndirect(&lt);
        CFont* po = pDC->SelectObject(&ft);

        CSize sz = pDC->GetTextExtent(segs[i].text);
        tot.cx += sz.cx;
        if (sz.cy > tot.cy) tot.cy = sz.cy;

        pDC->SelectObject(po);
        ft.DeleteObject();
    }
    return tot;
}


// ============================================================================
// 行スタイル API（ListBox / Combo / ListCtrl 共通）
// ============================================================================
const CCC_RowGlassStyle* CCustomListBox::LookupRowStyle(int nRow) const
{
    return CCC_LookupRowStyle(m_rowStyles, nRow);
}

void CCustomListBox::SetRowStyle(int nRow, COLORREF clrBg, BYTE nAlpha, BOOL bUseGlass)
{
    CCC_SetRowStyleMap(m_rowStyles, nRow, clrBg, nAlpha, bUseGlass);
    if (GetSafeHwnd() && nRow >= 0 && nRow < GetCount())
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
}

void CCustomListBox::ClearRowStyle(int nRow)
{
    m_rowStyles.erase(nRow);
    if (GetSafeHwnd() && nRow >= 0 && nRow < GetCount())
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
}

void CCustomListBox::ClearAllRowStyles()
{
    m_rowStyles.clear();
    if (GetSafeHwnd()) Invalidate(FALSE);
}

BOOL CCustomListBox::GetRowStyle(int nRow, CCC_RowGlassStyle* pOut) const
{
    if (!pOut) return FALSE;
    const CCC_RowGlassStyle* p = LookupRowStyle(nRow);
    if (!p) return FALSE;
    *pOut = *p;
    return TRUE;
}

const CCC_RowGlassStyle* CCustomComboBox::LookupRowStyle(int nRow) const
{
    return CCC_LookupRowStyle(m_rowStyles, nRow);
}

void CCustomComboBox::SetRowStyle(int nRow, COLORREF clrBg, BYTE nAlpha, BOOL bUseGlass)
{
    CCC_SetRowStyleMap(m_rowStyles, nRow, clrBg, nAlpha, bUseGlass);
    if (GetSafeHwnd() && nRow >= 0 && nRow < GetCount())
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
}

void CCustomComboBox::ClearRowStyle(int nRow)
{
    m_rowStyles.erase(nRow);
    if (GetSafeHwnd() && nRow >= 0 && nRow < GetCount())
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
}

void CCustomComboBox::ClearAllRowStyles()
{
    m_rowStyles.clear();
    if (GetSafeHwnd()) Invalidate(FALSE);
}

BOOL CCustomComboBox::GetRowStyle(int nRow, CCC_RowGlassStyle* pOut) const
{
    if (!pOut) return FALSE;
    const CCC_RowGlassStyle* p = LookupRowStyle(nRow);
    if (!p) return FALSE;
    *pOut = *p;
    return TRUE;
}

const CCC_RowGlassStyle* CCustomListCtrl::LookupRowStyle(int nRow) const
{
    return CCC_LookupRowStyle(m_rowStyles, nRow);
}

void CCustomListCtrl::SetRowStyle(int nRow, COLORREF clrBg, BYTE nAlpha, BOOL bUseGlass)
{
    CCC_SetRowStyleMap(m_rowStyles, nRow, clrBg, nAlpha, bUseGlass);
    if (GetSafeHwnd() && nRow >= 0 && nRow < GetItemCount())
        RedrawItems(nRow, nRow);
}

void CCustomListCtrl::ClearRowStyle(int nRow)
{
    m_rowStyles.erase(nRow);
    if (GetSafeHwnd() && nRow >= 0 && nRow < GetItemCount())
        RedrawItems(nRow, nRow);
}

void CCustomListCtrl::ClearAllRowStyles()
{
    m_rowStyles.clear();
    if (GetSafeHwnd()) Invalidate(FALSE);
}

BOOL CCustomListCtrl::GetRowStyle(int nRow, CCC_RowGlassStyle* pOut) const
{
    if (!pOut) return FALSE;
    const CCC_RowGlassStyle* p = LookupRowStyle(nRow);
    if (!p) return FALSE;
    *pOut = *p;
    return TRUE;
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
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_TIMER()
    ON_CONTROL_REFLECT(LBN_SELCHANGE, &CCustomListBox::OnSelChange)
    ON_MESSAGE(CCC_WM_POST_OPAQUE_PAINT, OnPostOpaquePaint)
END_MESSAGE_MAP()

static const UINT_PTR kListBoxGlassCoalesceTimerId = 4110;

CCustomListBox::CCustomListBox() : m_bAutoDelete(FALSE), m_bAeroMode(FALSE), m_bInOpaqueFullPaint(FALSE), m_nHotItem(-1)
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
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListBoxRowGlass(m_hWnd))
    {
        pDC->SetBkMode(TRANSPARENT);
        return (HBRUSH)::GetStockObject(HOLLOW_BRUSH);
    }
#endif
    pDC->SetBkColor(COLOR_LIST_BG);
    pDC->SetTextColor(RGB(0, 0, 0));
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

void CCustomListBox::OnPaint()
{
    CPaintDC dc(this);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListBoxRowGlass(m_hWnd))
    {
        PaintGlassClient(dc);
        return;
    }
    if (CCC_DeferListToOpaquePaint(m_hWnd))
    {
        PaintOpaqueClient(dc);
        return;
    }
#endif
    PaintListBoxClientBuffered(dc);
}

void CCustomListBox::DrawListBoxRow(CDC& dc, int i)
{
    if (i < 0) return;
    CRect rcClient;
    GetClientRect(&rcClient);
    CRect itemRc;
    if (!GetItemRect(i, &itemRc)) return;
    if (itemRc.right < rcClient.right) itemRc.right = rcClient.right;

    DRAWITEMSTRUCT dis = {};
    dis.CtlType = ODT_LISTBOX;
    dis.CtlID = (UINT)GetDlgCtrlID();
    dis.itemID = (UINT)i;
    dis.itemAction = ODA_DRAWENTIRE;
    dis.hwndItem = m_hWnd;
    dis.hDC = dc.GetSafeHdc();
    dis.rcItem = itemRc;
    dis.itemState = (GetSel(i) > 0) ? ODS_SELECTED : 0;
    if (!IsWindowEnabled()) dis.itemState |= ODS_DISABLED;
    DrawItem(&dis);
}

void CCustomListBox::PaintListBoxClientBuffered(CDC& dc)
{
    CRect r;
    GetClientRect(&r);
    if (r.IsRectEmpty()) return;

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    params.dwFlags = BPPF_ERASE;
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &r, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP)
    {
        PaintListBoxClient(dc);
        return;
    }
    CDC buf;
    buf.Attach(hdcBuf);
    PaintListBoxClient(buf);
    buf.Detach();
    ::EndBufferedPaint(hBP, TRUE);
}

void CCustomListBox::RepaintListBoxRows(int iFirst, int iLast)
{
    if (!GetSafeHwnd() || iFirst < 0 || iLast < iFirst) return;

    CRect rcClient;
    GetClientRect(&rcClient);
    CRect paintRc;
    BOOL bGot = FALSE;
    for (int i = iFirst; i <= iLast; ++i)
    {
        CRect r;
        if (!GetItemRect(i, &r)) continue;
        if (r.right < rcClient.right) r.right = rcClient.right;
        if (!bGot) { paintRc = r; bGot = TRUE; }
        else paintRc.UnionRect(&paintRc, &r);
    }
    if (!bGot) return;

    CClientDC dc(this);
    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &paintRc, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (hdcBuf && hBP)
    {
        CDC buf;
        buf.Attach(hdcBuf);
        for (int i = iFirst; i <= iLast; ++i)
            DrawListBoxRow(buf, i);
        buf.Detach();
        ::EndBufferedPaint(hBP, TRUE);
        return;
    }
    for (int i = iFirst; i <= iLast; ++i)
        DrawListBoxRow(dc, i);
}

void CCustomListBox::PaintOpaqueIntoBuffer(HDC hdcBuf)
{
    if (!hdcBuf || !m_hWnd) return;
    OnPrintClient((WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
}

void CCustomListBox::PaintOpaqueClient(CDC& dc)
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
        OnPrintClient((WPARAM)dc.GetSafeHdc(), PRF_CLIENT | PRF_ERASEBKGND);
        return;
    }
    OnPrintClient((WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
    ::BufferedPaintMakeOpaque(hBP, &r);
    ::EndBufferedPaint(hBP, TRUE);
}

void CCustomListBox::RepaintOpaqueIfNeeded()
{
#if CCUSTOM_AERO_SUPPORT
    if (!CCC_DeferListToOpaquePaint(m_hWnd) || m_bInOpaqueFullPaint) return;
    m_bInOpaqueFullPaint = TRUE;
    CClientDC dc(this);
    PaintOpaqueClient(dc);
    m_bInOpaqueFullPaint = FALSE;
#endif
}

void CCustomListBox::PaintListBoxClient(CDC& dc)
{
    CRect rc;
    GetClientRect(&rc);
    if (rc.IsRectEmpty()) return;

    dc.FillSolidRect(&rc, COLOR_LIST_BG);

    int nFirst = 0, nLast = -1;
    CCC_LbVisibleRange(m_hWnd, rc, nFirst, nLast);
    if (nLast < nFirst) return;

    for (int i = nFirst; i <= nLast; ++i)
        DrawListBoxRow(dc, i);

    CRect rLast;
    if (GetItemRect(nLast, &rLast) && rLast.bottom < rc.bottom)
    {
        CRect fill(rc.left, rLast.bottom, rc.right, rc.bottom);
        dc.FillSolidRect(&fill, CCC_ListZebraBg(nLast + 1));
    }
}

void CCustomListBox::RepaintClient()
{
    if (!GetSafeHwnd()) return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListBoxRowGlass(m_hWnd))
    {
        RequestGlassRepaint(TRUE);
        return;
    }
    if (CCC_DeferListToOpaquePaint(m_hWnd))
    {
        RepaintOpaqueIfNeeded();
        return;
    }
#endif
    Invalidate(FALSE);
}

void CCustomListBox::OnSelChange()
{
    RepaintClient();
}

void CCustomListBox::OnLButtonDown(UINT nFlags, CPoint point)
{
    CListBox::OnLButtonDown(nFlags, point);
}

void CCustomListBox::OnLButtonUp(UINT nFlags, CPoint point)
{
    CListBox::OnLButtonUp(nFlags, point);
}

void CCustomListBox::OnMouseMove(UINT nFlags, CPoint point)
{
    UNREFERENCED_PARAMETER(nFlags);
    UpdateHotItem(CCC_ListBoxHotItemFromPoint(this, point));
    TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, m_hWnd, 0 };
    TrackMouseEvent(&t);
}

void CCustomListBox::OnMouseLeave()
{
    UpdateHotItem(-1);
    CListBox::OnMouseLeave();
}

void CCustomListBox::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    CListBox::OnVScroll(nSBCode, nPos, pScrollBar);
    UpdateHotItemFromCursor();
}

BOOL CCustomListBox::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    const BOOL r = CListBox::OnMouseWheel(nFlags, zDelta, pt);
    UpdateHotItemFromCursor();
    return r;
}

void CCustomListBox::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kListBoxGlassCoalesceTimerId)
    {
        KillTimer(kListBoxGlassCoalesceTimerId);
        if (GetSafeHwnd())
            Invalidate(FALSE);
        return;
    }
    CListBox::OnTimer(nIDEvent);
}

void CCustomListBox::RequestGlassRepaint(BOOL bImmediate)
{
    if (!GetSafeHwnd()) return;
    if (bImmediate)
    {
        KillTimer(kListBoxGlassCoalesceTimerId);
        Invalidate(FALSE);
        UpdateWindow();
        return;
    }
    SetTimer(kListBoxGlassCoalesceTimerId, 16, NULL);
}

void CCustomListBox::RefreshRows(int iFirst, int iLast)
{
    UNREFERENCED_PARAMETER(iFirst);
    UNREFERENCED_PARAMETER(iLast);
    RepaintClient();
}

void CCustomListBox::UpdateHotItem(int n)
{
    if (m_nHotItem == n) return;
    const int o = m_nHotItem;
    m_nHotItem = n;
    if (!GetSafeHwnd()) return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListBoxRowGlass(m_hWnd))
    {
        RequestGlassRepaint(TRUE);
        return;
    }
    if (CCC_DeferListToOpaquePaint(m_hWnd))
    {
        RepaintOpaqueIfNeeded();
        return;
    }
#endif
    if (o < 0 && n < 0) return;
    int iFirst = o, iLast = n;
    if (o < 0) iFirst = n;
    if (n < 0) iLast = o;
    if (iFirst > iLast) { const int t = iFirst; iFirst = iLast; iLast = t; }
    RepaintListBoxRows(iFirst, iLast);
}

void CCustomListBox::UpdateHotItemFromCursor()
{
    if (!GetSafeHwnd()) return;
    CPoint pt;
    if (!GetCursorPos(&pt)) return;
    ScreenToClient(&pt);
    UpdateHotItem(CCC_ListBoxHotItemFromPoint(this, pt));
}

LRESULT CCustomListBox::OnPostOpaquePaint(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListBoxRowGlass(m_hWnd))
        return 0;
    if (CCC_DeferListToOpaquePaint(m_hWnd))
    {
        CClientDC dc(this);
        PaintOpaqueClient(dc);
    }
#endif
    return 0;
}

LRESULT CCustomListBox::OnPrintClient(WPARAM wParam, LPARAM)
{
    CDC* pDC = CDC::FromHandle((HDC)wParam);
    if (!pDC || !GetSafeHwnd()) return 0;

#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListBoxRowGlass(m_hWnd))
    {
        PaintGlassClient(*pDC);
        return 0;
    }
#endif

    PaintListBoxClient(*pDC);
    return 0;
}

BOOL CCustomListBox::OnEraseBkgnd(CDC* pDC)
{
    UNREFERENCED_PARAMETER(pDC);
    return TRUE;
}

void CCustomListBox::PaintListBoxGlassBackgrounds(CDC& dc, const CRect& rcClient, BOOL bChromaSolid)
{
    const int nCount = GetCount();
    if (nCount <= 0) return;

    int nFirst = 0, nLast = -1;
    CCC_LbVisibleRange(m_hWnd, rcClient, nFirst, nLast);
    if (nLast < nFirst) return;

    for (int ni = nFirst; ni <= nLast; ++ni)
    {
        CRect r;
        if (!GetItemRect(ni, &r)) continue;
        if (r.right < rcClient.right) r.right = rcClient.right;

        const BOOL bS = (GetSel(ni) > 0);
        const BOOL bH = (ni == m_nHotItem);
        COLORREF bg = CCC_ListGlassRowBg(ni, bS, bH);
        const CCC_RowGlassStyle* pCustom = LookupRowStyle(ni);
        if (pCustom && pCustom->bValid && pCustom->bUseGlass)
            bg = pCustom->clrBg;

        if (bChromaSolid && CCC_UseListBoxRowGlass(m_hWnd))
            dc.FillSolidRect(&r, bg);
        else
        {
            const BYTE alpha = CCC_ListGlassRowAlpha(ni, bS, bH);
            CCC_DrawRowGlassBackground(&dc, r, m_hWnd, m_bAeroMode, bg, alpha, bS, pCustom);
        }
    }

    CRect rLast;
    if (GetItemRect(nLast, &rLast) && rLast.bottom < rcClient.bottom)
    {
        CRect fill(rcClient.left, rLast.bottom, rcClient.right, rcClient.bottom);
        if (bChromaSolid)
            dc.FillSolidRect(&fill, COLOR_LIST_BG);
        else
            FillRectAlpha(&dc, fill, COLOR_LIST_BG, CCC_ScaleGlassAlpha((BYTE)200));
    }
}

void CCustomListBox::PaintListBoxItemForeground(CDC& dc, int ni, const CRect& r, DWORD itemState)
{
    if (ni < 0) return;

    int it = ni % 4;
    int is = 8;
    int ix = r.left + 5;
    int iy = r.top + (r.Height() - is) / 2;

    switch (it)
    {
    case 0: DrawFlower(&dc, ix + is / 2, iy + is / 2, is / 2, RGB(255, 200, 220)); break;
    case 1: DrawStar(&dc, ix + is / 2, iy + is / 2, is / 3, RGB(255, 215, 0)); break;
    case 2: DrawHeart(&dc, CRect(ix, iy, ix + is, iy + is), COLOR_HEART); break;
    case 3: DrawRibbon(&dc, CRect(ix, iy, ix + is, iy + is), RGB(255, 182, 193)); break;
    }

    if (itemState & ODS_SELECTED)
        DrawStar(&dc, r.right - 12, r.top + r.Height() / 2, 3, RGB(255, 215, 0));

    CString st;
    GetText(ni, st);
    CRect rt = r;
    rt.left += 20;
    rt.DeflateRect(1, 1);

    dc.SetTextColor(RGB(0, 0, 0));
    dc.SetBkMode(TRANSPARENT);

    CFont* po = dc.SelectObject(GetFont());
    DrawListSubitemCellText(&dc, st, rt);
    dc.SelectObject(po);

    if (ni < GetCount() - 1)
        DrawLaceLine(&dc, r.left + 15, r.bottom - 1, r.right - 15, r.bottom - 1, RGB(200, 180, 220));
}

void CCustomListBox::PaintListBoxForeground(CDC& dc, const CRect& rcClient)
{
    const int nCount = GetCount();
    if (nCount <= 0) return;

    int nFirst = 0, nLast = -1;
    CCC_LbVisibleRange(m_hWnd, rcClient, nFirst, nLast);
    if (nLast < nFirst) return;

    for (int ni = nFirst; ni <= nLast; ++ni)
    {
        CRect r;
        if (!GetItemRect(ni, &r)) continue;
        DWORD st = (GetSel(ni) > 0) ? ODS_SELECTED : 0;
        if (!IsWindowEnabled()) st |= ODS_DISABLED;
        PaintListBoxItemForeground(dc, ni, r, st);
    }
}

void CCustomListBox::PaintGlassClient(CDC& dc)
{
#if CCUSTOM_AERO_SUPPORT
    CRect rcC;
    GetClientRect(&rcC);
    const int w = rcC.Width();
    const int h = rcC.Height();
    if (w <= 0 || h <= 0) return;

    void* pGlassBits = nullptr;
    HBITMAP hGlassDib = CCC_CreateAlphaDib32(dc.GetSafeHdc(), w, h, &pGlassBits);
    CDC memGlass;
    memGlass.CreateCompatibleDC(&dc);
    HGDIOBJ obGlass = nullptr;
    if (hGlassDib)
    {
        obGlass = ::SelectObject(memGlass.GetSafeHdc(), hGlassDib);
        memGlass.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
        PaintListBoxGlassBackgrounds(memGlass, rcC, TRUE);

        int nFirst = 0, nLast = -1;
        CCC_LbVisibleRange(m_hWnd, rcC, nFirst, nLast);
        for (int ni = nFirst; ni <= nLast; ++ni)
        {
            CRect r;
            if (!GetItemRect(ni, &r)) continue;
            if (r.right < rcC.right) r.right = rcC.right;
            const BOOL bS = (GetSel(ni) > 0);
            const BOOL bH = (ni == m_nHotItem);
            RECT rr = r;
            CCC_SetDibAlphaGlassRect(pGlassBits, w, h, rr, CCC_AERO_CHROMA_KEY, CCC_ListGlassRowAlpha(ni, bS, bH));
        }
        CRect rLast;
        if (nLast >= nFirst && GetItemRect(nLast, &rLast) && rLast.bottom < rcC.bottom)
        {
            RECT fill = { rcC.left, rLast.bottom, rcC.right, rcC.bottom };
            CCC_SetDibAlphaGlassRect(pGlassBits, w, h, fill, CCC_AERO_CHROMA_KEY, CCC_ScaleGlassAlpha((BYTE)200));
        }
        CCC_SetDibChromaTransparent(pGlassBits, w, h, CCC_AERO_CHROMA_KEY);
    }

    void* pFgBits = nullptr;
    HBITMAP hFgDib = CCC_CreateAlphaDib32(dc.GetSafeHdc(), w, h, &pFgBits);
    CDC memFg;
    memFg.CreateCompatibleDC(&dc);
    HGDIOBJ obFg = nullptr;
    if (hFgDib)
    {
        obFg = ::SelectObject(memFg.GetSafeHdc(), hFgDib);
        memFg.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
        PaintListBoxForeground(memFg, rcC);
        CCC_SetDibAlphaFromChroma(pFgBits, w, h, CCC_AERO_CHROMA_KEY);
    }

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &rcC, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP || !hGlassDib)
    {
        if (obGlass) ::SelectObject(memGlass.GetSafeHdc(), obGlass);
        if (hGlassDib) ::DeleteObject(hGlassDib);
        if (obFg) { ::SelectObject(memFg.GetSafeHdc(), obFg); ::DeleteObject(hFgDib); }
        CDC memFb;
        CBitmap bmpFb;
        memFb.CreateCompatibleDC(&dc);
        bmpFb.CreateCompatibleBitmap(&dc, w, h);
        CBitmap* obFb = memFb.SelectObject(&bmpFb);
        memFb.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
        PaintListBoxGlassBackgrounds(memFb, rcC, TRUE);
        CCC_BlitChromaGlass(dc.GetSafeHdc(), 0, 0, w, h, memFb.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY, CCC_GetAeroGlassAlpha());
        memFb.SelectObject(obFb);
        PaintListBoxForeground(dc, rcC);
        return;
    }

    CCC_InitBufferedPaintTransparent(hBP, w, h);
    const BLENDFUNCTION bfGlass = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    ::GdiAlphaBlend(hdcBuf, 0, 0, w, h, memGlass.GetSafeHdc(), 0, 0, w, h, bfGlass);
    if (hFgDib && pFgBits)
    {
        const BLENDFUNCTION bfFg = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        ::GdiAlphaBlend(hdcBuf, 0, 0, w, h, memFg.GetSafeHdc(), 0, 0, w, h, bfFg);
    }
    ::EndBufferedPaint(hBP, TRUE);

    if (obGlass) ::SelectObject(memGlass.GetSafeHdc(), obGlass);
    if (hGlassDib) ::DeleteObject(hGlassDib);
    if (obFg)
    {
        ::SelectObject(memFg.GetSafeHdc(), obFg);
        ::DeleteObject(hFgDib);
    }
#endif
}

void CCustomListBox::DrawItem(LPDRAWITEMSTRUCT lp)
{
    if (lp->itemID == (UINT)-1) return;

#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListBoxRowGlass(m_hWnd))
        return;

    if (CCC_DeferListToOpaquePaint(m_hWnd) && !m_bInOpaqueFullPaint
        && (lp->itemAction & ODA_DRAWENTIRE) == 0)
    {
        RepaintOpaqueIfNeeded();
        return;
    }
#endif

    CDC* pDC = CDC::FromHandle(lp->hDC);
    CRect r = lp->rcItem;
    const int ni = (int)lp->itemID;
    const BOOL bSel = (lp->itemState & ODS_SELECTED) != 0;
    const BOOL bH = !bSel && (ni == m_nHotItem);
    COLORREF bg = CCC_ListGlassRowBg(ni, bSel, bH);
    const CCC_RowGlassStyle* pCustom = LookupRowStyle(ni);
    if (pCustom && pCustom->bValid)
        bg = pCustom->clrBg;

#if CCUSTOM_AERO_SUPPORT
    const BOOL bGlassLv = CCC_UseListBoxRowGlass(m_hWnd);
    const BOOL bOpaqueDefer = CCC_DeferListToOpaquePaint(m_hWnd);
    if (!bGlassLv && bOpaqueDefer && CCC_IsAeroEnabled())
    {
        const BYTE alpha = CCC_ListGlassRowAlpha(ni, bSel, bH);
        CCC_DrawRowGlassBackground(pDC, r, m_hWnd, m_bAeroMode, bg, alpha, bSel, pCustom);
    }
    else if (!bGlassLv)
#endif
        pDC->FillSolidRect(&r, bg);

    PaintListBoxItemForeground(*pDC, ni, r, lp->itemState);
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
    ON_CONTROL_REFLECT(CBN_CLOSEUP, &CCustomComboBox::OnCloseUp)
#if CCUSTOM_AERO_SUPPORT
    ON_WM_TIMER()
#endif
END_MESSAGE_MAP()

#if CCUSTOM_AERO_SUPPORT
static const UINT_PTR kComboDropGlassRetryTimerId = 0xCB01;
#endif

CCustomComboBox::CCustomComboBox()
    : m_bAutoDelete(FALSE), m_clrLabelText(RGB(240, 240, 255)),
    m_clrLabelBg(RGB(80, 60, 120)), m_bAeroMode(FALSE), m_nDropHotItem(-1)
{
    m_brBackground.CreateSolidBrush(COLOR_COMBO_BG);
}

CCustomComboBox::~CCustomComboBox()
{
#if CCUSTOM_AERO_SUPPORT
    CCC_ReleaseComboDropListFix(m_hWnd);
#endif
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
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseComboGlass(m_hWnd))
    {
        COMBOBOXINFO cbi = { sizeof(COMBOBOXINFO) };
        if (::GetComboBoxInfo(m_hWnd, &cbi) && cbi.hwndItem && cbi.hwndItem != m_hWnd)
            ::ShowWindow(cbi.hwndItem, SW_HIDE);
    }
#endif
}

HBRUSH CCustomComboBox::CtlColor(CDC* pDC, UINT nC)
{
    if (nC == CTLCOLOR_LISTBOX)
    {
#if CCUSTOM_AERO_SUPPORT
        if (CCC_UseComboGlass(m_hWnd))
        {
            pDC->SetBkMode(TRANSPARENT);
            return (HBRUSH)::GetStockObject(HOLLOW_BRUSH);
        }
#endif
        pDC->SetBkColor(COLOR_COMBO_BG);
        pDC->SetTextColor(RGB(0, 0, 0));
        return (HBRUSH)m_brBackground.GetSafeHandle();
    }
    return NULL;
}

BOOL CCustomComboBox::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseComboGlass(m_hWnd))
        return TRUE;
#endif
    if (pDC)
    {
        CRect r;
        GetClientRect(&r);
        pDC->FillSolidRect(&r, COLOR_COMBO_BG);
    }
    return TRUE;
}

void CCustomComboBox::PaintComboClosedForeground(CDC& dc, const CRect& r)
{
    CPen pF(PS_SOLID, 2, COLOR_VINE_DECO);
    CPen* op = dc.SelectObject(&pF);
    dc.SelectStockObject(NULL_BRUSH);
    dc.RoundRect(&r, CPoint(10, 10));

    int nb = GetSystemMetrics(SM_CXVSCROLL);
    COMBOBOXINFO cbiBtn = { sizeof(COMBOBOXINFO) };
    if (::GetComboBoxInfo(m_hWnd, &cbiBtn))
    {
        const int btnW = cbiBtn.rcButton.right - cbiBtn.rcButton.left;
        if (btnW > 0) nb = btnW;
    }
    CRect rB(r.right - nb - 4, r.top + 4, r.right - 4, r.bottom - 4);
    dc.FillSolidRect(&rB, RGB(255, 200, 220));

    {
        CPen pb(PS_SOLID, 1, RGB(200, 150, 180));
        dc.SelectObject(&pb);
        dc.SelectStockObject(NULL_BRUSH);
        dc.RoundRect(&rB, CPoint(6, 6));
        dc.SelectObject(op);
    }

    int hs = 6, sp = 2;
    int sx = rB.left + (rB.Width() - (hs * 3 + sp * 2)) / 2;
    int cy2 = rB.Height() / 2 + rB.top;

    for (int i = 0; i < 3; i++)
    {
        CRect rh(sx + i * (hs + sp), cy2 - hs / 2, sx + i * (hs + sp) + hs, cy2 + hs / 2);
        DrawHeart(&dc, rh, (i == 1) ? COLOR_HEART : RGB(255, 182, 193));
    }

    DrawStar(&dc, r.right - 8, r.top + 8, 3, RGB(255, 215, 0));

    int nPS = CComboBox::GetCurSel();
    CString st;
    if (nPS != CB_ERR) GetLBText(nPS, st);

    CFont* pOF = dc.SelectObject(GetFont());
    CRect rt = r;
    rt.left += 12;
    rt.right = rB.left - 4;

    BOOL bIL = (nPS >= 0 && nPS < (int)m_vDisabledItems.size() && m_vDisabledItems[nPS]);
    if (nPS != CB_ERR && !bIL)
    {
        int cs = (rt.Height() - 8) / 2;
        DrawCrown(&dc, rt.left + cs, rt.Height() / 2, cs, RGB(255, 215, 0));
        rt.left += cs * 2 + 4;
    }

    dc.SetTextColor(RGB(0, 0, 0));
    dc.SetBkMode(TRANSPARENT);
    dc.DrawText(st, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    dc.SelectObject(pOF);
}

void CCustomComboBox::PaintGlassClient(CDC& dc, BOOL bToPaintBuffer)
{
#if CCUSTOM_AERO_SUPPORT
    CRect rcC;
    GetClientRect(&rcC);
    const int w = rcC.Width();
    const int h = rcC.Height();
    if (w <= 0 || h <= 0) return;

    CDC memFG;
    memFG.CreateCompatibleDC(&dc);
    CBitmap bmpFG;
    bmpFG.CreateCompatibleBitmap(&dc, w, h);
    CBitmap* pOldFG = memFG.SelectObject(&bmpFG);
    memFG.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
    PaintComboClosedForeground(memFG, rcC);

    CCC_CommitUniformGlassFromFgMem(dc, rcC, COLOR_COMBO_BG, memFG.GetSafeHdc(), w, h, bToPaintBuffer);

    memFG.SelectObject(pOldFG);
#endif
}

void CCustomComboBox::PaintComboDropGlassBackgrounds(CDC& dc, HWND hwndList, const CRect& rcClient, BOOL bChromaSolid)
{
    if (!hwndList || !::IsWindow(hwndList)) return;
    const int nCount = (int)::SendMessage(hwndList, LB_GETCOUNT, 0, 0);
    if (nCount <= 0) return;

    int nFirst = 0, nLast = -1;
    CCC_LbVisibleRange(hwndList, rcClient, nFirst, nLast);
    if (nLast < nFirst) return;

    const int nSel = (int)::SendMessage(hwndList, LB_GETCURSEL, 0, 0);
    const BOOL bNoScroll = CCC_ComboDropListNoScroll(hwndList);

    for (int ni = nFirst; ni <= nLast; ++ni)
    {
        RECT rr = {};
        if (!::SendMessage(hwndList, LB_GETITEMRECT, ni, (LPARAM)&rr)) continue;
        CRect r(rr);
        if (r.right < rcClient.right) r.right = rcClient.right;

        const BOOL bD = (ni < (int)m_vDisabledItems.size()) && m_vDisabledItems[ni];
        const BOOL bS = !bD && (ni == nSel);
        const BOOL bH = !bD && !bS && (ni == m_nDropHotItem);
        COLORREF bg = CCC_ComboGlassRowBg(ni, bS, bH, bD, m_clrLabelBg);
        const CCC_RowGlassStyle* pCustom = LookupRowStyle(ni);
        if (pCustom && pCustom->bValid && pCustom->bUseGlass)
            bg = pCustom->clrBg;

        if (bChromaSolid && CCC_UseComboGlass(m_hWnd))
            dc.FillSolidRect(&r, bg);
        else
        {
            const BYTE alpha = CCC_ComboGlassRowAlpha(ni, bS, bH, bD, bNoScroll);
            CCC_DrawRowGlassBackground(&dc, r, m_hWnd, m_bAeroMode, bg, alpha, (bS || bD), pCustom, hwndList);
        }
    }

    RECT rrLast = {};
    if (::SendMessage(hwndList, LB_GETITEMRECT, nLast, (LPARAM)&rrLast) && rrLast.bottom < rcClient.bottom)
    {
        CRect fill(rcClient.left, rrLast.bottom, rcClient.right, rcClient.bottom);
        const BYTE fillAlpha = bNoScroll ? CCC_ScaleGlassAlpha((BYTE)190) : CCC_GetAeroGlassAlpha();
        if (bChromaSolid)
            dc.FillSolidRect(&fill, COLOR_COMBO_BG);
        else
            FillRectAlpha(&dc, fill, COLOR_COMBO_BG, fillAlpha);
    }
}

void CCustomComboBox::PaintComboDropItemForeground(CDC& dc, int itemID, const CRect& r, DWORD itemState)
{
    if (itemID < 0) return;
    const BOOL bD = (itemID < (int)m_vDisabledItems.size()) && m_vDisabledItems[itemID];
    const BOOL bS = !bD && (itemState & ODS_SELECTED);

    if (!bD)
    {
        int it = itemID % 4;
        const int is = 10;
        const int ix = r.left + 4;
        const int iy = r.top + (r.Height() - is) / 2;
        switch (it)
        {
        case 0: DrawFlower(&dc, ix + is / 2, iy + is / 2, is / 2, RGB(255, 200, 220)); break;
        case 1: DrawStar(&dc, ix + is / 2, iy + is / 2, is / 3, RGB(255, 215, 0)); break;
        case 2: DrawHeart(&dc, CRect(ix, iy, ix + is, iy + is), COLOR_HEART); break;
        case 3: DrawRibbon(&dc, CRect(ix, iy, ix + is, iy + is), RGB(255, 182, 193)); break;
        }
    }

    CString st;
    GetLBText(itemID, st);
    CFont* pOF = NULL;
    CFont fc;
    CFont* pF = GetFont();
    LOGFONT lf;
    pF->GetLogFont(&lf);

    if (bD)
    {
        dc.SetTextColor(m_clrLabelText);
        lf.lfWeight = FW_BOLD;
        lf.lfItalic = TRUE;
    }
    else
    {
        dc.SetTextColor(RGB(0, 0, 0));
        lf.lfWeight = FW_BOLD;
    }
    fc.CreateFontIndirect(&lf);
    pOF = dc.SelectObject(&fc);
    dc.SetBkMode(TRANSPARENT);
    CRect rt = r;
    rt.left += bD ? 4 : 20;
    dc.DrawText(st, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (pOF)
    {
        dc.SelectObject(pOF);
        fc.DeleteObject();
    }
    if (bS && !bD) DrawCrown(&dc, r.right - 14, r.top + r.Height() / 2, 6, RGB(255, 215, 0));
}

void CCustomComboBox::PaintDropListGlassClient(HWND hwndList, HDC hdc)
{
#if CCUSTOM_AERO_SUPPORT
    if (!hwndList || !hdc || !CCC_UseComboGlass(m_hWnd)) return;

    CRect rcC;
    ::GetClientRect(hwndList, &rcC);
    const int w = rcC.Width();
    const int h = rcC.Height();
    if (w <= 0 || h <= 0) return;

    CDC dc;
    dc.Attach(hdc);

    const BOOL bNoScroll = CCC_ComboDropListNoScroll(hwndList);
    int nFirst = 0, nLast = -1;
    CCC_LbVisibleRange(hwndList, rcC, nFirst, nLast);
    const int nSel = (int)::SendMessage(hwndList, LB_GETCURSEL, 0, 0);
    const BYTE fillAlpha = bNoScroll ? CCC_ScaleGlassAlpha((BYTE)190) : CCC_GetAeroGlassAlpha();

    void* pGlassBits = nullptr;
    HBITMAP hGlassDib = CCC_CreateAlphaDib32(dc.GetSafeHdc(), w, h, &pGlassBits);
    CDC memGlass;
    memGlass.CreateCompatibleDC(&dc);
    HGDIOBJ obGlass = nullptr;
    if (hGlassDib)
    {
        obGlass = ::SelectObject(memGlass.GetSafeHdc(), hGlassDib);
        memGlass.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
        PaintComboDropGlassBackgrounds(memGlass, hwndList, rcC, TRUE);

        if (nLast >= nFirst)
        {
            for (int ni = nFirst; ni <= nLast; ++ni)
            {
                RECT rr = {};
                if (!::SendMessage(hwndList, LB_GETITEMRECT, ni, (LPARAM)&rr)) continue;
                if (rr.right < rcC.right) rr.right = rcC.right;
                const BOOL bD = (ni < (int)m_vDisabledItems.size()) && m_vDisabledItems[ni];
                const BOOL bS = !bD && (ni == nSel);
                const BOOL bH = !bD && !bS && (ni == m_nDropHotItem);
                CCC_SetDibAlphaGlassRect(pGlassBits, w, h, rr, CCC_AERO_CHROMA_KEY,
                    CCC_ComboGlassRowAlpha(ni, bS, bH, bD, bNoScroll));
            }
            CRect rLast;
            if (::SendMessage(hwndList, LB_GETITEMRECT, nLast, (LPARAM)&rLast) && rLast.bottom < rcC.bottom)
            {
                RECT fill = { rcC.left, rLast.bottom, rcC.right, rcC.bottom };
                CCC_SetDibAlphaGlassRect(pGlassBits, w, h, fill, CCC_AERO_CHROMA_KEY, fillAlpha);
            }
        }
        CCC_SetDibChromaTransparent(pGlassBits, w, h, CCC_AERO_CHROMA_KEY);
    }

    void* pFgBits = nullptr;
    HBITMAP hFgDib = CCC_CreateAlphaDib32(dc.GetSafeHdc(), w, h, &pFgBits);
    CDC memFg;
    memFg.CreateCompatibleDC(&dc);
    HGDIOBJ obFg = nullptr;
    if (hFgDib)
    {
        obFg = ::SelectObject(memFg.GetSafeHdc(), hFgDib);
        memFg.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);

        if (nLast >= nFirst)
        {
            for (int ni = nFirst; ni <= nLast; ++ni)
            {
                RECT rr = {};
                if (!::SendMessage(hwndList, LB_GETITEMRECT, ni, (LPARAM)&rr)) continue;
                DWORD st = (ni == nSel) ? ODS_SELECTED : 0;
                PaintComboDropItemForeground(memFg, ni, CRect(rr), st);
            }
        }
        CCC_SetDibAlphaFromChroma(pFgBits, w, h, CCC_AERO_CHROMA_KEY);
    }

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(hdc, &rcC, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP || !hGlassDib)
    {
        if (obGlass) ::SelectObject(memGlass.GetSafeHdc(), obGlass);
        if (hGlassDib) ::DeleteObject(hGlassDib);
        if (obFg) { ::SelectObject(memFg.GetSafeHdc(), obFg); ::DeleteObject(hFgDib); }
        CDC memFb;
        CBitmap bmpFb;
        memFb.CreateCompatibleDC(&dc);
        bmpFb.CreateCompatibleBitmap(&dc, w, h);
        CBitmap* obFb = memFb.SelectObject(&bmpFb);
        memFb.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
        PaintComboDropGlassBackgrounds(memFb, hwndList, rcC, TRUE);
        if (!CCC_BlitComboDropBackdropCached(m_hWnd, hwndList, hdc, w, h))
        {
            CCC_ScheduleComboDropBackdropWarm(hwndList);
            dc.Detach();
            return;
        }
        CCC_BlitChromaGlass(hdc, 0, 0, w, h, memFb.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY, fillAlpha);
        memFb.SelectObject(obFb);
        if (nLast >= nFirst)
        {
            for (int ni = nFirst; ni <= nLast; ++ni)
            {
                RECT rr = {};
                if (!::SendMessage(hwndList, LB_GETITEMRECT, ni, (LPARAM)&rr)) continue;
                DWORD st = (ni == nSel) ? ODS_SELECTED : 0;
                PaintComboDropItemForeground(dc, ni, CRect(rr), st);
            }
        }
        dc.Detach();
        return;
    }

    CCC_InitBufferedPaintTransparent(hBP, w, h);
    if (!CCC_BlitComboDropBackdropCached(m_hWnd, hwndList, hdcBuf, w, h))
    {
        ::EndBufferedPaint(hBP, TRUE);
        if (obGlass) ::SelectObject(memGlass.GetSafeHdc(), obGlass);
        if (hGlassDib) ::DeleteObject(hGlassDib);
        if (obFg) { ::SelectObject(memFg.GetSafeHdc(), obFg); ::DeleteObject(hFgDib); }
        CCC_ScheduleComboDropBackdropWarm(hwndList);
        dc.Detach();
        return;
    }
    const BLENDFUNCTION bfGlass = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    ::GdiAlphaBlend(hdcBuf, 0, 0, w, h, memGlass.GetSafeHdc(), 0, 0, w, h, bfGlass);
    if (hFgDib && pFgBits)
    {
        const BLENDFUNCTION bfFg = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        ::GdiAlphaBlend(hdcBuf, 0, 0, w, h, memFg.GetSafeHdc(), 0, 0, w, h, bfFg);
    }
    ::EndBufferedPaint(hBP, TRUE);

    if (obGlass) ::SelectObject(memGlass.GetSafeHdc(), obGlass);
    if (hGlassDib) ::DeleteObject(hGlassDib);
    if (obFg)
    {
        ::SelectObject(memFg.GetSafeHdc(), obFg);
        ::DeleteObject(hFgDib);
    }
    dc.Detach();
#endif
}

void CCustomComboBox::PaintClient(CDC& dc)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseComboGlass(m_hWnd))
    {
        PaintGlassClient(dc);
        return;
    }
#endif

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
        FillRectAlpha(&mDC, r, COLOR_COMBO_BG, CCC_GetAeroGlassAlpha());
    }
    else mDC.FillSolidRect(&r, COLOR_COMBO_BG);

    PaintComboClosedForeground(mDC, r);

    if (bTrans) CCC_TransparentBltClearDest(dc.GetSafeHdc(), 0, 0, r.Width(), r.Height(), mDC.GetSafeHdc(), 0, 0, RGB(0, 0, 0));
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
#if CCUSTOM_AERO_SUPPORT
    const BOOL bDropList = (lp->hwndItem && ::IsWindow(lp->hwndItem) && lp->hwndItem != m_hWnd);
    if (bDropList && CCC_UseComboGlass(m_hWnd))
    {
        CCC_InstallComboDropListFix(m_hWnd);
        return;
    }
#endif

    CDC* pDC = CDC::FromHandle(lp->hDC);
    CRect r = lp->rcItem;
    BOOL bD = (lp->itemID < (UINT)m_vDisabledItems.size()) && m_vDisabledItems[lp->itemID];
    BOOL bS = !bD && (lp->itemState & ODS_SELECTED);
    const COLORREF zebra = (lp->itemID % 2 == 0) ? COLOR_COMBO_BG : RGB(255, 232, 220);
    COLORREF bg = bD ? m_clrLabelBg : (bS ? COLOR_SEL_BG : zebra);
    const BYTE alpha = bS ? CCC_ScaleGlassAlpha((BYTE)180) : (bD ? CCC_ScaleGlassAlpha((BYTE)200) : CCC_GetAeroGlassAlpha());
    const BOOL bBlackUnder = (bS || bD);
#if CCUSTOM_AERO_SUPPORT
    const BOOL bBlurCombo = CCC_IsBlurDialogChild(m_hWnd) && CCC_IsAeroEnabled();
    const BOOL bOpaqueRow = !CCC_UseComboGlass(m_hWnd) && (bBlurCombo || (bDropList && CCC_IsAeroEnabled()));
    const COLORREF rowSolid = bOpaqueRow
        ? CCC_RowSolidFillColor(m_hWnd, lp->hwndItem, m_bAeroMode, bg, alpha, bBlackUnder, LookupRowStyle((int)lp->itemID))
        : bg;
    if (bOpaqueRow)
    {
        CRect rf = r;
        if (bDropList)
            rf.bottom += 1;
        pDC->FillSolidRect(&rf, rowSolid);
    }
    else
#endif
        CCC_DrawRowGlassBackground(pDC, r, m_hWnd, m_bAeroMode, bg, alpha, bBlackUnder, LookupRowStyle((int)lp->itemID), lp->hwndItem);

#if CCUSTOM_AERO_SUPPORT
    if (bDropList && bBlurCombo)
        CCC_InstallComboDropListFix(m_hWnd);
#endif

    PaintComboDropItemForeground(*pDC, (int)lp->itemID, r, lp->itemState);
}

void CCustomComboBox::MeasureItem(LPMEASUREITEMSTRUCT lp)
{
    lp->itemHeight = 28;
}

void CCustomComboBox::OnDropdown()
{
    UpdateDropDownWidth();
#if CCUSTOM_AERO_SUPPORT
    CCC_InstallComboDropListFix(m_hWnd);
    if (CCC_UseComboGlass(m_hWnd) && !CCC_IsComboDropGlassInstalled(m_hWnd))
        SetTimer(kComboDropGlassRetryTimerId, 10, NULL);
    else
        CCC_RepaintComboDropGlass(m_hWnd);
#endif
}

void CCustomComboBox::OnCloseUp()
{
#if CCUSTOM_AERO_SUPPORT
    KillTimer(kComboDropGlassRetryTimerId);
    SetDropListHotItem(-1);
    CCC_ReleaseComboDropListFix(m_hWnd);
#endif
}

#if CCUSTOM_AERO_SUPPORT
void CCustomComboBox::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == kComboDropGlassRetryTimerId)
    {
        CCC_InstallComboDropListFix(m_hWnd);
        if (CCC_IsComboDropGlassInstalled(m_hWnd))
        {
            KillTimer(kComboDropGlassRetryTimerId);
            CCC_RepaintComboDropGlass(m_hWnd);
        }
        return;
    }
    CComboBox::OnTimer(nIDEvent);
}
#endif

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
END_MESSAGE_MAP()

CCustomSliderCtrl::CCustomSliderCtrl() : m_bAutoDelete(FALSE), m_nMode(0), m_bAeroMode(FALSE) {}
CCustomSliderCtrl::~CCustomSliderCtrl() {}

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
        CCC_InvalidateBlurParent(m_hWnd, m_bAeroMode);
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

    const BOOL bTrans = CCC_UseTransparentPaint(m_hWnd, m_bAeroMode);
    if (bTrans)
    {
        mDC.FillSolidRect(&r, CCC_AERO_CHROMA_KEY);
        DrawSlider(&mDC);
        CCC_BlitTransparentChroma(dc.GetSafeHdc(), 0, 0, r.Width(), r.Height(), mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    }
    else
    {
        mDC.FillSolidRect(&r, COLOR_DIALOG_BG);
        DrawSlider(&mDC);
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
    if (CCC_UseTransparentPaint(m_hWnd, m_bAeroMode)) return TRUE;
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
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseTransparentPaint(m_hWnd, m_bAeroMode))
        Invalidate(FALSE);
#endif
    return r;
}
LRESULT CCustomSliderCtrl::OnLButtonDownMsg(WPARAM w, LPARAM l)
{
    LRESULT r = Default();
#if CCUSTOM_AERO_SUPPORT
    CCC_InvalidateBlurParent(m_hWnd, m_bAeroMode);
#endif
    Invalidate(FALSE);
    return r;
}
LRESULT CCustomSliderCtrl::OnLButtonUpMsg(WPARAM w, LPARAM l)
{
    LRESULT r = Default();
#if CCUSTOM_AERO_SUPPORT
    CCC_InvalidateBlurParent(m_hWnd, m_bAeroMode);
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
    m_nLogicalPos = m_nVisualPos = p;
    CSliderCtrl::SetPos(p);
    if (::IsWindow(m_hWnd))
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

int CCustomRangeSliderCtrl::GetPos() const
{
    return m_nLogicalPos;
}

void CCustomRangeSliderCtrl::SetRange(int mn, int mx, BOOL b)
{
    m_nMin = mn;
    m_nMax = mx;
    CSliderCtrl::SetRange(mn, mx, FALSE);
    if (b && ::IsWindow(m_hWnd))
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CCustomRangeSliderCtrl::SetSelection(int mn, int mx)
{
    m_nSelMin = mn;
    m_nSelMax = mx;
    if (m_nSelMin > m_nSelMax)
    {
        int t = m_nSelMin;
        m_nSelMin = m_nSelMax;
        m_nSelMax = t;
    }
    if (::IsWindow(m_hWnd))
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

    const BOOL bTrans = CCC_UseTransparentPaint(m_hWnd, m_bAeroMode);
    if (bTrans)
    {
        mDC.FillSolidRect(&r, CCC_AERO_CHROMA_KEY);
        DrawRangeSlider(&mDC);
        CCC_BlitTransparentChroma(dc.GetSafeHdc(), 0, 0, r.Width(), r.Height(), mDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    }
    else
    {
        mDC.FillSolidRect(&r, COLOR_DIALOG_BG);
        DrawRangeSlider(&mDC);
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
    if (CCC_UseTransparentPaint(m_hWnd, m_bAeroMode)) return TRUE;
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

    // 現在位置（ハート）
    DrawHeart(pDC, CRect(xP - 9, cy - 12, xP + 9, cy + 6), COLOR_SLIDER_THUMB);
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
    CCC_InvalidateBlurParent(m_hWnd, m_bAeroMode);
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
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, m_nLogicalPos), (LPARAM)m_hWnd);
        }
#if CCUSTOM_AERO_SUPPORT
        CCC_InvalidateBlurParent(m_hWnd, m_bAeroMode);
#endif
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
    }
}
void CCustomRangeSliderCtrl::OnMouseMove(UINT f, CPoint p)
{
    if (m_bDragging)
    {
#if CCUSTOM_AERO_SUPPORT
        CCC_InvalidateBlurParent(m_hWnd, m_bAeroMode);
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
    ON_NOTIFY_REFLECT(LVN_ITEMCHANGED, OnItemChanged)
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
END_MESSAGE_MAP()

static const UINT_PTR kListScrollOpaqueTimerId = 4108;
static const UINT_PTR kGlassCoalesceTimerId = 4109;

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
void CCustomListCtrl::SetAeroMode(BOOL b)
{
    m_bAeroMode = b;
    if (GetSafeHwnd())
    {
        ApplyListViewGlassStyle();
        Invalidate();
    }
}

DWORD CCustomListCtrl::SetExtendedStyle(DWORD dwNewStyle)
{
    if (GetSafeHwnd() && CCC_UseListCtrlRowGlass(m_hWnd))
        dwNewStyle &= ~LVS_EX_DOUBLEBUFFER;
    return CListCtrl::SetExtendedStyle(dwNewStyle);
}

void CCustomListCtrl::ApplyListViewGlassStyle()
{
    if (!GetSafeHwnd()) return;
#if CCUSTOM_AERO_SUPPORT
    DWORD ex = GetExtendedStyle();
    DWORD want = ex;
    if (CCC_UseListCtrlRowGlass(m_hWnd))
        want &= ~LVS_EX_DOUBLEBUFFER;
    else
        want |= LVS_EX_DOUBLEBUFFER;
    if (want != ex)
        CListCtrl::SetExtendedStyle(want);
#else
    CListCtrl::SetExtendedStyle(GetExtendedStyle() | LVS_EX_DOUBLEBUFFER);
#endif
}

void CCustomListCtrl::PreSubclassWindow()
{
    CListCtrlA::PreSubclassWindow();
    ModifyStyle(0, WS_CLIPCHILDREN);
    SetBkColor(COLOR_LIST_BG);
    SetTextBkColor(COLOR_LIST_BG);
    SetTextColor(RGB(0, 0, 0));
    ApplyListViewGlassStyle();
}
HBRUSH CCustomListCtrl::CtlColor(CDC* pDC, UINT)
{
    pDC->SetBkColor(COLOR_LIST_BG);
    pDC->SetTextColor(RGB(0, 0, 0));
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

void CCustomListCtrl::OnMouseMove(UINT f, CPoint p)
{
    UpdateHotItem(CCC_ListHotItemFromPoint(this, p));

    TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, m_hWnd, 0 };
    TrackMouseEvent(&t);
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListCtrlRowGlass(m_hWnd))
        return;
#endif
    CListCtrl::OnMouseMove(f, p);
}

void CCustomListCtrl::OnMouseLeave()
{
    UpdateHotItem(-1);
    CListCtrl::OnMouseLeave();
}
void CCustomListCtrl::ScheduleOpaqueRepaint()
{
    if (!GetSafeHwnd()) return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListCtrlRowGlass(m_hWnd))
    {
        RequestGlassRepaint();
        return;
    }
    if (CCC_DeferListToOpaquePaint(m_hWnd))
        SendMessage(CCC_WM_POST_OPAQUE_PAINT);
    else
#endif
        PostMessage(CCC_WM_POST_OPAQUE_PAINT);
}

LRESULT CCustomListCtrl::OnPostOpaquePaint(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListCtrlRowGlass(m_hWnd))
        return 0;
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
    if (CCC_UseListCtrlRowGlass(m_hWnd))
    {
        UpdateHotItemFromCursor();
        return;
    }
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
    if (CCC_UseListCtrlRowGlass(m_hWnd))
    {
        UpdateHotItemFromCursor();
        return;
    }
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
    if (CCC_UseListCtrlRowGlass(m_hWnd))
    {
        UpdateHotItemFromCursor();
        return r;
    }
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
    if (nIDEvent == kGlassCoalesceTimerId)
    {
        KillTimer(kGlassCoalesceTimerId);
        if (GetSafeHwnd())
            Invalidate(FALSE);
        return;
    }
    if (nIDEvent == kListScrollOpaqueTimerId)
    {
        KillTimer(kListScrollOpaqueTimerId);
#if CCUSTOM_AERO_SUPPORT
        if (CCC_UseListCtrlRowGlass(m_hWnd))
            RequestGlassRepaint();
        else if (CCC_IsAeroEnabled() && CCC_IsWin11())
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
    if (CCC_UseListCtrlRowGlass(m_hWnd))
    {
        if (lpwndpos && !(lpwndpos->flags & SWP_NOSIZE))
            RequestGlassRepaint();
        return;
    }
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
        ScheduleOpaqueRepaint();
#endif
}

void CCustomListCtrl::RequestGlassRepaint()
{
    if (!GetSafeHwnd()) return;
    SetTimer(kGlassCoalesceTimerId, 16, NULL);
}

void CCustomListCtrl::RefreshRows(int iFirst, int iLast)
{
    if (!GetSafeHwnd() || iFirst < 0) return;
    if (iLast < iFirst) iLast = iFirst;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListCtrlRowGlass(m_hWnd))
    {
        RequestGlassRepaint();
        return;
    }
#endif
    RedrawItems(iFirst, iLast);
}

void CCustomListCtrl::UpdateHotItem(int n)
{
    if (m_nHotItem == n) return;
    const int o = m_nHotItem;
    m_nHotItem = n;
    if (!GetSafeHwnd()) return;

#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListCtrlRowGlass(m_hWnd))
    {
        RequestGlassRepaint();
        return;
    }
#endif
    if (o >= 0) RedrawItems(o, o);
    if (m_nHotItem >= 0) RedrawItems(m_nHotItem, m_nHotItem);
}

void CCustomListCtrl::UpdateHotItemFromCursor()
{
    if (!GetSafeHwnd()) return;
    CPoint pt;
    if (!GetCursorPos(&pt)) return;
    ScreenToClient(&pt);
    UpdateHotItem(CCC_ListHotItemFromPoint(this, pt));
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
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListCtrlRowGlass(m_hWnd))
        return TRUE;
#endif
    return FALSE;
}

void CCustomListCtrl::PaintOpaqueIntoBuffer(HDC hdcBuf)
{
    if (!hdcBuf || !m_hWnd) return;
    OnPrintClient((WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
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
    OnPrintClient((WPARAM)hdcBuf, PRF_CLIENT | PRF_ERASEBKGND);
    ::BufferedPaintMakeOpaque(hBP, &r);
    ::EndBufferedPaint(hBP, TRUE);
}

void CCustomListCtrl::PaintListCtrlClient(CDC& dc, BOOL bChromaCanvas)
{
    CRect rcC;
    GetClientRect(&rcC);
    if (rcC.Width() <= 0 || rcC.Height() <= 0) return;

    const BOOL bGlassDirect = CCC_UseListCtrlRowGlass(m_hWnd) && !bChromaCanvas;
    const COLORREF canvas = bChromaCanvas ? CCC_AERO_CHROMA_KEY : COLOR_LIST_BG;
    if (!bGlassDirect)
        dc.FillSolidRect(&rcC, canvas);

    CHeaderCtrl* pHdr = GetHeaderCtrl();
    const int nCols = pHdr ? pHdr->GetItemCount() : 0;
    if (nCols <= 0) return;
    const int nLC = nCols - 1;

    const int nCount = GetItemCount();
    if (nCount <= 0) return;

    int nFirst = GetTopIndex();
    if (nFirst < 0) nFirst = 0;
    int nLast = nFirst + GetCountPerPage();
    if (nLast >= nCount) nLast = nCount - 1;

    for (int ni = nFirst; ni <= nLast; ++ni)
    {
        for (int ns = 0; ns < nCols; ++ns)
            DrawListCtrlSubItem(&dc, ni, ns, nLC, rcC);
    }

    CRect rLast;
    if (!bGlassDirect && GetItemRect(nLast, &rLast, LVIR_BOUNDS) && rLast.bottom < rcC.bottom)
    {
        CRect fill(rcC.left, rLast.bottom, rcC.right, rcC.bottom);
        dc.FillSolidRect(&fill, canvas);
    }
}

void CCustomListCtrl::PaintListCtrlGlassBackgrounds(CDC& dc, const CRect& rcClient, BOOL bChromaSolid)
{
    CHeaderCtrl* pHdr = GetHeaderCtrl();
    const int nCols = pHdr ? pHdr->GetItemCount() : 0;
    if (nCols <= 0) return;

    const int nCount = GetItemCount();
    if (nCount <= 0) return;

    int nFirst = GetTopIndex();
    if (nFirst < 0) nFirst = 0;
    int nLast = nFirst + GetCountPerPage();
    if (nLast >= nCount) nLast = nCount - 1;

    for (int ni = nFirst; ni <= nLast; ++ni)
        DrawListCtrlRowBackground(&dc, ni, rcClient, bChromaSolid);

    CRect rLast;
    if (GetItemRect(nLast, &rLast, LVIR_BOUNDS) && rLast.bottom < rcClient.bottom)
    {
        CRect fill(rcClient.left, rLast.bottom, rcClient.right, rcClient.bottom);
        if (bChromaSolid)
            dc.FillSolidRect(&fill, COLOR_LIST_BG);
        else
            FillRectAlpha(&dc, fill, COLOR_LIST_BG, CCC_ScaleGlassAlpha((BYTE)200));
    }
}

void CCustomListCtrl::PaintListCtrlRowForeground(CDC& dc, int ni, const CRect& rcClient)
{
    CHeaderCtrl* pHdr = GetHeaderCtrl();
    const int nCols = pHdr ? pHdr->GetItemCount() : 0;
    if (nCols <= 0 || ni < 0) return;
    const int nLC = nCols - 1;
    for (int ns = 0; ns < nCols; ++ns)
        DrawListCtrlSubItemForeground(&dc, ni, ns, nLC, rcClient);
}

void CCustomListCtrl::PaintListCtrlForeground(CDC& dc, const CRect& rcClient)
{
    CHeaderCtrl* pHdr = GetHeaderCtrl();
    const int nCols = pHdr ? pHdr->GetItemCount() : 0;
    if (nCols <= 0) return;

    const int nCount = GetItemCount();
    if (nCount <= 0) return;

    int nFirst = GetTopIndex();
    if (nFirst < 0) nFirst = 0;
    int nLast = nFirst + GetCountPerPage();
    if (nLast >= nCount) nLast = nCount - 1;

    for (int ni = nFirst; ni <= nLast; ++ni)
        PaintListCtrlRowForeground(dc, ni, rcClient);
}

void CCustomListCtrl::PaintGlassClient(CDC& dc)
{
#if CCUSTOM_AERO_SUPPORT
    CRect rcC;
    GetClientRect(&rcC);
    const int w = rcC.Width();
    const int h = rcC.Height();
    if (w <= 0 || h <= 0) return;

    void* pGlassBits = nullptr;
    HBITMAP hGlassDib = CCC_CreateAlphaDib32(dc.GetSafeHdc(), w, h, &pGlassBits);
    CDC memGlass;
    memGlass.CreateCompatibleDC(&dc);
    HGDIOBJ obGlass = nullptr;
    if (hGlassDib)
    {
        obGlass = ::SelectObject(memGlass.GetSafeHdc(), hGlassDib);
        memGlass.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
        PaintListCtrlGlassBackgrounds(memGlass, rcC, TRUE);

        CHeaderCtrl* pHdr = GetHeaderCtrl();
        const int nCount = GetItemCount();
        if (pHdr && pHdr->GetItemCount() > 0 && nCount > 0)
        {
            int nFirst = GetTopIndex();
            if (nFirst < 0) nFirst = 0;
            int nLast = nFirst + GetCountPerPage();
            if (nLast >= nCount) nLast = nCount - 1;
            for (int ni = nFirst; ni <= nLast; ++ni)
            {
                CRect r;
                if (!GetItemRect(ni, &r, LVIR_BOUNDS)) continue;
                if (r.right < rcC.right) r.right = rcC.right;
                const BOOL bS = (GetItemState(ni, LVIS_SELECTED) & LVIS_SELECTED);
                const BOOL bH = (ni == m_nHotItem);
                RECT rr = r;
                CCC_SetDibAlphaGlassRect(pGlassBits, w, h, rr, CCC_AERO_CHROMA_KEY, CCC_ListGlassRowAlpha(ni, bS, bH));
            }
            CRect rLast;
            if (GetItemRect(nLast, &rLast, LVIR_BOUNDS) && rLast.bottom < rcC.bottom)
            {
                RECT fill = { rcC.left, rLast.bottom, rcC.right, rcC.bottom };
                CCC_SetDibAlphaGlassRect(pGlassBits, w, h, fill, CCC_AERO_CHROMA_KEY, CCC_ScaleGlassAlpha((BYTE)200));
            }
        }
        CCC_SetDibChromaTransparent(pGlassBits, w, h, CCC_AERO_CHROMA_KEY);
    }

    void* pFgBits = nullptr;
    HBITMAP hFgDib = CCC_CreateAlphaDib32(dc.GetSafeHdc(), w, h, &pFgBits);
    CDC memFg;
    memFg.CreateCompatibleDC(&dc);
    HGDIOBJ obFg = nullptr;
    if (hFgDib)
    {
        obFg = ::SelectObject(memFg.GetSafeHdc(), hFgDib);
        memFg.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
        PaintListCtrlForeground(memFg, rcC);
        CCC_SetDibAlphaFromChroma(pFgBits, w, h, CCC_AERO_CHROMA_KEY);
    }

    BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
    HDC hdcBuf = NULL;
    HPAINTBUFFER hBP = ::BeginBufferedPaint(dc.GetSafeHdc(), &rcC, BPBF_TOPDOWNDIB, &params, &hdcBuf);
    if (!hdcBuf || !hBP || !hGlassDib)
    {
        if (obGlass) ::SelectObject(memGlass.GetSafeHdc(), obGlass);
        if (hGlassDib) ::DeleteObject(hGlassDib);
        if (obFg) { ::SelectObject(memFg.GetSafeHdc(), obFg); ::DeleteObject(hFgDib); }

        CDC memFb;
        CBitmap bmpFb;
        memFb.CreateCompatibleDC(&dc);
        bmpFb.CreateCompatibleBitmap(&dc, w, h);
        CBitmap* obFb = memFb.SelectObject(&bmpFb);
        memFb.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);
        PaintListCtrlGlassBackgrounds(memFb, rcC, TRUE);
        CCC_BlitChromaGlass(dc.GetSafeHdc(), 0, 0, w, h, memFb.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY, CCC_GetAeroGlassAlpha());
        memFb.SelectObject(obFb);
        PaintListCtrlForeground(dc, rcC);
        return;
    }

    CCC_InitBufferedPaintTransparent(hBP, w, h);
    const BLENDFUNCTION bfGlass = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    ::GdiAlphaBlend(hdcBuf, 0, 0, w, h, memGlass.GetSafeHdc(), 0, 0, w, h, bfGlass);
    if (hFgDib && pFgBits)
    {
        const BLENDFUNCTION bfFg = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        ::GdiAlphaBlend(hdcBuf, 0, 0, w, h, memFg.GetSafeHdc(), 0, 0, w, h, bfFg);
    }
    ::EndBufferedPaint(hBP, TRUE);

    if (obGlass) ::SelectObject(memGlass.GetSafeHdc(), obGlass);
    if (hGlassDib) ::DeleteObject(hGlassDib);
    if (obFg)
    {
        ::SelectObject(memFg.GetSafeHdc(), obFg);
        ::DeleteObject(hFgDib);
    }
#else
    PaintListCtrlClient(dc, FALSE);
#endif
}

void CCustomListCtrl::OnPaint()
{
    Default();
}

void CCustomListCtrl::DrawListCtrlRowBackground(CDC* pDC, int ni, const CRect& rcClient, BOOL bChromaSolid)
{
    if (!pDC || ni < 0) return;

    CRect r;
    if (!GetItemRect(ni, &r, LVIR_BOUNDS)) return;
    if (r.right < rcClient.right) r.right = rcClient.right;

    const BOOL bS = (GetItemState(ni, LVIS_SELECTED) & LVIS_SELECTED);
    const BOOL bH = (ni == m_nHotItem);
    COLORREF bg = CCC_ListGlassRowBg(ni, bS, bH);
    const CCC_RowGlassStyle* pCustom = LookupRowStyle(ni);
    if (pCustom && pCustom->bValid && pCustom->bUseGlass)
        bg = pCustom->clrBg;
    const BYTE alpha = CCC_ListGlassRowAlpha(ni, bS, bH);

    if (bChromaSolid && CCC_UseListCtrlRowGlass(m_hWnd))
        pDC->FillSolidRect(&r, bg);
    else
        CCC_DrawRowGlassBackground(pDC, r, m_hWnd, m_bAeroMode, bg, alpha, bS, pCustom);
}

void CCustomListCtrl::DrawListCtrlSubItemForeground(CDC* pDC, int ni, int ns, int nLC, const CRect& rcClient)
{
    if (!pDC || ni < 0) return;

    CRect r;
    if (!GetSubItemRect(ni, ns, LVIR_BOUNDS, r)) return;

    if (ns == 0)
    {
        int cx0 = GetColumnWidth(0);
        if (cx0 > 0) r.right = (std::min)((int)r.right, (int)r.left + cx0);
    }
    if (ns >= nLC - 1 && r.right < rcClient.right) r.right = (int)rcClient.right;

    const BOOL bS = (GetItemState(ni, LVIS_SELECTED) & LVIS_SELECTED);
    const BOOL bH = (ni == m_nHotItem);

    if (ns == 0)
    {
        if (bS)
            DrawHeart(pDC, CRect(r.left + 2, r.top + 4, r.left + 16, r.top + 18), COLOR_HEART);
        COLORREF rowBg = CCC_ListGlassRowBg(ni, bS, bH);
        const CCC_RowGlassStyle* pCustom = LookupRowStyle(ni);
        if (pCustom && pCustom->bValid)
            rowBg = pCustom->clrBg;
        COLORREF iconBg = rowBg;
#if CCUSTOM_AERO_SUPPORT
        const BOOL bGlassLv = CCC_UseListCtrlRowGlass(m_hWnd);
        if (!bGlassLv && CCC_IsBlurDialogChild(m_hWnd) && CCC_IsAeroEnabled())
        {
            const BYTE alpha = CCC_ListGlassRowAlpha(ni, bS, bH);
            iconBg = CCC_RowSolidFillColor(m_hWnd, m_hWnd, m_bAeroMode, rowBg, alpha, bS, pCustom);
        }
#else
        const BOOL bGlassLv = FALSE;
#endif
        CRect ri;
        if (GetItemRect(ni, &ri, LVIR_ICON))
        {
            LVITEM lvi = { 0 };
            lvi.mask = LVIF_IMAGE;
            lvi.iItem = ni;
            GetItem(&lvi);
            CImageList* pIL = GetImageList(LVSIL_SMALL);
            if (pIL && lvi.iImage >= 0)
            {
                const COLORREF iconFill = bGlassLv ? CLR_NONE : iconBg;
                DrawTransparentIcon(pDC, pIL, lvi.iImage, ri, RGB(255, 255, 255), iconFill);
            }
        }
        if (bH && !bS) DrawStar(pDC, r.left + 10, r.top + 10, 2, RGB(255, 215, 0));
    }

    CString st = CCC_FetchListSubitemText(m_hWnd, ni, ns);
#if CCUSTOM_AERO_SUPPORT
    const BOOL bGlassText = CCC_UseListCtrlRowGlass(m_hWnd);
#else
    const BOOL bGlassText = FALSE;
#endif
    pDC->SetTextColor((m_bAeroMode && !bGlassText) ? RGB(1, 1, 1) : RGB(0, 0, 0));
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
            CImageList* pIL = GetImageList(LVSIL_SMALL);
            if (pIL && lvi.iImage >= 0 && ri.Width() > 0)
                tl = (std::max)(tl, (int)ri.right + 4);
        }
        tl = (std::min)(tl, (int)r.right - 4);
        rt.left = (std::max)(tl, (int)r.left + 4);
    }
    else
        rt.left += 6;
    rt.DeflateRect(2, 0);

    CFont* po = pDC->SelectObject(GetFont());
    DrawListSubitemCellText(pDC, st, rt);
    pDC->SelectObject(po);

    if (ns == nLC)
        DrawLaceLine(pDC, r.left + 10, r.bottom - 1, r.right - 10, r.bottom - 1, RGB(200, 180, 220));
    if (GetExtendedStyle() & LVS_EX_GRIDLINES)
    {
        CPen pp(PS_SOLID, 1, RGB(220, 220, 230));
        CPen* po2 = pDC->SelectObject(&pp);
        pDC->MoveTo(r.left, r.bottom - 1);
        pDC->LineTo(r.right, r.bottom - 1);
        pDC->SelectObject(po2);
    }
}

void CCustomListCtrl::DrawListCtrlSubItem(CDC* pDC, int ni, int ns, int nLC, const CRect& rcClient)
{
    if (!pDC || ni < 0) return;

    CRect r;
    if (!GetSubItemRect(ni, ns, LVIR_BOUNDS, r)) return;

    if (ns == 0)
    {
        int cx0 = GetColumnWidth(0);
        if (cx0 > 0) r.right = (std::min)((int)r.right, (int)r.left + cx0);
    }
    if (ns >= nLC - 1 && r.right < rcClient.right) r.right = (int)rcClient.right;

    const BOOL bS = (GetItemState(ni, LVIS_SELECTED) & LVIS_SELECTED);
    const BOOL bH = (ni == m_nHotItem);
    COLORREF bg = CCC_ListGlassRowBg(ni, bS, bH);
    const CCC_RowGlassStyle* pCustom = LookupRowStyle(ni);
    if (pCustom && pCustom->bValid && pCustom->bUseGlass)
        bg = pCustom->clrBg;
    const BYTE alpha = CCC_ListGlassRowAlpha(ni, bS, bH);
    CCC_DrawRowGlassBackground(pDC, r, m_hWnd, m_bAeroMode, bg, alpha, bS, pCustom);
    DrawListCtrlSubItemForeground(pDC, ni, ns, nLC, rcClient);
}

LRESULT CCustomListCtrl::OnPrintClient(WPARAM wParam, LPARAM)
{
    CDC* pDC = CDC::FromHandle((HDC)wParam);
    if (!pDC || !GetSafeHwnd()) return 0;

    CRect rcC;
    GetClientRect(&rcC);
    if (rcC.Width() <= 0 || rcC.Height() <= 0) return 0;

#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListCtrlRowGlass(m_hWnd))
    {
        PaintGlassClient(*pDC);
        return 0;
    }
#endif
    PaintListCtrlClient(*pDC, FALSE);
    return 0;
}

void CCustomListCtrl::OnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    const NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
    if (!p || !(p->uChanged & LVIF_STATE)) return;
    const UINT stMask = LVIS_SELECTED | LVIS_FOCUSED;
    if (!(p->uOldState & stMask) && !(p->uNewState & stMask)) return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseListCtrlRowGlass(m_hWnd))
        RequestGlassRepaint();
    else if (CCC_DeferListToOpaquePaint(m_hWnd))
        SendMessage(CCC_WM_POST_OPAQUE_PAINT);
#endif
}

void CCustomListCtrl::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVCUSTOMDRAW* p = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
    *pResult = CDRF_DODEFAULT;
#if CCUSTOM_AERO_SUPPORT
    static thread_local BOOL s_tlGlassPaintedThisFrame = FALSE;
    if (p->nmcd.dwDrawStage == CDDS_PREPAINT)
        s_tlGlassPaintedThisFrame = FALSE;
#endif
    switch (p->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
#if CCUSTOM_AERO_SUPPORT
        if (CCC_UseListCtrlRowGlass(m_hWnd))
        {
            CDC dc;
            dc.Attach(p->nmcd.hdc);
            PaintGlassClient(dc);
            dc.Detach();
            s_tlGlassPaintedThisFrame = TRUE;
            *pResult = CDRF_SKIPDEFAULT;
            return;
        }
        if (CCC_DeferListToOpaquePaint(m_hWnd))
        {
            *pResult = CDRF_SKIPDEFAULT;
            SendMessage(CCC_WM_POST_OPAQUE_PAINT);
            return;
        }
#endif
        *pResult = CDRF_NOTIFYITEMDRAW;
        break;
    case CDDS_ITEMPREPAINT:
#if CCUSTOM_AERO_SUPPORT
        if (CCC_UseListCtrlRowGlass(m_hWnd))
        {
            if (!s_tlGlassPaintedThisFrame)
            {
                CDC dc;
                dc.Attach(p->nmcd.hdc);
                PaintGlassClient(dc);
                dc.Detach();
                s_tlGlassPaintedThisFrame = TRUE;
            }
            *pResult = CDRF_SKIPDEFAULT;
            return;
        }
#endif
        *pResult = CDRF_NOTIFYSUBITEMDRAW;
        break;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
    {
        CDC* pDC = CDC::FromHandle(p->nmcd.hdc);
        int ni = (int)p->nmcd.dwItemSpec;
        int ns = p->iSubItem;
        CRect rcC;
        GetClientRect(&rcC);
        int nLC = GetHeaderCtrl()->GetItemCount() - 1;
        DrawListCtrlSubItem(pDC, ni, ns, nLC, rcC);
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
END_MESSAGE_MAP()

CCustomStandardButton::CCustomStandardButton()
    : m_bAutoDelete(FALSE), m_bMouseOver(FALSE), m_clrGradStart(RGB(255, 255, 255)),
    m_clrGradEnd(RGB(255, 255, 255)), m_nGradDirection(0), m_bGradEnable(FALSE),
    m_clrShadow(RGB(0, 0, 0)), m_nShadowDirection(135), m_nShadowDistance(2),
    m_nShadowBlur(3), m_bShadowEnable(FALSE)
{
    m_brBackground.CreateSolidBrush(COLOR_BUTTON_BG);
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
    CDC mDC;
    CBitmap mB;
    mDC.CreateCompatibleDC(&dc);
    mB.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
    CBitmap* ob = mDC.SelectObject(&mB);

    BOOL bP = (GetState() & BST_PUSHED) != 0;
    BOOL bF = (GetFocus() == this);
    BOOL bD = !IsWindowEnabled();
    if (((GetStyle() & BS_TYPEMASK) == BS_CHECKBOX || (GetStyle() & BS_TYPEMASK) == BS_AUTOCHECKBOX)
        && (GetStyle() & BS_PUSHLIKE))
    {
        if (GetCheck() == BST_CHECKED) bP = TRUE;
    }

    COLORREF bg = bD ? RGB(200, 200, 200)
        : (bP ? COLOR_BUTTON_PUSHED : (m_bMouseOver ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG));
    if (m_bGradEnable && !bD)
        DrawGradientBackground(&mDC, r, m_clrGradStart, m_clrGradEnd, m_nGradDirection);
    else
        mDC.FillSolidRect(&r, bg);

    if (!bD)
    {
        DrawDecorations(&mDC, r, 0, bP);
        if (m_bMouseOver && !bP)
        {
            DrawFlower(&mDC, r.Width() / 2 - 15, r.top + 10, 6, RGB(255, 200, 220));
            DrawFlower(&mDC, r.Width() / 2 + 15, r.top + 10, 6, RGB(255, 200, 220));
            DrawFlower(&mDC, r.Width() / 2, r.bottom - 10, 6, RGB(255, 200, 220));
        }
        if (bP)
        {
            DrawStar(&mDC, r.Width() / 2, r.top + 8, 3, RGB(255, 215, 0));
            DrawStar(&mDC, r.left + 15, r.Height() / 2, 2, RGB(255, 240, 150));
            DrawStar(&mDC, r.right - 15, r.Height() / 2, 2, RGB(255, 240, 150));
            DrawStar(&mDC, r.Width() / 2, r.bottom - 8, 2, RGB(255, 240, 150));
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
#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
    {
        CClientDC dc(this);
        PaintOpaqueClient(dc);
        return;
    }
#endif
    CClientDC dc(this);
    CRect r;
    GetClientRect(&r);
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
        Invalidate(FALSE);
    }
    CButton::OnMouseMove(f, p);
}

LRESULT CCustomStandardButton::OnMouseLeave(WPARAM, LPARAM)
{
    m_bMouseOver = FALSE;
    Invalidate(FALSE);
    return 0;
}

void CCustomStandardButton::OnSetFocus(CWnd* p)
{
    CButton::OnSetFocus(p);
    Invalidate(FALSE);
}

void CCustomStandardButton::OnKillFocus(CWnd* p)
{
    CButton::OnKillFocus(p);
    Invalidate(FALSE);
}

void CCustomStandardButton::OnEnable(BOOL b)
{
    CButton::OnEnable(b);
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
    ON_WM_SHOWWINDOW()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
END_MESSAGE_MAP()

CCustomCheckBox::CCustomCheckBox()
    : m_bAutoDelete(FALSE), m_bIsFlatStyle(FALSE), m_bIsPushLike(FALSE), m_bIsPressed(FALSE),
    m_bIsHot(FALSE), m_bTracking(FALSE), m_nCheck(0), m_bAeroMode(FALSE)
{}

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
    Invalidate();
}

void CCustomCheckBox::RepaintClient()
{
    if (!GetSafeHwnd())
        return;
    CClientDC dc(this);
    CRect r;
    GetClientRect(&r);
    OnDrawLayer(&dc, r);
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
    m_bIsPushLike = (GetStyle() & BS_PUSHLIKE) != 0;
    m_bIsFlatStyle = (GetStyle() & BS_FLAT) || m_bIsPushLike;
    m_nCheck = CButton::GetCheck();
    ModifyStyle(BS_FLAT | BS_PUSHLIKE, 0);
    CButton::PreSubclassWindow();
    Invalidate(FALSE);
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

void CCustomCheckBox::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CButton::OnShowWindow(bShow, nStatus);
    if (bShow)
        RepaintClient();
}

void CCustomCheckBox::OnPaint()
{
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
    OnDrawLayer(&dc, r);
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

static void CCC_CompositeTransparent(HWND hWnd, BOOL bAeroMode, CDC& destDC, const CRect& rect, std::function<void(CDC&)> drawFn)
{
    const BOOL bTrans = CCC_UseTransparentPaint(hWnd, bAeroMode);
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
    CCC_BlitTransparentChroma(destDC.GetSafeHdc(), rect.left, rect.top, rect.Width(), rect.Height(),
        memDC.GetSafeHdc(), 0, 0, CCC_AERO_CHROMA_KEY);
    memDC.SelectObject(pOld);
}

void CCustomCheckBox::OnDrawLayer(CDC* pDC, CRect rect)
{
    const int rw = rect.Width();
    const int rh = rect.Height();
    if (rw <= 0 || rh <= 0) return;

    CCC_CompositeTransparent(m_hWnd, m_bAeroMode, *pDC, rect, [&](CDC& dc)
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
            if (CCC_UseTransparentPaint(m_hWnd, m_bAeroMode) && !m_bIsPushLike)
            {
                const CRect full(0, 0, rw, rh);
                CCC_RemapSolidColorInDC(dc, full, COLOR_BUTTON_BG, CCC_AERO_CHROMA_KEY);
                CCC_RemapSolidColorInDC(dc, full, COLOR_BUTTON_HOVER, CCC_AERO_CHROMA_KEY);
                CCC_RemapSolidColorInDC(dc, full, COLOR_BUTTON_PUSHED, CCC_AERO_CHROMA_KEY);
                CCC_RemapSolidColorInDC(dc, full, RGB(200, 200, 200), CCC_AERO_CHROMA_KEY);
            }
            else if (CCC_UseTransparentPaint(m_hWnd, m_bAeroMode) && m_bIsPushLike)
            {
                // ボタン見た目: 前景色は不透明のまま、隙間のみガラス（CCustomStandardButton へ移行推奨）
            }
        }
        else
        {
            const BOOL bTrans = CCC_UseTransparentPaint(m_hWnd, m_bAeroMode);
            if (!bTrans) dc.FillSolidRect(0, 0, rw, rh, COLOR_DIALOG_BG);
            int s = 18; int cy2 = rh / 2;
            CRect rcB(0, cy2 - s / 2, s, cy2 + s / 2);
            CPen p2(PS_SOLID, 2, RGB(255, 140, 100)); CBrush b2(RGB(255, 255, 255));
            dc.SelectObject(&p2); dc.SelectObject(&b2);
            dc.RoundRect(&rcB, CPoint(5, 5));
            if (bC)
            {
                CRect rh2 = rcB;
                rh2.DeflateRect(1, 1);
                DrawHanamaru(&dc, rh2, RGB(255, 100, 150), RGB(255, 182, 193));
            }
            CString t;
            GetWindowText(t);
            if (!t.IsEmpty())
            {
                CRect rt(0, 0, rw, rh);
                rt.left = rcB.right + 8;
                DrawSmartText2(&dc, rt, t, DT_LEFT | DT_VCENTER, bD, FALSE);
            }
        }
        if (GetFocus() == this)
        {
            CRect rf(0, 0, rw, rh);
            if (!m_bIsFlatStyle) rf.left += 20; else rf.DeflateRect(3, 3);
            dc.DrawFocusRect(&rf);
        }
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
    if (CCC_UseTransparentPaint(m_hWnd, m_bAeroMode)) return TRUE;
#endif
    if (pDC)
    {
        CRect r;
        GetClientRect(&r);
        pDC->FillSolidRect(&r, COLOR_DIALOG_BG);
    }
    return TRUE;
}

void CCustomGroupBox::DrawGroupBox(CDC* pDC, CRect& rect)
{
    CCC_CompositeTransparent(m_hWnd, m_bAeroMode, *pDC, rect, [&](CDC& dc)
    {
        CRect r(0, 0, rect.Width(), rect.Height());
        CString t; GetWindowText(t);
        CFont* pOF = dc.SelectObject(GetFont());
        CSize s = dc.GetTextExtent(t);
        int nT = r.top + s.cy / 2;
        const BOOL bTrans = CCC_UseTransparentPaint(m_hWnd, m_bAeroMode);
        if (!bTrans) dc.FillSolidRect(&r, COLOR_DIALOG_BG);

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
        int off = 3;
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

        DrawRibbon(&dc, CRect(r.left + 2, nT - 8, r.left + 14, nT + 4), RGB(255, 182, 193));
        DrawRibbon(&dc, CRect(r.right - 14, nT - 8, r.right - 2, nT + 4), RGB(255, 182, 193));
        DrawRibbon(&dc, CRect(r.left + 2, r.bottom - 12, r.left + 14, r.bottom), RGB(255, 182, 193));
        DrawRibbon(&dc, CRect(r.right - 14, r.bottom - 12, r.right - 2, r.bottom), RGB(255, 182, 193));

        if (!t.IsEmpty())
        {
            CRect rt(r.left + 8, nT - s.cy / 2, r.left + 8 + s.cx + 4, nT + s.cy / 2);
            if (bTrans) dc.FillSolidRect(&rt, CCC_AERO_CHROMA_KEY);
            else dc.FillSolidRect(&rt, COLOR_DIALOG_BG);
            dc.SetBkMode(TRANSPARENT);
            dc.SetTextColor(RGB(0, 0, 0));
            dc.DrawText(t, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        dc.SelectObject(pOF);
    });
}

// ============================================================================
// カスタムダイアログクラス基底 (CDialog版)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomDialog, CDialog)

BEGIN_MESSAGE_MAP(CCustomDialog, CDialog)
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

CCustomDialog::CCustomDialog(UINT n, CWnd* p) : CDialog(n, p), m_bAeroEnabled(FALSE)
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
    BOOL r = CDialog::OnInitDialog();
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
    return h ? h : CDialog::OnCtlColor(pDC, pWnd, nC);
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
        CDialog::OnPaint();
}

#if CCUSTOM_AERO_SUPPORT
static BOOL CCC_PaintChildDirect(HWND hWnd, HDC hdcBuf);

void CCC_PaintButtonSTGlass(HWND hWnd, CButtonST* pBtn, HDC hdcDest)
{
    if (!hWnd || !pBtn || !hdcDest) return;
    RECT rect = {};
    ::GetClientRect(hWnd, &rect);
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;

    CDC memDC;
    memDC.CreateCompatibleDC(CDC::FromHandle(hdcDest));
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(CDC::FromHandle(hdcDest), w, h);
    CBitmap* pOld = memDC.SelectObject(&bmp);
    memDC.FillSolidRect(0, 0, w, h, CCC_AERO_CHROMA_KEY);

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
    dis.hDC = memDC.GetSafeHdc();
    dis.rcItem = { 0, 0, w, h };
    pBtn->BeginGlassCompositeDraw();
    pBtn->DrawItem(&dis);
    pBtn->EndGlassCompositeDraw();

    CRect rcC(0, 0, w, h);
    CDC dcDest;
    dcDest.Attach(hdcDest);
    CCC_CommitUniformGlassFromFgMem(dcDest, rcC, COLOR_BUTTON_BG, memDC.GetSafeHdc(), w, h, FALSE);
    dcDest.Detach();
    memDC.SelectObject(pOld);
}

static void CCC_DrawButtonSTClient(HWND hWnd, CButtonST* pBtn, HDC hdc, const RECT& rect)
{
    CBrush br(COLOR_BUTTON_BG);
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
}

static void CCC_RepaintGlassHwnd(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd))
        return;
    // 親の UpdateWindow では子の WM_PAINT は走らない。各ガラス子を同期再描画する。
    ::RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

static void CCC_ForcePaintButtonST(HWND hWnd, CButtonST* pBtn)
{
    if (!hWnd || !::IsWindow(hWnd) || !pBtn)
        return;
#if CCUSTOM_AERO_SUPPORT
    if (CCC_UseBlurChildGlassPaint(hWnd))
    {
        CCC_RepaintGlassHwnd(hWnd);
        return;
    }
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
    {
        ::PostMessage(hWnd, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
        return;
    }
#endif
    pBtn->ClearBackgroundCache();
    CWnd* pw = CWnd::FromHandlePermanent(hWnd);
    if (!pw) return;
    RECT rect = {};
    ::GetClientRect(hWnd, &rect);
    if (rect.right <= rect.left || rect.bottom <= rect.top)
        return;
    CClientDC dc(pw);
    CCC_DrawButtonSTClient(hWnd, pBtn, dc.GetSafeHdc(), rect);
}

class CCustomButtonSTGlassFixer
{
public:
    CCustomButtonSTGlassFixer() : m_hWnd(NULL), m_pBtn(NULL) {}
    ~CCustomButtonSTGlassFixer() { Uninstall(); }

    BOOL Install(HWND hWnd, CButtonST* pBtn)
    {
        if (m_hWnd || !pBtn) return FALSE;
        if (!::IsWindow(hWnd)) return FALSE;
        m_hWnd = hWnd;
        m_pBtn = pBtn;
        m_pBtn->SetAeroGlassMode(TRUE);
        return ::SetWindowSubclass(hWnd, SubclassProc, (UINT_PTR)this, (DWORD_PTR)this);
    }

    void Uninstall()
    {
        if (m_hWnd && ::IsWindow(m_hWnd))
            ::RemoveWindowSubclass(m_hWnd, SubclassProc, (UINT_PTR)this);
        m_hWnd = NULL;
        m_pBtn = NULL;
    }

private:
    HWND m_hWnd;
    CButtonST* m_pBtn;

    static void PaintBtn(CCustomButtonSTGlassFixer* pThis, HDC hDC)
    {
        if (!pThis || !pThis->m_pBtn || !hDC || !pThis->m_hWnd)
            return;
        CCC_PaintButtonSTGlass(pThis->m_hWnd, pThis->m_pBtn, hDC);
    }

    static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        CCustomButtonSTGlassFixer* pThis = (CCustomButtonSTGlassFixer*)dwRefData;
        switch (uMsg)
        {
        case WM_ERASEBKGND:
            return TRUE;
        case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC hDC = ::BeginPaint(hWnd, &ps);
            if (hDC)
                PaintBtn(pThis, hDC);
            ::EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            PaintBtn(pThis, (HDC)wParam);
            return 0;
        case WM_SHOWWINDOW:
        {
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            if (wParam && pThis->m_pBtn)
                CCC_ForcePaintButtonST(hWnd, pThis->m_pBtn);
            return lRes;
        }
        case WM_DESTROY:
            ::RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
            pThis->m_hWnd = NULL;
            pThis->m_pBtn = NULL;
            break;
        }
        return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }
};

static std::map<HWND, CCustomButtonSTGlassFixer*> g_buttonSTGlassFixers;

static void CCC_PaintEditGlass(HWND hWnd, CCustomEdit* pEdit, HDC hdcDest)
{
    if (!hWnd || !pEdit || !hdcDest) return;
    CDC dc;
    dc.Attach(hdcDest);
    pEdit->PaintGlassClient(dc, FALSE);
    dc.Detach();
}

static void CCC_PaintComboGlass(HWND hWnd, CCustomComboBox* pCombo, HDC hdcDest)
{
    if (!hWnd || !pCombo || !hdcDest) return;
    CDC dc;
    dc.Attach(hdcDest);
    pCombo->PaintGlassClient(dc, FALSE);
    dc.Detach();
}

class CCustomEditGlassFixer
{
public:
    CCustomEditGlassFixer() : m_hWnd(NULL), m_pEdit(NULL) {}
    ~CCustomEditGlassFixer() { Uninstall(); }

    BOOL Install(HWND hWnd, CCustomEdit* pEdit)
    {
        if (m_hWnd || !pEdit) return FALSE;
        if (!::IsWindow(hWnd)) return FALSE;
        m_hWnd = hWnd;
        m_pEdit = pEdit;
        return ::SetWindowSubclass(hWnd, SubclassProc, (UINT_PTR)this, (DWORD_PTR)this);
    }

    void Uninstall()
    {
        if (m_hWnd && ::IsWindow(m_hWnd))
            ::RemoveWindowSubclass(m_hWnd, SubclassProc, (UINT_PTR)this);
        m_hWnd = NULL;
        m_pEdit = NULL;
    }

private:
    HWND m_hWnd;
    CCustomEdit* m_pEdit;

    static void PaintEdit(CCustomEditGlassFixer* pThis, HDC hDC)
    {
        if (!pThis || !pThis->m_pEdit || !hDC || !pThis->m_hWnd)
            return;
        CCC_PaintEditGlass(pThis->m_hWnd, pThis->m_pEdit, hDC);
    }

    static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        CCustomEditGlassFixer* pThis = (CCustomEditGlassFixer*)dwRefData;
        switch (uMsg)
        {
        case WM_ERASEBKGND:
            return TRUE;
        case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC hDC = ::BeginPaint(hWnd, &ps);
            if (hDC)
                PaintEdit(pThis, hDC);
            ::EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            PaintEdit(pThis, (HDC)wParam);
            return 0;
        case WM_DESTROY:
            ::RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
            pThis->m_hWnd = NULL;
            pThis->m_pEdit = NULL;
            break;
        }
        return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }
};

class CCustomComboGlassFixer
{
public:
    CCustomComboGlassFixer() : m_hWnd(NULL), m_pCombo(NULL) {}
    ~CCustomComboGlassFixer() { Uninstall(); }

    BOOL Install(HWND hWnd, CCustomComboBox* pCombo)
    {
        if (m_hWnd || !pCombo) return FALSE;
        if (!::IsWindow(hWnd)) return FALSE;
        m_hWnd = hWnd;
        m_pCombo = pCombo;
        COMBOBOXINFO cbi = { sizeof(COMBOBOXINFO) };
        if (::GetComboBoxInfo(hWnd, &cbi) && cbi.hwndItem && cbi.hwndItem != hWnd)
            ::ShowWindow(cbi.hwndItem, SW_HIDE);
        return ::SetWindowSubclass(hWnd, SubclassProc, (UINT_PTR)this, (DWORD_PTR)this);
    }

    void Uninstall()
    {
        if (m_hWnd && ::IsWindow(m_hWnd))
            ::RemoveWindowSubclass(m_hWnd, SubclassProc, (UINT_PTR)this);
        m_hWnd = NULL;
        m_pCombo = NULL;
    }

private:
    HWND m_hWnd;
    CCustomComboBox* m_pCombo;

    static void PaintCombo(CCustomComboGlassFixer* pThis, HDC hDC)
    {
        if (!pThis || !pThis->m_pCombo || !hDC || !pThis->m_hWnd)
            return;
        CCC_PaintComboGlass(pThis->m_hWnd, pThis->m_pCombo, hDC);
    }

    static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        CCustomComboGlassFixer* pThis = (CCustomComboGlassFixer*)dwRefData;
        switch (uMsg)
        {
        case WM_ERASEBKGND:
            return TRUE;
        case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC hDC = ::BeginPaint(hWnd, &ps);
            if (hDC)
                PaintCombo(pThis, hDC);
            ::EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            PaintCombo(pThis, (HDC)wParam);
            return 0;
        case WM_DESTROY:
            ::RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
            pThis->m_hWnd = NULL;
            pThis->m_pCombo = NULL;
            break;
        }
        return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }
};

static std::map<HWND, CCustomEditGlassFixer*> g_editGlassFixers;
static std::map<HWND, CCustomComboGlassFixer*> g_comboGlassFixers;

static void CCC_ClearEditComboGlassFixersForDialog(HWND hDlg)
{
    if (!hDlg) return;
    for (auto it = g_editGlassFixers.begin(); it != g_editGlassFixers.end(); )
    {
        if (CCC_IsDescendantOf(hDlg, it->first))
        {
            if (it->second) { it->second->Uninstall(); delete it->second; }
            it = g_editGlassFixers.erase(it);
        }
        else
            ++it;
    }
    for (auto it = g_comboGlassFixers.begin(); it != g_comboGlassFixers.end(); )
    {
        if (CCC_IsDescendantOf(hDlg, it->first))
        {
            if (it->second) { it->second->Uninstall(); delete it->second; }
            it = g_comboGlassFixers.erase(it);
        }
        else
            ++it;
    }
}

static void CCC_InstallEditComboGlassFixersRecursive(HWND hParent)
{
    for (HWND hChild = ::GetWindow(hParent, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (CWnd* pw = CWnd::FromHandlePermanent(hChild))
        {
            if (CCC_UseEditGlass(hChild))
            {
                if (CCustomEdit* pEdit = dynamic_cast<CCustomEdit*>(pw))
                {
                    if (g_editGlassFixers.find(hChild) == g_editGlassFixers.end())
                    {
                        CCustomEditGlassFixer* pFixer = new CCustomEditGlassFixer();
                        if (pFixer->Install(hChild, pEdit))
                            g_editGlassFixers[hChild] = pFixer;
                        else
                            delete pFixer;
                    }
                    ::InvalidateRect(hChild, NULL, FALSE);
                }
            }
            if (CCC_UseComboGlass(hChild))
            {
                if (CCustomComboBox* pCombo = dynamic_cast<CCustomComboBox*>(pw))
                {
                    if (g_comboGlassFixers.find(hChild) == g_comboGlassFixers.end())
                    {
                        CCustomComboGlassFixer* pFixer = new CCustomComboGlassFixer();
                        if (pFixer->Install(hChild, pCombo))
                            g_comboGlassFixers[hChild] = pFixer;
                        else
                            delete pFixer;
                    }
                    ::InvalidateRect(hChild, NULL, FALSE);
                }
            }
        }
        CCC_InstallEditComboGlassFixersRecursive(hChild);
    }
}

static void CCC_ForcePaintGlassFieldsOnDialog(HWND hDlg)
{
    if (!hDlg || !::IsWindow(hDlg)) return;
    for (HWND hChild = ::GetWindow(hDlg, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (CCC_UseEditGlass(hChild) || CCC_UseComboGlass(hChild))
            CCC_RepaintGlassHwnd(hChild);
        CCC_ForcePaintGlassFieldsOnDialog(hChild);
    }
}

static void CCC_ReinstallBlurGlassFixersOnDialog(HWND hDlg)
{
    if (!hDlg || !::IsWindow(hDlg) || !CCC_IsAeroEnabled() || !CCC_IsWin11()) return;
    CCC_InstallButtonSTGlassFixersRecursive(hDlg);
    CCC_InstallEditComboGlassFixersRecursive(hDlg);
    CCC_ForcePaintAllButtonSTOnDialog(hDlg);
    CCC_ForcePaintGlassFieldsOnDialog(hDlg);
}

static void CCC_RestoreParentBlurGlassFixers(CWnd* pDlg)
{
    if (!pDlg) return;
    CWnd* pParent = pDlg->GetParent();
    if (!pParent || !pParent->GetSafeHwnd()) return;
    if (dynamic_cast<CCustomBlurDialogBase*>(pParent) || dynamic_cast<CCustomBlurDialogExBase*>(pParent))
        CCC_ReinstallBlurGlassFixersOnDialog(pParent->GetSafeHwnd());
}

static BOOL CCC_IsDescendantOf(HWND hAncestor, HWND hWnd)
{
    if (!hAncestor || !hWnd || !::IsWindow(hAncestor) || !::IsWindow(hWnd))
        return FALSE;
    for (HWND h = hWnd; h; h = ::GetParent(h))
    {
        if (h == hAncestor)
            return TRUE;
    }
    return FALSE;
}

static void CCC_ClearButtonSTGlassFixersForDialog(HWND hDlg)
{
    if (!hDlg) return;
    for (auto it = g_buttonSTGlassFixers.begin(); it != g_buttonSTGlassFixers.end(); )
    {
        if (CCC_IsDescendantOf(hDlg, it->first))
        {
            if (it->second)
            {
                it->second->Uninstall();
                delete it->second;
            }
            it = g_buttonSTGlassFixers.erase(it);
        }
        else
            ++it;
    }
}

static void CCC_ClearButtonSTGlassFixers()
{
    for (auto it = g_buttonSTGlassFixers.begin(); it != g_buttonSTGlassFixers.end(); ++it)
    {
        if (it->second)
        {
            it->second->Uninstall();
            delete it->second;
        }
    }
    g_buttonSTGlassFixers.clear();
}

static void CCC_InstallButtonSTGlassFixersRecursive(HWND hParent)
{
    for (HWND hChild = ::GetWindow(hParent, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (CWnd* pw = CWnd::FromHandlePermanent(hChild))
        {
            if (CButtonST* pBtn = dynamic_cast<CButtonST*>(pw))
            {
                auto it = g_buttonSTGlassFixers.find(hChild);
                if (it != g_buttonSTGlassFixers.end())
                {
                    ::InvalidateRect(hChild, NULL, FALSE);
                }
                else
                {
                    CCustomButtonSTGlassFixer* pFixer = new CCustomButtonSTGlassFixer();
                    if (pFixer->Install(hChild, pBtn))
                    {
                        g_buttonSTGlassFixers[hChild] = pFixer;
                        CCC_ForcePaintButtonST(hChild, pBtn);
                    }
                    else
                        delete pFixer;
                }
            }
        }
        CCC_InstallButtonSTGlassFixersRecursive(hChild);
    }
}

// Win11: 子ウィンドウの GDI はアルファ0のまま DWM に合成される。
// BufferedPaint で全面 alpha=255 にしてから WM_PRINTCLIENT で CCustom* を描く。
// ※WM_PRINTCLIENT を再帰的に PaintOpaque へ渡すと OnPrintClient が呼ばれず全面透過になる。
class CCustomOpaqueFixer
{
public:
    CCustomOpaqueFixer(COLORREF clrBg) : m_hWnd(NULL), m_bPrinting(FALSE), m_clrBg(clrBg) {}
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

    static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        CCustomOpaqueFixer* pThis = (CCustomOpaqueFixer*)dwRefData;
        switch (uMsg)
        {
        case WM_ERASEBKGND:
        {
            if (!pThis->m_bPrinting)
            {
                if (HDC hdc = (HDC)wParam)
                {
                    RECT rect = {};
                    ::GetClientRect(hWnd, &rect);
                    CBrush br(pThis->m_clrBg);
                    ::FillRect(hdc, &rect, (HBRUSH)br.GetSafeHandle());
                }
                return TRUE;
            }
            return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
        }
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
        case WM_LBUTTONUP:
        {
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
#if CCUSTOM_AERO_SUPPORT
            if (CCC_DeferListToOpaquePaint(hWnd))
            {
                HDC hDC = ::GetDC(hWnd);
                if (hDC)
                {
                    pThis->PaintOpaque(hWnd, hDC);
                    ::ReleaseDC(hWnd, hDC);
                }
            }
#endif
            return lRes;
        }
        case WM_VSCROLL:
        case WM_HSCROLL:
        case WM_MOUSEWHEEL:
        {
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
#if CCUSTOM_AERO_SUPPORT
            if (CCC_DeferListToOpaquePaint(hWnd))
            {
                HDC hDC = ::GetDC(hWnd);
                if (hDC)
                {
                    pThis->PaintOpaque(hWnd, hDC);
                    ::ReleaseDC(hWnd, hDC);
                }
                ::SendMessage(hWnd, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
            }
#endif
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

    void PaintOpaque(HWND hWnd, HDC hDestDC)
    {
        RECT rect = {};
        ::GetClientRect(hWnd, &rect);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0) return;

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

static std::map<HWND, CCustomOpaqueFixer*> g_comboDropFixers;
static std::map<HWND, class CCustomComboDropGlassFixer*> g_comboDropGlassFixers;

static BOOL CCC_IsComboDropGlassInstalled(HWND hwndCombo)
{
    return hwndCombo && g_comboDropGlassFixers.find(hwndCombo) != g_comboDropGlassFixers.end();
}

static void CCC_RepaintComboDropGlass(HWND hwndCombo)
{
    if (!CCC_IsComboDropGlassInstalled(hwndCombo)) return;
    COMBOBOXINFO cbi = { sizeof(COMBOBOXINFO) };
    if (!::GetComboBoxInfo(hwndCombo, &cbi) || !cbi.hwndList || !::IsWindow(cbi.hwndList))
        return;
    ::InvalidateRect(cbi.hwndList, NULL, FALSE);
    ::UpdateWindow(cbi.hwndList);
}

class CCustomComboDropGlassFixer
{
public:
    CCustomComboDropGlassFixer(HWND hwndCombo) : m_hwndCombo(hwndCombo), m_hwndList(NULL), m_bTrackingMouse(FALSE) {}
    ~CCustomComboDropGlassFixer() { Uninstall(); }

    BOOL Install(HWND hwndList)
    {
        if (m_hwndList) return FALSE;
        if (!::IsWindow(hwndList) || !::IsWindow(m_hwndCombo)) return FALSE;
        m_hwndList = hwndList;
        return ::SetWindowSubclass(hwndList, SubclassProc, (UINT_PTR)this, (DWORD_PTR)this);
    }

    void Uninstall()
    {
        if (m_hwndList && ::IsWindow(m_hwndList))
            ::RemoveWindowSubclass(m_hwndList, SubclassProc, (UINT_PTR)this);
        m_hwndList = NULL;
    }

private:
    HWND m_hwndCombo;
    HWND m_hwndList;
    BOOL m_bTrackingMouse;

    void PaintGlass(HWND hWnd, HDC hDestDC)
    {
        CCustomComboBox* pCombo = dynamic_cast<CCustomComboBox*>(CWnd::FromHandlePermanent(m_hwndCombo));
        if (!pCombo) return;
        pCombo->PaintDropListGlassClient(hWnd, hDestDC);
    }

    static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
        UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
    {
        CCustomComboDropGlassFixer* pThis = (CCustomComboDropGlassFixer*)dwRefData;
        switch (uMsg)
        {
        case WM_ERASEBKGND:
            return TRUE;
        case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC hDC = ::BeginPaint(hWnd, &ps);
            if (hDC) pThis->PaintGlass(hWnd, hDC);
            ::EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            if (HDC hdc = (HDC)wParam)
                pThis->PaintGlass(hWnd, hdc);
            return 0;
        case WM_MOUSEMOVE:
        {
            if (!pThis->m_bTrackingMouse)
            {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
                if (::TrackMouseEvent(&tme))
                    pThis->m_bTrackingMouse = TRUE;
            }
            CCustomComboBox* pCombo = dynamic_cast<CCustomComboBox*>(CWnd::FromHandlePermanent(pThis->m_hwndCombo));
            if (pCombo)
            {
                const DWORD hit = (DWORD)::SendMessage(hWnd, LB_ITEMFROMPOINT, 0, lParam);
                const int idx = (int)(short)LOWORD(hit);
                if (HIWORD(hit) == 0 && idx >= 0)
                {
                    if (pCombo->GetDropListHotItem() != idx)
                    {
                        pCombo->SetDropListHotItem(idx);
                        ::InvalidateRect(hWnd, NULL, FALSE);
                        ::UpdateWindow(hWnd);
                    }
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
        {
            pThis->m_bTrackingMouse = FALSE;
            if (CCustomComboBox* pCombo = dynamic_cast<CCustomComboBox*>(CWnd::FromHandlePermanent(pThis->m_hwndCombo)))
            {
                if (pCombo->GetDropListHotItem() != -1)
                {
                    pCombo->SetDropListHotItem(-1);
                    ::InvalidateRect(hWnd, NULL, FALSE);
                    ::UpdateWindow(hWnd);
                }
            }
            return 0;
        }
        case WM_VSCROLL:
        case WM_MOUSEWHEEL:
        case WM_LBUTTONUP:
        case WM_KEYDOWN:
        {
            LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            CCC_RepaintComboDropGlass(pThis->m_hwndCombo);
            return lRes;
        }
        case WM_TIMER:
            if (wParam == kComboDropBackdropWarmTimerId)
            {
                ::KillTimer(hWnd, kComboDropBackdropWarmTimerId);
                ::InvalidateRect(hWnd, NULL, FALSE);
                ::UpdateWindow(hWnd);
                return 0;
            }
            break;
        case WM_SHOWWINDOW:
            if (wParam)
            {
                CCC_InvalidateComboDropBackdrop(hWnd);
                ::SetTimer(hWnd, kComboDropBackdropWarmTimerId, 1, NULL);
                return 0;
            }
            return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
        case WM_DESTROY:
            ::KillTimer(hWnd, kComboDropBackdropWarmTimerId);
            CCC_InvalidateComboDropBackdrop(hWnd);
            ::RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
            pThis->m_hwndList = NULL;
            break;
        }
        return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }
};

static void CCC_InstallComboDropListOpaqueFix(HWND hwndCombo)
{
    if (!hwndCombo || !::IsWindow(hwndCombo)) return;
    if (!CCC_IsAeroEnabled() || !CCC_IsWin11() || !CCC_IsBlurDialogChild(hwndCombo))
        return;

    CCC_ReleaseComboDropListOpaqueFix(hwndCombo);

    COMBOBOXINFO cbi = { sizeof(COMBOBOXINFO) };
    if (!::GetComboBoxInfo(hwndCombo, &cbi) || !cbi.hwndList || !::IsWindow(cbi.hwndList))
        return;

    CCustomOpaqueFixer* pFixer = new CCustomOpaqueFixer(COLOR_COMBO_BG);
    if (!pFixer->Install(cbi.hwndList))
    {
        delete pFixer;
        return;
    }
    g_comboDropFixers[hwndCombo] = pFixer;
    ::InvalidateRect(cbi.hwndList, NULL, TRUE);
    ::UpdateWindow(cbi.hwndList);
}

static void CCC_ReleaseComboDropListOpaqueFix(HWND hwndCombo)
{
    auto it = g_comboDropFixers.find(hwndCombo);
    if (it == g_comboDropFixers.end()) return;
    if (it->second)
    {
        it->second->Uninstall();
        delete it->second;
    }
    g_comboDropFixers.erase(it);
}

static void CCC_InstallComboDropListGlassFix(HWND hwndCombo)
{
    if (!hwndCombo || !::IsWindow(hwndCombo)) return;
    if (!CCC_UseComboGlass(hwndCombo)) return;

    CCC_ReleaseComboDropListGlassFix(hwndCombo);

    COMBOBOXINFO cbi = { sizeof(COMBOBOXINFO) };
    if (!::GetComboBoxInfo(hwndCombo, &cbi) || !cbi.hwndList || !::IsWindow(cbi.hwndList))
        return;

    CCustomComboDropGlassFixer* pFixer = new CCustomComboDropGlassFixer(hwndCombo);
    if (!pFixer->Install(cbi.hwndList))
    {
        delete pFixer;
        return;
    }
    g_comboDropGlassFixers[hwndCombo] = pFixer;
    CCC_InvalidateComboDropBackdrop(cbi.hwndList);
    CCC_ApplyComboDropListDwm(hwndCombo, cbi.hwndList);
    ::InvalidateRect(cbi.hwndList, NULL, FALSE);
    ::UpdateWindow(cbi.hwndList);
    if (HWND hDlg = CCC_FindBlurDialogHwnd(hwndCombo))
        CCC_ForcePaintAllButtonSTOnDialog(hDlg);
}

static void CCC_ReleaseComboDropListGlassFix(HWND hwndCombo)
{
    auto it = g_comboDropGlassFixers.find(hwndCombo);
    if (it == g_comboDropGlassFixers.end()) return;
    if (it->second)
    {
        it->second->Uninstall();
        delete it->second;
    }
    g_comboDropGlassFixers.erase(it);
    CCC_InvalidateComboDropBackdrop(NULL);
    if (HWND hDlg = CCC_FindBlurDialogHwnd(hwndCombo))
        CCC_ForcePaintAllButtonSTOnDialog(hDlg);
}

static void CCC_InstallComboDropListFix(HWND hwndCombo)
{
    if (!hwndCombo || !::IsWindow(hwndCombo)) return;
    if (CCC_UseComboGlass(hwndCombo))
        CCC_InstallComboDropListGlassFix(hwndCombo);
    else
        CCC_InstallComboDropListOpaqueFix(hwndCombo);
}

static void CCC_ReleaseComboDropListFix(HWND hwndCombo)
{
    CCC_ReleaseComboDropListGlassFix(hwndCombo);
    CCC_ReleaseComboDropListOpaqueFix(hwndCombo);
}

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
    if (c.Find(_T("COMBOLBOX")) >= 0) return COLOR_COMBO_BG;
    if (c.Find(_T("SYSLISTVIEW32")) >= 0) return COLOR_LIST_BG;
    if (c.Find(_T("COMBOBOX")) >= 0) return COLOR_COMBO_BG;
    return COLOR_DIALOG_BG;
}

static BOOL CCC_IsGlassComboInternalEdit(HWND hWnd)
{
    if (!hWnd) return FALSE;
    TCHAR cls[16] = {};
    if (!::GetClassName(hWnd, cls, 15)) return FALSE;
    if (_tcsicmp(cls, _T("Edit")) != 0) return FALSE;
    const HWND hParent = ::GetParent(hWnd);
    return hParent && CCC_UseComboGlass(hParent);
}

static BOOL CCC_IsTransparentBlurControl(HWND hWnd);

static BOOL CCC_IsTransparentBlurControl(HWND hWnd)
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
    if (CCC_IsTransparentBlurControl(hWnd)) return FALSE;
    if (CCC_IsGlassComboInternalEdit(hWnd)) return FALSE;
    if (CWnd* pw = CWnd::FromHandlePermanent(hWnd))
    {
        if (dynamic_cast<CCustomListBox*>(pw))
            return !CCC_UseListBoxRowGlass(hWnd);
        if (dynamic_cast<CCustomListCtrl*>(pw))
            return !CCC_UseListCtrlRowGlass(hWnd);
        if (dynamic_cast<CCustomComboBox*>(pw))
            return !CCC_UseComboGlass(hWnd);
        if (dynamic_cast<CButtonST*>(pw))
            return !CCC_UseBlurChildGlassPaint(hWnd);
        if (dynamic_cast<CCustomEdit*>(pw))
            return !CCC_UseEditGlass(hWnd);
        return FALSE;
    }
    TCHAR cls[64] = {};
    ::GetClassName(hWnd, cls, 63);
    CString c(cls);
    c.MakeUpper();
    if (c.Find(_T("BUTTON")) >= 0) return TRUE;
    if (c.Find(_T("LISTBOX")) >= 0) return TRUE;
    if (c.Find(_T("SYSLISTVIEW32")) >= 0) return TRUE;
    if (c.Find(_T("COMBOBOX")) >= 0)
        return !CCC_UseComboGlass(hWnd);
    if (c.Find(_T("EDIT")) >= 0) return !CCC_UseEditGlass(hWnd);
    if (c.Find(_T("SYSHEADER32")) >= 0) return TRUE;
    return FALSE;
}

static BOOL CCC_UsesAeroGlassPaint(HWND hWnd)
{
    return CCC_UseComboGlass(hWnd) || CCC_UseEditGlass(hWnd)
        || CCC_UseListBoxRowGlass(hWnd) || CCC_UseListCtrlRowGlass(hWnd);
}

static BOOL CCC_DialogHasVisibleChildren(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd)) return FALSE;
    for (HWND hChild = ::GetWindow(hWnd, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (::IsWindowVisible(hChild))
            return TRUE;
    }
    return FALSE;
}

// ガラス子: WM_PAINT 経由で同期再描画（RDW_ERASE なし）
static void CCC_RepaintGlassChildAlphaSync(HWND hChild)
{
    if (!hChild || !::IsWindow(hChild) || !::IsWindowVisible(hChild))
        return;
    if (CCC_UseBlurChildGlassPaint(hChild) || CCC_UseEditGlass(hChild) || CCC_UseComboGlass(hChild))
        ::RedrawWindow(hChild, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

static BOOL CCC_IsGlassFieldChild(HWND hChild)
{
    return CCC_UseBlurChildGlassPaint(hChild) || CCC_UseEditGlass(hChild) || CCC_UseComboGlass(hChild);
}

static void CCC_RepaintChildDirectPrint(HWND hChild)
{
    if (!hChild || !::IsWindow(hChild) || !::IsWindowVisible(hChild))
        return;
    CWnd* pw = CWnd::FromHandlePermanent(hChild);
    if (!pw) return;
    CClientDC dc(pw);
    ::SendMessage(hChild, WM_PRINTCLIENT, (WPARAM)dc.GetSafeHdc(), PRF_CLIENT);
}

static BOOL CCC_UseTransparentDirectPrint(HWND hChild)
{
    if (CWnd* pw = CWnd::FromHandlePermanent(hChild))
    {
        if (dynamic_cast<CCustomGroupBox*>(pw)) return TRUE;
        if (dynamic_cast<CCustomCheckBox*>(pw)) return TRUE;
        if (dynamic_cast<CCustomStatic*>(pw)) return TRUE;
        if (dynamic_cast<CCustomSliderCtrl*>(pw)) return TRUE;
        if (dynamic_cast<CCustomRangeSliderCtrl*>(pw)) return TRUE;
    }
    return FALSE;
}

static void CCC_RepaintGlassFieldChildDirect(HWND hChild)
{
    if (!CCC_IsGlassFieldChild(hChild))
        return;
    CCC_RepaintChildDirectPrint(hChild);
}

static void CCC_RepaintGlassFieldChildrenDirectRecursive(HWND hWnd)
{
    for (HWND hChild = ::GetWindow(hWnd, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (::IsWindowVisible(hChild))
            CCC_RepaintGlassFieldChildDirect(hChild);
        CCC_RepaintGlassFieldChildrenDirectRecursive(hChild);
    }
}

static void CCC_RefreshTransparentChildrenAlphaRecursive(HWND hWnd)
{
    for (HWND hChild = ::GetWindow(hWnd, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (::IsWindowVisible(hChild))
        {
            // ButtonST/Edit/Combo は別パスで直接描画（二重 UPDATENOW でちらつく）
            if (CCC_IsGlassFieldChild(hChild))
                ;
            else if (CCC_UseTransparentDirectPrint(hChild))
                CCC_RepaintChildDirectPrint(hChild);
            else if (CCC_UsesAeroGlassPaint(hChild) || CCC_IsTransparentBlurControl(hChild))
                ::RedrawWindow(hChild, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
            else if (CCC_ShouldOpaqueFix(hChild))
                ::SendMessage(hChild, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
        }
        CCC_RefreshTransparentChildrenAlphaRecursive(hChild);
    }
}

static void CCC_RepaintAeroChildAlphaSync(HWND hChild)
{
    if (!hChild || !::IsWindow(hChild) || !::IsWindowVisible(hChild))
        return;
    if (CCC_IsGlassFieldChild(hChild))
        CCC_RepaintGlassFieldChildDirect(hChild);
    else if (CCC_UseTransparentDirectPrint(hChild))
        CCC_RepaintChildDirectPrint(hChild);
    else if (CCC_UsesAeroGlassPaint(hChild) || CCC_IsTransparentBlurControl(hChild))
        ::RedrawWindow(hChild, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    else if (CCC_ShouldOpaqueFix(hChild))
        ::SendMessage(hChild, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
}

static void CCC_RefreshGlassChildrenAlphaRecursive(HWND hWnd)
{
    for (HWND hChild = ::GetWindow(hWnd, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (::IsWindowVisible(hChild))
            CCC_RepaintAeroChildAlphaSync(hChild);
        CCC_RefreshGlassChildrenAlphaRecursive(hChild);
    }
}

static void CCC_QueueGlassChildRepaint(HWND hChild, BOOL bSync)
{
    if (!hChild || !::IsWindow(hChild)) return;
    const UINT rdwFlags = RDW_INVALIDATE | RDW_NOERASE | (bSync ? RDW_UPDATENOW : 0);
    if (CCC_UseBlurChildGlassPaint(hChild) || CCC_UseEditGlass(hChild) || CCC_UseComboGlass(hChild))
    {
        if (bSync)
            CCC_RepaintGlassHwnd(hChild);
        else
            ::RedrawWindow(hChild, NULL, NULL, rdwFlags);
        return;
    }
    if (CCC_UsesAeroGlassPaint(hChild) || CCC_IsTransparentBlurControl(hChild))
        ::RedrawWindow(hChild, NULL, NULL, rdwFlags);
}

static void CCC_InvalidateGlassChildrenRecursive(HWND hWnd, BOOL bSyncGlassRepaint, BOOL bSyncChildren)
{
    for (HWND hChild = ::GetWindow(hWnd, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (CCC_UseBlurChildGlassPaint(hChild) || CCC_UseEditGlass(hChild) || CCC_UseComboGlass(hChild))
        {
            if (bSyncGlassRepaint)
                CCC_RepaintGlassHwnd(hChild);
            else
                CCC_QueueGlassChildRepaint(hChild, bSyncChildren);
        }
        else if (::IsWindowVisible(hChild))
        {
            if (CCC_UsesAeroGlassPaint(hChild) || CCC_IsTransparentBlurControl(hChild))
                CCC_QueueGlassChildRepaint(hChild, bSyncChildren);
            else if (CCC_ShouldOpaqueFix(hChild))
            {
                if (bSyncChildren)
                    ::SendMessage(hChild, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
                else
                    ::PostMessage(hChild, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
            }
        }
        CCC_InvalidateGlassChildrenRecursive(hChild, bSyncGlassRepaint, bSyncChildren);
    }
}

void CCC_RefreshAeroGlassChildren(HWND hWnd)
{
    CCC_RefreshGlassChildrenAlphaRecursive(hWnd);
}

void CCC_RefreshAeroGlassAlphaDeferred(HWND hWnd, const RECT* pGapPreserveRect)
{
    if (!hWnd || !::IsWindow(hWnd) || !CCC_IsAeroEnabled() || !CCC_IsWin11())
        return;
    CCC_RepaintDialogAeroGaps(hWnd, pGapPreserveRect);
    CCC_InvalidateGlassChildrenRecursive(hWnd, FALSE, FALSE);
}

void CCC_RefreshAeroGlassAlphaOnDialog(HWND hWnd, const RECT* pGapPreserveRect, BOOL bSyncChildren)
{
    if (!hWnd || !::IsWindow(hWnd) || !CCC_IsAeroEnabled() || !CCC_IsWin11())
        return;
    // 隙間を先に更新（子 HWND は clip 除外）。子を後から描く。逆順だと隙間描画が子を消す。
    if (CCC_DialogHasVisibleChildren(hWnd))
        CCC_RepaintDialogAeroGaps(hWnd, pGapPreserveRect);
    else
        ::RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
    // グループボックス等の親背景 → 子の ButtonST/Edit（Invalidate なし直接描画でちらつき防止）
    CCC_RefreshTransparentChildrenAlphaRecursive(hWnd);
    CCC_RepaintGlassFieldChildrenDirectRecursive(hWnd);
    if (bSyncChildren)
        CCC_RepaintGlassFieldChildrenDirectRecursive(hWnd);
}

static void CCC_InvalidateGlassControlsOnDialog(HWND hDlg);

static std::map<HWND, DWORD> g_lastAeroGlassAlphaRefreshTick;

// aero_blur_Acrylic_Opacity: Win10=LWA_ALPHA / Win11=隙間+子コントロールガラス+リスト／コンボ
void CCC_RefreshAeroGlassAlphaForHwnd(HWND hWnd, const RECT* pGapPreserveRect, BOOL bImmediate)
{
    if (!hWnd || !::IsWindow(hWnd) || !CCC_IsAeroEnabled()) return;

    if (CCC_IsWin11())
    {
        if (bImmediate)
            g_lastAeroGlassAlphaRefreshTick.erase(hWnd);
        else
        {
            const DWORD now = ::GetTickCount();
            DWORD& lastTick = g_lastAeroGlassAlphaRefreshTick[hWnd];
            if (lastTick != 0 && (now - lastTick) < 16)
                return;
            lastTick = now;
        }
        // HSCROLL 中は PostMessage だとマウスキャプチャ下で処理されずリアルタイム性が失われるため同期呼び出し。
        CCC_RefreshAeroGlassAlphaOnDialog(hWnd, pGapPreserveRect, bImmediate);
        return;
    }
    else
    {
        CCC_RefreshAeroWindowLayer(hWnd);
        ::InvalidateRect(hWnd, NULL, FALSE);
    }
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
        if (CCC_UseEditGlass(hWnd))
        {
            pEdit->PaintGlassClient(dc, TRUE);
            dc.Detach();
            return TRUE;
        }
#endif
        CBrush br(COLOR_EDIT_BG);
        dc.FillRect(&r, &br);
        pEdit->DrawClientText(dc, r);
        dc.Detach();
        return TRUE;
    }
    else if (auto* pCombo = dynamic_cast<CCustomComboBox*>(pw))
    {
#if CCUSTOM_AERO_SUPPORT
        if (CCC_UseComboGlass(hWnd))
        {
            pCombo->PaintGlassClient(dc, TRUE);
            dc.Detach();
            return TRUE;
        }
#endif
        dc.Detach();
        return FALSE;
    }
    else if (auto* pList = dynamic_cast<CCustomListCtrl*>(pw))
    {
        pList->PaintOpaqueIntoBuffer(hdcBuf);
        dc.Detach();
        return TRUE;
    }
    else if (auto* pLB = dynamic_cast<CCustomListBox*>(pw))
    {
        pLB->PaintOpaqueIntoBuffer(hdcBuf);
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

static void CCC_InstallOpaqueFixersRecursive(HWND hParent, CTypedPtrList<CPtrList, CCustomOpaqueFixer*>& fixers)
{
    for (HWND hChild = ::GetWindow(hParent, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (CCC_ShouldOpaqueFix(hChild))
        {
            CCustomOpaqueFixer* pFixer = new CCustomOpaqueFixer(CCC_OpaqueBgForHwnd(hChild));
            if (pFixer->Install(hChild))
                fixers.AddTail(pFixer);
            else
                delete pFixer;
        }
        CCC_InstallOpaqueFixersRecursive(hChild, fixers);
    }
}

static void CCC_InvalidateAllButtonSTOnDialog(HWND hDlg)
{
    if (!hDlg || !::IsWindow(hDlg)) return;
    for (HWND hChild = ::GetWindow(hDlg, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (CWnd* pw = CWnd::FromHandlePermanent(hChild))
        {
            if (dynamic_cast<CButtonST*>(pw))
                ::InvalidateRect(hChild, NULL, FALSE);
        }
        CCC_InvalidateAllButtonSTOnDialog(hChild);
    }
}

static void CCC_ForcePaintAllButtonSTOnDialog(HWND hDlg)
{
    if (!hDlg || !::IsWindow(hDlg)) return;
    for (HWND hChild = ::GetWindow(hDlg, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (CWnd* pw = CWnd::FromHandlePermanent(hChild))
        {
            if (CButtonST* pBtn = dynamic_cast<CButtonST*>(pw))
            {
                if (CCC_UseBlurChildGlassPaint(hChild))
                    CCC_ForcePaintButtonST(hChild, pBtn);
            }
        }
        CCC_ForcePaintAllButtonSTOnDialog(hChild);
    }
}

static void CCC_InvalidateGlassControlsOnDialog(HWND hDlg)
{
    if (!hDlg || !::IsWindow(hDlg)) return;
    for (HWND hChild = ::GetWindow(hDlg, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (CCC_UseComboGlass(hChild) || CCC_UseEditGlass(hChild))
            ::InvalidateRect(hChild, NULL, FALSE);
        CCC_InvalidateGlassControlsOnDialog(hChild);
    }
}

static void CCC_FinalizeBlurDialog(CWnd* pDlg, BOOL bAero, BOOL& bBlurApplied,
    CTypedPtrList<CPtrList, CCustomOpaqueFixer*>& fixers)
{
    if (!pDlg || !pDlg->GetSafeHwnd()) return;

    if (!bAero)
    {
        CCC_ClearOpaqueFixerList(fixers);
        CCC_ClearButtonSTGlassFixersForDialog(pDlg->m_hWnd);
        CCC_ClearEditComboGlassFixersForDialog(pDlg->m_hWnd);
        bBlurApplied = FALSE;
        return;
    }
    if (bBlurApplied)
    {
        // イコライザー等の追加で RefreshAeroMode が呼ばれたとき fixer を外すと ButtonST が消える
        CCC_RefreshAeroGlassAlphaForHwnd(pDlg->m_hWnd, nullptr);
        return;
    }

    CCC_ClearOpaqueFixerList(fixers);
    CCC_ClearButtonSTGlassFixersForDialog(pDlg->m_hWnd);
    CCC_ClearEditComboGlassFixersForDialog(pDlg->m_hWnd);

    bBlurApplied = CCC_ApplyAero(pDlg->m_hWnd, TRUE) != FALSE;
    CCC_PrepareDialogSurface(pDlg->m_hWnd, TRUE);
    PROPAGATE_AERO_TO_CHILDREN(pDlg->m_hWnd, TRUE);
    pDlg->ModifyStyle(0, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

    if (CCC_IsWin11())
    {
        CCC_InstallOpaqueFixersRecursive(pDlg->m_hWnd, fixers);
        CCC_InstallButtonSTGlassFixersRecursive(pDlg->m_hWnd);
        CCC_InstallEditComboGlassFixersRecursive(pDlg->m_hWnd);
    }

    pDlg->SetWindowPos(NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_DRAWFRAME);
    pDlg->Invalidate(FALSE);
    if (CCC_IsWin11())
        pDlg->PostMessage(CCC_MSG_REFRESH_CHILDREN, 0, 0);
}

static void CCC_PostOpaqueRepaintRecursive(HWND hWnd)
{
    for (HWND hChild = ::GetWindow(hWnd, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (CCC_ShouldOpaqueFix(hChild))
            ::PostMessage(hChild, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
        CCC_PostOpaqueRepaintRecursive(hChild);
    }
}

static void CCC_ReapplyOpaqueFixers(CWnd* pDlg, CTypedPtrList<CPtrList, CCustomOpaqueFixer*>& fixers)
{
    UNREFERENCED_PARAMETER(fixers);
    if (!pDlg || !pDlg->GetSafeHwnd() || !CCC_IsWin11() || !CCC_IsAeroEnabled()) return;
    CCC_RepaintDialogAeroGaps(pDlg->m_hWnd, nullptr);
    CCC_RefreshChildrenAfterShow(pDlg->m_hWnd);
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
        if (CCC_UseBlurChildGlassPaint(hWnd))
        {
            CCC_RepaintGlassChildAlphaSync(hWnd);
            return;
        }
        CCC_ForcePaintButtonST(hWnd, pBtn);
        return;
    }
    if (auto* pCb = dynamic_cast<CCustomCheckBox*>(pw))
    {
        pCb->RepaintClient();
        return;
    }

    if (auto* pEdit = dynamic_cast<CCustomEdit*>(pw))
    {
        if (CCC_UseEditGlass(hWnd))
        {
            CCC_RepaintGlassChildAlphaSync(hWnd);
            return;
        }
        pEdit->RepaintClient();
        return;
    }
    if (auto* pCombo = dynamic_cast<CCustomComboBox*>(pw))
    {
        if (CCC_UseComboGlass(hWnd))
        {
            CCC_RepaintGlassChildAlphaSync(hWnd);
            return;
        }
    }

#if CCUSTOM_AERO_SUPPORT
    if (CCC_IsAeroEnabled() && CCC_IsWin11())
    {
        // RDW_ERASE は Win11 DWM 上で alpha=0 の消去→再描画遅延により点滅／消失の原因になる
        if (CCC_UsesAeroGlassPaint(hWnd) || CCC_IsTransparentBlurControl(hWnd))
        {
            ::InvalidateRect(hWnd, NULL, FALSE);
            return;
        }
        if (CCC_ShouldOpaqueFix(hWnd))
        {
            ::RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
            return;
        }
    }
#endif
    ::InvalidateRect(hWnd, NULL, FALSE);
    ::UpdateWindow(hWnd);
}

static BOOL CALLBACK CCC_RefreshChildProc(HWND hChild, LPARAM)
{
    CCC_ForceRepaintHwnd(hChild);
    return TRUE;
}

void CCC_RefreshChildrenAfterShow(HWND hWnd)
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

void CCC_RefreshChildrenAfterShow(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd))
        return;
    ::EnumChildWindows(hWnd, CCC_RefreshChildProcNoAero, 0);
}
#endif

// ============================================================================
// アクリルぼかし適用済みカスタムダイアログ (CDialog版)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomBlurDialogBase, CCustomDialog)

BEGIN_MESSAGE_MAP(CCustomBlurDialogBase, CCustomDialog)
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_SHOWWINDOW()
    ON_WM_WINDOWPOSCHANGED()
    ON_WM_DWMCOMPOSITIONCHANGED()
    ON_WM_DESTROY()
    ON_WM_DRAWITEM()
#if CCUSTOM_AERO_SUPPORT
    ON_MESSAGE(CCC_MSG_REAPPLY_OPAQUE_FIXERS, OnReapplyOpaqueFixers)
    ON_MESSAGE(CCC_MSG_REFRESH_CHILDREN, OnRefreshChildren)
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

void CCustomBlurDialogBase::ApplyDwmBlur()
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
#if CCUSTOM_AERO_SUPPORT
    m_bAeroEnabled = CCC_IsAeroEnabled();
    if (!m_bAeroEnabled)
    {
        CCC_ClearOpaqueFixerList(m_opaqueFixers);
        CCC_ClearButtonSTGlassFixersForDialog(m_hWnd);
        CCC_ClearEditComboGlassFixersForDialog(m_hWnd);
        CCC_ApplyAero(m_hWnd, FALSE);
        CCC_PrepareDialogSurface(m_hWnd, FALSE);
        PROPAGATE_AERO_TO_CHILDREN(m_hWnd, FALSE);
        m_bBlurApplied = FALSE;
        Invalidate();
        return;
    }
    CCC_FinalizeBlurDialog(this, TRUE, m_bBlurApplied, m_opaqueFixers);
#else
    m_bBlurApplied = FALSE;
#endif
}

void CCustomBlurDialogBase::RefreshAeroGlassAlpha(BOOL bImmediate)
{
#if CCUSTOM_AERO_SUPPORT
    if (!GetSafeHwnd() || !::IsWindow(m_hWnd) || !::IsWindowVisible(m_hWnd)) return;
    CCC_RefreshAeroGlassAlphaForHwnd(m_hWnd, nullptr, bImmediate);
#endif
}

void CCustomBlurDialogBase::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CCustomDialog::OnShowWindow(bShow, nStatus);
    if (!bShow)
        return;
#if CCUSTOM_AERO_SUPPORT
    if (m_bBlurApplied && CCC_IsAeroEnabled() && CCC_IsWin11())
    {
        CCC_RefreshDialogDwmBlur(m_hWnd);
        CCC_RepaintDialogAeroGaps(m_hWnd, nullptr);
        CCC_RefreshChildrenAfterShow(m_hWnd);
    }
#endif
    UNREFERENCED_PARAMETER(nStatus);
}

void CCustomBlurDialogBase::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
#if CCUSTOM_AERO_SUPPORT
    if (lpDrawItemStruct && lpDrawItemStruct->hDC && CCC_UseBlurChildGlassPaint(lpDrawItemStruct->hwndItem))
    {
        if (CWnd* pw = CWnd::FromHandlePermanent(lpDrawItemStruct->hwndItem))
        {
            if (CButtonST* pBtn = dynamic_cast<CButtonST*>(pw))
            {
                pBtn->SetAeroGlassMode(TRUE);
                CCC_PaintButtonSTGlass(lpDrawItemStruct->hwndItem, pBtn, lpDrawItemStruct->hDC);
                return;
            }
        }
    }
#else
    UNREFERENCED_PARAMETER(nIDCtl);
    UNREFERENCED_PARAMETER(lpDrawItemStruct);
#endif
    CCustomDialog::OnDrawItem(nIDCtl, lpDrawItemStruct);
}

BOOL CCustomBlurDialogBase::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
    if (m_bAeroEnabled && CCC_IsWin11() && pDC)
    {
        CCC_PaintDialogAeroGaps(*pDC, this, nullptr);
        return TRUE;
    }
#endif
    return CCustomDialog::OnEraseBkgnd(pDC);
}

void CCustomBlurDialogBase::OnPaint()
{
#if CCUSTOM_AERO_SUPPORT
    if (m_bAeroEnabled && CCC_IsWin11())
    {
        CPaintDC dc(this);
        CCC_PaintDialogAeroGaps(dc, this, nullptr);
        return;
    }
#endif
    CCustomDialog::OnPaint();
}

void CCustomBlurDialogBase::OnDestroy()
{
#if CCUSTOM_AERO_SUPPORT
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
    CCC_ClearButtonSTGlassFixersForDialog(m_hWnd);
    CCC_ClearEditComboGlassFixersForDialog(m_hWnd);
    CCC_RestoreParentBlurGlassFixers(this);
#endif
    CCustomDialog::OnDestroy();
}

void CCustomBlurDialogBase::OnSize(UINT nType, int cx, int cy)
{
    CCustomDialog::OnSize(nType, cx, cy);
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
    ApplyDwmBlur();
#endif
}

LRESULT CCustomBlurDialogBase::OnReapplyOpaqueFixers(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
    if (m_bAeroEnabled && CCC_IsWin11())
        CCC_ReapplyOpaqueFixers(this, m_opaqueFixers);
#endif
    return 0;
}

LRESULT CCustomBlurDialogBase::OnRefreshChildren(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
    if (m_bBlurApplied && m_bAeroEnabled)
        CCC_RefreshAeroGlassAlphaDeferred(m_hWnd, nullptr);
#endif
    return 0;
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
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_SHOWWINDOW()
    ON_WM_WINDOWPOSCHANGED()
    ON_WM_DWMCOMPOSITIONCHANGED()
    ON_WM_DESTROY()
#if CCUSTOM_AERO_SUPPORT
    ON_MESSAGE(CCC_MSG_REAPPLY_OPAQUE_FIXERS, OnReapplyOpaqueFixers)
    ON_MESSAGE(CCC_MSG_REFRESH_CHILDREN, OnRefreshChildren)
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

void CCustomBlurDialogExBase::ApplyDwmBlur()
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
#if CCUSTOM_AERO_SUPPORT
    m_bAeroEnabled = CCC_IsAeroEnabled();
    if (!m_bAeroEnabled)
    {
        CCC_ClearOpaqueFixerList(m_opaqueFixers);
        CCC_ClearButtonSTGlassFixersForDialog(m_hWnd);
        CCC_ClearEditComboGlassFixersForDialog(m_hWnd);
        CCC_ApplyAero(m_hWnd, FALSE);
        CCC_PrepareDialogSurface(m_hWnd, FALSE);
        PROPAGATE_AERO_TO_CHILDREN(m_hWnd, FALSE);
        m_bBlurApplied = FALSE;
        Invalidate();
        return;
    }
    CCC_FinalizeBlurDialog(this, TRUE, m_bBlurApplied, m_opaqueFixers);
#else
    m_bBlurApplied = FALSE;
#endif
}

void CCustomBlurDialogExBase::RefreshAeroGlassAlpha(BOOL bImmediate)
{
#if CCUSTOM_AERO_SUPPORT
    if (!GetSafeHwnd() || !::IsWindow(m_hWnd) || !::IsWindowVisible(m_hWnd)) return;
    CCC_RefreshAeroGlassAlphaForHwnd(m_hWnd, nullptr, bImmediate);
#endif
}

void CCustomBlurDialogExBase::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CCustomDialogEx::OnShowWindow(bShow, nStatus);
    if (!bShow)
        return;
#if CCUSTOM_AERO_SUPPORT
    if (m_bBlurApplied && CCC_IsAeroEnabled() && CCC_IsWin11())
    {
        CCC_RefreshDialogDwmBlur(m_hWnd);
        CCC_RepaintDialogAeroGaps(m_hWnd, nullptr);
        CCC_RefreshChildrenAfterShow(m_hWnd);
    }
#endif
    UNREFERENCED_PARAMETER(nStatus);
}

BOOL CCustomBlurDialogExBase::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
    if (m_bAeroEnabled && CCC_IsWin11() && pDC)
    {
        CCC_PaintDialogAeroGaps(*pDC, this, nullptr);
        return TRUE;
    }
#endif
    return CCustomDialogEx::OnEraseBkgnd(pDC);
}

void CCustomBlurDialogExBase::OnPaint()
{
#if CCUSTOM_AERO_SUPPORT
    if (m_bAeroEnabled && CCC_IsWin11())
    {
        CPaintDC dc(this);
        CCC_PaintDialogAeroGaps(dc, this, nullptr);
        return;
    }
#endif
    CCustomDialogEx::OnPaint();
}

void CCustomBlurDialogExBase::OnDestroy()
{
#if CCUSTOM_AERO_SUPPORT
    CCC_ClearOpaqueFixerList(m_opaqueFixers);
    CCC_ClearButtonSTGlassFixersForDialog(m_hWnd);
    CCC_ClearEditComboGlassFixersForDialog(m_hWnd);
    CCC_RestoreParentBlurGlassFixers(this);
#endif
    CCustomDialogEx::OnDestroy();
}

void CCustomBlurDialogExBase::OnSize(UINT nType, int cx, int cy)
{
    CCustomDialogEx::OnSize(nType, cx, cy);
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
    ApplyDwmBlur();
#endif
}

LRESULT CCustomBlurDialogExBase::OnReapplyOpaqueFixers(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
    if (m_bAeroEnabled && CCC_IsWin11())
        CCC_ReapplyOpaqueFixers(this, m_opaqueFixers);
#endif
    return 0;
}

LRESULT CCustomBlurDialogExBase::OnRefreshChildren(WPARAM, LPARAM)
{
#if CCUSTOM_AERO_SUPPORT
    if (m_bBlurApplied && m_bAeroEnabled)
        CCC_RefreshAeroGlassAlphaDeferred(m_hWnd, nullptr);
#endif
    return 0;
}
