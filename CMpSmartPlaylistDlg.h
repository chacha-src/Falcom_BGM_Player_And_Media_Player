#pragma once

#include "CCustomControl.h"
#include "MpSidecar.h"
#include "resource.h"

class CMpSmartPlaylistDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CMpSmartPlaylistDlg)
public:
	CMpSmartPlaylistDlg(CWnd* pParent = NULL);
	virtual ~CMpSmartPlaylistDlg();
	enum { IDD = IDD_MP_SMART };

	// IDOK: applied rule index; IDCANCEL / close: -1
	int m_appliedIndex;

protected:
	CCustomListCtrl m_lc;
	CCustomEdit m_name;
	CCustomCheckBox m_unplayed;
	CCustomCheckBox m_missing;
	CCustomCheckBox m_rating;
	CCustomEdit m_ratingN;
	CCustomCheckBox m_artistCk;
	CCustomEdit m_artist;
	CCustomCheckBox m_hourCk;
	CCustomEdit m_hourFrom;
	CCustomEdit m_hourTo;
	CCustomCheckBox m_playMaxCk;
	CCustomEdit m_playMax;
	CCustomStandardButton m_add;
	CCustomStandardButton m_update;
	CCustomStandardButton m_del;
	CCustomStandardButton m_apply;
	CCustomStandardButton m_close;
	CBrush m_brDlg;
	CToolTipCtrl m_tooltip;
	int m_sel;

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedAdd();
	afx_msg void OnBnClickedUpdate();
	afx_msg void OnBnClickedDel();
	afx_msg void OnBnClickedApply();
	afx_msg void OnBnClickedClose();
	afx_msg void OnClose();
	afx_msg void OnLvnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	void RebuildList();
	void LoadSelToUi();
	void UiToRule(MpSmartRule& r);
	int GetSelectedRule() const;

	DECLARE_MESSAGE_MAP()
};
