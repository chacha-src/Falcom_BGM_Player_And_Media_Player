#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "oggDlg.h"
#include <vector>

// mp3 / FLAC 書き出し。WAV書き出しと同じく一旦PCM化し、形式だけ変換する。
class CTranscodeExport : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CTranscodeExport)

public:
	CTranscodeExport(CWnd* pParent = NULL);
	virtual ~CTranscodeExport();

	enum { IDD = IDD_TRANSCODE };
	playlistdata0 pc;
	std::vector<playlistdata0> pcs;
	bool multiFile;
	int m_initialTab; // 0=WAV 1=mp3 2=FLAC (-1=保存値から mp3/FLAC)
	bool m_preferXfade; // コンテキストメニューからクロスフェード起動

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	DECLARE_MESSAGE_MAP()

	void RefreshQualityLabels();
	void ApplyTabUi();
	void ApplyKpiDurationUi();
	void ApplyPathModeUi(bool keepPathText);
	void SyncTitleFieldForPathMode();
	void ApplyMixUi();
	void RebuildMixVolList();
	void NormalizeMixPercents();
	BOOL SelectionHasKpi() const;
	int  DefaultKpiDurationSec() const;
	void PersistKpiDurationFromUi();
	int  CurrentFormat() const; // -1=WAV, 0=mp3, 1=FLAC
	bool IsXfadeMode();
	bool IsMixMode();
	CString ExtForFormat(int fmt) const;
	CString FilterForFormat(int fmt) const;
	CString NormalizeOutPath(const CString& pathIn, int fmt) const;
	CString OutputPathForItem(const CString& folderIn, const playlistdata0& item, int fmt) const;

public:
	virtual BOOL OnInitDialog();
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg LRESULT OnLayoutTabsMsg(WPARAM wParam, LPARAM lParam);
	afx_msg void OnBnClickedExec();
	afx_msg void OnBnClickedBrowse();
	afx_msg void OnBnClickedClose();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnCbnSelchangeFormat();
	afx_msg void OnTcnSelchangeTabs(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnBnClickedCoverClear();
	afx_msg void OnBnClickedXfade();
	afx_msg void OnBnClickedMix();
	afx_msg void OnCbnSelchangeMixN();
	afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);

protected:
	enum { WM_TC_LAYOUT_TABS = WM_APP + 7101 };
	void LayoutTabsBelowCaption();
	void LayoutHelpBtn();
	void ShowHelpSheet();

	CCustomStandardButton m_help;
	CCustomTabCtrl m_tabs;
	CCustomComboBox m_format;
	CCustomStatic m_formatLabel;
	CCustomComboBox m_quality;
	CCustomStatic m_qualityLabel;
	CCustomEdit m_loop;
	CCustomStatic m_kpiSecLabel;
	CCustomEdit m_kpiSec;
	CCustomStatic m_srateLabel;
	CCustomComboBox m_srate;
	CCustomEdit m_path;
	CCustomStatic m_status;
	CCustomStatic m_loopLabel;
	CCustomStatic m_pathLabel;
	CCustomStandardButton m_browse;
	CCustomStandardButton m_exec;
	CCustomStandardButton m_close;
	CCustomCheckBox m_fadeCheck;
	CCustomEdit m_fadeSec;
	CCustomStatic m_fadeLabel;
	CCustomCheckBox m_xfadeCheck;
	CCustomEdit m_xfadeSec;
	CCustomStatic m_xfadeLabel;
	CCustomCheckBox m_trimCheck;
	CCustomEdit m_trimSec;
	CCustomStatic m_trimLabel;
	CCustomCheckBox m_copyTags;
	CCustomCheckBox m_promptCheck;
	CCustomCheckBox m_mixCheck;
	CCustomStatic m_mixNLabel;
	CCustomComboBox m_mixN;
	CCustomListCtrl m_mixVol;
	CCustomProgressCtrl m_progress;
	CCustomStatic m_titleL;
	CCustomEdit m_title;
	CCustomStatic m_artistL;
	CCustomEdit m_artist;
	CCustomStatic m_albumL;
	CCustomEdit m_album;
	CCustomStatic m_coverL;
	CStatic m_coverPic;
	CCustomStatic m_cover;
	CCustomStandardButton m_coverClear;
	CString m_coverPath;
	HBITMAP m_coverBmp;
	CToolTipCtrl m_tooltip;
	int m_mixPct[64]; // 選択曲ごとの音量％（合計は正規化）
	int m_mixPctCount;
	int m_mixEditRow; // 割合インライン編集中の行

	static void ExportProgressThunk(int percent, LPCTSTR status, void* user);
};

// wavPath: 本アプリの書き出しWAV(80byteヘッダ)。outPath: .mp3 / .flac
BOOL EncodeWavToMp3(const CString& wavPath, const CString& outPath, int bitrateKbps);
BOOL EncodeWavToFlac(const CString& wavPath, const CString& outPath, int compressionLevel);
