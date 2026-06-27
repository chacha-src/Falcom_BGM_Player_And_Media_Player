#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "oggDlg.h"
#include <vector>

// WavExportOptions は oggDlg.h で定義

class CWavExport : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CWavExport)

public:
	CWavExport(CWnd* pParent = NULL);
	virtual ~CWavExport();

	enum { IDD = IDD_WAVEXPORT };
	playlistdata0 pc;
	std::vector<playlistdata0> pcs;
	bool multiFile;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	DECLARE_MESSAGE_MAP()

public:
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedWavExportExec();
	afx_msg void OnBnClickedWavExportBrowse();
	afx_msg void OnBnClickedWavExportClose();

	CCustomEdit m_loop;
	CCustomEdit m_path;
	CCustomStatic m_status;
	CCustomStatic m_loopLabel;
	CCustomStatic m_pathLabel;
	CCustomStandardButton m_browse;
	CCustomStandardButton m_exec;
	CCustomStandardButton m_close;
	CCustomCheckBox m_fadeCheck;
	CCustomEdit m_fadeSec;
	CCustomStatic m_fadeLabel;
	CCustomCheckBox m_trimCheck;
	CCustomEdit m_trimSec;
	CCustomStatic m_trimLabel;
};
