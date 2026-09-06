// oggDlg.h : ヘッダー ファイル
//
#include "afxmt.h"
//#include "afxcmn.h"
#if !defined(AFX_OGGDLG_H__6E748E56_5CF6_4ADE_8B4F_7FE83E42DCFA__INCLUDED_)
#define AFX_OGGDLG_H__6E748E56_5CF6_4ADE_8B4F_7FE83E42DCFA__INCLUDED_

// プレイリスト行の表示用メタデータを og 側へ反映(未再生時のバナー/情報パネル用)
void ApplyPlaylistRowDisplay(const playlistdata0& row);
void OggPersistSaveDatNow();

#ifndef WM_TIMERP_VSYNC_TICK
#define WM_TIMERP_VSYNC_TICK (WM_APP + 70)
#endif
#ifndef WM_SPEANA_TICK
#define WM_SPEANA_TICK (WM_APP + 73)
#endif
#ifndef WM_ENDPOINT_VOLUME
#define WM_ENDPOINT_VOLUME (WM_APP + 74)  // Windows 主音量変更 → スライダー同期
#endif
#ifndef WM_REFRESH_AERO_ALL
#define WM_REFRESH_AERO_ALL (WM_APP + 71)
#endif
#ifndef WM_OGG_DEFERRED_HEAVY_INIT
#define WM_OGG_DEFERRED_HEAVY_INIT (WM_APP + 100)
#endif
#ifndef WM_OGG_ENTER_MP_MODE
#define WM_OGG_ENTER_MP_MODE (WM_APP + 101)
#endif
// MP から EQ/ピアノ/アナの開閉を遅延実行(ボタンハンドラ内 Create/Destroy 回避)
// wParam: 1=piano toggle, 2=analyzer toggle, 3=MIDI monitor toggle, 4=FM monitor toggle
//         10..18=起動時サブUI復元(1メッセージ=1 Create、次を PostMessage)
//         10=EQ 11=ピアノ 12=Tune 13=アナライザ 14=プロンプト 15=ロール 16=DJパッド 17=MIDIモニタ 18=FMモニタ
#ifndef WM_OGG_TOGGLE_SUBUI
#define WM_OGG_TOGGLE_SUBUI (WM_APP + 102)
#endif
#ifndef WM_PLAYBACK_AUTO_STOPPED
#define WM_PLAYBACK_AUTO_STOPPED (WM_APP + 72)
#endif
// 動画画面を閉じる(タイトルバー× / 右クリック)。本体側で tear-down
#ifndef WM_OGG_CLOSE_DOUGA
#define WM_OGG_CLOSE_DOUGA (WM_APP + 103)
#endif
// 途中再生確認。Space の KEYDOWN 中に MessageBox すると同じキーではいが押される
#ifndef WM_OGG_RESUME_PROMPT
#define WM_OGG_RESUME_PROMPT (WM_APP + 104)
#endif
#ifndef IDT_OGG_RESUME_PROMPT
#define IDT_OGG_RESUME_PROMPT 10404
#endif
#ifndef IDT_OGG_RESUME_RESTART
#define IDT_OGG_RESUME_RESTART 10405
#endif

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
/////////////////////////////////////////////////////////////////////////////
// COggDlg ダイアログ
#include "LinkStatic.h"
#include "afxwin.h"
#include "afxcmn.h"
#include "atlimage.h"
#include <vector>
#include "afxwin.h"

#include <cstddef>
#include "resource.h"
#include "CCustomControl.h"

struct WavExportOptions {
	int fadeEnable;
	float fadeSec;
	int trimLeadEnable;
	float trimKeepSec;
	// タグ上書き(空欄は触らない)。copyTags=1 なら元ファイルからコピーしたうえで欠損のみ埋める
	int copyTags;
	int multiFile; // 1=タイトルは適用しない
	CString tagTitle;
	CString tagArtist;
	CString tagAlbum;
	CString coverImagePath; // jpg/png。指定時はジャケットをこれに差し替え
	float kpiDurationSec; // KPI(mode==-3)用の書き出し秒数。0以下は既定(240=4分)
	int sampleRate; // 0=ソースのまま。44100/48000/96000/192000 でリサンプル（明示8000..384000も可）
	int forceChannels; // 0=ソース。クロスフェード追従用 1..8
	int forceBits; // 0=ソース。クロスフェード追従用 16/24/32
	int applyPrompt; // 1=プロンプト実行を書き出しPCMへ時間適用
	// A-B / 範囲書き出し(末尾追記。0=未指定=先頭〜終端)
	int startFrame;
	int endFrame;
};

class CEqualizer;
class CPianoRoll;
class CPianoRollTuneDlg;
class CAnalyzerDlg;
class CMidiMonitorDlg;
class CFmMonitorDlg;
class CDouga;
class CPlayList;
class CRender;
class COggApp;
class COggDlg : public CCustomBlurDialogBase
{
	friend class CDouga;
	friend class CPlayList;
	friend class CRender;
	friend class COggApp;
// 構築
public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	COggDlg(CWnd* pParent = NULL);	// 標準のコンストラクタ
	float hD;
	void gamen(int uu);
	void gamenkill();
	void CloseVideoScreen(); // 動画画面だけ閉じる(動画専用再生なら停止も)
	void dougaplay(int uu, CString str = L"");
	DWORD GetVol();
	// bPaintBars=FALSE なら PCM→EQコード供給のみ（メイン GDI pending 中でも呼ぶ）
	// bFillLevels=TRUE なら描画せず spelv のみ更新（ローカルリモート用。bPaintBars と併用可）
	void Speana(BOOL bPaintBars = TRUE, BOOL bFillLevels = FALSE);
	void SyncPianoRollFromPlayCursor();
	void SyncPianoRollFast();
	void SyncAnalyzerFromPlayCursor();
	void TogglePianoRoll();
	void ToggleAnalyzer();
	void ToggleMidiMonitor();
	void ToggleFmMonitor();
	void HideMidiMonitorForMinimize(); /* FM/MIDI モニタ・譜面・VSTホスト等も隠す */
	void RestoreMidiMonitorAfterMinimize();
	void ShowPianoRollTune();
	void FeedPianoRoll(const void* pData, int bytes);
	// x,y は論理座標(*4)。戻り値は描画文字列のピクセル幅(hFont / GetTextExtent)。
	int  moji(CString s, int x, int y, COLORREF rgb);
	// x_px は 4x スケール済みピクセル X。y は論理座標(*4)。
	int  mojiPx(CString s, int x_px, int y, COLORREF rgb);
	int  mojisub(CString s, int x, int y, COLORREF rgb);
	CString UTF8toSJIS(const char* a);
	CString UTF8toUNI(const TCHAR* a);
	void Resize();
	void Closeds();
	void timerp();
	void Vol(int vol);
	void dp(CString a);
	LRESULT dp1(WPARAM, LPARAM);
	LRESULT dp2(WPARAM, LPARAM);
	LRESULT OnResumePrompt(WPARAM, LPARAM);
	void SetAdd(CString fnn,int mode,int loop1,int loop2,CString filen,int ret2,REFTIME time);
	// applyTags=false は中間WAV用(タグ/ジャケットのコピーを行わない)。
	BOOL ExportToWav(playlistdata0* pc, CString outputPath, int loopCount, const WavExportOptions* opts = NULL, bool applyTags = true);
	// format: 0=mp3 1=FLAC。一旦WAV書き出ししてから変換。
	BOOL ExportToTranscode(playlistdata0* pc, CString outputPath, int loopCount, const WavExportOptions* opts, int format, int mp3Kbps, int flacLevel);
	BOOL AnalyzeTrackForPrompt(playlistdata0* pc);
	double goertzel(const float* data, int N, double target_freq, double sample_rate);
	double hanWindow(int value, int index, int offset, int size);
	// dest!=NULL のとき og->img を汚さず dest へ読み込む(リスト用サムネ等)。
	void LoadJacket(CString s, CImage* dest = NULL);
	// リスト抽出済みの共有ディスク(等身大)を本編 img へ載せる。成功で TRUE。
	BOOL AdoptJacketFromDisk(LPCTSTR path);
	CString mp3file;
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	IMMDeviceEnumerator *deve;
	IMMDevice *dev;
	IAudioEndpointVolume *audio;
	void dsdload(CString&filen, CString&tagfile, CString&tagname, CString&tagalbum, ULONGLONG&po, int flg);
	void dsdclose();
	BOOL ReleaseDXSound();
	CString init(HWND hwnd,int sm=44100);
	int WASAPIInit();
	void WASAPIChange(WAVEFORMATEX* pwfx);
	static UINT wavread(LPVOID);
	CEvent timer;
	HKMP kmp, kmp1;
#if _UNICODE
	void _CreateShellLink(LPWSTR pszArguments, LPWSTR pszTitle, IShellLink **ppsl, int iconindex, bool WA,BOOL wa2=TRUE);
#else
	void _CreateShellLink(LPSTR pszArguments, LPSTR pszTitle, IShellLink **ppsl, int iconindex, bool WA,BOOL wa2=TRUE);
#endif
	CBrush *m_pDlgColor;

	// ポインタ化: ヘッダ完結型不要 → PCH/依存TUがピアノロール変更で全再ビルドされない
	CEqualizer* m_EqualizerDlg = nullptr;
	CPianoRoll* m_PianoRollDlg = nullptr;
	CPianoRollTuneDlg* m_PianoRollTuneDlg = nullptr;
	CAnalyzerDlg* m_AnalyzerDlg = nullptr;
	CMidiMonitorDlg* m_MidiMonitorDlg = nullptr;
	CFmMonitorDlg* m_FmMonitorDlg = nullptr;
	/* 本体最小化で隠した FM/MIDI 系（bit: 下記 kHideFmMidi*） */
	int m_fmMidiToolsHiddenMask = 0;
	bool m_cascadePrevValid = false;
	CRect m_cascadePrevRc;
	// SyncAnalyzerFromPlayCursor: bufwav3 上の前回終端バイト位置
	BOOL m_analyzerSyncValid = FALSE;
	ULONG m_analyzerSyncEndPos = 0;
	// カスタム帯+GDI バナーにマイク行だけ少し被る分の補正済みか
	BOOL m_bMicRowNudged = FALSE;
	void NudgeMicMixBelowGdi();

	CString lrc[300];
	DWORD lrctm[300];
	int lrcnum=0;
	int lrccur = 0; // 現在ハイライト中の歌詞行
	CString lrc_backup;

	KMPMODULE *mod;
	HINSTANCE hDLLk;
	pfnGetKMPModule pFunck;
	SOUNDINFO sikpi;
	TCHAR kpi[512];
	int jx = -1, jy;
	double jxy;
	CBitmap bmp, bmp1;
	CImage img;
	double m_jacketFocus;
	DWORD m_lastTick;
	CBrush m_brDlg;

	CFont* m_newFont;
	CFont* m_newFont1;
	// ダイアログ データ
	//{{AFX_DATA(COggDlg)
	enum { IDD = IDD_OGG_DIALOG };
	CCustomCheckBox	m_ysc2;
	CCustomCheckBox	m_ysc1;
	CCustomStatic	m_dsvols;
	CCustomSliderCtrl	m_dsval;
	CCustomCheckBox	m_zweiii;
	CCustomCheckBox	m_ed6tc;
	CCustomCheckBox	m_yso;
	CCustomRangeSliderCtrl	m_time;
	//CSliderCtrl	m_time;
	CCustomEdit	m_kaisuu;
	CCustomCheckBox	m_junji;
	CCustomCheckBox	m_random;
	CCustomCheckBox	m_xfade;
	CCustomEdit	m_xfadeSec;
	CCustomStatic	m_xfadeL;
	CCustomCheckBox	m_sita;
	CCustomStandardButton	m_ue;
	CCustomCheckBox	m_ed6sc;
	CCustomCheckBox	m_ed6fc;
	CCustomCheckBox	m_ysf;
	CCustomCheckBox	m_ys6;
	CCustomCheckBox	m_st;
	CCustomCheckBox	m_supe;
	CCustomStatic	m_sokudos;
	CCustomStatic	m_onteis;
	CCustomSliderCtrl	m_sokudo;
	CCustomSliderCtrl	m_ontei;
	CCustomStandardButton	m_ps;
	CCustomStatic	m_vol;
	CCustomSliderCtrl	m_sl;
	CCustomCheckBox	m_dou;
	CCustomCheckBox	m_c2;
	CCustomCheckBox	m_micmix;
	CCustomSliderCtrl	m_miclev;
	CCustomStatic	m_miclevs;
	CCustomComboBox m_micdev;
	CCustomStandardButton m_micdevRefresh;
	afx_msg void OnMicMixCheck();
	afx_msg void OnMicLevRelease(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnCbnSelchangeMicDev();
	afx_msg void OnMicDevRefresh();
	afx_msg LRESULT OnAudioDevChanged(WPARAM wParam, LPARAM lParam);
	void SyncMicMixUiFromSavedata();
	void ApplyMicMixLevelLabel();
	CCustomStatic	m_11;
	//}}AFX_DATA
	void play();
	void stop();
	BOOL stop1();
	void ResetPauseButtonUi();
	void SyncPauseButtonUi();
	void RefreshAllAeroWindows();
	void PostRefreshAllAeroWindows();
	static void Modec();
CWinThread * m_thread;
CWinThread* m_thread1;
	void rl(int);
	afx_msg void OnPause();
	void plug(CString ff,KMPMODULE *mod);
	void plugloop(CString ff);
//	void mcopy(char* a,char* b,int len);
	// ClassWizard は仮想関数のオーバーライドを生成します。
	//{{AFX_VIRTUAL(COggDlg)
	public:
	virtual BOOL DestroyWindow();
	afx_msg void OnRestart();
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV のサポート
	//}}AFX_VIRTUAL
// インプリメンテーション
protected:
	CToolTipCtrl m_tooltip;
	HICON m_hIcon;
	int randomf;
	// 生成されたメッセージ マップ関数
	//{{AFX_MSG(COggDlg)
	virtual BOOL OnInitDialog();
	afx_msg LRESULT OnDeferredHeavyStartup(WPARAM wParam, LPARAM lParam);
	void DeferredHeavyStartupImpl();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnDropFiles(HDROP hDropInfo);
#if WIN64
	afx_msg void OnTimer(UINT_PTR nIDEvent);
#else
	afx_msg void OnTimer(UINT nIDEvent);
#endif
	afx_msg void OnButton1();
	afx_msg void OnButton2();
	virtual void OnOK();
	afx_msg void OnButton5();
	afx_msg void OnButton6_FC();
	afx_msg void OnButton7_YSF();
	afx_msg void OnButton8_YS6();
	afx_msg void OnButton9_Folder();
	afx_msg void OnButton12();
	afx_msg void OnCheck5();
	afx_msg void OnCheck6();
	afx_msg void OnPlayXfade();
	afx_msg void OnPlayXfadeSec();
	afx_msg void OnButton14();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnYso();
	afx_msg void OnButton17_ED6TC();
	afx_msg void OnZWEIII();
	afx_msg void OnButton21();
	afx_msg void OnYsC1();
	afx_msg void OnYsC2();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedButton25();
	CCustomCheckBox m_xa;
	afx_msg void OnBnClickedButton27();
	afx_msg void OnBnClickedButton28();
	afx_msg void OnBnClickedButton31();
	CCustomCheckBox m_ys121;
	CCustomCheckBox m_ys122;
	CCustomCheckBox m_sor;
	afx_msg void OnBnClickedButton33();
	CCustomCheckBox m_zwei;
	CCustomCheckBox m_gurumin;
	afx_msg void OnBnClickedButton35();
	CCustomStandardButton m_rund;
	afx_msg void OnBnClickedButton37();
	CCustomCheckBox m_dino;
	CCustomStandardButton m_saisai;
	afx_msg void OnNMReleasedcaptureSlider2(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnBnClickedButton39();
	CCustomCheckBox m_br4;
	afx_msg void OnBnClickedButton44();
	afx_msg void OnBnClickedButton45();
	afx_msg void OnBnClickedButton46();
	CCustomCheckBox m_ed3;
	CCustomCheckBox m_ed4;
	CCustomCheckBox m_ed5;
	CCustomStandardButton d_ys6;
	CCustomStandardButton d_ys3;
	CCustomStandardButton d_yso;
	CCustomStandardButton d_ed6fc;
	CCustomStandardButton d_ed6sc;
	CCustomStandardButton d_ed6tc;
	CCustomStandardButton d_z2;
	CCustomStandardButton d_ysc1;
	CCustomStandardButton d_ysc2;
	CCustomStandardButton d_xa;
	CCustomStandardButton d_ys1;
	CCustomStandardButton d_ys2;
	CCustomStandardButton d_sor;
	CCustomStandardButton d_z1;
	CCustomStandardButton d_guru;
	CCustomStandardButton d_dino;
	CCustomStandardButton d_br4;
	CCustomStandardButton d_ed3;
	CCustomStandardButton d_ed4;
	CCustomStandardButton d_ed5;
	afx_msg void OnBnClickedButton47();
	CCustomStandardButton d_tuki;
	CCustomStandardButton d_nishi;
	afx_msg void OnBnClickedButton48();
	CCustomStandardButton d_arc;
	afx_msg void OnBnClickedButton51();
	afx_msg LRESULT OnUpdateAvailable(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnKpiPluginMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnCemuCatListMsg(WPARAM wParam, LPARAM lParam);
	CCustomStandardButton d_san1;
	afx_msg void OnBnClickedButton53();
	CCustomStandardButton d_san2;
	afx_msg void OnBnClickedButton54();
	afx_msg BOOL OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnSysKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	LRESULT OnHotKey(WPARAM wp, LPARAM);
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnEnterSizeMove();
	afx_msg void OnExitSizeMove();
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	CCustomStandardButton m_playlist;
	afx_msg void OnPlayList();
	afx_msg void OnBnmp3jake();
	CCustomStandardButton m_mp3jake;
	CCustomStatic m_OS;
	CCustomSliderCtrl m_kakuVol;
	CCustomStatic m_kakuVolval;
	CCustomStatic m_cpu;

	CCustomStatic m_os3;
	afx_msg void OnDestroy();
	virtual BOOL Create(LPCTSTR lpszTemplateName, CWnd* pParentWnd = NULL);
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
	afx_msg void OnActivateApp(BOOL bActive, DWORD dwThreadID);
	afx_msg BOOL OnNcActivate(BOOL bActive);
	CCustomStatic m_lrc;
	CCustomSliderCtrl m_tempo_sl;
	CCustomStatic m_temp_num;
	CCustomStatic m_lrc2;
	CCustomStatic m_lrc3;
	CCustomStatic m_lrc4;
	CCustomStatic m_lrc5;
	CCustomStatic m_pitch;
	CCustomSliderCtrl m_pitch_sl;
	CCustomStatic m_temp_s;
	CCustomStatic m_pitch_s;
	afx_msg void OnTempoStatic();
	afx_msg void OnPitchStatic();
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	CCustomStandardButton m_stoppp;
	CCustomStandardButton m_setteiii;
	CCustomStandardButton m_folderrrr;
	CCustomStandardButton m_syuryouuuu;
	afx_msg void OnStnClickedStatic2();
	CCustomStatic dummys1;
	CCustomStatic m_dummys2;
	CCustomStatic m_dummys3;
	CCustomStatic m_dummys4;
	afx_msg void OnStnDblclickStaticp();
	afx_msg void OnStnDblclickStatict();
	CCustomStandardButton m_fadedummy;
	afx_msg void OnBnClickedButton59();
	CCustomStandardButton m_eqq;
	CCustomStandardButton m_help;
	void LayoutHelpBtn();
	void ShowHelpSheet();
	afx_msg void OnBnClickedHelp();
	BOOL drawth = FALSE;

	afx_msg void OnSwitchMode();   // メディアプレイヤーモードへ切替
	afx_msg LRESULT OnEnterFalcomMsg(WPARAM, LPARAM);  // mp→ファルコム特化型 復帰(遅延実行)
	afx_msg LRESULT OnToggleSubUiMsg(WPARAM wParam, LPARAM lParam);  // mp→ピアノ/アナ開閉・起動復元(遅延)
	afx_msg void OnWindowPosChanging(WINDOWPOS* lpwndpos);
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	void StartTimerpVsyncThread();
	void StopTimerpVsyncThread();
	afx_msg LRESULT OnTimerpVsyncTick(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnSpeanaTick(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnEndpointVolume(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnRefreshAeroAll(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPlaybackAutoStopped(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnCloseDougaMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnEnterMpModeMsg(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnSongParamRestore(WPARAM wParam, LPARAM lParam); // 曲ごとパラメータ復元(再生スレッド→メイン)
	afx_msg LRESULT OnSongParamMarks(WPARAM wParam, LPARAM lParam);   // ★列再描画
	afx_msg LRESULT OnProAudioCueSeek(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnXfadeStart(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnXfadePromoted(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnXfadePreloadJacket(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnXfadePromoteUi(WPARAM wParam, LPARAM lParam);
	HANDLE m_hTimerpVsyncThread;
	HANDLE m_hTimerpVsyncStopEvent;
};

void COggDlg_SyncPianoRollFast();
void COggDlg_ShowPianoRollTune();
void COggDlg_SyncAnalyzerFast();
BOOL COgg_IsEqualizerVisible();

void SetupTaskbarThumbButtons(HWND hwnd, BOOL mediaPlayerMode);
void RefreshTaskbarJumpList(BOOL mediaPlayerMode);
void MpPushPlayHistory(LPCTSTR path, LPCTSTR displayName);
void MpPersistSavedataQuick();
void PlaylistEndModeFillCombo(CComboBox& cb);
void MpMicMixRestartIfRunning();
int MpMicPeakLevel(); // 0..1000
void MpTaskbarReplay();
void MpTaskbarNextTrack();
void MpTaskbarPrevTrack();
double OggGetGdiPlaybackTimeSec();
/* バナーと同じ可聴 PCM フレーム（playb − DS 書込先行）。FM モニタ同期用 */
__int64 OggGetHeardPcmFrames();
// テンポ/ピッチ スライダー(0..400, 200=100%) → 表示% / RB 換算（MP と本体で共通）
float TempoPercentFromPos(int tempoPos);
double TempoPlaybackRateFromPos(int tempoPos);
float TempoTimeRatioFromPos(int tempoPos);
float PitchScaleFromPos(int pitchPos);
int TempoPosFromPercent(float percent); // 表示% → スライダー 0..400
void OggResetRubberBandStretcher();
void RequestPlaybackRestart(HWND hwnd = NULL);
void OggCancelPendingPlaybackRestart(); // 停止など: 積んだ再演奏を捨てる
BOOL OggIsResumePromptActive();         // 途中再生のはい/いいえ/キャンセル表示中
// 再生開始前に途中位置を確認。キャンセルなら FALSE（再生しない。.save は残す）
BOOL OggPrepareResumeBeforePlayback(LPCTSTR mediaPath);
void OggRunResumePrompt(); // 表示中ウィンドウ側から呼ぶ途中再生確認
// タグ編集など: 現再生位置を .save に書き、次回 play で確認なし復帰する
void OggArmSilentResumeFromCurrent();
// リモート再生: MessageBox なし。Yes=途中から / No=先頭から(.save削除)
void OggArmRemoteSilentResumeYes();
void OggArmRemoteSilentResumeNo();
// 先頭から再生するが .save は残す（前へボタンの3秒ルール等）
void OggArmResumeFromStartKeepSave();

// KPI プラグイン: confirm/startupEmpty/reloadAfter。いいえ(起動0件時)は kpi_plugin_dl_skip を立てる。
BOOL OggKpiDownloadPlugins(CWnd* owner, BOOL confirm, BOOL startupEmpty, BOOL reloadAfter);
BOOL OggKpiReloadPlugins(CWnd* owner);
#ifndef WM_APP_KPI_PLUGIN
#define WM_APP_KPI_PLUGIN (WM_APP + 58) // wParam: 1=DL 2=再読込
#endif
#ifndef WM_APP_CEMU_CATLIST
#define WM_APP_CEMU_CATLIST (WM_APP + 59) // Cemu対応一覧を開く
#endif

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ は前行の直前に追加の宣言を挿入します。

#endif // !defined(AFX_OGGDLG_H__6E748E56_5CF6_4ADE_8B4F_7FE83E42DCFA__INCLUDED_)
