#pragma once
// CEmuHelpDlg — CEmu (hoot archive) 操作ガイド
#include "CCustomControl.h"
#include "GdiSoft2D.h"
#include "GdiSoft3D.h"
#include "resource.h"

class CEmuHelpDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CEmuHelpDlg)
public:
	explicit CEmuHelpDlg(CWnd* pParent = nullptr);
	virtual ~CEmuHelpDlg();
	enum { IDD = IDD_CEMU_HELP };

	static void ShowModal(CWnd* pParent);
	static void CloseIfOpen();

protected:
	enum { kChapterN = 4, kAnimTimerId = 1 };

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void OnOK();
	virtual void OnCancel();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnClose();
	afx_msg void OnTabSelChange(NMHDR* pNMHDR, LRESULT* pResult);
	DECLARE_MESSAGE_MAP()

	void LayoutChrome();
	void FitWindowToContent();

	CCustomTabCtrl m_tabs;
	CStatic m_body;
	CRect m_bodyRc;
	CDC m_mem;
	CBitmap m_memBmp;
	CBitmap* m_memOldBmp;
	int m_memW;
	int m_memH;
	GdiSoft3D::Context m_demo3d;
	GdiSoft3D::Cam m_demoCam;
	GdiSoft2D::Context m_demo2d;
	DWORD m_animTick;
	UINT_PTR m_timer;
	int m_chapter;
	int m_contentBottom;
	CToolTipCtrl m_tooltip;

	static CEmuHelpDlg* s_inst;
};
