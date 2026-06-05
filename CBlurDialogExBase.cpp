#include "stdafx.h"
#include "CBlurDialogExBase.h"

IMPLEMENT_DYNAMIC(CBlurDialogExBase, CCustomBlurDialogExBase)

BEGIN_MESSAGE_MAP(CBlurDialogExBase, CDialogEx)
	ON_WM_CREATE()
	ON_WM_CTLCOLOR()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CBlurDialogExBase::CBlurDialogExBase()
	: CDialogEx()
{
}

CBlurDialogExBase::CBlurDialogExBase(UINT nIDTemplate, CWnd* pParent)
	: CDialogEx(nIDTemplate, pParent)
{
}

CBlurDialogExBase::~CBlurDialogExBase()
{
}

BOOL CBlurDialogExBase::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	
	// DWM Blur効果を適用
	ApplyDwmBlur();
	
	return TRUE;
}

int CBlurDialogExBase::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDialogEx::OnCreate(lpCreateStruct) == -1)
		return -1;
	
	// DWM Blur効果を適用
	ApplyDwmBlur();
	
	return 0;
}

void CBlurDialogExBase::ApplyDwmBlur()
{
	if (!m_hWnd || !::IsWindow(m_hWnd))
		return;
	
	COSVersion os;
	os.GetVersionString();
	
	// Windows 11の場合：Acrylic効果を適用
	if (os.in.dwMajorVersion == 10 && os.in.dwBuildNumber >= 22000)
	{
		// Windows 11: Acrylic Blur効果
		EnableDwmAcrylicWin11(m_hWnd);
		
		// 角を丸くする
		EnableRoundedCorners(m_hWnd);
	}
	// Windows 10の場合：Blur Behind効果を適用
	else if (os.in.dwMajorVersion == 10)
	{
		// Windows 10: DWM Blur Behind
		EnableDwmBlurBehindWin10(m_hWnd);
	}
	// Windows Vista/7/8の場合：Blur Behind効果を適用
	else if (os.in.dwMajorVersion >= 6)
	{
		// Windows Vista/7/8: DWM Blur Behind
		EnableDwmBlurBehindWin10(m_hWnd);
	}
}

HBRUSH CBlurDialogExBase::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);
	
	COSVersion os;
	os.GetVersionString();
	
	// Windows 11の場合：背景を透明にする
	if (os.in.dwMajorVersion == 10 && os.in.dwBuildNumber >= 22000)
	{
		if (nCtlColor == CTLCOLOR_DLG)
		{
			pDC->SetBkMode(TRANSPARENT);
			return (HBRUSH)::GetStockObject(HOLLOW_BRUSH);
		}
		if (nCtlColor == CTLCOLOR_STATIC)
		{
			pDC->SetBkMode(TRANSPARENT);
			return (HBRUSH)::GetStockObject(HOLLOW_BRUSH);
		}
	}
	// Windows 10以前の場合：半透明の背景
	else if (os.in.dwMajorVersion >= 6)
	{
		if (nCtlColor == CTLCOLOR_DLG)
		{
			pDC->SetBkMode(TRANSPARENT);
			return (HBRUSH)::GetStockObject(HOLLOW_BRUSH);
		}
		if (nCtlColor == CTLCOLOR_STATIC)
		{
			pDC->SetBkMode(TRANSPARENT);
			return (HBRUSH)::GetStockObject(HOLLOW_BRUSH);
		}
	}
	
	return hbr;
}

BOOL CBlurDialogExBase::OnEraseBkgnd(CDC* pDC)
{
	COSVersion os;
	os.GetVersionString();
	
	// Windows Vista以上の場合：背景を消さない（DWMが描画する）
	if (os.in.dwMajorVersion >= 6)
	{
		return TRUE; // 背景を消さない
	}
	
	return CDialogEx::OnEraseBkgnd(pDC);
}

