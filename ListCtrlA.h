#pragma once
#include "stdafx.h"
class CListCtrlA : public CListCtrl
{
	DECLARE_DYNAMIC(CListCtrlA)
	public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	CString strTipText;
		CHAR ff1[4096];
		WCHAR ff2[4096];
	protected:
	virtual void PreSubclassWindow();
	int CellRectFromPoint(CPoint & point, RECT * cellrect, int * col) const;
	virtual INT_PTR OnToolHitTest( CPoint point, TOOLINFO* pTI ) const;
	/// 行・列ホバー時のツールチップ本文（プレイリスト既定／KPI 一覧などで上書き）
	virtual void BuildToolTipText(int row, int col, CString& out);

public:
	CListCtrlA(void);
	~CListCtrlA(void);
	playlistdata0 *pc;
protected:
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg BOOL OnToolTipText( UINT id, NMHDR * pNMHDR, LRESULT * pResult );
	DECLARE_MESSAGE_MAP()

};
