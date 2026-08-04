#include "stdafx.h"
#include "ogg.h"
#include "CMpSmartPlaylistDlg.h"

extern save savedata;

IMPLEMENT_DYNAMIC(CMpSmartPlaylistDlg, CCustomBlurDialogBase)

CMpSmartPlaylistDlg::CMpSmartPlaylistDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CMpSmartPlaylistDlg::IDD, pParent)
	, m_appliedIndex(-1)
	, m_sel(-1)
{
}

CMpSmartPlaylistDlg::~CMpSmartPlaylistDlg()
{
}

void CMpSmartPlaylistDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MSM_LIST, m_lc);
	DDX_Control(pDX, IDC_MSM_NAME, m_name);
	DDX_Control(pDX, IDC_MSM_UNPLAYED, m_unplayed);
	DDX_Control(pDX, IDC_MSM_MISSING, m_missing);
	DDX_Control(pDX, IDC_MSM_RATING, m_rating);
	DDX_Control(pDX, IDC_MSM_RATING_N, m_ratingN);
	DDX_Control(pDX, IDC_MSM_ARTIST_CK, m_artistCk);
	DDX_Control(pDX, IDC_MSM_ARTIST, m_artist);
	DDX_Control(pDX, IDC_MSM_HOUR_CK, m_hourCk);
	DDX_Control(pDX, IDC_MSM_HOUR_FROM, m_hourFrom);
	DDX_Control(pDX, IDC_MSM_HOUR_TO, m_hourTo);
	DDX_Control(pDX, IDC_MSM_PLAYMAX_CK, m_playMaxCk);
	DDX_Control(pDX, IDC_MSM_PLAYMAX, m_playMax);
	DDX_Control(pDX, IDC_MSM_ADD, m_add);
	DDX_Control(pDX, IDC_MSM_UPDATE, m_update);
	DDX_Control(pDX, IDC_MSM_DEL, m_del);
	DDX_Control(pDX, IDC_MSM_APPLY, m_apply);
	DDX_Control(pDX, IDC_MSM_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CMpSmartPlaylistDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_MSM_ADD, &CMpSmartPlaylistDlg::OnBnClickedAdd)
	ON_BN_CLICKED(IDC_MSM_UPDATE, &CMpSmartPlaylistDlg::OnBnClickedUpdate)
	ON_BN_CLICKED(IDC_MSM_DEL, &CMpSmartPlaylistDlg::OnBnClickedDel)
	ON_BN_CLICKED(IDC_MSM_APPLY, &CMpSmartPlaylistDlg::OnBnClickedApply)
	ON_BN_CLICKED(IDC_MSM_CLOSE, &CMpSmartPlaylistDlg::OnBnClickedClose)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_MSM_LIST, &CMpSmartPlaylistDlg::OnLvnItemchangedList)
	ON_WM_CLOSE()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CMpSmartPlaylistDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	MpSmart_EnsureDefaults();

	SetWindowText(LL14(
		L"スマートプレイリスト", L"Smart playlist", L"Playlist intelligente", L"Playlist smart", L"Lista inteligente",
		L"스마트 재생목록", L"智能播放列表", L"قائمة ذكية", L"Умный плейлист", L"Smart-Playlist",
		L"Playlist inteligente", L"Slimme playlist", L"Inteligentna playlista", L"Akilli liste"));
	SetDlgItemText(IDC_MSM_UNPLAYED, LL14(L"未再生", L"Unplayed", L"Non joues", L"Non riprodotti", L"No reproducidos", L"미재생", L"未播放", L"غير مشغّل", L"Неигранные", L"Ungespielt", L"Nao tocados", L"Ongespeeld", L"Nieodtworzone", L"Oynatilmamis"));
	SetDlgItemText(IDC_MSM_MISSING, LL14(L"欠損", L"Missing", L"Manquants", L"Mancanti", L"Faltantes", L"결손", L"缺失", L"مفقود", L"Отсутствующие", L"Fehlend", L"Ausentes", L"Ontbrekend", L"Brakujace", L"Eksik"));
	SetDlgItemText(IDC_MSM_RATING, LL14(L"評価≧", L"Rating ≥", L"Note ≥", L"Voto ≥", L"Valoración ≥", L"평점≧", L"评分≥", L"تقييم ≥", L"Оценка ≥", L"Bewertung ≥", L"Nota ≥", L"Beoordeling ≥", L"Ocena ≥", L"Puan ≥"));
	SetDlgItemText(IDC_MSM_ARTIST_CK, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"فنان", L"Исполнитель", L"Interpret", L"Artista", L"Artiest", L"Artysta", L"Sanatci"));
	SetDlgItemText(IDC_MSM_HOUR_CK, LL14(L"時間帯", L"Hour range", L"Plage horaire", L"Fascia oraria", L"Rango horario", L"시간대", L"时段", L"نطاق الساعة", L"Часы", L"Uhrzeit", L"Faixa horaria", L"Uurbereik", L"Zakres godzin", L"Saat araligi"));
	SetDlgItemText(IDC_MSM_PLAYMAX_CK, LL14(L"再生回数≦", L"Play count ≤", L"Lectures ≤", L"Ascolti ≤", L"Reproducciones ≤", L"재생횟수≦", L"播放次数≤", L"مرات التشغيل ≤", L"Прослушивания ≤", L"Wiedergaben ≤", L"Reproducoes ≤", L"Afspeelteller ≤", L"Odtworzenia ≤", L"Oynatma ≤"));
	SetDlgItemText(IDC_MSM_ADD, LL14(L"追加", L"Add", L"Ajouter", L"Aggiungi", L"Anadir", L"추가", L"添加", L"إضافة", L"Добавить", L"Hinzufügen", L"Adicionar", L"Toevoegen", L"Dodaj", L"Ekle"));
	SetDlgItemText(IDC_MSM_UPDATE, LL14(L"更新", L"Update", L"Mettre a jour", L"Aggiorna", L"Actualizar", L"업데이트", L"更新", L"تحديث", L"Обновить", L"Aktualisieren", L"Atualizar", L"Bijwerken", L"Aktualizuj", L"Guncelle"));
	SetDlgItemText(IDC_MSM_DEL, LL14(L"削除", L"Delete", L"Supprimer", L"Elimina", L"Eliminar", L"삭제", L"删除", L"حذف", L"Удалить", L"Löschen", L"Excluir", L"Verwijderen", L"Usun", L"Sil"));
	SetDlgItemText(IDC_MSM_APPLY, LL14(L"適用", L"Apply", L"Appliquer", L"Applica", L"Aplicar", L"적용", L"应用", L"تطبيق", L"Применить", L"Übernehmen", L"Aplicar", L"Toepassen", L"Zastosuj", L"Uygula"));
	SetDlgItemText(IDC_MSM_CLOSE, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));

	m_add.SetGradation(RGB(200, 240, 200), RGB(140, 210, 150), 0, TRUE);
	m_update.SetGradation(RGB(220, 240, 255), RGB(170, 210, 250), 0, TRUE);
	m_del.SetGradation(RGB(255, 220, 225), RGB(255, 170, 185), 0, TRUE);
	m_apply.SetGradation(RGB(200, 240, 200), RGB(140, 210, 150), 0, TRUE);
	m_close.SetGradation(RGB(235, 235, 240), RGB(200, 200, 210), 0, TRUE);

	m_lc.SetExtendedStyle(m_lc.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_lc.InsertColumn(0, LL14(L"ルール", L"Rule", L"Regle", L"Regola", L"Regla", L"규칙", L"规则", L"قاعدة", L"Правило", L"Regel", L"Regra", L"Regel", L"Regula", L"Kural"), LVCFMT_LEFT, 140, 0);

	RebuildList();
	if (m_lc.GetItemCount() > 0) {
		m_lc.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_sel = 0;
		LoadSelToUi();
	}
	return TRUE;
}

void CMpSmartPlaylistDlg::RebuildList()
{
	m_lc.SetRedraw(FALSE);
	m_lc.DeleteAllItems();
	const int n = MpSmart_Count();
	for (int i = 0; i < n; ++i) {
		MpSmartRule r;
		if (!MpSmart_Get(i, r)) continue;
		const int row = m_lc.InsertItem(i, r.name);
		m_lc.SetItemData(row, (DWORD_PTR)i);
	}
	m_lc.SetRedraw(TRUE);
	m_lc.Invalidate();
}

int CMpSmartPlaylistDlg::GetSelectedRule() const
{
	POSITION pos = m_lc.GetFirstSelectedItemPosition();
	if (!pos) return -1;
	const int row = m_lc.GetNextSelectedItem(pos);
	return (int)m_lc.GetItemData(row);
}

void CMpSmartPlaylistDlg::LoadSelToUi()
{
	const int i = GetSelectedRule();
	m_sel = i;
	MpSmartRule r;
	ZeroMemory(&r, sizeof(r));
	if (i >= 0) MpSmart_Get(i, r);
	m_name.SetWindowText(r.name);
	m_unplayed.SetCheck((r.flags & MP_SMART_UNPLAYED) ? 1 : 0);
	m_missing.SetCheck((r.flags & MP_SMART_MISSING) ? 1 : 0);
	m_rating.SetCheck((r.flags & MP_SMART_RATING_MIN) ? 1 : 0);
	CString s;
	s.Format(_T("%d"), r.ratingMin > 0 ? r.ratingMin : 1);
	m_ratingN.SetWindowText(s);
	m_artistCk.SetCheck((r.flags & MP_SMART_ARTIST) ? 1 : 0);
	m_artist.SetWindowText(r.artist);
	m_hourCk.SetCheck((r.flags & MP_SMART_HOUR_RANGE) ? 1 : 0);
	s.Format(_T("%d"), r.hourFrom);
	m_hourFrom.SetWindowText(s);
	s.Format(_T("%d"), r.hourTo);
	m_hourTo.SetWindowText(s);
	m_playMaxCk.SetCheck((r.flags & MP_SMART_PLAY_MAX) ? 1 : 0);
	s.Format(_T("%d"), r.playCountMax);
	m_playMax.SetWindowText(s);
}

void CMpSmartPlaylistDlg::UiToRule(MpSmartRule& r)
{
	ZeroMemory(&r, sizeof(r));
	CString name, artist, tmp;
	m_name.GetWindowText(name);
	name.Trim();
	if (name.IsEmpty()) name = _T("Rule");
	_tcsncpy(r.name, name, _countof(r.name) - 1);
	m_artist.GetWindowText(artist);
	_tcsncpy(r.artist, artist, _countof(r.artist) - 1);
	r.flags = 0;
	if (m_unplayed.GetCheck()) r.flags |= MP_SMART_UNPLAYED;
	if (m_missing.GetCheck()) r.flags |= MP_SMART_MISSING;
	if (m_rating.GetCheck()) r.flags |= MP_SMART_RATING_MIN;
	if (m_artistCk.GetCheck()) r.flags |= MP_SMART_ARTIST;
	if (m_hourCk.GetCheck()) r.flags |= MP_SMART_HOUR_RANGE;
	if (m_playMaxCk.GetCheck()) r.flags |= MP_SMART_PLAY_MAX;
	m_ratingN.GetWindowText(tmp); r.ratingMin = _tstoi(tmp);
	if (r.ratingMin < 1) r.ratingMin = 1;
	if (r.ratingMin > 5) r.ratingMin = 5;
	m_hourFrom.GetWindowText(tmp); r.hourFrom = _tstoi(tmp);
	m_hourTo.GetWindowText(tmp); r.hourTo = _tstoi(tmp);
	if (r.hourFrom < 0) r.hourFrom = 0;
	if (r.hourFrom > 23) r.hourFrom = 23;
	if (r.hourTo < 0) r.hourTo = 0;
	if (r.hourTo > 23) r.hourTo = 23;
	m_playMax.GetWindowText(tmp); r.playCountMax = _tstoi(tmp);
	if (r.playCountMax < 0) r.playCountMax = 0;
	r.enabled = 1;
}

void CMpSmartPlaylistDlg::OnLvnItemchangedList(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	*pResult = 0;
	if (!(pNMLV->uChanged & LVIF_STATE)) return;
	if ((pNMLV->uNewState & LVIS_SELECTED) && !(pNMLV->uOldState & LVIS_SELECTED))
		LoadSelToUi();
}

void CMpSmartPlaylistDlg::OnBnClickedAdd()
{
	MpSmartRule r;
	UiToRule(r);
	const int idx = MpSmart_Add(r);
	if (idx < 0) {
		MessageBox(LL14(
			L"ルール数が上限です。", L"Rule limit reached.", L"Limite de regles atteinte.", L"Limite regole raggiunta.", L"Limite de reglas.",
			L"규칙 수 한도입니다.", L"规则数已达上限。", L"تم بلوغ حد القواعد.", L"Достигнут лимит правил.", L"Regellimit erreicht.",
			L"Limite de regras.", L"Regel limiet bereikt.", L"Osiagnieto limit regul.", L"Kural limiti doldu."),
			_T("Smart"), MB_ICONWARNING);
		return;
	}
	RebuildList();
	for (int row = 0; row < m_lc.GetItemCount(); ++row) {
		if ((int)m_lc.GetItemData(row) == idx) {
			m_lc.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			break;
		}
	}
	LoadSelToUi();
}

void CMpSmartPlaylistDlg::OnBnClickedUpdate()
{
	const int i = GetSelectedRule();
	if (i < 0) return;
	MpSmartRule r;
	UiToRule(r);
	MpSmart_Set(i, r);
	RebuildList();
	for (int row = 0; row < m_lc.GetItemCount(); ++row) {
		if ((int)m_lc.GetItemData(row) == i) {
			m_lc.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			break;
		}
	}
}

void CMpSmartPlaylistDlg::OnBnClickedDel()
{
	const int i = GetSelectedRule();
	if (i < 0) return;
	if (MessageBox(LL14(
		L"このルールを削除しますか？", L"Delete this rule?", L"Supprimer cette regle ?", L"Eliminare questa regola?", L"¿Eliminar esta regla?",
		L"이 규칙을 삭제할까요?", L"删除此规则？", L"حذف هذه القاعدة؟", L"Удалить это правило?", L"Diese Regel loeschen?",
		L"Excluir esta regra?", L"Deze regel verwijderen?", L"Usunac te regule?", L"Bu kural silinsin mi?"),
		LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
			L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdzenie", L"Onay"),
		MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	MpSmart_Remove(i);
	RebuildList();
	if (m_lc.GetItemCount() > 0) {
		m_lc.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		LoadSelToUi();
	} else {
		m_sel = -1;
		MpSmartRule empty; ZeroMemory(&empty, sizeof(empty));
		m_name.SetWindowText(_T(""));
		m_unplayed.SetCheck(0); m_missing.SetCheck(0); m_rating.SetCheck(0);
		m_artistCk.SetCheck(0); m_hourCk.SetCheck(0); m_playMaxCk.SetCheck(0);
		m_ratingN.SetWindowText(_T("1"));
		m_artist.SetWindowText(_T(""));
		m_hourFrom.SetWindowText(_T("0")); m_hourTo.SetWindowText(_T("23"));
		m_playMax.SetWindowText(_T("0"));
	}
}

void CMpSmartPlaylistDlg::OnBnClickedApply()
{
	const int i = GetSelectedRule();
	if (i < 0) {
		MessageBox(LL14(
			L"適用するルールを選択してください。", L"Select a rule to apply.", L"Selectionnez une regle.", L"Seleziona una regola.", L"Seleccione una regla.",
			L"적용할 규칙을 선택하세요.", L"请选择要应用的规则。", L"حدد قاعدة للتطبيق.", L"Выберите правило.", L"Regel auswaehlen.",
			L"Selecione uma regra.", L"Selecteer een regel.", L"Wybierz regule.", L"Uygulanacak kurali secin."),
			_T("Smart"), MB_ICONINFORMATION);
		return;
	}
	// Persist current UI edits before apply
	MpSmartRule r;
	UiToRule(r);
	MpSmart_Set(i, r);
	m_appliedIndex = i;
	EndDialog(IDOK);
}

void CMpSmartPlaylistDlg::OnBnClickedClose()
{
	m_appliedIndex = -1;
	EndDialog(IDCANCEL);
}

void CMpSmartPlaylistDlg::OnClose()
{
	OnBnClickedClose();
}

HBRUSH CMpSmartPlaylistDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
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
