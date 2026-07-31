#pragma once

#include "CCustomControl.h"
#include "resource.h"

class CPromptDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CPromptDlg)
public:
	CPromptDlg(CWnd* pParent = nullptr);
	virtual ~CPromptDlg();
	enum { IDD = IDD_MP_PROMPT };

	void SaveTextToSavedata();
	void LoadTextFromSavedata();
	void UpdateRemainLabel();
	void SaveCurrentToHistory();
	CString GetPromptText() const;
	void ApplyTextFromRoll(const CString& text, UINT syncGen);
	UINT GetSyncGen() const { return m_syncGen; }

protected:
	CCustomEdit m_edit;
	CCustomStatic m_lblEdit;
	CCustomEdit m_legend;
	CCustomComboBox m_hist;
	CCustomComboBox m_mode;
	CCustomProgressCtrl m_progress;
	CCustomStandardButton m_run, m_stop, m_reset, m_clear, m_close, m_saveHist, m_analyze, m_roll;
	CToolTipCtrl m_tooltip;
	CFont m_fontLegend;
	CFont m_fontEditLbl;
	CFont m_fontBtn;
	CBrush m_brDlg;
	BOOL m_posRestored = FALSE;
	BOOL m_inSizeMove = FALSE;
	BOOL m_analyzing = FALSE;
	UINT m_syncGen = 0;
	BOOL m_applyingFromRoll = FALSE;
	static const int kMaxChars = 14000;

	void LayoutControls();
	void RefreshAfterLayout(BOOL bSyncRedraw);
	void SetupTooltips();
	void StyleButtons();
	void RefreshOpaqueFixers(BOOL bSync = FALSE);
	void SyncLayoutAndPaint(BOOL bSyncRedraw, BOOL bReapplyOpaqueFixers);
	void SavePosToSavedata();
	void RestorePosFromSavedata();
	void ReloadHistoryCombo();
	void FillModeCombo();
	int  GetSelectedAnalyzeMode() const;
	void SetAnalyzeUiBusy(BOOL busy);
	static void AnalyzeProgressThunk(int percent, LPCTSTR status, void* user);

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	virtual void OnClose();
	afx_msg void OnRun();
	afx_msg void OnAnalyze();
	afx_msg void OnRoll();
	afx_msg void OnModeSel();
	afx_msg void OnStop();
	afx_msg void OnReset();
	afx_msg void OnClear();
	afx_msg void OnCloseBtn();
	afx_msg void OnSaveHist();
	afx_msg void OnHistSel();
	afx_msg void OnTextChanged();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnEnterSizeMove();
	afx_msg void OnExitSizeMove();
	afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
#if CCUSTOM_AERO_SUPPORT
	afx_msg LRESULT OnReapplyOpaqueFixers(WPARAM wParam, LPARAM lParam);
#endif
	DECLARE_MESSAGE_MAP()
};

void MpShowPromptDialog(CWnd* pParent, BOOL bActivate = TRUE);
void MpTogglePromptDialog(CWnd* pParent);
BOOL MpIsPromptOpen();
CPromptDlg* MpPromptDlgInstance();
CString MpPromptSourceText();
void MpMakeIndependentZOrder(CWnd* w);
