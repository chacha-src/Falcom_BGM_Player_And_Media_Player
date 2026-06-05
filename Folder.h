#include "afxwin.h"
#if !defined(AFX_FOLDER_H__08EFA55A_7FC1_4B30_8B0F_7987E91B5FB7__INCLUDED_)
#define AFX_FOLDER_H__08EFA55A_7FC1_4B30_8B0F_7987E91B5FB7__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Folder.h : ヘッダー ファイル
//

/////////////////////////////////////////////////////////////////////////////
// CFolder ダイアログ
#include "CCustomControl.h"

class CFolder : public CCustomBlurDialogBase
{
// コンストラクション
public:
	CFolder(CWnd* pParent = NULL);   // 標準のコンストラクタ

// ダイアログ データ
	//{{AFX_DATA(CFolder)
	enum { IDD = IDD_DIALOG1 };
	CCustomEdit	m_8s;
	CCustomEdit	m_7s;
	CCustomEdit	m_6s;
	CCustomStandardButton	m_5;
	CCustomEdit	m_5s;
	CCustomEdit	m_4s;
	CCustomEdit	m_3s;
	CCustomEdit	m_2s;
	CCustomEdit	m_1s;
	CCustomStandardButton	m_4;
	CCustomStandardButton	m_3;
	CCustomStandardButton	m_2;
	CCustomStandardButton	m_1;
	//}}AFX_DATA

	CBrush m_brDlg;

	UINT GetOpenFolderName(HWND hWnd
                                    , LPCTSTR lpszDefaultFolder
                                    , LPTSTR lpszBuf
                                    , DWORD dwBufSize);

// オーバーライド
	// ClassWizard は仮想関数のオーバーライドを生成します。
	//{{AFX_VIRTUAL(CFolder)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	//}}AFX_VIRTUAL

// インプリメンテーション
protected:

	// 生成されたメッセージ マップ関数
	//{{AFX_MSG(CFolder)
	virtual BOOL OnInitDialog();
	afx_msg void On1();
	afx_msg void On2();
	afx_msg void On3();
	afx_msg void On4();
	afx_msg void On5();
	afx_msg void On6();
	afx_msg void OnButton20();
	afx_msg void OnButton22();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
public:
	CCustomEdit m_9s;
	afx_msg void On9XA();
	CCustomEdit m_10s;
	CCustomEdit m_11s;
	CCustomStandardButton m_10;
	CCustomStandardButton m_11;
	afx_msg void OnBnClickedButton29();
	afx_msg void OnBnClickedButton30();
	CCustomEdit m_12s;
	CCustomStandardButton m_12;
	afx_msg void OnBnClickedButton32();
	CCustomEdit m_13s;
	afx_msg void OnBnClickedButton34();
	CCustomEdit m_14s;
	afx_msg void OnBnClickedButton36();
	CCustomEdit m_15s;
	afx_msg void OnBnClickedButton38();
	CCustomEdit m_16s;
	CCustomEdit m_17s;
	CCustomEdit m_18s;
	CCustomEdit m_19s;
	afx_msg void OnBnClickedButton40();
	afx_msg void OnBnClickedButton41();
	afx_msg void OnBnClickedButton42();
	afx_msg void OnBnClickedButton43();
	CCustomEdit m_20s;
	afx_msg void OnBnClickedButton49();
	CCustomEdit m_21s;
	afx_msg void OnBnClickedButton50();
	CCustomEdit m_22s;
	afx_msg void OnBnClickedButton52();
	CCustomEdit m_23s;
	afx_msg void OnBnClickedButton55();
	CCustomEdit m_24s;
	afx_msg void OnBnClickedButton56();
	afx_msg void OnBnClickedButton25();
	afx_msg void OnBnClickedButton57();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	CCustomStandardButton m_okdummy;
	CCustomStandardButton aaaaaaaaaa;
	CCustomStandardButton asfsfcascs;
	CCustomStandardButton m6;
	CCustomStandardButton m7;
	CCustomStandardButton m8;
	CCustomStandardButton m9;
	CCustomStandardButton m15;
	CCustomStandardButton m16;
	CCustomStandardButton m17;
	CCustomStandardButton m18;
	CCustomStandardButton m19;
	CCustomStandardButton m20;
	CCustomStandardButton m21;
	CCustomStandardButton m22;
	CCustomStandardButton m23;
	CCustomStandardButton m24;
	CCustomStandardButton m25;
	CCustomStandardButton m27;
	CCustomStandardButton m_fsafa;
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ は前行の直前に追加の宣言を挿入します。

#endif // !defined(AFX_FOLDER_H__08EFA55A_7FC1_4B30_8B0F_7987E91B5FB7__INCLUDED_)
