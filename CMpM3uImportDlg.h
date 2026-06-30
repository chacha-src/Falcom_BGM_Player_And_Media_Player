#pragma once

#include "CCustomControl.h"
#include "CMpPlaylistIO.h"
#include "resource.h"

class CMpM3uImportDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CMpM3uImportDlg)
public:
	CMpM3uImportDlg(CWnd* pParent = NULL);
	virtual ~CMpM3uImportDlg();
	enum { IDD = IDD_MP_M3U_IMPORT };

	CString m_filePath;
	MpM3uImportOptions m_opt;

protected:
	CCustomComboBox m_plsel;
	CCustomCheckBox m_utf8;
	CCustomCheckBox m_resolve;
	CCustomCheckBox m_skipMissing;
	CCustomCheckBox m_skipDup;
	CBrush m_brDlg;

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	afx_msg void OnBrowse();
	afx_msg void OnImport();
	afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	void ReloadPlaylistCombo();
	void LoadOptionsFromUi();
	DECLARE_MESSAGE_MAP()
};

// インポートダイアログを開く(ファイルパス省略可)。D&D 時は path を渡す。
BOOL MpShowM3uImportDialog(CWnd* pParent, const CString& pathHint = _T(""));
