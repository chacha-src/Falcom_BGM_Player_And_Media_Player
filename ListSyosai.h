#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "FileTagInfo.h"
#include <vector>

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
	// 複数選択時の一括編集対象(空=単曲モード)。インデックスはプレイリスト行。
	std::vector<int> m_batchIndices;

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
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnBnClickedExplorer();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedBrowse();
	afx_msg void OnBnClickedTag2Pl();
	afx_msg void OnBnClickedReloadTag();
	afx_msg void OnBnClickedWriteTag();
	afx_msg void OnBnClickedCopyPath();
	afx_msg void OnBnClickedCopyName();
	afx_msg void OnBnClickedProTools();
	afx_msg void OnBnClickedClearParam();
	afx_msg void OnBnClickedHelp();
	virtual BOOL OnInitDialog();
	CCustomStandardButton m_ok2;
	CCustomStandardButton m_help;
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
	CCustomStatic m_lblStatus;
	CCustomStatic m_lblParam;
	CCustomStandardButton m_btnBrowse;
	CCustomStandardButton m_btnTag2Pl;
	CCustomStandardButton m_btnReloadTag;
	CCustomStandardButton m_btnWriteTag;
	CCustomStandardButton m_btnCopyPath;
	CCustomStandardButton m_btnCopyName;
	CCustomStandardButton m_btnProTools;
	CCustomStandardButton m_btnClearParam;

private:
	bool IsBatchMode() const { return m_batchIndices.size() > 1; }
	void CollectPlaylistFields();
	void CollectTagAndLoopFields(FileTagFields& tags, int& loopStart, int& loopEnd);
	void ApplyTagsToControls(const FileTagFields& tags, bool forceEmpty = false);
	void RefreshStatusLines();
	void ApplyBatchUi();
	void LayoutHelpBtn();
	void ShowHelpSheet();
	CString CurrentPathText() const;
};
