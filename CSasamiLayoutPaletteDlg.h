#pragma once

#include "CCustomControl.h"

#include "CSasamiNotePaletteDlg.h"

class CSasamiLayoutPaletteDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CSasamiLayoutPaletteDlg)
public:
	enum { IDD = IDD_SASAMI_LAYOUT_PAL };
	enum { kTabCount = 4, kItemMax = 48 };

	enum LayItemKind {
		LAY_ITEM_HEADER = 0,
		LAY_ITEM_KEY,
		LAY_ITEM_METER_N,
		LAY_ITEM_METER_D,
		LAY_ITEM_CLEF,
		LAY_ITEM_TR_SCOPE,
		LAY_ITEM_TR_ACT,
		LAY_ITEM_ACTION
	};

	struct LayItem {
		CRect rc;
		int kind;
		int value;
		int cmdId;
		wchar_t label[24];
	};

	CSasamiLayoutPaletteDlg(CWnd* pParent = nullptr);

	static CSasamiLayoutPaletteDlg* OpenNear(CWnd* owner, CPoint screenPt);
	static CSasamiLayoutPaletteDlg* Instance();

protected:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg BOOL OnTtnNeedText(UINT id, NMHDR* pNMHDR, LRESULT* pResult);
	virtual void PostNcDestroy();

	void LayoutChrome();
	void ApplyLang();
	void SetupItemTips();
	void PostCmd(int cmdId);
	void PostLayout(int kind, int a, int b);
	void BuildTabItems();
	void AddItem(int kind, int value, int cmdId, const wchar_t* label);
	void LayoutKeyGrid(const CRect& area);
	void LayoutMeterGrid(const CRect& area);
	void LayoutRowList(const CRect& area);
	void QueryState();
	int ItemSelected(const LayItem& it) const;
	int HitTab(CPoint pt) const;
	int HitItem(CPoint pt) const;
	void DrawPreview(CDC& dc, const CRect& rc) const;
	void DrawHeader(CDC& dc, const CRect& rc, const wchar_t* text) const;
	void DrawKeyGroups(CDC& dc, const CRect& area) const;
	const wchar_t* ItemTip(int i) const;
	void ActivateItem(int i);

	int m_tab;
	int m_itemCount;
	LayItem m_items[kItemMax];
	int m_hoverItem;
	int m_hoverSemi;
	int m_trScope;
	int m_trackMouse;
	CRect m_tabs[kTabCount];
	CRect m_previewRc;
	CRect m_contentRc;
	CRect m_keyMajorRc;
	CRect m_keyMinorRc;
	SasamiPalLayoutState m_st;
	CToolTipCtrl m_tip;
	HWND m_notify;
	static CSasamiLayoutPaletteDlg* s_inst;

	DECLARE_MESSAGE_MAP()
};
