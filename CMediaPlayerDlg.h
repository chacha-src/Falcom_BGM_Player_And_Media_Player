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
#include "CLyricsViewWnd.h"
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

// 欠損フラグ走査スレッド完了(UI を止めない PathFileExists)
#ifndef WM_MP_MISS_DONE
#define WM_MP_MISS_DONE (WM_APP + 63)
#endif

// ジャケット抽出スレッド完了(LoadJacket を UI から外す)
#ifndef WM_MP_JAK_DONE
#define WM_MP_JAK_DONE (WM_APP + 65)
#endif

// シーク波形オーバービュー構築完了
#ifndef WM_MP_WAVE_DONE
#define WM_MP_WAVE_DONE (WM_APP + 66)
#endif

// ライブラリツリー遅延構築(起動をブロックしない)
#ifndef WM_MP_LIB_BUILD
#define WM_MP_LIB_BUILD (WM_APP + 64)
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
	void HistRebuildList();                 // 再生履歴リストを再構築(og からも呼ぶ)
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
	CLyricsViewWnd m_lrcView; // 拡大時のカラオケ風歌詞ビュー

	// シークバー: og->m_time と同じ CCustomRangeSliderCtrl(ループ範囲表示付き)
	CCustomRangeSliderCtrl m_seek;
	CCustomSliderCtrl m_vol;
	// og 側の各スライダーを DoLayout で同位置にミラー配置(og は非表示のまま)
	CCustomSliderCtrl m_dsvol, m_kvol, m_tempo, m_pitch;
	CCustomStatic m_dsvolL, m_kvolL, m_tempoL, m_pitchL;

	// ---- 操作ボタン ----
	CCustomStandardButton m_prev, m_play, m_pause, m_stop, m_next, m_eq, m_piano, m_analyzer, m_pro, m_switch, m_settings, m_exit, m_jacket;
	CCustomStandardButton m_fadeout;   // フェードアウト(og IDC_BUTTON5 と同じ処理を委譲)
	CCustomStandardButton m_folder;    // フォルダ設定(og IDC_BUTTON9 と同じ処理を委譲)
	CCustomStandardButton m_plrename, m_pldelete, m_itemdel;
	CCustomStandardButton m_m3uExport, m_m3uImport;
	CCustomStandardButton m_supe, m_st, m_prompt, m_cmdroll;
	CCustomStandardButton m_abA, m_abB, m_abClr; // A-Bリピート
	CCustomCheckBox m_seekLock; // シーク左: loop1/2つまみロック(既定ON)
	CCustomStandardButton m_lrcExpand; // 歌詞パネル拡大/縮小
	CCustomStandardButton m_toolsToggle; // ツールメニュー（並べ替えパネル含む）
	CCustomStandardButton m_cheatBtn;    // 操作ガイド(?)
	CCustomStandardButton m_sortName, m_sortArt, m_sortAlb, m_sortTime;
	CCustomStandardButton m_addFolder; // フォルダから追加(ライブラリ)
	CCustomCheckBox m_findFilter;      // 検索=絞り込み
	CCustomStatic m_lrcBadge;          // LRC状態バッジ
	CCustomStatic m_plRailBg;          // Lib/Hist 左レールの不透明ピンク下地（白抜け防止）
	// ライブラリ左ドロワー(フォルダツリー＋アルバム一覧)
	CCustomStandardButton m_libToggle;
	CCustomStandardButton m_libAddRoot, m_libAddPl;
	CCustomTreeCtrl m_libTree;
	CCustomListCtrl m_libAlbums;
	CCustomStandardButton m_histToggle;
	CCustomListCtrl m_histList;
	CCustomStandardButton m_tempToggle;
	CCustomStandardButton m_tempClear;
	CCustomStatic m_tempHint;
	CCustomStandardButton m_emptyFolder, m_emptyM3u;
	int m_histBuilt;
	enum { kLibPathMax = 512, kLibAlbumMax = 256 };
	CString m_libPathBag[kLibPathMax];
	int m_libPathN;
	CString m_albumPathBag[kLibAlbumMax];
	int m_albumN;
	BYTE m_albumIsFile[kLibAlbumMax]; // 1=file 0=folder
	CString m_libSelFolder;
	int m_libTreeBuilt;
	int m_libBuildPosted; // 1=WM_MP_LIB_BUILD 投稿済み
	CButtonST m_lsup, m_up, m_down, m_lsdown;   // プレイリスト行移動(一番上/上/下/一番下)
	CButtonST m_findup, m_finddown;              // あいまい検索 上/下
	CCustomEdit m_find;

	// ---- 状態チェックボックス(og/pl の状態を SyncFromMain でミラー) ----
	CCustomComboBox m_plsel;     // プレイリスト切替/追加(pl->m_listchange のミラー)
	int m_lastComboCount;        // コンボ項目数の変化検出用
	int m_plselDropExtent;       // DROPDOWNLIST のドロップダウン高さ(初回のみ MoveWindow で設定)
	float m_plselLayoutDpi;      // 上記を設定したときの hD2(DPI 変化時に再初期化)

	// ---- プレイリストのドラッグ&ドロップ移動 ----
	int m_dragging;              // ドラッグ操作中(0/1) プレイリスト行
	int m_dragSrc;               // ドラッグ元の行インデックス
	HIMAGELIST m_hDragImage;     // DragMove に使うゴースト画像
	int m_libDrag;               // ライブラリ→PL ドラッグ中
	HIMAGELIST m_hLibDragImage;  // Lib→PL ドラッグゴースト
	CString m_libDragFolder;     // ドロップ中のパス(フォルダ or ファイル)

	CCustomCheckBox m_renzoku, m_loop, m_random;
	CCustomCheckBox m_tip, m_mini, m_savemp3, m_saveds, m_savewav;
	CCustomCheckBox m_micmix;
	CCustomSliderCtrl m_miclev;
	CCustomStatic m_miclevL;
	CCustomLevelMeter m_micMeter;
	CCustomCheckBox m_saveparam;   // 曲ごとオーディオ/DSP パラメータ保存
	CCustomStandardButton m_resetdata; // 保存ファイル削除でリセット
	CCustomStandardButton m_record;    // デバイス録音 UI
	CCustomStandardButton m_capture;   // 画面キャプチャ UI
	CCustomStatic m_kaisuuL;
	CCustomEdit m_kaisuu;
	// グループ枠は WS_CLIPSIBLINGS + 最背面で、内側コントロールを塗り潰さない
	CCustomGroupBox m_grpInfo, m_grpSnd, m_grpPl;
	CCustomListCtrl m_list;
	CImageList il;
	CFont m_fontList, m_fontTitle, m_fontInfo, m_fontTech, m_fontChk;
	CBrush m_brDlg;
	HICON m_hIcon;

	// ---- リスト同期の変化検出 ----
	int  m_lastCount;    // 前回描画時の pl->playcnt(差分更新用)
	int  m_lastPlcnt;
	int  m_lastScroll;   // 前回 EnsureVisible した再生行(同一なら再スクロール不要)
	int  m_lastFollowPnt; // FollowPlayingRow: 前回追従した pl->pnt(曲変化時だけ追従)
	int  m_lastMs2;      // savedata.ms2 の変化検出用(タイマー間隔の変更を反映するため)
	int  m_seekDragging; // ユーザーがシークをドラッグ中なら 1(ミラー更新をスキップ)
	int  m_seekHoldPos;  // シーク確定位置。timerp の一瞬古い値で棒が戻るのを抑止
	ULONGLONG m_seekHoldUntil; // 0=なし。GetTickCount64 期限まで HoldPos を優先
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
	// ツール▾＋Lib/Hist 左レール。アクリル時のグループ隙間白抜けを不透明ピンクで塞ぐ。
	CRect m_plRailRect;
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
	volatile LONG m_iscScrollPosted; // WM_MP_INFO_SCROLL 多重ポスト防止
	int  m_lastInfoPanelW;   // 曲情報パネル幅(リサイズ検出→marquee リセット)
	// marquee 行のワイドビットマップキャッシュ（毎フレーム CreateCompatible しない）
	CDC      m_iscRowDC[kInfoRows];
	CBitmap  m_iscRowBmp[kInfoRows];
	CBitmap* m_iscRowOldBmp[kInfoRows];
	int      m_iscRowCacheW[kInfoRows];
	int      m_iscRowCacheH[kInfoRows];
	CString  m_iscRowCacheText[kInfoRows];
	COLORREF m_iscRowCacheClr[kInfoRows];
	COLORREF m_iscRowCacheBg[kInfoRows];
	// 右曲情報パネル用オフスクリーン（毎フレーム CreateCompatible しない）
	CDC      m_infoMemDC;
	CBitmap  m_infoMemBmp;
	CBitmap* m_infoMemOldBmp = nullptr;
	int      m_infoMemW = 0;
	int      m_infoMemH = 0;
	int  m_listHdrDragCol;   // 列幅ドラッグ中の列(-1=なし)。HDN_TRACK 追随用
	int  m_lastToggleSupe;   // 押下トグル見た目の変化検出用(-1=未同期)
	int  m_lastToggleSt;
	int  m_lastToggleEq;
	int  m_lastTogglePiano;
	int  m_lastToggleAnalyzer;
	int  m_lastTogglePrompt;
	int  m_lastToggleCmdRoll;
	int  m_dsvolSlW;         // DS音量スライダー幅(DoLayout)。ラベル省略判定に使用
	int  m_mpBtnShort;       // 0=フル 1=EQ系短縮 2=フェード/JK等短縮 3=最小幅用の超短縮
	int  m_mpPromptShort;    // 0=プロンプト 1=指示(幅不足時)
	int  m_mpCmdRollShort;   // 0=ロール 1=短縮
	int  m_mpChkShort[6];    // 下部チェック tip..saveparam: 0=フル 1=中 2=短 (-1=未設定)

	// ---- A-Bリピート(PCMフレーム。両方>=0で有効) ----
	int  m_abApos;           // -1=未設定
	int  m_abBpos;
	int  m_abWrapBusy;       // シーク往復の再入防止
	int  m_abLoopCount;      // A-B 周回回数(表示用)

	// ---- シーク波形オーバービュー(ProAudio_BuildWaveOverview 非同期) ----
	float m_wavePeaks[1024];
	int   m_wavePeakN;
	TCHAR m_wavePath[1024];
	volatile LONG m_waveGen;
	volatile LONG m_waveBusy;
	int m_jacketRemBucket; // ジャケット残時間リング Invalidate 用(前回%)
	void KickWaveOverview();
	void ClearWaveOverview();
	void RefreshSeekCues();
	void JumpToCueIndex(int idx);
	void ApplyPracticeTempoPercent(int pct); // 50/75/100 → スライダー
	void SetPhraseAbAroundNow();             // 現在±mpPhraseSec を A-B に
	BOOL TryPlayFromQueue();                 // キュー先頭を再生してシフト。空なら FALSE
	void QueueAdd(int pcIdx, BOOL playNext);
	int  QueueCount() const;
	int  QueueAt(int i) const;
	void QueueMove(int from, int to);
	void QueueRemoveAt(int i);
	void QueueClear();
	void UpdateQueueChrome();                // ツール▾は記号のみ。状態はツールチップ
	void ShowToolsExtrasMenu(CPoint screenPt);
	void ShowLyricsExtrasMenu(CPoint screenPt);
	void ApplySleepTimer(int minutes);       // 0=Off
	void OpenTagEditForSelection();
	void CycleRatingForDisp(int disp);
	void ShiftLrcMs(int deltaMs);
	void DrawBannerMeters(CDC* pDC, int bannerW, int bannerH);
	void DrawJacketHeroOverlay(CDC& mem, int w, int h);

	// ---- 検索フィルタ(表示行→pc インデックス)。std 禁止のため素の配列 ----
	int* m_fmap;             // malloc。件数 m_fmapCap
	int  m_fmapCap;
	int  m_fcnt;             // フィルタ後件数(フィルタOFF時は playcnt と同値扱いしない: m_filtOn で判定)
	int  m_filtOn;           // 1=絞り込み中
	int  m_smartFilt;        // 0=なし 1=未再生 2=欠損のみ（クイック）
	int  m_activeSmartId;    // -1=なし / >=0 = MpSmart ルール index

	// ---- Up Next キュー(pc インデックス) ----
	int  m_queue[64];
	int  m_queueN;
	ULONGLONG m_sleepEndTick; // 0=オフ。GetTickCount64 期限

	// ---- 欠損フラグ(pc インデックス、1=欠損)。ワーカスレッド走査 ----
	char* m_miss;            // malloc(表示用)
	int   m_missCap;
	int   m_missScan;        // 互換: 走査完了位置(>=playcnt で完了)
	volatile LONG m_missGen; // 世代。PL変更で上げ、古いスレッド結果を破棄
	volatile LONG m_missBusy;// 1=走査スレッド稼働中
	void KickMissScan();     // playcnt 変化時に非同期走査を起動
	void StopMissScan();     // 破棄前に世代を上げて結果を無視
	void UpdateMissChrome(); // UpdateQueueChrome 経由で Tip 更新
	int  CountMissing() const;
	volatile LONG m_jakGen;  // ジャケット抽出世代
	volatile LONG m_jakBusy; // 1=ジャケット抽出スレッド稼働中
	TCHAR m_jakPend[1024];   // 抽出中パス
	int m_jakPrefetch;       // 非表示行の順次ディスクキャッシュ位置(disp)

	// ---- ジャケットサムネキャッシュ(LRU・固定スロット) ----
	enum { kMpJakN = 64, kMpJakPx = 24 };
	HBITMAP m_jakBmp[kMpJakN];
	TCHAR   m_jakKey[kMpJakN][1024];
	DWORD   m_jakTick[kMpJakN];
	int     m_jakRow[kMpJakN]; // 最後に紐付けた pc 行(-1=なし)

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
	int  GetSelectedPcIndex() const;
	void RestoreListScrollAnchor(int anchor);
	// og の timerp で使う sss 決定ロジックと同じ規則でタイトルを解決する。
	// mode 値により tagfile / stitle / fnn のどれを使うかが変わる。
	CString CurrentTrackTitle() const;
	// 1行分のテキストを mem DC へ描画する。収まれば静止描画(false)、はみ出せば
	// 行キャッシュのワイド DC から marquee オフセットで BitBlt する(true)。
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
	afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
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
	afx_msg void OnProTools();
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
	afx_msg void OnPrompt();
	afx_msg void OnCmdRoll();
	afx_msg void OnTip();
	afx_msg void OnMini();
	afx_msg void OnSaveMp3();
	afx_msg void OnSaveDs();
	afx_msg void OnSaveWav();
	afx_msg void OnMicMix();
	afx_msg void OnMicMixMenuToggle();
	afx_msg void OnMicLevRelease(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnRecord();
	afx_msg void OnCapture();
	afx_msg void OnSaveParam();
	afx_msg void OnResetData();
	afx_msg void OnKaisuuKillFocus();
	afx_msg void OnFindUp();
	afx_msg void OnFindDown();
	afx_msg void OnAbSetA();
	afx_msg void OnAbSetB();
	afx_msg void OnAbClear();
	afx_msg void OnSeekLock();
	afx_msg void OnSeekWaveToggle();
	afx_msg void OnSeekCueAdd();
	afx_msg void OnSeekCueClear();
	afx_msg void OnSeekCueJump1();
	afx_msg void OnSeekCueJump2();
	afx_msg void OnSeekCueJump3();
	afx_msg void OnSeekCueJump4();
	afx_msg void OnSeekCueJump5();
	afx_msg void OnSeekCueJump6();
	afx_msg void OnSeekCueJump7();
	afx_msg void OnSeekCueJump8();
	afx_msg void OnPracticeTempo50();
	afx_msg void OnPracticeTempo75();
	afx_msg void OnPracticeTempo100();
	afx_msg void OnPhraseAbNow();
	afx_msg void OnFiltUnplayed();
	afx_msg void OnFiltMissing();
	afx_msg void OnFiltClear();
	afx_msg void OnMissManage();
	afx_msg void OnSmartEdit();
	afx_msg void OnSmartApplyId(UINT nID);
	afx_msg void OnQueueShow();
	afx_msg void OnQueueAdd();
	afx_msg void OnQueuePlayNext();
	afx_msg void OnQueueClear();
	afx_msg void OnDupesScan();
	afx_msg void OnFolderSyncDiff();
	afx_msg void OnLrcPlus50();
	afx_msg void OnLrcMinus50();
	afx_msg void OnLrcPlus10();
	afx_msg void OnLrcMinus10();
	afx_msg void OnLrcPlus100();
	afx_msg void OnLrcMinus100();
	afx_msg void OnLrcSave();
	afx_msg void OnDeskLrcToggle();
	afx_msg void OnTagEdit();
	afx_msg void OnJacketReloadAlt();
	afx_msg void OnJacketPickCover();
	afx_msg void OnJacketSaveCover();
	afx_msg void OnExportAb();
	afx_msg void OnExportAbNow();
	afx_msg void OnAbPackExport();
	afx_msg void OnNormBatch();
	afx_msg void OnMbAutotag();
	afx_msg void OnNormScan();
	afx_msg void OnNormLufs14();
	afx_msg void OnNormLufs16();
	afx_msg void OnNormLufs18();
	afx_msg void OnNormPreview();
	afx_msg void OnAbSnapA();
	afx_msg void OnAbSnapB();
	afx_msg void OnAbApplyA();
	afx_msg void OnAbApplyB();
	afx_msg void OnAbSnapToggle();
	afx_msg void OnSleep15();
	afx_msg void OnSleep30();
	afx_msg void OnSleep60();
	afx_msg void OnSleepOff();
	afx_msg void OnSleepCustom();
	afx_msg void OnXfadePreviewToggle();
	afx_msg void OnBeatGridToggle();
	afx_msg void OnJacketRemOverlayToggle();
	afx_msg void OnMpBpmDetect();
	afx_msg void OnMpBpmCand1();
	afx_msg void OnMpBpmCand2();
	afx_msg void OnMpBpmCand3();
	afx_msg void OnMpDjPad();
	afx_msg void OnMpAlarm();
	afx_msg void OnMpMirror();
	afx_msg void OnMpRemote();
	afx_msg void OnMpRemoteDlg();
	afx_msg void OnMpRemoteBrowser();
	afx_msg void OnMpSsViz();
	afx_msg void OnMpVideoExtract();
	afx_msg void OnMpVideoReplace();
	afx_msg void OnMpGamePreset();
	afx_msg void OnMpGcpRange(UINT nID);
	afx_msg void OnMpMidiIn();
	afx_msg LRESULT OnMpTransportCmd(WPARAM wParam, LPARAM lParam);
	afx_msg void OnLrcExpand();
	afx_msg void OnToolsToggle();       // ツールメニューを開く
	afx_msg void OnToolsPanelToggle();  // 並べ替えパネル開閉
	afx_msg void OnCheatSheetBtn();
	afx_msg void OnSortName();
	afx_msg void OnSortArt();
	afx_msg void OnSortAlb();
	afx_msg void OnSortTime();
	afx_msg void OnAddFolder();
	afx_msg void OnFindFilter();
	afx_msg void OnLibToggle();
	afx_msg void OnHistToggle();
	afx_msg void OnTempToggle();
	afx_msg void OnTempClear();
	afx_msg void OnLibAddRoot();
	afx_msg void OnLibAddPl();
	afx_msg void OnLibTreeSel(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLibTreeExpanding(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLibTreeBeginDrag(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLibAlbumDblClk(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnLibAlbumBeginDrag(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnHistDblClk(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnEmptyAddFolder();
	afx_msg void OnEmptyM3u();
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnSpeanaStyleBar();
	afx_msg void OnSpeanaStyleMirror();
	afx_msg void OnSpeanaStyleWave();
	afx_msg void OnCorrMeterToggle();
	afx_msg void OnRefreshJacket();
	void LibStartFolderDrag(LPCTSTR path, CPoint ptClient);
	void LibAddPath(LPCTSTR path, BOOL playAfter);
	BOOL LibDropHitTestPlaylist(CPoint ptClient) const;
	void EnsureLibControls();
	void HistPlayIndex(int histIdx);
	void ShowCheatSheet();
	void UpdateEmptyStateUi();
	CString MpTechFormatLine() const;
	void LibRebuildTree();
	void LibFitNoHScroll(CWnd* pList);
	void LibFillChildren(HTREEITEM hParent);
	void LibFillAlbums(LPCTSTR folder);
	int  LibAllocPath(LPCTSTR path);
	CString LibItemPath(HTREEITEM h) const;
	CString LibRootsFilePath() const;
	void LibLoadUserRoots(CString* outs, int maxN, int& outN);
	void LibSaveUserRoots(const CString* roots, int n);
	void LibAddToPlaylist(LPCTSTR folder);
	afx_msg void OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnClickList(NMHDR* pNMHDR, LRESULT* pResult);
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
	afx_msg LRESULT OnMissScanDone(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnJakLoadDone(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWaveOverviewDone(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnLibBuildLazy(WPARAM wParam, LPARAM lParam);
	afx_msg BOOL OnNcActivate(BOOL bActive);
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	DECLARE_MESSAGE_MAP()
};

// グローバル(他ファイルから extern で参照)
extern CMediaPlayerDlg* mp;

// モード切替: ファルコム特化型 <-> メディアプレイヤー
void EnterMediaPlayerMode();
void EnterFalcomMode();
