#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"

// CPlayListNew ダイアログ

class CPlayListNew : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CPlayListNew)

public:
	CPlayListNew(CWnd* pParent = nullptr);   // 標準コンストラクター
	virtual ~CPlayListNew();

// ダイアログ データ
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_PLAYLIST_NEW };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート

	DECLARE_MESSAGE_MAP()
public:
	CString name;
	CCustomStatic m_label;
	CCustomEdit m_name;
	afx_msg void OnBnClickedOk();
	virtual BOOL OnInitDialog();
	CCustomStandardButton afsasfafs;
	CCustomStandardButton fsaascasa;
};
