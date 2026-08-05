#pragma once

#include "CCustomControl.h"
#include "resource.h"
#include <vector>

class CMpFolderSyncDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CMpFolderSyncDlg)
public:
	CMpFolderSyncDlg(CWnd* pParent, CString folder);
	virtual ~CMpFolderSyncDlg();
	enum { IDD = IDD_MP_FOLDER_SYNC };

protected:
	CString m_folder;
	CCustomListCtrl m_lcDisk; // on disk not in PL
	CCustomListCtrl m_lcPl;   // in PL not on disk
	CCustomStandardButton m_add;
	CCustomStandardButton m_remove;
	CCustomStandardButton m_close;
	CBrush m_brDlg;
	CToolTipCtrl m_tooltip;
	std::vector<CString> m_diskOnly;
	std::vector<int> m_plOnly; // pc indices

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedAdd();
	afx_msg void OnBnClickedRemove();
	afx_msg void OnBnClickedClose();
	afx_msg void OnClose();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	void Scan();
	void RebuildLists();
	void UpdateStatus();
	static BOOL IsAudioExt(const CString& path);
	static void CollectAudioFiles(const CString& folder, std::vector<CString>& out);

	DECLARE_MESSAGE_MAP()
};
