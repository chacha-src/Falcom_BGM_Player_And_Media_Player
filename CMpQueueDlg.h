#pragma once

#include "CCustomControl.h"
#include "resource.h"

class CMediaPlayerDlg;

class CMpQueueDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CMpQueueDlg)
public:
	CMpQueueDlg(CWnd* pParent = NULL);
	virtual ~CMpQueueDlg();
	enum { IDD = IDD_MP_QUEUE };

protected:
	CMediaPlayerDlg* m_mp;
	CCustomListCtrl m_lc;
	CCustomStandardButton m_up;
	CCustomStandardButton m_down;
	CCustomStandardButton m_remove;
	CCustomStandardButton m_clear;
	CCustomStandardButton m_close;
	CBrush m_brDlg;
	CToolTipCtrl m_tooltip;

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedUp();
	afx_msg void OnBnClickedDown();
	afx_msg void OnBnClickedRemove();
	afx_msg void OnBnClickedClear();
	afx_msg void OnBnClickedClose();
	afx_msg void OnClose();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	void RebuildList();
	int GetSelectedRow() const;

	DECLARE_MESSAGE_MAP()
};
