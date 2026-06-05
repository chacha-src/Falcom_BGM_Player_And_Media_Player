#pragma once
#include "CCustomControl.h"

// CArc ダイアログ

class CArc : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CArc)

public:
	CArc(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CArc();
	CString ret;
	int ret2;
	CCustomListBox	m_list;
	CString Gett(int a);

// ダイアログ データ
	enum { IDD = IDD_SENTAKU22 };
protected:
	cmnh();
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	afx_msg void OnDblclkList1();
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnPaint();
	afx_msg void OnBnClickedOk();
	CCustomStandardButton m_okdummy;

};
