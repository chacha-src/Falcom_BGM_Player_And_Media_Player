#pragma once

#include "CCustomControl.h"
#include "resource.h"

// SASAMI テキスト／譜面コンポーザ — コマンド説明（SASAMIM.HLP 調）
class CSasamiCmdHelpDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiCmdHelpDlg)
public:
	explicit CSasamiCmdHelpDlg(CWnd* pParent = nullptr);
	virtual ~CSasamiCmdHelpDlg();

	enum { IDD = IDD_SASAMI_CMD_HELP };

	enum TabId {
		kTabMidi1 = 0,
		kTabMidi2 = 1,
		kTabFm1 = 2,
		kTabFm2 = 3,
		kTabCommon = 4,
		kTabScore1 = 5,
		kTabScore2 = 6,
		kTabScore3 = 7,
		kTabN = 8
	};

	// initialTab: TabId。負数なら前回の章。
	static void Show(CWnd* owner, int initialTab = -1);
	static CSasamiCmdHelpDlg* Instance() { return s_inst; }

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnClose();
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnTabSelChange(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnCopy();
	DECLARE_MESSAGE_MAP()

	void LayoutChrome();
	void PaintChapter(CDC& dc, int chapter, int maxTextW, int& outBottom);
	void CopyChapterText(int chapter);
	void InvalidateBody();

	CCustomTabCtrl m_tabs;
	CCustomStandardButton m_copy;
	CStatic m_body;

	CRect m_bodyRc;
	CDC m_mem;
	CBitmap m_memBmp;
	CBitmap* m_memOldBmp;
	int m_memW;
	int m_memH;

	int m_chapter;
	int m_contentBottom;
	int m_scrollY;
	int m_scrollMax;
	CToolTipCtrl m_tooltip;

	static CSasamiCmdHelpDlg* s_inst;
};
