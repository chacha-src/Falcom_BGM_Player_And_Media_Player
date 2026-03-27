// PVI.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "PVI.h"


// CPVI ダイアログ

IMPLEMENT_DYNAMIC(CPVI, CCustomDialog)

CPVI::CPVI(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CPVI::IDD, pParent)
{

}

CPVI::~CPVI()
{
}

void CPVI::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT1, m_pvi);
	DDX_Control(pDX, IDOK, m_ok);
	DDX_Control(pDX, IDCANCEL, m_cdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CPVI, CCustomDialog)
	ON_BN_CLICKED(IDOK, &CPVI::OnBnClickedOk)
	ON_EN_CHANGE(IDC_EDIT1, &CPVI::OnEnChangeEdit1)
	cmn(CPVI);


// CPVI メッセージ ハンドラ

BOOL CPVI::OnInitDialog()
{
	CCustomDialog::OnInitDialog();

	SetWindowText(LL14(L"fmpmd.kpi PVIパス設定", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings", L"fmpmd.kpi PVI Path Settings"));
	SetDlgItemText(IDC_STATIC, LL14(L"PVIパス \\で必ず終わること　複数指定は ; で指定", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths.", L"PVI path must end with \\. Use ; for multiple paths."));
	SetDlgItemText(IDOK, LL14(
		L"適用",
		L"Apply",
		L"Appliquer",
		L"Applica",
		L"Aplicar",
		L"적용",
		L"应用",
		L"تطبيق",
		L"Применить",
		L"Anwenden",
		L"Aplicar",
		L"Toepassen",
		L"Zastosuj",
		L"Uygula"));
	SetDlgItemText(IDCANCEL, LL14(
		L"閉じる",
		L"Close",
		L"Fermer",
		L"Chiudi",
		L"Cerrar",
		L"닫기",
		L"关闭",
		L"إغلاق",
		L"Закрыть",
		L"Schliessen",
		L"Fechar",
		L"Sluiten",
		L"Zamknij",
		L"Kapat"));
	CString str = regload(_T(""));
	m_pvi.SetWindowText(str);
	RECT r;
	GetWindowRect(&r);
	r.top += 600;
	r.bottom += 600;
	MoveWindow(&r);
	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}

void CPVI::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	OnOK();
	CString str;
	m_pvi.GetWindowText(str);
	regsave(_T(""),str);
}

CString CPVI::regload(CString key)
{
	HKEY  hKey;              // キーのハンドル
	DWORD dwDisposition;     // 処理結果
	LONG  lret;              // 関数の戻り値
	CString strData;	// データを受け取るバッファ("DataName"の値)
	_TCHAR buf[20000];
	DWORD dwType=REG_SZ;		// 値の種類を受け取る
	DWORD dwSize;		// データのサイズを受け取る


	lret = RegCreateKeyEx(
		 HKEY_CURRENT_USER,		   // 現在オープンしているキーまたは定義済みキーのハンドル
		 _T("Software\\Kobarin's Soft\\KbMIDI Player\\SearchPath"),                 // サブキーの名前が入った文字列へのポインタ
		 0,                        // 予約パラメータ
		 _T(""),                       // キーのクラスが入った文字列へのポインタ
		 REG_OPTION_NON_VOLATILE,  // オプション
		 KEY_ALL_ACCESS,           // セキュリティのアクセスマスク
		 NULL,                     // ハンドルの子プロセスへの継承指定
		 &hKey,                    // 作成されたキーのハンドルを受け取る
		 &dwDisposition            // 処理結果
	);
	if(lret==ERROR_SUCCESS){
		RegQueryValueEx(hKey, _T("PVI"), NULL, &dwType, NULL, &dwSize);
		RegQueryValueEx(hKey, _T("PVI"), NULL, &dwType, (LPBYTE)buf, &dwSize);
		RegCloseKey(hKey);
		return buf;
	}else{
		return _T("");
	}
}

void CPVI::regsave(CString key, CString value)
{
	HKEY  hKey;              // キーのハンドル
	DWORD dwDisposition;     // 処理結果
	LONG  lret;              // 関数の戻り値
	CString strData;	// データを受け取るバッファ("DataName"の値)
	DWORD dwType=REG_SZ;		// 値の種類を受け取る
	DWORD dwSize;		// データのサイズを受け取る


	lret = RegCreateKeyEx(
		 HKEY_CURRENT_USER,		   // 現在オープンしているキーまたは定義済みキーのハンドル
		 _T("Software\\Kobarin's Soft\\KbMIDI Player\\SearchPath"),                 // サブキーの名前が入った文字列へのポインタ
		 0,                        // 予約パラメータ
		 _T(""),                       // キーのクラスが入った文字列へのポインタ
		 REG_OPTION_NON_VOLATILE,  // オプション
		 KEY_ALL_ACCESS,           // セキュリティのアクセスマスク
		 NULL,                     // ハンドルの子プロセスへの継承指定
		 &hKey,                    // 作成されたキーのハンドルを受け取る
		 &dwDisposition            // 処理結果
	);
	if(lret==ERROR_SUCCESS){
		RegSetValueEx(
		  hKey,         // 現在オープンしているキーのハンドル
		  _T("PVI"),    // 値の「名前」が入った文字列へのポインタ
		  0,            // 予約パラメータ
		  REG_SZ,       // 値の「種類」(NULLで終わる文字列はREG_SZ、32ビット値はREG_DWORD)
		  (CONST BYTE*)(LPCTSTR)value,  // 値の「データ」が入ったバッファへのポインタ
#if UNICODE
		  value.GetLength()*2 + 2         // lpDataのサイズを指定する
#else
		  value.GetLength() + 1         // lpDataのサイズを指定する
#endif
		);
		RegCloseKey(hKey);
		return;
	}else{
		return;
	}
}
static int CALLBACK EditWordBreakProc( LPTSTR , int , int , int );

void CPVI::OnEnChangeEdit1()
{
	// TODO:  これが RICHEDIT コントロールの場合、
	// まず、CCustomDialog::OnInitDialog() 関数をオーバーライドして、OR 状態の ENM_CHANGE
	// フラグをマスクに入れて、CRichEditCtrl().SetEventMask() を呼び出さない限り、
	// コントロールは、この通知を送信しません。

	// TODO:  ここにコントロール通知ハンドラ コードを追加してください。
    //CEdit * pEdit1 = (CEdit *)GetDlgItem(IDC_EDIT1);
    ::SendMessage(m_pvi.m_hWnd, EM_SETWORDBREAKPROC, 0, (LPARAM)EditWordBreakProc);
}

int CALLBACK EditWordBreakProc(LPTSTR lpch, int ichCurrent, int cch, int code)
{
    return (WB_ISDELIMITER == code) ? 0 : ichCurrent;
}
