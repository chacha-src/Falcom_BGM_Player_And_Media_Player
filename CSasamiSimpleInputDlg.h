#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"

/* Reusable tiny prompt: number or single-line text. Returns IDOK/IDCANCEL. */
class CSasamiSimpleInputDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiSimpleInputDlg)
public:
	enum { IDD = IDD_SASAMI_SIMPLE_INPUT };
	enum Mode { ModeNumber = 0, ModeText = 1 };

	CSasamiSimpleInputDlg(CWnd* pParent = nullptr);

	static int AskNumber(CWnd* owner, const wchar_t* title, const wchar_t* prompt,
		int defVal, int minV, int maxV, int* outVal);
	static int AskText(CWnd* owner, const wchar_t* title, const wchar_t* prompt,
		wchar_t* buf, int bufCch);

	CString m_title;
	CString m_prompt;
	Mode m_mode;
	int m_numVal;
	int m_numMin;
	int m_numMax;
	CString m_textVal;

protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);
	DECLARE_MESSAGE_MAP()
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);

	void LayoutChrome();
	CCustomStatic m_lbl;
	CCustomEdit m_edit;
	CCustomStandardButton m_ok, m_cancel;
};
