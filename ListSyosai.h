#pragma once
#include "afxwin.h"
#include "CCustomControl.h"

// CListSyosai ダイアログ

class CListSyosai : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CListSyosai)

public:
	CListSyosai(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CListSyosai();

// ダイアログ データ
	enum { IDD = IDD_SYOSAI };


	playlistdata0 pc;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	CToolTipCtrl m_tooltip;
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	DECLARE_MESSAGE_MAP()

	cmnh();
public:
	CCustomEdit m_name;
	CCustomEdit m_id;
	CCustomEdit m_game;
	CCustomEdit m_art;
	CCustomEdit m_alb;
	CCustomEdit m_fol;
	afx_msg
		void OnClose();
	void OnBnClickedOk2();
	void OnBnClickedOk();
	virtual BOOL OnInitDialog();
	CCustomStandardButton m_ok2;
	CCustomEdit m_cmt;
	CCustomEdit m_year;
	CCustomEdit m_track;
	CCustomEdit m_j;
	CCustomStandardButton m_ok;
	CCustomStandardButton m_cancel;
	CCustomEdit m_time;
	CCustomEdit m_loop1;
	CCustomEdit m_loop2;
	CCustomEdit m_ret2;
	CCustomStatic m_lblName;
	CCustomStatic m_lblId;
	CCustomStatic m_lblGame;
	CCustomStatic m_lblArt;
	CCustomStatic m_lblAlb;
	CCustomStatic m_lblFile;
	CCustomStatic m_lblYear;
	CCustomStatic m_lblTrack;
	CCustomStatic m_lblGenre;
	CCustomStatic m_lblCmt;
	CCustomStatic m_lblTime;
	CCustomStatic m_lblLoop;
	CCustomStatic m_lblRet2;
};
