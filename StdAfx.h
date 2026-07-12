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

	int mpcol[5];      // メディアプレイヤー画面リストの各列幅(0=未設定=既定値を使用)

	// WAV出力オプション(末尾追記)
	int wav_export_fade;           // 1=フェードアウト有効
	int wav_export_fade_sec;       // フェード秒数(既定15)
	int wav_export_trim_lead;      // 1=先頭無音カット有効
	int wav_export_trim_keep_sec;  // 先頭に残す無音秒数(既定1)

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
	int analyzerspecstyle;  // 0=塗+線(Ozone風) 1=線のみ 2=バー
	int analyzerpeakhold;   // 1=ピークホールド ON
	int analyzereqoverlay;  // 1=EQ帯域/ゲイン曲線オーバーレイ
};
extern save savedata;
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
	if (Games) \
		::SetWindowPos(Games->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); \
	::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); \
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
