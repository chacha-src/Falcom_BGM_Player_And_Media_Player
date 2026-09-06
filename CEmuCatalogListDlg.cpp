// CEmuCatalogListDlg.cpp — Cemu対応一覧（arcdata.zip）
#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "CEmuCatalogListDlg.h"
#include "PlayList.h"
#include "CEmu/cemu_mgr.h"
#include "CEmu/cemu_catalog.h"
#include "CEmu/cemu_modepref.h"
#include "CImageBase.h"
#include <algorithm>
#include <shlobj.h>

#pragma comment(lib, "shell32.lib")

extern CPlayList* pl;
extern int gameon;
extern int plcnt;

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

static int CEmuSameArchiveGroup(const CEmuGameEntry* a, const CEmuGameEntry* b)
{
	if (!a || !b || !a->archive[0] || !b->archive[0]) return 0;
	if (_stricmp(a->archive, b->archive) != 0) return 0;
	if (a->dataDir[0] && b->dataDir[0] && _stricmp(a->dataDir, b->dataDir) != 0)
		return 0;
	return 1;
}

/* 同一 archive(+dataDir) の先頭エントリか。音源違いを1行にまとめる。 */
static int CEmuIsFirstInArchiveGroup(const CEmuCatalog* cat, int index)
{
	if (!cat || index < 0 || index >= cat->count) return 0;
	const CEmuGameEntry* ge = cat->entry[index];
	if (!ge || !ge->archive[0]) return 0;
	for (int j = 0; j < index; j++) {
		const CEmuGameEntry* prev = cat->entry[j];
		if (CEmuSameArchiveGroup(prev, ge))
			return 0;
	}
	return 1;
}

static bool CatGroupMatchesFilter(const CEmuCatalog* cat, const CEmuGameEntry* ge,
	const CString& filterRaw)
{
	if (!ge) return false;
	CString raw = filterRaw;
	raw.Trim();
	if (raw.IsEmpty()) return true;
	raw.MakeLower();
	raw.Replace(_T(','), _T(' '));
	raw.Replace(_T(';'), _T(' '));
	raw.Replace(_T('/'), _T(' '));

	CString hay;
	hay.Format(L"%hs %hs %hs %hs %s",
		ge->archive[0] ? ge->archive : "",
		ge->platform[0] ? ge->platform : "",
		ge->subtype[0] ? ge->subtype : "",
		ge->dataDir[0] ? ge->dataDir : "",
		ge->name[0] ? ge->name : L"");
	if (cat && ge->archive[0]) {
		CEmuArchiveMode modes[CEMU_MODE_MAX];
		const int n = CEmuCatalogListArchiveModes(cat, ge->archive, ge->dataDir, NULL,
			modes, CEMU_MODE_MAX);
		for (int i = 0; i < n; i++) {
			if (modes[i].tag[0]) {
				hay += L' ';
				hay += modes[i].tag;
			}
			if (modes[i].subtype[0]) {
				hay += L' ';
				hay += modes[i].subtype;
			}
			if (modes[i].entryIndex >= 0 && modes[i].entryIndex < cat->count) {
				const CEmuGameEntry* e = cat->entry[modes[i].entryIndex];
				if (e && e->name[0]) {
					hay += L' ';
					hay += e->name;
				}
			}
		}
	}
	hay.MakeLower();

	for (int p = 0; p < raw.GetLength(); ) {
		while (p < raw.GetLength() && raw[p] == _T(' ')) ++p;
		if (p >= raw.GetLength()) break;
		const int start = p;
		while (p < raw.GetLength() && raw[p] != _T(' ')) ++p;
		CString t = raw.Mid(start, p - start);
		if (!t.IsEmpty() && hay.Find(t) < 0)
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

static const CEmuGameEntry* CEmuPickGroupRepresentative(const CEmuCatalog* cat,
	const CEmuGameEntry* ge)
{
	if (!cat || !ge || !ge->archive[0]) return ge;
	const CEmuGameEntry* best = CEmuCatalogFindArchiveForZipMode(cat, ge->archive,
		ge->dataDir, NULL, NULL);
	return best ? best : ge;
}

} // namespace

/* PlayList.cpp から公開 */
bool PlCemuAddZipAndPlay(LPCTSTR zipPhysical);

IMPLEMENT_DYNAMIC(CEmuCatListCtrl, CCustomListCtrl)
BEGIN_MESSAGE_MAP(CEmuCatListCtrl, CCustomListCtrl)
END_MESSAGE_MAP()

void CEmuCatListCtrl::BuildToolTipText(int row, int col, CString& out)
{
	UNREFERENCED_PARAMETER(col);
	out.Empty();
	if (row < 0) return;
	const DWORD_PTR data = GetItemData(row);
	const CEmuGameEntry* ge = (const CEmuGameEntry*)data;
	if (!ge) return;
	CEmuMgr* mgr = CEmuMgrGet();
	const CEmuCatalog* cat = mgr ? &mgr->catalog : NULL;
	CString modes = CEmuJoinArchiveModeTags(cat, ge);
	CString title = ge->name[0] ? ge->name : L"(no name)";
	CEmuStripModeParenFromTitle(title);
	out.Format(LL14(
		L"タイトル: %s\nアーカイブ: %hs\n機種: %hs\n音源: %s\ndata: %hs\n"
		L"（音源はプレイリストの右クリックで切り替え）",
		L"Title: %s\nArchive: %hs\nPlatform: %hs\nSound: %s\ndata: %hs\n"
		L"(Switch sound from the playlist right-click menu)",
		L"Titre: %s\nArchive: %hs\nPlateforme: %hs\nSon: %s\ndata: %hs\n"
		L"(Changer le son via le menu contextuel de la liste)",
		L"Titolo: %s\nArchivio: %hs\nPiattaforma: %hs\nSuono: %s\ndata: %hs\n"
		L"(Cambia il suono dal menu contestuale della playlist)",
		L"Titulo: %s\nArchivo: %hs\nPlataforma: %hs\nSonido: %s\ndata: %hs\n"
		L"(Cambia el sonido desde el menu contextual de la lista)",
		L"제목: %s\n아카이브: %hs\n기종: %hs\n음원: %s\ndata: %hs\n"
		L"(음원은 플레이리스트 우클릭으로 전환)",
		L"标题：%s\n归档：%hs\n机种：%hs\n音源：%s\ndata：%hs\n"
		L"（音源可在播放列表右键菜单切换）",
		L"العنوان: %s\nالأرشيف: %hs\nالمنصة: %hs\nالصوت: %s\ndata: %hs\n"
		L"(بدّل الصوت من قائمة التشغيل)",
		L"Название: %s\nАрхив: %hs\nПлатформа: %hs\nЗвук: %s\ndata: %hs\n"
		L"(Звук переключается в меню плейлиста)",
		L"Titel: %s\nArchiv: %hs\nPlattform: %hs\nSound: %s\ndata: %hs\n"
		L"(Sound per Playlist-Kontextmenu wechseln)",
		L"Titulo: %s\nArquivo: %hs\nPlataforma: %hs\nSom: %s\ndata: %hs\n"
		L"(Troque o som no menu da playlist)",
		L"Titel: %s\nArchief: %hs\nPlatform: %hs\nGeluid: %s\ndata: %hs\n"
		L"(Wissel geluid via playlist-snelmenu)",
		L"Tytul: %s\nArchiwum: %hs\nPlatforma: %hs\nDzwiek: %s\ndata: %hs\n"
		L"(Dzwiek zmienisz w menu playlisty)",
		L"Baslik: %s\nArsiv: %hs\nPlatform: %hs\nSes: %s\ndata: %hs\n"
		L"(Sesi playlist sag tik menuden degistir)"),
		(LPCTSTR)title,
		ge->archive[0] ? ge->archive : "-",
		ge->platform[0] ? ge->platform : "-",
		modes.IsEmpty() ? L"-" : (LPCTSTR)modes,
		ge->dataDir[0] ? ge->dataDir : "-");
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
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_DESTROY()
cmn(CEmuCatalogListDlg);
void CEmuCatalogListDlg::ShowModal(CWnd* pParent)
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
	CEmuCatalogListDlg dlg(pParent);
	dlg.DoModal();
}

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
			L"ダブルクリックでプレイリストへ追加して再生（同一zipの音源違いは1行。切替はプレイリストの右クリック）",
			L"Double-click to add and play (sound variants share one row; switch via playlist right-click)",
			L"Double-clic pour ajouter et lire (variantes son sur une ligne ; menu de la liste)",
			L"Doppio clic per aggiungere e riprodurre (varianti audio in una riga; menu playlist)",
			L"Doble clic para anadir y reproducir (variantes de sonido en una fila; menu de lista)",
			L"더블클릭으로 추가·재생(같은 zip 음원은 한 줄. 전환은 플레이리스트 우클릭)",
			L"双击加入并播放（同 zip 音源合并一行；播放列表右键切换）",
			L"نقر مزدوج للإضافة والتشغيل (صف واحد للصوت؛ التبديل من القائمة)",
			L"Двойной клик — в список (варианты звука в одной строке; меню плейлиста)",
			L"Doppelklick: abspielen (Sound-Varianten in einer Zeile; Playlist-Menü)",
			L"Duplo clique: tocar (variantes de som numa linha; menu da playlist)",
			L"Dubbelklik: spelen (geluidsvarianten op één regel; playlist-menu)",
			L"Dwuklik: odtworz (warianty dzwieku w jednym wierszu; menu playlisty)",
			L"Cift tik: cal (ses varyantlari tek satir; playlist sag tik)"));
	if (m_filterLbl.GetSafeHwnd())
		m_filterLbl.SetWindowText(LL14(L"絞り込み", L"Filter", L"Filtrer", L"Filtro", L"Filtro", L"필터", L"筛选", L"تصفية", L"Фильтр", L"Filter", L"Filtro", L"Filter", L"Filtr", L"Filtre"));
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();
	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	m_tooltip.AddTool(&m_ok, LL14(L"閉じます", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);

	if (m_lc.GetSafeHwnd()) {
		m_lc.SetExtendedStyle(m_lc.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
		m_lc.EnableToolTips(TRUE);
		m_lc.InsertColumn(0, LL14(L"タイトル", L"Title", L"Titre", L"Titolo", L"Titulo", L"제목", L"标题", L"العنوان", L"Название", L"Titel", L"Titulo", L"Titel", L"Tytul", L"Baslik"), LVCFMT_LEFT, 180);
		m_lc.InsertColumn(1, LL14(L"アーカイブ", L"Archive", L"Archive", L"Archivio", L"Archivo", L"아카이브", L"归档", L"الأرشيف", L"Архив", L"Archiv", L"Arquivo", L"Archief", L"Archiwum", L"Arsiv"), LVCFMT_LEFT, 100);
		m_lc.InsertColumn(2, LL14(L"機種", L"Platform", L"Plateforme", L"Piattaforma", L"Plataforma", L"기종", L"机种", L"المنصة", L"Платформа", L"Plattform", L"Plataforma", L"Platform", L"Platforma", L"Platform"), LVCFMT_LEFT, 80);
		m_lc.InsertColumn(3, LL14(L"音源", L"Sound", L"Son", L"Suono", L"Sonido", L"음원", L"音源", L"الصوت", L"Звук", L"Sound", L"Som", L"Geluid", L"Dzwiek", L"Ses"), LVCFMT_LEFT, 120);
		m_lc.InsertColumn(4, L"data", LVCFMT_LEFT, 60);
	}

	CEmuMgrEnsureCatalog(CEmuMgrGet());
	FillList();

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

void CEmuCatalogListDlg::FillList()
{
	if (!m_lc.GetSafeHwnd()) return;
	m_bFilling = TRUE;
	m_lc.SetRedraw(FALSE);
	m_lc.DeleteAllItems();
	CString filter;
	if (m_filter.GetSafeHwnd())
		m_filter.GetWindowText(filter);
	CEmuMgr* mgr = CEmuMgrGet();
	CEmuMgrEnsureCatalog(mgr);
	const CEmuCatalog* cat = mgr ? &mgr->catalog : NULL;
	int row = 0;
	for (int i = 0; cat && i < cat->count; i++) {
		const CEmuGameEntry* ge = cat->entry[i];
		if (!ge || !ge->archive[0]) continue;
		/* OPN/OPNA/MIDI など同一 zip の音源違いは1行にまとめる。 */
		if (!CEmuIsFirstInArchiveGroup(cat, i)) continue;
		if (!CatGroupMatchesFilter(cat, ge, filter)) continue;
		const CEmuGameEntry* pick = CEmuPickGroupRepresentative(cat, ge);
		if (!pick) pick = ge;
		CString title = pick->name[0] ? pick->name : L"(no name)";
		CEmuStripModeParenFromTitle(title);
		const int idx = m_lc.InsertItem(row, title);
		CString a = pick->archive;
		CString p = pick->platform;
		CString s = CEmuJoinArchiveModeTags(cat, pick);
		CString d = pick->dataDir;
		m_lc.SetItemText(idx, 1, a);
		m_lc.SetItemText(idx, 2, p);
		m_lc.SetItemText(idx, 3, s);
		m_lc.SetItemText(idx, 4, d);
		m_lc.SetItemData(idx, (DWORD_PTR)pick);
		row++;
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
	if (m_lc.GetHeaderCtrl()->GetItemCount() < 5) return;
	CRect rc;
	m_lc.GetClientRect(&rc);
	const int total = rc.Width();
	const int archW = 110, platW = 80, soundW = 120, dataW = 60;
	int titleW = total - archW - platW - soundW - dataW;
	if (titleW < 120) titleW = 120;
	m_lc.SetColumnWidth(0, titleW);
	m_lc.SetColumnWidth(1, archW);
	m_lc.SetColumnWidth(2, platW);
	m_lc.SetColumnWidth(3, soundW);
	m_lc.SetColumnWidth(4, dataW);
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
void CEmuCatalogListDlg::OnBnClickedOk() { EndDialog(IDOK); }
void CEmuCatalogListDlg::OnEnChangeFilter() { if (!m_bFilling) FillList(); }

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
	const CEmuGameEntry* ge = (const CEmuGameEntry*)m_lc.GetItemData(row);
	if (!ge || !ge->archive[0]) return 0;
	wchar_t zipPath[CEMU_ZIP_PATH] = {};
	if (!CEmuResolveZipForArchive(this, ge->archive, zipPath, (int)_countof(zipPath)))
		return 0;
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

void CEmuCatalogListDlg::OnNMDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	UNREFERENCED_PARAMETER(pNMHDR);
	if (pResult) *pResult = 0;
	PlaySelectedRow();
}
