// Filename.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "ListCtrlA.h"
#include "PlayList.h"
#include "Filename.h"


// CFilename ダイアログ

IMPLEMENT_DYNAMIC(CFilename, CCustomBlurDialogBase)

CFilename::CFilename(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CFilename::IDD, pParent)
{

}

CFilename::~CFilename()
{
}

void CFilename::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT1, m_name);
	DDX_Control(pDX, IDC_EDIT2, m_art);
	DDX_Control(pDX, IDC_EDIT3, m_alb);
	DDX_Control(pDX, IDC_EDIT4, m_fol);
	DDX_Control(pDX, IDCANCEL, m_cdummy);
	DDX_Control(pDX, ID_OK, mok);
	DDX_Control(pDX, IDC_FILENAME_LBL_NAME, m_lblName);
	DDX_Control(pDX, IDC_FILENAME_LBL_ART, m_lblArt);
	DDX_Control(pDX, IDC_FILENAME_LBL_ALB, m_lblAlb);
	DDX_Control(pDX, IDC_FILENAME_LBL_FOL, m_lblFol);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CFilename, CCustomBlurDialogBase)
	ON_BN_CLICKED(ID_OK, &CFilename::OnBnClickedOk)
	ON_BN_CLICKED(IDOK, &CFilename::OnBnClickedOk2)
	cmn(CFilename);


// CFilename メッセージ ハンドラ

BOOL CFilename::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"ファイル名変更", L"Rename File", L"Renommer le fichier", L"Rinomina file", L"Cambiar nombre de archivo", L"파일 이름 바꾸기", L"重命名文件", L"إعادة تسمية الملف", L"Переименовать файл", L"Datei umbenennen", L"Renomear arquivo", L"Bestand hernoemen", L"Zmień nazwę pliku", L"Dosyayı yeniden adlandır"));
	m_name.SetWindowText(pc.name);
	m_art.SetWindowText(pc.art);
	m_alb.SetWindowText(pc.alb);
	m_fol.SetWindowText(pc.fol);
	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}

void CFilename::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	CString s;
	m_name.GetWindowText(s);
	_tcscpy(pc.name,s);
	m_art.GetWindowText(s);
	_tcscpy(pc.art,s);
	m_alb.GetWindowText(s);
	_tcscpy(pc.alb,s);
	m_fol.GetWindowText(s);
	_tcscpy(pc.fol,s);
	OnOK();
}

void CFilename::OnBnClickedOk2()
{
	OnBnClickedOk();
}