// ZeroFol.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "ZeroFol.h"
#include "afxdialogex.h"


// CZeroFol ダイアログ

IMPLEMENT_DYNAMIC(CZeroFol, CCustomBlurDialogBase)

CZeroFol::CZeroFol(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(IDD_ZEROFOL, pParent)
{

}

CZeroFol::~CZeroFol()
{
}

void CZeroFol::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT2, m_fol);
	DDX_Control(pDX, IDOK, m_okdummy);
	DDX_Control(pDX, IDC_FOL, m_okdummya);
	DDX_Control(pDX, IDC_STATIC, m_msg);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CZeroFol, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_FOL, &CZeroFol::OnBnClickedFol)
	ON_BN_CLICKED(IDOK, &CZeroFol::OnBnClickedOk)
	cmn(CZeroFol);


// CZeroFol メッセージ ハンドラー

extern save savedata;
void CZeroFol::OnBnClickedFol()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CFileDialog f(TRUE, L"t_bgm._dt", NULL, 0, LL14(L"零の軌跡ループファイルt_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt"));
	if (f.DoModal() == IDOK) {
		CString s = f.GetFolderPath()+L"\\"+f.GetFileTitle() + L"." + f.GetFileExt();
		m_fol.SetWindowText(s);
	}
}



BOOL CZeroFol::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"碧の軌跡 t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt"));
	m_msg.SetWindowText(LL14(L"碧の軌跡にはループデータ存在しないため、零の軌跡の「t_bgm..dt」を選択してください", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt"));
	m_fol.SetWindowText(savedata.zero);
	RECT r;
	GetWindowRect(&r);
	r.top += 600;
	r.bottom += 600;
	MoveWindow(&r);
	return TRUE;  // return TRUE unless you set the focus to a control
				  // 例外 : OCX プロパティ ページは必ず FALSE を返します。
}


void CZeroFol::OnBnClickedOk()
{
	CString s;
	m_fol.GetWindowText(s);
	TCHAR *ss;
	ss = s.GetBuffer();
	_tcscpy(savedata.zero, ss);
	s.ReleaseBuffer();
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CCustomBlurDialogBase::OnOK();
}
