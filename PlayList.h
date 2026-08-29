#pragma once
#include "afxwin.h"
#include "afxcmn.h"
// CPlayList ダイアログ

struct playlistdata{
	TCHAR name[1024];
	TCHAR art[1024];
	TCHAR alb[1024];
	TCHAR fol[1024];
	int sub;
	int loop1;
	int loop2;
	int ret2;
	int time;
	int res2;
};

#include "ListCtrlA.h"
#include "CCustomControl.h"
#include <vector>

class CPlayList : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CPlayList)

public:
	CPlayList(CWnd* pParent = NULL);   // 標準コンストラクタ
	virtual ~CPlayList();
	CWnd* m_pParent;
// ダイアログ データ
	enum { IDD = IDD_PLAYLIST };
	CImageList il;
	playlistdata0 *pc;
	void OnList();
	int nnn;
	int pnt,pnt1;
	int playcnt;
	int m_tempMode; // 1=一時PL(Saveしない)。savedata.mpTempOpen と同期して使う

	void SIcon(int i);
	void SIconTimer(int i);
	int Add(CString name,int sub,int loop1,int loop2,CString art,CString alb,CString fol,int ret,int time,BOOL f=TRUE,BOOL ff=TRUE);
	void Del();
	void UndoLastDelete(); // 直近の編集を戻す(削除/貼り付け。Ctrl+Z)
	void RedoLastEdit();   // Ctrl+Y / Ctrl+Shift+Z
	BOOL CopySelectionToClipboard();
	void PasteFromClipboard();
	void SelectAllTracks(); // Ctrl+A
	BOOL HandleListEditKeys(MSG* pMsg); // リストフォーカス時の Ctrl+A/C/X/V/Z/Y
	// プレイリスト行をインデックス配列で削除(降順ソートして安全に後ろから)。Save あり。
	void DelByIndices(const std::vector<int>& indices);
	void Load(BOOL restoreSavedRow = TRUE);
	void Save();
	int chk(CString name,int sub,CString art,CString fol,int ret);
	int FindByPath(LPCTSTR fol); // 絶対パス一致(大文字小文字・正規化)。-1=なし
	void Fol(CString fname);
	// 単一ファイルをプレイリストへ追加(既存ならスキップ)。Fol の単一パス経路を利用。
	void AddFilePath(LPCTSTR path);
	void plug(CString ff,KMPMODULE *mod);
	void plugs(CString ext1,playlistdata *p,TCHAR* kpi, BYTE& kv);
	void FixMidiMode(playlistdata0& item); // MIDI は midPlayPrefer に合わせて -30/-3。動画(-2)の焼き付きも引き直す
	void RefreshMidiPlayModes(); // 全 MIDI 行を Fix。再生中エンジンは切らない（次曲から）
	void plugswinamp(CString ext1, playlistdata *p, TCHAR* kpi, BYTE& kv);
	void plugsxmplay(CString ext1, playlistdata *p, TCHAR* kpi, BYTE& kv);
	void plugsaimp(CString ext1, playlistdata *p, TCHAR* kpi, BYTE& kv);
	void Get(int i);
	void RestoreSavedPlaybackRow(); // Load 後: 保存 pnt(♪行)から filen/plcnt を復元

	void OnDrag(int x,int y);
	void OnEndDrag();
	void OnXCHG(int i,int j);
	int m_lDragTopItem = 0;
	int m_lDragTopItemt = 0;
	HIMAGELIST  m_hDragImage = 0;
	BOOL w_flg;
	CString GetModulePath();
	void loadplaylistname();

	int GetPlaylistFileCount();
	CString GetPlaylistDisplayName(int idx);
	int ShowTrackContextMenu(CPoint pt, CWnd* pOwner);
	void HandleTrackContextCmd(int cmd);
	void TransferSelectedToPlaylist(int targetIdx, bool moveNotCopy);
	void RemoveMissingFiles();
	// パス更新。存在するなら TRUE。未存在でも fol は更新・保存する。
	BOOL UpdateTrackPath(int index, LPCTSTR newPath);
	void DeleteTracksByIndices(std::vector<int> indices);

	CBrush m_brDlg;

	HICON m_hIcon;
	void RefreshNavControls();
	void ScheduleRefreshNavControls();
protected:
	afx_msg LRESULT OnReapplyOpaqueFixers(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPlMissDone(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPlJakDone(WPARAM wParam, LPARAM lParam);
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV サポート
	CToolTipCtrl m_tooltip;

	DECLARE_MESSAGE_MAP()
public:

	CString UTF8toSJIS(const char* a);
	CString UTF8toUNI(const TCHAR* a);

	virtual BOOL OnInitDialog();
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	CCustomStandardButton m_lsup;
	CCustomStandardButton m_lup;
	CCustomStandardButton m_lsdown;
	CCustomStandardButton m_ldown;
	afx_msg void OnNcDestroy();
	virtual BOOL DestroyWindow();
	afx_msg int Create(CWnd *pWnd);
	// MP裏生成で -32000 に置かれた窓を、ファルコム表示時に画面内へ戻す
	void EnsureOnScreen();
	afx_msg void OnClose();
	afx_msg void OnBnClickedOk();
	CCustomListCtrl m_lc;
	afx_msg void OnUP();
	afx_msg void OnSUP();
	afx_msg void OnSDOWN();
	afx_msg void OnDOWN();
	afx_msg void OnLvnKeydownList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg void OnNMDblclkList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnSize(UINT nType, int cx, int cy);
#if WIN64
	afx_msg void OnTimer(UINT_PTR nIDEvent);
#else
	afx_msg void OnTimer(UINT nIDEvent);
#endif
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	CCustomEdit m_e;
	CCustomCheckBox m_renzoku;
	CCustomCheckBox m_loop;
	afx_msg void OnBnClickedCheck4();
	afx_msg void OnBnClickedCheck1();
	CCustomCheckBox m_tool;
	afx_msg void OnLvnBegindragList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLvnGetdispinfoList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnNMRclickList1(NMHDR *pNMHDR, LRESULT *pResult);
	CCustomCheckBox m_saisyo;
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg void OnPop32787();
	afx_msg void OnPopWavExport();
	afx_msg void OnPopTranscode();
	afx_msg void OnPopXfadeExport();
	afx_msg void OnPopTagEdit();
	afx_msg void OnPopTagBatch();
	CCustomEdit m_find;
	afx_msg void OnFindUp();
	afx_msg void OnFindDown();
	CCustomStandardButton m_findup;
	CCustomStandardButton m_finddown;
	CCustomCheckBox m_savecheck;
	CCustomCheckBox m_save_mp3;
	CCustomCheckBox m_save_kpi;
	afx_msg void OnBnClickedCheck6mp3();
	afx_msg void OnBnClickedCheck7dshow();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
	afx_msg void OnSizing(UINT fwSide, LPRECT pRect);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg BOOL OnNcActivate(BOOL bActive);
	CCustomComboBox m_listchange;
	CCustomComboBox m_endMode;
	afx_msg void OnCbnSelchangeCombo1();
	afx_msg void OnCbnSelchangeEndMode();
	afx_msg void OnBnClickedButton3();
	afx_msg void OnBnClickedPlaydelete();
	afx_msg void OnBnClickedPianoroll();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnDestroy();
	void LayoutHelpBtn();
	void ShowHelpSheet();
	CCustomStandardButton m_namechage;
	CCustomStandardButton m_listdelete;
	CCustomStandardButton m_pianorollBtn;
	CCustomStandardButton m_help;
	CFont m_fontList;
};

CString NormalizePlaylistPath(LPCTSTR fol);
CString PlPhysicalMediaPath(LPCTSTR fol);
CString PlStorePlaylistFol(LPCTSTR fol, int sub);
// Falcom ゲームBGM等: fol は basename のみで play() 時に解決するため、パス存在では欠損判定しない。
BOOL PlIsFalcomGameBgmMode(int sub);
// リスト欠損バッジ用。TRUE=実ファイルとして欠落していると判断できる場合のみ。
BOOL PlTrackLooksMissing(int sub, LPCTSTR fol);
// 欠損ディスクキャッシュ(%LOCALAPPDATA%\oggYSED\miss)。-1=未登録 / 0=存在 / 1=欠損
int PlMissDiskGet(LPCTSTR fol);
void PlMissDiskSet(LPCTSTR fol, int miss);
void PlMissDiskForget(LPCTSTR fol);
// ジャケットサムネディスクキャッシュ(%LOCALAPPDATA%\oggYSED\jak)
CString PlJakDiskPath(LPCTSTR fol, BOOL noneSentinel);
void PlJakDiskForget(LPCTSTR fol);
// 歌詞(.lrc)有無キャッシュ(%LOCALAPPDATA%\oggYSED\lrcflag)。-1=未 / 0=あり / 1=なし
int PlLrcDiskGet(LPCTSTR fol);
void PlLrcDiskSet(LPCTSTR fol, int none);
void PlLrcDiskForget(LPCTSTR fol);
CString PlLrcSidecarPath(LPCTSTR fol);
// 未キャッシュならサイドカーを見て登録。戻り: 0=あり 1=なし (-1はfol不正)
int PlLrcProbe(LPCTSTR fol);
// チャンネル数キャッシュ(%LOCALAPPDATA%\oggYSED\chflag2)。-1=未 / 0=不明 / 1..8=ch
int PlChDiskGet(LPCTSTR fol);
void PlChDiskSet(LPCTSTR fol, int ch);
void PlChDiskForget(LPCTSTR fol);
// 未キャッシュならヘッダを覗いて登録。戻り: 0=不明 / 1..8=ch (-1はfol不正)
// 対応 peek: wav/rf64, flac, mp3, ogg/opus, dsf/dff/wsd。再生時も確定 ch を書き戻す
int PlChProbe(LPCTSTR fol);
// 印用ラベル: MONO / LR / 2.1 / 3 / 4 / 5.1 / 7.1 等。ch<=0 は空
void PlChFormatLabel(int ch, CString& out);
// [SAV]=曲ごと保存 / [LRC]=歌詞 / [MONO]|[LR]|[2.1]…=ch / [16ch]|[32ch] / [XG]|[88]…。キャッシュのみ参照
void PlFormatRowMarks(int row, LPCTSTR fol, CString& out);
// MIDI 印キャッシュ(%LOCALAPPDATA%\oggYSED\midflag)。-1=未
int PlMidDiskGet(LPCTSTR fol, int* ch32, int* mapKind, int* sysMode, int* mapForce);
void PlMidDiskSet(LPCTSTR fol, int ch32, int mapKind, int sysMode, int mapForce);
int PlMidProbe(LPCTSTR fol);          // 非MIDIは 0。未キャッシュなら走査して登録
int PlMidProbeIfNeeded(LPCTSTR fol);  // 走査したとき 1
void PlMidForceSet(LPCTSTR fol, int mapForce); // 0=自動 1..19=モニタと同じ
int PlFmForceGet(LPCTSTR fol); // -1=グローバル従属 / 0=BEEP / 1=OPN / 2=OPNA
void PlFmForceSet(LPCTSTR fol, int fmMode); // -1 clear, else 0..2
void PlMidNotifyMarkViews();

enum {
	PL_CTX_INFO = 1,
	PL_CTX_WAV = 2,
	PL_CTX_DEL = 3,
	PL_CTX_REMOVE_MISSING = 4,
	PL_CTX_CLEAR_SONGPARAM = 5,
	PL_CTX_PROTOOLS = 6,
	PL_CTX_TRANSCODE = 7,
	PL_CTX_SORT_NAME = 8,
	PL_CTX_SORT_ART = 9,
	PL_CTX_SORT_ALB = 10,
	PL_CTX_SORT_TIME = 11,
	PL_CTX_ADD_FOLDER = 12,
	PL_CTX_AB_SET_A = 13,
	PL_CTX_AB_SET_B = 14,
	PL_CTX_AB_CLEAR = 15,
	PL_CTX_COPY_TITLEART = 16,
	PL_CTX_ANALYZER = 17,
	PL_CTX_PIANOROLL = 18,
	PL_CTX_OPEN_FOLDER = 19,
	PL_CTX_ADD_SAME_FOLDER = 20,
	PL_CTX_RESCAN_MISS = 21,
	PL_CTX_REFRESH_JAK = 22,
	PL_CTX_TAG_EDIT = 23,
	PL_CTX_XFADE = 24,
	PL_CTX_MICMIX = 25,
	PL_CTX_QUEUE_ADD = 26,
	PL_CTX_QUEUE_PLAYNEXT = 27,
	PL_CTX_QUEUE_CLEAR = 28,
	PL_CTX_MB_AUTOTAG = 29,
	PL_CTX_BPM = 30,
	PL_CTX_BPM_CAND1 = 37,
	PL_CTX_BPM_CAND2 = 38,
	PL_CTX_BPM_CAND3 = 39,
	PL_CTX_VIDEO_EXTRACT = 40,
	PL_CTX_NORM_SCAN = 31,
	PL_CTX_EXPORT_AB = 32,
	PL_CTX_SSVIZ = 33,
	PL_CTX_DESK_LRC = 34,
	PL_CTX_DUPES = 35,
	PL_CTX_FOLDER_SYNC = 36,
	PL_CTX_EQ = 41,
	PL_CTX_TEMP_CLEAR = 42,
	PL_CTX_TEMP_EXIT = 43,
	PL_CTX_UNDO_DEL = 44,
	PL_CTX_EDIT_SELALL = 45,
	PL_CTX_EDIT_COPY = 46,
	PL_CTX_EDIT_CUT = 47,
	PL_CTX_EDIT_PASTE = 48,
	PL_CTX_EDIT_REDO = 49,
	PL_CTX_TAG_BATCH = 50,
	PL_CTX_MIDIMON = 51,
	PL_CTX_SURROUND = 52,
	PL_CTX_MIDMAP_BASE = 53, // +0 Auto … +19 PVmap（MIDIモニタと同じ順）
	PL_CTX_MIDMAP_LAST = PL_CTX_MIDMAP_BASE + 19,
	PL_CTX_SASAMIM_BASE = 74, // ささみ☆ﾐ 音源モード（mpy/mpw2）
	PL_CTX_SASAMIM_LAST = PL_CTX_SASAMIM_BASE + 19,
	PL_CTX_FMMON = 94, // SASAMI FPY FMモニタ
	PL_CTX_SASAMIFM_BASE = 95, // ささみ☆ﾐ FMモード +0=BEEP +1=OPN +2=OPNA
	PL_CTX_SASAMIFM_LAST = PL_CTX_SASAMIFM_BASE + 2,
	PL_CTX_MOVE_BASE = 42500,
	PL_CTX_COPY_BASE = 43500,
	PL_CTX_MOVE_MAX = PL_CTX_MOVE_BASE + 999,
	PL_CTX_COPY_MAX = PL_CTX_COPY_BASE + 999,
};

// あいまい検索 / 絞り込み共通。useRegex=FALSE は部分一致(大小無視)。
BOOL PlaylistItemMatchesSearch(const playlistdata0& item, LPCTSTR pattern, BOOL useRegex);

// 大量フィルタ用: 正規表現を1回だけコンパイル
struct PlaylistSearchCtx;
PlaylistSearchCtx* PlaylistSearchCtxCreate(LPCTSTR pattern, BOOL useRegex);
BOOL PlaylistSearchCtxMatch(PlaylistSearchCtx* ctx, const playlistdata0& item);
void PlaylistSearchCtxDestroy(PlaylistSearchCtx* ctx);
