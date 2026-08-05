// Folder.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "Folder.h"
#include "PVI.h"
#include "KpiV5ConfigDlg.h"
#include <direct.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CFolder ダイアログ


CFolder::CFolder(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CFolder::IDD, pParent)
{
	//{{AFX_DATA_INIT(CFolder)
	//}}AFX_DATA_INIT
}


void CFolder::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CFolder)
	DDX_Control(pDX, IDC_EDIT8, m_8s);
	DDX_Control(pDX, IDC_EDIT7, m_7s);
	DDX_Control(pDX, IDC_EDIT6, m_6s);
	DDX_Control(pDX, IDC_BUTTON16, m_5);
	DDX_Control(pDX, IDC_EDIT5, m_5s);
	DDX_Control(pDX, IDC_EDIT4, m_4s);
	DDX_Control(pDX, IDC_EDIT3, m_3s);
	DDX_Control(pDX, IDC_EDIT2, m_2s);
	DDX_Control(pDX, IDC_EDIT1, m_1s);
	DDX_Control(pDX, IDC_BUTTON11, m_4);
	DDX_Control(pDX, IDC_BUTTON10, m_3);
	DDX_Control(pDX, IDC_BUTTON5, m_2);
	DDX_Control(pDX, IDC_BUTTON1, m_1);
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDC_EDIT9, m_9s);
	DDX_Control(pDX, IDC_EDIT10, m_10s);
	DDX_Control(pDX, IDC_EDIT11, m_11s);
	DDX_Control(pDX, IDC_BUTTON29, m_10);
	DDX_Control(pDX, IDC_BUTTON30, m_11);
	DDX_Control(pDX, IDC_EDIT12, m_12s);
	DDX_Control(pDX, IDC_BUTTON32, m_12);
	DDX_Control(pDX, IDC_EDIT13, m_13s);
	DDX_Control(pDX, IDC_EDIT14, m_14s);
	DDX_Control(pDX, IDC_EDIT15, m_15s);
	DDX_Control(pDX, IDC_EDIT16, m_16s);
	DDX_Control(pDX, IDC_EDIT17, m_17s);
	DDX_Control(pDX, IDC_EDIT18, m_18s);
	DDX_Control(pDX, IDC_EDIT19, m_19s);
	DDX_Control(pDX, IDC_EDIT20, m_20s);
	DDX_Control(pDX, IDC_EDIT21, m_21s);
	DDX_Control(pDX, IDC_EDIT22, m_22s);
	DDX_Control(pDX, IDC_EDIT23, m_23s);
	DDX_Control(pDX, IDC_EDIT24, m_24s);
	DDX_Control(pDX, IDOK, m_okdummy);
	DDX_Control(pDX, IDC_BUTTON25, aaaaaaaaaa);
	DDX_Control(pDX, IDCANCEL, asfsfcascs);
	DDX_Control(pDX, IDC_BUTTON18, m6);
	DDX_Control(pDX, IDC_BUTTON20, m7);
	DDX_Control(pDX, IDC_BUTTON22, m8);
	DDX_Control(pDX, IDC_BUTTON26, m9);
	DDX_Control(pDX, IDC_BUTTON34, m15);
	DDX_Control(pDX, IDC_BUTTON36, m16);
	DDX_Control(pDX, IDC_BUTTON38, m17);
	DDX_Control(pDX, IDC_BUTTON40, m18);
	DDX_Control(pDX, IDC_BUTTON41, m19);
	DDX_Control(pDX, IDC_BUTTON42, m20);
	DDX_Control(pDX, IDC_BUTTON43, m21);
	DDX_Control(pDX, IDC_BUTTON49, m22);
	DDX_Control(pDX, IDC_BUTTON50, m23);
	DDX_Control(pDX, IDC_BUTTON52, m24);
	DDX_Control(pDX, IDC_BUTTON55, m25);
	DDX_Control(pDX, IDC_BUTTON56, m27);
	DDX_Control(pDX, IDC_BUTTON57, m_fsafa);
	DDX_Control(pDX, IDC_FD_HELP, m_help);
	static const UINT folLblIds[] = {
		IDC_FOL_LBL01, IDC_FOL_LBL02, IDC_FOL_LBL03, IDC_FOL_LBL04, IDC_FOL_LBL05,
		IDC_FOL_LBL06, IDC_FOL_LBL07, IDC_FOL_LBL08, IDC_FOL_LBL09, IDC_FOL_LBL10,
		IDC_FOL_LBL11, IDC_FOL_LBL12, IDC_FOL_LBL13, IDC_FOL_LBL14, IDC_FOL_LBL15,
		IDC_FOL_LBL16, IDC_FOL_LBL17, IDC_FOL_LBL18, IDC_FOL_LBL19, IDC_FOL_LBL20,
		IDC_FOL_LBL21, IDC_FOL_LBL22, IDC_FOL_LBL23, IDC_FOL_LBL24, IDC_FOL_LBL25
	};
	for (int i = 0; i < 25; i++)
		DDX_Control(pDX, folLblIds[i], m_folLbl[i]);
}


BEGIN_MESSAGE_MAP(CFolder, CCustomBlurDialogBase)
	//{{AFX_MSG_MAP(CFolder)
	ON_BN_CLICKED(IDC_BUTTON1, On1)
	ON_BN_CLICKED(IDC_BUTTON5, On2)
	ON_BN_CLICKED(IDC_BUTTON10, On3)
	ON_BN_CLICKED(IDC_BUTTON11, On4)
	ON_BN_CLICKED(IDC_BUTTON16, On5)
	ON_BN_CLICKED(IDC_BUTTON18, On6)
	ON_BN_CLICKED(IDC_BUTTON20, OnButton20)
	ON_BN_CLICKED(IDC_BUTTON22, OnButton22)
	//}}AFX_MSG_MAP
	ON_BN_CLICKED(IDC_BUTTON26, &CFolder::On9XA)
	ON_BN_CLICKED(IDC_BUTTON29, &CFolder::OnBnClickedButton29)
	ON_BN_CLICKED(IDC_BUTTON30, &CFolder::OnBnClickedButton30)
	ON_BN_CLICKED(IDC_BUTTON32, &CFolder::OnBnClickedButton32)
	ON_BN_CLICKED(IDC_BUTTON34, &CFolder::OnBnClickedButton34)
	ON_BN_CLICKED(IDC_BUTTON36, &CFolder::OnBnClickedButton36)
	ON_BN_CLICKED(IDC_BUTTON38, &CFolder::OnBnClickedButton38)
	ON_BN_CLICKED(IDC_BUTTON40, &CFolder::OnBnClickedButton40)
	ON_BN_CLICKED(IDC_BUTTON41, &CFolder::OnBnClickedButton41)
	ON_BN_CLICKED(IDC_BUTTON42, &CFolder::OnBnClickedButton42)
	ON_BN_CLICKED(IDC_BUTTON43, &CFolder::OnBnClickedButton43)
	ON_BN_CLICKED(IDC_BUTTON49, &CFolder::OnBnClickedButton49)
	ON_BN_CLICKED(IDC_BUTTON50, &CFolder::OnBnClickedButton50)
	ON_BN_CLICKED(IDC_BUTTON52, &CFolder::OnBnClickedButton52)
	ON_BN_CLICKED(IDC_BUTTON55, &CFolder::OnBnClickedButton55)
	ON_BN_CLICKED(IDC_BUTTON56, &CFolder::OnBnClickedButton56)
	ON_BN_CLICKED(IDC_BUTTON25, &CFolder::OnBnClickedButton25)
	ON_BN_CLICKED(IDC_BUTTON57, &CFolder::OnBnClickedButton57)
	ON_WM_CTLCOLOR()
	ON_WM_MOVING()
	ON_WM_CREATE()
	ON_BN_CLICKED(IDOK, &CFolder::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CFolder::OnBnClickedCancel)
	ON_BN_CLICKED(IDC_FD_HELP, &CFolder::OnBnClickedHelp)
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CFolder メッセージ ハンドラ
extern save savedata;
#include "CImageBase.h"
CImageBase* folderbase = NULL;
BOOL CFolder::OnInitDialog() 
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"フォルダ設定", L"Folder Settings", L"Paramètres dossier", L"Impostazioni cartella", L"Configuración de carpeta", L"폴더 설정", L"文件夹设置", L"إعدادات المجلد", L"Настройки папки", L"Ordnereinstellungen", L"Configurações de pasta", L"Mapinstellingen", L"Ustawienia folderu", L"Klasör ayarları"));
	SetDlgItemText(IDC_BUTTON57, LL14(
		L"KPI ver5設定",
		L"KPI ver5 Settings",
		L"Parametres KPI ver5",
		L"Impostazioni KPI ver5",
		L"Configuracion KPI ver5",
		L"KPI ver5 설정",
		L"KPI ver5 设置",
		L"إعدادات KPI ver5",
		L"Настройки KPI ver5",
		L"KPI ver5 Einstellungen",
		L"Configuracoes KPI ver5",
		L"KPI ver5 instellingen",
		L"Ustawienia KPI ver5",
		L"KPI ver5 ayarlari"));
	m_1s.SetWindowText(savedata.ys6);
	m_2s.SetWindowText(savedata.ysf);
	m_3s.SetWindowText(savedata.ed6fc);
	m_4s.SetWindowText(savedata.ed6sc);
	m_5s.SetWindowText(savedata.yso);
	m_6s.SetWindowText(savedata.ed6tc);
	m_7s.SetWindowText(savedata.zweiii);
	m_8s.SetWindowText(savedata.ysc);
	m_9s.SetWindowText(savedata.xa);
	m_10s.SetWindowText(savedata.ys12);
	m_11s.SetWindowText(savedata.sor);
	m_12s.SetWindowText(savedata.ys122);
	m_13s.SetWindowText(savedata.zwei);
	m_14s.SetWindowText(savedata.gurumin);
	m_15s.SetWindowText(savedata.dino);
	m_16s.SetWindowText(savedata.br4);
	m_17s.SetWindowText(savedata.ed3);
	m_18s.SetWindowText(savedata.ed4);
	m_19s.SetWindowText(savedata.ed5);
	m_20s.SetWindowText(savedata.tuki);
	m_21s.SetWindowText(savedata.nishi);
	m_22s.SetWindowText(savedata.arc);
	m_23s.SetWindowText(savedata.san1);
	m_24s.SetWindowText(savedata.san2);

	folderbase = NULL;
	CRect r;
	GetWindowRect(&r);
	MoveWindow(&r);
	extern CPlayList* pl;
	extern COggDlg* og;
	extern int ip;
	ip = 0;
	og->KillTimer(4923);
	og->KillTimer(4924);
	if (pl) {
		pl->KillTimer(4923);
		pl->KillTimer(4924);
	}
	EnableMainWindowLock(&savedata.folderMainLock);
	CCC_MainLockBringToFront(m_hWnd);

	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();

	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		m_tooltip.AddTool(&m_help, LL14(
			L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida",
			L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل",
			L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen",
			L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 420, 12000);
	}

	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	return TRUE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}

int CALLBACK SHBrowseProc(HWND hWnd, UINT uMsg
        , LPARAM lParam, LPARAM lpData);

//フォルダー選択ダイヤログの起動
UINT CFolder::GetOpenFolderName(HWND hWnd, LPCTSTR lpszDefaultFolder, LPTSTR lpszBuf, DWORD dwBufSize)
{
    BROWSEINFO bi;
    __unaligned ITEMIDLIST *pidl;
    TCHAR szSelectedFolder[1024];
    IMalloc *pMalloc;
	CoInitialize(NULL);
    ZeroMemory(&bi, sizeof(BROWSEINFO));
    bi.hwndOwner = hWnd;
    //コールバック関数を指定
    bi.lpfn = SHBrowseProc;
    //デフォルトで選択させておくフォルダを設定
    bi.lParam = (LPARAM)lpszDefaultFolder;
    //タイトルの設定
    bi.lpszTitle = LL14(L"各ゲームのフォルダを指定してください。", L"Select folder for each game.", L"Veuillez sélectionner le dossier pour chaque jeu.", L"Seleziona la cartella per ogni gioco.", L"Seleccione la carpeta para cada juego.", L"각 게임의 폴더를 선택하세요.", L"请为每个游戏选择文件夹。", L"الرجاء تحديد المجلد لكل لعبة.", L"Выберите папку для каждой игры.", L"Ordner für jedes Spiel auswählen.", L"Selecione a pasta para cada jogo.", L"Selecteer map voor elk spel.", L"Wybierz folder dla każdej gry.", L"Her oyun için klasör seçin.");
	bi.ulFlags =0x0040;//BIF_NEWDIALOGSTYLE;
    //フォルダダイヤログの起動
    pidl = SHBrowseForFolder(&bi);
	CoUninitialize();
    if (pidl) {
        //選択されたフォルダ名を取得
        SHGetPathFromIDList(pidl, szSelectedFolder);
        //システムが確保したITEMIDLISTを開放する
        SHGetMalloc(&pMalloc);
        if (pMalloc) {
            pMalloc->Free(pidl);
            pMalloc->Release();
        }
        if ((DWORD)lstrlen(szSelectedFolder) < dwBufSize) {
            _tcscpy(lpszBuf, szSelectedFolder);
        }
        //フォルダが選択された
        return IDOK;
    }
    //フォルダは選択されなかった
    return IDCANCEL;
}

int CALLBACK SHBrowseProc(HWND hWnd, UINT uMsg , LPARAM lParam, LPARAM lpData)
{
    if (uMsg == BFFM_INITIALIZED && lpData) {
        //デフォルトで選択させるパスの設定
        SendMessage(hWnd, BFFM_SETSELECTION, TRUE, lpData);
    }
    return (0);
}

void CFolder::On1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.ys6);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_1s.SetWindowText(pcFolder);
		_tcscpy(savedata.ys6,pcFolder);
    }
}

void CFolder::On2() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.ysf);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_2s.SetWindowText(pcFolder);
		_tcscpy(savedata.ysf,pcFolder);
    }
}

void CFolder::On3() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.ed6fc);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_3s.SetWindowText(pcFolder);
		_tcscpy(savedata.ed6fc,pcFolder);
    }
}

void CFolder::On4() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.ed6sc);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_4s.SetWindowText(pcFolder);
		_tcscpy(savedata.ed6sc,pcFolder);
    }
}

void CFolder::On5() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.yso);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_5s.SetWindowText(pcFolder);
		_tcscpy(savedata.yso,pcFolder);
    }
	
}

void CFolder::On6() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.ed6tc);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_6s.SetWindowText(pcFolder);
		_tcscpy(savedata.ed6tc,pcFolder);
    }
	
}

void CFolder::OnButton20() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.zweiii);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_7s.SetWindowText(pcFolder);
		_tcscpy(savedata.zweiii,pcFolder);
    }
	
}

void CFolder::OnButton22() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.ysc);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_8s.SetWindowText(pcFolder);
		_tcscpy(savedata.ysc,pcFolder);
    }
	
}

void CFolder::On9XA()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.xa);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_9s.SetWindowText(pcFolder);
		_tcscpy(savedata.xa,pcFolder);
    }
}

void CFolder::OnBnClickedButton29()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.ys12);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_10s.SetWindowText(pcFolder);
		_tcscpy(savedata.ys12,pcFolder);
    }
}

void CFolder::OnBnClickedButton30()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.sor);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_11s.SetWindowText(pcFolder);
		_tcscpy(savedata.sor,pcFolder);
    }
}

void CFolder::OnBnClickedButton32()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.ys122);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_12s.SetWindowText(pcFolder);
		_tcscpy(savedata.ys122,pcFolder);
    }
}

void CFolder::OnBnClickedButton34()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.zwei);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_13s.SetWindowText(pcFolder);
		_tcscpy(savedata.zwei,pcFolder);
    }
}

void CFolder::OnBnClickedButton36()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.gurumin);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_14s.SetWindowText(pcFolder);
		_tcscpy(savedata.gurumin,pcFolder);
    }
}

void CFolder::OnBnClickedButton38()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.dino);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_15s.SetWindowText(pcFolder);
		_tcscpy(savedata.dino,pcFolder);
    }
}

void CFolder::OnBnClickedButton40()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.br4);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_16s.SetWindowText(pcFolder);
		_tcscpy(savedata.br4,pcFolder);
    }
}

void CFolder::OnBnClickedButton41()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.ed3);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_17s.SetWindowText(pcFolder);
		_tcscpy(savedata.ed3,pcFolder);
    }
}

void CFolder::OnBnClickedButton42()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.ed4);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_18s.SetWindowText(pcFolder);
		_tcscpy(savedata.ed4,pcFolder);
    }
}

void CFolder::OnBnClickedButton43()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.ed5);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_19s.SetWindowText(pcFolder);
		_tcscpy(savedata.ed5,pcFolder);
    }
}

void CFolder::OnBnClickedButton49()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.tuki);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_20s.SetWindowText(pcFolder);
		_tcscpy(savedata.tuki,pcFolder);
    }
}

void CFolder::OnBnClickedButton50()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.nishi);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_21s.SetWindowText(pcFolder);
		_tcscpy(savedata.nishi,pcFolder);
    }
}

void CFolder::OnBnClickedButton52()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.arc);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_22s.SetWindowText(pcFolder);
		_tcscpy(savedata.arc,pcFolder);
    }
}

void CFolder::OnBnClickedButton55()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.san1);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_23s.SetWindowText(pcFolder);
		_tcscpy(savedata.san1,pcFolder);
    }
}

void CFolder::OnBnClickedButton56()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
    CWnd *pMainWnd = AfxGetApp()->m_pMainWnd;
    TCHAR pcFolder[1024];
    TCHAR str1[1024];
	CString str;
	_tchdir(savedata.san2);
	_tgetcwd(str1,1024);str=str1;
    if (IDOK == GetOpenFolderName(pMainWnd->GetSafeHwnd(), str, pcFolder, 1024)) {
		m_24s.SetWindowText(pcFolder);
		_tcscpy(savedata.san2,pcFolder);
    }
}

void CFolder::OnBnClickedButton25()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	CPVI *p = new CPVI(CWnd::FromHandle(GetSafeHwnd()));
	p->DoModal();
	delete p;
}

void CFolder::OnBnClickedButton57()
{
	CKpiV5ConfigDlg* p = new CKpiV5ConfigDlg(CWnd::FromHandle(GetSafeHwnd()));
	p->DoModal();
	delete p;
}


HBRUSH CFolder::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CCustomBlurDialogBase::OnCtlColor(pDC, pWnd, nCtlColor);

	// TODO: ここで DC の属性を変更してください。
	// TODO: 既定値を使用したくない場合は別のブラシを返します。
	return hbr;
}


void CFolder::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogBase::OnMoving(fwSide, pRect);
	CRect r;
	GetWindowRect(&r);
}


int CFolder::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CCustomBlurDialogBase::OnCreate(lpCreateStruct) == -1)
		return -1;

	// TODO: ここに特定な作成コードを追加してください。
	return 0;
}


void CFolder::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CCustomBlurDialogBase::OnOK();
}


void CFolder::OnBnClickedCancel()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CCustomBlurDialogBase::OnCancel();
}


void CFolder::OnTimer(UINT_PTR nIDEvent)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	CCustomBlurDialogBase::OnTimer(nIDEvent);
}

BOOL CFolder::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

namespace {

class CFdHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_FD_HELP };
	explicit CFdHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CFdHelpDlg* g_fdHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CFdHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CFdHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"フォルダ設定操作ガイド", L"Folder Settings Guide", L"Guide des dossiers", L"Guida cartelle",
		L"Guía de carpetas", L"폴더 설정 가이드", L"文件夹设置指南", L"دليل المجلدات",
		L"Руководство по папкам", L"Ordner-Anleitung", L"Guia de pastas", L"Mapgids",
		L"Przewodnik folderów", L"Klasör kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CFdHelpDlg::OnOK() { DestroyWindow(); }
void CFdHelpDlg::OnCancel() { DestroyWindow(); }
void CFdHelpDlg::OnClose() { DestroyWindow(); }

void CFdHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_fdHelpDlg == this)
		g_fdHelpDlg = nullptr;
	delete this;
}

BOOL CFdHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CFdHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	const int footerH = hp.footerH;
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 1;
	CBrush frameBrush(RGB(130, 130, 150));

	auto title = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(55, 45, 85));
		dc.TextOut(x, y, t);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(65, 65, 80));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 115));
		dc.TextOut(x, y, t);
	};

	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"フォルダ設定操作ガイド", L"Folder Settings — Guide", L"Guide dossiers", L"Guida cartelle",
		L"Guía carpetas", L"폴더 설정 가이드", L"文件夹设置指南", L"دليل المجلدات",
		L"Руководство по папкам", L"Ordner-Guide", L"Guia pastas", L"Mapgids",
		L"Przewodnik folderów", L"Klasör kılavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"各ゲームのインストール先を登録し、曲一覧や KPI 展開に使います。",
		L"Register each game install path for song lists and KPI extraction.",
		L"Enregistrez le dossier de chaque jeu (listes / KPI).",
		L"Registra il percorso di ogni gioco (liste / KPI).",
		L"Registra la ruta de cada juego (listas / KPI).",
		L"각 게임 설치 경로를 등록해 곡 목록·KPI 전개에 사용합니다.",
		L"登记各游戏安装路径，供曲目列表与 KPI 展开使用。",
		L"سجّل مسار تثبيت كل لعبة للقوائم وKPI.",
		L"Укажите папку каждой игры для списков и KPI.",
		L"Spielordner für Listen und KPI hinterlegen.",
		L"Registe a pasta de cada jogo (listas / KPI).",
		L"Registreer de map van elk spel (lijsten / KPI).",
		L"Zarejestruj folder każdej gry (listy / KPI).",
		L"Her oyunun kurulum yolunu kaydedin (listeler / KPI)."));
	y += lh + 4;

	title(L, y, LL14(L"基本操作", L"Basics", L"Bases", L"Basi", L"Básicos", L"기본", L"基本", L"أساسيات",
		L"Основы", L"Grundlagen", L"Básicos", L"Basis", L"Podstawy", L"Temeller"));
	y += titleLh;
	body(L, y, LL14(L"・「...」 …… フォルダ参照。選んだパスが編集欄に入ります", L"· \"...\" …… browse folder; selection fills the edit field", L"· « ... » …… parcourir; le chemin remplit le champ", L"· \"...\" …… sfoglia; il percorso va nel campo",
		L"· \"...\" …… examinar; la ruta llena el campo", L"· \"...\" …… 폴더 찾아보기. 선택 경로가 입력란에", L"· \"...\" …… 浏览文件夹，所选路径填入编辑框", L"· \"...\" …… استعراض؛ المسار يملأ الحقل",
		L"· «...» …… обзор папки; путь в поле", L"· \"...\" …… Ordner wählen; Pfad ins Feld", L"· \"...\" …… procurar; caminho vai ao campo", L"· \"...\" …… bladeren; pad vult het veld",
		L"· \"...\" …… przeglądaj; ścieżka trafia do pola", L"· \"...\" …… klasöre göz at; yol alana gelir")); y += lh;
	body(L, y, LL14(L"・OK …… 入力中のパスを保存して閉じます", L"· OK …… save the paths and close", L"· OK …… enregistrer et fermer", L"· OK …… salva i percorsi e chiudi",
		L"· OK …… guardar rutas y cerrar", L"· OK …… 경로를 저장하고 닫습니다", L"· OK …… 保存路径并关闭", L"· موافق …… حفظ المسارات والإغلاق",
		L"· OK …… сохранить пути и закрыть", L"· OK …… Pfade speichern und schließen", L"· OK …… gravar caminhos e fechar", L"· OK …… paden opslaan en sluiten",
		L"· OK …… zapisz ścieżki i zamknij", L"· OK …… yolları kaydedip kapat")); y += lh;
	body(L, y, LL14(L"・KPI ver5設定 …… KPI プラグイン ver5 の詳細設定を開きます", L"· KPI ver5 Settings …… open KPI plugin v5 options", L"· Paramètres KPI ver5 …… options du plugin", L"· Impostazioni KPI ver5 …… opzioni plugin",
		L"· Config. KPI ver5 …… opciones del plugin", L"· KPI ver5 설정 …… KPI 플러그인 v5 옵션", L"· KPI ver5 设置 …… 打开 KPI 插件 v5 选项", L"· إعدادات KPI ver5 …… خيارات المكون",
		L"· KPI ver5 …… настройки плагина v5", L"· KPI ver5 …… Plugin-Optionen öffnen", L"· KPI ver5 …… opções do plugin", L"· KPI ver5 …… pluginopties openen",
		L"· KPI ver5 …… otwórz opcje wtyczki", L"· KPI ver5 …… eklenti seçeneklerini aç")); y += lh + 4;

	// mini diagram: label | path | ...
	title(L, y, LL14(L"行の見方", L"Row layout", L"Disposition", L"Disposizione", L"Filas", L"행 구성", L"行布局", L"تخطيط الصف",
		L"Строка", L"Zeilenlayout", L"Linha", L"Rij-indeling", L"Układ wiersza", L"Satır düzeni"));
	y += titleLh;
	const int gx = L, gy = y, gw = min(300, rc.Width() - L * 2), gh = lh * 2 + 10;
	dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
	dc.FillSolidRect(gx + 4, gy + 6, 40, gh - 12, RGB(90, 120, 170));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + 8, gy + 8, L"YS6");
	dc.FillSolidRect(gx + 50, gy + 6, gw - 110, gh - 12, RGB(255, 255, 255));
	dc.FrameRect(CRect(gx + 50, gy + 6, gx + gw - 60, gy + gh - 6), &frameBrush);
	dc.SetTextColor(RGB(80, 80, 95));
	dc.TextOut(gx + 56, gy + 8, L"C:\\Games\\...");
	dc.FillSolidRect(gx + gw - 52, gy + 6, 44, gh - 12, RGB(180, 140, 60));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + gw - 44, gy + 8, L"...");
	dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
	y = gy + gh + 4;
	muted(L, y, LL14(
		L"左=ゲーム名、中央=パス、右の「...」=参照ボタン。",
		L"Left = game name, center = path, right \"...\" = browse.",
		L"Gauche = jeu, centre = chemin, « ... » = parcourir.",
		L"Sinistra = gioco, centro = percorso, \"...\" = sfoglia.",
		L"Izq. = juego, centro = ruta, \"...\" = examinar.",
		L"왼쪽=게임명, 가운데=경로, 오른쪽 \"...\"=찾아보기.",
		L"左=游戏名，中=路径，右\"...\"=浏览。",
		L"يسار=اللعبة، وسط=المسار، \"...\"=استعراض.",
		L"Слева=игра, центр=путь, «...»=обзор.",
		L"Links=Spiel, Mitte=Pfad, \"...\"=Durchsuchen.",
		L"Esq.=jogo, centro=caminho, \"...\"=procurar.",
		L"Links=spel, midden=pad, \"...\"=bladeren.",
		L"Lewo=gra, środek=ścieżka, \"...\"=przeglądaj.",
		L"Sol=oyun, orta=yol, \"...\"=göz at."));
	y += lh + 6;

	title(L, y, LL14(L"補足", L"Notes", L"Notes", L"Note", L"Notas", L"보완", L"补充", L"ملاحظات",
		L"Заметки", L"Hinweise", L"Notas", L"Opmerkingen", L"Uwagi", L"Notlar"));
	y += titleLh;
	body(L, y, LL14(L"・パスは曲の取り込み・アーカイブ展開の起点になります", L"· Paths are the root for song import and archive extract", L"· Chemins = base d'import / extraction", L"· Percorsi = base import / estrazione",
		L"· Rutas = base de importación / extracción", L"· 경로는 곡 가져오기·아카이브 전개의 기준", L"· 路径是导入曲目与解压归档的起点", L"· المسارات أساس الاستيراد والاستخراج",
		L"· Пути — корень импорта и распаковки", L"· Pfade = Wurzel für Import/Extraktion", L"· Caminhos = raiz de importação/extração", L"· Paden = basis voor import/extractie",
		L"· Ścieżki = baza importu/rozpakowania", L"· Yollar = içe aktarma/çıkarma kökü")); y += lh;
	muted(L, y, LL14(
		L"未設定のゲームは機能が使えません。必要なタイトルだけ指定すれば十分です。",
		L"Unset games stay unavailable. Set only the titles you need.",
		L"Jeux non définis = indisponibles. Définissez seulement ceux utiles.",
		L"Giochi non impostati = non disponibili. Imposta solo i necessari.",
		L"Juegos sin ruta = no disponibles. Configure solo los necesarios.",
		L"미설정 게임은 사용할 수 없습니다. 필요한 타이틀만 지정하면 됩니다.",
		L"未设置的游戏不可用。只需指定需要的标题。",
		L"الألعاب غير المعيّنة غير متاحة. عيّن ما تحتاجه فقط.",
		L"Не заданные игры недоступны. Укажите только нужные.",
		L"Unsetzte Spiele sind nicht nutzbar. Nur benötigte setzen.",
		L"Jogos sem pasta ficam indisponíveis. Defina só os necessários.",
		L"Niet ingestelde spellen zijn niet beschikbaar. Stel alleen nodige in.",
		L"Gry bez ścieżki są niedostępne. Ustaw tylko potrzebne.",
		L"Ayarlanmayan oyunlar kullanılamaz. Sadece gerekenleri ayarlayın."));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

void CFolder::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CFolder::ShowHelpSheet()
{
	if (g_fdHelpDlg && ::IsWindow(g_fdHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_fdHelpDlg, this);
		return;
	}
	if (g_fdHelpDlg && !::IsWindow(g_fdHelpDlg->GetSafeHwnd()))
		g_fdHelpDlg = nullptr;
	CFdHelpDlg* dlg = new CFdHelpDlg(this);
	if (!dlg->Create(IDD_FD_HELP, this)) {
		delete dlg;
		return;
	}
	g_fdHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CFolder::OnBnClickedHelp()
{
	ShowHelpSheet();
}

void CFolder::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

void CFolder::OnDestroy()
{
	if (g_fdHelpDlg && ::IsWindow(g_fdHelpDlg->GetSafeHwnd()))
		g_fdHelpDlg->DestroyWindow();
	CCustomBlurDialogBase::OnDestroy();
}
