#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "CSasamiPianoRollView.h"
#include "SasamiComposerDoc.h"
#include "CSasamiStaffCore.h"

class CSasamiPianoRollDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiPianoRollDlg)
public:
	CSasamiPianoRollDlg(CWnd* pParent = nullptr);
	enum { IDD = IDD_SASAMI_PIANO_ROLL };
	static CSasamiPianoRollDlg* Instance();
	static void OpenOwned(CWnd* owner, ScEvent* ev, int* evCount, ScStaffUi* ui, int* curPart, int isFm);
	void Bind(ScEvent* ev, int* evCount, ScStaffUi* ui, int* curPart, int isFm);
	void Refresh();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnClose();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnTimer(UINT_PTR nIDEvent);

	ScEvent* m_ev;
	int* m_evCount;
	ScStaffUi* m_ui;
	int* m_curPart;
	int m_isFm;
	ScPianoRollView m_roll;
	CRect m_bodyRc;
	static CSasamiPianoRollDlg* s_inst;
};
