// CMediaPlayerDlg.h : メディアプレイヤーモード画面(張りぼて)とモード選択ダイアログ
//
// このソフトは元々ファルコム特化型再生ソフトだが、メディアプレイヤーの側面も持つ。
// CMediaPlayerDlg は「張りぼて(ファサード)」であり、実体は COggDlg(og->) と
// CPlayList(pl->) にある。できるかぎりそこの関数を呼び、互換性を優先する。
// メディアプレイヤーモードの間は og / pl のウィンドウを非表示にして裏で生かしておき、
// 表示と簡単な操作の取り次ぎだけをこのクラスで行う。
//
#pragma once

#include "CCustomControl.h"
#include "resource.h"
#include <atlimage.h>

class COggDlg;
class CPlayList;

// メディアプレイヤー → ファルコム特化型 への切替を og 側で遅延実行するためのメッセージ
// (mp の操作ハンドラ内で mp 自身を破棄しないようにするため)
#ifndef WM_MP_ENTER_FALCOM
#define WM_MP_ENTER_FALCOM (WM_APP + 60)
#endif

/////////////////////////////////////////////////////////////////////////////
// CModeSelectDlg : 起動時のモード選択ダイアログ
//   ファルコムbgm特化型画面 / メディアプレイヤー画面 のどちらで起動するか選ぶ。
/////////////////////////////////////////////////////////////////////////////
class CModeSelectDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CModeSelectDlg)
public:
	CModeSelectDlg(CWnd* pParent = NULL);
	virtual ~CModeSelectDlg();
	enum { IDD = IDD_MODESELECT };

	CCustomStandardButton m_btnFalcom;
	CCustomStandardButton m_btnMedia;
	CCustomCheckBox m_ask;
	CBrush m_brDlg;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	afx_msg void OnFalcom();
	afx_msg void OnMedia();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////
// CMediaPlayerDlg : メディアプレイヤーモード画面(ファサード)
/////////////////////////////////////////////////////////////////////////////
class CMediaPlayerDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CMediaPlayerDlg)
public:
	CMediaPlayerDlg(CWnd* pParent = NULL);
	virtual ~CMediaPlayerDlg();
	enum { IDD = IDD_MEDIAPLAYER };

	int  Create(CWnd* pParent);
	void DoLayout();              // DPI/リサイズ対応の手動レイアウト
	void RefreshList(BOOL bForce = FALSE);  // pl->pc をそのまま反映
	void SyncFromMain();         // og/pl の状態をUIへ反映
	void MirrorSeekVol();        // 再生位置(playb追従)/時間/音量を高速ミラー
	void SavePos();              // 座標を savedata に保存
	void EnforceFalcomHidden();  // メディアモード中に裏画面が出ていたら隠す(監視)
	void ReloadPlaylistCombo();  // pl のプレイリスト一覧コンボをミラー
	void SyncSelectionToPlaylist(); // mp リストの選択を pl リストへ反映

	// 表示系コントロール(張りぼて)
	CCustomStatic m_title, m_artist, m_album, m_lrc, m_lrc2, m_lrc3, m_os, m_cpu, m_os3, m_time, m_volval, m_vollabel;
	CCustomRangeSliderCtrl m_seek;   // 再生位置(範囲スライダー: og->m_time と同型)
	CCustomSliderCtrl m_vol;
	CCustomSliderCtrl m_dsvol, m_kvol, m_tempo, m_pitch;   // DS音量/拡張音量/テンポ/ピッチ(og 流用)
	CCustomStatic m_dsvolL, m_kvolL, m_tempoL, m_pitchL;
	CCustomStandardButton m_prev, m_play, m_pause, m_stop, m_next, m_eq, m_piano, m_switch, m_settings, m_exit, m_jacket;
	CCustomStandardButton m_fadeout;   // フェードアウト(og IDC_BUTTON5 流用)
	CCustomStandardButton m_folder;    // フォルダ設定(og IDC_BUTTON9 流用)
	CCustomStandardButton m_plrename, m_pldelete, m_itemdel;
	CButtonST m_lsup, m_up, m_down, m_lsdown;   // 一番上/上/下/一番下(ButtonST 流用)
	CButtonST m_findup, m_finddown;             // あいまい検索 上/下(ButtonST)
	CCustomEdit m_find;                          // あいまい検索キーワード
	CCustomCheckBox m_supe, m_st;                // スペアナ / ST トグル(og 流用)
	CCustomComboBox m_plsel;     // プレイリスト切替/追加(pl->m_listchange のミラー)
	int m_lastComboCount;
	int m_dragging;              // リスト内ドラッグ移動中フラグ
	int m_dragSrc;              // ドラッグ元インデックス
	HIMAGELIST m_hDragImage;     // ドラッグ中の画像
	CCustomCheckBox m_renzoku, m_loop, m_random;
	CCustomCheckBox m_tip, m_mini, m_savemp3, m_saveds;   // ツールチップ/最小化連動/mp3保存/DShow保存
	CCustomGroupBox m_grpInfo, m_grpSnd, m_grpPl;         // 区分け枠(WS_CLIPSIBLINGS+最背面で兄弟を覆わない)
	CCustomListCtrl m_list;
	CImageList il;
	CFont m_fontList, m_fontTitle, m_fontInfo, m_fontChk;
	CBrush m_brDlg;
	HICON m_hIcon;
	int  m_lastCount;
	int  m_lastPlcnt;
	int  m_lastScroll;
	int  m_lastMs2;            // 描画タイマー間隔(savedata.ms2)の変化検出用
	int  m_seekDragging;
	float hD2;     // DPIスケール
	CRect m_bannerRect;   // ビジュアライザ(スペアナ+ジャケ+時間)描画領域
	CDC m_memBanner;      // バナー用永続メモリDC(毎フレームのビットマップ生成を回避)
	CBitmap m_bmpBanner;
	int m_bannerCacheW, m_bannerCacheH;

	void BlitVisualizer(CDC* pDC);   // og のオフスクリーン面を帯に描画

	virtual BOOL DestroyWindow();

protected:
	CToolTipCtrl m_tooltip;

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
#if WIN64
	afx_msg void OnTimer(UINT_PTR nIDEvent);
#else
	afx_msg void OnTimer(UINT nIDEvent);
#endif
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg void OnDestroy();
	afx_msg void OnClose();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnPrev();
	afx_msg void OnPlay();
	afx_msg void OnPauseBtn();
	afx_msg void OnStopBtn();
	afx_msg void OnNext();
	afx_msg void OnEq();
	afx_msg void OnPiano();
	afx_msg void OnFadeout();
	afx_msg void OnFolder();
	afx_msg void OnExit();
	afx_msg void OnJacket();
	afx_msg void OnSettings();
	afx_msg void OnTempoReset();
	afx_msg void OnPitchReset();
	afx_msg void OnSwitch();
	afx_msg void OnRenzoku();
	afx_msg void OnLoop();
	afx_msg void OnRandom();
	afx_msg void OnPlSel();
	afx_msg void OnPlRename();
	afx_msg void OnPlDelete();
	afx_msg void OnMoveTop();
	afx_msg void OnMoveUp();
	afx_msg void OnMoveDown();
	afx_msg void OnMoveBottom();
	afx_msg void OnItemDel();
	afx_msg void OnSupe();
	afx_msg void OnSt();
	afx_msg void OnTip();
	afx_msg void OnMini();
	afx_msg void OnSaveMp3();
	afx_msg void OnSaveDs();
	afx_msg void OnFindUp();
	afx_msg void OnFindDown();
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnRclickList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnBeginDragList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	DECLARE_MESSAGE_MAP()
};

// グローバル(他ファイルから extern で参照)
extern CMediaPlayerDlg* mp;

// モード切替: ファルコム特化型 <-> メディアプレイヤー
void EnterMediaPlayerMode();
void EnterFalcomMode();
