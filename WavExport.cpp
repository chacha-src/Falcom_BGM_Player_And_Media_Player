// WavExport.cpp
//

#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "WavExport.h"
#include "DecodeProgress.h"
#include "ExportTagUi.h"
#include <ShlObj.h>

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

CString WavExportBaseNameFromItem(const playlistdata0& item)
{
	CString name = item.name;
	if (name.IsEmpty()) {
		CString fol = item.fol;
		const int pos = fol.ReverseFind(L'\\');
		if (pos >= 0) name = fol.Mid(pos + 1);
		else name = fol;
	}
	const int dot = name.ReverseFind(L'.');
	if (dot >= 0) name = name.Left(dot);
	return name;
}

CString WavExportDefaultFolderFromPc(const playlistdata0& item)
{
	CString defPath = item.fol;
	const int pos = defPath.ReverseFind(L'\\');
	if (pos >= 0) defPath = defPath.Left(pos + 1);
	return defPath;
}

CString WavExportOutputPathForItem(const CString& folderIn, const playlistdata0& item)
{
	CString folder = folderIn;
	if (!folder.IsEmpty() && folder[folder.GetLength() - 1] != L'\\')
		folder += L'\\';
	return WavExportNormalizeOutputPath(folder + WavExportBaseNameFromItem(item) + L".wav");
}

CString WavExportDefaultOutputPath(const playlistdata0& item)
{
	return WavExportOutputPathForItem(WavExportDefaultFolderFromPc(item), item);
}

bool WavExportBrowseFolder(CWnd* owner, CString& outFolder)
{
	BROWSEINFO bi = {};
	bi.hwndOwner = owner ? owner->GetSafeHwnd() : NULL;
	bi.lpszTitle = LL14(L"出力フォルダを選択", L"Select output folder", L"Choisir le dossier de sortie", L"Scegli cartella di output",
		L"Seleccionar carpeta de salida", L"출력 폴더 선택", L"选择输出文件夹", L"اختر مجلد الإخراج",
		L"Выберите папку вывода", L"Ausgabeordner wählen", L"Selecionar pasta de saída", L"Selecteer uitvoermap",
		L"Wybierz folder wyjściowy", L"Çıktı klasörünü seç");
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
	if (!pidl) return false;
	wchar_t path[MAX_PATH] = {};
	const BOOL got = SHGetPathFromIDList(pidl, path);
	CoTaskMemFree(pidl);
	if (!got || path[0] == L'\0') return false;
	outFolder = path;
	return true;
}

} // namespace

IMPLEMENT_DYNAMIC(CWavExport, CCustomBlurDialogBase)

CWavExport::CWavExport(CWnd* pParent)
	: CCustomBlurDialogBase(CWavExport::IDD, pParent)
	, multiFile(false)
	, m_coverBmp(NULL)
{
}

CWavExport::~CWavExport()
{
	if (m_coverBmp) {
		::DeleteObject(m_coverBmp);
		m_coverBmp = NULL;
	}
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
	DDX_Control(pDX, IDC_WAVEXPORT_FADE, m_fadeCheck);
	DDX_Control(pDX, IDC_WAVEXPORT_FADE_SEC, m_fadeSec);
	DDX_Control(pDX, IDC_WAVEXPORT_FADE_LABEL, m_fadeLabel);
	DDX_Control(pDX, IDC_WAVEXPORT_TRIM, m_trimCheck);
	DDX_Control(pDX, IDC_WAVEXPORT_TRIM_SEC, m_trimSec);
	DDX_Control(pDX, IDC_WAVEXPORT_TRIM_LABEL, m_trimLabel);
	DDX_Control(pDX, IDC_WAVEXPORT_COPY_TAGS, m_copyTags);
	DDX_Control(pDX, IDC_WAVEXPORT_TITLE_L, m_titleL);
	DDX_Control(pDX, IDC_WAVEXPORT_TITLE, m_title);
	DDX_Control(pDX, IDC_WAVEXPORT_ARTIST_L, m_artistL);
	DDX_Control(pDX, IDC_WAVEXPORT_ARTIST, m_artist);
	DDX_Control(pDX, IDC_WAVEXPORT_ALBUM_L, m_albumL);
	DDX_Control(pDX, IDC_WAVEXPORT_ALBUM, m_album);
	DDX_Control(pDX, IDC_WAVEXPORT_COVER_L, m_coverL);
	DDX_Control(pDX, IDC_WAVEXPORT_COVER_PIC, m_coverPic);
	DDX_Control(pDX, IDC_WAVEXPORT_COVER, m_cover);
	DDX_Control(pDX, IDC_WAVEXPORT_COVER_CLEAR, m_coverClear);
}

BEGIN_MESSAGE_MAP(CWavExport, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_WAVEXPORT_EXEC, &CWavExport::OnBnClickedWavExportExec)
	ON_BN_CLICKED(IDC_WAVEXPORT_BROWSE, &CWavExport::OnBnClickedWavExportBrowse)
	ON_BN_CLICKED(IDC_WAVEXPORT_CLOSE, &CWavExport::OnBnClickedWavExportClose)
	ON_BN_CLICKED(IDC_WAVEXPORT_COVER_CLEAR, &CWavExport::OnBnClickedCoverClear)
	ON_WM_DROPFILES()
END_MESSAGE_MAP()

BOOL CWavExport::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	SetWindowText(LL14(L"WAVへ出力", L"Export to WAV", L"Exporter en WAV", L"Esporta in WAV",
		L"Exportar a WAV", L"WAV로 내보내기", L"导出到WAV", L"تصدير إلى WAV",
		L"Экспорт в WAV", L"Als WAV exportieren", L"Exportar para WAV", L"Exporteren naar WAV",
		L"Eksportuj do WAV", L"WAV'e aktar"));
	m_loopLabel.SetWindowText(LL14(L"繰返し回数", L"Loop count", L"Nombre de boucles", L"Conteggio loop",
		L"Repeticiones", L"반복 횟수", L"循环次数", L"عدد التكرار",
		L"Количество повторов", L"Schleifenzahl", L"Repetições", L"Aantal herhalingen",
		L"Liczba powtórzeń", L"Döngü sayısı"));
	if (multiFile) {
		m_pathLabel.SetWindowText(LL14(L"出力フォルダ", L"Output folder", L"Dossier de sortie", L"Cartella di output",
			L"Carpeta de salida", L"출력 폴더", L"输出文件夹", L"مجلد الإخراج",
			L"Папка вывода", L"Ausgabeordner", L"Pasta de saída", L"Uitvoermap",
			L"Folder wyjściowy", L"Çıktı klasörü"));
	}
	else {
		m_pathLabel.SetWindowText(LL14(L"出力ファイル名", L"Output file", L"Fichier de sortie", L"File di output",
			L"Archivo de salida", L"출력 파일", L"输出文件名", L"اسم الملف",
			L"Выходной файл", L"Ausgabedatei", L"Arquivo de saída", L"Uitvoerbestand",
			L"Plik wyjściowy", L"Çıktı dosyası"));
	}
	m_fadeCheck.SetWindowText(LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza",
		L"Fundido", L"페이드 아웃", L"淡出", L"تلاشي",
		L"Затухание", L"Ausblenden", L"Fade out", L"Fade-out",
		L"Wyciszanie", L"Solma"));
	m_fadeLabel.SetWindowText(LL14(L"秒", L"sec", L"sec", L"sec",
		L"seg", L"초", L"秒", L"ث",
		L"сек", L"Sek", L"seg", L"sec",
		L"sek", L"sn"));
	m_trimCheck.SetWindowText(LL14(L"先頭無音カット", L"Trim leading silence", L"Couper silence initial", L"Taglia silenzio iniziale",
		L"Cortar silencio inicial", L"앞 무음 제거", L"切除开头静音", L"قص الصمت الابتدائي",
		L"Обрезать нач. тишину", L"Stille am Anfang kürzen", L"Cortar silêncio inicial", L"Stilte begin trimmen",
		L"Przytnij ciszę na początku", L"Baştaki sessizliği kes"));
	m_trimLabel.SetWindowText(LL14(L"保持秒", L"Keep sec", L"Garder sec", L"Mantieni sec",
		L"Mantener seg", L"유지 초", L"保留秒", L"احتفظ ث",
		L"Оставить сек", L"Behalten Sek", L"Manter seg", L"Bewaar sec",
		L"Zostaw sek", L"Tut sn"));
	m_copyTags.SetWindowText(LL14(L"タグとジャケットをコピー", L"Copy tags and cover art", L"Copier les tags et la pochette", L"Copia tag e copertina",
		L"Copiar etiquetas y portada", L"태그와 재킷 복사", L"复制标签和封面", L"نسخ الوسوم والغلاف",
		L"Копировать теги и обложку", L"Tags und Cover kopieren", L"Copiar tags e capa", L"Tags en hoes kopiëren",
		L"Kopiuj tagi i okładkę", L"Etiketleri ve kapağı kopyala"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi",
		L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schließen", L"Fechar", L"Sluiten",
		L"Zamknij", L"Kapat"));
	m_exec.SetWindowText(LL14(L"実行", L"Execute", L"Exécuter", L"Esegui",
		L"Ejecutar", L"실행", L"执行", L"تنفيذ",
		L"Выполнить", L"Ausführen", L"Executar", L"Uitvoeren",
		L"Wykonaj", L"Çalıştır"));
	m_loop.SetWindowText(L"1");
	int fadeSec = savedata.wav_export_fade_sec;
	if (fadeSec <= 0) fadeSec = 15;
	int trimKeep = savedata.wav_export_trim_keep_sec;
	if (trimKeep <= 0) trimKeep = 1;
	CString s;
	s.Format(L"%d", fadeSec);
	m_fadeSec.SetWindowText(s);
	s.Format(L"%d", trimKeep);
	m_trimSec.SetWindowText(s);
	m_fadeCheck.SetCheck(savedata.wav_export_fade ? BST_CHECKED : BST_UNCHECKED);
	m_trimCheck.SetCheck(savedata.wav_export_trim_lead ? BST_CHECKED : BST_UNCHECKED);
	m_copyTags.SetCheck(savedata.wav_export_copy_tags ? BST_CHECKED : BST_UNCHECKED);
	if (multiFile) {
		m_path.SetWindowText(WavExportDefaultFolderFromPc(pc));
	}
	else {
		m_path.SetWindowText(WavExportDefaultOutputPath(pc));
	}
	m_status.SetWindowText(L"");
	if (CWnd* pPh = GetDlgItem(IDC_WAVEXPORT_PROGRESS)) {
		CRect rc; pPh->GetWindowRect(&rc); ScreenToClient(&rc);
		pPh->DestroyWindow();
		m_progress.Create(WS_CHILD | WS_VISIBLE, rc, this, IDC_WAVEXPORT_PROGRESS);
		m_progress.SetRange(0, 100);
		m_progress.SetPos(0);
		m_progress.SetShowPercent(TRUE);
		m_progress.SetColors(RGB(255, 236, 246), RGB(255, 170, 200), RGB(200, 120, 220));
		m_progress.SetAeroMode(CCC_IsAeroEnabled());
	}
	if (CWnd* pProgL = GetDlgItem(IDC_WAVEXPORT_PROG_L))
		pProgL->SetWindowText(LL14(L"進捗", L"Progress", L"Progression", L"Avanzamento", L"Progreso", L"진행", L"进度", L"Progress", L"Прогресс", L"Fortschritt", L"Progresso", L"Voortgang", L"Postep", L"Ilerleme"));
	ExportTagUi_InitFields(multiFile, pc, m_title, m_artist, m_album,
		m_titleL, m_artistL, m_albumL, m_coverL, m_coverPic, m_cover, m_coverClear, m_coverPath, m_coverBmp);
	DragAcceptFiles(TRUE);
	return TRUE;
}

void CWavExport::OnBnClickedCoverClear()
{
	ExportTagUi_ClearCover(m_coverPic, m_cover, m_coverPath, m_coverBmp);
}

void CWavExport::OnDropFiles(HDROP hDropInfo)
{
	ExportTagUi_OnDropFiles(hDropInfo, m_coverPic, m_cover, m_coverPath, m_coverBmp);
}

void CWavExport::ExportProgressThunk(int percent, LPCTSTR status, void* user)
{
	CWavExport* self = reinterpret_cast<CWavExport*>(user);
	if (!self || !::IsWindow(self->GetSafeHwnd())) return;
	if (self->m_progress.GetSafeHwnd()) {
		self->m_progress.SetPos(percent);
		self->m_progress.Invalidate(FALSE);
		self->m_progress.UpdateWindow();
	}
	if (status && status[0])
		self->m_status.SetWindowText(status);
	else if (CWnd* p = self->GetDlgItem(IDC_WAVEXPORT_PROG_L)) {
		CString s;
		s.Format(L"%d%%", percent);
		p->SetWindowText(s);
	}
	// 全メッセージを汲み出すと timer/Restart 再入で export が壊れる。描画だけ通す。
	MSG msg;
	while (::PeekMessage(&msg, self->GetSafeHwnd(), WM_PAINT, WM_PAINT, PM_REMOVE))
		::DispatchMessage(&msg);
}

void CWavExport::OnBnClickedWavExportBrowse()
{
	CString path;
	m_path.GetWindowText(path);
	if (multiFile) {
		if (WavExportBrowseFolder(this, path))
			m_path.SetWindowText(path);
		return;
	}
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
	CString loopStr, pathStr, fadeStr, trimStr;
	m_loop.GetWindowText(loopStr);
	m_path.GetWindowText(pathStr);
	m_fadeSec.GetWindowText(fadeStr);
	m_trimSec.GetWindowText(trimStr);
	int loopCount = _tstoi(loopStr);
	if (loopCount < 1) loopCount = 1;
	int fadeSec = _tstoi(fadeStr);
	if (fadeSec < 1) fadeSec = 15;
	int trimKeepSec = _tstoi(trimStr);
	if (trimKeepSec < 0) trimKeepSec = 1;

	WavExportOptions opts = {};
	opts.fadeEnable = m_fadeCheck.GetCheck() ? 1 : 0;
	opts.fadeSec = fadeSec;
	opts.trimLeadEnable = m_trimCheck.GetCheck() ? 1 : 0;
	opts.trimKeepSec = trimKeepSec;
	savedata.wav_export_fade = opts.fadeEnable;
	savedata.wav_export_fade_sec = opts.fadeSec;
	savedata.wav_export_trim_lead = opts.trimLeadEnable;
	savedata.wav_export_trim_keep_sec = opts.trimKeepSec;
	savedata.wav_export_copy_tags = m_copyTags.GetCheck() ? 1 : 0;
	ExportTagUi_Collect(multiFile, savedata.wav_export_copy_tags, m_title, m_artist, m_album, m_coverPath, opts);

	if (pathStr.IsEmpty()) {
		m_status.SetWindowText(multiFile
			? LL14(L"フォルダを指定してください", L"Please specify folder", L"Veuillez specifier le dossier", L"Specificare la cartella",
				L"Especifique la carpeta", L"폴더를 지정하세요", L"请指定文件夹", L"يرجى تحديد المجلد",
				L"Укажите папку", L"Bitte Ordner angeben", L"Especifique a pasta", L"Geef map op",
				L"Podaj folder", L"Klasor belirtin")
			: LL14(L"ファイル名を指定してください", L"Please specify file name", L"Veuillez specifier le nom du fichier",
				L"Specificare il nome del file", L"Especifique el nombre del archivo", L"파일 이름을 지정하세요", L"请指定文件名",
				L"يرجى تحديد اسم الملف", L"Укажите имя файла", L"Bitte Dateinamen angeben", L"Especifique o nome do arquivo",
				L"Geef bestandsnaam op", L"Podaj nazwę pliku", L"Dosya adini belirtin"));
		return;
	}
	// 再生中の状態が書き出しに混ざらないよう、先に停止する
	if (og) og->stop1();
	m_status.SetWindowText(LL14(L"出力中...", L"Exporting...", L"Export en cours...", L"Esportazione in corso...",
		L"Exportando...", L"내보내는 중...", L"导出中...", L"جاري التصدير...",
		L"Экспорт...", L"Exportiere...", L"Exportando...", L"Exporteren...",
		L"Eksportowanie...", L"Dışa aktarılıyor..."));
	m_exec.EnableWindow(FALSE);
	if (m_progress.GetSafeHwnd()) {
		m_progress.SetPos(0);
		m_progress.ShowWindow(SW_SHOW);
	}
	MpDecodeProgressReset();
	MpDecodeProgressSetPcmCap(95);
	MpDecodeProgressSetCb(&CWavExport::ExportProgressThunk, this);
	UpdateWindow();

	BOOL ok = TRUE;
	if (multiFile) {
		CString folder = pathStr;
		if (folder.Right(4).MakeLower() == L".wav") {
			int pos = folder.ReverseFind(L'\\');
			if (pos >= 0) folder = folder.Left(pos + 1);
		}
		if (!folder.IsEmpty() && folder[folder.GetLength() - 1] != L'\\')
			folder += L'\\';
		const size_t total = pcs.size();
		for (size_t i = 0; i < total; ++i) {
			CString outPath = WavExportOutputPathForItem(folder, pcs[i]);
			const int base = (int)((i * 100) / total);
			const int span = (int)(((i + 1) * 100) / total) - base;
			MpDecodeProgressSetSegment(base, span > 0 ? span : 1);
			MpDecodeProgressSetPcmCap(95);
			CString st;
			st.Format(LL14(L"出力中... (%d/%d)", L"Exporting... (%d/%d)", L"Export en cours... (%d/%d)", L"Esportazione... (%d/%d)",
				L"Exportando... (%d/%d)", L"내보내는 중... (%d/%d)", L"导出中... (%d/%d)", L"جاري التصدير... (%d/%d)",
				L"Экспорт... (%d/%d)", L"Exportiere... (%d/%d)", L"Exportando... (%d/%d)", L"Exporteren... (%d/%d)",
				L"Eksportowanie... (%d/%d)", L"Dışa aktarılıyor... (%d/%d)"),
				(int)(i + 1), (int)total);
			m_status.SetWindowText(st);
			UpdateWindow();
			DoEvent();
			ok = og->ExportToWav(&pcs[i], outPath, loopCount, &opts) && ok;
		}
	}
	else {
		CString path = WavExportNormalizeOutputPath(pathStr);
		CString pathForCompare = pathStr;
		if (pathForCompare.Right(4).MakeLower() != L".wav")
			pathForCompare += L".wav";
		if (path != pathForCompare)
			m_path.SetWindowText(path);
		MpDecodeProgressSetSegment(0, 100);
		MpDecodeProgressSetPcmCap(95);
		ok = og->ExportToWav(&pc, path, loopCount, &opts);
	}

	if (ok && m_progress.GetSafeHwnd())
		m_progress.SetPos(100);
	MpDecodeProgressClearCb();
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
