#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"

class CSasamiNotePaletteDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiNotePaletteDlg)
public:
	CSasamiNotePaletteDlg(CWnd* pParent = nullptr);
	enum { IDD = IDD_SASAMI_NOTE_PAL };
	enum { kCellCount = 32 };
	int m_baseDur;
	int m_durTicks;
	int m_rest;
	int m_accidental;
	int m_tuplet; /* 0,3,5,6,8 */
	int m_dotted;
	int m_markStack; /* 0=replace, 1=nest — radio with palette cells */
	HWND m_notify;

	static CSasamiNotePaletteDlg* OpenNear(CWnd* owner, CPoint screenPt);
	static CSasamiNotePaletteDlg* Instance();
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
	CRect m_cells[kCellCount];
	CToolTipCtrl m_tip;
	static CSasamiNotePaletteDlg* s_inst;
};

/* Palette → score:
   Duration: wParam = placeDur ticks
             lParam = rest|dotted<<1|tuplet<<4|accidental<<8|baseDur<<16
             (must NOT set SASAMI_PAL_CMD — baseDur<<16 collides with old cmd masks)
   Command:  wParam = 0
             lParam = SASAMI_PAL_CMD | cmdId  (cmdId in low 8 bits) */
enum { WM_SASAMI_PAL_DUR = WM_APP + 7201 };
enum {
	SASAMI_PAL_CMD = (int)0x80000000,
	SASAMI_PAL_CMD_FIT = 1,
	SASAMI_PAL_CMD_TEMPO = 2,
	SASAMI_PAL_CMD_GT = 3,
	SASAMI_PAL_CMD_PENCIL = 4,
	SASAMI_PAL_CMD_ERASE = 5,
	SASAMI_PAL_CMD_SEL = 6,
	SASAMI_PAL_CMD_MARK = 7,
	SASAMI_PAL_CMD_LOOP_A = 8,
	SASAMI_PAL_CMD_LOOP_B = 9,
	SASAMI_PAL_CMD_LOOP_CLR = 10,
	SASAMI_PAL_CMD_TIE = 11,
	SASAMI_PAL_CMD_LOOP_START = 12,
	SASAMI_PAL_CMD_LOOP_END = 13,
	SASAMI_PAL_CMD_PED_ON = 14,
	SASAMI_PAL_CMD_PED_OFF = 15,
	SASAMI_PAL_CMD_MARK_REPLACE = 16, /* place mode: replace same tick */
	SASAMI_PAL_CMD_MARK_STACK = 17    /* place mode: nest/stack */
};
