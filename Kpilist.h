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
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnDestroy();
	CCustomStandardButton m_okdummy;
	CCustomStatic m_desc;
private:
	// リサイズ時に子コントロールを再配置し、kpi/拡張子 列を自動フィットさせる
	void LayoutControls();
	void LayoutKpiColumns();
	// savedata に記録したウィンドウのサイズ・位置を復元/保存する
	void RestoreSavedPlacement();
	void SaveSavedPlacement();
	int m_minW = 0;   // 最小ウィンドウ幅(初期サイズ)
	int m_minH = 0;   // 最小ウィンドウ高さ(初期サイズ)
};
