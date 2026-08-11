#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"

class CTnNeedlePanel : public CCustomStatic
{
public:
	CTnNeedlePanel():m_cents(0){}
	void SetCents(int v){m_cents=max(-100,min(100,v));Invalidate(FALSE);}
protected:
	int m_cents;
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC*){return TRUE;}
	DECLARE_MESSAGE_MAP()
};

class CTunerPracticeDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CTunerPracticeDlg)
public:
	CTunerPracticeDlg(CWnd* p=NULL);
	virtual ~CTunerPracticeDlg();
	enum { IDD=IDD_TUNERPRACTICE };
protected:
	virtual void DoDataExchange(CDataExchange*);
	virtual BOOL PreTranslateMessage(MSG*);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()
	void FillDevices();
	void FillValues();
	void PersistUi();
	void StartAudio();
	void StopAudio();
	void LayoutHelpBtn();
	void ShowHelpSheet();
	static UINT __stdcall AudioThread(void*);
public:
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnCloseBtn();
	afx_msg void OnHelp();
	afx_msg void OnClick();
	afx_msg void OnPhrase();
	afx_msg void OnChanged();
	afx_msg void OnTimer(UINT_PTR);
	afx_msg void OnSize(UINT,int,int);
	afx_msg void OnDestroy();

	CCustomStandardButton m_help,m_click,m_phrase,m_close;
	CCustomStatic m_micL,m_outL,m_note,m_cents,m_bpmL,m_beatsL,m_a4L,m_status;
	CCustomComboBox m_mic,m_out,m_bpm,m_beats,m_a4;
	CCustomCheckBox m_mute;
	CTnNeedlePanel m_needle;
	CToolTipCtrl m_tooltip;
	enum { TN_MAX=32,TN_TIMER=93 };
	TCHAR m_micIds[TN_MAX][256],m_outIds[TN_MAX][256];
	int m_micCnt,m_outCnt,m_bpmVal,m_beatsVal,m_a4Val,m_beat;
	volatile LONG m_stop,m_run,m_hz100,m_cent100,m_clickRequest,m_lastHr;
	HANDLE m_thread;
	DWORD m_nextClickTick;
};
void OpenTunerPracticeModeless(CWnd*);
void CloseTunerPracticeIfOpen();
