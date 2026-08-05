// stdafx.h : 標準のシステム インクルード ファイルのインクルード ファイル、または
// 参照回数が多く、かつあまり変更されない、プロジェクト専用のインクルード ファイル
// を記述します。

// uni_avx2_vs2026|x86(oggのみ)でビルドすること。
// 関数名命名規則：分かりやすい短い関数とすること。
#pragma once
#pragma warning( disable : 4142 4091 )
#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN            // Windows ヘッダーから使用されていない部分を除外します。
#endif
#define DIRECT3D_VERSION 0x900
#define DIRECTSOUND_VERSION 0x0900
#ifdef VLD_FORCE_ENABLE
//#include "vld.h"
#endif
#include <SDKDDKVer.h>
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS      // 一部の CString コンストラクターは明示的です。

// 一般的で無視しても安全な MFC の警告メッセージの一部の非表示を解除します。
#define _AFX_ALL_WARNINGS

#include <afxwin.h>         // MFC のコアおよび標準コンポーネント
#include <afxext.h>         // MFC の拡張部分

//#include <afxdisp.h>        // MFC オートメーション クラス



#ifndef _AFX_NO_OLE_SUPPORT
#include <afxdtctl.h>           // MFC の Internet Explorer 4 コモン コントロール サポート
#endif
#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>             // MFC の Windows コモン コントロール サポート
#endif // _AFX_NO_AFXCMN_SUPPORT

#include <atlimage.h> // CImage / GDI+（oggDlg を PCH から外したためここで確保）

#pragma warning(disable : 4995)

#define _AFX_DISABLE_DEPRECATED
#ifndef _SECURE_ATL
#define _SECURE_ATL 1
#endif
typedef double REFTIME;
#include "yaneCriticalSection.h"
/*
typedef PUINT DWORD_PTR
#include	<d3d8.h>
#include	<d3dx8.h>
#include <dxerr8.h>
#include <dmusici.h>    // DirectAudioを使用可能にする
*/

#include "kmp_pi.h"
#include "dshow.h"

#include <mmdeviceapi.h>
#include <endpointvolume.h>

struct playlistdata0{
	TCHAR name[1024];
	TCHAR art[1024];
	TCHAR alb[1024];
	TCHAR fol[1024];
	int sub;
	TCHAR game[256];
	int loop1;
	int loop2;
	int ret2;
	int icon;
	int time;
};

// 必ず末尾に追加すること
struct save{
	TCHAR ysf[1024];
	TCHAR ys6[1024];
	TCHAR ed6fc[1024];
	TCHAR ed6sc[1024];
	int douga;
	int supe;
	int supe2;

	int random;
	int kaisuu;
	int gameflg[4];

	int xx,yy;
	int gx,gy;

	TCHAR yso[1024];
	int gameflg2;

	TCHAR ed6tc[1024];
	int gameflg3;

	TCHAR zweiii[1024];
	int gameflg4;

	int dsvol;
	int render;

	TCHAR ysc[1024];
	int gameflg5;
	int gameflg6;

	TCHAR xa[1024];
	int gameflg7;

	TCHAR ys12[1024];
	int gameflg8;
	int gameflg9;

	TCHAR sor[1024];
	int gameflg10;
	TCHAR ys122[1024];

	TCHAR zwei[1024];
	int gameflg11;

	TCHAR gurumin[1024];
	int gameflg12;

	TCHAR dino[1024];
	int gameflg13;

	RECT p;

	TCHAR br4[1024];
	int gameflg14;

	TCHAR ed3[1024];
	int gameflg15;

	TCHAR ed4[1024];
	int gameflg16;

	TCHAR ed5[1024];
	int gameflg17;

	TCHAR tuki[1024];
	TCHAR nishi[1024];
	TCHAR arc[1024];
	TCHAR san1[1024];
	TCHAR san2[1024];

	int fs;
	int evr;
	int con;
	int aero;
	int pl;
	int ffd;
	int vob;
	int haali;
	int spc;
	int mp3;
	int kpivol;

	TCHAR font1[1024];
	TCHAR font2[1024];

	int mp3orig;

	int audiost;

	int savecheck;
	int savecheck_mp3;
	int savecheck_dshow;

	int bit24;

	int m4a;

	int kakuVol;
	int kakuVal;

	int saveloop;
	int saverenzoku;

	int bit32;

	int ms;
	int ms2;

	GUID soundguid;
	int soundcur;

	TCHAR zero[1024];

	DWORD samples;

	double wup;

	int aerocheck;

	int playlistnum;
	TCHAR playlistname[1000][256*2];

	int speanamode;
	int speananum;

	int lrc_net;

	int eq[20];
	int eqsoundenv;
	int eqsoundeq;

	int eqx;
	int eqy;

	int eqsoundeffect;

	int eqwindow;
	int lang;
	int langselect;

	int upscale_enable;   // 1=設定優先のアップスケール有効（デフォルトON想定）
	int speaker_layout;   // 0=2ch 1=2.1ch 2=4ch 3=5.1ch 4=7.1ch 5=マッピングなし（ソースchのままレート/ビットのみ）

	__int64 lastUpdateCheck;  // update check: 0=not checked, else=last check time

	int pianorollwindow; // 1 = show, 0 = hide
	int pianorollx;
	int pianorolly;
	int pianorollw;
	int pianorollh;



	int saveversion; // 0=旧(ms2=スライダー1..60) 1=新(ms2=16..960ms)

	int eq_reverb; // 0-100 リバーブ 101-200 パンリバーブ
	int eq_chorus; // 0-100 コーラス 101-200 コーラスディストーション
	int eq_delay; // 0-100 ディレイ 101-200 マルチディレイ

	// --- メディアプレイヤーモード関連(末尾追記。旧.datは部分読込のため0初期化される) ---
	int playerMode;    // 現在の画面モード 0=ファルコム特化型 1=メディアプレイヤー
	int startupAsk;    // 起動時にモード選択ダイアログを出すか 1=出す 0=出さない
	int mpHasPos;      // メディアプレイヤー画面の保存座標が有効か 0=未設定 1=設定済み
	int mpx;           // メディアプレイヤー画面の左座標
	int mpy;           // メディアプレイヤー画面の上座標
	int mpw;           // メディアプレイヤー画面の幅
	int mph;           // メディアプレイヤー画面の高さ

	int inwoman;       // 隠し: 0=通常 1=淫女モード(UI演出のみ。F12を5回で切替)

	int mpcol[5];      // MPリスト列幅の意味スロット: [0]名前 [1]ゲーム [2]時間 [3]アーティスト [4]未使用(★/最終列は永続化しない)

	// WAV出力オプション(末尾追記)
	int wav_export_fade;           // 1=フェードアウト有効
	int wav_export_fade_sec;       // フェード秒数(既定15)
	int wav_export_trim_lead;      // 1=先頭無音を指定秒に揃える（長い→カット／短い→パッド）
	int wav_export_trim_keep_sec;  // 先頭無音の目標秒数(既定1)

	// 最近再生履歴(ジャンプリスト)。analyzer 等の新規フィールドはさらに末尾へ追記すること
	int mpHistCnt;
	TCHAR mpHistName[8][200];
	TCHAR mpHistPath[8][1024];

	// --- 以降は必ず末尾追記(途中挿入は旧.datをずらして破壊する) ---
	int analyzerwindow; // 1 = show, 0 = hide
	int analyzerx;
	int analyzery;
	int analyzerw;
	int analyzerh;
	int analyzerspeclayout; // 0=重ね 1=上下 2=左右 3=2x2 4=2x4
	// 周波数表示モード(CAnalyzerDlg::SpecStyle と同じ)
	// 0=Ozone(塗+線) 1=線のみ 2=バー 3=Cubase Frequency 4=Voxengo SPAN
	// 5=Ableton Spectrum 6=FabFilter Pro-Q
	int analyzerspecstyle;
	int analyzerpeakhold;   // 1=ピークホールド ON
	int analyzereqoverlay;  // 1=EQ帯域/ゲイン曲線オーバーレイ
	int analyzerwavespeed;  // 波形スクロール速度(%) 25..200 (100=等倍)
	int pianorollscrollspeed; // 簡易ピアノロール表示速度(%) 25..200 (100=等倍)
	// --- 簡易ピアノロール右クリック設定(末尾追記) ---
	int pianorollexprlegend; // 1=記号凡例表示
	int pianorollexprmarks;  // 1=表現記号表示
	int pianorolllevelmeter; // 1=レベルメーター表示
	int pianorolltopmost;    // 1=常に手前
	int pianorollreattack;   // 1=再アタック検出
	int pianorollimpulse;    // 1=打撃音ゴースト抑制
	int pianorollharmghost;  // 1=倍音ゴースト抑制
	int pianorollharmprof;   // 1=音色プロファイル判定
	// --- アナライザー右クリック設定(末尾追記) ---
	int analyzerlevelmeter;  // 1=レベルメーター表示
	int analyzertopmost;     // 1=常に手前

	// --- メディアプレイヤー プロンプト(演奏アレンジ) ---
	TCHAR mpPromptText[2001];       // プロンプト本文(MAX2000文字+終端)
	int mpPromptBackupValid;        // 1=バックアップ有効(実行時に保存)
	int mpPromptBackupPitch;        // スライダー位置 0..400 (200=100%)
	int mpPromptBackupTempo;
	int mpPromptBackupDsvol;        // m_dsval と同じ -498..1
	int mpPromptBackupEq[20];
	int mpPromptBackupEqReverb;
	int mpPromptBackupEqChorus;
	int mpPromptBackupEqDelay;
	// --- プロンプト窓 位置・サイズ(末尾追記) ---
	int mpPromptHasPos;   // 1=保存座標あり
	int mpPromptX;
	int mpPromptY;
	int mpPromptW;
	int mpPromptH;

	// --- プロンプト履歴(末尾追記) 最大20件 ---
	int mpPromptHistCnt;              // 0..20
	TCHAR mpPromptHistText[20][2001]; // 新しい順 [0]=最新

	// --- ピアノロール検出パラメータ調整(100=組込み既定値, 25..400) ---
	int prTuneSilencePct;       // SILENCE_ABS
	int prTuneBandSilBassPct;   // BAND_SILENCE_BASS
	int prTuneBandSilMidPct;    // BAND_SILENCE_MID
	int prTuneBandSilTrePct;    // BAND_SILENCE_TRE
	int prTuneHoldBassPct;      // HOLD_ENV_BASS
	int prTuneHoldMidPct;       // HOLD_ENV_MID
	int prTuneHoldTrePct;       // HOLD_ENV_TRE
	int prTuneRetrigPct;        // RETRIGGER_RATIO
	int prTunePickBassPct;      // 低音帯ピック相対閾値
	int prTunePickLowMidPct;    // 低中域
	int prTunePickMelodyPct;    // メロディ帯 C4-C6
	int prTunePickTrePct;       // 高音域
	int prTuneHarmGhostPct;     // 倍音ゴースト margin
	int prTuneHarmRejectPct;    // 倍音棄却比率(0.78)
	int prTuneHarmProfPct;      // 音色プロファイル最低確信度
	int prTuneAbsFloorPct;      // 絶対ノイズフロア基準
	int prTuneOnsetDeltaPct;    // オンセット delta
	int prTunewindow;           // 1=検出パラメータ調整画面表示
	int prTunex;                // 左上X (-1=未設定)
	int prTuney;                // 左上Y (-1=未設定)

	// --- サブウィンドウ: メインウィンドウ位置ロック(1=追随) ---
	int eqMainLock;
	int pianorollMainLock;
	int analyzerMainLock;
	int playlistMainLock;
	int renderMainLock;
	int folderMainLock;
	int mpPromptMainLock;
	int prTuneMainLock;

	// --- KPIプラグイン チェック状態(末尾追記。旧 kpilist.dat から移行) ---
	// kpi一覧でチェックを外したプラグインは再生に使用しない。
	// 並び順が変わっても復元できるよう、プラグインのファイル名(ベース名)で突き合わせる。
	int   kpiChkCnt;            // 保存済みエントリ数(0=未保存→旧 kpilist.dat から移行)
	TCHAR kpiChkName[200][64];  // プラグインのファイル名(ベース名)
	int   kpiChkState[200];     // 1=使用する 0=使用しない

	// --- KPI一覧ウィンドウのサイズ・位置(末尾追記) ---
	// kpiWndW==0 のときは未保存とみなし、既定位置で表示する。
	int   kpiWndX;              // 左上 X(スクリーン座標)
	int   kpiWndY;              // 左上 Y(スクリーン座標)
	int   kpiWndW;              // 幅(0=未保存)
	int   kpiWndH;              // 高さ

	// --- 曲ごとのオーディオ/DSP パラメータ保存 有効フラグ(末尾追記) ---
	// 1=oggYSEDbgmu_AudioData.dat に曲ごとのパラメータを保存・復元する。0=無効。
	int   saveSongParams;

	// --- AudioData.dat キー形式の移行状態(末尾追記) ---
	// 0=未移行(path のみキーの旧形式の可能性あり)
	// 1=mode+ret2 付きキーへコンバート済み(または新規)
	int   audioDataVersion;

	// --- プロンプトバックアップ: 環境/かかり具合(末尾追記) ---
	int mpPromptBackupEqEnv;     // eqsoundenv 0..100
	int mpPromptBackupEqEffect;  // eqsoundeffect 内部0..100 (UIは0..200)

	// --- EQコード表示更新間隔(ms)。レンダリング画面で設定。16..500、既定25 ---
	int eqCodeMs;

	// --- 再生詳細 / ProAudio モジュール設定(末尾追記。旧.datは0初期化→起動時に妥当値へ正規化) ---
	int pro_gapless;       // 1=ギャップレス経路を使う
	int pro_xfade_ms;      // 撤去済み（常に0。互換のためフィールドのみ残す）
	int pro_rg_mode;       // 0=Off 1=Track 2=Album
	int pro_rg_target;     // 目標ラウドネス(負値 dB、既定-18)
	int pro_ms_width;      // Mid/Side 幅 0..200 (100=中立)
	int pro_ms_mono;       // 1=モノ互換チェック(Side=0)
	int pro_export_limit;  // 1=WAV書き出しでリミッター
	int pro_export_ceiling;// 天井% 50..100 (既定99)
	int pro_export_tp;     // 1=True Peak 判定
	int pro_corr_meter;    // 1=アナライザー／MPバナーの相関メーター

	// --- mp3/FLAC 書き出し(末尾追記) ---
	int tc_format;         // 0=mp3 1=FLAC
	int tc_mp3_kbps;       // 128..320
	int tc_flac_level;     // 0..8

	// --- メディアプレイヤー画面 UI(末尾追記。旧.datは0初期化) ---
	int mpLrcExpand;       // 1=歌詞パネル拡大
	int mpFindFilter;      // 1=検索語でリストを絞り込み(0=ジャンプのみ)
	int mpToolsOpen;       // 1=並べ替え/フォルダ追加の折りたたみ帯を開く
	int mpSortKey;         // 0=なし 1=名前 2=アーティスト 3=アルバム 4=時間
	int mpSortAsc;         // 1=昇順 0=降順
	int mpLibOpen;         // 1=ライブラリ(フォルダツリー+アルバム)ドロワーを開く
	int mpHistOpen;        // 1=再生履歴ドロワーを開く
	int mpSpeanaStyle;     // 0=バー 1=ミラー 2=波形(バナー右クリック切替)

	// --- アナライザー Pro 拡張(末尾追記。旧.datは0初期化→起動時に妥当値へ正規化) ---
	int analyzerwavemode;     // 0=スクロール波形 1=トリガー式オシロ
	int analyzerlowermode;    // 0=スペクトラム 1=スペクトログラム 2=位相スコープ
	int analyzerspecdiff;     // 1=スペクトラム差分表示
	int analyzerfreqzoom;     // 0=全帯域 1=低域 2=中域 3=高域
	int analyzermarkers[4];   // 固定周波数マーカー(Hz)。0=未使用

	// --- 書き出し時のタグ/ジャケット引き継ぎ(末尾追記。旧.datは1へ正規化) ---
	int wav_export_copy_tags; // 1=タグとジャケットを出力ファイルへコピー

	// --- プロンプト解析モード(末尾追記。0..19 = MpPromptAnalyzeMode) ---
	int mpPromptAnalyzeMode;

	// --- 簡易ピアノロール 表示拡張(末尾追記。旧.datは0初期化→起動時に既定へ正規化) ---
	int pianorollviewmode;   // 0=通常(2D) 1=簡易3D
	int pianorollkeyrange;   // 表示する鍵数 88 または 108(表示のみ。解析は常に108鍵)
	int pianorollnotename;   // 1=白鍵にノート名(C/D/E…)を表示
	int pianoroll3dyaw;      // 簡易3D 水平回転角(度×10, -1800..1800)
	int pianoroll3dpitch;    // 簡易3D 仰角(度×10, -850..850)

	// --- 動画画面(末尾追記。旧.datは0初期化=従来動作) ---
	int dougatopmost;        // 1=動画ウィンドウを常に手前に表示
	int dougaaspect;         // 1=アスペクト比を維持(レターボックス表示)

	// --- 簡易ピアノロール 3Dズーム(末尾追記) ---
	int pianoroll3dzoom;     // 簡易3D ズーム(×100, 35..400, 100=等倍)

	// --- プロンプト本文(拡張)。旧 mpPromptText[2001] は互換のため残し、長い本文はこちら ---
	TCHAR mpPromptTextLong[14001]; // MAX14000+終端。旧.datは0→Load時に短文側へフォールバック

	// --- プロンプトバックアップ: EQプリセット番号(末尾追記) ---
	int mpPromptBackupEqSoundEq; // eqsoundeq

	// --- KPI(mode==-3) 書き出し秒数(末尾追記。旧.datは0→起動時240=4分へ) ---
	int wav_export_kpi_sec;

	// --- 書き出しサンプリングレート(0=ソースのまま, 44100/48000/96000/192000) ---
	int wav_export_sample_rate;

	// --- プロンプト / コマンドロール 開閉・座標(末尾追記) ---
	int mpPromptwindow;     // 1=プロンプト表示
	int mpCmdRollwindow;    // 1=コマンドロール表示
	int mpCmdRollHasPos;    // 1=保存座標あり
	int mpCmdRollX;
	int mpCmdRollY;
	int mpCmdRollW;
	int mpCmdRollH;
	int mpCmdRollMainLock;  // 1=メインに追随
	int mpCmdRollPxPerSec10; // 拡大率: m_pxPerSec*10 (既定120=12px/s)
	int wav_export_apply_prompt; // 1=書き出し時にプロンプト実行を適用
	// --- 複数書き出しクロスフェード(末尾追記。旧.datは0/5へ) ---
	int wav_export_xfade;     // 1=複数選択時に1ファイルへクロスフェード結合
	int wav_export_xfade_sec; // クロスフェード秒(既定5)
	// --- 同時ミックス書き出し(末尾追記) ---
	int wav_export_mix;       // 1=同時ミックス＋クロスフェード補充
	int wav_export_mix_n;     // 同時曲数(2..)

	// --- WAV保存時マイクミックス(末尾追記。旧.datは0初期化) ---
	int mic_mix;              // 1=WAVへ保存ON時にマイクをミックス
	int mic_mix_level;        // 0..200 (100=等倍)
	TCHAR mic_device[256];    // WASAPI キャプチャ端末 ID(空=既定)
	int mic_device_cur;       // CRender マイクコンボ選択(0=既定)

	// --- デバイス録音 / 画面キャプチャ(末尾追記。旧.datは0初期化) ---
	TCHAR loop_device[256];   // ループバック録音の再生端末 ID(空=既定)
	int loop_device_cur;      // 録音UIコンボ選択
	TCHAR cap_save_dir[1024]; // 画面キャプチャ保存先(空=曲と同じフォルダ等)
	int record_format;        // 0=WAV 1=mp3 2=FLAC
	int record_mp3_kbps;      // 128..320
	int record_mix_mic;       // 1=録音時にマイクもミックス
	TCHAR record_last_path[1024]; // 直近の出力パス
	int record_flac_level;    // 0..8
	int cap_with_audio;       // 1=画面キャプチャにシステム音
	int cap_with_mic;         // 1=画面キャプチャにマイク
	int cap_fps;              // 10..120 (録画＆プレビュー間隔)
	TCHAR cap_last_path[1024];

	// --- K-Lite Codec Pack 未導入の誘導(末尾追記。旧.datは0) ---
	// 0=未回答(未導入なら起動時に聞く) 1=いいえ済み(今後出さない)
	int kliteAskSkip;
	// 画面キャプチャモード(末尾追記)
	int cap_mode;          // 0=プライマリ 1=全モニタ 2=ウィンドウ合成 3=特定モニタ
	int cap_canvas_preset; // 0=自動 1=1280x720 2=1920x1080 3=1600x900 4=3840x2160
	int cap_canvas_w;      // 予備(将来カスタム)
	int cap_canvas_h;
	int cap_include_mp;    // 1=MP画面(慣らし中の曲)をキャプチャに載せる
	// 画面キャプチャ: 特定モニタ (末尾追記)
	int cap_monitor_idx;   // EnumDisplayMonitors 0始まり。mode=3(特定モニタ)で使用
	int cap_effect;        // SC_FX_* 画面キャプチャエフェクト（先頭スロット同期）
	// 画面キャプチャ: エフェクトチェーン (末尾追記)
	int cap_fx_n;          // 0..8
	int cap_fx0;
	int cap_fx1;
	int cap_fx2;
	int cap_fx3;
	int cap_fx4;
	int cap_fx5;
	int cap_fx6;
	int cap_fx7;

	// --- メディアプレイヤー シーク: ループつまみロック(末尾追記。旧.datは0=ロック) ---
	// 0=ロック(loop1/2つまみ固定) 1=解除(loop1/2つまみ可動)。A-Bつまみは常に可動。
	int mpSeekLoopUnlock;

	// --- シーク波形オーバービュー(末尾追記。旧.datは1へ正規化=表示) ---
	int mpSeekWave; // 1=波形表示 0=非表示

	// --- 練習用フレーズ幅(秒)。±N で A-B を張る(末尾追記。旧.datは4) ---
	int mpPhraseSec;

	// --- スリープタイマー(分)。0=Off。末尾追記 ---
	int mpSleepMin;

	// --- 再生履歴の時刻(当日0時からの分。不明は-1)。末尾追記 ---
	int mpHistTod[8];

	// --- シーク: 拍グリッド / 書き出しクロスフェード帯プレビュー(末尾追記) ---
	int mpBeatGrid;       // 1=表示
	int mpXfadePreview;   // 1=書き出しxfade帯をシークに表示

	// --- 附属機能(末尾追記。旧.datは offsetof で初期化) ---
	int mpLoopbackScore;     // 1=PC音(ループバック)をピアノロールへ(再生フィード抑制)
	int mpChordPanel;        // 1=ピアノロールにコード進行パネル
	int deskLrcOn;           // 1=歌詞ウィンドウ表示
	int deskLrcX, deskLrcY, deskLrcW, deskLrcH;
	int deskLrcAlpha;        // 40..255
	int mpVocalCenter;       // ボーカル: 100=中立, 0=キャンセル寄り, 200=強調(Midゲイン%)
	int mpMirrorOut;         // 1=二次デバイスへミラー出力
	int mpMirrorVol;         // 0..100 二次音量
	TCHAR mpMirrorDevice[256];
	int mpRemoteOn;          // 1=localhost 操作HTTP
	int mpRemotePort;        // 例 8765
	int mpAlarmHour;         // -1=Off, 0..23
	int mpAlarmMin;          // 0..59
	int mpSsVizOn;           // 1=起動時にSS風ビジュアライザを開かない(実行時フラグは別)
	int mpDetectedBpm;       // 直近BPM推定(0=未)＝自動反映した主候補
	int mpDjPadwindow;       // 1=DJパッド表示
	int mpNormTargetLufs;    // バッチ正規化目標(負: 例 -14)
	int mpKeyEqSuggest;      // 1=キー検出からEQプリセット自動提案を許可
	int mpJacketRemOverlay;  // 1=ミニジャケに残時間リング+タイム表示
	int mpBpmCand[3];        // 計測時の候補(主+4:3/3:2等)。0=空き

	// --- 画面キャプチャ FX 強度 S1..S8（スロットごと。0..8、既定4。末尾追記）---
	BYTE cap_fx_str[8][8];

	// --- 画面キャプチャ 配線プリセット16（名前自由。末尾追記）---
	TCHAR cap_fx_pre_name[16][48];
	int cap_fx_pre_n[16];
	int cap_fx_pre_fx[16][8];
	BYTE cap_fx_pre_str[16][8][8];
	int cap_fx_pre_sel; // 0..15

	// --- コンテキストメニュー(CCustomPopupMenu)用フォント。末尾追記 ---
	TCHAR popupMenuFace[32]; // LOGFONT face（空=DEFAULT_GUI）
	int popupMenuPoint;      // 8..24（既定 9）
	int popupMenuBold;       // 0/1
	int popupMenuItalic;     // 0/1

	// --- 歌詞ウィンドウフォント。末尾追記（旧.datは offsetof で初期化）---
	int deskLrcFontAuto; // 1=ウィンドウ高さで表示行数フィット
	int deskLrcFontPt;   // CreatePointFont 用ポイント10倍（80..480）。手動時に使用／自動時は実効値を同期
	int deskLrcLines;    // 自動フィット時の目標表示行数（3..20、既定10）
};
extern save savedata;
/* コード間隔(ms)。16..500。旧.dat や未設定は 25。 */
inline int EqCodeIntervalMs()
{
	int ms = savedata.eqCodeMs;
	if (ms < 16 || ms > 500) ms = 25;
	return ms;
}
/* lang: 0=ja 1=en 2=fr 3=it 4=es 5=ko 6=zh 7=ar 8=ru 9=de 10=pt 11=nl 12=pl 13=tr */
const wchar_t* LangPick14(
	const wchar_t* s0, const wchar_t* s1, const wchar_t* s2, const wchar_t* s3,
	const wchar_t* s4, const wchar_t* s5, const wchar_t* s6, const wchar_t* s7,
	const wchar_t* s8, const wchar_t* s9, const wchar_t* s10, const wchar_t* s11,
	const wchar_t* s12, const wchar_t* s13);
#define LL14(ja,en,fr,it,es,ko,zh,ar,ru,de,pt,nl,pl,tr) \
	LangPick14(ja,en,fr,it,es,ko,zh,ar,ru,de,pt,nl,pl,tr)
// LL2は使用しないこと。LL14のみ。翻訳は手抜きしない。
#define LL2(ja, en) LL14(ja, en, en, en, en, en, en, en, en, en, en, en, en, en)
extern int loop1;
extern int loop1_2;
char *b64_decode(char *s, int size,int &len);

#define LL L

int b64_ctoi(char c);

// サブウィンドウを表示中メイン(COggDlg/CMediaPlayerDlg)の移動に追随させる
// （UI 表記は「メインに追従」）
CWnd* CCC_GetActiveMainWindow();
void CCC_MainLockSetup(CWnd* pDlg, int* pSavedLockFlag, BOOL bOverlayPaint = FALSE);
void CCC_MainLockUnregister(HWND hWnd);
void CCC_MainLockOnMainMoving(LPRECT pMainRect);
void CCC_MainLockRefreshOffsets();
void CCC_MainLockRefreshOffsetsFor(CWnd* pMain);
void CCC_MainLockOnChildMoving(CWnd* pDlg, LPRECT pRect);
void CCC_MainLockPaintClient(CDC& dc, HWND hDlg);
BOOL CCC_MainLockOverlayHitTest(HWND hDlg, CPoint ptClient);
void CCC_MainLockOverlayToggle(HWND hDlg);
BOOL CCC_MainLockPreferQuickPresent();
void CCC_MainLockBringToFront(HWND hDlg);
int CCC_MainLockGetReserveWidth(HWND hDlg);
void CCC_MainLockGetOverlayRect(HWND hDlg, CRect& rc);
void CCC_MainLockSetHeaderRow(HWND hDlg, int top, int height);
void CCC_MainLockClearHeaderRow(HWND hDlg);
void CCC_InvalidateRectMinusOverlay(HWND hDlg, const CRect& area);
int  CCC_GetCustomCaptionHeight(HWND hDlg);
// キャプション帯のアクリルは savedata.aero と完全独立。インストール済みなら常に TRUE(1)
BOOL CCC_AcrylicCaption(HWND hWnd);
void CCC_CaptionPaint(CDC& dc, HWND hDlg);
void CCC_CaptionLayout(HWND hDlg);
void CCC_CaptionUnregister(HWND hDlg);
// キャプション隣の「?」を、実在する CAP ボタンと「メインに追随」の左へ置く（欠けるボタン分の空きを作らない）
void CCC_CaptionPlaceHelpBtn(HWND hDlg, CWnd* pHelp);
// オーナー付きヘルプを前面へ。TOPMOST は使わない（他UIがメインになったら下に回る）
void CCC_PresentOwnedHelp(CWnd* help, CWnd* owner);
// Per-Monitor DPI 変更時にキャプション帯高さを再計算してレイアウト
void CCC_CaptionRefreshDpi(HWND hDlg);

#define cmnh() 	CBrush m_brDlg; \
afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct); \
afx_msg void OnMouseMove(UINT nFlags, CPoint point); \
afx_msg void OnLButtonDown(UINT nFlags, CPoint point); \
afx_msg void OnLButtonUp(UINT nFlags, CPoint point); \
virtual BOOL DestroyWindow(); \
afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor); \
afx_msg void OnTimer(UINT_PTR nIDEvent); \
afx_msg BOOL OnNcActivate(BOOL bActive); \
afx_msg void OnMoving(UINT fwSide, LPRECT pRect); \
int m_bMoving1; \
CPoint m_pointOld1;
// ogg.h / oggDlg.h / PlayList.h は PCH に入れない(ピアノロール等の変更で全TU再ビルドになるため)。
// 必要な .cpp 側で個別 include すること。
class CImageBase;
#define cmn(xxx) 	ON_WM_CREATE()  \
ON_WM_MOUSEMOVE()  \
ON_WM_LBUTTONDOWN() \
ON_WM_MOVING() \
ON_WM_LBUTTONUP() \
ON_WM_CTLCOLOR()  \
ON_WM_TIMER() \
ON_WM_NCACTIVATE() \
END_MESSAGE_MAP() \
extern save savedata; \
extern CImageBase* Games; \
extern int gameon; \
extern int ip1; \
int xxx::OnCreate(LPCREATESTRUCT lpCreateStruct) \
{ \
	if (CCustomBlurDialogBase::OnCreate(lpCreateStruct) == -1) \
		return -1; \
	if (savedata.aero == 1) { \
		ModifyStyleEx(0, WS_EX_LAYERED); \
		SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY); \
		m_brDlg.CreateSolidBrush(RGB(255, 0, 0)); \
	} \
Games = NULL; \
	SetTimer(500, 100, NULL); \
    m_bMoving1 = 0; \
	return 0; \
} \
void xxx::OnMoving(UINT fwSide, LPRECT pRect) \
{ \
	CCustomBlurDialogBase::OnMoving(fwSide, pRect); \
	CRect r; \
	GetWindowRect(&r); \
	if (Games) \
		Games->MoveWindow(&r); \
} \
void xxx::OnLButtonDown(UINT nFlags, CPoint point) \
{ \
	m_bMoving1 = TRUE; \
	SetCapture(); \
	m_pointOld1 = point; \
	CCustomBlurDialogBase::OnLButtonDown(nFlags, point); \
} \
void xxx::OnLButtonUp(UINT nFlags, CPoint point) \
{ \
	if (m_bMoving1 == TRUE) { \
		m_bMoving1 = FALSE; \
		::ReleaseCapture(); \
	} \
	CCustomBlurDialogBase::OnLButtonUp(nFlags, point); \
} \
void xxx::OnMouseMove(UINT nFlags, CPoint point) \
{ \
	if (m_bMoving1 == TRUE) { \
		CRect rect; \
		GetWindowRect(&rect); \
		rect.left += (point.x - m_pointOld1.x); \
		rect.right += (point.x - m_pointOld1.x); \
		rect.top += (point.y - m_pointOld1.y); \
		rect.bottom += (point.y - m_pointOld1.y); \
		SetWindowPos(NULL, rect.left, rect.top, \
			rect.right - rect.left, rect.bottom - rect.top, \
			SWP_NOOWNERZORDER); \
		if (Games) \
			Games->MoveWindow(&rect); \
	} \
	CCustomBlurDialogBase::OnMouseMove(nFlags, point); \
} \
HBRUSH xxx::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) \
{ \
	HBRUSH hbr = CCustomBlurDialogBase::OnCtlColor(pDC, pWnd, nCtlColor); \
	if (savedata.aero == 1) { \
		if (nCtlColor == CTLCOLOR_DLG) \
		{ \
			return m_brDlg; \
		} \
		if (nCtlColor == CTLCOLOR_STATIC) \
		{ \
			SetBkMode(pDC->m_hDC, TRANSPARENT); \
			return m_brDlg; \
		} \
	} \
	return hbr; \
} \
void xxx::OnTimer(UINT_PTR nIDEvent) \
{ \
    if(nIDEvent==500 && savedata.aero){ \
	KillTimer(500); \
    if(ip1 != 0) return; \
    if(Games == NULL){ \
	Games = new CImageBase; \
	Games->oya = this; \
	Games->Create(this); \
     } \
	CRect r; \
	GetWindowRect(&r); \
	if (Games) \
		Games->MoveWindow(&r); \
	::SetWindowPos(m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); \
	if (Games) \
		::SetWindowPos(Games->m_hWnd, m_hWnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); \
    ip1 = 3; \
    SetTimer(501,10,NULL); \
    } \
    if(nIDEvent==501 && savedata.aero){ \
        ip1--; \
        if(ip1 <= 0){ ip1 = 0; KillTimer(501); }\
    } \
	CCustomBlurDialogBase::OnTimer(nIDEvent); \
} \
BOOL xxx::DestroyWindow() \
{ \
	if (Games){ \
		delete Games; \
    } \
    Games = NULL; \
	return CCustomBlurDialogBase::DestroyWindow(); \
} \
BOOL xxx::OnNcActivate(BOOL bActive) \
{ \
	SetTimer(500, 30, NULL); \
	return CCustomBlurDialogBase::OnNcActivate(bActive); \
} 




// Game dialog: /utf-8 char[] track titles, or existing TCHAR/L"..." tables
inline CString GameTrackTitle(const char* utf8)
{
	if (!utf8 || !*utf8)
		return CString();
	const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	if (wlen <= 0)
		return CString();
	CString s;
	LPWSTR buf = s.GetBuffer(wlen - 1);
	::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buf, wlen);
	s.ReleaseBuffer();
	return s;
}

inline CString GameTrackTitle(LPCTSTR wide)
{
	return CString(wide);
}

#ifdef _UNICODE
#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ は前行の直前に追加の宣言を挿入します。
