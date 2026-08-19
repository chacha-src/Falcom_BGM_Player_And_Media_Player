#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"

class CTagEditDlg;

class CTagBatchEditDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CTagBatchEditDlg)

public:
	CTagBatchEditDlg(CWnd* pParent = NULL);
	virtual ~CTagBatchEditDlg();

	enum { IDD = IDD_TAGBATCH };

	int* m_idx;
	int m_n;
	CTagEditDlg* m_te;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	DECLARE_MESSAGE_MAP()

public:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedApply();
	afx_msg void OnBnClickedClose();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnEnVScroll(UINT nID);
	afx_msg LRESULT OnPostSync(WPARAM wParam, LPARAM lParam);

	void LayoutHelpBtn();
	void LayoutAll();
	void ShowHelpSheet();
	void FillTexts();
	void SyncFrom(CWnd* src);
	void PersistPos();
	void RestorePos();
	CCustomEdit* EditAt(int i);

	CCustomStatic m_origL;
	CCustomStatic m_editL;
	CCustomStatic m_nameOL;
	CCustomStatic m_artOL;
	CCustomStatic m_albOL;
	CCustomStatic m_nameEL;
	CCustomStatic m_artEL;
	CCustomStatic m_albEL;
	CCustomEdit m_nameO;
	CCustomEdit m_artO;
	CCustomEdit m_albO;
	CCustomEdit m_nameE;
	CCustomEdit m_artE;
	CCustomEdit m_albE;
	CCustomStatic m_status;
	CCustomStandardButton m_apply;
	CCustomStandardButton m_close;
	CCustomStandardButton m_help;
	CToolTipCtrl m_tooltip;
	int m_syncing;
};
