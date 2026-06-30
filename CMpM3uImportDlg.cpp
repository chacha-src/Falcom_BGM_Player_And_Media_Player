#include "stdafx.h"
#include "CMpM3uImportDlg.h"
#include "PlayList.h"

extern CPlayList* pl;
extern save savedata;

IMPLEMENT_DYNAMIC(CMpM3uImportDlg, CCustomBlurDialogBase)

CMpM3uImportDlg::CMpM3uImportDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CMpM3uImportDlg::IDD, pParent)
{
	m_opt.targetPlaylist = savedata.playlistnum;
	m_opt.createNew = FALSE;
	m_opt.utf8 = FALSE;
	m_opt.resolveRelative = TRUE;
	m_opt.skipMissing = TRUE;
	m_opt.skipDuplicates = FALSE;
}

CMpM3uImportDlg::~CMpM3uImportDlg()
{
}

void CMpM3uImportDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MPI_PLSEL, m_plsel);
	DDX_Control(pDX, IDC_MPI_UTF8, m_utf8);
	DDX_Control(pDX, IDC_MPI_RESOLVE, m_resolve);
	DDX_Control(pDX, IDC_MPI_SKIPMISS, m_skipMissing);
	DDX_Control(pDX, IDC_MPI_SKIPDUP, m_skipDup);
}

BEGIN_MESSAGE_MAP(CMpM3uImportDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_MPI_BROWSE, &CMpM3uImportDlg::OnBrowse)
	ON_BN_CLICKED(IDC_MPI_IMPORT, &CMpM3uImportDlg::OnImport)
	ON_WM_DROPFILES()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CMpM3uImportDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	DragAcceptFiles(TRUE);

	SetWindowText(LL14(L"プレイリストのインポート", L"Import playlist", L"Importer une liste", L"Importa playlist", L"Importar lista", L"재생목록 가져오기", L"导入播放列表", L"استيراد قائمة", L"Импорт плейлиста", L"Playlist importieren", L"Importar lista", L"Playlist importeren", L"Import listy", L"Listeyi ice aktar"));
	SetDlgItemText(IDC_MPI_FILE_L, LL14(L"ファイル:", L"File:", L"Fichier:", L"File:", L"Archivo:", L"파일:", L"文件:", L"ملف:", L"Файл:", L"Datei:", L"Arquivo:", L"Bestand:", L"Plik:", L"Dosya:"));
	SetDlgItemText(IDC_MPI_PL_L, LL14(L"追加先:", L"Target:", L"Cible:", L"Destinazione:", L"Destino:", L"대상:", L"目标:", L"الهدف:", L"Цель:", L"Ziel:", L"Destino:", L"Doel:", L"Cel:", L"Hedef:"));
	SetDlgItemText(IDC_MPI_UTF8, LL14(L"UTF-8として強制読み込む(通常は自動判定)", L"Force UTF-8 (auto-detect by default)", L"Forcer UTF-8 (auto par defaut)", L"Forza UTF-8 (auto predefinito)", L"Forzar UTF-8 (auto por defecto)", L"UTF-8 강제(기본은 자동 판별)", L"强制UTF-8(默认自动判定)", L"فرض UTF-8 (تلقائي افتراضيا)", L"Принудительно UTF-8 (иначе авто)", L"UTF-8 erzwingen (sonst Auto)", L"Forcar UTF-8 (auto por padrao)", L"UTF-8 forceren (standaard auto)", L"Wymus UTF-8 (domyslnie auto)", L"UTF-8 zorla (varsayilan otomatik)"));
	SetDlgItemText(IDC_MPI_RESOLVE, LL14(L"相対パスをプレイリスト基準で解決", L"Resolve relative paths from playlist", L"Resoudre chemins relatifs", L"Risolvi percorsi relativi", L"Resolver rutas relativas", L"상대 경로 해석", L"解析相对路径", L"حل المسارات النسبية", L"Разрешать относительные пути", L"Relative Pfade aufloesen", L"Resolver caminhos relativos", L"Relatieve paden oplossen", L"Rozwiaz sciezki wzgledne", L"Goreceli yollari coz"));
	SetDlgItemText(IDC_MPI_SKIPMISS, LL14(L"存在しないファイルをスキップ", L"Skip missing files", L"Ignorer fichiers absents", L"Salta file mancanti", L"Omitir archivos inexistentes", L"없는 파일 건너뛰기", L"跳过不存在的文件", L"تخطي الملفات المفقودة", L"Пропускать отсутствующие", L"Fehlende ueberspringen", L"Ignorar ausentes", L"Ontbrekende overslaan", L"Pomijaj brakujace", L"Eksik dosyalari atla"));
	SetDlgItemText(IDC_MPI_SKIPDUP, LL14(L"重複パスをスキップ", L"Skip duplicate paths", L"Ignorer doublons", L"Salta duplicati", L"Omitir duplicados", L"중복 건너뛰기", L"跳过重复路径", L"تخطي التكرار", L"Пропускать дубликаты", L"Duplikate ueberspringen", L"Ignorar duplicados", L"Duplicaten overslaan", L"Pomijaj duplikaty", L"Yinelenenleri atla"));
	SetDlgItemText(IDC_MPI_BROWSE, LL14(L"参照...", L"Browse...", L"Parcourir...", L"Sfoglia...", L"Examinar...", L"찾아보기...", L"浏览...", L"استعراض...", L"Обзор...", L"Durchsuchen...", L"Procurar...", L"Bladeren...", L"Przegladaj...", L"Gozat..."));
	SetDlgItemText(IDC_MPI_IMPORT, LL14(L"インポート", L"Import", L"Importer", L"Importa", L"Importar", L"가져오기", L"导入", L"استيراد", L"Импорт", L"Importieren", L"Importar", L"Importeren", L"Importuj", L"Ice aktar"));

	if (!m_filePath.IsEmpty())
		SetDlgItemText(IDC_MPI_FILE, m_filePath);

	m_utf8.SetCheck(m_opt.utf8 ? 1 : 0);
	m_resolve.SetCheck(m_opt.resolveRelative ? 1 : 0);
	m_skipMissing.SetCheck(m_opt.skipMissing ? 1 : 0);
	m_skipDup.SetCheck(m_opt.skipDuplicates ? 1 : 0);

	CCustomControlUtility::SetControlBackgroundColor(&m_plsel, COLOR_COMBO_BG);
	ReloadPlaylistCombo();
	return TRUE;
}

void CMpM3uImportDlg::ReloadPlaylistCombo()
{
	if (!pl || !::IsWindow(pl->m_listchange.GetSafeHwnd())) return;
	m_plsel.ResetContent();
	int n = pl->m_listchange.GetCount();
	for (int i = 0; i < n; i++) {
		CString s;
		pl->m_listchange.GetLBText(i, s);
		m_plsel.AddString(s);
	}
	int sel = m_opt.targetPlaylist;
	if (sel < 0 || sel >= n) sel = savedata.playlistnum;
	if (sel >= 0 && sel < n) m_plsel.SetCurSel(sel);
}

void CMpM3uImportDlg::LoadOptionsFromUi()
{
	int sel = m_plsel.GetCurSel();
	int n = m_plsel.GetCount();
	m_opt.createNew = (n > 0 && sel == n - 1);
	if (m_opt.createNew)
		m_opt.targetPlaylist = n - 1;
	else
		m_opt.targetPlaylist = (sel >= 0) ? sel : savedata.playlistnum;
	m_opt.utf8 = m_utf8.GetCheck() ? TRUE : FALSE;
	m_opt.resolveRelative = m_resolve.GetCheck() ? TRUE : FALSE;
	m_opt.skipMissing = m_skipMissing.GetCheck() ? TRUE : FALSE;
	m_opt.skipDuplicates = m_skipDup.GetCheck() ? TRUE : FALSE;
}

void CMpM3uImportDlg::OnBrowse()
{
	CFileDialog fd(TRUE,
		_T("m3u"),
		NULL,
		OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
		LL14(L"プレイリスト (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|すべて (*.*)|*.*||",
			L"Playlists (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|All (*.*)|*.*||",
			L"Listes (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|Tous (*.*)|*.*||",
			L"Playlist (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|Tutti (*.*)|*.*||",
			L"Listas (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|Todos (*.*)|*.*||",
			L"재생목록 (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|모든 (*.*)|*.*||",
			L"播放列表 (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|全部 (*.*)|*.*||",
			L"قوائم (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|الكل (*.*)|*.*||",
			L"Плейлисты (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|Все (*.*)|*.*||",
			L"Playlists (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|Alle (*.*)|*.*||",
			L"Listas (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|Todos (*.*)|*.*||",
			L"Playlists (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|Alle (*.*)|*.*||",
			L"Listy (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|Wszystkie (*.*)|*.*||",
			L"Listeler (*.m3u;*.m3u8;*.pls;*.xspf)|*.m3u;*.m3u8;*.pls;*.xspf|Tumu (*.*)|*.*||"),
		this);
	if (fd.DoModal() == IDOK)
		SetDlgItemText(IDC_MPI_FILE, fd.GetPathName());
}

void CMpM3uImportDlg::OnImport()
{
	CString path;
	GetDlgItemText(IDC_MPI_FILE, path);
	path.Trim();
	if (path.IsEmpty() || !MpIsPlaylistExtension(path)) {
		AfxMessageBox(LL14(L"プレイリストファイルを指定してください。", L"Please select a playlist file.", L"Veuillez selectionner un fichier.", L"Seleziona un file playlist.", L"Seleccione un archivo.", L"재생목록 파일을 지정하세요.", L"请指定播放列表文件。", L"يرجى تحديد ملف.", L"Укажите файл плейлиста.", L"Bitte Playlist-Datei waehlen.", L"Selecione um arquivo.", L"Selecteer een bestand.", L"Wybierz plik listy.", L"Lutfen bir dosya secin."));
		return;
	}
	LoadOptionsFromUi();
	int n = MpImportPlaylistFile(path, m_opt);
	if (n < 0) {
		AfxMessageBox(LL14(L"インポートに失敗しました。", L"Import failed.", L"Echec de l'import.", L"Importazione fallita.", L"Error al importar.", L"가져오기 실패.", L"导入失败。", L"فشل الاستيراد.", L"Ошибка импорта.", L"Import fehlgeschlagen.", L"Falha na importacao.", L"Importeren mislukt.", L"Import nieudany.", L"Ice aktarma basarisiz."));
		return;
	}
	CString msg;
	msg.Format(LL14(L"%d 曲を追加しました。", L"Added %d track(s).", L"%d piste(s) ajoutee(s).", L"Aggiunte %d tracce.", L"Se anadieron %d pistas.", L"%d곡 추가.", L"已添加 %d 首。", L"تمت إضافة %d.", L"Добавлено %d.", L"%d Titel hinzugefuegt.", L"%d faixa(s) adicionada(s).", L"%d nummer(s) toegevoegd.", L"Dodano %d utworow.", L"%d parca eklendi."), n);
	AfxMessageBox(msg);
	EndDialog(IDOK);
}

void CMpM3uImportDlg::OnDropFiles(HDROP hDropInfo)
{
	TCHAR path[MAX_PATH];
	if (::DragQueryFile(hDropInfo, 0, path, MAX_PATH) > 0) {
		CString p = path;
		if (MpIsPlaylistExtension(p))
			SetDlgItemText(IDC_MPI_FILE, p);
	}
	::DragFinish(hDropInfo);
}

HBRUSH CMpM3uImportDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CCustomBlurDialogBase::OnCtlColor(pDC, pWnd, nCtlColor);
	if (savedata.aero != 1) {
		if (m_brDlg.GetSafeHandle() == NULL)
			m_brDlg.CreateSolidBrush(COLOR_DIALOG_BG);
		if (nCtlColor == CTLCOLOR_DLG || nCtlColor == CTLCOLOR_STATIC)
			return m_brDlg;
	}
	return hbr;
}

BOOL MpShowM3uImportDialog(CWnd* pParent, const CString& pathHint)
{
	CMpM3uImportDlg dlg(pParent);
	dlg.m_filePath = pathHint;
	return dlg.DoModal() == IDOK;
}
