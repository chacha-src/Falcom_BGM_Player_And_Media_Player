#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "oggDlg.h"
#include <vector>

// タグ / ジャケット編集（書き出しダイアログとは分離）
class CTagEditDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CTagEditDlg)

public:
	CTagEditDlg(CWnd* pParent = NULL);
	virtual ~CTagEditDlg();

	enum { IDD = IDD_TAGEDIT };
	playlistdata0 pc;
	std::vector<playlistdata0> pcs;
	bool multiFile;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	DECLARE_MESSAGE_MAP()

public:
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedSave();
	afx_msg void OnBnClickedClose();
	afx_msg void OnBnClickedCoverClear();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	void LayoutHelpBtn();
	void ShowHelpSheet();

	CCustomStatic m_titleL;
	CCustomEdit m_title;
	CCustomStatic m_artistL;
	CCustomEdit m_artist;
	CCustomStatic m_albumL;
	CCustomEdit m_album;
	CCustomStatic m_yearL;
	CCustomEdit m_year;
	CCustomStatic m_trackL;
	CCustomEdit m_track;
	CCustomStatic m_genreL;
	CCustomEdit m_genre;
	CCustomStatic m_commentL;
	CCustomEdit m_comment;
	CCustomStatic m_coverL;
	CStatic m_coverPic;
	CCustomStatic m_cover;
	CCustomStandardButton m_coverClear;
	CCustomStatic m_hint;
	CCustomStatic m_status;
	CCustomStandardButton m_save;
	CCustomStandardButton m_close;
	CCustomStandardButton m_help;
	CString m_coverPath;
	HBITMAP m_coverBmp;
	CToolTipCtrl m_tooltip;
};
