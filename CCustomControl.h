#pragma once
#include "stdafx.h"
#include "afxdialogex.h"
#include "BtnST.h"
#include "ListCtrlA.h"
#include "DwmBlurHelper.h"
#include "OSVersion.h"
#include <map>

// 色定義
#define COLOR_DIALOG_BG RGB(255, 225, 235)      // 薄いピンク
#define COLOR_EDIT_BG RGB(255, 222, 210)        // 薄いオレンジ
#define COLOR_EDIT_TEXT RGB(0, 0, 0)            // 濃い太い黒
#define COLOR_LIST_BG RGB(173, 216, 230)        // 薄い青（ライトブルー）
#define COLOR_COMBO_BG RGB(255, 222, 210)       // 薄いオレンジ
#define COLOR_BUTTON_BG RGB(200, 232, 190)      // 薄い緑
#define COLOR_BUTTON_PUSHED RGB(60, 160, 60)
#define COLOR_BUTTON_HOVER RGB(100, 200, 100)   // マウスオーバー時の緑
#define COLOR_SLIDER_THUMB RGB(128, 0, 128)     // 紫
#define COLOR_RANGE_SLIDER_THUMB RGB(255, 255, 255) // 白
#define COLOR_RANGE_SELECTION RGB(221, 160, 221) // うすい紫
#define COLOR_HANAMARU RGB(255, 0, 0)
#define COLOR_FLOWER_DECO        RGB(255, 240, 245) // 装飾用の淡い色
#define COLOR_VINE_DECO          RGB(80, 140, 80)   // 蔓の色
#define COLOR_HEART         RGB(255, 105, 180) // 華やかなピンク
#define COLOR_SEL_BG        RGB(221, 160, 221) // 薄い紫

// ダイアログから色を変更するためのユーティリティクラス
class CCustomControlUtility
{
public:
	// OnCtlColorで呼び出すヘルパー関数
	// 使用例: return CCustomControlUtility::SetControlColor(pDC, pWnd, nCtlColor, RGB(255,0,0), RGB(0,0,0));
	static HBRUSH SetControlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor, COLORREF clrBackground, COLORREF clrText = RGB(0, 0, 0))
	{
		pDC->SetBkColor(clrBackground);
		pDC->SetTextColor(clrText);

		static CBrush brushes[16];
		static COLORREF colors[16];
		static int nBrushCount = 0;

		// 既存のブラシを検索
		for (int i = 0; i < nBrushCount; i++)
		{
			if (colors[i] == clrBackground && brushes[i].GetSafeHandle())
			{
				return (HBRUSH)brushes[i].GetSafeHandle();
			}
		}

		// 新しいブラシを作成
		if (nBrushCount < 16)
		{
			if (brushes[nBrushCount].GetSafeHandle())
				brushes[nBrushCount].DeleteObject();
			brushes[nBrushCount].CreateSolidBrush(clrBackground);
			colors[nBrushCount] = clrBackground;
			nBrushCount++;
			return (HBRUSH)brushes[nBrushCount - 1].GetSafeHandle();
		}

		// 最後のブラシを上書き
		if (brushes[15].GetSafeHandle())
			brushes[15].DeleteObject();
		brushes[15].CreateSolidBrush(clrBackground);
		colors[15] = clrBackground;
		return (HBRUSH)brushes[15].GetSafeHandle();
	}

	// コントロールの背景色を設定（コントロール変数と色を渡すだけ）
	// 使用例: CCustomControlUtility::SetControlBackgroundColor(&m_editCtrl, RGB(255, 255, 0));
	// 注意: 親ダイアログのOnCtlColorでApplyControlColors()を呼び出す必要があります
	static void SetControlBackgroundColor(CWnd* pControl, COLORREF clrBackground, COLORREF clrText = RGB(0, 0, 0))
	{
		if (pControl && pControl->GetSafeHwnd())
		{
			HWND hWnd = pControl->GetSafeHwnd();

			// マップに色を保存
			GetControlColorMap()[hWnd].clrBackground = clrBackground;
			GetControlColorMap()[hWnd].clrText = clrText;

			// 再描画
			pControl->Invalidate();
			pControl->UpdateWindow();
		}
	}

	// コントロールの背景色をクリア
	static void ClearControlBackgroundColor(CWnd* pControl)
	{
		if (pControl && pControl->GetSafeHwnd())
		{
			HWND hWnd = pControl->GetSafeHwnd();
			GetControlColorMap().erase(hWnd);

			// 再描画
			pControl->Invalidate();
			pControl->UpdateWindow();
		}
	}

	// 親ダイアログのOnCtlColorで呼び出す関数
	// 使用例:
	// HBRUSH CYourDialog::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
	// {
	//     HBRUSH hbr = CCustomControlUtility::ApplyControlColors(pDC, pWnd, nCtlColor);
	//     if (hbr) return hbr;
	//     return CCustomDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	// }
	static HBRUSH ApplyControlColors(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
	{
		if (pWnd && pWnd->GetSafeHwnd())
		{
			HWND hWnd = pWnd->GetSafeHwnd();

			// マップから色を取得
			auto it = GetControlColorMap().find(hWnd);
			if (it != GetControlColorMap().end())
			{
				return SetControlColor(pDC, pWnd, nCtlColor, it->second.clrBackground, it->second.clrText);
			}
		}

		return NULL;
	}

private:
	struct ControlColors
	{
		COLORREF clrBackground;
		COLORREF clrText;
	};

	// 色マップのシングルトン
	static std::map<HWND, ControlColors>& GetControlColorMap()
	{
		static std::map<HWND, ControlColors> s_colorMap;
		return s_colorMap;
	}
};

// カスタムコントロールクラス定義

// カスタムエディットボックス
class CCustomEdit : public CEdit
{
	DECLARE_DYNAMIC(CCustomEdit)

public:
	CCustomEdit();
	virtual ~CCustomEdit();

protected:
	virtual void PreSubclassWindow();
	afx_msg HBRUSH CtlColor(CDC* pDC, UINT nCtlColor);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnNcPaint();
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);

	DECLARE_MESSAGE_MAP()

private:
	CBrush m_brBackground;
	CFont m_fontBold;
	BOOL m_bHasFocus;
};

// カスタムスタティック
class CCustomStatic : public CStatic
{
	DECLARE_DYNAMIC(CCustomStatic)

public:
	CCustomStatic();
	virtual ~CCustomStatic();

	void SetFont(CFont* pFont, BOOL bRedraw = TRUE);

protected:
	virtual void PreSubclassWindow();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);

	DECLARE_MESSAGE_MAP()

private:
	CFont m_font;
};

// カスタムリストボックス
class CCustomListBox : public CListBox
{
	DECLARE_DYNAMIC(CCustomListBox)

public:
	CCustomListBox();
	virtual ~CCustomListBox();

protected:
	virtual void PreSubclassWindow();
	afx_msg HBRUSH CtlColor(CDC* pDC, UINT nCtlColor);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	virtual void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct);

	DECLARE_MESSAGE_MAP()

private:
	CBrush m_brBackground;
};

// カスタムコンボボックス
class CCustomComboBox : public CComboBox
{
	DECLARE_DYNAMIC(CCustomComboBox)

public:
	CCustomComboBox();
	virtual ~CCustomComboBox();

protected:
	CBrush m_brBackground;
	void UpdateDropDownWidth();

	virtual void PreSubclassWindow();
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	virtual void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct);

	afx_msg HBRUSH CtlColor(CDC* pDC, UINT nCtlColor);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnDropdown(); // 展開時に幅を調整

	DECLARE_MESSAGE_MAP()
};

// カスタムリストビュー
class CCustomListCtrl : public CListCtrlA
{
	DECLARE_DYNAMIC(CCustomListCtrl)

public:
	CCustomListCtrl();
	virtual ~CCustomListCtrl();

protected:
	virtual void PreSubclassWindow();
	afx_msg HBRUSH CtlColor(CDC* pDC, UINT nCtlColor);
	afx_msg void OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);  // マウス移動処理
	afx_msg void OnMouseLeave();                          // マウス離脱処理
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);                  // 背景消去処理
	afx_msg void OnPaint();                                // 描画処理

	DECLARE_MESSAGE_MAP()

private:
	CBrush m_brBackground;
	int m_nHotItem;  // ホバー中のアイテム番号
};

// 通常ボタン用
class CCustomStandardButton : public CButton
{
	DECLARE_DYNAMIC(CCustomStandardButton)

public:
	CCustomStandardButton();
	virtual ~CCustomStandardButton();

protected:
	virtual void PreSubclassWindow();
	afx_msg HBRUSH CtlColor(CDC* pDC, UINT nCtlColor);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg LRESULT OnMouseLeave(WPARAM wParam, LPARAM lParam);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnEnable(BOOL bEnable);

	DECLARE_MESSAGE_MAP()

private:
	CBrush m_brBackground;
	BOOL m_bMouseOver;
};

class CCustomSliderCtrl : public CSliderCtrl
{
	DECLARE_DYNAMIC(CCustomSliderCtrl)

public:
	CCustomSliderCtrl();
	virtual ~CCustomSliderCtrl();

	// モード設定 (0: オーディオ風, 1: 目盛り付き)
	void SetMode(int nMode);
	int GetMode() const { return m_nMode; }
	void SetPos(int nPos, BOOL bRedraw = TRUE);

protected:
	int m_nMode; // 現在の描画モード

	virtual void PreSubclassWindow();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg LRESULT OnMouseMoveMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnLButtonDownMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnLButtonUpMsg(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	// 描画処理
	void DrawSlider(CDC* pDC);
	void DrawMode0(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos); // オーディオモード
	void DrawMode1(CDC* pDC, const CRect& rect, int nMin, int nMax, int nPos); // リニアモード
};

// カスタム範囲指定スライダー
class CCustomRangeSliderCtrl : public CSliderCtrl
{
	DECLARE_DYNAMIC(CCustomRangeSliderCtrl)

public:
	CCustomRangeSliderCtrl();
	virtual ~CCustomRangeSliderCtrl();

	void SetRange(int nMin, int nMax, BOOL bRedraw = TRUE);
	void SetSelection(int nMin, int nMax);
	void GetSelection(int& nMin, int& nMax) const;

	void SetPos(int nPos);
	int GetPos() const;

protected:
	virtual void PreSubclassWindow();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);

	DECLARE_MESSAGE_MAP()

private:
	void DrawRangeSlider(CDC* pDC);
	int ValueToPixel(int nValue) const;
	int PixelToValue(int nPixel) const;
	int HitTest(CPoint point) const;

	int m_nMin, m_nMax;
	int m_nSelMin, m_nSelMax;
	int m_nDragTarget;
	int m_nLogicalPos;
	BOOL m_bDragging;
	int m_nVisualPos;
};

// カスタムチェックボックス
class CCustomCheckBox : public CButton
{
	DECLARE_DYNAMIC(CCustomCheckBox)

public:
	CCustomCheckBox();
	virtual ~CCustomCheckBox();

	void SetFont(CFont* pFont, BOOL bRedraw = TRUE);
	int GetCheck();
	void SetCheck(int nCheck);

protected:
	virtual void PreSubclassWindow();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg LRESULT OnPrintClient(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	void OnDrawLayer(CDC* pDC, CRect rect);

	BOOL m_bIsFlatStyle;
	BOOL m_bIsPressed;
	BOOL m_bIsHot;
	BOOL m_bTracking;
	int m_nCheck;
};

// カスタムグループボックス
class CCustomGroupBox : public CButton
{
	DECLARE_DYNAMIC(CCustomGroupBox)

public:
	CCustomGroupBox();
	virtual ~CCustomGroupBox();

protected:
	virtual void PreSubclassWindow();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);

	DECLARE_MESSAGE_MAP()

private:
	void DrawGroupBox(CDC* pDC, CRect& rect);
};

// カスタムダイアログ
class CCustomDialog : public CDialog
{
	DECLARE_DYNAMIC(CCustomDialog)

public:
	CCustomDialog();
	CCustomDialog(UINT nIDTemplate, CWnd* pParentWnd = NULL);
	virtual ~CCustomDialog();

protected:
	virtual BOOL OnInitDialog();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnPaint();
	afx_msg LRESULT OnSubclassControls(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	CBrush m_brDialog;
	void SubclassChildControls();
};

// カスタムBlurDialogBase
class CCustomBlurDialogBase : public CCustomDialog
{
	DECLARE_DYNAMIC(CCustomBlurDialogBase)

public:
	CCustomBlurDialogBase();
	CCustomBlurDialogBase(UINT nIDTemplate, CWnd* pParent = NULL);
	virtual ~CCustomBlurDialogBase();

protected:
	virtual BOOL OnInitDialog();
	virtual int OnCreate(LPCREATESTRUCT lpCreateStruct);
	virtual void ApplyDwmBlur();

private:
	CBrush m_brDialog;
	BOOL m_bBlurApplied;
};

// カスタムDialogEx
class CCustomDialogEx : public CDialogEx
{
	DECLARE_DYNAMIC(CCustomDialogEx)

public:
	CCustomDialogEx();
	CCustomDialogEx(UINT nIDTemplate, CWnd* pParentWnd = NULL);
	virtual ~CCustomDialogEx();

protected:
	virtual BOOL OnInitDialog();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnPaint();
	afx_msg LRESULT OnSubclassControls(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

private:
	CBrush m_brDialog;
	void SubclassChildControls();
};

// カスタムBlurDialogExBase
class CCustomBlurDialogExBase : public CCustomDialogEx
{
	DECLARE_DYNAMIC(CCustomBlurDialogExBase)

public:
	CCustomBlurDialogExBase();
	CCustomBlurDialogExBase(UINT nIDTemplate, CWnd* pParent = nullptr);
	virtual ~CCustomBlurDialogExBase();

protected:
	virtual BOOL OnInitDialog();
	virtual int OnCreate(LPCREATESTRUCT lpCreateStruct);
	virtual void ApplyDwmBlur();

private:
	CBrush m_brDialog;
	BOOL m_bBlurApplied;
};