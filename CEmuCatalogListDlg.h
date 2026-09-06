#pragma once
#include "afxcmn.h"
#include "CCustomControl.h"
#include "cemu_types.h"

class CEmuCatListCtrl : public CCustomListCtrl
{
	DECLARE_DYNAMIC(CEmuCatListCtrl)
public:
	CEmuCatListCtrl() = default;
protected:
	void BuildToolTipText(int row, int col, CString& out) override;
	DECLARE_MESSAGE_MAP()
};

/* arcdata.zip 対応タイトル一覧（Kpilist と同型のアクリル＋リスト） */
class CEmuCatalogListDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CEmuCatalogListDlg)
public:
	CEmuCatalogListDlg(CWnd* pParent = NULL);
	virtual ~CEmuCatalogListDlg();
	enum { IDD = IDD_CEMU_CATLIST };
	static void ShowModal(CWnd* pParent);
	cmnh();
protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnEnChangeFilter();
	afx_msg void OnNMDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnDestroy();
	DECLARE_MESSAGE_MAP()

	void LayoutControls();
	void LayoutColumns();
	void LayoutHelpBtn();
	void ShowHelpSheet();
	void RestoreSavedPlacement();
	void SaveSavedPlacement();
	void FillList();
	int PlaySelectedRow();

	CEmuCatListCtrl m_lc;
	CCustomEdit m_filter;
	CCustomStatic m_filterLbl;
	CCustomStatic m_desc;
	CCustomStandardButton m_ok;
	CCustomStandardButton m_help;
	CToolTipCtrl m_tooltip;
	int m_minW = 0;
	int m_minH = 0;
	BOOL m_bFilling = FALSE;
};
