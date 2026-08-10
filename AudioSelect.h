#pragma once
#include "afxwin.h"
#include "CCustomControl.h"

// CAudioSelect ダイアログ（音声＋字幕）

class CAudioSelect : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CAudioSelect)

public:
	CAudioSelect(CWnd* pParent = NULL);
	virtual ~CAudioSelect();

	enum { IDD = IDD_AUDIOSELECT };

	// 入力: 一覧件数 / 出力: 選択結果
	int audioCount;
	int subCount;
	int no;     // 選択音声 index（0..）
	int subNo;  // 選択字幕 index（-1=なし, 0..）

protected:
	CToolTipCtrl m_tooltip;
	virtual void DoDataExchange(CDataExchange* pDX);

	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnLbnDblclkList1();
	afx_msg void OnLbnDblclkSubList();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	virtual void OnCancel();
	CCustomListBox m_lb;
	CCustomListBox m_lbSub;
	CCustomStandardButton m_okdummy;
	CCustomStatic m_desc;
	CCustomStatic m_audioLbl;
	CCustomStatic m_subLbl;
	virtual BOOL OnInitDialog();

private:
	void CommitAndClose(BOOL forceSubOff);
	void LayoutNoSubtitles();
	int PickOsLocaleAudio() const;
};
