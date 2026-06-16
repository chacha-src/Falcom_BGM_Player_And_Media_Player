// WavExport.cpp
//

#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "WavExport.h"

extern COggDlg* og;
extern void DoEvent();

namespace {

wchar_t WavExportMapInvalidFilenameChar(wchar_t c)
{
	switch (c) {
	case L'\\': return L'＼';
	case L'/':  return L'／';
	case L':':  return L'：';
	case L'*':  return L'＊';
	case L'?':  return L'？';
	case L'"':  return L'\xFF02';
	case L'<':  return L'＜';
	case L'>':  return L'＞';
	case L'|':  return L'｜';
	default:
		return (c < 32) ? L'_' : c;
	}
}

void WavExportTrimTrailingDotsAndSpaces(CString& s)
{
	while (s.GetLength() > 0) {
		const wchar_t c = s[s.GetLength() - 1];
		if (c == L'.' || c == L' ') s.Truncate(s.GetLength() - 1);
		else break;
	}
	if (s.IsEmpty()) s = L"_";
}

bool WavExportIsReservedDeviceName(const CString& upper)
{
	static const wchar_t* reserved[] = {
		L"CON", L"PRN", L"AUX", L"NUL",
		L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
		L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9",
		NULL
	};
	for (int i = 0; reserved[i]; ++i) {
		const int n = (int)wcslen(reserved[i]);
		if (upper.GetLength() == n && upper == reserved[i]) return true;
		if (upper.GetLength() > n && upper.Left(n) == reserved[i] && upper[n] == L'.') return true;
	}
	return false;
}

CString WavExportSanitizePathComponent(const CString& component)
{
	CString s = component;
	for (int i = 0; i < s.GetLength(); ++i)
		s.SetAt(i, WavExportMapInvalidFilenameChar(s[i]));
	WavExportTrimTrailingDotsAndSpaces(s);
	CString upper = s;
	upper.MakeUpper();
	if (WavExportIsReservedDeviceName(upper))
		s = L"_" + s;
	return s;
}

CString WavExportSanitizeFilePath(const CString& pathIn)
{
	if (pathIn.IsEmpty()) return pathIn;

	CString out;
	int i = 0;
	const int len = pathIn.GetLength();

	if (len >= 2 && pathIn[0] == L'\\' && pathIn[1] == L'\\') {
		out = L"\\\\";
		i = 2;
		int j = i;
		while (j < len && pathIn[j] != L'\\') ++j;
		if (j > i) out += WavExportSanitizePathComponent(pathIn.Mid(i, j - i));
		i = j;
	}
	else if (len >= 2 && pathIn[1] == L':') {
		out = pathIn.Left(2);
		i = 2;
	}

	if (i < len && pathIn[i] == L'\\') {
		out += L'\\';
		++i;
	}

	while (i < len) {
		int j = i;
		while (j < len && pathIn[j] != L'\\') ++j;
		CString part = pathIn.Mid(i, j - i);
		if (!part.IsEmpty())
			out += WavExportSanitizePathComponent(part);
		i = j;
		if (i < len && pathIn[i] == L'\\') {
			out += L'\\';
			++i;
		}
	}
	return out;
}

CString WavExportNormalizeOutputPath(const CString& pathIn)
{
	CString path = pathIn;
	if (path.Right(4).MakeLower() != L".wav") path += L".wav";
	return WavExportSanitizeFilePath(path);
}

} // namespace

IMPLEMENT_DYNAMIC(CWavExport, CCustomBlurDialogBase)

CWavExport::CWavExport(CWnd* pParent)
	: CCustomBlurDialogBase(CWavExport::IDD, pParent)
{
}

CWavExport::~CWavExport()
{
}

void CWavExport::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_WAVEXPORT_LOOP, m_loop);
	DDX_Control(pDX, IDC_WAVEXPORT_PATH, m_path);
	DDX_Control(pDX, IDC_WAVEXPORT_STATUS, m_status);
	DDX_Control(pDX, IDC_WAVEXPORT_LOOP_LABEL, m_loopLabel);
	DDX_Control(pDX, IDC_WAVEXPORT_PATH_LABEL, m_pathLabel);
	DDX_Control(pDX, IDC_WAVEXPORT_BROWSE, m_browse);
	DDX_Control(pDX, IDC_WAVEXPORT_EXEC, m_exec);
	DDX_Control(pDX, IDC_WAVEXPORT_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CWavExport, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_WAVEXPORT_EXEC, &CWavExport::OnBnClickedWavExportExec)
	ON_BN_CLICKED(IDC_WAVEXPORT_BROWSE, &CWavExport::OnBnClickedWavExportBrowse)
	ON_BN_CLICKED(IDC_WAVEXPORT_CLOSE, &CWavExport::OnBnClickedWavExportClose)
END_MESSAGE_MAP()

BOOL CWavExport::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"WAVへ出力", L"Export to WAV", L"Exporter en WAV", L"Esporta in WAV",
		L"Exportar a WAV", L"WAV로 내보내기", L"导出到WAV", L"تصدير إلى WAV",
		L"Экспорт в WAV", L"Als WAV exportieren", L"Exportar para WAV", L"Exporteren naar WAV",
		L"Eksportuj do WAV", L"WAV'e aktar"));
	m_loopLabel.SetWindowText(LL14(L"繰返し回数", L"Loop count", L"Nombre de boucles", L"Conteggio loop",
		L"Repeticiones", L"반복 횟수", L"循环次数", L"عدد التكرار",
		L"Количество повторов", L"Schleifenzahl", L"Repetições", L"Aantal herhalingen",
		L"Liczba powtórzeń", L"Döngü sayısı"));
	m_pathLabel.SetWindowText(LL14(L"出力ファイル名", L"Output file", L"Fichier de sortie", L"File di output",
		L"Archivo de salida", L"출력 파일", L"输出文件名", L"اسم الملف",
		L"Выходной файл", L"Ausgabedatei", L"Arquivo de saída", L"Uitvoerbestand",
		L"Plik wyjściowy", L"Çıktı dosyası"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi",
		L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schließen", L"Fechar", L"Sluiten",
		L"Zamknij", L"Kapat"));
	m_exec.SetWindowText(LL14(L"実行", L"Execute", L"Exécuter", L"Esegui",
		L"Ejecutar", L"실행", L"执行", L"تنفيذ",
		L"Выполнить", L"Ausführen", L"Executar", L"Uitvoeren",
		L"Wykonaj", L"Çalıştır"));
	m_loop.SetWindowText(L"1");
	CString defPath = pc.fol;
	int pos = defPath.ReverseFind(L'\\');
	if (pos >= 0) defPath = defPath.Left(pos + 1);
	defPath += pc.name;
	int dot = defPath.ReverseFind(L'.');
	if (dot >= 0) defPath = defPath.Left(dot);
	defPath += L".wav";
	defPath = WavExportNormalizeOutputPath(defPath);
	m_path.SetWindowText(defPath);
	m_status.SetWindowText(L"");
	return TRUE;
}

void CWavExport::OnBnClickedWavExportBrowse()
{
	CString path;
	m_path.GetWindowText(path);
	CFileDialog fd(FALSE, L"wav", path, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		LL14(L"WAVファイル (*.wav)|*.wav|すべてのファイル (*.*)|*.*||",
		L"WAV files (*.wav)|*.wav|All files (*.*)|*.*||",
		L"Fichiers WAV (*.wav)|*.wav|Tous les fichiers (*.*)|*.*||",
		L"File WAV (*.wav)|*.wav|Tutti i file (*.*)|*.*||",
		L"Archivos WAV (*.wav)|*.wav|Todos los archivos (*.*)|*.*||",
		L"WAV 파일 (*.wav)|*.wav|모든 파일 (*.*)|*.*||",
		L"WAV文件 (*.wav)|*.wav|所有文件 (*.*)|*.*||",
		L"ملفات WAV (*.wav)|*.wav|جميع الملفات (*.*)|*.*||",
		L"Файлы WAV (*.wav)|*.wav|Все файлы (*.*)|*.*||",
		L"WAV-Dateien (*.wav)|*.wav|Alle Dateien (*.*)|*.*||",
		L"Arquivos WAV (*.wav)|*.wav|Todos os arquivos (*.*)|*.*||",
		L"WAV-bestanden (*.wav)|*.wav|Alle bestanden (*.*)|*.*||",
		L"Pliki WAV (*.wav)|*.wav|Wszystkie pliki (*.*)|*.*||",
		L"WAV dosyalari (*.wav)|*.wav|Tum dosyalar (*.*)|*.*||"));
	if (fd.DoModal() == IDOK) {
		m_path.SetWindowText(fd.GetPathName());
	}
}

void CWavExport::OnBnClickedWavExportExec()
{
	CString loopStr, pathStr;
	m_loop.GetWindowText(loopStr);
	m_path.GetWindowText(pathStr);
	int loopCount = _tstoi(loopStr);
	if (loopCount < 1) loopCount = 1;
	if (pathStr.IsEmpty()) {
		m_status.SetWindowText(LL14(L"ファイル名を指定してください", L"Please specify file name", L"Veuillez specifier le nom du fichier",
			L"Specificare il nome del file", L"Especifique el nombre del archivo", L"파일 이름을 지정하세요", L"请指定文件名",
			L"يرجى تحديد اسم الملف", L"Укажите имя файла", L"Bitte Dateinamen angeben", L"Especifique o nome do arquivo",
			L"Geef bestandsnaam op", L"Podaj nazwę pliku", L"Dosya adini belirtin"));
		return;
	}
	CString path = WavExportNormalizeOutputPath(pathStr);
	CString pathForCompare = pathStr;
	if (pathForCompare.Right(4).MakeLower() != L".wav")
		pathForCompare += L".wav";
	if (path != pathForCompare)
		m_path.SetWindowText(path);
	m_status.SetWindowText(LL14(L"出力中...", L"Exporting...", L"Export en cours...", L"Esportazione...",
		L"Exportando...", L"내보내는 중...", L"导出中...", L"جاري التصدير...",
		L"Экспорт...", L"Exportiere...", L"Exportando...", L"Exporteren...",
		L"Eksportowanie...", L"Dışa aktarılıyor..."));
	m_exec.EnableWindow(FALSE);
	UpdateWindow();
	BOOL ok = og->ExportToWav(&pc, path, loopCount);
	m_exec.EnableWindow(TRUE);
	if (ok) {
		CString msg = LL14(L"完了", L"Complete", L"Termine", L"Completato",
			L"Completado", L"완료", L"完成", L"اكتمل",
			L"Завершено", L"Abgeschlossen", L"Concluido", L"Voltooid",
			L"Zakończono", L"Tamamlandı");
		m_status.SetWindowText(msg);
		MessageBox(msg, LL14(L"WAVへ出力", L"Export to WAV", L"Exporter en WAV", L"Esporta in WAV",
			L"Exportar a WAV", L"WAV로 내보내기", L"导出到WAV", L"تصدير إلى WAV",
			L"Экспорт в WAV", L"Als WAV exportieren", L"Exportar para WAV", L"Exporteren naar WAV",
			L"Eksportuj do WAV", L"WAV'e aktar"), MB_OK | MB_ICONINFORMATION);
	}
	else {
		CString msg = LL14(L"エラー", L"Error", L"Erreur", L"Errore",
			L"Error", L"오류", L"错误", L"خطأ",
			L"Ошибка", L"Fehler", L"Erro", L"Fout",
			L"Błąd", L"Hata");
		m_status.SetWindowText(msg);
		MessageBox(msg, LL14(L"WAVへ出力", L"Export to WAV", L"Exporter en WAV", L"Esporta in WAV",
			L"Exportar a WAV", L"WAV로 내보내기", L"导出到WAV", L"تصدير إلى WAV",
			L"Экспорт в WAV", L"Als WAV exportieren", L"Exportar para WAV", L"Exporteren naar WAV",
			L"Eksportuj do WAV", L"WAV'e aktar"), MB_OK | MB_ICONERROR);
	}
}

void CWavExport::OnBnClickedWavExportClose()
{
	EndDialog(IDCANCEL);
}
