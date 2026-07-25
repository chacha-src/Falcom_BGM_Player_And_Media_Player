// CMediaPlayerDlg.cpp : メディアプレイヤーモード画面(張りぼて)とモード選択ダイアログ
//
// 実体は COggDlg(og->) と CPlayList(pl->)。ここは表示と操作の取り次ぎだけを行う。
// メディアプレイヤーモード中は og / pl のウィンドウを非表示にして裏で生かしておく。
//
#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "CEqualizer.h"
#include "CPianoRoll.h"
#include "CAnalyzerDlg.h"
#include "CMediaPlayerDlg.h"
#include "SongParams.h"
#include "CImageBase.h"
#include "Mp3Image.h"
#include "AudioUpscaler.h"
#include "CMpPlaylistIO.h"
#include "CMpM3uImportDlg.h"
#include "CPromptDlg.h"
#include <direct.h>
#include <shobjidl.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

/////////////////////////////////////////////////////////////////////////////
// 外部参照(既存のグローバル/メイン画面・プレイリストの状態を流用)
extern COggDlg* og;
extern CPlayList* pl;
extern BOOL plw;
extern int plcnt;
extern int gameon;
extern int killw1;
extern save savedata;
extern TCHAR karento2[1024];
extern CString filen, fnn, tagname, tagfile, tagalbum;
extern CString stitle;   // ゲーム内タイトル等(oggDlg.cpp)
extern CString tagtrack; // 曲番号(トラック番号。oggDlg.cpp)
extern int wavbit_sample_Hz; // サンプルレート Hz (oggDlg.cpp)
extern int wavchannel;       // チャンネル数 (oggDlg.cpp)
extern int wavsam_depth;     // ビット深度 (oggDlg.cpp)
extern int mode;         // 再生モード(タイトル解決に使用, oggDlg.cpp)
extern int playy;   // 再生中フラグ(oggDlg.cpp)
extern int plf;          // 再生中(1=再生中。oggDlg.cpp)
extern int ps;           // 一時停止中(1=再開表示。oggDlg.cpp)
extern ITaskbarList3* ptl;   // タスクバー進捗(oggDlg.cpp で初期化)
extern void MpPushPlayHistory(LPCTSTR path, LPCTSTR displayName);
extern void MpTaskbarReplay();
extern void MpTaskbarNextTrack();
extern void MpTaskbarPrevTrack();
extern CDC dc;   // COggDlg のオフスクリーン合成面(スペアナ+ジャケ+時間)を流用
extern void ShowOggAboutDialog(CWnd* pParent);   // バージョン情報ダイアログ(oggDlg.cpp)

// og 側のオフスクリーン面のソース寸法(oggDlg.cpp の OnPaint と一致させる: srcW=MDCP+5)
static const int MP_SRCW = (88 * 2 + 175) * 4 + 5; // = 1409 (og の srcW と一致)
static const int MP_SRCH = (81 + 16) * 4;          // = 388

#pragma comment(lib, "version.lib")

// 実行ファイルのバージョンリソースからキャプション末尾「 Ver 0.9a Rel.xxxx.xx.xx」を作る。
// Ver は FILEVERSION(0,9,1,x)の第3要素を英字化(1='a')、Rel. は FileDescription から取得し、
// 見つからなければ LegalCopyright の "Copyright (C) 日付" から組み立てる。
static CString MpBuildVersionCaptionSuffix()
{
	TCHAR exePath[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, exePath, MAX_PATH);

	DWORD handle = 0;
	const DWORD size = GetFileVersionInfoSize(exePath, &handle);
	if (size == 0) return L"";

	BYTE* data = new BYTE[size];
	if (!GetFileVersionInfo(exePath, 0, size, data)) {
		delete[] data;
		return L"";
	}

	CString ver;
	VS_FIXEDFILEINFO* ffi = NULL;
	UINT len = 0;
	if (VerQueryValue(data, _T("\\"), (LPVOID*)&ffi, &len) && ffi && len >= sizeof(VS_FIXEDFILEINFO)) {
		const int major = HIWORD(ffi->dwFileVersionMS);
		const int minor = LOWORD(ffi->dwFileVersionMS);
		const int rev = HIWORD(ffi->dwFileVersionLS);
		if (rev >= 1 && rev <= 26)
			ver.Format(L" Ver %d.%d%c", major, minor, (wchar_t)(L'a' + rev - 1));
		else
			ver.Format(L" Ver %d.%d", major, minor);
	}

	CString rel;
	static const LPCTSTR strNames[] = { _T("FileDescription"), _T("LegalCopyright") };
	for (int i = 0; i < _countof(strNames) && rel.IsEmpty(); ++i) {
		CString query;
		query.Format(_T("\\StringFileInfo\\041104b0\\%s"), strNames[i]);
		LPVOID p = NULL;
		UINT l = 0;
		if (VerQueryValue(data, query, &p, &l) && p && l) {
			CString s((LPCTSTR)p);
			int pos = s.Find(L"Rel.");
			if (pos >= 0) {
				rel = L" " + s.Mid(pos);
				rel.TrimRight();
			}
			else if ((pos = s.Find(L"(C)")) >= 0) {
				CString d = s.Mid(pos + 3);
				d.Trim();
				if (!d.IsEmpty())
					rel = L" Rel." + d;
			}
		}
	}

	delete[] data;
	return ver + rel;
}

CMediaPlayerDlg* mp = NULL;
int g_mpBannerHover = 0;   // バナー(GDI)上にマウスがあるか(og の timerp がジャケットアニメに使用)
// 幅拡張時にジャケットを左余白へ分離表示しているか。1 の間は og の timerp が
// バナー内蔵ジャケット(半透明・タイトル背後)を描かない(二重表示・かぶり防止)。
int g_mpSideJacket = 0;

/////////////////////////////////////////////////////////////////////////////
// CModeSelectDlg
/////////////////////////////////////////////////////////////////////////////
IMPLEMENT_DYNAMIC(CModeSelectDlg, CCustomBlurDialogBase)

CModeSelectDlg::CModeSelectDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CModeSelectDlg::IDD, pParent)
{
}

CModeSelectDlg::~CModeSelectDlg()
{
}

void CModeSelectDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MODE_FALCOM, m_btnFalcom);
	DDX_Control(pDX, IDC_MODE_MEDIA, m_btnMedia);
	DDX_Control(pDX, IDC_MODE_ASK, m_ask);
}

BEGIN_MESSAGE_MAP(CModeSelectDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_MODE_FALCOM, &CModeSelectDlg::OnFalcom)
	ON_BN_CLICKED(IDC_MODE_MEDIA, &CModeSelectDlg::OnMedia)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CModeSelectDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"起動モードの選択", L"Select startup mode", L"Selection du mode de demarrage", L"Selezione modalita di avvio", L"Seleccion del modo de inicio", L"시작 모드 선택", L"选择启动模式", L"اختيار وضع البدء", L"Выбор режима запуска", L"Startmodus auswahlen", L"Selecionar modo de inicializacao", L"Opstartmodus selecteren", L"Tryb uruchamiania", L"Başlangıç modunu seç"));
	SetDlgItemText(IDC_STATIC, LL14(L"どちらの画面で起動しますか？", L"Which screen to start with?", L"Quel ecran au demarrage ?", L"Quale schermata avviare?", L"¿Con qué pantalla iniciar?", L"어느 화면으로 시작할까요?", L"以哪个画面启动？", L"بأي شاشة تبدأ؟", L"С какого экрана начать?", L"Mit welchem Bildschirm starten?", L"Qual tela iniciar?", L"Met welk scherm starten?", L"Którym ekranem uruchomić?", L"Hangi ekranla başlasın?"));
	m_btnFalcom.SetWindowText(LL14(L"ファルコムbgm特化型画面", L"Falcom BGM dedicated screen", L"Ecran dedie BGM Falcom", L"Schermata BGM Falcom", L"Pantalla dedicada BGM Falcom", L"팔콤 BGM 전용 화면", L"Falcom BGM 专用画面", L"شاشة Falcom BGM المخصصة", L"Экран Falcom BGM", L"Falcom-BGM-Bildschirm", L"Tela dedicada Falcom BGM", L"Falcom BGM-scherm", L"Ekran Falcom BGM", L"Falcom BGM ekranı"));
	m_btnMedia.SetWindowText(LL14(L"メディアプレイヤー画面", L"Media player screen", L"Ecran lecteur multimedia", L"Schermata lettore multimediale", L"Pantalla reproductor multimedia", L"미디어 플레이어 화면", L"媒体播放器画面", L"شاشة مشغل الوسائط", L"Экран медиаплеера", L"Media-Player-Bildschirm", L"Tela do reprodutor de midia", L"Mediaspeler-scherm", L"Ekran odtwarzacza multimediow", L"Medya oynatıcı ekranı"));
	m_ask.SetWindowText(LL14(L"次回も起動時に確認する", L"Ask again next startup", L"Demander au prochain demarrage", L"Chiedi al prossimo avvio", L"Preguntar en el proximo inicio", L"다음에도 시작 시 확인", L"下次启动时也询问", L"اسأل في المرة القادمة", L"Спрашивать при следующем запуске", L"Beim nachsten Start fragen", L"Perguntar no proximo inicio", L"Volgende keer opnieuw vragen", L"Zapytaj przy następnym starcie", L"Sonraki açılışta tekrar sor"));
	m_ask.SetCheck(savedata.startupAsk ? 1 : 0);

	// 少し可愛い系: ボタンを大きめのフォントに
	m_btnFalcom.SetGradation(RGB(255, 210, 230), RGB(255, 170, 205), 0, TRUE);
	m_btnMedia.SetGradation(RGB(205, 230, 255), RGB(170, 205, 255), 0, TRUE);

	return TRUE;
}

void CModeSelectDlg::OnFalcom()
{
	savedata.playerMode = 0;
	savedata.startupAsk = m_ask.GetCheck() ? 1 : 0;
	EndDialog(IDOK);
}

void CModeSelectDlg::OnMedia()
{
	savedata.playerMode = 1;
	savedata.startupAsk = m_ask.GetCheck() ? 1 : 0;
	EndDialog(IDOK);
}

HBRUSH CModeSelectDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CCustomBlurDialogBase::OnCtlColor(pDC, pWnd, nCtlColor);
	if (savedata.aero != 1) {
		if (m_brDlg.GetSafeHandle() == NULL)
			m_brDlg.CreateSolidBrush(COLOR_DIALOG_BG);
		if (nCtlColor == CTLCOLOR_DLG)
			return m_brDlg;
		if (nCtlColor == CTLCOLOR_STATIC) {
			pDC->SetBkMode(TRANSPARENT);
			return m_brDlg;
		}
	}
	return hbr;
}

/////////////////////////////////////////////////////////////////////////////
// CMediaPlayerDlg
/////////////////////////////////////////////////////////////////////////////
IMPLEMENT_DYNAMIC(CMediaPlayerDlg, CCustomBlurDialogExBase)

namespace {
const UINT_PTR kTimerListHdrDrag = 7;
const UINT_PTR kMpListHdrSubclassId = 4207;
}

LRESULT CALLBACK CMediaPlayerDlg::ListHeaderNotifySubclassProc(
	HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	CMediaPlayerDlg* pDlg = reinterpret_cast<CMediaPlayerDlg*>(dwRefData);
	if (pDlg && uMsg == WM_NOTIFY) {
		NMHDR* pN = reinterpret_cast<NMHDR*>(lParam);
		const HWND hHdr = ListView_GetHeader(hWnd);
		if (hHdr && pN && pN->hwndFrom == hHdr) {
			switch (pN->code) {
			case HDN_BEGINTRACKA:
			case HDN_BEGINTRACKW:
			case HDN_TRACKA:
			case HDN_TRACKW:
			case HDN_ENDTRACKA:
			case HDN_ENDTRACKW: {
				LRESULT lr = 0;
				pDlg->OnPlaylistHeaderNotify(pN, &lr);
				break;
			}
			}
		}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

CMediaPlayerDlg::CMediaPlayerDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CMediaPlayerDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_lastCount = -1;
	m_lastPlcnt = -2;
	m_lastScroll = -2;
	m_lastComboCount = -1;
	m_plselDropExtent = 0;
	m_plselLayoutDpi = 0.f;
	m_lastMs2 = 0;
	m_seekDragging = 0;
	m_lastPlayIcon = -999;
	m_savedEqVisible = 0;
	m_savedPianoVisible = 0;
	m_savedAnalyzerVisible = 0;
	m_inSizeMove = false;
	m_uiReady = false;
	m_dragging = 0;
	m_dragSrc = -1;
	m_hDragImage = NULL;
	hD2 = 1.0f;
	m_bannerRect.SetRectEmpty();
	m_jacketRect.SetRectEmpty();
	m_infoPanelRect.SetRectEmpty();
	m_bannerCacheW = 0;
	m_bannerCacheH = 0;
	for (int i = 0; i < kInfoRows; i++) {
		m_isc[i] = 0; m_iscW[i] = 0;
		m_iscRowOldBmp[i] = nullptr;
		m_iscRowCacheW[i] = 0;
		m_iscRowCacheH[i] = 0;
		m_iscRowCacheClr[i] = 0;
		m_iscRowCacheBg[i] = 0;
	}
	m_iscActive    = false;
	m_iscScrollPosted = 0;
	m_lastInfoPanelW = 0;
	m_infoMemOldBmp = nullptr;
	m_infoMemW = m_infoMemH = 0;
	m_listHdrDragCol = -1;
	m_lastToggleSupe = -1;
	m_lastToggleSt = -1;
	m_lastToggleEq = -1;
	m_lastTogglePiano = -1;
	m_lastToggleAnalyzer = -1;
	m_dsvolSlW = 0;
	m_mpBtnShort = -1;
	m_mpPromptShort = -1;
	for (int i = 0; i < 6; ++i)
		m_mpChkShort[i] = -1;
}

CMediaPlayerDlg::~CMediaPlayerDlg()
{
}

// DDX_Control は GetDlgItem 失敗時や二重 Subclass で CInvalidArgException になる。
// RC 未反映・IDずれ・自動 Subclass 済みでも Create を落とさない。
static void MpDdxControl(CDataExchange* pDX, int nIDC, CWnd& wnd)
{
	if (!pDX || !pDX->m_pDlgWnd) return;
	// 既に Subclass 済みなら二度目の DDX_Control は CInvalidArgException
	if (wnd.GetSafeHwnd()) return;
	HWND hDlg = pDX->m_pDlgWnd->GetSafeHwnd();
	if (!hDlg) return;
	HWND hCtrl = ::GetDlgItem(hDlg, nIDC);
	if (!hCtrl) return;
	// 別 CWnd が既に Subclass 済みなら DDX_Control は投げない
	if (CWnd::FromHandlePermanent(hCtrl)) return;
	try {
		DDX_Control(pDX, nIDC, wnd);
	}
	catch (CException* e) {
		e->Delete();
	}
}

void CMediaPlayerDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	MpDdxControl(pDX, IDC_MP_TITLE, m_title);
	MpDdxControl(pDX, IDC_MP_ARTIST, m_artist);
	MpDdxControl(pDX, IDC_MP_ALBUM, m_album);
	MpDdxControl(pDX, IDC_MP_LRC, m_lrc);
	MpDdxControl(pDX, IDC_MP_LRC2, m_lrc2);
	MpDdxControl(pDX, IDC_MP_LRC3, m_lrc3);
	MpDdxControl(pDX, IDC_MP_LRC4, m_lrc4);
	MpDdxControl(pDX, IDC_MP_LRC5, m_lrc5);
	MpDdxControl(pDX, IDC_MP_OS, m_os);
	MpDdxControl(pDX, IDC_MP_CPU, m_cpu);
	MpDdxControl(pDX, IDC_MP_OS3, m_os3);
	MpDdxControl(pDX, IDC_MP_TIME, m_time);
	MpDdxControl(pDX, IDC_MP_VOLVAL, m_volval);
	MpDdxControl(pDX, IDC_MP_VOL_L, m_vollabel);
	MpDdxControl(pDX, IDC_MP_SEEK, m_seek);
	MpDdxControl(pDX, IDC_MP_VOL, m_vol);
	MpDdxControl(pDX, IDC_MP_PREV, m_prev);
	MpDdxControl(pDX, IDC_MP_PLAY, m_play);
	MpDdxControl(pDX, IDC_MP_PAUSE, m_pause);
	MpDdxControl(pDX, IDC_MP_STOP, m_stop);
	MpDdxControl(pDX, IDC_MP_NEXT, m_next);
	MpDdxControl(pDX, IDC_MP_EQ, m_eq);
	MpDdxControl(pDX, IDC_MP_PIANO, m_piano);
	// IDC_MP_ANALYZER は RC に置かず動的生成(テンプレート差分と DDX 欠落例外を避ける)
	MpDdxControl(pDX, IDC_MP_SWITCHMODE, m_switch);
	MpDdxControl(pDX, IDC_MP_SETTINGS, m_settings);
	MpDdxControl(pDX, IDC_MP_EXIT, m_exit);
	MpDdxControl(pDX, IDC_MP_JACK, m_jacket);
	MpDdxControl(pDX, IDC_MP_FADEOUT, m_fadeout);
	MpDdxControl(pDX, IDC_MP_FOLDER, m_folder);
	MpDdxControl(pDX, IDC_MP_DSVOL, m_dsvol);
	MpDdxControl(pDX, IDC_MP_DSVOL_L, m_dsvolL);
	MpDdxControl(pDX, IDC_MP_KVOL, m_kvol);
	MpDdxControl(pDX, IDC_MP_KVOL_L, m_kvolL);
	MpDdxControl(pDX, IDC_MP_TEMPO, m_tempo);
	MpDdxControl(pDX, IDC_MP_TEMPO_L, m_tempoL);
	MpDdxControl(pDX, IDC_MP_PITCH, m_pitch);
	MpDdxControl(pDX, IDC_MP_PITCH_L, m_pitchL);
	MpDdxControl(pDX, IDC_MP_RENZOKU, m_renzoku);
	MpDdxControl(pDX, IDC_MP_LOOP, m_loop);
	MpDdxControl(pDX, IDC_MP_RANDOM, m_random);
	MpDdxControl(pDX, IDC_MP_PLSEL, m_plsel);
	MpDdxControl(pDX, IDC_MP_PLRENAME, m_plrename);
	MpDdxControl(pDX, IDC_MP_PLDELETE, m_pldelete);
	MpDdxControl(pDX, IDC_MP_LSUP, m_lsup);
	MpDdxControl(pDX, IDC_MP_UP, m_up);
	MpDdxControl(pDX, IDC_MP_DOWN, m_down);
	MpDdxControl(pDX, IDC_MP_LSDOWN, m_lsdown);
	MpDdxControl(pDX, IDC_MP_ITEMDEL, m_itemdel);
	MpDdxControl(pDX, IDC_MP_M3U_EXPORT, m_m3uExport);
	MpDdxControl(pDX, IDC_MP_M3U_IMPORT, m_m3uImport);
	MpDdxControl(pDX, IDC_MP_FIND, m_find);
	MpDdxControl(pDX, IDC_MP_FINDUP, m_findup);
	MpDdxControl(pDX, IDC_MP_FINDDOWN, m_finddown);
	MpDdxControl(pDX, IDC_MP_SUPE, m_supe);
	MpDdxControl(pDX, IDC_MP_ST, m_st);
	MpDdxControl(pDX, IDC_MP_TIP, m_tip);
	MpDdxControl(pDX, IDC_MP_MINI, m_mini);
	MpDdxControl(pDX, IDC_MP_SAVEMP3, m_savemp3);
	MpDdxControl(pDX, IDC_MP_SAVEDS, m_saveds);
	MpDdxControl(pDX, IDC_MP_SAVEWAV, m_savewav);
	MpDdxControl(pDX, IDC_MP_SAVEPARAM, m_saveparam);
	MpDdxControl(pDX, IDC_MP_RESETDATA, m_resetdata);
	MpDdxControl(pDX, IDC_MP_KAISUU_L, m_kaisuuL);
	MpDdxControl(pDX, IDC_MP_KAISUU, m_kaisuu);
	MpDdxControl(pDX, IDC_MP_GRP_INFO, m_grpInfo);
	MpDdxControl(pDX, IDC_MP_GRP_SND, m_grpSnd);
	MpDdxControl(pDX, IDC_MP_GRP_PL, m_grpPl);
	MpDdxControl(pDX, IDC_MP_LIST, m_list);
}

BEGIN_MESSAGE_MAP(CMediaPlayerDlg, CCustomBlurDialogExBase)
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_TIMER()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_WM_DROPFILES()
	ON_WM_DESTROY()
	ON_WM_CLOSE()
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_MP_PREV, &CMediaPlayerDlg::OnPrev)
	ON_BN_CLICKED(IDC_MP_PLAY, &CMediaPlayerDlg::OnPlay)
	ON_BN_CLICKED(IDC_MP_PAUSE, &CMediaPlayerDlg::OnPauseBtn)
	ON_BN_CLICKED(IDC_MP_STOP, &CMediaPlayerDlg::OnStopBtn)
	ON_BN_CLICKED(IDC_MP_NEXT, &CMediaPlayerDlg::OnNext)
	ON_BN_CLICKED(IDC_MP_EQ, &CMediaPlayerDlg::OnEq)
	ON_BN_CLICKED(IDC_MP_PIANO, &CMediaPlayerDlg::OnPiano)
	ON_BN_CLICKED(IDC_MP_ANALYZER, &CMediaPlayerDlg::OnAnalyzer)
	ON_BN_CLICKED(IDC_MP_FADEOUT, &CMediaPlayerDlg::OnFadeout)
	ON_BN_CLICKED(IDC_MP_FOLDER, &CMediaPlayerDlg::OnFolder)
	ON_BN_CLICKED(IDC_MP_EXIT, &CMediaPlayerDlg::OnExit)
	ON_BN_CLICKED(IDC_MP_JACK, &CMediaPlayerDlg::OnJacket)
	ON_BN_CLICKED(IDC_MP_SETTINGS, &CMediaPlayerDlg::OnSettings)
	ON_STN_CLICKED(IDC_MP_TEMPO_L, &CMediaPlayerDlg::OnTempoReset)
	ON_STN_CLICKED(IDC_MP_PITCH_L, &CMediaPlayerDlg::OnPitchReset)
	ON_BN_CLICKED(IDC_MP_SWITCHMODE, &CMediaPlayerDlg::OnSwitch)
	ON_BN_CLICKED(IDC_MP_RENZOKU, &CMediaPlayerDlg::OnRenzoku)
	ON_BN_CLICKED(IDC_MP_LOOP, &CMediaPlayerDlg::OnLoop)
	ON_BN_CLICKED(IDC_MP_RANDOM, &CMediaPlayerDlg::OnRandom)
	ON_CBN_SELCHANGE(IDC_MP_PLSEL, &CMediaPlayerDlg::OnPlSel)
	ON_CBN_DROPDOWN(IDC_MP_PLSEL, &CMediaPlayerDlg::OnPlselDropdown)
	ON_BN_CLICKED(IDC_MP_PLRENAME, &CMediaPlayerDlg::OnPlRename)
	ON_BN_CLICKED(IDC_MP_PLDELETE, &CMediaPlayerDlg::OnPlDelete)
	ON_BN_CLICKED(IDC_MP_LSUP, &CMediaPlayerDlg::OnMoveTop)
	ON_BN_CLICKED(IDC_MP_UP, &CMediaPlayerDlg::OnMoveUp)
	ON_BN_CLICKED(IDC_MP_DOWN, &CMediaPlayerDlg::OnMoveDown)
	ON_BN_CLICKED(IDC_MP_LSDOWN, &CMediaPlayerDlg::OnMoveBottom)
	ON_BN_CLICKED(IDC_MP_ITEMDEL, &CMediaPlayerDlg::OnItemDel)
	ON_BN_CLICKED(IDC_MP_M3U_EXPORT, &CMediaPlayerDlg::OnM3uExport)
	ON_BN_CLICKED(IDC_MP_M3U_IMPORT, &CMediaPlayerDlg::OnM3uImport)
	ON_BN_CLICKED(IDC_MP_SUPE, &CMediaPlayerDlg::OnSupe)
	ON_BN_CLICKED(IDC_MP_ST, &CMediaPlayerDlg::OnSt)
	ON_BN_CLICKED(IDC_MP_PROMPT, &CMediaPlayerDlg::OnPrompt)
	ON_BN_CLICKED(IDC_MP_TIP, &CMediaPlayerDlg::OnTip)
	ON_BN_CLICKED(IDC_MP_MINI, &CMediaPlayerDlg::OnMini)
	ON_BN_CLICKED(IDC_MP_SAVEMP3, &CMediaPlayerDlg::OnSaveMp3)
	ON_BN_CLICKED(IDC_MP_SAVEDS, &CMediaPlayerDlg::OnSaveDs)
	ON_BN_CLICKED(IDC_MP_SAVEWAV, &CMediaPlayerDlg::OnSaveWav)
	ON_BN_CLICKED(IDC_MP_SAVEPARAM, &CMediaPlayerDlg::OnSaveParam)
	ON_BN_CLICKED(IDC_MP_RESETDATA, &CMediaPlayerDlg::OnResetData)
	ON_EN_KILLFOCUS(IDC_MP_KAISUU, &CMediaPlayerDlg::OnKaisuuKillFocus)
	ON_BN_CLICKED(IDC_MP_FINDUP, &CMediaPlayerDlg::OnFindUp)
	ON_BN_CLICKED(IDC_MP_FINDDOWN, &CMediaPlayerDlg::OnFindDown)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_ENTERSIZEMOVE()
	ON_WM_EXITSIZEMOVE()
	ON_NOTIFY(LVN_GETDISPINFO, IDC_MP_LIST, &CMediaPlayerDlg::OnGetdispinfoList)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_MP_LIST, &CMediaPlayerDlg::OnListItemChanged)
	ON_NOTIFY(NM_DBLCLK, IDC_MP_LIST, &CMediaPlayerDlg::OnDblclkList)
	ON_NOTIFY(NM_RCLICK, IDC_MP_LIST, &CMediaPlayerDlg::OnRclickList)
	ON_NOTIFY(LVN_KEYDOWN, IDC_MP_LIST, &CMediaPlayerDlg::OnKeydownList)
	ON_NOTIFY(LVN_BEGINDRAG, IDC_MP_LIST, &CMediaPlayerDlg::OnBeginDragList)
	ON_NOTIFY(HDN_BEGINTRACKA, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_NOTIFY(HDN_BEGINTRACKW, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_NOTIFY(HDN_ENDTRACKA, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_NOTIFY(HDN_ENDTRACKW, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_NOTIFY(HDN_TRACKA, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_NOTIFY(HDN_TRACKW, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_MESSAGE(WM_MP_INFO_SCROLL, &CMediaPlayerDlg::OnInfoScrollTick)
	ON_MESSAGE(WM_MP_PLSEL_EXPAND, &CMediaPlayerDlg::OnPlselExpandPopup)
	ON_WM_NCACTIVATE()
	ON_WM_SYSCOMMAND()
	ON_WM_MOVING()
END_MESSAGE_MAP()

static void MpMakePushToggle(CWnd* p)
{
	if (p && p->GetSafeHwnd())
		p->ModifyStyle(BS_TYPEMASK, BS_AUTOCHECKBOX | BS_PUSHLIKE | WS_TABSTOP);
}

static void MpSetPushToggle(CCustomStandardButton& btn, BOOL on,
	COLORREF onS, COLORREF onE, COLORREF offS, COLORREF offE)
{
	if (!btn.GetSafeHwnd()) return;
	btn.SetCheck(on ? BST_CHECKED : BST_UNCHECKED);
	btn.SetGradation(on ? onS : offS, on ? onE : offE, 0, TRUE);
	btn.EnsureAnimTimer();
	btn.RepaintClient();
}

int CMediaPlayerDlg::Create(CWnd* pParent)
{
	BOOL bret = CCustomBlurDialogExBase::Create(CMediaPlayerDlg::IDD, pParent);
	if (bret == TRUE) {
		ShowWindow(SW_SHOW);
		// OnInitDialog 時点では EnsureVisible が効かないことがあるため、
		// 表示確定後にプレイリスト側の選択位置へ復元する。
		InitListScrollPosition();
	}
	return bret;
}

BOOL CMediaPlayerDlg::OnInitDialog()
{
	// MFC の WindowProc 外側 CATCH に上げると ReportError で
	// 「引数が正しくありません」が出る。ここで必ず飲み込む。
	try {
	if (!CCustomBlurDialogExBase::OnInitDialog())
		return FALSE;

	// 子コントロールを親の再描画で塗り潰さない(スタティック消失・リスト欠け・ちらつき防止)
	ModifyStyle(0, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

	// アナライザーボタンは RC 非依存で動的生成(DoLayout 前に HWND を確保)
	if (!m_analyzer.GetSafeHwnd() && m_piano.GetSafeHwnd()) {
		CRect rc;
		m_piano.GetWindowRect(&rc);
		ScreenToClient(&rc);
		if (rc.Width() < 1) rc.right = rc.left + 28;
		if (rc.Height() < 1) rc.bottom = rc.top + 12;
		const int gap = max(2, rc.Height() / 8);
		rc.OffsetRect(rc.Width() + gap, 0);
		if (!m_analyzer.Create(_T("アナ"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
			rc, this, IDC_MP_ANALYZER))
		{
			// 生成失敗時は以降のアナ操作をスキップ(Create 全体は落とさない)
		}
		else {
			// RC 由来の兄弟と違い Create はシステムフォントになるので、隣のボタンに揃える
			CFont* pFont = m_piano.GetFont();
			if (pFont)
				m_analyzer.SetFont(pFont);
		}
	}

	// プロンプトボタン(スペアナの左)
	if (!m_prompt.GetSafeHwnd() && m_supe.GetSafeHwnd()) {
		CRect rc;
		m_supe.GetWindowRect(&rc);
		ScreenToClient(&rc);
		if (rc.Width() < 1) rc.right = rc.left + 28;
		if (rc.Height() < 1) rc.bottom = rc.top + 12;
		const int gap = max(2, rc.Height() / 8);
		rc.OffsetRect(-(rc.Width() + gap), 0);
		if (!m_prompt.Create(LL14(L"プロンプト", L"Prompt", L"Prompt", L"Prompt", L"Prompt", L"프롬프트", L"提示", L"موجه", L"Промпт", L"Prompt", L"Prompt", L"Prompt", L"Prompt", L"Istem"),
			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_PROMPT))
		{
		}
		else {
			CFont* pFont = m_supe.GetFont();
			if (pFont)
				m_prompt.SetFont(pFont);
			m_prompt.SetGradation(RGB(255, 225, 245), RGB(255, 180, 210), 0, TRUE);
		}
	}

	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);

	// "バージョン情報..." メニュー項目をシステム メニュー(左上アイコンのメニュー)へ追加。
	// ファルコム特化型画面(COggDlg)と同じ挙動にそろえる。
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);
	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	CDC* dc = GetDC();
	if (dc) {
		hD2 = (float)GetDeviceCaps(dc->m_hDC, LOGPIXELSX) / 96.0f;
		ReleaseDC(dc);
	}
	if (hD2 < 1.0f) hD2 = 1.0f;

	{
		CString cap = LL14(L"メディアプレイヤー「らいら」", L"Media Player \"Raira\"", L"Lecteur multimedia « Raira »", L"Lettore multimediale \"Raira\"", L"Reproductor multimedia \"Raira\"", L"미디어 플레이어 「라이라」", L"媒体播放器「莱拉」", L"مشغل الوسائط \"رايرا\"", L"Медиаплеер «Райра»", L"Media-Player \"Raira\"", L"Reprodutor de midia \"Raira\"", L"Mediaspeler \"Raira\"", L"Odtwarzacz multimediow \"Raira\"", L"Medya Oynatıcı \"Raira\"");
		cap += MpBuildVersionCaptionSuffix();
		SetWindowText(cap);
	}

	m_play.SetWindowText(LL14(L"再生", L"Play", L"Lire", L"Riproduci", L"Reproducir", L"재생", L"播放", L"تشغيل", L"Играть", L"Wiedergabe", L"Reproduzir", L"Afspelen", L"Odtwarzaj", L"Oynat"));
	m_pause.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시정지", L"暂停", L"إيقاف مؤقت", L"Пауза", L"Pause", L"Pausar", L"Pauze", L"Pauza", L"Duraklat"));
	m_stop.SetWindowText(LL14(L"停止", L"Stop", L"Arret", L"Stop", L"Detener", L"정지", L"停止", L"إيقاف", L"Стоп", L"Stopp", L"Parar", L"Stop", L"Stop", L"Durdur"));
	m_renzoku.SetWindowText(LL14(L"連続再生", L"Continuous", L"Lect. continue", L"Continua", L"Continua", L"연속 재생", L"连续播放", L"تشغيل متتابع", L"Подряд", L"Folge", L"Continuo", L"Doorlopend", L"Ciągłe", L"Sürekli çal"));
	m_loop.SetWindowText(LL14(L"ループ再生", L"Loop play", L"Lecture boucle", L"Riproduci loop", L"Repetir", L"루프 재생", L"循环播放", L"تشغيل متكرر", L"Цикл", L"Schleife", L"Repetir", L"Lus afspelen", L"Odtwarz. pętli", L"Donguye al"));
	m_random.SetWindowText(LL14(L"ランダム再生", L"Random play", L"Lect. aleatoire", L"Casuale", L"Aleatorio", L"랜덤 재생", L"随机播放", L"تشغيل عشوائي", L"Случайно", L"Zufall", L"Aleatorio", L"Willekeurig", L"Losowo", L"Rastgele cal"));
	m_eq.SetWindowText(LL14(L"イコライザー", L"Equalizer", L"Egaliseur", L"Equalizzatore", L"Ecualizador", L"이퀄라이저", L"均衡器", L"المعادل", L"Эквалайзер", L"Equalizer", L"Equalizador", L"Equalizer", L"Korektor", L"Ekolayzer"));
	m_piano.SetWindowText(LL14(L"簡易ピアノロール", L"Simple Piano Roll", L"Rouleau piano simple", L"Piano roll semplice", L"Rollo piano simple", L"간이 피아노 롤", L"简易钢琴卷帘", L"لوحة بيانو بسيطة", L"Простой пианоролл", L"Einfache Klavierrolle", L"Piano roll simples", L"Eenvoudige pianorol", L"Prosta rolka pianina", L"Basit piyano rulosu"));
	if (m_analyzer.GetSafeHwnd())
		m_analyzer.SetWindowText(LL14(L"アナライザー", L"Analyzer", L"Analyseur", L"Analizzatore", L"Analizador", L"분석기", L"分析器", L"المحلل", L"Анализатор", L"Analysator", L"Analisador", L"Analyser", L"Analizator", L"Analizor"));
	m_switch.SetWindowText(LL14(L"ファルコム特化型へ", L"To Falcom screen", L"Vers ecran Falcom", L"Alla schermata Falcom", L"A pantalla Falcom", L"팔콤 화면으로", L"切换到Falcom画面", L"إلى شاشة Falcom", L"К экрану Falcom", L"Zum Falcom-Bildschirm", L"Para tela Falcom", L"Naar Falcom-scherm", L"Do ekranu Falcom", L"Falcom ekranına"));
	m_settings.SetWindowText(LL14(L"設定", L"Settings", L"Reglages", L"Impostazioni", L"Ajustes", L"설정", L"设置", L"إعدادات", L"Настройки", L"Einstellungen", L"Config.", L"Instellingen", L"Ustawienia", L"Ayarlar"));
	m_jacket.SetWindowText(LL14(L"ジャケット", L"Jacket", L"Pochette", L"Copertina", L"Caratula", L"자켓", L"封面", L"الغلاف", L"Обложка", L"Cover", L"Capa", L"Omslag", L"Okładka", L"Kapak"));
	m_exit.SetWindowText(LL14(L"終了", L"Exit", L"Quitter", L"Esci", L"Salir", L"종료", L"退出", L"خروج", L"Выход", L"Beenden", L"Sair", L"Afsluiten", L"Zakończ", L"Çıkış"));
	m_fadeout.SetWindowText(LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza", L"Desvanecer", L"페이드 아웃", L"淡出", L"تلاشي", L"Затухание", L"Ausblenden", L"Desvanecer", L"Uitfaden", L"Zanikanie", L"Soluklaştır"));
	m_folder.SetWindowText(LL14(L"フォルダ", L"Folder", L"Dossier", L"Cartella", L"Carpeta", L"폴더", L"文件夹", L"مجلد", L"Папка", L"Ordner", L"Pasta", L"Map", L"Folder", L"Klasor"));
	m_jacket.SetGradation(RGB(255, 235, 245), RGB(255, 200, 225), 0, TRUE);
	m_exit.SetGradation(RGB(255, 210, 210), RGB(255, 160, 160), 0, TRUE);
	m_fadeout.SetGradation(RGB(255, 235, 215), RGB(255, 200, 150), 0, TRUE);
	m_folder.SetGradation(RGB(220, 240, 230), RGB(180, 220, 200), 0, TRUE);
	m_vollabel.SetWindowText(LL14(L"!@C406848!@B主音量", L"!@C406848!@BVolume", L"!@C406848!@BVolume", L"!@C406848!@BVolume", L"!@C406848!@BVolumen", L"!@C406848!@B음량", L"!@C406848!@B音量", L"!@C406848!@Bالصوت", L"!@C406848!@BГромкость", L"!@C406848!@BLautstarke", L"!@C406848!@BVolume", L"!@C406848!@BVolume", L"!@C406848!@BGłośność", L"!@C406848!@BSes"));
	m_plrename.SetWindowText(LL14(L"名前変更", L"Rename", L"Renommer", L"Rinomina", L"Renombrar", L"이름변경", L"重命名", L"إعادة تسمية", L"Переименовать", L"Umbenennen", L"Renomear", L"Hernoemen", L"Zmień nazwę", L"Yeniden adlandır"));
	m_pldelete.SetWindowText(LL14(L"リスト削除", L"Delete list", L"Suppr. liste", L"Elimina lista", L"Eliminar lista", L"목록삭제", L"删除列表", L"حذف القائمة", L"Удалить список", L"Liste loschen", L"Excluir lista", L"Lijst wissen", L"Usuń listę", L"Listeyi sil"));
	m_itemdel.SetWindowText(LL14(L"曲削除", L"Remove", L"Retirer", L"Rimuovi", L"Quitar", L"곡삭제", L"删除曲目", L"حذف", L"Удалить", L"Entfernen", L"Remover", L"Verwijder", L"Usuń utwór", L"Parçayı sil"));
	m_m3uExport.SetWindowText(LL14(L"m3u出力", L"m3u export", L"Export m3u", L"Esporta m3u", L"Exportar m3u", L"m3u 내보내기", L"m3u导出", L"تصدير m3u", L"Экспорт m3u", L"m3u export", L"Exportar m3u", L"m3u export", L"Eksport m3u", L"m3u disa aktar"));
	m_m3uImport.SetWindowText(LL14(L"m3u入力", L"m3u import", L"Import m3u", L"Importa m3u", L"Importar m3u", L"m3u 가져오기", L"m3u导入", L"استيراد m3u", L"Импорт m3u", L"m3u import", L"Importar m3u", L"m3u import", L"Import m3u", L"m3u ice aktar"));
	m_supe.SetWindowText(LL14(L"スペアナ", L"Spectrum", L"Spectre", L"Spettro", L"Espectro", L"스펙트럼", L"频谱", L"الطيف", L"Спектр", L"Spektrum", L"Espectro", L"Spectrum", L"Widmo", L"Spektrum"));
	m_st.SetWindowText(LL14(L"ステレオ表示", L"Stereo view", L"Vue stereo", L"Vista stereo", L"Vista estereo", L"스테레오 표시", L"立体声显示", L"عرض ستيريو", L"Стерео", L"Stereo", L"Visao stereo", L"Stereo", L"Widok stereo", L"Stereo gosterim"));
	m_tip.SetWindowText(LL14(L"ツールチップ", L"Tooltips", L"Info-bulles", L"Suggerimenti", L"Sugerencias", L"툴팁", L"工具提示", L"تلميحات", L"Подсказки", L"Tooltips", L"Dicas", L"Tooltips", L"Etykiety", L"İpuçları"));
	m_mini.SetWindowText(LL14(L"最小化連動", L"Min. sync", L"Sync. min.", L"Sinc. min.", L"Sincr. min.", L"최소화 연동", L"最小化联动", L"تزامن التصغير", L"Синхр. сверт.", L"Min.-Sync", L"Sinc. min.", L"Min. koppelen", L"Synch. min.", L"Min. eşitle"));
	m_savemp3.SetWindowText(LL14(L"mp3途中保存", L"mp3 resume", L"mp3 reprise", L"mp3 ripresa", L"mp3 reanudar", L"mp3 위치저장", L"mp3续播", L"حفظ موضع mp3", L"mp3 позиция", L"mp3 Position", L"mp3 retomar", L"mp3 hervat", L"mp3 wznow", L"mp3 sürdür"));
	m_saveds.SetWindowText(LL14(L"DShow途中保存", L"DShow resume", L"DShow reprise", L"DShow ripresa", L"DShow reanudar", L"DShow 위치저장", L"DShow续播", L"حفظ موضع DShow", L"DShow позиция", L"DShow Position", L"DShow retomar", L"DShow hervat", L"DShow wznow", L"DShow sürdür"));
	m_savewav.SetWindowText(LL14(L"WAVファイルへ保存", L"Save to WAV file", L"Enregistrer en WAV", L"Salva come WAV", L"Guardar como WAV", L"WAV 파일로 저장", L"保存到WAV文件", L"حفظ كـ WAV", L"Сохранить в WAV", L"Als WAV speichern", L"Salvar como WAV", L"Opslaan als WAV", L"Zapisz jako WAV", L"WAV olarak kaydet"));
	m_saveparam.SetWindowText(LL14(L"曲ごとに設定保存", L"Save per-song", L"Réglages/morceau", L"Impost. per brano", L"Ajustes por pista", L"곡별 설정 저장", L"逐曲保存设置", L"حفظ لكل أغنية", L"Настройки на трек", L"Pro Titel speichern", L"Config. por faixa", L"Per nummer opslaan", L"Ustaw. na utwor", L"Parça başına kaydet"));
	m_resetdata.SetWindowText(LL14(L"保存をリセット", L"Reset saved", L"Réinitialiser", L"Reimposta salvati", L"Restablecer", L"저장 초기화", L"重置已存", L"إعادة تعيين", L"Сброс сохран.", L"Zurücksetzen", L"Redefinir", L"Reset opgeslagen", L"Resetuj zapis", L"Kayıtı sıfırla"));
	m_kaisuuL.SetWindowText(LL14(L"ループ回数", L"Loop count", L"Nombre de boucles", L"Conteggio loop", L"Cuenta de bucle", L"루프 횟수", L"循环次数", L"عدد الحلقات", L"Количество повторов", L"Schleifenzahler", L"Contagem de loop", L"Loopaantal", L"Liczba petli", L"Dongu sayisi"));
	{
		CString ks; ks.Format(_T("%d"), savedata.kaisuu > 0 ? savedata.kaisuu : 2);
		m_kaisuu.SetWindowText(ks);
	}
	m_grpInfo.SetWindowText(LL14(L"情報", L"Info", L"Info", L"Info", L"Info", L"정보", L"信息", L"معلومات", L"Инфо", L"Info", L"Info", L"Info", L"Info", L"Bilgi"));
	m_grpSnd.SetWindowText(LL14(L"サウンド調整", L"Sound", L"Son", L"Audio", L"Sonido", L"사운드", L"声音", L"الصوت", L"Звук", L"Sound", L"Som", L"Geluid", L"Dźwięk", L"Ses"));
	m_grpPl.SetWindowText(LL14(L"プレイリスト", L"Playlist", L"Liste", L"Playlist", L"Lista", L"재생목록", L"播放列表", L"قائمة", L"Плейлист", L"Playlist", L"Lista", L"Playlist", L"Lista", L"Liste"));
	// グループ枠は最背面 + WS_CLIPSIBLINGS で、内側のコントロールを塗り潰さない
	m_grpInfo.ModifyStyle(0, WS_CLIPSIBLINGS);
	m_grpSnd.ModifyStyle(0, WS_CLIPSIBLINGS);
	m_grpPl.ModifyStyle(0, WS_CLIPSIBLINGS);
	// ButtonST(プレイリストと同じアイコン): 一番上/上/下/一番下 と あいまい検索 上/下
	m_lsup.SetIcon(IDR_SUP);    m_lsup.SetFlat(TRUE);
	m_up.SetIcon(IDR_UP);       m_up.SetFlat(TRUE);
	m_down.SetIcon(IDR_DOWN);   m_down.SetFlat(TRUE);
	m_lsdown.SetIcon(IDR_SDOWN); m_lsdown.SetFlat(TRUE);
	m_findup.SetIcon(IDR_DOWN); m_findup.SetFlat(TRUE);
	m_finddown.SetIcon(IDR_UP); m_finddown.SetFlat(TRUE);

	// サウンド調整スライダー(og の各スライダーと同じ範囲に合わせる)
	m_dsvol.SetRange(-498, 1); 
	m_kvol.SetRange(100, 900);
	m_tempo.SetRange(0, 400);   m_tempo.SetMode(1);
	m_pitch.SetRange(0, 400);   m_pitch.SetMode(1);

	// シークスライダーに選択範囲(緑)を有効化。リソースでは付いていないため
	// ここで付与しないと MirrorSeekVol の SetSelection(ループ範囲/緑追随)が描画されない。
	m_seek.ModifyStyle(0, TBS_ENABLESELRANGE);

	// 少し可愛い系の配色
	m_prev.SetGradation(RGB(215, 235, 255), RGB(165, 205, 245), 0, TRUE);
	m_play.SetGradation(RGB(200, 240, 200), RGB(140, 210, 150), 0, TRUE);
	m_pause.SetGradation(RGB(255, 240, 200), RGB(255, 210, 140), 0, TRUE);
	m_stop.SetGradation(RGB(255, 215, 220), RGB(255, 170, 185), 0, TRUE);
	m_next.SetGradation(RGB(215, 235, 255), RGB(165, 205, 245), 0, TRUE);
	m_eq.SetGradation(RGB(230, 220, 255), RGB(200, 185, 250), 0, TRUE);
	m_piano.SetGradation(RGB(230, 220, 255), RGB(200, 185, 250), 0, TRUE);
	if (m_analyzer.GetSafeHwnd())
		m_analyzer.SetGradation(RGB(230, 220, 255), RGB(200, 185, 250), 0, TRUE);
	m_switch.SetGradation(RGB(225, 210, 255), RGB(190, 170, 255), 0, TRUE);
	m_settings.SetGradation(RGB(255, 235, 205), RGB(255, 205, 150), 0, TRUE);
	m_plrename.SetGradation(RGB(220, 240, 255), RGB(180, 215, 250), 0, TRUE);
	m_pldelete.SetGradation(RGB(255, 220, 225), RGB(255, 180, 190), 0, TRUE);
	m_itemdel.SetGradation(RGB(255, 220, 225), RGB(255, 180, 190), 0, TRUE);
	m_m3uExport.SetGradation(RGB(220, 240, 255), RGB(180, 215, 250), 0, TRUE);
	m_m3uImport.SetGradation(RGB(220, 240, 255), RGB(180, 215, 250), 0, TRUE);
	MpMakePushToggle(&m_supe);
	MpMakePushToggle(&m_st);
	MpMakePushToggle(&m_eq);
	MpMakePushToggle(&m_piano);
	if (m_analyzer.GetSafeHwnd())
		MpMakePushToggle(&m_analyzer);
	MpSetPushToggle(m_supe, FALSE, RGB(140, 220, 160), RGB(80, 180, 110), RGB(215, 240, 220), RGB(175, 215, 190));
	MpSetPushToggle(m_st, FALSE, RGB(160, 200, 255), RGB(100, 150, 230), RGB(215, 230, 255), RGB(175, 200, 245));
	MpSetPushToggle(m_eq, FALSE, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
	MpSetPushToggle(m_piano, FALSE, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
	if (m_analyzer.GetSafeHwnd())
		MpSetPushToggle(m_analyzer, FALSE, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
	CCustomControlUtility::SetControlBackgroundColor(&m_plsel, COLOR_COMBO_BG);
	// タイトルに淡いドロップシャドウで可愛く強調
	m_title.SetDropShadow(RGB(255, 220, 235), 0, 1, 0, TRUE);

	// リスト列(プレイリストと同じ並び)
	DWORD ex = m_list.GetExtendedStyle();
	ex |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_INFOTIP;
	m_list.SetExtendedStyle(ex);
	// Create 失敗後の Add は ENSURE→CInvalidArgException（「引数が正しくありません」）
	if (il.m_hImageList)
		il.DeleteImageList();
	if (il.Create(16, 16, ILC_COLOR32 | ILC_MASK, 0, 1)) {
		HICON h1 = AfxGetApp()->LoadIcon(IDI_ICON1);
		HICON h2 = AfxGetApp()->LoadIcon(IDI_ICON2);
		HICON h3 = AfxGetApp()->LoadIcon(IDI_ICON3);
		if (h1) il.Add(h1);
		if (h2) il.Add(h2);
		if (h3) il.Add(h3);
		m_list.SetImageList(&il, LVSIL_SMALL);
	}
	m_list.InsertColumn(0, LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"), LVCFMT_LEFT, (int)(220 * hD2));
	m_list.InsertColumn(1, L"★", LVCFMT_CENTER, (int)(20 * hD2)); // 曲ごと設定の有無(中央寄せ・余白最小)
	m_list.InsertColumn(2, LL14(L"ゲーム", L"Game", L"Jeu", L"Gioco", L"Juego", L"게임", L"游戏", L"لعبة", L"Игра", L"Spiel", L"Jogo", L"Spel", L"Gra", L"Oyun"), LVCFMT_LEFT, (int)(60 * hD2));
	m_list.InsertColumn(3, LL14(L"時間", L"Time", L"Duree", L"Durata", L"Duracion", L"시간", L"时间", L"الوقت", L"Время", L"Zeit", L"Duracao", L"Tijd", L"Czas", L"Sure"), LVCFMT_RIGHT, (int)(72 * hD2));
	m_list.InsertColumn(4, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Kunstler", L"Artista", L"Artiest", L"Artysta", L"Sanatçı"), LVCFMT_LEFT, (int)(160 * hD2));
	m_list.InsertColumn(5, LL14(L"アルバム/コメント", L"Album/Comment", L"Album/Comm.", L"Album/Comm.", L"Album/Com.", L"앨범/댓글", L"专辑/注释", L"الألبوم/تعليق", L"Альбом/Комм.", L"Album/Komm.", L"Album/Coment.", L"Album/Opm.", L"Album/Komentarz", L"Album/Yorum"), LVCFMT_LEFT, (int)(160 * hD2));

	// メディアプレイヤー側リストも pl->pc を参照してツールチップに保存パラメータを付記
	if (pl) m_list.pc = pl->pc;
	m_list.m_bSongParamTip = true;

	// 保存済みの列幅を復元(0=未設定なら上で設定した既定値のまま)
	// mpcol の意味スロットは ★挿入前と同じ: [0]=名前 [1]=ゲーム [2]=時間 [3]=アーティスト [4]=未使用
	// ★列は狭固定のため永続化しない。最終列(5=アルバム)は FitPlaylistLastColumn で右端フィット。
	savedata.mpcol[4] = 0;
	if (savedata.mpcol[0] > 0) {
		int w = savedata.mpcol[0];
		if (w > (int)(2000 * hD2)) w = (int)(2000 * hD2);
		m_list.SetColumnWidth(0, w); // 名前
	}
	if (savedata.mpcol[1] > 0) {
		int w = savedata.mpcol[1];
		if (w > (int)(2000 * hD2)) w = (int)(2000 * hD2);
		m_list.SetColumnWidth(2, w); // ゲーム
	}
	if (savedata.mpcol[2] > 0) {
		int w = savedata.mpcol[2];
		if (w > (int)(2000 * hD2)) w = (int)(2000 * hD2);
		if (w < (int)(72 * hD2))
			w = (int)(72 * hD2);   // 「取得不能」等が切れない最小幅
		m_list.SetColumnWidth(3, w); // 時間
	}
	if (savedata.mpcol[3] > 0) {
		int w = savedata.mpcol[3];
		if (w > (int)(2000 * hD2)) w = (int)(2000 * hD2);
		m_list.SetColumnWidth(4, w); // アーティスト
	}
	FitPlaylistLastColumn();
	// 列ドラッグ中も幅をライブ反映(HDN_TRACK + ヘッダー幅ポーリング)
	if (CHeaderCtrl* pHdr = m_list.GetHeaderCtrl()) {
		pHdr->ModifyStyle(0, HDS_FULLDRAG);
		pHdr->ModifyStyle(HDS_DRAGDROP, 0); // 列並べ替え中は幅追随不要
	}
	if (::IsWindow(m_list.GetSafeHwnd()))
		SetWindowSubclass(m_list.GetSafeHwnd(), ListHeaderNotifySubclassProc, kMpListHdrSubclassId, (DWORD_PTR)this);

	// フォント(.dat ずれで顔名が壊れていると CreateFont が失敗し得るため LF_FACESIZE に収める)
	TCHAR faceSafe[LF_FACESIZE];
	{
		LPCTSTR src = _tcslen(savedata.font2) ? savedata.font2
			: (_tcslen(savedata.font1) ? savedata.font1 : _T("メイリオ"));
		bool bad = false;
		for (LPCTSTR p = src; *p; ++p) {
			if ((unsigned short)*p < 0x20) { bad = true; break; }
		}
		if (bad || _tcslen(src) == 0 || _tcslen(src) >= LF_FACESIZE)
			src = _T("メイリオ");
		_tcsncpy_s(faceSafe, src, _TRUNCATE);
		auto makeFont = [&](CFont& f, int px, int weight) {
			if (f.GetSafeHandle()) f.DeleteObject();
			const int h = max(8, (int)(px * hD2 + 0.5f));
			if (!f.CreateFont(-h, 0, 0, 0, weight, 0, 0, 0, SHIFTJIS_CHARSET,
				OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, CLEARTYPE_QUALITY,
				DEFAULT_PITCH | FF_SWISS, faceSafe))
			{
				f.CreateFont(-h, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
					OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, CLEARTYPE_QUALITY,
					DEFAULT_PITCH | FF_SWISS, _T("Segoe UI"));
			}
		};
		makeFont(m_fontTitle, 20, FW_BOLD);
		makeFont(m_fontInfo, 13, FW_NORMAL);
		// リスト・連続/ループ/回数/下部チェックは同一サイズ(拡大・縮小描画で差が出ないよう)
		makeFont(m_fontList, 12, FW_NORMAL);
		makeFont(m_fontChk, 12, FW_NORMAL);
	}
	m_tip.SetFont(&m_fontChk, TRUE);
	m_mini.SetFont(&m_fontChk, TRUE);
	m_savemp3.SetFont(&m_fontChk, TRUE);
	m_saveds.SetFont(&m_fontChk, TRUE);
	m_savewav.SetFont(&m_fontChk, TRUE);
	m_saveparam.SetFont(&m_fontChk, TRUE);
	m_renzoku.SetFont(&m_fontChk, TRUE);
	m_loop.SetFont(&m_fontChk, TRUE);
	m_random.SetFont(&m_fontChk, TRUE);
	m_kaisuuL.SetFont(&m_fontChk, TRUE);
	// PreferWideMode は縦に引き伸ばして「ループ回数」だけ巨大化するので使わない
	m_kaisuuL.SetPreferWideMode(FALSE);
	m_kaisuu.SetFont(&m_fontChk, TRUE);
	// タイトル/アーティスト/アルバムはバナーGDIに表示されるのでスタティックは隠す(縦幅節約)
	m_title.ShowWindow(SW_HIDE);
	m_artist.ShowWindow(SW_HIDE);
	m_album.ShowWindow(SW_HIDE);
	m_lrc.SetFont(&m_fontInfo, TRUE);
	m_lrc2.SetFont(&m_fontInfo, TRUE);
	m_lrc3.SetFont(&m_fontInfo, TRUE);
	m_lrc4.SetFont(&m_fontInfo, TRUE);
	m_lrc5.SetFont(&m_fontInfo, TRUE);
	m_dsvolL.SetFont(&m_fontInfo, TRUE);
	m_kvolL.SetFont(&m_fontInfo, TRUE);
	m_tempoL.SetFont(&m_fontInfo, TRUE);
	m_pitchL.SetFont(&m_fontInfo, TRUE);
	m_vollabel.SetFont(&m_fontInfo, TRUE);
	m_volval.SetFont(&m_fontInfo, TRUE);
	m_time.SetFont(&m_fontInfo, TRUE);
	m_list.SetFont(&m_fontList, TRUE);
	// ツールチップは SyncFromMain が m_tip を確定した後に ApplyListTooltipState で設定

	// タイトルは可愛くピンク強調
	m_title.SetGradation(RGB(255, 105, 180), RGB(150, 60, 160), 0, TRUE);

	m_vol.SetRange(0, 100);
	m_vol.SetPos(100);

	// 初期座標: 保存座標があればそれ、なければファルコム画面の位置・プレイリストの大きさ
	{
		int x = savedata.mpx, y = savedata.mpy, w = savedata.mpw, h = savedata.mph;
		if (!savedata.mpHasPos || w < 100 || h < 100 || w > 10000 || h > 10000) {
			RECT ro = { 0,0,0,0 };
			if (og && ::IsWindow(og->GetSafeHwnd()))
				og->GetWindowRect(&ro);
			x = (ro.left != 0 || ro.right != 0) ? ro.left : 100;
			y = (ro.top != 0 || ro.bottom != 0) ? ro.top : 100;
			w = (int)(580 * hD2);
			h = (int)(620 * hD2);
			if (pl && ::IsWindow(pl->GetSafeHwnd())) {
				RECT rp; pl->GetWindowRect(&rp);
				int pw = rp.right - rp.left, ph = rp.bottom - rp.top;
				if (pw > 200) w = pw;
				if (ph > 200) h = ph + (int)(360 * hD2); // プレイリスト分+情報/操作部
			}
		}
		// 最小サイズを下回らないようにクランプ(レイアウト崩れ防止)
		if (w < (int)(620 * hD2)) w = (int)(620 * hD2);
		if (h < (int)(560 * hD2)) h = (int)(560 * hD2);
		// 画面外チェック
		RECT rcWork; SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
		if (x < rcWork.left - 50 || x > rcWork.right - 50) x = rcWork.left + 40;
		if (y < rcWork.top - 10 || y > rcWork.bottom - 50) y = rcWork.top + 40;
		MoveWindow(x, y, w, h);
	}

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_BALLOON | TTS_NOPREFIX);
	auto addTip = [this](CWnd& w, LPCTSTR text) {
		if (!m_tooltip.GetSafeHwnd() || !text) return;
		HWND hw = w.GetSafeHwnd();
		if (!hw || !::IsWindow(hw)) return;
		m_tooltip.AddTool(&w, text);
	};
	addTip(m_switch, LL14(L"ファルコムbgm特化型画面へ戻します。", L"Return to the Falcom BGM dedicated screen.", L"Revenir a l'ecran Falcom.", L"Torna alla schermata Falcom.", L"Volver a la pantalla Falcom.", L"팔콤 전용 화면으로 돌아갑니다.", L"返回Falcom专用画面。", L"العودة إلى شاشة Falcom.", L"Вернуться к экрану Falcom.", L"Zum Falcom-Bildschirm zuruck.", L"Voltar para a tela Falcom.", L"Terug naar Falcom-scherm.", L"Powrot do ekranu Falcom.", L"Falcom ekranına dön."));
	addTip(m_prev, LL14(L"前の曲へ。", L"Previous track.", L"Piste precedente.", L"Traccia precedente.", L"Pista anterior.", L"이전 곡.", L"上一曲。", L"المقطع السابق.", L"Предыдущий трек.", L"Vorheriger Titel.", L"Faixa anterior.", L"Vorige track.", L"Poprzedni utwor.", L"Onceki parca."));
	addTip(m_play, LL14(L"再生 / 選択曲を再生します。", L"Play / play the selected track.", L"Lire la piste selectionnee.", L"Riproduci la traccia selezionata.", L"Reproducir la pista seleccionada.", L"선택한 곡을 재생합니다.", L"播放所选曲目。", L"تشغيل المقطع المحدد.", L"Воспроизвести выбранный трек.", L"Ausgewahlten Titel abspielen.", L"Reproduzir a faixa selecionada.", L"Geselecteerde track afspelen.", L"Odtworz wybrany utwor.", L"Seçili parçayı çal."));
	addTip(m_pause, LL14(L"一時停止 / 再開します。", L"Pause / resume.", L"Pause / reprise.", L"Pausa / riprendi.", L"Pausar / reanudar.", L"일시정지 / 재개.", L"暂停/继续。", L"إيقاف مؤقت / استئناف.", L"Пауза / продолжить.", L"Pause / Fortsetzen.", L"Pausar / retomar.", L"Pauze / hervatten.", L"Pauza / wznow.", L"Duraklat / surdur."));
	addTip(m_stop, LL14(L"停止します。", L"Stop.", L"Arreter.", L"Ferma.", L"Detener.", L"정지합니다.", L"停止。", L"إيقاف.", L"Остановить.", L"Stoppen.", L"Parar.", L"Stoppen.", L"Zatrzymaj.", L"Durdur."));
	addTip(m_next, LL14(L"次の曲へ。", L"Next track.", L"Piste suivante.", L"Traccia successiva.", L"Pista siguiente.", L"다음 곡.", L"下一曲。", L"المقطع التالي.", L"Следующий трек.", L"Nachster Titel.", L"Proxima faixa.", L"Volgende track.", L"Następny utwór.", L"Sonraki parca."));
	addTip(m_renzoku, LL14(L"プレイリストを順番に連続再生します。", L"Play the playlist continuously in order.", L"Lecture continue dans l'ordre.", L"Riproduzione continua in ordine.", L"Reproduccion continua en orden.", L"순서대로 연속 재생.", L"按顺序连续播放。", L"تشغيل متواصل بالترتيب.", L"Непрерывное воспроизведение по порядку.", L"Fortlaufend in Reihenfolge abspielen.", L"Reproducao continua em ordem.", L"Doorlopend afspelen op volgorde.", L"Odtwarzaj po kolei.", L"Sırayla sürekli çal."));
	addTip(m_loop, LL14(L"選択した曲をループ再生します。", L"Loop the selected track.", L"Lire la piste en boucle.", L"Ripeti la traccia.", L"Repetir la pista.", L"선택한 곡을 반복 재생.", L"循环播放所选曲目。", L"تكرار المقطع المحدد.", L"Зациклить выбранный трек.", L"Ausgewahlten Titel wiederholen.", L"Repetir a faixa selecionada.", L"Geselecteerde track herhalen.", L"Zapętl wybrany utwór.", L"Seçili parçayı döngüye al."));
	addTip(m_random, LL14(L"ランダム再生 / 順次再生を切り替えます。", L"Toggle random / sequential play.", L"Lecture aleatoire / sequentielle.", L"Riproduzione casuale / sequenziale.", L"Reproduccion aleatoria / secuencial.", L"랜덤 / 순차 재생 전환.", L"切换随机/顺序播放。", L"تبديل التشغيل العشوائي/المتسلسل.", L"Случайное / последовательное.", L"Zufall / Reihenfolge umschalten.", L"Aleatorio / sequencial.", L"Willekeurig / opeenvolgend.", L"Losowo / po kolei.", L"Rastgele / sıralı."));
	addTip(m_seek, LL14(L"再生位置。ドラッグでシークします(ループ範囲も表示)。", L"Playback position. Drag to seek (loop range shown).", L"Position de lecture. Glissez pour chercher.", L"Posizione. Trascina per cercare.", L"Posicion. Arrastra para buscar.", L"재생 위치. 드래그로 탐색.", L"播放位置。拖动以定位。", L"موضع التشغيل. اسحب للبحث.", L"Позиция. Перетащите для перемотки.", L"Position. Zum Suchen ziehen.", L"Posicao. Arraste para buscar.", L"Positie. Sleep om te zoeken.", L"Pozycja. Przeciągnij.", L"Konum. Surukleyerek ara."));
	addTip(m_vol, LL14(L"音量を調整します。", L"Adjust volume.", L"Regler le volume.", L"Regola il volume.", L"Ajustar el volumen.", L"음량을 조절합니다.", L"调整音量。", L"ضبط مستوى الصوت.", L"Регулировка громкости.", L"Lautstarke einstellen.", L"Ajustar o volume.", L"Volume aanpassen.", L"Reguluj głośność.", L"Sesi ayarla."));
	addTip(m_eq, LL14(L"イコライザーを開きます。", L"Open the equalizer.", L"Ouvrir l'egaliseur.", L"Apri l'equalizzatore.", L"Abrir el ecualizador.", L"이퀄라이저를 엽니다.", L"打开均衡器。", L"فتح المعادل.", L"Открыть эквалайзер.", L"Equalizer offnen.", L"Abrir o equalizador.", L"Equalizer openen.", L"Otworz korektor.", L"Ekolayzeri ac."));
	addTip(m_piano, LL14(L"簡易ピアノロールを開きます。", L"Open the simple piano roll.", L"Ouvrir le rouleau piano simple.", L"Apri il piano roll semplice.", L"Abrir el rollo de piano simple.", L"간이 피아노 롤을 엽니다.", L"打开简易钢琴卷帘。", L"فتح لوحة البيانو البسيطة.", L"Открыть простой пианоролл.", L"Einfache Klavierrolle offnen.", L"Abrir o piano roll simples.", L"Eenvoudige pianorol openen.", L"Otworz prosta rolke pianina.", L"Basit piyano rulosunu ac."));
	if (m_analyzer.GetSafeHwnd())
		addTip(m_analyzer, LL14(L"アナライザーを開きます。", L"Open the analyzer.", L"Ouvrir l'analyseur.", L"Apri l'analizzatore.", L"Abrir el analizador.", L"분석기를 엽니다.", L"打开分析器。", L"فتح المحلل.", L"Открыть анализатор.", L"Analysator offnen.", L"Abrir o analisador.", L"Analyser openen.", L"Otworz analizator.", L"Analizoru ac."));
	addTip(m_jacket, LL14(L"ジャケット画像を別窓で表示します。", L"Show cover art in a separate window.", L"Afficher la pochette.", L"Mostra la copertina.", L"Mostrar la caratula.", L"커버 이미지를 표시합니다.", L"在单独窗口显示封面。", L"عرض صورة الغلاف.", L"Показать обложку.", L"Cover anzeigen.", L"Mostrar a capa.", L"Toon hoes.", L"Pokaż okładkę.", L"Kapak resmini goster."));
	addTip(m_exit, LL14(L"アプリケーションを終了します。", L"Exit the application.", L"Quitter l'application.", L"Esci dall'applicazione.", L"Salir de la aplicacion.", L"앱을 종료합니다.", L"退出应用程序。", L"إنهاء التطبيق.", L"Выйти из приложения.", L"Anwendung beenden.", L"Sair do aplicativo.", L"Toepassing afsluiten.", L"Zamknij aplikację.", L"Uygulamadan çık."));
	// m_list のバルーンは「ツールチップ」OFF時のみ。ON時は行詳細ツールチップへ切替(ApplyListTooltipState)。
	addTip(m_settings, LL14(L"設定画面を開きます。", L"Open settings.", L"Ouvrir les reglages.", L"Apri le impostazioni.", L"Abrir ajustes.", L"설정 화면을 엽니다.", L"打开设置。", L"فتح الإعدادات.", L"Открыть настройки.", L"Einstellungen offnen.", L"Abrir configuracoes.", L"Instellingen openen.", L"Otworz ustawienia.", L"Ayarları aç."));
	addTip(m_fadeout, LL14(L"再生中の曲をフェードアウトして停止します。", L"Fade out and stop the current track.", L"Fondu et arret du morceau.", L"Dissolvenza e stop del brano.", L"Desvanecer y detener la pista.", L"현재 곡을 페이드 아웃하여 정지합니다.", L"淡出并停止当前曲目。", L"تلاشي وإيقاف المقطع الحالي.", L"Затухание и остановка трека.", L"Aktuellen Titel ausblenden und stoppen.", L"Desvanecer e parar a faixa.", L"Huidige track uitfaden en stoppen.", L"Wycisz i zatrzymaj utwor.", L"Parçayı soluklaştırıp durdur."));
	addTip(m_folder, LL14(L"フォルダ設定画面を開きます(フォルダの登録/追加)。", L"Open folder settings (register/add folders).", L"Ouvrir les parametres de dossier.", L"Apri impostazioni cartella.", L"Abrir config. de carpeta.", L"폴더 설정 화면을 엽니다.", L"打开文件夹设置。", L"فتح إعدادات المجلد.", L"Открыть настройки папки.", L"Ordnereinstellungen offnen.", L"Abrir config. de pasta.", L"Mapinstellingen openen.", L"Otworz ustawienia folderu.", L"Klasör ayarlarını aç."));
	addTip(m_dsvol, LL14(L"DirectSound音量を調整します。", L"Adjust DirectSound volume.", L"Reglez le volume DirectSound.", L"Regola il volume DirectSound.", L"Ajustar volumen DirectSound.", L"DirectSound 음량 조절.", L"调整DirectSound音量。", L"ضبط مستوى صوت DirectSound.", L"Громкость DirectSound.", L"DirectSound-Lautstarke.", L"Volume DirectSound.", L"DirectSound-volume.", L"Głośność DirectSound.", L"DirectSound sesi."));
	addTip(m_kvol, LL14(L"拡張音量(ブースト)を調整します。", L"Adjust extended (boost) volume.", L"Volume etendu (boost).", L"Volume esteso (boost).", L"Volumen extendido (boost).", L"확장(부스트) 음량 조절.", L"调整扩展(增益)音量。", L"ضبط الصوت الموسع (التعزيز).", L"Расширенная громкость (буст).", L"Erweiterte Lautstarke (Boost).", L"Volume estendido (boost).", L"Uitgebreid (boost) volume.", L"Rozszerzona głośność.", L"Genişletilmiş ses."));
	addTip(m_tempo, LL14(L"再生テンポを調整します(ラベルをクリックで100%に戻す)。", L"Adjust playback tempo (click label to reset to 100%).", L"Tempo de lecture (clic sur le label = 100%).", L"Tempo (clic sull'etichetta = 100%).", L"Tempo (clic en etiqueta = 100%).", L"재생 템포 조절(라벨 클릭 시 100%).", L"调整播放速度(点击标签恢复100%)。", L"ضبط الإيقاع (انقر التسمية لإعادة 100%).", L"Темп (клик по метке = 100%).", L"Tempo (Label klicken = 100%).", L"Tempo (clique no rotulo = 100%).", L"Tempo (klik label = 100%).", L"Tempo (etykieta = 100%).", L"Tempo (etikete tıkla = %100)."));
	addTip(m_pitch, LL14(L"再生ピッチ(音程)を調整します(ラベルをクリックで100%に戻す)。", L"Adjust playback pitch (click label to reset to 100%).", L"Hauteur (clic sur le label = 100%).", L"Altezza (clic sull'etichetta = 100%).", L"Tono (clic en etiqueta = 100%).", L"재생 피치 조절(라벨 클릭 시 100%).", L"调整音高(点击标签恢复100%)。", L"ضبط طبقة الصوت (انقر التسمية لإعادة 100%).", L"Высота (клик по метке = 100%).", L"Tonhohe (Label klicken = 100%).", L"Tom (clique no rotulo = 100%).", L"Toonhoogte (klik label = 100%).", L"Wysokosc (klik = 100%).", L"Perde (etikete tıkla = %100)."));
	addTip(m_plsel, LL14(L"プレイリストを切り替え/新規追加します。", L"Switch / add a playlist.", L"Changer / ajouter une liste.", L"Cambia / aggiungi playlist.", L"Cambiar / anadir lista.", L"재생목록 전환/추가.", L"切换/新建播放列表。", L"تبديل / إضافة قائمة.", L"Сменить / добавить плейлист.", L"Playlist wechseln / hinzufugen.", L"Trocar / adicionar lista.", L"Playlist wisselen/toevoegen.", L"Zmień/dodaj listę.", L"Liste değiştir/ekle."));
	addTip(m_plrename, LL14(L"現在のプレイリスト名を変更します。", L"Rename the current playlist.", L"Renommer la liste.", L"Rinomina la playlist.", L"Renombrar la lista.", L"현재 재생목록 이름 변경.", L"重命名当前播放列表。", L"إعادة تسمية القائمة.", L"Переименовать плейлист.", L"Playlist umbenennen.", L"Renomear a lista.", L"Lijst hernoemen.", L"Zmień nazwę listy.", L"Listeyi yeniden adlandır."));
	addTip(m_pldelete, LL14(L"現在のプレイリストを削除します。", L"Delete the current playlist.", L"Supprimer la liste.", L"Elimina la playlist.", L"Eliminar la lista.", L"현재 재생목록 삭제.", L"删除当前播放列表。", L"حذف القائمة.", L"Удалить плейлист.", L"Playlist loschen.", L"Excluir a lista.", L"Lijst verwijderen.", L"Usuń listę.", L"Listeyi sil."));
	addTip(m_m3uExport, LL14(L"現在のプレイリストをM3U形式で書き出します。", L"Export the current playlist as M3U.", L"Exporter la liste en M3U.", L"Esporta la playlist in M3U.", L"Exportar la lista como M3U.", L"현재 재생목록을 M3U로 내보냅니다.", L"将当前播放列表导出为M3U。", L"تصدير القائمة ك M3U.", L"Экспорт плейлиста в M3U.", L"Playlist als M3U exportieren.", L"Exportar lista como M3U.", L"Playlist exporteren als M3U.", L"Eksportuj liste do M3U.", L"Listeyi M3U olarak disa aktar."));
	addTip(m_m3uImport, LL14(L"プレイリストファイル(M3U/PLS等)を読み込みます。", L"Import a playlist file (M3U/PLS etc.).", L"Importer un fichier de liste.", L"Importa un file playlist.", L"Importar archivo de lista.", L"재생목록 파일을 가져옵니다.", L"导入播放列表文件。", L"استيراد ملف قائمة.", L"Импорт файла плейлиста.", L"Playlist-Datei importieren.", L"Importar arquivo de lista.", L"Playlistbestand importeren.", L"Importuj plik listy.", L"Oynatma listesi dosyasi ice aktar."));
	addTip(m_up, LL14(L"選択した曲を上へ移動します。", L"Move selected track up.", L"Monter la piste.", L"Sposta su.", L"Subir pista.", L"선택 곡을 위로.", L"上移所选曲目。", L"تحريك لأعلى.", L"Переместить вверх.", L"Nach oben.", L"Mover para cima.", L"Omhoog verplaatsen.", L"Przesuń w górę.", L"Yukarı taşı."));
	addTip(m_down, LL14(L"選択した曲を下へ移動します。", L"Move selected track down.", L"Descendre la piste.", L"Sposta giu.", L"Bajar pista.", L"선택 곡을 아래로.", L"下移所选曲目。", L"تحريك لأسفل.", L"Переместить вниз.", L"Nach unten.", L"Mover para baixo.", L"Omlaag verplaatsen.", L"Przesuń w dół.", L"Aşağı taşı."));
	addTip(m_itemdel, LL14(L"選択した曲をリストから削除します。", L"Remove selected track(s) from the list.", L"Retirer les pistes selectionnees.", L"Rimuovi le tracce selezionate.", L"Quitar pistas seleccionadas.", L"선택 곡을 목록에서 삭제.", L"从列表删除所选曲目。", L"حذف المقاطع المحددة.", L"Удалить выбранные треки.", L"Ausgewahlte Titel entfernen.", L"Remover faixas selecionadas.", L"Geselecteerde tracks verwijderen.", L"Usuń zaznaczone utwory.", L"Seçili parçaları sil."));
	addTip(m_supe, LL14(L"スペアナ表示を切り替えます。", L"Toggle spectrum display.", L"Afficher le spectre.", L"Mostra spettro.", L"Mostrar espectro.", L"스펙트럼 표시 전환.", L"切换频谱显示。", L"تبديل عرض الطيف.", L"Спектр вкл/выкл.", L"Spektrum umschalten.", L"Alternar espectro.", L"Spectrum wisselen.", L"Przełącz widmo.", L"Spektrumu değiştir."));
	if (m_prompt.GetSafeHwnd())
		addTip(m_prompt, LL14(L"演奏アレンジ用プロンプトウィンドウを開きます。", L"Open the performance prompt window.", L"Ouvrir la fenetre de prompt.", L"Apri finestra prompt.", L"Abrir ventana de prompt.", L"연주 프롬프트 창을 엽니다.", L"打开演奏提示窗口。", L"فتح نافذة الموجه.", L"Открыть окно промпта.", L"Prompt-Fenster oeffnen.", L"Abrir janela de prompt.", L"Promptvenster openen.", L"Otworz okno promptu.", L"Istem penceresini ac."));
	addTip(m_st, LL14(L"スペアナのステレオ(L/R)表示を切り替えます。", L"Toggle stereo (L/R) spectrum view.", L"Afficher le spectre stereo L/R.", L"Mostra spettro stereo L/R.", L"Mostrar espectro estereo L/R.", L"스테레오(L/R) 스펙트럼 표시 전환.", L"切换立体声(L/R)频谱显示。", L"تبديل عرض الطيف الستيريو.", L"Переключить стерео-спектр.", L"Stereo-Spektrum umschalten.", L"Alternar espectro stereo.", L"Stereo spectrum wisselen.", L"Przelacz widmo stereo.", L"Stereo spektrumu degistir."));
	addTip(m_find, LL14(L"あいまい検索キーワード。▲▼で前後検索。", L"Fuzzy search keyword. Use up/down to find.", L"Mot-cle recherche floue.", L"Parola chiave ricerca fuzzy.", L"Palabra busqueda difusa.", L"퍼지 검색어. ▲▼로 검색.", L"模糊搜索关键字。▲▼查找。", L"كلمة بحث غامض.", L"Слово нечеткого поиска.", L"Fuzzy-Suchbegriff.", L"Palavra de busca fuzzy.", L"Fuzzy zoekterm.", L"Słowo wyszukiwania.", L"Bulanık arama kelimesi."));
	addTip(m_findup, LL14(L"下方向(リスト後方)に検索します。", L"Search downward in the list.", L"Chercher vers le bas.", L"Cerca in basso.", L"Buscar abajo.", L"아래로 검색.", L"向下搜索。", L"بحث للأسفل.", L"Искать вниз.", L"Abwarts suchen.", L"Buscar abaixo.", L"Omlaag zoeken.", L"Szukaj w dol.", L"Asagi ara."));
	addTip(m_finddown, LL14(L"上方向(リスト前方)に検索します。", L"Search upward in the list.", L"Chercher vers le haut.", L"Cerca in alto.", L"Buscar arriba.", L"위로 검색.", L"向上搜索。", L"بحث للأعلى.", L"Искать вверх.", L"Aufwarts suchen.", L"Buscar acima.", L"Omhoog zoeken.", L"Szukaj w gore.", L"Yukari ara."));
	addTip(m_lsup, LL14(L"選択曲を一番上へ移動。", L"Move to top.", L"Tout en haut.", L"In cima.", L"Al principio.", L"맨 위로.", L"移到顶部。", L"إلى الأعلى.", L"В начало.", L"Ganz nach oben.", L"Para o topo.", L"Naar boven.", L"Na gore.", L"En uste."));
	addTip(m_up, LL14(L"選択曲を上へ移動。", L"Move up.", L"Monter.", L"Su.", L"Subir.", L"위로.", L"上移。", L"لأعلى.", L"Вверх.", L"Hoch.", L"Cima.", L"Omhoog.", L"W gore.", L"Yukarı."));
	addTip(m_down, LL14(L"選択曲を下へ移動。", L"Move down.", L"Descendre.", L"Giu.", L"Bajar.", L"아래로.", L"下移。", L"لأسفل.", L"Вниз.", L"Runter.", L"Baixo.", L"Omlaag.", L"W dół.", L"Aşağı."));
	addTip(m_lsdown, LL14(L"選択曲を一番下へ移動。", L"Move to bottom.", L"Tout en bas.", L"In fondo.", L"Al final.", L"맨 아래로.", L"移到底部。", L"إلى الأسفل.", L"В конец.", L"Ganz nach unten.", L"Para o final.", L"Naar beneden.", L"Na dol.", L"En alta."));
	addTip(m_tip, LL14(L"行ツールチップの表示を切り替えます。", L"Toggle row tooltips.", L"Info-bulles des lignes.", L"Suggerimenti righe.", L"Sugerencias de filas.", L"행 툴팁 표시 전환.", L"切换行工具提示。", L"تبديل تلميحات الصفوف.", L"Подсказки строк.", L"Zeilen-Tooltips.", L"Dicas de linha.", L"Rij-tooltips.", L"Etykiety wierszy.", L"Satır ipuçları."));
	addTip(m_mini, LL14(L"最小化/復帰をメイン画面と連動させます。", L"Sync minimize/restore with main window.", L"Synchroniser min./rest.", L"Sincronizza min./rip.", L"Sincronizar min./rest.", L"최소화/복원 연동.", L"最小化/还原联动。", L"تزامن التصغير/الاستعادة.", L"Синхр. сверт./восст.", L"Min./Wiederh. synchron.", L"Sincronizar min./rest.", L"Min./herstel synch.", L"Synch. min./przywr.", L"Min./geri yükleme eşitle."));
	addTip(m_savemp3, LL14(L"mp3再生時に途中保存を有効にします。", L"Enable resume save for mp3.", L"Reprise pour mp3.", L"Ripresa per mp3.", L"Reanudar para mp3.", L"mp3 위치 저장.", L"mp3续播保存。", L"حفظ موضع mp3.", L"Сохранение позиции mp3.", L"mp3-Position speichern.", L"Retomar mp3.", L"mp3 hervatten.", L"Wznawianie mp3.", L"mp3 surdurme."));
	addTip(m_saveds, LL14(L"DirectShow(動画等)で途中保存を有効にします。", L"Enable resume save for DirectShow.", L"Reprise pour DirectShow.", L"Ripresa per DirectShow.", L"Reanudar para DirectShow.", L"DirectShow 위치 저장.", L"DirectShow续播保存。", L"حفظ موضع DirectShow.", L"Сохранение позиции DirectShow.", L"DirectShow-Position.", L"Retomar DirectShow.", L"DirectShow hervatten.", L"Wznawianie DirectShow.", L"DirectShow surdurme."));
	addTip(m_savewav, LL14(L"再生中の音声をWAVファイルへ保存します。", L"Save playback audio to a WAV file.", L"Enregistrer l'audio en WAV.", L"Salva l'audio in WAV.", L"Guardar audio en WAV.", L"재생 음을 WAV로 저장.", L"将播放音频保存为WAV。", L"حفظ الصوت كـ WAV.", L"Сохранить звук в WAV.", L"Audio als WAV speichern.", L"Salvar audio em WAV.", L"Audio opslaan als WAV.", L"Zapis audio jako WAV.", L"Sesi WAV olarak kaydet."));
	addTip(m_saveparam, LL14(L"曲ごとに音量・EQ・テンポ等の全パラメータを記憶し、その曲を再生する度に自動で復元します。", L"Remember all parameters (volume, EQ, tempo, etc.) per song and auto-restore them each time the song plays.", L"Memoriser tous les parametres par morceau et les restaurer automatiquement.", L"Memorizza tutti i parametri per brano e li ripristina automaticamente.", L"Recuerda todos los parametros por pista y los restaura automaticamente.", L"곡별로 볼륨·EQ·템포 등 모든 파라미터를 기억하고 재생할 때마다 자동 복원합니다.", L"逐曲记忆音量、EQ、速度等所有参数，每次播放该曲时自动恢复。", L"تذكر كل المعلمات لكل أغنية واستعادتها تلقائيًا.", L"Запоминать все параметры для каждого трека и восстанавливать автоматически.", L"Alle Parameter pro Titel merken und automatisch wiederherstellen.", L"Memoriza todos os parametros por faixa e restaura automaticamente.", L"Onthoud alle parameters per nummer en herstel automatisch.", L"Zapamietaj wszystkie parametry na utwor i przywracaj automatycznie.", L"Her parça için tüm parametreleri hatırla ve otomatik geri yükle."));
	addTip(m_resetdata, LL14(L"曲ごとに保存した設定を全削除し、音量50%・拡張100%・EQ等を初期状態へ戻します。", L"Delete all per-song saved settings and reset volume to 50%, ext to 100%, EQ etc. to defaults.", L"Supprimer tous les reglages par morceau et reinitialiser les parametres.", L"Elimina tutte le impostazioni per brano e ripristina i parametri.", L"Elimina todos los ajustes por pista y restablece los parametros.", L"곡별 저장 설정을 모두 삭제하고 볼륨 50%·확장 100%·EQ 등을 초기화합니다.", L"删除所有逐曲保存的设置，并将音量重置为50%、扩展100%、EQ等为默认。", L"حذف كل الإعدادات المحفوظة لكل أغنية وإعادة الضبط.", L"Удалить все сохранённые настройки треков и сбросить параметры.", L"Alle pro-Titel-Einstellungen loeschen und Parameter zuruecksetzen.", L"Excluir todas as configuracoes por faixa e redefinir os parametros.", L"Verwijder alle per-nummer-instellingen en reset de parameters.", L"Usun wszystkie ustawienia na utwor i zresetuj parametry.", L"Tum parca ayarlarini sil ve parametreleri sifirla."));
	addTip(m_kaisuu, LL14(L"連続再生時、指定回数ループしたら次の曲へ進みます。", L"During continuous play, advance after this many loops.", L"En lecture continue, passer apres ce nombre de boucles.", L"In riproduzione continua, avanza dopo questo numero di loop.", L"En reproduccion continua, avanzar tras este numero de bucles.", L"연속 재생 시 지정 횟수만큼 반복 후 다음 곡.", L"连续播放时，循环指定次数后进入下一首。", L"في التشغيل المستمر، الانتقال بعد هذا العدد من الحلقات.", L"При непрерывном воспроизведении перейти после стольких повторов.", L"Bei Dauerwiedergabe nach so vielen Schleifen weiter.", L"Na reproducao continua, avancar apos este numero de loops.", L"Bij doorlopend afspelen na dit aantal loops verder.", L"Przy ciaglym odtwarzaniu przejdz po tylu petlach.", L"Surekli calmada bu dongu sayisindan sonra ilerle."));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 10000);
	m_find.SetFont(&m_fontList, TRUE);

	DoLayout();
	CCC_GroupBoxesBack(GetSafeHwnd());   // 区分け枠を最背面へ(兄弟コントロールを覆わない)
	ReloadPlaylistCombo();
	RefreshList(TRUE);
	SyncFromMain();
	ApplyListTooltipState(); // 行詳細 ON/OFF とリスト・バルーン切替を初期確定
#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1)
		RefreshAeroMode();   // レイアウト確定後にアクリル/不透明化を再適用
#endif

	// 描画タイマーは og のスレッド基準(16ms=60fps)に合わせて固定する。
	// 実際のスペアナ等の更新頻度は og 側が savedata.ms2 で律速しており(ms2カウンタ)、
	// mp はそれを 60fps で Blit して pending を解除するだけ。ここで savedata.ms2 を
	// そのまま間隔に使うと描画全体がその間隔まで律速され遅くなる(=不具合の原因)。
	m_lastMs2 = 16;
	SetTimer(1, 250, NULL);          // 低速: テキスト/リスト/コンボ/チェックの同期
	SetTimer(2, 100, NULL);          // 安全網: 取りこぼし時のみバナー再描画(通常はtimerpが駆動)
	SetTimer(3, 33, NULL);           // 高速: シーク(playb追従)/時間/音量のミラー
#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1)
		SetTimer(4, 250, NULL);  // 遅延でアクリル再適用(ウィンドウ合成確定後)。一回で止める。
#endif
	// 起動直後はリスト項目を不可視時に設定したためスクロールバーが未実現。
	// 表示確定後にリストの非クライアント(枠/スクロールバー)を再描画して確実に表示
	// (アクリル時は OpaqueFixer の WM_NCPAINT で不透明化される)。一回限り。
	SetTimer(6, 120, NULL);
	m_uiReady = true;
	return TRUE;
	}
	catch (CException* e)
	{
		// 診断: 実行フォルダに残す(次回から原因切り分け用)
		{
			TCHAR msg[512] = {};
			e->GetErrorMessage(msg, _countof(msg) - 1);
			CStdioFile f;
			if (f.Open(_T("mp_init_exception.log"),
				CFile::modeCreate | CFile::modeWrite | CFile::typeText))
			{
				CString line;
				line.Format(_T("CMediaPlayerDlg::OnInitDialog caught %hs: %s\n"),
					e->GetRuntimeClass()->m_lpszClassName, msg);
				f.WriteString(line);
				f.Close();
			}
		}
		e->Delete();
		return TRUE;
	}
}

// og 所有のまま(EQ/簡易ピアノロールと同じアクリルグループ)にして非アクティブでも
// アクリルを維持しつつ、WS_EX_APPWINDOW でタスクバーに単独ボタンを出す。
BOOL CMediaPlayerDlg::PreCreateWindow(CREATESTRUCT& cs)
{
	if (!CCustomBlurDialogExBase::PreCreateWindow(cs))
		return FALSE;
	cs.dwExStyle |= WS_EX_APPWINDOW;
	return TRUE;
}

BOOL CMediaPlayerDlg::RelayPreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN) {
		CWnd* pFocus = GetFocus();
		if (pFocus && pFocus->GetSafeHwnd() == m_find.GetSafeHwnd()) {
			OnFindUp();  // Enter = 次の候補へ(og の IDOK/終了へ流さない)
			return TRUE;
		}
	}
	return FALSE;
}

BOOL CMediaPlayerDlg::PreTranslateMessage(MSG* pMsg)
{
	if (RelayPreTranslateMessage(pMsg))
		return TRUE;
	if (CCC_InwomanHotkey(pMsg, this))
		return TRUE; // 隠し: F12を5回で淫女モード切替
	// リスト行ツールチップ (CListCtrlA 実装): ツールチップ表示ON時のみリレー
	if (m_list.GetSafeHwnd() && m_tip.GetCheck())
		if (m_list.PreTranslateMessage(pMsg))
			return TRUE;
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CMediaPlayerDlg::RequestAppShutdown()
{
	SavePos();
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
}

BOOL CMediaPlayerDlg::DestroyWindow()
{
	SavePos();
	// バナー内蔵ジャケ(ファルコム特化型のミニジャケ)抑止フラグを必ず解除する。
	// これを残すと、ファルコム特化型へ戻した後もミニジャケが表示されなくなる。
	g_mpSideJacket = 0;
	KillTimer(1);
	KillTimer(2);
	KillTimer(3);
	KillTimer(4);
	KillTimer(7);
	if (::IsWindow(m_list.GetSafeHwnd()))
		RemoveWindowSubclass(m_list.GetSafeHwnd(), ListHeaderNotifySubclassProc, kMpListHdrSubclassId);
	if (m_bmpBanner.GetSafeHandle()) m_bmpBanner.DeleteObject();
	if (m_memBanner.GetSafeHdc()) m_memBanner.DeleteDC();
	for (int i = 0; i < kInfoRows; i++) {
		if (m_iscRowDC[i].GetSafeHdc()) {
			if (m_iscRowOldBmp[i]) m_iscRowDC[i].SelectObject(m_iscRowOldBmp[i]);
			m_iscRowDC[i].DeleteDC();
		}
		m_iscRowBmp[i].DeleteObject();
		m_iscRowOldBmp[i] = nullptr;
		m_iscRowCacheW[i] = m_iscRowCacheH[i] = 0;
		m_iscRowCacheText[i].Empty();
	}
	InterlockedExchange(&m_iscScrollPosted, 0);
	if (m_infoMemDC.GetSafeHdc()) {
		if (m_infoMemOldBmp) m_infoMemDC.SelectObject(m_infoMemOldBmp);
		m_infoMemDC.DeleteDC();
	}
	m_infoMemBmp.DeleteObject();
	m_infoMemOldBmp = nullptr;
	m_infoMemW = m_infoMemH = 0;
	return CCustomBlurDialogExBase::DestroyWindow();
}

// 1コントロールを移動するヘルパ。
// w/h が 0 以下だと MoveWindow/SetWindowPos が ERROR_INVALID_PARAMETER
// （「引数が正しくありません」）を立てるため、その場合は移動しない。
static void MoveCtl(CWnd* p, int x, int y, int w, int h)
{
	if (!p || !p->GetSafeHwnd()) return;
	if (w <= 0 || h <= 0) return;
	p->MoveWindow(x, y, w, h);
}

// m_plsel: CBS_DROPDOWNLIST の MoveWindow 高さはドロップダウン領域。毎回 tbH を渡すと潰れる。
// 初回だけ RC 相当の dropExtent を設定し、以降のリサイズは位置・幅のみ変更する。
#ifndef CB_SETMINVISIBLE
#define CB_SETMINVISIBLE 0x1702
#endif

static int MpPlselClosedH(float s)
{
	return max(1, (int)(19 * s + 0.5f));
}

static const int kMpPlselListRowH = 28;

static int MpPlselQueryRowH(HWND hCombo)
{
	int h = (int)(INT_PTR)::SendMessage(hCombo, CB_GETITEMHEIGHT, 0, 0);
	if (h <= 1)
		h = (int)(INT_PTR)::SendMessage(hCombo, CB_GETITEMHEIGHT, (WPARAM)-1, 0);
	if (h <= 1)
		h = kMpPlselListRowH;
	return h;
}

// listRowH = ドロップダウン行の高さ(MeasureItem と同じ 28px)
// closedH  = 選択欄の高さ(MoveCtl の tbH と同じ)。index 1 で明示する。
static void FixPlselDropList(CCustomComboBox& cb, int listRowH, int closedH)
{
	if (!cb.GetSafeHwnd() || listRowH <= 0 || closedH <= 0) return;
	const HWND h = cb.GetSafeHwnd();
	const auto setH = [&](WPARAM idx, int ht) -> LRESULT {
		return ::SendMessage(h, CB_SETITEMHEIGHT, idx, (LPARAM)ht);
	};
	const LRESULT r0 = setH(0, listRowH);
	if (cb.GetStyle() & CBS_OWNERDRAWVARIABLE)
	{
		const int n = (int)::SendMessage(h, CB_GETCOUNT, 0, 0);
		for (int i = 1; i < n; ++i)
			setH((WPARAM)i, listRowH);
	}
	else if (r0 == CB_ERR)
	{
		setH((WPARAM)-1, listRowH);
	}
	setH(1, closedH);
	const int cnt = (int)::SendMessage(h, CB_GETCOUNT, 0, 0);
	if (cnt > 0)
		::SendMessage(h, CB_SETMINVISIBLE, (WPARAM)min(cnt, 12), 0);
}

static void ExpandPlselDropListPopup(HWND hCombo)
{
	if (!hCombo) return;
	COMBOBOXINFO ci = { sizeof(ci) };
	if (!::GetComboBoxInfo(hCombo, &ci) || !ci.hwndList)
		return;
	const int cnt = (int)::SendMessage(hCombo, CB_GETCOUNT, 0, 0);
	if (cnt <= 0) return;
	const int vis = min(cnt, 12);
	// 行高はリストボックス自身から取得(コンボの CB_GETITEMHEIGHT とずれる環境がある)
	int rowH = (int)(INT_PTR)::SendMessage(ci.hwndList, LB_GETITEMHEIGHT, 0, 0);
	if (rowH <= 1)
		rowH = MpPlselQueryRowH(hCombo);
	// リストボックスの実際の枠(非クライアント)ぶんを実測して足す。SM_CYEDGE 固定だと
	// テーマ/DPI により誤差が出て下に空白が残る。
	CRect wr, cr;
	::GetWindowRect(ci.hwndList, &wr);
	::GetClientRect(ci.hwndList, &cr);
	const int ncH = max(0, wr.Height() - cr.Height());
	const int needH = rowH * vis + ncH;
	::SetWindowPos(ci.hwndList, NULL, 0, 0, wr.Width(), needH,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CMediaPlayerDlg::LayoutPlselCombo(int x, int y, int w, int tbH, float s)
{
	if (!::IsWindow(m_plsel.GetSafeHwnd())) return;
	if (m_plselDropExtent > 0 && fabs(m_plselLayoutDpi - s) > 0.01f)
		m_plselDropExtent = 0;

	const int closedH = MpPlselClosedH(s);
	const int dropExt = max((int)(182 * s + 0.5f), kMpPlselListRowH * 12);

	if (m_plselDropExtent <= 0)
	{
		m_plsel.MoveWindow(x, y, w, dropExt);
		m_plselDropExtent = dropExt;
		m_plselLayoutDpi = s;
		FixPlselDropList(m_plsel, kMpPlselListRowH, closedH);
	}
	else
	{
		m_plsel.SetWindowPos(NULL, x, y, w, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
		FixPlselDropList(m_plsel, kMpPlselListRowH, closedH);
	}

	CRect cr;
	m_plsel.GetWindowRect(&cr);
	ScreenToClient(&cr);
	const int dy = y + max(0, (tbH - cr.Height()) / 2);
	if (cr.left != x || cr.top != dy)
		m_plsel.SetWindowPos(NULL, x, dy, w, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

// DPI/リサイズ対応の手動レイアウト。RC で固定配置すると高 DPI で壊れるため
// OnInitDialog 後・OnSize ごとに呼ぶ。コントロール座標は hD2 スケールで計算する。
// バナー領域の計算はアスペクト維持(MP_SRCW:MP_SRCH)で行い、余白は DoLayout
// 内で m_jacketRect / m_infoPanelRect に割り当てる。
void CMediaPlayerDlg::DoLayout()
{
	if (!::IsWindow(GetSafeHwnd())) return;
	CRect rc; GetClientRect(&rc);
	const int W = rc.Width(), H = rc.Height();
	if (W < 32 || H < 32) return;   // 初期化途中の極小クライアントでは触らない
	const float s = hD2;
	const int M = (int)(10 * s);             // マージン

	// 上部: ビジュアライザ(スペアナ+ジャケ+時間)の帯。
	// アスペクト比(MP_SRCW:MP_SRCH)を保ったまま、高さを上限に抑えて幅を決める。
	// 横に伸ばしすぎると文字や隠れジャケットが見にくいため、最大幅を超えたら
	// それ以上ストレッチせず中央寄せにし、左右の余白は背景色/アクリルにする(OnPaintが処理)。
	int avail = W - M * 2;
	int bannerH = (int)(96 * s);                                  // 既定の帯高さ(上限)
	int bannerW = (int)((double)bannerH * (double)MP_SRCW / (double)MP_SRCH);  // 高さからアスペクト幅
	if (bannerW > avail) {                                        // 幅が足りない狭い窓では幅に合わせて縮小
		bannerW = avail;
		bannerH = (int)((double)bannerW * (double)MP_SRCH / (double)MP_SRCW);
		int bannerMin = (int)(54 * s);
		if (bannerH < bannerMin) bannerH = bannerMin;
	}

	// ===== 余白(左右)の有効活用 =====
	// バナーはアスペクト維持のため幅に上限があり、窓を広げると左右に余白ができる。
	// その余白へ: 左=ジャケット(ミニ・正方形), 右=曲情報パネル を順に展開する。
	// 余白が少ない狭い窓では従来どおりバナーを中央寄せするだけ(サイドパネル無し)。
	m_jacketRect.SetRectEmpty();
	m_infoPanelRect.SetRectEmpty();
	const int sideGap = (int)(10 * s);
	int freeSpace = avail - bannerW;                              // 左右に使える余白の合計
	int jacketSide = bannerH;                                     // ジャケットは帯と同じ高さの正方形
	bool showJacket = (freeSpace >= jacketSide + sideGap * 2);
	int leftZone = showJacket ? (jacketSide + sideGap) : 0;       // [ジャケ][gap] の占有幅
	int minInfo = (int)(130 * s);                                 // 情報パネルを出す最小幅
	int remainFree = freeSpace - leftZone;                        // ジャケ配置後に残る余白
	bool showInfo = showJacket && (remainFree >= minInfo + sideGap);

	int bannerX;
	if (!showJacket) {
		bannerX = M + freeSpace / 2;                              // 従来: 中央寄せのみ
	}
	else if (!showInfo) {
		// ジャケ+バナーのみ: 左右に余白を均等配分して塊を中央寄せ(空白を最小化)
		int pad = remainFree / 2;
		int jacketX = M + pad;
		m_jacketRect.SetRect(jacketX, M, jacketX + jacketSide, M + bannerH);
		bannerX = jacketX + leftZone;
	}
	else {
		// ジャケ(左端)+ バナー + 情報パネル(右端まで)で余白を埋め切る
		int jacketX = M;
		m_jacketRect.SetRect(jacketX, M, jacketX + jacketSide, M + bannerH);
		bannerX = jacketX + leftZone;
		int infoX = bannerX + bannerW + sideGap;
		m_infoPanelRect.SetRect(infoX, M, M + avail, M + bannerH);
	}
	m_bannerRect.SetRect(bannerX, M, bannerX + bannerW, M + bannerH);

	// og の timerp 側: ジャケットを左へ分離している間はバナー内蔵ジャケ描画を抑止
	// （ホバー前面化アルファも不要なので、分離中はバナーホバーも落とす）
	g_mpSideJacket = showJacket ? 1 : 0;
	if (g_mpSideJacket)
		g_mpBannerHover = 0;

	const int gTitle = (int)(14 * s);   // グループ枠のタイトル分の高さ
	const int gPad = (int)(5 * s);      // グループ内側の余白

	// ===== 情報グループ(歌詞5行 or 歌詞3行+OS/CPU) =====
	// 歌詞有無で中身は切替えるが、枠の高さは固定(5行分)にしてプレイリスト位置が動かないようにする。
	int infoTop = M + bannerH + (int)(2 * s);
	int ix = M + gPad, iw = W - M * 2 - gPad * 2;
	if (iw < 1) iw = 1;
	int y = infoTop + gTitle;
	int lh = (int)(17 * s);   // 情報フォント13px が収まる行高
	const int osH = (int)(15 * s);
	const int infoInnerH = lh * 5 + (int)(1 * s);
	const bool hasLyrics = (og && og->lrcnum >= 2);
	if (hasLyrics) {
		MoveCtl(&m_lrc, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc2, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc3, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc4, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc5, ix, y, iw, lh);
		m_lrc4.ShowWindow(SW_SHOW);
		m_lrc5.ShowWindow(SW_SHOW);
		MoveCtl(&m_os, ix, infoTop, 0, 0);
		MoveCtl(&m_cpu, ix, infoTop, 0, 0);
		MoveCtl(&m_os3, ix, infoTop, 0, 0);
		m_os.ShowWindow(SW_HIDE);
		m_cpu.ShowWindow(SW_HIDE);
		m_os3.ShowWindow(SW_HIDE);
	}
	else {
		MoveCtl(&m_lrc, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc2, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc3, ix, y, iw, lh); y += lh + (int)(1 * s);
		MoveCtl(&m_os, ix, y, iw * 3 / 5, osH);
		MoveCtl(&m_cpu, ix + iw * 3 / 5, y, iw * 2 / 5, osH); y += osH;
		MoveCtl(&m_os3, ix, y, iw, osH);
		MoveCtl(&m_lrc4, ix, infoTop, 0, 0);
		MoveCtl(&m_lrc5, ix, infoTop, 0, 0);
		m_lrc4.ShowWindow(SW_HIDE);
		m_lrc5.ShowWindow(SW_HIDE);
		m_os.ShowWindow(SW_SHOW);
		m_cpu.ShowWindow(SW_SHOW);
		m_os3.ShowWindow(SW_SHOW);
	}
	const int infoBottom = infoTop + gTitle + infoInnerH + gPad;
	MoveCtl(&m_grpInfo, M, infoTop, W - M * 2, infoBottom - infoTop);

	// ===== シーク(範囲スライダー, 全幅) + 時間% =====
	int seekY = infoBottom + (int)(5 * s);
	int timeW = (int)(42 * s);
	int seekW = W - M * 2 - timeW - (int)(4 * s);
	if (seekW < 1) seekW = 1;
	MoveCtl(&m_seek, M, seekY, seekW, (int)(16 * s));
	MoveCtl(&m_time, W - M - timeW, seekY + (int)(2 * s), timeW, (int)(14 * s));

	// ===== 操作行: 前/再生/一時停止/停止/フェードアウト/次 + ジャケ/EQ/ロール/アナ + 主音量(右) =====
	int by = seekY + (int)(22 * s);
	int bh = (int)(24 * s), gap = (int)(3 * s);
	if (bh < 1) bh = 1;

	int volValW = (int)(44 * s), volLblW = (int)(38 * s);
	const int volSlW = max(1, (int)(100 * s));
	int volvalX = W - M - volValW;
	int volSlX = volvalX - (int)(4 * s) - volSlW;
	int volLblX = volSlX - volLblW;
	const int freeEnd = volLblX - gap;

	// 幅に応じて 0=フル / 1=EQ系短縮 / 2=フェード・JK等も短縮 / 3=最小幅用の超短縮
	// 各段階は「その段階の幅でもまだ主音量に食い込むか」で次へ進む(同条件で2と3が同時発火しないこと)。
	const int prevW = max(1, (int)(40 * s));
	const int playW = max(1, (int)(48 * s));
	const int stopW = max(1, (int)(44 * s));
	const int nextW = max(1, (int)(40 * s));
	const int pauseFull = max(1, (int)(68 * s)), pauseShort = max(1, (int)(40 * s)), pauseTiny = max(1, (int)(28 * s));
	const int fadeFull = max(1, (int)(92 * s)), fadeShort = max(1, (int)(52 * s)), fadeTiny = max(1, (int)(28 * s));
	const int jkFull = max(1, (int)(62 * s)), jkShort = max(1, (int)(32 * s)), jkTiny = max(1, (int)(28 * s));
	const int ebwFull = max(1, (int)(84 * s)), pbwFull = max(1, (int)(128 * s)), abwFull = max(1, (int)(88 * s));
	const int ebwShort = max(1, (int)(42 * s)), pbwShort = max(1, (int)(56 * s)), abwShort = max(1, (int)(48 * s));
	const int ebwTiny = max(1, (int)(30 * s)), pbwTiny = max(1, (int)(28 * s)), abwTiny = max(1, (int)(28 * s));

	const int baseLeft = M + prevW + gap + playW + gap + stopW + gap + nextW + (int)(8 * s);
	// 各候補レイアウトの右端(アナライザー右端)。freeEnd を超えたら一段短い段階へ。
	const int endLv0 = baseLeft + pauseFull + gap + fadeFull + gap + jkFull + gap
		+ ebwFull + gap + pbwFull + gap + abwFull;
	const int endLv1 = baseLeft + pauseFull + gap + fadeFull + gap + jkFull + gap
		+ ebwShort + gap + pbwShort + gap + abwShort;
	const int endLv2 = baseLeft + pauseShort + gap + fadeShort + gap + jkShort + gap
		+ ebwShort + gap + pbwShort + gap + abwShort;

	int shortLv = 0;
	if (endLv0 > freeEnd)
		shortLv = 1;
	if (endLv1 > freeEnd)
		shortLv = 2;
	if (endLv2 > freeEnd)
		shortLv = 3;

	const int pauseW = (shortLv >= 3) ? pauseTiny : (shortLv >= 2) ? pauseShort : pauseFull;
	const int fadeW = (shortLv >= 3) ? fadeTiny : (shortLv >= 2) ? fadeShort : fadeFull;
	const int jkw = (shortLv >= 3) ? jkTiny : (shortLv >= 2) ? jkShort : jkFull;
	const int ebw = (shortLv >= 3) ? ebwTiny : (shortLv >= 1) ? ebwShort : ebwFull;
	const int pbw = (shortLv >= 3) ? pbwTiny : (shortLv >= 1) ? pbwShort : pbwFull;
	const int abw = (shortLv >= 3) ? abwTiny : (shortLv >= 1) ? abwShort : abwFull;

	if (shortLv != m_mpBtnShort) {
		m_mpBtnShort = shortLv;
		if (shortLv >= 3) {
			m_fadeout.SetWindowText(LL14(L"FO", L"FO", L"Fd", L"Fd", L"Fd", L"페", L"淡", L"تل", L"Зт", L"AO", L"Fd", L"Fo", L"Zan", L"So"));
			m_jacket.SetWindowText(LL14(L"JK", L"JK", L"Poc", L"Cop", L"Car", L"JK", L"封", L"غل", L"Обл", L"Cov", L"Cap", L"Oms", L"Okł", L"Kap"));
		}
		else if (shortLv >= 2) {
			m_fadeout.SetWindowText(LL14(L"フェード", L"Fade", L"Fondu", L"Fade", L"Fade", L"페이드", L"淡出", L"تلاشي", L"Затухание", L"Fade", L"Fade", L"Fade", L"Fade", L"Fade"));
			m_jacket.SetWindowText(LL14(L"JK", L"JK", L"Poc", L"Cop", L"Car", L"JK", L"封", L"غل", L"Обл", L"Cov", L"Cap", L"Oms", L"Okł", L"Kap"));
		}
		else {
			m_fadeout.SetWindowText(LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza", L"Desvanecer", L"페이드 아웃", L"淡出", L"تلاشي", L"Затухание", L"Ausblenden", L"Desvanecer", L"Uitfaden", L"Zanikanie", L"Soluklaştır"));
			m_jacket.SetWindowText(LL14(L"ジャケット", L"Jacket", L"Pochette", L"Copertina", L"Caratula", L"자켓", L"封面", L"الغلاف", L"Обложка", L"Cover", L"Capa", L"Omslag", L"Okładka", L"Kapak"));
		}
		if (shortLv >= 3) {
			if (m_eq.GetSafeHwnd())
				m_eq.SetWindowText(LL14(L"EQ", L"EQ", L"Égal.", L"EQ", L"Ecual.", L"EQ", L"均衡", L"معادل", L"Экв.", L"EQ", L"Equal.", L"EQ", L"Kor.", L"Ekol."));
			if (m_piano.GetSafeHwnd())
				m_piano.SetWindowText(LL14(L"ロ", L"PR", L"PR", L"PR", L"PR", L"롤", L"卷", L"رول", L"Рл", L"PR", L"PR", L"PR", L"PR", L"PR"));
			if (m_analyzer.GetSafeHwnd())
				m_analyzer.SetWindowText(LL14(L"ア", L"A", L"A", L"A", L"A", L"아", L"析", L"أ", L"А", L"A", L"A", L"A", L"A", L"A"));
		}
		else if (shortLv >= 1) {
			if (m_eq.GetSafeHwnd())
				m_eq.SetWindowText(LL14(L"EQ", L"EQ", L"Égal.", L"EQ", L"Ecual.", L"EQ", L"均衡", L"معادل", L"Экв.", L"EQ", L"Equal.", L"EQ", L"Kor.", L"Ekol."));
			if (m_piano.GetSafeHwnd())
				m_piano.SetWindowText(LL14(L"ロール", L"Roll", L"Rouleau", L"Roll", L"Rollo", L"롤", L"卷帘", L"رول", L"Ролл", L"Rolle", L"Rolo", L"Rol", L"Rolka", L"Rulo"));
			if (m_analyzer.GetSafeHwnd())
				m_analyzer.SetWindowText(LL14(L"アナ", L"Ana", L"Ana", L"Ana", L"Ana", L"아나", L"分析", L"محلل", L"Ана", L"Ana", L"Ana", L"Ana", L"Ana", L"Ana"));
		}
		else {
			if (m_eq.GetSafeHwnd())
				m_eq.SetWindowText(LL14(L"イコライザー", L"Equalizer", L"Egaliseur", L"Equalizzatore", L"Ecualizador", L"이퀄라이저", L"均衡器", L"المعادل", L"Эквалайзер", L"Equalizer", L"Equalizador", L"Equalizer", L"Korektor", L"Ekolayzer"));
			if (m_piano.GetSafeHwnd())
				m_piano.SetWindowText(LL14(L"簡易ピアノロール", L"Simple Piano Roll", L"Rouleau piano simple", L"Piano roll semplice", L"Rollo piano simple", L"간이 피아노 롤", L"简易钢琴卷帘", L"لوحة بيانو بسيطة", L"Простой пианоролл", L"Einfache Klavierrolle", L"Piano roll simples", L"Eenvoudige pianorol", L"Prosta rolka pianina", L"Basit piyano rulosu"));
			if (m_analyzer.GetSafeHwnd())
				m_analyzer.SetWindowText(LL14(L"アナライザー", L"Analyzer", L"Analyseur", L"Analizzatore", L"Analizador", L"분석기", L"分析器", L"المحلل", L"Анализатор", L"Analysator", L"Analisador", L"Analyser", L"Analizator", L"Analizor"));
		}
		ApplyPauseButtonLabel();
	}

	int bx = M;
	MoveCtl(&m_prev, bx, by, prevW, bh); bx += prevW + gap;
	MoveCtl(&m_play, bx, by, playW, bh); bx += playW + gap;
	MoveCtl(&m_pause, bx, by, pauseW, bh); bx += pauseW + gap;
	MoveCtl(&m_stop, bx, by, stopW, bh); bx += stopW + gap;
	MoveCtl(&m_fadeout, bx, by, fadeW, bh); bx += fadeW + gap;
	MoveCtl(&m_next, bx, by, nextW, bh); bx += nextW + (int)(8 * s);
	MoveCtl(&m_jacket, bx, by, jkw, bh); bx += jkw + gap;
	MoveCtl(&m_eq, bx, by, ebw, bh); bx += ebw + gap;
	MoveCtl(&m_piano, bx, by, pbw, bh); bx += pbw + gap;
	if (m_analyzer.GetSafeHwnd()) {
		MoveCtl(&m_analyzer, bx, by, abw, bh);
		bx += abw + gap;
	}
	MoveCtl(&m_vollabel, volLblX, by + (int)(5 * s), volLblW, (int)(15 * s));
	MoveCtl(&m_vol, volSlX, by + (int)(4 * s), volSlW, (int)(16 * s));
	MoveCtl(&m_volval, volvalX, by + (int)(5 * s), volValW, (int)(16 * s));

	// ===== オプション行(1段): 連続/ループ/回数/ランダム + スペアナ/ST/フォルダ =====
	// chkRowH は 12px フォント(約 tmHeight≈16)が DrawSmartText2 で縮小されない高さにする。
	int by2 = by + bh + (int)(4 * s);
	int ch = (int)(24 * s);
	int chkRowH = (int)(20 * s);
	int optY = by2 + (ch - chkRowH) / 2;
	int cx = M;
	MoveCtl(&m_renzoku, cx, optY, (int)(86 * s), chkRowH); cx += (int)(90 * s);
	MoveCtl(&m_loop, cx, optY, (int)(92 * s), chkRowH); cx += (int)(96 * s);
	MoveCtl(&m_kaisuuL, cx, optY, (int)(80 * s), chkRowH); cx += (int)(82 * s);
	MoveCtl(&m_kaisuu, cx, optY, (int)(36 * s), chkRowH); cx += (int)(40 * s);
	MoveCtl(&m_random, cx, optY, (int)(98 * s), chkRowH);
	int folW = (int)(54 * s), stW = (int)(72 * s), supeW = (int)(62 * s);
	const int prWFull = max(1, (int)(76 * s));
	const int prWShort = max(1, (int)(36 * s));
	const int randomEndX = M + (int)(90 * s) + (int)(96 * s) + (int)(82 * s) + (int)(40 * s) + (int)(98 * s);
	int btnRowH = (int)(24 * s);
	int btnY1 = by2 + (ch - btnRowH) / 2;
	int rcx = W - M - folW;
	MoveCtl(&m_folder, rcx, by2, folW, ch); rcx -= (int)(4 * s) + stW;
	MoveCtl(&m_st, rcx, btnY1, stW, btnRowH); rcx -= (int)(4 * s) + supeW;
	MoveCtl(&m_supe, rcx, btnY1, supeW, btnRowH);
	const int prGap = (int)(8 * s);
	const bool prUseFull = (rcx - prGap - prWFull >= randomEndX);
	const int prW = prUseFull ? prWFull : prWShort;
	if (m_prompt.GetSafeHwnd()) {
		const int prShortLv = prUseFull ? 0 : 1;
		if (prShortLv != m_mpPromptShort) {
			m_mpPromptShort = prShortLv;
			m_prompt.SetWindowText(prUseFull
				? LL14(L"プロンプト", L"Prompt", L"Prompt", L"Prompt", L"Prompt", L"프롬프트", L"提示", L"موجه", L"Промпт", L"Prompt", L"Prompt", L"Prompt", L"Prompt", L"Istem")
				: LL14(L"プロ", L"Pr", L"Pr", L"Pr", L"Pr", L"프", L"示", L"مو", L"Пр", L"Pr", L"Pr", L"Pr", L"Pr", L"Pr"));
		}
		rcx -= (int)(4 * s) + prW;
		MoveCtl(&m_prompt, rcx, btnY1, prW, btnRowH);
	}

	// ===== サウンドグループ: 設定 + DS/拡張/テンポ/ピッチ(1段で省スペース) =====
	int sndTop = by2 + ch + (int)(5 * s);
	int sy = sndTop + gTitle;
	int slLabelH = (int)(15 * s), slH = (int)(16 * s);   // ラベル(13px)が収まる高さ
	MoveCtl(&m_settings, M + gPad, sy + (int)(4 * s), (int)(48 * s), (int)(24 * s));
	int slX = M + gPad + (int)(54 * s);
	int slGap = (int)(8 * s);
	int slW = (W - M - gPad - slX - slGap * 3) / 4;
	if (slW < (int)(56 * s)) slW = (int)(56 * s);
	struct { CCustomStatic* lbl; CCustomSliderCtrl* sl; } snd[4] = {
		{ &m_dsvolL, &m_dsvol }, { &m_kvolL, &m_kvol }, { &m_tempoL, &m_tempo }, { &m_pitchL, &m_pitch }
	};
	for (int i = 0; i < 4; i++) {
		int cxs = slX + i * (slW + slGap);
		MoveCtl(snd[i].lbl, cxs, sy, slW, slLabelH);
		MoveCtl(snd[i].sl, cxs, sy + slLabelH + (int)(1 * s), slW, slH);
		if (i == 0) m_dsvolSlW = slW;
	}
	int sndBottom = sy + slLabelH + slH + (int)(1 * s) + gPad;
	MoveCtl(&m_grpSnd, M, sndTop, W - M * 2, sndBottom - sndTop);

	// ===== プレイリストグループ: ツールバー + リスト + 下部チェック =====
	int plTop = sndBottom + (int)(5 * s);
	int by4 = plTop + gTitle;
	int tbH = (int)(19 * s);
	int comboW = (int)(120 * s);
	LayoutPlselCombo(M + gPad, by4, comboW, tbH, s);
	int tx = M + gPad + comboW + (int)(5 * s);
	int tbw = (int)(50 * s);
	MoveCtl(&m_plrename, tx, by4, tbw, tbH); tx += tbw + (int)(3 * s);
	MoveCtl(&m_pldelete, tx, by4, tbw, tbH); tx += tbw + (int)(4 * s);
	int m3uw = (int)(44 * s);
	MoveCtl(&m_m3uExport, tx, by4, m3uw, tbH); tx += m3uw + (int)(2 * s);
	MoveCtl(&m_m3uImport, tx, by4, m3uw, tbH); tx += m3uw + (int)(6 * s);
	int ibw = (int)(16 * s);
	int findW = (int)(86 * s);
	MoveCtl(&m_find, tx, by4 + (int)(1 * s), findW, tbH - (int)(2 * s)); tx += findW + (int)(2 * s);
	MoveCtl(&m_finddown, tx, by4, ibw, tbH); tx += ibw + (int)(1 * s);
	MoveCtl(&m_findup, tx, by4, ibw, tbH);
	int delGap = (int)(24 * s);
	int delW = (int)(50 * s);
	int delX = W - M - gPad - delW;
	int moveRight = delX - delGap;
	MoveCtl(&m_itemdel, delX, by4, delW, tbH);
	MoveCtl(&m_lsdown, moveRight - ibw, by4, ibw, tbH);
	MoveCtl(&m_down, moveRight - ibw * 2 - (int)(1 * s), by4, ibw, tbH);
	MoveCtl(&m_up, moveRight - ibw * 3 - (int)(2 * s), by4, ibw, tbH);
	MoveCtl(&m_lsup, moveRight - ibw * 4 - (int)(3 * s), by4, ibw, tbH);

	int swH = (int)(22 * s);
	int listY = by4 + tbH + (int)(4 * s);
	const int botY = H - swH - M + (int)(2 * s);
	const int ckY = botY - (int)(8 * s) - chkRowH;
	int listH = ckY - (int)(3 * s) - listY;
	if (listH < (int)(50 * s)) listH = (int)(50 * s);
	MoveCtl(&m_list, M + gPad, listY, W - M * 2 - gPad * 2, listH);

	// アルバム/コメント列(最終列=4)をリスト右端へぴたりとフィットさせる
	FitPlaylistLastColumn();

	// 下部チェック(ツールチップ〜曲保存): 均等スロット幅に収まる最長ラベルを実測で選ぶ
	// CCustomCheckBox::OnDrawLayer と同じ箱サイズ/余白で必要幅 = 箱 + 8 + 文字幅 + 右余白
	int availCk = W - (M + gPad) * 2;
	int gapCk = (int)(5 * s);
	int ckW = (availCk - gapCk * 5) / 6;
	if (ckW < 1) ckW = 1;
	int boxS = chkRowH - 4;
	if (boxS > 18) boxS = 18;
	if (boxS < 14) boxS = 14;
	const int ckExtra = boxS + 8 + 4;

	CClientDC cdc(this);
	CFont* pOldChkF = nullptr;
	if (m_fontChk.GetSafeHandle())
		pOldChkF = cdc.SelectObject(&m_fontChk);
	auto ckNeed = [&](LPCTSTR t) -> int {
		if (!t || !*t) return ckExtra;
		return ckExtra + cdc.GetTextExtent(t).cx;
	};
	auto ckApply = [&](CCustomCheckBox& ctl, int idx, LPCTSTR full, LPCTSTR mid, LPCTSTR sh) {
		LPCTSTR use = sh;
		int lv = 2;
		if (ckNeed(full) <= ckW) { use = full; lv = 0; }
		else if (ckNeed(mid) <= ckW) { use = mid; lv = 1; }
		if (m_mpChkShort[idx] != lv) {
			m_mpChkShort[idx] = lv;
			ctl.SetWindowText(use);
		}
	};

	// tip: ツールチップ → チップ → チ
	ckApply(m_tip, 0,
		LL14(L"ツールチップ", L"Tooltips", L"Info-bulles", L"Suggerimenti", L"Sugerencias", L"툴팁", L"工具提示", L"تلميحات", L"Подсказки", L"Tooltips", L"Dicas", L"Tooltips", L"Etykiety", L"İpuçları"),
		LL14(L"チップ", L"Tips", L"Bulles", L"Sugger.", L"Tips", L"팁", L"提示", L"تلميح", L"Подск.", L"Tips", L"Dicas", L"Tips", L"Etyk.", L"İpucu"),
		LL14(L"チ", L"Tip", L"Tip", L"Tip", L"Tip", L"팁", L"提", L"تل", L"Пд", L"Tip", L"Dic", L"Tip", L"Et", L"İp"));
	// mini: 最小化連動 → 最小化 → 最小
	ckApply(m_mini, 1,
		LL14(L"最小化連動", L"Min. sync", L"Sync. min.", L"Sinc. min.", L"Sincr. min.", L"최소화 연동", L"最小化联动", L"تزامن التصغير", L"Синхр. сверт.", L"Min.-Sync", L"Sinc. min.", L"Min. koppelen", L"Synch. min.", L"Min. eşitle"),
		LL14(L"最小化", L"Minimize", L"Réduire", L"Riduci", L"Minimizar", L"최소화", L"最小化", L"تصغير", L"Свернуть", L"Minimieren", L"Minimizar", L"Minimaliseren", L"Minimalizuj", L"Küçült"),
		LL14(L"最小", L"Min", L"Min", L"Min", L"Min", L"최소", L"最小", L"تص", L"Свр", L"Min", L"Min", L"Min", L"Min", L"Min"));
	// mp3途中保存 → mp3保存 → mp3
	ckApply(m_savemp3, 2,
		LL14(L"mp3途中保存", L"mp3 resume", L"mp3 reprise", L"mp3 ripresa", L"mp3 reanudar", L"mp3 위치저장", L"mp3续播", L"حفظ موضع mp3", L"mp3 позиция", L"mp3 Position", L"mp3 retomar", L"mp3 hervat", L"mp3 wznow", L"mp3 sürdür"),
		LL14(L"mp3保存", L"mp3 save", L"mp3 sauver", L"mp3 salva", L"mp3 guardar", L"mp3 저장", L"mp3保存", L"حفظ موضع mp3", L"mp3 сохр.", L"mp3 speichern", L"mp3 salvar", L"mp3 opslaan", L"mp3 zapis", L"mp3 kaydet"),
		LL14(L"mp3", L"mp3", L"mp3", L"mp3", L"mp3", L"mp3", L"mp3", L"mp3", L"mp3", L"mp3", L"mp3", L"mp3", L"mp3", L"mp3"));
	// DShow途中保存 → DShow保存 → DS
	ckApply(m_saveds, 3,
		LL14(L"DShow途中保存", L"DShow resume", L"DShow reprise", L"DShow ripresa", L"DShow reanudar", L"DShow 위치저장", L"DShow续播", L"حفظ موضع DShow", L"DShow позиция", L"DShow Position", L"DShow retomar", L"DShow hervat", L"DShow wznow", L"DShow sürdür"),
		LL14(L"DShow保存", L"DShow save", L"DShow sauver", L"DShow salva", L"DShow guardar", L"DShow 저장", L"DShow保存", L"حفظ DS", L"DShow сохр.", L"DShow speichern", L"DShow salvar", L"DShow opslaan", L"DShow zapis", L"DShow kaydet"),
		LL14(L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS"));
	// WAVファイルへ保存 → WAV保存 → WAV
	ckApply(m_savewav, 4,
		LL14(L"WAVファイルへ保存", L"Save to WAV file", L"Enregistrer en WAV", L"Salva come WAV", L"Guardar como WAV", L"WAV 파일로 저장", L"保存到WAV文件", L"حفظ كـ WAV", L"Сохранить в WAV", L"Als WAV speichern", L"Salvar como WAV", L"Opslaan als WAV", L"Zapisz jako WAV", L"WAV olarak kaydet"),
		LL14(L"WAV保存", L"Save WAV", L"Sauver WAV", L"Salva WAV", L"Guardar WAV", L"WAV 저장", L"WAV保存", L"حفظ WAV", L"WAV сохр.", L"WAV speichern", L"Salvar WAV", L"WAV opslaan", L"Zapis WAV", L"WAV kaydet"),
		LL14(L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV"));
	// 曲ごとに設定保存 → 曲ごと保存 → 曲保存
	ckApply(m_saveparam, 5,
		LL14(L"曲ごとに設定保存", L"Save per-song", L"Réglages/morceau", L"Impost. per brano", L"Ajustes por pista", L"곡별 설정 저장", L"逐曲保存设置", L"حفظ لكل أغنية", L"Настройки на трек", L"Pro Titel speichern", L"Config. por faixa", L"Per nummer opslaan", L"Ustaw. na utwor", L"Parça başına kaydet"),
		LL14(L"曲ごと保存", L"Per-song", L"Par morceau", L"Per brano", L"Por pista", L"곡별 저장", L"逐曲保存", L"لكل أغنية", L"На трек", L"Pro Titel", L"Por faixa", L"Per nummer", L"Na utwor", L"Parça kaydet"),
		LL14(L"曲保存", L"Song save", L"Mém. piste", L"Salva brano", L"Guarda pista", L"곡저장", L"曲保存", L"أغنية", L"Трек", L"Titel", L"Faixa", L"Nummer", L"Utwór", L"Parça"));

	if (pOldChkF)
		cdc.SelectObject(pOldChkF);

	int ckx = M + gPad;
	MoveCtl(&m_tip, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_mini, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_savemp3, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_saveds, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_savewav, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_saveparam, ckx, ckY, ckW, chkRowH);
	int plBottom = ckY + chkRowH + gPad;
	MoveCtl(&m_grpPl, M, plTop, W - M * 2, plBottom - plTop);

	// 最下部: 切替(左) / 保存リセット(切替の右) / 終了(右)  ※ジャケは操作行へ移動済み
	int swW = (int)(140 * s);
	MoveCtl(&m_switch, M, botY, swW, swH);
	int rsW = (int)(120 * s);
	MoveCtl(&m_resetdata, M + swW + (int)(6 * s), botY, rsW, swH);
	int exW = (int)(80 * s);
	MoveCtl(&m_exit, W - M - exW, botY, exW, swH);

	CCC_GroupBoxesBack(GetSafeHwnd());   // 枠は最背面(子コントロールを覆わない)
	Invalidate();
}

void CMediaPlayerDlg::FitPlaylistLastColumn(int dragCol, int dragWidth)
{
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	CRect lcr;
	m_list.GetClientRect(&lcr);
	const int clientW = lcr.Width();
	if (clientW <= 0) return;

	// 最終列(5=アルバム)以外の幅合計。ドラッグ中の列は仮幅を使う。
	const BOOL bDragOther = (dragCol >= 0 && dragCol < 5 && dragWidth > 0);

	int used = 0;
	for (int ci = 0; ci < 5; ++ci) {
		if (bDragOther && ci == dragCol)
			used += dragWidth;
		else
			used += m_list.GetColumnWidth(ci);
	}

	const int minLast = (int)(80 * hD2);
	int last = clientW - used;
	if (last < minLast) last = minLast;

	if (bDragOther) {
		const int curDrag = m_list.GetColumnWidth(dragCol);
		if (curDrag != dragWidth)
			m_list.SetColumnWidth(dragCol, dragWidth);
	}

	if (m_list.GetColumnWidth(5) != last)
		m_list.SetColumnWidth(5, last);

	if (CHeaderCtrl* pHdr = m_list.GetHeaderCtrl())
		pHdr->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
	if (bDragOther)
		m_list.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_NOERASE);
}

void CMediaPlayerDlg::TickListHdrDragFit()
{
	if (m_listHdrDragCol < 0 || m_listHdrDragCol >= 5) return;
	CHeaderCtrl* pHdr = m_list.GetHeaderCtrl();
	if (!pHdr) return;
	HDITEM hi = {};
	hi.mask = HDI_WIDTH;
	if (pHdr->GetItem(m_listHdrDragCol, &hi) && hi.cxy > 0)
		FitPlaylistLastColumn(m_listHdrDragCol, hi.cxy);
}

void CMediaPlayerDlg::OnPlaylistHeaderNotify(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (pResult) *pResult = 0;
	if (!pNMHDR) {
		FitPlaylistLastColumn();
		return;
	}

	HD_NOTIFY* phd = reinterpret_cast<HD_NOTIFY*>(pNMHDR);
	const UINT code = pNMHDR->code;
	int dragCol = -1;
	int dragCx = -1;
	if (phd && phd->iItem >= 0 && phd->iItem < 5) {
		dragCol = phd->iItem;
		if (phd->pitem)
			dragCx = phd->pitem->cxy;
	}

	switch (code) {
	case HDN_BEGINTRACKA:
	case HDN_BEGINTRACKW:
		if (phd) {
			m_listHdrDragCol = phd->iItem;
			if (phd->iItem >= 0 && phd->iItem < 5)
				SetTimer(kTimerListHdrDrag, 16, NULL);
		}
		return;
	case HDN_TRACKA:
	case HDN_TRACKW:
		if (phd && phd->iItem == 5)
			return;
		if (dragCol >= 0 && dragCx > 0)
			FitPlaylistLastColumn(dragCol, dragCx);
		return;
	case HDN_ENDTRACKA:
	case HDN_ENDTRACKW:
		KillTimer(kTimerListHdrDrag);
		m_listHdrDragCol = -1;
		FitPlaylistLastColumn();
		return;
	default:
		break;
	}
}

void CMediaPlayerDlg::OnListHeaderEndTrack(NMHDR* pNMHDR, LRESULT* pResult)
{
	OnPlaylistHeaderNotify(pNMHDR, pResult);
}

void CMediaPlayerDlg::SyncPushToggleButtons()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	const int supeOn = og->m_supe.GetCheck() ? 1 : 0;
	const int stOn = og->m_st.GetCheck() ? 1 : 0;
	const int eqOpen = (::IsWindow(og->m_EqualizerDlg->GetSafeHwnd()) && ::IsWindowVisible(og->m_EqualizerDlg->m_hWnd)) ? 1 : 0;
	const int pianoOpen = (::IsWindow(og->m_PianoRollDlg->GetSafeHwnd()) && ::IsWindowVisible(og->m_PianoRollDlg->m_hWnd)) ? 1 : 0;
	const int analyzerOpen = (::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd()) && ::IsWindowVisible(og->m_AnalyzerDlg->m_hWnd)) ? 1 : 0;
	if (supeOn != m_lastToggleSupe) {
		MpSetPushToggle(m_supe, supeOn, RGB(140, 220, 160), RGB(80, 180, 110), RGB(215, 240, 220), RGB(175, 215, 190));
		m_lastToggleSupe = supeOn;
	}
	if (stOn != m_lastToggleSt) {
		MpSetPushToggle(m_st, stOn, RGB(160, 200, 255), RGB(100, 150, 230), RGB(215, 230, 255), RGB(175, 200, 245));
		m_lastToggleSt = stOn;
	}
	if (eqOpen != m_lastToggleEq) {
		MpSetPushToggle(m_eq, eqOpen, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
		m_lastToggleEq = eqOpen;
	}
	if (pianoOpen != m_lastTogglePiano) {
		MpSetPushToggle(m_piano, pianoOpen, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
		m_lastTogglePiano = pianoOpen;
	}
	if (m_analyzer.GetSafeHwnd() && analyzerOpen != m_lastToggleAnalyzer) {
		MpSetPushToggle(m_analyzer, analyzerOpen, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
		m_lastToggleAnalyzer = analyzerOpen;
	}
}

BOOL CMediaPlayerDlg::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	LPNMHDR pN = reinterpret_cast<LPNMHDR>(lParam);
	if (pN && ::IsWindow(m_list.GetSafeHwnd())) {
		HWND hHdr = m_list.GetHeaderCtrl() ? m_list.GetHeaderCtrl()->GetSafeHwnd() : NULL;
		if (hHdr && pN->hwndFrom == hHdr) {
			switch (pN->code) {
			case HDN_BEGINTRACKA:
			case HDN_BEGINTRACKW:
			case HDN_ENDTRACKA:
			case HDN_ENDTRACKW:
			case HDN_TRACKA:
			case HDN_TRACKW:
				OnPlaylistHeaderNotify(pN, pResult);
				return TRUE;
			}
		}
	}
	return CCustomBlurDialogExBase::OnNotify(wParam, lParam, pResult);
}

// 手動レイアウト後に仮想リストのスクロール範囲と Z 順を再確定する。
// OnSize では RedrawWindow するが、歌詞モード切替など DoLayout 単独呼び出し時は必要。
void CMediaPlayerDlg::RefreshListAfterLayout()
{
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	if (pl && pl->playcnt > 0) {
		int anchor = GetListScrollAnchor();
		m_list.SetItemCount(pl->playcnt);
		m_lastCount = pl->playcnt;
		RestoreListScrollAnchor(anchor);
	}
	m_list.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	m_list.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
}

// プレイリスト(pl)と同じく仮想リスト(LVS_OWNERDATA)としてミラーする。
// 仮想リストは SetItemCount でスクロール範囲(=スクロールバー)が直接確定するため、
// アクリル時に OpaqueFixer が WM_PAINT を横取りしてもスクロールバーが正しく表示される。
// (非仮想だと WM_PAINT 依存でスクロールバーが出ない/消える不具合になっていた。)
// 仮想リスト(LVS_OWNERDATA)の件数変化・再生中アイコン移動を反映する。
// Timer1(250ms)から呼ばれる。件数が同じなら SetItemCount は呼ばずコストを最小化する。
// bForce=TRUE は並べ替え/タグ更新時など表示内容が変わった場合に全行再取得を強制する。
void CMediaPlayerDlg::RefreshList(BOOL bForce)
{
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	if (!pl || pl->pc == NULL) {
		if (m_list.GetItemCount() > 0) { m_list.SetItemCount(0); m_lastCount = 0; }
		return;
	}
	m_list.pc = pl->pc; // Load/realloc 後の実体ポインタを再同期
	int cnt = pl->playcnt;

	// 件数変化 or 強制(並べ替え/タグ更新/追加削除)時に範囲を再設定。
	if (bForce || cnt != m_lastCount) {
		int anchor = GetListScrollAnchor();
		m_list.SetItemCount(cnt);   // 仮想リスト: スクロール範囲を確定(pl と同じ仕組み)
		m_lastCount = cnt;
		if (cnt > 0)
			RestoreListScrollAnchor(anchor);   // SetItemCount で先頭へ戻るのを防ぐ
		if (bForce) m_list.Invalidate(FALSE);   // 表示内容(順序/タグ)の変化を反映
	}

	// 再生中(♪)アイコンの点滅・移動を反映(該当行だけ再取得=GetDispInfo 再問合せ)。
	// Timer1(250ms)毎に無条件で RedrawItems すると、pl 側の点滅周期(1200ms)とずれて
	// 行が小刻みに再描画されちらつく(=ぎこちなく見える)。アイコン値が実際に
	// 変化したフレームだけ再描画して、pl 同様のなめらかな点滅にする。
	int pnt = pl->pnt;
	if (pnt != m_lastPlcnt) {
		if (m_lastPlcnt >= 0 && m_lastPlcnt < cnt) m_list.RedrawItems(m_lastPlcnt, m_lastPlcnt);
		m_lastPlcnt = pnt;
		m_lastPlayIcon = -999;   // 行が変わったら次回必ず再描画
	}
	if (pnt >= 0 && pnt < cnt) {
		int ic = pl->pc[pnt].icon;
		if (ic != m_lastPlayIcon) {
			m_list.RedrawItems(pnt, pnt);
			m_lastPlayIcon = ic;
		}
	}

	FollowPlayingRow();   // ♪ 行へカーソル追従
}

void CMediaPlayerDlg::NotifyPlayIconChanged()
{
	// PlayList::SIconTimer から呼ばれる。Timer1(250ms)待ちだと点滅が間引かれて飛び飛びに見える。
	if (!::IsWindow(m_list.GetSafeHwnd()) || !pl || pl->pc == NULL) return;
	const int cnt = pl->playcnt;
	const int pnt = pl->pnt;
	if (pnt < 0 || pnt >= cnt) return;
	const int ic = pl->pc[pnt].icon;
	if (pnt != m_lastPlcnt) {
		if (m_lastPlcnt >= 0 && m_lastPlcnt < cnt)
			m_list.RedrawItems(m_lastPlcnt, m_lastPlcnt);
		m_lastPlcnt = pnt;
		m_lastPlayIcon = -999;
	}
	if (ic != m_lastPlayIcon) {
		m_list.RedrawItems(pnt, pnt);
		m_lastPlayIcon = ic;
	}
}

// 仮想リスト(LVS_OWNERDATA)の表示内容を pl->pc から供給する(pl の同名処理と同等)。
void CMediaPlayerDlg::OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVDISPINFO* di = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
	*pResult = 0;
	if (di == NULL || !pl || pl->pc == NULL || pl->playcnt <= 0) return;
	int i = di->item.iItem;
	if (i < 0 || i >= pl->playcnt) i = 0;
	const playlistdata0& d = pl->pc[i];
	if (di->item.mask & LVIF_TEXT) {
		switch (di->item.iSubItem) {
		case 0: _tcscpy_s(di->item.pszText, di->item.cchTextMax, d.name); break;
		case 1: _tcscpy_s(di->item.pszText, di->item.cchTextMax, SongParams_HasEntryForRow(i) ? _T("★") : _T("")); break;
		case 2: _tcscpy_s(di->item.pszText, di->item.cchTextMax, d.game); break;
		case 3: {
			CString s;
			if (d.time == 0) s = _T("");
			else if (d.time == -1) s = LL14(L"取得不能", L"N/A", L"N/D", L"N/D", L"N/D", L"해당 없음", L"不可用", L"غ/م", L"Н/Д", L"k. A.", L"N/D", L"N.v.t.", L"Brak", L"Yok");
			else if (d.time >= 3600) s.Format(_T("%d:%02d:%02d"), d.time / 3600, (d.time / 60) % 60, d.time % 60);
			else s.Format(_T("%d:%02d"), d.time / 60, d.time % 60);
			_tcsncpy_s(di->item.pszText, di->item.cchTextMax, s, _TRUNCATE);
		} break;
		case 4: _tcscpy_s(di->item.pszText, di->item.cchTextMax, d.art); break;
		case 5: _tcscpy_s(di->item.pszText, di->item.cchTextMax, d.alb); break;
		default: break;
		}
	}
	if (di->item.mask & LVIF_IMAGE)
		di->item.iImage = d.icon;
}

void CMediaPlayerDlg::OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	if (plf) return;   // 再生中は表示を差し替えない
	const NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	if (!p || !(p->uChanged & LVIF_STATE)) return;
	if (!((p->uNewState ^ p->uOldState) & LVIS_SELECTED)) return;
	if (!(p->uNewState & LVIS_SELECTED)) return;
	const int i = p->iItem;
	if (!pl || i < 0 || i >= pl->playcnt) return;
	pl->Get(i);
	plcnt = i;
	m_lastScroll = i;
}

int CMediaPlayerDlg::GetListScrollAnchor() const
{
	if (!pl || pl->playcnt <= 0) return 0;
	if (pl->pnt1 >= 0 && pl->pnt1 < pl->playcnt) return pl->pnt1;
	if (plcnt >= 0 && plcnt < pl->playcnt) return plcnt;
	if (::IsWindow(m_list.GetSafeHwnd())) {
		int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
		if (sel >= 0 && sel < pl->playcnt) return sel;
	}
	if (::IsWindow(pl->m_lc.GetSafeHwnd())) {
		int sel = pl->m_lc.GetNextItem(-1, LVNI_SELECTED);
		if (sel >= 0 && sel < pl->playcnt) return sel;
		int top = pl->m_lc.GetTopIndex();
		if (top >= 0 && top < pl->playcnt) return top;
	}
	if (pl->pnt >= 0 && pl->pnt < pl->playcnt) return pl->pnt;
	return 0;
}

void CMediaPlayerDlg::RestoreListScrollAnchor(int anchor)
{
	if (!::IsWindow(m_list.GetSafeHwnd()) || !pl || pl->playcnt <= 0) return;
	if (anchor < 0) anchor = 0;
	if (anchor >= pl->playcnt) anchor = pl->playcnt - 1;
	for (int k = -1; (k = m_list.GetNextItem(k, LVNI_SELECTED)) != -1; )
		m_list.SetItemState(k, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.SetItemState(anchor, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.EnsureVisible(anchor, FALSE);
	m_lastScroll = anchor;   // FollowPlayingRow が直後の選択を上書きしない
}

void CMediaPlayerDlg::InitListScrollPosition()
{
	if (!::IsWindow(m_list.GetSafeHwnd()) || !pl || pl->playcnt <= 0) return;
	int anchor = GetListScrollAnchor();
	RestoreListScrollAnchor(anchor);
	// FollowPlayingRow の初回強制追従を抑え、♪行が変わった時だけ追従する
	if (pl->pnt >= 0 && pl->pnt < pl->playcnt)
		m_lastScroll = pl->pnt;
	else
		m_lastScroll = anchor;
}

// 再生中(♪)の行へカーソル(選択)を移動して可視化する。
// ♪ の行は pl->pnt(SIcon が pc[pnt].icon を再生中アイコンへ切替えている)。
// 項目挿入後に呼ぶこと。pnt が変わった時のみ追従し、同一曲中のユーザー選択は邪魔しない。
// 再生中(♪)行へスクロールして選択する。pl->pnt が変化した場合のみ動作する。
// 起動時の位置復元は InitListScrollPosition() を使う。
void CMediaPlayerDlg::FollowPlayingRow()
{
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	if (!pl || pl->pc == NULL) return;
	// あいまい検索で別行を選択中(pnt1)は、再生行(♪)への強制追従をしない
	if (pl->pnt1 != -1) return;
	int cnt = pl->playcnt;
	int play = pl->pnt;
	if (play < 0 || play >= cnt) return;
	if (play == m_lastScroll) return;
	if (m_list.GetItemCount() < cnt) return;   // 項目未挿入なら何もしない
	int k = -1;
	while ((k = m_list.GetNextItem(k, LVNI_SELECTED)) != -1)
		m_list.SetItemState(k, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.SetItemState(play, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.EnsureVisible(play, FALSE);
	m_lastScroll = play;
}

// m_mpBtnShort と ps に応じて一時停止/再開ラベルを設定する。
// og->m_ps のフル文言をそのままミラーすると短縮段階が潰れるため、こちらで段階別文言を選ぶ。
void CMediaPlayerDlg::ApplyPauseButtonLabel()
{
	if (!::IsWindow(m_pause.GetSafeHwnd())) return;
	CString text;
	if (ps == 1) {
		if (m_mpBtnShort >= 3)
			text = LL14(L"再", L">", L">", L">", L">", L"재", L"继", L">", L">", L">", L">", L">", L">", L">");
		else
			text = LL14(L"再開", L"Resume", L"Reprendre", L"Riprendi", L"Reanudar", L"재개", L"恢复", L"استئناف", L"Продолжить", L"Fortsetzen", L"Retomar", L"Hervatten", L"Wznów", L"Sürdür");
	}
	else if (m_mpBtnShort >= 3) {
		text = LL14(L"停", L"||", L"||", L"||", L"||", L"정", L"停", L"||", L"||", L"||", L"||", L"||", L"||", L"||");
	}
	else if (m_mpBtnShort >= 2) {
		text = LL14(L"一時停", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시정", L"暂停", L"إيقاف", L"Пауза", L"Pause", L"Pausa", L"Pauze", L"Pauza", L"Duraklat");
	}
	else {
		text = LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시정지", L"暂停", L"إيقاف مؤقت", L"Пауза", L"Pause", L"Pausar", L"Pauze", L"Pauza", L"Duraklat");
	}
	CString cur;
	m_pause.GetWindowText(cur);
	if (cur != text) {
		m_pause.SetWindowText(text);
		m_pause.RepaintClient();
	}
}

// og/pl の UI 状態(歌詞・スライダー位置・チェック状態・コンボ選択)をこの画面へ反映する。
// 差分のみ SetWindowText / SetCheck するのはちらつき防止のため。
// Timer1(250ms)から定期呼び出しされるほか、コントロール操作直後にも都度呼ぶ。
void CMediaPlayerDlg::SyncFromMain()
{
	if (!::IsWindow(GetSafeHwnd())) return;

	// タイトル/アーティスト/アルバムはバナーGDIに表示されるためここでは更新しない。

	// 歌詞(5行) / OS / CPU (og からそのまま引き継ぐ)
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		CString s, s2;
		const bool hasLyrics = (og->lrcnum >= 2);
		og->m_lrc.GetWindowText(s); m_lrc.GetWindowText(s2); if (s != s2) m_lrc.SetWindowText(s);
		og->m_lrc2.GetWindowText(s); m_lrc2.GetWindowText(s2); if (s != s2) m_lrc2.SetWindowText(s);
		og->m_lrc3.GetWindowText(s); m_lrc3.GetWindowText(s2); if (s != s2) m_lrc3.SetWindowText(s);
		if (hasLyrics) {
			og->m_lrc4.GetWindowText(s); m_lrc4.GetWindowText(s2); if (s != s2) m_lrc4.SetWindowText(s);
			og->m_lrc5.GetWindowText(s); m_lrc5.GetWindowText(s2); if (s != s2) m_lrc5.SetWindowText(s);
		}
		if (!hasLyrics) {
			og->m_OS.GetWindowText(s); m_os.GetWindowText(s2); if (s != s2) m_os.SetWindowText(s);
			og->m_cpu.GetWindowText(s); m_cpu.GetWindowText(s2); if (s != s2) m_cpu.SetWindowText(s);
			og->m_os3.GetWindowText(s); m_os3.GetWindowText(s2); if (s != s2) m_os3.SetWindowText(s);
		}
		static int s_lastLyricsMode = -1;
		const int lyricsMode = hasLyrics ? 1 : 0;
		if (s_lastLyricsMode != lyricsMode) {
			s_lastLyricsMode = lyricsMode;
			DoLayout();
			CCC_GroupBoxesBack(GetSafeHwnd());
			RefreshListAfterLayout();
			RedrawWindow(NULL, NULL,
				RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		}

		// シーク/音量は高速タイマーで追従(ここでも一応呼ぶ)
		MirrorSeekVol();

		// サウンド調整(DS音量/拡張/テンポ/ピッチ)を og からミラー。ドラッグ中のものは触らない。
		CWnd* pf2 = GetFocus();
		HWND hf = pf2 ? pf2->GetSafeHwnd() : NULL;
		if (hf != m_dsvol.GetSafeHwnd()) m_dsvol.SetPos(og->m_dsval.GetPos());
		if (hf != m_kvol.GetSafeHwnd())  m_kvol.SetPos(og->m_kakuVol.GetPos());
		if (hf != m_tempo.GetSafeHwnd()) m_tempo.SetPos(og->m_tempo_sl.GetPos());
		if (hf != m_pitch.GetSafeHwnd()) m_pitch.SetPos(og->m_pitch_sl.GetPos());
		CString l;
		double dsp = (og->m_dsval.GetPos() + 499) * 2.0 / 10.0;
		CString dsLbl = (m_dsvolSlW >= (int)(92 * hD2))
			? LL14(L"DirectSound音量", L"DirectSound volume", L"Volume DirectSound", L"Volume DirectSound", L"Volumen DirectSound", L"DirectSound 음량", L"DirectSound音量", L"صوت DirectSound", L"DirectSound", L"DirectSound-Lautstarke", L"Volume DirectSound", L"DirectSound-volume", L"Głośność DirectSound", L"DirectSound sesi")
			: LL14(L"DS音量", L"DS volume", L"Volume DS", L"Volume DS", L"Volumen DS", L"DS 음량", L"DS音量", L"مستوى DS", L"Громкость DS", L"DS-Lautstarke", L"Volume DS", L"DS-volume", L"Głośność DS", L"DS sesi");
		l.Format(_T("!@C606868%s!@C206088 %.1f%%"), (LPCTSTR)dsLbl, dsp); m_dsvolL.GetWindowText(s2); if (l != s2) m_dsvolL.SetWindowText(l);
		{
			CString lbl = LL14(L"拡張音量", L"Extended volume", L"Volume etendu", L"Volume esteso", L"Volumen extendido", L"확장 음량", L"扩展音量", L"الصوت الموسع", L"Расшир. громкость", L"Erweiterte Lautstarke", L"Volume estendido", L"Uitgebreid volume", L"Rozszerzona głośność", L"Genisletilmis ses");
			l.Format(_T("!@C606868%s!@C904820 %.1f%%"), (LPCTSTR)lbl, (double)og->m_kakuVol.GetPos());
		}
		m_kvolL.GetWindowText(s2); if (l != s2) m_kvolL.SetWindowText(l);
		{
			CString lbl = LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"템포", L"速度", L"الإيقاع", L"Темп", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo");
			l.Format(_T("!@C606868%s!@C186878 %d%%"), (LPCTSTR)lbl, og->m_tempo_sl.GetPos() / 2);
		}
		m_tempoL.GetWindowText(s2); if (l != s2) m_tempoL.SetWindowText(l);
		{
			CString lbl = LL14(L"ピッチ", L"Pitch", L"Hauteur", L"Altezza", L"Tono", L"피치", L"音高", L"طبقة الصوت", L"Высота", L"Tonhohe", L"Tom", L"Toonhoogte", L"Wysokość", L"Perde");
			l.Format(_T("!@C606868%s!@C704878 %d%%"), (LPCTSTR)lbl, og->m_pitch_sl.GetPos() / 2);
		}
		m_pitchL.GetWindowText(s2); if (l != s2) m_pitchL.SetWindowText(l);

		// 乱数/順次・スペアナ/ステレオ/EQ/簡易ピアノロールの押下見た目
		int v1;
		v1 = og->m_random.GetCheck() ? 1 : 0; if (m_random.GetCheck() != v1) m_random.SetCheck(v1);
		SyncPushToggleButtons();

		// ジャケット(ボタン/ミニジャケクリック)はデータがあるときのみ有効
		BOOL hasJacket = (og->jx > 0 && !og->img.IsNull());
		if (m_jacket.IsWindowEnabled() != hasJacket)
			m_jacket.EnableWindow(hasJacket);

		// 一時停止/再開ボタン表記(短縮段階を維持)
		ApplyPauseButtonLabel();

		v1 = og->m_c2.GetCheck() ? 1 : 0;
		if (m_savewav.GetCheck() != v1) m_savewav.SetCheck(v1);

		v1 = savedata.saveSongParams ? 1 : 0;
		if (m_saveparam.GetSafeHwnd() && m_saveparam.GetCheck() != v1) m_saveparam.SetCheck(v1);

		if (GetFocus() != (CWnd*)&m_kaisuu) {
			og->m_kaisuu.GetWindowText(s);
			m_kaisuu.GetWindowText(s2);
			if (s != s2) m_kaisuu.SetWindowText(s);
		}
	}

	if (pl && ::IsWindow(pl->GetSafeHwnd())) {
		int v2;
		v2 = pl->m_renzoku.GetCheck() ? 1 : 0; if (m_renzoku.GetCheck() != v2) m_renzoku.SetCheck(v2);
		v2 = pl->m_loop.GetCheck() ? 1 : 0; if (m_loop.GetCheck() != v2) m_loop.SetCheck(v2);
		v2 = pl->m_tool.GetCheck() ? 1 : 0;
		if (m_tip.GetCheck() != v2) { m_tip.SetCheck(v2); ApplyListTooltipState(); }
		v2 = pl->m_saisyo.GetCheck() ? 1 : 0; if (m_mini.GetCheck() != v2) m_mini.SetCheck(v2);
		v2 = pl->m_save_mp3.GetCheck() ? 1 : 0; if (m_savemp3.GetCheck() != v2) m_savemp3.SetCheck(v2);
		v2 = pl->m_save_kpi.GetCheck() ? 1 : 0; if (m_saveds.GetCheck() != v2) m_saveds.SetCheck(v2);
		// プレイリスト一覧の増減や選択変更を反映。
		// ただしユーザーがコンボを操作中(ドロップダウン展開中/フォーカス中)は
		// SetCurSel で選択を奪わない。さもないと「2を選んでも1に戻る」不具合になる。
		if (::IsWindow(pl->m_listchange.GetSafeHwnd())) {
			BOOL busy = m_plsel.GetDroppedState() ||
				(GetFocus() == (CWnd*)&m_plsel);
			int n = pl->m_listchange.GetCount();
			if (n != m_lastComboCount) {
				if (!busy) ReloadPlaylistCombo();
			}
			else if (!busy && m_plsel.GetCurSel() != savedata.playlistnum)
				m_plsel.SetCurSel(savedata.playlistnum);
		}
	}

	// サイドパネル(左ジャケ/右曲情報)の表示内容が変わったら再描画。
	// 毎フレーム描画はせず、曲(タイトル/アーティスト/アルバム/ジャケ)変化時のみ更新。
	if (!m_jacketRect.IsRectEmpty() || !m_infoPanelRect.IsRectEmpty()) {
		CString fmt; if (::IsWindow(m_os.GetSafeHwnd())) m_os.GetWindowText(fmt);
		CString key;
		key.Format(_T("%s\x01%s\x01%s\x01%s\x01%s\x01%d\x01%d\x01%d\x01%d\x01%d\x01%d\x01%d"),
			(LPCTSTR)CurrentTrackTitle(), (LPCTSTR)tagname, (LPCTSTR)tagalbum,
			(LPCTSTR)tagtrack, (LPCTSTR)fmt, og ? og->jx : -1,
			g_pcm_upscale_active, wavbit_sample_Hz, wavchannel, wavsam_depth,
			g_ds_pcm_rate, g_ds_pcm_ch, g_ds_pcm_bits);
		if (key != m_lastBannerKey) {
			m_lastBannerKey = key;
			ResetInfoScroll();   // 曲変更時はスクロール位置をリセット
			InvalidateSidePanels();
		}
	}
}

void CMediaPlayerDlg::EnforceFalcomHidden()
{
	if (savedata.playerMode != 1) return;
	extern CImageBase* maini;
	extern CImageBase* playbase;
	if (og && ::IsWindow(og->GetSafeHwnd()) && ::IsWindowVisible(og->m_hWnd))
		::ShowWindow(og->m_hWnd, SW_HIDE);
	if (pl && ::IsWindow(pl->GetSafeHwnd()) && ::IsWindowVisible(pl->m_hWnd))
		::ShowWindow(pl->m_hWnd, SW_HIDE);
	if (maini && ::IsWindow(maini->GetSafeHwnd()) && ::IsWindowVisible(maini->m_hWnd))
		::ShowWindow(maini->m_hWnd, SW_HIDE);
	if (playbase && ::IsWindow(playbase->GetSafeHwnd()) && ::IsWindowVisible(playbase->m_hWnd))
		::ShowWindow(playbase->m_hWnd, SW_HIDE);
}

// 再生位置・時間表示・音量を og からミラーする高速ミラー関数。
// Timer3(100ms)と SyncFromMain から呼ばれる。og の timerp が playb(再生位置)を
// SetPos するため、それに合わせて m_seek と m_time を追従させる。
// タスクバー進捗(ITaskbarList3)も og ではなく mp のウィンドウに対して設定する。
void CMediaPlayerDlg::MirrorSeekVol()
{
	if (!og || !::IsWindow(og->GetSafeHwnd()) || !::IsWindow(GetSafeHwnd())) return;
	CString s2;
	// シーク(og->m_time=範囲スライダー。timerp が playb を SetPos するのでそれに追従)
	if (!m_seekDragging) {
		int mn = og->m_time.GetMinValue();
		int mx = og->m_time.GetMaxValue();
		if (mx <= mn) mx = mn + 1;
		int ps = og->m_time.GetPos();
		int selMn, selMx; og->m_time.GetSelection(selMn, selMx);
		// 一括更新+見た目変化時のみ1回 UPDATENOW(バナー合流の Invalidate だと経過で遅延)。
		m_seek.SetPlaybackMirror(ps, selMn, selMx, mn, mx);
		double pct = (double)(ps - mn) * 100.0 / (double)(mx - mn);
		if (pct < 0.0) pct = 0.0; if (pct > 100.0) pct = 100.0;
		CString t; t.Format(_T("!@C206830%.1f%%"), pct);
		m_time.GetWindowText(s2); if (t != s2) m_time.SetWindowText(t);

		// タスクバー進捗(緑追随)。og は非表示なので og->m_hWnd ではなく
		// このメディアプレイヤー画面のウィンドウに対して設定する必要がある。
		if (ptl) {
			if (plf && mx > mn) {
				ptl->SetProgressState(m_hWnd, TBPF_NORMAL);
				ptl->SetProgressValue(m_hWnd, (ULONGLONG)(ps - mn), (ULONGLONG)(mx - mn));
			}
			else {
				ptl->SetProgressState(m_hWnd, TBPF_NOPROGRESS);
			}
		}
	}
	// 音量(og->m_sl 0..100000 を 0..100 でミラー)
	int v = og->m_sl.GetPos() / 1000;
	if (v < 0) v = 0; if (v > 100) v = 100;
	CWnd* pf = GetFocus();
	if (!(pf && pf->GetSafeHwnd() == m_vol.GetSafeHwnd())) m_vol.SetPos(v);
	double vpct = (double)og->m_sl.GetPos() / 1000.0;
	if (!og->deve) vpct *= 100.0;
	CString vs; vs.Format(_T("!@C206830%.1f%%"), vpct); m_volval.GetWindowText(s2); if (vs != s2) m_volval.SetWindowText(vs);
}

void CMediaPlayerDlg::SavePos()
{
	if (!::IsWindow(GetSafeHwnd())) return;
	if (IsIconic()) return;
	RECT r; GetWindowRect(&r);
	savedata.mpx = r.left;
	savedata.mpy = r.top;
	savedata.mpw = r.right - r.left;
	savedata.mph = r.bottom - r.top;
	savedata.mpHasPos = 1;
	// リストの列幅も保存(最終列と★は起動時フィット/既定のため意味スロット 0..3 のみ)
	// mpcol: [0]=名前 [1]=ゲーム [2]=時間 [3]=アーティスト (列index 0,2,3,4)
	if (::IsWindow(m_list.GetSafeHwnd())) {
		int w = m_list.GetColumnWidth(0);
		if (w > 0) savedata.mpcol[0] = w;
		w = m_list.GetColumnWidth(2);
		if (w > 0) savedata.mpcol[1] = w;
		w = m_list.GetColumnWidth(3);
		if (w > 0) savedata.mpcol[2] = w;
		w = m_list.GetColumnWidth(4);
		if (w > 0) savedata.mpcol[3] = w;
	}
}

#if WIN64
void CMediaPlayerDlg::OnTimer(UINT_PTR nIDEvent)
#else
void CMediaPlayerDlg::OnTimer(UINT nIDEvent)
#endif
{
	if (nIDEvent == 1) {
		// 低速: テキスト/リスト/シーク/音量を同期(変化時のみ更新)
		SyncFromMain();
		RefreshList(FALSE);
		// 起動直後に裏画面が残る対策: メディアプレイヤーモード中は必ず隠す(監視)
		EnforceFalcomHidden();
		// 描画タイマー(2)は 60fps 固定。更新頻度の律速は og 側(ms2カウンタ)が行うため、
		// ここで張り直す必要はない。
	}
	else if (nIDEvent == 2) {
		// 安全網: 通常は og の timerp が新フレーム時(ms2レート)に mp バナーを無効化し、
		// mp の OnPaint が Blit + pending 解除を行う(=ファルコム特化型と同等の負荷)。
		// 万一 WM_PAINT が取りこぼされて pending が固着し合成が止まるのを防ぐため、
		// pending 中だけバナー再描画を促す。無条件 Invalidate だと曲番号 GDI が点滅する。
		extern LONG COgg_GetGdiPaintPending();
		if (::IsWindowVisible(GetSafeHwnd()) && !IsIconic() && COgg_GetGdiPaintPending())
			InvalidateRect(&m_bannerRect, FALSE);
	}
	else if (nIDEvent == 3) {
		// 高速: 再生位置(playb)に追従するシーク・時間・音量のミラー
		MirrorSeekVol();
		// EQ/ピアノ/スペアナ/ST を X ボタン等で閉じたときも押下見た目を追従
		SyncPushToggleButtons();
		// バナーのホバー状態を再計算(カーソルが帯の上にあれば前面化アニメ継続)。
		// 前面ウィンドウ条件は付けない(再生でフォーカスが移ってもアニメを止めない)。
		if (::IsWindowVisible(GetSafeHwnd()) && !IsIconic()) {
			CPoint pt; ::GetCursorPos(&pt); ScreenToClient(&pt);
			// ジャケットを左へ分離している間はバナー内蔵ジャケが無いので
			// ホバー演出(タイトル減光・ジャケ前面化)は無効化する。
			g_mpBannerHover = (!g_mpSideJacket && m_bannerRect.PtInRect(pt)) ? 1 : 0;

			// info パネルスクロールは TheadLoop から WM_MP_INFO_SCROLL で駆動（~30fps V-Sync同期）
			// Timer3 では行わない（精度不足のため TheadLoop ベースに移植済み）
		}
		else g_mpBannerHover = 0;
	}
	else if (nIDEvent == 4) {
		// 遅延アクリル再適用(合成確定後)。一度きり。
		KillTimer(4);
#if CCUSTOM_AERO_SUPPORT
		if (savedata.aero == 1) {
			RefreshAeroMode();
			if (CCC_IsWin11()) {
				BOOL comp = FALSE;
				if (SUCCEEDED(::DwmIsCompositionEnabled(&comp)) && comp) {
					int bt = 3;  // DWMSBT_TRANSIENTWINDOW(アクリル)
					::DwmSetWindowAttribute(m_hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &bt, sizeof(bt));
					MARGINS mg = { -1, -1, -1, -1 };
					::DwmExtendFrameIntoClientArea(m_hWnd, &mg);
				}
			}
			RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
		}
#endif
	}
	else if (nIDEvent == 6) {
		// 起動直後のスクロールバー未表示対策(両モード)。モード切替時と同じく
		// 子の非クライアント(枠/スクロールバー)を再描画させる。RDW_FRAME により
		// リストへ WM_NCPAINT が飛び、アクリル時は OpaqueFixer が不透明化する。
		KillTimer(6);
		if (::IsWindow(m_list.GetSafeHwnd())) {
			if (pl && pl->playcnt > 0) {
				int anchor = GetListScrollAnchor();
				m_list.SetItemCount(pl->playcnt);   // 可視状態で範囲を再確定
				RestoreListScrollAnchor(anchor);    // SetItemCount で先頭へ戻るのを防ぐ
			}
			m_list.RedrawWindow(NULL, NULL,
				RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
		}
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
	}
	else if (nIDEvent == kTimerListHdrDrag) {
		TickListHdrDragFit();
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CMediaPlayerDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	// Create/MoveWindow 中の WM_SIZE は列作成前などに来る。
	// 未準備の GetCheck/DoLayout は ENSURE→「引数が正しくありません」。
	if (!m_uiReady)
		return;
	try {
	if (nType == SIZE_MINIMIZED) {
		if (m_mini.GetSafeHwnd() && m_mini.GetCheck() && og && ::IsWindow(og->GetSafeHwnd())) {
			if (::IsWindow(og->m_EqualizerDlg->GetSafeHwnd())) {
				m_savedEqVisible = ::IsWindowVisible(og->m_EqualizerDlg->m_hWnd) ? 1 : 0;
				if (m_savedEqVisible) ::ShowWindow(og->m_EqualizerDlg->m_hWnd, SW_HIDE);
			}
			if (::IsWindow(og->m_PianoRollDlg->GetSafeHwnd())) {
				m_savedPianoVisible = ::IsWindowVisible(og->m_PianoRollDlg->m_hWnd) ? 1 : 0;
				if (m_savedPianoVisible) ::ShowWindow(og->m_PianoRollDlg->m_hWnd, SW_HIDE);
			}
			if (::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd())) {
				m_savedAnalyzerVisible = ::IsWindowVisible(og->m_AnalyzerDlg->m_hWnd) ? 1 : 0;
				if (m_savedAnalyzerVisible) ::ShowWindow(og->m_AnalyzerDlg->m_hWnd, SW_HIDE);
			}
		}
		return;
	}
	if (::IsWindow(m_hWnd)) {
		if (nType == SIZE_RESTORED && m_mini.GetSafeHwnd() && m_mini.GetCheck() && og && ::IsWindow(og->GetSafeHwnd())) {
			if (m_savedEqVisible && ::IsWindow(og->m_EqualizerDlg->GetSafeHwnd()))
				::ShowWindow(og->m_EqualizerDlg->m_hWnd, SW_SHOW);
			if (m_savedPianoVisible && ::IsWindow(og->m_PianoRollDlg->GetSafeHwnd()))
				::ShowWindow(og->m_PianoRollDlg->m_hWnd, SW_SHOW);
			if (m_savedAnalyzerVisible && ::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd()))
				::ShowWindow(og->m_AnalyzerDlg->m_hWnd, SW_SHOW);
			m_savedEqVisible = 0;
			m_savedPianoVisible = 0;
			m_savedAnalyzerVisible = 0;
		}
		DoLayout();
		if (m_inSizeMove) {
			RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
		}
		else {
			RedrawWindow(NULL, NULL,
				RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
			if (::IsWindow(m_list.GetSafeHwnd()))
				m_list.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
		}
	}
	}
	catch (CException* e)
	{
		OutputDebugString(_T("[CMediaPlayerDlg::OnSize] CException swallowed\n"));
		e->Delete();
	}
}

void CMediaPlayerDlg::OnEnterSizeMove()
{
	m_inSizeMove = true;
	Default();
}

void CMediaPlayerDlg::OnExitSizeMove()
{
	m_inSizeMove = false;
	if (::IsWindow(m_hWnd) && !IsIconic()) {
		DoLayout();
		// 確定時に一度だけ同期再描画して、ドラッグ中の簡易描画の崩れを整える。
		RedrawWindow(NULL, NULL,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		if (::IsWindow(m_list.GetSafeHwnd()))
			m_list.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
	}
	Default();
}

void CMediaPlayerDlg::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogExBase::OnMoving(fwSide, pRect);
	CCC_MainLockOnMainMoving(pRect);
	SavePos();
}

void CMediaPlayerDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	lpMMI->ptMinTrackSize.x = (int)(620 * hD2);
	lpMMI->ptMinTrackSize.y = (int)(560 * hD2);
	CCustomBlurDialogExBase::OnGetMinMaxInfo(lpMMI);
}

// og のオフスクリーン合成面(スペアナ+ジャケ+時間)を帯に描画(同一UIスレッドなので安全)
void CMediaPlayerDlg::BlitVisualizer(CDC* pDC)
{
	if (!pDC || m_bannerRect.IsRectEmpty()) return;
	if (dc.GetSafeHdc() == NULL) return;
	int dw = m_bannerRect.Width(), dh = m_bannerRect.Height();
	if (dw <= 0 || dh <= 0) return;

#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1 && CCC_IsWin11()) {
		// アクリル(Win11ガラス)時: 通常Blitはアルファ0で透明になってしまうため、
		// og と同じ専用ヘルパで黒透過＋実体ピクセルを不透明合成する。
		CCC_BlitStretchNF(pDC->m_hDC, m_bannerRect.left, m_bannerRect.top, dw, dh,
			dc.GetSafeHdc(), 0, 0, MP_SRCW, MP_SRCH, RGB(0, 0, 0));
		return;
	}
#endif
	// 非アクリル: 永続メモリDCにキャッシュして軽量Blit(毎フレームのビットマップ生成を回避)
	if (m_memBanner.GetSafeHdc() == NULL)
		m_memBanner.CreateCompatibleDC(pDC);
	if (m_bannerCacheW != dw || m_bannerCacheH != dh || m_bmpBanner.GetSafeHandle() == NULL) {
		if (m_bmpBanner.GetSafeHandle()) m_bmpBanner.DeleteObject();
		m_bmpBanner.CreateCompatibleBitmap(pDC, dw, dh);
		m_bannerCacheW = dw; m_bannerCacheH = dh;
	}
	HGDIOBJ oldBmp = ::SelectObject(m_memBanner.GetSafeHdc(), m_bmpBanner.GetSafeHandle());
	int oldMode = ::SetStretchBltMode(m_memBanner.GetSafeHdc(), COLORONCOLOR);
	::SetBrushOrgEx(m_memBanner.GetSafeHdc(), 0, 0, NULL);
	::StretchBlt(m_memBanner.GetSafeHdc(), 0, 0, dw, dh, dc.GetSafeHdc(), 0, 0, MP_SRCW, MP_SRCH, SRCCOPY);
	::SetStretchBltMode(m_memBanner.GetSafeHdc(), oldMode);
	::BitBlt(pDC->m_hDC, m_bannerRect.left, m_bannerRect.top, dw, dh, m_memBanner.GetSafeHdc(), 0, 0, SRCCOPY);
	::SelectObject(m_memBanner.GetSafeHdc(), oldBmp);
}

// og と同じ規則で表示用タイトルを解決(timerp の sss 決定ロジックと一致)
CString CMediaPlayerDlg::CurrentTrackTitle() const
{
	CString t = fnn;
	if (mode == -10 || mode == -9 || mode == -8 || mode == -7) {
		if (!tagfile.IsEmpty()) t = tagfile;
	}
	if ((stitle != _T("") && mode == -1) || mode == 21 || mode == -6) t = stitle;
	// wav 等もタグのタイトルがあれば優先(無ければファイル名のまま)
	if (mode == 999 && !stitle.IsEmpty()) t = stitle;
	return t;
}

void CMediaPlayerDlg::InvalidateSidePanels()
{
	if (!::IsWindow(GetSafeHwnd())) return;
	if (!m_jacketRect.IsRectEmpty())    InvalidateRect(&m_jacketRect, FALSE);
	if (!m_infoPanelRect.IsRectEmpty()) InvalidateRect(&m_infoPanelRect, FALSE);
}

// m_tip チェックボックスの状態を m_list のツールチップ設定に反映。
// ON : 行詳細ツールチップ(CListCtrlA)のみ。ダイアログ側バルーンは外す。
// OFF: 行詳細を止め、「ダブルクリックで再生…」バルーンを出す。
void CMediaPlayerDlg::ApplyListTooltipState()
{
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	const bool on = (m_tip.GetCheck() != 0);
	m_list.EnableToolTips(on ? TRUE : FALSE);
	DWORD exStyle = m_list.GetExtendedStyle();
	if (on)
		exStyle &= ~LVS_EX_INFOTIP;   // カスタムツールチップ使用中はシステム infotip を無効
	else
		exStyle |= LVS_EX_INFOTIP;
	m_list.SetExtendedStyle(exStyle);

	if (m_tooltip.GetSafeHwnd()) {
		const CString balloon = LL14(
			L"ダブルクリックで再生。ファイルをドロップして追加できます。",
			L"Double-click to play. Drop files to add.",
			L"Double-clic pour lire. Glissez des fichiers.",
			L"Doppio clic per riprodurre. Trascina file.",
			L"Doble clic para reproducir. Suelta archivos.",
			L"더블 클릭으로 재생. 파일을 드롭해 추가.",
			L"双击播放。拖入文件添加。",
			L"انقر مزدوجاً للتشغيل. أفلت الملفات.",
			L"Двойной клик — воспроизведение. Перетащите файлы.",
			L"Doppelklick zum Abspielen. Dateien ablegen.",
			L"Clique duplo para tocar. Solte arquivos.",
			L"Dubbelklik om af te spelen. Sleep bestanden.",
			L"Kliknij dwukrotnie. Upuść pliki.",
			L"Çift tıkla çal. Dosya bırak.");
		// いったん外してから、OFF のときだけ付け直す(ON 時の二重表示防止)
		m_tooltip.DelTool(&m_list);
		if (!on)
			m_tooltip.AddTool(&m_list, balloon);
	}
}

// WM_MP_INFO_SCROLL ハンドラ。TheadLoop から ~30fps で PostMessage される。
// タイマーよりも V-Sync に近いタイミングで呼ばれるため marquee が滑らかになる。
// m_iscActive が true なら右曲情報パネルを無効化 → DrawSidePanels がスクロールを1段進めて
// 再び true にセットする(→次 tick でまた無効化)。スクロール不要なら m_iscActive は
// false のままで再描画は発生しない。
LRESULT CMediaPlayerDlg::OnInfoScrollTick(WPARAM, LPARAM)
{
	// posted は描画完了まで保持。先に降ろすと TheadLoop が連投する。
	// ピアノ/アナライザが開いていると Invalidate だけの WM_PAINT は後回しになるため、
	// 情報パネルだけ UPDATENOW でこのターンに描画してスクロールを守る。
	if (m_iscActive && !m_infoPanelRect.IsRectEmpty()) {
		m_iscActive = false;
		RedrawWindow(&m_infoPanelRect, NULL,
			RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
	}
	else {
		InterlockedExchange(&m_iscScrollPosted, 0);
	}
	return 0;
}

// 非アクティブ化でアクリル背景が落ちる対策。
// mp はタスクバー表示のためトップレベル化(オーナー解除)されており、
// EQ/簡易ピアノロール等の og 所有ウィンドウと違い、非アクティブ時に DWM の
// アクリルバックドロップが維持されない。活性が変わるたびに backdrop 属性と
// フレーム拡張を再適用して、非アクティブでもアクリルを保つ。
BOOL CMediaPlayerDlg::OnNcActivate(BOOL bActive)
{
	BOOL r = CCustomBlurDialogExBase::OnNcActivate(bActive);
#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1 && CCC_IsWin11())
		CCC_RefreshDwmBlur(m_hWnd);   // backdrop=acrylic + フレーム拡張を再適用
#endif
	return r;
}

void CMediaPlayerDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		ShowOggAboutDialog(this);
		return;
	}
	if ((nID & 0xFFF0) == SC_CLOSE)
	{
		OnClose();
		return;
	}
	CCustomBlurDialogExBase::OnSysCommand(nID, lParam);
}

void CMediaPlayerDlg::ResetInfoScroll()
{
	for (int i = 0; i < kInfoRows; i++) {
		m_isc[i] = 0; m_iscW[i] = 0;
		if (m_iscRowDC[i].GetSafeHdc()) {
			if (m_iscRowOldBmp[i]) m_iscRowDC[i].SelectObject(m_iscRowOldBmp[i]);
			m_iscRowDC[i].DeleteDC();
		}
		m_iscRowBmp[i].DeleteObject();
		m_iscRowOldBmp[i] = nullptr;
		m_iscRowCacheW[i] = m_iscRowCacheH[i] = 0;
		m_iscRowCacheText[i].Empty();
	}
	m_iscActive = false;
	InterlockedExchange(&m_iscScrollPosted, 0);
}

// 1行のテキストをスクロール対応で mem DC へ描画する。
//
// 収まる場合: DrawText で静止描画して false を返す(スクロール不要)。
//
// はみ出す場合: 「テキスト + セパレータ」2連続のワイド DC を行キャッシュし、
// m_isc[rowIdx] オフセットで可視幅(tw)分だけ BitBlt する。
// （旧実装は毎フレーム CreateCompatibleBitmap/CreatePen → 長時間で GDI が死ぬ）
//
// rowIdx: m_isc/m_iscW のインデックス(0=タイトル行, 1〜5=サブ行)
bool CMediaPlayerDlg::DrawInfoScrollRow(CDC& mem, int tx, int y, int tw, int lineH,
	const CString& text, COLORREF clr, int rowIdx, COLORREF kBg, CFont* font)
{
	if (text.IsEmpty() || tw <= 0 || lineH <= 0) return false;
	if (rowIdx < 0 || rowIdx >= kInfoRows) return false;

	CFont* oldFont = mem.SelectObject(font);
	CSize szText = mem.GetTextExtent(text);
	mem.SelectObject(oldFont);

	if (szText.cx <= tw) {
		m_isc[rowIdx]  = 0;
		m_iscW[rowIdx] = 0;
		mem.SelectObject(font);
		mem.SetTextColor(clr);
		CRect rr(tx, y, tx + tw, y + lineH);
		mem.DrawText(text, &rr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
		mem.SelectObject(oldFont);
		return false;
	}

	const CString kSep = _T("　　 ");
	CString scrollText = text + kSep;
	mem.SelectObject(font);
	CSize szFull = mem.GetTextExtent(scrollText);
	mem.SelectObject(oldFont);

	if (szFull.cx <= 0) return false;
	m_iscW[rowIdx] = szFull.cx;

	const int wideW = szFull.cx * 2 + 4;
	const bool needRebuild =
		!m_iscRowDC[rowIdx].GetSafeHdc()
		|| m_iscRowCacheW[rowIdx] != wideW
		|| m_iscRowCacheH[rowIdx] != lineH
		|| m_iscRowCacheClr[rowIdx] != clr
		|| m_iscRowCacheBg[rowIdx] != kBg
		|| m_iscRowCacheText[rowIdx] != scrollText;

	if (needRebuild) {
		if (m_iscRowDC[rowIdx].GetSafeHdc()) {
			if (m_iscRowOldBmp[rowIdx]) m_iscRowDC[rowIdx].SelectObject(m_iscRowOldBmp[rowIdx]);
			m_iscRowDC[rowIdx].DeleteDC();
		}
		m_iscRowBmp[rowIdx].DeleteObject();
		m_iscRowOldBmp[rowIdx] = nullptr;
		m_iscRowCacheW[rowIdx] = m_iscRowCacheH[rowIdx] = 0;

		if (!m_iscRowDC[rowIdx].CreateCompatibleDC(&mem))
			return false;
		if (!m_iscRowBmp[rowIdx].CreateCompatibleBitmap(&mem, wideW, lineH)) {
			m_iscRowDC[rowIdx].DeleteDC();
			return false;
		}
		m_iscRowOldBmp[rowIdx] = m_iscRowDC[rowIdx].SelectObject(&m_iscRowBmp[rowIdx]);
		CDC& wdc = m_iscRowDC[rowIdx];
		wdc.FillSolidRect(0, 0, wideW, lineH, kBg);
		wdc.SetBkMode(TRANSPARENT);
		wdc.SetTextColor(clr);

		CFont* wf = wdc.SelectObject(font);
		CRect wr1(0, 0, szFull.cx + 4, lineH);
		wdc.DrawText(scrollText, &wr1, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
		CRect wr2(szFull.cx, 0, szFull.cx * 2 + 4, lineH);
		wdc.DrawText(scrollText, &wr2, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

		CSize szTextOnly = wdc.GetTextExtent(text);
		int sx = szTextOnly.cx;
		int sw = szFull.cx - szTextOnly.cx;
		if (sw > 8) {
			int cy = lineH / 2;
			int dr = max(2, lineH / 10);
			HDC hdc = wdc.GetSafeHdc();
			HGDIOBJ oldPen = ::SelectObject(hdc, ::GetStockObject(NULL_PEN));
			HGDIOBJ oldBrush = ::SelectObject(hdc, ::GetStockObject(DC_BRUSH));
			::SetDCBrushColor(hdc, clr);

			int lx = sx + sw / 3;
			wdc.Ellipse(lx - dr, cy - dr, lx + dr, cy + dr);
			int rx = sx + sw * 2 / 3;
			wdc.Ellipse(rx - dr, cy - dr, rx + dr, cy + dr);
			int mx = sx + sw / 2;
			int mr = max(2, lineH / 8);
			POINT diaPts[4] = { {mx, cy - mr}, {mx + mr, cy}, {mx, cy + mr}, {mx - mr, cy} };
			wdc.Polygon(diaPts, 4);

			::SelectObject(hdc, ::GetStockObject(DC_PEN));
			::SetDCPenColor(hdc, clr);
			::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
			wdc.MoveTo(sx + 2, cy); wdc.LineTo(lx - dr - 1, cy);
			wdc.MoveTo(rx + dr + 1, cy); wdc.LineTo(sx + sw - 2, cy);

			int sx2 = sx + szFull.cx;
			int lx2 = sx2 + sw / 3, rx2 = sx2 + sw * 2 / 3, mx2 = sx2 + sw / 2;
			::SelectObject(hdc, ::GetStockObject(NULL_PEN));
			::SelectObject(hdc, ::GetStockObject(DC_BRUSH));
			::SetDCBrushColor(hdc, clr);
			wdc.Ellipse(lx2 - dr, cy - dr, lx2 + dr, cy + dr);
			wdc.Ellipse(rx2 - dr, cy - dr, rx2 + dr, cy + dr);
			POINT diaPts2[4] = { {mx2, cy - mr}, {mx2 + mr, cy}, {mx2, cy + mr}, {mx2 - mr, cy} };
			wdc.Polygon(diaPts2, 4);
			::SelectObject(hdc, ::GetStockObject(DC_PEN));
			::SetDCPenColor(hdc, clr);
			::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
			wdc.MoveTo(sx2 + 2, cy); wdc.LineTo(lx2 - dr - 1, cy);
			wdc.MoveTo(rx2 + dr + 1, cy); wdc.LineTo(sx2 + sw - 2, cy);

			::SelectObject(hdc, oldBrush);
			::SelectObject(hdc, oldPen);
		}
		wdc.SelectObject(wf);

		m_iscRowCacheW[rowIdx] = wideW;
		m_iscRowCacheH[rowIdx] = lineH;
		m_iscRowCacheClr[rowIdx] = clr;
		m_iscRowCacheBg[rowIdx] = kBg;
		m_iscRowCacheText[rowIdx] = scrollText;
	}

	CDC& wdc = m_iscRowDC[rowIdx];
	if (!wdc.GetSafeHdc()) return false;

	int off = m_isc[rowIdx] % szFull.cx;
	if (off < 0) off = 0;

	int saved = mem.SaveDC();
	mem.IntersectClipRect(tx, y, tx + tw, y + lineH);
	mem.BitBlt(tx - off, y, szFull.cx, lineH, &wdc, 0, 0, SRCCOPY);
	mem.BitBlt(tx + szFull.cx - off, y, tw, lineH, &wdc, szFull.cx, 0, SRCCOPY);
	mem.RestoreDC(saved);

	m_isc[rowIdx] += 2;
	if (m_isc[rowIdx] >= szFull.cx) m_isc[rowIdx] -= szFull.cx;

	return true;
}

// ジャケット無しのとき、素っ気ないアイコンの代わりに「Media Player らいら」の
// タイトルと、ほんのり可愛いパステルの模様(縦グラデ + 水玉 + キラキラ/お花)を描く。
// dc は w×h のオフスクリーン。純黒(=アクリルのクロマキー)は使わない。
static void Mp_DrawNoJacketPlaceholder(CDC& dc, int w, int h)
{
	if (w <= 0 || h <= 0) return;

	// --- 背景: やわらかいピンク → ラベンダーの縦グラデ ---
	for (int y = 0; y < h; y++) {
		int t = (h > 1) ? (y * 100 / (h - 1)) : 0;
		int r = 255 + (234 - 255) * t / 100;
		int g = 226 + (223 - 226) * t / 100;
		int b = 240 + (250 - 240) * t / 100;
		dc.FillSolidRect(0, y, w, 1, RGB(r, g, b));
	}

	dc.SetBkMode(TRANSPARENT);
	CGdiObject* opnNull = dc.SelectStockObject(NULL_PEN);

	// --- 水玉模様(市松状にオフセット、ほんのり白でやさしく) ---
	int step = max(12, h / 5);
	int dot = max(2, step / 6);
	{
		CBrush brDot(RGB(255, 245, 250));
		CBrush* ob = dc.SelectObject(&brDot);
		for (int gy = 0, row = 0; gy <= h + step; gy += step, row++) {
			int offx = (row & 1) ? step / 2 : 0;
			for (int gx = -step; gx <= w + step; gx += step) {
				int cx = gx + offx, cy = gy;
				dc.Ellipse(cx - dot, cy - dot, cx + dot, cy + dot);
			}
		}
		dc.SelectObject(ob);
	}

	// --- ちいさなキラキラ(4尖)とお花(アクセント・ハートは使わない) ---
	auto sparkle = [&](int cx, int cy, int s, COLORREF c) {
		if (s < 2) return;
		CBrush br(c);
		CBrush* ob = dc.SelectObject(&br);
		// 縦横のひし形クロス
		POINT v[4] = { { cx, cy - s }, { cx + max(1, s / 4), cy }, { cx, cy + s }, { cx - max(1, s / 4), cy } };
		POINT hz[4] = { { cx - s, cy }, { cx, cy - max(1, s / 4) }, { cx + s, cy }, { cx, cy + max(1, s / 4) } };
		dc.Polygon(v, 4);
		dc.Polygon(hz, 4);
		dc.SelectObject(ob);
	};
	auto flower = [&](int cx, int cy, int s, COLORREF petal, COLORREF core) {
		if (s < 2) return;
		CBrush brP(petal);
		CBrush* ob = dc.SelectObject(&brP);
		int pr = max(2, s * 2 / 3);
		dc.Ellipse(cx - pr, cy - s - pr / 3, cx + pr, cy - s / 4);           // 上
		dc.Ellipse(cx - pr, cy + s / 4, cx + pr, cy + s + pr / 3);           // 下
		dc.Ellipse(cx - s - pr / 3, cy - pr, cx - s / 4, cy + pr);           // 左
		dc.Ellipse(cx + s / 4, cy - pr, cx + s + pr / 3, cy + pr);           // 右
		CBrush brC(core);
		dc.SelectObject(&brC);
		int cr = max(1, s / 3);
		dc.Ellipse(cx - cr, cy - cr, cx + cr, cy + cr);
		dc.SelectObject(ob);
	};
	int hs = max(3, h / 12);
	int fs = max(3, h / 14);
	sparkle(w * 18 / 100, h * 22 / 100, hs, RGB(255, 198, 220));
	flower(w * 80 / 100, h * 28 / 100, fs, RGB(255, 210, 228), RGB(255, 236, 180));
	flower(w * 22 / 100, h * 78 / 100, fs, RGB(255, 204, 222), RGB(255, 240, 190));
	sparkle(w * 78 / 100, h * 82 / 100, hs, RGB(255, 205, 224));
	// 中央寄りに小さなキラを1つ(タイトル周りをふんわり)
	sparkle(w * 88 / 100, h * 58 / 100, max(2, hs * 2 / 3), RGB(255, 220, 232));

	dc.SelectObject(opnNull);

	// --- タイトル: "Media Player" / "らいら" を中央に(下地にやわらかい白影) ---
	int hbig = max(11, h / 4);
	int hsml = max(9, h / 9);
	LOGFONT lf; ZeroMemory(&lf, sizeof(lf));
	lstrcpyn(lf.lfFaceName, _T("Yu Gothic UI"), LF_FACESIZE);
	lf.lfQuality = CLEARTYPE_QUALITY;
	lf.lfWeight = FW_SEMIBOLD;

	CFont fSml; lf.lfHeight = -hsml; fSml.CreateFontIndirect(&lf);
	CFont fBig; lf.lfHeight = -hbig; lf.lfWeight = FW_BOLD; fBig.CreateFontIndirect(&lf);

	int totalH = hsml + hbig + max(1, h / 40);
	int y0 = (h - totalH) / 2; if (y0 < 0) y0 = 0;

	auto shadowText = [&](CFont& f, int yy, int hh, LPCTSTR s, COLORREF fg) {
		CFont* of = dc.SelectObject(&f);
		CRect rt(0, yy, w, yy + hh);
		CRect rs = rt; rs.OffsetRect(1, 1);
		dc.SetTextColor(RGB(255, 255, 255));
		dc.DrawText(s, -1, &rs, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc.SetTextColor(fg);
		dc.DrawText(s, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectObject(of);
	};
	shadowText(fSml, y0, hsml, _T("Media Player"), RGB(214, 108, 150));
	// ブランド名はウィンドウタイトルと同じ LL14 表記(英語は Raira。らいら固定は翻訳漏れ)
	shadowText(fBig, y0 + hsml + max(1, h / 40), hbig,
		LL14(L"らいら", L"Raira", L"Raira", L"Raira", L"Raira", L"라이라", L"莱拉", L"رايرا", L"Райра", L"Raira", L"Raira", L"Raira", L"Raira", L"Raira"),
		RGB(200, 72, 128));
}

// 左ジャケット / 右曲情報 パネルを描画。バナーと同じ黒地に統一し、上部の帯全体が
// ひとつのメディアバー(左:ジャケ / 中央:スペアナ / 右:曲情報)に見えるようにする。
// 内容は曲変更/リサイズ時のみ再描画されるため(毎フレームではない)ちらつかない。
void CMediaPlayerDlg::DrawSidePanels(CDC* pDC)
{
	if (!pDC) return;
	if (m_jacketRect.IsRectEmpty() && m_infoPanelRect.IsRectEmpty()) return;

	bool aero = false;
#if CCUSTOM_AERO_SUPPORT
	aero = (savedata.aero == 1 && CCC_IsWin11());
#endif
	// アクリル時は黒 = クロマキー(ガラス透過)。非アクリル時も黒背景でバナーと統一。
	const COLORREF kBg = RGB(0, 0, 0);

	// 更新領域(クリップ)に重なるパネルだけ再構築する。バナーは毎フレーム無効化
	// されるが、その際クリップはバナー矩形のみなので、ここでの重い描画(画像縮小/
	// 文字描画)は走らない(=サイドパネルは曲変更/リサイズ時のみ再描画)。
	CRect clip; pDC->GetClipBox(&clip);

	// ---- 左: ジャケット(ミニ・余白へ分離) ----
	if (!m_jacketRect.IsRectEmpty() && CRect().IntersectRect(&clip, &m_jacketRect)) {
		int w = m_jacketRect.Width(), h = m_jacketRect.Height();
		if (w > 0 && h > 0) {
			CDC mem; mem.CreateCompatibleDC(pDC);
			CBitmap bm; bm.CreateCompatibleBitmap(pDC, w, h);
			CBitmap* ob = mem.SelectObject(&bm);
			mem.FillSolidRect(0, 0, w, h, kBg);
			if (og && og->jx > 0 && !og->img.IsNull()) {
				double jr = og->jxy; if (jr <= 0.0) jr = 1.0;
				int dw = w, dh = h;                 // アスペクト維持で正方形内にフィット
				if (jr >= 1.0) { dw = w; dh = (int)((double)w / jr); }
				else { dh = h; dw = (int)((double)h * jr); }
				if (dw > w) { dw = w; dh = (int)((double)w / jr); }
				if (dh > h) { dh = h; dw = (int)((double)h * jr); }
				if (dw < 1) dw = 1; if (dh < 1) dh = 1;
				int dx = (w - dw) / 2, dy = (h - dh) / 2;
				int om = ::SetStretchBltMode(mem.GetSafeHdc(), HALFTONE);
				::SetBrushOrgEx(mem.GetSafeHdc(), 0, 0, NULL);
				og->img.Draw(mem.GetSafeHdc(), dx, dy, dw, dh, 0, 0, og->jx, og->jy);
				::SetStretchBltMode(mem.GetSafeHdc(), om);
			}
			else {                                  // ジャケ無し: 「Media Player らいら」+ 可愛い模様
				Mp_DrawNoJacketPlaceholder(mem, w, h);
			}
			if (aero)
				CCC_BlitStretchNF(pDC->m_hDC, m_jacketRect.left, m_jacketRect.top, w, h, mem.GetSafeHdc(), 0, 0, w, h, kBg);
			else
				pDC->BitBlt(m_jacketRect.left, m_jacketRect.top, w, h, &mem, 0, 0, SRCCOPY);
			mem.SelectObject(ob);
		}
	}

	// ---- 右: 曲情報パネル(タイトル/アーティスト/アルバム/形式, スクロール対応) ----
	if (!m_infoPanelRect.IsRectEmpty() && CRect().IntersectRect(&clip, &m_infoPanelRect)) {
		int w = m_infoPanelRect.Width(), h = m_infoPanelRect.Height();

		// リサイズ検出: パネル幅変化時はスクロールをリセット
		if (w != m_lastInfoPanelW) { ResetInfoScroll(); m_lastInfoPanelW = w; }

		if (w > 0 && h > 0) {
			bool memOk = (m_infoMemDC.GetSafeHdc() && m_infoMemW == w && m_infoMemH == h);
			if (!memOk) {
				if (m_infoMemDC.GetSafeHdc()) {
					if (m_infoMemOldBmp) m_infoMemDC.SelectObject(m_infoMemOldBmp);
					m_infoMemDC.DeleteDC();
				}
				m_infoMemBmp.DeleteObject();
				m_infoMemOldBmp = nullptr;
				m_infoMemW = m_infoMemH = 0;
				if (m_infoMemDC.CreateCompatibleDC(pDC)
					&& m_infoMemBmp.CreateCompatibleBitmap(pDC, w, h)) {
					m_infoMemOldBmp = m_infoMemDC.SelectObject(&m_infoMemBmp);
					m_infoMemW = w;
					m_infoMemH = h;
					memOk = true;
				}
				else {
					if (m_infoMemDC.GetSafeHdc()) m_infoMemDC.DeleteDC();
					m_infoMemBmp.DeleteObject();
				}
			}
			if (memOk) {
			CDC& mem = m_infoMemDC;
			mem.FillSolidRect(0, 0, w, h, kBg);
			mem.SetBkMode(TRANSPARENT);

			const int pad = (int)(8 * hD2);
			int tx = pad, tw = w - pad * 2;
			if (tw < 1) tw = 1;

			// ---- 情報収集 ----
			CString title = CurrentTrackTitle();
			CString artist = tagname, album = tagalbum;
			CString track = tagtrack;
			if (mode == -3)
				track.Empty();
			CString fmt; if (::IsWindow(m_os.GetSafeHwnd())) m_os.GetWindowText(fmt);

			// 曲番号行
			CString trackLine;
			if (!track.IsEmpty())
				trackLine.Format(LL14(L"曲番号 %s", L"Track %s", L"Piste %s", L"Traccia %s", L"Pista %s", L"곡번호 %s", L"曲号 %s", L"المقطع %s", L"Трек %s", L"Titel %s", L"Faixa %s", L"Track %s", L"Utwór %s", L"Parça %s"), (LPCTSTR)track);

			// Hz / チャンネル / ビット深度行（アップスケール時は src ✦ dst）
			CString audioLine = FormatAudioPlaybackDisplay(wavbit_sample_Hz, wavchannel, wavsam_depth);

			// ---- 行高・縦中央寄せ ----
			int titleH  = (int)(24 * hD2);
			int lineH   = (int)(17 * hD2);
			if (lineH < 12) lineH = 12;
			int ruleGap = (int)(6 * hD2);
			int rows = 1; // タイトル
			if (!artist.IsEmpty())    rows++;
			if (!album.IsEmpty())     rows++;
			if (!trackLine.IsEmpty()) rows++;
			if (!audioLine.IsEmpty()) rows++;
			if (!fmt.IsEmpty())       rows++;
			int totalH = titleH + ruleGap + (rows - 1) * lineH;
			int y = (h - totalH) / 2; if (y < 0) y = 0;

			// ---- タイトル行(スクロール対応) ----
			CFont* of = mem.SelectObject(&m_fontTitle);
			mem.SetBkMode(TRANSPARENT);
			bool titleScrolled = DrawInfoScrollRow(mem, tx, y, tw, titleH,
				title.IsEmpty() ? CString(_T("‐")) : title,
				RGB(255, 255, 255), 0, kBg, &m_fontTitle);
			if (titleScrolled) m_iscActive = true;
			y += titleH;
			mem.SelectObject(of);

			// アクセント罫線（グリーン系でスペアナと調和）
			mem.FillSolidRect(tx, y + ruleGap / 2, tw, max(1, (int)(1 * hD2 + 0.5)), RGB(0, 160, 0));
			y += ruleGap;

			// ---- サブ行（アーティスト/アルバム/曲番号/オーディオ/フォーマット、各スクロール対応） ----
			struct SubRow { CString s; COLORREF c; int idx; };
			SubRow items[5]; int n = 0;
			if (!artist.IsEmpty())    { items[n] = { artist,    RGB(225, 225, 225), 1 }; n++; }
			if (!album.IsEmpty())     { items[n] = { album,     RGB(190, 190, 190), 2 }; n++; }
			if (!trackLine.IsEmpty()) { items[n] = { trackLine, RGB(180, 180, 210), 3 }; n++; }
			if (!audioLine.IsEmpty()) { items[n] = { audioLine, RGB(130, 210, 230), 4 }; n++; }
			if (!fmt.IsEmpty())       { items[n] = { fmt,       RGB(150, 200, 150), 5 }; n++; }

			mem.SetBkMode(TRANSPARENT);
			for (int i = 0; i < n; i++) {
				bool scrolled = DrawInfoScrollRow(mem, tx, y, tw, lineH,
					items[i].s, items[i].c, items[i].idx, kBg, &m_fontInfo);
				if (scrolled) m_iscActive = true;
				y += lineH;
			}

			if (aero)
				CCC_BlitStretchNF(pDC->m_hDC, m_infoPanelRect.left, m_infoPanelRect.top, w, h, mem.GetSafeHdc(), 0, 0, w, h, kBg);
			else
				pDC->BitBlt(m_infoPanelRect.left, m_infoPanelRect.top, w, h, &mem, 0, 0, SRCCOPY);
			} // memOk
		}
		// スクロール tick の背圧を解放（描画完了後に次の Post を許可）
		InterlockedExchange(&m_iscScrollPosted, 0);
	}
}

BOOL CMediaPlayerDlg::OnEraseBkgnd(CDC* pDC)
{
	// 描画は OnPaint / Blit で完結させ、ここでは消去しない(チラつき防止)
	return TRUE;
}

void CMediaPlayerDlg::OnPaint()
{
	extern void COgg_ClearGdiPaintPending();
#if CCUSTOM_AERO_SUPPORT
	// アクリル(Win11) パス
	// CCC_PaintAeroGaps は SelectClipRgn を書き換えるため SaveDC/RestoreDC で挟む。
	// preserve=&m_bannerRect でバナーを保護(毎フレームのクリアを防いで点滅なし)。
	// グループボックスは CCC_ClipNoChildren で除外されて白くなるため、
	// RestoreDC 後に個別で CCC_ClearRectChroma を呼んで明示的にクロマクリアする。
	if (savedata.aero == 1 && CCC_IsWin11()) {
		CPaintDC dc(this);
		CRect clipBox; dc.GetClipBox(&clipBox);
		auto clipInside = [](const CRect& clip, const CRect& rect) -> bool {
			return !rect.IsRectEmpty()
				&& rect.PtInRect(clip.TopLeft()) && rect.PtInRect(clip.BottomRight());
		};
		// サイドパネルだけの再描画(曲情報スクロール等)では gap クリアをしない。
		// アクリル gap クリア→再描画の1フレーム空白が曲番号 GDI のちらつきになる。
		const bool sidePanelOnly =
			clipInside(clipBox, m_infoPanelRect) || clipInside(clipBox, m_jacketRect);
		bool hitBanner = !m_bannerRect.IsRectEmpty() && CRect().IntersectRect(&clipBox, &m_bannerRect);
		if (!sidePanelOnly) {
			int saved = dc.SaveDC();
			// ジャケット/曲情報パネルは直後の DrawSidePanels の Blit が全ピクセルを
			// 書き直すため gap クリア不要。バナー無効化(毎フレーム)とパネル無効化
			// (スクロール/曲変更)が同じ WM_PAINT に合流したとき、ここでクリアすると
			// クリア→再描画の1フレーム空白が曲番号 GDI のちらつきになる。
			if (!m_jacketRect.IsRectEmpty())    dc.ExcludeClipRect(&m_jacketRect);
			if (!m_infoPanelRect.IsRectEmpty()) dc.ExcludeClipRect(&m_infoPanelRect);
			CCC_PaintAeroGaps(dc, this, &m_bannerRect);
			dc.RestoreDC(saved);
		}
		if (hitBanner) BlitVisualizer(&dc);
		DrawSidePanels(&dc);
		COgg_ClearGdiPaintPending();
		return;
	}
#endif
	CPaintDC pdc(this);
	CRect clipBox; pdc.GetClipBox(&clipBox);
	bool hitBanner = !m_bannerRect.IsRectEmpty() && CRect().IntersectRect(&clipBox, &m_bannerRect);
	// 非アクリル時は背景をベース色で塗る。バナー/サイドパネルは直後の Blit/GDI で
	// 完全に上書きするため除外する（先に塗ると一瞬フラッシュしてちらつく）。
	{
		CRect rcc; GetClientRect(&rcc);
		int saved = pdc.SaveDC();
		pdc.ExcludeClipRect(&m_bannerRect);
		if (!m_jacketRect.IsRectEmpty())   pdc.ExcludeClipRect(&m_jacketRect);
		if (!m_infoPanelRect.IsRectEmpty()) pdc.ExcludeClipRect(&m_infoPanelRect);
		CBrush bg(COLOR_DIALOG_BG);
		pdc.FillRect(&rcc, &bg);
		pdc.RestoreDC(saved);
	}
	if (hitBanner) BlitVisualizer(&pdc);
	DrawSidePanels(&pdc);
	COgg_ClearGdiPaintPending();
}

HBRUSH CMediaPlayerDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CCustomBlurDialogExBase::OnCtlColor(pDC, pWnd, nCtlColor);

#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1 && CCC_IsWin11()) {
		// アクリル/Win11: リスト・エディットを暗色で統一(ベースクラスの薄ピンクを上書き)。
		// CTLCOLOR_STATIC/BTN はベースクラスが NULL_BRUSH+黒テキストを返す(そのまま使う)。
		static CBrush s_brListAero(RGB(25, 25, 30));
		static CBrush s_brEditAero(RGB(22, 22, 28));
		if (nCtlColor == CTLCOLOR_LISTBOX) {
			pDC->SetBkColor(RGB(25, 25, 30));
			pDC->SetTextColor(RGB(200, 200, 210));
			return (HBRUSH)s_brListAero.GetSafeHandle();
		}
		if (nCtlColor == CTLCOLOR_EDIT) {
			pDC->SetBkColor(RGB(22, 22, 28));
			pDC->SetTextColor(RGB(200, 200, 210));
			return (HBRUSH)s_brEditAero.GetSafeHandle();
		}
		// CTLCOLOR_STATIC / CTLCOLOR_BTN (グループボックス含む):
		// ベースクラスは黒テキスト+NULL_BRUSH だが、アクリル越しでは見えない場合がある。
		// 白テキストに変更(ガラス上でどんな背景でも視認可)。
		if (nCtlColor == CTLCOLOR_STATIC || nCtlColor == CTLCOLOR_BTN) {
			pDC->SetBkMode(TRANSPARENT);
			pDC->SetTextColor(RGB(230, 230, 230));
			return (HBRUSH)GetStockObject(NULL_BRUSH);
		}
		return hbr;
	}
#endif

	// 非アクリル: ダイアログ背景色で統一
	if (savedata.aero != 1) {
		if (m_brDlg.GetSafeHandle() == NULL)
			m_brDlg.CreateSolidBrush(COLOR_DIALOG_BG);
		if (nCtlColor == CTLCOLOR_DLG)
			return m_brDlg;
		if (nCtlColor == CTLCOLOR_STATIC) {
			pDC->SetBkMode(TRANSPARENT);
			return m_brDlg;
		}
	}
	return hbr;
}

void CMediaPlayerDlg::OnDropFiles(HDROP hDropInfo)
{
	UINT n = ::DragQueryFile(hDropInfo, 0xFFFFFFFF, NULL, 0);
	for (UINT i = 0; i < n; i++) {
		TCHAR path[MAX_PATH];
		if (::DragQueryFile(hDropInfo, i, path, MAX_PATH) > 0) {
			if (MpIsPlaylistExtension(path)) {
				MpShowM3uImportDialog(this, path);
				::DragFinish(hDropInfo);
				return;
			}
		}
	}
	if (pl)
		pl->OnDropFiles(hDropInfo);
	RefreshList(TRUE);
	::DragFinish(hDropInfo);
}

void CMediaPlayerDlg::OnDestroy()
{
	SavePos();
	KillTimer(1);
	KillTimer(7);
	if (::IsWindow(m_list.GetSafeHwnd()))
		RemoveWindowSubclass(m_list.GetSafeHwnd(), ListHeaderNotifySubclassProc, kMpListHdrSubclassId);
	CCustomBlurDialogExBase::OnDestroy();
}

void CMediaPlayerDlg::OnClose()
{
	// メディアプレイヤー画面の×はアプリ終了(メイン画面を閉じる)
	RequestAppShutdown();
}

void CMediaPlayerDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	CSliderCtrl* r = (CSliderCtrl*)pScrollBar;
	if (r && r->GetSafeHwnd() == m_seek.GetSafeHwnd()) {
		if (nSBCode == SB_THUMBTRACK) {
			m_seekDragging = 1;
			CCustomBlurDialogExBase::OnHScroll(nSBCode, nPos, pScrollBar);
			return;
		}
		if (nSBCode == SB_THUMBPOSITION || nSBCode == SB_ENDSCROLL ||
			nSBCode == SB_PAGELEFT || nSBCode == SB_PAGERIGHT ||
			nSBCode == SB_LINELEFT || nSBCode == SB_LINERIGHT) {
			int p = m_seek.GetPos();
			if (og && ::IsWindow(og->GetSafeHwnd())) {
				// og 側の m_time を動かして og の既存シーク処理を流用
				og->m_time.SetPos(p);
				og->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, p), (LPARAM)og->m_time.GetSafeHwnd());
			}
			m_seekDragging = 0;
		}
	}
	else if (r && r->GetSafeHwnd() == m_vol.GetSafeHwnd()) {
		int v = m_vol.GetPos();
		if (og && ::IsWindow(og->GetSafeHwnd()))
			og->m_sl.SetPos(v * 1000);
		CString vs; vs.Format(_T("!@C206830%.1f%%"), (double)v); m_volval.SetWindowText(vs);
	}
	else if (og && ::IsWindow(og->GetSafeHwnd()) && r) {
		// サウンド調整スライダー → og の対応スライダーへ反映(timerp がライブ取得)
		HWND h = r->GetSafeHwnd();
		if (h == m_dsvol.GetSafeHwnd())      og->m_dsval.SetPos(m_dsvol.GetPos());
		else if (h == m_kvol.GetSafeHwnd())  og->m_kakuVol.SetPos(m_kvol.GetPos());
		else if (h == m_tempo.GetSafeHwnd()) og->m_tempo_sl.SetPos(m_tempo.GetPos());
		else if (h == m_pitch.GetSafeHwnd()) og->m_pitch_sl.SetPos(m_pitch.GetPos());
	}
	CCustomBlurDialogExBase::OnHScroll(nSBCode, nPos, pScrollBar);
}

// ----- プレイリスト操作(og/pl 流用) -----
static void MP_PlayIndex(int idx)
{
	if (!pl || idx < 0 || idx >= pl->playcnt) return;
	pl->Get(idx);          // fnn/filen/modesub/loop1/loop2/ret2 をセット + 選択
	plcnt = idx;
	gameon = 0;
	MpPushPlayHistory(pl->pc[idx].fol, pl->pc[idx].name);
	if (og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());  // 再生(再演奏)
}

void CMediaPlayerDlg::OnPrev()
{
	MpTaskbarPrevTrack();
}

void CMediaPlayerDlg::OnNext()
{
	MpTaskbarNextTrack();
}

void CMediaPlayerDlg::OnPlay()
{
	int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
	if (sel >= 0 && pl && sel < pl->playcnt)
		MP_PlayIndex(sel);
	else
		MpTaskbarReplay();
}

void CMediaPlayerDlg::OnPauseBtn()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->OnPause();
	// OnPause は og->m_ps のみ更新するため、表示中の mp ボタンへ即反映
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SyncPauseButtonUi();
}

void CMediaPlayerDlg::OnStopBtn()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON1, BN_CLICKED), 0);
}

void CMediaPlayerDlg::OnEq()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON59, BN_CLICKED), 0);
	SyncPushToggleButtons();
}

void CMediaPlayerDlg::OnPiano()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->TogglePianoRoll();
	SyncPushToggleButtons();
}

void CMediaPlayerDlg::OnAnalyzer()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->ToggleAnalyzer();
	SyncPushToggleButtons();
}

void CMediaPlayerDlg::OnFadeout()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON5, BN_CLICKED), 0);  // フェードアウト
}

void CMediaPlayerDlg::OnFolder()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON9, BN_CLICKED), 0);  // フォルダ設定
}

void CMediaPlayerDlg::OnSettings()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON21, BN_CLICKED), 0);  // 設定
}

void CMediaPlayerDlg::OnJacket()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->OnBnmp3jake();   // ジャケット表示(別窓)
}

void CMediaPlayerDlg::OnExit()
{
	RequestAppShutdown();
}

void CMediaPlayerDlg::OnTempoReset()
{
	// ラベルクリックでテンポを 100%(200) に戻す(og の既存処理を流用)
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		og->OnTempoStatic();
		m_tempo.SetPos(og->m_tempo_sl.GetPos());
	}
}

void CMediaPlayerDlg::OnPitchReset()
{
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		og->OnPitchStatic();
		m_pitch.SetPos(og->m_pitch_sl.GetPos());
	}
}

void CMediaPlayerDlg::OnSwitch()
{
	// mp 自身を破棄する処理(EnterFalcomMode)を mp のハンドラ内で直接呼ばず、og へ委ねる
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_MP_ENTER_FALCOM, 0, 0);
}

void CMediaPlayerDlg::OnRenzoku()
{
	if (!pl) return;
	int st = m_renzoku.GetCheck() ? 1 : 0;
	pl->m_renzoku.SetCheck(st);
	pl->OnBnClickedCheck1();   // 既存処理(保存)
}

void CMediaPlayerDlg::OnLoop()
{
	if (!pl) return;
	int st = m_loop.GetCheck() ? 1 : 0;
	pl->m_loop.SetCheck(st);
	pl->OnBnClickedCheck4();   // 既存処理(保存)
}

void CMediaPlayerDlg::OnRandom()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	int st = m_random.GetCheck() ? 1 : 0;
	// 既存ハンドラ(OnCheck5=ランダム / OnCheck6=順次)を WM_COMMAND で流用
	if (st) og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CHECK5, BN_CLICKED), 0);
	else    og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CHECK6, BN_CLICKED), 0);
}

void CMediaPlayerDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
	if (sel >= 0)
		MP_PlayIndex(sel);
}

// pl のプレイリスト一覧コンボをそのままミラー(全項目 enabled / 論理=物理)
void CMediaPlayerDlg::ReloadPlaylistCombo()
{
	if (!pl || !::IsWindow(pl->m_listchange.GetSafeHwnd()) || !::IsWindow(m_plsel.GetSafeHwnd()))
		return;
	int n = pl->m_listchange.GetCount();
	m_plsel.ResetContent();
	for (int i = 0; i < n; i++) {
		CString s;
		pl->m_listchange.GetLBText(i, s);
		m_plsel.AddString(s);
	}
	m_plsel.SetCurSel(savedata.playlistnum);
	FixPlselDropList(m_plsel, kMpPlselListRowH, MpPlselClosedH(hD2));
	m_lastComboCount = n;
}

void CMediaPlayerDlg::OnPlselDropdown()
{
	FixPlselDropList(m_plsel, kMpPlselListRowH, MpPlselClosedH(hD2));
	ExpandPlselDropListPopup(m_plsel.GetSafeHwnd());
	PostMessage(WM_MP_PLSEL_EXPAND, 0, 0);
}

LRESULT CMediaPlayerDlg::OnPlselExpandPopup(WPARAM, LPARAM)
{
	if (!::IsWindow(m_plsel.GetSafeHwnd())) return 0;
	ExpandPlselDropListPopup(m_plsel.GetSafeHwnd());
	return 0;
}

// mp リストの選択状態を pl リストへ反映(上下移動/削除を pl の既存処理へ委譲するため)
void CMediaPlayerDlg::SyncSelectionToPlaylist()
{
	if (!pl || !::IsWindow(pl->m_lc.GetSafeHwnd())) return;
	int n = m_list.GetItemCount();
	for (int i = 0; i < n && i < pl->playcnt; i++) {
		UINT st = m_list.GetItemState(i, LVIS_SELECTED);
		pl->m_lc.SetItemState(i, (st & LVIS_SELECTED) ? LVIS_SELECTED : 0, LVIS_SELECTED);
	}
}

extern BOOL changeflg;

void CMediaPlayerDlg::OnPlSel()
{
	if (!pl || !::IsWindow(pl->m_listchange.GetSafeHwnd())) return;
	int sel = m_plsel.GetCurSel();
	if (sel < 0) return;
	if (sel == savedata.playlistnum && pl->pc != NULL && pl->playcnt > 0)
		return;
	changeflg = TRUE;
	pl->m_listchange.SetCurSel(sel);
	changeflg = FALSE;
	pl->OnCbnSelchangeCombo1();   // プレイリスト切替/新規作成(既存処理)
	ReloadPlaylistCombo();
	RefreshList(TRUE);
}

void CMediaPlayerDlg::OnPlRename()
{
	if (!pl) return;
	pl->OnBnClickedButton3();
	ReloadPlaylistCombo();
}

void CMediaPlayerDlg::OnPlDelete()
{
	if (!pl) return;
	pl->OnBnClickedPlaydelete();
	ReloadPlaylistCombo();
	RefreshList(TRUE);
}

// 選択行の並べ替え。mp リストの選択を基準に pl->pc を直接並べ替える。
// 旧実装は pl->m_lc(裏側の隠しリスト)の選択状態に依存していたため、
// 選択同期のズレや一番上/上ボタンの取り違え、OnSDOWN のヒープ範囲外書込みで
// 「位置がおかしい / 行が消える / 以降の曲が消える」不具合が発生していた。
// ここでは配列を直接操作し、再生インデックスと選択を確実に追従させる。
void CMediaPlayerDlg::MoveSelected(int mode)
{
	if (!pl || pl->pc == NULL || pl->playcnt <= 0) return;
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	const int n = pl->playcnt;

	// mp リストの選択フラグを取得
	char* sel = (char*)calloc((size_t)n, 1);
	if (!sel) return;
	int selCount = 0;
	int k = -1;
	while ((k = m_list.GetNextItem(k, LVNI_SELECTED)) != -1) {
		if (k >= 0 && k < n) { sel[k] = 1; selCount++; }
	}
	if (selCount == 0 || selCount == n) { free(sel); return; }  // 何もない/全選択は移動不要

	// arr[新しい位置] = 元のインデックス
	int* arr = (int*)malloc(sizeof(int) * (size_t)n);
	if (!arr) { free(sel); return; }
	int idx = 0;
	if (mode == 0) {                       // 一番上
		for (int i = 0; i < n; i++) if (sel[i]) arr[idx++] = i;
		for (int i = 0; i < n; i++) if (!sel[i]) arr[idx++] = i;
	} else if (mode == 3) {                // 一番下
		for (int i = 0; i < n; i++) if (!sel[i]) arr[idx++] = i;
		for (int i = 0; i < n; i++) if (sel[i]) arr[idx++] = i;
	} else {
		for (int i = 0; i < n; i++) arr[i] = i;
		if (mode == 1) {                   // 上へ1つ(選択ブロックは結合したまま)
			for (int i = 1; i < n; i++)
				if (sel[arr[i]] && !sel[arr[i - 1]]) { int t = arr[i]; arr[i] = arr[i - 1]; arr[i - 1] = t; }
		} else {                           // 下へ1つ
			for (int i = n - 2; i >= 0; i--)
				if (sel[arr[i]] && !sel[arr[i + 1]]) { int t = arr[i]; arr[i] = arr[i + 1]; arr[i + 1] = t; }
		}
	}

	BOOL changed = FALSE;
	for (int i = 0; i < n; i++) if (arr[i] != i) { changed = TRUE; break; }
	if (!changed) { free(arr); free(sel); return; }

	// 並べ替え後の pc を作成し、元→新の位置写像 pos を作る
	playlistdata0* np = (playlistdata0*)malloc(sizeof(playlistdata0) * (size_t)n);
	int* pos = (int*)malloc(sizeof(int) * (size_t)n);
	if (!np || !pos) { free(np); free(pos); free(arr); free(sel); return; }
	for (int i = 0; i < n; i++) { np[i] = pl->pc[arr[i]]; pos[arr[i]] = i; }   // 再生中アイコン等もそのまま追従
	memcpy(pl->pc, np, sizeof(playlistdata0) * (size_t)n);
	free(np);

	// 再生インデックスを追従
	if (plcnt >= 0 && plcnt < n) plcnt = pos[plcnt];
	if (pl->pnt >= 0 && pl->pnt < n) pl->pnt = pos[pl->pnt];
	if (pl->pnt1 >= 0 && pl->pnt1 < n) pl->pnt1 = pos[pl->pnt1];

	// 裏の pl->m_lc の選択も合わせておく(Falcom 画面との整合)
	if (::IsWindow(pl->m_lc.GetSafeHwnd())) {
		for (int i = 0; i < n; i++)
			pl->m_lc.SetItemState(i, sel[arr[i]] ? LVIS_SELECTED : 0, LVIS_SELECTED);
	}

	pl->Save();
	RefreshList(TRUE);

	// mp リストの選択を移動後の行へ追従させる
	int total = m_list.GetItemCount();
	for (int i = total - 1; i >= 0; i--)
		m_list.SetItemState(i, 0, LVIS_SELECTED | LVIS_FOCUSED);
	int firstSel = -1;
	for (int i = 0; i < n && i < total; i++) {
		if (sel[arr[i]]) {
			m_list.SetItemState(i, LVIS_SELECTED | (firstSel < 0 ? LVIS_FOCUSED : 0), LVIS_SELECTED | LVIS_FOCUSED);
			if (firstSel < 0) firstSel = i;
		}
	}
	if (firstSel >= 0) m_list.EnsureVisible(firstSel, FALSE);
	// FollowPlayingRow が選択を奪わないように、再生行の追従基準を更新しておく
	m_lastScroll = (pl->pnt >= 0 && pl->pnt < n) ? pl->pnt : firstSel;

	free(pos); free(arr); free(sel);
}

void CMediaPlayerDlg::OnMoveTop()
{
	MoveSelected(0);
}

void CMediaPlayerDlg::OnMoveUp()
{
	MoveSelected(1);
}

void CMediaPlayerDlg::OnMoveDown()
{
	MoveSelected(2);
}

void CMediaPlayerDlg::OnMoveBottom()
{
	MoveSelected(3);
}

void CMediaPlayerDlg::OnItemDel()
{
	if (!pl) return;
	SyncSelectionToPlaylist();
	pl->Del();
	RefreshList(TRUE);
}

void CMediaPlayerDlg::OnSupe()
{
	if (og && ::IsWindow(og->m_supe.GetSafeHwnd())) {
		int st = (m_supe.GetCheck() == BST_CHECKED) ? 1 : 0;
		og->m_supe.SetCheck(st);
	}
	SyncPushToggleButtons();
}

void CMediaPlayerDlg::OnSt()
{
	if (og && ::IsWindow(og->m_st.GetSafeHwnd())) {
		int st = (m_st.GetCheck() == BST_CHECKED) ? 1 : 0;
		og->m_st.SetCheck(st);
	}
	SyncPushToggleButtons();
}

void CMediaPlayerDlg::OnPrompt()
{
	MpShowPromptDialog(this);
}

void CMediaPlayerDlg::OnM3uExport()
{
	if (!pl || pl->playcnt <= 0) {
		AfxMessageBox(LL14(L"書き出す曲がありません。", L"No tracks to export.", L"Aucune piste.", L"Nessuna traccia.", L"Sin pistas.", L"내보낼 곡이 없습니다.", L"没有可导出的曲目。", L"لا مقاطع.", L"Нет треков.", L"Keine Titel.", L"Sem faixas.", L"Geen nummers.", L"Brak utworow.", L"Disa aktarilacak parca yok."));
		return;
	}
	CFileDialog fd(FALSE, _T("m3u"), _T("playlist.m3u"),
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		LL14(L"M3U プレイリスト (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|すべて (*.*)|*.*||",
			L"M3U playlist (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|All (*.*)|*.*||",
			L"Liste M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Tous (*.*)|*.*||",
			L"Playlist M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Tutti (*.*)|*.*||",
			L"Lista M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Todos (*.*)|*.*||",
			L"M3U 재생목록 (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|모든 (*.*)|*.*||",
			L"M3U 播放列表 (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|全部 (*.*)|*.*||",
			L"قوائم M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|الكل (*.*)|*.*||",
			L"Плейлист M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Все (*.*)|*.*||",
			L"M3U-Playlist (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Alle (*.*)|*.*||",
			L"Lista M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Todos (*.*)|*.*||",
			L"M3U-playlist (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Alle (*.*)|*.*||",
			L"Lista M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Wszystkie (*.*)|*.*||",
			L"M3U listesi (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Tumu (*.*)|*.*||"),
		this);
	if (fd.DoModal() != IDOK) return;
	CString ext = fd.GetFileExt(); ext.MakeLower();
	// .m3u も UTF-8 BOM で出力(日本語パス/曲名を正しく保持)。.m3u8 も同様。
	BOOL utf8 = TRUE;
	UNREFERENCED_PARAMETER(ext);
	if (!MpExportPlaylistM3U(fd.GetPathName(), utf8))
		AfxMessageBox(LL14(L"書き出しに失敗しました。", L"Export failed.", L"Echec export.", L"Esportazione fallita.", L"Error al exportar.", L"내보내기 실패.", L"导出失败。", L"فشل التصدير.", L"Ошибка экспорта.", L"Export fehlgeschlagen.", L"Falha na exportacao.", L"Exporteren mislukt.", L"Eksport nieudany.", L"Disa aktarma basarisiz."));
}

void CMediaPlayerDlg::OnM3uImport()
{
	MpShowM3uImportDialog(this);
}

BOOL CMediaPlayerDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (HIWORD(wParam) == THBN_CLICKED) {
		UINT id = LOWORD(wParam);
		if (id == 0) OnPlay();
		else if (id == 1) OnPauseBtn();
		else if (id == 2) OnStopBtn();
		else if (id == 3) OnNext();
		return TRUE;
	}
	return CCustomBlurDialogExBase::OnCommand(wParam, lParam);
}

void CMediaPlayerDlg::OnTip()
{
	if (pl && ::IsWindow(pl->m_tool.GetSafeHwnd()))
		pl->m_tool.SetCheck(m_tip.GetCheck() ? 1 : 0);   // pl のタイマーが反映
	ApplyListTooltipState();   // m_list にも即時反映
}

void CMediaPlayerDlg::OnMini()
{
	if (pl && ::IsWindow(pl->m_saisyo.GetSafeHwnd()))
		pl->m_saisyo.SetCheck(m_mini.GetCheck() ? 1 : 0);
}

void CMediaPlayerDlg::OnSaveMp3()
{
	if (!pl) return;
	pl->m_save_mp3.SetCheck(m_savemp3.GetCheck() ? 1 : 0);
	pl->OnBnClickedCheck6mp3();   // savedata へ保存(既存処理)
}

void CMediaPlayerDlg::OnSaveDs()
{
	if (!pl) return;
	pl->m_save_kpi.SetCheck(m_saveds.GetCheck() ? 1 : 0);
	pl->OnBnClickedCheck7dshow();
}

void CMediaPlayerDlg::OnSaveWav()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	og->m_c2.SetCheck(m_savewav.GetCheck() ? 1 : 0);
}

void CMediaPlayerDlg::OnSaveParam()
{
	savedata.saveSongParams = m_saveparam.GetCheck() ? 1 : 0;
	// チェック状態はすぐ .dat へ(再起動でフラグが消えるとツールチップも出ない)
	MpPersistSavedataQuick();
}

void CMediaPlayerDlg::OnResetData()
{
	CString msg = LL14(
		L"曲ごとに保存した設定をすべて削除し、音量・EQ など各種パラメータを初期状態に戻します。よろしいですか？",
		L"Delete all per-song saved settings and reset volume, EQ and other parameters to defaults. Continue?",
		L"Supprimer tous les reglages par morceau et reinitialiser les parametres ?",
		L"Eliminare tutte le impostazioni per brano e ripristinare i parametri?",
		L"¿Eliminar todos los ajustes por pista y restablecer los parámetros?",
		L"곡별 저장 설정을 모두 삭제하고 볼륨·EQ 등 파라미터를 초기화합니다. 계속할까요?",
		L"删除所有逐曲保存的设置，并将音量、EQ等参数重置为默认。是否继续？",
		L"حذف كل الإعدادات المحفوظة لكل أغنية وإعادة الضبط؟",
		L"Удалить все сохранённые настройки для треков и сбросить параметры?",
		L"Alle pro-Titel-Einstellungen loeschen und Parameter zuruecksetzen?",
		L"Excluir todas as configuracoes por faixa e redefinir os parametros?",
		L"Alle per-nummer-instellingen verwijderen en parameters resetten?",
		L"Usunąć wszystkie ustawienia na utwór i zresetować parametry?",
		L"Tüm parça ayarlarını silip parametreleri sıfırlansın mı?");
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;

	// 保存ファイルとメモリ内テーブルを破棄
	SongParams_ResetAll();

	// ライブのパラメータを既定へ: DS音量50% / 拡張音量100% / その他デフォルト
	SongParam d;
	ZeroMemory(&d, sizeof(d));
	d.dsvol = -249;       // 50%
	d.kakuVol = 100;      // 100%
	d.pitchPos = 200;     // 100%
	d.tempoPos = 200;     // 100%
	for (int i = 0; i < 20; i++) d.eq[i] = 100; // フラット/中立
	d.eqsoundenv = 0;
	d.eqsoundeq = 0;
	d.eqsoundeffect = 50; // 環境のかかり具合 既定
	d.eq_reverb = 0;
	d.eq_chorus = 0;
	d.eq_delay = 0;
	d.analyzerspecstyle = 0;
	SongParams_ApplyEntryToMain(d);
}

void CMediaPlayerDlg::OnKaisuuKillFocus()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	CString s;
	m_kaisuu.GetWindowText(s);
	s.Trim();
	int n = _tstoi(s);
	if (n < 1) n = 1;
	s.Format(_T("%d"), n);
	m_kaisuu.SetWindowText(s);
	og->m_kaisuu.SetWindowText(s);
	savedata.kaisuu = n;
	MpPersistSavedataQuick();
}

// リスト右クリック: 詳細編集 / WAV保存 / 削除 / 記憶パラメータ削除 / 他リスト移動・コピー / 存在しないファイル削除
void CMediaPlayerDlg::OnRclickList(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	if (!pl) return;
	CPoint pt;
	::GetCursorPos(&pt);
	CPoint clientPt = pt;
	m_list.ScreenToClient(&clientPt);
	const int hit = m_list.HitTest(clientPt, NULL);
	if (hit >= 0 && !(m_list.GetItemState(hit, LVIS_SELECTED) & LVIS_SELECTED)) {
		const int n = m_list.GetItemCount();
		for (int i = 0; i < n; ++i)
			m_list.SetItemState(i, 0, LVIS_SELECTED);
		m_list.SetItemState(hit, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
	SyncSelectionToPlaylist();
	const int cmd = pl->ShowTrackContextMenu(pt, this);
	if (cmd != 0)
		pl->HandleTrackContextCmd(cmd);
}

// リストでの DELETE キー押下: 選択曲を削除（プレイリスト本体の Del を流用）
void CMediaPlayerDlg::OnKeydownList(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLVKEYDOWN pLVKeyDown = reinterpret_cast<LPNMLVKEYDOWN>(pNMHDR);
	*pResult = 0;
	if (!pl) return;
	if (pLVKeyDown && pLVKeyDown->wVKey == VK_DELETE) {
		SyncSelectionToPlaylist();
		pl->Del();
		RefreshList(TRUE);
	}
}

// あいまい検索: pl のキーワード欄へ転記して pl の検索処理を流用し、結果を mp リストへ反映
void CMediaPlayerDlg::OnFindUp()
{
	if (!pl || !::IsWindow(pl->GetSafeHwnd())) return;
	CString s; m_find.GetWindowText(s);
	pl->m_find.SetWindowText(s);
	pl->OnFindUp();
	int i = pl->pnt1;
	if (i >= 0 && i < pl->playcnt && i < m_list.GetItemCount()) {
		for (int k = m_list.GetItemCount() - 1; k >= 0; k--)
			m_list.SetItemState(k, 0, LVIS_SELECTED);
		m_list.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_list.EnsureVisible(i, FALSE);
		m_lastScroll = i;
	}
	if (m_find.GetSafeHwnd()) m_find.SetFocus();
}

void CMediaPlayerDlg::OnFindDown()
{
	if (!pl || !::IsWindow(pl->GetSafeHwnd())) return;
	CString s; m_find.GetWindowText(s);
	pl->m_find.SetWindowText(s);
	pl->OnFindDown();
	int i = pl->pnt1;
	if (i >= 0 && i < pl->playcnt && i < m_list.GetItemCount()) {
		for (int k = m_list.GetItemCount() - 1; k >= 0; k--)
			m_list.SetItemState(k, 0, LVIS_SELECTED);
		m_list.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_list.EnsureVisible(i, FALSE);
		m_lastScroll = i;
	}
	if (m_find.GetSafeHwnd()) m_find.SetFocus();
}

// pc[] を src→dst へ移動(リスト内ドラッグ移動)。再生インデックスも追従。
static void MP_MovePlaylistItem(int src, int dst)
{
	if (!pl || !pl->pc) return;
	int n = pl->playcnt;
	if (src < 0 || src >= n || dst < 0 || dst >= n || src == dst) return;
	playlistdata0 tmp = pl->pc[src];
	if (src < dst) for (int i = src; i < dst; i++) pl->pc[i] = pl->pc[i + 1];
	else           for (int i = src; i > dst; i--) pl->pc[i] = pl->pc[i - 1];
	pl->pc[dst] = tmp;
	auto adj = [&](int idx)->int {
		if (idx == src) return dst;
		if (src < dst && idx > src && idx <= dst) return idx - 1;
		if (src > dst && idx >= dst && idx < src) return idx + 1;
		return idx;
	};
	plcnt = adj(plcnt);
	pl->pnt = adj(pl->pnt);
	pl->pnt1 = adj(pl->pnt1);
	if (::IsWindow(pl->m_lc.GetSafeHwnd())) pl->m_lc.RedrawWindow();
	pl->Save();
}

void CMediaPlayerDlg::OnBeginDragList(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	LPNMLISTVIEW nm = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	m_dragSrc = nm->iItem;
	if (m_dragSrc < 0) return;
	m_dragging = 1;
	// ドラッグ画像(プレイリストと同様の見た目)
	POINT ptHot = { 0,0 };
	m_hDragImage = ListView_CreateDragImage(m_list.m_hWnd, m_dragSrc, &ptHot);
	if (m_hDragImage) {
		ImageList_BeginDrag(m_hDragImage, 0, 0, 0);
		POINT pc = nm->ptAction;          // リストクライアント座標
		m_list.ClientToScreen(&pc);
		ScreenToClient(&pc);
		ImageList_DragEnter(GetSafeHwnd(), pc.x, pc.y);
	}
	SetCapture();
}

// ミニジャケット(幅拡張時に左へ分離表示する正方形ジャケ)クリックで、
// ジャケボタンと同じくジャケット拡大表示を開く。座標は DoLayout が m_jacketRect を
// 毎リサイズ更新するので、リサイズで位置が変わっても追従する。
// ジャケ分離していない狭い窓ではバナー内蔵ジャケなので、バナー領域クリックでも開く。
void CMediaPlayerDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	BOOL hasJacket = (og && og->jx > 0 && !og->img.IsNull());
	if (g_mpSideJacket && !m_jacketRect.IsRectEmpty() && m_jacketRect.PtInRect(point)) {
		if (hasJacket) OnJacket();
		return;
	}
	if (!g_mpSideJacket && !m_bannerRect.IsRectEmpty() && m_bannerRect.PtInRect(point)) {
		if (hasJacket) OnJacket();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CMediaPlayerDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	// バナー上ホバーで og と同じジャケットアニメを発火(ジャケ分離中は無効)
	g_mpBannerHover = (!g_mpSideJacket && m_bannerRect.PtInRect(point)) ? 1 : 0;
	// ジャケ拡大できる領域(ミニジャケ or バナー内蔵ジャケ)では手のひらカーソル
	if (!m_dragging) {
		BOOL hasJacket = (og && og->jx > 0 && !og->img.IsNull());
		const bool overJacket = hasJacket && (
			(g_mpSideJacket && !m_jacketRect.IsRectEmpty() && m_jacketRect.PtInRect(point)) ||
			(!g_mpSideJacket && !m_bannerRect.IsRectEmpty() && m_bannerRect.PtInRect(point)));
		if (overJacket)
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
	}
	if (m_dragging) {
		::SetCursor(::LoadCursor(NULL, IDC_HAND));
		if (m_hDragImage) {
			ImageList_DragMove(point.x, point.y);
		}
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

void CMediaPlayerDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_dragging) {
		m_dragging = 0;
		ReleaseCapture();
		if (m_hDragImage) {
			ImageList_DragLeave(GetSafeHwnd());
			ImageList_EndDrag();
			ImageList_Destroy(m_hDragImage);
			m_hDragImage = NULL;
		}
		// ドロップ先の行を mp リスト座標で判定
		CPoint sp = point; ClientToScreen(&sp);
		CPoint lp = sp; m_list.ScreenToClient(&lp);
		UINT fl = 0;
		int dst = m_list.HitTest(lp, &fl);
		if (dst < 0) {
			CRect rc; m_list.GetClientRect(&rc);
			if (lp.y >= rc.bottom) dst = m_list.GetItemCount() - 1; // 末尾へ
		}
		if (pl && m_dragSrc >= 0 && dst >= 0 && dst != m_dragSrc && dst < pl->playcnt) {
			MP_MovePlaylistItem(m_dragSrc, dst);
			RefreshList(TRUE);
			if (dst < m_list.GetItemCount()) {
				m_list.SetItemState(dst, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				m_list.EnsureVisible(dst, FALSE);
			}
		}
		m_dragSrc = -1;
	}
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

/////////////////////////////////////////////////////////////////////////////
// モード切替
/////////////////////////////////////////////////////////////////////////////
// ファルコム特化型 → メディアプレイヤーモードへ切替。
// mp を新規生成し og/pl を非表示にする。og は再生エンジンとして裏で動き続ける。
// DWM アクリル問題対策として mp をトップレベル化(オーナー解除)してから RefreshAeroMode する。
void EnterMediaPlayerMode()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	if (mp && ::IsWindow(mp->GetSafeHwnd())) {  // 既に MP モード
		mp->ShowWindow(SW_SHOW);
		return;
	}

	// プレイリストを必ず生成(裏で生かす)。再生はプレイリスト方式にする。
	if (!pl) {
		killw1 = 0;
		pl = new CPlayList;
		if (!pl->Create(og)) {
			delete pl;
			pl = NULL;
		}
	}
	if (!pl || !::IsWindow(pl->GetSafeHwnd()))
		return;
	plw = 1;
	savedata.playerMode = 1;

	// メディアプレイヤー画面を生成・表示。
	// オーナーは og のまま(EQ/簡易ピアノロールと同じアクリルグループ)にして、
	// 非アクティブ時もアクリルが維持されるようにする。トップレベル化(オーナー解除)は
	// 孤立窓となり非アクティブでアクリルが落ちるため行わない。タスクバー単独表示は
	// PreCreateWindow の WS_EX_APPWINDOW で確保する。
	mp = new CMediaPlayerDlg;
	if (!mp->Create(og) || !::IsWindow(mp->GetSafeHwnd())) {
		delete mp;
		mp = NULL;
		return;
	}
#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1)
		mp->RefreshAeroMode();
#endif

	// 重複防止: プレイリスト/メイン画面/aeroオーバーレイの単独ウィンドウを隠す
	extern CImageBase* maini;
	extern CImageBase* playbase;
	if (pl && ::IsWindow(pl->GetSafeHwnd()))
		::ShowWindow(pl->m_hWnd, SW_HIDE);
	if (maini && ::IsWindow(maini->GetSafeHwnd()))
		::ShowWindow(maini->m_hWnd, SW_HIDE);
	if (playbase && ::IsWindow(playbase->GetSafeHwnd()))
		::ShowWindow(playbase->m_hWnd, SW_HIDE);
	// イコライザー/簡易ピアノロールはオプション窓なので閉じない(そのまま維持)
	::ShowWindow(og->m_hWnd, SW_HIDE);

	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
		::SetForegroundWindow(mp->m_hWnd);
		mp->SetFocus();
		SetupTaskbarThumbButtons(mp->m_hWnd, TRUE);
		RefreshTaskbarJumpList(TRUE);
#if CCUSTOM_AERO_SUPPORT
		if (savedata.aero == 1)
			mp->RefreshAeroMode();   // 前面化後に再適用
#endif
		// mp 生成・配置後にオフセット再計算(早すぎると og 基準になり座標が狂う)
		CCC_MainLockRefreshOffsetsFor(mp);
	}
}

// メディアプレイヤー → ファルコム特化型モードへ切替。
// mp を破棄して og を再表示する。EnterFalcomMode 自体は og->m_hWnd の OnReceive
// (WM_MP_ENTER_FALCOM)から遅延呼び出しされるため、mp のハンドラ内で mp を破棄
// してしまう問題を避けられる。
void EnterFalcomMode()
{
	savedata.playerMode = 0;

	// ファルコム特化型では内蔵(ミニ)ジャケを必ず表示するため抑止フラグを解除。
	g_mpSideJacket = 0;

	// メディアプレイヤー画面を破棄
	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
		mp->SavePos();
		mp->DestroyWindow();
	}
	if (mp) { delete mp; mp = NULL; }

	// ファルコム特化型: タスクバーを og 用に戻す
	if (og && ::IsWindow(og->m_hWnd)) {
		SetupTaskbarThumbButtons(og->m_hWnd, FALSE);
		RefreshTaskbarJumpList(FALSE);
	}

	// メイン画面を表示し、aero/全コントロールを確実に再反映(▲▼の開閉状態は保持=Resizeは呼ばない)
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		::ShowWindow(og->m_hWnd, SW_SHOW);
		::SetForegroundWindow(og->m_hWnd);
		CCC_GroupBoxesBack(og->m_hWnd);   // 枠を最背面へ(チェックボックスを覆わない)
#if CCUSTOM_AERO_SUPPORT
		og->RefreshAeroMode();                   // アクリル/非アクリルを再適用
#endif
		CCC_RefreshKids(og->m_hWnd);   // 再表示時の子コントロール再描画
		og->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
		og->PostRefreshAllAeroWindows();         // EQ/ピアノ/プレイリスト等も再反映
		// og 再表示後に mp 基準→og 基準へオフセットを付け替え
		CCC_MainLockRefreshOffsetsFor(og);
	}

	// プレイリストは savedata.pl に従って表示/非表示
	if (pl && ::IsWindow(pl->GetSafeHwnd())) {
		if (savedata.pl) {
			::ShowWindow(pl->m_hWnd, SW_SHOW);
			plw = 1;
		}
		else {
			::ShowWindow(pl->m_hWnd, SW_HIDE);
			plw = 0;
		}
	}

	// aero オーバーレイを復帰(aero==2 のときのみ存在)
	extern CImageBase* maini;
	extern CImageBase* playbase;
	if (maini && ::IsWindow(maini->GetSafeHwnd()))
		::ShowWindow(maini->m_hWnd, SW_SHOW);
	if (playbase && ::IsWindow(playbase->GetSafeHwnd()) && savedata.pl)
		::ShowWindow(playbase->m_hWnd, SW_SHOW);
}
