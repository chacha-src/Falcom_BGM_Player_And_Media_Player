#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "SasamiComposerDoc.h"

class CSasamiInsertFxDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiInsertFxDlg)
public:
	CSasamiInsertFxDlg(CWnd* pParent = nullptr);
	enum { IDD = IDD_SASAMI_INSERT_FX };

	static CSasamiInsertFxDlg* OpenOwned(CWnd* owner, ScMidiDoc* doc, int part1to32);
	static void CloseOpen(void);

	ScMidiDoc* m_doc;
	int m_part;
	HWND m_notify;

protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnCbnSlot();
	afx_msg void OnBnClickedPick();
	afx_msg void OnBnClickedBypass();
	afx_msg void OnBnClickedEditor();
	afx_msg void OnBnClickedClose();
	afx_msg void OnClose();

	void LayoutChrome();
	void ApplyLang();
	int CurSlot() const;
	void RefreshHint();
	void RefreshKnobs();
	void NotifyChanged();
	void CaptureStateToDoc();
	int KnobParamIndex(int knobI) const;

	CCustomComboBox m_slot;
	CCustomStandardButton m_pick, m_bypass, m_editor, m_close;
	CCustomStatic m_hint;
	CRect m_bodyRc;
	CRect m_knobRc[12];
	int m_dragKnob;
	int m_dragStartY;
	float m_dragStartVal;
	int m_paramPage; /* 12 knobs per page */
	static CSasamiInsertFxDlg* s_inst;
};

enum { WM_SASAMI_INSERT_FX_CHANGED = WM_APP + 7211 };
