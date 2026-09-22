#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "WrdEngine.h"

class CWrdViewDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CWrdViewDlg)
public:
	CWrdViewDlg(CWnd* pParent = nullptr);
	virtual ~CWrdViewDlg();
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_WRDVIEW };
#endif
	void PumpSyncNow();
	void IdlePulse();
	void DetachForDestroy();
	void LayoutHelpBtn();
	void ReloadCurrent();
	int LoadWrdPath(const wchar_t* wrdPath);
	const wchar_t* LoadedWrdPath() const { return m_wrdPath; }

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	virtual BOOL PreTranslateMessage(MSG* pMsg);

private:
	void PersistPos();
	void SyncFromPlayback();
	void UnloadWrd();
	void ReleasePaintBuffers();
	bool EnsureFrameBuffer(CDC& refDC, int w, int h);
	UINT WindowDpi() const;
	int Scale(int v96, UINT dpi) const { return MulDiv(v96, (int)dpi, 96); }

	CCustomStandardButton m_help;
	CToolTipCtrl m_tooltip;
	WrdEngine m_eng;
	wchar_t m_wrdPath[520];
	wchar_t m_srcPath[520];
	bool m_paintDisabled;
	bool m_alwaysOnTop;
	CDC m_frameDC;
	CBitmap m_frameBmp;
	CBitmap* m_frameOld;
	int m_frameW, m_frameH;
	int m_lastTickDrawn;
};
