// CMpCommandPaletteDlg.h : メディアプレイヤーのコマンドパレット(モードレス・シングルトン)
//
// Ctrl+K / バナー右クリックメニューから開く。実行は mp / og への WM_COMMAND 中継を基本とし、
// メニューの Track() 戻り値でしか処理していない Soft3D 系だけ savedata + 各窓の同期で行う。
//
#pragma once

#include "CCustomControl.h"
#include "resource.h"

class CMpCommandPaletteDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CMpCommandPaletteDlg)
public:
	explicit CMpCommandPaletteDlg(CWnd* pParent = nullptr);
	virtual ~CMpCommandPaletteDlg();
	enum { IDD = IDD_MP_CMDPAL };

	// owner 付きモードレスで開く。既に開いていれば前面へ出してフィルタへフォーカス。
	static void OpenPalette(CWnd* owner);
	static void CloseIfOpen();

protected:
	CCustomEdit m_filter;
	CCustomListCtrl m_list;
	CCustomStandardButton m_run;
	CCustomStandardButton m_close;
	CBrush m_brDlg;
	CToolTipCtrl m_tooltip;

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	// モードレスなので EndDialog を通さない
	virtual void OnOK();
	virtual void OnCancel();

	afx_msg void OnFilterChange();
	afx_msg void OnBnClickedRun();
	afx_msg void OnBnClickedClose();
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnClose();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	void RebuildList();
	int  GetSelectedRow() const;
	int  GetSelectedCmdId() const;
	void RunSelected();
	void ExecCommand(int id);

	DECLARE_MESSAGE_MAP()
};

// CMediaPlayerDlg::OpenCommandPalette から呼ぶ入口
void MpOpenCommandPalette(CWnd* owner);
