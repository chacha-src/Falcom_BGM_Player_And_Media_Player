#include "stdafx.h"
#include "ogg.h"
#include "CMissingFilesDlg.h"
#include "PlayList.h"
#include <algorithm>

extern save savedata;

enum {
	MF_COL_NO = 0,
	MF_COL_NAME = 1,
	MF_COL_PATH = 2,
};

#ifndef IDC_MF_INLINE_EDIT_DUMMY
#define IDC_MF_INLINE_EDIT_DUMMY 39990
#endif

IMPLEMENT_DYNAMIC(CMissingFilesListCtrl, CCustomListCtrl)

BEGIN_MESSAGE_MAP(CMissingFilesListCtrl, CCustomListCtrl)
END_MESSAGE_MAP()

void CMissingFilesListCtrl::BuildToolTipText(int row, int col, CString& out)
{
	out.Empty();
	if (!m_pItems || row < 0 || row >= (int)m_pItems->size())
		return;
	const MissingFileItem& it = (*m_pItems)[row];
	if (col == MF_COL_PATH || col < 0) {
		out = it.path.IsEmpty()
			? LL14(L"（パスなし）", L"(no path)", L"(aucun chemin)", L"(nessun percorso)",
				L"(sin ruta)", L"(경로 없음)", L"（无路径）", L"(بدون مسار)",
				L"(нет пути)", L"(kein Pfad)", L"(sem caminho)", L"(geen pad)",
				L"(brak sciezki)", L"(yol yok)")
			: it.path;
	} else if (col == MF_COL_NAME) {
		out = it.name;
	} else {
		out.Format(L"#%d", it.plIndex + 1);
	}
}

IMPLEMENT_DYNAMIC(CMissingFilesDlg, CCustomBlurDialogBase)

CMissingFilesDlg::CMissingFilesDlg(CPlayList* pPlayList, const std::vector<int>& missingIndices, CWnd* pParent)
	: CCustomBlurDialogBase(CMissingFilesDlg::IDD, pParent)
	, m_pPlayList(pPlayList)
	, m_selRow(-1)
	, m_bInlineEdit(FALSE)
	, m_inlineRow(-1)
{
	m_items.reserve(missingIndices.size());
	if (!m_pPlayList || !m_pPlayList->pc)
		return;
	for (int idx : missingIndices) {
		if (idx < 0 || idx >= m_pPlayList->playcnt)
			continue;
		MissingFileItem it;
		it.plIndex = idx;
		it.name = m_pPlayList->pc[idx].name;
		it.path = m_pPlayList->pc[idx].fol;
		it.sub = m_pPlayList->pc[idx].sub;
		m_items.push_back(it);
	}
}

CMissingFilesDlg::~CMissingFilesDlg()
{
	if (m_inlineEdit.GetSafeHwnd())
		m_inlineEdit.DestroyWindow();
}

void CMissingFilesDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MF_LIST, m_lc);
	DDX_Control(pDX, IDC_MF_PATH, m_path);
	DDX_Control(pDX, IDC_MF_BROWSE, m_browse);
	DDX_Control(pDX, IDC_MF_APPLY, m_apply);
	DDX_Control(pDX, IDC_MF_OPENFOL, m_openFol);
	DDX_Control(pDX, IDC_MF_DELETE, m_delete);
	DDX_Control(pDX, IDC_MF_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CMissingFilesDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_MF_BROWSE, &CMissingFilesDlg::OnBnClickedBrowse)
	ON_BN_CLICKED(IDC_MF_APPLY, &CMissingFilesDlg::OnBnClickedApply)
	ON_BN_CLICKED(IDC_MF_OPENFOL, &CMissingFilesDlg::OnBnClickedOpenFol)
	ON_BN_CLICKED(IDC_MF_DELETE, &CMissingFilesDlg::OnBnClickedDelete)
	ON_BN_CLICKED(IDC_MF_CLOSE, &CMissingFilesDlg::OnBnClickedClose)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_MF_LIST, &CMissingFilesDlg::OnLvnItemchangedList)
	ON_NOTIFY(NM_DBLCLK, IDC_MF_LIST, &CMissingFilesDlg::OnNMDblclkList)
	ON_NOTIFY(NM_CLICK, IDC_MF_LIST, &CMissingFilesDlg::OnNMClickList)
	ON_EN_KILLFOCUS(IDC_MF_INLINE_EDIT_DUMMY, &CMissingFilesDlg::OnInlineEditKillFocus)
	ON_WM_CLOSE()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CMissingFilesDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);

	SetWindowText(LL14(
		L"存在しないファイルの確認",
		L"Review missing files",
		L"Verifier les fichiers manquants",
		L"Verifica file mancanti",
		L"Revisar archivos inexistentes",
		L"없는 파일 확인",
		L"确认不存在的文件",
		L"مراجعة الملفات المفقودة",
		L"Проверка отсутствующих файлов",
		L"Fehlende Dateien prüfen",
		L"Revisar arquivos ausentes",
		L"Ontbrekende bestanden controleren",
		L"Sprawdz brakujace pliki",
		L"Eksik dosyalari incele"));

	SetDlgItemText(IDC_MF_GRP_LIST, LL14(
		L"存在しないファイル",
		L"Missing files",
		L"Fichiers manquants",
		L"File mancanti",
		L"Archivos inexistentes",
		L"없는 파일",
		L"不存在的文件",
		L"الملفات المفقودة",
		L"Отсутствующие файлы",
		L"Fehlende Dateien",
		L"Arquivos ausentes",
		L"Ontbrekende bestanden",
		L"Brakujace pliki",
		L"Eksik dosyalar"));

	SetDlgItemText(IDC_MF_GRP_PATH, LL14(
		L"パスの修正",
		L"Fix path",
		L"Corriger le chemin",
		L"Correggi percorso",
		L"Corregir ruta",
		L"경로 수정",
		L"修正路径",
		L"تصحيح المسار",
		L"Исправить путь",
		L"Pfad korrigieren",
		L"Corrigir caminho",
		L"Pad herstellen",
		L"Popraw sciezke",
		L"Yolu duzelt"));

	SetDlgItemText(IDC_MF_PATH_L, LL14(
		L"パス:", L"Path:", L"Chemin:", L"Percorso:", L"Ruta:",
		L"경로:", L"路径:", L"المسار:", L"Путь:", L"Pfad:",
		L"Caminho:", L"Pad:", L"Sciezka:", L"Yol:"));

	SetDlgItemText(IDC_MF_BROWSE, LL14(
		L"参照...", L"Browse...", L"Parcourir...", L"Sfoglia...", L"Examinar...",
		L"찾아보기...", L"浏览...", L"استعراض...", L"Обзор...", L"Durchsuchen...",
		L"Procurar...", L"Bladeren...", L"Przegladaj...", L"Gozat..."));

	SetDlgItemText(IDC_MF_APPLY, LL14(
		L"適用", L"Apply", L"Appliquer", L"Applica", L"Aplicar",
		L"적용", L"应用", L"تطبيق", L"Применить", L"Übernehmen",
		L"Aplicar", L"Toepassen", L"Zastosuj", L"Uygula"));

	SetDlgItemText(IDC_MF_OPENFOL, LL14(
		L"フォルダを開く", L"Open folder", L"Ouvrir le dossier", L"Apri cartella", L"Abrir carpeta",
		L"폴더 열기", L"打开文件夹", L"فتح المجلد", L"Открыть папку", L"Ordner öffnen",
		L"Abrir pasta", L"Map openen", L"Otworz folder", L"Klasoru ac"));

	SetDlgItemText(IDC_MF_DELETE, LL14(
		L"残件を削除", L"Delete remaining", L"Supprimer le reste", L"Elimina rimanenti", L"Eliminar restantes",
		L"남은 항목 삭제", L"删除剩余项", L"حذف المتبقي", L"Удалить оставшиеся", L"Restliche löschen",
		L"Excluir restantes", L"Resterende verwijderen", L"Usun pozostale", L"Kalanlari sil"));

	SetDlgItemText(IDC_MF_CLOSE, LL14(
		L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
		L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen",
		L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));

	SetDlgItemText(IDC_MF_HINT, LL14(
		L"パスを修正して「適用」すると、ファイルが存在する場合はプレイリストに反映され、この一覧から外れます。ダブルクリックでパス列を直接編集できます。",
		L"Fix the path and click Apply. If the file exists, it updates the playlist and leaves this list. Double-click the path column to edit inline.",
		L"Corrigez le chemin puis Appliquer. S'il existe, la liste est mise a jour et l'entree disparait. Double-clic pour editer.",
		L"Correggi il percorso e Applica. Se esiste, aggiorna la playlist e esce dall'elenco. Doppio clic per modificare.",
		L"Corrija la ruta y pulse Aplicar. Si existe, se refleja en la lista y se quita de aqui. Doble clic para editar.",
		L"경로를 고친 뒤 적용을 누르면, 파일이 있으면 재생목록에 반영되고 이 목록에서 빠집니다. 경로 열을 더블클릭하면 직접 편집할 수 있습니다.",
		L"修正路径后点击“应用”。若文件存在，将写回播放列表并从此列表移除。双击路径列可直接编辑。",
		L"صحّح المسار ثم تطبيق. إن وُجد الملف يُحدَّث قائمة التشغيل ويُزال من هنا. انقر نقراً مزدوجاً للتحرير.",
		L"Исправьте путь и нажмите «Применить». Если файл есть, плейлист обновится и запись исчезнет. Двойной щелчок — правка.",
		L"Pfad korrigieren und Übernehmen. Existiert die Datei, wird die Playlist aktualisiert und der Eintrag entfernt. Doppelklick zum Bearbeiten.",
		L"Corrija o caminho e clique Aplicar. Se existir, atualiza a playlist e sai desta lista. Clique duplo para editar.",
		L"Herstel het pad en klik Toepassen. Bestaat het bestand, dan wordt de playlist bijgewerkt en verdwijnt het hier. Dubbelklik om te bewerken.",
		L"Popraw sciezke i kliknij Zastosuj. Jesli plik istnieje, playlista sie zaktualizuje i pozycja zniknie. Podwojne klikniecie edytuje.",
		L"Yolu duzeltip Uygula'ya basin. Dosya varsa listeye yansir ve buradan cikar. Yol sutununa cift tiklayarak duzenleyin."));

	m_browse.SetGradation(RGB(220, 240, 255), RGB(170, 210, 250), 0, TRUE);
	m_apply.SetGradation(RGB(200, 240, 200), RGB(140, 210, 150), 0, TRUE);
	m_openFol.SetGradation(RGB(220, 240, 230), RGB(180, 220, 200), 0, TRUE);
	m_delete.SetGradation(RGB(255, 220, 225), RGB(255, 170, 185), 0, TRUE);
	m_close.SetGradation(RGB(235, 235, 240), RGB(200, 200, 210), 0, TRUE);

	m_lc.m_pItems = &m_items;
	m_lc.SetExtendedStyle(m_lc.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);
	m_lc.InsertColumn(MF_COL_NO, LL14(L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#"), LVCFMT_RIGHT, 36, 0);
	m_lc.InsertColumn(MF_COL_NAME, LL14(
		L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم",
		L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"), LVCFMT_LEFT, 120, 0);
	m_lc.InsertColumn(MF_COL_PATH, LL14(
		L"ファイルフルパス", L"Full file path", L"Chemin complet", L"Percorso completo", L"Ruta completa",
		L"전체 파일 경로", L"文件完整路径", L"المسار الكامل", L"Полный путь", L"Vollständiger Pfad",
		L"Caminho completo", L"Volledig pad", L"Pelna sciezka", L"Tam dosya yolu"), LVCFMT_LEFT, 100, 0);
	FitPathColumn();

	RebuildList();
	if (m_lc.GetItemCount() > 0) {
		m_lc.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_selRow = 0;
		SyncPathEditFromSelection();
	}
	UpdateStatus();
	UpdateButtonEnable();
	return TRUE;
}

void CMissingFilesDlg::FitPathColumn()
{
	if (!::IsWindow(m_lc.GetSafeHwnd()))
		return;
	CRect rc;
	m_lc.GetClientRect(&rc);
	const int used = m_lc.GetColumnWidth(MF_COL_NO) + m_lc.GetColumnWidth(MF_COL_NAME);
	int pathW = rc.Width() - used;
	if (pathW < 80)
		pathW = 80;
	m_lc.SetColumnWidth(MF_COL_PATH, pathW);
}

void CMissingFilesDlg::RebuildList()
{
	m_lc.SetRedraw(FALSE);
	m_lc.DeleteAllItems();
	for (int i = 0; i < (int)m_items.size(); ++i) {
		CString no;
		no.Format(L"%d", m_items[i].plIndex + 1);
		const int row = m_lc.InsertItem(i, no);
		m_lc.SetItemText(row, MF_COL_NAME, m_items[i].name);
		m_lc.SetItemText(row, MF_COL_PATH, m_items[i].path);
		m_lc.SetItemData(row, (DWORD_PTR)i);
	}
	m_lc.SetRedraw(TRUE);
	FitPathColumn();
	m_lc.Invalidate();
}

void CMissingFilesDlg::SyncPathEditFromSelection()
{
	const int row = GetSelectedRow();
	m_selRow = row;
	if (row < 0 || row >= (int)m_items.size()) {
		m_path.SetWindowText(L"");
		return;
	}
	m_path.SetWindowText(m_items[row].path);
}

void CMissingFilesDlg::UpdateStatus()
{
	CString s;
	s.Format(LL14(
		L"%d 件（パス修正で一覧から外せます）",
		L"%d item(s) (fix path to remove from list)",
		L"%d element(s) (corrigez pour retirer)",
		L"%d elemento/i (correggi per rimuovere)",
		L"%d elemento(s) (corrija para quitar)",
		L"%d개 (경로 수정 시 목록에서 제거)",
		L"%d 项（修正路径可从此列表移除）",
		L"%d عنصر (صحّح المسار للإزالة)",
		L"%d шт. (исправьте путь, чтобы убрать)",
		L"%d Eintrag/Einträge (Pfad korrigieren zum Entfernen)",
		L"%d item(ns) (corrija o caminho para remover)",
		L"%d item(s) (herstel pad om te verwijderen)",
		L"%d poz. (popraw sciezke, by usunac)",
		L"%d oge (listeden cikarmak icin yolu duzelt)"), (int)m_items.size());
	SetDlgItemText(IDC_MF_STATUS, s);
}

void CMissingFilesDlg::UpdateButtonEnable()
{
	const BOOL has = !m_items.empty();
	const int row = GetSelectedRow();
	const BOOL sel = (row >= 0);
	m_browse.EnableWindow(sel);
	m_apply.EnableWindow(sel);
	m_openFol.EnableWindow(sel);
	m_delete.EnableWindow(has);
	m_path.EnableWindow(sel);
}

int CMissingFilesDlg::GetSelectedRow() const
{
	POSITION pos = m_lc.GetFirstSelectedItemPosition();
	if (!pos)
		return -1;
	return m_lc.GetNextSelectedItem(pos);
}

void CMissingFilesDlg::RemoveRowAt(int row)
{
	if (row < 0 || row >= (int)m_items.size())
		return;
	EndInlineEdit(FALSE);
	m_items.erase(m_items.begin() + row);
	RebuildList();
	const int n = (int)m_items.size();
	if (n > 0) {
		const int next = (row < n) ? row : (n - 1);
		m_lc.SetItemState(next, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_selRow = next;
	} else {
		m_selRow = -1;
	}
	SyncPathEditFromSelection();
	UpdateStatus();
	UpdateButtonEnable();
}

BOOL CMissingFilesDlg::ApplyPathToRow(int row, const CString& newPathRaw, BOOL showError)
{
	if (!m_pPlayList || !m_pPlayList->pc || row < 0 || row >= (int)m_items.size())
		return FALSE;

	CString newPath = newPathRaw;
	newPath.Trim();
	MissingFileItem& it = m_items[row];
	const int plIndex = it.plIndex;
	if (plIndex < 0 || plIndex >= m_pPlayList->playcnt)
		return FALSE;

	if (!m_pPlayList->UpdateTrackPath(plIndex, newPath)) {
		// まだ存在しない → 一覧上の表示だけ更新して残す
		it.path = m_pPlayList->pc[plIndex].fol;
		m_lc.SetItemText(row, MF_COL_PATH, it.path);
		m_path.SetWindowText(it.path);
		if (showError) {
			MessageBox(LL14(
				L"指定のパスにファイルが存在しません。パスを確認してください。",
				L"The specified path does not exist. Please check the path.",
				L"Le chemin specifie n'existe pas. Verifiez le chemin.",
				L"Il percorso specificato non esiste. Controllare il percorso.",
				L"La ruta especificada no existe. Compruebe la ruta.",
				L"지정한 경로에 파일이 없습니다. 경로를 확인하세요.",
				L"指定路径不存在。请检查路径。",
				L"المسار المحدد غير موجود. يرجى التحقق.",
				L"Указанный путь не существует. Проверьте путь.",
				L"Der angegebene Pfad existiert nicht. Bitte prüfen.",
				L"O caminho especificado nao existe. Verifique o caminho.",
				L"Het opgegeven pad bestaat niet. Controleer het pad.",
				L"Podana sciezka nie istnieje. Sprawdz sciezke.",
				L"Belirtilen yol yok. Lutfen yolu kontrol edin."),
				LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
					L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdzenie", L"Onay"),
				MB_ICONWARNING);
		}
		return FALSE;
	}

	// 正常パス → 元プレイリスト反映済み、この一覧から削除
	RemoveRowAt(row);
	if (m_items.empty()) {
		MessageBox(LL14(
			L"すべてのパスが復元されました。",
			L"All paths have been restored.",
			L"Tous les chemins ont ete restaures.",
			L"Tutti i percorsi sono stati ripristinati.",
			L"Se restauraron todas las rutas.",
			L"모든 경로가 복원되었습니다.",
			L"所有路径均已恢复。",
			L"تمت استعادة جميع المسارات.",
			L"Все пути восстановлены.",
			L"Alle Pfade wurden wiederhergestellt.",
			L"Todos os caminhos foram restaurados.",
			L"Alle paden zijn hersteld.",
			L"Wszystkie sciezki zostaly przywrocone.",
			L"Tum yollar geri yuklendi."),
			LL14(L"ogg簡易プレイヤ", L"ogg Simple Player", L"ogg Lecteur simple", L"ogg Lettore semplice",
				L"ogg Reproductor simple", L"ogg 간이 플레이어", L"ogg简易播放器", L"ogg مشغل بسيط",
				L"ogg Простой плеер", L"ogg Einfacher Player", L"ogg Player simples", L"ogg Eenvoudige speler",
				L"ogg Prosty odtwarzacz", L"ogg Basit oynatıcı"), MB_ICONINFORMATION);
		EndDialog(IDCANCEL);
	}
	return TRUE;
}

CString CMissingFilesDlg::BrowseForFile(const CString& initialPath)
{
	CString initDir;
	CString initName;
	if (!initialPath.IsEmpty()) {
		const CString phys = PlPhysicalMediaPath(initialPath);
		const int slash = phys.ReverseFind(_T('\\'));
		if (slash >= 0) {
			initDir = phys.Left(slash);
			initName = phys.Mid(slash + 1);
		} else {
			initName = phys;
		}
	}

	CFileDialog fd(TRUE, NULL, initName.IsEmpty() ? NULL : (LPCTSTR)initName,
		OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
		LL14(L"すべてのファイル (*.*)|*.*||",
			L"All files (*.*)|*.*||",
			L"Tous les fichiers (*.*)|*.*||",
			L"Tutti i file (*.*)|*.*||",
			L"Todos los archivos (*.*)|*.*||",
			L"모든 파일 (*.*)|*.*||",
			L"全部文件 (*.*)|*.*||",
			L"كل الملفات (*.*)|*.*||",
			L"Все файлы (*.*)|*.*||",
			L"Alle Dateien (*.*)|*.*||",
			L"Todos os arquivos (*.*)|*.*||",
			L"Alle bestanden (*.*)|*.*||",
			L"Wszystkie pliki (*.*)|*.*||",
			L"Tum dosyalar (*.*)|*.*||"),
		this);
	if (!initDir.IsEmpty())
		fd.m_ofn.lpstrInitialDir = initDir;
	if (fd.DoModal() != IDOK)
		return CString();
	return fd.GetPathName();
}

void CMissingFilesDlg::OnBnClickedBrowse()
{
	EndInlineEdit(FALSE);
	const int row = GetSelectedRow();
	if (row < 0)
		return;
	const CString picked = BrowseForFile(m_items[row].path);
	if (picked.IsEmpty())
		return;
	m_path.SetWindowText(picked);
	ApplyPathToRow(row, picked, TRUE);
}

void CMissingFilesDlg::OnBnClickedApply()
{
	EndInlineEdit(FALSE);
	const int row = GetSelectedRow();
	if (row < 0)
		return;
	CString path;
	m_path.GetWindowText(path);
	ApplyPathToRow(row, path, TRUE);
}

void CMissingFilesDlg::OnBnClickedOpenFol()
{
	const int row = GetSelectedRow();
	if (row < 0)
		return;
	CString path = m_items[row].path;
	m_path.GetWindowText(path);
	path.Trim();
	const CString phys = PlPhysicalMediaPath(path);
	CString folder = phys;
	const int slash = folder.ReverseFind(_T('\\'));
	if (slash >= 0)
		folder = folder.Left(slash);
	if (folder.IsEmpty() || !PathFileExists(folder)) {
		MessageBox(LL14(
			L"開けるフォルダがありません。",
			L"No folder can be opened.",
			L"Aucun dossier a ouvrir.",
			L"Nessuna cartella da aprire.",
			L"No hay carpeta para abrir.",
			L"열 폴더가 없습니다.",
			L"没有可打开的文件夹。",
			L"لا يوجد مجلد لفتحه.",
			L"Нет папки для открытия.",
			L"Kein Ordner zum Öffnen.",
			L"Nenhuma pasta para abrir.",
			L"Geen map om te openen.",
			L"Brak folderu do otwarcia.",
			L"Acilacak klasor yok."),
			LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
				L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdzenie", L"Onay"),
			MB_ICONINFORMATION);
		return;
	}
	ShellExecute(NULL, _T("open"), folder, _T(""), NULL, SW_SHOWNORMAL);
}

void CMissingFilesDlg::OnBnClickedDelete()
{
	if (m_items.empty())
		return;

	CString msg;
	msg.Format(LL14(
		L"%d 件の存在しないファイルを一覧から削除しますか？",
		L"Remove %d missing file(s) from the list?",
		L"Supprimer %d fichier(s) manquant(s) ?",
		L"Rimuovere %d file mancanti?",
		L"¿Eliminar %d archivo(s) inexistente(s)?",
		L"없는 파일 %d개를 목록에서 삭제할까요?",
		L"从列表中删除 %d 个不存在的文件吗？",
		L"إزالة %d ملف(ات) مفقود(ة)؟",
		L"Удалить %d отсутствующих файлов?",
		L"%d fehlende Datei(en) entfernen?",
		L"Remover %d arquivo(s) ausente(s)?",
		L"%d ontbrekende bestand(en) verwijderen?",
		L"Usunąć %d brakujących plików?",
		L"%d eksik dosya listeden silinsin mi?"), (int)m_items.size());

	if (MessageBox(msg,
		LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
			L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdzenie", L"Onay"),
		MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;

	m_toDelete.clear();
	m_toDelete.reserve(m_items.size());
	for (const auto& it : m_items)
		m_toDelete.push_back(it.plIndex);
	std::sort(m_toDelete.begin(), m_toDelete.end());
	EndDialog(IDOK);
}

void CMissingFilesDlg::OnBnClickedClose()
{
	EndInlineEdit(FALSE);
	EndDialog(IDCANCEL);
}

void CMissingFilesDlg::OnClose()
{
	OnBnClickedClose();
}

void CMissingFilesDlg::OnLvnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	*pResult = 0;
	if (!(pNMLV->uChanged & LVIF_STATE))
		return;
	if ((pNMLV->uNewState & LVIS_SELECTED) && !(pNMLV->uOldState & LVIS_SELECTED)) {
		if (m_bInlineEdit && m_inlineRow != pNMLV->iItem)
			EndInlineEdit(TRUE);
		SyncPathEditFromSelection();
		UpdateButtonEnable();
	}
}

void CMissingFilesDlg::OnNMDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE pNM = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	*pResult = 0;
	if (pNM->iItem < 0)
		return;
	if (pNM->iSubItem == MF_COL_PATH)
		StartInlineEdit(pNM->iItem);
	else
		OnBnClickedBrowse();
}

void CMissingFilesDlg::OnNMClickList(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE pNM = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	*pResult = 0;
	if (pNM->iItem < 0)
		return;
	// パス列をクリックしたら編集欄へフォーカス（直接編集しやすく）
	if (pNM->iSubItem == MF_COL_PATH) {
		SyncPathEditFromSelection();
		m_path.SetFocus();
		m_path.SetSel(0, -1);
	}
}

void CMissingFilesDlg::StartInlineEdit(int row)
{
	if (row < 0 || row >= (int)m_items.size())
		return;
	EndInlineEdit(FALSE);

	CRect rc;
	if (!m_lc.GetSubItemRect(row, MF_COL_PATH, LVIR_BOUNDS, rc))
		return;
	// ラベル領域の左端調整（アイコン列オフセット対策）
	CRect rcLabel;
	if (m_lc.GetSubItemRect(row, MF_COL_PATH, LVIR_LABEL, rcLabel) && rcLabel.Width() > 8)
		rc = rcLabel;

	m_lc.ClientToScreen(&rc);
	ScreenToClient(&rc);

	if (!m_inlineEdit.GetSafeHwnd()) {
		m_inlineEdit.Create(WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
			rc, this, IDC_MF_INLINE_EDIT_DUMMY);
		m_inlineEdit.SetFont(GetFont());
	} else {
		m_inlineEdit.MoveWindow(&rc);
	}
	m_inlineEdit.SetWindowText(m_items[row].path);
	m_inlineEdit.ShowWindow(SW_SHOW);
	m_inlineEdit.SetFocus();
	m_inlineEdit.SetSel(0, -1);
	m_bInlineEdit = TRUE;
	m_inlineRow = row;
}

void CMissingFilesDlg::EndInlineEdit(BOOL commit)
{
	if (!m_bInlineEdit)
		return;
	const int row = m_inlineRow;
	CString text;
	if (m_inlineEdit.GetSafeHwnd() && commit)
		m_inlineEdit.GetWindowText(text);
	// KillFocus 再入防止: 先にフラグを落としてから Hide
	m_bInlineEdit = FALSE;
	m_inlineRow = -1;
	if (m_inlineEdit.GetSafeHwnd())
		m_inlineEdit.ShowWindow(SW_HIDE);
	if (commit && row >= 0)
		ApplyPathToRow(row, text, TRUE);
}

void CMissingFilesDlg::OnInlineEditKillFocus()
{
	if (m_bInlineEdit)
		EndInlineEdit(TRUE);
}

BOOL CMissingFilesDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN) {
		if (m_bInlineEdit && m_inlineEdit.GetSafeHwnd() &&
			(pMsg->hwnd == m_inlineEdit.GetSafeHwnd())) {
			if (pMsg->wParam == VK_RETURN) {
				EndInlineEdit(TRUE);
				return TRUE;
			}
			if (pMsg->wParam == VK_ESCAPE) {
				EndInlineEdit(FALSE);
				return TRUE;
			}
		}
		if (pMsg->hwnd == m_path.GetSafeHwnd() && pMsg->wParam == VK_RETURN) {
			OnBnClickedApply();
			return TRUE;
		}
	}
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

HBRUSH CMissingFilesDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
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
