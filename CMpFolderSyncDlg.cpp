#include "stdafx.h"
#include "ogg.h"
#include "CMpFolderSyncDlg.h"
#include "PlayList.h"
#include <set>

extern CPlayList* pl;
extern save savedata;

IMPLEMENT_DYNAMIC(CMpFolderSyncDlg, CCustomBlurDialogBase)

CMpFolderSyncDlg::CMpFolderSyncDlg(CWnd* pParent, CString folder)
	: CCustomBlurDialogBase(CMpFolderSyncDlg::IDD, pParent)
	, m_folder(folder)
{
	m_folder.Trim();
	m_folder.Replace(_T('/'), _T('\\'));
	while (m_folder.GetLength() > 3 && m_folder.Right(1) == _T("\\"))
		m_folder = m_folder.Left(m_folder.GetLength() - 1);
}

CMpFolderSyncDlg::~CMpFolderSyncDlg()
{
}

void CMpFolderSyncDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MPS_MISSING_PL, m_lcDisk);
	DDX_Control(pDX, IDC_MPS_MISSING_DISK, m_lcPl);
	DDX_Control(pDX, IDC_MPS_ADD, m_add);
	DDX_Control(pDX, IDC_MPS_REMOVE, m_remove);
	DDX_Control(pDX, IDC_MPS_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CMpFolderSyncDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_MPS_ADD, &CMpFolderSyncDlg::OnBnClickedAdd)
	ON_BN_CLICKED(IDC_MPS_REMOVE, &CMpFolderSyncDlg::OnBnClickedRemove)
	ON_BN_CLICKED(IDC_MPS_CLOSE, &CMpFolderSyncDlg::OnBnClickedClose)
	ON_WM_CLOSE()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CMpFolderSyncDlg::IsAudioExt(const CString& path)
{
	CString e = path;
	e.MakeLower();
	const int dot = e.ReverseFind(_T('.'));
	if (dot < 0) return FALSE;
	e = e.Mid(dot);
	return e == _T(".ogg") || e == _T(".mp3") || e == _T(".wav") || e == _T(".flac")
		|| e == _T(".m4a") || e == _T(".aac") || e == _T(".wma") || e == _T(".opus")
		|| e == _T(".qull3");
}

void CMpFolderSyncDlg::CollectAudioFiles(const CString& folder, std::vector<CString>& out)
{
	CString pat = folder;
	if (pat.Right(1) != _T("\\")) pat += _T("\\");
	pat += _T("*.*");
	WIN32_FIND_DATA fd;
	HANDLE h = ::FindFirstFile(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.cFileName[0] == _T('.') &&
			(fd.cFileName[1] == 0 || (fd.cFileName[1] == _T('.') && fd.cFileName[2] == 0)))
			continue;
		CString full = folder;
		if (full.Right(1) != _T("\\")) full += _T("\\");
		full += fd.cFileName;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			CollectAudioFiles(full, out);
		} else if (IsAudioExt(full)) {
			out.push_back(NormalizePlaylistPath(full));
		}
	} while (::FindNextFile(h, &fd));
	::FindClose(h);
}

BOOL CMpFolderSyncDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);

	SetWindowText(LL14(
		L"フォルダ同期", L"Folder sync", L"Sync dossier", L"Sincronizza cartella", L"Sincronizar carpeta",
		L"폴더 동기화", L"文件夹同步", L"مزامنة المجلد", L"Синхронизация папки", L"Ordner-Sync",
		L"Sincronizar pasta", L"Map synchroniseren", L"Sync folderu", L"Klasor senkron"));
	SetDlgItemText(IDC_MPS_LBL1, LL14(
		L"PLに無い（ディスクのみ）", L"Not in PL (disk only)", L"Absent de PL (disque)", L"Non in PL (disco)", L"No en PL (disco)",
		L"PL에 없음(디스크만)", L"不在播放列表（仅磁盘）", L"ليس في القائمة (القرص)", L"Нет в PL (только диск)", L"Nicht in PL (nur Disk)",
		L"Nao na PL (so disco)", L"Niet in PL (alleen schijf)", L"Brak w PL (tylko dysk)", L"PL'de yok (sadece disk)"));
	SetDlgItemText(IDC_MPS_LBL2, LL14(
		L"diskに無い（PLのみ）", L"Not on disk (PL only)", L"Absent du disque (PL)", L"Non su disco (solo PL)", L"No en disco (solo PL)",
		L"디스크에 없음(PL만)", L"不在磁盘（仅播放列表）", L"ليس على القرص (القائمة)", L"Нет на диске (только PL)", L"Nicht auf Disk (nur PL)",
		L"Nao no disco (so PL)", L"Niet op schijf (alleen PL)", L"Brak na dysku (tylko PL)", L"Diskte yok (sadece PL)"));
	SetDlgItemText(IDC_MPS_ADD, LL14(
		L"追加", L"Add", L"Ajouter", L"Aggiungi", L"Anadir",
		L"추가", L"添加", L"إضافة", L"Добавить", L"Hinzufügen",
		L"Adicionar", L"Toevoegen", L"Dodaj", L"Ekle"));
	SetDlgItemText(IDC_MPS_REMOVE, LL14(
		L"削除", L"Remove", L"Supprimer", L"Rimuovi", L"Quitar",
		L"삭제", L"删除", L"إزالة", L"Удалить", L"Entfernen",
		L"Remover", L"Verwijderen", L"Usun", L"Kaldir"));
	SetDlgItemText(IDC_MPS_CLOSE, LL14(
		L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
		L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen",
		L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));

	m_add.SetGradation(RGB(200, 240, 200), RGB(140, 210, 150), 0, TRUE);
	m_remove.SetGradation(RGB(255, 220, 225), RGB(255, 170, 185), 0, TRUE);
	m_close.SetGradation(RGB(235, 235, 240), RGB(200, 200, 210), 0, TRUE);

	m_lcDisk.SetExtendedStyle(m_lcDisk.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_lcPl.SetExtendedStyle(m_lcPl.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_lcDisk.InsertColumn(0, LL14(L"パス", L"Path", L"Chemin", L"Percorso", L"Ruta", L"경로", L"路径", L"المسار", L"Путь", L"Pfad", L"Caminho", L"Pad", L"Sciezka", L"Yol"), LVCFMT_LEFT, 230, 0);
	m_lcPl.InsertColumn(0, LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"), LVCFMT_LEFT, 230, 0);

	Scan();
	RebuildLists();
	UpdateStatus();

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_NOPREFIX);
	m_tooltip.AddTool(&m_add, LL14(L"ディスクのみの曲をプレイリストへ追加。", L"Add disk-only tracks to the playlist.", L"Ajouter les pistes disque-only.", L"Aggiungi brani solo su disco.", L"Anadir pistas solo en disco.", L"디스크만 있는 곡을 PL에 추가.", L"将仅磁盘曲目加入播放列表。", L"إضافة المقاطع الموجودة على القرص فقط.", L"Добавить треки только с диска.", L"Nur-auf-Disk-Titel zur Playlist.", L"Adicionar faixas so no disco.", L"Schijf-only nummers toevoegen.", L"Dodaj utwory tylko z dysku.", L"Sadece diskteki parcalari listeye ekle."));
	m_tooltip.AddTool(&m_remove, LL14(L"ディスクに無いプレイリスト項目を削除。", L"Remove playlist entries missing on disk.", L"Supprimer les entrees absentes du disque.", L"Rimuovi voci assenti dal disco.", L"Quitar entradas ausentes en disco.", L"디스크에 없는 PL 항목 삭제.", L"删除磁盘上不存在的播放列表项。", L"حذف عناصر القائمة الغائبة عن القرص.", L"Удалить записи без файла на диске.", L"PL-Einträge ohne Datei entfernen.", L"Remover entradas ausentes no disco.", L"PL-items zonder schijfbestand verwijderen.", L"Usun wpisy PL bez pliku na dysku.", L"Diskte olmayan liste ogelerini sil."));
	m_tooltip.AddTool(&m_close, LL14(L"閉じる。", L"Close.", L"Fermer.", L"Chiudi.", L"Cerrar.", L"닫기.", L"关闭。", L"إغلاق.", L"Закрыть.", L"Schließen.", L"Fechar.", L"Sluiten.", L"Zamknij.", L"Kapat."));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 8000);

	return TRUE;
}

BOOL CMpFolderSyncDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CMpFolderSyncDlg::Scan()
{
	m_diskOnly.clear();
	m_plOnly.clear();
	if (m_folder.IsEmpty()) return;

	std::vector<CString> diskFiles;
	CollectAudioFiles(m_folder, diskFiles);

	CString root = NormalizePlaylistPath(m_folder);
	root.MakeLower();
	if (root.Right(1) != _T("\\")) root += _T("\\");

	std::set<CString> plPaths;
	if (pl && pl->pc) {
		for (int i = 0; i < pl->playcnt; ++i) {
			CString p = NormalizePlaylistPath(pl->pc[i].fol);
			CString plower = p;
			plower.MakeLower();
			if (plower.GetLength() >= root.GetLength()
				&& plower.Left(root.GetLength()) == root) {
				plPaths.insert(plower);
				const CString phys = PlPhysicalMediaPath(p);
				if (!PathFileExists(phys))
					m_plOnly.push_back(i);
			}
		}
	}

	for (const CString& f : diskFiles) {
		CString key = f;
		key.MakeLower();
		if (plPaths.find(key) == plPaths.end())
			m_diskOnly.push_back(f);
	}
}

void CMpFolderSyncDlg::RebuildLists()
{
	m_lcDisk.SetRedraw(FALSE);
	m_lcPl.SetRedraw(FALSE);
	m_lcDisk.DeleteAllItems();
	m_lcPl.DeleteAllItems();
	for (int i = 0; i < (int)m_diskOnly.size(); ++i) {
		const int row = m_lcDisk.InsertItem(i, m_diskOnly[i]);
		m_lcDisk.SetItemData(row, (DWORD_PTR)i);
	}
	for (int i = 0; i < (int)m_plOnly.size(); ++i) {
		const int pcIdx = m_plOnly[i];
		CString name = (pl && pl->pc && pcIdx >= 0 && pcIdx < pl->playcnt) ? pl->pc[pcIdx].name : _T("?");
		const int row = m_lcPl.InsertItem(i, name);
		m_lcPl.SetItemData(row, (DWORD_PTR)pcIdx);
	}
	m_lcDisk.SetRedraw(TRUE);
	m_lcPl.SetRedraw(TRUE);
	m_lcDisk.Invalidate();
	m_lcPl.Invalidate();
}

void CMpFolderSyncDlg::UpdateStatus()
{
	CString s;
	s.Format(LL14(
		L"ディスクのみ %d / PLのみ %d", L"Disk only %d / PL only %d", L"Disque seul %d / PL seul %d", L"Solo disco %d / solo PL %d", L"Solo disco %d / solo PL %d",
		L"디스크만 %d / PL만 %d", L"仅磁盘 %d / 仅列表 %d", L"القرص فقط %d / القائمة فقط %d", L"Только диск %d / только PL %d", L"Nur Disk %d / nur PL %d",
		L"So disco %d / so PL %d", L"Alleen schijf %d / alleen PL %d", L"Tylko dysk %d / tylko PL %d", L"Sadece disk %d / sadece PL %d"),
		(int)m_diskOnly.size(), (int)m_plOnly.size());
	SetDlgItemText(IDC_MPS_STATUS, s);
}

void CMpFolderSyncDlg::OnBnClickedAdd()
{
	if (!pl) return;
	int added = 0;
	POSITION pos = m_lcDisk.GetFirstSelectedItemPosition();
	while (pos) {
		const int row = m_lcDisk.GetNextSelectedItem(pos);
		const int idx = (int)m_lcDisk.GetItemData(row);
		if (idx < 0 || idx >= (int)m_diskOnly.size()) continue;
		pl->AddFilePath(m_diskOnly[idx]);
		added++;
	}
	if (added <= 0) {
		MessageBox(LL14(
			L"追加するファイルを選択してください。", L"Select files to add.", L"Selectionnez des fichiers.", L"Seleziona i file.", L"Seleccione archivos.",
			L"추가할 파일을 선택하세요.", L"请选择要添加的文件。", L"حدد ملفات للإضافة.", L"Выберите файлы.", L"Dateien auswaehlen.",
			L"Selecione arquivos.", L"Selecteer bestanden.", L"Wybierz pliki.", L"Eklenecek dosyalari secin."),
			LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
				L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdzenie", L"Onay"),
			MB_ICONINFORMATION);
		return;
	}
	pl->Save();
	if (pl->m_lc.GetSafeHwnd())
		pl->m_lc.SetItemCount(pl->playcnt);
	Scan();
	RebuildLists();
	UpdateStatus();
}

void CMpFolderSyncDlg::OnBnClickedRemove()
{
	if (!pl) return;
	std::vector<int> indices;
	POSITION pos = m_lcPl.GetFirstSelectedItemPosition();
	while (pos) {
		const int row = m_lcPl.GetNextSelectedItem(pos);
		const int pcIdx = (int)m_lcPl.GetItemData(row);
		if (pcIdx >= 0) indices.push_back(pcIdx);
	}
	if (indices.empty()) {
		MessageBox(LL14(
			L"削除する項目を選択してください。", L"Select items to remove.", L"Selectionnez des elements.", L"Seleziona elementi.", L"Seleccione elementos.",
			L"삭제할 항목을 선택하세요.", L"请选择要删除的项。", L"حدد عناصر للإزالة.", L"Выберите элементы.", L"Eintraege auswaehlen.",
			L"Selecione itens.", L"Selecteer items.", L"Wybierz pozycje.", L"Silinecek oegeleri secin."),
			LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
				L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdzenie", L"Onay"),
			MB_ICONINFORMATION);
		return;
	}
	CString msg;
	msg.Format(LL14(
		L"%d 件をプレイリストから削除しますか？", L"Remove %d item(s) from the playlist?", L"Supprimer %d element(s) ?", L"Rimuovere %d elemento/i?", L"¿Quitar %d elemento(s)?",
		L"%d개를 재생목록에서 삭제할까요?", L"从播放列表删除 %d 项？", L"إزالة %d من القائمة؟", L"Удалить %d из плейлиста?", L"%d Eintrag/Eintraege entfernen?",
		L"Remover %d da playlist?", L"%d uit playlist verwijderen?", L"Usunac %d z playlisty?", L"%d oge listeden silinsin mi?"), (int)indices.size());
	if (MessageBox(msg,
		LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
			L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdzenie", L"Onay"),
		MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	pl->DelByIndices(indices);
	Scan();
	RebuildLists();
	UpdateStatus();
}

void CMpFolderSyncDlg::OnBnClickedClose()
{
	EndDialog(IDCANCEL);
}

void CMpFolderSyncDlg::OnClose()
{
	OnBnClickedClose();
}

HBRUSH CMpFolderSyncDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
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
