#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "SasamiComposerDoc.h"

/* GS/XG/LA/55/88… tone map picker with bank + click-to-audition.
   VST3 button switches this part to a dedicated instrument (HALion etc.). */
class CSasamiToneMapDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiToneMapDlg)
public:
	CSasamiToneMapDlg(CWnd* pParent = nullptr);
	enum { IDD = IDD_SASAMI_TONE_MAP };

	/* Modal. Writes bind prog/bank on OK (GS/XG). VST3 pick also returns IDOK
	   after loading dedicated plugin into the part. */
	static int PickForPart(CWnd* owner, int part1to32, ScMidiVstBind* bind);

	int m_part;
	int m_prog;
	int m_bankMsb;
	int m_bankLsb;
	int m_mapSel;
	int m_pickedVst3; /* 1 if user left via VST3 pick */

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnCbnSelchangeMap();
	afx_msg void OnCbnSelchangeBank();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	afx_msg void OnBnClickedEditor();
	afx_msg void OnBnClickedVst3();
	afx_msg void OnDestroy();
	virtual void OnCancel();

	void LayoutChrome();
	void RebuildBankCombos();
	void RebuildGrid();
	void ApplyTone(int pc, int audition);
	int HitTone(CPoint pt) const;
	int ToneIsEmpty(int pc) const;

	ScMidiVstBind* m_bind;
	CCustomStandardButton m_ok, m_cancel, m_editor, m_vst3;
	CCustomComboBox m_map, m_bankM, m_bankL;
	CCustomStatic m_hint, m_lblMap, m_lblMsb, m_lblLsb;
	CRect m_gridRc;
	int m_scroll;
	int m_cellH;
	wchar_t m_names[128][40];
};
