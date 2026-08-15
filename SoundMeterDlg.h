#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"

// 相対 dBFS マイクメータ（校正 SPL 騒音計ではない）
class CSoundMeterDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CSoundMeterDlg)
public:
	CSoundMeterDlg(CWnd* pParent = NULL);
	virtual ~CSoundMeterDlg();
	enum { IDD = IDD_SOUNDMETER };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()

	void FillMicCombo();
	void PersistUi();
	void RefreshOpaqueUi();
	void LayoutHelpBtn();
	void ShowHelpSheet();
	void StartCapture();
	void StopCapture();
	void PaintUiFromPeaks();
	static UINT __stdcall CaptureThread(void* p);

public:
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedClose();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnCbnSelchangeMic();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg void OnMicDevRefresh();
	afx_msg LRESULT OnAudioDevChanged(WPARAM, LPARAM);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	virtual void OnCancel();
	virtual void OnOK();

	CCustomStandardButton m_help;
	CCustomStatic m_micL;
	CCustomComboBox m_mic;
	CCustomStandardButton m_micRefresh;
	CCustomStatic m_dbfs;
	CCustomStatic m_hold;
	CCustomStatic m_meterL;
	CCustomLevelMeter m_meter;
	CCustomStatic m_note;
	CCustomStatic m_status;
	CCustomStandardButton m_close;
	CToolTipCtrl m_tooltip;

	enum { SM_TIMER = 81, SM_DEV_MAX = 32 };
	TCHAR m_devIds[SM_DEV_MAX][256];
	int m_devCnt;

	volatile LONG m_stop;
	volatile LONG m_run;
	volatile LONG m_peak;
	volatile LONG m_holdPeak;
	HANDLE m_thread;
	int m_response; // 0..2
};

void OpenSoundMeterModeless(CWnd* parent);
void CloseSoundMeterIfOpen();
