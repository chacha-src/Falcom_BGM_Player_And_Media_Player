#pragma once
#include "CCustomControl.h"

// CZwei ダイアログ

class CZwei : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CZwei)

public:
	CZwei(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CZwei();

// ダイアログ データ
	enum { IDD = IDD_SENTAKU12 };

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
	afx_msg void OnBnClickedOk();
	CCustomStandardButton m_okdummy;
};

// Zwei!! プレイリスト行の fol 復元用（ret2 = リスト選択インデックス）
CString ZweiFolFromIndex(int idx);
