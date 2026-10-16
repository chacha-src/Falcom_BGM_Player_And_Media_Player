#pragma once
#include "afxwin.h"
#include "afxcmn.h"
#include "CCustomControl.h"
#include "Kpilist.h"
#include "FileTagInfo.h"
#include <vector>

class CPlayList;
struct playlistdata0;

class CPlayProperty_tab1 : public CCustomDialog
{
	DECLARE_DYNAMIC(CPlayProperty_tab1)
public:
	enum { IDD = IDD_PLAYPROP_TAB1 };
	CPlayProperty_tab1(CWnd* pParent = NULL);
	playlistdata0* m_pc;
	std::vector<int>* m_batch;
	void CollectPlaylistFields();
	void CollectTagAndLoopFields(FileTagFields& tags, int& loopStart, int& loopEnd);
	CString CurrentPathText() const;
	void RefreshStatusLines();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedExplorer();
	afx_msg void OnBnClickedBrowse();
	afx_msg void OnBnClickedTag2Pl();
	afx_msg void OnBnClickedReloadTag();
	afx_msg void OnBnClickedWriteTag();
	afx_msg void OnBnClickedCopyPath();
	afx_msg void OnBnClickedCopyName();
	afx_msg void OnBnClickedProTools();
	afx_msg void OnBnClickedClearParam();
	DECLARE_MESSAGE_MAP()

	void ApplyTagsToControls(const FileTagFields& tags, bool forceEmpty = false);
	bool IsBatchMode() const { return m_batch && m_batch->size() > 1; }
	void ApplyBatchUi();

	CCustomEdit m_name, m_id, m_game, m_art, m_alb, m_fol;
	CCustomEdit m_cmt, m_year, m_track, m_j, m_time, m_loop1, m_loop2, m_ret2;
	CCustomStatic m_lblName, m_lblId, m_lblGame, m_lblArt, m_lblAlb, m_lblFile;
	CCustomStatic m_lblYear, m_lblTrack, m_lblGenre, m_lblCmt, m_lblTime, m_lblLoop, m_lblRet2;
	CCustomStatic m_lblStatus, m_lblParam, m_lblMeta;
	CCustomStandardButton m_btnBrowse, m_btnTag2Pl, m_btnReloadTag, m_btnWriteTag;
	CCustomStandardButton m_btnCopyPath, m_btnCopyName, m_btnProTools, m_btnClearParam, m_btnExplorer;
	CCustomGroupBox m_grpEdit, m_grpTag, m_grpInternal;
	CToolTipCtrl m_tooltip;
};

class CPlayProperty_tab2 : public CCustomDialog
{
	DECLARE_DYNAMIC(CPlayProperty_tab2)
public:
	enum { IDD = IDD_PLAYPROP_TAB2 };
	CPlayProperty_tab2(CWnd* pParent = NULL);
	virtual ~CPlayProperty_tab2();
	playlistdata0* m_pc;
	std::vector<playlistdata0> m_pcs;
	bool m_multi;
	BOOL WriteTags();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedSave();
	afx_msg void OnBnClickedCoverClear();
	afx_msg void OnDropFiles(HDROP hDropInfo);
	DECLARE_MESSAGE_MAP()

	CCustomStatic m_titleL, m_artistL, m_albumL, m_yearL, m_trackL, m_genreL, m_commentL, m_coverL;
	CCustomEdit m_title, m_artist, m_album, m_year, m_track, m_genre, m_comment;
	CStatic m_coverPic;
	CCustomStatic m_cover, m_hint, m_status;
	CCustomStandardButton m_coverClear, m_save;
	CString m_coverPath;
	HBITMAP m_coverBmp;
	CToolTipCtrl m_tooltip;
};

class CPlayProperty_tab3;
class CPlayPropCfgList : public CCustomListCtrl
{
	DECLARE_DYNAMIC(CPlayPropCfgList)
public:
	CPlayPropCfgList() = default;
protected:
	BOOL WantLiveEditRow(int row) override;
	void OnLiveEditText(int row, LPCTSTR text) override;
};

class CPlayProperty_tab3 : public CCustomDialog
{
	DECLARE_DYNAMIC(CPlayProperty_tab3)
public:
	enum { IDD = IDD_PLAYPROP_TAB3 };
	CPlayProperty_tab3(CWnd* pParent = NULL);
	int m_kpiIdx;
	BOOL m_pluginLoaded;
	void LoadPlugin(int idx);
	void SaveCfgRow(int row);
	friend class CPlayPropCfgList;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnLvnItemchangedCfg(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnCfgDblClk(NMHDR* pNMHDR, LRESULT* pResult);
	DECLARE_MESSAGE_MAP()

	CKpiInfoList m_info;
	CPlayPropCfgList m_cfg;
	CCustomEdit m_cfgHelp;
	CCustomStatic m_kpiName, m_infoLbl, m_cfgLbl;
	BOOL m_bFillingDetail;
	int m_inN;
	int m_cfgN;
	wchar_t m_inName[32][48];
	wchar_t m_inVal[32][512];
	int m_cfgType[96];
	BYTE m_cfgLock[96];
	wchar_t m_cfgSec[96][96];
	wchar_t m_cfgKey[96][96];
	wchar_t m_cfgDesc[96][192];
	wchar_t m_cfgVal[96][512];
	wchar_t m_cfgHelpTxt[96][768];
	wchar_t m_cfgList[96][512];
	wchar_t m_cfgDef[96][256];
};

class CPlayProperty_tab4 : public CCustomDialog
{
	DECLARE_DYNAMIC(CPlayProperty_tab4)
public:
	enum { IDD = IDD_PLAYPROP_TAB4 };
	CPlayProperty_tab4(CWnd* pParent = NULL);
	CPlayList* m_pl;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnMoreCmd(UINT nID);
	DECLARE_MESSAGE_MAP()

	CCustomStatic m_hint;
	CCustomStandardButton m_btnExplorer, m_btnOpenFolder, m_btnCopyPath, m_btnCopyName;
	CCustomStandardButton m_btnProTools, m_btnClearParam, m_btnWav, m_btnTranscode, m_btnVideo;
	CCustomStandardButton m_btnBpm, m_btnTagBatch, m_btnQueue, m_btnPlayNext, m_btnAddSame, m_btnMidExport;
};

class CPlayProperty : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CPlayProperty)
public:
	enum { IDD = IDD_PLAYPROP };
	CPlayProperty(CWnd* pParent = NULL);
	playlistdata0 pc;
	std::vector<int> m_batchIndices;
	CPlayList* m_pl;
	BOOL ApplyChanges();
	static int FindKpiForPath(LPCTSTR path);

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedApply();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnTabSelChange(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDestroy();
	DECLARE_MESSAGE_MAP()

	void LayoutPages();
	void ShowPage(int idx);
	void EnsurePage(int page);

	CCustomTabCtrl m_tabs;
	CCustomStandardButton m_ok, m_cancel, m_apply;
	CPlayProperty_tab1 m_tab1;
	CPlayProperty_tab2 m_tab2;
	CPlayProperty_tab3 m_tab3;
	CPlayProperty_tab4 m_tab4;
	int m_kpiIdx;
	int m_minW;
	int m_minH;
	CToolTipCtrl m_tooltip;
};
