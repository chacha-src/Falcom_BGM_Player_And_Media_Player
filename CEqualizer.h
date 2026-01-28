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
	int mod = 0;
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
	CCustomStatic m_v10;
	CCustomStatic m_v11;
	CCustomStatic m_v12;
	CCustomStatic m_v13;
	CCustomStatic m_v14;
	CCustomSliderCtrl m_s14;
	CCustomSliderCtrl m_s13;
	CCustomSliderCtrl m_s12;
	CCustomSliderCtrl m_s11;
	CCustomSliderCtrl m_s10;
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	CCustomStatic m_seff;
	CCustomSliderCtrl m_eff;
	afx_msg void OnBnClickedOk();
	CCustomSliderCtrl m_smaster;
	CCustomSliderCtrl m_ssenmei;
	CCustomSliderCtrl m_skoutei;
	CCustomSliderCtrl m_smitsudo;
	CCustomSliderCtrl m_srittai;
	CCustomStatic m_vmaster;
	CCustomStatic m_vsenmei;
	CCustomStatic m_vkoutei;
	CCustomStatic m_vmitsudo;
	CCustomStatic m_vrittai;
	afx_msg void OnBnClickedOk4();
	CCustomStandardButton sdasdsdadsd;
	CCustomStatic m_t;
	CCustomStatic m_key;
};
