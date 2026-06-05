#pragma once
#include "CCustomControl.h"

// CED4 ダイアログ

class CED4 : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CED4)

public:
	CED4(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CED4();
	CString ret;
	int ret2;
	CCustomListBox	m_list;
	CString Gett(int a);

// ダイアログ データ
	enum { IDD = IDD_SENTAKU18 };
	cmnh();
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	afx_msg void OnDblclkList1();
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
public:
	CCustomStandardButton m_okdummy;
};
