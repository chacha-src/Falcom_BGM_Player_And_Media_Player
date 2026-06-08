#pragma once
#include "CCustomControl.h"

// CZeroFol ダイアログ

class CZeroFol : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CZeroFol)

public:
	CZeroFol(CWnd* pParent = NULL);   // 標準コンストラクター
	virtual ~CZeroFol();

// ダイアログ データ
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ZEROFOL };
#endif
	cmnh();
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート

	DECLARE_MESSAGE_MAP()
public:
	CCustomEdit m_fol;

	afx_msg void OnBnClickedFol();
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedOk();
	CCustomStandardButton m_okdummy;
	CCustomStandardButton m_okdummya;
	CCustomStatic m_msg;
};


