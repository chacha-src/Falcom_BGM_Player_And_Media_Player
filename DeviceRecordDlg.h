#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"

// 再生端末のループバック録音 → WAV / mp3 / FLAC
class CDeviceRecordDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CDeviceRecordDlg)

public:
	CDeviceRecordDlg(CWnd* pParent = NULL);
	virtual ~CDeviceRecordDlg();
	enum { IDD = IDD_DEVICERECORD };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	DECLARE_MESSAGE_MAP()

	void FillDeviceCombo();
	void RefreshQualityCombo();
	void PersistUiToSavedata();
	void RefreshOpaqueUi();
	CString ExtForFormat(int fmt) const;
	CString FilterForFormat(int fmt) const;
	CString NormalizeOutPath(const CString& pathIn, int fmt) const;
	void SetRecordingUi(BOOL recording);
	void UpdateElapsedUi();
	BOOL StartRecording();
	void StopRecording(BOOL encodeAfter);
	static UINT __stdcall CaptureThread(void* p);

public:
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedBrowse();
	afx_msg void OnBnClickedStart();
	afx_msg void OnBnClickedClose();
	afx_msg void OnCbnSelchangeFormat();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	virtual void OnCancel();
	virtual void OnOK();

	CCustomStatic m_devLabel;
	CCustomComboBox m_dev;
	CCustomStatic m_fmtLabel;
	CCustomComboBox m_fmt;
	CCustomStatic m_qualLabel;
	CCustomComboBox m_qual;
	CCustomStatic m_pathLabel;
	CCustomEdit m_path;
	CCustomStandardButton m_browse;
	CCustomCheckBox m_mixMic;
	CCustomStandardButton m_start;
	CCustomStandardButton m_close;
	CCustomStatic m_status;
	CCustomStatic m_time;
	CToolTipCtrl m_tooltip;

	enum { DR_DEV_MAX = 32, DR_HDR = 80, DR_TIMER = 71 };
	TCHAR m_devIds[DR_DEV_MAX][256];
	int m_devCnt;

	volatile LONG m_stop;
	volatile LONG m_run;
	volatile LONG m_pcmBytes;
	volatile LONG m_lastHr;
	HANDLE m_thread;
	CFile m_wavFile;
	CRITICAL_SECTION m_fileCs;
	BOOL m_csInit;
	BOOL m_uiLocked;     // 録音中は EnableWindow せず入力だけ無視(透過防止)
	BOOL m_stopping;     // StopRecording 再入防止
	BOOL m_everStarted;
	CString m_wavPath;
	CString m_finalPath;
	int m_outFmt;
	int m_mp3Kbps;
	int m_flacLevel;
	BOOL m_doMixMic;
	DWORD m_startTick;
	WORD m_wavCh;
	DWORD m_wavHz;
	WORD m_wavBits;
};
