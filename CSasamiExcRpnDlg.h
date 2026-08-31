#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "SasamiComposerDoc.h"

enum { WM_SASAMI_EXC_RPN_CHANGED = WM_APP + 7210 };

class CSasamiExcRpnDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiExcRpnDlg)
public:
	CSasamiExcRpnDlg(CWnd* pParent = nullptr);
	enum { IDD = IDD_SASAMI_EXC_RPN };

	static CSasamiExcRpnDlg* OpenOwned(CWnd* owner, ScMidiDoc* doc, int ch0, uint32_t tick);
	static void CloseOpen(void);

	ScMidiDoc* m_doc;
	int m_ch;
	uint32_t m_tick;
	HWND m_notify;

protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnCbnPreset();
	afx_msg void OnBnClickedRpn();
	afx_msg void OnBnClickedNrpn();
	afx_msg void OnBnClickedSysex();
	afx_msg void OnBnClickedClose();
	afx_msg void OnClose();

	void LayoutChrome();
	void ApplyLang();
	void RefreshHint(const wchar_t* msg);
	int ReadTriplet(int* a, int* b, int* c);
	int ReadHex(uint8_t* out, int maxOut);
	void NotifyChanged();

	CCustomComboBox m_preset;
	CCustomEdit m_hex, m_msb, m_lsb, m_data;
	CCustomStandardButton m_btnRpn, m_btnNrpn, m_btnSysex, m_btnClose;
	CCustomStatic m_hint;
	static CSasamiExcRpnDlg* s_inst;
};
