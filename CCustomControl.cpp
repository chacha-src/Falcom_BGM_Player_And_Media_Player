#include "stdafx.h"
#include "CCustomControl.h"
#include <cmath>

#ifdef SubclassWindow
#undef SubclassWindow
#endif

// ============================================================================
// 共通ヘルパー関数
// ============================================================================

/**
 * @brief グラデーション背景を描画する（GradientFill APIを使用・最適化版）
 * @param pDC デバイスコンテキスト
 * @param rect 描画領域
 * @param colorStart 開始色
 * @param colorEnd 終了色
 * @param nDirection 方向（0-359度、0=下から上、90=左から右）
 */
static void DrawGradientBackground(CDC* pDC, const CRect& rect, COLORREF colorStart, COLORREF colorEnd, int nDirection)
{
    // [FIX❸] GradientFill APIを使用して、毎フレーム256回のCPen生成を排除
    int normalizedDir = nDirection % 360;
    if (normalizedDir < 0) normalizedDir += 360;

    // 水平(90-180度)または垂直(それ以外)のどちらかに正規化
    BOOL bHorizontal = (normalizedDir >= 45 && normalizedDir < 135) ||
        (normalizedDir >= 225 && normalizedDir < 315);
    BOOL bReverse = (normalizedDir >= 135 && normalizedDir < 315);

    COLORREF clrA = bReverse ? colorEnd : colorStart;
    COLORREF clrB = bReverse ? colorStart : colorEnd;

    TRIVERTEX vtx[2];
    vtx[0].x = rect.left;
    vtx[0].y = rect.top;
    vtx[0].Red = (COLOR16)(GetRValue(clrA) << 8);
    vtx[0].Green = (COLOR16)(GetGValue(clrA) << 8);
    vtx[0].Blue = (COLOR16)(GetBValue(clrA) << 8);
    vtx[0].Alpha = 0;

    vtx[1].x = rect.right;
    vtx[1].y = rect.bottom;
    vtx[1].Red = (COLOR16)(GetRValue(clrB) << 8);
    vtx[1].Green = (COLOR16)(GetGValue(clrB) << 8);
    vtx[1].Blue = (COLOR16)(GetBValue(clrB) << 8);
    vtx[1].Alpha = 0;

    GRADIENT_RECT gr = { 0, 1 };
    ::GradientFill(pDC->GetSafeHdc(), vtx, 2, &gr, 1,
        bHorizontal ? GRADIENT_FILL_RECT_H : GRADIENT_FILL_RECT_V);
}

/**
 * @brief ドロップシャドウ付きテキストを描画する（グローバル関数）
 */
static void DrawTextWithShadow(CDC* pDC, const CRect& rect, const CString& strText, UINT nFormat,
    COLORREF clrText, COLORREF clrShadow, int nShadowDirection, int nShadowDistance,
    int nShadowBlur, BOOL bShadowEnable, COLORREF clrBackground)
{
    if (bShadowEnable && nShadowDistance > 0)
    {
        double rad = nShadowDirection * 3.14159265358979323846 / 180.0;
        int offsetX = (int)(nShadowDistance * cos(rad));
        int offsetY = (int)(nShadowDistance * sin(rad));

        for (int blur = nShadowBlur; blur > 0; blur--)
        {
            int alpha = 255 / (nShadowBlur + 1) * (nShadowBlur - blur + 1) / nShadowBlur;
            int r = (GetRValue(clrShadow) * alpha + GetRValue(clrBackground) * (255 - alpha)) / 255;
            int g = (GetGValue(clrShadow) * alpha + GetGValue(clrBackground) * (255 - alpha)) / 255;
            int b = (GetBValue(clrShadow) * alpha + GetBValue(clrBackground) * (255 - alpha)) / 255;

            pDC->SetTextColor(RGB(r, g, b));
            int blurOffset = blur - nShadowBlur / 2;
            CRect rcShadow = rect;
            rcShadow.OffsetRect(offsetX + blurOffset, offsetY + blurOffset);
            pDC->DrawText(strText, rcShadow, nFormat);
        }
    }

    pDC->SetTextColor(clrText);
    CRect rcText = rect;
    pDC->DrawText(strText, rcText, nFormat);
}

/**
 * @brief グラデーション付きテキストを描画する（最適化版）
 * [FIX❷] 1ピクセルごとのDrawTextループをバンド幅4pxにまとめてDrawText呼び出し回数を大幅削減。
 *        さらに水平グラデーションは中間ビットマップで1回のDrawTextで済む手法を採用。
 */
static void DrawTextWithGradient(CDC* pDC, const CRect& rect, const CString& strText, UINT nFormat,
    COLORREF clrStart, COLORREF clrEnd, int nDirection,
    COLORREF clrShadow, int nShadowDirection, int nShadowDistance, int nShadowBlur, BOOL bShadowEnable, COLORREF clrBackground,
    int nActualTextWidth = -1)
{
    if (strText.IsEmpty()) return;

    // 1. ドロップシャドウ描画
    if (bShadowEnable && nShadowDistance > 0)
    {
        double rad = nShadowDirection * 3.14159265358979323846 / 180.0;
        int offsetX = (int)(nShadowDistance * cos(rad));
        int offsetY = (int)(nShadowDistance * sin(rad));

        for (int blur = nShadowBlur; blur > 0; blur--)
        {
            int alpha = 255 / (nShadowBlur + 1) * (nShadowBlur - blur + 1) / nShadowBlur;
            int r = (GetRValue(clrShadow) * alpha + GetRValue(clrBackground) * (255 - alpha)) / 255;
            int g = (GetGValue(clrShadow) * alpha + GetGValue(clrBackground) * (255 - alpha)) / 255;
            int b = (GetBValue(clrShadow) * alpha + GetBValue(clrBackground) * (255 - alpha)) / 255;

            pDC->SetTextColor(RGB(r, g, b));
            CRect rcShadow = rect;
            rcShadow.OffsetRect(offsetX + (blur - nShadowBlur / 2), offsetY + (blur - nShadowBlur / 2));
            pDC->DrawText(strText, rcShadow, nFormat);
        }
    }

    // 2. テキストサイズ測定
    CSize szText = pDC->GetTextExtent(strText);
    int nWidth = (nActualTextWidth > 0) ? nActualTextWidth : szText.cx;
    int nHeight = szText.cy;

    LOGFONT lf;
    pDC->GetCurrentFont()->GetLogFont(&lf);
    int italicMargin = lf.lfItalic ? (abs(lf.lfHeight) / 2) : 0;

    CRect rcGradArea = rect;
    if (nFormat & DT_CENTER)
        rcGradArea.left = rect.left + (rect.Width() - nWidth) / 2;
    else if (nFormat & DT_RIGHT)
        rcGradArea.left = rect.right - nWidth;
    rcGradArea.right = rcGradArea.left + nWidth + italicMargin;

    int normalizedDir = nDirection % 360;
    if (normalizedDir < 0) normalizedDir += 360;

    pDC->SetBkMode(TRANSPARENT);

    // [FIX❷] バンド幅定数（1→4でDrawText呼び出し回数を約1/4に削減）
    const int kBandWidth = 4;

    // 対角線グラデーション（45, 135, 225, 315度）
    if (normalizedDir == 45 || normalizedDir == 135 || normalizedDir == 225 || normalizedDir == 315)
    {
        int diagonal = (int)sqrt((double)(rcGradArea.Width() * rcGradArea.Width() + nHeight * nHeight));
        if (diagonal <= 0) diagonal = 1;

        for (int i = 0; i < diagonal; i += kBandWidth)
        {
            double ratio = (double)i / diagonal;
            int r = GetRValue(clrStart) + (int)((GetRValue(clrEnd) - GetRValue(clrStart)) * ratio);
            int g = GetGValue(clrStart) + (int)((GetGValue(clrEnd) - GetGValue(clrStart)) * ratio);
            int b = GetBValue(clrStart) + (int)((GetBValue(clrEnd) - GetBValue(clrStart)) * ratio);
            pDC->SetTextColor(RGB(r, g, b));

            double diagRatio = (double)i / (double)(diagonal - 1);
            int x = rcGradArea.left + (int)(rcGradArea.Width() * diagRatio);

            CRect rcSlice = rect;
            rcSlice.left = x;
            rcSlice.right = min(x + kBandWidth, rect.right);

            CRgn rgn;
            rgn.CreateRectRgnIndirect(&rcSlice);
            pDC->SelectClipRgn(&rgn);

            CRect rcDraw = rect;
            rcDraw.right += italicMargin;
            pDC->DrawText(strText, rcDraw, nFormat);

            pDC->SelectClipRgn(NULL);
        }
    }
    // 水平グラデーション
    else if ((normalizedDir >= 90 && normalizedDir < 180) || (normalizedDir >= 270 && normalizedDir < 360))
    {
        int totalSteps = rcGradArea.Width();
        if (totalSteps <= 0) totalSteps = 1;
        BOOL bLeftToRight = (normalizedDir >= 90 && normalizedDir < 180);

        for (int i = 0; i < totalSteps; i += kBandWidth)
        {
            double ratio = bLeftToRight ? ((double)i / totalSteps) : (1.0 - (double)i / totalSteps);
            int r = GetRValue(clrStart) + (int)((GetRValue(clrEnd) - GetRValue(clrStart)) * ratio);
            int g = GetGValue(clrStart) + (int)((GetGValue(clrEnd) - GetGValue(clrStart)) * ratio);
            int b = GetBValue(clrStart) + (int)((GetBValue(clrEnd) - GetBValue(clrStart)) * ratio);
            pDC->SetTextColor(RGB(r, g, b));

            CRect rcSlice = rect;
            rcSlice.left = rcGradArea.left + i;
            rcSlice.right = min(rcSlice.left + kBandWidth, rcGradArea.right);

            CRgn rgn;
            rgn.CreateRectRgnIndirect(&rcSlice);
            pDC->SelectClipRgn(&rgn);

            CRect rcDraw = rect;
            rcDraw.right += italicMargin;
            pDC->DrawText(strText, rcDraw, nFormat);

            pDC->SelectClipRgn(NULL);
        }
    }
    // 垂直グラデーション
    else
    {
        int totalSteps = nHeight;
        if (totalSteps <= 0) totalSteps = 1;
        BOOL bBottomToTop = (normalizedDir >= 0 && normalizedDir < 90);

        for (int i = 0; i < totalSteps; i += kBandWidth)
        {
            double ratio = bBottomToTop ? ((double)i / totalSteps) : (1.0 - (double)i / totalSteps);
            int r = GetRValue(clrStart) + (int)((GetRValue(clrEnd) - GetRValue(clrStart)) * ratio);
            int g = GetGValue(clrStart) + (int)((GetGValue(clrEnd) - GetGValue(clrStart)) * ratio);
            int b = GetBValue(clrStart) + (int)((GetBValue(clrEnd) - GetBValue(clrStart)) * ratio);
            pDC->SetTextColor(RGB(r, g, b));

            CRect rcSlice = rect;
            rcSlice.top = bBottomToTop ? (rect.bottom - i - kBandWidth) : (rect.top + i);
            rcSlice.bottom = rcSlice.top + kBandWidth;

            CRgn rgn;
            rgn.CreateRectRgnIndirect(&rcSlice);
            pDC->SelectClipRgn(&rgn);

            CRect rcDraw = rect;
            rcDraw.right += italicMargin;
            pDC->DrawText(strText, rcDraw, nFormat);

            pDC->SelectClipRgn(NULL);
        }
    }
}

// -------- 以下の描画ヘルパーは変更なし --------

static void DrawHeart(CDC* pDC, CRect rc, COLORREF color)
{
    CBrush br(color);
    CPen pen(PS_SOLID, 1, color);
    CBrush* pOldBr = pDC->SelectObject(&br);
    CPen* pOldPen = pDC->SelectObject(&pen);

    int cx = rc.CenterPoint().x;
    int cy = rc.CenterPoint().y;
    int w = rc.Width() / 2;
    if (w < 2) w = 2;

    POINT pts[8];
    pts[0] = { cx,     cy + w };
    pts[1] = { cx - w, cy - w / 3 };
    pts[2] = { cx - w, cy - w };
    pts[3] = { cx,     cy - w / 2 };
    pts[4] = { cx,     cy - w / 2 };
    pts[5] = { cx + w, cy - w };
    pts[6] = { cx + w, cy - w / 3 };
    pts[7] = { cx,     cy + w };
    pDC->Polygon(pts, 8);

    pDC->SelectObject(pOldBr);
    pDC->SelectObject(pOldPen);
}

static void DrawStar(CDC* pDC, int cx, int cy, int size, COLORREF color)
{
    CPen pen(PS_SOLID, 2, color);
    CPen* pOldPen = pDC->SelectObject(&pen);

    pDC->MoveTo(cx, cy - size);        pDC->LineTo(cx, cy + size);
    pDC->MoveTo(cx - size, cy);        pDC->LineTo(cx + size, cy);
    pDC->MoveTo(cx - size * 7 / 10, cy - size * 7 / 10); pDC->LineTo(cx + size * 7 / 10, cy + size * 7 / 10);
    pDC->MoveTo(cx + size * 7 / 10, cy - size * 7 / 10); pDC->LineTo(cx - size * 7 / 10, cy + size * 7 / 10);

    pDC->SelectObject(pOldPen);
}

static void DrawMusicNote(CDC* pDC, CRect rc, COLORREF color)
{
    CBrush br(color);
    CPen pen(PS_SOLID, 2, color);
    CBrush* pOldBr = pDC->SelectObject(&br);
    CPen* pOldPen = pDC->SelectObject(&pen);

    int cx = rc.CenterPoint().x;
    int cy = rc.CenterPoint().y;
    int h = rc.Height() * 6 / 10;
    int w = rc.Width() / 3;

    CRect rcNote(cx - w / 2, cy + h / 4, cx + w / 2, cy + h / 4 + w);
    pDC->Ellipse(&rcNote);
    pDC->MoveTo(cx + w / 2, cy + h / 4 + w / 2);
    pDC->LineTo(cx + w / 2, cy - h / 2);

    CPoint pts[4];
    pts[0] = CPoint(cx + w / 2, cy - h / 2);
    pts[1] = CPoint(cx + w / 2 + w, cy - h / 4);
    pts[2] = CPoint(cx + w / 2 + w, cy);
    pts[3] = CPoint(cx + w / 2, cy + h / 8);
    pDC->SelectObject(GetStockObject(NULL_PEN));
    pDC->Polygon(pts, 4);

    pDC->SelectObject(pOldBr);
    pDC->SelectObject(pOldPen);
}

static void DrawDiamond(CDC* pDC, CRect rc, COLORREF color)
{
    int cx = rc.CenterPoint().x;
    int cy = rc.CenterPoint().y;
    int w = rc.Width() / 2;
    int h = rc.Height() / 2;

    CPoint pts[4];
    pts[0] = CPoint(cx, cy - h);
    pts[1] = CPoint(cx + w, cy);
    pts[2] = CPoint(cx, cy + h);
    pts[3] = CPoint(cx - w, cy);

    CBrush brOuter(RGB(200, 200, 255));
    CPen   penOuter(PS_SOLID, 1, RGB(150, 150, 255));
    CBrush* pOldBr = pDC->SelectObject(&brOuter);
    CPen* pOldPen = pDC->SelectObject(&penOuter);
    pDC->Polygon(pts, 4);

    CPoint ptsInner[4];
    ptsInner[0] = CPoint(cx, cy - h * 6 / 10);
    ptsInner[1] = CPoint(cx + w * 6 / 10, cy);
    ptsInner[2] = CPoint(cx, cy + h * 6 / 10);
    ptsInner[3] = CPoint(cx - w * 6 / 10, cy);

    CBrush brInner(color);
    pDC->SelectObject(&brInner);
    pDC->Polygon(ptsInner, 4);

    CBrush brHighlight(RGB(255, 255, 255));
    pDC->SelectObject(&brHighlight);
    CRect rcHL(cx - 2, cy - 3, cx + 2, cy + 1);
    pDC->Ellipse(&rcHL);

    pDC->SelectObject(pOldBr);
    pDC->SelectObject(pOldPen);

    CPen penLight(PS_SOLID, 1, RGB(255, 255, 200));
    pDC->SelectObject(&penLight);
    pDC->MoveTo(cx, cy - h - 3); pDC->LineTo(cx, cy - h - 6);
    pDC->MoveTo(cx, cy + h + 3); pDC->LineTo(cx, cy + h + 6);
    pDC->MoveTo(cx - w - 3, cy); pDC->LineTo(cx - w - 6, cy);
    pDC->MoveTo(cx + w + 3, cy); pDC->LineTo(cx + w + 6, cy);
    pDC->SelectObject(pOldPen);
}

static void DrawCrown(CDC* pDC, int cx, int cy, int size, COLORREF color)
{
    CBrush br(color);
    CPen pen(PS_SOLID, 1, RGB(255, 215, 0));
    CBrush* pOldBr = pDC->SelectObject(&br);
    CPen* pOldPen = pDC->SelectObject(&pen);

    CPoint pts[8];
    pts[0] = CPoint(cx - size, cy + size / 2);
    pts[1] = CPoint(cx - size * 2 / 3, cy - size / 2);
    pts[2] = CPoint(cx - size / 3, cy);
    pts[3] = CPoint(cx, cy - size);
    pts[4] = CPoint(cx + size / 3, cy);
    pts[5] = CPoint(cx + size * 2 / 3, cy - size / 2);
    pts[6] = CPoint(cx + size, cy + size / 2);
    pts[7] = CPoint(cx - size, cy + size / 2);
    pDC->Polygon(pts, 7);

    CBrush brJewel(RGB(255, 100, 100));
    pDC->SelectObject(&brJewel);
    pDC->Ellipse(cx - 2, cy - size - 2, cx + 2, cy - size + 2);
    pDC->Ellipse(cx - size * 2 / 3 - 2, cy - size / 2 - 2, cx - size * 2 / 3 + 2, cy - size / 2 + 2);
    pDC->Ellipse(cx + size * 2 / 3 - 2, cy - size / 2 - 2, cx + size * 2 / 3 + 2, cy - size / 2 + 2);

    pDC->SelectObject(pOldBr);
    pDC->SelectObject(pOldPen);
}

static void DrawLaceLine(CDC* pDC, int x1, int y1, int x2, int y2, COLORREF color)
{
    CPen pen(PS_SOLID, 1, color);
    CPen* pOldPen = pDC->SelectObject(&pen);

    int dx = x2 - x1;
    int dy = y2 - y1;
    int steps = max(abs(dx), abs(dy)) / 8;
    if (steps < 2) steps = 2;

    for (int i = 0; i <= steps; i++)
    {
        int x = x1 + dx * i / steps;
        int y = y1 + dy * i / steps;
        int wave = (i % 2 == 0) ? 2 : -2;

        if (abs(dx) > abs(dy))
            pDC->Ellipse(x - 2, y + wave - 2, x + 2, y + wave + 2);
        else
            pDC->Ellipse(x + wave - 2, y - 2, x + wave + 2, y + 2);
    }

    pDC->SelectObject(pOldPen);
}

static void DrawRibbon(CDC* pDC, CRect rc, COLORREF color)
{
    CBrush br(color);
    CPen pen(PS_SOLID, 1, RGB(200, 100, 150));
    CBrush* pOldBr = pDC->SelectObject(&br);
    CPen* pOldPen = pDC->SelectObject(&pen);

    int cx = rc.CenterPoint().x;
    int cy = rc.CenterPoint().y;
    int w = rc.Width() / 2;
    int h = rc.Height() / 2;

    CRect rcCenter(cx - w, cy - h / 3, cx + w, cy + h / 3);
    pDC->RoundRect(&rcCenter, CPoint(h / 2, h / 2));
    CRect rcLeft(cx - w / 3, cy - h, cx, cy + h);
    pDC->Ellipse(&rcLeft);
    CRect rcRight(cx, cy - h, cx + w / 3, cy + h);
    pDC->Ellipse(&rcRight);

    pDC->SelectObject(pOldBr);
    pDC->SelectObject(pOldPen);
}

static void DrawBigRibbon(CDC* pDC, int cx, int cy, int size, COLORREF color)
{
    CBrush br(color);
    CPen pen(PS_SOLID, 2, RGB(255, 140, 180));
    CBrush* pOldBr = pDC->SelectObject(&br);
    CPen* pOldPen = pDC->SelectObject(&pen);

    CRect rcCenter(cx - size / 2, cy - size / 3, cx + size / 2, cy + size / 3);
    pDC->RoundRect(&rcCenter, CPoint(size / 4, size / 4));

    CPoint ptsLeft[4];
    ptsLeft[0] = CPoint(cx - size / 2, cy);
    ptsLeft[1] = CPoint(cx - size, cy - size / 2);
    ptsLeft[2] = CPoint(cx - size * 9 / 10, cy);
    ptsLeft[3] = CPoint(cx - size, cy + size / 2);
    pDC->Polygon(ptsLeft, 4);

    CPoint ptsRight[4];
    ptsRight[0] = CPoint(cx + size / 2, cy);
    ptsRight[1] = CPoint(cx + size, cy - size / 2);
    ptsRight[2] = CPoint(cx + size * 9 / 10, cy);
    ptsRight[3] = CPoint(cx + size, cy + size / 2);
    pDC->Polygon(ptsRight, 4);

    CBrush brGold(RGB(255, 215, 0));
    pDC->SelectObject(&brGold);
    pDC->Ellipse(cx - 3, cy - 3, cx + 3, cy + 3);

    pDC->SelectObject(pOldBr);
    pDC->SelectObject(pOldPen);
}

static void DrawFlower(CDC* pDC, int cx, int cy, int size, COLORREF color)
{
    CBrush br(color);
    CPen pen(PS_SOLID, 1, color);
    CBrush* pOldBr = pDC->SelectObject(&br);
    CPen* pOldPen = pDC->SelectObject(&pen);

    for (int i = 0; i < 5; i++)
    {
        double angle = i * 2.0 * 3.14159 / 5.0;
        int px = cx + (int)(size * 0.6 * cos(angle));
        int py = cy + (int)(size * 0.6 * sin(angle));
        pDC->Ellipse(px - size / 3, py - size / 3, px + size / 3, py + size / 3);
    }

    CBrush brCenter(RGB(255, 255, 100));
    pDC->SelectObject(&brCenter);
    pDC->Ellipse(cx - size / 4, cy - size / 4, cx + size / 4, cy + size / 4);

    pDC->SelectObject(pOldBr);
    pDC->SelectObject(pOldPen);
}

static void DrawHanamaru(CDC* pDC, CRect rc, COLORREF colorCenter, COLORREF colorPetal)
{
    int cx = rc.CenterPoint().x;
    int cy = rc.CenterPoint().y;
    int radius = min(rc.Width(), rc.Height()) / 2 - 2;
    if (radius < 3) return;

    CBrush brPetal(colorPetal);
    CPen penPetal(PS_SOLID, 1, RGB(255, 140, 180));
    CBrush* pOldBr = pDC->SelectObject(&brPetal);
    CPen* pOldPen = pDC->SelectObject(&penPetal);

    const int numPetals = 8;
    const double angleStep = 2.0 * 3.14159265358979323846 / numPetals;

    for (int i = 0; i < numPetals; i++)
    {
        double angle = angleStep * i;
        int px = cx + (int)(radius * 0.65 * cos(angle));
        int py = cy + (int)(radius * 0.65 * sin(angle));
        int petalSize = radius / 2.5;
        CRect rcPetal(px - petalSize, py - petalSize, px + petalSize, py + petalSize);
        pDC->Ellipse(&rcPetal);
    }

    CBrush brMidPetal(RGB(255, 150, 180));
    pDC->SelectObject(&brMidPetal);
    for (int i = 0; i < numPetals; i++)
    {
        double angle = angleStep * i + angleStep / 2.0;
        int px = cx + (int)(radius * 0.45 * cos(angle));
        int py = cy + (int)(radius * 0.45 * sin(angle));
        int petalSize = radius / 4;
        CRect rcPetal(px - petalSize, py - petalSize, px + petalSize, py + petalSize);
        pDC->Ellipse(&rcPetal);
    }

    CBrush brCenterOuter(RGB(255, 120, 160));
    pDC->SelectObject(&brCenterOuter);
    int outerRadius = radius / 2;
    CRect rcOuter(cx - outerRadius, cy - outerRadius, cx + outerRadius, cy + outerRadius);
    pDC->Ellipse(&rcOuter);

    CBrush brCenterInner(colorCenter);
    pDC->SelectObject(&brCenterInner);
    int innerRadius = radius / 3;
    CRect rcInner(cx - innerRadius, cy - innerRadius, cx + innerRadius, cy + innerRadius);
    pDC->Ellipse(&rcInner);

    CBrush brOrange(RGB(255, 140, 80));
    CPen penOrange(PS_SOLID, 1, RGB(255, 100, 50));
    pDC->SelectObject(&brOrange);
    pDC->SelectObject(&penOrange);
    int smallRadius = radius / 6;
    CRect rcSmall(cx - smallRadius, cy - smallRadius, cx + smallRadius, cy + smallRadius);
    pDC->Ellipse(&rcSmall);

    pDC->SelectObject(pOldBr);
    pDC->SelectObject(pOldPen);

    DrawStar(pDC, cx - radius * 8 / 10, cy - radius * 8 / 10, 2, RGB(255, 140, 180));
    DrawStar(pDC, cx + radius * 8 / 10, cy - radius * 8 / 10, 2, RGB(255, 140, 180));
    DrawStar(pDC, cx - radius * 8 / 10, cy + radius * 8 / 10, 2, RGB(255, 140, 180));
    DrawStar(pDC, cx + radius * 8 / 10, cy + radius * 8 / 10, 2, RGB(255, 140, 180));
    DrawStar(pDC, cx, cy - radius * 9 / 10, 1, RGB(255, 180, 200));
    DrawStar(pDC, cx, cy + radius * 9 / 10, 1, RGB(255, 180, 200));
    DrawStar(pDC, cx - radius * 9 / 10, cy, 1, RGB(255, 180, 200));
    DrawStar(pDC, cx + radius * 9 / 10, cy, 1, RGB(255, 180, 200));
}

static void DrawDecorations(CDC* pDC, CRect rect, BOOL bPatternA, BOOL bPushed)
{
    CPen penVine(PS_SOLID, 1, COLOR_VINE_DECO);
    CBrush brFlower(COLOR_HEART);
    CBrush brCenter(RGB(255, 255, 0));

    CPen* pOldPen = pDC->SelectObject(&penVine);
    CBrush* pOldBrush = pDC->SelectObject(&brFlower);

    int offset = bPushed ? 1 : 0;
    rect.DeflateRect(2, 2);

    struct Corner { int x; int y; int dx; int dy; };
    std::vector<Corner> corners;

    if (bPatternA)
    {
        corners.push_back({ rect.left + offset, rect.top + offset,  1,  1 });
        corners.push_back({ rect.right - 1 + offset, rect.bottom - 1 + offset, -1, -1 });
    }
    else
    {
        corners.push_back({ rect.right - 1 + offset, rect.top + offset, -1,  1 });
        corners.push_back({ rect.left + offset,  rect.bottom - 1 + offset,  1, -1 });
    }

    for (const auto& c : corners)
    {
        CPoint pts[4];
        pts[0] = CPoint(c.x, c.y + 12 * c.dy);
        pts[1] = CPoint(c.x + 4 * c.dx, c.y + 4 * c.dy);
        pts[2] = CPoint(c.x + 4 * c.dx, c.y + 4 * c.dy);
        pts[3] = CPoint(c.x + 12 * c.dx, c.y);
        pDC->PolyBezier(pts, 4);

        int r = 2;
        int fx = c.x + 4 * c.dx;
        int fy = c.y + 4 * c.dy;

        pDC->SelectObject(&brFlower);
        pDC->SelectObject(GetStockObject(NULL_PEN));
        pDC->Ellipse(fx - r, fy - r * 2, fx + r, fy);
        pDC->Ellipse(fx - r, fy, fx + r, fy + r * 2);
        pDC->Ellipse(fx - r * 2, fy - r, fx, fy + r);
        pDC->Ellipse(fx, fy - r, fx + r * 2, fy + r);

        pDC->SelectObject(&brCenter);
        pDC->Ellipse(fx - 1, fy - 1, fx + 1, fy + 1);
        pDC->SelectObject(&penVine);
    }

    pDC->SelectObject(pOldPen);
    pDC->SelectObject(pOldBrush);
}

static void DrawSmartText(CDC* pDC, CRect rect, CString strText, BOOL bDisabled, BOOL bPushed)
{
    if (strText.IsEmpty()) return;

    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(bDisabled ? RGB(128, 128, 128) : COLOR_EDIT_TEXT);

    CRect rcText = rect;
    rcText.DeflateRect(1, 1);
    if (bPushed) rcText.OffsetRect(1, 1);

    CFont* pCurrentFont = pDC->GetCurrentFont();
    LOGFONT lf;
    pCurrentFont->GetLogFont(&lf);

    long targetHeight = abs(lf.lfHeight);
    targetHeight = max(8L, targetHeight - 2);
    lf.lfHeight = -targetHeight;

    CFont fontSmall;
    fontSmall.CreateFontIndirect(&lf);
    CFont* pOldFont = pDC->SelectObject(&fontSmall);

    CSize szText = pDC->GetTextExtent(strText);
    if (szText.cx <= rcText.Width())
    {
        pDC->DrawText(strText, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else
    {
        CRect rcCalc = rcText;
        int nHeight = pDC->DrawText(strText, &rcCalc, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
        if (nHeight <= rcText.Height())
        {
            CRect rcDraw = rcText;
            rcDraw.top += (rcText.Height() - nHeight) / 2;
            pDC->DrawText(strText, &rcDraw, DT_CENTER | DT_WORDBREAK);
        }
        else
        {
            pDC->SelectObject(pOldFont);
            fontSmall.DeleteObject();

            int nTryHeight = targetHeight;
            BOOL bPrinted = FALSE;

            while (nTryHeight > 6)
            {
                nTryHeight--;
                lf.lfHeight = -nTryHeight;
                CFont fontTry;
                fontTry.CreateFontIndirect(&lf);
                pDC->SelectObject(&fontTry);

                CRect rcTry = rcText;
                int nH = pDC->DrawText(strText, &rcTry, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);
                if (nH <= rcText.Height() && rcTry.Width() <= rcText.Width())
                {
                    CRect rcDraw = rcText;
                    rcDraw.top += (rcText.Height() - nH) / 2;
                    pDC->DrawText(strText, &rcDraw, DT_CENTER | DT_WORDBREAK);
                    pDC->SelectObject(pOldFont);
                    fontTry.DeleteObject();
                    bPrinted = TRUE;
                    break;
                }
                pDC->SelectObject(pOldFont);
                fontTry.DeleteObject();
            }

            if (!bPrinted)
            {
                lf.lfHeight = -6;
                CFont fontMin;
                fontMin.CreateFontIndirect(&lf);
                pDC->SelectObject(&fontMin);
                pDC->DrawText(strText, &rcText, DT_CENTER | DT_WORDBREAK | DT_VCENTER);
                pDC->SelectObject(pOldFont);
                fontMin.DeleteObject();
            }
            return;
        }
    }

    pDC->SelectObject(pOldFont);
    fontSmall.DeleteObject();
}

static void DrawSmartText2(CDC* pDC, CRect rect, CString strText, UINT nFormat, BOOL bDisabled, BOOL bPushed)
{
    if (strText.IsEmpty()) return;

    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(bDisabled ? RGB(128, 128, 128) : COLOR_EDIT_TEXT);

    CRect rcLimit = rect;
    rcLimit.DeflateRect(2, 0);
    if (bPushed) rcLimit.OffsetRect(1, 1);

    CFont* pCurrentFont = pDC->GetCurrentFont();
    LOGFONT lf;
    pCurrentFont->GetLogFont(&lf);
    long targetHeight = abs(lf.lfHeight);
    const long MIN_HEIGHT = 6;

    CFont fontFinal;
    while (targetHeight >= MIN_HEIGHT)
    {
        lf.lfHeight = -targetHeight;
        CFont fontTry;
        fontTry.CreateFontIndirect(&lf);
        CFont* pOldFont = pDC->SelectObject(&fontTry);

        CRect rcCalc = rcLimit;
        pDC->DrawText(strText, &rcCalc, nFormat | DT_CALCRECT);
        pDC->SelectObject(pOldFont);

        if (rcCalc.Width() <= rcLimit.Width() && rcCalc.Height() <= rcLimit.Height())
        {
            fontFinal.CreateFontIndirect(&lf);
            fontTry.DeleteObject();
            break;
        }
        fontTry.DeleteObject();
        targetHeight--;
    }

    if (!fontFinal.GetSafeHandle())
    {
        lf.lfHeight = -MIN_HEIGHT;
        fontFinal.CreateFontIndirect(&lf);
    }

    CFont* pOldFont = pDC->SelectObject(&fontFinal);
    pDC->DrawText(strText, &rcLimit, nFormat);
    pDC->SelectObject(pOldFont);   // [FIX❶] 先にDCから外す
    fontFinal.DeleteObject();
}

// ============================================================================
// [FIX❺] CCustomControlUtility - ブラシキャッシュをmapで管理
// ============================================================================
/* SetControlColor の static brushes 配列を map 方式に変更した実装を
   CCustomControl.h 内の inline 実装としてではなく .cpp 側で提供します。
   ヘッダ側の既存 inline 実装は以下のように置き換えてください。          */

   // ============================================================================
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
END_MESSAGE_MAP()

CCustomEdit::CCustomEdit() : m_bHasFocus(FALSE), m_bAutoDelete(FALSE)
{
    m_brBackground.CreateSolidBrush(COLOR_EDIT_BG);
}

CCustomEdit::~CCustomEdit()
{
    if (m_fontBold.GetSafeHandle())     m_fontBold.DeleteObject();
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

// [FIX❺] EnableAutoDelete に対応した PostNcDestroy
void CCustomEdit::PostNcDestroy()
{
    CEdit::PostNcDestroy();
    if (m_bAutoDelete) delete this;  // ← 追加
}

void CCustomEdit::PreSubclassWindow()
{
    CEdit::PreSubclassWindow();
    CWnd* pParent = GetParent();
    if (pParent)
    {
        CFont* pParentFont = pParent->GetFont();
        if (pParentFont)
        {
            LOGFONT lf;
            pParentFont->GetLogFont(&lf);
            lf.lfWeight = FW_BOLD;
            if (m_fontBold.GetSafeHandle()) m_fontBold.DeleteObject();
            m_fontBold.CreateFontIndirect(&lf);
            CEdit::SetFont(&m_fontBold);
        }
    }
}

HBRUSH CCustomEdit::CtlColor(CDC* pDC, UINT nCtlColor)
{
    pDC->SetBkColor(COLOR_EDIT_BG);
    pDC->SetTextColor(COLOR_EDIT_TEXT);
    return (HBRUSH)m_brBackground.GetSafeHandle();
}

void CCustomEdit::OnPaint() { Default(); }
BOOL CCustomEdit::OnEraseBkgnd(CDC* pDC) { return FALSE; }

void CCustomEdit::OnNcPaint()
{
    CWindowDC dc(this);
    CRect rect;
    GetWindowRect(&rect);
    rect.OffsetRect(-rect.left, -rect.top);

    CPen pen(PS_SOLID, 2, m_bHasFocus ? RGB(255, 140, 180) : RGB(255, 182, 193));
    CPen* pOldPen = dc.SelectObject(&pen);
    dc.SelectStockObject(NULL_BRUSH);
    dc.RoundRect(&rect, CPoint(6, 6));
    dc.SelectObject(pOldPen);

    if (m_bHasFocus)
    {
        DrawStar(&dc, rect.right - 8, rect.top + 8, 3, RGB(255, 215, 0));
        DrawStar(&dc, rect.left + 8, rect.top + 8, 2, RGB(255, 240, 150));
        DrawStar(&dc, rect.right - 8, rect.bottom - 8, 2, RGB(255, 240, 150));
    }

    CRect rcRibbonL(rect.left + 2, rect.CenterPoint().y - 3, rect.left + 8, rect.CenterPoint().y + 3);
    CRect rcRibbonR(rect.right - 8, rect.CenterPoint().y - 3, rect.right - 2, rect.CenterPoint().y + 3);
    DrawRibbon(&dc, rcRibbonL, RGB(255, 200, 220));
    DrawRibbon(&dc, rcRibbonR, RGB(255, 200, 220));
}

void CCustomEdit::OnSetFocus(CWnd* pOldWnd)
{
    CEdit::OnSetFocus(pOldWnd);
    m_bHasFocus = TRUE;
    SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

void CCustomEdit::OnKillFocus(CWnd* pNewWnd)
{
    CEdit::OnKillFocus(pNewWnd);
    m_bHasFocus = FALSE;
    SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

// ============================================================================
// CCustomStatic
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomStatic, CStatic)

BEGIN_MESSAGE_MAP(CCustomStatic, CStatic)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_MESSAGE(WM_SETTEXT, OnSetText)
    ON_MESSAGE(WM_GETTEXT, OnGetText)
    ON_MESSAGE(WM_GETTEXTLENGTH, OnGetTextLength)
END_MESSAGE_MAP()

CCustomStatic::CCustomStatic()
    : m_bAutoDelete(FALSE)
    , m_clrGradStart(RGB(255, 255, 255)), m_clrGradEnd(RGB(255, 255, 255))
    , m_nGradDirection(0), m_bGradEnable(FALSE)
    , m_clrShadow(RGB(0, 0, 0)), m_nShadowDirection(135)
    , m_nShadowDistance(2), m_nShadowBlur(3), m_bShadowEnable(FALSE)
    , m_bPreferWideMode(FALSE)
    , m_nCachedHeight(0), m_nCachedWidth(0)
    , m_strCachedText(_T("")), m_strText(_T(""))
{
}

CCustomStatic::~CCustomStatic()
{
    if (m_font.GetSafeHandle()) m_font.DeleteObject();
}

// [FIX❺]
void CCustomStatic::PostNcDestroy()
{
    CStatic::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

void CCustomStatic::SetGradation(COLORREF colorStart, COLORREF colorEnd, int nDirection, BOOL bEnable)
{
    m_clrGradStart = colorStart;
    m_clrGradEnd = colorEnd;
    m_nGradDirection = nDirection % 360;
    if (m_nGradDirection < 0) m_nGradDirection += 360;
    m_bGradEnable = bEnable;
    m_strCachedText.Empty();
    if (GetSafeHwnd()) Invalidate();
}

void CCustomStatic::GetGradation(COLORREF* pColorStart, COLORREF* pColorEnd, int* pDirection, BOOL* pbEnable) const
{
    if (pColorStart) *pColorStart = m_clrGradStart;
    if (pColorEnd)   *pColorEnd = m_clrGradEnd;
    if (pDirection)  *pDirection = m_nGradDirection;
    if (pbEnable)    *pbEnable = m_bGradEnable;
}

void CCustomStatic::SetDropShadow(COLORREF color, int nDirection, int nDistance, int nBlur, BOOL bEnable)
{
    m_clrShadow = color;
    m_nShadowDirection = nDirection % 360;
    if (m_nShadowDirection < 0) m_nShadowDirection += 360;
    m_nShadowDistance = max(0, nDistance);
    m_nShadowBlur = max(0, min(20, nBlur));
    m_bShadowEnable = bEnable;
    if (GetSafeHwnd()) Invalidate();
}

void CCustomStatic::GetDropShadow(COLORREF* pColor, int* pDirection, int* pDistance, int* pBlur, BOOL* pbEnable) const
{
    if (pColor)     *pColor = m_clrShadow;
    if (pDirection) *pDirection = m_nShadowDirection;
    if (pDistance)  *pDistance = m_nShadowDistance;
    if (pBlur)      *pBlur = m_nShadowBlur;
    if (pbEnable)   *pbEnable = m_bShadowEnable;
}

void CCustomStatic::SetPreferWideMode(BOOL bPreferWide)
{
    m_bPreferWideMode = bPreferWide;
    m_strCachedText.Empty();
    if (GetSafeHwnd()) Invalidate();
}

BOOL CCustomStatic::GetPreferWideMode() const { return m_bPreferWideMode; }

void CCustomStatic::SetFont(CFont* pFont, BOOL bRedraw)
{
    if (pFont)
    {
        LOGFONT lf;
        pFont->GetLogFont(&lf);
        if (m_font.GetSafeHandle()) m_font.DeleteObject();
        m_font.CreateFontIndirect(&lf);
        CStatic::SetFont(&m_font, bRedraw);
    }
}

void CCustomStatic::PreSubclassWindow()
{
    CStatic::PreSubclassWindow();
    CWnd::GetWindowText(m_strText);
    CWnd* pParent = GetParent();
    if (pParent)
    {
        CFont* pParentFont = pParent->GetFont();
        if (pParentFont) SetFont(pParentFont, FALSE);
    }
}

void CCustomStatic::OnPaint()
{
    CPaintDC dc(this);
    CRect rect;
    GetClientRect(&rect);

    CDC memDC;
    memDC.CreateCompatibleDC(&dc);
    CBitmap memBitmap;
    memBitmap.CreateCompatibleBitmap(&dc, rect.Width(), rect.Height());
    CBitmap* pOldBitmap = memDC.SelectObject(&memBitmap);

    memDC.FillSolidRect(&rect, COLOR_DIALOG_BG);

    if (m_strText.IsEmpty()) CWnd::GetWindowText(m_strText);

    if (!m_strText.IsEmpty())
    {
        CString strText = m_strText;
        std::vector<TextSegment> segments = ParseFormattedText(strText);
        BOOL bHasFormatting = (strText.Find(_T("!@")) >= 0);

        CRect rectWithMargin = rect;
        rectWithMargin.DeflateRect(1, 1);

        CFont* pBaseFont = GetFont();
        CFont* pOldFont = memDC.SelectObject(pBaseFont);
        memDC.SetBkMode(TRANSPARENT);

        LOGFONT lfBase;
        pBaseFont->GetLogFont(&lfBase);
        const int kMinHeight = 6;
        const int baseHeight = abs(lfBase.lfHeight);

        int finalHeight = 0;
        int finalWidth = 0;
        CSize szFinal;

        BOOL bNeedRecalc = (strText != m_strCachedText) ||
            (m_nCachedHeight == 0) ||
            (m_rectCached != rect);

        if (bNeedRecalc)
        {
            if (bHasFormatting)
            {
                auto MeasureText = [&](int height, int width) -> CSize {
                    return MeasureSegmentedText(&memDC, segments, lfBase, height, width);
                    };

                int fitHeight = kMinHeight;
                int baseWidth = 0;
                CSize szFit;

                for (int h = baseHeight; h >= kMinHeight; h--)
                {
                    LOGFONT lfTry = lfBase;
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
                    finalHeight = rectWithMargin.Height();
                    double scale = (double)finalHeight / fitHeight;
                    finalWidth = max(1, (int)(baseWidth / scale));
                    szFinal = MeasureText(finalHeight, finalWidth);
                }

                if (m_bPreferWideMode && szFinal.cx < rectWithMargin.Width())
                {
                    int startWidth = (finalWidth > 0) ? finalWidth : baseWidth;
                    int maxWidth = startWidth * 3;
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
            }
            else
            {
                auto MeasureText = [&](int height, int width) -> CSize {
                    LOGFONT lfTry = lfBase;
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

                int fitHeight = kMinHeight;
                int baseWidth = 0;
                CSize szFit;

                for (int h = baseHeight; h >= kMinHeight; h--)
                {
                    LOGFONT lfTry = lfBase;
                    lfTry.lfHeight = -h;
                    lfTry.lfWidth = 0;
                    CFont fontTry;
                    fontTry.CreateFontIndirect(&lfTry);
                    CFont* pOld = memDC.SelectObject(&fontTry);
                    CSize size = memDC.GetTextExtent(strText);
                    TEXTMETRIC tm;
                    memDC.GetTextMetrics(&tm);
                    memDC.SelectObject(pOld);
                    fontTry.DeleteObject();

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
                    finalHeight = rectWithMargin.Height();
                    double scale = (double)finalHeight / fitHeight;
                    finalWidth = max(1, (int)(baseWidth / scale));
                    szFinal = MeasureText(finalHeight, finalWidth);
                }

                if (m_bPreferWideMode && szFinal.cx < rectWithMargin.Width())
                {
                    int startWidth = (finalWidth > 0) ? finalWidth : baseWidth;
                    int maxWidth = startWidth * 3;
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

        DWORD dwStyle = GetStyle();
        UINT nFormat = DT_VCENTER | DT_SINGLELINE;
        if (dwStyle & SS_CENTER) nFormat |= DT_CENTER;
        else if (dwStyle & SS_RIGHT)  nFormat |= DT_RIGHT;
        else                          nFormat |= DT_LEFT;

        if (bHasFormatting)
        {
            DrawSegmentedText(&memDC, rect, segments, lfBase, finalHeight, finalWidth, nFormat);
        }
        else
        {
            // [FIX❶] fontFinal の SelectObject → 描画 → SelectObject(pOldFont) → DeleteObject の順を厳守
            CFont fontFinal;
            LOGFONT lfFinal = lfBase;
            lfFinal.lfHeight = -finalHeight;
            lfFinal.lfWidth = finalWidth;
            fontFinal.CreateFontIndirect(&lfFinal);
            CFont* pOldFontInner = memDC.SelectObject(&fontFinal);  // ← DC に選択

            szFinal = memDC.GetTextExtent(strText);

            if (m_bGradEnable)
            {
                DrawTextWithGradient(&memDC, rect, strText, nFormat,
                    m_clrGradStart, m_clrGradEnd, m_nGradDirection,
                    m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
                    m_bShadowEnable, COLOR_DIALOG_BG, szFinal.cx);
            }
            else
            {
                DrawTextWithShadow(&memDC, rect, strText, nFormat, RGB(0, 0, 0),
                    m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
                    m_bShadowEnable, COLOR_DIALOG_BG);
            }

            memDC.SelectObject(pOldFontInner);  // [FIX❶] ← 先に DC から外す
            fontFinal.DeleteObject();            // [FIX❶] ← それから解放
        }

        memDC.SelectObject(pOldFont);
    }

    dc.BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOldBitmap);
    memBitmap.DeleteObject();
    memDC.DeleteDC();
}

BOOL CCustomStatic::OnEraseBkgnd(CDC* pDC) { return TRUE; }

LRESULT CCustomStatic::OnSetText(WPARAM wParam, LPARAM lParam)
{
    LPCTSTR lpszText = (LPCTSTR)lParam;
    m_strText = lpszText ? lpszText : _T("");
    m_strCachedText.Empty();
    if (GetSafeHwnd()) Invalidate();
    return TRUE;
}

LRESULT CCustomStatic::OnGetText(WPARAM wParam, LPARAM lParam)
{
    int nMaxCount = (int)wParam;
    LPTSTR lpszText = (LPTSTR)lParam;
    if (!lpszText || nMaxCount <= 0) return 0;

    int nLen = m_strText.GetLength();
    int nCopyLen = min(nLen, nMaxCount - 1);
    if (nCopyLen > 0) _tcsncpy_s(lpszText, nMaxCount, (LPCTSTR)m_strText, nCopyLen);
    lpszText[nCopyLen] = _T('\0');
    return nCopyLen;
}

LRESULT CCustomStatic::OnGetTextLength(WPARAM, LPARAM)
{
    return m_strText.GetLength();
}

// ----------- ParseFormattedText -----------
std::vector<TextSegment> CCustomStatic::ParseFormattedText(const CString& str)
{
    std::vector<TextSegment> segments;
    BOOL bBold = FALSE;
    BOOL bItalic = FALSE;
    BOOL bHasColor = FALSE;
    COLORREF currentColor = RGB(0, 0, 0);
    int nFontSizeOffset = 0;
    CString current;

    for (int i = 0; i < str.GetLength(); i++)
    {
        if (i + 1 < str.GetLength() && str[i] == _T('!') && str[i + 1] == _T('@'))
        {
            if (i + 2 < str.GetLength())
            {
                TCHAR cmd = str[i + 2];

                auto FlushSegment = [&]() {
                    if (!current.IsEmpty())
                    {
                        TextSegment seg;
                        seg.text = current;
                        seg.bBold = bBold;
                        seg.bItalic = bItalic;
                        seg.bHasColor = bHasColor;
                        seg.clrText = currentColor;
                        seg.nFontSizeOffset = nFontSizeOffset;
                        segments.push_back(seg);
                        current.Empty();
                    }
                    };

                if (cmd == _T('B')) { FlushSegment(); bBold = !bBold;   i += 2; continue; }
                else if (cmd == _T('I')) { FlushSegment(); bItalic = !bItalic; i += 2; continue; }
                else if (cmd == _T('C') && i + 8 < str.GetLength())
                {
                    CString hexColor = str.Mid(i + 3, 6);
                    int r = 0, g = 0, b = 0;
                    if (_stscanf_s(hexColor, _T("%2x%2x%2x"), &r, &g, &b) == 3)
                    {
                        FlushSegment();
                        bHasColor = TRUE;
                        currentColor = RGB(r, g, b);
                        i += 8; continue;
                    }
                }
                else if (cmd == _T('F') && i + 5 < str.GetLength())
                {
                    TCHAR sign = str[i + 3];
                    CString num = str.Mid(i + 4, 2);
                    if ((sign == _T('+') || sign == _T('-')) && num.GetLength() == 2 && _istdigit(num[0]) && _istdigit(num[1]))
                    {
                        int offset = _ttoi(num);
                        if (sign == _T('-')) offset = -offset;
                        FlushSegment();
                        nFontSizeOffset += offset;
                        i += 5; continue;
                    }
                }
            }
        }
        current += str[i];
    }

    if (!current.IsEmpty())
    {
        TextSegment seg;
        seg.text = current;
        seg.bBold = bBold;
        seg.bItalic = bItalic;
        seg.bHasColor = bHasColor;
        seg.clrText = currentColor;
        seg.nFontSizeOffset = nFontSizeOffset;
        segments.push_back(seg);
    }
    return segments;
}

CSize CCustomStatic::MeasureSegmentedText(CDC* pDC, const std::vector<TextSegment>& segments,
    const LOGFONT& lfBase, int height, int width)
{
    CSize totalSize(0, 0);
    for (size_t i = 0; i < segments.size(); i++)
    {
        LOGFONT lf = lfBase;
        lf.lfHeight = -max(6, height + segments[i].nFontSizeOffset);
        lf.lfWidth = width;
        if (segments[i].bBold)   lf.lfWeight = FW_BOLD;
        if (segments[i].bItalic) lf.lfItalic = TRUE;

        CFont font;
        font.CreateFontIndirect(&lf);
        CFont* pOldFont = pDC->SelectObject(&font);
        CSize sz = pDC->GetTextExtent(segments[i].text);
        totalSize.cx += sz.cx;
        if (sz.cy > totalSize.cy) totalSize.cy = sz.cy;
        pDC->SelectObject(pOldFont);
        font.DeleteObject();
    }
    return totalSize;
}

void CCustomStatic::DrawSegmentedText(CDC* pDC, const CRect& rect,
    const std::vector<TextSegment>& segments,
    const LOGFONT& lfBase, int height, int width, UINT nFormat)
{
    CSize totalSize = MeasureSegmentedText(pDC, segments, lfBase, height, width);
    int xPos = rect.left;

    if (nFormat & DT_CENTER) xPos = rect.left + (rect.Width() - totalSize.cx) / 2;
    else if (nFormat & DT_RIGHT)  xPos = rect.right - totalSize.cx;

    for (size_t i = 0; i < segments.size(); i++)
    {
        LOGFONT lf = lfBase;
        lf.lfHeight = -max(6, height + segments[i].nFontSizeOffset);
        lf.lfWidth = width;
        if (segments[i].bBold)   lf.lfWeight = FW_BOLD;
        if (segments[i].bItalic) lf.lfItalic = TRUE;

        CFont font;
        font.CreateFontIndirect(&lf);
        CFont* pOldFont = pDC->SelectObject(&font);

        CSize sz = pDC->GetTextExtent(segments[i].text);
        CRect segmentRect;
        segmentRect.left = xPos;
        segmentRect.right = xPos + sz.cx;
        segmentRect.top = rect.top;
        segmentRect.bottom = rect.bottom;

        COLORREF textColor = segments[i].bHasColor ? segments[i].clrText : RGB(0, 0, 0);

        if (m_bGradEnable)
        {
            DrawTextWithGradient(pDC, segmentRect, segments[i].text, DT_VCENTER | DT_SINGLELINE | DT_LEFT,
                m_clrGradStart, m_clrGradEnd, m_nGradDirection,
                m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
                m_bShadowEnable, COLOR_DIALOG_BG, sz.cx);
        }
        else
        {
            DrawTextWithShadow(pDC, segmentRect, segments[i].text, DT_VCENTER | DT_SINGLELINE | DT_LEFT, textColor,
                m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
                m_bShadowEnable, COLOR_DIALOG_BG);
        }

        xPos += sz.cx;
        pDC->SelectObject(pOldFont);  // [FIX❶] DC から外してから
        font.DeleteObject();
    }
}

// ============================================================================
// CCustomListBox
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomListBox, CListBox)

BEGIN_MESSAGE_MAP(CCustomListBox, CListBox)
    ON_WM_CTLCOLOR_REFLECT()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCustomListBox::CCustomListBox() : m_bAutoDelete(FALSE)
{
    m_brBackground.CreateSolidBrush(COLOR_LIST_BG);
}

CCustomListBox::~CCustomListBox()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

// [FIX❺]
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

void CCustomListBox::OnPaint() { Default(); }
BOOL CCustomListBox::OnEraseBkgnd(CDC*) { return FALSE; }

void CCustomListBox::DrawItem(LPDRAWITEMSTRUCT lp)
{
    if (lp->itemID == (UINT)-1) return;
    CDC* pDC = CDC::FromHandle(lp->hDC);
    CRect rect = lp->rcItem;

    COLORREF clrBg;
    if (lp->itemState & ODS_SELECTED) clrBg = COLOR_SEL_BG;
    else if (lp->itemID % 2 == 0)          clrBg = COLOR_LIST_BG;
    else                                    clrBg = RGB(183, 221, 238);

    pDC->FillSolidRect(&rect, clrBg);  // [FIX❹] CBrush生成をやめFillSolidRectへ

    int iconType = lp->itemID % 4;
    int iconSize = 8;
    int iconX = rect.left + 5;
    int iconY = rect.top + (rect.Height() - iconSize) / 2;

    switch (iconType)
    {
    case 0: DrawFlower(pDC, iconX + iconSize / 2, iconY + iconSize / 2, iconSize / 2, RGB(255, 200, 220)); break;
    case 1: DrawStar(pDC, iconX + iconSize / 2, iconY + iconSize / 2, iconSize / 3, RGB(255, 215, 0)); break;
    case 2: { CRect rcH(iconX, iconY, iconX + iconSize, iconY + iconSize); DrawHeart(pDC, rcH, COLOR_HEART); break; }
    case 3: { CRect rcR(iconX, iconY, iconX + iconSize, iconY + iconSize); DrawRibbon(pDC, rcR, RGB(255, 182, 193)); break; }
    }

    if (lp->itemState & ODS_SELECTED)
        DrawStar(pDC, rect.right - 12, rect.top + rect.Height() / 2, 3, RGB(255, 215, 0));

    CString strText;
    GetText(lp->itemID, strText);
    CRect rcText = rect;
    rcText.left += 20;
    pDC->SetBkMode(TRANSPARENT);
    DrawSmartText(pDC, rcText, strText, FALSE, FALSE);

    if (lp->itemID < (UINT)(GetCount() - 1))
        DrawLaceLine(pDC, rect.left + 15, rect.bottom - 1, rect.right - 15, rect.bottom - 1, RGB(200, 180, 220));
}

void CCustomListBox::MeasureItem(LPMEASUREITEMSTRUCT lp) { lp->itemHeight = 24; }

// ============================================================================
// CCustomComboBox
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomComboBox, CComboBox)

BEGIN_MESSAGE_MAP(CCustomComboBox, CComboBox)
    ON_WM_CTLCOLOR_REFLECT()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_CONTROL_REFLECT(CBN_DROPDOWN, &CCustomComboBox::OnDropdown)
END_MESSAGE_MAP()

CCustomComboBox::CCustomComboBox()
    : m_bAutoDelete(FALSE), m_clrLabelText(RGB(240, 240, 255))
    , m_clrLabelBg(RGB(80, 60, 120))
{
    m_brBackground.CreateSolidBrush(COLOR_COMBO_BG);
}

CCustomComboBox::~CCustomComboBox()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

// [FIX❺]
void CCustomComboBox::PostNcDestroy()
{
    CComboBox::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

int CCustomComboBox::AddString(LPCTSTR lpszString, BOOL bDisabled)
{
    int nIndex = CComboBox::AddString(lpszString);
    if (nIndex >= 0)
    {
        if (nIndex >= (int)m_vDisabledItems.size()) m_vDisabledItems.resize(nIndex + 1, FALSE);
        m_vDisabledItems[nIndex] = bDisabled;
        if (!bDisabled) m_vSelectableIndices.push_back(nIndex);
    }
    return nIndex;
}

int CCustomComboBox::GetCurSel() const
{
    int nPhysical = CComboBox::GetCurSel();
    if (nPhysical < 0) return -1;
    for (int i = 0; i < (int)m_vSelectableIndices.size(); i++)
        if (m_vSelectableIndices[i] == nPhysical) return i;
    return -1;
}

int CCustomComboBox::SetCurSel(int nLogicalIndex)
{
    if (nLogicalIndex < 0) return CComboBox::SetCurSel(-1);
    if (nLogicalIndex >= (int)m_vSelectableIndices.size())
    {
        if (m_vSelectableIndices.empty()) return CB_ERR;
        nLogicalIndex = (int)m_vSelectableIndices.size() - 1;
    }
    return CComboBox::SetCurSel(m_vSelectableIndices[nLogicalIndex]);
}

void CCustomComboBox::SetLabelColor(COLORREF clrText, COLORREF clrBackground) { m_clrLabelText = clrText; m_clrLabelBg = clrBackground; if (GetSafeHwnd())Invalidate(); }
void CCustomComboBox::GetLabelColor(COLORREF* pClrText, COLORREF* pClrBackground) const { if (pClrText)*pClrText = m_clrLabelText; if (pClrBackground)*pClrBackground = m_clrLabelBg; }

int CCustomComboBox::LogicalToPhysical(int nLogical) const
{
    if (nLogical < 0 || nLogical >= (int)m_vSelectableIndices.size()) return -1;
    return m_vSelectableIndices[nLogical];
}

int CCustomComboBox::PhysicalToLogical(int nPhysical) const
{
    for (int i = 0; i < (int)m_vSelectableIndices.size(); i++)
        if (m_vSelectableIndices[i] == nPhysical) return i;
    return -1;
}

void CCustomComboBox::PreSubclassWindow()
{
    CComboBox::PreSubclassWindow();
    DWORD dwStyle = GetStyle();
    dwStyle &= ~CBS_OWNERDRAWVARIABLE;
    dwStyle |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
    ModifyStyle(0, CBS_OWNERDRAWFIXED | CBS_HASSTRINGS);
    SetWindowLong(GetSafeHwnd(), GWL_STYLE, dwStyle);
}

HBRUSH CCustomComboBox::CtlColor(CDC* pDC, UINT nCtlColor)
{
    if (nCtlColor == CTLCOLOR_LISTBOX)
    {
        pDC->SetBkColor(COLOR_COMBO_BG);
        pDC->SetTextColor(RGB(0, 0, 0));
        return (HBRUSH)m_brBackground.GetSafeHandle();
    }
    return NULL;
}

BOOL CCustomComboBox::OnEraseBkgnd(CDC*) { return TRUE; }

void CCustomComboBox::OnPaint()
{
    CPaintDC dcPaint(this);
    CRect rect;
    GetClientRect(&rect);

    CDC memDC;
    CBitmap memBmp;
    memDC.CreateCompatibleDC(&dcPaint);
    memBmp.CreateCompatibleBitmap(&dcPaint, rect.Width(), rect.Height());
    CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

    memDC.FillSolidRect(&rect, COLOR_COMBO_BG);

    CPen penFrame(PS_SOLID, 2, COLOR_VINE_DECO);
    CPen* pOldPen = memDC.SelectObject(&penFrame);
    memDC.SelectStockObject(NULL_BRUSH);
    memDC.RoundRect(&rect, CPoint(10, 10));

    int nBtnWidth = GetSystemMetrics(SM_CXVSCROLL);
    CRect rcBtn(rect.right - nBtnWidth - 4, rect.top + 4, rect.right - 4, rect.bottom - 4);

    // [FIX❹] DrawItemと同様にFillSolidRectへ
    memDC.FillSolidRect(&rcBtn, RGB(255, 200, 220));
    // 角丸は既存のRoundRectで再描画
    {
        CPen penBtn(PS_SOLID, 1, RGB(200, 150, 180));
        memDC.SelectObject(&penBtn);
        memDC.SelectStockObject(NULL_BRUSH);
        memDC.RoundRect(&rcBtn, CPoint(6, 6));
        memDC.SelectObject(pOldPen);
    }

    int nHeartSize = 6;
    int nSpacing = 2;
    int nStartX = rcBtn.left + (rcBtn.Width() - (nHeartSize * 3 + nSpacing * 2)) / 2;
    int nCenterY = rcBtn.Height() / 2 + rcBtn.top;

    for (int i = 0; i < 3; i++)
    {
        CRect rcHeart(
            nStartX + i * (nHeartSize + nSpacing), nCenterY - nHeartSize / 2,
            nStartX + i * (nHeartSize + nSpacing) + nHeartSize, nCenterY + nHeartSize / 2);
        DrawHeart(&memDC, rcHeart, (i == 1) ? COLOR_HEART : RGB(255, 182, 193));
    }

    DrawStar(&memDC, rect.right - 8, rect.top + 8, 3, RGB(255, 215, 0));

    int nPhysicalSel = CComboBox::GetCurSel();
    CString strText;
    if (nPhysicalSel != CB_ERR) GetLBText(nPhysicalSel, strText);

    memDC.SetTextColor(RGB(0, 0, 0));
    CFont* pOldFont = memDC.SelectObject(GetFont());

    CRect rcText = rect;
    rcText.left += 12;
    rcText.right = rcBtn.left - 4;

    BOOL bIsLabel = (nPhysicalSel >= 0 && nPhysicalSel < (int)m_vDisabledItems.size() && m_vDisabledItems[nPhysicalSel]);
    if (nPhysicalSel != CB_ERR && !bIsLabel)
    {
        int crownSize = (rcText.Height() - 8) / 2;
        DrawCrown(&memDC, rcText.left + crownSize, rcText.Height() / 2, crownSize, RGB(255, 215, 0));
        rcText.left += crownSize * 2 + 4;
    }

    memDC.SetBkMode(TRANSPARENT);
    memDC.DrawText(strText, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    memDC.SelectObject(pOldFont);

    dcPaint.BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOldBmp);
    memBmp.DeleteObject();
    memDC.DeleteDC();
}

void CCustomComboBox::DrawItem(LPDRAWITEMSTRUCT lp)
{
    if (lp->itemID == (UINT)-1) return;
    CDC* pDC = CDC::FromHandle(lp->hDC);
    CRect rect = lp->rcItem;

    BOOL bDisabled = (lp->itemID < (UINT)m_vDisabledItems.size()) && m_vDisabledItems[lp->itemID];
    BOOL bSel = !bDisabled && (lp->itemState & ODS_SELECTED);

    COLORREF clrBg;
    if (bDisabled)           clrBg = m_clrLabelBg;
    else if (bSel)                clrBg = COLOR_SEL_BG;
    else if (lp->itemID % 2 == 0)  clrBg = COLOR_COMBO_BG;
    else                          clrBg = RGB(255, 232, 220);

    pDC->FillSolidRect(&rect, clrBg);  // [FIX❹]

    if (!bDisabled)
    {
        int iconType = lp->itemID % 4;
        int iconSize = 8;
        int iconX = rect.left + 6;
        int iconY = rect.top + (rect.Height() - iconSize) / 2;

        switch (iconType)
        {
        case 0: DrawFlower(pDC, iconX + iconSize / 2, iconY + iconSize / 2, iconSize / 2, RGB(255, 200, 220)); break;
        case 1: DrawStar(pDC, iconX + iconSize / 2, iconY + iconSize / 2, iconSize / 3, RGB(255, 215, 0)); break;
        case 2: { CRect rcH(iconX, iconY, iconX + iconSize, iconY + iconSize); DrawHeart(pDC, rcH, COLOR_HEART); break; }
        case 3: { CRect rcR(iconX, iconY, iconX + iconSize, iconY + iconSize); DrawRibbon(pDC, rcR, RGB(255, 182, 193)); break; }
        }
    }

    CString strText;
    GetLBText(lp->itemID, strText);

    CFont* pOldFont = NULL;
    CFont fontCustom;
    CFont* pFont = GetFont();
    LOGFONT lf;
    pFont->GetLogFont(&lf);

    if (bDisabled)
    {
        pDC->SetTextColor(m_clrLabelText);
        lf.lfWeight = FW_BOLD;
        lf.lfItalic = TRUE;
    }
    else
    {
        pDC->SetTextColor(RGB(0, 0, 0));
        lf.lfWeight = FW_BOLD;
    }
    fontCustom.CreateFontIndirect(&lf);
    pOldFont = pDC->SelectObject(&fontCustom);
    pDC->SetBkMode(TRANSPARENT);

    CRect rcText = rect;
    rcText.left += bDisabled ? 4 : 20;
    pDC->DrawText(strText, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (pOldFont)
    {
        pDC->SelectObject(pOldFont);  // [FIX❶]
        fontCustom.DeleteObject();
    }

    if (bSel && !bDisabled)
        DrawCrown(pDC, rect.right - 14, rect.top + rect.Height() / 2, 6, RGB(255, 215, 0));

    if (bDisabled)
    {
        CPen penSep(PS_SOLID, 2, RGB(200, 200, 240));
        CPen* pOldPen = pDC->SelectObject(&penSep);
        if (lp->itemID > 0) { pDC->MoveTo(rect.left + 2, rect.top); pDC->LineTo(rect.right - 2, rect.top); }
        pDC->MoveTo(rect.left + 2, rect.bottom - 1); pDC->LineTo(rect.right - 2, rect.bottom - 1);
        pDC->SelectObject(pOldPen);
    }
    else if (lp->itemID < (UINT)(GetCount() - 1))
    {
        BOOL bNextIsLabel = (lp->itemID + 1 < (UINT)m_vDisabledItems.size()) && m_vDisabledItems[lp->itemID + 1];
        if (!bNextIsLabel)
            DrawLaceLine(pDC, rect.left + 15, rect.bottom - 1, rect.right - 15, rect.bottom - 1, RGB(200, 180, 220));
    }
}

void CCustomComboBox::MeasureItem(LPMEASUREITEMSTRUCT lp) { lp->itemHeight = 28; }
void CCustomComboBox::OnDropdown() { UpdateDropDownWidth(); }

BOOL CCustomComboBox::OnCommand(WPARAM wParam, LPARAM lParam)
{
    WORD wNotifyCode = HIWORD(wParam);
    if (wNotifyCode == CBN_SELCHANGE || wNotifyCode == CBN_SELENDOK)
    {
        int nCurSel = CComboBox::GetCurSel();
        if (nCurSel >= 0)
        {
            BOOL bDisabled = (nCurSel < (int)m_vDisabledItems.size() && m_vDisabledItems[nCurSel]);
            if (bDisabled)
            {
                int nCount = CComboBox::GetCount();
                for (int i = nCurSel + 1; i < nCount; i++)
                {
                    if (!(i < (int)m_vDisabledItems.size() && m_vDisabledItems[i]))
                    {
                        CComboBox::SetCurSel(i); return TRUE;
                    }
                }
                for (int i = nCurSel - 1; i >= 0; i--)
                {
                    if (!(i < (int)m_vDisabledItems.size() && m_vDisabledItems[i]))
                    {
                        CComboBox::SetCurSel(i); return TRUE;
                    }
                }
                CComboBox::SetCurSel(-1);
                return TRUE;
            }
        }
    }
    return CComboBox::OnCommand(wParam, lParam);
}

void CCustomComboBox::UpdateDropDownWidth()
{
    CClientDC dc(this);
    int nMaxWidth = 0;
    CFont* pOldFont = dc.SelectObject(GetFont());
    for (int i = 0; i < GetCount(); i++)
    {
        CString str;
        GetLBText(i, str);
        nMaxWidth = max(nMaxWidth, dc.GetTextExtent(str).cx);
    }
    nMaxWidth += GetSystemMetrics(SM_CXVSCROLL) + 40;
    CRect rect;
    GetWindowRect(&rect);
    SetDroppedWidth(max(nMaxWidth, rect.Width()));
    dc.SelectObject(pOldFont);
}

// ============================================================================
// CCustomListCtrl
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomListCtrl, CListCtrl)

BEGIN_MESSAGE_MAP(CCustomListCtrl, CListCtrl)
    ON_WM_CTLCOLOR_REFLECT()
    ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnCustomDraw)
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_VSCROLL()
    ON_WM_HSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCustomListCtrl::CCustomListCtrl() : m_bAutoDelete(FALSE), m_nHotItem(-1)
{
    m_brBackground.CreateSolidBrush(COLOR_LIST_BG);
}

CCustomListCtrl::~CCustomListCtrl()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

// [FIX❺]
void CCustomListCtrl::PostNcDestroy()
{
    CListCtrlA::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

void CCustomListCtrl::PreSubclassWindow()
{
    CListCtrlA::PreSubclassWindow();
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

void CCustomListCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
    LVHITTESTINFO hti; hti.pt = point;
    UpdateHotItem(SubItemHitTest(&hti));
    TRACKMOUSEEVENT tme = { sizeof(tme),TME_LEAVE,m_hWnd,0 };
    TrackMouseEvent(&tme);
    CListCtrl::OnMouseMove(nFlags, point);
}

void CCustomListCtrl::OnMouseLeave() { UpdateHotItem(-1); CListCtrl::OnMouseLeave(); }
void CCustomListCtrl::OnVScroll(UINT n, UINT p, CScrollBar* s) { CListCtrl::OnVScroll(n, p, s); UpdateHotItemFromCursor(); RedrawVisibleItems(); }
void CCustomListCtrl::OnHScroll(UINT n, UINT p, CScrollBar* s) { CListCtrl::OnHScroll(n, p, s); UpdateHotItemFromCursor(); RedrawVisibleItems(); }
BOOL CCustomListCtrl::OnMouseWheel(UINT n, short z, CPoint p) { BOOL r = CListCtrl::OnMouseWheel(n, z, p); UpdateHotItemFromCursor(); RedrawVisibleItems(); return r; }

void CCustomListCtrl::UpdateHotItem(int nItem)
{
    if (m_nHotItem == nItem) return;
    int nOldHot = m_nHotItem;
    m_nHotItem = nItem;
    if (nOldHot >= 0) RedrawItems(nOldHot, nOldHot);
    if (m_nHotItem >= 0) RedrawItems(m_nHotItem, m_nHotItem);
    UpdateWindow();
}

void CCustomListCtrl::UpdateHotItemFromCursor()
{
    if (!GetSafeHwnd()) return;
    CPoint pt;
    if (!GetCursorPos(&pt)) return;
    ScreenToClient(&pt);
    LVHITTESTINFO hti; hti.pt = pt;
    UpdateHotItem(SubItemHitTest(&hti));
}

void CCustomListCtrl::RedrawVisibleItems()
{
    int nTop = GetTopIndex();
    int nBottom = nTop + GetCountPerPage();
    int nCount = GetItemCount();
    if (nBottom >= nCount) nBottom = nCount - 1;
    if (nTop >= 0 && nBottom >= nTop) RedrawItems(nTop, nBottom);
}

BOOL CCustomListCtrl::OnEraseBkgnd(CDC*) { return FALSE; }
void CCustomListCtrl::OnPaint() { Default(); }

static void DrawTransparentIcon(CDC* pDC, CImageList* pImgList, int nImageIndex, CRect rcIcon, COLORREF clrMask)
{
    if (!pImgList || nImageIndex < 0) return;
    IMAGEINFO ii;
    if (!pImgList->GetImageInfo(nImageIndex, &ii)) return;
    int w = CRect(ii.rcImage).Width();
    int h = CRect(ii.rcImage).Height();

    CDC memDC;
    memDC.CreateCompatibleDC(pDC);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(pDC, w, h);
    CBitmap* pOldBmp = memDC.SelectObject(&bmp);
    memDC.FillSolidRect(0, 0, w, h, clrMask);
    pImgList->Draw(&memDC, nImageIndex, CPoint(0, 0), ILD_NORMAL);

    int x = rcIcon.left + (rcIcon.Width() - w) / 2;
    int y = rcIcon.top + (rcIcon.Height() - h) / 2;
    ::TransparentBlt(pDC->GetSafeHdc(), x, y, w, h, memDC.GetSafeHdc(), 0, 0, w, h, clrMask);
    memDC.SelectObject(pOldBmp);
}

void CCustomListCtrl::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVCUSTOMDRAW* pLVCD = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
    *pResult = CDRF_DODEFAULT;

    switch (pLVCD->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        *pResult = CDRF_NOTIFYITEMDRAW;
        break;

    case CDDS_ITEMPREPAINT:
        *pResult = CDRF_NOTIFYSUBITEMDRAW;
        break;

    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
    {
        CDC* pDC = CDC::FromHandle(pLVCD->nmcd.hdc);
        int nItem = (int)pLVCD->nmcd.dwItemSpec;
        int nSubItem = pLVCD->iSubItem;

        CRect rect;
        GetSubItemRect(nItem, nSubItem, LVIR_BOUNDS, rect);

        BOOL bSel = (GetItemState(nItem, LVIS_SELECTED) & LVIS_SELECTED);
        BOOL bHot = (nItem == m_nHotItem);

        COLORREF clrBg;
        if (bSel)          clrBg = COLOR_SEL_BG;
        else if (nItem % 2 == 0)    clrBg = COLOR_LIST_BG;
        else                    clrBg = RGB(183, 221, 238);
        if (bHot && !bSel)      clrBg = RGB(220, 235, 250);

        pDC->FillSolidRect(&rect, clrBg);  // [FIX❹] CBrush廃止

        if (nSubItem == 0)
        {
            if (bSel) { CRect rcH(rect.left + 2, rect.top + 4, rect.left + 16, rect.top + 18); DrawHeart(pDC, rcH, COLOR_HEART); }

            CRect rcIcon;
            if (GetItemRect(nItem, &rcIcon, LVIR_ICON))
            {
                LVITEM lvi = { 0 };
                lvi.mask = LVIF_IMAGE;
                lvi.iItem = nItem;
                GetItem(&lvi);
                CImageList* pImgList = GetImageList(LVSIL_SMALL);
                if (pImgList && lvi.iImage >= 0)
                    DrawTransparentIcon(pDC, pImgList, lvi.iImage, rcIcon, RGB(255, 255, 255));
            }

            if (bHot && !bSel) DrawStar(pDC, rect.left + 10, rect.top + 10, 2, RGB(255, 215, 0));
        }

        CString strText = GetItemText(nItem, nSubItem);
        pDC->SetTextColor(RGB(0, 0, 0));
        pDC->SetBkColor(clrBg);
        pDC->SetBkMode(OPAQUE);

        CRect rcText = rect;
        rcText.left += (nSubItem == 0) ? 36 : 6;
        rcText.DeflateRect(2, 2);

        CFont* pOldFont = pDC->SelectObject(GetFont());
        pDC->DrawText(strText, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        pDC->SelectObject(pOldFont);

        if (nSubItem == GetHeaderCtrl()->GetItemCount() - 1)
            DrawLaceLine(pDC, rect.left + 10, rect.bottom - 1, rect.right - 10, rect.bottom - 1, RGB(200, 180, 220));

        if (GetExtendedStyle() & LVS_EX_GRIDLINES)
        {
            CPen pen(PS_SOLID, 1, RGB(220, 220, 230));
            CPen* pOldP = pDC->SelectObject(&pen);
            pDC->MoveTo(rect.left, rect.bottom - 1); pDC->LineTo(rect.right, rect.bottom - 1);
            pDC->SelectObject(pOldP);
        }

        *pResult = CDRF_SKIPDEFAULT;
        break;
    }
    }
}

// ============================================================================
// CCustomStandardButton
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomStandardButton, CButton)

BEGIN_MESSAGE_MAP(CCustomStandardButton, CButton)
    ON_WM_CTLCOLOR_REFLECT()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_MOUSEMOVE()
    ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
    ON_WM_SETFOCUS()
    ON_WM_KILLFOCUS()
    ON_WM_ENABLE()
END_MESSAGE_MAP()

CCustomStandardButton::CCustomStandardButton()
    : m_bAutoDelete(FALSE), m_bMouseOver(FALSE)
    , m_clrGradStart(RGB(255, 255, 255)), m_clrGradEnd(RGB(255, 255, 255))
    , m_nGradDirection(0), m_bGradEnable(FALSE)
    , m_clrShadow(RGB(0, 0, 0)), m_nShadowDirection(135)
    , m_nShadowDistance(2), m_nShadowBlur(3), m_bShadowEnable(FALSE)
{
    m_brBackground.CreateSolidBrush(COLOR_BUTTON_BG);
}

CCustomStandardButton::~CCustomStandardButton()
{
    if (m_brBackground.GetSafeHandle()) m_brBackground.DeleteObject();
}

// [FIX❺]
void CCustomStandardButton::PostNcDestroy()
{
    CButton::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

void CCustomStandardButton::SetGradation(COLORREF s, COLORREF e, int d, BOOL en)
{
    m_clrGradStart = s; m_clrGradEnd = e;
    m_nGradDirection = d % 360; if (m_nGradDirection < 0)m_nGradDirection += 360;
    m_bGradEnable = en;
    if (GetSafeHwnd())Invalidate(FALSE);
}
void CCustomStandardButton::GetGradation(COLORREF* ps, COLORREF* pe, int* pd, BOOL* pbe) const
{
    if (ps)*ps = m_clrGradStart; if (pe)*pe = m_clrGradEnd; if (pd)*pd = m_nGradDirection; if (pbe)*pbe = m_bGradEnable;
}

void CCustomStandardButton::SetDropShadow(COLORREF c, int d, int dist, int blur, BOOL en)
{
    m_clrShadow = c; m_nShadowDirection = d % 360; if (m_nShadowDirection < 0)m_nShadowDirection += 360;
    m_nShadowDistance = max(0, dist); m_nShadowBlur = max(0, min(20, blur)); m_bShadowEnable = en;
    if (GetSafeHwnd())Invalidate(FALSE);
}
void CCustomStandardButton::GetDropShadow(COLORREF* pc, int* pd, int* pdist, int* pblur, BOOL* pbe) const
{
    if (pc)*pc = m_clrShadow; if (pd)*pd = m_nShadowDirection; if (pdist)*pdist = m_nShadowDistance; if (pblur)*pblur = m_nShadowBlur; if (pbe)*pbe = m_bShadowEnable;
}

void CCustomStandardButton::PreSubclassWindow()
{
    CButton::PreSubclassWindow();
    ModifyStyle(0, BS_OWNERDRAW);
}

HBRUSH CCustomStandardButton::CtlColor(CDC*, UINT) { return (HBRUSH)m_brBackground.GetSafeHandle(); }

void CCustomStandardButton::OnPaint()
{
    CPaintDC dcPaint(this);
    CRect rect;
    GetClientRect(&rect);

    CDC memDC;
    CBitmap memBmp;
    memDC.CreateCompatibleDC(&dcPaint);
    memBmp.CreateCompatibleBitmap(&dcPaint, rect.Width(), rect.Height());
    CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

    BOOL bPushed = (GetState() & BST_PUSHED) != 0;
    BOOL bFocused = (GetFocus() == this);
    BOOL bDisabled = !IsWindowEnabled();

    if (((GetStyle() & BS_TYPEMASK) == BS_CHECKBOX || (GetStyle() & BS_TYPEMASK) == BS_AUTOCHECKBOX) && (GetStyle() & BS_PUSHLIKE))
        if (GetCheck() == BST_CHECKED) bPushed = TRUE;

    COLORREF clrBg = bDisabled ? RGB(200, 200, 200) :
        (bPushed ? COLOR_BUTTON_PUSHED :
            (m_bMouseOver ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG));

    if (m_bGradEnable && !bDisabled)
        DrawGradientBackground(&memDC, rect, m_clrGradStart, m_clrGradEnd, m_nGradDirection);
    else
        memDC.FillSolidRect(&rect, clrBg);

    if (!bDisabled)
    {
        DrawDecorations(&memDC, rect, 0, bPushed);
        if (m_bMouseOver && !bPushed)
        {
            DrawFlower(&memDC, rect.Width() / 2 - 15, rect.top + 10, 6, RGB(255, 200, 220));
            DrawFlower(&memDC, rect.Width() / 2 + 15, rect.top + 10, 6, RGB(255, 200, 220));
            DrawFlower(&memDC, rect.Width() / 2, rect.bottom - 10, 6, RGB(255, 200, 220));
        }
        if (bPushed)
        {
            DrawStar(&memDC, rect.Width() / 2, rect.top + 8, 3, RGB(255, 215, 0));
            DrawStar(&memDC, rect.left + 15, rect.Height() / 2, 2, RGB(255, 240, 150));
            DrawStar(&memDC, rect.right - 15, rect.Height() / 2, 2, RGB(255, 240, 150));
            DrawStar(&memDC, rect.Width() / 2, rect.bottom - 8, 2, RGB(255, 240, 150));
        }
    }

    CPen penLight(PS_SOLID, 2, RGB(255, 255, 255));
    CPen penDark(PS_SOLID, 2, RGB(128, 128, 128));
    CPen* pOldPen;

    if (bPushed)
    {
        pOldPen = memDC.SelectObject(&penDark);
        memDC.MoveTo(rect.left, rect.bottom - 1); memDC.LineTo(rect.left, rect.top); memDC.LineTo(rect.right - 1, rect.top);
        memDC.SelectObject(&penLight);
        memDC.LineTo(rect.right - 1, rect.bottom - 1); memDC.LineTo(rect.left, rect.bottom - 1);
        CRect rcIn = rect; rcIn.DeflateRect(2, 2);
        memDC.SelectObject(&penDark);
        memDC.MoveTo(rcIn.left, rcIn.bottom - 1); memDC.LineTo(rcIn.left, rcIn.top); memDC.LineTo(rcIn.right - 1, rcIn.top);
    }
    else
    {
        pOldPen = memDC.SelectObject(&penLight);
        memDC.MoveTo(rect.left, rect.bottom - 1); memDC.LineTo(rect.left, rect.top); memDC.LineTo(rect.right - 1, rect.top);
        memDC.SelectObject(&penDark);
        memDC.LineTo(rect.right - 1, rect.bottom - 1); memDC.LineTo(rect.left, rect.bottom - 1);
        CRect rcIn = rect; rcIn.DeflateRect(2, 2);
        memDC.SelectObject(&penLight);
        memDC.MoveTo(rcIn.left, rcIn.bottom - 1); memDC.LineTo(rcIn.left, rcIn.top); memDC.LineTo(rcIn.right - 1, rcIn.top);
    }
    memDC.SelectObject(pOldPen);

    if (bFocused && !bDisabled)
    {
        CRect rcF = rect; rcF.DeflateRect(4, 4);
        memDC.DrawFocusRect(&rcF);
    }

    CString str;
    GetWindowText(str);
    CFont* pFont = GetFont();
    CFont* pOldF = memDC.SelectObject(pFont ? pFont : (CFont*)memDC.SelectStockObject(DEFAULT_GUI_FONT));
    DrawSmartText(&memDC, rect, str, bDisabled, bPushed);
    memDC.SelectObject(pOldF);

    dcPaint.BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOldBmp);
    memBmp.DeleteObject();
    memDC.DeleteDC();
}

BOOL CCustomStandardButton::OnEraseBkgnd(CDC*) { return TRUE; }

void CCustomStandardButton::OnMouseMove(UINT nF, CPoint p)
{
    if (!m_bMouseOver)
    {
        TRACKMOUSEEVENT tme = { sizeof(tme),TME_LEAVE,m_hWnd,0 };
        TrackMouseEvent(&tme);
        m_bMouseOver = TRUE;
        Invalidate(FALSE);
    }
    CButton::OnMouseMove(nF, p);
}

LRESULT CCustomStandardButton::OnMouseLeave(WPARAM, LPARAM) { m_bMouseOver = FALSE; Invalidate(FALSE); return 0; }
void CCustomStandardButton::OnSetFocus(CWnd* p) { CButton::OnSetFocus(p);  Invalidate(FALSE); }
void CCustomStandardButton::OnKillFocus(CWnd* p) { CButton::OnKillFocus(p); Invalidate(FALSE); }
void CCustomStandardButton::OnEnable(BOOL b) { CButton::OnEnable(b);    Invalidate(FALSE); }

// ============================================================================
// CCustomSliderCtrl
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomSliderCtrl, CSliderCtrl)

BEGIN_MESSAGE_MAP(CCustomSliderCtrl, CSliderCtrl)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_MESSAGE(WM_MOUSEMOVE, OnMouseMoveMsg)
    ON_MESSAGE(WM_LBUTTONDOWN, OnLButtonDownMsg)
    ON_MESSAGE(WM_LBUTTONUP, OnLButtonUpMsg)
END_MESSAGE_MAP()

CCustomSliderCtrl::CCustomSliderCtrl() : m_bAutoDelete(FALSE), m_nMode(0) {}
CCustomSliderCtrl::~CCustomSliderCtrl() {}

// [FIX❺]
void CCustomSliderCtrl::PostNcDestroy()
{
    CSliderCtrl::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

void CCustomSliderCtrl::SetMode(int m) { if (m_nMode != m) { m_nMode = m; if (GetSafeHwnd())Invalidate(FALSE); } }
void CCustomSliderCtrl::SetPos(int p, BOOL b) { CSliderCtrl::SetPos(p); if (b && GetSafeHwnd()) { Invalidate(FALSE); UpdateWindow(); } }
void CCustomSliderCtrl::PreSubclassWindow() { CSliderCtrl::PreSubclassWindow(); }

void CCustomSliderCtrl::OnPaint()
{
    CPaintDC dcPaint(this);
    CRect r; GetClientRect(&r);

    CDC memDC; CBitmap memBmp;
    memDC.CreateCompatibleDC(&dcPaint);
    memBmp.CreateCompatibleBitmap(&dcPaint, r.Width(), r.Height());
    CBitmap* pOldB = memDC.SelectObject(&memBmp);
    memDC.FillSolidRect(&r, COLOR_DIALOG_BG);
    DrawSlider(&memDC);
    dcPaint.BitBlt(0, 0, r.Width(), r.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOldB);
    memBmp.DeleteObject(); memDC.DeleteDC();
}

BOOL CCustomSliderCtrl::OnEraseBkgnd(CDC*) { return TRUE; }

LRESULT CCustomSliderCtrl::OnMouseMoveMsg(WPARAM w, LPARAM l)
{
    if (m_nMode == 1 || m_nMode == 2)
    {
        int nMin, nMax; GetRange(nMin, nMax);
        int nCenter = (nMin + nMax) / 2;
        int nOldPos = CSliderCtrl::GetPos();
        LRESULT result = Default();
        int nNewPos = CSliderCtrl::GetPos();
        if (nOldPos != nCenter && abs(nNewPos - nCenter) <= 2)
        {
            CSliderCtrl::SetPos(nCenter);
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, nCenter), (LPARAM)m_hWnd);
        }
        Invalidate(FALSE);
        return result;
    }
    LRESULT result = Default(); Invalidate(FALSE); return result;
}

LRESULT CCustomSliderCtrl::OnLButtonDownMsg(WPARAM w, LPARAM l)
{
    LRESULT r = Default();
    if (m_nMode == 1 || m_nMode == 2)
    {
        int nMin, nMax; GetRange(nMin, nMax);
        int nCenter = (nMin + nMax) / 2;
        if (abs(CSliderCtrl::GetPos() - nCenter) <= 2)
        {
            CSliderCtrl::SetPos(nCenter);
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, nCenter), (LPARAM)m_hWnd);
        }
    }
    Invalidate(FALSE); return r;
}

LRESULT CCustomSliderCtrl::OnLButtonUpMsg(WPARAM w, LPARAM l)
{
    LRESULT r = Default();
    if (m_nMode == 1 || m_nMode == 2)
    {
        int nMin, nMax; GetRange(nMin, nMax);
        int nCenter = (nMin + nMax) / 2;
        if (abs(CSliderCtrl::GetPos() - nCenter) <= 2)
        {
            CSliderCtrl::SetPos(nCenter);
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, nCenter), (LPARAM)m_hWnd);
        }
    }
    Invalidate(FALSE); return r;
}

void CCustomSliderCtrl::DrawSlider(CDC* pDC)
{
    CRect r; GetClientRect(&r);
    int nMin, nMax; GetRange(nMin, nMax);
    int nPos = GetPos();
    if (nMax <= nMin) return;

    if (m_nMode == 0) DrawMode0(pDC, r, nMin, nMax, nPos);
    else if (m_nMode == 1) DrawMode1(pDC, r, nMin, nMax, nPos);
    else if (m_nMode == 2) DrawMode2(pDC, r, nMin, nMax, nPos);
    else                 DrawMode1(pDC, r, nMin, nMax, nPos);
}

void CCustomSliderCtrl::DrawMode0(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
    int nRange = nMax - nMin;
    BOOL bVert = (GetStyle() & TBS_VERT);

    if (!bVert)
    {
        int nMarginX = 12, nTrackL = rect.left + nMarginX, nTrackR = rect.right - nMarginX;
        int nTrackW = nTrackR - nTrackL; if (nTrackW <= 0)return;
        int nThumbX = nTrackL + (int)((double)(nPos - nMin) * nTrackW / nRange);
        int nCenterY = rect.Height() / 2, nBottomY = rect.bottom - 8;

        CPoint pts[4] = { {nTrackL,nBottomY},{nTrackL,nBottomY - 2},{nTrackR,rect.top + 4},{nTrackR,nBottomY} };
        CBrush brBack(COLOR_RANGE_SELECTION);
        pDC->SelectObject(&brBack);
        CPen penVine(PS_SOLID, 1, COLOR_VINE_DECO);
        pDC->SelectObject(&penVine);
        pDC->Polygon(pts, 4);

        if (nThumbX > nTrackL)
        {
            CRgn rgnP, rgnL;
            rgnP.CreatePolygonRgn(pts, 4, WINDING);
            rgnL.CreateRectRgn(rect.left, rect.top, nThumbX, rect.bottom);
            rgnP.CombineRgn(&rgnP, &rgnL, RGN_AND);
            CBrush brA(RGB(180, 200, 255));
            pDC->FillRgn(&rgnP, &brA);
        }

        CRect rcNote(nThumbX - 10, nCenterY - 12, nThumbX + 10, nCenterY + 12);
        DrawMusicNote(pDC, rcNote, RGB(138, 43, 226));
        DrawStar(pDC, nThumbX - 12, nCenterY - 14, 2, RGB(255, 215, 0));
        DrawStar(pDC, nThumbX + 12, nCenterY - 14, 2, RGB(255, 215, 0));
    }
    else
    {
        int nMarginY = 12, nTrackT = rect.top + nMarginY, nTrackB = rect.bottom - nMarginY;
        int nTrackH = nTrackB - nTrackT; if (nTrackH <= 0)return;
        int nThumbY = nTrackT + (int)((double)(nPos - nMin) * nTrackH / nRange);
        int nCenterX = rect.Width() / 2;

        CPoint pts[4] = { {nCenterX - 8,nTrackB},{nCenterX - 2,nTrackT},{nCenterX + 2,nTrackT},{nCenterX + 8,nTrackB} };
        CBrush brBack(COLOR_RANGE_SELECTION);
        pDC->SelectObject(&brBack);
        CPen penVine(PS_SOLID, 1, COLOR_VINE_DECO);
        pDC->SelectObject(&penVine);
        pDC->Polygon(pts, 4);

        if (nThumbY < nTrackB)
        {
            CRgn rgnP, rgnB;
            rgnP.CreatePolygonRgn(pts, 4, WINDING);
            rgnB.CreateRectRgn(rect.left, nThumbY, rect.right, rect.bottom);
            rgnP.CombineRgn(&rgnP, &rgnB, RGN_AND);
            CBrush brA(RGB(180, 200, 255));
            pDC->FillRgn(&rgnP, &brA);
        }

        CRect rcNote(nCenterX - 10, nThumbY - 12, nCenterX + 10, nThumbY + 12);
        DrawMusicNote(pDC, rcNote, RGB(138, 43, 226));
        DrawStar(pDC, nCenterX + 14, nThumbY, 2, RGB(255, 215, 0));
    }
}

void CCustomSliderCtrl::DrawMode1(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
    int nRange = nMax - nMin;
    BOOL bVert = (GetStyle() & TBS_VERT);

    if (!bVert)
    {
        int nCenterY = rect.Height() / 2;
        int nTrackL = 12, nTrackR = rect.Width() - 12, nTrackW = nTrackR - nTrackL;
        if (nTrackW <= 0)return;
        int nThumbPos = nTrackL + (int)((double)(nPos - nMin) * nTrackW / nRange);

        CPen penA(PS_SOLID, 5, RGB(200, 150, 255));
        pDC->SelectObject(&penA);
        pDC->MoveTo(nTrackL, nCenterY); pDC->LineTo(nThumbPos, nCenterY);
        CPen penI(PS_SOLID, 3, RGB(220, 220, 230));
        pDC->SelectObject(&penI);
        pDC->LineTo(nTrackR, nCenterY);

        CPen penT(PS_SOLID, 2, RGB(150, 100, 200));
        pDC->SelectObject(&penT);
        for (int i = 0; i <= 10; i++)
        {
            int nTickX = nTrackL + (nTrackW * i / 10);
            int nTickH = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(nTickX, nCenterY - nTickH); pDC->LineTo(nTickX, nCenterY + nTickH);
            if (i % 5 == 0) { CBrush br(RGB(200, 180, 255)); CBrush* pOldBr = pDC->SelectObject(&br); pDC->Ellipse(nTickX - 3, nCenterY - nTickH - 5, nTickX + 3, nCenterY - nTickH + 1); pDC->SelectObject(pOldBr); }
        }

        CRect rcD(nThumbPos - 9, nCenterY - 12, nThumbPos + 9, nCenterY + 12);
        DrawDiamond(pDC, rcD, RGB(200, 180, 255));

        CPen penLight(PS_SOLID, 1, RGB(255, 240, 200));
        pDC->SelectObject(&penLight);
        for (int angle = 0; angle < 360; angle += 45)
        {
            double rad = angle * 3.14159 / 180.0;
            pDC->MoveTo(nThumbPos + (int)(12 * cos(rad)), nCenterY + (int)(12 * sin(rad)));
            pDC->LineTo(nThumbPos + (int)(18 * cos(rad)), nCenterY + (int)(18 * sin(rad)));
        }
    }
    else
    {
        int nCenterX = rect.Width() / 2;
        int nTrackT = 12, nTrackB = rect.Height() - 12, nTrackH = nTrackB - nTrackT;
        if (nTrackH <= 0)return;
        int nThumbPos = nTrackT + (int)((double)(nPos - nMin) * nTrackH / nRange);

        CPen penA(PS_SOLID, 5, RGB(200, 150, 255)); pDC->SelectObject(&penA);
        pDC->MoveTo(nCenterX, nThumbPos); pDC->LineTo(nCenterX, nTrackB);
        CPen penI(PS_SOLID, 3, RGB(220, 220, 230)); pDC->SelectObject(&penI);
        pDC->MoveTo(nCenterX, nTrackT);  pDC->LineTo(nCenterX, nThumbPos);

        CPen penT(PS_SOLID, 2, RGB(150, 100, 200)); pDC->SelectObject(&penT);
        for (int i = 0; i <= 10; i++)
        {
            int nTickY = nTrackT + (nTrackH * i / 10);
            int nTickW = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(nCenterX - nTickW, nTickY); pDC->LineTo(nCenterX + nTickW, nTickY);
            if (i % 5 == 0) { CBrush br(RGB(200, 180, 255)); CBrush* pOldBr = pDC->SelectObject(&br); pDC->Ellipse(nCenterX + nTickW + 1, nTickY - 3, nCenterX + nTickW + 7, nTickY + 3); pDC->SelectObject(pOldBr); }
        }

        CRect rcD(nCenterX - 9, nThumbPos - 12, nCenterX + 9, nThumbPos + 12);
        DrawDiamond(pDC, rcD, RGB(200, 180, 255));
    }
}

void CCustomSliderCtrl::DrawMode2(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
    int nRange = nMax - nMin;
    BOOL bVert = (GetStyle() & TBS_VERT);

    if (!bVert)
    {
        int nCenterY = rect.Height() / 2;
        int nTrackL = 12, nTrackR = rect.Width() - 12, nTrackW = nTrackR - nTrackL;
        if (nTrackW <= 0)return;
        int nThumbPos = nTrackL + (int)((double)(nPos - nMin) * nTrackW / nRange);

        CPen penA(PS_SOLID, 5, RGB(100, 200, 150)); pDC->SelectObject(&penA);
        pDC->MoveTo(nTrackL, nCenterY); pDC->LineTo(nThumbPos, nCenterY);
        CPen penI(PS_SOLID, 3, RGB(220, 220, 230)); pDC->SelectObject(&penI);
        pDC->LineTo(nTrackR, nCenterY);

        CPen penT(PS_SOLID, 2, RGB(80, 160, 120)); pDC->SelectObject(&penT);
        for (int i = 0; i <= 10; i++)
        {
            int nTickX = nTrackL + (nTrackW * i / 10);
            int nTickH = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(nTickX, nCenterY - nTickH); pDC->LineTo(nTickX, nCenterY + nTickH);
            if (i % 5 == 0) { CBrush br(RGB(150, 220, 180)); CBrush* pOldBr = pDC->SelectObject(&br); pDC->Ellipse(nTickX - 3, nCenterY - nTickH - 5, nTickX + 3, nCenterY - nTickH + 1); pDC->SelectObject(pOldBr); }
        }

        CRect rcD(nThumbPos - 9, nCenterY - 12, nThumbPos + 9, nCenterY + 12);
        DrawDiamond(pDC, rcD, RGB(100, 220, 160));

        CPen penLight(PS_SOLID, 1, RGB(200, 255, 220)); pDC->SelectObject(&penLight);
        for (int angle = 0; angle < 360; angle += 45)
        {
            double rad = angle * 3.14159 / 180.0;
            pDC->MoveTo(nThumbPos + (int)(12 * cos(rad)), nCenterY + (int)(12 * sin(rad)));
            pDC->LineTo(nThumbPos + (int)(18 * cos(rad)), nCenterY + (int)(18 * sin(rad)));
        }
    }
    else
    {
        int nCenterX = rect.Width() / 2;
        int nTrackT = 12, nTrackB = rect.Height() - 12, nTrackH = nTrackB - nTrackT;
        if (nTrackH <= 0)return;
        int nThumbPos = nTrackT + (int)((double)(nPos - nMin) * nTrackH / nRange);

        CPen penA(PS_SOLID, 5, RGB(100, 200, 150)); pDC->SelectObject(&penA);
        pDC->MoveTo(nCenterX, nThumbPos); pDC->LineTo(nCenterX, nTrackB);
        CPen penI(PS_SOLID, 3, RGB(220, 220, 230)); pDC->SelectObject(&penI);
        pDC->MoveTo(nCenterX, nTrackT); pDC->LineTo(nCenterX, nThumbPos);

        CPen penT(PS_SOLID, 2, RGB(80, 160, 120)); pDC->SelectObject(&penT);
        for (int i = 0; i <= 10; i++)
        {
            int nTickY = nTrackT + (nTrackH * i / 10);
            int nTickW = (i % 5 == 0) ? 10 : 5;
            pDC->MoveTo(nCenterX - nTickW, nTickY); pDC->LineTo(nCenterX + nTickW, nTickY);
            if (i % 5 == 0) { CBrush br(RGB(150, 220, 180)); CBrush* pOldBr = pDC->SelectObject(&br); pDC->Ellipse(nCenterX + nTickW + 1, nTickY - 3, nCenterX + nTickW + 7, nTickY + 3); pDC->SelectObject(pOldBr); }
        }

        CRect rcD(nCenterX - 9, nThumbPos - 12, nCenterX + 9, nThumbPos + 12);
        DrawDiamond(pDC, rcD, RGB(100, 220, 160));
    }
}

// ============================================================================
// CCustomRangeSliderCtrl
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomRangeSliderCtrl, CSliderCtrl)

BEGIN_MESSAGE_MAP(CCustomRangeSliderCtrl, CSliderCtrl)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
END_MESSAGE_MAP()

CCustomRangeSliderCtrl::CCustomRangeSliderCtrl()
    : m_bAutoDelete(FALSE), m_nMin(0), m_nMax(100), m_nSelMin(0), m_nSelMax(100)
    , m_nDragTarget(0), m_bDragging(FALSE), m_nVisualPos(0), m_nLogicalPos(0)
{
}

CCustomRangeSliderCtrl::~CCustomRangeSliderCtrl() {}

// [FIX❺]
void CCustomRangeSliderCtrl::PostNcDestroy()
{
    CSliderCtrl::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

void CCustomRangeSliderCtrl::PreSubclassWindow()
{
    CSliderCtrl::PreSubclassWindow();
    HMODULE h = LoadLibrary(_T("UxTheme.dll"));
    if (h)
    {
        typedef HRESULT(WINAPI* S)(HWND, LPCWSTR, LPCWSTR);
        S p = (S)GetProcAddress(h, "SetWindowTheme");
        if (p) p(m_hWnd, L"", L"");
        FreeLibrary(h);
    }
    int mn, mx; CSliderCtrl::GetRange(mn, mx);
    m_nMin = mn; m_nMax = mx;
    m_nLogicalPos = m_nVisualPos = CSliderCtrl::GetPos();
}

void CCustomRangeSliderCtrl::SetPos(int p)
{
    p = max(m_nMin, min(m_nMax, p));
    m_nLogicalPos = m_nVisualPos = p;
    CSliderCtrl::SetPos(p);
    if (::IsWindow(m_hWnd)) RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

int CCustomRangeSliderCtrl::GetPos() const { return m_nLogicalPos; }

void CCustomRangeSliderCtrl::SetRange(int mn, int mx, BOOL b)
{
    m_nMin = mn; m_nMax = mx; m_nVisualPos = m_nLogicalPos;
    CSliderCtrl::SetRange(mn, mx, FALSE);
    if (b && ::IsWindow(m_hWnd)) RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CCustomRangeSliderCtrl::SetSelection(int mn, int mx)
{
    m_nSelMin = mn; m_nSelMax = mx;
    if (m_nSelMin > m_nSelMax) { int t = m_nSelMin; m_nSelMin = m_nSelMax; m_nSelMax = t; }
    if (::IsWindow(m_hWnd)) RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CCustomRangeSliderCtrl::GetSelection(int& mn, int& mx) const
{
    mn = max(m_nMin, min(m_nMax, m_nSelMin));
    mx = max(m_nMin, min(m_nMax, m_nSelMax));
}

void CCustomRangeSliderCtrl::OnPaint()
{
    CPaintDC dc(this);
    CRect r; GetClientRect(&r);
    CDC mDC; CBitmap mB;
    mDC.CreateCompatibleDC(&dc);
    mB.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
    CBitmap* pOldB = mDC.SelectObject(&mB);
    mDC.FillSolidRect(&r, COLOR_DIALOG_BG);
    DrawRangeSlider(&mDC);
    dc.BitBlt(0, 0, r.Width(), r.Height(), &mDC, 0, 0, SRCCOPY);
    mDC.SelectObject(pOldB);
    mB.DeleteObject(); mDC.DeleteDC();
}

BOOL CCustomRangeSliderCtrl::OnEraseBkgnd(CDC*) { return TRUE; }

void CCustomRangeSliderCtrl::DrawRangeSlider(CDC* pDC)
{
    CRect r; GetClientRect(&r);
    if (m_nMax <= m_nMin) return;

    int cy = r.Height() / 2;
    int cur = m_bDragging ? m_nVisualPos : m_nLogicalPos;

    int xMin = ValueToPixel(m_nSelMin);
    int xMax = ValueToPixel(m_nSelMax);
    int xPos = ValueToPixel(cur);

    CPen penT(PS_SOLID, 4, RGB(200, 200, 200));
    pDC->SelectObject(&penT);
    pDC->MoveTo(14, cy); pDC->LineTo(r.Width() - 14, cy);

    if (xMax > xMin) pDC->FillSolidRect(CRect(xMin, cy - 4, xMax, cy + 4), COLOR_RANGE_SELECTION);

    CPen penB(PS_SOLID, 1, RGB(0, 0, 0));
    pDC->SelectObject(&penB);

    pDC->FillSolidRect(CRect(xMin - 5, cy - 8, xMin + 5, cy + 8), COLOR_RANGE_SLIDER_THUMB);
    pDC->SelectObject(GetStockObject(NULL_BRUSH));
    pDC->Rectangle(CRect(xMin - 5, cy - 8, xMin + 5, cy + 8));

    pDC->FillSolidRect(CRect(xMax - 5, cy - 8, xMax + 5, cy + 8), COLOR_RANGE_SLIDER_THUMB);
    pDC->Rectangle(CRect(xMax - 5, cy - 8, xMax + 5, cy + 8));

    DrawHeart(pDC, CRect(xPos - 9, cy - 12, xPos + 9, cy + 6), COLOR_SLIDER_THUMB);
}

int CCustomRangeSliderCtrl::ValueToPixel(int v) const
{
    CRect r; GetClientRect(&r);
    int w = r.Width() - 28;
    if (w <= 0 || m_nMax <= m_nMin) return 14;
    int val = max(m_nMin, min(m_nMax, v));
    return 14 + (int)((long long)(val - m_nMin) * w / (m_nMax - m_nMin));
}

int CCustomRangeSliderCtrl::PixelToValue(int x) const
{
    CRect r; GetClientRect(&r);
    int w = r.Width() - 28;
    if (w <= 0 || m_nMax <= m_nMin) return m_nMin;
    int px = max(14, min(r.Width() - 14, x));
    return m_nMin + (int)((double)(px - 14) / w * (m_nMax - m_nMin) + 0.5);
}

int CCustomRangeSliderCtrl::HitTest(CPoint p) const
{
    CRect r; GetClientRect(&r);
    int cy = r.Height() / 2;
    int xM = ValueToPixel(m_nLogicalPos);
    int xMx = ValueToPixel(m_nSelMax);
    int xMn = ValueToPixel(m_nSelMin);
    if (CRect(xM - 10, cy - 14, xM + 10, cy + 14).PtInRect(p))  return 3;
    if (CRect(xMx - 7, cy - 10, xMx + 7, cy + 10).PtInRect(p))  return 2;
    if (CRect(xMn - 7, cy - 10, xMn + 7, cy + 10).PtInRect(p))  return 1;
    return 0;
}

void CCustomRangeSliderCtrl::OnLButtonDown(UINT nF, CPoint p)
{
    SetFocus();
    m_nVisualPos = m_nLogicalPos;
    m_nDragTarget = HitTest(p);

    if (m_nDragTarget == 0)
    {
        m_nVisualPos = PixelToValue(p.x);
        m_nDragTarget = 3;
        CSliderCtrl::SetPos(m_nVisualPos);
        GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, m_nVisualPos), (LPARAM)m_hWnd);
    }
    m_bDragging = TRUE; SetCapture();
    RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CCustomRangeSliderCtrl::OnLButtonUp(UINT nF, CPoint p)
{
    if (m_bDragging)
    {
        m_bDragging = FALSE; ReleaseCapture();
        if (m_nDragTarget == 3)
        {
            m_nLogicalPos = m_nVisualPos;
            CSliderCtrl::SetPos(m_nLogicalPos);
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, m_nLogicalPos), (LPARAM)m_hWnd);
            GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, m_nLogicalPos), (LPARAM)m_hWnd);
        }
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
}

void CCustomRangeSliderCtrl::OnMouseMove(UINT nF, CPoint p)
{
    if (m_bDragging)
    {
        int v = PixelToValue(p.x);
        if (m_nDragTarget == 3) { m_nVisualPos = max(m_nMin, min(m_nMax, v)); CSliderCtrl::SetPos(m_nVisualPos); GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, m_nVisualPos), (LPARAM)m_hWnd); }
        else if (m_nDragTarget == 1) m_nSelMin = min(v, m_nSelMax);
        else if (m_nDragTarget == 2) m_nSelMax = max(v, m_nSelMin);
        RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
}

// ============================================================================
// CCustomCheckBox
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomCheckBox, CButton)

BEGIN_MESSAGE_MAP(CCustomCheckBox, CButton)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
END_MESSAGE_MAP()

CCustomCheckBox::CCustomCheckBox()
    : m_bAutoDelete(FALSE), m_bIsFlatStyle(FALSE), m_bIsPressed(FALSE), m_bIsHot(FALSE), m_bTracking(FALSE), m_nCheck(0)
{
}
CCustomCheckBox::~CCustomCheckBox() {}

// [FIX❺]
void CCustomCheckBox::PostNcDestroy()
{
    CButton::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

int  CCustomCheckBox::GetCheck() { return m_nCheck; }
void CCustomCheckBox::SetCheck(int n) { m_nCheck = n; CButton::SetCheck(n); Invalidate(); }

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

void CCustomCheckBox::SetFont(CFont* pFont, BOOL bRedraw) { CButton::SetFont(pFont, bRedraw); }

void CCustomCheckBox::OnLButtonDown(UINT n, CPoint p) { m_bIsPressed = m_bIsHot = TRUE; SetCapture(); Invalidate(); }

void CCustomCheckBox::OnLButtonUp(UINT n, CPoint p)
{
    if (m_bIsPressed)
    {
        m_bIsPressed = FALSE; ReleaseCapture();
        CRect r; GetClientRect(&r);
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
    if (!m_bTracking) { TRACKMOUSEEVENT t = { sizeof(t),TME_LEAVE,m_hWnd,0 }; TrackMouseEvent(&t); m_bTracking = TRUE; }
    CRect r; GetClientRect(&r);
    BOOL h = r.PtInRect(p);
    if (m_bIsHot != h) { m_bIsHot = h; Invalidate(); }
}

void CCustomCheckBox::OnMouseLeave() { m_bIsHot = m_bTracking = FALSE; Invalidate(); }
void CCustomCheckBox::OnPaint() { CPaintDC dc(this); CRect r; GetClientRect(&r); OnDrawLayer(&dc, r); }
LRESULT CCustomCheckBox::OnPrintClient(WPARAM w, LPARAM) { CDC* pDC = CDC::FromHandle((HDC)w); CRect r; GetClientRect(&r); OnDrawLayer(pDC, r); return 0; }
BOOL CCustomCheckBox::OnEraseBkgnd(CDC*) { return TRUE; }

void CCustomCheckBox::OnDrawLayer(CDC* pDC, CRect rect)
{
    BOOL bC = (m_nCheck == BST_CHECKED);
    BOOL bD = !IsWindowEnabled();
    BOOL bP = m_bIsPressed && m_bIsHot;

    pDC->SelectObject(GetFont() ? GetFont() : (CFont*)pDC->SelectStockObject(DEFAULT_GUI_FONT));

    if (m_bIsFlatStyle)
    {
        BOOL s = bC || bP;
        COLORREF bg = bD ? RGB(200, 200, 200) : (s ? COLOR_BUTTON_PUSHED : (m_bIsHot ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG));
        pDC->FillSolidRect(&rect, bg);
        if (!bD) DrawDecorations(pDC, rect, 0, s);
        pDC->Draw3dRect(&rect, s ? RGB(100, 100, 100) : RGB(255, 255, 255), s ? RGB(255, 255, 255) : RGB(100, 100, 100));
        CString t; GetWindowText(t);
        DrawSmartText(pDC, rect, t, bD, s);
    }
    else
    {
        pDC->FillSolidRect(&rect, COLOR_DIALOG_BG);
        int s = 18, cy = rect.Height() / 2;
        CRect rcB(rect.left, cy - s / 2, rect.left + s, cy + s / 2);
        CPen pen(PS_SOLID, 2, RGB(255, 140, 100));
        CBrush br(RGB(255, 255, 255));
        pDC->SelectObject(&pen); pDC->SelectObject(&br);
        pDC->RoundRect(&rcB, CPoint(5, 5));

        if (bC) { CRect rcH = rcB; rcH.DeflateRect(1, 1); DrawHanamaru(pDC, rcH, RGB(255, 100, 150), RGB(255, 182, 193)); }

        CString t; GetWindowText(t);
        if (!t.IsEmpty()) { CRect rcT = rect; rcT.left = rcB.right + 8; DrawSmartText2(pDC, rcT, t, DT_LEFT | DT_VCENTER, bD, FALSE); }
    }

    if (GetFocus() == this)
    {
        CRect rcF = rect;
        if (!m_bIsFlatStyle) rcF.left += 20; else rcF.DeflateRect(3, 3);
        pDC->DrawFocusRect(&rcF);
    }
}

// ============================================================================
// CCustomGroupBox
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomGroupBox, CButton)

BEGIN_MESSAGE_MAP(CCustomGroupBox, CButton)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCustomGroupBox::CCustomGroupBox() : m_bAutoDelete(FALSE) {}
CCustomGroupBox::~CCustomGroupBox() {}

// [FIX❺]
void CCustomGroupBox::PostNcDestroy()
{
    CButton::PostNcDestroy();
    if (m_bAutoDelete) delete this;
}

void CCustomGroupBox::PreSubclassWindow() { CButton::PreSubclassWindow(); }
void CCustomGroupBox::OnPaint() { CPaintDC dc(this); CRect r; GetClientRect(&r); DrawGroupBox(&dc, r); }
BOOL CCustomGroupBox::OnEraseBkgnd(CDC*) { return TRUE; }

void CCustomGroupBox::DrawGroupBox(CDC* pDC, CRect& rect)
{
    CString t; GetWindowText(t);
    CFont* pOldF = pDC->SelectObject(GetFont());
    CSize s = pDC->GetTextExtent(t);
    int nT = rect.top + s.cy / 2;

    CPen penOuter(PS_SOLID, 2, RGB(255, 140, 180));
    CPen penInner(PS_SOLID, 1, RGB(255, 200, 220));

    pDC->SelectObject(&penOuter);
    pDC->SelectObject(GetStockObject(NULL_BRUSH));
    pDC->MoveTo(rect.left + 1, nT);
    if (s.cx > 0) { pDC->LineTo(rect.left + 6, nT); pDC->MoveTo(rect.left + s.cx + 16, nT); }
    pDC->LineTo(rect.right - 2, nT); pDC->LineTo(rect.right - 2, rect.bottom - 2);
    pDC->LineTo(rect.left + 1, rect.bottom - 2); pDC->LineTo(rect.left + 1, nT);

    pDC->SelectObject(&penInner);
    int offset = 3;
    pDC->MoveTo(rect.left + offset, nT + offset);
    if (s.cx > 0) { pDC->LineTo(rect.left + 6 + offset, nT + offset); pDC->MoveTo(rect.left + s.cx + 16, nT + offset); }
    pDC->LineTo(rect.right - offset, nT + offset); pDC->LineTo(rect.right - offset, rect.bottom - offset);
    pDC->LineTo(rect.left + offset, rect.bottom - offset); pDC->LineTo(rect.left + offset, nT + offset);

    DrawRibbon(pDC, CRect(rect.left + 2, nT - 8, rect.left + 14, nT + 4), RGB(255, 182, 193));
    DrawRibbon(pDC, CRect(rect.right - 14, nT - 8, rect.right - 2, nT + 4), RGB(255, 182, 193));
    DrawRibbon(pDC, CRect(rect.left + 2, rect.bottom - 12, rect.left + 14, rect.bottom), RGB(255, 182, 193));
    DrawRibbon(pDC, CRect(rect.right - 14, rect.bottom - 12, rect.right - 2, rect.bottom), RGB(255, 182, 193));

    if (!t.IsEmpty())
    {
        CRect rcT(rect.left + 8, nT - s.cy / 2, rect.left + 8 + s.cx + 4, nT + s.cy / 2);
        pDC->FillSolidRect(&rcT, COLOR_DIALOG_BG);
        pDC->SetBkMode(TRANSPARENT);
        pDC->SetTextColor(RGB(0, 0, 0));
        pDC->DrawText(t, &rcT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    pDC->SelectObject(pOldF);
}

// ============================================================================
// CCustomDialog
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomDialog, CDialog)

BEGIN_MESSAGE_MAP(CCustomDialog, CDialog)
    ON_WM_CTLCOLOR()
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_MESSAGE(WM_USER + 1000, OnSubclassControls)
END_MESSAGE_MAP()

CCustomDialog::CCustomDialog() { m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG); }
CCustomDialog::CCustomDialog(UINT nIDTemplate, CWnd* pParentWnd)
    : CDialog(nIDTemplate, pParentWnd) {
    m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
}
CCustomDialog::~CCustomDialog() { if (m_brDialog.GetSafeHandle())m_brDialog.DeleteObject(); }

BOOL CCustomDialog::OnInitDialog()
{
    BOOL bResult = CDialog::OnInitDialog();
    PostMessage(WM_USER + 1000, 0, 0);
    return bResult;
}

LRESULT CCustomDialog::OnSubclassControls(WPARAM, LPARAM) { SubclassChildControls(); return 0; }

void CCustomDialog::SubclassChildControls()
{
    HWND hWndChild = ::GetWindow(m_hWnd, GW_CHILD);
    while (hWndChild != NULL)
    {
        if (!::IsWindow(hWndChild)) break;
        if (!(::GetWindowLong(hWndChild, GWL_STYLE) & WS_VISIBLE)) { hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT); continue; }
        CWnd* pWnd = CWnd::FromHandlePermanent(hWndChild);
        if (pWnd != NULL && pWnd != this) { hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT); continue; }

        TCHAR szClassName[256];
        ::GetClassName(hWndChild, szClassName, 256);

        if (_tcsicmp(szClassName, _T("Edit")) == 0) { CCustomEdit* p = new CCustomEdit();            p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        else if (_tcsicmp(szClassName, _T("Static")) == 0) { CCustomStatic* p = new CCustomStatic();        p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        else if (_tcsicmp(szClassName, _T("ListBox")) == 0) { CCustomListBox* p = new CCustomListBox();       p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        else if (_tcsicmp(szClassName, _T("ComboBox")) == 0) { CCustomComboBox* p = new CCustomComboBox();     p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        else if (_tcsicmp(szClassName, WC_LISTVIEW) == 0) { CCustomListCtrl* p = new CCustomListCtrl();     p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        else if (_tcsicmp(szClassName, _T("Button")) == 0)
        {
            LONG lStyle = ::GetWindowLong(hWndChild, GWL_STYLE);
            UINT nType = lStyle & BS_TYPEMASK;
            if (nType == BS_GROUPBOX) { CCustomGroupBox* p = new CCustomGroupBox();         p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
            else if (nType == BS_PUSHBUTTON || nType == BS_DEFPUSHBUTTON || (lStyle & BS_PUSHLIKE)) { CCustomStandardButton* p = new CCustomStandardButton(); p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
            else { CCustomCheckBox* p = new CCustomCheckBox();         p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        }
        else if (_tcsicmp(szClassName, TRACKBAR_CLASS) == 0)
        {
            LONG lStyle = ::GetWindowLong(hWndChild, GWL_STYLE);
            if (lStyle & TBS_ENABLESELRANGE) { CCustomRangeSliderCtrl* p = new CCustomRangeSliderCtrl(); p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
            else { CCustomSliderCtrl* p = new CCustomSliderCtrl();            p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        }

        hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT);
    }
}

HBRUSH CCustomDialog::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    if (nCtlColor == CTLCOLOR_DLG) return (HBRUSH)m_brDialog.GetSafeHandle();

    if (nCtlColor == CTLCOLOR_EDIT)
    {
        CWnd* pParent = pWnd->GetParent();
        if (pParent)
        {
            TCHAR sz[256]; ::GetClassName(pParent->m_hWnd, sz, 256);
            if (_tcsicmp(sz, _T("ComboBox")) == 0)
            {
                pDC->SetBkColor(COLOR_COMBO_BG); pDC->SetTextColor(RGB(0, 0, 0));
                static CBrush brCombo(COLOR_COMBO_BG); return (HBRUSH)brCombo.GetSafeHandle();
            }
        }
        pDC->SetBkColor(COLOR_EDIT_BG); pDC->SetTextColor(RGB(0, 0, 0));
        static CBrush brEdit(COLOR_EDIT_BG); return (HBRUSH)brEdit.GetSafeHandle();
    }

    if (nCtlColor == CTLCOLOR_LISTBOX)
    {
        pDC->SetBkColor(COLOR_COMBO_BG); pDC->SetTextColor(RGB(0, 0, 0));
        static CBrush brCombo(COLOR_COMBO_BG); return (HBRUSH)brCombo.GetSafeHandle();
    }

    if (nCtlColor == CTLCOLOR_STATIC || nCtlColor == CTLCOLOR_BTN)
    {
        pDC->SetBkColor(COLOR_DIALOG_BG); pDC->SetTextColor(RGB(0, 0, 0));
        return (HBRUSH)m_brDialog.GetSafeHandle();
    }

    return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

BOOL CCustomDialog::OnEraseBkgnd(CDC* pDC)
{
    CRect rect; GetClientRect(&rect);
    pDC->FillRect(&rect, &m_brDialog);
    return TRUE;
}

void CCustomDialog::OnPaint() { Default(); }

// ============================================================================
// CCustomBlurDialogBase
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomBlurDialogBase, CCustomDialog)

CCustomBlurDialogBase::CCustomBlurDialogBase() : m_bBlurApplied(FALSE) { m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG); }
CCustomBlurDialogBase::CCustomBlurDialogBase(UINT nIDTemplate, CWnd* pParent) : CCustomDialog(nIDTemplate, pParent), m_bBlurApplied(FALSE) { m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG); }
CCustomBlurDialogBase::~CCustomBlurDialogBase() { if (m_brDialog.GetSafeHandle())m_brDialog.DeleteObject(); }

BOOL CCustomBlurDialogBase::OnInitDialog() { BOOL b = CCustomDialog::OnInitDialog(); ApplyDwmBlur(); return b; }
int  CCustomBlurDialogBase::OnCreate(LPCREATESTRUCT lp) { if (CCustomDialog::OnCreate(lp) == -1)return -1; ApplyDwmBlur(); return 0; }

void CCustomBlurDialogBase::ApplyDwmBlur()
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
    COSVersion os; os.GetVersionString();
    if (os.in.dwMajorVersion == 10 && os.in.dwBuildNumber >= 22000) { EnableDwmAcrylicWin11(m_hWnd); EnableRoundedCorners(m_hWnd); }
    else if (os.in.dwMajorVersion == 10 || os.in.dwMajorVersion >= 6) { EnableDwmBlurBehindWin10(m_hWnd); }
    m_bBlurApplied = TRUE;
}

// ============================================================================
// CCustomDialogEx
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomDialogEx, CDialogEx)

BEGIN_MESSAGE_MAP(CCustomDialogEx, CDialogEx)
    ON_WM_CTLCOLOR()
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_MESSAGE(WM_USER + 1000, OnSubclassControls)
END_MESSAGE_MAP()

CCustomDialogEx::CCustomDialogEx() { m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG); }
CCustomDialogEx::CCustomDialogEx(UINT nIDTemplate, CWnd* pParentWnd)
    : CDialogEx(nIDTemplate, pParentWnd) {
    m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
}
CCustomDialogEx::~CCustomDialogEx() { if (m_brDialog.GetSafeHandle())m_brDialog.DeleteObject(); }

BOOL CCustomDialogEx::OnInitDialog() { BOOL b = CDialogEx::OnInitDialog(); PostMessage(WM_USER + 1000, 0, 0); return b; }
LRESULT CCustomDialogEx::OnSubclassControls(WPARAM, LPARAM) { SubclassChildControls(); return 0; }

void CCustomDialogEx::SubclassChildControls()
{
    HWND hWndChild = ::GetWindow(m_hWnd, GW_CHILD);
    while (hWndChild != NULL)
    {
        if (!::IsWindow(hWndChild)) break;
        if (!(::GetWindowLong(hWndChild, GWL_STYLE) & WS_VISIBLE)) { hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT); continue; }
        CWnd* pWnd = CWnd::FromHandlePermanent(hWndChild);
        if (pWnd != NULL && pWnd != this) { hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT); continue; }

        TCHAR szClassName[256];
        ::GetClassName(hWndChild, szClassName, 256);

        if (_tcsicmp(szClassName, _T("Edit")) == 0) { CCustomEdit* p = new CCustomEdit();            p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        else if (_tcsicmp(szClassName, _T("Static")) == 0) { CCustomStatic* p = new CCustomStatic();        p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        else if (_tcsicmp(szClassName, _T("ListBox")) == 0) { CCustomListBox* p = new CCustomListBox();       p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        else if (_tcsicmp(szClassName, _T("ComboBox")) == 0) { CCustomComboBox* p = new CCustomComboBox();     p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        else if (_tcsicmp(szClassName, WC_LISTVIEW) == 0) { CCustomListCtrl* p = new CCustomListCtrl();     p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        else if (_tcsicmp(szClassName, _T("Button")) == 0)
        {
            LONG lStyle = ::GetWindowLong(hWndChild, GWL_STYLE);
            if ((lStyle & BS_CHECKBOX) || (lStyle & BS_AUTOCHECKBOX))
            {
                if (lStyle & BS_PUSHLIKE) { CCustomStandardButton* p = new CCustomStandardButton(); p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
                else { CCustomCheckBox* p = new CCustomCheckBox();              p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
            }
            else if (lStyle & BS_GROUPBOX) { CCustomGroupBox* p = new CCustomGroupBox();         p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
            else { CCustomStandardButton* p = new CCustomStandardButton(); p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        }
        else if (_tcsicmp(szClassName, TRACKBAR_CLASS) == 0)
        {
            LONG lStyle = ::GetWindowLong(hWndChild, GWL_STYLE);
            if (lStyle & TBS_ENABLESELRANGE) { CCustomRangeSliderCtrl* p = new CCustomRangeSliderCtrl(); p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
            else { CCustomSliderCtrl* p = new CCustomSliderCtrl();            p->EnableAutoDelete(); p->SubclassWindow(hWndChild); }
        }

        hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT);
    }
}

HBRUSH CCustomDialogEx::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    if (nCtlColor == CTLCOLOR_DLG) return (HBRUSH)m_brDialog.GetSafeHandle();

    if (nCtlColor == CTLCOLOR_EDIT)
    {
        CWnd* pParent = pWnd->GetParent();
        if (pParent)
        {
            TCHAR sz[256]; ::GetClassName(pParent->m_hWnd, sz, 256);
            if (_tcsicmp(sz, _T("ComboBox")) == 0)
            {
                pDC->SetBkColor(COLOR_COMBO_BG); pDC->SetTextColor(RGB(0, 0, 0));
                static CBrush brCombo(COLOR_COMBO_BG); return (HBRUSH)brCombo.GetSafeHandle();
            }
        }
        pDC->SetBkColor(COLOR_EDIT_BG); pDC->SetTextColor(RGB(0, 0, 0));
        static CBrush brEdit(COLOR_EDIT_BG); return (HBRUSH)brEdit.GetSafeHandle();
    }

    if (nCtlColor == CTLCOLOR_LISTBOX)
    {
        pDC->SetBkColor(COLOR_COMBO_BG); pDC->SetTextColor(RGB(0, 0, 0));
        static CBrush brCombo(COLOR_COMBO_BG); return (HBRUSH)brCombo.GetSafeHandle();
    }

    if (nCtlColor == CTLCOLOR_STATIC || nCtlColor == CTLCOLOR_BTN)
    {
        pDC->SetBkColor(COLOR_DIALOG_BG); pDC->SetTextColor(RGB(0, 0, 0));
        return (HBRUSH)m_brDialog.GetSafeHandle();
    }

    return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}

BOOL CCustomDialogEx::OnEraseBkgnd(CDC* pDC)
{
    CRect rect; GetClientRect(&rect);
    pDC->FillRect(&rect, &m_brDialog);
    return TRUE;
}

void CCustomDialogEx::OnPaint() { Default(); }

// ============================================================================
// CCustomBlurDialogExBase
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomBlurDialogExBase, CCustomDialogEx)

CCustomBlurDialogExBase::CCustomBlurDialogExBase() : m_bBlurApplied(FALSE) { m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG); }
CCustomBlurDialogExBase::CCustomBlurDialogExBase(UINT nIDTemplate, CWnd* pParent) : CCustomDialogEx(nIDTemplate, pParent), m_bBlurApplied(FALSE) { m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG); }
CCustomBlurDialogExBase::~CCustomBlurDialogExBase() { if (m_brDialog.GetSafeHandle())m_brDialog.DeleteObject(); }

BOOL CCustomBlurDialogExBase::OnInitDialog() { BOOL b = CCustomDialogEx::OnInitDialog(); ApplyDwmBlur(); return b; }
int  CCustomBlurDialogExBase::OnCreate(LPCREATESTRUCT lp) { if (CCustomDialogEx::OnCreate(lp) == -1)return -1; ApplyDwmBlur(); return 0; }

void CCustomBlurDialogExBase::ApplyDwmBlur()
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
    COSVersion os; os.GetVersionString();
    if (os.in.dwMajorVersion == 10 && os.in.dwBuildNumber >= 22000) { EnableDwmAcrylicWin11(m_hWnd); EnableRoundedCorners(m_hWnd); }
    else if (os.in.dwMajorVersion == 10 || os.in.dwMajorVersion >= 6) { EnableDwmBlurBehindWin10(m_hWnd); }
    m_bBlurApplied = TRUE;
}
