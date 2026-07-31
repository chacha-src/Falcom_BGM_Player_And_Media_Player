#pragma once

#include "CCustomControl.h"
#include "ListCtrlA.h"
#include "resource.h"
#include <vector>

class CPlayList;

struct MissingFileItem {
	int plIndex;
	CString name;
	CString path;
	int sub;
};

// パス列のツールチップ用（CCustomListCtrl を壊さない派生）
class CMissingFilesListCtrl : public CCustomListCtrl
{
	DECLARE_DYNAMIC(CMissingFilesListCtrl)
public:
	std::vector<MissingFileItem>* m_pItems = nullptr;
protected:
	virtual void BuildToolTipText(int row, int col, CString& out);
	DECLARE_MESSAGE_MAP()
};

class CMissingFilesDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CMissingFilesDlg)
public:
	CMissingFilesDlg(CPlayList* pPlayList, const std::vector<int>& missingIndices, CWnd* pParent = NULL);
	virtual ~CMissingFilesDlg();
	enum { IDD = IDD_MISSING_FILES };

	// IDOK で閉じたとき: 削除対象のプレイリスト行インデックス（昇順）
	std::vector<int> m_toDelete;

protected:
	CPlayList* m_pPlayList;
	std::vector<MissingFileItem> m_items;
	CMissingFilesListCtrl m_lc;
	CCustomEdit m_path;
	CCustomStandardButton m_browse;
	CCustomStandardButton m_apply;
	CCustomStandardButton m_openFol;
	CCustomStandardButton m_delete;
	CCustomStandardButton m_close;
	CBrush m_brDlg;
	int m_selRow;
	BOOL m_bInlineEdit;
	CCustomEdit m_inlineEdit;
	int m_inlineRow;

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnClose();
	afx_msg void OnBnClickedBrowse();
	afx_msg void OnBnClickedApply();
	afx_msg void OnBnClickedOpenFol();
	afx_msg void OnBnClickedDelete();
	afx_msg void OnBnClickedClose();
	afx_msg void OnLvnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNMDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNMClickList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnInlineEditKillFocus();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	void RebuildList();
	void FitPathColumn();
	void SyncPathEditFromSelection();
	void UpdateStatus();
	void UpdateButtonEnable();
	int GetSelectedRow() const;
	BOOL ApplyPathToRow(int row, const CString& newPath, BOOL showError);
	void RemoveRowAt(int row);
	void EndInlineEdit(BOOL commit);
	void StartInlineEdit(int row);
	CString BrowseForFile(const CString& initialPath);

	DECLARE_MESSAGE_MAP()
};
