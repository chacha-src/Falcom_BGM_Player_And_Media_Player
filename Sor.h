#pragma once
#include "CCustomControl.h"

// CSor ダイアログ

class CSor : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CSor)

public:
	CSor(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CSor();

// ダイアログ データ
	enum { IDD = IDD_SENTAKU13 };

	CString ret;
	int ret2;
	CCustomListBox	m_list;
	CString Gett(int a);
	cmnh();
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	afx_msg void OnDblclkList1();
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
public:
	CCustomStandardButton m_okdummy;
};
