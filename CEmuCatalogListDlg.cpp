// CEmuCatalogListDlg.cpp — Cemu対応一覧（arcdata.zip）
#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "CEmuCatalogListDlg.h"
#include "CMediaPlayerDlg.h"
#include "PlayList.h"
#include "CEmu/cemu_mgr.h"
#include "CEmu/cemu_catalog.h"
#include "CEmu/cemu_modepref.h"
#include "CEmu/cemu_support.h"
#include "CImageBase.h"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <shlobj.h>
#include <commctrl.h>

#ifndef I_IMAGENONE
#define I_IMAGENONE (-2)
#endif

#pragma comment(lib, "shell32.lib")

extern CPlayList* pl;
extern int gameon;
extern int plcnt;
extern CMediaPlayerDlg* mp;

enum { WM_CEMU_CAT_FILTER = WM_APP + 70 };

static CEmuCatalogListDlg* g_cemuCatList = nullptr;

namespace {

class CEmuCatHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_KPI_HELP };
	explicit CEmuCatHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK() { DestroyWindow(); }
	virtual void OnCancel() { DestroyWindow(); }
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose() { DestroyWindow(); }
	DECLARE_MESSAGE_MAP()
};

static CEmuCatHelpDlg* g_cemuCatHelp = nullptr;

BEGIN_MESSAGE_MAP(CEmuCatHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CEmuCatHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"Cemu対応一覧ガイド", L"Cemu Supported List Guide", L"Guide liste Cemu", L"Guida elenco Cemu",
		L"Guía lista Cemu", L"Cemu 대응 목록 가이드", L"Cemu 对应一览指南", L"دليل قائمة Cemu",
		L"Руководство списка Cemu", L"Cemu-Listen-Anleitung", L"Guia lista Cemu", L"Cemu-lijstgids",
		L"Przewodnik listy Cemu", L"Cemu liste kilavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CEmuCatHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_cemuCatHelp == this) g_cemuCatHelp = nullptr;
	delete this;
}

BOOL CEmuCatHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CEmuCatHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp)) return;
	CDC& dc = hp.mem;
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());
	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	int y = 6;
	const int L = 10;
	dc.SetTextColor(RGB(55, 45, 85));
	dc.TextOut(L, y, LL14(L"Cemu対応一覧", L"Cemu Supported List", L"Liste Cemu", L"Elenco Cemu",
		L"Lista Cemu", L"Cemu 대응 목록", L"Cemu 对应一览", L"قائمة Cemu",
		L"Список Cemu", L"Cemu-Liste", L"Lista Cemu", L"Cemu-lijst", L"Lista Cemu", L"Cemu listesi"));
	y += lh + 2;
	dc.SetTextColor(RGB(65, 65, 80));
	auto body = [&](LPCTSTR t) { dc.TextOut(L, y, t); y += lh; };
	body(LL14(
		L"・ダブルクリック …… 対応 zip をプレイリストへ追加して再生",
		L"· Double-click …… add matching zip to playlist and play",
		L"· Double-clic …… ajouter le zip et lire",
		L"· Doppio clic …… aggiungi zip e riproduci",
		L"· Doble clic …… anadir zip y reproducir",
		L"· 더블클릭 …… 대응 zip을 플레이리스트에 넣고 재생",
		L"· 双击 …… 将对应 zip 加入播放列表并播放",
		L"· نقر مزدوج …… إضافة zip والتشغيل",
		L"· Двойной клик …… добавить zip и играть",
		L"· Doppelklick …… Zip zur Playlist und abspielen",
		L"· Duplo clique …… adicionar zip e tocar",
		L"· Dubbelklik …… zip toevoegen en spelen",
		L"· Dwuklik …… dodaj zip i odtworz",
		L"· Cift tik …… zip ekle ve cal"));
	body(LL14(
		L"・絞り込み …… 空白区切りで複数指定（すべて含む）。全角空白・, / も可",
		L"· Filter …… space-separated terms (AND). Full-width space, comma, / also split",
		L"· Filtre …… mots separes par espace (ET). Espace pleine chasse, virgule, /",
		L"· Filtro …… termini separati da spazio (AND). Spazio pieno, virgola, /",
		L"· Filtro …… terminos separados por espacio (AND). Espacio ancho, coma, /",
		L"· 필터 …… 공백으로 여러 단어(모두 포함). 전각 공백·,/ 도 가능",
		L"· 筛选 …… 空格分隔多项（需全部匹配）。全角空格、逗号、/ 也可",
		L"· تصفية …… افصل بمسافة (يجب أن تطابق كلها)",
		L"· Фильтр …… слова через пробел (И). Полный пробел, запятая, /",
		L"· Filter …… Leerzeichen trennt (UND). Vollbreite Leerzeichen, Komma, /",
		L"· Filtro …… espacos separam (E). Espaco largo, virgula, /",
		L"· Filter …… spaties scheiden (EN). Volledige spatie, komma, /",
		L"· Filtr …… spacje oddzielaja (AND). Pelna spacja, przecinek, /",
		L"· Filtre …… boslukla birden fazla (hepsi). Tam bosluk, virgul, /"));
	body(LL14(
		L"・見出しクリック …… 昇順 → 降順 → 元の順。▲/▼ で今の状態",
		L"· Header click …… Asc → Desc → original. ▲/▼ shows the state",
		L"· Clic en-tete …… Croissant → Decroissant → original. ▲/▼",
		L"· Clic intestazione …… Crescente → Decrescente → originale. ▲/▼",
		L"· Clic encabezado …… Asc → Desc → original. ▲/▼",
		L"· 헤더 클릭 …… 오름차순 → 내림차순 → 원래. ▲/▼",
		L"· 单击列头 …… 升序 → 降序 → 原始。▲/▼ 表示状态",
		L"· نقر الرأس …… تصاعدي → تنازلي → الأصل. ▲/▼",
		L"· Клик по заголовку …… возр. → убыв. → исходный. ▲/▼",
		L"· Kopfklick …… Auf → Ab → Original. ▲/▼",
		L"· Clique no cabecalho …… Asc → Desc → original. ▲/▼",
		L"· Kopklik …… Oplopend → Aflopend → origineel. ▲/▼",
		L"· Klik naglowka …… Rosnaco → Malejaco → oryginal. ▲/▼",
		L"· Baslik tik …… Artan → Azalan → orijinal. ▲/▼"));
	body(LL14(
		L"・ジャンル列 …… Shooter / Action / RPG / ARPG / SRPG / Adventure / Fighting など。絞り込み可（STG, ACT, FTG など旧略称も可）",
		L"· Genre column …… Shooter / Action / RPG / ARPG / SRPG / Adventure / Fighting. Filterable (old STG, ACT, FTG aliases too)",
		L"· Colonne genre …… Shooter / Action / RPG / ARPG / SRPG / Adventure / Fighting. Filtrable (alias STG, ACT, FTG)",
		L"· Colonna genere …… Shooter / Action / RPG / ARPG / SRPG / Adventure / Fighting. Filtrabile (alias STG, ACT, FTG)",
		L"· Columna genero …… Shooter / Action / RPG / ARPG / SRPG / Adventure / Fighting. Filtrable (alias STG, ACT, FTG)",
		L"· 장르 열 …… Shooter / Action / RPG / ARPG / SRPG / Adventure / Fighting. 필터 가능 (STG, ACT, FTG 별칭)",
		L"· 类型列 …… Shooter / Action / RPG / ARPG / SRPG / Adventure / Fighting。可筛选（STG, ACT, FTG 旧简称也可）",
		L"· عمود النوع …… Shooter / Action / RPG / ARPG / SRPG. قابل للتصفية",
		L"· Столбец жанра …… Shooter / Action / RPG / ARPG / SRPG. Фильтр",
		L"· Genre-Spalte …… Shooter / Action / RPG / ARPG / SRPG. Filterbar",
		L"· Coluna genero …… Shooter / Action / RPG / ARPG / SRPG. Filtravel",
		L"· Genrekolom …… Shooter / Action / RPG / ARPG / SRPG. Filterbaar",
		L"· Kolumna gatunku …… Shooter / Action / RPG / ARPG / SRPG. Filtrowalne",
		L"· Tur sutunu …… Shooter / Action / RPG / ARPG / SRPG. Filtrelenebilir"));
	body(LL14(
		L"・モニタ列 …… FM / MIDI / 両方。同じ zip に両モードがあるときは FM+MIDI",
		L"· Monitor column …… FM / MIDI / both when the zip has both modes",
		L"· Colonne moniteur …… FM / MIDI / les deux si le zip a les deux",
		L"· Colonna monitor …… FM / MIDI / entrambi se lo zip ha entrambi",
		L"· Columna monitor …… FM / MIDI / ambos si el zip tiene ambos",
		L"· 모니터 열 …… FM / MIDI / 둘 다. zip에 둘 다 있으면 FM+MIDI",
		L"· 监视器列 …… FM / MIDI / 两者。同一 zip 两种模式时为 FM+MIDI",
		L"· عمود الشاشة …… FM / MIDI / كلاهما إن وُجد الاثنان",
		L"· Столбец монитора …… FM / MIDI / оба, если в zip оба режима",
		L"· Monitor-Spalte …… FM / MIDI / beides, wenn das Zip beides hat",
		L"· Coluna monitor …… FM / MIDI / ambos se o zip tiver os dois",
		L"· Monitorkolom …… FM / MIDI / beide als de zip beide heeft",
		L"· Kolumna monitora …… FM / MIDI / oba gdy zip ma oba",
		L"· Monitor sutunu …… FM / MIDI / ikisi de zipte varsa FM+MIDI"));
	body(LL14(
		L"・音源列 …… OPN/OPNA/MIDI などは同じ zip を1行にまとめ、切替はプレイリストの右クリック",
		L"· Sound column …… OPN/OPNA/MIDI share one row; switch via playlist right-click",
		L"· Colonne son …… OPN/OPNA/MIDI sur une ligne ; changer via le menu de la liste",
		L"· Colonna suono …… OPN/OPNA/MIDI in una riga; cambia dal menu playlist",
		L"· Columna sonido …… OPN/OPNA/MIDI en una fila; cambia en el menu de la lista",
		L"· 음원 열 …… OPN/OPNA/MIDI는 한 줄로 합침. 전환은 플레이리스트 우클릭",
		L"· 音源列 …… OPN/OPNA/MIDI 合并为一行；在播放列表右键切换",
		L"· عمود الصوت …… OPN/OPNA/MIDI في صف واحد؛ التبديل من قائمة التشغيل",
		L"· Столбец звука …… OPN/OPNA/MIDI в одной строке; переключение в плейлисте",
		L"· Sound-Spalte …… OPN/OPNA/MIDI in einer Zeile; Wechsel per Playlist-Menü",
		L"· Coluna som …… OPN/OPNA/MIDI numa linha; mude no menu da playlist",
		L"· Geluidskolom …… OPN/OPNA/MIDI op één regel; wissel via playlist-menu",
		L"· Kolumna dzwieku …… OPN/OPNA/MIDI w jednym wierszu; zmiana w playliscie",
		L"· Ses sutunu …… OPN/OPNA/MIDI tek satir; playlist sag tik ile degistir"));
	body(LL14(
		L"・zip 探索 …… プレイリストに Cemu 曲があればその周辺から検索",
		L"· Zip search …… if a Cemu track is in the playlist, search nearby",
		L"· Recherche zip …… si un titre Cemu est en liste, chercher autour",
		L"· Ricerca zip …… se c'e un brano Cemu, cerca nelle vicinanze",
		L"· Busqueda zip …… si hay Cemu en la lista, buscar cerca",
		L"· zip 탐색 …… 플레이리스트에 Cemu가 있으면 주변에서 검색",
		L"· zip 查找 …… 若列表中已有 Cemu 曲，则在其附近搜索",
		L"· بحث zip …… إن وُجد مسار Cemu ابحث بالقرب",
		L"· Поиск zip …… если в списке есть Cemu — искать рядом",
		L"· Zip-Suche …… bei Cemu in der Liste in der Nähe suchen",
		L"· Busca zip …… se houver Cemu na lista, procurar perto",
		L"· Zip-zoek …… bij Cemu in lijst in de buurt zoeken",
		L"· Szukaj zip …… gdy jest Cemu na liscie, szukaj wokol",
		L"· Zip arama …… listede Cemu varsa yakininda ara"));
	body(LL14(
		L"・無いとき …… フォルダ選択画面で探索ルートを指定",
		L"· Otherwise …… choose a folder as the search root",
		L"· Sinon …… choisir un dossier racine",
		L"· Altrimenti …… scegli una cartella radice",
		L"· Si no …… elige una carpeta raiz",
		L"· 없으면 …… 폴더 선택으로 검색 루트 지정",
		L"· 否则 …… 用文件夹选择指定搜索根目录",
		L"· وإلا …… اختر مجلد جذر البحث",
		L"· Иначе …… выберите корневую папку",
		L"· Sonst …… Ordner als Suchwurzel wählen",
		L"· Senao …… escolha uma pasta raiz",
		L"· Anders …… kies een rootmap",
		L"· Inaczej …… wybierz folder glowny",
		L"· Yoksa …… arama kok klasorunu sec"));
	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

static int CEmuHasExeArcdata()
{
	wchar_t arc[MAX_PATH] = {};
	CEmuCatalogGetExeArcdataPath(arc, MAX_PATH);
	return (arc[0] && GetFileAttributesW(arc) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
}

/* xxx/yyyy/zzzz.zip → xxx を返す */
static int CEmuRootFromZipPath(const wchar_t* zipPath, wchar_t* out, int outCch)
{
	if (!zipPath || !zipPath[0] || !out || outCch <= 0) return 0;
	out[0] = 0;
	wchar_t buf[CEMU_ZIP_PATH];
	wcsncpy_s(buf, zipPath, _TRUNCATE);
	wchar_t* colon = wcsstr(buf, L"::");
	if (colon) *colon = 0;
	wchar_t* slash = wcsrchr(buf, L'\\');
	if (!slash) slash = wcsrchr(buf, L'/');
	if (!slash) return 0;
	*slash = 0; /* yyyy */
	slash = wcsrchr(buf, L'\\');
	if (!slash) slash = wcsrchr(buf, L'/');
	if (slash) {
		*slash = 0; /* xxx */
		wcsncpy_s(out, (size_t)outCch, buf, _TRUNCATE);
	} else {
		wcsncpy_s(out, (size_t)outCch, buf, _TRUNCATE);
	}
	return out[0] != 0;
}

static int CEmuFindZipRecursive(const wchar_t* dir, const wchar_t* fileName,
	wchar_t* out, int outCch, int depth)
{
	if (!dir || !dir[0] || !fileName || !fileName[0] || !out || outCch <= 0 || depth < 0)
		return 0;
	wchar_t cand[MAX_PATH];
	_snwprintf_s(cand, _TRUNCATE, L"%s\\%s", dir, fileName);
	if (GetFileAttributesW(cand) != INVALID_FILE_ATTRIBUTES) {
		wcsncpy_s(out, (size_t)outCch, cand, _TRUNCATE);
		return 1;
	}
	if (depth == 0) return 0;
	wchar_t pattern[MAX_PATH];
	_snwprintf_s(pattern, _TRUNCATE, L"%s\\*", dir);
	WIN32_FIND_DATAW fd = {};
	HANDLE h = FindFirstFileW(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	int found = 0;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0
			|| (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		_snwprintf_s(cand, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
		if (CEmuFindZipRecursive(cand, fileName, out, outCch, depth - 1)) {
			found = 1;
			break;
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return found;
}

static int CEmuBrowseFolder(CWnd* owner, wchar_t* out, int outCch)
{
	if (!out || outCch <= 0) return 0;
	out[0] = 0;
	BROWSEINFOW bi = {};
	bi.hwndOwner = owner ? owner->GetSafeHwnd() : NULL;
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	bi.lpszTitle = LL14(
		L"Cemu 用 zip を探すフォルダを選んでください",
		L"Choose a folder to search for Cemu zips",
		L"Choisir un dossier pour chercher les zip Cemu",
		L"Scegli una cartella per cercare i zip Cemu",
		L"Elige una carpeta para buscar zips Cemu",
		L"Cemu zip을 찾을 폴더를 선택하세요",
		L"请选择用于查找 Cemu zip 的文件夹",
		L"اختر مجلدًا للبحث عن ملفات zip لـ Cemu",
		L"Выберите папку для поиска zip Cemu",
		L"Ordner zum Suchen von Cemu-Zips wählen",
		L"Escolha uma pasta para procurar zips Cemu",
		L"Kies een map om Cemu-zips te zoeken",
		L"Wybierz folder do szukania zipow Cemu",
		L"Cemu zip aramak icin klasor secin");
	LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
	if (!pidl) return 0;
	const BOOL ok = SHGetPathFromIDListW(pidl, out);
	CoTaskMemFree(pidl);
	return (ok && out[0]) ? 1 : 0;
}

static int CEmuResolveZipForArchive(CWnd* owner, const char* archiveStem, wchar_t* out, int outCch)
{
	if (!archiveStem || !archiveStem[0] || !out || outCch <= 0) return 0;
	out[0] = 0;
	wchar_t fileName[CEMU_ARCHIVE_NAME + 8];
	_snwprintf_s(fileName, _TRUNCATE, L"%hs.zip", archiveStem);

	wchar_t root[MAX_PATH] = {};
	if (pl && pl->pc) {
		for (int i = 0; i < pl->playcnt; i++) {
			if (pl->pc[i].sub != MODE_CEMU) continue;
			wchar_t phys[CEMU_ZIP_PATH] = {};
			unsigned ti = 1;
			if (!CEmuParseVirtualPath(pl->pc[i].fol, phys, (int)_countof(phys), &ti))
				wcsncpy_s(phys, pl->pc[i].fol, _TRUNCATE);
			if (CEmuRootFromZipPath(phys, root, MAX_PATH))
				break;
		}
	}
	if (!root[0]) {
		if (!CEmuBrowseFolder(owner, root, MAX_PATH))
			return 0;
	}
	if (CEmuFindZipRecursive(root, fileName, out, outCch, 6))
		return 1;

	CString msg = LL14(
		L"対応する zip が見つかりませんでした。\nファイル名: ",
		L"Matching zip was not found.\nFile name: ",
		L"Zip correspondant introuvable.\nNom: ",
		L"Zip corrispondente non trovato.\nNome: ",
		L"No se encontro el zip.\nNombre: ",
		L"대응 zip을 찾지 못했습니다.\n파일명: ",
		L"未找到对应 zip。\n文件名：",
		L"لم يُعثر على zip.\nالاسم: ",
		L"Соответствующий zip не найден.\nИмя: ",
		L"Passendes Zip nicht gefunden.\nDateiname: ",
		L"Zip correspondente nao encontrado.\nNome: ",
		L"Bijbehorende zip niet gevonden.\nBestandsnaam: ",
		L"Nie znaleziono zip.\nNazwa: ",
		L"Eslesen zip bulunamadi.\nDosya adi: ");
	msg += fileName;
	AfxMessageBox(msg, MB_ICONINFORMATION);
	return 0;
}

/* タイトル末尾の (OPNA)/(OPN)/(GS) などを外す（音源は別列・コンテキストで切替）。 */
static void CEmuStripModeParenFromTitle(CString& name)
{
	for (;;) {
		name.TrimRight();
		const int n = name.GetLength();
		if (n < 3 || name[n - 1] != L')') break;
		const int open = name.ReverseFind(L'(');
		if (open < 0 || open >= n - 1) break;
		CString inside = name.Mid(open + 1, n - open - 2);
		inside.Trim();
		if (inside.IsEmpty() || inside.GetLength() > 16) break;
		inside.MakeUpper();
		bool modeLike = false;
		static const wchar_t* const kTags[] = {
			L"OPNA", L"OPN", L"OPM", L"OPLL", L"OPL", L"OPL2", L"OPL3",
			L"MIDI", L"GS", L"BEEP", L"ADLIB", L"86", L"CMS", L"SB",
			L"SOUNDBLASTER", L"GAMEBLASTER", L"FM", L"PSG", L"SCC",
		};
		for (size_t i = 0; i < _countof(kTags); i++) {
			if (inside == kTags[i]) { modeLike = true; break; }
		}
		if (!modeLike) {
			modeLike = true;
			for (int i = 0; i < inside.GetLength(); i++) {
				const wchar_t c = inside[i];
				if (!((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')
					|| c == L'+' || c == L'-')) {
					modeLike = false;
					break;
				}
			}
		}
		if (!modeLike) break;
		name = name.Left(open);
	}
	name.TrimRight();
}

static void CEmuArchivePrimaryStem(const char* archive, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!archive || !archive[0]) return;
	strncpy_s(out, (size_t)outCap, archive, _TRUNCATE);
	char* comma = strchr(out, ',');
	if (comma) *comma = 0;
	for (char* p = out; *p; p++) {
		if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
	}
}

/* Scan dir + immediate subdirs for *.zip (merge into stems / stemToPath). */
static void CEmuScanLocalZipStemsMerge(const wchar_t* root,
	std::unordered_set<std::string>& stems,
	std::unordered_map<std::string, std::wstring>& stemToPath)
{
	if (!root || !root[0]) return;

	auto addZip = [&](const wchar_t* fullPath, const wchar_t* fileName) {
		if (!fullPath || !fileName) return;
		wchar_t stemW[CEMU_ARCHIVE_NAME];
		wcsncpy_s(stemW, fileName, _TRUNCATE);
		wchar_t* dot = wcsrchr(stemW, L'.');
		if (dot) *dot = 0;
		if (!stemW[0]) return;
		char stemA[CEMU_ARCHIVE_NAME];
		WideCharToMultiByte(CP_UTF8, 0, stemW, -1, stemA, (int)sizeof(stemA), NULL, NULL);
		for (char* p = stemA; *p; p++) {
			if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
		}
		if (!stemA[0]) return;
		stems.insert(stemA);
		if (stemToPath.find(stemA) == stemToPath.end())
			stemToPath.emplace(stemA, fullPath);
	};

	auto scanDir = [&](const wchar_t* dir) {
		wchar_t pattern[MAX_PATH];
		_snwprintf_s(pattern, _TRUNCATE, L"%s\\*.zip", dir);
		WIN32_FIND_DATAW fd = {};
		HANDLE h = FindFirstFileW(pattern, &fd);
		if (h == INVALID_HANDLE_VALUE) return;
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			wchar_t full[MAX_PATH];
			_snwprintf_s(full, _TRUNCATE, L"%s\\%s", dir, fd.cFileName);
			addZip(full, fd.cFileName);
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	};

	scanDir(root);
	wchar_t pattern[MAX_PATH];
	_snwprintf_s(pattern, _TRUNCATE, L"%s\\*", root);
	WIN32_FIND_DATAW fd = {};
	HANDLE h = FindFirstFileW(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0
			|| (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
			continue;
		wchar_t sub[MAX_PATH];
		_snwprintf_s(sub, _TRUNCATE, L"%s\\%s", root, fd.cFileName);
		scanDir(sub);
		/* One more level (e.g. MyRoms\x68k\*.zip when user picked MyRoms). */
		wchar_t pattern2[MAX_PATH];
		_snwprintf_s(pattern2, _TRUNCATE, L"%s\\*", sub);
		WIN32_FIND_DATAW fd2 = {};
		HANDLE h2 = FindFirstFileW(pattern2, &fd2);
		if (h2 == INVALID_HANDLE_VALUE) continue;
		do {
			if (!(fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
			if (fd2.cFileName[0] == L'.' && (fd2.cFileName[1] == 0
				|| (fd2.cFileName[1] == L'.' && fd2.cFileName[2] == 0)))
				continue;
			wchar_t sub2[MAX_PATH];
			_snwprintf_s(sub2, _TRUNCATE, L"%s\\%s", sub, fd2.cFileName);
			scanDir(sub2);
		} while (FindNextFileW(h2, &fd2));
		FindClose(h2);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static void CEmuZipScanRootPath(wchar_t* out, int outCch)
{
	if (!out || outCch <= 0) return;
	out[0] = 0;
	wchar_t base[MAX_PATH] = {};
	if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base)) || !base[0])
		return;
	_snwprintf_s(out, (size_t)outCch, _TRUNCATE, L"%s\\oggYSED", base);
	CreateDirectoryW(out, NULL);
	wcscat_s(out, (size_t)outCch, L"\\cemu_zip_root.txt");
}

static void CEmuLoadZipScanRoot(wchar_t* out, int outCch)
{
	if (!out || outCch <= 0) return;
	out[0] = 0;
	wchar_t path[MAX_PATH] = {};
	CEmuZipScanRootPath(path, MAX_PATH);
	if (!path[0]) return;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	char buf[MAX_PATH * 3] = {};
	DWORD rd = 0;
	ReadFile(h, buf, (DWORD)sizeof(buf) - 1, &rd, NULL);
	CloseHandle(h);
	if (rd == 0) return;
	buf[rd] = 0;
	while (rd > 0 && (buf[rd - 1] == '\r' || buf[rd - 1] == '\n' || buf[rd - 1] == ' '))
		buf[--rd] = 0;
	MultiByteToWideChar(CP_UTF8, 0, buf, -1, out, outCch);
	if (out[0] && GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES)
		out[0] = 0;
}

static void CEmuSaveZipScanRoot(const wchar_t* root)
{
	wchar_t path[MAX_PATH] = {};
	CEmuZipScanRootPath(path, MAX_PATH);
	if (!path[0]) return;
	if (!root || !root[0]) {
		DeleteFileW(path);
		return;
	}
	char utf8[MAX_PATH * 3] = {};
	WideCharToMultiByte(CP_UTF8, 0, root, -1, utf8, (int)sizeof(utf8), NULL, NULL);
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	DWORD wr = 0;
	WriteFile(h, utf8, (DWORD)strlen(utf8), &wr, NULL);
	CloseHandle(h);
}

static size_t CEmuCollectLocalZipStems(std::unordered_set<std::string>& stems,
	std::unordered_map<std::string, std::wstring>& stemToPath)
{
	stems.clear();
	stemToPath.clear();
	CEmuMgr* mgr = CEmuMgrGet();
	if (mgr && mgr->dataRoot[0])
		CEmuScanLocalZipStemsMerge(mgr->dataRoot, stems, stemToPath);
	wchar_t extra[MAX_PATH] = {};
	CEmuLoadZipScanRoot(extra, MAX_PATH);
	if (extra[0])
		CEmuScanLocalZipStemsMerge(extra, stems, stemToPath);
	return stems.size();
}

static bool CEmuIsFilterSep(wchar_t c)
{
	return c == L' ' || c == L'\t' || c == L',' || c == L';' || c == L'/'
		|| c == 0x3000 /* 全角空白 */ || c == 0x00A0 /* NBSP */
		|| c == 0xFF0F /* ／ */ || c == 0xFF0C /* ， */;
}

static bool CatRowMatchesFilter(const CString& hayLower, const CString& filterRaw)
{
	CString raw = filterRaw;
	raw.Trim();
	if (raw.IsEmpty()) return true;
	raw.MakeLower();

	for (int p = 0; p < raw.GetLength(); ) {
		while (p < raw.GetLength() && CEmuIsFilterSep(raw[p])) ++p;
		if (p >= raw.GetLength()) break;
		const int start = p;
		while (p < raw.GetLength() && !CEmuIsFilterSep(raw[p])) ++p;
		CString t = raw.Mid(start, p - start);
		t.Trim();
		if (t.IsEmpty()) continue;
		if (hayLower.Find(t) < 0)
			return false;
	}
	return true;
}

static CString CEmuJoinArchiveModeTags(const CEmuCatalog* cat, const CEmuGameEntry* ge)
{
	CString out;
	if (!cat || !ge || !ge->archive[0]) {
		if (ge && ge->subtype[0]) out = ge->subtype;
		return out;
	}
	CEmuArchiveMode modes[CEMU_MODE_MAX];
	const int n = CEmuCatalogListArchiveModes(cat, ge->archive, ge->dataDir, NULL,
		modes, CEMU_MODE_MAX);
	for (int i = 0; i < n; i++) {
		if (!modes[i].tag[0]) continue;
		if (!out.IsEmpty()) out += L" / ";
		out += modes[i].tag;
	}
	if (out.IsEmpty() && ge->subtype[0])
		out = ge->subtype;
	return out;
}

static void CEmuFillMonitorFlags(const CEmuCatalog* cat, const CEmuGameEntry* ge,
	int& monFm, int& monMidi)
{
	monFm = 0;
	monMidi = 0;
	if (cat && ge && ge->archive[0]) {
		CEmuArchiveMode modes[CEMU_MODE_MAX];
		const int n = CEmuCatalogListArchiveModes(cat, ge->archive, ge->dataDir, NULL,
			modes, CEMU_MODE_MAX);
		for (int i = 0; i < n; i++) {
			if (!modes[i].tag[0]) continue;
			if (modes[i].isMidi) monMidi = 1;
			else monFm = 1;
		}
	}
	if (!monFm && !monMidi && ge) {
		char tag[CEMU_MODE_TAG] = {};
		if (CEmuModeTagFromEntry(ge, tag, (int)sizeof(tag)) && tag[0]) {
			if (CEmuModeIsMidiTag(tag)) monMidi = 1;
			else monFm = 1;
		} else {
			monFm = 1;
		}
	}
}

static CString CEmuMonitorChipText(int fm, int midi)
{
	CString s;
	if (fm) s += L"[FMmon]";
	if (midi) s += L"[MIDmon]";
	return s;
}

static CString CEmuMonitorPlainLabel(int fm, int midi)
{
	if (fm && midi) return L"FM+MIDI";
	if (midi) return L"MIDI";
	if (fm) return L"FM";
	return L"-";
}

static CString CEmuChipWrap(const CString& tag)
{
	CString t = tag;
	t.Trim();
	if (t.IsEmpty()) return CString();
	/* チップパーサは英数 . + - のみ。空白や / は落とす。 */
	CString inner;
	for (int i = 0; i < t.GetLength(); i++) {
		const wchar_t c = t[i];
		if ((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z')
			|| (c >= L'a' && c <= L'z') || c == L'.' || c == L'+' || c == L'-')
			inner.AppendChar(c);
	}
	if (inner.IsEmpty()) return CString();
	return L"[" + inner + L"]";
}

static CString CEmuShortSoundTag(const CString& tag)
{
	if (tag.CompareNoCase(L"SOUNDBLASTER") == 0) return L"SB";
	if (tag.CompareNoCase(L"GAMEBLASTER") == 0) return L"CMS";
	return tag;
}

static CString CEmuModesChipText(const CString& modes)
{
	CString out;
	CString rest = modes;
	while (!rest.IsEmpty()) {
		CString tok;
		const int slash = rest.Find(L" / ");
		if (slash >= 0) {
			tok = rest.Left(slash);
			rest = rest.Mid(slash + 3);
		} else {
			tok = rest;
			rest.Empty();
		}
		tok.Trim();
		if (tok.IsEmpty()) continue;
		out += CEmuChipWrap(CEmuShortSoundTag(tok));
	}
	return out;
}

static CString CEmuPlatformChipLabel(const char* platform)
{
	if (!platform || !platform[0]) return L"?";
	if (_strnicmp(platform, "pcat", 4) == 0) return L"PC-AT";
	if (_strnicmp(platform, "pc98", 4) == 0) return L"PC-98";
	if (_strnicmp(platform, "pc88", 4) == 0 || _stricmp(platform, "pc80sr") == 0)
		return L"PC-88";
	if (_stricmp(platform, "x68k") == 0) return L"X68K";
	if (_stricmp(platform, "x1") == 0) return L"X1";
	if (_stricmp(platform, "fmtowns") == 0) return L"TOWNS";
	if (_strnicmp(platform, "fm7", 3) == 0 || _stricmp(platform, "mucomfm") == 0)
		return L"FM7";
	if (_stricmp(platform, "msx") == 0) return L"MSX";
	if (_stricmp(platform, "sg1000") == 0) return L"SG";
	if (_stricmp(platform, "neogeo") == 0) return L"NEO";
	if (_stricmp(platform, "videosystem") == 0) return L"VSYS";
	CString lab(platform);
	lab.MakeUpper();
	if (lab.GetLength() > 10) lab = lab.Left(10);
	return lab;
}

static CString CEmuGenreHayExtra(const char* g)
{
	if (!g || !g[0]) return CString();
	CString s;
	s.Format(L" %hs", g);
	if (_stricmp(g, "RPG") == 0)
		s += L" rpg ロールプレイング role-playing jrpg";
	else if (_stricmp(g, "ARPG") == 0)
		s += L" rpg arpg アクションRPG action-rpg action role-playing";
	else if (_stricmp(g, "SRPG") == 0)
		s += L" rpg srpg slg シミュレーションRPG tactical strategy-rpg";
	else if (_stricmp(g, "Shooter") == 0)
		s += L" stg シューティング shooter shmup shooting";
	else if (_stricmp(g, "Action") == 0)
		s += L" act アクション action platform beat";
	else if (_stricmp(g, "Adventure") == 0)
		s += L" adv アドベンチャー adventure";
	else if (_stricmp(g, "Novel") == 0)
		s += L" adv vn ノベル visual-novel visual novel eroge";
	else if (_stricmp(g, "Strategy") == 0)
		s += L" slg シミュレーション strategy wargame tactics";
	else if (_stricmp(g, "Sim") == 0)
		s += L" slg シミュレーション simulation sim";
	else if (_stricmp(g, "Fighting") == 0)
		s += L" ftg 格闘 fighting fighter vs";
	else if (_stricmp(g, "Racing") == 0)
		s += L" rac レース racing driving";
	else if (_stricmp(g, "Puzzle") == 0)
		s += L" pzl パズル puzzle";
	else if (_stricmp(g, "Sports") == 0)
		s += L" spt スポーツ sports";
	else if (_stricmp(g, "Mahjong") == 0)
		s += L" tbl 麻雀 mahjong table";
	else if (_stricmp(g, "Quiz") == 0)
		s += L" tbl クイズ quiz table";
	else if (_stricmp(g, "Board") == 0)
		s += L" tbl 将棋 テーブル board chess shogi cards table";
	else if (_stricmp(g, "Music") == 0)
		s += L" mus 音楽 music driver mml";
	else if (_stricmp(g, "Other") == 0)
		s += L" etc その他 other misc";
	return s;
}

static COLORREF CEmuDataKindColor(const char* dataDir)
{
	if (!dataDir || !dataDir[0]) return RGB(210, 210, 220);
	if (_stricmp(dataDir, "pc") == 0) return RGB(186, 214, 255);
	if (_stricmp(dataDir, "pc98") == 0) return RGB(210, 196, 255);
	if (_stricmp(dataDir, "pc88") == 0) return RGB(186, 236, 200);
	if (_stricmp(dataDir, "x68k") == 0) return RGB(255, 230, 170);
	if (_stricmp(dataDir, "x1") == 0) return RGB(255, 200, 170);
	if (_stricmp(dataDir, "fmtowns") == 0) return RGB(170, 230, 230);
	if (_stricmp(dataDir, "fm7") == 0) return RGB(200, 255, 186);
	if (_stricmp(dataDir, "msx") == 0) return RGB(255, 186, 196);
	if (_stricmp(dataDir, "sc3000") == 0) return RGB(230, 210, 170);
	if (_stricmp(dataDir, "ac") == 0) return RGB(200, 210, 230);
	unsigned h = 2166136261u;
	for (const char* p = dataDir; *p; p++) {
		char c = *p;
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		h ^= (unsigned)(unsigned char)c;
		h *= 16777619u;
	}
	return RGB(168 + (int)(h % 72), 168 + (int)((h >> 8) % 72), 168 + (int)((h >> 16) % 72));
}

static int CEmuCatCmpRows(const CEmuCatListRow& a, const CEmuCatListRow& b, int col)
{
	int c = 0;
	const char* aa = (a.ge && a.ge->archive[0]) ? a.ge->archive : "";
	const char* ba = (b.ge && b.ge->archive[0]) ? b.ge->archive : "";
	const char* ap = (a.ge && a.ge->platform[0]) ? a.ge->platform : "";
	const char* bp = (b.ge && b.ge->platform[0]) ? b.ge->platform : "";
	const char* ad = (a.ge && a.ge->dataDir[0]) ? a.ge->dataDir : "";
	const char* bd = (b.ge && b.ge->dataDir[0]) ? b.ge->dataDir : "";
	const char* ag = (a.ge && a.ge->genre[0]) ? a.ge->genre : "";
	const char* bg = (b.ge && b.ge->genre[0]) ? b.ge->genre : "";
	switch (col) {
	case 0: c = a.title.CompareNoCase(b.title); break;
	case 1: c = _stricmp(ag, bg); break;
	case 2: c = _stricmp(aa, ba); break;
	case 3: c = _stricmp(ap, bp); break;
	case 4: c = a.modes.CompareNoCase(b.modes); break;
	case 5: {
		const int ka = (a.monFm ? 1 : 0) + (a.monMidi ? 2 : 0);
		const int kb = (b.monFm ? 1 : 0) + (b.monMidi ? 2 : 0);
		c = ka - kb;
		break;
	}
	case 6: c = _stricmp(ad, bd); break;
	default: break;
	}
	if (c == 0) c = a.origIndex - b.origIndex;
	return c;
}

static HBITMAP CEmuMakeSortArrowBmp(BOOL up)
{
	const int sz = 13;
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = sz;
	bi.bmiHeader.biHeight = -sz;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP hb = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (!hb || !bits) return NULL;
	DWORD* px = (DWORD*)bits;
	for (int i = 0; i < sz * sz; i++)
		px[i] = 0x00000000;
	const int cx = sz / 2;
	const DWORD fill = 0xFF5A4696; /* 不透明・見出し紫 */
	auto put = [&](int x, int y) {
		if (x >= 0 && x < sz && y >= 0 && y < sz)
			px[y * sz + x] = fill;
	};
	if (up) {
		const int top = 2, bot = sz - 3;
		for (int y = top; y <= bot; y++) {
			const int t = y - top;
			const int half = (t * (sz - 4)) / (std::max)(1, bot - top);
			for (int x = cx - half; x <= cx + half; x++)
				put(x, y);
		}
	} else {
		const int top = 3, bot = sz - 3;
		for (int y = top; y <= bot; y++) {
			const int t = bot - y;
			const int half = (t * (sz - 4)) / (std::max)(1, bot - top);
			for (int x = cx - half; x <= cx + half; x++)
				put(x, y);
		}
	}
	return hb;
}

static const CEmuGameEntry* CEmuPickGroupRepresentative(const CEmuCatalog* cat,
	const CEmuGameEntry* ge)
{
	if (!cat || !ge || !ge->archive[0]) return ge;
	const CEmuGameEntry* best = CEmuCatalogFindArchiveForZipMode(cat, ge->archive,
		ge->dataDir, NULL, NULL);
	return best ? best : ge;
}

static CString CEmuBuildFilterHay(const CEmuCatalog* cat, const CEmuGameEntry* ge,
	const CString& modes)
{
	CString hay;
	hay.Format(L"%hs %hs %hs %hs %hs %s %s",
		ge->archive[0] ? ge->archive : "",
		ge->platform[0] ? ge->platform : "",
		ge->subtype[0] ? ge->subtype : "",
		ge->dataDir[0] ? ge->dataDir : "",
		ge->genre[0] ? ge->genre : "",
		ge->name[0] ? ge->name : L"",
		(LPCTSTR)modes);
	if (cat && ge->archive[0]) {
		CEmuArchiveMode m[CEMU_MODE_MAX];
		const int n = CEmuCatalogListArchiveModes(cat, ge->archive, ge->dataDir, NULL,
			m, CEMU_MODE_MAX);
		for (int i = 0; i < n; i++) {
			if (m[i].entryIndex >= 0 && m[i].entryIndex < cat->count) {
				const CEmuGameEntry* e = cat->entry[m[i].entryIndex];
				if (e && e->name[0]) {
					hay += L' ';
					hay += e->name;
				}
			}
			if (m[i].tag[0]) {
				hay += L' ';
				hay += CEmuShortSoundTag(CString(m[i].tag));
			}
		}
	}
	hay += L' ';
	hay += CEmuPlatformChipLabel(ge->platform[0] ? ge->platform : "");
	hay += L' ';
	hay += CEmuModesChipText(modes);
	hay += CEmuGenreHayExtra(ge->genre[0] ? ge->genre : "");
	hay.MakeLower();
	return hay;
}

} // namespace

/* PlayList.cpp から公開 */
bool PlCemuAddZipAndPlay(LPCTSTR zipPhysical);

IMPLEMENT_DYNAMIC(CEmuCatListCtrl, CCustomListCtrl)
BEGIN_MESSAGE_MAP(CEmuCatListCtrl, CCustomListCtrl)
END_MESSAGE_MAP()

void CEmuCatListCtrl::EnsureSortArrows()
{
	if (m_sortIL.GetSafeHandle()) return;
	if (!m_sortIL.Create(13, 13, ILC_COLOR32, 2, 1))
		return;
	HBITMAP up = CEmuMakeSortArrowBmp(TRUE);
	HBITMAP dn = CEmuMakeSortArrowBmp(FALSE);
	if (up) {
		ImageList_Add(m_sortIL.GetSafeHandle(), up, NULL);
		::DeleteObject(up);
	}
	if (dn) {
		ImageList_Add(m_sortIL.GetSafeHandle(), dn, NULL);
		::DeleteObject(dn);
	}
	if (CHeaderCtrl* hdr = GetHeaderCtrl())
		hdr->SetImageList(&m_sortIL);
}

void CEmuCatListCtrl::SetSortState(int col, int dir)
{
	CHeaderCtrl* hdr = GetHeaderCtrl();
	if (!hdr) return;
	EnsureSortArrows();
	const int n = hdr->GetItemCount();
	if (!m_colTitleReady && n >= 7) {
		for (int i = 0; i < 7 && i < n; i++) {
			TCHAR buf[128] = {};
			HDITEM hi = {};
			hi.mask = HDI_TEXT;
			hi.pszText = buf;
			hi.cchTextMax = (int)_countof(buf);
			if (hdr->GetItem(i, &hi) && hi.pszText)
				m_colTitle[i] = hi.pszText;
		}
		m_colTitleReady = TRUE;
	}
	for (int i = 0; i < n; i++) {
		HDITEM hi = {};
		hi.mask = HDI_FORMAT | HDI_IMAGE | HDI_TEXT;
		TCHAR buf[160] = {};
		hi.pszText = buf;
		hi.cchTextMax = (int)_countof(buf);
		hdr->GetItem(i, &hi);
		hi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN | HDF_IMAGE | HDF_BITMAP_ON_RIGHT);
		CString title = (i < 7 && m_colTitleReady) ? m_colTitle[i] : CString(buf);
		if (i == col && dir == 1) {
			hi.fmt |= HDF_IMAGE | HDF_BITMAP_ON_RIGHT;
			hi.iImage = 0;
			title += L"  ▲";
		} else if (i == col && dir == 2) {
			hi.fmt |= HDF_IMAGE | HDF_BITMAP_ON_RIGHT;
			hi.iImage = 1;
			title += L"  ▼";
		} else {
			hi.iImage = I_IMAGENONE;
		}
		wcsncpy_s(buf, title, _TRUNCATE);
		hi.pszText = buf;
		hi.mask = HDI_FORMAT | HDI_IMAGE | HDI_TEXT;
		hdr->SetItem(i, &hi);
	}
}

BOOL CEmuCatListCtrl::GetListRowTint(int row, COLORREF& tintBg, COLORREF& accent) const
{
	if (row < 0) return FALSE;
	const CEmuCatListRow* rowp = (const CEmuCatListRow*)GetItemData(row);
	if (!rowp || !rowp->ge) return FALSE;
	const COLORREF c = CEmuDataKindColor(rowp->ge->dataDir);
	tintBg = c;
	accent = c;
	return TRUE;
}

void CEmuCatListCtrl::BuildToolTipText(int row, int col, CString& out)
{
	UNREFERENCED_PARAMETER(col);
	out.Empty();
	if (row < 0) return;
	const CEmuCatListRow* rowp = (const CEmuCatListRow*)GetItemData(row);
	if (!rowp || !rowp->ge) return;
	const CEmuGameEntry* ge = rowp->ge;
	CString modes = rowp->modes;
	CString title = rowp->title;
	const CString monLab = CEmuMonitorPlainLabel(rowp->monFm, rowp->monMidi);
	out.Format(LL14(
		L"タイトル: %s\nジャンル: %hs\nアーカイブ: %hs\n機種: %hs\n音源: %s\nモニタ: %s\ndata: %hs\n"
		L"zip: %s\n（音源はプレイリストの右クリックで切り替え）",
		L"Title: %s\nGenre: %hs\nArchive: %hs\nPlatform: %hs\nSound: %s\nMonitor: %s\ndata: %hs\n"
		L"zip: %s\n(Switch sound from the playlist right-click menu)",
		L"Titre: %s\nGenre: %hs\nArchive: %hs\nPlateforme: %hs\nSon: %s\nMoniteur: %s\ndata: %hs\n"
		L"zip: %s\n(Changer le son via le menu contextuel de la liste)",
		L"Titolo: %s\nGenere: %hs\nArchivio: %hs\nPiattaforma: %hs\nSuono: %s\nMonitor: %s\ndata: %hs\n"
		L"zip: %s\n(Cambia il suono dal menu contestuale della playlist)",
		L"Titulo: %s\nGenero: %hs\nArchivo: %hs\nPlataforma: %hs\nSonido: %s\nMonitor: %s\ndata: %hs\n"
		L"zip: %s\n(Cambia el sonido desde el menu contextual de la lista)",
		L"제목: %s\n장르: %hs\n아카이브: %hs\n기종: %hs\n음원: %s\n모니터: %s\ndata: %hs\n"
		L"zip: %s\n(음원은 플레이리스트 우클릭으로 전환)",
		L"标题：%s\n类型：%hs\n归档：%hs\n机种：%hs\n音源：%s\n监视器：%s\ndata：%hs\n"
		L"zip：%s\n（音源可在播放列表右键菜单切换）",
		L"العنوان: %s\nالنوع: %hs\nالأرشيف: %hs\nالمنصة: %hs\nالصوت: %s\nالشاشة: %s\ndata: %hs\n"
		L"zip: %s\n(بدّل الصوت من قائمة التشغيل)",
		L"Название: %s\nЖанр: %hs\nАрхив: %hs\nПлатформа: %hs\nЗвук: %s\nМонитор: %s\ndata: %hs\n"
		L"zip: %s\n(Звук переключается в меню плейлиста)",
		L"Titel: %s\nGenre: %hs\nArchiv: %hs\nPlattform: %hs\nSound: %s\nMonitor: %s\ndata: %hs\n"
		L"zip: %s\n(Sound per Playlist-Kontextmenu wechseln)",
		L"Titulo: %s\nGenero: %hs\nArquivo: %hs\nPlataforma: %hs\nSom: %s\nMonitor: %s\ndata: %hs\n"
		L"zip: %s\n(Troque o som no menu da playlist)",
		L"Titel: %s\nGenre: %hs\nArchief: %hs\nPlatform: %hs\nGeluid: %s\nMonitor: %s\ndata: %hs\n"
		L"zip: %s\n(Wissel geluid via playlist-snelmenu)",
		L"Tytul: %s\nGatunek: %hs\nArchiwum: %hs\nPlatforma: %hs\nDzwiek: %s\nMonitor: %s\ndata: %hs\n"
		L"zip: %s\n(Dzwiek zmienisz w menu playlisty)",
		L"Baslik: %s\nTur: %hs\nArsiv: %hs\nPlatform: %hs\nSes: %s\nMonitor: %s\ndata: %hs\n"
		L"zip: %s\n(Sesi playlist sag tik menuden degistir)"),
		(LPCTSTR)title,
		ge->genre[0] ? ge->genre : "-",
		ge->archive[0] ? ge->archive : "-",
		ge->platform[0] ? ge->platform : "-",
		modes.IsEmpty() ? L"-" : (LPCTSTR)modes,
		(LPCTSTR)monLab,
		ge->dataDir[0] ? ge->dataDir : "-",
		rowp->zipPath.IsEmpty() ? L"-" : (LPCTSTR)rowp->zipPath);
}

IMPLEMENT_DYNAMIC(CEmuCatalogListDlg, CCustomBlurDialogBase)

CEmuCatalogListDlg::CEmuCatalogListDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CEmuCatalogListDlg::IDD, pParent)
{
}

CEmuCatalogListDlg::~CEmuCatalogListDlg()
{
}

void CEmuCatalogListDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CEMU_CAT_LIST, m_lc);
	DDX_Control(pDX, IDC_CEMU_CAT_FILTER, m_filter);
	DDX_Control(pDX, IDC_CEMU_CAT_FILTER_L, m_filterLbl);
	DDX_Control(pDX, IDC_CEMU_CAT_DESC, m_desc);
	DDX_Control(pDX, IDOK, m_ok);
	DDX_Control(pDX, IDC_CEMU_CAT_HELP, m_help);
}

BEGIN_MESSAGE_MAP(CEmuCatalogListDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDOK, &CEmuCatalogListDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDC_CEMU_CAT_HELP, &CEmuCatalogListDlg::OnBnClickedHelp)
	ON_EN_CHANGE(IDC_CEMU_CAT_FILTER, &CEmuCatalogListDlg::OnEnChangeFilter)
	ON_NOTIFY(NM_DBLCLK, IDC_CEMU_CAT_LIST, &CEmuCatalogListDlg::OnNMDblclkList)
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_CEMU_CAT_LIST, &CEmuCatalogListDlg::OnLvnColumnClick)
	ON_MESSAGE(WM_CEMU_CAT_FILTER, &CEmuCatalogListDlg::OnFilterApply)
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_DESTROY()
	ON_WM_CLOSE()
	ON_WM_ACTIVATE()
cmn(CEmuCatalogListDlg);

void CEmuCatalogListDlg::CloseIfOpen()
{
	if (g_cemuCatList && ::IsWindow(g_cemuCatList->GetSafeHwnd()))
		g_cemuCatList->DestroyWindow();
	else if (g_cemuCatList) {
		delete g_cemuCatList;
		g_cemuCatList = nullptr;
	}
}

void CEmuCatalogListDlg::Show(CWnd* pParent)
{
	if (!CEmuHasExeArcdata()) {
		AfxMessageBox(LL14(
			L"exe と同じ場所に arcdata.zip がありません。",
			L"arcdata.zip was not found next to the executable.",
			L"arcdata.zip introuvable a cote de l'exe.",
			L"arcdata.zip non trovato accanto all'exe.",
			L"No hay arcdata.zip junto al exe.",
			L"실행 파일 옆에 arcdata.zip 이 없습니다.",
			L"程序同目录没有 arcdata.zip。",
			L"لا يوجد arcdata.zip بجانب البرنامج.",
			L"Рядом с exe нет arcdata.zip.",
			L"arcdata.zip fehlt neben der Exe.",
			L"arcdata.zip nao esta ao lado do exe.",
			L"arcdata.zip ontbreekt naast de exe.",
			L"Brak arcdata.zip obok exe.",
			L"exe yaninda arcdata.zip yok."), MB_ICONINFORMATION);
		return;
	}

	CEmuMgrEnsureCatalog(CEmuMgrGet());

	/* Prefer MP as owner so closing MP destroys this window with it. */
	CWnd* owner = pParent;
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		owner = mp;
	else if (!owner)
		owner = AfxGetMainWnd();

	std::unordered_set<std::string> stems;
	std::unordered_map<std::string, std::wstring> stemToPath;
	CEmuCollectLocalZipStems(stems, stemToPath);
	if (stems.empty()) {
		wchar_t folder[MAX_PATH] = {};
		if (!CEmuBrowseFolder(owner, folder, MAX_PATH))
			return;
		CEmuSaveZipScanRoot(folder);
		stems.clear();
		stemToPath.clear();
		CEmuCollectLocalZipStems(stems, stemToPath);
		if (stems.empty()) {
			AfxMessageBox(LL14(
				L"選んだフォルダ配下に対応 zip が見つかりませんでした。",
				L"No matching zips were found under the chosen folder.",
				L"Aucun zip correspondant sous le dossier choisi.",
				L"Nessun zip corrispondente nella cartella scelta.",
				L"No hay zips coincidentes en la carpeta elegida.",
				L"선택한 폴더 아래에 대응 zip이 없습니다.",
				L"所选文件夹下没有对应的 zip。",
				L"لا توجد ملفات zip مطابقة تحت المجلد المختار.",
				L"В выбранной папке нет подходящих zip.",
				L"Unter dem gewählten Ordner keine passenden Zips.",
				L"Nenhum zip correspondente na pasta escolhida.",
				L"Geen passende zips onder de gekozen map.",
				L"Brak pasujacych zip w wybranym folderze.",
				L"Secilen klasorde eslesen zip yok."), MB_ICONINFORMATION);
			return;
		}
	}

	if (g_cemuCatList && ::IsWindow(g_cemuCatList->GetSafeHwnd())) {
		g_cemuCatList->BuildRowCache();
		g_cemuCatList->ApplyFilterToList();
		g_cemuCatList->ShowWindow(SW_SHOW);
		g_cemuCatList->SetForegroundWindow();
		return;
	}
	if (g_cemuCatList) {
		delete g_cemuCatList;
		g_cemuCatList = nullptr;
	}

	CEmuCatalogListDlg* dlg = new CEmuCatalogListDlg(owner);
	if (!dlg->Create(CEmuCatalogListDlg::IDD, owner)) {
		delete dlg;
		return;
	}
	g_cemuCatList = dlg;
	dlg->ShowWindow(SW_SHOW);
	dlg->SetForegroundWindow();
}

void CEmuCatalogListDlg::PostNcDestroy()
{
	if (g_cemuCatList == this)
		g_cemuCatList = nullptr;
	CCustomBlurDialogBase::PostNcDestroy();
	delete this;
}

void CEmuCatalogListDlg::OnOK() { DestroyWindow(); }
void CEmuCatalogListDlg::OnCancel() { DestroyWindow(); }
void CEmuCatalogListDlg::OnClose() { DestroyWindow(); }

BOOL CEmuCatalogListDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(
		L"Cemu対応一覧", L"Cemu Supported List", L"Liste compatible Cemu", L"Elenco supportato Cemu",
		L"Lista compatible Cemu", L"Cemu 대응 목록", L"Cemu 对应一览", L"قائمة Cemu المدعومة",
		L"Список поддержки Cemu", L"Cemu-Unterstützungsliste", L"Lista suportada Cemu", L"Cemu-ondersteuningslijst",
		L"Lista obslugi Cemu", L"Cemu destek listesi"));
	if (m_desc.GetSafeHwnd())
		m_desc.SetWindowText(LL14(
			L"ダブルクリックでプレイリストへ追加して再生（手元にある zip のみ表示。同一zipの音源違いは1行）",
			L"Double-click to add and play (local zips only; sound variants share one row)",
			L"Double-clic pour ajouter et lire (zips locaux uniquement ; variantes son sur une ligne)",
			L"Doppio clic per aggiungere e riprodurre (solo zip locali; varianti audio in una riga)",
			L"Doble clic para anadir y reproducir (solo zips locales; variantes en una fila)",
			L"더블클릭으로 추가·재생(로컬 zip만 표시. 같은 zip 음원은 한 줄)",
			L"双击加入并播放（仅显示本地 zip；同 zip 音源合并一行）",
			L"نقر مزدوج للإضافة والتشغيل (الملفات المحلية فقط؛ صف واحد للصوت)",
			L"Двойной клик — в список (только локальные zip; варианты звука в одной строке)",
			L"Doppelklick: abspielen (nur lokale Zips; Sound-Varianten in einer Zeile)",
			L"Duplo clique: tocar (somente zips locais; variantes numa linha)",
			L"Dubbelklik: spelen (alleen lokale zips; geluidsvarianten op één regel)",
			L"Dwuklik: odtworz (tylko lokalne zip; warianty w jednym wierszu)",
			L"Cift tik: cal (yalniz yerel zip; ses varyantlari tek satir)"));
	if (m_filterLbl.GetSafeHwnd())
		m_filterLbl.SetWindowText(LL14(L"絞り込み", L"Filter", L"Filtrer", L"Filtro", L"Filtro", L"필터", L"筛选", L"تصفية", L"Фильтр", L"Filter", L"Filtro", L"Filter", L"Filtr", L"Filtre"));
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();
	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	m_tooltip.AddTool(&m_ok, LL14(L"閉じます", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
	if (m_filter.GetSafeHwnd()) {
		m_tooltip.AddTool(&m_filter, LL14(
			L"タイトル・ジャンル・機種・音源・モニタ・data で絞り込み。空白区切りで複数指定（すべて含む）。Shooter / ARPG / STG なども可。",
			L"Filter by title, genre, platform, sound, monitor, data. Space-separated terms (AND). Shooter / ARPG / STG also work.",
			L"Filtrer titre/genre/plateforme/son/data. Mots separes par espace (ET). Espace pleine chasse, virgule, / aussi.",
			L"Filtra titolo/genere/piattaforma/suono/data. Termini separati da spazio (AND). Spazio pieno, virgola, /.",
			L"Filtrar titulo/genero/plataforma/sonido/data. Terminos separados por espacio (AND). Espacio ancho, coma, /.",
			L"제목·장르·기종·음원·data로 필터. 공백으로 여러 단어(모두 포함). 전각 공백·,/ 도 가능. 비우면 전체. RPG/STG 가능.",
			L"按标题/类型/机种/音源/data 筛选。空格分隔多项（需全部匹配）。全角空格、逗号、/ 也可。空则全显示。RPG/STG 也可。",
			L"تصفية بالعنوان والنوع والمنصة والصوت. افصل بمسافة (يجب أن تطابق كلها).",
			L"Фильтр по названию, жанру, платформе, звуку. Слова через пробел (И).",
			L"Nach Titel, Genre, Plattform, Sound, data filtern. Leerzeichen trennt (UND). Vollbreite Leerzeichen, Komma, /.",
			L"Filtrar por titulo, genero, plataforma, som, data. Espacos separam (E). Espaco largo, virgula, /.",
			L"Filter op titel, genre, platform, geluid, data. Spaties scheiden (EN). Volledige spatie, komma, /.",
			L"Filtruj tytul/gatunek/platforme/dzwiek/data. Spacje oddzielaja (AND). Pelna spacja, przecinek, /.",
			L"Baslik, tur, platform, ses, data ile filtre. Boslukla birden fazla (hepsi). Tam bosluk, virgul, /."));
		m_filter.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)LL14(
			L"空白区切りで複数指定",
			L"Space-separated terms",
			L"Mots separes par espace",
			L"Termini separati da spazio",
			L"Terminos separados por espacio",
			L"공백으로 여러 단어",
			L"空格分隔多项",
			L"افصل بمسافة",
			L"Слова через пробел",
			L"Leerzeichen trennt",
			L"Espacos separam",
			L"Spaties scheiden",
			L"Spacje oddzielaja",
			L"Boslukla birden fazla"));
	}
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);

	if (m_lc.GetSafeHwnd()) {
		m_lc.SetExtendedStyle(m_lc.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
		m_lc.EnableToolTips(TRUE);
		m_lc.InsertColumn(0, LL14(L"タイトル", L"Title", L"Titre", L"Titolo", L"Titulo", L"제목", L"标题", L"العنوان", L"Название", L"Titel", L"Titulo", L"Titel", L"Tytul", L"Baslik"), LVCFMT_LEFT, 180);
		m_lc.InsertColumn(1, LL14(L"ジャンル", L"Genre", L"Genre", L"Genere", L"Genero", L"장르", L"类型", L"النوع", L"Жанр", L"Genre", L"Genero", L"Genre", L"Gatunek", L"Tur"), LVCFMT_LEFT, 108);
		m_lc.InsertColumn(2, LL14(L"アーカイブ", L"Archive", L"Archive", L"Archivio", L"Archivo", L"아카이브", L"归档", L"الأرشيف", L"Архив", L"Archiv", L"Arquivo", L"Archief", L"Archiwum", L"Arsiv"), LVCFMT_LEFT, 100);
		m_lc.InsertColumn(3, LL14(L"機種", L"Platform", L"Plateforme", L"Piattaforma", L"Plataforma", L"기종", L"机种", L"المنصة", L"Платформа", L"Plattform", L"Plataforma", L"Platform", L"Platforma", L"Platform"), LVCFMT_LEFT, 80);
		m_lc.InsertColumn(4, LL14(L"音源", L"Sound", L"Son", L"Suono", L"Sonido", L"음원", L"音源", L"الصوت", L"Звук", L"Sound", L"Som", L"Geluid", L"Dzwiek", L"Ses"), LVCFMT_LEFT, 120);
		m_lc.InsertColumn(5, LL14(L"モニタ", L"Monitor", L"Moniteur", L"Monitor", L"Monitor", L"모니터", L"监视器", L"الشاشة", L"Монитор", L"Monitor", L"Monitor", L"Monitor", L"Monitor", L"Monitor"), LVCFMT_LEFT, 100);
		m_lc.InsertColumn(6, L"data", LVCFMT_LEFT, 60);
		m_lc.SetSortState(-1, 0);
	}

	CEmuMgrEnsureCatalog(CEmuMgrGet());
	BuildRowCache();
	ApplyFilterToList();

	CRect wr;
	GetWindowRect(&wr);
	m_minW = wr.Width();
	m_minH = wr.Height();
	RestoreSavedPlacement();
	LayoutControls();
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	CCC_BringDialogToForeground(this);
	return TRUE;
}

BOOL CEmuCatalogListDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

int CEmuCatalogListDlg::BuildRowCache()
{
	m_rows.clear();
	m_zipStemCount = 0;
	CEmuMgr* mgr = CEmuMgrGet();
	CEmuMgrEnsureCatalog(mgr);
	const CEmuCatalog* cat = mgr ? &mgr->catalog : NULL;
	if (!cat || cat->count <= 0) return 0;

	std::unordered_set<std::string> localStems;
	std::unordered_map<std::string, std::wstring> stemToPath;
	m_zipStemCount = CEmuCollectLocalZipStems(localStems, stemToPath);

	/* First pass: one row per archive(+dataDir) group — O(n) with a set. */
	std::unordered_set<std::string> seenGroup;
	m_rows.reserve((size_t)(cat->count / 2) + 8);
	for (int i = 0; i < cat->count; i++) {
		const CEmuGameEntry* ge = cat->entry[i];
		if (!ge || !ge->archive[0]) continue;

		char groupKey[CEMU_ARCHIVE_NAME + CEMU_DATA_DIR + 4];
		_snprintf_s(groupKey, _TRUNCATE, "%s\n%s", ge->archive, ge->dataDir[0] ? ge->dataDir : "");
		for (char* p = groupKey; *p; p++) {
			if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
		}
		if (!seenGroup.insert(groupKey).second)
			continue;

		char stem[CEMU_ARCHIVE_NAME];
		CEmuArchivePrimaryStem(ge->archive, stem, (int)sizeof(stem));
		if (!stem[0] || localStems.find(stem) == localStems.end())
			continue;
		/* Listing an archive that plays nothing is worse than not offering
		   it: keep the list to titles verified to play. */
		if (!CEmuArchiveIsSupported(stem))
			continue;

		const CEmuGameEntry* pick = CEmuPickGroupRepresentative(cat, ge);
		if (!pick) pick = ge;

		CEmuCatListRow row;
		row.ge = pick;
		row.title = pick->name[0] ? pick->name : L"(no name)";
		CEmuStripModeParenFromTitle(row.title);
		row.modes = CEmuJoinArchiveModeTags(cat, pick);
		CEmuFillMonitorFlags(cat, pick, row.monFm, row.monMidi);
		row.hayLower = CEmuBuildFilterHay(cat, pick, row.modes);
		if (row.monFm) row.hayLower += L" fm fmmon";
		if (row.monMidi) row.hayLower += L" midi midimon";
		if (row.monFm && row.monMidi) row.hayLower += L" fm+midi";
		auto it = stemToPath.find(stem);
		if (it != stemToPath.end())
			row.zipPath = it->second.c_str();
		row.origIndex = (int)m_rows.size();
		m_rows.push_back(std::move(row));
	}
	return (int)m_rows.size();
}

void CEmuCatalogListDlg::ApplyFilterToList()
{
	if (!m_lc.GetSafeHwnd()) return;
	m_bFilling = TRUE;
	m_lc.SetRedraw(FALSE);

	CString keepKey;
	{
		POSITION pos = m_lc.GetFirstSelectedItemPosition();
		if (pos) {
			const int sel = m_lc.GetNextSelectedItem(pos);
			const CEmuCatListRow* rowp = (const CEmuCatListRow*)m_lc.GetItemData(sel);
			if (rowp && rowp->ge && rowp->ge->archive[0]) {
				keepKey.Format(L"%hs\n%hs", rowp->ge->archive,
					rowp->ge->dataDir[0] ? rowp->ge->dataDir : "");
			}
		}
	}

	m_lc.DeleteAllItems();
	CString filter;
	if (m_filter.GetSafeHwnd())
		m_filter.GetWindowText(filter);

	std::vector<CEmuCatListRow*> vis;
	vis.reserve(m_rows.size());
	for (size_t i = 0; i < m_rows.size(); i++) {
		CEmuCatListRow& r = m_rows[i];
		if (CatRowMatchesFilter(r.hayLower, filter))
			vis.push_back(&r);
	}
	if (m_sortCol >= 0 && m_sortDir != 0) {
		const int col = m_sortCol;
		const int dir = m_sortDir;
		std::stable_sort(vis.begin(), vis.end(),
			[col, dir](const CEmuCatListRow* a, const CEmuCatListRow* b) {
				const int c = CEmuCatCmpRows(*a, *b, col);
				return (dir == 2) ? (c > 0) : (c < 0);
			});
	}

	int restore = -1;
	int row = 0;
	for (size_t i = 0; i < vis.size(); i++) {
		CEmuCatListRow& r = *vis[i];
		const int idx = m_lc.InsertItem(row, r.title);
		m_lc.SetItemText(idx, 1, CEmuChipWrap(CString(r.ge->genre[0] ? r.ge->genre : "-")));
		m_lc.SetItemText(idx, 2, CString(r.ge->archive));
		m_lc.SetItemText(idx, 3, CEmuChipWrap(CEmuPlatformChipLabel(
			r.ge->platform[0] ? r.ge->platform : "")));
		CString soundChip = CEmuModesChipText(r.modes);
		if (soundChip.IsEmpty() && r.ge->subtype[0])
			soundChip = CEmuChipWrap(CEmuShortSoundTag(CString(r.ge->subtype)));
		m_lc.SetItemText(idx, 4, soundChip);
		m_lc.SetItemText(idx, 5, CEmuMonitorChipText(r.monFm, r.monMidi));
		m_lc.SetItemText(idx, 6, CEmuChipWrap(CString(r.ge->dataDir[0] ? r.ge->dataDir : "-")));
		m_lc.SetItemData(idx, (DWORD_PTR)&r);
		if (restore < 0 && !keepKey.IsEmpty() && r.ge && r.ge->archive[0]) {
			CString k;
			k.Format(L"%hs\n%hs", r.ge->archive, r.ge->dataDir[0] ? r.ge->dataDir : "");
			if (k == keepKey) restore = idx;
		}
		row++;
	}
	if (restore >= 0) {
		m_lc.SetItemState(restore, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_lc.EnsureVisible(restore, FALSE);
	}
	m_lc.SetRedraw(TRUE);
	m_lc.Invalidate();
	m_bFilling = FALSE;
	LayoutColumns();
}

void CEmuCatalogListDlg::LayoutControls()
{
	if (!m_lc.GetSafeHwnd()) return;
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
	const int mx = PX(7 * dx);
	const int bw = PX(50 * dx);
	const int bh = PX(14 * dy);
	const int descTop = PX(11 * dy) + capH;
	const int descH = PX(9 * dy);
	const int filtTop = PX(24 * dy) + capH;
	const int filtH = PX(14 * dy);
	const int filtLblW = PX(36 * dx);
	const int listTop = PX(43 * dy) + capH;
	const int by = cy - PX(7 * dy) - bh;
	if (m_ok.GetSafeHwnd())
		m_ok.MoveWindow((cx - bw) / 2, by, bw, bh);
	if (m_desc.GetSafeHwnd())
		m_desc.MoveWindow(mx, descTop, (std::max)(0, cx - 2 * mx), descH);
	const int filtEditX = mx + filtLblW + PX(3 * dx);
	const int filtEditW = (std::max)(0, cx - filtEditX - mx);
	if (m_filterLbl.GetSafeHwnd())
		m_filterLbl.MoveWindow(mx, filtTop + PX(2 * dy), filtLblW, PX(10 * dy));
	if (m_filter.GetSafeHwnd())
		m_filter.MoveWindow(filtEditX, filtTop, filtEditW, filtH);
	const int listBottom = by - PX(6 * dy);
	const int listH = (std::max)(0, listBottom - listTop);
	m_lc.MoveWindow(mx, listTop, (std::max)(0, cx - 2 * mx), listH);
	LayoutColumns();
}

void CEmuCatalogListDlg::LayoutColumns()
{
	if (!m_lc.GetSafeHwnd() || !m_lc.GetHeaderCtrl()) return;
	if (m_lc.GetHeaderCtrl()->GetItemCount() < 7) return;
	CRect rc;
	m_lc.GetClientRect(&rc);
	const int total = rc.Width();
	const int genreW = 108, archW = 110, platW = 88, soundW = 150, monW = 108, dataW = 64;
	int titleW = total - genreW - archW - platW - soundW - monW - dataW;
	if (titleW < 120) titleW = 120;
	m_lc.SetColumnWidth(0, titleW);
	m_lc.SetColumnWidth(1, genreW);
	m_lc.SetColumnWidth(2, archW);
	m_lc.SetColumnWidth(3, platW);
	m_lc.SetColumnWidth(4, soundW);
	m_lc.SetColumnWidth(5, monW);
	m_lc.SetColumnWidth(6, dataW);
}

void CEmuCatalogListDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CEmuCatalogListDlg::ShowHelpSheet()
{
	if (g_cemuCatHelp && ::IsWindow(g_cemuCatHelp->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_cemuCatHelp, this);
		return;
	}
	if (g_cemuCatHelp && !::IsWindow(g_cemuCatHelp->GetSafeHwnd()))
		g_cemuCatHelp = nullptr;
	CEmuCatHelpDlg* dlg = new CEmuCatHelpDlg(this);
	if (!dlg->Create(IDD_KPI_HELP, this)) {
		delete dlg;
		return;
	}
	g_cemuCatHelp = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CEmuCatalogListDlg::OnBnClickedHelp() { ShowHelpSheet(); }
void CEmuCatalogListDlg::OnBnClickedOk() { DestroyWindow(); }
void CEmuCatalogListDlg::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CCustomBlurDialogBase::OnActivate(nState, pWndOther, bMinimized);
	if (nState == WA_INACTIVE || bMinimized) return;
	/* Re-scan so newly dropped zips appear without reopening. */
	std::unordered_set<std::string> stems;
	std::unordered_map<std::string, std::wstring> stemToPath;
	const size_t n = CEmuCollectLocalZipStems(stems, stemToPath);
	if (n == m_zipStemCount) return;
	BuildRowCache();
	ApplyFilterToList();
}
void CEmuCatalogListDlg::OnEnChangeFilter()
{
	if (m_bFilling) return;
	/* Debounce: coalesces rapid typing into one list rebuild. */
	++m_filterGen;
	PostMessage(WM_CEMU_CAT_FILTER, (WPARAM)m_filterGen, 0);
}

LRESULT CEmuCatalogListDlg::OnFilterApply(WPARAM wParam, LPARAM)
{
	if ((unsigned)wParam != m_filterGen) return 0;
	ApplyFilterToList();
	return 0;
}

void CEmuCatalogListDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		LayoutControls();
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

void CEmuCatalogListDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	if (m_minW > 0 && m_minH > 0) {
		lpMMI->ptMinTrackSize.x = m_minW;
		lpMMI->ptMinTrackSize.y = m_minH;
	}
	CCustomBlurDialogBase::OnGetMinMaxInfo(lpMMI);
}

void CEmuCatalogListDlg::RestoreSavedPlacement()
{
	if (savedata.cemuListW <= 0 || savedata.cemuListH <= 0) return;
	CRect want(savedata.cemuListX, savedata.cemuListY,
		savedata.cemuListX + savedata.cemuListW,
		savedata.cemuListY + savedata.cemuListH);
	if (want.Width() < m_minW) want.right = want.left + m_minW;
	if (want.Height() < m_minH) want.bottom = want.top + m_minH;
	HMONITOR hMon = ::MonitorFromRect(&want, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi; mi.cbSize = sizeof(mi);
	if (hMon && ::GetMonitorInfo(hMon, &mi)) {
		const CRect wa(mi.rcWork);
		int w = (std::min)(want.Width(), (int)wa.Width());
		int h = (std::min)(want.Height(), (int)wa.Height());
		int x = want.left, y = want.top;
		if (x < wa.left) x = wa.left;
		if (y < wa.top) y = wa.top;
		if (x + w > wa.right) x = wa.right - w;
		if (y + h > wa.bottom) y = wa.bottom - h;
		want.SetRect(x, y, x + w, y + h);
	}
	MoveWindow(&want);
}

void CEmuCatalogListDlg::SaveSavedPlacement()
{
	if (!GetSafeHwnd() || !::IsWindow(m_hWnd)) return;
	if (IsIconic() || IsZoomed()) return;
	CRect wr;
	GetWindowRect(&wr);
	if (wr.Width() <= 0 || wr.Height() <= 0) return;
	savedata.cemuListX = wr.left;
	savedata.cemuListY = wr.top;
	savedata.cemuListW = wr.Width();
	savedata.cemuListH = wr.Height();
}

void CEmuCatalogListDlg::OnDestroy()
{
	SaveSavedPlacement();
	MpPersistSavedataQuick();
	if (g_cemuCatHelp && ::IsWindow(g_cemuCatHelp->GetSafeHwnd()))
		g_cemuCatHelp->DestroyWindow();
	CCustomBlurDialogBase::OnDestroy();
}

int CEmuCatalogListDlg::PlaySelectedRow()
{
	POSITION pos = m_lc.GetFirstSelectedItemPosition();
	if (!pos) return 0;
	const int row = m_lc.GetNextSelectedItem(pos);
	const CEmuCatListRow* rowp = (const CEmuCatListRow*)m_lc.GetItemData(row);
	if (!rowp || !rowp->ge || !rowp->ge->archive[0]) return 0;
	wchar_t zipPath[CEMU_ZIP_PATH] = {};
	if (rowp->zipPath.GetLength() > 0
		&& GetFileAttributesW(rowp->zipPath) != INVALID_FILE_ATTRIBUTES) {
		wcsncpy_s(zipPath, rowp->zipPath, _TRUNCATE);
	} else if (!CEmuResolveZipForArchive(this, rowp->ge->archive, zipPath, (int)_countof(zipPath))) {
		return 0;
	}
	/* 一覧で選んだ行の音源を優先（複数モードでも再選択 UI を出さない） */
	{
		char tag[CEMU_MODE_TAG] = {};
		if (CEmuModeTagFromEntry(rowp->ge, tag, (int)sizeof(tag)) && tag[0])
			CEmuModePrefSet(zipPath, tag);
	}
	if (!PlCemuAddZipAndPlay(zipPath)) {
		AfxMessageBox(LL14(
			L"プレイリストへの追加に失敗しました。",
			L"Failed to add to the playlist.",
			L"Echec de l'ajout a la liste.",
			L"Aggiunta alla playlist non riuscita.",
			L"No se pudo anadir a la lista.",
			L"플레이리스트 추가에 실패했습니다.",
			L"加入播放列表失败。",
			L"فشلت الإضافة إلى القائمة.",
			L"Не удалось добавить в список.",
			L"Hinzufugen zur Playlist fehlgeschlagen.",
			L"Falha ao adicionar a playlist.",
			L"Toevoegen aan afspeellijst mislukt.",
			L"Nie udalo sie dodac do playlisty.",
			L"Listeye ekleme basarisiz."), MB_ICONWARNING);
		return 0;
	}
	return 1;
}

void CEmuCatalogListDlg::UpdateSortHeader()
{
	m_lc.SetSortState(m_sortCol, m_sortDir);
}

void CEmuCatalogListDlg::OnLvnColumnClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (pResult) *pResult = 0;
	NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	if (!p) return;
	const int col = p->iSubItem;
	if (col < 0 || col > 6) return;
	if (col == m_sortCol) {
		m_sortDir = (m_sortDir + 1) % 3;
		if (m_sortDir == 0)
			m_sortCol = -1;
	} else {
		m_sortCol = col;
		m_sortDir = 1;
	}
	UpdateSortHeader();
	ApplyFilterToList();
}

void CEmuCatalogListDlg::OnNMDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	UNREFERENCED_PARAMETER(pNMHDR);
	if (pResult) *pResult = 0;
	PlaySelectedRow();
}
