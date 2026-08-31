#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "SasamiComposerDoc.h"

/* Floating note editor: pitch / gate% / vel / VST / cmd-roll EQ */
class CSasamiNotePropsDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiNotePropsDlg)
public:
	CSasamiNotePropsDlg(CWnd* pParent = nullptr);
	enum { IDD = IDD_SASAMI_NOTE_PROPS };

	ScEvent* m_ev;
	int m_isFm;
	int m_part; /* 1..32 */
	HWND m_notify;

	static CSasamiNotePropsDlg* OpenForEvent(CWnd* owner, ScEvent* ev, int isFm, int part1to32);
	static CSasamiNotePropsDlg* Instance();
	static void CloseOpen(void);
	static void NotifyVstResult(int part1to32, int ok);
	static void RaiseSelf(void);
	void RefreshVstHint();
	void RefreshChromeOpaque();

protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedApply();
	afx_msg void OnBnClickedVst();
	afx_msg void OnBnClickedEq();
	afx_msg void OnBnClickedClose();
	afx_msg void OnClose();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg LRESULT OnDeferredOpenVst(WPARAM w, LPARAM l);
	afx_msg void OnTimer(UINT_PTR id);

	void LayoutChrome();
	void ApplyLang();
	void LoadFromEv();

	CCustomEdit m_edNote, m_edGt, m_edVel, m_edEq;
	CCustomStandardButton m_btnApply, m_btnVst, m_btnEq, m_btnClose;
	CCustomStatic m_hint;
	static CSasamiNotePropsDlg* s_inst;
	enum { NP_HINT_TIMER = 77 };
};

enum { WM_SASAMI_NOTE_PROPS = WM_APP + 7202 };
enum { WM_SASAMI_NP_OPEN_VST = WM_APP + 7205 };
/* wParam for WM_SASAMI_NOTE_PROPS: 1=apply 2=sync bind 3=FM voice 4=assign VST 5=VST done */
