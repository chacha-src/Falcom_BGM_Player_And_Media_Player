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

// TheadLoop から ~30fps で投げる info パネルスクロール tick
// (Timer3 の代替: VSync 同期で滑らかにするため PostMessage 経由)
#ifndef WM_MP_INFO_SCROLL
#define WM_MP_INFO_SCROLL  (WM_APP + 61)
#endif

// ドロップダウン展開直後にリストボックス(hwndList)の高さを再設定する(環境差対策)
#ifndef WM_MP_PLSEL_EXPAND
#define WM_MP_PLSEL_EXPAND  (WM_APP + 62)
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
// 基底は CCustomBlurDialogExBase(CDialogEx派生)。
// CDialog派生の CCustomBlurDialogBase だと非アクティブ時に DWM アクリルが落ちる
// (EQ/簡易ピアノロール等の動作する窓と同じ CDialogEx 系に合わせる)。
class CMediaPlayerDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CMediaPlayerDlg)
public:
	CMediaPlayerDlg(CWnd* pParent = NULL);
	virtual ~CMediaPlayerDlg();
	enum { IDD = IDD_MEDIAPLAYER };

	int  Create(CWnd* pParent);
	void DoLayout();              // DPI/リサイズ対応の手動レイアウト
	void LayoutPlselCombo(int x, int y, int w, int tbH, float s); // m_plsel: 高さは初回のみ設定
	void RefreshListAfterLayout(); // レイアウト変更後に仮想リストの描画範囲を再確定
	void RefreshList(BOOL bForce = FALSE);  // pl->pc をそのまま反映
	void FollowPlayingRow();                // 再生中(♪)の行へカーソル追従(項目挿入後に呼ぶ)
	void NotifyPlayIconChanged();          // SIconTimer 直後に♪点滅を即反映(250ms待ちしない)
	void InitListScrollPosition();          // 起動/表示確定時にリスト位置を復元
	void SyncFromMain();         // og/pl の状態をUIへ反映
	void ApplyPauseButtonLabel(); // m_mpBtnShort と ps に応じた一時停止/再開ラベル
	void MirrorSeekVol();        // 再生位置(playb追従)/時間/音量を高速ミラー
	void SavePos();              // 座標を savedata に保存
	void EnforceFalcomHidden();  // メディアモード中に裏画面が出ていたら隠す(監視)
	void ReloadPlaylistCombo();  // pl のプレイリスト一覧コンボをミラー
	void SyncSelectionToPlaylist(); // mp リストの選択を pl リストへ反映
	void FitPlaylistLastColumn(int dragCol = -1, int dragWidth = -1); // 最終列をリスト右端へフィット(余白防止)
	void SyncPushToggleButtons();   // スペアナ/ST/EQ/ピアノの押下見た目
	// 選択行の並べ替え(mode: 0=一番上 / 1=上へ / 2=下へ / 3=一番下)。
	// pl->pc を直接並べ替え、再生インデックス(plcnt/pnt/pnt1)と選択を追従させる。
	void MoveSelected(int mode);
	BOOL RelayPreTranslateMessage(MSG* pMsg);  // og::DoModal 中の Enter 等を中継
	void RequestAppShutdown();

	// ---- 情報表示スタティック(バナー GDI に隠れているものは SW_HIDE してある) ----
	CCustomStatic m_title, m_artist, m_album, m_lrc, m_lrc2, m_lrc3, m_lrc4, m_lrc5, m_os, m_cpu, m_os3, m_time, m_volval, m_vollabel;

	// シークバー: og->m_time と同じ CCustomRangeSliderCtrl(ループ範囲表示付き)
	CCustomRangeSliderCtrl m_seek;
	CCustomSliderCtrl m_vol;
	// og 側の各スライダーを DoLayout で同位置にミラー配置(og は非表示のまま)
	CCustomSliderCtrl m_dsvol, m_kvol, m_tempo, m_pitch;
	CCustomStatic m_dsvolL, m_kvolL, m_tempoL, m_pitchL;

	// ---- 操作ボタン ----
	CCustomStandardButton m_prev, m_play, m_pause, m_stop, m_next, m_eq, m_piano, m_analyzer, m_switch, m_settings, m_exit, m_jacket;
	CCustomStandardButton m_fadeout;   // フェードアウト(og IDC_BUTTON5 と同じ処理を委譲)
	CCustomStandardButton m_folder;    // フォルダ設定(og IDC_BUTTON9 と同じ処理を委譲)
	CCustomStandardButton m_plrename, m_pldelete, m_itemdel;
	CCustomStandardButton m_m3uExport, m_m3uImport;
	CCustomStandardButton m_supe, m_st;
	CButtonST m_lsup, m_up, m_down, m_lsdown;   // プレイリスト行移動(一番上/上/下/一番下)
	CButtonST m_findup, m_finddown;              // あいまい検索 上/下
	CCustomEdit m_find;

	// ---- 状態チェックボックス(og/pl の状態を SyncFromMain でミラー) ----
	CCustomComboBox m_plsel;     // プレイリスト切替/追加(pl->m_listchange のミラー)
	int m_lastComboCount;        // コンボ項目数の変化検出用
	int m_plselDropExtent;       // DROPDOWNLIST のドロップダウン高さ(初回のみ MoveWindow で設定)
	float m_plselLayoutDpi;      // 上記を設定したときの hD2(DPI 変化時に再初期化)

	// ---- プレイリストのドラッグ&ドロップ移動 ----
	int m_dragging;              // ドラッグ操作中(0/1)
	int m_dragSrc;               // ドラッグ元の行インデックス
	HIMAGELIST m_hDragImage;     // DragMove に使うゴースト画像

	CCustomCheckBox m_renzoku, m_loop, m_random;
	CCustomCheckBox m_tip, m_mini, m_savemp3, m_saveds, m_savewav;
	CCustomStatic m_kaisuuL;
	CCustomEdit m_kaisuu;
	// グループ枠は WS_CLIPSIBLINGS + 最背面で、内側コントロールを塗り潰さない
	CCustomGroupBox m_grpInfo, m_grpSnd, m_grpPl;
	CCustomListCtrl m_list;
	CImageList il;
	CFont m_fontList, m_fontTitle, m_fontInfo, m_fontChk;
	CBrush m_brDlg;
	HICON m_hIcon;

	// ---- リスト同期の変化検出 ----
	int  m_lastCount;    // 前回描画時の pl->playcnt(差分更新用)
	int  m_lastPlcnt;
	int  m_lastScroll;   // 前回 EnsureVisible した再生行(同一なら再スクロール不要)
	int  m_lastMs2;      // savedata.ms2 の変化検出用(タイマー間隔の変更を反映するため)
	int  m_seekDragging; // ユーザーがシークをドラッグ中なら 1(ミラー更新をスキップ)
	int  m_lastPlayIcon; // 再生行(♪)の前回アイコン値。変化時のみ再描画して点滅をなめらかに
	int  m_savedEqVisible;       // 最小化連動: 最小化前にイコライザーが表示されていたか
	int  m_savedPianoVisible;    // 最小化連動: 最小化前に簡易ピアノロールが表示されていたか
	int  m_savedAnalyzerVisible; // 最小化連動: 最小化前にアナライザーが表示されていたか
	bool m_inSizeMove;        // ユーザーが枠をドラッグしてリサイズ中(重い同期再描画を抑制)
	bool m_uiReady;           // OnInitDialog 完了前の WM_SIZE では GetCheck/DoLayout しない
	float hD2;           // DPI スケール係数(96dpi = 1.0)

	// ---- バナー領域(ビジュアライザ Blit 先) ----
	// DoLayout でアスペクト比維持のまま計算された矩形。OnPaint で BlitVisualizer が使用。
	CRect m_bannerRect;
	// 幅拡張時に左余白へ分離するジャケット(ミニ)領域。IsRectEmpty なら非表示。
	CRect m_jacketRect;
	// 幅拡張時に右余白へ展開する曲情報パネル領域。IsRectEmpty なら非表示。
	CRect m_infoPanelRect;
	// サイドパネルの再描画判定キー(タイトル/アーティスト/アルバム/ジャケ世代を結合した文字列)
	CString m_lastBannerKey;
	// 非アクリル時のバナー Blit 用永続メモリ DC(サイズ変化時のみ再確保)
	CDC m_memBanner;
	CBitmap m_bmpBanner;
	int m_bannerCacheW, m_bannerCacheH;

	// ---- 右曲情報パネルのテキスト marquee スクロール ----
	// WM_MP_INFO_SCROLL(TheadLoop ~30fps)で進行。収まる行は静止したまま。
	// [0]=タイトル, [1]=アーティスト, [2]=アルバム, [3]=曲番号, [4]=オーディオ情報, [5]=フォーマット
	static const int kInfoRows = 6;
	int  m_isc[kInfoRows];   // 各行の現在スクロールオフセット(px)。0=静止
	int  m_iscW[kInfoRows];  // テキスト+セパレータの全幅(0=行が収まるため不要)
	bool m_iscActive;        // 少なくとも1行がスクロール中。次の WM_MP_INFO_SCROLL 発行を判定
	int  m_lastInfoPanelW;   // 曲情報パネル幅(リサイズ検出→marquee リセット)
	int  m_listHdrDragCol;   // 列幅ドラッグ中の列(-1=なし)。HDN_TRACK 追随用
	int  m_lastToggleSupe;   // 押下トグル見た目の変化検出用(-1=未同期)
	int  m_lastToggleSt;
	int  m_lastToggleEq;
	int  m_lastTogglePiano;
	int  m_lastToggleAnalyzer;
	int  m_dsvolSlW;         // DS音量スライダー幅(DoLayout)。ラベル省略判定に使用
	int  m_mpBtnShort;       // 0=フル 1=EQ系短縮 2=フェード/JK等短縮 3=最小幅用の超短縮

	// og のオフスクリーン合成 DC(スペアナ+ジャケ+時間)を m_bannerRect へ StretchBlit する。
	// アクリル(Win11)時は黒透過合成、非アクリル時は永続メモリ DC でキャッシュ Blit。
	void BlitVisualizer(CDC* pDC);
	// 左ジャケット・右曲情報パネルをオフスクリーンバッファで GDI 描画して Blit する。
	// クリップ矩形がサイドパネルと重ならない限り重い処理は走らない(バナーの毎フレーム無効化と共存)。
	void DrawSidePanels(CDC* pDC);
	void InvalidateSidePanels();     // 曲変更・リサイズ時にサイドパネルの WM_PAINT を要求
	void ResetInfoScroll();          // 曲変更・リサイズ時に marquee オフセットを全行リセット
	// m_tip チェックの ON/OFF を m_list のカスタムツールチップ(CListCtrlA)に反映する。
	// LVS_EX_INFOTIP はカスタム実装と競合するため、ON 時は除去・OFF 時は付与する。
	void ApplyListTooltipState();
	int  GetListScrollAnchor() const;
	void RestoreListScrollAnchor(int anchor);
	// og の timerp で使う sss 決定ロジックと同じ規則でタイトルを解決する。
	// mode 値により tagfile / stitle / fnn のどれを使うかが変わる。
	CString CurrentTrackTitle() const;
	// 1行分のテキストを mem DC へ描画する。収まれば静止描画(false)、はみ出せば
	// 2コピーのワイド DC を作り marquee オフセットで切り出して BitBlt する(true)。
	bool DrawInfoScrollRow(CDC& mem, int tx, int y, int tw, int lineH,
		const CString& text, COLORREF clr, int rowIdx, COLORREF kBg, CFont* font);

	virtual BOOL DestroyWindow();

protected:
	CToolTipCtrl m_tooltip;

	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnEnterSizeMove();
	afx_msg void OnExitSizeMove();
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
	afx_msg void OnAnalyzer();
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
	afx_msg void OnPlselDropdown();
	afx_msg void OnPlRename();
	afx_msg void OnPlDelete();
	afx_msg void OnMoveTop();
	afx_msg void OnMoveUp();
	afx_msg void OnMoveDown();
	afx_msg void OnMoveBottom();
	afx_msg void OnItemDel();
	afx_msg void OnM3uExport();
	afx_msg void OnM3uImport();
	afx_msg void OnSupe();
	afx_msg void OnSt();
	afx_msg void OnTip();
	afx_msg void OnMini();
	afx_msg void OnSaveMp3();
	afx_msg void OnSaveDs();
	afx_msg void OnSaveWav();
	afx_msg void OnKaisuuKillFocus();
	afx_msg void OnFindUp();
	afx_msg void OnFindDown();
	afx_msg void OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnRclickList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnKeydownList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnBeginDragList(NMHDR* pNMHDR, LRESULT* pResult);
	void OnPlaylistHeaderNotify(NMHDR* pNMHDR, LRESULT* pResult);
	void TickListHdrDragFit(); // 列ドラッグ中: ヘッダー幅を読んで最終列を追随
	static LRESULT CALLBACK ListHeaderNotifySubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
	afx_msg void OnListHeaderEndTrack(NMHDR* pNMHDR, LRESULT* pResult);
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg LRESULT OnInfoScrollTick(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPlselExpandPopup(WPARAM wParam, LPARAM lParam);
	afx_msg BOOL OnNcActivate(BOOL bActive);
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	DECLARE_MESSAGE_MAP()
};

// グローバル(他ファイルから extern で参照)
extern CMediaPlayerDlg* mp;

// モード切替: ファルコム特化型 <-> メディアプレイヤー
void EnterMediaPlayerMode();
void EnterFalcomMode();
