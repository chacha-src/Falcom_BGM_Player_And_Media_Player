#pragma once
#include "CCustomControl.h"

// CYs12_2 ダイアログ

class CYs12_2 : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CYs12_2)

public:
	CYs12_2(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CYs12_2();

// ダイアログ データ
	enum { IDD = IDD_SENTAKU11 };

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
