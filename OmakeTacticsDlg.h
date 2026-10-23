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
	otac::Game& GameRef() { return m_game; }
	BOOL GameReady() const { return m_gameOk; }

protected:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	virtual void OnOK() {}
	virtual void OnCancel() { RequestDestroyWindow(); }
	afx_msg void OnTimer(UINT_PTR id);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnDestroy();
	afx_msg void OnClose();
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* mmi);
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	DECLARE_MESSAGE_MAP()

	void LayoutView();
	void StoreWindowRect();
	void PollPad(float dt);

	CTacView m_view;
	otac::Game m_game;
	BOOL m_gameOk = FALSE;
	BOOL m_destroying = FALSE;
	BOOL m_swapShown = FALSE;
	LARGE_INTEGER m_qpf = {}, m_last = {};
	int m_viewW = 0, m_viewH = 0;
};
