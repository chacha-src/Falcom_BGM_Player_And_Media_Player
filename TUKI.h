#pragma once
#include "CCustomControl.h"

// CTUKI ダイアログ

class CTUKI : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CTUKI)

public:
	CTUKI(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CTUKI();
	CString ret;
	int ret2;
	CCustomListBox	m_list;
	CString Gett(int a);

// ダイアログ データ
	enum { IDD = IDD_SENTAKU20 };
	cmnh();
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	afx_msg void OnDblclkList1();
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
public:
	CCustomStandardButton m_okdummy;
};
