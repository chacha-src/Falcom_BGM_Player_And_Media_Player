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
 * @brief グラデーション背景を描画する（グローバル関数）
 * @param pDC デバイスコンテキスト
 * @param rect 描画領域
 * @param colorStart 開始色
 * @param colorEnd 終了色
 * @param nDirection 方向（0-359度、0=下から上、90=左から右）
 */
static void DrawGradientBackground(CDC* pDC, const CRect& rect, COLORREF colorStart, COLORREF colorEnd, int nDirection)
{
	double rad = nDirection * 3.14159265358979323846 / 180.0;

	// 256段階でグラデーション描画
	int steps = 256;

	for (int i = 0; i < steps; i++)
	{
		double ratio = (double)i / (steps - 1);

		// 色を補間
		int r = GetRValue(colorStart) + (int)((GetRValue(colorEnd) - GetRValue(colorStart)) * ratio);
		int g = GetGValue(colorStart) + (int)((GetGValue(colorEnd) - GetGValue(colorStart)) * ratio);
		int b = GetBValue(colorStart) + (int)((GetBValue(colorEnd) - GetBValue(colorStart)) * ratio);

		COLORREF clr = RGB(r, g, b);
		CPen pen(PS_SOLID, 1, clr);
		CPen* pOldPen = pDC->SelectObject(&pen);

		// 矩形の対角線の長さ
		double diagonal = sqrt((double)(rect.Width() * rect.Width() + rect.Height() * rect.Height()));

		// 中心点
		int cx = rect.Width() / 2;
		int cy = rect.Height() / 2;

		// グラデーションラインの位置計算
		double t = ratio - 0.5;
		double dist = t * diagonal;

		// 角度から垂直方向のベクトルを計算
		double perpAngle = rad + 3.14159265358979323846 / 2.0;
		double dx = cos(perpAngle);
		double dy = -sin(perpAngle);

		// グラデーション位置
		double gradX = cos(rad);
		double gradY = -sin(rad);

		// グラデーションラインの中心位置
		int lineX = cx + (int)(gradX * dist);
		int lineY = cy + (int)(gradY * dist);

		// グラデーションラインを描画
		int x1 = lineX + (int)(dx * diagonal);
		int y1 = lineY + (int)(dy * diagonal);
		int x2 = lineX - (int)(dx * diagonal);
		int y2 = lineY - (int)(dy * diagonal);

		pDC->MoveTo(x1, y1);
		pDC->LineTo(x2, y2);

		pDC->SelectObject(pOldPen);
	}
}

/**
 * @brief ドロップシャドウ付きテキストを描画する（グローバル関数）
 * @param pDC デバイスコンテキスト
 * @param rect 描画領域
 * @param strText テキスト
 * @param nFormat 描画フォーマット
 * @param clrText テキスト色
 * @param clrShadow 影の色
 * @param nShadowDirection 影の方向（0-359度）
 * @param nShadowDistance 影の距離
 * @param nShadowBlur 影のぼかし度（0-20）
 * @param bShadowEnable 影を有効にするか
 * @param clrBackground 背景色（アルファブレンド計算用）
 */
static void DrawTextWithShadow(CDC* pDC, const CRect& rect, const CString& strText, UINT nFormat,
	COLORREF clrText, COLORREF clrShadow, int nShadowDirection, int nShadowDistance,
	int nShadowBlur, BOOL bShadowEnable, COLORREF clrBackground)
{
	// ドロップシャドウ描画（方向を修正：0度=右、90度=下、180度=左、270度=上）
	if (bShadowEnable && nShadowDistance > 0)
	{
		// 影の方向ベクトル計算（修正版）
		double rad = nShadowDirection * 3.14159265358979323846 / 180.0;
		int offsetX = (int)(nShadowDistance * cos(rad));
		int offsetY = (int)(nShadowDistance * sin(rad)); // sin の符号を反転

		// ブラー効果（複数の影を重ねて描画）
		for (int blur = nShadowBlur; blur > 0; blur--)
		{
			// ブラーの強度に応じて透明度を変える
			int alpha = 255 / (nShadowBlur + 1) * (nShadowBlur - blur + 1) / nShadowBlur;

			// 影の色（アルファブレンド風）
			int r = (GetRValue(clrShadow) * alpha + GetRValue(clrBackground) * (255 - alpha)) / 255;
			int g = (GetGValue(clrShadow) * alpha + GetGValue(clrBackground) * (255 - alpha)) / 255;
			int b = (GetBValue(clrShadow) * alpha + GetBValue(clrBackground) * (255 - alpha)) / 255;

			pDC->SetTextColor(RGB(r, g, b));

			// ブラー範囲に応じて少しずつずらして描画
			int blurOffset = blur - nShadowBlur / 2;
			CRect rcShadow = rect;
			rcShadow.OffsetRect(offsetX + blurOffset, offsetY + blurOffset);
			pDC->DrawText(strText, rcShadow, nFormat);
		}
	}

	// 本文テキスト描画
	pDC->SetTextColor(clrText);
	CRect rcText = rect; // コピーを作成
	pDC->DrawText(strText, rcText, nFormat);
}

/**
 * @brief グラデーション付きテキストを描画する（グローバル関数）
 * @param pDC デバイスコンテキスト
 * @param rect 描画領域
 * @param strText テキスト
 * @param nFormat 描画フォーマット
 * @param clrStart 開始色
 * @param clrEnd 終了色
 * @param nDirection グラデーション方向（0-359度、0=下から上、90=左から右）
 * @param clrShadow 影の色
 * @param nShadowDirection 影の方向
 * @param nShadowDistance 影の距離
 * @param nShadowBlur 影のぼかし度
 * @param bShadowEnable 影を有効にするか
 * @param clrBackground 背景色
 */
 /**
  * @brief グラデーション付きテキストを描画する
  * 全角度対応、DPIに依存せず均一なグラデーションを適用
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

	// イタリック体の余白
	LOGFONT lf;
	pDC->GetCurrentFont()->GetLogFont(&lf);
	int italicMargin = lf.lfItalic ? (abs(lf.lfHeight) / 2) : 0;

	// グラデーション領域の決定
	CRect rcGradArea = rect;
	if (nFormat & DT_CENTER) {
		rcGradArea.left = rect.left + (rect.Width() - nWidth) / 2;
	}
	else if (nFormat & DT_RIGHT) {
		rcGradArea.left = rect.right - nWidth;
	}
	rcGradArea.right = rcGradArea.left + nWidth + italicMargin;

	// 3. 方向の正規化
	int normalizedDir = nDirection % 360;
	if (normalizedDir < 0) normalizedDir += 360;

	// 4. 方向別のグラデーション描画
	pDC->SetBkMode(TRANSPARENT);

	// 対角線グラデーション（45, 135, 225, 315度）
	if (normalizedDir == 45 || normalizedDir == 135 || normalizedDir == 225 || normalizedDir == 315)
	{
		int diagonal = (int)sqrt((double)(rcGradArea.Width() * rcGradArea.Width() + nHeight * nHeight));
		if (diagonal <= 0) diagonal = 1;

		for (int i = 0; i < diagonal; i++)
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
			rcSlice.right = x + 2;

			CRgn rgn;
			rgn.CreateRectRgnIndirect(&rcSlice);
			pDC->SelectClipRgn(&rgn);

			CRect rcDrawText = rect;
			rcDrawText.right += italicMargin;
			pDC->DrawText(strText, rcDrawText, nFormat);

			pDC->SelectClipRgn(NULL);
		}
	}
	// 水平グラデーション（90-180度: 左→右、270-360度: 右→左）
	else if ((normalizedDir >= 90 && normalizedDir < 180) || (normalizedDir >= 270 && normalizedDir < 360))
	{
		int totalSteps = rcGradArea.Width();
		if (totalSteps <= 0) totalSteps = 1;
		BOOL bLeftToRight = (normalizedDir >= 90 && normalizedDir < 180);

		for (int i = 0; i < totalSteps; i++)
		{
			double ratio = bLeftToRight ? ((double)i / totalSteps) : (1.0 - (double)i / totalSteps);
			int r = GetRValue(clrStart) + (int)((GetRValue(clrEnd) - GetRValue(clrStart)) * ratio);
			int g = GetGValue(clrStart) + (int)((GetGValue(clrEnd) - GetGValue(clrStart)) * ratio);
			int b = GetBValue(clrStart) + (int)((GetBValue(clrEnd) - GetBValue(clrStart)) * ratio);
			pDC->SetTextColor(RGB(r, g, b));

			CRect rcSlice = rect;
			rcSlice.left = rcGradArea.left + i;
			rcSlice.right = rcSlice.left + 1;

			CRgn rgn;
			rgn.CreateRectRgnIndirect(&rcSlice);
			pDC->SelectClipRgn(&rgn);

			CRect rcDrawText = rect;
			rcDrawText.right += italicMargin;
			pDC->DrawText(strText, rcDrawText, nFormat);

			pDC->SelectClipRgn(NULL);
		}
	}
	// 垂直グラデーション（0-90度: 下→上、180-270度: 上→下）
	else
	{
		int totalSteps = nHeight;
		if (totalSteps <= 0) totalSteps = 1;
		BOOL bBottomToTop = (normalizedDir >= 0 && normalizedDir < 90);

		for (int i = 0; i < totalSteps; i++)
		{
			double ratio = bBottomToTop ? ((double)i / totalSteps) : (1.0 - (double)i / totalSteps);
			int r = GetRValue(clrStart) + (int)((GetRValue(clrEnd) - GetRValue(clrStart)) * ratio);
			int g = GetGValue(clrStart) + (int)((GetGValue(clrEnd) - GetGValue(clrStart)) * ratio);
			int b = GetBValue(clrStart) + (int)((GetBValue(clrEnd) - GetBValue(clrStart)) * ratio);
			pDC->SetTextColor(RGB(r, g, b));

			CRect rcSlice = rect;
			rcSlice.top = bBottomToTop ? (rect.bottom - i - 1) : (rect.top + i);
			rcSlice.bottom = rcSlice.top + 1;

			CRgn rgn;
			rgn.CreateRectRgnIndirect(&rcSlice);
			pDC->SelectClipRgn(&rgn);

			CRect rcDrawText = rect;
			rcDrawText.right += italicMargin;
			pDC->DrawText(strText, rcDrawText, nFormat);

			pDC->SelectClipRgn(NULL);
		}
	}
}
/**
 * @brief ハート型の図形を描画する
 */
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

/**
 * @brief 小さな星(キラキラ)を描画する
 */
static void DrawStar(CDC* pDC, int cx, int cy, int size, COLORREF color)
{
	CPen pen(PS_SOLID, 2, color);
	CPen* pOldPen = pDC->SelectObject(&pen);

	// 縦線
	pDC->MoveTo(cx, cy - size);
	pDC->LineTo(cx, cy + size);

	// 横線
	pDC->MoveTo(cx - size, cy);
	pDC->LineTo(cx + size, cy);

	// 斜め線1
	pDC->MoveTo(cx - size * 7 / 10, cy - size * 7 / 10);
	pDC->LineTo(cx + size * 7 / 10, cy + size * 7 / 10);

	// 斜め線2
	pDC->MoveTo(cx + size * 7 / 10, cy - size * 7 / 10);
	pDC->LineTo(cx - size * 7 / 10, cy + size * 7 / 10);

	pDC->SelectObject(pOldPen);
}

/**
 * @brief 音符(♪)を描画する
 */
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

	// 音符の玉（下部）
	CRect rcNote(cx - w / 2, cy + h / 4, cx + w / 2, cy + h / 4 + w);
	pDC->Ellipse(&rcNote);

	// 音符の棒
	pDC->MoveTo(cx + w / 2, cy + h / 4 + w / 2);
	pDC->LineTo(cx + w / 2, cy - h / 2);

	// 音符の旗（上部の装飾）
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

/**
 * @brief ダイヤモンド型の宝石を描画する
 */
static void DrawDiamond(CDC* pDC, CRect rc, COLORREF color)
{
	int cx = rc.CenterPoint().x;
	int cy = rc.CenterPoint().y;
	int w = rc.Width() / 2;
	int h = rc.Height() / 2;

	// ダイヤモンドの形
	CPoint pts[4];
	pts[0] = CPoint(cx, cy - h);     // 上
	pts[1] = CPoint(cx + w, cy);     // 右
	pts[2] = CPoint(cx, cy + h);     // 下
	pts[3] = CPoint(cx - w, cy);     // 左

	// グラデーション風に複数の色で描画
	CBrush brOuter(RGB(200, 200, 255)); // 外側：淡い青
	CPen penOuter(PS_SOLID, 1, RGB(150, 150, 255));
	CBrush* pOldBr = pDC->SelectObject(&brOuter);
	CPen* pOldPen = pDC->SelectObject(&penOuter);
	pDC->Polygon(pts, 4);

	// 内側のダイヤモンド（より明るく）
	CPoint ptsInner[4];
	ptsInner[0] = CPoint(cx, cy - h * 6 / 10);
	ptsInner[1] = CPoint(cx + w * 6 / 10, cy);
	ptsInner[2] = CPoint(cx, cy + h * 6 / 10);
	ptsInner[3] = CPoint(cx - w * 6 / 10, cy);

	CBrush brInner(color);
	pDC->SelectObject(&brInner);
	pDC->Polygon(ptsInner, 4);

	// 中心のハイライト
	CBrush brHighlight(RGB(255, 255, 255));
	pDC->SelectObject(&brHighlight);
	CRect rcHighlight(cx - 2, cy - 3, cx + 2, cy + 1);
	pDC->Ellipse(&rcHighlight);

	pDC->SelectObject(pOldBr);
	pDC->SelectObject(pOldPen);

	// キラキラ光線
	CPen penLight(PS_SOLID, 1, RGB(255, 255, 200));
	pDC->SelectObject(&penLight);

	// 4方向に光線
	pDC->MoveTo(cx, cy - h - 3);
	pDC->LineTo(cx, cy - h - 6);
	pDC->MoveTo(cx, cy + h + 3);
	pDC->LineTo(cx, cy + h + 6);
	pDC->MoveTo(cx - w - 3, cy);
	pDC->LineTo(cx - w - 6, cy);
	pDC->MoveTo(cx + w + 3, cy);
	pDC->LineTo(cx + w + 6, cy);

	pDC->SelectObject(pOldPen);
}

/**
 * @brief 王冠を描画する
 */
static void DrawCrown(CDC* pDC, int cx, int cy, int size, COLORREF color)
{
	CBrush br(color);
	CPen pen(PS_SOLID, 1, RGB(255, 215, 0));
	CBrush* pOldBr = pDC->SelectObject(&br);
	CPen* pOldPen = pDC->SelectObject(&pen);

	// 王冠の形
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

	// 宝石を3つ配置
	CBrush brJewel(RGB(255, 100, 100));
	pDC->SelectObject(&brJewel);

	pDC->Ellipse(cx - 2, cy - size - 2, cx + 2, cy - size + 2);
	pDC->Ellipse(cx - size * 2 / 3 - 2, cy - size / 2 - 2, cx - size * 2 / 3 + 2, cy - size / 2 + 2);
	pDC->Ellipse(cx + size * 2 / 3 - 2, cy - size / 2 - 2, cx + size * 2 / 3 + 2, cy - size / 2 + 2);

	pDC->SelectObject(pOldBr);
	pDC->SelectObject(pOldPen);
}

/**
 * @brief レース模様風の線を描画する
 */
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

/**
 * @brief リボンを描画する
 */
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

/**
 * @brief 大きなリボン装飾を描画する
 */
static void DrawBigRibbon(CDC* pDC, int cx, int cy, int size, COLORREF color)
{
	CBrush br(color);
	CPen pen(PS_SOLID, 2, RGB(255, 140, 180));
	CBrush* pOldBr = pDC->SelectObject(&br);
	CPen* pOldPen = pDC->SelectObject(&pen);

	// 中央の結び目
	CRect rcCenter(cx - size / 2, cy - size / 3, cx + size / 2, cy + size / 3);
	pDC->RoundRect(&rcCenter, CPoint(size / 4, size / 4));

	// 左のリボン部分
	CPoint ptsLeft[4];
	ptsLeft[0] = CPoint(cx - size / 2, cy);
	ptsLeft[1] = CPoint(cx - size, cy - size / 2);
	ptsLeft[2] = CPoint(cx - size * 9 / 10, cy);
	ptsLeft[3] = CPoint(cx - size, cy + size / 2);
	pDC->Polygon(ptsLeft, 4);

	// 右のリボン部分
	CPoint ptsRight[4];
	ptsRight[0] = CPoint(cx + size / 2, cy);
	ptsRight[1] = CPoint(cx + size, cy - size / 2);
	ptsRight[2] = CPoint(cx + size * 9 / 10, cy);
	ptsRight[3] = CPoint(cx + size, cy + size / 2);
	pDC->Polygon(ptsRight, 4);

	// キラキラ
	CBrush brGold(RGB(255, 215, 0));
	pDC->SelectObject(&brGold);
	pDC->Ellipse(cx - 3, cy - 3, cx + 3, cy + 3);

	pDC->SelectObject(pOldBr);
	pDC->SelectObject(pOldPen);
}

/**
 * @brief 小さな花を描画する
 */
static void DrawFlower(CDC* pDC, int cx, int cy, int size, COLORREF color)
{
	CBrush br(color);
	CPen pen(PS_SOLID, 1, color);
	CBrush* pOldBr = pDC->SelectObject(&br);
	CPen* pOldPen = pDC->SelectObject(&pen);

	// 5枚の花びら
	for (int i = 0; i < 5; i++)
	{
		double angle = i * 2.0 * 3.14159 / 5.0;
		int px = cx + (int)(size * 0.6 * cos(angle));
		int py = cy + (int)(size * 0.6 * sin(angle));
		pDC->Ellipse(px - size / 3, py - size / 3, px + size / 3, py + size / 3);
	}

	// 中心
	CBrush brCenter(RGB(255, 255, 100));
	pDC->SelectObject(&brCenter);
	pDC->Ellipse(cx - size / 4, cy - size / 4, cx + size / 4, cy + size / 4);

	pDC->SelectObject(pOldBr);
	pDC->SelectObject(pOldPen);
}

/**
 * @brief 思い切り可愛い花丸を描画する ✿✿✿
 */
static void DrawHanamaru(CDC* pDC, CRect rc, COLORREF colorCenter, COLORREF colorPetal)
{
	int cx = rc.CenterPoint().x;
	int cy = rc.CenterPoint().y;
	int radius = min(rc.Width(), rc.Height()) / 2 - 2;

	if (radius < 3) return;

	// 外側の大きな花びら (8枚)
	CBrush brPetal(colorPetal);
	CPen penPetal(PS_SOLID, 1, RGB(255, 140, 180)); // ピンクの縁取り
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

	// 中間の花びら(少し濃いピンク)
	CBrush brMidPetal(RGB(255, 150, 180));
	pDC->SelectObject(&brMidPetal);

	for (int i = 0; i < numPetals; i++)
	{
		double angle = angleStep * i + angleStep / 2.0; // 間に配置
		int px = cx + (int)(radius * 0.45 * cos(angle));
		int py = cy + (int)(radius * 0.45 * sin(angle));
		int petalSize = radius / 4;

		CRect rcPetal(px - petalSize, py - petalSize, px + petalSize, py + petalSize);
		pDC->Ellipse(&rcPetal);
	}

	// 中心の大きな丸(グラデーション風に2重)
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

	// 中心のさらに小さな丸(オレンジで花の芯らしく)
	CBrush brOrange(RGB(255, 140, 80));
	CPen penOrange(PS_SOLID, 1, RGB(255, 100, 50));
	pDC->SelectObject(&brOrange);
	pDC->SelectObject(&penOrange);

	int smallRadius = radius / 6;
	CRect rcSmall(cx - smallRadius, cy - smallRadius, cx + smallRadius, cy + smallRadius);
	pDC->Ellipse(&rcSmall);

	pDC->SelectObject(pOldBr);
	pDC->SelectObject(pOldPen);

	// キラキラ星を四隅に配置（ピンクで統一）
	DrawStar(pDC, cx - radius * 8 / 10, cy - radius * 8 / 10, 2, RGB(255, 140, 180));
	DrawStar(pDC, cx + radius * 8 / 10, cy - radius * 8 / 10, 2, RGB(255, 140, 180));
	DrawStar(pDC, cx - radius * 8 / 10, cy + radius * 8 / 10, 2, RGB(255, 140, 180));
	DrawStar(pDC, cx + radius * 8 / 10, cy + radius * 8 / 10, 2, RGB(255, 140, 180));

	// 上下左右にも小さな星（ピンク）
	DrawStar(pDC, cx, cy - radius * 9 / 10, 1, RGB(255, 180, 200));
	DrawStar(pDC, cx, cy + radius * 9 / 10, 1, RGB(255, 180, 200));
	DrawStar(pDC, cx - radius * 9 / 10, cy, 1, RGB(255, 180, 200));
	DrawStar(pDC, cx + radius * 9 / 10, cy, 1, RGB(255, 180, 200));
}

/**
 * @brief ボタン等の角の装飾
 */
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

	if (bPatternA) {
		corners.push_back({ rect.left + offset, rect.top + offset, 1, 1 });
		corners.push_back({ rect.right - 1 + offset, rect.bottom - 1 + offset, -1, -1 });
	}
	else {
		corners.push_back({ rect.right - 1 + offset, rect.top + offset, -1, 1 });
		corners.push_back({ rect.left + offset, rect.bottom - 1 + offset, 1, -1 });
	}

	for (const auto& c : corners)
	{
		CPoint pts[4];
		pts[0] = CPoint(c.x, c.y + (12 * c.dy));
		pts[1] = CPoint(c.x + (4 * c.dx), c.y + (4 * c.dy));
		pts[2] = CPoint(c.x + (4 * c.dx), c.y + (4 * c.dy));
		pts[3] = CPoint(c.x + (12 * c.dx), c.y);
		pDC->PolyBezier(pts, 4);

		int r = 2;
		int fx = c.x + (4 * c.dx);
		int fy = c.y + (4 * c.dy);

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

/**
 * @brief グラデーション背景を描画（グローバル関数）
 * @param pDC 描画先DC
 * @param rect 描画範囲
 * @param colorStart 開始色
 * @param colorEnd 終了色
 * @param nDirection 方向（0-359度、0=下から上、90=左から右）
 */

 /**
  * @brief テキストを自動縮小しながら描画する(中央揃え)
  */
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
	targetHeight = max(8, targetHeight - 2);
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
		}
	}

	pDC->SelectObject(pOldFont);
	fontSmall.DeleteObject();
}

/**
 * @brief テキストを自動縮小しながら描画する(フォーマット指定版)
 */
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
	pDC->SelectObject(pOldFont);
	fontFinal.DeleteObject();
}

// ============================================================================
// CCustomEdit - カスタムエディットコントロール(お姫様仕様)
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

CCustomEdit::CCustomEdit()
	: m_bHasFocus(FALSE)
{
	m_brBackground.CreateSolidBrush(COLOR_EDIT_BG);
}

CCustomEdit::~CCustomEdit()
{
	if (m_fontBold.GetSafeHandle())
		m_fontBold.DeleteObject();
	if (m_brBackground.GetSafeHandle())
		m_brBackground.DeleteObject();
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

			if (m_fontBold.GetSafeHandle())
				m_fontBold.DeleteObject();

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

void CCustomEdit::OnPaint()
{
	Default();
}

BOOL CCustomEdit::OnEraseBkgnd(CDC* pDC)
{
	return FALSE;
}

void CCustomEdit::OnNcPaint()
{
	CWindowDC dc(this);
	CRect rect;
	GetWindowRect(&rect);
	rect.OffsetRect(-rect.left, -rect.top);

	// 角丸の枠線
	CPen pen(PS_SOLID, 2, m_bHasFocus ? RGB(255, 140, 180) : RGB(255, 182, 193));
	CPen* pOldPen = dc.SelectObject(&pen);
	dc.SelectStockObject(NULL_BRUSH);

	dc.RoundRect(&rect, CPoint(6, 6));

	dc.SelectObject(pOldPen);

	// フォーカス時にキラキラ星
	if (m_bHasFocus)
	{
		DrawStar(&dc, rect.right - 8, rect.top + 8, 3, RGB(255, 215, 0));
		DrawStar(&dc, rect.left + 8, rect.top + 8, 2, RGB(255, 240, 150));
		DrawStar(&dc, rect.right - 8, rect.bottom - 8, 2, RGB(255, 240, 150));
	}

	// 左右に小さなリボン装飾
	CRect rcRibbonL(rect.left + 2, rect.CenterPoint().y - 3, rect.left + 8, rect.CenterPoint().y + 3);
	DrawRibbon(&dc, rcRibbonL, RGB(255, 200, 220));

	CRect rcRibbonR(rect.right - 8, rect.CenterPoint().y - 3, rect.right - 2, rect.CenterPoint().y + 3);
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
// CCustomStatic - カスタムスタティックテキスト
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomStatic, CStatic)

BEGIN_MESSAGE_MAP(CCustomStatic, CStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCustomStatic::CCustomStatic()
	: m_clrGradStart(RGB(255, 255, 255))
	, m_clrGradEnd(RGB(255, 255, 255))
	, m_nGradDirection(0)
	, m_bGradEnable(FALSE)
	, m_clrShadow(RGB(0, 0, 0))
	, m_nShadowDirection(135)
	, m_nShadowDistance(2)
	, m_nShadowBlur(3)
	, m_bShadowEnable(FALSE)
	, m_bPreferWideMode(FALSE)
{
}

CCustomStatic::~CCustomStatic()
{
	if (m_font.GetSafeHandle())
		m_font.DeleteObject();
}

// 0,45,90,135,180,225,270,315 が設定できる角度 0が下から上、時計回り。
void CCustomStatic::SetGradation(COLORREF colorStart, COLORREF colorEnd, int nDirection, BOOL bEnable)
{
	m_clrGradStart = colorStart;
	m_clrGradEnd = colorEnd;
	m_nGradDirection = nDirection % 360; // 0-359に正規化
	if (m_nGradDirection < 0) m_nGradDirection += 360;
	m_bGradEnable = bEnable;

	if (GetSafeHwnd())
		Invalidate();
}

void CCustomStatic::GetGradation(COLORREF* pColorStart, COLORREF* pColorEnd, int* pDirection, BOOL* pbEnable) const
{
	if (pColorStart) *pColorStart = m_clrGradStart;
	if (pColorEnd) *pColorEnd = m_clrGradEnd;
	if (pDirection) *pDirection = m_nGradDirection;
	if (pbEnable) *pbEnable = m_bGradEnable;
}

void CCustomStatic::SetDropShadow(COLORREF color, int nDirection, int nDistance, int nBlur, BOOL bEnable)
{
	m_clrShadow = color;
	m_nShadowDirection = nDirection % 360;
	if (m_nShadowDirection < 0) m_nShadowDirection += 360;
	m_nShadowDistance = max(0, nDistance);
	m_nShadowBlur = max(0, min(20, nBlur)); // 0-20の範囲に制限
	m_bShadowEnable = bEnable;

	if (GetSafeHwnd())
		Invalidate();
}

void CCustomStatic::GetDropShadow(COLORREF* pColor, int* pDirection, int* pDistance, int* pBlur, BOOL* pbEnable) const
{
	if (pColor) *pColor = m_clrShadow;
	if (pDirection) *pDirection = m_nShadowDirection;
	if (pDistance) *pDistance = m_nShadowDistance;
	if (pBlur) *pBlur = m_nShadowBlur;
	if (pbEnable) *pbEnable = m_bShadowEnable;
}

void CCustomStatic::SetPreferWideMode(BOOL bPreferWide)
{
	m_bPreferWideMode = bPreferWide;
	if (GetSafeHwnd())
		Invalidate();
}

BOOL CCustomStatic::GetPreferWideMode() const
{
	return m_bPreferWideMode;
}

void CCustomStatic::SetFont(CFont* pFont, BOOL bRedraw)
{
	if (pFont)
	{
		LOGFONT lf;
		pFont->GetLogFont(&lf);

		if (m_font.GetSafeHandle())
			m_font.DeleteObject();

		m_font.CreateFontIndirect(&lf);
		CStatic::SetFont(&m_font, bRedraw);
	}
}

void CCustomStatic::PreSubclassWindow()
{
	CStatic::PreSubclassWindow();

	CWnd* pParent = GetParent();
	if (pParent)
	{
		CFont* pParentFont = pParent->GetFont();
		if (pParentFont)
			SetFont(pParentFont, FALSE);
	}
}

void CCustomStatic::OnPaint()
{
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(&rect);

	dc.FillSolidRect(&rect, COLOR_DIALOG_BG);

	CString strText;
	GetWindowText(strText);
	if (strText.IsEmpty()) return;

	CFont* pBaseFont = GetFont();
	CFont* pOldFont = dc.SelectObject(pBaseFont);
	dc.SetBkMode(TRANSPARENT);

	// 自動縮小ロジック
	CSize szText = dc.GetTextExtent(strText);
	CFont fontScaled;

	if (szText.cx > rect.Width())
	{
		LOGFONT lf;
		pBaseFont->GetLogFont(&lf);
		int nTargetHeight = abs(lf.lfHeight);

		while (nTargetHeight > 6 && szText.cx > rect.Width())
		{
			nTargetHeight--;
			lf.lfHeight = -nTargetHeight;
			if (fontScaled.GetSafeHandle()) fontScaled.DeleteObject();
			fontScaled.CreateFontIndirect(&lf);
			dc.SelectObject(&fontScaled);
			szText = dc.GetTextExtent(strText);
		}
	}

	// フォーマット決定
	DWORD dwStyle = GetStyle();
	UINT nFormat = DT_VCENTER | DT_SINGLELINE;
	if (dwStyle & SS_CENTER) nFormat |= DT_CENTER;
	else if (dwStyle & SS_RIGHT) nFormat |= DT_RIGHT;
	else nFormat |= DT_LEFT;

	// 描画実行
	if (m_bGradEnable)
	{
		DrawTextWithGradient(&dc, rect, strText, nFormat,
			m_clrGradStart, m_clrGradEnd, m_nGradDirection,
			m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
			m_bShadowEnable, COLOR_DIALOG_BG, szText.cx);
	}
	else
	{
		DrawTextWithShadow(&dc, rect, strText, nFormat, RGB(0, 0, 0),
			m_clrShadow, m_nShadowDirection, m_nShadowDistance, m_nShadowBlur,
			m_bShadowEnable, COLOR_DIALOG_BG);
	}

	if (fontScaled.GetSafeHandle()) fontScaled.DeleteObject();
	dc.SelectObject(pOldFont);
}

BOOL CCustomStatic::OnEraseBkgnd(CDC* pDC)
{
	CRect rect;
	GetClientRect(&rect);
	pDC->FillSolidRect(&rect, COLOR_DIALOG_BG);
	return TRUE;
}

// ============================================================================
// CCustomListBox - カスタムリストボックス(お姫様仕様)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomListBox, CListBox)

BEGIN_MESSAGE_MAP(CCustomListBox, CListBox)
	ON_WM_CTLCOLOR_REFLECT()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCustomListBox::CCustomListBox()
{
	m_brBackground.CreateSolidBrush(COLOR_LIST_BG);
}

CCustomListBox::~CCustomListBox()
{
	if (m_brBackground.GetSafeHandle())
		m_brBackground.DeleteObject();
}

void CCustomListBox::PreSubclassWindow()
{
	CListBox::PreSubclassWindow();
	ModifyStyle(0, LBS_OWNERDRAWFIXED | LBS_HASSTRINGS);
}

HBRUSH CCustomListBox::CtlColor(CDC* pDC, UINT nCtlColor)
{
	pDC->SetBkColor(COLOR_LIST_BG);
	pDC->SetTextColor(RGB(0, 0, 0));
	return (HBRUSH)m_brBackground.GetSafeHandle();
}

void CCustomListBox::OnPaint()
{
	Default();
}

BOOL CCustomListBox::OnEraseBkgnd(CDC* pDC)
{
	return FALSE;
}

void CCustomListBox::DrawItem(LPDRAWITEMSTRUCT lp)
{
	if (lp->itemID == (UINT)-1) return;

	CDC* pDC = CDC::FromHandle(lp->hDC);
	CRect rect = lp->rcItem;

	// ストライプ（奇数/偶数で色を変える）
	COLORREF clrBg;
	if (lp->itemState & ODS_SELECTED)
		clrBg = COLOR_SEL_BG;
	else if (lp->itemID % 2 == 0)
		clrBg = COLOR_LIST_BG;
	else
		clrBg = RGB(183, 221, 238); // 少し濃いストライプ

	pDC->FillSolidRect(&rect, clrBg);

	// 左端に小さなアイコン（花、星、ハート、リボンを順番に）
	int iconType = lp->itemID % 4;
	int iconSize = 8;
	int iconX = rect.left + 5;
	int iconY = rect.top + (rect.Height() - iconSize) / 2;

	switch (iconType)
	{
	case 0: // 花
		DrawFlower(pDC, iconX + iconSize / 2, iconY + iconSize / 2, iconSize / 2, RGB(255, 200, 220));
		break;
	case 1: // 星
		DrawStar(pDC, iconX + iconSize / 2, iconY + iconSize / 2, iconSize / 3, RGB(255, 215, 0));
		break;
	case 2: // ハート
	{
		CRect rcHeart(iconX, iconY, iconX + iconSize, iconY + iconSize);
		DrawHeart(pDC, rcHeart, COLOR_HEART);
		break;
	}
	case 3: // リボン
	{
		CRect rcRibbon(iconX, iconY, iconX + iconSize, iconY + iconSize);
		DrawRibbon(pDC, rcRibbon, RGB(255, 182, 193));
		break;
	}
	}

	// 選択時にさらにキラキラ
	if (lp->itemState & ODS_SELECTED)
	{
		DrawStar(pDC, rect.right - 12, rect.top + rect.Height() / 2, 3, RGB(255, 215, 0));
	}

	// テキスト描画
	CString strText;
	GetText(lp->itemID, strText);
	CRect rcText = rect;
	rcText.left += 20; // アイコン分のスペース

	pDC->SetBkMode(TRANSPARENT);
	DrawSmartText(pDC, rcText, strText, FALSE, FALSE);

	// アイテム間にレース模様の区切り線
	if (lp->itemID < (UINT)(GetCount() - 1))
	{
		DrawLaceLine(pDC, rect.left + 15, rect.bottom - 1, rect.right - 15, rect.bottom - 1, RGB(200, 180, 220));
	}
}

void CCustomListBox::MeasureItem(LPMEASUREITEMSTRUCT lp)
{
	lp->itemHeight = 24; // 高さを増やして華やかに
}

// ============================================================================
// CCustomComboBox - カスタムコンボボックス(お姫様仕様・完全版)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomComboBox, CComboBox)

BEGIN_MESSAGE_MAP(CCustomComboBox, CComboBox)
	ON_WM_CTLCOLOR_REFLECT()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_CONTROL_REFLECT(CBN_DROPDOWN, &CCustomComboBox::OnDropdown)
END_MESSAGE_MAP()

CCustomComboBox::CCustomComboBox()
	: m_clrLabelText(RGB(240, 240, 255))    // デフォルト：白っぽい色
	, m_clrLabelBg(RGB(80, 60, 120))        // デフォルト：濃い紫
{
	m_brBackground.CreateSolidBrush(COLOR_COMBO_BG);
}

CCustomComboBox::~CCustomComboBox()
{
	if (m_brBackground.GetSafeHandle())
		m_brBackground.DeleteObject();
}

int CCustomComboBox::AddString(LPCTSTR lpszString, BOOL bDisabled)
{
	int nIndex = CComboBox::AddString(lpszString);

	if (nIndex >= 0)
	{
		// リストのサイズを調整
		if (nIndex >= (int)m_vDisabledItems.size())
			m_vDisabledItems.resize(nIndex + 1, FALSE);

		m_vDisabledItems[nIndex] = bDisabled;

		// 選択可能な項目の場合のみ配列に追加
		if (!bDisabled)
		{
			m_vSelectableIndices.push_back(nIndex);
		}

		// デバッグ出力
		TRACE(_T("AddString: index=%d, disabled=%d, selectableCount=%d, text=%s\n"),
			nIndex, bDisabled, (int)m_vSelectableIndices.size(), lpszString);
	}

	return nIndex;
}

int CCustomComboBox::GetCurSel() const
{
	// 実際に選択されている項目番号を取得
	int nPhysical = CComboBox::GetCurSel();
	if (nPhysical < 0)
		return -1;

	// 選択可能な項目の番号に変換（配列で検索）
	for (int i = 0; i < (int)m_vSelectableIndices.size(); i++)
	{
		if (m_vSelectableIndices[i] == nPhysical)
		{
			TRACE(_T("GetCurSel: physical=%d -> logical=%d\n"), nPhysical, i);
			return i; // 選択可能な項目としての番号（0, 1, 2...）
		}
	}

	// 選択不可の項目が選ばれている場合（通常は発生しない）
	TRACE(_T("GetCurSel: physical=%d is DISABLED, returning -1\n"), nPhysical);
	return -1;
}

int CCustomComboBox::SetCurSel(int nLogicalIndex)
{
	// 選択可能な項目の番号を実際の項目番号に変換
	if (nLogicalIndex < 0)
	{
		TRACE(_T("SetCurSel: logical=-1 -> physical=-1 (clear selection)\n"));
		return CComboBox::SetCurSel(-1);
	}

	// 範囲外の場合は最後の有効な項目を選択
	if (nLogicalIndex >= (int)m_vSelectableIndices.size())
	{
		if (m_vSelectableIndices.empty())
		{
			TRACE(_T("SetCurSel: no selectable items\n"));
			return CB_ERR;
		}

		// 最後の有効な項目を選択
		nLogicalIndex = (int)m_vSelectableIndices.size() - 1;
		TRACE(_T("SetCurSel: out of range, using last item logical=%d\n"), nLogicalIndex);
	}

	// 実際の項目番号を取得
	int nPhysical = m_vSelectableIndices[nLogicalIndex];

	TRACE(_T("SetCurSel: logical=%d -> physical=%d\n"), nLogicalIndex, nPhysical);
	return CComboBox::SetCurSel(nPhysical);
}

void CCustomComboBox::SetLabelColor(COLORREF clrText, COLORREF clrBackground)
{
	m_clrLabelText = clrText;
	m_clrLabelBg = clrBackground;

	if (GetSafeHwnd())
		Invalidate();
}

void CCustomComboBox::GetLabelColor(COLORREF* pClrText, COLORREF* pClrBackground) const
{
	if (pClrText) *pClrText = m_clrLabelText;
	if (pClrBackground) *pClrBackground = m_clrLabelBg;
}

int CCustomComboBox::LogicalToPhysical(int nLogical) const
{
	// 選択可能な項目の番号（0,1,2...）を実際の項目番号に変換
	if (nLogical < 0 || nLogical >= (int)m_vSelectableIndices.size())
		return -1;

	return m_vSelectableIndices[nLogical];
}

int CCustomComboBox::PhysicalToLogical(int nPhysical) const
{
	// 実際の項目番号を選択可能な項目の番号に変換
	for (int i = 0; i < (int)m_vSelectableIndices.size(); i++)
	{
		if (m_vSelectableIndices[i] == nPhysical)
			return i;
	}

	return -1;
}

void CCustomComboBox::PreSubclassWindow()
{
	CComboBox::PreSubclassWindow();

	// 既存のスタイルを確認してOWNERDRAWを強制設定
	DWORD dwStyle = GetStyle();

	// CBS_SIMPLEやCBS_DROPDOWNなどを保持しながらOWNERDRAWを追加
	dwStyle &= ~(CBS_OWNERDRAWVARIABLE); // 可変サイズは削除
	dwStyle |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS; // 固定サイズで文字列保持

	ModifyStyle(0, CBS_OWNERDRAWFIXED | CBS_HASSTRINGS);

	// 念のため直接スタイルを設定
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

BOOL CCustomComboBox::OnEraseBkgnd(CDC* pDC)
{
	return TRUE;
}

void CCustomComboBox::OnPaint()
{
	CPaintDC dcPaint(this);
	CRect rect;
	GetClientRect(&rect);

	// ダブルバッファリングでちらつき防止
	CDC memDC;
	CBitmap memBmp;
	memDC.CreateCompatibleDC(&dcPaint);
	memBmp.CreateCompatibleBitmap(&dcPaint, rect.Width(), rect.Height());
	CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

	// 背景塗りつぶし
	memDC.FillSolidRect(&rect, COLOR_COMBO_BG);

	// 角丸の枠線（より丸く）
	CPen penFrame(PS_SOLID, 2, COLOR_VINE_DECO);
	CPen* pOldPen = memDC.SelectObject(&penFrame);
	memDC.SelectStockObject(NULL_BRUSH);
	memDC.RoundRect(&rect, CPoint(10, 10));

	// ドロップダウンボタン部分を華やかなリボン型に
	int nBtnWidth = GetSystemMetrics(SM_CXVSCROLL);
	CRect rcBtn(rect.right - nBtnWidth - 4, rect.top + 4, rect.right - 4, rect.bottom - 4);

	// リボン型の背景
	CBrush brBtn(RGB(255, 200, 220));
	memDC.SelectObject(&brBtn);
	memDC.RoundRect(&rcBtn, CPoint(6, 6));

	// ドロップダウンに3つの小さなハート♡♡♡
	int nHeartSize = 6;
	int nSpacing = 2;
	int nStartX = rcBtn.left + (rcBtn.Width() - (nHeartSize * 3 + nSpacing * 2)) / 2;
	int nCenterY = rcBtn.Height() / 2 + rcBtn.top;

	for (int i = 0; i < 3; i++)
	{
		CRect rcHeart(
			nStartX + i * (nHeartSize + nSpacing),
			nCenterY - nHeartSize / 2,
			nStartX + i * (nHeartSize + nSpacing) + nHeartSize,
			nCenterY + nHeartSize / 2
		);
		COLORREF clrHeart = (i == 1) ? COLOR_HEART : RGB(255, 182, 193);
		DrawHeart(&memDC, rcHeart, clrHeart);
	}

	// キラキラ星を追加
	DrawStar(&memDC, rect.right - 8, rect.top + 8, 3, RGB(255, 215, 0));

	// 選択中のテキスト（実際の項目番号を取得）
	int nPhysicalSel = CComboBox::GetCurSel(); // 基底クラスのGetCurSelを直接呼ぶ
	CString strText;
	if (nPhysicalSel != CB_ERR)
		GetLBText(nPhysicalSel, strText);

	memDC.SetTextColor(RGB(0, 0, 0));
	CFont* pOldFont = memDC.SelectObject(GetFont());

	CRect rcText = rect;
	rcText.left += 12;
	rcText.right = rcBtn.left - 4;

	// 選択項目には王冠マーク（ラベル項目でない場合のみ）
	BOOL bIsLabel = (nPhysicalSel >= 0 && nPhysicalSel < (int)m_vDisabledItems.size() && m_vDisabledItems[nPhysicalSel]);
	if (nPhysicalSel != CB_ERR && !bIsLabel)
	{
		int crownSize = (rcText.Height() - 8) / 2;
		DrawCrown(&memDC, rcText.left + crownSize, rcText.Height() / 2, crownSize, RGB(255, 215, 0));
		rcText.left += crownSize * 2 + 4;
	}

	memDC.DrawText(strText, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

	memDC.SelectObject(pOldPen);
	memDC.SelectObject(pOldFont);

	// メモリDCから画面へ転送
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

	// ラベル項目（選択不可）かどうかをチェック
	BOOL bDisabled = FALSE;
	if (lp->itemID < (UINT)m_vDisabledItems.size())
	{
		bDisabled = m_vDisabledItems[lp->itemID];
	}

	// ラベル項目の場合は選択状態を無視
	BOOL bSel = FALSE;
	if (!bDisabled)
	{
		bSel = (lp->itemState & ODS_SELECTED);
	}

	// デバッグ出力（最初の呼び出しのみ）
	static BOOL bFirstCall = TRUE;
	if (bFirstCall)
	{
		TRACE(_T("DrawItem called! itemID=%d, disabled=%d, vectorSize=%d\n"),
			lp->itemID, bDisabled, (int)m_vDisabledItems.size());
		bFirstCall = FALSE;
	}

	// 背景
	COLORREF clrBg;
	if (bDisabled)
	{
		// ラベル項目は常に特別な背景色（濃い紫、選択状態でも変化なし）
		clrBg = m_clrLabelBg;
		TRACE(_T("DrawItem: itemID=%d is LABEL, using dark purple bg\n"), lp->itemID);
	}
	else if (bSel)
	{
		clrBg = COLOR_SEL_BG;
	}
	else if (lp->itemID % 2 == 0)
	{
		clrBg = COLOR_COMBO_BG;
	}
	else
	{
		clrBg = RGB(255, 232, 220); // ストライプ色
	}

	pDC->FillSolidRect(&rect, clrBg);

	// ラベル項目の場合はアイコンを描画しない、それ以外は各アイテムに小さなアイコン
	if (!bDisabled)
	{
		int iconType = lp->itemID % 4;
		int iconSize = 8;
		int iconX = rect.left + 6;
		int iconY = rect.top + (rect.Height() - iconSize) / 2;

		switch (iconType)
		{
		case 0:
			DrawFlower(pDC, iconX + iconSize / 2, iconY + iconSize / 2, iconSize / 2, RGB(255, 200, 220));
			break;
		case 1:
			DrawStar(pDC, iconX + iconSize / 2, iconY + iconSize / 2, iconSize / 3, RGB(255, 215, 0));
			break;
		case 2:
		{
			CRect rcHeart(iconX, iconY, iconX + iconSize, iconY + iconSize);
			DrawHeart(pDC, rcHeart, COLOR_HEART);
			break;
		}
		case 3:
		{
			CRect rcRibbon(iconX, iconY, iconX + iconSize, iconY + iconSize);
			DrawRibbon(pDC, rcRibbon, RGB(255, 182, 193));
			break;
		}
		}
	}

	// テキスト
	CString strText;
	GetLBText(lp->itemID, strText);

	// テキスト色とフォント
	CFont* pOldFont = NULL;
	CFont fontCustom;

	CFont* pFont = GetFont();
	LOGFONT lf;
	pFont->GetLogFont(&lf);

	if (bDisabled)
	{
		// ラベル項目は太字イタリックにして目立たせる
		pDC->SetTextColor(m_clrLabelText);
		lf.lfWeight = FW_BOLD; // 太字
		lf.lfItalic = TRUE;    // イタリック
		fontCustom.CreateFontIndirect(&lf);
		pOldFont = pDC->SelectObject(&fontCustom);
	}
	else
	{
		// 通常項目は太字
		pDC->SetTextColor(RGB(0, 0, 0));
		lf.lfWeight = FW_BOLD; // 太字
		fontCustom.CreateFontIndirect(&lf);
		pOldFont = pDC->SelectObject(&fontCustom);
	}

	pDC->SetBkMode(TRANSPARENT);

	CRect rcText = rect;
	if (!bDisabled)
		rcText.left += 20; // アイコン分のスペース
	else
		rcText.left += 4; // ラベルは左寄せ（より左に）

	pDC->DrawText(strText, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

	// フォントを元に戻す
	if (pOldFont)
	{
		pDC->SelectObject(pOldFont);
		fontCustom.DeleteObject();
	}

	// 選択項目には王冠（ラベルには表示しない）
	if (bSel && !bDisabled)
	{
		int crownSize = 6;
		DrawCrown(pDC, rect.right - crownSize - 8, rect.top + rect.Height() / 2, crownSize, RGB(255, 215, 0));
	}

	// ラベル項目の場合は上下に太めの区切り線
	if (bDisabled)
	{
		CPen penSeparator(PS_SOLID, 2, RGB(200, 200, 240)); // 白っぽい線
		CPen* pOldPen = pDC->SelectObject(&penSeparator);

		// 上側の線（最初の項目以外）
		if (lp->itemID > 0)
		{
			pDC->MoveTo(rect.left + 2, rect.top);
			pDC->LineTo(rect.right - 2, rect.top);
		}

		// 下側の線（常に描画）
		pDC->MoveTo(rect.left + 2, rect.bottom - 1);
		pDC->LineTo(rect.right - 2, rect.bottom - 1);

		pDC->SelectObject(pOldPen);
	}
	// 通常項目のレース模様の区切り線
	else if (lp->itemID < (UINT)(GetCount() - 1))
	{
		// 次の項目がラベルでない場合のみ区切り線を描画
		BOOL bNextIsLabel = FALSE;
		if (lp->itemID + 1 < (UINT)m_vDisabledItems.size())
		{
			bNextIsLabel = m_vDisabledItems[lp->itemID + 1];
		}

		if (!bNextIsLabel)
		{
			DrawLaceLine(pDC, rect.left + 15, rect.bottom - 1, rect.right - 15, rect.bottom - 1, RGB(200, 180, 220));
		}
	}
}

void CCustomComboBox::MeasureItem(LPMEASUREITEMSTRUCT lp)
{
	lp->itemHeight = 28; // 縦を広めに
}

void CCustomComboBox::OnDropdown()
{
	UpdateDropDownWidth();
}

BOOL CCustomComboBox::OnCommand(WPARAM wParam, LPARAM lParam)
{
	WORD wNotifyCode = HIWORD(wParam);

	// CBN_SELCHANGE または CBN_SELENDOK の場合
	if (wNotifyCode == CBN_SELCHANGE || wNotifyCode == CBN_SELENDOK)
	{
		// 選択された項目がラベル（選択不可）の場合、スキップする
		int nCurSel = CComboBox::GetCurSel();

		if (nCurSel >= 0)
		{
			// ラベル項目かチェック
			BOOL bDisabled = (nCurSel < (int)m_vDisabledItems.size() && m_vDisabledItems[nCurSel]);

			if (bDisabled)
			{
				TRACE(_T("OnCommand: Label item clicked (physical=%d), keeping dropdown open\n"), nCurSel);

				// ラベル項目が選択されたので、次の選択可能な項目を探す
				int nCount = CComboBox::GetCount();
				int nOldSel = nCurSel;

				// 下方向に検索
				for (int i = nCurSel + 1; i < nCount; i++)
				{
					BOOL bDisabledItem = (i < (int)m_vDisabledItems.size() && m_vDisabledItems[i]);
					if (!bDisabledItem)
					{
						CComboBox::SetCurSel(i);
						// ドロップダウンは開いたまま（閉じない）
						// 親に通知は送らない（ラベルをクリックしても何も起きない）
						return TRUE;
					}
				}

				// 上方向に検索
				for (int i = nCurSel - 1; i >= 0; i--)
				{
					BOOL bDisabledItem = (i < (int)m_vDisabledItems.size() && m_vDisabledItems[i]);
					if (!bDisabledItem)
					{
						CComboBox::SetCurSel(i);
						// ドロップダウンは開いたまま（閉じない）
						// 親に通知は送らない
						return TRUE;
					}
				}

				// 選択可能な項目が見つからない場合
				CComboBox::SetCurSel(-1);
				return TRUE;
			}
		}
	}

	// 通常の項目の場合は親に通知を送る
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

	nMaxWidth += GetSystemMetrics(SM_CXVSCROLL) + 40; // アイコン+王冠分のスペース

	CRect rect;
	GetWindowRect(&rect);
	SetDroppedWidth(max(nMaxWidth, rect.Width()));

	dc.SelectObject(pOldFont);
}

// ============================================================================
// CCustomListCtrl - カスタムリストコントロール(お姫様仕様・ちらつき完全防止)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomListCtrl, CListCtrl)

BEGIN_MESSAGE_MAP(CCustomListCtrl, CListCtrl)
	ON_WM_CTLCOLOR_REFLECT()
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnCustomDraw)
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCustomListCtrl::CCustomListCtrl()
	: m_nHotItem(-1)
{
	m_brBackground.CreateSolidBrush(COLOR_LIST_BG);
}

CCustomListCtrl::~CCustomListCtrl()
{
	if (m_brBackground.GetSafeHandle())
		m_brBackground.DeleteObject();
}

void CCustomListCtrl::PreSubclassWindow()
{
	CListCtrlA::PreSubclassWindow();
	SetBkColor(COLOR_LIST_BG);
	SetTextBkColor(COLOR_LIST_BG);
	SetTextColor(RGB(0, 0, 0));

	// ダブルバッファリング有効化（ちらつき完全防止）
	SetExtendedStyle(GetExtendedStyle() | LVS_EX_DOUBLEBUFFER);
}

HBRUSH CCustomListCtrl::CtlColor(CDC* pDC, UINT nCtlColor)
{
	pDC->SetBkColor(COLOR_LIST_BG);
	pDC->SetTextColor(RGB(0, 0, 0));
	return (HBRUSH)m_brBackground.GetSafeHandle();
}

void CCustomListCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	LVHITTESTINFO hti;
	hti.pt = point;
	int nItem = SubItemHitTest(&hti);

	if (m_nHotItem != nItem)
	{
		int nOldHot = m_nHotItem;
		m_nHotItem = nItem;

		// 最小限の再描画でちらつき防止
		if (nOldHot >= 0)
			RedrawItems(nOldHot, nOldHot);

		if (m_nHotItem >= 0)
			RedrawItems(m_nHotItem, m_nHotItem);

		UpdateWindow();
	}

	TRACKMOUSEEVENT tme;
	tme.cbSize = sizeof(TRACKMOUSEEVENT);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	TrackMouseEvent(&tme);

	CListCtrl::OnMouseMove(nFlags, point);
}

void CCustomListCtrl::OnMouseLeave()
{
	if (m_nHotItem >= 0)
	{
		int nOldHot = m_nHotItem;
		m_nHotItem = -1;
		RedrawItems(nOldHot, nOldHot);
		UpdateWindow();
	}

	CListCtrl::OnMouseLeave();
}

BOOL CCustomListCtrl::OnEraseBkgnd(CDC* pDC)
{
	return FALSE; // ダブルバッファリングに任せる
}

void CCustomListCtrl::OnPaint()
{
	Default(); // ダブルバッファリングされたデフォルト描画
}

void DrawTransparentIcon(CDC* pDC, CImageList* pImgList, int nImageIndex, CRect rcIcon, COLORREF clrMask)
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
	::TransparentBlt(pDC->GetSafeHdc(), x, y, w, h,
		memDC.GetSafeHdc(), 0, 0, w, h, clrMask);

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

		// ストライプ背景（奇数/偶数）
		COLORREF clrBg;
		if (bSel)
			clrBg = COLOR_SEL_BG;
		else if (nItem % 2 == 0)
			clrBg = COLOR_LIST_BG;
		else
			clrBg = RGB(183, 221, 238); // ストライプ色

		// ホバー時は少し明るく
		if (bHot && !bSel)
		{
			clrBg = RGB(220, 235, 250);
		}

		// 背景を完全にクリア（文字欠落防止の重要ポイント）
		CBrush brBg(clrBg);
		pDC->FillRect(&rect, &brBg);

		// サブアイテム0の処理
		if (nSubItem == 0)
		{
			// アイコン描画
			CRect rcIcon;
			if (GetItemRect(nItem, &rcIcon, LVIR_ICON))
			{
				LVITEM lvi = { 0 };
				lvi.mask = LVIF_IMAGE;
				lvi.iItem = nItem;
				GetItem(&lvi);

				CImageList* pImgList = GetImageList(LVSIL_SMALL);
				if (pImgList && lvi.iImage >= 0)
				{
					DrawTransparentIcon(pDC, pImgList, lvi.iImage, rcIcon, RGB(255, 255, 255));
				}
			}

			// 選択時のハート
			if (bSel)
			{
				CRect rcHeart(rect.left + 2, rect.top + 4, rect.left + 16, rect.top + 18);
				DrawHeart(pDC, rcHeart, COLOR_HEART);
			}

			// ホバー時に小さなキラキラ
			if (bHot && !bSel)
			{
				DrawStar(pDC, rect.left + 10, rect.top + 10, 2, RGB(255, 215, 0));
			}
		}

		// テキスト描画（背景色を明示的に設定）
		CString strText = GetItemText(nItem, nSubItem);
		pDC->SetTextColor(RGB(0, 0, 0));
		pDC->SetBkColor(clrBg);  // ★重要: 背景色を設定
		pDC->SetBkMode(OPAQUE);

		CRect rcText = rect;
		rcText.left += (nSubItem == 0) ? 36 : 6;
		rcText.DeflateRect(2, 2);

		CFont* pFont = GetFont();
		CFont* pOldFont = pDC->SelectObject(pFont);

		pDC->DrawText(strText, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

		pDC->SelectObject(pOldFont);

		// レース模様の区切り線
		if (nSubItem == GetHeaderCtrl()->GetItemCount() - 1) // 最後のサブアイテムのみ
		{
			DrawLaceLine(pDC, rect.left + 10, rect.bottom - 1, rect.right - 10, rect.bottom - 1, RGB(200, 180, 220));
		}

		// グリッド線（オプション）
		if (GetExtendedStyle() & LVS_EX_GRIDLINES)
		{
			CPen pen(PS_SOLID, 1, RGB(220, 220, 230));
			CPen* pOldP = pDC->SelectObject(&pen);
			pDC->MoveTo(rect.left, rect.bottom - 1);
			pDC->LineTo(rect.right, rect.bottom - 1);
			pDC->SelectObject(pOldP);
		}

		*pResult = CDRF_SKIPDEFAULT;
		break;
	}
	}
}

// ============================================================================
// CCustomStandardButton - カスタム標準ボタン(お姫様仕様)
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
	: m_bMouseOver(FALSE)
	, m_clrGradStart(RGB(255, 255, 255))
	, m_clrGradEnd(RGB(255, 255, 255))
	, m_nGradDirection(0)
	, m_bGradEnable(FALSE)
	, m_clrShadow(RGB(0, 0, 0))
	, m_nShadowDirection(135)
	, m_nShadowDistance(2)
	, m_nShadowBlur(3)
	, m_bShadowEnable(FALSE)
{
	m_brBackground.CreateSolidBrush(COLOR_BUTTON_BG);
}

CCustomStandardButton::~CCustomStandardButton()
{
	if (m_brBackground.GetSafeHandle())
		m_brBackground.DeleteObject();
}

void CCustomStandardButton::SetGradation(COLORREF colorStart, COLORREF colorEnd, int nDirection, BOOL bEnable)
{
	m_clrGradStart = colorStart;
	m_clrGradEnd = colorEnd;
	m_nGradDirection = nDirection % 360;
	if (m_nGradDirection < 0) m_nGradDirection += 360;
	m_bGradEnable = bEnable;

	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CCustomStandardButton::GetGradation(COLORREF* pColorStart, COLORREF* pColorEnd, int* pDirection, BOOL* pbEnable) const
{
	if (pColorStart) *pColorStart = m_clrGradStart;
	if (pColorEnd) *pColorEnd = m_clrGradEnd;
	if (pDirection) *pDirection = m_nGradDirection;
	if (pbEnable) *pbEnable = m_bGradEnable;
}

void CCustomStandardButton::SetDropShadow(COLORREF color, int nDirection, int nDistance, int nBlur, BOOL bEnable)
{
	m_clrShadow = color;
	m_nShadowDirection = nDirection % 360;
	if (m_nShadowDirection < 0) m_nShadowDirection += 360;
	m_nShadowDistance = max(0, nDistance);
	m_nShadowBlur = max(0, min(20, nBlur));
	m_bShadowEnable = bEnable;

	if (GetSafeHwnd())
		Invalidate(FALSE);
}

void CCustomStandardButton::GetDropShadow(COLORREF* pColor, int* pDirection, int* pDistance, int* pBlur, BOOL* pbEnable) const
{
	if (pColor) *pColor = m_clrShadow;
	if (pDirection) *pDirection = m_nShadowDirection;
	if (pDistance) *pDistance = m_nShadowDistance;
	if (pBlur) *pBlur = m_nShadowBlur;
	if (pbEnable) *pbEnable = m_bShadowEnable;
}

void CCustomStandardButton::PreSubclassWindow()
{
	CButton::PreSubclassWindow();
	ModifyStyle(0, BS_OWNERDRAW);
}

HBRUSH CCustomStandardButton::CtlColor(CDC* pDC, UINT nCtlColor)
{
	return (HBRUSH)m_brBackground.GetSafeHandle();
}

void CCustomStandardButton::OnPaint()
{
	CPaintDC dcPaint(this);
	CRect rect;
	GetClientRect(&rect);

	// ダブルバッファリングでちらつき完全防止
	CDC memDC;
	CBitmap memBmp;
	memDC.CreateCompatibleDC(&dcPaint);
	memBmp.CreateCompatibleBitmap(&dcPaint, rect.Width(), rect.Height());
	CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

	BOOL bPushed = (GetState() & BST_PUSHED) != 0;
	BOOL bFocused = (GetFocus() == this);
	BOOL bDisabled = !IsWindowEnabled();

	if (((GetStyle() & BS_TYPEMASK) == BS_CHECKBOX ||
		(GetStyle() & BS_TYPEMASK) == BS_AUTOCHECKBOX) &&
		(GetStyle() & BS_PUSHLIKE))
	{
		if (GetCheck() == BST_CHECKED)
			bPushed = TRUE;
	}

	COLORREF clrBg = bDisabled ? RGB(200, 200, 200) :
		(bPushed ? COLOR_BUTTON_PUSHED :
			(m_bMouseOver ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG));

	// グラデーション背景
	if (m_bGradEnable && !bDisabled)
	{
		DrawGradientBackground(&memDC, rect, m_clrGradStart, m_clrGradEnd, m_nGradDirection);
	}
	else
	{
		memDC.FillSolidRect(&rect, clrBg);
	}

	// 華やかな装飾（花びらとキラキラで控えめに）
	if (!bDisabled)
	{
		DrawDecorations(&memDC, rect, 0, bPushed);

		// ホバー時に花びらマークが浮かぶ
		if (m_bMouseOver && !bPushed)
		{
			int flowerSize = 6;
			DrawFlower(&memDC, rect.Width() / 2 - 15, rect.top + 10, flowerSize, RGB(255, 200, 220));
			DrawFlower(&memDC, rect.Width() / 2 + 15, rect.top + 10, flowerSize, RGB(255, 200, 220));
			DrawFlower(&memDC, rect.Width() / 2, rect.bottom - 10, flowerSize, RGB(255, 200, 220));
		}

		// 押下時にキラキラエフェクト
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
		memDC.MoveTo(rect.left, rect.bottom - 1);
		memDC.LineTo(rect.left, rect.top);
		memDC.LineTo(rect.right - 1, rect.top);

		memDC.SelectObject(&penLight);
		memDC.LineTo(rect.right - 1, rect.bottom - 1);
		memDC.LineTo(rect.left, rect.bottom - 1);

		CRect rcIn = rect;
		rcIn.DeflateRect(2, 2);
		memDC.SelectObject(&penDark);
		memDC.MoveTo(rcIn.left, rcIn.bottom - 1);
		memDC.LineTo(rcIn.left, rcIn.top);
		memDC.LineTo(rcIn.right - 1, rcIn.top);
	}
	else
	{
		pOldPen = memDC.SelectObject(&penLight);
		memDC.MoveTo(rect.left, rect.bottom - 1);
		memDC.LineTo(rect.left, rect.top);
		memDC.LineTo(rect.right - 1, rect.top);

		memDC.SelectObject(&penDark);
		memDC.LineTo(rect.right - 1, rect.bottom - 1);
		memDC.LineTo(rect.left, rect.bottom - 1);

		CRect rcIn = rect;
		rcIn.DeflateRect(2, 2);
		memDC.SelectObject(&penLight);
		memDC.MoveTo(rcIn.left, rcIn.bottom - 1);
		memDC.LineTo(rcIn.left, rcIn.top);
		memDC.LineTo(rcIn.right - 1, rcIn.top);
	}

	memDC.SelectObject(pOldPen);

	if (bFocused && !bDisabled)
	{
		CRect rcF = rect;
		rcF.DeflateRect(4, 4);
		memDC.DrawFocusRect(&rcF);
	}

	CString str;
	GetWindowText(str);
	CFont* pFont = GetFont();
	CFont* pOldF = memDC.SelectObject(pFont ? pFont : (CFont*)memDC.SelectStockObject(DEFAULT_GUI_FONT));

	DrawSmartText(&memDC, rect, str, bDisabled, bPushed);
	memDC.SelectObject(pOldF);

	// メモリDCから画面へ一気に転送（ちらつき防止）
	dcPaint.BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);

	memDC.SelectObject(pOldBmp);
	memBmp.DeleteObject();
	memDC.DeleteDC();
}

BOOL CCustomStandardButton::OnEraseBkgnd(CDC* pDC)
{
	return TRUE; // ちらつき防止
}

void CCustomStandardButton::OnMouseMove(UINT nF, CPoint p)
{
	if (!m_bMouseOver)
	{
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		TrackMouseEvent(&tme);
		m_bMouseOver = TRUE;
		Invalidate(FALSE);
	}
	CButton::OnMouseMove(nF, p);
}

LRESULT CCustomStandardButton::OnMouseLeave(WPARAM w, LPARAM l)
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
// CCustomSliderCtrl - カスタムスライダー(お姫様仕様・ちらつき完全防止)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomSliderCtrl, CSliderCtrl)

BEGIN_MESSAGE_MAP(CCustomSliderCtrl, CSliderCtrl)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_MOUSEMOVE, OnMouseMoveMsg)
	ON_MESSAGE(WM_LBUTTONDOWN, OnLButtonDownMsg)
	ON_MESSAGE(WM_LBUTTONUP, OnLButtonUpMsg)
END_MESSAGE_MAP()

CCustomSliderCtrl::CCustomSliderCtrl()
	: m_nMode(0)
{
}

CCustomSliderCtrl::~CCustomSliderCtrl()
{
}

void CCustomSliderCtrl::SetMode(int m)
{
	if (m_nMode != m)
	{
		m_nMode = m;
		if (GetSafeHwnd())
			Invalidate(FALSE); // ちらつき防止
	}
}

void CCustomSliderCtrl::SetPos(int p, BOOL b)
{
	CSliderCtrl::SetPos(p);
	if (b && GetSafeHwnd())
	{
		// 必要な部分のみ再描画
		Invalidate(FALSE);
		UpdateWindow();
	}
}

void CCustomSliderCtrl::PreSubclassWindow()
{
	CSliderCtrl::PreSubclassWindow();
}

void CCustomSliderCtrl::OnPaint()
{
	CPaintDC dcPaint(this);
	CRect r;
	GetClientRect(&r);

	// ダブルバッファリングでちらつき完全防止
	CDC memDC;
	CBitmap memBmp;
	memDC.CreateCompatibleDC(&dcPaint);
	memBmp.CreateCompatibleBitmap(&dcPaint, r.Width(), r.Height());
	CBitmap* pOldB = memDC.SelectObject(&memBmp);

	memDC.FillSolidRect(&r, COLOR_DIALOG_BG);

	DrawSlider(&memDC);

	// メモリDCから画面へ一気に転送
	dcPaint.BitBlt(0, 0, r.Width(), r.Height(), &memDC, 0, 0, SRCCOPY);

	memDC.SelectObject(pOldB);
	memBmp.DeleteObject();
	memDC.DeleteDC();
}

BOOL CCustomSliderCtrl::OnEraseBkgnd(CDC* p)
{
	return TRUE; // ちらつき防止
}

LRESULT CCustomSliderCtrl::OnMouseMoveMsg(WPARAM w, LPARAM l)
{
	// Mode 1, 2 のみ中央点スナップ機能を有効化
	if (m_nMode == 1 || m_nMode == 2)
	{
		int nMin, nMax;
		GetRange(nMin, nMax);
		int nCenter = (nMin + nMax) / 2;
		int nOldPos = CSliderCtrl::GetPos(); // Default()呼び出し前の位置

		LRESULT result = Default();

		int nNewPos = CSliderCtrl::GetPos(); // Default()呼び出し後の位置

		// 中央点の±2以内なら中央点にスナップ（ただし、中央点から離れる方向は除く）
		int nSnapRange = 2;
		if (abs(nNewPos - nCenter) <= nSnapRange)
		{
			// 中央点にいた場合は、離れる方向へは自由に移動させる
			if (nOldPos != nCenter)
			{
				// 中央点以外から中央に近づいた → スナップ
				CSliderCtrl::SetPos(nCenter);
				GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, nCenter), (LPARAM)m_hWnd);
			}
			// nOldPos == nCenter の場合はスナップしない（98, 99, 101, 102に自由に移動可能）
		}

		Invalidate(FALSE);
		return result;
	}
	else
	{
		LRESULT result = Default();
		Invalidate(FALSE);
		return result;
	}
}

LRESULT CCustomSliderCtrl::OnLButtonDownMsg(WPARAM w, LPARAM l)
{
	LRESULT r = Default();

	// Mode 1, 2 のみ中央点スナップ機能を有効化
	if (m_nMode == 1 || m_nMode == 2)
	{
		int nMin, nMax;
		GetRange(nMin, nMax);
		int nCenter = (nMin + nMax) / 2;
		int nPos = CSliderCtrl::GetPos();

		// クリック時は常にスナップ（クリックは新規位置への移動なので）
		int nSnapRange = 2;
		if (abs(nPos - nCenter) <= nSnapRange)
		{
			CSliderCtrl::SetPos(nCenter);
			GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, nCenter), (LPARAM)m_hWnd);
		}
	}

	Invalidate(FALSE);
	return r;
}

LRESULT CCustomSliderCtrl::OnLButtonUpMsg(WPARAM w, LPARAM l)
{
	LRESULT r = Default();

	// Mode 1, 2 のみ中央点スナップ機能を有効化
	if (m_nMode == 1 || m_nMode == 2)
	{
		int nMin, nMax;
		GetRange(nMin, nMax);
		int nCenter = (nMin + nMax) / 2;
		int nPos = CSliderCtrl::GetPos();

		// ドラッグ終了時は常にスナップ（最終位置の調整）
		int nSnapRange = 2;
		if (abs(nPos - nCenter) <= nSnapRange)
		{
			CSliderCtrl::SetPos(nCenter);
			GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, nCenter), (LPARAM)m_hWnd);
		}
	}

	Invalidate(FALSE);
	return r;
}

void CCustomSliderCtrl::DrawSlider(CDC* pDC)
{
	CRect r;
	GetClientRect(&r);

	int nMin, nMax;
	GetRange(nMin, nMax);
	int nPos = GetPos();

	if (nMax <= nMin) return;

	if (m_nMode == 0)
		DrawMode0(pDC, r, nMin, nMax, nPos);
	else if (m_nMode == 1)
		DrawMode1(pDC, r, nMin, nMax, nPos);
	else if (m_nMode == 2)
		DrawMode2(pDC, r, nMin, nMax, nPos);
	else
		DrawMode1(pDC, r, nMin, nMax, nPos); // デフォルトはMode1
}

/**
 * @brief オーディオモード(音符デザイン + サウンドウェーブ背景)
 */
void CCustomSliderCtrl::DrawMode0(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
	int nRange = nMax - nMin;
	BOOL bVert = (GetStyle() & TBS_VERT);

	if (!bVert)
	{
		int nMarginX = 12; // Mode 1, 2と統一
		int nTrackL = rect.left + nMarginX;
		int nTrackR = rect.right - nMarginX;
		int nTrackW = nTrackR - nTrackL;

		if (nTrackW <= 0) return;

		int nThumbX = nTrackL + (int)((double)(nPos - nMin) * nTrackW / nRange);
		int nCenterY = rect.Height() / 2;
		int nBottomY = rect.bottom - 8;

		// トラック（台形）
		CPoint pts[4] = {
			{nTrackL, nBottomY},
			{nTrackL, nBottomY - 2},
			{nTrackR, rect.top + 4},
			{nTrackR, nBottomY}
		};

		CBrush brBack(COLOR_RANGE_SELECTION);
		pDC->SelectObject(&brBack);
		CPen penVine(PS_SOLID, 1, COLOR_VINE_DECO);
		pDC->SelectObject(&penVine);
		pDC->Polygon(pts, 4);

		// アクティブ範囲
		if (nThumbX > nTrackL)
		{
			CRgn rgnP, rgnL;
			rgnP.CreatePolygonRgn(pts, 4, WINDING);
			rgnL.CreateRectRgn(rect.left, rect.top, nThumbX, rect.bottom);
			rgnP.CombineRgn(&rgnP, &rgnL, RGN_AND);

			CBrush brA(RGB(180, 200, 255)); // 音楽的な青紫
			pDC->FillRgn(&rgnP, &brA);
		}

		// サムを音符で描画 ♪
		CRect rcNote(nThumbX - 10, nCenterY - 12, nThumbX + 10, nCenterY + 12);
		DrawMusicNote(pDC, rcNote, RGB(138, 43, 226)); // 紫の音符

		// 音符の周りにキラキラ
		DrawStar(pDC, nThumbX - 12, nCenterY - 14, 2, RGB(255, 215, 0));
		DrawStar(pDC, nThumbX + 12, nCenterY - 14, 2, RGB(255, 215, 0));
	}
	else
	{
		// 垂直スライダー（同様の処理）
		int nMarginY = 12; // Mode 1, 2と統一
		int nTrackT = rect.top + nMarginY;
		int nTrackB = rect.bottom - nMarginY;
		int nTrackH = nTrackB - nTrackT;

		if (nTrackH <= 0) return;

		int nThumbY = nTrackT + (int)((double)(nPos - nMin) * nTrackH / nRange);
		int nCenterX = rect.Width() / 2;

		CPoint pts[4] = {
			{nCenterX - 8, nTrackB},
			{nCenterX - 2, nTrackT},
			{nCenterX + 2, nTrackT},
			{nCenterX + 8, nTrackB}
		};

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

/**
 * @brief 目盛りモード(ダイヤモンド宝石デザイン)
 */
void CCustomSliderCtrl::DrawMode1(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
	int nRange = nMax - nMin;
	BOOL bVert = (GetStyle() & TBS_VERT);

	if (!bVert)
	{
		int nCenterY = rect.Height() / 2;
		int nTrackL = 12;
		int nTrackR = rect.Width() - 12;
		int nTrackW = nTrackR - nTrackL;

		if (nTrackW <= 0) return;

		int nThumbPos = nTrackL + (int)((double)(nPos - nMin) * nTrackW / nRange);

		// 選択部分のライン（宝石のような輝き）
		CPen penA(PS_SOLID, 5, RGB(200, 150, 255)); // 紫のグラデーション風
		pDC->SelectObject(&penA);
		pDC->MoveTo(nTrackL, nCenterY);
		pDC->LineTo(nThumbPos, nCenterY);

		// 非選択部分のライン
		CPen penI(PS_SOLID, 3, RGB(220, 220, 230));
		pDC->SelectObject(&penI);
		pDC->LineTo(nTrackR, nCenterY);

		// 装飾的な目盛りの描画
		CPen penT(PS_SOLID, 2, RGB(150, 100, 200));
		pDC->SelectObject(&penT);

		for (int i = 0; i <= 10; i++)
		{
			int nTickX = nTrackL + (nTrackW * i / 10);
			int nTickH = (i % 5 == 0) ? 10 : 5;

			// 目盛り線
			pDC->MoveTo(nTickX, nCenterY - nTickH);
			pDC->LineTo(nTickX, nCenterY + nTickH);

			// 重要な目盛りに小さな宝石
			if (i % 5 == 0)
			{
				CBrush br(RGB(200, 180, 255));
				CBrush* pOldBr = pDC->SelectObject(&br);
				pDC->Ellipse(nTickX - 3, nCenterY - nTickH - 5, nTickX + 3, nCenterY - nTickH + 1);
				pDC->SelectObject(pOldBr);
			}
		}

		// サムをダイヤモンド宝石で描画
		CRect rcDiamond(nThumbPos - 9, nCenterY - 12, nThumbPos + 9, nCenterY + 12);
		DrawDiamond(pDC, rcDiamond, RGB(200, 180, 255));

		// ダイヤモンドから放射状の光
		CPen penLight(PS_SOLID, 1, RGB(255, 240, 200));
		pDC->SelectObject(&penLight);
		for (int angle = 0; angle < 360; angle += 45)
		{
			double rad = angle * 3.14159 / 180.0;
			int x1 = nThumbPos + (int)(12 * cos(rad));
			int y1 = nCenterY + (int)(12 * sin(rad));
			int x2 = nThumbPos + (int)(18 * cos(rad));
			int y2 = nCenterY + (int)(18 * sin(rad));
			pDC->MoveTo(x1, y1);
			pDC->LineTo(x2, y2);
		}
	}
	else
	{
		int nCenterX = rect.Width() / 2;
		int nTrackT = 12;
		int nTrackB = rect.Height() - 12;
		int nTrackH = nTrackB - nTrackT;

		if (nTrackH <= 0) return;

		int nThumbPos = nTrackT + (int)((double)(nPos - nMin) * nTrackH / nRange);

		CPen penA(PS_SOLID, 5, RGB(200, 150, 255));
		pDC->SelectObject(&penA);
		pDC->MoveTo(nCenterX, nThumbPos);
		pDC->LineTo(nCenterX, nTrackB);

		CPen penI(PS_SOLID, 3, RGB(220, 220, 230));
		pDC->SelectObject(&penI);
		pDC->MoveTo(nCenterX, nTrackT);
		pDC->LineTo(nCenterX, nThumbPos);

		CPen penT(PS_SOLID, 2, RGB(150, 100, 200));
		pDC->SelectObject(&penT);

		for (int i = 0; i <= 10; i++)
		{
			int nTickY = nTrackT + (nTrackH * i / 10);
			int nTickW = (i % 5 == 0) ? 10 : 5;
			pDC->MoveTo(nCenterX - nTickW, nTickY);
			pDC->LineTo(nCenterX + nTickW, nTickY);

			if (i % 5 == 0)
			{
				CBrush br(RGB(200, 180, 255));
				CBrush* pOldBr = pDC->SelectObject(&br);
				pDC->Ellipse(nCenterX + nTickW + 1, nTickY - 3, nCenterX + nTickW + 7, nTickY + 3);
				pDC->SelectObject(pOldBr);
			}
		}

		CRect rcDiamond(nCenterX - 9, nThumbPos - 12, nCenterX + 9, nThumbPos + 12);
		DrawDiamond(pDC, rcDiamond, RGB(200, 180, 255));
	}
}

/**
 * @brief 目盛りモード2(ダイヤモンド宝石デザイン - 色違い)
 */
void CCustomSliderCtrl::DrawMode2(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
	int nRange = nMax - nMin;
	BOOL bVert = (GetStyle() & TBS_VERT);

	if (!bVert)
	{
		int nCenterY = rect.Height() / 2;
		int nTrackL = 12;
		int nTrackR = rect.Width() - 12;
		int nTrackW = nTrackR - nTrackL;

		if (nTrackW <= 0) return;

		int nThumbPos = nTrackL + (int)((double)(nPos - nMin) * nTrackW / nRange);

		// 選択部分のライン（エメラルド風の緑）
		CPen penA(PS_SOLID, 5, RGB(100, 200, 150)); // 緑のグラデーション風
		pDC->SelectObject(&penA);
		pDC->MoveTo(nTrackL, nCenterY);
		pDC->LineTo(nThumbPos, nCenterY);

		// 非選択部分のライン
		CPen penI(PS_SOLID, 3, RGB(220, 220, 230));
		pDC->SelectObject(&penI);
		pDC->LineTo(nTrackR, nCenterY);

		// 装飾的な目盛りの描画
		CPen penT(PS_SOLID, 2, RGB(80, 160, 120));
		pDC->SelectObject(&penT);

		for (int i = 0; i <= 10; i++)
		{
			int nTickX = nTrackL + (nTrackW * i / 10);
			int nTickH = (i % 5 == 0) ? 10 : 5;

			// 目盛り線
			pDC->MoveTo(nTickX, nCenterY - nTickH);
			pDC->LineTo(nTickX, nCenterY + nTickH);

			// 重要な目盛りに小さな宝石
			if (i % 5 == 0)
			{
				CBrush br(RGB(150, 220, 180));
				CBrush* pOldBr = pDC->SelectObject(&br);
				pDC->Ellipse(nTickX - 3, nCenterY - nTickH - 5, nTickX + 3, nCenterY - nTickH + 1);
				pDC->SelectObject(pOldBr);
			}
		}

		// サムをエメラルド色のダイヤモンド宝石で描画
		CRect rcDiamond(nThumbPos - 9, nCenterY - 12, nThumbPos + 9, nCenterY + 12);
		DrawDiamond(pDC, rcDiamond, RGB(100, 220, 160));

		// ダイヤモンドから放射状の光
		CPen penLight(PS_SOLID, 1, RGB(200, 255, 220));
		pDC->SelectObject(&penLight);
		for (int angle = 0; angle < 360; angle += 45)
		{
			double rad = angle * 3.14159 / 180.0;
			int x1 = nThumbPos + (int)(12 * cos(rad));
			int y1 = nCenterY + (int)(12 * sin(rad));
			int x2 = nThumbPos + (int)(18 * cos(rad));
			int y2 = nCenterY + (int)(18 * sin(rad));
			pDC->MoveTo(x1, y1);
			pDC->LineTo(x2, y2);
		}
	}
	else
	{
		int nCenterX = rect.Width() / 2;
		int nTrackT = 12;
		int nTrackB = rect.Height() - 12;
		int nTrackH = nTrackB - nTrackT;

		if (nTrackH <= 0) return;

		int nThumbPos = nTrackT + (int)((double)(nPos - nMin) * nTrackH / nRange);

		CPen penA(PS_SOLID, 5, RGB(100, 200, 150));
		pDC->SelectObject(&penA);
		pDC->MoveTo(nCenterX, nThumbPos);
		pDC->LineTo(nCenterX, nTrackB);

		CPen penI(PS_SOLID, 3, RGB(220, 220, 230));
		pDC->SelectObject(&penI);
		pDC->MoveTo(nCenterX, nTrackT);
		pDC->LineTo(nCenterX, nThumbPos);

		CPen penT(PS_SOLID, 2, RGB(80, 160, 120));
		pDC->SelectObject(&penT);

		for (int i = 0; i <= 10; i++)
		{
			int nTickY = nTrackT + (nTrackH * i / 10);
			int nTickW = (i % 5 == 0) ? 10 : 5;
			pDC->MoveTo(nCenterX - nTickW, nTickY);
			pDC->LineTo(nCenterX + nTickW, nTickY);

			if (i % 5 == 0)
			{
				CBrush br(RGB(150, 220, 180));
				CBrush* pOldBr = pDC->SelectObject(&br);
				pDC->Ellipse(nCenterX + nTickW + 1, nTickY - 3, nCenterX + nTickW + 7, nTickY + 3);
				pDC->SelectObject(pOldBr);
			}
		}

		CRect rcDiamond(nCenterX - 9, nThumbPos - 12, nCenterX + 9, nThumbPos + 12);
		DrawDiamond(pDC, rcDiamond, RGB(100, 220, 160));
	}
}

// ============================================================================
// CCustomRangeSliderCtrl - カスタム範囲スライダー
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
	: m_nMin(0)
	, m_nMax(100)
	, m_nSelMin(0)
	, m_nSelMax(100)
	, m_nDragTarget(0)
	, m_bDragging(FALSE)
	, m_nVisualPos(0)
	, m_nLogicalPos(0)
{
}

CCustomRangeSliderCtrl::~CCustomRangeSliderCtrl()
{
}

void CCustomRangeSliderCtrl::PreSubclassWindow()
{
	CSliderCtrl::PreSubclassWindow();

	HMODULE h = LoadLibrary(_T("UxTheme.dll"));
	if (h)
	{
		typedef HRESULT(WINAPI* S)(HWND, LPCWSTR, LPCWSTR);
		S p = (S)GetProcAddress(h, "SetWindowTheme");
		if (p)
			p(m_hWnd, L"", L"");
		FreeLibrary(h);
	}

	int min, max;
	CSliderCtrl::GetRange(min, max);
	m_nMin = min;
	m_nMax = max;
	m_nLogicalPos = CSliderCtrl::GetPos();
	m_nVisualPos = m_nLogicalPos;
}

void CCustomRangeSliderCtrl::SetPos(int p)
{
	if (p < m_nMin) p = m_nMin;
	if (p > m_nMax) p = m_nMax;

	m_nLogicalPos = p;
	m_nVisualPos = p;

	// 基底クラスのCSliderCtrlも同期
	CSliderCtrl::SetPos(p);

	if (::IsWindow(m_hWnd))
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

int CCustomRangeSliderCtrl::GetPos() const
{
	return m_nLogicalPos;
}

void CCustomRangeSliderCtrl::SetRange(int min, int max, BOOL b)
{
	m_nMin = min;
	m_nMax = max;
	m_nVisualPos = m_nLogicalPos;

	CSliderCtrl::SetRange(min, max, FALSE);

	if (b && ::IsWindow(m_hWnd))
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CCustomRangeSliderCtrl::SetSelection(int min, int max)
{
	m_nSelMin = min;
	m_nSelMax = max;

	if (m_nSelMin > m_nSelMax)
	{
		int t = m_nSelMin;
		m_nSelMin = m_nSelMax;
		m_nSelMax = t;
	}

	if (::IsWindow(m_hWnd))
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CCustomRangeSliderCtrl::GetSelection(int& min, int& max) const
{
	min = max(m_nMin, min(m_nMax, m_nSelMin));
	max = max(m_nMin, min(m_nMax, m_nSelMax));
}

void CCustomRangeSliderCtrl::OnPaint()
{
	CPaintDC dc(this);
	CRect r;
	GetClientRect(&r);

	CDC mDC;
	mDC.CreateCompatibleDC(&dc);
	CBitmap mB;
	mB.CreateCompatibleBitmap(&dc, r.Width(), r.Height());
	CBitmap* pOldB = mDC.SelectObject(&mB);

	mDC.FillSolidRect(&r, COLOR_DIALOG_BG);

	DrawRangeSlider(&mDC);

	dc.BitBlt(0, 0, r.Width(), r.Height(), &mDC, 0, 0, SRCCOPY);

	mDC.SelectObject(pOldB);
	mB.DeleteObject();
	mDC.DeleteDC();
}

BOOL CCustomRangeSliderCtrl::OnEraseBkgnd(CDC* p)
{
	return TRUE;
}

void CCustomRangeSliderCtrl::DrawRangeSlider(CDC* pDC)
{
	CRect r;
	GetClientRect(&r);

	// ゼロ除算防止：範囲が不正な場合は描画しない
	if (m_nMax <= m_nMin)
		return;

	int cy = r.Height() / 2;
	int cur = m_bDragging ? m_nVisualPos : m_nLogicalPos;

	int xMin = ValueToPixel(m_nSelMin);
	int xMax = ValueToPixel(m_nSelMax);
	int xPos = ValueToPixel(cur);

	CPen penT(PS_SOLID, 4, RGB(200, 200, 200));
	pDC->SelectObject(&penT);
	pDC->MoveTo(14, cy);
	pDC->LineTo(r.Width() - 14, cy);

	if (xMax > xMin)
	{
		CRect rcS(xMin, cy - 4, xMax, cy + 4);
		pDC->FillSolidRect(&rcS, COLOR_RANGE_SELECTION);
	}

	CPen penB(PS_SOLID, 1, RGB(0, 0, 0));
	pDC->SelectObject(&penB);

	CRect rcMin(xMin - 5, cy - 8, xMin + 5, cy + 8);
	pDC->FillSolidRect(&rcMin, COLOR_RANGE_SLIDER_THUMB);
	pDC->SelectObject(GetStockObject(NULL_BRUSH));
	pDC->Rectangle(&rcMin);

	CRect rcMax(xMax - 5, cy - 8, xMax + 5, cy + 8);
	pDC->FillSolidRect(&rcMax, COLOR_RANGE_SLIDER_THUMB);
	pDC->Rectangle(&rcMax);

	CRect rcH(xPos - 9, cy - 12, xPos + 9, cy + 6);
	DrawHeart(pDC, rcH, COLOR_SLIDER_THUMB);
}

int CCustomRangeSliderCtrl::ValueToPixel(int v) const
{
	CRect r;
	GetClientRect(&r);
	int w = r.Width() - 28;

	if (w <= 0) return 14;

	// ゼロ除算防止
	if (m_nMax <= m_nMin) return 14;

	int val = max(m_nMin, min(m_nMax, v));
	return 14 + (int)((long long)(val - m_nMin) * w / (m_nMax - m_nMin));
}

int CCustomRangeSliderCtrl::PixelToValue(int x) const
{
	CRect r;
	GetClientRect(&r);
	int w = r.Width() - 28;

	if (w <= 0) return m_nMin;

	// ゼロ除算防止
	if (m_nMax <= m_nMin) return m_nMin;

	int px = max(14, min(r.Width() - 14, x));
	return m_nMin + (int)((double)(px - 14) / w * (m_nMax - m_nMin) + 0.5);
}

int CCustomRangeSliderCtrl::HitTest(CPoint p) const
{
	CRect r;
	GetClientRect(&r);
	int cy = r.Height() / 2;

	int xM = ValueToPixel(m_nLogicalPos);
	int xMax = ValueToPixel(m_nSelMax);
	int xMin = ValueToPixel(m_nSelMin);

	if (CRect(xM - 10, cy - 14, xM + 10, cy + 14).PtInRect(p))
		return 3;

	if (CRect(xMax - 7, cy - 10, xMax + 7, cy + 10).PtInRect(p))
		return 2;

	if (CRect(xMin - 7, cy - 10, xMin + 7, cy + 10).PtInRect(p))
		return 1;

	return 0;
}

void CCustomRangeSliderCtrl::OnLButtonDown(UINT nF, CPoint p)
{
	SetFocus();
	m_nVisualPos = m_nLogicalPos;
	m_nDragTarget = HitTest(p);

	if (m_nDragTarget == 0)
	{
		// トラックをクリック → その位置へ移動
		m_nVisualPos = PixelToValue(p.x);
		m_nDragTarget = 3;

		// 基底クラスも更新
		CSliderCtrl::SetPos(m_nVisualPos);

		// 即座に親へ通知（TB_THUMBTRACKで移動開始を通知）
		GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, m_nVisualPos), (LPARAM)m_hWnd);
	}

	m_bDragging = TRUE;
	SetCapture();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CCustomRangeSliderCtrl::OnLButtonUp(UINT nF, CPoint p)
{
	if (m_bDragging)
	{
		m_bDragging = FALSE;
		ReleaseCapture();

		if (m_nDragTarget == 3)
		{
			m_nLogicalPos = m_nVisualPos;

			// 基底クラスも更新
			CSliderCtrl::SetPos(m_nLogicalPos);

			// 親へ通知（TB_THUMBPOSITIONとTB_ENDTRACK）
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

		if (m_nDragTarget == 3)
		{
			m_nVisualPos = max(m_nMin, min(m_nMax, v));

			// 基底クラスも更新
			CSliderCtrl::SetPos(m_nVisualPos);

			// 親へ通知
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
// CCustomCheckBox - カスタムチェックボックス(お姫様仕様)
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
	: m_bIsFlatStyle(FALSE)
	, m_bIsPressed(FALSE)
	, m_bIsHot(FALSE)
	, m_bTracking(FALSE)
	, m_nCheck(0)
{
}

CCustomCheckBox::~CCustomCheckBox()
{
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

void CCustomCheckBox::PreSubclassWindow()
{
	HMODULE h = LoadLibrary(_T("UxTheme.dll"));
	if (h)
	{
		typedef HRESULT(WINAPI* S)(HWND, LPCWSTR, LPCWSTR);
		S p = (S)GetProcAddress(h, "SetWindowTheme");
		if (p)
			p(m_hWnd, L"", L"");
		FreeLibrary(h);
	}

	m_bIsFlatStyle = (GetStyle() & BS_FLAT) || (GetStyle() & BS_PUSHLIKE);
	m_nCheck = CButton::GetCheck();
	ModifyStyle(BS_TYPEMASK | BS_FLAT | BS_PUSHLIKE, BS_OWNERDRAW);

	CButton::PreSubclassWindow();
}

void CCustomCheckBox::OnLButtonDown(UINT n, CPoint p)
{
	m_bIsPressed = TRUE;
	m_bIsHot = TRUE;
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
	m_bIsHot = FALSE;
	m_bTracking = FALSE;
	Invalidate();
}

void CCustomCheckBox::OnPaint()
{
	CPaintDC dc(this);
	CRect r;
	GetClientRect(&r);
	OnDrawLayer(&dc, r);
}

LRESULT CCustomCheckBox::OnPrintClient(WPARAM w, LPARAM l)
{
	CDC* pDC = CDC::FromHandle((HDC)w);
	CRect r;
	GetClientRect(&r);
	OnDrawLayer(pDC, r);
	return 0;
}

BOOL CCustomCheckBox::OnEraseBkgnd(CDC* p)
{
	return TRUE;
}

void CCustomCheckBox::OnDrawLayer(CDC* pDC, CRect rect)
{
	BOOL bC = (m_nCheck == BST_CHECKED);
	BOOL bD = !IsWindowEnabled();
	BOOL bP = m_bIsPressed && m_bIsHot;

	pDC->SelectObject(GetFont() ? GetFont() : (CFont*)pDC->SelectStockObject(DEFAULT_GUI_FONT));

	if (m_bIsFlatStyle)
	{
		BOOL s = bC || bP;
		COLORREF bg = bD ? RGB(200, 200, 200) :
			(s ? COLOR_BUTTON_PUSHED :
				(m_bIsHot ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG));
		pDC->FillSolidRect(&rect, bg);

		if (!bD)
			DrawDecorations(pDC, rect, 0, s);

		pDC->Draw3dRect(&rect, s ? RGB(100, 100, 100) : RGB(255, 255, 255),
			s ? RGB(255, 255, 255) : RGB(100, 100, 100));

		CString t;
		GetWindowText(t);
		DrawSmartText(pDC, rect, t, bD, s);
	}
	else
	{
		pDC->FillSolidRect(&rect, COLOR_DIALOG_BG);

		int s = 18; // 少し大きく
		int cy = rect.Height() / 2;
		CRect rcB(rect.left, cy - s / 2, rect.left + s, cy + s / 2);

		// チェックボックスの枠(可愛らしく角丸)
		CPen pen(PS_SOLID, 2, RGB(255, 140, 100));
		CBrush br(RGB(255, 255, 255));
		pDC->SelectObject(&pen);
		pDC->SelectObject(&br);
		pDC->RoundRect(&rcB, CPoint(5, 5));

		// チェック状態なら思い切り可愛い花丸✿
		if (bC)
		{
			CRect rcH = rcB;
			rcH.DeflateRect(1, 1);
			DrawHanamaru(pDC, rcH, RGB(255, 100, 150), RGB(255, 182, 193));
		}

		CString t;
		GetWindowText(t);
		if (!t.IsEmpty())
		{
			CRect rcT = rect;
			rcT.left = rcB.right + 8;
			DrawSmartText2(pDC, rcT, t, DT_LEFT | DT_VCENTER, bD, FALSE);
		}
	}

	if (GetFocus() == this)
	{
		CRect rcF = rect;
		if (!m_bIsFlatStyle)
			rcF.left += 20;
		else
			rcF.DeflateRect(3, 3);

		pDC->DrawFocusRect(&rcF);
	}
}

// ============================================================================
// CCustomGroupBox - カスタムグループボックス(お姫様仕様)
// ============================================================================
IMPLEMENT_DYNAMIC(CCustomGroupBox, CButton)

BEGIN_MESSAGE_MAP(CCustomGroupBox, CButton)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCustomGroupBox::CCustomGroupBox()
{
}

CCustomGroupBox::~CCustomGroupBox()
{
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

BOOL CCustomGroupBox::OnEraseBkgnd(CDC* p)
{
	return TRUE;
}

void CCustomGroupBox::DrawGroupBox(CDC* pDC, CRect& rect)
{
	CString t;
	GetWindowText(t);

	CFont* pOldF = pDC->SelectObject(GetFont());
	CSize s = pDC->GetTextExtent(t);

	int nT = rect.top + s.cy / 2;

	// 二重線で豪華な枠線
	CPen penOuter(PS_SOLID, 2, RGB(255, 140, 180)); // 外側：濃いピンク
	CPen penInner(PS_SOLID, 1, RGB(255, 200, 220)); // 内側：薄いピンク

	// 外側の線
	pDC->SelectObject(&penOuter);
	pDC->SelectObject(GetStockObject(NULL_BRUSH));

	pDC->MoveTo(rect.left + 1, nT);
	if (s.cx > 0)
	{
		pDC->LineTo(rect.left + 6, nT);
		pDC->MoveTo(rect.left + s.cx + 16, nT); // リボン分のスペース
	}

	pDC->LineTo(rect.right - 2, nT);
	pDC->LineTo(rect.right - 2, rect.bottom - 2);
	pDC->LineTo(rect.left + 1, rect.bottom - 2);
	pDC->LineTo(rect.left + 1, nT);

	// 内側の線（少し内側に）
	pDC->SelectObject(&penInner);
	int offset = 3;

	pDC->MoveTo(rect.left + offset, nT + offset);
	if (s.cx > 0)
	{
		pDC->LineTo(rect.left + 6 + offset, nT + offset);
		pDC->MoveTo(rect.left + s.cx + 16, nT + offset);
	}

	pDC->LineTo(rect.right - offset, nT + offset);
	pDC->LineTo(rect.right - offset, rect.bottom - offset);
	pDC->LineTo(rect.left + offset, rect.bottom - offset);
	pDC->LineTo(rect.left + offset, nT + offset);

	// 四隅に小さめのリボン装飾（枠線の上に配置）
	int ribbonSize = 6; // より小さく
	// 左上
	DrawRibbon(pDC, CRect(rect.left + 2, nT - 8, rect.left + 14, nT + 4), RGB(255, 182, 193));
	// 右上
	DrawRibbon(pDC, CRect(rect.right - 14, nT - 8, rect.right - 2, nT + 4), RGB(255, 182, 193));
	// 左下
	DrawRibbon(pDC, CRect(rect.left + 2, rect.bottom - 12, rect.left + 14, rect.bottom), RGB(255, 182, 193));
	// 右下
	DrawRibbon(pDC, CRect(rect.right - 14, rect.bottom - 12, rect.right - 2, rect.bottom), RGB(255, 182, 193));

	// タイトルテキスト
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

// ダイアログクラスは以前と同じため省略
// CCustomDialog、CCustomBlurDialogBase、CCustomDialogEx、CCustomBlurDialogExBaseは
// 元のコードをそのまま使用してください

// ============================================================================
// CCustomDialog - カスタムダイアログ
// ============================================================================

IMPLEMENT_DYNAMIC(CCustomDialog, CDialog)

BEGIN_MESSAGE_MAP(CCustomDialog, CDialog)
	ON_WM_CTLCOLOR()
	ON_WM_ERASEBKGND()
	ON_WM_PAINT()
	ON_MESSAGE(WM_USER + 1000, OnSubclassControls)
END_MESSAGE_MAP()

CCustomDialog::CCustomDialog()
{
	m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
}

CCustomDialog::CCustomDialog(UINT nIDTemplate, CWnd* pParentWnd)
	: CDialog(nIDTemplate, pParentWnd)
{
	m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
}

CCustomDialog::~CCustomDialog()
{
	if (m_brDialog.GetSafeHandle())
		m_brDialog.DeleteObject();
}

BOOL CCustomDialog::OnInitDialog()
{
	BOOL bResult = CDialog::OnInitDialog();
	PostMessage(WM_USER + 1000, 0, 0);
	return bResult;
}

LRESULT CCustomDialog::OnSubclassControls(WPARAM wParam, LPARAM lParam)
{
	SubclassChildControls();
	return 0;
}

void CCustomDialog::SubclassChildControls()
{
	CFont* pParentFont = GetFont();

	HWND hWndChild = ::GetWindow(m_hWnd, GW_CHILD);

	while (hWndChild != NULL)
	{
		if (!::IsWindow(hWndChild))
			break;

		if (!(::GetWindowLong(hWndChild, GWL_STYLE) & WS_VISIBLE))
		{
			hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT);
			continue;
		}

		CWnd* pWnd = CWnd::FromHandlePermanent(hWndChild);
		if (pWnd != NULL && pWnd != this)
		{
			hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT);
			continue;
		}

		TCHAR szClassName[256];
		::GetClassName(hWndChild, szClassName, 256);

		if (_tcsicmp(szClassName, _T("Edit")) == 0)
		{
			CCustomEdit* pEdit = new CCustomEdit();
			pEdit->SubclassWindow(hWndChild);
		}
		else if (_tcsicmp(szClassName, _T("Static")) == 0)
		{
			CCustomStatic* pStatic = new CCustomStatic();
			pStatic->SubclassWindow(hWndChild);
		}
		else if (_tcsicmp(szClassName, _T("ListBox")) == 0)
		{
			CCustomListBox* pListBox = new CCustomListBox();
			pListBox->SubclassWindow(hWndChild);
		}
		else if (_tcsicmp(szClassName, _T("ComboBox")) == 0)
		{
			CCustomComboBox* pCombo = new CCustomComboBox();
			pCombo->SubclassWindow(hWndChild);
		}
		else if (_tcsicmp(szClassName, WC_LISTVIEW) == 0)
		{
			CCustomListCtrl* pListCtrl = new CCustomListCtrl();
			pListCtrl->SubclassWindow(hWndChild);
		}
		else if (_tcsicmp(szClassName, _T("Button")) == 0)
		{
			LONG lStyle = ::GetWindowLong(hWndChild, GWL_STYLE);
			UINT nType = lStyle & BS_TYPEMASK;

			if (nType == BS_GROUPBOX)
			{
				CCustomGroupBox* pGroup = new CCustomGroupBox();
				pGroup->SubclassWindow(hWndChild);
			}
			else if (nType == BS_PUSHBUTTON || nType == BS_DEFPUSHBUTTON || (lStyle & BS_PUSHLIKE))
			{
				CCustomStandardButton* pButton = new CCustomStandardButton();
				pButton->SubclassWindow(hWndChild);
			}
			else
			{
				CCustomCheckBox* pCheck = new CCustomCheckBox();
				pCheck->SubclassWindow(hWndChild);
			}
		}
		else if (_tcsicmp(szClassName, TRACKBAR_CLASS) == 0)
		{
			LONG lStyle = ::GetWindowLong(hWndChild, GWL_STYLE);

			if (lStyle & TBS_ENABLESELRANGE)
			{
				CCustomRangeSliderCtrl* pRangeSlider = new CCustomRangeSliderCtrl();
				pRangeSlider->SubclassWindow(hWndChild);
			}
			else
			{
				CCustomSliderCtrl* pSlider = new CCustomSliderCtrl();
				pSlider->SubclassWindow(hWndChild);
			}
		}

		hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT);
	}
}

HBRUSH CCustomDialog::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (nCtlColor == CTLCOLOR_DLG)
	{
		return (HBRUSH)m_brDialog.GetSafeHandle();
	}

	if (nCtlColor == CTLCOLOR_EDIT)
	{
		CWnd* pParent = pWnd->GetParent();
		if (pParent)
		{
			TCHAR szClassName[256];
			::GetClassName(pParent->m_hWnd, szClassName, 256);

			if (_tcsicmp(szClassName, _T("ComboBox")) == 0)
			{
				pDC->SetBkColor(COLOR_COMBO_BG);
				pDC->SetTextColor(RGB(0, 0, 0));
				static CBrush brCombo(COLOR_COMBO_BG);
				return (HBRUSH)brCombo.GetSafeHandle();
			}
		}

		pDC->SetBkColor(COLOR_EDIT_BG);
		pDC->SetTextColor(RGB(0, 0, 0));
		static CBrush brEdit(COLOR_EDIT_BG);
		return (HBRUSH)brEdit.GetSafeHandle();
	}

	if (nCtlColor == CTLCOLOR_LISTBOX)
	{
		pDC->SetBkColor(COLOR_COMBO_BG);
		pDC->SetTextColor(RGB(0, 0, 0));
		static CBrush brCombo(COLOR_COMBO_BG);
		return (HBRUSH)brCombo.GetSafeHandle();
	}

	if (nCtlColor == CTLCOLOR_STATIC)
	{
		pDC->SetBkColor(COLOR_DIALOG_BG);
		pDC->SetTextColor(RGB(0, 0, 0));
		return (HBRUSH)m_brDialog.GetSafeHandle();
	}

	if (nCtlColor == CTLCOLOR_BTN)
	{
		pDC->SetBkColor(COLOR_DIALOG_BG);
		pDC->SetTextColor(RGB(0, 0, 0));
		return (HBRUSH)m_brDialog.GetSafeHandle();
	}

	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

BOOL CCustomDialog::OnEraseBkgnd(CDC* pDC)
{
	CRect rect;
	GetClientRect(&rect);
	pDC->FillRect(&rect, &m_brDialog);
	return TRUE;
}

void CCustomDialog::OnPaint()
{
	Default();
}

// ============================================================================
// CCustomBlurDialogBase
// ============================================================================

IMPLEMENT_DYNAMIC(CCustomBlurDialogBase, CCustomDialog)

CCustomBlurDialogBase::CCustomBlurDialogBase()
	: m_bBlurApplied(FALSE)
{
	m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
}

CCustomBlurDialogBase::CCustomBlurDialogBase(UINT nIDTemplate, CWnd* pParent)
	: CCustomDialog(nIDTemplate, pParent)
	, m_bBlurApplied(FALSE)
{
	m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
}

CCustomBlurDialogBase::~CCustomBlurDialogBase()
{
	if (m_brDialog.GetSafeHandle())
		m_brDialog.DeleteObject();
}

BOOL CCustomBlurDialogBase::OnInitDialog()
{
	BOOL bResult = CCustomDialog::OnInitDialog();
	ApplyDwmBlur();
	return bResult;
}

int CCustomBlurDialogBase::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CCustomDialog::OnCreate(lpCreateStruct) == -1)
		return -1;

	ApplyDwmBlur();
	return 0;
}

void CCustomBlurDialogBase::ApplyDwmBlur()
{
	if (!m_hWnd || !::IsWindow(m_hWnd))
		return;

	COSVersion os;
	os.GetVersionString();

	if (os.in.dwMajorVersion == 10 && os.in.dwBuildNumber >= 22000)
	{
		EnableDwmAcrylicWin11(m_hWnd);
		EnableRoundedCorners(m_hWnd);
		m_bBlurApplied = TRUE;
	}
	else if (os.in.dwMajorVersion == 10)
	{
		EnableDwmBlurBehindWin10(m_hWnd);
		m_bBlurApplied = TRUE;
	}
	else if (os.in.dwMajorVersion >= 6)
	{
		EnableDwmBlurBehindWin10(m_hWnd);
		m_bBlurApplied = TRUE;
	}
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

CCustomDialogEx::CCustomDialogEx()
{
	m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
}

CCustomDialogEx::CCustomDialogEx(UINT nIDTemplate, CWnd* pParentWnd)
	: CDialogEx(nIDTemplate, pParentWnd)
{
	m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
}

CCustomDialogEx::~CCustomDialogEx()
{
	if (m_brDialog.GetSafeHandle())
		m_brDialog.DeleteObject();
}

BOOL CCustomDialogEx::OnInitDialog()
{
	BOOL bResult = CDialogEx::OnInitDialog();
	PostMessage(WM_USER + 1000, 0, 0);
	return bResult;
}

LRESULT CCustomDialogEx::OnSubclassControls(WPARAM wParam, LPARAM lParam)
{
	SubclassChildControls();
	return 0;
}

void CCustomDialogEx::SubclassChildControls()
{
	CFont* pParentFont = GetFont();

	HWND hWndChild = ::GetWindow(m_hWnd, GW_CHILD);

	while (hWndChild != NULL)
	{
		if (!::IsWindow(hWndChild))
			break;

		if (!(::GetWindowLong(hWndChild, GWL_STYLE) & WS_VISIBLE))
		{
			hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT);
			continue;
		}

		CWnd* pWnd = CWnd::FromHandlePermanent(hWndChild);
		if (pWnd != NULL && pWnd != this)
		{
			hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT);
			continue;
		}

		TCHAR szClassName[256];
		::GetClassName(hWndChild, szClassName, 256);

		if (_tcsicmp(szClassName, _T("Edit")) == 0)
		{
			CCustomEdit* pEdit = new CCustomEdit();
			pEdit->SubclassWindow(hWndChild);
		}
		else if (_tcsicmp(szClassName, _T("Static")) == 0)
		{
			CCustomStatic* pStatic = new CCustomStatic();
			pStatic->SubclassWindow(hWndChild);
		}
		else if (_tcsicmp(szClassName, _T("ListBox")) == 0)
		{
			CCustomListBox* pListBox = new CCustomListBox();
			pListBox->SubclassWindow(hWndChild);
		}
		else if (_tcsicmp(szClassName, _T("ComboBox")) == 0)
		{
			CCustomComboBox* pCombo = new CCustomComboBox();
			pCombo->SubclassWindow(hWndChild);
		}
		else if (_tcsicmp(szClassName, WC_LISTVIEW) == 0)
		{
			CCustomListCtrl* pListCtrl = new CCustomListCtrl();
			pListCtrl->SubclassWindow(hWndChild);
		}
		else if (_tcsicmp(szClassName, _T("Button")) == 0)
		{
			LONG lStyle = ::GetWindowLong(hWndChild, GWL_STYLE);

			if ((lStyle & BS_CHECKBOX) || (lStyle & BS_AUTOCHECKBOX))
			{
				if (lStyle & BS_PUSHLIKE)
				{
					CCustomStandardButton* pButton = new CCustomStandardButton();
					pButton->SubclassWindow(hWndChild);
				}
				else
				{
					CCustomCheckBox* pCheck = new CCustomCheckBox();
					pCheck->SubclassWindow(hWndChild);
				}
			}
			else if (lStyle & BS_GROUPBOX)
			{
				CCustomGroupBox* pGroup = new CCustomGroupBox();
				pGroup->SubclassWindow(hWndChild);
			}
			else
			{
				CCustomStandardButton* pButton = new CCustomStandardButton();
				pButton->SubclassWindow(hWndChild);
			}
		}
		else if (_tcsicmp(szClassName, TRACKBAR_CLASS) == 0)
		{
			LONG lStyle = ::GetWindowLong(hWndChild, GWL_STYLE);

			if (lStyle & TBS_ENABLESELRANGE)
			{
				CCustomRangeSliderCtrl* pRangeSlider = new CCustomRangeSliderCtrl();
				pRangeSlider->SubclassWindow(hWndChild);
			}
			else
			{
				CCustomSliderCtrl* pSlider = new CCustomSliderCtrl();
				pSlider->SubclassWindow(hWndChild);
			}
		}

		hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT);
	}
}

HBRUSH CCustomDialogEx::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (nCtlColor == CTLCOLOR_DLG)
	{
		return (HBRUSH)m_brDialog.GetSafeHandle();
	}

	if (nCtlColor == CTLCOLOR_EDIT)
	{
		CWnd* pParent = pWnd->GetParent();
		if (pParent)
		{
			TCHAR szClassName[256];
			::GetClassName(pParent->m_hWnd, szClassName, 256);

			if (_tcsicmp(szClassName, _T("ComboBox")) == 0)
			{
				pDC->SetBkColor(COLOR_COMBO_BG);
				pDC->SetTextColor(RGB(0, 0, 0));
				static CBrush brCombo(COLOR_COMBO_BG);
				return (HBRUSH)brCombo.GetSafeHandle();
			}
		}

		pDC->SetBkColor(COLOR_EDIT_BG);
		pDC->SetTextColor(RGB(0, 0, 0));
		static CBrush brEdit(COLOR_EDIT_BG);
		return (HBRUSH)brEdit.GetSafeHandle();
	}

	if (nCtlColor == CTLCOLOR_LISTBOX)
	{
		pDC->SetBkColor(COLOR_COMBO_BG);
		pDC->SetTextColor(RGB(0, 0, 0));
		static CBrush brCombo(COLOR_COMBO_BG);
		return (HBRUSH)brCombo.GetSafeHandle();
	}

	if (nCtlColor == CTLCOLOR_STATIC)
	{
		pDC->SetBkColor(COLOR_DIALOG_BG);
		pDC->SetTextColor(RGB(0, 0, 0));
		return (HBRUSH)m_brDialog.GetSafeHandle();
	}

	if (nCtlColor == CTLCOLOR_BTN)
	{
		pDC->SetBkColor(COLOR_DIALOG_BG);
		pDC->SetTextColor(RGB(0, 0, 0));
		return (HBRUSH)m_brDialog.GetSafeHandle();
	}

	return CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
}

BOOL CCustomDialogEx::OnEraseBkgnd(CDC* pDC)
{
	CRect rect;
	GetClientRect(&rect);
	pDC->FillRect(&rect, &m_brDialog);
	return TRUE;
}

void CCustomDialogEx::OnPaint()
{
	Default();
}

// ============================================================================
// CCustomBlurDialogExBase
// ============================================================================

IMPLEMENT_DYNAMIC(CCustomBlurDialogExBase, CCustomDialogEx)

CCustomBlurDialogExBase::CCustomBlurDialogExBase()
	: m_bBlurApplied(FALSE)
{
	m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
}

CCustomBlurDialogExBase::CCustomBlurDialogExBase(UINT nIDTemplate, CWnd* pParent)
	: CCustomDialogEx(nIDTemplate, pParent)
	, m_bBlurApplied(FALSE)
{
	m_brDialog.CreateSolidBrush(COLOR_DIALOG_BG);
}

CCustomBlurDialogExBase::~CCustomBlurDialogExBase()
{
	if (m_brDialog.GetSafeHandle())
		m_brDialog.DeleteObject();
}

BOOL CCustomBlurDialogExBase::OnInitDialog()
{
	BOOL bResult = CCustomDialogEx::OnInitDialog();
	ApplyDwmBlur();
	return bResult;
}

int CCustomBlurDialogExBase::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CCustomDialogEx::OnCreate(lpCreateStruct) == -1)
		return -1;

	ApplyDwmBlur();
	return 0;
}

void CCustomBlurDialogExBase::ApplyDwmBlur()
{
	if (!m_hWnd || !::IsWindow(m_hWnd))
		return;

	COSVersion os;
	os.GetVersionString();

	if (os.in.dwMajorVersion == 10 && os.in.dwBuildNumber >= 22000)
	{
		EnableDwmAcrylicWin11(m_hWnd);
		EnableRoundedCorners(m_hWnd);
		m_bBlurApplied = TRUE;
	}
	else if (os.in.dwMajorVersion == 10)
	{
		EnableDwmBlurBehindWin10(m_hWnd);
		m_bBlurApplied = TRUE;
	}
	else if (os.in.dwMajorVersion >= 6)
	{
		EnableDwmBlurBehindWin10(m_hWnd);
		m_bBlurApplied = TRUE;
	}
}