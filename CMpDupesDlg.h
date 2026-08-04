#pragma once

#include "CCustomControl.h"
#include "resource.h"
#include <vector>

class CMpDupesDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CMpDupesDlg)
public:
	CMpDupesDlg(CWnd* pParent = NULL);
	virtual ~CMpDupesDlg();
	enum { IDD = IDD_MP_DUPES };

protected:
	CCustomListCtrl m_lc;
	CCustomStandardButton m_delete;
	CCustomStandardButton m_close;
	CBrush m_brDlg;
	std::vector<int> m_groupOf; // parallel to list rows: group id, or -1 for header

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedDelete();
	afx_msg void OnBnClickedClose();
	afx_msg void OnClose();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	void RebuildList();
	void UpdateStatus();

	DECLARE_MESSAGE_MAP()
};
