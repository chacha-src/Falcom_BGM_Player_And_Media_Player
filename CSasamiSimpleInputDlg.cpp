#include "stdafx.h"
#include "ogg.h"
#include "CSasamiSimpleInputDlg.h"

IMPLEMENT_DYNAMIC(CSasamiSimpleInputDlg, CCustomBlurDialogExBase)

CSasamiSimpleInputDlg::CSasamiSimpleInputDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_SASAMI_SIMPLE_INPUT, pParent)
	, m_mode(ModeNumber), m_numVal(2), m_numMin(1), m_numMax(99)
{
}

int CSasamiSimpleInputDlg::AskNumber(CWnd* owner, const wchar_t* title, const wchar_t* prompt,
	int defVal, int minV, int maxV, int* outVal)
{
	CSasamiSimpleInputDlg dlg(owner);
	dlg.m_title = title ? title : L"Input";
	dlg.m_prompt = prompt ? prompt : L"";
	dlg.m_mode = ModeNumber;
	dlg.m_numVal = defVal;
	dlg.m_numMin = minV;
	dlg.m_numMax = maxV;
	if (dlg.DoModal() != IDOK) return IDCANCEL;
	if (outVal) *outVal = dlg.m_numVal;
	return IDOK;
}

int CSasamiSimpleInputDlg::AskText(CWnd* owner, const wchar_t* title, const wchar_t* prompt,
	wchar_t* buf, int bufCch)
{
	if (!buf || bufCch < 2) return IDCANCEL;
	CSasamiSimpleInputDlg dlg(owner);
	dlg.m_title = title ? title : L"Input";
	dlg.m_prompt = prompt ? prompt : L"";
	dlg.m_mode = ModeText;
	dlg.m_textVal = buf;
	if (dlg.DoModal() != IDOK) return IDCANCEL;
	wcsncpy_s(buf, bufCch, dlg.m_textVal, _TRUNCATE);
	return IDOK;
}

void CSasamiSimpleInputDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CSasamiSimpleInputDlg, CCustomBlurDialogExBase)
	ON_BN_CLICKED(IDOK, &CSasamiSimpleInputDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CSasamiSimpleInputDlg::OnBnClickedCancel)
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

BOOL CSasamiSimpleInputDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(m_title);
	CRect rc(0, 0, 360, 160);
	SetWindowPos(NULL, 0, 0, rc.Width(), rc.Height(), SWP_NOMOVE | SWP_NOZORDER);

	m_lbl.Create(m_prompt, WS_CHILD | WS_VISIBLE, CRect(0, 0, 10, 10), this, 1001);
	m_edit.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		CRect(0, 0, 10, 10), this, 1002);
	m_ok.Create(L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
		CRect(0, 0, 10, 10), this, IDOK);
	m_cancel.Create(L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
		CRect(0, 0, 10, 10), this, IDCANCEL);

	auto flat = [](CCustomStandardButton& b) {
		b.SetFlat(TRUE);
	};
	flat(m_ok); flat(m_cancel);

	if (m_mode == ModeNumber) {
		CString s; s.Format(L"%d", m_numVal);
		m_edit.SetWindowText(s);
	} else {
		m_edit.SetWindowText(m_textVal);
	}
	LayoutChrome();
	m_edit.SetFocus();
	m_edit.SetSel(0, -1);
	return FALSE;
}

void CSasamiSimpleInputDlg::LayoutChrome()
{
	CRect rc; GetClientRect(&rc);
	const int cap = CCC_GetCustomCaptionHeight(m_hWnd);
	const int pad = 12;
	m_lbl.MoveWindow(pad, cap + pad, rc.Width() - pad * 2, 22);
	m_edit.MoveWindow(pad, cap + pad + 28, rc.Width() - pad * 2, 26);
	const int bw = 72, bh = 26;
	m_ok.MoveWindow(rc.right - pad - bw * 2 - 8, rc.bottom - pad - bh, bw, bh);
	m_cancel.MoveWindow(rc.right - pad - bw, rc.bottom - pad - bh, bw, bh);
}

void CSasamiSimpleInputDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (m_edit.GetSafeHwnd()) LayoutChrome();
}

BOOL CSasamiSimpleInputDlg::OnEraseBkgnd(CDC* pDC)
{
	return CCustomBlurDialogExBase::OnEraseBkgnd(pDC);
}

void CSasamiSimpleInputDlg::OnPaint()
{
	CPaintDC dc(this);
	CCC_CaptionPaint(dc, m_hWnd);
}

void CSasamiSimpleInputDlg::OnBnClickedOk()
{
	CString t;
	m_edit.GetWindowText(t);
	if (m_mode == ModeNumber) {
		int v = _wtoi(t);
		if (v < m_numMin) v = m_numMin;
		if (v > m_numMax) v = m_numMax;
		m_numVal = v;
	} else {
		m_textVal = t;
	}
	EndDialog(IDOK);
}

void CSasamiSimpleInputDlg::OnBnClickedCancel()
{
	EndDialog(IDCANCEL);
}
