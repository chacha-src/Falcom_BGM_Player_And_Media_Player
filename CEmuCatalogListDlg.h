#pragma once
#include "afxcmn.h"
#include "CCustomControl.h"
#include "cemu_types.h"
#include <vector>

class CEmuCatListCtrl : public CCustomListCtrl
{
	DECLARE_DYNAMIC(CEmuCatListCtrl)
public:
	CEmuCatListCtrl() = default;
	/* dir: 0=元順（印なし） / 1=昇順 ▲ / 2=降順 ▼ */
	void SetSortState(int col, int dir);
protected:
	void BuildToolTipText(int row, int col, CString& out) override;
	BOOL GetListRowTint(int row, COLORREF& tintBg, COLORREF& accent) const override;
	DECLARE_MESSAGE_MAP()
	void EnsureSortArrows();
	CImageList m_sortIL;
	CString m_colTitle[7];
	BOOL m_colTitleReady = FALSE;
};

/* One list row = one archive group that has a local zip. */
struct CEmuCatListRow {
	const CEmuGameEntry* ge;
	CString title;
	CString modes;
	CString hayLower;
	CString zipPath;
	int origIndex = 0;
	int monFm = 0;   /* FMモニタ */
	int monMidi = 0; /* MIDIモニタ */
};

/* arcdata.zip 対応タイトル一覧（モデルレス。MP 閉じると一緒に閉じる） */
class CEmuCatalogListDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CEmuCatalogListDlg)
public:
	CEmuCatalogListDlg(CWnd* pParent = NULL);
	virtual ~CEmuCatalogListDlg();
	enum { IDD = IDD_CEMU_CATLIST };
	/* Open (or refresh) modeless. Closes with owner (MP). */
	static void Show(CWnd* pParent);
	static void CloseIfOpen();
	cmnh();
protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnEnChangeFilter();
	afx_msg void OnNMDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLvnColumnClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnDestroy();
	afx_msg void OnClose();
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg LRESULT OnFilterApply(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

	void LayoutControls();
	void LayoutColumns();
	void LayoutHelpBtn();
	void ShowHelpSheet();
	void RestoreSavedPlacement();
	void SaveSavedPlacement();
	/* Returns number of rows with a local zip. Re-scans disk each call. */
	int BuildRowCache();
	void ApplyFilterToList();
	void UpdateSortHeader();
	int PlaySelectedRow();

	CEmuCatListCtrl m_lc;
	CCustomEdit m_filter;
	CCustomStatic m_filterLbl;
	CCustomStatic m_desc;
	CCustomStandardButton m_ok;
	CCustomStandardButton m_help;
	CToolTipCtrl m_tooltip;
	int m_minW = 0;
	int m_minH = 0;
	BOOL m_bFilling = FALSE;
	std::vector<CEmuCatListRow> m_rows;
	unsigned m_filterGen = 0;
	size_t m_zipStemCount = 0; /* last scan size — detect newly added zips */
	int m_sortCol = -1; /* -1 = 元の順 */
	int m_sortDir = 0;   /* 0=元 1=昇順 2=降順 */
};
