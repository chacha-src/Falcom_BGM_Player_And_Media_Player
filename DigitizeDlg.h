#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"

class CDigitizeDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CDigitizeDlg)
public:
	CDigitizeDlg(CWnd* pParent = NULL);
	virtual ~CDigitizeDlg();
	enum { IDD = IDD_DIGITIZE };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()

	void FillDevices();
	void FillSettings();
	void PersistUi();
	void SetRecordingUi(BOOL on);
	void LayoutHelpBtn();
	void ShowHelpSheet();
	BOOL StartRecording();
	void StopRecording(BOOL encode);
	static UINT __stdcall CaptureThread(void* p);

public:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnBrowse();
	afx_msg void OnStart();
	afx_msg void OnCloseBtn();
	afx_msg void OnHelp();
	afx_msg void OnFormat();
	afx_msg void OnControlChanged();
	afx_msg void OnTimer(UINT_PTR id);
	afx_msg void OnSize(UINT type, int cx, int cy);
	afx_msg void OnDestroy();

	CCustomStandardButton m_help, m_browse, m_start, m_close;
	CCustomStatic m_capL, m_monL, m_fmtL, m_qualL, m_pathL;
	CCustomStatic m_hpfL, m_gainL, m_gateL, m_meterL, m_time, m_status;
	CCustomComboBox m_cap, m_mon, m_fmt, m_qual, m_hpf, m_gain, m_gate;
	CCustomEdit m_path;
	CCustomCheckBox m_monitor;
	CCustomLevelMeter m_meter;
	CToolTipCtrl m_tooltip;

	enum { DIG_DEV_MAX = 32, DIG_TIMER = 91 };
	TCHAR m_capIds[DIG_DEV_MAX][256], m_monIds[DIG_DEV_MAX][256];
	int m_capCnt, m_monCnt;
	volatile LONG m_stop, m_run, m_peak, m_pcmBytes, m_lastHr;
	HANDLE m_thread;
	CFile m_file;
	CRITICAL_SECTION m_fileCs;
	BOOL m_csInit, m_uiLocked, m_stopping;
	CString m_wavPath, m_finalPath;
	int m_outFmt, m_mp3Kbps, m_flacLevel, m_hpfHz, m_gainPct, m_gatePct;
	DWORD m_startTick;
};

void OpenDigitizeModeless(CWnd* parent);
void CloseDigitizeIfOpen();
