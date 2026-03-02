// CPlayListNew.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "afxdialogex.h"
#include "CPlayListNew.h"


// CPlayListNew ダイアログ

IMPLEMENT_DYNAMIC(CPlayListNew, CCustomBlurDialogExBase)

CPlayListNew::CPlayListNew(CWnd* pParent /*=nullptr*/)
	: CCustomBlurDialogExBase(IDD_PLAYLIST_NEW, pParent)
{

}

CPlayListNew::~CPlayListNew()
{
}

void CPlayListNew::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT1, m_name);
	DDX_Control(pDX, IDOK, afsasfafs);
	DDX_Control(pDX, IDCANCEL, fsaascasa);
}


BEGIN_MESSAGE_MAP(CPlayListNew, CCustomBlurDialogExBase)
	ON_BN_CLICKED(IDOK, &CPlayListNew::OnBnClickedOk)
END_MESSAGE_MAP()


// CPlayListNew メッセージ ハンドラー


void CPlayListNew::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	m_name.GetWindowText(name);
	if (name.GetLength() > 200) {
		MessageBox(LL14(L"文字数が長すぎます", L"Text is too long", L"Texte trop long", L"Testo troppo lungo", L"Texto demasiado largo", L"글자 수가 너무 깁니다", L"文字过长", L"النص طويل جداً", L"Текст слишком длинный", L"Text zu lang", L"Texto muito longo", L"Tekst te lang", L"Tekst za długi", L"Metin çok uzun"), LL14(L"ogg簡易プレイヤ", L"ogg Simple Player", L"ogg Lecteur simple", L"ogg Lettore semplice", L"ogg Reproductor simple", L"ogg 간이 플레이어", L"ogg简易播放器", L"ogg مشغل بسيط", L"ogg Простой плеер", L"ogg Einfacher Player", L"ogg Player simples", L"ogg Eenvoudige speler", L"ogg Prosty odtwarzacz", L"ogg Basit oynatıcı"));
		return;
	}
	CCustomBlurDialogExBase::OnOK();
}


BOOL CPlayListNew::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();

	SetWindowText(LL14(L"新しいプレイリスト", L"New Playlist", L"Nouvelle liste", L"Nuova playlist", L"Nueva lista", L"새 재생 목록", L"新建播放列表", L"قائمة جديدة", L"Новый плейлист", L"Neue Playlist", L"Nova playlist", L"Nieuwe afspeellijst", L"Nowa playlist", L"Yeni çalma listesi"));
	SetDlgItemText(IDC_STATIC, LL14(L"新しいプレイリスト名", L"New playlist name", L"Nom de la nouvelle liste", L"Nome nuova playlist", L"Nombre de nueva lista", L"새 재생 목록 이름", L"新建播放列表名称", L"اسم القائمة الجديدة", L"Имя нового плейлиста", L"Name der neuen Playlist", L"Nome da nova playlist", L"Naam nieuwe afspeellijst", L"Nazwa nowej playlisty", L"Yeni çalma listesi adı"));
	m_name.SetWindowText(name);
	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}
