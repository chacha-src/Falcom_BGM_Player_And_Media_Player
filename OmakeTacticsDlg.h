#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"
#include "OmakeTacticsGame.h"

class COmakeTacticsDlg : public CFrameWnd
{
	DECLARE_DYNAMIC(COmakeTacticsDlg)
public:
	COmakeTacticsDlg();
	virtual ~COmakeTacticsDlg();
	BOOL CreateFrame();

	void RequestDestroyWindow();
	void StartShown();
	BOOL RunIdleFrame();
	BOOL HandleKeyMessage(MSG* pMsg);
	void EnsureImeFocus();
	BOOL Destroying() const { return m_destroying; }
	otac::Game& GameRef() { return m_game; }
	BOOL GameReady() const { return m_gameOk; }
	void PumpLiveInput();

protected:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	afx_msg int OnCreate(LPCREATESTRUCT cs);
	afx_msg void OnMouseMove(UINT nFlags, CPoint pt);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint pt);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint pt);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint pt);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint pt);
	afx_msg void OnMButtonDown(UINT nFlags, CPoint pt);
	afx_msg void OnMButtonUp(UINT nFlags, CPoint pt);
	afx_msg void OnCaptureChanged(CWnd* pWnd);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnPaint();
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	void Down(int btn, CPoint pt);
	void Up(int btn, CPoint pt);
	void ReleaseButtons();
	int m_btnMask = 0;
	afx_msg void OnTimer(UINT_PTR id);
	afx_msg LRESULT OnTacPace(WPARAM wParam, LPARAM lParam);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnEnterSizeMove();
	afx_msg void OnExitSizeMove();
	afx_msg LRESULT OnNcHitTest(CPoint pt);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
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
	void SyncBorderlessZ();
	void ApplyChrome(BOOL windowed);
	BOOL MonitorSize(RECT* rc, int* pxW, int* pxH) const;
	void PollPad(float dt);
	void KickPace();
	void StartPace();
	void StopPace();
	void DrainInput();
	void PumpPace();
	void ReleaseUiInput();
	void SyncImeEdit();
	void PullImeToGame();
	void PlaceImeWindow();
	void HideImeEdit();
	static DWORD WINAPI PaceThread(LPVOID param);

	otac::Game m_game;
	BOOL m_gameOk = FALSE;
	BOOL m_clientLive = FALSE;
	BOOL m_destroying = FALSE;
	BOOL m_swapShown = FALSE;
	BOOL m_fg = TRUE;
	LARGE_INTEGER m_qpf = {}, m_tickAt = {};
	HANDLE m_paceStop = NULL;
	HANDLE m_paceDone = NULL;
	HANDLE m_paceThread = NULL;
	BOOL m_inPace = FALSE;
	volatile LONG m_pacePosted = 0;
	volatile LONG m_paceHz = 240;
	int m_winApplied = -1;
	BOOL m_sizing = FALSE;
	BOOL m_exclFail = FALSE;
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
