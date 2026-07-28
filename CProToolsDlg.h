#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "ProAudio.h"

// 再生詳細(ギャップレス/RG/M/S/ループ/キュー/タグ)を1ダイアログに集約
// EQ/アナ同様のモードレス。DoModal にしない(開いたままアプリ終了できなくなるため)。
// 「メインに追従」は付けない(設定窓であり表示追従の対象外)。
class CProToolsDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CProToolsDlg)
public:
	CProToolsDlg(CWnd* pParent = NULL);
	virtual ~CProToolsDlg();
	enum { IDD = IDD_PROTOOLS };
	playlistdata0 pc; // ループ/キュー対象曲(空なら現在再生曲)
	bool hasTrack;

	void LoadTrackFromSelection();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()

	void LoadFromSavedata();
	void SaveToSavedata();
	void ApplyLiveFlags(); // チェック/M/S など即時反映(適用ボタン不要)
	void RefreshCueList();
	void DrawWave(CDC& dc, const CRect& rc);
	void ApplyLoopFromUi();
	void CloseModeless();
	void SetupToolTips();

	float m_peaksL[PRO_WAVE_PEAKS];
	float m_peaksR[PRO_WAVE_PEAKS];
	int m_peakCount;
	int m_totalFrames;
	int m_loopIn;
	int m_loopOut;
	CToolTipCtrl m_tooltip;

public:
	virtual void OnOK();
	virtual void OnCancel();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedApply();
	afx_msg void OnBnClickedCueAdd();
	afx_msg void OnBnClickedCueDel();
	afx_msg void OnBnClickedCueJump();
	afx_msg void OnBnClickedLoopIn();
	afx_msg void OnBnClickedLoopOut();
	afx_msg void OnBnClickedWriteTag();
	afx_msg void OnBnClickedLiveFlag();
	afx_msg void OnCbnSelchangeRgMode();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnEnKillfocusLiveEdit();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();

	CCustomCheckBox m_gapless;
	CCustomComboBox m_rgMode;
	CCustomEdit m_rgTarget;
	CCustomSliderCtrl m_msWidth;
	CCustomStatic m_msVal;
	CCustomCheckBox m_msMono;
	CCustomCheckBox m_expLimit;
	CCustomEdit m_expCeil;
	CCustomCheckBox m_expTp;
	CCustomCheckBox m_corr;
	CCustomListBox m_cues;
	CCustomEdit m_loopInEdit;
	CCustomEdit m_loopOutEdit;
	CCustomEdit m_loopFadeEdit;
	CCustomEdit m_tagTitle;
	CCustomEdit m_tagArtist;
	CCustomEdit m_tagAlbum;
	CCustomEdit m_tagYear;
	CCustomEdit m_tagTrack;
	CCustomEdit m_tagGenre;
	CCustomEdit m_tagComment;
	CCustomStandardButton m_ok;
	CCustomStandardButton m_apply;
	CCustomStandardButton m_cueAdd;
	CCustomStandardButton m_cueDel;
	CCustomStandardButton m_cueJump;
	CCustomStandardButton m_loopInBtn;
	CCustomStandardButton m_loopOutBtn;
	CCustomStandardButton m_writeTag;
	CRect m_waveRc;
};

void OpenProToolsForSelection();
void CloseProToolsIfOpen();
