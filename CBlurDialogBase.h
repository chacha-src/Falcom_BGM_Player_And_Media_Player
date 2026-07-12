#pragma once

#include <afxwin.h>
#include <afxtempl.h>
#include <uxtheme.h>
#include <commctrl.h>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

#include "DwmBlurHelper.h"
#include "OSVersion.h"

// ---------------------------------------------------------
// CControlFixer (Win32 API版)
// ---------------------------------------------------------
class CControlFixer
{
public:
	CControlFixer();
	virtual ~CControlFixer();

	BOOL Install(HWND hWnd);
	void Uninstall();

protected:
	HWND m_hWnd;
	BOOL m_bPrinting; // ★追加：WM_PRINT処理中フラグ

	static LRESULT CALLBACK SubclassProc(
		HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

	void OnPaint(HWND hWnd, HDC hDC);
};

// ---------------------------------------------------------
// CBlurDialogBase
// ---------------------------------------------------------
class CBlurDialogBase : public CDialog
{
	DECLARE_DYNAMIC(CBlurDialogBase)

public:
	CBlurDialogBase();
	CBlurDialogBase(UINT nIDTemplate, CWnd* pParent = NULL);
	virtual ~CBlurDialogBase();

protected:
	virtual BOOL OnInitDialog();
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);

	void ApplyDwmBlur();
	void DebugOutput(LPCTSTR format, ...);
	void FixListControlsBackground();

	// コントロール不透明化のメイン関数
	void FixChildOpacity();
	// ★追加：再帰的に子ウィンドウを探すヘルパー
	void RecursiveApplyFix(HWND hWndParent);

	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
	afx_msg void OnNcPaint();
	afx_msg void OnPaint();
	afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
	afx_msg void OnDestroy();

	DECLARE_MESSAGE_MAP()

private:
	BOOL m_bBlurApplied;
	CTypedPtrList<CPtrList, CControlFixer*> m_fixerList;
};