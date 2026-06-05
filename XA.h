#pragma once
#include "CCustomControl.h"

// CXA ダイアログ

class CXA : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CXA)

public:
	CXA(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CXA();
	int ret,ret2;

// ダイアログ データ
	enum { IDD = IDD_SENTAKU9 };
	CCustomListBox	m_list;
	CString Gett(int a);
	int loop1,loop2;
	cmnh();
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	afx_msg void OnDblclkList1();
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
public:
	CCustomStandardButton m_okdummy;
};
