#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"

// EqKey ワーカー解析完了 → UI スレッドでコード表示更新（WM_TIMER 飢餓回避）
#ifndef WM_EQ_KEY_UPDATE
#define WM_EQ_KEY_UPDATE (WM_APP + 430)
#endif

void SnapshotEqKeyCodes(CString& lo, CString& mid, CString& hi, CString& all);
void RegisterEqKeyUiHwnd(HWND h);
void UnregisterEqKeyUiHwnd(HWND h);
void AckEqKeyUiNotify();

// CEqualizer ダイアログ

class CEqualizer : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CEqualizer)

public:
	CEqualizer(CWnd* pParent = nullptr);   // 標準コンストラクター
	virtual ~CEqualizer();
	void ApplyTitleFont();

// ダイアログ データ
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_EQUALIZER };
#endif
	CToolTipCtrl m_tooltip;
	int mod = 0;
	void LayoutHelpBtn();
	void LayoutToneColumns();
	void ShowHelpSheet();

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
	CFont m_titleFont; // ApplyTitleFont 用(SetFont 寿命に合わせメンバ保持)
	CCustomStatic m_keyLow;
	CCustomStatic m_keyMid;
	CCustomStatic m_keyHigh;
	CCustomStatic m_keyAll;
	CCustomStatic m_lblDry;
	CCustomStatic m_lblWet;
	CCustomStatic m_lblAcoustic;
	CCustomStatic m_lblSpectrum;
	CCustomStatic m_lblFreq;
	CCustomStatic m_lblBand;
	CCustomStatic m_lblLoudness;
	CCustomStatic m_lblWarmth;
	CCustomStatic m_freq25;
	CCustomStatic m_freq40;
	CCustomStatic m_freq63;
	CCustomStatic m_freq100;
	CCustomStatic m_freq160;
	CCustomStatic m_freq250;
	CCustomStatic m_freq400;
	CCustomStatic m_freq630;
	CCustomStatic m_freq1000;
	CCustomStatic m_freq1600;
	CCustomStatic m_freq2500;
	CCustomStatic m_freq4000;
	CCustomStatic m_freq6300;
	CCustomStatic m_freq10000;
	CCustomStatic m_freq16000;
	CCustomStatic m_unitHz;
	CCustomStatic m_unitPct;

	// OnTimer の差分抑制用（ダイアログ再作成時は HWND が空でも CString が残らないようメンバで保持）
	CString m_cachedKeyLow;
	CString m_cachedKeyMid;
	CString m_cachedKeyHigh;
	CString m_cachedKeyAll;
	void ApplyKeyCodesUi();
	void SyncSlidersFromSavedata();
	afx_msg LRESULT OnEqKeyUpdate(WPARAM wParam, LPARAM lParam);
	afx_msg void OnDestroy();
	afx_msg void OnClose();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnBnClickedHelp();
	CCustomStandardButton m_help;
	CCustomSliderCtrl m_reverb;
	CCustomSliderCtrl m_chorus;
	CCustomSliderCtrl m_delay;
	CCustomStatic m_reverbi;
	CCustomStatic m_chorusi;
	CCustomStatic m_delayi;
	CCustomStandardButton m_abA;
	CCustomStandardButton m_abB;
	CCustomStandardButton m_abTog;
	afx_msg void OnBnClickedAbA();
	afx_msg void OnBnClickedAbB();
	afx_msg void OnBnClickedAbTog();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnSuggestEqFromKey();
	afx_msg void OnToggleKeyEqAuto();
};
