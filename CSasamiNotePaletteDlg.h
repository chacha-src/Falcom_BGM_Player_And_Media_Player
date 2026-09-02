#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"

class CSasamiNotePaletteDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiNotePaletteDlg)
public:
	CSasamiNotePaletteDlg(CWnd* pParent = nullptr);
	enum { IDD = IDD_SASAMI_NOTE_PAL };
	enum { kCellCount = 48 };
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
	afx_msg void OnMove(int x, int y);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
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
	SASAMI_PAL_CMD_MARK_STACK = 17,   /* place mode: nest/stack */
	SASAMI_PAL_CMD_OTTAVA_8VA = 18,
	SASAMI_PAL_CMD_OTTAVA_8VB = 19,
	SASAMI_PAL_CMD_OTTAVA_16VA = 20,
	SASAMI_PAL_CMD_OTTAVA_16VB = 21,
	SASAMI_PAL_CMD_OTTAVA_32VA = 22,
	SASAMI_PAL_CMD_OTTAVA_32VB = 23,
	SASAMI_PAL_CMD_OTTAVA_LOCO = 24,
	SASAMI_PAL_CMD_METER_24 = 30,
	SASAMI_PAL_CMD_METER_34 = 31,
	SASAMI_PAL_CMD_METER_44 = 32,
	SASAMI_PAL_CMD_METER_54 = 33,
	SASAMI_PAL_CMD_METER_68 = 34,
	SASAMI_PAL_CMD_METER_78 = 35,
	SASAMI_PAL_CMD_METER_98 = 36,
	SASAMI_PAL_CMD_METER_CUSTOM = 37,
	SASAMI_PAL_CMD_METER_DEL = 38,
	SASAMI_PAL_CMD_KEY = 39,
	SASAMI_PAL_CMD_CLEF_G = 40,
	SASAMI_PAL_CMD_CLEF_F = 41,
	SASAMI_PAL_CMD_CLEF_GF = 42,
	SASAMI_PAL_CMD_CLEF_DR = 43,
	SASAMI_PAL_CMD_TR_PLUS = 44,
	SASAMI_PAL_CMD_TR_MINUS = 45,
	SASAMI_PAL_CMD_TR_SEL_P12 = 46,
	SASAMI_PAL_CMD_TR_SEL_M12 = 47,
	SASAMI_PAL_CMD_TR_PART_PLUS = 48,
	SASAMI_PAL_CMD_TR_PART_MINUS = 49,
	SASAMI_PAL_CMD_TR_ALL_PLUS = 50,
	SASAMI_PAL_CMD_TR_ALL_MINUS = 51,
	SASAMI_PAL_CMD_METER_14 = 52,
	SASAMI_PAL_CMD_METER_22 = 53,
	SASAMI_PAL_CMD_METER_38 = 54,
	SASAMI_PAL_CMD_METER_48 = 55,
	SASAMI_PAL_CMD_METER_58 = 56,
	SASAMI_PAL_CMD_METER_88 = 57,
	SASAMI_PAL_CMD_METER_108 = 58,
	SASAMI_PAL_CMD_METER_118 = 59,
	SASAMI_PAL_CMD_METER_128 = 60,
	SASAMI_PAL_CMD_METER_138 = 61,
	SASAMI_PAL_CMD_METER_158 = 62,
	SASAMI_PAL_CMD_METER_64 = 63,
	SASAMI_PAL_CMD_METER_32 = 64,
	SASAMI_PAL_CMD_METER_74 = 65,
	SASAMI_PAL_CMD_KEY_BASE = 70 /* +0..14 → keySig -7..+7 */
};
enum { WM_SASAMI_PAL_QUERY_STATE = WM_APP + 7202 };
enum { WM_SASAMI_PAL_LAYOUT = WM_APP + 7203 };
enum {
	SASAMI_PAL_LAYOUT_METER = 1,
	SASAMI_PAL_LAYOUT_KEY = 2
};
struct SasamiPalLayoutState {
	int meterN;
	int meterD;
	int keySig;
	int clef;
	int nSel;
	int previewNote; /* midi pitch of primary selection, or -1 */
	int curCh;
};
