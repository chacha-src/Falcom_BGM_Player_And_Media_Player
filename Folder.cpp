// Folder.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "ogg.h"
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

	if (savedata.aero) {
		folderbase = new CImageBase;
		folderbase->Create(NULL);
		folderbase->oya = this;
	}
	else {
		folderbase = NULL;
	}
	CRect r;
	GetWindowRect(&r);
	MoveWindow(&r);
	if (savedata.aero) {
		if (folderbase)
			folderbase->MoveWindow(&r);
	}
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
	if (savedata.aero) {
		if (folderbase)
			::SetWindowPos(folderbase->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		if (folderbase)
			SetTimer(10, 200, NULL);
	}
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


void CFolder::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogBase::OnMoving(fwSide, pRect);
	CRect r;
	GetWindowRect(&r);
	if (savedata.aero) {
		if (folderbase)
			folderbase->MoveWindow(&r);
	}
}


int CFolder::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CCustomBlurDialogBase::OnCreate(lpCreateStruct) == -1)
		return -1;

	// TODO: ここに特定な作成コードを追加してください。
	if (savedata.aero == 1) {
		ModifyStyleEx(0, WS_EX_LAYERED);

		// レイヤードウィンドウの不透明度と透明のカラーキー
		SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY);

		// 赤色のブラシを作成する．
		m_brDlg.CreateSolidBrush(RGB(255, 0, 0));
	}
	return 0;
}


void CFolder::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	if (folderbase)
		delete folderbase;
	CCustomBlurDialogBase::OnOK();
}


void CFolder::OnBnClickedCancel()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	if (folderbase)
		delete folderbase;
	CCustomBlurDialogBase::OnCancel();
}


void CFolder::OnTimer(UINT_PTR nIDEvent)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	KillTimer(10);
	CRect r;
	GetWindowRect(&r);
	if (folderbase)
	folderbase->MoveWindow(&r);
	CCustomBlurDialogBase::OnTimer(nIDEvent);
}
