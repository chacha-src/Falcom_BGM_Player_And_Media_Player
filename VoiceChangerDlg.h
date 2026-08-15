#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"

class CVoiceChangerDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CVoiceChangerDlg)
public:
	CVoiceChangerDlg(CWnd* pParent=NULL);
	virtual ~CVoiceChangerDlg();
	enum { IDD=IDD_VOICECHANGER };
protected:
	virtual void DoDataExchange(CDataExchange*);
	virtual BOOL PreTranslateMessage(MSG*);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()
	void FillDevices();
	afx_msg void OnMicDevRefresh();
	afx_msg LRESULT OnAudioDevChanged(WPARAM, LPARAM);
	void FillValues();
	void PersistUi(BOOL markCustom=FALSE);
	void SyncLiveParams();
	void ApplyPreset(int n);
	void SelectComboPct(CCustomComboBox& cb, int pct, const int* vals, int nVals);
	int ComboPct(CCustomComboBox& cb, const int* vals, int nVals) const;
	BOOL StartAudio();
	void StopAudio();
	void SetRunningUi(BOOL);
	void LayoutHelpBtn();
	void ShowHelpSheet();
	static UINT __stdcall AudioThread(void*);
public:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnStart();
	afx_msg void OnCloseBtn();
	afx_msg void OnHelp();
	afx_msg void OnPreset();
	afx_msg void OnChanged();
	afx_msg void OnTimer(UINT_PTR);
	afx_msg void OnSize(UINT,int,int);
	afx_msg void OnDestroy();
	afx_msg void OnContextMenu(CWnd*,CPoint);

	CCustomStandardButton m_help,m_start,m_close;
	CCustomStatic m_micL,m_outL,m_pitchL,m_formL,m_gainL,m_brightL,m_breathL,m_qualL,m_presetL,m_styleL,m_meterL,m_status;
	CCustomComboBox m_mic,m_out,m_pitch,m_form,m_gain,m_bright,m_breath,m_qual,m_preset,m_fx;
	CCustomStandardButton m_micRefresh,m_outRefresh;
	CCustomCheckBox m_monitor;
	CCustomLevelMeter m_meter;
	CToolTipCtrl m_tooltip;
	enum { VC_MAX=32,VC_TIMER=92,VC_PRESET_N=23 };
	TCHAR m_micIds[VC_MAX][256],m_outIds[VC_MAX][256];
	int m_micCnt,m_outCnt;
	volatile LONG m_stop,m_run,m_peak,m_lastHr;
	HANDLE m_thread;
	BOOL m_devLocked;
	volatile LONG m_pitchPct,m_formPct,m_gainPct,m_brightPct,m_breathPct,m_quality,m_fxStyle,m_monitorOn;
};
void OpenVoiceChangerModeless(CWnd*);
void CloseVoiceChangerIfOpen();
