#pragma once
#include "afxwin.h"
#include "CCustomControl.h"

// CWavExport ダイアログ - プレイリストからWAVへ出力（再生なし）

class CWavExport : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CWavExport)

public:
	CWavExport(CWnd* pParent = NULL);
	virtual ~CWavExport();

	enum { IDD = IDD_WAVEXPORT };
	playlistdata0 pc;

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
};
