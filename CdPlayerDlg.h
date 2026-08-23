#pragma once

#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"
#include <mmsystem.h>

class CCdCoverCtrl : public CCustomStatic
{
	DECLARE_DYNAMIC(CCdCoverCtrl)
public:
	CCdCoverCtrl();
	virtual ~CCdCoverCtrl();
	void SetImage(HBITMAP hbmp);
	void ClearImage();
	void PaintToDC(CDC& dc);

protected:
	HBITMAP m_bmp;
	int m_w, m_h;
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg LRESULT OnPrintClient(WPARAM wParam, LPARAM lParam);
};

class CCdPlayerDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CCdPlayerDlg)
public:
	CCdPlayerDlg(CWnd* pParent = NULL);
	virtual ~CCdPlayerDlg();
	enum { IDD = IDD_CDPLAYER };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual void PostNcDestroy();
	virtual void OnCancel();
	virtual void OnOK();

	void LayoutHelpBtn();
	void LayoutHelpBtnAndCaption();
	void LayoutChildren(int cx, int cy);
	void ApplyLang();
	void FillDrives();
	void LoadToc();
	void FillTrackList();
	void UpdateMetaUi();
	void PersistUi();
	void RestoreWindowPos();
	void SaveWindowPos();
	void SetStatus(LPCTSTR text);
	void ShowHelpSheet();
	void ApplyVolume();
	void StopPlay(BOOL join);
	void StartPlay(int trackIndex, DWORD startLba);
	void PausePlay();
	void PlayPrev();
	void PlayNext();
	void SeekToRatio(int pos);
	void SeekToPos(int pos);
	void SyncSeekRange();
	void ApplyAbLoop();
	void ClearAbLoop(BOOL announce = FALSE);
	void SetAbAtPlayhead(BOOL isB);
	void RefreshAbButtons();
	void RipTracks(int mode);
	void LookupNet(BOOL useSearch);
	void ShowCandPicker();
	void ApplyCand(int idx);
	BOOL LookupCachePath(TCHAR* path, int cch, LPCTSTR ext) const;
	BOOL TryApplyLookupCache();
	void SaveLookupCache();
	int ParseMbJson(const char* json);
	int ParseCddbRead(const char* text);
	int AddCand(LPCTSTR album, LPCTSTR artist, const char* mbid, LPCTSTR src);
	int ParseItunesJson(const char* json);
	int ParseDeezerJson(const char* json);
	void ParseCdstubJson(const char* json);
	void AbortDiscIo();
	void BurnDisc(int kind);
	void EraseRw();
	void PopupMenu(CPoint screen);
	void RefreshRipQuality();
	int CurrentDriveLetter() const;
	int SelectedTrack() const;
	void CollectSelTracks(int* idx, int* n) const;
	void EndCellEdit(BOOL commit);
	void BeginCellEdit(int row, int col);
	void ApplyCellText(int row, int col, LPCTSTR text);
	void CommitAlbumFields();
	void CopyMetaCells();
	void PasteMetaCells();
	BOOL GetMetaCellRect(int row, int col, CRect& rcDlg);
	HANDLE OpenCdHandle();
	void CloseCdHandle();

	static UINT __stdcall PlayThread(void* p);
	static UINT __stdcall RipThread(void* p);
	static BOOL RipRangeToWav(CCdPlayerDlg* self, DWORD lba0, DWORD lba1, LPCTSTR wavPath,
		int dRate, int dCh, int dBits, BOOL live, int progIndex, int progCount);
	static UINT __stdcall LookupThread(void* p);
	static UINT __stdcall CoverThread(void* p);
	static UINT __stdcall BurnThread(void* p);
	static void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnHelp();
	afx_msg void OnCloseButton();
	afx_msg void OnRefresh();
	afx_msg void OnEject();
	afx_msg void OnLoad();
	afx_msg void OnPlay();
	afx_msg void OnPause();
	afx_msg void OnStop();
	afx_msg void OnPrev();
	afx_msg void OnNext();
	afx_msg void OnAbA();
	afx_msg void OnAbB();
	afx_msg void OnAbClr();
	afx_msg void OnLookup();
	afx_msg void OnSearchGo();
	afx_msg void OnBrowse();
	afx_msg void OnRipSel();
	afx_msg void OnRipAll();
	afx_msg void OnRipOne();
	afx_msg void OnBurnAudio();
	afx_msg void OnBurnData();
	afx_msg void OnErase();
	afx_msg void OnFmtChange();
	afx_msg void OnQualChange();
	afx_msg void OnDriveChange();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnDestroy();
	afx_msg void OnTimer(UINT_PTR id);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnListClick(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnListDblClk(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnAlbumKillFocus();
	afx_msg void OnArtistKillFocus();
	afx_msg void OnCellKillFocus();
	afx_msg BOOL OnDeviceChange(UINT nEventType, DWORD_PTR dwData);
	afx_msg LRESULT OnPosMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnEndedMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnRipProgMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnRipDoneMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnLookupMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnCoverMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnBurnProgMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnBurnDoneMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnLoadTocMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMediaMsg(WPARAM wParam, LPARAM lParam);

public:
	CCustomStandardButton m_help;
	CCustomStandardButton m_close;
	CCustomStandardButton m_refresh;
	CCustomStandardButton m_eject;
	CCustomStandardButton m_load;
	CCustomStandardButton m_play;
	CCustomStandardButton m_pause;
	CCustomStandardButton m_stop;
	CCustomStandardButton m_prev;
	CCustomStandardButton m_next;
	CCustomStandardButton m_abABtn;
	CCustomStandardButton m_abBBtn;
	CCustomStandardButton m_abClrBtn;
	CCustomStandardButton m_lookup;
	CCustomStandardButton m_searchGo;
	CCustomStandardButton m_browse;
	CCustomStandardButton m_ripSel;
	CCustomStandardButton m_ripAll;
	CCustomStandardButton m_ripOne;
	CCustomStandardButton m_burnAudio;
	CCustomStandardButton m_burnData;
	CCustomStandardButton m_erase;
	CCustomComboBox m_drive;
	CCustomComboBox m_fmt;
	CCustomComboBox m_qual;
	CCustomListCtrl m_list;
	CCdCoverCtrl m_cover;
	CCustomEdit m_album;
	CCustomEdit m_artist;
	CCustomEdit m_cellEdit;
	CCustomStatic m_discid;
	CCustomStatic m_time;
	CCustomStatic m_status;
	CCustomStatic m_driveL;
	CCustomStatic m_fmtL;
	CCustomStatic m_qualL;
	CCustomStatic m_folderL;
	CCustomStatic m_volL;
	CCustomRangeSliderCtrl m_seek;
	CCustomSliderCtrl m_vol;
	CCustomCheckBox m_repeat;
	CCustomCheckBox m_shuffle;
	CCustomCheckBox m_addPl;
	CCustomEdit m_search;
	CCustomEdit m_folder;
	CCustomProgressCtrl m_progress;
	CToolTipCtrl m_tooltip;

	enum {
		CD_MAX_TRACK = 99,
		CD_HDR = 80,
		CD_PLAY_BUFS = 12,
		CD_PLAY_SECS = 16,
		CD_TIMER_UI = 1,
		CD_TIMER_DISC = 2,
		CD_TIMER_FOLLOW = 3,
		CD_TIMER_EDIT = 4,
		CD_TIMER_MEDIA = 5
	};

	HANDLE m_hCd;
	CRITICAL_SECTION m_cdCs;
	volatile LONG m_alive;
	volatile LONG m_playStop;
	volatile LONG m_ripStop;
	volatile LONG m_lookupStop;
	volatile LONG m_burnStop;
	HANDLE m_playTh, m_ripTh, m_lookupTh, m_coverTh, m_burnTh;
	HWAVEOUT m_hwo;
	volatile LONG m_playVol;
	HANDLE m_waveEvt;
	int m_trackN;
	int m_firstTrack;
	DWORD m_startLba[CD_MAX_TRACK];
	DWORD m_endLba[CD_MAX_TRACK];
	DWORD m_leadout;
	TCHAR m_title[CD_MAX_TRACK][128];
	TCHAR m_trArtist[CD_MAX_TRACK][128];
	TCHAR m_isrc[CD_MAX_TRACK][16];
	TCHAR m_albumName[256];
	TCHAR m_albumArtist[256];
	char m_discidA[40];
	char m_cddbId[16];
	DWORD m_cddbOff[CD_MAX_TRACK];
	int m_cddbNsec;
	TCHAR m_mbid[48];
	int m_curTrack;
	int m_listPlayTrack;
	DWORD m_playLba;
	DWORD m_playEnd;
	int m_playFrames;
	BOOL m_paused;
	BOOL m_seekDrag;
	int m_seekDragTarget;
	int m_abA, m_abB, m_abTrack;
	DWORD m_loopStartLba, m_loopEndLba;
	int m_lastTimeShown;
	int m_ripMode;
	int m_ripIdx[CD_MAX_TRACK];
	int m_ripN;
	TCHAR m_ripFolder[MAX_PATH];
	int m_ripFmt;
	int m_ripQual;
	int m_burnKind;
	TCHAR m_burnFiles[64][MAX_PATH];
	int m_burnN;
	TCHAR m_searchQ[256];
	TCHAR m_statusBuf[256];
	BYTE* m_coverJpg;
	int m_coverJpgLen;
	int m_parentX, m_parentY;
	TCHAR m_lastTime[64];
	BOOL m_ready;
	BOOL m_tocBusy;
	BOOL m_fillingDrives;
	BOOL m_mediaRetry;
	CWnd* m_candUi;
	enum { CD_MAX_CAND = 16 };
	TCHAR m_candAlbum[CD_MAX_CAND][256];
	TCHAR m_candArtist[CD_MAX_CAND][128];
	TCHAR m_candSrc[CD_MAX_CAND][24];
	char m_candMbid[CD_MAX_CAND][48];
	int m_candTrkN[CD_MAX_CAND];
	TCHAR m_candTrk[CD_MAX_CAND][CD_MAX_TRACK][128];
	int m_candN;
	BOOL m_cellEditing;
	BOOL m_endingEdit;
	int m_editRow, m_editCol;
	int m_cellRow, m_cellCol;
	int m_pendingRow, m_pendingCol;
	int m_clickRow, m_clickCol;
	BOOL m_clickWasSel;
};

void OpenCdPlayerModeless(CWnd* parent);
void CloseCdPlayerIfOpen();
