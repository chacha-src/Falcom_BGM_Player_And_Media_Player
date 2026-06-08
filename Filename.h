#pragma once
#include "afxwin.h"
#include "CCustomControl.h"

// CFilename ダイアログ

class CFilename : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CFilename)

public:
	CFilename(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CFilename();

// ダイアログ データ
	enum { IDD = IDD_FILENAME };
	playlistdata0 pc;
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート

	DECLARE_MESSAGE_MAP()
	cmnh();
public:
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedOk2();
	CCustomEdit m_name;
	CCustomEdit m_alb;
	CCustomEdit m_art;
	CCustomEdit m_fol;
	CCustomStandardButton m_cdummy;
	CCustomStandardButton mok;
	CCustomStatic m_lblName;
	CCustomStatic m_lblArt;
	CCustomStatic m_lblAlb;
	CCustomStatic m_lblFol;
};
