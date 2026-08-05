#pragma once

#include "CCustomControl.h"
#include "CLyricsViewWnd.h"
#include "resource.h"

// デスクトップ常時最前面の歌詞オーバーレイ(モードレス・シングルトン)
// 透過度は WS_EX_LAYERED + LWA_ALPHA（アクリル帯とは併用不可）
class CDesktopLyricsWnd : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CDesktopLyricsWnd)
public:
	enum { IDD = IDD_DESKTOP_LYRICS };
	enum { kViewChildId = 3870 };

	CDesktopLyricsWnd(CWnd* pParent = NULL);
	virtual ~CDesktopLyricsWnd();

	void SyncFromOg();
	void PersistGeometry();
	void ApplyWindowAlpha();
	void MakeSolidClient();
	void SetDeskLrcAlpha(int a, BOOL syncSlider);

protected:
	CLyricsViewWnd m_view;
	CCustomSliderCtrl m_alpha;
	CCustomStatic m_alphaL;
	CCustomStandardButton m_close;
	BOOL m_dragLyrics;
	CPoint m_dragOff;
	CToolTipCtrl m_tooltip;

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual void PostNcDestroy();
	virtual void OnCancel();
	virtual void ApplyDwmBlur();
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnDestroy();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	DECLARE_MESSAGE_MAP()

private:
	void LayoutClient();
	void ShowDeskLrcMenu(CPoint screenPt);
};

void OpenDesktopLyricsModeless(CWnd* pParent);
void CloseDesktopLyricsIfOpen();
void SyncDesktopLyricsIfOpen();

