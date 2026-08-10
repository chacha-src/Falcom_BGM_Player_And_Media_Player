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
	CFileDialog f(TRUE, L"t_bgm._dt", NULL, 0, LL14(L"零の軌跡ループファイルt_bgm._dt|t_bgm._dt", L"Zero no Kiseki loop file t_bgm._dt|t_bgm._dt", L"Fichier boucle Zero no Kiseki t_bgm._dt|t_bgm._dt", L"File loop Zero no Kiseki t_bgm._dt|t_bgm._dt", L"Archivo bucle Zero no Kiseki t_bgm._dt|t_bgm._dt", L"영의 궤적 루프 파일 t_bgm._dt|t_bgm._dt", L"零之轨迹循环文件 t_bgm._dt|t_bgm._dt", L"ملف حلقة Zero no Kiseki t_bgm._dt|t_bgm._dt", L"Файл петли Zero no Kiseki t_bgm._dt|t_bgm._dt", L"Zero no Kiseki Loop-Datei t_bgm._dt|t_bgm._dt", L"Ficheiro loop Zero no Kiseki t_bgm._dt|t_bgm._dt", L"Zero no Kiseki loopbestand t_bgm._dt|t_bgm._dt", L"Plik pętli Zero no Kiseki t_bgm._dt|t_bgm._dt", L"Zero no Kiseki döngü dosyası t_bgm._dt|t_bgm._dt"));
	if (f.DoModal() == IDOK) {
		CString s = f.GetFolderPath()+L"\\"+f.GetFileTitle() + L"." + f.GetFileExt();
		m_fol.SetWindowText(s);
	}
}



BOOL CZeroFol::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"碧の軌跡 t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"아오의 궤적 t_bgm._dt", L"碧之轨迹 t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt", L"Ao no Kiseki t_bgm._dt"));
	m_msg.SetWindowText(LL14(L"碧の軌跡にはループデータ存在しないため、零の軌跡の「t_bgm..dt」を選択してください", L"Ao no Kiseki has no loop data; select Zero no Kiseki t_bgm._dt", L"Ao no Kiseki n'a pas de données de boucle; sélectionnez t_bgm._dt de Zero no Kiseki", L"Ao no Kiseki non ha dati loop; seleziona t_bgm._dt di Zero no Kiseki", L"Ao no Kiseki no tiene datos de bucle; seleccione t_bgm._dt de Zero no Kiseki", L"아오의 궤적에는 루프 데이터가 없으므로, 영의 궤적의 t_bgm._dt를 선택하세요", L"碧之轨迹没有循环数据，请选择零之轨迹的 t_bgm._dt", L"Ao no Kiseki لا يحتوي على بيانات حلقة; اختر t_bgm._dt من Zero no Kiseki", L"В Ao no Kiseki нет данных петли; выберите t_bgm._dt из Zero no Kiseki", L"Ao no Kiseki hat keine Loop-Daten; wählen Sie t_bgm._dt von Zero no Kiseki", L"Ao no Kiseki não tem dados de loop; selecione t_bgm._dt de Zero no Kiseki", L"Ao no Kiseki heeft geen loopgegevens; kies t_bgm._dt van Zero no Kiseki", L"Ao no Kiseki nie ma danych pętli; wybierz t_bgm._dt z Zero no Kiseki", L"Ao no Kiseki'de döngü verisi yok; Zero no Kiseki t_bgm._dt seçin"));
	m_fol.SetWindowText(savedata.zero);
	RECT r;
	GetWindowRect(&r);
	r.top += 600;
	r.bottom += 600;
	MoveWindow(&r);
	CCC_BringDialogToForeground(this);
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
