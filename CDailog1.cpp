#include "stdafx.h"
#include "ogg.h"
#include "afxdialogex.h"
#include "CDailog1.h"

extern save savedata;
BEGIN_MESSAGE_MAP(CDailog1, CCustomBlurDialogBase)
	ON_WM_CREATE()
	ON_WM_MOVING()
	ON_WM_CTLCOLOR()
	ON_WM_TIMER()
	ON_WM_NCACTIVATE()
END_MESSAGE_MAP()

int CDailog1::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CCustomBlurDialogBase::OnCreate(lpCreateStruct) == -1)
		return -1;
	return 0;
}

void CDailog1::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogBase::OnMoving(fwSide, pRect);
}

HBRUSH CDailog1::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	return CCustomBlurDialogBase::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CDailog1::OnTimer(UINT_PTR nIDEvent)
{
	CCustomBlurDialogBase::OnTimer(nIDEvent);
}

BOOL CDailog1::OnNcActivate(BOOL bActive)
{
	return CCustomBlurDialogBase::OnNcActivate(bActive);
}
