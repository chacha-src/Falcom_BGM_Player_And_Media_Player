#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "TagBatchEditDlg.h"
#include "TagEditDlg.h"
#include "FileTagInfo.h"
#include "PlayList.h"
#include "CMediaPlayerDlg.h"
#include "CCustomPopupMenu.h"

extern COggDlg* og;
extern CPlayList* pl;
extern CMediaPlayerDlg* mp;
extern CString filen;
extern int plf;
extern int playf;
extern save savedata;
extern void MpPersistSavedataQuick();

IMPLEMENT_DYNAMIC(CTagBatchEditDlg, CCustomBlurDialogBase)

#define TB_MSG_SYNC (WM_APP + 401)

CTagBatchEditDlg::CTagBatchEditDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CTagBatchEditDlg::IDD, pParent)
	, m_idx(NULL)
	, m_n(0)
	, m_te(NULL)
	, m_syncing(0)
{
}

CTagBatchEditDlg::~CTagBatchEditDlg()
{
}

void CTagBatchEditDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TB_ORIG_L, m_origL);
	DDX_Control(pDX, IDC_TB_EDIT_L, m_editL);
	DDX_Control(pDX, IDC_TB_NAME_OL, m_nameOL);
	DDX_Control(pDX, IDC_TB_ART_OL, m_artOL);
	DDX_Control(pDX, IDC_TB_ALB_OL, m_albOL);
	DDX_Control(pDX, IDC_TB_NAME_EL, m_nameEL);
	DDX_Control(pDX, IDC_TB_ART_EL, m_artEL);
	DDX_Control(pDX, IDC_TB_ALB_EL, m_albEL);
	DDX_Control(pDX, IDC_TB_NAME_O, m_nameO);
	DDX_Control(pDX, IDC_TB_ART_O, m_artO);
	DDX_Control(pDX, IDC_TB_ALB_O, m_albO);
	DDX_Control(pDX, IDC_TB_NAME_E, m_nameE);
	DDX_Control(pDX, IDC_TB_ART_E, m_artE);
	DDX_Control(pDX, IDC_TB_ALB_E, m_albE);
	DDX_Control(pDX, IDC_TB_STATUS, m_status);
	DDX_Control(pDX, IDC_TB_APPLY, m_apply);
	DDX_Control(pDX, IDC_TB_CLOSE, m_close);
	DDX_Control(pDX, IDC_TB_HELP, m_help);
}

BEGIN_MESSAGE_MAP(CTagBatchEditDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_TB_APPLY, &CTagBatchEditDlg::OnBnClickedApply)
	ON_BN_CLICKED(IDC_TB_CLOSE, &CTagBatchEditDlg::OnBnClickedClose)
	ON_BN_CLICKED(IDC_TB_HELP, &CTagBatchEditDlg::OnBnClickedHelp)
	ON_CONTROL_RANGE(EN_VSCROLL, IDC_TB_NAME_O, IDC_TB_ALB_E, &CTagBatchEditDlg::OnEnVScroll)
	ON_CONTROL_RANGE(EN_HSCROLL, IDC_TB_NAME_O, IDC_TB_ALB_E, &CTagBatchEditDlg::OnEnVScroll)
	ON_MESSAGE(TB_MSG_SYNC, &CTagBatchEditDlg::OnPostSync)
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

CCustomEdit* CTagBatchEditDlg::EditAt(int i)
{
	switch (i) {
	case 0: return &m_nameO;
	case 1: return &m_artO;
	case 2: return &m_albO;
	case 3: return &m_nameE;
	case 4: return &m_artE;
	case 5: return &m_albE;
	default: return NULL;
	}
}

void CTagBatchEditDlg::FillTexts()
{
	if (!pl || !pl->pc || !m_idx || m_n <= 0)
		return;
	const int n = m_n;
	const int cap = n * 1100 + 8;
	TCHAR* buf = (TCHAR*)malloc((size_t)cap * sizeof(TCHAR));
	if (!buf)
		return;
	CCustomEdit* lefts[3] = { &m_nameO, &m_artO, &m_albO };
	CCustomEdit* rights[3] = { &m_nameE, &m_artE, &m_albE };
	for (int f = 0; f < 3; ++f) {
		int pos = 0;
		for (int i = 0; i < n; ++i) {
			const int pc = m_idx[i];
			LPCTSTR s = _T("");
			if (pc >= 0 && pc < pl->playcnt) {
				if (f == 0) {
					s = pl->pc[pc].name;
					if (!s[0]) {
						s = pl->pc[pc].fol;
						LPCTSTR slash = _tcsrchr(s, _T('\\'));
						if (!slash) slash = _tcsrchr(s, _T('/'));
						if (slash && slash[1]) s = slash + 1;
					}
				}
				else if (f == 1)
					s = pl->pc[pc].art;
				else
					s = pl->pc[pc].alb;
			}
			if (!s) s = _T("");
			int len = (int)_tcslen(s);
			if (len > 1023) len = 1023;
			if (pos + len + 3 >= cap)
				break;
			if (i > 0) {
				buf[pos++] = _T('\r');
				buf[pos++] = _T('\n');
			}
			if (len > 0) {
				memcpy(buf + pos, s, (size_t)len * sizeof(TCHAR));
				pos += len;
			}
		}
		buf[pos] = 0;
		lefts[f]->SetWindowText(buf);
		rights[f]->SetWindowText(buf);
	}
	free(buf);
}

void CTagBatchEditDlg::SyncFrom(CWnd* src)
{
	if (m_syncing || !src || !src->GetSafeHwnd())
		return;
	m_syncing = 1;
	const int first = (int)src->SendMessage(EM_GETFIRSTVISIBLELINE, 0, 0);
	const int hpos = src->GetScrollPos(SB_HORZ);
	for (int i = 0; i < 6; ++i) {
		CCustomEdit* e = EditAt(i);
		if (!e || e == src || !e->GetSafeHwnd())
			continue;
		const int of = (int)e->SendMessage(EM_GETFIRSTVISIBLELINE, 0, 0);
		const int d = first - of;
		if (d)
			e->LineScroll(d);
		const int oh = e->GetScrollPos(SB_HORZ);
		if (oh != hpos) {
			SCROLLINFO si = {};
			si.cbSize = sizeof(si);
			si.fMask = SIF_POS;
			si.nPos = hpos;
			e->SetScrollInfo(SB_HORZ, &si, TRUE);
			e->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, hpos), 0);
		}
	}
	m_syncing = 0;
}

void CTagBatchEditDlg::OnEnVScroll(UINT nID)
{
	if (m_syncing)
		return;
	CWnd* w = GetDlgItem(nID);
	if (w)
		SyncFrom(w);
}

LRESULT CTagBatchEditDlg::OnPostSync(WPARAM wParam, LPARAM)
{
	HWND h = (HWND)wParam;
	if (h && ::IsWindow(h))
		SyncFrom(CWnd::FromHandle(h));
	return 0;
}

void CTagBatchEditDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CTagBatchEditDlg::LayoutAll()
{
	if (!GetSafeHwnd() || !m_nameO.GetSafeHwnd())
		return;
	CRect rc;
	GetClientRect(&rc);
	int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH < 0)
		capH = 0;
	const int m = 8;
	const int gap = 10;
	const int labH = 16;
	const int btnH = 22;
	const int statusH = 16;
	const int bot = m + btnH + 6;
	int y = capH + 6;
	const int cx = rc.Width();
	const int cy = rc.Height();
	if (cx < 240 || cy < capH + 160)
		return;
	const int mid = cx / 2;
	const int colW = (cx - m * 2 - gap) / 2;
	if (colW < 80)
		return;
	const int leftX = m;
	const int rightX = m + colW + gap;
	if (m_origL.GetSafeHwnd())
		m_origL.SetWindowPos(NULL, leftX, y, colW, labH, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_editL.GetSafeHwnd())
		m_editL.SetWindowPos(NULL, rightX, y, colW, labH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += labH + 2;
	const int remain = cy - y - bot - statusH;
	int boxH = (remain - labH * 3 - 12) / 3;
	if (boxH < 40)
		boxH = 40;
	CCustomStatic* labsL[3] = { &m_nameOL, &m_artOL, &m_albOL };
	CCustomStatic* labsR[3] = { &m_nameEL, &m_artEL, &m_albEL };
	CCustomEdit* boxL[3] = { &m_nameO, &m_artO, &m_albO };
	CCustomEdit* boxR[3] = { &m_nameE, &m_artE, &m_albE };
	for (int i = 0; i < 3; ++i) {
		labsL[i]->SetWindowPos(NULL, leftX, y, colW, labH, SWP_NOZORDER | SWP_NOACTIVATE);
		labsR[i]->SetWindowPos(NULL, rightX, y, colW, labH, SWP_NOZORDER | SWP_NOACTIVATE);
		y += labH + 2;
		boxL[i]->SetWindowPos(NULL, leftX, y, colW, boxH, SWP_NOZORDER | SWP_NOACTIVATE);
		boxR[i]->SetWindowPos(NULL, rightX, y, colW, boxH, SWP_NOZORDER | SWP_NOACTIVATE);
		y += boxH + 6;
	}
	const int by = cy - m - btnH;
	if (m_status.GetSafeHwnd())
		m_status.SetWindowPos(NULL, m, by + 4, cx - m * 2 - 180, statusH, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_apply.GetSafeHwnd())
		m_apply.SetWindowPos(NULL, cx - m - 168, by, 80, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_close.GetSafeHwnd())
		m_close.SetWindowPos(NULL, cx - m - 80, by, 80, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
}

void CTagBatchEditDlg::PersistPos()
{
	if (!::IsWindow(GetSafeHwnd()) || IsIconic())
		return;
	CRect r;
	GetWindowRect(&r);
	savedata.teBatchX = r.left;
	savedata.teBatchY = r.top;
	savedata.teBatchW = r.Width();
	savedata.teBatchH = r.Height();
	savedata.teBatchHasPos = 1;
	MpPersistSavedataQuick();
}

void CTagBatchEditDlg::RestorePos()
{
	int x = savedata.teBatchX, y = savedata.teBatchY;
	int w = savedata.teBatchW, h = savedata.teBatchH;
	if (!savedata.teBatchHasPos || w < 480 || h < 320 || w > 10000 || h > 10000) {
		w = 780;
		h = 560;
		if (GetParent() && ::IsWindow(GetParent()->GetSafeHwnd())) {
			CRect pr;
			GetParent()->GetWindowRect(&pr);
			x = pr.left + 40;
			y = pr.top + 40;
		}
		else {
			x = 100;
			y = 80;
		}
	}
	RECT rcWork{};
	SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
	if (x < rcWork.left - 50 || x > rcWork.right - 50) x = rcWork.left + 40;
	if (y < rcWork.top - 10 || y > rcWork.bottom - 50) y = rcWork.top + 40;
	MoveWindow(x, y, w, h);
}

BOOL CTagBatchEditDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	ModifyStyle(0, WS_CLIPCHILDREN);

	SetWindowText(LL14(L"まとめて編集", L"Batch edit", L"Edition groupée", L"Modifica in blocco",
		L"Edición por lote", L"일괄 편집", L"批量编辑", L"تحرير دفعي",
		L"Пакетное правки", L"Sammelbearbeitung", L"Edicao em lote", L"Batch bewerken",
		L"Edycja zbiorcza", L"Toplu duzenleme"));

	m_origL.SetWindowText(LL14(L"オリジナル", L"Original", L"Original", L"Originale", L"Original",
		L"원본", L"原始", L"الأصل", L"Оригинал", L"Original", L"Original", L"Origineel",
		L"Oryginal", L"Orijinal"));
	m_editL.SetWindowText(LL14(L"編集", L"Edit", L"Edition", L"Modifica", L"Editar",
		L"편집", L"编辑", L"تحرير", L"Правка", L"Bearbeiten", L"Editar", L"Bewerken",
		L"Edycja", L"Duzenle"));
	m_nameOL.SetWindowText(LL14(L"ファイル名", L"Filename", L"Nom de fichier", L"Nome file", L"Nombre de archivo",
		L"파일명", L"文件名", L"اسم الملف", L"Имя файла", L"Dateiname", L"Nome do arquivo", L"Bestandsnaam",
		L"Nazwa pliku", L"Dosya adi"));
	m_nameEL.SetWindowText(LL14(L"ファイル名", L"Filename", L"Nom de fichier", L"Nome file", L"Nombre de archivo",
		L"파일명", L"文件名", L"اسم الملف", L"Имя файла", L"Dateiname", L"Nome do arquivo", L"Bestandsnaam",
		L"Nazwa pliku", L"Dosya adi"));
	m_artOL.SetWindowText(LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista",
		L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Interpret", L"Artista", L"Artiest",
		L"Artysta", L"Sanatci"));
	m_artEL.SetWindowText(LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista",
		L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Interpret", L"Artista", L"Artiest",
		L"Artysta", L"Sanatci"));
	m_albOL.SetWindowText(LL14(L"アルバム", L"Album", L"Album", L"Album", L"Album",
		L"앨범", L"专辑", L"الألبوم", L"Альбом", L"Album", L"Album", L"Album",
		L"Album", L"Album"));
	m_albEL.SetWindowText(LL14(L"アルバム", L"Album", L"Album", L"Album", L"Album",
		L"앨범", L"专辑", L"الألبوم", L"Альбом", L"Album", L"Album", L"Album",
		L"Album", L"Album"));
	m_apply.SetWindowText(LL14(L"適用", L"Apply", L"Appliquer", L"Applica", L"Aplicar",
		L"적용", L"应用", L"تطبيق", L"Применить", L"Anwenden", L"Aplicar", L"Toepassen",
		L"Zastosuj", L"Uygula"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar",
		L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten",
		L"Zamknij", L"Kapat"));

	for (int i = 0; i < 6; ++i) {
		CCustomEdit* e = EditAt(i);
		if (!e || !e->GetSafeHwnd())
			continue;
		e->SendMessage(EM_LIMITTEXT, (WPARAM)(4 * 1024 * 1024), 0);
	}
	m_nameO.SetReadOnly(TRUE);
	m_artO.SetReadOnly(TRUE);
	m_albO.SetReadOnly(TRUE);
	m_apply.SetAeroMode(FALSE);
	m_close.SetAeroMode(FALSE);
	m_status.SetAeroMode(FALSE);
	m_origL.SetAeroMode(FALSE);
	m_editL.SetAeroMode(FALSE);
	m_nameOL.SetAeroMode(FALSE);
	m_artOL.SetAeroMode(FALSE);
	m_albOL.SetAeroMode(FALSE);
	m_nameEL.SetAeroMode(FALSE);
	m_artEL.SetAeroMode(FALSE);
	m_albEL.SetAeroMode(FALSE);

	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);

	FillTexts();
	m_status.SetWindowText(L"");

	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		m_tooltip.AddTool(&m_help, LL14(
			L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida",
			L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل",
			L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen",
			L"Pokaz przewodnik", L"Islem kilavuzunu goster"));
		m_tooltip.AddTool(&m_nameO, LL14(
			L"選択曲の元のファイル名（タイトル）。コピー可、編集不可", L"Original filenames (titles). Copyable, read-only",
			L"Noms d'origine. Copiables, lecture seule", L"Nomi originali. Copiabili, sola lettura",
			L"Nombres originales. Copiables, solo lectura", L"원본 파일명(제목). 복사 가능, 읽기 전용",
			L"原始文件名（标题）。可复制、只读", L"أسماء أصلية. نسخ فقط",
			L"Исходные имена. Можно копировать", L"Originalnamen. Kopierbar, nur lesen",
			L"Nomes originais. Copiaveis, somente leitura", L"Oorspronkelijke namen. Kopieerbaar, alleen-lezen",
			L"Oryginalne nazwy. Do kopiowania, tylko odczyt", L"Orijinal adlar. Kopyalanabilir, salt okunur"));
		m_tooltip.AddTool(&m_artO, LL14(
			L"選択曲の元のアーティスト。コピー可、編集不可", L"Original artists. Copyable, read-only",
			L"Artistes d'origine. Copiables, lecture seule", L"Artisti originali. Copiabili, sola lettura",
			L"Artistas originales. Copiables, solo lectura", L"원본 아티스트. 복사 가능, 읽기 전용",
			L"原始艺术家。可复制、只读", L"فنانون أصليون. نسخ فقط",
			L"Исходные исполнители. Можно копировать", L"Original-Interpreten. Kopierbar, nur lesen",
			L"Artistas originais. Copiaveis, somente leitura", L"Oorspronkelijke artiesten. Kopieerbaar, alleen-lezen",
			L"Oryginalni artysci. Do kopiowania, tylko odczyt", L"Orijinal sanatcilar. Kopyalanabilir, salt okunur"));
		m_tooltip.AddTool(&m_albO, LL14(
			L"選択曲の元のアルバム。コピー可、編集不可", L"Original albums. Copyable, read-only",
			L"Albums d'origine. Copiables, lecture seule", L"Album originali. Copiabili, sola lettura",
			L"Álbumes originales. Copiables, solo lectura", L"원본 앨범. 복사 가능, 읽기 전용",
			L"原始专辑。可复制、只读", L"ألبومات أصلية. نسخ فقط",
			L"Исходные альбомы. Можно копировать", L"Original-Alben. Kopierbar, nur lesen",
			L"Albuns originais. Copiaveis, somente leitura", L"Oorspronkelijke albums. Kopieerbaar, alleen-lezen",
			L"Oryginalne albumy. Do kopiowania, tylko odczyt", L"Orijinal albumler. Kopyalanabilir, salt okunur"));
		m_tooltip.AddTool(&m_nameE, LL14(
			L"編集するファイル名（タイトル）。1行が1曲。適用でタグへ書く", L"Edit filenames (titles). One line per track. Apply writes tags",
			L"Noms a editer. Une ligne par piste. Appliquer ecrit les tags", L"Nomi da modificare. Una riga per brano. Applica scrive i tag",
			L"Nombres a editar. Una linea por pista. Aplicar escribe etiquetas", L"편집할 파일명(제목). 한 줄=한 곡. 적용 시 태그에 기록",
			L"要编辑的文件名（标题）。一行一首。应用写入标签", L"أسماء للتحرير. سطر لكل مقطع. تطبيق يكتب الوسوم",
			L"Имена для правки. Строка = трек. Применить пишет теги", L"Dateinamen bearbeiten. Eine Zeile pro Titel. Anwenden schreibt Tags",
			L"Nomes para editar. Uma linha por faixa. Aplicar grava tags", L"Namen bewerken. Een regel per nummer. Toepassen schrijft tags",
			L"Nazwy do edycji. Wiersz = utwor. Zastosuj zapisuje tagi", L"Duzenlenecek adlar. Satir=parca. Uygula etiket yazar"));
		m_tooltip.AddTool(&m_artE, LL14(
			L"編集するアーティスト。1行が1曲。適用でタグへ書く", L"Edit artists. One line per track. Apply writes tags",
			L"Artistes a editer. Une ligne par piste", L"Artisti da modificare. Una riga per brano",
			L"Artistas a editar. Una linea por pista", L"편집할 아티스트. 한 줄=한 곡",
			L"要编辑的艺术家。一行一首", L"فنانون للتحرير. سطر لكل مقطع",
			L"Исполнители для правки. Строка = трек", L"Interpreten bearbeiten. Eine Zeile pro Titel",
			L"Artistas para editar. Uma linha por faixa", L"Artiesten bewerken. Een regel per nummer",
			L"Artysci do edycji. Wiersz = utwor", L"Duzenlenecek sanatcilar. Satir=parca"));
		m_tooltip.AddTool(&m_albE, LL14(
			L"編集するアルバム。1行が1曲。適用でタグへ書く", L"Edit albums. One line per track. Apply writes tags",
			L"Albums a editer. Une ligne par piste", L"Album da modificare. Una riga per brano",
			L"Álbumes a editar. Una linea por pista", L"편집할 앨범. 한 줄=한 곡",
			L"要编辑的专辑。一行一首", L"ألبومات للتحرير. سطر لكل مقطع",
			L"Альбомы для правки. Строка = трек", L"Alben bearbeiten. Eine Zeile pro Titel",
			L"Albuns para editar. Uma linha por faixa", L"Albums bewerken. Een regel per nummer",
			L"Albumy do edycji. Wiersz = utwor", L"Duzenlenecek albumler. Satir=parca"));
		m_tooltip.AddTool(&m_apply, LL14(
			L"右欄の内容を各曲のタグへまとめて書き込みます", L"Write the right-hand lines to each track's tags",
			L"Ecrire les lignes de droite dans les tags", L"Scrivi le righe di destra nei tag",
			L"Escribir las lineas derechas en las etiquetas", L"오른쪽 내용을 각 곡 태그에 한꺼번에 씁니다",
			L"将右侧内容一并写入各曲标签", L"كتابة الأسطر اليمنى إلى الوسوم",
			L"Записать правые строки в теги", L"Rechte Zeilen in die Tags schreiben",
			L"Gravar as linhas da direita nas tags", L"Rechterregels naar tags schrijven",
			L"Zapisz prawe wiersze do tagow", L"Sag satirlari etiketlere yaz"));
		m_tooltip.AddTool(&m_close, LL14(
			L"書き込まずに閉じます", L"Close without writing", L"Fermer sans ecrire", L"Chiudi senza scrivere",
			L"Cerrar sin escribir", L"쓰지 않고 닫습니다", L"不写入并关闭", L"إغلاق دون كتابة",
			L"Закрыть без записи", L"Schliessen ohne Schreiben", L"Fechar sem gravar", L"Sluiten zonder schrijven",
			L"Zamknij bez zapisu", L"Yazmadan kapat"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 420, 10000);
	}

	EnableMainWindowLock(&savedata.teBatchMainLock);
	RestorePos();
	CCC_CaptionEnsureHostAcrylic(m_hWnd);
	LayoutAll();
	LayoutHelpBtn();
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	return TRUE;
}

BOOL CTagBatchEditDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	if (pMsg && (pMsg->message == WM_MOUSEWHEEL
		|| (pMsg->message == WM_KEYDOWN
			&& (pMsg->wParam == VK_UP || pMsg->wParam == VK_DOWN
				|| pMsg->wParam == VK_PRIOR || pMsg->wParam == VK_NEXT
				|| pMsg->wParam == VK_LEFT || pMsg->wParam == VK_RIGHT
				|| pMsg->wParam == VK_HOME || pMsg->wParam == VK_END)))) {
		HWND h = pMsg->hwnd;
		for (int i = 0; i < 6; ++i) {
			CCustomEdit* e = EditAt(i);
			if (e && e->GetSafeHwnd() == h) {
				PostMessage(TB_MSG_SYNC, (WPARAM)h, 0);
				break;
			}
		}
	}
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CTagBatchEditDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	if (!pWnd)
		return;
	int which = -1;
	for (int i = 0; i < 6; ++i) {
		CCustomEdit* e = EditAt(i);
		if (e && e == pWnd) {
			which = i;
			break;
		}
	}
	if (which < 0)
		return;
	const BOOL isLeft = (which < 3);
	if (point.x == -1 && point.y == -1) {
		CRect r;
		pWnd->GetWindowRect(&r);
		point.x = r.left + 8;
		point.y = r.top + 8;
	}
	enum {
		TBCM_CUT = 1, TBCM_COPY, TBCM_PASTE, TBCM_SELALL,
		TBCM_FROM_NAME, TBCM_FROM_ART, TBCM_FROM_ALB, TBCM_FROM_ALL
	};
	CCustomPopupMenu menu;
	if (!isLeft)
		menu.AddCommand(TBCM_CUT,
			LL14(L"切り取り", L"Cut", L"Couper", L"Taglia", L"Cortar", L"잘라내기", L"剪切", L"قص",
				L"Вырезать", L"Ausschneiden", L"Recortar", L"Knippen", L"Wytnij", L"Kes"));
	menu.AddCommand(TBCM_COPY,
		LL14(L"コピー", L"Copy", L"Copier", L"Copia", L"Copiar", L"복사", L"复制", L"نسخ",
			L"Копировать", L"Kopieren", L"Copiar", L"Kopieren", L"Kopiuj", L"Kopyala"));
	if (!isLeft)
		menu.AddCommand(TBCM_PASTE,
			LL14(L"貼り付け", L"Paste", L"Coller", L"Incolla", L"Pegar", L"붙여넣기", L"粘贴", L"لصق",
				L"Вставить", L"Einfuegen", L"Colar", L"Plakken", L"Wklej", L"Yapistir"));
	menu.AddCommand(TBCM_SELALL,
		LL14(L"すべて選択", L"Select all", L"Tout selectionner", L"Seleziona tutto", L"Seleccionar todo",
			L"모두 선택", L"全选", L"تحديد الكل", L"Выделить всё", L"Alles auswaehlen",
			L"Selecionar tudo", L"Alles selecteren", L"Zaznacz wszystko", L"Tumunu sec"));
	if (!isLeft) {
		CCustomPopupMenu* sub = menu.AddSubMenu(
			LL14(L"左からコピー", L"Copy from left", L"Copier depuis la gauche", L"Copia da sinistra",
				L"Copiar desde la izquierda", L"왼쪽에서 복사", L"从左侧复制", L"نسخ من اليسار",
				L"Копировать слева", L"Von links kopieren", L"Copiar da esquerda", L"Kopieren van links",
				L"Kopiuj z lewej", L"Soldan kopyala"),
			LL14(L"オリジナル欄の内容を右側へコピーします", L"Copy original column text to the right",
				L"Copier le texte original a droite", L"Copia il testo originale a destra",
				L"Copiar el texto original a la derecha", L"원본 열을 오른쪽으로 복사",
				L"将原始栏复制到右侧", L"نسخ العمود الأصلي إلى اليمين",
				L"Скопировать оригинал направо", L"Originale nach rechts kopieren",
				L"Copiar o original para a direita", L"Origineel naar rechts kopieren",
				L"Kopiuj oryginal w prawo", L"Orijinali saga kopyala"));
		if (sub) {
			sub->AddCommand(TBCM_FROM_NAME, LL14(L"ファイル名", L"Filename", L"Nom", L"Nome", L"Nombre",
				L"파일명", L"文件名", L"اسم", L"Имя", L"Dateiname", L"Nome", L"Naam", L"Nazwa", L"Ad"));
			sub->AddCommand(TBCM_FROM_ART, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista",
				L"아티스트", L"艺术家", L"فنان", L"Исполнитель", L"Interpret", L"Artista", L"Artiest", L"Artysta", L"Sanatci"));
			sub->AddCommand(TBCM_FROM_ALB, LL14(L"アルバム", L"Album", L"Album", L"Album", L"Album",
				L"앨범", L"专辑", L"ألبوم", L"Альбом", L"Album", L"Album", L"Album", L"Album", L"Album"));
			sub->AddCommand(TBCM_FROM_ALL, LL14(L"全部", L"All", L"Tout", L"Tutto", L"Todo",
				L"전부", L"全部", L"الكل", L"Все", L"Alles", L"Tudo", L"Alles", L"Wszystko", L"Tumu"));
		}
	}
	const UINT cmd = menu.Track(point, this);
	if (!cmd)
		return;
	if (cmd == TBCM_CUT)
		pWnd->SendMessage(WM_CUT);
	else if (cmd == TBCM_COPY)
		pWnd->SendMessage(WM_COPY);
	else if (cmd == TBCM_PASTE)
		pWnd->SendMessage(WM_PASTE);
	else if (cmd == TBCM_SELALL)
		((CEdit*)pWnd)->SetSel(0, -1);
	else if (cmd == TBCM_FROM_NAME || cmd == TBCM_FROM_ART || cmd == TBCM_FROM_ALB || cmd == TBCM_FROM_ALL) {
		CString t;
		if (cmd == TBCM_FROM_NAME || cmd == TBCM_FROM_ALL) {
			m_nameO.GetWindowText(t);
			m_nameE.SetWindowText(t);
		}
		if (cmd == TBCM_FROM_ART || cmd == TBCM_FROM_ALL) {
			m_artO.GetWindowText(t);
			m_artE.SetWindowText(t);
		}
		if (cmd == TBCM_FROM_ALB || cmd == TBCM_FROM_ALL) {
			m_albO.GetWindowText(t);
			m_albE.SetWindowText(t);
		}
		SyncFrom(&m_nameE);
	}
}

void CTagBatchEditDlg::OnBnClickedApply()
{
	if (!pl || !pl->pc || !m_idx || m_n <= 0)
		return;
	const int n = m_n;
	TCHAR* names = (TCHAR*)malloc((size_t)n * 1024 * sizeof(TCHAR));
	TCHAR* arts = (TCHAR*)malloc((size_t)n * 1024 * sizeof(TCHAR));
	TCHAR* albs = (TCHAR*)malloc((size_t)n * 1024 * sizeof(TCHAR));
	if (!names || !arts || !albs) {
		if (names) free(names);
		if (arts) free(arts);
		if (albs) free(albs);
		return;
	}
	ZeroMemory(names, (size_t)n * 1024 * sizeof(TCHAR));
	ZeroMemory(arts, (size_t)n * 1024 * sizeof(TCHAR));
	ZeroMemory(albs, (size_t)n * 1024 * sizeof(TCHAR));
	int got[3] = { 0, 0, 0 };
	TCHAR* dests[3] = { names, arts, albs };
	CCustomEdit* srcs[3] = { &m_nameE, &m_artE, &m_albE };
	for (int f = 0; f < 3; ++f) {
		const int tlen = srcs[f]->GetWindowTextLength();
		TCHAR* buf = (TCHAR*)malloc(((size_t)tlen + 4) * sizeof(TCHAR));
		if (!buf)
			continue;
		srcs[f]->GetWindowText(buf, tlen + 1);
		int lineI = 0;
		TCHAR* s = buf;
		for (;;) {
			TCHAR* e = s;
			while (*e && *e != _T('\r') && *e != _T('\n'))
				++e;
			if (lineI < n) {
				int len = (int)(e - s);
				if (len > 1023) len = 1023;
				TCHAR* d = dests[f] + lineI * 1024;
				if (len > 0)
					memcpy(d, s, (size_t)len * sizeof(TCHAR));
				d[len] = 0;
			}
			++lineI;
			if (!*e)
				break;
			if (*e == _T('\r')) ++e;
			if (*e == _T('\n')) ++e;
			s = e;
			if (!*s)
				break;
		}
		got[f] = (lineI > n) ? n : lineI;
		free(buf);
	}

	BOOL needResume = FALSE;
	CString playPhys;
	if (og && (plf || playf) && !filen.IsEmpty()) {
		playPhys = PlPhysicalMediaPath(filen);
		if (playPhys.IsEmpty()) playPhys = filen;
		for (int i = 0; i < n; ++i) {
			const int pc = m_idx[i];
			if (pc < 0 || pc >= pl->playcnt) continue;
			CString p = PlPhysicalMediaPath(pl->pc[pc].fol);
			if (p.IsEmpty()) p = pl->pc[pc].fol;
			if (!p.IsEmpty() && _tcsicmp(p, playPhys) == 0) {
				needResume = TRUE;
				break;
			}
		}
	}
	int savecheckBak = savedata.savecheck;
	if (needResume) {
		OggArmSilentResumeFromCurrent();
		savedata.savecheck = 0;
		og->stop1();
		savedata.savecheck = savecheckBak;
	}

	int okN = 0, skipN = 0, failN = 0;
	for (int i = 0; i < n; ++i) {
		const int pc = m_idx[i];
		if (pc < 0 || pc >= pl->playcnt) { failN++; continue; }
		CString path = PlPhysicalMediaPath(pl->pc[pc].fol);
		if (path.IsEmpty()) path = pl->pc[pc].fol;
		if (path.IsEmpty()) { failN++; continue; }
		CString el = path;
		el.MakeLower();
		if (el.GetLength() >= 4 && (el.Right(4) == _T(".dff") || el.Right(4) == _T(".wsd"))) {
			skipN++;
			continue;
		}
		FileTagFields fields;
		ReadFileTagFields(path, fields);
		if (i < got[0]) fields.title = dests[0] + i * 1024;
		if (i < got[1]) fields.artist = dests[1] + i * 1024;
		if (i < got[2]) fields.album = dests[2] + i * 1024;
		if (!WriteFileTagFields(path, fields)) {
			failN++;
			continue;
		}
		okN++;
		PlJakDiskForget(pl->pc[pc].fol);
		if (mp) {
			for (int j = 0; j < CMediaPlayerDlg::kMpJakN; ++j) {
				if (mp->m_jakKey[j][0] && _tcsicmp(mp->m_jakKey[j], pl->pc[pc].fol) == 0) {
					if (mp->m_jakBmp[j]) { ::DeleteObject(mp->m_jakBmp[j]); mp->m_jakBmp[j] = NULL; }
					mp->m_jakKey[j][0] = 0;
					mp->m_jakTick[j] = 0;
					mp->m_jakRow[j] = -1;
					break;
				}
			}
		}
		if (i < got[0]) {
			_tcsncpy(pl->pc[pc].name, dests[0] + i * 1024, _countof(pl->pc[pc].name) - 1);
			pl->pc[pc].name[_countof(pl->pc[pc].name) - 1] = 0;
			if (m_te) {
				if (!m_te->multiFile) {
					_tcsncpy(m_te->pc.name, pl->pc[pc].name, _countof(m_te->pc.name) - 1);
					m_te->pc.name[_countof(m_te->pc.name) - 1] = 0;
				}
				else if (i < (int)m_te->pcs.size()) {
					_tcsncpy(m_te->pcs[i].name, pl->pc[pc].name, _countof(m_te->pcs[i].name) - 1);
					m_te->pcs[i].name[_countof(m_te->pcs[i].name) - 1] = 0;
				}
			}
		}
		if (i < got[1]) {
			_tcsncpy(pl->pc[pc].art, dests[1] + i * 1024, _countof(pl->pc[pc].art) - 1);
			pl->pc[pc].art[_countof(pl->pc[pc].art) - 1] = 0;
			if (m_te) {
				if (!m_te->multiFile) {
					_tcsncpy(m_te->pc.art, pl->pc[pc].art, _countof(m_te->pc.art) - 1);
					m_te->pc.art[_countof(m_te->pc.art) - 1] = 0;
				}
				else if (i < (int)m_te->pcs.size()) {
					_tcsncpy(m_te->pcs[i].art, pl->pc[pc].art, _countof(m_te->pcs[i].art) - 1);
					m_te->pcs[i].art[_countof(m_te->pcs[i].art) - 1] = 0;
				}
			}
		}
		if (i < got[2]) {
			_tcsncpy(pl->pc[pc].alb, dests[2] + i * 1024, _countof(pl->pc[pc].alb) - 1);
			pl->pc[pc].alb[_countof(pl->pc[pc].alb) - 1] = 0;
			if (m_te) {
				if (!m_te->multiFile) {
					_tcsncpy(m_te->pc.alb, pl->pc[pc].alb, _countof(m_te->pc.alb) - 1);
					m_te->pc.alb[_countof(m_te->pc.alb) - 1] = 0;
				}
				else if (i < (int)m_te->pcs.size()) {
					_tcsncpy(m_te->pcs[i].alb, pl->pc[pc].alb, _countof(m_te->pcs[i].alb) - 1);
					m_te->pcs[i].alb[_countof(m_te->pcs[i].alb) - 1] = 0;
				}
			}
		}
	}
	free(names);
	free(arts);
	free(albs);

	pl->Save();
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->RefreshList(TRUE);
	if (::IsWindow(pl->m_lc.GetSafeHwnd()))
		pl->m_lc.Invalidate(FALSE);

	CString msg;
	if (skipN > 0 && okN == 0 && failN == 0) {
		msg = LL14(L"この形式はタグ書き込み非対応です (.dff/.wsd)",
			L"Tag writing is not supported for this format (.dff/.wsd)",
			L"Ecriture de tags non prise en charge (.dff/.wsd)",
			L"Scrittura tag non supportata (.dff/.wsd)",
			L"Escritura de etiquetas no compatible (.dff/.wsd)",
			L"이 형식은 태그 쓰기 미지원 (.dff/.wsd)",
			L"此格式不支持写入标签 (.dff/.wsd)",
			L"Tag writing unsupported (.dff/.wsd)",
			L"Запись тегов не поддерживается (.dff/.wsd)",
			L"Tag-Schreiben nicht unterstuetzt (.dff/.wsd)",
			L"Gravacao de tags nao suportada (.dff/.wsd)",
			L"Tag schrijven niet ondersteund (.dff/.wsd)",
			L"Zapis tagow nieobslugiwany (.dff/.wsd)",
			L"Bu bicimde etiket yazimi desteklenmiyor (.dff/.wsd)");
		m_status.SetWindowText(msg);
		MessageBox(msg, LL14(L"まとめて編集", L"Batch edit", L"Edition groupée", L"Modifica in blocco",
			L"Edición por lote", L"일괄 편집", L"批量编辑", L"تحرير دفعي",
			L"Пакетное правки", L"Sammelbearbeitung", L"Edicao em lote", L"Batch bewerken",
			L"Edycja zbiorcza", L"Toplu duzenleme"), MB_OK | MB_ICONWARNING);
		if (needResume && og && ::IsWindow(og->GetSafeHwnd()))
			RequestPlaybackRestart(og->GetSafeHwnd());
		return;
	}
	if (failN == 0 && okN > 0) {
		msg = LL14(L"適用しました", L"Applied", L"Applique", L"Applicato", L"Aplicado", L"적용됨", L"已应用", L"Applied",
			L"Применено", L"Angewendet", L"Aplicado", L"Toegepast", L"Zastosowano", L"Uygulandi");
		if (skipN > 0) {
			CString extra;
			extra.Format(LL14(L" (スキップ: %d)", L" (skipped: %d)", L" (ignores: %d)", L" (saltati: %d)",
				L" (omitidos: %d)", L" (건너뜀: %d)", L" (跳过: %d)", L" (skipped: %d)",
				L" (пропущено: %d)", L" (uebersprungen: %d)", L" (ignorados: %d)", L" (overgeslagen: %d)",
				L" (pominieto: %d)", L" (atlandi: %d)"), skipN);
			msg += extra;
		}
		m_status.SetWindowText(msg);
		if (needResume && og && ::IsWindow(og->GetSafeHwnd()))
			RequestPlaybackRestart(og->GetSafeHwnd());
		EndDialog(IDOK);
	}
	else {
		msg = LL14(L"適用に失敗したファイルがあります", L"Some files failed to apply",
			L"Echec pour certains fichiers", L"Alcuni file non applicati", L"Algunos archivos fallaron",
			L"일부 파일 적용 실패", L"部分文件应用失败", L"Some files failed",
			L"Часть файлов не применена", L"Einige Dateien fehlgeschlagen", L"Alguns arquivos falharam",
			L"Sommige bestanden mislukt", L"Niektore pliki nieudane", L"Bazi dosyalar basarisiz");
		m_status.SetWindowText(msg);
		MessageBox(msg, LL14(L"まとめて編集", L"Batch edit", L"Edition groupée", L"Modifica in blocco",
			L"Edición por lote", L"일괄 편집", L"批量编辑", L"تحرير دفعي",
			L"Пакетное правки", L"Sammelbearbeitung", L"Edicao em lote", L"Batch bewerken",
			L"Edycja zbiorcza", L"Toplu duzenleme"), MB_OK | MB_ICONERROR);
		if (needResume && og && ::IsWindow(og->GetSafeHwnd()))
			RequestPlaybackRestart(og->GetSafeHwnd());
	}
}

void CTagBatchEditDlg::OnBnClickedClose()
{
	EndDialog(IDCANCEL);
}

void CTagBatchEditDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		LayoutAll();
		CCC_CaptionEnsureHostAcrylic(m_hWnd);
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

namespace {

class CTbHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_TB_HELP };
	explicit CTbHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CTbHelpDlg* g_tbHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CTbHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CTbHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"まとめて編集操作ガイド", L"Batch Tag Edit Guide", L"Guide d'édition groupée", L"Guida modifica in blocco",
		L"Guía de edición por lote", L"일괄 편집 가이드", L"批量编辑指南", L"دليل التحرير الدفعي",
		L"Руководство пакетного правки", L"Sammelbearbeitungs-Anleitung", L"Guia de edicao em lote", L"Batch-bewerkingsgids",
		L"Przewodnik edycji zbiorczej", L"Toplu duzenleme kilavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CTbHelpDlg::OnOK() { DestroyWindow(); }
void CTbHelpDlg::OnCancel() { DestroyWindow(); }
void CTbHelpDlg::OnClose() { DestroyWindow(); }

void CTbHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_tbHelpDlg == this)
		g_tbHelpDlg = nullptr;
	delete this;
}

BOOL CTbHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect r;
	GetClientRect(&r);
	pDC->FillSolidRect(r, RGB(248, 248, 252));
	return TRUE;
}

void CTbHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());
	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 1;
	auto title = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(55, 45, 85));
		dc.TextOut(x, y, t);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(65, 65, 80));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 115));
		dc.TextOut(x, y, t);
	};
	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"まとめて編集操作ガイド", L"Batch Tag Edit — Guide", L"Guide edition groupée", L"Guida modifica in blocco",
		L"Guía edición por lote", L"일괄 편집 가이드", L"批量编辑指南", L"دليل التحرير الدفعي",
		L"Пакетное правки — руководство", L"Sammelbearbeitung — Guide", L"Guia edicao em lote", L"Batch bewerken — gids",
		L"Edycja zbiorcza — przewodnik", L"Toplu duzenleme kilavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"複数曲のファイル名・アーティスト・アルバムを、テキストとして一度に直してタグへ書きます。",
		L"Rewrite filename, artist, and album for many tracks as text, then write tags.",
		L"Corriger nom, artiste, album de plusieurs pistes en texte, puis ecrire les tags.",
		L"Correggi nome, artista, album di piu brani come testo, poi scrivi i tag.",
		L"Reescribe nombre, artista y album de varias pistas como texto y escribe etiquetas.",
		L"여러 곡의 파일명·아티스트·앨범을 텍스트로 한 번에 고쳐 태그에 씁니다.",
		L"把多首的文件名、艺术家、专辑当文本一次改好并写入标签。",
		L"أعد كتابة الاسم والفنان والألبوم كنص ثم اكتب الوسوم.",
		L"Правьте имя, исполнителя и альбом многих треков текстом и пишите теги.",
		L"Dateiname, Artist und Album vieler Titel als Text aendern und Tags schreiben.",
		L"Reescreva nome, artista e album de varias faixas como texto e grave as tags.",
		L"Bestandsnaam, artiest en album van veel nummers als tekst aanpassen en tags schrijven.",
		L"Zmien nazwe, artyste i album wielu utworow jako tekst i zapisz tagi.",
		L"Cok parcada ad, sanatci, albumu metin olarak duzenleyip etiketlere yazin."));
	y += lh + 4;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, rc.Width() - L * 2, min(140, max(112, rc.Height() / 5)),
		CCC_HELPDEMO_KLIST);

	title(L, y, LL14(L"左右 6 欄", L"Six columns", L"Six zones", L"Sei campi", L"Seis campos", L"좌우 6칸", L"左右六栏", L"ستة حقول",
		L"Шесть полей", L"Sechs Felder", L"Seis campos", L"Zes velden", L"Szesc pol", L"Alti alan"));
	y += titleLh;
	body(L, y, LL14(L"・左 …… オリジナル。コピーはできるが編集不可", L"· Left …… original. Copyable, read-only", L"· Gauche …… original. Copiable, lecture seule", L"· Sinistra …… originale. Copiabile, sola lettura",
		L"· Izquierda …… original. Copiable, solo lectura", L"· 왼쪽 …… 원본. 복사 가능, 읽기 전용", L"· 左 …… 原始。可复制、只读", L"· يسار …… أصل. نسخ فقط",
		L"· Слева …… оригинал. Можно копировать", L"· Links …… Original. Kopierbar, nur lesen", L"· Esquerda …… original. Copiavel, somente leitura", L"· Links …… origineel. Kopieerbaar, alleen-lezen",
		L"· Lewa …… oryginal. Do kopiowania, tylko odczyt", L"· Sol …… orijinal. Kopyalanabilir, salt okunur")); y += lh;
	body(L, y, LL14(L"・右 …… 編集。1行が1曲。改行で曲の順が揃います", L"· Right …… edit. One line per track; line order matches selection", L"· Droite …… edition. Une ligne par piste, dans l'ordre de selection", L"· Destra …… modifica. Una riga per brano, stesso ordine",
		L"· Derecha …… editar. Una linea por pista, mismo orden", L"· 오른쪽 …… 편집. 한 줄=한 곡, 선택 순서", L"· 右 …… 编辑。一行一首，顺序与选择相同", L"· يمين …… تحرير. سطر لكل مقطع بنفس الترتيب",
		L"· Справа …… правка. Строка = трек, тот же порядок", L"· Rechts …… bearbeiten. Eine Zeile pro Titel, gleiche Reihenfolge", L"· Direita …… editar. Uma linha por faixa, mesma ordem", L"· Rechts …… bewerken. Een regel per nummer, zelfde volgorde",
		L"· Prawa …… edycja. Wiersz = utwor, ta sama kolejnosc", L"· Sag …… duzenle. Satir=parca, ayni sira")); y += lh;
	body(L, y, LL14(L"・スクロール …… 6 つの欄の縦横スクロールが連動します", L"· Scroll …… all six boxes scroll together", L"· Defilement …… les 6 zones defilent ensemble", L"· Scorrimento …… le 6 caselle scorrono insieme",
		L"· Desplazamiento …… las 6 cajas se mueven juntas", L"· 스크롤 …… 6칸이 함께 움직입니다", L"· 滚动 …… 六个框同步滚动", L"· تمرير …… الحقول الستة تتحرك معاً",
		L"· Прокрутка …… все шесть полей синхронны", L"· Scroll …… alle sechs Felder laufen zusammen", L"· Rolagem …… as 6 caixas rolam juntas", L"· Scroll …… alle zes vakken samen",
		L"· Przewijanie …… szesc pol razem", L"· Kaydirma …… alti kutu birlikte")); y += lh + 4;

	title(L, y, LL14(L"適用", L"Apply", L"Appliquer", L"Applica", L"Aplicar", L"적용", L"应用", L"تطبيق",
		L"Применить", L"Anwenden", L"Aplicar", L"Toepassen", L"Zastosuj", L"Uygula"));
	y += titleLh;
	body(L, y, LL14(L"・適用 …… 右欄を TITLE / アーティスト / アルバム タグへ書き込みます（ファイル名のリネームはしません）", L"· Apply …… writes TITLE / artist / album tags (does not rename files)", L"· Appliquer …… ecrit les tags TITLE / artiste / album (sans renommer)", L"· Applica …… scrive i tag TITLE / artista / album (non rinomina)",
		L"· Aplicar …… escribe TITLE / artista / album (no renombra)", L"· 적용 …… TITLE/아티스트/앨범 태그에 기록 (파일명 변경 없음)", L"· 应用 …… 写入 TITLE/艺术家/专辑标签（不重命名文件）", L"· تطبيق …… يكتب TITLE/فنان/ألبوم (دون إعادة تسمية)",
		L"· Применить …… пишет TITLE / исполнитель / альбом (без переименования)", L"· Anwenden …… schreibt TITLE / Artist / Album (ohne Umbenennen)", L"· Aplicar …… grava TITLE / artista / album (nao renomeia)", L"· Toepassen …… schrijft TITLE / artiest / album (geen hernoemen)",
		L"· Zastosuj …… zapisuje TITLE / artysta / album (bez zmiany nazwy pliku)", L"· Uygula …… TITLE / sanatci / album yazar (yeniden adlandirma yok)")); y += lh;
	body(L, y, LL14(L"・行が足りない曲は変更しません。空行は空タグとして書きます", L"· Tracks past the last line are unchanged. An empty line writes an empty tag", L"· Pistes au-dela de la derniere ligne inchangees. Ligne vide = tag vide", L"· Brani oltre l'ultima riga invariati. Riga vuota = tag vuoto",
		L"· Pistas tras la ultima linea no cambian. Linea vacia = etiqueta vacia", L"· 줄이 모자란 곡은 유지. 빈 줄은 빈 태그", L"· 行数不够的曲目不改。空行写成空标签", L"· المقاطع بعد آخر سطر لا تتغير. سطر فارغ = وسم فارغ",
		L"· Треки после последней строки не меняются. Пустая строка = пустой тег", L"· Titel nach der letzten Zeile unveraendert. Leere Zeile = leerer Tag", L"· Faixas apos a ultima linha nao mudam. Linha vazia = tag vazia", L"· Nummers na de laatste regel ongewijzigd. Lege regel = lege tag",
		L"· Utwory po ostatnim wierszu bez zmian. Pusty wiersz = pusty tag", L"· Son satirdan sonraki parcaclar degismez. Bos satir = bos etiket")); y += lh + 4;
	muted(L, y, LL14(
		L"右クリック …… コピー／貼り付け。右側は「左からコピー」サブメニューあり。",
		L"Right-click …… copy/paste. Right side has Copy from left submenu.",
		L"Clic droit …… copier/coller. A droite: sous-menu Copier depuis la gauche.",
		L"Tasto destro …… copia/incolla. A destra: sottomenu Copia da sinistra.",
		L"Clic derecho …… copiar/pegar. A la derecha: submenú Copiar desde la izquierda.",
		L"우클릭 …… 복사/붙여넣기. 오른쪽은 '왼쪽에서 복사' 하위 메뉴.",
		L"右键 …… 复制/粘贴。右侧有「从左侧复制」子菜单。",
		L"زر أيمن …… نسخ/لصق. اليمين فيه قائمة فرعية للنسخ من اليسار.",
		L"ПКМ …… копировать/вставить. Справа подменю «скопировать слева».",
		L"Rechtsklick …… Kopieren/Einfuegen. Rechts: Untermenue Von links kopieren.",
		L"Clique direito …… copiar/colar. Direita: submenu Copiar da esquerda.",
		L"Rechtsklik …… kopieren/plakken. Rechts: submenu Kopieren van links.",
		L"PPM …… kopiuj/wklej. Po prawej podmenu Kopiuj z lewej.",
		L"Sag tik …… kopyala/yapistir. Sagda Soldan kopyala alt menusu."));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

void CTagBatchEditDlg::ShowHelpSheet()
{
	if (g_tbHelpDlg && ::IsWindow(g_tbHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_tbHelpDlg, this);
		return;
	}
	if (g_tbHelpDlg && !::IsWindow(g_tbHelpDlg->GetSafeHwnd()))
		g_tbHelpDlg = nullptr;
	CTbHelpDlg* dlg = new CTbHelpDlg(this);
	if (!dlg->Create(IDD_TB_HELP, this)) {
		delete dlg;
		return;
	}
	g_tbHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CTagBatchEditDlg::OnBnClickedHelp()
{
	ShowHelpSheet();
}

void CTagBatchEditDlg::OnDestroy()
{
	if (g_tbHelpDlg && ::IsWindow(g_tbHelpDlg->GetSafeHwnd()))
		g_tbHelpDlg->DestroyWindow();
	PersistPos();
	CCustomBlurDialogBase::OnDestroy();
}
