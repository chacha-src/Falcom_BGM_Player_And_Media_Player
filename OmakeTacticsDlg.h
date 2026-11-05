#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"
#include "OmakeTacticsGame.h"

class COmakeTacticsDlg;

class CTacView : public CWnd
{
public:
	COmakeTacticsDlg* m_owner = nullptr;
	BOOL CreateView(CWnd* parent, const CRect& rc);
	void ReleaseButtons();
protected:
	afx_msg void OnMouseMove(UINT nFlags, CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint pt);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint pt);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint pt);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint pt);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint pt);
	afx_msg void OnMButtonDown(UINT nFlags, CPoint pt);
	afx_msg void OnMButtonUp(UINT nFlags, CPoint pt);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnCaptureChanged(CWnd* pWnd);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnPaint();
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg UINT OnGetDlgCode();
	afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
	DECLARE_MESSAGE_MAP()
	void Down(int btn, CPoint pt);
	void Up(int btn, CPoint pt);
	int m_btnMask = 0;
};

class COmakeTacticsDlg : public CCustomBlurDialogBase
{
public:
	enum { IDD = IDD_OMAKE_TACTICS };
	explicit COmakeTacticsDlg(CWnd* pParent = nullptr);
	virtual ~COmakeTacticsDlg();

	void RequestDestroyWindow();
	BOOL HandleKeyMessage(MSG* pMsg);
	void EnsureImeFocus();
	BOOL Destroying() const { return m_destroying; }
	otac::Game& GameRef() { return m_game; }
	BOOL GameReady() const { return m_gameOk; }

protected:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	virtual void OnOK() {}
	virtual void OnCancel() { RequestDestroyWindow(); }
	afx_msg void OnTimer(UINT_PTR id);
	afx_msg LRESULT OnTacPace(WPARAM wParam, LPARAM lParam);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnDestroy();
	afx_msg void OnClose();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnTacCapClose();
	afx_msg LRESULT OnTacDestroy(WPARAM wParam, LPARAM lParam);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* mmi);
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg void OnActivateApp(BOOL bActive, DWORD dwThreadID);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg UINT OnGetDlgCode();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnImeChange();
	afx_msg void OnImeSetFocus();
	DECLARE_MESSAGE_MAP()

	void LayoutView();
	void StoreWindowRect();
	void ApplyWinMode();
	void PollPad(float dt);
	void KickPace();
	void StartPace();
	void StopPace();
	void DrainInput();
	void ReleaseUiInput();
	void SyncImeEdit();
	void PullImeToGame();
	void PlaceImeWindow();
	void HideImeEdit();
	static DWORD WINAPI PaceThread(LPVOID param);

	CTacView m_view;
	otac::Game m_game;
	BOOL m_gameOk = FALSE;
	BOOL m_destroying = FALSE;
	BOOL m_swapShown = FALSE;
	BOOL m_fg = TRUE;
	LARGE_INTEGER m_qpf = {}, m_tickAt = {};
	HANDLE m_paceStop = NULL;
	HANDLE m_paceThread = NULL;
	BOOL m_inPace = FALSE;
	volatile LONG m_pacePosted = 0;
	volatile LONG m_paceHz = 240;
	int m_winApplied = -1;
	int m_viewW = 0, m_viewH = 0;
	CEdit m_ime;
	CFont m_imeFont;
	CBrush m_imeBrush;
	int m_imeKind = 0;
	BOOL m_imePush = FALSE;
	BOOL m_imeAllowClear = FALSE;
	void* m_imeCtx = nullptr;
	int m_imeFontPx = 16;
};
