#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"

class CSasamiNotePaletteDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiNotePaletteDlg)
public:
	CSasamiNotePaletteDlg(CWnd* pParent = nullptr);
	enum { IDD = IDD_SASAMI_NOTE_PAL };
	int m_baseDur;
	int m_durTicks;
	int m_rest;
	int m_accidental;
	int m_triplet;
	int m_dotted;
	HWND m_notify;

	static CSasamiNotePaletteDlg* OpenNear(CWnd* owner, CPoint screenPt);
	void NotifyParent();

protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnClose();
	afx_msg BOOL OnTtnNeedText(UINT id, NMHDR* pNMHDR, LRESULT* pResult);

	void LayoutChrome();
	void ApplyLang();
	void SetupCellTips();
	CRect m_cells[20];
	CToolTipCtrl m_tip;
	static CSasamiNotePaletteDlg* s_inst;
};

/* wParam=placeDur ticks, lParam bits: rest|dotted<<1|triplet<<2 | (accidental signed in bits 8..15) */
enum { WM_SASAMI_PAL_DUR = WM_APP + 7201 };
