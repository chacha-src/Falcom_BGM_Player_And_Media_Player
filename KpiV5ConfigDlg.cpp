#include "stdafx.h"
#include "ogg.h"
#include "KpiV5ConfigDlg.h"
#include "KpiV5ConfigStore.h"

#include <vector>

IMPLEMENT_DYNAMIC(CKpiV5ConfigDlg, CCustomBlurDialogBase)

CKpiV5ConfigDlg::CKpiV5ConfigDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CKpiV5ConfigDlg::IDD, pParent)
{
}

CKpiV5ConfigDlg::~CKpiV5ConfigDlg()
{
}

void CKpiV5ConfigDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT_KPI5CFG, m_text);
	DDX_Control(pDX, IDOK, m_ok);
	DDX_Control(pDX, IDCANCEL, m_cancel);
	DDX_Control(pDX, IDC_STATIC_KPI5CFG, m_cccc);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CKpiV5ConfigDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDOK, &CKpiV5ConfigDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CKpiV5ConfigDlg::OnBnClickedCancel)
	cmn(CKpiV5ConfigDlg);

BOOL CKpiV5ConfigDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(
		L"KPI ver5 専用パラメータ設定",
		L"KPI ver5 Dedicated Parameters",
		L"Parametres dedies KPI ver5",
		L"Parametri dedicati KPI ver5",
		L"Parametros dedicados KPI ver5",
		L"KPI ver5 전용 매개변수",
		L"KPI ver5 专用参数",
		L"معلمات KPI ver5 المخصصة",
		L"Специальные параметры KPI ver5",
		L"KPI ver5 Sonderparameter",
		L"Parametros dedicados KPI ver5",
		L"KPI ver5 speciale parameters",
		L"Parametry dedykowane KPI ver5",
		L"KPI ver5 ozel parametreler"));

	m_cccc.SetWindowText(LL14(
		L"1行ごとに plugin.section.key=value 形式で設定します。先頭 # はコメントです。",
		L"One line: plugin.section.key=value. Lines starting with # are comments.",
		L"Une ligne: plugin.section.key=value. Les lignes commencant par # sont des commentaires.",
		L"Una riga: plugin.section.key=value. Le righe che iniziano con # sono commenti.",
		L"Una linea: plugin.section.key=value. Las lineas que empiezan con # son comentarios.",
		L"한 줄 형식: plugin.section.key=value. #으로 시작하는 줄은 주석입니다.",
		L"每行格式: plugin.section.key=value。以 # 开头的行是注释。",
		L"سطر واحد: plugin.section.key=value. الاسطر التي تبدأ بـ # هي تعليقات.",
		L"Одна строка: plugin.section.key=value. Строки с # в начале считаются комментариями.",
		L"Eine Zeile: plugin.section.key=value. Zeilen mit # am Anfang sind Kommentare.",
		L"Uma linha: plugin.section.key=value. Linhas iniciadas por # sao comentarios.",
		L"Een regel: plugin.section.key=value. Regels die met # beginnen zijn opmerkingen.",
		L"Jeden wiersz: plugin.section.key=value. Wiersze zaczynajace sie od # to komentarze.",
		L"Tek satir: plugin.section.key=value. # ile baslayan satirlar yorumdur."));
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

	BuildInitialText();
	CCC_BringDialogToForeground(this);
	return TRUE;
}

void CKpiV5ConfigDlg::BuildInitialText()
{
	CString text;
	text += L"# kbpsf / kbpsf2 / kb2sf / kbusf / kbgsf / kbncsf / kbdsf / kbgym / kbsnsf / kbssf / kbvgm / kbgme / kbsid / kbnsfplug / kbfmoplmidi preset keys\r\n";
	const std::vector<KpiV5ConfigEntry>& entries = GetKpiV5KnownEntries();
	for (size_t i = 0; i < entries.size(); i++) {
		const KpiV5ConfigEntry& e = entries[i];
		std::wstring val = KpiV5GetStr(e.plugin, e.section, e.key, e.defaultValue ? e.defaultValue : L"");
		CString line;
		line.Format(L"%s.%s.%s=%s\r\n", e.plugin, e.section, e.key, val.c_str());
		text += line;
	}
	m_text.SetWindowText(text);
}

bool CKpiV5ConfigDlg::ParseAndSave()
{
	CString all;
	m_text.GetWindowText(all);
	int pos = 0;
	CString line = all.Tokenize(L"\r\n", pos);
	while (!line.IsEmpty()) {
		CString t = line;
		t.Trim();
		if (!t.IsEmpty() && t[0] != L'#') {
			int eq = t.Find(L'=');
			if (eq <= 0) return false;
			CString lhs = t.Left(eq);
			CString rhs = t.Mid(eq + 1);
			lhs.Trim(); rhs.Trim();
			int d1 = lhs.Find(L'.');
			int d2 = (d1 >= 0) ? lhs.Find(L'.', d1 + 1) : -1;
			if (d1 <= 0 || d2 <= d1 + 1 || d2 >= lhs.GetLength() - 1) return false;
			CString plugin = lhs.Left(d1);
			CString section = lhs.Mid(d1 + 1, d2 - d1 - 1);
			CString key = lhs.Mid(d2 + 1);
			KpiV5SetStr((const wchar_t*)plugin, (const wchar_t*)section, (const wchar_t*)key, (const wchar_t*)rhs);
		}
		line = all.Tokenize(L"\r\n", pos);
	}
	return true;
}

void CKpiV5ConfigDlg::OnBnClickedOk()
{
	if (!ParseAndSave()) {
		MessageBox(LL14(L"設定形式が不正です。plugin.section.key=value 形式で入力してください。", L"Invalid format. Use plugin.section.key=value.", L"Format invalide. Utilisez plugin.section.key=value.", L"Formato non valido. Usare plugin.section.key=value.", L"Formato no válido. Use plugin.section.key=value.", L"형식이 올바르지 않습니다. plugin.section.key=value 형식으로 입력하세요.", L"格式无效。请使用 plugin.section.key=value 格式。", L"تنسيق غير صالح. استخدم plugin.section.key=value.", L"Неверный формат. Используйте plugin.section.key=value.", L"Ungültiges Format. Verwenden Sie plugin.section.key=value.", L"Formato inválido. Use plugin.section.key=value.", L"Ongeldig formaat. Gebruik plugin.section.key=value.", L"Nieprawidłowy format. Użyj plugin.section.key=value.", L"Geçersiz biçim. plugin.section.key=value kullanın."),
			LL14(L"入力エラー", L"Input Error", L"Erreur de saisie", L"Errore di input", L"Error de entrada", L"입력 오류", L"输入错误", L"خطأ في الإدخال", L"Ошибка ввода", L"Eingabefehler", L"Erro de entrada", L"Invoerfout", L"Błąd wejścia", L"Giriş hatası"));
		return;
	}
	OnOK();
}

void CKpiV5ConfigDlg::OnBnClickedCancel()
{
	OnCancel();
}
