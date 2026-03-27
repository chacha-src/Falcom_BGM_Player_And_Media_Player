#pragma once

#include "afxwin.h"
#include "CCustomControl.h"

class CKpiV5ConfigDlg : public CCustomDialog
{
	DECLARE_DYNAMIC(CKpiV5ConfigDlg)

public:
	CKpiV5ConfigDlg(CWnd* pParent = NULL);
	virtual ~CKpiV5ConfigDlg();

	enum { IDD = IDD_KPI5CFG };
	cmnh();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	DECLARE_MESSAGE_MAP()

private:
	void BuildInitialText();
	bool ParseAndSave();

private:
	CCustomEdit m_text;
	CCustomStandardButton m_ok;
	CCustomStandardButton m_cancel;
public:
	CCustomStatic m_cccc;
};
