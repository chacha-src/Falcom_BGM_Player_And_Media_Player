#pragma once
#include "afxcmn.h"
#include "ListCtrlA.h"
#include "CCustomControl.h"

// KPI 一覧用リストビュー（行ツールチップ：パス／Ver／CPU／拡張子）
class CKpiListCtrl : public CCustomListCtrl
{
	DECLARE_DYNAMIC(CKpiListCtrl)
public:
	CKpiListCtrl() = default;
protected:
	void BuildToolTipText(int row, int col, CString& out) override;
	DECLARE_MESSAGE_MAP()
};

// CKpilist ダイアログ

class CKpilist : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CKpilist)

public:
	CKpilist(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CKpilist();
	void Init();
	void Save();
	int status;
// ダイアログ データ
	enum { IDD = IDD_KPI };
	cmnh();
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	CToolTipCtrl m_tooltip;

	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	CKpiListCtrl m_lc;
	afx_msg void OnLvnItemchangedList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnBnClickedOk();
	CCustomStandardButton m_okdummy;
	CCustomStatic m_desc;
};
