// CMediaPlayerDlg.cpp : メディアプレイヤーモード画面(張りぼて)とモード選択ダイアログ
//
// 実体は COggDlg(og->) と CPlayList(pl->)。ここは表示と操作の取り次ぎだけを行う。
// メディアプレイヤーモード中は og / pl のウィンドウを非表示にして裏で生かしておく。
//
#include "stdafx.h"
#include "ogg.h"
#include "CMediaPlayerDlg.h"
#include "CImageBase.h"
#include "Mp3Image.h"
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
extern ITaskbarList3* ptl;   // タスクバー進捗(oggDlg.cpp で初期化)
extern CDC dc;   // COggDlg のオフスクリーン合成面(スペアナ+ジャケ+時間)を流用
extern void ShowOggAboutDialog(CWnd* pParent);   // バージョン情報ダイアログ(oggDlg.cpp)

// og 側のオフスクリーン面のソース寸法(oggDlg.cpp の OnPaint と一致させる: srcW=MDCP+5)
static const int MP_SRCW = (88 * 2 + 175) * 4 + 5; // = 1409 (og の srcW と一致)
static const int MP_SRCH = (81 + 16) * 4;          // = 388

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

	SetWindowText(LL14(L"起動モードの選択", L"Select startup mode", L"Selection du mode de demarrage", L"Selezione modalita di avvio", L"Seleccion del modo de inicio", L"시작 모드 선택", L"选择启动模式", L"اختيار وضع البدء", L"Выбор режима запуска", L"Startmodus auswahlen", L"Selecionar modo de inicializacao", L"Opstartmodus selecteren", L"Tryb uruchamiania", L"Ba?lang?c modunu sec"));
	SetDlgItemText(IDC_STATIC, LL14(L"どちらの画面で起動しますか？", L"Which screen to start with?", L"Quel ecran au demarrage ?", L"Quale schermata avviare?", L"?Con que pantalla iniciar?", L"어느 화면으로 시작할까요?", L"以哪个画面启动？", L"بأي شاشة تبدأ؟", L"С какого экрана начать?", L"Mit welchem Bildschirm starten?", L"Qual tela iniciar?", L"Met welk scherm starten?", L"Ktorym ekranem uruchomic?", L"Hangi ekranla ba?las?n?"));
	m_btnFalcom.SetWindowText(LL14(L"ファルコムbgm特化型画面", L"Falcom BGM dedicated screen", L"Ecran dedie BGM Falcom", L"Schermata BGM Falcom", L"Pantalla dedicada BGM Falcom", L"팔콤 BGM 전용 화면", L"Falcom BGM 专用画面", L"شاشة Falcom BGM المخصصة", L"Экран Falcom BGM", L"Falcom-BGM-Bildschirm", L"Tela dedicada Falcom BGM", L"Falcom BGM-scherm", L"Ekran Falcom BGM", L"Falcom BGM ekran?"));
	m_btnMedia.SetWindowText(LL14(L"メディアプレイヤー画面", L"Media player screen", L"Ecran lecteur multimedia", L"Schermata lettore multimediale", L"Pantalla reproductor multimedia", L"미디어 플레이어 화면", L"媒体播放器画面", L"شاشة مشغل الوسائط", L"Экран медиаплеера", L"Media-Player-Bildschirm", L"Tela do reprodutor de midia", L"Mediaspeler-scherm", L"Ekran odtwarzacza multimediow", L"Medya oynat?c? ekran?"));
	m_ask.SetWindowText(LL14(L"次回も起動時に確認する", L"Ask again next startup", L"Demander au prochain demarrage", L"Chiedi al prossimo avvio", L"Preguntar en el proximo inicio", L"다음에도 시작 시 확인", L"下次启动时也询问", L"اسأل في المرة القادمة", L"Спрашивать при следующем запуске", L"Beim nachsten Start fragen", L"Perguntar no proximo inicio", L"Volgende keer opnieuw vragen", L"Zapytaj przy nast?pnym starcie", L"Sonraki ac?l??ta tekrar sor"));
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

CMediaPlayerDlg::CMediaPlayerDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CMediaPlayerDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_lastCount = -1;
	m_lastPlcnt = -2;
	m_lastScroll = -2;
	m_lastComboCount = -1;
	m_lastMs2 = 0;
	m_seekDragging = 0;
	m_lastPlayIcon = -999;
	m_savedEqVisible = 0;
	m_savedPianoVisible = 0;
	m_inSizeMove = false;
	m_dragging = 0;
	m_dragSrc = -1;
	m_hDragImage = NULL;
	hD2 = 1.0f;
	m_bannerRect.SetRectEmpty();
	m_jacketRect.SetRectEmpty();
	m_infoPanelRect.SetRectEmpty();
	m_bannerCacheW = 0;
	m_bannerCacheH = 0;
	for (int i = 0; i < kInfoRows; i++) { m_isc[i] = 0; m_iscW[i] = 0; }
	m_iscActive    = false;
	m_lastInfoPanelW = 0;
}

CMediaPlayerDlg::~CMediaPlayerDlg()
{
}

void CMediaPlayerDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MP_TITLE, m_title);
	DDX_Control(pDX, IDC_MP_ARTIST, m_artist);
	DDX_Control(pDX, IDC_MP_ALBUM, m_album);
	DDX_Control(pDX, IDC_MP_LRC, m_lrc);
	DDX_Control(pDX, IDC_MP_LRC2, m_lrc2);
	DDX_Control(pDX, IDC_MP_LRC3, m_lrc3);
	DDX_Control(pDX, IDC_MP_LRC4, m_lrc4);
	DDX_Control(pDX, IDC_MP_LRC5, m_lrc5);
	DDX_Control(pDX, IDC_MP_OS, m_os);
	DDX_Control(pDX, IDC_MP_CPU, m_cpu);
	DDX_Control(pDX, IDC_MP_OS3, m_os3);
	DDX_Control(pDX, IDC_MP_TIME, m_time);
	DDX_Control(pDX, IDC_MP_VOLVAL, m_volval);
	DDX_Control(pDX, IDC_MP_VOL_L, m_vollabel);
	DDX_Control(pDX, IDC_MP_SEEK, m_seek);
	DDX_Control(pDX, IDC_MP_VOL, m_vol);
	DDX_Control(pDX, IDC_MP_PREV, m_prev);
	DDX_Control(pDX, IDC_MP_PLAY, m_play);
	DDX_Control(pDX, IDC_MP_PAUSE, m_pause);
	DDX_Control(pDX, IDC_MP_STOP, m_stop);
	DDX_Control(pDX, IDC_MP_NEXT, m_next);
	DDX_Control(pDX, IDC_MP_EQ, m_eq);
	DDX_Control(pDX, IDC_MP_PIANO, m_piano);
	DDX_Control(pDX, IDC_MP_SWITCHMODE, m_switch);
	DDX_Control(pDX, IDC_MP_SETTINGS, m_settings);
	DDX_Control(pDX, IDC_MP_EXIT, m_exit);
	DDX_Control(pDX, IDC_MP_JACK, m_jacket);
	DDX_Control(pDX, IDC_MP_FADEOUT, m_fadeout);
	DDX_Control(pDX, IDC_MP_FOLDER, m_folder);
	DDX_Control(pDX, IDC_MP_DSVOL, m_dsvol);
	DDX_Control(pDX, IDC_MP_DSVOL_L, m_dsvolL);
	DDX_Control(pDX, IDC_MP_KVOL, m_kvol);
	DDX_Control(pDX, IDC_MP_KVOL_L, m_kvolL);
	DDX_Control(pDX, IDC_MP_TEMPO, m_tempo);
	DDX_Control(pDX, IDC_MP_TEMPO_L, m_tempoL);
	DDX_Control(pDX, IDC_MP_PITCH, m_pitch);
	DDX_Control(pDX, IDC_MP_PITCH_L, m_pitchL);
	DDX_Control(pDX, IDC_MP_RENZOKU, m_renzoku);
	DDX_Control(pDX, IDC_MP_LOOP, m_loop);
	DDX_Control(pDX, IDC_MP_RANDOM, m_random);
	DDX_Control(pDX, IDC_MP_PLSEL, m_plsel);
	DDX_Control(pDX, IDC_MP_PLRENAME, m_plrename);
	DDX_Control(pDX, IDC_MP_PLDELETE, m_pldelete);
	DDX_Control(pDX, IDC_MP_LSUP, m_lsup);
	DDX_Control(pDX, IDC_MP_UP, m_up);
	DDX_Control(pDX, IDC_MP_DOWN, m_down);
	DDX_Control(pDX, IDC_MP_LSDOWN, m_lsdown);
	DDX_Control(pDX, IDC_MP_ITEMDEL, m_itemdel);
	DDX_Control(pDX, IDC_MP_FIND, m_find);
	DDX_Control(pDX, IDC_MP_FINDUP, m_findup);
	DDX_Control(pDX, IDC_MP_FINDDOWN, m_finddown);
	DDX_Control(pDX, IDC_MP_SUPE, m_supe);
	DDX_Control(pDX, IDC_MP_ST, m_st);
	DDX_Control(pDX, IDC_MP_TIP, m_tip);
	DDX_Control(pDX, IDC_MP_MINI, m_mini);
	DDX_Control(pDX, IDC_MP_SAVEMP3, m_savemp3);
	DDX_Control(pDX, IDC_MP_SAVEDS, m_saveds);
	DDX_Control(pDX, IDC_MP_GRP_INFO, m_grpInfo);
	DDX_Control(pDX, IDC_MP_GRP_SND, m_grpSnd);
	DDX_Control(pDX, IDC_MP_GRP_PL, m_grpPl);
	DDX_Control(pDX, IDC_MP_LIST, m_list);
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
	ON_BN_CLICKED(IDC_MP_PLRENAME, &CMediaPlayerDlg::OnPlRename)
	ON_BN_CLICKED(IDC_MP_PLDELETE, &CMediaPlayerDlg::OnPlDelete)
	ON_BN_CLICKED(IDC_MP_LSUP, &CMediaPlayerDlg::OnMoveTop)
	ON_BN_CLICKED(IDC_MP_UP, &CMediaPlayerDlg::OnMoveUp)
	ON_BN_CLICKED(IDC_MP_DOWN, &CMediaPlayerDlg::OnMoveDown)
	ON_BN_CLICKED(IDC_MP_LSDOWN, &CMediaPlayerDlg::OnMoveBottom)
	ON_BN_CLICKED(IDC_MP_ITEMDEL, &CMediaPlayerDlg::OnItemDel)
	ON_BN_CLICKED(IDC_MP_SUPE, &CMediaPlayerDlg::OnSupe)
	ON_BN_CLICKED(IDC_MP_ST, &CMediaPlayerDlg::OnSt)
	ON_BN_CLICKED(IDC_MP_TIP, &CMediaPlayerDlg::OnTip)
	ON_BN_CLICKED(IDC_MP_MINI, &CMediaPlayerDlg::OnMini)
	ON_BN_CLICKED(IDC_MP_SAVEMP3, &CMediaPlayerDlg::OnSaveMp3)
	ON_BN_CLICKED(IDC_MP_SAVEDS, &CMediaPlayerDlg::OnSaveDs)
	ON_BN_CLICKED(IDC_MP_FINDUP, &CMediaPlayerDlg::OnFindUp)
	ON_BN_CLICKED(IDC_MP_FINDDOWN, &CMediaPlayerDlg::OnFindDown)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_ENTERSIZEMOVE()
	ON_WM_EXITSIZEMOVE()
	ON_NOTIFY(LVN_GETDISPINFO, IDC_MP_LIST, &CMediaPlayerDlg::OnGetdispinfoList)
	ON_NOTIFY(NM_DBLCLK, IDC_MP_LIST, &CMediaPlayerDlg::OnDblclkList)
	ON_NOTIFY(NM_RCLICK, IDC_MP_LIST, &CMediaPlayerDlg::OnRclickList)
	ON_NOTIFY(LVN_KEYDOWN, IDC_MP_LIST, &CMediaPlayerDlg::OnKeydownList)
	ON_NOTIFY(LVN_BEGINDRAG, IDC_MP_LIST, &CMediaPlayerDlg::OnBeginDragList)
	ON_MESSAGE(WM_MP_INFO_SCROLL, &CMediaPlayerDlg::OnInfoScrollTick)
	ON_WM_NCACTIVATE()
	ON_WM_SYSCOMMAND()
END_MESSAGE_MAP()

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
	CCustomBlurDialogExBase::OnInitDialog();

	// 子コントロールを親の再描画で塗り潰さない(スタティック消失・リスト欠け・ちらつき防止)
	ModifyStyle(0, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

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

	SetWindowText(LL14(L"メディアプレイヤー", L"Media Player", L"Lecteur multimedia", L"Lettore multimediale", L"Reproductor multimedia", L"미디어 플레이어", L"媒体播放器", L"مشغل الوسائط", L"Медиаплеер", L"Media-Player", L"Reprodutor de midia", L"Mediaspeler", L"Odtwarzacz multimediow", L"Medya Oynat?c?"));

	m_play.SetWindowText(LL14(L"再生", L"Play", L"Lire", L"Riproduci", L"Reproducir", L"재생", L"播放", L"تشغيل", L"Играть", L"Wiedergabe", L"Reproduzir", L"Afspelen", L"Odtwarzaj", L"Oynat"));
	m_pause.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시정지", L"暂停", L"إيقاف مؤقت", L"Пауза", L"Pause", L"Pausar", L"Pauze", L"Pauza", L"Duraklat"));
	m_stop.SetWindowText(LL14(L"停止", L"Stop", L"Arret", L"Stop", L"Detener", L"정지", L"停止", L"إيقاف", L"Стоп", L"Stopp", L"Parar", L"Stop", L"Stop", L"Durdur"));
	m_renzoku.SetWindowText(LL14(L"連続再生", L"Continuous", L"Lect. continue", L"Continua", L"Continua", L"연속 재생", L"连续播放", L"تشغيل متتابع", L"Подряд", L"Folge", L"Continuo", L"Doorlopend", L"Ci?g?e", L"Surekli cal"));
	m_loop.SetWindowText(LL14(L"ループ再生", L"Loop play", L"Lecture boucle", L"Riproduci loop", L"Repetir", L"루프 재생", L"循环播放", L"تشغيل متكرر", L"Цикл", L"Schleife", L"Repetir", L"Lus afspelen", L"Odtwarz. p?tli", L"Donguye al"));
	m_random.SetWindowText(LL14(L"ランダム再生", L"Random play", L"Lect. aleatoire", L"Casuale", L"Aleatorio", L"랜덤 재생", L"随机播放", L"تشغيل عشوائي", L"Случайно", L"Zufall", L"Aleatorio", L"Willekeurig", L"Losowo", L"Rastgele cal"));
	m_eq.SetWindowText(LL14(L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ"));
	m_piano.SetWindowText(LL14(L"ピアノ", L"Piano", L"Piano", L"Piano", L"Piano", L"피아노", L"钢琴", L"بيانو", L"Пиано", L"Klavier", L"Piano", L"Piano", L"Pianino", L"Piyano"));
	m_switch.SetWindowText(LL14(L"ファルコム特化型へ", L"To Falcom screen", L"Vers ecran Falcom", L"Alla schermata Falcom", L"A pantalla Falcom", L"팔콤 화면으로", L"切换到Falcom画面", L"إلى شاشة Falcom", L"К экрану Falcom", L"Zum Falcom-Bildschirm", L"Para tela Falcom", L"Naar Falcom-scherm", L"Do ekranu Falcom", L"Falcom ekran?na"));
	m_settings.SetWindowText(LL14(L"設定", L"Settings", L"Reglages", L"Impostazioni", L"Ajustes", L"설정", L"设置", L"إعدادات", L"Настройки", L"Einstellungen", L"Config.", L"Instellingen", L"Ustawienia", L"Ayarlar"));
	m_jacket.SetWindowText(LL14(L"ジャケ", L"Cover", L"Pochette", L"Copertina", L"Caratula", L"커버", L"封面", L"الغلاف", L"Обложка", L"Cover", L"Capa", L"Omslag", L"Ok?adka", L"Kapak"));
	m_exit.SetWindowText(LL14(L"終了", L"Exit", L"Quitter", L"Esci", L"Salir", L"종료", L"退出", L"خروج", L"Выход", L"Beenden", L"Sair", L"Afsluiten", L"Zako?cz", L"C?k??"));
	m_fadeout.SetWindowText(LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza", L"Desvanecer", L"페이드 아웃", L"淡出", L"تلاشي", L"Затухание", L"Ausblenden", L"Desvanecer", L"Fade out", L"Zanikanie", L"Solukla?t?r"));
	m_folder.SetWindowText(LL14(L"フォルダ", L"Folder", L"Dossier", L"Cartella", L"Carpeta", L"폴더", L"文件夹", L"مجلد", L"Папка", L"Ordner", L"Pasta", L"Map", L"Folder", L"Klasor"));
	m_jacket.SetGradation(RGB(255, 235, 245), RGB(255, 200, 225), 0, TRUE);
	m_exit.SetGradation(RGB(255, 210, 210), RGB(255, 160, 160), 0, TRUE);
	m_fadeout.SetGradation(RGB(255, 235, 215), RGB(255, 200, 150), 0, TRUE);
	m_folder.SetGradation(RGB(220, 240, 230), RGB(180, 220, 200), 0, TRUE);
	m_vollabel.SetWindowText(LL14(L"主音量", L"Volume", L"Volume", L"Volume", L"Volumen", L"음량", L"音量", L"الصوت", L"Громкость", L"Lautstarke", L"Volume", L"Volume", L"G?o?no??", L"Ses"));
	m_plrename.SetWindowText(LL14(L"名前変更", L"Rename", L"Renommer", L"Rinomina", L"Renombrar", L"이름변경", L"重命名", L"إعادة تسمية", L"Переименовать", L"Umbenennen", L"Renomear", L"Hernoemen", L"Zmie? nazw?", L"Yeniden adland?r"));
	m_pldelete.SetWindowText(LL14(L"リスト削除", L"Delete list", L"Suppr. liste", L"Elimina lista", L"Eliminar lista", L"목록삭제", L"删除列表", L"حذف القائمة", L"Удалить список", L"Liste loschen", L"Excluir lista", L"Lijst wissen", L"Usu? list?", L"Listeyi sil"));
	m_itemdel.SetWindowText(LL14(L"曲削除", L"Remove", L"Retirer", L"Rimuovi", L"Quitar", L"곡삭제", L"删除曲目", L"حذف", L"Удалить", L"Entfernen", L"Remover", L"Verwijder", L"Usu? utwor", L"Parcay? sil"));
	m_supe.SetWindowText(LL14(L"スペアナ", L"Spectrum", L"Spectre", L"Spettro", L"Espectro", L"스펙트럼", L"频谱", L"الطيف", L"Спектр", L"Spektrum", L"Espectro", L"Spectrum", L"Widmo", L"Spektrum"));
	m_st.SetWindowText(LL14(L"ST", L"ST", L"ST", L"ST", L"ST", L"ST", L"ST", L"ST", L"ST", L"ST", L"ST", L"ST", L"ST", L"ST"));
	m_tip.SetWindowText(LL14(L"ツールチップ", L"Tooltips", L"Info-bulles", L"Suggerimenti", L"Sugerencias", L"툴팁", L"工具提示", L"تلميحات", L"Подсказки", L"Tooltips", L"Dicas", L"Tooltips", L"Etykiety", L"?puclar?"));
	m_mini.SetWindowText(LL14(L"最小化連動", L"Min. sync", L"Min. sync", L"Min. sync", L"Min. sync", L"최소화 연동", L"最小化联动", L"تزامن التصغير", L"Синхр. сверт.", L"Min.-Sync", L"Sinc. min.", L"Min. sync", L"Synch. min.", L"Min. e?itle"));
	m_savemp3.SetWindowText(LL14(L"mp3途中保存", L"mp3 resume", L"mp3 reprise", L"mp3 ripresa", L"mp3 reanudar", L"mp3 위치저장", L"mp3续播", L"حفظ mp3", L"mp3 позиция", L"mp3 Position", L"mp3 retomar", L"mp3 hervat", L"mp3 wznow", L"mp3 surdur"));
	m_saveds.SetWindowText(LL14(L"DShow途中保存", L"DShow resume", L"DShow reprise", L"DShow ripresa", L"DShow reanudar", L"DShow 위치저장", L"DShow续播", L"حفظ DShow", L"DShow позиция", L"DShow Position", L"DShow retomar", L"DShow hervat", L"DShow wznow", L"DShow surdur"));
	m_grpInfo.SetWindowText(LL14(L"情報", L"Info", L"Info", L"Info", L"Info", L"정보", L"信息", L"معلومات", L"Инфо", L"Info", L"Info", L"Info", L"Info", L"Bilgi"));
	m_grpSnd.SetWindowText(LL14(L"サウンド調整", L"Sound", L"Son", L"Audio", L"Sonido", L"사운드", L"声音", L"الصوت", L"Звук", L"Sound", L"Som", L"Geluid", L"D?wi?k", L"Ses"));
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
	m_finddown.SetIcon(IDR_DOWN); m_finddown.SetFlat(TRUE);
	m_findup.SetIcon(IDR_UP);    m_findup.SetFlat(TRUE);

	// サウンド調整スライダー(og の各スライダーと同じ範囲に合わせる)
	m_dsvol.SetRange(-498, 1);  m_dsvol.SetMode(1);
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
	m_switch.SetGradation(RGB(225, 210, 255), RGB(190, 170, 255), 0, TRUE);
	m_settings.SetGradation(RGB(255, 235, 205), RGB(255, 205, 150), 0, TRUE);
	m_plrename.SetGradation(RGB(220, 240, 255), RGB(180, 215, 250), 0, TRUE);
	m_pldelete.SetGradation(RGB(255, 220, 225), RGB(255, 180, 190), 0, TRUE);
	m_itemdel.SetGradation(RGB(255, 220, 225), RGB(255, 180, 190), 0, TRUE);
	CCustomControlUtility::SetControlBackgroundColor(&m_plsel, COLOR_COMBO_BG);
	// タイトルに淡いドロップシャドウで可愛く強調
	m_title.SetDropShadow(RGB(255, 220, 235), 0, 1, 0, TRUE);

	// リスト列(プレイリストと同じ並び)
	DWORD ex = m_list.GetExtendedStyle();
	ex |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_INFOTIP;
	m_list.SetExtendedStyle(ex);
	il.Create(16, 16, ILC_COLOR, 0, 1);   // プレイリストと同じ生成方法に合わせる
	il.Add(AfxGetApp()->LoadIcon(IDI_ICON1));
	il.Add(AfxGetApp()->LoadIcon(IDI_ICON2));
	il.Add(AfxGetApp()->LoadIcon(IDI_ICON3));
	m_list.SetImageList(&il, LVSIL_SMALL);
	m_list.InsertColumn(0, LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"), LVCFMT_LEFT, (int)(220 * hD2));
	m_list.InsertColumn(1, LL14(L"ゲーム", L"Game", L"Jeu", L"Gioco", L"Juego", L"게임", L"游戏", L"لعبة", L"Игра", L"Spiel", L"Jogo", L"Spel", L"Gra", L"Oyun"), LVCFMT_LEFT, (int)(60 * hD2));
	m_list.InsertColumn(2, LL14(L"時間", L"Time", L"Duree", L"Durata", L"Duracion", L"시간", L"时间", L"الوقت", L"Время", L"Zeit", L"Duracao", L"Tijd", L"Czas", L"Sure"), LVCFMT_RIGHT, (int)(60 * hD2));
	m_list.InsertColumn(3, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Kunstler", L"Artista", L"Artiest", L"Artysta", L"Sanatc?"), LVCFMT_LEFT, (int)(160 * hD2));
	m_list.InsertColumn(4, LL14(L"アルバム/コメント", L"Album/Comment", L"Album/Comm.", L"Album/Comm.", L"Album/Com.", L"앨범/댓글", L"专辑/注释", L"الألبوم/تعليق", L"Альбом/Комм.", L"Album/Komm.", L"Album/Coment.", L"Album/Opm.", L"Album/Komentarz", L"Album/Yorum"), LVCFMT_LEFT, (int)(160 * hD2));

	// 保存済みの列幅を復元(0=未設定なら上で設定した既定値のまま)
	for (int ci = 0; ci < 5; ++ci) {
		if (savedata.mpcol[ci] > 0)
			m_list.SetColumnWidth(ci, savedata.mpcol[ci]);
	}

	// フォント
	m_fontTitle.CreateFont(-(int)(20 * hD2), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, _tcslen(savedata.font2) ? savedata.font2 : _T("メイリオ"));
	m_fontInfo.CreateFont(-(int)(13 * hD2), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, _tcslen(savedata.font2) ? savedata.font2 : _T("メイリオ"));
	m_fontList.CreateFont(-(int)(14 * hD2), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, _tcslen(savedata.font2) ? savedata.font2 : _T("メイリオ"));
	// チェックボックス用に少し大きめのフォント(下部チェックを見やすく)
	m_fontChk.CreateFont(-(int)(15 * hD2), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, _tcslen(savedata.font2) ? savedata.font2 : _T("メイリオ"));
	m_tip.SetFont(&m_fontChk, TRUE);
	m_mini.SetFont(&m_fontChk, TRUE);
	m_savemp3.SetFont(&m_fontChk, TRUE);
	m_saveds.SetFont(&m_fontChk, TRUE);
	m_renzoku.SetFont(&m_fontChk, TRUE);
	m_loop.SetFont(&m_fontChk, TRUE);
	m_random.SetFont(&m_fontChk, TRUE);
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
	m_list.SetFont(&m_fontList, TRUE);
	// ツールチップは SyncFromMain が m_tip を確定した後に ApplyListTooltipState で設定

	// タイトルは可愛くピンク強調
	m_title.SetGradation(RGB(255, 105, 180), RGB(150, 60, 160), 0, TRUE);

	m_vol.SetRange(0, 100);
	m_vol.SetPos(100);

	// 初期座標: 保存座標があればそれ、なければファルコム画面の位置・プレイリストの大きさ
	{
		int x = savedata.mpx, y = savedata.mpy, w = savedata.mpw, h = savedata.mph;
		if (!savedata.mpHasPos || w < 100 || h < 100) {
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

	m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_BALLOON);
	m_tooltip.Activate(TRUE);
	m_tooltip.SetDelayTime(TTDT_AUTOPOP, 10000);
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 360);
	m_tooltip.AddTool(&m_switch, LL14(L"ファルコムbgm特化型画面へ戻します。", L"Return to the Falcom BGM dedicated screen.", L"Revenir a l'ecran Falcom.", L"Torna alla schermata Falcom.", L"Volver a la pantalla Falcom.", L"팔콤 전용 화면으로 돌아갑니다.", L"返回Falcom专用画面。", L"العودة إلى شاشة Falcom.", L"Вернуться к экрану Falcom.", L"Zum Falcom-Bildschirm zuruck.", L"Voltar para a tela Falcom.", L"Terug naar Falcom-scherm.", L"Powrot do ekranu Falcom.", L"Falcom ekran?na don."));
	m_tooltip.AddTool(&m_prev, LL14(L"前の曲へ。", L"Previous track.", L"Piste precedente.", L"Traccia precedente.", L"Pista anterior.", L"이전 곡.", L"上一曲。", L"المقطع السابق.", L"Предыдущий трек.", L"Vorheriger Titel.", L"Faixa anterior.", L"Vorige track.", L"Poprzedni utwor.", L"Onceki parca."));
	m_tooltip.AddTool(&m_play, LL14(L"再生 / 選択曲を再生します。", L"Play / play the selected track.", L"Lire la piste selectionnee.", L"Riproduci la traccia selezionata.", L"Reproducir la pista seleccionada.", L"선택한 곡을 재생합니다.", L"播放所选曲目。", L"تشغيل المقطع المحدد.", L"Воспроизвести выбранный трек.", L"Ausgewahlten Titel abspielen.", L"Reproduzir a faixa selecionada.", L"Geselecteerde track afspelen.", L"Odtworz wybrany utwor.", L"Secili parcay? cal."));
	m_tooltip.AddTool(&m_pause, LL14(L"一時停止 / 再開します。", L"Pause / resume.", L"Pause / reprise.", L"Pausa / riprendi.", L"Pausar / reanudar.", L"일시정지 / 재개.", L"暂停/继续。", L"إيقاف مؤقت / استئناف.", L"Пауза / продолжить.", L"Pause / Fortsetzen.", L"Pausar / retomar.", L"Pauze / hervatten.", L"Pauza / wznow.", L"Duraklat / surdur."));
	m_tooltip.AddTool(&m_stop, LL14(L"停止します。", L"Stop.", L"Arreter.", L"Ferma.", L"Detener.", L"정지합니다.", L"停止。", L"إيقاف.", L"Остановить.", L"Stoppen.", L"Parar.", L"Stoppen.", L"Zatrzymaj.", L"Durdur."));
	m_tooltip.AddTool(&m_next, LL14(L"次の曲へ。", L"Next track.", L"Piste suivante.", L"Traccia successiva.", L"Pista siguiente.", L"다음 곡.", L"下一曲。", L"المقطع التالي.", L"Следующий трек.", L"Nachster Titel.", L"Proxima faixa.", L"Volgende track.", L"Nast?pny utwor.", L"Sonraki parca."));
	m_tooltip.AddTool(&m_renzoku, LL14(L"プレイリストを順番に連続再生します。", L"Play the playlist continuously in order.", L"Lecture continue dans l'ordre.", L"Riproduzione continua in ordine.", L"Reproduccion continua en orden.", L"순서대로 연속 재생.", L"按顺序连续播放。", L"تشغيل متواصل بالترتيب.", L"Непрерывное воспроизведение по порядку.", L"Fortlaufend in Reihenfolge abspielen.", L"Reproducao continua em ordem.", L"Doorlopend afspelen op volgorde.", L"Odtwarzaj po kolei.", L"S?rayla surekli cal."));
	m_tooltip.AddTool(&m_loop, LL14(L"選択した曲をループ再生します。", L"Loop the selected track.", L"Lire la piste en boucle.", L"Ripeti la traccia.", L"Repetir la pista.", L"선택한 곡을 반복 재생.", L"循环播放所选曲目。", L"تكرار المقطع المحدد.", L"Зациклить выбранный трек.", L"Ausgewahlten Titel wiederholen.", L"Repetir a faixa selecionada.", L"Geselecteerde track herhalen.", L"Zap?tl wybrany utwor.", L"Secili parcay? donguye al."));
	m_tooltip.AddTool(&m_random, LL14(L"ランダム再生 / 順次再生を切り替えます。", L"Toggle random / sequential play.", L"Lecture aleatoire / sequentielle.", L"Riproduzione casuale / sequenziale.", L"Reproduccion aleatoria / secuencial.", L"랜덤 / 순차 재생 전환.", L"切换随机/顺序播放。", L"تبديل التشغيل العشوائي/المتسلسل.", L"Случайное / последовательное.", L"Zufall / Reihenfolge umschalten.", L"Aleatorio / sequencial.", L"Willekeurig / opeenvolgend.", L"Losowo / po kolei.", L"Rastgele / s?ral?."));
	m_tooltip.AddTool(&m_seek, LL14(L"再生位置。ドラッグでシークします(ループ範囲も表示)。", L"Playback position. Drag to seek (loop range shown).", L"Position de lecture. Glissez pour chercher.", L"Posizione. Trascina per cercare.", L"Posicion. Arrastra para buscar.", L"재생 위치. 드래그로 탐색.", L"播放位置。拖动以定位。", L"موضع التشغيل. اسحب للبحث.", L"Позиция. Перетащите для перемотки.", L"Position. Zum Suchen ziehen.", L"Posicao. Arraste para buscar.", L"Positie. Sleep om te zoeken.", L"Pozycja. Przeci?gnij.", L"Konum. Surukleyerek ara."));
	m_tooltip.AddTool(&m_vol, LL14(L"音量を調整します。", L"Adjust volume.", L"Regler le volume.", L"Regola il volume.", L"Ajustar el volumen.", L"음량을 조절합니다.", L"调整音量。", L"ضبط مستوى الصوت.", L"Регулировка громкости.", L"Lautstarke einstellen.", L"Ajustar o volume.", L"Volume aanpassen.", L"Reguluj g?o?no??.", L"Sesi ayarla."));
	m_tooltip.AddTool(&m_eq, LL14(L"イコライザーを開きます。", L"Open the equalizer.", L"Ouvrir l'egaliseur.", L"Apri l'equalizzatore.", L"Abrir el ecualizador.", L"이퀄라이저를 엽니다.", L"打开均衡器。", L"فتح المعادل.", L"Открыть эквалайзер.", L"Equalizer offnen.", L"Abrir o equalizador.", L"Equalizer openen.", L"Otworz korektor.", L"Ekolayzeri ac."));
	m_tooltip.AddTool(&m_piano, LL14(L"ピアノロールを開きます。", L"Open the piano roll.", L"Ouvrir le rouleau piano.", L"Apri il piano roll.", L"Abrir el rollo de piano.", L"피아노 롤을 엽니다.", L"打开钢琴卷帘。", L"فتح لوحة البيانو.", L"Открыть пианоролл.", L"Klavierrolle offnen.", L"Abrir o piano roll.", L"Pianorol openen.", L"Otworz rolke pianina.", L"Piyano rulosunu ac."));
	m_tooltip.AddTool(&m_jacket, LL14(L"ジャケット画像を別窓で表示します。", L"Show cover art in a separate window.", L"Afficher la pochette.", L"Mostra la copertina.", L"Mostrar la caratula.", L"커버 이미지를 표시합니다.", L"在单独窗口显示封面。", L"عرض صورة الغلاف.", L"Показать обложку.", L"Cover anzeigen.", L"Mostrar a capa.", L"Toon hoes.", L"Poka? ok?adk?.", L"Kapak resmini goster."));
	m_tooltip.AddTool(&m_exit, LL14(L"アプリケーションを終了します。", L"Exit the application.", L"Quitter l'application.", L"Esci dall'applicazione.", L"Salir de la aplicacion.", L"앱을 종료합니다.", L"退出应用程序。", L"إنهاء التطبيق.", L"Выйти из приложения.", L"Anwendung beenden.", L"Sair do aplicativo.", L"Toepassing afsluiten.", L"Zamknij aplikacj?.", L"Uygulamadan c?k."));
	m_tooltip.AddTool(&m_list, LL14(L"ダブルクリックで再生。ファイルをドロップして追加できます。", L"Double-click to play. Drop files to add.", L"Double-clic pour lire. Glissez des fichiers.", L"Doppio clic per riprodurre. Trascina file.", L"Doble clic para reproducir. Suelta archivos.", L"더블 클릭으로 재생. 파일을 드롭해 추가.", L"双击播放。拖入文件添加。", L"انقر مزدوجاً للتشغيل. أفلت الملفات.", L"Двойной клик — воспроизведение. Перетащите файлы.", L"Doppelklick zum Abspielen. Dateien ablegen.", L"Clique duplo para tocar. Solte arquivos.", L"Dubbelklik om af te spelen. Sleep bestanden.", L"Kliknij dwukrotnie. Upu?? pliki.", L"Cift t?kla cal. Dosya b?rak."));
	m_tooltip.AddTool(&m_settings, LL14(L"設定画面を開きます。", L"Open settings.", L"Ouvrir les reglages.", L"Apri le impostazioni.", L"Abrir ajustes.", L"설정 화면을 엽니다.", L"打开设置。", L"فتح الإعدادات.", L"Открыть настройки.", L"Einstellungen offnen.", L"Abrir configuracoes.", L"Instellingen openen.", L"Otworz ustawienia.", L"Ayarlar? ac."));
	m_tooltip.AddTool(&m_fadeout, LL14(L"再生中の曲をフェードアウトして停止します。", L"Fade out and stop the current track.", L"Fondu et arret du morceau.", L"Dissolvenza e stop del brano.", L"Desvanecer y detener la pista.", L"현재 곡을 페이드 아웃하여 정지합니다.", L"淡出并停止当前曲目。", L"تلاشي وإيقاف المقطع الحالي.", L"Затухание и остановка трека.", L"Aktuellen Titel ausblenden und stoppen.", L"Desvanecer e parar a faixa.", L"Huidige track uitfaden en stoppen.", L"Wycisz i zatrzymaj utwor.", L"Parcay? soluklast?r?p durdur."));
	m_tooltip.AddTool(&m_folder, LL14(L"フォルダ設定画面を開きます(フォルダの登録/追加)。", L"Open folder settings (register/add folders).", L"Ouvrir les parametres de dossier.", L"Apri impostazioni cartella.", L"Abrir config. de carpeta.", L"폴더 설정 화면을 엽니다.", L"打开文件夹设置。", L"فتح إعدادات المجلد.", L"Открыть настройки папки.", L"Ordnereinstellungen offnen.", L"Abrir config. de pasta.", L"Mapinstellingen openen.", L"Otworz ustawienia folderu.", L"Klasor ayarlar?n? ac."));
	m_tooltip.AddTool(&m_dsvol, LL14(L"DirectSound音量を調整します。", L"Adjust DirectSound volume.", L"Reglez le volume DirectSound.", L"Regola il volume DirectSound.", L"Ajustar volumen DirectSound.", L"DirectSound 음량 조절.", L"调整DirectSound音量。", L"ضبط مستوى صوت DirectSound.", L"Громкость DirectSound.", L"DirectSound-Lautstarke.", L"Volume DirectSound.", L"DirectSound-volume.", L"G?o?no?? DirectSound.", L"DirectSound sesi."));
	m_tooltip.AddTool(&m_kvol, LL14(L"拡張音量(ブースト)を調整します。", L"Adjust extended (boost) volume.", L"Volume etendu (boost).", L"Volume esteso (boost).", L"Volumen extendido (boost).", L"확장(부스트) 음량 조절.", L"调整扩展(增益)音量。", L"ضبط الصوت الموسع (التعزيز).", L"Расширенная громкость (буст).", L"Erweiterte Lautstarke (Boost).", L"Volume estendido (boost).", L"Uitgebreid (boost) volume.", L"Rozszerzona g?o?no??.", L"Geni?letilmi? ses."));
	m_tooltip.AddTool(&m_tempo, LL14(L"再生テンポを調整します(ラベルをクリックで100%に戻す)。", L"Adjust playback tempo (click label to reset to 100%).", L"Tempo de lecture (clic sur le label = 100%).", L"Tempo (clic sull'etichetta = 100%).", L"Tempo (clic en etiqueta = 100%).", L"재생 템포 조절(라벨 클릭 시 100%).", L"调整播放速度(点击标签恢复100%)。", L"ضبط الإيقاع (انقر التسمية لإعادة 100%).", L"Темп (клик по метке = 100%).", L"Tempo (Label klicken = 100%).", L"Tempo (clique no rotulo = 100%).", L"Tempo (klik label = 100%).", L"Tempo (etykieta = 100%).", L"Tempo (etikete t?kla = %100)."));
	m_tooltip.AddTool(&m_pitch, LL14(L"再生ピッチ(音程)を調整します(ラベルをクリックで100%に戻す)。", L"Adjust playback pitch (click label to reset to 100%).", L"Hauteur (clic sur le label = 100%).", L"Altezza (clic sull'etichetta = 100%).", L"Tono (clic en etiqueta = 100%).", L"재생 피치 조절(라벨 클릭 시 100%).", L"调整音高(点击标签恢复100%)。", L"ضبط طبقة الصوت (انقر التسمية لإعادة 100%).", L"Высота (клик по метке = 100%).", L"Tonhohe (Label klicken = 100%).", L"Tom (clique no rotulo = 100%).", L"Toonhoogte (klik label = 100%).", L"Wysokosc (klik = 100%).", L"Perde (etikete t?kla = %100)."));
	m_tooltip.AddTool(&m_plsel, LL14(L"プレイリストを切り替え/新規追加します。", L"Switch / add a playlist.", L"Changer / ajouter une liste.", L"Cambia / aggiungi playlist.", L"Cambiar / anadir lista.", L"재생목록 전환/추가.", L"切换/新建播放列表。", L"تبديل / إضافة قائمة.", L"Сменить / добавить плейлист.", L"Playlist wechseln / hinzufugen.", L"Trocar / adicionar lista.", L"Playlist wisselen/toevoegen.", L"Zmie?/dodaj list?.", L"Liste de?i?tir/ekle."));
	m_tooltip.AddTool(&m_plrename, LL14(L"現在のプレイリスト名を変更します。", L"Rename the current playlist.", L"Renommer la liste.", L"Rinomina la playlist.", L"Renombrar la lista.", L"현재 재생목록 이름 변경.", L"重命名当前播放列表。", L"إعادة تسمية القائمة.", L"Переименовать плейлист.", L"Playlist umbenennen.", L"Renomear a lista.", L"Lijst hernoemen.", L"Zmie? nazw? listy.", L"Listeyi yeniden adland?r."));
	m_tooltip.AddTool(&m_pldelete, LL14(L"現在のプレイリストを削除します。", L"Delete the current playlist.", L"Supprimer la liste.", L"Elimina la playlist.", L"Eliminar la lista.", L"현재 재생목록 삭제.", L"删除当前播放列表。", L"حذف القائمة.", L"Удалить плейлист.", L"Playlist loschen.", L"Excluir a lista.", L"Lijst verwijderen.", L"Usu? list?.", L"Listeyi sil."));
	m_tooltip.AddTool(&m_up, LL14(L"選択した曲を上へ移動します。", L"Move selected track up.", L"Monter la piste.", L"Sposta su.", L"Subir pista.", L"선택 곡을 위로.", L"上移所选曲目。", L"تحريك لأعلى.", L"Переместить вверх.", L"Nach oben.", L"Mover para cima.", L"Omhoog verplaatsen.", L"Przesu? w gore.", L"Yukar? ta??."));
	m_tooltip.AddTool(&m_down, LL14(L"選択した曲を下へ移動します。", L"Move selected track down.", L"Descendre la piste.", L"Sposta giu.", L"Bajar pista.", L"선택 곡을 아래로.", L"下移所选曲目。", L"تحريك لأسفل.", L"Переместить вниз.", L"Nach unten.", L"Mover para baixo.", L"Omlaag verplaatsen.", L"Przesu? w dol.", L"A?a?? ta??."));
	m_tooltip.AddTool(&m_itemdel, LL14(L"選択した曲をリストから削除します。", L"Remove selected track(s) from the list.", L"Retirer les pistes selectionnees.", L"Rimuovi le tracce selezionate.", L"Quitar pistas seleccionadas.", L"선택 곡을 목록에서 삭제.", L"从列表删除所选曲目。", L"حذف المقاطع المحددة.", L"Удалить выбранные треки.", L"Ausgewahlte Titel entfernen.", L"Remover faixas selecionadas.", L"Geselecteerde tracks verwijderen.", L"Usu? zaznaczone utwory.", L"Secili parcalar? sil."));
	m_tooltip.AddTool(&m_supe, LL14(L"スペアナ表示を切り替えます。", L"Toggle spectrum display.", L"Afficher le spectre.", L"Mostra spettro.", L"Mostrar espectro.", L"스펙트럼 표시 전환.", L"切换频谱显示。", L"تبديل عرض الطيف.", L"Спектр вкл/выкл.", L"Spektrum umschalten.", L"Alternar espectro.", L"Spectrum wisselen.", L"Prze??cz widmo.", L"Spektrumu de?i?tir."));
	m_tooltip.AddTool(&m_st, LL14(L"スペアナのST表示を切り替えます。", L"Toggle spectrum ST mode.", L"Mode ST du spectre.", L"Modalita ST spettro.", L"Modo ST espectro.", L"스펙트럼 ST 모드.", L"切换频谱ST模式。", L"وضع ST للطيف.", L"Режим ST спектра.", L"Spektrum-ST-Modus.", L"Modo ST do espectro.", L"Spectrum ST-modus.", L"Tryb ST widma.", L"Spektrum ST modu."));
	m_tooltip.AddTool(&m_find, LL14(L"あいまい検索キーワード。▲▼で前後検索。", L"Fuzzy search keyword. Use up/down to find.", L"Mot-cle recherche floue.", L"Parola chiave ricerca fuzzy.", L"Palabra busqueda difusa.", L"퍼지 검색어. ▲▼로 검색.", L"模糊搜索关键字。▲▼查找。", L"كلمة بحث غامض.", L"Слово нечеткого поиска.", L"Fuzzy-Suchbegriff.", L"Palavra de busca fuzzy.", L"Fuzzy zoekterm.", L"S?owo wyszukiwania.", L"Bulan?k arama kelimesi."));
	m_tooltip.AddTool(&m_findup, LL14(L"上方向に検索します。", L"Search upward.", L"Chercher vers le haut.", L"Cerca in alto.", L"Buscar arriba.", L"위로 검색.", L"向上搜索。", L"بحث للأعلى.", L"Искать вверх.", L"Aufwarts suchen.", L"Buscar acima.", L"Omhoog zoeken.", L"Szukaj w gore.", L"Yukar? ara."));
	m_tooltip.AddTool(&m_finddown, LL14(L"下方向に検索します。", L"Search downward.", L"Chercher vers le bas.", L"Cerca in basso.", L"Buscar abajo.", L"아래로 검색.", L"向下搜索。", L"بحث للأسفل.", L"Искать вниз.", L"Abwarts suchen.", L"Buscar abaixo.", L"Omlaag zoeken.", L"Szukaj w dol.", L"A?a?? ara."));
	m_tooltip.AddTool(&m_lsup, LL14(L"選択曲を一番上へ移動。", L"Move to top.", L"Tout en haut.", L"In cima.", L"Al principio.", L"맨 위로.", L"移到顶部。", L"إلى الأعلى.", L"В начало.", L"Ganz nach oben.", L"Para o topo.", L"Naar boven.", L"Na gore.", L"En uste."));
	m_tooltip.AddTool(&m_up, LL14(L"選択曲を上へ移動。", L"Move up.", L"Monter.", L"Su.", L"Subir.", L"위로.", L"上移。", L"لأعلى.", L"Вверх.", L"Hoch.", L"Cima.", L"Omhoog.", L"W gore.", L"Yukar?."));
	m_tooltip.AddTool(&m_down, LL14(L"選択曲を下へ移動。", L"Move down.", L"Descendre.", L"Giu.", L"Bajar.", L"아래로.", L"下移。", L"لأسفل.", L"Вниз.", L"Runter.", L"Baixo.", L"Omlaag.", L"W dol.", L"A?a??."));
	m_tooltip.AddTool(&m_lsdown, LL14(L"選択曲を一番下へ移動。", L"Move to bottom.", L"Tout en bas.", L"In fondo.", L"Al final.", L"맨 아래로.", L"移到底部。", L"إلى الأسفل.", L"В конец.", L"Ganz nach unten.", L"Para o final.", L"Naar beneden.", L"Na dol.", L"En alta."));
	m_tooltip.AddTool(&m_tip, LL14(L"行ツールチップの表示を切り替えます。", L"Toggle row tooltips.", L"Info-bulles des lignes.", L"Suggerimenti righe.", L"Sugerencias de filas.", L"행 툴팁 표시 전환.", L"切换行工具提示。", L"تبديل تلميحات الصفوف.", L"Подсказки строк.", L"Zeilen-Tooltips.", L"Dicas de linha.", L"Rij-tooltips.", L"Etykiety wierszy.", L"Sat?r ipuclar?."));
	m_tooltip.AddTool(&m_mini, LL14(L"最小化/復帰をメイン画面と連動させます。", L"Sync minimize/restore with main window.", L"Synchroniser min./rest.", L"Sincronizza min./rip.", L"Sincronizar min./rest.", L"최소화/복원 연동.", L"最小化/还原联动。", L"تزامن التصغير/الاستعادة.", L"Синхр. сверт./восст.", L"Min./Wiederh. synchron.", L"Sincronizar min./rest.", L"Min./herstel synch.", L"Synch. min./przywr.", L"Min./geri yukleme e?itle."));
	m_tooltip.AddTool(&m_savemp3, LL14(L"mp3再生時に途中保存を有効にします。", L"Enable resume save for mp3.", L"Reprise pour mp3.", L"Ripresa per mp3.", L"Reanudar para mp3.", L"mp3 위치 저장.", L"mp3续播保存。", L"حفظ موضع mp3.", L"Сохранение позиции mp3.", L"mp3-Position speichern.", L"Retomar mp3.", L"mp3 hervatten.", L"Wznawianie mp3.", L"mp3 surdurme."));
	m_tooltip.AddTool(&m_saveds, LL14(L"DirectShow(動画等)で途中保存を有効にします。", L"Enable resume save for DirectShow.", L"Reprise pour DirectShow.", L"Ripresa per DirectShow.", L"Reanudar para DirectShow.", L"DirectShow 위치 저장.", L"DirectShow续播保存。", L"حفظ موضع DirectShow.", L"Сохранение позиции DirectShow.", L"DirectShow-Position.", L"Retomar DirectShow.", L"DirectShow hervatten.", L"Wznawianie DirectShow.", L"DirectShow surdurme."));
	m_find.SetFont(&m_fontList, TRUE);

	DoLayout();
	CCC_SendGroupBoxesToBack(GetSafeHwnd());   // 区分け枠を最背面へ(兄弟コントロールを覆わない)
	ReloadPlaylistCombo();
	RefreshList(TRUE);
	SyncFromMain();
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
	return TRUE;
}

// og 所有のまま(EQ/ピアノロールと同じアクリルグループ)にして非アクティブでも
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
	if (CCC_ProcessInwomanHotkey(pMsg, this))
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
	if (m_bmpBanner.GetSafeHandle()) m_bmpBanner.DeleteObject();
	if (m_memBanner.GetSafeHdc()) m_memBanner.DeleteDC();
	return CCustomBlurDialogExBase::DestroyWindow();
}

// 1コントロールを移動するヘルパ
static void MoveCtl(CWnd* p, int x, int y, int w, int h)
{
	if (p && p->GetSafeHwnd())
		p->MoveWindow(x, y, w, h);
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
	g_mpSideJacket = showJacket ? 1 : 0;

	const int gTitle = (int)(14 * s);   // グループ枠のタイトル分の高さ
	const int gPad = (int)(5 * s);      // グループ内側の余白

	// ===== 情報グループ(歌詞5行 or 歌詞3行+OS/CPU) =====
	// 歌詞有無で中身は切替えるが、枠の高さは固定(5行分)にしてプレイリスト位置が動かないようにする。
	int infoTop = M + bannerH + (int)(2 * s);
	int ix = M + gPad, iw = W - M * 2 - gPad * 2;
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
	MoveCtl(&m_seek, M, seekY, W - M * 2 - timeW - (int)(4 * s), (int)(16 * s));
	MoveCtl(&m_time, W - M - timeW, seekY + (int)(2 * s), timeW, (int)(14 * s));

	// ===== 操作行: 前/再生/一時停止/停止/フェードアウト/次 + ジャケ/EQ/ピアノ + 主音量(右) =====
	// 主音量手前の空きへ ジャケ/EQ/ピアノ を配置し、音量スライダーは残り幅へ伸縮させる。
	int by = seekY + (int)(22 * s);
	int bh = (int)(24 * s), gap = (int)(3 * s);
	int bx = M;
	MoveCtl(&m_prev, bx, by, (int)(40 * s), bh); bx += (int)(40 * s) + gap;
	MoveCtl(&m_play, bx, by, (int)(44 * s), bh); bx += (int)(44 * s) + gap;
	MoveCtl(&m_pause, bx, by, (int)(52 * s), bh); bx += (int)(52 * s) + gap;
	MoveCtl(&m_stop, bx, by, (int)(44 * s), bh); bx += (int)(44 * s) + gap;
	MoveCtl(&m_fadeout, bx, by, (int)(72 * s), bh); bx += (int)(72 * s) + gap;
	MoveCtl(&m_next, bx, by, (int)(40 * s), bh); bx += (int)(40 * s) + (int)(8 * s);
	int jkw = (int)(48 * s), ebw = (int)(40 * s), pbw = (int)(50 * s);
	MoveCtl(&m_jacket, bx, by, jkw, bh); bx += jkw + gap;
	MoveCtl(&m_eq, bx, by, ebw, bh); bx += ebw + gap;
	MoveCtl(&m_piano, bx, by, pbw, bh); bx += pbw + (int)(8 * s);
	int volValW = (int)(30 * s), volLblW = (int)(38 * s);
	int volvalX = W - M - volValW;
	int volLblX = bx;
	int volSlX = volLblX + volLblW;
	int volSlW = volvalX - (int)(2 * s) - volSlX;
	if (volSlW < (int)(50 * s)) volSlW = (int)(50 * s);
	MoveCtl(&m_vollabel, volLblX, by + (int)(5 * s), volLblW, (int)(15 * s));
	MoveCtl(&m_vol, volSlX, by + (int)(4 * s), volSlW, (int)(16 * s));
	MoveCtl(&m_volval, volvalX, by + (int)(5 * s), volValW, (int)(15 * s));

	// ===== オプション行: 連続再生/ループ再生/ランダム再生(左) + スペアナ/ST/フォルダ(右寄せ) =====
	int by2 = by + bh + (int)(4 * s);
	int ch = (int)(22 * s);             // 行高(フォルダボタン)
	int ckH = (int)(17 * s);            // チェックボックス高(フォントが収まる)
	int ckY2 = by2 + (ch - ckH) / 2;    // 縦中央
	int cx = M;
	MoveCtl(&m_renzoku, cx, ckY2, (int)(72 * s), ckH); cx += (int)(76 * s);
	MoveCtl(&m_loop, cx, ckY2, (int)(80 * s), ckH); cx += (int)(84 * s);
	MoveCtl(&m_random, cx, ckY2, (int)(90 * s), ckH);
	// スペアナ/ST/フォルダ は右寄せ(STの後ろにフォルダを置く)
	int folW = (int)(54 * s), stW = (int)(30 * s), supeW = (int)(50 * s);
	int rcx = W - M - folW;
	MoveCtl(&m_folder, rcx, by2, folW, ch); rcx -= (int)(4 * s) + stW;
	MoveCtl(&m_st, rcx, ckY2, stW, ckH); rcx -= (int)(2 * s) + supeW;
	MoveCtl(&m_supe, rcx, ckY2, supeW, ckH);

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
	}
	int sndBottom = sy + slLabelH + slH + (int)(1 * s) + gPad;
	MoveCtl(&m_grpSnd, M, sndTop, W - M * 2, sndBottom - sndTop);

	// ===== プレイリストグループ: ツールバー + リスト + 下部チェック =====
	int plTop = sndBottom + (int)(5 * s);
	int by4 = plTop + gTitle;
	int tbH = (int)(19 * s);
	int comboW = (int)(120 * s);
	MoveCtl(&m_plsel, M + gPad, by4, comboW, tbH);
	int tx = M + gPad + comboW + (int)(5 * s);
	int tbw = (int)(50 * s);
	MoveCtl(&m_plrename, tx, by4, tbw, tbH); tx += tbw + (int)(3 * s);
	MoveCtl(&m_pldelete, tx, by4, tbw, tbH); tx += tbw + (int)(8 * s);
	int ibw = (int)(16 * s);
	int findW = (int)(86 * s);
	MoveCtl(&m_find, tx, by4 + (int)(1 * s), findW, tbH - (int)(2 * s)); tx += findW + (int)(2 * s);
	MoveCtl(&m_finddown, tx, by4, ibw, tbH); tx += ibw + (int)(1 * s);
	MoveCtl(&m_findup, tx, by4, ibw, tbH);
	int rx = W - M - gPad - (int)(50 * s);
	MoveCtl(&m_itemdel, rx, by4, (int)(50 * s), tbH); rx -= ibw + (int)(2 * s);
	MoveCtl(&m_lsdown, rx, by4, ibw, tbH); rx -= ibw + (int)(1 * s);
	MoveCtl(&m_down, rx, by4, ibw, tbH); rx -= ibw + (int)(1 * s);
	MoveCtl(&m_up, rx, by4, ibw, tbH); rx -= ibw + (int)(1 * s);
	MoveCtl(&m_lsup, rx, by4, ibw, tbH);

	int swH = (int)(22 * s);
	int chkRowH = (int)(17 * s);
	int listY = by4 + tbH + (int)(4 * s);
	const int botY = H - swH - M + (int)(2 * s);
	const int ckY = botY - (int)(8 * s) - chkRowH;
	int listH = ckY - (int)(3 * s) - listY;
	if (listH < (int)(50 * s)) listH = (int)(50 * s);
	MoveCtl(&m_list, M + gPad, listY, W - M * 2 - gPad * 2, listH);

	// アルバム/コメント列(最終列=4)をリスト右端へぴたりとフィットさせる。
	// 他列の合計を引いた残り幅を割り当て、後ろに余白(空列)を残さない。
	// 最低幅を下回る狭い窓では最低幅に固定し、横スクロールバーが出るに任せる。
	if (::IsWindow(m_list.GetSafeHwnd())) {
		CRect lcr; m_list.GetClientRect(&lcr);   // 縦スクロールバー分を除いた可視幅
		int used = 0;
		for (int ci = 0; ci < 4; ++ci) used += m_list.GetColumnWidth(ci);
		int minLast = (int)(80 * s);
		int last = lcr.Width() - used;
		if (last < minLast) last = minLast;
		if (m_list.GetColumnWidth(4) != last) m_list.SetColumnWidth(4, last);
	}

	// 下部チェック(ツールチップ/最小化連動/mp3途中保存/DShow途中保存)は横いっぱいに均等配置
	int availCk = W - (M + gPad) * 2;
	int gapCk = (int)(6 * s);
	int ckW = (availCk - gapCk * 3) / 4;
	if (ckW < (int)(60 * s)) ckW = (int)(60 * s);
	int ckx = M + gPad;
	MoveCtl(&m_tip, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_mini, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_savemp3, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_saveds, ckx, ckY, ckW, chkRowH);
	int plBottom = ckY + chkRowH + gPad;
	MoveCtl(&m_grpPl, M, plTop, W - M * 2, plBottom - plTop);

	// 最下部: 切替(左) / 終了(右)  ※ジャケは操作行へ移動済み
	MoveCtl(&m_switch, M, botY, (int)(140 * s), swH);
	int exW = (int)(80 * s);
	MoveCtl(&m_exit, W - M - exW, botY, exW, swH);

	Invalidate();
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
		case 1: _tcscpy_s(di->item.pszText, di->item.cchTextMax, d.game); break;
		case 2: {
			CString s;
			if (d.time == 0) s = _T("");
			else if (d.time == -1) s = LL14(L"取得不能", L"N/A", L"N/A", L"N/A", L"N/A", L"N/A", L"N/A", L"N/A", L"N/A", L"N/A", L"N/A", L"N/A", L"N/A", L"N/A");
			else if (d.time >= 3600) s.Format(_T("%d:%02d:%02d"), d.time / 3600, (d.time / 60) % 60, d.time % 60);
			else s.Format(_T("%d:%02d"), d.time / 60, d.time % 60);
			_tcsncpy_s(di->item.pszText, di->item.cchTextMax, s, _TRUNCATE);
		} break;
		case 3: _tcscpy_s(di->item.pszText, di->item.cchTextMax, d.art); break;
		case 4: _tcscpy_s(di->item.pszText, di->item.cchTextMax, d.alb); break;
		default: break;
		}
	}
	if (di->item.mask & LVIF_IMAGE)
		di->item.iImage = d.icon;
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
	if (anchor < 0 || anchor >= pl->playcnt) anchor = 0;
	for (int k = -1; (k = m_list.GetNextItem(k, LVNI_SELECTED)) != -1; )
		m_list.SetItemState(k, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.SetItemState(anchor, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.EnsureVisible(anchor, FALSE);
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
			CCC_SendGroupBoxesToBack(GetSafeHwnd());
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
		int dsp = (og->m_dsval.GetPos() + 499) * 2 / 10;
		l.Format(LL2(L"DS音量 %d%%", L"DS %d%%"), dsp); m_dsvolL.GetWindowText(s2); if (l != s2) m_dsvolL.SetWindowText(l);
		l.Format(LL2(L"拡張 %d", L"Boost %d"), og->m_kakuVol.GetPos()); m_kvolL.GetWindowText(s2); if (l != s2) m_kvolL.SetWindowText(l);
		l.Format(LL2(L"テンポ %d%%", L"Tempo %d%%"), og->m_tempo_sl.GetPos() / 2); m_tempoL.GetWindowText(s2); if (l != s2) m_tempoL.SetWindowText(l);
		l.Format(LL2(L"ピッチ %d%%", L"Pitch %d%%"), og->m_pitch_sl.GetPos() / 2); m_pitchL.GetWindowText(s2); if (l != s2) m_pitchL.SetWindowText(l);

		// 乱数/順次・スペアナ/ST の状態(変化時のみ。毎tick SetCheck はちらつくため)
		int v1;
		v1 = og->m_random.GetCheck() ? 1 : 0; if (m_random.GetCheck() != v1) m_random.SetCheck(v1);
		v1 = og->m_supe.GetCheck() ? 1 : 0; if (m_supe.GetCheck() != v1) m_supe.SetCheck(v1);
		v1 = og->m_st.GetCheck() ? 1 : 0; if (m_st.GetCheck() != v1) m_st.SetCheck(v1);
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
		key.Format(_T("%s\x01%s\x01%s\x01%s\x01%s\x01%d"),
			(LPCTSTR)CurrentTrackTitle(), (LPCTSTR)tagname, (LPCTSTR)tagalbum,
			(LPCTSTR)tagtrack, (LPCTSTR)fmt, og ? og->jx : -1);
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
		m_seek.SetRange(mn, mx, FALSE);
		m_seek.SetSelection(selMn, selMx);
		m_seek.SetPos(ps);
		int pct = (int)((double)(ps - mn) * 100.0 / (double)(mx - mn));
		if (pct < 0) pct = 0; if (pct > 100) pct = 100;
		CString t; t.Format(_T("%d%%"), pct);
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
	CString vs; vs.Format(_T("%d"), v); m_volval.GetWindowText(s2); if (vs != s2) m_volval.SetWindowText(vs);
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
	// リストの列幅も保存(次回起動時に復元)
	if (::IsWindow(m_list.GetSafeHwnd())) {
		for (int ci = 0; ci < 5; ++ci) {
			int w = m_list.GetColumnWidth(ci);
			if (w > 0)
				savedata.mpcol[ci] = w;
		}
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
		// 低頻度でバナーの再描画だけ促す(60fpsの常時Blitはしない=ピアノロール等の負荷源を排除)。
		if (::IsWindowVisible(GetSafeHwnd()) && !IsIconic())
			InvalidateRect(&m_bannerRect, FALSE);
	}
	else if (nIDEvent == 3) {
		// 高速: 再生位置(playb)に追従するシーク・時間・音量のミラー
		MirrorSeekVol();
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
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CMediaPlayerDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED) {
		// 最小化連動: ファルコム特化型では og 最小化で OS がオプション窓(EQ/ピアノロール)を
		// 自動で隠すが、メディアプレイヤーモードでは mp が独立窓のため自前で連動させる。
		// これらは og 所有のため、最小化前の表示状態を覚えて復帰時に戻す。
		if (m_mini.GetCheck() && og && ::IsWindow(og->GetSafeHwnd())) {
			if (::IsWindow(og->m_EqualizerDlg.GetSafeHwnd())) {
				m_savedEqVisible = ::IsWindowVisible(og->m_EqualizerDlg.m_hWnd) ? 1 : 0;
				if (m_savedEqVisible) ::ShowWindow(og->m_EqualizerDlg.m_hWnd, SW_HIDE);
			}
			if (::IsWindow(og->m_PianoRollDlg.GetSafeHwnd())) {
				m_savedPianoVisible = ::IsWindowVisible(og->m_PianoRollDlg.m_hWnd) ? 1 : 0;
				if (m_savedPianoVisible) ::ShowWindow(og->m_PianoRollDlg.m_hWnd, SW_HIDE);
			}
		}
		return;
	}
	if (::IsWindow(m_hWnd)) {
		// 最小化からの復帰: 連動で隠したオプション窓(EQ/ピアノロール)を元に戻す。
		if (nType == SIZE_RESTORED && m_mini.GetCheck() && og && ::IsWindow(og->GetSafeHwnd())) {
			if (m_savedEqVisible && ::IsWindow(og->m_EqualizerDlg.GetSafeHwnd()))
				::ShowWindow(og->m_EqualizerDlg.m_hWnd, SW_SHOW);
			if (m_savedPianoVisible && ::IsWindow(og->m_PianoRollDlg.GetSafeHwnd()))
				::ShowWindow(og->m_PianoRollDlg.m_hWnd, SW_SHOW);
			m_savedEqVisible = 0;
			m_savedPianoVisible = 0;
		}
		DoLayout();
		if (m_inSizeMove) {
			// 対話的リサイズ中(枠ドラッグ中)は同期再描画(RDW_UPDATENOW)を避け、
			// 無効化のみでペイントをコアレスさせて軽量化する。確定時(OnExitSizeMove)に
			// 一度だけ全子コントロールを同期再描画してきれいに整える。
			RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
		}
		else {
			// プログラム的なサイズ変更/最大化など: 従来どおり即時できれいに整える。
			// WS_CLIPCHILDREN 済みなので親の ERASE が子を塗り潰すことはない。
			RedrawWindow(NULL, NULL,
				RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
			if (::IsWindow(m_list.GetSafeHwnd()))
				m_list.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
		}
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
		CCC_BlitStretchChromaNoFlicker(pDC->m_hDC, m_bannerRect.left, m_bannerRect.top, dw, dh,
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
	if (mode == -10 || mode == -9 || mode == -8 || mode == -7) t = tagfile;
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
// CListCtrlA 実装のカスタムツールチップ(行詳細)を ON/OFF する。
// PlayList の m_lc.EnableToolTips/SetExtendedStyle と同じ方式。
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
}

// WM_MP_INFO_SCROLL ハンドラ。TheadLoop から ~30fps で PostMessage される。
// タイマーよりも V-Sync に近いタイミングで呼ばれるため marquee が滑らかになる。
// m_iscActive が true なら右曲情報パネルを無効化 → DrawSidePanels がスクロールを1段進めて
// 再び true にセットする(→次 tick でまた無効化)。スクロール不要なら m_iscActive は
// false のままで再描画は発生しない。
LRESULT CMediaPlayerDlg::OnInfoScrollTick(WPARAM, LPARAM)
{
	if (m_iscActive && !m_infoPanelRect.IsRectEmpty()) {
		m_iscActive = false;   // DrawSidePanels が再セット(スクロール継続中なら true に戻す)
		InvalidateRect(&m_infoPanelRect, FALSE);
	}
	return 0;
}

// 非アクティブ化でアクリル背景が落ちる対策。
// mp はタスクバー表示のためトップレベル化(オーナー解除)されており、
// EQ/ピアノロール等の og 所有ウィンドウと違い、非アクティブ時に DWM の
// アクリルバックドロップが維持されない。活性が変わるたびに backdrop 属性と
// フレーム拡張を再適用して、非アクティブでもアクリルを保つ。
BOOL CMediaPlayerDlg::OnNcActivate(BOOL bActive)
{
	BOOL r = CCustomBlurDialogExBase::OnNcActivate(bActive);
#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1 && CCC_IsWin11())
		CCC_RefreshDialogDwmBlur(m_hWnd);   // backdrop=acrylic + フレーム拡張を再適用
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
	for (int i = 0; i < kInfoRows; i++) { m_isc[i] = 0; m_iscW[i] = 0; }
	m_iscActive = false;
}

// 1行のテキストをスクロール対応で mem DC へ描画する。
//
// 収まる場合: DrawText で静止描画して false を返す(スクロール不要)。
//
// はみ出す場合: 「テキスト + セパレータ」を2回並べたワイド DC を作り、
// m_isc[rowIdx] をオフセットとして可視幅(tw)分だけ切り出して BitBlt する。
// オフセットは 2px/呼び出し 進むため ~30fps で呼べば ~60px/sec になる。
// セパレータ部には左右ドット + 中央ダイヤの GDI 装飾を描く(視覚的な区切り)。
//
// rowIdx: m_isc/m_iscW のインデックス(0=タイトル行, 1〜5=サブ行)
bool CMediaPlayerDlg::DrawInfoScrollRow(CDC& mem, int tx, int y, int tw, int lineH,
	const CString& text, COLORREF clr, int rowIdx, COLORREF kBg, CFont* font)
{
	if (text.IsEmpty() || tw <= 0 || lineH <= 0) return false;

	CFont* oldFont = mem.SelectObject(font);
	CSize szText = mem.GetTextExtent(text);
	mem.SelectObject(oldFont);

	if (szText.cx <= tw) {
		// テキストが収まる場合: 通常描画、カウンタリセット
		m_isc[rowIdx]  = 0;
		m_iscW[rowIdx] = 0;
		mem.SelectObject(font);
		mem.SetTextColor(clr);
		CRect rr(tx, y, tx + tw, y + lineH);
		mem.DrawText(text, &rr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
		mem.SelectObject(oldFont);
		return false;
	}

	// テキストが収まらない場合: セパレータ付き marquee スクロール
	const CString kSep = _T("　　 ");   // 全角2+半角1スペース（自然な間隔）
	CString scrollText = text + kSep;
	mem.SelectObject(font);
	CSize szFull = mem.GetTextExtent(scrollText);
	mem.SelectObject(oldFont);

	if (szFull.cx <= 0) return false;
	m_iscW[rowIdx] = szFull.cx;

	// スクロール用ワイド一時 DC を作成（テキスト2連続 = シームレスループ）
	int wideW = szFull.cx * 2 + 4;
	CDC wdc; wdc.CreateCompatibleDC(&mem);
	CBitmap wbm; wbm.CreateCompatibleBitmap(&mem, wideW, lineH);
	CBitmap* ob = wdc.SelectObject(&wbm);
	wdc.FillSolidRect(0, 0, wideW, lineH, kBg);
	wdc.SetBkMode(TRANSPARENT);
	wdc.SetTextColor(clr);

	// テキストを2回描画（シームレスループ用）
	CFont* wf = wdc.SelectObject(font);
	CRect wr1(0, 0, szFull.cx + 4, lineH);
	wdc.DrawText(scrollText, &wr1, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
	CRect wr2(szFull.cx, 0, szFull.cx * 2 + 4, lineH);
	wdc.DrawText(scrollText, &wr2, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

	// セパレータ部に GDI 装飾を描画（左右ドット + 細線）
	CSize szTextOnly = wdc.GetTextExtent(text);
	int sx = szTextOnly.cx;           // セパレータ開始X
	int sw = szFull.cx - szTextOnly.cx; // セパレータ幅
	if (sw > 8) {
		int cy = lineH / 2;
		int dr = max(2, lineH / 10);
		CPen nullPen(PS_NULL, 0, RGB(0, 0, 0));
		CBrush brDeco(clr);
		CPen* opDeco  = wdc.SelectObject(&nullPen);
		CBrush* obDeco = wdc.SelectObject(&brDeco);

		// 左ドット
		int lx = sx + sw / 3;
		wdc.Ellipse(lx - dr, cy - dr, lx + dr, cy + dr);
		// 右ドット
		int rx = sx + sw * 2 / 3;
		wdc.Ellipse(rx - dr, cy - dr, rx + dr, cy + dr);
		// 中央小ダイヤ
		int mx = sx + sw / 2;
		int mr = max(2, lineH / 8);
		POINT diaPts[4] = { {mx, cy - mr}, {mx + mr, cy}, {mx, cy + mr}, {mx - mr, cy} };
		wdc.Polygon(diaPts, 4);

		// 細線（左端〜左ドット, 右ドット〜右端）
		CPen linePen(PS_SOLID, 1, clr);
		wdc.SelectObject(&linePen);
		wdc.SelectStockObject(NULL_BRUSH);
		wdc.MoveTo(sx + 2,  cy); wdc.LineTo(lx - dr - 1, cy);
		wdc.MoveTo(rx + dr + 1, cy); wdc.LineTo(sx + sw - 2, cy);

		// 2コピー目にも同じ装飾
		int sx2 = sx + szFull.cx;
		int lx2 = sx2 + sw / 3, rx2 = sx2 + sw * 2 / 3, mx2 = sx2 + sw / 2;
		wdc.SelectObject(&nullPen);
		wdc.SelectObject(&brDeco);
		wdc.Ellipse(lx2 - dr, cy - dr, lx2 + dr, cy + dr);
		wdc.Ellipse(rx2 - dr, cy - dr, rx2 + dr, cy + dr);
		POINT diaPts2[4] = { {mx2, cy - mr}, {mx2 + mr, cy}, {mx2, cy + mr}, {mx2 - mr, cy} };
		wdc.Polygon(diaPts2, 4);
		wdc.SelectObject(&linePen);
		wdc.SelectStockObject(NULL_BRUSH);
		wdc.MoveTo(sx2 + 2,     cy); wdc.LineTo(lx2 - dr - 1, cy);
		wdc.MoveTo(rx2 + dr + 1, cy); wdc.LineTo(sx2 + sw - 2, cy);

		wdc.SelectObject(obDeco);
		wdc.SelectObject(opDeco);
	}
	wdc.SelectObject(wf);

	// 現在のオフセット位置から tw 幅分だけ切り出して Blit する。
	// ワイド DC には「A〜Z + sep + A〜Z + sep」と2周分描画してあるため、
	// off が szFull.cx を超えてもシームレスにラップアラウンドする。
	int off = m_isc[rowIdx] % szFull.cx;
	if (off < 0) off = 0;

	int saved = mem.SaveDC();
	mem.IntersectClipRect(tx, y, tx + tw, y + lineH);
	// 1コピー目: left = tx - off (off=0 のとき先頭と一致)
	mem.BitBlt(tx - off, y, szFull.cx, lineH, &wdc, 0, 0, SRCCOPY);
	// 2コピー目: 1コピー目が左へずれた分の右端を埋めるラップアラウンド
	mem.BitBlt(tx + szFull.cx - off, y, tw, lineH, &wdc, szFull.cx, 0, SRCCOPY);
	mem.RestoreDC(saved);

	wdc.SelectObject(ob);

	// スクロールカウンタを進める（2px/呼び出し ≈ 60px/sec @30fps）
	m_isc[rowIdx] += 2;
	if (m_isc[rowIdx] >= szFull.cx) m_isc[rowIdx] -= szFull.cx;

	return true;
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
			else if (m_hIcon) {                     // ジャケ無し: アイコンを中央に(空白回避)
				int isz = (w < h ? w : h) * 11 / 20;
				::DrawIconEx(mem.GetSafeHdc(), (w - isz) / 2, (h - isz) / 2, m_hIcon, isz, isz, 0, NULL, DI_NORMAL);
			}
			if (aero)
				CCC_BlitStretchChromaNoFlicker(pDC->m_hDC, m_jacketRect.left, m_jacketRect.top, w, h, mem.GetSafeHdc(), 0, 0, w, h, kBg);
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
			CDC mem; mem.CreateCompatibleDC(pDC);
			CBitmap bm; bm.CreateCompatibleBitmap(pDC, w, h);
			CBitmap* ob = mem.SelectObject(&bm);
			mem.FillSolidRect(0, 0, w, h, kBg);
			mem.SetBkMode(TRANSPARENT);

			const int pad = (int)(8 * hD2);
			int tx = pad, tw = w - pad * 2;
			if (tw < 1) tw = 1;

			// ---- 情報収集 ----
			CString title = CurrentTrackTitle();
			CString artist = tagname, album = tagalbum;
			CString track = tagtrack;
			CString fmt; if (::IsWindow(m_os.GetSafeHwnd())) m_os.GetWindowText(fmt);

			// 曲番号行
			CString trackLine;
			if (!track.IsEmpty())
				trackLine.Format(LL2(L"曲番号 %s", L"Track %s"), (LPCTSTR)track);

			// Hz / チャンネル / ビット深度行
			CString audioLine;
			if (wavbit_sample_Hz > 0 && wavchannel > 0) {
				CString chStr;
				switch (wavchannel) {
				case 1: chStr = L"mono";   break;
				case 2: chStr = L"stereo"; break;
				case 3: chStr = L"3ch";    break;
				case 4: chStr = L"4ch";    break;
				case 5: chStr = L"4.1ch";  break;
				case 6: chStr = L"5.1ch";  break;
				case 7: chStr = L"6.1ch";  break;
				case 8: chStr = L"7.1ch";  break;
				default: chStr.Format(L"%dch", wavchannel); break;
				}
				int bitsDisp = abs(wavsam_depth);
				if (bitsDisp > 0)
					audioLine.Format(L"%d Hz  %s  %d bit", wavbit_sample_Hz, (LPCTSTR)chStr, bitsDisp);
				else
					audioLine.Format(L"%d Hz  %s", wavbit_sample_Hz, (LPCTSTR)chStr);
			}

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
				CCC_BlitStretchChromaNoFlicker(pDC->m_hDC, m_infoPanelRect.left, m_infoPanelRect.top, w, h, mem.GetSafeHdc(), 0, 0, w, h, kBg);
			else
				pDC->BitBlt(m_infoPanelRect.left, m_infoPanelRect.top, w, h, &mem, 0, 0, SRCCOPY);
			mem.SelectObject(ob);
		}
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
	// CCC_PaintDialogAeroGaps は SelectClipRgn を書き換えるため SaveDC/RestoreDC で挟む。
	// preserve=&m_bannerRect でバナーを保護(毎フレームのクリアを防いで点滅なし)。
	// グループボックスは CCC_SelectClipExcludeChildren で除外されて白くなるため、
	// RestoreDC 後に個別で CCC_ClearRectChroma を呼んで明示的にクロマクリアする。
	if (savedata.aero == 1 && CCC_IsWin11()) {
		CPaintDC dc(this);
		// 更新領域がバナーに重なる時だけバナーを Blit する。
		// info パネルのスクロール tick(30fps)では clip=infoPanelRect となり、
		// アクリル時に重い BlitVisualizer(毎回 DIB 生成+バッファペイント)を回避する。
		CRect clipBox; dc.GetClipBox(&clipBox);
		bool hitBanner = !m_bannerRect.IsRectEmpty() && CRect().IntersectRect(&clipBox, &m_bannerRect);
		{
			int saved = dc.SaveDC();
			CCC_PaintDialogAeroGaps(dc, this, &m_bannerRect);
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
	// プレイリストへ追加(両リストビューへ反映するため pl 経由)。
	// メディアプレイヤーモードでは pl は裏で生きているので pl->OnDropFiles を呼ぶ。
	if (pl)
		pl->OnDropFiles(hDropInfo);
	RefreshList(TRUE);
	CCustomBlurDialogExBase::OnDropFiles(hDropInfo);
}

void CMediaPlayerDlg::OnDestroy()
{
	SavePos();
	KillTimer(1);
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
		CString vs; vs.Format(_T("%d"), v); m_volval.SetWindowText(vs);
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
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_APP + 2, 0, 0);  // 再生(再演奏)
}

void CMediaPlayerDlg::OnPrev()
{
	if (!pl || pl->playcnt <= 0) return;
	int idx = plcnt - 1;
	if (idx < 0) idx = pl->playcnt - 1;
	MP_PlayIndex(idx);
}

void CMediaPlayerDlg::OnNext()
{
	if (!pl || pl->playcnt <= 0) return;
	int idx = plcnt + 1;
	if (idx >= pl->playcnt) idx = 0;
	MP_PlayIndex(idx);
}

void CMediaPlayerDlg::OnPlay()
{
	int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
	if (sel >= 0 && pl && sel < pl->playcnt)
		MP_PlayIndex(sel);
	else if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_APP + 2, 0, 0);
}

void CMediaPlayerDlg::OnPauseBtn()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->OnPause();
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
}

void CMediaPlayerDlg::OnPiano()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->TogglePianoRoll();
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
	m_lastComboCount = n;
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

void CMediaPlayerDlg::OnPlSel()
{
	if (!pl || !::IsWindow(pl->m_listchange.GetSafeHwnd())) return;
	int sel = m_plsel.GetCurSel();
	if (sel < 0) return;
	pl->m_listchange.SetCurSel(sel);
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
	if (og && ::IsWindow(og->m_supe.GetSafeHwnd()))
		og->m_supe.SetCheck(m_supe.GetCheck() ? 1 : 0);   // timerp がライブ参照
}

void CMediaPlayerDlg::OnSt()
{
	if (og && ::IsWindow(og->m_st.GetSafeHwnd()))
		og->m_st.SetCheck(m_st.GetCheck() ? 1 : 0);
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

// リスト右クリック: 詳細編集 / WAV保存 / 削除(pl の処理を流用)
void CMediaPlayerDlg::OnRclickList(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	if (!pl) return;
	SyncSelectionToPlaylist();
	CPoint pt; ::GetCursorPos(&pt);
	CMenu menu; menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, 1, LL14(L"詳細編集", L"Edit details", L"Editer details", L"Modifica dettagli", L"Editar detalles", L"상세 편집", L"详细编辑", L"تحرير التفاصيل", L"Изменить детали", L"Details bearbeiten", L"Editar detalhes", L"Details bewerken", L"Edytuj szczego?y", L"Detaylar? duzenle"));
	menu.AppendMenu(MF_STRING, 2, LL14(L"WAVに保存", L"Save as WAV", L"Enregistrer en WAV", L"Salva come WAV", L"Guardar como WAV", L"WAV로 저장", L"保存为WAV", L"حفظ كـ WAV", L"Сохранить как WAV", L"Als WAV speichern", L"Salvar como WAV", L"Opslaan als WAV", L"Zapisz jako WAV", L"WAV olarak kaydet"));
	menu.AppendMenu(MF_SEPARATOR, 0, (LPCTSTR)NULL);
	menu.AppendMenu(MF_STRING, 3, LL14(L"削除", L"Delete", L"Supprimer", L"Elimina", L"Eliminar", L"삭제", L"删除", L"حذف", L"Удалить", L"Loschen", L"Excluir", L"Verwijderen", L"Usu?", L"Sil"));
	int cmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD, pt.x, pt.y, this);
	if (cmd == 1) pl->OnList();
	else if (cmd == 2) pl->OnPopWavExport();
	else if (cmd == 3) { pl->Del(); RefreshList(TRUE); }
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
	if (g_mpSideJacket && !m_jacketRect.IsRectEmpty() && m_jacketRect.PtInRect(point)) {
		OnJacket();
		return;
	}
	if (!g_mpSideJacket && !m_bannerRect.IsRectEmpty() && m_bannerRect.PtInRect(point)) {
		OnJacket();
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
		const bool overJacket =
			(g_mpSideJacket && !m_jacketRect.IsRectEmpty() && m_jacketRect.PtInRect(point)) ||
			(!g_mpSideJacket && !m_bannerRect.IsRectEmpty() && m_bannerRect.PtInRect(point));
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
		pl->Create(og);
	}
	plw = 1;
	savedata.playerMode = 1;

	// メディアプレイヤー画面を生成・表示。
	// オーナーは og のまま(EQ/ピアノロールと同じアクリルグループ)にして、
	// 非アクティブ時もアクリルが維持されるようにする。トップレベル化(オーナー解除)は
	// 孤立窓となり非アクティブでアクリルが落ちるため行わない。タスクバー単独表示は
	// PreCreateWindow の WS_EX_APPWINDOW で確保する。
	mp = new CMediaPlayerDlg;
	mp->Create(og);
	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
#if CCUSTOM_AERO_SUPPORT
		if (savedata.aero == 1)
			mp->RefreshAeroMode();
#endif
	}

	// 重複防止: プレイリスト/メイン画面/aeroオーバーレイの単独ウィンドウを隠す
	extern CImageBase* maini;
	extern CImageBase* playbase;
	if (pl && ::IsWindow(pl->GetSafeHwnd()))
		::ShowWindow(pl->m_hWnd, SW_HIDE);
	if (maini && ::IsWindow(maini->GetSafeHwnd()))
		::ShowWindow(maini->m_hWnd, SW_HIDE);
	if (playbase && ::IsWindow(playbase->GetSafeHwnd()))
		::ShowWindow(playbase->m_hWnd, SW_HIDE);
	// イコライザー/ピアノロールはオプション窓なので閉じない(そのまま維持)
	::ShowWindow(og->m_hWnd, SW_HIDE);

	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
		::SetForegroundWindow(mp->m_hWnd);
		mp->SetFocus();
#if CCUSTOM_AERO_SUPPORT
		if (savedata.aero == 1)
			mp->RefreshAeroMode();   // 前面化後に再適用
#endif
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

	// メイン画面を表示し、aero/全コントロールを確実に再反映(▲▼の開閉状態は保持=Resizeは呼ばない)
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		::ShowWindow(og->m_hWnd, SW_SHOW);
		::SetForegroundWindow(og->m_hWnd);
		CCC_SendGroupBoxesToBack(og->m_hWnd);   // 枠を最背面へ(チェックボックスを覆わない)
#if CCUSTOM_AERO_SUPPORT
		og->RefreshAeroMode();                   // アクリル/非アクリルを再適用
#endif
		CCC_RefreshChildrenAfterShow(og->m_hWnd);   // 再表示時の子コントロール再描画
		og->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
		og->PostRefreshAllAeroWindows();         // EQ/ピアノ/プレイリスト等も再反映
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
