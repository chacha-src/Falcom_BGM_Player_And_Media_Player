#include "stdafx.h"
#include "ogg.h"
#include "CMpQueueDlg.h"
#include "CMediaPlayerDlg.h"
#include "PlayList.h"

extern CMediaPlayerDlg* mp;
extern CPlayList* pl;
extern save savedata;

IMPLEMENT_DYNAMIC(CMpQueueDlg, CCustomBlurDialogBase)

CMpQueueDlg::CMpQueueDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CMpQueueDlg::IDD, pParent)
	, m_mp(NULL)
{
	m_mp = mp;
	if (!m_mp && pParent)
		m_mp = DYNAMIC_DOWNCAST(CMediaPlayerDlg, pParent);
}

CMpQueueDlg::~CMpQueueDlg()
{
}

void CMpQueueDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MPQ_LIST, m_lc);
	DDX_Control(pDX, IDC_MPQ_UP, m_up);
	DDX_Control(pDX, IDC_MPQ_DOWN, m_down);
	DDX_Control(pDX, IDC_MPQ_REMOVE, m_remove);
	DDX_Control(pDX, IDC_MPQ_CLEAR, m_clear);
	DDX_Control(pDX, IDC_MPQ_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CMpQueueDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_MPQ_UP, &CMpQueueDlg::OnBnClickedUp)
	ON_BN_CLICKED(IDC_MPQ_DOWN, &CMpQueueDlg::OnBnClickedDown)
	ON_BN_CLICKED(IDC_MPQ_REMOVE, &CMpQueueDlg::OnBnClickedRemove)
	ON_BN_CLICKED(IDC_MPQ_CLEAR, &CMpQueueDlg::OnBnClickedClear)
	ON_BN_CLICKED(IDC_MPQ_CLOSE, &CMpQueueDlg::OnBnClickedClose)
	ON_WM_CLOSE()
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CMpQueueDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);

	SetWindowText(LL14(
		L"Up Next キュー", L"Up Next queue", L"File Up Next", L"Coda Up Next", L"Cola Up Next",
		L"Up Next 큐", L"Up Next 队列", L"طابور Up Next", L"Очередь Up Next", L"Up-Next-Warteschlange",
		L"Fila Up Next", L"Up Next-wachtrij", L"Kolejka Up Next", L"Up Next kuyrugu"));
	SetDlgItemText(IDC_MPQ_UP, LL14(L"↑", L"↑", L"↑", L"↑", L"↑", L"↑", L"↑", L"↑", L"↑", L"↑", L"↑", L"↑", L"↑", L"↑"));
	SetDlgItemText(IDC_MPQ_DOWN, LL14(L"↓", L"↓", L"↓", L"↓", L"↓", L"↓", L"↓", L"↓", L"↓", L"↓", L"↓", L"↓", L"↓", L"↓"));
	SetDlgItemText(IDC_MPQ_REMOVE, LL14(
		L"削除", L"Remove", L"Supprimer", L"Rimuovi", L"Quitar",
		L"삭제", L"删除", L"إزالة", L"Удалить", L"Entfernen",
		L"Remover", L"Verwijderen", L"Usun", L"Kaldir"));
	SetDlgItemText(IDC_MPQ_CLEAR, LL14(
		L"クリア", L"Clear", L"Vider", L"Svuota", L"Vaciar",
		L"비우기", L"清空", L"مسح", L"Очистить", L"Leeren",
		L"Limpar", L"Wissen", L"Wyczysc", L"Temizle"));
	SetDlgItemText(IDC_MPQ_CLOSE, LL14(
		L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
		L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen",
		L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));

	m_up.SetGradation(RGB(220, 240, 255), RGB(170, 210, 250), 0, TRUE);
	m_down.SetGradation(RGB(220, 240, 255), RGB(170, 210, 250), 0, TRUE);
	m_remove.SetGradation(RGB(255, 220, 225), RGB(255, 170, 185), 0, TRUE);
	m_clear.SetGradation(RGB(255, 230, 200), RGB(255, 190, 140), 0, TRUE);
	m_close.SetGradation(RGB(235, 235, 240), RGB(200, 200, 210), 0, TRUE);

	m_lc.SetExtendedStyle(m_lc.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_lc.InsertColumn(0, _T("#"), LVCFMT_RIGHT, 36, 0);
	m_lc.InsertColumn(1, LL14(
		L"曲名", L"Title", L"Titre", L"Titolo", L"Titulo",
		L"제목", L"标题", L"العنوان", L"Название", L"Titel",
		L"Titulo", L"Titel", L"Tytul", L"Baslik"), LVCFMT_LEFT, 220, 0);

	RebuildList();
	return TRUE;
}

void CMpQueueDlg::RebuildList()
{
	m_lc.SetRedraw(FALSE);
	m_lc.DeleteAllItems();
	if (!m_mp) {
		m_lc.SetRedraw(TRUE);
		return;
	}
	const int n = m_mp->QueueCount();
	for (int i = 0; i < n; ++i) {
		const int pcIdx = m_mp->QueueAt(i);
		CString no; no.Format(_T("%d"), i + 1);
		CString title;
		if (pl && pl->pc && pcIdx >= 0 && pcIdx < pl->playcnt)
			title = pl->pc[pcIdx].name;
		else
			title.Format(_T("#%d"), pcIdx);
		const int row = m_lc.InsertItem(i, no);
		m_lc.SetItemText(row, 1, title);
		m_lc.SetItemData(row, (DWORD_PTR)i);
	}
	m_lc.SetRedraw(TRUE);
	m_lc.Invalidate();
}

int CMpQueueDlg::GetSelectedRow() const
{
	POSITION pos = m_lc.GetFirstSelectedItemPosition();
	if (!pos) return -1;
	return m_lc.GetNextSelectedItem(pos);
}

void CMpQueueDlg::OnBnClickedUp()
{
	if (!m_mp) return;
	const int row = GetSelectedRow();
	if (row <= 0) return;
	m_mp->QueueMove(row, row - 1);
	RebuildList();
	m_lc.SetItemState(row - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CMpQueueDlg::OnBnClickedDown()
{
	if (!m_mp) return;
	const int row = GetSelectedRow();
	if (row < 0 || row >= m_mp->QueueCount() - 1) return;
	m_mp->QueueMove(row, row + 1);
	RebuildList();
	m_lc.SetItemState(row + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CMpQueueDlg::OnBnClickedRemove()
{
	if (!m_mp) return;
	const int row = GetSelectedRow();
	if (row < 0) return;
	m_mp->QueueRemoveAt(row);
	RebuildList();
	const int n = m_mp->QueueCount();
	if (n > 0) {
		const int next = (row < n) ? row : (n - 1);
		m_lc.SetItemState(next, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
}

void CMpQueueDlg::OnBnClickedClear()
{
	if (!m_mp) return;
	if (m_mp->QueueCount() <= 0) return;
	if (MessageBox(LL14(
		L"キューをすべてクリアしますか？", L"Clear the entire queue?", L"Vider toute la file ?", L"Svuotare tutta la coda?", L"¿Vaciar toda la cola?",
		L"큐를 모두 비울까요?", L"清空整个队列？", L"مسح الطابور بالكامل؟", L"Очистить всю очередь?", L"Gesamte Warteschlange leeren?",
		L"Limpar toda a fila?", L"Hele wachtrij wissen?", L"Wyczyscic cala kolejke?", L"Tum kuyruk temizlensin mi?"),
		LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
			L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdzenie", L"Onay"),
		MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	m_mp->QueueClear();
	RebuildList();
}

void CMpQueueDlg::OnBnClickedClose()
{
	EndDialog(IDCANCEL);
}

void CMpQueueDlg::OnClose()
{
	OnBnClickedClose();
}

HBRUSH CMpQueueDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
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
