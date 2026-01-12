#include "stdafx.h"
#include "ListCtrlA.h"
#include "BtnST.h"
#include "DwmBlurHelper.h"
#include "OSVersion.h"
#include "CCustomControl.h"

#ifdef SubclassWindow
#undef SubclassWindow
#endif

// ============================================================================
// 共通ヘルパー関数（クラス内部で使用）
// ============================================================================
static void DrawDecorations(CDC* pDC, CRect rect, BOOL bPatternA, BOOL bPushed)
{
	// お嬢様のために、四隅に花の飾りを描きます
	// bPatternA: TRUEなら左上＆右下、FALSEなら右上＆左下

	CPen penVine(PS_SOLID, 1, COLOR_VINE_DECO);
	CBrush brFlower(RGB(255, 192, 203)); // ピンクの花びら
	CBrush brCenter(RGB(255, 255, 0));   // 黄色の花芯

	CPen* pOldPen = pDC->SelectObject(&penVine);
	CBrush* pOldBrush = pDC->SelectObject(&brFlower);

	int offset = bPushed ? 1 : 0; // ボタンが押されているときは少しずらす
	rect.DeflateRect(2, 2); // 枠線に被らないように少し内側へ

	// 装飾を描くコーナーのリストを作成
	struct Corner { int x; int y; int dx; int dy; };
	std::vector<Corner> corners;

	if (bPatternA) {
		// 左上
		corners.push_back({ rect.left + offset, rect.top + offset, 1, 1 });
		// 右下
		corners.push_back({ rect.right - 1 + offset, rect.bottom - 1 + offset, -1, -1 });
	}
	else {
		// 右上
		corners.push_back({ rect.right - 1 + offset, rect.top + offset, -1, 1 });
		// 左下
		corners.push_back({ rect.left + offset, rect.bottom - 1 + offset, 1, -1 });
	}

	for (const auto& c : corners)
	{
		// 1. 蔓（つる）を描く (ベジェ曲線)
		// コーナーから内側へ伸びる曲線
		CPoint pts[4];
		pts[0] = CPoint(c.x, c.y + (15 * c.dy)); // 始点（縦側）
		pts[1] = CPoint(c.x + (5 * c.dx), c.y + (5 * c.dy)); // 制御点1
		pts[2] = CPoint(c.x + (5 * c.dx), c.y + (5 * c.dy)); // 制御点2
		pts[3] = CPoint(c.x + (15 * c.dx), c.y); // 終点（横側）
		pDC->PolyBezier(pts, 4);

		// 2. 花を描く
		// コーナー付近に配置
		int r = 3; // 花の半径
		int fx = c.x + (4 * c.dx);
		int fy = c.y + (4 * c.dy);

		// 花びら（4つの円）
		pDC->SelectObject(&brFlower);
		pDC->SelectObject(GetStockObject(NULL_PEN)); // 枠線なし
		pDC->Ellipse(fx - r, fy - r * 2, fx + r, fy);
		pDC->Ellipse(fx - r, fy, fx + r, fy + r * 2);
		pDC->Ellipse(fx - r * 2, fy - r, fx, fy + r);
		pDC->Ellipse(fx, fy - r, fx + r * 2, fy + r);

		// 花芯
		pDC->SelectObject(&brCenter);
		pDC->Ellipse(fx - 2, fy - 2, fx + 2, fy + 2);

		// ペンを戻す
		pDC->SelectObject(&penVine);
	}

	pDC->SelectObject(pOldPen);
	pDC->SelectObject(pOldBrush);
}

// テキスト描画のロジック（お嬢様のご要望通りに調整）
static void DrawSmartText(CDC* pDC, CRect rect, CString strText, BOOL bDisabled, BOOL bPushed)
{
	if (strText.IsEmpty()) return;

	pDC->SetBkMode(TRANSPARENT);
	pDC->SetTextColor(bDisabled ? RGB(128, 128, 128) : COLOR_EDIT_TEXT);

	CRect rcText = rect;
	rcText.DeflateRect(1, 1); // 余白
	if (bPushed) rcText.OffsetRect(1, 1);

	// 現在のフォント情報を取得
	CFont* pCurrentFont = pDC->GetCurrentFont();
	LOGFONT lf;
	pCurrentFont->GetLogFont(&lf);

	long originalHeight = lf.lfHeight;

	// 【ステップ1】 まず 2px 小さくする
	// lfHeightは負の値（ピクセル数）であることが多いので、絶対値を小さくする方向で計算
	long targetHeight = abs(originalHeight);
	targetHeight = max(8, targetHeight - 2); // 最低でも8pxは確保
	lf.lfHeight = -targetHeight;

	CFont fontSmall;
	fontSmall.CreateFontIndirect(&lf);
	CFont* pOldFont = pDC->SelectObject(&fontSmall);

	// 一行で収まるかチェック
	CSize szText = pDC->GetTextExtent(strText);

	if (szText.cx <= rcText.Width())
	{
		// 収まるならそのまま描画（上下中央）
		pDC->DrawText(strText, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	else
	{
		// 【ステップ2】 収まらないなら自動折り返しを試みる
		// 同じフォントサイズ（-2px状態）で計算
		CRect rcCalc = rcText;
		int nHeight = pDC->DrawText(strText, &rcCalc, DT_CENTER | DT_WORDBREAK | DT_CALCRECT);

		if (nHeight <= rcText.Height())
		{
			// 折り返して高さが収まるなら描画
			CRect rcDraw = rcText;
			// 縦方向の中央寄せ計算
			int nTopOffset = (rcText.Height() - nHeight) / 2;
			rcDraw.top += nTopOffset;
			rcDraw.bottom = rcDraw.top + nHeight;
			pDC->DrawText(strText, &rcDraw, DT_CENTER | DT_WORDBREAK);
		}
		else
		{
			pDC->SelectObject(pOldFont);
			fontSmall.DeleteObject();

			int nTryHeight = targetHeight;
			BOOL bPrinted = FALSE;

			// 最小サイズ(例えば6px)までループ
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
					// 収まった！描画
					CRect rcDraw = rcText;
					int nTopOffset = (rcText.Height() - nH) / 2;
					rcDraw.top += nTopOffset;
					rcDraw.bottom = rcDraw.top + nH;
					pDC->DrawText(strText, &rcDraw, DT_CENTER | DT_WORDBREAK);

					pDC->SelectObject(pOldFont);
					fontTry.DeleteObject();
					bPrinted = TRUE;
					break;
				}

				pDC->SelectObject(pOldFont); // ループのために戻す
				fontTry.DeleteObject();
			}

			// 万が一最小サイズでも入らない場合は、最小サイズでクリッピングして描画
			if (!bPrinted)
			{
				lf.lfHeight = -6;
				CFont fontMin;
				fontMin.CreateFontIndirect(&lf);
				pDC->SelectObject(&fontMin);
				pDC->DrawText(strText, &rcText, DT_CENTER | DT_WORDBREAK | DT_VCENTER);
				pDC->SelectObject(pOldFont);
			}
			return; // 処理終了
		}
	}

	// ステップ1,2で描画完了した場合の後始末
	pDC->SelectObject(pOldFont);
}

static void DrawSmartText2(CDC* pDC, CRect rect, CString strText, UINT nFormat, BOOL bDisabled, BOOL bPushed)
{
	if (strText.IsEmpty()) return;

	pDC->SetBkMode(TRANSPARENT);
	pDC->SetTextColor(bDisabled ? RGB(128, 128, 128) : COLOR_EDIT_TEXT);

	// ---------------------------------------------------------
	// 描画・判定用の矩形を準備
	// ---------------------------------------------------------
	CRect rcLimit = rect;

	// 横方向：最低限の美観のため、左右に2pxずつの余白は確保します
	// これがないと枠線に文字がくっついてしまいます
	rcLimit.DeflateRect(2, 0);

	// 押下時は全体を1px右下へずらす（これは見た目のため維持）
	if (bPushed) rcLimit.OffsetRect(1, 1);

	// ---------------------------------------------------------
	// フォント縮小ループ
	// ---------------------------------------------------------
	CFont* pCurrentFont = pDC->GetCurrentFont();
	LOGFONT lf;
	pCurrentFont->GetLogFont(&lf);

	long originalHeight = abs(lf.lfHeight);
	long targetHeight = originalHeight;
	const long MIN_HEIGHT = 6;

	CFont fontTry;
	CFont* pOldFont = NULL;

	while (targetHeight >= MIN_HEIGHT)
	{
		if (fontTry.GetSafeHandle()) fontTry.DeleteObject();

		lf.lfHeight = -targetHeight;
		fontTry.CreateFontIndirect(&lf);

		pOldFont = pDC->SelectObject(&fontTry);

		// サイズ計測
		// rcCalc には「許容できる最大幅(rcLimit.Width)」が入った状態からスタートし、
		// DrawText が実際に必要な高さ・幅に書き換えます。
		CRect rcCalc = rcLimit;

		// DT_CALCRECT: 描画はせずサイズ計算のみ
		// nFormat (DT_WORDBREAK等) に従って計算されます
		pDC->DrawText(strText, &rcCalc, nFormat | DT_CALCRECT);

		pDC->SelectObject(pOldFont);

		// 判定
		// 幅が収まっているか？ (DT_WORDBREAK指定時は自動で折り返すので幅は収まるはずですが、単一行時は重要)
		// 高さが収まっているか？ (ここが重要)
		if (rcCalc.Width() <= rcLimit.Width() && rcCalc.Height() <= rcLimit.Height())
		{
			// 収まったのでループ終了
			break;
		}

		// 収まらない場合、フォントを1px小さくして再挑戦
		targetHeight--;
	}

	// ---------------------------------------------------------
	// 決定したフォントで描画
	// ---------------------------------------------------------
	// ループを抜けた時点（または最小サイズ）のフォントを作成・適用
	if (!fontTry.GetSafeHandle()) // ループせず初回で抜けた場合などの保険
	{
		lf.lfHeight = -targetHeight;
		fontTry.CreateFontIndirect(&lf);
	}

	pOldFont = pDC->SelectObject(&fontTry);

	// 描画時は rcLimit（縦幅いっぱい）の領域を使います。
	// DT_VCENTER がある場合、枠の高さ中央に配置されます。
	pDC->DrawText(strText, &rcLimit, nFormat);

	pDC->SelectObject(pOldFont);
	fontTry.DeleteObject();
}

// ============================================================================
// CCustomEdit
// ============================================================================

IMPLEMENT_DYNAMIC(CCustomEdit, CEdit)

BEGIN_MESSAGE_MAP(CCustomEdit, CEdit)
	ON_WM_CTLCOLOR_REFLECT()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCustomEdit::CCustomEdit()
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
		if (pParentFont && pParentFont->GetSafeHandle())
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

// ============================================================================
// CCustomStatic
// ============================================================================

IMPLEMENT_DYNAMIC(CCustomStatic, CStatic)

BEGIN_MESSAGE_MAP(CCustomStatic, CStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCustomStatic::CCustomStatic()
{
}

CCustomStatic::~CCustomStatic()
{
	if (m_font.GetSafeHandle())
		m_font.DeleteObject();
}

void CCustomStatic::SetFont(CFont* pFont, BOOL bRedraw)
{
	if (pFont && pFont->GetSafeHandle())
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
		if (pParentFont && pParentFont->GetSafeHandle())
		{
			SetFont(pParentFont, FALSE);
		}
	}
}

void CCustomStatic::OnPaint()
{
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(&rect);

	// 背景を完全にクリア（不透明）
	CBrush brDialog(COLOR_DIALOG_BG);
	dc.FillRect(&rect, &brDialog);

	CString strText;
	GetWindowText(strText);

	// フォントを設定
	CFont* pOldFont = NULL;
	CFont* pFont = GetFont();
	if (pFont && pFont->GetSafeHandle())
	{
		pOldFont = dc.SelectObject(pFont);
	}

	// テキストを描画（不透明背景）
	dc.SetBkColor(COLOR_DIALOG_BG);
	dc.SetTextColor(RGB(0, 0, 0));

	DWORD dwStyle = GetStyle();
	UINT nFormat = DT_VCENTER | DT_WORDBREAK; // 折り返しを有効化

	if (dwStyle & SS_CENTER)
		nFormat |= DT_CENTER;
	else if (dwStyle & SS_RIGHT)
		nFormat |= DT_RIGHT;
	else
		nFormat |= DT_LEFT;

	// SS_SINGLELINEの場合は折り返さない
	if (dwStyle & SS_CENTERIMAGE)
		nFormat = (nFormat & ~DT_WORDBREAK) | DT_SINGLELINE;

//	dc.DrawText(strText, &rect, nFormat);
	DrawSmartText2(&dc, rect, strText, nFormat, FALSE, FALSE);
	if (pOldFont)
		dc.SelectObject(pOldFont);
}

BOOL CCustomStatic::OnEraseBkgnd(CDC* pDC)
{
	CRect rect;
	GetClientRect(&rect);
	CBrush brDialog(COLOR_DIALOG_BG);
	pDC->FillRect(&rect, &brDialog);
	return TRUE;
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

void CCustomListBox::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (lpDrawItemStruct->itemID == (UINT)-1)
		return;

	CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
	CRect rect = lpDrawItemStruct->rcItem;

	if (lpDrawItemStruct->itemState & ODS_SELECTED)
	{
		CBrush brSel(RGB(180, 180, 220));
		pDC->FillRect(&rect, &brSel);
		pDC->SetTextColor(RGB(0, 0, 0));
	}
	else
	{
		pDC->FillRect(&rect, &m_brBackground);
		pDC->SetTextColor(RGB(0, 0, 0));
	}

	CString strText;
	GetText(lpDrawItemStruct->itemID, strText);

	pDC->SetBkMode(TRANSPARENT);
	rect.left += 4;
	rect.right -= 2;
	DrawSmartText(pDC, rect, strText, FALSE, FALSE);
	//pDC->DrawText(strText, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void CCustomListBox::MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	lpMeasureItemStruct->itemHeight = 18;
}

// ============================================================================
// CCustomComboBox
// ============================================================================

IMPLEMENT_DYNAMIC(CCustomComboBox, CComboBox)

BEGIN_MESSAGE_MAP(CCustomComboBox, CComboBox)
	ON_WM_CTLCOLOR_REFLECT()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCustomComboBox::CCustomComboBox()
{
	m_brBackground.CreateSolidBrush(COLOR_COMBO_BG);
}

CCustomComboBox::~CCustomComboBox()
{
	if (m_brBackground.GetSafeHandle())
		m_brBackground.DeleteObject();
}

void CCustomComboBox::PreSubclassWindow()
{
	CComboBox::PreSubclassWindow();

	DWORD dwStyle = GetStyle();

	// CBS_DROPDOWNLISTの場合のみオーナードロー
	if ((dwStyle & CBS_DROPDOWNLIST) == CBS_DROPDOWNLIST)
	{
		ModifyStyle(0, CBS_OWNERDRAWFIXED | CBS_HASSTRINGS);
	}
}

HBRUSH CCustomComboBox::CtlColor(CDC* pDC, UINT nCtlColor)
{
	pDC->SetBkColor(COLOR_COMBO_BG);
	pDC->SetTextColor(RGB(0, 0, 0));
	return (HBRUSH)m_brBackground.GetSafeHandle();
}

void CCustomComboBox::OnPaint()
{
	Default();
}

BOOL CCustomComboBox::OnEraseBkgnd(CDC* pDC)
{
	return FALSE;
}

void CCustomComboBox::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (lpDrawItemStruct->itemID == (UINT)-1)
		return;

	CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
	CRect rect = lpDrawItemStruct->rcItem;

	if (lpDrawItemStruct->itemState & ODS_SELECTED)
	{
		CBrush brSel(RGB(180, 180, 220));
		pDC->FillRect(&rect, &brSel);
		pDC->SetTextColor(RGB(0, 0, 0));
	}
	else
	{
		pDC->FillRect(&rect, &m_brBackground);
		pDC->SetTextColor(RGB(0, 0, 0));
	}

	CString strText;
	GetLBText(lpDrawItemStruct->itemID, strText);

	pDC->SetBkMode(TRANSPARENT);
	rect.left += 4;
	rect.right -= 2;
	pDC->DrawText(strText, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void CCustomComboBox::MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	lpMeasureItemStruct->itemHeight = 18;
}

// ============================================================================
// CCustomListCtrl
// ============================================================================

IMPLEMENT_DYNAMIC(CCustomListCtrl, CListCtrl)

BEGIN_MESSAGE_MAP(CCustomListCtrl, CListCtrl)
	ON_WM_CTLCOLOR_REFLECT()
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnCustomDraw)
END_MESSAGE_MAP()

CCustomListCtrl::CCustomListCtrl()
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
	CListCtrlA::PreSubclassWindow(); // AクラスまたはCListCtrl
	SetBkColor(COLOR_LIST_BG);
	SetTextBkColor(COLOR_LIST_BG);
	SetTextColor(RGB(0, 0, 0));
}

HBRUSH CCustomListCtrl::CtlColor(CDC* pDC, UINT nCtlColor)
{
	pDC->SetBkColor(COLOR_LIST_BG);
	pDC->SetTextColor(RGB(0, 0, 0));
	return (HBRUSH)m_brBackground.GetSafeHandle();
}

// アイコンを透過描画するヘルパー関数
void DrawTransparentIcon(CDC* pDC, CImageList* pImgList, int nImageIndex, CRect rcIcon, COLORREF clrMask)
{
	if (!pImgList || nImageIndex < 0) return;

	IMAGEINFO ii;
	if (!pImgList->GetImageInfo(nImageIndex, &ii)) return;

	CRect rcImage(ii.rcImage);
	int w = rcImage.Width();
	int h = rcImage.Height();

	// 1. メモリDCにアイコンをそのまま描画（白背景のまま）
	CDC memDC;
	memDC.CreateCompatibleDC(pDC);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(pDC, w, h);
	CBitmap* pOldBmp = memDC.SelectObject(&bmp);

	// 背景を白で塗りつぶしてから描画
	memDC.FillSolidRect(0, 0, w, h, clrMask);
	pImgList->Draw(&memDC, nImageIndex, CPoint(0, 0), ILD_NORMAL);

	// 2. TransparentBltで、指定色(白)を抜いて画面へ転送
	//    アイコン領域の中央に配置
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

	if (pLVCD->nmcd.dwDrawStage == CDDS_PREPAINT)
	{
		*pResult = CDRF_NOTIFYITEMDRAW;
	}
	else if (pLVCD->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
	{
		CDC* pDC = CDC::FromHandle(pLVCD->nmcd.hdc);
		int nItem = (int)pLVCD->nmcd.dwItemSpec;
		UINT uState = pLVCD->nmcd.uItemState;

		// 選択状態かどうか
		BOOL bSelected = (uState & CDIS_SELECTED);

		// テキスト部分の背景色
		COLORREF clrTextBk = bSelected ? COLOR_LIST_BG : COLOR_LIST_BG; // 青固定
		COLORREF clrText = RGB(0, 0, 0);

		pLVCD->clrText = clrText;
		pLVCD->clrTextBk = clrTextBk;

		// --- アイコン描画処理 ---
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
				COLORREF clrIconBk = COLOR_LIST_BG;

				// 1. 固定色(薄い青)で塗りつぶす
				pDC->FillSolidRect(&rcIcon, clrIconBk);

				// 【追加】グリッドラインを手動で描く
				// リストビューのグリッド線が有効かどうか確認
				if (GetExtendedStyle() & LVS_EX_GRIDLINES)
				{
					// グリッド線の色（通常はシステムカラーの3DLIGHTか、薄いグレー）
					// ここでは一般的な薄いグレー(RGB(240, 240, 240))を使用
					CPen penGrid(PS_SOLID, 1, RGB(240, 240, 240));
					CPen* pOldPen = pDC->SelectObject(&penGrid);

					// 下線を描く
					pDC->MoveTo(rcIcon.left, rcIcon.bottom - 1);
					pDC->LineTo(rcIcon.right, rcIcon.bottom - 1);

					// 必要なら右線も描く（縦線も消えている場合）
					// pDC->MoveTo(rcIcon.right - 1, rcIcon.top);
					// pDC->LineTo(rcIcon.right - 1, rcIcon.bottom);

					pDC->SelectObject(pOldPen);
				}

				// 2. アイコンを透過描画
				DrawTransparentIcon(pDC, pImgList, lvi.iImage, rcIcon, RGB(255, 255, 255));

				// 3. クリップ領域から除外
				pDC->ExcludeClipRect(&rcIcon);
			}
		}

		*pResult = CDRF_NEWFONT;
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
	: m_bMouseOver(FALSE)
{
	m_brBackground.CreateSolidBrush(COLOR_BUTTON_BG);
}

CCustomStandardButton::~CCustomStandardButton()
{
	if (m_brBackground.GetSafeHandle())
		m_brBackground.DeleteObject();
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
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(&rect);

	// 状態取得
	BOOL bPushed = (GetState() & BST_PUSHED) != 0;
	BOOL bFocused = (GetFocus() == this);
	BOOL bDisabled = !IsWindowEnabled();

	// BS_PUSHLIKE チェックボックス対応
	LONG lStyle = GetStyle();
	BOOL bIsPushLikeCheckbox = ((lStyle & BS_CHECKBOX) || (lStyle & BS_AUTOCHECKBOX)) && (lStyle & BS_PUSHLIKE);
	if (bIsPushLikeCheckbox)
	{
		if (GetCheck() == BST_CHECKED)
			bPushed = TRUE;
	}

	// 背景色決定
	COLORREF clrBg = COLOR_BUTTON_BG;
	if (bDisabled)
	{
		clrBg = RGB(200, 200, 200);
	}
	else if (bPushed)
	{
		clrBg = COLOR_BUTTON_PUSHED;
	}
	else if (m_bMouseOver)
	{
		clrBg = COLOR_BUTTON_HOVER;
	}

	// ダブルバッファリング等は簡易化のため省略し、直接描画
	CBrush brBg(clrBg);
	dc.FillRect(&rect, &brBg);

	// --- 装飾描画 (ここを追加) ---
	if (!bDisabled)
	{
		DrawDecorations(&dc, rect, 0, bPushed);
	}

	// 3D枠の描画（お嬢様のご指定通り、凝った枠線は維持）
	CPen penLight(PS_SOLID, 2, RGB(255, 255, 255));
	CPen penDark(PS_SOLID, 2, RGB(128, 128, 128));
	CPen* pOldPen;

	if (bPushed)
	{
		// 沈んだ状態
		pOldPen = dc.SelectObject(&penDark);
		dc.MoveTo(rect.left, rect.bottom - 1);
		dc.LineTo(rect.left, rect.top);
		dc.LineTo(rect.right - 1, rect.top);

		dc.SelectObject(&penLight);
		dc.MoveTo(rect.right - 1, rect.top);
		dc.LineTo(rect.right - 1, rect.bottom - 1);
		dc.LineTo(rect.left, rect.bottom - 1);

		// 内側にも影
		CRect rectInner = rect;
		rectInner.DeflateRect(2, 2);
		dc.SelectObject(&penDark);
		dc.MoveTo(rectInner.left, rectInner.bottom - 1);
		dc.LineTo(rectInner.left, rectInner.top);
		dc.LineTo(rectInner.right - 1, rectInner.top);
	}
	else
	{
		// 浮いた状態
		pOldPen = dc.SelectObject(&penLight);
		dc.MoveTo(rect.left, rect.bottom - 1);
		dc.LineTo(rect.left, rect.top);
		dc.LineTo(rect.right - 1, rect.top);

		dc.SelectObject(&penDark);
		dc.MoveTo(rect.right - 1, rect.top);
		dc.LineTo(rect.right - 1, rect.bottom - 1);
		dc.LineTo(rect.left, rect.bottom - 1);

		CRect rectInner = rect;
		rectInner.DeflateRect(2, 2);
		dc.SelectObject(&penLight);
		dc.MoveTo(rectInner.left, rectInner.bottom - 1);
		dc.LineTo(rectInner.left, rectInner.top);
		dc.LineTo(rectInner.right - 1, rectInner.top);
	}
	dc.SelectObject(pOldPen);

	// フォーカス枠
	if (bFocused && !bDisabled)
	{
		CRect rcFocus = rect;
		rcFocus.DeflateRect(4, 4);
		dc.DrawFocusRect(&rcFocus);
	}

	// テキスト描画 (スマートフィッティング呼び出し)
	CString strText;
	GetWindowText(strText);

	// フォント設定（親のフォント、なければデフォルト）
	CFont* pFont = GetFont();
	CFont* pOldFont = NULL;
	if (pFont) pOldFont = dc.SelectObject(pFont);
	else pOldFont = (CFont*)dc.SelectStockObject(DEFAULT_GUI_FONT);

	DrawSmartText(&dc, rect, strText, bDisabled, bPushed);

	if (pOldFont) dc.SelectObject(pOldFont);
}

// その他のイベントハンドラは変更なしですが、フル実装のため記載
BOOL CCustomStandardButton::OnEraseBkgnd(CDC* pDC) { return TRUE; }
void CCustomStandardButton::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_bMouseOver)
	{
		TRACKMOUSEEVENT tme;
		tme.cbSize = sizeof(TRACKMOUSEEVENT);
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = m_hWnd;
		TrackMouseEvent(&tme);
		m_bMouseOver = TRUE;
		Invalidate();
	}
	CButton::OnMouseMove(nFlags, point);
}
LRESULT CCustomStandardButton::OnMouseLeave(WPARAM wParam, LPARAM lParam)
{
	m_bMouseOver = FALSE;
	Invalidate();
	return 0;
}
void CCustomStandardButton::OnSetFocus(CWnd* pOldWnd) { CButton::OnSetFocus(pOldWnd); Invalidate(); }
void CCustomStandardButton::OnKillFocus(CWnd* pNewWnd) { CButton::OnKillFocus(pNewWnd); Invalidate(); }
void CCustomStandardButton::OnEnable(BOOL bEnable) { CButton::OnEnable(bEnable); Invalidate(); }

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

CCustomSliderCtrl::CCustomSliderCtrl()
	: m_nMode(0) // 既定はモード0（オーディオ風）
{
}

CCustomSliderCtrl::~CCustomSliderCtrl()
{
}

void CCustomSliderCtrl::SetMode(int nMode)
{
	if (m_nMode != nMode)
	{
		m_nMode = nMode;
		if (GetSafeHwnd())
		{
			Invalidate(); // モード変更時に再描画
		}
	}
}

void CCustomSliderCtrl::PreSubclassWindow()
{
	CSliderCtrl::PreSubclassWindow();
}

void CCustomSliderCtrl::OnPaint()
{
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(&rect);

	// ダブルバッファリング
	CDC memDC;
	CBitmap memBmp;
	memDC.CreateCompatibleDC(&dc);
	memBmp.CreateCompatibleBitmap(&dc, rect.Width(), rect.Height());
	CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

	// 背景塗りつぶし（指定色：薄いピンク）
	CBrush brDialog(COLOR_DIALOG_BG);
	memDC.FillRect(&rect, &brDialog);

	// スライダー描画
	DrawSlider(&memDC);

	// 転送
	dc.BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);

	memDC.SelectObject(pOldBmp);
	memBmp.DeleteObject();
	memDC.DeleteDC();
}

BOOL CCustomSliderCtrl::OnEraseBkgnd(CDC* pDC)
{
	return TRUE; // ちらつき防止
}

LRESULT CCustomSliderCtrl::OnMouseMoveMsg(WPARAM wParam, LPARAM lParam)
{
	Invalidate(); // マウス移動時に再描画
	return Default();
}

LRESULT CCustomSliderCtrl::OnLButtonDownMsg(WPARAM wParam, LPARAM lParam)
{
	LRESULT result = Default();
	Invalidate();
	return result;
}

LRESULT CCustomSliderCtrl::OnLButtonUpMsg(WPARAM wParam, LPARAM lParam)
{
	LRESULT result = Default();
	Invalidate();
	return result;
}

void CCustomSliderCtrl::DrawSlider(CDC* pDC)
{
	CRect rect;
	GetClientRect(&rect);

	int nMin, nMax;
	GetRange(nMin, nMax);
	int nPos = GetPos();

	// 範囲が無効な場合は描画しない
	if (nMax <= nMin) return;

	if (m_nMode == 0)
	{
		DrawMode0(pDC, rect, nMin, nMax, nPos);
	}
	else
	{
		DrawMode1(pDC, rect, nMin, nMax, nPos);
	}
}

// ----------------------------------------------------------------------------
// Mode 0: オーディオボリューム風
// 改修：黒部分を廃止し、指定されたパステルカラーで構成
// ----------------------------------------------------------------------------
void CCustomSliderCtrl::DrawMode0(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
	int nRange = nMax - nMin;

	int nMarginX = 12;
	int nMarginY = 4;

	// トラック領域
	int nTrackLeft = rect.left + nMarginX;
	int nTrackRight = rect.right - nMarginX;
	int nTrackWidth = nTrackRight - nTrackLeft;
	if (nTrackWidth <= 0) return;

	// つまみの位置計算
	int nThumbX = nTrackLeft + (int)((double)(nPos - nMin) * nTrackWidth / nRange);

	// 三角形の頂点計算
	int nMinHeight = 2; // 左端の高さ
	int nMaxHeight = (rect.Height() - nMarginY * 2) / 2; // 右端の高さ
	int nCenterY = rect.Height() / 2;
	int nBottomY = rect.bottom - nMarginY - 4;

	CPoint pts[4];
	pts[0] = CPoint(nTrackLeft, nBottomY); // 左下
	pts[1] = CPoint(nTrackLeft, nBottomY - 2); // 左上（最小高さ）
	pts[2] = CPoint(nTrackRight, rect.top + nMarginY); // 右上（最大高さ）
	pts[3] = CPoint(nTrackRight, nBottomY); // 右下

	// 1. 背景のくさび形（くぼみ）を描画
	{
		CBrush brBack(COLOR_RANGE_SELECTION);
		CPen penNull(PS_NULL, 0, RGB(0, 0, 0));
		CBrush* pOldBr = pDC->SelectObject(&brBack);
		CPen* pOldPen = pDC->SelectObject(&penNull);
		pDC->Polygon(pts, 4);

		pDC->SelectObject(pOldBr);
		pDC->SelectObject(pOldPen);

		// 枠線を描画（くぼみの輪郭）
		CPen penFrame(PS_SOLID, 1, COLOR_SLIDER_THUMB);
		pDC->SelectObject(&penFrame);
		pDC->SelectObject(GetStockObject(NULL_BRUSH));
		pDC->Polygon(pts, 4);
	}

	// 2. アクティブ部分（左側）の塗りつぶし
	if (nThumbX > nTrackLeft)
	{
		CRgn rgnPoly, rgnLeft;
		rgnPoly.CreatePolygonRgn(pts, 4, WINDING);
		rgnLeft.CreateRectRgn(rect.left, rect.top, nThumbX, rect.bottom);
		rgnPoly.CombineRgn(&rgnPoly, &rgnLeft, RGN_AND);

		CBrush brActive(COLOR_LIST_BG);
		pDC->FillRgn(&rgnPoly, &brActive);
	}

	// 3. つまみの描画
	// 長方形
	{
		int nThumbW = 10;
		int nThumbH = rect.Height() - 6;
		CRect rcThumb(nThumbX - nThumbW / 2, nCenterY - nThumbH / 2, nThumbX + nThumbW / 2, nCenterY + nThumbH / 2);

		// つまみ本体：COLOR_SLIDER_THUMB (紫)
		CBrush brThumb(COLOR_SLIDER_THUMB);
		pDC->FillRect(&rcThumb, &brThumb);

		// 枠線：少し引き締めるため黒（COLOR_EDIT_TEXT）を使用するか、そのまま紫のまま立体感を出すか。
		// ここでは定義にある COLOR_EDIT_TEXT を使い、くっきりさせます。
		pDC->DrawEdge(&rcThumb, EDGE_RAISED, BF_RECT); // 立体枠

		// つまみの中央線（滑り止め風）
		// 紫の上のアクセントとして COLOR_RANGE_SLIDER_THUMB (白) を使用
		CPen penGrip(PS_SOLID, 1, COLOR_RANGE_SLIDER_THUMB);
		CPen* pOldPen = pDC->SelectObject(&penGrip);
		pDC->MoveTo(nThumbX, rcThumb.top + 3);
		pDC->LineTo(nThumbX, rcThumb.bottom - 3);
		pDC->SelectObject(pOldPen);
	}
}

// ----------------------------------------------------------------------------
// Mode 1: 目盛り付きリニアスライダー
// 改修：定義色に合わせて調整
// ----------------------------------------------------------------------------
void CCustomSliderCtrl::DrawMode1(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos)
{
	int nRange = nMax - nMin;
	int nCenterY = rect.Height() / 2;
	int nTrackLeft = 12;
	int nTrackRight = rect.Width() - 12;
	int nTrackWidth = nTrackRight - nTrackLeft;

	if (nTrackWidth <= 0) return;

	int nThumbPos = nTrackLeft + (int)((double)(nPos - nMin) * nTrackWidth / nRange);

	// 1. 軌道（ライン）の描画
	{
		// 左側（アクティブ）：太線固定
		// 紫 (COLOR_SLIDER_THUMB) を使用して強調
		CPen penActive(PS_SOLID, 4, COLOR_SLIDER_THUMB);
		CPen* pOldPen = pDC->SelectObject(&penActive);
		pDC->MoveTo(nTrackLeft, nCenterY);
		pDC->LineTo(nThumbPos, nCenterY);

		// 右側（非アクティブ）：細線
		// 薄い青 (COLOR_LIST_BG) または 薄い緑 (COLOR_BUTTON_BG)
		// Mode 0の背景と合わせて薄い青にします
		CPen penInactive(PS_SOLID, 2, COLOR_LIST_BG);
		pDC->SelectObject(&penInactive);
		pDC->MoveTo(nThumbPos, nCenterY);
		pDC->LineTo(nTrackRight, nCenterY);
		pDC->SelectObject(pOldPen);
	}

	// 2. 目盛りの描画
	{
		// 目盛りは黒 (COLOR_EDIT_TEXT) でくっきりと
		CPen penTick(PS_SOLID, 1, COLOR_EDIT_TEXT);
		CPen* pOldPen = pDC->SelectObject(&penTick);

		for (int i = 0; i <= 10; i++)
		{
			int nTickX = nTrackLeft + (nTrackWidth * i / 10);
			int nTickH = (i == 0 || i == 5 || i == 10) ? 8 : 4;

			pDC->MoveTo(nTickX, nCenterY - nTickH);
			pDC->LineTo(nTickX, nCenterY + nTickH);
		}
		pDC->SelectObject(pOldPen);
	}

	// 3. つまみの描画（ホームベース型）
	{
		int nThumbW = 14;
		int nThumbH = 18;

		CPoint pts[5];
		pts[0] = CPoint(nThumbPos, nCenterY + 4);
		pts[1] = CPoint(nThumbPos - nThumbW / 2, nCenterY - 4);
		pts[2] = CPoint(nThumbPos - nThumbW / 2, nCenterY - nThumbH / 2 - 2);
		pts[3] = CPoint(nThumbPos + nThumbW / 2, nCenterY - nThumbH / 2 - 2);
		pts[4] = CPoint(nThumbPos + nThumbW / 2, nCenterY - 4);

		// つまみの色：COLOR_SLIDER_THUMB (紫)
		CBrush brThumb(COLOR_SLIDER_THUMB);
		CBrush* pOldBr = pDC->SelectObject(&brThumb);

		// 枠線：黒 (COLOR_EDIT_TEXT)
		CPen penFrame(PS_SOLID, 1, COLOR_EDIT_TEXT);
		CPen* pOldPen = pDC->SelectObject(&penFrame);

		pDC->Polygon(pts, 5);

		// つまみの中央に小さなアクセント（白丸）などを描画しても可愛いかもしれません
		// ここではシンプルに

		pDC->SelectObject(pOldBr);
		pDC->SelectObject(pOldPen);
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
	: m_nMin(0), m_nMax(100), m_nSelMin(0), m_nSelMax(100)
	, m_nDragTarget(0), m_bDragging(FALSE), m_nVisualPos(0)
	, m_nLogicalPos(0) // 自前管理用の初期値
{
}

CCustomRangeSliderCtrl::~CCustomRangeSliderCtrl() {}

void CCustomRangeSliderCtrl::PreSubclassWindow()
{
	CSliderCtrl::PreSubclassWindow();

	HMODULE hUxTheme = LoadLibrary(_T("UxTheme.dll"));
	if (hUxTheme)
	{
		typedef HRESULT(WINAPI* SETWINDOWTHEME)(HWND, LPCWSTR, LPCWSTR);
		SETWINDOWTHEME pfnSetWindowTheme = (SETWINDOWTHEME)GetProcAddress(hUxTheme, "SetWindowTheme");
		if (pfnSetWindowTheme) pfnSetWindowTheme(m_hWnd, L"", L"");
		FreeLibrary(hUxTheme);
	}

	int nMin, nMax;
	CSliderCtrl::GetRange(nMin, nMax);
	m_nMin = nMin; m_nMax = nMax;

	if (m_nSelMin == 0 && m_nSelMax == 0) {
		m_nSelMin = nMin; m_nSelMax = nMax;
	}

	// 初期位置を基底クラスから取得して同期
	m_nLogicalPos = CSliderCtrl::GetPos();
	m_nVisualPos = m_nLogicalPos;
}

// 【重要】SetPosを上書き。基底クラスのSetPosは呼ばず、自前変数を更新して全体再描画する。
// これにより「標準つまみサイズに基づいた部分的更新」による残像を回避する。
void CCustomRangeSliderCtrl::SetPos(int nPos)
{
	// 範囲制限
	if (nPos < m_nMin) nPos = m_nMin;
	if (nPos > m_nMax) nPos = m_nMax;

	m_nLogicalPos = nPos;
	m_nVisualPos = nPos; // 見た目も同期

	// 基底クラスにも一応通知しておく（メッセージ処理用）だが、描画には関係させない
	// CSliderCtrl::SetPos(nPos); // ←これは呼ぶと残像の元になるので呼ばない、または呼んだ後に強制全描画が必要

	// 強制的に全体を再描画（InvalidateではなくRedrawWindowで即時反映）
	if (::IsWindow(m_hWnd)) {
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
	}
}

// 【重要】GetPosも上書き
int CCustomRangeSliderCtrl::GetPos() const
{
	return m_nLogicalPos;
}

void CCustomRangeSliderCtrl::SetRange(int nMin, int nMax, BOOL bRedraw)
{
	m_nMin = nMin;
	m_nMax = nMax;

	m_nVisualPos = m_nLogicalPos;

	CSliderCtrl::SetRange(nMin, nMax, FALSE);

	if (bRedraw) {
		if (::IsWindow(m_hWnd)) {
			RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
		}
	}
}

void CCustomRangeSliderCtrl::SetSelection(int nMin, int nMax)
{
	m_nSelMin = nMin;
	m_nSelMax = nMax;
	if (m_nSelMin > m_nSelMax) {
		int t = m_nSelMin; m_nSelMin = m_nSelMax; m_nSelMax = t;
	}
	if (::IsWindow(m_hWnd)) {
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
	}
}

void CCustomRangeSliderCtrl::GetSelection(int& nMin, int& nMax) const
{
	nMin = max(m_nMin, min(m_nMax, m_nSelMin));
	nMax = max(m_nMin, min(m_nMax, m_nSelMax));
}

void CCustomRangeSliderCtrl::OnPaint()
{
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(&rect);

	CDC memDC;
	CBitmap memBmp;
	memDC.CreateCompatibleDC(&dc);
	memBmp.CreateCompatibleBitmap(&dc, rect.Width(), rect.Height());
	CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

	// 背景を完全に塗りつぶす
	memDC.FillSolidRect(&rect, COLOR_DIALOG_BG);

	DrawRangeSlider(&memDC);

	dc.BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);

	memDC.SelectObject(pOldBmp);
	memBmp.DeleteObject();
	memDC.DeleteDC();
}

BOOL CCustomRangeSliderCtrl::OnEraseBkgnd(CDC* pDC) { return TRUE; }

void CCustomRangeSliderCtrl::DrawRangeSlider(CDC* pDC)
{
	CRect rect;
	GetClientRect(&rect);
	int cy = rect.Height() / 2;

	// ドラッグ中はVisualPos、それ以外は自前管理のLogicalPosを使う
	// 基底クラスのGetPos()は使わない
	int currentPos = m_bDragging ? m_nVisualPos : m_nLogicalPos;

	int safeMin = max(m_nMin, min(m_nMax, m_nSelMin));
	int safeMax = max(m_nMin, min(m_nMax, m_nSelMax));
	int safePos = max(m_nMin, min(m_nMax, currentPos));

	int xMin = ValueToPixel(safeMin);
	int xMax = ValueToPixel(safeMax);
	int xPos = ValueToPixel(safePos);

	// トラック
	CPen penTrack(PS_SOLID, 4, RGB(200, 200, 200));
	CPen* pOldPen = pDC->SelectObject(&penTrack);
	pDC->MoveTo(14, cy);
	pDC->LineTo(rect.Width() - 14, cy);
	pDC->SelectObject(pOldPen);

	// 選択範囲
	if (xMax > xMin) {
		CRect rcSel(xMin, cy - 4, xMax, cy + 4);
		pDC->FillSolidRect(&rcSel, COLOR_RANGE_SELECTION);
	}

	CPen penBorder(PS_SOLID, 1, RGB(0, 0, 0));
	pDC->SelectObject(&penBorder);

	// つまみ(Min)
	CRect rcMin(xMin - 5, cy - 8, xMin + 5, cy + 8);
	pDC->FillSolidRect(&rcMin, COLOR_RANGE_SLIDER_THUMB);
	pDC->SelectObject(GetStockObject(NULL_BRUSH));
	pDC->Rectangle(&rcMin);

	// つまみ(Max)
	CRect rcMax(xMax - 5, cy - 8, xMax + 5, cy + 8);
	pDC->FillSolidRect(&rcMax, COLOR_RANGE_SLIDER_THUMB);
	pDC->Rectangle(&rcMax);

	// つまみ(Main)
	CRect rcMain(xPos - 8, cy - 12, xPos + 8, cy + 12);
	pDC->FillSolidRect(&rcMain, COLOR_SLIDER_THUMB);
	pDC->Rectangle(&rcMain);

	pDC->SelectObject(pOldPen);
}

int CCustomRangeSliderCtrl::ValueToPixel(int nValue) const
{
	CRect rect; GetClientRect(&rect);
	int w = rect.Width() - 28;
	if (w <= 0) return 14;
	int range = m_nMax - m_nMin;
	if (range <= 0) return 14;

	int val = max(m_nMin, min(m_nMax, nValue));
	return 14 + (int)((long long)(val - m_nMin) * w / range);
}

int CCustomRangeSliderCtrl::PixelToValue(int nPixel) const
{
	CRect rect; GetClientRect(&rect);
	int w = rect.Width() - 28;
	if (w <= 0) return m_nMin;
	int x = max(14, min(rect.Width() - 14, nPixel));
	return m_nMin + (int)((double)(x - 14) / w * (m_nMax - m_nMin) + 0.5);
}

int CCustomRangeSliderCtrl::HitTest(CPoint point) const
{
	CRect rect; GetClientRect(&rect);
	int cy = rect.Height() / 2;
	int currentPos = m_bDragging ? m_nVisualPos : m_nLogicalPos;

	int xMain = ValueToPixel(currentPos);
	if (CRect(xMain - 10, cy - 14, xMain + 10, cy + 14).PtInRect(point)) return 3;

	int xMax = ValueToPixel(max(m_nMin, min(m_nMax, m_nSelMax)));
	if (CRect(xMax - 7, cy - 10, xMax + 7, cy + 10).PtInRect(point)) return 2;

	int xMin = ValueToPixel(max(m_nMin, min(m_nMax, m_nSelMin)));
	if (CRect(xMin - 7, cy - 10, xMin + 7, cy + 10).PtInRect(point)) return 1;
	return 0;
}

void CCustomRangeSliderCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetFocus();
	m_nVisualPos = m_nLogicalPos; // 現在位置からスタート
	m_nDragTarget = HitTest(point);
	if (m_nDragTarget == 0) {
		int nVal = PixelToValue(point.x);
		m_nVisualPos = nVal;
		m_nDragTarget = 3;
	}
	m_bDragging = TRUE;
	SetCapture();

	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CCustomRangeSliderCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_bDragging) {
		m_bDragging = FALSE;
		ReleaseCapture();
		if (m_nDragTarget == 3 || m_nDragTarget == 0) {
			// ドラッグ終了、論理位置を更新
			m_nLogicalPos = m_nVisualPos;

			// 親へ通知
			GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, m_nLogicalPos), (LPARAM)m_hWnd);
			GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, 0), (LPARAM)m_hWnd);
		}
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
	}
}

void CCustomRangeSliderCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_bDragging) {
		int nVal = PixelToValue(point.x);
		if (m_nDragTarget == 3) {
			m_nVisualPos = max(m_nMin, min(m_nMax, nVal));
			// 親へリアルタイム通知
			GetParent()->SendMessage(WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, m_nVisualPos), (LPARAM)m_hWnd);
		}
		else if (m_nDragTarget == 1) {
			m_nSelMin = min(nVal, max(m_nMin, min(m_nMax, m_nSelMax)));
		}
		else if (m_nDragTarget == 2) {
			m_nSelMax = max(nVal, max(m_nMin, min(m_nMax, m_nSelMin)));
		}

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
	: m_bIsFlatStyle(FALSE), m_bIsPressed(FALSE), m_bIsHot(FALSE), m_bTracking(FALSE)
	, m_nCheck(0)
{
}

CCustomCheckBox::~CCustomCheckBox()
{
}

void CCustomCheckBox::SetFont(CFont* pFont, BOOL bRedraw)
{
	CButton::SetFont(pFont, bRedraw);
}

int CCustomCheckBox::GetCheck() { return m_nCheck; }
void CCustomCheckBox::SetCheck(int nCheck) {
	m_nCheck = nCheck;
	CButton::SetCheck(nCheck);
	Invalidate();
}

void CCustomCheckBox::PreSubclassWindow()
{
	HMODULE hUxTheme = LoadLibrary(_T("UxTheme.dll"));
	if (hUxTheme) {
		typedef HRESULT(WINAPI* SETWINDOWTHEME)(HWND, LPCWSTR, LPCWSTR);
		SETWINDOWTHEME pfnSetWindowTheme = (SETWINDOWTHEME)GetProcAddress(hUxTheme, "SetWindowTheme");
		if (pfnSetWindowTheme) pfnSetWindowTheme(m_hWnd, L"", L"");
		FreeLibrary(hUxTheme);
	}

	LONG lStyle = GetStyle();
	m_bIsFlatStyle = (lStyle & BS_FLAT) || (lStyle & BS_PUSHLIKE);
	m_nCheck = CButton::GetCheck();

	ModifyStyle(BS_TYPEMASK | BS_FLAT | BS_PUSHLIKE, BS_OWNERDRAW);
	CButton::PreSubclassWindow();
}

void CCustomCheckBox::OnLButtonDown(UINT nFlags, CPoint point)
{
	m_bIsPressed = TRUE;
	m_bIsHot = TRUE;
	SetCapture();
	Invalidate();
}

void CCustomCheckBox::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_bIsPressed) {
		m_bIsPressed = FALSE;
		ReleaseCapture();
		CRect rect; GetClientRect(&rect);
		if (rect.PtInRect(point)) {
			m_nCheck = (m_nCheck == BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED;
			CButton::SetCheck(m_nCheck);
			CWnd* pParent = GetParent();
			if (pParent) pParent->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), BN_CLICKED), (LPARAM)m_hWnd);
		}
		Invalidate();
	}
}

void CCustomCheckBox::OnMouseMove(UINT nFlags, CPoint point)
{
	if (!m_bTracking) {
		TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hWnd, 0 };
		TrackMouseEvent(&tme);
		m_bTracking = TRUE;
	}
	CRect rect; GetClientRect(&rect);
	BOOL bHot = rect.PtInRect(point);
	if (m_bIsHot != bHot) {
		m_bIsHot = bHot;
		Invalidate();
	}
}

void CCustomCheckBox::OnMouseLeave()
{
	m_bIsHot = FALSE;
	m_bTracking = FALSE;
	Invalidate();
}

// 描画メッセージ
void CCustomCheckBox::OnPaint()
{
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(&rect);
	OnDrawLayer(&dc, rect);
}

// 印刷クライアントメッセージ（テーマ描画などで呼ばれることがある）
LRESULT CCustomCheckBox::OnPrintClient(WPARAM wParam, LPARAM lParam)
{
	CDC* pDC = CDC::FromHandle((HDC)wParam);
	CRect rect;
	GetClientRect(&rect);
	OnDrawLayer(pDC, rect);
	return 0;
}

BOOL CCustomCheckBox::OnEraseBkgnd(CDC* pDC)
{
	return TRUE; // OnPaintで背景を描くのでちらつき防止のため何もしない
}

void CCustomCheckBox::OnDrawLayer(CDC* pDC, CRect rect)
{
	BOOL bChecked = (m_nCheck == BST_CHECKED);
	BOOL bDisabled = !IsWindowEnabled();
	BOOL bPressed = m_bIsPressed && m_bIsHot;

	CFont* pFont = GetFont();
	CFont* pOldFont = NULL;
	if (pFont) pOldFont = pDC->SelectObject(pFont);
	else pOldFont = (CFont*)pDC->SelectStockObject(DEFAULT_GUI_FONT);

	if (m_bIsFlatStyle) {
		// --- フラットスタイル（プッシュライク）の描画 ---
		BOOL bSunken = bChecked || bPressed;
		COLORREF bg;
		if (bDisabled) bg = RGB(200, 200, 200);
		else if (bSunken) bg = COLOR_BUTTON_PUSHED;
		else if (m_bIsHot) bg = COLOR_BUTTON_HOVER;
		else bg = COLOR_BUTTON_BG;

		pDC->FillSolidRect(&rect, bg);

		// 装飾
		if (!bDisabled) {
			DrawDecorations(pDC, rect, 0, bSunken);
		}

		// 枠線の描画
		pDC->Draw3dRect(&rect,
			bSunken ? RGB(100, 100, 100) : RGB(255, 255, 255),
			bSunken ? RGB(255, 255, 255) : RGB(100, 100, 100));

		// テキスト描画（スマートロジック）
		CString strText; GetWindowText(strText);
		DrawSmartText(pDC, rect, strText, bDisabled, bSunken);
	}
	else {
		// --- 通常のチェックボックススタイル ---
		// 背景クリア（親ダイアログの色に合わせるべきですが、ここでは簡易的に白か指定色）
		// Dialog背景色で塗りつぶし
		pDC->FillSolidRect(&rect, COLOR_DIALOG_BG);

		int nSize = 16;
		int cy = rect.Height() / 2;

		// 左端から配置
		CRect rcBox(rect.left, cy - nSize / 2, rect.left + nSize, cy + nSize / 2);

		CPen pen(PS_SOLID, 1, RGB(255, 140, 100));
		CBrush br(RGB(255, 255, 255));
		CPen* pOldPenBox = pDC->SelectObject(&pen);
		CBrush* pOldBrBox = pDC->SelectObject(&br);

		pDC->RoundRect(&rcBox, CPoint(4, 4));

		pDC->SelectObject(pOldPenBox);
		pDC->SelectObject(pOldBrBox);

		if (bChecked) {
			// 花丸（お嬢様のご指定のベジェ曲線）
			CPen penHanamaru(PS_SOLID, 2, COLOR_HANAMARU);
			CPen* pOldPen = pDC->SelectObject(&penHanamaru);
			CPoint center = rcBox.CenterPoint();
			int r = 6;

			// ベジェ曲線は (3n + 1) 個の点が必要
			// 7点使用：Start(1) + Curve1(3) + Curve2(3) = 7点
			CPoint pts[] = {
				CPoint(center.x + r, center.y - r / 2),     // Start
				CPoint(center.x + r, center.y + r),         // C1
				CPoint(center.x - r, center.y + r),         // C2
				CPoint(center.x - r, center.y - r),         // End1 / Start2
				CPoint(center.x + r / 2, center.y - r),     // C3
				CPoint(center.x + r / 2, center.y + r / 2), // C4
				CPoint(center.x - r / 2, center.y + r / 2), // End2
			};

			pDC->PolyBezier(pts, 7);
			pDC->SelectObject(pOldPen);
		}

		CString strText; GetWindowText(strText);
		if (!strText.IsEmpty()) {
			// チェックボックスの横の文字もスマートロジックを適用しますが、
			// 領域がチェックボックスの分狭くなっていることに注意
			CRect rcText = rect;
			rcText.left = rcBox.right + 6;

			// チェックボックスの場合は基本的に左寄せ・一行が好ましいですが、
			// 要件に合わせてスマートロジック（縮小・折り返し）を適用します。
			DrawSmartText2(pDC, rcText, strText, DT_LEFT ,bDisabled, FALSE);
		}
	}

	if (GetFocus() == this) {
		CRect rcFocus = rect;
		if (!m_bIsFlatStyle) {
			rcFocus.left += 18; // ボックス分ずらす
		}
		else {
			rcFocus.DeflateRect(3, 3);
		}
		pDC->DrawFocusRect(&rcFocus);
	}

	if (pOldFont) pDC->SelectObject(pOldFont);
}

// ============================================================================
// CCustomGroupBox
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
	// BS_GROUPBOXスタイルを保持
}

void CCustomGroupBox::OnPaint()
{
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(&rect);
	DrawGroupBox(&dc, rect);
}

BOOL CCustomGroupBox::OnEraseBkgnd(CDC* pDC)
{
	return TRUE;
}

void CCustomGroupBox::DrawGroupBox(CDC* pDC, CRect& rect)
{
	CString strText;
	GetWindowText(strText);

	CFont* pFont = GetFont();
	CFont* pOldFont = NULL;
	if (pFont)
		pOldFont = pDC->SelectObject(pFont);

	CSize textSize(0, 0);
	if (!strText.IsEmpty())
	{
		textSize = pDC->GetTextExtent(strText);
	}

	// 太い枠線
	CPen penBlack(PS_SOLID, 2, RGB(0, 0, 0));
	CPen* pOldPen = pDC->SelectObject(&penBlack);
	pDC->SelectObject(GetStockObject(NULL_BRUSH));

	int nTextHeight = textSize.cy;
	int nTop = rect.top + nTextHeight / 2;

	// 上線
	pDC->MoveTo(rect.left + 1, nTop);
	if (textSize.cx > 0)
	{
		pDC->LineTo(rect.left + 6, nTop);
		pDC->MoveTo(rect.left + textSize.cx + 10, nTop);
	}
	pDC->LineTo(rect.right - 2, nTop);

	// 左線
	pDC->MoveTo(rect.left + 1, nTop);
	pDC->LineTo(rect.left + 1, rect.bottom - 2);

	// 右線
	pDC->MoveTo(rect.right - 2, nTop);
	pDC->LineTo(rect.right - 2, rect.bottom - 2);

	// 下線
	pDC->MoveTo(rect.left + 1, rect.bottom - 2);
	pDC->LineTo(rect.right - 2, rect.bottom - 2);

	if (!strText.IsEmpty())
	{
		CRect textRect = rect;
		textRect.top = nTop - 6;
		textRect.left += 8;
		textRect.bottom = nTop + 8;

		CBrush brDialog(COLOR_DIALOG_BG);
		CRect textBgRect = textRect;
		textBgRect.right = textBgRect.left + textSize.cx + 4;
		pDC->FillRect(&textBgRect, &brDialog);

		pDC->SetBkMode(TRANSPARENT);
		pDC->SetTextColor(RGB(0, 0, 0));
		pDC->DrawText(strText, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	}

	pDC->SelectObject(pOldPen);
	if (pOldFont)
		pDC->SelectObject(pOldFont);
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

		// 非表示のコントロールはスキップ
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
		::GetClassName(hWndChild, szClassName, 256);

		if (_tcsicmp(szClassName, _T("Button")) == 0)
		{
			LONG lStyle = ::GetWindowLong(hWndChild, GWL_STYLE);
			UINT nType = lStyle & BS_TYPEMASK;

			// デバッグ用: どのコントロールが何と判定されたか出力ウィンドウに表示
			CString strDbg;
			strDbg.Format(_T("Control ID: %d, Style: 0x%X, Type: 0x%X -> "), ::GetDlgCtrlID(hWndChild), lStyle, nType);

			// 1. 明らかにグループボックス(0x07)である場合
			if (nType == BS_GROUPBOX)
			{
				strDbg += _T("Group Box\n");
				CCustomGroupBox* pGroup = new CCustomGroupBox();
				pGroup->SubclassWindow(hWndChild);
			}
			// 2. それ以外で、プッシュボタンスタイルを持っている場合
			//    (BS_PUSHBUTTON=0x00, BS_DEFPUSHBUTTON=0x01, または BS_PUSHLIKE=0x1000)
			else if (nType == BS_PUSHBUTTON || nType == BS_DEFPUSHBUTTON || (lStyle & BS_PUSHLIKE))
			{
				strDbg += _T("Standard Button\n");
				CCustomStandardButton* pButton = new CCustomStandardButton();
				pButton->SubclassWindow(hWndChild);
			}
			// 3. 上記以外はすべてチェックボックス（またはラジオボタン）とみなす
			//    (BS_CHECKBOX=0x02, BS_AUTOCHECKBOX=0x03, BS_RADIOBUTTON=0x04 等)
			else
			{
				strDbg += _T("Check Box (Flowed here)\n");
				CCustomCheckBox* pCheck = new CCustomCheckBox();
				pCheck->SubclassWindow(hWndChild);
			}

			// 出力ウィンドウで確認してください
			::OutputDebugString(strDbg);
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
	// デフォルトの色を設定（サブクラス化されていないコントロール用）
	if (nCtlColor == CTLCOLOR_DLG)
	{
		return (HBRUSH)m_brDialog.GetSafeHandle();
	}

	// エディット
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

	// リストボックス
	if (nCtlColor == CTLCOLOR_LISTBOX)
	{
		pDC->SetBkColor(COLOR_COMBO_BG);
		pDC->SetTextColor(RGB(0, 0, 0));
		static CBrush brCombo(COLOR_COMBO_BG);
		return (HBRUSH)brCombo.GetSafeHandle();
	}

	// スタティック
	if (nCtlColor == CTLCOLOR_STATIC)
	{
		pDC->SetBkColor(COLOR_DIALOG_BG);
		pDC->SetTextColor(RGB(0, 0, 0));
		return (HBRUSH)m_brDialog.GetSafeHandle();
	}

	// ボタン（チェックボックス等）
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

		// 非表示のコントロールはスキップ
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
					// 通常のチェックボックスをCCustomCheckBoxでサブクラス化
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
	// デフォルトの色を設定（サブクラス化されていないコントロール用）
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