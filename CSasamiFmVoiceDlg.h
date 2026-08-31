#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "SasamiComposerDoc.h"

class CSasamiFmVoiceDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiFmVoiceDlg)
public:
	CSasamiFmVoiceDlg(CWnd* pParent = nullptr);
	virtual ~CSasamiFmVoiceDlg();
	enum { IDD = IDD_SASAMI_FM_VOICE };

	void SetVoice(const uint8_t v[25]);
	void GetVoice(uint8_t v[25]) const;
	int DoEdit(CWnd* owner, uint8_t v[25]); /* modal; returns IDOK/IDCANCEL */

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnBnClickedPreview();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();

	void LayoutChrome();
	void ApplyLang();
	int PreviewBeep();

	CCustomStandardButton m_btnPreview;
	CCustomStandardButton m_btnOk;
	CCustomStandardButton m_btnCancel;
	CCustomComboBox m_cmbPreset;
	uint8_t m_voice[25];
	CRect m_bodyRc;
	CRect m_knobRc[22]; /* 4 operators x 5 parameters, FB, ALG */
	int m_dragKnob;
	int m_dragStartY;
	int m_dragStartValue;
	afx_msg void OnCbnPreset();
};
