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
#include "CImageBase.h"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <shlobj.h>

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

static bool CatRowMatchesFilter(const CString& hayLower, const CString& filterRaw)
{
	CString raw = filterRaw;
	raw.Trim();
	if (raw.IsEmpty()) return true;
	raw.MakeLower();
	raw.Replace(_T(','), _T(' '));
	raw.Replace(_T(';'), _T(' '));
	raw.Replace(_T('/'), _T(' '));

	for (int p = 0; p < raw.GetLength(); ) {
		while (p < raw.GetLength() && raw[p] == _T(' ')) ++p;
		if (p >= raw.GetLength()) break;
		const int start = p;
		while (p < raw.GetLength() && raw[p] != _T(' ')) ++p;
		CString t = raw.Mid(start, p - start);
		if (!t.IsEmpty() && hayLower.Find(t) < 0)
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

static CString CEmuBuildFilterHay(const CEmuCatalog* cat, const CEmuGameEntry* ge,
	const CString& modes)
{
	CString hay;
	hay.Format(L"%hs %hs %hs %hs %s %s",
		ge->archive[0] ? ge->archive : "",
		ge->platform[0] ? ge->platform : "",
		ge->subtype[0] ? ge->subtype : "",
		ge->dataDir[0] ? ge->dataDir : "",
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
		}
	}
	hay.MakeLower();
	return hay;
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
	const CEmuCatListRow* rowp = (const CEmuCatListRow*)GetItemData(row);
	if (!rowp || !rowp->ge) return;
	const CEmuGameEntry* ge = rowp->ge;
	CString modes = rowp->modes;
	CString title = rowp->title;
	out.Format(LL14(
		L"タイトル: %s\nアーカイブ: %hs\n機種: %hs\n音源: %s\ndata: %hs\n"
		L"zip: %s\n（音源はプレイリストの右クリックで切り替え）",
		L"Title: %s\nArchive: %hs\nPlatform: %hs\nSound: %s\ndata: %hs\n"
		L"zip: %s\n(Switch sound from the playlist right-click menu)",
		L"Titre: %s\nArchive: %hs\nPlateforme: %hs\nSon: %s\ndata: %hs\n"
		L"zip: %s\n(Changer le son via le menu contextuel de la liste)",
		L"Titolo: %s\nArchivio: %hs\nPiattaforma: %hs\nSuono: %s\ndata: %hs\n"
		L"zip: %s\n(Cambia il suono dal menu contestuale della playlist)",
		L"Titulo: %s\nArchivo: %hs\nPlataforma: %hs\nSonido: %s\ndata: %hs\n"
		L"zip: %s\n(Cambia el sonido desde el menu contextual de la lista)",
		L"제목: %s\n아카이브: %hs\n기종: %hs\n음원: %s\ndata: %hs\n"
		L"zip: %s\n(음원은 플레이리스트 우클릭으로 전환)",
		L"标题：%s\n归档：%hs\n机种：%hs\n音源：%s\ndata：%hs\n"
		L"zip：%s\n（音源可在播放列表右键菜单切换）",
		L"العنوان: %s\nالأرشيف: %hs\nالمنصة: %hs\nالصوت: %s\ndata: %hs\n"
		L"zip: %s\n(بدّل الصوت من قائمة التشغيل)",
		L"Название: %s\nАрхив: %hs\nПлатформа: %hs\nЗвук: %s\ndata: %hs\n"
		L"zip: %s\n(Звук переключается в меню плейлиста)",
		L"Titel: %s\nArchiv: %hs\nPlattform: %hs\nSound: %s\ndata: %hs\n"
		L"zip: %s\n(Sound per Playlist-Kontextmenu wechseln)",
		L"Titulo: %s\nArquivo: %hs\nPlataforma: %hs\nSom: %s\ndata: %hs\n"
		L"zip: %s\n(Troque o som no menu da playlist)",
		L"Titel: %s\nArchief: %hs\nPlatform: %hs\nGeluid: %s\ndata: %hs\n"
		L"zip: %s\n(Wissel geluid via playlist-snelmenu)",
		L"Tytul: %s\nArchiwum: %hs\nPlatforma: %hs\nDzwiek: %s\ndata: %hs\n"
		L"zip: %s\n(Dzwiek zmienisz w menu playlisty)",
		L"Baslik: %s\nArsiv: %hs\nPlatform: %hs\nSes: %s\ndata: %hs\n"
		L"zip: %s\n(Sesi playlist sag tik menuden degistir)"),
		(LPCTSTR)title,
		ge->archive[0] ? ge->archive : "-",
		ge->platform[0] ? ge->platform : "-",
		modes.IsEmpty() ? L"-" : (LPCTSTR)modes,
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

		const CEmuGameEntry* pick = CEmuPickGroupRepresentative(cat, ge);
		if (!pick) pick = ge;

		CEmuCatListRow row;
		row.ge = pick;
		row.title = pick->name[0] ? pick->name : L"(no name)";
		CEmuStripModeParenFromTitle(row.title);
		row.modes = CEmuJoinArchiveModeTags(cat, pick);
		row.hayLower = CEmuBuildFilterHay(cat, pick, row.modes);
		auto it = stemToPath.find(stem);
		if (it != stemToPath.end())
			row.zipPath = it->second.c_str();
		m_rows.push_back(std::move(row));
	}
	return (int)m_rows.size();
}

void CEmuCatalogListDlg::ApplyFilterToList()
{
	if (!m_lc.GetSafeHwnd()) return;
	m_bFilling = TRUE;
	m_lc.SetRedraw(FALSE);
	m_lc.DeleteAllItems();
	CString filter;
	if (m_filter.GetSafeHwnd())
		m_filter.GetWindowText(filter);

	int row = 0;
	for (size_t i = 0; i < m_rows.size(); i++) {
		CEmuCatListRow& r = m_rows[i];
		if (!CatRowMatchesFilter(r.hayLower, filter)) continue;
		const int idx = m_lc.InsertItem(row, r.title);
		m_lc.SetItemText(idx, 1, CString(r.ge->archive));
		m_lc.SetItemText(idx, 2, CString(r.ge->platform));
		m_lc.SetItemText(idx, 3, r.modes);
		m_lc.SetItemText(idx, 4, CString(r.ge->dataDir));
		m_lc.SetItemData(idx, (DWORD_PTR)&r);
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
