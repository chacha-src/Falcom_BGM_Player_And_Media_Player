// Kpilist.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Kpilist.h"
#include <algorithm>

// 先に使用するファイルスコープ関数の前方宣言
static void KpiPersistSavedata();

namespace {

class CKpiHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_KPI_HELP };
	explicit CKpiHelpDlg(CWnd* pParent = nullptr)
		: CDialog(IDD, pParent) {}
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

static CKpiHelpDlg* g_kpiHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CKpiHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CKpiHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"KPI一覧操作ガイド", L"KPI List Guide", L"Guide liste KPI", L"Guida elenco KPI",
		L"Guía lista KPI", L"KPI 목록 가이드", L"KPI 列表指南", L"دليل قائمة KPI",
		L"Руководство списка KPI", L"KPI-Listen-Anleitung", L"Guia lista KPI", L"KPI-lijst gids",
		L"Przewodnik listy KPI", L"KPI listesi kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CKpiHelpDlg::OnOK() { DestroyWindow(); }
void CKpiHelpDlg::OnCancel() { DestroyWindow(); }
void CKpiHelpDlg::OnClose() { DestroyWindow(); }

void CKpiHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_kpiHelpDlg == this)
		g_kpiHelpDlg = nullptr;
	delete this;
}

BOOL CKpiHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CKpiHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	const int footerH = hp.footerH;
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
	title(L, y, LL14(L"KPI一覧操作ガイド", L"KPI List — Guide", L"Guide liste KPI", L"Guida elenco KPI",
		L"Guía lista KPI", L"KPI 목록 가이드", L"KPI 列表指南", L"دليل قائمة KPI",
		L"Руководство списка KPI", L"KPI-Listen-Guide", L"Guia lista KPI", L"KPI-lijst gids",
		L"Przewodnik KPI", L"KPI listesi kılavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"プラグインの有効/無効と優先順を管理します。上から順に適用されます。",
		L"Manage plugin enable/order. Applied top-first.",
		L"Gérer activation/ordre. Appliqué du haut vers le bas.",
		L"Gestisci attivazione/ordine. Applicato dall'alto.",
		L"Gestionar activación/orden. Se aplica de arriba abajo.",
		L"플러그인 사용/우선순위를 관리합니다. 위에서부터 적용됩니다.",
		L"管理插件启用与优先顺序。从上到下依次应用。",
		L"إدارة التفعيل/الترتيب. يُطبَّق من الأعلى.",
		L"Управление включением/порядком. Сверху вниз.",
		L"Plugins aktivieren/ordnen. Oben zuerst.",
		L"Gerenciar ativação/ordem. Aplicado de cima.",
		L"Beheer activatie/volgorde. Boven eerst.",
		L"Zarządzaj włączeniem/kolejnością. Od góry.",
		L"Eklenti açık/sıra yönet. Üstten uygulanır."));
	y += lh + 4;

	title(L, y, LL14(L"一覧と優先順", L"List & priority", L"Liste et priorité", L"Elenco e priorità",
		L"Lista y prioridad", L"목록과 우선순위", L"列表与优先顺序", L"القائمة والأولوية",
		L"Список и приоритет", L"Liste & Priorität", L"Lista e prioridade", L"Lijst & prioriteit",
		L"Lista i priorytet", L"Liste ve öncelik"));
	y += titleLh;
	body(L, y, LL14(
		L"・チェック …… 有効な KPI のみ再生時に使われます",
		L"· Check …… only enabled KPIs are used for playback",
		L"· Case …… seuls les KPI cochés sont utilisés",
		L"· Spunta …… solo i KPI abilitati vengono usati",
		L"· Marca …… solo se usan KPI habilitados",
		L"· 체크 …… 사용 중인 KPI만 재생에 사용됩니다",
		L"· 勾选 …… 仅启用的 KPI 用于播放",
		L"· تحديد …… يُستخدم فقط KPI المفعّل",
		L"· Галочка …… используются только включённые KPI",
		L"· Haken …… nur aktivierte KPI werden genutzt",
		L"· Marcar …… só KPIs ativos são usados",
		L"· Vink …… alleen actieve KPI’s worden gebruikt",
		L"· Zaznacz …… używane tylko włączone KPI",
		L"· İşaret …… yalnızca açık KPI kullanılır")); y += lh;
	body(L, y, LL14(
		L"・並び …… 上ほど優先。同じ拡張子は上のプラグインが先に試されます",
		L"· Order …… top has priority. Same ext: top plugin tried first",
		L"· Ordre …… haut = priorité. Même ext: haut d'abord",
		L"· Ordine …… alto = priorità. Stessa est: alto prima",
		L"· Orden …… arriba = prioridad. Misma ext: arriba primero",
		L"· 순서 …… 위가 우선. 같은 확장자는 위 플러그인이 먼저",
		L"· 顺序 …… 越靠上越优先。相同扩展名先试上方插件",
		L"· ترتيب …… الأعلى أولوية. نفس الامتداد: الأعلى أولاً",
		L"· Порядок …… верх приоритетнее. То же расш. — сверху",
		L"· Reihenfolge …… oben zuerst. Gleiche Ext: oben zuerst",
		L"· Ordem …… topo tem prioridade. Mesma ext: topo primeiro",
		L"· Volgorde …… boven heeft voorrang. Zelfde ext: boven eerst",
		L"· Kolejność …… góra ma priorytet. To samo rozsz.: góra",
		L"· Sıra …… üst öncelikli. Aynı uzantı: üst önce")); y += lh;
	body(L, y, LL14(
		L"・列 …… kpi名 / Ver / Arch / 対応拡張子。行にマウスで詳細ツールチップ",
		L"· Columns …… name / Ver / Arch / extensions. Hover row for tip",
		L"· Colonnes …… nom / Ver / Arch / extensions. Survol = tip",
		L"· Colonne …… nome / Ver / Arch / estensioni. Passa per tip",
		L"· Columnas …… nombre / Ver / Arch / extensiones. Tip al pasar",
		L"· 열 …… 이름 / Ver / Arch / 확장자. 행에 마우스면 상세 팁",
		L"· 列 …… 名称 / Ver / Arch / 扩展名。悬停行显示详情",
		L"· أعمدة …… الاسم / Ver / Arch / الامتدادات. مرّر للتلميح",
		L"· Столбцы …… имя / Ver / Arch / расширения. Наведите для подсказки",
		L"· Spalten …… Name / Ver / Arch / Erweiterungen. Hover = Tip",
		L"· Colunas …… nome / Ver / Arch / extensões. Passe para tip",
		L"· Kolommen …… naam / Ver / Arch / extensies. Hover = tip",
		L"· Kolumny …… nazwa / Ver / Arch / rozszerzenia. Tip po najechaniu",
		L"· Sütun …… ad / Ver / Arch / uzantılar. Satıra gelince ipucu")); y += lh + 4;

	title(L, y, LL14(L"拡張子フィルタ", L"Extension filter", L"Filtre d'extension", L"Filtro estensione",
		L"Filtro de extensión", L"확장자 필터", L"扩展名筛选", L"تصفية الامتداد",
		L"Фильтр расширения", L"Erweiterungsfilter", L"Filtro de extensão", L"Extensiefilter",
		L"Filtr rozszerzenia", L"Uzantı filtresi"));
	y += titleLh;
	body(L, y, LL14(
		L"・入力すると即座に一覧を絞り込みます。空欄で全表示",
		L"· Typing filters the list immediately. Empty shows all",
		L"· La saisie filtre aussitôt. Vide = tout",
		L"· Digitando filtra subito. Vuoto = tutto",
		L"· Al escribir filtra al instante. Vacío = todo",
		L"· 입력하면 즉시 목록을 좁힙니다. 비우면 전체",
		L"· 输入即筛选列表。空则显示全部",
		L"· الكتابة تصفّي فوراً. فارغ = الكل",
		L"· Ввод сразу фильтрует. Пусто = всё",
		L"· Eingabe filtert sofort. Leer = alle",
		L"· Digitar filtra na hora. Vazio = todos",
		L"· Typen filtert meteen. Leeg = alles",
		L"· Wpisywanie filtruje od razu. Puste = wszystkie",
		L"· Yazınca anında süzülür. Boş = tümü")); y += lh;
	body(L, y, LL14(
		L"・先頭の . の有無は問いません。複数は空白や , / で区切れます",
		L"· Leading dot optional. Separate multiple with space, comma or /",
		L"· Point initial optionnel. Séparez par espace, virgule ou /",
		L"· Punto iniziale opzionale. Separare con spazio, virgola o /",
		L"· Punto inicial opcional. Separe con espacio, coma o /",
		L"· 앞의 . 유무는 무관. 여러 개는 공백·,·/ 로 구분",
		L"· 前导点可选。多个用空格、逗号或 / 分隔",
		L"· النقطة الأولى اختيارية. افصل بمسافة أو , أو /",
		L"· Точка в начале необязательна. Разделяйте пробелом, запятой или /",
		L"· Führender Punkt optional. Trennen mit Leerzeichen, Komma oder /",
		L"· Ponto inicial opcional. Separe com espaço, vírgula ou /",
		L"· Voorlopende punt optioneel. Scheid met spatie, komma of /",
		L"· Kropka na początku opcjonalna. Oddziel spacją, przecinkiem lub /",
		L"· Baştaki . isteğe bağlı. Birden fazlasını boşluk, , veya / ile ayırın")); y += lh + 4;

	title(L, y, LL14(L"Ver5 について", L"About Ver5", L"À propos de Ver5", L"Informazioni su Ver5",
		L"Acerca de Ver5", L"Ver5 정보", L"关于 Ver5", L"حول Ver5",
		L"О Ver5", L"Zu Ver5", L"Sobre Ver5", L"Over Ver5",
		L"O Ver5", L"Ver5 hakkında"));
	y += titleLh;
	body(L, y, LL14(
		L"・Ver5 プラグインは一部機能が未対応です（説明文にも記載）",
		L"· Ver5 plugins have some unsupported features (also noted in the caption)",
		L"· Les plugins Ver5 ont des fonctions non prises en charge",
		L"· I plugin Ver5 hanno funzioni non supportate",
		L"· Los plugins Ver5 tienen funciones no admitidas",
		L"· Ver5 플러그인은 일부 기능이 미지원입니다(설명에도 표시)",
		L"· Ver5 插件部分功能未支持（说明文字中也有标注）",
		L"· إضافات Ver5 لها ميزات غير مدعومة",
		L"· У плагинов Ver5 часть функций не поддерживается",
		L"· Ver5-Plugins haben teilweise ununterstützte Funktionen",
		L"· Plugins Ver5 têm recursos não suportados",
		L"· Ver5-plugins hebben deels niet-ondersteunde functies",
		L"· Wtyczki Ver5 mają częściowo nieobsługiwane funkcje",
		L"· Ver5 eklentilerinde bazı özellikler desteklenmez")); y += lh + 4;

	title(L, y, LL14(L"OK", L"OK", L"OK", L"OK", L"OK", L"OK", L"确定", L"موافق",
		L"ОК", L"OK", L"OK", L"OK", L"OK", L"Tamam"));
	y += titleLh;
	body(L, y, LL14(
		L"・チェック状態を保存して閉じます。ウィンドウサイズも記憶されます",
		L"· Saves check state and closes. Window size is also remembered",
		L"· Enregistre les cases et ferme. La taille est mémorisée",
		L"· Salva le spunte e chiude. Anche la dimensione viene ricordata",
		L"· Guarda las marcas y cierra. También se recuerda el tamaño",
		L"· 체크 상태를 저장하고 닫습니다. 창 크기도 기억됩니다",
		L"· 保存勾选状态并关闭。窗口大小也会被记住",
		L"· يحفظ التحديد ويغلق. يُحفظ حجم النافذة أيضاً",
		L"· Сохраняет галочки и закрывает. Размер окна тоже запоминается",
		L"· Speichert Haken und schließt. Fenstergröße wird gemerkt",
		L"· Salva as marcas e fecha. O tamanho também é lembrado",
		L"· Slaat vinkjes op en sluit. Venstergrootte wordt onthouden",
		L"· Zapisuje zaznaczenia i zamyka. Rozmiar okna też jest zapamiętany",
		L"· İşaretleri kaydedip kapatır. Pencere boyutu da hatırlanır"));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

// CKpilist ダイアログ

IMPLEMENT_DYNAMIC(CKpilist, CCustomBlurDialogBase)

CKpilist::CKpilist(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CKpilist::IDD, pParent)
{

}

CKpilist::~CKpilist()
{
}

void CKpilist::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_lc);
	DDX_Control(pDX, IDOK, m_okdummy);
	DDX_Control(pDX, IDC_STATIC, m_desc);
	DDX_Control(pDX, IDC_KPI_EXTFILTER, m_extFilter);
	DDX_Control(pDX, IDC_KPI_EXTFILTER_L, m_extFilterLbl);
	DDX_Control(pDX, IDC_KPI_HELP, m_help);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CKpilist, CCustomBlurDialogBase)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST1, &CKpilist::OnLvnItemchangedList1)
	ON_EN_CHANGE(IDC_KPI_EXTFILTER, &CKpilist::OnEnChangeExtFilter)
	ON_BN_CLICKED(IDOK, &CKpilist::OnBnClickedOk)
	ON_BN_CLICKED(IDC_KPI_HELP, &CKpilist::OnBnClickedHelp)
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_DESTROY()
cmn(CKpilist);

// CKpilist メッセージ ハンドラ
extern CString ext[150][300];
extern int kpicnt;
extern CString kpif[400];
extern TCHAR kpifs[200][64];
extern BOOL kpichk[200];
extern BYTE kpiarch[150];
extern BYTE kvar[150][300];
extern TCHAR karento2[1024];

IMPLEMENT_DYNAMIC(CKpiListCtrl, CCustomListCtrl)

BEGIN_MESSAGE_MAP(CKpiListCtrl, CCustomListCtrl)
END_MESSAGE_MAP()

void CKpiListCtrl::BuildToolTipText(int row, int col, CString& out)
{
	UNREFERENCED_PARAMETER(col);
	out.Empty();
	if (row < 0 || row >= GetItemCount())
		return;
	// 表示行 ≠ KPI 配列 index(拡張子フィルタ時)。ItemData が実 index。
	const int kpiIdx = (int)GetItemData(row);
	if (kpiIdx < 0 || kpiIdx >= kpicnt)
		return;

	CString exts;
	for (int i = 0; ; ++i)
	{
		if (ext[kpiIdx][i].IsEmpty())
			break;
		if (!exts.IsEmpty())
			exts += L'/';
		exts += ext[kpiIdx][i];
	}

	CString ver;
	ver.Format(L"%u", (unsigned)kvar[kpiIdx][0]);
	CString arch = L"?";
	if (kpiarch[kpiIdx] == 32)
		arch = L"x86";
	else if (kpiarch[kpiIdx] == 64)
		arch = L"x64";

	out.Format(LL14(
		L"パス：%s\nバージョン：%s\nCPU：%s\n拡張子：%s",
		L"Path: %s\nVersion: %s\nCPU: %s\nExtensions: %s",
		L"Chemin : %s\nVersion : %s\nCPU : %s\nExtensions : %s",
		L"Percorso: %s\nVersione: %s\nCPU: %s\nEstensioni: %s",
		L"Ruta: %s\nVersión: %s\nCPU: %s\nExtensiones: %s",
		L"경로: %s\n버전: %s\nCPU: %s\n확장자: %s",
		L"路径：%s\n版本：%s\nCPU：%s\n扩展名：%s",
		L"المسار: %s\nالإصدار: %s\nالمعالج: %s\nالامتدادات: %s",
		L"Путь: %s\nВерсия: %s\nЦП: %s\nРасширения: %s",
		L"Pfad: %s\nVersion: %s\nCPU: %s\nErweiterungen: %s",
		L"Caminho: %s\nVersão: %s\nCPU: %s\nExtensões: %s",
		L"Pad: %s\nVersie: %s\nCPU: %s\nExtensies: %s",
		L"Ścieżka: %s\nWersja: %s\nCPU: %s\nRozszerzenia: %s",
		L"Yol: %s\nSürüm: %s\nCPU: %s\nUzantılar: %s"),
		(LPCTSTR)kpif[kpiIdx], (LPCTSTR)ver, (LPCTSTR)arch, (LPCTSTR)exts);
}

BOOL CKpilist::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"kpi一覧", L"kpi list", L"Liste kpi", L"Elenco kpi", L"Lista kpi", L"kpi 목록", L"kpi 列表", L"قائمة kpi", L"Список kpi", L"kpi-Liste", L"Lista kpi", L"kpi-lijst", L"Lista kpi", L"kpi listesi"));
	if (m_extFilterLbl.GetSafeHwnd())
		m_extFilterLbl.SetWindowText(LL14(L"拡張子", L"Ext", L"Ext", L"Est", L"Ext", L"확장자", L"扩展名", L"امتداد", L"Расш", L"Erw", L"Ext", L"Ext", L"Rozsz", L"Uzantı"));
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();
	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	m_tooltip.AddTool(&m_okdummy, LL14(L"閉じます", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
	if (m_extFilter.GetSafeHwnd()) {
		m_tooltip.AddTool(&m_extFilter, LL14(
			L"拡張子で一覧を絞り込みます。入力すると即座に反映。空欄で全表示。. の有無は問いません。複数は空白や , / で区切れます。",
			L"Filter the list by extension. Updates as you type. Empty shows all. Dot optional. Separate multiple with space, comma or /.",
			L"Filtrer par extension. Mise a jour pendant la saisie.",
			L"Filtra per estensione. Aggiorna mentre digiti.",
			L"Filtrar por extension. Se actualiza al escribir.",
			L"확장자로 목록을 필터. 입력 즉시 반영. 비우면 전체.",
			L"按扩展名筛选列表。输入即更新。空则显示全部。",
			L"تصفية حسب الامتداد. يتحدث أثناء الكتابة.",
			L"Фильтр по расширению. Обновляется при вводе.",
			L"Nach Erweiterung filtern. Sofort bei Eingabe.",
			L"Filtrar por extensao. Atualiza ao digitar.",
			L"Filter op extensie. Direct bij typen.",
			L"Filtruj po rozszerzeniu. Od razu przy wpisywaniu.",
			L"Uzantiya gore filtrele. Yazarken aninda guncellenir."));
	}
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);

	Init();
	if (m_lc.GetSafeHwnd())
	{
		m_lc.EnableToolTips(TRUE);
		DWORD ex = m_lc.GetExtendedStyle();
		ex |= LVS_EX_INFOTIP;
		m_lc.SetExtendedStyle(ex);
	}

	// サイジング枠(WS_THICKFRAME)はダイアログテンプレート側で付与済み。

	// これ以上小さくできない最小サイズ = テンプレートの初期サイズ
	CRect wr;
	GetWindowRect(&wr);
	m_minW = wr.Width();
	m_minH = wr.Height();

	// 保存済みのサイズ・位置を復元(未保存 kpiWndW==0 のときは既定位置のまま)
	RestoreSavedPlacement();

	// 初期レイアウト(kpi/拡張子 列を自動フィット。kpi 初期幅もここで拡げる)
	LayoutControls();
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();

	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}

// リサイズ時に子コントロール(説明/フィルタ/リスト/OK)を再配置する。
// 位置・サイズはダイアログテンプレート(DLU)の比率を MapDialogRect で px 換算して再現。
void CKpilist::LayoutControls()
{
	if (!m_lc.GetSafeHwnd()) return;

	// 1 DLU あたりの px を求める(フォント依存・DPI 依存を吸収)
	CRect base(0, 0, 4, 8);
	MapDialogRect(&base);
	const double dx = base.Width() / 4.0;
	const double dy = base.Height() / 8.0;
	auto PX = [](double v) { return (int)(v + 0.5); };

	CRect rc;
	GetClientRect(&rc);
	const int cx = rc.Width();
	const int cy = rc.Height();
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);

	const int mx = PX(7 * dx);              // 左右マージン
	const int bw = PX(50 * dx);             // OK ボタン幅
	const int bh = PX(14 * dy);             // OK ボタン高さ
	const int descTop = PX(11 * dy) + capH;
	const int descH = PX(9 * dy);
	const int filtTop = PX(24 * dy) + capH;
	const int filtH = PX(14 * dy);
	const int filtLblW = PX(36 * dx);
	const int listTop = PX(43 * dy) + capH;

	// OK ボタン: 下端中央
	const int by = cy - PX(7 * dy) - bh;
	if (m_okdummy.GetSafeHwnd())
		m_okdummy.MoveWindow((cx - bw) / 2, by, bw, bh);

	// 説明テキスト: 上部いっぱい
	if (m_desc.GetSafeHwnd())
		m_desc.MoveWindow(mx, descTop, (std::max)(0, cx - 2 * mx), descH);

	// 拡張子フィルタ: 説明の下
	const int filtEditX = mx + filtLblW + PX(3 * dx);
	const int filtEditW = (std::max)(0, cx - filtEditX - mx);
	if (m_extFilterLbl.GetSafeHwnd())
		m_extFilterLbl.MoveWindow(mx, filtTop + PX(2 * dy), filtLblW, PX(10 * dy));
	if (m_extFilter.GetSafeHwnd())
		m_extFilter.MoveWindow(filtEditX, filtTop, filtEditW, filtH);

	// リスト: フィルタの下〜ボタンの上
	const int listBottom = by - PX(6 * dy);
	const int listH = (std::max)(0, listBottom - listTop);
	m_lc.MoveWindow(mx, listTop, (std::max)(0, cx - 2 * mx), listH);

	LayoutKpiColumns();
}

// kpi と 拡張子 の列幅を、Ver/Arch を固定したうえで残り幅へ自動フィットさせる。
// 拡張子 列は右端までフィット(kpi と 2 分割し、リサイズで両方が拡縮する)。
void CKpilist::LayoutKpiColumns()
{
	if (!m_lc.GetSafeHwnd()) return;
	if (m_lc.GetHeaderCtrl() == NULL || m_lc.GetHeaderCtrl()->GetItemCount() < 4) return;

	CRect rc;
	m_lc.GetClientRect(&rc);   // 縦スクロールバー分は除かれる
	const int total = rc.Width();

	const int verW = 50;
	const int archW = 60;
	int avail = total - verW - archW;
	if (avail < 160) avail = 160;   // 最低限の割り当て

	int kpiW = (int)(avail * 0.45);
	const int minKpi = 90;
	if (kpiW < minKpi) kpiW = minKpi;
	int extW = avail - kpiW;        // 残り全部 → 右端までフィット
	const int minExt = 90;
	if (extW < minExt) extW = minExt;

	m_lc.SetColumnWidth(0, kpiW);
	m_lc.SetColumnWidth(1, verW);
	m_lc.SetColumnWidth(2, archW);
	m_lc.SetColumnWidth(3, extW);
}

void CKpilist::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		LayoutControls();
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

void CKpilist::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	if (m_minW > 0 && m_minH > 0)
	{
		lpMMI->ptMinTrackSize.x = m_minW;
		lpMMI->ptMinTrackSize.y = m_minH;
	}
	CCustomBlurDialogBase::OnGetMinMaxInfo(lpMMI);
}

// savedata に記録したサイズ・位置を復元する。
// 未保存(kpiWndW==0)なら Init() の既定位置のまま。最小サイズ以上・画面内に補正する。
void CKpilist::RestoreSavedPlacement()
{
	if (savedata.kpiWndW <= 0 || savedata.kpiWndH <= 0) return;

	CRect want(savedata.kpiWndX, savedata.kpiWndY,
		savedata.kpiWndX + savedata.kpiWndW,
		savedata.kpiWndY + savedata.kpiWndH);

	// 最小サイズを下回らないよう補正
	if (want.Width()  < m_minW) want.right  = want.left + m_minW;
	if (want.Height() < m_minH) want.bottom = want.top  + m_minH;

	// 対象モニタの作業領域内に収まるよう補正(画面外復元を防ぐ)
	HMONITOR hMon = ::MonitorFromRect(&want, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi; mi.cbSize = sizeof(mi);
	if (hMon && ::GetMonitorInfo(hMon, &mi))
	{
		const CRect wa(mi.rcWork);
		int w = (std::min)(want.Width(), (int)wa.Width());
		int h = (std::min)(want.Height(), (int)wa.Height());
		int x = want.left, y = want.top;
		if (x < wa.left)            x = wa.left;
		if (y < wa.top)             y = wa.top;
		if (x + w > wa.right)       x = wa.right - w;
		if (y + h > wa.bottom)      y = wa.bottom - h;
		want.SetRect(x, y, x + w, y + h);
	}

	MoveWindow(&want);
}

// 現在のサイズ・位置を savedata に記録する(最大化/最小化中は記録しない)。
void CKpilist::SaveSavedPlacement()
{
	if (!GetSafeHwnd() || !::IsWindow(m_hWnd)) return;
	if (IsIconic() || IsZoomed()) return;   // 通常状態の矩形のみ保存

	CRect wr;
	GetWindowRect(&wr);
	if (wr.Width() <= 0 || wr.Height() <= 0) return;

	savedata.kpiWndX = wr.left;
	savedata.kpiWndY = wr.top;
	savedata.kpiWndW = wr.Width();
	savedata.kpiWndH = wr.Height();
}

void CKpilist::OnDestroy()
{
	// OK / × / Esc いずれの閉じ方でもサイズ・位置を保存して永続化する
	if (status == 0)
	{
		SaveSavedPlacement();
		KpiPersistSavedata();
	}
	if (g_kpiHelpDlg && ::IsWindow(g_kpiHelpDlg->GetSafeHwnd()))
		g_kpiHelpDlg->DestroyWindow();
	CCustomBlurDialogBase::OnDestroy();
}

void CKpilist::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CKpilist::ShowHelpSheet()
{
	if (g_kpiHelpDlg && ::IsWindow(g_kpiHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_kpiHelpDlg, this);
		return;
	}
	if (g_kpiHelpDlg && !::IsWindow(g_kpiHelpDlg->GetSafeHwnd()))
		g_kpiHelpDlg = nullptr;
	CKpiHelpDlg* dlg = new CKpiHelpDlg(this);
	if (!dlg->Create(IDD_KPI_HELP, this)) {
		delete dlg;
		return;
	}
	g_kpiHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CKpilist::OnBnClickedHelp()
{
	ShowHelpSheet();
}

BOOL kpichks[300];

// kpif[i] のフルパスからファイル名(ベース名)だけを取り出す
static CString KpiBaseName(const CString& path)
{
	return path.Right(path.GetLength() - path.ReverseFind('\\') - 1);
}

// 拡張子トークンの先頭 '.' を除いた比較用キー
static CString KpiExtKey(CString e)
{
	e.Trim();
	e.MakeLower();
	if (!e.IsEmpty() && e[0] == _T('.'))
		e = e.Mid(1);
	return e;
}

// フィルタ文字列に合う KPI か。空欄=全表示。空白/,/; 区切りは OR。
static bool KpiExtMatchesFilter(int kpiIdx, const CString& filterRaw)
{
	CString raw = filterRaw;
	raw.Trim();
	if (raw.IsEmpty())
		return true;

	raw.MakeLower();
	raw.Replace(_T(','), _T(' '));
	raw.Replace(_T(';'), _T(' '));
	raw.Replace(_T('/'), _T(' '));

	CStringArray tokens;
	for (int p = 0; p < raw.GetLength(); ) {
		while (p < raw.GetLength() && raw[p] == _T(' ')) ++p;
		if (p >= raw.GetLength()) break;
		const int start = p;
		while (p < raw.GetLength() && raw[p] != _T(' ')) ++p;
		CString t = KpiExtKey(raw.Mid(start, p - start));
		if (!t.IsEmpty())
			tokens.Add(t);
	}
	if (tokens.GetCount() == 0)
		return true;

	for (INT_PTR t = 0; t < tokens.GetCount(); ++t) {
		const CString& want = tokens[t];
		for (int i = 0; ; ++i) {
			if (ext[kpiIdx][i].IsEmpty())
				break;
			const CString have = KpiExtKey(ext[kpiIdx][i]);
			if (have.IsEmpty())
				continue;
			// 部分一致(例: "sp" → spc)。先頭一致も許容。
			if (have.Find(want) >= 0 || want.Find(have) >= 0)
				return true;
		}
	}
	return false;
}

// 旧 kpilist.dat は廃止。残っていれば savedata へ一度だけ移植し、そのうえで削除する。
// savedata 側に既にエントリがあれば移植済みとみなし、残骸ファイルがあれば掃除する。
static void KpiMigrateLegacyToSavedata()
{
	CString ss = karento2;
	ss += _T("kpilist.dat");

	// savedata 未保存(=未移植)かつ旧ファイルがある場合のみ内容を取り込む
	if (savedata.kpiChkCnt == 0) {
		CFile ff;
		if (ff.Open(ss, CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
			try {
				int cnt = 0;
				if (ff.Read(&cnt, sizeof(cnt)) == sizeof(cnt)) {
					if (cnt < 0) cnt = 0;
					if (cnt > 200) cnt = 200;
					int stored = 0;
					for (int i = 0; i < cnt; i++) {
						BOOL chk = TRUE;
						TCHAR nm[64] = { 0 };
						if (ff.Read(&chk, sizeof(BOOL)) != sizeof(BOOL)) break;
						if (ff.Read(nm, 64 * sizeof(TCHAR)) != 64 * sizeof(TCHAR)) break;
						nm[63] = 0;
						savedata.kpiChkState[stored] = chk ? 1 : 0;
						_tcsncpy(savedata.kpiChkName[stored], nm, 63);
						savedata.kpiChkName[stored][63] = 0;
						stored++;
					}
					savedata.kpiChkCnt = stored;
				}
			}
			catch (...) {
			}
			ff.Close();
		}
	}

	// 廃止済みのため、残っている kpilist.dat は削除する(移植は savedata へ完了)
	::DeleteFile(ss);
}

// 現在の savedata(=最新)を .dat へ書き出し、チェック変更を即座に永続化する。
static void KpiPersistSavedata()
{
	CString ss = karento2;
#if _UNICODE
	ss += L"oggYSEDbgmu.dat";
#else
	ss += "oggYSEDbgm.dat";
#endif
	CFile ab;
	if (ab.Open(ss, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
		ab.Write(&savedata, sizeof(save));
		ab.Close();
	}
}

// 表示中行のチェック状態を kpichk[実KPI index] へ吸い上げる。
// フィルタ再構築で行が消える前に呼ぶこと(非表示行のチェックを壊さない)。
void CKpilist::SyncChecksFromList()
{
	if (!m_lc.GetSafeHwnd()) return;
	const int n = m_lc.GetItemCount();
	for (int row = 0; row < n; ++row) {
		const int idx = (int)m_lc.GetItemData(row);
		if (idx < 0 || idx >= kpicnt || idx >= 200) continue;
		kpichk[idx] = m_lc.GetCheck(row) ? TRUE : FALSE;
	}
}

void CKpilist::FillKpiList()
{
	if (!m_lc.GetSafeHwnd()) return;

	// 再構築前に表示中のチェックを本体配列へ退避
	SyncChecksFromList();

	CString filter;
	if (m_extFilter.GetSafeHwnd())
		m_extFilter.GetWindowText(filter);

	m_bFillingList = TRUE;
	m_lc.SetRedraw(FALSE);
	m_lc.DeleteAllItems();

	TCHAR* buf = (TCHAR*)calloc(10000, sizeof(TCHAR));
	if (!buf) {
		m_lc.SetRedraw(TRUE);
		m_bFillingList = FALSE;
		return;
	}

	LV_ITEM LvItem;
	ZeroMemory(&LvItem, sizeof(LvItem));

	for (int j = 0; j < kpicnt; j++) {
		if (!KpiExtMatchesFilter(j, filter))
			continue;
		try {
			CString s;
			for (int i = 0; ; i++) {
				if (ext[j][i] == "") break;
				s += ext[j][i]; s += "/";
			}
			if (!s.IsEmpty())
				s = s.Left(s.GetLength() - 1);

			const int a2 = kpif[j].ReverseFind(L'\\');
			_tcscpy(buf, kpif[j].Right(kpif[j].GetLength() - a2 - 1));
			LvItem.pszText = buf;
			LvItem.iItem = m_lc.GetItemCount();
			LvItem.mask = LVIF_TEXT | LVIF_PARAM;
			LvItem.iSubItem = 0;
			LvItem.lParam = j; // 実 KPI index(チェック保存の正)
			LvItem.cchTextMax = (int)_tcslen(LvItem.pszText);
			const int idItem = m_lc.InsertItem(&LvItem);
			if (idItem < 0) continue;
			m_lc.SetItemData(idItem, (DWORD_PTR)j);

			LvItem.mask = LVIF_TEXT;
			LvItem.iItem = idItem;
			CString sss; sss.Format(L"%d", kvar[j][0]);
			_tcscpy(buf, sss);
			LvItem.iSubItem = 1;
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);

			CString arch = L"?";
			if (kpiarch[j] == 32) arch = L"x86";
			else if (kpiarch[j] == 64) arch = L"x64";
			_tcscpy(buf, arch);
			LvItem.iSubItem = 2;
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);

			_tcscpy(buf, s);
			LvItem.iSubItem = 3;
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);

			// チェックは常に kpichk[実index] から(行番号ではない)
			m_lc.SetCheck(idItem, kpichk[j] ? TRUE : FALSE);
		}
		catch (...) {
			break;
		}
	}
	free(buf);
	m_lc.SetRedraw(TRUE);
	m_lc.Invalidate(FALSE);
	m_bFillingList = FALSE;
	LayoutKpiColumns();
}

void CKpilist::Init()
{
	// 保存済みチェック状態を savedata から取得(初回は旧 kpilist.dat から移行)
	KpiMigrateLegacyToSavedata();
	int cnt = savedata.kpiChkCnt;
	if (cnt > 200) cnt = 200;

	// 現在のプラグイン名 → 保存済み状態を突き合わせて runtime の kpichk[] を確定。
	// 未保存の新規プラグインは既定で使用(チェックON)。並び順に依らずファイル名で復元。
	for (int j = 0; j < kpicnt; j++) {
		BOOL chk = TRUE;
		CString name = KpiBaseName(kpif[j]);
		for (int i = 0; i < cnt; i++) {
			if (name.CompareNoCase(savedata.kpiChkName[i]) == 0) {
				chk = savedata.kpiChkState[i] ? TRUE : FALSE;
				break;
			}
		}
		kpichk[j] = chk;
	}

	// status==1(起動時)はウィンドウ未生成のため UI は組まない
	if (status != 0) return;

	DWORD dwExStyle = m_lc.GetExtendedStyle();
	dwExStyle |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_INFOTIP | LVS_EX_CHECKBOXES;
	m_lc.SetExtendedStyle(dwExStyle);
	m_lc.ModifyStyle(0, LVS_REPORT);
	m_lc.InsertColumn(0, LL14(L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi"), LVCFMT_LEFT, 100, 0);
	m_lc.InsertColumn(1, L"Ver", LVCFMT_CENTER, 50, 0);
	m_lc.InsertColumn(2, L"Arch", LVCFMT_CENTER, 60, 0);
	m_lc.InsertColumn(3, LL14(L"拡張子", L"Extensions", L"Extensions", L"Estensioni", L"Extensiones", L"확장자", L"扩展名", L"الامتدادات", L"Расширения", L"Erweiterungen", L"Extensões", L"Extensies", L"Rozszerzenia", L"Uzantılar"), LVCFMT_LEFT, 340, 0);

	FillKpiList();

	RECT r;
	GetWindowRect(&r);
	r.top += 600;
	r.bottom += 600;
	MoveWindow(&r);
}

void CKpilist::Save()
{
	int n = kpicnt;
	if (n > 200) n = 200;

	// ダイアログ表示中は、まず表示中チェックを kpichk へ反映。
	// フィルタで隠れた行は既に Sync/ITEMCHANGED で kpichk に残っている。
	if (status == 0)
		SyncChecksFromList();

	for (int i = 0; i < n; i++) {
		savedata.kpiChkState[i] = kpichk[i] ? 1 : 0;
		CString bn = KpiBaseName(kpif[i]);
		_tcsncpy(savedata.kpiChkName[i], bn, 63);
		savedata.kpiChkName[i][63] = 0;
	}
	savedata.kpiChkCnt = n;

	// チェック変更を即座に .dat へ反映(アプリ終了を待たずに永続化)
	KpiPersistSavedata();
}

void CKpilist::OnLvnItemchangedList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	*pResult = 0;
	if (m_bFillingList || !pNMLV) return;
	// チェックボックス(状態イメージ)の変化だけを kpichk[実index] へ即反映
	if ((pNMLV->uChanged & LVIF_STATE) == 0) return;
	const UINT oldImg = (pNMLV->uOldState & LVIS_STATEIMAGEMASK);
	const UINT newImg = (pNMLV->uNewState & LVIS_STATEIMAGEMASK);
	if (oldImg == newImg) return;
	const int row = pNMLV->iItem;
	if (row < 0) return;
	const int idx = (int)m_lc.GetItemData(row);
	if (idx < 0 || idx >= kpicnt || idx >= 200) return;
	kpichk[idx] = m_lc.GetCheck(row) ? TRUE : FALSE;
}

void CKpilist::OnEnChangeExtFilter()
{
	if (m_bFillingList) return;
	FillKpiList();
}

BOOL CKpilist::PreTranslateMessage(MSG* pMsg)
{
	if (m_lc.GetSafeHwnd() && m_lc.PreTranslateMessage(pMsg))
		return TRUE;
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CKpilist::OnBnClickedOk()
{
	// ダイアログのチェック変更を savedata へ保存してから閉じる
	Save();
	CCustomBlurDialogBase::OnOK();
}

