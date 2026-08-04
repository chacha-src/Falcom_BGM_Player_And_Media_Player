#include "stdafx.h"
#include "ogg.h"
#include "CMpDupesDlg.h"
#include "PlayList.h"
#include <algorithm>
#include <map>

extern CPlayList* pl;
extern save savedata;

IMPLEMENT_DYNAMIC(CMpDupesDlg, CCustomBlurDialogBase)

CMpDupesDlg::CMpDupesDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CMpDupesDlg::IDD, pParent)
{
}

CMpDupesDlg::~CMpDupesDlg()
{
}

void CMpDupesDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MPD_LIST, m_lc);
	DDX_Control(pDX, IDC_MPD_DELETE, m_delete);
	DDX_Control(pDX, IDC_MPD_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CMpDupesDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_MPD_DELETE, &CMpDupesDlg::OnBnClickedDelete)
	ON_BN_CLICKED(IDC_MPD_CLOSE, &CMpDupesDlg::OnBnClickedClose)
	ON_WM_CLOSE()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CMpDupesDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);

	SetWindowText(LL14(
		L"重複ファイル", L"Duplicate files", L"Fichiers en double", L"File duplicati", L"Archivos duplicados",
		L"중복 파일", L"重复文件", L"ملفات مكررة", L"Дубликаты файлов", L"Doppelte Dateien",
		L"Arquivos duplicados", L"Dubbele bestanden", L"Zduplikowane pliki", L"Yinelenen dosyalar"));
	SetDlgItemText(IDC_MPD_DELETE, LL14(
		L"削除", L"Delete", L"Supprimer", L"Elimina", L"Eliminar",
		L"삭제", L"删除", L"حذف", L"Удалить", L"Löschen",
		L"Excluir", L"Verwijderen", L"Usun", L"Sil"));
	SetDlgItemText(IDC_MPD_CLOSE, LL14(
		L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
		L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen",
		L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));

	m_delete.SetGradation(RGB(255, 220, 225), RGB(255, 170, 185), 0, TRUE);
	m_close.SetGradation(RGB(235, 235, 240), RGB(200, 200, 210), 0, TRUE);

	m_lc.SetExtendedStyle(m_lc.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_lc.InsertColumn(0, LL14(
		L"重複グループ", L"Duplicate groups", L"Groupes", L"Gruppi", L"Grupos",
		L"중복 그룹", L"重复组", L"مجموعات", L"Группы", L"Gruppen",
		L"Grupos", L"Groepen", L"Grupy", L"Gruplar"), LVCFMT_LEFT, 400, 0);

	RebuildList();
	UpdateStatus();
	return TRUE;
}

void CMpDupesDlg::RebuildList()
{
	m_lc.SetRedraw(FALSE);
	m_lc.DeleteAllItems();
	m_groupOf.clear();
	if (!pl || !pl->pc || pl->playcnt <= 0) {
		m_lc.SetRedraw(TRUE);
		return;
	}

	const int n = pl->playcnt;
	std::vector<int> groupId(n, -1);
	std::vector<CString> groupKey;
	int nextG = 0;

	// Primary: normalized path
	std::map<CString, int> pathToG;
	for (int i = 0; i < n; ++i) {
		CString key = NormalizePlaylistPath(pl->pc[i].fol);
		key.MakeLower();
		if (key.IsEmpty()) continue;
		auto it = pathToG.find(key);
		if (it == pathToG.end()) {
			pathToG[key] = nextG;
			groupId[i] = nextG;
			groupKey.push_back(NormalizePlaylistPath(pl->pc[i].fol));
			nextG++;
		} else {
			groupId[i] = it->second;
		}
	}

	// Secondary: same basename + size when paths differ (merge into primary groups)
	for (int i = 0; i < n; ++i) {
		if (groupId[i] < 0) continue;
		CString path = PlPhysicalMediaPath(pl->pc[i].fol);
		const int slash = path.ReverseFind(_T('\\'));
		CString base = (slash >= 0) ? path.Mid(slash + 1) : path;
		base.MakeLower();
		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		ULONGLONG sz = 0;
		if (::GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
			sz = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
		for (int j = i + 1; j < n; ++j) {
			if (groupId[j] < 0 || groupId[j] == groupId[i]) continue;
			CString path2 = PlPhysicalMediaPath(pl->pc[j].fol);
			const int slash2 = path2.ReverseFind(_T('\\'));
			CString base2 = (slash2 >= 0) ? path2.Mid(slash2 + 1) : path2;
			base2.MakeLower();
			if (base != base2) continue;
			WIN32_FILE_ATTRIBUTE_DATA fad2 = {};
			ULONGLONG sz2 = 0;
			if (::GetFileAttributesEx(path2, GetFileExInfoStandard, &fad2))
				sz2 = ((ULONGLONG)fad2.nFileSizeHigh << 32) | fad2.nFileSizeLow;
			if (sz != 0 && sz == sz2) {
				const int from = groupId[j];
				const int to = groupId[i];
				for (int k = 0; k < n; ++k)
					if (groupId[k] == from) groupId[k] = to;
			}
		}
	}

	// Count members per group; only show groups with 2+
	std::vector<int> counts(nextG, 0);
	for (int i = 0; i < n; ++i)
		if (groupId[i] >= 0 && groupId[i] < nextG) counts[groupId[i]]++;

	int shown = 0;
	for (int g = 0; g < nextG; ++g) {
		if (counts[g] < 2) continue;
		shown++;
		CString hdr;
		CString sample;
		for (int i = 0; i < n; ++i) {
			if (groupId[i] == g) { sample = NormalizePlaylistPath(pl->pc[i].fol); break; }
		}
		hdr.Format(LL14(
			L"グループ %d: %s", L"Group %d: %s", L"Groupe %d: %s", L"Gruppo %d: %s", L"Grupo %d: %s",
			L"그룹 %d: %s", L"组 %d: %s", L"مجموعة %d: %s", L"Группа %d: %s", L"Gruppe %d: %s",
			L"Grupo %d: %s", L"Groep %d: %s", L"Grupa %d: %s", L"Grup %d: %s"), shown, (LPCTSTR)sample);
		const int row = m_lc.InsertItem(m_lc.GetItemCount(), hdr);
		m_lc.SetItemData(row, (DWORD_PTR)-1);
		m_groupOf.push_back(g);

		for (int i = 0; i < n; ++i) {
			if (groupId[i] != g) continue;
			CString line;
			line.Format(_T("    [%d] %s"), i, pl->pc[i].name);
			const int r = m_lc.InsertItem(m_lc.GetItemCount(), line);
			m_lc.SetItemData(r, (DWORD_PTR)i);
			m_groupOf.push_back(g);
		}
	}

	m_lc.SetRedraw(TRUE);
	m_lc.Invalidate();
}

void CMpDupesDlg::UpdateStatus()
{
	int groups = 0, items = 0;
	for (int i = 0; i < m_lc.GetItemCount(); ++i) {
		if ((int)m_lc.GetItemData(i) < 0) groups++;
		else items++;
	}
	CString s;
	s.Format(LL14(
		L"%d グループ / %d 件", L"%d group(s) / %d item(s)", L"%d groupe(s) / %d", L"%d gruppo/i / %d", L"%d grupo(s) / %d",
		L"%d 그룹 / %d개", L"%d 组 / %d 项", L"%d مجموعة / %d", L"%d групп / %d", L"%d Gruppe(n) / %d",
		L"%d grupo(s) / %d", L"%d groep(en) / %d", L"%d grup / %d", L"%d grup / %d"), groups, items);
	SetDlgItemText(IDC_MPD_STATUS, s);
}

void CMpDupesDlg::OnBnClickedDelete()
{
	if (!pl || !pl->pc) return;

	std::vector<int> selected;
	POSITION pos = m_lc.GetFirstSelectedItemPosition();
	while (pos) {
		const int row = m_lc.GetNextSelectedItem(pos);
		const int pcIdx = (int)m_lc.GetItemData(row);
		if (pcIdx >= 0)
			selected.push_back(pcIdx);
	}

	std::vector<int> toDel;
	if (!selected.empty()) {
		// Keep earliest index per group among selection; delete the rest of selection
		std::map<int, int> keepFirst; // group -> min pc index in selection
		for (int idx : selected) {
			int g = -1;
			for (int row = 0; row < m_lc.GetItemCount(); ++row) {
				if ((int)m_lc.GetItemData(row) == idx && row < (int)m_groupOf.size()) {
					g = m_groupOf[row];
					break;
				}
			}
			if (g < 0) continue;
			auto it = keepFirst.find(g);
			if (it == keepFirst.end() || idx < it->second)
				keepFirst[g] = idx;
		}
		for (int idx : selected) {
			int g = -1;
			for (int row = 0; row < m_lc.GetItemCount(); ++row) {
				if ((int)m_lc.GetItemData(row) == idx && row < (int)m_groupOf.size()) {
					g = m_groupOf[row];
					break;
				}
			}
			if (g < 0) continue;
			if (keepFirst[g] != idx)
				toDel.push_back(idx);
		}
		// If user selected only later dupes (none is the "first" of group in full PL),
		// also allow deleting all selected that are later than group first in PL.
		if (toDel.empty()) {
			for (int idx : selected) {
				CString key = NormalizePlaylistPath(pl->pc[idx].fol);
				key.MakeLower();
				BOOL later = FALSE;
				for (int j = 0; j < idx; ++j) {
					CString k2 = NormalizePlaylistPath(pl->pc[j].fol);
					k2.MakeLower();
					if (k2 == key) { later = TRUE; break; }
				}
				if (later) toDel.push_back(idx);
			}
		}
	}

	if (toDel.empty()) {
		MessageBox(LL14(
			L"削除する後続の重複を選択してください。",
			L"Select later duplicates to delete.",
			L"Selectionnez les doublons ulterieurs.",
			L"Selezionare i duplicati successivi.",
			L"Seleccione duplicados posteriores.",
			L"삭제할 나중 중복을 선택하세요.",
			L"请选择要删除的后续重复项。",
			L"حدد المكررات اللاحقة للحذف.",
			L"Выберите последующие дубликаты.",
			L"Spaetere Duplikate auswaehlen.",
			L"Selecione duplicatas posteriores.",
			L"Selecteer latere duplicaten.",
			L"Wybierz pozniejsze duplikaty.",
			L"Silinecek sonraki yinelenenleri secin."),
			LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
				L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdzenie", L"Onay"),
			MB_ICONINFORMATION);
		return;
	}

	CString msg;
	msg.Format(LL14(
		L"%d 件の重複を削除しますか？", L"Delete %d duplicate(s)?", L"Supprimer %d doublon(s) ?", L"Eliminare %d duplicat(i)?", L"¿Eliminar %d duplicado(s)?",
		L"중복 %d개를 삭제할까요?", L"删除 %d 个重复项？", L"حذف %d مكرر؟", L"Удалить %d дубликат(ов)?", L"%d Duplikat(e) loeschen?",
		L"Excluir %d duplicata(s)?", L"%d duplicaten verwijderen?", L"Usunac %d duplikat(ow)?", L"%d yinelenen silinsin mi?"), (int)toDel.size());
	if (MessageBox(msg,
		LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
			L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdzenie", L"Onay"),
		MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;

	pl->DelByIndices(toDel);
	RebuildList();
	UpdateStatus();
}

void CMpDupesDlg::OnBnClickedClose()
{
	EndDialog(IDCANCEL);
}

void CMpDupesDlg::OnClose()
{
	OnBnClickedClose();
}

HBRUSH CMpDupesDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
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
