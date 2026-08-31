#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"

class CSasamiVstPickDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiVstPickDlg)
public:
	CSasamiVstPickDlg(CWnd* pParent = nullptr);
	enum { IDD = IDD_SASAMI_VST_PICK };
	wchar_t m_path[520];
	int m_isVst3;
	int m_part;

	static int PickForPart(CWnd* owner, int part1to32, wchar_t* outPath, int outCch, int* outIs3);

protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	afx_msg void OnBnClickedRescan();

	void LayoutChrome();
	void ApplyLang();
	void RebuildList();
	int HitRow(CPoint pt) const;
	void AcceptSel();

	CCustomStandardButton m_ok, m_cancel, m_rescan;
	CCustomStatic m_hint;
	CRect m_listRc;
	int m_scroll;
	int m_sel;
	int m_count;
	int m_idx[512];
};
