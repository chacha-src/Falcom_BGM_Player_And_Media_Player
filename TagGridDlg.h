#pragma once
#include "CCustomControl.h"
#include "FileTagInfo.h"
#include <vector>
#include <map>

// プレイリストとは別に、ドロップしたファイルを一覧でまとめて直すタグ編集。
class CTagGridDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CTagGridDlg)
public:
	CTagGridDlg(CWnd* pParent = NULL);
	virtual ~CTagGridDlg();
	enum { IDD = IDD_TAGGRID };
	void Seed(const std::vector<CString>& paths);

	struct Row {
		CString path;
		CString fileName;
		FileTagFields tags;
		CString coverPath;
		bool dirty;
		Row() : dirty(false) {}
	};

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void OnOK();
	virtual void OnCancel();

	std::vector<CString> m_seed;
	std::vector<Row> m_rows;
	std::map<CString, int> m_groups;
	bool m_adding;
	CCustomListCtrl m_list;
	CCustomStandardButton m_add, m_dir, m_name, m_num, m_save, m_close, m_coverBrowse, m_coverClear, m_lyrApply;
	CCustomEdit m_pat, m_lyrics, m_cell;
	CCustomStatic m_lyricsL, m_coverH, m_status, m_hint;
	CStatic m_coverPic;
	HBITMAP m_coverBmp;
	int m_editRow, m_editCol;
	bool m_inCommit;
	bool m_fillLyrics;
	bool m_lyrDirty;
	DWORD m_menuTick;
	std::vector<int> m_lyrSel;

	void AddPaths(const std::vector<CString>& paths);
	void RefreshRow(int i);
	void StatusCount();
	void Selected(std::vector<int>& ix);
	void BeginEdit(int row, int col);
	void CommitCell();
	void CancelCell();
	void FlushLyrics();
	void ShowSide();
	void Mark(const std::vector<int>& ix);
	void ApplyCoverFile(const CString& image);
	bool AnyDirty() const;
	void DoSave();
	void ShowGridMenu(CPoint screenPt);

	afx_msg void OnAdd();
	afx_msg void OnDir();
	afx_msg void OnName();
	afx_msg void OnNum();
	afx_msg void OnSave();
	afx_msg void OnCloseBtn();
	afx_msg void OnCoverBrowse();
	afx_msg void OnCoverClear();
	afx_msg void OnLyrApply();
	afx_msg void OnLyricsChange();
	afx_msg void OnListRClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnListClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnListDbl(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnCellKill();
	afx_msg void OnDropFiles(HDROP hDrop);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	DECLARE_MESSAGE_MAP()
};
