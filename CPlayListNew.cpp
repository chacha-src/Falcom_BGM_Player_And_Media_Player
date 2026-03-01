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
		MessageBox(LL2(L"文字数が長すぎます", L"Text is too long"), LL2(L"ogg簡易プレイヤ", L"ogg Simple Player"));
		return;
	}
	CCustomBlurDialogExBase::OnOK();
}


BOOL CPlayListNew::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();

	SetWindowText(LL2(L"新しいプレイリスト", L"New Playlist"));
	SetDlgItemText(IDC_STATIC, LL2(L"新しいプレイリスト名", L"New playlist name"));
	m_name.SetWindowText(name);
	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}
