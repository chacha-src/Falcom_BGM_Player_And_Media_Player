// PlayList.cpp : 実装ファイル
//

#include "stdafx.h"
#include "direct.h"
#include "dshow.h"
#include "ogg.h"
#include "oggDlg.h"
#include "ListCtrlA.h"
#include "PlayList.h"
#include "ListSyosai.h"
#include "Filename.h"
#include "Douga.h"
#include "mp3image.h"
#include "CImageBase.h"
#include "CPlayListNew.h"

// CPlayList ダイアログ

IMPLEMENT_DYNAMIC(CPlayList, CCustomDialog)

extern 	CString ext[150][300];
extern 	CString kpif[400];
extern  BOOL kpichk[200];
extern 	int kpicnt;
extern COggDlg *og;
extern BOOL plw;

extern BYTE kvar[150][300];
extern BYTE kvver;

CPlayList::CPlayList(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CPlayList::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDI_PL);
	pc=NULL;
	plw=0;
//	pc = new playlistdata0[60000];
}

CPlayList::~CPlayList()
{
	if (pc) {
		free(pc);
		pc = NULL;
	}
	m_tooltip.DestroyWindow();
}

void CPlayList::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_BUTTON1, m_lsup);
	DDX_Control(pDX, IDC_BUTTON5, m_lup);
	DDX_Control(pDX, IDC_BUTTON10, m_lsdown);
	DDX_Control(pDX, IDC_BUTTON11, m_ldown);
	DDX_Control(pDX, IDC_LIST1, m_lc);
	DDX_Control(pDX, IDC_EDIT1, m_e);
	DDX_Control(pDX, IDC_CHECK1, m_renzoku);
	DDX_Control(pDX, IDC_CHECK4, m_loop);
	DDX_Control(pDX, IDC_CHECK28, m_tool);
	DDX_Control(pDX, IDC_CHECK29, m_saisyo);
	DDX_Control(pDX, IDC_EDIT2, m_find);
	DDX_Control(pDX, IDC_BUTTON16, m_findup);
	DDX_Control(pDX, IDC_BUTTON20, m_finddown);
	DDX_Control(pDX, IDC_CHECK5, m_savecheck);
	DDX_Control(pDX, IDC_CHECK6, m_save_mp3);
	DDX_Control(pDX, IDC_CHECK7, m_save_kpi);
	DDX_Control(pDX, IDC_COMBO1, m_listchange);
	DDX_Control(pDX, IDC_BUTTON3, m_namechage);
	DDX_Control(pDX, IDC_PLAYDELETE, m_listdelete);
}


BEGIN_MESSAGE_MAP(CPlayList, CCustomDialog)
	ON_WM_NCDESTROY()
	ON_WM_CREATE()
	ON_WM_CLOSE()
	ON_BN_CLICKED(IDOK, &CPlayList::OnBnClickedOk)
	ON_BN_CLICKED(IDC_BUTTON1, &CPlayList::OnUP)
	ON_BN_CLICKED(IDC_BUTTON5, &CPlayList::OnSUP)
	ON_BN_CLICKED(IDC_BUTTON10, &CPlayList::OnSDOWN)
	ON_BN_CLICKED(IDC_BUTTON11, &CPlayList::OnDOWN)
	ON_NOTIFY(LVN_KEYDOWN, IDC_LIST1, &CPlayList::OnLvnKeydownList1)
	ON_WM_DROPFILES()
	ON_NOTIFY(NM_DBLCLK, IDC_LIST1, &CPlayList::OnNMDblclkList1)
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_KEYDOWN()
	ON_BN_CLICKED(IDC_CHECK4, &CPlayList::OnBnClickedCheck4)
	ON_BN_CLICKED(IDC_CHECK1, &CPlayList::OnBnClickedCheck1)
	ON_NOTIFY(LVN_BEGINDRAG, IDC_LIST1, &CPlayList::OnLvnBegindragList1)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST1, &CPlayList::OnLvnGetdispinfoList1)
	ON_NOTIFY(NM_RCLICK, IDC_LIST1, &CPlayList::OnNMRclickList1)
	ON_COMMAND(ID_POP_32776, OnList)
	ON_COMMAND(ID_POP_32777,Del)
	ON_WM_ACTIVATE()
	ON_COMMAND(ID_POP_32787, &CPlayList::OnPop32787)
	ON_BN_CLICKED(IDC_BUTTON16, &CPlayList::OnFindUp)
	ON_BN_CLICKED(IDC_BUTTON20, &CPlayList::OnFindDown)
	ON_BN_CLICKED(IDC_CHECK6, &CPlayList::OnBnClickedCheck6mp3)
	ON_BN_CLICKED(IDC_CHECK7, &CPlayList::OnBnClickedCheck7dshow)
	ON_WM_CTLCOLOR()
	ON_WM_SHOWWINDOW()
	ON_WM_MOVING()
	ON_WM_SIZING()
	ON_WM_SETFOCUS()
	ON_WM_NCACTIVATE()
	ON_CBN_SELCHANGE(IDC_COMBO1, &CPlayList::OnCbnSelchangeCombo1)
	ON_BN_CLICKED(IDC_BUTTON3, &CPlayList::OnBnClickedButton3)
	ON_BN_CLICKED(IDC_PLAYDELETE, &CPlayList::OnBnClickedPlaydelete)
END_MESSAGE_MAP()

#include <eh.h>
class SE_Exception1
{
private:
    unsigned int nSE;
public:
    SE_Exception1() {}
    SE_Exception1( unsigned int n ) : nSE( n ) {}
    ~SE_Exception1() {}
    unsigned int getSeNumber() { return nSE; }
};
void trans_func1( unsigned int, EXCEPTION_POINTERS* );
void trans_func1( unsigned int u, EXCEPTION_POINTERS* pExp )
{
    throw SE_Exception1();
}


int playcnt=0;
float hD2;
int syo;
int syomode;
CString syos;
extern TCHAR karento2[1024];
extern int fade1;
extern IMediaPosition *pMediaPosition;
extern int mode,videoonly,playf;
extern int plcnt;
extern save savedata;
extern CPlayList* pl;
CImageBase* playbase;
int ogpl = 0;

BOOL CPlayList::OnInitDialog()
{
	CCustomDialog::OnInitDialog();

	CDC *desktopDc = GetDC();
	// Get native resolution
	int horizontalDPI = GetDeviceCaps(desktopDc->m_hDC, LOGPIXELSX);
	hD2 = (float)(horizontalDPI) / (96.0f);
	ReleaseDC(desktopDc);

	playcnt=0;
	w_flg=TRUE;
	pnt=0;
	SetIcon(m_hIcon, TRUE);			// 大きいアイコンを設定
	SetIcon(m_hIcon, FALSE);		// 小さいアイコンを設定
	SetWindowText(LL2(L"プレイリスト", L"Playlist"));
	SetDlgItemText(IDC_CHECK1, LL2(L"連続再生", L"Continuous play"));
	SetDlgItemText(IDC_CHECK4, LL2(L"ループ再生", L"Loop play"));
	SetDlgItemText(IDC_CHECK28, LL2(L"ツールチップ表示", L"Show tooltips"));
	SetDlgItemText(IDC_CHECK29, LL2(L"最小化、復帰", L"Minimize, restore"));
	SetDlgItemText(IDC_CHECK5, LL2(L"再生位置\nを保存", L"Save\nplayback position"));
	SetDlgItemText(IDC_STATICido, LL2(L"ファイル移動", L"File move"));
	SetDlgItemText(IDC_STATICken, LL2(L"あいまい検索", L"Fuzzy search"));
	SetDlgItemText(IDC_BUTTON3, LL2(L"名前変更", L"Rename"));
	SetDlgItemText(IDC_PLAYDELETE, LL2(L"リスト削除", L"Delete list"));
	m_lsup.SetIcon(IDR_SUP);
	m_lsup.SetFlat(TRUE);
	m_lup.SetIcon(IDR_UP);
	m_lup.SetFlat(TRUE);
	m_lsdown.SetIcon(IDR_SDOWN);
	m_lsdown.SetFlat(TRUE);
	m_ldown.SetIcon(IDR_DOWN);
	m_ldown.SetFlat(TRUE);

	m_findup.SetIcon(IDR_DOWN);
	m_findup.SetFlat(TRUE);
	m_finddown.SetIcon(IDR_UP);
	m_finddown.SetFlat(TRUE);

	m_tooltip.Create(this,TTS_ALWAYSTIP | TTS_BALLOON);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(GetDlgItem(IDOK), LL2(L"プレイリストを閉じます。", L"Close the playlist."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON1), LL2(L"選択項目を一番上に持って行きます。", L"Move selected item to the top."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON5), LL2(L"選択項目を上に持って行きます。", L"Move selected item up."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON10), LL2(L"選択項目を一番下に持って行きます。", L"Move selected item to the bottom."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON11), LL2(L"選択項目を下に持って行きます。", L"Move selected item down."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON16), LL2(L"現在の位置から下に検索します。", L"Search downward from current position."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON20), LL2(L"現在の位置から上に検索します。", L"Search upward from current position."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK1), LL2(L"プレイリストの順番に再生を行います。\n再生中にファイルドロップして追加しても演奏中の曲はそのまま鳴り続けます。", L"Playback in playlist order.\nEven if files are added during playback, the currently playing track continues."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK4), LL2(L"選択した曲をループさせます。\n再生する前にチェックを入れる必要があります。\nそうでないとループはかかりません。\nループポイントが0のもの(mp3やループしない曲)が対象です。", L"Loop selected track.\nCheck before playback to enable looping.\nOtherwise, looping will not work.\nApplies to tracks with loop point 0 (mp3 or non-looping tracks)."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK28), LL2(L"ツールチップを表示します。", L"Show tooltips."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK29), LL2(L"最小化、最小化からの復帰時、メイン画面とプレイリスト画面も同時に最小化、最小化からの復帰を行います。", L"When minimizing/restoring, main window and playlist window minimize/restore together."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK5), LL2(L"途中で演奏を停止した位置を自動保存します。\nmp3系と動画(avi,mp4など)のみ対応。\n停止ボタンもしくは終了したときのみ保存します。\n再生中に違う曲を選んだ時は位置は保存しません。", L"Auto-save playback position when stopped.\nSupports mp3 and video (avi, mp4, etc.) only.\nSaves only when stop button is pressed or when exiting.\nPosition is not saved when selecting a different track during playback."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK6), LL2(L"mp3再生時に途中保存を有効にします。", L"Enable mid-playback save for mp3."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK7), LL2(L"動画などのDirectShow使用時に途中保存を有効にします。", L"Enable mid-playback save for DirectShow (videos, etc.)."));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO1), LL2(L"プレイリストを変更または追加します。", L"Change or add playlists."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON3), LL2(L"プレイリスト名を変更します。", L"Rename playlist."));
	m_tooltip.AddTool(GetDlgItem(IDC_PLAYDELETE), LL2(L"表示されているプレイリストを削除します。\n※削除したものは復活できないので注意ください。", L"Delete the displayed playlist.\n*Deleted playlists cannot be recovered."));
	m_tooltip.SetDelayTime(TTDT_AUTOPOP, 10000);
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);
//	m_lc.SetMaxTipWidth(500)
	DWORD dwExStyle = m_lc.GetExtendedStyle();
	dwExStyle |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES;//|LVS_EX_INFOTIP;
	m_lc.SetExtendedStyle(dwExStyle);
	il.Create(16, 16, ILC_COLOR, 0, 1);
	il.Add(::AfxGetApp()->LoadIcon(IDI_ICON1)); 
	il.Add(::AfxGetApp()->LoadIcon(IDI_ICON2)); 
	il.Add(::AfxGetApp()->LoadIcon(IDI_ICON3)); 
	m_lc.SetImageList(&il,LVSIL_SMALL);
	m_lc.ModifyStyle ( 0, LVS_REPORT );
	m_lc.InsertColumn ( 0, LL2(L"名前", L"Name"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 1, LL2(L"ゲーム", L"Game"), LVCFMT_LEFT, 50, 0 );
	m_lc.InsertColumn ( 2, LL2(L"時間", L"Time"), LVCFMT_RIGHT, 50, 0 );
	m_lc.InsertColumn ( 3, LL2(L"アーティスト", L"Artist"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 4, LL2(L"アルバム/コメント", L"Album/Comment"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 5, LL2(L"フォルダ", L"Folder"), LVCFMT_LEFT, 50, 0 );
	m_lc.pc = pc;
//	pc=NULL;
//	pc = (playlistdata0*)malloc(sizeof(playlistdata0)*50000);
//	if(pc==NULL)
//		EndDialog(0);
	m_lc.SetFocus();
	pnt=pnt1=-1;
	nnn=1;
	pc=NULL;

	m_savecheck.SetCheck(savedata.savecheck);
	m_save_mp3.SetCheck(savedata.savecheck_mp3);
	m_save_kpi.SetCheck(savedata.savecheck_dshow);

	loadplaylistname();

	Load();
	if(pc==NULL){
		pc = (playlistdata0*)malloc(sizeof(playlistdata0));
	}
	SetTimer(20,20,NULL);
	SetTimer(3000,1200,NULL);
	SetTimer(40,500,NULL);
	SetTimer(5000,100,NULL);
	SIcon(pnt1);

	CCustomControlUtility::SetControlBackgroundColor(&m_listchange, COLOR_COMBO_BG);

//	CFont pFont;
//	BOOL retfont=pFont.CreateFont(-15,0,0,0,400,0,0,0,128,3,2,1,50,savedata.font2);
//	if(retfont){
//		m_lc.SetFont(&pFont,TRUE);
//		m_find.SetFont(&pFont,TRUE);
//	}
//	pFont.DeleteObject();
//	if(retfont==0)
//		retfont=pFont.CreateFont(0,0,0,0,FW_NORMAL,FALSE,FALSE,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH | FF_SWISS,_T("MS UI Gothic"));
//	if(retfont==0)
//		retfont=pFont.CreateFont(0,0,0,0,FW_NORMAL,FALSE,FALSE,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH | FF_SWISS,_T("ＭＳ Ｐゴシック"));
//	if(retfont){
//		m_lc.SetFont(&pFont,TRUE);
//		m_find.SetFont(&pFont,TRUE);
//	}
	Invalidate();
	playbase = NULL;
	if (savedata.aero) {
		playbase = new CImageBase;
		playbase->Create(pl);
		playbase->oya = pl;
	}
	CRect r;
	GetWindowRect(&r);
	if(playbase)
		playbase->MoveWindow(&r);

	plw = 1;
	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}
extern int killw1;

void CPlayList::OnNcDestroy()
{
	CCustomDialog::OnNcDestroy();

	// TODO: ここにメッセージ ハンドラ コードを追加します。
	killw1=1;
}

BOOL CPlayList::DestroyWindow()
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
	Save();
//	free(pc);
//	pc=NULL;
//	KillTimer(20);
//	KillTimer(30);
	BOOL rr=CCustomDialog::DestroyWindow();
	pl=NULL;
//	if(nnn)
//		delete this;
	plw=0;
	if(playbase) delete playbase;
	playbase = NULL;
	return rr;
}

int CPlayList::Create(CWnd *pWnd)
{
	 m_pParent = NULL;
	BOOL bret = CCustomDialog::Create( CPlayList::IDD, this);
	if (savedata.aero == 1) {
		ModifyStyleEx(0, WS_EX_LAYERED);

		// レイヤードウィンドウの不透明度と透明のカラーキー
		SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY);

		// 赤色のブラシを作成する．
		m_brDlg.CreateSolidBrush(RGB(255, 0, 0));
	}
	if (bret == TRUE)
		ShowWindow(SW_SHOW);
	return bret;
}

void CPlayList::OnClose()
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	nnn=0;
	DestroyWindow();

	CCustomDialog::OnClose();
}

void CPlayList::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
//	DestroyWindow();
}

BOOL CPlayList::PreTranslateMessage(MSG* pMsg)
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
		m_tooltip.RelayEvent(pMsg);

	return CCustomDialog::PreTranslateMessage(pMsg);
}

int pnt1=-1;

int CPlayList::chk(CString name,int sub,CString art,CString fol,int ret)
{
	int i=m_lc.GetItemCount(),c=0;
	pnt1=-1;
	CString s,s1;
	for(int j=0;j<i;j++){
		c=0;
		if ((pc[j].sub == -10) || (pc[j].sub == -2) || (pc[j].sub == -3 || pc[j].sub == 30)) {
			if (_tcscmp(pc[j].fol, fol) == 0 && pc[j].sub == sub && _tcscmp(pc[j].name, name) == 0)
				return j;
		}else{
			if(_tcscmp(pc[j].fol,fol)==0 && pc[j].sub==sub && (pc[j].ret2==ret))
				return j;
		}
	}
	return -1;
}

CString CPlayList::UTF8toSJIS(const char* a)
{
	WCHAR f[1024];
	char ff[1024];
	int rr=MultiByteToWideChar(CP_UTF8,0,a,-1,f,1024);
	int rr2=WideCharToMultiByte(CP_ACP,0,f,rr,ff,0,NULL,NULL);
	WideCharToMultiByte(CP_ACP,0,f,rr,ff,rr2,NULL,NULL);
	CString s; s=f;
	return s;
//	return _T("");
}

CString CPlayList::UTF8toUNI(const TCHAR* a)
{
//	WCHAR f[1024];
//	char ff[1024];
//	int rr2=WideCharToMultiByte(CP_ACP, 0, a,1024,ff,1024,NULL,NULL);
//	int rr= MultiByteToWideChar(CP_UTF8,0,ff,-1,f ,1024);
//	WideCharToMultiByte(CP_ACP,0,f,rr,ff,rr2,NULL,NULL);
	CString s; s=a;
	return s;
//	return _T("");
}

int CPlayList::Add(CString name,int sub,int loop1,int loop2,CString art,CString alb,CString fol,int ret,int time,BOOL f,BOOL ff)
{
	int cnt1;
	CString s,ss;
	switch(sub){
		case 1:s=(savedata.lang == 0 ? "空の軌跡SC" : "Sora no Kiseki SC");break;
		case 2:s=(savedata.lang == 0 ? "空の軌跡FC" : "Sora no Kiseki FC");break;
		case 3:s=(savedata.lang == 0 ? "イース フェルガナの誓い" : "Ys Felghana no Chikai");break;
		case 4:s=(savedata.lang == 0 ? "Ys6 ナピシュテムの匣" : "Ys6 Napishtim no Hako");break;
		case 5:s=(savedata.lang == 0 ? "イース オリジン" : "Ys Origin");break;
		case 6:s=(savedata.lang == 0 ? "空の軌跡 The3rd" : "Sora no Kiseki The3rd");break;
		case 7:s="ZWEI II";break;
		case 8:s="Ys I&II Chronicles 1";break;
		case 9:s="Ys I&II Chronicles 2";break;
		case 10:s="XANADU NEXT";break;
		case 11:s=savedata.lang ? "Ys I&II Complete 1" : "Ys I&II 完全版 1";break;
		case 12:s=savedata.lang ? "Ys I&II Complete 2" : "Ys I&II 完全版 2";break;
		case 13:s="Sorcerian Original";break;
		case 14:s="Zwei!!";break;
		case 15:s=savedata.lang ? "Gurumin -GURUMIN-" : "ぐるみん -GURUMIN-";break;
		case 16:s=(savedata.lang == 0 ? "ダイナソア リザレクション" : "Dinosaur Resurrection");break;
		case 17:s=(savedata.lang == 0 ? "Brandish4 眠れる神の塔" : "Brandish4 Tower of the Sleeping God");break;
		case 18:s=savedata.lang ? "White Witch" : "白き魔女";break;
		case 19:s=savedata.lang ? "Crimson Tears" : "朱紅い雫";break;
		case 20:s=savedata.lang ? "Cagesong of the Ocean" : "海の檻歌";break;
		case 21:s = savedata.lang ? _T("Trails of Cold Steel I,II,Ys8") : _T("閃の軌跡Ⅰ,Ⅱ,Ys8"); break;
		case 30:s = savedata.lang ? _T("Trails in the Sky The 1st") : _T("空の軌跡 The 1st"); break;
		case -6:s = savedata.lang ? _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX") : _T("閃Ⅲ,Ⅳ,創,零改,Ys9,YsX"); break;
		case -11:s=savedata.lang ? "Lunacy of the Moon" : "月影のラプソディー";break;
		case -12:s=savedata.lang ? "Rhapsody of the West Wind" : "西風の狂詩曲";break;
		case -13:s=(savedata.lang == 0 ? "アークトゥルス" : "Arcturus");break;
		case -14:s=savedata.lang ? "Fantasia Sango 1" : "幻想三国志1";break;
		case -15:s=savedata.lang ? "Fantasia Sango 2" : "幻想三国志2";break;
		case -3:
			ss=fol.Right(fol.GetLength()-fol.ReverseFind('.')-1);
			s.Format((savedata.lang == 0 ? _T("%sファイル") : _T("%s File")),ss);break;
		case -2:
			ss=fol.Right(fol.GetLength()-fol.ReverseFind('.')-1);
			s.Format((savedata.lang == 0 ? _T("%sファイル") : _T("%s File")),ss);break;
		case -1:s=(savedata.lang == 0 ? "oggファイル" : "ogg File");break;
		case -7:
			s = fol; s.MakeLower();
			if (s.Right(3) == "dsf") { s = savedata.lang ? _T("dsf File(DSD)") : _T("dsfファイル(DSD)"); break; }
			if (s.Right(3) == "wsd") { s = savedata.lang ? _T("wsd File(DSD)") : _T("wsdファイル(DSD)"); break; }
			if (s.Right(3) == "dff") { s = savedata.lang ? _T("dff File(DSD)") : _T("dffファイル(DSD)"); break; }
		case -8:
			s = fol; s.MakeLower();
			if (s.Right(4) == "flac") { s = savedata.lang ? _T("flac File") : _T("flacファイル"); break; }
			if (s.Right(6).MakeLower() == "qull3h") { s = savedata.lang ? _T("Qull3H File") : _T("Qull3Hファイル"); break; }
		case -9:
			s = fol; s.MakeLower();
			if (s.Right(3) == "m4a") { s = savedata.lang ? _T("m4a File") : _T("m4aファイル"); break; }
			if (s.Right(3) == "aac") { s = savedata.lang ? _T("aac File") : _T("aacファイル"); break; }
		case -10:
			s=fol;s.MakeLower();
			if(s.Right(3)=="mp3"){ s=(savedata.lang == 0 ? "mp3ファイル" : "mp3 File");break;}
			if(s.Right(3)=="mp2"){ s=(savedata.lang == 0 ? "mp2ファイル" : "mp2 File");break;}
			if(s.Right(3)=="mp1"){ s=(savedata.lang == 0 ? "mp1ファイル" : "mp1 File");break;}
			if(s.Right(3)=="rmp"){ s=(savedata.lang == 0 ? "rmpファイル" : "rmp File");break;}
	}

	if(f)
		if((cnt1=chk(name,sub,art,fol,ret))!=-1){
			pc[cnt1].time=time;
			RECT r;
			m_lc.GetItemRect(cnt1,&r,LVIR_BOUNDS);
			m_lc.RedrawWindow(&r);	
			return cnt1;
		}
//	if(playcnt<60000){
		if(ff){
			playlistdata0 *tmp;	tmp=pc;
		size_t size=_msize(pc);
		playlistdata0 *newPc = (playlistdata0*)realloc(tmp, size + sizeof(playlistdata0));
		if (newPc == NULL) {
			pc = tmp;
			return -1;
		}
		pc = newPc;
			m_lc.SetItemCount(playcnt+1);
		}
		_tcscpy(pc[playcnt].name,name);
		_tcscpy(pc[playcnt].art,art);
		_tcscpy(pc[playcnt].alb,alb);
		_tcscpy(pc[playcnt].fol,fol);
		_tcscpy(pc[playcnt].game,s);
		pc[playcnt].loop1=loop1;
		pc[playcnt].loop2=loop2;
		pc[playcnt].sub=sub;
		pc[playcnt].ret2=ret;
		pc[playcnt].icon=1;
		pc[playcnt].time=time;
//		RECT r;
//		m_lc.GetItemRect(playcnt,&r,LVIR_BOUNDS);
//		m_lc.RedrawWindow(&r);	
		playcnt++;
//	}		
		
	return -1;
}

void CPlayList::Del()
{
	int Lindex=-1,j=0;
	for(;;){//選択されているものをピックアップ
		Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
		if(Lindex==-1) break;
		m_lc.SetItemState(Lindex,m_lc.GetItemState(Lindex,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		for(int i=Lindex+1+j;i<playcnt;i++){
			memcpy(&pc[i-1],&pc[i],sizeof(playlistdata0));
		}
		playcnt--;j--;
	}
	playlistdata0 *tmp;	tmp=pc;
	playlistdata0 *newPc = (playlistdata0*)realloc(tmp, (size_t)sizeof(playlistdata0) * (playcnt + 2));
	if (newPc) {
		pc = newPc;
	} else {
		pc = tmp;
	}//余裕を持って解放
	m_lc.SetItemCount(playcnt);
	for(j=0;j<playcnt;j++) pc[j].icon=1;
	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnSUP()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount();
	CString s,s1;
	for(int j=0;j<i-1;j++){
		if((m_lc.GetItemState(j+1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j+1].icon=1;
			memcpy(&ppp,&pc[j+1],sizeof(playlistdata0));
			memcpy(&pc[j+1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j,j+1);
			m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
			m_lc.SetItemState(j+1,m_lc.GetItemState(j+1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
	}
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnUP()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount(),i2=0;
	CString s,s1;
	for(;;){i2=0;
		for(int j=0;j<i-1;j++){
			if((m_lc.GetItemState(j+1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j+1].icon=1;
			memcpy(&ppp,&pc[j+1],sizeof(playlistdata0));
			memcpy(&pc[j+1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j,j+1);
				m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
				m_lc.SetItemState(j+1,m_lc.GetItemState(j+1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
				i2=1;
			}
		}
		if(i2==0) break;
	}	
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnSDOWN()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount(),i2=0;
	CString s,s1;
	for(;;){i2=0;
		for(int j=i-1;j>0;j--){
			if((m_lc.GetItemState(j-1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j+1].icon=1;
			memcpy(&ppp,&pc[j-1],sizeof(playlistdata0));
			memcpy(&pc[j-1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j-1,j);
				m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
				m_lc.SetItemState(j-1,m_lc.GetItemState(j-1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
				i2=1;
			}
		}
		if(i2==0) break;
	}	
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnDOWN()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount();
	CString s,s1;
	for(int j=i-1;j>0;j--){
		if((m_lc.GetItemState(j-1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j-1].icon=1;
			memcpy(&ppp,&pc[j-1],sizeof(playlistdata0));
			memcpy(&pc[j-1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j-1,j);

			m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
			m_lc.SetItemState(j-1,m_lc.GetItemState(j-1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
	}
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnXCHG(int i,int j)
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
			playlistdata0 ppp;
			pc[j].icon=pc[j-1].icon=1;
			memcpy(&ppp,&pc[i],sizeof(playlistdata0));
			memcpy(&pc[i],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
}

extern COggDlg *og;
extern CString filen,fnn;

extern int modesub,ret2;
extern int loop1, loop2;

void CPlayList::Get(int i)
{
		fnn=pc[i].name; filen=pc[i].fol; modesub=pc[i].sub; loop1=pc[i].loop1; loop2=pc[i].loop2; ret2=pc[i].ret2;
		for(int k=0;k<playcnt;k++){
			m_lc.SetItemState(k,m_lc.GetItemState(k,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
		m_lc.SetItemState(i,LVIS_SELECTED,LVIS_SELECTED);
		SIcon(i);
}

void CPlayList::OnLvnKeydownList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLVKEYDOWN pLVKeyDow = reinterpret_cast<LPNMLVKEYDOWN>(pNMHDR);
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	if(pLVKeyDow->wVKey == VK_DELETE){
		Del();
	}
	*pResult = 0;
}


void CPlayList::OnDropFiles(HDROP hDropInfo)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	TCHAR filen_c[1024];
	syo = 0; syos = ""; syomode = 0;
	int ii=m_lc.GetItemCount();
	UINT cnt = DragQueryFile(hDropInfo,(UINT)-1,filen_c,sizeof(filen_c));
	TCHAR tmp[1024];
	_tgetcwd(tmp,1000);
		for(UINT i=0;i<cnt;i++){
			DragQueryFile(hDropInfo,(UINT)i,filen_c,sizeof(filen_c));
			Fol(filen_c);
		}
	_tchdir(tmp);
	if(syo==1 && (fade1==1 || playf==0) && !pMediaPosition){
		plcnt=ii;
		SIcon(ii);
	}
	if(syo==1 && m_renzoku.GetCheck()==FALSE){
		plcnt=ii;
		SIcon(ii);
		if(PathIsDirectory(syos)==FALSE)
			filen = syos;
		else
			filen = syos + L"\\" + fnn;
		if (syomode == 30) {
			filen = syos;
		}
		og->dp(filen);
	}
	if(syo==1 && pMediaPosition){
		if(mode==-2 || videoonly==TRUE){
			REFTIME aa,bb;
			pMediaPosition->get_CurrentPosition(&aa);
			pMediaPosition->get_Duration(&bb);
			if(aa>=bb){
				if (PathIsDirectory(syos) == FALSE)
					filen = syos;
				else
					filen = syos + L"\\" + fnn;
				og->dp(filen);
			}
		}
	}
	if(syo==1 && (fade1==1 || playf==0) && !pMediaPosition){
		if (PathIsDirectory(syos) == FALSE)
			filen = syos;
		else
			filen = syos + L"\\" + fnn;
		og->dp(filen);
	}
	Save();
	CCustomDialog::OnDropFiles(hDropInfo);
}

#include "Id3tagv1.h"
#include "Id3tagv2.h"

#include "vorbis/codec.h"
#include "vorbis/vorbisfile.h"
OggVorbis_File vf1;
extern BYTE bufimage[0x30000f];

// OggVorbisコールバック関数
extern size_t Callback_Read(
	void* ptr,
	size_t size,
	size_t nmemb,
	void* datasource
);

extern int Callback_Seek(
	void *datasource,
	ogg_int64_t offset,
	int whence
);

extern int Callback_Close(void *datasource);

extern long Callback_Tell(void *datasource);

extern ov_callbacks callbacks;

void CPlayList::Fol(CString fname)
{
	CString fname_full = fname;
	CString ft; 
	ft = "*.*";
	CString ft2;
	if (PathIsDirectory(fname) == FALSE) {
		CString ft1;
		ft1 = fname;
		ft = ft1.Right(ft1.GetLength()-ft1.ReverseFind(L'\\')-1);
	}
	CString s, ss;
	playlistdata p; ZeroMemory(&p, sizeof(p));
	CFileFind f;
	if (PathIsDirectory(fname) == FALSE) {
		CString ff = fname.Left(fname.ReverseFind('\\'));
		_tchdir(ff);
	}
	else {
		_tchdir(fname);
	}
	CString fname1 = fname;
	if (f.FindFile(ft)) {
		int b = 1;
		for (; b;) {
			b = f.FindNextFile();
			s = f.GetFileName();
			if (f.IsDirectory() == 0) {
				fname = fname1;
				BOOL a1 = PathIsDirectory(fname);
				if (a1) {
					fname = fname1 + L"\\" + s;
				}
				else {
					
				}
				//CString ff = fname.Left(fname.ReverseFind('\\'));
				//_tchdir(ff);
				ft = s;
				ft2 = s;
				ft.MakeLower();
				CFile ff2;
				
				ff2.Open(s, CFile::modeRead | CFile::shareDenyNone, NULL);
				ff2.Read(bufimage, 2);
				ff2.Close();
				if (ft.Right(4).MakeLower() == ".ogg" || ft.Right(4) == ".OGG" || ft.Right(6).MakeLower() == ".qull3") {
					p.sub = -1;
					mode = -1;
					FILE *fp;
					fp = _tfopen(fname, _T("rb"));
					if (fp == NULL) {
						return;
					}

					if (ov_open_callbacks(fp, &vf1, NULL, 0, callbacks) < 0) {
						fclose(fp);
						return;
					}
					CString cc;
					_tcscpy(p.name, ft2);
					p.alb[0] = p.art[0] = NULL;
					for (int iii = 0; iii < vf1.vc->comments; iii++) {
#if _UNICODE
						WCHAR f[1024];
						MultiByteToWideChar(CP_UTF8, 0, vf1.vc->user_comments[iii], -1, f, 1024);
						cc = f;
#else
						cc = vf1.vc->user_comments[iii];
#endif
						if (cc.Left(6).MakeUpper() == "TITLE=")
						{
#if _UNICODE
							ss = UTF8toUNI(cc.Mid(6));
#else
							ss = UTF8toSJIS(cc.Mid(6));
#endif
							_tcscpy(p.name, ss);
						}
						if (cc.Left(7).MakeUpper() == "ARTIST=")
						{
#if _UNICODE
							ss = UTF8toUNI(cc.Mid(7));
#else
							ss = UTF8toSJIS(cc.Mid(7));
#endif
							_tcscpy(p.art, ss);
						}
						if (cc.Left(6).MakeUpper() == "ALBUM=")
						{
#if _UNICODE
							ss = UTF8toUNI(cc.Mid(6));
#else
							ss = UTF8toSJIS(cc.Mid(6));
#endif
							_tcscpy(p.alb, ss);
						}
					}
					ov_clear(&vf1);
					fclose(fp);

					//YS8 steam版用　bgmテーブル変換
					CString sss = fname.Left(fname.ReverseFind('\\')); ss = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					_tchdir(sss);
					//ys8用
					CStdioFile f;
					char *buff;
					int looping = 0;
					int igg;

					ss = ss.Left(ss.ReverseFind('.'));
					char file[256];
					WCHAR outcm[1024];
					WideCharToMultiByte(CP_ACP, 0, ss, 1024, file, 256, NULL, NULL);
					FILE *fp2;
					fp2 = _wfopen(L"..\\text\\bgmtbl.tbl", L"r");
					if (fp2) {
						buff = (char*)calloc(256, 1);
						for (;;) {
							if (fgets(buff, 256, fp2) == NULL) {
								free(buff); break;
							}
							char *p1 = strstr(buff, file);
							if (p1 == NULL) continue;
							if (buff[0] == '/') continue;
							p1 += strlen(file) + 1;
							for (; *p1 == 0x09; p1++);
							if (*p1 == '1') looping = 1;
							p1++;
							for (; *p1 == 0x09; p1++);
							typedef struct {
								char st[8];
								char a[1];
								char ed[8];
							} aa;
							aa *aa1;
							aa1 = (aa*)p1;
							int i, j;
							j = 0;
							for (i = 0; i < 8; i++) {
								switch (aa1->st[i])
								{
								case '0':
									j *= 10; j += 0;
									break;
								case '1':
									j *= 10; j += 1;
									break;
								case '2':
									j *= 10; j += 2;
									break;
								case '3':
									j *= 10; j += 3;
									break;
								case '4':
									j *= 10; j += 4;
									break;
								case '5':
									j *= 10; j += 5;
									break;
								case '6':
									j *= 10; j += 6;
									break;
								case '7':
									j *= 10; j += 7;
									break;
								case '8':
									j *= 10; j += 8;
									break;
								case '9':
									j *= 10; j += 9;
									break;
								}
							}
							loop1 = j;
							j = 0;
							for (i = 0; i < 8; i++) {
								switch (aa1->ed[i])
								{
								case '0':
									j *= 10; j += 0;
									break;
								case '1':
									j *= 10; j += 1;
									break;
								case '2':
									j *= 10; j += 2;
									break;
								case '3':
									j *= 10; j += 3;
									break;
								case '4':
									j *= 10; j += 4;
									break;
								case '5':
									j *= 10; j += 5;
									break;
								case '6':
									j *= 10; j += 6;
									break;
								case '7':
									j *= 10; j += 7;
									break;
								case '8':
									j *= 10; j += 8;
									break;
								case '9':
									j *= 10; j += 9;
									break;
								}
							}
							loop2 = j - loop1;
							p1 += sizeof(aa) + 1;
							for (; *p1 == 0x09; p1++);
							p1 += 3;
							char* pp = p1;
							for (; *p1 != 0xd; p1++) {
								if (*p1 == 0x9) {
									*p1 = 0x20;
								}
							}
							p1 = pp;
							MultiByteToWideChar(CP_ACP, 0, p1, -1, outcm, 1024);
							ss = outcm;
							_tcscpy(p.name, ss.Trim());
							if (looping == 0) {
								loop1 = loop2 = 0;
							}
							free(buff); break;
						}
						fclose(fp2);
					}

					//YSC
					sss = fname.Left(fname.ReverseFind('\\'));
					_tchdir(sss);
					ss = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					if (ss == "yc_b001.ogg") {
						ss = LL2(L"バトル#58", L"Battle #58");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b002.ogg") {
						ss = LL2(L"灼熱の炎の中で", L"Within the Blazing Flames");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b003.ogg") {
						ss = LL2(L"最終決戦", L"Final Battle");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b004.ogg") {
						ss = LL2(L"黒き翼", L"Black Wings");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b005.ogg") {
						ss = "The False God of Causality";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d101.ogg") {
						ss = LL2(L"ダンジョン", L"Dungeon");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d201.ogg") {
						ss = LL2(L"道化師の誘い", L"Clown's Invitation");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d301.ogg") {
						ss = LL2(L"地下遺跡", L"Underground Ruins");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d401.ogg") {
						ss = LL2(L"導きの塔～エルディールにくちづけを", L"Tower of Guidance -Kiss for Eldeel-");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d501.ogg") {
						ss = LL2(L"失われし仮面を求めて", L"Seeking the Lost Mask");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d701.ogg") {
						ss = LL2(L"イリス", L"Iris");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d702.ogg") {
						ss = "yc_d702";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d703.ogg") {
						ss = LL2(L"聖域", L"Sanctuary");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e001.ogg") {
						ss = LL2(L"賢者", L"Sage");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e002.ogg") {
						ss = LL2(L"復活の儀式", L"Resurrection Ceremony");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e003.ogg") {
						ss = LL2(L"レファンス", L"Refance");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e004.ogg") {
						ss = LL2(L"涙の少年剣士", L"Young Swordsman in Tears");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e005.ogg") {
						ss = LL2(L"エルディール", L"Eldeel");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e006.ogg") {
						ss = LL2(L"ロムン帝国 -嗚呼レオ団長-", L"Romun Empire -Alas Captain Leo-");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e008.ogg") {
						ss = "yc_e008";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e010.ogg") {
						ss = LL2(L"冒険家、誕生", L"Birth of an Adventurer");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f101.ogg") {
						ss = LL2(L"燃ゆる剣", L"Burning Sword");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f201.ogg") {
						ss = LL2(L"セルセタの樹海", L"Forest of Celceta");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f301.ogg") {
						ss = LL2(L"クレーター", L"Crater");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f401.ogg") {
						ss = "THE DAWN OF YS";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f501.ogg") {
						ss = LL2(L"暁の森", L"Forest of Dawn");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f601.ogg") {
						ss = LL2(L"一陣の風", L"Gust of Wind");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f701.ogg") {
						ss = LL2(L"神代の地", L"Land of the Gods");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f801.ogg") {
						ss = LL2(L"真実への序曲", L"Overture to Truth");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f901.ogg") {
						ss = LL2(L"雨上がりの朝に", L"Morning After the Rain");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_over.ogg") {
						ss = LL2(L"ゲームオーバー", L"Game Over");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t101.ogg") {
						ss = LL2(L"辺境都市《キャスナン》", L"Frontier City Casnan");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t201.ogg") {
						ss = LL2(L"優しくなりたい", L"I Want to Be Kind");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t301.ogg") {
						ss = LL2(L"古代の伝承", L"Ancient Legend");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t501.ogg") {
						ss = "RODA";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_title.ogg") {
						ss = "THEME OF ADOL 2012";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_op.ogg") {
						ss = "The Foliage Ocean in CELCETA -Opening size-";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_end.ogg") {
						ss = LL2(L"新たな時代のステージへ", L"To the Stage of a New Era");
						_tcscpy(p.name, ss);
					}

					//zero 
					CString ss;
					ss = fname.Right(fname.GetLength() - fname.ReverseFind(L'\\') - 1);
					sss = fname.Left(fname.ReverseFind('\\'));
					int fg = 0;
					CFile ffff;
					if (ffff.Open(sss + L"\\..\\text\\t_bgm._dt", CFile::modeRead)) { fg = 1; ffff.Close(); }
					CString zero = savedata.zero;
					if(zero != L"") if (ffff.Open(savedata.zero, CFile::modeRead)) { fg = 1; ffff.Close(); }
					if (ss.Mid(0, 3) == L"ed7" && fg == 1) {
						CString a;
						switch (_ttoi(ss.Mid(2, 4))) {
						case 7001:
							a = LL2(L"零の軌跡", L"Trails from Zero");
							break;
						case 7002:
							a = L"way of live -Opening Version-";
							break;
						case 7003:
							a = LL2(L"新しき日々～予兆", L"New Days -Omen-");
							break;
						case 7005:
							a = LL2(L"想い破れて・・・", L"Broken Heart...");
							break;
						case 7052:
							a = LL2(L"碧い軌跡 -Opening size-", L"Azure Arbitrator -Opening size-");
							break;
						case 7053:
							a = LL2(L"それでも僕らは。", L"Yet We're Still Here.");
							break;
						case 7100:
							a = LL2(L"街角の風景", L"Street Corner Scenery");
							break;
						case 7101:
							a = LL2(L"明日は明日の風が吹く", L"Tomorrow the Wind Will Blow");
							break;
						case 7102:
							a = LL2(L"クロスベルの午後", L"Afternoon in Crossbell");
							break;
						case 7103:
							a = L"During Mission Accomplishment";
							break;
						case 7104:
							a = LL2(L"創立記念祭", L"Founding Festival");
							break;
						case 7105:
							a = LL2(L"降水確率10%", L"10% Chance of Rain");
							break;
						case 7106:
							a = LL2(L"風船と紙吹雪", L"Balloons and Confetti");
							break;
						case 7110:
							a = LL2(L"特務支援課", L"Special Support Section");
							break;
						case 7111:
							a = LL2(L"C.S.P.D. -クロスベル警察", L"C.S.P.D. -Crossbell Police");
							break;
						case 7113:
							a = L"Arc-en-ciel";
							break;
						case 7114:
							a = LL2(L"黒月貿易公司", L"Heiyue Trading Company");
							break;
						case 7116:
							a = L"IGNIS";
							break;
						case 7117:
							a = L"TRINITY";
							break;
						case 7120:
							a = LL2(L"アルモリカ村", L"Armorica Village");
							break;
						case 7121:
							a = LL2(L"鉱山町マインツ", L"Mines Town Mainz");
							break;
						case 7122:
							a = L"Killing Bear";
							break;
						case 7123:
							a = LL2(L"聖ウルスラ医科大学", L"St. Ursula Medical College");
							break;
						case 7124:
							a = LL2(L"クロスベル大聖堂", L"Crossbell Cathedral");
							break;
						case 7125:
							a = LL2(L"黒の競売会", L"Black Auction");
							break;
						case 7126:
							a = LL2(L"大国にはさまれて", L"Caught Between Nations");
							break;
						case 7150:
							a = LL2(L"新たなる日常", L"New Daily Life");
							break;
						case 7151:
							a = LL2(L"動き始めた事態", L"Events in Motion");
							break;
						case 7160:
							a = LL2(L"ミシュラムワンダーランド", L"Mishyram Wonderland");
							break;
						case 7161:
							a = LL2(L"束の間の休息", L"Brief Respite");
							break;
						case 7162:
							a = LL2(L"ささやかな晩餐", L"Simple Dinner");
							break;
						case 7200:
							a = LL2(L"水と草木と青い空", L"Water, Trees and Blue Sky");
							break;
						case 7201:
							a = LL2(L"片手にはレモネード", L"Lemonade in One Hand");
							break;
						case 7202:
							a = LL2(L"木霊の道", L"Path of Echoes");
							break;
						case 7203:
							a = LL2(L"古の鼓動", L"Ancient Pulse");
							break;
						case 7204:
							a = L"On The Green Road";
							break;
						case 7205:
							a = LL2(L"鉄橋を越えて", L"Crossing the Iron Bridge");
							break;
						case 7250:
							a = LL2(L"木洩れ日の中の静寂", L"Tranquility in the Dappled Light");
							break;
						case 7251:
							a = LL2(L"偽りの楽土を越えて", L"Beyond the False Paradise");
							break;
						case 7300:
							a = LL2(L"ジオフロント", L"Geofront");
							break;
						case 7301:
							a = LL2(L"七耀の煌き", L"Septium Radiance");
							break;
						case 7302:
							a = LL2(L"ルバーチェ商会", L"Revache Trading Company");
							break;
						case 7303:
							a = LL2(L"鳴るはずのない鐘", L"The Bell That Shouldn't Ring");
							break;
						case 7304:
							a = LL2(L"忘れられし幻夢の狭間", L"Forgotten Phantasmal Gap");
							break;
						case 7305:
							a = L"A Light Illuminating The Depths";
							break;
						case 7350:
							a = LL2(L"Dの残影", L"D's Shadow");
							break;
						case 7351:
							a = LL2(L"異変の兆し", L"Omen of Change");
							break;
						case 7352:
							a = L"Mystic Core";
							break;
						case 7353:
							a = LL2(L"最果ての樹", L"Tree at World's End");
							break;
						case 7354:
							a = LL2(L"暴魔の呼び声", L"Call of the Beast");
							break;
						case 7356:
							a = LL2(L"不明", L"Unknown");
							break;
						case 7400:
							a = L"Get Over The Barrier!";
							break;
						case 7401:
							a = L"Arrest The Criminal";
							break;
						case 7402:
							a = L"Formidable Enemy";
							break;
						case 7403:
							a = L"Stand Up Battle Formation Again!";
							break;
						case 7404:
							a = L"Inevitable Struggle";
							break;
						case 7405:
							a = L"Demonic Drive";
							break;
						case 7406:
							a = L"Arrival Existence";
							break;
						case 7408:
							a = LL2(L"これが俺たちの力だ!", L"This Is Our Power!");
							break;
						case 7450:
							a = L"Seize The Truth!";
							break;
						case 7451:
							a = L"Concentrate All Firepower!!";
							break;
						case 7452:
							a = L"Conflicting Passions";
							break;
						case 7453:
							a = L"Unexpected Emergency";
							break;
						case 7454:
							a = L"Mythtic Roar";
							break;
						case 7455:
							a = L"Destruction Impulse";
							break;
						case 7458:
							a = L"Unfathomed Force";
							break;
						case 7459:
							a = L"The Azure Arbitrator";
							break;
						case 7460:
							a = LL2(L"効果音", L"Sound Effect");
							break;
						case 7500:
							a = LL2(L"金の太陽、銀の月　-陽の熱情", L"Golden Sun, Silver Moon -Solar Passion-");
							break;
						case 7501:
							a = LL2(L"金の太陽、銀の月　-月の慕情", L"Golden Sun, Silver Moon -Lunar Affection-");
							break;
						case 7502:
							a = LL2(L"金の太陽、銀の月　-童心", L"Golden Sun, Silver Moon -Innocence-");
							break;
						case 7503:
							a = LL2(L"金の太陽、銀の月　-運命の刻", L"Golden Sun, Silver Moon -Hour of Fate-");
							break;
						case 7504:
							a = LL2(L"金の太陽、銀の月　-譲れぬ想い", L"Golden Sun, Silver Moon -Unyielding Feelings-");
							break;
						case 7505:
							a = LL2(L"金の太陽、銀の月　-幾千の夜を越えて", L"Golden Sun, Silver Moon -Beyond Countless Nights-");
							break;
						case 7506:
							a = LL2(L"金の太陽、銀の月　-夜明け～大団円", L"Golden Sun, Silver Moon -Dawn to Grand Finale-");
							break;
						case 7507:
							a = L"Intense Chase";
							break;
						case 7509:
							a = LL2(L"守りぬく意志", L"Unyielding Will");
							break;
						case 7510:
							a = LL2(L"叡智への誘い", L"Invitation to Wisdom");
							break;
						case 7511:
							a = LL2(L"危地", L"Perilous Ground");
							break;
						case 7512:
							a = LL2(L"揺るぎない強さ", L"Unshakable Strength");
							break;
						case 7513:
							a = LL2(L"夜景に霞む星空", L"Starry Sky in the Night");
							break;
						case 7514:
							a = LL2(L"いつかきっと", L"Someday");
							break;
						case 7515:
							a = LL2(L"柔らかな心", L"Tender Heart");
							break;
						case 7516:
							a = LL2(L"点と線", L"Dots and Lines");
							break;
						case 7517:
							a = LL2(L"一触即発", L"Imminent Crisis");
							break;
						case 7518:
							a = L"Foolish Gig";
							break;
						case 7519:
							a = LL2(L"リベールからの風", L"Wind from Liberl");
							break;
						case 7520:
							a = LL2(L"とどいた想い", L"Feelings Delivered");
							break;
						case 7521:
							a = L"Underground Kids";
							break;
						case 7522:
							a = L"Terminal Room";
							break;
						case 7523:
							a = LL2(L"響きあう心", L"Resonating Hearts");
							break;
						case 7524:
							a = L"Limit Break";
							break;
						case 7525:
							a = LL2(L"パラダイスミ☆", L"Paradigm☆");
							break;
						case 7526:
							a = L"Gnosis";
							break;
						case 7527:
							a = L"Get Over The Barrier! -Roaring Version-";
							break;
						case 7528:
							a = LL2(L"それぞれの明日", L"Our Tomorrows");
							break;
						case 7529:
							a = LL2(L"効果音楽1", L"Sound Effect Music 1");
							break;
						case 7530:
							a = LL2(L"効果音楽2", L"Sound Effect Music 2");
							break;
						case 7531:
							a = LL2(L"効果音楽3", L"Sound Effect Music 3");
							break;
						case 7532:
							a = LL2(L"効果音楽4", L"Sound Effect Music 4");
							break;
						case 7533:
							a = LL2(L"踏み出す勇気", L"Courage to Step Forward");
							break;
						case 7534:
							a = LL2(L"その背中を見つめて", L"Watching Your Back");
							break;
						case 7540:
						case 7541:
						case 7542:
						case 7543:
						case 7544:
							a = LL2(L"不明", L"Unknown");
							break;
						case 7550:
							a = LL2(L"オルキスタワー", L"Orchis Tower");
							break;
						case 7551:
							a = L"Catastrophe";
							break;
						case 7552:
							a = LL2(L"碧き雫", L"Azure Arbitrator");
							break;
						case 7553:
							a = LL2(L"神機降臨", L"Divine Mechanoid Descent");
							break;
						case 7554:
							a = LL2(L"ふるわれる奇蹟", L"Shaking Miracle");
							break;
						case 7555:
							a = LL2(L"予定外の奇蹟", L"Unexpected Miracle");
							break;
						case 7556:
							a = LL2(L"鋼鉄の咆哮 -脅威-", L"Roar of Steel -Threat-");
							break;
						case 7560:
							a = LL2(L"雨の日の真実", L"Truth on a Rainy Day");
							break;
						case 7561:
							a = LL2(L"不穏", L"Troubled");
							break;
						case 7562:
							a = LL2(L"効果音", L"Sound Effect");
							break;
						case 7563:
							a = LL2(L"犠牲の先の希望", L"Hope Beyond Sacrifice");
							break;
						case 7564:
							a = L"Strange Feel";
							break;
						case 7565:
							a = L"Exhilarating Ride";
							break;
						case 7566:
							a = LL2(L"それぞれの正義", L"Each One's Justice");
							break;
						case 7567:
							a = LL2(L"乗り越えるべき壁", L"Wall to Overcome");
							break;
						case 7568:
							a = LL2(L"月下の想い", L"Feelings Under the Moon");
							break;
						case 7569:
							a = L"Miss You";
							break;
						case 7570:
							a = LL2(L"天の車", L"Chariot of Heaven");
							break;
						case 7571:
							a = LL2(L"突きつけられた現実", L"Reality Thrust Upon Us");
							break;
						case 7572:
							a = LL2(L"効果音", L"Sound Effect");
							break;
						case 7573:
							a = LL2(L"全てを識るもの", L"The Omniscient");
							break;
						case 7574:
							a = LL2(L"想い、辿り着く場所", L"Where Feelings Lead");
							break;
						case 7575:
							a = LL2(L"揺れ動く心", L"Wavering Heart");
							break;
						case 7576:
							a = LL2(L"星降る夜に", L"On a Starry Night");
							break;
						case 7577:
							a = LL2(L"効果音", L"Sound Effect");
							break;
						case 7578:
							a = LL2(L"効果音", L"Sound Effect");
							break;
						case 7579:
							a = LL2(L"効果音", L"Sound Effect");
							break;
						case 7580:
							a = LL2(L"効果音", L"Sound Effect");
							break;
						case 7581:
							a = LL2(L"本当の絆", L"True Bonds");
							break;
						case 7582:
							a = LL2(L"猛き獣たち", L"Fierce Beasts");
							break;
						case 7583:
							a = LL2(L"西ゼムリア通商会議", L"West Zemuria Trade Conference");
							break;
						case 7584:
							a = LL2(L"効果音", L"Sound Effect");
							break;
						case 7585:
							a = LL2(L"千年の妄執", L"Obsession of Millennia");
							break;
						case 7586:
							a = LL2(L"鋼鉄の咆哮 -死線-", L"Roar of Steel -Death Line-");
							break;
						case 7587:
							a = LL2(L"ポムっと! -お花見団子の逆襲-", L"Pom! -Cherry Blossom Dango Counterattack-");
							break;
						case 7588:
							a = LL2(L"Fateful Confrontation -ポムっと! Ver.-", L"Fateful Confrontation -Pom! Ver.-");
							break;
						case 7589:
							a = LL2(L"ポムりますか", L"Shall We Pom?");
							break;
						case 7590:
							a = LL2(L"エリィ絶叫コースター", L"Elie Scream Coaster");
							break;
						case 7591:
							a = LL2(L"小さな英雄 -オルゴール-", L"Little Hero -Music Box-");
							break;
						case 7592:
							a = L"TOWER OF THE SHADOW OF DEATH -Jukebox-";
							break;
						}
						_tcscpy(p.name, a);
					}
					_tcscpy(p.fol, fname1);
					p.loop1 = p.loop2 = 0;
						}
				else if (fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1).Mid(0,3) == L"ed8" && (ft.Right(4) == ".wav")) {
					p.sub = 21; p.loop1 = p.loop2 = 0;
					CString a = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					switch (_ttoi(a.Mid(2, 4))) {
					case 8001:
						a = LL2(L"特科クラス《VII組》", L"Class VII");
						break;
					case 8002:
						a = LL2(L"ただひたすらに、前へ", L"Ever Forward");
						break;
					case 8100:
						a = LL2(L"近郊都市トリスタ", L"Suburban City Trista");
						break;
					case 8101:
						a = LL2(L"交易町ケルディック", L"Trading Town Celdic");
						break;
					case 8102:
						a = LL2(L"翡翠の公都バリアハート", L"Jade Capital Bareahard");
						break;
					case 8103:
						a = LL2(L"湖畔の街レグラム", L"Lakeside Town Legram");
						break;
					case 8104:
						a = LL2(L"黒銀の鋼都ルーレ", L"Iron City Roer");
						break;
					case 8106:
						a = LL2(L"遊牧民の集落", L"Nomad Settlement");
						break;
					case 8107:
						a = LL2(L"緋の帝都ヘイムダル", L"Crimson Capital Heimdallr");
						break;
					case 8108:
						a = LL2(L"癒しの我が家", L"Healing Home");
						break;
					case 8109:
						a = LL2(L"ダイニングバー《F》", L"Dining Bar F");
						break;
					case 8110:
						a = LL2(L"常在戦場の気概", L"Ever-Present War Spirit");
						break;
					case 8111:
						a = LL2(L"ガレリアの巨壁", L"Garelia Fortress");
						break;
					case 8120:
						a = LL2(L"足湯の温もり", L"Foot Bath Warmth");
						break;
					case 8121:
						a = LL2(L"静寂の郷", L"Silent Village");
						break;
					case 8122:
						a = LL2(L"明日への休息", L"Rest for Tomorrow");
						break;
					case 8123:
						a = LL2(L"春の陽射し", L"Spring Sunshine");
						break;
					case 8125:
						a = LL2(L"カレイジャス発進！", L"Courageous Launch!");
						break;
					case 8126:
						a = LL2(L"目覚める意志", L"Awakening Will");
						break;
					case 8127:
						a = LL2(L"白銀の巨船", L"Silver Ship");
						break;
					case 8150:
						a = LL2(L"放課後の時間", L"After School");
						break;
					case 8152:
						a = LL2(L"さわやかな朝", L"Refreshing Morning");
						break;
					case 8153:
						a = LL2(L"雨音の学院", L"Rain-sound Academy");
						break;
					case 8154:
						a = LL2(L"爽やかな陽射し", L"Clear Sunshine");
						break;
					case 8156:
						a = LL2(L"トールズ士官学院祭", L"Thors Academy Festival");
						break;
					case 8158:
						a = LL2(L"青空の開放感", L"Open Sky");
						break;
					case 8159:
						a = LL2(L"自由行動日", L"Free Day");
						break;
					case 8200:
						a = LL2(L"異郷の空", L"Foreign Sky");
						break;
					case 8201:
						a = LL2(L"峡谷道を往く", L"Through the Canyon");
						break;
					case 8202:
						a = LL2(L"精霊の小道", L"Spirit Path");
						break;
					case 8203:
						a = LL2(L"蒼穹の大地", L"Azure Skies Land");
						break;
					case 8210:
						a = LL2(L"戦火を越えて", L"Beyond the Flames of War");
						break;
					case 8212:
						a = L"Trudge Along";
						break;
					case 8213:
						a = LL2(L"冬の訪れ", L"Arrival of Winter");
						break;
					case 8300:
						a = LL2(L"旧校舎の謎", L"Old Schoolhouse Mystery");
						break;
					case 8301:
						a = LL2(L"探索", L"Exploration");
						break;
					case 8302:
						a = LL2(L"深淵へ向かう", L"Toward the Abyss");
						break;
					case 8303:
						a = LL2(L"聖女の城", L"Saint's Castle");
						break;
					case 8304:
						a = LL2(L"明日を掴むために", L"To Seize Tomorrow");
						break;
					case 8305:
						a = LL2(L"地下に眠る遺構", L"Ruins Beneath");
						break;
					case 8308:
						a = LL2(L"世の礎たるために", L"To Be the World's Foundation");
						break;
					case 8310:
						a = LL2(L"精霊窟", L"Spirit Cave");
						break;
					case 8311:
						a = LL2(L"不明", L"Unknown");
						break;
					case 8312:
						a = L"Phantasmal Blaze";
						break;
					case 8313:
						a = LL2(L"夢幻回廊", L"Phantasmagoria Corridor");
						break;
					case 8315:
						a = LL2(L"幻煌", L"Phantom Radiance");
						break;
					case 8400:
						a = L"The Glint of Cold Steel";
						break;
					case 8401:
						a = L"Tie a Link of ARCUS!";
						break;
					case 8402:
						a = L"Belief";
						break;
					case 8403:
						a = L"Even if Driven to the Wall";
						break;
					case 8404:
						a = L"Eliminate Crisis!";
						break;
					case 8405:
						a = L"Exceed!";
						break;
					case 8406:
						a = L"Don't be Defeated by a Friend!";
						break;
					case 8407:
						a = L"Machinery Attack";
						break;
					case 8408:
						a = LL2(L"巨イナルチカラ", L"Colossal Power");
						break;
					case 8409:
						a = L"The Decisive Collision";
						break;
					case 8410:
						a = LL2(L"この手で道を切り拓く!", L"Carve Our Path with These Hands!");
						break;
					case 8411:
						a = LL2(L"赤点です...", L"Failed...");
						break;
					case 8412:
						a = L"Unknown Threat";
						break;
					case 8413:
						a = LL2(L"不明", L"Unknown");
						break;
					case 8420:
						a = L"Heated Mind";
						break;
					case 8421:
						a = LL2(L"不明", L"Unknown");
						break;
					case 8423:
						a = L"Impatient";
						break;
					case 8424:
						a = L"Severe Blow";
						break;
					case 8426:
						a = L"Transcend Beat";
						break;
					case 8429:
						a = L"Blue Destination";
						break;
					case 8430:
						a = L"Heteromorphy";
						break;
					case 8431:
						a = LL2(L"輝ける明日へ", L"Toward a Shining Tomorrow");
						break;
					case 8435:
						a = LL2(L"迫る巨影", L"Approaching Giant Shadow");
						break;
					case 8441:
						a = L"E.O.V";
						break;
					case 8442:
						a = LL2(L"不明", L"Unknown");
						break;
					case 8500:
						a = L"Strain";
						break;
					case 8501:
						a = LL2(L"夜のひととき", L"Nighttime");
						break;
					case 8502:
						a = LL2(L"トラブル発生", L"Trouble");
						break;
					case 8503:
						a = LL2(L"鉄路遥々", L"Distant Iron Road");
						break;
					case 8504:
						a = LL2(L"旅愁", L"Travel Melancholy");
						break;
					case 8505:
						a = LL2(L"皇城にて", L"At the Imperial Castle");
						break;
					case 8506:
						a = L"Let's Study";
						break;
					case 8507:
						a = LL2(L"知恵を絞って", L"Rack Your Brains");
						break;
					case 8508:
						a = LL2(L"実技教練", L"Combat Training");
						break;
					case 8509:
						a = LL2(L"寮に帰ろう", L"Back to the Dorm");
						break;
					case 8510:
						a = LL2(L"アーベントタイム", L"Evening Time");
						break;
					case 8512:
						a = LL2(L"鉄の統率", L"Iron Command");
						break;
					case 8513:
						a = LL2(L"暗躍", L"Moving in the Shadows");
						break;
					case 8514:
						a = LL2(L"想いの行き先", L"Where Feelings Lead");
						break;
					case 8515:
						a = LL2(L"傷心", L"Heartbreak");
						break;
					case 8516:
						a = LL2(L"揺らめく炎を見つめて", L"Watching the Flickering Flames");
						break;
					case 8517:
						a = LL2(L"一途な気持ち", L"Single-minded Feelings");
						break;
					case 8520:
						a = LL2(L"臨戦態勢", L"Combat Ready");
						break;
					case 8521:
						a = L"Seriousness";
						break;
					case 8522:
						a = LL2(L"静かなる昂揚", L"Quiet Exhilaration");
						break;
					case 8523:
						a = LL2(L"暖かな夕餉", L"Warm Dinner");
						break;
					case 8524:
						a = L"Atrocious Raid";
						break;
					case 8525:
						a = LL2(L"全てを賭して今、ここに立つ", L"Standing Here, Betting Everything");
						break;
					case 8527:
						a = LL2(L"新しい仲間たち", L"New Comrades");
						break;
					case 8528:
						a = LL2(L"不透明な事態", L"Opaque Situation");
						break;
					case 8529:
						a = LL2(L"鉄血へのレクイエム", L"Requiem for Iron and Blood");
						break;
					case 8530:
						a = LL2(L"幻想の唄 -PHANTASMAGORIA-", L"Phantom Song -PHANTASMAGORIA-");
						break;
					case 8531:
						a = LL2(L"刻ハ至レリ", L"The Hour Has Come");
						break;
					case 8532:
						a = LL2(L"目覚めし伝承", L"Awakening Legend");
						break;
					case 8533:
						a = LL2(L"唯一の希望", L"Only Hope");
						break;
					case 8535:
						a = LL2(L"不明", L"Unknown");
						break;
					case 8537:
						a = LL2(L"不明", L"Unknown");
						break;
					case 8538:
						a = LL2(L"今はまだ...", L"Not Yet...");
						break;
					case 8539:
						a = LL2(L"あの日に見た夜空", L"The Night Sky I Saw That Day");
						break;
					case 8540:
						a = LL2(L"偽りの時間", L"False Time");
						break;
					case 8541:
						a = LL2(L"紅き翼 -新たなる風-", L"Crimson Wings -New Wind-");
						break;
					case 8550:
						a = LL2(L"再会", L"Reunion");
						break;
					case 8551:
						a = LL2(L"かけがえのない人へ", L"To Someone Irreplaceable");
						break;
					case 8552:
						a = LL2(L"惜しむように、愛おしむように", L"Cherishing, Treasuring");
						break;
					case 8553:
						a = LL2(L"ライノの花が咲く頃", L"When the Rhino Flower Blooms");
						break;
					case 8555:
						a = LL2(L"戦場の掟", L"Rules of Battlefield");
						break;
					case 8556:
						a = L"Remaining Glow";
						break;
					case 8557:
						a = LL2(L"深淵の魔女", L"Witch of the Abyss");
						break;
					case 8558:
						a = L"ALTINA";
						break;
					case 8559:
						a = LL2(L"威風", L"Dignity");
						break;
					case 8560:
						a = LL2(L"一撃に賭ける", L"Bet on One Strike");
						break;
					case 8561:
						a = LL2(L"ユミル渓谷道", L"Ymir Valley Road");
						break;
					case 8562:
						a = L"Awakening";
						break;
					case 8563:
						a = L"Blitzkrieg";
						break;
					case 8564:
						a = LL2(L"魔王の凱歌", L"Demon Lord's Triumph");
						break;
					case 8566:
						a = LL2(L"内なる黄昏", L"Inner Twilight");
						break;
					case 8567:
						a = LL2(L"蘇る記憶", L"Awakened Memories");
						break;
					case 8570:
						a = LL2(L"静かな決意", L"Quiet Resolution");
						break;
					case 8571:
						a = LL2(L"乾坤一擲", L"All or Nothing");
						break;
					case 8572:
						a = LL2(L"交戦", L"Combat");
						break;
					case 8573:
						a = LL2(L"効果音", L"Sound Effect");
						break;
					case 8600:
						a = LL2(L"大市の賑わい", L"Bustling Market");
						break;
					case 8601:
						a = LL2(L"剣の遊戯", L"Sword Play");
						break;
					case 8602:
						a = LL2(L"紙一重の攻防", L"Close Fight");
						break;
					case 8603:
						a = LL2(L"走れマッハ号!", L"Run Mach Train!");
						break;
					case 8605:
						a = LL2(L"効果音", L"Sound Effect");
						break;
					case 8606:
						a = LL2(L"効果音", L"Sound Effect");
						break;
					case 8607:
						a = LL2(L"星屑のカンタータ", L"Cantata of Stardust");
						break;
					case 8608:
						a = LL2(L"効果音", L"Sound Effect");
						break;
					case 8609:
						a = L"Sonata No.45";
						break;
					case 8610:
						a = LL2(L"効果音", L"Sound Effect");
						break;
					case 8620:
						a = LL2(L"雪ウサギを追いかけて", L"Chasing the Snow Rabbit");
						break;
					case 8621:
						a = L"Take The Windward!";
						break;
					case 8622:
						a = LL2(L"効果音", L"Sound Effect");
						break;
					case 8623:
						a = LL2(L"効果音", L"Sound Effect");
						break;
					case 8624:
						a = LL2(L"効果音", L"Sound Effect");
						break;
					case 8625:
						a = LL2(L"効果音", L"Sound Effect");
						break;
					case 8627:
						a = LL2(L"効果音", L"Sound Effect");
						break;
					case 8628:
						a = LL2(L"不明", L"Unknown");
						break;
					case 8629:
						a = LL2(L"効果音", L"Sound Effect");
						break;
					case 8700:
					case 8703:
					case 8704:
					case 8710:
					case 8711:
						a = LL2(L"音楽", L"Music");
						break;
					}
					_tcscpy(p.name, a);
					_tcscpy(p.fol, fname1);
				}
				else if ((ft == L"bgm1.pac" || ft == L"bgm2.pac" || ft == L"bgm3.pac") && fname.Find(L"Trails in the Sky 1st Chapter") > 0) {
					p.sub = 30; p.loop1 = p.loop2 = 0;
					char ti1[][100] = {
						"001 風を共に舞う気持ち",
						"100 地方都市ロレント",
						"101 商業都市ボース",
						"102 海港都市ルーアン",
						"103 工房都市ツァイス",
						"104 王都グランセル",
						"105 陽だまりにて和む猫",
						"106 国境警備も楽じゃない",
						"107 王城",
						"108 グランアリーナ",
						"108bグランアリーナ(ノーイントロ)",
						"200 リベールの歩き方",
						"201 Secret Green Passage",
						"202 Rock on the Road",
						"300 闇を彷徨う",
						"301 行く手をはばむ鋼の床",
						"302 暗がりがくれた安らぎ",
						"303 四輪の塔",
						"304 レイストン要塞",
						"305 虚ろなる光の封土",
						"400 Sophisticated Fight -Quick Battle-",
						"401 Sophisticated Fight -Command Battle-",
						"402 To be Suggestive",
						"403 銀の意志",
						"404 Challenger Invited",
						"405 Ancient Makes",
						"406 至宝を守護せしモノ",
						"407 撃破！！",
						"408 消え行く星",
						"410 ピンチ！！",
						"500 星の在り処 Harmonica short Ver.",
						"501 琥珀の愛 Hum Ver.(日本語)",
						"501e琥珀の愛 Hum Ver.",
						"502 琥珀の愛 Piano Ver.",
						"502b琥珀の愛 Piano Ver.1.5",
						"503 琥珀の愛 Lute Ver.",
						"504 星の在り処 Harmonica long Ver.",
						"505 賑やかに行こう",
						"510 去り行く決意",
						"511 暗躍する者たち",
						"512 奴を逃がすな！",
						"513 胸の中に",
						"514 月明りの下で",
						"516 忍び寄る危機",
						"517 俺達カプア一家！",
						"518 旅立ちの小径",
						"519 奪還",
						"520 呪縛からの解放、そして・・・",
						"521 告白",
						"522 黒のオーブメント",
						"523 リベールの誇り",
						"530 組曲 白き花のマドリガル - 姫の悩み",
						"531 組曲 白き花のマドリガル - 騎士達の嘆き",
						"532 組曲 白き花のマドリガル - それぞれの思惑",
						"533 組曲 白き花のマドリガル - 城",
						"534 組曲 白き花のマドリガル - コロシアム",
						"535 組曲 白き花のマドリガル - 決闘",
						"536 組曲 白き花のマドリガル - 姫の死",
						"537 組曲 白き花のマドリガル - 大団円",
						""
					};
					char ti1_en[][100] = {
						"001 Dancing with the Wind",
						"100 Provincial City Rolent",
						"101 Commercial City Bose",
						"102 Port City Ruan",
						"103 Workshop City Zeiss",
						"104 Royal Capital Grancel",
						"105 Cat Relaxing in the Sun",
						"106 Border Patrol Isn't Easy",
						"107 Royal Castle",
						"108 Grand Arena",
						"108b Grand Arena (No Intro)",
						"200 Walking in Liberl",
						"201 Secret Green Passage",
						"202 Rock on the Road",
						"300 Wandering in the Darkness",
						"301 Steel Floor Blocking the Path",
						"302 Peace in the Darkness",
						"303 Tetracyclic Towers",
						"304 Leiston Fortress",
						"305 Hollow Land of Light",
						"400 Sophisticated Fight -Quick Battle-",
						"401 Sophisticated Fight -Command Battle-",
						"402 To be Suggestive",
						"403 Silver Will",
						"404 Challenger Invited",
						"405 Ancient Makes",
						"406 Guardian of the Treasure",
						"407 Crush!!",
						"408 Disappearing Star",
						"410 Pinch!!",
						"500 Where the Stars Are Harmonica short Ver.",
						"501 Amber Love Hum Ver.(Japanese)",
						"501e Amber Love Hum Ver.",
						"502 Amber Love Piano Ver.",
						"502b Amber Love Piano Ver.1.5",
						"503 Amber Love Lute Ver.",
						"504 Where the Stars Are Harmonica long Ver.",
						"505 Let's Go Lively",
						"510 Determination to Leave",
						"511 Those Who Move in the Shadows",
						"512 Don't Let Him Escape!",
						"513 In My Heart",
						"514 Under the Moonlight",
						"516 Creeping Crisis",
						"517 We're the Capua Family!",
						"518 Path of Departure",
						"519 Recapture",
						"520 Liberation from the Curse, and...",
						"521 Confession",
						"522 Black Ouroboros",
						"523 Pride of Liberl",
						"530 Suite Madrigal of the White Flower - Princess's Worry",
						"531 Suite Madrigal of the White Flower - Knights' Lament",
						"532 Suite Madrigal of the White Flower - Each One's Scheme",
						"533 Suite Madrigal of the White Flower - Castle",
						"534 Suite Madrigal of the White Flower - Colosseum",
						"535 Suite Madrigal of the White Flower - Duel",
						"536 Suite Madrigal of the White Flower - Princess's Death",
						"537 Suite Madrigal of the White Flower - Grand Finale",
						""
					};
					struct a {
						int start;
						int d1;
						int size;
						int d2;
						int loop1;
						int d3;
						int loop2;
						int d4;
					};
					//データ数は分からないので多く取っておく
					struct dd {
						int loop1;
						int loop2;
						char wav[6];
					};

					dd ddata[1000];

					a ldata = {};
					char data[33] = { 0 };
					char data0;
					int id = 0;

					CFile f;
					if (f.Open(fname, CFile::modeRead | CFile::shareDenyNone)) {
						//空の軌跡 The 1st
						//最初の8バイトは飛ばす
						f.Read(data, 8);
						for (;;) {
							//データ取得
							f.Read(data, 32);
							//ループデータにも入れる
							memcpy(&ldata, data, 32);
							//bgmで始まるまで。
							CStringA s;
							s = data;
							if (s.Left(3) == "bgm") break;
							//bgmで無い場合は、データとして保持
							ddata[id].loop1 = ldata.loop2;
							ddata[id].loop2 = ldata.loop1;
							id++;
							//16バイト飛ばす
							f.Read(data, 16);
						}
						f.SeekToBegin();
						f.Seek(0x770, 1);//現在位置から16バイト戻る
						ZeroMemory(data, 21);
						//bgmのファイル名は20文字
						//bgmファイル名取得
						CString a = L"";
						for (int i = 0; i < 59; i++) {
							f.Read(data, 20);
							CStringA s = data;
							if (s.Find("fmt") > 0) break;
							if (s.Find("F") > 0) break;
							if (s.Find("b.") > 0 || s.Find("e.") > 0) {
								f.Read(&data0, 1); }

							//10文字目から、ed6001.wav と入っているので、001だけ抜き出す
							CStringA s1 = s.Mid(12, 4); s1.Replace(".", ""); 
							a = CString(s1) + L" ";
							CString aa1a = L"";
							p.loop1 = ddata[i].loop1;
							p.loop2 = ddata[i].loop2;
							for (int j = 0;; j++) {
								CStringA s2 = ti1[j];
								if (s2 == "") { 
									a += LL2(L"不明", L"Unknown");
									break;
								}
								if (s2.Left(4).Trim() == s1) {
									a = CString(savedata.lang ? ti1_en[j] : ti1[j]).Mid(4);
									aa1a = CString(ti1[j]).Left(4).Trim();
									if (aa1a == L"501e") {
										if (ft == L"bgm1.pac") a += L"(English)";
										if (ft == L"bgm2.pac") a += L"(English)";
										if (ft == L"bgm3.pac") a += LL2(L"(日本語)", L"(Japanese)");
									}

									break;
								}
							}
							_tcscpy(p.name, a);
							_tcscpy(p.fol, fname + L"::" + aa1a + a);
							p.alb[0] = 0;
							p.art[0] = 0;
							if (ft == L"bgm1.pac") {
								wcscpy(p.art, LL2(L"steam版 空の軌跡 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac"));
								wcscpy(p.alb, LL2(L"BGM:標準", L"BGM:Standard"));
							}
							if (ft == L"bgm2.pac") {
								wcscpy(p.art, LL2(L"steam版 空の軌跡 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac"));
								wcscpy(p.alb, LL2(L"BGM:アレンジ", L"BGM:Arrange"));
							}
							if (ft == L"bgm3.pac") {
								wcscpy(p.art, LL2(L"steam版 空の軌跡 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac"));
								wcscpy(p.alb, LL2(L"BGM:オリジナル", L"BGM:Original"));
							}
							if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; syomode = 30; }
							Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
						}

						f.Close();
					}
				}
				else if (ft.Right(5) == ".opus") {
					p.sub = -6; p.loop1 = p.loop2 = 0;
					CString a = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					CString b = a.Mid(6, 1);
					int err;
					int fff = 0;
					//Ys X
					if (ft == L"y_act_e002.opus") {
						a = L"Operation SANDRAS";
						fff = 1;
					}
					if (ft == L"y_act_e002_s1.opus") {
						a = LL2(L"Operation SANDRAS(重低音)", L"Operation SANDRAS (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b100.opus") {
						a = L"Overblaze";
						fff = 1;
					}
					if (ft == L"y_b100_s1.opus") {
						a = LL2(L"Overblaze(重低音)", L"Overblaze (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b200.opus") {
						a = L"Through the North Wind";
						fff = 1;
					}
					if (ft == L"y_b200_s1.opus") {
						a = LL2(L"Through the North Wind(重低音)", L"Through the North Wind (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b210.opus") {
						a = LL2(L"高鳴る鼓動", L"Pounding Heartbeat");
						fff = 1;
					}
					if (ft == L"y_b210_s1.opus") {
						a = LL2(L"高鳴る鼓動(重低音)", L"Pounding Heartbeat (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b300.opus") {
						a = LL2(L"石火の如く", L"Like Flint");
						fff = 1;
					}
					if (ft == L"y_b300_s1.opus") {
						a = LL2(L"石火の如く(重低音)", L"Like Flint (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b400.opus") {
						a = L"Can You Do It";
						fff = 1;
					}
					if (ft == L"y_b400_s1.opus") {
						a = LL2(L"Can You Do It(重低音)", L"Can You Do It (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b500.opus") {
						a = LL2(L"BERSERK -戦斧の咆哮-", L"BERSERK -Roar of the Battle Axe-");
						fff = 1;
					}
					if (ft == L"y_b500_s1.opus") {
						a = LL2(L"BERSERK -戦斧の咆哮-(重低音)", L"BERSERK -Roar of the Battle Axe-(Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b510.opus") {
						a = LL2(L"悪意の洗礼", L"Baptism of Malice");
						fff = 1;
					}
					if (ft == L"y_b510_s1.opus") {
						a = LL2(L"悪意の洗礼(重低音)", L"Baptism of Malice (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b520.opus") {
						a = L"The Ultimate Pleasure in My Hands";
						fff = 1;
					}
					if (ft == L"y_b520_s1.opus") {
						a = LL2(L"The Ultimate Pleasure in My Hands(重低音)", L"The Ultimate Pleasure in My Hands (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b610.opus") {
						a = LL2(L"辿り着いた極光の下で", L"Under the Northern Lights");
						fff = 1;
					}
					if (ft == L"y_b610_s1.opus") {
						a = LL2(L"辿り着いた極光の下で(重低音)", L"Under the Northern Lights (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b620.opus") {
						a = L"Nordics Saga -The Endless Bloody Sea-";
						fff = 1;
					}
					if (ft == L"y_b620_s1.opus") {
						a = LL2(L"Nordics Saga -The Endless Bloody Sea-(重低音)", L"Nordics Saga -The Endless Bloody Sea- (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b700.opus") {
						a = L"Ready to Fire!";
						fff = 1;
					}
					if (ft == L"y_b700_s1.opus") {
						a = LL2(L"Ready to Fire!(重低音)", L"Ready to Fire! (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b710.opus") {
						a = L"Hello, Those Who Can't Die";
						fff = 1;
					}
					if (ft == L"y_b710_s1.opus") {
						a = LL2(L"Hello, Those Who Can't Die(重低音)", L"Hello, Those Who Can't Die (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_b720.opus") {
						a = L"Landing Warfare";
						fff = 1;
					}
					if (ft == L"y_b720_s1.opus") {
						a = LL2(L"Landing Warfare(重低音)", L"Landing Warfare (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_bgm_none.opus") {
						a = LL2(L"無音", L"Silence");
						fff = 1;
					}
					if (ft == L"y_d100.opus") {
						a = LL2(L"光届かぬその奥に", L"In the Depths Where Light Doesn't Reach");
						fff = 1;
					}
					if (ft == L"y_d100_s1.opus") {
						a = LL2(L"光届かぬその奥に(重低音)", L"In the Depths Where Light Doesn't Reach (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_d200.opus") {
						a = L"Eerie Stillness";
						fff = 1;
					}
					if (ft == L"y_d200_s1.opus") {
						a = LL2(L"Eerie Stillness(重低音)", L"Eerie Stillness (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_d400.opus") {
						a = LL2(L"飽くなき渇望", L"Insatiable Thirst");
						fff = 1;
					}
					if (ft == L"y_d400_s1.opus") {
						a = LL2(L"飽くなき渇望(重低音)", L"Insatiable Thirst (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_d410.opus") {
						a = L"The Inner Darkness";
						fff = 1;
					}
					if (ft == L"y_d410_s1.opus") {
						a = LL2(L"The Inner Darkness(重低音)", L"The Inner Darkness (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_d500.opus") {
						a = L"Hardhearted Rock Line";
						fff = 1;
					}
					if (ft == L"y_d500_s1.opus") {
						a = LL2(L"Hardhearted Rock Line(重低音)", L"Hardhearted Rock Line (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_d600.opus") {
						a = LL2(L"夢の痕跡", L"Dream Traces");
						fff = 1;
					}
					if (ft == L"y_d600_s1.opus") {
						a = LL2(L"夢の痕跡(重低音)", L"Dream Traces (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_d710.opus") {
						a = LL2(L"甲鉄戦艦ナグルファ", L"Ironclad Battleship Naglfar");
						fff = 1;
					}
					if (ft == L"y_d710_s1.opus") {
						a = LL2(L"甲鉄戦艦ナグルファ(重低音)", L"Ironclad Battleship Naglfar (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_d800.opus") {
						a = L"LILA -Innocent Wish-";
						fff = 1;
					}
					if (ft == L"y_d800_s1.opus") {
						a = LL2(L"LILA -Innocent Wish-(重低音)", L"LILA -Innocent Wish- (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_d900.opus") {
						a = LL2(L"エギル海底神殿", L"Egil Undersea Temple");
						fff = 1;
					}
					if (ft == L"y_d900_s1.opus") {
						a = LL2(L"エギル海底神殿(重低音)", L"Egil Undersea Temple (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_d1010.opus") {
						a = L"The Paradise Lost of Norman";
						fff = 1;
					}
					if (ft == L"y_d1010_s1.opus") {
						a = LL2(L"The Paradise Lost of Norman(重低音)", L"The Paradise Lost of Norman (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_e001.opus") {
						a = L"Yesterday's Journey, Tomorrow's Dream";
						fff = 1;
					}
					if (ft == L"y_e002.opus") {
						a = L"Surging Pressure";
						fff = 1;
					}
					if (ft == L"y_e003.opus") {
						a = L"Turn of the Tide";
						fff = 1;
					}
					if (ft == L"y_e004.opus") {
						a = LL2(L"あの時からずっと…", L"Ever Since That Day...");
						fff = 1;
					}
					if (ft == L"y_e005.opus") {
						a = L"Waver as the Wave";
						fff = 1;
					}
					if (ft == L"y_e006.opus") {
						a = LL2(L"切っても切れない絆", L"Unbreakable Bonds");
						fff = 1;
					}
					if (ft == L"y_e007.opus") {
						a = LL2(L"灰色の深層", L"Gray Depths");
						fff = 1;
					}
					if (ft == L"y_e007_s1.opus") {
						a = LL2(L"灰色の深層(重低音)", L"Gray Depths (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_e008.opus") {
						a = L"Premonition of Turmoil";
						fff = 1;
					}
					if (ft == L"y_e009.opus") {
						a = LL2(L"歪な願望", L"Twisted Desire");
						fff = 1;
					}
					if (ft == L"y_e010.opus") {
						a = L"The Road so Far, the Future Ahead";
						fff = 1;
					}
					if (ft == L"y_e011.opus") {
						a = L"Violent Warriors";
						fff = 1;
					}
					if (ft == L"y_e011_s1.opus") {
						a = LL2(L"Violent Warriors(重低音)", L"Violent Warriors (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_e012.opus") {
						a = LL2(L"手筈通りに", L"As Planned");
						fff = 1;
					}
					if (ft == L"y_e013.opus") {
						a = LL2(L"不明", L"Unknown");
						fff = 1;
					}
					if (ft == L"y_e014.opus") {
						a = L"ROLLO -Because of Its Purity-";
						fff = 1;
					}
					if (ft == L"y_e015.opus") {
						a = L"Deep Unconscious";
						fff = 1;
					}
					if (ft == L"y_e015_s1.opus") {
						a = LL2(L"Deep Unconscious(重低音)", L"Deep Unconscious (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f100.opus") {
						a = L"TO BE FREE";
						fff = 1;
					}
					if (ft == L"y_f100_s1.opus") {
						a = LL2(L"TO BE FREE(重低音)", L"TO BE FREE (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f110.opus") {
						a = L"Brother's Footsteps on the Island";
						fff = 1;
					}
					if (ft == L"y_f110_s1.opus") {
						a = LL2(L"Brother's Footsteps on the Island(重低音)", L"Brother's Footsteps on the Island (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f120.opus") {
						a = L"Burn with You";
						fff = 1;
					}
					if (ft == L"y_f120_s1.opus") {
						a = LL2(L"Burn with You(重低音)", L"Burn with You (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f130.opus") {
						a = L"Destined to Keep Running";
						fff = 1;
					}
					if (ft == L"y_f130_s1.opus") {
						a = LL2(L"Destined to Keep Running(重低音)", L"Destined to Keep Running (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f140.opus") {
						a = L"Ride on Mana!";
						fff = 1;
					}
					if (ft == L"y_f140_s1.opus") {
						a = LL2(L"Ride on Mana!(重低音)", L"Ride on Mana! (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f150.opus") {
						a = L"Heat Hazard";
						fff = 1;
					}
					if (ft == L"y_f150_s1.opus") {
						a = LL2(L"Heat Hazard(重低音)", L"Heat Hazard (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f160.opus") {
						a = LL2(L"瞳の中の少年剣士", L"Young Swordsman in My Eyes");
						fff = 1;
					}
					if (ft == L"y_f160_s1.opus") {
						a = LL2(L"瞳の中の少年剣士(重低音)", L"Young Swordsman in My Eyes (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f200.opus") {
						a = LL2(L"錨を揚げろ！", L"Weigh Anchor!");
						fff = 1;
					}
					if (ft == L"y_f200_s1.opus") {
						a = LL2(L"錨を揚げろ！(重低音)", L"Weigh Anchor! (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f210.opus") {
						a = LL2(L"悠き海に生きる者", L"Those Who Live in the Vast Sea");
						fff = 1;
					}
					if (ft == L"y_f210_s1.opus") {
						a = LL2(L"悠き海に生きる者(重低音)", L"Those Who Live in the Vast Sea (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f220.opus") {
						a = LL2(L"コンパスは踊る", L"The Compass Dances");
						fff = 1;
					}
					if (ft == L"y_f220_s1.opus") {
						a = LL2(L"コンパスは踊る(重低音)", L"The Compass Dances (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f230.opus") {
						a = LL2(L"開闢の海", L"Sea of Genesis");
						fff = 1;
					}
					if (ft == L"y_f230_s1.opus") {
						a = LL2(L"開闢の海(重低音)", L"Sea of Genesis (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_f310.opus") {
						a = L"If I Could Go Back to Those Days";
						fff = 1;
					}
					if (ft == L"y_f310_s1.opus") {
						a = LL2(L"If I Could Go Back to Those Days(重低音)", L"If I Could Go Back to Those Days (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_gameover.opus") {
						a = L"SO MUCH FOR TODAY (Ys X Ver.)";
						fff = 1;
					}
					if (ft == L"y_op.opus") {
						a = L"Facing the Distant Horizon";
						fff = 1;
					}
					if (ft == L"y_op_lp.opus") {
						a = L"Facing the Distant Horizon(lp)";
						fff = 1;
					}
					if (ft == L"y_t100.opus") {
						a = L"Our Hometown";
						fff = 1;
					}
					if (ft == L"y_t100_s1.opus") {
						a = LL2(L"Our Hometown(重低音)", L"Our Hometown (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_t200.opus") {
						a = LL2(L"根ざすべき場所", L"Where We Belong");
						fff = 1;
					}
					if (ft == L"y_t200_s1.opus") {
						a = LL2(L"根ざすべき場所(重低音)", L"Where We Belong (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_t300.opus") {
						a = L"Sometime Siesta";
						fff = 1;
					}
					if (ft == L"y_t300_s1.opus") {
						a = LL2(L"Sometime Siesta(重低音)", L"Sometime Siesta (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_t301.opus") {
						a = L"Innermost Feelings";
						fff = 1;
					}
					if (ft == L"y_t301_s1.opus") {
						a = LL2(L"Innermost Feelings(重低音)", L"Innermost Feelings (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_t500.opus") {
						a = LL2(L"情景に揺蕩う", L"Drifting in the Scene");
						fff = 1;
					}
					if (ft == L"y_t500_s1.opus") {
						a = LL2(L"情景に揺蕩う(重低音)", L"Drifting in the Scene (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_t600.opus") {
						a = LL2(L"盾の兄弟", L"Shield Brothers");
						fff = 1;
					}
					if (ft == L"y_t600_s1.opus") {
						a = LL2(L"盾の兄弟(重低音)", L"Shield Brothers (Bass Boost)");
						fff = 1;
					}
					if (ft == L"y_title.opus") {
						a = LL2(L"その優しさは誰のため", L"For Whom Is That Kindness");
						fff = 1;
					}

					if (fff == 0)
					if (a.Left(2) == "y9") {
						if (a.Mid(4, 4) = "b001") { a = "FEEL FORCE"; }
						if (a.Mid(4, 4) = "b002") { a = "TROUBLEMAKER"; }
						if (a.Mid(4, 4) = "b003") { a = "MONSTRUM SPECTRUM"; }
						if (a.Mid(4, 4) = "b004") { a = "LACRIMA CRISIS"; }
						if (a.Mid(4, 4) = "b005") { a = "WELCOME TO CHAOS"; }
						if (a.Mid(4, 4) = "b006") { a = "JUDGEMENT TIME"; }
						if (a.Mid(4, 4) = "b007") { a = "KNOCK ON NOX"; }
						if (a.Mid(4, 4) = "b008") { a = "ANIMA ERGASTULUM"; }
						if (a.Mid(4, 5) = "b010b") { a = "URBAN TERROR"; }
						if (a.Mid(4, 4) = "b010") { a = LL2(L"URBAN TERROR(イントロあり)", L"URBAN TERROR (With Intro)"); }
						if (a.Mid(4, 5) = "b011b") { a = "DREAMING IN THE GRIMWALD"; }
						if (a.Mid(4, 4) = "b011") { a = LL2(L"DREAMING IN THE GRIMWALD(イントロあり)", L"DREAMING IN THE GRIMWALD(with intro)"); }
						if (a.Mid(4, 4) = "b012") { a = "WILD CARD"; }
						if (a.Mid(4, 5) = "b014b") { a = "FULL MOON CEREMONY"; }
						if (a.Mid(4, 4) = "b014") { a = LL2(L"FULL MOON CEREMONY(イントロあり)", L"FULL MOON CEREMONY (With Intro)"); }
						if (a.Mid(4, 4) = "d101") { a = "HEART BEAT SHAKER"; }
						if (a.Mid(4, 4) = "d201") { a = "CLOACA MAXIMA"; }
						if (a.Mid(4, 4) = "d301") { a = "RUIN OF DRY MOAT"; }
						if (a.Mid(4, 4) = "d401") { a = "MARIONETTE, MARIONETTE"; }
						if (a.Mid(4, 4) = "d501") { a = "THE CAVE OF GROAN"; }
						if (a.Mid(4, 4) = "d601") { a = "EVAN MACHA"; }
						if (a.Mid(4, 4) = "d701") { a = "A QUARRY RUIN"; }
						if (a.Mid(4, 4) = "d702") { a = "CROSSING A/A"; }
						if (a.Mid(4, 4) = "d801") { a = "CATCH ME IF YOU CAN"; }
						if (a.Mid(4, 4) = "d901") { a = "ALCHEMY LAB"; }
						if (a.Mid(4, 4) = "d911") { a = "STRATEGIC ZONE"; }
						if (a.Mid(4, 5) = "d1001") { a = "FORTRESS UNDERGROUND"; }
						if (a.Mid(4, 5) = "d2001") { a = "DANCE WITH TRAPS"; }
						if (a.Mid(4, 4) = "e001") { a = "APRILIS"; }
						if (a.Mid(4, 4) = "e002") { a = "TAKE IT EASY!"; }
						if (a.Mid(4, 4) = "e003") { a = "PETITE FLEUR"; }
						if (a.Mid(4, 4) = "e004") { a = "EYES ON..."; }
						if (a.Mid(4, 4) = "e005") { a = "FORGOTTEN DAYS"; }
						if (a.Mid(4, 4) = "e006") { a = "PRISON OF BALDUQ -LIVE THE FUTURE-"; }
						if (a.Mid(4, 4) = "e007") { a = "PRISON OF BALDUQ -YEARNING-"; }
						if (a.Mid(4, 4) = "e008") { a = L"IL ÉTAIT UNE FOIS"; }
						if (a.Mid(4, 4) = "e009") { a = "WHO KNOWS THE TRUTH?"; }
						if (a.Mid(4, 4) = "e010") { a = "DECISION"; }
						if (a.Mid(4, 4) = "e011") { a = "STAGNANT POOL"; }
						if (a.Mid(4, 4) = "e013") { a = "INQUISITION"; }
						if (a.Mid(4, 4) = "e014") { a = "SILLY MEETING"; }
						if (a.Mid(4, 4) = "e016") { a = "MONSTRUM NOX"; }
						if (a.Mid(4, 4) = "e017") { a = "CHALLENGER'S ROAD"; }
						if (a.Mid(4, 4) = "e018") { a = "RED MULETA"; }
						if (a.Mid(4, 4) = "e019") { a = "NAB THE TAIL"; }
						if (a.Mid(4, 4) = "e020") { a = "THUS SPOKE AN ALCHEMIST"; }
						if (a.Mid(4, 4) = "e023") { a = "DENOUEMENT"; }
						if (a.Mid(4, 4) = "e024") { a = "INVITATION TO THE CRIMSON NIGHT"; }
						if (a.Mid(4, 4) = "f101") { a = "NORSE WIND"; }
						if (a.Mid(4, 4) = "f201") { a = "TRANQUIL SILENCE"; }
						if (a.Mid(4, 4) = "f301") { a = "GLESSING WAY!"; }
						if (a.Mid(4, 4) = "f501") { a = "DESERT AFTER TEARS"; }
						if (a.Mid(4, 4) = "muon") { a = LL2(L"無音", L"Silence"); }
						if (a.Mid(4, 4) = "t101") { a = "PRISONCITY"; }
						if (a.Mid(4, 4) = "t102") { a = "IN PROFILE, ON BELFRY"; }
						if (a.Mid(4, 4) = "t103") { a = "NEW LIFE"; }
						if (a.Mid(4, 4) = "t104") { a = "GRIA RECOLLECTION"; }
						if (a.Mid(4, 4) = "t201") { a = "BAR \"DANDELION\""; }
						if (a.Mid(4, 4) = "t301") { a = "AMBIGUOUS TERRITORY"; }
						if (a.Mid(4, 4) = "t402") { a = "WALTZ FOR GRACE"; }
						if (a.Mid(4, 4) = "t501") { a = "HEAT AND SPLENDOR"; }
						if (a.Mid(4, 4) = "t901") { a = "ONLY THE CORPSE GOES OUT"; }
						if (a.Mid(4, 4) = "t902") { a = "A GOLDEN KEY CAN OPEN ANY DOOR"; }
						if (a.Mid(4, 4) = "tbox") { a = "TREASURE BOX -Ys IX-"; }
					}
					else {
						switch (_ttoi(a.Mid(2, 5))) {
						case 81004:
							a = LL2(L"罪と罰と偽りと", L"Sin, Punishment and Falsehood");
							break;
						case 81005:
							a = LL2(L"昏き鐘の残響", L"Resonance of the Dark Bell");
							break;
						case 81006:
							a = "Right on the Mark";
							break;
						case 81007:
							a = LL2(L"悪夢ふたたび", L"Nightmare Again");
							break;
						case 81008:
							a = "Crossbell Nostalgia";
							break;
						case 81009:
							a = LL2(L"創まりの円庭", L"Garden of Beginnings");
							break;
						case 81010:
							a = "Mysterious Element";
							break;
						case 81012:
							a = "Stand Up Again and Again!";
							break;
						case 81014:
							a = "Purgatory Scream";
							break;
						case 81015:
							a = LL2(L"さざめきの途路", L"Path of Tumult");
							break;
						case 81016:
							a = LL2(L"蒼の大地に生きる者", L"Those Who Live on the Azure Land");
							break;
						case 81017:
							a = LL2(L"黎明の鐘", L"Bell of Dawn");
							break;
						case 81018:
							a = LL2(L"レメディファンタジア -仲間とともに-", L"Remedi Fantasia -With Comrades-");
							break;
						case 81019:
							a = "Slight Suspicion";
							break;
						case 81020:
							a = "Maliciousness in the Mirror";
							break;
						case 81021:
							a = LL2(L"暗澹たる世界", L"Dark World");
							break;
						case 81022:
							a = LL2(L"ひとときの温もり", L"Brief Warmth");
							break;
						case 81023:
							a = LL2(L"今、創まりのとき", L"Now, the Moment of Creation");
							break;
						case 81024:
							a = "KERAUNOS -Fear and Hatred-";
							break;
						case 81025:
							a = LL2(L"亡失われた魂", L"Lost Souls");
							break;
						case 81026:
							a = LL2(L"穏やかな時間", L"Peaceful Time");
							break;
						case 81027:
							break;
						case 81028:
							a = LL2(L"運命という名の歯車", L"Gears of Fate");
							break;
						case 81200:
							a = "Crossing Causal Lines";
							break;
						case 81201:
							a = "Glittering Mirage";
							break;
						case 81202:
							a = "Like a Whirlwind";
							break;
						case 81203:
							a = "Hide and Seek by Myself";
							break;
						case 81315:
							a = LL2(L"鉱山町マインツ -創Ver.-", L"Mines Town Mainz -Reverie Ver.-");
							break;
						case 81316:
							a = LL2(L"木霊の道 -創Ver.-", L"Path of Echoes -Reverie Ver.-");
							break;
						case 81317:
							a = "Raindrops with the Wind";
							break;
						case 81319:
							a = LL2(L"陽溜まりにただいまを", L"Home in the Sunshine");
							break;
						case 81320:
							a = "Wind-Up Yesterday!";
							break;
						case 81321:
							a = LL2(L"零の邂逅", L"Zero Encounter");
							break;
						case 81322:
							a = LL2(L"影の見えざる手", L"Invisible Hand in the Shadows");
							break;
						case 81950:
							break;
						case 81951:
							break;
						case 81952:
							break;
						case 81953:
							break;
						case 81954:
							break;
						case 81955:
							break;
						case 81956:
							break;
						case 81957:
							break;
						case 81958:
							break;
						case 81961:
							break;
						case 81962:
							break;
						case 81963:
							break;
						case 81964:
							break;
						case 81965:
							break;
						case 81966:
							break;
						case 81967:
							break;
						case 81968:
							break;
						case 81969:
							break;
						case 82065:
							a = LL2(L"鋼鉄牙城", L"Iron Fortress");
							break;
						case 82113:
							a = "Zero Break Battle";
							break;
						case 82114:
							a = "Stake Everything Strategy";
							break;
						case 82123:
							break;
						case 82124:
							a = "POM's Paradise";
							break;
						case 82125:
							a = LL2(L"波間に弾む心", L"Heart Bouncing on the Waves");
							break;
						case 82129:
							a = "Reverse Babel";
							break;
						case 82131:
							a = "Aim a Gun at the Bullet";
							break;
						case 82133:
							a = "Section G.F.S. II";
							break;
						case 82135:
							a = "Magical Revolt";
							break;
						case 82136:
							a = LL2(L"流麗闘冴", L"Elegant Battle");
							break;
						case 82137:
							a = "The Road to All-Out War";
							break;
						case 82138:
							a = "LAPIS";
							break;
						case 82140:
							a = "Invisible Hilly Country";
							break;
						case 82141:
							a = LL2(L"ひとかけらの光明", L"Sliver of Light");
							break;
						case 82143:
							a = LL2(L"反攻の烽火", L"Beacon of Counterattack");
							break;
						case 82147:
							a = "Rapid Wind";
							break;
						case 82148:
							a = "NO END NO WORLD -Instrumental Ver.-";
							break;
						case 82150:
							a = "Be Caught Up!";
							break;
						case 82151:
							a = "Breeding Innumerable Arms";
							break;
						case 82152:
							a = "The Destination of FATE";
							break;
						case 82154:
							a = "Twinkle Attack";
							break;
						case 82157:
							a = "Sword of Swords";
							break;
						case 82158:
							a = LL2(L"今宵は宴と参りましょう", L"Tonight We Feast");
							break;
						case 82159:
							a = "Flash Your Fighting Spirit";
							break;
						case 82161:
							a = LL2(L"鈍色に這う", L"Crawling in Gray");
							break;
						case 82163:
							a = "Pyro Labyrinth";
							break;
						case 82164:
							a = LL2(L"優しさを未来に託して", L"Entrust Kindness to the Future");
							break;
						case 82166:
							a = LL2(L"高らかに、誇らしく", L"Loud and Proud");
							break;
						case 82170:
							a = "Infinity Rage";
							break;
						case 82171:
							a = "Heavy Violent Match";
							break;
						case 82173:
							a = "Roar of Evil Spirits";
							break;
						case 82174:
							a = "Bad Dream Invasion";
							break;
						case 82175:
							a = "Golden Fever";
							break;
						case 82177:
							a = "The Perfect Steel of ZERO";
							break;
						case 82178:
							a = "Twilight Hermitage";
							break;
						case 82179:
							a = "Something Luxury...?";
							break;
						case 82183:
							a = "Challenger Invigorated";
							break;
						case 82184:
							a = LL2(L"このあと美味しくいただきました", L"Then We Ate Deliciously");
							break;
						case 82186:
							a = "Emergency Order";
							break;
						case 82188:
							a = LL2(L"激烈! 撃滅! ミシュナイダー!!", L"Fierce! Crush! Mishnayder!!");
							break;
						case 82189:
							a = "Life Goes On";
							break;
						default:
							if (a == L"ed8_inf_ex.opus") {
								a = LL2(L"夢幻の彼方へ", L"To the Realm of Dreams");
							}
						}
						switch (_ttoi(a.Mid(2, 4))) {
						case 8001:
							a = LL2(L"特科クラス《VII組》", L"Class VII");
							break;
						case 8002:
							a = LL2(L"スタートライン", L"Start Line");
							break;
						case 8003:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8004:
							a = "Youthful Victory";
							break;
						case 8006:
							a = LL2(L"ただひたすらに、前へ", L"Ever Forward");
							break;
						case 8007:
							a = LL2(L"縁 -つなぐもの-", L"Fate -Connecting-");
							break;
						case 8102:
							a = LL2(L"翡翠の公都バリアハート", L"Jade Capital Bareahard");
							break;
						case 8104:
							a = LL2(L"黒銀の鋼都ルーレ", L"Iron City Roer");
							break;
						case 8150:
							a = LL2(L"下校途中にパンケーキ", L"Pancakes on the Way Home");
							break;
						case 8151:
							a = LL2(L"可能性は無限大", L"Infinite Possibilities");
							break;
						case 8152:
							a = LL2(L"夜のしじまに", L"In the Night Silence");
							break;
						case 8153:
							a = LL2(L"夕景", L"Evening Scene");
							break;
						case 8154:
							a = LL2(L"新しい朝", L"New Morning");
							break;
						case 8155:
							a = LL2(L"束の間の里帰り", L"Brief Homecoming");
							break;
						case 8156:
							a = LL2(L"白亜の旧都セントアーク", L"White City St. Ark");
							break;
						case 8157:
							a = LL2(L"紡績町パルム", L"Spinning Town Parm");
							break;
						case 8158:
							a = LL2(L"籠の中のクロスベル", L"Crossbell in a Cage");
							break;
						case 8159:
							a = LL2(L"今、成すべきこと", L"What Must Be Done Now");
							break;
						case 8160:
							a = LL2(L"歓楽都市ラクウェル", L"Pleasure City Raquel");
							break;
						case 8161:
							a = LL2(L"静かなる駆け引き", L"Quiet Maneuvering");
							break;
						case 8162:
							a = LL2(L"赫奕たるヘイムダル", L"Splendid Heimdallr");
							break;
						case 8163:
							a = LL2(L"紺碧の海都オルディス", L"Azure Port City Ordys");
							break;
						case 8164:
							a = LL2(L"最前線都市", L"Front-line City");
							break;
						case 8165:
							a = "Base Camp";
							break;
						case 8166:
							a = LL2(L"精強なる兵たち", L"Elite Soldiers");
							break;
						case 8168:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8170:
							a = LL2(L"隠れ里エリン", L"Hidden Village Erin");
							break;
						case 8171:
							a = LL2(L"潜入調査", L"Infiltration");
							break;
						case 8172:
							a = LL2(L"昏冥の中で", L"In the Darkness");
							break;
						case 8173:
							a = LL2(L"紅き閃影 -光まとう翼-", L"Crimson Flash -Wings of Light-");
							break;
						case 8174:
							a = LL2(L"聖ウルスラ医科大学 -閃Ver.-", L"St. Ursula Medical College -CS Ver.-");
							break;
						case 8175:
							a = LL2(L"一抹の不安、一縷の望み", L"Hint of Unease, Ray of Hope");
							break;
						case 8176:
							a = "Lyrical Amber";
							break;
						case 8177:
							a = LL2(L"水面を渡る風", L"Wind Over the Water");
							break;
						case 8250:
							a = LL2(L"流れる雲の彼方に", L"Beyond the Drifting Clouds");
							break;
						case 8251:
							a = LL2(L"静寂の小路", L"Path of Silence");
							break;
						case 8252:
							a = LL2(L"崖谷の狭間", L"Gap of the Cliff");
							break;
						case 8253:
							a = "Weathering Road";
							break;
						case 8260:
							a = LL2(L"彼の地へ向かって", L"Toward That Land");
							break;
						case 8261:
							a = LL2(L"終焉の途へ", L"Toward the End");
							break;
						case 8262:
							a = LL2(L"全てを識るもの -閃Ver.-", L"Omniscient -CS Ver.-");
							break;
						case 8263:
							a = LL2(L"たそがれ緑道", L"Twilight Green Path");
							break;
						case 8311:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8350:
							a = LL2(L"アインヘル小要塞", L"Einhel Fortress");
							break;
						case 8351:
							a = LL2(L"伝承の裏で", L"Behind the Legend");
							break;
						case 8352:
							a = "Unplanned Residue";
							break;
						case 8353:
							a = LL2(L"忘れられし幻夢の狭間 -閃Ver.-", L"Forgotten Phantasmal Gap -CS Ver.-");
							break;
						case 8354:
							a = LL2(L"幽世の気配", L"Atmosphere of the Netherworld");
							break;
						case 8355:
							a = "solid as the Rock of JUNO";
							break;
						case 8356:
							a = LL2(L"地下に巣喰う", L"Nesting Underground");
							break;
						case 8359:
							a = "Spiral of Erebos";
							break;
						case 8360:
							a = LL2(L"鋼の障壁", L"Steel Barrier");
							break;
						case 8363:
							a = "Break In";
							break;
						case 8365:
							a = LL2(L"サングラール迷宮", L"Sanglar Maze");
							break;
						case 8366:
							a = LL2(L"静けき森の魔女", L"Witch of the Silent Forest");
							break;
						case 8367:
							a = LL2(L"Mystic Core -閃Ver.-", L"Mystic Core -CS Ver.-");
							break;
						case 8368:
							a = LL2(L"斉いし舞台", L"Unified Stage");
							break;
						case 8369:
							a = LL2(L"シンクロニシティ #23", L"Synchronicity #23");
							break;
						case 8371:
							a = LL2(L"世界の命運を賭けて", L"Betting on the World's Fate");
							break;
						case 8372:
							a = "The End of -SAGA-";
							break;
						case 8429:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8450:
							a = "Brave Steel";
							break;
						case 8451:
							a = "Toughness!!";
							break;
						case 8452:
							a = LL2(L"剣戟怒涛", L"Sword and Lance Storm");
							break;
						case 8453:
							a = "Proud Grudge";
							break;
						case 8454:
							a = LL2(L"チープ・トラップ", L"Cheap Trap");
							break;
						case 8455:
							a = "STEP AHEAD";
							break;
						case 8456:
							a = LL2(L"劣勢を挽回せよ！", L"Turn the Tide!");
							break;
						case 8457:
							a = "Abrupt Visitor";
							break;
						case 8458:
							a = LL2(L"行き着く先 -Opening Size-", L"Destination -Opening Size-");
							break;
						case 8460:
							a = "Lift-off!";
							break;
						case 8461:
							a = "Accursed Tycoon";
							break;
						case 8464:
							a = "One-Way to the Netherworld";
							break;
						case 8465:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8466:
							a = "Erosion of Madness";
							break;
						case 8467:
							a = "DOOMSDAY TRANCE";
							break;
						case 8468:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8472:
							a = "Malicious Fiend";
							break;
						case 8473:
							a = "Unlikely Combination";
							break;
						case 8474:
							a = "Robust One";
							break;
						case 8475:
							a = LL2(L"古の盟約", L"Ancient Covenant");
							break;
						case 8476:
							a = LL2(L"七の相克 -EXCELLION KRIEG-", L"Seven Antagonisms -EXCELLION KRIEG-");
							break;
						case 8477:
							a = "Burning Throb";
							break;
						case 8478:
							a = "Neck or Nothing";
							break;
						case 8479:
							a = "Majestic Roar";
							break;
						case 8480:
							a = "With Our Own Hands!!";
							break;
						case 8500:
							a = LL2(L"授業は合同で", L"Joint Class");
							break;
						case 8501:
							a = "Power or Technique";
							break;
						case 8502:
							a = "Briefing Time";
							break;
						case 8503:
							a = LL2(L"第II分校の日常", L"Daily Life at Branch II");
							break;
						case 8504:
							a = LL2(L"充実したひととき", L"Satisfying Moment");
							break;
						case 8505:
							a = LL2(L"異端の研究者", L"Heretic Researcher");
							break;
						case 8506:
							a = LL2(L"君に伝えたいこと", L"What I Want to Tell You");
							break;
						case 8507:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8508:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8509:
							a = LL2(L"張り詰めた思惑", L"Tense Speculation");
							break;
						case 8510:
							a = LL2(L"混迷の対立", L"Chaotic Conflict");
							break;
						case 8511:
							a = LL2(L"急転直下", L"Sudden Turn");
							break;
						case 8512:
							a = LL2(L"蠢く陰謀", L"Writhing Conspiracy");
							break;
						case 8513:
							a = LL2(L"託されたもの", L"Entrusted One");
							break;
						case 8514:
							a = LL2(L"羅刹の薫陶", L"Rasetsu's Guidance");
							break;
						case 8515:
							a = LL2(L"ハーメル -遺されたもの-", L"Hamel -What Was Left Behind-");
							break;
						case 8516:
							a = LL2(L"Welcome Back! アーベントタイム(ラジオ)", L"Welcome Back! Evening Time(radio)");
							break;
						case 8517:
							a = LL2(L"夏至祭", L"Summer Solstice Festival");
							break;
						case 8519:
							a = LL2(L"夏至祭", L"Summer Solstice Festival");
							break;
						case 8520:
							a = LL2(L"翡翠庭園", L"Jade Garden");
							break;
						case 8521:
							a = LL2(L"初めての円舞曲", L"First Waltz");
							break;
						case 8522:
							a = LL2(L"真打ち登場！", L"Headliner's Entrance!");
							break;
						case 8524:
							a = "Tragedy";
							break;
						case 8528:
							a = LL2(L"僅かな希望の先に", L"Beyond Slight Hope");
							break;
						case 8530:
							a = LL2(L"帰路へ", L"On the Road Home");
							break;
						case 8532:
							a = "Roots of Scar";
							break;
						case 8534:
							a = LL2(L"想い千里を走り", L"Feelings Run a Thousand Miles");
							break;
						case 8536:
							a = LL2(L"光射す空の下で", L"Under the Shining Sky");
							break;
						case 8539:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8541:
							if (b == L"b")
								a = LL2(L"空を見上げて -Eliot Ver.-", L"Look Up at the Sky -Eliot Ver.-");
							else
								a = LL2(L"空を見上げて -Eliot Ver.-", L"Look Up at the Sky -Eliot Ver.-");
							break;
						case 8542:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8543:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8544:
							a = "Little Rain";
							break;
						case 8545:
							a = LL2(L"暗雲", L"Dark Clouds");
							break;
						case 8546:
							a = LL2(L"鐘、鳴り響く時", L"When the Bell Tolls");
							break;
						case 8547:
							a = LL2(L"巨イナル黄昏", L"Giant Twilight");
							break;
						case 8548:
							a = LL2(L"あの日の約束", L"That Day's Promise");
							break;
						case 8551:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8553:
							a = "Sensitive Talk";
							break;
						case 8554:
							a = LL2(L"哀花", L"Mournful Flower");
							break;
						case 8555:
							a = "Feel at Home";
							break;
						case 8556:
							a = LL2(L"幾千万の夜を越えて", L"Beyond Countless Nights");
							break;
						case 8557:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8558:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8559:
							a = LL2(L"優しき微睡み", L"Gentle Slumber");
							break;
						case 8560:
							a = LL2(L"最悪の最善手", L"Best Move in the Worst Situation");
							break;
						case 8562:
							a = LL2(L"黒の真実", L"Black Truth");
							break;
						case 8563:
							a = LL2(L"いつでもそばに", L"Always by Your Side");
							break;
						case 8564:
							a = LL2(L"その温もりは小さいけれど。", L"That warmth is small, but.");
							break;
						case 8566:
							a = LL2(L"それでも前へ", L"Still Forward");
							break;
						case 8570:
							a = LL2(L"想いひとつに", L"Hearts as One");
							break;
						case 8571:
							a = LL2(L"千年要塞", L"Millennium Fortress");
							break;
						case 8572:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8573:
							a = LL2(L"せめてこの夜に誓って", L"At Least Swear Tonight");
							break;
						case 8574:
							a = "Constraint";
							break;
						case 8575:
							a = LL2(L"過ぎ去りし日々", L"Days Gone By");
							break;
						case 8576:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8577:
							a = LL2(L"それぞれの覚悟", L"Each One's Resolve");
							break;
						case 8578:
							a = LL2(L"無明の闇の中で", L"In the Darkness");
							break;
						case 8579:
							a = LL2(L"変わる世界 -闇の底から-", L"Changing World -From the Depths of Darkness-");
							break;
						case 8600:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8601:
							a = LL2(L"ゲートイン", L"Gate In");
							break;
						case 8602:
							a = LL2(L"不明(空の軌跡)", L"Unknown(Sky)");
							break;
						case 8603:
							a = LL2(L"女神はいつも見ています", L"The Goddess is Always Watching");
							break;
						case 8604:
							a = LL2(L"不明(空の軌跡)", L"Unknown(Sky)");
							break;
						case 8605:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8606:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8608:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8610:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8611:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8612:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8613:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8614:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8616:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8617:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8618:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8619:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8620:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8621:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8702:
							a = "Master's Vertex";
							break;
						case 8706:
							a = "Endure Grief";
							break;
						case 8707:
							a = "Intuition and Insight";
							break;
						case 8708:
							a = "Bold Assailants";
							break;
						case 8709:
							a = "Seductive Shudder";
							break;
						case 8711:
							a = "Blue Stardust";
							break;
						case 8713:
							a = "Pleasure Smile";
							break;
						case 8714:
							a = LL2(L"巨竜目覚める", L"The Great Dragon Awakens");
							break;
						case 8715:
							a = LL2(L"未来へ。", L"To the Future.");
							break;
						case 8716:
							a = LL2(L"明日への軌跡 -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-");
							break;
						case 8717:
							a = "Deep Carnival";
							break;
						case 8718:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8719:
							a = "Chain Chain Chain!";
							break;
						case 8720:
							a = LL2(L"明日への軌跡", L"Trails to Tomorrow");
							break;
						case 8721:
							a = LL2(L"愛の詩(歌)", L"Poem of Love(vocal)");
							break;
						case 8722:
							a = "Celestial Coalescence";
							break;
						case 8800:
							a = "Vantage Masters";
							break;
						case 8801:
							a = "Concept H.M.I.";
							break;
						case 8802:
							a = LL2(L"風よりも駿く", L"Swifter Than the Wind");
							break;
						case 8803:
							a = "Brilliant Escape";
							break;
						case 8810:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8811:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8812:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8910:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8911:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8912:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8913:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8916:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8917:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8918:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8919:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8920:
							a = LL2(L"不明", L"Unknown");
							break;
						case 8921:
							a = LL2(L"不明", L"Unknown");
							break;

						}
					}

					a = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					if (a.Left(3) == L"ed7") {
						int b = _ttoi(a.Mid(3, 3));
						CString fil = fname.Left(fname.ReverseFind(L'\\')) + L"\\..\\..\\data\\bgm\\info.yaml";
						FILE* fp;
						errno_t ferr;
						ferr = _tfopen_s(&fp, fil, _T("r, ccs=UTF-8"));
						if (ferr == 0) {
							CStdioFile fzero(fp);
							fzero.SeekToBegin();
							CString stf, stl, stn;
							BOOL ck = FALSE;
							for (;;) {
								if (fzero.ReadString(stf) == FALSE) break;
								stl.Format(L"'%d'", b);
								if (stf.Find(stl) != -1) {
									ck = TRUE;
								}
								if (stf.Find(L"jp:") != -1 && ck == TRUE) {
									int k = stf.Find(L"jp:") + 4;
									stn = stf.Mid(k);
									break;
								}
							}
							if (stn != L"") {
								a = stn;
							}
							fzero.Close();
							fclose(fp);
						}
					}
					_tcscpy(p.name, a);
					_tcscpy(p.fol, fname1);
				}
				else if (ft.Right(4) == ".mp3" || ft.Right(4) == ".MP3" || ft.Right(4) == ".mp2" || ft.Right(4) == ".MP2" ||
					ft.Right(4) == ".mp1" || ft.Right(4) == ".MP1" || ft.Right(4) == ".rmp" || ft.Right(4) == ".RMP") {
					p.sub = -10; p.loop1 = p.loop2 = 0;
					ft = ft2;
					_tcscpy(p.fol, fname1);
					CId3tagv1 ta1p;
					CId3tagv2 ta2p;
					int b = ta2p.Load(fname);
					ss = ta2p.GetArtist(); if (b == -1) { ta1p.Load(fname); ss = ta1p.GetArtist(); } _tcscpy(p.art, ss);
					ss = ta2p.GetTitle(); if (b == -1) ss = ta1p.GetTitle(); if (ss == "")ss = ft; _tcscpy(p.name, ss);
					ss = ta2p.GetAlbum(); if (b == -1) ss = ta1p.GetAlbum(); _tcscpy(p.alb, ss);
				}
				else if ((bufimage[0] == 0xff && (bufimage[1] & 0xf0 == 0xf0)) && (ft.Right(4) == ".aac" || ft.Right(4) == ".AAC")) {
					p.sub = -9;
					ft = ft2;
					_tcscpy(p.name, ft2);
					_tcscpy(p.fol, fname1);
				}
				else if ((ft.Right(4) == ".dsf" || ft.Right(4) == ".DSF" || ft.Right(4) == ".dff" || ft.Right(4) == ".DFF" || ft.Right(4) == ".wsd" || ft.Right(4) == ".WSD")) {
					CString tagfile, tagname, tagalbum;
					ULONGLONG po;
					ft = ft2;
					og->dsdload(fname,tagfile, tagname, tagalbum,po, 1);
					og->dsdclose();
					_tcscpy(p.name, tagfile);
					_tcscpy(p.alb, tagalbum);
					_tcscpy(p.art, tagname);
					_tcscpy(p.fol, fname1);
					p.sub = -7; p.loop1 = p.loop2 = 0;
				}
				else if ((ft.Right(4) == ".m4a" || ft.Right(4) == ".M4A" || ft.Right(4) == ".aac" || ft.Right(4) == ".AAC")) {
					ft = ft2;
					CFile ff;
					char buf[1024];
					TCHAR kpi[512];
					ff.Open(fname, CFile::modeRead | CFile::shareDenyNone, NULL);
					int flg, read = ff.Read(bufimage, sizeof(bufimage));
					ff.Close();
					kpi[0] = 0;
					plugs(s, &p, kpi, kvver);
					if (kpi[0] == 0)
						p.sub = -3;
					else
						p.sub = -2;
					if (savedata.m4a == 1)
						p.sub = -9;
					_tcscpy(p.name, ft);
					_tcscpy(p.fol, fname1);
					flg = 0;
					int i;
					for (i = 0; i < read - 4; i++) {
						if (bufimage[i] == 'u' && bufimage[i + 1] == 'd' && bufimage[i + 2] == 't' && bufimage[i + 3] == 'a') {
							int j;
							for (j = i + 4; j < read - 4; j++) {
								if (bufimage[j] == 'a' && bufimage[j + 1] == 'l' && bufimage[j + 2] == 'b' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
									j += 19;
									for (int k = j; k < read - 4; k++) {
										if (bufimage[k] == 0) {
											flg = 1;
											buf[k - j] = 0;
											buf[k - j + 1] = 0;
											buf[k - j + 2] = 0;
											break;
										}
										buf[k - j] = bufimage[k];
									}
								}
								if (flg == 1) {
									const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
									TCHAR* buff = new TCHAR[wlen + 1];
									if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
									{
										buff[wlen] = _T('\0');
									}
									wcscpy(p.alb, buff);
									delete[] buff;
									flg = 0;
									break;
								}
							}
							for (j = i + 4; j < read - 4; j++) {
								if (bufimage[j] == 'A' && bufimage[j + 1] == 'R' && bufimage[j + 2] == 'T' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
									j += 19;
									for (int k = j; k < read - 4; k++) {
										if (bufimage[k] == 0) {
											flg = 1;
											buf[k - j] = 0;
											buf[k - j + 1] = 0;
											buf[k - j + 2] = 0;
											break;
										}
										buf[k - j] = bufimage[k];
									}
								}
								if (flg == 1) {
									const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
									TCHAR* buff = new TCHAR[wlen + 1];
									if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
									{
										buff[wlen] = _T('\0');
									}
									wcscpy(p.art, buff);
									delete[] buff;
									flg = 0;
									break;
								}
							}
							for (j = i + 4; j < read - 4; j++) {
								if (bufimage[j] == 'n' && bufimage[j + 1] == 'a' && bufimage[j + 2] == 'm' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
									j += 19;
									for (int k = j; k < read - 4; k++) {
										if (bufimage[k] == 0) {
											flg = 1;
											buf[k - j] = 0;
											buf[k - j + 1] = 0;
											buf[k - j + 2] = 0;
											break;
									}
										buf[k - j] = bufimage[k];
									}
								}
								if (flg == 1) {
									const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
									TCHAR* buff = new TCHAR[wlen + 1];
									if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
									{
										buff[wlen] = _T('\0');
								}
									wcscpy(p.name, buff);
									delete[] buff;
									flg = 0;
									break;
							}
						}
					}
				}
						}
				else if ((ft.Right(5).MakeLower() == ".flac" || ft.Right(5) == ".FLAC" || ft.Right(7).MakeLower() == L".qull3h")) {
					ft = ft2;
					CFile ff;
					char buf[2024];
					TCHAR kpi[512];
					ff.Open(fname, CFile::modeRead | CFile::shareDenyNone, NULL);
					int flg, read = ff.Read(bufimage, sizeof(bufimage));
					ff.Close();
						if (bufimage[0] == 0xBF) {
							BYTE offenc[7] = { 0xd9,0x3F,0x86,0x7B,0xC7,0x61,0xaa };
							int off = 0;
							for (int ll = 0; ll < sizeof(bufimage); ll++) {
								bufimage[ll] ^= offenc[off];
								off++; off %= 7;
							}
						}
					kpi[0] = 0;
					plugs(s, &p, kpi, kvver);
					if (kpi[0] == 0)
						p.sub = -3;
					else
						p.sub = -2;
					//			if (savedata.m4a == 1)
					p.sub = -8;
					_tcscpy(p.name, ft);
					_tcscpy(p.fol, fname1);
					flg = 0;
					int i = 0, j;
					for (j = i; j < read - 6; j++) {
						if (bufimage[j] == 'A' && bufimage[j + 1] == 'L' && bufimage[j + 2] == 'B' && bufimage[j + 3] == 'U' && bufimage[j + 4] == 'M' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = 0;
							}
							wcscpy(p.alb, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 6; j++) {
						if ((bufimage[j] == 'A' || bufimage[j] == 'a') && bufimage[j + 1] == 'l' && bufimage[j + 2] == 'b' && bufimage[j + 3] == 'u' && bufimage[j + 4] == 'm' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = 0;
							}
							wcscpy(p.alb, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 6; j++) {
						if (bufimage[j] == 'A' && bufimage[j + 1] == 'R' && bufimage[j + 2] == 'T' && bufimage[j + 3] == 'I' && bufimage[j + 4] == 'S' && bufimage[j + 5] == 'T' && bufimage[j + 6] == '=') {
							j += 7;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.art, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 6; j++) {
						if ((bufimage[j] == 'A' || bufimage[j] == 'a') && bufimage[j + 1] == 'r' && bufimage[j + 2] == 't' && bufimage[j + 3] == 'i' && bufimage[j + 4] == 's' && bufimage[j + 5] == 't' && bufimage[j + 6] == '=') {
							j += 7;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
							}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.art, buff);
							delete[] buff;
							flg = 0;
							break;
							}
						}
					for (j = i; j < read - 4; j++) {
						if (bufimage[j] == 'T' && bufimage[j + 1] == 'I' && bufimage[j + 2] == 'T' && bufimage[j + 3] == 'L' && bufimage[j + 4] == 'E' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.name, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 4; j++) {
						if ((bufimage[j] == 'T' || bufimage[j] == 't') && bufimage[j + 1] == 'i' && bufimage[j + 2] == 't' && bufimage[j + 3] == 'l' && bufimage[j + 4] == 'e' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.name, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
				}
				else {
					p.sub = -2;
					_tcscpy(p.name, s);
					_tcscpy(p.fol, fname1);
					p.alb[0] = p.art[0] = NULL; p.loop1 = p.loop2 = 0;
					TCHAR kpi[512]; kpi[0] = 0;
					plugs(fname, &p, kpi, kvver);
					if (kpi[0]) {
						ft = fname.Left(fname.ReverseFind('.')); ft += ".m3u";
						char ftt[1024];
						WideCharToMultiByte(CP_ACP, 0, ft, -1, ftt, 2000, " ", FALSE);
						ss = fname.Right(4); ss.MakeLower();
						if (ss == ".kss") {
							FILE *f; if (f = fopen(ftt, "r")) {
								char buf[256];  int st, ed, tmp;
								for (;;) {
									if (fgets(buf, sizeof(buf), f) == NULL) break;
									//							if (f.Read(buf1, 250) == FALSE) break;
									if (buf[0] == '#' || buf[0] == '\r' || buf[0] == '\n') continue;
									ss = buf;
									st = ss.Find(',', 0); ed = ss.Find(',', st + 1); s = ss.Mid(st + 1, (ed - 1) - st);
									if (s.Left(1) == _T("$")) {
										int num = 0;
										CString s3 = s.Mid(1, 1);
										if (_T("0") <= s3 && _T("9") >= s3) num = s3.GetAt(0) - _T('0');
										if (_T("a") <= s3 && _T("f") >= s3) num = s3.GetAt(0) - _T('a') + 10;
										if (_T("A") <= s3 && _T("F") >= s3) num = s3.GetAt(0) - _T('A') + 10;
										s3 = s.Mid(2, 1); num *= 16;
										if (_T("0") <= s3 && _T("9") >= s3) num += s3.GetAt(0) - _T('0');
										if (_T("a") <= s3 && _T("f") >= s3) num += s3.GetAt(0) - _T('a') + 10;
										if (_T("A") <= s3 && _T("F") >= s3) num += s3.GetAt(0) - _T('A') + 10;
										ft.Format(_T("%s::%04d"), fname, num + 1);
									}
									else
										ft.Format(_T("%s::%04d"), fname, _tstoi(s) + 1);
									_tcscpy(p.fol, ft);
									//TCHAR ss1[2001];
									//MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
									//ss = ss1;
									st = ss.Find(L',', ed); ed = ss.Find(L',', st + 1); s = ss.Mid(st + 1, (ed - 1) - st);
									_tcscpy(p.name, s);
									if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
									Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
								}
								fclose(f);
								return;
							}
						}
						ft = fname.Left(fname.ReverseFind('.')); ft += ".frm";
						if (ss == ".nsf") {
							CStdioFile f; if (f.Open(ft, CFile::modeRead | CFile::typeText, NULL)) {
								TCHAR buf[256]; int st, ed, tmp;
								f.ReadString(buf, 256);
								f.ReadString(buf, 256);
								_tcscpy(p.alb, buf);
								f.ReadString(buf, 256);
								s = buf; int j = s.Find(_T("songs")); if (j >= 0) {
									int k = s.Find(_T("S.E."));
									int l = s.ReverseFind('('); ss = s.Mid(l + 1, 3); j = _tstoi(ss);
									if (k >= 0) { l = s.ReverseFind('&'); ss = s.Mid(l + 1, 3); j += _tstoi(ss); }
									for (l = 0; l < j; l++) {
										s = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
										ss.Format(_T("%s::%04d"), s, l + 1);
										_tcscpy(p.name, ss);
										ss.Format(_T("%s::%04d"), fname, l + 1);
										_tcscpy(p.fol, ss);
										if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
										Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
									}
								}
								f.Close();
								return;
							}
						}
						if (ss == ".gbs") {
							CFile f; if (f.Open(fname, CFile::modeRead, NULL)) {
								char buf[32];
								f.Read(buf, 16); int i = buf[4];
								f.Read(buf, 32);
#if UNICODE
								TCHAR ss1[512];
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
								_tcscpy(p.name, ss1);
#else
								_tcscpy(p.name, buf);
#endif
								f.Read(buf, 32);
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
								_tcscpy(p.alb, ss1);
#else
								_tcscpy(p.alb, buf);
#endif
								f.Read(buf, 32);
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
								_tcscpy(p.art, ss1);
#else
								_tcscpy(p.art, buf);
#endif
								f.Close();
								for (int j = 0; j < i; j++) {
									ss.Format(_T("%s::%04d"), fname, j + 1); _tcscpy(p.fol, ss);
									if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
									Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
								}
								return;
							}
						}
						if (ss == ".hes") {
							ft = fname1.Right(fname1.GetLength() - fname1.ReverseFind(L'\\') - 1);
							_tcscpy(p.name, ft);
							_tcscpy(p.fol, fname1);
							_tchdir(fname);
							CString ftt0 = ft;
							p.alb[0] = p.art[0] = NULL; p.loop1 = p.loop2 = 0;
							TCHAR kpi[512]; kpi[0] = 0;
							plugs(fname, &p, kpi,kvver);
							if (kpi[0]) {
								ft = fname.Left(fname.ReverseFind('.')); ft += ".m3u";
								char ftt[1024];
								WideCharToMultiByte(CP_ACP, 0, ft, -1, ftt, 2000, " ", FALSE);
								ft = fname1.Right(fname1.GetLength() - fname1.ReverseFind(L'\\') - 1);
								ss = fname.Right(4); ss.MakeLower();
								if (ss == L".hes") {
									FILE *f; if (f = fopen(ftt, "r")) {
										char buf[256];  int st, ed;
										for (;;) {
											if (fgets(buf, sizeof(buf), f) == NULL) break;
											if (buf[0] == _T('#') || buf[0] == _T('\r') || buf[0] == _T('\n')) continue;
											ss = buf;
											int z = 0;
											ss.Replace(L"\n", L"");
											ss.Replace(L"\r", L"");

											if ((z = ss.Find(',', 0)) != -1) {
												s = ss.Mid(z + 1);
												if (s.Left(1) == _T("$")) {
													int num = 0;
													CString s3 = s.Mid(1, 1);
													if (_T("0") <= s3 && _T("9") >= s3) num = s3.GetAt(0) - _T('0');
													if (_T("a") <= s3 && _T("f") >= s3) num = s3.GetAt(0) - _T('a') + 10;
													if (_T("A") <= s3 && _T("F") >= s3) num = s3.GetAt(0) - _T('A') + 10;
													s3 = s.Mid(2, 1); num *= 16;
													if (_T("0") <= s3 && _T("9") >= s3) num += s3.GetAt(0) - _T('0');
													if (_T("a") <= s3 && _T("f") >= s3) num += s3.GetAt(0) - _T('a') + 10;
													if (_T("A") <= s3 && _T("F") >= s3) num += s3.GetAt(0) - _T('A') + 10;
													ftt0.Format(_T("%s::%04d"), fname, num + 1);
												}
												else
													ftt0.Format(_T("%s::%04d"), fname, _tstoi(s) + 1);
												_tcscpy(p.fol, ftt0);
												st = ss.Find(L',', z + 1); ed = ss.Find(L',', st + 1); s = ss.Mid(st + 1, (ed - 1) - st);
												_tcscpy(p.name, s);
												p.sub = -3;
												if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
												Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
											}
											else {
												s = fname1.Left(fname1.ReverseFind(L'\\')+1);
												ftt0.Format(_T("%s%s"), s,ss);
												_tcscpy(p.fol, ftt0);
												ftt0.Format(_T("%s"), ss);
												_tcscpy(p.name, ftt0);
												p.sub = -10;
												if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
												Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
											}
										}
										fclose(f);
									}
									else {
										ft = ftt0;
										for (int i = 1; i < 255; i++) {
											ftt0.Format(_T("%s::%04d"), fname, i + 1);
											_tcscpy(p.fol, ftt0);
											ftt0.Format(_T("%s::%04d"), ft, i + 1);
											_tcscpy(p.name, ftt0);
											if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
											Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
										}
									}
									return;
								}
								return;
							}
							return;
						}
						if (ss == ".ovi" || ss == ".opi" || ss == ".ozi") {
							CFile f; char buf[512], *buf2;
							f.Open(fname, CFile::modeRead | CFile::shareDenyRead, NULL);
							if (f.GetLength() > 512)
								f.Seek(-512, CFile::end);
							else
								f.SeekToBegin();
							f.Read(buf, 512);
							int i = 0;
							f.Close();
							for (; i < 500; i++) {
								if (buf[i] == 'F'&&buf[i + 1] == 'M'&&buf[i + 2] == 'C') break;
							}
							if (i != 500) {
								buf2 = buf + i + 4; ss = buf2;
								int st = ss.Find(0x0d, 0);
								ft = ss.Left(st); _tcscpy(p.name, ft);
								int ed = ss.Find(0x0d, st + 2);
								ft = ss.Mid(st + 1, ed - st - 1); _tcscpy(p.art, ft);
								st = ss.Find(0x0d, ed + 2);
								ft = ss.Mid(ed + 1, st - ed - 1); _tcscpy(p.alb, ft);
								if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub; fnn = p.name; }
								Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
								return;
							}
						}
						ss = fname.Right(2); ss.MakeLower();
						ft = fname.Right(3); ft.MakeLower();
						if (ss == ".m" || ft == ".mz") {
							CFile ff; char buf[512], *buf2;
							ff.Open(fname, CFile::modeRead | CFile::shareDenyNone, NULL);
							if (ff.GetLength() > 512)
								ff.Seek(-512, CFile::end);
							else
								ff.SeekToBegin();
							ff.Read(buf, 512);
							int jj = ff.GetLength(); if (jj > 510) jj = 510;
							jj -= 3;
							int i;
							for (i = jj; i > 0; i--) {
								if (buf[i] == 0 && (buf[i + 1] == 0 || (BYTE)buf[i + 1] == 255) && buf[i + 2] == 0)break;
							}
							ff.Close();
							if (i != 0) {
								buf2 = buf + i + 3;
								int j = 0;
								for (;; j++)if (buf2[j] == 0)break;
#if UNICODE
								TCHAR ss1[512];
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								_tcscpy(p.name, ss1); buf2 += j;
#else
								_tcscpy(p.name, buf2); buf2 += j;
#endif
								for (j = 0;; j++)if (buf2[j] != 0)break;
								buf2 += j;
								for (j = 0;; j++)if (buf2[j] == 0)break;
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								_tcscpy(p.art, ss1); buf2 += j;
#else
								_tcscpy(p.art, buf2); buf2 += j;
#endif
								for (j = 0;; j++)if (buf2[j] != 0)break;
								buf2 += j;
								for (j = 0;; j++)if (buf2[j] == 0)break;
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								_tcscpy(p.alb, ss1); buf2 += j;
#else
								_tcscpy(p.alb, buf2); buf2 += j;
#endif
								if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub; fnn = p.name; }
								Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
							}
							return;
						}
						ft = fname.Right(4); ft.MakeLower();
						if (ft == ".tta") {
							CFile ff; char buf[512], *buf2;
							ff.Open(fname, CFile::modeRead | CFile::shareDenyNone, NULL);
							if (ff.GetLength() > 0x80)
								ff.Seek(-0x80, CFile::end);
							else
								ff.SeekToBegin();
							ff.Read(buf, 0x80);
							int i = 0;
							for (; i < 0x80; i++) {
								if (buf[i + 0] == 'T'&&buf[i + 1] == 'A'&&buf[i + 2] == 'G')break;
							}
							ff.Close();
							if (i != 0x80) {
								buf2 = buf + i + 3;
#if UNICODE
								TCHAR ss1[512];
								TCHAR buf3 = buf2[30]; buf2[30] = 0;
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								buf2[30] = buf3;
								_tcscpy(p.name, ss1); buf2 += 30;
#else
								_tcscpy(p.name, buf2); buf2 += 30;
#endif
#if UNICODE
								buf3 = buf2[30]; buf2[30] = 0;
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								buf2[30] = buf3;
								_tcscpy(p.art, ss1); buf2 += 30;
#else
								_tcscpy(p.art, buf2); buf2 += 30;
#endif
#if UNICODE
								buf3 = buf2[30]; buf2[30] = 0;
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								buf2[30] = buf3;
								_tcscpy(p.alb, ss1); buf2 += 30;
#else
								_tcscpy(p.alb, buf2); buf2 += 30;
#endif
								if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub; fnn = s; }
								Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
							}
							return;
						}
					}

				}
				CString sL = s;
				s.MakeLower();
				if (s.Right(4) == ".png" || s.Right(4) == ".url" || s.Right(4) == ".jpg" || s.Right(4) == ".bmp" || s.Right(4) == ".cue" || s.Right(4) == ".iso" || s.Right(4) == ".bin" || s.Right(4) == ".img" || s.Right(4) == ".mds" || s.Right(4) == ".mdf" || s.Right(4) == ".ccd" || s.Right(4) == ".sub" || s.Right(4) == ".pdf" || s.Right(4) == ".com" || s.Right(4) == ".exe" || s.Right(4) == ".dll" || s.Right(4) == ".bat" || s.Right(4) == ".reg" || s.Right(4) == ".msi" || s.Right(4) == ".nfo" || s.Right(4) == ".diz" || s.Right(4) == ".gif" || s.Right(4) == ".ico" ||
					s.Right(4) == ".lrc" || s.Right(4) == ".zip" || s.Right(4) == ".lzh" || s.Right(4) == ".cab" || s.Right(4) == ".rar" || s.Right(4) == ".txt" || s.Right(4) == ".doc" || s.Right(4) == "html" || s.Right(4) == ".htm" || s.Right(4) == ".ini" || s.Right(4) == ".xml" || s.Right(4) == ".kar" || s.Right(4) == ".hed" || s.Right(4) == ".mzi" || s.Right(4) == ".mag" || s.Right(4) == ".mvi" || s.Right(4) == ".lvi" || s.Right(4) == ".mpi" || s.Right(4) == ".pvi" || s.Right(4) == ".pzi" || s.Right(4) == ".p86" || s.Right(4) == ".mml" || s.Right(4) == ".m3u" || s.Right(4) == ".frm" || s.Right(7) == ".psflib" || s.Right(8) == ".psf2lib" || s.Right(7) == ".usflib" || s.Right(7) == ".2sflib" || s.Right(3) == ".gb" || s.Right(7) == ".gsflib" || s.Right(4) == ".pdx") {
				}
				else {
					if (syo == 0) {
						syo = 1; syos = p.fol; modesub = p.sub; fnn = sL;
					}
					CString fol = p.fol;
					if (PathIsDirectory(fol)) {
						fol += L"\\" + sL;
					}
					

					Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, fol, 0, 0);
					fol = p.fol;
					if (PathIsDirectory(fname_full) == FALSE) {
						return;
					}
				}
			}
		}
	}
	f.Close();
	fname = fname1;
	int cdd=0;
	if(PathIsDirectory(fname)){
		CFileFind cf1;
		if (cf1.FindFile(_T("*.*")) != 0) {
			int r = 1;
			for (; r;) {
				r = cf1.FindNextFile();
				CString ss, sss;
				ss = cf1.GetFileName();
				sss = cf1.GetFilePath();
				if (!(ss == L"." || ss == L"..")) {
					if ((cf1.IsHidden() == 0)) {
						if (cf1.IsDirectory() != 0) { //フォルダ？
							Fol(cf1.GetFilePath());//*/fname+cf1.GetFileName();
						}
					}
				}
			}
		}
		cf1.Close();
	}
	else {

	}
}



void CPlayList::plugs(CString fff, playlistdata *p,TCHAR* kpi, BYTE& kv)
{
	CString ss,ft;
	int flg=0;
	for(int i=0;i<kpicnt;i++){
		for(int j=0;;j++){
			if(ext[i][j]=="") break;
			ss=fff.Right(fff.GetLength()-fff.ReverseFind('.'));ss.MakeLower();
			if(ext[i][j]==ss){
				ss=kpif[i];
				if (kpichk[i] == 1) {
					flg = 1;
					kv = kvar[i][j];
					break;
				}
			}
		}
		if(flg==1)break;
	}
	if(flg==1){
		_tcscpy(p->fol,fff);
		p->sub=-3;
		ft=fff.Right(fff.GetLength()-fff.ReverseFind('\\')-1);
		_tcscpy(p->name,ft);
		p->alb[0]=NULL;p->art[0]=NULL;p->loop1=p->loop2=p->ret2=0;
		_tcscpy(kpi,ss);
	}
}

void CPlayList::Save()
{
	TCHAR tmp[1024];int cnt,j;CString s;
	int cx,cy,x,y;RECT r;
	int c;
	_tgetcwd(tmp,1000);
	_tchdir(karento2);
	if(IsIconic()){
		ShowWindow(SW_RESTORE);
		GetWindowRect(&r);
		ShowWindow(SW_MINIMIZE);
	}else
		GetWindowRect(&r);
	x=r.left;y=r.top;cx=r.right-x;cy=r.bottom-y;
#if _UNICODE
	int lcnt = savedata.playlistnum;
	CString s0;
	if (lcnt == 0)
		s0.Format(L"playlistu.dat");
	else
		s0.Format(L"playlistu%d.dat", lcnt);
	CFile f;if(f.Open(s0,CFile::modeCreate|CFile::modeWrite,NULL)==TRUE){
#else
	CFile f;if(f.Open(_T("playlist.dat"),CFile::modeCreate|CFile::modeWrite,NULL)==TRUE){
#endif
		cnt=playcnt;
		f.Write(&cnt,4);
		f.Write(&x,4);
		f.Write(&y,4);
		f.Write(&cx,4);
		f.Write(&cy,4);
		c=m_lc.GetColumnWidth(0);f.Write(&c,4);
		c=m_lc.GetColumnWidth(1);f.Write(&c,4);
		c=m_lc.GetColumnWidth(3);f.Write(&c,4);
		c=m_lc.GetColumnWidth(4);f.Write(&c,4);
		c=m_lc.GetColumnWidth(7);f.Write(&c,4);
		playlistdata pld;
		for(int i=0;i<cnt;i++){ZeroMemory(&pld,sizeof(pld));
			_tcscpy(pld.alb,pc[i].alb);
			_tcscpy(pld.art,pc[i].art);
			_tcscpy(pld.fol,pc[i].fol);
			_tcscpy(pld.name,pc[i].name);
			pld.loop1=pc[i].loop1;
			pld.loop2=pc[i].loop2;
			pld.sub=pc[i].sub;
			pld.ret2=pc[i].ret2;
			pld.time=pc[i].time;
			f.Write(&pld,sizeof(pld));
		}
		c=m_loop.GetCheck();f.Write(&c,4);
		c=m_renzoku.GetCheck();f.Write(&c,4);
		c=m_tool.GetCheck();f.Write(&c,4);
		c=m_saisyo.GetCheck();f.Write(&c,4);
		c=m_lc.GetColumnWidth(2);f.Write(&c,4);
		c=m_lc.GetColumnWidth(5);f.Write(&c,4);
		f.Write(&pnt,4);
		f.Close();

	}
	_tchdir(tmp);
}


void CPlayList::Load()
{
	TCHAR tmp[1024];int cnt;
	int cx,cy,x=-10000,y,c;
	_tgetcwd(tmp,1000);
	_tchdir(karento2);
#if _UNICODE
	int lcnt = savedata.playlistnum;
	CString s;
	if (lcnt == 0)
		s.Format(L"playlistu.dat");
	else
		s.Format(L"playlistu%d.dat", lcnt);
	CFile f;if(f.Open(s,CFile::modeRead,NULL)==TRUE){
#else
	CFile f;if(f.Open(_T("playlist.dat"),CFile::modeRead,NULL)==TRUE){
#endif
		f.Read(&cnt,4);
		pc = (playlistdata0*)malloc(sizeof(playlistdata0) * (cnt + 1));
		f.Read(&x,4);
		f.Read(&y,4);
		f.Read(&cx,4);
		f.Read(&cy,4);
		f.Read(&c,4);m_lc.SetColumnWidth(0,c);
		f.Read(&c,4);m_lc.SetColumnWidth(1,c);
		f.Read(&c,4);m_lc.SetColumnWidth(3,c);
		f.Read(&c,4);m_lc.SetColumnWidth(4,c);
		f.Read(&c,4);
		playlistdata pld;
		m_lc.SetItemCount(cnt);
		for(int i=0;i<cnt;i++){
			f.Read(&pld,sizeof(pld));
			Add(pld.name,pld.sub,pld.loop1,pld.loop2,pld.art,pld.alb,pld.fol,pld.ret2,pld.time,FALSE,FALSE);			
		}
		c=0;f.Read(&c,4);m_loop.SetCheck(c);
		c=0;f.Read(&c,4);m_renzoku.SetCheck(c);
		c=1;f.Read(&c,4);m_tool.SetCheck(c);
		c=1;f.Read(&c,4);m_saisyo.SetCheck(c);
		c=-1;f.Read(&c,4);if(c!=-1)m_lc.SetColumnWidth(2,c);
		c=-1;f.Read(&c,4);if(c!=-1)m_lc.SetColumnWidth(5,c);
		pnt1=-1;f.Read(&pnt1,4);//if(c!=-1)SIcon(pnt);
		f.Close();
	}
	_tchdir(tmp);
	if(GetAsyncKeyState(VK_LCONTROL)&0x8000){
		x=-10000;
	}
	if(x!=-10000){
		MoveWindow(x,y,cx,cy,TRUE);
		RECT r;
		GetClientRect(&r);
	m_lc.SetWindowPos(&wndNoTopMost,0,0,(int)(r.right-20*(hD2)),(int)(r.bottom-80 * (hD2 )),SWP_NOMOVE|SWP_NOOWNERZORDER|SWP_NOZORDER);
	}
}
int SC=0;
void CPlayList::SIcon(int i){
	if(i<0) return;
	if(i>=playcnt) return;
	if(i==pnt) return;
	RECT r;
	pc[i].icon=0; if(pnt>=0&&pnt<playcnt){ pc[pnt].icon=1;
			m_lc.GetItemRect(pnt,&r,LVIR_ICON);
			m_lc.RedrawWindow(&r);
	}
	pnt=i;
	m_lc.GetItemRect(pnt,&r,LVIR_ICON);
	m_lc.RedrawWindow(&r);
	m_lc.EnsureVisible(i,FALSE);
	SC=0;
}

void CPlayList::SIconTimer(int i){
	CString s; s.Format(L"%d", i);
	if(pnt<0) return;
	if(pnt>=playcnt) return;
	if(IsBadReadPtr(&pc[pnt],sizeof(playlistdata0))) return;
	//_try{
	if(i==0)
		pc[pnt].icon=2;
	else
		pc[pnt].icon=0;
	//}__except(EXCEPTION_EXECUTE_HANDLER){}
	RECT r;
	m_lc.GetItemRect(pnt,&r,LVIR_ICON);
	m_lc.RedrawWindow(&r);
}
int pln=0;
extern int ps;
extern void DoEvent();
extern int gameon;
void CPlayList::OnNMDblclkList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	*pResult = 0;
	CString s;int i,j;
	int Lindex=-1;
	Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);i=Lindex;
	if(Lindex>=playcnt) return;
	if(Lindex==-1) return;
	//SIcon(i);
	fnn=pc[Lindex].name;
	filen=pc[Lindex].fol;
	modesub=pc[Lindex].sub;
	loop1=pc[Lindex].loop1;
	loop2=pc[Lindex].loop2;
	ret2=pc[Lindex].ret2;
	plcnt=i;
	gameon = 0;
	if(pln==0){
		pln=1;
		og->OnRestart();
//		for(;ps==1;){
//			DoEvent();
//			og->OnRestart();
//		}
		pln=0;
	}
}
extern CDouga *pMainFrame1;
extern long height, width;
int ip1 = 0;
void CPlayList::OnSize(UINT nType, int cx, int cy)
{
	CCustomDialog::OnSize(nType, cx, cy);

	// TODO: ここにメッセージ ハンドラ コードを追加します。
	RECT r;
	GetClientRect(&r);
	if( ::IsWindow( this->GetSafeHwnd()) == TRUE &&  this->IsWindowVisible() == TRUE)
		m_lc.SetWindowPos(&wndNoTopMost, 0, 0, (int)(r.right - 20 * (hD2 )), (int)(r.bottom - 80 * (hD2 )), SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
	if(pl){
		if (nType == SIZE_MINIMIZED){
			if(m_saisyo.GetCheck())
				og->ShowWindow(SW_MINIMIZE);
			if(pMainFrame1){
				pMainFrame1->ShowWindow(SW_HIDE);
			}
			if (playbase)
				playbase->ShowWindow(SW_MINIMIZE);
		}
		if(nType== SIZE_RESTORED){
			if (ogpl == 1) {
				ogpl = 0;
//				return;
			}
			if(m_saisyo.GetCheck())
				og->ShowWindow(SW_RESTORE);
			if(pMainFrame1 && height!=0){
				pMainFrame1->ShowWindow(SW_SHOWNORMAL);
			}
			if (playbase)
				playbase->ShowWindow(SW_RESTORE);
	//		ip1 = 0;
//			SetTimer(4923, 100, NULL);
		}
	}
}
int kk=0;
extern int lenl;
int tlg=0;

extern int aaaa,aaaa1;
extern CPlayList*pl;
void timerpl(UINT nIDEvent,CPlayList* pl);
void timerpl1(UINT nIDEvent,CPlayList* pl);
void timerpl1(UINT nIDEvent,CPlayList* pl)
{
	if (nIDEvent == 4927) {
		pl->KillTimer(4927);
		if (ip1 > 0) {
			 return;
		}
		if (playbase)
				::SetWindowPos(playbase->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			::SetWindowPos(pl->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			pl->SetTimer(4930, 10, NULL);
			ip1 = 3;
	}
	if (nIDEvent == 4924) {
		pl->KillTimer(4924);
		if (ip1 <= 0) return;
		if (playbase)
			::SetWindowPos(playbase->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		::SetWindowPos(pl->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		pl->SetTimer(4930, 10, NULL);
		ip1 = 3;
	}
	if (nIDEvent == 4930) {
		ip1--;
		if (ip1 <= 0) {
			ip1 = 0;
			aaaa = 0;
			pl->KillTimer(4930);
		}
	}
	if(nIDEvent==5000){
		pl->KillTimer(5000);
		pl->SIcon(pl->pnt1);
	}
	if(nIDEvent==40){
		pl->KillTimer(40);
		plw=1;
	}

	if(nIDEvent==3000){
		pl->SIconTimer(SC);
		SC++; SC = SC % 2;
	}
	if(nIDEvent==20){
		
		if(pl->w_flg==FALSE) return;
		if(pl->GetFocus()==NULL){return;}
		if(pl->m_find.GetFocus()->m_hWnd==pl->m_find.m_hWnd){return;}
		{
			HWND rtn;
			TCHAR Name[1024];
			long Leng = sizeof(Name);
			rtn = GetActiveWindow();
			GetWindowTextW(rtn, Name, Leng);
			CString sss;
			sss = Name;
			if (sss != _T("プレイリスト")) {
				return;
			}
			if((GetKeyState(VK_RETURN)&0x8000)==0 && kk==1)
				kk=0;
			if(GetKeyState(VK_RETURN)&0x8000 && kk==0){
				kk=1;
				CString s;int i,j;
				int Lindex=-1;
				Lindex=pl->m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				i=Lindex;
				if(Lindex>=playcnt) return;
				if(Lindex==-1) return;
//				pl->SIcon(i);
				fnn=pl->pc[Lindex].name;
				filen=pl->pc[Lindex].fol;
				modesub=pl->pc[Lindex].sub;
				loop1=pl->pc[Lindex].loop1;
				loop2=pl->pc[Lindex].loop2;
				ret2=pl->pc[Lindex].ret2;
				plcnt=i;
				og->OnRestart();
			}
			if((GetKeyState(VK_CONTROL)&0x8000) && (GetKeyState('A')&0x8000)){
				int i=pl->m_lc.GetItemCount();
				for(int j=0;j<i;j++){
					pl->m_lc.SetItemState(j,LVIS_SELECTED,LVIS_SELECTED);
				}
			}
		}
		int tl=pl->m_tool.GetCheck();
		if(tl!=tlg){
			tlg=tl;
			if(tlg){
				pl->m_lc.EnableToolTips(TRUE);
				tl=pl->m_lc.GetExtendedStyle();
				tl = tl & ~LVS_EX_INFOTIP;
				pl->m_lc.SetExtendedStyle(tl);
			}else{
				pl->m_lc.EnableToolTips(FALSE);
				tl=pl->m_lc.GetExtendedStyle();
				tl |=LVS_EX_INFOTIP;
				pl->m_lc.SetExtendedStyle(tl);
			}
		}
	}
}
void timerpl(UINT nIDEvent,CPlayList* pl)
{
	try{
		_set_se_translator( trans_func1 );
		timerpl1(nIDEvent,pl);
//	}__except(EXCEPTION_EXECUTE_HANDLER){}
	}catch(SE_Exception1 e){
	}
	catch(_EXCEPTION_POINTERS *ep){
	}
	catch(...){}
}

extern int stflg;

#if WIN64
void CPlayList::OnTimer(UINT_PTR nIDEvent) 
#else
void CPlayList::OnTimer(UINT nIDEvent) 
#endif
{
	savedata.saveloop = m_loop.GetCheck();
	savedata.saverenzoku = m_renzoku.GetCheck();
	savedata.savecheck=m_savecheck.GetCheck();
	savedata.savecheck_mp3 = m_save_mp3.GetCheck();
	savedata.savecheck_dshow = m_save_kpi.GetCheck();
	CPlayList* pl = (CPlayList*)this;
	if(stflg == FALSE)
		timerpl(nIDEvent,pl);
	CCustomDialog::OnTimer(nIDEvent);
}

void CPlayList::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	CCustomDialog::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CPlayList::OnBnClickedCheck4()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	Save();
}

void CPlayList::OnBnClickedCheck1()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	Save();
}

void CPlayList::OnLvnBegindragList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNM = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	POINT ptPos,ptPos2;
    HIMAGELIST hOneImageList;
    HIMAGELIST hTempImageList;
	IMAGEINFO imf;
	long iHeight;
	m_hDragImage = ListView_CreateDragImage(m_lc.m_hWnd,pNM->iItem,&ptPos);
	ImageList_GetImageInfo(m_hDragImage, 0, &imf);
	iHeight = imf.rcImage.bottom;
	for(int Lindex=-1;;){
		Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);//pNM->iItem
		if(Lindex==-1) break;
		if(pNM->iItem==Lindex){
		}else{
            hOneImageList= ListView_CreateDragImage(m_lc.m_hWnd,Lindex,&ptPos2);
            hTempImageList = ImageList_Merge(m_hDragImage, 
                             0, hOneImageList, 0, 0, iHeight);
            ImageList_Destroy(m_hDragImage);
            ImageList_Destroy(hOneImageList);
            m_hDragImage = hTempImageList;
            ImageList_GetImageInfo(m_hDragImage, 0, &imf);
            iHeight = imf.rcImage.bottom;		}
	}
 	// ドラッグ開始
	POINT ptCursor;
	GetCursorPos(&ptCursor);
	m_lc.ScreenToClient(&ptCursor);

	long lX = ptCursor.x- ptPos.x;
	long lY = ptCursor.y- ptPos.y;

	ImageList_BeginDrag(m_hDragImage,0,lX,lY);
	ImageList_DragEnter(m_hWnd,0,0);
	SetCapture();


	*pResult = 0;
}

void CPlayList::OnDrag(int x,int y)
{
	POINT Point={x,y};
	ClientToScreen(&Point);
	RECT Rect;
	GetWindowRect(&Rect);
	ImageList_DragMove(Point.x-Rect.left,Point.y-Rect.top);
	{
		CPoint  point,point2;CRect rect;
		GetCursorPos(&point);
		ScreenToClient(&point);
		m_lc.GetWindowRect(&rect);
		point2.y=rect.top; point2.x=rect.left;
		ScreenToClient(&point2);
		point-=point2;
		int hItem = m_lc.HitTest(point ,NULL);
	}
}

void CPlayList::OnEndDrag()
{
	// ドラッグ終了
	ImageList_DragLeave(m_hWnd);
	ImageList_EndDrag();
	ImageList_Destroy(m_hDragImage);
	m_hDragImage = NULL;

	// カーソル表示
	ShowCursor(TRUE);
}
void CPlayList::OnMouseMove(UINT nFlags, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	if(GetCapture()==this){
		OnDrag(point.x,point.y);
	}
	CCustomDialog::OnMouseMove(nFlags, point);
}

void CPlayList::OnLButtonUp(UINT nFlags, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	if(GetCapture()==this){
		OnEndDrag();
		// キャプチャ解除
		ReleaseCapture();
		//実際の移動のための座標割りだし
		CPoint  point,point2;CRect rect;
		GetCursorPos(&point);
		ScreenToClient(&point);
		m_lc.GetWindowRect(&rect);
		point2.y=rect.top; point2.x=rect.left;
		ScreenToClient(&point2);
		point-=point2;
		int hItem = m_lc.HitTest(point,NULL);
		if( hItem != -1){
			playlistdata *p;int cnt=0,j,cnt2=0,*cn;
			int Lindex = -1, Lindexx;
			for(;;){
				Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				if(Lindex==-1) break;
				cnt++;
			}
			//転送データをあらかじめ作っておく
			p = (playlistdata*)malloc(sizeof(playlistdata)*cnt);
			for(Lindexx=-1;;cnt2++){
				Lindexx=m_lc.GetNextItem(Lindexx,LVNI_ALL |LVNI_SELECTED);
				if(Lindexx==-1) break;
			}
			//転送するインテックス番号を獲得する
			int cn1;
			cn =(int*)malloc(sizeof(int)*cnt2);
			for(cn1=0,Lindexx=-1;;cn1++){
				Lindexx=m_lc.GetNextItem(Lindexx,LVNI_ALL |LVNI_SELECTED);
				if(Lindexx==-1) break;
				cn[cn1]=Lindexx;
			}
			CString s;
			for(cnt=0,Lindex=-1;;cnt++){
				Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				if(Lindex==-1) break;
				_tcscpy(p[cnt].name,pc[Lindex].name);
				_tcscpy(p[cnt].fol,pc[Lindex].fol);
				p[cnt].sub=pc[Lindex].sub;
				p[cnt].ret2=pc[Lindex].ret2;
			}
			for(Lindex=-1;;){
				playlistdata pp;
				Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				if(Lindex==-1) break;
				_tcscpy(pp.name,pc[hItem].name);
				_tcscpy(pp.fol,pc[hItem].fol);
				pp.sub=pc[hItem].sub;
				pp.ret2=pc[hItem].ret2;
				int cnt1 = 0;
				for(;cnt1<cnt;cnt1++){
					if(_tcscmp(p[cnt1].name,pp.name)==0 && _tcscmp(p[cnt1].fol,pp.fol)==0 && p[cnt1].sub==pp.sub && p[cnt1].ret2==pp.ret2){
						break;
					}
				}
				if(cnt1!=cnt) break;
				if(hItem<Lindex){//選択項目が下　ドロップ位置が上
					int k=Lindex-hItem;
					m_lc.SetItemState(Lindex,m_lc.GetItemState(Lindex,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
					for(int kk=0;kk<k;kk++){
						OnXCHG(Lindex-kk,Lindex-kk-1);
					}
					m_lc.SetItemState(hItem  ,m_lc.GetItemState(hItem,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
				}else{//選択項目が上　ドロップ位置が下
					int Lindexx = -1;
					for(;;){
						Lindexx=m_lc.GetNextItem(Lindexx,LVNI_ALL |LVNI_SELECTED);
						if(Lindexx==-1) break;
						m_lc.SetItemState(Lindexx,m_lc.GetItemState(Lindexx,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
					}
					int i;
					for(i=0;i<cn1;i++){
						int k=hItem-cn[i];
						for(int kk=0;kk<k;kk++){
								OnXCHG(cn[i]+kk+1,cn[i]+kk);
						}
						for(int j=0;j<cn1;j++) cn[j]--;
					}
					hItem-=cn1;
					for(i=0;i<cn1;i++){
						hItem++;
						m_lc.SetItemState(hItem  ,m_lc.GetItemState(hItem,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
					}
					break;
				}
				hItem++;
			}
			free(cn);
			free(p);
			m_lc.RedrawWindow();
			m_lDragTopItem=0;m_lDragTopItemt=0;
		 }
	}

	CCustomDialog::OnLButtonUp(nFlags, point);
}

void CPlayList::OnLvnGetdispinfoList1(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVDISPINFO* lpDInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);

	// 基本的なNULLチェックを行います
	if (lpDInfo == NULL) return;

	*pResult = 0; // 初期化

	try {
		_set_se_translator(trans_func1);

		// pcがまだ確保されていない、あるいは要素数が0の場合は何もせず安全に終了します
		// ※ m_nPcCount は pc配列の要素数を管理している変数に置き換えてください
		if (pc == NULL || playcnt <= 0) {
			return;
		}

		// 要求されたインデックスを取得します
		int nTargetIndex = lpDInfo->item.iItem;

		// インデックスが範囲外（個数オーバー）の場合の処理
		if (nTargetIndex < 0 || nTargetIndex >= playcnt) {
			// お嬢様のご指示通り、範囲外なら0番目を参照するようにします
			nTargetIndex = 0;
		}

		// テキスト情報の要求に対する処理
		if (lpDInfo->item.mask & LVIF_TEXT) {
			// 安全確保済みの nTargetIndex を使用して pc にアクセスします
			switch (lpDInfo->item.iSubItem) {
			case 0:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].name);
				break;
			case 1:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].game);
				break;
			case 2: {
				CString s;
				if (pc[nTargetIndex].time >= 3600)
					s.Format(_T("%d:%02d:%02d"), pc[nTargetIndex].time / 3600, (pc[nTargetIndex].time / 60) % 60, pc[nTargetIndex].time % 60);
				else
					s.Format(_T("%d:%02d"), pc[nTargetIndex].time / 60, pc[nTargetIndex].time % 60);

				if (pc[nTargetIndex].time == 0) s = "";
				if (pc[nTargetIndex].time == -1) s = LL2(L"取得不能", L"Unable to fetch");
				_tcscpy(lpDInfo->item.pszText, s);
			} break;
			case 3:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].art);
				break;
			case 4:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].alb);
				break;
			case 5:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].fol);
				break;
			default:
				break;
			}
		}

		// 画像情報の要求に対する処理
		if (lpDInfo->item.mask & LVIF_IMAGE) {
			// ここでも安全な nTargetIndex を使用します
			lpDInfo->item.iImage = pc[nTargetIndex].icon;
		}
	}
	catch (SE_Exception1 e) {
		// 例外発生時の処理
	}
	catch (_EXCEPTION_POINTERS* ep) {
		// 例外発生時の処理
	}
	catch (...) {
		// その他の例外
	}
}
void CPlayList::OnNMRclickList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	*pResult = 0;

	CPoint point;
	CRect rect;
	GetCursorPos(&point);

	CMenu menu;
	VERIFY(menu.LoadMenu(CG_IDR_POPUP_LIST));

	CMenu* pPopup = menu.GetSubMenu(0);
	ASSERT(pPopup != NULL);
	CWnd* pWndPopupOwner = this;

	while (pWndPopupOwner->GetStyle() & WS_CHILD)
		pWndPopupOwner = pWndPopupOwner->GetParent();

	pPopup->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y,
		pWndPopupOwner);

}

void CPlayList::OnList()
{
	int Lindex=-1;
	Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
	CListSyosai *a = new CListSyosai(CWnd::FromHandle(GetSafeHwnd()));
	w_flg=FALSE;
	CWnd::PostMessage(0x118);
	memcpy(&a->pc,&pc[Lindex],sizeof(playlistdata0));
	a->DoModal();
	w_flg=TRUE;
	delete a;
}
#define ID_HOTKEY0 8000
#define ID_HOTKEY1 8001
#define ID_HOTKEY2 8002
#define ID_HOTKEY3 8003
void CPlayList::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CCustomDialog::OnActivate(nState, pWndOther, bMinimized);
	int l = 5;
	if(plw){
		if ((nState == WA_ACTIVE || nState == WA_CLICKACTIVE) && bMinimized == 0 && pl->m_saisyo.GetCheck()) {
			l = 20;
			ogpl = 1;
			og->ShowWindow(SW_RESTORE);
		}
	}
	if (nState == WA_ACTIVE || nState == WA_CLICKACTIVE) {
		SetTimer(4927, 10, NULL);

	}
	else {
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY0);
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY1);
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY2);
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY3);
	}
//	else {
//		if (nState == WA_INACTIVE) {
//			SetTimer(4924, l, NULL);
//		}
//	}
	// TODO: ここにメッセージ ハンドラ コードを追加します。
}

void CPlayList::OnPop32787()//ファイル名変更
{
	// TODO: ここにコマンド ハンドラ コードを追加します。
	int Lindex=-1;
	Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
	CFilename *a = new CFilename(CWnd::FromHandle(GetSafeHwnd()));
	w_flg=FALSE;
	memcpy(&a->pc,&pc[Lindex],sizeof(playlistdata0));
	CWnd::PostMessage(0x118);
	int ret=a->DoModal();
	if(ret==IDOK){
		_tcscpy(pc[Lindex].name,a->pc.name);
		_tcscpy(pc[Lindex].art,a->pc.art);
		_tcscpy(pc[Lindex].alb,a->pc.alb);
		_tcscpy(pc[Lindex].fol,a->pc.fol);
		RECT r;
		m_lc.GetItemRect(Lindex,&r,LVIR_BOUNDS);
		m_lc.RedrawWindow(&r);
	}
	w_flg=TRUE;
	delete a;
}

void CPlayList::OnFindUp()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	CString s;
	m_find.GetWindowText(s);
	s.MakeLower();
	if(s==_T("")) return;
	int pnt2;

	if(pnt<0) pnt=-1;
	if(pnt>=playcnt) pnt=playcnt;

	pnt2=pnt;
	if(pnt1!=-1) pnt2=pnt1;


	int flg=0;
	int i;
	for(i=pnt2;i<playcnt;i++){
		CString ss,ssl;
		ss=pc[i].name;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
		ss=pc[i].alb;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
		ss=pc[i].art;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
	}

	if(flg){
		for(int k=0;k<playcnt;k++){
			m_lc.SetItemState(k,m_lc.GetItemState(k,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}

		pnt1=i;

		m_lc.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
		m_lc.EnsureVisible(i,FALSE);
	}
	m_lc.SetFocus();
}

void CPlayList::OnFindDown()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	CString s;
	m_find.GetWindowText(s);
	s.MakeLower();
	if(s==_T("")) return;
	int pnt2;

	if(pnt<0) pnt=-1;
	if(pnt>=playcnt) pnt=playcnt;

	pnt2=pnt;
	if(pnt1!=-1) pnt2=pnt1;


	int flg=0;
	int i;
	for(i=pnt2;i>=0;i--){
		CString ss,ssl;
		ss=pc[i].name;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
		ss=pc[i].alb;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
		ss=pc[i].art;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
	}

	if(flg){
		for(int k=0;k<playcnt;k++){
			m_lc.SetItemState(k,m_lc.GetItemState(k,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
		pnt1=i;

		m_lc.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
		m_lc.EnsureVisible(i,FALSE);
	}
	m_lc.SetFocus();
}


void CPlayList::OnBnClickedCheck6mp3()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
}


void CPlayList::OnBnClickedCheck7dshow()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
}


HBRUSH CPlayList::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{

	HBRUSH hbr = CCustomControlUtility::ApplyControlColors(pDC, pWnd, nCtlColor);
	if (hbr)
		return hbr;

	hbr = CCustomDialog::OnCtlColor(pDC, pWnd, nCtlColor);

	// TODO: ここで DC の属性を変更してください。
	if (savedata.aero == 1) {
		if (nCtlColor == CTLCOLOR_DLG)
		{
			return m_brDlg;
		}
		if (nCtlColor == CTLCOLOR_STATIC)
		{
			SetBkMode(pDC->m_hDC, TRANSPARENT);
			return m_brDlg;
		}
	}
	// TODO: 既定値を使用したくない場合は別のブラシを返します。
	return hbr;
}


void CPlayList::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomDialog::OnShowWindow(bShow, nStatus);
	Invalidate();

	// TODO: ここにメッセージ ハンドラー コードを追加します。
}


void CPlayList::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomDialog::OnMoving(fwSide, pRect);
	CRect r;
	GetWindowRect(&r);
	if (playbase)
	playbase->MoveWindow(&r);
//	if (playbase)
//		::SetWindowPos(playbase->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
//	::SetWindowPos(og->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	// TODO: ここにメッセージ ハンドラー コードを追加します。
}


void CPlayList::OnSizing(UINT fwSide, LPRECT pRect)
{
	CCustomDialog::OnSizing(fwSide, pRect);
	CRect r;
	GetWindowRect(&r);
	if(playbase)
		playbase->MoveWindow(&r);
	// TODO: ここにメッセージ ハンドラー コードを追加します。
}


void CPlayList::OnSetFocus(CWnd* pOldWnd)
{
	CCustomDialog::OnSetFocus(pOldWnd);

	// TODO: ここにメッセージ ハンドラー コードを追加します。

}


BOOL CPlayList::OnNcActivate(BOOL bActive)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
		// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	UINT_PTR aa = 0;
	DWORD aaa = 0;
	if (plw) {
		if (bActive && pl->m_saisyo.GetCheck()) {
		//	og->ShowWindow(SW_RESTORE);
		}
	}
	if (bActive) {
		//aaaa = 1;
//		if (playbase) playbase->ShowWindow(SW_SHOW);
//		KillTimer(4930);
		aa = SetTimer(4927, 10, NULL);
		aaa = GetLastError();
		aaa = aaa;
	}
	else {
		//if(!bActive)
		//	SetTimer(4924, 10, NULL);
	}
	return CCustomDialog::OnNcActivate(bActive);
}

BOOL changeflg = FALSE;
void CPlayList::OnCbnSelchangeCombo1()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	if (changeflg == TRUE) return;
	Save();
	int num = m_listchange.GetCurSel();
	savedata.playlistnum = num;
	playcnt = 0;
	playlistdata0* tmp; tmp = pc;
	free(pc);
	pc = NULL;
	Load();
	if (pc == NULL) {
		pc = (playlistdata0*)malloc(sizeof(playlistdata0));
	}
	m_lc.SetItemCount(playcnt);
	for (int j = 0; j < playcnt; j++) pc[j].icon = 1;
	m_lc.RedrawWindow();
	Save();

	loadplaylistname();
}

void CPlayList::loadplaylistname()
{
	m_listchange.ResetContent();
	int lcnt = 0;
	for (lcnt = 0;; lcnt++) {
		CString s;
		if (lcnt == 0)
			s.Format(L"playlistu.dat");
		else
			s.Format(L"playlistu%d.dat", lcnt);
		if (!PathFileExists(GetModulePath() + s))
			break;
	}
	if (lcnt >= 999) lcnt = 999;
	for (int ii = 0; ii < lcnt; ii++) {
		CString s, ss;
		ss = savedata.playlistname[ii];
		if (ss == "") {
			ss.Format(LL2(L"プレイリスト：%d", L"Playlist: %d"), ii + 1);
		}
		s.Format(L"%s",ss);
		m_listchange.AddString(s);
	}
	m_listchange.AddString(LL2(L"<新しいプレイリスト>", L"<New playlist>"));
	m_listchange.SetCurSel(savedata.playlistnum);
	int num = m_listchange.GetCurSel();
	if (num != savedata.playlistnum) {
		savedata.playlistnum = 0;
		m_listchange.SetCurSel(savedata.playlistnum);
	}
}

CString CPlayList::GetModulePath()
{
	// 実行ファイルのパス
	CString modulePath = _T("");
	// ドライブ名、ディレクトリ名、ファイル名、拡張子
	wchar_t path[_MAX_PATH], drive[_MAX_PATH], dir[_MAX_PATH], fname[_MAX_PATH], ext[_MAX_PATH];

	// 実行ファイルのファイルパスを取得
	if (::GetModuleFileName(NULL, path, _MAX_PATH) != 0)
	{
		// ファイルパスを分割
		::_wsplitpath_s(path, drive, dir, fname, ext);
		// ドライブとディレクトリ名を結合して実行ファイルパスとする
		modulePath = CString(drive) + CString(dir);
	}

	return modulePath;
}

void CPlayList::OnBnClickedButton3()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CPlayListNew pn;
	pn.name = savedata.playlistname[savedata.playlistnum];
	if (pn.name == L"") pn.name.Format(L"プレイリスト：%d", savedata.playlistnum + 1);
	if (pn.DoModal() == IDOK) {
		wcscpy(savedata.playlistname[savedata.playlistnum], pn.name);
		loadplaylistname();
	}

}


void CPlayList::OnBnClickedPlaydelete()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	if (MessageBox(LL2(L"現在のリストを削除しますがよろしいですか？", L"Delete the current list?"), LL2(L"削除確認", L"Confirm Delete"), MB_YESNO) == IDNO) {
		return;
	}
	changeflg = TRUE;
	Save();
	CString s;
	int num = m_listchange.GetCurSel();

	int lcnt = 0;
	for (lcnt = 0;; lcnt++) {
		CString s;
		if (lcnt == 0)
			s.Format(L"playlistu.dat");
		else
			s.Format(L"playlistu%d.dat", lcnt);
		if (!PathFileExists(GetModulePath() + s))
			break;
	}
	if (lcnt >= 999) lcnt = 999;

	if (lcnt == 0 && num == 0) {//まだ追加してなくて、一番最初のを削除されたとき、
		m_listchange.ResetContent();
		s.Format(L"playlistu.dat");
		savedata.playlistname[0][0] = 0;
		CFile::Remove(GetModulePath() + s);
		savedata.playlistnum = 0;
		Load();
		savedata.playlistnum = 0;
		loadplaylistname();
		changeflg = FALSE;
		return;
	}

	if (lcnt != 0) {//削除されたとき
		CString s1, s2;
		if (num == 0)
			s1.Format(L"playlistu.dat");
		else
			s1.Format(L"playlistu%d.dat", num);
		if (PathFileExists(GetModulePath() + s1))
			CFile::Remove(GetModulePath() + s1);
		for (int j = num; j < lcnt - 1; j++) {
			if (j == 0)
				s1.Format(L"playlistu.dat");
			else
				s1.Format(L"playlistu%d.dat", j);
			const int jj = j + 1;
			if (jj == 0)
				s2.Format(L"playlistu.dat");
			else
				s2.Format(L"playlistu%d.dat", jj);

			CFile::Rename(GetModulePath() + s2, GetModulePath() + s1);
			wcscpy(savedata.playlistname[j], savedata.playlistname[jj]);
		}
		lcnt = 0;
		for (lcnt = 0;; lcnt++) {
			CString s;
			if (lcnt == 0)
				s.Format(L"playlistu.dat");
			else
				s.Format(L"playlistu%d.dat", lcnt);
			if (!PathFileExists(GetModulePath() + s))
				break;
		}
		if (lcnt >= 999) lcnt = 999;

		savedata.playlistname[lcnt][0] = 0;
		if (num == lcnt) {
			savedata.playlistnum = 0;
			num = 0;
		}
		loadplaylistname();
		int num = m_listchange.GetCurSel();
		savedata.playlistnum = num;
		playcnt = 0;
		playlistdata0* tmp; tmp = pc;
		free(pc);
		pc = NULL;
		Load();
		if (pc == NULL) {
			pc = (playlistdata0*)malloc(sizeof(playlistdata0));
		}
		m_lc.SetItemCount(playcnt);
		for (int j = 0; j < playcnt; j++) pc[j].icon = 1;
		m_lc.RedrawWindow();
		Save();
		changeflg = FALSE;
		return;
	}
}

