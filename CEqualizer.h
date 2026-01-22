#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"

// CEqualizer ダイアログ

class CEqualizer : public CCustomDialogEx
{
	DECLARE_DYNAMIC(CEqualizer)

public:
	CEqualizer(CWnd* pParent = nullptr);   // 標準コンストラクター
	virtual ~CEqualizer();

// ダイアログ データ
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_EQUALIZER };
#endif
	CToolTipCtrl m_tooltip;
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート

	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
public:

	CCustomSliderCtrl m_s0;
	CCustomSliderCtrl m_s1;
	CCustomSliderCtrl m_s2;
	CCustomSliderCtrl m_s3;
	CCustomSliderCtrl m_s4;
	CCustomSliderCtrl m_s5;
	CCustomSliderCtrl m_s6;
	CCustomSliderCtrl m_s7;
	CCustomSliderCtrl m_s8;
	CCustomSliderCtrl m_s9;
	CCustomStatic m_v0;
	CCustomStatic m_v1;
	CCustomStatic m_v2;
	CCustomStatic m_v3;
	CCustomStatic m_v4;
	CCustomStatic m_v5;
	CCustomStatic m_v6;
	CCustomStatic m_v7;
	CCustomStatic m_v8;
	CCustomStatic m_v9;
	CCustomComboBox m_env;
	CCustomComboBox m_pre;
	CCustomStandardButton m_ok;
	afx_msg void OnCbnSelchangeCombo1();
	afx_msg void OnCbnSelchangeCombo5();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnBnClickedOk3();
	CCustomStandardButton dum;
};
