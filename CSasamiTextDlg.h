#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "SasamiComposerDoc.h"

class CSasamiTextDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiTextDlg)
public:
	CSasamiTextDlg(CWnd* pParent = nullptr);
	virtual ~CSasamiTextDlg();
	enum { IDD = IDD_SASAMI_TEXT };
	static CSasamiTextDlg* Instance();
	static void OpenOwned(CWnd* owner);
	CString GetMmlText() const;
	void SetMmlFromScore(const wchar_t* mml);
	void SetFmTextFromScore(const wchar_t* text);
	void NewDocument();
	void PersistSession();
	void PersistUiGeom();
	void RestoreUiGeom();
	void PushTextToScore();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnBnClickedCompile();
	afx_msg void OnBnClickedSave();
	afx_msg void OnBnClickedOpen();
	afx_msg void OnBnClickedNew();
	afx_msg void OnBnClickedPlay();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnBnClickedInsertVst();
	afx_msg void OnBnClickedExport();
	afx_msg void OnBnClickedScore();
	afx_msg void OnBnClickedMode();
	afx_msg void OnBnClickedB64Fold();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnEnChange();
	afx_msg void OnTimer(UINT_PTR nIDEvent);

	void LayoutChrome();
	void ApplyLang();
	void RefreshChromeOpaque();
	int CompileAndBuild(wchar_t* outPath, int outCch, int* isFm);
	void ColorizeEdit();
	void SetupTooltips();
	void SyncTextFullFromEdit();
	void ApplyB64ViewToEdit();
	void UpdateB64FoldButton();
	void ReloadFullTextFromScore();
	void ScheduleScoreSync();

	CCustomEdit m_edit;
	CCustomStandardButton m_btnCompile, m_btnScore, m_btnMode;
	CCustomStandardButton m_btnSave;
	CCustomStandardButton m_btnOpen;
	CCustomStandardButton m_btnNew;
	CCustomStandardButton m_btnPlay;
	CCustomStandardButton m_btnHelp;
	CCustomStandardButton m_btnVst;
	CCustomStandardButton m_btnExport;
	CCustomStandardButton m_btnB64Fold;
	CCustomStatic m_status;
	CToolTipCtrl m_tooltip;
	CRect m_bodyRc;
	ScMidiDoc m_midi;
	ScFmDoc m_fm;
	int m_modeFm; /* 0=midi text, 1=fm text */
	int m_b64Expanded; /* 0=collapsed view, 1=full @VSTSTATEB64 lines */
	BOOL m_b64RefreshLock;
	CString m_textFull;
	wchar_t m_lastOut[MAX_PATH];
	static CSasamiTextDlg* s_inst;
	enum { kTextScoreSyncTimer = 7103 };
};
