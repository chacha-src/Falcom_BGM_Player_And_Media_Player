#include "afxdialogex.h"
#include "afxwin.h"
#include "afxcmn.h"
#if !defined(AFX_RENDER_H__F5FB1AA1_8545_4B26_80A3_4E0FA43C0548__INCLUDED_)
#define AFX_RENDER_H__F5FB1AA1_8545_4B26_80A3_4E0FA43C0548__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Render.h : ヘッダー ファイル
//

/////////////////////////////////////////////////////////////////////////////
// CRender ダイアログ
#include "CCustomControl.h"

class CRender : public CCustomBlurDialogExBase
{
// コンストラクション
public:
	CRender(CWnd* pParent = NULL);   // 標準のコンストラクタ

// ダイアログ データ
	//{{AFX_DATA(CRender)
	enum { IDD = IDD_Render };
	CCustomComboBox	m_1;
	//}}AFX_DATA
	DECLARE_DYNAMIC(CRender);

	CWnd* m_pParent;
	int m_modeless;
	int Create(CWnd* pWnd);
	void CloseModeless();
	CBrush m_brDlg;
	
	// オーバーライド
	// ClassWizard は仮想関数のオーバーライドを生成します。
	//{{AFX_VIRTUAL(CRender)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	//}}AFX_VIRTUAL

	BOOL MySetFileType(LPCTSTR lpExt, LPCTSTR lpDocName, LPCTSTR lpDocType, LPCTSTR lpPath, LPCTSTR lpPath1);
	static BOOL CALLBACK DSEnumCallback(LPGUID p_guid, LPCWSTR psz_desc, LPCWSTR psz_mod, LPVOID data);
// インプリメンテーション
protected:
	CToolTipCtrl m_tooltip;
	CCustomStandardButton m_help;
	void LayoutHelpBtn();
	void ShowHelpSheet();
	// 生成されたメッセージ マップ関数
	//{{AFX_MSG(CRender)
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	virtual void PostNcDestroy();
	afx_msg void OnClose();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedHelp();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	CCustomCheckBox m_evr;
	virtual INT_PTR OnToolHitTest(CPoint point, TOOLINFO* pTI) const;
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	CCustomCheckBox m_con;
	CCustomCheckBox m_a;
	CCustomCheckBox m_ffd;
	afx_msg void OnBnClickedCancel2();
	CCustomStandardButton m_l;
	CCustomCheckBox m_vob;
	CCustomCheckBox m_haali;
	afx_msg void Onspc2x();
	afx_msg void Onspc4x();
	afx_msg void Onspc8x();
	CCustomCheckBox m_spc2x;
	CCustomCheckBox m_spc4x;
	CCustomCheckBox m_spc8x;
	afx_msg void Onspc1x();
	CCustomCheckBox m_spc1x;
	afx_msg void Onspc16x();
	CCustomCheckBox m_spc16x;
	CCustomCheckBox m_mp31;
	CCustomCheckBox m_mp315;
	CCustomCheckBox m_mp32;
	CCustomCheckBox m_mp325;
	CCustomCheckBox m_mp33;
	afx_msg void Onmp31();
	afx_msg void Onmp315();
	afx_msg void Onmp32();
	afx_msg void Onmp325();
	afx_msg void Onmp33();
	CCustomCheckBox m_kpi10;
	CCustomCheckBox m_kpi15;
	CCustomCheckBox m_kpi20;
	CCustomCheckBox m_kpi25;
	CCustomCheckBox m_kpi30;
	afx_msg void Onkpi10();
	afx_msg void Onkpi15();
	afx_msg void Onkpi20();
	afx_msg void Onkpi25();
	afx_msg void Onkpi30();
	afx_msg void Onkpi();
	CCustomStandardButton m_kpi;
	CCustomStandardButton m_kpiPluginDl;
	CCustomStandardButton m_kpiPluginReload;
	afx_msg void OnKpiPluginDl();
	afx_msg void OnKpiPluginReload();
	afx_msg void OnFontMain();
	afx_msg void OnFontList();
	CCustomCheckBox m_mp3orig;
	CCustomCheckBox m_audiost;
	CCustomCheckBox m_24;
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClicked24bit();
	CCustomCheckBox m_m4a;
	afx_msg void OnBnClickedCheck50();
	afx_msg void OnBnClickedCancel4();
	CCustomCheckBox m_32bit;
	CCustomSliderCtrl m_ms;
	CCustomStatic m_ms2;
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	CCustomSliderCtrl m_hyouji2;
	CCustomStatic m_hyouji3;
	CCustomSliderCtrl m_eqCode;
	CCustomStatic m_eqCodeMs;
	CCustomComboBox m_soundlist;
	CCustomComboBox m_miclist;
	CCustomStandardButton m_micRefresh;
	CCustomStatic m_micLabel;
	afx_msg void OnCbnSelchangeCombo2();
	afx_msg void OnCbnSelchangeMic();
	afx_msg void OnMicDevRefresh();
	CCustomStandardButton m_ao;
	afx_msg void OnBnClickedButton1();
	CCustomComboBox m_Hz;
	afx_msg void OnCbnSelchangeCombo3();
	CCustomStatic m_wup;
	CCustomSliderCtrl w_wups;
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
	afx_msg void OnBnClickedCancel();
	afx_msg void OnBnClickedCheck52();
	CCustomCheckBox m_speana;
	CCustomComboBox m_speana_num;
	CCustomCheckBox m_netlrc;
	CCustomStandardButton m_okdummy;
	CCustomStandardButton m_kanren;
	CCustomStandardButton m_canceldummy;
	CCustomComboBox m_comboLang;
	afx_msg void OnCbnEditchangeCombo4();
	afx_msg void OnCbnSelchangeCombo4();
	afx_msg void OnBnClickedCancel5();
	CCustomCheckBox m_upscale;
	CCustomCheckBox m_midPreferKpi;
	CCustomCheckBox m_midPreferVst;
	CCustomComboBox m_vstMultiCombo;
	CCustomEdit m_vstExtraPath;
	CCustomEdit m_vstMultiDll;
	CCustomStandardButton m_vstExtraBrowse;
	CCustomStandardButton m_vstMultiBrowse;
	CCustomStandardButton m_vstScanNow;
	afx_msg void OnMidPreferKpi();
	afx_msg void OnMidPreferVst();
	afx_msg void OnVstExtraBrowse();
	afx_msg void OnVstMultiBrowse();
	afx_msg void OnVstScanNow();
	void FillVstMultiCombo();
	void LayoutMidiVstRows();
	CCustomComboBox m_speaker;
	afx_msg void OnCbnSelchangeSpeaker();
	afx_msg void OnBnClickedCheckUpscale();
	afx_msg void OnBnClicked32bit();
	GUID m_bakSoundGuid;
	int m_bakSoundCur;
	DWORD m_bakSamples;
	int m_bakUpscale;
	int m_bakSpeaker;
	int m_bakBit24;
	int m_bakBit32;
	TCHAR m_bakFont1[1024];
	TCHAR m_bakFont2[1024];
	int m_bakAero;
	afx_msg void OnBnClickedCheck3();
};

extern CRender* g_renderDlg;
void CloseRenderIfOpen();

// 設定ダイアログを開かずに savedata の出力形式を再生パスへ反映する
void MpRecreatePlaybackOutput();

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ は前行の直前に追加の宣言を挿入します。

#endif // !defined(AFX_RENDER_H__F5FB1AA1_8545_4B26_80A3_4E0FA43C0548__INCLUDED_)
